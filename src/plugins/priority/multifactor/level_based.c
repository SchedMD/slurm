/*****************************************************************************\
 *  level_based.c - level_based slurm multifactor algorithm
 *****************************************************************************
 *
 *  Copyright (C) 2014 Brigham Young University
 *  Authors: Ryan Cox <ryan_cox@byu.edu>, Levi Morrison <levi_morrison@byu.edu>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <math.h>

#include "src/common/slurm_priority.h"
#include "src/slurmctld/locks.h"
#include "src/common/assoc_mgr.h"
#include "level_based.h"
#include "priority_multifactor.h"


/* How many levels to care about */
static uint16_t priority_levels;

/* How many bits available for each level */
static uint32_t bucket_width_in_bits;

/* Unused bucket bits (e.g. 64 % priority_levels) */
static uint32_t unused_bucket_bits;

/* Maximum value that can be stored in a bucket */
static uint64_t bucket_max;


void _level_based_calc_children_fs(List children_list,
				   List users,
				   uint16_t assoc_level);
uint64_t _level_based_calc_level_fs(slurmdb_association_rec_t *assoc,
				    uint16_t assoc_level);
static void _level_based_decay_apply_new_usage(struct job_record *job_ptr,
					       time_t *start_time_ptr);
static void _level_based_apply_priority_fs(void);


extern void level_based_init(void) {
	priority_levels = slurm_get_priority_levels();
	/* calculate how many bits per level. truncate if necessary */
	bucket_width_in_bits = 64 / priority_levels;
	unused_bucket_bits = 64 % priority_levels;
	bucket_max = UINT64_MAX >> (64 - bucket_width_in_bits);
}


/* LEVEL_BASED code called from the decay thread loop */
extern void level_based_decay(List job_list, time_t start_time)
{
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };

	/* apply decayed usage */
	lock_slurmctld(job_write_lock);
	list_for_each(job_list,
		      (ListForF) _level_based_decay_apply_new_usage,
		      &start_time);
	unlock_slurmctld(job_write_lock);

	/* calculate priority for associations */
	assoc_mgr_lock(&locks);
	_level_based_apply_priority_fs();
	assoc_mgr_unlock(&locks);

	/* assign job priorities */
	lock_slurmctld(job_write_lock);
	list_for_each(job_list,
		      (ListForF) decay_apply_weighted_factors,
		      &start_time);
	unlock_slurmctld(job_write_lock);
}


/* Normalize the assoc's usage for use in usage_efctv:
 * from:  0.0 to parent->usage->usage_raw
 * to:    0.0 to 1.0
 *
 * In LEVEL_BASED, usage_efctv is the normalized usage within the account
 */
extern double level_based_calc_assoc_usage(slurmdb_association_rec_t *assoc)
{
	double norm = 0.0l;
	slurmdb_association_rec_t *parent = assoc->usage->fs_assoc_ptr;

	if (parent && parent->usage->usage_raw)
		norm = NORMALIZE_VALUE(
			assoc->usage->usage_raw,
			0.0L, (long double) parent->usage->usage_raw,
			0.0L, 1.0L);

	return norm;
}


/* Apply usage with decay factor. Call standard functions */
static void _level_based_decay_apply_new_usage(
	struct job_record *job_ptr,
	time_t *start_time_ptr)
{
	if (!decay_apply_new_usage(job_ptr, start_time_ptr))
		return;
	/*
	 * Priority 0 is reserved for held jobs. Also skip priority
	 * calculation for non-pending jobs.
	 */
	if ((job_ptr->priority == 0) || !IS_JOB_PENDING(job_ptr))
		return;

	set_priority_factors(*start_time_ptr, job_ptr);
	last_job_update = time(NULL);
}


static void _level_based_calc_children_fs_priority_debug(
	uint64_t priority_fs_raw,
	uint64_t level_fs_raw,
	slurmdb_association_rec_t *assoc,
	uint16_t assoc_level)
{
	int spaces;
	char *name;

	if (!priority_debug)
		return;

	spaces = (assoc_level + 1) * 4;
	name = assoc->user ? assoc->user : assoc->acct;

	debug2("%*s0x%016"PRIX64" | 0x%016"PRIX64" (%s)",
	       spaces,
	       "",
	       priority_fs_raw,
	       level_fs_raw,
	       name);
	if (assoc->user)
		debug2("%*s%18s = 0x%016"PRIX64" (%s)",
		       spaces,
		       "",
		       "",
		       priority_fs_raw | level_fs_raw,
		       assoc->user);

}


/* Calculate F=2**(-Ueff/S) at the current level. Shift the result based on
 * depth in the association tree and the bucket size.
 */
uint64_t _level_based_calc_level_fs(slurmdb_association_rec_t *assoc,
					   uint16_t assoc_level)
{
	uint64_t level_fs = 0;
	long double level_ratio = 0.0L;
	long double shares_adj = 0.0L;

	if (assoc->shares_raw == SLURMDB_FS_USE_PARENT) {
		if(assoc->user)
			level_fs = 1.0L;
		else
			return 0;
	} else if (assoc->usage->shares_norm) {

		/* This function normalizes shares to be between 0.1 and 1.0;
		 * this range fares much better than 0.0 to 1.0 when used in
		 * the denominator of the fairshare calculation:
		 *   2**(-UsageEffective / Shares)
		 *
		 * Compare these two:
		 * http://www.wolframalpha.com/input/?i=2%5E-%28u%2Fs%29%2C+u+from+0+to+1%2C+s+from+.1+to+1
		 * http://www.wolframalpha.com/input/?i=2%5E-%28u%2Fs%29%2C+u+from+0+to+1%2C+s+from+0+to+1
		 */
		shares_adj = NORMALIZE_VALUE(assoc->usage->shares_norm,
					     0.0l, 1.0l,
					     0.1L, 1.0L);
		level_ratio = assoc->usage->usage_efctv / shares_adj;
	}

	/* reserve 0 for special casing */
	level_fs = NORMALIZE_VALUE(powl(2L, -level_ratio),
				   0.0L, 1.0L,
				   1, bucket_max);


	level_fs <<= ((priority_levels - assoc_level - 1)
		      * bucket_width_in_bits
		      + unused_bucket_bits);
	return level_fs;
}


