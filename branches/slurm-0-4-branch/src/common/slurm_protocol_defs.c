/*****************************************************************************\
 *  slurm_protocol_defs.c - functions for initializing and releasing 
 *	storage for RPC data structures. these are the functions used by 
 *	the slurm daemons directly, not for user client use.
 *
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <stdio.h>

#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"

static void _free_all_job_info (job_info_msg_t *msg);
static void _slurm_free_job_info_members (job_info_t * job);

static void _free_all_node_info (node_info_msg_t *msg);
static void _slurm_free_node_info_members (node_info_t * node);

static void _free_all_partitions (partition_info_msg_t *msg);
static void _slurm_free_partition_info_members (partition_info_t * part);

static void _free_all_step_info (job_step_info_response_msg_t *msg);
static void _slurm_free_job_step_info_members (job_step_info_t * msg);


void slurm_free_last_update_msg(last_update_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_shutdown_msg(shutdown_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_old_job_alloc_msg(old_job_alloc_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_return_code_msg(return_code_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_id_msg(job_id_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_id_request_msg(job_id_request_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_id_response_msg(job_id_response_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_info_request_msg(job_info_request_msg_t *msg)
{
	xfree(msg);
}

void slurm_free_job_step_info_request_msg(
		job_step_info_request_msg_t *msg)
{
	xfree(msg);
}

void inline slurm_free_node_info_request_msg(
		node_info_request_msg_t *msg)
{
	xfree(msg);
}

void inline slurm_free_part_info_request_msg(
		part_info_request_msg_t *msg)
{
	xfree(msg);
}

void slurm_free_job_desc_msg(job_desc_msg_t * msg)
{
	int i;

	if (msg) {
		select_g_free_jobinfo(&msg->select_jobinfo);
		xfree(msg->alloc_node);
		for (i = 0; i < msg->env_size; i++) {
			xfree(msg->environment[i]);
		}
		xfree(msg->environment);
		xfree(msg->features);
		xfree(msg->name);
		xfree(msg->partition);
		xfree(msg->req_nodes);
		xfree(msg->exc_nodes);
		xfree(msg->script);
		xfree(msg->argv);
		xfree(msg->err);
		xfree(msg->in);
		xfree(msg->out);
		xfree(msg->work_dir);
		xfree(msg->host);
		xfree(msg->account);
		xfree(msg);
	}
}

void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->nodes);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg->script);
		xfree(msg->err);
		xfree(msg->in);
		xfree(msg->out);
		xfree(msg->work_dir);

		for (i = 0; i < msg->argc; i++) {
			xfree(msg->argv[i]);
		}
		xfree(msg->argv);

		if (msg->environment) {
			for (i = 0; i < msg->envc; i++) {
				xfree(msg->environment[i]);
			}
			xfree(msg->environment);
		}

		select_g_free_jobinfo(&msg->select_jobinfo);

		xfree(msg);
	}
}

void slurm_free_job_info(job_info_t * job)
{
	if (job) {
		slurm_free_job_info_members(job);
		xfree(job);
	}
}

void slurm_free_job_info_members(job_info_t * job)
{
	if (job) {
		select_g_free_jobinfo(&job->select_jobinfo);
		xfree(job->account);
		xfree(job->nodes);
		xfree(job->partition);
		xfree(job->name);
		xfree(job->node_inx);
		xfree(job->req_nodes);
		xfree(job->features);
		xfree(job->req_node_inx);
	}
}

void slurm_free_node_registration_status_msg
	(slurm_node_registration_status_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg->job_id);
		xfree(msg->step_id);
		switch_g_free_node_info(&msg->switch_nodeinfo);
		xfree(msg);
	}
}


void slurm_free_update_node_msg(update_node_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_names);
		xfree(msg->reason);
		xfree(msg);
	}
}

void slurm_free_update_part_msg(update_part_msg_t * msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg->allow_groups);
		xfree(msg);
	}
}

void slurm_free_delete_part_msg(delete_part_msg_t * msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg);
	}
}

void slurm_free_job_step_create_request_msg(job_step_create_request_msg_t *
					    msg)
{
	if (msg) {
		xfree(msg->node_list);
		xfree(msg->host);
		xfree(msg);
	}
}

void slurm_free_job_complete_msg(complete_job_step_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg);
	}
}


void slurm_free_launch_tasks_response_msg(launch_tasks_response_msg_t *
					  msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg->local_pids);
		xfree(msg);
	}
}

void slurm_free_kill_job_msg(kill_job_msg_t * msg)
{
	if (msg) {
		select_g_free_jobinfo(&msg->select_jobinfo);
		xfree(msg);
	}
}

void slurm_free_update_job_time_msg(job_time_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_task_exit_msg(task_exit_msg_t * msg)
{
	if (msg) {
		xfree(msg->task_id_list);
		xfree(msg);
	}
}

void slurm_free_launch_tasks_request_msg(launch_tasks_request_msg_t * msg)
{
	int i;
	if (msg == NULL)
		return;

	slurm_cred_destroy(msg->cred);

	if (msg->env) {
		for (i = 0; i < msg->envc; i++) {
			xfree(msg->env[i]);
		}
		xfree(msg->env);
	}
	xfree(msg->cwd);
	if (msg->argv) {
		for (i = 0; i < msg->argc; i++) {
			xfree(msg->argv[i]);
		}
		xfree(msg->argv);
	}
	xfree(msg->global_task_ids);
	xfree(msg->ifname);
	xfree(msg->ofname);
	xfree(msg->efname);

	if (msg->switch_job)
		switch_free_jobinfo(msg->switch_job);

	xfree(msg);
}

void slurm_free_spawn_task_request_msg(spawn_task_request_msg_t * msg)
{
	int i;
	if (msg == NULL)
		return;

	slurm_cred_destroy(msg->cred);

	if (msg->env) {
		for (i = 0; i < msg->envc; i++) {
			xfree(msg->env[i]);
		}
		xfree(msg->env);
	}
	xfree(msg->cwd);
	if (msg->argv) {
		for (i = 0; i < msg->argc; i++) {
			xfree(msg->argv[i]);
		}
		xfree(msg->argv);
	}

	if (msg->switch_job)
		switch_free_jobinfo(msg->switch_job);

	xfree(msg);
}

void slurm_free_reattach_tasks_request_msg(reattach_tasks_request_msg_t *msg)
{
	if (msg) {
		xfree(msg->ofname);
		xfree(msg->efname);
		xfree(msg->ifname);
		xfree(msg);
	}
}

void slurm_free_reattach_tasks_response_msg(reattach_tasks_response_msg_t *msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg->executable_name);
		xfree(msg->local_pids);
		xfree(msg->gids);
		xfree(msg);
	}
}

void slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_epilog_complete_msg(epilog_complete_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_name);
		xfree(msg);
	}
}

void inline slurm_free_srun_ping_msg(srun_ping_msg_t * msg)
{
	if (msg) {
		xfree(msg);
	}
}

void inline slurm_free_srun_node_fail_msg(srun_node_fail_msg_t * msg)
{
	if (msg) {
		xfree(msg->nodelist);
		xfree(msg);
	}
}

void inline slurm_free_srun_timeout_msg(srun_timeout_msg_t * msg)
{
	if (msg) {
		xfree(msg);
	}
}

void inline slurm_free_checkpoint_msg(checkpoint_msg_t *msg)
{
	if (msg) {
		xfree(msg);
	}
}

void inline slurm_free_checkpoint_resp_msg(checkpoint_resp_msg_t *msg)
{
	if (msg) {
		xfree(msg->ckpt_strerror);
		xfree(msg);
	}
}

/* Given a job's reason for waiting, return a descriptive string */
extern char *job_reason_string(enum job_wait_reason inx)
{
	switch (inx) {
		case WAIT_NO_REASON:
			return "None";
		case WAIT_PRIORITY:
			return "Priority";
		case WAIT_DEPENDENCY:
			return "Dependency";
		case WAIT_RESOUCES:
			return "Resources";
		case WAIT_PART_NODE_LIMIT:
			return "PartitionNodeLimit";
		case WAIT_PART_TIME_LIMIT:
			return "PartitionTimeLimit";
		case WAIT_PART_STATE:
			return "PartitionDown";
		case WAIT_HELD:
			return "JobHeld";
		default:
			return "?";
	}
}

