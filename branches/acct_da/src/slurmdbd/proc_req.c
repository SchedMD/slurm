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
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobacct_storage.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmctld/slurmctld.h"

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
 * uid IN/OUT - user ID who initiated the RPC
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
			error("Invalid RPC msg_type=%d", msg_type);
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
	int rc = SLURM_ERROR;

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

	info("DBD_CLUSTER_PROCS: CLUSTER_NAME:%s PROC_COUNT:%u TIME:%u", 
	     cluster_procs_msg->cluster_name, cluster_procs_msg->proc_count,
	     cluster_procs_msg->event_time);
	rc = clusteracct_storage_g_cluster_procs(
		cluster_procs_msg->cluster_name,
		cluster_procs_msg->proc_count,
		cluster_procs_msg->event_time);
	slurm_dbd_free_cluster_procs_msg(cluster_procs_msg);
	*out_buffer = make_dbd_rc_msg(rc);
	return rc;
}

static int _get_jobs(Buf in_buffer, Buf *out_buffer)
{
	int i;
	dbd_get_jobs_msg_t *get_jobs_msg;
	dbd_got_jobs_msg_t got_jobs_msg;
	dbd_job_info_t jobs[2];

	if (slurm_dbd_unpack_get_jobs_msg(&get_jobs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_GET_JOBS message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_GET_JOBS: JOB_COUNT:%u", get_jobs_msg->job_count);
	for (i=0; i<get_jobs_msg->job_count; i++) {
		info("DBD_GET_JOBS: JOB_ID[%d]:%u.%u", i, 
		     get_jobs_msg->job_ids[i], get_jobs_msg->step_ids[i]);
	}
	info("DBD_GET_JOBS: PART_COUNT:%u", get_jobs_msg->part_count);
	for (i=0; i<get_jobs_msg->part_count; i++) {
		info("DBD_GET_JOBS: PART_NAME[%d]:%s", i, 
		     get_jobs_msg->part_name[i]);
	}
	slurm_dbd_free_get_jobs_msg(get_jobs_msg);

	got_jobs_msg.job_count	= 2;
	got_jobs_msg.job_info	= jobs;
	jobs[0].block_id	= "block0";
	jobs[0].job_id		= 1234;
	jobs[0].name		= "name0";
	jobs[0].nodes		= "nodes0";
	jobs[0].part_name	= "part0";
	jobs[1].block_id	= "block1";
	jobs[1].job_id		= 5678;
	jobs[1].name		= "name1";
	jobs[1].nodes		= "nodes1";
	jobs[1].part_name	= "part1";
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_JOBS, *out_buffer);
	slurm_dbd_pack_got_jobs_msg(&got_jobs_msg, *out_buffer);
	return SLURM_SUCCESS;
}


static int _init_conn(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_init_msg_t *init_msg;

	if (slurm_dbd_unpack_init_msg(&init_msg, in_buffer, 
				      slurmdbd_conf->auth_info) != SLURM_SUCCESS) {
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

	info("DBD_INIT: VERSION:%u UID:%u", init_msg->version, init_msg->uid);
	slurm_dbd_free_init_msg(init_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int  _job_complete(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_comp_msg_t *job_comp_msg;
	struct job_record job;
	struct job_details details;
	
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

	info("DBD_JOB_COMPLETE: ID:%u NAME:%s", 
	     job_comp_msg->job_id, job_comp_msg->name);

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));
	job.details = &details;
	job.job_id = job_comp_msg->job_id;
	job.assoc_id = job_comp_msg->assoc_id;
	job.db_index = job_comp_msg->db_index;
	job.name = job_comp_msg->name;
	job.nodes = job_comp_msg->nodes;

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

	info("DBD_JOB_START: ID:%u NAME:%s", 
	     job_start_msg->job_id, job_start_msg->name);
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

	info("DBD_JOB_SUSPEND: ID:%u STATE:%s", 
	     job_suspend_msg->job_id, 
	     job_state_string((enum job_states) job_suspend_msg->job_state));
	slurm_dbd_free_job_suspend_msg(job_suspend_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int _node_state(Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_node_state_msg_t *node_state_msg;
	struct node_record node_ptr;

	memset(&node_ptr, 0, sizeof(struct node_record));

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

	info("DBD_NODE_STATE: NODE:%s STATE:%s REASON:%s TIME:%u", 
	     node_state_msg->hostlist,
	     _node_state_string(node_state_msg->new_state),
	     node_state_msg->reason, 
	     node_state_msg->event_time);
	node_ptr.name = node_state_msg->hostlist;

	slurmctld_conf.fast_schedule = 0;

	if(node_state_msg->new_state == DBD_NODE_STATE_DOWN)
		clusteracct_storage_g_node_down(node_state_msg->cluster_name,
						&node_ptr,
						node_state_msg->event_time,
						node_state_msg->reason);
	else
		clusteracct_storage_g_node_up(node_state_msg->cluster_name,
					      &node_ptr,
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

	info("DBD_STEP_COMPLETE: ID:%u.%u NAME:%s", 
	     step_comp_msg->job_id, step_comp_msg->step_id,
	     step_comp_msg->name);
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

	info("DBD_STEP_START: ID:%u.%u NAME:%s", 
	     step_start_msg->job_id, step_start_msg->step_id,
	     step_start_msg->name);
	slurm_dbd_free_step_start_msg(step_start_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}
