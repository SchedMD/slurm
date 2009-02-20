/*****************************************************************************\
 *  start_job.c - Process Wiki start job request
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xstring.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

static int	_start_job(uint32_t jobid, int task_cnt, char *hostlist, 
			char *tasklist, char *comment_ptr, 
			int *err_code, char **err_msg);

/* Start a job:
 *	CMD=STARTJOB ARG=<jobid> TASKLIST=<node_list> [COMMENT=<whatever>]
 * RET 0 on success, -1 on failure */
extern int	start_job(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *comment_ptr, *task_ptr, *tasklist, *tmp_char;
	int i, rc, task_cnt;
	uint32_t jobid;
	hostlist_t hl = (hostlist_t) NULL;
	char host_string[MAXHOSTRANGELEN];
	static char reply_msg[128];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "STARTJOB lacks ARG";
		error("wiki: STARTJOB lacks ARG");
		return -1;
	}
	jobid = strtoul(arg_ptr+4, &tmp_char, 10);
	if (!isspace(tmp_char[0])) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: STARTJOB has invalid jobid");
		return -1;
	}

	comment_ptr = strstr(cmd_ptr, "COMMENT=");
	task_ptr    = strstr(cmd_ptr, "TASKLIST=");

	if (comment_ptr) {
		comment_ptr[7] = ':';
		comment_ptr += 8;
		if (comment_ptr[0] == '\"') {
			comment_ptr++;
			for (i=0; i<MAX_COMMENT_LEN; i++) {
				if (comment_ptr[i] == '\0')
					break;
				if (comment_ptr[i] == '\"') {
					comment_ptr[i] = '\0';
					break;
				}
			}
			if (i == MAX_COMMENT_LEN)
				comment_ptr[i-1] = '\0';
		} else if (comment_ptr[0] == '\'') {
			comment_ptr++;
			for (i=0; i<MAX_COMMENT_LEN; i++) {
				if (comment_ptr[i] == '\0')
					break;
				if (comment_ptr[i] == '\'') {
					comment_ptr[i] = '\0';
					break;
				}
			}
			if (i == MAX_COMMENT_LEN)
				comment_ptr[i-1] = '\0';
		} else
			null_term(comment_ptr);
	}

	if (task_ptr == NULL) {
		*err_code = -300;
		*err_msg = "STARTJOB lacks TASKLIST";
		error("wiki: STARTJOB lacks TASKLIST");
		return -1;
	}
	task_ptr += 9;	/* skip over "TASKLIST=" */
	if ((task_ptr[0] == '\0') || isspace(task_ptr[0])) {
		/* No TASKLIST specification, useful for testing */
		host_string[0] = '0';
		task_cnt = 0;
		tasklist = NULL;
	} else {
		null_term(task_ptr);
		tasklist = moab2slurm_task_list(task_ptr, &task_cnt);
		if (tasklist)
			hl = hostlist_create(tasklist);
		if ((tasklist == NULL) || (hl == NULL)) {
			*err_code = -300;
			*err_msg = "STARTJOB TASKLIST is invalid";
			error("wiki: STARTJOB TASKLIST is invalid: %s",
			      task_ptr);
			xfree(tasklist);
			return -1;
		}
		hostlist_uniq(hl);
		hostlist_sort(hl);
		i = hostlist_ranged_string(hl, sizeof(host_string), host_string);
		hostlist_destroy(hl);
		if (i < 0) {
			*err_code = -300;
			*err_msg = "STARTJOB has invalid TASKLIST";
			error("wiki: STARTJOB has invalid TASKLIST: %s",
			      host_string);
			xfree(tasklist);
			return -1;
		}
	}

	rc = _start_job(jobid, task_cnt, host_string, tasklist, comment_ptr,
			err_code, err_msg);
	xfree(tasklist);
	if (rc == 0) {
		snprintf(reply_msg, sizeof(reply_msg), 
			"job %u started successfully", jobid);
		*err_msg = reply_msg;
	}
	return rc;
}

/*
 * Attempt to start a job
 * jobid     (IN) - job id
 * task_cnt  (IN) - total count of tasks to start
 * hostlist  (IN) - SLURM hostlist expression with no repeated hostnames
 * tasklist  (IN/OUT) - comma separated list of hosts with tasks to be started,
 *                  list hostname once per task to start
 * comment_ptr (IN) - new comment field for the job or NULL for no change
 * err_code (OUT) - Moab error code
 * err_msg  (OUT) - Moab error message
 */
