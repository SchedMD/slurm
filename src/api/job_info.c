/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/forward.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

static pthread_mutex_t job_node_info_lock = PTHREAD_MUTEX_INITIALIZER;
static node_info_msg_t *job_node_ptr = NULL;

/* This set of functions loads/free node information so that we can map a job's
 * core bitmap to it's CPU IDs based upon the thread count on each node. */
static void _load_node_info(void)
{
	slurm_mutex_lock(&job_node_info_lock);
	if (!job_node_ptr)
		(void) slurm_load_node((time_t) NULL, &job_node_ptr, 0);
	slurm_mutex_unlock(&job_node_info_lock);
}
static uint32_t _threads_per_core(char *host)
{
	uint32_t i, threads = 1;

	if (!job_node_ptr || !host)
		return threads;

	slurm_mutex_lock(&job_node_info_lock);
	for (i = 0; i < job_node_ptr->record_count; i++) {
		if (job_node_ptr->node_array[i].name &&
		    !strcmp(host, job_node_ptr->node_array[i].name)) {
			threads = job_node_ptr->node_array[i].threads;
			break;
		}
	}
	slurm_mutex_unlock(&job_node_info_lock);
	return threads;
}
static void _free_node_info(void)
{
#if 0
	slurm_mutex_lock(&job_node_info_lock);
	if (job_node_ptr) {
		slurm_free_node_info_msg(job_node_ptr);
		job_node_ptr = NULL;
	}
	slurm_mutex_unlock(&job_node_info_lock);
#endif
}

/* Perform file name substitutions
 * %A - Job array's master job allocation number.
 * %a - Job array ID (index) number.
 * %j - Job ID
 * %u - User name
 */
static void _fname_format(char *buf, int buf_size, job_info_t * job_ptr,
			  char *fname)
{
	char *ptr, *tmp, *tmp2 = NULL, *user;

	tmp = xstrdup(fname);
	while ((ptr = strstr(tmp, "%A"))) {	/* Array job ID */
		ptr[0] = '\0';
		if (job_ptr->array_task_id == NO_VAL) {
			/* Not a job array */
			xstrfmtcat(tmp2, "%s%u%s", tmp, job_ptr->job_id, ptr+2);
		} else {
			xstrfmtcat(tmp2, "%s%u%s", tmp, job_ptr->array_job_id,
				   ptr+2);
		}
		xfree(tmp);	/* transfer the results */
		tmp = tmp2;
		tmp2 = NULL;
	}
	while ((ptr = strstr(tmp, "%a"))) {	/* Array task ID */
		ptr[0] = '\0';
		xstrfmtcat(tmp2, "%s%u%s", tmp, job_ptr->array_task_id, ptr+2);
		xfree(tmp);	/* transfer the results */
		tmp = tmp2;
		tmp2 = NULL;
	}
	while ((ptr = strstr(tmp, "%j"))) {	/* Job ID */
		ptr[0] = '\0';
		xstrfmtcat(tmp2, "%s%u%s", tmp, job_ptr->job_id, ptr+2);
		xfree(tmp);	/* transfer the results */
		tmp = tmp2;
		tmp2 = NULL;
	}
	while ((ptr = strstr(tmp, "%u"))) {	/* User name */
		ptr[0] = '\0';
		user = uid_to_string((uid_t) job_ptr->user_id);
		xstrfmtcat(tmp2, "%s%s%s", tmp, user, ptr+2);
		xfree(user);
		xfree(tmp);	/* transfer the results */
		tmp = tmp2;
		tmp2 = NULL;
	}
	if (tmp[0] == '/')
		snprintf(buf, buf_size, "%s", tmp);
	else
		snprintf(buf, buf_size, "%s/%s", job_ptr->work_dir, tmp);
	xfree(tmp);
}

/* Given a job record pointer, return its stderr path in buf */
extern void slurm_get_job_stderr(char *buf, int buf_size, job_info_t * job_ptr)
{
	if (job_ptr == NULL)
		snprintf(buf, buf_size, "%s", "job pointer is NULL");
	else if (job_ptr->std_err)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_err);
	else if (job_ptr->batch_flag == 0)
		snprintf(buf, buf_size, "%s", "");
	else if (job_ptr->std_out)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_out);
	else if (job_ptr->array_job_id) {
		snprintf(buf, buf_size, "%s/slurm-%u_%u.out",
			 job_ptr->work_dir,
			 job_ptr->array_job_id, job_ptr->array_task_id);
	} else {
		snprintf(buf, buf_size, "%s/slurm-%u.out",
			 job_ptr->work_dir, job_ptr->job_id);
	}
}

/* Given a job record pointer, return its stdin path in buf */
extern void slurm_get_job_stdin(char *buf, int buf_size, job_info_t * job_ptr)
{
	if (job_ptr == NULL)
		snprintf(buf, buf_size, "%s", "job pointer is NULL");
	else if (job_ptr->std_in)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_in);
	else if (job_ptr->batch_flag == 0)
		snprintf(buf, buf_size, "%s", "");
	else
		snprintf(buf, buf_size, "%s", "StdIn=/dev/null");
}

/* Given a job record pointer, return its stdout path in buf */
extern void slurm_get_job_stdout(char *buf, int buf_size, job_info_t * job_ptr)
{
	if (job_ptr == NULL)
		snprintf(buf, buf_size, "%s", "job pointer is NULL");
	else if (job_ptr->std_out)
		_fname_format(buf, buf_size, job_ptr, job_ptr->std_out);
	else if (job_ptr->batch_flag == 0)
		snprintf(buf, buf_size, "%s", "");
	else if (job_ptr->array_job_id) {
		snprintf(buf, buf_size, "%s/slurm-%u_%u.out",
			 job_ptr->work_dir,
			 job_ptr->array_job_id, job_ptr->array_task_id);
	} else {
		snprintf(buf, buf_size, "%s/slurm-%u.out",
			 job_ptr->work_dir, job_ptr->job_id);
	}
}

