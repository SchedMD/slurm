/*****************************************************************************\
 *  job_will_run.c - Process Wiki job will_run test
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

#define MAX_JOB_QUEUE 20

static void     _select_list_del(void *x);
static char *	_will_run_test(uint32_t *jobid, time_t *start_time, 
			       char **node_list, int job_cnt, 
			       int *err_code, char **err_msg);

/*
 * job_will_run - Determine if, when and where a priority ordered list of jobs
 *		  can be initiated with the currently running jobs as a 
 *		  backgorund
 * cmd_ptr IN   - CMD=JOBWILLRUN ARG=JOBID=<JOBID>[@<TIME>],<AVAIL_NODES>
 *		   [JOBID=<JOBID>[@<TIME>],<AVAIL_NODES>]...
 * err_code OUT - 0 on success or some error code
 * err_msg OUT  - error message if any of the specified jobs can not be started
 *		  at the specified time (if given) on the available nodes. 
 *		  Otherwise information on when and where the pending jobs
 *		  will be initiated
 *                ARG=<JOBID>:<PROCS>@<TIME>,<USED_NODES>
 *		     [<JOBID>:<PROCS>@<TIME>,<USED_NODES>]
 * NOTE: xfree() err_msg if err_code is zero
 * RET 0 on success, -1 on failure
 */
