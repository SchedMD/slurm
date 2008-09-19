/*****************************************************************************\
 *  step_mgr.c - manage the job step information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <slurm/slurm_errno.h>

#include "src/common/bitstring.h"
#include "src/common/checkpoint.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/switch.h"
#include "src/common/xstring.h"
#include "src/common/forward.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobacct_gather.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

#define STEP_DEBUG 0
#define MAX_RETRIES 10

static int  _count_cpus(bitstr_t *bitmap);
static void _free_step_rec(struct step_record *step_ptr);
static void _pack_ctld_job_step_info(struct step_record *step, Buf buffer);
static bitstr_t * _pick_step_nodes (struct job_record  *job_ptr, 
				    job_step_create_request_msg_t *step_spec,
				    bool batch_step, int *return_code);
static hostlist_t _step_range_to_hostlist(struct step_record *step_ptr,
				uint32_t range_first, uint32_t range_last);
static int _step_hostname_to_inx(struct step_record *step_ptr,
				char *node_name);
static void _step_dealloc_lps(struct step_record *step_ptr);


/* 
 * create_step_record - create an empty step_record for the specified job.
 * IN job_ptr - pointer to job table entry to have step record added
 * RET a pointer to the record or NULL if error
 * NOTE: allocates memory that should be xfreed with delete_step_record
 */
struct step_record * 
create_step_record (struct job_record *job_ptr) 
{
	struct step_record *step_ptr;

	xassert(job_ptr);
	step_ptr = (struct step_record *) xmalloc(sizeof (struct step_record));

	last_job_update = time(NULL);
	step_ptr->job_ptr = job_ptr; 
	step_ptr->step_id = (job_ptr->next_step_id)++;
	step_ptr->start_time = time(NULL) ;
	step_ptr->jobacct = jobacct_gather_g_create(NULL);
	step_ptr->ckpt_path = NULL;
	if (list_append (job_ptr->step_list, step_ptr) == NULL)
		fatal ("create_step_record: unable to allocate memory");

	return step_ptr;
}


/* 
 * delete_step_records - delete step record for specified job_ptr
 * IN job_ptr - pointer to job table entry to have step records removed
 * IN filter  - determine which job steps to delete
 *              0: delete all job steps
 *              1: delete only job steps without a switch allocation
 */
extern void 
delete_step_records (struct job_record *job_ptr, int filter) 
{
	ListIterator step_iterator;
	struct step_record *step_ptr;

	xassert(job_ptr);
	step_iterator = list_iterator_create (job_ptr->step_list);

	last_job_update = time(NULL);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if ((filter == 1) && (step_ptr->switch_job))
			continue;

		list_remove (step_iterator);
		if (step_ptr->switch_job) {
			switch_g_job_step_complete(
				step_ptr->switch_job,
				step_ptr->step_layout->node_list);
			switch_free_jobinfo(step_ptr->switch_job);
		}
		checkpoint_free_jobinfo(step_ptr->check_job);
		_free_step_rec(step_ptr);
	}		

	list_iterator_destroy (step_iterator);
}

/* _free_step_rec - delete a step record's data structures */
static void _free_step_rec(struct step_record *step_ptr)
{
	xfree(step_ptr->host);
	xfree(step_ptr->name);
	slurm_step_layout_destroy(step_ptr->step_layout);
	jobacct_gather_g_destroy(step_ptr->jobacct);
	FREE_NULL_BITMAP(step_ptr->core_bitmap_job);
	FREE_NULL_BITMAP(step_ptr->exit_node_bitmap);
	FREE_NULL_BITMAP(step_ptr->step_node_bitmap);
	xfree(step_ptr->network);
	xfree(step_ptr->ckpt_path);
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

	xassert(job_ptr);
	error_code = ENOENT;
	step_iterator = list_iterator_create (job_ptr->step_list);
	last_job_update = time(NULL);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (step_ptr->step_id == step_id) {
			list_remove (step_iterator);
/* FIXME: If job step record is preserved after completion, 
 * the switch_g_job_step_complete() must be called upon completion 
 * and not upon record purging. Presently both events occur 
 * simultaneously. */
			if (step_ptr->switch_job) {
				switch_g_job_step_complete(
					step_ptr->switch_job, 
					step_ptr->step_layout->node_list);
				switch_free_jobinfo (step_ptr->switch_job);
			}
			checkpoint_free_jobinfo (step_ptr->check_job);

			xfree(step_ptr->host);
			xfree(step_ptr->name);
			slurm_step_layout_destroy(step_ptr->step_layout);
			jobacct_gather_g_destroy(step_ptr->jobacct);
			FREE_NULL_BITMAP(step_ptr->step_node_bitmap);
			FREE_NULL_BITMAP(step_ptr->exit_node_bitmap);
			if (step_ptr->network)
				xfree(step_ptr->network);
			xfree(step_ptr->ckpt_path);
			xfree(step_ptr);
			error_code = 0;
			break;
		}
	}		

	list_iterator_destroy (step_iterator);
	return error_code;
}


/*
 * dump_step_desc - dump the incoming step initiate request message
 * IN step_spec - job step request specification from RPC
 */
void
dump_step_desc(job_step_create_request_msg_t *step_spec)
{
	if (step_spec == NULL) 
		return;

	debug3("StepDesc: user_id=%u job_id=%u node_count=%u, cpu_count=%u", 
		step_spec->user_id, step_spec->job_id, 
		step_spec->node_count, step_spec->cpu_count);
	debug3("   num_tasks=%u relative=%u task_dist=%u node_list=%s", 
		step_spec->num_tasks, step_spec->relative, 
		step_spec->task_dist, step_spec->node_list);
	debug3("   host=%s port=%u name=%s network=%s checkpoint=%u", 
		step_spec->host, step_spec->port, step_spec->name,
		step_spec->network, step_spec->ckpt_interval);
	debug3("   checkpoint-path=%s exclusive=%u immediate=%u mem_per_task=%u",
	        step_spec->ckpt_path, step_spec->exclusive, 
		step_spec->immediate, step_spec->mem_per_task);
}


/* 
 * find_step_record - return a pointer to the step record with the given 
 *	job_id and step_id
 * IN job_ptr - pointer to job table entry to have step record added
 * IN step_id - id of the desired job step or NO_VAL for first one
 * RET pointer to the job step's record, NULL on error
 */
