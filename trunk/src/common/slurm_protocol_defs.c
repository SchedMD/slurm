/*****************************************************************************\
 *  slurm_protocol_defs.c - functions for initializing and releasing
 *	storage for RPC data structures. these are the functions used by
 *	the slurm daemons directly, not for user client use.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <stdio.h>

#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/job_options.h"
#include "src/common/forward.h"
#include "src/common/slurm_jobacct_gather.h"
#ifdef HAVE_BG
#include "src/plugins/select/bluegene/wrap_rm_api.h"
#endif

static void _free_all_job_info (job_info_msg_t *msg);

static void _free_all_node_info (node_info_msg_t *msg);
static void _slurm_free_node_info_members (node_info_t * node);

static void _free_all_partitions (partition_info_msg_t *msg);
static void _slurm_free_partition_info_members (partition_info_t * part);

static void  _free_all_reservations(reserve_info_msg_t *msg);
static void _slurm_free_reserve_info_members (reserve_info_t * part);

static void _free_all_step_info (job_step_info_response_msg_t *msg);
static void _slurm_free_job_step_info_members (job_step_info_t * msg);
static void _make_lower(char *change);

/*
 * slurm_msg_t_init - initialize a slurm message
 * OUT msg - pointer to the slurm_msg_t structure which will be initialized
 */
extern void slurm_msg_t_init(slurm_msg_t *msg)
{
	memset(msg, 0, sizeof(slurm_msg_t));

	msg->msg_type = (uint16_t)NO_VAL;
	msg->conn_fd = -1;

	forward_init(&msg->forward, NULL);

	return;
}

/*
 * slurm_msg_t_copy - initialize a slurm_msg_t structure "dest" with
 *	values from the "src" slurm_msg_t structure.
 * IN src - Pointer to the initialized message from which "dest" will
 *	be initialized.
 * OUT dest - Pointer to the slurm_msg_t which will be intialized.
 * NOTE: the "dest" structure will contain pointers into the contents of "src".
 */
extern void slurm_msg_t_copy(slurm_msg_t *dest, slurm_msg_t *src)
{
	slurm_msg_t_init(dest);
	dest->forward = src->forward;
	dest->ret_list = src->ret_list;
	dest->forward_struct = src->forward_struct;
	dest->orig_addr.sin_addr.s_addr = 0;
	return;
}

extern void slurm_destroy_char(void *object)
{
	char *tmp = (char *)object;
	xfree(tmp);
}

extern void slurm_destroy_uint32_ptr(void *object)
{
	uint32_t *tmp = (uint32_t *)object;
	xfree(tmp);
}

