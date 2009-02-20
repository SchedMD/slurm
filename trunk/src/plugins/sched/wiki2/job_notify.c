/*****************************************************************************\
 *  job_notify.c - Process Wiki job notify request
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
#include <strings.h>
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

static int	_job_notify(uint32_t jobid, char *msg_ptr)
{
	struct job_record *job_ptr;

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		error("wiki: NOTIFYJOB has invalid jobid %u", jobid);
		return ESLURM_INVALID_JOB_ID;
	}
	if (IS_JOB_FINISHED(job_ptr)) {
		error("wiki: NOTIFYJOB jobid %u is finished", jobid);
		return ESLURM_INVALID_JOB_ID;
	}
	srun_user_message(job_ptr, msg_ptr);
	return SLURM_SUCCESS;
}

/* Notify a job via arbitrary message: 
 *	CMD=NOTIFYJOB ARG=<jobid> MSG=<string>
 * RET 0 on success, -1 on failure */
extern int	job_notify_wiki(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *msg_ptr;
	int slurm_rc;
	uint32_t jobid;
	static char reply_msg[128];
	/* Locks: read job */
	slurmctld_lock_t job_read_lock = {
		NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "NOTIFYJOB lacks ARG=";
		error("wiki: NOTIFYJOB lacks ARG=");
		return -1;
	}
	arg_ptr += 4;
	jobid = atol(arg_ptr);
	msg_ptr = strstr(cmd_ptr, "MSG=");
	if (msg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "NOTIFYJOB lacks MSG=";
		error("wiki: NOTIFYJOB lacks MSG=");
		return -1;
	}
	msg_ptr += 4;

	lock_slurmctld(job_read_lock);
	slurm_rc = _job_notify(jobid, msg_ptr);
	unlock_slurmctld(job_read_lock);
	if (slurm_rc != SLURM_SUCCESS) {
		*err_code = -700;
		*err_msg = slurm_strerror(slurm_rc);
		error("wiki: Failed to notify job %u (%m)", jobid);
		return -1;
	}

	snprintf(reply_msg, sizeof(reply_msg),
		"job %u notified successfully", jobid);
	*err_msg = reply_msg;
	return 0;
}
