/*****************************************************************************\
 * job_scheduler.c - manage the scheduling of pending jobs in priority order
 *	Note there is a global job list (job_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/common/assoc_mgr.h"
#include "src/common/env.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

#define _DEBUG 0
#define MAX_RETRIES 10

static char **	_build_env(struct job_record *job_ptr);
static void	_depend_list_del(void *dep_ptr);
static void	_feature_list_delete(void *x);
static void *	_run_epilog(void *arg);
static void *	_run_prolog(void *arg);
static int	_valid_feature_list(uint32_t job_id, List feature_list);
static int	_valid_node_feature(char *feature);


/*
 * _build_user_job_list - build list of jobs for a given user
 *			  and an optional job name
 * IN  user_id - user id
 * IN  job_name - job name constraint
 * OUT job_queue - pointer to job queue
 * RET number of entries in job_queue
 * NOTE: the buffer at *job_queue must be xfreed by the caller
 */
static int _build_user_job_list(uint32_t user_id, char* job_name,
			        struct job_queue **job_queue)
{
	ListIterator job_iterator;
	struct job_record *job_ptr = NULL;
	int job_buffer_size, job_queue_size;
	struct job_queue *my_job_queue;

	/* build list pending jobs */
	job_buffer_size = job_queue_size = 0;
	job_queue[0] = my_job_queue = NULL;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		if (job_ptr->user_id != user_id)
			continue;
		if (job_name && job_ptr->name &&
		    strcmp(job_name, job_ptr->name))
			continue;
		if (job_buffer_size <= job_queue_size) {
			job_buffer_size += 200;
			xrealloc(my_job_queue, job_buffer_size *
				 sizeof(struct job_queue));
		}
		my_job_queue[job_queue_size].job_ptr = job_ptr;
		my_job_queue[job_queue_size].job_priority = job_ptr->priority;
		my_job_queue[job_queue_size].part_priority =
				job_ptr->part_ptr->priority;
		job_queue_size++;
	}
	list_iterator_destroy(job_iterator);

	job_queue[0] = my_job_queue;
	return job_queue_size;
}


/*
 * build_job_queue - build (non-priority ordered) list of pending jobs
 * OUT job_queue - pointer to job queue
 * RET number of entries in job_queue
 * NOTE: the buffer at *job_queue must be xfreed by the caller
 */
extern int build_job_queue(struct job_queue **job_queue)
{
	ListIterator job_iterator;
	struct job_record *job_ptr = NULL;
	int job_buffer_size, job_queue_size;
	struct job_queue *my_job_queue;

	if (job_list == NULL)
		return 0;

	/* build list pending jobs */
	job_buffer_size = job_queue_size = 0;
	job_queue[0] = my_job_queue = NULL;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		if ((!IS_JOB_PENDING(job_ptr))   ||
		    IS_JOB_COMPLETING(job_ptr)   ||
		    (job_ptr->priority == 0))	/* held */
			continue;
		if (!job_independent(job_ptr, 0))	/* can not run now */
			continue;
		if (job_buffer_size <= job_queue_size) {
			job_buffer_size += 200;
			xrealloc(my_job_queue, job_buffer_size *
				 sizeof(struct job_queue));
		}
		my_job_queue[job_queue_size].job_ptr  = job_ptr;
		my_job_queue[job_queue_size].job_priority = job_ptr->priority;
		my_job_queue[job_queue_size].part_priority =
						job_ptr->part_ptr->priority;
		job_queue_size++;
	}
	list_iterator_destroy(job_iterator);

	job_queue[0] = my_job_queue;
	return job_queue_size;
}

/*
 * job_is_completing - Determine if jobs are in the process of completing.
 * RET - True of any job is in the process of completing AND
 *	 CompleteWait is configured non-zero
 * NOTE: This function can reduce resource fragmentation, which is a
 * critical issue on Elan interconnect based systems.
 */
extern bool job_is_completing(void)
{
	bool completing = false;
	ListIterator job_iterator;
	struct job_record *job_ptr = NULL;
	uint16_t complete_wait = slurm_get_complete_wait();
	time_t recent;

	if ((job_list == NULL) || (complete_wait == 0))
		return completing;

	recent = time(NULL) - complete_wait;
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_COMPLETING(job_ptr) &&
		    (job_ptr->end_time >= recent)) {
			completing = true;
			break;
		}
	}
	list_iterator_destroy(job_iterator);

	return completing;
}

/*
 * set_job_elig_time - set the eligible time for pending jobs once their
 *      dependencies are lifted (in job->details->begin_time)
 */
extern void set_job_elig_time(void)
{
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr = NULL;
	ListIterator job_iterator;
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		part_ptr = job_ptr->part_ptr;
		if (!IS_JOB_PENDING(job_ptr))
			continue;
		if (part_ptr == NULL)
			continue;
		if ((job_ptr->details == NULL) || job_ptr->details->begin_time)
			continue;
		if (part_ptr->state_up == 0)
			continue;
		if ((job_ptr->time_limit != NO_VAL) &&
		    (job_ptr->time_limit > part_ptr->max_time))
			continue;
		if ((job_ptr->details->max_nodes != 0) &&
		    ((job_ptr->details->max_nodes < part_ptr->min_nodes) ||
		     (job_ptr->details->min_nodes > part_ptr->max_nodes)))
			continue;
		/* Job's eligible time is set in job_independent() */
		if (!job_independent(job_ptr, 0))
			continue;
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_write_lock);
}

/* Test of part_ptr can still run jobs or if its nodes have
 * already been reserved by higher priority jobs (those in
 * the failed_parts array) */
static bool _failed_partition(struct part_record *part_ptr,
			      struct part_record **failed_parts,
			      int failed_part_cnt)
{
	int i;

	for (i = 0; i < failed_part_cnt; i++) {
		if (failed_parts[i] == part_ptr)
			return true;
	}
	return false;
}

/*
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority
 *	order until a request fails
 * RET count of jobs scheduled
 * Note: We re-build the queue every time. Jobs can not only be added
 *	or removed from the queue, but have their priority or partition
 *	changed with the update_job RPC. In general nodes will be in priority
 *	order (by submit time), so the sorting should be pretty fast.
 */
