/*****************************************************************************\
 * job_scheduler.c - manage the scheduling of pending jobs in priority order
 *	Note there is a global job list (job_list)
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"

#define MAX_RETRIES 10

struct job_queue {
	int priority;
	struct job_record *job_ptr;
};

static int  _build_job_queue(struct job_queue **job_queue);
static void _launch_job(struct job_record *job_ptr);
static void _sort_job_queue(struct job_queue *job_queue,
			    int job_queue_size);
static char **_xduparray(uint16_t size, char ** array);

/* 
 * _build_job_queue - build (non-priority ordered) list of pending jobs
 * OUT job_queue - pointer to job queue
 * RET number of entries in job_queue
 * global: job_list - global list of job records
 * NOTE: the buffer at *job_queue must be xfreed by the caller
 */
static int _build_job_queue(struct job_queue **job_queue)
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
		if ((job_ptr->job_state != JOB_PENDING)    ||
		    (job_ptr->job_state &  JOB_COMPLETING) ||
		    (job_ptr->priority == 0))	/* held */
			continue;
		if (!job_independent(job_ptr))	/* waiting for other job */
			continue;
		if (job_buffer_size <= job_queue_size) {
			job_buffer_size += 50;
			xrealloc(my_job_queue, job_buffer_size *
				 sizeof(struct job_queue));
		}
		my_job_queue[job_queue_size].job_ptr  = job_ptr;
		my_job_queue[job_queue_size].priority = job_ptr->priority;
		job_queue_size++;
	}
	list_iterator_destroy(job_iterator);

	job_queue[0] = my_job_queue;
	return job_queue_size;
}

/*
 * job_is_completing - Determine if jobs are in the process of completing.
 * RET - True of any job is in the process of completing
 * NOTE: This function can reduce resource fragmentation, which is a 
 * critical issue on Elan interconnect based systems.
 */
extern bool job_is_completing(void)
{
	bool completing = false;
	ListIterator job_iterator;
	struct job_record *job_ptr = NULL;
	time_t recent = time(NULL) - (slurmctld_conf.kill_wait + 2);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((job_ptr->job_state & JOB_COMPLETING) &&
		    (job_ptr->end_time >= recent)) {
			completing = true;
			break;
		}
	}
	list_iterator_destroy(job_iterator);

	return completing;
}

/* 
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority  
 *	order until a request fails
 * RET count of jobs scheduled
 * global: job_list - global list of job records
 *	last_job_update - time of last update to job table
 * Note: We re-build the queue every time. Jobs can not only be added 
 *	or removed from the queue, but have their priority or partition 
 *	changed with the update_job RPC. In general nodes will be in priority 
 *	order (by submit time), so the sorting should be pretty fast.
 */
