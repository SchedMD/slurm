/*****************************************************************************\
 *  checkpoint.c - Process checkpoint related functions.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/checkpoint.h"
#include "src/common/slurm_protocol_api.h"

extern char * __progname;

static int _handle_rc_msg(slurm_msg_t *msg);
static int _checkpoint_op (uint16_t op, uint16_t data,
			   uint32_t job_id, uint32_t step_id,
			   char *image_dir);
/*
 * _checkpoint_op - perform many checkpoint operation for some job step.
 * IN op        - operation to perform
 * IN data      - operation-specific data
 * IN job_id    - job on which to perform operation
 * IN step_id   - job step on which to perform operation
 * IN image_dir - directory used to get/put checkpoint images
 * RET 0 or a slurm error code
 */
static int _checkpoint_op (uint16_t op, uint16_t data,
			   uint32_t job_id, uint32_t step_id,
			   char *image_dir)
{
	int rc;
	checkpoint_msg_t ckp_req;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	ckp_req.op        = op;
	ckp_req.data      = data;
	ckp_req.job_id    = job_id;
	ckp_req.step_id   = step_id;
	ckp_req.image_dir = image_dir;
	req_msg.msg_type  = REQUEST_CHECKPOINT;
	req_msg.data      = &ckp_req;

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc,
					      working_cluster_rec) < 0)
		return SLURM_ERROR;

	slurm_seterrno(rc);
	return rc;
}

/*
 * slurm_checkpoint_able - determine if the specified job step can presently
 *	be checkpointed
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * OUT start_time - time at which checkpoint request was issued
 * RET 0 (can be checkpoined) or a slurm error code
 */
extern int slurm_checkpoint_able (uint32_t job_id, uint32_t step_id,
		time_t *start_time)
{
	int rc;
	slurm_msg_t req_msg, resp_msg;
	checkpoint_msg_t ckp_req;
	checkpoint_resp_msg_t *resp;

	ckp_req.op        = CHECK_ABLE;
	ckp_req.job_id    = job_id;
	ckp_req.step_id   = step_id;
	ckp_req.image_dir = NULL;
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type  = REQUEST_CHECKPOINT;
	req_msg.data      = &ckp_req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_CHECKPOINT:
		resp = (checkpoint_resp_msg_t *) resp_msg.data;
		*start_time = resp->event_time;
		slurm_free_checkpoint_resp_msg(resp_msg.data);
		rc = SLURM_SUCCESS;
		break;
	case RESPONSE_SLURM_RC:
		rc = _handle_rc_msg(&resp_msg);
		break;
	default:
		*start_time = (time_t) NULL;
		rc = SLURM_ERROR;
	}
	return rc;
}

