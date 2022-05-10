/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *****************************************************************************
 *  Portions Copyright (C) 2010-2017 SchedMD LLC <https://www.schedmd.com>.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "slurm/slurm_errno.h"

#include "src/common/cpu_frequency.h"
#include "src/common/forward.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/select.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/uthash.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/* Use a hash table to identify duplicate job records across the clusters in
 * a federation */
#define JOB_HASH_SIZE 1000

/* Data structures for pthreads used to gather job information from multiple
 * clusters in parallel */
typedef struct load_job_req_struct {
	slurmdb_cluster_rec_t *cluster;
	bool local_cluster;
	slurm_msg_t *req_msg;
	List resp_msg_list;
} load_job_req_struct_t;

typedef struct load_job_resp_struct {
	job_info_msg_t *new_msg;
} load_job_resp_struct_t;

typedef struct load_job_prio_resp_struct {
	bool local_cluster;
	priority_factors_response_msg_t *new_msg;
} load_job_prio_resp_struct_t;

static pthread_mutex_t job_node_info_lock = PTHREAD_MUTEX_INITIALIZER;
static node_info_msg_t *job_node_ptr = NULL;

/* This set of functions loads/free node information so that we can map a job's
 * core bitmap to it's CPU IDs based upon the thread count on each node. */

