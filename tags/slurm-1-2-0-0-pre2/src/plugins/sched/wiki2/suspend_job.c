/*****************************************************************************\
 *  suspend_job.c - Process Wiki suspend job request
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "./msg.h"
#include "src/slurmctld/slurmctld.h"

/* RET 0 on success, -1 on failure */
extern int	suspend_job(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *tmp_char;
	int slurm_rc;
	suspend_msg_t msg;
	uint32_t jobid;
	static char reply_msg[128];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = 300;
		*err_msg = "SUSPENDJOB lacks ARG";
		error("wiki: SUSPENDJOB lacks ARG");
		return -1;
	}
	jobid = strtol(arg_ptr+4, &tmp_char, 10);
	if (!isspace(tmp_char[0])) {
		*err_code = 300;
		*err_msg = "Invalid ARG value";
		error("wiki: SUSPENDJOB has invalid jobid");
		return -1;
	}

	msg.job_id = jobid;
	msg.op = SUSPEND_JOB;
	slurm_rc = job_suspend(&msg, 0, -1);
	if (slurm_rc != SLURM_SUCCESS) {
		*err_code = 700;
		*err_msg = slurm_strerror(slurm_rc);
		error("wiki: Failed to suspend job %u (%m)", jobid);
		return -1;
	}

	snprintf(reply_msg, sizeof(reply_msg),
		"job %u suspended successfully", jobid);
	*err_msg = reply_msg;
	return 0;
}