/*
 * slurm_xlate_job_id - Translate a Slurm job ID string into a slurm job ID
 *	number. If this job ID contains an array index, map this to the
 *	equivalent Slurm job ID number (e.g. "123_2" to 124)
 *
 * IN job_id_str - String containing a single job ID number
 * RET - equivalent job ID number or 0 on error
 */
extern uint32_t slurm_xlate_job_id(char *job_id_str)
{
	char *next_str;
	uint32_t i, job_id;
	uint16_t array_id;
	job_info_msg_t *resp = NULL;
	slurm_job_info_t *job_ptr;

	job_id = (uint32_t) strtol(job_id_str, &next_str, 10);
	if (next_str[0] == '\0')
		return job_id;
	if (next_str[0] != '_')
		return (uint32_t) 0;
	array_id = (uint16_t) strtol(next_str + 1, &next_str, 10);
	if (next_str[0] != '\0')
		return (uint32_t) 0;
	if ((slurm_load_job(&resp, job_id, SHOW_ALL) != 0) || (resp == NULL))
		return (uint32_t) 0;
	job_id = 0;
	for (i = 0, job_ptr = resp->job_array; i < resp->record_count;
	     i++, job_ptr++) {
		if (job_ptr->array_task_id == array_id) {
			job_id = job_ptr->job_id;
			break;
		}
	}
	slurm_free_job_info_msg(resp);
	return job_id;
}

/*
 * slurm_print_job_info_msg - output information about all Slurm
 *	jobs based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_info_msg_ptr - job information message pointer
 * IN one_liner - print as a single line if true
 */
extern void
slurm_print_job_info_msg ( FILE* out, job_info_msg_t *jinfo, int one_liner )
{
	int i;
	job_info_t *job_ptr = jinfo->job_array;
	char time_str[32];

	slurm_make_time_str ((time_t *)&jinfo->last_update, time_str,
		sizeof(time_str));
	fprintf( out, "Job data as of %s, record count %d\n",
		 time_str, jinfo->record_count);

	for (i = 0; i < jinfo->record_count; i++)
		slurm_print_job_info(out, &job_ptr[i], one_liner);
}

static void _sprint_range(char *str, uint32_t str_size,
			  uint32_t lower, uint32_t upper)
{
	char tmp[128];
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (cluster_flags & CLUSTER_FLAG_BG) {
		convert_num_unit((float)lower, tmp, sizeof(tmp), UNIT_NONE);
	} else {
		snprintf(tmp, sizeof(tmp), "%u", lower);
	}
	if (upper > 0) {
    		char tmp2[128];
		if (cluster_flags & CLUSTER_FLAG_BG) {
			convert_num_unit((float)upper, tmp2,
					 sizeof(tmp2), UNIT_NONE);
		} else {
			snprintf(tmp2, sizeof(tmp2), "%u", upper);
		}
		snprintf(str, str_size, "%s-%s", tmp, tmp2);
	} else
		snprintf(str, str_size, "%s", tmp);

}

/*
 * slurm_print_job_info - output information about a specific Slurm
 *	job based upon message as loaded using slurm_load_jobs
 * IN out - file to write to
 * IN job_ptr - an individual job information record pointer
 * IN one_liner - print as a single line if true
 */
extern void
slurm_print_job_info ( FILE* out, job_info_t * job_ptr, int one_liner )
{
	char *print_this;

	_load_node_info();
	print_this = slurm_sprint_job_info(job_ptr, one_liner);
	fprintf(out, "%s", print_this);
	xfree(print_this);
	_free_node_info();
}

/*
 * slurm_sprint_job_info - output information about a specific Slurm
 *	job based upon message as loaded using slurm_load_jobs
 * IN job_ptr - an individual job information record pointer
 * IN one_liner - print as a single line if true
 * RET out - char * containing formatted output (must be freed after call)
 *           NULL is returned on failure.
 */