/* returns number of objects added to list */
extern int slurm_addto_char_list(List char_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if(!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if(names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while(names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if(quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if(names[i] == ',') {
				name = xmalloc((i-start+1));
				memcpy(name, names+start, (i-start));
				//info("got %s %d", name, i-start);

				while((tmp_char = list_next(itr))) {
					if(!strcasecmp(tmp_char, name))
						break;
				}
				/* If we get a duplicate remove the
				   first one and tack this on the end.
				   This is needed for get associations
				   with qos.
				*/
				if(tmp_char)
					list_delete_item(itr);
				else
					count++;

				_make_lower(name);
				list_append(char_list, name);

				list_iterator_reset(itr);

				i++;
				start = i;
				if(!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}

		name = xmalloc((i-start)+1);
		memcpy(name, names+start, (i-start));
		while((tmp_char = list_next(itr))) {
			if(!strcasecmp(tmp_char, name))
				break;
		}

		/* If we get a duplicate remove the
		   first one and tack this on the end.
		   This is needed for get associations
		   with qos.
		*/
		if(tmp_char)
			list_delete_item(itr);
		else
			count++;

		_make_lower(name);
		list_append(char_list, name);
	}
	list_iterator_destroy(itr);
	return count;
}

extern int slurm_sort_char_list_asc(char *name_a, char *name_b)
{
	int diff = strcmp(name_a, name_b);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;

	return 0;
}

extern int slurm_sort_char_list_desc(char *name_a, char *name_b)
{
	int diff = strcmp(name_a, name_b);

	if (diff > 0)
		return -1;
	else if (diff < 0)
		return 1;

	return 0;
}

void slurm_free_last_update_msg(last_update_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_shutdown_msg(shutdown_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_alloc_info_msg(job_alloc_info_msg_t * msg)
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

void slurm_free_job_step_id_msg(job_step_id_msg_t * msg)
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
		xfree(msg->account);
		xfree(msg->alloc_node);
		for (i = 0; i < msg->argc; i++)
			xfree(msg->argv[i]);
		xfree(msg->argv);
		xfree(msg->blrtsimage);
		xfree(msg->ckpt_dir);
		xfree(msg->comment);
		xfree(msg->cpu_bind);
		xfree(msg->dependency);
		for (i = 0; i < msg->env_size; i++)
			xfree(msg->environment[i]);
		xfree(msg->environment);
		xfree(msg->std_err);
		xfree(msg->exc_nodes);
		xfree(msg->features);
		xfree(msg->std_in);
		xfree(msg->licenses);
		xfree(msg->linuximage);
		xfree(msg->mail_user);
		xfree(msg->mem_bind);
		xfree(msg->mloaderimage);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->std_out);
		xfree(msg->partition);
		xfree(msg->ramdiskimage);
		xfree(msg->req_nodes);
		xfree(msg->reservation);
		xfree(msg->resp_host);
		xfree(msg->script);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		for (i = 0; i < msg->spank_job_env_size; i++)
			xfree(msg->spank_job_env[i]);
		xfree(msg->spank_job_env);
		xfree(msg->wckey);
		xfree(msg->work_dir);
		xfree(msg);
	}
}

void slurm_free_job_launch_msg(batch_job_launch_msg_t * msg)
{
	int i;

	if (msg) {
		xfree(msg->nodes);
		xfree(msg->cpu_bind);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg->script);
		xfree(msg->std_err);
		xfree(msg->std_in);
		xfree(msg->std_out);
		xfree(msg->work_dir);
		xfree(msg->ckpt_dir);
		xfree(msg->restart_dir);

		for (i = 0; i < msg->argc; i++)
			xfree(msg->argv[i]);
		xfree(msg->argv);
		for (i = 0; i < msg->spank_job_env_size; i++)
			xfree(msg->spank_job_env[i]);
		xfree(msg->spank_job_env);

		if (msg->environment) {
			for (i = 0; i < msg->envc; i++)
				xfree(msg->environment[i]);
			xfree(msg->environment);
		}

		select_g_select_jobinfo_free(msg->select_jobinfo);
		slurm_cred_destroy(msg->cred);

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
		xfree(job->account);
		xfree(job->alloc_node);
		xfree(job->command);
		xfree(job->comment);
		xfree(job->dependency);
		xfree(job->exc_nodes);
		xfree(job->exc_node_inx);
		xfree(job->features);
		xfree(job->licenses);
		xfree(job->name);
		xfree(job->network);
		xfree(job->node_inx);
		xfree(job->nodes);
		xfree(job->partition);
		xfree(job->req_node_inx);
		xfree(job->req_nodes);
		xfree(job->resv_name);
		select_g_select_jobinfo_free(job->select_jobinfo);
		free_job_resources(&job->job_resrcs);
		xfree(job->state_desc);
		xfree(job->wckey);
		xfree(job->work_dir);
	}
}

void slurm_free_node_registration_status_msg(
	slurm_node_registration_status_msg_t * msg)
{
	if (msg) {
		xfree(msg->arch);
		xfree(msg->job_id);
		xfree(msg->node_name);
		xfree(msg->os);
		xfree(msg->step_id);
		if (msg->startup)
			switch_g_free_node_info(&msg->switch_nodeinfo);
		xfree(msg);
	}
}


void slurm_free_update_node_msg(update_node_msg_t * msg)
{
	if (msg) {
		xfree(msg->features);
		xfree(msg->node_names);
		xfree(msg->reason);
		xfree(msg);
	}
}

void slurm_free_update_part_msg(update_part_msg_t * msg)
{
	if (msg) {
		_slurm_free_partition_info_members((partition_info_t *)msg);
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

void slurm_free_resv_desc_msg(resv_desc_msg_t * msg)
{
	if (msg) {
		xfree(msg->accounts);
		xfree(msg->features);
		xfree(msg->licenses);
		xfree(msg->name);
		xfree(msg->node_list);
		xfree(msg->partition);
		xfree(msg->users);
		xfree(msg);
	}
}

void slurm_free_resv_name_msg(reservation_name_msg_t * msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg);
	}
}

void slurm_free_resv_info_request_msg(resv_info_request_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_job_step_create_request_msg(job_step_create_request_msg_t *
					    msg)
{
	if (msg) {
		xfree(msg->host);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->node_list);
		xfree(msg->ckpt_dir);
		xfree(msg);
	}
}

void slurm_free_complete_job_allocation_msg(
	complete_job_allocation_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_complete_batch_script_msg(complete_batch_script_msg_t * msg)
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
		xfree(msg->task_ids);
		xfree(msg);
	}
}

void slurm_free_kill_job_msg(kill_job_msg_t * msg)
{
	if (msg) {
		int i;
		xfree(msg->nodes);
		select_g_select_jobinfo_free(msg->select_jobinfo);
		for (i=0; i<msg->spank_job_env_size; i++)
			xfree(msg->spank_job_env[i]);
		xfree(msg->spank_job_env);
		xfree(msg);
	}
}

void slurm_free_signal_job_msg(signal_job_msg_t * msg)
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
	xfree(msg->cpu_bind);
	xfree(msg->mem_bind);
	if (msg->argv) {
		for (i = 0; i < msg->argc; i++) {
			xfree(msg->argv[i]);
		}
		xfree(msg->argv);
	}
	for (i = 0; i < msg->spank_job_env_size; i++) {
		xfree(msg->spank_job_env[i]);
	}
	xfree(msg->spank_job_env);
	if(msg->nnodes && msg->global_task_ids)
		for(i=0; i<msg->nnodes; i++) {
			xfree(msg->global_task_ids[i]);
		}
	xfree(msg->tasks_to_launch);
	xfree(msg->cpus_allocated);
	xfree(msg->resp_port);
	xfree(msg->io_port);
	xfree(msg->global_task_ids);
	xfree(msg->ifname);
	xfree(msg->ofname);
	xfree(msg->efname);

	xfree(msg->task_prolog);
	xfree(msg->task_epilog);
	xfree(msg->complete_nodelist);

	xfree(msg->ckpt_dir);
	xfree(msg->restart_dir);

	if (msg->switch_job)
		switch_free_jobinfo(msg->switch_job);

	if (msg->options)
		job_options_destroy(msg->options);

	xfree(msg);
}

void slurm_free_task_user_managed_io_stream_msg(task_user_managed_io_msg_t *msg)
{
	xfree(msg);
}

void slurm_free_reattach_tasks_request_msg(reattach_tasks_request_msg_t *msg)
{
	if (msg) {
		xfree(msg->resp_port);
		xfree(msg->io_port);
		slurm_cred_destroy(msg->cred);
		xfree(msg);
	}
}

