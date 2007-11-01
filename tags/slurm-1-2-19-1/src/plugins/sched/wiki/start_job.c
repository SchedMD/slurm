/*****************************************************************************\
 *  start_job.c - Process Wiki start job request
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

static int	_start_job(uint32_t jobid, char *hostlist, 
			int *err_code, char **err_msg);

/* RET 0 on success, -1 on failure */
extern int	start_job(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *task_ptr, *node_ptr, *tmp_char;
	int i;
	uint32_t jobid;
	hostlist_t hl;
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

	task_ptr = strstr(cmd_ptr, "TASKLIST=");
	if (task_ptr == NULL) {
		*err_code = -300;
		*err_msg = "STARTJOB lacks TASKLIST";
		error("wiki: STARTJOB lacks TASKLIST");
		return -1;
	}
	node_ptr = task_ptr + 9;
	for (i=0; node_ptr[i]!='\0'; i++) {
		if (node_ptr[i] == ':')
			node_ptr[i] = ',';
	}
	hl = hostlist_create(node_ptr);
	if (hl == NULL) {
		*err_code = -300;
		*err_msg = "STARTJOB TASKLIST is invalid";
		error("wiki: STARTJOB TASKLIST is invalid: %s",
			node_ptr);
		return -1;
	}
	hostlist_uniq(hl);	/* for now, don't worry about task layout */
	hostlist_sort(hl);
	i = hostlist_ranged_string(hl, sizeof(host_string), host_string);
	hostlist_destroy(hl);
	if (i < 0) {
		*err_code = -300;
		*err_msg = "STARTJOB has invalid TASKLIST";
		error("wiki: STARTJOB has invalid TASKLIST: %s",
			host_string);
		return -1;
	}
	if (_start_job(jobid, host_string, err_code, err_msg) != 0)
		return -1;

	snprintf(reply_msg, sizeof(reply_msg), 
		"job %u started successfully", jobid);
	*err_msg = reply_msg;
	return 0;
}

static int	_start_job(uint32_t jobid, char *hostlist,
			int *err_code, char **err_msg)
{
	int rc = 0;
	struct job_record *job_ptr;
	/* Write lock on job info, read lock on node info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	char *new_node_list, *save_req_nodes = NULL;
	static char tmp_msg[128];
	bitstr_t *new_bitmap, *save_req_bitmap = (bitstr_t *) NULL;

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
		*err_msg = "Job not pending, can't update";
		error("wiki: Attempt to start non-pending job %u",
			jobid);
		rc = -1;
		goto fini;
	}

	new_node_list = xstrdup(hostlist);
	if (hostlist && (new_node_list == NULL)) {
		*err_code = -700;
		*err_msg = "Invalid TASKLIST";
		error("wiki: Attempt to set invalid node list for job %u, %s",
			jobid, hostlist);
		rc = -1;
		goto fini;
	}

	if (node_name2bitmap(new_node_list, false, &new_bitmap) != 0) {
		*err_code = -700;
		*err_msg = "Invalid TASKLIST";
		error("wiki: Attempt to set invalid node list for job %u, %s",
			jobid, hostlist);
		xfree(new_node_list);
		rc = -1;
		goto fini;
	}

	/* Remove any excluded nodes, incompatable with Wiki */
	if (job_ptr->details->exc_nodes) {
		error("wiki: clearing exc_nodes for job %u", jobid);
		xfree(job_ptr->details->exc_nodes);
		if (job_ptr->details->exc_node_bitmap)
			FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
	}

	/* start it now */
	save_req_nodes = job_ptr->details->req_nodes;
	job_ptr->details->req_nodes = new_node_list;
	save_req_bitmap = job_ptr->details->req_node_bitmap;
	job_ptr->details->req_node_bitmap = new_bitmap;
	job_ptr->priority = 100000000;

 fini:	unlock_slurmctld(job_write_lock);
	if (rc == 0) {	/* New job to start ASAP */
		(void) schedule();	/* provides own locking */
		/* Check to insure the job was actually started */
		lock_slurmctld(job_write_lock);
		/* job_ptr = find_job_record(jobid);	don't bother */
		if ((job_ptr->job_id == jobid) && job_ptr->details &&
		    (job_ptr->job_state == JOB_RUNNING)) {
			/* Restore required node list */
			xfree(job_ptr->details->req_nodes);
			job_ptr->details->req_nodes = save_req_nodes;
			FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
			job_ptr->details->req_node_bitmap = save_req_bitmap;
		} else {
			xfree(save_req_nodes);
			FREE_NULL_BITMAP(save_req_bitmap);
		}

		if ((job_ptr->job_id == jobid)
		&&  (job_ptr->job_state != JOB_RUNNING)) {
			uint16_t wait_reason = 0;
			char *wait_string;

			/* restore job state */
			job_ptr->priority = 0;
			if (job_ptr->details) {
				/* Details get cleared on job abort; happens
				 * if the request is sufficiently messed up.
				 * This happens when Moab tries to start a
				 * a job on invalid nodes (wrong partition). */
				xfree(job_ptr->details->req_nodes);
				FREE_NULL_BITMAP(job_ptr->details->
						 req_node_bitmap);
			}
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
			}
			*err_code = -910 - wait_reason;
			snprintf(tmp_msg, sizeof(tmp_msg),
				"Could not start job %u: %s",
				jobid, wait_string);
			*err_msg = tmp_msg;
			error("wiki: %s", tmp_msg);
			rc = -1;
		}
		unlock_slurmctld(job_write_lock);
		schedule_node_save();	/* provides own locking */
		schedule_job_save();	/* provides own locking */
	}
	return rc;
}
