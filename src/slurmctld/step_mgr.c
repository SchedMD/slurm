/*****************************************************************************\
 *  step_mgr.c - manage the job step information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2012-2013 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/param.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/checkpoint.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/switch.h"
#include "src/common/xstring.h"
#include "src/common/slurm_ext_sensors.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

#define MAX_RETRIES 10

static void _build_pending_step(struct job_record  *job_ptr,
				job_step_create_request_msg_t *step_specs);
static int  _count_cpus(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t *usable_cpu_cnt);
static struct step_record * _create_step_record(struct job_record *job_ptr);
static void _dump_step_layout(struct step_record *step_ptr);
static void _free_step_rec(struct step_record *step_ptr);
static bool _is_mem_resv(void);
static int  _opt_cpu_cnt(uint32_t step_min_cpus, bitstr_t *node_bitmap,
			 uint32_t *usable_cpu_cnt);
static int  _opt_node_cnt(uint32_t step_min_nodes, uint32_t step_max_nodes,
			  int nodes_avail, int nodes_picked_cnt);
static void _pack_ctld_job_step_info(struct step_record *step, Buf buffer,
				     uint16_t protocol_version);
static bitstr_t * _pick_step_nodes(struct job_record *job_ptr,
				   job_step_create_request_msg_t *step_spec,
				   List step_gres_list, int cpus_per_task,
				   uint32_t node_count,
				   dynamic_plugin_data_t *select_jobinfo,
				   int *return_code);
static bitstr_t *_pick_step_nodes_cpus(struct job_record *job_ptr,
				       bitstr_t *nodes_bitmap, int node_cnt,
				       int cpu_cnt, uint32_t *usable_cpu_cnt);
static hostlist_t _step_range_to_hostlist(struct step_record *step_ptr,
				uint32_t range_first, uint32_t range_last);
static int _step_hostname_to_inx(struct step_record *step_ptr,
				char *node_name);
static void _step_dealloc_lps(struct step_record *step_ptr);

/* Determine how many more CPUs are required for a job step */
static int  _opt_cpu_cnt(uint32_t step_min_cpus, bitstr_t *node_bitmap,
			 uint32_t *usable_cpu_cnt)
{
	int rem_cpus = step_min_cpus;
	int first_bit, last_bit, i;

	if (!node_bitmap)
		return rem_cpus;
	xassert(usable_cpu_cnt);
	first_bit = bit_ffs(node_bitmap);
	if (first_bit >= 0)
		last_bit = bit_fls(node_bitmap);
	else
		last_bit = first_bit - 1;
	for (i = first_bit; i <= last_bit; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		if (usable_cpu_cnt[i] >= rem_cpus)
			return 0;
		rem_cpus -= usable_cpu_cnt[i];
	}

	return rem_cpus;
}

/* Select the optimal node count for a job step based upon it's min and
 * max target, available resources, and nodes already picked */
static int _opt_node_cnt(uint32_t step_min_nodes, uint32_t step_max_nodes,
			 int nodes_avail, int nodes_picked_cnt)
{
	int target_node_cnt;

	if ((step_max_nodes > step_min_nodes) && (step_max_nodes != NO_VAL))
		target_node_cnt = step_max_nodes;
	else
		target_node_cnt = step_min_nodes;
	if (target_node_cnt > nodes_picked_cnt)
		target_node_cnt -= nodes_picked_cnt;
	else
		target_node_cnt = 0;
	if (nodes_avail < target_node_cnt)
		target_node_cnt = nodes_avail;

	return target_node_cnt;
}

/*
 * _create_step_record - create an empty step_record for the specified job.
 * IN job_ptr - pointer to job table entry to have step record added
 * RET a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
static struct step_record * _create_step_record(struct job_record *job_ptr)
{
	struct step_record *step_ptr;

	xassert(job_ptr);
	/* NOTE: Reserve highest step ID values for NO_VAL and
	 * SLURM_BATCH_SCRIPT */
	if (job_ptr->next_step_id >= 0xfffffff0) {
		/* avoid step records in the accounting database */
		info("job %u has reached step id limit", job_ptr->job_id);
		return NULL;
	}

	step_ptr = (struct step_record *) xmalloc(sizeof(struct step_record));

	last_job_update = time(NULL);
	step_ptr->job_ptr    = job_ptr;
	step_ptr->exit_code  = NO_VAL;
	step_ptr->time_limit = INFINITE;
	step_ptr->jobacct    = jobacctinfo_create(NULL);
	step_ptr->requid     = -1;
	step_ptr->start_protocol_ver = SLURM_PROTOCOL_VERSION;
	(void) list_append (job_ptr->step_list, step_ptr);

	return step_ptr;
}

/* The step with a state of PENDING is used as a placeholder for a host and
 * port that can be used to wake a pending srun as soon another step ends */
static void _build_pending_step(struct job_record *job_ptr,
				job_step_create_request_msg_t *step_specs)
{
	struct step_record *step_ptr;

	if ((step_specs->host == NULL) || (step_specs->port == 0))
		return;

	step_ptr = _create_step_record(job_ptr);
	if (step_ptr == NULL)
		return;

	step_ptr->port      = step_specs->port;
	step_ptr->host      = xstrdup(step_specs->host);
	step_ptr->state     = JOB_PENDING;
	step_ptr->cpu_count = step_specs->num_tasks;
	step_ptr->time_last_active = time(NULL);
	step_ptr->step_id   = INFINITE;
}

static void _internal_step_complete(struct job_record *job_ptr,
				    struct step_record *step_ptr)
{
	jobacct_storage_g_step_complete(acct_db_conn, step_ptr);
	job_ptr->derived_ec = MAX(job_ptr->derived_ec,
				  step_ptr->exit_code);

	/* This operations are needed for Cray systems and also provide a
	 * cleaner state for requeued jobs. */
	step_ptr->state = JOB_COMPLETING;
	select_g_step_finish(step_ptr);
#if !defined(HAVE_NATIVE_CRAY) && !defined(HAVE_CRAY_NETWORK)
	/* On native Cray, post_job_step is called after NHC completes.
	 * IF SIMULATING A CRAY THIS NEEDS TO BE COMMENTED OUT!!!! */
	post_job_step(step_ptr);
#endif
}

/*
 * delete_step_records - Delete step record for specified job_ptr.
 * This function is called when a step fails to run to completion. For example,
 * when the job is killed due to reaching its time limit or allocated nodes
 * go DOWN.
 * IN job_ptr - pointer to job table entry to have step records removed
 */
extern void delete_step_records (struct job_record *job_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);

	last_job_update = time(NULL);
	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		/* Only check if not a pending step */
		if (step_ptr->step_id != INFINITE) {
			uint16_t cleaning = 0;
			select_g_select_jobinfo_get(step_ptr->select_jobinfo,
						    SELECT_JOBDATA_CLEANING,
						    &cleaning);
			if (cleaning)	/* Step already in cleanup. */
				continue;
			/* _internal_step_complete() will purge step record */
			_internal_step_complete(job_ptr, step_ptr);
		} else {
			list_remove (step_iterator);
			_free_step_rec(step_ptr);
		}
	}
	list_iterator_destroy(step_iterator);
}

/*
 * step_list_purge - Simple purge of a job's step list records. No testing is
 *	performed to insure the step records has no active references.
 * IN job_ptr - pointer to job table entry to have step records removed
 */
extern void step_list_purge(struct job_record *job_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);
	if (job_ptr->step_list == NULL)
		return;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		list_remove (step_iterator);
		_free_step_rec(step_ptr);
	}
	list_iterator_destroy(step_iterator);
	list_destroy(job_ptr->step_list);
}

/* _free_step_rec - delete a step record's data structures */
static void _free_step_rec(struct step_record *step_ptr)
{
/* FIXME: If job step record is preserved after completion,
 * the switch_g_job_step_complete() must be called upon completion
 * and not upon record purging. Presently both events occur
 * simultaneously. */
	if (step_ptr->switch_job) {
		switch_g_job_step_complete(step_ptr->switch_job,
					   step_ptr->step_layout->node_list);
		switch_g_free_jobinfo (step_ptr->switch_job);
	}
	resv_port_free(step_ptr);
	checkpoint_free_jobinfo (step_ptr->check_job);

	xfree(step_ptr->host);
	xfree(step_ptr->name);
	slurm_step_layout_destroy(step_ptr->step_layout);
	jobacctinfo_destroy(step_ptr->jobacct);
	FREE_NULL_BITMAP(step_ptr->core_bitmap_job);
	FREE_NULL_BITMAP(step_ptr->exit_node_bitmap);
	FREE_NULL_BITMAP(step_ptr->step_node_bitmap);
	xfree(step_ptr->resv_port_array);
	xfree(step_ptr->resv_ports);
	xfree(step_ptr->network);
	xfree(step_ptr->ckpt_dir);
	xfree(step_ptr->gres);
	if (step_ptr->gres_list)
		list_destroy(step_ptr->gres_list);
	select_g_select_jobinfo_free(step_ptr->select_jobinfo);
	xfree(step_ptr->ext_sensors);
	step_ptr->job_ptr = NULL;
	xfree(step_ptr);
}

/*
 * delete_step_record - delete record for job step for specified job_ptr
 *	and step_id
 * IN job_ptr - pointer to job table entry to have step record removed
 * IN step_id - id of the desired job step
 * RET 0 on success, errno otherwise
 */
int
delete_step_record (struct job_record *job_ptr, uint32_t step_id)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	int error_code;
	uint16_t cleaning = 0;

	xassert(job_ptr);
	error_code = ENOENT;
	if (!job_ptr->step_list)
		return error_code;

	last_job_update = time(NULL);
	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->step_id != step_id)
			continue;

		error_code = 0;
		select_g_select_jobinfo_get(step_ptr->select_jobinfo,
					    SELECT_JOBDATA_CLEANING,
					    &cleaning);
		if (cleaning)	/* Step clean-up in progress. */
			break;
		list_remove(step_iterator);
		_free_step_rec(step_ptr);
		break;
	}
	list_iterator_destroy(step_iterator);

	return error_code;
}


/*
 * dump_step_desc - dump the incoming step initiate request message
 * IN step_spec - job step request specification from RPC
 */
void
dump_step_desc(job_step_create_request_msg_t *step_spec)
{
	uint32_t mem_value = step_spec->pn_min_memory;
	char *mem_type = "node";

	if (mem_value & MEM_PER_CPU) {
		mem_value &= (~MEM_PER_CPU);
		mem_type   = "cpu";
	}

	debug3("StepDesc: user_id=%u job_id=%u node_count=%u-%u cpu_count=%u",
	       step_spec->user_id, step_spec->job_id,
	       step_spec->min_nodes, step_spec->max_nodes,
	       step_spec->cpu_count);
	debug3("   cpu_freq=%u num_tasks=%u relative=%u task_dist=%u plane=%u",
	       step_spec->cpu_freq, step_spec->num_tasks, step_spec->relative,
	       step_spec->task_dist, step_spec->plane_size);
	debug3("   node_list=%s  constraints=%s",
	       step_spec->node_list, step_spec->features);
	debug3("   host=%s port=%u name=%s network=%s exclusive=%u",
	       step_spec->host, step_spec->port, step_spec->name,
	       step_spec->network, step_spec->exclusive);
	debug3("   checkpoint-dir=%s checkpoint_int=%u",
	       step_spec->ckpt_dir, step_spec->ckpt_interval);
	debug3("   mem_per_%s=%u resv_port_cnt=%u immediate=%u no_kill=%u",
	       mem_type, mem_value, step_spec->resv_port_cnt,
	       step_spec->immediate, step_spec->no_kill);
	debug3("   overcommit=%d time_limit=%u gres=%s",
	       step_spec->overcommit, step_spec->time_limit, step_spec->gres);
}


/*
 * find_step_record - return a pointer to the step record with the given
 *	job_id and step_id
 * IN job_ptr - pointer to job table entry to have step record added
 * IN step_id - id of the desired job step or NO_VAL for first one
 * RET pointer to the job step's record, NULL on error
 */
struct step_record *
find_step_record(struct job_record *job_ptr, uint32_t step_id)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;

	if (job_ptr == NULL)
		return NULL;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if ((step_ptr->step_id == step_id) || (step_id == NO_VAL))
			break;
	}
	list_iterator_destroy (step_iterator);

	return step_ptr;
}


/*
 * job_step_signal - signal the specified job step
 * IN job_id - id of the job to be cancelled
 * IN step_id - id of the job step to be cancelled
 * IN signal - user id of user issuing the RPC
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int job_step_signal(uint32_t job_id, uint32_t step_id,
		    uint16_t signal, uid_t uid)
{
	struct job_record *job_ptr;
	struct step_record *step_ptr, step_rec;
	int rc = SLURM_SUCCESS;
	static bool notify_slurmd = true;
	static int notify_srun = -1;
	static bool front_end = false;

	if (notify_srun == -1) {
		char *launch_type = slurm_get_launch_type();
		/* do this for all but slurm (poe, aprun, etc...) */
		if (strcmp(launch_type, "launch/slurm")) {
			notify_srun = 1;
			notify_slurmd = false;
		} else
			notify_srun = 0;
		xfree(launch_type);
	}

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		error("job_step_signal: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if (IS_JOB_FINISHED(job_ptr)) {
		rc = ESLURM_ALREADY_DONE;
		if (signal != SIG_NODE_FAIL)
			return rc;
	} else if (!IS_JOB_RUNNING(job_ptr)) {
		verbose("job_step_signal: step %u.%u can not be sent signal "
			"%u from state=%s", job_id, step_id, signal,
			job_state_string(job_ptr->job_state));
		if (signal != SIG_NODE_FAIL)
			return ESLURM_TRANSITION_STATE_NO_UPDATE;
	}

	if ((job_ptr->user_id != uid) && (uid != 0) && (uid != getuid())) {
		error("Security violation, JOB_CANCEL RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	step_ptr = find_step_record(job_ptr, step_id);
	if (step_ptr == NULL) {
		if (signal != SIG_NODE_FAIL) {
			info("job_step_signal step %u.%u not found",
			     job_id, step_id);
			return ESLURM_INVALID_JOB_ID;
		}
		if (job_ptr->node_bitmap == NULL) {
			/* Job state has already been cleared for requeue.
			 * This indicates that all nodes are already down.
			 * Rely upon real-time server to manage cnodes state */
			info("%s: job %u already requeued, can not down cnodes",
			     __func__, job_id);
			return ESLURM_ALREADY_DONE;
		}
		/* If we get a node fail signal, down the cnodes to avoid
		 * allocating them to another job. */
		debug("job_step_signal step %u.%u not found, but got "
		      "SIG_NODE_FAIL, so failing all nodes in allocation.",
		      job_id, step_id);
		memset(&step_rec, 0, sizeof(struct step_record));
		step_rec.step_id = NO_VAL;
		step_rec.job_ptr = job_ptr;
		step_rec.select_jobinfo = job_ptr->select_jobinfo;
		step_rec.step_node_bitmap = job_ptr->node_bitmap;
		step_ptr = &step_rec;
		rc = ESLURM_ALREADY_DONE;
	}

	/* If SIG_NODE_FAIL codes through it means we had nodes failed
	 * so handle that in the select plugin and switch the signal
	 * to KILL afterwards.
	 */
	if (signal == SIG_NODE_FAIL) {
		select_g_fail_cnode(step_ptr);
		signal = SIGKILL;
		if (rc != SLURM_SUCCESS)
			return rc;
	}
	if (notify_srun)
		srun_step_signal(step_ptr, signal);

	/* save user ID of the one who requested the job be cancelled */
	if (signal == SIGKILL) {
		step_ptr->requid = uid;
		srun_step_complete(step_ptr);
	}

#ifdef HAVE_FRONT_END
	front_end = true;
#endif
	/* Never signal tasks on a front_end system if we aren't
	 * suppose to notify the slurmd (i.e. BGQ and Cray) */
	if (front_end && !notify_slurmd) {
	} else if ((signal == SIGKILL) || notify_slurmd)
		signal_step_tasks(step_ptr, signal, REQUEST_SIGNAL_TASKS);

	return SLURM_SUCCESS;
}

/*
 * signal_step_tasks - send specific signal to specific job step
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 * IN msg_type - message type to send
 */
void signal_step_tasks(struct step_record *step_ptr, uint16_t signal,
		       slurm_msg_type_t msg_type)
{
#ifndef HAVE_FRONT_END
	int i;
#endif
	kill_tasks_msg_t *kill_tasks_msg;
	agent_arg_t *agent_args = NULL;

	xassert(step_ptr);
	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = msg_type;
	agent_args->retry    = 1;
	agent_args->hostlist = hostlist_create(NULL);
	kill_tasks_msg = xmalloc(sizeof(kill_tasks_msg_t));
	kill_tasks_msg->job_id      = step_ptr->job_ptr->job_id;
	kill_tasks_msg->job_step_id = step_ptr->step_id;
	kill_tasks_msg->signal      = signal;

#ifdef HAVE_FRONT_END
	xassert(step_ptr->job_ptr->batch_host);
	if (step_ptr->job_ptr->front_end_ptr)
		agent_args->protocol_version =
			step_ptr->job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, step_ptr->job_ptr->batch_host);
	agent_args->node_count = 1;
#else
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i) == 0)
			continue;
		if (agent_args->protocol_version >
		    node_record_table_ptr[i].protocol_version)
			agent_args->protocol_version =
				node_record_table_ptr[i].protocol_version;
		hostlist_push_host(agent_args->hostlist,
				   node_record_table_ptr[i].name);
		agent_args->node_count++;
	}
#endif

	if (agent_args->node_count == 0) {
		xfree(kill_tasks_msg);
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = kill_tasks_msg;
	agent_queue_request(agent_args);
	return;
}

