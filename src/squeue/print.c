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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include "src/common/cpu_frequency.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/select.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/squeue/print.h"
#include "src/squeue/squeue.h"

static void	_combine_pending_array_tasks(List l);
static int	_filter_job(job_info_t * job);
static int	_filter_job_part(char *part_name);
static int	_filter_step(job_step_info_t * step);
static void	_job_list_del(void *x);
static uint32_t	_part_get_prio_tier(char *part_name);
static void	_part_state_free(void);
static void	_part_state_load(void);
static int	_print_str(char *str, int width, bool right, bool cut_output);

static int _print_job_from_format(void *x, void *arg);
static int _print_step_from_format(void *x, void *arg);

static partition_info_msg_t *part_info_msg = NULL;

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/

int print_jobs_array(job_info_t * jobs, int size, List format)
{
	squeue_job_rec_t *job_rec_ptr;
	char *tmp, *tok, *save_ptr = NULL;
	int i;
	List l;

	l = list_create(_job_list_del);
	if (!params.no_header)
		_print_job_from_format(NULL, format);
	_part_state_load();

	/* Filter out the jobs of interest */
	for (i = 0; i < size; i++) {
		if (_filter_job(&jobs[i]))
			continue;
		if (params.priority_flag) {
			tmp = xstrdup(jobs[i].partition);
			tok = strtok_r(tmp, ",", &save_ptr);
			while (tok) {
				if (_filter_job_part(tok) == 0) {
					job_rec_ptr = xmalloc(
						      sizeof(squeue_job_rec_t));
					job_rec_ptr->job_ptr = jobs + i;
					job_rec_ptr->part_name = xstrdup(tok);
					job_rec_ptr->part_prio =
						_part_get_prio_tier(tok);
					list_append(l, (void *) job_rec_ptr);
				}
				tok = strtok_r(NULL, ",", &save_ptr);
			}
			xfree(tmp);
		} else {
			if (_filter_job_part(jobs[i].partition))
				continue;
			job_rec_ptr = xmalloc(sizeof(squeue_job_rec_t));
			job_rec_ptr->job_ptr = jobs + i;
			/*
			 * Note: for multi-partition definitions the following
			 * is ill-defined, and you should really use the -P
			 * option for a consistent view.
			 * This will sort such jobs alphabetically, but that
			 * may be confusing as to why "foo,bar" and "bar,foo"
			 * submissions end up in different places.
			 */
			job_rec_ptr->part_name = xstrdup(jobs[i].partition);
			list_append(l, (void *) job_rec_ptr);
		}
	}

	_combine_pending_array_tasks(l);
	_part_state_free();
	sort_jobs_by_start_time (l);
	sort_job_list (l);

	/* Print the jobs of interest */
	list_for_each(l, _print_job_from_format, format);
	FREE_NULL_LIST(l);

	return SLURM_SUCCESS;
}

int print_steps_array(job_step_info_t * steps, int size, List format)
{
	if (!params.no_header)
		_print_step_from_format(NULL, format);

	if (size > 0) {
		int i;
		List step_list;

		step_list = list_create(NULL);

		/* Filter out the jobs of interest */
		for (i = 0; i < size; i++) {
			if (_filter_step(&steps[i]))
				continue;
			list_append(step_list, (void *) &steps[i]);
		}

		sort_step_list(step_list);

		/* Print the steps of interest */
		list_for_each(step_list, _print_step_from_format, format);
		FREE_NULL_LIST(step_list);
	}

	return SLURM_SUCCESS;
}

/* Combine a job array's task "reason" into the master job array record
 * reason as needed */
static void _merge_job_reason(job_info_t *job_ptr, job_info_t *task_ptr)
{
	char *task_desc;

	if (job_ptr->state_reason == task_ptr->state_reason)
		return;

	if (!job_ptr->state_desc) {
		job_ptr->state_desc =
			xstrdup(job_reason_string(job_ptr->state_reason));
	}
	task_desc = job_reason_string(task_ptr->state_reason);
	if (strstr(job_ptr->state_desc, task_desc))
		return;
	xstrfmtcat(job_ptr->state_desc, ",%s", task_desc);
}

/* Combine pending tasks of a job array into a single record.
 * The tasks may have been split into separate job records because they were
 * modified or started, but the records can be re-combined if pending. */
static void _combine_pending_array_tasks(List job_list)
{
	squeue_job_rec_t *job_rec_ptr, *task_rec_ptr;
	ListIterator job_iterator, task_iterator;
	bitstr_t *task_bitmap;
	int bitmap_size, update_cnt;

	if (params.array_flag)	/* Want to see each task separately */
		return;

	job_iterator = list_iterator_create(job_list);
	while ((job_rec_ptr = list_next(job_iterator))) {
		if (!IS_JOB_PENDING(job_rec_ptr->job_ptr) ||
		    !job_rec_ptr->job_ptr->array_task_str ||
		    !job_rec_ptr->job_ptr->array_bitmap)
			continue;
		update_cnt = 0;
		task_bitmap = job_rec_ptr->job_ptr->array_bitmap;
		bitmap_size = bit_size(task_bitmap);
		task_iterator = list_iterator_create(job_list);
		while ((task_rec_ptr = list_next(task_iterator))) {
			if (!IS_JOB_PENDING(task_rec_ptr->job_ptr))
				continue;	/* Not pending */
			if ((task_rec_ptr == job_rec_ptr) ||
			    (task_rec_ptr->job_ptr->array_job_id !=
			     job_rec_ptr->job_ptr->array_job_id) ||
			    (task_rec_ptr->job_ptr->array_task_id >=
			     bitmap_size))
				continue;	/* Different job array ID */
			if (xstrcmp(task_rec_ptr->job_ptr->name,
				    job_rec_ptr->job_ptr->name))
				continue;	/* Different name */
			if (xstrcmp(task_rec_ptr->job_ptr->partition,
				    job_rec_ptr->job_ptr->partition))
				continue;	/* Different partition */
			/* Want to see each reason separately */
			if (params.array_unique_flag)
				continue;
			/* Combine this task into master job array record */
			update_cnt++;
			_merge_job_reason(job_rec_ptr->job_ptr,
					  task_rec_ptr->job_ptr);
			bit_set(task_bitmap,
				task_rec_ptr->job_ptr->array_task_id);
			list_delete_item(task_iterator);
		}
		list_iterator_destroy(task_iterator);
		if (update_cnt) {
			int bitstr_len = -1;
			char *bitstr_len_str = getenv("SLURM_BITSTR_LEN");
			if (bitstr_len_str)
				bitstr_len = atoi(bitstr_len_str);
			if (bitstr_len < 0)
				bitstr_len = 64;
			xfree(job_rec_ptr->job_ptr->array_task_str);
			job_rec_ptr->job_ptr->array_task_str =
				xmalloc(bitstr_len);
			if (bitstr_len > 0)
				bit_fmt(job_rec_ptr->job_ptr->array_task_str,
					bitstr_len, task_bitmap);
			else {
				/* Print the full bitmap's string
				 * representation.  For huge bitmaps this can
				 * take roughly one minute, so let the client do
				 * the work */
				job_rec_ptr->job_ptr->array_task_str =
					bit_fmt_full(task_bitmap);
			}
		}
	}
	list_iterator_destroy(job_iterator);
}

