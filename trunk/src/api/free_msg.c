/*****************************************************************************\
 *  free_msg.c - free RPC response messages including all allocated memory
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>.
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

#include <errno.h>
#include <stdio.h>

#include "src/api/slurm.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

static void _free_all_job_info (job_info_msg_t *msg);
static void _slurm_free_job_info_members (job_info_t * job);

static void _free_all_node_info (node_info_msg_t *msg);
static void _slurm_free_node_info_members (node_info_t * node);

static void _free_all_partitions (partition_info_msg_t *msg);
static void _slurm_free_partition_info_members (partition_info_t * part);

static void _free_all_step_info (job_step_info_response_msg_t *msg);
static void _slurm_free_job_step_info_members (job_step_info_t * msg);


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
		if (msg->node_list)
			xfree(msg->node_list);
		if (msg->cpus_per_node)
			xfree(msg->cpus_per_node);
		if (msg->cpu_count_reps)
			xfree(msg->cpu_count_reps);
		if (msg->node_addr)
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
		if (msg->node_list)
			xfree(msg->node_list);
		if (msg->cpus_per_node)
			xfree(msg->cpus_per_node);
		if (msg->cpu_count_reps)
			xfree(msg->cpu_count_reps);
		if (msg->credentials)
			xfree(msg->credentials);
		if (msg->node_addr)
			xfree(msg->node_addr);
#		ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(msg->qsw_job);
#		endif
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
		if (msg->credentials)
			xfree(msg->credentials);

#		ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(msg->qsw_job);
#		endif

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
		if (config_ptr->backup_addr)
			xfree(config_ptr->backup_addr);
		if (config_ptr->backup_controller)
			xfree(config_ptr->backup_controller);
		if (config_ptr->control_addr)
			xfree(config_ptr->control_addr);
		if (config_ptr->control_machine)
			xfree(config_ptr->control_machine);
		if (config_ptr->epilog)
			xfree(config_ptr->epilog);
		if (config_ptr->prioritize)
			xfree(config_ptr->prioritize);
		if (config_ptr->prolog)
			xfree(config_ptr->prolog);
		if (config_ptr->slurm_user_name)
			xfree(config_ptr->slurm_user_name);
		if (config_ptr->slurmctld_logfile)
			xfree(config_ptr->slurmctld_logfile);
		if (config_ptr->slurmd_logfile)
			xfree(config_ptr->slurmd_logfile);
		if (config_ptr->slurmd_spooldir)
			xfree(config_ptr->slurmd_spooldir);
		if (config_ptr->slurm_conf)
			xfree(config_ptr->slurm_conf);
		if (config_ptr->state_save_location)
			xfree(config_ptr->state_save_location);
		if (config_ptr->tmp_fs)
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
		if (msg->partition != NULL)
			xfree(msg->partition);
		if (msg->nodes != NULL)
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
		if (node->name)
			xfree(node->name);
		if (node->features)
			xfree(node->features);
		if (node->partition)
			xfree(node->partition);
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

