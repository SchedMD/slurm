/*****************************************************************************\
 *  checkpoint.c - Process checkpoint related functions.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <slurm/slurm.h>

#include "src/common/checkpoint.h"
#include "src/common/slurm_protocol_api.h"

static int _handle_rc_msg(slurm_msg_t *msg);
static int _checkpoint_op (uint16_t op, uint16_t data,
		uint32_t job_id, uint32_t step_id);
/*
 * _checkpoint_op - perform some checkpoint operation for some job step
 * IN op      - operation to perform
 * IN data   - operation-specific data
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
static int _checkpoint_op (uint16_t op, uint16_t data,
		uint32_t job_id, uint32_t step_id)
{
	int rc;
	slurm_msg_t msg;
	checkpoint_msg_t req;

	/*
	 * Request message:
	 */
	req.op       = op;
	req.job_id   = job_id;
	req.step_id  = step_id;
	msg.msg_type = REQUEST_CHECKPOINT;
	msg.data     = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc) < 0)
		return SLURM_ERROR;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}

/*
 * slurm_checkpoint_disable - disable checkpoint requests for some job step
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_disable (uint32_t job_id, uint32_t step_id)
{
	return _checkpoint_op (CHECK_DISABLE, 0, job_id, step_id);
}


/*
 * slurm_checkpoint_enable - enable checkpoint requests for some job step
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_enable (uint32_t job_id, uint32_t step_id)
{
	return _checkpoint_op (CHECK_ENABLE, 0, job_id, step_id);
}

/*
 * slurm_checkpoint_create - initiate a checkpoint requests for some job step.
 *	the job will continue execution after the checkpoint operation completes
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * IN max_wait - maximum wait for operation to complete, in seconds
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_create (uint32_t job_id, uint32_t step_id, 
		uint16_t max_wait)
{
	return _checkpoint_op (CHECK_CREATE, max_wait, job_id, step_id);
}

/*
 * slurm_checkpoint_vacate - initiate a checkpoint requests for some job step.
 *	the job will terminate after the checkpoint operation completes
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * IN max_wait - maximum wait for operation to complete, in seconds
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_vacate (uint32_t job_id, uint32_t step_id, 
		uint16_t max_wait)
{
	return _checkpoint_op (CHECK_VACATE, max_wait, job_id, step_id);
}

/*
 * slurm_checkpoint_resume - resume execution of a checkpointed job step.
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_resume (uint32_t job_id, uint32_t step_id)
{
	return _checkpoint_op (CHECK_RESUME, 0, job_id, step_id);
}

/*
 * slurm_checkpoint_complete - note the successful completion of a job step's 
 *	checkpoint operation.
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_complete (uint32_t job_id, uint32_t step_id)
{
	return _checkpoint_op (CHECK_COMPLETE, 0, job_id, step_id);
}

/*
 * slurm_checkpoint_failed - note the unsuccessful completion of a job step's 
 *	checkpoint operation.
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * IN ckpt_errno - plugin-specific error code indicative of the failure type
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_failed (uint32_t job_id, uint32_t step_id, 
		uint16_t ckpt_errno)
{
	return _checkpoint_op (CHECK_FAILED, ckpt_errno, job_id, step_id);
}

/*
 * slurm_checkpoint_error - gather error information for the last checkpoint operation 
 * for some job step
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * OUT ckpt_errno - error number associated with the last checkpoint operation,
 *	this value is dependent upon the checkpoint plugin used and may be
 *	completely unrelated to slurm error codes
 * OUT ckpt_strerror - string describing the message associated with the last 
 *	checkpoint operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_error ( uint32_t job_id, uint32_t step_id, 
		uint16_t *ckpt_errno, char **ckpt_strerror)
{
	int rc;
	slurm_msg_t msg;
	checkpoint_msg_t req;
	slurm_msg_t resp_msg;
	checkpoint_resp_msg_t *ckpt_resp;

	if ((ckpt_errno == NULL) || (ckpt_strerror == NULL))
		return EINVAL;

	/*
	 * Request message:
	 */
	req.op       = CHECK_ERROR;
	req.job_id   = job_id;
	req.step_id  = step_id;
	msg.msg_type = REQUEST_CHECKPOINT;
	msg.data     = &req;

	rc = slurm_send_recv_controller_msg(&msg, &resp_msg);

	if (rc == SLURM_SOCKET_ERROR) 
		return SLURM_SOCKET_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_PROTOCOL_ERROR;
		break;
	case RESPONSE_CHECKPOINT:
		ckpt_resp = (checkpoint_resp_msg_t *) resp_msg.data;
		*ckpt_errno = ckpt_resp->ckpt_errno;
		*ckpt_strerror = strdup(ckpt_resp->ckpt_strerror);
		slurm_free_checkpoint_resp_msg(ckpt_resp);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return SLURM_SUCCESS;
}

/*
 *  Handle a return code message type. 
 *    if return code is nonzero, sets errno to return code and returns < 0.
 *    Otherwise, returns 0 (SLURM_SUCCES)
 */
static int
_handle_rc_msg(slurm_msg_t *msg)
{
	int rc = ((return_code_msg_t *) msg->data)->return_code;
	slurm_free_return_code_msg(msg->data);

	if (rc) 
		slurm_seterrno_ret(rc);
	else
		return SLURM_SUCCESS;
}