static uint32_t _threads_per_core(char *host)
{
	uint32_t i, threads = 1;

	if (!host)
		return threads;

	slurm_mutex_lock(&job_node_info_lock);
	if (!job_node_ptr)
		slurm_load_node((time_t) NULL, &job_node_ptr, 0);

	for (i = 0; i < job_node_ptr->record_count; i++) {
		if (job_node_ptr->node_array[i].name &&
		    !xstrcmp(host, job_node_ptr->node_array[i].name)) {
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
 * %x - Job name
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
	xstrsubstituteall(tmp, "%x", job_ptr->name);	/* Job name */

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
		snprintf(buf, buf_size, "%s", "/dev/null");
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
 *	equivalent Slurm job ID number (e.g. "123_2" to 124). If this job ID
 *	contains an HetJob component, map this to the equivalent Slurm job ID
 *	number (e.g. "123+2" to 125).
 *
 * IN job_id_str - String containing a single job ID number
 * RET - equivalent job ID number or 0 on error
 */
extern uint32_t slurm_xlate_job_id(char *job_id_str)
{
	char *next_str;
	uint32_t job_id;

	job_id = (uint32_t) strtol(job_id_str, &next_str, 10);
	if (next_str[0] == '\0')
		return job_id;

	if (next_str[0] == '_') {
		job_info_msg_t *resp = NULL;
		slurm_job_info_t *job_ptr;
		uint16_t array_id = (uint16_t) strtol(next_str + 1, &next_str,
						      10);
		if (next_str[0] != '\0')
			return (uint32_t) 0;

		if ((slurm_load_job(&resp, job_id, SHOW_ALL) != 0) ||
		    (resp == NULL))
			return (uint32_t) 0;

		job_id = 0;
		job_ptr = resp->job_array;
		for (uint32_t i = 0; i < resp->record_count; i++, job_ptr++) {
			if (job_ptr->array_task_id == array_id) {
				job_id = job_ptr->job_id;
				break;
			}
		}

		slurm_free_job_info_msg(resp);
		return job_id;
	}

	if (next_str[0] == '+') {
		uint16_t comp_offset =
			(uint16_t) strtol(next_str + 1, &next_str, 10);

		if (next_str[0] != '\0') {
			return (uint32_t) 0;
		}

		return job_id + comp_offset;
	}

	return (uint32_t) 0;
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
	if (upper > 0)
		snprintf(str, str_size, "%u-%u", lower, upper);
	else
		snprintf(str, str_size, "%u", lower);
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

	if ((print_this = slurm_sprint_job_info(job_ptr, one_liner))) {
		fprintf(out, "%s", print_this);
		xfree(print_this);
	}
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
	char *gres_last = "", tmp1[128], tmp2[128];
	char *tmp6_ptr;
	char tmp_line[1024 * 128];
	char tmp_path[MAXPATHLEN];
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
	int64_t nice;
	int bit_inx, bit_reps;
	uint64_t *last_mem_alloc_ptr = NULL;
	uint64_t last_mem_alloc = NO_VAL64;
	char *last_hosts;
	hostlist_t hl, hl_last;
	uint32_t threads;
	char *line_end = (one_liner) ? " " : "\n   ";

	if (job_ptr->job_id == 0)	/* Duplicated sibling job record */
		return NULL;

	/****** Line 1 ******/
	xstrfmtcat(out, "JobId=%u ", job_ptr->job_id);

	if (job_ptr->array_job_id) {
		if (job_ptr->array_task_str) {
			xstrfmtcat(out, "ArrayJobId=%u ArrayTaskId=%s ",
				   job_ptr->array_job_id,
				   job_ptr->array_task_str);
		} else {
			xstrfmtcat(out, "ArrayJobId=%u ArrayTaskId=%u ",
				   job_ptr->array_job_id,
				   job_ptr->array_task_id);
		}
		if (job_ptr->array_max_tasks) {
			xstrfmtcat(out, "ArrayTaskThrottle=%u ",
				   job_ptr->array_max_tasks);
		}
	} else if (job_ptr->het_job_id) {
		xstrfmtcat(out, "HetJobId=%u HetJobOffset=%u ",
			   job_ptr->het_job_id, job_ptr->het_job_offset);
	}
	xstrfmtcat(out, "JobName=%s", job_ptr->name);
	xstrcat(out, line_end);

	/****** Line ******/
	if (job_ptr->het_job_id_set) {
		xstrfmtcat(out, "HetJobIdSet=%s", job_ptr->het_job_id_set);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	user_name = uid_to_string((uid_t) job_ptr->user_id);
	group_name = gid_to_string((gid_t) job_ptr->group_id);
	xstrfmtcat(out, "UserId=%s(%u) GroupId=%s(%u) MCS_label=%s",
		   user_name, job_ptr->user_id, group_name, job_ptr->group_id,
		   (job_ptr->mcs_label==NULL) ? "N/A" : job_ptr->mcs_label);
	xfree(user_name);
	xfree(group_name);
	xstrcat(out, line_end);

	/****** Line 3 ******/
	nice = ((int64_t)job_ptr->nice) - NICE_OFFSET;
	xstrfmtcat(out, "Priority=%u Nice=%"PRIi64" Account=%s QOS=%s",
		   job_ptr->priority, nice, job_ptr->account, job_ptr->qos);
	if (slurm_get_track_wckey())
		xstrfmtcat(out, " WCKey=%s", job_ptr->wckey);
	xstrcat(out, line_end);

	/****** Line 4 ******/
	xstrfmtcat(out, "JobState=%s ", job_state_string(job_ptr->job_state));

	if (job_ptr->state_desc) {
		/* Replace white space with underscore for easier parsing */
		for (j=0; job_ptr->state_desc[j]; j++) {
			if (isspace((int)job_ptr->state_desc[j]))
				job_ptr->state_desc[j] = '_';
		}
		xstrfmtcat(out, "Reason=%s ", job_ptr->state_desc);
	} else
		xstrfmtcat(out, "Reason=%s ", job_reason_string(job_ptr->state_reason));

	xstrfmtcat(out, "Dependency=%s", job_ptr->dependency);
	xstrcat(out, line_end);

	/****** Line 5 ******/
	xstrfmtcat(out, "Requeue=%u Restarts=%u BatchFlag=%u Reboot=%u ",
		 job_ptr->requeue, job_ptr->restart_cnt, job_ptr->batch_flag,
		 job_ptr->reboot);
	exit_status = term_sig = 0;
	if (WIFSIGNALED(job_ptr->exit_code))
		term_sig = WTERMSIG(job_ptr->exit_code);
	else if (WIFEXITED(job_ptr->exit_code))
		exit_status = WEXITSTATUS(job_ptr->exit_code);
	xstrfmtcat(out, "ExitCode=%u:%u", exit_status, term_sig);
	xstrcat(out, line_end);

	/****** Line 5a (optional) ******/
	if (job_ptr->show_flags & SHOW_DETAIL) {
		exit_status = term_sig = 0;
		if (WIFSIGNALED(job_ptr->derived_ec))
			term_sig = WTERMSIG(job_ptr->derived_ec);
		else if (WIFEXITED(job_ptr->derived_ec))
			exit_status = WEXITSTATUS(job_ptr->derived_ec);
		xstrfmtcat(out, "DerivedExitCode=%u:%u", exit_status, term_sig);
		xstrcat(out, line_end);
	}

	/****** Line 6 ******/
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
	secs2time_str(run_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "RunTime=%s ", time_str);

	if (job_ptr->time_limit == NO_VAL)
		xstrcat(out, "TimeLimit=Partition_Limit ");
	else {
		mins2time_str(job_ptr->time_limit, time_str, sizeof(time_str));
		xstrfmtcat(out, "TimeLimit=%s ", time_str);
	}

	if (job_ptr->time_min == 0)
		xstrcat(out, "TimeMin=N/A");
	else {
		mins2time_str(job_ptr->time_min, time_str, sizeof(time_str));
		xstrfmtcat(out, "TimeMin=%s", time_str);
	}
	xstrcat(out, line_end);

	/****** Line 7 ******/
	slurm_make_time_str(&job_ptr->submit_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "SubmitTime=%s ", time_str);

	slurm_make_time_str(&job_ptr->eligible_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "EligibleTime=%s", time_str);

	xstrcat(out, line_end);

	/****** Line 7.5 ******/
	slurm_make_time_str(&job_ptr->accrue_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "AccrueTime=%s", time_str);

	xstrcat(out, line_end);

	/****** Line 8 (optional) ******/
	if (job_ptr->resize_time) {
		slurm_make_time_str(&job_ptr->resize_time, time_str, sizeof(time_str));
		xstrfmtcat(out, "ResizeTime=%s", time_str);
		xstrcat(out, line_end);
	}

	/****** Line 9 ******/
	slurm_make_time_str(&job_ptr->start_time, time_str, sizeof(time_str));
	xstrfmtcat(out, "StartTime=%s ", time_str);

	if ((job_ptr->time_limit == INFINITE) &&
	    (job_ptr->end_time > time(NULL)))
		xstrcat(out, "EndTime=Unknown ");
	else {
		slurm_make_time_str(&job_ptr->end_time, time_str, sizeof(time_str));
		xstrfmtcat(out, "EndTime=%s ", time_str);
	}

	if (job_ptr->deadline) {
		slurm_make_time_str(&job_ptr->deadline, time_str, sizeof(time_str));
		xstrfmtcat(out, "Deadline=%s", time_str);
	} else {
		xstrcat(out, "Deadline=N/A");
	}

	xstrcat(out, line_end);

	/****** Line ******/

	if (job_ptr->bitflags & CRON_JOB || job_ptr->cronspec) {
		if (job_ptr->bitflags & CRON_JOB)
			xstrcat(out, "CronJob=Yes ");
		xstrfmtcat(out, "CrontabSpec=\"%s\"", job_ptr->cronspec);
		xstrcat(out, line_end);
	}

	/****** Line ******/
	/*
	 * only print this line if preemption is enabled and job started
	 * 	see src/slurmctld/job_mgr.c:pack_job, 'preemptable'
	 */
	if (job_ptr->preemptable_time) {
		slurm_make_time_str(&job_ptr->preemptable_time,
				    time_str, sizeof(time_str));
		xstrfmtcat(out, "PreemptEligibleTime=%s ", time_str);

		if (job_ptr->preempt_time == 0)
			xstrcat(out, "PreemptTime=None");
		else {
			slurm_make_time_str(&job_ptr->preempt_time, time_str,
					    sizeof(time_str));
			xstrfmtcat(out, "PreemptTime=%s", time_str);
		}

		xstrcat(out, line_end);
	}

	/****** Line 10 ******/
	if (job_ptr->suspend_time) {
		slurm_make_time_str(&job_ptr->suspend_time, time_str, sizeof(time_str));
		xstrfmtcat(out, "SuspendTime=%s ", time_str);
	} else
		xstrcat(out, "SuspendTime=None ");

	xstrfmtcat(out, "SecsPreSuspend=%ld ", (long int)job_ptr->pre_sus_time);

	slurm_make_time_str(&job_ptr->last_sched_eval, time_str,
			    sizeof(time_str));
	xstrfmtcat(out, "LastSchedEval=%s Scheduler=%s%s", time_str,
		   job_ptr->bitflags & BACKFILL_SCHED ? "Backfill" : "Main",
		   job_ptr->bitflags & BACKFILL_LAST ? ":*" : "");
	xstrcat(out, line_end);

	/****** Line 11 ******/
	xstrfmtcat(out, "Partition=%s AllocNode:Sid=%s:%u",
		   job_ptr->partition, job_ptr->alloc_node, job_ptr->alloc_sid);
	xstrcat(out, line_end);

	/****** Line 12 ******/
	xstrfmtcat(out, "Req%s=%s Exc%s=%s", nodelist, job_ptr->req_nodes,
		   nodelist, job_ptr->exc_nodes);
	xstrcat(out, line_end);

	/****** Line 13 ******/
	xstrfmtcat(out, "%s=%s", nodelist, job_ptr->nodes);

	if (job_ptr->sched_nodes)
		xstrfmtcat(out, " Sched%s=%s", nodelist, job_ptr->sched_nodes);

	xstrcat(out, line_end);

	/****** Line 14 (optional) ******/
	if (job_ptr->batch_features)
		xstrfmtcat(out, "BatchFeatures=%s", job_ptr->batch_features);
	if (job_ptr->batch_host) {
		char *sep = "";
		if (job_ptr->batch_features)
			sep = " ";
		xstrfmtcat(out, "%sBatchHost=%s", sep, job_ptr->batch_host);
	}
	if (job_ptr->batch_features || job_ptr->batch_host)
		xstrcat(out, line_end);

	/****** Line 14a (optional) ******/
	if (job_ptr->fed_siblings_active || job_ptr->fed_siblings_viable) {
		xstrfmtcat(out, "FedOrigin=%s FedViableSiblings=%s FedActiveSiblings=%s",
			   job_ptr->fed_origin_str,
			   job_ptr->fed_siblings_viable_str,
			   job_ptr->fed_siblings_active_str);
		xstrcat(out, line_end);
	}

	/****** Line 15 ******/
	if (IS_JOB_PENDING(job_ptr)) {
		min_nodes = job_ptr->num_nodes;
		max_nodes = job_ptr->max_nodes;
		if (max_nodes && (max_nodes < min_nodes))
			min_nodes = max_nodes;
	} else {
		min_nodes = job_ptr->num_nodes;
		max_nodes = 0;
	}

	_sprint_range(tmp_line, sizeof(tmp_line), min_nodes, max_nodes);
	xstrfmtcat(out, "NumNodes=%s ", tmp_line);
	_sprint_range(tmp_line, sizeof(tmp_line), job_ptr->num_cpus, job_ptr->max_cpus);
	xstrfmtcat(out, "NumCPUs=%s ", tmp_line);

	if (job_ptr->num_tasks == NO_VAL)
		xstrcat(out, "NumTasks=N/A ");
	else
		xstrfmtcat(out, "NumTasks=%u ", job_ptr->num_tasks);

	if (job_ptr->cpus_per_task == NO_VAL16)
		xstrfmtcat(out, "CPUs/Task=N/A ");
	else
		xstrfmtcat(out, "CPUs/Task=%u ", job_ptr->cpus_per_task);

	if (job_ptr->boards_per_node == NO_VAL16)
		xstrcat(out, "ReqB:S:C:T=*:");
	else
		xstrfmtcat(out, "ReqB:S:C:T=%u:", job_ptr->boards_per_node);

	if (job_ptr->sockets_per_board == NO_VAL16)
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->sockets_per_board);

	if (job_ptr->cores_per_socket == NO_VAL16)
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->cores_per_socket);

	if (job_ptr->threads_per_core == NO_VAL16)
		xstrcat(out, "*");
	else
		xstrfmtcat(out, "%u", job_ptr->threads_per_core);

	xstrcat(out, line_end);

	/****** Line 16 ******/
	/* Tres should already of been converted at this point from simple */
	xstrfmtcat(out, "TRES=%s",
		   job_ptr->tres_alloc_str ? job_ptr->tres_alloc_str
					   : job_ptr->tres_req_str);
	xstrcat(out, line_end);

	/****** Line 17 ******/
	if (job_ptr->sockets_per_node == NO_VAL16)
		xstrcat(out, "Socks/Node=* ");
	else
		xstrfmtcat(out, "Socks/Node=%u ", job_ptr->sockets_per_node);

	if (job_ptr->ntasks_per_node == NO_VAL16)
		xstrcat(out, "NtasksPerN:B:S:C=*:");
	else
		xstrfmtcat(out, "NtasksPerN:B:S:C=%u:", job_ptr->ntasks_per_node);

	if (job_ptr->ntasks_per_board == NO_VAL16)
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->ntasks_per_board);

	if ((job_ptr->ntasks_per_socket == NO_VAL16) ||
	    (job_ptr->ntasks_per_socket == INFINITE16))
		xstrcat(out, "*:");
	else
		xstrfmtcat(out, "%u:", job_ptr->ntasks_per_socket);

	if ((job_ptr->ntasks_per_core == NO_VAL16) ||
	    (job_ptr->ntasks_per_core == INFINITE16))
		xstrcat(out, "* ");
	else
		xstrfmtcat(out, "%u ", job_ptr->ntasks_per_core);

	if (job_ptr->core_spec == NO_VAL16)
		xstrcat(out, "CoreSpec=*");
	else if (job_ptr->core_spec & CORE_SPEC_THREAD)
		xstrfmtcat(out, "ThreadSpec=%d",
			   (job_ptr->core_spec & (~CORE_SPEC_THREAD)));
	else
		xstrfmtcat(out, "CoreSpec=%u", job_ptr->core_spec);

	xstrcat(out, line_end);

	if (job_resrcs && job_resrcs->core_bitmap &&
	    ((last = bit_fls(job_resrcs->core_bitmap)) != -1)) {

		xstrfmtcat(out, "JOB_GRES=%s", job_ptr->gres_total);
		xstrcat(out, line_end);

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

		gres_last = "";
		/* tmp1[] stores the current cpu(s) allocated */
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
			 * If the allocation values for this host are not the
			 * same as the last host, print the report of the last
			 * group of hosts that had identical allocation values.
			 */
			if (xstrcmp(tmp1, tmp2) ||
			    ((rel_node_inx < job_ptr->gres_detail_cnt) &&
			     xstrcmp(job_ptr->gres_detail_str[rel_node_inx],
				     gres_last)) ||
			    (last_mem_alloc_ptr != job_resrcs->memory_allocated) ||
			    (job_resrcs->memory_allocated &&
			     (last_mem_alloc !=
			      job_resrcs->memory_allocated[rel_node_inx]))) {
				if (hostlist_count(hl_last)) {
					last_hosts =
						hostlist_ranged_string_xmalloc(
						hl_last);
					xstrfmtcat(out,
						   "  Nodes=%s CPU_IDs=%s "
						   "Mem=%"PRIu64" GRES=%s",
						   last_hosts, tmp2,
						   last_mem_alloc_ptr ?
						   last_mem_alloc : 0,
						   gres_last);
					xfree(last_hosts);
					xstrcat(out, line_end);

					hostlist_destroy(hl_last);
					hl_last = hostlist_create(NULL);
				}

				strcpy(tmp2, tmp1);
				if (rel_node_inx < job_ptr->gres_detail_cnt) {
					gres_last = job_ptr->
						    gres_detail_str[rel_node_inx];
				} else {
					gres_last = "";
				}
				last_mem_alloc_ptr = job_resrcs->memory_allocated;
				if (last_mem_alloc_ptr)
					last_mem_alloc = job_resrcs->
						memory_allocated[rel_node_inx];
				else
					last_mem_alloc = NO_VAL64;
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
			xstrfmtcat(out, "  Nodes=%s CPU_IDs=%s Mem=%"PRIu64" GRES=%s",
				 last_hosts, tmp2,
				 last_mem_alloc_ptr ? last_mem_alloc : 0,
				 gres_last);
			xfree(last_hosts);
			xstrcat(out, line_end);
		}
		hostlist_destroy(hl);
		hostlist_destroy(hl_last);
	}
	/****** Line 18 ******/
	if (job_ptr->pn_min_memory & MEM_PER_CPU) {
		job_ptr->pn_min_memory &= (~MEM_PER_CPU);
		tmp6_ptr = "CPU";
	} else
		tmp6_ptr = "Node";

	xstrfmtcat(out, "MinCPUsNode=%u ", job_ptr->pn_min_cpus);

	convert_num_unit((float)job_ptr->pn_min_memory, tmp1, sizeof(tmp1),
			 UNIT_MEGA, NO_VAL, CONVERT_NUM_UNIT_EXACT);
	convert_num_unit((float)job_ptr->pn_min_tmp_disk, tmp2, sizeof(tmp2),
			 UNIT_MEGA, NO_VAL, CONVERT_NUM_UNIT_EXACT);
	xstrfmtcat(out, "MinMemory%s=%s MinTmpDiskNode=%s", tmp6_ptr, tmp1, tmp2);
	xstrcat(out, line_end);

	/****** Line ******/
	secs2time_str((time_t)job_ptr->delay_boot, tmp1, sizeof(tmp1));
	xstrfmtcat(out, "Features=%s DelayBoot=%s", job_ptr->features, tmp1);
	xstrcat(out, line_end);

	/****** Line (optional) ******/
	if (job_ptr->cluster_features) {
		xstrfmtcat(out, "ClusterFeatures=%s",
			   job_ptr->cluster_features);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	if (job_ptr->prefer) {
		xstrfmtcat(out, "Prefer=%s", job_ptr->prefer);
		xstrcat(out, line_end);
	}

	/****** Line (optional) ******/
	if (job_ptr->resv_name) {
		xstrfmtcat(out, "Reservation=%s", job_ptr->resv_name);
		xstrcat(out, line_end);
	}

	/****** Line 20 ******/
	xstrfmtcat(out, "OverSubscribe=%s Contiguous=%d Licenses=%s Network=%s",
		   job_share_string(job_ptr->shared), job_ptr->contiguous,
		   job_ptr->licenses, job_ptr->network);
	xstrcat(out, line_end);

	/****** Line 21 ******/
	xstrfmtcat(out, "Command=%s", job_ptr->command);
	xstrcat(out, line_end);

	/****** Line 22 ******/
	xstrfmtcat(out, "WorkDir=%s", job_ptr->work_dir);

	/****** Line (optional) ******/
	if (job_ptr->admin_comment) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "AdminComment=%s ", job_ptr->admin_comment);
	}

	/****** Line (optional) ******/
	if (job_ptr->system_comment) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "SystemComment=%s ", job_ptr->system_comment);
	}

	/****** Line (optional) ******/
	if (job_ptr->comment) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Comment=%s ", job_ptr->comment);
	}

	/****** Line 30 (optional) ******/
	if (job_ptr->batch_flag) {
		xstrcat(out, line_end);
		slurm_get_job_stderr(tmp_path, sizeof(tmp_path), job_ptr);
		xstrfmtcat(out, "StdErr=%s", tmp_path);
	}

	/****** Line 31 (optional) ******/
	if (job_ptr->batch_flag) {
		xstrcat(out, line_end);
		slurm_get_job_stdin(tmp_path, sizeof(tmp_path), job_ptr);
		xstrfmtcat(out, "StdIn=%s", tmp_path);
	}

	/****** Line 32 (optional) ******/
	if (job_ptr->batch_flag) {
		xstrcat(out, line_end);
		slurm_get_job_stdout(tmp_path, sizeof(tmp_path), job_ptr);
		xstrfmtcat(out, "StdOut=%s", tmp_path);
	}

	/****** Line 34 (optional) ******/
	if (job_ptr->req_switch) {
		char time_buf[32];
		xstrcat(out, line_end);
		secs2time_str((time_t) job_ptr->wait4switch, time_buf,
			      sizeof(time_buf));
		xstrfmtcat(out, "Switches=%u@%s", job_ptr->req_switch, time_buf);
	}

	/****** Line 35 (optional) ******/
	if (job_ptr->burst_buffer) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "BurstBuffer=%s", job_ptr->burst_buffer);
	}

	/****** Line (optional) ******/
	if (job_ptr->burst_buffer_state) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "BurstBufferState=%s",
			   job_ptr->burst_buffer_state);
	}

	/****** Line 36 (optional) ******/
	if (cpu_freq_debug(NULL, NULL, tmp1, sizeof(tmp1),
			   job_ptr->cpu_freq_gov, job_ptr->cpu_freq_min,
			   job_ptr->cpu_freq_max, NO_VAL) != 0) {
		xstrcat(out, line_end);
		xstrcat(out, tmp1);
	}

	/****** Line 37 ******/
	xstrcat(out, line_end);
	xstrfmtcat(out, "Power=%s", power_flags_str(job_ptr->power_flags));

	/****** Line 38 (optional) ******/
	if (job_ptr->bitflags &
	    (GRES_DISABLE_BIND | GRES_ENFORCE_BIND | KILL_INV_DEP |
	     NO_KILL_INV_DEP | SPREAD_JOB)) {
		xstrcat(out, line_end);
		if (job_ptr->bitflags & GRES_DISABLE_BIND)
			xstrcat(out, "GresEnforceBind=No");
		if (job_ptr->bitflags & GRES_ENFORCE_BIND)
			xstrcat(out, "GresEnforceBind=Yes");
		if (job_ptr->bitflags & KILL_INV_DEP)
			xstrcat(out, "KillOInInvalidDependent=Yes");
		if (job_ptr->bitflags & NO_KILL_INV_DEP)
			xstrcat(out, "KillOInInvalidDependent=No");
		if (job_ptr->bitflags & SPREAD_JOB)
			xstrcat(out, "SpreadJob=Yes");
	}

	/****** Line (optional) ******/
	if (job_ptr->cpus_per_tres) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "CpusPerTres=%s", job_ptr->cpus_per_tres);
	}

	/****** Line (optional) ******/
	if (job_ptr->mem_per_tres) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "MemPerTres=%s", job_ptr->mem_per_tres);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_bind) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresBind=%s", job_ptr->tres_bind);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_freq) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresFreq=%s", job_ptr->tres_freq);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_job) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerJob=%s", job_ptr->tres_per_job);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_node) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerNode=%s", job_ptr->tres_per_node);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_socket) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerSocket=%s", job_ptr->tres_per_socket);
	}

	/****** Line (optional) ******/
	if (job_ptr->tres_per_task) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "TresPerTask=%s", job_ptr->tres_per_task);
	}

	/****** Line (optional) ******/
	if (job_ptr->mail_type && job_ptr->mail_user) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "MailUser=%s MailType=%s",
			   job_ptr->mail_user,
			   print_mail_type(job_ptr->mail_type));
	}

	/****** Line (optional) ******/
	if ((job_ptr->ntasks_per_tres) &&
	    (job_ptr->ntasks_per_tres != NO_VAL16) &&
	    (job_ptr->ntasks_per_tres != INFINITE16)) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "NtasksPerTRES=%u", job_ptr->ntasks_per_tres);
	}

	/****** Line (optional) ******/
	if (job_ptr->container) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "Container=%s", job_ptr->container);
	}

	/****** Line (optional) ******/
	if (job_ptr->selinux_context) {
		xstrcat(out, line_end);
		xstrfmtcat(out, "SELinuxContext=%s", job_ptr->selinux_context);
	}

	xstrcat(out, line_end);

	/****** END OF JOB RECORD ******/
	if (one_liner)
		xstrcat(out, "\n");
	else
		xstrcat(out, "\n\n");

	return out;
}

