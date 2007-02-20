/*****************************************************************************\
 *  init_msg.c - initialize RPC messages contents
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  UCRL-CODE-217948.
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

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"

/*
 * slurm_init_job_desc_msg - initialize job descriptor with 
 *	default values 
 * OUT job_desc_msg - user defined job descriptor
 */
void slurm_init_job_desc_msg(job_desc_msg_t * job_desc_msg)
{
	job_desc_msg->account     = NULL;
	job_desc_msg->alloc_node  = NULL;
	job_desc_msg->alloc_sid   = NO_VAL;
	job_desc_msg->contiguous  = (uint16_t) NO_VAL;
	job_desc_msg->cpus_per_task = (uint16_t) NO_VAL;
	job_desc_msg->dependency  = NO_VAL;
	job_desc_msg->environment = ((char **) NULL);
	job_desc_msg->env_size    = 0;
	job_desc_msg->features    = NULL;
	job_desc_msg->immediate   = 0;
	job_desc_msg->job_id      = NO_VAL;
	job_desc_msg->kill_on_node_fail = (uint16_t) NO_VAL;
	job_desc_msg->name        = NULL;
	job_desc_msg->network     = NULL;
	job_desc_msg->nice        = NICE_OFFSET;
	job_desc_msg->min_procs   = NO_VAL;
	job_desc_msg->min_memory  = NO_VAL;
	job_desc_msg->min_tmp_disk= NO_VAL;
	job_desc_msg->partition   = NULL;
	job_desc_msg->priority    = NO_VAL;
	job_desc_msg->req_nodes   = NULL;
	job_desc_msg->exc_nodes   = NULL;
	job_desc_msg->script      = NULL;
	job_desc_msg->argv        = ((char **) NULL);
	job_desc_msg->argc        = 0;
	job_desc_msg->shared      = (uint16_t) NO_VAL;
	job_desc_msg->task_dist   = (uint16_t) NO_VAL;
	job_desc_msg->time_limit  = NO_VAL;
	job_desc_msg->num_procs   = NO_VAL;
	job_desc_msg->max_nodes   = NO_VAL;
	job_desc_msg->min_nodes   = NO_VAL;
	job_desc_msg->num_tasks   = NO_VAL;
	job_desc_msg->err         = NULL;
	job_desc_msg->in          = NULL;
	job_desc_msg->out         = NULL;
	job_desc_msg->user_id     = NO_VAL;
	job_desc_msg->work_dir    = NULL;
	job_desc_msg->host        = NULL;
	job_desc_msg->mail_user   = NULL;
	job_desc_msg->port        = 0;
	job_desc_msg->begin_time  = 0;
#if SYSTEM_DIMENSIONS
{
	int i;
	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		job_desc_msg->geometry[i] = (uint16_t) NO_VAL;
}
#endif
	job_desc_msg->conn_type   = (uint16_t) NO_VAL;
	job_desc_msg->rotate      = (uint16_t) NO_VAL;
	job_desc_msg->exclusive   = (uint16_t) NO_VAL;
}

/*
 * slurm_init_part_desc_msg - initialize partition descriptor with 
 *	default values 
 * OUT job_desc_msg - user defined partition descriptor
 */
void slurm_init_part_desc_msg (update_part_msg_t * update_part_msg)
{
	update_part_msg->name 		= NULL;
	update_part_msg->nodes 		= NULL;
	update_part_msg->allow_groups 	= NULL;
	update_part_msg->max_time 	= (uint32_t) NO_VAL;
	update_part_msg->max_nodes 	= (uint32_t) NO_VAL;
	update_part_msg->min_nodes 	= (uint32_t) NO_VAL;
	update_part_msg->hidden 	= (uint16_t) NO_VAL;
	update_part_msg->default_part 	= (uint16_t) NO_VAL;
	update_part_msg->root_only 	= (uint16_t) NO_VAL;
	update_part_msg->shared 	= (uint16_t) NO_VAL;
	update_part_msg->state_up 	= (uint16_t) NO_VAL;
}


