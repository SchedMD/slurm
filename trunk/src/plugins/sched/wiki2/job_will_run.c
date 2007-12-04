/*****************************************************************************\
 *  job_will_run.c - Process Wiki job will_run test
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
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/state_save.h"

static char *	_will_run_test(uint32_t jobid, char *job_list, 
			char *exclude_list, int *err_code, char **err_msg);

/*
 * get_jobs - get information on specific job(s) changed since some time
 * cmd_ptr IN   - CMD=JOBWILLRUN ARG=<JOBID> AFTER=<JOBID>[:<JOBID>...]
 *                              [EXCLUDE=<node_list>]
 * err_code OUT - 0 on success or some error code
 * err_msg OUT  - error message or the JOBID from ordered list after 
 *                which the specified job can start (no JOBID if job 
 *                can start immediately) and the assigned node list.
 *                ARG=<JOBID> [AFTER=<JOBID>] NODES=<node_list>
 * NOTE: xfree() err_msg if err_code is zero
 * RET 0 on success, -1 on failure
 */
extern int	job_will_run(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *tmp_char, *job_list, *exclude_list;
	char *buf, *tmp_buf;
	int buf_size;
	uint32_t jobid;
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
	jobid = strtoul(arg_ptr+4, &tmp_char, 10);
	if ((tmp_char[0] != '\0') && (!isspace(tmp_char[0]))) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: JOBWILLRUN has invalid jobid");
		return -1;
	}

	job_list = strstr(cmd_ptr, "AFTER=");
	if (job_list) {
		job_list += 6;
		null_term(job_list);
	} else {
		*err_code = -300;
		*err_msg = "Invalid AFTER value";
		error("wiki: JOBWILLRUN has invalid jobid");
		return -1;
	}

	exclude_list = strstr(cmd_ptr, "EXCLUDE=");
	if (exclude_list) {
		exclude_list += 8;
		null_term(exclude_list);
	}

	lock_slurmctld(job_write_lock);
	buf = _will_run_test(jobid, job_list, exclude_list, err_code,
			     err_msg);
	unlock_slurmctld(job_write_lock);

	if (!buf) {
		info("wiki: JOBWILLRUN failed for job %u", jobid);
		return -1;
	}

	buf_size = strlen(buf);
	tmp_buf = xmalloc(buf_size + 32);
	sprintf(tmp_buf, "SC=0 ARG=%s", buf);
	xfree(buf);
	*err_code = 0;
	*err_msg = tmp_buf;
	return 0;
}

static char *	_will_run_test(uint32_t jobid, char *job_list, 
			char *exclude_list, int *err_code, char **err_msg)
{
	struct job_record *job_ptr;
	bitstr_t *save_exc_bitmap = NULL, *new_bitmap = NULL;
	uint32_t save_prio, *jobid_list = NULL;
	struct job_record **job_ptr_list;
	int i, job_list_size;
	char *tmp_char;

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = -700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		return NULL;
	}

	if ((job_ptr->details == NULL) ||
	    (job_ptr->job_state != JOB_PENDING)) {
		*err_code = -700;
		*err_msg = "Job not pending, can't test  will_run";
		error("wiki: Attempt to test will_run of non-pending job %u",
			jobid);
		return NULL;
	}

	/* parse the job list */
	job_list_size = strlen(job_list) + 1;
	jobid_list = xmalloc(job_list_size * sizeof(uint32_t));
	job_ptr_list = xmalloc(job_list_size * sizeof (struct job_record *));
	tmp_char = job_list;
	for (i=0; i<job_list_size; ) {
		jobid_list[i] = strtoul(tmp_char, &tmp_char, 10);
		if ((tmp_char[0] != '\0') && (!isspace(tmp_char[0])) &&
		    (tmp_char[0] != ':')) {
			*err_code = -300;
			*err_msg = "Invalid AFTER value";
			error("wiki: Invalid AFTER value of %s", job_list);
			xfree(jobid_list);
			xfree(job_ptr_list);
			return NULL;
		}
		job_ptr_list[i] = find_job_record(jobid_list[i]);
		if (job_ptr_list[i])
			i++;
		else {
			error("wiki: willrun AFTER job %u not found", 
				jobid_list[i]);
			jobid_list[i] = 0;
		}
		if (tmp_char[0] == ':')
			tmp_char++;
		else
			break;
	}

	if (exclude_list) {
		if (node_name2bitmap(exclude_list, false, &new_bitmap) != 0) {
			*err_code = -700;
			*err_msg = "Invalid EXCLUDE value";
			error("wiki: Attempt to set invalid exclude node "
				"list for job %u, %s",
				jobid, exclude_list);
			return NULL;
		}
		save_exc_bitmap = job_ptr->details->exc_node_bitmap;
		job_ptr->details->exc_node_bitmap = new_bitmap;
	}

	/* test when the job can execute */
	save_prio = job_ptr->priority;
	job_ptr->priority = 1;

#if 0
	/* execute will_run logic here */
	/* Note that last jobid_list entry has a value of zero */
	rc = select_nodes(job_ptr, true, &picked_node_bitmap);

	if (rc == SLURM_SUCCESS) {
		*err_code = 0;
		snprintf(reply_msg, sizeof(reply_msg),
			"SC=0 Job %d runnable now TASKLIST:%s",
			jobid, picked_node_list);
		*err_msg = reply_msg;
	} else if (rc == ESLURM_NODES_BUSY) {
		*err_code = 1;
		snprintf(reply_msg, sizeof(reply_msg),
			"SC=1 Job %u runnable later TASKLIST:%s",
			jobid, picked_node_list);
		*err_msg = reply_msg;
	} else if (rc == ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
		*err_code = 1;
		snprintf(reply_msg, sizeof(reply_msg),
			"SC=1 Job %u not runnable with current configuration",
			jobid);
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
#endif

	/* Restore job's state, release allocated memory */
	if (save_exc_bitmap)
		job_ptr->details->exc_node_bitmap = save_exc_bitmap;
	FREE_NULL_BITMAP(new_bitmap);
	job_ptr->priority = save_prio;
	xfree(jobid_list);
	xfree(job_ptr_list);

#if 1
	*err_code = -810;
	*err_msg  = "JOBWILLRUN not yet supported";
 	return NULL;
#endif
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
