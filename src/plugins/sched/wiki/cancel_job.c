/*****************************************************************************\
 *  cancel_job.c - Process Wiki cancel job request
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#define TYPE_ADMIN   0
#define TYPE_TIMEOUT 1

static int	_cancel_job(uint32_t jobid, int *err_code, char **err_msg);
static int	_timeout_job(uint32_t jobid, int *err_code, char **err_msg);

/* RET 0 on success, -1 on failure */
extern int	cancel_job(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *tmp_char;
	int cancel_type = TYPE_ADMIN;
	uint32_t jobid;
	static char reply_msg[128];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "CANCELJOB lacks ARG";
		error("wiki: CANCELJOB lacks ARG");
		return -1;
	}
	jobid = strtoul(arg_ptr+4, &tmp_char, 10);
	if (!isspace(tmp_char[0])) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: CANCELJOB has invalid jobid");
		return -1;
	}

	if      (strstr(cmd_ptr, "TYPE=TIMEOUT") != 0)
		cancel_type = TYPE_TIMEOUT;
	else if (strstr(cmd_ptr, "TYPE=WALLCLOCK") != 0)
		cancel_type = TYPE_TIMEOUT;
	else if (strstr(cmd_ptr, "TYPE=ADMIN") != 0)
		cancel_type = TYPE_ADMIN;
	else if (strstr(cmd_ptr, "TYPE=") != 0) {
		*err_code = -300;
		*err_msg = "Invalid TYPE value";
		error("wiki: CANCELJOB has invalid TYPE");
		return -1;
	}

	if (cancel_type == TYPE_ADMIN) {
		if (_cancel_job(jobid, err_code, err_msg) != 0)
			return -1;
	} else {
		if (_timeout_job(jobid, err_code, err_msg) != 0)
			return -1;
	}

	snprintf(reply_msg, sizeof(reply_msg),
		"job %u cancelled successfully", jobid);
	*err_msg = reply_msg;
	return 0;
}

/* Cancel a job now */
static int	_cancel_job(uint32_t jobid, int *err_code, char **err_msg)
{
	int rc = 0, slurm_rc;
	/* Write lock on job info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(job_write_lock);
	slurm_rc = job_signal(jobid, SIGKILL, 0, 0, false);
	if (slurm_rc != SLURM_SUCCESS) {
		*err_code = -700;
		*err_msg = slurm_strerror(slurm_rc);
		error("wiki: Failed to signal job %u (%m)", jobid);
		rc = -1;
		goto fini;
	}

	info("wiki: cancel job %u", jobid);

 fini:	unlock_slurmctld(job_write_lock);
	return rc;
}

/* Set timeout for specific job, the job will be purged soon */
static int	_timeout_job(uint32_t jobid, int *err_code, char **err_msg)
{
	int rc = 0;
	struct job_record *job_ptr;
	/* Write lock on job info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL) {
		*err_code = -700;
		*err_msg = "No such job";
		error("wiki: Failed to find job %u", jobid);
		rc = -1;
		goto fini;
	}

	job_ptr->end_time = time(NULL);
	debug("wiki: set end time for job %u", jobid);

 fini:	unlock_slurmctld(job_write_lock);
	return rc;
}

