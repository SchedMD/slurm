/*****************************************************************************\
 *  sort.c - squeue sorting functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  UCRL-CODE-217948.
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

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>

#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/squeue/print.h"
#include "src/squeue/squeue.h"

/* If you want "linux12" to sort before "linux2", then set PURE_ALPHA_SORT */
#define PURE_ALPHA_SORT 0

static bool reverse_order;

static int _sort_job_by_group_id(void *void1, void *void2);
static int _sort_job_by_group_name(void *void1, void *void2);
static int _sort_job_by_id(void *void1, void *void2);
static int _sort_job_by_name(void *void1, void *void2);
static int _sort_job_by_state(void *void1, void *void2);
static int _sort_job_by_state_compact(void *void1, void *void2);
static int _sort_job_by_time_end(void *void1, void *void2);
static int _sort_job_by_time_limit(void *void1, void *void2);
static int _sort_job_by_time_start(void *void1, void *void2);
static int _sort_job_by_time_used(void *void1, void *void2);
static int _sort_job_by_node_list(void *void1, void *void2);
static int _sort_job_by_num_nodes(void *void1, void *void2);
static int _sort_job_by_num_procs(void *void1, void *void2);
static int _sort_job_by_partition(void *void1, void *void2);
static int _sort_job_by_priority(void *void1, void *void2);
static int _sort_job_by_user_id(void *void1, void *void2);
static int _sort_job_by_user_name(void *void1, void *void2);

static int _sort_step_by_id(void *void1, void *void2);
static int _sort_step_by_node_list(void *void1, void *void2);
static int _sort_step_by_partition(void *void1, void *void2);
static int _sort_step_by_time_start(void *void1, void *void2);
static int _sort_step_by_time_used(void *void1, void *void2);
static int _sort_step_by_user_id(void *void1, void *void2);
static int _sort_step_by_user_name(void *void1, void *void2);

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/

void sort_job_list(List job_list)
{
	int i;

	if (params.sort == NULL)
		params.sort = xstrdup("P,t,-p"); /* Partition,state,priority */

	for (i=(strlen(params.sort)-1); i >= 0; i--) {
		reverse_order = false;
		if ((params.sort[i] == ',') || 
		    (params.sort[i] == '+') || params.sort[i] == '-')
			continue;
		if ((i > 0) && (params.sort[i-1] == '-'))
			reverse_order = true;

		if      (params.sort[i] == 'c')
			;	/* sort_job_by_min_cpus_per_node */
		else if (params.sort[i] == 'C')
			list_sort(job_list, _sort_job_by_num_procs);
		else if (params.sort[i] == 'd')
			;	/* sort_job_by_min_tmp_disk_per_node */
		else if (params.sort[i] == 'D')
			list_sort(job_list, _sort_job_by_num_nodes);
		else if (params.sort[i] == 'e')
			list_sort(job_list, _sort_job_by_time_end);
		else if (params.sort[i] == 'f')
			;	/* sort_job_by_featuers */
		else if (params.sort[i] == 'g')
			list_sort(job_list, _sort_job_by_group_name);
		else if (params.sort[i] == 'G')
			list_sort(job_list, _sort_job_by_group_id);
		else if (params.sort[i] == 'h')
			;	/* sort_job_by_shared */
		else if (params.sort[i] == 'i')
			list_sort(job_list, _sort_job_by_id);
		else if (params.sort[i] == 'j')
			list_sort(job_list, _sort_job_by_name);
		else if (params.sort[i] == 'l')
			list_sort(job_list, _sort_job_by_time_limit);
		else if (params.sort[i] == 'm')
			;	/* sort_job_by_min_mem_per_node */
		else if (params.sort[i] == 'M')
			list_sort(job_list, _sort_job_by_time_used);
		else if (params.sort[i] == 'n')
			;	/* sort_job_by_nodes_requested */
		else if (params.sort[i] == 'N')
			list_sort(job_list, _sort_job_by_node_list);
		else if (params.sort[i] == 'O')
			;	/* sort_job_by_contiguous */
		else if (params.sort[i] == 'p')
			list_sort(job_list, _sort_job_by_priority);
		else if (params.sort[i] == 'P')
			list_sort(job_list, _sort_job_by_partition);
		else if (params.sort[i] == 'S')
			list_sort(job_list, _sort_job_by_time_start);
		else if (params.sort[i] == 't')
			list_sort(job_list, _sort_job_by_state_compact);
		else if (params.sort[i] == 'T')
			list_sort(job_list, _sort_job_by_state);
		else if (params.sort[i] == 'u')
			list_sort(job_list, _sort_job_by_user_name);
		else if (params.sort[i] == 'U')
			list_sort(job_list, _sort_job_by_user_id);
	}
}

void sort_jobs_by_start_time (List jobs)
{
	reverse_order = true;
	list_sort (jobs, _sort_job_by_time_start);
	return;
}