int schedule(void)
{
	struct job_queue *job_queue;
	int i, j, error_code, failed_part_cnt, job_queue_size, job_cnt = 0;
	struct job_record *job_ptr;
	struct part_record **failed_parts;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock =
	    { READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
#ifdef HAVE_BG
	char *ionodes = NULL;
	char tmp_char[256];
#endif
	static bool wiki_sched = false;
	static bool wiki_sched_test = false;
	DEF_TIMERS;

	START_TIMER;
	/* don't bother trying to avoid fragmentation with sched/wiki */
	if (!wiki_sched_test) {
		char *sched_type = slurm_get_sched_type();
		if ((strcmp(sched_type, "sched/wiki") == 0)
		||  (strcmp(sched_type, "sched/wiki2") == 0))
			wiki_sched = true;
		xfree(sched_type);
		wiki_sched_test = true;
	}

	lock_slurmctld(job_write_lock);
	/* Avoid resource fragmentation if important */
	if ((!wiki_sched) && switch_no_frag() && job_is_completing()) {
		unlock_slurmctld(job_write_lock);
		debug("schedule() returning, some job still completing");
		return SLURM_SUCCESS;
	}
	debug("Running job scheduler");
	job_queue_size = _build_job_queue(&job_queue);
	if (job_queue_size == 0) {
		unlock_slurmctld(job_write_lock);
		return SLURM_SUCCESS;
	}
	_sort_job_queue(job_queue, job_queue_size);

	failed_part_cnt = 0;
	failed_parts = NULL;
	for (i = 0; i < job_queue_size; i++) {
		job_ptr = job_queue[i].job_ptr;
		if (job_ptr->priority == 0)	/* held */
			continue;
		for (j = 0; j < failed_part_cnt; j++) {
			if (failed_parts[j] == job_ptr->part_ptr)
				break;
		}
		if (j < failed_part_cnt)
			continue;

		error_code = select_nodes(job_ptr, false, NULL);
		if (error_code == ESLURM_NODES_BUSY) {
#ifndef HAVE_BG 	/* keep trying to schedule jobs in partition */
			/* While we use static partitiioning on Blue Gene, 
			 * each job can be scheduled independently without 
			 * impacting other jobs with different characteristics
			 * (e.g. node-use [virtual or coprocessor] or conn-type
			 * [mesh, torus, or nav]). Because of this we sort and 
			 * then try to schedule every pending job. This does 
			 * increase the overhead of this job scheduling cycle, 
			 * but the only way to effectively avoid this is to 
			 * define each SLURM partition as containing a 
			 * single Blue Gene job partition type (e.g. 
			 * group all Blue Gene job partitions of type 
			 * 2x2x2 coprocessor mesh into a single SLURM
			 * partition, say "co-mesh-222") */
			xrealloc(failed_parts,
				 (failed_part_cnt + 1) * 
				 sizeof(struct part_record *));
			failed_parts[failed_part_cnt++] =
			    job_ptr->part_ptr;
#endif
		} else if (error_code == SLURM_SUCCESS) {	
			/* job initiated */
			last_job_update = time(NULL);
#ifdef HAVE_BG
			select_g_get_jobinfo(job_ptr->select_jobinfo, 
					     SELECT_DATA_IONODES, 
					     &ionodes);
			if(ionodes) {
				sprintf(tmp_char,"%s[%s]",
						job_ptr->nodes,
						ionodes);
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
			if (job_ptr->batch_flag)
				_launch_job(job_ptr);
			else
				srun_allocate(job_ptr->job_id);
			job_cnt++;
		} else if (error_code !=
		           ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
			info("schedule: JobId=%u non-runnable: %s",
				job_ptr->job_id, 
				slurm_strerror(error_code));
			last_job_update = time(NULL);
			job_ptr->job_state = JOB_FAILED;
			job_ptr->exit_code = 1;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			job_ptr->start_time = job_ptr->end_time = time(NULL);
			job_completion_logger(job_ptr);
			delete_job_details(job_ptr);
		}
	}

	xfree(failed_parts);
	xfree(job_queue);
	unlock_slurmctld(job_write_lock);
	END_TIMER2("schedule");
	return job_cnt;
}


/* 
 * _sort_job_queue - sort job_queue in decending priority order
 * IN job_queue_size - count of elements in the job queue
 * IN/OUT job_queue - pointer to sorted job queue
 */
static void _sort_job_queue(struct job_queue *job_queue, int job_queue_size)
{
	int i, j, top_prio_inx;
	int tmp_prio, top_prio;
	struct job_record *tmp_job_ptr;

	for (i = 0; i < job_queue_size; i++) {
		top_prio = job_queue[i].priority;
		top_prio_inx = i;
		for (j = (i + 1); j < job_queue_size; j++) {
			if (top_prio >= job_queue[j].priority)
				continue;
			top_prio = job_queue[j].priority;
			top_prio_inx = j;
		}
		if (top_prio_inx == i)
			continue;
		tmp_prio = job_queue[i].priority;
		tmp_job_ptr = job_queue[i].job_ptr;
		job_queue[i].priority = job_queue[top_prio_inx].priority;
		job_queue[i].job_ptr = job_queue[top_prio_inx].job_ptr;
		job_queue[top_prio_inx].priority = tmp_prio;
		job_queue[top_prio_inx].job_ptr = tmp_job_ptr;
	}
}

/* _launch_job - send an RPC to a slurmd to initiate a batch job 
 * IN job_ptr - pointer to job that will be initiated
 */
static void _launch_job(struct job_record *job_ptr)
{
	batch_job_launch_msg_t *launch_msg_ptr;
	agent_arg_t *agent_arg_ptr;
	struct node_record *node_ptr;

	node_ptr = find_first_node_record(job_ptr->node_bitmap);
	if (node_ptr == NULL)
		return;

	/* Initialization of data structures */
	launch_msg_ptr =
	    (batch_job_launch_msg_t *)
	    xmalloc(sizeof(batch_job_launch_msg_t));
	launch_msg_ptr->job_id = job_ptr->job_id;
	launch_msg_ptr->step_id = NO_VAL;
	launch_msg_ptr->uid = job_ptr->user_id;
	launch_msg_ptr->gid = job_ptr->group_id;
	launch_msg_ptr->nprocs = job_ptr->details->num_tasks;
	launch_msg_ptr->nodes = xstrdup(job_ptr->nodes);
	launch_msg_ptr->overcommit = job_ptr->details->overcommit;

	if (make_batch_job_cred(launch_msg_ptr)) {
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

	launch_msg_ptr->err = xstrdup(job_ptr->details->err);
	launch_msg_ptr->in = xstrdup(job_ptr->details->in);
	launch_msg_ptr->out = xstrdup(job_ptr->details->out);
	launch_msg_ptr->work_dir = xstrdup(job_ptr->details->work_dir);
	launch_msg_ptr->argc = job_ptr->details->argc;
	launch_msg_ptr->argv = _xduparray(job_ptr->details->argc,
					job_ptr->details->argv);
	launch_msg_ptr->script = get_job_script(job_ptr);
	launch_msg_ptr->environment =
	    get_job_env(job_ptr, &launch_msg_ptr->envc);

	launch_msg_ptr->num_cpu_groups = job_ptr->num_cpu_groups;
	launch_msg_ptr->cpus_per_node  = xmalloc(sizeof(uint32_t) *
			job_ptr->num_cpu_groups);
	memcpy(launch_msg_ptr->cpus_per_node, job_ptr->cpus_per_node,
			(sizeof(uint32_t) * job_ptr->num_cpu_groups));
	launch_msg_ptr->cpu_count_reps  = xmalloc(sizeof(uint32_t) *
			job_ptr->num_cpu_groups);
	memcpy(launch_msg_ptr->cpu_count_reps, job_ptr->cpu_count_reps,
			(sizeof(uint32_t) * job_ptr->num_cpu_groups));

	launch_msg_ptr->select_jobinfo = select_g_copy_jobinfo(
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

static char **
_xduparray(uint16_t size, char ** array)
{
	int i;
	char ** result;

	if (size == 0)
		return (char **)NULL;

	result = (char **) xmalloc(sizeof(char *) * size);
	for (i=0; i<size; i++)
		result[i] = xstrdup(array[i]);
	return result;
}

/*
 * make_batch_job_cred - add a job credential to the batch_job_launch_msg
 * IN/OUT launch_msg_ptr - batch_job_launch_msg in which job_id, step_id, 
 *                         uid and nodes have already been set 
 * RET 0 or error code
 */
extern int make_batch_job_cred(batch_job_launch_msg_t *launch_msg_ptr)
{
	slurm_cred_arg_t cred_arg;

	cred_arg.jobid     = launch_msg_ptr->job_id;
	cred_arg.stepid    = launch_msg_ptr->step_id;
	cred_arg.uid       = launch_msg_ptr->uid;
	cred_arg.hostlist  = launch_msg_ptr->nodes;
	cred_arg.alloc_lps_cnt = 0;
	cred_arg.alloc_lps = NULL;

	launch_msg_ptr->cred = slurm_cred_create(slurmctld_config.cred_ctx,
			 &cred_arg);

	if (launch_msg_ptr->cred)
		return SLURM_SUCCESS;
	error("slurm_cred_create failure for batch job %u", cred_arg.jobid);
	return SLURM_ERROR;
}