/* Return true if the specified job id is local to a cluster
 * (not a federated job) */
static inline bool _test_local_job(uint32_t job_id)
{
	if ((job_id & (~MAX_JOB_ID)) == 0)
		return true;
	return false;
}

static int
_load_cluster_jobs(slurm_msg_t *req_msg, job_info_msg_t **job_info_msg_pptr,
		   slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t resp_msg;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&resp_msg);

	*job_info_msg_pptr = NULL;

	if (slurm_send_recv_controller_msg(req_msg, &resp_msg, cluster) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_INFO:
		*job_info_msg_pptr = (job_info_msg_t *)resp_msg.data;
		resp_msg.data = NULL;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		break;
	default:
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		break;
	}
	if (rc)
		slurm_seterrno(rc);

	return rc;
}

/* Thread to read job information from some cluster */
static void *_load_job_thread(void *args)
{
	load_job_req_struct_t *load_args = (load_job_req_struct_t *) args;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	job_info_msg_t *new_msg = NULL;
	int rc;

	if ((rc = _load_cluster_jobs(load_args->req_msg, &new_msg, cluster)) ||
	    !new_msg) {
		verbose("Error reading job information from cluster %s: %s",
			cluster->name, slurm_strerror(rc));
	} else {
		load_job_resp_struct_t *job_resp;
		job_resp = xmalloc(sizeof(load_job_resp_struct_t));
		job_resp->new_msg = new_msg;
		list_append(load_args->resp_msg_list, job_resp);
	}
	xfree(args);

	return (void *) NULL;
}


