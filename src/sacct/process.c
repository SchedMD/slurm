/*****************************************************************************\
 *  process.c - process functions for sacct
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "sacct.h"

static void _aggregate_tres_usage_stats_internal(char **dest_tres_max,
						 char **dest_tres_max_nodeid,
						 char **dest_tres_max_taskid,
						 char *from_tres_max,
						 char *from_tres_max_nodeid,
						 char *from_tres_max_taskid)
{
	List dest_tres_list = NULL, from_tres_list = NULL;
	ListIterator itr;
	slurmdb_tres_rec_t *dest_tres_rec, *from_tres_rec;
	uint32_t flags;

	xassert(dest_tres_max);
	xassert(dest_tres_max_nodeid);
	xassert(dest_tres_max_taskid);

	flags = TRES_STR_FLAG_REMOVE;

	/* We always want these lists to exist */
	dest_tres_list = list_create(slurmdb_destroy_tres_rec);
	from_tres_list = list_create(slurmdb_destroy_tres_rec);

	slurmdb_tres_list_from_string(&dest_tres_list, *dest_tres_max, flags);
	slurmdb_tres_list_from_string(&from_tres_list, from_tres_max, flags);

	itr = list_iterator_create(from_tres_list);
	while ((from_tres_rec = list_next(itr))) {
		if (from_tres_rec->count == INFINITE64)
			continue;

		if (!(dest_tres_rec = list_find_first(dest_tres_list,
						      slurmdb_find_tres_in_list,
						      &from_tres_rec->id))) {
			list_remove(itr);
			list_append(dest_tres_list, from_tres_rec);
		} else {
			if (dest_tres_rec->count == INFINITE64 ||
			    (dest_tres_rec->count < from_tres_rec->count)) {
				dest_tres_rec->count = from_tres_rec->count;

				/* overload rec_count to be the nodeid */
				dest_tres_rec->rec_count =
					slurmdb_find_tres_count_in_string(
						from_tres_max_nodeid,
						from_tres_rec->id);
				/* overload alloc_secs to be the taskid */
				dest_tres_rec->alloc_secs =
					slurmdb_find_tres_count_in_string(
						from_tres_max_taskid,
						from_tres_rec->id);
			}
		}
	}
	list_iterator_destroy(itr);

	/* make the string now from the list */
	flags = TRES_STR_FLAG_SIMPLE + TRES_STR_FLAG_REMOVE;
	xfree(*dest_tres_max);
	*dest_tres_max = slurmdb_make_tres_string(dest_tres_list, flags);

	itr = list_iterator_create(dest_tres_list);

	/* Now process the nodeid */
	xfree(*dest_tres_max_nodeid);
	while ((dest_tres_rec = list_next(itr)))
		dest_tres_rec->count = (uint64_t)dest_tres_rec->rec_count;
	*dest_tres_max_nodeid = slurmdb_make_tres_string(dest_tres_list, flags);

	/* Now process the taskid */
	xfree(*dest_tres_max_taskid);
	list_iterator_reset(itr);
	while ((dest_tres_rec = list_next(itr)))
		dest_tres_rec->count = dest_tres_rec->alloc_secs;
	*dest_tres_max_taskid = slurmdb_make_tres_string(dest_tres_list, flags);

	list_iterator_destroy(itr);

	FREE_NULL_LIST(dest_tres_list);
	FREE_NULL_LIST(from_tres_list);
}

static void _aggregate_tres_usage_stats(slurmdb_stats_t *dest,
					slurmdb_stats_t *from)
{
	uint32_t flags;

	if (!dest->tres_usage_in_max) {
		dest->tres_usage_in_ave = xstrdup(from->tres_usage_in_ave);
		dest->tres_usage_in_max = xstrdup(from->tres_usage_in_max);
		dest->tres_usage_in_max_taskid =
			xstrdup(from->tres_usage_in_max_taskid);
		dest->tres_usage_in_max_nodeid =
			xstrdup(from->tres_usage_in_max_nodeid);
		dest->tres_usage_in_min = xstrdup(from->tres_usage_in_min);
		dest->tres_usage_in_min_taskid =
			xstrdup(from->tres_usage_in_min_taskid);
		dest->tres_usage_in_min_nodeid =
			xstrdup(from->tres_usage_in_min_nodeid);
		dest->tres_usage_in_tot = xstrdup(from->tres_usage_in_tot);

		dest->tres_usage_out_ave = xstrdup(from->tres_usage_out_ave);
		dest->tres_usage_out_max = xstrdup(from->tres_usage_out_max);
		dest->tres_usage_out_max_taskid =
			xstrdup(from->tres_usage_out_max_taskid);
		dest->tres_usage_out_max_nodeid =
			xstrdup(from->tres_usage_out_max_nodeid);
		dest->tres_usage_out_min = xstrdup(from->tres_usage_out_min);
		dest->tres_usage_out_min_taskid =
			xstrdup(from->tres_usage_out_min_taskid);
		dest->tres_usage_out_min_nodeid =
			xstrdup(from->tres_usage_out_min_nodeid);
		dest->tres_usage_out_tot = xstrdup(from->tres_usage_out_tot);
		return;
	}

	_aggregate_tres_usage_stats_internal(&dest->tres_usage_in_max,
					     &dest->tres_usage_in_max_nodeid,
					     &dest->tres_usage_in_max_taskid,
					     from->tres_usage_in_max,
					     from->tres_usage_in_max_nodeid,
					     from->tres_usage_in_max_taskid);
	_aggregate_tres_usage_stats_internal(&dest->tres_usage_in_min,
					     &dest->tres_usage_in_min_nodeid,
					     &dest->tres_usage_in_min_taskid,
					     from->tres_usage_in_min,
					     from->tres_usage_in_min_nodeid,
					     from->tres_usage_in_min_taskid);
	_aggregate_tres_usage_stats_internal(&dest->tres_usage_out_max,
					     &dest->tres_usage_out_max_nodeid,
					     &dest->tres_usage_out_max_taskid,
					     from->tres_usage_out_max,
					     from->tres_usage_out_max_nodeid,
					     from->tres_usage_out_max_taskid);
	_aggregate_tres_usage_stats_internal(&dest->tres_usage_out_min,
					     &dest->tres_usage_out_min_nodeid,
					     &dest->tres_usage_out_min_taskid,
					     from->tres_usage_out_min,
					     from->tres_usage_out_min_nodeid,
					     from->tres_usage_out_min_taskid);

	flags =	TRES_STR_FLAG_SIMPLE + TRES_STR_FLAG_REMOVE + TRES_STR_FLAG_SUM;
	(void)slurmdb_combine_tres_strings(
		&dest->tres_usage_in_ave, from->tres_usage_in_ave, flags);
	(void)slurmdb_combine_tres_strings(
		&dest->tres_usage_out_ave, from->tres_usage_out_ave, flags);
	(void)slurmdb_combine_tres_strings(
		&dest->tres_usage_in_tot, from->tres_usage_in_tot, flags);
	(void)slurmdb_combine_tres_strings(
		&dest->tres_usage_out_tot, from->tres_usage_out_tot, flags);
}

void aggregate_stats(slurmdb_stats_t *dest, slurmdb_stats_t *from)
{
	/* Means it is a blank record */
	if (from->act_cpufreq == NO_VAL)
		return;

	if ((from->consumed_energy == NO_VAL64) ||
	    (dest->consumed_energy == NO_VAL64))
		dest->consumed_energy = NO_VAL64;
	else
		dest->consumed_energy += from->consumed_energy;

	dest->act_cpufreq += from->act_cpufreq;

	_aggregate_tres_usage_stats(dest, from);
}