extern int schedule(void)
{
	struct job_queue *job_queue;
	int i, error_code, failed_part_cnt = 0, job_queue_size, job_cnt = 0;
	struct job_record *job_ptr;
	struct part_record **failed_parts = NULL;
	bitstr_t *save_avail_node_bitmap;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock =
	    { READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
#ifdef HAVE_BG
	char *ionodes = NULL;
	char tmp_char[256];
#endif
	static bool backfill_sched = false;
	static bool sched_test = false;
	static bool wiki_sched = false;
	static int sched_timeout = 0;
	time_t now = time(NULL);

	DEF_TIMERS;

	if(!sched_timeout)
		sched_timeout = MIN(slurm_get_msg_timeout(), 10);

	START_TIMER;
	if (!sched_test) {
		char *sched_type = slurm_get_sched_type();
		/* On BlueGene, do FIFO only with sched/backfill */
		if (strcmp(sched_type, "sched/backfill") == 0)
			backfill_sched = true;
		/* Disable avoiding of fragmentation with sched/wiki */
		if ((strcmp(sched_type, "sched/wiki") == 0) ||
		    (strcmp(sched_type, "sched/wiki2") == 0))
			wiki_sched = true;
		xfree(sched_type);
		sched_test = true;
	}

	lock_slurmctld(job_write_lock);
	/* Avoid resource fragmentation if important */
	if ((!wiki_sched) && job_is_completing()) {
		unlock_slurmctld(job_write_lock);
		debug("schedule() returning, some job still completing");
		return SLURM_SUCCESS;
	}
	debug("Running job scheduler");
	job_queue_size = build_job_queue(&job_queue);
	if (job_queue_size == 0) {
		unlock_slurmctld(job_write_lock);
		return SLURM_SUCCESS;
	}
	sort_job_queue(job_queue, job_queue_size);

	failed_parts = xmalloc(sizeof(struct part_record *) *
			       list_count(part_list));
	save_avail_node_bitmap = bit_copy(avail_node_bitmap);

	for (i = 0; i < job_queue_size; i++) {
		job_ptr = job_queue[i].job_ptr;
		if (job_ptr->priority == 0)	/* held */
			continue;

		if ((job_ptr->resv_name == NULL) &&
		    _failed_partition(job_ptr->part_ptr, failed_parts,
				      failed_part_cnt)) {
			job_ptr->state_reason = WAIT_PRIORITY;
			xfree(job_ptr->state_desc);
			continue;
		}
		if (bit_overlap(avail_node_bitmap,
				job_ptr->part_ptr->node_bitmap) == 0) {
			/* All nodes DRAIN, DOWN, or
			 * reserved for jobs in higher priority partition */
			job_ptr->state_reason = WAIT_RESOURCES;
			continue;
		}
		if (license_job_test(job_ptr, time(NULL)) != SLURM_SUCCESS) {
			job_ptr->state_reason = WAIT_LICENSES;
			xfree(job_ptr->state_desc);
			continue;
		}

		if (assoc_mgr_validate_assoc_id(acct_db_conn,
						job_ptr->assoc_id,
						accounting_enforce)) {
			/* NOTE: This only happens if a user's account is
			 * disabled between when the job was submitted and
			 * the time we consider running it. It should be
			 * very rare. */
			info("schedule: JobId=%u has invalid account",
				job_ptr->job_id);
			last_job_update = time(NULL);
			job_ptr->job_state = JOB_FAILED;
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_BANK_ACCOUNT;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = time(NULL);
			job_completion_logger(job_ptr);
			delete_job_details(job_ptr);
			continue;
		}

		error_code = select_nodes(job_ptr, false, NULL);
		if (error_code == ESLURM_NODES_BUSY) {
			bool fail_by_part = true;
#ifdef HAVE_BG
			/* When we use static or overlap partitioning on
			 * BlueGene, each job can possibly be scheduled
			 * independently, without impacting other jobs of
			 * different sizes. Therefor we sort and try to
			 * schedule every pending job unless the backfill
			 * scheduler is configured. */
			if (!backfill_sched)
				fail_by_part = false;
#endif
			if (fail_by_part) {
		 		/* do not schedule more jobs in this partition
				 * or on nodes in this partition */
				failed_parts[failed_part_cnt++] =
						job_ptr->part_ptr;
				bit_not(job_ptr->part_ptr->node_bitmap);
				bit_and(avail_node_bitmap,
					job_ptr->part_ptr->node_bitmap);
				bit_not(job_ptr->part_ptr->node_bitmap);
			}
		} else if (error_code == ESLURM_RESERVATION_NOT_USABLE) {
			if (job_ptr->resv_ptr
			    && job_ptr->resv_ptr->node_bitmap) {
				bit_not(job_ptr->resv_ptr->node_bitmap);
				bit_and(avail_node_bitmap,
					job_ptr->resv_ptr->node_bitmap);
				bit_not(job_ptr->resv_ptr->node_bitmap);
			} else {
				/* The job has no reservation but requires
				 * nodes that are currently in some reservation
				 * so just skip over this job and try running
				 * the next lower priority job */
			}
		} else if (error_code == SLURM_SUCCESS) {
			/* job initiated */
			last_job_update = now;
#ifdef HAVE_BG
			select_g_select_jobinfo_get(job_ptr->select_jobinfo,
						    SELECT_JOBDATA_IONODES,
						    &ionodes);
			if(ionodes) {
				sprintf(tmp_char,"%s[%s]",
					job_ptr->nodes, ionodes);
			} else {
				sprintf(tmp_char,"%s",job_ptr->nodes);
			}
			info("schedule: JobId=%u BPList=%s",
			     job_ptr->job_id, tmp_char);
			xfree(ionodes);
#else
			info("schedule: JobId=%u NodeList=%s",
			     job_ptr->job_id, job_ptr->nodes);
#endif
			if (job_ptr->batch_flag == 0)
				srun_allocate(job_ptr->job_id);
			else if (job_ptr->details->prolog_running == 0)
				launch_job(job_ptr);
			job_cnt++;
		} else if ((error_code != ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
			   (error_code != ESLURM_NODE_NOT_AVAIL) &&
			   (error_code != ESLURM_ACCOUNTING_POLICY)) {
			info("schedule: JobId=%u non-runnable: %s",
				job_ptr->job_id,
				slurm_strerror(error_code));
			if (!wiki_sched) {
				last_job_update = now;
				job_ptr->job_state = JOB_FAILED;
				job_ptr->exit_code = 1;
				job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
				xfree(job_ptr->state_desc);
				job_ptr->start_time = job_ptr->end_time = now;
				job_completion_logger(job_ptr);
				delete_job_details(job_ptr);
			}
		}

		if ((time(NULL) - now) >= sched_timeout) {
			debug("schedule: loop taking to long breaking out");
			break;
		}
	}

	bit_free(avail_node_bitmap);
	avail_node_bitmap = save_avail_node_bitmap;
	xfree(failed_parts);
	xfree(job_queue);
	unlock_slurmctld(job_write_lock);
	END_TIMER2("schedule");
	return job_cnt;
}


/*
 * sort_job_queue - sort job_queue in decending priority order
 * IN job_queue_size - count of elements in the job queue
 * IN/OUT job_queue - pointer to sorted job queue
 */
extern void sort_job_queue(struct job_queue *job_queue, int job_queue_size)
{
	int i, j, top_prio_inx;
	struct job_record *tmp_job_ptr;
	uint32_t top_job_prio,  tmp_job_prio;
	uint16_t top_part_prio, tmp_part_prio;

	for (i = 0; i < job_queue_size; i++) {
		top_prio_inx  = i;
		top_job_prio  = job_queue[i].job_priority;
		top_part_prio = job_queue[i].part_priority;

		for (j = (i + 1); j < job_queue_size; j++) {
			if (top_part_prio > job_queue[j].part_priority)
				continue;
			if ((top_part_prio == job_queue[j].part_priority) &&
			    (top_job_prio  >= job_queue[j].job_priority))
				continue;

			top_prio_inx  = j;
			top_job_prio  = job_queue[j].job_priority;
			top_part_prio = job_queue[j].part_priority;
		}
		if (top_prio_inx == i)
			continue;	/* in correct order */

		/* swap records at top_prio_inx and i */
		tmp_job_ptr   = job_queue[i].job_ptr;
		tmp_job_prio  = job_queue[i].job_priority;
		tmp_part_prio = job_queue[i].part_priority;

		job_queue[i].job_ptr       = job_queue[top_prio_inx].job_ptr;
		job_queue[i].job_priority  = job_queue[top_prio_inx].
					     job_priority;


		job_queue[top_prio_inx].job_ptr       = tmp_job_ptr;
		job_queue[top_prio_inx].job_priority  = tmp_job_prio;
		job_queue[top_prio_inx].part_priority = tmp_part_prio;

	}
}

/*
 * launch_job - send an RPC to a slurmd to initiate a batch job
 * IN job_ptr - pointer to job that will be initiated
 */
extern void launch_job(struct job_record *job_ptr)
{
	batch_job_launch_msg_t *launch_msg_ptr;
	agent_arg_t *agent_arg_ptr;
	struct node_record *node_ptr;

	node_ptr = find_first_node_record(job_ptr->node_bitmap);
	if (node_ptr == NULL)
		return;

	/* Initialization of data structures */
	launch_msg_ptr = (batch_job_launch_msg_t *)
				xmalloc(sizeof(batch_job_launch_msg_t));
	launch_msg_ptr->job_id = job_ptr->job_id;
	launch_msg_ptr->step_id = NO_VAL;
	launch_msg_ptr->uid = job_ptr->user_id;
	launch_msg_ptr->gid = job_ptr->group_id;
	launch_msg_ptr->nprocs = job_ptr->details->num_tasks;
	launch_msg_ptr->nodes = xstrdup(job_ptr->nodes);
	launch_msg_ptr->overcommit = job_ptr->details->overcommit;
	launch_msg_ptr->open_mode  = job_ptr->details->open_mode;
	launch_msg_ptr->acctg_freq = job_ptr->details->acctg_freq;
	launch_msg_ptr->cpus_per_task = job_ptr->details->cpus_per_task;
	launch_msg_ptr->restart_cnt   = job_ptr->restart_cnt;

	if (make_batch_job_cred(launch_msg_ptr, job_ptr)) {
		error("aborting batch job %u", job_ptr->job_id);
		/* FIXME: This is a kludge, but this event indicates a serious
		 * problem with OpenSSH and should never happen. We are
		 * too deep into the job launch to gracefully clean up. */
		job_ptr->end_time    = time(NULL);
		job_ptr->time_limit = 0;
		xfree(launch_msg_ptr->nodes);
		xfree(launch_msg_ptr);
		return;
	}

	launch_msg_ptr->std_err = xstrdup(job_ptr->details->std_err);
	launch_msg_ptr->std_in = xstrdup(job_ptr->details->std_in);
	launch_msg_ptr->std_out = xstrdup(job_ptr->details->std_out);
	launch_msg_ptr->work_dir = xstrdup(job_ptr->details->work_dir);
	launch_msg_ptr->ckpt_dir = xstrdup(job_ptr->details->ckpt_dir);
	launch_msg_ptr->restart_dir = xstrdup(job_ptr->details->restart_dir);
	launch_msg_ptr->argc = job_ptr->details->argc;
	launch_msg_ptr->argv = xduparray(job_ptr->details->argc,
					 job_ptr->details->argv);
	launch_msg_ptr->spank_job_env_size = job_ptr->spank_job_env_size;
	launch_msg_ptr->spank_job_env = xduparray(job_ptr->spank_job_env_size,
						  job_ptr->spank_job_env);
	launch_msg_ptr->script = get_job_script(job_ptr);
	launch_msg_ptr->environment = get_job_env(job_ptr,
						  &launch_msg_ptr->envc);
	launch_msg_ptr->job_mem = job_ptr->details->job_min_memory;
	launch_msg_ptr->num_cpu_groups = job_ptr->job_resrcs->cpu_array_cnt;
	launch_msg_ptr->cpus_per_node  = xmalloc(sizeof(uint16_t) *
			job_ptr->job_resrcs->cpu_array_cnt);
	memcpy(launch_msg_ptr->cpus_per_node,
	       job_ptr->job_resrcs->cpu_array_value,
	       (sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt));
	launch_msg_ptr->cpu_count_reps  = xmalloc(sizeof(uint32_t) *
			job_ptr->job_resrcs->cpu_array_cnt);
	memcpy(launch_msg_ptr->cpu_count_reps,
	       job_ptr->job_resrcs->cpu_array_reps,
	       (sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt));

	launch_msg_ptr->select_jobinfo = select_g_select_jobinfo_copy(
			job_ptr->select_jobinfo);

	agent_arg_ptr = (agent_arg_t *) xmalloc(sizeof(agent_arg_t));
	agent_arg_ptr->node_count = 1;
	agent_arg_ptr->retry = 0;
	agent_arg_ptr->hostlist = hostlist_create(node_ptr->name);
	agent_arg_ptr->msg_type = REQUEST_BATCH_JOB_LAUNCH;
	agent_arg_ptr->msg_args = (void *) launch_msg_ptr;

	/* Launch the RPC via agent */
	agent_queue_request(agent_arg_ptr);
}

/*
 * make_batch_job_cred - add a job credential to the batch_job_launch_msg
 * IN/OUT launch_msg_ptr - batch_job_launch_msg in which job_id, step_id,
 *                         uid and nodes have already been set
 * IN job_ptr - pointer to job record
 * RET 0 or error code
 */
extern int make_batch_job_cred(batch_job_launch_msg_t *launch_msg_ptr,
			       struct job_record *job_ptr)
{
	slurm_cred_arg_t cred_arg;
	job_resources_t *job_resrcs_ptr;

	memset(&cred_arg, 0, sizeof(slurm_cred_arg_t));

	cred_arg.jobid     = launch_msg_ptr->job_id;
	cred_arg.stepid    = launch_msg_ptr->step_id;
	cred_arg.uid       = launch_msg_ptr->uid;
#ifdef HAVE_FRONT_END
	cred_arg.hostlist  = node_record_table_ptr[0].name;
#else
	cred_arg.hostlist  = launch_msg_ptr->nodes;
#endif
	if (job_ptr->details == NULL)
		cred_arg.job_mem = 0;
	else if (job_ptr->details->job_min_memory & MEM_PER_CPU) {
		xassert(job_ptr->job_resrcs);
		xassert(job_ptr->job_resrcs->cpus);
		cred_arg.job_mem = job_ptr->details->job_min_memory;
		cred_arg.job_mem &= (~MEM_PER_CPU);
		cred_arg.job_mem *= job_ptr->job_resrcs->cpus[0];
	} else
		cred_arg.job_mem = job_ptr->details->job_min_memory;

	/* Identify the cores allocated to this job. */
	xassert(job_ptr->job_resrcs);
	job_resrcs_ptr = job_ptr->job_resrcs;
	cred_arg.core_bitmap         = job_resrcs_ptr->core_bitmap;
	cred_arg.cores_per_socket    = job_resrcs_ptr->cores_per_socket;
	cred_arg.sockets_per_node    = job_resrcs_ptr->sockets_per_node;
	cred_arg.sock_core_rep_count = job_resrcs_ptr->sock_core_rep_count;
	cred_arg.job_nhosts          = job_resrcs_ptr->nhosts;
	cred_arg.job_hostlist        = job_ptr->nodes;

	launch_msg_ptr->cred = slurm_cred_create(slurmctld_config.cred_ctx,
			 &cred_arg);

	if (launch_msg_ptr->cred)
		return SLURM_SUCCESS;
	error("slurm_cred_create failure for batch job %u", cred_arg.jobid);
	return SLURM_ERROR;
}

static void _depend_list_del(void *dep_ptr)
{
	xfree(dep_ptr);
}

/* Print a job's dependency information based upon job_ptr->depend_list */
extern void print_job_dependency(struct job_record *job_ptr)
{
	ListIterator depend_iter;
	struct depend_spec *dep_ptr;
	char *dep_str;

	info("Dependency information for job %u", job_ptr->job_id);
	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->depend_list == NULL))
		return;

	depend_iter = list_iterator_create(job_ptr->details->depend_list);
	if (!depend_iter)
		fatal("list_iterator_create memory allocation failure");
	while ((dep_ptr = list_next(depend_iter))) {
		if      (dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) {
			info("  singleton");
			continue;
		}
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER)
			dep_str = "after";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_ANY)
			dep_str = "afterany";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_NOT_OK)
			dep_str = "afternotok";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_OK)
			dep_str = "afterok";
		else
			dep_str = "unknown";
		info("  %s:%u", dep_str, dep_ptr->job_id);
	}
	list_iterator_destroy(depend_iter);
}