static int _sort_orig_clusters(const void *a, const void *b)
{
	slurm_job_info_t *job1 = (slurm_job_info_t *)a;
	slurm_job_info_t *job2 = (slurm_job_info_t *)b;

	if (!xstrcmp(job1->cluster, job1->fed_origin_str))
		return -1;
	if (!xstrcmp(job2->cluster, job2->fed_origin_str))
		return 1;
	return 0;
}

static int _load_fed_jobs(slurm_msg_t *req_msg,
			  job_info_msg_t **job_info_msg_pptr,
			  uint16_t show_flags, char *cluster_name,
			  slurmdb_federation_rec_t *fed)
{
	int i, j;
	load_job_resp_struct_t *job_resp;
	job_info_msg_t *orig_msg = NULL, *new_msg = NULL;
	uint32_t new_rec_cnt;
	uint32_t hash_inx, *hash_tbl_size = NULL, **hash_job_id = NULL;
	slurmdb_cluster_rec_t *cluster;
	ListIterator iter;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	load_job_req_struct_t *load_args;
	List resp_msg_list;

	*job_info_msg_pptr = NULL;

	/* Spawn one pthread per cluster to collect job information */
	resp_msg_list = list_create(NULL);
	load_thread = xmalloc(sizeof(pthread_t) *
			      list_count(fed->cluster_list));
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter))) {
		if ((cluster->control_host == NULL) ||
		    (cluster->control_host[0] == '\0'))
			continue;	/* Cluster down */

		/* Only show jobs from the local cluster */
		if ((show_flags & SHOW_LOCAL) &&
		    xstrcmp(cluster->name, cluster_name))
			continue;

		load_args = xmalloc(sizeof(load_job_req_struct_t));
		load_args->cluster = cluster;
		load_args->req_msg = req_msg;
		load_args->resp_msg_list = resp_msg_list;
		slurm_thread_create(&load_thread[pthread_count],
				    _load_job_thread, load_args);
		pthread_count++;

	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		pthread_join(load_thread[i], NULL);
	xfree(load_thread);

	/* Merge the responses into a single response message */
	iter = list_iterator_create(resp_msg_list);
	while ((job_resp = (load_job_resp_struct_t *) list_next(iter))) {
		new_msg = job_resp->new_msg;
		if (!orig_msg) {
			orig_msg = new_msg;
			*job_info_msg_pptr = orig_msg;
		} else {
			/* Merge job records into a single response message */
			orig_msg->last_update = MIN(orig_msg->last_update,
						    new_msg->last_update);
			new_rec_cnt = orig_msg->record_count +
				      new_msg->record_count;
			if (new_msg->record_count) {
				orig_msg->job_array =
					xrealloc(orig_msg->job_array,
						 sizeof(slurm_job_info_t) *
						 new_rec_cnt);
				(void) memcpy(orig_msg->job_array +
					      orig_msg->record_count,
					      new_msg->job_array,
					      sizeof(slurm_job_info_t) *
					      new_msg->record_count);
				orig_msg->record_count = new_rec_cnt;
			}
			xfree(new_msg->job_array);
			xfree(new_msg);
		}
		xfree(job_resp);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resp_msg_list);

	if (!orig_msg)
		slurm_seterrno_ret(ESLURM_INVALID_JOB_ID);

	/* Find duplicate job records and jobs local to other clusters and set
	 * their job_id == 0 so they get skipped in reporting */
	if ((show_flags & SHOW_SIBLING) == 0) {
		hash_tbl_size = xmalloc(sizeof(uint32_t) * JOB_HASH_SIZE);
		hash_job_id = xmalloc(sizeof(uint32_t *) * JOB_HASH_SIZE);
		for (i = 0; i < JOB_HASH_SIZE; i++) {
			hash_tbl_size[i] = 100;
			hash_job_id[i] = xmalloc(sizeof(uint32_t ) *
						 hash_tbl_size[i]);
		}
	}

	/* Put the origin jobs at top and remove duplicates. */
	qsort(orig_msg->job_array, orig_msg->record_count,
	      sizeof(slurm_job_info_t), _sort_orig_clusters);
	for (i = 0; orig_msg && i < orig_msg->record_count; i++) {
		slurm_job_info_t *job_ptr = &orig_msg->job_array[i];

		/*
		 * Only show non-federated jobs that are local. Non-federated
		 * jobs will not have a fed_origin_str.
		 */
		if (_test_local_job(job_ptr->job_id) &&
		    !job_ptr->fed_origin_str &&
		    xstrcmp(job_ptr->cluster, cluster_name)) {
			job_ptr->job_id = 0;
			continue;
		}

		if (show_flags & SHOW_SIBLING)
			continue;
		hash_inx = job_ptr->job_id % JOB_HASH_SIZE;
		for (j = 0;
		     (j < hash_tbl_size[hash_inx] && hash_job_id[hash_inx][j]);
		     j++) {
			if (job_ptr->job_id == hash_job_id[hash_inx][j]) {
				job_ptr->job_id = 0;
				break;
			}
		}
		if (job_ptr->job_id == 0) {
			continue;	/* Duplicate */
		} else if (j >= hash_tbl_size[hash_inx]) {
			hash_tbl_size[hash_inx] *= 2;
			xrealloc(hash_job_id[hash_inx],
				 sizeof(uint32_t) * hash_tbl_size[hash_inx]);
		}
		hash_job_id[hash_inx][j] = job_ptr->job_id;
	}
	if ((show_flags & SHOW_SIBLING) == 0) {
		for (i = 0; i < JOB_HASH_SIZE; i++)
			xfree(hash_job_id[i]);
		xfree(hash_tbl_size);
		xfree(hash_job_id);
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_job_batch_script - retrieve the batch script for a given jobid
 * returns SLURM_SUCCESS, or appropriate error code
 */
extern int slurm_job_batch_script(FILE *out, uint32_t jobid)
{
	job_id_msg_t msg;
	slurm_msg_t req, resp;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	memset(&msg, 0, sizeof(msg));
	msg.job_id = jobid;
	req.msg_type = REQUEST_BATCH_SCRIPT;
	req.data = &msg;

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec) < 0)
		return SLURM_ERROR;

	if (resp.msg_type == RESPONSE_BATCH_SCRIPT) {
		if (fprintf(out, "%s", (char *) resp.data) < 0)
			rc = SLURM_ERROR;
		xfree(resp.data);
	} else if (resp.msg_type == RESPONSE_SLURM_RC) {
		rc = ((return_code_msg_t *) resp.data)->return_code;
		slurm_free_return_code_msg(resp.data);
		if (rc)
			slurm_seterrno_ret(rc);
	} else {
		rc = SLURM_ERROR;
	}

	return rc;
}

