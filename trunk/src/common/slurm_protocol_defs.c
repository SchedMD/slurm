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
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"

#define FREE_IF_SET(_X)			\
	do {				\
		if (_X) xfree (_X);	\
	} while (0)

void slurm_free_last_update_msg(last_update_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_shutdown_msg(shutdown_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_job_step_id(job_step_id_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_old_job_alloc_msg(old_job_alloc_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_return_code_msg(return_code_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_job_id_request_msg(job_id_request_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_job_id_response_msg(job_id_response_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_batch_resp_msg(batch_launch_response_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_job_step_kill_msg(job_step_kill_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_job_desc_msg(job_desc_msg_t * msg)
{
	int i;

	if (msg) {
		for (i = 0; i < msg->env_size; i++) {
			FREE_IF_SET(msg->environment[i]);
		}
		FREE_IF_SET(msg->environment);
		FREE_IF_SET(msg->features);
		FREE_IF_SET(msg->name);
		FREE_IF_SET(msg->partition);
		FREE_IF_SET(msg->req_nodes);
		FREE_IF_SET(msg->exc_nodes);
		FREE_IF_SET(msg->script);
		FREE_IF_SET(msg->err);
		FREE_IF_SET(msg->in);
		FREE_IF_SET(msg->out);
		FREE_IF_SET(msg->work_dir);
		xfree(msg);
	}
}

void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg)
{
	int i;

	if (msg) {
		FREE_IF_SET(msg->nodes);
		FREE_IF_SET(msg->script);
		FREE_IF_SET(msg->err);
		FREE_IF_SET(msg->in);
		FREE_IF_SET(msg->out);
		FREE_IF_SET(msg->work_dir);

		for (i = 0; i < msg->argc; i++) {
			FREE_IF_SET(msg->argv[i]);
		}
		FREE_IF_SET(msg->argv);

		if (msg->environment) {
			for (i = 0; i < msg->envc; i++) {
				FREE_IF_SET(msg->environment[i]);
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
		FREE_IF_SET(job->nodes);
		FREE_IF_SET(job->partition);
		FREE_IF_SET(job->name);
		FREE_IF_SET(job->node_inx);
		FREE_IF_SET(job->req_nodes);
		FREE_IF_SET(job->features);
		xfree(job->req_node_inx);
	}
}

void slurm_free_node_registration_status_msg
	(slurm_node_registration_status_msg_t * msg)
{
	if (msg) {
		FREE_IF_SET(msg->node_name);
		FREE_IF_SET(msg->job_id);
		FREE_IF_SET(msg->step_id);
		xfree(msg);
	}
}


void slurm_free_update_node_msg(update_node_msg_t * msg)
{
	if (msg) {
		FREE_IF_SET(msg->node_names);
		xfree(msg);
	}
}

void slurm_free_update_part_msg(update_part_msg_t * msg)
{
	if (msg) {
		FREE_IF_SET(msg->name);
		FREE_IF_SET(msg->nodes);
		FREE_IF_SET(msg->allow_groups);
		xfree(msg);
	}
}

void slurm_free_job_step_create_request_msg(job_step_create_request_msg_t *
					    msg)
{
	if (msg) {
		FREE_IF_SET(msg->node_list);
		xfree(msg);
	}
}

void slurm_free_job_complete_msg(complete_job_step_msg_t * msg)
{
	if (msg) {
		FREE_IF_SET(msg->node_name);
		xfree(msg);
	}
}


void slurm_free_launch_tasks_response_msg(launch_tasks_response_msg_t *
					  msg)
{
	if (msg) {
		FREE_IF_SET(msg->node_name);
		FREE_IF_SET(msg->local_pids);
		xfree(msg);
	}
}

void slurm_free_revoke_credential_msg(revoke_credential_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_update_job_time_msg(job_time_msg_t * msg)
{
	FREE_IF_SET(msg);
}

void slurm_free_task_exit_msg(task_exit_msg_t * msg)
{
	if (msg) {
		FREE_IF_SET(msg->task_id_list);
		xfree(msg);
	}
}

void slurm_free_launch_tasks_request_msg(launch_tasks_request_msg_t * msg)
{
	int i;
	if (msg) {
		FREE_IF_SET(msg->credential);
		if (msg->env) {
			for (i = 0; i < msg->envc; i++) {
				FREE_IF_SET(msg->env[i]);
			}
			xfree(msg->env);
		}
		FREE_IF_SET(msg->cwd);
		if (msg->argv) {
			for (i = 0; i < msg->argc; i++) {
				FREE_IF_SET(msg->argv[i]);
			}
			xfree(msg->argv);
		}
		FREE_IF_SET(msg->global_task_ids);
		FREE_IF_SET(msg->ofname);
		FREE_IF_SET(msg->ofname);
		FREE_IF_SET(msg->ofname);

#		ifdef HAVE_LIBELAN3
		qsw_free_jobinfo(msg->qsw_job);
#		endif

		xfree(msg);
	}
}

void slurm_free_reattach_tasks_request_msg(reattach_tasks_request_msg_t *msg)
{
	if (msg) {
		FREE_IF_SET(msg->ofname);
		FREE_IF_SET(msg->efname);
		FREE_IF_SET(msg->ifname);
		xfree(msg);
	}
}

void slurm_free_reattach_tasks_response_msg(reattach_tasks_response_msg_t *msg)
{
	if (msg) {
		FREE_IF_SET(msg->node_name);
		FREE_IF_SET(msg->executable_name);
		FREE_IF_SET(msg->local_pids);
		FREE_IF_SET(msg->gids);
		xfree(msg);
	}
}

void slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg)
{
	FREE_IF_SET(msg);
}

char *job_state_string(enum job_states inx)
{
	static char *job_state_string[] = {
		"PENDING",
		"RUNNING",
		"COMPLETE",
		"FAILED",
		"TIMEOUT",
		"NODE_FAIL",
		"END"
	};
	return job_state_string[inx];
}

char *job_state_string_compact(enum job_states inx)
{
	static char *job_state_string[] = {
		"PD",
		"R",
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
	static char *node_down_string[] = {
		"NoResp+DOWN",
		"NoResp+UNKNOWN",
		"NoResp+IDLE",
		"NoResp+ALLOCATED",
		"NoResp+DRAINED",
		"NoResp+DRAINING",
		"END"
	};
	if (inx & NODE_STATE_NO_RESPOND) {
		inx = (uint16_t) (inx & (~NODE_STATE_NO_RESPOND));
		return node_down_string[inx];
	}
	else
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
	inx = (uint16_t) (inx & (~NODE_STATE_NO_RESPOND));
	return node_state_string[inx];
}