/*
 * Determine if a job's dependencies are met
 * RET: 0 = no dependencies
 *      1 = dependencies remain
 *      2 = failure (job completion code not per dependency), delete the job
 */
extern int test_job_dependency(struct job_record *job_ptr)
{
	ListIterator depend_iter;
	struct depend_spec *dep_ptr;
	bool failure = false;
 	struct job_queue *job_queue = NULL;
 	int i, now, job_queue_size = 0;
 	struct job_record *qjob_ptr;

	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->depend_list == NULL))
		return 0;

	depend_iter = list_iterator_create(job_ptr->details->depend_list);
	if (!depend_iter)
		fatal("list_iterator_create memory allocation failure");
	while ((dep_ptr = list_next(depend_iter))) {
 	        if ((dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) &&
 		    job_ptr->name) {
 		        /* get user jobs with the same user and name */
 			job_queue_size = _build_user_job_list(job_ptr->user_id,
							      job_ptr->name,
							      &job_queue);
 			now = 1;
 			for (i=0; i<job_queue_size; i++) {
				qjob_ptr = job_queue[i].job_ptr;
				/* already running/suspended job or previously
				 * submitted pending job */
				if (IS_JOB_RUNNING(qjob_ptr) ||
				    IS_JOB_SUSPENDED(qjob_ptr) ||
				    (IS_JOB_PENDING(qjob_ptr) &&
				     (qjob_ptr->job_id < job_ptr->job_id))) {
					now = 0;
					break;
 				}
 			}
 			if (job_queue_size > 0)
				xfree(job_queue);
			/* job can run now, delete dependency */
 			if (now)
 				list_delete_item(depend_iter);
 			else
				break;
 		} else if (dep_ptr->job_ptr->job_id != dep_ptr->job_id) {
			/* job is gone, dependency lifted */
			list_delete_item(depend_iter);
		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER) {
			if (!IS_JOB_PENDING(dep_ptr->job_ptr))
				list_delete_item(depend_iter);
			else
				break;
		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_ANY) {
			if (IS_JOB_FINISHED(dep_ptr->job_ptr))
				list_delete_item(depend_iter);
			else
				break;
		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_NOT_OK) {
			if (!IS_JOB_FINISHED(dep_ptr->job_ptr))
				break;
			if (!IS_JOB_COMPLETE(dep_ptr->job_ptr))
				list_delete_item(depend_iter);
			else {
				failure = true;
				break;
			}
		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_OK) {
			if (!IS_JOB_FINISHED(dep_ptr->job_ptr))
				break;
			if (IS_JOB_COMPLETE(dep_ptr->job_ptr))
				list_delete_item(depend_iter);
			else {
				failure = true;
				break;
			}
		} else
			failure = true;
	}
	list_iterator_destroy(depend_iter);

	if (failure)
		return 2;
	if (dep_ptr)
		return 1;
	return 0;
}