extern char *
slurm_sprint_job_info ( job_info_t * job_ptr, int one_liner )
{
	int i, j, k;
	char time_str[32], *group_name, *user_name;
	char tmp1[128], tmp2[128], tmp3[128], tmp4[128], tmp5[128], tmp6[128];
	char *tmp6_ptr;
	char tmp_line[1024];
	char *ionodes = NULL;
	uint16_t exit_status = 0, term_sig = 0;
	job_resources_t *job_resrcs = job_ptr->job_resrcs;
	char *out = NULL;
	time_t run_time;
	uint32_t min_nodes, max_nodes = 0;
	char *nodelist = "NodeList";
	bitstr_t *cpu_bitmap;
	char *host;
	int sock_inx, sock_reps, last;
	int abs_node_inx, rel_node_inx;
	int nice;
	int bit_inx, bit_reps;
	uint32_t *last_mem_alloc_ptr = NULL;
	uint32_t last_mem_alloc = NO_VAL;
	char *last_hosts;
	hostlist_t hl, hl_last;
	char select_buf[122];
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	uint32_t threads;

	if (cluster_flags & CLUSTER_FLAG_BG) {
		nodelist = "MidplaneList";
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_IONODES,
					    &ionodes);
	}

	/****** Line 1 ******/
	snprintf(tmp_line, sizeof(tmp_line), "JobId=%u ", job_ptr->job_id);
	out = xstrdup(tmp_line);
	if (job_ptr->array_job_id) {
		if (job_ptr->array_task_str) {
			snprintf(tmp_line, sizeof(tmp_line),
				 "ArrayJobId=%u ArrayTaskId=%s ",
				 job_ptr->array_job_id,
				 job_ptr->array_task_str);
		} else {
			snprintf(tmp_line, sizeof(tmp_line),
				 "ArrayJobId=%u ArrayTaskId=%u ",
				 job_ptr->array_job_id,
				 job_ptr->array_task_id);
		}
		xstrcat(out, tmp_line);
	}
	snprintf(tmp_line, sizeof(tmp_line), "JobName=%s", job_ptr->name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 2 ******/
	user_name = uid_to_string((uid_t) job_ptr->user_id);
	group_name = gid_to_string((gid_t) job_ptr->group_id);
	snprintf(tmp_line, sizeof(tmp_line),
		 "UserId=%s(%u) GroupId=%s(%u)",
		 user_name, job_ptr->user_id, group_name, job_ptr->group_id);
	xfree(user_name);
	xfree(group_name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 3 ******/
	nice  = job_ptr->nice;
	nice -= NICE_OFFSET;
	snprintf(tmp_line, sizeof(tmp_line),
		 "Priority=%u Nice=%d Account=%s QOS=%s",
		 job_ptr->priority, nice, job_ptr->account, job_ptr->qos);
	xstrcat(out, tmp_line);
	if (slurm_get_track_wckey()) {
		snprintf(tmp_line, sizeof(tmp_line),
			 " WCKey=%s", job_ptr->wckey);
		xstrcat(out, tmp_line);
	}
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 4 ******/
	if (job_ptr->state_desc) {
		/* Replace white space with underscore for easier parsing */
		for (j=0; job_ptr->state_desc[j]; j++) {
			if (isspace((int)job_ptr->state_desc[j]))
				job_ptr->state_desc[j] = '_';
		}
		tmp6_ptr = job_ptr->state_desc;
	} else
		tmp6_ptr = job_reason_string(job_ptr->state_reason);
	snprintf(tmp_line, sizeof(tmp_line),
		 "JobState=%s Reason=%s Dependency=%s",
		 job_state_string(job_ptr->job_state), tmp6_ptr,
		 job_ptr->dependency);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 5 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Requeue=%u Restarts=%u BatchFlag=%u Reboot=%u ",
		 job_ptr->requeue, job_ptr->restart_cnt, job_ptr->batch_flag,
		 job_ptr->reboot);
	xstrcat(out, tmp_line);
	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	exit_status = WEXITSTATUS(job_ptr->exit_code);
	snprintf(tmp_line, sizeof(tmp_line),
		 "ExitCode=%u:%u", exit_status, term_sig);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 5a (optional) ******/
	if (!(job_ptr->show_flags & SHOW_DETAIL))
		goto line6;
	if (WIFSIGNALED(job_ptr->derived_ec))
		term_sig = WTERMSIG(job_ptr->derived_ec);
	else
		term_sig = 0;
	exit_status = WEXITSTATUS(job_ptr->derived_ec);
	snprintf(tmp_line, sizeof(tmp_line),
		 "DerivedExitCode=%u:%u", exit_status, term_sig);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 6 ******/
line6:
	snprintf(tmp_line, sizeof(tmp_line), "RunTime=");
	xstrcat(out, tmp_line);
	if (IS_JOB_PENDING(job_ptr))
		run_time = 0;
	else if (IS_JOB_SUSPENDED(job_ptr))
		run_time = job_ptr->pre_sus_time;
	else {
		time_t end_time;
		if (IS_JOB_RUNNING(job_ptr) || (job_ptr->end_time == 0))
			end_time = time(NULL);
		else
			end_time = job_ptr->end_time;
		if (job_ptr->suspend_time) {
			run_time = (time_t)
				(difftime(end_time, job_ptr->suspend_time)
				 + job_ptr->pre_sus_time);
		} else
			run_time = (time_t)
				difftime(end_time, job_ptr->start_time);
	}
	secs2time_str(run_time, tmp1, sizeof(tmp1));
	sprintf(tmp_line, "%s ", tmp1);
	xstrcat(out, tmp_line);

	snprintf(tmp_line, sizeof(tmp_line), "TimeLimit=");
	xstrcat(out, tmp_line);
	if (job_ptr->time_limit == NO_VAL)
		sprintf(tmp_line, "Partition_Limit");
	else {
		mins2time_str(job_ptr->time_limit, tmp_line,
			      sizeof(tmp_line));
	}
	xstrcat(out, tmp_line);
	snprintf(tmp_line, sizeof(tmp_line), " TimeMin=");
	xstrcat(out, tmp_line);
	if (job_ptr->time_min == 0)
		sprintf(tmp_line, "N/A");
	else {
		mins2time_str(job_ptr->time_min, tmp_line,
			      sizeof(tmp_line));
	}
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 7 ******/
	slurm_make_time_str((time_t *)&job_ptr->submit_time, time_str,
			    sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "SubmitTime=%s ", time_str);
	xstrcat(out, tmp_line);

	slurm_make_time_str((time_t *)&job_ptr->eligible_time, time_str,
			    sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "EligibleTime=%s", time_str);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 8 (optional) ******/
	if (job_ptr->resize_time) {
		slurm_make_time_str((time_t *)&job_ptr->resize_time, time_str,
				    sizeof(time_str));
		snprintf(tmp_line, sizeof(tmp_line), "ResizeTime=%s", time_str);
		xstrcat(out, tmp_line);
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}

	/****** Line 9 ******/
	slurm_make_time_str((time_t *)&job_ptr->start_time, time_str,
			    sizeof(time_str));
	snprintf(tmp_line, sizeof(tmp_line), "StartTime=%s ", time_str);
	xstrcat(out, tmp_line);

	snprintf(tmp_line, sizeof(tmp_line), "EndTime=");
	xstrcat(out, tmp_line);
	if ((job_ptr->time_limit == INFINITE) &&
	    (job_ptr->end_time > time(NULL)))
		sprintf(tmp_line, "Unknown");
	else {
		slurm_make_time_str ((time_t *)&job_ptr->end_time, time_str,
				     sizeof(time_str));
		sprintf(tmp_line, "%s", time_str);
	}
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 10 ******/
	if (job_ptr->preempt_time == 0)
		sprintf(tmp_line, "PreemptTime=None ");
	else {
		slurm_make_time_str((time_t *)&job_ptr->preempt_time,
				    time_str, sizeof(time_str));
		snprintf(tmp_line, sizeof(tmp_line), "PreemptTime=%s ",
			 time_str);
	}
	xstrcat(out, tmp_line);
	if (job_ptr->suspend_time) {
		slurm_make_time_str ((time_t *)&job_ptr->suspend_time,
				     time_str, sizeof(time_str));
	} else {
		strncpy(time_str, "None", sizeof(time_str));
	}
	snprintf(tmp_line, sizeof(tmp_line),
		 "SuspendTime=%s SecsPreSuspend=%ld",
		 time_str, (long int)job_ptr->pre_sus_time);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 11 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Partition=%s AllocNode:Sid=%s:%u",
		 job_ptr->partition, job_ptr->alloc_node, job_ptr->alloc_sid);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 12 ******/
	snprintf(tmp_line, sizeof(tmp_line), "Req%s=%s Exc%s=%s",
		 nodelist, job_ptr->req_nodes, nodelist, job_ptr->exc_nodes);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 13 ******/
	xstrfmtcat(out, "%s=", nodelist);
	xstrcat(out, job_ptr->nodes);
	if (job_ptr->nodes && ionodes) {
		snprintf(tmp_line, sizeof(tmp_line), "[%s]", ionodes);
		xstrcat(out, tmp_line);
		xfree(ionodes);
	}
	if (job_ptr->sched_nodes)
		xstrfmtcat(out, " Sched%s=%s", nodelist, job_ptr->sched_nodes);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 14 (optional) ******/
	if (job_ptr->batch_host) {
		snprintf(tmp_line, sizeof(tmp_line), "BatchHost=%s",
			 job_ptr->batch_host);
		xstrcat(out, tmp_line);
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
	}

	/****** Line 15 ******/
	if (cluster_flags & CLUSTER_FLAG_BG) {
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &min_nodes);
		if ((min_nodes == 0) || (min_nodes == NO_VAL)) {
			min_nodes = job_ptr->num_nodes;
			max_nodes = job_ptr->max_nodes;
		} else if (job_ptr->max_nodes)
			max_nodes = min_nodes;
	} else if (IS_JOB_PENDING(job_ptr)) {
		min_nodes = job_ptr->num_nodes;
		if ((min_nodes == 1) && (job_ptr->num_cpus > 1)
		    && job_ptr->ntasks_per_node
		    && (job_ptr->ntasks_per_node != (uint16_t) NO_VAL)) {
			int node_cnt2 = job_ptr->num_cpus;
			node_cnt2 = (node_cnt2 + job_ptr->ntasks_per_node - 1)
				    / job_ptr->ntasks_per_node;
			if (min_nodes < node_cnt2)
				min_nodes = node_cnt2;
		}
		max_nodes = job_ptr->max_nodes;
		if (max_nodes && (max_nodes < min_nodes))
			min_nodes = max_nodes;
	} else {
		min_nodes = job_ptr->num_nodes;
		max_nodes = 0;
	}

	_sprint_range(tmp1, sizeof(tmp1), job_ptr->num_cpus, job_ptr->max_cpus);
	_sprint_range(tmp2, sizeof(tmp2), min_nodes, max_nodes);
	if (job_ptr->boards_per_node == (uint16_t) NO_VAL)
		strcpy(tmp3, "*");
	else
		snprintf(tmp3, sizeof(tmp3), "%u", job_ptr->boards_per_node);
	if (job_ptr->sockets_per_board == (uint16_t) NO_VAL)
		strcpy(tmp4, "*");
	else
		snprintf(tmp4, sizeof(tmp4), "%u", job_ptr->sockets_per_board);
	if (job_ptr->cores_per_socket == (uint16_t) NO_VAL)
		strcpy(tmp5, "*");
	else
		snprintf(tmp5, sizeof(tmp5), "%u", job_ptr->cores_per_socket);
	if (job_ptr->threads_per_core == (uint16_t) NO_VAL)
		strcpy(tmp6, "*");
	else
		snprintf(tmp6, sizeof(tmp6), "%u", job_ptr->threads_per_core);
	snprintf(tmp_line, sizeof(tmp_line),
		 "NumNodes=%s NumCPUs=%s CPUs/Task=%u ReqB:S:C:T=%s:%s:%s:%s",
		 tmp2, tmp1, job_ptr->cpus_per_task, tmp3, tmp4, tmp5, tmp6);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 16 ******/
	if (job_ptr->sockets_per_node == (uint16_t) NO_VAL)
		strcpy(tmp1, "*");
	else
		snprintf(tmp1, sizeof(tmp1), "%u", job_ptr->sockets_per_node);
	if (job_ptr->ntasks_per_node == (uint16_t) NO_VAL)
		strcpy(tmp2, "*");
	else
		snprintf(tmp2, sizeof(tmp2), "%u", job_ptr->ntasks_per_node);
	if (job_ptr->ntasks_per_board == (uint16_t) NO_VAL)
		strcpy(tmp3, "*");
	else
		snprintf(tmp3, sizeof(tmp3), "%u", job_ptr->ntasks_per_board);
	if ((job_ptr->ntasks_per_socket == (uint16_t) NO_VAL) ||
	    (job_ptr->ntasks_per_socket == (uint16_t) INFINITE))
		strcpy(tmp4, "*");
	else
		snprintf(tmp4, sizeof(tmp4), "%u", job_ptr->ntasks_per_socket);
	if ((job_ptr->ntasks_per_core == (uint16_t) NO_VAL) ||
	    (job_ptr->ntasks_per_core == (uint16_t) INFINITE))
		strcpy(tmp5, "*");
	else
		snprintf(tmp5, sizeof(tmp5), "%u", job_ptr->ntasks_per_core);
	if (job_ptr->core_spec == (uint16_t) NO_VAL)
		strcpy(tmp6, "*");
	else
		snprintf(tmp6, sizeof(tmp6), "%u", job_ptr->core_spec);
	snprintf(tmp_line, sizeof(tmp_line),
		 "Socks/Node=%s NtasksPerN:B:S:C=%s:%s:%s:%s CoreSpec=%s",
		 tmp1, tmp2, tmp3, tmp4, tmp5, tmp6);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	if (!job_resrcs)
		goto line15;

	if (cluster_flags & CLUSTER_FLAG_BG) {
		if ((job_resrcs->cpu_array_cnt > 0) &&
		    (job_resrcs->cpu_array_value) &&
		    (job_resrcs->cpu_array_reps)) {
			int length = 0;
			xstrcat(out, "CPUs=");
			length += 10;
			for (i = 0; i < job_resrcs->cpu_array_cnt; i++) {
				if (length > 70) {
					/* skip to last CPU group entry */
					if (i < job_resrcs->cpu_array_cnt - 1) {
						continue;
					}
					/* add ellipsis before last entry */
					xstrcat(out, "...,");
					length += 4;
				}

				snprintf(tmp_line, sizeof(tmp_line), "%d",
					 job_resrcs->cpus[i]);
				xstrcat(out, tmp_line);
				length += strlen(tmp_line);
				if (job_resrcs->cpu_array_reps[i] > 1) {
					snprintf(tmp_line, sizeof(tmp_line),
						 "*%d",
						 job_resrcs->cpu_array_reps[i]);
					xstrcat(out, tmp_line);
					length += strlen(tmp_line);
				}
				if (i < job_resrcs->cpu_array_cnt - 1) {
					xstrcat(out, ",");
					length++;
				}
			}
			if (one_liner)
				xstrcat(out, " ");
			else
				xstrcat(out, "\n   ");
		}
	} else {
		if (!job_resrcs->core_bitmap)
			goto line15;

		last  = bit_fls(job_resrcs->core_bitmap);
		if (last == -1)
			goto line15;

		hl = hostlist_create(job_resrcs->nodes);
		if (!hl) {
			error("slurm_sprint_job_info: hostlist_create: %s",
			      job_resrcs->nodes);
			return NULL;
		}
		hl_last = hostlist_create(NULL);
		if (!hl_last) {
			error("slurm_sprint_job_info: hostlist_create: NULL");
			hostlist_destroy(hl);
			return NULL;
		}

		bit_inx = 0;
		i = sock_inx = sock_reps = 0;
		abs_node_inx = job_ptr->node_inx[i];

/*	tmp1[] stores the current cpu(s) allocated	*/
		tmp2[0] = '\0';	/* stores last cpu(s) allocated */
		for (rel_node_inx=0; rel_node_inx < job_resrcs->nhosts;
		     rel_node_inx++) {

			if (sock_reps >=
			    job_resrcs->sock_core_rep_count[sock_inx]) {
				sock_inx++;
				sock_reps = 0;
			}
			sock_reps++;

			bit_reps = job_resrcs->sockets_per_node[sock_inx] *
				   job_resrcs->cores_per_socket[sock_inx];
			host = hostlist_shift(hl);
			threads = _threads_per_core(host);
			cpu_bitmap = bit_alloc(bit_reps * threads);
			for (j = 0; j < bit_reps; j++) {
				if (bit_test(job_resrcs->core_bitmap, bit_inx)){
					for (k = 0; k < threads; k++)
						bit_set(cpu_bitmap,
							(j * threads) + k);
				}
				bit_inx++;
			}
			bit_fmt(tmp1, sizeof(tmp1), cpu_bitmap);
			FREE_NULL_BITMAP(cpu_bitmap);
/*
 *		If the allocation values for this host are not the same as the
 *		last host, print the report of the last group of hosts that had
 *		identical allocation values.
 */
			if (strcmp(tmp1, tmp2) ||
			    (last_mem_alloc_ptr != job_resrcs->memory_allocated) ||
			    (job_resrcs->memory_allocated &&
			     (last_mem_alloc !=
			      job_resrcs->memory_allocated[rel_node_inx]))) {
				if (hostlist_count(hl_last)) {
					last_hosts =
						hostlist_ranged_string_xmalloc(
						hl_last);
					snprintf(tmp_line, sizeof(tmp_line),
						 "  Nodes=%s CPU_IDs=%s Mem=%u",
						 last_hosts, tmp2,
						 last_mem_alloc_ptr ?
						 last_mem_alloc : 0);
					xfree(last_hosts);
					xstrcat(out, tmp_line);
					if (one_liner)
						xstrcat(out, " ");
					else
						xstrcat(out, "\n   ");

					hostlist_destroy(hl_last);
					hl_last = hostlist_create(NULL);
				}
				strcpy(tmp2, tmp1);
				last_mem_alloc_ptr = job_resrcs->memory_allocated;
				if (last_mem_alloc_ptr)
					last_mem_alloc = job_resrcs->
						memory_allocated[rel_node_inx];
				else
					last_mem_alloc = NO_VAL;
			}
			hostlist_push_host(hl_last, host);
			free(host);

			if (bit_inx > last)
				break;

			if (abs_node_inx > job_ptr->node_inx[i+1]) {
				i += 2;
				abs_node_inx = job_ptr->node_inx[i];
			} else {
				abs_node_inx++;
			}
		}

		if (hostlist_count(hl_last)) {
			last_hosts = hostlist_ranged_string_xmalloc(hl_last);
			snprintf(tmp_line, sizeof(tmp_line),
				 "  Nodes=%s CPU_IDs=%s Mem=%u",
				 last_hosts, tmp2,
				 last_mem_alloc_ptr ? last_mem_alloc : 0);
			xfree(last_hosts);
			xstrcat(out, tmp_line);
			if (one_liner)
				xstrcat(out, " ");
			else
				xstrcat(out, "\n   ");
		}
		hostlist_destroy(hl);
		hostlist_destroy(hl_last);
	}
	/****** Line 17 ******/