struct step_record *
find_step_record(struct job_record *job_ptr, uint16_t step_id) 
{
	ListIterator step_iterator;
	struct step_record *step_ptr;

	if (job_ptr == NULL)
		return NULL;

	step_iterator = list_iterator_create (job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if ((step_ptr->step_id == step_id)
		||  ((uint16_t) step_id == (uint16_t) NO_VAL)) {
			break;
		}
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
	struct step_record *step_ptr;

	job_ptr = find_job_record(job_id);
	if (job_ptr == NULL) {
		error("job_step_cancel: invalid job id %u", job_id);
		return ESLURM_INVALID_JOB_ID;
	}

	if (IS_JOB_FINISHED(job_ptr))
		return ESLURM_ALREADY_DONE;
	if (job_ptr->job_state != JOB_RUNNING) {
		verbose("job_step_signal: step %u.%u can not be sent signal "
			"%u from state=%s", job_id, step_id, signal,
			job_state_string(job_ptr->job_state));
		return ESLURM_TRANSITION_STATE_NO_UPDATE;
	}

	if ((job_ptr->user_id != uid) && (uid != 0) && (uid != getuid())) {
		error("Security violation, JOB_CANCEL RPC from uid %d",
		      uid);
		return ESLURM_USER_ID_MISSING;
	}

	step_ptr = find_step_record(job_ptr, step_id);
	if (step_ptr == NULL) {
		info("job_step_cancel step %u.%u not found",
		     job_id, step_id);
		return ESLURM_INVALID_JOB_ID;
	}
	
	/* save user ID of the one who requested the job be cancelled */
	if (signal == SIGKILL) {
		step_ptr->job_ptr->requid = uid;
		srun_step_complete(step_ptr);
	}

	signal_step_tasks(step_ptr, signal);
	return SLURM_SUCCESS;
}

/*
 * signal_step_tasks - send specific signal to specific job step
 * IN step_ptr - step record pointer
 * IN signal - signal to send
 */
void signal_step_tasks(struct step_record *step_ptr, uint16_t signal)
{
	int i;
	kill_tasks_msg_t *kill_tasks_msg;
	agent_arg_t *agent_args = NULL;
	
	xassert(step_ptr);
	agent_args = xmalloc(sizeof(agent_arg_t));
	if (signal == SIGKILL)
		agent_args->msg_type = REQUEST_TERMINATE_TASKS;
	else
		agent_args->msg_type = REQUEST_SIGNAL_TASKS;
	agent_args->retry = 1;
	agent_args->hostlist = hostlist_create("");
	kill_tasks_msg = xmalloc(sizeof(kill_tasks_msg_t));
	kill_tasks_msg->job_id      = step_ptr->job_ptr->job_id;
	kill_tasks_msg->job_step_id = step_ptr->step_id;
	kill_tasks_msg->signal      = signal;
	
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i) == 0)
			continue;
		hostlist_push(agent_args->hostlist,
			node_record_table_ptr[i].name);
		agent_args->node_count++;
#ifdef HAVE_FRONT_END		/* Operate only on front-end */
		break;
#endif
	}

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
	int error_code;

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

	jobacct_storage_g_step_complete(acct_db_conn, step_ptr);
	_step_dealloc_lps(step_ptr);

	if ((job_ptr->kill_on_step_done)
	    &&  (list_count(job_ptr->step_list) <= 1)
	    &&  (!IS_JOB_FINISHED(job_ptr))) 
		return job_complete(job_id, uid, requeue, job_return_code);

	last_job_update = time(NULL);
	error_code = delete_step_record(job_ptr, step_id);
	if (error_code == ENOENT) {
		info("job_step_complete step %u.%u not found", job_id,
		     step_id);
		return ESLURM_ALREADY_DONE;
	}
	return SLURM_SUCCESS;
}

/* 
 * _pick_step_nodes - select nodes for a job step that satisfy its requirements
 *	we satisfy the super-set of constraints.
 * IN job_ptr - pointer to job to have new step started
 * IN step_spec - job step specification
 * IN batch_step - if set then step is a batch script
 * OUT return_code - exit code or SLURM_SUCCESS
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: returns all of a job's nodes if step_spec->node_count == INFINITE
 * NOTE: returned bitmap must be freed by the caller using bit_free()
 */
static bitstr_t *
_pick_step_nodes (struct job_record  *job_ptr, 
		  job_step_create_request_msg_t *step_spec,
		  bool batch_step, int *return_code)
{
	bitstr_t *nodes_avail = NULL, *nodes_idle = NULL;
	bitstr_t *nodes_picked = NULL, *node_tmp = NULL;
	int error_code, nodes_picked_cnt=0, cpus_picked_cnt = 0, i;
	ListIterator step_iterator;
	struct step_record *step_p;
	select_job_res_t select_ptr = job_ptr->select_job;
#if STEP_DEBUG
	char *temp;
#endif

	xassert(select_ptr);
	xassert(select_ptr->cpus);
	xassert(select_ptr->cpus_used);
	xassert(select_ptr->memory_allocated);
	xassert(select_ptr->memory_used);

	*return_code = SLURM_SUCCESS;
	if (job_ptr->node_bitmap == NULL) {
		*return_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		return NULL;
	}
	
	nodes_avail = bit_copy (job_ptr->node_bitmap);
	if (nodes_avail == NULL)
		fatal("bit_copy malloc failure");
	bit_and (nodes_avail, up_node_bitmap);

	if (job_ptr->next_step_id == 0) {
		if (job_ptr->details && job_ptr->details->prolog_running) {
			*return_code = ESLURM_NODES_BUSY;
			return NULL;
		}
		for (i=bit_ffs(job_ptr->node_bitmap); i<node_record_count; 
		     i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			if ((node_record_table_ptr[i].node_state &
			     NODE_STATE_POWER_SAVE) ||
			    (node_record_table_ptr[i].node_state &
			     NODE_STATE_NO_RESPOND)) {
				/* Node is/was powered down. Need to wait 
				 * for it to start responding again. */
				FREE_NULL_BITMAP(nodes_avail);
				*return_code = ESLURM_NODES_BUSY;
				return NULL;
			}
		}
	}

	/* In exclusive mode, just satisfy the processor count.
	 * Do not use nodes that have no unused CPUs or insufficient 
	 * unused memory */
	if (step_spec->exclusive) {
		int avail_tasks, node_inx = 0, tot_tasks = 0, usable_mem;
		for (i=bit_ffs(job_ptr->node_bitmap); i<node_record_count; 
		     i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			avail_tasks = select_ptr->cpus[node_inx] - 
				      select_ptr->cpus_used[node_inx];
			tot_tasks += job_ptr->select_job->cpus[node_inx];
			if (step_spec->mem_per_task) {
				usable_mem = select_ptr->
					     memory_allocated[node_inx] -
					     select_ptr->memory_used[node_inx];
				usable_mem /= step_spec->mem_per_task;
				avail_tasks = MIN(avail_tasks, usable_mem);
				usable_mem = select_ptr->
					     memory_allocated[node_inx];
				usable_mem /= step_spec->mem_per_task;
				tot_tasks = MIN(tot_tasks, usable_mem);
			}
			if ((avail_tasks <= 0) ||
			    (cpus_picked_cnt >= step_spec->cpu_count))
				bit_clear(nodes_avail, i);
			else
				cpus_picked_cnt += avail_tasks;
			if (++node_inx >= job_ptr->node_cnt)
				break;
		}
		if (cpus_picked_cnt >= step_spec->cpu_count)
			return nodes_avail;

		FREE_NULL_BITMAP(nodes_avail);
		if (tot_tasks >= step_spec->cpu_count)
			*return_code = ESLURM_NODES_BUSY;
		else
			*return_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
		return NULL;
	}

	if (step_spec->mem_per_task) {
		int node_inx = 0, usable_mem;
		for (i=bit_ffs(job_ptr->node_bitmap); i<node_record_count; 
		     i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			usable_mem = select_ptr->memory_allocated[node_inx] -
				     select_ptr->memory_used[node_inx];
			usable_mem /= step_spec->mem_per_task;
			if (usable_mem <= 0) {
				if (step_spec->node_count == INFINITE) {
					FREE_NULL_BITMAP(nodes_avail);
					*return_code = 
						ESLURM_INVALID_TASK_MEMORY;
					return NULL;
				}
				bit_clear(nodes_avail, i);
			}
			if (++node_inx >= job_ptr->node_cnt)
				break;
		}
	}

	if (step_spec->node_count == INFINITE)	/* use all nodes */
		return nodes_avail;

	if (step_spec->node_list) {
		bitstr_t *selected_nodes = NULL;
#if STEP_DEBUG
		info("selected nodelist is %s", step_spec->node_list);
#endif
		error_code = node_name2bitmap(step_spec->node_list, false, 
					      &selected_nodes);
		
		if (error_code) {
			info("_pick_step_nodes: invalid node list %s", 
				step_spec->node_list);
			bit_free(selected_nodes);
			goto cleanup;
		}
		if (!bit_super_set(selected_nodes, job_ptr->node_bitmap)) {
			info ("_pick_step_nodes: requested nodes %s not part "
				"of job %u", 
				step_spec->node_list, job_ptr->job_id);
			bit_free(selected_nodes);
			goto cleanup;
		}
		if (!bit_super_set(selected_nodes, nodes_avail)) {
			info ("_pick_step_nodes: requested nodes %s "
			      "have inadequate memory",
			       step_spec->node_list);
			bit_free(selected_nodes);
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
				error("Can't do an ARBITRARY task layout with "
				      "switch type elan. Switching DIST type "
				      "to BLOCK");
				xfree(step_spec->node_list);
				step_spec->task_dist = SLURM_DIST_BLOCK;
				FREE_NULL_BITMAP(selected_nodes);
				step_spec->node_count =
					bit_set_count(nodes_avail);
			} else 
				step_spec->node_count =
					bit_set_count(selected_nodes);
		}
		if (selected_nodes) {
			/* use selected nodes to run the job and
			 * make them unavailable for future use */
			
			/* If we have selected more than we requested
			 * make the available nodes equal to the
			 * selected nodes and we will pick from that
			 * list later on in the function.
			 * Other than that copy the nodes selected as
			 * the nodes we want.
			 */ 
			if (step_spec->node_count 
			    && (bit_set_count(selected_nodes)
				> step_spec->node_count)) {
				nodes_picked =
					bit_alloc(bit_size(nodes_avail));
				if (nodes_picked == NULL)
					fatal("bit_alloc malloc failure");
				bit_free(nodes_avail);
				nodes_avail = selected_nodes;
				selected_nodes = NULL;
			} else {
				nodes_picked = bit_copy(selected_nodes);
				bit_not(selected_nodes);
				bit_and(nodes_avail, selected_nodes);
				bit_free(selected_nodes);
			}
		}
	} else {
		nodes_picked = bit_alloc(bit_size(nodes_avail));
		if (nodes_picked == NULL)
			fatal("bit_alloc malloc failure");
	}
	
	if (step_spec->relative != (uint16_t)NO_VAL) {
		/* Remove first (step_spec->relative) nodes from  
		 * available list */
		bitstr_t *relative_nodes = NULL;
		relative_nodes = 
			bit_pick_cnt(nodes_avail, step_spec->relative);
		if (relative_nodes == NULL) {
			info ("_pick_step_nodes: "
			      "Invalid relative value (%u) for job %u",
			      step_spec->relative, job_ptr->job_id);
			goto cleanup;
		}
		bit_not (relative_nodes);
		bit_and (nodes_avail, relative_nodes);
		bit_free (relative_nodes);
	} else {
		nodes_idle = bit_alloc (bit_size (nodes_avail) );
		if (nodes_idle == NULL)
			fatal("bit_alloc malloc failure");
		step_iterator = 
			list_iterator_create(job_ptr->step_list);
		while ((step_p = (struct step_record *)
			list_next(step_iterator))) {
			bit_or(nodes_idle, step_p->step_node_bitmap);
#if STEP_DEBUG
			temp = bitmap2node_name(step_p->step_node_bitmap);
			info("step %d has nodes %s", step_p->step_id, temp);
			xfree(temp);
#endif
		} 
		list_iterator_destroy (step_iterator);
		bit_not(nodes_idle);
		bit_and(nodes_idle, nodes_avail);
	}