/*
 * Parse a job dependency string and use it to establish a "depend_spec"
 * list of dependencies. We accept both old format (a single job ID) and
 * new format (e.g. "afterok:123:124,after:128").
 * IN job_ptr - job record to have dependency and depend_list updated
 * IN new_depend - new dependency description
 * RET returns an error code from slurm_errno.h
 */
extern int update_job_dependency(struct job_record *job_ptr, char *new_depend)
{
	int rc = SLURM_SUCCESS;
	uint16_t depend_type = 0;
	uint32_t job_id = 0;
	char *tok = new_depend, *sep_ptr, *sep_ptr2;
	List new_depend_list = NULL;
	struct depend_spec *dep_ptr;
	struct job_record *dep_job_ptr;
	char dep_buf[32];

	if (job_ptr->details == NULL)
		return EINVAL;

	/* Clear dependencies on NULL or empty dependency input */
	if ((new_depend == NULL) || (new_depend[0] == '\0')) {
		xfree(job_ptr->details->dependency);
		if (job_ptr->details->depend_list) {
			list_destroy(job_ptr->details->depend_list);
			job_ptr->details->depend_list = NULL;
		}
		return rc;

	}

	new_depend_list = list_create(_depend_list_del);
	/* validate new dependency string */
	while (rc == SLURM_SUCCESS) {

 	        /* test singleton dependency flag */
 	        if ( strncasecmp(tok, "singleton", 9) == 0 ) {
			depend_type = SLURM_DEPEND_SINGLETON;
			dep_ptr = xmalloc(sizeof(struct depend_spec));
			dep_ptr->depend_type = depend_type;
			/* dep_ptr->job_id = 0;		set by xmalloc */
			/* dep_ptr->job_ptr = NULL;	set by xmalloc */
			if (!list_append(new_depend_list, dep_ptr)) {
				fatal("list_append memory allocation "
				      "failure for singleton");
			}
			if ( *(tok + 9 ) == ',' ) {
				tok+=10;
				continue;
			}
			else
				break;
 		}

		sep_ptr = strchr(tok, ':');
		if ((sep_ptr == NULL) && (job_id == 0)) {
			job_id = strtol(tok, &sep_ptr, 10);
			if ((sep_ptr == NULL) || (sep_ptr[0] != '\0') ||
			    (job_id < 0) || (job_id == job_ptr->job_id)) {
				rc = EINVAL;
				break;
			}
			/* old format, just a single job_id */
			dep_job_ptr = find_job_record(job_id);
			if (!dep_job_ptr)	/* assume already done */
				break;
			snprintf(dep_buf, sizeof(dep_buf),
				 "afterany:%u", job_id);
			new_depend = dep_buf;
			dep_ptr = xmalloc(sizeof(struct depend_spec));
			dep_ptr->depend_type = SLURM_DEPEND_AFTER_ANY;
			dep_ptr->job_id = job_id;
			dep_ptr->job_ptr = dep_job_ptr;
			if (!list_append(new_depend_list, dep_ptr))
				fatal("list_append memory allocation failure");
			break;
		}

		if      (strncasecmp(tok, "afternotok", 10) == 0)
			depend_type = SLURM_DEPEND_AFTER_NOT_OK;
		else if (strncasecmp(tok, "afterany", 8) == 0)
			depend_type = SLURM_DEPEND_AFTER_ANY;
		else if (strncasecmp(tok, "afterok", 7) == 0)
			depend_type = SLURM_DEPEND_AFTER_OK;
		else if (strncasecmp(tok, "after", 5) == 0)
			depend_type = SLURM_DEPEND_AFTER;
		else {
			rc = EINVAL;
			break;
		}
		sep_ptr++;	/* skip over ":" */
		while (rc == SLURM_SUCCESS) {
			job_id = strtol(sep_ptr, &sep_ptr2, 10);
			if ((sep_ptr2 == NULL) ||
			    (job_id < 0) || (job_id == job_ptr->job_id) ||
			    ((sep_ptr2[0] != '\0') && (sep_ptr2[0] != ',') &&
			     (sep_ptr2[0] != ':'))) {
				rc = EINVAL;
				break;
			}
			dep_job_ptr = find_job_record(job_id);
			if (dep_job_ptr) {	/* job still active */
				dep_ptr = xmalloc(sizeof(struct depend_spec));
				dep_ptr->depend_type = depend_type;
				dep_ptr->job_id = job_id;
				dep_ptr->job_ptr = dep_job_ptr;
				if (!list_append(new_depend_list, dep_ptr)) {
					fatal("list_append memory allocation "
						"failure");
				}
			}
			if (sep_ptr2[0] != ':')
				break;
			sep_ptr = sep_ptr2 + 1;	/* skip over ":" */
		}
		if (sep_ptr2[0] == ',')
			tok = sep_ptr2 + 1;
		else
			break;
	}

	if (rc == SLURM_SUCCESS) {
		xfree(job_ptr->details->dependency);
		job_ptr->details->dependency = xstrdup(new_depend);
		if (job_ptr->details->depend_list)
			list_destroy(job_ptr->details->depend_list);
		job_ptr->details->depend_list = new_depend_list;
#if _DEBUG
		print_job_dependency(job_ptr);
#endif
	} else {
		list_destroy(new_depend_list);
	}
	return rc;
}

