/*****************************************************************************\
 *  slurm_protocol_defs.c - functions for initializing and releasing 
 *	storage for RPC data structures. these are the functions used by 
 *	the slurm daemons directly, not for user client use.
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
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"


void slurm_free_last_update_msg(last_update_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_shutdown_msg(shutdown_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_step_id(job_step_id_t * msg)
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

void slurm_free_job_desc_msg(job_desc_msg_t * msg)
{
	int i;

	if (msg) {
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
		xfree(msg->err);
		xfree(msg->in);
		xfree(msg->out);
		xfree(msg->work_dir);
		xfree(msg);
	}
}

void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->nodes);
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

void slurm_free_job_step_create_request_msg(job_step_create_request_msg_t *
					    msg)
{
	if (msg) {
		xfree(msg->node_list);
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
	xfree(msg);
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
	xfree(msg->ofname);
	xfree(msg->ofname);
	xfree(msg->ofname);

#	ifdef HAVE_ELAN
	qsw_free_jobinfo(msg->qsw_job);
#	endif

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

char *job_state_string(enum job_states inx)
{
	static char *job_state_string[] = {
		"PENDING",
		"RUNNING",
		"COMPLETED",
		"FAILED",
		"TIMEOUT",
		"NODE_FAIL",
		"END"
	};
	if (inx & JOB_COMPLETING)
		return "COMPLETING";
	else
		return job_state_string[inx];
}

char *job_state_string_compact(enum job_states inx)
{
	static char *job_state_string[] = {
		"PD",
		"R",
		"CD",
		"F",
		"TO",
		"NF",
		"END"
	};
	if (inx & JOB_COMPLETING)
		return "CG";
	else
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
		"COMPLETING",
		"END"
	};
	static char *node_no_resp_string[] = {
		"DOWN*",
		"UNKNOWN*",
		"IDLE*",
		"ALLOCATED*",
		"DRAINED*",
		"DRAINING*",
		"COMPLETING*",
		"END"
	};
	if (inx & NODE_STATE_NO_RESPOND) {
		inx = (uint16_t) (inx & (~NODE_STATE_NO_RESPOND));
		return node_no_resp_string[inx];
	}
	else
		return node_state_string[inx];
}

char *node_state_string_compact(enum node_states inx)
{
	static char *node_state_string[] = {
		"DOWN",
		"UNK",
		"IDLE",
		"ALLOC",
		"DRAIN",
		"DRNG",
		"COMP",
		"END"
	};
	static char *node_no_resp_string[] = {
		"DOWN*",
		"UNK*",
		"IDLE*",
		"ALLOC*",
		"DRAIN*",
		"DRAIN*",
		"COMP*",
		"END"
	};
	if (inx & NODE_STATE_NO_RESPOND) {
		inx = (uint16_t) (inx & (~NODE_STATE_NO_RESPOND));
		return node_no_resp_string[inx];
	}
	else
		return node_state_string[inx];
}