/*
 * signal_step_tasks_on_node - send specific signal to specific job step
 *                             on a specific node.
 * IN node_name - name of node on which to signal tasks
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 * IN msg_type - message type to send
 */
void signal_step_tasks_on_node(char* node_name, struct step_record *step_ptr,
			       uint16_t signal, slurm_msg_type_t msg_type)
{
	kill_tasks_msg_t *kill_tasks_msg;
	agent_arg_t *agent_args = NULL;

	xassert(step_ptr);
	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = msg_type;
	agent_args->retry    = 1;
#ifdef HAVE_FRONT_END
	xassert(step_ptr->job_ptr->batch_host);
	agent_args->node_count++;
	if (step_ptr->job_ptr->front_end_ptr)
		agent_args->protocol_version =
			step_ptr->job_ptr->front_end_ptr->protocol_version;
	agent_args->hostlist = hostlist_create(step_ptr->job_ptr->batch_host);
	if (!agent_args->hostlist)
		fatal("Invalid batch_host: %s", step_ptr->job_ptr->batch_host);
#else
	struct node_record *node_ptr;
	if ((node_ptr = find_node_record(node_name)))
		agent_args->protocol_version = node_ptr->protocol_version;
	agent_args->node_count++;
	agent_args->hostlist = hostlist_create(node_name);
	if (!agent_args->hostlist)
		fatal("Invalid node_name: %s", node_name);
#endif
	kill_tasks_msg = xmalloc(sizeof(kill_tasks_msg_t));
	kill_tasks_msg->job_id      = step_ptr->job_ptr->job_id;
	kill_tasks_msg->job_step_id = step_ptr->step_id;
	kill_tasks_msg->signal      = signal;
	agent_args->msg_args = kill_tasks_msg;
	agent_queue_request(agent_args);
	return;
}

/* A step just completed, signal srun processes with pending steps to retry */
static void _wake_pending_steps(struct job_record *job_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	int start_count = 0;
	time_t max_age = time(NULL) - 60;   /* Wake step after 60 seconds */

	if (!IS_JOB_RUNNING(job_ptr))
		return;

	if (!job_ptr->step_list)
		return;

	/* We do not know which steps can use currently available resources.
	 * Try to start a bit more based upon step sizes. Effectiveness
	 * varies with step sizes, constraints and order. */
	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if ((step_ptr->state == JOB_PENDING) &&
		    ((start_count < 8) ||
		     (step_ptr->time_last_active <= max_age))) {
			srun_step_signal(step_ptr, 0);
			list_remove (step_iterator);
			/* Step never started, no need to check
			 * SELECT_JOBDATA_CLEANING. */
			_free_step_rec(step_ptr);
			start_count++;
		}
	}
	list_iterator_destroy (step_iterator);
}

/*
 * job_step_complete - note normal completion the specified job step
 * IN job_id - id of the job to be completed
 * IN step_id - id of the job step to be completed
 * IN uid - user id of user issuing the RPC
 * IN requeue - job should be run again if possible
 * IN job_return_code - job's return code, if set then set state to JOB_FAILED
 * RET 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
int job_step_complete(uint32_t job_id, uint32_t step_id, uid_t uid,
		      bool requeue, uint32_t job_return_code)
{
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	uint16_t cleaning = 0;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		info("job_step_complete: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && (uid != 0) && (uid != getuid())) {
		error("Security violation, JOB_COMPLETE RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	step_ptr = find_step_record(job_ptr, step_id);
	if (step_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;

	if (step_ptr->step_id == INFINITE)	/* batch step */
		return SLURM_SUCCESS;

	/* If the job is already cleaning we have already been here
	 * before, so just return. */
	select_g_select_jobinfo_get(step_ptr->select_jobinfo,
				    SELECT_JOBDATA_CLEANING,
				    &cleaning);
	if (cleaning) {	/* Step hasn't finished cleanup yet. */
		debug("%s: Cleaning flag already set for "
		      "job step %u.%u, no reason to cleanup again.",
		      __func__, step_ptr->step_id, step_ptr->job_ptr->job_id);
		return SLURM_SUCCESS;
	}

	_internal_step_complete(job_ptr, step_ptr);

	last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

/* Pick nodes to be allocated to a job step. If a CPU count is also specified,
 * then select nodes with a sufficient CPU count.
 * IN job_ptr - job to contain step allocation
 * IN/OUT node_bitmap - nodes available (IN), selectect for use (OUT)
 * IN node_cnt - step node count specification
 * IN cpu_cnt - step CPU count specification
 * IN usable_cpu_cnt - count of usable CPUs on each node in node_bitmap
 */
static bitstr_t *_pick_step_nodes_cpus(struct job_record *job_ptr,
				       bitstr_t *nodes_bitmap, int node_cnt,
				       int cpu_cnt, uint32_t *usable_cpu_cnt)
{
	bitstr_t *picked_node_bitmap = NULL;
	int *usable_cpu_array;
	int first_bit, last_bit;
	int cpu_target;	/* Target number of CPUs per allocated node */
	int rem_nodes, rem_cpus, save_rem_nodes, save_rem_cpus;
	int i;

	xassert(node_cnt > 0);
	xassert(nodes_bitmap);
	xassert(usable_cpu_cnt);
	cpu_target = (cpu_cnt + node_cnt - 1) / node_cnt;
	if (cpu_target > 1024)
		info("_pick_step_nodes_cpus: high cpu_target (%d)",cpu_target);
	if ((cpu_cnt <= node_cnt) || (cpu_target > 1024))
		return bit_pick_cnt(nodes_bitmap, node_cnt);

	/* Need to satisfy both a node count and a cpu count */
	picked_node_bitmap = bit_alloc(node_record_count);
	usable_cpu_array = xmalloc(sizeof(int) * cpu_target);
	rem_nodes = node_cnt;
	rem_cpus  = cpu_cnt;
	first_bit = bit_ffs(nodes_bitmap);
	if (first_bit >= 0)
		last_bit  = bit_fls(nodes_bitmap);
	else
		last_bit = first_bit - 1;
	for (i = first_bit; i <= last_bit; i++) {
		if (!bit_test(nodes_bitmap, i))
			continue;
		if (usable_cpu_cnt[i] < cpu_target) {
			usable_cpu_array[usable_cpu_cnt[i]]++;
			continue;
		}
		bit_set(picked_node_bitmap, i);
		rem_cpus -= usable_cpu_cnt[i];
		rem_nodes--;
		if ((rem_cpus <= 0) && (rem_nodes <= 0)) {
			/* Satisfied request */
			xfree(usable_cpu_array);
			return picked_node_bitmap;
		}
		if (rem_nodes == 0) {	/* Reached node limit, not CPU limit */
			xfree(usable_cpu_array);
			bit_free(picked_node_bitmap);
			return NULL;
		}
	}

	/* Need more resources. Determine what CPU counts per node to use */
	save_rem_nodes = rem_nodes;
	save_rem_cpus  = rem_cpus;
	usable_cpu_array[0] = 0;
	for (i = (cpu_target - 1); i > 0; i--) {
		if (usable_cpu_array[i] == 0)
			continue;
		if (usable_cpu_array[i] > rem_nodes)
			usable_cpu_array[i] = rem_nodes;
		if (rem_nodes > 0) {
			rem_nodes -= usable_cpu_array[i];
			rem_cpus  -= (usable_cpu_array[i] * i);
		}
	}
	if ((rem_cpus > 0) || (rem_nodes > 0)){	/* Can not satisfy request */
		xfree(usable_cpu_array);
		bit_free(picked_node_bitmap);
		return NULL;
	}
	rem_nodes = save_rem_nodes;
	rem_cpus  = save_rem_cpus;

	/* Pick nodes with CPU counts below original target */
	for (i = first_bit; i <= last_bit; i++) {
		if (!bit_test(nodes_bitmap, i))
			continue;
		if (usable_cpu_cnt[i] >= cpu_target)
			continue;	/* already picked */
		if (usable_cpu_array[usable_cpu_cnt[i]] == 0)
			continue;
		usable_cpu_array[usable_cpu_cnt[i]]--;
		bit_set(picked_node_bitmap, i);
		rem_cpus -= usable_cpu_cnt[i];
		rem_nodes--;
		if ((rem_cpus <= 0) && (rem_nodes <= 0)) {
			/* Satisfied request */
			xfree(usable_cpu_array);
			return picked_node_bitmap;
		}
		if (rem_nodes == 0)	/* Reached node limit */
			break;
	}

	/* Can not satisfy request */
	xfree(usable_cpu_array);
	bit_free(picked_node_bitmap);
	return NULL;
}

/*
 * _pick_step_nodes - select nodes for a job step that satisfy its requirements
 *	we satisfy the super-set of constraints.
 * IN job_ptr - pointer to job to have new step started
 * IN step_spec - job step specification
 * IN step_gres_list - job step's gres requirement details
 * IN cpus_per_task - NOTE could be zero
 * IN node_count - How many real nodes a select plugin should be looking for
 * OUT return_code - exit code or SLURM_SUCCESS
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: returns all of a job's nodes if step_spec->node_count == INFINITE
 * NOTE: returned bitmap must be freed by the caller using FREE_NULL_BITMAP()
 */