static void _pre_list_del(void *x)
{
	xfree(x);
}

/* Determine if a pending job will run using only the specified nodes
 * (in job_desc_msg->req_nodes), build response message and return
 * SLURM_SUCCESS on success. Otherwise return an error code. Caller
 * must free response message */
extern int job_start_data(job_desc_msg_t *job_desc_msg,
			  will_run_response_msg_t **resp)
{
	struct job_record *job_ptr;
	struct part_record *part_ptr;
	bitstr_t *avail_bitmap = NULL, *resv_bitmap = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	int i, rc = SLURM_SUCCESS;
	time_t now = time(NULL), start_res;
	List preemptee_candidates = NULL, preemptee_job_list = NULL;

	job_ptr = find_job_record(job_desc_msg->job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;

	part_ptr = job_ptr->part_ptr;
	if (part_ptr == NULL)
		return ESLURM_INVALID_PARTITION_NAME;

	if ((job_ptr->details == NULL) || (!IS_JOB_PENDING(job_ptr)))
		return ESLURM_DISABLED;

	if ((job_desc_msg->req_nodes == NULL) ||
	    (job_desc_msg->req_nodes == '\0')) {
		/* assume all nodes available to job for testing */
		avail_bitmap = bit_alloc(node_record_count);
		bit_nset(avail_bitmap, 0, (node_record_count - 1));
	} else if (node_name2bitmap(job_desc_msg->req_nodes, false,
				    &avail_bitmap) != 0) {
		return ESLURM_INVALID_NODE_NAME;
	}

	/* Consider only nodes in this job's partition */
	if (part_ptr->node_bitmap)
		bit_and(avail_bitmap, part_ptr->node_bitmap);
	else
		rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (job_req_node_filter(job_ptr, avail_bitmap))
		rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (job_ptr->details->exc_node_bitmap) {
		bitstr_t *exc_node_mask = NULL;
		exc_node_mask = bit_copy(job_ptr->details->exc_node_bitmap);
		if (exc_node_mask == NULL)
			fatal("bit_copy malloc failure");
		bit_not(exc_node_mask);
		bit_and(avail_bitmap, exc_node_mask);
		FREE_NULL_BITMAP(exc_node_mask);
	}
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_bitmap)) {
			rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}
	}

	/* Enforce reservation: access control, time and nodes */
	if (job_ptr->details->begin_time)
		start_res = job_ptr->details->begin_time;
	else
		start_res = now;
	i = job_test_resv(job_ptr, &start_res, false, &resv_bitmap);
	if (i != SLURM_SUCCESS)
		return i;
	bit_and(avail_bitmap, resv_bitmap);
	FREE_NULL_BITMAP(resv_bitmap);

	/* Only consider nodes that are not DOWN or DRAINED */
	bit_and(avail_bitmap, avail_node_bitmap);

	if (rc == SLURM_SUCCESS) {
		min_nodes = MAX(job_ptr->details->min_nodes,
				part_ptr->min_nodes);
		if (job_ptr->details->max_nodes == 0)
			max_nodes = part_ptr->max_nodes;
		else
			max_nodes = MIN(job_ptr->details->max_nodes,
					part_ptr->max_nodes);
		max_nodes = MIN(max_nodes, 500000);	/* prevent overflows */
		if (!job_ptr->limit_set_max_nodes &&
		    job_ptr->details->max_nodes)
			req_nodes = max_nodes;
		else
			req_nodes = min_nodes;
		preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);
		rc = select_g_job_test(job_ptr, avail_bitmap,
				min_nodes, max_nodes, req_nodes,
				SELECT_MODE_WILL_RUN, preemptee_candidates,
				&preemptee_job_list);
	}

	if (rc == SLURM_SUCCESS) {
		will_run_response_msg_t *resp_data;
		resp_data = xmalloc(sizeof(will_run_response_msg_t));
		resp_data->job_id     = job_ptr->job_id;
#ifdef HAVE_BG
		select_g_select_jobinfo_get(job_ptr->select_jobinfo,
                        	     SELECT_JOBDATA_NODE_CNT,
				     &resp_data->proc_cnt);

#else
		resp_data->proc_cnt = job_ptr->total_procs;
#endif
		resp_data->start_time = MAX(job_ptr->start_time, start_res);
		job_ptr->start_time   = 0;  /* restore pending job start time */
		resp_data->node_list  = bitmap2node_name(avail_bitmap);

		if (preemptee_job_list) {
			ListIterator preemptee_iterator;
			uint32_t *preemptee_jid;
			struct job_record *tmp_job_ptr;
			resp_data->preemptee_job_id=list_create(_pre_list_del);
			if (resp_data->preemptee_job_id == NULL)
				fatal("list_create: malloc failure");
			preemptee_iterator = list_iterator_create(
							preemptee_job_list);
			while ((tmp_job_ptr = (struct job_record *)
					list_next(preemptee_iterator))) {
				preemptee_jid = xmalloc(sizeof(uint32_t));
				(*preemptee_jid) = tmp_job_ptr->job_id;
				list_append(resp_data->preemptee_job_id,
					    preemptee_jid);
			}
			list_iterator_destroy(preemptee_iterator);
		}
		*resp = resp_data;
	} else {
		rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	if (preemptee_candidates)
		list_destroy(preemptee_candidates);
	if (preemptee_job_list)
		list_destroy(preemptee_job_list);
	FREE_NULL_BITMAP(avail_bitmap);
	return rc;
}

