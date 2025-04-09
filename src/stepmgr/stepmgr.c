/*****************************************************************************\
 *  stepmgr.c - manage the job step information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "stepmgr.h"

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/forward.h"
#include "src/common/node_features.h"
#include "src/common/port_mgr.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_protocol_socket.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/mcs.h"
#include "src/interfaces/select.h"
#include "src/interfaces/switch.h"

#include "src/stepmgr/gres_stepmgr.h"
#include "src/stepmgr/srun_comm.h"

typedef struct {
	uint16_t flags;
	bool found;
	int rc_in;
	uint16_t signal;
	slurm_step_id_t step_id;
	uid_t uid;
} step_signal_t;

typedef struct {
	bitstr_t *all_gres_core_bitmap;
	bitstr_t *any_gres_core_bitmap;
	int core_end_bit;
	int core_start_bit;
	int job_node_inx;
	list_t *node_gres_list;
} foreach_gres_filter_t;

static void _build_pending_step(job_record_t *job_ptr,
				job_step_create_request_msg_t *step_specs);
static int _step_partial_comp(step_record_t *step_ptr,
			      step_complete_msg_t *req, bool finish,
			      int *rem, uint32_t *max_rc);
static int  _count_cpus(job_record_t *job_ptr, bitstr_t *bitmap,
			uint32_t *usable_cpu_cnt);
static void _dump_step_layout(step_record_t *step_ptr);
static bool _is_mem_resv(void);
static int  _opt_cpu_cnt(uint32_t step_min_cpus, bitstr_t *node_bitmap,
			 uint32_t *usable_cpu_cnt);
static int  _opt_node_cnt(uint32_t step_min_nodes, uint32_t step_max_nodes,
			  int nodes_avail, int nodes_picked_cnt);
static bitstr_t *_pick_step_nodes(job_record_t *job_ptr,
				  job_step_create_request_msg_t *step_spec,
				  list_t *step_gres_list, int cpus_per_task,
				  uint32_t node_count,
				  dynamic_plugin_data_t *select_jobinfo,
				  int *return_code);
static bitstr_t *_pick_step_nodes_cpus(job_record_t *job_ptr,
				       bitstr_t *nodes_bitmap, int node_cnt,
				       int cpu_cnt, uint32_t *usable_cpu_cnt);
static void _step_dealloc_lps(step_record_t *step_ptr);
static step_record_t *_build_interactive_step(
	job_record_t *job_ptr_in,
	job_step_create_request_msg_t *step_specs,
	uint16_t protocol_version);
static int _build_ext_launcher_step(step_record_t **new_step_record,
				    job_record_t *job_ptr,
				    job_step_create_request_msg_t *step_specs,
				    uint16_t protocol_version);
static void _wake_pending_steps(job_record_t *job_ptr);

stepmgr_ops_t *stepmgr_ops = NULL;

extern void stepmgr_init(stepmgr_ops_t *ops)
{
	/*
	 * Just keep pointers so that the pointers can be assigned after being
	 * intialized init is called.
	 */
	stepmgr_ops = ops;
}

/* Determine how many more CPUs are required for a job step */
static int  _opt_cpu_cnt(uint32_t step_min_cpus, bitstr_t *node_bitmap,
			 uint32_t *usable_cpu_cnt)
{
	int rem_cpus = step_min_cpus;

	if (!node_bitmap)
		return rem_cpus;
	xassert(usable_cpu_cnt);

	for (int i = 0; next_node_bitmap(node_bitmap, &i); i++) {
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

/* Purge any duplicate job steps for this PID */
static int _purge_duplicate_steps(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	job_step_create_request_msg_t *step_specs =
		(job_step_create_request_msg_t *) arg;

	if ((step_ptr->step_id.step_id == SLURM_PENDING_STEP) &&
	    (step_ptr->state == JOB_PENDING) &&
	    (step_ptr->srun_pid	== step_specs->srun_pid) &&
	    (!xstrcmp(step_ptr->host, step_specs->host))) {
		return 1;
	}

	/*
	 * See if we have the same step id.  If we do check to see if we
	 * have the same step_het_comp or if the step's is NO_VAL,
	 * meaning this step is not a het step.
	 */
	if ((step_specs->step_id.step_id == step_ptr->step_id.step_id) &&
	    ((step_specs->step_id.step_het_comp ==
	      step_ptr->step_id.step_het_comp) ||
	     (step_ptr->step_id.step_het_comp == NO_VAL)))
		return -1;

	return 0;
}

/* The step with a state of PENDING is used as a placeholder for a host and
 * port that can be used to wake a pending srun as soon another step ends */
static void _build_pending_step(job_record_t *job_ptr,
				job_step_create_request_msg_t *step_specs)
{
	step_record_t *step_ptr;

	if ((step_specs->host == NULL) || (step_specs->port == 0))
		return;

	step_ptr = create_step_record(job_ptr, 0);
	if (step_ptr == NULL)
		return;

	*stepmgr_ops->last_job_update = time(NULL);

	step_ptr->cpu_count	= step_specs->num_tasks;
	step_ptr->port		= step_specs->port;
	step_ptr->srun_pid	= step_specs->srun_pid;
	step_ptr->host		= xstrdup(step_specs->host);
	step_ptr->state		= JOB_PENDING;
	step_ptr->step_id.job_id = job_ptr->job_id;
	step_ptr->step_id.step_id = SLURM_PENDING_STEP;
	step_ptr->step_id.step_het_comp = NO_VAL;
	step_ptr->submit_line = xstrdup(step_specs->submit_line);

	if (job_ptr->node_bitmap)
		step_ptr->step_node_bitmap = bit_copy(job_ptr->node_bitmap);
	step_ptr->time_last_active = time(NULL);
}

static void _internal_step_complete(step_record_t *step_ptr, int remaining)
{
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	job_record_t *job_ptr = step_ptr->job_ptr;
	bool add_energy = true;

	if ((slurm_conf.prolog_flags & PROLOG_FLAG_CONTAIN) &&
	    (step_ptr->step_id.step_id != SLURM_EXTERN_CONT))
		add_energy = false;

	if (add_energy && jobacct && job_ptr->tres_alloc_cnt &&
	    (jobacct->energy.consumed_energy != NO_VAL64)) {
		if (job_ptr->tres_alloc_cnt[TRES_ARRAY_ENERGY] == NO_VAL64)
			job_ptr->tres_alloc_cnt[TRES_ARRAY_ENERGY] = 0;
		job_ptr->tres_alloc_cnt[TRES_ARRAY_ENERGY] +=
			jobacct->energy.consumed_energy;
	}

	if (IS_JOB_FINISHED(job_ptr) &&
	    job_ptr->tres_alloc_cnt &&
	    (job_ptr->tres_alloc_cnt[TRES_ENERGY] != NO_VAL64) &&
	    (remaining == 1)) {
		assoc_mgr_set_job_tres_alloc_str(job_ptr, false);
		/* This flag says we have processed the tres alloc including
		 * energy from all steps, so don't process or handle it again
		 * with the job.  It also tells the slurmdbd plugin to send it
		 * to the DBD.
		 */
		job_ptr->bit_flags |= TRES_STR_CALC;
	}

	jobacct_storage_g_step_complete(stepmgr_ops->acct_db_conn, step_ptr);

	if (step_ptr->step_id.step_id == SLURM_PENDING_STEP)
		return;

	/*
	 * Derived exit code is the highest exit code of srun steps, so we
	 * exclude the batch and extern steps.
	 *
	 * Sync with _get_derived_ec_update_str() for setting derived_ec on the
	 * dbd side.
	 */
	if ((step_ptr->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step_ptr->step_id.step_id != SLURM_BATCH_SCRIPT) &&
	    ((step_ptr->exit_code == SIG_OOM) ||
	     (step_ptr->exit_code > job_ptr->derived_ec)))
		job_ptr->derived_ec = step_ptr->exit_code;

	step_ptr->state |= JOB_COMPLETING;
	select_g_step_finish(step_ptr, false);

	_step_dealloc_lps(step_ptr);

	/* Don't need to set state. Will be destroyed in next steps. */
	/* step_ptr->state = JOB_COMPLETE; */
}

static int _step_signal(void *object, void *arg)
{
	step_record_t *step_ptr = (step_record_t *)object;
	step_signal_t *step_signal = (step_signal_t *)arg;
	uint16_t signal;
	int rc;


	if (!(step_signal->flags & KILL_FULL_JOB) &&
	    !find_step_id(step_ptr, &step_signal->step_id))
		return SLURM_SUCCESS;

	step_signal->found = true;
	signal = step_signal->signal;

	/*
	 * If step_het_comp is NO_VAL means it is a non-het step, so return
	 * SLURM_ERROR to break out of the list_for_each.
	 */
	rc = (step_ptr->step_id.step_het_comp == NO_VAL) ?
		SLURM_ERROR : SLURM_SUCCESS;

	if (step_signal->flags & KILL_OOM)
		step_ptr->exit_code = SIG_OOM;
	if (step_signal->flags & KILL_NO_SIG_FAIL) {
		debug("%s: setting SSF_NO_SIG_FAIL for %pS",
		      __func__, step_ptr);
		step_ptr->flags |= SSF_NO_SIG_FAIL;
	}

	/*
	 * If SIG_NODE_FAIL codes through it means we had nodes failed
	 * so handle that in the select plugin and switch the signal
	 * to KILL afterwards.
	 */
	if (signal == SIG_NODE_FAIL) {
		if (step_signal->rc_in != SLURM_SUCCESS)
			return rc;
		signal = SIGKILL;
	}

	/* save user ID of the one who requested the job be cancelled */
	if (signal == SIGKILL) {
		step_ptr->requid = step_signal->uid;
		srun_step_complete(step_ptr);
	}

	signal_step_tasks(step_ptr, signal, REQUEST_SIGNAL_TASKS);

	return rc;
}

static int _step_not_cleaning(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	int *remaining = (int *) arg;

	if (step_ptr->step_id.step_id == SLURM_PENDING_STEP)
		srun_step_signal(step_ptr, 0);
	_internal_step_complete(step_ptr, *remaining);

	(*remaining)--;
	return 1;
}

/*
 * _finish_step_comp - Finish deallocating and delete a non-pending step.
 */
static int _finish_step_comp(void *x, void *args)
{
	int remaining;
	step_record_t *step_ptr = x;
	job_record_t *job_ptr = step_ptr->job_ptr;

	if (step_ptr->step_id.step_id == SLURM_PENDING_STEP)
		return 0;

	remaining = list_count(job_ptr->step_list);
	_internal_step_complete(step_ptr, remaining);
	delete_step_record(job_ptr, step_ptr);
	_wake_pending_steps(job_ptr);

	*stepmgr_ops->last_job_update = time(NULL);

	return 1;
}

/*
 * delete_step_records - Delete step record for specified job_ptr.
 * This function is called when a step fails to run to completion. For example,
 * when the job is killed due to reaching its time limit or allocated nodes
 * go DOWN.
 * IN job_ptr - pointer to job table entry to have step records removed
 */
extern void delete_step_records(job_record_t *job_ptr)
{
	int remaining;
	xassert(job_ptr);

	remaining = list_count(job_ptr->step_list);
	*stepmgr_ops->last_job_update = time(NULL);
	list_delete_all(job_ptr->step_list, _step_not_cleaning, &remaining);
}

/*
 * delete_step_record - delete record for job step for specified job_ptr
 *	and step_id
 * IN job_ptr - pointer to job table entry to have step record removed
 * IN step_ptr - pointer to step table entry of the desired job step
 */
void delete_step_record(job_record_t *job_ptr, step_record_t *step_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->step_list);
	xassert(step_ptr);

	*stepmgr_ops->last_job_update = time(NULL);
	list_delete_ptr(job_ptr->step_list, step_ptr);
}


/*
 * dump_step_desc - dump the incoming step initiate request message
 * IN step_spec - job step request specification from RPC
 */
void
dump_step_desc(job_step_create_request_msg_t *step_spec)
{
	uint64_t mem_value = step_spec->pn_min_memory;
	char *mem_type = "node";

	if (mem_value & MEM_PER_CPU) {
		mem_value &= (~MEM_PER_CPU);
		mem_type   = "cpu";
	}

	log_flag(CPU_FREQ, "StepDesc: user_id=%u JobId=%u cpu_freq_gov=%u cpu_freq_max=%u cpu_freq_min=%u",
		 step_spec->user_id, step_spec->step_id.job_id,
		 step_spec->cpu_freq_gov,
		 step_spec->cpu_freq_max, step_spec->cpu_freq_min);
	debug3("StepDesc: user_id=%u %ps node_count=%u-%u cpu_count=%u num_tasks=%u",
	       step_spec->user_id, &step_spec->step_id,
	       step_spec->min_nodes, step_spec->max_nodes,
	       step_spec->cpu_count, step_spec->num_tasks);
	debug3("   cpu_freq_gov=%u cpu_freq_max=%u cpu_freq_min=%u "
	       "relative=%u task_dist=0x%X plane=%u",
	       step_spec->cpu_freq_gov, step_spec->cpu_freq_max,
	       step_spec->cpu_freq_min, step_spec->relative,
	       step_spec->task_dist, step_spec->plane_size);
	debug3("   node_list=%s  constraints=%s",
	       step_spec->node_list, step_spec->features);
	debug3("   host=%s port=%u srun_pid=%u name=%s network=%s exclusive=%s",
	       step_spec->host, step_spec->port, step_spec->srun_pid,
	       step_spec->name, step_spec->network,
	       (step_spec->flags & SSF_EXCLUSIVE) ? "yes" : "no");
	debug3("   mem_per_%s=%"PRIu64" resv_port_cnt=%u immediate=%u no_kill=%s",
	       mem_type, mem_value, step_spec->resv_port_cnt,
	       step_spec->immediate,
	       (step_spec->flags & SSF_NO_KILL) ? "yes" : "no");
	debug3("   overcommit=%s time_limit=%u",
	       (step_spec->flags & SSF_OVERCOMMIT) ? "yes" : "no",
	       step_spec->time_limit);

	if (step_spec->cpus_per_tres)
		debug3("   CPUs_per_TRES=%s", step_spec->cpus_per_tres);
	if (step_spec->mem_per_tres)
		debug3("   Mem_per_TRES=%s", step_spec->mem_per_tres);
	if (step_spec->tres_bind)
		debug3("   TRES_bind=%s", step_spec->tres_bind);
	if (step_spec->tres_freq)
		debug3("   TRES_freq=%s", step_spec->tres_freq);
	if (step_spec->tres_per_step)
		debug3("   TRES_per_step=%s", step_spec->tres_per_step);
	if (step_spec->tres_per_node)
		debug3("   TRES_per_node=%s", step_spec->tres_per_node);
	if (step_spec->tres_per_socket)
		debug3("   TRES_per_socket=%s", step_spec->tres_per_socket);
	if (step_spec->tres_per_task)
		debug3("   TRES_per_task=%s", step_spec->tres_per_task);
	if (step_spec->container || step_spec->container_id)
		debug3("   Container=%s ContainerID=%s",
		       step_spec->container, step_spec->container_id);
}

/*
 * job_step_signal - signal the specified job step
 * IN step_id - filled in slurm_step_id_t
 * IN signal - user id of user issuing the RPC
 * IN flags - RPC flags
 * IN uid - user id of user issuing the RPC
 * RET 0 on success, otherwise ESLURM error code
 * global: job_list - pointer global job list
 *	last_job_update - time of last job table update
 */
extern int job_step_signal(slurm_step_id_t *step_id,
			   uint16_t signal, uint16_t flags, uid_t uid)
{
	job_record_t *job_ptr;
	step_signal_t step_signal = {
		.flags = flags,
		.found = false,
		.rc_in = SLURM_SUCCESS,
		.signal = signal,
		.uid = uid,
	};

	memcpy(&step_signal.step_id, step_id, sizeof(step_signal.step_id));

	job_ptr = stepmgr_ops->find_job_record(step_id->job_id);
	if (job_ptr == NULL) {
		error("job_step_signal: invalid JobId=%u", step_id->job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && !validate_slurm_user(uid)) {
		error("Security violation, JOB_CANCEL RPC from uid %u", uid);
		return ESLURM_USER_ID_MISSING;
	}

	if (IS_JOB_FINISHED(job_ptr)) {
		step_signal.rc_in = ESLURM_ALREADY_DONE;
		if (signal != SIG_NODE_FAIL)
			return step_signal.rc_in;
	} else if (!IS_JOB_RUNNING(job_ptr)) {
		verbose("%s: %pJ is in state %s, cannot signal steps",
			__func__, job_ptr,
			job_state_string(job_ptr->job_state));
		if (signal != SIG_NODE_FAIL)
			return ESLURM_TRANSITION_STATE_NO_UPDATE;
	}

	list_for_each(job_ptr->step_list, _step_signal, &step_signal);

	if (!step_signal.found && running_in_slurmctld() &&
	    (job_ptr->bit_flags & STEPMGR_ENABLED)) {
		agent_arg_t *agent_args = NULL;
		job_step_kill_msg_t *kill_msg = NULL;
		node_record_t *node_ptr;

		kill_msg = xmalloc(sizeof(*kill_msg));
		kill_msg->signal = signal;
		kill_msg->flags = flags;
		kill_msg->step_id = *step_id;

		agent_args = xmalloc(sizeof(agent_arg_t));
		agent_args->msg_type = REQUEST_CANCEL_JOB_STEP;
		agent_args->retry = 1;
		agent_args->hostlist = hostlist_create(job_ptr->batch_host);
		agent_args->node_count = 1;
		if ((node_ptr = find_node_record(job_ptr->batch_host)))
			agent_args->protocol_version =
				node_ptr->protocol_version;

		agent_args->msg_args = kill_msg;
		set_agent_arg_r_uid(agent_args, slurm_conf.slurmd_user_id);
		stepmgr_ops->agent_queue_request(agent_args);

		step_signal.found = true;
		step_signal.rc_in = SLURM_SUCCESS;
	}

	if (!step_signal.found) {
		info("%s: %pJ StepId=%u not found",
		     __func__, job_ptr, step_id->step_id);
		return ESLURM_INVALID_JOB_ID;
	}

	return step_signal.rc_in;
}

/*
 * signal_step_tasks - send specific signal to specific job step
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 * IN msg_type - message type to send
 */
void signal_step_tasks(step_record_t *step_ptr, uint16_t signal,
		       slurm_msg_type_t msg_type)
{
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
	static bool cloud_dns = false;
	static time_t last_update = 0;
#endif
	signal_tasks_msg_t *signal_tasks_msg;
	agent_arg_t *agent_args = NULL;

	xassert(step_ptr);
	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = msg_type;
	agent_args->retry    = 1;
	agent_args->hostlist = hostlist_create(NULL);
	signal_tasks_msg = xmalloc(sizeof(signal_tasks_msg_t));
	memcpy(&signal_tasks_msg->step_id, &step_ptr->step_id,
	       sizeof(signal_tasks_msg->step_id));
	signal_tasks_msg->signal      = signal;
	if (step_ptr->flags & SSF_NO_SIG_FAIL)
		signal_tasks_msg->flags |= KILL_NO_SIG_FAIL;

	log_flag(STEPS, "%s: queueing signal %d with flags=0x%x for %pS",
	      __func__, signal, signal_tasks_msg->flags, step_ptr);

#ifdef HAVE_FRONT_END
	xassert(step_ptr->job_ptr->batch_host);
	if (step_ptr->job_ptr->front_end_ptr)
		agent_args->protocol_version =
			step_ptr->job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, step_ptr->job_ptr->batch_host);
	agent_args->node_count = 1;
#else
        if (last_update != slurm_conf.last_update) {
                if (xstrcasestr(slurm_conf.slurmctld_params, "cloud_dns"))
                        cloud_dns = true;
                else
                        cloud_dns = false;
                last_update = slurm_conf.last_update;
        }

	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	for (int i = 0;
	     (node_ptr = next_node_bitmap(step_ptr->step_node_bitmap, &i));
	     i++) {
		if (agent_args->protocol_version > node_ptr->protocol_version)
			agent_args->protocol_version =
				node_ptr->protocol_version;
		hostlist_push_host(agent_args->hostlist, node_ptr->name);
		agent_args->node_count++;
		if (PACK_FANOUT_ADDRS(node_ptr))
			agent_args->msg_flags |= SLURM_PACK_ADDRS;
	}
#endif

	if (agent_args->node_count == 0) {
		xfree(signal_tasks_msg);
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = signal_tasks_msg;
	set_agent_arg_r_uid(agent_args, SLURM_AUTH_UID_ANY);
	stepmgr_ops->agent_queue_request(agent_args);
}

/*
 * signal_step_tasks_on_node - send specific signal to specific job step
 *                             on a specific node.
 * IN node_name - name of node on which to signal tasks
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 * IN msg_type - message type to send
 */
void signal_step_tasks_on_node(char* node_name, step_record_t *step_ptr,
			       uint16_t signal, slurm_msg_type_t msg_type)
{
	signal_tasks_msg_t *signal_tasks_msg;
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
	node_record_t *node_ptr;
	if ((node_ptr = find_node_record(node_name)))
		agent_args->protocol_version = node_ptr->protocol_version;
	agent_args->node_count++;
	agent_args->hostlist = hostlist_create(node_name);
	if (!agent_args->hostlist)
		fatal("Invalid node_name: %s", node_name);
#endif
	signal_tasks_msg = xmalloc(sizeof(signal_tasks_msg_t));
	memcpy(&signal_tasks_msg->step_id, &step_ptr->step_id,
	       sizeof(signal_tasks_msg->step_id));
	signal_tasks_msg->signal      = signal;
	agent_args->msg_args = signal_tasks_msg;
	set_agent_arg_r_uid(agent_args, SLURM_AUTH_UID_ANY);
	stepmgr_ops->agent_queue_request(agent_args);
}

typedef struct {
	int config_start_count;
	int start_count;
	time_t max_age;
} wake_steps_args_t;

static int _wake_steps(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	wake_steps_args_t *args = (wake_steps_args_t *) arg;

	if (step_ptr->state != JOB_PENDING)
		return 0;

	if ((args->start_count < args->config_start_count) ||
	    (step_ptr->time_last_active <= args->max_age)) {
		srun_step_signal(step_ptr, 0);
		args->start_count++;
		return 1;
	}

	return 0;
}

/* A step just completed, signal srun processes with pending steps to retry */
static void _wake_pending_steps(job_record_t *job_ptr)
{
	static int config_start_count = -1, config_max_age = -1;
	wake_steps_args_t args;

	if (!IS_JOB_RUNNING(job_ptr))
		return;

	if (!job_ptr->step_list)
		return;

	if (config_start_count == -1) {
		char *tmp_ptr;
		long int param;
		config_start_count = 8;
		config_max_age = 60;

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "step_retry_count="))) {
			param = strtol(tmp_ptr + 17, NULL, 10);
			if ((param >= 1) && (param != LONG_MIN) &&
			    (param != LONG_MAX))
				config_start_count = param;
		}
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "step_retry_time="))) {
			param = strtol(tmp_ptr + 16, NULL, 10);
			if ((param >= 1) && (param != LONG_MIN) &&
			    (param != LONG_MAX))
				config_max_age = param;
		}
	}
	args.max_age = time(NULL) - config_max_age;

	/* We do not know which steps can use currently available resources.
	 * Try to start a bit more based upon step sizes. Effectiveness
	 * varies with step sizes, constraints and order. */
	args.config_start_count = config_start_count;
	args.start_count = 0;
	list_delete_all(job_ptr->step_list, _wake_steps, &args);
}

