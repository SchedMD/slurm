/*****************************************************************************\
 *  job_will_run.c - Process Wiki job will_run test
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "./msg.h"
#include "src/common/node_select.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

#define MAX_JOB_QUEUE 20

static char *	_will_run_test(uint32_t jobid, time_t start_time,
			       char *node_list, int *err_code, char **err_msg);
static char *	_will_run_test2(uint32_t jobid, time_t start_time,
				char *node_list,
				uint32_t *preemptee, int preemptee_cnt,
				int *err_code, char **err_msg);
/*
 * job_will_run - Determine if, when and where a priority ordered list of jobs
 *		  can be initiated with the currently running jobs as a
 *		  backgorund
 * cmd_ptr IN   - CMD=JOBWILLRUN ARG=JOBID=<JOBID>[@<TIME>],<AVAIL_NODES>
 * err_code OUT - 0 on success or some error code
 * err_msg OUT  - error message if any of the specified jobs can not be started
 *		  at the specified time (if given) on the available nodes.
 *		  Otherwise information on when and where the pending jobs
 *		  will be initiated
 *                ARG=<JOBID>:<PROCS>@<TIME>,<USED_NODES>
 * NOTE: xfree() err_msg if err_code is zero
 * RET 0 on success, -1 on failure
 */
extern int	job_will_run(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *buf, *tmp_buf, *tmp_char;
	uint32_t jobid;
	time_t start_time;
	char *avail_nodes;
	/* Locks: write job, read node and partition info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "JOBWILLRUN lacks ARG";
		error("wiki: JOBWILLRUN lacks ARG");
		return -1;
	}
	arg_ptr += 4;

	if (strncmp(arg_ptr, "JOBID=", 6)) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: JOBWILLRUN has invalid ARG value");
		return -1;
	}
	arg_ptr += 6;
	jobid = strtoul(arg_ptr, &tmp_char, 10);
	if (tmp_char[0] == '@')
		start_time = strtoul(tmp_char+1, &tmp_char, 10);
	else
		start_time = time(NULL);
	if (tmp_char[0] != ',') {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: JOBWILLRUN has invalid ARG value");
		return -1;
	}
	avail_nodes = tmp_char + 1;

	lock_slurmctld(job_write_lock);
	buf = _will_run_test(jobid, start_time, avail_nodes,
			     err_code, err_msg);
	unlock_slurmctld(job_write_lock);

	if (!buf)
		return -1;

	tmp_buf = xmalloc(strlen(buf) + 32);
	sprintf(tmp_buf, "SC=0 ARG=%s", buf);
	xfree(buf);
	*err_code = 0;
	*err_msg = tmp_buf;
	return 0;
}

static char *	_will_run_test(uint32_t jobid, time_t start_time,
			       char *node_list, int *err_code, char **err_msg)
{
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr;
	bitstr_t *avail_bitmap = NULL, *resv_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL;
	char *hostlist, *reply_msg = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	int rc;
	time_t start_res, orig_start_time;
	List preemptee_candidates;

	debug2("wiki2: will_run job_id=%u start_time=%u node_list=%s",
		jobid, (uint32_t)start_time, node_list);

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = -700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		return NULL;
	}
	if ((job_ptr->details == NULL) || (!IS_JOB_PENDING(job_ptr))) {
		*err_code = -700;
		*err_msg = "WillRun not applicable to non-pending job";
		error("wiki: WillRun on non-pending job %u", jobid);
		return NULL;
	}

	part_ptr = job_ptr->part_ptr;
	if (part_ptr == NULL) {
		*err_code = -700;
		*err_msg = "Job lacks a partition";
		error("wiki: Job %u lacks a partition", jobid);
		return NULL;
	}

	if ((node_list == NULL) || (node_list[0] == '\0')) {
		/* assume all nodes available to job for testing */
		avail_bitmap = bit_copy(avail_node_bitmap);
	} else if (node_name2bitmap(node_list, false, &avail_bitmap) != 0) {
		*err_code = -700;
		*err_msg = "Invalid available nodes value";
		error("wiki: Attempt to set invalid available node "
		      "list for job %u, %s", jobid, node_list);
		return NULL;
	}

	/* Enforce reservation: access control, time and nodes */
	start_res = start_time;
	rc = job_test_resv(job_ptr, &start_res, true, &resv_bitmap,
			   &exc_core_bitmap);
	if (rc != SLURM_SUCCESS) {
		*err_code = -730;
		*err_msg = "Job denied access to reservation";
		error("wiki: reservation access denied for job %u", jobid);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}
	start_time = MAX(start_time, start_res);
	bit_and(avail_bitmap, resv_bitmap);
	FREE_NULL_BITMAP(resv_bitmap);

	/* Only consider nodes that are not DOWN or DRAINED */
	bit_and(avail_bitmap, avail_node_bitmap);

	/* Consider only nodes in this job's partition */
	if (part_ptr->node_bitmap)
		bit_and(avail_bitmap, part_ptr->node_bitmap);
	else {
		*err_code = -730;
		*err_msg = "Job's partition has no nodes";
		error("wiki: no nodes in partition %s for job %u",
			part_ptr->name, jobid);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}

	if (job_req_node_filter(job_ptr, avail_bitmap) != SLURM_SUCCESS) {
		/* Job probably has invalid feature list */
		*err_code = -730;
		*err_msg = "Job's required features not available "
			   "on selected nodes";
		error("wiki: job %u not runnable on hosts=%s",
		      jobid, node_list);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}
	if (job_ptr->details->exc_node_bitmap) {
		bit_not(job_ptr->details->exc_node_bitmap);
		bit_and(avail_bitmap, job_ptr->details->exc_node_bitmap);
		bit_not(job_ptr->details->exc_node_bitmap);
	}
	if ((job_ptr->details->req_node_bitmap) &&
	    (!bit_super_set(job_ptr->details->req_node_bitmap,
			    avail_bitmap))) {
		*err_code = -730;
		*err_msg = "Job's required nodes not available";
		error("wiki: job %u not runnable on hosts=%s",
		      jobid, node_list);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}

	min_nodes = MAX(job_ptr->details->min_nodes, part_ptr->min_nodes);
	if (job_ptr->details->max_nodes == 0)
		max_nodes = part_ptr->max_nodes;
	else
		max_nodes = MIN(job_ptr->details->max_nodes,
				part_ptr->max_nodes);
	max_nodes = MIN(max_nodes, 500000); /* prevent overflows */
	if (job_ptr->details->max_nodes)
		req_nodes = max_nodes;
	else
		req_nodes = min_nodes;
	if (min_nodes > max_nodes) {
		/* job's min_nodes exceeds partitions max_nodes */
		*err_code = -730;
		*err_msg = "Job's min_nodes > max_nodes";
		error("wiki: job %u not runnable on hosts=%s",
		      jobid, node_list);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}

	preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);

	orig_start_time = job_ptr->start_time;
	rc = select_g_job_test(job_ptr, avail_bitmap,
			       min_nodes, max_nodes, req_nodes,
			       SELECT_MODE_WILL_RUN,
			       preemptee_candidates, NULL, exc_core_bitmap);
	if (preemptee_candidates)
		list_destroy(preemptee_candidates);

	if (rc == SLURM_SUCCESS) {
		char tmp_str[128];
		*err_code = 0;
		uint32_t proc_cnt = 0;

		xstrcat(reply_msg, "STARTINFO=");
#ifdef HAVE_BG
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
                             		    SELECT_JOBDATA_NODE_CNT,
					    &proc_cnt);