#if STEP_DEBUG
	temp = bitmap2node_name(nodes_avail);
	info("can pick from %s %d", temp, step_spec->node_count);
	xfree(temp);
	temp = bitmap2node_name(nodes_idle);
	info("can pick from %s", temp);
	xfree(temp);
#endif

	/* if user specifies step needs a specific processor count and 
	 * all nodes have the same processor count, just translate this to
	 * a node count */
	if (step_spec->cpu_count && job_ptr->select_job && 
	    (job_ptr->select_job->cpu_array_cnt == 1) &&
	    job_ptr->select_job->cpu_array_value) {
		i = (step_spec->cpu_count + 
		     (job_ptr->select_job->cpu_array_value[0] - 1)) /
		    job_ptr->select_job->cpu_array_value[0];
		step_spec->node_count = (i > step_spec->node_count) ? 
					 i : step_spec->node_count ;
		step_spec->cpu_count = 0;
	}

	if (step_spec->node_count) {
		nodes_picked_cnt = bit_set_count(nodes_picked);
#if STEP_DEBUG
		info("got %u %d", step_spec->node_count, nodes_picked_cnt);
#endif
		if (nodes_idle 
		    && (bit_set_count(nodes_idle) >= step_spec->node_count)
		    && (step_spec->node_count > nodes_picked_cnt)) {
			node_tmp = bit_pick_cnt(nodes_idle,
						(step_spec->node_count -
						 nodes_picked_cnt));
			if (node_tmp == NULL)
				goto cleanup;
			bit_or  (nodes_picked, node_tmp);
			bit_not (node_tmp);
			bit_and (nodes_idle, node_tmp);
			bit_and (nodes_avail, node_tmp);
			bit_free (node_tmp);
			node_tmp = NULL;
			nodes_picked_cnt = step_spec->node_count;
		}
		if (step_spec->node_count > nodes_picked_cnt) {
			node_tmp = bit_pick_cnt(nodes_avail, 
						(step_spec->node_count - 
						 nodes_picked_cnt));
			if (node_tmp == NULL)
				goto cleanup;
			bit_or  (nodes_picked, node_tmp);
			bit_not (node_tmp);
			bit_and (nodes_avail, node_tmp);
			bit_free (node_tmp);
			node_tmp = NULL;
			nodes_picked_cnt = step_spec->node_count;
		}
	}
	
	if (step_spec->cpu_count) {
		/* make sure the selected nodes have enough cpus */
		cpus_picked_cnt = _count_cpus(nodes_picked);
		/* user is requesting more cpus than we got from the
		 * picked nodes we should return with an error */
		if (step_spec->cpu_count > cpus_picked_cnt) {
			debug2("Have %d nodes with %d cpus which is less "
			       "than what the user is asking for (%d cpus) "
			       "aborting.",
			       nodes_picked_cnt, cpus_picked_cnt,
			       step_spec->cpu_count);
			goto cleanup;
		}
	}
	
	FREE_NULL_BITMAP(nodes_avail);
	FREE_NULL_BITMAP(nodes_idle);
	return nodes_picked;

cleanup:
	FREE_NULL_BITMAP(nodes_avail);
	FREE_NULL_BITMAP(nodes_idle);
	FREE_NULL_BITMAP(nodes_picked);
	*return_code = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	return NULL;
}

/*
 * _count_cpus - report how many cpus are associated with the identified nodes 
 * IN bitmap - map of nodes to tally
 * RET cpu count
 * globals: node_record_count - number of nodes configured
 *	node_record_table_ptr - pointer to global node table
 */
static int _count_cpus(bitstr_t *bitmap)
{
	int i, sum;

	sum = 0;
	for (i = 0; i < node_record_count; i++) {
		if (bit_test(bitmap, i) != 1)
			continue;
		if (slurmctld_conf.fast_schedule)
			sum += node_record_table_ptr[i].config_ptr->cpus;
		else
			sum += node_record_table_ptr[i].cpus;
	}
	return sum;
}