void sort_step_list(List step_list)
{
	int i;

	if (params.sort == NULL)
		params.sort = xstrdup("P,i");	/* Partition, step id */
	for (i=(strlen(params.sort)-1); i >= 0; i--) {
		reverse_order = false;
		if ((params.sort[i] == ',') || 
		    (params.sort[i] == '+') || params.sort[i] == '-')
			continue;
		if ((i > 0) && (params.sort[i-1] == '-'))
			reverse_order = true;

		if      (params.sort[i] == 'i')
			list_sort(step_list, _sort_step_by_id);
		else if (params.sort[i] == 'N')
			list_sort(step_list, _sort_step_by_node_list);
		else if (params.sort[i] == 'P')
			list_sort(step_list, _sort_step_by_partition);
		else if (params.sort[i] == 'S')
			list_sort(step_list, _sort_step_by_time_start);
		else if (params.sort[i] == 'M')
			list_sort(step_list, _sort_step_by_time_used);
		else if (params.sort[i] == 'u')
			list_sort(step_list, _sort_step_by_user_name);
		else if (params.sort[i] == 'U')
			list_sort(step_list, _sort_step_by_user_id);
	}
}

/*****************************************************************************
 * Local Job Sort Functions
 *****************************************************************************/
static int _sort_job_by_group_id(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->group_id - job2->group_id;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_group_name(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;
	struct group *group_info = NULL;
	char *name1 = "", *name2 = "";

	if ((group_info = getgrgid((gid_t) job1->group_id)))
		name1 = group_info->gr_name;
	if ((group_info = getgrgid((gid_t) job2->group_id)))
		name2 = group_info->gr_name;
	diff = strcmp(name1, name2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_id(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->job_id - job2->job_id;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_name(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = strcmp(job1->name, job2->name);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_node_list(void *void1, void *void2)
{
	int diff = 0;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;
	hostlist_t hostlist1, hostlist2;
	char *val1, *val2;
#if	PURE_ALPHA_SORT == 0
	int inx;
#endif

	hostlist1 = hostlist_create(job1->nodes);
	hostlist_sort(hostlist1);
	val1 = hostlist_shift(hostlist1);
	if (val1 == NULL)
		val1 = "";
	hostlist_destroy(hostlist1);

	hostlist2 = hostlist_create(job2->nodes);
	hostlist_sort(hostlist2);
	val2 = hostlist_shift(hostlist2);
	if (val2 == NULL)
		val2 = "";
	hostlist_destroy(hostlist2);

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

static int _sort_job_by_num_nodes(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->num_nodes - job2->num_nodes;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_num_procs(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->num_procs - job2->num_procs;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_state(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = strcmp(job_state_string(job1->job_state),
			 job_state_string(job2->job_state));

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_state_compact(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = strcmp(job_state_string_compact(job1->job_state),
			 job_state_string_compact(job2->job_state));

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_end(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->end_time - job2->end_time;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_limit(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->time_limit - job2->time_limit;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_start(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->start_time - job2->start_time;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_used(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;
	long time1, time2;

	time1 = job_time_used(job1);
	time2 = job_time_used(job2);
	diff = time1 - time2;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_partition(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = strcmp(job1->partition, job2->partition);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_priority(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->priority - job2->priority;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_user_id(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	diff = job1->user_id - job2->user_id;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_user_name(void *void1, void *void2)
{
	int diff;
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;
	char *name1, *name2;

	name1 = uid_to_string((uid_t) job1->user_id);
	name2 = uid_to_string((uid_t) job2->user_id);
	diff = strcmp(name1, name2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

/*****************************************************************************
 * Local Step Sort Functions
 *****************************************************************************/
static int _sort_step_by_id(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	diff = step1->job_id - step2->job_id;
	if (diff == 0)
		diff = step1->step_id - step2->step_id;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_node_list(void *void1, void *void2)
{
	int diff = 0;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	hostlist_t hostlist1, hostlist2;
	char *val1, *val2;
#if	PURE_ALPHA_SORT == 0
	int inx;
#endif

	hostlist1 = hostlist_create(step1->nodes);
	hostlist_sort(hostlist1);
	val1 = hostlist_shift(hostlist1);
	if (val1 == NULL)
		val1 = "";
	hostlist_destroy(hostlist1);

	hostlist2 = hostlist_create(step2->nodes);
	hostlist_sort(hostlist2);
	val2 = hostlist_shift(hostlist2);
	if (val2 == NULL)
		val2 = "";
	hostlist_destroy(hostlist2);

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

static int _sort_step_by_partition(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	diff = strcmp(step1->partition, step2->partition);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_time_start(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	diff = step1->start_time - step2->start_time;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_time_used(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;
	time_t now, used1, used2;

	now = time(NULL);
	used1 = difftime(now, step1->start_time);
	used2 = difftime(now, step2->start_time);
	diff = used1 - used2;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_user_id(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	diff = step1->user_id - step2->user_id;

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_user_name(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;
	char *name1, *name2;

	name1 = uid_to_string((uid_t) step1->user_id);
	name2 = uid_to_string((uid_t) step2->user_id);
	diff = strcmp(name1, name2);

	if (reverse_order)
		diff = -diff;
	return diff;
}