#else
		proc_cnt = job_ptr->total_cpus;
#endif
		snprintf(tmp_str, sizeof(tmp_str), "%u:%u@%u,",
			 jobid, proc_cnt, (uint32_t) job_ptr->start_time);
		xstrcat(reply_msg, tmp_str);
		hostlist = bitmap2node_name(avail_bitmap);
		xstrcat(reply_msg, hostlist);
		xfree(hostlist);
	} else {
		xstrcat(reply_msg, "Jobs not runable on selected nodes");
		error("wiki: jobs not runnable on nodes");
	}

	/* Restore pending job's expected start time */
	job_ptr->start_time = orig_start_time;
	FREE_NULL_BITMAP(avail_bitmap);
	FREE_NULL_BITMAP(exc_core_bitmap);
	return reply_msg;
}

/*
 * job_will_run2 - Determine if, when and where a pending job can be
 *		   initiated with the currently running jobs either preempted
 *		   or left running as on other resources
 * cmd_ptr IN   - CMD=JOBWILLRUN ARG=<JOBID> [STARTTIME=<TIME>]
 *		  NODES=<AVAIL_NODES> [PREEMPT=<JOBID1>[,<JOBID2> ..]]
 * err_code OUT - 0 on success or some error code
 * err_msg OUT  - error message if any of the specified jobs can not be started
 *		  at the specified time (if given) on the available nodes.
 *		  Otherwise information on when and where the pending jobs
 *		  will be initiated
 *                ARG=<JOBID> TASKS=<CPU_COUNT> STARTTIME=<TIME>
 *			NODES=<USED_NODES> [PREEMPT=<JOBID1>[,<JOBID2> ..]]
 * NOTE: xfree() err_msg if err_code is zero
 * RET 0 on success, -1 on failure
 */