void slurm_free_reattach_tasks_response_msg(reattach_tasks_response_msg_t *msg)
{
	int i;

	if (msg) {
		xfree(msg->node_name);
		xfree(msg->local_pids);
		xfree(msg->gtids);
		for (i = 0; i < msg->ntasks; i++) {
			xfree(msg->executable_names[i]);
		}
		xfree(msg->executable_names);
		xfree(msg);
	}
}

void slurm_free_kill_tasks_msg(kill_tasks_msg_t * msg)
{
	xfree(msg);
}

void slurm_free_checkpoint_tasks_msg(checkpoint_tasks_msg_t * msg)
{
	if (msg) {
		xfree(msg->image_dir);
		xfree(msg);
	}
}

void slurm_free_epilog_complete_msg(epilog_complete_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_name);
		switch_g_free_node_info(&msg->switch_nodeinfo);
		xfree(msg);
	}
}

void inline slurm_free_srun_job_complete_msg(srun_job_complete_msg_t * msg)
{
	xfree(msg);
}

void inline slurm_free_srun_exec_msg(srun_exec_msg_t *msg)
{
	int i;

	if (msg) {
		for (i = 0; i < msg->argc; i++)
			xfree(msg->argv[i]);
		xfree(msg->argv);
		xfree(msg);
	}
}

void inline slurm_free_srun_ping_msg(srun_ping_msg_t * msg)
{
	xfree(msg);
}

void inline slurm_free_srun_node_fail_msg(srun_node_fail_msg_t * msg)
{
	if (msg) {
		xfree(msg->nodelist);
		xfree(msg);
	}
}

void inline slurm_free_srun_step_missing_msg(srun_step_missing_msg_t * msg)
{
	if (msg) {
		xfree(msg->nodelist);
		xfree(msg);
	}
}

void inline slurm_free_srun_timeout_msg(srun_timeout_msg_t * msg)
{
	xfree(msg);
}

void inline slurm_free_srun_user_msg(srun_user_msg_t * user_msg)
{
	if (user_msg) {
		xfree(user_msg->msg);
		xfree(user_msg);
	}
}

void inline slurm_free_checkpoint_msg(checkpoint_msg_t *msg)
{
	if (msg) {
		xfree(msg->image_dir);
		xfree(msg);
	}
}

void inline slurm_free_checkpoint_comp_msg(checkpoint_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->error_msg);
		xfree(msg);
	}
}

void inline slurm_free_checkpoint_task_comp_msg(checkpoint_task_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->error_msg);
		xfree(msg);
	}
}

void inline slurm_free_checkpoint_resp_msg(checkpoint_resp_msg_t *msg)
{
	if (msg) {
		xfree(msg->error_msg);
		xfree(msg);
	}
}
void inline slurm_free_suspend_msg(suspend_msg_t *msg)
{
	xfree(msg);
}

/* Given a job's reason for waiting, return a descriptive string */
extern char *job_reason_string(enum job_state_reason inx)
{
	switch (inx) {
	case WAIT_NO_REASON:
		return "None";
	case WAIT_PRIORITY:
		return "Priority";
	case WAIT_DEPENDENCY:
		return "Dependency";
	case WAIT_RESOURCES:
		return "Resources";
	case WAIT_PART_NODE_LIMIT:
		return "PartitionNodeLimit";
	case WAIT_PART_TIME_LIMIT:
		return "PartitionTimeLimit";
	case WAIT_PART_STATE:
		return "PartitionDown";
	case WAIT_HELD:
		return "JobHeld";
	case WAIT_TIME:
		return "BeginTime";
	case WAIT_LICENSES:
		return "Licenses";
	case WAIT_ASSOC_JOB_LIMIT:
		return "AssociationJobLimit";
	case WAIT_ASSOC_RESOURCE_LIMIT:
		return "AssociationResourceLimit";
	case WAIT_ASSOC_TIME_LIMIT:
		return "AssociationTimeLimit";
	case WAIT_RESERVATION:
		return "Reservation";
	case WAIT_NODE_NOT_AVAIL:
		return "ReqNodeNotAvail";
	case FAIL_DOWN_PARTITION:
		return "PartitionDown";
	case FAIL_DOWN_NODE:
		return "NodeDown";
	case FAIL_BAD_CONSTRAINTS:
		return "BadConstraints";
	case FAIL_SYSTEM:
		return "SystemFailure";
	case FAIL_LAUNCH:
		return "JobLaunchFailure";
	case FAIL_EXIT_CODE:
		return "NonZeroExitCode";
	case FAIL_TIMEOUT:
		return "TimeLimit";
	case FAIL_INACTIVE_LIMIT:
		return "InactiveLimit";
	case FAIL_BANK_ACCOUNT:
		return "InvalidBankAccount";
	default:
		return "?";
	}
}

void inline slurm_free_get_kvs_msg(kvs_get_msg_t *msg)
{
	if (msg) {
		xfree(msg->hostname);
		xfree(msg);
	}
}

void inline slurm_free_will_run_response_msg(will_run_response_msg_t *msg)
{
        if (msg) {
                xfree(msg->node_list);
		if (msg->preemptee_job_id)
			list_destroy(msg->preemptee_job_id);
                xfree(msg);
        }
}

