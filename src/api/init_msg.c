/*****************************************************************************\
 *  init_msg.c - initialize RPC messages contents
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
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>

#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

void slurm_init_job_desc_msg(job_desc_msg_t * job_desc_msg)
{
	job_desc_msg->contiguous =
			(uint16_t) SLURM_JOB_DESC_DEFAULT_CONTIGUOUS;
	job_desc_msg->kill_on_node_fail =
			(uint16_t) SLURM_JOB_DESC_DEFAULT_KILL_NODE_FAIL;
	job_desc_msg->environment = SLURM_JOB_DESC_DEFAULT_ENVIRONMENT;
	job_desc_msg->env_size    = SLURM_JOB_DESC_DEFAULT_ENV_SIZE;
	job_desc_msg->features    = SLURM_JOB_DESC_DEFAULT_FEATURES;
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
	job_desc_msg->err         = NULL;
	job_desc_msg->in          = NULL;
	job_desc_msg->out         = NULL;
	job_desc_msg->user_id     = SLURM_JOB_DESC_DEFAULT_USER_ID;
	job_desc_msg->work_dir    = SLURM_JOB_DESC_DEFAULT_WORKING_DIR;
}

void slurm_init_part_desc_msg (update_part_msg_t * update_part_msg)
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