extern int	job_will_run2(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *buf, *tmp_buf, *tmp_char;
	int preemptee_cnt = 0;
	uint32_t jobid, *preemptee = NULL, tmp_id;
	time_t start_time;
	char *avail_nodes = NULL;
	/* Locks: write job, read node and partition info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "JOBWILLRUN lacks ARG";
		error("wiki: JOBWILLRUN lacks ARG");
		return -1;
	}
	arg_ptr += 4;

	jobid = strtoul(arg_ptr, &tmp_char, 10);
	if ((tmp_char[0] != ' ') && (tmp_char[0] != '\0')) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: JOBWILLRUN has invalid ARG value");
		return -1;
	}

	arg_ptr = strstr(cmd_ptr, "STARTTIME=");
	if (arg_ptr) {
		arg_ptr += 10;
		start_time = strtoul(arg_ptr, &tmp_char, 10);
		if ((tmp_char[0] != ' ') && (tmp_char[0] != '\0')) {
			*err_code = -300;
			*err_msg = "Invalid STARTTIME value";
			error("wiki: JOBWILLRUN has invalid STARTTIME value");
			return -1;
		}
	} else {
		start_time = time(NULL);
	}

	arg_ptr = strstr(cmd_ptr, "PREEMPT=");
	if (arg_ptr) {
		arg_ptr += 8;
		preemptee = xmalloc(sizeof(uint32_t) * strlen(arg_ptr));
		while (1) {
			tmp_id = strtoul(arg_ptr, &tmp_char, 10);
			if ((tmp_char[0] != ' ') && (tmp_char[0] != '\0') &&
			    (tmp_char[0] != ',')) {
				*err_code = -300;
				*err_msg = "Invalid PREEMPT value";
				error("wiki: JOBWILLRUN has invalid PREEMPT "
				      "value");
				xfree(preemptee);
				xfree(avail_nodes);
				return -1;
			}
			preemptee[preemptee_cnt++] = tmp_id;
			if (tmp_char[0] != ',')
				break;
			arg_ptr = tmp_char + 1;
		}
	}

	/* Keep this last, since we modify the input string */
	arg_ptr = strstr(cmd_ptr, "NODES=");
	if (arg_ptr) {
		arg_ptr += 6;
		avail_nodes = xstrdup(arg_ptr);
		arg_ptr = strchr(avail_nodes, ' ');
		if (arg_ptr)
			arg_ptr[0] = '\0';
	} else {
		*err_code = -300;
		*err_msg = "Missing NODES value";
		error("wiki: JOBWILLRUN lacks NODES value");
		xfree(preemptee);
		return -1;
	}

	lock_slurmctld(job_write_lock);
	buf = _will_run_test2(jobid, start_time, avail_nodes,
			      preemptee, preemptee_cnt,
			      err_code, err_msg);
	unlock_slurmctld(job_write_lock);

	xfree(preemptee);
	xfree(avail_nodes);
	if (!buf)
		return -1;

	tmp_buf = xmalloc(strlen(buf) + 32);
	sprintf(tmp_buf, "SC=0 ARG=%s", buf);
	xfree(buf);
	*err_code = 0;
	*err_msg = tmp_buf;
	return 0;
}

