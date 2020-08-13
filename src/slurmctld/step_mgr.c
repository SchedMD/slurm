/*****************************************************************************\
 *  step_mgr.c - manage the job step information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2012-2016 SchedMD LLC.
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

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/forward.h"
#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/switch.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/xstring.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/port_mgr.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

typedef struct {
	uint16_t flags;
	bool found;
	int rc_in;
	uint16_t signal;
	slurm_step_id_t step_id;
	uid_t uid;
} step_signal_t;

static void _build_pending_step(job_record_t *job_ptr,
				job_step_create_request_msg_t *step_specs);
static int  _count_cpus(job_record_t *job_ptr, bitstr_t *bitmap,
			uint32_t *usable_cpu_cnt);
static step_record_t *_create_step_record(job_record_t *job_ptr,
					  uint16_t protocol_version);
static void _dump_step_layout(step_record_t *step_ptr);
static void _free_step_rec(step_record_t *step_ptr);
static bool _is_mem_resv(void);
static int  _opt_cpu_cnt(uint32_t step_min_cpus, bitstr_t *node_bitmap,
			 uint32_t *usable_cpu_cnt);
static int  _opt_node_cnt(uint32_t step_min_nodes, uint32_t step_max_nodes,
			  int nodes_avail, int nodes_picked_cnt);
static void _pack_ctld_job_step_info(step_record_t *step, Buf buffer,
				     uint16_t protocol_version);
static bitstr_t *_pick_step_nodes(job_record_t *job_ptr,
				  job_step_create_request_msg_t *step_spec,
				  List step_gres_list, int cpus_per_task,
				  uint32_t node_count,
				  dynamic_plugin_data_t *select_jobinfo,
				  int *return_code);
static bitstr_t *_pick_step_nodes_cpus(job_record_t *job_ptr,
				       bitstr_t *nodes_bitmap, int node_cnt,
				       int cpu_cnt, uint32_t *usable_cpu_cnt);
static int _purge_duplicate_steps(job_record_t *job_ptr,
				  job_step_create_request_msg_t *step_specs);
static hostlist_t _step_range_to_hostlist(step_record_t *step_ptr,
					  uint32_t range_first,
					  uint32_t range_last);
static int _step_hostname_to_inx(step_record_t *step_ptr, char *node_name);
static void _step_dealloc_lps(step_record_t *step_ptr);

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
 * IN protocol_version - slurm protocol version of client
 * RET a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
static step_record_t *_create_step_record(job_record_t *job_ptr,
					  uint16_t protocol_version)
{
	step_record_t *step_ptr;

	xassert(job_ptr);
	/* NOTE: Reserve highest step ID values for
	 * SLURM_EXTERN_CONT and SLURM_BATCH_SCRIPT and any other
	 * special step that may come our way. */
	if (job_ptr->next_step_id >= SLURM_MAX_NORMAL_STEP_ID) {
		/* avoid step records in the accounting database */
		info("%pJ has reached step id limit", job_ptr);
		return NULL;
	}

	step_ptr = xmalloc(sizeof(*step_ptr));

	last_job_update = time(NULL);
	step_ptr->job_ptr    = job_ptr;
	step_ptr->exit_code  = NO_VAL;
	step_ptr->time_limit = INFINITE;
	step_ptr->jobacct    = jobacctinfo_create(NULL);
	step_ptr->requid     = -1;
	if (protocol_version)
		step_ptr->start_protocol_ver = protocol_version;
	else
		step_ptr->start_protocol_ver = job_ptr->start_protocol_ver;

	step_ptr->magic = STEP_MAGIC;
	(void) list_append (job_ptr->step_list, step_ptr);

	return step_ptr;
}

/* Purge any duplicate job steps for this PID */
static int _purge_duplicate_steps(job_record_t *job_ptr,
				  job_step_create_request_msg_t *step_specs)
{
	ListIterator step_iterator;
	step_record_t *step_ptr;
	int rc = SLURM_SUCCESS;
	xassert(job_ptr);

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if ((step_ptr->step_id.step_id == SLURM_PENDING_STEP)   &&
		    (step_ptr->state    == JOB_PENDING)          &&
		    (step_ptr->srun_pid	== step_specs->srun_pid) &&
		    (xstrcmp(step_ptr->host, step_specs->host) == 0)) {
			list_remove (step_iterator);
			_free_step_rec(step_ptr);
			break;
		}

		if (step_specs->step_id.step_id == NO_VAL)
			continue;

		/*
		 * See if we have the same step id.  If we do check to see if we
		 * have the same step_het_comp or if the step's is NO_VAL,
		 * meaning this step is not a het step.
		 */
		if ((step_specs->step_id.step_id ==
		     step_ptr->step_id.step_id) &&
		    ((step_specs->step_id.step_het_comp ==
		      step_ptr->step_id.step_het_comp) ||
		     (step_ptr->step_id.step_het_comp == NO_VAL)))
			rc = ESLURM_DUPLICATE_STEP_ID;
	}
	list_iterator_destroy(step_iterator);

	return rc;
}

/* The step with a state of PENDING is used as a placeholder for a host and
 * port that can be used to wake a pending srun as soon another step ends */
static void _build_pending_step(job_record_t *job_ptr,
				job_step_create_request_msg_t *step_specs)
{
	step_record_t *step_ptr;

	if ((step_specs->host == NULL) || (step_specs->port == 0))
		return;

	step_ptr = _create_step_record(job_ptr, 0);
	if (step_ptr == NULL)
		return;

	step_ptr->cpu_count	= step_specs->num_tasks;
	step_ptr->port		= step_specs->port;
	step_ptr->srun_pid	= step_specs->srun_pid;
	step_ptr->host		= xstrdup(step_specs->host);
	step_ptr->state		= JOB_PENDING;
	step_ptr->step_id.job_id = job_ptr->job_id;
	step_ptr->step_id.step_id = SLURM_PENDING_STEP;
	step_ptr->step_id.step_het_comp = NO_VAL;
	if (job_ptr->node_bitmap)
		step_ptr->step_node_bitmap = bit_copy(job_ptr->node_bitmap);
	step_ptr->time_last_active = time(NULL);

}

static void _internal_step_complete(job_record_t *job_ptr,
				    step_record_t *step_ptr)
{
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
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
	    (list_count(job_ptr->step_list) == 1)) {
		set_job_tres_alloc_str(job_ptr, false);
		/* This flag says we have processed the tres alloc including
		 * energy from all steps, so don't process or handle it again
		 * with the job.  It also tells the slurmdbd plugin to send it
		 * to the DBD.
		 */
		job_ptr->bit_flags |= TRES_STR_CALC;
	}

	jobacct_storage_g_step_complete(acct_db_conn, step_ptr);

	if (step_ptr->step_id.step_id == SLURM_PENDING_STEP)
		return;

	/*
	 * Derived exit code is the highest exit code of srun steps, so we
	 * exclude the batch and extern steps.
	 */
	if ((step_ptr->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (step_ptr->step_id.step_id != SLURM_BATCH_SCRIPT) &&
	    ((step_ptr->exit_code == SIG_OOM) ||
	     (step_ptr->exit_code > job_ptr->derived_ec)))
		job_ptr->derived_ec = step_ptr->exit_code;

	step_ptr->state |= JOB_COMPLETING;
	select_g_step_finish(step_ptr, false);
	post_job_step(step_ptr);
}

/*
 * _find_step_id - Find specific step_id entry in the step list,
 *		   see common/list.h for documentation
 * - object - the step list from a job_record_t
 * - key - slurm_step_id_t
 */
static int _find_step_id(void *object, void *key)
{
	step_record_t *step_ptr = (step_record_t *)object;
	slurm_step_id_t *step_id = (slurm_step_id_t *)key;

	return verify_step_id(&step_ptr->step_id, step_id);
}

static int _step_signal(void *object, void *arg)
{
	step_record_t *step_ptr = (step_record_t *)object;
	step_signal_t *step_signal = (step_signal_t *)arg;
	uint16_t signal;
	int rc;


	if (!(step_signal->flags & KILL_FULL_JOB) &&
	    !_find_step_id(step_ptr, &step_signal->step_id))
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

/*
 * delete_step_records - Delete step record for specified job_ptr.
 * This function is called when a step fails to run to completion. For example,
 * when the job is killed due to reaching its time limit or allocated nodes
 * go DOWN.
 * IN job_ptr - pointer to job table entry to have step records removed
 */
extern void delete_step_records(job_record_t *job_ptr)
{
	ListIterator step_iterator;
	step_record_t *step_ptr;

	xassert(job_ptr);

	last_job_update = time(NULL);
	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		/* Only check if not a pending step */
		if (step_ptr->step_id.step_id != SLURM_PENDING_STEP) {
			uint16_t cleaning = 0;
			select_g_select_jobinfo_get(step_ptr->select_jobinfo,
						    SELECT_JOBDATA_CLEANING,
						    &cleaning);
			if (cleaning)	/* Step already in cleanup. */
				continue;
			/* _internal_step_complete() will purge step record */
			_internal_step_complete(job_ptr, step_ptr);
		} else {
			_internal_step_complete(job_ptr, step_ptr);
			list_remove (step_iterator);
			_free_step_rec(step_ptr);
		}
	}
	list_iterator_destroy(step_iterator);
}

/*
 * step_list_purge - Simple purge of a job's step list records. No testing is
 *	performed to ensure the step records has no active references.
 *	**ONLY CALL WHEN FREEING MEMORY AT END OF JOB**
 * IN job_ptr - pointer to job table entry to have step records removed
 */
extern void step_list_purge(job_record_t *job_ptr)
{
	ListIterator step_iterator;
	step_record_t *step_ptr;

	xassert(job_ptr);
	if (job_ptr->step_list == NULL)
		return;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		list_remove (step_iterator);
		_free_step_rec(step_ptr);
	}
	list_iterator_destroy(step_iterator);
	FREE_NULL_LIST(job_ptr->step_list);
}

/* _free_step_rec - delete a step record's data structures */
static void _free_step_rec(step_record_t *step_ptr)
{
	xassert(step_ptr);
	xassert(step_ptr->magic == STEP_MAGIC);
/*
 * FIXME: If job step record is preserved after completion,
 * the switch_g_job_step_complete() must be called upon completion
 * and not upon record purging. Presently both events occur simultaneously.
 */
	if (step_ptr->switch_job) {
		if (step_ptr->step_layout)
			switch_g_job_step_complete(
				step_ptr->switch_job,
				step_ptr->step_layout->node_list);
		switch_g_free_jobinfo (step_ptr->switch_job);
	}
	resv_port_free(step_ptr);

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
	FREE_NULL_LIST(step_ptr->gres_list);
	select_g_select_jobinfo_free(step_ptr->select_jobinfo);
	xfree(step_ptr->tres_alloc_str);
	xfree(step_ptr->tres_fmt_alloc_str);
	xfree(step_ptr->ext_sensors);
	xfree(step_ptr->cpus_per_tres);
	xfree(step_ptr->mem_per_tres);
	xfree(step_ptr->tres_bind);
	xfree(step_ptr->tres_freq);
	xfree(step_ptr->tres_per_step);
	xfree(step_ptr->tres_per_node);
	xfree(step_ptr->tres_per_socket);
	xfree(step_ptr->tres_per_task);
	xfree(step_ptr);
}

/*
 * delete_step_record - delete record for job step for specified job_ptr
 *	and step_id
 * IN job_ptr - pointer to job table entry to have step record removed
 * IN step_ptr - pointer to step table entry of the desired job step
 * RET 0 on success, errno otherwise
 */
int delete_step_record(job_record_t *job_ptr, step_record_t *step_ptr_in)
{
	ListIterator step_iterator;
	step_record_t *step_ptr;
	int error_code;
	uint16_t cleaning = 0;

	xassert(job_ptr);
	error_code = ENOENT;
	if (!job_ptr->step_list)
		return error_code;

	last_job_update = time(NULL);
	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr != step_ptr_in)
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
	debug3("   host=%s port=%u srun_pid=%u name=%s network=%s exclusive=%u",
	       step_spec->host, step_spec->port, step_spec->srun_pid,
	       step_spec->name, step_spec->network, step_spec->exclusive);
	debug3("   mem_per_%s=%"PRIu64" resv_port_cnt=%u immediate=%u"
	       " no_kill=%u", mem_type, mem_value, step_spec->resv_port_cnt,
	       step_spec->immediate, step_spec->no_kill);
	debug3("   overcommit=%d time_limit=%u",
	       step_spec->overcommit, step_spec->time_limit);

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
}

/*
 * find_step_record - return a pointer to the step record with the given
 *	job_id and step_id
 * IN job_ptr - pointer to job table entry to have step record added
 * IN step_id - id+het_comp of the desired job step
 * RET pointer to the job step's record, NULL on error
 */