/*
 * slurm_load_jobs - issue RPC to get all job configuration
 *	information if changed since update_time
 * IN update_time - time of current configuration data
 * IN/OUT job_info_msg_pptr - place to store a job configuration pointer
 * IN show_flags -  job filtering option: 0, SHOW_ALL, SHOW_DETAIL or SHOW_LOCAL
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_job_info_msg
 */
extern int
slurm_load_jobs (time_t update_time, job_info_msg_t **job_info_msg_pptr,
		 uint16_t show_flags)
{
	slurm_msg_t req_msg;
	job_info_request_msg_t req;
	char *cluster_name = NULL;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if (working_cluster_rec)
		cluster_name = working_cluster_rec->name;
	else
		cluster_name = slurm_conf.cluster_name;

	if ((show_flags & SHOW_FEDERATION) && !(show_flags & SHOW_LOCAL) &&
	    (slurm_load_federation(&ptr) == SLURM_SUCCESS) &&
	    cluster_in_federation(ptr, cluster_name)) {
		/* In federation. Need full info from all clusters */
		update_time = (time_t) 0;
		show_flags &= (~SHOW_LOCAL);
	} else {
		/* Report local cluster info only */
		show_flags |= SHOW_LOCAL;
		show_flags &= (~SHOW_FEDERATION);
	}

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.last_update  = update_time;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO;
	req_msg.data     = &req;

	if (show_flags & SHOW_FEDERATION) {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_jobs(&req_msg, job_info_msg_pptr, show_flags,
				    cluster_name, fed);
	} else {
		rc = _load_cluster_jobs(&req_msg, job_info_msg_pptr,
					working_cluster_rec);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
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
	slurm_msg_t req_msg;
	job_user_id_msg_t req;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if ((show_flags & SHOW_LOCAL) == 0) {
		if (slurm_load_federation(&ptr) ||
		    !cluster_in_federation(ptr, slurm_conf.cluster_name)) {
			/* Not in federation */
			show_flags |= SHOW_LOCAL;
		}
	}

	slurm_msg_t_init(&req_msg);
	memset(&req, 0, sizeof(req));
	req.show_flags   = show_flags;
	req.user_id      = user_id;
	req_msg.msg_type = REQUEST_JOB_USER_INFO;
	req_msg.data     = &req;

	/* With -M option, working_cluster_rec is set and  we only get
	 * information for that cluster */
	if (working_cluster_rec || !ptr || (show_flags & SHOW_LOCAL)) {
		rc = _load_cluster_jobs(&req_msg, job_info_msg_pptr,
					working_cluster_rec);
	} else {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_jobs(&req_msg, job_info_msg_pptr, show_flags,
				    slurm_conf.cluster_name, fed);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
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
slurm_load_job (job_info_msg_t **job_info_msg_pptr, uint32_t job_id,
		uint16_t show_flags)
{
	slurm_msg_t req_msg;
	job_id_msg_t req;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if ((show_flags & SHOW_LOCAL) == 0) {
		if (slurm_load_federation(&ptr) ||
		    !cluster_in_federation(ptr, slurm_conf.cluster_name)) {
			/* Not in federation */
			show_flags |= SHOW_LOCAL;
		}
	}

	memset(&req, 0, sizeof(req));
	slurm_msg_t_init(&req_msg);
	req.job_id       = job_id;
	req.show_flags   = show_flags;
	req_msg.msg_type = REQUEST_JOB_INFO_SINGLE;
	req_msg.data     = &req;

	/* With -M option, working_cluster_rec is set and  we only get
	 * information for that cluster */
	if (working_cluster_rec || !ptr || (show_flags & SHOW_LOCAL)) {
		rc = _load_cluster_jobs(&req_msg, job_info_msg_pptr,
					working_cluster_rec);
	} else {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_jobs(&req_msg, job_info_msg_pptr, show_flags,
				    slurm_conf.cluster_name, fed);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
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
			slurm_conf_get_addr(this_addr, &req_msg.address,
					    req_msg.flags);
		} else {
			this_addr = "localhost";
			slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
				       this_addr);
		}
	} else {
		char this_host[256];
		/*
		 *  Set request message address to slurmd on localhost
		 */
		gethostname_short(this_host, sizeof(this_host));
		this_addr = slurm_conf_get_nodeaddr(this_host, NULL);
		if (this_addr == NULL)
			this_addr = xstrdup("localhost");
		slurm_set_addr(&req_msg.address, slurm_conf.slurmd_port,
			       this_addr);
		xfree(this_addr);
	}

	memset(&req, 0, sizeof(req));
	req.job_pid      = job_pid;
	req_msg.msg_type = REQUEST_JOB_ID;
	req_msg.data     = &req;
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	rc = slurm_send_recv_node_msg(&req_msg, &resp_msg, 0);

	if ((rc != 0) || !resp_msg.auth_cred) {
		if (resp_msg.auth_cred)
			auth_g_destroy(resp_msg.auth_cred);
		return SLURM_ERROR;
	}
	if (resp_msg.auth_cred)
		auth_g_destroy(resp_msg.auth_cred);
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

	return SLURM_SUCCESS;
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

	memset(&job_msg, 0, sizeof(job_msg));
	job_msg.job_id     = jobid;
	req_msg.msg_type   = REQUEST_JOB_END_TIME;
	req_msg.data       = &job_msg;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
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

	memset(&msg, 0, sizeof(msg));
	msg.job_id   = job_id;
	req.msg_type = REQUEST_JOB_READY;
	req.data     = &msg;

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec) <0)
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