static char *	_will_run_test2(uint32_t jobid, time_t start_time,
				char *node_list,
				uint32_t *preemptee, int preemptee_cnt,
				int *err_code, char **err_msg)
{
	struct job_record *job_ptr = NULL, *pre_ptr;
	struct part_record *part_ptr;
	bitstr_t *avail_bitmap = NULL, *resv_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL;
	time_t start_res;
	uint32_t min_nodes, max_nodes, req_nodes;
	List preemptee_candidates = NULL, preempted_jobs = NULL;
	time_t orig_start_time;
	char *reply_msg = NULL;
	int i, rc;

	xassert(node_list);
	debug2("wiki2: will_run2 job_id=%u start_time=%u node_list=%s",
		jobid, (uint32_t)start_time, node_list);

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = -700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		return NULL;
	}
	if ((job_ptr->details == NULL) || (!IS_JOB_PENDING(job_ptr))) {
		*err_code = -700;
		*err_msg = "WillRun not applicable to non-pending job";
		error("wiki: WillRun on non-pending job %u", jobid);
		return NULL;
	}

	part_ptr = job_ptr->part_ptr;
	if (part_ptr == NULL) {
		*err_code = -700;
		*err_msg = "Job lacks a partition";
		error("wiki: Job %u lacks a partition", jobid);
		return NULL;
	}

	if (node_name2bitmap(node_list, false, &avail_bitmap) != 0) {
		*err_code = -700;
		*err_msg = "Invalid available nodes value";
		error("wiki: Attempt to set invalid available node "
		      "list for job %u, %s", jobid, node_list);
		return NULL;
	}

	/* Enforce reservation: access control, time and nodes */
	start_res = start_time;
	rc = job_test_resv(job_ptr, &start_res, true, &resv_bitmap,
			   &exc_core_bitmap);
	if (rc != SLURM_SUCCESS) {
		*err_code = -730;
		*err_msg = "Job denied access to reservation";
		error("wiki: reservation access denied for job %u", jobid);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}
	start_time = MAX(start_time, start_res);
	bit_and(avail_bitmap, resv_bitmap);
	FREE_NULL_BITMAP(resv_bitmap);

	/* Only consider nodes that are not DOWN or DRAINED */
	bit_and(avail_bitmap, avail_node_bitmap);

	/* Consider only nodes in this job's partition */
	if (part_ptr->node_bitmap)
		bit_and(avail_bitmap, part_ptr->node_bitmap);
	else {
		*err_code = -730;
		*err_msg = "Job's partition has no nodes";
		error("wiki: no nodes in partition %s for job %u",
			part_ptr->name, jobid);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}

	if (job_req_node_filter(job_ptr, avail_bitmap) != SLURM_SUCCESS) {
		/* Job probably has invalid feature list */
		*err_code = -730;
		*err_msg = "Job's required features not available "
			   "on selected nodes";
		error("wiki: job %u not runnable on hosts=%s",
			jobid, node_list);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}
	if (job_ptr->details->exc_node_bitmap) {
		bit_not(job_ptr->details->exc_node_bitmap);
		bit_and(avail_bitmap, job_ptr->details->exc_node_bitmap);
		bit_not(job_ptr->details->exc_node_bitmap);
	}
	if ((job_ptr->details->req_node_bitmap) &&
	    (!bit_super_set(job_ptr->details->req_node_bitmap,
			    avail_bitmap))) {
		*err_code = -730;
		*err_msg = "Job's required nodes not available";
		error("wiki: job %u not runnable on hosts=%s",
			jobid, node_list);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}

	min_nodes = MAX(job_ptr->details->min_nodes, part_ptr->min_nodes);
	if (job_ptr->details->max_nodes == 0)
		max_nodes = part_ptr->max_nodes;
	else
		max_nodes = MIN(job_ptr->details->max_nodes,
				part_ptr->max_nodes);
	max_nodes = MIN(max_nodes, 500000); /* prevent overflows */
	if (job_ptr->details->max_nodes)
		req_nodes = max_nodes;
	else
		req_nodes = min_nodes;
	if (min_nodes > max_nodes) {
		/* job's min_nodes exceeds partitions max_nodes */
		*err_code = -730;
		*err_msg = "Job's min_nodes > max_nodes";
		error("wiki: job %u not runnable on hosts=%s",
			jobid, node_list);
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return NULL;
	}

	if (preemptee_cnt) {
		preemptee_candidates = list_create(NULL);
		for (i=0; i<preemptee_cnt; i++) {
			if ((pre_ptr = find_job_record(preemptee[i])))
				list_append(preemptee_candidates, pre_ptr);
		}
	}

	orig_start_time = job_ptr->start_time;
	rc = select_g_job_test(job_ptr, avail_bitmap, min_nodes, max_nodes,
			       req_nodes, SELECT_MODE_WILL_RUN,
			       preemptee_candidates, &preempted_jobs,
			       exc_core_bitmap);
	if (preemptee_candidates)
		list_destroy(preemptee_candidates);

	if (rc == SLURM_SUCCESS) {
		char *hostlist, *sep, tmp_str[128];
		uint32_t pre_cnt = 0, proc_cnt = 0;

#ifdef HAVE_BG
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				     SELECT_JOBDATA_NODE_CNT, &proc_cnt);
#else
		proc_cnt = job_ptr->total_cpus;