/* Update the step's core bitmaps, create as needed.
 *	Add the specified task count for a specific node in the job's 
 *	and step's allocation */
static void _pick_step_cores(struct step_record *step_ptr, 
			     select_job_res_t select_ptr, 
			     int step_node_inx, int job_node_inx,
			     uint16_t task_cnt)
{
	int bit_offset, core_inx, sock_inx;
	uint16_t sockets, cores;
	bool use_all_cores;

	if (!step_ptr->core_bitmap_job) {
		step_ptr->core_bitmap_job = bit_alloc(bit_size(select_ptr->
							       core_bitmap));
	}
	if (get_select_job_res_cnt(select_ptr, job_node_inx, &sockets, &cores))
		fatal("get_select_job_res_cnt");

	if (task_cnt == (cores * sockets))
		use_all_cores = true;
	else
		use_all_cores = false;

	/* select idle cores first */
	for (core_inx=0; core_inx<cores; core_inx++) {
		for (sock_inx=0; sock_inx<sockets; sock_inx++) {
			bit_offset = get_select_job_res_offset(select_ptr,
							       job_node_inx,
							       sock_inx, 
							       core_inx);
			if (bit_offset < 0)
				fatal("get_select_job_res_offset");
			if (!bit_test(select_ptr->core_bitmap, bit_offset))
				continue;
			if ((use_all_cores == false) &&
			    bit_test(select_ptr->core_bitmap_used, bit_offset))
				continue;
			bit_set(select_ptr->core_bitmap_used, bit_offset);
			bit_set(step_ptr->core_bitmap_job, bit_offset);
#if 0
			info("step alloc N:%d S:%dC :%d", 
			     job_node_inx, sock_inx, core_inx);
#endif
			if (--task_cnt == 0)
				return;
		}
	}
	if (use_all_cores)
		return;

	/* Need to over-subscribe some cores */
	for (core_inx=0; core_inx<cores; core_inx++) {
		for (sock_inx=0; sock_inx<sockets; sock_inx++) {
			bit_offset = get_select_job_res_offset(select_ptr,
							       job_node_inx,
							       sock_inx, 
							       core_inx);
			if (bit_offset < 0)
				fatal("get_select_job_res_offset");
			if (!bit_test(select_ptr->core_bitmap, bit_offset))
				continue;
			if (bit_test(step_ptr->core_bitmap_job, bit_offset))
				continue;   /* already taken by this step */
			bit_set(step_ptr->core_bitmap_job, bit_offset);
#if 0
			info("step alloc N:%d S:%dC :%d", 
			     job_node_inx, sock_inx, core_inx);
#endif
			if (--task_cnt == 0)
				return;
		}
	}
}


/* Update a job's record of allocated CPUs when a job step gets scheduled */
extern void step_alloc_lps(struct step_record *step_ptr)
{
	struct job_record  *job_ptr = step_ptr->job_ptr;
	select_job_res_t select_ptr = job_ptr->select_job;
	int i_node, i_first, i_last;
	int job_node_inx = -1, step_node_inx = -1;
	bool pick_step_cores = true;

	xassert(select_ptr);
	xassert(select_ptr->core_bitmap);
	xassert(select_ptr->core_bitmap_used);
	xassert(select_ptr->cpus);
	xassert(select_ptr->cpus_used);
	xassert(select_ptr->memory_allocated);
	xassert(select_ptr->memory_used);

	i_first = bit_ffs(job_ptr->node_bitmap);
	i_last  = bit_fls(job_ptr->node_bitmap);
	if (i_first == -1)	/* empty bitmap */
		return;

	if (step_ptr->core_bitmap_job) {
		/* "scontrol reconfig" of live system */
		pick_step_cores = false;
	} else if (step_ptr->cpu_count == job_ptr->total_procs) {
		/* Step uses all of job's cores
		 * Just copy the bitmap to save time */
		step_ptr->core_bitmap_job = bit_copy(select_ptr->core_bitmap);
		pick_step_cores = false;
	}

	for (i_node = i_first; i_node <= i_last; i_node++) {
		if (!bit_test(job_ptr->node_bitmap, i_node))
			continue;
		job_node_inx++;
		if (!bit_test(step_ptr->step_node_bitmap, i_node))
			continue;
		step_node_inx++;
		select_ptr->cpus_used[job_node_inx] += 
			step_ptr->step_layout->tasks[step_node_inx];
		if (step_ptr->mem_per_task) {
			select_ptr->memory_used[job_node_inx] += 
				(step_ptr->mem_per_task *
				 step_ptr->step_layout->tasks[step_node_inx]);
		}
		if (pick_step_cores) {
			_pick_step_cores(step_ptr, select_ptr, 
					 step_node_inx, job_node_inx,
					 step_ptr->step_layout->
					 tasks[step_node_inx]);
		}
#if 0
		info("step alloc of %s procs: %u of %u", 
		     node_record_table_ptr[i_node].name,
		     select_ptr->cpus_used[job_node_inx],
		     select_ptr->cpus[job_node_inx]);
#endif
		if (step_node_inx == (step_ptr->step_layout->node_cnt - 1))
			break;
	}
	
}

static void _step_dealloc_lps(struct step_record *step_ptr)
{
	struct job_record  *job_ptr = step_ptr->job_ptr;
	select_job_res_t select_ptr = job_ptr->select_job;
	int i_node, i_first, i_last;
	int job_node_inx = -1, step_node_inx = -1;

	xassert(select_ptr);
	xassert(select_ptr->core_bitmap);
	xassert(select_ptr->core_bitmap_used);
	xassert(select_ptr->cpus);
	xassert(select_ptr->cpus_used);
	xassert(select_ptr->memory_allocated);
	xassert(select_ptr->memory_used);

	if (step_ptr->step_layout == NULL)	/* batch step */
		return;

	i_first = bit_ffs(job_ptr->node_bitmap);
	i_last  = bit_fls(job_ptr->node_bitmap);
	if (i_first == -1)	/* empty bitmap */
		return;
	for (i_node = i_first; i_node <= i_last; i_node++) {
		if (!bit_test(job_ptr->node_bitmap, i_node))
			continue;
		job_node_inx++;
		if (!bit_test(step_ptr->step_node_bitmap, i_node))
			continue;
		step_node_inx++;
		if (select_ptr->cpus_used[job_node_inx] >=
		    step_ptr->step_layout->tasks[step_node_inx]) {
			select_ptr->cpus_used[job_node_inx] -= 
				step_ptr->step_layout->tasks[step_node_inx];
		} else {
			error("_step_dealloc_lps: cpu underflow for %u.%u",
				job_ptr->job_id, step_ptr->step_id);
			select_ptr->cpus_used[job_node_inx] = 0;
		}
		if (step_ptr->mem_per_task) {
			uint32_t mem_use = step_ptr->mem_per_task *
					   step_ptr->step_layout->
					   tasks[step_node_inx];
			if (select_ptr->memory_used[job_node_inx] >= mem_use)
				select_ptr->memory_used[job_node_inx] -= mem_use;
			else {
				error("_step_dealloc_lps: "
				      "mem underflow for %u.%u",
				      job_ptr->job_id, step_ptr->step_id);
				select_ptr->memory_used[job_node_inx] = 0;
			}
		}
#if 0
		info("step dealloc of %s procs: %u of %u", 
		     node_record_table_ptr[i_node].name,
		     select_ptr->cpus_used[job_node_inx],
		     select_ptr->cpus[job_node_inx]);
#endif
		if (step_node_inx == (step_ptr->step_layout->node_cnt - 1))
			break;
	}
	if (step_ptr->core_bitmap_job) {
		/* Mark the job's cores as no longer in use */
		bit_not(step_ptr->core_bitmap_job);
		bit_and(select_ptr->core_bitmap_used,
			step_ptr->core_bitmap_job);
		/* no need for bit_not(step_ptr->core_bitmap_job); */
		FREE_NULL_BITMAP(step_ptr->core_bitmap_job);
	}
}