int slurm_job_cpus_allocated_str_on_node_id(char *cpus,
					    size_t cpus_len,
					    job_resources_t *job_resrcs_ptr,
					    int node_id)
{
	uint32_t threads = 1;
	int inx = 0;
	bitstr_t *cpu_bitmap;
	int j, k, bit_inx, bit_reps, hi;

	if (!job_resrcs_ptr || node_id < 0)
		slurm_seterrno_ret(EINVAL);

	/* find index in and first bit index in sock_core_rep_count[]
	 * for this node id */
	bit_inx = 0;
	hi = node_id + 1;    /* change from 0-origin to 1-origin */
	for (inx = 0; hi; inx++) {
		if (hi > job_resrcs_ptr->sock_core_rep_count[inx]) {
			bit_inx += job_resrcs_ptr->sockets_per_node[inx] *
				   job_resrcs_ptr->cores_per_socket[inx] *
				   job_resrcs_ptr->sock_core_rep_count[inx];
			hi -= job_resrcs_ptr->sock_core_rep_count[inx];
		} else {
			bit_inx += job_resrcs_ptr->sockets_per_node[inx] *
				   job_resrcs_ptr->cores_per_socket[inx] *
				   (hi - 1);
			break;
		}
	}

	bit_reps = job_resrcs_ptr->sockets_per_node[inx] *
		   job_resrcs_ptr->cores_per_socket[inx];

	/* get the number of threads per core on this node
	 */
	if (job_node_ptr)
		threads = job_node_ptr->node_array[node_id].threads;
	cpu_bitmap = bit_alloc(bit_reps * threads);
	for (j = 0; j < bit_reps; j++) {
		if (bit_test(job_resrcs_ptr->core_bitmap, bit_inx)){
			for (k = 0; k < threads; k++)
				bit_set(cpu_bitmap,
					(j * threads) + k);
		}
		bit_inx++;
	}
	bit_fmt(cpus, cpus_len, cpu_bitmap);
	FREE_NULL_BITMAP(cpu_bitmap);

	return SLURM_SUCCESS;
}

