/*****************************************************************************\
 *  print.c - squeue print job functions
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

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/squeue/print.h"
#include "src/squeue/squeue.h"

static int _filter_job(job_info_t * job);
static int _filter_step(job_step_info_t * step);
static int _print_str(char *str, int width, bool right, bool cut_output);

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/
int print_jobs(List jobs, List format)
{
	if (list_count(jobs) > 0) {
		job_info_t *job = NULL;
		ListIterator i = list_iterator_create(jobs);

		print_job_from_format(NULL, format);
		while ((job = (job_info_t *) list_next(i)) != NULL) {
			print_job_from_format(job, format);
		}
		list_iterator_destroy(i);
	} else
		printf("No jobs found in system\n");

	return SLURM_SUCCESS;
}

int print_steps(List steps, List format)
{
	if (list_count(steps) > 0) {
		job_step_info_t *step = NULL;
		ListIterator i = list_iterator_create(steps);

		print_step_from_format(NULL, format);
		while ((step = (job_step_info_t *) list_next(i)) != NULL) {
			print_step_from_format(step, format);
		}
	} else
		printf("No job steps found in system\n");

	return SLURM_SUCCESS;
}

int print_jobs_array(job_info_t * jobs, int size, List format)
{
	if (size > 0) {
		int i = 0;
		List job_list;
		ListIterator job_iterator;
		job_info_t *job_ptr;

		job_list = list_create(NULL);
		if (!params.no_header)
			print_job_from_format(NULL, format);

		/* Filter out the jobs of interest */
		for (; i < size; i++) {
			if (_filter_job(&jobs[i]))
				continue;
			list_append(job_list, (void *) &jobs[i]);
		}

		/* Sort the jobs */
		if (params.sort == NULL) {
			params.sort = xmalloc(6);
			/* Partition, state, priority */
			strcat(params.sort, "P,t,p");
		}
		for (i = strlen(params.sort); i >= 0; i--) {
			if (params.sort[i] == ',')
				continue;
			else if (params.sort[i] == 'i')
				sort_job_by_job_id(job_list);
			else if (params.sort[i] == 'j')
				sort_job_by_job_name(job_list);
			else if (params.sort[i] == 'p')
				sort_job_by_priority(job_list);
			else if (params.sort[i] == 'P')
				sort_job_by_partition(job_list);
			else if ((params.sort[i] == 't')
				 || (params.sort[i] == 'T'))
				sort_job_by_state(job_list);
			else if ((params.sort[i] == 'u')
				 || (params.sort[i] == 'U'))
				sort_job_by_user(job_list);
		}

		/* Print the jobs of interest */
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = list_next(job_iterator))) {
			print_job_from_format(job_ptr, format);
		}
		list_iterator_destroy(job_iterator);
		list_destroy(job_list);
	} else
		printf("No jobs found in system\n");

	return SLURM_SUCCESS;
}

int print_steps_array(job_step_info_t * steps, int size, List format)
{

	if (size > 0) {
		int i = 0;
		List step_list;
		ListIterator step_iterator;
		job_step_info_t *step_ptr;

		step_list = list_create(NULL);
		if (!params.no_header)
			print_step_from_format(NULL, format);

		/* Filter out the jobs of interest */
		for (; i < size; i++) {
			if (_filter_step(&steps[i]))
				continue;
			list_append(step_list, (void *) &steps[i]);
		}

		/* Sort the job steps */
		if (params.sort == NULL) {
			params.sort = xmalloc(4);
			strcat(params.sort, "P,i");	/* Partition, id */
		}
		for (i = strlen(params.sort); i >= 0; i--) {
			if (params.sort[i] == ',')
				continue;
			else if (params.sort[i] == 'i')
				sort_step_by_job_step_id(step_list);
			else if (params.sort[i] == 'P')
				sort_step_by_partition(step_list);
			else if ((params.sort[i] == 'u')
				 || (params.sort[i] == 'U'))
				sort_step_by_user(step_list);
		}

		/* Print the steps of interest */
		step_iterator = list_iterator_create(step_list);
		while ((step_ptr = list_next(step_iterator))) {
			print_step_from_format(step_ptr, format);
		}
		list_iterator_destroy(step_iterator);
		list_destroy(step_list);
	} else
		printf("No job steps found in system\n");

	return SLURM_SUCCESS;
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
		if ((printed = printf(temp)) < 0)
			return printed;
	}

	while (printed++ < width)
		printf(" ");

	return printed;
}

int _print_nodes(char *nodes, int width, bool right, bool cut)
{
	hostlist_t hl = hostlist_create(nodes);
	char buf[1024];
	int retval;
	hostlist_ranged_string(hl, 1024, buf);
	retval = _print_str(buf, width, right, false);
	hostlist_destroy(hl);
	return retval;
}


