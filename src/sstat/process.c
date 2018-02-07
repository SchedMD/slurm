/*****************************************************************************\
 *  process.c - process functions for stats
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "sstat.h"

static void _aggregate_tres_usage_stats(slurmdb_stats_t *dest,
					slurmdb_stats_t *from,
					uint32_t tres_id)
{
	List tres_list = NULL;
	slurmdb_tres_rec_t *tres_rec = NULL;
	uint32_t flags = TRES_STR_FLAG_SIMPLE + TRES_STR_FLAG_REPLACE;
	char *new_tres_str = NULL;
	uint64_t dest_count = 0;
	uint64_t from_count = 0;

	tres_list = list_create(slurmdb_destroy_tres_rec);
	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	tres_rec->id = tres_id;
	list_append(tres_list, tres_rec);
	dest_count = slurmdb_find_tres_count_in_string(dest->tres_usage_in_max,
						       tres_id);
	if (dest_count == INFINITE64)
		dest_count = 0;
	from_count = slurmdb_find_tres_count_in_string(from->tres_usage_in_max,
						       tres_id);
	if (from_count == INFINITE64)
		from_count = 0;
	if (dest_count < from_count) {
		tres_rec->count = from_count;
		new_tres_str = slurmdb_make_tres_string(tres_list, flags);
		dest->tres_usage_in_max = slurmdb_combine_tres_strings(
			&dest->tres_usage_in_max,
			new_tres_str, flags);
		xfree(new_tres_str);
		tres_rec->count = slurmdb_find_tres_count_in_string(
			from->tres_usage_in_max_taskid, tres_id);
		new_tres_str = slurmdb_make_tres_string(tres_list, flags);
		dest->tres_usage_in_max_taskid = slurmdb_combine_tres_strings(
			&dest->tres_usage_in_max_taskid, new_tres_str, flags);
		xfree(new_tres_str);
		tres_rec->count = slurmdb_find_tres_count_in_string(
			from->tres_usage_in_max_nodeid, tres_id);
		new_tres_str = slurmdb_make_tres_string(tres_list, flags);
		dest->tres_usage_in_max_nodeid = slurmdb_combine_tres_strings(
			&dest->tres_usage_in_max_nodeid, new_tres_str, flags);
		xfree(new_tres_str);
	}

	dest_count = slurmdb_find_tres_count_in_string(dest->tres_usage_in_ave,
						       tres_id);
	if (dest_count == INFINITE64)
		dest_count = 0;

	from_count = slurmdb_find_tres_count_in_string(from->tres_usage_in_ave,
						       tres_id);
	if (from_count == INFINITE64)
		from_count = 0;
	tres_rec->count = dest_count + from_count;
	new_tres_str = slurmdb_make_tres_string(tres_list, flags);
	dest->tres_usage_in_ave = slurmdb_combine_tres_strings(
		&dest->tres_usage_in_ave, new_tres_str, flags);
	xfree(new_tres_str);
	dest_count = slurmdb_find_tres_count_in_string(dest->tres_usage_out_max,
						       tres_id);
	if (dest_count == INFINITE64)
		dest_count = 0;
	from_count = slurmdb_find_tres_count_in_string(from->tres_usage_out_max,
						       tres_id);
	if (from_count == INFINITE64)
		dest_count = 0;
	if (dest_count < from_count) {
		tres_rec->count = from_count;
		new_tres_str = slurmdb_make_tres_string(tres_list, flags);
		dest->tres_usage_out_max = slurmdb_combine_tres_strings(
			&dest->tres_usage_out_max,
			new_tres_str, flags);
		xfree(new_tres_str);
		tres_rec->count = slurmdb_find_tres_count_in_string(
			from->tres_usage_out_max_taskid, tres_id);
		new_tres_str = slurmdb_make_tres_string(tres_list, flags);
		dest->tres_usage_out_max_taskid = slurmdb_combine_tres_strings(
			&dest->tres_usage_out_max_taskid, new_tres_str, flags);
		xfree(new_tres_str);
		tres_rec->count = slurmdb_find_tres_count_in_string(
			from->tres_usage_out_max_nodeid, tres_id);
		new_tres_str = slurmdb_make_tres_string(tres_list, flags);
		dest->tres_usage_out_max_nodeid = slurmdb_combine_tres_strings(
			&dest->tres_usage_out_max_nodeid, new_tres_str, flags);
		xfree(new_tres_str);
	}
	dest_count = slurmdb_find_tres_count_in_string(dest->tres_usage_out_ave,
						       tres_id);
	if (dest_count == INFINITE64)
		dest_count = 0;
	from_count = slurmdb_find_tres_count_in_string(from->tres_usage_out_ave,
						       tres_id);
	if (from_count == INFINITE64)
		from_count = 0;
	tres_rec->count = dest_count + from_count;
	new_tres_str = slurmdb_make_tres_string(tres_list, flags);
	dest->tres_usage_out_ave = slurmdb_combine_tres_strings(
		&dest->tres_usage_out_ave, new_tres_str, flags);
	xfree(new_tres_str);
	FREE_NULL_LIST(tres_list);
}

void aggregate_stats(slurmdb_stats_t *dest, slurmdb_stats_t *from)
{
	if (dest->vsize_max < from->vsize_max) {
		dest->vsize_max = from->vsize_max;
		dest->vsize_max_nodeid = from->vsize_max_nodeid;
		dest->vsize_max_taskid = from->vsize_max_taskid;
	}
	dest->vsize_ave += from->vsize_ave;

	if (dest->rss_max < from->rss_max) {
		dest->rss_max = from->rss_max;
		dest->rss_max_nodeid = from->rss_max_nodeid;
		dest->rss_max_taskid = from->rss_max_taskid;
	}
	dest->rss_ave += from->rss_ave;

	if (dest->pages_max < from->pages_max) {
		dest->pages_max = from->pages_max;
		dest->pages_max_nodeid = from->pages_max_nodeid;
		dest->pages_max_taskid = from->pages_max_taskid;
	}
	dest->pages_ave += from->pages_ave;

	if ((dest->cpu_min > from->cpu_min)
	    || (dest->cpu_min == NO_VAL)) {
		dest->cpu_min = from->cpu_min;
		dest->cpu_min_nodeid = from->cpu_min_nodeid;
		dest->cpu_min_taskid = from->cpu_min_taskid;
	}
	dest->cpu_ave += from->cpu_ave;
	if ((from->consumed_energy == NO_VAL64) ||
	    (dest->consumed_energy == NO_VAL64))
		dest->consumed_energy = NO_VAL64;
	else
		dest->consumed_energy += from->consumed_energy;
	dest->act_cpufreq += from->act_cpufreq;
	if (dest->disk_read_max < from->disk_read_max) {
		dest->disk_read_max = from->disk_read_max;
		dest->disk_read_max_nodeid = from->disk_read_max_nodeid;
		dest->disk_read_max_taskid = from->disk_read_max_taskid;
	}
	dest->disk_read_ave += from->disk_read_ave;
	if (dest->disk_write_max < from->disk_write_max) {
		dest->disk_write_max = from->disk_write_max;
		dest->disk_write_max_nodeid = from->disk_write_max_nodeid;
		dest->disk_write_max_taskid = from->disk_write_max_taskid;
	}
	dest->disk_write_ave += from->disk_write_ave;

	_aggregate_tres_usage_stats(dest, from, TRES_USAGE_DISK);
}
