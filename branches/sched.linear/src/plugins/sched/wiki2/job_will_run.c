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
#include "src/common/node_select.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/state_save.h"

static char *	_will_run_test(uint32_t jobid, char *node_list, 
			int *err_code, char **err_msg);

/*
 * get_jobs - get information on specific job(s) changed since some time
 * cmd_ptr IN   - CMD=JOBWILLRUN ARG=<JOBID> AVAIL_NODES=<node_list>]
 * err_code OUT - 0 on success or some error code
 * err_msg OUT  - error message or the JOBID from ordered list after 
 *                which the specified job can start (no JOBID if job 
 *                can start immediately) and the assigned node list.
 *                ARG=<JOBID> STARTDATE=<uts> HOSTLIST=<node_list>
 * NOTE: xfree() err_msg if err_code is zero
 * RET 0 on success, -1 on failure
 */
extern int	job_will_run(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *tmp_char, *avail_nodes, *buf, *tmp_buf;
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

	avail_nodes = strstr(cmd_ptr, "AVAIL_NODES=");
	if (avail_nodes) {
		avail_nodes += 12;
		null_term(avail_nodes);
	} else {
		*err_code = -300;
		*err_msg = "Invalid AVAIL_NODES value";
		error("wiki: JOBWILLRUN call lacks AVAIL_NODES argument");
		return -1;
	}

	lock_slurmctld(job_write_lock);
	buf = _will_run_test(jobid, avail_nodes, err_code, err_msg);
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

static char *	_will_run_test(uint32_t jobid, char *node_list, 
			int *err_code, char **err_msg)
{
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bitstr_t *avail_bitmap = NULL;
	char *reply_msg;
	uint32_t min_nodes, max_nodes, req_nodes;
	int rc;

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = -700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		return NULL;
	}

	part_ptr = job_ptr->part_ptr;
	if (part_ptr == NULL) {
		*err_code = -700;
		*err_msg = "Job lacks a partition";
		error("wiki: Job %u lacks a partition", jobid);
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

	if ((node_list == NULL) ||
	    (node_name2bitmap(node_list, false, &avail_bitmap) != 0)) {
		*err_code = -700;
		*err_msg = "Invalid AVAIL_NODES value";
		error("wiki: Attempt to set invalid available node "
			"list for job %u, %s",
			jobid, node_list);
		return NULL;
	}

	min_nodes = MAX(job_ptr->details->min_nodes, part_ptr->min_nodes);
	if (job_ptr->details->max_nodes == 0)
		max_nodes = part_ptr->max_nodes;
	else
		max_nodes = MIN(job_ptr->details->max_nodes, 
				part_ptr->max_nodes);
	max_nodes = MIN(max_nodes, 500000);	/* prevent overflows */
	if (job_ptr->details->max_nodes)
		req_nodes = max_nodes;
	else
		req_nodes = min_nodes;

	rc = select_g_job_test(job_ptr, avail_bitmap,
			min_nodes, max_nodes, req_nodes, 
			SELECT_MODE_WILL_RUN);
	
	if (rc == SLURM_SUCCESS) {
		char *hostlist = bitmap2node_name(avail_bitmap);
		reply_msg = xmalloc(strlen(hostlist) + 128);
		sprintf(reply_msg, 
			 "%u STARTDATE=%lu HOSTLIST=%s",
			 jobid, job_ptr->start_time, hostlist);
		xfree(hostlist);
		*err_code = 0;
		return reply_msg;
	} else {
		*err_code = -740;
		*err_msg = "Job not runable on selected nodes";
		error("wiki: job %d not runnable on hosts=%s", 
			jobid, node_list);
		FREE_NULL_BITMAP(avail_bitmap);
		return NULL;
	}

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