/*
 * epilog_slurmctld - execute the epilog_slurmctld for a job that has just
 *	terminated.
 * IN job_ptr - pointer to job that has been terminated
 * RET SLURM_SUCCESS(0) or error code
 */
extern int epilog_slurmctld(struct job_record *job_ptr)
{
	int rc;
	pthread_t thread_id_epilog;
	pthread_attr_t thread_attr_epilog;

	if ((slurmctld_conf.epilog_slurmctld == NULL) ||
	    (slurmctld_conf.epilog_slurmctld[0] == '\0'))
		return SLURM_SUCCESS;

	if (access(slurmctld_conf.epilog_slurmctld, X_OK) < 0) {
		error("Invalid EpilogSlurmctld: %m");
		return errno;
	}

	slurm_attr_init(&thread_attr_epilog);
	pthread_attr_setdetachstate(&thread_attr_epilog,
				    PTHREAD_CREATE_DETACHED);
	while(1) {
		rc = pthread_create(&thread_id_epilog,
				    &thread_attr_epilog,
				    _run_epilog, (void *) job_ptr);
		if (rc == 0)
			return SLURM_SUCCESS;
		if (errno == EAGAIN)
			continue;
		error("pthread_create: %m");
		return errno;
	}
}

static char **_build_env(struct job_record *job_ptr)
{
	char **my_env, *name;

	my_env = xmalloc(sizeof(char *));
	my_env[0] = NULL;

	/* Set SPANK env vars first so that we can overrite as needed
	 * below. Prevent user hacking from setting SLURM_JOB_ID etc. */
	if (job_ptr->spank_job_env_size) {
		env_array_merge(&my_env,
				(const char **) job_ptr->spank_job_env);
	}

#ifdef HAVE_CRAY_XT
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
			     SELECT_JOBDATA_RESV_ID, &name);
	setenvf(&env, "BASIL_RESERVATION_ID", "%s", name);
	xfree(name);
#endif
#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
			     SELECT_JOBDATA_BLOCK_ID, &name);
	setenvf(&my_env, "MPIRUN_PARTITION", "%s", name);
#endif
	setenvf(&my_env, "SLURM_JOB_ACCOUNT", "%s", job_ptr->account);
	if (job_ptr->details) {
		setenvf(&my_env, "SLURM_JOB_CONSTRAINTS",
			"%s", job_ptr->details->features);
	}
	setenvf(&my_env, "SLURM_JOB_GID", "%u", job_ptr->group_id);
	name = gid_to_string((uid_t) job_ptr->group_id);
	setenvf(&my_env, "SLURM_JOB_GROUP", "%s", name);
	xfree(name);
	setenvf(&my_env, "SLURM_JOB_ID", "%u", job_ptr->job_id);
	setenvf(&my_env, "SLURM_JOB_NAME", "%s", job_ptr->name);
	setenvf(&my_env, "SLURM_JOB_NODELIST", "%s", job_ptr->nodes);
	setenvf(&my_env, "SLURM_JOB_PARTITION", "%s", job_ptr->partition);
	setenvf(&my_env, "SLURM_JOB_UID", "%u", job_ptr->user_id);
	name = uid_to_string((uid_t) job_ptr->user_id);
	setenvf(&my_env, "SLURM_JOB_USER", "%s", name);
	xfree(name);

	return my_env;
}

