/*****************************************************************************\
 *  proc_req.c - functions for processing incoming RPCs.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"

/* Local functions */
static int   _cluster_procs(Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_jobs(Buf in_buffer, Buf *out_buffer);
static int   _init_conn(Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _job_complete(Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _job_start(Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _job_suspend(Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _node_state(Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static char *_node_state_string(uint16_t node_state);
static int   _step_complete(Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _step_start(Buf in_buffer, Buf *out_buffer, uint32_t *uid);

/* Process an incoming RPC
 * msg IN - incoming message
 * msg_size IN - size of msg in bytes
 * first IN - set if first message received on the socket
 * buffer OUT - outgoing response, must be freed by caller
 * RET SLURM_SUCCESS or error code */
extern int 
proc_req(char *msg, uint32_t msg_size, bool first, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	uint16_t msg_type;
	Buf in_buffer;

	in_buffer = create_buf(msg, msg_size); /* puts msg into buffer struct */
	safe_unpack16(&msg_type, in_buffer);

	if (first && (msg_type != DBD_INIT)) {
		error("Initial RPC not DBD_INIT type (%d)", msg_type);
		rc = EINVAL;
		*out_buffer = make_dbd_rc_msg(rc);
	} else {
		switch (msg_type) {
		case DBD_CLUSTER_PROCS:
			rc = _cluster_procs(in_buffer, out_buffer, uid);
			break;
		case DBD_GET_JOBS:
			rc = _get_jobs(in_buffer, out_buffer);
			break;
		case DBD_INIT:
			if (first)
				rc = _init_conn(in_buffer, out_buffer, uid);
			else {
				error("DBD_INIT sent after connection "
					"established");
				rc = EINVAL;
				*out_buffer = make_dbd_rc_msg(rc);
			}
			break;
		case DBD_JOB_COMPLETE:
			rc = _job_complete(in_buffer, out_buffer, uid);
			break;
		case DBD_JOB_START:
			rc = _job_start(in_buffer, out_buffer, uid);
			break;
		case DBD_JOB_SUSPEND:
			rc = _job_suspend(in_buffer, out_buffer, uid);
			break;
		case DBD_NODE_STATE:
			rc = _node_state(in_buffer, out_buffer, uid);
			break;
		case DBD_STEP_COMPLETE:
			rc = _step_complete(in_buffer, out_buffer, uid);
			break;
		case DBD_STEP_START:
			rc = _step_start(in_buffer, out_buffer, uid);
			break;
		default:
			error("invalid RPC msg_type=%d", msg_type);
			rc = EINVAL;
			*out_buffer = make_dbd_rc_msg(rc);
			break;
		}
	}

	xfer_buf_data(in_buffer);	/* delete in_buffer struct without 
					 * xfree of msg */
	return rc;

unpack_error:
	free_buf(in_buffer);
	return SLURM_ERROR;
}

static int _cluster_procs(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cluster_procs_msg_t *cluster_procs_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_CLUSTER_PROCS message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurm_dbd_unpack_cluster_procs_msg(&cluster_procs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_CLUSTER_PROCS message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_CLUSTER_PROCS: %s:%u@%u", 
	     cluster_procs_msg->cluster_name, cluster_procs_msg->proc_count,
	     cluster_procs_msg->event_time);
	slurm_dbd_free_cluster_procs_msg(cluster_procs_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int _get_jobs(Buf in_buffer, Buf *out_buffer)
{
	int i;
	dbd_get_jobs_msg_t *get_jobs_msg;
	dbd_got_jobs_msg_t got_jobs_msg;
	uint32_t jobs[2];

	if (slurm_dbd_unpack_get_jobs_msg(&get_jobs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_GET_JOBS message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_GET_JOBS: job count %u", get_jobs_msg->job_count);
	for (i=0; i<get_jobs_msg->job_count; i++)
		info("DBD_GET_JOBS: job_id[%d] %u", i, get_jobs_msg->job_ids[i]);
	slurm_dbd_free_get_jobs_msg(get_jobs_msg);

	got_jobs_msg.job_count = 2;
	jobs[0] = 1234;
	jobs[1] = 5678;
	got_jobs_msg.job_ids = jobs;
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_JOBS, *out_buffer);
	slurm_dbd_pack_got_jobs_msg(&got_jobs_msg, *out_buffer);
	return SLURM_SUCCESS;
}

static int _init_conn(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_init_msg_t *init_msg;

	if (slurm_dbd_unpack_init_msg(&init_msg, in_buffer) != SLURM_SUCCESS) {
		error("Failed to unpack DBD_INIT message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}
	if (init_msg->version != SLURM_DBD_VERSION) {
		error("Incompatable RPC version (%d != %d)",
			init_msg->version, SLURM_DBD_VERSION);
		return SLURM_ERROR;
	}
	*uid = init_msg->uid;

	info("DBD_INIT: %u from uid:%u", init_msg->version, init_msg->uid);
	slurm_dbd_free_init_msg(init_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int  _job_complete(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_comp_msg_t *job_comp_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_JOB_COMPLETE message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurm_dbd_unpack_job_complete_msg(&job_comp_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_COMPLETE message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_JOB_COMPLETE: %u", job_comp_msg->job_id);
	slurm_dbd_free_job_complete_msg(job_comp_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int  _job_start(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_start_msg_t *job_start_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_JOB_START message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurm_dbd_unpack_job_start_msg(&job_start_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_START message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_JOB_START: %u", job_start_msg->job_id);
	slurm_dbd_free_job_start_msg(job_start_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int  _job_suspend(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_suspend_msg_t *job_suspend_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_JOB_SUSPEND message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurm_dbd_unpack_job_suspend_msg(&job_suspend_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_SUSPEND message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_JOB_SUSPEND: %u", job_suspend_msg->job_id);
	slurm_dbd_free_job_suspend_msg(job_suspend_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int _node_state(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_node_state_msg_t *node_state_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_NODE_STATE message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurm_dbd_unpack_node_state_msg(&node_state_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_NODE_STATE message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_NODE_STATE: %s:%s:%s@%u", 
	     node_state_msg->hostlist,
	     _node_state_string(node_state_msg->new_state),
	     node_state_msg->reason, 
	     node_state_msg->event_time);
	slurm_dbd_free_node_state_msg(node_state_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static char *_node_state_string(uint16_t node_state)
{
	switch(node_state) {
		case DBD_NODE_STATE_DOWN:
			return "DOWN";
		case DBD_NODE_STATE_UP:
			return "UP";
	}
	return "UNKNOWN";
}

static int  _step_complete(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_comp_msg_t *step_comp_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_STEP_COMPLETE message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurm_dbd_unpack_step_complete_msg(&step_comp_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_STEP_COMPLETE message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_STEP_COMPLETE: %u.%u", 
	     step_comp_msg->job_id, step_comp_msg->step_id);
	slurm_dbd_free_step_complete_msg(step_comp_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int  _step_start(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_start_msg_t *step_start_msg;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_STEP_START message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurm_dbd_unpack_step_start_msg(&step_start_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_STEP_START message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_STEP_START: %u.%u", 
	     step_start_msg->job_id, step_start_msg->step_id);
	slurm_dbd_free_step_start_msg(step_start_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}
