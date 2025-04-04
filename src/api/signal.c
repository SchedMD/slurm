/*****************************************************************************\
 *  signal.c - Send a signal to a slurm job or job step
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>.
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
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/xmalloc.h"
#include "src/common/hostlist.h"
#include "src/common/read_config.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"

static int _local_send_recv_rc_msgs(const char *nodelist,
				    slurm_msg_type_t type, void *data)
{
	list_t *ret_list = NULL;
	int temp_rc = 0, rc = 0;
	ret_data_info_t *ret_data_info = NULL;
	slurm_msg_t *msg = xmalloc(sizeof(slurm_msg_t));

	slurm_msg_t_init(msg);
	slurm_msg_set_r_uid(msg, SLURM_AUTH_UID_ANY);
	msg->msg_type = type;
	msg->data = data;

	if ((ret_list = slurm_send_recv_msgs(nodelist, msg, 0))) {
		while ((ret_data_info = list_pop(ret_list))) {
			temp_rc = slurm_get_return_code(ret_data_info->type,
							ret_data_info->data);
			if (temp_rc)
				rc = temp_rc;
		}
	} else {
		error("slurm_signal_job: no list was returned");
		rc = SLURM_ERROR;
	}

	/* don't attempt to free a local variable */
	msg->data = NULL;

	slurm_free_msg(msg);
	return rc;
}

static int _signal_batch_script_step(const resource_allocation_response_msg_t
				     *allocation, uint32_t signal)
{
	slurm_msg_t msg;
	signal_tasks_msg_t rpc;
	int rc = SLURM_SUCCESS;
	char *name = allocation->batch_host;

	if (!name) {
		error("%s: No batch_host in allocation", __func__);
		return -1;
	}
	memset(&rpc, 0, sizeof(rpc));
	rpc.step_id.job_id = allocation->job_id;
	rpc.step_id.step_id = SLURM_BATCH_SCRIPT;
	rpc.step_id.step_het_comp = NO_VAL;
	rpc.signal = signal;
	rpc.flags = KILL_JOB_BATCH;

	slurm_msg_t_init(&msg);
	slurm_msg_set_r_uid(&msg, slurm_conf.slurmd_user_id);
	msg.msg_type = REQUEST_SIGNAL_TASKS;
	msg.data = &rpc;
	if (slurm_conf_get_addr(name, &msg.address, msg.flags)
	    == SLURM_ERROR) {
		error("%s: can't find address for host %s, check slurm.conf",
		      __func__, name);
		return -1;
	}
	if (slurm_send_recv_rc_msg_only_one(&msg, &rc, 0) < 0) {
		error("%s: %m", __func__);
		rc = -1;
	}
	return rc;
}

static int _signal_job_step(const job_step_info_t *step, uint16_t signal)
{
	signal_tasks_msg_t rpc;
	int rc = SLURM_SUCCESS;

	/* same remote procedure call for each node */
	memset(&rpc, 0, sizeof(rpc));
	memcpy(&rpc.step_id, &step->step_id, sizeof(rpc.step_id));
	rpc.signal = signal;

	rc = _local_send_recv_rc_msgs(step->nodes,
				      REQUEST_SIGNAL_TASKS, &rpc);
	return rc;
}

static int _terminate_batch_script_step(const resource_allocation_response_msg_t
					* allocation)
{
	slurm_msg_t msg;
	signal_tasks_msg_t rpc;
	int rc = SLURM_SUCCESS;
	int i;
	char *name = allocation->batch_host;

	if (!name) {
		error("%s: No batch_host in allocation", __func__);
		return -1;
	}

	memset(&rpc, 0, sizeof(rpc));
	rpc.step_id.job_id = allocation->job_id;
	rpc.step_id.step_id = SLURM_BATCH_SCRIPT;
	rpc.step_id.step_het_comp = NO_VAL;
	rpc.signal = (uint16_t)-1; /* not used by slurmd */

	slurm_msg_t_init(&msg);
	msg.msg_type = REQUEST_TERMINATE_TASKS;
	slurm_msg_set_r_uid(&msg, slurm_conf.slurmd_user_id);
	msg.data = &rpc;

	if (slurm_conf_get_addr(name, &msg.address, msg.flags)
	    == SLURM_ERROR) {
		error("%s: " "can't find address for host %s, check slurm.conf",
		      __func__, name);
		return -1;
	}
	i = slurm_send_recv_rc_msg_only_one(&msg, &rc, 0);
	if (i != 0)
		rc = i;

	return rc;
}

/*
 * Send a REQUEST_TERMINATE_TASKS rpc to all nodes in a job step.
 *
 * RET Upon successful termination of the job step, 0 shall be returned.
 * Otherwise, -1 shall be returned and errno set to indicate the error.
 */
static int _terminate_job_step(const job_step_info_t *step)
{
	signal_tasks_msg_t rpc;
	int rc = SLURM_SUCCESS;

	/*
	 *  Send REQUEST_TERMINATE_TASKS to all nodes of the step
	 */
	memset(&rpc, 0, sizeof(rpc));
	memcpy(&rpc.step_id, &step->step_id, sizeof(rpc.step_id));
	rpc.signal = (uint16_t)-1; /* not used by slurmd */
	rc = _local_send_recv_rc_msgs(step->nodes,
				      REQUEST_TERMINATE_TASKS, &rpc);
	if ((rc == -1) && (errno == ESLURM_ALREADY_DONE)) {
		rc = 0;
		errno = 0;
	}

	return rc;
}