char *job_state_string(uint16_t inx)
{
	/* Process JOB_STATE_FLAGS */
	if (inx & JOB_COMPLETING)
		return "COMPLETING";
	if (inx & JOB_CONFIGURING)
		return "CONFIGURING";

	/* Process JOB_STATE_BASE */
	switch (inx & JOB_STATE_BASE) {
	case JOB_PENDING:
		return "PENDING";
	case JOB_RUNNING:
		return "RUNNING";
	case JOB_SUSPENDED:
		return "SUSPENDED";
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

char *job_state_string_compact(uint16_t inx)
{
	/* Process JOB_STATE_FLAGS */
	if (inx & JOB_COMPLETING)
		return "CG";
	if (inx & JOB_CONFIGURING)
		return "CF";

	/* Process JOB_STATE_BASE */
	switch (inx & JOB_STATE_BASE) {
	case JOB_PENDING:
		return "PD";
	case JOB_RUNNING:
		return "R";
	case JOB_SUSPENDED:
		return "S";
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

extern char *reservation_flags_string(uint16_t flags)
{
	char *flag_str = xstrdup("");

	if (flags & RESERVE_FLAG_MAINT)
		xstrcat(flag_str, "MAINT");
	if (flags & RESERVE_FLAG_NO_MAINT) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_MAINT");
	}
	if (flags & RESERVE_FLAG_OVERLAP) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "OVERLAP");
	}
	if (flags & RESERVE_FLAG_IGN_JOBS) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "IGNORE_JOBS");
	}
	if (flags & RESERVE_FLAG_DAILY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "DAILY");
	}
	if (flags & RESERVE_FLAG_NO_DAILY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_DAILY");
	}
	if (flags & RESERVE_FLAG_WEEKLY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "WEEKLY");
	}
	if (flags & RESERVE_FLAG_NO_WEEKLY) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "NO_WEEKLY");
	}
	if (flags & RESERVE_FLAG_SPEC_NODES) {
		if (flag_str[0])
			xstrcat(flag_str, ",");
		xstrcat(flag_str, "SPEC_NODES");
	}
	return flag_str;
}

char *node_state_string(uint16_t inx)
{
	int  base            = (inx & NODE_STATE_BASE);
	bool comp_flag       = (inx & NODE_STATE_COMPLETING);
	bool drain_flag      = (inx & NODE_STATE_DRAIN);
	bool fail_flag       = (inx & NODE_STATE_FAIL);
	bool maint_flag      = (inx & NODE_STATE_MAINT);
	bool no_resp_flag    = (inx & NODE_STATE_NO_RESPOND);
	bool power_down_flag = (inx & NODE_STATE_POWER_SAVE);
	bool power_up_flag   = (inx & NODE_STATE_POWER_UP);

	if (maint_flag) {
		if (no_resp_flag)
			return "MAINT*";
		return "MAINT";
	}
	if (drain_flag) {
		if (comp_flag || (base == NODE_STATE_ALLOCATED)) {
			if (no_resp_flag)
				return "DRAINING*";
			return "DRAINING";
		} else if (base == NODE_STATE_ERROR) {
			if (no_resp_flag)
				return "ERROR*";
			return "ERROR";
		} else if (base == NODE_STATE_MIXED) {
			if (no_resp_flag)
				return "MIXED*";
			return "MIXED";
		} else {
			if (no_resp_flag)
				return "DRAINED*";
			return "DRAINED";
		}
	}
	if (fail_flag) {
		if (comp_flag || (base == NODE_STATE_ALLOCATED)) {
			if (no_resp_flag)
				return "FAILING*";
			return "FAILING";
		} else {
			if (no_resp_flag)
				return "FAIL*";
			return "FAIL";
		}
	}

	if (inx == NODE_STATE_POWER_SAVE)
		return "POWER_DOWN";
	if (inx == NODE_STATE_POWER_UP)
		return "POWER_UP";
	if (base == NODE_STATE_DOWN) {
		if (no_resp_flag)
			return "DOWN*";
		return "DOWN";
	}

	if (base == NODE_STATE_ALLOCATED) {
		if (power_up_flag)
			return "ALLOCATED#";
		if (power_down_flag)
			return "ALLOCATED~";
		if (no_resp_flag)
			return "ALLOCATED*";
		if (comp_flag)
			return "ALLOCATED+";
		return "ALLOCATED";
	}
	if (comp_flag) {
		if (no_resp_flag)
			return "COMPLETING*";
		return "COMPLETING";
	}
	if (base == NODE_STATE_IDLE) {
		if (power_up_flag)
			return "IDLE#";
		if (power_down_flag)
			return "IDLE~";
		if (no_resp_flag)
			return "IDLE*";
		return "IDLE";
	}
	if (base == NODE_STATE_ERROR) {
		if (power_up_flag)
			return "ERROR#";
		if (power_down_flag)
			return "ERROR~";
		if (no_resp_flag)
			return "ERROR*";
		return "ERROR";
	}
	if (base == NODE_STATE_MIXED) {
		if (power_up_flag)
			return "MIXED#";
		if (power_down_flag)
			return "MIXED~";
		if (no_resp_flag)
			return "MIXED*";
		return "MIXED";
	}
	if (base == NODE_STATE_FUTURE) {
		if (no_resp_flag)
			return "FUTURE*";
		return "FUTURE";
	}
	if (base == NODE_STATE_UNKNOWN) {
		if (no_resp_flag)
			return "UNKNOWN*";
		return "UNKNOWN";
	}
	return "?";
}