#endif
		snprintf(tmp_str, sizeof(tmp_str),
			 "STARTINFO=%u TASKS=%u STARTTIME=%u NODES=",
			 job_ptr->job_id, proc_cnt,
			 (uint32_t) job_ptr->start_time);
		xstrcat(reply_msg, tmp_str);
		hostlist = bitmap2node_name(avail_bitmap);
		xstrcat(reply_msg, hostlist);
		xfree(hostlist);

		if (preempted_jobs) {
			while ((pre_ptr = list_pop(preempted_jobs))) {
				if (pre_cnt++)
					sep = ",";
				else
					sep = " PREEMPT=";
				snprintf(tmp_str, sizeof(tmp_str), "%s%u",
					 sep, pre_ptr->job_id);
				xstrcat(reply_msg, tmp_str);
			}
			list_destroy(preempted_jobs);
		}
	} else {
		xstrcat(reply_msg, "Jobs not runable on selected nodes");
		error("wiki: jobs not runnable on nodes");
	}

	/* Restore pending job's expected start time */
	job_ptr->start_time = orig_start_time;

	FREE_NULL_BITMAP(avail_bitmap);
	FREE_NULL_BITMAP(exc_core_bitmap);
	return reply_msg;
}

/*
 * bitmap2wiki_node_name  - given a bitmap, build a list of colon separated
 *	node names (if we can't use node range expressions), or the
 *	normal slurm node name expression
 *
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the returned pointer when no longer required
 */
extern char *	bitmap2wiki_node_name(bitstr_t *bitmap)
{
	int i;
	char *buf = NULL;

	if (use_host_exp)
		return bitmap2node_name(bitmap);

	if (bitmap == NULL)
		return xstrdup("");

	for (i = 0; i < node_record_count; i++) {
		if (bit_test (bitmap, i) == 0)
			continue;
		if (buf)
			xstrcat(buf, ":");
		xstrcat(buf, node_record_table_ptr[i].name);
	}
	return buf;
}
