/*****************************************************************************\
 *  suspend.c - job step suspend and resume functions.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "slurm/slurm.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * _suspend_op - perform a suspend/resume operation for some job.
 * IN op         - operation to perform
 * IN job_id     - job on which to perform operation or NO_VAL
 * RET 0 or a slurm error code
 * NOTE: Supply either job_id NO_VAL or job_id_str as NULL, not both
 */
static int _suspend_op(uint16_t op, uint32_t job_id)
{
	int rc = SLURM_SUCCESS;
	suspend_msg_t sus_req;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	sus_req.op         = op;
	sus_req.job_id     = job_id;
	sus_req.job_id_str = NULL;
	req_msg.msg_type   = REQUEST_SUSPEND;
	req_msg.data       = &sus_req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
		return SLURM_ERROR;

	slurm_seterrno(rc);
	return rc;
}

/*
 * slurm_suspend - suspend execution of a job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_suspend(uint32_t job_id)
{
	return _suspend_op (SUSPEND_JOB, job_id);
}

/*
 * slurm_resume - resume execution of a previously suspended job.
 * IN job_id  - job on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_resume(uint32_t job_id)
{
	return _suspend_op(RESUME_JOB, job_id);
}

/*
 * _suspend_op2 - perform a suspend/resume operation for some job.
 * IN op         - operation to perform
 * IN job_id_str - job on which to perform operation in string format or NULL
 * OUT resp      - slurm error codes by job array task ID
 * RET 0 or a slurm error code
 * NOTE: Supply either job_id NO_VAL or job_id_str as NULL, not both
 */
static int _suspend_op2(uint16_t op, char *job_id_str,
			job_array_resp_msg_t **resp)
{
	int rc = SLURM_SUCCESS;
	suspend_msg_t sus_req;
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	sus_req.op         = op;
	sus_req.job_id     = NO_VAL;
	sus_req.job_id_str = job_id_str;
	req_msg.msg_type   = REQUEST_SUSPEND;
	req_msg.data       = &sus_req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_ARRAY_ERRORS:
		*resp = (job_array_resp_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		if (rc)
			slurm_seterrno(rc);
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return rc;
}

/*
 * slurm_suspend2 - suspend execution of a job.
 * IN job_id in string form  - job on which to perform operation
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_suspend2(char *job_id, job_array_resp_msg_t **resp)
{
	return _suspend_op2(SUSPEND_JOB, job_id, resp);
}


/*
 * slurm_resume2 - resume execution of a previously suspended job.
 * IN job_id in string form  - job on which to perform operation
 * OUT resp - per task response to the request,
 *	      free using slurm_free_job_array_resp()
 * RET 0 or a slurm error code
 */
extern int slurm_resume2(char *job_id, job_array_resp_msg_t **resp)
{
	return _suspend_op2(RESUME_JOB, job_id, resp);
}

/*
 * slurm_requeue - re-queue a batch job, if already running
 *	then terminate it first
 * IN job_id     - job on which to perform operation
 * IN state      - state in which to place the job
 * RET 0 or a slurm error code
 */
extern int slurm_requeue(uint32_t job_id, uint32_t state)
{
	int rc = SLURM_SUCCESS;
	requeue_msg_t requeue_req;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);

	requeue_req.job_id	= job_id;
	requeue_req.job_id_str	= NULL;
	requeue_req.state	= state;
	req_msg.msg_type	= REQUEST_JOB_REQUEUE;
	req_msg.data		= &requeue_req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
		return SLURM_ERROR;

	slurm_seterrno(rc);
	return rc;
}

/*
 * slurm_requeue2 - re-queue a batch job, if already running
 *	then terminate it first
 * IN job_id_str - job on which to perform operation in string format or NULL
 * IN state      - state in which to place the job
 * RET 0 or a slurm error code
 */
extern int slurm_requeue2(char *job_id_str, uint32_t state,
			  job_array_resp_msg_t **resp)
{
	int rc = SLURM_SUCCESS;
	requeue_msg_t requeue_req;
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	requeue_req.job_id	= NO_VAL;
	requeue_req.job_id_str	= job_id_str;
	requeue_req.state	= state;
	req_msg.msg_type	= REQUEST_JOB_REQUEUE;
	req_msg.data		= &requeue_req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);
	switch (resp_msg.msg_type) {
	case RESPONSE_JOB_ARRAY_ERRORS:
		*resp = (job_array_resp_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		if (rc)
			slurm_seterrno(rc);
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return rc;
}