static bitstr_t *
_pick_step_nodes (struct job_record  *job_ptr,
		  job_step_create_request_msg_t *step_spec,
		  List step_gres_list, int cpus_per_task, uint32_t node_count,
		  dynamic_plugin_data_t *select_jobinfo, int *return_code)
{
	int node_inx, first_bit, last_bit;
	struct node_record *node_ptr;
	bitstr_t *nodes_avail = NULL, *nodes_idle = NULL,
		*select_nodes_avail = NULL;
	bitstr_t *nodes_picked = NULL, *node_tmp = NULL;
	int error_code, nodes_picked_cnt = 0, cpus_picked_cnt = 0;
	int cpu_cnt, i, task_cnt;
	int mem_blocked_nodes = 0, mem_blocked_cpus = 0;
	ListIterator step_iterator;
	struct step_record *step_p;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	uint32_t *usable_cpu_cnt = NULL;

	xassert(job_resrcs_ptr);
	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	*return_code = SLURM_SUCCESS;
	if (job_ptr->node_bitmap == NULL) {
		*return_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		return NULL;
	}

	if (step_spec->max_nodes == 0)
		step_spec->max_nodes = job_ptr->node_cnt;

	if (step_spec->max_nodes < step_spec->min_nodes) {
		*return_code = ESLURM_INVALID_NODE_COUNT;
		return NULL;
	}

	/* If we have a select plugin that figures this out for us
	 * just return.  Else just do the normal operations.
	 */
	if ((nodes_picked = select_g_step_pick_nodes(
		     job_ptr, select_jobinfo, node_count, &select_nodes_avail)))
		return nodes_picked;
#ifdef HAVE_BGQ
	*return_code = ESLURM_NODES_BUSY;
	return NULL;
#endif
	if (!nodes_avail)
		nodes_avail = bit_copy (job_ptr->node_bitmap);
	bit_and (nodes_avail, up_node_bitmap);
	if (step_spec->features) {
		/* We only select for a single feature name here.
		 * Add support for AND, OR, etc. here if desired */
		struct features_record *feat_ptr;
		feat_ptr = list_find_first(feature_list, list_find_feature,
					   (void *) step_spec->features);
		if (feat_ptr && feat_ptr->node_bitmap)
			bit_and(nodes_avail, feat_ptr->node_bitmap);
		else
			bit_nclear(nodes_avail, 0, (bit_size(nodes_avail)-1));
	}

	if (step_spec->pn_min_memory &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("_pick_step_nodes: job lacks memory allocation details "
		      "to enforce memory limits for job %u", job_ptr->job_id);
		step_spec->pn_min_memory = 0;
	} else if (step_spec->pn_min_memory == MEM_PER_CPU)
		step_spec->pn_min_memory = 0;	/* clear MEM_PER_CPU flag */

	if (job_ptr->next_step_id == 0) {
		if (job_ptr->details && job_ptr->details->prolog_running) {
			*return_code = ESLURM_PROLOG_RUNNING;
			FREE_NULL_BITMAP(nodes_avail);
			FREE_NULL_BITMAP(select_nodes_avail);
			return NULL;
		}
		for (i=bit_ffs(job_ptr->node_bitmap); i<node_record_count;
		     i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (IS_NODE_POWER_SAVE(node_ptr) ||
			    IS_NODE_FUTURE(node_ptr) ||
			    IS_NODE_NO_RESPOND(node_ptr)) {
				/* Node is/was powered down. Need to wait
				 * for it to start responding again. */
				FREE_NULL_BITMAP(nodes_avail);
				FREE_NULL_BITMAP(select_nodes_avail);
				*return_code = ESLURM_NODES_BUSY;
				/* Update job's end-time to allow for node
				 * boot time. */
				if ((job_ptr->time_limit != INFINITE) &&
				    (!job_ptr->preempt_time)) {
					job_ptr->end_time = time(NULL) +
						(job_ptr->time_limit * 60);
				}
				return NULL;
			}
		}
		job_ptr->job_state &= (~JOB_CONFIGURING);
		debug("Configuration for job %u complete", job_ptr->job_id);
	}

	/* In exclusive mode, just satisfy the processor count.
	 * Do not use nodes that have no unused CPUs or insufficient
	 * unused memory */
	if (step_spec->exclusive) {
		int avail_cpus, avail_tasks, total_cpus, total_tasks, node_inx;
		int i_first, i_last;
		uint32_t avail_mem, total_mem, gres_cnt;
		uint32_t nodes_picked_cnt = 0;
		uint32_t tasks_picked_cnt = 0, total_task_cnt = 0;
		bitstr_t *selected_nodes = NULL, *non_selected_nodes = NULL;
		int *non_selected_tasks = NULL;

		if (step_spec->node_list) {
			error_code = node_name2bitmap(step_spec->node_list,
						      false,
						      &selected_nodes);
			if (error_code) {
				info("_pick_step_nodes: invalid node list (%s) "
				     "for job step %u",
				     step_spec->node_list, job_ptr->job_id);
				FREE_NULL_BITMAP(selected_nodes);
				goto cleanup;
			}
			if (!bit_super_set(selected_nodes,
					   job_ptr->node_bitmap)) {
				info("_pick_step_nodes: selected nodes (%s) "
				     "not in job %u",
				     step_spec->node_list, job_ptr->job_id);
				FREE_NULL_BITMAP(selected_nodes);
				goto cleanup;
			}
			if (!bit_super_set(selected_nodes, up_node_bitmap)) {
				info("_pick_step_nodes: selected nodes (%s) "
				     "are DOWN",
				     step_spec->node_list);
				FREE_NULL_BITMAP(selected_nodes);
				goto cleanup;
			}
			non_selected_nodes = bit_alloc(node_record_count);
			non_selected_tasks = xmalloc(sizeof(int) *
						     node_record_count);
		}

		node_inx = -1;
		i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
		i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_resrcs_ptr->node_bitmap, i))
				continue;
			node_inx++;
			if (!bit_test(nodes_avail, i))
				continue;	/* node now DOWN */
			avail_cpus = job_resrcs_ptr->cpus[node_inx] -
				     job_resrcs_ptr->cpus_used[node_inx];
			total_cpus = job_resrcs_ptr->cpus[node_inx];
			if (cpus_per_task > 0) {
				avail_tasks = avail_cpus / cpus_per_task;
				total_tasks = total_cpus / cpus_per_task;
			} else {
				avail_tasks = step_spec->num_tasks;
				total_tasks = step_spec->num_tasks;
			}
			if (_is_mem_resv() &&
			    (step_spec->pn_min_memory & MEM_PER_CPU)) {
				uint32_t mem_use = step_spec->pn_min_memory;
				mem_use &= (~MEM_PER_CPU);

				avail_mem = job_resrcs_ptr->
					memory_allocated[node_inx] -
					job_resrcs_ptr->memory_used[node_inx];
				task_cnt = avail_mem / mem_use;
				if (cpus_per_task > 0)
					task_cnt /= cpus_per_task;
				avail_tasks = MIN(avail_tasks, task_cnt);

				total_mem = job_resrcs_ptr->
					    memory_allocated[node_inx];
				task_cnt = total_mem / mem_use;
				if (cpus_per_task > 0)
					task_cnt /= cpus_per_task;
				total_tasks = MIN(total_tasks, task_cnt);
			} else if (_is_mem_resv() && step_spec->pn_min_memory) {
				uint32_t mem_use = step_spec->pn_min_memory;

				avail_mem = job_resrcs_ptr->
					memory_allocated[node_inx] -
					job_resrcs_ptr->memory_used[node_inx];
				if (avail_mem < mem_use)
					avail_tasks = 0;

				total_mem = job_resrcs_ptr->
					    memory_allocated[node_inx];
				if (total_mem < mem_use)
					total_tasks = 0;
			}

			gres_cnt = gres_plugin_step_test(step_gres_list,
							 job_ptr->gres_list,
							 node_inx, false,
							 job_ptr->job_id,
							 NO_VAL);
			if ((gres_cnt != NO_VAL) && (cpus_per_task > 0))
				gres_cnt /= cpus_per_task;
			avail_tasks = MIN(avail_tasks, gres_cnt);
			gres_cnt = gres_plugin_step_test(step_gres_list,
							 job_ptr->gres_list,
							 node_inx, true,
							 job_ptr->job_id,
							 NO_VAL);
			if ((gres_cnt != NO_VAL) && (cpus_per_task > 0))
				gres_cnt /= cpus_per_task;
			total_tasks = MIN(total_tasks, gres_cnt);
			if (step_spec->plane_size != (uint16_t) NO_VAL) {
				if (avail_tasks < step_spec->plane_size)
					avail_tasks = 0;
				else {
					/* Round count down */
					avail_tasks /= step_spec->plane_size;
					avail_tasks *= step_spec->plane_size;
				}
				if (total_tasks < step_spec->plane_size)
					total_tasks = 0;
				else {
					/* Round count down */
					total_tasks /= step_spec->plane_size;
					total_tasks *= step_spec->plane_size;
				}
			}

			if (nodes_picked_cnt >= step_spec->max_nodes)
				bit_clear(nodes_avail, i);
			else if ((avail_tasks <= 0) ||
				 ((selected_nodes == NULL) &&
				  (nodes_picked_cnt >= step_spec->min_nodes) &&
				  (tasks_picked_cnt > 0)   &&
				  (tasks_picked_cnt >= step_spec->num_tasks))) {
				bit_clear(nodes_avail, i);
				total_task_cnt += total_tasks;
			} else if (selected_nodes &&
				   !bit_test(selected_nodes, i)) {
				/* Usable, but not selected node */
				bit_clear(nodes_avail, i);
				bit_set(non_selected_nodes, i);
				non_selected_tasks[i] = avail_tasks;
			} else if (select_nodes_avail &&
				   !bit_test(select_nodes_avail, i)) {
				/* Select does not want you to use this */
				bit_clear(nodes_avail, i);
			} else {
				nodes_picked_cnt++;
				tasks_picked_cnt += avail_tasks;
				total_task_cnt += total_tasks;
			}
		}

		if (selected_nodes) {
			if (!bit_super_set(selected_nodes, nodes_avail)) {
				/* some required nodes have no available
				 * processors, defer request */
				i_last = -1;
				tasks_picked_cnt = 0;
			}
			FREE_NULL_BITMAP(selected_nodes);
			/* Add resources for non-selected nodes as needed */
			for (i = i_first; i <= i_last; i++) {
				if ((nodes_picked_cnt >= step_spec->min_nodes)&&
				    (tasks_picked_cnt >= step_spec->num_tasks))
					break;
				if (!bit_test(non_selected_nodes, i))
					continue;
				bit_set(nodes_avail, i);
				nodes_picked_cnt++;
				tasks_picked_cnt += non_selected_tasks[i];
			}
			FREE_NULL_BITMAP(non_selected_nodes);
			xfree(non_selected_tasks);
		}

		if (select_nodes_avail) {
			/* The select plugin told us these were the
			 * only ones we could choose from.  If it
			 * doesn't fit here then defer request */
			if (!bit_super_set(nodes_avail, select_nodes_avail)) {
				tasks_picked_cnt = 0;
			}
			FREE_NULL_BITMAP(selected_nodes);
		}

		if (tasks_picked_cnt >= step_spec->num_tasks)
			return nodes_avail;
		FREE_NULL_BITMAP(nodes_avail);

		if (total_task_cnt >= step_spec->num_tasks)
			*return_code = ESLURM_NODES_BUSY;
		else
			*return_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		return NULL;
	}

	if ((step_spec->pn_min_memory && _is_mem_resv()) ||
	    (step_spec->gres && (step_spec->gres[0]))) {
		int fail_mode = ESLURM_INVALID_TASK_MEMORY;
		uint32_t tmp_mem, tmp_cpus, avail_cpus, total_cpus;
		uint32_t avail_tasks, total_tasks;

		usable_cpu_cnt = xmalloc(sizeof(uint32_t) * node_record_count);
		first_bit = bit_ffs(job_resrcs_ptr->node_bitmap);
		last_bit  = bit_fls(job_resrcs_ptr->node_bitmap);
		for (i=first_bit, node_inx=-1; i<=last_bit; i++) {
			if (!bit_test(job_resrcs_ptr->node_bitmap, i))
				continue;
			node_inx++;
			if (!bit_test(nodes_avail, i))
				continue;	/* node now DOWN */

			total_cpus = job_resrcs_ptr->cpus[node_inx];
			usable_cpu_cnt[i] = avail_cpus = total_cpus;
			if (_is_mem_resv() &&
			    step_spec->pn_min_memory & MEM_PER_CPU) {
				uint32_t mem_use = step_spec->pn_min_memory;
				mem_use &= (~MEM_PER_CPU);
				/* ignore current step allocations */
				tmp_mem    = job_resrcs_ptr->
					     memory_allocated[node_inx];
				tmp_cpus   = tmp_mem / mem_use;
				total_cpus = MIN(total_cpus, tmp_cpus);
				/* consider current step allocations */
				tmp_mem   -= job_resrcs_ptr->
					     memory_used[node_inx];
				tmp_cpus   = tmp_mem / mem_use;
				if (tmp_cpus < avail_cpus) {
					avail_cpus = tmp_cpus;
					usable_cpu_cnt[i] = avail_cpus;
					fail_mode = ESLURM_INVALID_TASK_MEMORY;
				}
			} else if (_is_mem_resv() && step_spec->pn_min_memory) {
				uint32_t mem_use = step_spec->pn_min_memory;
				/* ignore current step allocations */
				tmp_mem    = job_resrcs_ptr->
					     memory_allocated[node_inx];
				if (tmp_mem < mem_use)
					total_cpus = 0;
				/* consider current step allocations */
				tmp_mem   -= job_resrcs_ptr->
					     memory_used[node_inx];
				if ((tmp_mem < mem_use) && (avail_cpus > 0)) {
					avail_cpus = 0;
					usable_cpu_cnt[i] = avail_cpus;
					fail_mode = ESLURM_INVALID_TASK_MEMORY;
				}
			}

			if (step_spec->gres) {
				/* ignore current step allocations */
				tmp_cpus = gres_plugin_step_test(step_gres_list,
							job_ptr->gres_list,
							node_inx, true,
							job_ptr->job_id,
							NO_VAL);
				total_cpus = MIN(total_cpus, tmp_cpus);
				/* consider current step allocations */
				tmp_cpus = gres_plugin_step_test(step_gres_list,
							job_ptr->gres_list,
							node_inx, false,
							job_ptr->job_id,
							NO_VAL);
				if (tmp_cpus < avail_cpus) {
					avail_cpus = tmp_cpus;
					usable_cpu_cnt[i] = avail_cpus;
					fail_mode = ESLURM_INVALID_GRES;
				}
			}

			avail_tasks = avail_cpus;
			total_tasks = total_cpus;
			if (cpus_per_task > 0) {
				avail_tasks /= cpus_per_task;
				total_tasks /= cpus_per_task;
			}
			if (avail_tasks == 0) {
				if (step_spec->min_nodes == INFINITE) {
					FREE_NULL_BITMAP(nodes_avail);
					FREE_NULL_BITMAP(select_nodes_avail);
					xfree(usable_cpu_cnt);
					*return_code = ESLURM_NODES_BUSY;
					if (total_tasks == 0)
						*return_code = fail_mode;
					return NULL;
				}
				bit_clear(nodes_avail, i);
				mem_blocked_nodes++;
				mem_blocked_cpus += (total_cpus - avail_cpus);
			} else {
				mem_blocked_cpus += (total_cpus - avail_cpus);
			}
		}
	}

	if (step_spec->min_nodes == INFINITE) {	/* use all nodes */
		xfree(usable_cpu_cnt);
		FREE_NULL_BITMAP(select_nodes_avail);
		return nodes_avail;
	}

	if (select_nodes_avail) {
		/* The select plugin told us these were the
		 * only ones we could choose from.  If it
		 * doesn't fit here then defer request */
		bit_and(nodes_avail, select_nodes_avail);
		FREE_NULL_BITMAP(select_nodes_avail);
	}

	if (step_spec->node_list) {
		bitstr_t *selected_nodes = NULL;
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS)
			info("selected nodelist is %s", step_spec->node_list);

		error_code = node_name2bitmap(step_spec->node_list, false,
					      &selected_nodes);
		if (error_code) {
			info("_pick_step_nodes: invalid node list %s",
				step_spec->node_list);
			FREE_NULL_BITMAP(selected_nodes);
			goto cleanup;
		}
		if (!bit_super_set(selected_nodes, job_ptr->node_bitmap)) {
			info ("_pick_step_nodes: requested nodes %s not part "
				"of job %u",
				step_spec->node_list, job_ptr->job_id);
			FREE_NULL_BITMAP(selected_nodes);
			goto cleanup;
		}
		if (!bit_super_set(selected_nodes, nodes_avail)) {
			/*
			 * If some nodes still have some memory allocated
			 * to other steps, just defer the execution of the
			 * step
			 */
			if (mem_blocked_nodes == 0) {
				*return_code = ESLURM_INVALID_TASK_MEMORY;
				info ("_pick_step_nodes: requested nodes %s "
				      "have inadequate memory",
				      step_spec->node_list);
			} else {
				*return_code = ESLURM_NODES_BUSY;
				info ("_pick_step_nodes: some requested nodes"
				      " %s still have memory used by other steps",
				      step_spec->node_list);
			}
			FREE_NULL_BITMAP(selected_nodes);
			goto cleanup;
		}
		if (step_spec->task_dist == SLURM_DIST_ARBITRARY) {
			/* if we are in arbitrary mode we need to make
			 * sure we aren't running on an elan switch.
			 * If we aren't change the number of nodes
			 * available to the number we were given since
			 * that is what the user wants to run on.
			 */
			if (!strcmp(slurmctld_conf.switch_type,
				    "switch/elan")) {
				info("Can't do an ARBITRARY task layout with "
				     "switch type elan. Switching DIST type "
				     "to BLOCK");
				xfree(step_spec->node_list);
				step_spec->task_dist = SLURM_DIST_BLOCK;
				FREE_NULL_BITMAP(selected_nodes);
				step_spec->min_nodes =
					bit_set_count(nodes_avail);
			} else {
				step_spec->min_nodes =
					bit_set_count(selected_nodes);
			}
		}
		if (selected_nodes) {
			int node_cnt = 0;
			/* use selected nodes to run the job and
			 * make them unavailable for future use */

			/* If we have selected more than we requested
			 * make the available nodes equal to the
			 * selected nodes and we will pick from that
			 * list later on in the function.
			 * Other than that copy the nodes selected as
			 * the nodes we want.
			 */
			node_cnt = bit_set_count(selected_nodes);
			if (node_cnt > step_spec->max_nodes) {
				info("_pick_step_nodes: requested nodes %s "
				     "exceed max node count for job step %u",
				     step_spec->node_list, job_ptr->job_id);
				FREE_NULL_BITMAP(selected_nodes);
				goto cleanup;
			} else if (step_spec->min_nodes &&
				   (node_cnt > step_spec->min_nodes)) {
				nodes_picked = bit_alloc(bit_size(nodes_avail));
				FREE_NULL_BITMAP(nodes_avail);
				nodes_avail = selected_nodes;
				selected_nodes = NULL;
			} else {
				nodes_picked = bit_copy(selected_nodes);
				bit_not(selected_nodes);
				bit_and(nodes_avail, selected_nodes);
				FREE_NULL_BITMAP(selected_nodes);
			}
		}
	} else {
		nodes_picked = bit_alloc(bit_size(nodes_avail));
	}

	/* In case we are in relative mode, do not look for idle nodes
	 * as we will not try to get idle nodes first but try to get
	 * the relative node first */
	if (step_spec->relative != (uint16_t)NO_VAL) {
		/* Remove first (step_spec->relative) nodes from
		 * available list */
		bitstr_t *relative_nodes = NULL;
		relative_nodes = bit_pick_cnt(job_ptr->node_bitmap,
					      step_spec->relative);
		if (relative_nodes == NULL) {
			info ("_pick_step_nodes: "
			      "Invalid relative value (%u) for job %u",
			      step_spec->relative, job_ptr->job_id);
			goto cleanup;
		}
		bit_not (relative_nodes);
		bit_and (nodes_avail, relative_nodes);
		FREE_NULL_BITMAP (relative_nodes);
	} else {
		nodes_idle = bit_alloc (bit_size (nodes_avail) );
		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_p = (struct step_record *)
			list_next(step_iterator))) {
			if (step_p->state < JOB_RUNNING)
				continue;
			bit_or(nodes_idle, step_p->step_node_bitmap);
			if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
				char *temp;
				temp = bitmap2node_name(step_p->
							step_node_bitmap);
				info("step %u.%u has nodes %s",
				     job_ptr->job_id, step_p->step_id, temp);
				xfree(temp);
			}
		}
		list_iterator_destroy (step_iterator);
		bit_not(nodes_idle);
		bit_and(nodes_idle, nodes_avail);
	}

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
		char *temp1, *temp2, *temp3;
		temp1 = bitmap2node_name(nodes_avail);
		temp2 = bitmap2node_name(nodes_idle);
		if (step_spec->node_list)
			temp3 = step_spec->node_list;
		else
			temp3 = "NONE";
		info("step pick %u-%u nodes, avail:%s idle:%s picked:%s",
		     step_spec->min_nodes, step_spec->max_nodes, temp1, temp2,
		     temp3);
		xfree(temp1);
		xfree(temp2);
	}

	/* if user specifies step needs a specific processor count and
	 * all nodes have the same processor count, just translate this to
	 * a node count */
	if (step_spec->cpu_count && job_ptr->job_resrcs &&
	    (job_ptr->job_resrcs->cpu_array_cnt == 1) &&
	    (job_ptr->job_resrcs->cpu_array_value)) {
		i = (step_spec->cpu_count +
		     (job_ptr->job_resrcs->cpu_array_value[0] - 1)) /
		    job_ptr->job_resrcs->cpu_array_value[0];
		step_spec->min_nodes = (i > step_spec->min_nodes) ?
					i : step_spec->min_nodes ;
		if (step_spec->max_nodes < step_spec->min_nodes) {
			info("Job step %u max node count incompatible with CPU "
			     "count", job_ptr->job_id);
			*return_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
			goto cleanup;
		}
	}

	if (step_spec->min_nodes) {
		int cpus_needed, node_avail_cnt, nodes_needed;

		if (usable_cpu_cnt == NULL) {
			usable_cpu_cnt = xmalloc(sizeof(uint32_t) *
						 node_record_count);
			first_bit = bit_ffs(job_resrcs_ptr->node_bitmap);
			last_bit  = bit_fls(job_resrcs_ptr->node_bitmap);
			for (i=first_bit, node_inx=-1; i<=last_bit; i++) {
				if (!bit_test(job_resrcs_ptr->node_bitmap, i))
					continue;
				node_inx++;
				usable_cpu_cnt[i] = job_resrcs_ptr->
						    cpus[node_inx];
			}

		}
		nodes_picked_cnt = bit_set_count(nodes_picked);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
			verbose("step picked %d of %u nodes",
				nodes_picked_cnt, step_spec->min_nodes);
		}
		if (nodes_idle)
			node_avail_cnt = bit_set_count(nodes_idle);
		else
			node_avail_cnt = 0;
		nodes_needed = step_spec->min_nodes - nodes_picked_cnt;
		if ((nodes_needed > 0) &&
		    (node_avail_cnt >= nodes_needed)) {
			cpus_needed = _opt_cpu_cnt(step_spec->cpu_count,
						   nodes_picked,
						   usable_cpu_cnt);
			nodes_needed = _opt_node_cnt(step_spec->min_nodes,
						     step_spec->max_nodes,
						     node_avail_cnt,
						     nodes_picked_cnt);
			node_tmp = _pick_step_nodes_cpus(job_ptr, nodes_idle,
							 nodes_needed,
							 cpus_needed,
							 usable_cpu_cnt);
			if (node_tmp) {
				bit_or  (nodes_picked, node_tmp);
				bit_not (node_tmp);
				bit_and (nodes_idle, node_tmp);
				bit_and (nodes_avail, node_tmp);
				FREE_NULL_BITMAP (node_tmp);
				node_tmp = NULL;
				nodes_picked_cnt = step_spec->min_nodes;
				nodes_needed = 0;
			}
		}
		if (nodes_avail)
			node_avail_cnt = bit_set_count(nodes_avail);
		else
			node_avail_cnt = 0;
		if ((nodes_needed > 0) &&
		    (node_avail_cnt >= nodes_needed)) {
			cpus_needed = _opt_cpu_cnt(step_spec->cpu_count,
						   nodes_picked,
						   usable_cpu_cnt);
			nodes_needed = _opt_node_cnt(step_spec->min_nodes,
						     step_spec->max_nodes,
						     node_avail_cnt,
						     nodes_picked_cnt);
			node_tmp = _pick_step_nodes_cpus(job_ptr, nodes_avail,
							 nodes_needed,
							 cpus_needed,
							 usable_cpu_cnt);
			if (node_tmp == NULL) {
				/* Count of nodes already picked for step */
				int pick_node_cnt = bit_set_count(nodes_avail);
				pick_node_cnt += nodes_picked_cnt;
				if ((step_spec->max_nodes <= pick_node_cnt) &&
				    (mem_blocked_cpus == 0)) {
					*return_code =
						ESLURM_TOO_MANY_REQUESTED_CPUS;
				} else if ((mem_blocked_cpus > 0) ||
					   (step_spec->min_nodes <=
					    (pick_node_cnt+mem_blocked_nodes))){
					*return_code = ESLURM_NODES_BUSY;
				} else if (!bit_super_set(job_ptr->node_bitmap,
							  up_node_bitmap)) {
					*return_code = ESLURM_NODE_NOT_AVAIL;
				}
				goto cleanup;
			}
			bit_or  (nodes_picked, node_tmp);
			bit_not (node_tmp);
			bit_and (nodes_avail, node_tmp);
			FREE_NULL_BITMAP (node_tmp);
			node_tmp = NULL;
			nodes_picked_cnt = step_spec->min_nodes;
		} else if (nodes_needed > 0) {
			if ((step_spec->max_nodes <= nodes_picked_cnt) &&
			    (mem_blocked_cpus == 0)) {
				*return_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
			} else if ((mem_blocked_cpus > 0) ||
				   (step_spec->min_nodes <=
				    (nodes_picked_cnt + mem_blocked_nodes))) {
				*return_code = ESLURM_NODES_BUSY;
			} else if (!bit_super_set(job_ptr->node_bitmap,
						  up_node_bitmap)) {
				*return_code = ESLURM_NODE_NOT_AVAIL;
			}
			goto cleanup;
		}
	}
	if (step_spec->cpu_count) {
		/* make sure the selected nodes have enough cpus */
		cpus_picked_cnt = _count_cpus(job_ptr, nodes_picked,
					      usable_cpu_cnt);
		if ((step_spec->cpu_count > cpus_picked_cnt) &&
		    (step_spec->max_nodes > nodes_picked_cnt)) {
			/* Attempt to add more nodes to allocation */
			nodes_picked_cnt = bit_set_count(nodes_picked);
			while (step_spec->cpu_count > cpus_picked_cnt) {
				node_tmp = bit_pick_cnt(nodes_avail, 1);
				if (node_tmp == NULL)
					break;

				cpu_cnt = _count_cpus(job_ptr, node_tmp,
						      usable_cpu_cnt);
				if (cpu_cnt == 0) {
					/* Node not usable (memory insufficient
					 * to allocate any CPUs, etc.) */
					bit_not(node_tmp);
					bit_and(nodes_avail, node_tmp);
					FREE_NULL_BITMAP(node_tmp);
					continue;
				}

				bit_or  (nodes_picked, node_tmp);
				bit_not (node_tmp);
				bit_and (nodes_avail, node_tmp);
				FREE_NULL_BITMAP (node_tmp);
				node_tmp = NULL;
				nodes_picked_cnt += 1;
				if (step_spec->min_nodes)
					step_spec->min_nodes = nodes_picked_cnt;

				cpus_picked_cnt += cpu_cnt;
				if (nodes_picked_cnt >= step_spec->max_nodes)
					break;
			}
		}

		/* user is requesting more cpus than we got from the
		 * picked nodes we should return with an error */
		if (step_spec->cpu_count > cpus_picked_cnt) {
			if (step_spec->cpu_count &&
			    (step_spec->cpu_count <=
			     (cpus_picked_cnt + mem_blocked_cpus))) {
				*return_code = ESLURM_NODES_BUSY;
			} else if (!bit_super_set(job_ptr->node_bitmap,
						  up_node_bitmap)) {
				*return_code = ESLURM_NODE_NOT_AVAIL;
			}
			debug2("Have %d nodes with %d cpus which is less "
			       "than what the user is asking for (%d cpus) "
			       "aborting.",
			       nodes_picked_cnt, cpus_picked_cnt,
			       step_spec->cpu_count);
			goto cleanup;
		}
	}

	FREE_NULL_BITMAP(nodes_avail);
	FREE_NULL_BITMAP(select_nodes_avail);
	FREE_NULL_BITMAP(nodes_idle);
	xfree(usable_cpu_cnt);
	return nodes_picked;

