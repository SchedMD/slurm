/*****************************************************************************\
 *  print.c - squeue print job functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2013 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/squeue/print.h"
#include "src/squeue/squeue.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static int	_filter_job(job_info_t * job);
static int	_filter_step(job_step_info_t * step);
static int	_get_node_cnt(job_info_t * job);
static bool	_merge_job_array(List l, job_info_t * job_ptr);
static int	_nodes_in_list(char *node_list);
static int	_print_str(char *str, int width, bool right, bool cut_output);

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/

int print_steps(List steps, List format)
{
	print_step_from_format(NULL, format);

	if (list_count(steps) > 0) {
		job_step_info_t *step = NULL;
		ListIterator i = list_iterator_create(steps);

		while ((step = (job_step_info_t *) list_next(i)) != NULL) {
			print_step_from_format(step, format);
		}
	}

	return SLURM_SUCCESS;
}

int print_jobs_array(job_info_t * jobs, int size, List format)
{
	int i;
	List l;

	l = list_create(NULL);
	if (!params.no_header)
		print_job_from_format(NULL, format);

	/* Filter out the jobs of interest */
	for (i = 0; i < size; i++) {
		if (_filter_job(&jobs[i]))
			continue;
		if (_merge_job_array(l, &jobs[i]))
			continue;
		list_append(l, (void *) &jobs[i]);
	}
	sort_jobs_by_start_time (l);
	sort_job_list (l);

	/* Print the jobs of interest */
	list_for_each (l, (ListForF) print_job_from_format, (void *) format);
	list_destroy (l);

	return SLURM_SUCCESS;
}

int print_steps_array(job_step_info_t * steps, int size, List format)
{

	if (!params.no_header)
		print_step_from_format(NULL, format);

	if (size > 0) {
		int i;
		List step_list;
		ListIterator step_iterator;
		job_step_info_t *step_ptr;

		step_list = list_create(NULL);

		/* Filter out the jobs of interest */
		for (i = 0; i < size; i++) {
			if (_filter_step(&steps[i]))
				continue;
			list_append(step_list, (void *) &steps[i]);
		}

		sort_step_list(step_list);

		/* Print the steps of interest */
		step_iterator = list_iterator_create(step_list);
		while ((step_ptr = list_next(step_iterator))) {
			print_step_from_format(step_ptr, format);
		}
		list_iterator_destroy(step_iterator);
		list_destroy(step_list);
	}

	return SLURM_SUCCESS;
}

static bool _merge_job_array(List l, job_info_t * job_ptr)
{
	job_info_t *list_job_ptr;
	ListIterator iter;
	bool merge = false;

	if (params.array_flag)
		return merge;
	if (job_ptr->array_task_id == NO_VAL)
		return merge;
	if (!IS_JOB_PENDING(job_ptr))
		return merge;
	xfree(job_ptr->node_inx);
	if (!l)
		return merge;

	iter = list_iterator_create(l);
	while ((list_job_ptr = list_next(iter))) {
		if ((list_job_ptr->array_task_id ==  NO_VAL) ||
		    (job_ptr->array_job_id != list_job_ptr->array_job_id) ||
		    (!IS_JOB_PENDING(list_job_ptr)))
			continue;
		/* We re-purpose the job's node_inx array to store the
		 * array_task_id values */
		if (!list_job_ptr->node_inx) {
			list_job_ptr->node_inx = xmalloc(sizeof(int32_t) * 0xffff);
			list_job_ptr->node_inx[0] = 1;		/* offset */
			list_job_ptr->node_inx[1] =
				list_job_ptr->array_task_id;
		}
		list_job_ptr->node_inx[0]++;
		list_job_ptr->node_inx[list_job_ptr->node_inx[0]] =
				job_ptr->array_task_id;
		merge = true;
		break;
	}
	list_iterator_destroy(iter);

	return merge;
}

static int _print_str(char *str, int width, bool right, bool cut_output)
{
	char format[64];
	int printed = 0;

	if (right == true && width != 0)
		snprintf(format, 64, "%%%ds", width);
	else if (width != 0)
		snprintf(format, 64, "%%.%ds", width);
	else {
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}

	if ((width == 0) || (cut_output == false)) {
		if ((printed = printf(format, str)) < 0)
			return printed;
	} else {
		char temp[width + 1];
		snprintf(temp, width + 1, format, str);
		if ((printed = printf("%s",temp)) < 0)
			return printed;
	}

	while (printed++ < width)
		printf(" ");

	return printed;
}

int _print_nodes(char *nodes, int width, bool right, bool cut)
{
	hostlist_t hl = hostlist_create(nodes);
	char *buf = NULL;
	int retval;
	buf = hostlist_ranged_string_xmalloc(hl);
	retval = _print_str(buf, width, right, false);
	xfree(buf);
	hostlist_destroy(hl);
	return retval;
}


