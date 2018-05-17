/*****************************************************************************\
 *  sort.c - squeue sorting functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
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

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>

#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/squeue/print.h"
#include "src/squeue/squeue.h"

/* If you want "linux12" to sort before "linux2", then set PURE_ALPHA_SORT */
#define PURE_ALPHA_SORT 0
#define CLUSTER_NAME_LEN 7

static bool reverse_order;

static void _get_job_info_from_void(job_info_t **j1, job_info_t **j2, void *v1, void *v2);
static void _get_step_info_from_void(job_step_info_t **j1, job_step_info_t **j2, void *v1, void *v2);
static int _sort_job_by_batch_host(void *void1, void *void2);
static int _sort_job_by_cluster_name(void *void1, void *void2);
static int _sort_job_by_group_id(void *void1, void *void2);
static int _sort_job_by_group_name(void *void1, void *void2);
static int _sort_job_by_id(void *void1, void *void2);
static int _sort_job_by_name(void *void1, void *void2);
static int _sort_job_by_state(void *void1, void *void2);
static int _sort_job_by_state_compact(void *void1, void *void2);
static int _sort_job_by_time_end(void *void1, void *void2);
static int _sort_job_by_time_left(void *void1, void *void2);
static int _sort_job_by_time_limit(void *void1, void *void2);
static int _sort_job_by_time_submit(void *void1, void *void2);
static int _sort_job_by_time_start(void *void1, void *void2);
static int _sort_job_by_time_used(void *void1, void *void2);
static int _sort_job_by_node_list(void *void1, void *void2);
static int _sort_job_by_num_nodes(void *void1, void *void2);
static int _sort_job_by_num_cpus(void *void1, void *void2);
static int _sort_job_by_num_sct(void *void1, void *void2);
static int _sort_job_by_sockets(void *void1, void *void2);
static int _sort_job_by_cores(void *void1, void *void2);
static int _sort_job_by_threads(void *void1, void *void2);
static int _sort_job_by_min_memory(void *void1, void *void2);
static int _sort_job_by_min_tmp_disk(void *void1, void *void2);
static int _sort_job_by_partition(void *void1, void *void2);
static int _sort_job_by_priority(void *void1, void *void2);
static int _sort_job_by_user_id(void *void1, void *void2);
static int _sort_job_by_user_name(void *void1, void *void2);
static int _sort_job_by_reservation(void *void1, void *void2);

static int _sort_step_by_cluster_name(void *void1, void *void2);
static int _sort_step_by_id(void *void1, void *void2);
static int _sort_step_by_node_list(void *void1, void *void2);
static int _sort_step_by_partition(void *void1, void *void2);
static int _sort_step_by_time_start(void *void1, void *void2);
static int _sort_step_by_time_limit(void *void1, void *void2);
static int _sort_step_by_time_used(void *void1, void *void2);
static int _sort_step_by_user_id(void *void1, void *void2);
static int _sort_step_by_user_name(void *void1, void *void2);

static time_t now;

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/