cleanup:
	FREE_NULL_BITMAP(nodes_avail);
	FREE_NULL_BITMAP(select_nodes_avail);
	FREE_NULL_BITMAP(nodes_idle);
	FREE_NULL_BITMAP(nodes_picked);
	xfree(usable_cpu_cnt);
	if (*return_code == SLURM_SUCCESS) {
		*return_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	} else if (*return_code == ESLURM_NODE_NOT_AVAIL) {
		/* Return ESLURM_NODES_BUSY if the node is not responding.
		 * The node will eventually either come back UP or go DOWN. */
		nodes_picked = bit_copy(up_node_bitmap);
		bit_not(nodes_picked);
		bit_and(nodes_picked, job_ptr->node_bitmap);
		first_bit = bit_ffs(nodes_picked);
		if (first_bit == -1)
			last_bit = -2;
		else
			last_bit = bit_fls(nodes_picked);
		for (i = first_bit; i <= last_bit; i++) {
			if (!bit_test(nodes_picked, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (!IS_NODE_NO_RESPOND(node_ptr)) {
				*return_code = ESLURM_NODES_BUSY;
				break;
			}
		}
		FREE_NULL_BITMAP(nodes_picked);
	}
	return NULL;
}

/*
 * _count_cpus - report how many cpus are allocated to this job for the
 *		 identified nodes
 * IN job_ptr - point to job
 * IN bitmap - map of nodes to tally
 * IN usable_cpu_cnt - count of usable CPUs based upon memory or gres specs
 *		NULL if not available
 * RET cpu count
 */
static int _count_cpus(struct job_record *job_ptr, bitstr_t *bitmap,
		       uint32_t *usable_cpu_cnt)
{
	int i, sum = 0;
	struct node_record *node_ptr;

	if (job_ptr->job_resrcs && job_ptr->job_resrcs->cpus &&
	    job_ptr->job_resrcs->node_bitmap) {
		int node_inx = -1;
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (!bit_test(job_ptr->job_resrcs->node_bitmap, i))
				continue;
			node_inx++;
			if (!bit_test(job_ptr->node_bitmap, i) ||
			    !bit_test(bitmap, i)) {
				/* absent from current job or step bitmap */
				continue;
			}
			if (usable_cpu_cnt)
				sum += usable_cpu_cnt[i];
			else
				sum += job_ptr->job_resrcs->cpus[node_inx];
		}
	} else {
		error("job %u lacks cpus array", job_ptr->job_id);
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (!bit_test(bitmap, i))
				continue;
			if (slurmctld_conf.fast_schedule)
				sum += node_ptr->config_ptr->cpus;
			else
				sum += node_ptr->cpus;
		}
	}

	return sum;
}

/* Update the step's core bitmaps, create as needed.
 *	Add the specified task count for a specific node in the job's
 *	and step's allocation */
static void _pick_step_cores(struct step_record *step_ptr,
			     job_resources_t *job_resrcs_ptr,
			     int job_node_inx, uint16_t task_cnt)
{
	int bit_offset, core_inx, i, sock_inx;
	uint16_t sockets, cores;
	int cpu_cnt = (int) task_cnt;
	bool use_all_cores;
	static int last_core_inx;

	if (!step_ptr->core_bitmap_job)
		step_ptr->core_bitmap_job =
			bit_alloc(bit_size(job_resrcs_ptr->core_bitmap));

	if (get_job_resources_cnt(job_resrcs_ptr, job_node_inx,
				  &sockets, &cores))
		fatal("get_job_resources_cnt");

	if (task_cnt == (cores * sockets))
		use_all_cores = true;
	else
		use_all_cores = false;
	if (step_ptr->cpus_per_task > 0)
		cpu_cnt *= step_ptr->cpus_per_task;

	/* select idle cores first */
	for (core_inx=0; core_inx<cores; core_inx++) {
		for (sock_inx=0; sock_inx<sockets; sock_inx++) {
			bit_offset = get_job_resources_offset(job_resrcs_ptr,
							       job_node_inx,
							       sock_inx,
							       core_inx);
			if (bit_offset < 0)
				fatal("get_job_resources_offset");
			if (!bit_test(job_resrcs_ptr->core_bitmap, bit_offset))
				continue;
			if ((use_all_cores == false) &&
			    bit_test(job_resrcs_ptr->core_bitmap_used,
				     bit_offset)) {
				continue;
			}
			bit_set(job_resrcs_ptr->core_bitmap_used, bit_offset);
			bit_set(step_ptr->core_bitmap_job, bit_offset);
#if 0
			info("step alloc N:%d S:%dC :%d",
			     job_node_inx, sock_inx, core_inx);
#endif
			if (--cpu_cnt == 0)
				return;
		}
	}
	/* The test for cores==0 is just to avoid CLANG errors.
	 * It should never happen */
	if (use_all_cores || (cores == 0))
		return;

	/* We need to over-subscribe one or more cores.
	 * Use last_core_inx to avoid putting all of the extra
	 * work onto core zero */
	verbose("job step needs to over-subscribe cores");
	last_core_inx = (last_core_inx + 1) % cores;
	for (i=0; i<cores; i++) {
		core_inx = (last_core_inx + i) % cores;
		for (sock_inx=0; sock_inx<sockets; sock_inx++) {
			bit_offset = get_job_resources_offset(job_resrcs_ptr,
							       job_node_inx,
							       sock_inx,
							       core_inx);
			if (bit_offset < 0)
				fatal("get_job_resources_offset");
			if (!bit_test(job_resrcs_ptr->core_bitmap, bit_offset))
				continue;
			if (bit_test(step_ptr->core_bitmap_job, bit_offset))
				continue;   /* already taken by this step */
			bit_set(step_ptr->core_bitmap_job, bit_offset);
#if 0
			info("step alloc N:%d S:%dC :%d",
			     job_node_inx, sock_inx, core_inx);
#endif
			if (--cpu_cnt == 0)
				return;
		}
	}
}

#ifdef HAVE_ALPS_CRAY
/* Return the total cpu count on a given node index */
static int _get_node_cpus(int node_inx)
{
	struct node_record *node_ptr;

	node_ptr = node_record_table_ptr + node_inx;
	if (slurmctld_conf.fast_schedule)
		return node_ptr->config_ptr->cpus;

	return node_ptr->cpus;
}
#endif

/* Update a job's record of allocated CPUs when a job step gets scheduled */
extern void step_alloc_lps(struct step_record *step_ptr)
{
	struct job_record  *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int cpus_alloc;
	int i_node, i_first, i_last;
	int job_node_inx = -1, step_node_inx = -1;
	bool pick_step_cores = true;

	xassert(job_resrcs_ptr);
	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	if (step_ptr->step_layout == NULL)	/* batch step */
		return;

	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
	if (i_first == -1)	/* empty bitmap */
		return;

#ifdef HAVE_BG
	pick_step_cores = false;
#else
	xassert(job_resrcs_ptr->core_bitmap);
	xassert(job_resrcs_ptr->core_bitmap_used);
	if (step_ptr->core_bitmap_job) {
		/* "scontrol reconfig" of live system */
		pick_step_cores = false;
	} else if ((step_ptr->exclusive == 0) ||
		   (step_ptr->cpu_count == job_ptr->total_cpus)) {
		/* Step uses all of job's cores
		 * Just copy the bitmap to save time */
		step_ptr->core_bitmap_job = bit_copy(
			job_resrcs_ptr->core_bitmap);
		pick_step_cores = false;
	}
#endif

	if (step_ptr->pn_min_memory && _is_mem_resv() &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("step_alloc_lps: lack memory allocation details "
		      "to enforce memory limits for job %u", job_ptr->job_id);
		step_ptr->pn_min_memory = 0;
	}

	for (i_node = i_first; i_node <= i_last; i_node++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i_node))
			continue;
		job_node_inx++;
		if (!bit_test(step_ptr->step_node_bitmap, i_node))
			continue;
		step_node_inx++;
		if (job_node_inx >= job_resrcs_ptr->nhosts)
			fatal("step_alloc_lps: node index bad");
#ifdef HAVE_ALPS_CRAY
		/* Since with alps cray you can only run 1 job per node
		   return all CPUs as being allocated.
		*/
		cpus_alloc = _get_node_cpus(step_node_inx);
#else
		/* NOTE: The --overcommit option can result in
		 * cpus_used[] having a higher value than cpus[] */
		cpus_alloc = step_ptr->step_layout->tasks[step_node_inx] *
			     step_ptr->cpus_per_task;
#endif
		job_resrcs_ptr->cpus_used[job_node_inx] += cpus_alloc;
		gres_plugin_step_alloc(step_ptr->gres_list, job_ptr->gres_list,
				       job_node_inx, cpus_alloc,
				       job_ptr->job_id, step_ptr->step_id);
		if (step_ptr->pn_min_memory && _is_mem_resv()) {
			if (step_ptr->pn_min_memory & MEM_PER_CPU) {
				uint32_t mem_use = step_ptr->pn_min_memory;
				mem_use &= (~MEM_PER_CPU);
				job_resrcs_ptr->memory_used[job_node_inx] +=
					(mem_use * cpus_alloc);
			} else {
				job_resrcs_ptr->memory_used[job_node_inx] +=
					step_ptr->pn_min_memory;
			}
		}
		if (pick_step_cores) {
			_pick_step_cores(step_ptr, job_resrcs_ptr,
					 job_node_inx,
					 step_ptr->step_layout->
					 tasks[step_node_inx]);
		}
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_CPU_BIND)
			_dump_step_layout(step_ptr);
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
			info("step alloc of %s procs: %u of %u",
			     node_record_table_ptr[i_node].name,
			     job_resrcs_ptr->cpus_used[job_node_inx],
			     job_resrcs_ptr->cpus[job_node_inx]);
		}
		if (step_node_inx == (step_ptr->step_layout->node_cnt - 1))
			break;
	}
	gres_plugin_step_state_log(step_ptr->gres_list, job_ptr->job_id,
				   step_ptr->step_id);
}

/* Dump a job step's CPU binding information.
 * NOTE: The core_bitmap_job and node index are based upon
 * the _job_ allocation */
static void _dump_step_layout(struct step_record *step_ptr)
{
	struct job_record* job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int i, bit_inx, core_inx, node_inx, rep, sock_inx;

	if ((step_ptr->core_bitmap_job == NULL) ||
	    (job_resrcs_ptr == NULL) ||
	    (job_resrcs_ptr->cores_per_socket == NULL))
		return;

	info("====================");
	info("step_id:%u.%u", job_ptr->job_id, step_ptr->step_id);
	for (i=0, bit_inx=0, node_inx=0; node_inx<job_resrcs_ptr->nhosts; i++) {
		for (rep=0; rep<job_resrcs_ptr->sock_core_rep_count[i]; rep++) {
			for (sock_inx=0;
			     sock_inx<job_resrcs_ptr->sockets_per_node[i];
			     sock_inx++) {
				for (core_inx=0;
			 	     core_inx<job_resrcs_ptr->cores_per_socket[i];
			 	     core_inx++) {
					if (bit_test(step_ptr->
						     core_bitmap_job,
						     bit_inx++)) {
						info("JobNode[%d] Socket[%d] "
						     "Core[%d] is allocated",
						     node_inx, sock_inx,
						     core_inx);
					}
				}
			}
			node_inx++;
		}
	}
	info("====================");
}

static void _step_dealloc_lps(struct step_record *step_ptr)
{
	struct job_record  *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int cpus_alloc;
	int i_node, i_first, i_last;
	int job_node_inx = -1, step_node_inx = -1;

	xassert(job_resrcs_ptr);
	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	if (step_ptr->step_layout == NULL)	/* batch step */
		return;

	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
	if (i_first == -1)	/* empty bitmap */
		return;

	if (step_ptr->pn_min_memory && _is_mem_resv() &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("_step_dealloc_lps: lack memory allocation details "
		      "to enforce memory limits for job %u", job_ptr->job_id);
		step_ptr->pn_min_memory = 0;
	}

	for (i_node = i_first; i_node <= i_last; i_node++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i_node))
			continue;
		job_node_inx++;
		if (!bit_test(step_ptr->step_node_bitmap, i_node))
			continue;
		step_node_inx++;
		if (job_node_inx >= job_resrcs_ptr->nhosts)
			fatal("_step_dealloc_lps: node index bad");
#ifdef HAVE_ALPS_CRAY
		/* Since with alps cray you can only run 1 job per node
		   return all CPUs as being allocated.
		*/
		cpus_alloc = _get_node_cpus(step_node_inx);
#else
		cpus_alloc = step_ptr->step_layout->tasks[step_node_inx] *
			     step_ptr->cpus_per_task;
#endif
		if (job_resrcs_ptr->cpus_used[job_node_inx] >= cpus_alloc)
			job_resrcs_ptr->cpus_used[job_node_inx] -= cpus_alloc;
		else {
			error("_step_dealloc_lps: cpu underflow for %u.%u",
				job_ptr->job_id, step_ptr->step_id);
			job_resrcs_ptr->cpus_used[job_node_inx] = 0;
		}
		if (step_ptr->pn_min_memory && _is_mem_resv()) {
			uint32_t mem_use = step_ptr->pn_min_memory;
			if (mem_use & MEM_PER_CPU) {
				mem_use &= (~MEM_PER_CPU);
				mem_use *= cpus_alloc;
			}
			if (job_resrcs_ptr->memory_used[job_node_inx] >=
			    mem_use) {
				job_resrcs_ptr->memory_used[job_node_inx] -=
						mem_use;
			} else {
				error("_step_dealloc_lps: "
				      "mem underflow for %u.%u",
				      job_ptr->job_id, step_ptr->step_id);
				job_resrcs_ptr->memory_used[job_node_inx] = 0;
			}
		}
		if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
			info("step dealloc of %s procs: %u of %u",
			     node_record_table_ptr[i_node].name,
			     job_resrcs_ptr->cpus_used[job_node_inx],
			     job_resrcs_ptr->cpus[job_node_inx]);
		}
		if (step_node_inx == (step_ptr->step_layout->node_cnt - 1))
			break;
	}

#ifndef HAVE_BG
	xassert(job_resrcs_ptr->core_bitmap);
	xassert(job_resrcs_ptr->core_bitmap_used);
	if (step_ptr->core_bitmap_job) {
		/* Mark the job's cores as no longer in use */
		bit_not(step_ptr->core_bitmap_job);
		bit_and(job_resrcs_ptr->core_bitmap_used,
			step_ptr->core_bitmap_job);
		/* no need for bit_not(step_ptr->core_bitmap_job); */
		FREE_NULL_BITMAP(step_ptr->core_bitmap_job);
	}
#endif
}

static int _test_strlen(char *test_str, char *str_name, int max_str_len)
{
	int i = 0;

	if (test_str)
		i = strlen(test_str);
	if (i > max_str_len) {
		info("step_create_request: strlen(%s) too big (%d > %d)",
		     str_name, i, max_str_len);
		return ESLURM_PATHNAME_TOO_LONG;
	}
	return SLURM_SUCCESS;
}