int _print_int(int number, int width, bool right, bool cut_output)
{
	char buf[32];

	snprintf(buf, 32, "%d", number);
	return _print_str(buf, width, right, cut_output);
}


int _print_secs(long time, int width, bool right, bool cut_output)
{
	char str[FORMAT_STRING_SIZE];
	long days, hours, minutes, seconds;

	seconds =  time % 60;
	minutes = (time / 60)   % 60;
	hours   = (time / 3600) % 24;
	days    =  time / 86400;

	if ((time < 0) || (time > (365 * 24 * 3600)))
		snprintf(str, FORMAT_STRING_SIZE, "INVALID");
	else if (days)
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld-%2.2ld:%2.2ld:%2.2ld",
		         days, hours, minutes, seconds);
	else if (hours)
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld:%2.2ld",
		         hours, minutes, seconds);
	else
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld",
		         minutes, seconds);

	_print_str(str, width, right, cut_output);
	return SLURM_SUCCESS;
}

int _print_time(time_t t, int level, int width, bool right)
{
	if (t) {
		char time_str[32];
		slurm_make_time_str(&t, time_str, sizeof(time_str));
		_print_str(time_str, width, right, true);
	} else
		_print_str("N/A", width, right, true);

	return SLURM_SUCCESS;
}

/*****************************************************************************
 * Job Print Functions
 *****************************************************************************/