static void *_run_epilog(void *arg)
{
	struct job_record *job_ptr = (struct job_record *) arg;
	uint32_t job_id;
	pid_t cpid;
	int i, status, wait_rc;
	char *argv[2], **my_env;
	/* Locks: Read config, job */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

	lock_slurmctld(config_read_lock);
	argv[0] = xstrdup(slurmctld_conf.epilog_slurmctld);
	argv[1] = NULL;
	my_env = _build_env(job_ptr);
	job_id = job_ptr->job_id;
	unlock_slurmctld(config_read_lock);

	if ((cpid = fork()) < 0) {
		error("epilog_slurmctld fork error: %m");
		goto fini;
	}
	if (cpid == 0) {
#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execve(argv[0], argv, my_env);
		exit(127);
	}

	while (1) {
		wait_rc = waitpid(cpid, &status, 0);
		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("epilog_slurmctld waitpid error: %m");
			break;
		} else if (wait_rc > 0) {
			killpg(cpid, SIGKILL);	/* kill children too */
			break;
		}
	}
	if (status != 0) {
		error("epilog_slurmctld job %u epilog exit status %u:%u",
		      job_id, WEXITSTATUS(status), WTERMSIG(status));
	} else
		debug2("epilog_slurmctld job %u prolog completed", job_id);

 fini:	xfree(argv[0]);
	for (i=0; my_env[i]; i++)
		xfree(my_env[i]);
	xfree(my_env);
	return NULL;
}

/*
 * prolog_slurmctld - execute the prolog_slurmctld for a job that has just
 *	been allocated resources.
 * IN job_ptr - pointer to job that will be initiated
 * RET SLURM_SUCCESS(0) or error code
 */
extern int prolog_slurmctld(struct job_record *job_ptr)
{
	int rc;
	pthread_t thread_id_prolog;
	pthread_attr_t thread_attr_prolog;

	if ((slurmctld_conf.prolog_slurmctld == NULL) ||
	    (slurmctld_conf.prolog_slurmctld[0] == '\0'))
		return SLURM_SUCCESS;

	if (access(slurmctld_conf.prolog_slurmctld, X_OK) < 0) {
		error("Invalid PrologSlurmctld: %m");
		return errno;
	}

	if (job_ptr->details)
		job_ptr->details->prolog_running = 1;

	slurm_attr_init(&thread_attr_prolog);
	pthread_attr_setdetachstate(&thread_attr_prolog,
				    PTHREAD_CREATE_DETACHED);
	while(1) {
		rc = pthread_create(&thread_id_prolog,
				    &thread_attr_prolog,
				    _run_prolog, (void *) job_ptr);
		if (rc == 0)
			return SLURM_SUCCESS;
		if (errno == EAGAIN)
			continue;
		error("pthread_create: %m");
		return errno;
	}
}

static void *_run_prolog(void *arg)
{
	struct job_record *job_ptr = (struct job_record *) arg;
	uint32_t job_id;
	pid_t cpid;
	int i, rc, status, wait_rc;
	char *argv[2], **my_env;
	/* Locks: Read config, job; Write nodes */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, READ_LOCK, WRITE_LOCK, NO_LOCK };
	bitstr_t *node_bitmap = NULL;
	static int last_job_requeue = 0;

	lock_slurmctld(config_read_lock);
	argv[0] = xstrdup(slurmctld_conf.prolog_slurmctld);
	argv[1] = NULL;
	my_env = _build_env(job_ptr);
	job_id = job_ptr->job_id;
	if (job_ptr->node_bitmap) {
		node_bitmap = bit_copy(job_ptr->node_bitmap);
		for (i=0; i<node_record_count; i++) {
			if (bit_test(node_bitmap, i) == 0)
				continue;
			node_record_table_ptr[i].node_state |=
				NODE_STATE_POWER_UP;
		}
	}
	unlock_slurmctld(config_read_lock);

	if ((cpid = fork()) < 0) {
		error("prolog_slurmctld fork error: %m");
		goto fini;
	}
	if (cpid == 0) {
#ifdef SETPGRP_TWO_ARGS
		setpgrp(0, 0);
#else
		setpgrp();
#endif
		execve(argv[0], argv, my_env);
		exit(127);
	}

	while (1) {
		wait_rc = waitpid(cpid, &status, 0);
		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("prolog_slurmctld waitpid error: %m");
			break;
		} else if (wait_rc > 0) {
			killpg(cpid, SIGKILL);	/* kill children too */
			break;
		}
	}
	if (status != 0) {
		bool kill_job = false;
		slurmctld_lock_t job_write_lock = {
			NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK };
		error("prolog_slurmctld job %u prolog exit status %u:%u",
		      job_id, WEXITSTATUS(status), WTERMSIG(status));
		lock_slurmctld(job_write_lock);
		if (last_job_requeue == job_id) {
			info("prolog_slurmctld failed again for job %u");
			kill_job = true;
		} else if ((rc = job_requeue(0, job_id, -1))) {
			info("unable to requeue job %u: %m");
			kill_job = true;
		} else
			last_job_requeue = job_id;
		if (kill_job)
			(void) job_signal(job_id, SIGKILL, 0, 0);

		unlock_slurmctld(job_write_lock);
	} else
		debug2("prolog_slurmctld job %u prolog completed", job_id);

 fini:	xfree(argv[0]);
	for (i=0; my_env[i]; i++)
		xfree(my_env[i]);
	xfree(my_env);
	lock_slurmctld(config_read_lock);
	if (job_ptr->job_id != job_id) {
		error("prolog_slurmctld job %u pointer invalid", job_id);
		job_ptr = find_job_record(job_id);
		if (job_ptr == NULL)
			error("prolog_slurmctld job %u now defunct", job_id);
	}
	if (job_ptr) {
		if (job_ptr->details)
			job_ptr->details->prolog_running = 0;
		if (job_ptr->batch_flag &&
		    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)))
			launch_job(job_ptr);
		for (i=0; i<node_record_count; i++) {
			if (bit_test(job_ptr->node_bitmap, i) == 0)
				continue;
			node_record_table_ptr[i].node_state &=
				(~NODE_STATE_POWER_UP);
		}
	} else if (node_bitmap) {
		for (i=0; i<node_record_count; i++) {
			if (bit_test(node_bitmap, i) == 0)
				continue;
			node_record_table_ptr[i].node_state &=
				(~NODE_STATE_POWER_UP);
		}
	}
	unlock_slurmctld(config_read_lock);
	FREE_NULL_BITMAP(node_bitmap);

	return NULL;
}