char *job_state_string(enum job_states inx)
{
	if (inx & JOB_COMPLETING)
		return "COMPLETING";

	switch (inx) {
		case JOB_PENDING:
			return "PENDING";
		case JOB_RUNNING:
			return "RUNNING";
		case JOB_COMPLETE:
			return "COMPLETED";
		case JOB_CANCELLED:
			return "CANCELLED";
		case JOB_FAILED:
			return "FAILED";
		case JOB_TIMEOUT:
			return "TIMEOUT";
		case JOB_NODE_FAIL:
			return "NODE_FAIL";
		default:
			return "?";
	}
}

char *job_state_string_compact(enum job_states inx)
{
	if (inx & JOB_COMPLETING)
		return "CG";

	switch (inx) {
		case JOB_PENDING:
			return "PD";
		case JOB_RUNNING:
			return "R";
		case JOB_COMPLETE:
			return "CD";
		case JOB_CANCELLED:
			return "CA";
		case JOB_FAILED:
			return "F";
		case JOB_TIMEOUT:
			return "TO";
		case JOB_NODE_FAIL:
			return "NF";
		default:
			return "?";
	}
}

char *node_state_string(enum node_states inx)
{
	bool no_resp_flag;

	if (inx & NODE_STATE_NO_RESPOND) {
		no_resp_flag = true;
		inx = (uint16_t) (inx & (~NODE_STATE_NO_RESPOND));
	} else
		no_resp_flag = false;

	switch (inx) {
		case NODE_STATE_DOWN:
			if (no_resp_flag)
				return "DOWN*";
			return "DOWN";
		case NODE_STATE_UNKNOWN:
			if (no_resp_flag)
				return "UNKNOWN*";
			return "UNKNOWN";
		case NODE_STATE_IDLE:
			if (no_resp_flag)
				return "IDLE*";
			return "IDLE";
		case NODE_STATE_ALLOCATED:
			if (no_resp_flag)
				return "ALLOCATED*";
			return "ALLOCATED";
		case NODE_STATE_DRAINED:
			if (no_resp_flag)
				return "DRAINED*";
			return "DRAINED";
		case NODE_STATE_DRAINING:
			if (no_resp_flag)
				return "DRAINING*";
			return "DRAINING";
		case NODE_STATE_COMPLETING:
			if (no_resp_flag)
				return "COMPLETING*";
			return "COMPLETING";
		default:
			return "?";
	}
}