char *node_state_string_compact(uint16_t inx)
{
	bool comp_flag       = (inx & NODE_STATE_COMPLETING);
	bool drain_flag      = (inx & NODE_STATE_DRAIN);
	bool fail_flag       = (inx & NODE_STATE_FAIL);
	bool maint_flag      = (inx & NODE_STATE_MAINT);
	bool no_resp_flag    = (inx & NODE_STATE_NO_RESPOND);
	bool power_down_flag = (inx & NODE_STATE_POWER_SAVE);
	bool power_up_flag   = (inx & NODE_STATE_POWER_UP);

	inx = (uint16_t) (inx & NODE_STATE_BASE);

	if (maint_flag) {
		if (no_resp_flag)
			return "MAINT*";
		return "MAINT";
	}
	if (drain_flag) {
		if (comp_flag || (inx == NODE_STATE_ALLOCATED)) {
			if (no_resp_flag)
				return "DRNG*";
			return "DRNG";
		} else if (inx == NODE_STATE_ERROR) {
			if (no_resp_flag)
				return "ERROR*";
			return "ERROR";
		} else if (inx == NODE_STATE_MIXED) {
			if (no_resp_flag)
				return "MIXED*";
			return "MIXED";
		} else {
			if (no_resp_flag)
				return "DRAIN*";
			return "DRAIN";
		}
	}
	if (fail_flag) {
		if (comp_flag || (inx == NODE_STATE_ALLOCATED)) {
			if (no_resp_flag)
				return "FAILG*";
			return "FAILG";
		} else {
			if (no_resp_flag)
				return "FAIL*";
			return "FAIL";
		}
	}

	if (inx == NODE_STATE_POWER_SAVE)
		return "POW_DN";
	if (inx == NODE_STATE_POWER_UP)
		return "POW_UP";
	if (inx == NODE_STATE_DOWN) {
		if (no_resp_flag)
			return "DOWN*";
		return "DOWN";
	}

	if (inx == NODE_STATE_ALLOCATED) {
		if (power_up_flag)
			return "ALLOC#";
		if (power_down_flag)
			return "ALLOC~";
		if (no_resp_flag)
			return "ALLOC*";
		if (comp_flag)
			return "ALLOC+";
		return "ALLOC";
	}
	if (comp_flag) {
		if (no_resp_flag)
			return "COMP*";
		return "COMP";
	}
	if (inx == NODE_STATE_IDLE) {
		if (power_up_flag)
			return "IDLE#";
		if (power_down_flag)
			return "IDLE~";
		if (no_resp_flag)
			return "IDLE*";
		return "IDLE";
	}
	if (inx == NODE_STATE_ERROR) {
		if (power_up_flag)
			return "ERR#";
		if (power_down_flag)
			return "ERR~";
		if (no_resp_flag)
			return "ERR*";
		return "ERR";
	}
	if (inx == NODE_STATE_MIXED) {
		if (power_up_flag)
			return "MIX#";
		if (power_down_flag)
			return "MIX~";
		if (no_resp_flag)
			return "MIX*";
		return "MIX";
	}
	if (inx == NODE_STATE_FUTURE) {
		if (no_resp_flag)
			return "FUTR*";
		return "FUTR";
	}
	if (inx == NODE_STATE_UNKNOWN) {
		if (no_resp_flag)
			return "UNK*";
		return "UNK";
	}
	return "?";
}


extern void
private_data_string(uint16_t private_data, char *str, int str_len)
{
	if (str_len > 0)
		str[0] = '\0';
	if (str_len < 42) {
		error("private_data_string: output buffer too small");
		return;
	}

	if (private_data & PRIVATE_DATA_JOBS)
		strcat(str, "jobs"); //4 len
	if (private_data & PRIVATE_DATA_NODES) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "nodes"); //6 len
	}
	if (private_data & PRIVATE_DATA_PARTITIONS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "partitions"); //11 len
	}
	if (private_data & PRIVATE_DATA_USAGE) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "usage"); //6 len
	}
	if (private_data & PRIVATE_DATA_USERS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "users"); //6 len
	}
	if (private_data & PRIVATE_DATA_ACCOUNTS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "accounts"); //9 len
	}
	// total len 42

	if (str[0] == '\0')
		strcat(str, "none");
}

extern void
accounting_enforce_string(uint16_t enforce, char *str, int str_len)
{
	if (str_len > 0)
		str[0] = '\0';
	if (str_len < 30) {
		error("enforce: output buffer too small");
		return;
	}

	if (enforce & ACCOUNTING_ENFORCE_ASSOCS)
		strcat(str, "associations"); //12 len
	if (enforce & ACCOUNTING_ENFORCE_LIMITS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "limits"); //7 len
	}
	if (enforce & ACCOUNTING_ENFORCE_QOS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "qos"); //4 len
	}
	if (enforce & ACCOUNTING_ENFORCE_WCKEYS) {
		if (str[0])
			strcat(str, ",");
		strcat(str, "wckeys"); //7 len
	}
	// total len 30

	if (str[0] == '\0')
		strcat(str, "none");
}

extern char *conn_type_string(enum connection_type conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH):
		return "Mesh";
	case (SELECT_TORUS):
		return "Torus";
	case (SELECT_SMALL):
		return "Small";
	case (SELECT_NAV):
		return "NAV";
#ifndef HAVE_BGL
	case SELECT_HTC_S:
		return "HTC_S";
	case SELECT_HTC_D:
		return "HTC_D";
	case SELECT_HTC_V:
		return "HTC_V";
	case SELECT_HTC_L:
		return "HTC_L";
#endif
	default:
		return "n/a";
	}
	return "n/a";
}

#ifdef HAVE_BGL
extern char* node_use_string(enum node_use_type node_use)
{
	switch (node_use) {
	case (SELECT_COPROCESSOR_MODE):
		return "COPROCESSOR";
	case (SELECT_VIRTUAL_NODE_MODE):
		return "VIRTUAL";
	default:
		break;
	}
	return "";
}
#endif