/*
 * step_create - creates a step_record in step_specs->job_id, sets up the
 *	according to the step_specs.
 * IN step_specs - job step specifications
 * OUT new_step_record - pointer to the new step_record (NULL on error)
 * IN kill_job_when_step_done - if set kill the job on step completion
 * IN batch_step - if set then step is a batch script
 * RET - 0 or error code
 * NOTE: don't free the returned step_record because that is managed through
 * 	the job.
 */
extern int
step_create(job_step_create_request_msg_t *step_specs, 
	    struct step_record** new_step_record,
	    bool kill_job_when_step_done, bool batch_step)
{
	struct step_record *step_ptr;
	struct job_record  *job_ptr;
	bitstr_t *nodeset;
	int node_count, ret_code;
	time_t now = time(NULL);
	char *step_node_list = NULL;
	uint32_t orig_cpu_count;

	*new_step_record = NULL;
	job_ptr = find_job_record (step_specs->job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID ;

	if ((job_ptr->details == NULL) || 
	    (job_ptr->job_state == JOB_SUSPENDED))
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
	    (step_specs->task_dist != SLURM_DIST_PLANE) &&
	    (step_specs->task_dist != SLURM_DIST_ARBITRARY))
		return ESLURM_BAD_DIST;

	if ((step_specs->task_dist == SLURM_DIST_ARBITRARY) &&
	    (!strcmp(slurmctld_conf.switch_type, "switch/elan"))) {
		return ESLURM_TASKDIST_ARBITRARY_UNSUPPORTED;
	}

	if ((step_specs->host      && 
	     (strlen(step_specs->host)      > MAX_STR_LEN)) ||
	    (step_specs->node_list && 
	     (strlen(step_specs->node_list) > MAX_STR_LEN)) ||
	    (step_specs->network   && 
	     (strlen(step_specs->network)   > MAX_STR_LEN)) ||
	    (step_specs->name      && 
	     (strlen(step_specs->name)      > MAX_STR_LEN)) ||
	    (step_specs->ckpt_path && 
	     (strlen(step_specs->ckpt_path) > MAX_STR_LEN)))
		return ESLURM_PATHNAME_TOO_LONG;

	/* if the overcommit flag is checked we 0 out the cpu_count
	 * which makes it so we don't check to see the available cpus
	 */
	orig_cpu_count =  step_specs->cpu_count;
	if (step_specs->overcommit)
		step_specs->cpu_count = 0;

	if (job_ptr->kill_on_step_done)
		/* Don't start more steps, job already being cancelled */
		return ESLURM_ALREADY_DONE;
	job_ptr->kill_on_step_done = kill_job_when_step_done;

	job_ptr->time_last_active = now;
	nodeset = _pick_step_nodes(job_ptr, step_specs, batch_step, &ret_code);
	if (nodeset == NULL)
		return ret_code;
	node_count = bit_set_count(nodeset);

	if (step_specs->num_tasks == NO_VAL) {
		if (step_specs->cpu_count != NO_VAL)
			step_specs->num_tasks = step_specs->cpu_count;
		else
			step_specs->num_tasks = node_count;
	}
	
	if ((step_specs->num_tasks < 1)
	||  (step_specs->num_tasks > (node_count*MAX_TASKS_PER_NODE))) {
		error("step has invalid task count: %u", 
		      step_specs->num_tasks);
		bit_free(nodeset);
		return ESLURM_BAD_TASK_COUNT;
	}

	step_ptr = create_step_record (job_ptr);
	if (step_ptr == NULL)
		fatal ("create_step_record failed with no memory");

	/* set the step_record values */

	/* Here is where the node list is set for the step */
	if(step_specs->node_list 
	   && step_specs->task_dist == SLURM_DIST_ARBITRARY) {
		step_node_list = xstrdup(step_specs->node_list);
		xfree(step_specs->node_list);
		step_specs->node_list = bitmap2node_name(nodeset);
	} else {
		step_node_list = bitmap2node_name(nodeset);
		xfree(step_specs->node_list);
		step_specs->node_list = xstrdup(step_node_list);
	}
#if STEP_DEBUG
	info("got %s and %s looking for %d nodes", step_node_list,
	     step_specs->node_list, step_specs->node_count);
#endif
	step_ptr->step_node_bitmap = nodeset;
	
	switch(step_specs->task_dist) {
	case SLURM_DIST_CYCLIC: 
	case SLURM_DIST_CYCLIC_CYCLIC: 
	case SLURM_DIST_CYCLIC_BLOCK: 
		step_ptr->cyclic_alloc = 1;
		break;
	default:
		step_ptr->cyclic_alloc = 0;
		break;
	}

	step_ptr->port = step_specs->port;
	step_ptr->host = xstrdup(step_specs->host);
	step_ptr->batch_step = batch_step;
	step_ptr->mem_per_task = step_specs->mem_per_task;
	step_ptr->ckpt_interval = step_specs->ckpt_interval;
	step_ptr->ckpt_time = now;
	step_ptr->cpu_count = orig_cpu_count;
	step_ptr->exit_code = NO_VAL;
	step_ptr->exclusive = step_specs->exclusive;
	step_ptr->ckpt_path = xstrdup(step_specs->ckpt_path);

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
	
	/* a batch script does not need switch info */
	if (!batch_step) {
		step_ptr->step_layout = 
			step_layout_create(step_ptr,
					   step_node_list,
					   step_specs->node_count,
					   step_specs->num_tasks,
					   step_specs->task_dist,
					   step_specs->plane_size);
		if (!step_ptr->step_layout) {
			_free_step_rec(step_ptr);
			return SLURM_ERROR;
		}
		if (switch_alloc_jobinfo (&step_ptr->switch_job) < 0)
			fatal ("step_create: switch_alloc_jobinfo error");
		
		if (switch_build_jobinfo(step_ptr->switch_job, 
					 step_ptr->step_layout->node_list,
					 step_ptr->step_layout->tasks, 
					 step_ptr->cyclic_alloc,
					 step_ptr->network) < 0) {
			error("switch_build_jobinfo: %m");
			delete_step_record (job_ptr, step_ptr->step_id);
			return ESLURM_INTERCONNECT_FAILURE;
		}
		step_alloc_lps(step_ptr);
	}
	if (checkpoint_alloc_jobinfo (&step_ptr->check_job) < 0)
		fatal ("step_create: checkpoint_alloc_jobinfo error");
	xfree(step_node_list);
	*new_step_record = step_ptr;
	jobacct_storage_g_step_start(acct_db_conn, step_ptr);
	return SLURM_SUCCESS;
}

