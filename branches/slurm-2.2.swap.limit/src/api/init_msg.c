/*****************************************************************************\
 *  init_msg.c - initialize RPC messages contents
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>

#include <slurm/slurm.h>

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
	job_desc_msg->acctg_freq	= (uint16_t) NO_VAL;
	job_desc_msg->alloc_sid		= NO_VAL;
	job_desc_msg->conn_type		= (uint16_t) NO_VAL;
	job_desc_msg->contiguous	= (uint16_t) NO_VAL;
	job_desc_msg->cpu_bind_type	= (uint16_t) NO_VAL;
	job_desc_msg->cpus_per_task	= (uint16_t) NO_VAL;
#ifdef HAVE_BG
{
	int i;
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		job_desc_msg->geometry[i] = (uint16_t) NO_VAL;
}
#endif
	job_desc_msg->group_id		= NO_VAL;
	job_desc_msg->job_id		= NO_VAL;
	job_desc_msg->pn_min_cpus	= (uint16_t) NO_VAL;
	job_desc_msg->pn_min_memory    = NO_VAL;
	job_desc_msg->pn_min_tmp_disk  = NO_VAL;
	job_desc_msg->kill_on_node_fail = (uint16_t) NO_VAL;
	job_desc_msg->max_cpus		= NO_VAL;
	job_desc_msg->max_nodes		= NO_VAL;
	job_desc_msg->mem_bind_type	= (uint16_t) NO_VAL;
	job_desc_msg->min_cores		= (uint16_t) NO_VAL;
	job_desc_msg->min_cpus		= NO_VAL;
	job_desc_msg->min_nodes		= NO_VAL;
	job_desc_msg->min_sockets	= (uint16_t) NO_VAL;
	job_desc_msg->min_threads	= (uint16_t) NO_VAL;
	job_desc_msg->nice		= (uint16_t) NO_VAL;
	job_desc_msg->ntasks_per_core	= (uint16_t) NO_VAL;
	job_desc_msg->ntasks_per_node	= (uint16_t) NO_VAL;
	job_desc_msg->ntasks_per_socket	= (uint16_t) NO_VAL;
	job_desc_msg->num_tasks		= NO_VAL;
	job_desc_msg->overcommit	= (uint8_t) NO_VAL;
	job_desc_msg->plane_size	= (uint16_t) NO_VAL;
	job_desc_msg->priority		= NO_VAL;
	job_desc_msg->reboot		= (uint16_t) NO_VAL;
	job_desc_msg->requeue		= (uint16_t) NO_VAL;
	job_desc_msg->rotate		= (uint16_t) NO_VAL;
	job_desc_msg->shared		= (uint16_t) NO_VAL;
	job_desc_msg->task_dist		= (uint16_t) NO_VAL;
	job_desc_msg->time_limit	= NO_VAL;
	job_desc_msg->user_id		= NO_VAL;
}

/*
 * slurm_init_part_desc_msg - initialize partition descriptor with
 *	default values
 * IN/OUT update_part_msg - user defined partition descriptor
 */
void slurm_init_part_desc_msg (update_part_msg_t * update_part_msg)
{
	memset(update_part_msg, 0, sizeof(update_part_msg_t));
	update_part_msg->default_part 	= (uint16_t) NO_VAL;
	update_part_msg->default_time   = (uint32_t) NO_VAL;
	update_part_msg->hidden 	= (uint16_t) NO_VAL;
	update_part_msg->max_nodes 	= NO_VAL;
	update_part_msg->max_share 	= (uint16_t) NO_VAL;
	update_part_msg->min_nodes 	= NO_VAL;
	update_part_msg->max_time 	= (uint32_t) NO_VAL;
	update_part_msg->priority 	= (uint16_t) NO_VAL;
	update_part_msg->root_only 	= (uint16_t) NO_VAL;
	update_part_msg->state_up 	= (uint16_t) NO_VAL;
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
	resv_msg->flags		= (uint16_t) NO_VAL;
	resv_msg->node_cnt	= NO_VAL;
	resv_msg->start_time	= (time_t) NO_VAL;
}

/*
 * slurm_init_update_node_msg - initialize node update message
 * OUT update_node_msg - user defined node descriptor
 */
void slurm_init_update_node_msg (update_node_msg_t * update_node_msg)
{
	memset(update_node_msg, 0, sizeof(update_node_msg_t));
	update_node_msg->node_state = (uint16_t) NO_VAL;
	update_node_msg->weight = (uint32_t) NO_VAL;
}

/*
 * slurm_init_update_block_msg - initialize block update message
 * OUT update_block_msg - user defined block descriptor
 */
void slurm_init_update_block_msg (update_block_msg_t *update_block_msg)
{
	memset(update_block_msg, 0, sizeof(update_block_msg_t));
	update_block_msg->conn_type = (uint16_t)NO_VAL;
	update_block_msg->job_running = NO_VAL;
	update_block_msg->node_cnt = NO_VAL;
	update_block_msg->node_use = (uint16_t)NO_VAL;
	update_block_msg->state = (uint16_t)NO_VAL;

}
