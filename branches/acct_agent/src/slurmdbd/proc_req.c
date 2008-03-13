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
#include "src/common/jobacct_common.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmctld/slurmctld.h"

/* Local functions */
static int   _cluster_procs(void *db_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _get_assocs(void *db_conn, Buf in_buffer, Buf *out_buffer);
static int   _get_jobs(void *db_conn, Buf in_buffer, Buf *out_buffer);
static int   _get_users(void *db_conn, Buf in_buffer, Buf *out_buffer);
static int   _init_conn(void *db_conn, Buf in_buffer, Buf *out_buffer, 
			uint32_t *uid, uint16_t *port, char **cluster_name);
static int   _job_complete(void *db_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _job_start(void *db_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _job_suspend(void *db_conn,
			  Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _node_state(void *db_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static char *_node_state_string(uint16_t node_state);
static int   _step_complete(void *db_conn,
			    Buf in_buffer, Buf *out_buffer, uint32_t *uid);
static int   _step_start(void *db_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid);

/* Process an incoming RPC
 * msg IN - incoming message
 * msg_size IN - size of msg in bytes
 * first IN - set if first message received on the socket
 * buffer OUT - outgoing response, must be freed by caller
 * uid IN/OUT - user ID who initiated the RPC
 * port OUT - slurmctld port to get update notifications, set for DBD_INIT only
 * cluster_name OUT - cluster associated with message, set for DBD_INIT only
 * RET SLURM_SUCCESS or error code
 */
extern int 
proc_req(void *db_conn, char *msg, uint32_t msg_size, bool first, 
	 Buf *out_buffer, uint32_t *uid, uint16_t *port, char **cluster_name)
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
		case DBD_ADD_ACCOUNTS:
			break;
		case DBD_ADD_ACCOUNT_COORDS:
			break;
		case DBD_ADD_ASSOCS:
			break;
		case DBD_ADD_CLUSTERS:
			break;
		case DBD_ADD_USERS:
			break;
		case DBD_CLUSTER_PROCS:
			rc = _cluster_procs(db_conn,
					    in_buffer, out_buffer, uid);
			break;
		case DBD_GET_ACCOUNTS:
			break;
		case DBD_GET_ASSOCS:
			rc = _get_assocs(db_conn, in_buffer, out_buffer);
			break;
		case DBD_GET_ASSOC_DAY:
			break;
		case DBD_GET_ASSOC_HOUR:
			break;
		case DBD_GET_ASSOC_MONTH:
			break;
		case DBD_GET_CLUSTERS:
			break;
		case DBD_GET_CLUSTER_HOUR:
			break;
		case DBD_GET_CLUSTER_DAY:
			break;
		case DBD_GET_CLUSTER_MONTH:
			break;
		case DBD_GET_JOBS:
			rc = _get_jobs(db_conn, in_buffer, out_buffer);
			break;
		case DBD_GET_USERS:
			rc = _get_users(db_conn, in_buffer, out_buffer);
			break;
		case DBD_INIT:
			if (first)
				rc = _init_conn(db_conn,
						in_buffer, out_buffer, uid,
						port, cluster_name);
			else {
				error("DBD_INIT sent after connection "
				      "established");
				rc = EINVAL;
				*out_buffer = make_dbd_rc_msg(rc);
			}
			break;
		case DBD_JOB_COMPLETE:
			rc = _job_complete(db_conn,
					   in_buffer, out_buffer, uid);
			break;
		case DBD_JOB_START:
			rc = _job_start(db_conn,
					in_buffer, out_buffer, uid);
			break;
		case DBD_JOB_SUSPEND:
			rc = _job_suspend(db_conn,
					  in_buffer, out_buffer, uid);
			break;
		case DBD_MODIFY_ACCOUNTS:
			break;
		case DBD_MODIFY_ASSOCS:
			break;
		case DBD_MODIFY_CLUSTERS:
			break;
		case DBD_MODIFY_USERS:
			break;
		case DBD_MODIFY_USER_ADMIN_LEVEL:
			break;
		case DBD_NODE_STATE:
			rc = _node_state(db_conn,
					 in_buffer, out_buffer, uid);
			break;
		case DBD_REMOVE_ACCOUNTS:
			break;
		case DBD_REMOVE_ACCOUNT_COORDS:
			break;
		case DBD_REMOVE_ASSOCS:
			break;
		case DBD_REMOVE_CLUSTERS:
			break;
		case DBD_REMOVE_USERS:
			break;
		case DBD_STEP_COMPLETE:
			rc = _step_complete(db_conn,
					    in_buffer, out_buffer, uid);
			break;
		case DBD_STEP_START:
			rc = _step_start(db_conn,
					 in_buffer, out_buffer, uid);
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

static int _cluster_procs(void *db_conn,
			  Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_cluster_procs_msg_t *cluster_procs_msg;
	int rc = SLURM_ERROR;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_CLUSTER_PROCS message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_cluster_procs_msg(&cluster_procs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_CLUSTER_PROCS message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	info("DBD_CLUSTER_PROCS: CLUSTER_NAME:%s PROC_COUNT:%u TIME:%u", 
	     cluster_procs_msg->cluster_name, cluster_procs_msg->proc_count,
	     cluster_procs_msg->event_time);
	rc = clusteracct_storage_g_cluster_procs(
		db_conn,
		cluster_procs_msg->cluster_name,
		cluster_procs_msg->proc_count,
		cluster_procs_msg->event_time);
	slurmdbd_free_cluster_procs_msg(cluster_procs_msg);
	*out_buffer = make_dbd_rc_msg(rc);
	return rc;
}

static int _get_assocs(void *db_conn, Buf in_buffer, Buf *out_buffer)
{
	dbd_cond_msg_t *get_msg;
	dbd_list_msg_t list_msg;

	if (slurmdbd_unpack_cond_msg(DBD_GET_ASSOCS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_GET_ASSOCS message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}
	
	info("DBD_GET_ASSOCS: called");

	list_msg.my_list = acct_storage_g_get_associations(
		db_conn, get_msg->cond);
	slurmdbd_free_cond_msg(DBD_GET_ASSOCS, get_msg);


	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_ASSOCS, *out_buffer);
	slurmdbd_pack_list_msg(DBD_GOT_ASSOCS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	info("DBD_GET_ASSOCS: done");
	
	return SLURM_SUCCESS;
}

static int _get_jobs(void *db_conn, Buf in_buffer, Buf *out_buffer)
{
	dbd_get_jobs_msg_t *get_jobs_msg;
	dbd_list_msg_t list_msg;
	sacct_parameters_t sacct_params;

	if (slurmdbd_unpack_get_jobs_msg(&get_jobs_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_GET_JOBS message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}
	
	info("DBD_GET_JOBS: called");
	memset(&sacct_params, 0, sizeof(sacct_params));
	sacct_params.opt_cluster = get_jobs_msg->cluster_name;

	list_msg.my_list = jobacct_storage_g_get_jobs(
		db_conn,
		get_jobs_msg->selected_steps, get_jobs_msg->selected_parts,
		&sacct_params);
	slurmdbd_free_get_jobs_msg(get_jobs_msg);


	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_JOBS, *out_buffer);
	slurmdbd_pack_list_msg(DBD_GOT_JOBS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	info("DBD_GET_JOBS: done");
	
	return SLURM_SUCCESS;
}

static int _get_users(void *db_conn, Buf in_buffer, Buf *out_buffer)
{
	dbd_cond_msg_t *get_msg;
	dbd_list_msg_t list_msg;

	if (slurmdbd_unpack_cond_msg(DBD_GET_USERS, &get_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_GET_USERS message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}
	
	info("DBD_GET_USERS: called");

	list_msg.my_list = acct_storage_g_get_users(db_conn, get_msg->cond);
	slurmdbd_free_cond_msg(DBD_GET_USERS, get_msg);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_USERS, *out_buffer);
	slurmdbd_pack_list_msg(DBD_GOT_USERS, &list_msg, *out_buffer);
	if(list_msg.my_list)
		list_destroy(list_msg.my_list);
	info("DBD_GET_USERS: done");
	
	return SLURM_SUCCESS;
}

static int _init_conn(void *db_conn, Buf in_buffer, Buf *out_buffer, 
		      uint32_t *uid, uint16_t *port, char **cluster_name)
{
	dbd_init_msg_t *init_msg;

	if (slurmdbd_unpack_init_msg(&init_msg, in_buffer, 
				      slurmdbd_conf->auth_info) != SLURM_SUCCESS) {
		error("Failed to unpack DBD_INIT message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}
	if (init_msg->version != SLURMDBD_VERSION) {
		error("Incompatable RPC version (%d != %d)",
			init_msg->version, SLURMDBD_VERSION);
		return SLURM_ERROR;
	}
	*uid = init_msg->uid;
	*port = init_msg->slurmctld_port;
	if (init_msg->cluster_name && init_msg->cluster_name[0])
		*cluster_name = xstrdup(init_msg->cluster_name);
	else
		*cluster_name = NULL;

	info("DBD_INIT: VERSION:%u UID:%u CLUSTER:%s PORT:%u", 
	     init_msg->version, init_msg->uid, 
	     init_msg->cluster_name, init_msg->slurmctld_port);
	slurmdbd_free_init_msg(init_msg);
	*out_buffer = make_dbd_rc_msg(SLURM_SUCCESS);
	return SLURM_SUCCESS;
}

static int  _job_complete(void *db_conn,
			  Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_comp_msg_t *job_comp_msg;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_JOB_COMPLETE message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_job_complete_msg(&job_comp_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_COMPLETE message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	debug2("DBD_JOB_COMPLETE: ID:%u ", job_comp_msg->job_id);

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = job_comp_msg->assoc_id;
	job.db_index = job_comp_msg->db_index;
	job.end_time = job_comp_msg->end_time;
	job.exit_code = job_comp_msg->exit_code;
	job.job_id = job_comp_msg->job_id;
	job.job_state = job_comp_msg->job_state;
	job.nodes = job_comp_msg->nodes;
	job.start_time = job_comp_msg->start_time;
	details.submit_time = job_comp_msg->submit_time;

	job.details = &details;
	rc = jobacct_storage_g_job_complete(db_conn, &job);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	slurmdbd_free_job_complete_msg(job_comp_msg);
	*out_buffer = make_dbd_rc_msg(rc);
	return SLURM_SUCCESS;
}

static int  _job_start(void *db_conn,
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_start_msg_t *job_start_msg;
	dbd_job_start_rc_msg_t job_start_rc_msg;
	struct job_record job;
	struct job_details details;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_JOB_START message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_job_start_msg(&job_start_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_START message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));
	memset(&job_start_rc_msg, 0, sizeof(dbd_job_start_rc_msg_t));

	job.total_procs = job_start_msg->alloc_cpus;
	job.assoc_id = job_start_msg->assoc_id;
	job.comment = job_start_msg->block_id;
	details.begin_time = job_start_msg->eligible_time;
	job.group_id = job_start_msg->gid;
	job.job_id = job_start_msg->job_id;
	job.job_state = job_start_msg->job_state;
	job.name = job_start_msg->name;
	job.nodes = job_start_msg->nodes;
	job.partition = job_start_msg->partition;
	job.num_procs = job_start_msg->req_cpus;
	job.priority = job_start_msg->priority;
	job.start_time = job_start_msg->start_time;
	details.submit_time = job_start_msg->submit_time;

	job.details = &details;

	debug2("DBD_JOB_START: ID:%u NAME:%s", 
	       job_start_msg->job_id, job_start_msg->name);

	job_start_rc_msg.return_code = jobacct_storage_g_job_start(db_conn,
								   &job);
	job_start_rc_msg.db_index = job.db_index;

	slurmdbd_free_job_start_msg(job_start_msg);
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_JOB_START_RC, *out_buffer);
	slurmdbd_pack_job_start_rc_msg(&job_start_rc_msg, *out_buffer);
	return SLURM_SUCCESS;
}

static int  _job_suspend(void *db_conn,
			 Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_suspend_msg_t *job_suspend_msg;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_JOB_SUSPEND message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_job_suspend_msg(&job_suspend_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_JOB_SUSPEND message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	debug2("DBD_JOB_SUSPEND: ID:%u STATE:%s", 
	       job_suspend_msg->job_id, 
	       job_state_string((enum job_states) job_suspend_msg->job_state));

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = job_suspend_msg->assoc_id;
	job.db_index = job_suspend_msg->db_index;
	job.job_id = job_suspend_msg->job_id;
	job.job_state = job_suspend_msg->job_state;
	details.submit_time = job_suspend_msg->submit_time;
	job.suspend_time = job_suspend_msg->suspend_time;

	job.details = &details;
	rc = jobacct_storage_g_job_suspend(db_conn, &job);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	slurmdbd_free_job_suspend_msg(job_suspend_msg);
	*out_buffer = make_dbd_rc_msg(rc);
	return SLURM_SUCCESS;
}

static int _node_state(void *db_conn,
		       Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_node_state_msg_t *node_state_msg;
	struct node_record node_ptr;
	int rc = SLURM_SUCCESS;

	memset(&node_ptr, 0, sizeof(struct node_record));

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_NODE_STATE message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_node_state_msg(&node_state_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_NODE_STATE message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	debug2("DBD_NODE_STATE: NODE:%s STATE:%s REASON:%s TIME:%u", 
	       node_state_msg->hostlist,
	       _node_state_string(node_state_msg->new_state),
	       node_state_msg->reason, 
	       node_state_msg->event_time);
	node_ptr.name = node_state_msg->hostlist;

	slurmctld_conf.fast_schedule = 0;

	if(node_state_msg->new_state == DBD_NODE_STATE_DOWN)
		rc = clusteracct_storage_g_node_down(
			db_conn,
			node_state_msg->cluster_name,
			&node_ptr,
			node_state_msg->event_time,
			node_state_msg->reason);
	else
		rc = clusteracct_storage_g_node_up(db_conn,
						   node_state_msg->cluster_name,
						   &node_ptr,
						   node_state_msg->event_time);
	
	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	slurmdbd_free_node_state_msg(node_state_msg);
	*out_buffer = make_dbd_rc_msg(rc);
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

static int  _step_complete(void *db_conn,
			   Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_comp_msg_t *step_comp_msg;
	struct step_record step;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_STEP_COMPLETE message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_step_complete_msg(&step_comp_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_STEP_COMPLETE message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	debug2("DBD_STEP_COMPLETE: ID:%u.%u SUBMIT:%u", 
	       step_comp_msg->job_id, step_comp_msg->step_id,
	       step_comp_msg->job_submit_time);

	memset(&step, 0, sizeof(struct step_record));
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = step_comp_msg->assoc_id;
	job.db_index = step_comp_msg->db_index;
	job.end_time = step_comp_msg->end_time;
	step.jobacct = step_comp_msg->jobacct;
	job.job_id = step_comp_msg->job_id;
	job.requid = step_comp_msg->req_uid;
	job.start_time = step_comp_msg->start_time;
	details.submit_time = step_comp_msg->job_submit_time;
	step.step_id = step_comp_msg->step_id;
	job.total_procs = step_comp_msg->total_procs;

	job.details = &details;
	step.job_ptr = &job;

	rc = jobacct_storage_g_step_complete(db_conn, &step);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	slurmdbd_free_step_complete_msg(step_comp_msg);
	*out_buffer = make_dbd_rc_msg(rc);
	return SLURM_SUCCESS;
}

static int  _step_start(void *db_conn,
			Buf in_buffer, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_start_msg_t *step_start_msg;
	struct step_record step;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;

	if (*uid != slurmdbd_conf->slurm_user_id) {
		error("DBD_STEP_START message from invalid uid %u", *uid);
		*out_buffer = make_dbd_rc_msg(ESLURM_ACCESS_DENIED);
		return SLURM_ERROR;
	}
	if (slurmdbd_unpack_step_start_msg(&step_start_msg, in_buffer) !=
	    SLURM_SUCCESS) {
		error("Failed to unpack DBD_STEP_START message");
		*out_buffer = make_dbd_rc_msg(SLURM_ERROR);
		return SLURM_ERROR;
	}

	debug2("DBD_STEP_START: ID:%u.%u NAME:%s SUBMIT:%d", 
	     step_start_msg->job_id, step_start_msg->step_id,
	       step_start_msg->name, step_start_msg->job_submit_time);

	memset(&step, 0, sizeof(struct step_record));
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = step_start_msg->assoc_id;
	job.db_index = step_start_msg->db_index;
	job.job_id = step_start_msg->job_id;
	step.name = step_start_msg->name;
	job.nodes = step_start_msg->nodes;
	job.start_time = step_start_msg->start_time;
	details.submit_time = step_start_msg->job_submit_time;
	step.step_id = step_start_msg->step_id;
	job.total_procs = step_start_msg->total_procs;

	job.details = &details;
	step.job_ptr = &job;

	rc = jobacct_storage_g_step_start(db_conn, &step);

	if(rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	slurmdbd_free_step_start_msg(step_start_msg);
	*out_buffer = make_dbd_rc_msg(rc);
	return SLURM_SUCCESS;
}