extern slurm_step_layout_t *step_layout_create(struct step_record *step_ptr,
					       char *step_node_list,
					       uint32_t node_count,
					       uint32_t num_tasks,
					       uint16_t task_dist,
					       uint32_t plane_size)
{
	uint16_t cpus_per_node[node_count];
	uint32_t cpu_count_reps[node_count];
	int cpu_inx = -1;
	int i, usable_cpus, usable_mem;
	int set_nodes = 0, set_cpus = 0;
	int pos = -1;
	int first_bit, last_bit;
	struct job_record *job_ptr = step_ptr->job_ptr;
	select_job_res_t select_ptr = job_ptr->select_job;

	xassert(select_ptr);
	xassert(select_ptr->cpus);
	xassert(select_ptr->cpus_used);
	xassert(select_ptr->memory_allocated);
	xassert(select_ptr->memory_used);

	/* build the cpus-per-node arrays for the subset of nodes
	 * used by this job step */
	first_bit = bit_ffs(step_ptr->step_node_bitmap);
	last_bit  = bit_fls(step_ptr->step_node_bitmap);
	for (i = first_bit; i <= last_bit; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i)) {
			/* find out the position in the job */
			pos = bit_get_pos_num(job_ptr->node_bitmap, i);
			if (pos == -1)
				return NULL;
			if (step_ptr->exclusive) {
				usable_cpus = select_ptr->cpus[pos] -
					      select_ptr->cpus_used[pos];
				usable_cpus = MAX(usable_cpus, 
						  (num_tasks - set_cpus));
			} else
				usable_cpus = select_ptr->cpus[pos];
			if (step_ptr->mem_per_task) {
				usable_mem = select_ptr->memory_allocated[pos] -
					     select_ptr->memory_used[pos];
				usable_mem /= step_ptr->mem_per_task;
				usable_cpus = MIN(usable_cpus, usable_mem);
			}
			if (usable_cpus <= 0) {
				error("step_layout_create no usable cpus");
				return NULL;
			}
			debug2("step_layout cpus = %d pos = %d", 
			       usable_cpus, pos);
			
			if ((cpu_inx == -1) ||
			    (cpus_per_node[cpu_inx] != usable_cpus)) {
				cpu_inx++;
				
				cpus_per_node[cpu_inx] = usable_cpus;
				cpu_count_reps[cpu_inx] = 1;
			} else
				cpu_count_reps[cpu_inx]++;
			set_nodes++;
			set_cpus += usable_cpus;
			if (set_nodes == node_count)
				break;
		}
	}

	/* layout the tasks on the nodes */
	return slurm_step_layout_create(step_node_list,
					cpus_per_node, cpu_count_reps, 
					node_count, num_tasks, task_dist,
					plane_size);
}

/* Pack the data for a specific job step record
 * IN step - pointer to a job step record
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _pack_ctld_job_step_info(struct step_record *step_ptr, Buf buffer)
{
	int task_cnt;
	char *node_list = NULL;
	time_t begin_time, run_time;

	if (step_ptr->step_layout) {
		task_cnt = step_ptr->step_layout->task_cnt;
		node_list = step_ptr->step_layout->node_list;		
	} else {
		task_cnt = step_ptr->job_ptr->num_procs;
		node_list = step_ptr->job_ptr->nodes;	
	}
	pack32(step_ptr->job_ptr->job_id, buffer);
	pack16(step_ptr->step_id, buffer);
	pack16(step_ptr->ckpt_interval, buffer);
	pack32(step_ptr->job_ptr->user_id, buffer);
	pack32(task_cnt, buffer);

	pack_time(step_ptr->start_time, buffer);
	if (step_ptr->job_ptr->job_state == JOB_SUSPENDED) {
		run_time = step_ptr->pre_sus_time;
	} else {
		begin_time = MAX(step_ptr->start_time,
				step_ptr->job_ptr->suspend_time);
		run_time = step_ptr->pre_sus_time +
			difftime(time(NULL), begin_time);
	}
	pack_time(run_time, buffer);
	packstr(step_ptr->job_ptr->partition, buffer);
	packstr(node_list, buffer);
	packstr(step_ptr->name, buffer);
	packstr(step_ptr->network, buffer);
	pack_bit_fmt(step_ptr->step_node_bitmap, buffer);
	packstr(step_ptr->ckpt_path, buffer);
	
}

/* 
 * pack_ctld_job_step_info_response_msg - packs job step info
 * IN job_id - specific id or zero for all
 * IN step_id - specific id or zero for all
 * IN uid - user issuing request
 * IN show_flags - job step filtering options
 * OUT buffer - location to store data, pointers automatically advanced 
 * RET - 0 or error code
 * NOTE: MUST free_buf buffer
 */
extern int pack_ctld_job_step_info_response_msg(uint32_t job_id, 
			uint32_t step_id, uid_t uid, 
			uint16_t show_flags, Buf buffer)
{
	ListIterator job_iterator;
	ListIterator step_iterator;
	int error_code = 0;
	uint32_t steps_packed = 0, tmp_offset;
	struct step_record *step_ptr;
	struct job_record *job_ptr;
	time_t now = time(NULL);

	pack_time(now, buffer);
	pack32(steps_packed, buffer);	/* steps_packed placeholder */

	part_filter_set(uid);
	if (job_id == 0) {
		/* Return all steps for all jobs */
		job_iterator = list_iterator_create(job_list);
		while ((job_ptr = 
				(struct job_record *) 
				list_next(job_iterator))) {
			if (((show_flags & SHOW_ALL) == 0) && (uid != 0) &&
			    (job_ptr->part_ptr) && 
			    (job_ptr->part_ptr->hidden))
				continue;

			if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS)
			&&  (job_ptr->user_id != uid) 
			&&  !validate_super_user(uid))
				continue;

			step_iterator =
			    list_iterator_create(job_ptr->step_list);
			while ((step_ptr =
					(struct step_record *)
					list_next(step_iterator))) {
				_pack_ctld_job_step_info(step_ptr, buffer);
				steps_packed++;
			}
			list_iterator_destroy(step_iterator);
		}
		list_iterator_destroy(job_iterator);

	} else if (step_id == 0) {
		/* Return all steps for specific job_id */
		job_ptr = find_job_record(job_id);
		if (((show_flags & SHOW_ALL) == 0) && 
		    (job_ptr->part_ptr) && 
		    (job_ptr->part_ptr->hidden))
			job_ptr = NULL;
		else if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS)
		&&  (job_ptr->user_id != uid) && !validate_super_user(uid))
			job_ptr = NULL;

		if (job_ptr) {
			step_iterator = 
				list_iterator_create(job_ptr->step_list);
			while ((step_ptr =
					(struct step_record *)
					list_next(step_iterator))) {
				_pack_ctld_job_step_info(step_ptr, buffer);
				steps_packed++;
			}
			list_iterator_destroy(step_iterator);
		} else
			error_code = ESLURM_INVALID_JOB_ID;
	} else {
		/* Return data for specific job_id.step_id */
		job_ptr = find_job_record(job_id);
		if (((show_flags & SHOW_ALL) == 0) 
		&&  (job_ptr != NULL)
		&&  (job_ptr->part_ptr) 
		&&  (job_ptr->part_ptr->hidden))
			job_ptr = NULL;
		else if ((slurmctld_conf.private_data & PRIVATE_DATA_JOBS)
		&&  (job_ptr->user_id != uid) && !validate_super_user(uid))
			job_ptr = NULL;

		step_ptr = find_step_record(job_ptr, step_id);
		if (step_ptr == NULL)
			error_code = ESLURM_INVALID_JOB_ID;
		else {
			_pack_ctld_job_step_info(step_ptr, buffer);
			steps_packed++;
		}
	}
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
 * step_on_node - determine if the specified job has any job steps allocated to 
 * 	the specified node 
 * IN job_ptr - pointer to an active job record
 * IN node_ptr - pointer to a node record
 * RET true of job has step on the node, false otherwise 
 */
