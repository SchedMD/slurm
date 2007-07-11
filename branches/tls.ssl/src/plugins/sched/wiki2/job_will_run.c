/*****************************************************************************\
 *  job_will_run.c - Process Wiki job will_run test
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
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
#include "slurm/slurm_errno.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/state_save.h"

static char *	_copy_nodelist_no_dup(char *node_list);
static int	_will_run_test(uint32_t jobid, char *hostlist, 
			int *err_code, char **err_msg);

/* RET 0 on success, -1 on failure */
extern int	job_will_run(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *task_ptr, *node_ptr, *tmp_char;
	int i;
	uint32_t jobid;
	char host_string[MAXHOSTRANGELEN];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "JOBWILLRUN lacks ARG";
		error("wiki: JOBWILLRUN lacks ARG");
		return -1;
	}
	jobid = strtoul(arg_ptr+4, &tmp_char, 10);
	if ((tmp_char[0] != '\0') && (!isspace(tmp_char[0]))) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: JOBWILLRUN has invalid jobid");
		return -1;
	}

	task_ptr = strstr(cmd_ptr, "TASKLIST=");
	if (task_ptr) {
		hostlist_t hl;
		node_ptr = task_ptr + 9;
		for (i=0; node_ptr[i]!='\0'; i++) {
			if (node_ptr[i] == ':')
				node_ptr[i] = ',';
		}
		hl = hostlist_create(node_ptr);
		i = hostlist_ranged_string(hl, sizeof(host_string), host_string);
		hostlist_destroy(hl);
		if (i < 0) {
			*err_code = -300;
			*err_msg = "JOBWILLRUN has invalid TASKLIST";
			error("wiki: JOBWILLRUN has invalid TASKLIST");
			return -1;
		}
	} else {
		/* no restrictions on nodes available for use */
		strcpy(host_string, "");
	}

	if (_will_run_test(jobid, host_string, err_code, err_msg) != 0)
		return -1;

	return 0;
}

static int	_will_run_test(uint32_t jobid, char *hostlist,
			int *err_code, char **err_msg)
{
	int rc = 0, i;
	struct job_record *job_ptr;
	/* Write lock on job info, read lock on node info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	char *new_node_list, *picked_node_list = NULL;
	bitstr_t *new_bitmap, *save_exc_bitmap, *save_req_bitmap;
	uint32_t save_prio;
	bitstr_t *picked_node_bitmap = NULL;
	/* Just create a big static message buffer to avoid dealing with
	 * xmalloc/xfree. We'll switch to compressed node naming soon
	 * and this buffer can be set smaller then. */
	static char reply_msg[16384];

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = -700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		rc = -1;
		unlock_slurmctld(job_write_lock);
		return rc;
	}

	if ((job_ptr->details == NULL)
	||  (job_ptr->job_state != JOB_PENDING)) {
		*err_code = -700;
		*err_msg = "Job not pending, can't test  will_run";
		error("wiki: Attempt to test will_run of non-pending job %u",
			jobid);
		rc = -1;
		unlock_slurmctld(job_write_lock);
		return rc;
	}

	new_node_list = _copy_nodelist_no_dup(hostlist);
	if (hostlist && (new_node_list == NULL)) {
		*err_code = -700;
		*err_msg = "Invalid TASKLIST";
		error("wiki: Attempt to set invalid node list for job %u, %s",
			jobid, hostlist);
		rc = -1;
		unlock_slurmctld(job_write_lock);
		return rc;
	}

	if (node_name2bitmap(new_node_list, false, &new_bitmap) != 0) {
		*err_code = -700;
		*err_msg = "Invalid TASKLIST";
		error("wiki: Attempt to set invalid node list for job %u, %s",
			jobid, hostlist);
		rc = -1;
		xfree(new_node_list);
		unlock_slurmctld(job_write_lock);
		return rc;
	}

	/* Put the inverse of this on the excluded node list, 
	 * Remove any required nodes, and test */
	save_exc_bitmap = job_ptr->details->exc_node_bitmap;
	if (hostlist[0]) {	/* empty hostlist, all nodes usable */
		bit_not(new_bitmap);
		job_ptr->details->exc_node_bitmap = new_bitmap;
	}
	save_req_bitmap = job_ptr->details->req_node_bitmap;
	job_ptr->details->req_node_bitmap = bit_alloc(node_record_count);
	save_prio = job_ptr->priority;
	job_ptr->priority = 1;

	rc = select_nodes(job_ptr, true, &picked_node_bitmap);
	if (picked_node_bitmap) {
		picked_node_list = bitmap2wiki_node_name(picked_node_bitmap);
		i = strlen(picked_node_list);
		if ((i + 64) > sizeof(reply_msg))
			error("wiki: will_run buffer overflow");
	}

	if (rc == SLURM_SUCCESS) {
		*err_code = 0;
		snprintf(reply_msg, sizeof(reply_msg),
			"SC=0 Job %d runnable now TASKLIST:%s",
			jobid, picked_node_list);
		*err_msg = reply_msg;
	} else if (rc == ESLURM_NODES_BUSY) {
		*err_code = 1;
		snprintf(reply_msg, sizeof(reply_msg),
			"SC=1 Job %d runnable later TASKLIST:%s",
			jobid, picked_node_list);
		*err_msg = reply_msg;
	} else {
		char *err_str = slurm_strerror(rc);
		error("wiki: job %d never runnable on hosts=%s %s", 
			jobid, new_node_list, err_str);
		*err_code = -740;
		snprintf(reply_msg, sizeof(reply_msg), 
			"SC=-740 Job %d not runable: %s", 
			jobid, err_str);
		*err_msg = reply_msg;
	}

	/* Restore job's state, release memory */
	xfree(picked_node_list);
	FREE_NULL_BITMAP(picked_node_bitmap);
	xfree(new_node_list);
	bit_free(new_bitmap);
	FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	job_ptr->details->exc_node_bitmap = save_exc_bitmap;
	job_ptr->details->req_node_bitmap = save_req_bitmap;
	job_ptr->priority = save_prio;
	unlock_slurmctld(job_write_lock);
 	return rc;
}

static char *	_copy_nodelist_no_dup(char *node_list)
{
	int   new_size = 128;
	char *new_str;
	hostlist_t hl = hostlist_create(node_list);

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

/*
 * bitmap2wiki_node_name  - given a bitmap, build a list of colon separated 
 *	node names (if we can't use node range expressions), or the 
 *	normal slurm node name expression
 *
 * IN bitmap - bitmap pointer
 * RET pointer to node list or NULL on error 
 * globals: node_record_table_ptr - pointer to node table
 * NOTE: the caller must xfree the memory at node_list when no longer required
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