/*
 * step_create - creates a step_record in step_specs->job_id, sets up the
 *	according to the step_specs.
 * IN step_specs - job step specifications
 * OUT new_step_record - pointer to the new step_record (NULL on error)
 * IN batch_step - if set then step is a batch script
 * RET - 0 or error code
 * NOTE: don't free the returned step_record because that is managed through
 * 	the job.
 */
extern int
step_create(job_step_create_request_msg_t *step_specs,
	    struct step_record** new_step_record, bool batch_step)
{
	struct step_record *step_ptr;
	struct job_record  *job_ptr;
	bitstr_t *nodeset;
	int cpus_per_task, ret_code, i;
	uint32_t node_count = 0;
	time_t now = time(NULL);
	char *step_node_list = NULL;
	uint32_t orig_cpu_count;
	List step_gres_list = (List) NULL;
	dynamic_plugin_data_t *select_jobinfo = NULL;
#ifdef HAVE_ALPS_CRAY
	uint32_t resv_id = 0;
#endif
#if defined HAVE_BG
	static uint16_t cpus_per_mp = (uint16_t)NO_VAL;
#elif (!defined HAVE_ALPS_CRAY)
	uint32_t max_tasks;
#endif
	*new_step_record = NULL;
	job_ptr = find_job_record (step_specs->job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID ;

	if ((job_ptr->details == NULL) || IS_JOB_SUSPENDED(job_ptr))
		return ESLURM_DISABLED;

	if (IS_JOB_PENDING(job_ptr)) {
		/* NOTE: LSF creates a job allocation for batch jobs.
		 * After the allocation has been made, LSF submits a
		 * job to run in that allocation (sbatch --jobid= ...).
		 * If that job is pending either LSF messed up or LSF is
		 * not being used. We have seen this problem with Moab. */
		return ESLURM_DUPLICATE_JOB_ID;
	}

	/* NOTE: We have already confirmed the UID originating
	 * the request is identical with step_specs->user_id */
	if (step_specs->user_id != job_ptr->user_id)
		return ESLURM_ACCESS_DENIED ;

	if (batch_step) {
		info("user %u attempting to run batch script within "
			"an existing job", step_specs->user_id);
		/* This seems hazardous to allow, but LSF seems to
		 * work this way, so don't treat it as an error. */
	}

	if (IS_JOB_FINISHED(job_ptr) ||
	    (job_ptr->end_time <= time(NULL)))
		return ESLURM_ALREADY_DONE;

	if ((step_specs->task_dist != SLURM_DIST_CYCLIC) &&
	    (step_specs->task_dist != SLURM_DIST_BLOCK) &&
	    (step_specs->task_dist != SLURM_DIST_CYCLIC_CYCLIC) &&
	    (step_specs->task_dist != SLURM_DIST_BLOCK_CYCLIC) &&
	    (step_specs->task_dist != SLURM_DIST_CYCLIC_BLOCK) &&
	    (step_specs->task_dist != SLURM_DIST_BLOCK_BLOCK) &&
	    (step_specs->task_dist != SLURM_DIST_CYCLIC_CFULL) &&
	    (step_specs->task_dist != SLURM_DIST_BLOCK_CFULL) &&
	    (step_specs->task_dist != SLURM_DIST_PLANE) &&
	    (step_specs->task_dist != SLURM_DIST_ARBITRARY))
		return ESLURM_BAD_DIST;

	if ((step_specs->task_dist == SLURM_DIST_ARBITRARY) &&
	    (!strcmp(slurmctld_conf.switch_type, "switch/elan"))) {
		return ESLURM_TASKDIST_ARBITRARY_UNSUPPORTED;
	}

	if (_test_strlen(step_specs->ckpt_dir, "ckpt_dir", MAXPATHLEN)	||
	    _test_strlen(step_specs->gres, "gres", 1024)		||
	    _test_strlen(step_specs->host, "host", 1024)		||
	    _test_strlen(step_specs->name, "name", 1024)		||
	    _test_strlen(step_specs->network, "network", 1024)		||
	    _test_strlen(step_specs->node_list, "node_list", 1024*64))
		return ESLURM_PATHNAME_TOO_LONG;

	if (job_ptr->next_step_id >= slurmctld_conf.max_step_cnt)
		return ESLURM_STEP_LIMIT;

#ifdef HAVE_ALPS_CRAY
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_RESV_ID, &resv_id);
#endif

#if defined HAVE_BG
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_NODE_CNT,
				    &node_count);

#if defined HAVE_BGQ
	if (step_specs->min_nodes < node_count) {
		node_count = step_specs->min_nodes;

		step_specs->min_nodes = 1;
		step_specs->max_nodes = 1;
	} else if (node_count == step_specs->min_nodes) {
		step_specs->min_nodes = job_ptr->details->min_nodes;
		step_specs->max_nodes = job_ptr->details->max_nodes;
	} else {
		error("bad node count %u only have %u", step_specs->min_nodes,
		      node_count);
		return ESLURM_INVALID_NODE_COUNT;
	}
#else
	/* No sub-block steps in BGL/P, always give them the full allocation */
	step_specs->min_nodes = job_ptr->details->min_nodes;
	step_specs->max_nodes = job_ptr->details->max_nodes;
#endif

	if (cpus_per_mp == (uint16_t)NO_VAL)
		select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
					&cpus_per_mp);
	/* Below is done to get the correct cpu_count and then we need to set
	 * the cpu_count to 0 later so just pretend we are overcommitting. */
	step_specs->cpu_count = node_count * cpus_per_mp;
	step_specs->overcommit = 1;
	step_specs->exclusive = 0;
#endif

#ifndef HAVE_BGQ /* This is to remove a Clang error since
		  * orig_cpu_count is set below for BGQ systems. */
	/* if the overcommit flag is checked, we 0 set cpu_count=0
	 * which makes it so we don't check to see the available cpus
	 */
	orig_cpu_count =  step_specs->cpu_count;
#endif

	if (step_specs->overcommit) {
		if (step_specs->exclusive) {
			/* Not really a legitimate combination, try to
			 * exclusively allocate one CPU per task */
			step_specs->overcommit = 0;
			step_specs->cpu_count = step_specs->num_tasks;
		} else
			step_specs->cpu_count = 0;
	}

	/* determine cpus_per_task value by reversing what srun does */
	if (step_specs->num_tasks < 1)
		return ESLURM_BAD_TASK_COUNT;

	/* we set cpus_per_task to 0 if we can't spread them evenly
	 * over the nodes (hetergeneous systems) */
	if (!step_specs->cpu_count
	    || (step_specs->cpu_count % step_specs->num_tasks))
		cpus_per_task = 0;
	else {
		cpus_per_task = step_specs->cpu_count / step_specs->num_tasks;
		if (cpus_per_task < 1)
			cpus_per_task = 1;
	}

	if (step_specs->no_kill > 1)
		step_specs->no_kill = 1;

	if (step_specs->gres && !strcasecmp(step_specs->gres, "NONE"))
		xfree(step_specs->gres);
	else if (step_specs->gres == NULL)
		step_specs->gres = xstrdup(job_ptr->gres);
	i = gres_plugin_step_state_validate(step_specs->gres, &step_gres_list,
					    job_ptr->gres_list, job_ptr->job_id,
					    NO_VAL);
	if (i != SLURM_SUCCESS) {
		if (step_gres_list)
			list_destroy(step_gres_list);
		return i;
	}

	job_ptr->time_last_active = now;

	/* make sure this exists since we need it so we don't core on
	 * a xassert */
	select_jobinfo = select_g_select_jobinfo_alloc();
	nodeset = _pick_step_nodes(job_ptr, step_specs, step_gres_list,
				   cpus_per_task, node_count, select_jobinfo,
				   &ret_code);
	if (nodeset == NULL) {
		if (step_gres_list)
			list_destroy(step_gres_list);
		select_g_select_jobinfo_free(select_jobinfo);
		if ((ret_code == ESLURM_NODES_BUSY) ||
		    (ret_code == ESLURM_PORTS_BUSY) ||
		    (ret_code == ESLURM_INTERCONNECT_BUSY))
			_build_pending_step(job_ptr, step_specs);
		return ret_code;
	}
#ifdef HAVE_ALPS_CRAY
	select_g_select_jobinfo_set(select_jobinfo,
				    SELECT_JOBDATA_RESV_ID, &resv_id);
#endif
#ifdef HAVE_BGQ
	/* Things might of changed here since sometimes users ask for
	 * the wrong size in cnodes to make a block. */
	select_g_select_jobinfo_get(select_jobinfo,
				    SELECT_JOBDATA_NODE_CNT,
				    &node_count);
	step_specs->cpu_count = node_count * cpus_per_mp;
	orig_cpu_count =  step_specs->cpu_count;
#else
	node_count = bit_set_count(nodeset);
#endif
	if (step_specs->num_tasks == NO_VAL) {
		if (step_specs->cpu_count != NO_VAL)
			step_specs->num_tasks = step_specs->cpu_count;
		else
			step_specs->num_tasks = node_count;
	}

#if (!defined HAVE_BG && !defined HAVE_ALPS_CRAY)
	max_tasks = node_count * slurmctld_conf.max_tasks_per_node;
	if (step_specs->num_tasks > max_tasks) {
		error("step has invalid task count: %u max is %u",
		      step_specs->num_tasks, max_tasks);
		if (step_gres_list)
			list_destroy(step_gres_list);
		FREE_NULL_BITMAP(nodeset);
		select_g_select_jobinfo_free(select_jobinfo);
		return ESLURM_BAD_TASK_COUNT;
	}
#endif
	step_ptr = _create_step_record(job_ptr);
	if (step_ptr == NULL) {
		if (step_gres_list)
			list_destroy(step_gres_list);
		FREE_NULL_BITMAP(nodeset);
		select_g_select_jobinfo_free(select_jobinfo);
		return ESLURMD_TOOMANYSTEPS;
	}
	step_ptr->start_time = time(NULL);
	step_ptr->state      = JOB_RUNNING;
	step_ptr->step_id    = job_ptr->next_step_id++;

	/* Here is where the node list is set for the step */
	if (step_specs->node_list &&
	    (step_specs->task_dist == SLURM_DIST_ARBITRARY)) {
		step_node_list = xstrdup(step_specs->node_list);
		xfree(step_specs->node_list);
		step_specs->node_list = bitmap2node_name(nodeset);
	} else {
		step_node_list = bitmap2node_name_sortable(nodeset, false);
		xfree(step_specs->node_list);
		step_specs->node_list = xstrdup(step_node_list);
	}
	if (slurmctld_conf.debug_flags & DEBUG_FLAG_STEPS) {
		verbose("Picked nodes %s when accumulating from %s",
			step_node_list, step_specs->node_list);
	}
	step_ptr->step_node_bitmap = nodeset;

	switch(step_specs->task_dist) {
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_CYCLIC_CFULL:
	case SLURM_DIST_CYCLIC_BLOCK:
		step_ptr->cyclic_alloc = 1;
		break;
	default:
		step_ptr->cyclic_alloc = 0;
		break;
	}

	step_ptr->gres      = step_specs->gres;
	step_specs->gres    = NULL;
	step_ptr->gres_list = step_gres_list;
	step_gres_list      = (List) NULL;
	gres_plugin_step_state_log(step_ptr->gres_list, job_ptr->job_id,
				   step_ptr->step_id);

	step_ptr->port = step_specs->port;
	step_ptr->host = xstrdup(step_specs->host);
	step_ptr->batch_step = batch_step;
	step_ptr->cpu_freq = step_specs->cpu_freq;
	step_ptr->cpus_per_task = (uint16_t)cpus_per_task;
	step_ptr->pn_min_memory = step_specs->pn_min_memory;
	step_ptr->ckpt_interval = step_specs->ckpt_interval;
	step_ptr->ckpt_time = now;
	step_ptr->cpu_count = orig_cpu_count;
	step_ptr->exit_code = NO_VAL;
	step_ptr->exclusive = step_specs->exclusive;
	step_ptr->ckpt_dir  = xstrdup(step_specs->ckpt_dir);
	step_ptr->no_kill   = step_specs->no_kill;
	step_ptr->ext_sensors = ext_sensors_alloc();

	/* step's name and network default to job's values if not
	 * specified in the step specification */
	if (step_specs->name && step_specs->name[0])
		step_ptr->name = xstrdup(step_specs->name);
	else
		step_ptr->name = xstrdup(job_ptr->name);
	if (step_specs->network && step_specs->network[0])
		step_ptr->network = xstrdup(step_specs->network);
	else
		step_ptr->network = xstrdup(job_ptr->network);

	step_ptr->select_jobinfo = select_jobinfo;
	select_jobinfo = NULL;

	/* the step time_limit is recorded as submitted (INFINITE
	 * or partition->max_time by default), but the allocation
	 * time limits may cut it short */
	if (step_specs->time_limit == NO_VAL || step_specs->time_limit == 0 ||
	    step_specs->time_limit == INFINITE) {
		step_ptr->time_limit = INFINITE;
	} else {
		/* enforce partition limits if necessary */
		if ((step_specs->time_limit > job_ptr->part_ptr->max_time) &&
		    slurmctld_conf.enforce_part_limits) {
			info("_step_create: step time greater than partition's "
			     "(%u > %u)", step_specs->time_limit,
			     job_ptr->part_ptr->max_time);
			delete_step_record (job_ptr, step_ptr->step_id);
			return ESLURM_INVALID_TIME_LIMIT;
		}
		step_ptr->time_limit = step_specs->time_limit;
	}

	/* a batch script does not need switch info */
	if (!batch_step) {
		step_ptr->step_layout =
			step_layout_create(step_ptr,
					   step_node_list, node_count,
					   step_specs->num_tasks,
					   (uint16_t)cpus_per_task,
					   step_specs->task_dist,
					   step_specs->plane_size);
		xfree(step_node_list);
		if (!step_ptr->step_layout) {
			delete_step_record (job_ptr, step_ptr->step_id);
			if (step_specs->pn_min_memory)
				return ESLURM_INVALID_TASK_MEMORY;
			return SLURM_ERROR;
		}
		if ((step_specs->resv_port_cnt != (uint16_t) NO_VAL)
		    && (step_specs->resv_port_cnt == 0)) {
			/* reserved port count set to maximum task count on
			 * any node plus one */
			for (i = 0; i < step_ptr->step_layout->node_cnt; i++) {
				step_specs->resv_port_cnt =
					MAX(step_specs->resv_port_cnt,
					    step_ptr->step_layout->tasks[i]);
			}
			step_specs->resv_port_cnt++;
		}
		if (step_specs->resv_port_cnt != (uint16_t) NO_VAL
		    && step_specs->resv_port_cnt != 0) {
			step_ptr->resv_port_cnt = step_specs->resv_port_cnt;
			i = resv_port_alloc(step_ptr);
			if (i != SLURM_SUCCESS) {
				delete_step_record (job_ptr,
						    step_ptr->step_id);
				return i;
			}
		}

		if (switch_g_alloc_jobinfo(&step_ptr->switch_job,
					   step_ptr->job_ptr->job_id,
					   step_ptr->step_id) < 0)
			fatal ("step_create: switch_g_alloc_jobinfo error");

		if (switch_g_build_jobinfo(step_ptr->switch_job,
					   step_ptr->step_layout,
					   step_ptr->network) < 0) {
			delete_step_record (job_ptr, step_ptr->step_id);
			if (errno == ESLURM_INTERCONNECT_BUSY)
				return errno;
			return ESLURM_INTERCONNECT_FAILURE;
		}
		step_alloc_lps(step_ptr);
	} else
		xfree(step_node_list);
	if (checkpoint_alloc_jobinfo (&step_ptr->check_job) < 0)
		fatal ("step_create: checkpoint_alloc_jobinfo error");
	*new_step_record = step_ptr;

	if (!with_slurmdbd && !job_ptr->db_index)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	select_g_step_start(step_ptr);

	jobacct_storage_g_step_start(acct_db_conn, step_ptr);
	return SLURM_SUCCESS;
}