/*
 * slurm_checkpoint_disable - disable checkpoint requests for some job step
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_disable (uint32_t job_id, uint32_t step_id)
{
	return _checkpoint_op (CHECK_DISABLE, 0, job_id, step_id, NULL);
}


/*
 * slurm_checkpoint_enable - enable checkpoint requests for some job step
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_enable (uint32_t job_id, uint32_t step_id)
{
	return _checkpoint_op (CHECK_ENABLE, 0, job_id, step_id, NULL);
}

/*
 * slurm_checkpoint_create - initiate a checkpoint requests for some job step.
 *	the job will continue execution after the checkpoint operation completes
 * IN job_id   - job on which to perform operation
 * IN step_id  - job step on which to perform operation
 * IN max_wait  - maximum wait for operation to complete, in seconds
 * IN image_dir - directory used to get/put checkpoint images
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_create (uint32_t job_id, uint32_t step_id,
		uint16_t max_wait, char *image_dir)
{
	return _checkpoint_op (CHECK_CREATE, max_wait, job_id, step_id,
			       image_dir);
}

/*
 * slurm_checkpoint_requeue - initiate a checkpoint requests for some job.
 *	the job will be requeued after the checkpoint operation completes
 * IN job_id  - job on which to perform operation
 * IN max_wait - maximum wait for operation to complete, in seconds
 * IN image_dir - directory used to get/put checkpoint images
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_requeue (uint32_t job_id, uint16_t max_wait,
				     char *image_dir)
{
	return _checkpoint_op (CHECK_REQUEUE, max_wait, job_id,
			       (uint32_t) SLURM_BATCH_SCRIPT, image_dir);
}

/*
 * slurm_checkpoint_vacate - initiate a checkpoint requests for some job step.
 *	the job will terminate after the checkpoint operation completes
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * IN max_wait - maximum wait for operation to complete, in seconds
 * IN image_dir - directory used to get/put checkpoint images
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_vacate (uint32_t job_id, uint32_t step_id,
		uint16_t max_wait, char *image_dir)
{
	return _checkpoint_op (CHECK_VACATE, max_wait, job_id, step_id,
			       image_dir);
}

/*
 * slurm_checkpoint_restart - restart execution of a checkpointed job step.
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_restart (uint32_t job_id, uint32_t step_id,
				     uint16_t stick, char *image_dir)
{
	return _checkpoint_op (CHECK_RESTART, stick, job_id, step_id, image_dir);
}

/*
 * slurm_checkpoint_complete - note the completion of a job step's checkpoint
 *	operation.
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * IN begin_time - time at which checkpoint began
 * IN error_code - error code, highest value for all complete calls is preserved
 * IN error_msg - error message, preserved for highest error_code
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_complete (uint32_t job_id, uint32_t step_id,
		time_t begin_time, uint32_t error_code, char *error_msg)
{
	int rc;
	slurm_msg_t msg;
	checkpoint_comp_msg_t req;

	slurm_msg_t_init(&msg);
	req.job_id       = job_id;
	req.step_id      = step_id;
	req.begin_time   = begin_time;
	req.error_code   = error_code;
	req.error_msg    = error_msg;
	msg.msg_type     = REQUEST_CHECKPOINT_COMP;
	msg.data         = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc,
					      working_cluster_rec) < 0)
		return SLURM_ERROR;
	if (rc)
		slurm_seterrno_ret(rc);
	return SLURM_SUCCESS;
}

/*
 * slurm_checkpoint_error - gather error information for the last checkpoint
 *	operation for some job step
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * OUT error_code - error number associated with the last checkpoint operation,
 *	this value is dependent upon the checkpoint plugin used and may be
 *	completely unrelated to slurm error codes, the highest value for all
 *	complete calls is preserved
 * OUT error_msg - error message, preserved for highest error_code, value
 *	must be freed by the caller to prevent memory leak
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_error (uint32_t job_id, uint32_t step_id,
				   uint32_t *error_code, char **error_msg)
{
	int rc;
	slurm_msg_t msg;
	checkpoint_msg_t req;
	slurm_msg_t resp_msg;
	checkpoint_resp_msg_t *ckpt_resp;

	if ((error_code == NULL) || (error_msg == NULL))
		return EINVAL;

	/*
	 * Request message:
	 */
	req.op        = CHECK_ERROR;
	req.job_id    = job_id;
	req.step_id   = step_id;
	req.image_dir = NULL;
	slurm_msg_t_init(&msg);
	slurm_msg_t_init(&resp_msg);
	msg.msg_type  = REQUEST_CHECKPOINT;
	msg.data      = &req;

	rc = slurm_send_recv_controller_msg(&msg, &resp_msg,
					    working_cluster_rec);

	if (rc == SLURM_SOCKET_ERROR)
		return rc;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		*error_code = 0;
		*error_msg = strdup("");
		rc = _handle_rc_msg(&resp_msg);
		break;
	case RESPONSE_CHECKPOINT:
		ckpt_resp = (checkpoint_resp_msg_t *) resp_msg.data;
		*error_code = ckpt_resp->error_code;
		if (ckpt_resp->error_msg)
			*error_msg = strdup(ckpt_resp->error_msg);
		else
			*error_msg = strdup("");
		slurm_free_checkpoint_resp_msg(ckpt_resp);
		rc = SLURM_SUCCESS;
		break;
	default:
		rc = SLURM_UNEXPECTED_MSG_ERROR;
	}

	return rc;
}

/*
 *  Handle a return code message type.
 *    Sets errno to return code and returns it
 */
static int
_handle_rc_msg(slurm_msg_t *msg)
{
	int rc = ((return_code_msg_t *) msg->data)->return_code;
	slurm_free_return_code_msg(msg->data);
	slurm_seterrno(rc);
	return rc;
}

/*
 * slurm_checkpoint_task_complete - note the completion of a task's checkpoint
 *	operation.
 * IN job_id  - job on which to perform operation
 * IN step_id - job step on which to perform operation
 * IN task_id - task which completed the operation
 * IN begin_time - time at which checkpoint began
 * IN error_code - error code, highest value for all complete calls is preserved
 * IN error_msg - error message, preserved for highest error_code
 * RET 0 or a slurm error code
 */
extern int slurm_checkpoint_task_complete (uint32_t job_id, uint32_t step_id,
					   uint32_t task_id, time_t begin_time,
					   uint32_t error_code,
					   char *error_msg)
{
	int rc;
	slurm_msg_t msg;
	checkpoint_task_comp_msg_t req;

	slurm_msg_t_init(&msg);
	req.job_id       = job_id;
	req.step_id      = step_id;
	req.task_id      = task_id;
	req.begin_time   = begin_time;
	req.error_code   = error_code;
	req.error_msg    = error_msg;
	msg.msg_type     = REQUEST_CHECKPOINT_TASK_COMP;
	msg.data         = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc,
					      working_cluster_rec) < 0)
		return SLURM_ERROR;
	if (rc)
		slurm_seterrno_ret(rc);
	return SLURM_SUCCESS;
}

/*
 * slurm_checkpoint_tasks - send checkpoint request to tasks of
 *     specified step
 * IN job_id: job ID of step
 * IN step_id: step ID of step
 * IN image_dir: location to store ckpt images. parameter to plugin.
 * IN max_wait: seconds to wait for the operation to complete
 * IN nodelist: nodes to send the request
 * RET: 0 on success, non-zero on failure with errno set
 */
extern int
slurm_checkpoint_tasks(uint32_t job_id, uint16_t step_id, time_t begin_time,
		       char *image_dir, uint16_t max_wait, char *nodelist)
{
	return checkpoint_tasks(job_id, step_id, begin_time,
				image_dir, max_wait, nodelist);
}
