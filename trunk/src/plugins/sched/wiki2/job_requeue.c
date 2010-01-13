/*****************************************************************************\
 *  job_requeue.c - Process Wiki job requeue request
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
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
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

/* RET 0 on success, -1 on failure */
extern int	job_requeue_wiki(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *tmp_char;
	uint32_t jobid;
	struct job_record *job_ptr;
	static char reply_msg[128];
	int slurm_rc;
	/* Write lock on job and node info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "REQUEUEJOB lacks ARG";
		error("wiki: REQUEUEJOB lacks ARG");
		return -1;
	}
	jobid = strtoul(arg_ptr+4, &tmp_char, 10);
	if ((tmp_char[0] != '\0') && (!isspace(tmp_char[0]))) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: REQUEUEJOB has invalid jobid");
		return -1;
	}

	lock_slurmctld(job_write_lock);
	slurm_rc = job_requeue(0, jobid, -1, (uint16_t)NO_VAL);
	if (slurm_rc != SLURM_SUCCESS) {
		unlock_slurmctld(job_write_lock);
		*err_code = -700;
		*err_msg = slurm_strerror(slurm_rc);
		error("wiki: Failed to requeue job %u (%m)", jobid);
		return -1;
	}

	/* We need to clear the required node list here.
	 * If the job was submitted with srun and a
	 * required node list, it gets lost here. */
	job_ptr = find_job_record(jobid);
	if (job_ptr && job_ptr->details) {
		xfree(job_ptr->details->req_nodes);
		FREE_NULL_BITMAP(job_ptr->details->req_node_bitmap);
	}
	info("wiki: requeued job %u", jobid);
	unlock_slurmctld(job_write_lock);
	snprintf(reply_msg, sizeof(reply_msg),
		"job %u requeued successfully", jobid);
	*err_msg = reply_msg;
	return 0;
}