extern slurm_step_layout_t *step_layout_create(struct step_record *step_ptr,
					       char *step_node_list,
					       uint32_t node_count,
					       uint32_t num_tasks,
					       uint16_t cpus_per_task,
					       uint16_t task_dist,
					       uint16_t plane_size)
{
	uint16_t cpus_per_node[node_count];
	struct job_record *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
#ifndef HAVE_BGQ
	uint32_t gres_cpus;
	int cpu_inx = -1;
	int i, usable_cpus, usable_mem;
	int set_nodes = 0/* , set_tasks = 0 */;
	int pos = -1, job_node_offset = -1;
	int first_bit, last_bit;
	uint32_t cpu_count_reps[node_count];
#else
	uint32_t cpu_count_reps[1];
#endif

	xassert(job_resrcs_ptr);
	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	if (step_ptr->pn_min_memory && _is_mem_resv() &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("step_layout_create: lack memory allocation details "
		      "to enforce memory limits for job %u", job_ptr->job_id);
		step_ptr->pn_min_memory = 0;
	} else if (step_ptr->pn_min_memory == MEM_PER_CPU)
		step_ptr->pn_min_memory = 0;	/* clear MEM_PER_CPU flag */

#ifdef HAVE_BGQ
	/* Since we have to deal with a conversion between cnodes and
	   midplanes here the math is really easy, and already has
	   been figured out for us in the plugin, so just copy the
	   numbers.
	*/
	memcpy(cpus_per_node, job_resrcs_ptr->cpus, sizeof(cpus_per_node));
	cpu_count_reps[0] = job_resrcs_ptr->ncpus;

#else
	/* build the cpus-per-node arrays for the subset of nodes
	 * used by this job step */
	first_bit = bit_ffs(job_ptr->node_bitmap);
	last_bit  = bit_fls(job_ptr->node_bitmap);
	for (i = first_bit; i <= last_bit; i++) {
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;
		job_node_offset++;
		if (bit_test(step_ptr->step_node_bitmap, i)) {
			/* find out the position in the job */
			pos = bit_get_pos_num(job_resrcs_ptr->node_bitmap, i);
			if (pos == -1)
				return NULL;
			if (pos >= job_resrcs_ptr->nhosts)
				fatal("step_layout_create: node index bad");
			if (step_ptr->exclusive) {
				usable_cpus = job_resrcs_ptr->cpus[pos] -
					      job_resrcs_ptr->cpus_used[pos];
			} else
				usable_cpus = job_resrcs_ptr->cpus[pos];
			if ((step_ptr->pn_min_memory & MEM_PER_CPU) &&
			    _is_mem_resv()) {
				uint32_t mem_use = step_ptr->pn_min_memory;
				mem_use &= (~MEM_PER_CPU);
				usable_mem =
					job_resrcs_ptr->memory_allocated[pos] -
					job_resrcs_ptr->memory_used[pos];
				usable_mem /= mem_use;
				usable_cpus = MIN(usable_cpus, usable_mem);
			}

			gres_cpus = gres_plugin_step_test(step_ptr->gres_list,
							  job_ptr->gres_list,
							  job_node_offset,
							  false,
							  job_ptr->job_id,
							  step_ptr->step_id);
			usable_cpus = MIN(usable_cpus, gres_cpus);
			if (usable_cpus <= 0) {
				error("step_layout_create no usable cpus");
				return NULL;
			}
			debug3("step_layout cpus = %d pos = %d",
			       usable_cpus, pos);

			if ((cpu_inx == -1) ||
			    (cpus_per_node[cpu_inx] != usable_cpus)) {
				cpu_inx++;

				cpus_per_node[cpu_inx] = usable_cpus;
				cpu_count_reps[cpu_inx] = 1;
			} else
				cpu_count_reps[cpu_inx]++;
			set_nodes++;
			/*FIX ME: on a heterogeneous system running
			  the linear select plugin we could get a node
			  that doesn't have as many cpus as we decided
			  we needed for each task.  This would result
			  in not getting a task for the node we
			  received.  This is usually in error.  This
			  only happens when the person doesn't specify
			  how many cpus_per_task they want, and we
			  have to come up with a number, in this case
			  it is wrong.
			*/
			/* if (cpus_per_task > 0) */
			/* 	set_tasks +=  */
			/* 		(uint16_t)usable_cpus / cpus_per_task; */
			/* else */
			/* 	/\* since cpus_per_task is 0 we just */
			/* 	   add the number of cpus available */
			/* 	   for this job *\/ */
			/* 	set_tasks += usable_cpus; */
			/* info("usable_cpus is %d and set_tasks %d %d", */
			/*      usable_cpus, set_tasks, cpus_per_task); */
			if (set_nodes == node_count)
				break;
		}
	}
#endif
	/* if (set_tasks < num_tasks) { */
	/* 	error("Resources only available for %u of %u tasks", */
	/* 	     set_tasks, num_tasks); */
	/* 	return NULL; */
	/* } */

	/* layout the tasks on the nodes */
	return slurm_step_layout_create(step_node_list,
					cpus_per_node, cpu_count_reps,
					node_count, num_tasks,
					cpus_per_task,
					task_dist,
					plane_size);
}

/* Pack the data for a specific job step record
 * IN step - pointer to a job step record
 * IN/OUT buffer - location to store data, pointers automatically advanced
 * IN protocol_version - slurm protocol version of client
 */
static void _pack_ctld_job_step_info(struct step_record *step_ptr, Buf buffer,
				     uint16_t protocol_version)
{
	uint32_t task_cnt, cpu_cnt;
	char *node_list = NULL;
	time_t begin_time, run_time;
	bitstr_t *pack_bitstr;

//#if defined HAVE_FRONT_END && (!defined HAVE_BGQ || !defined HAVE_BG_FILES)
#if defined HAVE_FRONT_END && (!defined HAVE_BGQ) && (!defined HAVE_ALPS_CRAY)
	/* On front-end systems, the steps only execute on one node.
	 * We need to make them appear like they are running on the job's
	 * entire allocation (which they really are). */
	task_cnt = step_ptr->job_ptr->cpu_cnt;
	node_list = step_ptr->job_ptr->nodes;
	pack_bitstr = step_ptr->job_ptr->node_bitmap;

	if (step_ptr->job_ptr->total_cpus)
		cpu_cnt = step_ptr->job_ptr->total_cpus;
	else if (step_ptr->job_ptr->details)
		cpu_cnt = step_ptr->job_ptr->details->min_cpus;
	else
		cpu_cnt = step_ptr->job_ptr->cpu_cnt;
#else
	pack_bitstr = step_ptr->step_node_bitmap;
	if (step_ptr->step_layout) {
		task_cnt = step_ptr->step_layout->task_cnt;
		node_list = step_ptr->step_layout->node_list;
	} else {
		task_cnt = step_ptr->cpu_count;
		node_list = step_ptr->job_ptr->nodes;
	}
	cpu_cnt = step_ptr->cpu_count;
#endif

	if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		pack32(step_ptr->job_ptr->array_job_id, buffer);
		pack32(step_ptr->job_ptr->array_task_id, buffer);
		pack32(step_ptr->job_ptr->job_id, buffer);
		pack32(step_ptr->step_id, buffer);
		pack16(step_ptr->ckpt_interval, buffer);
		pack32(step_ptr->job_ptr->user_id, buffer);
		pack32(cpu_cnt, buffer);
		pack32(step_ptr->cpu_freq, buffer);
		pack32(task_cnt, buffer);
		pack32(step_ptr->time_limit, buffer);
		pack16(step_ptr->state, buffer);

		pack_time(step_ptr->start_time, buffer);
		if (IS_JOB_SUSPENDED(step_ptr->job_ptr)) {
			run_time = step_ptr->pre_sus_time;
		} else {
			begin_time = MAX(step_ptr->start_time,
					 step_ptr->job_ptr->suspend_time);
			run_time = step_ptr->pre_sus_time +
				difftime(time(NULL), begin_time);
		}
		pack_time(run_time, buffer);

		if (step_ptr->job_ptr->part_ptr)
			packstr(step_ptr->job_ptr->part_ptr->name, buffer);
		else
			packstr(step_ptr->job_ptr->partition, buffer);
		packstr(step_ptr->resv_ports, buffer);
		packstr(node_list, buffer);
		packstr(step_ptr->name, buffer);
		packstr(step_ptr->network, buffer);
		pack_bit_fmt(pack_bitstr, buffer);
		packstr(step_ptr->ckpt_dir, buffer);
		packstr(step_ptr->gres, buffer);
		select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
					     protocol_version);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		pack32(step_ptr->job_ptr->array_job_id, buffer);
		pack16((uint16_t) step_ptr->job_ptr->array_task_id, buffer);
		pack32(step_ptr->job_ptr->job_id, buffer);
		pack32(step_ptr->step_id, buffer);
		pack16(step_ptr->ckpt_interval, buffer);
		pack32(step_ptr->job_ptr->user_id, buffer);
		pack32(cpu_cnt, buffer);
		pack32(step_ptr->cpu_freq, buffer);
		pack32(task_cnt, buffer);
		pack32(step_ptr->time_limit, buffer);
		pack16(step_ptr->state, buffer);

		pack_time(step_ptr->start_time, buffer);
		if (IS_JOB_SUSPENDED(step_ptr->job_ptr)) {
			run_time = step_ptr->pre_sus_time;
		} else {
			begin_time = MAX(step_ptr->start_time,
					 step_ptr->job_ptr->suspend_time);
			run_time = step_ptr->pre_sus_time +
				difftime(time(NULL), begin_time);
		}
		pack_time(run_time, buffer);

		packstr(step_ptr->job_ptr->partition, buffer);
		packstr(step_ptr->resv_ports, buffer);
		packstr(node_list, buffer);
		packstr(step_ptr->name, buffer);
		packstr(step_ptr->network, buffer);
		pack_bit_fmt(pack_bitstr, buffer);
		packstr(step_ptr->ckpt_dir, buffer);
		packstr(step_ptr->gres, buffer);
		select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
					     protocol_version);
	} else {
		error("_pack_ctld_job_step_info: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

/*
 * pack_ctld_job_step_info_response_msg - packs job step info
 * IN job_id - specific id or NO_VAL for all
 * IN step_id - specific id or NO_VAL for all
 * IN uid - user issuing request
 * IN show_flags - job step filtering options
 * OUT buffer - location to store data, pointers automatically advanced
 * RET - 0 or error code
 * NOTE: MUST free_buf buffer
 */
extern int pack_ctld_job_step_info_response_msg(
	uint32_t job_id, uint32_t step_id, uid_t uid,
	uint16_t show_flags, Buf buffer, uint16_t protocol_version)
{
	ListIterator job_iterator;
	ListIterator step_iterator;
	int error_code = 0;
	uint32_t steps_packed = 0, tmp_offset;
	struct step_record *step_ptr;
	struct job_record *job_ptr;
	time_t now = time(NULL);
	int valid_job = 0;

	pack_time(now, buffer);
	pack32(steps_packed, buffer);	/* steps_packed placeholder */

	part_filter_set(uid);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if ((job_id != NO_VAL) && (job_id != job_ptr->job_id) &&
		    (job_id != job_ptr->array_job_id))
			continue;

		if (((show_flags & SHOW_ALL) == 0) &&
		    (job_ptr->part_ptr) &&
		    (job_ptr->part_ptr->flags & PART_FLAG_HIDDEN))
			continue;

		if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS) &&
		    (job_ptr->user_id != uid) && !validate_operator(uid) &&
		    !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						  job_ptr->account))
			continue;

		valid_job = 1;

		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = list_next(step_iterator))) {
			if ((step_id != NO_VAL) &&
			    (step_ptr->step_id != step_id))
				continue;
			_pack_ctld_job_step_info(step_ptr, buffer,
						 protocol_version);
			steps_packed++;
		}
		list_iterator_destroy(step_iterator);
	}
	list_iterator_destroy(job_iterator);

	if (list_count(job_list) && !valid_job && !steps_packed)
		error_code = ESLURM_INVALID_JOB_ID;

	part_filter_clear();

	/* put the real record count in the message body header */
	tmp_offset = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack_time(now, buffer);
	pack32(steps_packed, buffer);
	set_buf_offset(buffer, tmp_offset);

	return error_code;
}

/*
 * kill_step_on_node - determine if the specified job has any job steps
 *	allocated to the specified node and kill them unless no_kill flag
 *	is set on the step
 * IN job_ptr - pointer to an active job record
 * IN node_ptr - pointer to a node record
 * IN node_fail - true of removed node has failed
 * RET count of killed job steps
 */
extern int kill_step_on_node(struct job_record  *job_ptr,
			     struct node_record *node_ptr, bool node_fail)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	int found = 0;
	int bit_position;

	if ((job_ptr == NULL) || (node_ptr == NULL))
		return found;

	bit_position = node_ptr - node_record_table_ptr;
	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		if (bit_test(step_ptr->step_node_bitmap, bit_position) == 0)
			continue;
		if (node_fail && !step_ptr->no_kill)
			srun_step_complete(step_ptr);
		info("killing step %u.%u on node %s",
		     job_ptr->job_id, step_ptr->step_id, node_ptr->name);
		signal_step_tasks_on_node(node_ptr->name, step_ptr, SIGKILL,
					  REQUEST_TERMINATE_TASKS);
		found++;
	}

	list_iterator_destroy (step_iterator);
	return found;
}

/*
 * job_step_checkpoint - perform some checkpoint operation
 * IN ckpt_ptr - checkpoint request message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint(checkpoint_msg_t *ckpt_ptr,
			       uid_t uid, slurm_fd_t conn_fd,
			       uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	checkpoint_resp_msg_t resp_data;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&resp_msg);
	resp_msg.protocol_version = protocol_version;

	/* find the job */
	job_ptr = find_job_record (ckpt_ptr->job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	if ((uid != job_ptr->user_id) && (uid != 0)) {
		rc = ESLURM_ACCESS_DENIED ;
		goto reply;
	}
	if (IS_JOB_PENDING(job_ptr)) {
		rc = ESLURM_JOB_PENDING;
		goto reply;
	} else if (IS_JOB_SUSPENDED(job_ptr)) {
		/* job can't get cycles for checkpoint
		 * if it is already suspended */
		rc = ESLURM_DISABLED;
		goto reply;
	} else if (!IS_JOB_RUNNING(job_ptr)) {
		rc = ESLURM_ALREADY_DONE;
		goto reply;
	}

	memset((void *)&resp_data, 0, sizeof(checkpoint_resp_msg_t));
	step_ptr = find_step_record(job_ptr, ckpt_ptr->step_id);
	if (step_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
	} else {
		if (ckpt_ptr->image_dir == NULL) {
			ckpt_ptr->image_dir = xstrdup(step_ptr->ckpt_dir);
		}
		xstrfmtcat(ckpt_ptr->image_dir, "/%u.%u", job_ptr->job_id,
			   step_ptr->step_id);

		rc = checkpoint_op(ckpt_ptr->job_id, ckpt_ptr->step_id,
				   step_ptr, ckpt_ptr->op, ckpt_ptr->data,
				   ckpt_ptr->image_dir, &resp_data.event_time,
				   &resp_data.error_code,
				   &resp_data.error_msg);
		last_job_update = time(NULL);
	}

    reply:
	if ((rc == SLURM_SUCCESS) &&
	    ((ckpt_ptr->op == CHECK_ABLE) || (ckpt_ptr->op == CHECK_ERROR))) {
		resp_msg.msg_type = RESPONSE_CHECKPOINT;
		resp_msg.data = &resp_data;
		 (void) slurm_send_node_msg(conn_fd, &resp_msg);
	} else {
		return_code_msg_t rc_msg;
		rc_msg.return_code = rc;
		resp_msg.msg_type  = RESPONSE_SLURM_RC;
		resp_msg.data      = &rc_msg;
		(void) slurm_send_node_msg(conn_fd, &resp_msg);
	}
	return rc;
}

/*
 * job_step_checkpoint_comp - note job step checkpoint completion
 * IN ckpt_ptr - checkpoint complete status message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint_comp(checkpoint_comp_msg_t *ckpt_ptr,
				    uid_t uid, slurm_fd_t conn_fd,
				    uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;

	slurm_msg_t_init(&resp_msg);
	resp_msg.protocol_version = protocol_version;

	/* find the job */
	job_ptr = find_job_record (ckpt_ptr->job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	if ((uid != job_ptr->user_id) && (uid != 0)) {
		rc = ESLURM_ACCESS_DENIED;
		goto reply;
	}
	if (IS_JOB_PENDING(job_ptr)) {
		rc = ESLURM_JOB_PENDING;
		goto reply;
	} else if (!IS_JOB_RUNNING(job_ptr) &&
		   !IS_JOB_SUSPENDED(job_ptr)) {
		rc = ESLURM_ALREADY_DONE;
		goto reply;
	}

	step_ptr = find_step_record(job_ptr, ckpt_ptr->step_id);
	if (step_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	} else {
		rc = checkpoint_comp((void *)step_ptr, ckpt_ptr->begin_time,
			ckpt_ptr->error_code, ckpt_ptr->error_msg);
		last_job_update = time(NULL);
	}

    reply:
	rc_msg.return_code = rc;
	resp_msg.msg_type  = RESPONSE_SLURM_RC;
	resp_msg.data      = &rc_msg;
	(void) slurm_send_node_msg(conn_fd, &resp_msg);
	return rc;
}

/*
 * job_step_checkpoint_task_comp - note task checkpoint completion
 * IN ckpt_ptr - checkpoint task complete status message
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * IN protocol_version - slurm protocol version of client
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint_task_comp(checkpoint_task_comp_msg_t *ckpt_ptr,
					 uid_t uid, slurm_fd_t conn_fd,
					 uint16_t protocol_version)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;

	slurm_msg_t_init(&resp_msg);
	resp_msg.protocol_version = protocol_version;

	/* find the job */
	job_ptr = find_job_record (ckpt_ptr->job_id);
	if (job_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	}
	if ((uid != job_ptr->user_id) && (uid != 0)) {
		rc = ESLURM_ACCESS_DENIED;
		goto reply;
	}
	if (IS_JOB_PENDING(job_ptr)) {
		rc = ESLURM_JOB_PENDING;
		goto reply;
	} else if (!IS_JOB_RUNNING(job_ptr) &&
		   !IS_JOB_SUSPENDED(job_ptr)) {
		rc = ESLURM_ALREADY_DONE;
		goto reply;
	}

	step_ptr = find_step_record(job_ptr, ckpt_ptr->step_id);
	if (step_ptr == NULL) {
		rc = ESLURM_INVALID_JOB_ID;
		goto reply;
	} else {
		rc = checkpoint_task_comp((void *)step_ptr,
			ckpt_ptr->task_id, ckpt_ptr->begin_time,
			ckpt_ptr->error_code, ckpt_ptr->error_msg);
		last_job_update = time(NULL);
	}

    reply:
	rc_msg.return_code = rc;
	resp_msg.msg_type  = RESPONSE_SLURM_RC;
	resp_msg.data      = &rc_msg;
	(void) slurm_send_node_msg(conn_fd, &resp_msg);
	return rc;
}

/*
 * step_partial_comp - Note the completion of a job step on at least
 *	some of its nodes
 * IN req     - step_completion_msg RPC from slurmstepd
 * IN uid     - UID issuing the request
 * OUT rem    - count of nodes for which responses are still pending
 * OUT max_rc - highest return code for any step thus far
 * RET 0 on success, otherwise ESLURM error code
 */