/* Set cur_inx to the next round-robin node index */
static int _next_node_inx(int *cur_inx, int *check_cnt, int len, int node_cnt,
			  bitstr_t *nodes_bitmap, bitstr_t **picked_node_bitmap,
			  int start_inx)
{
	bool wrapped = false;
	xassert(cur_inx);
	xassert(check_cnt);
	xassert(nodes_bitmap);
	xassert(picked_node_bitmap);

	if (*check_cnt == 0) {
		*cur_inx = start_inx;
	} else {
		*cur_inx = (*cur_inx + 1) % len;
		wrapped = *cur_inx <= start_inx;
		if (*cur_inx == start_inx)
			return SLURM_ERROR; /* Normal break case */
	}

	if (*check_cnt >= node_cnt)
		return SLURM_ERROR; /* Normal break case */

	*cur_inx = bit_ffs_from_bit(nodes_bitmap, *cur_inx);

	if (wrapped && (*cur_inx >= start_inx))
		return SLURM_ERROR; /* Normal break case */

	if (*cur_inx < 0) {
		/* This should never happen */
		xassert(false);
		FREE_NULL_BITMAP(*picked_node_bitmap);
		return SLURM_ERROR;
	}

	(*check_cnt)++;
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
static bitstr_t *_pick_step_nodes_cpus(job_record_t *job_ptr,
				       bitstr_t *nodes_bitmap, int node_cnt,
				       int cpu_cnt, uint32_t *usable_cpu_cnt)
{
	bitstr_t *picked_node_bitmap = NULL;
	int *usable_cpu_array;
	int cpu_target;	/* Target number of CPUs per allocated node */
	int rem_nodes, rem_cpus, save_rem_nodes, save_rem_cpus;
	int i;
	int start_inx, bit_len, check_cnt;

	xassert(node_cnt > 0);
	xassert(nodes_bitmap);
	xassert(usable_cpu_cnt);

	picked_node_bitmap = bit_alloc(node_record_count);
	start_inx = job_ptr->job_resrcs->next_step_node_inx;
	bit_len = bit_fls(nodes_bitmap) + 1;
	if (start_inx >= bit_len)
		start_inx = 0;

	cpu_target = (cpu_cnt + node_cnt - 1) / node_cnt;
	if (cpu_target > 1024)
		info("%s: high cpu_target (%d)", __func__, cpu_target);
	if ((cpu_cnt <= node_cnt) || (cpu_target > 1024)) {
		check_cnt = 0;
		while (_next_node_inx(&i, &check_cnt, bit_len, node_cnt,
				      nodes_bitmap, &picked_node_bitmap,
				      start_inx) == SLURM_SUCCESS)
			bit_set(picked_node_bitmap, i);

		return picked_node_bitmap;
	}

	/* Need to satisfy both a node count and a cpu count */
	usable_cpu_array = xcalloc(cpu_target, sizeof(int));
	rem_nodes = node_cnt;
	rem_cpus  = cpu_cnt;
	check_cnt = 0;
	while (_next_node_inx(&i, &check_cnt, bit_len, bit_len, nodes_bitmap,
			      &picked_node_bitmap, start_inx) ==
	       SLURM_SUCCESS) {
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
			FREE_NULL_BITMAP(picked_node_bitmap);
			return NULL;
		}
	}

	if (!picked_node_bitmap) {
		xfree(usable_cpu_array);
		return NULL;
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
		FREE_NULL_BITMAP(picked_node_bitmap);
		return NULL;
	}
	rem_nodes = save_rem_nodes;
	rem_cpus  = save_rem_cpus;

	/* Pick nodes with CPU counts below original target */
	check_cnt = 0;
	while (_next_node_inx(&i, &check_cnt, bit_len, bit_len, nodes_bitmap,
			      &picked_node_bitmap, start_inx) ==
	       SLURM_SUCCESS) {
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
	FREE_NULL_BITMAP(picked_node_bitmap);
	return NULL;
}

static int _mark_busy_nodes(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	bitstr_t *busy = (bitstr_t *) arg;

	if (step_ptr->state < JOB_RUNNING)
		return 0;

	/*
	 * Don't consider the batch and extern steps when
	 * looking for "idle" nodes.
	 */
	if ((step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT) ||
	    (step_ptr->step_id.step_id == SLURM_EXTERN_CONT) ||
	    (step_ptr->step_id.step_id == SLURM_INTERACTIVE_STEP) ||
	    (step_ptr->flags & SSF_EXT_LAUNCHER))
		return 0;

	if (!step_ptr->step_node_bitmap) {
		error("%s: %pS has no step_node_bitmap",
		      __func__, step_ptr);
		return 0;
	}

	bit_or(busy, step_ptr->step_node_bitmap);

	if (slurm_conf.debug_flags & DEBUG_FLAG_STEPS) {
		char *temp;
		temp = bitmap2node_name(step_ptr->step_node_bitmap);
		log_flag(STEPS, "%s: %pS has nodes %s",
			 __func__, step_ptr, temp);
		xfree(temp);
	}

	return 0;
}

static void _step_test_gres(job_step_create_request_msg_t *step_spec,
			    gres_stepmgr_step_test_args_t *gres_test_args,
			    job_record_t *job_ptr,
			    uint32_t *node_usable_cpu_cnt,
			    uint32_t *total_cpus,
			    uint32_t *avail_cpus,
			    int *gres_invalid_nodes,
			    int *fail_mode)
{
	uint64_t gres_cpus;
	int err_code = SLURM_SUCCESS;

	gres_test_args->err_code = &err_code;

	/* ignore current step allocations */
	gres_test_args->ignore_alloc = true;
	gres_cpus = gres_stepmgr_step_test(gres_test_args);
	*total_cpus = MIN(*total_cpus, gres_cpus);

	/*
	 * consider current step allocations if
	 * not --overlap=force
	 */
	if (!(step_spec->flags & SSF_OVERLAP_FORCE)) {
		gres_test_args->ignore_alloc = false;
		gres_cpus = gres_stepmgr_step_test(gres_test_args);
	}
	if (gres_cpus < *avail_cpus) {
		log_flag(STEPS, "%s: %pJ Usable CPUs for GRES %"PRIu64" from %d previously available",
			 __func__, job_ptr, gres_cpus,
			 *avail_cpus);
		*avail_cpus = gres_cpus;
		*node_usable_cpu_cnt = *avail_cpus;
		if (err_code != SLURM_SUCCESS)
			*fail_mode = err_code;
		else
			*fail_mode = ESLURM_INVALID_GRES;
		if (*total_cpus == 0) {
			/*
			 * total_cpus == 0 is set from this:
			 *   MIN(*total_cpus, gres_cpus);
			 * This means that it is impossible to run this step on
			 * this node due to GRES.
			 */
			*gres_invalid_nodes = *gres_invalid_nodes + 1;
		}
	}
}

/* Returns threads_per_core required by the step or NO_VAL16 if not specified */
static uint16_t _get_threads_per_core(uint16_t step_threads_per_core,
				      job_record_t *job_ptr)
{
	uint16_t tpc = NO_VAL16;

	if (step_threads_per_core &&
	    (step_threads_per_core != NO_VAL16)) {
		tpc = step_threads_per_core;
	} else if (job_ptr->details->mc_ptr->threads_per_core &&
		   (job_ptr->details->mc_ptr->threads_per_core != NO_VAL16))
		tpc = job_ptr->details->mc_ptr->threads_per_core;
	return tpc;
}

static int _cmp_cpu_counts(const void *num1, const void *num2) {
	uint16_t cpu1 = *(uint16_t *) num1;
	uint16_t cpu2 = *(uint16_t *) num2;

	if (cpu1 > cpu2)
		return -1;
	else if (cpu1 < cpu2)
		return 1;
	return 0;
}

static void _set_max_num_tasks(job_step_create_request_msg_t *step_spec,
			       job_record_t *job_ptr,
			       bitstr_t *node_bitmap,
			       int cpus_per_task)
{
	int j = 0;
	int k = 0;
	uint32_t avail_cnt, num_nodes;
	uint16_t *cpus;
	uint32_t num_tasks = 0;
	uint16_t tpc = _get_threads_per_core(step_spec->threads_per_core,
					     job_ptr);

	xassert(node_bitmap);
	xassert(cpus_per_task);

	avail_cnt = bit_set_count(node_bitmap);
	num_nodes = MIN(avail_cnt, step_spec->max_nodes);
	cpus = xcalloc(avail_cnt, sizeof(*cpus));
	for (int i = 0; i < job_ptr->job_resrcs->nhosts; i++) {
		j = bit_ffs_from_bit(job_ptr->job_resrcs->node_bitmap, j);
		if (j < 0)
			break;
		if (!bit_test(node_bitmap, j)) {
			j++;
			continue;
		}

		if (tpc != NO_VAL16) {
			cpus[k] = ROUNDUP(job_ptr->job_resrcs->cpus[i],
					  node_record_table_ptr[j]->tpc);
			cpus[k] *= tpc;
		} else
			cpus[k] = job_ptr->job_resrcs->cpus[i];

		j++;
		k++;
	}

	if (num_nodes < avail_cnt)
		qsort(cpus, avail_cnt, sizeof(*cpus), _cmp_cpu_counts);

	for (int i = 0; i < num_nodes; i++) {
		num_tasks += cpus[i] / cpus_per_task;
	}
	step_spec->num_tasks = num_tasks;
	step_spec->cpu_count = num_tasks * cpus_per_task;

	xfree(cpus);
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
static bitstr_t *_pick_step_nodes(job_record_t *job_ptr,
				  job_step_create_request_msg_t *step_spec,
				  list_t *step_gres_list, int cpus_per_task,
				  uint32_t node_count,
				  dynamic_plugin_data_t *select_jobinfo,
				  int *return_code)
{
	node_record_t *node_ptr;
	bitstr_t *nodes_avail = NULL, *nodes_idle = NULL;
	bitstr_t *select_nodes_avail = NULL;
	bitstr_t *nodes_picked = NULL, *node_tmp = NULL;
	int error_code, nodes_picked_cnt = 0, cpus_picked_cnt = 0;
	int cpu_cnt, i;
	int mem_blocked_nodes = 0, mem_blocked_cpus = 0;
	int job_blocked_nodes = 0, job_blocked_cpus = 0;
	int gres_invalid_nodes = 0;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	uint32_t *usable_cpu_cnt = NULL;
	gres_stepmgr_step_test_args_t gres_test_args = {
		.cpus_per_task = cpus_per_task,
		.first_step_node = true,
		.job_gres_list = job_ptr->gres_list_alloc,
		.job_id = job_ptr->job_id,
		.job_resrcs_ptr = job_resrcs_ptr,
		.max_rem_nodes = step_spec->max_nodes,
		.step_gres_list = step_gres_list,
		.step_id = NO_VAL,
		.test_mem = false,
	};

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

	/*
	 * If we have a select plugin that selects step resources, then use it
	 * and return (does not happen today). Otherwise select step resources
	 * in this function.
	 */
	if ((nodes_picked = select_g_step_pick_nodes(job_ptr, select_jobinfo,
						     node_count,
						     &select_nodes_avail))) {
		job_resrcs_ptr->next_step_node_inx = bit_fls(nodes_picked) + 1;
		return nodes_picked;
	}

	if (!nodes_avail)
		nodes_avail = bit_copy (job_ptr->node_bitmap);
	bit_and(nodes_avail, stepmgr_ops->up_node_bitmap);

	if (step_spec->exc_nodes) {
		bitstr_t *exc_bitmap = NULL;
		error_code = node_name2bitmap(step_spec->exc_nodes, false,
					      &exc_bitmap, NULL);
		if (error_code) {
			*return_code = ESLURM_INVALID_NODE_NAME;
			FREE_NULL_BITMAP(exc_bitmap);
			goto cleanup;
		}
		bit_and_not(nodes_avail, exc_bitmap);

		if (step_spec->node_list) {
			bitstr_t *req_nodes = NULL;
			error_code = node_name2bitmap(
				step_spec->node_list, false,
				&req_nodes, NULL);
			if (error_code) {
				info("%s: invalid requested node list %s",
				     __func__,
				     step_spec->node_list);
				FREE_NULL_BITMAP(exc_bitmap);
				FREE_NULL_BITMAP(req_nodes);
				goto cleanup;
			}
			if (bit_overlap_any(req_nodes, exc_bitmap)) {
				info("%s: %ps requested nodes %s is also excluded %s",
				     __func__, &step_spec->step_id,
				     step_spec->node_list,
				     step_spec->exc_nodes);
				FREE_NULL_BITMAP(exc_bitmap);
				FREE_NULL_BITMAP(req_nodes);
				goto cleanup;
			}
			FREE_NULL_BITMAP(req_nodes);
		}
		FREE_NULL_BITMAP(exc_bitmap);
	}

	if (step_spec->features &&
	    (!job_ptr->details ||
	     xstrcmp(step_spec->features, job_ptr->details->features_use))) {
		/*
		 * We only select for a single feature name here.
		 * Ignore step features if equal to job features.
		 * FIXME: Add support for AND, OR, etc. here if desired
		 */
		node_feature_t *feat_ptr;
		feat_ptr =
			list_find_first(active_feature_list, list_find_feature,
					(void *) step_spec->features);
		if (feat_ptr && feat_ptr->node_bitmap)
			bit_and(nodes_avail, feat_ptr->node_bitmap);
		else {
			bit_clear_all(nodes_avail);
			*return_code = ESLURM_INVALID_FEATURE;
			goto cleanup;
		}
	}

	if (step_spec->pn_min_memory &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("%s: job lacks memory allocation details to enforce memory limits for %pJ",
		      __func__, job_ptr);
		step_spec->pn_min_memory = 0;
	} else if (step_spec->pn_min_memory == MEM_PER_CPU)
		step_spec->pn_min_memory = 0;	/* clear MEM_PER_CPU flag */

	if (job_ptr->next_step_id == 0) {
		for (int i = 0;
		     (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
		     i++) {
			if (IS_NODE_POWERED_DOWN(node_ptr) ||
			    IS_NODE_FUTURE(node_ptr) ||
			    IS_NODE_NO_RESPOND(node_ptr)) {
				/*
				 * Node is/was powered down. Need to wait
				 * for it to start responding again.
				 */
				FREE_NULL_BITMAP(nodes_avail);
				FREE_NULL_BITMAP(select_nodes_avail);
				*return_code = ESLURM_NODES_BUSY;
				return NULL;
			}
		}
		if (IS_JOB_CONFIGURING(job_ptr)) {
			info("%s: Configuration for %pJ is complete",
			     __func__, job_ptr);
			stepmgr_ops->job_config_fini(job_ptr);
		}
	}

	if (_is_mem_resv() && step_spec->pn_min_memory &&
	    ((step_spec->pn_min_memory & MEM_PER_CPU) == 0) &&
	    job_ptr->details && job_ptr->details->pn_min_memory &&
	    ((job_ptr->details->pn_min_memory & MEM_PER_CPU) == 0) &&
	    (step_spec->pn_min_memory >
	     job_ptr->details->pn_min_memory)) {
		FREE_NULL_BITMAP(nodes_avail);
		FREE_NULL_BITMAP(select_nodes_avail);
		*return_code = ESLURM_INVALID_TASK_MEMORY;
		return NULL;
	}

	usable_cpu_cnt = xcalloc(node_record_count, sizeof(uint32_t));
	for (int i = 0, node_inx = -1;
	     (node_ptr = next_node_bitmap(job_resrcs_ptr->node_bitmap, &i));
	     i++) {
		node_inx++;
		if (!bit_test(nodes_avail, i))
			continue;	/* node now DOWN */

		usable_cpu_cnt[i] = job_resrcs_ptr->cpus[node_inx];

		log_flag(STEPS, "%s: %pJ Currently running steps use %d of allocated %d CPUs on node %s",
			 __func__, job_ptr,
			 job_resrcs_ptr->cpus_used[node_inx],
			 usable_cpu_cnt[i], node_record_table_ptr[i]->name);

		/*
		 * Don't do this test if --overlap=force or
		 * --external-launcher
		 */
		if ((!(step_spec->flags & SSF_OVERLAP_FORCE)) &&
		    (!(step_spec->flags & SSF_EXT_LAUNCHER))) {
			/*
			 * If whole is given and
			 * job_resrcs_ptr->cpus_used[node_inx]
			 * we can't use this node.
			 */
			if ((step_spec->flags & SSF_WHOLE) &&
			    job_resrcs_ptr->cpus_used[node_inx]) {
				log_flag(STEPS, "%s: %pJ Node requested --whole node while other step running here.",
					 __func__, job_ptr);
				job_blocked_cpus +=
					job_resrcs_ptr->cpus_used[node_inx];
				job_blocked_nodes++;
				usable_cpu_cnt[i] = 0;
			} else {
				usable_cpu_cnt[i] -=
					job_resrcs_ptr->cpus_used[node_inx];
				job_blocked_cpus +=
					job_resrcs_ptr->cpus_used[node_inx];
				if (!usable_cpu_cnt[i]) {
					job_blocked_nodes++;
					log_flag(STEPS, "%s: %pJ Skipping node %s. Not enough CPUs to run step here.",
						 __func__,
						 job_ptr,
						 node_record_table_ptr[i]->name);
				}
			}
		}

		if (!usable_cpu_cnt[i]) {
			bit_clear(nodes_avail, i);
			continue;
		}

		if ((step_spec->pn_min_memory && _is_mem_resv()) ||
		    step_gres_list) {
			int fail_mode = ESLURM_NODES_BUSY;
			uint64_t tmp_mem;
			uint32_t tmp_cpus, avail_cpus, total_cpus;
			uint32_t avail_tasks, total_tasks;

			gres_test_args.node_offset = node_inx;
			gres_test_args.test_mem = false;

			avail_cpus = total_cpus = usable_cpu_cnt[i];;
			if (_is_mem_resv() &&
			    step_spec->pn_min_memory & MEM_PER_CPU) {
				uint64_t mem_use = step_spec->pn_min_memory;
				mem_use &= (~MEM_PER_CPU);
				/* ignore current step allocations */
				tmp_mem    = job_resrcs_ptr->
					memory_allocated[node_inx];
				tmp_cpus   = tmp_mem / mem_use;
				total_cpus = MIN(total_cpus, tmp_cpus);
				/*
				 * consider current step allocations if
				 * not --overlap=force
				 */
				if (!(step_spec->flags & SSF_OVERLAP_FORCE)) {
					tmp_mem   -= job_resrcs_ptr->
						memory_used[node_inx];
					tmp_cpus   = tmp_mem / mem_use;
				}
				if (tmp_cpus < avail_cpus) {
					avail_cpus = tmp_cpus;
					usable_cpu_cnt[i] = avail_cpus;
					fail_mode = ESLURM_INVALID_TASK_MEMORY;
				}
				log_flag(STEPS, "%s: %pJ Based on --mem-per-cpu=%"PRIu64" we have %d/%d usable of available cpus on node %s, usable memory was: %"PRIu64,
					 __func__, job_ptr, mem_use, tmp_cpus,
					 avail_cpus, node_ptr->name, tmp_mem);
			} else if (_is_mem_resv() && step_spec->pn_min_memory) {
				uint64_t mem_use = step_spec->pn_min_memory;
				/* ignore current step allocations */
				tmp_mem    = job_resrcs_ptr->
					memory_allocated[node_inx];
				if (tmp_mem < mem_use)
					total_cpus = 0;
				/*
				 * consider current step allocations if
				 * not --overlap=force
				 */
				if (!(step_spec->flags & SSF_OVERLAP_FORCE)) {
					tmp_mem   -= job_resrcs_ptr->
						memory_used[node_inx];
				}
				if ((tmp_mem < mem_use) && (avail_cpus > 0)) {
					log_flag(STEPS, "%s: %pJ Usable memory on node %s: %"PRIu64" is less than requested %"PRIu64" skipping the node",
						 __func__, job_ptr,
						 node_ptr->name,
						 tmp_mem,
						 mem_use);
					avail_cpus = 0;
					usable_cpu_cnt[i] = avail_cpus;
					fail_mode = ESLURM_INVALID_TASK_MEMORY;
				}
			} else if (_is_mem_resv())
				gres_test_args.test_mem = true;

			_step_test_gres(step_spec, &gres_test_args, job_ptr,
					&usable_cpu_cnt[i],
					&total_cpus, &avail_cpus,
					&gres_invalid_nodes,
					&fail_mode);

			avail_tasks = avail_cpus;
			total_tasks = total_cpus;
			if (cpus_per_task > 0) {
				avail_tasks /= cpus_per_task;
				total_tasks /= cpus_per_task;
			}
			if (avail_tasks == 0) {
				log_flag(STEPS, "%s: %pJ No task can start on node %s",
					 __func__, job_ptr, node_ptr->name);
				if ((step_spec->min_nodes == INFINITE) ||
				    (step_spec->min_nodes ==
				     job_ptr->node_cnt)) {
					log_flag(STEPS, "%s: %pJ All nodes in allocation required, but can't use them now",
						 __func__, job_ptr);
					FREE_NULL_BITMAP(nodes_avail);
					FREE_NULL_BITMAP(select_nodes_avail);
					xfree(usable_cpu_cnt);
					*return_code = ESLURM_NODES_BUSY;
					if (total_tasks == 0) {
						*return_code = fail_mode;
						log_flag(STEPS, "%s: %pJ Step cannot ever run in the allocation: %s",
							 __func__,
							 job_ptr,
							 slurm_strerror(
								 fail_mode));
					}
					return NULL;
				}
				bit_clear(nodes_avail, i);
				mem_blocked_nodes++;
				mem_blocked_cpus += (total_cpus - avail_cpus);
			} else {
				mem_blocked_cpus += (total_cpus - avail_cpus);
				gres_test_args.first_step_node = false;
			}
		}
	}

	if (gres_invalid_nodes >
	    (job_resrcs_ptr->nhosts - step_spec->min_nodes)) {
		*return_code = ESLURM_INVALID_GRES;
		log_flag(STEPS, "%s: Never able to satisfy the GRES request for this step",
			 __func__);
		FREE_NULL_BITMAP(nodes_avail);
		FREE_NULL_BITMAP(select_nodes_avail);
		xfree(usable_cpu_cnt);
		return NULL;
	}

	if (step_spec->min_nodes == INFINITE) {	/* use all nodes */
		if ((step_spec->num_tasks == NO_VAL) && nodes_avail &&
		    !(step_spec->flags & SSF_EXT_LAUNCHER)) {
			_set_max_num_tasks(step_spec, job_ptr, nodes_avail,
					   cpus_per_task);
			if (step_spec->num_tasks == 0) {
				log_flag(STEPS, "%s: Step requested more processors per task (%d) than can be satisfied.",
					 __func__, cpus_per_task);
				*return_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
				goto cleanup;
			}
		}

		job_resrcs_ptr->next_step_node_inx = 0;
		xfree(usable_cpu_cnt);
		FREE_NULL_BITMAP(select_nodes_avail);
		return nodes_avail;
	}

	if (select_nodes_avail) {
		/*
		 * The select plugin told us these were the only ones we could
		 * choose from.  If it doesn't fit here then defer request
		 */
		bit_and(nodes_avail, select_nodes_avail);
		FREE_NULL_BITMAP(select_nodes_avail);
	}

	/*
	 * An allocating srun will send in the same node_list that was already
	 * used to construct the job allocation. In that case, we can assume
	 * that the job allocation already satifies those requirements.
	 */
	if (step_spec->node_list && xstrcmp(step_spec->node_list,
					    job_ptr->details->req_nodes)) {
		bitstr_t *selected_nodes = NULL;
		log_flag(STEPS, "%s: selected nodelist is %s",
			 __func__, step_spec->node_list);
		error_code = node_name2bitmap(step_spec->node_list, false,
					      &selected_nodes, NULL);
		if (error_code) {
			log_flag(STEPS, "%s: invalid node list %s", __func__,
				 step_spec->node_list);
			FREE_NULL_BITMAP(selected_nodes);
			goto cleanup;
		}
		if (!bit_super_set(selected_nodes, job_ptr->node_bitmap)) {
			log_flag(STEPS, "%s: requested nodes %s not part of %pJ",
				 __func__, step_spec->node_list, job_ptr);
			FREE_NULL_BITMAP(selected_nodes);
			goto cleanup;
		}
		if (!bit_super_set(selected_nodes, nodes_avail)) {
			/*
			 * If some nodes still have some memory or CPUs
			 * allocated to other steps, just defer the execution
			 * of the step
			 */
			if (job_blocked_nodes) {
				*return_code = ESLURM_NODES_BUSY;
				log_flag(STEPS, "%s: some requested nodes %s still have CPUs used by other steps",
					 __func__, step_spec->node_list);
			} else if (mem_blocked_nodes == 0) {
				*return_code = ESLURM_INVALID_TASK_MEMORY;
				log_flag(STEPS, "%s: requested nodes %s have inadequate memory",
					 __func__, step_spec->node_list);
			} else {
				*return_code = ESLURM_NODES_BUSY;
				log_flag(STEPS, "%s: some requested nodes %s still have memory used by other steps",
					 __func__, step_spec->node_list);
			}
			FREE_NULL_BITMAP(selected_nodes);
			goto cleanup;
		}
		if ((step_spec->task_dist & SLURM_DIST_STATE_BASE) ==
		    SLURM_DIST_ARBITRARY) {
			step_spec->min_nodes = bit_set_count(selected_nodes);
		}
		if (selected_nodes) {
			int node_cnt = 0;
			/*
			 * Use selected nodes to run the step and
			 * mark them unavailable for future use
			 */

			/*
			 * If we have selected more than we requested
			 * make the available nodes equal to the
			 * selected nodes and we will pick from that
			 * list later on in the function.
			 * Other than that copy the nodes selected as
			 * the nodes we want.
			 */
			node_cnt = bit_set_count(selected_nodes);
			if (node_cnt > step_spec->max_nodes) {
				log_flag(STEPS, "%s: requested nodes %s exceed max node count for %pJ (%d > %u)",
					 __func__, step_spec->node_list,
					 job_ptr, node_cnt,
					 step_spec->max_nodes);
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
				bit_and_not(nodes_avail, selected_nodes);
				FREE_NULL_BITMAP(selected_nodes);
			}
		}
	} else {
		nodes_picked = bit_alloc(bit_size(nodes_avail));
	}

	/* If gres_per_step then filter nodes_avail to nodes that fill req */
	gres_stepmgr_step_test_per_step(step_gres_list, job_ptr,
				     nodes_avail, step_spec->min_nodes);

	/*
	 * In case we are in relative mode, do not look for idle nodes
	 * as we will not try to get idle nodes first but try to get
	 * the relative node first
	 */
	if (step_spec->relative != NO_VAL16) {
		/*
		 * Remove first (step_spec->relative) nodes from
		 * available list
		 */
		bitstr_t *relative_nodes = NULL;
		relative_nodes = bit_pick_cnt(job_ptr->node_bitmap,
					      step_spec->relative);
		if (relative_nodes == NULL) {
			log_flag(STEPS, "%s: Invalid relative value (%u) for %pJ",
				 __func__, step_spec->relative, job_ptr);
			goto cleanup;
		}
		bit_and_not(nodes_avail, relative_nodes);
		FREE_NULL_BITMAP(relative_nodes);
	} else {
		nodes_idle = bit_alloc (bit_size (nodes_avail) );
		list_for_each(job_ptr->step_list, _mark_busy_nodes, nodes_idle);
		bit_not(nodes_idle);
		bit_and(nodes_idle, nodes_avail);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_STEPS) {
		char *temp1, *temp2, *temp3;
		temp1 = bitmap2node_name(nodes_avail);
		temp2 = bitmap2node_name(nodes_idle);
		if (step_spec->node_list)
			temp3 = step_spec->node_list;
		else
			temp3 = "NONE";
		log_flag(STEPS, "%s: step pick %u-%u nodes, avail:%s idle:%s picked:%s",
			 __func__, step_spec->min_nodes, step_spec->max_nodes,
			 temp1, temp2, temp3);
		xfree(temp1);
		xfree(temp2);
	}

	if ((step_spec->num_tasks == NO_VAL) &&
	    !(step_spec->flags & SSF_EXT_LAUNCHER)) {
		uint32_t cnt = 0;
		bitstr_t *node_bitmap = NULL;

		if ((step_spec->flags & SSF_OVERLAP_FORCE) && nodes_avail) {
			cnt = bit_set_count(nodes_avail);
			node_bitmap = nodes_avail;
		} else if (nodes_idle) {
			cnt = bit_set_count(nodes_idle);
			node_bitmap = nodes_idle;
		}
		if (cnt < step_spec->min_nodes) {
			log_flag(STEPS, "%s: Step requested more nodes (%u) than are available (%d), deferring step until enough nodes are available.",
				 __func__, step_spec->min_nodes, cnt);
			*return_code = ESLURM_NODES_BUSY;
			goto cleanup;
		}

		_set_max_num_tasks(step_spec, job_ptr, node_bitmap,
				   cpus_per_task);
		if (step_spec->num_tasks == 0) {
			log_flag(STEPS, "%s: Step requested more processors per task (%d) than can be satisfied.",
				 __func__, cpus_per_task);
			*return_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
			goto cleanup;
		}
	}

	/*
	 * If user specifies step needs a specific processor count and
	 * all nodes have the same processor count, just translate this to
	 * a node count
	 */
	if (step_spec->cpu_count && job_resrcs_ptr &&
	    (job_resrcs_ptr->cpu_array_cnt == 1) &&
	    (job_resrcs_ptr->cpu_array_value)) {
		uint32_t cpu_count = step_spec->cpu_count;
		uint16_t req_tpc;
		/*
		 * Expand cpu account to account for blocked/used threads when
		 * using threads-per-core. See _step_[de]alloc_lps() for similar
		 * code.
		 */
		req_tpc = _get_threads_per_core(step_spec->threads_per_core,
						job_ptr);

		/*
		 * Only process this differently if the allocation requested
		 * more threads per core than the step is requesting as
		 * job_resrcs->cpu_array_value is already processed with the
		 * threads per core the allocation requested so you don't need
		 * to do this again. See src/common/job_resources.c
		 * build_job_resources_cpu_array().
		 */
		if ((req_tpc != NO_VAL16) &&
		    (req_tpc < job_resrcs_ptr->threads_per_core)) {
			int first_inx = bit_ffs(job_resrcs_ptr->node_bitmap);
			if (first_inx == -1) {
				error("%s: Job %pJ doesn't have any nodes in it! This should never happen",
				      __func__, job_ptr);
				*return_code = ESLURM_INVALID_NODE_COUNT;
				goto cleanup;
			}
			if (req_tpc < node_record_table_ptr[first_inx]->tpc) {
				cpu_count = ROUNDUP(cpu_count, req_tpc);
				cpu_count *=
					node_record_table_ptr[first_inx]->tpc;
			} else if (req_tpc >
				   node_record_table_ptr[first_inx]->tpc) {
				log_flag(STEPS, "%s: requested more threads per core than possible in allocation (%u > %u) for %pJ",
					 __func__,
					 req_tpc,
					 node_record_table_ptr[first_inx]->tpc,
					 job_ptr);
				*return_code = ESLURM_BAD_THREAD_PER_CORE;
				goto cleanup;
			}
		}

		i = ROUNDUP(cpu_count, job_resrcs_ptr->cpu_array_value[0]);
		step_spec->min_nodes = (i > step_spec->min_nodes) ?
			i : step_spec->min_nodes ;

		/*
		 * If we are trying to pack the nodes we only want the minimum
		 * it takes to satisfy the request.
		 */
		if (step_spec->task_dist & SLURM_DIST_PACK_NODES)
			step_spec->max_nodes = step_spec->min_nodes;

		if (step_spec->max_nodes < step_spec->min_nodes) {
			log_flag(STEPS, "%s: %pJ max node less than min node count (%u < %u)",
				 __func__, job_ptr, step_spec->max_nodes,
				 step_spec->min_nodes);
			*return_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
			goto cleanup;
		}
	}

	if (step_spec->min_nodes) {
		int cpus_needed, node_avail_cnt, nodes_needed;

		nodes_picked_cnt = bit_set_count(nodes_picked);
		log_flag(STEPS, "%s: step picked %d of %u nodes",
			 __func__, nodes_picked_cnt, step_spec->min_nodes);

		/*
		 * First do a basic test - if there aren't enough nodes for
		 * this step to run on then we need to defer execution of this
		 * step. As long as there aren't enough nodes for this
		 * step we can never test if the step requested too
		 * many CPUs, too much memory, etc. so we just bail right here.
		 */
		if (nodes_avail)
			node_avail_cnt = bit_set_count(nodes_avail);
		else
			node_avail_cnt = 0;
		if ((node_avail_cnt + nodes_picked_cnt) <
		    step_spec->min_nodes) {
			log_flag(STEPS, "%s: Step requested more nodes (%u) than are available (%d), deferring step until enough nodes are available.",
				 __func__, step_spec->min_nodes,
				 node_avail_cnt);
			*return_code = ESLURM_NODES_BUSY;
			goto cleanup;
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
				bit_or(nodes_picked, node_tmp);
				bit_and_not(nodes_idle, node_tmp);
				bit_and_not(nodes_avail, node_tmp);
				FREE_NULL_BITMAP(node_tmp);
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
				    (mem_blocked_cpus == 0) &&
				    (job_blocked_cpus == 0)) {
					*return_code =
						ESLURM_TOO_MANY_REQUESTED_CPUS;
				} else if ((mem_blocked_cpus > 0) ||
					   (step_spec->min_nodes <=
					    (pick_node_cnt + mem_blocked_nodes +
					     job_blocked_nodes))) {
					*return_code = ESLURM_NODES_BUSY;
				} else if (!bit_super_set(job_ptr->node_bitmap,
							  stepmgr_ops->up_node_bitmap)) {
					*return_code = ESLURM_NODE_NOT_AVAIL;
				}
				goto cleanup;
			}
			bit_or(nodes_picked, node_tmp);
			bit_and_not(nodes_avail, node_tmp);
			FREE_NULL_BITMAP(node_tmp);
			nodes_picked_cnt = step_spec->min_nodes;
		} else if (nodes_needed > 0) {
			if ((step_spec->max_nodes <= nodes_picked_cnt) &&
			    (mem_blocked_cpus == 0) &&
			    (job_blocked_cpus == 0)) {
				*return_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
			} else if ((mem_blocked_cpus > 0) ||
				   (step_spec->min_nodes <=
				    (nodes_picked_cnt + mem_blocked_nodes +
				     job_blocked_nodes))) {
				*return_code = ESLURM_NODES_BUSY;
			} else if (!bit_super_set(job_ptr->node_bitmap,
						  stepmgr_ops->up_node_bitmap)) {
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
					/*
					 * Node not usable (memory insufficient
					 * to allocate any CPUs, etc.)
					 */
					bit_and_not(nodes_avail, node_tmp);
					FREE_NULL_BITMAP(node_tmp);
					continue;
				}

				bit_or(nodes_picked, node_tmp);
				bit_and_not(nodes_avail, node_tmp);
				FREE_NULL_BITMAP(node_tmp);
				nodes_picked_cnt += 1;
				if (step_spec->min_nodes)
					step_spec->min_nodes = nodes_picked_cnt;

				cpus_picked_cnt += cpu_cnt;
				if (nodes_picked_cnt >= step_spec->max_nodes)
					break;
			}
		}

		/*
		 * User is requesting more cpus than we got from the
		 * picked nodes. We should return with an error
		 */
		if (step_spec->cpu_count > cpus_picked_cnt) {
			if (step_spec->cpu_count &&
			    (step_spec->cpu_count <=
			     (cpus_picked_cnt + mem_blocked_cpus +
			      job_blocked_cpus))) {
				*return_code = ESLURM_NODES_BUSY;
			} else if (!bit_super_set(job_ptr->node_bitmap,
						  stepmgr_ops->up_node_bitmap)) {
				*return_code = ESLURM_NODE_NOT_AVAIL;
			}
			log_flag(STEPS, "Have %d nodes with %d cpus which is less than what the user is asking for (%d cpus) aborting.",
				 nodes_picked_cnt,
				 cpus_picked_cnt,
				 step_spec->cpu_count);
			goto cleanup;
		}
	}

	job_resrcs_ptr->next_step_node_inx = bit_fls(nodes_picked) + 1;
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
		/*
		 * Return ESLURM_NODES_BUSY if the node is not responding.
		 * The node will eventually either come back UP or go DOWN.
		 */
		nodes_picked = bit_copy(stepmgr_ops->up_node_bitmap);
		bit_not(nodes_picked);
		bit_and(nodes_picked, job_ptr->node_bitmap);
		for (i = 0; (node_ptr = next_node_bitmap(
				     job_resrcs_ptr->node_bitmap, &i));
		     i++) {
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
static int _count_cpus(job_record_t *job_ptr, bitstr_t *bitmap,
		       uint32_t *usable_cpu_cnt)
{
	int i, sum = 0;
	node_record_t *node_ptr;

	if (job_ptr->job_resrcs && job_ptr->job_resrcs->cpus &&
	    job_ptr->job_resrcs->node_bitmap) {
		int node_inx = -1;
		for (i = 0;
		     (node_ptr = next_node_bitmap(
			job_ptr->job_resrcs->node_bitmap, &i));
		     i++) {
			node_inx++;
			if (!bit_test(job_ptr->node_bitmap, node_ptr->index) ||
			    !bit_test(bitmap, node_ptr->index)) {
				/* absent from current job or step bitmap */
				continue;
			}
			if (usable_cpu_cnt)
				sum += usable_cpu_cnt[node_ptr->index];
			else
				sum += job_ptr->job_resrcs->cpus[node_inx];
		}
	} else {
		error("%pJ lacks cpus array", job_ptr);
		for (i = 0; (node_ptr = next_node_bitmap(bitmap, &i)); i++) {
			sum += node_ptr->config_ptr->cpus;
		}
	}

	return sum;
}

/* Clear avail_core_bitmap cores which are not bound to the allocated gres */
static int _gres_filter_avail_cores(void *x, void *arg)
{
	gres_state_t *gres_state_step = x;
	foreach_gres_filter_t *args = arg;
	gres_step_state_t *gres_ss = gres_state_step->gres_data;
	bitstr_t *filter_core_bitmap;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;

	/* Bail early if this GRES isn't used on the node */
	if (!gres_ss->gres_cnt_node_alloc ||
	    !gres_ss->gres_cnt_node_alloc[args->job_node_inx])
		return 0;

	if (!(gres_state_node = list_find_first(args->node_gres_list,
						gres_find_id,
						&gres_state_step->plugin_id))) {
		error("No node gres when step gres is allocated. This should never happen.");
		return 0;
	}
	gres_ns = gres_state_node->gres_data;

	if (!gres_ns->topo_cnt) /* No topology info */
		return 0;

	filter_core_bitmap = bit_copy(args->all_gres_core_bitmap);

	/* Determine which specific cores can be used */
	for (int i = 0; i < gres_ns->topo_cnt; i++) {
		/* Is this gres allocated to the step? */
		if (gres_ss->gres_bit_alloc &&
		    !bit_overlap_any(
			    gres_ss->gres_bit_alloc[args->job_node_inx],
			    gres_ns->topo_gres_bitmap[i]))
			continue;
		/* Does it specifify which cores which can use it */
		if (!gres_ns->topo_core_bitmap[i]) {
			bit_nset(args->any_gres_core_bitmap,
				 args->core_start_bit, args->core_end_bit);
			continue;
		}
		bit_nclear(filter_core_bitmap, args->core_start_bit,
			   args->core_end_bit);
		for (int j = 0;
		     j < bit_size(gres_ns->topo_core_bitmap[i]);
		     j++) {
			if (bit_test(gres_ns->topo_core_bitmap[i], j)) {
				bit_set(filter_core_bitmap,
					args->core_start_bit + j);
			}
		}
		bit_or(args->any_gres_core_bitmap, filter_core_bitmap);
		bit_and(args->all_gres_core_bitmap, filter_core_bitmap);
	}
	FREE_NULL_BITMAP(filter_core_bitmap);
	return 0;
}

/* Return true if a core was picked, false if not */
static bool _pick_step_core(step_record_t *step_ptr,
			    job_resources_t *job_resrcs_ptr,
			    bitstr_t *avail_core_bitmap, int job_node_inx,
			    int sock_inx, int core_inx, bool use_all_cores,
			    bool oversubscribing_cores)
{
	int bit_offset;

	bit_offset = get_job_resources_offset(job_resrcs_ptr,
					      job_node_inx,
					      sock_inx,
					      core_inx);
	if (bit_offset < 0)
		fatal("get_job_resources_offset");

	if (!bit_test(avail_core_bitmap, bit_offset))
		return false;

	if (oversubscribing_cores) {
		/* Already allocated cores, now we are oversubscribing cores */
		if (bit_test(step_ptr->core_bitmap_job, bit_offset))
			return false; /* already taken by this step */

		log_flag(STEPS, "%s: over-subscribe alloc Node:%d Socket:%d Core:%d",
			 __func__, job_node_inx, sock_inx, core_inx);
	} else {
		/* Check and set the job's used cores. */
		if (!(step_ptr->flags & SSF_OVERLAP_FORCE)) {
			if ((use_all_cores == false) &&
			    bit_test(job_resrcs_ptr->core_bitmap_used,
				     bit_offset))
				return false;
			bit_set(job_resrcs_ptr->core_bitmap_used, bit_offset);
		}

		log_flag(STEPS, "%s: alloc Node:%d Socket:%d Core:%d",
			 __func__, job_node_inx, sock_inx, core_inx);
	}

	bit_set(step_ptr->core_bitmap_job, bit_offset);

	return true;
}

static bool _handle_core_select(step_record_t *step_ptr,
				job_resources_t *job_resrcs_ptr,
				bitstr_t *avail_core_bitmap,
				int job_node_inx, uint16_t sockets,
				uint16_t cores, bool use_all_cores,
				bool oversubscribing_cores, int *core_cnt,
				uint16_t cores_per_task)
{
	int core_inx, i, sock_inx;
	static int last_core_inx;

	xassert(core_cnt);

	if (*core_cnt <= 0)
		return true;

	/*
	 * Use last_core_inx to avoid putting all of the extra
	 * work onto core zero when oversubscribing cpus.
	 */
	if (oversubscribing_cores)
		last_core_inx = (last_core_inx + 1) % cores;

	/*
	 * Figure out the task distribution. The default is to cyclically
	 * distribute to sockets.
	 */
	if (step_ptr->step_layout &&
	    ((step_ptr->step_layout->task_dist & SLURM_DIST_SOCKMASK) ==
	     SLURM_DIST_SOCKBLOCK)) {
		/* Fill sockets before allocating to the next socket */
		for (sock_inx=0; sock_inx < sockets; sock_inx++) {
			for (i=0; i < cores; i++) {
				if (oversubscribing_cores)
					core_inx = (last_core_inx + i) % cores;
				else
					core_inx = i;

				if (!_pick_step_core(step_ptr, job_resrcs_ptr,
						     avail_core_bitmap,
						     job_node_inx, sock_inx,
						     core_inx, use_all_cores,
						     oversubscribing_cores))
					continue;

				if (--(*core_cnt) == 0)
					return true;
			}
		}
	} else if (step_ptr->step_layout &&
		   ((step_ptr->step_layout->task_dist & SLURM_DIST_SOCKMASK) ==
		    SLURM_DIST_SOCKCFULL)) {
		for (i = 0; i < cores; i++) {
			if (oversubscribing_cores)
				core_inx = (last_core_inx + i) % cores;
			else
				core_inx = i;
			for (sock_inx = 0; sock_inx < sockets; sock_inx++) {
				if (!_pick_step_core(step_ptr, job_resrcs_ptr,
						     avail_core_bitmap,
						     job_node_inx, sock_inx,
						     core_inx, use_all_cores,
						     oversubscribing_cores)) {
						if (sock_inx == sockets)
							sock_inx = 0;
						continue;
				}
				if (--(*core_cnt) == 0)
					return true;
			}
		}
	} else { /* SLURM_DIST_SOCKCYCLIC */
		int task_alloc_cores = 0;
		int *next_core = xcalloc(sockets, sizeof(int));
		bool nothing_allocated = false;
		while (!nothing_allocated) {
			nothing_allocated = true;
			for (sock_inx = 0; sock_inx < sockets; sock_inx++) {
				for (i = next_core[sock_inx]; i < cores;
				     i++) {
					if (oversubscribing_cores)
						core_inx = (last_core_inx + i) %
							   cores;
					else
						core_inx = i;

					next_core[sock_inx] = i + 1;
					if (!_pick_step_core(
						step_ptr,
						job_resrcs_ptr,
						avail_core_bitmap,
						job_node_inx,
						sock_inx,
						core_inx,
						use_all_cores,
						oversubscribing_cores))
						continue;
					nothing_allocated = false;
					if (--(*core_cnt) == 0) {
						xfree(next_core);
						return true;
					}
					if (++task_alloc_cores ==
					    cores_per_task) {
						task_alloc_cores = 0;
						break;
					}
				}
			}
		}
		xfree(next_core);
	}
	return false;
}

/* Update the step's core bitmaps, create as needed.
 *	Add the specified task count for a specific node in the job's
 *	and step's allocation */
static int _pick_step_cores(step_record_t *step_ptr,
			    job_resources_t *job_resrcs_ptr, int job_node_inx,
			    uint16_t task_cnt, uint16_t cpus_per_core,
			    int node_inx, int ntasks_per_core,
			    int gres_cpus_alloc)
{
	uint16_t sockets, cores, cores_per_task, tasks_per_node;
	int core_cnt = (int) task_cnt;
	bool use_all_cores;
	bitstr_t *all_gres_core_bitmap = NULL, *any_gres_core_bitmap = NULL;

	xassert(task_cnt);

	if (!step_ptr->core_bitmap_job)
		step_ptr->core_bitmap_job =
			bit_alloc(bit_size(job_resrcs_ptr->core_bitmap));

	if (get_job_resources_cnt(job_resrcs_ptr, job_node_inx,
				  &sockets, &cores))
		fatal("get_job_resources_cnt");

	if (ntasks_per_core != INFINITE16)
		tasks_per_node = cores * ntasks_per_core * sockets;
	else
		tasks_per_node = cores * cpus_per_core * sockets;

	if (((step_ptr->flags & SSF_WHOLE) || task_cnt == (cores * sockets)) &&
	    (task_cnt <= tasks_per_node || (step_ptr->flags & SSF_OVERCOMMIT)))
	{
		use_all_cores = true;
		core_cnt = ROUNDUP(job_resrcs_ptr->cpus[job_node_inx],
				   cpus_per_core);
	} else {
		use_all_cores = false;

		if (gres_cpus_alloc) {
			core_cnt = ROUNDUP(gres_cpus_alloc, cpus_per_core);
		} else if (step_ptr->cpus_per_task > 0) {
			core_cnt *= step_ptr->cpus_per_task;
			core_cnt = ROUNDUP(core_cnt, cpus_per_core);
		}

		log_flag(STEPS, "%s: step %pS requires %u cores on node %d with cpus_per_core=%u, available cpus from job: %u",
			 __func__, step_ptr, core_cnt, job_node_inx,
			 cpus_per_core, job_resrcs_ptr->cpus[job_node_inx]);

		if (core_cnt > ROUNDUP(job_resrcs_ptr->cpus[job_node_inx],
				       cpus_per_core) &&
		    !(step_ptr->flags & SSF_OVERCOMMIT)) {
			/* Node can never fullfill step request */
			return ESLURM_TOO_MANY_REQUESTED_CPUS;
		}
	}

	all_gres_core_bitmap = bit_copy(job_resrcs_ptr->core_bitmap);
	any_gres_core_bitmap = bit_copy(job_resrcs_ptr->core_bitmap);
	if (step_ptr->gres_list_alloc) {
		foreach_gres_filter_t args = {
			.all_gres_core_bitmap = all_gres_core_bitmap,
			.any_gres_core_bitmap = any_gres_core_bitmap,
			.core_start_bit = get_job_resources_offset(
				job_resrcs_ptr, job_node_inx, 0, 0),
			.core_end_bit = get_job_resources_offset(
				job_resrcs_ptr, job_node_inx, sockets - 1,
				cores - 1),
			.job_node_inx = job_node_inx,
			.node_gres_list =
				node_record_table_ptr[node_inx]->gres_list,
		};

		if ((args.core_start_bit > bit_size(all_gres_core_bitmap)) ||
		    (args.core_end_bit > bit_size(all_gres_core_bitmap)))
			error("coremap offsets fall outside core_bitmap size. This should never happen.");
		else if (!args.node_gres_list)
			error("No node gres when step gres is allocated. This should never happen.");
		else {
			bit_nclear(any_gres_core_bitmap, args.core_start_bit,
				   args.core_end_bit);
			list_for_each(step_ptr->gres_list_alloc,
				      _gres_filter_avail_cores, &args);
			bit_and(any_gres_core_bitmap,
				job_resrcs_ptr->core_bitmap);
		}
	}
	cores_per_task = ROUNDUP(core_cnt, task_cnt); /* Round up */

	/* select idle cores that fit all gres binding first */
	if (_handle_core_select(step_ptr, job_resrcs_ptr,
				all_gres_core_bitmap, job_node_inx,
				sockets, cores, use_all_cores, false, &core_cnt,
				cores_per_task))
		goto cleanup;

	/* select idle cores that fit any gres binding second */
	if (!bit_equal(all_gres_core_bitmap, any_gres_core_bitmap) &&
	    _handle_core_select(step_ptr, job_resrcs_ptr,
				any_gres_core_bitmap, job_node_inx,
				sockets, cores, use_all_cores, false, &core_cnt,
				cores_per_task))
		goto cleanup;

	/* select any idle cores */
	if (!(step_ptr->job_ptr->bit_flags & GRES_ENFORCE_BIND) &&
	    !bit_equal(any_gres_core_bitmap, job_resrcs_ptr->core_bitmap)) {
		log_flag(STEPS, "gres topology sub-optimal for %ps",
			&(step_ptr->step_id));
		if (_handle_core_select(step_ptr, job_resrcs_ptr,
					job_resrcs_ptr->core_bitmap,
					job_node_inx, sockets, cores,
					use_all_cores, false, &core_cnt,
					cores_per_task))
			goto cleanup;
	}

	/* The test for cores==0 is just to avoid CLANG errors.
	 * It should never happen */
	if (use_all_cores || (cores == 0))
		goto cleanup;


	if (!(step_ptr->flags & SSF_OVERCOMMIT)) {
		FREE_NULL_BITMAP(all_gres_core_bitmap);
		FREE_NULL_BITMAP(any_gres_core_bitmap);
		return ESLURM_NODES_BUSY;
	}

	/* We need to over-subscribe one or more cores. */
	log_flag(STEPS, "%s: %pS needs to over-subscribe cores required:%u assigned:%u/%"PRIu64 " overcommit:%c exclusive:%c",
		 __func__, step_ptr, cores,
		 bit_set_count(job_resrcs_ptr->core_bitmap),
		 bit_size(job_resrcs_ptr->core_bitmap),
		 ((step_ptr->flags & SSF_OVERCOMMIT) ? 'T' : 'F'),
		 ((step_ptr->flags & SSF_EXCLUSIVE) ? 'T' : 'F'));

	/* oversubscribe cores that fit all gres binding first */
	if (_handle_core_select(step_ptr, job_resrcs_ptr,
				all_gres_core_bitmap, job_node_inx,
				sockets, cores, use_all_cores, true, &core_cnt,
				cores_per_task))
		goto cleanup;

	/* oversubscribe cores that fit any gres binding second */
	if (!bit_equal(all_gres_core_bitmap, any_gres_core_bitmap) &&
	    _handle_core_select(step_ptr, job_resrcs_ptr,
				any_gres_core_bitmap, job_node_inx,
				sockets, cores, use_all_cores, true, &core_cnt,
				cores_per_task))
		goto cleanup;

	/* oversubscribe any cores */
	if (!(step_ptr->job_ptr->bit_flags & GRES_ENFORCE_BIND) &&
	    !bit_equal(any_gres_core_bitmap, job_resrcs_ptr->core_bitmap) &&
	    _handle_core_select(step_ptr, job_resrcs_ptr,
				job_resrcs_ptr->core_bitmap, job_node_inx,
				sockets, cores, use_all_cores, true, &core_cnt,
				cores_per_task))
		goto cleanup;


cleanup:
	FREE_NULL_BITMAP(all_gres_core_bitmap);
	FREE_NULL_BITMAP(any_gres_core_bitmap);
	return SLURM_SUCCESS;
}

static bool _use_one_thread_per_core(step_record_t *step_ptr)
{
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;

	if ((step_ptr->threads_per_core == 1) ||
	    ((step_ptr->threads_per_core == NO_VAL16) &&
	     (job_ptr->details->mc_ptr->threads_per_core == 1)) ||
	    (!(job_resrcs_ptr->whole_node & WHOLE_NODE_REQUIRED) &&
	     (slurm_conf.select_type_param & (CR_CORE | CR_SOCKET)) &&
	     (job_ptr->details &&
	      (job_ptr->details->cpu_bind_type != NO_VAL16) &&
	      (job_ptr->details->cpu_bind_type &
	       CPU_BIND_ONE_THREAD_PER_CORE))))
		return true;
	return false;
}

static void _modify_cpus_alloc_for_tpc(uint16_t cr_type, uint16_t req_tpc,
				       uint16_t vpus, int *cpus_alloc)
{
	xassert(cpus_alloc);

	if ((cr_type & (CR_CORE | CR_SOCKET | CR_LINEAR)) &&
	    (req_tpc != NO_VAL16) && (req_tpc < vpus)) {
		*cpus_alloc = ROUNDUP(*cpus_alloc, req_tpc);
		*cpus_alloc *= vpus;
	}
}

/* Update a job's record of allocated CPUs when a job step gets scheduled */
static int _step_alloc_lps(step_record_t *step_ptr, char **err_msg)
{
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	node_record_t *node_ptr;
	slurm_step_layout_t *step_layout = step_ptr->step_layout;
	int cpus_alloc, cpus_alloc_mem, cpu_array_inx = 0;
	int job_node_inx = -1, step_node_inx = -1, node_cnt = 0;
	bool first_step_node = true, pick_step_cores = true;
	bool all_job_mem = false;
	uint32_t rem_nodes;
	int rc = SLURM_SUCCESS, final_rc = SLURM_SUCCESS;
	multi_core_data_t *mc_ptr = job_ptr->details->mc_ptr;
	uint16_t orig_cpus_per_task = step_ptr->cpus_per_task;
	uint16_t *cpus_per_task_array = NULL;
	uint16_t *cpus_alloc_pn = NULL;
	uint16_t ntasks_per_core = step_ptr->ntasks_per_core;
	uint16_t req_tpc = _get_threads_per_core(step_ptr->threads_per_core,
						 job_ptr);

	xassert(job_resrcs_ptr);
	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	if (!step_layout) /* batch step */
		return rc;

	if (!bit_set_count(job_resrcs_ptr->node_bitmap))
		return rc;

	xfree(*err_msg);

	xassert(job_resrcs_ptr->core_bitmap);
	xassert(job_resrcs_ptr->core_bitmap_used);
	if (step_ptr->core_bitmap_job) {
		/* "scontrol reconfig" of live system */
		pick_step_cores = false;
	} else if (!(step_ptr->flags & SSF_OVERCOMMIT) &&
		   (step_ptr->cpu_count == job_ptr->total_cpus) &&
		   ((ntasks_per_core == mc_ptr->threads_per_core) ||
		    (ntasks_per_core == INFINITE16))) {
		/*
		 * If the step isn't overcommitting and uses all of job's cores
		 * Just copy the bitmap to save time
		 */
		step_ptr->core_bitmap_job = bit_copy(
			job_resrcs_ptr->core_bitmap);
		pick_step_cores = false;
	}

	if (step_ptr->pn_min_memory && _is_mem_resv() &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("%s: lack memory allocation details to enforce memory limits for %pJ",
		      __func__, job_ptr);
		step_ptr->pn_min_memory = 0;
	}

	if (!step_ptr->pn_min_memory)
		all_job_mem = true;

	rem_nodes = bit_set_count(step_ptr->step_node_bitmap);
	xassert(rem_nodes == step_layout->node_cnt);

	cpus_alloc_pn = xcalloc(step_layout->node_cnt, sizeof(*cpus_alloc_pn));
	step_ptr->memory_allocated = xcalloc(rem_nodes, sizeof(uint64_t));
	for (int i = 0;
	     (node_ptr = next_node_bitmap(job_resrcs_ptr->node_bitmap, &i));
	     i++) {
		/*
		 * gres_cpus_alloc - if cpus_per_gres is requested, this is
		 * cpus_per_gres * gres_alloc on this node
		 */
		int gres_cpus_alloc = 0;
		uint16_t cpus_per_task = orig_cpus_per_task;
		uint64_t gres_step_node_mem_alloc = 0;
		uint16_t vpus, avail_cpus_per_core, alloc_cpus_per_core;
		uint16_t task_cnt;
		bitstr_t *unused_core_bitmap;
		job_node_inx++;
		if (!bit_test(step_ptr->step_node_bitmap, i))
			continue;
		step_node_inx++;
		if (job_node_inx >= job_resrcs_ptr->nhosts)
			fatal("%s: node index bad", __func__);

		if (!(task_cnt = step_layout->tasks[step_node_inx])) {
			/* This should have been caught earlier */
			error("Bad step layout: no tasks placed on node %d (%s)",
			      job_node_inx,
			      node_ptr->name);
			final_rc = ESLURM_BAD_TASK_COUNT;
			/*
			 * Finish allocating resources to all nodes to avoid
			 * underflow errors in _step_alloc_lps
			 */
			continue;
		}

		/*
		 * NOTE: The --overcommit option can result in
		 * cpus_used[] having a higher value than cpus[]
		 */

		/*
		 * If whole allocate all cpus here instead of just the ones
		 * requested
		 */
		if (first_step_node)
			step_ptr->cpu_count = 0;

		if ((++node_cnt) >
		    job_resrcs_ptr->cpu_array_reps[cpu_array_inx]) {
			cpu_array_inx++;
			node_cnt = 0;
		}

		vpus = node_ptr->tpc;

		if (req_tpc != NO_VAL16)
			avail_cpus_per_core = req_tpc;
		else
			avail_cpus_per_core = vpus;

		/*
		 * If the step requested cpus_per_gres, this is mutually
		 * exclusive with cpus_per_task. We need to calculate total
		 * gres times cpus_per_gres to get a total cpu count.
		 */
		unused_core_bitmap = bit_copy(job_resrcs_ptr->core_bitmap);
		bit_and_not(unused_core_bitmap,
			    job_resrcs_ptr->core_bitmap_used);
		rc = gres_stepmgr_step_alloc(step_ptr->gres_list_req,
					  &step_ptr->gres_list_alloc,
					  job_ptr->gres_list_alloc,
					  job_node_inx, first_step_node,
					  task_cnt,
					  rem_nodes, job_ptr->job_id,
					  step_ptr->step_id.step_id,
					  !(step_ptr->flags &
					    SSF_OVERLAP_FORCE),
					  &gres_step_node_mem_alloc,
					  node_ptr->gres_list,
					  unused_core_bitmap,
					  &gres_cpus_alloc);
		FREE_NULL_BITMAP(unused_core_bitmap);
		if (rc != SLURM_SUCCESS) {
			log_flag(STEPS, "unable to allocate step GRES for job node %d (%s): %s",
				 job_node_inx,
				 node_ptr->name,
				 slurm_strerror(rc));
			/*
			 * We need to set alloc resources before we continue to
			 * avoid underflow in _step_dealloc_lps()
			 */
			final_rc = rc;
		}
		first_step_node = false;
		rem_nodes--;

		if (gres_cpus_alloc) {
			if (task_cnt > gres_cpus_alloc) {
				/*
				 * Do not error here. If a job requests fewer
				 * cpus than tasks via cpus_per_gres,
				 * the job will be allocated one cpu per task.
				 * Do the same here.
				 * Use this same logic in _step_dealloc_lps.
				 */
				cpus_per_task = 1;
				log_flag(STEPS, "%s: %pS node %d (%s) gres_cpus_alloc (%d) < tasks (%u), changing gres_cpus_alloc to tasks.",
					 __func__, step_ptr, job_node_inx,
					 node_ptr->name, gres_cpus_alloc,
					 task_cnt);
				gres_cpus_alloc = task_cnt;
			} else {
				cpus_per_task = gres_cpus_alloc / task_cnt;
			}
		}

		/*
		 * Modify cpus-per-task to request full cores if they can't
		 * be shared
		*/
		if ((ntasks_per_core != INFINITE16) && ntasks_per_core) {
			alloc_cpus_per_core = avail_cpus_per_core /
					      ntasks_per_core;
			if ((alloc_cpus_per_core > 1) &&
			    (cpus_per_task % alloc_cpus_per_core)) {
				cpus_per_task += alloc_cpus_per_core -
					(cpus_per_task % alloc_cpus_per_core);
				/*
				 * Modify gres_cpus_alloc to account for
				 * ntasks_per_core. If this results in
				 * requesting more cores than are available,
				 * then _pick_step_cores() will fail.
				 *
				 * Make sure to use this same logic in
				 * _step_dealloc_lps() to know how many
				 * cpus were allocated to this step on this
				 * node.
				 */
				if (gres_cpus_alloc)
					gres_cpus_alloc = task_cnt *
						cpus_per_task;
			}
		}
		step_ptr->cpus_per_task = cpus_per_task;
		/*
		 * Only populate cpus_per_task_array if needed: if cpus_per_tres
		 * was requested, then cpus_per_task may not be the same on all
		 * nodes. Otherwise, cpus_per_task is the same on all nodes,
		 * and this per-node array isn't needed.
		 */
		if (gres_cpus_alloc) {
			if (!cpus_per_task_array)
				cpus_per_task_array =
					xcalloc(step_layout->node_cnt,
						sizeof(*cpus_per_task_array));
			cpus_per_task_array[step_node_inx] = cpus_per_task;
		}
		log_flag(STEPS, "%s: %pS node %d (%s) gres_cpus_alloc=%d tasks=%u cpus_per_task=%u",
			 __func__, step_ptr, job_node_inx, node_ptr->name,
			 gres_cpus_alloc, task_cnt,
			 cpus_per_task);

		if (step_ptr->flags & SSF_WHOLE) {
			cpus_alloc_mem = cpus_alloc =
				job_resrcs_ptr->cpus[job_node_inx];

			/*
			 * If we are requesting all the memory in the job
			 * (--mem=0) we get it all, otherwise we use what was
			 * requested specifically for the step.
			 *
			 * Else factor in the tpc so we get the correct amount
			 * of memory.
			 */
			if (all_job_mem)
				cpus_alloc_mem =
					job_resrcs_ptr->
					cpu_array_value[cpu_array_inx];
			else if ((req_tpc != NO_VAL16) &&
				 (req_tpc < vpus)) {
				cpus_alloc_mem = ROUNDUP(cpus_alloc_mem, vpus);
				cpus_alloc_mem *= req_tpc;
			}
		} else {
			if (gres_cpus_alloc)
				cpus_alloc = gres_cpus_alloc;
			else
				cpus_alloc = task_cnt * cpus_per_task;

			/*
			 * If we are requesting all the memory in the job
			 * (--mem=0) we get it all, otherwise we use what was
			 * requested specifically for the step.
			 */
			if (all_job_mem)
				cpus_alloc_mem =
					job_resrcs_ptr->
					cpu_array_value[cpu_array_inx];
			else
				cpus_alloc_mem = cpus_alloc;

			/*
			 * If we are doing threads per core we need the whole
			 * core allocated even though we are only using what was
			 * requested. Don't worry about cpus_alloc_mem, it's
			 * already correct.
			 */
			_modify_cpus_alloc_for_tpc(job_resrcs_ptr->cr_type,
						   req_tpc, vpus, &cpus_alloc);

			/*
			 * TODO: We need ntasks-per-* sent to the ctld to make
			 * more decisions on allocation cores.
			 */
		}
		step_ptr->cpu_count += cpus_alloc;
		cpus_alloc_pn[step_node_inx] = cpus_alloc;

		/*
		 * Don't count this step against the allocation if
		 * --overlap=force
		 */
		if (!(step_ptr->flags & SSF_OVERLAP_FORCE)) {
			cpus_alloc = ROUNDUP(cpus_alloc, vpus);
			cpus_alloc *= vpus;
			if ((job_resrcs_ptr->cr_type & CR_CPU) && (vpus > 1) &&
			    (job_resrcs_ptr->cpus_used[job_node_inx] +
			     cpus_alloc) > job_resrcs_ptr->cpus[job_node_inx])
				job_resrcs_ptr->cpus_used[job_node_inx] =
					job_resrcs_ptr->cpus[job_node_inx];
			else
				job_resrcs_ptr->cpus_used[job_node_inx] +=
					cpus_alloc;
		}

		if (!step_ptr->pn_min_memory && !gres_step_node_mem_alloc) {
			/* If we aren't requesting memory get it from the job */
			step_ptr->pn_min_memory =
				job_resrcs_ptr->memory_allocated[job_node_inx];
			step_ptr->flags |= SSF_MEM_ZERO;
		}

		if (step_ptr->pn_min_memory && _is_mem_resv()) {
			uint64_t mem_use;
			if (step_ptr->pn_min_memory & MEM_PER_CPU) {
				mem_use = step_ptr->pn_min_memory;
				mem_use &= (~MEM_PER_CPU);
				mem_use *= cpus_alloc_mem;
			} else if (step_ptr->flags & SSF_MEM_ZERO) {
				mem_use = job_resrcs_ptr->
					memory_allocated[job_node_inx];
			} else {
				mem_use = step_ptr->pn_min_memory;
			}
			step_ptr->memory_allocated[step_node_inx] = mem_use;
			/*
			 * Do not count against the job's memory allocation if
			 * --mem=0 or --overlap=force were requested.
			 */
			if (!(step_ptr->flags & SSF_MEM_ZERO) &&
			    !(step_ptr->flags & SSF_OVERLAP_FORCE))
				job_resrcs_ptr->memory_used[job_node_inx] +=
					mem_use;
		} else if (_is_mem_resv()) {
			step_ptr->memory_allocated[step_node_inx] =
				gres_step_node_mem_alloc;
			/*
			 * Don't count this step against the allocation if
			 * --overlap=force
			 */
			if (!(step_ptr->flags & SSF_OVERLAP_FORCE))
				job_resrcs_ptr->memory_used[job_node_inx] +=
					gres_step_node_mem_alloc;
		}

		/*
		 * Now that we have set cpus and memory used for this node,
		 * we can check if there was an error, and continue to the
		 * next node. If any node had an error, we can also skip
		 * picking cores and skip to the next node.
		 *
		 */
		if (final_rc != SLURM_SUCCESS) {
			continue;
		}

		if (pick_step_cores) {
			uint16_t cpus_per_core = 1;
			/*
			 * Here we're setting number of CPUs per core
			 * if we don't enforce 1 thread per core
			 *
			 * TODO: move cpus_per_core to slurm_step_layout_t
			 */
			if (!_use_one_thread_per_core(step_ptr) &&
			    (!(node_ptr->cpus == node_ptr->tot_cores))) {
				if (step_ptr->threads_per_core != NO_VAL16)
					cpus_per_core =
						step_ptr->threads_per_core;
				else if (mc_ptr->threads_per_core != NO_VAL16)
					cpus_per_core =
						mc_ptr->threads_per_core;
				else {
					cpus_per_core = node_ptr->threads;
				}
			}
			if ((rc = _pick_step_cores(step_ptr, job_resrcs_ptr,
						   job_node_inx,
						   task_cnt,
						   cpus_per_core, i,
						   ntasks_per_core,
						   gres_cpus_alloc))) {
				log_flag(STEPS, "unable to pick step cores for job node %d (%s): %s",
					 job_node_inx,
					 node_ptr->name,
					 slurm_strerror(rc));
				final_rc = rc;
				/* Finish allocating resources to all nodes */
				continue;
			}
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND)
			_dump_step_layout(step_ptr);

		if (step_ptr->flags & SSF_OVERLAP_FORCE)
			log_flag(STEPS, "step alloc on job node %d (%s); does not count against job allocation",
				 job_node_inx,
				 node_ptr->name);

		else
			log_flag(STEPS, "step alloc on job node %d (%s) used %u of %u CPUs",
				 job_node_inx,
				 node_ptr->name,
				 job_resrcs_ptr->cpus_used[job_node_inx],
				 job_resrcs_ptr->cpus[job_node_inx]);

		if (step_node_inx == (step_layout->node_cnt - 1))
			break;
	}
	slurm_array16_to_value_reps(cpus_per_task_array, step_layout->node_cnt,
				    &step_layout->cpt_compact_array,
				    &step_layout->cpt_compact_reps,
				    &step_layout->cpt_compact_cnt);
	xfree(cpus_per_task_array);

	slurm_array16_to_value_reps(cpus_alloc_pn, step_layout->node_cnt,
				    &step_ptr->cpu_alloc_values,
				    &step_ptr->cpu_alloc_reps,
				    &step_ptr->cpu_alloc_array_cnt);
	xfree(cpus_alloc_pn);

	gres_step_state_log(step_ptr->gres_list_req, job_ptr->job_id,
			    step_ptr->step_id.step_id);
	if ((slurm_conf.debug_flags & DEBUG_FLAG_GRES) &&
	    step_ptr->gres_list_alloc)
		info("Step Alloc GRES:");
	gres_step_state_log(step_ptr->gres_list_alloc, job_ptr->job_id,
			    step_ptr->step_id.step_id);

	/*
	 * If we failed to allocate resources on at least one of the nodes, we
	 * need to deallocate resources.
	 * Creating a backup of the resources then restoring in case of an
	 * error does not work - this method leaves cpus allocated to the node
	 * after the job completes. Instead, we try to allocate resources on
	 * all nodes in the job even if one of the nodes resulted in a failure.
	 */
	if (final_rc != SLURM_SUCCESS)
		_step_dealloc_lps(step_ptr);

	return final_rc;
}

/* Dump a job step's CPU binding information.
 * NOTE: The core_bitmap_job and node index are based upon
 * the _job_ allocation */
static void _dump_step_layout(step_record_t *step_ptr)
{
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int i, bit_inx, core_inx, node_inx, rep, sock_inx;

	if ((step_ptr->core_bitmap_job == NULL) ||
	    (job_resrcs_ptr == NULL) ||
	    (job_resrcs_ptr->cores_per_socket == NULL))
		return;

	info("====================");
	info("%pS", step_ptr);
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

static void _step_dealloc_lps(step_record_t *step_ptr)
{
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int cpus_alloc;
	int job_node_inx = -1, step_node_inx = -1;
	uint32_t step_id = step_ptr->step_id.step_id;
	node_record_t *node_ptr;
	uint16_t req_tpc = _get_threads_per_core(step_ptr->threads_per_core,
						 job_ptr);

	xassert(job_resrcs_ptr);
	if (!job_resrcs_ptr) {
		error("%s: job_resrcs is NULL for %pS; this should never happen",
		      __func__, step_ptr);
		return;
	}

	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	/* These special steps do not allocate any resources */
	if ((step_id == SLURM_EXTERN_CONT) ||
	    (step_id == SLURM_BATCH_SCRIPT) ||
	    (step_id == SLURM_INTERACTIVE_STEP) ||
	    (step_ptr->flags & SSF_EXT_LAUNCHER)) {
		log_flag(STEPS, "Skip %s for %pS", __func__, step_ptr);
		return;
	}

	if (!bit_set_count(job_resrcs_ptr->node_bitmap))
		return;

	if (step_ptr->memory_allocated && _is_mem_resv() &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("%s: lack memory allocation details to enforce memory limits for %pJ",
		      __func__, job_ptr);
	}

	for (int i = 0;
	     (node_ptr = next_node_bitmap(job_resrcs_ptr->node_bitmap, &i));
	     i++) {
		uint16_t vpus = node_ptr->tpc;
		job_node_inx++;
		if (!bit_test(step_ptr->step_node_bitmap, i))
			continue;
		step_node_inx++;
		if (job_node_inx >= job_resrcs_ptr->nhosts)
			fatal("_step_dealloc_lps: node index bad");

		/*
		 * We need to free GRES structures regardless of overlap.
		 */
		gres_stepmgr_step_dealloc(step_ptr->gres_list_alloc,
				       job_ptr->gres_list_alloc, job_ptr->job_id,
				       step_ptr->step_id.step_id,
				       job_node_inx,
				       !(step_ptr->flags & SSF_OVERLAP_FORCE));

		if (step_ptr->flags & SSF_OVERLAP_FORCE) {
			log_flag(STEPS, "step dealloc on job node %d (%s); did not count against job allocation",
				 job_node_inx,
				 node_ptr->name);
			continue; /* Next node */
		}

		/*
		 * If zero tasks, then _step_alloc_lps() error'd and did not
		 * allocate any resources, so we should not deallocate anything.
		 */
		if (!step_ptr->step_layout->tasks[step_node_inx])
			continue;

		if (step_ptr->start_protocol_ver >=
		    SLURM_23_11_PROTOCOL_VERSION) {
			int inx;

			xassert(step_ptr->cpu_alloc_array_cnt);
			xassert(step_ptr->cpu_alloc_reps);
			xassert(step_ptr->cpu_alloc_values);

			inx = slurm_get_rep_count_inx(
				step_ptr->cpu_alloc_reps,
				step_ptr->cpu_alloc_array_cnt,
				step_node_inx);
			cpus_alloc = step_ptr->cpu_alloc_values[inx];
		} else if (step_ptr->flags & SSF_WHOLE) {
			cpus_alloc = job_resrcs_ptr->cpus[job_node_inx];
		} else {
			uint16_t cpus_per_task = step_ptr->cpus_per_task;

			cpus_alloc =
				step_ptr->step_layout->tasks[step_node_inx] *
				cpus_per_task;

			/*
			 * If we are doing threads per core we need the whole
			 * core allocated even though we are only using what was
			 * requested.
			 */
			_modify_cpus_alloc_for_tpc(job_resrcs_ptr->cr_type,
						   req_tpc, vpus, &cpus_alloc);

			/*
			 * TODO: We need ntasks-per-* sent to the ctld to make
			 * more decisions on allocation cores.
			 */
		}

		cpus_alloc = ROUNDUP(cpus_alloc, vpus);
		cpus_alloc *= vpus;

		if ((job_resrcs_ptr->cr_type & CR_CPU) && (node_ptr->tpc > 1)) {
			int core_alloc = ROUNDUP(cpus_alloc, vpus);
			int used_cores =
				ROUNDUP(job_resrcs_ptr->cpus_used[job_node_inx],
					vpus);

			/* If CR_CPU is used with a thread cound > 1 the cpus
			 * recorded being allocated to a job don't have to be a
			 * multiple of threads per core. Make sure to dealloc
			 * full cores and not partial cores.
			 */

			if (used_cores >= core_alloc) {
				used_cores -= core_alloc;
				job_resrcs_ptr->cpus_used[job_node_inx] =
					MIN(used_cores * vpus,
					    job_resrcs_ptr->cpus[job_node_inx]);
			} else {
				error("%s: CPU underflow for %pS (%u<%u on job node %d)",
					__func__, step_ptr, used_cores * vpus,
					core_alloc * vpus, job_node_inx);
				job_resrcs_ptr->cpus_used[job_node_inx] = 0;
			}
		} else if (job_resrcs_ptr->cpus_used[job_node_inx] >= cpus_alloc) {
			job_resrcs_ptr->cpus_used[job_node_inx] -= cpus_alloc;
		} else {
			error("%s: CPU underflow for %pS (%u<%u on job node %d)",
			      __func__, step_ptr,
			      job_resrcs_ptr->cpus_used[job_node_inx],
			      cpus_alloc, job_node_inx);
			job_resrcs_ptr->cpus_used[job_node_inx] = 0;
		}
		if (step_ptr->memory_allocated && _is_mem_resv() &&
		    !(step_ptr->flags & SSF_MEM_ZERO)) {
			uint64_t mem_use =
				step_ptr->memory_allocated[step_node_inx];
			if (job_resrcs_ptr->memory_used[job_node_inx] >=
			    mem_use) {
				job_resrcs_ptr->memory_used[job_node_inx] -=
					mem_use;
				log_flag(STEPS, "Deallocating %"PRIu64"MB of memory on node %d (%s) now used: %"PRIu64" of %"PRIu64,
					 mem_use,
					 job_node_inx,
					 node_ptr->name,
					 job_resrcs_ptr->
					 memory_used[job_node_inx],
					 job_resrcs_ptr->
					 memory_allocated[job_node_inx]);
			} else {
				error("%s: Allocated memory underflow for %pS (freed memeory=%"PRIu64")",
				      __func__, step_ptr, mem_use);
				job_resrcs_ptr->memory_used[job_node_inx] = 0;
			}
		}
		log_flag(STEPS, "step dealloc on job node %d (%s) used: %u of %u CPUs",
			 job_node_inx, node_ptr->name,
			 job_resrcs_ptr->cpus_used[job_node_inx],
			 job_resrcs_ptr->cpus[job_node_inx]);
		if (step_node_inx == (step_ptr->step_layout->node_cnt - 1))
			break;
	}

	xassert(job_resrcs_ptr->core_bitmap);
	xassert(job_resrcs_ptr->core_bitmap_used);
	if (step_ptr->core_bitmap_job) {
		/* Mark the job's cores as no longer in use */
		int job_core_size, step_core_size;
		job_core_size  = bit_size(job_resrcs_ptr->core_bitmap_used);
		step_core_size = bit_size(step_ptr->core_bitmap_job);
		/*
		 * Don't remove step's used cores from job core_bitmap_used if
		 * SSF_OVERLAP_FORCE
		 */
		if (job_core_size == step_core_size) {
			if (!(step_ptr->flags & SSF_OVERLAP_FORCE))
				bit_and_not(job_resrcs_ptr->core_bitmap_used,
					    step_ptr->core_bitmap_job);
		} else {
			error("%s: %pS core_bitmap size mismatch (%d != %d)",
			      __func__, step_ptr, job_core_size,
			      step_core_size);
		}
		FREE_NULL_BITMAP(step_ptr->core_bitmap_job);
	}
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

/* Calculate a step's cpus_per_task value. Set to zero if we can't distributed
 * the tasks evenly over the nodes (heterogeneous job allocation). */
static int _calc_cpus_per_task(job_step_create_request_msg_t *step_specs,
			       job_record_t *job_ptr)
{
	int cpus_per_task = 0, i;
	int num_tasks;
	char *cpt = NULL;

	if ((cpt = xstrstr(step_specs->tres_per_task, "cpu:"))) {
		cpus_per_task = slurm_atoul(cpt + 4);
		if (cpus_per_task < 0)
			cpus_per_task = 0;
		return cpus_per_task;
	}

	if (step_specs->cpus_per_tres)
		return 0;
	if (step_specs->num_tasks == NO_VAL)
		return 0;

	if ((step_specs->cpu_count == 0) ||
	    (step_specs->cpu_count % step_specs->num_tasks))
		return cpus_per_task;

	cpus_per_task = step_specs->cpu_count / step_specs->num_tasks;
	if (cpus_per_task < 1)
		cpus_per_task = 1;

	if (!job_ptr->job_resrcs)
		return cpus_per_task;

	num_tasks = step_specs->num_tasks;
	for (i = 0; i < job_ptr->job_resrcs->cpu_array_cnt; i++) {
		if (cpus_per_task > job_ptr->job_resrcs->cpu_array_value[i]) {
			cpus_per_task = 0;
			break;
		}
		num_tasks -= (job_ptr->job_resrcs->cpu_array_value[i] /
			      cpus_per_task) *
			job_ptr->job_resrcs->cpu_array_reps[i];
	}

	if (num_tasks > 0)
		return 0;

	return cpus_per_task;
}

/*
 * Set a job's default cpu_bind_type based upon configuration of allocated nodes,
 * partition or global TaskPluginParams
 */
static void _set_def_cpu_bind(job_record_t *job_ptr)
{
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	node_record_t *node_ptr;
	uint32_t bind_bits, bind_to_bits, node_bind = NO_VAL;
	bool node_fail = false;

	if (!job_ptr->details || !job_resrcs_ptr ||
	    !job_resrcs_ptr->node_bitmap)
		return;		/* No data structure */

	bind_to_bits = CPU_BIND_TO_SOCKETS | CPU_BIND_TO_CORES |
		CPU_BIND_TO_THREADS | CPU_BIND_TO_LDOMS;
	if ((job_ptr->details->cpu_bind_type != NO_VAL16) &&
	    (job_ptr->details->cpu_bind_type & bind_to_bits)) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND) {
			char tmp_str[128];
			slurm_sprint_cpu_bind_type(
				tmp_str, job_ptr->details->cpu_bind_type);
			log_flag(CPU_BIND, "%pJ CpuBind='%s' already set for job/allocation using it as a default for new step.",
				 job_ptr, tmp_str);
		}
		return;		/* Already set */
	}
	bind_bits = job_ptr->details->cpu_bind_type & CPU_BIND_VERBOSE;

	/*
	 * Set job's cpu_bind to the node's cpu_bind if all of the job's
	 * allocated nodes have the same cpu_bind (or it is not set)
	 */
	for (int i = 0;
	     (node_ptr = next_node_bitmap(job_resrcs_ptr->node_bitmap, &i));
	     i++) {
		if (node_bind == NO_VAL) {
			if (node_ptr->cpu_bind != 0)
				node_bind = node_ptr->cpu_bind;
		} else if ((node_ptr->cpu_bind != 0) &&
			   (node_bind != node_ptr->cpu_bind)) {
			node_fail = true;
			break;
		}
	}
	if (!node_fail && (node_bind != NO_VAL)) {
		job_ptr->details->cpu_bind_type = bind_bits | node_bind;
		if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND) {
			char tmp_str[128];
			slurm_sprint_cpu_bind_type(
				tmp_str, job_ptr->details->cpu_bind_type);
			log_flag(CPU_BIND, "%pJ setting default CpuBind to nodes default '%s' for new step.",
				 job_ptr, tmp_str);
		}
		return;
	}

	/* Use partition's cpu_bind (if any) */
	if (job_ptr->part_ptr && job_ptr->part_ptr->cpu_bind) {
		job_ptr->details->cpu_bind_type = bind_bits |
			job_ptr->part_ptr->cpu_bind;
		if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND) {
			char tmp_str[128];
			slurm_sprint_cpu_bind_type(
				tmp_str, job_ptr->details->cpu_bind_type);
			log_flag(CPU_BIND, "%pJ setting default CpuBind to partition default '%s' for new step.",
				 job_ptr, tmp_str);

		}
		return;
	}

	/* Use global default from TaskPluginParams */
	job_ptr->details->cpu_bind_type = bind_bits |
		slurm_conf.task_plugin_param;

	if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND) {
		char tmp_str[128];
		slurm_sprint_cpu_bind_type(tmp_str,
					   job_ptr->details->cpu_bind_type);
		log_flag(CPU_BIND, "%pJ setting default CpuBind to TaskPluginParam '%s' for new step.",
			 job_ptr, tmp_str);

	}
}