extern char *bg_block_state_string(uint16_t state)
{
	static char tmp[16];

#ifdef HAVE_BG
	switch ((rm_partition_state_t)state) {
#ifdef HAVE_BGL
	case RM_PARTITION_BUSY:
		return "BUSY";
#else
	case RM_PARTITION_REBOOTING:
		return "REBOOTING";
#endif
	case RM_PARTITION_CONFIGURING:
		return "CONFIG";
	case RM_PARTITION_DEALLOCATING:
		return "DEALLOC";
	case RM_PARTITION_ERROR:
		return "ERROR";
	case RM_PARTITION_FREE:
		return "FREE";
	case RM_PARTITION_NAV:
		return "NAV";
	case RM_PARTITION_READY:
		return "READY";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
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
		select_g_select_jobinfo_free(msg->select_jobinfo);
		xfree(msg->node_list);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg);
	}
}

/*
 * slurm_free_sbcast_cred_msg - free slurm resource allocation response
 *	message including an sbcast credential
 * IN msg - pointer to response message from slurm_sbcast_lookup()
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
void slurm_free_sbcast_cred_msg(job_sbcast_cred_msg_t * msg)
{
	if (msg) {
		xfree(msg->node_addr);
		xfree(msg->node_list);
		delete_sbcast_cred(msg->sbcast_cred);
		xfree(msg);
	}
}

/*
 * slurm_free_job_alloc_info_response_msg - free slurm job allocation
 *	                                    info response message
 * IN msg - pointer to job allocation info response message
 * NOTE: buffer is loaded by slurm_allocate_resources
 */
void slurm_free_job_alloc_info_response_msg(job_alloc_info_response_msg_t *msg)
{
	if (msg) {
		select_g_select_jobinfo_free(msg->select_jobinfo);
		xfree(msg->node_list);
		xfree(msg->cpus_per_node);
		xfree(msg->cpu_count_reps);
		xfree(msg->node_addr);
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
		xfree(msg->resv_ports);
		slurm_step_layout_destroy(msg->step_layout);
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
		free_slurm_conf(config_ptr, 0);
		xfree(config_ptr);
	}
}

/*
 * slurm_free_slurmd_status - free slurmd state information
 * IN msg - pointer to slurmd state information
 * NOTE: buffer is loaded by slurm_load_slurmd_status
 */
extern void slurm_free_slurmd_status(slurmd_status_t* slurmd_status_ptr)
{
	if (slurmd_status_ptr) {
		xfree(slurmd_status_ptr->hostname);
		xfree(slurmd_status_ptr->slurmd_logfile);
		xfree(slurmd_status_ptr->step_list);
		xfree(slurmd_status_ptr->version);
		xfree(slurmd_status_ptr);
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
		slurm_free_job_info_members (&msg->job_array[i]);
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
		xfree(msg->resv_ports);
		xfree(msg->nodes);
		xfree(msg->name);
		xfree(msg->network);
		xfree(msg->node_inx);
		xfree(msg->ckpt_dir);
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
		xfree(node->arch);
		xfree(node->features);
		xfree(node->os);
		xfree(node->reason);
		select_g_select_nodeinfo_free(node->select_nodeinfo);
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
		xfree(part->allow_alloc_nodes);
		xfree(part->allow_groups);
		xfree(part->name);
		xfree(part->nodes);
		xfree(part->node_inx);
	}
}

/*
 * slurm_free_reserve_info_msg - free the reservation information
 *	response message
 * IN msg - pointer to reservation information response message
 * NOTE: buffer is loaded by slurm_load_reservation
 */
void slurm_free_reservation_info_msg(reserve_info_msg_t * msg)
{
	if (msg) {
		if (msg->reservation_array) {
			_free_all_reservations(msg);
			xfree(msg->reservation_array);
		}
		xfree(msg);
	}
}

static void  _free_all_reservations(reserve_info_msg_t *msg)
{
	int i;

	if ((msg == NULL) ||
	    (msg->reservation_array == NULL))
		return;

	for (i = 0; i < msg->record_count; i++)
		_slurm_free_reserve_info_members(
			&msg->reservation_array[i]);

}

static void _slurm_free_reserve_info_members(reserve_info_t * resv)
{
	if (resv) {
		xfree(resv->accounts);
		xfree(resv->features);
		xfree(resv->licenses);
		xfree(resv->name);
		xfree(resv->node_inx);
		xfree(resv->node_list);
		xfree(resv->partition);
		xfree(resv->users);
	}
}

/*
 * slurm_free_topo_info_msg - free the switch topology configuration
 *	information response message
 * IN msg - pointer to switch topology configuration response message
 * NOTE: buffer is loaded by slurm_load_topo.
 */
extern void slurm_free_topo_info_msg(topo_info_response_msg_t *msg)
{
	int i;

	if (msg) {
		for (i = 0; i < msg->record_count; i++) {
			xfree(msg->topo_array[i].name);
			xfree(msg->topo_array[i].nodes);
			xfree(msg->topo_array[i].switches);
		}
		xfree(msg);
	}
}


extern void slurm_free_file_bcast_msg(file_bcast_msg_t *msg)
{
	if (msg) {
		xfree(msg->block);
		xfree(msg->fname);
		delete_sbcast_cred(msg->cred);
		xfree(msg);
	}
}

extern void slurm_free_step_complete_msg(step_complete_msg_t *msg)
{
	if (msg) {
		jobacct_gather_g_destroy(msg->jobacct);
		xfree(msg);
	}
}

extern void slurm_free_stat_jobacct_msg(stat_jobacct_msg_t *msg)
{
	if (msg) {
		jobacct_gather_g_destroy(msg->jobacct);
		xfree(msg);
	}
}