bool step_on_node(struct job_record  *job_ptr, struct node_record *node_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	bool found = false;
	int bit_position;

	if ((job_ptr == NULL) || (node_ptr == NULL))
		return false;

	bit_position = node_ptr - node_record_table_ptr;
	step_iterator = list_iterator_create (job_ptr->step_list);	
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		if (bit_test(step_ptr->step_node_bitmap, bit_position)) {
			found = true;
			break;
		}
	}		

	list_iterator_destroy (step_iterator);
	return found;
}

/*
 * job_step_checkpoint - perform some checkpoint operation
 * IN ckpt_ptr - checkpoint request message 
 * IN uid - user id of the user issuing the RPC
 * IN conn_fd - file descriptor on which to send reply
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint(checkpoint_msg_t *ckpt_ptr,
		uid_t uid, slurm_fd conn_fd)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	checkpoint_resp_msg_t resp_data;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&resp_msg);
	
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
	if (job_ptr->job_state == JOB_PENDING) {
		rc = ESLURM_JOB_PENDING;
		goto reply;
	} else if (job_ptr->job_state == JOB_SUSPENDED) {
		/* job can't get cycles for checkpoint 
		 * if it is already suspended */
		rc = ESLURM_DISABLED;
		goto reply;
	} else if (job_ptr->job_state != JOB_RUNNING) {
		rc = ESLURM_ALREADY_DONE;
		goto reply;
	}

	bzero((void *)&resp_data, sizeof(checkpoint_resp_msg_t));
	/* find the individual job step */
	if (ckpt_ptr->step_id != NO_VAL) {
		step_ptr = find_step_record(job_ptr, ckpt_ptr->step_id);
		if (step_ptr == NULL) {
			rc = ESLURM_INVALID_JOB_ID;
			goto reply;
		} else {
			rc = checkpoint_op(ckpt_ptr->op, ckpt_ptr->data, 
				(void *)step_ptr, &resp_data.event_time, 
				&resp_data.error_code, &resp_data.error_msg);
			last_job_update = time(NULL);
		}
	}

	/* operate on all of a job's steps */
	else {
		int update_rc = -2;
		ListIterator step_iterator;

		step_iterator = list_iterator_create (job_ptr->step_list);
		while ((step_ptr = (struct step_record *) 
					list_next (step_iterator))) {
			update_rc = checkpoint_op(ckpt_ptr->op, 
						  ckpt_ptr->data,
						  (void *)step_ptr,
						  &resp_data.event_time,
						  &resp_data.error_code,
						  &resp_data.error_msg);
			rc = MAX(rc, update_rc);
		}
		if (update_rc != -2)	/* some work done */
			last_job_update = time(NULL);
		list_iterator_destroy (step_iterator);
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
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint_comp(checkpoint_comp_msg_t *ckpt_ptr,
		uid_t uid, slurm_fd conn_fd)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;
	
	slurm_msg_t_init(&resp_msg);
		
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
	if (job_ptr->job_state == JOB_PENDING) {
		rc = ESLURM_JOB_PENDING;
		goto reply;
	} else if ((job_ptr->job_state != JOB_RUNNING)
	&&         (job_ptr->job_state != JOB_SUSPENDED)) {
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
 * RET 0 on success, otherwise ESLURM error code
 */
extern int job_step_checkpoint_task_comp(checkpoint_task_comp_msg_t *ckpt_ptr,
		uid_t uid, slurm_fd conn_fd)
{
	int rc = SLURM_SUCCESS;
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	slurm_msg_t resp_msg;
	return_code_msg_t rc_msg;
	
	slurm_msg_t_init(&resp_msg);
		
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
	if (job_ptr->job_state == JOB_PENDING) {
		rc = ESLURM_JOB_PENDING;
		goto reply;
	} else if ((job_ptr->job_state != JOB_RUNNING)
	&&         (job_ptr->job_state != JOB_SUSPENDED)) {
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
 * OUT rem    - count of nodes for which responses are still pending
 * OUT max_rc - highest return code for any step thus far
 * RET 0 on success, otherwise ESLURM error code
 */
extern int step_partial_comp(step_complete_msg_t *req, int *rem, 
			     uint32_t *max_rc)
{
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	int nodes, rem_nodes;

	/* find the job, step, and validate input */
	job_ptr = find_job_record (req->job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if (job_ptr->job_state == JOB_PENDING)
		return ESLURM_JOB_PENDING;
	step_ptr = find_step_record(job_ptr, req->job_step_id);
	if (step_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;
	if (step_ptr->batch_step) {
		if(rem)
			*rem = 0;
		step_ptr->exit_code = req->step_rc;
		if (max_rc)
			*max_rc = step_ptr->exit_code;
		jobacct_gather_g_aggregate(step_ptr->jobacct, req->jobacct);
		/* we don't want to delete the step record here since
		   right after we delete this step again if we delete
		   it here we won't find it when we try the second
		   time */
		//delete_step_record(job_ptr, req->job_step_id);
		return SLURM_SUCCESS;
	}
	if (req->range_last < req->range_first) {
		error("step_partial_comp: range: %u-%u", req->range_first, 
			req->range_last);
		return EINVAL;
	}

	jobacct_gather_g_aggregate(step_ptr->jobacct, req->jobacct);

	if (step_ptr->exit_code == NO_VAL) {
		/* initialize the node bitmap for exited nodes */
		nodes = bit_set_count(step_ptr->step_node_bitmap);
		if (req->range_last >= nodes) {	/* range is zero origin */
			error("step_partial_comp: last=%u, nodes=%d",
				req->range_last, nodes);
			return EINVAL;
		}
		xassert(step_ptr->exit_node_bitmap == NULL);
		step_ptr->exit_node_bitmap = bit_alloc(nodes);
		if (step_ptr->exit_node_bitmap == NULL)
			fatal("bit_alloc: %m");
		step_ptr->exit_code = req->step_rc;
	} else {
		xassert(step_ptr->exit_node_bitmap);
		nodes = _bitstr_bits(step_ptr->exit_node_bitmap);
		if (req->range_last >= nodes) {	/* range is zero origin */
			error("step_partial_comp: last=%u, nodes=%d",
				req->range_last, nodes);
			return EINVAL;
		}
		step_ptr->exit_code = MAX(step_ptr->exit_code, req->step_rc);
	}

	bit_nset(step_ptr->exit_node_bitmap, req->range_first,
		req->range_last);
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
			switch_free_jobinfo (step_ptr->switch_job);
			step_ptr->switch_job = NULL;
		}
	} else if (switch_g_part_comp() && step_ptr->switch_job) {
		/* release switch windows on completed nodes,
		 * must translate range numbers to nodelist */
		hostlist_t hl;
		char *node_list;
		int new_size = 8096;

		hl = _step_range_to_hostlist(step_ptr,
			req->range_first, req->range_last);
		node_list = (char *) xmalloc(new_size);
		while (hostlist_ranged_string(hl, new_size,
				node_list) == -1) {
			new_size *= 2;
			xrealloc(node_list, new_size );
		}
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
	hostlist_t hl = hostlist_create("");

	for (i = 0; i < node_record_count; i++) {
		if (bit_test(step_ptr->step_node_bitmap, i) == 0)
			continue;
		node_inx++;
		if ((node_inx >= range_first)
		&&  (node_inx <= range_last)) {
			hostlist_push(hl, 
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
		_resume_job_step(job_ptr, step_ptr, now);
	}
	list_iterator_destroy (step_iterator);
}


/*
 * dump_job_step_state - dump the state of a specific job step to a buffer,
 *	load with load_step_state
 * IN step_ptr - pointer to job step for which information is to be dumpped
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
extern void dump_job_step_state(struct step_record *step_ptr, Buf buffer)
{
	pack16(step_ptr->step_id, buffer);
	pack16(step_ptr->cyclic_alloc, buffer);
	pack16(step_ptr->port, buffer);
	pack16(step_ptr->ckpt_interval, buffer);

	pack32(step_ptr->cpu_count, buffer);
	pack32(step_ptr->mem_per_task, buffer);
	pack32(step_ptr->exit_code, buffer);
	if (step_ptr->exit_code != NO_VAL) {
		pack_bit_fmt(step_ptr->exit_node_bitmap, buffer);
		pack16((uint16_t) _bitstr_bits(step_ptr->exit_node_bitmap), 
			buffer);
	}
	if (step_ptr->core_bitmap_job) {
		uint32_t core_size = bit_size(step_ptr->core_bitmap_job);
		pack32(core_size, buffer);
		pack_bit_fmt(step_ptr->core_bitmap_job, buffer);
	} else
		pack32((uint32_t) 0, buffer);

	pack_time(step_ptr->start_time, buffer);
	pack_time(step_ptr->pre_sus_time, buffer);
	pack_time(step_ptr->tot_sus_time, buffer);
	pack_time(step_ptr->ckpt_time, buffer);

	packstr(step_ptr->host,  buffer);
	packstr(step_ptr->name, buffer);
	packstr(step_ptr->network, buffer);
	packstr(step_ptr->ckpt_path, buffer);
	pack16(step_ptr->batch_step, buffer);
	if (!step_ptr->batch_step) {
		pack_slurm_step_layout(step_ptr->step_layout, buffer);
		switch_pack_jobinfo(step_ptr->switch_job, buffer);
	}
	checkpoint_pack_jobinfo(step_ptr->check_job, buffer);
}

/*
 * Create a new job step from data in a buffer (as created by 
 *	dump_job_step_state)
 * IN/OUT - job_ptr - point to a job for which the step is to be loaded.
 * IN/OUT buffer - location to get data from, pointers advanced
 */
extern int load_step_state(struct job_record *job_ptr, Buf buffer)
{
	struct step_record *step_ptr = NULL;
	uint16_t step_id, cyclic_alloc, port, batch_step, bit_cnt;
	uint16_t ckpt_interval;
	uint32_t core_size, cpu_count, exit_code, mem_per_task, name_len;
	time_t start_time, pre_sus_time, tot_sus_time, ckpt_time;
	char *host = NULL, *ckpt_path = NULL, *core_job = NULL;
	char *name = NULL, *network = NULL, *bit_fmt = NULL;
	switch_jobinfo_t switch_tmp = NULL;
	check_jobinfo_t check_tmp = NULL;
	slurm_step_layout_t *step_layout = NULL;
	
	safe_unpack16(&step_id, buffer);
	safe_unpack16(&cyclic_alloc, buffer);
	safe_unpack16(&port, buffer);
	safe_unpack16(&ckpt_interval, buffer);

	safe_unpack32(&cpu_count, buffer);
	safe_unpack32(&mem_per_task, buffer);
	safe_unpack32(&exit_code, buffer);
	if (exit_code != NO_VAL) {
		safe_unpackstr_xmalloc(&bit_fmt, &name_len, buffer);
		safe_unpack16(&bit_cnt, buffer);
	}
	safe_unpack32(&core_size, buffer);
	if (core_size)
		safe_unpackstr_xmalloc(&core_job, &name_len, buffer);

	safe_unpack_time(&start_time, buffer);
	safe_unpack_time(&pre_sus_time, buffer);
	safe_unpack_time(&tot_sus_time, buffer);
	safe_unpack_time(&ckpt_time, buffer);

	safe_unpackstr_xmalloc(&host, &name_len, buffer);
	safe_unpackstr_xmalloc(&name, &name_len, buffer);
	safe_unpackstr_xmalloc(&network, &name_len, buffer);
	safe_unpackstr_xmalloc(&ckpt_path, &name_len, buffer);
	safe_unpack16(&batch_step, buffer);
	if (!batch_step) {
		if (unpack_slurm_step_layout(&step_layout, buffer))
			goto unpack_error;
		switch_alloc_jobinfo(&switch_tmp);
        	if (switch_unpack_jobinfo(switch_tmp, buffer))
                	goto unpack_error;
	}
	checkpoint_alloc_jobinfo(&check_tmp);
        if (checkpoint_unpack_jobinfo(check_tmp, buffer))
                goto unpack_error;

	/* validity test as possible */
	if (cyclic_alloc > 1) {
		error("Invalid data for job %u.%u: cyclic_alloc=%u",
		      job_ptr->job_id, step_id, cyclic_alloc);
		goto unpack_error;
	}

	step_ptr = find_step_record(job_ptr, step_id);
	if (step_ptr == NULL)
		step_ptr = create_step_record(job_ptr);
	if (step_ptr == NULL)
		goto unpack_error;

	/* set new values */
	step_ptr->step_id      = step_id;
	step_ptr->cpu_count    = cpu_count;
	step_ptr->cyclic_alloc = cyclic_alloc;
	step_ptr->name         = name;
	step_ptr->network      = network;
	step_ptr->ckpt_path    = ckpt_path;
	step_ptr->port         = port;
	step_ptr->ckpt_interval= ckpt_interval;
	step_ptr->mem_per_task = mem_per_task;
	step_ptr->host         = host;
	step_ptr->batch_step   = batch_step;
	host                   = NULL;  /* re-used, nothing left to free */
	step_ptr->start_time   = start_time;
	step_ptr->pre_sus_time = pre_sus_time;
	step_ptr->tot_sus_time = tot_sus_time;
	step_ptr->ckpt_time    = ckpt_time;

	slurm_step_layout_destroy(step_ptr->step_layout);
	step_ptr->step_layout  = step_layout;
	
	step_ptr->switch_job   = switch_tmp;
	step_ptr->check_job    = check_tmp;

	step_ptr->exit_code    = exit_code;
	if (bit_fmt) {
		/* NOTE: This is only recovered if a job step completion
		 * is actively in progress at step save time. Otherwise
		 * the bitmap is NULL. */ 
		step_ptr->exit_node_bitmap = bit_alloc(bit_cnt);
		if (step_ptr->exit_node_bitmap == NULL)
			fatal("bit_alloc: %m");
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
	xfree(name);
	xfree(network);
	xfree(ckpt_path);
	xfree(bit_fmt);
	xfree(core_job);
	if (switch_tmp)
		switch_free_jobinfo(switch_tmp);
	slurm_step_layout_destroy(step_layout);
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
		if (job_ptr->job_state != JOB_RUNNING)
			continue;
		step_iterator = list_iterator_create (job_ptr->step_list);
		while ((step_ptr = (struct step_record *) 
				list_next (step_iterator))) {
			if (step_ptr->ckpt_interval == 0)
				continue;
			ckpt_due = step_ptr->ckpt_time +
				(step_ptr->ckpt_interval * 60);
			if (ckpt_due > now) 
				continue;
			step_ptr->ckpt_time = now;
			last_job_update = now;
			(void) checkpoint_op(CHECK_CREATE, 0, 
				(void *)step_ptr, &event_time, 
				&error_code, &error_msg);
		}
		list_iterator_destroy (step_iterator);
	}
	list_iterator_destroy(job_iterator);
}