/*
 * build_feature_list - Translate a job's feature string into a feature_list
 * IN  details->features
 * OUT details->feature_list
 * RET error code
 */
extern int build_feature_list(struct job_record *job_ptr)
{
	struct job_details *detail_ptr = job_ptr->details;
	char *tmp_requested, *str_ptr1, *str_ptr2, *feature = NULL;
	int bracket = 0, count = 0, i;
	bool have_count = false, have_or = false;
	struct feature_record *feat;

	if (detail_ptr->features == NULL)	/* no constraints */
		return SLURM_SUCCESS;
	if (detail_ptr->feature_list)		/* already processed */
		return SLURM_SUCCESS;

	tmp_requested = xstrdup(detail_ptr->features);
	str_ptr1 = tmp_requested;
	detail_ptr->feature_list = list_create(_feature_list_delete);
	for (i=0; ; i++) {
		if (tmp_requested[i] == '*') {
			tmp_requested[i] = '\0';
			have_count = true;
			count = strtol(&tmp_requested[i+1], &str_ptr2, 10);
			if ((feature == NULL) || (count <= 0)) {
				info("Job %u invalid constraint %s",
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_INVALID_FEATURE;
			}
			i = str_ptr2 - tmp_requested - 1;
		} else if (tmp_requested[i] == '&') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (bracket != 0)) {
				info("Job %u invalid constraint %s",
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_INVALID_FEATURE;
			}
			feat = xmalloc(sizeof(struct feature_record));
			feat->name = xstrdup(feature);
			feat->count = count;
			feat->op_code = FEATURE_OP_AND;
			list_append(detail_ptr->feature_list, feat);
			feature = NULL;
			count = 0;
		} else if (tmp_requested[i] == '|') {
			tmp_requested[i] = '\0';
			have_or = true;
			if (feature == NULL) {
				info("Job %u invalid constraint %s",
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_INVALID_FEATURE;
			}
			feat = xmalloc(sizeof(struct feature_record));
			feat->name = xstrdup(feature);
			feat->count = count;
			if (bracket)
				feat->op_code = FEATURE_OP_XOR;
			else
				feat->op_code = FEATURE_OP_OR;
			list_append(detail_ptr->feature_list, feat);
			feature = NULL;
			count = 0;
		} else if (tmp_requested[i] == '[') {
			tmp_requested[i] = '\0';
			if ((feature != NULL) || bracket) {
				info("Job %u invalid constraint %s",
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_INVALID_FEATURE;
			}
			bracket++;
		} else if (tmp_requested[i] == ']') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (bracket == 0)) {
				info("Job %u invalid constraint %s",
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_INVALID_FEATURE;
			}
			bracket = 0;
		} else if (tmp_requested[i] == '\0') {
			if (feature) {
				feat = xmalloc(sizeof(struct feature_record));
				feat->name = xstrdup(feature);
				feat->count = count;
				feat->op_code = FEATURE_OP_END;
				list_append(detail_ptr->feature_list, feat);
			}
			break;
		} else if (tmp_requested[i] == ',') {
			info("Job %u invalid constraint %s",
				job_ptr->job_id, detail_ptr->features);
			xfree(tmp_requested);
			return ESLURM_INVALID_FEATURE;
		} else if (feature == NULL) {
			feature = &tmp_requested[i];
		}
	}
	xfree(tmp_requested);
	if (have_count && have_or) {
		info("Job %u invalid constraint (OR with feature count): %s",
			job_ptr->job_id, detail_ptr->features);
		return ESLURM_INVALID_FEATURE;
	}

	return _valid_feature_list(job_ptr->job_id, detail_ptr->feature_list);
}

static void _feature_list_delete(void *x)
{
	struct feature_record *feature = (struct feature_record *)x;
	xfree(feature->name);
	xfree(feature);
}

static int _valid_feature_list(uint32_t job_id, List feature_list)
{
	ListIterator feat_iter;
	struct feature_record *feat_ptr;
	char *buf = NULL, tmp[16];
	int bracket = 0;
	int rc = SLURM_SUCCESS;

	if (feature_list == NULL) {
		debug2("Job %u feature list is empty", job_id);
		return rc;
	}

	feat_iter = list_iterator_create(feature_list);
	while ((feat_ptr = (struct feature_record *)list_next(feat_iter))) {
		if (feat_ptr->op_code == FEATURE_OP_XOR) {
			if (bracket == 0)
				xstrcat(buf, "[");
			bracket = 1;
		}
		xstrcat(buf, feat_ptr->name);
		if (rc == SLURM_SUCCESS)
			rc = _valid_node_feature(feat_ptr->name);
		if (feat_ptr->count) {
			snprintf(tmp, sizeof(tmp), "*%u", feat_ptr->count);
			xstrcat(buf, tmp);
		}
		if (bracket && (feat_ptr->op_code != FEATURE_OP_XOR)) {
			xstrcat(buf, "]");
			bracket = 0;
		}
		if (feat_ptr->op_code == FEATURE_OP_AND)
			xstrcat(buf, "&");
		else if ((feat_ptr->op_code == FEATURE_OP_OR) ||
			 (feat_ptr->op_code == FEATURE_OP_XOR))
			xstrcat(buf, "|");
	}
	list_iterator_destroy(feat_iter);
	if (rc == SLURM_SUCCESS)
		debug("Job %u feature list: %s", job_id, buf);
	else
		info("Job %u has invalid feature list: %s", job_id, buf);
	xfree(buf);
	return rc;
}

static int _valid_node_feature(char *feature)
{
	int rc = ESLURM_INVALID_FEATURE;
	struct features_record *feature_ptr;
	ListIterator feature_iter;

	/* Clear these nodes from the feature_list record,
	 * then restore as needed */
	feature_iter = list_iterator_create(feature_list);
	if (feature_iter == NULL)
		fatal("list_inerator_create malloc failure");
	while ((feature_ptr = (struct features_record *)
			list_next(feature_iter))) {
		if (strcmp(feature_ptr->name, feature))
			continue;
		rc = SLURM_SUCCESS;
		break;
	}
	list_iterator_destroy(feature_iter);

	return rc;
}