int _print_int(int number, int width, bool right, bool cut_output)
{
	char buf[32];

	snprintf(buf, 32, "%d", number);
	return _print_str(buf, width, right, cut_output);
}


int _create_format(char *buffer, const char *type, int width, bool right)
{
	if (snprintf
	    (buffer, FORMAT_STRING_SIZE,
	     (right ? " %%-%d.%d%s" : "%%%d.%d%s "), width, width - 1,
	     type) == -1)
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

int _print_time(time_t t, int level, int width, bool right)
{
	struct tm time;
	char str[FORMAT_STRING_SIZE];

	if (t) {
		localtime_r(&t, &time);

		switch (level) {
		case 1:
		case 2:
		default:
			snprintf(str, FORMAT_STRING_SIZE,
				 "%2.2u/%2.2u-%2.2u:%2.2u",
				 (time.tm_mon + 1), time.tm_mday,
				 time.tm_hour, time.tm_min);
			break;
		}

		_print_str(str, width, right, true);
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
	int total_width = 0, inx;

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
	if (job == NULL) {
		/* one-origin for no trailing space */
		for (inx=1; inx<total_width; inx++)
			printf("-");
		printf("\n");
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

	if (list_append(list, tmp) == NULL) {
		fprintf(stderr, "Memory exhausted\n");
		exit(1);
	}
	return SLURM_SUCCESS;
}


int _print_job_job_id(job_info_t * job, int width, bool right, char* suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (job == NULL)	/* Print the Header instead */
		_print_str("JOBID", width, right, true);
	else {
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->job_id);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_partition(job_info_t * job, int width, bool right, char* suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (job == NULL)	/* Print the Header instead */
		_print_str("PARTITION", width, right, true);
	else {
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
	struct passwd *user_info = NULL;

	if (job == NULL)	/* Print the Header instead */
		_print_str("USER", width, right, true);
	else {
		user_info = getpwuid((uid_t) job->user_id);
		if (user_info && user_info->pw_name[0])
			_print_str(user_info->pw_name, width, right, true);
		else
			_print_int(job->user_id, width, right, true);
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

int _print_job_time_limit(job_info_t * job, int width, bool right, 
			  char* suffix)
{
	char time[FORMAT_STRING_SIZE];
	if (job == NULL)	/* Print the Header instead */
		_print_str("TIME_LIMIT", width, right, true);
	else if (job->time_limit == INFINITE)
		_print_str("UNLIMITED", width, right, true);
	else if (job->time_limit == NO_VAL)
		_print_str("NOT_SET", width, right, true);
	else {
		/* format is "hours:minutes" */
		snprintf(time, FORMAT_STRING_SIZE, "%d:%2.2d",
			 job->time_limit / 60,
			 job->time_limit % 60);

		_print_str(time, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_start_time(job_info_t * job, int width, bool right, 
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

int _print_job_end_time(job_info_t * job, int width, bool right, char* suffix)
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
		float prio = (float) job->priority / 
		             (float) ((uint32_t) 0xffffffff);
		sprintf(temp, "%f", prio);
		_print_str(temp, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_nodes(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("NODES", width, right, false);
	else
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
			curr_width +=
			    _print_int(*current, width, right, true);
			printf(",");
		}
		while (curr_width < width)
			curr_width += printf(" ");
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_num_procs(job_info_t * job, int width, bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("NUM_PROCS", width, right, true);
	else
		_print_int(job->num_procs, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_num_nodes(job_info_t * job, int width, bool right_justify, 
			 char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("NUM_NODES", width, right_justify, true);
	else
		_print_int(job->num_nodes, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_shared(job_info_t * job, int width, bool right_justify, 
		      char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("SHARED", width, right_justify, true);
	else
		_print_int(job->shared, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_contiguous(job_info_t * job, int width, bool right_justify, 
			  char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("CONTIGUOUS", width, right_justify, true);
	else
		_print_int(job->contiguous, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_min_procs(job_info_t * job, int width, bool right_justify, 
			 char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("MIN_PROCS", width, right_justify, true);
	else
		_print_int(job->min_procs, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_min_memory(job_info_t * job, int width, bool right_justify, 
			  char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("MIN_MEMORY", width, right_justify, true);
	else
		_print_int(job->min_memory, width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int
_print_job_min_tmp_disk(job_info_t * job, int width, bool right_justify, 
			char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("MIN_TMP_DISK", width, right_justify, true);
	else
		_print_int(job->min_tmp_disk, width, right_justify, true);
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


/*****************************************************************************
 * Job Step  Print Functions
 *****************************************************************************/
int print_step_from_format(job_step_info_t * job_step, List list)
{
	ListIterator i = list_iterator_create(list);
	step_format_t *current;
	int total_width = 0, inx;

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
	if (job_step == NULL) {
		/* one-origin for no trailing space */
		for (inx=1; inx<total_width; inx++)
			printf("-");
		printf("\n");
	}
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

	if (step == NULL)	/* Print the Header instead */
		_print_str("STEPID", width, right, true);
	else {
		snprintf(id, FORMAT_STRING_SIZE, "%u.%u", step->job_id,
			 step->step_id);
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
	struct passwd *user_info = NULL;

	if (step == NULL)	/* Print the Header instead */
		_print_str("USER", width, right, true);
	else {
		user_info = getpwuid((uid_t) step->user_id);
		if (user_info && user_info->pw_name[0])
			_print_str(user_info->pw_name, width, right, true);
		else
			_print_int(step->user_id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_start_time(job_step_info_t * step, int width, bool right, 
			   char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("START_TIME", width, right, true);
	else
		_print_time(step->start_time, 0, width, right);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_step_nodes(job_step_info_t * step, int width, bool right, 
		      char* suffix)
{
	if (step == NULL)	/* Print the Header instead */
		_print_str("NODES", width, right, false);
	else
		_print_nodes(step->nodes, width, right, false);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

/* filter job records per input specifications, 
 * returns 1 if job should be filter out (not printed) */
static int _filter_job(job_info_t * job)
{
	int filter;
	ListIterator iterator;
	uint32_t *job_id, *user;
	enum job_states *state_id;
	char *part;

	if (params.job_list) {
		filter = 1;
		iterator = list_iterator_create(params.job_list);
		while ((job_id = list_next(iterator))) {
			if (*job_id == job->job_id) {
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
			if (strcmp(part, job->partition) == 0) {
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
			if (*state_id == job->job_state) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 3;
	} else {
		if ((job->job_state != JOB_PENDING) &&
		    (job->job_state != JOB_RUNNING))
			return 4;
	}

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
			return 5;
	}

	return 0;
}

/* filter step records per input specifications, 
 * returns 1 if step should be filter out (not printed) */
static int _filter_step(job_step_info_t * step)
{
	int filter;
	ListIterator iterator;
	uint32_t *job_id, *user;
	char *part;
	squeue_job_step_t *job_step_id;

	if (params.job_list) {
		filter = 1;
		iterator = list_iterator_create(params.job_list);
		while ((job_id = list_next(iterator))) {
			if (*job_id == step->job_id) {
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
			if ((job_step_id->job_id == step->job_id) &&
			    (job_step_id->step_id == step->step_id)) {
				filter = 0;
				break;
			}
		}
		list_iterator_destroy(iterator);
		if (filter == 1)
			return 3;
	}

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
			return 5;
	}

	return 0;
}

/*****************************************************************************
 * Job Sort Functions
 *****************************************************************************/
/* sort lower to higher */
int _sort_job_by_id(void *void1, void *void2)
{
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	return job1->job_id - job2->job_id;
}

/* sort lower to higher */
int _sort_job_by_name(void *void1, void *void2)
{
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	return strcmp(job1->name, job2->name);
}

/* sort higher to lower */
int _sort_job_by_priority(void *void1, void *void2)
{
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	return (job2->priority - job1->priority);
}

int _sort_job_by_partition(void *void1, void *void2)
{
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	return strcmp(job1->partition, job2->partition);
}

int _sort_job_by_state(void *void1, void *void2)
{
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	return (job1->job_state - job2->job_state);
}

/* sort lower to higher */
int _sort_job_by_user(void *void1, void *void2)
{
	job_info_t *job1 = (job_info_t *) void1;
	job_info_t *job2 = (job_info_t *) void2;

	return (job1->user_id - job2->user_id);
}

/*****************************************************************************
 * Step Sort Functions
 *****************************************************************************/
/* sort lower to higher */
int _sort_step_by_id(void *void1, void *void2)
{
	int i;
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	i = step1->job_id - step2->job_id;
	if (i == 0)
		return (step1->step_id - step2->step_id);
	else
		return i;
}

int _sort_step_by_partition(void *void1, void *void2)
{
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	return strcmp(step1->partition, step2->partition);
}

/* sort lower to higher */
int _sort_step_by_user(void *void1, void *void2)
{
	job_step_info_t *step1 = (job_step_info_t *) void1;
	job_step_info_t *step2 = (job_step_info_t *) void2;

	return (step1->user_id - step2->user_id);
}