char *node_state_string_compact(enum node_states inx)
{
	bool no_resp_flag;

	if (inx & NODE_STATE_NO_RESPOND) {
		no_resp_flag = true;
		inx = (uint16_t) (inx & (~NODE_STATE_NO_RESPOND));
	} else
		no_resp_flag = false;

	switch (inx) {
		case NODE_STATE_DOWN:
			if (no_resp_flag)
				return "DOWN*";
			return "DOWN";
		case NODE_STATE_UNKNOWN:
			if (no_resp_flag)
				return "UNK*";
			return "UNK";
		case NODE_STATE_IDLE:
			if (no_resp_flag)
				return "IDLE*";
			return "IDLE";
		case NODE_STATE_ALLOCATED:
			if (no_resp_flag)
				return "ALLOC*";
			return "ALLOC";
		case NODE_STATE_DRAINED:
			if (no_resp_flag)
				return "DRAIN*";
			return "DRAIN";
		case NODE_STATE_DRAINING:
			if (no_resp_flag)
				return "DRNG*";
			return "DRNG";
		case NODE_STATE_COMPLETING:
			if (no_resp_flag)
				return "COMP*";
			return "COMP";
		default:
			return "?";
	}
}

/*
 * slurm_free_resource_allocation_response_msg - free slurm resource
 *	allocation response message
 * IN msg - pointer to allocation response message
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
void slurm_free_resource_allocation_response_msg ( 
				resource_allocation_response_msg_t * msg)
{
	if (msg) {
		select_g_free_jobinfo(&msg->select_jobinfo);
		xfree(msg->node_list);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg->node_addr);
		xfree(msg);
	}
}


/*
 * slurm_free_resource_allocation_and_run_response_msg - free slurm 
 *	resource allocation and run job step response message
 * IN msg - pointer to allocation and run job step response message
 * NOTE: buffer is loaded by slurm_allocate_resources_and_run
 */
void slurm_free_resource_allocation_and_run_response_msg ( 
			resource_allocation_and_run_response_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_list);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg->node_addr);
		slurm_cred_destroy(msg->cred);
		if (msg->switch_job)
			switch_free_jobinfo(msg->switch_job);
		xfree(msg);
	}
}


/*
 * slurm_free_job_step_create_response_msg - free slurm 
 *	job step create response message
 * IN msg - pointer to job step create response message
 * NOTE: buffer is loaded by slurm_job_step_create
 */
void slurm_free_job_step_create_response_msg(
		job_step_create_response_msg_t * msg)
{
	if (msg) {
		slurm_cred_destroy(msg->cred);

		if (msg->switch_job)
			switch_free_jobinfo(msg->switch_job);

		xfree(msg);
	}

}


/*
 * slurm_free_submit_response_response_msg - free slurm 
 *	job submit response message
 * IN msg - pointer to job submit response message
 * NOTE: buffer is loaded by slurm_submit_batch_job
 */
void slurm_free_submit_response_response_msg(submit_response_msg_t * msg)
{
	if (msg)
		xfree(msg);
}


/*
 * slurm_free_ctl_conf - free slurm control information response message
 * IN msg - pointer to slurm control information response message
 * NOTE: buffer is loaded by slurm_load_jobs
 */
