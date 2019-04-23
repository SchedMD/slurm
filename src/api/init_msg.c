/*****************************************************************************\
 *  init_msg.c - initialize RPC messages contents
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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
#include <stdio.h>

#include "slurm/slurm.h"

#include "src/common/slurm_protocol_api.h"
#include "src/common/forward.h"

/*
 * slurm_init_job_desc_msg - initialize job descriptor with
 *	default values
 * IN/OUT job_desc_msg - user defined job descriptor
 */
void slurm_init_job_desc_msg(job_desc_msg_t * job_desc_msg)
{
	memset(job_desc_msg, 0, sizeof(job_desc_msg_t));
	job_desc_msg->alloc_sid		= NO_VAL;
	job_desc_msg->contiguous	= NO_VAL16;
	job_desc_msg->core_spec		= NO_VAL16;
	job_desc_msg->cores_per_socket	= NO_VAL16;
	job_desc_msg->cpu_bind_type	= NO_VAL16;
	job_desc_msg->cpu_freq_min	= NO_VAL;
	job_desc_msg->cpu_freq_max	= NO_VAL;
	job_desc_msg->cpu_freq_gov	= NO_VAL;
	job_desc_msg->cpus_per_task	= NO_VAL16;
	job_desc_msg->delay_boot	= NO_VAL;
	job_desc_msg->group_id		= NO_VAL;
	job_desc_msg->job_id		= NO_VAL;
	job_desc_msg->kill_on_node_fail = NO_VAL16;
	job_desc_msg->max_cpus		= NO_VAL;
	job_desc_msg->max_nodes		= NO_VAL;
	job_desc_msg->mem_bind_type	= NO_VAL16;
	job_desc_msg->min_cpus		= NO_VAL;
	job_desc_msg->min_nodes		= NO_VAL;
	job_desc_msg->nice		= NO_VAL;
	job_desc_msg->ntasks_per_core	= NO_VAL16;
	job_desc_msg->ntasks_per_node	= NO_VAL16;
	job_desc_msg->ntasks_per_socket	= NO_VAL16;
	job_desc_msg->num_tasks		= NO_VAL;
	job_desc_msg->overcommit	= NO_VAL8;
	job_desc_msg->plane_size	= NO_VAL16;
	job_desc_msg->pn_min_cpus	= NO_VAL16;
	job_desc_msg->pn_min_memory	= NO_VAL64;
	job_desc_msg->pn_min_tmp_disk	= NO_VAL;
	job_desc_msg->priority		= NO_VAL;
	job_desc_msg->reboot		= NO_VAL16;
	job_desc_msg->requeue		= NO_VAL16;
	job_desc_msg->req_switch	= NO_VAL;
	job_desc_msg->shared		= NO_VAL16;
	job_desc_msg->site_factor	= NO_VAL;
	job_desc_msg->sockets_per_node	= NO_VAL16;
	job_desc_msg->task_dist		= NO_VAL;
	job_desc_msg->threads_per_core	= NO_VAL16;
	job_desc_msg->time_limit	= NO_VAL;
	job_desc_msg->time_min		= NO_VAL;
	job_desc_msg->user_id		= NO_VAL;
	job_desc_msg->wait_all_nodes	= NO_VAL16;
	job_desc_msg->wait4switch	= NO_VAL;
}

/*
 * slurm_init_update_step_msg - initialize step update message with default
 *	values before calling slurm_update_step()
 * OUT step_msg - step update messasge descriptor
 */
extern void slurm_init_update_step_msg (step_update_request_msg_t * step_msg)
{
	memset(step_msg, 0, sizeof(step_update_request_msg_t));
	step_msg->job_id     = NO_VAL;
	step_msg->step_id    = NO_VAL;
	step_msg->time_limit = NO_VAL;
}

/*
 * slurm_init_part_desc_msg - initialize partition descriptor with
 *	default values
 * IN/OUT update_part_msg - user defined partition descriptor
 */
void slurm_init_part_desc_msg (update_part_msg_t * update_part_msg)
{
	memset(update_part_msg, 0, sizeof(update_part_msg_t));
	update_part_msg->default_time   = NO_VAL;
	update_part_msg->def_mem_per_cpu = NO_VAL64;
	update_part_msg->grace_time     = NO_VAL;
	update_part_msg->max_cpus_per_node = NO_VAL;
	update_part_msg->max_mem_per_cpu = NO_VAL64;
	update_part_msg->max_nodes 	= NO_VAL;
	update_part_msg->max_share 	= NO_VAL16;
	update_part_msg->min_nodes 	= NO_VAL;
	update_part_msg->max_time 	= NO_VAL;
	update_part_msg->over_time_limit = NO_VAL16;
	update_part_msg->preempt_mode 	= NO_VAL16;
	update_part_msg->priority_job_factor = NO_VAL16;
	update_part_msg->priority_tier	= NO_VAL16;
	update_part_msg->state_up 	= NO_VAL16;
}

/*
 * slurm_init_resv_desc_msg - initialize reservation descriptor with
 *	default values
 * OUT job_desc_msg - user defined partition descriptor
 */
void slurm_init_resv_desc_msg (resv_desc_msg_t * resv_msg)
{
	memset(resv_msg, 0, sizeof(resv_desc_msg_t));
	resv_msg->duration	= NO_VAL;
	resv_msg->end_time	= (time_t) NO_VAL;
	resv_msg->flags		= NO_VAL;
	resv_msg->start_time	= (time_t) NO_VAL;
	resv_msg->resv_watts	= NO_VAL;
}

/*
 * slurm_init_update_node_msg - initialize node update message
 * OUT update_node_msg - user defined node descriptor
 */
void slurm_init_update_node_msg (update_node_msg_t * update_node_msg)
{
	memset(update_node_msg, 0, sizeof(update_node_msg_t));
	update_node_msg->node_state = NO_VAL;
	update_node_msg->weight = NO_VAL;
}

/*
 * slurm_init_update_front_end_msg - initialize front_end node update message
 * OUT update_front_end_msg - user defined node descriptor
 */
void slurm_init_update_front_end_msg (update_front_end_msg_t *
				      update_front_end_msg)
{
	memset(update_front_end_msg, 0, sizeof(update_front_end_msg_t));
	update_front_end_msg->node_state = NO_VAL;
}

/*
 * slurm_init_trigger_msg - initialize trigger clear/update message
 * OUT trigger_info_msg - user defined trigger descriptor
 */
void slurm_init_trigger_msg (trigger_info_t *trigger_info_msg)
{
	memset(trigger_info_msg, 0, sizeof(trigger_info_t));
	trigger_info_msg->user_id = NO_VAL;
}