extern int step_partial_comp(step_complete_msg_t *req, uid_t uid,
			     int *rem, uint32_t *max_rc)
{
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	int nodes, rem_nodes;

	/* find the job, step, and validate input */
	job_ptr = find_job_record (req->job_id);
	if (job_ptr == NULL) {
		info("step_partial_comp: JobID=%u invalid", req->job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	/* If we are requeuing the job the completing flag will be set
	 * but the state will be Pending, so don't use IS_JOB_PENDING
	 * which won't see the completing flag.
	 */
	if (job_ptr->job_state == JOB_PENDING) {
		info("step_partial_comp: JobID=%u pending", req->job_id);
		return ESLURM_JOB_PENDING;
	}

	if ((!validate_slurm_user(uid)) && (uid != job_ptr->user_id)) {
		/* Normally from slurmstepd, from srun on some failures */
		error("Security violation: "
		      "REQUEST_STEP_COMPLETE RPC for job %u from uid=%u",
		      job_ptr->job_id, (unsigned int) uid);
		return ESLURM_USER_ID_MISSING;
	}

	step_ptr = find_step_record(job_ptr, req->job_step_id);
	if (step_ptr == NULL) {
		info("step_partial_comp: StepID=%u.%u invalid",
		     req->job_id, req->job_step_id);
		return ESLURM_INVALID_JOB_ID;
	}
	if (step_ptr->batch_step) {
		if (rem)
			*rem = 0;
		step_ptr->exit_code = req->step_rc;
		if (max_rc)
			*max_rc = step_ptr->exit_code;
		jobacctinfo_aggregate(step_ptr->jobacct, req->jobacct);
		/* we don't want to delete the step record here since
		 * right after we delete this step again if we delete
		 * it here we won't find it when we try the second time */
		//delete_step_record(job_ptr, req->job_step_id);
		return SLURM_SUCCESS;
	}
	if (req->range_last < req->range_first) {
		error("step_partial_comp: StepID=%u.%u range=%u-%u",
		      req->job_id, req->job_step_id, req->range_first,
		      req->range_last);
		return EINVAL;
	}

	ext_sensors_g_get_stependdata(step_ptr);
	jobacctinfo_aggregate(step_ptr->jobacct, req->jobacct);

	/* we have been adding task average frequencies for
	 * jobacct->act_cpufreq so we need to divide with the
	 * total number of tasks/cpus for the step average frequency */
	if (step_ptr->cpu_count && step_ptr->jobacct)
		step_ptr->jobacct->act_cpufreq /= step_ptr->cpu_count;

	if (!step_ptr->exit_node_bitmap) {
		/* initialize the node bitmap for exited nodes */
		nodes = bit_set_count(step_ptr->step_node_bitmap);
#if defined HAVE_BGQ || defined HAVE_ALPS_CRAY
		/* For BGQ we only have 1 real task, so if it exits,
		   the whole step is ending as well.
		*/
		req->range_last = nodes - 1;
#endif
		step_ptr->exit_node_bitmap = bit_alloc(nodes);
		step_ptr->exit_code = req->step_rc;
	} else {
		nodes = bit_size(step_ptr->exit_node_bitmap);
#if defined HAVE_BGQ || defined HAVE_ALPS_CRAY
		/* For BGQ we only have 1 real task, so if it exits,
		   the whole step is ending as well.
		*/
		req->range_last = nodes - 1;
#endif
		step_ptr->exit_code = MAX(step_ptr->exit_code, req->step_rc);
	}
	if ((req->range_first >= nodes) || (req->range_last >= nodes) ||
	    (req->range_first > req->range_last)) {
		/* range is zero origin */
		error("step_partial_comp: StepID=%u.%u range=%u-%u nodes=%d",
		      req->job_id, req->job_step_id, req->range_first,
		      req->range_last, nodes);
		return EINVAL;
	}

	bit_nset(step_ptr->exit_node_bitmap,
		 req->range_first, req->range_last);
	rem_nodes = bit_clear_count(step_ptr->exit_node_bitmap);
	if (rem)
		*rem = rem_nodes;
	if (rem_nodes == 0) {
		/* release all switch windows */
		if (step_ptr->switch_job) {
			debug2("full switch release for step %u.%u, "
			       "nodes %s", req->job_id,
			       req->job_step_id,
			       step_ptr->step_layout->node_list);
			switch_g_job_step_complete(
				step_ptr->switch_job,
				step_ptr->step_layout->node_list);
			switch_g_free_jobinfo (step_ptr->switch_job);
			step_ptr->switch_job = NULL;
		}
	} else if (switch_g_part_comp() && step_ptr->switch_job) {
		/* release switch windows on completed nodes,
		 * must translate range numbers to nodelist */
		hostlist_t hl;
		char *node_list;

		hl = _step_range_to_hostlist(step_ptr,
			req->range_first, req->range_last);
		node_list = hostlist_ranged_string_xmalloc(hl);
		debug2("partitial switch release for step %u.%u, "
			"nodes %s", req->job_id,
			req->job_step_id, node_list);
		switch_g_job_step_part_comp(
			step_ptr->switch_job, node_list);
		hostlist_destroy(hl);
		xfree(node_list);
	}

	if (max_rc)
		*max_rc = step_ptr->exit_code;

	return SLURM_SUCCESS;
}

/* convert a range of nodes allocated to a step to a hostlist with
 * names of those nodes */
static hostlist_t _step_range_to_hostlist(struct step_record *step_ptr,
		uint32_t range_first, uint32_t range_last)
{
	int i, node_inx = -1;
	hostlist_t hl = hostlist_create(NULL);

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i) == 0)
			continue;
		node_inx++;
		if ((node_inx >= range_first)
		&&  (node_inx <= range_last)) {
			hostlist_push_host(hl,
				node_record_table_ptr[i].name);
		}
	}

	return hl;
}

/* convert a single node name to it's offset within a step's
 * nodes allocation. returns -1 on error */
static int _step_hostname_to_inx(struct step_record *step_ptr,
		char *node_name)
{
	struct node_record *node_ptr;
	int i, node_inx, node_offset = 0;

	node_ptr = find_node_record(node_name);
	if (node_ptr == NULL)
		return -1;
	node_inx = node_ptr - node_record_table_ptr;

	for (i = 0; i < node_inx; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i))
			node_offset++;
	}
	return node_offset;
}

extern int step_epilog_complete(struct job_record  *job_ptr,
		char *node_name)
{
	int rc = 0, node_inx, step_offset;
	ListIterator step_iterator;
	struct step_record *step_ptr;
	struct node_record *node_ptr;

	if (!switch_g_part_comp()) {
		/* don't bother with partitial completions */
		return 0;
	}
	if ((node_ptr = find_node_record(node_name)) == NULL)
		return 0;
	node_inx = node_ptr - node_record_table_ptr;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		if ((!step_ptr->switch_job)
		||  (bit_test(step_ptr->step_node_bitmap, node_inx) == 0))
			continue;
		if (step_ptr->exit_node_bitmap) {
			step_offset = _step_hostname_to_inx(
					step_ptr, node_name);
			if ((step_offset < 0)
			||  bit_test(step_ptr->exit_node_bitmap,
					step_offset))
				continue;
			bit_set(step_ptr->exit_node_bitmap,
				step_offset);
		}
		rc++;
		debug2("partitial switch release for step %u.%u, "
			"epilog on %s", job_ptr->job_id,
			step_ptr->step_id, node_name);
		switch_g_job_step_part_comp(
			step_ptr->switch_job, node_name);
	}
	list_iterator_destroy (step_iterator);

	return rc;
}

static void
_suspend_job_step(struct job_record *job_ptr,
		struct step_record *step_ptr, time_t now)
{
	if ((job_ptr->suspend_time)
	&&  (job_ptr->suspend_time > step_ptr->start_time)) {
		step_ptr->pre_sus_time +=
			difftime(now, job_ptr->suspend_time);
	} else {
		step_ptr->pre_sus_time +=
			difftime(now, step_ptr->start_time);
	}

}

/* Update time stamps for job step suspend */
extern void
suspend_job_step(struct job_record *job_ptr)
{
	time_t now = time(NULL);
	ListIterator step_iterator;
	struct step_record *step_ptr;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		_suspend_job_step(job_ptr, step_ptr, now);
	}
	list_iterator_destroy (step_iterator);
}

static void
_resume_job_step(struct job_record *job_ptr,
		struct step_record *step_ptr, time_t now)
{
	if ((job_ptr->suspend_time) &&
	    (job_ptr->suspend_time < step_ptr->start_time)) {
		step_ptr->tot_sus_time +=
			difftime(now, step_ptr->start_time);
	} else {
		step_ptr->tot_sus_time +=
			difftime(now, job_ptr->suspend_time);
	}
}

/* Update time stamps for job step resume */
extern void
resume_job_step(struct job_record *job_ptr)
{
	time_t now = time(NULL);
	ListIterator step_iterator;
	struct step_record *step_ptr;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		_resume_job_step(job_ptr, step_ptr, now);
	}
	list_iterator_destroy (step_iterator);
}


/*
 * dump_job_step_state - dump the state of a specific job step to a buffer,
 *	load with load_step_state
 * IN job_ptr - pointer to job for which information is to be dumpped
 * IN step_ptr - pointer to job step for which information is to be dumpped
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern void dump_job_step_state(struct job_record *job_ptr,
				struct step_record *step_ptr, Buf buffer)
{
	pack32(step_ptr->step_id, buffer);
	pack16(step_ptr->cyclic_alloc, buffer);
	pack16(step_ptr->port, buffer);
	pack16(step_ptr->ckpt_interval, buffer);
	pack16(step_ptr->cpus_per_task, buffer);
	pack16(step_ptr->resv_port_cnt, buffer);
	pack16(step_ptr->state, buffer);
	pack16(step_ptr->start_protocol_ver, buffer);

	pack8(step_ptr->no_kill, buffer);

	pack32(step_ptr->cpu_count, buffer);
	pack32(step_ptr->pn_min_memory, buffer);
	pack32(step_ptr->exit_code, buffer);
	if (step_ptr->exit_code != NO_VAL) {
		uint16_t bit_cnt = 0;
		if (step_ptr->exit_node_bitmap)
			bit_cnt = bit_size(step_ptr->exit_node_bitmap);
		pack_bit_fmt(step_ptr->exit_node_bitmap, buffer);
		pack16(bit_cnt, buffer);
	}
	if (step_ptr->core_bitmap_job) {
		uint32_t core_size = bit_size(step_ptr->core_bitmap_job);
		pack32(core_size, buffer);
		pack_bit_fmt(step_ptr->core_bitmap_job, buffer);
	} else
		pack32((uint32_t) 0, buffer);
	pack32(step_ptr->time_limit, buffer);
	pack32(step_ptr->cpu_freq, buffer);

	pack_time(step_ptr->start_time, buffer);
	pack_time(step_ptr->pre_sus_time, buffer);
	pack_time(step_ptr->tot_sus_time, buffer);
	pack_time(step_ptr->ckpt_time, buffer);

	packstr(step_ptr->host,  buffer);
	packstr(step_ptr->resv_ports, buffer);
	packstr(step_ptr->name, buffer);
	packstr(step_ptr->network, buffer);
	packstr(step_ptr->ckpt_dir, buffer);

	packstr(step_ptr->gres, buffer);
	(void) gres_plugin_step_state_pack(step_ptr->gres_list, buffer,
					   job_ptr->job_id, step_ptr->step_id,
					   SLURM_PROTOCOL_VERSION);

	pack16(step_ptr->batch_step, buffer);
	if (!step_ptr->batch_step) {
		pack_slurm_step_layout(step_ptr->step_layout, buffer,
				       SLURM_PROTOCOL_VERSION);
		switch_g_pack_jobinfo(step_ptr->switch_job, buffer,
				      SLURM_PROTOCOL_VERSION);
	}
	checkpoint_pack_jobinfo(step_ptr->check_job, buffer,
				SLURM_PROTOCOL_VERSION);
	select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
				     SLURM_PROTOCOL_VERSION);

}

/*
 * Create a new job step from data in a buffer (as created by
 *	dump_job_step_state)
 * IN/OUT - job_ptr - point to a job for which the step is to be loaded.
 * IN/OUT buffer - location to get data from, pointers advanced
 */