int print_job_from_format(job_info_t * job, List list)
{
	ListIterator i = list_iterator_create(list);
	job_format_t *current;
	int total_width = 0;

	while ((current = (job_format_t *) list_next(i)) != NULL) {
		if (current->
		    function(job, current->width, current->right_justify,
			     current->suffix)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
		if (current->width)
			total_width += (current->width + 1);
		else
			total_width += 10;
	}
	list_iterator_destroy(i);

	printf("\n");
#if 0
	if (job == NULL) {
		int inx;
		/* one-origin for no trailing space */
		for (inx=1; inx<total_width; inx++)
			printf("-");
		printf("\n");
	}
#endif
	return SLURM_SUCCESS;
}

int
job_format_add_function(List list, int width, bool right, char *suffix,
			int (*function) (job_info_t *, int, bool, char*))
{
	job_format_t *tmp = (job_format_t *) xmalloc(sizeof(job_format_t));
	tmp->function = function;
	tmp->width = width;
	tmp->right_justify = right;
	tmp->suffix = suffix;

	if (list_append(list, tmp) == NULL) {
		fprintf(stderr, "Memory exhausted\n");
		exit(1);
	}
	return SLURM_SUCCESS;
}

int _print_job_array_job_id(job_info_t * job, int width, bool right,
			    char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("ARRAY_JOB_ID", width, right, true);
	} else if (job->array_task_id != NO_VAL) {
		char id[FORMAT_STRING_SIZE];
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->array_job_id);
		_print_str(id, width, right, true);
	} else {
		_print_str("N/A", width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_array_task_id(job_info_t * job, int width, bool right,
			     char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("ARRAY_TASK_ID", width, right, true);
	} else if (job->array_task_id != NO_VAL) {
		char id[FORMAT_STRING_SIZE];
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->array_task_id);
		_print_str(id, width, right, true);
	} else {
		_print_str("N/A", width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_batch_host(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("EXEC_HOST", width, right, true);
	else {
		char *eh = job->batch_flag ? job->batch_host : job->alloc_node;
		char id[FORMAT_STRING_SIZE];

		snprintf(id, FORMAT_STRING_SIZE, "%s", eh ? eh : "n/a");
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_core_spec(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL) 	/* Print the Header instead */
		_print_str("CORE_SPEC", width, right, true);
	else
		_print_int(job->core_spec, width, right, true);
	return SLURM_SUCCESS;
}

int _print_job_job_id(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("JOBID", width, right, true);
	} else if ((job->array_task_id != NO_VAL) &&
		   !params.array_flag && IS_JOB_PENDING(job)  &&
		   job->node_inx) {
		uint32_t i, local_width = width, max_task_id = 0;
		char *id, *task_str;
		bitstr_t *task_bits;
		for (i = 1; i <= job->node_inx[0]; i++)
			max_task_id = MAX(max_task_id, job->node_inx[i]);
		task_bits = bit_alloc(max_task_id + 1);
		for (i = 1; i <= job->node_inx[0]; i++)
			bit_set(task_bits, job->node_inx[i]);
		if (local_width == 0) {
			local_width = bit_set_count(task_bits) *
				      FORMAT_STRING_SIZE;
		}
		id = xmalloc(local_width);
		task_str = xmalloc(local_width);
		bit_fmt(task_str, local_width, task_bits);
		snprintf(id, local_width, "%u_[%s]",
			 job->array_job_id, task_str);
		_print_str(id, width, right, true);
		bit_free(task_bits);
		xfree(id);
		xfree(task_str);
	} else if (job->array_task_id != NO_VAL) {
		char id[FORMAT_STRING_SIZE];
		snprintf(id, FORMAT_STRING_SIZE, "%u_%u",
			 job->array_job_id, job->array_task_id);
		_print_str(id, width, right, true);
	} else {
		char id[FORMAT_STRING_SIZE];
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->job_id);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_job_id2(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("JOBID", width, right, true);
	} else {
		char id[FORMAT_STRING_SIZE];
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->job_id);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_partition(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("PARTITION", width, right, true);
	else {
		char id[FORMAT_STRING_SIZE];
		snprintf(id, FORMAT_STRING_SIZE, "%s", job->partition);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_prefix(job_info_t * job, int width, bool right, char* suffix)
{
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_reason(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)        /* Print the Header instead */
		_print_str("REASON", width, right, true);
	else {
		char id[FORMAT_STRING_SIZE], *reason;
		if (job->state_desc)
			reason = job->state_desc;
		else
			reason = job_reason_string(job->state_reason);
		snprintf(id, FORMAT_STRING_SIZE, "%s", reason);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_name(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("NAME", width, right, true);
	else
		_print_str(job->name, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_licenses(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("LICENSES", width, right, true);
	else
		_print_str(job->licenses, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_wckey(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("WCKEY", width, right, true);
	else
		_print_str(job->wckey, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_user_id(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("USER", width, right, true);
	else
		_print_int(job->user_id, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_user_name(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("USER", width, right, true);
	else {
		char *uname = uid_to_string((uid_t) job->user_id);
		_print_str(uname, width, right, true);
		xfree(uname);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_gres(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("GRES", width, right, true);
	else
		_print_str(job->gres, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_group_id(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("GROUP", width, right, true);
	else
		_print_int(job->group_id, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_group_name(job_info_t * job, int width, bool right, char* suffix)
{
	struct group *group_info = NULL;

	if (job == NULL)	/* Print the Header instead */
		_print_str("GROUP", width, right, true);
	else {
		group_info = getgrgid((gid_t) job->group_id);
		if (group_info && group_info->gr_name[0])
			_print_str(group_info->gr_name, width, right, true);
		else
			_print_int(job->group_id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_job_state(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("STATE", width, right, true);
	else
		_print_str(job_state_string(job->job_state), width, right,
			   true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_job_state_compact(job_info_t * job, int width, bool right,
				 char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("ST", width, right, true);
	else
		_print_str(job_state_string_compact(job->job_state), width,
			   right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_time_left(job_info_t * job, int width, bool right,
			  char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("TIME_LEFT", width, right, true);
	else if (job->time_limit == INFINITE)
		_print_str("UNLIMITED", width, right, true);
	else if (job->time_limit == NO_VAL)
		_print_str("NOT_SET", width, right, true);
	else {
		time_t time_left = job->time_limit * 60 - job_time_used(job);
		_print_secs(time_left, width, right, false);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_time_limit(job_info_t * job, int width, bool right,
			  char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("TIMELIMIT", width, right, true);
	else if (job->time_limit == INFINITE)
		_print_str("UNLIMITED", width, right, true);
	else if (job->time_limit == NO_VAL)
		_print_str("NOT_SET", width, right, true);
	else
		_print_secs((job->time_limit*60), width, right, false);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_time_used(job_info_t * job, int width, bool right,
			   char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("TIME", width, right, true);
	else
		_print_secs(job_time_used(job), width, right, false);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

long job_time_used(job_info_t * job_ptr)
{
	time_t end_time;

	if ((job_ptr->start_time == 0) || IS_JOB_PENDING(job_ptr))
		return 0L;

	if (IS_JOB_SUSPENDED(job_ptr))
		return (long) job_ptr->pre_sus_time;

	if (IS_JOB_RUNNING(job_ptr) || (job_ptr->end_time == 0))
		end_time = time(NULL);
	else
		end_time = job_ptr->end_time;

	if (job_ptr->suspend_time)
		return (long) (difftime(end_time, job_ptr->suspend_time)
				+ job_ptr->pre_sus_time);
	return (long) (difftime(end_time, job_ptr->start_time));
}

int _print_job_time_start(job_info_t * job, int width, bool right,
			  char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("START_TIME", width, right, true);
	else
		_print_time(job->start_time, 0, width, right);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_time_end(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("END_TIME", width, right, true);
	else if ((job->time_limit == INFINITE) &&
		 (job->end_time > time(NULL)))
		_print_str("NONE", width, right, true);
	else
		_print_time(job->end_time, 0, width, right);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_priority(job_info_t * job, int width, bool right, char* suffix)
{
	char temp[FORMAT_STRING_SIZE];
	if (job == NULL)	/* Print the Header instead */
		_print_str("PRIORITY", width, right, true);
	else {
		double prio = (double) job->priority /
		              (double) ((uint32_t) 0xffffffff);
		sprintf(temp, "%16.14f", prio);
		_print_str(temp, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_priority_long(job_info_t * job, int width, bool right, char* suffix)
{
	char temp[FORMAT_STRING_SIZE];
	if (job == NULL)	/* Print the Header instead */
		_print_str("PRIORITY", width, right, true);
	else {
		sprintf(temp, "%u", job->priority);
		_print_str(temp, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_nodes(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL) {       /* Print the Header instead */
		char *title = "NODELIST";
		if (params.cluster_flags & CLUSTER_FLAG_BG)
			title = "MIDPLANELIST";
		_print_str(title, width, right, false);
	} else {
		char *nodes = xstrdup(job->nodes);
		char *ionodes = NULL;

		if (nodes) {
			select_g_select_jobinfo_get(job->select_jobinfo,
						    SELECT_JOBDATA_IONODES,
						    &ionodes);
		}
		if (ionodes) {
			xstrfmtcat(nodes, "[%s]", ionodes);
			xfree(ionodes);
			_print_str(nodes, width, right, false);
		} else
			_print_nodes(nodes, width, right, false);
		xfree(nodes);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_reason_list(job_info_t * job, int width, bool right,
		char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		char *title = "NODELIST(REASON)";
		if (params.cluster_flags & CLUSTER_FLAG_BG)
			title = "MIDPLANELIST(REASON)";
		_print_str(title, width, right, false);
	} else if (!IS_JOB_COMPLETING(job)
		   && (IS_JOB_PENDING(job)
		       || IS_JOB_TIMEOUT(job)
		       || IS_JOB_FAILED(job))) {
		char id[FORMAT_STRING_SIZE], *reason;
		if (job->state_desc)
			reason = job->state_desc;
		else
			reason = job_reason_string(job->state_reason);
		snprintf(id, FORMAT_STRING_SIZE, "(%s)", reason);
		_print_str(id, width, right, true);
	} else {
		char *nodes = xstrdup(job->nodes);
		char *ionodes = NULL;

		select_g_select_jobinfo_get(job->select_jobinfo,
					    SELECT_JOBDATA_IONODES,
					    &ionodes);
		if (ionodes) {
			xstrfmtcat(nodes, "[%s]", ionodes);
			xfree(ionodes);
			_print_str(nodes, width, right, false);
		} else
			_print_nodes(nodes, width, right, false);
		xfree(nodes);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_node_inx(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("NODE_BY_INDEX", width, right, true);
	else {
		int *current = job->node_inx;
		int curr_width = 0;
		while (*current != -1 && curr_width < width) {
			if (curr_width)
				printf(",");
			curr_width += _print_int(*current, width, right, true);
			current++;
		}
		while (curr_width < width)
			curr_width += printf(" ");
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_num_cpus(job_info_t * job, int width, bool right, char* suffix)
{
	char tmp_char[18];
	if (job == NULL)	/* Print the Header instead */
		_print_str("CPUS", width, right, true);
	else {
		if (params.cluster_flags & CLUSTER_FLAG_BG)
			convert_num_unit((float)job->num_cpus, tmp_char,
					 sizeof(tmp_char), UNIT_NONE);
		else
			snprintf(tmp_char, sizeof(tmp_char),
				 "%u", job->num_cpus);
       		_print_str(tmp_char, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_num_nodes(job_info_t * job, int width, bool right_justify,
			 char* suffix)
{
	uint32_t node_cnt = 0;
	char tmp_char[8];

	if (job == NULL)	/* Print the Header instead */
		_print_str("NODES", width, right_justify, true);
	else {
		if (params.cluster_flags & CLUSTER_FLAG_BG)
			select_g_select_jobinfo_get(job->select_jobinfo,
						    SELECT_JOBDATA_NODE_CNT,
						    &node_cnt);

		if ((node_cnt == 0) || (node_cnt == NO_VAL))
			node_cnt = _get_node_cnt(job);

		if (params.cluster_flags & CLUSTER_FLAG_BG)
			convert_num_unit((float)node_cnt, tmp_char,
					 sizeof(tmp_char), UNIT_NONE);
		else
			snprintf(tmp_char, sizeof(tmp_char), "%d", node_cnt);

		_print_str(tmp_char, width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

static int _get_node_cnt(job_info_t * job)
{
	int node_cnt = 0;

	/*  For PENDING jobs, return the maximum of the requested nodelist,
	 *   requested maximum number of nodes, or requested CPUs rounded
	 *   to nearest node.
	 *
	 *  For COMPLETING jobs, the job->nodes nodelist has already been
	 *   altered to list only the nodes still in the comp state, and
	 *   thus we count only those nodes toward the total nodes still
	 *   allocated to this job.
	 */

	if (IS_JOB_PENDING(job)) {
		node_cnt = _nodes_in_list(job->req_nodes);
		node_cnt = MAX(node_cnt, job->num_nodes);
		if ((node_cnt == 1) && (job->num_cpus > 1)
		    && job->ntasks_per_node
		    && (job->ntasks_per_node != (uint16_t) NO_VAL)) {
			int num_tasks = job->num_cpus;
			if (job->cpus_per_task != (uint16_t) NO_VAL)
				num_tasks /= job->cpus_per_task;
			node_cnt = (num_tasks + 1) / job->ntasks_per_node;
			if (node_cnt > num_tasks)
				node_cnt = num_tasks;
			else if (!node_cnt)
				node_cnt = 1;
		}
	} else
		node_cnt = _nodes_in_list(job->nodes);
	return node_cnt;
}

int _print_job_num_sct(job_info_t * job, int width, bool right_justify,
			 char* suffix)
{
	char sockets[10];
	char cores[10];
	char threads[10];
	char sct[(10+1)*3];
	if (job) {
		if (job->sockets_per_node == (uint16_t) NO_VAL)
			strcpy(sockets, "*");
		else
			convert_num_unit((float)job->sockets_per_node, sockets,
					sizeof(sockets), UNIT_NONE);
		if (job->cores_per_socket == (uint16_t) NO_VAL)
			strcpy(cores, "*");
		else
			convert_num_unit((float)job->cores_per_socket, cores,
					sizeof(cores), UNIT_NONE);
		if (job->threads_per_core == (uint16_t) NO_VAL)
			strcpy(threads, "*");
		else
			convert_num_unit((float)job->threads_per_core, threads,
					sizeof(threads), UNIT_NONE);
		sct[0] = '\0';
		strcat(sct, sockets);
		strcat(sct, ":");
		strcat(sct, cores);
		strcat(sct, ":");
		strcat(sct, threads);
		_print_str(sct, width, right_justify, true);
	} else {
		_print_str("S:C:T", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

static int _nodes_in_list(char *node_list)
{
	hostset_t host_set = hostset_create(node_list);
	int count = hostset_count(host_set);
	hostset_destroy(host_set);
	return count;
}

int _print_job_shared(job_info_t * job, int width, bool right_justify,
		      char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("SHARED", width, right_justify, true);
	else {
		switch (job->shared) {
		case 0:
			_print_str("no", width, right_justify, true);
			break;
		case 1:
		case 2:
			_print_str("yes", width, right_justify, true);
			break;
		case (uint16_t)NO_VAL:
		default:
			_print_str("unknwn", width, right_justify, true);
			break;
		}
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_contiguous(job_info_t * job, int width, bool right_justify,
			  char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("CONTIGUOUS", width, right_justify, true);
	else {
		_print_int(job->contiguous, width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_pn_min_cpus(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	char tmp_char[8];

	if (job == NULL)	/* Print the Header instead */
		_print_str("MIN_CPUS", width, right_justify, true);
	else {
		convert_num_unit((float)job->pn_min_cpus, tmp_char,
				 sizeof(tmp_char), UNIT_NONE);
		_print_str(tmp_char, width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_sockets(job_info_t * job, int width, bool right_justify,
		       char* suffix)
{
	char tmp_char[8];

	if (job == NULL)	/* Print the Header instead */
		_print_str("SOCKETS_PER_NODE", width, right_justify, true);
	else {
		if (job->sockets_per_node == (uint16_t) NO_VAL)
			strcpy(tmp_char, "*");
		else
			convert_num_unit((float)job->sockets_per_node, tmp_char,
				 sizeof(tmp_char), UNIT_NONE);
		_print_str(tmp_char, width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_cores(job_info_t * job, int width, bool right_justify,
		     char* suffix)
{
	char tmp_char[8];

	if (job == NULL)	/* Print the Header instead */
		_print_str("CORES_PER_SOCKET", width, right_justify, true);
	else {
		if (job->cores_per_socket == (uint16_t) NO_VAL)
			strcpy(tmp_char, "*");
		else
			convert_num_unit((float)job->cores_per_socket, tmp_char,
					sizeof(tmp_char), UNIT_NONE);
		_print_str(tmp_char, width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_threads(job_info_t * job, int width, bool right_justify,
		       char* suffix)
{
	char tmp_char[8];

	if (job == NULL)	/* Print the Header instead */
		_print_str("THREADS_PER_CORE", width, right_justify, true);
	else {
		if (job->threads_per_core == (uint16_t) NO_VAL)
			strcpy(tmp_char, "*");
		else
			convert_num_unit((float)job->threads_per_core, tmp_char,
					sizeof(tmp_char), UNIT_NONE);
		_print_str(tmp_char, width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_pn_min_memory(job_info_t * job, int width, bool right_justify,
			  char* suffix)
{
	char min_mem[10];
	char tmp_char[21];

	if (job == NULL)	/* Print the Header instead */
		_print_str("MIN_MEMORY", width, right_justify, true);
	else {
	    	tmp_char[0] = '\0';
		job->pn_min_memory &= (~MEM_PER_CPU);
		convert_num_unit((float)job->pn_min_memory, min_mem,
				 sizeof(min_mem), UNIT_NONE);
		strcat(tmp_char, min_mem);
		_print_str(tmp_char, width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int
_print_pn_min_tmp_disk(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	char tmp_char[10];

	if (job == NULL)	/* Print the Header instead */
		_print_str("MIN_TMP_DISK", width, right_justify, true);
	else {
		convert_num_unit((float)job->pn_min_tmp_disk,
				 tmp_char, sizeof(tmp_char), UNIT_NONE);
		_print_str(tmp_char, width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_req_nodes(job_info_t * job, int width, bool right_justify,
			 char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("REQ_NODES", width, right_justify, true);
	else
		_print_nodes(job->req_nodes, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_exc_nodes(job_info_t * job, int width, bool right_justify,
			 char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("EXC_NODES", width, right_justify, true);
	else
		_print_nodes(job->exc_nodes, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int
_print_job_req_node_inx(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("REQ_NODES_BY_INX", width, right_justify, true);
	else {
		int *current = job->req_node_inx;
		int curr_width = 0;
		while (*current != -1 && curr_width < width) {
			curr_width +=
			    _print_int(*current, width, right_justify,
				       true);
			printf(",");
		}
		while (curr_width < width)
			curr_width += printf(" ");
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int
_print_job_exc_node_inx(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("EXC_NODES_BY_INX", width, right_justify, true);
	else {
		int *current = job->exc_node_inx;
		int curr_width = 0;
		while (*current != -1 && curr_width < width) {
			curr_width +=
			    _print_int(*current, width, right_justify,
				       true);
			printf(",");
		}
		while (curr_width < width)
			curr_width += printf(" ");
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_features(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("FEATURES", width, right_justify, true);
	else
		_print_str(job->features, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_account(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	 /* Print the Header instead */
		_print_str("ACCOUNT", width, right_justify, true);
	else
		_print_str(job->account, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_comment(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	 /* Print the Header instead */
		_print_str("COMMENT", width, right_justify, true);
	else
		_print_str(job->comment, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_dependency(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("DEPENDENCY", width, right_justify, true);
	else if (job->dependency)
		_print_str(job->dependency, width, right_justify, true);
	else
		_print_str("", width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_qos(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	 /* Print the Header instead */
		_print_str("QOS", width, right_justify, true);
	else
		_print_str(job->qos, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_select_jobinfo(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	char select_buf[100];

	if (job == NULL)	/* Print the Header instead */
		select_g_select_jobinfo_sprint(NULL,
			select_buf, sizeof(select_buf), SELECT_PRINT_HEAD);
	else
		select_g_select_jobinfo_sprint(job->select_jobinfo,
			select_buf, sizeof(select_buf), SELECT_PRINT_DATA);
	_print_str(select_buf, width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_reservation(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)	 /* Print the Header instead */
		_print_str("RESERVATION", width, right_justify, true);
	else
		_print_str(job->resv_name, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_command(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)
		_print_str("COMMAND", width, right_justify, true);
	else
		_print_str(job->command, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_work_dir(job_info_t * job, int width, bool right_justify,
			char* suffix)
{
	if (job == NULL)
		_print_str("WORK_DIR", width, right_justify, true);
	else
		_print_str(job->work_dir, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}


/*****************************************************************************
 * Job Step Print Functions
 *****************************************************************************/
int print_step_from_format(job_step_info_t * job_step, List list)
{
	ListIterator i = list_iterator_create(list);
	step_format_t *current;
	int total_width = 0;

	while ((current = (step_format_t *) list_next(i)) != NULL) {
		if (current->
		    function(job_step, current->width,
			     current->right_justify, current->suffix)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
		if (current->width)
			total_width += current->width;
		else
			total_width += 10;
	}
	list_iterator_destroy(i);
	printf("\n");
#if 0
	if (job_step == NULL) {
		int inx;
		/* one-origin for no trailing space */
		for (inx=1; inx<total_width; inx++)
			printf("-");
		printf("\n");
	}
#endif
	return SLURM_SUCCESS;
}

int
step_format_add_function(List list, int width, bool right_justify,
			 char* suffix,
			 int (*function) (job_step_info_t *, int, bool, char*))
{
	step_format_t *tmp =
	    (step_format_t *) xmalloc(sizeof(step_format_t));
	tmp->function = function;
	tmp->width = width;
	tmp->right_justify = right_justify;
	tmp->suffix = suffix;

	if (list_append(list, tmp) == NULL) {
		fprintf(stderr, "Memory exhausted\n");
		exit(1);
	}
	return SLURM_SUCCESS;
}

int _print_step_id(job_step_info_t * step, int width, bool right, char* suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (step == NULL) {	/* Print the Header instead */
		_print_str("STEPID", width, right, true);
	} else if (step->array_job_id) {
		if (step->step_id == INFINITE) {	/* Pending */
			snprintf(id, FORMAT_STRING_SIZE, "%u_%u.TBD",
				 step->array_job_id, step->array_task_id);
		} else {
			snprintf(id, FORMAT_STRING_SIZE, "%u_%u.%u",
				 step->array_job_id, step->array_task_id,
				 step->step_id);
		}
		_print_str(id, width, right, true);
	} else {
		if (step->step_id == INFINITE) {	/* Pending */
			snprintf(id, FORMAT_STRING_SIZE, "%u.TBD", step->job_id);
		} else {
			snprintf(id, FORMAT_STRING_SIZE, "%u.%u",
				 step->job_id, step->step_id);
		}
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_partition(job_step_info_t * step, int width, bool right,
			  char* suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (step == NULL)	/* Print the Header instead */
		_print_str("PARTITION", width, right, true);
	else {
		snprintf(id, FORMAT_STRING_SIZE, "%s", step->partition);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_prefix(job_step_info_t * step, int width, bool right,
		       char* suffix)
{
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_user_id(job_step_info_t * step, int width, bool right,
			char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("USER", width, right, true);
	else
		_print_int(step->user_id, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_user_name(job_step_info_t * step, int width, bool right,
			  char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("USER", width, right, true);
	else {
		char *uname = uid_to_string((uid_t) step->user_id);
		_print_str(uname, width, right, true);
		xfree(uname);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_time_limit(job_step_info_t * step, int width, bool right,
			   char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("LIMIT", width, false, true);
	else if (step->time_limit == INFINITE)
		_print_str("UNLIMITED", width, right, true);
	else
		_print_secs(step->time_limit * 60, width, right, false);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_time_start(job_step_info_t * step, int width, bool right,
			   char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("START_TIME", width, false, true);
	else
		_print_time(step->start_time, 0, width, right);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_time_used(job_step_info_t * step, int width, bool right,
			   char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("TIME", width, right, true);
	else {
		long delta_t = step->run_time;
		_print_secs(delta_t, width, right, false);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_name(job_step_info_t * step, int width, bool right,
			char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("NAME", width, right, true);
	else
		_print_str(step->name, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_nodes(job_step_info_t * step, int width, bool right,
		      char* suffix)
{
	if (step == NULL) {	/* Print the Header instead */
		char *title = "NODELIST";
		if (params.cluster_flags & CLUSTER_FLAG_BG)
			title = "MIDPLANELIST";

		_print_str(title, width, right, false);
	} else {
		char *nodes = xstrdup(step->nodes);
		char *ionodes = NULL;

		if (nodes) {
			select_g_select_jobinfo_get(step->select_jobinfo,
						    SELECT_JOBDATA_IONODES,
						    &ionodes);
		}
		if (ionodes) {
			xstrfmtcat(nodes, "[%s]", ionodes);
			xfree(ionodes);
			_print_str(nodes, width, right, false);
		} else
			_print_nodes(nodes, width, right, false);
		xfree(nodes);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_num_tasks(job_step_info_t * step, int width, bool right,
			  char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("TASKS", width, right, true);
	else
		_print_int(step->num_tasks, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_gres(job_step_info_t * step, int width, bool right,
		     char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("GRES", width, right, true);
	else
		_print_str(step->gres, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

/* filter job records per input specifications,
 * returns >0 if job should be filter out (not printed) */
static int _filter_job(job_info_t * job)
{
	int filter;
	ListIterator iterator;
	uint32_t *user;
	uint16_t *state_id;
	char *account, *part, *qos, *name;
	squeue_job_step_t *job_step_id;

	if (params.job_list) {
		filter = 1;
		iterator = list_iterator_create(params.job_list);
		while ((job_step_id = list_next(iterator))) {
			if (((job_step_id->array_id == (uint16_t) NO_VAL)   &&
			     ((job_step_id->job_id   == job->array_job_id) ||
			      (job_step_id->job_id   == job->job_id)))      ||
			    ((job_step_id->array_id == job->array_task_id)  &&
			     (job_step_id->job_id   == job->array_job_id))) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 1;
	}

	if (params.part_list) {
		char *token = NULL, *last = NULL, *tmp_name = NULL;

		filter = 1;
		if (job->partition) {
			tmp_name = xstrdup(job->partition);
			token = strtok_r(tmp_name, ",", &last);
		}
		while (token && filter) {
			iterator = list_iterator_create(params.part_list);
			while ((part = list_next(iterator))) {
				if (strcmp(part, token) == 0) {
					filter = 0;
					break;
				}
			}
			list_iterator_destroy(iterator);
			token = strtok_r(NULL, ",", &last);
		}
		xfree(tmp_name);
		if (filter == 1)
			return 2;
	}

	if (params.account_list) {
		filter = 1;
		iterator = list_iterator_create(params.account_list);
		while ((account = list_next(iterator))) {
			 if ((job->account != NULL) &&
			     (strcasecmp(account, job->account) == 0)) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 2;
	}

	if (params.qos_list) {
		filter = 1;
		iterator = list_iterator_create(params.qos_list);
		while ((qos = list_next(iterator))) {
			 if ((job->qos != NULL) &&
			     (strcasecmp(qos, job->qos) == 0)) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 2;
	}

	if (params.state_list) {
		filter = 1;
		iterator = list_iterator_create(params.state_list);
		while ((state_id = list_next(iterator))) {
			if ((*state_id == job->job_state) ||
			    ((*state_id == JOB_COMPLETING) &&
			     (*state_id & job->job_state)) ||
			    ((*state_id == JOB_CONFIGURING) &&
			     (*state_id & job->job_state))) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 3;
	} else {
		if (!IS_JOB_PENDING(job) &&
		    !IS_JOB_RUNNING(job) &&
		    !IS_JOB_SUSPENDED(job) &&
		    !IS_JOB_COMPLETING(job))
			return 4;
	}

	if ((params.nodes)
	    && ((job->nodes == NULL)
		|| (!hostset_intersects(params.nodes, job->nodes))))
		return 5;

	if (params.user_list) {
		filter = 1;
		iterator = list_iterator_create(params.user_list);
		while ((user = list_next(iterator))) {
			if (*user == job->user_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 6;
	}

	if (params.reservation) {
		if ((job->resv_name == NULL) ||
		    (strcmp(job->resv_name, params.reservation))) {
			return 7;
		}
	}

	if (params.name_list) {
		filter = 1;
		iterator = list_iterator_create(params.name_list);
		while ((name = list_next(iterator))) {
			if ((job->name != NULL) &&
			     (strcasecmp(name, job->name) == 0)) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 8;
	}

	return 0;
}

/* filter step records per input specifications,
 * returns 1 if step should be filter out (not printed) */
static int _filter_step(job_step_info_t * step)
{
	int filter;
	ListIterator iterator;
	uint32_t *user;
	char *part;
	squeue_job_step_t *job_step_id;

	if (step->state == JOB_PENDING)
		return 1;

	if (params.job_list) {
		filter = 1;
		iterator = list_iterator_create(params.job_list);
		while ((job_step_id = list_next(iterator))) {
			if (((job_step_id->array_id == (uint16_t) NO_VAL)   &&
			     ((job_step_id->job_id  == step->array_job_id)  ||
			      (job_step_id->job_id  == step->job_id)))      ||
			    ((job_step_id->array_id == step->array_task_id) &&
			     (job_step_id->job_id   == step->array_job_id))) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 1;
	}

	if (params.part_list) {
		filter = 1;
		iterator = list_iterator_create(params.part_list);
		while ((part = list_next(iterator))) {
			if (strcmp(part, step->partition) == 0) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 2;
	}

	if (params.step_list) {
		filter = 1;
		iterator = list_iterator_create(params.step_list);
		while ((job_step_id = list_next(iterator))) {
			if (job_step_id->step_id != step->step_id)
				continue;
			if (((job_step_id->array_id == (uint16_t) NO_VAL)  &&
			     ((job_step_id->job_id  == step->array_job_id) ||
			      (job_step_id->job_id  == step->job_id)))      ||
			    ((job_step_id->array_id == step->array_task_id) &&
			     (job_step_id->job_id   == step->array_job_id))) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 3;
	}

	if ((params.nodes)
	    && ((step->nodes == NULL)
		|| (!hostset_intersects(params.nodes, step->nodes))))
		return 5;

	if (params.user_list) {
		filter = 1;
		iterator = list_iterator_create(params.user_list);
		while ((user = list_next(iterator))) {
			if (*user == step->user_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 6;
	}

	return 0;
}

int _print_com_invalid(void * p, int width, bool right, char* suffix)
{
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}
