/*****************************************************************************\
 *  job_signal.c - Process Wiki job signal request
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
#include <strings.h>
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

/* translate a signal value to the numeric value, sig_ptr value
 * can have three different forms:
 * 1. A number
 * 2. SIGUSR1 (or other suffix)
 * 3. USR1
 */
static uint16_t _xlate_signal(char *sig_ptr)
{
	uint16_t sig_val;
	char *tmp_char;

	if ((sig_ptr[0] >= '0') && (sig_ptr[0] <= '9')) {
		sig_val = (uint16_t) strtoul(sig_ptr, &tmp_char, 10);
		if ((tmp_char[0] != '\0') && (!isspace(tmp_char[0])))
			return (uint16_t) 0;
		return sig_val;
	}

	if (strncasecmp(sig_ptr, "SIG", 3) == 0)
		sig_ptr += 3;

	if (strncasecmp(sig_ptr, "HUP", 3) == 0)
		return SIGHUP;
	if (strncasecmp(sig_ptr, "INT", 3) == 0)
		return SIGINT;
	if (strncasecmp(sig_ptr, "URG", 3) == 0)
		return SIGURG;
	if (strncasecmp(sig_ptr, "QUIT", 4) == 0)
		return SIGQUIT;
	if (strncasecmp(sig_ptr, "ABRT", 4) == 0)
		return SIGABRT;
	if (strncasecmp(sig_ptr, "ALRM", 4) == 0)
		return SIGALRM;
	if (strncasecmp(sig_ptr, "TERM", 4) == 0)
		return SIGTERM;
	if (strncasecmp(sig_ptr, "USR1", 4) == 0)
		return SIGUSR1;
	if (strncasecmp(sig_ptr, "USR2", 4) == 0)
		return SIGUSR2;
	if (strncasecmp(sig_ptr, "CONT", 4) == 0)
		return SIGCONT;
	if (strncasecmp(sig_ptr, "STOP", 4) == 0)
		return SIGSTOP;

	return (uint16_t) 0;
}

static int	_job_signal(uint32_t jobid, uint16_t sig_num)
{
	struct job_record *job_ptr;
	int rc = SLURM_SUCCESS;

	job_ptr = find_job_record(jobid);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;

	if (job_ptr->batch_flag)
		rc = job_signal(jobid, sig_num, 1, 0);
	if (rc == SLURM_SUCCESS)
		rc = job_signal(jobid, sig_num, 0, 0);
	return rc;
}

/* RET 0 on success, -1 on failure */
extern int	job_signal_wiki(char *cmd_ptr, int *err_code, char **err_msg)
{
	char *arg_ptr, *sig_ptr, *tmp_char;
	int slurm_rc;
	uint32_t jobid;
	uint16_t sig_num;
	static char reply_msg[128];
	/* Locks: write job and node info */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = -300;
		*err_msg = "SIGNALJOB lacks ARG=";
		error("wiki: SIGNALJOB lacks ARG=");
		return -1;
	}
	arg_ptr +=4;
	jobid = strtoul(arg_ptr, &tmp_char, 10);
	if ((tmp_char[0] != '\0') && (!isspace(tmp_char[0]))) {
		*err_code = -300;
		*err_msg = "Invalid ARG value";
		error("wiki: SIGNALJOB has invalid jobid %s",
			arg_ptr);
		return -1;
	}

	sig_ptr = strstr(cmd_ptr, "VALUE=");
	if (sig_ptr == NULL) {
		*err_code = -300;
		*err_msg = "SIGNALJOB lacks VALUE=";
		error("wiki: SIGNALJOB lacks VALUE=");
		return -1;
	}
	sig_ptr += 6;
	sig_num = _xlate_signal(sig_ptr);
	if (sig_num == 0) {
		*err_code = -300;
		*err_msg = "SIGNALJOB has invalid signal value";
		error("wiki: SIGNALJOB has invalid signal value: %s",
			sig_ptr);
		return -1;
	}

	lock_slurmctld(job_write_lock);
	slurm_rc = _job_signal(jobid, sig_num);
	unlock_slurmctld(job_write_lock);
	if (slurm_rc != SLURM_SUCCESS) {
		*err_code = -700;
		*err_msg = slurm_strerror(slurm_rc);
		error("wiki: Failed to signal job %u: %s",
			jobid, slurm_strerror(slurm_rc));
		return -1;
	}

	snprintf(reply_msg, sizeof(reply_msg),
		"job %u signalled", jobid);
	*err_msg = reply_msg;
	return 0;
}