/*
 * A step may explicitly set a TRES count to zero in order to avoid making use
 * of the job's TRES specifications. At this point, clear the records with
 * zero counts.
 */
static void _clear_zero_tres(char **tres_spec)
{
	char *new_spec = NULL, *new_sep = "";
	char *tmp, *tok, *sep, *end_ptr = NULL, *save_ptr = NULL;
	long int cnt;

	if (*tres_spec == NULL)
		return;

	tmp = xstrdup(*tres_spec);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		bool copy_rec = true;
		sep = strrchr(tok, ':');
		if (sep) {
			cnt = strtoll(sep+1, &end_ptr, 10);
			if ((cnt == 0) && (end_ptr[0] == '\0'))
				copy_rec = false;
		}
		if (copy_rec) {
			xstrfmtcat(new_spec, "%s%s", new_sep, tok);
			new_sep = ",";
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
	xfree(*tres_spec);
	*tres_spec = new_spec;
}

/*
 * A step may explicitly request --gres=none in order to avoid making use
 * of the job's TRES specifications. At this point, clear all GRES records.
 */
static void _clear_gres_tres(char **tres_spec)
{
	char *new_spec = NULL, *new_sep = "";
	char *tmp, *tok, *save_ptr = NULL;

	if (*tres_spec == NULL)
		return;

	tmp = xstrdup(*tres_spec);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		if (xstrncmp(tok, "gres", 4)) {
			xstrfmtcat(new_spec, "%s%s", new_sep, tok);
			new_sep = ",";
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
	xfree(*tres_spec);
	*tres_spec = new_spec;
}

/*
 * If a job step specification does not include any GRES specification,
 * then copy those values from the job record.
 * Currently we only want to check if the step lacks a "gres" request.
 * "tres_per_[step|task]" has "cpu:<count>" in it, so we need to search for
 * "gres" in the strings.
 */
static void _copy_job_tres_to_step(job_step_create_request_msg_t *step_specs,
				   job_record_t *job_ptr)
{
	if (!xstrcasecmp(step_specs->tres_per_node, "NONE")) {
		xfree(step_specs->tres_per_node);
		_clear_gres_tres(&step_specs->tres_per_step);
		_clear_gres_tres(&step_specs->tres_per_socket);
		_clear_gres_tres(&step_specs->tres_per_task);
	} else if (xstrstr(step_specs->tres_per_step, "gres")	||
		   xstrstr(step_specs->tres_per_node, "gres")	||
		   xstrstr(step_specs->tres_per_socket, "gres")	||
		   xstrstr(step_specs->tres_per_task, "gres")) {
		_clear_zero_tres(&step_specs->tres_per_step);
		_clear_zero_tres(&step_specs->tres_per_node);
		_clear_zero_tres(&step_specs->tres_per_socket);
		_clear_zero_tres(&step_specs->tres_per_task);
	} else {
		xfree(step_specs->tres_per_step);
		xfree(step_specs->tres_per_node);
		xfree(step_specs->tres_per_socket);
		xfree(step_specs->tres_per_task);
		step_specs->tres_per_step   = xstrdup(job_ptr->tres_per_job);
		step_specs->tres_per_node   = xstrdup(job_ptr->tres_per_node);
		step_specs->tres_per_socket = xstrdup(job_ptr->tres_per_socket);
		step_specs->tres_per_task   = xstrdup(job_ptr->tres_per_task);
	}
}

static int _test_step_desc_fields(job_step_create_request_msg_t *step_specs)
{
	static time_t sched_update = 0;
	static int max_submit_line = DEFAULT_MAX_SUBMIT_LINE_SIZE;

	if (sched_update != slurm_conf.last_update) {
		char *tmp_ptr;
		sched_update = slurm_conf.last_update;

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "max_submit_line_size="))) {
			max_submit_line = atoi(tmp_ptr + 21);
		} else {
			max_submit_line = DEFAULT_MAX_SUBMIT_LINE_SIZE;
		}
	}

	if (_test_strlen(step_specs->host, "host", 1024) ||
	    _test_strlen(step_specs->name, "name", 1024) ||
	    _test_strlen(step_specs->network, "network", 1024) ||
	    _test_strlen(step_specs->submit_line, "submit_line",
			 max_submit_line))
		return ESLURM_PATHNAME_TOO_LONG;
	return SLURM_SUCCESS;
}

