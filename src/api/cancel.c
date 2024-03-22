/*****************************************************************************\
 *  cancel.c - cancel a slurm job or job step
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"

#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static int _slurm_kill_job_internal(uint32_t job_id,
				    const char *sjob_id_in, const char *sibling,
				    uint16_t signal, uint16_t flags)
{
	int cc = 0, rc = SLURM_SUCCESS;
	slurm_msg_t msg;
	job_step_kill_msg_t req;
	char *sjob_id =
		job_id ? xstrdup_printf("%u", job_id) : xstrdup(sjob_id_in);

	if (!sjob_id) {
		errno = EINVAL;
		return SLURM_ERROR;
	}

	slurm_msg_t_init(&msg);

	memset(&req, 0, sizeof(req));
	req.step_id.job_id = NO_VAL;
	req.sjob_id = sjob_id;
	req.step_id.step_id = NO_VAL;
	req.step_id.step_het_comp = NO_VAL;
	req.signal = signal;
	req.flags = flags;
	req.sibling = xstrdup(sibling);
	msg.msg_type = REQUEST_KILL_JOB;
        msg.data = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &cc, working_cluster_rec))
		rc = SLURM_ERROR;

	xfree(sjob_id);
	xfree(req.sibling);

	if (cc)
		slurm_seterrno_ret(cc);

	return rc;
}

/*
 * slurm_kill_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * IN flags      - see KILL_JOB_* flags in slurm.h
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int
slurm_kill_job (uint32_t job_id, uint16_t signal, uint16_t flags)
{
	return _slurm_kill_job_internal(job_id, NULL, NULL, signal, flags);
}

extern int slurm_kill_jobs(kill_jobs_msg_t *kill_msg,
			   kill_jobs_resp_msg_t **kill_msg_resp)
{
	int rc = SLURM_SUCCESS;
	slurm_msg_t msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&msg);
	slurm_msg_t_init(&resp_msg);
	msg.data = kill_msg;
	msg.msg_type = REQUEST_KILL_JOBS;

	if ((rc = slurm_send_recv_controller_msg(&msg, &resp_msg,
						 working_cluster_rec) < 0)) {
		error("%s: Unable to signal jobs: %s",
		      __func__, slurm_strerror(rc));
		return rc;
	}

	switch(resp_msg.msg_type) {
	case RESPONSE_KILL_JOBS:
		*kill_msg_resp = resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return rc;
}

/*
 * Kill a job step with job id "job_id" and step id "step_id", optionally
 *	sending the processes in the job step a signal "signal"
 * IN job_id     - the job's id
 * IN step_id    - the job step's id
 * IN signal     - signal number
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int slurm_kill_job_step(uint32_t job_id, uint32_t step_id,
			       uint16_t signal, uint16_t flags)
{
	int rc;
	slurm_msg_t msg;
	job_step_kill_msg_t req;

	slurm_msg_t_init(&msg);
	/*
	 * Request message:
	 */
	memset(&req, 0, sizeof(job_step_kill_msg_t));
	req.step_id.job_id = job_id;
	req.sjob_id     = NULL;
	req.step_id.step_id = step_id;
	req.step_id.step_het_comp = NO_VAL;
	req.signal      = signal;
	req.flags = flags;
	msg.msg_type    = REQUEST_CANCEL_JOB_STEP;
	msg.data        = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc, working_cluster_rec)<0)
		return SLURM_ERROR;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}

extern int slurm_kill_job2(const char *job_id, uint16_t signal, uint16_t flags,
			   const char *sibling)
{
	return _slurm_kill_job_internal(0, job_id, sibling, signal, flags);
}