extern int load_step_state(struct job_record *job_ptr, Buf buffer,
			   uint16_t protocol_version)
{
	struct step_record *step_ptr = NULL;
	uint8_t no_kill;
	uint16_t cyclic_alloc, port, batch_step, bit_cnt;
	uint16_t start_protocol_ver = SLURM_MIN_PROTOCOL_VERSION;
	uint16_t ckpt_interval, cpus_per_task, resv_port_cnt, state;
	uint32_t core_size, cpu_count, exit_code, pn_min_memory, name_len;
	uint32_t step_id, time_limit, cpu_freq;
	time_t start_time, pre_sus_time, tot_sus_time, ckpt_time;
	char *host = NULL, *ckpt_dir = NULL, *core_job = NULL;
	char *resv_ports = NULL, *name = NULL, *network = NULL;
	char *bit_fmt = NULL, *gres = NULL;
	switch_jobinfo_t *switch_tmp = NULL;
	check_jobinfo_t check_tmp = NULL;
	slurm_step_layout_t *step_layout = NULL;
	List gres_list = NULL;
	dynamic_plugin_data_t *select_jobinfo = NULL;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		safe_unpack32(&step_id, buffer);
		safe_unpack16(&cyclic_alloc, buffer);
		safe_unpack16(&port, buffer);
		safe_unpack16(&ckpt_interval, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpack16(&state, buffer);
		safe_unpack16(&start_protocol_ver, buffer);

		safe_unpack8(&no_kill, buffer);

		safe_unpack32(&cpu_count, buffer);
		safe_unpack32(&pn_min_memory, buffer);
		safe_unpack32(&exit_code, buffer);
		if (exit_code != NO_VAL) {
			safe_unpackstr_xmalloc(&bit_fmt, &name_len, buffer);
			safe_unpack16(&bit_cnt, buffer);
		}
		safe_unpack32(&core_size, buffer);
		if (core_size)
			safe_unpackstr_xmalloc(&core_job, &name_len, buffer);
		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&cpu_freq, buffer);

		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);
		safe_unpack_time(&ckpt_time, buffer);

		safe_unpackstr_xmalloc(&host, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_ports, &name_len, buffer);
		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&ckpt_dir, &name_len, buffer);

		safe_unpackstr_xmalloc(&gres, &name_len, buffer);
		if (gres_plugin_step_state_unpack(&gres_list, buffer,
						  job_ptr->job_id, step_id,
						  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&batch_step, buffer);
		if (!batch_step) {
			if (unpack_slurm_step_layout(&step_layout, buffer,
						     protocol_version))
				goto unpack_error;
			switch_g_alloc_jobinfo(&switch_tmp,
					       job_ptr->job_id, step_id);
			if (switch_g_unpack_jobinfo(switch_tmp, buffer,
						    protocol_version))
				goto unpack_error;
		}
		checkpoint_alloc_jobinfo(&check_tmp);
		if (checkpoint_unpack_jobinfo(check_tmp, buffer,
					      protocol_version))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&step_id, buffer);
		safe_unpack16(&cyclic_alloc, buffer);
		safe_unpack16(&port, buffer);
		safe_unpack16(&ckpt_interval, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpack16(&state, buffer);

		safe_unpack8(&no_kill, buffer);

		safe_unpack32(&cpu_count, buffer);
		safe_unpack32(&pn_min_memory, buffer);
		safe_unpack32(&exit_code, buffer);
		if (exit_code != NO_VAL) {
			safe_unpackstr_xmalloc(&bit_fmt, &name_len, buffer);
			safe_unpack16(&bit_cnt, buffer);
		}
		safe_unpack32(&core_size, buffer);
		if (core_size)
			safe_unpackstr_xmalloc(&core_job, &name_len, buffer);
		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&cpu_freq, buffer);

		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);
		safe_unpack_time(&ckpt_time, buffer);

		safe_unpackstr_xmalloc(&host, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_ports, &name_len, buffer);
		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&ckpt_dir, &name_len, buffer);

		safe_unpackstr_xmalloc(&gres, &name_len, buffer);
		if (gres_plugin_step_state_unpack(&gres_list, buffer,
						  job_ptr->job_id, step_id,
						  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&batch_step, buffer);
		if (!batch_step) {
			if (unpack_slurm_step_layout(&step_layout, buffer,
						     protocol_version))
				goto unpack_error;
			switch_g_alloc_jobinfo(&switch_tmp,
					       job_ptr->job_id, step_id);
			if (switch_g_unpack_jobinfo(switch_tmp, buffer,
						    protocol_version))
				goto unpack_error;
		}
		checkpoint_alloc_jobinfo(&check_tmp);
		if (checkpoint_unpack_jobinfo(check_tmp, buffer,
					      protocol_version))
			goto unpack_error;

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
	} else {
		error("load_step_state: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	/* validity test as possible */
	if (cyclic_alloc > 1) {
		error("Invalid data for job %u.%u: cyclic_alloc=%u",
		      job_ptr->job_id, step_id, cyclic_alloc);
		goto unpack_error;
	}
	if (no_kill > 1) {
		error("Invalid data for job %u.%u: no_kill=%u",
		      job_ptr->job_id, step_id, no_kill);
		goto unpack_error;
	}

	step_ptr = find_step_record(job_ptr, step_id);
	if (step_ptr == NULL)
		step_ptr = _create_step_record(job_ptr);
	if (step_ptr == NULL)
		goto unpack_error;

	/* set new values */
	step_ptr->step_id      = step_id;
	step_ptr->cpu_count    = cpu_count;
	step_ptr->cpus_per_task= cpus_per_task;
	step_ptr->cyclic_alloc = cyclic_alloc;
	step_ptr->resv_port_cnt= resv_port_cnt;
	step_ptr->resv_ports   = resv_ports;
	step_ptr->name         = name;
	step_ptr->network      = network;
	step_ptr->no_kill      = no_kill;
	step_ptr->ckpt_dir     = ckpt_dir;
	step_ptr->gres         = gres;
	step_ptr->gres_list    = gres_list;
	step_ptr->port         = port;
	step_ptr->ckpt_interval= ckpt_interval;
	step_ptr->pn_min_memory= pn_min_memory;
	step_ptr->host         = host;
	host                   = NULL;  /* re-used, nothing left to free */
	step_ptr->batch_step   = batch_step;
	step_ptr->start_time   = start_time;
	step_ptr->time_limit   = time_limit;
	step_ptr->pre_sus_time = pre_sus_time;
	step_ptr->tot_sus_time = tot_sus_time;
	step_ptr->ckpt_time    = ckpt_time;

	if (!select_jobinfo)
		select_jobinfo = select_g_select_jobinfo_alloc();
	step_ptr->select_jobinfo = select_jobinfo;
	select_jobinfo = NULL;

	slurm_step_layout_destroy(step_ptr->step_layout);
	step_ptr->step_layout  = step_layout;

	step_ptr->switch_job   = switch_tmp;
	step_ptr->check_job    = check_tmp;
	step_ptr->cpu_freq     = cpu_freq;
	step_ptr->state        = state;
	step_ptr->start_protocol_ver = start_protocol_ver;

	if (!step_ptr->ext_sensors)
		step_ptr->ext_sensors = ext_sensors_alloc();

	step_ptr->exit_code    = exit_code;
	if (bit_fmt) {
		/* NOTE: This is only recovered if a job step completion
		 * is actively in progress at step save time. Otherwise
		 * the bitmap is NULL. */
		step_ptr->exit_node_bitmap = bit_alloc(bit_cnt);
		if (bit_unfmt(step_ptr->exit_node_bitmap, bit_fmt)) {
			error("error recovering exit_node_bitmap from %s",
				bit_fmt);
		}
		xfree(bit_fmt);
	}
	if (core_size) {
		step_ptr->core_bitmap_job = bit_alloc(core_size);
		if (bit_unfmt(step_ptr->core_bitmap_job, core_job)) {
			error("error recovering core_bitmap_job from %s",
			      core_job);
		}
		xfree(core_job);
	}

	if (step_ptr->step_layout && step_ptr->step_layout->node_list) {
		switch_g_job_step_allocated(switch_tmp,
					    step_ptr->step_layout->node_list);
	} else {
		switch_g_job_step_allocated(switch_tmp, NULL);
	}
	info("recovered job step %u.%u", job_ptr->job_id, step_id);
	return SLURM_SUCCESS;

unpack_error:
	xfree(host);
	xfree(resv_ports);
	xfree(name);
	xfree(network);
	xfree(ckpt_dir);
	xfree(gres);
	if (gres_list)
		list_destroy(gres_list);
	xfree(bit_fmt);
	xfree(core_job);
	if (switch_tmp)
		switch_g_free_jobinfo(switch_tmp);
	slurm_step_layout_destroy(step_layout);
	select_g_select_jobinfo_free(select_jobinfo);
	return SLURM_FAILURE;
}

/* Perform periodic job step checkpoints (per user request) */
extern void step_checkpoint(void)
{
	static int ckpt_run = -1;
	time_t now = time(NULL), ckpt_due;
	ListIterator job_iterator;
	struct job_record *job_ptr;
	ListIterator step_iterator;
	struct step_record *step_ptr;
	time_t event_time;
	uint32_t error_code;
	char *error_msg;
	checkpoint_msg_t ckpt_req;

	/* Exit if "checkpoint/none" is configured */
	if (ckpt_run == -1) {
		char *ckpt_type = slurm_get_checkpoint_type();
		if (strcasecmp(ckpt_type, "checkpoint/none"))
			ckpt_run = 1;
		else
			ckpt_run = 0;
		xfree(ckpt_type);
	}
	if (ckpt_run == 0)
		return;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr))
			continue;
		if (job_ptr->batch_flag &&
		    (job_ptr->ckpt_interval != 0)) { /* periodic job ckpt */
			ckpt_due = job_ptr->ckpt_time +
				   (job_ptr->ckpt_interval * 60);
			if (ckpt_due > now)
				continue;
			/*
			 * DO NOT initiate a checkpoint request if the job is
			 * started just now, in case it is restarting from checkpoint.
			 */
			ckpt_due = job_ptr->start_time +
				   (job_ptr->ckpt_interval * 60);
			if (ckpt_due > now)
				continue;

			ckpt_req.op = CHECK_CREATE;
			ckpt_req.data = 0;
			ckpt_req.job_id = job_ptr->job_id;
			ckpt_req.step_id = SLURM_BATCH_SCRIPT;
			ckpt_req.image_dir = NULL;
			job_checkpoint(&ckpt_req, getuid(), -1,
				       (uint16_t)NO_VAL);
			job_ptr->ckpt_time = now;
			last_job_update = now;
			continue; /* ignore periodic step ckpt */
		}
		step_iterator = list_iterator_create (job_ptr->step_list);
		while ((step_ptr = (struct step_record *)
				list_next (step_iterator))) {
			char *image_dir = NULL;
			if (step_ptr->state != JOB_RUNNING)
				continue;
			if (step_ptr->ckpt_interval == 0)
				continue;
			ckpt_due = step_ptr->ckpt_time +
				   (step_ptr->ckpt_interval * 60);
			if (ckpt_due > now)
				continue;
			/*
			 * DO NOT initiate a checkpoint request if the step is
			 * started just now, in case it is restarting from
			 * checkpoint.
			 */
			ckpt_due = step_ptr->start_time +
				   (step_ptr->ckpt_interval * 60);
			if (ckpt_due > now)
				continue;

			step_ptr->ckpt_time = now;
			last_job_update = now;
			image_dir = xstrdup(step_ptr->ckpt_dir);
			xstrfmtcat(image_dir, "/%u.%u", job_ptr->job_id,
				   step_ptr->step_id);
			(void) checkpoint_op(job_ptr->job_id,
					     step_ptr->step_id,
					     step_ptr, CHECK_CREATE, 0,
					     image_dir, &event_time,
					     &error_code, &error_msg);
			xfree(image_dir);
		}
		list_iterator_destroy (step_iterator);
	}
	list_iterator_destroy(job_iterator);
}

static void _signal_step_timelimit(struct job_record *job_ptr,
				   struct step_record *step_ptr, time_t now)
{
#ifndef HAVE_FRONT_END
	int i;
#endif
	kill_job_msg_t *kill_step;
	agent_arg_t *agent_args = NULL;
	static int notify_srun = -1;

	if (notify_srun == -1) {
#if defined HAVE_BG_FILES && !defined HAVE_BG_L_P
		notify_srun = 1;
#else
		char *launch_type = slurm_get_launch_type();
		/* do this for all but slurm (poe, aprun, etc...) */
		if (strcmp(launch_type, "launch/slurm"))
			notify_srun = 1;
		else
			notify_srun = 0;
		xfree(launch_type);
#endif
	}

	step_ptr->state = JOB_TIMEOUT;

	if (notify_srun) {
		srun_step_timeout(step_ptr, now);
		return;
	}

	xassert(step_ptr);
	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_KILL_TIMELIMIT;
	agent_args->retry = 1;
	agent_args->hostlist = hostlist_create(NULL);
	kill_step = xmalloc(sizeof(kill_job_msg_t));
	kill_step->job_id    = job_ptr->job_id;
	kill_step->step_id   = step_ptr->step_id;
	kill_step->job_state = job_ptr->job_state;
	kill_step->job_uid   = job_ptr->user_id;
	kill_step->nodes     = xstrdup(job_ptr->nodes);
	kill_step->time      = now;
	kill_step->start_time = job_ptr->start_time;
	kill_step->select_jobinfo = select_g_select_jobinfo_copy(
			job_ptr->select_jobinfo);

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr)
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
	agent_args->node_count++;
#else
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i) == 0)
			continue;
		if (agent_args->protocol_version >
		    node_record_table_ptr[i].protocol_version)
			agent_args->protocol_version =
				node_record_table_ptr[i].protocol_version;
		hostlist_push_host(agent_args->hostlist,
			node_record_table_ptr[i].name);
		agent_args->node_count++;
	}
#endif

	if (agent_args->node_count == 0) {
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		if (kill_step->select_jobinfo) {
			select_g_select_jobinfo_free(
				kill_step->select_jobinfo);
		}
		xfree(kill_step);
		return;
	}

	agent_args->msg_args = kill_step;
	agent_queue_request(agent_args);
	return;
}

extern void
check_job_step_time_limit (struct job_record *job_ptr, time_t now)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	uint32_t job_run_mins = 0;

	xassert(job_ptr);

	if (job_ptr->job_state != JOB_RUNNING)
		return;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		if (step_ptr->time_limit == INFINITE ||
		    step_ptr->time_limit == NO_VAL)
			continue;
		job_run_mins = (uint32_t) (((now - step_ptr->start_time) -
				step_ptr->tot_sus_time) / 60);
		if (job_run_mins >= step_ptr->time_limit) {
			/* this step has timed out */
			info("check_job_step_time_limit: job %u step %u "
				"has timed out (%u)",
				job_ptr->job_id, step_ptr->step_id,
				step_ptr->time_limit);
			_signal_step_timelimit(job_ptr, step_ptr, now);
		}
	}
	list_iterator_destroy (step_iterator);
}

/* Return true if memory is a reserved resources, false otherwise */
static bool _is_mem_resv(void)
{
	static bool mem_resv_value  = false;
	static bool mem_resv_tested = false;

	if (!mem_resv_tested) {
		mem_resv_tested = true;
		slurm_ctl_conf_t *conf = slurm_conf_lock();
		if (conf->select_type_param & CR_MEMORY)
			mem_resv_value = true;
		slurm_conf_unlock();
	}

	return mem_resv_value;
}

/* Process job step update request from specified user,
 * RET - 0 or error code */
extern int update_step(step_update_request_msg_t *req, uid_t uid)
{
	struct job_record *job_ptr;
	struct step_record *step_ptr = NULL;
	ListIterator step_iterator;
	int mod_cnt = 0;
	bool new_step = 0;

	job_ptr = find_job_record(req->job_id);
	if (job_ptr == NULL) {
		error("update_step: invalid job id %u", req->job_id);
		return ESLURM_INVALID_JOB_ID;
	}
	if (req->jobacct) {
		if (!validate_slurm_user(uid)) {
			error("Security violation, STEP_UPDATE RPC "
			      "from uid %d", uid);
			return ESLURM_USER_ID_MISSING;
		}
		/* Need to create a temporary step record (using some other
		 * launch mechanism that didn't use srun to launch).  Don't use
		 * _create_step_record though since we don't want to push it on
		 * the job's step_list. */
		if (req->step_id == NO_VAL) {
			step_ptr = xmalloc(sizeof(struct step_record));
			step_ptr->job_ptr    = job_ptr;
			step_ptr->exit_code  = NO_VAL;
			step_ptr->time_limit = INFINITE;
			step_ptr->jobacct    = jobacctinfo_create(NULL);
			step_ptr->requid     = -1;
			step_ptr->step_node_bitmap =
				bit_copy(job_ptr->node_bitmap);
			req->step_id = step_ptr->step_id =
				job_ptr->next_step_id++;
			new_step = 1;
		} else {
			if (req->step_id >= job_ptr->next_step_id)
				return ESLURM_INVALID_JOB_ID;
			if (!(step_ptr
			      = find_step_record(job_ptr, req->step_id))) {
				/* If updating this after the fact we
				   need to remake the step so we can
				   send the updated parts to
				   accounting.
				*/
				step_ptr = xmalloc(sizeof(struct step_record));
				step_ptr->job_ptr    = job_ptr;
				step_ptr->jobacct    = jobacctinfo_create(NULL);
				step_ptr->requid     = -1;
				step_ptr->step_id    = req->step_id;
				new_step = 1;
			}
		}
	} else if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
		   !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						 job_ptr->account)) {
		error("Security violation, STEP_UPDATE RPC from uid %d", uid);
		return ESLURM_USER_ID_MISSING;
	}

	/* No need to limit step time limit as job time limit will kill
	 * any steps with any time limit */
	if (req->step_id == NO_VAL) {
		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = (struct step_record *)
				   list_next (step_iterator))) {
			if (step_ptr->state != JOB_RUNNING)
				continue;
			step_ptr->time_limit = req->time_limit;
			mod_cnt++;
			info("Updating step %u.%u time limit to %u",
			     req->job_id, step_ptr->step_id, req->time_limit);
		}
		list_iterator_destroy (step_iterator);
	} else {
		if (!step_ptr)
			step_ptr = find_step_record(job_ptr, req->step_id);

		if (!step_ptr)
			return ESLURM_INVALID_JOB_ID;

		if (req->jobacct) {
			jobacctinfo_aggregate(step_ptr->jobacct, req->jobacct);
			if (new_step) {
				step_ptr->start_time = req->start_time;
				step_ptr->name = xstrdup(req->name);
				jobacct_storage_g_step_start(
					acct_db_conn, step_ptr);
			} else if (!step_ptr->exit_node_bitmap) {
				/* If the exit_code is not NO_VAL then
				 * we need to initialize the node bitmap for
				 * exited nodes for packing. */
				int nodes = bit_set_count(
					step_ptr->step_node_bitmap);
				step_ptr->exit_node_bitmap = bit_alloc(nodes);
				if (!step_ptr->exit_node_bitmap)
					fatal("bit_alloc: %m");
			}
			step_ptr->exit_code = req->exit_code;

			jobacct_storage_g_step_complete(acct_db_conn, step_ptr);

			if (new_step) {
				/* This was a temporary step record, never
				 * linked to the job, so there is no need to
				 * check SELECT_JOBDATA_CLEANING. */
				_free_step_rec(step_ptr);
			}
			mod_cnt++;
			info("Updating step %u.%u jobacct info",
			     req->job_id, req->step_id);
		} else {
			step_ptr->time_limit = req->time_limit;
			mod_cnt++;
			info("Updating step %u.%u time limit to %u",
			     req->job_id, req->step_id, req->time_limit);
		}
	}
	if (mod_cnt)
		last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

/* Return the total core count on a given node index */
static int _get_node_cores(int node_inx)
{
	struct node_record *node_ptr;
	int socks, cores;

	node_ptr = node_record_table_ptr + node_inx;
	if (slurmctld_conf.fast_schedule) {
		socks = node_ptr->config_ptr->sockets;
		cores = node_ptr->config_ptr->cores;
	} else {
		socks = node_ptr->sockets;
		cores = node_ptr->cores;
	}
	return socks * cores;
}

/*
 * Rebuild a job step's core_bitmap_job after a job has just changed size
 * job_ptr IN - job that was just re-sized
 * orig_job_node_bitmap IN - The job's original node bitmap
 */
extern void rebuild_step_bitmaps(struct job_record *job_ptr,
				 bitstr_t *orig_job_node_bitmap)
{
	struct step_record *step_ptr;
	ListIterator step_iterator;
	bitstr_t *orig_step_core_bitmap;
	int i, j, i_first, i_last, i_size;
	int old_core_offset, new_core_offset, node_core_count;
	bool old_node_set, new_node_set;

	if (job_ptr->step_list == NULL)
		return;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *)
			   list_next (step_iterator))) {
		if (step_ptr->state < JOB_RUNNING)
			continue;
		gres_plugin_step_state_rebase(step_ptr->gres_list,
					orig_job_node_bitmap,
					job_ptr->job_resrcs->node_bitmap);
		if (step_ptr->core_bitmap_job == NULL)
			continue;
		orig_step_core_bitmap = step_ptr->core_bitmap_job;
		i_size = bit_size(job_ptr->job_resrcs->core_bitmap);
		step_ptr->core_bitmap_job = bit_alloc(i_size);
		old_core_offset = 0;
		new_core_offset = 0;
		i_first = MIN(bit_ffs(orig_job_node_bitmap),
			      bit_ffs(job_ptr->job_resrcs->node_bitmap));
		i_last  = MAX(bit_fls(orig_job_node_bitmap),
			      bit_fls(job_ptr->job_resrcs->node_bitmap));
		for (i = i_first; i <= i_last; i++) {
			old_node_set = bit_test(orig_job_node_bitmap, i);
			new_node_set = bit_test(job_ptr->job_resrcs->
						node_bitmap, i);
			if (!old_node_set && !new_node_set)
				continue;
			node_core_count = _get_node_cores(i);
			if (old_node_set && new_node_set) {
				for (j = 0; j < node_core_count; j++) {
					if (!bit_test(orig_step_core_bitmap,
						      old_core_offset + j))
						continue;
					bit_set(step_ptr->core_bitmap_job,
						new_core_offset + j);
					bit_set(job_ptr->job_resrcs->
						core_bitmap_used,
						new_core_offset + j);
				}
			}
			if (old_node_set)
				old_core_offset += node_core_count;
			if (new_node_set)
				new_core_offset += node_core_count;
		}
		bit_free(orig_step_core_bitmap);
	}
	list_iterator_destroy (step_iterator);
}

extern int post_job_step(struct step_record *step_ptr)
{
	struct job_record *job_ptr = step_ptr->job_ptr;
	int error_code;

	_step_dealloc_lps(step_ptr);
	gres_plugin_step_dealloc(step_ptr->gres_list,
				 job_ptr->gres_list, job_ptr->job_id,
				 step_ptr->step_id);

	last_job_update = time(NULL);
	step_ptr->state = JOB_COMPLETE;

	error_code = delete_step_record(job_ptr, step_ptr->step_id);
	if (error_code == ENOENT) {
		info("remove_job_step step %u.%u not found", job_ptr->job_id,
		     step_ptr->step_id);
		return ESLURM_ALREADY_DONE;
	}
	_wake_pending_steps(job_ptr);

	return SLURM_SUCCESS;
}