static void _job_list_del(void *x)
{
	squeue_job_rec_t *job_rec_ptr = (squeue_job_rec_t *) x;
	xfree(job_rec_ptr->part_name);
	xfree(job_rec_ptr);
}

static uint32_t _part_get_prio_tier(char *part_name)
{
	partition_info_t *part_ptr;
	uint32_t part_prio = 1;	/* Default partition priority */
	int i;

	for (i = 0, part_ptr = part_info_msg->partition_array;
	     i < part_info_msg->record_count; i++, part_ptr++) {
		if (!xstrcmp(part_ptr->name, part_name)) {
			part_prio = part_ptr->priority_tier;
			break;
		}
	}
	return part_prio;
}

static void _part_state_free(void)
{
	if (part_info_msg) {
		slurm_free_partition_info_msg(part_info_msg);
		part_info_msg = NULL;
	}
}

static void _part_state_load(void)
{
	int rc;

	rc = slurm_load_partitions(0, &part_info_msg, SHOW_ALL);
	if (rc != SLURM_SUCCESS)
		slurm_perror ("slurm_load_partitions");
}

static int _print_str(char *str, int width, bool right, bool cut_output)
{
	char format[64];
	int printed = 0;

	if (right == true && width > 0)
		snprintf(format, 64, "%%%ds", width);
	else if (width > 0)
		snprintf(format, 64, "%%.%ds", width);
	else if (width < 0) {
		format[0] = '%';
		format[1] = 's';
		format[2] = ' ';
		format[3] = '\0';
	} else if (width == 0) {
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}

	if ((width <= 0) || (cut_output == false) ) {
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

	if ((time < 0) || (time > YEAR_SECONDS))
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
static int _print_one_job_from_format(job_info_t * job, List list)
{
	ListIterator iter = list_iterator_create(list);
	job_format_t *current;

	while ((current = list_next(iter))) {
		if (current->
		    function(job, current->width, current->right_justify,
			     current->suffix)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
	}
	list_iterator_destroy(iter);

	printf("\n");
	return SLURM_SUCCESS;
}

static int _print_job_from_format(void *x, void *arg)
{
	int i, i_first, i_last;
	bitstr_t *bitmap;
	squeue_job_rec_t *job_rec_ptr = (squeue_job_rec_t *) x;
	List list = (List) arg;

	if (!job_rec_ptr) {
		_print_one_job_from_format(NULL, list);
		return SLURM_SUCCESS;
	}

	if (job_rec_ptr->part_name) {
		xfree(job_rec_ptr->job_ptr->partition);
		job_rec_ptr->job_ptr->partition = xstrdup(job_rec_ptr->
							  part_name);

	}
	if (job_rec_ptr->job_ptr->array_task_str && params.array_flag) {
		char *p;

		if ((p = strchr(job_rec_ptr->job_ptr->array_task_str, '%')))
			*p = 0;
		bitmap = bit_alloc(slurm_conf.max_array_sz);
		bit_unfmt(bitmap, job_rec_ptr->job_ptr->array_task_str);
		xfree(job_rec_ptr->job_ptr->array_task_str);
		i_first = bit_ffs(bitmap);
		if (i_first == -1)
			i_last = -2;
		else
			i_last = bit_fls(bitmap);
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(bitmap, i))
				continue;
			job_rec_ptr->job_ptr->array_task_id = i;
			_print_one_job_from_format(job_rec_ptr->job_ptr, list);
		}
		FREE_NULL_BITMAP(bitmap);
	} else {
		_print_one_job_from_format(job_rec_ptr->job_ptr, list);
	}

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
	list_append(list, tmp);

	return SLURM_SUCCESS;
}

int _print_job_array_job_id(job_info_t * job, int width, bool right,
			    char* suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (job == NULL) {	/* Print the Header instead */
		_print_str("ARRAY_JOB_ID", width, right, true);
	} else if (job->array_task_str ||
		   (job->array_task_id != NO_VAL)) {
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->array_job_id);
		_print_str(id, width, right, true);
	} else {
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->job_id);
		_print_str(id, width, right, true);
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
	} else if (job->array_task_str) {
		_print_str(job->array_task_str, width, right, true);
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
		_print_str(eh ? eh : "n/a", width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_burst_buffer(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("BURST_BUFFER", width, right, true);
	else {
		_print_str(job->burst_buffer, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_burst_buffer_state(job_info_t * job, int width, bool right,
				  char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("BURST_BUFFER_STATE", width, right, true);
	else {
		_print_str(job->burst_buffer_state, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_cluster_name(job_info_t * job, int width, bool right,
			    char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("CLUSTER", width, right, true);
	else
		_print_str(job->cluster, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_container(job_info_t *job, int width, bool right, char *suffix)
{
	if (!job)		/* Print the Header instead */
		_print_str("CONTAINER", width, right, true);
	else
		_print_str(job->container, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_core_spec(job_info_t * job, int width, bool right, char* suffix)
{
	char spec[FORMAT_STRING_SIZE];

	if (job == NULL) {	/* Print the Header instead */
		_print_str("CORE_SPEC", width, right, true);
	} else if (job->core_spec == NO_VAL16) {
		_print_str("N/A", width, right, true);
	} else if (job->core_spec & CORE_SPEC_THREAD) {
		snprintf(spec, FORMAT_STRING_SIZE, "%d Threads",
			 (job->core_spec & (~CORE_SPEC_THREAD)));
		_print_str(spec, width, right, true);
	} else {
		_print_int(job->core_spec, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_delay_boot(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("DELAY_BOOT", width, right, true);
	else
		_print_secs((long)job->delay_boot, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_job_id(job_info_t * job, int width, bool right, char* suffix)
{
	char id[FORMAT_STRING_SIZE];
	int len;
	char *buf;

	if (job == NULL) {	/* Print the Header instead */
		_print_str("JOBID", width, right, true);
	} else if (job->array_task_str) {
		if (getenv("SLURM_BITSTR_LEN")) {
			len = strlen(job->array_task_str) + 64;
			buf = xmalloc(len);
			sprintf(buf, "%u_[%s]", job->array_job_id,
				job->array_task_str);
			_print_str(buf, width, right, false);
			xfree(buf);
		} else {
			snprintf(id, FORMAT_STRING_SIZE, "%u_[%s]",
				 job->array_job_id, job->array_task_str);
			_print_str(id, width, right, true);
		}
	} else if (job->array_task_id != NO_VAL) {
		snprintf(id, FORMAT_STRING_SIZE, "%u_%u",
			 job->array_job_id, job->array_task_id);
		_print_str(id, width, right, true);
	} else if (job->het_job_id) {
		snprintf(id, FORMAT_STRING_SIZE, "%u+%u",
			 job->het_job_id, job->het_job_offset);
		_print_str(id, width, right, true);
	} else {
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
		_print_str(job->partition, width, right, true);
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
		char *reason;
		if (job->state_desc)
			reason = job->state_desc;
		else
			reason = job_reason_string(job->state_reason);
		_print_str(reason, width, right, true);
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
		_print_str("UID", width, right, true);
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
		char *uname = uid_to_string_cached((uid_t) job->user_id);
		_print_str(uname, width, right, true);
	}
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

int _print_job_last_sched_eval(job_info_t *job, int width, bool right,
			       char *suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("LAST_SCHED_EVAL", width, right, true);
	else
		_print_time(job->last_sched_eval, 0, width, right);
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
		_print_str("TIME_LIMIT", width, right, true);
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
int _print_job_het_job_offset(job_info_t * job, int width, bool right,
			      char* suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (job == NULL)	/* Print the Header instead */
		_print_str("HET_JOB_OFFSET", width, right, true);
	else if (job->het_job_id == 0)
		_print_str("N/A", width, right, true);
	else {
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->het_job_offset);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_het_job_id(job_info_t * job, int width, bool right,
			  char* suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (job == NULL)	/* Print the Header instead */
		_print_str("HET_JOB_ID", width, right, true);
	else if (job->het_job_id == 0)
		_print_str("N/A", width, right, true);
	else {
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->het_job_id);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_het_job_id_set(job_info_t * job, int width, bool right,
			      char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("HET_JOB_ID_SET", width, right, true);
	else if (job->het_job_id == 0)
		_print_str("N/A", width, right, true);
	else
		_print_str(job->het_job_id_set, width, right, true);

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

int _print_job_time_submit(job_info_t * job, int width, bool right,
			  char* suffix)
{
	if (job == NULL)        /* Print the Header instead */
		_print_str("SUBMIT_TIME", width, right, true);
	else
		_print_time(job->submit_time, 0, width, right);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_time_pending(job_info_t *job, int width, bool right,
			    char *suffix)
{
	time_t now = time(NULL);

	/*
	 * If the job has started, defined as (start - submit).
	 * Else, defined as (now - submit).
	 */

	if (!job)	/* Print the Header instead */
		_print_str("PENDING_TIME", width, right, true);
	else if (job->start_time && (job->start_time < now))
		_print_int((job->start_time - job->submit_time), width, right,
			   true);
	else
		_print_int((now - job->submit_time), width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
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
int _print_job_deadline(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)        /* Print the Header instead */
		_print_str("DEADLINE", width, right, true);
	else
		_print_time(job->deadline, 0, width, right);
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
	if (job == NULL)	/* Print the Header instead */
		_print_str("NODELIST", width, right, false);
	else
		_print_nodes(job->nodes, width, right, false);

	if (suffix)
		printf("%s", suffix);

	return SLURM_SUCCESS;
}

int _print_job_schednodes(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("SCHEDNODES", width, right, false);
	else
		_print_str(job->sched_nodes, width, right, false);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_reason_list(job_info_t * job, int width, bool right,
		char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("NODELIST(REASON)", width, right, false);
	} else if (!IS_JOB_COMPLETING(job)
		   && (IS_JOB_PENDING(job)
		       || IS_JOB_STAGE_OUT(job)
		       || IS_JOB_TIMEOUT(job)
		       || IS_JOB_OOM(job)
		       || IS_JOB_DEADLINE(job)
		       || IS_JOB_FAILED(job))) {
		char *reason_fmt = NULL, *reason = NULL;
		if (job->state_desc)
			reason = job->state_desc;
		else
			reason = job_reason_string(job->state_reason);
		xstrfmtcat(reason_fmt, "(%s)", reason);
		_print_str(reason_fmt, width, right, true);
		xfree(reason_fmt);
	} else
		_print_nodes(job->nodes, width, right, false);

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
		snprintf(tmp_char, sizeof(tmp_char), "%u", job->num_cpus);
       		_print_str(tmp_char, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_num_nodes(job_info_t * job, int width, bool right_justify,
			 char* suffix)
{
	char tmp_char[8];

	if (job == NULL)	/* Print the Header instead */
		_print_str("NODES", width, right_justify, true);
	else {
		snprintf(tmp_char, sizeof(tmp_char), "%d", job->num_nodes);
		_print_str(tmp_char, width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_num_sct(job_info_t * job, int width, bool right_justify,
			 char* suffix)
{
	char sockets[10];
	char cores[10];
	char threads[10];
	char *sct = NULL;
	if (job) {
		if (job->sockets_per_node == NO_VAL16)
			strcpy(sockets, "*");
		else
			convert_num_unit((float)job->sockets_per_node, sockets,
					sizeof(sockets), UNIT_NONE, NO_VAL,
					params.convert_flags);
		if (job->cores_per_socket == NO_VAL16)
			strcpy(cores, "*");
		else
			convert_num_unit((float)job->cores_per_socket, cores,
					sizeof(cores), UNIT_NONE, NO_VAL,
					params.convert_flags);
		if (job->threads_per_core == NO_VAL16)
			strcpy(threads, "*");
		else
			convert_num_unit((float)job->threads_per_core, threads,
					sizeof(threads), UNIT_NONE, NO_VAL,
					params.convert_flags);
		xstrfmtcat(sct, "%s:%s:%s", sockets, cores, threads);
		_print_str(sct, width, right_justify, true);
		xfree(sct);
	} else {
		_print_str("S:C:T", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_num_tasks(job_info_t * job, int width, bool right, char* suffix)
{
	char tmp_char[18];
	if (job == NULL) {	/* Print the Header instead */
		_print_str("TASKS", width, right, true);
	} else {
		snprintf(tmp_char, sizeof(tmp_char), "%u", job->num_tasks);
       		_print_str(tmp_char, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_over_subscribe(job_info_t * job, int width, bool right_justify,
			      char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("OVER_SUBSCRIBE", width, right_justify, true);
	} else {
		_print_str(job_share_string(job->shared),
			   width, right_justify, true);
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
				 sizeof(tmp_char), UNIT_NONE, NO_VAL,
				 params.convert_flags);
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
		if (job->sockets_per_node == NO_VAL16)
			strcpy(tmp_char, "*");
		else
			convert_num_unit((float)job->sockets_per_node, tmp_char,
				 sizeof(tmp_char), UNIT_NONE, NO_VAL,
				 params.convert_flags);
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
		if (job->cores_per_socket == NO_VAL16)
			strcpy(tmp_char, "*");
		else
			convert_num_unit((float)job->cores_per_socket, tmp_char,
					sizeof(tmp_char), UNIT_NONE, NO_VAL,
					params.convert_flags);
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
		if (job->threads_per_core == NO_VAL16)
			strcpy(tmp_char, "*");
		else
			convert_num_unit((float)job->threads_per_core, tmp_char,
					sizeof(tmp_char), UNIT_NONE, NO_VAL,
					params.convert_flags);
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

	if (job == NULL)	/* Print the Header instead */
		_print_str("MIN_MEMORY", width, right_justify, true);
	else {
		job->pn_min_memory &= (~MEM_PER_CPU);
		convert_num_unit((float)job->pn_min_memory, min_mem,
				 sizeof(min_mem), UNIT_MEGA, NO_VAL,
				 params.convert_flags);
		_print_str(min_mem, width, right_justify, true);
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
		convert_num_unit((float)job->pn_min_tmp_disk, tmp_char,
				 sizeof(tmp_char), UNIT_MEGA, NO_VAL,
				 params.convert_flags);
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

int _print_job_cluster_features(job_info_t * job, int width, bool right_justify,
				char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("CLUSTER_FEATURES", width, right_justify, true);
	else
		_print_str(job->cluster_features, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_prefer(job_info_t * job, int width, bool right_justify,
		      char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("PREFER", width, right_justify, true);
	else
		_print_str(job->prefer, width, right_justify, true);
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

int _print_job_admin_comment(job_info_t * job, int width, bool right_justify,
			     char* suffix)
{
	if (job == NULL)	 /* Print the Header instead */
		_print_str("ADMIN_COMMENT", width, right_justify, true);
	else
		_print_str(job->admin_comment, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_system_comment(job_info_t * job, int width, bool right_justify,
			      char* suffix)
{
	if (job == NULL)	 /* Print the Header instead */
		_print_str("SYSTEM_COMMENT", width, right_justify, true);
	else
		_print_str(job->system_comment, width, right_justify, true);
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
	else
		_print_str(job->dependency, width, right_justify, true);
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

int _print_job_nice(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("NICE", width, right_justify, true);
	else {
		int nice = (int) job->nice;
		nice -= NICE_OFFSET;
		_print_int(nice, width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_accrue_time(job_info_t * job, int width, bool right_justify,
			   char* suffix)
{
	if (job == NULL)
		_print_str("ACCRUE_TIME", width, right_justify, true);
	else {
		_print_time(job->accrue_time, 0, width, right_justify);
	}

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_alloc_nodes(job_info_t * job, int width, bool right_justify,
			   char* suffix)
{
	if (job == NULL)
		_print_str("ALLOC_NODES", width, right_justify, true);
	else
		_print_str(job->alloc_node, width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_alloc_sid(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("ALLOC_SID", width, right_justify, true);
	else
		_print_int(job->alloc_sid, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_assoc_id(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("ASSOC_ID", width, right_justify, true);
	else
		_print_int(job->assoc_id, width, right_justify,true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;

}

int _print_job_batch_flag(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("BATCH_FLAG", width, right_justify, true);
	else
		_print_int(job->batch_flag, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_boards_per_node(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("BOARDS_PER_NODE", width, right_justify, true);
	else if (job->boards_per_node == NO_VAL16)
		_print_str("N/A", width, right_justify, true);
	else
		_print_int(job->boards_per_node, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_cpus_per_task(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("CPUS_PER_TASK", width, right_justify, true);
	else if (job->cpus_per_task == NO_VAL16)
		_print_str("N/A", width, right_justify, true);
	else
		_print_int(job->cpus_per_task, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_derived_ec(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("DERIVED_EC", width, right_justify, true);
	else
		_print_int(job->derived_ec, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;

}

int _print_job_eligible_time(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("ELIGIBLE_TIME", width, right_justify, true);
	else {
		_print_time(job->eligible_time, 0, width, right_justify);
	}

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_exit_code(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("EXIT_CODE", width, right_justify, true);
	else
		_print_int(job->exit_code, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_fed_origin(job_info_t * job, int width, bool right_justify,
			    char* suffix)
{
	if (job == NULL)
		_print_str("ORIGIN", width, right_justify, true);
	else {
		if (job->fed_origin_str)
			_print_str(job->fed_origin_str, width, right_justify,
				   true);
		else
			_print_str("NA", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_fed_origin_raw(job_info_t * job, int width, bool right_justify,
			      char* suffix)
{
	if (job == NULL)
		_print_str("ORIGIN_RAW", width, right_justify, true);
	else {
		int id = job->job_id >> 26;
		if (id)
			_print_int(id, width, right_justify, true);
		else
			_print_str("NA", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_fed_siblings_active(job_info_t * job, int width,
				   bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("ACTIVE_SIBLINGS", width, right_justify, true);
	else {
		if (job->fed_siblings_active_str)
			_print_str(job->fed_siblings_active_str, width, right_justify,
				   true);
		else
			_print_str("NA", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_fed_siblings_active_raw(job_info_t * job, int width,
				       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("ACTIVE_SIBLINGS_RAW", width, right_justify, true);
	else {
		int bit = 1;
		char *ids = NULL;
		uint64_t tmp_sibs = job->fed_siblings_active;
		while (tmp_sibs) {
			if (tmp_sibs & 1)
				xstrfmtcat(ids, "%s%d", (ids) ? "," : "", bit);

			tmp_sibs >>= 1;
			bit++;
		}
		if (ids)
			_print_str(ids, width, right_justify, true);
		else
			_print_str("NA", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_fed_siblings_viable(job_info_t * job, int width,
				   bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("VIABLE_SIBLINGS", width, right_justify, true);
	else {
		if (job->fed_siblings_viable_str)
			_print_str(job->fed_siblings_viable_str, width,
				   right_justify, true);
		else
			_print_str("NA", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_fed_siblings_viable_raw(job_info_t * job, int width,
				       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("VIALBLE_SIBLINGS_RAW", width, right_justify, true);
	else {
		int bit = 1;
		char *ids = NULL;
		uint64_t tmp_sibs = job->fed_siblings_viable;
		while (tmp_sibs) {
			if (tmp_sibs & 1)
				xstrfmtcat(ids, "%s%d", (ids) ? "," : "", bit);

			tmp_sibs >>= 1;
			bit++;
		}
		if (ids)
			_print_str(ids, width, right_justify, true);
		else
			_print_str("NA", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_max_cpus(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("MAX_CPUS", width, right_justify, true);
	else if (job->max_cpus != 0)
		_print_int(job->max_cpus, width, right_justify, true);
	else
		_print_int(job->num_cpus, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_max_nodes(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("MAX_NODES", width, right_justify, true);
	else if (job->max_nodes != 0)
		_print_int(job->max_nodes, width, right_justify, true);
	else
		_print_int(job->num_nodes, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_network(job_info_t * job, int width, bool right_justify,
		    char* suffix)
{
	if (job == NULL)
		_print_str("NETWORK", width, right_justify, true);
	else
		_print_str(job->network, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_ntasks_per_core(job_info_t * job, int width, bool right_justify,
			       char* suffix)
{
	if (job == NULL)
		_print_str("NTASKS_PER_CORE", width, right_justify, true);
	else if ((job->ntasks_per_core == NO_VAL16) ||
		 (job->ntasks_per_core == INFINITE16))
		_print_str("N/A", width, right_justify, true);
	else
		_print_int(job->ntasks_per_core, width, right_justify, true);

	if(suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_ntasks_per_node(job_info_t * job, int width, bool right_justify,
			       char* suffix)
{
	if (job == NULL)
		_print_str("NTASKS_PER_NODE", width, right_justify, true);
	else if ((job->ntasks_per_node == NO_VAL16) ||
		 (job->ntasks_per_node == INFINITE16))
		_print_str("N/A", width, right_justify, true);
	else
		_print_int(job->ntasks_per_node, width, right_justify, true);

	if(suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_ntasks_per_socket(job_info_t * job, int width,
				 bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("NTASKS_PER_SOCKET", width, right_justify, true);
	else if ((job->ntasks_per_socket == NO_VAL16) ||
		 (job->ntasks_per_socket == INFINITE16))
		_print_str("N/A", width, right_justify, true);
	else
		_print_int(job->ntasks_per_socket, width, right_justify, true);

	if(suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_ntasks_per_board(job_info_t * job, int width,
				 bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("NTASKS_PER_BOARD", width, right_justify, true);
	else if ((job->ntasks_per_board == NO_VAL16) ||
		 (job->ntasks_per_board == INFINITE16))
		_print_str("N/A", width, right_justify, true);
	else
		_print_int(job->ntasks_per_board, width, right_justify, true);

	if(suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_preempt_time(job_info_t * job, int width,
				 bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("PREEMPT_TIME", width, right_justify, true);
	else if (job->preempt_time == INFINITE)
		_print_str("UNLIMITED", width, right_justify, true);
	else if (job->preempt_time == NO_VAL)
		_print_str("NOT_SET", width, right_justify, true);
	else
		_print_time(job->preempt_time, 0, width, right_justify);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;

}

int _print_job_profile(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("PROFILE", width, right_justify, true);
	else
		_print_str(acct_gather_profile_to_string(job->profile),
			   width, right_justify, true);
	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_reboot(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("REBOOT", width, right_justify, true);
	else
		_print_int(job->reboot, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_req_switch(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("REQ_SWITCH", width, right_justify, true);
	else
		_print_int(job->req_switch, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;
}

int _print_job_requeue(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("REQUEUE", width, right_justify, true);
	else
		_print_int(job->requeue, width, right_justify, true);

	if (suffix)
		printf("%s",suffix);
	return SLURM_SUCCESS;

}

int _print_job_resize_time(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("RESIZE_TIME", width, right_justify, true);
	else if (job->resize_time)
		_print_secs((job->resize_time*60), width, right_justify, true);
	else
		_print_str("N/A", width, right_justify, false);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;

}

int _print_job_restart_cnt(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("RESTART_COUNT", width, right_justify, true);
	else
		_print_int(job->restart_cnt, width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_sockets_per_board(job_info_t * job, int width,
				 bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("SOCKETS_PER_BOARD", width, right_justify, true);
	else
		_print_int(job->sockets_per_board, width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;

}

int _print_job_std_err(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	char tmp_line[1024];

	if (job == NULL)
		_print_str("STDERR", width, right_justify, true);
	else if (!job->batch_flag)
		_print_str("N/A", width, right_justify, true);
	else if (job->std_err)
		_print_str(job->std_err, width, right_justify, true);
	else if (job->std_out)
		_print_str(job->std_out, width, right_justify, true);
	else {
		snprintf(tmp_line,sizeof(tmp_line), "%s/slurm-%u.out",
			 job->work_dir, job->job_id);

		_print_str(tmp_line, width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_std_in(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("STDIN", width, right_justify, true);
	else
		_print_str(job->std_in, width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;

}

int _print_job_std_out(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	char tmp_line[1024];

	if (job == NULL)
		_print_str("STDOUT", width, right_justify, true);
	else if (job->std_out)
		_print_str(job->std_out, width, right_justify, true);
	else {
		snprintf(tmp_line,sizeof(tmp_line), "%s/slurm-%u.out",
			 job->work_dir, job->job_id);

		_print_str(tmp_line, width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_min_time(job_info_t * job, int width,
		       bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("TIME_MIN", width, right_justify, true);
	else
		_print_secs((job->time_min*60), width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_wait4switch(job_info_t * job, int width,
			   bool right_justify, char* suffix)
{
	if (job == NULL)
		_print_str("WAIT4SWITCH", width, right_justify, true);
	else
		_print_secs(job->wait4switch,
			    width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_cpus_per_tres(job_info_t *job, int width,
			     bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("CPUS_PER_TRES", width, right_justify, true);
	} else {
		if (job->cpus_per_tres)
			_print_str(job->cpus_per_tres, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_mem_per_tres(job_info_t *job, int width,
			    bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("MEM_PER_TRES", width, right_justify, true);
	} else {
		if (job->mem_per_tres)
			_print_str(job->mem_per_tres, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_tres_alloc(job_info_t *job, int width,
			  bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("TRES_ALLOC", width, right_justify, true);
	} else {
		if (job->tres_alloc_str)
			_print_str(job->tres_alloc_str, width,
				   right_justify, true);
		else if (job->tres_req_str)
			_print_str(job->tres_req_str, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_tres_bind(job_info_t *job, int width,
			 bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("TRES_BIND", width, right_justify, true);
	} else {
		if (job->tres_bind)
			_print_str(job->tres_bind, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_tres_freq(job_info_t *job, int width,
			 bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("TRES_FREQ", width, right_justify, true);
	} else {
		if (job->tres_freq)
			_print_str(job->tres_freq, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_tres_per_job(job_info_t *job, int width,
			    bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("TRES_PER_JOB", width, right_justify, true);
	} else {
		if (job->tres_per_job)
			_print_str(job->tres_per_job, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_tres_per_node(job_info_t *job, int width,
			     bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("TRES_PER_NODE", width, right_justify, true);
	} else {
		if (job->tres_per_node)
			_print_str(job->tres_per_node, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_tres_per_socket(job_info_t *job, int width,
			       bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("TRES_PER_SOCKET", width, right_justify, true);
	} else {
		if (job->tres_per_socket)
			_print_str(job->tres_per_socket, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_tres_per_task(job_info_t *job, int width,
			     bool right_justify, char *suffix)
{
	if (job == NULL) {
		_print_str("TRES_PER_TASK", width, right_justify, true);
	} else {
		if (job->tres_per_task)
			_print_str(job->tres_per_task, width,
				   right_justify, true);
		else
			_print_str("N/A", width,
				   right_justify, true);

	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_mcs_label(job_info_t * job, int width,
			bool right, char* suffix)
{
	if (job == NULL)
		_print_str("MCSLABEL", width, right, true);
	else
		_print_str(job->mcs_label, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

/*****************************************************************************
 * Job Step Print Functions
 *****************************************************************************/
static int _print_step_from_format(void *x, void *arg)
{
	job_step_info_t *job_step = (job_step_info_t *) x;
	List list = (List) arg;
	ListIterator i = list_iterator_create(list);
	step_format_t *current;

	while ((current = list_next(i))) {
		if (current->
		    function(job_step, current->width,
			     current->right_justify, current->suffix)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
	}
	list_iterator_destroy(i);
	printf("\n");

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
	list_append(list, tmp);

	return SLURM_SUCCESS;
}

int _print_step_cluster_name(job_step_info_t *step, int width, bool right,
			     char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("CLUSTER", width, right, true);
	else
		_print_str(step->cluster, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_container(job_step_info_t *step, int width, bool right,
			  char *suffix)
{
	if (!step)		/* Print the Header instead */
		_print_str("CONTAINER", width, right, true);
	else
		_print_str(step->container, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_id(job_step_info_t * step, int width, bool right, char* suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (step == NULL) {	/* Print the Header instead */
		_print_str("STEPID", width, right, true);
	} else {
		uint16_t flags = STEP_ID_FLAG_NO_PREFIX;
		int len = FORMAT_STRING_SIZE;
		int pos = 0;
		if (step->array_job_id) {
			pos = snprintf(id, len, "%u_%u.",
				       step->array_job_id, step->array_task_id);
			flags |= STEP_ID_FLAG_NO_JOB;
			len -= pos;
		}

		log_build_step_id_str(&step->step_id,
				      id+pos, len, flags);

		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_partition(job_step_info_t * step, int width, bool right,
			  char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("PARTITION", width, right, true);
	else {
		_print_str(step->partition, width, right, true);
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
		_print_str("UID", width, right, true);
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
		char *uname = uid_to_string_cached((uid_t) step->user_id);
		_print_str(uname, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_time_limit(job_step_info_t * step, int width, bool right,
			   char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("TIME_LIMIT", width, right, true);
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
	if (step == NULL)	/* Print the Header instead */
		_print_str("NODELIST", width, right, false);
	else
		_print_nodes(step->nodes, width, right, false);

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

int _print_step_array_job_id(job_step_info_t * step, int width, bool right,
			     char* suffix)
{
	if (step == NULL)
		_print_str("ARRAY_JOB_ID", width, right, true);
	else if (step->array_job_id != NO_VAL)
		_print_int(step->array_job_id, width, right, true);
	else
		_print_int(step->step_id.job_id, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_array_task_id(job_step_info_t * step, int width, bool right,
			      char* suffix)
{
	if (step == NULL)
		_print_str("ARRAY_TASK_ID", width, right, true);
	else if (step->array_task_id != NO_VAL)
		_print_int(step->array_task_id, width, right, true);
	else
		_print_str("N/A", width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;

}

int _print_step_job_id(job_step_info_t * step, int width, bool right,
		       char* suffix)
{
	if (step == NULL)
		_print_str("JOB_ID", width, right, true);
	else
		_print_int(step->step_id.job_id, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_network(job_step_info_t * step, int width, bool right,
			char* suffix)
{
	if (step == NULL)
		_print_str("NETWORK", width, right, true);
	else
		_print_str(step->network, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_node_inx(job_step_info_t * step, int width, bool right,
			 char* suffix)
{
	if (step == NULL)
		_print_str("NODE_INDEX", width, right, true);
	else {
		int *current = step->node_inx;
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

int _print_step_num_cpus(job_step_info_t * step, int width, bool right,
			 char* suffix)
{
	if (step == NULL)
		_print_str("NUM_CPUS", width, right, true);
	else
		_print_int(step->num_cpus, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_cpu_freq(job_step_info_t * step, int width, bool right,
			 char* suffix)
{
	char bfm[16], bfx[16], bfg[16], bfall[48];

	if (step == NULL) {
		_print_str("CPU_FREQ", width, right, true);
		if (suffix)
			printf("%s", suffix);
		return SLURM_SUCCESS;
	}
	cpu_freq_to_string(bfm, sizeof(bfm), step->cpu_freq_min);
	cpu_freq_to_string(bfx, sizeof(bfx), step->cpu_freq_max);
	cpu_freq_to_string(bfg, sizeof(bfg), step->cpu_freq_gov);
	snprintf(bfall, sizeof(bfall), "%s-%s:%s", bfm, bfx, bfg);
	_print_str(bfall, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_resv_ports(job_step_info_t * step, int width, bool right,
			   char* suffix)
{
	if (step == NULL)
		_print_str("RESERVED_PORTS", width, right, true);
	else
		_print_str(step->resv_ports, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_state(job_step_info_t * step, int width, bool right,
			char* suffix)
{
	if (step == NULL)
		_print_str("STATE", width, right, true);
	else
		_print_str(job_state_string(step->state), width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_cpus_per_tres(job_step_info_t * step, int width, bool right,
			      char* suffix)
{
	if (step == NULL)
		_print_str("CPUS_PER_TRES", width, right, true);
	else
		_print_str(step->cpus_per_tres, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_mem_per_tres(job_step_info_t * step, int width, bool right,
			     char* suffix)
{
	if (step == NULL)
		_print_str("MEM_PER_TRES", width, right, true);
	else
		_print_str(step->mem_per_tres, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_tres_bind(job_step_info_t * step, int width, bool right,
			  char* suffix)
{
	if (step == NULL)
		_print_str("TRES_BIND", width, right, true);
	else
		_print_str(step->tres_bind, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_tres_freq(job_step_info_t * step, int width, bool right,
			  char* suffix)
{
	if (step == NULL)
		_print_str("TRES_FREQ", width, right, true);
	else
		_print_str(step->tres_freq, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_tres_per_step(job_step_info_t * step, int width, bool right,
			      char* suffix)
{
	if (step == NULL)
		_print_str("TRES_PER_STEP", width, right, true);
	else
		_print_str(step->tres_per_step, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_tres_per_node(job_step_info_t * step, int width, bool right,
			      char* suffix)
{
	if (step == NULL)
		_print_str("TRES_PER_JOB", width, right, true);
	else
		_print_str(step->tres_per_node, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_tres_per_socket(job_step_info_t * step, int width, bool right,
				char* suffix)
{
	if (step == NULL)
		_print_str("TRES_PER_SOCKET", width, right, true);
	else
		_print_str(step->tres_per_socket, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_tres_per_task(job_step_info_t * step, int width, bool right,
			      char* suffix)
{
	if (step == NULL)
		_print_str("TRES_PER_TASK", width, right, true);
	else
		_print_str(step->tres_per_task, width, right, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

/*
 * Filter job records per input specifications,
 * Returns >0 if job should be filter out (not printed)
 * Returns 0 if job record should be printed
 */
static int _filter_job(job_info_t * job)
{
	int i, filter;
	ListIterator iterator;
	uint32_t *user;
	uint32_t *state_id;
	char *account, *license, *qos, *name;
	squeue_job_step_t *job_step_id;
	bool partial_array = false;

	if (job->job_id == 0)
		return 1;

	if (params.job_list) {
		filter = 1;
		iterator = list_iterator_create(params.job_list);
		while ((job_step_id = list_next(iterator))) {
			if (((job_step_id->array_id == NO_VAL)             &&
			     ((job_step_id->step_id.job_id ==
			       job->array_job_id) ||
			      (job_step_id->step_id.job_id ==
			       job->job_id))) ||
			    ((job_step_id->array_id == job->array_task_id) &&
			     (job_step_id->step_id.job_id ==
			      job->array_job_id))) {
				filter = 0;
				break;
			}
			if ((job_step_id->array_id != NO_VAL)             &&
			    (job_step_id->step_id.job_id ==
			     job->array_job_id) &&
			    (job->array_bitmap &&
			     bit_test(job->array_bitmap,
				      job_step_id->array_id))) {
				filter = 0;
				partial_array = true;
				break;
			}
			if (job_step_id->step_id.job_id == job->het_job_id) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 1;
	}

	if (params.licenses_list) {
		char *token = NULL, *last = NULL, *tmp_name = NULL;
		char *tmp_token;

		filter = 1;
		if (job->licenses) {
			tmp_name = xstrdup(job->licenses);
			token = strtok_r(tmp_name, ",", &last);
		}
		while (token && filter) {
			/* Consider license name only, ignore ":" lic count */
			tmp_token = token;
			while (*tmp_token) {
				if (*tmp_token == ':') {
					*tmp_token = '\0';
					break;
				}
				tmp_token++;
			}
			iterator = list_iterator_create(params.licenses_list);
			while ((license = list_next(iterator))) {
				if (xstrcmp(token, license) == 0) {
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
			     (xstrcasecmp(account, job->account) == 0)) {
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
			     (xstrcasecmp(qos, job->qos) == 0)) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 2;
	}

	if (params.all_states) {
	} else if (params.state_list) {
		filter = 1;
		iterator = list_iterator_create(params.state_list);
		while ((state_id = list_next(iterator))) {
			bool match = false;
			job->job_state &= ~JOB_UPDATE_DB;
			if (*state_id &  JOB_STATE_FLAGS) {
				if (*state_id &  job->job_state)
					match = true;
			} else if (*state_id == job->job_state)
				match = true;
			if (match) {
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
		    !IS_JOB_STAGE_OUT(job) &&
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
		    (xstrcmp(job->resv_name, params.reservation))) {
			return 7;
		}
	}

	if (params.name_list) {
		filter = 1;
		iterator = list_iterator_create(params.name_list);
		while ((name = list_next(iterator))) {
			if ((job->name != NULL) &&
			     (xstrcasecmp(name, job->name) == 0)) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 8;
	}

	if (partial_array) {
		/* Print this record, but perhaps only some job array records */
		bitstr_t *new_array_bitmap;
		int array_len = bit_size(job->array_bitmap);
		new_array_bitmap = bit_alloc(array_len);
		iterator = list_iterator_create(params.job_list);
		while ((job_step_id = list_next(iterator))) {
			if ((job_step_id->step_id.job_id ==
			     job->array_job_id) &&
			    (job_step_id->array_id < array_len)) {
				bit_set(new_array_bitmap,job_step_id->array_id);
			}
		}
		list_iterator_destroy(iterator);
		bit_and(job->array_bitmap, new_array_bitmap);
		bit_free(new_array_bitmap);
		xfree(job->array_task_str);
		i = bit_set_count(job->array_bitmap);
		if (i == 1) {
			job->array_task_id = bit_ffs(job->array_bitmap);
			bit_free(job->array_bitmap);
		} else {
			i = i * 16 + 10;
			job->array_task_str = xmalloc(i);
			(void) bit_fmt(job->array_task_str, i,
				       job->array_bitmap);
		}
	}

	return 0;
}

/* Return 0 if supplied partition name is to be printed, otherwise return 2 */
static int _filter_job_part(char *part_name)
{
	char *token = NULL, *last = NULL, *tmp_name = NULL, *part;
	ListIterator iterator;
	int rc = 2;

	if (!params.part_list)
		return 0;

	if (part_name) {
		tmp_name = xstrdup(part_name);
		token = strtok_r(tmp_name, ",", &last);
	}
	while (token && (rc != 0)) {
		iterator = list_iterator_create(params.part_list);
		while ((part = list_next(iterator))) {
			if (xstrcmp(part, token) == 0) {
				rc = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		token = strtok_r(NULL, ",", &last);
	}
	xfree(tmp_name);

	return rc;
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
			if (((job_step_id->array_id == NO_VAL)   &&
			     ((job_step_id->step_id.job_id ==
			       step->array_job_id) ||
			      (job_step_id->step_id.job_id ==
			       step->step_id.job_id))) ||
			    ((job_step_id->array_id == step->array_task_id) &&
			     (job_step_id->step_id.job_id ==
			      step->array_job_id))) {
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
			if (xstrcmp(part, step->partition) == 0) {
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
			if (job_step_id->step_id.step_id !=
			    step->step_id.step_id)
				continue;
			if (((job_step_id->array_id == NO_VAL) &&
			     ((job_step_id->step_id.job_id ==
			       step->array_job_id) ||
			      (job_step_id->step_id.job_id ==
			       step->step_id.job_id))) ||
			    ((job_step_id->array_id == step->array_task_id) &&
			     (job_step_id->step_id.job_id ==
			      step->array_job_id))) {
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
