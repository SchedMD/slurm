/*****************************************************************************\
 *  start_job.c - Process Wiki start job request
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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

#include "./msg.h"
#include "src/common/bitstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

static char *	_copy_nodelist_no_dup(char *node_list);
static int	_start_job(uint32_t jobid, char *hostlist, 
			int *err_code, char **err_msg);

/* RET 0 on success, -1 on failure */
extern int	start_job(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *task_ptr, *node_ptr, *tmp_char;
	int i;
	uint32_t jobid;
	hostlist_t hl;
	char host_string[1024];
	static char reply_msg[128];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = 300;
		*err_msg = "STARTJOB lacks ARG";
		error("wiki: STARTJOB lacks ARG");
		return -1;
	}
	jobid = strtol(arg_ptr+4, &tmp_char, 10);
	if (!isspace(tmp_char[0])) {
		*err_code = 300;
		*err_msg = "Invalid ARG value";
		error("wiki: STARTJOB has invalid jobid");
		return -1;
	}

	task_ptr = strstr(cmd_ptr, "TASKLIST=");
	if (task_ptr == NULL) {
		*err_code = 300;
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
	i = hostlist_ranged_string(hl, sizeof(host_string), host_string);
	hostlist_destroy(hl);
	if (i < 0) {
		*err_code = 300;
		*err_msg = "STARTJOB has invalid TASKLIST";
		error("wiki: STARTJOB has invalid TASKLIST");
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
	char *new_node_list;
	bitstr_t *new_bitmap;

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = 700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		rc = -1;
		goto fini;
	}

	if ((job_ptr->details == NULL)
	||  (job_ptr->job_state != JOB_PENDING)) {
		*err_code = 700;
		*err_msg = "Job not pending, can't update";
		error("wiki: Attempt to change state of non-pending job %u",
			jobid);
		rc = -1;
		goto fini;
	}

	new_node_list = _copy_nodelist_no_dup(hostlist);
	if (hostlist && (new_node_list == NULL)) {
		*err_code = 700;
		*err_msg = "Invalid TASKLIST";
		error("wiki: Attempt to set invalid node list for job %u, %s",
			jobid, hostlist);
		rc = -1;
		goto fini;
	}

	if (node_name2bitmap(new_node_list, false, &new_bitmap) != 0) {
		*err_code = 700;
		*err_msg = "Invalid TASKLIST";
		error("wiki: Attempt to set invalid node list for job %u, %s",
			jobid, hostlist);
		xfree(hostlist);
		rc = -1;
		goto fini;
	}

	/* Remove any excluded nodes, incompatable with Wiki */
	if (job_ptr->details->exc_nodes) {
		error("wiki: clearing exc_nodes for job %u", jobid);
		xfree(job_ptr->details->exc_nodes);
		if (job_ptr->details->exc_node_bitmap)
			bit_free(job_ptr->details->exc_node_bitmap);
	}

	/* start it now */
	xfree(job_ptr->details->req_nodes);
	job_ptr->details->req_nodes = new_node_list;
	if (job_ptr->details->req_node_bitmap)
		bit_free(job_ptr->details->req_node_bitmap);
	job_ptr->details->req_node_bitmap = new_bitmap;
	job_ptr->priority = 1000000;

 fini:	unlock_slurmctld(job_write_lock);
	/* functions below provide their own locking */
	if (rc == 0) {	/* New job to start ASAP */
		(void) schedule();
		schedule_node_save();
		schedule_job_save();
	}
	return rc;
}

static char *	_copy_nodelist_no_dup(char *node_list)
{
	int   new_size = 128;
	char *new_str;
	hostlist_t hl = hostlist_create( node_list );

	if (hl == NULL)
		return NULL;

	hostlist_uniq(hl);
	new_str = xmalloc(new_size);
	while (hostlist_ranged_string(hl, new_size, new_str) == -1) {
		new_size *= 2;
		xrealloc(new_str, new_size);
	}
	hostlist_destroy(hl);
	return new_str;
}

