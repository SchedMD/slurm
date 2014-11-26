/*****************************************************************************\
 *  fair_tree.c - Fair Tree fairshare algorithm for Slurm
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

#ifndef   _ISOC99_SOURCE
#  define _ISOC99_SOURCE /* INFINITY */
#endif

#include <math.h>
#include <stdlib.h>

#include "fair_tree.h"

static void _ft_decay_apply_new_usage(struct job_record *job, time_t *start);
static void _apply_priority_fs(void);

/* Fair Tree code called from the decay thread loop */
extern void fair_tree_decay(List jobs, time_t start)
{
	slurmctld_lock_t job_write_lock =
		{ NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	assoc_mgr_lock_t locks =
		{ WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };

	/* apply decayed usage */
	lock_slurmctld(job_write_lock);
	list_for_each(jobs, (ListForF) _ft_decay_apply_new_usage, &start);
	unlock_slurmctld(job_write_lock);

	/* calculate fs factor for associations */
	assoc_mgr_lock(&locks);
	_apply_priority_fs();
	assoc_mgr_unlock(&locks);

	/* assign job priorities */
	lock_slurmctld(job_write_lock);
	list_for_each(jobs, (ListForF) decay_apply_weighted_factors, &start);
	unlock_slurmctld(job_write_lock);
}


/* In Fair Tree, usage_efctv is the normalized usage within the account */
static void _ft_set_assoc_usage_efctv(slurmdb_association_rec_t *assoc)
{
	slurmdb_association_rec_t *parent = assoc->usage->fs_assoc_ptr;

	if (!parent || !parent->usage->usage_raw) {
		assoc->usage->usage_efctv = 0L;
		return;
	}

	assoc->usage->usage_efctv =
		assoc->usage->usage_raw / parent->usage->usage_raw;
}


/* Apply usage with decay factor. Call standard functions */
static void _ft_decay_apply_new_usage(struct job_record *job, time_t *start)
{
	if (!decay_apply_new_usage(job, start))
		return;

	/* Priority 0 is reserved for held jobs. Also skip priority
	 * calculation for non-pending jobs. */
	if ((job->priority == 0) || !IS_JOB_PENDING(job))
		return;

	set_priority_factors(*start, job);
	last_job_update = time(NULL);
}


static void _ft_debug(slurmdb_association_rec_t *assoc,
		      uint16_t assoc_level, bool tied)
{
	int spaces;
	char *name;
	int tie_char_count = tied ? 1 : 0;

	spaces = (assoc_level + 1) * 4;
	name = assoc->user ? assoc->user : assoc->acct;

	if (assoc->shares_raw == SLURMDB_FS_USE_PARENT) {
		info("%*s%.*s%s (%s):  parent",
		     spaces,
		     "",
		     tie_char_count,
		     "=",
		     name,
		     assoc->acct);
	} else {
		info("%*s%.*s%s (%s):  %.20Lf",
		     spaces,
		     "",
		     tie_char_count,
		     "=",
		     name,
		     assoc->acct,
		     assoc->usage->level_fs);
	}

}


/* Sort so that higher level_fs values are first in the list */
static int _cmp_level_fs(const void *x,
			 const void *y)
{
	/* We sort based on the following critereon:
	 *  1. level_fs value
	 *  2. Prioritize users over accounts (required for tie breakers when
	 *     comparing users and accounts)
	 */
	slurmdb_association_rec_t **a = (slurmdb_association_rec_t **)x;
	slurmdb_association_rec_t **b = (slurmdb_association_rec_t **)y;

	/* 1. level_fs value */
	if ((*a)->usage->level_fs != (*b)->usage->level_fs)
		return (*a)->usage->level_fs < (*b)->usage->level_fs ? 1 : -1;

	/* 2. Prioritize users over accounts */

	/* a and b are both users or both accounts */
	if (!(*a)->user == !(*b)->user)
		return 0;

	/* -1 if a is user, 1 if b is user */
	return (*a)->user ? -1 : 1;
}


/* Calculate LF = S / U for an association.
 *
 * U is usage_raw / parent's usage_raw.
 * S is shares_raw / level_shares
 *
 * The range of values is 0.0 .. INFINITY.
 * If LF > 1.0, the association is under-served.
 * If LF < 1.0, the association is over-served.
 */
static void _calc_assoc_fs(slurmdb_association_rec_t *assoc)
{
	long double U; /* long double U != long W */
	long double S;

	_ft_set_assoc_usage_efctv(assoc);

	/* Fair Tree doesn't use usage_norm but we will set it anyway */
	set_assoc_usage_norm(assoc);

	U = assoc->usage->usage_efctv;
	S = assoc->usage->shares_norm;

	/* Users marked as USE_PARENT are assigned the maximum level_fs so they
	 * rank highest in their account, subject to ties.
	 * Accounts marked as USE_PARENT do not use level_fs */
	if (assoc->shares_raw == SLURMDB_FS_USE_PARENT) {
		if (assoc->user)
			assoc->usage->level_fs = INFINITY;
		else
			assoc->usage->level_fs = (long double) NO_VAL;
		return;
	}

	/* If S is 0, the assoc is assigned the lowest possible LF value. If
	 * U==0 && S!=0, assoc is assigned the highest possible value, infinity.
	 * Checking for U==0 then setting level_fs=INFINITY is not the same
	 * since you would still have to check for S==0 then set level_fs=0.
	 *
	 * NOT A BUG: U can be 0. The result is infinity, a valid value. */
	if (S == 0L)
		assoc->usage->level_fs = 0L;
	else
		assoc->usage->level_fs = S / U;
}


static slurmdb_association_rec_t** _append_children_to_array(
	List list, slurmdb_association_rec_t** merged,
	size_t *child_count)
{
	ListIterator itr;
	slurmdb_association_rec_t *next;
	size_t i = *child_count;
	*child_count += list_count(list);

	merged = xrealloc(merged, sizeof(slurmdb_association_rec_t*)
			  * (*child_count + 1));

	itr = list_iterator_create(list);
	while ((next = list_next(itr)))
		merged[i++] = next;
	list_iterator_destroy(itr);

	return merged;
}


static size_t _count_tied_accounts(slurmdb_association_rec_t** assocs,
				   size_t i)
{
	slurmdb_association_rec_t* next_assoc;
	slurmdb_association_rec_t* assoc = assocs[i];
	size_t tied_accounts = 0;
	while ((next_assoc = assocs[++i])) {
		if (!next_assoc->user)
			break;
		if (assoc->usage->level_fs != next_assoc->usage->level_fs)
			break;
		tied_accounts++;
	}
	return tied_accounts;
}


static slurmdb_association_rec_t** _merge_accounts(
	slurmdb_association_rec_t** siblings,
	size_t begin, size_t end, uint16_t assoc_level)
{
	size_t i;
	size_t child_count = 0;
	/* merged is a null terminated array */
	slurmdb_association_rec_t** merged = (slurmdb_association_rec_t **)
		xmalloc(sizeof(slurmdb_association_rec_t *));
	merged[0] = NULL;

	for (i = begin; i <= end; i++) {
		List children = siblings[i]->usage->children_list;

		if (priority_debug && i > begin)
			_ft_debug(siblings[i], assoc_level, true);

		if (!children || list_is_empty(children)) {
			continue;
		}

		merged = _append_children_to_array(children, merged,
						   &child_count);
	}
	return merged;
}


/* Calculate fairshare for each child then sort children by fairshare value
 * (level_fs). Once they are sorted, operate on each child in sorted order.
 * This portion of the tree is now sorted and users are given a fairshare value
 * based on the order they are operated on. The basic equation is
 * (rank / g_user_assoc_count), though ties are allowed. The rank is decremented
 * for each user that is encountered.
 */
static void _calc_tree_fs(slurmdb_association_rec_t** siblings,
			  uint16_t assoc_level, uint32_t *rank, uint32_t *i,
			  bool account_tied)
{
	slurmdb_association_rec_t *assoc = NULL;
	long double prev_level_fs = (long double) NO_VAL;
	bool tied = false;
	size_t ndx;

	/* Calculate level_fs for each child */
	for (ndx = 0; (assoc = siblings[ndx]); ndx++)
		_calc_assoc_fs(assoc);

	/* Sort children by level_fs */
	qsort(siblings, ndx, sizeof(slurmdb_association_rec_t *),
	      _cmp_level_fs);

	/* Iterate through children in sorted order. If it's a user, calculate
	 * fs_factor, otherwise recurse. */
	for (ndx = 0; (assoc = siblings[ndx]); ndx++) {
		if (account_tied) {
			tied = true;
			account_tied = false;
		} else {
			tied = prev_level_fs == assoc->usage->level_fs;
		}

		if (priority_debug)
			_ft_debug(assoc, assoc_level, tied);
		if (assoc->user) {
			if (!tied)
				*rank = *i;

			/* Set the final fairshare factor for this user */
			assoc->usage->fs_factor =
				*rank / (double) g_user_assoc_count;
			(*i)--;
		} else {
			slurmdb_association_rec_t** children;
			size_t merge_count =
				_count_tied_accounts(siblings, ndx);

			/* Merging does not affect child level_fs calculations
			 * since the necessary information is stored on each
			 * assoc's usage struct */
			children = _merge_accounts(siblings, ndx,
						   ndx + merge_count,
						   assoc_level);

			_calc_tree_fs(children, assoc_level + 1, rank, i, tied);

			/* Skip over any merged accounts */
			ndx += merge_count;

			xfree(children);
		}
		prev_level_fs = assoc->usage->level_fs;
	}

}


/* Start fairshare calculations at root. Call assoc_mgr_lock before this. */
static void _apply_priority_fs(void)
{
	slurmdb_association_rec_t** children = NULL;
	uint32_t rank = g_user_assoc_count;
	uint32_t i = rank;
	size_t child_count = 0;

	if (priority_debug)
		info("Fair Tree fairshare algorithm, starting at root:");

	assoc_mgr_root_assoc->usage->level_fs = 1L;

	/* _calc_tree_fs requires an array instead of List */
	children = _append_children_to_array(
		assoc_mgr_root_assoc->usage->children_list,
		children,
		&child_count);

	_calc_tree_fs(children, 0, &rank, &i, false);

	xfree(children);
}