static int _switch_setup(step_record_t *step_ptr)
{
	xassert(step_ptr);

	if (!step_ptr->step_layout)
		return SLURM_SUCCESS;

	errno = 0;
	if (switch_g_build_stepinfo(&step_ptr->switch_step,
				    step_ptr->step_layout,
				    step_ptr) < 0) {
		if (errno == ESLURM_INTERCONNECT_BUSY)
			return errno;
		return ESLURM_INTERCONNECT_FAILURE;
	}
	return SLURM_SUCCESS;
}

extern int step_create(job_record_t *job_ptr,
		       job_step_create_request_msg_t *step_specs,
		       step_record_t** new_step_record,
		       uint16_t protocol_version, char **err_msg)
{
	step_record_t *step_ptr;
	bitstr_t *nodeset;
	int cpus_per_task, ret_code, i;
	uint32_t node_count = 0;
	time_t now = time(NULL);
	char *step_node_list = NULL;
	uint32_t orig_cpu_count;
	list_t *step_gres_list = NULL;
	dynamic_plugin_data_t *select_jobinfo = NULL;
	uint32_t task_dist;
	uint32_t max_tasks;
	uint32_t over_time_limit;
	bool resv_ports_present = false;

	*new_step_record = NULL;

	xassert(job_ptr);

	/*
	 * NOTE: We have already confirmed the UID originating
	 * the request is identical with step_specs->user_id
	 */
	if (step_specs->user_id != job_ptr->user_id)
		return ESLURM_ACCESS_DENIED ;

	if (step_specs->step_id.step_id != NO_VAL) {
		if (list_delete_first(job_ptr->step_list,
				      _purge_duplicate_steps,
				      step_specs) < 0)
			return ESLURM_DUPLICATE_STEP_ID;
	}

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

	/* Get OverTimeLimit from job's partition if set, or globally. */
	if (job_ptr->part_ptr &&
	    (job_ptr->part_ptr->over_time_limit != NO_VAL16))
		over_time_limit = job_ptr->part_ptr->over_time_limit;
	else
		over_time_limit = slurm_conf.over_time_limit;

	if (over_time_limit == INFINITE16)
		over_time_limit = YEAR_MINUTES;

	if (IS_JOB_FINISHED(job_ptr) ||
	    (((job_ptr->end_time + (over_time_limit * 60)) <= time(NULL)) &&
	     !IS_JOB_CONFIGURING(job_ptr)))
		return ESLURM_ALREADY_DONE;

	if (job_ptr->details->prolog_running)
		return ESLURM_PROLOG_RUNNING;

	if (step_specs->flags & SSF_INTERACTIVE) {
		debug("%s: interactive step requested", __func__);
		*new_step_record = _build_interactive_step(job_ptr, step_specs,
							   protocol_version);
		if (*new_step_record)
			return SLURM_SUCCESS;
		else
			return ESLURM_DUPLICATE_STEP_ID;
	}

	if (step_specs->flags & SSF_EXT_LAUNCHER) {
		debug("%s: external launcher step requested", __func__);
		return _build_ext_launcher_step(new_step_record, job_ptr,
						step_specs, protocol_version);
	}

	/* A step cannot request more threads per core than its allocation. */
	if ((step_specs->threads_per_core != NO_VAL16) &&
	    (step_specs->threads_per_core >
	     job_ptr->job_resrcs->threads_per_core))
		return ESLURM_BAD_THREAD_PER_CORE;

	task_dist = step_specs->task_dist & SLURM_DIST_STATE_BASE;
	/* Set to block in the case that mem is 0. srun leaves the dist
	 * set to unknown if mem is 0. */
	if ((task_dist == SLURM_DIST_UNKNOWN) &&
	    (!(step_specs->pn_min_memory &(~MEM_PER_CPU)))) {
		step_specs->task_dist &= SLURM_DIST_STATE_FLAGS;
		step_specs->task_dist |= SLURM_DIST_BLOCK;
		task_dist = SLURM_DIST_BLOCK;
	}

	if ((task_dist != SLURM_DIST_CYCLIC) &&
	    (task_dist != SLURM_DIST_BLOCK) &&
	    (task_dist != SLURM_DIST_CYCLIC_CYCLIC) &&
	    (task_dist != SLURM_DIST_BLOCK_CYCLIC) &&
	    (task_dist != SLURM_DIST_CYCLIC_BLOCK) &&
	    (task_dist != SLURM_DIST_BLOCK_BLOCK) &&
	    (task_dist != SLURM_DIST_CYCLIC_CFULL) &&
	    (task_dist != SLURM_DIST_BLOCK_CFULL) &&
	    (task_dist != SLURM_DIST_CYCLIC_CYCLIC_CYCLIC) &&
	    (task_dist != SLURM_DIST_CYCLIC_CYCLIC_BLOCK) &&
	    (task_dist != SLURM_DIST_CYCLIC_CYCLIC_CFULL) &&
	    (task_dist != SLURM_DIST_CYCLIC_BLOCK_CYCLIC) &&
	    (task_dist != SLURM_DIST_CYCLIC_BLOCK_BLOCK) &&
	    (task_dist != SLURM_DIST_CYCLIC_BLOCK_CFULL) &&
	    (task_dist != SLURM_DIST_CYCLIC_CFULL_CYCLIC) &&
	    (task_dist != SLURM_DIST_CYCLIC_CFULL_BLOCK) &&
	    (task_dist != SLURM_DIST_CYCLIC_CFULL_CFULL) &&
	    (task_dist != SLURM_DIST_BLOCK_CYCLIC_CYCLIC) &&
	    (task_dist != SLURM_DIST_BLOCK_CYCLIC_BLOCK) &&
	    (task_dist != SLURM_DIST_BLOCK_CYCLIC_CFULL) &&
	    (task_dist != SLURM_DIST_BLOCK_BLOCK_CYCLIC) &&
	    (task_dist != SLURM_DIST_BLOCK_BLOCK_BLOCK) &&
	    (task_dist != SLURM_DIST_BLOCK_BLOCK_CFULL) &&
	    (task_dist != SLURM_DIST_BLOCK_CFULL_CYCLIC) &&
	    (task_dist != SLURM_DIST_BLOCK_CFULL_BLOCK) &&
	    (task_dist != SLURM_DIST_BLOCK_CFULL_CFULL) &&
	    (task_dist != SLURM_DIST_PLANE) &&
	    (task_dist != SLURM_DIST_ARBITRARY))
		return ESLURM_BAD_DIST;

	if (!assoc_mgr_valid_tres_cnt(step_specs->cpus_per_tres, 0) ||
	    !assoc_mgr_valid_tres_cnt(step_specs->mem_per_tres, 0) ||
	    tres_bind_verify_cmdline(step_specs->tres_bind) ||
	    tres_freq_verify_cmdline(step_specs->tres_freq) ||
	    !assoc_mgr_valid_tres_cnt(step_specs->tres_per_step, 0) ||
	    (!assoc_mgr_valid_tres_cnt(step_specs->tres_per_node, 0) &&
	     xstrcasecmp(step_specs->tres_per_node, "NONE")) ||
	    !assoc_mgr_valid_tres_cnt(step_specs->tres_per_socket, 0) ||
	    !assoc_mgr_valid_tres_cnt(step_specs->tres_per_task, 0))
		return ESLURM_INVALID_TRES;

	if ((ret_code = _test_step_desc_fields(step_specs)) != SLURM_SUCCESS)
		return ret_code;

	if (job_ptr->next_step_id >= slurm_conf.max_step_cnt)
		return ESLURM_STEP_LIMIT;

	/*
	 * If the overcommit flag is checked, we set cpu_count=0
	 * which makes it so we don't check to see the available cpus
	 */
	orig_cpu_count =  step_specs->cpu_count;

	if (step_specs->flags & SSF_OVERCOMMIT)
		step_specs->cpu_count = 0;

	if (!step_specs->ntasks_per_tres)
		step_specs->ntasks_per_tres = NO_VAL16;

	/* determine cpus_per_task value by reversing what srun does */
	if (step_specs->num_tasks < 1)
		return ESLURM_BAD_TASK_COUNT;

	cpus_per_task = _calc_cpus_per_task(step_specs, job_ptr);

	_copy_job_tres_to_step(step_specs, job_ptr);

	/* If whole is given we probably need to copy tres_per_* from the job */
	i = gres_step_state_validate(step_specs->cpus_per_tres,
				     step_specs->tres_per_step,
				     step_specs->tres_per_node,
				     step_specs->tres_per_socket,
				     step_specs->tres_per_task,
				     step_specs->mem_per_tres,
				     step_specs->ntasks_per_tres,
				     step_specs->min_nodes,
				     &step_gres_list,
				     job_ptr->job_id,
				     NO_VAL, &step_specs->num_tasks,
				     &step_specs->cpu_count, err_msg);
	if (i != SLURM_SUCCESS) {
		FREE_NULL_LIST(step_gres_list);
		return i;
	}

	job_ptr->time_last_active = now;

	/* make sure select_jobinfo exists to avoid xassert */
	select_jobinfo = select_g_select_jobinfo_alloc();
	nodeset = _pick_step_nodes(job_ptr, step_specs, step_gres_list,
				   cpus_per_task, node_count, select_jobinfo,
				   &ret_code);
	if (nodeset == NULL) {
		FREE_NULL_LIST(step_gres_list);
		select_g_select_jobinfo_free(select_jobinfo);
		if ((ret_code == ESLURM_NODES_BUSY) ||
		    (ret_code == ESLURM_PORTS_BUSY) ||
		    (ret_code == ESLURM_INTERCONNECT_BUSY))
			_build_pending_step(job_ptr, step_specs);
		return ret_code;
	}
	_set_def_cpu_bind(job_ptr);

	node_count = bit_set_count(nodeset);
	xassert(step_specs->num_tasks != NO_VAL);

	max_tasks = node_count * slurm_conf.max_tasks_per_node;
	if (step_specs->num_tasks > max_tasks) {
		error("step has invalid task count: %u max is %u",
		      step_specs->num_tasks, max_tasks);
		FREE_NULL_LIST(step_gres_list);
		FREE_NULL_BITMAP(nodeset);
		select_g_select_jobinfo_free(select_jobinfo);
		return ESLURM_BAD_TASK_COUNT;
	}

	step_ptr = create_step_record(job_ptr, protocol_version);
	if (step_ptr == NULL) {
		FREE_NULL_LIST(step_gres_list);
		FREE_NULL_BITMAP(nodeset);
		select_g_select_jobinfo_free(select_jobinfo);
		return ESLURMD_TOOMANYSTEPS;
	}
	*stepmgr_ops->last_job_update = time(NULL);

	step_ptr->start_time = time(NULL);
	step_ptr->state      = JOB_RUNNING;

	memcpy(&step_ptr->step_id, &step_specs->step_id,
	       sizeof(step_ptr->step_id));

	if (step_specs->array_task_id != NO_VAL)
		step_ptr->step_id.job_id = job_ptr->job_id;

	if (step_specs->step_id.step_id != NO_VAL) {
		if (step_specs->step_id.step_het_comp == NO_VAL) {
			job_ptr->next_step_id =
				MAX(job_ptr->next_step_id,
				    step_specs->step_id.step_id);
			job_ptr->next_step_id++;
		}
	} else if (job_ptr->het_job_id &&
		   (job_ptr->het_job_id != job_ptr->job_id)) {
		job_record_t *het_job;
		het_job = stepmgr_ops->find_job_record(job_ptr->het_job_id);
		if (het_job)
			step_ptr->step_id.step_id = het_job->next_step_id++;
		else
			step_ptr->step_id.step_id = job_ptr->next_step_id++;
		job_ptr->next_step_id = MAX(job_ptr->next_step_id,
					    step_ptr->step_id.step_id);
	} else {
		step_ptr->step_id.step_id = job_ptr->next_step_id++;
	}

	/* Here is where the node list is set for the step */
	if (step_specs->node_list &&
	    ((step_specs->task_dist & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY)) {
		step_node_list = xstrdup(step_specs->node_list);
		xfree(step_specs->node_list);
		step_specs->node_list = bitmap2node_name(nodeset);
	} else {
		step_node_list = bitmap2node_name_sortable(nodeset, false);
		xfree(step_specs->node_list);
		step_specs->node_list = xstrdup(step_node_list);
	}
	log_flag(STEPS, "Picked nodes %s when accumulating from %s",
		 step_node_list, step_specs->node_list);
	step_ptr->step_node_bitmap = nodeset;

	switch (step_specs->task_dist & SLURM_DIST_NODESOCKMASK) {
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

	step_ptr->container = xstrdup(step_specs->container);
	step_ptr->container_id = xstrdup(step_specs->container_id);
	step_ptr->gres_list_req = step_gres_list;
	step_gres_list = NULL;
	gres_step_state_log(step_ptr->gres_list_req, job_ptr->job_id,
			    step_ptr->step_id.step_id);
	if ((slurm_conf.debug_flags & DEBUG_FLAG_GRES) &&
	    step_ptr->gres_list_alloc)
		info("Step Alloc GRES:");
	gres_step_state_log(step_ptr->gres_list_alloc, job_ptr->job_id,
			    step_ptr->step_id.step_id);

	step_ptr->port = step_specs->port;
	step_ptr->srun_pid = step_specs->srun_pid;
	step_ptr->host = xstrdup(step_specs->host);
	if ((step_specs->cpu_freq_min == NO_VAL) &&
	    (step_specs->cpu_freq_max == NO_VAL) &&
	    (step_specs->cpu_freq_gov == NO_VAL)) {
		step_ptr->cpu_freq_min = job_ptr->details->cpu_freq_min;
		step_ptr->cpu_freq_max = job_ptr->details->cpu_freq_max;
		step_ptr->cpu_freq_gov = job_ptr->details->cpu_freq_gov;
	} else {
		step_ptr->cpu_freq_min = step_specs->cpu_freq_min;
		step_ptr->cpu_freq_max = step_specs->cpu_freq_max;
		step_ptr->cpu_freq_gov = step_specs->cpu_freq_gov;
	}
	step_ptr->cpus_per_task = (uint16_t)cpus_per_task;
	step_ptr->ntasks_per_core = step_specs->ntasks_per_core;
	step_ptr->pn_min_memory = step_specs->pn_min_memory;
	/*
	 * cpu_count can be updated by gres_step_state_validate() if OVERCOMMIT
	 * is not used. If so, use the updated value.
	 */
	if (step_specs->flags & SSF_OVERCOMMIT)
		step_ptr->cpu_count = orig_cpu_count;
	else
		step_ptr->cpu_count = step_specs->cpu_count;
	step_ptr->exit_code = NO_VAL;
	step_ptr->flags = step_specs->flags;

	step_ptr->cpus_per_tres = xstrdup(step_specs->cpus_per_tres);
	step_ptr->mem_per_tres = xstrdup(step_specs->mem_per_tres);
	step_ptr->submit_line = xstrdup(step_specs->submit_line);
	step_ptr->tres_bind = xstrdup(step_specs->tres_bind);
	step_ptr->tres_freq = xstrdup(step_specs->tres_freq);
	step_ptr->tres_per_step = xstrdup(step_specs->tres_per_step);
	step_ptr->tres_per_node = xstrdup(step_specs->tres_per_node);
	step_ptr->tres_per_socket = xstrdup(step_specs->tres_per_socket);
	step_ptr->tres_per_task = xstrdup(step_specs->tres_per_task);

	step_ptr->threads_per_core = step_specs->threads_per_core;

	/*
	 * step's name and network default to job's values if not
	 * specified in the step specification
	 */
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

	/*
	 * the step time_limit is recorded as submitted (INFINITE
	 * or partition->max_time by default), but the allocation
	 * time limits may cut it short
	 */
	if (step_specs->time_limit == NO_VAL || step_specs->time_limit == 0 ||
	    step_specs->time_limit == INFINITE) {
		step_ptr->time_limit = INFINITE;
	} else {
		/* enforce partition limits if necessary */
		if ((step_specs->time_limit > job_ptr->part_ptr->max_time) &&
		    slurm_conf.enforce_part_limits) {
			info("%s: %pS time greater than partition's (%u > %u)",
			     __func__, step_ptr, step_specs->time_limit,
			     job_ptr->part_ptr->max_time);
			delete_step_record(job_ptr, step_ptr);
			xfree(step_node_list);
			return ESLURM_INVALID_TIME_LIMIT;
		}
		step_ptr->time_limit = step_specs->time_limit;
	}

	step_ptr->step_layout =
		step_layout_create(step_ptr,
				   step_node_list, node_count,
				   step_specs->num_tasks,
				   (uint16_t)cpus_per_task,
				   step_specs->task_dist,
				   step_specs->plane_size);
	xfree(step_node_list);
	if (!step_ptr->step_layout) {
		delete_step_record(job_ptr, step_ptr);
		if (step_specs->pn_min_memory)
			return ESLURM_INVALID_TASK_MEMORY;
		return SLURM_ERROR;
	}
	if (slurm_conf.mpi_params && xstrstr(slurm_conf.mpi_params, "ports="))
		resv_ports_present = true;
	if ((step_specs->resv_port_cnt == NO_VAL16) &&
	    (resv_ports_present || job_ptr->resv_ports)) {
		step_specs->resv_port_cnt = 0;
		/*
		 * reserved port count set to maximum task count on
		 * any node plus one
		 */
		for (i = 0; i < step_ptr->step_layout->node_cnt; i++) {
			step_specs->resv_port_cnt =
				MAX(step_specs->resv_port_cnt,
				    step_ptr->step_layout->tasks[i]);
		}
		step_specs->resv_port_cnt++;
	}
	if ((step_specs->resv_port_cnt != NO_VAL16) &&
	    (step_specs->resv_port_cnt != 0)) {
		step_ptr->resv_port_cnt = step_specs->resv_port_cnt;
		i = resv_port_step_alloc(step_ptr);
		if (i != SLURM_SUCCESS) {
			delete_step_record(job_ptr, step_ptr);
			return i;
		}
	}

	if ((ret_code = _switch_setup(step_ptr))) {
		delete_step_record(job_ptr, step_ptr);
		return ret_code;
	}

	if ((ret_code = _step_alloc_lps(step_ptr, err_msg))) {
		delete_step_record(job_ptr, step_ptr);
		return ret_code;
	}

	xassert(bit_set_count(step_ptr->core_bitmap_job) != 0);

	*new_step_record = step_ptr;

	select_g_step_start(step_ptr);


	step_set_alloc_tres(step_ptr, node_count, false, true);
	jobacct_storage_g_step_start(stepmgr_ops->acct_db_conn, step_ptr);
	return SLURM_SUCCESS;
}

extern slurm_step_layout_t *step_layout_create(step_record_t *step_ptr,
					       char *step_node_list,
					       uint32_t node_count,
					       uint32_t num_tasks,
					       uint16_t cpus_per_task,
					       uint32_t task_dist,
					       uint16_t plane_size)
{
	slurm_step_layout_t *step_layout = NULL;
	uint16_t cpus_per_node[node_count];
	uint16_t cpus_per_task_array[node_count];
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	slurm_step_layout_req_t step_layout_req;
	uint64_t gres_cpus;
	int cpu_inx = -1, cpus_task_inx = -1;
	int usable_cpus, usable_mem;
	int set_nodes = 0/* , set_tasks = 0 */;
	int pos = -1;
	uint32_t cpu_count_reps[node_count];
	uint32_t cpus_task_reps[node_count];
	uint32_t cpus_task = 0;
	uint16_t ntasks_per_core = step_ptr->ntasks_per_core;
	uint16_t ntasks_per_socket = 0;
	node_record_t *node_ptr;
	gres_stepmgr_step_test_args_t gres_test_args = {
		.cpus_per_task = step_ptr->cpus_per_task,
		.first_step_node = true,
		.job_gres_list = job_ptr->gres_list_alloc,
		.job_id = job_ptr->job_id,
		.job_resrcs_ptr = job_resrcs_ptr,
		.node_offset = -1,
		.step_gres_list = step_ptr->gres_list_req,
		.step_id = step_ptr->step_id.step_id,
		.test_mem = false,
	};

	xassert(job_resrcs_ptr);
	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	if (step_ptr->pn_min_memory && _is_mem_resv() &&
	    ((job_resrcs_ptr->memory_allocated == NULL) ||
	     (job_resrcs_ptr->memory_used == NULL))) {
		error("%s: lack memory allocation details to enforce memory limits for %pJ",
		      __func__, job_ptr);
		step_ptr->pn_min_memory = 0;
	} else if (step_ptr->pn_min_memory == MEM_PER_CPU)
		step_ptr->pn_min_memory = 0;	/* clear MEM_PER_CPU flag */

#ifdef HAVE_FRONT_END
	if (step_ptr->job_ptr->front_end_ptr &&
	    (step_ptr->start_protocol_ver >
	     step_ptr->job_ptr->front_end_ptr->protocol_version))
		step_ptr->start_protocol_ver =
			step_ptr->job_ptr->front_end_ptr->protocol_version;
#endif

	/* build cpus-per-node arrays for the subset of nodes used by step */
	gres_test_args.max_rem_nodes =
		bit_set_count(step_ptr->step_node_bitmap);
	for (int i = 0; (node_ptr = next_node_bitmap(job_ptr->node_bitmap, &i));
	     i++) {
		uint16_t cpus, cpus_used;
		int err_code = SLURM_SUCCESS;
		node_record_t *node_ptr;

		gres_test_args.test_mem = false;
		gres_test_args.err_code = &err_code;
		gres_test_args.node_offset++;
		if (!bit_test(step_ptr->step_node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr[i];

#ifndef HAVE_FRONT_END
		if (step_ptr->start_protocol_ver > node_ptr->protocol_version)
			step_ptr->start_protocol_ver =
				node_ptr->protocol_version;
#endif

		/* find out the position in the job */
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			return NULL;
		pos = bit_set_count_range(job_resrcs_ptr->node_bitmap, 0, i);
		if (pos >= job_resrcs_ptr->nhosts)
			fatal("%s: node index bad", __func__);

		cpus = job_resrcs_ptr->cpus[pos];
		cpus_used = job_resrcs_ptr->cpus_used[pos];
		/*
		 * Here we are trying to figure out the number
		 * of cpus available if we only want to run 1
		 * thread per core.
		 */
		if (_use_one_thread_per_core(step_ptr)) {
			uint16_t threads;
			threads = node_ptr->config_ptr->threads;

			cpus /= threads;
			cpus_used /= threads;
			cpus_per_task_array[0] = cpus_per_task;
			cpus_task_reps[0] = node_count;
		} else {
			/*
			 * Here we are trying to figure out how many
			 * CPUs each task really needs. This really
			 * only becomes an issue if the job requested
			 * ntasks_per_core|socket=1. We just increase
			 * the number of cpus_per_task to the thread
			 * count. Since the system could be
			 * heterogeneous, we needed to make this an
			 * array.
			 */
			uint16_t threads_per_core;
			multi_core_data_t *mc_ptr = NULL;

			if (job_ptr->details)
				mc_ptr = job_ptr->details->mc_ptr;

			if (step_ptr->threads_per_core != NO_VAL16)
				threads_per_core = step_ptr->threads_per_core;
			else if (mc_ptr &&
				 (mc_ptr->threads_per_core != NO_VAL16))
				threads_per_core = mc_ptr->threads_per_core;
			else
				threads_per_core =
					node_ptr->config_ptr->threads;
			if (ntasks_per_socket == 1) {
				uint16_t threads_per_socket;
				threads_per_socket =
					node_ptr->config_ptr->cores;
				threads_per_socket *= threads_per_core;

				if (cpus_per_task < threads_per_socket)
					cpus_task = threads_per_socket;
			} else if ((ntasks_per_core == 1) &&
				   (cpus_per_task < threads_per_core))
				cpus_task = threads_per_core;
			else
				cpus_task = cpus_per_task;

			if ((cpus_task_inx == -1) ||
			    (cpus_per_task_array[cpus_task_inx] != cpus_task)) {
				cpus_task_inx++;
				cpus_per_task_array[cpus_task_inx] = cpus_task;
				cpus_task_reps[cpus_task_inx] = 1;
			} else
				cpus_task_reps[cpus_task_inx]++;
		}

		if (step_ptr->flags & SSF_OVERLAP_FORCE)
			usable_cpus = cpus;
		else
			usable_cpus = cpus - cpus_used;

		if (usable_cpus <= 0)
			continue;

		if ((step_ptr->pn_min_memory & MEM_PER_CPU) && _is_mem_resv()) {
			uint64_t mem_use = step_ptr->pn_min_memory;
			mem_use &= (~MEM_PER_CPU);
			usable_mem = job_resrcs_ptr->memory_allocated[pos] -
				     job_resrcs_ptr->memory_used[pos];
			usable_mem /= mem_use;
			usable_cpus = MIN(usable_cpus, usable_mem);
		} else if ((!step_ptr->pn_min_memory) && _is_mem_resv()) {
			gres_test_args.test_mem = true;
		}

		if (step_ptr->flags & SSF_OVERLAP_FORCE)
			gres_test_args.ignore_alloc = true;
		else
			gres_test_args.ignore_alloc = false;

		gres_cpus = gres_stepmgr_step_test(&gres_test_args);
		if (usable_cpus > gres_cpus)
			usable_cpus = gres_cpus;
		if (usable_cpus <= 0) {
			error("%s: no usable CPUs", __func__);
			return NULL;
		}
		debug3("step_layout cpus = %d pos = %d", usable_cpus, pos);

		if ((cpu_inx == -1) ||
		    (cpus_per_node[cpu_inx] != usable_cpus)) {
			cpu_inx++;

			cpus_per_node[cpu_inx] = usable_cpus;
			cpu_count_reps[cpu_inx] = 1;
		} else
			cpu_count_reps[cpu_inx]++;
		set_nodes++;
		gres_test_args.first_step_node = false;
		gres_test_args.max_rem_nodes--;

#if 0
		/*
		 * FIXME: on a heterogeneous system running the
		 * select/linear plugin we could get a node that doesn't
		 * have as many CPUs as we decided we needed for each
		 * task. This would result in not getting a task for
		 * the node we selected. This is usually in error. This
		 * only happens when the person doesn't specify how many
		 * cpus_per_task they want, and we have to come up with
		 * a number, in this case it is wrong.
		 */
		if (cpus_per_task > 0) {
			set_tasks +=
				(uint16_t)usable_cpus / cpus_per_task;
		} else {
			/*
			 * Since cpus_per_task is 0, we just add the
			 * count of CPUs available for this job
			 */
			set_tasks += usable_cpus;
		}
		info("usable_cpus is %d and set_tasks %d %d",
		     usable_cpus, set_tasks, cpus_per_task);
#endif
		if (set_nodes == node_count)
			break;
	}

	/* if (set_tasks < num_tasks) { */
	/*	error("Resources only available for %u of %u tasks", */
	/*	     set_tasks, num_tasks); */
	/*	return NULL; */
	/* } */

	/* layout the tasks on the nodes */
	memset(&step_layout_req, 0, sizeof(slurm_step_layout_req_t));
	step_layout_req.node_list = step_node_list;
	step_layout_req.cpus_per_node = cpus_per_node;
	step_layout_req.cpu_count_reps = cpu_count_reps;
	step_layout_req.cpus_per_task = cpus_per_task_array;
	step_layout_req.cpus_task_reps = cpus_task_reps;
	step_layout_req.num_hosts = node_count;
	step_layout_req.num_tasks = num_tasks;
	step_layout_req.task_dist = task_dist;
	step_layout_req.plane_size = plane_size;

	if ((step_layout = slurm_step_layout_create(&step_layout_req))) {
		step_layout->start_protocol_ver = step_ptr->start_protocol_ver;

		if (job_ptr->node_addrs)
			step_layout->alias_addrs = build_alias_addrs(job_ptr);
	}

	return step_layout;
}

typedef struct {
	list_t *dealloc_steps;
	node_record_t *node_ptr;
	bool node_fail;
} kill_step_on_node_args_t;

static int _kill_step_on_node(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	kill_step_on_node_args_t *args = (kill_step_on_node_args_t *) arg;
	int step_node_inx = 0;
	int bit_position = args->node_ptr->index;
	int rem = 0;
	uint32_t step_rc = 0;
	step_complete_msg_t req;

	if (step_ptr->state != JOB_RUNNING)
		return 0;
	if (!bit_test(step_ptr->step_node_bitmap, bit_position))
		return 0;

	/* Remove step allocation from the job's allocation */
	step_node_inx = bit_set_count_range(step_ptr->step_node_bitmap, 0,
					    bit_position);

	memset(&req, 0, sizeof(step_complete_msg_t));
	memcpy(&req.step_id, &step_ptr->step_id, sizeof(req.step_id));

	req.range_first = step_node_inx;
	req.range_last = step_node_inx;
	req.step_rc = 9;
	req.jobacct = NULL;	/* No accounting */
	(void) _step_partial_comp(step_ptr, &req, false, &rem, &step_rc);

	/*
	 * Do not kill the extern step on all nodes, only on the nodes that
	 * failed. Otherwise things that rely on the extern step such as x11
	 * or job_container/tmpfs won't work on the remaining nodes in the
	 * allocation.
	 */
	if (args->node_fail && !(step_ptr->flags & SSF_NO_KILL) &&
	    (step_ptr->step_id.step_id != SLURM_EXTERN_CONT)) {
		info("Killing %pS due to failed node %s",
		     step_ptr, args->node_ptr->name);

		/*
		 * Never signal tasks on a front_end system.
		 * Otherwise signal step on all nodes
		 */
#ifndef HAVE_FRONT_END
		signal_step_tasks(step_ptr, SIGKILL, REQUEST_TERMINATE_TASKS);
#endif
	} else {
		info("Killing %pS on failed node %s",
		     step_ptr, args->node_ptr->name);
		signal_step_tasks_on_node(args->node_ptr->name, step_ptr,
					  SIGKILL, REQUEST_TERMINATE_TASKS);
	}

	if (!rem) {
		if (!args->dealloc_steps)
			args->dealloc_steps = list_create(NULL);
		list_append(args->dealloc_steps, step_ptr);
	}

	return 0;
}

/*
 * kill_step_on_node - determine if the specified job has any job steps
 *	allocated to the specified node and kill them unless no_kill flag
 *	is set on the step
 * IN job_ptr - pointer to an active job record
 * IN node_ptr - pointer to a node record
 * IN node_fail - true of removed node has failed
 */
extern void kill_step_on_node(job_record_t *job_ptr, node_record_t *node_ptr,
			      bool node_fail)
{
	kill_step_on_node_args_t args = {
		.dealloc_steps = NULL,
		.node_ptr = node_ptr,
		.node_fail = node_fail,
	};

	if (!job_ptr || !node_ptr)
		return;

	list_for_each(job_ptr->step_list, _kill_step_on_node, &args);

	if (args.dealloc_steps) {
		/*
		 * Because _finish_step_comp() may free the step_ptr, call
		 * list_delete_all() to delete the list-node when the step_ptr
		 * is free'd. It doesn't actually matter because we are
		 * deleting the list immediately afterward, but it is good
		 * practice to not leave invalid pointer references.
		 */
		list_delete_all(args.dealloc_steps, _finish_step_comp, NULL);
		FREE_NULL_LIST(args.dealloc_steps);
	}
}

/*
 * step_partial_comp - Note the completion of a job step on at least
 *	some of its nodes
 * IN req     - step_completion_msg RPC from slurmstepd
 * IN uid     - UID issuing the request
 * IN finish  - If true, no error, and no rem is 0 finish the step.
 * OUT rem    - count of nodes for which responses are still pending
 * OUT max_rc - highest return code for any step thus far
 * RET 0 on success, otherwise ESLURM error code
 */
extern int step_partial_comp(step_complete_msg_t *req, uid_t uid, bool finish,
			     int *rem, uint32_t *max_rc)
{
	job_record_t *job_ptr;
	step_record_t *step_ptr;

	xassert(rem);

	/* find the job, step, and validate input */
	job_ptr = stepmgr_ops->find_job_record(req->step_id.job_id);
	if (job_ptr == NULL) {
		info("%s: JobId=%u invalid", __func__, req->step_id.job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	/* If we are requeuing the job the completing flag will be set
	 * but the state will be Pending, so don't use IS_JOB_PENDING
	 * which won't see the completing flag.
	 */
	if (job_ptr->job_state == JOB_PENDING) {
		info("%s: %pJ pending", __func__, job_ptr);
		return ESLURM_JOB_PENDING;
	}

	if ((!validate_slurm_user(uid)) && (uid != job_ptr->user_id)) {
		/* Normally from slurmstepd, from srun on some failures */
		error("Security violation: REQUEST_STEP_COMPLETE RPC for %pJ from uid=%u",
		      job_ptr, (unsigned int) uid);
		return ESLURM_USER_ID_MISSING;
	}

	step_ptr = find_step_record(job_ptr, &req->step_id);

	if (step_ptr == NULL) {
		info("step_partial_comp: %pJ StepID=%u invalid; this step may have already completed",
		     job_ptr, req->step_id.step_id);
		return ESLURM_INVALID_JOB_ID;
	}
	if (req->range_last < req->range_first) {
		error("%s: %pS range=%u-%u",
		      __func__, step_ptr, req->range_first, req->range_last);
		return EINVAL;
	}

	return _step_partial_comp(step_ptr, req, finish, rem, max_rc);
}

static int _step_partial_comp(step_record_t *step_ptr,
			      step_complete_msg_t *req, bool finish,
			      int *rem, uint32_t *max_rc)
{
	int nodes, rem_nodes;
#ifndef HAVE_FRONT_END
	int range_bits, set_bits;
#endif

	if (step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT) {
		error("%s: batch step received for %pJ. This should never happen.",
		      __func__, step_ptr->job_ptr);
		return ESLURM_INVALID_JOB_ID;
	}

	/* we have been adding task average frequencies for
	 * jobacct->act_cpufreq so we need to divide with the
	 * total number of tasks/cpus for the step average frequency */
	if (step_ptr->cpu_count && step_ptr->jobacct)
		step_ptr->jobacct->act_cpufreq /= step_ptr->cpu_count;

	if (!step_ptr->exit_node_bitmap) {
		/* initialize the node bitmap for exited nodes */
		nodes = bit_set_count(step_ptr->step_node_bitmap);
		step_ptr->exit_node_bitmap = bit_alloc(nodes);
		step_ptr->exit_code = req->step_rc;
	} else {
		nodes = bit_size(step_ptr->exit_node_bitmap);
		if ((req->step_rc == SIG_OOM) ||
		    (req->step_rc > step_ptr->exit_code))
			step_ptr->exit_code = req->step_rc;
	}
	if ((req->range_first >= nodes) || (req->range_last >= nodes) ||
	    (req->range_first > req->range_last)) {
		/* range is zero origin */
		error("%s: %pS range=%u-%u nodes=%d",
		      __func__, step_ptr, req->range_first, req->range_last,
		      nodes);
		return EINVAL;
	}

	if ((step_ptr->flags & SSF_NO_SIG_FAIL) && WIFSIGNALED(req->step_rc)) {
		step_ptr->exit_code = 0;
	}

#ifdef HAVE_FRONT_END
	bit_set_all(step_ptr->exit_node_bitmap);
	rem_nodes = 0;
#else
	range_bits = req->range_last + 1 - req->range_first;
	set_bits = bit_set_count_range(step_ptr->exit_node_bitmap,
				       req->range_first,
				       req->range_last + 1);

	/* Check if any stepd of the range was already received */
	if (set_bits) {
		/* If all are already received skip jobacctinfo_aggregate */
		if (set_bits == range_bits) {
			debug("Step complete from %d to %d was already processed. Probably a RPC was resent from a child.",
			      req->range_first, req->range_last);
			goto no_aggregate;
		}

		/*
		 * If partially received, we cannot recover the right gathered
		 * information. If we don't gather the new one we'll miss some
		 * information, and if we gather it some of the info will be
		 * duplicated. We log that error and chose to partially
		 * duplicate because it's probably a smaller error.
		 */
		error("Step complete from %d to %d was already processed (%d of %d). Probably a RPC was resent from a child and gathered information is partially duplicated.",
		      req->range_first, req->range_last,
		      set_bits, range_bits);
	}

	bit_nset(step_ptr->exit_node_bitmap,
		 req->range_first, req->range_last);

#endif

	jobacctinfo_aggregate(step_ptr->jobacct, req->jobacct);

#ifndef HAVE_FRONT_END
no_aggregate:
	rem_nodes = bit_clear_count(step_ptr->exit_node_bitmap);
#endif

	*rem = rem_nodes;
	if (rem_nodes == 0) {
		/* release all switch windows */
		if (step_ptr->switch_step) {
			debug2("full switch release for %pS, nodes %s",
			       step_ptr, step_ptr->step_layout->node_list);
			switch_g_job_step_complete(
				step_ptr->switch_step,
				step_ptr->step_layout->node_list);
			switch_g_free_stepinfo(step_ptr->switch_step);
			step_ptr->switch_step = NULL;
		}
	}

	if (max_rc)
		*max_rc = step_ptr->exit_code;

	if (req->step_rc == ESLURMD_EXECVE_FAILED)
		step_ptr->state = JOB_NODE_FAIL;

	/* The step has finished, finish it completely */
	if (!*rem && finish) {
		(void) _finish_step_comp(step_ptr, NULL);
	}

	return SLURM_SUCCESS;
}

/*
 * step_set_alloc_tres - set the tres up when allocating the step.
 * Only set when job is running.
 */
extern void step_set_alloc_tres(step_record_t *step_ptr, uint32_t node_count,
				bool assoc_mgr_locked, bool make_formatted)
{
	uint64_t cpu_count = 1, mem_count = 0;
	char *tmp_tres_str = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };
	job_record_t *job_ptr = step_ptr->job_ptr;

	xassert(step_ptr);

	xfree(step_ptr->tres_alloc_str);
	xfree(step_ptr->tres_fmt_alloc_str);

	if (((step_ptr->step_id.step_id == SLURM_EXTERN_CONT) ||
	     (step_ptr->flags & SSF_EXT_LAUNCHER)) &&
	    job_ptr->tres_alloc_str) {
		/* get the tres from the whole job */
		step_ptr->tres_alloc_str =
			xstrdup(job_ptr->tres_alloc_str);
		if (make_formatted)
			step_ptr->tres_fmt_alloc_str =
				xstrdup(job_ptr->tres_fmt_alloc_str);
		return;
	}

	if (!assoc_mgr_locked)
		assoc_mgr_lock(&locks);

	if (((step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT) ||
	     (step_ptr->step_id.step_id == SLURM_INTERACTIVE_STEP)) &&
	    job_ptr->job_resrcs) {
		int batch_inx = 0;

		/*
		 * Figure out the index for the batch_host in relation to the
		 * job specific job_resrcs structure.
		 */
		if (job_ptr->batch_host) {
			batch_inx = job_get_node_inx(
				job_ptr->batch_host, job_ptr->node_bitmap);
			if (batch_inx == -1) {
				error("%s: Invalid batch host %s for %pJ; this should never happen",
				      __func__, job_ptr->batch_host, job_ptr);
				batch_inx = 0;
			}
		}

		/* get the cpus and memory on the first node */
		if (job_ptr->job_resrcs->cpus)
			cpu_count = job_ptr->job_resrcs->cpus[batch_inx];
		if (job_ptr->job_resrcs->memory_allocated)
			mem_count = job_ptr->job_resrcs->
				memory_allocated[batch_inx];

		tmp_tres_str = gres_stepmgr_gres_on_node_as_tres(
			job_ptr->gres_list_alloc, 0, true);
	} else {
		if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			cpu_count = (uint64_t)job_ptr->total_cpus;
		else
			cpu_count = (uint64_t)step_ptr->cpu_count;

		for (int i = 0; i < bit_set_count(step_ptr->step_node_bitmap);
		     i++)
			mem_count += step_ptr->memory_allocated[i];

		tmp_tres_str = gres_stepmgr_gres_2_tres_str(
			step_ptr->gres_list_alloc, true);
	}

	xstrfmtcat(step_ptr->tres_alloc_str,
		   "%s%u=%"PRIu64",%u=%"PRIu64",%u=%u",
		   step_ptr->tres_alloc_str ? "," : "",
		   TRES_CPU, cpu_count,
		   TRES_MEM, mem_count,
		   TRES_NODE, node_count);

	if (tmp_tres_str) {
		xstrfmtcat(step_ptr->tres_alloc_str, "%s%s",
			   step_ptr->tres_alloc_str ? "," : "",
			   tmp_tres_str);
		xfree(tmp_tres_str);
	}

	if (make_formatted)
		step_ptr->tres_fmt_alloc_str =
			slurmdb_make_tres_string_from_simple(
				step_ptr->tres_alloc_str, assoc_mgr_tres_list,
				NO_VAL, CONVERT_NUM_UNIT_EXACT, 0, NULL);

	if (!assoc_mgr_locked)
		assoc_mgr_unlock(&locks);
}

static int _suspend_job_step(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	job_record_t *job_ptr = step_ptr->job_ptr;
	time_t *now = (time_t *) arg;

	if (step_ptr->state != JOB_RUNNING)
		return 0;

	if ((job_ptr->suspend_time) &&
	    (job_ptr->suspend_time > step_ptr->start_time)) {
		step_ptr->pre_sus_time +=
			difftime(*now, job_ptr->suspend_time);
	} else {
		step_ptr->pre_sus_time +=
			difftime(*now, step_ptr->start_time);
	}

	return 0;
}

/* Update time stamps for job step suspend */
extern void suspend_job_step(job_record_t *job_ptr)
{
	time_t now = time(NULL);
	list_for_each(job_ptr->step_list, _suspend_job_step, &now);
}

static int _resume_job_step(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	job_record_t *job_ptr = (job_record_t *) step_ptr->job_ptr;
	time_t *now = (time_t *) arg;

	if (step_ptr->state != JOB_RUNNING)
		return 0;

	if ((job_ptr->suspend_time) &&
	    (job_ptr->suspend_time < step_ptr->start_time)) {
		step_ptr->tot_sus_time +=
			difftime(*now, step_ptr->start_time);
	} else {
		step_ptr->tot_sus_time +=
			difftime(*now, job_ptr->suspend_time);
	}

	return 0;
}

/* Update time stamps for job step resume */
extern void resume_job_step(job_record_t *job_ptr)
{
	time_t now = time(NULL);
	list_for_each(job_ptr->step_list, _resume_job_step, &now);
}

static void _signal_step_timelimit(step_record_t *step_ptr, time_t now)
{
#ifndef HAVE_FRONT_END
	node_record_t *node_ptr;
	static bool cloud_dns = false;
	static time_t last_update = 0;
#endif
	job_record_t *job_ptr = step_ptr->job_ptr;
	kill_job_msg_t *kill_step;
	agent_arg_t *agent_args = NULL;

	step_ptr->state = JOB_TIMEOUT;

	xassert(step_ptr);
	agent_args = xmalloc(sizeof(agent_arg_t));
	agent_args->msg_type = REQUEST_KILL_TIMELIMIT;
	agent_args->retry = 1;
	agent_args->hostlist = hostlist_create(NULL);
	kill_step = xmalloc(sizeof(kill_job_msg_t));
	memcpy(&kill_step->step_id, &step_ptr->step_id,
	       sizeof(kill_step->step_id));
	kill_step->het_job_id = job_ptr->het_job_id;
	kill_step->job_state = job_ptr->job_state;
	kill_step->job_uid   = job_ptr->user_id;
	kill_step->job_gid   = job_ptr->group_id;
	kill_step->nodes     = xstrdup(job_ptr->nodes);
	kill_step->time      = now;
	kill_step->start_time = job_ptr->start_time;
	kill_step->details = xstrdup(job_ptr->state_desc);

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	if (job_ptr->front_end_ptr)
		agent_args->protocol_version =
			job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, job_ptr->batch_host);
	agent_args->node_count++;
#else
        if (last_update != slurm_conf.last_update) {
                if (xstrcasestr(slurm_conf.slurmctld_params, "cloud_dns"))
                        cloud_dns = true;
                else
                        cloud_dns = false;
                last_update = slurm_conf.last_update;
        }

	if (step_ptr->step_node_bitmap) {
		agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
		for (int i = 0;
		     (node_ptr = next_node_bitmap(step_ptr->step_node_bitmap,
						  &i));
		     i++) {
			if (agent_args->protocol_version >
			    node_ptr->protocol_version) {
				agent_args->protocol_version =
					node_ptr->protocol_version;
			}
			hostlist_push_host(agent_args->hostlist,
					   node_ptr->name);
			agent_args->node_count++;
			if (PACK_FANOUT_ADDRS(node_ptr))
				agent_args->msg_flags |= SLURM_PACK_ADDRS;
		}
	} else {
		/* Could happen on node failure */
		info("%s: %pJ Step %u has NULL node_bitmap", __func__,
		     job_ptr, step_ptr->step_id.step_id);
	}
#endif

	if (agent_args->node_count == 0) {
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		slurm_free_kill_job_msg(kill_step);
		return;
	}

	agent_args->msg_args = kill_step;
	set_agent_arg_r_uid(agent_args, SLURM_AUTH_UID_ANY);
	stepmgr_ops->agent_queue_request(agent_args);
}

extern int check_job_step_time_limit(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	time_t *now = (time_t *) arg;
	uint32_t job_run_mins = 0;

	if (step_ptr->state != JOB_RUNNING)
		return 0;

	if (step_ptr->time_limit == INFINITE || step_ptr->time_limit == NO_VAL)
		return 0;

	job_run_mins = (uint32_t) (((*now - step_ptr->start_time) -
				    step_ptr->tot_sus_time) / 60);

	if (job_run_mins >= step_ptr->time_limit) {
		/* this step has timed out */
		info("%s: %pS has timed out (%u)",
		     __func__, step_ptr, step_ptr->time_limit);
		_signal_step_timelimit(step_ptr, *now);
	}

	return 0;
}

/* Return true if memory is a reserved resources, false otherwise */
static bool _is_mem_resv(void)
{
	static bool mem_resv_value  = false;
	static bool mem_resv_tested = false;

	if (!mem_resv_tested) {
		mem_resv_tested = true;
		if (slurm_conf.select_type_param & CR_MEMORY)
			mem_resv_value = true;
	}

	return mem_resv_value;
}

typedef struct {
	int mod_cnt;
	uint32_t time_limit;
} update_step_args_t;

static int _update_step(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	update_step_args_t *args = (update_step_args_t *) arg;

	if (step_ptr->state != JOB_RUNNING)
		return 0;

	step_ptr->time_limit = args->time_limit;
	args->mod_cnt++;

	info("Updating %pS time limit to %u", step_ptr, args->time_limit);

	return 0;
}

/*
 * Process job step update request from specified user,
 * RET - 0 or error code
 */
extern int update_step(step_update_request_msg_t *req, uid_t uid)
{
	job_record_t *job_ptr;
	step_record_t *step_ptr = NULL;
	update_step_args_t args = { .mod_cnt = 0 };
	slurm_step_id_t step_id = { 0 };

	job_ptr = stepmgr_ops->find_job_record(req->job_id);
	if (job_ptr == NULL) {
		error("%s: invalid JobId=%u", __func__, req->job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	step_id.job_id = job_ptr->job_id;
	step_id.step_id = req->step_id;
	step_id.step_het_comp = NO_VAL;

	/*
	 * No need to limit step time limit as job time limit will kill
	 * any steps with any time limit
	 */
	if (req->step_id == NO_VAL) {
		args.time_limit = req->time_limit;
		list_for_each(job_ptr->step_list, _update_step, &args);
	} else {
		step_ptr = find_step_record(job_ptr, &step_id);

		if (!step_ptr && (job_ptr->bit_flags & STEPMGR_ENABLED))
			goto stepmgr;
		if (!step_ptr)
			return ESLURM_INVALID_JOB_ID;
		if (req->time_limit) {
			step_ptr->time_limit = req->time_limit;
			args.mod_cnt++;
			info("Updating %pS time limit to %u",
			     step_ptr, req->time_limit);
		}
	}

stepmgr:
	if (running_in_slurmctld() && !step_ptr &&
	    (job_ptr->bit_flags & STEPMGR_ENABLED)) {
		agent_arg_t *agent_args = NULL;
		step_update_request_msg_t *agent_update_msg = NULL;

		agent_update_msg = xmalloc(sizeof(*agent_update_msg));
		agent_update_msg->job_id = req->job_id;
		agent_update_msg->step_id = req->step_id;
		agent_update_msg->time_limit = req->time_limit;

		agent_args = xmalloc(sizeof(*agent_args));
		agent_args->msg_type = REQUEST_UPDATE_JOB_STEP;
		agent_args->retry = 1;
		agent_args->hostlist = hostlist_create(job_ptr->batch_host);
		agent_args->node_count = 1;
		agent_args->protocol_version = SLURM_PROTOCOL_VERSION;

		agent_args->msg_args = agent_update_msg;
		set_agent_arg_r_uid(agent_args, slurm_conf.slurmd_user_id);
		stepmgr_ops->agent_queue_request(agent_args);
		args.mod_cnt++;
	}

	if (args.mod_cnt)
		*stepmgr_ops->last_job_update = time(NULL);

	return SLURM_SUCCESS;
}

static int _rebuild_bitmaps(void *x, void *arg)
{
	int i_first, i_last, i_size;
	int old_core_offset = 0, new_core_offset = 0;
	bool old_node_set, new_node_set;
	uint32_t step_id;
	bitstr_t *orig_step_core_bitmap;
	step_record_t *step_ptr = (step_record_t *) x;
	bitstr_t *orig_job_node_bitmap = (bitstr_t *) arg;
	job_record_t *job_ptr = step_ptr->job_ptr;

	if (step_ptr->state < JOB_RUNNING)
		return 0;

	gres_stepmgr_step_state_rebase(step_ptr->gres_list_alloc,
				    orig_job_node_bitmap,
				    job_ptr->job_resrcs->node_bitmap);
	if (!step_ptr->core_bitmap_job)
		return 0;

	step_id = step_ptr->step_id.step_id;

	orig_step_core_bitmap = step_ptr->core_bitmap_job;
	i_size = bit_size(job_ptr->job_resrcs->core_bitmap);
	step_ptr->core_bitmap_job = bit_alloc(i_size);
	i_first = MIN(bit_ffs(orig_job_node_bitmap),
		      bit_ffs(job_ptr->job_resrcs->node_bitmap));
	i_last  = MAX(bit_fls(orig_job_node_bitmap),
		      bit_fls(job_ptr->job_resrcs->node_bitmap));
	for (int i = i_first; i <= i_last; i++) {
		old_node_set = bit_test(orig_job_node_bitmap, i);
		new_node_set = bit_test(job_ptr->job_resrcs->node_bitmap, i);
		if (!old_node_set && !new_node_set)
			continue;
		if (old_node_set && new_node_set) {
			for (int j = 0; j < node_record_table_ptr[i]->tot_cores;
			     j++) {
				if (!bit_test(orig_step_core_bitmap,
					      old_core_offset + j))
					continue;
				bit_set(step_ptr->core_bitmap_job,
					new_core_offset + j);
				/*
				 * Only regular, non-overlapping steps should
				 * set bits in core_bitmap_used
				 */
				if ((step_id != SLURM_INTERACTIVE_STEP) &&
				    (step_id != SLURM_EXTERN_CONT) &&
				    (step_id != SLURM_BATCH_SCRIPT) &&
				    !(step_ptr->flags & SSF_OVERLAP_FORCE) &&
				    !(step_ptr->flags & SSF_EXT_LAUNCHER))
					bit_set(job_ptr->job_resrcs->
						core_bitmap_used,
						new_core_offset + j);
			}
		}
		if (old_node_set)
			old_core_offset += node_record_table_ptr[i]->tot_cores;
		if (new_node_set)
			new_core_offset += node_record_table_ptr[i]->tot_cores;
	}
	FREE_NULL_BITMAP(orig_step_core_bitmap);

	return 0;
}

/*
 * Rebuild a job step's core_bitmap_job after a job has just changed size
 * job_ptr IN - job that was just re-sized
 * orig_job_node_bitmap IN - The job's original node bitmap
 */
extern void rebuild_step_bitmaps(job_record_t *job_ptr,
				 bitstr_t *orig_job_node_bitmap)
{
	if (job_ptr->step_list == NULL)
		return;

	log_flag(STEPS, "Resizing steps of %pJ", job_ptr);
	list_for_each(job_ptr->step_list, _rebuild_bitmaps,
		      orig_job_node_bitmap);

}

extern step_record_t *build_extern_step(job_record_t *job_ptr)
{
	step_record_t *step_ptr = create_step_record(job_ptr, 0);
	char *node_list;
	uint32_t node_cnt;

#ifdef HAVE_FRONT_END
	node_list = job_ptr->front_end_ptr->name;
	node_cnt = 1;
#else
	node_list = job_ptr->nodes;
	node_cnt = job_ptr->node_cnt;
#endif
	if (!step_ptr) {
		error("%s: Can't create step_record! This should never happen",
		      __func__);
		return NULL;
	}

	*stepmgr_ops->last_job_update = time(NULL);

	step_ptr->step_layout = fake_slurm_step_layout_create(
		node_list, NULL, NULL, node_cnt, node_cnt,
		SLURM_PROTOCOL_VERSION);

	step_ptr->name = xstrdup("extern");
	step_ptr->select_jobinfo = select_g_select_jobinfo_alloc();
	step_ptr->state = JOB_RUNNING;
	step_ptr->start_time = job_ptr->start_time;
	step_ptr->step_id.job_id = job_ptr->job_id;
	step_ptr->step_id.step_id = SLURM_EXTERN_CONT;
	step_ptr->step_id.step_het_comp = NO_VAL;
	if (job_ptr->node_bitmap)
		step_ptr->step_node_bitmap =
			bit_copy(job_ptr->node_bitmap);
	step_ptr->time_last_active = time(NULL);
	step_set_alloc_tres(step_ptr, 1, false, false);

	jobacct_storage_g_step_start(stepmgr_ops->acct_db_conn, step_ptr);

	return step_ptr;
}

extern step_record_t *build_batch_step(job_record_t *job_ptr_in)
{
	job_record_t *job_ptr;
	step_record_t *step_ptr;
	char *host = NULL;

	if (job_ptr_in->het_job_id) {
		job_ptr = stepmgr_ops->find_job_record(job_ptr_in->het_job_id);
		if (!job_ptr) {
			error("%s: hetjob leader is corrupt! This should never happen",
			      __func__);
			job_ptr = job_ptr_in;
		}
	} else
		job_ptr = job_ptr_in;

	step_ptr = create_step_record(job_ptr, 0);

	if (!step_ptr) {
		error("%s: Can't create step_record! This should never happen",
		      __func__);
		return NULL;
	}

	*stepmgr_ops->last_job_update = time(NULL);

#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr =
		stepmgr_ops->find_front_end_record(job_ptr->batch_host);
	if (front_end_ptr && front_end_ptr->name)
		host = front_end_ptr->name;
	else {
		error("%s: could not find front-end node for %pJ",__func__,
		      job_ptr);
		host = job_ptr->batch_host;
	}
#else
	host = job_ptr->batch_host;
#endif
	step_ptr->step_layout = fake_slurm_step_layout_create(
		host, NULL, NULL, 1, 1, SLURM_PROTOCOL_VERSION);
	step_ptr->name = xstrdup("batch");
	step_ptr->select_jobinfo = select_g_select_jobinfo_alloc();
	step_ptr->state = JOB_RUNNING;
	step_ptr->start_time = job_ptr->start_time;
	step_ptr->step_id.job_id = job_ptr->job_id;
	step_ptr->step_id.step_id = SLURM_BATCH_SCRIPT;
	step_ptr->step_id.step_het_comp = NO_VAL;
	step_ptr->container = xstrdup(job_ptr->container);
	step_ptr->container_id = xstrdup(job_ptr->container_id);

#ifndef HAVE_FRONT_END
	if (node_name2bitmap(job_ptr->batch_host, false,
			     &step_ptr->step_node_bitmap, NULL)) {
		error("%s: %pJ has invalid node list (%s)",
		      __func__, job_ptr, job_ptr->batch_host);
	}
#endif

	step_ptr->time_last_active = time(NULL);
	step_set_alloc_tres(step_ptr, 1, false, false);

	jobacct_storage_g_step_start(stepmgr_ops->acct_db_conn, step_ptr);

	return step_ptr;
}

static step_record_t *_build_interactive_step(
	job_record_t *job_ptr_in,
	job_step_create_request_msg_t *step_specs,
	uint16_t protocol_version)
{
	job_record_t *job_ptr;
	step_record_t *step_ptr;
	char *host = NULL;
	slurm_step_id_t step_id = {0};

	if (job_ptr_in->het_job_id) {
		job_ptr = stepmgr_ops->find_job_record(job_ptr_in->het_job_id);
		if (!job_ptr) {
			error("%s: hetjob leader is corrupt! This should never happen",
			      __func__);
			job_ptr = job_ptr_in;
		}
	} else
		job_ptr = job_ptr_in;

	step_id.job_id = job_ptr->job_id;
	step_id.step_id = SLURM_INTERACTIVE_STEP;
	step_id.step_het_comp = NO_VAL;
	step_ptr = find_step_record(job_ptr, &step_id);
	if (step_ptr) {
		debug("%s: interactive step for %pJ already exists",
		      __func__, job_ptr);
		return NULL;
	}

#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr =
		stepmgr_ops->find_front_end_record(job_ptr->batch_host);
	if (front_end_ptr && front_end_ptr->name)
		host = front_end_ptr->name;
	else {
		error("%s: could not find front-end node for %pJ",__func__,
		      job_ptr);
		host = job_ptr->batch_host;
	}
#else
	host = job_ptr->batch_host;
#endif
	if (!host) {
		error("%s: %pJ batch_host is NULL! This should never happen",
		      __func__, job_ptr);
		return NULL;
	}

	step_ptr = create_step_record(job_ptr, protocol_version);

	if (!step_ptr) {
		error("%s: Can't create step_record! This should never happen",
		      __func__);
		return NULL;
	}
	*stepmgr_ops->last_job_update = time(NULL);

	step_ptr->step_layout = fake_slurm_step_layout_create(
		host, NULL, NULL, 1, 1, protocol_version);
	step_ptr->name = xstrdup("interactive");
	step_ptr->select_jobinfo = select_g_select_jobinfo_alloc();
	step_ptr->state = JOB_RUNNING;
	step_ptr->start_time = job_ptr->start_time;
	step_ptr->step_id.job_id = job_ptr->job_id;
	step_ptr->step_id.step_id = SLURM_INTERACTIVE_STEP;
	step_ptr->step_id.step_het_comp = NO_VAL;
	step_ptr->container = xstrdup(job_ptr->container);
	step_ptr->container_id = xstrdup(job_ptr->container_id);

	step_ptr->port = step_specs->port;
	step_ptr->srun_pid = step_specs->srun_pid;
	step_ptr->host = xstrdup(step_specs->host);
	step_ptr->submit_line = xstrdup(step_specs->submit_line);

	step_ptr->core_bitmap_job = bit_copy(job_ptr->job_resrcs->core_bitmap);

#ifndef HAVE_FRONT_END
	if (node_name2bitmap(job_ptr->batch_host, false,
			     &step_ptr->step_node_bitmap, NULL)) {
		error("%s: %pJ has invalid node list (%s)",
		      __func__, job_ptr, job_ptr->batch_host);
		delete_step_record(job_ptr, step_ptr);
		return NULL;
	}
#endif

	step_ptr->time_last_active = time(NULL);
	step_set_alloc_tres(step_ptr, 1, false, false);

	jobacct_storage_g_step_start(stepmgr_ops->acct_db_conn, step_ptr);

	return step_ptr;
}

/*
 * Build a special step for mpi launchers.
 */
static int _build_ext_launcher_step(step_record_t **step_rec,
				    job_record_t *job_ptr,
				    job_step_create_request_msg_t *step_specs,
				    uint16_t protocol_version)
{
	bitstr_t *nodeset;
	uint32_t node_count;
	int rc;
	dynamic_plugin_data_t *select_jobinfo = NULL;
	char *step_node_list;
	step_record_t *step_ptr;

	if (!step_rec)
		return SLURM_ERROR;

	if (job_ptr->next_step_id >= slurm_conf.max_step_cnt) {
		error("%s: %pJ MaxStepCount limit reached", __func__, job_ptr);
		return ESLURM_STEP_LIMIT;
	}

	/* Reset some fields we're going to ignore in _pick_step_nodes. */
	step_specs->flags = SSF_EXT_LAUNCHER;
	step_specs->cpu_count = 0;
	xfree(step_specs->cpus_per_tres);
	step_specs->ntasks_per_core = NO_VAL16;
	step_specs->ntasks_per_tres = NO_VAL16;
	step_specs->pn_min_memory = 0;
	xfree(step_specs->mem_per_tres);
	step_specs->threads_per_core = NO_VAL16;
	xfree(step_specs->tres_bind);
	xfree(step_specs->tres_per_step);
	xfree(step_specs->tres_per_node);
	xfree(step_specs->tres_per_socket);
	xfree(step_specs->tres_per_task);

	/* Select the nodes for this job */
	select_jobinfo = select_g_select_jobinfo_alloc();
	nodeset = _pick_step_nodes(job_ptr, step_specs, NULL, 0, 0,
				   select_jobinfo, &rc);
	if (nodeset == NULL) {
		select_g_select_jobinfo_free(select_jobinfo);
		return rc;
	}

	/* Here is where the node list is set for the step */
	if (step_specs->node_list &&
	    ((step_specs->task_dist & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY)) {
		step_node_list = xstrdup(step_specs->node_list);
		xfree(step_specs->node_list);
		step_specs->node_list = bitmap2node_name(nodeset);
	} else {
		step_node_list = bitmap2node_name_sortable(nodeset, false);
		xfree(step_specs->node_list);
		step_specs->node_list = xstrdup(step_node_list);
	}
	log_flag(STEPS, "Picked nodes %s when accumulating from %s",
		 step_node_list, step_specs->node_list);

	step_ptr = *step_rec = create_step_record(job_ptr, protocol_version);

	if (!step_ptr) {
		error("%s: Can't create step_record! This should never happen",
		      __func__);
		select_g_select_jobinfo_free(select_jobinfo);
		return SLURM_ERROR;
	}
	*stepmgr_ops->last_job_update = time(NULL);

	/* We want 1 task per node. */
	step_ptr->step_node_bitmap = nodeset;
	node_count = bit_set_count(nodeset);
	step_specs->num_tasks = node_count;

	/* Create the fake step layout with 1 task per node */
	step_ptr->step_layout = fake_slurm_step_layout_create(
		step_node_list, NULL, NULL, node_count, node_count,
		SLURM_PROTOCOL_VERSION);
	xfree(step_node_list);

	if (!step_ptr->step_layout) {
		select_g_select_jobinfo_free(select_jobinfo);
		delete_step_record(job_ptr, step_ptr);
		return SLURM_ERROR;
	}

	/* Needed for not considering it in _mark_busy_nodes */
	step_ptr->flags |= SSF_EXT_LAUNCHER;

	/* Set the step id */
	memcpy(&step_ptr->step_id, &step_specs->step_id,
	       sizeof(step_ptr->step_id));

	if (step_specs->array_task_id != NO_VAL)
		step_ptr->step_id.job_id = job_ptr->job_id;

	if (step_specs->step_id.step_id != NO_VAL) {
		if (step_specs->step_id.step_het_comp == NO_VAL) {
			job_ptr->next_step_id =
				MAX(job_ptr->next_step_id,
				    step_specs->step_id.step_id);
			job_ptr->next_step_id++;
		}
	} else if (job_ptr->het_job_id &&
		   (job_ptr->het_job_id != job_ptr->job_id)) {
		job_record_t *het_job;
		het_job = stepmgr_ops->find_job_record(job_ptr->het_job_id);
		if (het_job)
			step_ptr->step_id.step_id = het_job->next_step_id++;
		else
			step_ptr->step_id.step_id = job_ptr->next_step_id++;
		job_ptr->next_step_id = MAX(job_ptr->next_step_id,
					    step_ptr->step_id.step_id);
	} else {
		step_ptr->step_id.step_id = job_ptr->next_step_id++;
	}

	/* The step needs to run on all the cores. */
	step_ptr->core_bitmap_job = bit_copy(job_ptr->job_resrcs->core_bitmap);
	step_ptr->name = xstrdup(step_specs->name);
	step_ptr->select_jobinfo = select_jobinfo;
	step_ptr->state = JOB_RUNNING;
	step_ptr->start_time = job_ptr->start_time;
	step_ptr->time_last_active = time(NULL);

	step_set_alloc_tres(step_ptr, 1, false, false);
	jobacct_storage_g_step_start(stepmgr_ops->acct_db_conn, step_ptr);

	if ((rc = _switch_setup(step_ptr))) {
		delete_step_record(job_ptr, step_ptr);
		return rc;
	}

	return SLURM_SUCCESS;
}

extern slurm_node_alias_addrs_t *build_alias_addrs(job_record_t *job_ptr)
{
	slurm_node_alias_addrs_t *alias_addrs;

	if (!job_ptr || !job_ptr->node_addrs)
		return NULL;

	alias_addrs = xmalloc(sizeof(slurm_node_alias_addrs_t));
	alias_addrs->node_cnt = job_ptr->node_cnt;
	alias_addrs->node_addrs = xcalloc(job_ptr->node_cnt,
					  sizeof(slurm_addr_t));
	memcpy(alias_addrs->node_addrs, job_ptr->node_addrs,
	       (sizeof(slurm_addr_t) * job_ptr->node_cnt));
	alias_addrs->node_list = xstrdup(job_ptr->nodes);

	return alias_addrs;
}

extern int job_get_node_inx(char *node_name, bitstr_t *node_bitmap)
{
	int node_inx = -1;

	if (!node_name)
		return -1;

	xassert(node_bitmap);

	node_inx = node_name_get_inx(node_name);
	if (node_inx == -1)
		return -1;

	if (!bit_test(node_bitmap, node_inx))
		return -1;

	return bit_set_count_range(node_bitmap, 0, node_inx);
}

static void _kill_step_on_msg_fail(step_complete_msg_t *req, slurm_msg_t *msg,
				   void (*lock_func)(bool lock))
{
	int rc, rem;
	uint32_t step_rc;
	DEF_TIMERS;
	/* init */
	START_TIMER;
	error("Step creation timed out: Deallocating %ps nodes %u-%u",
	      &req->step_id, req->range_first, req->range_last);

	if (lock_func)
		lock_func(true);

	rc = step_partial_comp(req, msg->auth_uid, true, &rem, &step_rc);

	if (lock_func)
		lock_func(false);

	END_TIMER2(__func__);
	log_flag(STEPS, "%s: %ps rc:%s %s",
		 __func__, &req->step_id, slurm_strerror(rc), TIME_STR);
}

/* create a credential for a given job step, return error code */
static int _make_step_cred(step_record_t *step_ptr, slurm_cred_t **slurm_cred,
			   uint16_t protocol_version)
{
	slurm_cred_arg_t cred_arg;
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;

	xassert(job_resrcs_ptr && job_resrcs_ptr->cpus);

	setup_cred_arg(&cred_arg, job_ptr);

	memcpy(&cred_arg.step_id, &step_ptr->step_id, sizeof(cred_arg.step_id));
	if (job_resrcs_ptr->memory_allocated) {
		slurm_array64_to_value_reps(job_resrcs_ptr->memory_allocated,
					    job_resrcs_ptr->nhosts,
					    &cred_arg.job_mem_alloc,
					    &cred_arg.job_mem_alloc_rep_count,
					    &cred_arg.job_mem_alloc_size);
	}

	cred_arg.step_gres_list  = step_ptr->gres_list_alloc;

	cred_arg.step_core_bitmap = step_ptr->core_bitmap_job;
#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	cred_arg.step_hostlist   = job_ptr->batch_host;
#else
	cred_arg.step_hostlist   = step_ptr->step_layout->node_list;
#endif
	if (step_ptr->memory_allocated) {
		slurm_array64_to_value_reps(step_ptr->memory_allocated,
					    step_ptr->step_layout->node_cnt,
					    &cred_arg.step_mem_alloc,
					    &cred_arg.step_mem_alloc_rep_count,
					    &cred_arg.step_mem_alloc_size);
	}

	cred_arg.switch_step = step_ptr->switch_step;

	*slurm_cred = slurm_cred_create(&cred_arg, true, protocol_version);

	xfree(cred_arg.job_mem_alloc);
	xfree(cred_arg.job_mem_alloc_rep_count);
	xfree(cred_arg.step_mem_alloc);
	xfree(cred_arg.step_mem_alloc_rep_count);
	if (*slurm_cred == NULL) {
		error("slurm_cred_create error");
		return ESLURM_INVALID_JOB_CREDENTIAL;
	}

	return SLURM_SUCCESS;
}

extern int step_create_from_msg(slurm_msg_t *msg,
				void (*lock_func)(bool lock),
				void (*fail_lock_func)(bool lock))
{
	char *err_msg = NULL;
	int error_code = SLURM_SUCCESS;
	DEF_TIMERS;
	step_record_t *step_rec;
	job_step_create_response_msg_t job_step_resp;
	job_step_create_request_msg_t *req_step_msg = msg->data;
	slurm_cred_t *slurm_cred = NULL;
	job_record_t *job_ptr = NULL;

	START_TIMER;

	xassert(msg->auth_ids_set);

	if (req_step_msg->user_id == SLURM_AUTH_NOBODY) {
		req_step_msg->user_id = msg->auth_uid;

		if (get_log_level() >= LOG_LEVEL_DEBUG3) {
			char *host = auth_g_get_host(msg);
			debug3("%s: [%s] set RPC user_id to %d",
			       __func__, host, msg->auth_uid);
			xfree(host);
		}
	} else if (msg->auth_uid != req_step_msg->user_id) {
		error("Security violation, JOB_STEP_CREATE RPC from uid=%u to run as uid %u",
		      msg->auth_uid, req_step_msg->user_id);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return ESLURM_USER_ID_MISSING;
	}

#if defined HAVE_FRONT_END
	/* Limited job step support */
	/* Non-super users not permitted to run job steps on front-end.
	 * A single slurmd can not handle a heavy load. */
	if (!validate_slurm_user(msg->auth_uid)) {
		info("Attempt to execute job step by uid=%u", msg->auth_uid);
		slurm_send_rc_msg(msg, ESLURM_NO_STEPS);
		return ESLURM_USER_ID_MISSING;
	}
#endif

	dump_step_desc(req_step_msg);

	if (lock_func) {
		lock_func(true);
	}

	if (req_step_msg->array_task_id != NO_VAL)
		job_ptr = stepmgr_ops->find_job_array_rec(
			req_step_msg->step_id.job_id,
			req_step_msg->array_task_id);
	else
		job_ptr = stepmgr_ops->find_job_record(
			req_step_msg->step_id.job_id);

	if (job_ptr == NULL) {
		error_code = ESLURM_INVALID_JOB_ID ;
		goto end_it;
	}

	if (running_in_slurmctld() &&
	    (job_ptr->bit_flags & STEPMGR_ENABLED)) {
		if (msg->protocol_version < SLURM_24_05_PROTOCOL_VERSION) {
			error("rpc %s from non-supported client version %d for stepmgr job",
			      rpc_num2string(msg->msg_type),
			      msg->protocol_version);
			slurm_send_rc_msg(msg, ESLURM_NOT_SUPPORTED);
		} else {
			slurm_send_reroute_msg(msg, NULL, job_ptr->batch_host);
		}
		if (lock_func)
			lock_func(false);
		return SLURM_SUCCESS;
	}

	error_code = step_create(job_ptr, req_step_msg, &step_rec,
				 msg->protocol_version, &err_msg);

	if (error_code == SLURM_SUCCESS) {
		error_code = _make_step_cred(step_rec, &slurm_cred,
					     step_rec->start_protocol_ver);
	}
	END_TIMER2(__func__);

end_it:
	/* return result */
	if (error_code) {
		if (lock_func)
			lock_func(false);

		if (error_code == ESLURM_PROLOG_RUNNING)
			log_flag(STEPS, "%s for configuring %ps: %s",
				 __func__, &req_step_msg->step_id,
				 slurm_strerror(error_code));
		else if (error_code == ESLURM_DISABLED)
			log_flag(STEPS, "%s for suspended %ps: %s",
				 __func__, &req_step_msg->step_id,
				 slurm_strerror(error_code));
		else
			log_flag(STEPS, "%s for %ps: %s",
				 __func__, &req_step_msg->step_id,
				 slurm_strerror(error_code));
		if (err_msg)
			slurm_send_rc_err_msg(msg, error_code, err_msg);
		else
			slurm_send_rc_msg(msg, error_code);
	} else {
		slurm_step_layout_t *step_layout = NULL;
		dynamic_plugin_data_t *switch_step = NULL;

		log_flag(STEPS, "%s: %pS %s %s",
			 __func__, step_rec, req_step_msg->node_list, TIME_STR);

		memset(&job_step_resp, 0, sizeof(job_step_resp));
		job_step_resp.job_id = step_rec->step_id.job_id;
		job_step_resp.job_step_id = step_rec->step_id.step_id;
		job_step_resp.resv_ports  = step_rec->resv_ports;

		step_layout = slurm_step_layout_copy(step_rec->step_layout);
		job_step_resp.step_layout = step_layout;

#ifdef HAVE_FRONT_END
		if (step_rec->job_ptr->batch_host) {
			job_step_resp.step_layout->front_end =
				xstrdup(step_rec->job_ptr->batch_host);
		}
#endif
		if (step_rec->job_ptr && step_rec->job_ptr->details &&
		    (step_rec->job_ptr->details->cpu_bind_type != NO_VAL16)) {
			job_step_resp.def_cpu_bind_type =
				step_rec->job_ptr->details->cpu_bind_type;
		}
		job_step_resp.cred = slurm_cred;
		job_step_resp.use_protocol_ver = step_rec->start_protocol_ver;

		if (step_rec->switch_step)
			switch_g_duplicate_stepinfo(step_rec->switch_step,
						    &switch_step);
		job_step_resp.switch_step = switch_step;

		if (job_ptr->bit_flags & STEPMGR_ENABLED)
			job_step_resp.stepmgr = job_ptr->batch_host;

		if (lock_func)
			lock_func(false);

		if (msg->protocol_version != step_rec->start_protocol_ver) {
			log_flag(NET, "%s: responding with non-matching msg 0x%x to step 0x%x protocol version",
				 __func__, msg->protocol_version,
				 step_rec->start_protocol_ver);
			msg->protocol_version = step_rec->start_protocol_ver;
		}

		if (send_msg_response(msg, RESPONSE_JOB_STEP_CREATE,
				      &job_step_resp)) {
			step_complete_msg_t req;

			memset(&req, 0, sizeof(req));
			req.step_id = step_rec->step_id;
			req.jobacct = step_rec->jobacct;
			req.step_rc = SIGKILL;
			req.range_first = 0;
			req.range_last = step_layout->node_cnt - 1;
			_kill_step_on_msg_fail(&req, msg, fail_lock_func);
		}

		slurm_cred_destroy(slurm_cred);
		slurm_step_layout_destroy(step_layout);
		switch_g_free_stepinfo(switch_step);
	}

	xfree(err_msg);

	return error_code;
}

/*
 * pack_job_step_info_response_msg - packs job step info
 * IN step_id - specific id or NO_VAL/NO_VAL for all
 * IN uid - user issuing request
 * IN show_flags - job step filtering options
 * OUT buffer - location to store data, pointers automatically advanced
 * RET - 0 or error code
 * NOTE: MUST free_buf buffer
 */
extern int pack_job_step_info_response_msg(pack_step_args_t *args)
{
	int error_code = 0;
	uint32_t tmp_offset;
	time_t now = time(NULL);

	if (args->proto_version >= SLURM_24_05_PROTOCOL_VERSION) {
		/* steps_packed placeholder */
		pack32(args->steps_packed, args->buffer);
		pack_time(now, args->buffer);

		list_for_each_ro(args->job_step_list,
				 args->pack_job_step_list_func, args);

		if (list_count(job_list) && !args->valid_job &&
		    !args->steps_packed)
			error_code = ESLURM_INVALID_JOB_ID;

		slurm_pack_list(args->stepmgr_jobs,
				slurm_pack_stepmgr_job_info, args->buffer,
				args->proto_version);

		/* put the real record count in the message body header */
		tmp_offset = get_buf_offset(args->buffer);
		set_buf_offset(args->buffer, 0);
		pack32(args->steps_packed, args->buffer);

		set_buf_offset(args->buffer, tmp_offset);
	} else if (args->proto_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* steps_packed placeholder */
		pack32(args->steps_packed, args->buffer);
		pack_time(now, args->buffer);

		list_for_each_ro(args->job_step_list,
				 args->pack_job_step_list_func, args);

		if (list_count(job_list) && !args->valid_job &&
		    !args->steps_packed)
			error_code = ESLURM_INVALID_JOB_ID;

		/* put the real record count in the message body header */
		tmp_offset = get_buf_offset(args->buffer);
		set_buf_offset(args->buffer, 0);
		pack32(args->steps_packed, args->buffer);

		set_buf_offset(args->buffer, tmp_offset);
	}

	xfree(args->visible_parts);

	return error_code;
}

extern int stepmgr_get_step_layouts(job_record_t *job_ptr,
				    slurm_step_id_t *step_id,
				    slurm_step_layout_t **out_step_layout)
{
	list_itr_t *itr;
	step_record_t *step_ptr = NULL;
	slurm_step_layout_t *step_layout = NULL;

	/* We can't call find_step_record here since we may need more than 1 */
	itr = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(itr))) {
		if (!verify_step_id(&step_ptr->step_id, step_id))
			continue;
		/*
		 * Rebuild alias_addrs if need after restart of slurmctld
		 */
		 if (job_ptr->node_addrs &&
		     !step_ptr->step_layout->alias_addrs) {
			step_ptr->step_layout->alias_addrs =
				build_alias_addrs(job_ptr);
		}

		if (step_layout)
			slurm_step_layout_merge(step_layout,
						step_ptr->step_layout);
		else
			step_layout = slurm_step_layout_copy(
				step_ptr->step_layout);

		/* break if don't need to look for further het_steps */
		if (step_ptr->step_id.step_het_comp == NO_VAL)
			break;
		/*
		 * If we are looking for a specific het step we can break here
		 * as well.
		 */
		if (step_id->step_het_comp != NO_VAL)
			break;
	}
	list_iterator_destroy(itr);

	if (!step_layout) {
		log_flag(STEPS, "%s: %pJ StepId=%u Not Found",
			 __func__, job_ptr, step_id->step_id);
		return ESLURM_INVALID_JOB_ID;
	}

	/*
	 * The cpt_compact* fields don't go to the client because they are not
	 * handled in slurm_step_layout_merge(). Free them so the client does
	 * not get bad data.
	 */
	xfree(step_layout->cpt_compact_array);
	xfree(step_layout->cpt_compact_reps);
	step_layout->cpt_compact_cnt = 0;

#ifdef HAVE_FRONT_END
	if (job_ptr->batch_host)
		step_layout->front_end = xstrdup(job_ptr->batch_host);
#endif

	*out_step_layout = step_layout;

	return SLURM_SUCCESS;
}

extern int stepmgr_get_job_sbcast_cred_msg(job_record_t *job_ptr,
					   slurm_step_id_t *step_id,
					   char *hetjob_nodelist,
					   uint16_t protocol_version,
					   job_sbcast_cred_msg_t **out_sbcast_cred_msg)
{
	sbcast_cred_t *sbcast_cred;
	sbcast_cred_arg_t sbcast_arg;
	step_record_t *step_ptr = NULL;
	char *node_list = NULL;
	job_sbcast_cred_msg_t *job_info_resp_msg;

	xassert(job_ptr);

	node_list = hetjob_nodelist;

	if (step_id->step_id != NO_VAL) {
		step_ptr = find_step_record(job_ptr, step_id);
		if (!step_ptr) {
			return ESLURM_INVALID_JOB_ID;
		} else if (step_ptr->step_layout &&
			   (step_ptr->step_layout->node_cnt !=
			    job_ptr->node_cnt)) {
			node_list = step_ptr->step_layout->node_list;
		}
	}

	if (!node_list)
		node_list = job_ptr->nodes;

	/*
	 * Note - using pointers to other xmalloc'd elements owned by other
	 * structures to avoid copy overhead. Do not free them!
	 */
	memset(&sbcast_arg, 0, sizeof(sbcast_arg));
	sbcast_arg.job_id = job_ptr->job_id;
	sbcast_arg.het_job_id = job_ptr->het_job_id;
	if (step_ptr)
		sbcast_arg.step_id = step_ptr->step_id.step_id;
	else
		sbcast_arg.step_id = job_ptr->next_step_id;
	sbcast_arg.nodes = node_list; /* avoid extra copy */
	sbcast_arg.expiration = job_ptr->end_time;

	if (!(sbcast_cred = create_sbcast_cred(&sbcast_arg, job_ptr->user_id,
					       job_ptr->group_id,
					       protocol_version))) {
		error("%s %pJ cred create error", __func__, job_ptr);
		return SLURM_ERROR;
	}

	job_info_resp_msg = xmalloc(sizeof(*job_info_resp_msg));
	job_info_resp_msg->job_id = job_ptr->job_id;
	job_info_resp_msg->node_list = xstrdup(node_list);
	job_info_resp_msg->sbcast_cred = sbcast_cred;

	*out_sbcast_cred_msg = job_info_resp_msg;

	return SLURM_SUCCESS;
}

/* Build structure with job allocation details */
extern resource_allocation_response_msg_t *build_job_info_resp(
	job_record_t *job_ptr)
{
	resource_allocation_response_msg_t *job_info_resp_msg;
	int i, j;

	job_info_resp_msg = xmalloc(sizeof(resource_allocation_response_msg_t));


	if (!job_ptr->job_resrcs) {
		;
	} else if (bit_equal(job_ptr->node_bitmap,
			     job_ptr->job_resrcs->node_bitmap)) {
		job_info_resp_msg->num_cpu_groups =
			job_ptr->job_resrcs->cpu_array_cnt;
		job_info_resp_msg->cpu_count_reps =
			xcalloc(job_ptr->job_resrcs->cpu_array_cnt,
				sizeof(uint32_t));
		memcpy(job_info_resp_msg->cpu_count_reps,
		       job_ptr->job_resrcs->cpu_array_reps,
		       (sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt));
		job_info_resp_msg->cpus_per_node  =
			xcalloc(job_ptr->job_resrcs->cpu_array_cnt,
				sizeof(uint16_t));
		memcpy(job_info_resp_msg->cpus_per_node,
		       job_ptr->job_resrcs->cpu_array_value,
		       (sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt));
	} else {
		/* Job has changed size, rebuild CPU count info */
		job_info_resp_msg->num_cpu_groups = job_ptr->node_cnt;
		job_info_resp_msg->cpu_count_reps = xcalloc(job_ptr->node_cnt,
							    sizeof(uint32_t));
		job_info_resp_msg->cpus_per_node = xcalloc(job_ptr->node_cnt,
							   sizeof(uint32_t));
		for (i = 0, j = -1; i < job_ptr->job_resrcs->nhosts; i++) {
			if (job_ptr->job_resrcs->cpus[i] == 0)
				continue;
			if ((j == -1) ||
			    (job_info_resp_msg->cpus_per_node[j] !=
			     job_ptr->job_resrcs->cpus[i])) {
				j++;
				job_info_resp_msg->cpus_per_node[j] =
					job_ptr->job_resrcs->cpus[i];
				job_info_resp_msg->cpu_count_reps[j] = 1;
			} else {
				job_info_resp_msg->cpu_count_reps[j]++;
			}
		}
		job_info_resp_msg->num_cpu_groups = j + 1;
	}
	job_info_resp_msg->account        = xstrdup(job_ptr->account);
	job_info_resp_msg->alias_list     = xstrdup(job_ptr->alias_list);
	job_info_resp_msg->batch_host = xstrdup(job_ptr->batch_host);
	job_info_resp_msg->job_id         = job_ptr->job_id;
	job_info_resp_msg->node_cnt       = job_ptr->node_cnt;
	job_info_resp_msg->node_list      = xstrdup(job_ptr->nodes);
	if (job_ptr->part_ptr)
		job_info_resp_msg->partition = xstrdup(job_ptr->part_ptr->name);
	else
		job_info_resp_msg->partition = xstrdup(job_ptr->partition);
	if (job_ptr->qos_ptr) {
		slurmdb_qos_rec_t *qos;
		qos = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		job_info_resp_msg->qos = xstrdup(qos->name);
	}
	job_info_resp_msg->resv_name      = xstrdup(job_ptr->resv_name);
	if (job_ptr->details) {
		if (job_ptr->bit_flags & JOB_MEM_SET) {
			job_info_resp_msg->pn_min_memory =
				job_ptr->details->pn_min_memory;
		}
		if (job_ptr->details->mc_ptr) {
			job_info_resp_msg->ntasks_per_board =
				job_ptr->details->mc_ptr->ntasks_per_board;
			job_info_resp_msg->ntasks_per_core =
				job_ptr->details->mc_ptr->ntasks_per_core;
			job_info_resp_msg->ntasks_per_socket =
				job_ptr->details->mc_ptr->ntasks_per_socket;
		}
	} else {
		/* job_info_resp_msg->pn_min_memory     = 0; */
		job_info_resp_msg->ntasks_per_board  = NO_VAL16;
		job_info_resp_msg->ntasks_per_core   = NO_VAL16;
		job_info_resp_msg->ntasks_per_socket = NO_VAL16;
	}

	if (job_ptr->details && job_ptr->details->env_cnt) {
		job_info_resp_msg->env_size = job_ptr->details->env_cnt;
		job_info_resp_msg->environment =
			xcalloc(job_info_resp_msg->env_size + 1,
				sizeof(char *));
		for (i = 0; i < job_info_resp_msg->env_size; i++) {
			job_info_resp_msg->environment[i] =
				xstrdup(job_ptr->details->env_sup[i]);
		}
		job_info_resp_msg->environment[i] = NULL;
	}

	job_info_resp_msg->uid = job_ptr->user_id;
	job_info_resp_msg->user_name = user_from_job(job_ptr);
	job_info_resp_msg->gid = job_ptr->group_id;
	job_info_resp_msg->group_name = group_from_job(job_ptr);

	return job_info_resp_msg;
}
