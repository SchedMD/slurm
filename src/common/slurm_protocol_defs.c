/*****************************************************************************\
 *  slurm_protocol_defs.c - functions for initializing and releasing 
 *	storage for RPC data structures
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
#  include <config.h>
#endif

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <src/common/slurm_protocol_defs.h>
#include <src/common/xmalloc.h>

/* short messages*/
void slurm_free_last_update_msg(last_update_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_id_msg(job_id_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_step_id(job_step_id_t * msg)
{
	xfree(msg);
}

void slurm_free_return_code_msg(return_code_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_ctl_conf(slurm_ctl_conf_info_msg_t * build_ptr)
{
	if (build_ptr) {
		if (build_ptr->backup_controller)
			xfree(build_ptr->backup_controller);
		if (build_ptr->control_machine)
			xfree(build_ptr->control_machine);
		if (build_ptr->epilog)
			xfree(build_ptr->epilog);
		if (build_ptr->prioritize)
			xfree(build_ptr->prioritize);
		if (build_ptr->prolog)
			xfree(build_ptr->prolog);
		if (build_ptr->slurm_conf)
			xfree(build_ptr->slurm_conf);
		if (build_ptr->state_save_location)
			xfree(build_ptr->state_save_location);
		if (build_ptr->tmp_fs)
			xfree(build_ptr->tmp_fs);
		xfree(build_ptr);
	}
}

void slurm_free_job_desc_msg(job_desc_msg_t * msg)
{
	int i;

	if (msg) {
		for (i = 0; i < msg->env_size; i++) {
			if (msg->environment[i])
				xfree(msg->environment[i]);
		}
		if (msg->environment)
			xfree(msg->environment);
		if (msg->features)
			xfree(msg->features);
		if (msg->groups)
			xfree(msg->groups);
		if (msg->name)
			xfree(msg->name);
		if (msg->partition)
			xfree(msg->partition);
		if (msg->req_nodes)
			xfree(msg->req_nodes);
		if (msg->script)
			xfree(msg->script);
		if (msg->stderr)
			xfree(msg->stderr);
		if (msg->stdin)
			xfree(msg->stdin);
		if (msg->stdout)
			xfree(msg->stdout);
		if (msg->work_dir)
			xfree(msg->work_dir);
		xfree(msg);
	}
}

static void 
_free_all_partitions(partition_info_msg_t *msg)
{
	int i;
	xassert(msg != NULL);
	xassert(msg->partition_array != NULL);

	for (i = 0; i < msg->record_count; i++)
		slurm_free_partition_info_members(&msg->partition_array[i]);

}

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

void slurm_free_partition_info(partition_info_t * part)
{
	if (part) {
		slurm_free_partition_info_members(part);
		xfree(part);
	}
}

void slurm_free_partition_info_members(partition_info_t * part)
{
	if (part) {
		if (part->name)
			xfree(part->name);
		if (part->allow_groups)
			xfree(part->allow_groups);
		if (part->nodes)
			xfree(part->nodes);
		if (part->node_inx)
			xfree(part->node_inx);
	}
}

static void _free_all_job_info(job_info_msg_t *msg)
{
	int i;
	xassert(msg != NULL);
	xassert(msg->job_array != NULL);

	for (i = 0; i < msg->record_count; i++)
		slurm_free_job_info_members(&msg->job_array[i]);
}

void slurm_free_job_info_msg(job_info_msg_t * msg)
{
	int i;
	if (msg) {
		if (msg->job_array) {
			_free_all_job_info(msg);
			xfree(msg->job_array);
		}
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
		if (job->nodes)
			xfree(job->nodes);
		if (job->partition)
			xfree(job->partition);
		if (job->name)
			xfree(job->name);
		if (job->node_inx)
			xfree(job->node_inx);
		if (job->req_nodes)
			xfree(job->req_nodes);
		if (job->features)
			xfree(job->features);
		xfree(job->req_node_inx);
	}
}

void slurm_free_job_step_info_members(job_step_info_t * msg)
{
	if (msg != NULL) {
		if (msg->partition != NULL)
			xfree(msg->partition);
		if (msg->nodes != NULL)
			xfree(msg->nodes);
	}
}

void slurm_free_job_step_info(job_step_info_t * msg)
{
	if (msg != NULL) {
		slurm_free_job_step_info_members(msg);
		xfree(msg);
	}
}


static void _free_all_step_info(job_step_info_response_msg_t *msg)
{
	int i;
	xassert (msg != NULL);
	xassert (msg->job_steps != NULL);

	for (i = 0; i < msg->job_step_count; i++)
		slurm_free_job_step_info_members(&msg->job_steps[i]);
}

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

static void _free_all_node_info(node_info_msg_t *msg)
{
	int i;
	xassert(msg != NULL);
	xassert(msg->node_array != NULL);
	
	for (i = 0; i < msg->record_count; i++) 
		slurm_free_node_info_members(&msg->node_array[i]);
}

void slurm_free_node_info_msg(node_info_msg_t * msg)
{
	int i;
	if (msg) {
		if (msg->node_array) {
			_free_all_node_info(msg);
			xfree(msg->node_array);
		}
		xfree(msg);
	}
}

void slurm_free_node_info(node_info_t * node)
{
	if (node) {
		slurm_free_node_info_members(node);
		xfree(node);
	}
}

void slurm_free_node_info_members(node_info_t * node)
{
	if (node) {
		if (node->name)
			xfree(node->name);
		if (node->features)
			xfree(node->features);
		if (node->partition)
			xfree(node->partition);
	}
}

void
slurm_free_resource_allocation_response_msg
(resource_allocation_response_msg_t * msg)
{
	if (msg) {
		if (msg->node_list)
			xfree(msg->node_list);
		if (msg->cpus_per_node)
			xfree(msg->cpus_per_node);
		if (msg->cpu_count_reps)
			xfree(msg->cpu_count_reps);
		xfree(msg);
	}
}

void
slurm_free_resource_allocation_and_run_response_msg
(resource_allocation_and_run_response_msg_t * msg)
{
	if (msg) {
		if (msg->node_list)
			xfree(msg->node_list);
		if (msg->cpus_per_node)
			xfree(msg->cpus_per_node);
		if (msg->cpu_count_reps)
			xfree(msg->cpu_count_reps);
		if (msg->credentials)
			xfree(msg->credentials);
#		ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(msg->qsw_job);
#		endif
		xfree(msg);
	}
}

void slurm_free_submit_response_response_msg(submit_response_msg_t * msg)
{
	if (msg)
		xfree(msg);
}

void
slurm_free_node_registration_status_msg
(slurm_node_registration_status_msg_t * msg)
{
	if (msg) {
		if (msg->node_name)
			xfree(msg->node_name);
		xfree(msg);
	}
}


void slurm_free_update_node_msg(update_node_msg_t * msg)
{
	if (msg) {
		if (msg->node_names)
			xfree(msg->node_names);
		xfree(msg);
	}
}

void slurm_free_update_part_msg(update_part_msg_t * msg)
{
	if (msg) {
		if (msg->name)
			xfree(msg->name);
		if (msg->nodes)
			xfree(msg->nodes);
		if (msg->allow_groups)
			xfree(msg->allow_groups);
		xfree(msg);
	}
}

void slurm_free_job_step_create_request_msg(job_step_create_request_msg_t *
					    msg)
{
	if (msg) {
		if (msg->node_list)
			xfree(msg->node_list);
		xfree(msg);
	}
}

void slurm_free_job_step_create_response_msg(job_step_create_response_msg_t
					     * msg)
{
	if (msg) {
		if (msg->credentials)
			xfree(msg->credentials);

#		ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(msg->qsw_job);
#		endif

		xfree(msg);
	}

}

void slurm_free_launch_tasks_response_msg(launch_tasks_response_msg_t *
					  msg)
{
	if (msg) {
		if (msg->node_name)
			xfree(msg->node_name);
		xfree(msg);
	}
}

void slurm_free_revoke_credential_msg(revoke_credential_msg_t * msg)
{
	if (msg) {
		xfree(msg);
	}
}

void slurm_free_task_exit_msg(task_exit_msg_t * msg)
{
	if (msg) {
		xfree(msg);
	}
}

void slurm_free_launch_tasks_request_msg(launch_tasks_request_msg_t * msg)
{
	int i;
	if (msg) {
		if (msg->credential)
			xfree(msg->credential);
		if (msg->env) {
			for (i = 0; i < msg->envc; i++) {
				if (msg->env[i])
					xfree(msg->env[i]);
			}
			xfree(msg->env);
		}
		if (msg->cwd)
			xfree(msg->cwd);
		if (msg->argv) {
			for (i = 0; i < msg->argc; i++) {
				if (msg->argv[i])
					xfree(msg->argv[i]);
			}
			xfree(msg->argv);
		}
		if (msg->global_task_ids)
			xfree(msg->global_task_ids);
#		ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(msg->qsw_job);
#		endif

		xfree(msg);
	}
}

void slurm_free_reattach_tasks_streams_msg(reattach_tasks_streams_msg_t *
					   msg)
{
	if (msg) {
		if (msg->credential)
			xfree(msg->credential);
		if (msg->global_task_ids)
			xfree(msg->global_task_ids);
		xfree(msg);
	}
}

void slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg)
{
	if (msg) {
		xfree(msg);
	}
}

/**********************
 Init functions
 **********************/


void slurm_init_job_desc_msg(job_desc_msg_t * job_desc_msg)
{
	job_desc_msg->contiguous =
	    (uint16_t) SLURM_JOB_DESC_DEFAULT_CONTIGUOUS;
	job_desc_msg->environment = SLURM_JOB_DESC_DEFAULT_ENVIRONMENT;
	job_desc_msg->env_size    = SLURM_JOB_DESC_DEFAULT_ENV_SIZE;
	job_desc_msg->features    = SLURM_JOB_DESC_DEFAULT_FEATURES;

	job_desc_msg->groups      = SLURM_JOB_DESC_DEFAULT_GROUPS;	
	job_desc_msg->job_id      = SLURM_JOB_DESC_DEFAULT_JOB_ID;

	job_desc_msg->name        = SLURM_JOB_DESC_DEFAULT_JOB_NAME;
	job_desc_msg->min_procs   = SLURM_JOB_DESC_DEFAULT_MIN_PROCS;
	job_desc_msg->min_memory  = SLURM_JOB_DESC_DEFAULT_MIN_MEMORY;
	job_desc_msg->min_tmp_disk= SLURM_JOB_DESC_DEFAULT_MIN_TMP_DISK;
	job_desc_msg->partition   = SLURM_JOB_DESC_DEFAULT_PARTITION;
	job_desc_msg->priority    = SLURM_JOB_DESC_DEFAULT_PRIORITY;
	job_desc_msg->req_nodes   = SLURM_JOB_DESC_DEFAULT_REQ_NODES;
	job_desc_msg->script      = SLURM_JOB_DESC_DEFAULT_JOB_SCRIPT;
	job_desc_msg->shared      = (uint16_t) SLURM_JOB_DESC_DEFAULT_SHARED;
	job_desc_msg->time_limit  = SLURM_JOB_DESC_DEFAULT_TIME_LIMIT;
	job_desc_msg->num_procs   = SLURM_JOB_DESC_DEFAULT_NUM_PROCS;
	job_desc_msg->num_nodes   = SLURM_JOB_DESC_DEFAULT_NUM_NODES;
	job_desc_msg->stderr      = NULL;
	job_desc_msg->stdin       = NULL;
	job_desc_msg->stdout      = NULL;
	job_desc_msg->user_id     = SLURM_JOB_DESC_DEFAULT_USER_ID;
	job_desc_msg->work_dir    = SLURM_JOB_DESC_DEFAULT_WORKING_DIR;
}

void slurm_init_part_desc_msg(update_part_msg_t * update_part_msg)
{
	update_part_msg->name 		= NULL;
	update_part_msg->nodes 		= NULL;
	update_part_msg->allow_groups 	= NULL;
	update_part_msg->max_time 	= (uint32_t) NO_VAL;
	update_part_msg->max_nodes 	= (uint32_t) NO_VAL;
	update_part_msg->default_part 	= (uint16_t) NO_VAL;
	update_part_msg->root_only 	= (uint16_t) NO_VAL;
	update_part_msg->shared 	= (uint16_t) NO_VAL;
	update_part_msg->state_up 	= (uint16_t) NO_VAL;
}

char *job_state_string(enum job_states inx)
{
	static char *job_state_string[] = {
		"PENDING",
		"STAGE_IN",
		"RUNNING",
		"STAGE_OUT",
		"COMPLETE",
		"FAILED",
		"TIMEOUT",
		"END"
	};
	return job_state_string[inx];
}

char *job_state_string_compact(enum job_states inx)
{
	static char *job_state_string[] = {
		"PD",
		"SI",
		"R",
		"SO",
		"C",
		"F",
		"TO",
		"END"
	};
	return job_state_string[inx];
}

char *node_state_string(enum node_states inx)
{
	static char *node_state_string[] = {
		"DOWN",
		"UNKNOWN",
		"IDLE",
		"ALLOCATED",
		"DRAINED",
		"DRAINING",
		"END"
	};
	return node_state_string[inx];
}

char *node_state_string_compact(enum node_states inx)
{
	static char *node_state_string[] = {
		"DN",
		"UN",
		"I",
		"AL",
		"DD",
		"DG",
		"END"
	};
	return node_state_string[inx];
}