void inline slurm_free_block_info_request_msg(
	block_info_request_msg_t *msg)
{
	xfree(msg);
}
void inline slurm_free_trigger_msg(trigger_info_msg_t *msg)
{
	int i;

	for (i=0; i<msg->record_count; i++) {
		xfree(msg->trigger_array[i].res_id);
		xfree(msg->trigger_array[i].program);
	}
	xfree(msg->trigger_array);
	xfree(msg);
}

void slurm_free_set_debug_level_msg(set_debug_level_msg_t *msg)
{
	xfree(msg);
}

void inline slurm_destroy_association_shares_object(void *object)
{
	association_shares_object_t *obj_ptr =
		(association_shares_object_t *)object;

	if(obj_ptr) {
		xfree(obj_ptr->cluster);
		xfree(obj_ptr->name);
		xfree(obj_ptr->parent);
		xfree(obj_ptr);
	}
}

void inline slurm_free_shares_request_msg(shares_request_msg_t *msg)
{
	if(msg) {
		if(msg->acct_list)
			list_destroy(msg->acct_list);
		if(msg->user_list)
			list_destroy(msg->user_list);
		xfree(msg);
	}
}

void inline slurm_free_shares_response_msg(shares_response_msg_t *msg)
{
	if(msg) {
		if(msg->assoc_shares_list)
			list_destroy(msg->assoc_shares_list);
		xfree(msg);
	}
}

void inline slurm_destroy_priority_factors_object(void *object)
{
	priority_factors_object_t *obj_ptr =
		(priority_factors_object_t *)object;
	xfree(obj_ptr);
}

void inline slurm_free_priority_factors_request_msg(
	priority_factors_request_msg_t *msg)
{
	if(msg) {
		if(msg->job_id_list)
			list_destroy(msg->job_id_list);
		if(msg->uid_list)
			list_destroy(msg->uid_list);
		xfree(msg);
	}
}

void inline slurm_free_priority_factors_response_msg(
	priority_factors_response_msg_t *msg)
{
	if(msg) {
		if(msg->priority_factors_list)
			list_destroy(msg->priority_factors_list);
		xfree(msg);
	}
}


void inline slurm_free_accounting_update_msg(accounting_update_msg_t *msg)
{
	if(msg) {
		if(msg->update_list)
			list_destroy(msg->update_list);
		xfree(msg);
	}
}