step_record_t *find_step_record(job_record_t *job_ptr, slurm_step_id_t *step_id)
{
	if (job_ptr == NULL)
		return NULL;

	return list_find_first(job_ptr->step_list, _find_step_id, step_id);
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
	step_signal_t step_signal = { .flags = flags,
				      .found = false,
				      .rc_in = SLURM_SUCCESS,
				      .signal = signal,
				      .uid = uid };

	memcpy(&step_signal.step_id, step_id, sizeof(step_signal.step_id));

	job_ptr = find_job_record(step_id->job_id);
	if (job_ptr == NULL) {
		error("job_step_signal: invalid JobId=%u", step_id->job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if ((job_ptr->user_id != uid) && !validate_slurm_user(uid)) {
		error("Security violation, JOB_CANCEL RPC from uid %d",
		      uid);
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
	int i, i_first, i_last;
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

#ifdef HAVE_FRONT_END
	xassert(step_ptr->job_ptr->batch_host);
	if (step_ptr->job_ptr->front_end_ptr)
		agent_args->protocol_version =
			step_ptr->job_ptr->front_end_ptr->protocol_version;
	hostlist_push_host(agent_args->hostlist, step_ptr->job_ptr->batch_host);
	agent_args->node_count = 1;
#else
	agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	i_first = bit_ffs(step_ptr->step_node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(step_ptr->step_node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
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
		xfree(signal_tasks_msg);
		hostlist_destroy(agent_args->hostlist);
		xfree(agent_args);
		return;
	}

	agent_args->msg_args = signal_tasks_msg;
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
	agent_queue_request(agent_args);
	return;
}

/* A step just completed, signal srun processes with pending steps to retry */
static void _wake_pending_steps(job_record_t *job_ptr)
{
	static int config_start_count = -1, config_max_age = -1;
	ListIterator step_iterator;
	step_record_t *step_ptr;
	int start_count = 0;
	time_t max_age;

	if (!IS_JOB_RUNNING(job_ptr))
		return;

	if (!job_ptr->step_list)
		return;

	if (config_start_count == -1) {
		char *sched_params = slurm_get_sched_params();
		char *tmp_ptr;
		long int param;
		config_start_count = 8;
		config_max_age = 60;

		if ((tmp_ptr = xstrcasestr(sched_params, "step_retry_count="))) {
			param = strtol(tmp_ptr + 17, NULL, 10);
			if ((param >= 1) && (param != LONG_MIN) &&
			    (param != LONG_MAX))
				config_start_count = param;
		}
		if ((tmp_ptr = xstrcasestr(sched_params, "step_retry_time="))) {
			param = strtol(tmp_ptr + 16, NULL, 10);
			if ((param >= 1) && (param != LONG_MIN) &&
			    (param != LONG_MAX))
				config_max_age = param;
		}
		xfree(sched_params);
	}
	max_age = time(NULL) - config_max_age;

	/* We do not know which steps can use currently available resources.
	 * Try to start a bit more based upon step sizes. Effectiveness
	 * varies with step sizes, constraints and order. */
	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if ((step_ptr->state == JOB_PENDING) &&
		    ((start_count < config_start_count) ||
		     (step_ptr->time_last_active <= max_age))) {
			srun_step_signal(step_ptr, 0);
			list_remove(step_iterator);
			/* Step never started, no need to check
			 * SELECT_JOBDATA_CLEANING. */
			_free_step_rec(step_ptr);
			start_count++;
		}
	}
	list_iterator_destroy (step_iterator);
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
	int first_bit, last_bit;
	int cpu_target;	/* Target number of CPUs per allocated node */
	int rem_nodes, rem_cpus, save_rem_nodes, save_rem_cpus;
	int i;

	xassert(node_cnt > 0);
	xassert(nodes_bitmap);
	xassert(usable_cpu_cnt);
	cpu_target = (cpu_cnt + node_cnt - 1) / node_cnt;
	if (cpu_target > 1024)
		info("%s: high cpu_target (%d)", __func__, cpu_target);
	if ((cpu_cnt <= node_cnt) || (cpu_target > 1024))
		return bit_pick_cnt(nodes_bitmap, node_cnt);

	/* Need to satisfy both a node count and a cpu count */
	picked_node_bitmap = bit_alloc(node_record_count);
	usable_cpu_array = xcalloc(cpu_target, sizeof(int));
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
static bitstr_t *_pick_step_nodes(job_record_t *job_ptr,
				  job_step_create_request_msg_t *step_spec,
				  List step_gres_list, int cpus_per_task,
				  uint32_t node_count,
				  dynamic_plugin_data_t *select_jobinfo,
				  int *return_code)
{
	int node_inx, first_bit, last_bit;
	node_record_t *node_ptr;
	bitstr_t *nodes_avail = NULL, *nodes_idle = NULL;
	bitstr_t *select_nodes_avail = NULL;
	bitstr_t *nodes_picked = NULL, *node_tmp = NULL;
	int error_code, nodes_picked_cnt = 0, cpus_picked_cnt = 0;
	int cpu_cnt, i, task_cnt, max_rem_nodes;
	int mem_blocked_nodes = 0, mem_blocked_cpus = 0;
	ListIterator step_iterator;
	step_record_t *step_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	uint32_t *usable_cpu_cnt = NULL;
	uint64_t gres_cpus;
	int i_first, i_last;
	bool first_step_node = true;

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
	max_rem_nodes = step_spec->max_nodes;

	/*
	 * If we have a select plugin that selects step resources, then use it
	 * and return (does not happen today). Otherwise select step resources
	 * in this function.
	 */
	if ((nodes_picked = select_g_step_pick_nodes(job_ptr, select_jobinfo,
						     node_count,
						     &select_nodes_avail)))
		return nodes_picked;

	if (!nodes_avail)
		nodes_avail = bit_copy (job_ptr->node_bitmap);
	bit_and(nodes_avail, up_node_bitmap);

	if (step_spec->exc_nodes) {
		bitstr_t *exc_bitmap = NULL;
		error_code = node_name2bitmap(step_spec->exc_nodes, false,
					      &exc_bitmap);
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
				&req_nodes);
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

	if (step_spec->features) {
		/*
		 * We only select for a single feature name here.
		 * FIXME: Add support for AND, OR, etc. here if desired
		 */
		node_feature_t *feat_ptr;
		feat_ptr = list_find_first(active_feature_list,
					   list_find_feature,
					   (void *) step_spec->features);
		if (feat_ptr && feat_ptr->node_bitmap)
			bit_and(nodes_avail, feat_ptr->node_bitmap);
		else
			bit_clear_all(nodes_avail);
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
		if (job_ptr->details && job_ptr->details->prolog_running) {
			*return_code = ESLURM_PROLOG_RUNNING;
			FREE_NULL_BITMAP(nodes_avail);
			FREE_NULL_BITMAP(select_nodes_avail);
			return NULL;
		}
		i_first = bit_ffs(job_ptr->node_bitmap);
		if (i_first >= 0)
			i_last  = bit_fls(job_ptr->node_bitmap);
		else
			i_last = -2;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (IS_NODE_POWER_SAVE(node_ptr) ||
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
			job_config_fini(job_ptr);
		}
	}

	/*
	 * Exclusive mode:
	 * Do not use nodes with insufficient CPUs, memory or GRES.
	 */
	if (step_spec->exclusive) {
		int avail_cpus, avail_tasks, total_cpus, total_tasks, node_inx;
		uint64_t avail_mem, total_mem;
		uint32_t nodes_picked_cnt = 0;
		uint32_t tasks_picked_cnt = 0, total_task_cnt = 0;
		bitstr_t *selected_nodes = NULL, *non_selected_nodes = NULL;
		int *non_selected_tasks = NULL;

		if (step_spec->node_list) {
			error_code = node_name2bitmap(step_spec->node_list,
						      false,
						      &selected_nodes);
			if (error_code) {
				info("%s: invalid node list (%s) for %pJ",
				     __func__, step_spec->node_list, job_ptr);
				FREE_NULL_BITMAP(selected_nodes);
				goto cleanup;
			}
			if (!bit_super_set(selected_nodes,
					   job_ptr->node_bitmap)) {
				info("%s: selected nodes (%s) not in %pJ",
				     __func__, step_spec->node_list, job_ptr);
				FREE_NULL_BITMAP(selected_nodes);
				goto cleanup;
			}
			if (!bit_super_set(selected_nodes, up_node_bitmap)) {
				info("%s: selected nodes (%s) are DOWN",
				     __func__, step_spec->node_list);
				FREE_NULL_BITMAP(selected_nodes);
				goto cleanup;
			}
			non_selected_nodes = bit_alloc(node_record_count);
			non_selected_tasks = xcalloc(node_record_count,
						     sizeof(int));
		}

		node_inx = -1;
		i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
		if (i_first >= 0)
			i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
		else
			i_last = -2;
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
				uint64_t mem_use = step_spec->pn_min_memory;
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
				uint64_t mem_use = step_spec->pn_min_memory;

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

			gres_cpus = gres_plugin_step_test(step_gres_list,
						job_ptr->gres_list, node_inx,
						first_step_node, cpus_per_task,
						max_rem_nodes, false,
						job_ptr->job_id, NO_VAL);
			if ((gres_cpus != NO_VAL64) && (cpus_per_task > 0))
				gres_cpus /= cpus_per_task;
			avail_tasks = MIN((uint64_t)avail_tasks, gres_cpus);
			gres_cpus = gres_plugin_step_test(step_gres_list,
						job_ptr->gres_list, node_inx,
						first_step_node, cpus_per_task,
						max_rem_nodes, true,
						job_ptr->job_id, NO_VAL);
			if ((gres_cpus != NO_VAL64) && (cpus_per_task > 0))
				gres_cpus /= cpus_per_task;
			total_tasks = MIN((uint64_t)total_tasks, gres_cpus);
			if (step_spec->plane_size &&
			    step_spec->plane_size != NO_VAL16) {
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
				max_rem_nodes--;
				first_step_node = false;
			}
		}

		if (selected_nodes) {
			if (!bit_super_set(selected_nodes, nodes_avail)) {
				/*
				 * Some required nodes have no available
				 * processors, defer request
				 */
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
			/*
			 * The select plugin told us these were the
			 * only nodes we could choose from.  If step
			 * doesn't fit here then defer request
			 */
			if (!bit_super_set(nodes_avail, select_nodes_avail)) {
				tasks_picked_cnt = 0;
			}
			FREE_NULL_BITMAP(selected_nodes);
		}

		if (tasks_picked_cnt >= step_spec->num_tasks)
			return nodes_avail;
		FREE_NULL_BITMAP(nodes_avail);
		FREE_NULL_BITMAP(select_nodes_avail);

		if (total_task_cnt >= step_spec->num_tasks)
			*return_code = ESLURM_NODES_BUSY;
		else
			*return_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		return NULL;
	}

	if ((step_spec->pn_min_memory && _is_mem_resv()) || step_gres_list) {
		int fail_mode = ESLURM_INVALID_TASK_MEMORY;
		uint64_t tmp_mem;
		uint32_t tmp_cpus, avail_cpus, total_cpus;
		uint32_t avail_tasks, total_tasks;

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
		first_bit = bit_ffs(job_resrcs_ptr->node_bitmap);
		if (first_bit >= 0)
			last_bit  = bit_fls(job_resrcs_ptr->node_bitmap);
		else
			last_bit = -2;
		for (i = first_bit, node_inx = -1; i <= last_bit; i++) {
			if (!bit_test(job_resrcs_ptr->node_bitmap, i))
				continue;
			node_inx++;
			if (!bit_test(nodes_avail, i))
				continue;	/* node now DOWN */

			total_cpus = job_resrcs_ptr->cpus[node_inx];
			usable_cpu_cnt[i] = avail_cpus = total_cpus;
			if (_is_mem_resv() &&
			    step_spec->pn_min_memory & MEM_PER_CPU) {
				uint64_t mem_use = step_spec->pn_min_memory;
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
				uint64_t mem_use = step_spec->pn_min_memory;
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

			/* ignore current step allocations */
			gres_cpus = gres_plugin_step_test(step_gres_list,
						job_ptr->gres_list, node_inx,
						first_step_node, cpus_per_task,
						max_rem_nodes, true,
						job_ptr->job_id, NO_VAL);
			total_cpus = MIN(total_cpus, gres_cpus);
			/* consider current step allocations */
			gres_cpus = gres_plugin_step_test(step_gres_list,
						job_ptr->gres_list, node_inx,
						first_step_node, cpus_per_task,
						max_rem_nodes, false,
						job_ptr->job_id, NO_VAL);
			if (gres_cpus < avail_cpus) {
				avail_cpus = gres_cpus;
				usable_cpu_cnt[i] = avail_cpus;
				fail_mode = ESLURM_INVALID_GRES;
			}

			avail_tasks = avail_cpus;
			total_tasks = total_cpus;
			if (cpus_per_task > 0) {
				avail_tasks /= cpus_per_task;
				total_tasks /= cpus_per_task;
			}
			if (avail_tasks == 0) {
				if ((step_spec->min_nodes == INFINITE) ||
				    (step_spec->min_nodes ==
				     job_ptr->node_cnt)) {
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
				max_rem_nodes--;
				first_step_node = false;
			}
		}
	}

	if (step_spec->min_nodes == INFINITE) {	/* use all nodes */
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

	if (step_spec->node_list) {
		bitstr_t *selected_nodes = NULL;
		log_flag(STEPS, "%s: selected nodelist is %s",
			 __func__, step_spec->node_list);
		error_code = node_name2bitmap(step_spec->node_list, false,
					      &selected_nodes);
		if (error_code) {
			info("%s: invalid node list %s", __func__,
			     step_spec->node_list);
			FREE_NULL_BITMAP(selected_nodes);
			goto cleanup;
		}
		if (!bit_super_set(selected_nodes, job_ptr->node_bitmap)) {
			info("%s: requested nodes %s not part of %pJ",
			     __func__, step_spec->node_list, job_ptr);
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
				info("%s: requested nodes %s have inadequate memory",
				     __func__, step_spec->node_list);
			} else {
				*return_code = ESLURM_NODES_BUSY;
				info ("%s: some requested nodes %s still have memory used by other steps",
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
				info("%s: requested nodes %s exceed max node count for %pJ (%d > %u)",
				     __func__, step_spec->node_list, job_ptr,
				     node_cnt, step_spec->max_nodes);
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
			info("%s: Invalid relative value (%u) for %pJ",
			     __func__, step_spec->relative, job_ptr);
			goto cleanup;
		}
		bit_and_not(nodes_avail, relative_nodes);
		FREE_NULL_BITMAP(relative_nodes);
	} else {
		nodes_idle = bit_alloc (bit_size (nodes_avail) );
		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = list_next(step_iterator))) {
			if (step_ptr->state < JOB_RUNNING)
				continue;

			/*
			 * Don't consider the batch and extern steps when
			 * looking for "idle" nodes.
			 */
			if ((step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT) ||
			    (step_ptr->step_id.step_id == SLURM_EXTERN_CONT))
				continue;

			if (!step_ptr->step_node_bitmap) {
				error("%s: %pS has no step_node_bitmap",
				      __func__, step_ptr);
				continue;
			}

			bit_or(nodes_idle, step_ptr->step_node_bitmap);
			if (slurm_conf.debug_flags & DEBUG_FLAG_STEPS) {
				char *temp;
				temp = bitmap2node_name(step_ptr->
							step_node_bitmap);
				info("%s: %pS has nodes %s", __func__,
				     step_ptr, temp);
				xfree(temp);
			}
		}
		list_iterator_destroy (step_iterator);
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
		info("%s: step pick %u-%u nodes, avail:%s idle:%s picked:%s",
		     __func__, step_spec->min_nodes, step_spec->max_nodes,
		     temp1, temp2, temp3);
		xfree(temp1);
		xfree(temp2);
	}

	/*
	 * If user specifies step needs a specific processor count and
	 * all nodes have the same processor count, just translate this to
	 * a node count
	 */
	if (step_spec->cpu_count && job_ptr->job_resrcs &&
	    (job_ptr->job_resrcs->cpu_array_cnt == 1) &&
	    (job_ptr->job_resrcs->cpu_array_value)) {
		i = (step_spec->cpu_count +
		     (job_ptr->job_resrcs->cpu_array_value[0] - 1)) /
		    job_ptr->job_resrcs->cpu_array_value[0];
		step_spec->min_nodes = (i > step_spec->min_nodes) ?
					i : step_spec->min_nodes ;
		if (step_spec->max_nodes < step_spec->min_nodes) {
			info("%s: %pJ max node less than min node count (%u < %u)",
			     __func__, job_ptr, step_spec->max_nodes,
			     step_spec->min_nodes);
			*return_code = ESLURM_TOO_MANY_REQUESTED_CPUS;
			goto cleanup;
		}
	}

	if (step_spec->min_nodes) {
		int cpus_needed, node_avail_cnt, nodes_needed;

		if (usable_cpu_cnt == NULL) {
			usable_cpu_cnt = xcalloc(node_record_count,
						 sizeof(uint32_t));
			first_bit = bit_ffs(job_resrcs_ptr->node_bitmap);
			if (first_bit >= 0)
				last_bit  = bit_fls(job_resrcs_ptr->node_bitmap);
			else
				last_bit = -2;
			for (i = first_bit, node_inx = -1; i <= last_bit; i++) {
				if (!bit_test(job_resrcs_ptr->node_bitmap, i))
					continue;
				node_inx++;
				usable_cpu_cnt[i] = job_resrcs_ptr->
						    cpus[node_inx];
			}

		}
		nodes_picked_cnt = bit_set_count(nodes_picked);
		log_flag(STEPS, "%s: step picked %d of %u nodes",
			 __func__, nodes_picked_cnt, step_spec->min_nodes);
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
			bit_or(nodes_picked, node_tmp);
			bit_and_not(nodes_avail, node_tmp);
			FREE_NULL_BITMAP(node_tmp);
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
		/*
		 * Return ESLURM_NODES_BUSY if the node is not responding.
		 * The node will eventually either come back UP or go DOWN.
		 */
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
static int _count_cpus(job_record_t *job_ptr, bitstr_t *bitmap,
		       uint32_t *usable_cpu_cnt)
{
	int i, sum = 0;
	node_record_t *node_ptr;

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
		error("%pJ lacks cpus array", job_ptr);
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (!bit_test(bitmap, i))
				continue;
			sum += node_ptr->config_ptr->cpus;
		}
	}

	return sum;
}

/* Update the step's core bitmaps, create as needed.
 *	Add the specified task count for a specific node in the job's
 *	and step's allocation */
static void _pick_step_cores(step_record_t *step_ptr,
			     job_resources_t *job_resrcs_ptr,
			     int job_node_inx, uint16_t task_cnt,
			     uint16_t cpus_per_core)
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

	if (step_ptr->cpus_per_task > 0) {
		cpu_cnt *= step_ptr->cpus_per_task + cpus_per_core - 1;
		cpu_cnt	/= cpus_per_core;
	}

	/* select idle cores first */
	for (sock_inx=0; sock_inx<sockets; sock_inx++) {
		for (core_inx=0; core_inx<cores; core_inx++) {
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

static bool _use_one_thread_per_core(job_record_t *job_ptr)
{
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;

	if ((job_resrcs_ptr->whole_node != 1) &&
	    (slurm_conf.select_type_param & (CR_CORE | CR_SOCKET)) &&
	    (job_ptr->details &&
	     (job_ptr->details->cpu_bind_type != NO_VAL16) &&
	     (job_ptr->details->cpu_bind_type & CPU_BIND_ONE_THREAD_PER_CORE)))
		return true;
	return false;
}

/* Update a job's record of allocated CPUs when a job step gets scheduled */
extern void step_alloc_lps(step_record_t *step_ptr)
{
	job_record_t *job_ptr = step_ptr->job_ptr;
	job_resources_t *job_resrcs_ptr = job_ptr->job_resrcs;
	int cpus_alloc;
	int i_node, i_first, i_last;
	int job_node_inx = -1, step_node_inx = -1;
	bool first_step_node = true, pick_step_cores = true;
	uint32_t rem_nodes;

	xassert(job_resrcs_ptr);
	xassert(job_resrcs_ptr->cpus);
	xassert(job_resrcs_ptr->cpus_used);

	if (step_ptr->step_layout == NULL)	/* batch step */
		return;

	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
	if (i_first == -1)	/* empty bitmap */
		return;

	xassert(job_resrcs_ptr->core_bitmap);
	xassert(job_resrcs_ptr->core_bitmap_used);
	if (step_ptr->core_bitmap_job) {
		/* "scontrol reconfig" of live system */
		pick_step_cores = false;
	} else if ((step_ptr->exclusive == 0) ||
		   (step_ptr->cpu_count == job_ptr->total_cpus)) {
		/*
		 * Step uses all of job's cores
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

	rem_nodes = bit_set_count(step_ptr->step_node_bitmap);
	for (i_node = i_first; i_node <= i_last; i_node++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i_node))
			continue;
		job_node_inx++;
		if (!bit_test(step_ptr->step_node_bitmap, i_node))
			continue;
		step_node_inx++;
		if (job_node_inx >= job_resrcs_ptr->nhosts)
			fatal("%s: node index bad", __func__);

		/*
		 * NOTE: The --overcommit option can result in
		 * cpus_used[] having a higher value than cpus[]
		 */
		cpus_alloc = step_ptr->step_layout->tasks[step_node_inx] *
			     step_ptr->cpus_per_task;
		job_resrcs_ptr->cpus_used[job_node_inx] += cpus_alloc;
		gres_plugin_step_alloc(step_ptr->gres_list, job_ptr->gres_list,
				job_node_inx, first_step_node,
				step_ptr->step_layout->tasks[step_node_inx],
				rem_nodes, job_ptr->job_id,
				step_ptr->step_id.step_id);
		first_step_node = false;
		rem_nodes--;
		if (step_ptr->pn_min_memory && _is_mem_resv()) {
			if (step_ptr->pn_min_memory & MEM_PER_CPU) {
				uint64_t mem_use = step_ptr->pn_min_memory;
				mem_use &= (~MEM_PER_CPU);
				job_resrcs_ptr->memory_used[job_node_inx] +=
					(mem_use * cpus_alloc);
			} else {
				job_resrcs_ptr->memory_used[job_node_inx] +=
					step_ptr->pn_min_memory;
			}
		}
		if (pick_step_cores) {
			uint16_t cpus_per_core = 1;
			/*
			 * Here we're setting number of CPUs per core
			 * if we don't enforce 1 thread per core
			 *
			 * TODO: move cpus_per_core to slurm_step_layout_t
			 */
			if (!_use_one_thread_per_core(job_ptr))
				cpus_per_core =
					node_record_table_ptr[i_node].threads;
			_pick_step_cores(step_ptr, job_resrcs_ptr,
					 job_node_inx,
					 step_ptr->step_layout->
					 tasks[step_node_inx],
					 cpus_per_core);
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND)
			_dump_step_layout(step_ptr);
		log_flag(STEPS, "step alloc on job node %d (%s) used %u of %u CPUs",
			 job_node_inx, node_record_table_ptr[i_node].name,
			 job_resrcs_ptr->cpus_used[job_node_inx],
			 job_resrcs_ptr->cpus[job_node_inx]);
		if (step_node_inx == (step_ptr->step_layout->node_cnt - 1))
			break;
	}
	gres_plugin_step_state_log(step_ptr->gres_list, job_ptr->job_id,
				   step_ptr->step_id.step_id);
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
		error("%s: lack memory allocation details to enforce memory limits for %pJ",
		      __func__, job_ptr);
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
		cpus_alloc = step_ptr->step_layout->tasks[step_node_inx] *
			     step_ptr->cpus_per_task;
		if (job_resrcs_ptr->cpus_used[job_node_inx] >= cpus_alloc) {
			job_resrcs_ptr->cpus_used[job_node_inx] -= cpus_alloc;
		} else {
			error("%s: CPU underflow for %pS (%u<%u on job node %d)",
			      __func__, step_ptr,
			      job_resrcs_ptr->cpus_used[job_node_inx],
			      cpus_alloc, job_node_inx);
			job_resrcs_ptr->cpus_used[job_node_inx] = 0;
		}
		if (step_ptr->pn_min_memory && _is_mem_resv()) {
			uint64_t mem_use = step_ptr->pn_min_memory;
			if (mem_use & MEM_PER_CPU) {
				mem_use &= (~MEM_PER_CPU);
				mem_use *= cpus_alloc;
			}
			if (job_resrcs_ptr->memory_used[job_node_inx] >=
			    mem_use) {
				job_resrcs_ptr->memory_used[job_node_inx] -=
						mem_use;
			} else {
				error("%s: mem underflow for %pS",
				      __func__, step_ptr);
				job_resrcs_ptr->memory_used[job_node_inx] = 0;
			}
		}
		log_flag(STEPS, "step dealloc on job node %d (%s) used: %u of %u CPUs",
			 job_node_inx, node_record_table_ptr[i_node].name,
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
		if (job_core_size == step_core_size) {
			bit_and_not(job_resrcs_ptr->core_bitmap_used,
				    step_ptr->core_bitmap_job);
		} else if (job_ptr->bit_flags & JOB_RESIZED) {
			/*
			 * If a job is resized, the core bitmap will differ in
			 * the step. See rebuild_step_bitmaps(). The problem
			 * will go away when we have per-node core bitmaps.
			 */
			info("%s: %pS ending, unable to update job's core use information due to job resizing",
			     __func__, step_ptr);
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
	int i, i_first, i_last;
	uint32_t bind_bits, bind_to_bits, node_bind = NO_VAL;
	bool node_fail = false;

	if (!job_ptr->details || !job_resrcs_ptr ||
	    !job_resrcs_ptr->node_bitmap)
		return;		/* No data structure */

	bind_to_bits = CPU_BIND_TO_SOCKETS | CPU_BIND_TO_CORES |
		       CPU_BIND_TO_THREADS | CPU_BIND_TO_LDOMS |
		       CPU_BIND_TO_BOARDS;
	if ((job_ptr->details->cpu_bind_type != NO_VAL16) &&
	    (job_ptr->details->cpu_bind_type & bind_to_bits))
		return;		/* Already set */
	bind_bits = job_ptr->details->cpu_bind_type & CPU_BIND_VERBOSE;

	/*
	 * Set job's cpu_bind to the node's cpu_bind if all of the job's
	 * allocated nodes have the same cpu_bind (or it is not set)
	 */
	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	if (i_first == -1)
		i_last = -2;
	else
		i_last  = bit_fls(job_resrcs_ptr->node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
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
		return;
	}

	/* Use partition's cpu_bind (if any) */
	if (job_ptr->part_ptr && job_ptr->part_ptr->cpu_bind) {
		job_ptr->details->cpu_bind_type = bind_bits |
						  job_ptr->part_ptr->cpu_bind;
		return;
	}

	/* Use global default from TaskPluginParams */
	job_ptr->details->cpu_bind_type = bind_bits |
					  slurm_conf.task_plugin_param;
	return;
}

/*
 * A step may explicity set a TRES count to zero in order to avoid making use
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
 * If a job step specification does not include any TRES specification,
 * then copy those values from the job record
 */
static void _copy_job_tres_to_step(job_step_create_request_msg_t *step_specs,
				   job_record_t *job_ptr)
{
	if (!xstrcasecmp(step_specs->tres_per_node, "NONE")) {
		xfree(step_specs->tres_per_node);
	} else if (step_specs->tres_per_step	||
		   step_specs->tres_per_node	||
		   step_specs->tres_per_socket	||
		   step_specs->tres_per_task) {
		_clear_zero_tres(&step_specs->tres_per_step);
		_clear_zero_tres(&step_specs->tres_per_node);
		_clear_zero_tres(&step_specs->tres_per_socket);
		_clear_zero_tres(&step_specs->tres_per_task);
	} else {
		step_specs->tres_per_step   = xstrdup(job_ptr->tres_per_job);
		step_specs->tres_per_node   = xstrdup(job_ptr->tres_per_node);
		step_specs->tres_per_socket = xstrdup(job_ptr->tres_per_socket);
		step_specs->tres_per_task   = xstrdup(job_ptr->tres_per_task);
	}
}

/*
 * step_create - creates a step_record in step_specs->job_id, sets up the
 *	according to the step_specs.
 * IN step_specs - job step specifications
 * OUT new_step_record - pointer to the new step_record (NULL on error)
 * RET - 0 or error code
 * NOTE: don't free the returned step_record because that is managed through
 *	the job.
 */
extern int step_create(job_step_create_request_msg_t *step_specs,
		       step_record_t** new_step_record,
		       uint16_t protocol_version)
{
	step_record_t *step_ptr;
	job_record_t *job_ptr;
	bitstr_t *nodeset;
	int cpus_per_task, ret_code, i;
	uint32_t node_count = 0;
	time_t now = time(NULL);
	char *step_node_list = NULL;
	uint32_t orig_cpu_count;
	List step_gres_list = (List) NULL;
	dynamic_plugin_data_t *select_jobinfo = NULL;
	uint32_t task_dist;
	uint32_t max_tasks;
	uint32_t jobid;
	slurm_step_layout_t *step_layout = NULL;
	bool tmp_step_layout_used = false;
#ifdef HAVE_NATIVE_CRAY
	slurm_step_layout_t tmp_step_layout;
#endif

	*new_step_record = NULL;
	job_ptr = find_job_record (step_specs->step_id.job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID ;

	/*
	 * NOTE: We have already confirmed the UID originating
	 * the request is identical with step_specs->user_id
	 */
	if (step_specs->user_id != job_ptr->user_id)
		return ESLURM_ACCESS_DENIED ;

	ret_code = _purge_duplicate_steps(job_ptr, step_specs);
	if (ret_code != SLURM_SUCCESS)
		return ret_code;

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

	if (IS_JOB_FINISHED(job_ptr) ||
	    ((job_ptr->end_time <= time(NULL)) && !IS_JOB_CONFIGURING(job_ptr)))
		return ESLURM_ALREADY_DONE;

	task_dist = step_specs->task_dist & SLURM_DIST_STATE_BASE;
	/* Set to block in the case that mem is 0. srun leaves the dist
	 * set to unknown if mem is 0.
	 * ex. SallocDefaultCommand=srun -n1 -N1 --mem=0 ... */
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

	if (!valid_tres_cnt(step_specs->cpus_per_tres)	||
	    !valid_tres_cnt(step_specs->mem_per_tres)	||
	    tres_bind_verify_cmdline(step_specs->tres_bind) ||
	    tres_freq_verify_cmdline(step_specs->tres_freq) ||
	    !valid_tres_cnt(step_specs->tres_per_step)	||
	    (!valid_tres_cnt(step_specs->tres_per_node)	&&
	     xstrcasecmp(step_specs->tres_per_node, "NONE")) ||
	    !valid_tres_cnt(step_specs->tres_per_socket)||
	    !valid_tres_cnt(step_specs->tres_per_task))
		return ESLURM_INVALID_TRES;

	if (_test_strlen(step_specs->host, "host", 1024)		||
	    _test_strlen(step_specs->name, "name", 1024)		||
	    _test_strlen(step_specs->network, "network", 1024))
		return ESLURM_PATHNAME_TOO_LONG;

	if (job_ptr->next_step_id >= slurm_conf.max_step_cnt)
		return ESLURM_STEP_LIMIT;

	/* if the overcommit flag is checked, we 0 set cpu_count=0
	 * which makes it so we don't check to see the available cpus
	 */
	orig_cpu_count =  step_specs->cpu_count;

	if (step_specs->overcommit) {
		if (step_specs->exclusive) {
			/*
			 * Not really a legitimate combination,
			 * try to exclusively allocate one CPU per task
			 */
			step_specs->overcommit = 0;
			step_specs->cpu_count = step_specs->num_tasks;
		} else
			step_specs->cpu_count = 0;
	}

	/* determine cpus_per_task value by reversing what srun does */
	if (step_specs->num_tasks < 1)
		return ESLURM_BAD_TASK_COUNT;

	cpus_per_task = _calc_cpus_per_task(step_specs, job_ptr);

	if (step_specs->no_kill > 1)
		step_specs->no_kill = 1;
	_copy_job_tres_to_step(step_specs, job_ptr);
	i = gres_plugin_step_state_validate(step_specs->cpus_per_tres,
					    step_specs->tres_per_step,
					    step_specs->tres_per_node,
					    step_specs->tres_per_socket,
					    step_specs->tres_per_task,
					    step_specs->mem_per_tres,
					    &step_gres_list,
					    job_ptr->gres_list, job_ptr->job_id,
					    NO_VAL);
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
	if (step_specs->num_tasks == NO_VAL) {
		if (step_specs->cpu_count != NO_VAL)
			step_specs->num_tasks = step_specs->cpu_count;
		else
			step_specs->num_tasks = node_count;
	}

	max_tasks = node_count * slurm_conf.max_tasks_per_node;
	if (step_specs->num_tasks > max_tasks) {
		error("step has invalid task count: %u max is %u",
		      step_specs->num_tasks, max_tasks);
		FREE_NULL_LIST(step_gres_list);
		FREE_NULL_BITMAP(nodeset);
		select_g_select_jobinfo_free(select_jobinfo);
		return ESLURM_BAD_TASK_COUNT;
	}

	step_ptr = _create_step_record(job_ptr, protocol_version);
	if (step_ptr == NULL) {
		FREE_NULL_LIST(step_gres_list);
		FREE_NULL_BITMAP(nodeset);
		select_g_select_jobinfo_free(select_jobinfo);
		return ESLURMD_TOOMANYSTEPS;
	}
	step_ptr->start_time = time(NULL);
	step_ptr->state      = JOB_RUNNING;

	memcpy(&step_ptr->step_id, &step_specs->step_id,
	       sizeof(step_ptr->step_id));

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
		het_job = find_job_record(job_ptr->het_job_id);
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

	step_ptr->gres_list = step_gres_list;
	step_gres_list      = (List) NULL;
	gres_plugin_step_state_log(step_ptr->gres_list, job_ptr->job_id,
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
	step_ptr->pn_min_memory = step_specs->pn_min_memory;
	step_ptr->cpu_count = orig_cpu_count;
	step_ptr->exit_code = NO_VAL;
	step_ptr->exclusive = step_specs->exclusive;
	step_ptr->no_kill   = step_specs->no_kill;
	step_ptr->ext_sensors = ext_sensors_alloc();

	step_ptr->cpus_per_tres = xstrdup(step_specs->cpus_per_tres);
	step_ptr->mem_per_tres = xstrdup(step_specs->mem_per_tres);
	step_ptr->tres_bind = xstrdup(step_specs->tres_bind);
	step_ptr->tres_freq = xstrdup(step_specs->tres_freq);
	step_ptr->tres_per_step = xstrdup(step_specs->tres_per_step);
	step_ptr->tres_per_node = xstrdup(step_specs->tres_per_node);
	step_ptr->tres_per_socket = xstrdup(step_specs->tres_per_socket);
	step_ptr->tres_per_task = xstrdup(step_specs->tres_per_task);

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
	if (step_specs->resv_port_cnt == NO_VAL16 && slurm_conf.mpi_params) {
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
		i = resv_port_alloc(step_ptr);
		if (i != SLURM_SUCCESS) {
			delete_step_record(job_ptr, step_ptr);
			return i;
		}
	}

#ifdef HAVE_NATIVE_CRAY
	if (job_ptr->het_job_id && (job_ptr->het_job_id != NO_VAL)) {
		job_record_t *het_job_ptr;
		step_record_t *het_step_ptr;
		bitstr_t *het_grp_bits = NULL;

		/*
		 * Het job compontents are sent across on the network
		 * variable.
		 */
		if (!step_specs->step_het_grps) {
			het_job_ptr = find_job_record(job_ptr->het_job_id);
		} else {
			int first_bit = 0;
			het_grp_bits = bit_alloc(128);
			if (bit_unfmt_hexmask(het_grp_bits,
					      step_specs->step_het_grps)) {
				error("%s: bad het group given", __func__);
				FREE_NULL_BITMAP(het_grp_bits);
				return ESLURM_INTERCONNECT_FAILURE;
			}
			if ((first_bit = bit_ffs(het_grp_bits)) == -1) {
				error("%s: no components given from srun for hetstep %pS",
				      __func__, step_ptr);
				return ESLURM_INTERCONNECT_FAILURE;
			}
			/* The het step might not start on the 0 component. */
			het_job_ptr = find_het_job_record(
				job_ptr->het_job_id, first_bit);
		}

		/* Get the step record from the first component in the step */
		het_step_ptr = find_step_record(het_job_ptr,
						&step_ptr->step_id);

		jobid = job_ptr->het_job_id;
		if (!het_step_ptr || !het_step_ptr->switch_job) {
			job_record_t *het_job_comp_ptr;
			hostlist_t hl = hostlist_create(NULL);
			ListIterator itr;

			/* Now let's get the real het_job_ptr */
			het_job_ptr = find_job_record(job_ptr->het_job_id);
			itr = list_iterator_create(het_job_ptr->het_job_list);
			while ((het_job_comp_ptr = list_next(itr))) {
				if (het_grp_bits &&
				    !bit_test(het_grp_bits,
					      het_job_comp_ptr->het_job_offset))
					continue;
				hostlist_push(hl, het_job_comp_ptr->nodes);
			}
			list_iterator_destroy(itr);
			FREE_NULL_BITMAP(het_grp_bits);

			hostlist_uniq(hl);

			memset(&tmp_step_layout, 0, sizeof(tmp_step_layout));
			step_layout = &tmp_step_layout;
			step_layout->node_list =
				hostlist_ranged_string_xmalloc(hl);
			step_layout->node_cnt = hostlist_count(hl);
			hostlist_destroy(hl);
			tmp_step_layout_used = true;
		} else {

			if (!het_step_ptr->switch_job)
				return ESLURM_INTERCONNECT_FAILURE;

			switch_g_duplicate_jobinfo(het_step_ptr->switch_job,
						   &step_ptr->switch_job);
			/*
			 * Prevent switch_g_build_jobinfo from getting a new
			 * cookie below.
			 */
			step_layout = NULL;
		}
       } else if (step_specs->step_id.step_het_comp != NO_VAL) {
               slurm_step_id_t step_id = {
                       .job_id = step_ptr->step_id.job_id,
                       .step_id = step_ptr->step_id.step_id,
                       .step_het_comp = 0,
               };
               /* get the first het step component */
               step_record_t *het_step_ptr =
		       find_step_record(job_ptr, &step_id);

               jobid = job_ptr->job_id;
               if (!het_step_ptr || !het_step_ptr->switch_job) {
                       memset(&tmp_step_layout, 0, sizeof(tmp_step_layout));
                       step_layout = &tmp_step_layout;
                       step_layout->node_list = xstrdup(job_ptr->nodes);
                       step_layout->node_cnt = job_ptr->node_cnt;
                       tmp_step_layout_used = true;
               } else {
                       if (!het_step_ptr->switch_job)
                               return ESLURM_INTERCONNECT_FAILURE;

                       switch_g_duplicate_jobinfo(het_step_ptr->switch_job,
                                                  &step_ptr->switch_job);
                       /*
                        * Prevent switch_g_build_jobinfo from getting a new
                        * cookie below.
                        */
                       step_layout = NULL;
               }
	} else {
		step_layout = step_ptr->step_layout;
		jobid = job_ptr->job_id;
	}
#else
	step_layout = step_ptr->step_layout;
	jobid = job_ptr->job_id;
#endif

	if (step_layout) {
		if (switch_g_alloc_jobinfo(&step_ptr->switch_job,
					   jobid,
					   step_ptr->step_id.step_id) < 0)
			fatal("%s: switch_g_alloc_jobinfo error", __func__);

		if (switch_g_build_jobinfo(step_ptr->switch_job,
					   step_layout,
					   step_ptr->network) < 0) {
			delete_step_record(job_ptr, step_ptr);
			if (tmp_step_layout_used)
				xfree(step_layout->node_list);
			if (errno == ESLURM_INTERCONNECT_BUSY)
				return errno;
			return ESLURM_INTERCONNECT_FAILURE;
		}
	}

	if (tmp_step_layout_used)
		xfree(step_layout->node_list);

	step_alloc_lps(step_ptr);

	*new_step_record = step_ptr;

	if (!with_slurmdbd && !job_ptr->db_index)
		jobacct_storage_g_job_start(acct_db_conn, job_ptr);

	select_g_step_start(step_ptr);


	step_set_alloc_tres(step_ptr, node_count, false, true);
	jobacct_storage_g_step_start(acct_db_conn, step_ptr);
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
	int i, usable_cpus, usable_mem, rem_nodes;
	int set_nodes = 0/* , set_tasks = 0 */;
	int pos = -1, job_node_offset = -1;
	int first_bit, last_bit;
	uint32_t cpu_count_reps[node_count];
	uint32_t cpus_task_reps[node_count];
	uint32_t cpus_task = 0;
	uint16_t ntasks_per_core = 0;
	uint16_t ntasks_per_socket = 0;
	bool first_step_node = true;

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

	if (job_ptr->details && job_ptr->details->mc_ptr) {
		multi_core_data_t *mc_ptr = job_ptr->details->mc_ptr;
		if (mc_ptr->ntasks_per_core &&
		    (mc_ptr->ntasks_per_core != INFINITE16)) {
			ntasks_per_core = mc_ptr->ntasks_per_core;
		}
	}

	/* build cpus-per-node arrays for the subset of nodes used by step */
	rem_nodes = bit_set_count(step_ptr->step_node_bitmap);
	first_bit = bit_ffs(job_ptr->node_bitmap);
	if (first_bit >= 0)
		last_bit = bit_fls(job_ptr->node_bitmap);
	else
		last_bit = -2;
	for (i = first_bit; i <= last_bit; i++) {
		uint16_t cpus, cpus_used;

		if (!bit_test(job_ptr->node_bitmap, i))
			continue;
		job_node_offset++;
		if (bit_test(step_ptr->step_node_bitmap, i)) {
			node_record_t *node_ptr =
				node_record_table_ptr + i;

#ifndef HAVE_FRONT_END
			if (step_ptr->start_protocol_ver >
			    node_ptr->protocol_version)
				step_ptr->start_protocol_ver =
					node_ptr->protocol_version;
#endif

			/* find out the position in the job */
			pos = bit_get_pos_num(job_resrcs_ptr->node_bitmap, i);
			if (pos == -1)
				return NULL;
			if (pos >= job_resrcs_ptr->nhosts)
				fatal("%s: node index bad", __func__);

			cpus = job_resrcs_ptr->cpus[pos];
			cpus_used = job_resrcs_ptr->cpus_used[pos];
			/*
			 * Here we are trying to figure out the number
			 * of cpus available if we only want to run 1
			 * thread per core.
			 */
			if (_use_one_thread_per_core(job_ptr)) {
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
				    (cpus_per_task_array[cpus_task_inx] !=
				     cpus_task)) {
					cpus_task_inx++;
					cpus_per_task_array[cpus_task_inx] =
						cpus_task;
					cpus_task_reps[cpus_task_inx] = 1;
				} else
					cpus_task_reps[cpus_task_inx]++;
			}

			if (step_ptr->exclusive) {
				usable_cpus = cpus - cpus_used;
			} else
				usable_cpus = cpus;
			if ((step_ptr->pn_min_memory & MEM_PER_CPU) &&
			    _is_mem_resv()) {
				uint64_t mem_use = step_ptr->pn_min_memory;
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
							first_step_node,
							step_ptr->cpus_per_task,
							rem_nodes, false,
							job_ptr->job_id,
							step_ptr->step_id.step_id);
			if (usable_cpus > gres_cpus)
				usable_cpus = gres_cpus;
			if (usable_cpus <= 0) {
				error("%s: no usable CPUs", __func__);
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
			first_step_node = false;
			rem_nodes--;

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
	step_layout_req.cpus_per_task = cpus_per_task_array;
	step_layout_req.task_dist = task_dist;
	step_layout_req.plane_size = plane_size;

	if ((step_layout = slurm_step_layout_create(&step_layout_req))) {
		step_layout->start_protocol_ver = step_ptr->start_protocol_ver;
	}

	return step_layout;
}

/* Pack the data for a specific job step record
 * IN step - pointer to a job step record
 * IN/OUT buffer - location to store data, pointers automatically advanced
 * IN protocol_version - slurm protocol version of client
 */
static void _pack_ctld_job_step_info(step_record_t *step_ptr, Buf buffer,
				     uint16_t protocol_version)
{
	uint32_t task_cnt, cpu_cnt;
	char *node_list = NULL;
	time_t begin_time, run_time;
	bitstr_t *pack_bitstr;

#if defined HAVE_FRONT_END
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

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		pack32(step_ptr->job_ptr->array_job_id, buffer);
		pack32(step_ptr->job_ptr->array_task_id, buffer);

		pack_step_id(&step_ptr->step_id, buffer, protocol_version);

		pack32(step_ptr->job_ptr->user_id, buffer);
		pack32(cpu_cnt, buffer);
		pack32(step_ptr->cpu_freq_min, buffer);
		pack32(step_ptr->cpu_freq_max, buffer);
		pack32(step_ptr->cpu_freq_gov, buffer);
		pack32(task_cnt, buffer);
		if (step_ptr->step_layout)
			pack32(step_ptr->step_layout->task_dist, buffer);
		else
			pack32((uint32_t) SLURM_DIST_UNKNOWN, buffer);
		pack32(step_ptr->time_limit, buffer);
		pack32(step_ptr->state, buffer);
		pack32(step_ptr->srun_pid, buffer);

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

		packstr(slurm_conf.cluster_name, buffer);
		if (step_ptr->job_ptr->part_ptr)
			packstr(step_ptr->job_ptr->part_ptr->name, buffer);
		else
			packstr(step_ptr->job_ptr->partition, buffer);
		packstr(step_ptr->host, buffer);
		packstr(step_ptr->resv_ports, buffer);
		packstr(node_list, buffer);
		packstr(step_ptr->name, buffer);
		packstr(step_ptr->network, buffer);
		pack_bit_str_hex(pack_bitstr, buffer);
		select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
					     protocol_version);
		packstr(step_ptr->tres_fmt_alloc_str, buffer);
		pack16(step_ptr->start_protocol_ver, buffer);

		packstr(step_ptr->cpus_per_tres, buffer);
		packstr(step_ptr->mem_per_tres, buffer);
		packstr(step_ptr->tres_bind, buffer);
		packstr(step_ptr->tres_freq, buffer);
		packstr(step_ptr->tres_per_step, buffer);
		packstr(step_ptr->tres_per_node, buffer);
		packstr(step_ptr->tres_per_socket, buffer);
		packstr(step_ptr->tres_per_task, buffer);
	} else if (protocol_version >= SLURM_20_02_PROTOCOL_VERSION) {
		pack32(step_ptr->job_ptr->array_job_id, buffer);
		pack32(step_ptr->job_ptr->array_task_id, buffer);
		pack_step_id(&step_ptr->step_id, buffer, protocol_version);
		pack32(step_ptr->job_ptr->user_id, buffer);
		pack32(cpu_cnt, buffer);
		pack32(step_ptr->cpu_freq_min, buffer);
		pack32(step_ptr->cpu_freq_max, buffer);
		pack32(step_ptr->cpu_freq_gov, buffer);
		pack32(task_cnt, buffer);
		if (step_ptr->step_layout)
			pack32(step_ptr->step_layout->task_dist, buffer);
		else
			pack32((uint32_t) SLURM_DIST_UNKNOWN, buffer);
		pack32(step_ptr->time_limit, buffer);
		pack32(step_ptr->state, buffer);
		pack32(step_ptr->srun_pid, buffer);

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

		packstr(slurm_conf.cluster_name, buffer);
		if (step_ptr->job_ptr->part_ptr)
			packstr(step_ptr->job_ptr->part_ptr->name, buffer);
		else
			packstr(step_ptr->job_ptr->partition, buffer);
		packstr(step_ptr->host, buffer);
		packstr(step_ptr->resv_ports, buffer);
		packstr(node_list, buffer);
		packstr(step_ptr->name, buffer);
		packstr(step_ptr->network, buffer);
		pack_bit_str_hex(pack_bitstr, buffer);
		select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
					     protocol_version);
		packstr(step_ptr->tres_fmt_alloc_str, buffer);
		pack16(step_ptr->start_protocol_ver, buffer);

		packstr(step_ptr->cpus_per_tres, buffer);
		packstr(step_ptr->mem_per_tres, buffer);
		packstr(step_ptr->tres_bind, buffer);
		packstr(step_ptr->tres_freq, buffer);
		packstr(step_ptr->tres_per_step, buffer);
		packstr(step_ptr->tres_per_node, buffer);
		packstr(step_ptr->tres_per_socket, buffer);
		packstr(step_ptr->tres_per_task, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(step_ptr->job_ptr->array_job_id, buffer);
		pack32(step_ptr->job_ptr->array_task_id, buffer);
		pack_step_id(&step_ptr->step_id, buffer, protocol_version);
		pack16(0, buffer); /* was ckpt_interval */
		pack32(step_ptr->job_ptr->user_id, buffer);
		pack32(cpu_cnt, buffer);
		pack32(step_ptr->cpu_freq_min, buffer);
		pack32(step_ptr->cpu_freq_max, buffer);
		pack32(step_ptr->cpu_freq_gov, buffer);
		pack32(task_cnt, buffer);
		if (step_ptr->step_layout)
			pack32(step_ptr->step_layout->task_dist, buffer);
		else
			pack32((uint32_t) SLURM_DIST_UNKNOWN, buffer);
		pack32(step_ptr->time_limit, buffer);
		pack32(step_ptr->state, buffer);
		pack32(step_ptr->srun_pid, buffer);

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

		packstr(slurm_conf.cluster_name, buffer);
		if (step_ptr->job_ptr->part_ptr)
			packstr(step_ptr->job_ptr->part_ptr->name, buffer);
		else
			packstr(step_ptr->job_ptr->partition, buffer);
		packstr(step_ptr->host, buffer);
		packstr(step_ptr->resv_ports, buffer);
		packstr(node_list, buffer);
		packstr(step_ptr->name, buffer);
		packstr(step_ptr->network, buffer);
		pack_bit_str_hex(pack_bitstr, buffer);
		packnull(buffer); /* was ckpt_dir */
		select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
					     protocol_version);
		packstr(step_ptr->tres_fmt_alloc_str, buffer);
		pack16(step_ptr->start_protocol_ver, buffer);

		packstr(step_ptr->cpus_per_tres, buffer);
		packstr(step_ptr->mem_per_tres, buffer);
		packstr(step_ptr->tres_bind, buffer);
		packstr(step_ptr->tres_freq, buffer);
		packstr(step_ptr->tres_per_step, buffer);
		packstr(step_ptr->tres_per_node, buffer);
		packstr(step_ptr->tres_per_socket, buffer);
		packstr(step_ptr->tres_per_task, buffer);
	} else {
		error("%s: protocol_version %hu not supported", __func__,
		      protocol_version);
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
	step_record_t *step_ptr;
	job_record_t *job_ptr;
	time_t now = time(NULL);
	int valid_job = 0;

	pack_time(now, buffer);
	pack32(steps_packed, buffer);	/* steps_packed placeholder */

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if ((job_id != NO_VAL) && (job_id != job_ptr->job_id) &&
		    (job_id != job_ptr->array_job_id))
			continue;

		valid_job = 1;

		if (((show_flags & SHOW_ALL) == 0) && (uid != 0) &&
		    (job_ptr->part_ptr) && !part_is_visible(job_ptr->part_ptr, uid))
			continue;

		if ((slurm_conf.private_data & PRIVATE_DATA_JOBS) &&
		    (job_ptr->user_id != uid) && !validate_operator(uid) &&
		    (((slurm_mcs_get_privatedata() == 0) &&
		      !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						    job_ptr->account)) ||
		     ((slurm_mcs_get_privatedata() == 1) &&
		      (mcs_g_check_mcs_label(uid, job_ptr->mcs_label) != 0))))
			continue;

		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = list_next(step_iterator))) {
			if ((step_id != NO_VAL) &&
			    (step_ptr->step_id.step_id != step_id))
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
extern int kill_step_on_node(job_record_t *job_ptr, node_record_t *node_ptr,
			     bool node_fail)
{
#ifdef HAVE_FRONT_END
	static bool front_end = true;
#else
	static bool front_end = false;
#endif
	ListIterator step_iterator;
	step_record_t *step_ptr;
	int i, i_first, i_last;
	uint32_t step_rc = 0;
	int bit_position, found = 0, rem = 0, step_node_inx;
	step_complete_msg_t req;

	if ((job_ptr == NULL) || (node_ptr == NULL))
		return found;

	bit_position = node_ptr - node_record_table_ptr;
	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		if (!bit_test(step_ptr->step_node_bitmap, bit_position))
			continue;

		/* Remove step allocation from the job's allocation */
		i_first = bit_ffs(step_ptr->step_node_bitmap);
		i_last = bit_fls(step_ptr->step_node_bitmap);
		for (i = i_first, step_node_inx = 0; i <= i_last; i++) {
			if (i == bit_position)
				break;
			if (bit_test(step_ptr->step_node_bitmap, i))
				step_node_inx++;
		}
		memset(&req, 0, sizeof(step_complete_msg_t));

		memcpy(&req.step_id, &step_ptr->step_id, sizeof(req.step_id));

		req.range_first = step_node_inx;
		req.range_last = step_node_inx;
		req.step_rc = 9;
		req.jobacct = NULL;	/* No accounting */
		(void) step_partial_comp(&req, 0, false, &rem, &step_rc);

		if (node_fail && !step_ptr->no_kill) {
			info("Killing %pS due to failed node %s", step_ptr,
			     node_ptr->name);

			/*
			 * Never signal tasks on a front_end system.
			 * Otherwise signal step on all nodes
			 */
			if (!front_end) {
				signal_step_tasks(step_ptr, SIGKILL,
						  REQUEST_TERMINATE_TASKS);
			}
		} else {
			info("Killing %pS on failed node %s", step_ptr,
			     node_ptr->name);
			signal_step_tasks_on_node(node_ptr->name, step_ptr,
						  SIGKILL,
						  REQUEST_TERMINATE_TASKS);
		}
		found++;
	}

	list_iterator_destroy (step_iterator);
	return found;
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
	int nodes, rem_nodes;

	xassert(rem);

	/* find the job, step, and validate input */
	job_ptr = find_job_record(req->step_id.job_id);
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
		info("step_partial_comp: %pJ StepID=%u invalid",
		     job_ptr, req->step_id.step_id);
		return ESLURM_INVALID_JOB_ID;
	}
	if (step_ptr->batch_step) {
		error("%s: batch step received for %pJ. This should never happen.",
		      __func__, job_ptr);
		return ESLURM_INVALID_JOB_ID;
	}
	if (req->range_last < req->range_first) {
		error("%s: %pS range=%u-%u",
		      __func__, step_ptr, req->range_first, req->range_last);
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

#ifdef HAVE_FRONT_END
	bit_set_all(step_ptr->exit_node_bitmap);
	rem_nodes = 0;
#else
	bit_nset(step_ptr->exit_node_bitmap,
		 req->range_first, req->range_last);
	rem_nodes = bit_clear_count(step_ptr->exit_node_bitmap);
#endif
	*rem = rem_nodes;
	if (rem_nodes == 0) {
		/* release all switch windows */
		if (step_ptr->switch_job) {
			debug2("full switch release for %pS, nodes %s",
			       step_ptr, step_ptr->step_layout->node_list);
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
		debug2("partial switch release for %pS, nodes %s",
			step_ptr, node_list);
		switch_g_job_step_part_comp(
			step_ptr->switch_job, node_list);
		hostlist_destroy(hl);
		xfree(node_list);
	}

	if (max_rc)
		*max_rc = step_ptr->exit_code;

	/* The step has finished, finish it completely */
	if (!*rem && finish) {
		uint16_t cleaning = 0;
		if (step_ptr->step_id.step_id == SLURM_PENDING_STEP)
			return SLURM_SUCCESS;

		/* If the job is already cleaning we have already been here
		 * before, so just return. */
		select_g_select_jobinfo_get(step_ptr->select_jobinfo,
					    SELECT_JOBDATA_CLEANING,
					    &cleaning);
		if (cleaning) {	/* Step hasn't finished cleanup yet. */
			debug("%s: Cleaning flag already set for %pS, no reason to cleanup again.",
			      __func__, step_ptr);
			return SLURM_SUCCESS;
		}

		_internal_step_complete(job_ptr, step_ptr);

		last_job_update = time(NULL);
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
	uint64_t cpu_count = 1, mem_count = 1;
	char *tmp_tres_str = NULL;

	xassert(step_ptr);
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	xfree(step_ptr->tres_alloc_str);
	xfree(step_ptr->tres_fmt_alloc_str);

	if ((step_ptr->step_id.step_id == SLURM_BATCH_SCRIPT) &&
	    step_ptr->job_ptr->job_resrcs) {
		/* get the cpus and memory on the first node */
		if (step_ptr->job_ptr->job_resrcs->cpus)
			cpu_count = step_ptr->job_ptr->job_resrcs->cpus[0];
		if (step_ptr->job_ptr->job_resrcs->memory_allocated)
			mem_count = step_ptr->job_ptr->job_resrcs->
				memory_allocated[0];
	} else if ((step_ptr->step_id.step_id == SLURM_EXTERN_CONT) &&
		   step_ptr->job_ptr->tres_alloc_str) {
		/* get the tres from the whole job */
		step_ptr->tres_alloc_str =
			xstrdup(step_ptr->job_ptr->tres_alloc_str);
		if (make_formatted)
			step_ptr->tres_fmt_alloc_str =
				xstrdup(step_ptr->job_ptr->tres_fmt_alloc_str);
		return;
	} else {
		if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt)
			cpu_count = (uint64_t)step_ptr->job_ptr->total_cpus;
		else
			cpu_count = (uint64_t)step_ptr->cpu_count;
		mem_count = (uint64_t)step_ptr->pn_min_memory;
		if (mem_count & MEM_PER_CPU) {
			mem_count &= (~MEM_PER_CPU);
			mem_count *= cpu_count;
		} else
			mem_count *= node_count;
	}

	xstrfmtcat(step_ptr->tres_alloc_str,
		   "%s%u=%"PRIu64",%u=%"PRIu64",%u=%u",
		   step_ptr->tres_alloc_str ? "," : "",
		   TRES_CPU, cpu_count,
		   TRES_MEM, mem_count,
		   TRES_NODE, node_count);

	if ((tmp_tres_str = gres_2_tres_str(step_ptr->gres_list, 0, true))) {
		xstrfmtcat(step_ptr->tres_alloc_str, "%s%s",
			   step_ptr->tres_alloc_str ? "," : "",
			   tmp_tres_str);
		xfree(tmp_tres_str);
	}

	if (make_formatted) {
		assoc_mgr_lock_t locks = { .tres = READ_LOCK };
		if (!assoc_mgr_locked)
			assoc_mgr_lock(&locks);

		step_ptr->tres_fmt_alloc_str =
			slurmdb_make_tres_string_from_simple(
				step_ptr->tres_alloc_str, assoc_mgr_tres_list,
				NO_VAL, CONVERT_NUM_UNIT_EXACT, 0, NULL);

		if (!assoc_mgr_locked)
			assoc_mgr_unlock(&locks);
	}

	return;
}

/* convert a range of nodes allocated to a step to a hostlist with
 * names of those nodes */
static hostlist_t _step_range_to_hostlist(step_record_t *step_ptr,
					  uint32_t range_first,
					  uint32_t range_last)
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
static int _step_hostname_to_inx(step_record_t *step_ptr, char *node_name)
{
	node_record_t *node_ptr;
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

extern int step_epilog_complete(job_record_t *job_ptr, char *node_name)
{
	int rc = 0, node_inx, step_offset;
	ListIterator step_iterator;
	step_record_t *step_ptr;
	node_record_t *node_ptr;

	if (!switch_g_part_comp()) {
		/* don't bother with partial completions */
		return 0;
	}
	if ((node_ptr = find_node_record(node_name)) == NULL)
		return 0;
	node_inx = node_ptr - node_record_table_ptr;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
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
		debug2("partial switch release for %pS, epilog on %s",
			step_ptr, node_name);
		switch_g_job_step_part_comp(
			step_ptr->switch_job, node_name);
	}
	list_iterator_destroy (step_iterator);

	return rc;
}

static void _suspend_job_step(job_record_t *job_ptr, step_record_t *step_ptr,
			      time_t now)
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
extern void suspend_job_step(job_record_t *job_ptr)
{
	time_t now = time(NULL);
	ListIterator step_iterator;
	step_record_t *step_ptr;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		_suspend_job_step(job_ptr, step_ptr, now);
	}
	list_iterator_destroy (step_iterator);
}

static void _resume_job_step(job_record_t *job_ptr, step_record_t *step_ptr,
			     time_t now)
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
extern void resume_job_step(job_record_t *job_ptr)
{
	time_t now = time(NULL);
	ListIterator step_iterator;
	step_record_t *step_ptr;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		_resume_job_step(job_ptr, step_ptr, now);
	}
	list_iterator_destroy (step_iterator);
}


/*
 * dump_job_step_state - dump the state of a specific job step to a buffer,
 *	load with load_step_state
 * IN step_ptr - pointer to job step for which information is to be dumped
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern int dump_job_step_state(void *x, void *arg)
{
	step_record_t *step_ptr = (step_record_t *) x;
	Buf buffer = (Buf) arg;

	if (step_ptr->state < JOB_RUNNING)
		return 0;

	pack16((uint16_t) STEP_FLAG, buffer);

	pack32(step_ptr->step_id.step_id, buffer);
	pack32(step_ptr->step_id.step_het_comp, buffer);
	pack16(step_ptr->cyclic_alloc, buffer);
	pack32(step_ptr->srun_pid, buffer);
	pack16(step_ptr->port, buffer);
	pack16(step_ptr->cpus_per_task, buffer);
	pack16(step_ptr->resv_port_cnt, buffer);
	pack16(step_ptr->state, buffer);
	pack16(step_ptr->start_protocol_ver, buffer);

	pack8(step_ptr->no_kill, buffer);

	pack32(step_ptr->cpu_count, buffer);
	pack64(step_ptr->pn_min_memory, buffer);
	pack32(step_ptr->exit_code, buffer);
	if (step_ptr->exit_code != NO_VAL) {
		pack_bit_str_hex(step_ptr->exit_node_bitmap, buffer);
	}
	pack_bit_str_hex(step_ptr->core_bitmap_job, buffer);
	pack32(step_ptr->time_limit, buffer);
	pack32(step_ptr->cpu_freq_min, buffer);
	pack32(step_ptr->cpu_freq_max, buffer);
	pack32(step_ptr->cpu_freq_gov, buffer);

	pack_time(step_ptr->start_time, buffer);
	pack_time(step_ptr->pre_sus_time, buffer);
	pack_time(step_ptr->tot_sus_time, buffer);

	packstr(step_ptr->host,  buffer);
	packstr(step_ptr->resv_ports, buffer);
	packstr(step_ptr->name, buffer);
	packstr(step_ptr->network, buffer);

	(void) gres_plugin_step_state_pack(step_ptr->gres_list, buffer,
					   &step_ptr->step_id,
					   SLURM_PROTOCOL_VERSION);

	pack16(step_ptr->batch_step, buffer);

	pack_slurm_step_layout(step_ptr->step_layout, buffer,
			       SLURM_PROTOCOL_VERSION);

	if (step_ptr->switch_job) {
		pack8(1, buffer);
		switch_g_pack_jobinfo(step_ptr->switch_job, buffer,
				      SLURM_PROTOCOL_VERSION);
	} else
		pack8(0, buffer);

	select_g_select_jobinfo_pack(step_ptr->select_jobinfo, buffer,
				     SLURM_PROTOCOL_VERSION);
	packstr(step_ptr->tres_alloc_str, buffer);
	packstr(step_ptr->tres_fmt_alloc_str, buffer);

	packstr(step_ptr->cpus_per_tres, buffer);
	packstr(step_ptr->mem_per_tres, buffer);
	packstr(step_ptr->tres_bind, buffer);
	packstr(step_ptr->tres_freq, buffer);
	packstr(step_ptr->tres_per_step, buffer);
	packstr(step_ptr->tres_per_node, buffer);
	packstr(step_ptr->tres_per_socket, buffer);
	packstr(step_ptr->tres_per_task, buffer);
	return 0;
}

/*
 * Create a new job step from data in a buffer (as created by
 *	dump_job_step_state)
 * IN/OUT - job_ptr - point to a job for which the step is to be loaded.
 * IN/OUT buffer - location to get data from, pointers advanced
 */
/* NOTE: assoc_mgr tres and assoc read lock must be locked before calling */
extern int load_step_state(job_record_t *job_ptr, Buf buffer,
			   uint16_t protocol_version)
{
	step_record_t *step_ptr = NULL;
	bitstr_t *exit_node_bitmap = NULL, *core_bitmap_job = NULL;
	uint8_t no_kill, uint8_tmp;
	uint16_t cyclic_alloc, port, batch_step;
	uint16_t start_protocol_ver = SLURM_MIN_PROTOCOL_VERSION;
	uint16_t cpus_per_task, resv_port_cnt, state;
	uint32_t cpu_count, exit_code, name_len, srun_pid = 0;
	uint32_t time_limit, cpu_freq_min, cpu_freq_max, cpu_freq_gov;
	uint64_t pn_min_memory;
	time_t start_time, pre_sus_time, tot_sus_time;
	char *host = NULL, *core_job = NULL;
	char *resv_ports = NULL, *name = NULL, *network = NULL;
	char *tres_alloc_str = NULL, *tres_fmt_alloc_str = NULL;
	char *cpus_per_tres = NULL, *mem_per_tres = NULL, *tres_bind = NULL;
	char *tres_freq = NULL, *tres_per_step = NULL, *tres_per_node = NULL;
	char *tres_per_socket = NULL, *tres_per_task = NULL;
	dynamic_plugin_data_t *switch_tmp = NULL;
	slurm_step_layout_t *step_layout = NULL;
	List gres_list = NULL;
	dynamic_plugin_data_t *select_jobinfo = NULL;
	slurm_step_id_t step_id = { .job_id = job_ptr->job_id,
				    .step_het_comp = NO_VAL };

	if (protocol_version >= SLURM_20_11_PROTOCOL_VERSION) {
		safe_unpack32(&step_id.step_id, buffer);
		safe_unpack32(&step_id.step_het_comp, buffer);
		safe_unpack16(&cyclic_alloc, buffer);
		safe_unpack32(&srun_pid, buffer);
		safe_unpack16(&port, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpack16(&state, buffer);
		safe_unpack16(&start_protocol_ver, buffer);

		safe_unpack8(&no_kill, buffer);

		safe_unpack32(&cpu_count, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&exit_code, buffer);
		if (exit_code != NO_VAL) {
			unpack_bit_str_hex(&exit_node_bitmap, buffer);
		}
		unpack_bit_str_hex(&core_bitmap_job, buffer);

		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);

		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);

		safe_unpackstr_xmalloc(&host, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_ports, &name_len, buffer);
		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);

		if (gres_plugin_step_state_unpack(&gres_list, buffer,
						  &step_id,
						  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&batch_step, buffer);

		if (unpack_slurm_step_layout(&step_layout, buffer,
					     protocol_version))
			goto unpack_error;

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp &&
		    (switch_g_unpack_jobinfo(&switch_tmp, buffer,
					     protocol_version)))
				goto unpack_error;

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		safe_unpackstr_xmalloc(&tres_alloc_str, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_alloc_str, &name_len, buffer);

		safe_unpackstr_xmalloc(&cpus_per_tres, &name_len, buffer);
		safe_unpackstr_xmalloc(&mem_per_tres, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_bind, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_freq, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_step, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_socket, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_task, &name_len, buffer);
	} else if (protocol_version >= SLURM_20_02_PROTOCOL_VERSION) {
		safe_unpack32(&step_id.step_id, buffer);
		convert_old_step_id(&step_id.step_id);
		safe_unpack16(&cyclic_alloc, buffer);
		safe_unpack32(&srun_pid, buffer);
		safe_unpack16(&port, buffer);
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpack16(&state, buffer);
		safe_unpack16(&start_protocol_ver, buffer);

		safe_unpack8(&no_kill, buffer);

		safe_unpack32(&cpu_count, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&exit_code, buffer);
		if (exit_code != NO_VAL) {
			unpack_bit_str_hex(&exit_node_bitmap, buffer);
		}
		unpack_bit_str_hex(&core_bitmap_job, buffer);

		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);

		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);

		safe_unpackstr_xmalloc(&host, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_ports, &name_len, buffer);
		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);

		if (gres_plugin_step_state_unpack(&gres_list, buffer,
						  &step_id,
						  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&batch_step, buffer);

		if (unpack_slurm_step_layout(&step_layout, buffer,
					     protocol_version))
			goto unpack_error;

		safe_unpack8(&uint8_tmp, buffer);
		if (uint8_tmp &&
		    (switch_g_unpack_jobinfo(&switch_tmp, buffer,
					     protocol_version)))
				goto unpack_error;

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		safe_unpackstr_xmalloc(&tres_alloc_str, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_alloc_str, &name_len, buffer);

		safe_unpackstr_xmalloc(&cpus_per_tres, &name_len, buffer);
		safe_unpackstr_xmalloc(&mem_per_tres, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_bind, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_freq, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_step, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_socket, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_task, &name_len, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		char *temp_str;
		time_t time_tmp;

		uint16_t uint16_tmp;
		safe_unpack32(&step_id.step_id, buffer);
		convert_old_step_id(&step_id.step_id);
		safe_unpack16(&cyclic_alloc, buffer);
		safe_unpack32(&srun_pid, buffer);
		safe_unpack16(&port, buffer);
		safe_unpack16(&uint16_tmp, buffer); /* was ckpt_interval */
		safe_unpack16(&cpus_per_task, buffer);
		safe_unpack16(&resv_port_cnt, buffer);
		safe_unpack16(&state, buffer);
		safe_unpack16(&start_protocol_ver, buffer);

		safe_unpack8(&no_kill, buffer);

		safe_unpack32(&cpu_count, buffer);
		safe_unpack64(&pn_min_memory, buffer);
		safe_unpack32(&exit_code, buffer);
		if (exit_code != NO_VAL) {
			unpack_bit_str_hex(&exit_node_bitmap, buffer);
		}
		unpack_bit_str_hex(&core_bitmap_job, buffer);

		safe_unpack32(&time_limit, buffer);
		safe_unpack32(&cpu_freq_min, buffer);
		safe_unpack32(&cpu_freq_max, buffer);
		safe_unpack32(&cpu_freq_gov, buffer);

		safe_unpack_time(&start_time, buffer);
		safe_unpack_time(&pre_sus_time, buffer);
		safe_unpack_time(&tot_sus_time, buffer);
		safe_unpack_time(&time_tmp, buffer); /* was ckpt_time */

		safe_unpackstr_xmalloc(&host, &name_len, buffer);
		safe_unpackstr_xmalloc(&resv_ports, &name_len, buffer);
		safe_unpackstr_xmalloc(&name, &name_len, buffer);
		safe_unpackstr_xmalloc(&network, &name_len, buffer);
		safe_unpackstr_xmalloc(&temp_str, &name_len, buffer);
		xfree(temp_str); /* was ckpt_dir */

		if (gres_plugin_step_state_unpack(&gres_list, buffer,
						  &step_id,
						  protocol_version)
		    != SLURM_SUCCESS)
			goto unpack_error;

		safe_unpack16(&batch_step, buffer);
		if (!batch_step) {
			if (unpack_slurm_step_layout(&step_layout, buffer,
						     protocol_version))
				goto unpack_error;
			if (switch_g_unpack_jobinfo(&switch_tmp, buffer,
						    protocol_version))
				goto unpack_error;
		} else
			step_layout = fake_slurm_step_layout_create(
				job_ptr->batch_host, NULL, NULL, 1, 1);

		/* fake out the former checkpoint plugin */
		{
			uint16_t id;
			uint32_t size;
			safe_unpack16(&id, buffer);
			safe_unpack32(&size, buffer);
			/* skip past any checkpoint plugin info */
			size += get_buf_offset(buffer);
			set_buf_offset(buffer, size);
		}

		if (select_g_select_jobinfo_unpack(&select_jobinfo, buffer,
						   protocol_version))
			goto unpack_error;
		safe_unpackstr_xmalloc(&tres_alloc_str, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_fmt_alloc_str, &name_len, buffer);

		safe_unpackstr_xmalloc(&cpus_per_tres, &name_len, buffer);
		safe_unpackstr_xmalloc(&mem_per_tres, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_bind, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_freq, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_step, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_node, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_socket, &name_len, buffer);
		safe_unpackstr_xmalloc(&tres_per_task, &name_len, buffer);
	} else {
		error("load_step_state: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}

	/* validity test as possible */
	if (cyclic_alloc > 1) {
		error("Invalid data for %pJ StepId=%u: cyclic_alloc=%u",
		      job_ptr, step_id.step_id, cyclic_alloc);
		goto unpack_error;
	}
	if (no_kill > 1) {
		error("Invalid data for %pJ StepId=%u: no_kill=%u",
		      job_ptr, step_id.step_id, no_kill);
		goto unpack_error;
	}

	step_ptr = find_step_record(job_ptr, &step_id);
	if (step_ptr == NULL)
		step_ptr = _create_step_record(job_ptr, start_protocol_ver);
	if (step_ptr == NULL)
		goto unpack_error;

	/* set new values */
	memcpy(&step_ptr->step_id, &step_id, sizeof(step_ptr->step_id));

	step_ptr->cpu_count    = cpu_count;
	step_ptr->cpus_per_task= cpus_per_task;
	step_ptr->cyclic_alloc = cyclic_alloc;
	step_ptr->resv_port_cnt= resv_port_cnt;
	step_ptr->resv_ports   = resv_ports;
	step_ptr->name         = name;
	step_ptr->network      = network;
	step_ptr->no_kill      = no_kill;
	step_ptr->gres_list    = gres_list;
	step_ptr->srun_pid     = srun_pid;
	step_ptr->port         = port;
	step_ptr->pn_min_memory= pn_min_memory;
	step_ptr->host         = host;
	host                   = NULL;  /* re-used, nothing left to free */
	step_ptr->batch_step   = batch_step;
	step_ptr->start_time   = start_time;
	step_ptr->time_limit   = time_limit;
	step_ptr->pre_sus_time = pre_sus_time;
	step_ptr->tot_sus_time = tot_sus_time;

	if (!select_jobinfo)
		select_jobinfo = select_g_select_jobinfo_alloc();
	step_ptr->select_jobinfo = select_jobinfo;
	select_jobinfo = NULL;

	slurm_step_layout_destroy(step_ptr->step_layout);
	step_ptr->step_layout  = step_layout;

	if ((step_ptr->step_id.step_id == SLURM_EXTERN_CONT) && switch_tmp) {
		switch_g_free_jobinfo(switch_tmp);
		switch_tmp = NULL;
	} else
		step_ptr->switch_job   = switch_tmp;

	xfree(step_ptr->tres_alloc_str);
	step_ptr->tres_alloc_str     = tres_alloc_str;
	tres_alloc_str = NULL;

	xfree(step_ptr->cpus_per_tres);
	step_ptr->cpus_per_tres = cpus_per_tres;
	cpus_per_tres = NULL;
	xfree(step_ptr->mem_per_tres);
	step_ptr->mem_per_tres = mem_per_tres;
	mem_per_tres = NULL;
	xfree(step_ptr->tres_bind);
	step_ptr->tres_bind = tres_bind;
	tres_bind = NULL;
	xfree(step_ptr->tres_freq);
	step_ptr->tres_freq = tres_freq;
	tres_freq = NULL;
	xfree(step_ptr->tres_per_step);
	step_ptr->tres_per_step = tres_per_step;
	tres_per_step = NULL;
	xfree(step_ptr->tres_per_node);
	step_ptr->tres_per_node = tres_per_node;
	tres_per_node = NULL;
	xfree(step_ptr->tres_per_socket);
	step_ptr->tres_per_socket = tres_per_socket;
	tres_per_socket = NULL;
	xfree(step_ptr->tres_per_task);
	step_ptr->tres_per_task = tres_per_task;
	tres_per_task = NULL;

	xfree(step_ptr->tres_fmt_alloc_str);
	step_ptr->tres_fmt_alloc_str = tres_fmt_alloc_str;
	tres_fmt_alloc_str = NULL;

	step_ptr->cpu_freq_min = cpu_freq_min;
	step_ptr->cpu_freq_max = cpu_freq_max;
	step_ptr->cpu_freq_gov = cpu_freq_gov;
	step_ptr->state        = state;

	step_ptr->start_protocol_ver = start_protocol_ver;

	if (!step_ptr->ext_sensors)
		step_ptr->ext_sensors = ext_sensors_alloc();

	step_ptr->exit_code    = exit_code;

	if (exit_node_bitmap) {
		step_ptr->exit_node_bitmap = exit_node_bitmap;
		exit_node_bitmap = NULL;
	}

	if (core_bitmap_job) {
		step_ptr->core_bitmap_job = core_bitmap_job;
		core_bitmap_job = NULL;
	}

	if (step_ptr->step_layout && switch_tmp)
		switch_g_job_step_allocated(switch_tmp,
					    step_ptr->step_layout->node_list);

	info("recovered %pS", step_ptr);
	return SLURM_SUCCESS;

unpack_error:
	xfree(host);
	xfree(resv_ports);
	xfree(name);
	xfree(network);
	FREE_NULL_LIST(gres_list);
	bit_free(exit_node_bitmap);
	bit_free(core_bitmap_job);
	xfree(core_job);
	if (switch_tmp)
		switch_g_free_jobinfo(switch_tmp);
	slurm_step_layout_destroy(step_layout);
	select_g_select_jobinfo_free(select_jobinfo);
	xfree(tres_alloc_str);
	xfree(tres_fmt_alloc_str);
	xfree(cpus_per_tres);
	xfree(mem_per_tres);
	xfree(tres_bind);
	xfree(tres_freq);
	xfree(tres_per_step);
	xfree(tres_per_node);
	xfree(tres_per_socket);
	xfree(tres_per_task);

	return SLURM_ERROR;
}

static void _signal_step_timelimit(job_record_t *job_ptr, step_record_t *step_ptr,
				   time_t now)
{
#ifndef HAVE_FRONT_END
	int i;
#endif
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
	if (step_ptr->step_node_bitmap) {
		agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
		for (i = 0; i < node_record_count; i++) {
			if (bit_test(step_ptr->step_node_bitmap, i) == 0)
				continue;
			if (agent_args->protocol_version >
			    node_record_table_ptr[i].protocol_version) {
				agent_args->protocol_version =
					node_record_table_ptr[i].
					protocol_version;
			}
			hostlist_push_host(agent_args->hostlist,
					   node_record_table_ptr[i].name);
			agent_args->node_count++;
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

extern void check_job_step_time_limit(job_record_t *job_ptr, time_t now)
{
	ListIterator step_iterator;
	step_record_t *step_ptr;
	uint32_t job_run_mins = 0;

	xassert(job_ptr);

	if (job_ptr->job_state != JOB_RUNNING)
		return;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		if (step_ptr->state != JOB_RUNNING)
			continue;
		if (step_ptr->time_limit == INFINITE ||
		    step_ptr->time_limit == NO_VAL)
			continue;
		job_run_mins = (uint32_t) (((now - step_ptr->start_time) -
				step_ptr->tot_sus_time) / 60);
		if (job_run_mins >= step_ptr->time_limit) {
			/* this step has timed out */
			info("%s: %pS has timed out (%u)",
			     __func__, step_ptr, step_ptr->time_limit);
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
		if (slurm_conf.select_type_param & CR_MEMORY)
			mem_resv_value = true;
	}

	return mem_resv_value;
}

/*
 * Process job step update request from specified user,
 * RET - 0 or error code
 */
extern int update_step(step_update_request_msg_t *req, uid_t uid)
{
	job_record_t *job_ptr;
	step_record_t *step_ptr = NULL;
	step_record_t *step2_ptr = NULL;
	ListIterator step_iterator;
	int mod_cnt = 0;
	bool new_step = false;
	slurm_step_id_t step_id;

	job_ptr = find_job_record(req->job_id);
	if (job_ptr == NULL) {
		error("%s: invalid JobId=%u", __func__, req->job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	step_id.job_id = job_ptr->job_id;
	step_id.step_id = req->step_id;
	step_id.step_het_comp = NO_VAL;

	if (req->jobacct) {
		if (!validate_slurm_user(uid)) {
			error("Security violation, STEP_UPDATE RPC from uid %d",
			      uid);
			return ESLURM_USER_ID_MISSING;
		}
		/*
		 * Need to create a temporary step record (using some other
		 * launch mechanism that didn't use srun to launch).  Don't use
		 * _create_step_record though since we don't want to push it on
		 * the job's step_list.
		 */
		if (req->step_id == NO_VAL) {
			step_ptr = xmalloc(sizeof(*step_ptr));
			step_ptr->job_ptr    = job_ptr;
			step_ptr->exit_code  = NO_VAL;
			step_ptr->time_limit = INFINITE;
			step_ptr->jobacct    = jobacctinfo_create(NULL);
			step_ptr->requid     = -1;
			step_ptr->step_node_bitmap =
				bit_copy(job_ptr->node_bitmap);
			req->step_id = step_ptr->step_id.step_id =
				job_ptr->next_step_id++;
			new_step = 1;
		} else {
			if (req->step_id >= job_ptr->next_step_id)
				return ESLURM_INVALID_JOB_ID;

			if (!(step_ptr = find_step_record(job_ptr, &step_id))) {
				/*
				 * If updating this after the fact we need to
				 * remake the step so we can send the updated
				 * parts to accounting.
				 */
				step_ptr = xmalloc(sizeof(*step_ptr));
				step_ptr->job_ptr    = job_ptr;
				step_ptr->jobacct    = jobacctinfo_create(NULL);
				step_ptr->requid     = -1;
				memcpy(&step_ptr->step_id, &step_id,
				       sizeof(step_ptr->step_id));
				new_step = 1;
			}
		}
	} else if ((job_ptr->user_id != uid) && !validate_operator(uid) &&
		   !assoc_mgr_is_user_acct_coord(acct_db_conn, uid,
						 job_ptr->account)) {
		error("Security violation, STEP_UPDATE RPC from uid %d", uid);
		return ESLURM_USER_ID_MISSING;
	}

	/*
	 * No need to limit step time limit as job time limit will kill
	 * any steps with any time limit
	 */
	if (req->step_id == NO_VAL) {
		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step2_ptr = list_next(step_iterator))) {
			if (step2_ptr->state != JOB_RUNNING)
				continue;
			step2_ptr->time_limit = req->time_limit;
			mod_cnt++;
			info("Updating %pS time limit to %u",
			     step2_ptr, req->time_limit);
		}
		list_iterator_destroy (step_iterator);
	} else {
		if (!step_ptr)
			step_ptr = find_step_record(job_ptr, &step_id);

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
				/*
				 * If the exit_code is not NO_VAL then
				 * we need to initialize the node bitmap for
				 * exited nodes for packing.
				 */
				int nodes = bit_set_count(
					step_ptr->step_node_bitmap);
				step_ptr->exit_node_bitmap = bit_alloc(nodes);
			}
			step_ptr->exit_code = req->exit_code;

			jobacct_storage_g_step_complete(acct_db_conn, step_ptr);

			mod_cnt++;
			info("Updating %pS jobacct info", step_ptr);
		} else {
			step_ptr->time_limit = req->time_limit;
			mod_cnt++;
			info("Updating %pS time limit to %u",
			     step_ptr, req->time_limit);
		}
	}
	if (mod_cnt)
		last_job_update = time(NULL);
	if (new_step) {
		/*
		 * This was a temporary step record, never linked to the job,
		 * so there is no need to check SELECT_JOBDATA_CLEANING.
		 */
		_free_step_rec(step_ptr);
	}

	return SLURM_SUCCESS;
}

/* Return the total core count on a given node index */
static int _get_node_cores(int node_inx)
{
	node_record_t *node_ptr = node_record_table_ptr + node_inx;
	int socks, cores;

	socks = node_ptr->config_ptr->sockets;
	cores = node_ptr->config_ptr->cores;

	return socks * cores;
}

/*
 * Rebuild a job step's core_bitmap_job after a job has just changed size
 * job_ptr IN - job that was just re-sized
 * orig_job_node_bitmap IN - The job's original node bitmap
 */
extern void rebuild_step_bitmaps(job_record_t *job_ptr,
				 bitstr_t *orig_job_node_bitmap)
{
	step_record_t *step_ptr;
	ListIterator step_iterator;
	bitstr_t *orig_step_core_bitmap;
	int i, j, i_first, i_last, i_size;
	int old_core_offset, new_core_offset, node_core_count;
	bool old_node_set, new_node_set;

	if (job_ptr->step_list == NULL)
		return;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
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

/* NOTE: this function will call delete_step_record which will lock
 * the job_ptr->step_list so make sure you don't call this if you are
 * already holding a lock on that list (list_for_each).
 */
extern int post_job_step(step_record_t *step_ptr)
{
	job_record_t *job_ptr = step_ptr->job_ptr;
	int error_code;

	_step_dealloc_lps(step_ptr);
	gres_plugin_step_dealloc(step_ptr->gres_list,
				 job_ptr->gres_list, job_ptr->job_id,
				 step_ptr->step_id.step_id);

	/* Don't need to set state. Will be destroyed in next steps. */
	/* step_ptr->state = JOB_COMPLETE; */

	error_code = delete_step_record(job_ptr, step_ptr);
	if (error_code == ENOENT) {
		info("remove_job_step %pS not found", step_ptr);
		return ESLURM_ALREADY_DONE;
	}
	_wake_pending_steps(job_ptr);

	return SLURM_SUCCESS;
}

extern step_record_t *build_extern_step(job_record_t *job_ptr)
{
	step_record_t *step_ptr = _create_step_record(job_ptr, 0);
	char *node_list;
	uint32_t node_cnt;

#ifdef HAVE_FRONT_END
	node_list = job_ptr->front_end_ptr->name;
	node_cnt = 1;
#else
	node_list = job_ptr->nodes;
	node_cnt = job_ptr->node_cnt;
#endif

	step_ptr->step_layout = fake_slurm_step_layout_create(
		node_list, NULL, NULL, node_cnt, node_cnt);

	step_ptr->ext_sensors = ext_sensors_alloc();
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

	jobacct_storage_g_step_start(acct_db_conn, step_ptr);

	return step_ptr;
}

extern step_record_t *build_batch_step(job_record_t *job_ptr_in)
{
	job_record_t *job_ptr;
	step_record_t *step_ptr;

	if (job_ptr_in->het_job_id) {
		job_ptr = find_job_record(job_ptr_in->het_job_id);
		if (!job_ptr) {
			error("%s: hetjob leader is corrupt! This should never happen",
			      __func__);
			job_ptr = job_ptr_in;
		}
	} else
		job_ptr = job_ptr_in;

	step_ptr = _create_step_record(job_ptr, 0);

	step_ptr->step_layout = fake_slurm_step_layout_create(
		job_ptr->batch_host, NULL, NULL, 1, 1);
	step_ptr->ext_sensors = ext_sensors_alloc();
	step_ptr->name = xstrdup("batch");
	step_ptr->select_jobinfo = select_g_select_jobinfo_alloc();
	step_ptr->state = JOB_RUNNING;
	step_ptr->start_time = job_ptr->start_time;
	step_ptr->step_id.job_id = job_ptr->job_id;
	step_ptr->step_id.step_id = SLURM_BATCH_SCRIPT;
	step_ptr->step_id.step_het_comp = NO_VAL;
	step_ptr->batch_step = 1;

	if (node_name2bitmap(job_ptr->batch_host, false,
			     &step_ptr->step_node_bitmap)) {
		error("%s: %pJ has invalid node list (%s)",
		      __func__, job_ptr, job_ptr->batch_host);
	}

	step_ptr->time_last_active = time(NULL);
	step_set_alloc_tres(step_ptr, 1, false, false);

	jobacct_storage_g_step_start(acct_db_conn, step_ptr);

	return step_ptr;
}
