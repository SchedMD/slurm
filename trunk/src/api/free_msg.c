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

#define X_FREE(_X)			\
	do {				\
		if (_X) xfree (_X);	\
	} while (0)

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
		X_FREE(msg->node_list);
		X_FREE(msg->cpus_per_node);
		X_FREE(msg->cpu_count_reps);
		X_FREE(msg->node_addr);
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
		X_FREE(msg->node_list);
		X_FREE(msg->cpus_per_node);
		X_FREE(msg->cpu_count_reps);
		X_FREE(msg->node_addr);
		X_FREE(msg->credentials);
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
		X_FREE(msg->credentials);

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
		X_FREE(config_ptr->backup_addr);
		X_FREE(config_ptr->backup_controller);
		X_FREE(config_ptr->control_addr);
		X_FREE(config_ptr->control_machine);
		X_FREE(config_ptr->epilog);
		X_FREE(config_ptr->job_credential_private_key);
		X_FREE(config_ptr->job_credential_public_certificate);
		X_FREE(config_ptr->prioritize);
		X_FREE(config_ptr->prolog);
		X_FREE(config_ptr->slurm_user_name);
		X_FREE(config_ptr->slurmctld_pidfile);
		X_FREE(config_ptr->slurmctld_logfile);
		X_FREE(config_ptr->slurmd_logfile);
		X_FREE(config_ptr->slurmd_pidfile);
		X_FREE(config_ptr->slurmd_spooldir);
		X_FREE(config_ptr->slurm_conf);
		X_FREE(config_ptr->state_save_location);
		X_FREE(config_ptr->tmp_fs);
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
		X_FREE(job->nodes);
		X_FREE(job->partition);
		X_FREE(job->name);
		X_FREE(job->node_inx);
		X_FREE(job->req_nodes);
		X_FREE(job->features);
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
		X_FREE(msg->partition);
		X_FREE(msg->nodes);
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
		X_FREE(node->name);
		X_FREE(node->features);
		X_FREE(node->partition);
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
		X_FREE(part->name);
		X_FREE(part->allow_groups);
		X_FREE(part->nodes);
		X_FREE(part->node_inx);
	}
}