extern int	job_will_run(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *buf, *tmp_buf, *tmp_char;
	int job_cnt;
	uint32_t jobid[MAX_JOB_QUEUE];
	time_t start_time[MAX_JOB_QUEUE];
	char *avail_nodes[MAX_JOB_QUEUE];
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

	for (job_cnt=0; job_cnt<MAX_JOB_QUEUE; ) {
		if (strncmp(arg_ptr, "JOBID=", 6)) {
			*err_code = -300;
			*err_msg = "Invalid ARG value";
			error("wiki: JOBWILLRUN has invalid ARG value");
			return -1;
		}
		arg_ptr += 6;
		jobid[job_cnt] = strtoul(arg_ptr, &tmp_char, 10);
		if (tmp_char[0] == '@')
			start_time[job_cnt] = strtoul(tmp_char+1, &tmp_char,
						      10);
		else
			start_time[job_cnt] = 0;
		if (tmp_char[0] != ',') {
			*err_code = -300;
			*err_msg = "Invalid ARG value";
			error("wiki: JOBWILLRUN has invalid ARG value");
			return -1;
		}
		avail_nodes[job_cnt] = tmp_char + 1;
		job_cnt++;

		while (tmp_char[0] && (!isspace(tmp_char[0])))
			tmp_char++;
		if (tmp_char[0] == '\0')
			break;
		tmp_char[0] = '\0';	/* was space */
		tmp_char++;
		while (isspace(tmp_char[0]))
			tmp_char++;
		if (tmp_char[0] == '\0')
			break;
		arg_ptr = tmp_char;
	}

	lock_slurmctld(job_write_lock);
	buf = _will_run_test(jobid, start_time, avail_nodes, job_cnt, 
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

static void _select_list_del (void *x)
{
	select_will_run_t *select_will_run = (select_will_run_t *) x;
	FREE_NULL_BITMAP(select_will_run->avail_nodes);
	xfree(select_will_run);
}

static char *	_will_run_test(uint32_t *jobid, time_t *start_time, 
			       char **node_list, int job_cnt, 
			       int *err_code, char **err_msg)
{
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bitstr_t *avail_bitmap = NULL, *resv_bitmap = NULL;
	char *hostlist, *reply_msg = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	int i, rc;
	select_will_run_t *select_will_run = NULL;
	List select_list;
	ListIterator iter;
	time_t now = time(NULL), when;

	select_list = list_create(_select_list_del);
	if (select_list == NULL)
		fatal("list_create: malloc failure");

	for (i=0; i<job_cnt; i++) {
		debug2("wiki2: will_run job_id=%u start_time=%u node_list=%s",
			jobid[i], start_time[i], node_list[i]);
		job_ptr = find_job_record(jobid[i]);
		if (job_ptr == NULL) {
			*err_code = -700;
			*err_msg = "No such job";
			error("wiki: Failed to find job %u", jobid[i]);
			break;
		}
		if (job_ptr->job_state != JOB_PENDING) {
			*err_code = -700;
			*err_msg = "WillRun not applicable to non-pending job";
			error("wiki: WillRun on non-pending job %u", jobid[i]);
			break;
		}

		part_ptr = job_ptr->part_ptr;
		if (part_ptr == NULL) {
			*err_code = -700;
			*err_msg = "Job lacks a partition";
			error("wiki: Job %u lacks a partition", jobid[i]);
			break;
		}

		if ((job_ptr->details == NULL) ||
		    (job_ptr->job_state != JOB_PENDING)) {
			*err_code = -700;
			*err_msg = "Job not pending, can't test  will_run";
			error("wiki: Attempt to test will_run of non-pending "
			      "job %u", jobid[i]);
			break;
		}

		if ((node_list[i] == NULL) || (node_list[i][0] == '\0')) {
			/* assume all nodes available to job for testing */
			avail_bitmap = bit_copy(avail_node_bitmap);
		} else if (node_name2bitmap(node_list[i], false, 
					    &avail_bitmap) != 0) {
			*err_code = -700;
			*err_msg = "Invalid available nodes value";
			error("wiki: Attempt to set invalid available node "
			      "list for job %u, %s", jobid[i], node_list[i]);
			break;
		}

		/* Enforce reservation: access control, time and nodes */
		if (start_time[i])
			when = start_time[i];
		else
			when = now;
		rc = job_test_resv(job_ptr, &when, &resv_bitmap);
		if (when > now)
			start_time[i] = when;
		if (rc != SLURM_SUCCESS) {
			*err_code = -730;
			*err_msg = "Job denied access to reservation";
			error("wiki: reservation access denied for job %u", 
			      jobid[i]);
			break;
		}
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
				part_ptr->name, jobid[i]);
			break;
		}

		if (job_req_node_filter(job_ptr, avail_bitmap) != 
		    SLURM_SUCCESS) {
			/* Job probably has invalid feature list */
			*err_code = -730;
			*err_msg = "Job's required features not available "
				   "on selected nodes";
			error("wiki: job %u not runnable on hosts=%s", 
				jobid[i], node_list[i]);
			break;
		}
		if (job_ptr->details->exc_node_bitmap) {
			bit_not(job_ptr->details->exc_node_bitmap);
			bit_and(avail_bitmap, 
				job_ptr->details->exc_node_bitmap);
			bit_not(job_ptr->details->exc_node_bitmap);
		}
		if ((job_ptr->details->req_node_bitmap) &&
		    (!bit_super_set(job_ptr->details->req_node_bitmap, 
				    avail_bitmap))) {
			*err_code = -730;
			*err_msg = "Job's required nodes not available";
			error("wiki: job %u not runnable on hosts=%s", 
				jobid[i], node_list[i]);
			break;
		}

		min_nodes = MAX(job_ptr->details->min_nodes, 
				part_ptr->min_nodes);
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
				jobid[i], node_list[i]);
			break;
		}
		select_will_run = xmalloc(sizeof(select_will_run_t));
		select_will_run->avail_nodes = avail_bitmap;
		avail_bitmap = NULL;
		select_will_run->job_ptr     = job_ptr;
		job_ptr->start_time          = start_time[i];
		select_will_run->max_nodes   = max_nodes;
		select_will_run->min_nodes   = min_nodes;
		select_will_run->req_nodes   = req_nodes;
		list_push(select_list, select_will_run); 
	}
	FREE_NULL_BITMAP(avail_bitmap);
	if (i < job_cnt) {	/* error logged above */
		/* Restore pending job start time */
		iter = list_iterator_create(select_list);
		if (iter == NULL)
			fatal("list_iterator_create: malloc failure");
		while ((select_will_run = list_next(iter)))
			select_will_run->job_ptr->start_time = 0;
		list_iterator_destroy(iter);
		list_destroy(select_list);
		return NULL;
	}

	if (job_cnt == 1) {
		rc = select_g_job_test(
				select_will_run->job_ptr, 
				select_will_run->avail_nodes,
				select_will_run->min_nodes, 
				select_will_run->max_nodes, 
				select_will_run->req_nodes, 
				SELECT_MODE_WILL_RUN);
	} else {
		rc = select_g_job_list_test(select_list);
	}

	if (rc == SLURM_SUCCESS) {
		char tmp_str[128];
		*err_code = 0;
		uint32_t proc_cnt = 0;

		iter = list_iterator_create(select_list);
		if (iter == NULL)
			fatal("list_iterator_create: malloc failure");
		for (i=0; i<job_cnt; i++) {
			select_will_run = list_next(iter);
			if (select_will_run == NULL) {
				error("wiki2: select_list size is bad");
				break;
			}
			if (i)
				xstrcat(reply_msg, " ");
			else
				xstrcat(reply_msg, "STARTINFO=");
#ifdef HAVE_BG
			select_g_get_jobinfo(select_will_run->job_ptr->
					     select_jobinfo,
                             		     SELECT_DATA_NODE_CNT, 
					     &proc_cnt);

#else
			proc_cnt = select_will_run->job_ptr->total_procs;
#endif
			snprintf(tmp_str, sizeof(tmp_str), "%u:%u@%u,",
				 select_will_run->job_ptr->job_id,
				 proc_cnt,
				 (uint32_t) select_will_run->
					    job_ptr->start_time);
			/* Restore pending job start time */
			select_will_run->job_ptr->start_time = 0;
			xstrcat(reply_msg, tmp_str);
			hostlist = bitmap2node_name(select_will_run->
						    avail_nodes);
			xstrcat(reply_msg, hostlist);
			xfree(hostlist);
		}
		list_iterator_destroy(iter);
	} else {
		/* Restore pending job start times */
		iter = list_iterator_create(select_list);
		if (iter == NULL)
			fatal("list_iterator_create: malloc failure");
		while ((select_will_run = list_next(iter)))
			select_will_run->job_ptr->start_time = 0;
		list_iterator_destroy(iter);
		xstrcat(reply_msg, "Jobs not runable on selected nodes");
		error("wiki: jobs not runnable on nodes");
	}

	list_destroy(select_list);
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