int slurm_job_cpus_allocated_str_on_node(char *cpus,
					 size_t cpus_len,
					 job_resources_t *job_resrcs_ptr,
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
		return SLURM_ERROR;

	return slurm_job_cpus_allocated_str_on_node_id(cpus,
						       cpus_len,
						       job_resrcs_ptr,
						       node_id);
}

/*
 * slurm_network_callerid - issue RPC to get the job id of a job from a remote
 * slurmd based upon network socket information.
 *
 * IN req - Information about network connection in question
 * OUT job_id -  ID of the job or NO_VAL
 * OUT node_name - name of the remote slurmd
 * IN node_name_size - size of the node_name buffer
 * RET SLURM_SUCCESS or SLURM_ERROR on error
 */
extern int
slurm_network_callerid (network_callerid_msg_t req, uint32_t *job_id,
	char *node_name, int node_name_size)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	network_callerid_resp_t *resp;
	slurm_addr_t addr;

	debug("slurm_network_callerid RPC: start");

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/* ip_src is the IP we want to talk to. Hopefully there's a slurmd
	 * listening there */
	memset(&addr, 0, sizeof(addr));
	addr.ss_family = req.af;

	if (addr.ss_family == AF_INET6) {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *) &addr;
		memcpy(&(in6->sin6_addr.s6_addr), req.ip_src, 16);
	        in6->sin6_port = htons(slurm_conf.slurmd_port);
	} else {
		struct sockaddr_in *in = (struct sockaddr_in *) &addr;
		memcpy(&(in->sin_addr.s_addr), req.ip_src, 4);
		in->sin_port = htons(slurm_conf.slurmd_port);
	}

	req_msg.address = addr;

	req_msg.msg_type = REQUEST_NETWORK_CALLERID;
	req_msg.data     = &req;
	slurm_msg_set_r_uid(&req_msg, SLURM_AUTH_UID_ANY);

	if (slurm_send_recv_node_msg(&req_msg, &resp_msg, 0) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
		case RESPONSE_NETWORK_CALLERID:
			resp = (network_callerid_resp_t*)resp_msg.data;
			*job_id = resp->job_id;
			strlcpy(node_name, resp->node_name, node_name_size);
			break;
		case RESPONSE_SLURM_RC:
			rc = ((return_code_msg_t *) resp_msg.data)->return_code;
			if (rc)
				slurm_seterrno_ret(rc);
			break;
		default:
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
			break;
	}

	slurm_free_network_callerid_msg(resp_msg.data);
	return SLURM_SUCCESS;
}

static int
_load_cluster_job_prio(slurm_msg_t *req_msg,
		       priority_factors_response_msg_t **factors_resp,
		       slurmdb_cluster_rec_t *cluster)
{
	slurm_msg_t resp_msg;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&resp_msg);

	if (slurm_send_recv_controller_msg(req_msg, &resp_msg, cluster) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_PRIORITY_FACTORS:
		*factors_resp =
			(priority_factors_response_msg_t *) resp_msg.data;
		resp_msg.data = NULL;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		break;
	default:
		rc = SLURM_UNEXPECTED_MSG_ERROR;
		break;
	}
	if (rc)
		slurm_seterrno(rc);

	return rc;
}

/* Sort responses so local cluster response is first */
static int _local_resp_first_prio(void *x, void *y)
{
	load_job_prio_resp_struct_t *resp_x = *(load_job_prio_resp_struct_t **)x;
	load_job_prio_resp_struct_t *resp_y = *(load_job_prio_resp_struct_t **)y;

	if (resp_x->local_cluster)
		return -1;
	if (resp_y->local_cluster)
		return 1;
	return 0;
}

/* Add cluster_name to job priority info records */
static void _add_cluster_name(priority_factors_response_msg_t *new_msg,
			      char *cluster_name)
{
	priority_factors_object_t *prio_obj;
	ListIterator iter;

	if (!new_msg || !new_msg->priority_factors_list)
		return;

	iter = list_iterator_create(new_msg->priority_factors_list);
	while ((prio_obj = (priority_factors_object_t *) list_next(iter)))
		prio_obj->cluster_name = xstrdup(cluster_name);
	list_iterator_destroy(iter);
}

/* Thread to read job priority factor information from some cluster */
static void *_load_job_prio_thread(void *args)
{
	load_job_req_struct_t *load_args = (load_job_req_struct_t *) args;
	slurmdb_cluster_rec_t *cluster = load_args->cluster;
	priority_factors_response_msg_t *new_msg = NULL;
	int rc;

	if ((rc = _load_cluster_job_prio(load_args->req_msg, &new_msg,
					 cluster)) || !new_msg) {
		verbose("Error reading job information from cluster %s: %s",
			cluster->name, slurm_strerror(rc));
	} else {
		load_job_prio_resp_struct_t *job_resp;
		_add_cluster_name(new_msg, cluster->name);


		job_resp = xmalloc(sizeof(load_job_prio_resp_struct_t));
		job_resp->local_cluster = load_args->local_cluster;
		job_resp->new_msg = new_msg;
		list_append(load_args->resp_msg_list, job_resp);
	}
	xfree(args);

	return (void *) NULL;
}

