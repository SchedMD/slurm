/*****************************************************************************\
 *  sort.c - sinfo sorting functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, 
 *             Moe Jette <jette1@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <ctype.h>
#include "src/common/xstring.h"
#include "src/sinfo/sinfo.h"
#include "src/squeue/print.h"

/* If you want "linux12" to sort before "linux2", then set PURE_ALPHA_SORT */
#define PURE_ALPHA_SORT 0

static bool reverse_order;
static bool part_order;		/* order same as in part table */

static int _sort_by_avail(void *void1, void *void2);
static int _sort_by_cpus(void *void1, void *void2);
static int _sort_by_disk(void *void1, void *void2);
static int _sort_by_features(void *void1, void *void2);
static int _sort_by_groups(void *void1, void *void2);
static int _sort_by_job_size(void *void1, void *void2);
static int _sort_by_max_time(void *void1, void *void2);
static int _sort_by_memory(void *void1, void *void2);
static int _sort_by_node_list(void *void1, void *void2);
static int _sort_by_nodes_ai(void *void1, void *void2);
static int _sort_by_nodes(void *void1, void *void2);
static int _sort_by_partition(void *void1, void *void2);
static int _sort_by_reason(void *void1, void *void2);
static int _sort_by_root(void *void1, void *void2);
static int _sort_by_share(void *void1, void *void2);
static int _sort_by_state(void *void1, void *void2);
static int _sort_by_weight(void *void1, void *void2);

/*****************************************************************************
 * Global Sort Function
 *****************************************************************************/
void sort_sinfo_list(List sinfo_list)
{
	int i;

	if (params.sort == NULL) {
		if (params.node_flag)
			params.sort = xstrdup("N");
		else
			params.sort = xstrdup("#P,-t");
	}

	for (i=(strlen(params.sort)-1); i >= 0; i--) {
		reverse_order = false;
		part_order    = false;

		if ((params.sort[i] == ',') || (params.sort[i] == '#') ||  
		    (params.sort[i] == '+') || (params.sort[i] == '-'))
			continue;
		if ((i > 0) && (params.sort[i-1] == '-'))
			reverse_order = true;
		if ((i > 0) && (params.sort[i-1] == '#'))
                        part_order = true;

		if      (params.sort[i] == 'a')
				list_sort(sinfo_list, _sort_by_avail);
		else if (params.sort[i] == 'A')
				list_sort(sinfo_list, _sort_by_nodes_ai);
		else if (params.sort[i] == 'c')
				list_sort(sinfo_list, _sort_by_cpus);
		else if (params.sort[i] == 'd')
				list_sort(sinfo_list, _sort_by_disk);
		else if (params.sort[i] == 'D')
				list_sort(sinfo_list, _sort_by_nodes);
		else if (params.sort[i] == 'f')
				list_sort(sinfo_list, _sort_by_features);
		else if (params.sort[i] == 'F')
				list_sort(sinfo_list, _sort_by_nodes_ai);
		else if (params.sort[i] == 'g')
				list_sort(sinfo_list, _sort_by_groups);
		else if (params.sort[i] == 'h')
				list_sort(sinfo_list, _sort_by_share);
		else if (params.sort[i] == 'l')
				list_sort(sinfo_list, _sort_by_max_time);
		else if (params.sort[i] == 'm')
				list_sort(sinfo_list, _sort_by_memory);
		else if (params.sort[i] == 'N')
				list_sort(sinfo_list, _sort_by_node_list);
		else if (params.sort[i] == 'P')
				list_sort(sinfo_list, _sort_by_partition);
		else if (params.sort[i] == 'r')
				list_sort(sinfo_list, _sort_by_root);
		else if (params.sort[i] == 'R')
				list_sort(sinfo_list, _sort_by_reason);
		else if (params.sort[i] == 's')
				list_sort(sinfo_list, _sort_by_job_size);
		else if (params.sort[i] == 't')
				list_sort(sinfo_list, _sort_by_state);
		else if (params.sort[i] == 'T')
				list_sort(sinfo_list, _sort_by_state);
		else if (params.sort[i] == 'w')
				list_sort(sinfo_list, _sort_by_weight);
	}
}

/*****************************************************************************
 * Local Sort Functions
 *****************************************************************************/