line15:
	if (job_ptr->pn_min_memory & MEM_PER_CPU) {
		job_ptr->pn_min_memory &= (~MEM_PER_CPU);
		tmp6_ptr = "CPU";
	} else
		tmp6_ptr = "Node";

	if (cluster_flags & CLUSTER_FLAG_BG) {
		convert_num_unit((float)job_ptr->pn_min_cpus,
				 tmp1, sizeof(tmp1), UNIT_NONE);
		snprintf(tmp_line, sizeof(tmp_line), "MinCPUsNode=%s",	tmp1);
	} else {
		snprintf(tmp_line, sizeof(tmp_line), "MinCPUsNode=%u",
			 job_ptr->pn_min_cpus);
	}

	xstrcat(out, tmp_line);
	convert_num_unit((float)job_ptr->pn_min_memory, tmp1, sizeof(tmp1),
			 UNIT_MEGA);
	convert_num_unit((float)job_ptr->pn_min_tmp_disk, tmp2, sizeof(tmp2),
			 UNIT_MEGA);
	snprintf(tmp_line, sizeof(tmp_line),
		 " MinMemory%s=%s MinTmpDiskNode=%s",
		 tmp6_ptr, tmp1, tmp2);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 18 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Features=%s Gres=%s Reservation=%s",
		 job_ptr->features, job_ptr->gres, job_ptr->resv_name);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 19 ******/
	snprintf(tmp_line, sizeof(tmp_line),
		 "Shared=%s Contiguous=%d Licenses=%s Network=%s",
		 (job_ptr->shared == 0 ? "0" :
		  job_ptr->shared == 1 ? "1" : "OK"),
		 job_ptr->contiguous, job_ptr->licenses, job_ptr->network);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 20 ******/
	snprintf(tmp_line, sizeof(tmp_line), "Command=%s",
		 job_ptr->command);
	xstrcat(out, tmp_line);
	if (one_liner)
		xstrcat(out, " ");
	else
		xstrcat(out, "\n   ");

	/****** Line 21 ******/
	snprintf(tmp_line, sizeof(tmp_line), "WorkDir=%s",
		 job_ptr->work_dir);
	xstrcat(out, tmp_line);

	if (cluster_flags & CLUSTER_FLAG_BG) {
		/****** Line 22 (optional) ******/
		select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
					       select_buf, sizeof(select_buf),
					       SELECT_PRINT_BG_ID);
		if (select_buf[0] != '\0') {
			if (one_liner)
				xstrcat(out, " ");
			else
				xstrcat(out, "\n   ");
			snprintf(tmp_line, sizeof(tmp_line),
				 "Block_ID=%s", select_buf);
			xstrcat(out, tmp_line);
		}

		/****** Line 23 (optional) ******/
		select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
					       select_buf, sizeof(select_buf),
					       SELECT_PRINT_MIXED_SHORT);
		if (select_buf[0] != '\0') {
			if (one_liner)
				xstrcat(out, " ");
			else
				xstrcat(out, "\n   ");
			xstrcat(out, select_buf);
		}

		if (cluster_flags & CLUSTER_FLAG_BGL) {
			/****** Line 24 (optional) ******/
			select_g_select_jobinfo_sprint(
				job_ptr->select_jobinfo,
				select_buf, sizeof(select_buf),
				SELECT_PRINT_BLRTS_IMAGE);
			if (select_buf[0] != '\0') {
				if (one_liner)
					xstrcat(out, " ");
				else
					xstrcat(out, "\n   ");
				snprintf(tmp_line, sizeof(tmp_line),
					 "BlrtsImage=%s", select_buf);
				xstrcat(out, tmp_line);
			}
		}
		/****** Line 25 (optional) ******/
		select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
					       select_buf, sizeof(select_buf),
					       SELECT_PRINT_LINUX_IMAGE);
		if (select_buf[0] != '\0') {
			if (one_liner)
				xstrcat(out, " ");
			else
				xstrcat(out, "\n   ");
			if (cluster_flags & CLUSTER_FLAG_BGL)
				snprintf(tmp_line, sizeof(tmp_line),
					 "LinuxImage=%s", select_buf);
			else
				snprintf(tmp_line, sizeof(tmp_line),
					 "CnloadImage=%s", select_buf);

			xstrcat(out, tmp_line);
		}
		/****** Line 26 (optional) ******/
		select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
					       select_buf, sizeof(select_buf),
					       SELECT_PRINT_MLOADER_IMAGE);
		if (select_buf[0] != '\0') {
			if (one_liner)
				xstrcat(out, " ");
			else
				xstrcat(out, "\n   ");
			snprintf(tmp_line, sizeof(tmp_line),
				 "MloaderImage=%s", select_buf);
			xstrcat(out, tmp_line);
		}
		/****** Line 27 (optional) ******/
		select_g_select_jobinfo_sprint(job_ptr->select_jobinfo,
					       select_buf, sizeof(select_buf),
					       SELECT_PRINT_RAMDISK_IMAGE);
		if (select_buf[0] != '\0') {
			if (one_liner)
				xstrcat(out, " ");
			else
				xstrcat(out, "\n   ");
			if (cluster_flags & CLUSTER_FLAG_BGL)
				snprintf(tmp_line, sizeof(tmp_line),
					 "RamDiskImage=%s", select_buf);
			else
				snprintf(tmp_line, sizeof(tmp_line),
					 "IoloadImage=%s", select_buf);
			xstrcat(out, tmp_line);
		}
	}

	/****** Line 28 (optional) ******/
	if (job_ptr->comment) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		snprintf(tmp_line, sizeof(tmp_line), "Comment=%s ",
			 job_ptr->comment);
		xstrcat(out, tmp_line);
	}

	/****** Line 29 (optional) ******/
	if (job_ptr->batch_flag) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		slurm_get_job_stderr(tmp_line, sizeof(tmp_line), job_ptr);
		xstrfmtcat(out, "StdErr=%s", tmp_line);
	}

	/****** Line 30 (optional) ******/
	if (job_ptr->batch_flag) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		slurm_get_job_stdin(tmp_line, sizeof(tmp_line), job_ptr);
		xstrfmtcat(out, "StdIn=%s", tmp_line);
	}

	/****** Line 31 (optional) ******/
	if (job_ptr->batch_flag) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		slurm_get_job_stdout(tmp_line, sizeof(tmp_line), job_ptr);
		xstrfmtcat(out, "StdOut=%s", tmp_line);
	}

	/****** Line 32 (optional) ******/
	if (job_ptr->batch_script) {
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		xstrcat(out, "BatchScript=\n");
		xstrcat(out, job_ptr->batch_script);
	}

	/****** Line 33 (optional) ******/
	if (job_ptr->req_switch) {
		char time_buf[32];
		if (one_liner)
			xstrcat(out, " ");
		else
			xstrcat(out, "\n   ");
		secs2time_str((time_t) job_ptr->wait4switch, time_buf,
			      sizeof(time_buf));
		snprintf(tmp_line, sizeof(tmp_line), "Switches=%u@%s\n",
			 job_ptr->req_switch, time_buf);
		xstrcat(out, tmp_line);
	}

	/****** Line 34 (optional) ******/
	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;

}