static int _load_fed_job_prio(slurm_msg_t *req_msg,
			      priority_factors_response_msg_t **factors_resp,
			      uint16_t show_flags, char *cluster_name,
			      slurmdb_federation_rec_t *fed)
{
	int i, j;
	int local_job_cnt = 0;
	load_job_prio_resp_struct_t *job_resp;
	priority_factors_response_msg_t *orig_msg = NULL, *new_msg = NULL;
	priority_factors_object_t *prio_obj;
	uint32_t hash_job_inx, *hash_tbl_size = NULL, **hash_job_id = NULL;
	uint32_t hash_part_inx, **hash_part_id = NULL;
	slurmdb_cluster_rec_t *cluster;
	ListIterator iter;
	int pthread_count = 0;
	pthread_t *load_thread = 0;
	load_job_req_struct_t *load_args;
	List resp_msg_list;

	*factors_resp = NULL;

	/* Spawn one pthread per cluster to collect job information */
	resp_msg_list = list_create(NULL);
	load_thread = xmalloc(sizeof(pthread_t) *
			      list_count(fed->cluster_list));
	iter = list_iterator_create(fed->cluster_list);
	while ((cluster = (slurmdb_cluster_rec_t *) list_next(iter))) {
		bool local_cluster = false;
		if ((cluster->control_host == NULL) ||
		    (cluster->control_host[0] == '\0'))
			continue;	/* Cluster down */

		if (!xstrcmp(cluster->name, cluster_name))
			local_cluster = true;
		if ((show_flags & SHOW_LOCAL) && !local_cluster)
			continue;

		load_args = xmalloc(sizeof(load_job_req_struct_t));
		load_args->cluster = cluster;
		load_args->local_cluster = local_cluster;
		load_args->req_msg = req_msg;
		load_args->resp_msg_list = resp_msg_list;
		slurm_thread_create(&load_thread[pthread_count],
				    _load_job_prio_thread, load_args);
		pthread_count++;

	}
	list_iterator_destroy(iter);

	/* Wait for all pthreads to complete */
	for (i = 0; i < pthread_count; i++)
		pthread_join(load_thread[i], NULL);
	xfree(load_thread);

	/* Move the response from the local cluster (if any) to top of list */
	list_sort(resp_msg_list, _local_resp_first_prio);

	/* Merge the responses into a single response message */
	iter = list_iterator_create(resp_msg_list);
	while ((job_resp = (load_job_prio_resp_struct_t *) list_next(iter))) {
		new_msg = job_resp->new_msg;
		if (!new_msg->priority_factors_list) {
			/* Just skip this one. */
		} else if (!orig_msg) {
			orig_msg = new_msg;
			if (job_resp->local_cluster) {
				local_job_cnt = list_count(
						new_msg->priority_factors_list);
			}
			*factors_resp = orig_msg;
		} else {
			/* Merge prio records into a single response message */
			list_transfer(orig_msg->priority_factors_list,
				      new_msg->priority_factors_list);
			FREE_NULL_LIST(new_msg->priority_factors_list);
			xfree(new_msg);
		}
		xfree(job_resp);
	}
	list_iterator_destroy(iter);
	FREE_NULL_LIST(resp_msg_list);

	/* In a single cluster scenario with no jobs, the priority_factors_list
	 * will be NULL. sprio will handle this above. If the user requests
	 * specific jobids it will give the corresponding error otherwise the
	 * header will be printed and no jobs will be printed out. */
	if (!*factors_resp) {
		*factors_resp =
			xmalloc(sizeof(priority_factors_response_msg_t));
		return SLURM_SUCCESS;
	}

	/* Find duplicate job records and jobs local to other clusters and set
	 * their job_id == 0 so they get skipped in reporting */
	if ((show_flags & SHOW_SIBLING) == 0) {
		hash_tbl_size = xmalloc(sizeof(uint32_t)   * JOB_HASH_SIZE);
		hash_job_id   = xmalloc(sizeof(uint32_t *) * JOB_HASH_SIZE);
		hash_part_id  = xmalloc(sizeof(uint32_t *) * JOB_HASH_SIZE);
		for (i = 0; i < JOB_HASH_SIZE; i++) {
			hash_tbl_size[i] = 100;
			hash_job_id[i]  = xmalloc(sizeof(uint32_t) *
						  hash_tbl_size[i]);
			hash_part_id[i] = xmalloc(sizeof(uint32_t) *
						  hash_tbl_size[i]);
		}
	}
	iter = list_iterator_create(orig_msg->priority_factors_list);
	i = 0;
	while ((prio_obj = (priority_factors_object_t *) list_next(iter))) {
		bool found_job = false, local_cluster = false;
		if (i++ < local_job_cnt) {
			local_cluster = true;
		} else if (_test_local_job(prio_obj->job_id)) {
			list_delete_item(iter);
			continue;
		}

		if (show_flags & SHOW_SIBLING)
			continue;
		hash_job_inx = prio_obj->job_id % JOB_HASH_SIZE;
		if (prio_obj->partition) {
			HASH_FCN(prio_obj->partition,
				 strlen(prio_obj->partition), hash_part_inx);
		} else {
			hash_part_inx = 0;
		}
		for (j = 0;
		     ((j < hash_tbl_size[hash_job_inx]) &&
		      hash_job_id[hash_job_inx][j]); j++) {
			if ((prio_obj->job_id ==
			     hash_job_id[hash_job_inx][j]) &&
			    (hash_part_inx == hash_part_id[hash_job_inx][j])) {
				found_job = true;
				break;
			}
		}
		if (found_job && local_cluster) {
			/* Local job in multiple partitions */
			continue;
		} if (found_job) {
			/* Duplicate remote job,
			 * possible in multiple partitions */
			list_delete_item(iter);
			continue;
		} else if (j >= hash_tbl_size[hash_job_inx]) {
			hash_tbl_size[hash_job_inx] *= 2;
			xrealloc(hash_job_id[hash_job_inx],
				 sizeof(uint32_t) * hash_tbl_size[hash_job_inx]);
		}
		hash_job_id[hash_job_inx][j]  = prio_obj->job_id;
		hash_part_id[hash_job_inx][j] = hash_part_inx;
	}
	list_iterator_destroy(iter);
	if ((show_flags & SHOW_SIBLING) == 0) {
		for (i = 0; i < JOB_HASH_SIZE; i++) {
			xfree(hash_job_id[i]);
			xfree(hash_part_id[i]);
		}
		xfree(hash_tbl_size);
		xfree(hash_job_id);
		xfree(hash_part_id);
	}

	return SLURM_SUCCESS;
}

/*
 * slurm_load_job_prio - issue RPC to get job priority information for
 *	jobs which pass filter test
 * OUT factors_resp - job priority factors
 * IN job_id_list - list of job IDs to be reported
 * IN partitions - comma delimited list of partition names to be reported
 * IN uid_list - list of user IDs to be reported
 * IN show_flags -  job filtering option: 0, SHOW_LOCAL and/or SHOW_SIBLING
 * RET 0 or -1 on error
 * NOTE: free the response using slurm_free_priority_factors_response_msg()
 */
extern int
slurm_load_job_prio(priority_factors_response_msg_t **factors_resp,
		    List job_id_list, char *partitions, List uid_list,
		    uint16_t show_flags)
{
	slurm_msg_t req_msg;
	priority_factors_request_msg_t factors_req;
	void *ptr = NULL;
	slurmdb_federation_rec_t *fed;
	int rc;

	if ((show_flags & SHOW_FEDERATION) && !(show_flags & SHOW_LOCAL) &&
	    (slurm_load_federation(&ptr) == SLURM_SUCCESS) &&
	    cluster_in_federation(ptr, slurm_conf.cluster_name)) {
		/* In federation. Need full info from all clusters */
		show_flags &= (~SHOW_LOCAL);
	} else {
		/* Report local cluster info only */
		show_flags |= SHOW_LOCAL;
		show_flags &= (~SHOW_FEDERATION);
	}

	memset(&factors_req, 0, sizeof(factors_req));
	factors_req.job_id_list = job_id_list;
	factors_req.partitions  = partitions;
	factors_req.uid_list    = uid_list;

	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_PRIORITY_FACTORS;
	req_msg.data     = &factors_req;

	/* With -M option, working_cluster_rec is set and  we only get
	 * information for that cluster */
	if (show_flags & SHOW_FEDERATION) {
		fed = (slurmdb_federation_rec_t *) ptr;
		rc = _load_fed_job_prio(&req_msg, factors_resp, show_flags,
					slurm_conf.cluster_name, fed);
	} else {
		rc = _load_cluster_job_prio(&req_msg, factors_resp,
					    working_cluster_rec);
	}

	if (ptr)
		slurm_destroy_federation_rec(ptr);

	return rc;
}