extern int slurm_free_msg_data(slurm_msg_type_t type, void *data)
{
	switch(type) {
	case REQUEST_BUILD_INFO:
		slurm_free_last_update_msg(data);
		break;
	case REQUEST_JOB_INFO:
		slurm_free_job_info_request_msg(data);
		break;
	case REQUEST_NODE_INFO:
		slurm_free_node_info_request_msg(data);
		break;
	case REQUEST_PARTITION_INFO:
		slurm_free_part_info_request_msg(data);
		break;
	case MESSAGE_EPILOG_COMPLETE:
		slurm_free_epilog_complete_msg(data);
		break;
	case REQUEST_CANCEL_JOB_STEP:
		slurm_free_job_step_kill_msg(data);
		break;
	case REQUEST_COMPLETE_JOB_ALLOCATION:
		slurm_free_complete_job_allocation_msg(data);
		break;
	case REQUEST_COMPLETE_BATCH_SCRIPT:
		slurm_free_complete_batch_script_msg(data);
		break;
	case REQUEST_JOB_STEP_CREATE:
		slurm_free_job_step_create_request_msg(data);
		break;
	case REQUEST_JOB_STEP_INFO:
		slurm_free_job_step_info_request_msg(data);
		break;
	case REQUEST_RESOURCE_ALLOCATION:
	case REQUEST_JOB_WILL_RUN:
	case REQUEST_SUBMIT_BATCH_JOB:
	case REQUEST_UPDATE_JOB:
		slurm_free_job_desc_msg(data);
		break;
	case MESSAGE_NODE_REGISTRATION_STATUS:
		slurm_free_node_registration_status_msg(data);
		break;
	case REQUEST_JOB_END_TIME:
	case REQUEST_JOB_ALLOCATION_INFO:
		slurm_free_job_alloc_info_msg(data);
		break;
	case REQUEST_SHUTDOWN:
		slurm_free_shutdown_msg(data);
		break;
	case REQUEST_UPDATE_NODE:
		slurm_free_update_node_msg(data);
		break;
	case REQUEST_CREATE_PARTITION:
	case REQUEST_UPDATE_PARTITION:
		slurm_free_update_part_msg(data);
		break;
	case REQUEST_DELETE_PARTITION:
		slurm_free_delete_part_msg(data);
		break;
	case REQUEST_CREATE_RESERVATION:
	case REQUEST_UPDATE_RESERVATION:
		slurm_free_resv_desc_msg(data);
		break;
	case REQUEST_DELETE_RESERVATION:
	case RESPONSE_CREATE_RESERVATION:
		slurm_free_resv_name_msg(data);
		break;
	case REQUEST_RESERVATION_INFO:
		slurm_free_resv_info_request_msg(data);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		slurm_free_node_registration_status_msg(data);
		break;
	case REQUEST_CHECKPOINT:
		slurm_free_checkpoint_msg(data);
		break;
	case REQUEST_CHECKPOINT_COMP:
		slurm_free_checkpoint_comp_msg(data);
		break;
	case REQUEST_CHECKPOINT_TASK_COMP:
		slurm_free_checkpoint_task_comp_msg(data);
		break;
	case REQUEST_SUSPEND:
		slurm_free_suspend_msg(data);
		break;
	case REQUEST_JOB_READY:
	case REQUEST_JOB_REQUEUE:
	case REQUEST_JOB_INFO_SINGLE:
		slurm_free_job_id_msg(data);
		break;
	case REQUEST_SHARE_INFO:
		slurm_free_shares_request_msg(data);
		break;
	case RESPONSE_SHARE_INFO:
		slurm_free_shares_response_msg(data);
		break;
	case REQUEST_PRIORITY_FACTORS:
		slurm_free_priority_factors_request_msg(data);
		break;
	case RESPONSE_PRIORITY_FACTORS:
		slurm_free_priority_factors_response_msg(data);
		break;
	case REQUEST_BLOCK_INFO:
		slurm_free_block_info_request_msg(data);
		break;
	case REQUEST_STEP_COMPLETE:
		slurm_free_step_complete_msg(data);
		break;
	case MESSAGE_STAT_JOBACCT:
		slurm_free_stat_jobacct_msg(data);
		break;
	case REQUEST_BATCH_JOB_LAUNCH:
		slurm_free_job_launch_msg(data);
		break;
	case REQUEST_LAUNCH_TASKS:
		slurm_free_launch_tasks_request_msg(data);
		break;
	case TASK_USER_MANAGED_IO_STREAM:
		slurm_free_task_user_managed_io_stream_msg(data);
		break;
	case REQUEST_SIGNAL_TASKS:
	case REQUEST_TERMINATE_TASKS:
		slurm_free_kill_tasks_msg(data);
		break;
	case REQUEST_CHECKPOINT_TASKS:
		slurm_free_checkpoint_tasks_msg(data);
		break;
	case REQUEST_KILL_TIMELIMIT:
		slurm_free_timelimit_msg(data);
		break;
	case REQUEST_REATTACH_TASKS:
		slurm_free_reattach_tasks_request_msg(data);
		break;
	case RESPONSE_REATTACH_TASKS:
		slurm_free_reattach_tasks_response_msg(data);
		break;
	case REQUEST_SIGNAL_JOB:
		slurm_free_signal_job_msg(data);
		break;
	case REQUEST_ABORT_JOB:
	case REQUEST_TERMINATE_JOB:
		slurm_free_kill_job_msg(data);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		slurm_free_update_job_time_msg(data);
		break;
	case REQUEST_JOB_ID:
		slurm_free_job_id_request_msg(data);
		break;
	case REQUEST_FILE_BCAST:
		slurm_free_file_bcast_msg(data);
		break;
	case RESPONSE_SLURM_RC:
		slurm_free_return_code_msg(data);
		break;
	case REQUEST_SET_DEBUG_LEVEL:
		slurm_free_set_debug_level_msg(data);
		break;
	case SLURM_SUCCESS:
	case REQUEST_PING:
	case REQUEST_RECONFIGURE:
	case REQUEST_CONTROL:
	case REQUEST_TAKEOVER:
	case REQUEST_SHUTDOWN_IMMEDIATE:
	case RESPONSE_FORWARD_FAILED:
	case REQUEST_DAEMON_STATUS:
	case REQUEST_HEALTH_CHECK:
	case ACCOUNTING_FIRST_REG:
	case REQUEST_TOPO_INFO:
		/* No body to free */
		break;
	case ACCOUNTING_UPDATE_MSG:
		slurm_free_accounting_update_msg(data);
		break;
	case RESPONSE_TOPO_INFO:
		slurm_free_topo_info_msg(data);
		break;
	default:
		error("invalid type trying to be freed %u", type);
		break;
	}
	return SLURM_SUCCESS;
}

extern uint32_t slurm_get_return_code(slurm_msg_type_t type, void *data)
{
	uint32_t rc = 0;

	switch(type) {
	case MESSAGE_EPILOG_COMPLETE:
		rc = ((epilog_complete_msg_t *)data)->return_code;
		break;
	case MESSAGE_STAT_JOBACCT:
		rc = ((stat_jobacct_msg_t *)data)->return_code;
		break;
	case RESPONSE_REATTACH_TASKS:
		rc = ((reattach_tasks_response_msg_t *)data)->return_code;
		break;
	case RESPONSE_JOB_ID:
		rc = ((job_id_response_msg_t *)data)->return_code;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *)data)->return_code;
		break;
	case RESPONSE_FORWARD_FAILED:
		/* There may be other reasons for the failure, but
		 * this may be a slurm_msg_t data type lacking the
		 * err field found in ret_data_info_t data type */
		rc = SLURM_COMMUNICATIONS_CONNECTION_ERROR;
		break;
	default:
		error("don't know the rc for type %u returning %u", type, rc);
		break;
	}
	return rc;
}

void inline slurm_free_job_notify_msg(job_notify_msg_t * msg)
{
	if (msg) {
		xfree(msg->message);
		xfree(msg);
	}
}

/* make everything lowercase should not be called on static char *'s */
static void _make_lower(char *change)
{
	if(change) {
		int j = 0;
		while(change[j]) {
			char lower = tolower(change[j]);
			if(lower != change[j])
				change[j] = lower;
			j++;
		}
	}
}

/* Validate SPANK specified job environment does not contain any invalid
 * names. Log failures using info() */
extern bool valid_spank_job_env(char **spank_job_env,
			        uint32_t spank_job_env_size, uid_t uid)
{
	int i;

	for (i=0; i<spank_job_env_size; i++) {
		if ((strncmp(spank_job_env[i], "LD_PRELOAD=", 11) == 0) ||
		    (strncmp(spank_job_env[i], "PATH=",        5) == 0)) {
			info("Invalid spank_job_env from uid %d: %s",
			     (int) uid, spank_job_env[i]);
			return false;
		}
	}
	return true;
}