/*
 * slurm_load_jobs - issue RPC to get all job configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN/OUT job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags -  job filtering option: 0, SHOW_ALL or SHOW_DETAIL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_jobs (time_t update_time, job_info_msg_t **job_info_msg_pptr,
		 uint16_t show_flags)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.last_update  = update_time;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_INFO:
		*job_info_msg_pptr = (job_info_msg_t *)resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_load_job_user - issue RPC to get slurm information about all jobs
 *	to be run as the specified user
 * IN/OUT job_info_msg_pptr - place to store a job configuration pointer
 * IN user_id - ID of user we want information for
 * IN show_flags - job filtering options
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int slurm_load_job_user (job_info_msg_t **job_info_msg_pptr,
				uint32_t user_id,
				uint16_t show_flags)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_user_id_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.show_flags   = show_flags;
	req.user_id      = user_id;
	req_msg.msg_type = REQUEST_JOB_USER_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_INFO:
		*job_info_msg_pptr = (job_info_msg_t *)resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_load_job - issue RPC to get job information for one job ID
 * IN job_info_msg_pptr - place to store a job configuration pointer
 * IN job_id -  ID of job we want information about
 * IN show_flags -  job filtering option: 0, SHOW_ALL or SHOW_DETAIL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_job (job_info_msg_t **resp, uint32_t job_id, uint16_t show_flags)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_id_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.job_id = job_id;
	req.show_flags = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO_SINGLE;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_INFO:
		*resp = (job_info_msg_t *)resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * slurm_pid2jobid - issue RPC to get the slurm job_id given a process_id
 *	on this machine
 * IN job_pid     - process_id of interest on this machine
 * OUT job_id_ptr - place to store a slurm job_id
 * RET 0 or -1 on error
 */