/*
 * slurm_signal_job - send the specified signal to all steps of an existing job
 * IN job_id     - the job's id
 * IN signal     - signal number
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int
slurm_signal_job (uint32_t job_id, uint16_t signal)
{
	int rc = SLURM_SUCCESS;
	resource_allocation_response_msg_t *alloc_info = NULL;
	signal_tasks_msg_t rpc;

	if (slurm_allocation_lookup(job_id, &alloc_info)) {
		rc = errno;
		goto fail1;
	}

	/* same remote procedure call for each node */
	memset(&rpc, 0, sizeof(rpc));
	rpc.step_id.job_id = job_id;
	rpc.step_id.step_id = NO_VAL;
	rpc.step_id.step_het_comp = NO_VAL;
	rpc.signal = signal;
	rpc.flags = KILL_STEPS_ONLY;

	rc = _local_send_recv_rc_msgs(alloc_info->node_list,
				      REQUEST_SIGNAL_TASKS, &rpc);
	slurm_free_resource_allocation_response_msg(alloc_info);
fail1:
	if (rc) {
		slurm_seterrno_ret(rc);
	} else {
		return SLURM_SUCCESS;
	}
}

/*
 * slurm_signal_job_step - send the specified signal to an existing job step
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to send a signal to a job's batch script
 * IN signal  - signal number
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int
slurm_signal_job_step (uint32_t job_id, uint32_t step_id, uint32_t signal)
{
	job_step_info_response_msg_t *step_info = NULL;
	int rc;
	int i;
	int save_errno = 0;

	/*
	 * The controller won't give us info about the batch script job step,
	 * so we need to handle that separately.
	 */
	if (step_id == SLURM_BATCH_SCRIPT) {
		resource_allocation_response_msg_t *alloc_info = NULL;
		if (slurm_allocation_lookup(job_id, &alloc_info))
			return -1;

		rc = _signal_batch_script_step(alloc_info, signal);
		slurm_free_resource_allocation_response_msg(alloc_info);
		errno = rc;
		return rc ? -1 : 0;
	}

	/*
	 * Otherwise, look through the list of job step info and find
	 * the one matching step_id.  Signal that step.
	 */
	rc = slurm_get_job_steps((time_t)0, job_id, step_id,
				 &step_info, SHOW_ALL);
 	if (rc != 0) {
 		save_errno = errno;
 		goto fail;
 	}
	for (i = 0; i < step_info->job_step_count; i++) {
		if ((step_info->job_steps[i].step_id.job_id == job_id) &&
		    (step_info->job_steps[i].step_id.step_id == step_id)) {
 			rc = _signal_job_step(&step_info->job_steps[i],
 					      signal);
 			save_errno = rc;
			break;
		}
	}
	slurm_free_job_step_info_response_msg(step_info);
fail:
 	errno = save_errno;
 	return rc ? -1 : 0;
}

/*
 * slurm_terminate_job_step - terminates a job step by sending a
 * 	REQUEST_TERMINATE_TASKS rpc to all slurmd of a job step.
 * IN job_id  - the job's id
 * IN step_id - the job step's id - use SLURM_BATCH_SCRIPT as the step_id
 *              to terminate a job's batch script
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 */
extern int
slurm_terminate_job_step (uint32_t job_id, uint32_t step_id)
{
	job_step_info_response_msg_t *step_info = NULL;
	int rc = 0;
	int i;
	int save_errno = 0;

	/*
	 * The controller won't give us info about the batch script job step,
	 * so we need to handle that separately.
	 */
	if (step_id == SLURM_BATCH_SCRIPT) {
		resource_allocation_response_msg_t *alloc_info = NULL;
		if (slurm_allocation_lookup(job_id, &alloc_info))
			return -1;

		rc = _terminate_batch_script_step(alloc_info);
		slurm_free_resource_allocation_response_msg(alloc_info);
		errno = rc;
		return rc ? -1 : 0;
	}

	/*
	 * Otherwise, look through the list of job step info and find
	 * the one matching step_id.  Terminate that step.
	 */
	rc = slurm_get_job_steps((time_t)0, job_id, step_id,
				 &step_info, SHOW_ALL);
	if (rc != 0) {
		save_errno = errno;
		goto fail;
	}
	for (i = 0; i < step_info->job_step_count; i++) {
		if ((step_info->job_steps[i].step_id.job_id == job_id) &&
		    (step_info->job_steps[i].step_id.step_id == step_id)) {
			rc = _terminate_job_step(&step_info->job_steps[i]);
			save_errno = errno;
			break;
		}
	}
	slurm_free_job_step_info_response_msg(step_info);
fail:
	errno = save_errno;
	return rc ? -1 : 0;
}

/*
 * slurm_notify_job - send message to the job's stdout,
 *	usable only by user root
 * IN job_id - slurm job_id or 0 for all jobs
 * IN message - arbitrary message
 * RET 0 or -1 on error
 */
extern int slurm_notify_job (uint32_t job_id, char *message)
{
	int rc;
	slurm_msg_t msg;
	job_notify_msg_t req;

	slurm_msg_t_init(&msg);
	/*
	 * Request message:
	 */
	memset(&req, 0, sizeof(req));
	req.step_id.job_id = job_id;
	req.step_id.step_id = NO_VAL;	/* currently not used */
	req.step_id.step_het_comp = NO_VAL;	/* currently not used */
	req.message     = message;
	msg.msg_type    = REQUEST_JOB_NOTIFY;
	msg.data        = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc,
					      working_cluster_rec) < 0)
		return SLURM_ERROR;

	if (rc) {
		slurm_seterrno_ret(rc);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