/* Calculate and set priority_fs_raw at each level then recurse to children.
 * Also, append users to user list while we are traversing.
 * This function calls and is called by _level_based_calc_children_fs().
 */
static void _level_based_calc_assoc_fs(
	List users,
	slurmdb_association_rec_t *assoc,
	uint16_t assoc_level)
{
	const uint64_t priority_fs_raw =
		assoc->usage->parent_assoc_ptr->usage->priority_fs_raw;
	uint64_t level_fs = 0;

	/* Calculate the fairshare factor at this level, properly shifted
	 *
	 * If assoc_level >= priority_levels, the tree is deeper than
	 * priority_levels; you are done with priority calculations but still
	 * need to set the values on each child.
	 */
	if (assoc_level < priority_levels)
		level_fs = _level_based_calc_level_fs(assoc, assoc_level);

	/* Bitwise OR the level fairshare factor with the parent's. For a
	 * user, this is the final fairshare factor that is used in sorting
	 * and ranking.
	 */
	assoc->usage->priority_fs_raw = priority_fs_raw | level_fs;

	/* Found a user, add to users list */
	if (assoc->user)
		list_append(users, assoc);

	_level_based_calc_children_fs_priority_debug(
		priority_fs_raw, level_fs, assoc, assoc_level);

	/* If USE_PARENT, set priority_fs_raw equal to parent then work on
	 * children */
	if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
		_level_based_calc_children_fs(
			assoc->usage->children_list, users, assoc_level);
	else if (!assoc->user)
		/* If this is an account, descend to child accounts */
		_level_based_calc_children_fs(
			assoc->usage->children_list,
			users,
			assoc_level + 1
			);
}


/* Call _level_based_calc_assoc_fs() on each child, if any. This function will
 * be called again by _level_based_calc_assoc_fs() for child accounts (not
 * users), thus making it recursive.
 */
void _level_based_calc_children_fs(List children_list,
				   List users,
				   uint16_t assoc_level)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;

	if (!children_list || !list_count(children_list))
		return;

	itr = list_iterator_create(children_list);
	while ((assoc = list_next(itr)))
		_level_based_calc_assoc_fs(
			users, assoc, assoc_level);
	list_iterator_destroy(itr);
}


/* Sort so that higher priority_fs_raw values are first in the list */
int _level_based_sort_priority_fs(slurmdb_association_rec_t **x,
				  slurmdb_association_rec_t **y)
{
	uint64_t a = (*x)->usage->priority_fs_raw;
	uint64_t b = (*y)->usage->priority_fs_raw;

	if (a < b)
		return 1;
	else if (b < a)
		return -1;
	else
		return 0;
}


/* Iterate through sorted list of users. Apply priorities based on their rank,
 * allowing for duplicate rankings if priority_fs_raw is equal for users
 * (i vs rank).
 */
void _level_based_apply_rank(List users)
{
	ListIterator itr = list_iterator_create(users);
	slurmdb_association_rec_t *assoc;
	int count = list_count(users);
	int i = count - 1;
	int rank = count - 1;
	/* priority_fs_raw can't be equal to 0 due to normalization in
	 * _level_based_calc_level_fs */
	uint64_t prev_priority_fs_raw = 0;

	while ((assoc = list_next(itr))) {
		xassert(assoc->usage->priority_fs_raw != 0);

		/* If same as prev, rank stays the same. This allows for
		 * rankings like 7,6,5,5,5,2,1,0 */
		if(prev_priority_fs_raw != assoc->usage->priority_fs_raw)
			rank = i;
		assoc->usage->priority_fs_ranked =
			NORMALIZE_VALUE(rank, 0.0, (long double) count,
					0, UINT64_MAX);
		if (priority_debug)
			info("Fairshare for user %s in acct %s: ranked "
			     "%d/%d (0x%016"PRIX64")",
			     assoc->user, assoc->acct, rank, count,
			     assoc->usage->priority_fs_ranked);
		i--;
		prev_priority_fs_raw = assoc->usage->priority_fs_raw;
	}

	list_iterator_destroy(itr);
}


/* Calculate fairshare for associations, sort users by priority_fs_raw, then
 * use the rank in the sorted list as a user's fs factor
 *
 * Call assoc_mgr_lock before this */
static void _level_based_apply_priority_fs(void)
{
	List users = list_create(NULL);

	if (priority_debug) {
		debug2("LEVEL_BASED Fairshare, starting at root:");
		debug2("%s | %s", "parent_fs", "current_fs");
	}
	assoc_mgr_root_assoc->usage->priority_fs_raw = 0;
	assoc_mgr_root_assoc->usage->priority_fs_ranked = 0;

	/* set priority_fs_raw on each assoc and add users to List users */
	_level_based_calc_children_fs(
		assoc_mgr_root_assoc->usage->children_list,
		users,
		0);

	/* sort users by priority_fs_raw */
	list_sort(users, (ListCmpF) _level_based_sort_priority_fs);

	/* set user ranking based on their position in the sorted list */
	_level_based_apply_rank(users);

	list_destroy(users);
}