static int	_start_job(uint32_t jobid, int task_cnt, char *hostlist, 
			char *tasklist, char *comment_ptr, 
			int *err_code, char **err_msg)
{
	int rc = 0, old_task_cnt = 1;
	struct job_record *job_ptr;
	/* Write lock on job info, read lock on node info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	char *new_node_list = NULL;
	static char tmp_msg[128];
	bitstr_t *new_bitmap = (bitstr_t *) NULL;
	bitstr_t *save_req_bitmap = (bitstr_t *) NULL;
	bitoff_t i, bsize;
	int ll; /* layout info index */
	char *node_name, *node_idx, *node_cur, *save_req_nodes = NULL;
	size_t node_name_len;
	static uint32_t cr_test = 0, cr_enabled = 0;

	if (cr_test == 0) {
		select_g_get_info_from_plugin(SELECT_CR_PLUGIN, NULL,
						&cr_enabled);
		cr_test = 1;
	}

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = -700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		rc = -1;
		goto fini;
	}

	if ((job_ptr->details == NULL)
	||  (job_ptr->job_state != JOB_PENDING)) {
		*err_code = -700;
		*err_msg = "Job not pending, can't start";
		error("wiki: Attempt to start job %u in state %s",
			jobid, job_state_string(job_ptr->job_state));
		rc = -1;
		goto fini;
	}

	if (comment_ptr) {
		char *reserved = strstr(comment_ptr, "RESERVED:");
		if (reserved) {
			reserved += 9;
			job_ptr->details->reserved_resources =
				strtol(reserved, NULL, 10);
		}
		xfree(job_ptr->comment);
		job_ptr->comment = xstrdup(comment_ptr);
	}

	if (task_cnt) {
		new_node_list = xstrdup(hostlist);
		if (node_name2bitmap(new_node_list, false, &new_bitmap) != 0) {
			*err_code = -700;
			*err_msg = "Invalid TASKLIST";
			error("wiki: Attempt to set invalid node list for "
				"job %u, %s",
				jobid, hostlist);
			xfree(new_node_list);
			rc = -1;
			goto fini;
		}

		if (!bit_super_set(new_bitmap, avail_node_bitmap)) {
			/* Selected node is UP and not responding
			 * or it just went DOWN */
			*err_code = -700;
			*err_msg = "TASKLIST includes non-responsive node";
			error("wiki: Attempt to use non-responsive nodes for "
				"job %u, %s",
				jobid, hostlist);
			xfree(new_node_list);
			bit_free(new_bitmap);
			rc = -1;
			goto fini;
		}

		/* User excluded node list incompatable with Wiki
		 * Exclude all nodes not explicitly requested */
		FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
		job_ptr->details->exc_node_bitmap = bit_copy(new_bitmap);
		bit_not(job_ptr->details->exc_node_bitmap);
	}

	/* Build layout information from tasklist (assuming that Moab
	 * sends a non-bracketed list of nodes, repeated as many times
	 * as cpus should be used per node); at this point, node names
	 * are comma-separated. This is _not_ a fast algorithm as it
	 * performs many string compares. */
	xfree(job_ptr->details->req_node_layout);
	if (task_cnt && cr_enabled) {
		uint16_t cpus_per_task = MAX(1, job_ptr->details->cpus_per_task);
		job_ptr->details->req_node_layout = (uint16_t *)
			xmalloc(bit_set_count(new_bitmap) * sizeof(uint16_t));
		bsize = bit_size(new_bitmap);
		for (i = 0, ll = -1; i < bsize; i++) {
			if (!bit_test(new_bitmap, i))
				continue;
			ll++;
			node_name = node_record_table_ptr[i].name;
			node_name_len  = strlen(node_name);
			if (node_name_len == 0)
				continue;
			node_cur = tasklist;
			while (*node_cur) {
				if ((node_idx = strstr(node_cur, node_name))) {
					if ((node_idx[node_name_len] == ',') ||
				 	    (node_idx[node_name_len] == '\0')) {
						job_ptr->details->
							req_node_layout[ll] +=
							cpus_per_task;
					}
					node_cur = strchr(node_idx, ',');
					if (node_cur)
						continue;
				}
				break;
			}
		}
	}

	/* save and update job state to start now */
	save_req_nodes = job_ptr->details->req_nodes;
	job_ptr->details->req_nodes = new_node_list;
	save_req_bitmap = job_ptr->details->req_node_bitmap;
	job_ptr->details->req_node_bitmap = new_bitmap;
	old_task_cnt = job_ptr->num_procs;
	job_ptr->num_procs = MAX(task_cnt, old_task_cnt); 
	job_ptr->priority = 100000000;

 fini:	unlock_slurmctld(job_write_lock);
	if (rc)
		return rc;

	/* No errors so far */
	(void) schedule();	/* provides own locking */

	/* Check to insure the job was actually started */
	lock_slurmctld(job_write_lock);
	if (job_ptr->job_id != jobid)
		job_ptr = find_job_record(jobid);

	if (job_ptr && (job_ptr->job_id == jobid) 
	&&  (job_ptr->job_state != JOB_RUNNING)) {
		uint16_t wait_reason = 0;
		char *wait_string;

		if (job_ptr->job_state == JOB_FAILED)
			wait_string = "Invalid request, job aborted";
		else {
			wait_reason = job_ptr->state_reason;
			if (wait_reason == WAIT_HELD) {
				/* some job is completing, slurmctld did
				 * not even try to schedule this job */
				wait_reason = WAIT_RESOURCES;
			}
			wait_string = job_reason_string(wait_reason);
			job_ptr->state_reason = WAIT_HELD;
			xfree(job_ptr->state_desc);
		}
		*err_code = -910 - wait_reason;
		snprintf(tmp_msg, sizeof(tmp_msg),
			"Could not start job %u(%s): %s",
			jobid, new_node_list, wait_string);
		*err_msg = tmp_msg;
		error("wiki: %s", tmp_msg);

		/* restore some of job state */
		job_ptr->priority = 0;
		job_ptr->num_procs = old_task_cnt;
		rc = -1;
	}

	if (job_ptr && (job_ptr->job_id == jobid) && job_ptr->details) {
		/* Restore required node list in case job requeued */
		xfree(job_ptr->details->req_nodes);
		job_ptr->details->req_nodes = save_req_nodes;
		FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
		job_ptr->details->req_node_bitmap = save_req_bitmap;
		FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
		xfree(job_ptr->details->req_node_layout);
	} else {
		error("wiki: start_job(%u) job missing", jobid);
		xfree(save_req_nodes);
		FREE_NULL_BITMAP(save_req_bitmap);
	}

	unlock_slurmctld(job_write_lock);
	schedule_node_save();	/* provides own locking */
	schedule_job_save();	/* provides own locking */
	return rc;
}