void slurm_free_ctl_conf(slurm_ctl_conf_info_msg_t * config_ptr)
{
	if (config_ptr) {
		xfree(config_ptr->authtype);
		xfree(config_ptr->backup_addr);
		xfree(config_ptr->backup_controller);
		xfree(config_ptr->control_addr);
		xfree(config_ptr->control_machine);
		xfree(config_ptr->epilog);
		xfree(config_ptr->job_comp_loc);
		xfree(config_ptr->job_comp_type);
		xfree(config_ptr->job_credential_private_key);
		xfree(config_ptr->job_credential_public_certificate);
		xfree(config_ptr->plugindir);
		xfree(config_ptr->prolog);
		xfree(config_ptr->slurm_user_name);
		xfree(config_ptr->slurmctld_pidfile);
		xfree(config_ptr->slurmctld_logfile);
		xfree(config_ptr->slurmd_logfile);
		xfree(config_ptr->slurmd_pidfile);
		xfree(config_ptr->slurmd_spooldir);
		xfree(config_ptr->slurm_conf);
		xfree(config_ptr->state_save_location);
		xfree(config_ptr->tmp_fs);
		xfree(config_ptr);
	}
}


/*
 * slurm_free_job_info - free the job information response message
 * IN msg - pointer to job information response message
 * NOTE: buffer is loaded by slurm_load_job.
 */
void slurm_free_job_info_msg(job_info_msg_t * job_buffer_ptr)
{
	if (job_buffer_ptr) {
		if (job_buffer_ptr->job_array) {
			_free_all_job_info(job_buffer_ptr);
			xfree(job_buffer_ptr->job_array);
		}
		xfree(job_buffer_ptr);
	}
}

static void _free_all_job_info(job_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->job_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++)
		_slurm_free_job_info_members (&msg->job_array[i]);
}

static void _slurm_free_job_info_members(job_info_t * job)
{
	if (job) {
		xfree(job->nodes);
		xfree(job->partition);
		xfree(job->alloc_node);
		xfree(job->name);
		xfree(job->node_inx);
		xfree(job->req_nodes);
		xfree(job->features);
		xfree(job->req_node_inx);
		select_g_free_jobinfo(&job->select_jobinfo);
	}
}


/*
 * slurm_free_job_step_info_response_msg - free the job step 
 *	information response message
 * IN msg - pointer to job step information response message
 * NOTE: buffer is loaded by slurm_get_job_steps.
 */
void slurm_free_job_step_info_response_msg(job_step_info_response_msg_t *
					   msg)
{
	if (msg != NULL) {
		if (msg->job_steps != NULL) {
			_free_all_step_info(msg);
			xfree(msg->job_steps);
		}
		xfree(msg);
	}
}

static void _free_all_step_info (job_step_info_response_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->job_steps == NULL))
		return;

	for (i = 0; i < msg->job_step_count; i++)
		_slurm_free_job_step_info_members (&msg->job_steps[i]);
}

static void _slurm_free_job_step_info_members (job_step_info_t * msg)
{
	if (msg != NULL) {
		xfree(msg->partition);
		xfree(msg->nodes);
	}
}


/*
 * slurm_free_node_info - free the node information response message
 * IN msg - pointer to node information response message
 * NOTE: buffer is loaded by slurm_load_node.
 */
void slurm_free_node_info_msg(node_info_msg_t * msg)
{
	if (msg) {
		if (msg->node_array) {
			_free_all_node_info(msg);
			xfree(msg->node_array);
		}
		xfree(msg);
	}
}

static void _free_all_node_info(node_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->node_array == NULL))
		return;
	
	for (i = 0; i < msg->record_count; i++) 
		_slurm_free_node_info_members(&msg->node_array[i]);
}

static void _slurm_free_node_info_members(node_info_t * node)
{
	if (node) {
		xfree(node->name);
		xfree(node->features);
		xfree(node->partition);
		xfree(node->reason);
	}
}


/*
 * slurm_free_partition_info_msg - free the partition information 
 *	response message
 * IN msg - pointer to partition information response message
 * NOTE: buffer is loaded by slurm_load_partitions
 */
void slurm_free_partition_info_msg(partition_info_msg_t * msg)
{
	if (msg) {
		if (msg->partition_array) {
			_free_all_partitions(msg);
			xfree(msg->partition_array);
		}
		xfree(msg);
	}
}

static void  _free_all_partitions(partition_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->partition_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++)
		_slurm_free_partition_info_members(
			&msg->partition_array[i]);

}

static void _slurm_free_partition_info_members(partition_info_t * part)
{
	if (part) {
		xfree(part->name);
		xfree(part->allow_groups);
		xfree(part->nodes);
		xfree(part->node_inx);
	}
}