void sort_job_list(List job_list)
{
	int i;
	now = time(NULL);

	if (params.sort == NULL)
		params.sort = xstrdup("P,t,-p"); /* Partition,state,priority */

	for (i=(strlen(params.sort)-1); i >= 0; i--) {
		reverse_order = false;
		if ((params.sort[i] == ',') ||
		    (params.sort[i] == '+') || params.sort[i] == '-')
			continue;
		if ((i > 0) && (params.sort[i-1] == '-'))
			reverse_order = true;

		if ((CLUSTER_NAME_LEN <= (i + 1)) &&
		    !xstrncasecmp(params.sort + (i - CLUSTER_NAME_LEN + 1),
				  "cluster", CLUSTER_NAME_LEN))
		{
			if ((CLUSTER_NAME_LEN <= i) &&
			    (params.sort[i - CLUSTER_NAME_LEN] == '-'))
				reverse_order = true;

			list_sort(job_list, _sort_job_by_cluster_name);
			i -= CLUSTER_NAME_LEN - 1;
		} else if (params.sort[i] == 'B')
			list_sort(job_list, _sort_job_by_batch_host);
		else if (params.sort[i] == 'b')	/* Vestigial gres sort */
			info("Invalid sort specification: b");
		else if (params.sort[i] == 'c')
			;	/* sort_job_by_min_cpus_per_node */
		else if (params.sort[i] == 'C')
			list_sort(job_list, _sort_job_by_num_cpus);
		else if (params.sort[i] == 'd')
			list_sort(job_list, _sort_job_by_min_tmp_disk);
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
			;	/* sort_job_by_over_subscribe, not supported */
		else if (params.sort[i] == 'H')
			list_sort(job_list, _sort_job_by_sockets);
		else if (params.sort[i] == 'i')
			list_sort(job_list, _sort_job_by_id);
		else if (params.sort[i] == 'I')
			list_sort(job_list, _sort_job_by_cores);
		else if (params.sort[i] == 'j')
			list_sort(job_list, _sort_job_by_name);
		else if (params.sort[i] == 'J')
			list_sort(job_list, _sort_job_by_threads);
		else if (params.sort[i] == 'l')
			list_sort(job_list, _sort_job_by_time_limit);
		else if (params.sort[i] == 'L')
			list_sort(job_list, _sort_job_by_time_left);
		else if (params.sort[i] == 'm')
			list_sort(job_list, _sort_job_by_min_memory);
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
		else if (params.sort[i] == 'Q')
			list_sort(job_list, _sort_job_by_priority);
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
		else if (params.sort[i] == 'v')
			list_sort(job_list, _sort_job_by_reservation);
		else if (params.sort[i] == 'V')
			list_sort(job_list, _sort_job_by_time_submit);
		else if (params.sort[i] == 'z')
			list_sort(job_list, _sort_job_by_num_sct);
		else {
			error("Invalid sort specification: %c",
			      params.sort[i]);
			exit(1);
		}
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
	now = time(NULL);

	if (params.sort == NULL)
		params.sort = xstrdup("P,i");	/* Partition, step id */
	for (i=(strlen(params.sort)-1); i >= 0; i--) {
		reverse_order = false;
		if ((params.sort[i] == ',') ||
		    (params.sort[i] == '+') || params.sort[i] == '-')
			continue;
		if ((i > 0) && (params.sort[i-1] == '-'))
			reverse_order = true;

		if ((CLUSTER_NAME_LEN <= (i + 1)) &&
		    !xstrncasecmp(params.sort + (i - CLUSTER_NAME_LEN + 1),
				  "cluster", CLUSTER_NAME_LEN)) {
			if ((CLUSTER_NAME_LEN <= i) &&
			    (params.sort[i - CLUSTER_NAME_LEN] == '-'))
				reverse_order = true;

			list_sort(step_list, _sort_step_by_cluster_name);
			i -= CLUSTER_NAME_LEN - 1;
		}
		else if (params.sort[i] == 'b')	/* Vestigial gres sort */
			info("Invalid sort specification: b");
		else if (params.sort[i] == 'i')
			list_sort(step_list, _sort_step_by_id);
		else if (params.sort[i] == 'N')
			list_sort(step_list, _sort_step_by_node_list);
		else if (params.sort[i] == 'P')
			list_sort(step_list, _sort_step_by_partition);
		else if (params.sort[i] == 'l')
			list_sort(step_list, _sort_step_by_time_limit);
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
static inline int _diff_long(long value_1, long value_2)
{
	if (value_1 > value_2)
		return 1;
	if (value_1 < value_2)
		return -1;
	return 0;
}
static inline int _diff_time(time_t value_1, time_t value_2)
{
	if (value_1 > value_2)
		return 1;
	if (value_1 < value_2)
		return -1;
	return 0;
}
static inline int _diff_uint32(uint32_t value_1, uint32_t value_2)
{
	if (value_1 > value_2)
		return 1;
	if (value_1 < value_2)
		return -1;
	return 0;
}
static inline int _diff_uint64(uint64_t value_1, uint64_t value_2)
{
	if (value_1 > value_2)
		return 1;
	if (value_1 < value_2)
		return -1;
	return 0;
}
static void _get_job_info_from_void(job_info_t **j1, job_info_t **j2,
				    void *void1, void *void2)
{
	*j1 = (*(squeue_job_rec_t **)void1)->job_ptr;
	*j2 = (*(squeue_job_rec_t **)void2)->job_ptr;
}
static void _get_part_prio_info_from_void(uint32_t *prio1, uint32_t *prio2,
					  void *void1, void *void2)
{
	*prio1 = (*(squeue_job_rec_t **)void1)->part_prio;
	*prio2 = (*(squeue_job_rec_t **)void2)->part_prio;
}
static void _get_part_name_info_from_void(char **name1, char **name2,
					  void *void1, void *void2)
{
	*name1 = (*(squeue_job_rec_t **)void1)->part_name;
	*name2 = (*(squeue_job_rec_t **)void2)->part_name;
}
static void _get_step_info_from_void(job_step_info_t **s1, job_step_info_t **s2,
				     void *v1, void *v2)
{
	*s1 = *(job_step_info_t **)v1;
	*s2 = *(job_step_info_t **)v2;
}
static int _sort_job_by_batch_host(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	char *val1 = "", *val2 = "";

	_get_job_info_from_void(&job1, &job2, void1, void2);

	if (job1->batch_host)
		val1 = job1->batch_host;
	if (job2->batch_host)
		val2 = job2->batch_host;
	diff = xstrcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_cluster_name(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = xstrcmp(job1->cluster, job2->cluster);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_group_id(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->group_id, job2->group_id);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_group_name(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	struct group *group_info = NULL;
	char *name1 = "", *name2 = "";

	_get_job_info_from_void(&job1, &job2, void1, void2);

	if ((group_info = getgrgid((gid_t) job1->group_id)))
		name1 = group_info->gr_name;
	if ((group_info = getgrgid((gid_t) job2->group_id)))
		name2 = group_info->gr_name;
	diff = xstrcmp(name1, name2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_id(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	uint32_t job_id1, job_id2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	if (job1->pack_job_id)
		job_id1 = job1->pack_job_id;
	else if (job1->array_task_id == NO_VAL)
		job_id1 = job1->job_id;
	else
		job_id1 = job1->array_job_id;

	if (job2->pack_job_id)
		job_id2 = job2->pack_job_id;
	else if (job2->array_task_id == NO_VAL)
		job_id2 = job2->job_id;
	else
		job_id2 = job2->array_job_id;

	if (job_id1 == job_id2) {
		if (job1->pack_job_id)
			job_id1 = job1->pack_job_offset;
		else
			job_id1 = job1->array_task_id;
		if (job2->pack_job_id)
			job_id2 = job2->pack_job_offset;
		else
			job_id2 = job2->array_task_id;
	}

	diff = _diff_uint32(job_id1, job_id2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_name(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	char *val1 = "", *val2 = "";

	_get_job_info_from_void(&job1, &job2, void1, void2);

	if (job1->name)
		val1 = job1->name;
	if (job2->name)
		val2 = job2->name;
	diff = xstrcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_node_list(void *void1, void *void2)
{
	int diff = 0;
	job_info_t *job1;
	job_info_t *job2;
	hostlist_t hostlist1, hostlist2;
	char *val1, *val2;
	char *ptr1, *ptr2;
#if	PURE_ALPHA_SORT == 0
	int inx;
#endif

	_get_job_info_from_void(&job1, &job2, void1, void2);

	hostlist1 = hostlist_create(job1->nodes);
	hostlist_sort(hostlist1);
	val1 = hostlist_shift(hostlist1);
	if (val1)
		ptr1 = val1;
	else
		ptr1 = "";
	hostlist_destroy(hostlist1);

	hostlist2 = hostlist_create(job2->nodes);
	hostlist_sort(hostlist2);
	val2 = hostlist_shift(hostlist2);
	if (val2)
		ptr2 = val2;
	else
		ptr2 = "";
	hostlist_destroy(hostlist2);

#if	PURE_ALPHA_SORT
	diff = xstrcmp(ptr1, ptr2);
#else
	for (inx = 0; ; inx++) {
		if (ptr1[inx] == ptr2[inx]) {
			if (ptr1[inx] == '\0')
				break;
			continue;
		}
		if ((isdigit((int)ptr1[inx])) &&
		    (isdigit((int)ptr2[inx]))) {
			int num1, num2;
			num1 = atoi(ptr1 + inx);
			num2 = atoi(ptr2 + inx);
			diff = num1 - num2;
		} else
			diff = xstrcmp(ptr1, ptr2);
		break;
	}
#endif
	if (val1)
		free(val1);
	if (val2)
		free(val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_num_nodes(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->num_nodes, job2->num_nodes);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_num_cpus(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->num_cpus, job2->num_cpus);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_num_sct(void *void1, void *void2)
{
	int diffs, diffc, difft;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diffs = _diff_uint32(job1->sockets_per_node, job2->sockets_per_node);
	diffc = _diff_uint32(job1->cores_per_socket, job2->cores_per_socket);
	difft = _diff_uint32(job1->threads_per_core, job2->threads_per_core);

	if (reverse_order) {
		diffs = -diffs;
		diffc = -diffc;
		difft = -difft;
	}
	if (diffs)
		return diffs;
	else if (diffc)
		return diffc;
	else
		return difft;
}

static int _sort_job_by_sockets(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->sockets_per_node, job2->sockets_per_node);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_cores(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->cores_per_socket, job2->cores_per_socket);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_threads(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->threads_per_core, job2->threads_per_core);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_min_memory(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	job1->pn_min_memory &= (~MEM_PER_CPU);
	job2->pn_min_memory &= (~MEM_PER_CPU);
	diff = _diff_uint64(job1->pn_min_memory, job2->pn_min_memory);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_min_tmp_disk(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->pn_min_tmp_disk, job2->pn_min_tmp_disk);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_state(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = xstrcmp(job_state_string(job1->job_state),
		       job_state_string(job2->job_state));

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_state_compact(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = xstrcmp(job_state_string_compact(job1->job_state),
		       job_state_string_compact(job2->job_state));

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_end(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_time(job1->end_time, job2->end_time);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_left(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	time_t time1, time2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	if ((job1->time_limit == INFINITE) || (job1->time_limit == NO_VAL))
		time1 = INFINITE;
	else
		time1 = job1->time_limit * 60 - job_time_used(job1);
	if ((job2->time_limit == INFINITE) || (job2->time_limit == NO_VAL))
		time2 = INFINITE;
	else
		time2 = job2->time_limit * 60 - job_time_used(job2);
	diff = _diff_time(time1, time2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_limit(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->time_limit, job2->time_limit);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_submit(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_time(job1->submit_time, job2->submit_time);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static time_t _get_start_time(job_info_t *job)
{
	if (job->start_time == (time_t) 0)
		return (now + 100);
	if ((job->job_state == JOB_PENDING) && (job->start_time < now))
		return now;
	return job->start_time;
}

static int _sort_job_by_time_start(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	time_t start_time1, start_time2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	start_time1 = _get_start_time(job1);
	start_time2 = _get_start_time(job2);

	diff = _diff_time(start_time1, start_time2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_time_used(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	long time1, time2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	time1 = job_time_used(job1);
	time2 = job_time_used(job2);
	diff = _diff_long(time1, time2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_partition(void *void1, void *void2)
{
	int diff;
	char *val1 = NULL, *val2 = NULL;

	_get_part_name_info_from_void(&val1, &val1, void1, void2);

	diff = xstrcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_priority(void *void1, void *void2)
{
	int diff;
	job_info_t *job1, *job2;
	uint32_t prio1 = 0, prio2 = 0;

	_get_part_prio_info_from_void(&prio1, &prio2, void1, void2);
	diff = _diff_uint32(prio1, prio2);

	if (diff == 0) {  /* Same partition priority, test job priority */
		_get_job_info_from_void(&job1, &job2, void1, void2);
		diff = _diff_uint32(job1->priority, job2->priority);
	}

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_user_id(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	diff = _diff_uint32(job1->user_id, job2->user_id);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_user_name(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	char *name1, *name2;

	_get_job_info_from_void(&job1, &job2, void1, void2);

	name1 = uid_to_string_cached((uid_t) job1->user_id);
	name2 = uid_to_string_cached((uid_t) job2->user_id);
	diff = xstrcmp(name1, name2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_job_by_reservation(void *void1, void *void2)
{
	int diff;
	job_info_t *job1;
	job_info_t *job2;
	char *val1 = "", *val2 = "";

	_get_job_info_from_void(&job1, &job2, void1, void2);

	if (job1->resv_name)
		val1 = job1->resv_name;
	if (job2->resv_name)
		val2 = job2->resv_name;
	diff = xstrcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

/*****************************************************************************
 * Local Step Sort Functions
 *****************************************************************************/
static int _sort_step_by_cluster_name(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;

	_get_step_info_from_void(&step1, &step2, void1, void2);

	diff = xstrcmp(step1->cluster, step2->cluster);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_id(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;

	_get_step_info_from_void(&step1, &step2, void1, void2);

	diff = _diff_uint32(step1->job_id, step2->job_id);
	if (diff == 0)
		diff = _diff_uint32(step1->step_id, step2->step_id);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_node_list(void *void1, void *void2)
{
	int diff = 0;
	job_step_info_t *step1;
	job_step_info_t *step2;

	hostlist_t hostlist1, hostlist2;
	char *val1, *val2;
	char *ptr1, *ptr2;
#if	PURE_ALPHA_SORT == 0
	int inx;
#endif

	_get_step_info_from_void(&step1, &step2, void1, void2);

	hostlist1 = hostlist_create(step1->nodes);
	hostlist_sort(hostlist1);
	val1 = hostlist_shift(hostlist1);
	if (val1)
		ptr1 = val1;
	else
		ptr1 = "";
	hostlist_destroy(hostlist1);

	hostlist2 = hostlist_create(step2->nodes);
	hostlist_sort(hostlist2);
	val2 = hostlist_shift(hostlist2);
	if (val2)
		ptr2 = val2;
	else
		ptr2 = "";
	hostlist_destroy(hostlist2);

#if	PURE_ALPHA_SORT
	diff = xstrcmp(ptr1, ptr2);
#else
	for (inx = 0; ; inx++) {
		if (ptr1[inx] == ptr2[inx]) {
			if (ptr1[inx] == '\0')
				break;
			continue;
		}
		if ((isdigit((int)ptr1[inx])) &&
		    (isdigit((int)ptr2[inx]))) {
			int num1, num2;
			num1 = atoi(ptr1 + inx);
			num2 = atoi(ptr2 + inx);
			diff = num1 - num2;
		} else
			diff = xstrcmp(ptr1, ptr2);
		break;
	}
#endif
	if (val1)
		free(val1);
	if (val2)
		free(val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_partition(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;
	char *val1 = "", *val2 = "";

	_get_step_info_from_void(&step1, &step2, void1, void2);

	if (step1->partition)
		val1 = step1->partition;
	if (step2->partition)
		val2 = step2->partition;
	diff = xstrcmp(val1, val2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_time_limit(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;

	_get_step_info_from_void(&step1, &step2, void1, void2);

	diff = _diff_uint32(step1->time_limit, step2->time_limit);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_time_start(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;

	_get_step_info_from_void(&step1, &step2, void1, void2);

	diff = _diff_time(step1->start_time, step2->start_time);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_time_used(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;
	time_t used1, used2;

	_get_step_info_from_void(&step1, &step2, void1, void2);

	used1 = difftime(now, step1->start_time);
	used2 = difftime(now, step2->start_time);
	diff = _diff_time(used1, used2);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_user_id(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;

	_get_step_info_from_void(&step1, &step2, void1, void2);

	diff = _diff_uint32(step1->user_id, step2->user_id);

	if (reverse_order)
		diff = -diff;
	return diff;
}

static int _sort_step_by_user_name(void *void1, void *void2)
{
	int diff;
	job_step_info_t *step1;
	job_step_info_t *step2;
	char *name1, *name2;

	_get_step_info_from_void(&step1, &step2, void1, void2);

	name1 = uid_to_string_cached((uid_t) step1->user_id);
	name2 = uid_to_string_cached((uid_t) step2->user_id);
	diff = xstrcmp(name1, name2);

	if (reverse_order)
		diff = -diff;
	return diff;
}