extern int
slurm_pid2jobid (pid_t job_pid, uint32_t *jobid)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	job_id_request_msg_t req;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	char *this_addr;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (cluster_flags & CLUSTER_FLAG_MULTSD) {
		if ((this_addr = getenv("SLURMD_NODENAME"))) {
			slurm_conf_get_addr(this_addr, &req_msg.address);
		} else {
			this_addr = "localhost";
			slurm_set_addr(&req_msg.address,
				       (uint16_t)slurm_get_slurmd_port(),
				       this_addr);
		}
	} else {
		char this_host[256];
		/*
		 *  Set request message address to slurmd on localhost
		 */
		gethostname_short(this_host, sizeof(this_host));
		this_addr = slurm_conf_get_nodeaddr(this_host);
		if (this_addr == NULL)
			this_addr = xstrdup("localhost");
		slurm_set_addr(&req_msg.address,
			       (uint16_t)slurm_get_slurmd_port(),
			       this_addr);
		xfree(this_addr);
	}

	req.job_pid      = job_pid;
	req_msg.msg_type = REQUEST_JOB_ID;
	req_msg.data     = &req;

	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if (rc != 0 || !resp_msg.auth_cred) {
		error("slurm_pid2jobid: %m");
		if (resp_msg.auth_cred)
			g_slurm_auth_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if (resp_msg.auth_cred)
		g_slurm_auth_destroy(resp_msg.auth_cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_ID:
		*jobid = ((job_id_response_msg_t *) resp_msg.data)->job_id;
		slurm_free_job_id_response_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_get_rem_time - get the expected time remaining for a given job
 * IN jobid     - slurm job id
 * RET remaining time in seconds or -1 on error
 */
extern long slurm_get_rem_time(uint32_t jobid)
{
	time_t now = time(NULL);
	time_t end_time = 0;
	long rc;

	if (slurm_get_end_time(jobid, &end_time) != SLURM_SUCCESS)
		return -1L;

	rc = difftime(end_time, now);
	if (rc < 0)
		rc = 0L;
	return rc;
}

/* FORTRAN VERSIONS OF slurm_get_rem_time */
extern int32_t islurm_get_rem_time__(uint32_t *jobid)
{
	time_t now = time(NULL);
	time_t end_time = 0;
	int32_t rc;

	if ((jobid == NULL)
	    ||  (slurm_get_end_time(*jobid, &end_time)
		 != SLURM_SUCCESS))
		return 0;

	rc = difftime(end_time, now);
	if (rc < 0)
		rc = 0;
	return rc;
}
extern int32_t islurm_get_rem_time2__()
{
	uint32_t jobid;
	char *slurm_job_id = getenv("SLURM_JOB_ID");

	if (slurm_job_id == NULL)
		return 0;
	jobid = atol(slurm_job_id);
	return islurm_get_rem_time__(&jobid);
}


/*
 * slurm_get_end_time - get the expected end time for a given slurm job
 * IN jobid     - slurm job id
 * end_time_ptr - location in which to store scheduled end time for job
 * RET 0 or -1 on error
 */
extern int
slurm_get_end_time(uint32_t jobid, time_t *end_time_ptr)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	job_alloc_info_msg_t job_msg;
	srun_timeout_msg_t *timeout_msg;
	time_t now = time(NULL);
	static uint32_t jobid_cache = 0;
	static uint32_t jobid_env = 0;
	static time_t endtime_cache = 0;
	static time_t last_test_time = 0;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	if (!end_time_ptr)
		slurm_seterrno_ret(EINVAL);

	if (jobid == 0) {
		if (jobid_env) {
			jobid = jobid_env;
		} else {
			char *env = getenv("SLURM_JOB_ID");
			if (env) {
				jobid = (uint32_t) atol(env);
				jobid_env = jobid;
			}
		}
		if (jobid == 0) {
			slurm_seterrno(ESLURM_INVALID_JOB_ID);
			return SLURM_ERROR;
		}
	}

	/* Just use cached data if data less than 60 seconds old */
	if ((jobid == jobid_cache)
	&&  (difftime(now, last_test_time) < 60)) {
		*end_time_ptr  = endtime_cache;
		return SLURM_SUCCESS;
	}

	job_msg.job_id     = jobid;
	req_msg.msg_type   = REQUEST_JOB_END_TIME;
	req_msg.data       = &job_msg;

	if (slurm_send_recv_controller_msg(
		    &req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case SRUN_TIMEOUT:
		timeout_msg = (srun_timeout_msg_t *) resp_msg.data;
		last_test_time = time(NULL);
		jobid_cache    = jobid;
		endtime_cache  = timeout_msg->timeout;
		*end_time_ptr  = endtime_cache;
		slurm_free_srun_timeout_msg(resp_msg.data);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (endtime_cache)
			*end_time_ptr  = endtime_cache;
		else if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		if (endtime_cache)
			*end_time_ptr  = endtime_cache;
		else
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_job_node_ready - report if nodes are ready for job to execute now
 * IN job_id - slurm job id
 * RET: READY_* values as defined in slurm.h
 */
extern int slurm_job_node_ready(uint32_t job_id)
{
	slurm_msg_t req, resp;
	job_id_msg_t msg;
	int rc;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	req.msg_type = REQUEST_JOB_READY;
	req.data     = &msg;
	msg.job_id   = job_id;

	if (slurm_send_recv_controller_msg(&req, &resp) < 0)
		return READY_JOB_ERROR;

	if (resp.msg_type == RESPONSE_JOB_READY) {
		rc = ((return_code_msg_t *) resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
	} else if (resp.msg_type == RESPONSE_SLURM_RC) {
		int job_rc = ((return_code_msg_t *) resp.data) ->
				return_code;
		if ((job_rc == ESLURM_INVALID_PARTITION_NAME) ||
		    (job_rc == ESLURM_INVALID_JOB_ID))
			rc = READY_JOB_FATAL;
		else	/* EAGAIN */
			rc = READY_JOB_ERROR;
		slurm_free_return_code_msg(resp.data);
	} else if (resp.msg_type == RESPONSE_PROLOG_EXECUTING) {
		rc = READY_JOB_ERROR;
	} else {
		rc = READY_JOB_ERROR;
	}

	return rc;
}

extern int slurm_job_cpus_allocated_on_node_id(
	job_resources_t *job_resrcs_ptr, int node_id)
{
	int i;
	int start_node=-1; /* start with -1 less so the array reps
			    * lines up correctly */

	if (!job_resrcs_ptr || node_id < 0)
		slurm_seterrno_ret(EINVAL);

	for (i = 0; i < job_resrcs_ptr->cpu_array_cnt; i++) {
		start_node += job_resrcs_ptr->cpu_array_reps[i];
		if (start_node >= node_id)
			break;
	}

	if (i >= job_resrcs_ptr->cpu_array_cnt)
		return (0); /* nodeid not in this job */

	return job_resrcs_ptr->cpu_array_value[i];
}

extern int slurm_job_cpus_allocated_on_node(job_resources_t *job_resrcs_ptr,
					    const char *node)
{
	hostlist_t node_hl;
	int node_id;

	if (!job_resrcs_ptr || !node || !job_resrcs_ptr->nodes)
		slurm_seterrno_ret(EINVAL);

	node_hl = hostlist_create(job_resrcs_ptr->nodes);
	node_id = hostlist_find(node_hl, node);
	hostlist_destroy(node_hl);
	if (node_id == -1)
		return (0); /* No cpus allocated on this node */

	return slurm_job_cpus_allocated_on_node_id(job_resrcs_ptr, node_id);
}