static int _sort_by_avail(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	int val1 = 0, val2 = 0;

	if (sinfo1->part_info)
		val1 = sinfo1->part_info->state_up;
	if (sinfo2->part_info)
		val2 = sinfo2->part_info->state_up;
	diff = val1 - val2;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_cpus(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;

	diff = sinfo1->min_cpus - sinfo2->min_cpus;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_disk(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;

	diff = sinfo1->min_disk - sinfo2->min_disk;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_features(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	char *val1 = "", *val2 = "";

	if (sinfo1->features)
		val1 = sinfo1->features;
	if (sinfo2->features)
		val2 = sinfo2->features;
	diff = strcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_groups(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	char *val1 = "", *val2 = "";

	if (sinfo1->part_info && sinfo1->part_info->allow_groups)
		val1 = sinfo1->part_info->allow_groups;
	if (sinfo2->part_info && sinfo2->part_info->allow_groups)
		val2 = sinfo2->part_info->allow_groups;
	diff = strcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_job_size(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	uint32_t val1 = 0, val2 = 0;

	if (sinfo1->part_info) {
		val1 = sinfo1->part_info->max_nodes;
		if (val1 != INFINITE)
			val1 += sinfo1->part_info->min_nodes;
	}
	if (sinfo2->part_info) {
		val2 = sinfo2->part_info->max_nodes;
		if (val2 != INFINITE)
			val2 += sinfo2->part_info->min_nodes;
	}
	diff = val1 - val2;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_max_time(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	uint32_t val1 = 0, val2 = 0;

	if (sinfo1->part_info)
		val1 = sinfo1->part_info->max_time;
	if (sinfo2->part_info)
		val2 = sinfo2->part_info->max_time;
	diff = val1 - val2;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_memory(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;

	diff = sinfo1->min_mem - sinfo2->min_mem;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_node_list(void *void1, void *void2)
{
	int diff = 0;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	char *val1, *val2;
#if	PURE_ALPHA_SORT == 0
	int inx;
#endif

	val1 = hostlist_shift(sinfo1->nodes);
	if (val1) {
		hostlist_push_host(sinfo1->nodes, val1);
		hostlist_sort(sinfo1->nodes);
	} else
		val1 = "";

	val2 = hostlist_shift(sinfo2->nodes);
	if (val2) {
		hostlist_push_host(sinfo2->nodes, val2);
		hostlist_sort(sinfo2->nodes);
	} else
		val2 = "";

#if	PURE_ALPHA_SORT
	diff = strcmp(val1, val2);
#else
	for (inx=0; ; inx++) {
		if (val1[inx] == val2[inx]) {
			if (val1[inx] == '\0')
				break;
			continue;
		}
		if ((isdigit((int)val1[inx])) &&
		    (isdigit((int)val2[inx]))) {
			int num1, num2;
			num1 = atoi(val1+inx);
			num2 = atoi(val2+inx);
			diff = num1 - num2;
		} else
			diff = strcmp(val1, val2);
		break;
	}
#endif
	if (strlen(val1))
		free(val1);
	if (strlen(val2))
		free(val2);

	if (reverse_order)
		diff = -diff;

	return diff;
}

static int _sort_by_nodes_ai(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;

	diff = sinfo1->nodes_alloc - sinfo2->nodes_alloc;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_nodes(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;

	diff = sinfo1->nodes_tot - sinfo2->nodes_tot;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_partition(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	char *val1 = "", *val2 = "";

	if (part_order) {
		diff = (int)sinfo1->part_inx - (int)sinfo2->part_inx;
	} else {
		if (sinfo1->part_info && sinfo1->part_info->name)
			val1 = sinfo1->part_info->name;
		if (sinfo2->part_info && sinfo2->part_info->name)
			val2 = sinfo2->part_info->name;
		diff = strcmp(val1, val2);
	}

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_reason(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	char *val1 = "", *val2 = "";

	if (sinfo1->reason)
		val1 = sinfo1->reason;
	if (sinfo2->reason)
		val2 = sinfo2->reason;
	diff = strcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_root(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	int val1 = 0, val2 = 0;

	if (sinfo1->part_info)
		val1 = sinfo1->part_info->root_only;
	if (sinfo2->part_info)
		val2 = sinfo2->part_info->root_only;
	diff = val1 - val2;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_share(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;
	int val1 = 0, val2 = 0;

	if (sinfo1->part_info)
		val1 = sinfo1->part_info->shared;
	if (sinfo2->part_info)
		val2 = sinfo2->part_info->shared;
	diff = val1 - val2;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_by_state(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;

	diff = (int)sinfo1->node_state - (int)sinfo2->node_state;

	if (reverse_order)
		diff = -diff;

	return diff;
}

static int _sort_by_weight(void *void1, void *void2)
{
	int diff;
	sinfo_data_t *sinfo1 = (sinfo_data_t *) void1;
	sinfo_data_t *sinfo2 = (sinfo_data_t *) void2;

	diff = sinfo1->min_weight - sinfo2->min_weight;

	if (reverse_order)
		diff = -diff;
	return diff;
}
