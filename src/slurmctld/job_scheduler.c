/*****************************************************************************\
 * job_scheduler.c - manage the scheduling of pending jobs in priority order
 *	Note there is a global job list (job_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2016 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h> /* for SIGKILL */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "src/common/assoc_mgr.h"
#include "src/common/env.h"
#include "src/common/gres.h"
#include "src/common/group_cache.h"
#include "src/common/layouts_mgr.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/node_select.h"
#include "src/common/power.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather.h"
#include "src/common/strlcpy.h"
#include "src/common/parse_time.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"
#include "src/slurmctld/state_save.h"
#include "src/slurmctld/powercapping.h"

#define _DEBUG 0
#ifndef CORRESPOND_ARRAY_TASK_CNT
#  define CORRESPOND_ARRAY_TASK_CNT 10
#endif
#define BUILD_TIMEOUT 2000000	/* Max build_job_queue() run time in usec */
#define MAX_FAILED_RESV 10

typedef struct epilog_arg {
	char *epilog_slurmctld;
	uint32_t job_id;
	char **my_env;
} epilog_arg_t;

static char **	_build_env(struct job_record *job_ptr, bool is_epilog);
static batch_job_launch_msg_t *_build_launch_job_msg(struct job_record *job_ptr,
						     uint16_t protocol_version);
static void	_depend_list_del(void *dep_ptr);
static void	_feature_list_delete(void *x);
static void	_job_queue_append(List job_queue, struct job_record *job_ptr,
				  struct part_record *part_ptr, uint32_t priority);
static void	_job_queue_rec_del(void *x);
static bool	_job_runnable_test1(struct job_record *job_ptr,
				    bool clear_start);
static bool	_job_runnable_test2(struct job_record *job_ptr,
				    bool check_min_time);
static void *	_run_epilog(void *arg);
static void *	_run_prolog(void *arg);
static bool	_scan_depend(List dependency_list, uint32_t job_id);
static void *	_sched_agent(void *args);
static int	_schedule(uint32_t job_limit);
static int	_valid_feature_list(struct job_record *job_ptr,
				    List feature_list);
static int	_valid_node_feature(char *feature, bool can_reboot);
#ifndef HAVE_FRONT_END
static void *	_wait_boot(void *arg);
#endif
static int	build_queue_timeout = BUILD_TIMEOUT;
static int	save_last_part_update = 0;

static pthread_mutex_t sched_mutex = PTHREAD_MUTEX_INITIALIZER;
static int sched_pend_thread = 0;
static bool sched_running = false;
static struct timeval sched_last = {0, 0};
static uint32_t max_array_size = NO_VAL;
#ifdef HAVE_ALPS_CRAY
static int sched_min_interval = 1000000;
#else
static int sched_min_interval = 2;
#endif

static int bb_array_stage_cnt = 10;
extern diag_stats_t slurmctld_diag_stats;

/*
 * Calculate how busy the system is by figuring out how busy each node is.
 */
static double _get_system_usage()
{
	static double sys_usage_per = 0.0;
	static time_t last_idle_update = 0;
	static uint16_t priority_flags = 0;
	static bool statics_inited = false;

	if (!statics_inited) {
		priority_flags = slurm_get_priority_flags();
		statics_inited = true;
	}

	if (last_idle_update < last_node_update) {
		int    i;
		double alloc_tres = 0;
		double tot_tres   = 0;

		select_g_select_nodeinfo_set_all();

		for (i = 0; i < node_record_count; i++) {
			struct node_record *node_ptr =
				&node_record_table_ptr[i];
			double node_alloc_tres = 0.0;
			double node_tot_tres   = 0.0;

			select_g_select_nodeinfo_get(
				node_ptr->select_nodeinfo,
				SELECT_NODEDATA_TRES_ALLOC_WEIGHTED,
				NODE_STATE_ALLOCATED, &node_alloc_tres);

			node_tot_tres =
				assoc_mgr_tres_weighted(
				node_ptr->tres_cnt,
				node_ptr->config_ptr->tres_weights,
				priority_flags, false);

			alloc_tres += node_alloc_tres;
			tot_tres   += node_tot_tres;
		}
		last_idle_update = last_node_update;

		if (tot_tres)
			sys_usage_per = (alloc_tres / tot_tres) * 100;
	}

	return sys_usage_per;
}

/*
 * _build_user_job_list - build list of jobs for a given user
 *			  and an optional job name
 * IN  user_id - user id
 * IN  job_name - job name constraint
 * RET the job queue
 * NOTE: the caller must call FREE_NULL_LIST() on RET value to free memory
 */
static List _build_user_job_list(uint32_t user_id, char* job_name)
{
	List job_queue;
	ListIterator job_iterator;
	struct job_record *job_ptr = NULL;

	job_queue = list_create(NULL);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		xassert (job_ptr->magic == JOB_MAGIC);
		if (job_ptr->user_id != user_id)
			continue;
		if (job_name && job_ptr->name &&
		    xstrcmp(job_name, job_ptr->name))
			continue;
		list_append(job_queue, job_ptr);
	}
	list_iterator_destroy(job_iterator);

	return job_queue;
}

static void _job_queue_append(List job_queue, struct job_record *job_ptr,
			      struct part_record *part_ptr, uint32_t prio)
{
	job_queue_rec_t *job_queue_rec;

	job_queue_rec = xmalloc(sizeof(job_queue_rec_t));
	job_queue_rec->array_task_id = job_ptr->array_task_id;
	job_queue_rec->job_id   = job_ptr->job_id;
	job_queue_rec->job_ptr  = job_ptr;
	job_queue_rec->part_ptr = part_ptr;
	job_queue_rec->priority = prio;
	list_append(job_queue, job_queue_rec);
}

static void _job_queue_rec_del(void *x)
{
	xfree(x);
}

/* Return true if the job has some step still in a cleaning state, which
 * can happen on a Cray if a job is requeued and the step NHC is still running
 * after the requeued job is eligible to run again */
static uint16_t _is_step_cleaning(struct job_record *job_ptr)
{
	ListIterator step_iterator;
	struct step_record *step_ptr;
	uint16_t cleaning = 0;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = (struct step_record *) list_next (step_iterator))) {
		/* Only check if not a pending step */
		if (step_ptr->step_id != SLURM_PENDING_STEP) {
			select_g_select_jobinfo_get(step_ptr->select_jobinfo,
						    SELECT_JOBDATA_CLEANING,
						    &cleaning);
			if (cleaning)
				break;
		}
	}
	list_iterator_destroy(step_iterator);

	return cleaning;
}

/* Job test for ability to run now, excludes partition specific tests */
static bool _job_runnable_test1(struct job_record *job_ptr, bool sched_plugin)
{
	bool job_indepen = false;
	uint16_t cleaning = 0;
	time_t now = time(NULL);

	xassert(job_ptr->magic == JOB_MAGIC);
	if (!IS_JOB_PENDING(job_ptr) || IS_JOB_COMPLETING(job_ptr))
		return false;

	if (IS_JOB_REVOKED(job_ptr))
		return false;

	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_CLEANING,
				    &cleaning);
	if (!cleaning)
		cleaning = _is_step_cleaning(job_ptr);
	if (cleaning ||
	    (job_ptr->details && job_ptr->details->prolog_running) ||
	    (job_ptr->step_list && list_count(job_ptr->step_list))) {
		/* Job's been requeued and the
		 * previous run hasn't finished yet */
		job_ptr->state_reason = WAIT_CLEANING;
		xfree(job_ptr->state_desc);
		debug3("sched: JobId=%u. State=PENDING. "
		       "Reason=Cleaning.",
		       job_ptr->job_id);
		return false;
	}

#ifdef HAVE_FRONT_END
	/* At least one front-end node up at this point */
	if (job_ptr->state_reason == WAIT_FRONT_END) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
		last_job_update = now;
	}
#endif

	job_indepen = job_independent(job_ptr, 0);
	if (sched_plugin)
		job_ptr->start_time = (time_t) 0;
	if (job_ptr->priority == 0)	{ /* held */
		if (job_ptr->state_reason != FAIL_BAD_CONSTRAINTS
		    && (job_ptr->state_reason != WAIT_RESV_DELETED)
		    && (job_ptr->state_reason != FAIL_BURST_BUFFER_OP)
		    && (job_ptr->state_reason != WAIT_HELD)
		    && (job_ptr->state_reason != WAIT_HELD_USER)
		    && job_ptr->state_reason != WAIT_MAX_REQUEUE) {
			job_ptr->state_reason = WAIT_HELD;
			xfree(job_ptr->state_desc);
			last_job_update = now;
		}
		debug3("sched: JobId=%u. State=%s. Reason=%s. Priority=%u.",
		       job_ptr->job_id,
		       job_state_string(job_ptr->job_state),
		       job_reason_string(job_ptr->state_reason),
		       job_ptr->priority);
		return false;
	}

	if (!job_indepen &&
	    ((job_ptr->state_reason == WAIT_HELD) ||
	     (job_ptr->state_reason == WAIT_HELD_USER))) {
		/* released behind active dependency? */
		job_ptr->state_reason = WAIT_DEPENDENCY;
		xfree(job_ptr->state_desc);
	}

	if (!job_indepen)	/* can not run now */
		return false;

	return true;
}

/*
 * Job and partition tests for ability to run now
 * IN job_ptr - job to test
 * IN check_min_time - If set, test job's minimum time limit
 *		otherwise test maximum time limit
 */
static bool _job_runnable_test2(struct job_record *job_ptr, bool check_min_time)
{
	int reason;

	reason = job_limits_check(&job_ptr, check_min_time);
	if ((reason != job_ptr->state_reason) &&
	    ((reason != WAIT_NO_REASON) ||
	     (!part_policy_job_runnable_state(job_ptr)))) {
		job_ptr->state_reason = reason;
		xfree(job_ptr->state_desc);
	}
	if (reason != WAIT_NO_REASON)
		return false;
	return true;
}

/*
 * build_job_queue - build (non-priority ordered) list of pending jobs
 * IN clear_start - if set then clear the start_time for pending jobs,
 *		    true when called from sched/backfill or sched/builtin
 * IN backfill - true if running backfill scheduler, enforce min time limit
 * RET the job queue
 * NOTE: the caller must call FREE_NULL_LIST() on RET value to free memory
 */
extern List build_job_queue(bool clear_start, bool backfill)
{
	static time_t last_log_time = 0;
	List job_queue;
	ListIterator depend_iter, job_iterator, part_iterator;
	struct job_record *job_ptr = NULL, *new_job_ptr;
	struct part_record *part_ptr;
	struct depend_spec *dep_ptr;
	int i, pend_cnt, reason, dep_corr;
	struct timeval start_tv = {0, 0};
	int tested_jobs = 0;
	char jobid_buf[32];
	int job_part_pairs = 0;
	time_t now = time(NULL);

	/* init the timer */
	(void) slurm_delta_tv(&start_tv);
	job_queue = list_create(_job_queue_rec_del);

	/* Create individual job records for job arrays that need burst buffer
	 * staging */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_PENDING(job_ptr) ||
		    !job_ptr->burst_buffer || !job_ptr->array_recs ||
		    !job_ptr->array_recs->task_id_bitmap ||
		    (job_ptr->array_task_id != NO_VAL))
			continue;
		if ((i = bit_ffs(job_ptr->array_recs->task_id_bitmap)) < 0)
			continue;
		pend_cnt = num_pending_job_array_tasks(job_ptr->array_job_id);
		if (pend_cnt >= bb_array_stage_cnt)
			continue;
		if (job_ptr->array_recs->task_cnt < 1)
			continue;
		if (job_ptr->array_recs->task_cnt == 1) {
			job_ptr->array_task_id = i;
			job_array_post_sched(job_ptr);
			continue;
		}
		job_ptr->array_task_id = i;
		new_job_ptr = job_array_split(job_ptr);
		if (new_job_ptr) {
			debug("%s: Split out %s for burst buffer use", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
			new_job_ptr->job_state = JOB_PENDING;
			new_job_ptr->start_time = (time_t) 0;
			/* Do NOT clear db_index here, it is handled when
			 * task_id_str is created elsewhere */
			(void) bb_g_job_validate2(job_ptr, NULL);
		} else {
			error("%s: Unable to copy record for %s", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		}
	}
	list_iterator_destroy(job_iterator);

	/* Create individual job records for job arrays with
	 * depend_type == SLURM_DEPEND_AFTER_CORRESPOND */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_PENDING(job_ptr) ||
		    !job_ptr->array_recs ||
		    !job_ptr->array_recs->task_id_bitmap ||
		    (job_ptr->array_task_id != NO_VAL))
			continue;
		if ((i = bit_ffs(job_ptr->array_recs->task_id_bitmap)) < 0)
			continue;
		if ((job_ptr->details == NULL) ||
		    (job_ptr->details->depend_list == NULL) ||
		    (list_count(job_ptr->details->depend_list) == 0))
			continue;
	        depend_iter = list_iterator_create(job_ptr->details->depend_list);
		dep_corr = 0;
	        while ((dep_ptr = list_next(depend_iter))) {
	                if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_CORRESPOND) {
				dep_corr = 1;
				break;
			}
	        }
		if (!dep_corr)
			continue;
		pend_cnt = num_pending_job_array_tasks(job_ptr->array_job_id);
		if (pend_cnt >= CORRESPOND_ARRAY_TASK_CNT)
			continue;
		if (job_ptr->array_recs->task_cnt < 1)
			continue;
		if (job_ptr->array_recs->task_cnt == 1) {
			job_ptr->array_task_id = i;
			job_array_post_sched(job_ptr);
			continue;
		}
		job_ptr->array_task_id = i;
		new_job_ptr = job_array_split(job_ptr);
		if (new_job_ptr) {
			info("%s: Split out %s for SLURM_DEPEND_AFTER_CORRESPOND use",
			      __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
			new_job_ptr->job_state = JOB_PENDING;
			new_job_ptr->start_time = (time_t) 0;
			/* Do NOT clear db_index here, it is handled when
			 * task_id_str is created elsewhere */
		} else {
			error("%s: Unable to copy record for %s", __func__,
			      jobid2fmt(job_ptr, jobid_buf, sizeof(jobid_buf)));
		}
	}
	list_iterator_destroy(job_iterator);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (((tested_jobs % 100) == 0) &&
		    (slurm_delta_tv(&start_tv) >= build_queue_timeout)) {
			if (difftime(now, last_log_time) > 600) {
				/* Log at most once every 10 minutes */
				info("%s has run for %d usec, exiting with %d "
				     "of %d jobs tested, %d job-partition "
				     "pairs added",
				     __func__, build_queue_timeout, tested_jobs,
				     list_count(job_list), job_part_pairs);
				last_log_time = now;
			}
			break;
		}
		tested_jobs++;
		job_ptr->preempt_in_progress = false;	/* initialize */
		if (job_ptr->state_reason != WAIT_NO_REASON) {
			job_ptr->state_reason_prev = job_ptr->state_reason;
			last_job_update = now;
		} else if ((job_ptr->state_reason_prev == WAIT_TIME) &&
			   job_ptr->details &&
			   (job_ptr->details->begin_time <= now)) {
			job_ptr->state_reason_prev = job_ptr->state_reason;
			last_job_update = now;
		}
		if (!_job_runnable_test1(job_ptr, clear_start))
			continue;

		if (job_ptr->part_ptr_list) {
			int inx = -1;
			part_iterator = list_iterator_create(
				job_ptr->part_ptr_list);
			while ((part_ptr = (struct part_record *)
				list_next(part_iterator))) {
				job_ptr->part_ptr = part_ptr;
				reason = job_limits_check(&job_ptr, backfill);
				if ((reason != WAIT_NO_REASON) &&
				    (reason != job_ptr->state_reason)) {
					job_ptr->state_reason = reason;
					xfree(job_ptr->state_desc);
					last_job_update = now;
				}
				/* priority_array index matches part_ptr_list
				 * position: increment inx */
				inx++;
				if (reason != WAIT_NO_REASON)
					continue;
				job_part_pairs++;
				if (job_ptr->priority_array) {
					_job_queue_append(job_queue, job_ptr,
							  part_ptr,
							  job_ptr->
							  priority_array[inx]);
				} else {
					_job_queue_append(job_queue, job_ptr,
							  part_ptr,
							  job_ptr->priority);
				}
			}
			list_iterator_destroy(part_iterator);
		} else {
			if (job_ptr->part_ptr == NULL) {
				part_ptr = find_part_record(job_ptr->partition);
				if (part_ptr == NULL) {
					error("Could not find partition %s "
					      "for job %u", job_ptr->partition,
					      job_ptr->job_id);
					continue;
				}
				job_ptr->part_ptr = part_ptr;
				error("partition pointer reset for job %u, "
				      "part %s", job_ptr->job_id,
				      job_ptr->partition);
			}
			if (!_job_runnable_test2(job_ptr, backfill))
				continue;
			job_part_pairs++;
			_job_queue_append(job_queue, job_ptr,
					  job_ptr->part_ptr, job_ptr->priority);
		}
	}
	list_iterator_destroy(job_iterator);

	return job_queue;
}

/*
 * job_is_completing - Determine if jobs are in the process of completing.
 * IN/OUT  eff_cg_bitmap - optional bitmap of all relevent completing nodes,
 *                         relevenace determined by filtering via CompleteWait
 *                         if NULL, function will terminate at first completing
 *                         job
 * RET - True of any job is in the process of completing AND
 *	 CompleteWait is configured non-zero
 * NOTE: This function can reduce resource fragmentation, which is a
 * critical issue on Elan interconnect based systems.
 */
extern bool job_is_completing(bitstr_t *eff_cg_bitmap)
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
			/* can return after finding first completing job so long
			 * as a map of nodes in partitions affected by
			 * completing jobs is not required */
			if (!eff_cg_bitmap)
				break;
			else
				bit_or(eff_cg_bitmap,
				       job_ptr->part_ptr->node_bitmap);
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
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	time_t now = time(NULL);
#ifdef HAVE_BG
	static uint16_t cpus_per_node = 0;
	if (!cpus_per_node)
		select_g_alter_node_cnt(SELECT_GET_NODE_CPU_CNT,
					&cpus_per_node);
#endif

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		uint32_t job_min_nodes, job_max_nodes;
		uint32_t part_min_nodes, part_max_nodes;
		part_ptr = job_ptr->part_ptr;
		if (!IS_JOB_PENDING(job_ptr))
			continue;
		if (part_ptr == NULL)
			continue;
		if ((job_ptr->details == NULL) ||
		    (job_ptr->details->begin_time > now))
			continue;
		if ((part_ptr->state_up & PARTITION_SCHED) == 0)
			continue;
		if ((job_ptr->time_limit != NO_VAL) &&
		    (job_ptr->time_limit > part_ptr->max_time))
			continue;
#ifdef HAVE_BG
		job_min_nodes = job_ptr->details->min_cpus / cpus_per_node;
		job_max_nodes = job_ptr->details->max_cpus / cpus_per_node;
		part_min_nodes = part_ptr->min_nodes_orig;
		part_max_nodes = part_ptr->max_nodes_orig;
#else
		job_min_nodes = job_ptr->details->min_nodes;
		job_max_nodes = job_ptr->details->max_nodes;
		part_min_nodes = part_ptr->min_nodes;
		part_max_nodes = part_ptr->max_nodes;
#endif
		if ((job_max_nodes != 0) &&
		    ((job_max_nodes < part_min_nodes) ||
		     (job_min_nodes > part_max_nodes)))
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

static void _do_diag_stats(long delta_t)
{
	if (delta_t > slurmctld_diag_stats.schedule_cycle_max)
		slurmctld_diag_stats.schedule_cycle_max = delta_t;

	slurmctld_diag_stats.schedule_cycle_sum += delta_t;
	slurmctld_diag_stats.schedule_cycle_last = delta_t;
	slurmctld_diag_stats.schedule_cycle_counter++;
}


/*
 * Given that one batch job just completed, attempt to launch a suitable
 * replacement batch job in a response messge as a REQUEST_BATCH_JOB_LAUNCH
 * message type, alternately send a return code fo SLURM_SUCCESS
 * msg IN - The original message from slurmd
 * fini_job_ptr IN - Pointer to job that just completed and needs replacement
 * RET true if there are pending jobs that might use the resources
 */
extern bool replace_batch_job(slurm_msg_t * msg, void *fini_job, bool locked)
{
	static int select_serial = -1;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock =
	    { READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	struct job_record *job_ptr = NULL;
	struct job_record *fini_job_ptr = (struct job_record *) fini_job;
	struct part_record *part_ptr;
	ListIterator job_iterator = NULL, part_iterator = NULL;
	batch_job_launch_msg_t *launch_msg = NULL;
	bitstr_t *orig_exc_bitmap = NULL;
	bool have_node_bitmaps, pending_jobs = false;
	time_t now, min_age;
	int error_code;

	if (select_serial == -1) {
		if (xstrcmp(slurmctld_conf.select_type, "select/serial"))
			select_serial = 0;
		else
			select_serial = 1;
	}
	if ((select_serial != 1) || (fini_job_ptr == NULL) ||
	    (msg->msg_type != REQUEST_COMPLETE_BATCH_JOB))
		goto send_reply;

	now = time(NULL);
	min_age = now - slurmctld_conf.min_job_age;
	if (!locked)
		lock_slurmctld(job_write_lock);
	if (!fini_job_ptr->job_resrcs ||
	    !fini_job_ptr->job_resrcs->node_bitmap) {
		/* This should never happen, but if it does, avoid using
		 * a bad pointer below. */
		error("job_resrcs empty for job %u", fini_job_ptr->job_id);
		if (!locked)
			unlock_slurmctld(job_write_lock);
		goto send_reply;
	}
	job_iterator = list_iterator_create(job_list);
	while (1) {
		if (job_ptr && part_iterator)
			goto next_part;

		job_ptr = (struct job_record *) list_next(job_iterator);
		if (!job_ptr)
			break;

		if ((job_ptr == fini_job_ptr)   ||
		    (job_ptr->priority == 0)    ||
		    (job_ptr->pack_job_id != 0) ||
		    (job_ptr->details == NULL)  ||
		    !avail_front_end(job_ptr))
			continue;

		if (!IS_JOB_PENDING(job_ptr)) {
			if (IS_JOB_FINISHED(job_ptr)  &&
			    (job_ptr != fini_job_ptr) &&
			    (job_ptr->end_time <= min_age)) {
				/* If we don't have a db_index by now and we
				 * are running with the slurmdbd lets put it on
				 * the list to be handled later when it comes
				 * back up since we won't get another chance.
				 * This is fine because start() doesn't wait
				 * for db_index for a finished job.
				 */
				jobacct_storage_job_start_direct(acct_db_conn,
								 job_ptr);
				list_delete_item(job_iterator);
			}
			continue;
		}

		/* Tests dependencies, begin time and reservations */
		if (!job_independent(job_ptr, 0))
			continue;

		if (job_ptr->part_ptr_list) {
			part_iterator = list_iterator_create(job_ptr->
							     part_ptr_list);
next_part:		part_ptr = (struct part_record *)
				   list_next(part_iterator);
			if (part_ptr) {
				job_ptr->part_ptr = part_ptr;
			} else {
				list_iterator_destroy(part_iterator);
				part_iterator = NULL;
				continue;
			}
		}
		if (job_limits_check(&job_ptr, false) != WAIT_NO_REASON)
			continue;

		/* Test for valid account, QOS and required nodes on each pass */
		if (job_ptr->state_reason == FAIL_ACCOUNT) {
			slurmdb_assoc_rec_t assoc_rec;
			memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
			assoc_rec.acct      = job_ptr->account;
			if (job_ptr->part_ptr)
				assoc_rec.partition = job_ptr->part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;

			if (!assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
						     accounting_enforce,
						     &job_ptr->assoc_ptr,
						     false)) {
				job_ptr->state_reason = WAIT_NO_REASON;
				xfree(job_ptr->state_desc);
				job_ptr->assoc_id = assoc_rec.id;
				last_job_update = now;
			} else {
				continue;
			}
		}
		if (job_ptr->qos_id) {
			assoc_mgr_lock_t locks = {
				READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				NO_LOCK, NO_LOCK, NO_LOCK };

			assoc_mgr_lock(&locks);
			if (job_ptr->assoc_ptr &&
			    ((job_ptr->qos_id >= g_qos_count) ||
			     !bit_test(job_ptr->assoc_ptr->usage->valid_qos,
				       job_ptr->qos_id)) &&
			    !job_ptr->limit_set.qos) {
				info("sched: JobId=%u has invalid QOS",
					job_ptr->job_id);
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = FAIL_QOS;
				last_job_update = now;
				assoc_mgr_unlock(&locks);
				continue;
			} else if (job_ptr->state_reason == FAIL_QOS) {
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = WAIT_NO_REASON;
				last_job_update = now;
			}
			assoc_mgr_unlock(&locks);
		}

		if ((job_ptr->state_reason == WAIT_QOS_JOB_LIMIT)
		    || (job_ptr->state_reason >= WAIT_QOS_GRP_CPU
			&& job_ptr->state_reason <= WAIT_QOS_MAX_NODE_PER_USER)
		    || (job_ptr->state_reason == WAIT_QOS_TIME_LIMIT)) {
			job_ptr->state_reason = WAIT_NO_REASON;
			xfree(job_ptr->state_desc);
			last_job_update = now;
		}

		if ((job_ptr->state_reason == WAIT_NODE_NOT_AVAIL) &&
		    job_ptr->details->req_node_bitmap &&
		    !bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_node_bitmap)) {
			continue;
		}

		if (bit_overlap(avail_node_bitmap,
				job_ptr->part_ptr->node_bitmap) == 0) {
			/* This node DRAIN or DOWN */
			continue;
		}

		if (license_job_test(job_ptr, now, true) != SLURM_SUCCESS) {
			job_ptr->state_reason = WAIT_LICENSES;
			xfree(job_ptr->state_desc);
			last_job_update = now;
			continue;
		}

		if (assoc_mgr_validate_assoc_id(acct_db_conn,
						job_ptr->assoc_id,
						accounting_enforce)) {
			/* NOTE: This only happens if a user's account is
			 * disabled between when the job was submitted and
			 * the time we consider running it. It should be
			 * very rare. */
			info("sched: JobId=%u has invalid account",
			     job_ptr->job_id);
			last_job_update = now;
			job_ptr->state_reason = FAIL_ACCOUNT;
			xfree(job_ptr->state_desc);
			continue;
		}

		if (job_ptr->details->exc_node_bitmap)
			have_node_bitmaps = true;
		else
			have_node_bitmaps = false;
		if (have_node_bitmaps &&
		    (bit_overlap(job_ptr->details->exc_node_bitmap,
				 fini_job_ptr->job_resrcs->node_bitmap) != 0))
			continue;

		if (!job_ptr->batch_flag) {  /* Can't pull interactive jobs */
			pending_jobs = true;
			break;
		}

		if (have_node_bitmaps)
			orig_exc_bitmap = job_ptr->details->exc_node_bitmap;
		else
			orig_exc_bitmap = NULL;
		job_ptr->details->exc_node_bitmap =
			bit_copy(fini_job_ptr->job_resrcs->node_bitmap);
		bit_not(job_ptr->details->exc_node_bitmap);
		error_code = select_nodes(job_ptr, false, NULL, NULL, NULL);
		bit_free(job_ptr->details->exc_node_bitmap);
		job_ptr->details->exc_node_bitmap = orig_exc_bitmap;
		if (error_code == SLURM_SUCCESS) {
			last_job_update = now;
			info("sched: Allocate JobId=%u Partition=%s NodeList=%s #CPUs=%u",
			     job_ptr->job_id, job_ptr->part_ptr->name,
			     job_ptr->nodes, job_ptr->total_cpus);

			if (
#ifdef HAVE_BG
				/* On a bluegene system we need to run the
				 * prolog while the job is CONFIGURING so this
				 * can't work off the CONFIGURING flag as done
				 * elsewhere.
				 */
				!job_ptr->details->prolog_running &&
				!(job_ptr->bit_flags & NODE_REBOOT)
#else
				!IS_JOB_CONFIGURING(job_ptr)
#endif
				) {
				launch_msg = _build_launch_job_msg(job_ptr,
							msg->protocol_version);
			}
		}
		break;
	}
	if (!locked)
		unlock_slurmctld(job_write_lock);
	if (job_iterator)
		list_iterator_destroy(job_iterator);
	if (part_iterator)
		list_iterator_destroy(part_iterator);

send_reply:
	if (launch_msg) {
		if (msg->msg_index && msg->ret_list) {
			slurm_msg_t *resp_msg = xmalloc_nz(sizeof(slurm_msg_t));
			slurm_msg_t_init(resp_msg);

			resp_msg->msg_index = msg->msg_index;
			resp_msg->ret_list = NULL;
			/* The return list here is the list we are sending to
			   the node, so after we attach this message to it set
			   it to NULL to remove it.
			*/
			resp_msg->flags = msg->flags;
			resp_msg->protocol_version = msg->protocol_version;
			resp_msg->address = msg->address;
			resp_msg->msg_type = REQUEST_BATCH_JOB_LAUNCH;
			resp_msg->data = launch_msg;
			list_append(msg->ret_list, resp_msg);
		} else {
			slurm_msg_t response_msg;
			slurm_msg_t_init(&response_msg);
			response_msg.flags = msg->flags;
			response_msg.protocol_version = msg->protocol_version;
			response_msg.address = msg->address;
			response_msg.msg_type = REQUEST_BATCH_JOB_LAUNCH;
			response_msg.data = launch_msg;
			slurm_send_node_msg(msg->conn_fd, &response_msg);
			slurm_free_job_launch_msg(launch_msg);
		}
		return false;
	}
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
	return pending_jobs;
}

/* Return true of all partitions have the same priority, otherwise false. */
static bool _all_partition_priorities_same(void)
{
	struct part_record *part_ptr;
	ListIterator iter;
	bool part_priority_set = false;
	uint32_t part_priority = 0;
	bool result = true;

	iter = list_iterator_create(part_list);
	while ((part_ptr = (struct part_record *) list_next(iter))) {
		if (!part_priority_set) {
			part_priority = part_ptr->priority_tier;
			part_priority_set = true;
		} else if (part_priority != part_ptr->priority_tier) {
			result = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return result;
}

/*
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority
 *	order until a request fails
 * IN job_limit - maximum number of jobs to test now, avoid testing the full
 *		  queue on every job submit (0 means to use the system default,
 *		  SchedulerParameters for default_queue_depth)
 * RET count of jobs scheduled
 * Note: If the scheduler has executed recently, rather than executing again
 *	right away, a thread will be spawned to execute later in an effort
 *	to reduce system overhead.
 * Note: We re-build the queue every time. Jobs can not only be added
 *	or removed from the queue, but have their priority or partition
 *	changed with the update_job RPC. In general nodes will be in priority
 *	order (by submit time), so the sorting should be pretty fast.
 * Note: job_write_lock must be unlocked before calling this.
 */
extern int schedule(uint32_t job_limit)
{
	static int sched_job_limit = -1;
	int job_count = 0;
	struct timeval now;
	long delta_t;

	if (slurmctld_config.scheduling_disabled)
		return 0;

	gettimeofday(&now, NULL);
	if (sched_last.tv_sec == 0) {
		delta_t = sched_min_interval;
	} else if (sched_running) {
		delta_t = 0;
	} else {
		delta_t  = (now.tv_sec  - sched_last.tv_sec) * 1000000;
		delta_t +=  now.tv_usec - sched_last.tv_usec;
	}

	slurm_mutex_lock(&sched_mutex);
	if (sched_job_limit == 0)
		;				/* leave unlimited */
	else if (job_limit == 0)
		sched_job_limit = 0;		/* set unlimited */
	else if (sched_job_limit == -1)
		sched_job_limit = job_limit;	/* set initial value */
	else
		sched_job_limit += job_limit;	/* test more jobs */

	if (delta_t >= sched_min_interval) {
		/* Temporariy set time in the future until we get the real
		 * scheduler completion time */
		sched_last.tv_sec  = now.tv_sec;
		sched_last.tv_usec = now.tv_usec;
		sched_running = true;
		job_limit = sched_job_limit;
		sched_job_limit = -1;
		slurm_mutex_unlock(&sched_mutex);

		job_count = _schedule(job_limit);

		slurm_mutex_lock(&sched_mutex);
		gettimeofday(&now, NULL);
		sched_last.tv_sec  = now.tv_sec;
		sched_last.tv_usec = now.tv_usec;
		sched_running = false;
		slurm_mutex_unlock(&sched_mutex);
	} else if (sched_pend_thread == 0) {
		/* We don't want to run now, but also don't want to defer
		 * this forever, so spawn a thread to run later */
		sched_pend_thread = 1;
		slurm_thread_create_detached(NULL, _sched_agent, NULL);
		slurm_mutex_unlock(&sched_mutex);
	} else {
		/* Nothing to do, agent already pending */
		slurm_mutex_unlock(&sched_mutex);
	}

	return job_count;
}

/* Thread used to possibly start job scheduler later, if nothing else does */
static void *_sched_agent(void *args)
{
	long delta_t;
	struct timeval now;
	useconds_t usec;
	int job_cnt;

	usec = sched_min_interval / 2;
	usec = MIN(usec, 1000000);
	usec = MAX(usec, 10000);

	/* Keep waiting until scheduler() can really run */
	while (!slurmctld_config.shutdown_time) {
		usleep(usec);
		if (sched_running)
			continue;
		gettimeofday(&now, NULL);
		delta_t  = (now.tv_sec  - sched_last.tv_sec) * 1000000;
		delta_t +=  now.tv_usec - sched_last.tv_usec;
		if (delta_t >= sched_min_interval)
			break;
	}

	job_cnt = schedule(1);
	slurm_mutex_lock(&sched_mutex);
	sched_pend_thread = 0;
	slurm_mutex_unlock(&sched_mutex);
	if (job_cnt) {
		/* jobs were started, save state */
		schedule_node_save();		/* Has own locking */
		schedule_job_save();		/* Has own locking */
	}

	return NULL;
}

/* Determine if job's deadline specification is still valid, kill job if not
 * job_ptr IN - Job to test
 * func IN - function named used for logging, "sched" or "backfill"
 * RET - true of valid, false if invalid and job cancelled
 */
extern bool deadline_ok(struct job_record *job_ptr, char *func)
{
	time_t now;
	char time_str_deadline[32];
	bool fail_job = false;
	time_t inter;

	now = time(NULL);
	if ((job_ptr->time_min) && (job_ptr->time_min != NO_VAL)) {
		inter = now + job_ptr->time_min * 60;
		if (job_ptr->deadline < inter) {
			slurm_make_time_str(&job_ptr->deadline,
					    time_str_deadline,
					    sizeof(time_str_deadline));
			info("%s: JobId %u with time_min %u exceeded deadline "
			     "%s and cancelled ",
			     func, job_ptr->job_id, job_ptr->time_min,
			     time_str_deadline);
			fail_job = true;
		}
	} else if ((job_ptr->time_limit != NO_VAL) &&
		   (job_ptr->time_limit != INFINITE)) {
		inter = now + job_ptr->time_limit * 60;
		if (job_ptr->deadline < inter) {
			slurm_make_time_str(&job_ptr->deadline,
					   time_str_deadline,
					   sizeof(time_str_deadline));
			info("%s: JobId %u with time_limit %u exceeded "
			     "deadline %s and cancelled ",
			     func, job_ptr->job_id, job_ptr->time_limit,
			    time_str_deadline);
			fail_job = true;
		}
	}
	if (fail_job) {
		last_job_update = now;
		job_ptr->job_state = JOB_DEADLINE;
		job_ptr->exit_code = 1;
		job_ptr->state_reason = FAIL_DEADLINE;
		xfree(job_ptr->state_desc);
		job_ptr->start_time = now;
		job_ptr->end_time = now;
		srun_allocate_abort(job_ptr);
		job_completion_logger(job_ptr, false);
		return false;
	}
	return true;
}

static int _schedule(uint32_t job_limit)
{
	ListIterator job_iterator = NULL, part_iterator = NULL;
	List job_queue = NULL;
	int failed_part_cnt = 0, failed_resv_cnt = 0, job_cnt = 0;
	int error_code, i, j, part_cnt, time_limit, pend_time;
	uint32_t job_depth = 0, array_task_id;
	job_queue_rec_t *job_queue_rec;
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr, **failed_parts = NULL;
	struct part_record *skip_part_ptr = NULL;
	struct slurmctld_resv **failed_resv = NULL;
	bitstr_t *save_avail_node_bitmap;
	struct part_record **sched_part_ptr = NULL;
	int *sched_part_jobs = NULL, bb_wait_cnt = 0;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock =
	    { READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	char jbuf[JBUFSIZ];
	bool is_job_array_head;
#ifdef HAVE_BG
	char *ionodes = NULL;
	char tmp_char[256];
	static bool backfill_sched = false;
#endif
	static time_t sched_update = 0;
	static bool fifo_sched = false;
	static bool assoc_limit_stop = false;
	static int sched_timeout = 0;
	static int sched_max_job_start = 0;
	static int bf_min_age_reserve = 0;
	static uint32_t bf_min_prio_reserve = 0;
	static int def_job_limit = 100;
	static int max_jobs_per_part = 0;
	static int defer_rpc_cnt = 0;
	static bool reduce_completing_frag = 0;
	time_t now, last_job_sched_start, sched_start;
	uint32_t reject_array_job_id = 0;
	struct part_record *reject_array_part = NULL;
	uint16_t reject_state_reason = WAIT_NO_REASON;
	char job_id_buf[32];
	char *unavail_node_str = NULL;
	bool fail_by_part;
	uint32_t deadline_time_limit, save_time_limit = 0;
#if HAVE_SYS_PRCTL_H
	char get_name[16];
#endif
	DEF_TIMERS;

	if (slurmctld_config.shutdown_time)
		return 0;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_GET_NAME, get_name, NULL, NULL, NULL) < 0) {
		error("%s: cannot get my name %m", __func__);
		strlcpy(get_name, "slurmctld", sizeof(get_name));
	}
	if (prctl(PR_SET_NAME, "sched", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "sched");
	}
#endif

	if (sched_update != slurmctld_conf.last_update) {
		char *sched_params, *tmp_ptr;
		char *sched_type = slurm_get_sched_type();
		char *prio_type = slurm_get_priority_type();
#ifdef HAVE_BG
		/* On BlueGene, do FIFO only with sched/backfill */
		if (xstrcmp(sched_type, "sched/backfill") == 0)
			backfill_sched = true;
#endif
		if ((xstrcmp(sched_type, "sched/builtin") == 0) &&
		    (xstrcmp(prio_type, "priority/basic") == 0) &&
		    _all_partition_priorities_same())
			fifo_sched = true;
		else
			fifo_sched = false;
		xfree(sched_type);
		xfree(prio_type);

		sched_params = slurm_get_sched_params();

		if (sched_params &&
		    (strstr(sched_params, "assoc_limit_stop")))
			assoc_limit_stop = true;
		else
			assoc_limit_stop = false;

		if (sched_params &&
		    (tmp_ptr=strstr(sched_params, "batch_sched_delay="))) {
			batch_sched_delay = atoi(tmp_ptr + 18);
			if (batch_sched_delay < 0) {
				error("Invalid batch_sched_delay: %d",
				      batch_sched_delay);
				batch_sched_delay = 3;
			}
		} else {
			batch_sched_delay = 3;
		}

		bb_array_stage_cnt = 10;
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "bb_array_stage_cnt="))) {
			int task_cnt = atoi(tmp_ptr + 19);
			if (task_cnt > 0)
				bb_array_stage_cnt = task_cnt;
		}

		bf_min_age_reserve = 0;
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "bf_min_age_reserve="))) {
			int min_age = atoi(tmp_ptr + 19);
			if (min_age > 0)
				bf_min_age_reserve = min_age;
		}

		bf_min_prio_reserve = 0;
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "bf_min_prio_reserve="))) {
			int64_t min_prio = (int64_t) atoll(tmp_ptr + 20);
			if (min_prio > 0)
				bf_min_prio_reserve = (uint32_t) min_prio;
		}

		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "build_queue_timeout="))) {
			build_queue_timeout = atoi(tmp_ptr + 20);
			if (build_queue_timeout < 100) {
				error("Invalid build_queue_time: %d",
				      build_queue_timeout);
				build_queue_timeout = BUILD_TIMEOUT;
			}
		} else {
			build_queue_timeout = BUILD_TIMEOUT;
		}

		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "default_queue_depth="))) {
			def_job_limit = atoi(tmp_ptr + 20);
			if (def_job_limit < 0) {
				error("ignoring SchedulerParameters: "
				      "default_queue_depth value of %d",
				      def_job_limit);
				def_job_limit = 100;
			}
		} else {
			def_job_limit = 100;
		}

		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "partition_job_depth="))) {
			max_jobs_per_part = atoi(tmp_ptr + 20);
			if (max_jobs_per_part < 0) {
				error("ignoring SchedulerParameters: "
				      "partition_job_depth value of %d",
				      max_jobs_per_part);
				max_jobs_per_part = 0;
			}
		} else {
			max_jobs_per_part = 0;
		}

		if (sched_params &&
		    (strstr(sched_params, "reduce_completing_frag")))
			reduce_completing_frag = true;
		else
			reduce_completing_frag = false;

		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "max_rpc_cnt=")))
			defer_rpc_cnt = atoi(tmp_ptr + 12);
		else if (sched_params &&
			 (tmp_ptr = strstr(sched_params, "max_rpc_count=")))
			defer_rpc_cnt = atoi(tmp_ptr + 14);
		else
			defer_rpc_cnt = 0;
		if (defer_rpc_cnt < 0) {
			error("Invalid max_rpc_cnt: %d", defer_rpc_cnt);
			defer_rpc_cnt = 0;
		}

		time_limit = slurm_get_msg_timeout() / 2;
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "max_sched_time="))) {
			sched_timeout = atoi(tmp_ptr + 15);
			if ((sched_timeout <= 0) ||
			    (sched_timeout > time_limit)) {
				error("Invalid max_sched_time: %d",
				      sched_timeout);
				sched_timeout = 0;
			}
		} else {
			sched_timeout = 0;
		}
		if (sched_timeout == 0) {
			sched_timeout = MAX(time_limit, 1);
			sched_timeout = MIN(sched_timeout, 2);
		}

		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "sched_interval="))) {
			sched_interval = atoi(tmp_ptr + 15);
			if (sched_interval < 0) {
				error("Invalid sched_interval: %d",
				      sched_interval);
				sched_interval = 60;
			}
		} else {
			sched_interval = 60;
		}

		if (sched_params &&
		    (tmp_ptr=strstr(sched_params, "sched_min_interval="))) {
			i = atoi(tmp_ptr + 19);
			if (i < 0)
				error("Invalid sched_min_interval: %d", i);
			else
				sched_min_interval = i;
		} else {
			sched_min_interval = 2;
		}

		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "sched_max_job_start="))) {
			sched_max_job_start = atoi(tmp_ptr + 20);
			if (sched_max_job_start < 0) {
				error("Invalid sched_max_job_start: %d",
				      sched_max_job_start);
				sched_max_job_start = 0;
			}
		} else {
			sched_max_job_start = 0;
		}

		xfree(sched_params);
		sched_update = slurmctld_conf.last_update;
		info("SchedulerParameters=default_queue_depth=%d,"
		     "max_rpc_cnt=%d,max_sched_time=%d,partition_job_depth=%d,"
		     "sched_max_job_start=%d,sched_min_interval=%d",
		     def_job_limit, defer_rpc_cnt, sched_timeout,
		     max_jobs_per_part, sched_max_job_start,
		     sched_min_interval);
	}

	if ((defer_rpc_cnt > 0) &&
	    (slurmctld_config.server_thread_count >= defer_rpc_cnt)) {
		debug("sched: schedule() returning, too many RPCs");
		goto out;
	}

	if (!fed_mgr_sibs_synced()) {
		debug("sched: schedule() returning, federation siblings not synced yet");
		goto out;
	}

	if (job_limit == 0)
		job_limit = def_job_limit;

	lock_slurmctld(job_write_lock);
	now = time(NULL);
	sched_start = now;
	last_job_sched_start = now;
	START_TIMER;
	if (!avail_front_end(NULL)) {
		ListIterator job_iterator = list_iterator_create(job_list);
		while ((job_ptr = (struct job_record *)
				list_next(job_iterator))) {
			if (!IS_JOB_PENDING(job_ptr))
				continue;
			if ((job_ptr->state_reason != WAIT_NO_REASON) &&
			    (job_ptr->state_reason != WAIT_RESOURCES) &&
			    (job_ptr->state_reason != WAIT_POWER_NOT_AVAIL) &&
			    (job_ptr->state_reason != WAIT_POWER_RESERVED) &&
			    (job_ptr->state_reason != WAIT_NODE_NOT_AVAIL))
				continue;
			job_ptr->state_reason = WAIT_FRONT_END;
		}
		list_iterator_destroy(job_iterator);

		unlock_slurmctld(job_write_lock);
		debug("sched: schedule() returning, no front end nodes are "
		      "available");
		goto out;
	}

	if (!reduce_completing_frag && job_is_completing(NULL)) {
		unlock_slurmctld(job_write_lock);
		debug("sched: schedule() returning, some job is still completing");
		goto out;
	}


#ifdef HAVE_ALPS_CRAY
	/*
	 * Run a Basil Inventory immediately before scheduling, to avoid
	 * race conditions caused by ALPS node state change (caused e.g.
	 * by the node health checker).
	 * This relies on the above write lock for the node state.
	 */
	if (select_g_update_block(NULL)) {
		unlock_slurmctld(job_write_lock);
		debug4("sched: not scheduling due to ALPS");
		goto out;
	}
#endif

	part_cnt = list_count(part_list);
	failed_parts = xmalloc(sizeof(struct part_record *) * part_cnt);
	failed_resv = xmalloc(sizeof(struct slurmctld_resv*) * MAX_FAILED_RESV);
	save_avail_node_bitmap = bit_copy(avail_node_bitmap);
	bit_not(avail_node_bitmap);
	unavail_node_str = bitmap2node_name(avail_node_bitmap);
	bit_not(avail_node_bitmap);
	bit_and_not(avail_node_bitmap, booting_node_bitmap);

	/* Avoid resource fragmentation if important */
	if (reduce_completing_frag) {
		bitstr_t *eff_cg_bitmap = bit_alloc(node_record_count);
		if (job_is_completing(eff_cg_bitmap)) {
			ListIterator part_iterator;
			struct part_record *part_ptr = NULL;
			char *cg_part_str = NULL;

			part_iterator = list_iterator_create(part_list);
			while ((part_ptr = list_next(part_iterator))) {
				if (bit_overlap(eff_cg_bitmap,
						part_ptr->node_bitmap)) {
					failed_parts[failed_part_cnt++] =
						part_ptr;
					bit_and_not(avail_node_bitmap,
						    part_ptr->node_bitmap);
					if (slurmctld_conf.slurmctld_debug >=
					    LOG_LEVEL_DEBUG) {
						if (cg_part_str)
							xstrcat(cg_part_str,
								",");
						xstrfmtcat(cg_part_str, "%s",
							   part_ptr->name);
					}
				}
			}
			list_iterator_destroy(part_iterator);
			if (cg_part_str) {
				debug("sched: some job is still completing, skipping partitions '%s'",
				      cg_part_str);
				xfree(cg_part_str);
			}
		}
		bit_free(eff_cg_bitmap);
	}

	if (max_jobs_per_part) {
		ListIterator part_iterator;
		sched_part_ptr  = xmalloc(sizeof(struct part_record *) *
					  part_cnt);
		sched_part_jobs = xmalloc(sizeof(int) * part_cnt);
		part_iterator = list_iterator_create(part_list);
		i = 0;
		while ((part_ptr = (struct part_record *)
				   list_next(part_iterator))) {
			sched_part_ptr[i++] = part_ptr;
		}
		list_iterator_destroy(part_iterator);
	}

	debug("sched: Running job scheduler");
	/*
	 * If we are doing FIFO scheduling, use the job records right off the
	 * job list.
	 *
	 * If a job is submitted to multiple partitions then build_job_queue()
	 * will return a separate record for each job:partition pair.
	 *
	 * In both cases, we test each partition associated with the job.
	 */
	if (fifo_sched) {
		slurmctld_diag_stats.schedule_queue_len = list_count(job_list);
		job_iterator = list_iterator_create(job_list);
	} else {
		job_queue = build_job_queue(false, false);
		slurmctld_diag_stats.schedule_queue_len = list_count(job_queue);
		sort_job_queue(job_queue);
	}
	while (1) {
		if (fifo_sched) {
			if (job_ptr && part_iterator &&
			    IS_JOB_PENDING(job_ptr)) /* test job in next part */
				goto next_part;
			job_ptr = (struct job_record *) list_next(job_iterator);
			if (!job_ptr)
				break;
			if (!avail_front_end(job_ptr)) {
				job_ptr->state_reason = WAIT_FRONT_END;
				xfree(job_ptr->state_desc);
				last_job_update = now;
				continue;
			}
			if (!_job_runnable_test1(job_ptr, false))
				continue;
			if (job_ptr->part_ptr_list) {
				part_iterator = list_iterator_create(
							job_ptr->part_ptr_list);
next_part:			part_ptr = (struct part_record *)
					   list_next(part_iterator);
				if (part_ptr) {
					job_ptr->part_ptr = part_ptr;
					if (job_limits_check(&job_ptr, false) !=
					    WAIT_NO_REASON)
						continue;
				} else {
					list_iterator_destroy(part_iterator);
					part_iterator = NULL;
					continue;
				}
			} else {
				if (!_job_runnable_test2(job_ptr, false))
					continue;
			}
		} else {
			job_queue_rec = list_pop(job_queue);
			if (!job_queue_rec)
				break;
			array_task_id = job_queue_rec->array_task_id;
			job_ptr  = job_queue_rec->job_ptr;
			part_ptr = job_queue_rec->part_ptr;
			job_ptr->priority = job_queue_rec->priority;
			xfree(job_queue_rec);
			if (!avail_front_end(job_ptr)) {
				job_ptr->state_reason = WAIT_FRONT_END;
				xfree(job_ptr->state_desc);
				last_job_update = now;
				continue;
			}
			if ((job_ptr->array_task_id != array_task_id) &&
			    (array_task_id == NO_VAL)) {
				/* Job array element started in other partition,
				 * reset pointer to "master" job array record */
				job_ptr = find_job_record(job_ptr->array_job_id);
			}
			if (!job_ptr || !IS_JOB_PENDING(job_ptr))
				continue;	/* started in other partition */
			job_ptr->part_ptr = part_ptr;
		}

		job_ptr->last_sched_eval = time(NULL);

		if (job_ptr->preempt_in_progress)
			continue;	/* scheduled in another partition */

		if (job_ptr->pack_job_id) {
			fail_by_part = true;
			goto fail_this_part;
		}

		if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL))
			is_job_array_head = true;
		else
			is_job_array_head = false;

next_task:
		if ((time(NULL) - sched_start) >= sched_timeout) {
			debug("sched: loop taking too long, breaking out");
			break;
		}
		if (sched_max_job_start && (job_cnt >= sched_max_job_start)) {
			debug("sched: sched_max_job_start reached, breaking out");
			break;
		}

		if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
			if ((reject_array_job_id == job_ptr->array_job_id) &&
			    (reject_array_part   == job_ptr->part_ptr)) {
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = reject_state_reason;
				continue;  /* already rejected array element */
			}

			/* assume reject whole array for now, clear if OK */
			reject_array_job_id = job_ptr->array_job_id;
			reject_array_part   = job_ptr->part_ptr;

			if (!job_array_start_test(job_ptr)) {
				reject_state_reason = job_ptr->state_reason;
				continue;
			}
		}
		if (max_jobs_per_part) {
			bool skip_job = false;
			for (j = 0; j < part_cnt; j++) {
				if (sched_part_ptr[j] != job_ptr->part_ptr)
					continue;
				if (sched_part_jobs[j]++ >=
				    max_jobs_per_part)
					skip_job = true;
				break;
			}
			if (skip_job) {
				if (job_ptr->part_ptr == skip_part_ptr)
					continue;
				debug2("sched: reached partition %s job limit",
				       job_ptr->part_ptr->name);
				if (job_ptr->state_reason == WAIT_NO_REASON) {
					xfree(job_ptr->state_desc);
					job_ptr->state_reason = WAIT_PRIORITY;
				}
				skip_part_ptr = job_ptr->part_ptr;
				continue;
			}
		}
		if (job_depth++ > job_limit) {
			debug("sched: already tested %u jobs, breaking out",
			       job_depth);
			break;
		}
		if ((defer_rpc_cnt > 0) &&
		     (slurmctld_config.server_thread_count >= defer_rpc_cnt)) {
			debug("sched: schedule() returning, too many RPCs");
			break;
		}

		slurmctld_diag_stats.schedule_cycle_depth++;

		if (job_ptr->resv_name) {
			bool found_resv = false;
			for (i = 0; i < failed_resv_cnt; i++) {
				if (failed_resv[i] == job_ptr->resv_ptr) {
					found_resv = true;
					break;
				}
			}
			if (found_resv) {
				job_ptr->state_reason = WAIT_PRIORITY;
				xfree(job_ptr->state_desc);
				debug3("sched: JobId=%u. State=PENDING. "
				       "Reason=Priority. Priority=%u. "
				       "Resv=%s.",
				       job_ptr->job_id, job_ptr->priority,
				       job_ptr->resv_name);
				continue;
			}
		} else if (_failed_partition(job_ptr->part_ptr, failed_parts,
					     failed_part_cnt)) {
			job_ptr->state_reason = WAIT_PRIORITY;
			xfree(job_ptr->state_desc);
			last_job_update = now;
			debug("sched: JobId=%u. State=PENDING. "
			       "Reason=Priority, Priority=%u. Partition=%s.",
			       job_ptr->job_id, job_ptr->priority,
			       job_ptr->partition);
			continue;
		}

		/* Test for valid account, QOS and required nodes on each pass */
		if (job_ptr->state_reason == FAIL_ACCOUNT) {
			slurmdb_assoc_rec_t assoc_rec;
			memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));
			assoc_rec.acct      = job_ptr->account;
			if (job_ptr->part_ptr)
				assoc_rec.partition = job_ptr->part_ptr->name;
			assoc_rec.uid       = job_ptr->user_id;

			if (!assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
						     accounting_enforce,
						     &job_ptr->assoc_ptr,
						     false)) {
				job_ptr->state_reason = WAIT_NO_REASON;
				xfree(job_ptr->state_desc);
				job_ptr->assoc_id = assoc_rec.id;
				last_job_update = now;
			} else {
				debug("sched: JobId=%u has invalid association",
				      job_ptr->job_id);
				xfree(job_ptr->state_desc);
				job_ptr->state_reason =
					WAIT_ASSOC_RESOURCE_LIMIT;
				continue;
			}
		}
		if (job_ptr->qos_id) {
			assoc_mgr_lock_t locks = {
				READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				NO_LOCK, NO_LOCK, NO_LOCK };

			assoc_mgr_lock(&locks);
			if (job_ptr->assoc_ptr
			    && (accounting_enforce & ACCOUNTING_ENFORCE_QOS)
			    && ((job_ptr->qos_id >= g_qos_count) ||
				!bit_test(job_ptr->assoc_ptr->usage->valid_qos,
					  job_ptr->qos_id))
			    && !job_ptr->limit_set.qos) {
				debug("sched: JobId=%u has invalid QOS",
				      job_ptr->job_id);
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = FAIL_QOS;
				last_job_update = now;
				assoc_mgr_unlock(&locks);
				continue;
			} else if (job_ptr->state_reason == FAIL_QOS) {
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = WAIT_NO_REASON;
				last_job_update = now;
			}
			assoc_mgr_unlock(&locks);
		}

		deadline_time_limit = 0;
		if ((job_ptr->deadline) && (job_ptr->deadline != NO_VAL)) {
			if (!deadline_ok(job_ptr, "sched"))
				continue;

			deadline_time_limit = job_ptr->deadline - now;
			deadline_time_limit /= 60;
			if ((job_ptr->time_limit != NO_VAL) &&
			    (job_ptr->time_limit != INFINITE)) {
				deadline_time_limit = MIN(job_ptr->time_limit,
							  deadline_time_limit);
			} else {
				if ((job_ptr->part_ptr->default_time != NO_VAL) &&
				    (job_ptr->part_ptr->default_time != INFINITE)){
					deadline_time_limit = MIN(
						job_ptr->part_ptr->default_time,
						deadline_time_limit);
				} else if ((job_ptr->part_ptr->max_time != NO_VAL) &&
					   (job_ptr->part_ptr->max_time != INFINITE)){
					deadline_time_limit = MIN(
						job_ptr->part_ptr->max_time,
						deadline_time_limit);
				}
			}
		}

		if (!acct_policy_job_runnable_state(job_ptr) &&
		    !acct_policy_job_runnable_pre_select(job_ptr, false))
			continue;

		if ((job_ptr->state_reason == WAIT_NODE_NOT_AVAIL) &&
		    job_ptr->details && job_ptr->details->req_node_bitmap &&
		    !bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_node_bitmap)) {
			continue;
		}

		if (!job_ptr->part_ptr)
			continue;
		i = bit_overlap(avail_node_bitmap,
				job_ptr->part_ptr->node_bitmap);
		if ((job_ptr->details &&
		    (job_ptr->details->min_nodes != NO_VAL) &&
		    (job_ptr->details->min_nodes >  i)) ||
		    (!job_ptr->details && (i == 0))) {
			/* Too many nodes DRAIN, DOWN, or
			 * reserved for jobs in higher priority partition */
			job_ptr->state_reason = WAIT_RESOURCES;
			xfree(job_ptr->state_desc);
			last_job_update = now;
			debug3("sched: JobId=%u. State=%s. Reason=%s. "
			       "Priority=%u. Partition=%s.",
			       job_ptr->job_id,
			       job_state_string(job_ptr->job_state),
			       job_reason_string(job_ptr->state_reason),
			       job_ptr->priority,
			       job_ptr->partition);
			continue;
		}
		if (license_job_test(job_ptr, time(NULL), true) !=
		    SLURM_SUCCESS) {
			job_ptr->state_reason = WAIT_LICENSES;
			xfree(job_ptr->state_desc);
			last_job_update = now;
			debug3("sched: JobId=%u. State=%s. Reason=%s. "
			       "Priority=%u.",
			       job_ptr->job_id,
			       job_state_string(job_ptr->job_state),
			       job_reason_string(job_ptr->state_reason),
			       job_ptr->priority);
			continue;
		}

		if (assoc_mgr_validate_assoc_id(acct_db_conn,
						job_ptr->assoc_id,
						accounting_enforce)) {
			/* NOTE: This only happens if a user's account is
			 * disabled between when the job was submitted and
			 * the time we consider running it. It should be
			 * very rare. */
			info("sched: JobId=%u has invalid account",
			     job_ptr->job_id);
			last_job_update = now;
			job_ptr->state_reason = FAIL_ACCOUNT;
			xfree(job_ptr->state_desc);
			continue;
		}

		last_job_sched_start = MAX(last_job_sched_start,
					   job_ptr->start_time);
		if (deadline_time_limit) {
			save_time_limit = job_ptr->time_limit;
			job_ptr->time_limit = deadline_time_limit;
		}

		/* get fed job lock from origin cluster */
		if (fed_mgr_job_lock(job_ptr)) {
			error_code = ESLURM_FED_JOB_LOCK;
			goto skip_start;
		}

		error_code = select_nodes(job_ptr, false, NULL,
					  unavail_node_str, NULL);

		if (error_code == SLURM_SUCCESS) {
			/* If the following fails because of network
			 * connectivity, the origin cluster should ask
			 * when it comes back up if the cluster_lock
			 * cluster actually started the job */
			fed_mgr_job_start(job_ptr, job_ptr->start_time);
		} else {
			fed_mgr_job_unlock(job_ptr);
		}

skip_start:

		fail_by_part = false;
		if ((error_code != SLURM_SUCCESS) && deadline_time_limit)
			job_ptr->time_limit = save_time_limit;
		if ((error_code == ESLURM_NODES_BUSY) ||
		    (error_code == ESLURM_POWER_NOT_AVAIL) ||
		    (error_code == ESLURM_POWER_RESERVED)) {
			debug3("sched: JobId=%u. State=%s. Reason=%s. "
			       "Priority=%u. Partition=%s.",
			       job_ptr->job_id,
			       job_state_string(job_ptr->job_state),
			       job_reason_string(job_ptr->state_reason),
			       job_ptr->priority, job_ptr->partition);
			fail_by_part = true;
		} else if (error_code == ESLURM_BURST_BUFFER_WAIT) {
			if (job_ptr->start_time == 0) {
				job_ptr->start_time = last_job_sched_start;
				bb_wait_cnt++;
			}
			debug3("sched: JobId=%u. State=%s. Reason=%s. "
			       "Priority=%u.",
			       job_ptr->job_id,
			       job_state_string(job_ptr->job_state),
			       job_reason_string(job_ptr->state_reason),
			       job_ptr->priority);
			continue;
		} else if ((error_code == ESLURM_RESERVATION_BUSY) ||
			   (error_code == ESLURM_RESERVATION_NOT_USABLE)) {
			if (job_ptr->resv_ptr &&
			    job_ptr->resv_ptr->node_bitmap) {
				debug3("sched: JobId=%u. State=%s. "
				       "Reason=%s. Priority=%u.",
				       job_ptr->job_id,
				       job_state_string(job_ptr->job_state),
				       job_reason_string(job_ptr->
							 state_reason),
				       job_ptr->priority);
				bit_and_not(avail_node_bitmap,
					job_ptr->resv_ptr->node_bitmap);
			} else {
				/* The job has no reservation but requires
				 * nodes that are currently in some reservation
				 * so just skip over this job and try running
				 * the next lower priority job */
				debug3("sched: JobId=%u State=%s. "
				       "Reason=Required nodes are reserved."
				       "Priority=%u",job_ptr->job_id,
				       job_state_string(job_ptr->job_state),
				       job_ptr->priority);
			}
		} else if (error_code == ESLURM_FED_JOB_LOCK) {
			job_ptr->state_reason = WAIT_FED_JOB_LOCK;
			xfree(job_ptr->state_desc);
			last_job_update = now;
			debug3("sched: JobId=%u. State=%s. Reason=%s. "
			       "Priority=%u. Partition=%s.",
			       job_ptr->job_id,
			       job_state_string(job_ptr->job_state),
			       job_reason_string(job_ptr->state_reason),
			       job_ptr->priority, job_ptr->partition);
			fail_by_part = true;
		} else if (error_code == SLURM_SUCCESS) {
			/* job initiated */
			debug3("sched: JobId=%u initiated", job_ptr->job_id);
			last_job_update = now;
			reject_array_job_id = 0;
			reject_array_part   = NULL;

			/* synchronize power layouts key/values */
			if ((powercap_get_cluster_current_cap() != 0) &&
			    (which_power_layout() == 2)) {
				layouts_entity_pull_kv("power",
						       "Cluster",
						       "CurrentSumPower");
			}
#ifdef HAVE_BG
			select_g_select_jobinfo_get(job_ptr->select_jobinfo,
						    SELECT_JOBDATA_IONODES,
						    &ionodes);
			if (ionodes) {
				sprintf(tmp_char,"%s[%s]",
					job_ptr->nodes, ionodes);
			} else {
				sprintf(tmp_char,"%s",job_ptr->nodes);
			}

			info("sched: Allocate %s MidplaneList=%s Partition=%s",
			     jobid2fmt(job_ptr, job_id_buf, sizeof(job_id_buf)),
			     tmp_char, job_ptr->part_ptr->name);
			xfree(ionodes);
#else
			info("sched: Allocate %s NodeList=%s #CPUs=%u "
			     "Partition=%s",
			     jobid2fmt(job_ptr, job_id_buf, sizeof(job_id_buf)),
			     job_ptr->nodes, job_ptr->total_cpus,
			     job_ptr->part_ptr->name);
#endif
			if (job_ptr->batch_flag == 0)
				srun_allocate(job_ptr->job_id);
			else if (
#ifdef HAVE_BG
				/* On a bluegene system we need to run the
				 * prolog while the job is CONFIGURING so this
				 * can't work off the CONFIGURING flag as done
				 * elsewhere.
				 */
				!job_ptr->details->prolog_running &&
				!(job_ptr->bit_flags & NODE_REBOOT)
#else
				!IS_JOB_CONFIGURING(job_ptr)
#endif
				)
				launch_job(job_ptr);
			rebuild_job_part_list(job_ptr);
			job_cnt++;
			if (is_job_array_head &&
			    (job_ptr->array_task_id != NO_VAL)) {
				/* Try starting another task of the job array */
				job_ptr = find_job_record(job_ptr->array_job_id);
				if (job_ptr && IS_JOB_PENDING(job_ptr) &&
				    (bb_g_job_test_stage_in(job_ptr,false) ==1))
					goto next_task;
			}
			continue;
		} else if ((error_code ==
			    ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			   job_ptr->part_ptr_list) {
			debug("JobId=%u non-runnable in partition %s: %s",
			      job_ptr->job_id, job_ptr->part_ptr->name,
			      slurm_strerror(error_code));
		} else if (error_code == ESLURM_ACCOUNTING_POLICY) {
			debug3("sched: JobId=%u delayed for accounting policy",
			       job_ptr->job_id);
			/* potentially starve this job */
			if (assoc_limit_stop)
				fail_by_part = true;
		} else if ((error_code !=
			    ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
			   (error_code != ESLURM_NODE_NOT_AVAIL)      &&
			   (error_code != ESLURM_INVALID_BURST_BUFFER_REQUEST)){
			info("sched: schedule: %s non-runnable: %s",
			     jobid2str(job_ptr, jbuf, sizeof(jbuf)),
			     slurm_strerror(error_code));
			last_job_update = now;
			job_ptr->job_state = JOB_PENDING;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_ptr->priority = 0;
		}

#ifdef HAVE_BG
		/* When we use static or overlap partitioning on BlueGene,
		 * each job can possibly be scheduled independently, without
		 * impacting other jobs of different sizes. Therefore we sort
		 * and try to schedule every pending job unless the backfill
		 * scheduler is configured. */
		if (!backfill_sched)
			fail_by_part = false;
#else
		if (job_ptr->details && job_ptr->details->req_node_bitmap &&
		    (bit_set_count(job_ptr->details->req_node_bitmap) >=
		     job_ptr->details->min_nodes)) {
			fail_by_part = false;
			/* Do not schedule more jobs on nodes required by this
			 * job, but don't block the entire queue/partition. */
			bit_and_not(avail_node_bitmap,
				job_ptr->details->req_node_bitmap);
		}
#endif
		if (fail_by_part && job_ptr->resv_name) {
		 	/* do not schedule more jobs in this reservation, but
			 * other jobs in this partition can be scheduled. */
			fail_by_part = false;
			if (failed_resv_cnt < MAX_FAILED_RESV) {
				failed_resv[failed_resv_cnt++] =
					job_ptr->resv_ptr;
			}
		}
		if (fail_by_part && bf_min_age_reserve) {
			/* Consider other jobs in this partition if job has been
			 * waiting for less than bf_min_age_reserve time */
			if (job_ptr->details->begin_time == 0) {
				fail_by_part = false;
			} else {
				pend_time = difftime(now,
					job_ptr->details->begin_time);
				if (pend_time < bf_min_age_reserve)
					fail_by_part = false;
			}
		}
		if (fail_by_part && bf_min_prio_reserve &&
		    (job_ptr->priority < bf_min_prio_reserve))
			fail_by_part = false;

fail_this_part:	if (fail_by_part) {
			/* Search for duplicates */
			for (i = 0; i < failed_part_cnt; i++) {
				if (failed_parts[i] == job_ptr->part_ptr) {
					fail_by_part = false;
					break;
				}
			}
		}
		if (fail_by_part) {
			/*
			 * Do not schedule more jobs in this partition or on
			 * nodes in this partition
			 */
			failed_parts[failed_part_cnt++] = job_ptr->part_ptr;
			bit_and_not(avail_node_bitmap,
				job_ptr->part_ptr->node_bitmap);
		}

		if ((reject_array_job_id == job_ptr->array_job_id) &&
		    (reject_array_part   == job_ptr->part_ptr)) {
			/* All other elements of this job array get the
			 * same reason */
			reject_state_reason = job_ptr->state_reason;
		}
	}

	if (bb_wait_cnt)
		(void) bb_g_job_try_stage_in();

	save_last_part_update = last_part_update;
	FREE_NULL_BITMAP(avail_node_bitmap);
	avail_node_bitmap = save_avail_node_bitmap;
	xfree(unavail_node_str);
	xfree(failed_parts);
	xfree(failed_resv);
	if (fifo_sched) {
		if (job_iterator)
			list_iterator_destroy(job_iterator);
		if (part_iterator)
			list_iterator_destroy(part_iterator);
	} else if (job_queue) {
		FREE_NULL_LIST(job_queue);
	}
	xfree(sched_part_ptr);
	xfree(sched_part_jobs);
	if ((slurmctld_config.server_thread_count >= 150) &&
	    (defer_rpc_cnt == 0)) {
		info("sched: %d pending RPCs at cycle end, consider "
		     "configuring max_rpc_cnt",
		     slurmctld_config.server_thread_count);
	}
	unlock_slurmctld(job_write_lock);
	END_TIMER2("schedule");

	_do_diag_stats(DELTA_TIMER);

out:
#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, get_name, NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m",
		      __func__, get_name);
	}
#endif
	return job_cnt;
}

/*
 * sort_job_queue - sort job_queue in descending priority order
 * IN/OUT job_queue - sorted job queue
 */
extern void sort_job_queue(List job_queue)
{
	list_sort(job_queue, sort_job_queue2);
}

/* Note this differs from the ListCmpF typedef since we want jobs sorted
 * in order of decreasing priority then submit time and the by increasing
 * job id */
extern int sort_job_queue2(void *x, void *y)
{
	job_queue_rec_t *job_rec1 = *(job_queue_rec_t **) x;
	job_queue_rec_t *job_rec2 = *(job_queue_rec_t **) y;
	bool has_resv1, has_resv2;
	static time_t config_update = 0;
	static bool preemption_enabled = true;
	uint32_t job_id1, job_id2;
	uint32_t p1, p2;

	/* The following block of code is designed to minimize run time in
	 * typical configurations for this frequently executed function. */
	if (config_update != slurmctld_conf.last_update) {
		preemption_enabled = slurm_preemption_enabled();
		config_update = slurmctld_conf.last_update;
	}
	if (preemption_enabled) {
		if (slurm_job_preempt_check(job_rec1, job_rec2))
			return -1;
		if (slurm_job_preempt_check(job_rec2, job_rec1))
			return 1;
	}

	has_resv1 = (job_rec1->job_ptr->resv_id != 0);
	has_resv2 = (job_rec2->job_ptr->resv_id != 0);
	if (has_resv1 && !has_resv2)
		return -1;
	if (!has_resv1 && has_resv2)
		return 1;

	if (job_rec1->part_ptr && job_rec2->part_ptr) {
		p1 = job_rec1->part_ptr->priority_tier;
		p2 = job_rec2->part_ptr->priority_tier;
		if (p1 < p2)
			return 1;
		if (p1 > p2)
			return -1;
	}

	if (job_rec1->job_ptr->part_ptr_list &&
	    job_rec1->job_ptr->priority_array)
		p1 = job_rec1->priority;
	else
		p1 = job_rec1->job_ptr->priority;


	if (job_rec2->job_ptr->part_ptr_list &&
	    job_rec2->job_ptr->priority_array)
		p2 = job_rec2->priority;
	else
		p2 = job_rec2->job_ptr->priority;


	if (p1 < p2)
		return 1;
	if (p1 > p2)
		return -1;

	/* If the priorities are the same sort by submission time */
	if (job_rec1->job_ptr->details && job_rec2->job_ptr->details) {
		if (job_rec1->job_ptr->details->submit_time >
		    job_rec2->job_ptr->details->submit_time)
			return 1;
		if (job_rec2->job_ptr->details->submit_time >
		    job_rec1->job_ptr->details->submit_time)
			return -1;
	}

	/* If the submission times are the same sort by increasing job id's */
	if (job_rec1->array_task_id == NO_VAL)
		job_id1 = job_rec1->job_id;
	else
		job_id1 = job_rec1->job_ptr->array_job_id;
	if (job_rec2->array_task_id == NO_VAL)
		job_id2 = job_rec2->job_id;
	else
		job_id2 = job_rec2->job_ptr->array_job_id;
	if (job_id1 > job_id2)
		return 1;
	else if (job_id1 < job_id2)
		return -1;

	/* If job IDs match compare task IDs */
	if (job_rec1->array_task_id > job_rec2->array_task_id)
		return 1;

	return -1;
}

/* The environment" variable is points to one big xmalloc. In order to
 * manipulate the array for a pack job, we need to split it into an array
 * containing multiple xmalloc variables */
static void _split_env(batch_job_launch_msg_t *launch_msg_ptr)
{
	int i;

	for (i = 1; i < launch_msg_ptr->envc; i++) {
		launch_msg_ptr->environment[i] =
			xstrdup(launch_msg_ptr->environment[i]);
	}
}

/* Given a scheduled job, return a pointer to it batch_job_launch_msg_t data */
static batch_job_launch_msg_t *_build_launch_job_msg(struct job_record *job_ptr,
						     uint16_t protocol_version)
{
	batch_job_launch_msg_t *launch_msg_ptr;

	/* Initialization of data structures */
	launch_msg_ptr = (batch_job_launch_msg_t *)
				xmalloc(sizeof(batch_job_launch_msg_t));
	launch_msg_ptr->job_id = job_ptr->job_id;
	launch_msg_ptr->step_id = NO_VAL;
	launch_msg_ptr->array_job_id = job_ptr->array_job_id;
	launch_msg_ptr->array_task_id = job_ptr->array_task_id;
	launch_msg_ptr->uid = job_ptr->user_id;
	launch_msg_ptr->gid = job_ptr->group_id;

	if (slurmctld_config.send_groups_in_cred) {
		/* fill in the job_record field if not yet filled in */
		if (!job_ptr->user_name)
			job_ptr->user_name = uid_to_string_or_null(job_ptr->user_id);
		/* this may still be null, in which case the client will handle */
		launch_msg_ptr->user_name = xstrdup(job_ptr->user_name);
		/* lookup and send extended gids list */
		if (!job_ptr->ngids || !job_ptr->gids)
			job_ptr->ngids = group_cache_lookup(job_ptr->user_id,
							    job_ptr->group_id,
							    job_ptr->user_name,
							    &job_ptr->gids);
		launch_msg_ptr->ngids = job_ptr->ngids;
		launch_msg_ptr->gids = copy_gids(job_ptr->ngids, job_ptr->gids);
	}

	if ((launch_msg_ptr->script = get_job_script(job_ptr)) == NULL) {
		error("Can not find batch script, Aborting batch job %u",
		      job_ptr->job_id);
		/* FIXME: This is a kludge, but this event indicates a missing
		 * batch script and should never happen. We are too deep into
		 * the job launch to gracefully clean up here. */
		slurm_free_job_launch_msg(launch_msg_ptr);
		job_complete(job_ptr->job_id, slurmctld_conf.slurm_user_id,
			     true, false, 0);
		return NULL;
	}


	launch_msg_ptr->ntasks = job_ptr->details->num_tasks;
	launch_msg_ptr->alias_list = xstrdup(job_ptr->alias_list);
	launch_msg_ptr->nodes = xstrdup(job_ptr->nodes);
	launch_msg_ptr->overcommit = job_ptr->details->overcommit;
	launch_msg_ptr->open_mode  = job_ptr->details->open_mode;
	launch_msg_ptr->cpus_per_task = job_ptr->details->cpus_per_task;
	launch_msg_ptr->pn_min_memory = job_ptr->details->pn_min_memory;
	launch_msg_ptr->restart_cnt   = job_ptr->restart_cnt;
	launch_msg_ptr->profile       = job_ptr->profile;

	if (make_batch_job_cred(launch_msg_ptr, job_ptr, protocol_version)) {
		/* FIXME: This is a kludge, but this event indicates a serious
		 * problem with Munge or OpenSSH and should never happen. We
		 * are too deep into the job launch to gracefully clean up from
		 * from the launch, so requeue if possible. */
		error("Can not create job credential, attempting to requeue "
		      "batch job %u", job_ptr->job_id);
		slurm_free_job_launch_msg(launch_msg_ptr);
		job_ptr->batch_flag = 1;	/* Allow repeated requeue */
		job_ptr->details->begin_time = time(NULL) + 120;
		job_complete(job_ptr->job_id, slurmctld_conf.slurm_user_id,
			     true, false, 0);
		return NULL;
	}

	launch_msg_ptr->acctg_freq = xstrdup(job_ptr->details->acctg_freq);
	if (job_ptr->part_ptr)
		launch_msg_ptr->partition = xstrdup(job_ptr->part_ptr->name);
	else
		launch_msg_ptr->partition = xstrdup(job_ptr->partition);
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
	launch_msg_ptr->environment = get_job_env(job_ptr,
						  &launch_msg_ptr->envc);
	if (launch_msg_ptr->environment == NULL) {
		error("%s: environment missing or corrupted aborting job %u",
		      __func__, job_ptr->job_id);
		slurm_free_job_launch_msg(launch_msg_ptr);
		job_complete(job_ptr->job_id, slurmctld_conf.slurm_user_id,
			     false, true, 0);
		return NULL;
	}
	_split_env(launch_msg_ptr);
	launch_msg_ptr->job_mem = job_ptr->details->pn_min_memory;
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

	if (job_ptr->account) {
		launch_msg_ptr->account = xstrdup(job_ptr->account);
	}
	if (job_ptr->qos_ptr) {
		if (!xstrcmp(job_ptr->qos_ptr->description,
			     "Normal QOS default"))
			launch_msg_ptr->qos = xstrdup("normal");
		else
			launch_msg_ptr->qos = xstrdup(
				job_ptr->qos_ptr->description);
	}
	if (job_ptr->resv_name) {
		launch_msg_ptr->resv_name = xstrdup(job_ptr->resv_name);
	}

	return launch_msg_ptr;
}

/* Validate the job is ready for launch
 * RET pointer to batch job to launch or NULL if not ready yet */
static struct job_record *_pack_job_ready(struct job_record *job_ptr)
{
	struct job_record *pack_leader, *pack_job;
	ListIterator iter;

	if (job_ptr->pack_job_id == 0)	/* Not a pack job */
		return job_ptr;

	pack_leader = find_job_record(job_ptr->pack_job_id);
	if (!pack_leader) {
		error("Job pack leader %u not found", job_ptr->pack_job_id);
		return NULL;
	}
	if (!pack_leader->pack_job_list) {
		error("Job pack leader %u lacks pack_job_list",
		      job_ptr->pack_job_id);
		return NULL;
	}

	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
#ifndef HAVE_BG
		uint8_t prolog = 0;
#endif
		if (pack_leader->pack_job_id != pack_job->pack_job_id) {
			error("%s: Bad pack_job_list for job %u",
			      __func__, pack_leader->pack_job_id);
			continue;
		}
#ifndef HAVE_BG
		if (job_ptr->details)
			prolog = pack_job->details->prolog_running;
		if (prolog || IS_JOB_CONFIGURING(pack_job) ||
		    !test_job_nodes_ready(pack_job)) {
			pack_leader = NULL;
			break;
		}
#endif
		if ((job_ptr->batch_flag == 0) ||
		    (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))) {
			pack_leader = NULL;
			break;
		}
	}
	list_iterator_destroy(iter);

	if (slurmctld_conf.debug_flags & DEBUG_FLAG_HETERO_JOBS) {
		if (pack_leader) {
			info("Batch pack job %u being launched",
			     pack_leader->job_id);
		} else if (pack_job) {
			info("Batch pack job %u waiting for job %u to be ready",
			     pack_job->pack_job_id, pack_job->job_id);
		}
	}

	return pack_leader;
}

/*
 * Set some pack job environment variables. This will include information
 * about multple job components (i.e. different slurmctld job records).
 */
static void _set_pack_env(struct job_record *pack_leader,
			  batch_job_launch_msg_t *launch_msg_ptr)
{
	struct job_record *pack_job;
	int i, pack_offset = 0;
	ListIterator iter;

	if (pack_leader->pack_job_id == 0)
		return;
	if (!launch_msg_ptr->environment) {
		error("Job %u lacks environment", pack_leader->pack_job_id);
		return;
	}
	if (!pack_leader->pack_job_list) {
		error("Job pack leader %u lacks pack_job_list",
		      pack_leader->pack_job_id);
		return;
	}

	/* "environment" needs NULL terminator */
	xrealloc(launch_msg_ptr->environment,
		 sizeof(char *) * (launch_msg_ptr->envc + 1));
	iter = list_iterator_create(pack_leader->pack_job_list);
	while ((pack_job = (struct job_record *) list_next(iter))) {
		uint16_t cpus_per_task = 1;
		uint32_t num_cpus = 0;
		uint64_t tmp_mem = 0;
		char *tmp_str = NULL;

		if (pack_leader->pack_job_id != pack_job->pack_job_id) {
			error("%s: Bad pack_job_list for job %u",
			      __func__, pack_leader->pack_job_id);
			continue;
		}
#if HAVE_BG
		(void) env_array_overwrite_pack_fmt(
				&launch_msg_ptr->environment,
				"SLURM_BG_NUM_NODES", pack_offset,
				"%u", pack_job->node_cnt);
#endif
		if (pack_job->details &&
		    (pack_job->details->cpus_per_task > 0) &&
		    (pack_job->details->cpus_per_task != NO_VAL16)) {
			cpus_per_task = pack_job->details->cpus_per_task;
		}
		if (pack_job->account) {
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_JOB_ACCOUNT",
					pack_offset, "%s", pack_job->account);
		}

		if (pack_job->job_resrcs) {
			tmp_str = uint32_compressed_to_str(
					pack_job->job_resrcs->cpu_array_cnt,
					pack_job->job_resrcs->cpu_array_value,
					pack_job->job_resrcs->cpu_array_reps);
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_JOB_CPUS_PER_NODE",
					pack_offset, "%s", tmp_str);
			xfree(tmp_str);
		}
		(void) env_array_overwrite_pack_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_ID",
				pack_offset, "%u", pack_job->job_id);
		(void) env_array_overwrite_pack_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_NAME",
				pack_offset, "%s", pack_job->name);
		(void) env_array_overwrite_pack_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_NODELIST",
				pack_offset, "%s", pack_job->nodes);
		(void) env_array_overwrite_pack_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_NUM_NODES",
				pack_offset, "%u", pack_job->node_cnt);
		if (pack_job->partition) {
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_JOB_PARTITION",
					pack_offset, "%s", pack_job->partition);
		}
		if (pack_job->qos_ptr) {
			slurmdb_qos_rec_t *qos;
			char *qos_name;

			qos = (slurmdb_qos_rec_t *) pack_job->qos_ptr;
			if (!xstrcmp(qos->description, "Normal QOS default"))
				qos_name = "normal";
			else
				qos_name = qos->description;
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_JOB_QOS",
					pack_offset, "%s", qos_name);
		}
		if (pack_job->resv_name) {
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_JOB_RESERVATION",
					pack_offset, "%s", pack_job->resv_name);
		}
		if (pack_job->details)
			tmp_mem = pack_job->details->pn_min_memory;
		if (tmp_mem & MEM_PER_CPU) {
			tmp_mem &= (~MEM_PER_CPU);
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_MEM_PER_CPU",
					pack_offset, "%"PRIu64"", tmp_mem);
		} else if (tmp_mem) {
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_MEM_PER_NODE",
					pack_offset, "%"PRIu64"", tmp_mem);
		}
		if (pack_job->alias_list) {
			(void) env_array_overwrite_pack_fmt(
					&launch_msg_ptr->environment,
					"SLURM_NODE_ALIASES", pack_offset,
					"%s", pack_job->alias_list);
		}
		if (pack_job->details && pack_job->job_resrcs) {
			/* Both should always be set for active jobs */
			struct job_resources *resrcs_ptr = pack_job->job_resrcs;
			slurm_step_layout_t *step_layout = NULL;
			slurm_step_layout_req_t step_layout_req;
			uint16_t cpus_per_task_array[1];
			uint32_t cpus_task_reps[1], task_dist;
			memset(&step_layout_req, 0,
			       sizeof(slurm_step_layout_req_t));
			for (i = 0; i < resrcs_ptr->cpu_array_cnt; i++) {
				num_cpus += resrcs_ptr->cpu_array_value[i] *
					    resrcs_ptr->cpu_array_reps[i];
			}

			if (pack_job->details->num_tasks) {
				step_layout_req.num_tasks =
					pack_job->details->num_tasks;
			} else {
				step_layout_req.num_tasks = num_cpus / cpus_per_task;
			}
			step_layout_req.num_hosts = pack_job->node_cnt;

			if ((step_layout_req.node_list =
			     getenvp(launch_msg_ptr->environment,
				     "SLURM_ARBITRARY_NODELIST"))) {
				task_dist = SLURM_DIST_ARBITRARY;
			} else {
				step_layout_req.node_list = pack_job->nodes;
				task_dist = SLURM_DIST_BLOCK;
			}
			step_layout_req.cpus_per_node =
				pack_job->job_resrcs->cpu_array_value;
			step_layout_req.cpu_count_reps =
				pack_job->job_resrcs->cpu_array_reps;
			cpus_per_task_array[0] = cpus_per_task;
			step_layout_req.cpus_per_task = cpus_per_task_array;
			cpus_task_reps[0] = pack_job->node_cnt;
			step_layout_req.cpus_task_reps = cpus_task_reps;
			step_layout_req.task_dist = task_dist;
			step_layout_req.plane_size = NO_VAL16;
			step_layout = slurm_step_layout_create(&step_layout_req);
			if (step_layout) {
				tmp_str = uint16_array_to_str(
							step_layout->node_cnt,
							step_layout->tasks);
				slurm_step_layout_destroy(step_layout);
				(void) env_array_overwrite_pack_fmt(
						&launch_msg_ptr->environment,
						"SLURM_TASKS_PER_NODE",
						 pack_offset,"%s", tmp_str);
				xfree(tmp_str);
			}
		}
		pack_offset++;
	}
	list_iterator_destroy(iter);
	(void) env_array_overwrite_fmt(&launch_msg_ptr->environment,
				       "SLURM_PACK_SIZE", "%d", pack_offset);

	for (i = 0; launch_msg_ptr->environment[i]; i++)
		;
	launch_msg_ptr->envc = i;
}

/*
 * launch_job - send an RPC to a slurmd to initiate a batch job
 * IN job_ptr - pointer to job that will be initiated
 */
extern void launch_job(struct job_record *job_ptr)
{
	batch_job_launch_msg_t *launch_msg_ptr;
	uint16_t protocol_version = NO_VAL16;
	agent_arg_t *agent_arg_ptr;
	struct job_record *launch_job_ptr;

#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
	front_end_ptr = find_front_end_record(job_ptr->batch_host);
	if (front_end_ptr)
		protocol_version = front_end_ptr->protocol_version;
#else
	struct node_record *node_ptr;
	node_ptr = find_node_record(job_ptr->batch_host);
	if (node_ptr)
		protocol_version = node_ptr->protocol_version;
#endif
	launch_job_ptr = _pack_job_ready(job_ptr);
	if (!launch_job_ptr)
		return;

	launch_msg_ptr = _build_launch_job_msg(launch_job_ptr,protocol_version);
	if (launch_msg_ptr == NULL)
		return;
	if (launch_job_ptr->pack_job_id)
		_set_pack_env(launch_job_ptr, launch_msg_ptr);

	agent_arg_ptr = (agent_arg_t *) xmalloc(sizeof(agent_arg_t));
	agent_arg_ptr->protocol_version = protocol_version;
	agent_arg_ptr->node_count = 1;
	agent_arg_ptr->retry = 0;
	xassert(job_ptr->batch_host);
	agent_arg_ptr->hostlist = hostlist_create(launch_job_ptr->batch_host);
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
			       struct job_record *job_ptr,
			       uint16_t protocol_version)
{
	slurm_cred_arg_t cred_arg;
	job_resources_t *job_resrcs_ptr;

	xassert(job_ptr->job_resrcs);
	job_resrcs_ptr = job_ptr->job_resrcs;

	if (job_ptr->job_resrcs == NULL) {
		error("%s: job %u is missing job_resrcs info",
		      __func__, job_ptr->job_id);
		return SLURM_ERROR;
	}

	memset(&cred_arg, 0, sizeof(slurm_cred_arg_t));

	cred_arg.jobid     = launch_msg_ptr->job_id;
	cred_arg.stepid    = launch_msg_ptr->step_id;
	cred_arg.uid       = launch_msg_ptr->uid;
	cred_arg.gid       = launch_msg_ptr->gid;
	cred_arg.user_name = launch_msg_ptr->user_name;
	cred_arg.x11       = job_ptr->details->x11;
	cred_arg.job_constraints     = job_ptr->details->features;
	cred_arg.job_hostlist        = job_resrcs_ptr->nodes;
	cred_arg.job_core_bitmap     = job_resrcs_ptr->core_bitmap;
	cred_arg.job_core_spec       = job_ptr->details->core_spec;
	cred_arg.job_mem_limit       = job_ptr->details->pn_min_memory;
	cred_arg.job_nhosts          = job_resrcs_ptr->nhosts;
	cred_arg.job_gres_list       = job_ptr->gres_list;
/*	cred_arg.step_gres_list      = NULL; */

#ifdef HAVE_FRONT_END
	xassert(job_ptr->batch_host);
	cred_arg.step_hostlist       = job_ptr->batch_host;
#else
	cred_arg.step_hostlist       = launch_msg_ptr->nodes;
#endif
	cred_arg.step_core_bitmap    = job_resrcs_ptr->core_bitmap;
	cred_arg.step_mem_limit      = job_ptr->details->pn_min_memory;

	cred_arg.cores_per_socket    = job_resrcs_ptr->cores_per_socket;
	cred_arg.sockets_per_node    = job_resrcs_ptr->sockets_per_node;
	cred_arg.sock_core_rep_count = job_resrcs_ptr->sock_core_rep_count;

	launch_msg_ptr->cred = slurm_cred_create(slurmctld_config.cred_ctx,
						 &cred_arg, protocol_version);

	if (launch_msg_ptr->cred)
		return SLURM_SUCCESS;
	error("slurm_cred_create failure for batch job %u", cred_arg.jobid);
	return SLURM_ERROR;
}

static void _depend_list_del(void *dep_ptr)
{
	xfree(dep_ptr);
}

/*
 * Copy a job's dependency list
 * IN depend_list_src - a job's depend_lst
 * RET copy of depend_list_src, must bee freed by caller
 */
extern List depended_list_copy(List depend_list_src)
{
	struct depend_spec *dep_src, *dep_dest;
	ListIterator iter;
	List depend_list_dest = NULL;

	if (!depend_list_src)
		return depend_list_dest;

	depend_list_dest = list_create(_depend_list_del);
	iter = list_iterator_create(depend_list_src);
	while ((dep_src = (struct depend_spec *) list_next(iter))) {
		dep_dest = xmalloc(sizeof(struct depend_spec));
		memcpy(dep_dest, dep_src, sizeof(struct depend_spec));
		list_append(depend_list_dest, dep_dest);
	}
	list_iterator_destroy(iter);
	return depend_list_dest;
}

/* Print a job's dependency information based upon job_ptr->depend_list */
extern void print_job_dependency(struct job_record *job_ptr)
{
	ListIterator depend_iter;
	struct depend_spec *dep_ptr;
	char *dep_flags, *dep_str;

	info("Dependency information for job %u", job_ptr->job_id);
	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->depend_list == NULL))
		return;

	depend_iter = list_iterator_create(job_ptr->details->depend_list);
	while ((dep_ptr = list_next(depend_iter))) {
		if      (dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) {
			info("  singleton");
			continue;
		}

		if (dep_ptr->depend_flags & SLURM_FLAGS_OR)
			dep_flags = "OR";
		else
			dep_flags = "";

		if      (dep_ptr->depend_type == SLURM_DEPEND_AFTER)
			dep_str = "after";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_ANY)
			dep_str = "afterany";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_NOT_OK)
			dep_str = "afternotok";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_OK)
			dep_str = "afterok";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_CORRESPOND)
			dep_str = "aftercorr";
		else if (dep_ptr->depend_type == SLURM_DEPEND_EXPAND)
			dep_str = "expand";
		else
			dep_str = "unknown";

		if (dep_ptr->array_task_id == INFINITE)
			info("  %s:%u_* %s",
			     dep_str, dep_ptr->job_id, dep_flags);
		else if (dep_ptr->array_task_id == NO_VAL)
			info("  %s:%u %s",
			     dep_str, dep_ptr->job_id, dep_flags);
		else
			info("  %s:%u_%u %s",
			     dep_str, dep_ptr->job_id,
			     dep_ptr->array_task_id, dep_flags);
	}
	list_iterator_destroy(depend_iter);
}

static void _depend_list2str(struct job_record *job_ptr, bool set_or_flag)
{
	ListIterator depend_iter;
	struct depend_spec *dep_ptr;
	char *dep_str, *sep = "";

	if (job_ptr->details == NULL)
		return;

	xfree(job_ptr->details->dependency);

	if (job_ptr->details->depend_list == NULL
		|| list_count(job_ptr->details->depend_list) == 0)
		return;

	depend_iter = list_iterator_create(job_ptr->details->depend_list);
	while ((dep_ptr = list_next(depend_iter))) {
		if      (dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) {
			xstrfmtcat(job_ptr->details->dependency,
				   "%ssingleton", sep);
			sep = ",";
			continue;
		}

		if      (dep_ptr->depend_type == SLURM_DEPEND_AFTER)
			dep_str = "after";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_ANY)
			dep_str = "afterany";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_NOT_OK)
			dep_str = "afternotok";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_OK)
			dep_str = "afterok";
		else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_CORRESPOND)
			dep_str = "aftercorr";
		else if (dep_ptr->depend_type == SLURM_DEPEND_EXPAND)
			dep_str = "expand";
		else
			dep_str = "unknown";

		if (dep_ptr->array_task_id == INFINITE)
			xstrfmtcat(job_ptr->details->dependency, "%s%s:%u_*",
				   sep, dep_str, dep_ptr->job_id);
		else if (dep_ptr->array_task_id == NO_VAL)
			xstrfmtcat(job_ptr->details->dependency, "%s%s:%u",
				   sep, dep_str, dep_ptr->job_id);
		else
			xstrfmtcat(job_ptr->details->dependency, "%s%s:%u_%u",
				   sep, dep_str, dep_ptr->job_id,
				   dep_ptr->array_task_id);

		if (set_or_flag)
			dep_ptr->depend_flags |= SLURM_FLAGS_OR;
		if (dep_ptr->depend_flags & SLURM_FLAGS_OR)
			sep = "?";
		else
			sep = ",";
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
	ListIterator depend_iter, job_iterator;
	struct depend_spec *dep_ptr;
	bool failure = false, depends = false, rebuild_str = false;
	bool or_satisfied = false;
 	List job_queue = NULL;
 	bool run_now;
	int results = 0;
	struct job_record *qjob_ptr, *djob_ptr, *dcjob_ptr;

	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->depend_list == NULL) ||
	    (list_count(job_ptr->details->depend_list) == 0))
		return 0;

	depend_iter = list_iterator_create(job_ptr->details->depend_list);
	while ((dep_ptr = list_next(depend_iter))) {
		bool clear_dep = false;
		dep_ptr->job_ptr = find_job_array_rec(dep_ptr->job_id,
						      dep_ptr->array_task_id);
		djob_ptr = dep_ptr->job_ptr;
 		if ((dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) &&
 		    job_ptr->name) {
 			/* get user jobs with the same user and name */
 			job_queue = _build_user_job_list(job_ptr->user_id,
							 job_ptr->name);
 			run_now = true;
			job_iterator = list_iterator_create(job_queue);
			while ((qjob_ptr = (struct job_record *)
					   list_next(job_iterator))) {
				/* already running/suspended job or previously
				 * submitted pending job */
				if (IS_JOB_RUNNING(qjob_ptr) ||
				    IS_JOB_SUSPENDED(qjob_ptr) ||
				    (IS_JOB_PENDING(qjob_ptr) &&
				     (qjob_ptr->job_id < job_ptr->job_id))) {
					run_now = false;
					break;
 				}
 			}
			list_iterator_destroy(job_iterator);
			FREE_NULL_LIST(job_queue);
			/* job can run now, delete dependency */
 			if (run_now)
 				list_delete_item(depend_iter);
 			else
				depends = true;
		} else if ((djob_ptr == NULL) ||
			   (djob_ptr->magic != JOB_MAGIC) ||
			   ((djob_ptr->job_id != dep_ptr->job_id) &&
			    (djob_ptr->array_job_id != dep_ptr->job_id))) {
			/* job is gone, dependency lifted */
			clear_dep = true;
		} else if (dep_ptr->array_task_id == INFINITE) {
			bool array_complete, array_completed, array_pending;
			array_complete=test_job_array_complete(dep_ptr->job_id);
			array_completed=test_job_array_completed(dep_ptr->job_id);
			array_pending  =test_job_array_pending(dep_ptr->job_id);
			/* Special case, apply test to job array as a whole */
			if (dep_ptr->depend_type == SLURM_DEPEND_AFTER) {
				if (!array_pending)
					clear_dep = true;
				else
					depends = true;
			} else if (dep_ptr->depend_type ==
				   SLURM_DEPEND_AFTER_ANY) {
				if (array_completed)
					clear_dep = true;
				else
					depends = true;
			} else if (dep_ptr->depend_type ==
				   SLURM_DEPEND_AFTER_NOT_OK) {
				if (dep_ptr->job_ptr->job_state &
				    JOB_SPECIAL_EXIT)
					clear_dep = true;
				else if (!array_completed)
					depends = true;
				else if (!array_complete)
					clear_dep = true;
				else {
					failure = true;
					break;
				}
			} else if (dep_ptr->depend_type ==
				   SLURM_DEPEND_AFTER_OK) {
				if (!array_completed)
					depends = true;
				else if (array_complete)
					clear_dep = true;
				else {
					failure = true;
					break;
				}
			} else if (dep_ptr->depend_type ==
				   SLURM_DEPEND_AFTER_CORRESPOND) {
				if ((job_ptr->array_task_id == NO_VAL) ||
				    (job_ptr->array_task_id == INFINITE)) {
					dcjob_ptr = NULL;
				} else {
					dcjob_ptr = find_job_array_rec(
							dep_ptr->job_id,
							job_ptr->array_task_id);
				}
				if (dcjob_ptr) {
					if (!IS_JOB_COMPLETED(dcjob_ptr))
						depends = true;
					else if (IS_JOB_COMPLETE(dcjob_ptr))
						clear_dep = true;
					else {
						failure = true;
						break;
					}
				} else {
					if (!array_completed)
						depends = true;
					else if (array_complete)
						clear_dep = true;
					else if (job_ptr->array_recs &&
						 (job_ptr->array_task_id ==
						  NO_VAL)) {
						depends = true;
					} else {
						failure = true;
						break;
					}
				}
			}

		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER) {
			if (!IS_JOB_PENDING(djob_ptr))
				clear_dep = true;
			else
				depends = true;
		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_ANY) {
			if (IS_JOB_COMPLETED(djob_ptr))
				clear_dep = true;
			else
				depends = true;
		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_NOT_OK) {
			if (djob_ptr->job_state & JOB_SPECIAL_EXIT)
				clear_dep = true;
			else if (!IS_JOB_COMPLETED(djob_ptr))
				depends = true;
			else if (!IS_JOB_COMPLETE(djob_ptr))
				clear_dep = true;
			else {
				failure = true;
				break;
			}
		} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_OK) {
			if (!IS_JOB_COMPLETED(djob_ptr))
				depends = true;
			else if (IS_JOB_COMPLETE(djob_ptr))
				clear_dep = true;
			else {
				failure = true;
				break;
			}
		} else if (dep_ptr->depend_type ==
			   SLURM_DEPEND_AFTER_CORRESPOND) {
			if (!IS_JOB_COMPLETED(djob_ptr))
				depends = true;
			else if (IS_JOB_COMPLETE(djob_ptr))
				clear_dep = true;
			else {
				failure = true;
				break;
			}
		} else if (dep_ptr->depend_type == SLURM_DEPEND_EXPAND) {
			time_t now = time(NULL);
			if (IS_JOB_PENDING(djob_ptr)) {
				depends = true;
			} else if (IS_JOB_COMPLETED(djob_ptr)) {
				failure = true;
				break;
			} else if ((djob_ptr->end_time != 0) &&
				   (djob_ptr->end_time > now)) {
				job_ptr->time_limit = djob_ptr->end_time - now;
				job_ptr->time_limit /= 60;  /* sec to min */
			}
			if (job_ptr->details && djob_ptr->details) {
				job_ptr->details->share_res =
					djob_ptr->details->share_res;
				job_ptr->details->whole_node =
					djob_ptr->details->whole_node;
			}
		} else
			failure = true;
		if (clear_dep && djob_ptr &&
		    (bb_g_job_test_stage_out(djob_ptr) != 1))
			clear_dep = false; /* Wait for burst buffer stage-out */
		if (clear_dep) {
			rebuild_str = true;
			if (dep_ptr->depend_flags & SLURM_FLAGS_OR) {
				or_satisfied = true;
				break;
			}
			list_delete_item(depend_iter);
		}
	}
	list_iterator_destroy(depend_iter);
	if (or_satisfied)
		list_flush(job_ptr->details->depend_list);
	if (rebuild_str)
		_depend_list2str(job_ptr, false);

	if (failure)
		results = 2;
	else if (depends)
		results = 1;

	return results;
}

/* Given a new job dependency specification, expand job array specifications
 * into a collection of task IDs that update_job_dependency can parse.
 * (e.g. "after:123_[4-5]" to "after:123_4:123_5")
 * Returns NULL if not valid job array specification.
 * Returned value must be xfreed. */
static char *_xlate_array_dep(char *new_depend)
{
	char *new_array_dep = NULL, *array_tmp, *jobid_ptr = NULL, *sep;
	bitstr_t *array_bitmap;
	int i;
	uint32_t job_id;
	int32_t t, t_first, t_last;

	if (strstr(new_depend, "_[") == NULL)
		return NULL;	/* No job array expressions */

	if (max_array_size == NO_VAL) {
		max_array_size = slurmctld_conf.max_array_sz;
	}

	for (i = 0; new_depend[i]; i++) {
		xstrfmtcat(new_array_dep, "%c", new_depend[i]);
		if ((new_depend[i] >= '0') && (new_depend[i] <= '9')) {
			if (jobid_ptr == NULL)
				jobid_ptr = new_depend + i;
		} else if ((new_depend[i] == '_') && (new_depend[i+1] == '[') &&
			   (jobid_ptr != NULL)) {
			job_id = (uint32_t) atol(jobid_ptr);
			i += 2;	/* Skip over "_[" */
			array_tmp = xstrdup(new_depend + i);
			sep = strchr(array_tmp, ']');
			if (sep)
				sep[0] = '\0';
			array_bitmap = bit_alloc(max_array_size);
			if ((sep == NULL) ||
			    (bit_unfmt(array_bitmap, array_tmp) != 0) ||
			    ((t_first = bit_ffs(array_bitmap)) == -1)) {
				/* Invalid format */
				xfree(array_tmp);
				bit_free(array_bitmap);
				xfree(new_array_dep);
				return NULL;
			}
			i += (sep - array_tmp);	/* Move to location of ']' */
			xfree(array_tmp);
			t_last = bit_fls(array_bitmap);
			for (t = t_first; t <= t_last; t++) {
				if (!bit_test(array_bitmap, t))
					continue;
				if (t == t_first) {
					xstrfmtcat(new_array_dep, "%d", t);
				} else {
					xstrfmtcat(new_array_dep, ":%u_%d",
						   job_id, t);
				}
			}
			bit_free(array_bitmap);
			jobid_ptr = NULL;
		} else {
			jobid_ptr = NULL;
		}
	}

	return new_array_dep;
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
	uint32_t array_task_id;
	char *tok, *new_array_dep, *sep_ptr, *sep_ptr2 = NULL;
	List new_depend_list = NULL;
	struct depend_spec *dep_ptr;
	struct job_record *dep_job_ptr;
	int expand_cnt = 0;
	bool or_flag = false;

	if (job_ptr->details == NULL)
		return EINVAL;

	/* Clear dependencies on NULL, "0", or empty dependency input */
	job_ptr->details->expanding_jobid = 0;
	if ((new_depend == NULL) || (new_depend[0] == '\0') ||
	    ((new_depend[0] == '0') && (new_depend[1] == '\0'))) {
		xfree(job_ptr->details->dependency);
		FREE_NULL_LIST(job_ptr->details->depend_list);
		return rc;

	}

	new_depend_list = list_create(_depend_list_del);
	if ((new_array_dep = _xlate_array_dep(new_depend)))
		tok = new_array_dep;
	else
		tok = new_depend;
	/* validate new dependency string */
	while (rc == SLURM_SUCCESS) {
 		/* test singleton dependency flag */
 		if (xstrncasecmp(tok, "singleton", 9) == 0) {
			depend_type = SLURM_DEPEND_SINGLETON;
			dep_ptr = xmalloc(sizeof(struct depend_spec));
			dep_ptr->depend_type = depend_type;
			/* dep_ptr->job_id = 0;		set by xmalloc */
			/* dep_ptr->job_ptr = NULL;	set by xmalloc */
			(void) list_append(new_depend_list, dep_ptr);
			if (tok[9] == ',') {
				tok += 10;
				continue;
			}
			if (tok[9] != '\0')
				rc = ESLURM_DEPENDENCY;
			break;
 		}

		/* Test for old format, just a job ID */
		sep_ptr = strchr(tok, ':');
		if ((sep_ptr == NULL) && (tok[0] >= '0') && (tok[0] <= '9')) {
			job_id = strtol(tok, &sep_ptr, 10);
			if ((sep_ptr != NULL) && (sep_ptr[0] == '_')) {
				if (sep_ptr[1] == '*') {
					array_task_id = INFINITE;
					sep_ptr += 2;	/* Past "_*" */
				} else {
					array_task_id = strtol(sep_ptr+1,
							       &sep_ptr, 10);
				}
			} else {
				array_task_id = NO_VAL;
			}
			if ((sep_ptr == NULL) ||
			    (job_id == 0) || (job_id == job_ptr->job_id) ||
			    ((sep_ptr[0] != '\0') && (sep_ptr[0] != ','))) {
				rc = ESLURM_DEPENDENCY;
				break;
			}
			if (array_task_id == NO_VAL) {
				dep_job_ptr = find_job_record(job_id);
				if (!dep_job_ptr) {
					dep_job_ptr = find_job_array_rec(job_id,
								      INFINITE);
				}
				if (dep_job_ptr &&
				    (dep_job_ptr->array_job_id == job_id) &&
				    ((dep_job_ptr->array_task_id != NO_VAL) ||
				     (dep_job_ptr->array_recs != NULL))) {
					array_task_id = INFINITE;
				}
			} else {
				dep_job_ptr = find_job_array_rec(job_id,
								 array_task_id);
			}
			if (dep_job_ptr) {
				dep_ptr = xmalloc(sizeof(struct depend_spec));
				dep_ptr->array_task_id = array_task_id;
				dep_ptr->depend_type = SLURM_DEPEND_AFTER_ANY;
				if (array_task_id == NO_VAL) {
					dep_ptr->job_id = dep_job_ptr->job_id;
				} else {
					dep_ptr->job_id =
						dep_job_ptr->array_job_id;
				}
				dep_ptr->job_ptr = dep_job_ptr;
				(void) list_append(new_depend_list, dep_ptr);
			}
			if (sep_ptr && (sep_ptr[0] == ',')) {
				tok = sep_ptr + 1;
				continue;
			} else {
				break;
			}
		} else if (sep_ptr == NULL) {
			rc = ESLURM_DEPENDENCY;
			break;
		}

		/* New format, <test>:job_ID */
		if      (xstrncasecmp(tok, "afternotok", 10) == 0)
			depend_type = SLURM_DEPEND_AFTER_NOT_OK;
		else if (xstrncasecmp(tok, "aftercorr", 9) == 0)
			depend_type = SLURM_DEPEND_AFTER_CORRESPOND;
		else if (xstrncasecmp(tok, "afterany", 8) == 0)
			depend_type = SLURM_DEPEND_AFTER_ANY;
		else if (xstrncasecmp(tok, "afterok", 7) == 0)
			depend_type = SLURM_DEPEND_AFTER_OK;
		else if (xstrncasecmp(tok, "after", 5) == 0)
			depend_type = SLURM_DEPEND_AFTER;
		else if (xstrncasecmp(tok, "expand", 6) == 0) {
			if (!select_g_job_expand_allow()) {
				rc = ESLURM_DEPENDENCY;
				break;
			}
			depend_type = SLURM_DEPEND_EXPAND;
		} else {
			rc = ESLURM_DEPENDENCY;
			break;
		}
		sep_ptr++;	/* skip over ":" */
		while (rc == SLURM_SUCCESS) {
			job_id = strtol(sep_ptr, &sep_ptr2, 10);
			if ((sep_ptr2 != NULL) && (sep_ptr2[0] == '_')) {
				if (sep_ptr2[1] == '*') {
					array_task_id = INFINITE;
					sep_ptr2 += 2;	/* Past "_*" */
				} else {
					array_task_id = strtol(sep_ptr2+1,
							       &sep_ptr2, 10);
				}
			} else
				array_task_id = NO_VAL;
			if ((sep_ptr2 == NULL) ||
			    (job_id == 0) || (job_id == job_ptr->job_id) ||
			    ((sep_ptr2[0] != '\0') && (sep_ptr2[0] != ',') &&
			     (sep_ptr2[0] != '?')  && (sep_ptr2[0] != ':'))) {
				rc = ESLURM_DEPENDENCY;
				break;
			}
			if (array_task_id == NO_VAL) {
				dep_job_ptr = find_job_record(job_id);
				if (!dep_job_ptr) {
					dep_job_ptr = find_job_array_rec(job_id,
								      INFINITE);
				}
				if (dep_job_ptr &&
				    (dep_job_ptr->array_job_id == job_id) &&
				    ((dep_job_ptr->array_task_id != NO_VAL) ||
				     (dep_job_ptr->array_recs != NULL))) {
					array_task_id = INFINITE;
				}
			} else {
				dep_job_ptr = find_job_array_rec(job_id,
								 array_task_id);
			}
			if ((depend_type == SLURM_DEPEND_EXPAND) &&
			    ((expand_cnt++ > 0) || (dep_job_ptr == NULL) ||
			     (!IS_JOB_RUNNING(dep_job_ptr))              ||
			     (dep_job_ptr->qos_id != job_ptr->qos_id)    ||
			     (dep_job_ptr->part_ptr == NULL)             ||
			     (job_ptr->part_ptr     == NULL)             ||
			     (dep_job_ptr->part_ptr != job_ptr->part_ptr))) {
				/* Expand only jobs in the same QOS and
				 * and partition */
				rc = ESLURM_DEPENDENCY;
				break;
			}
			if (depend_type == SLURM_DEPEND_EXPAND) {
				assoc_mgr_lock_t locks = {
					NO_LOCK, NO_LOCK, NO_LOCK, NO_LOCK,
					READ_LOCK, NO_LOCK, NO_LOCK };

				job_ptr->details->expanding_jobid = job_id;
				/* GRES configuration of this job must match
				 * the job being expanded */
				xfree(job_ptr->gres);
				job_ptr->gres = xstrdup(dep_job_ptr->gres);
				FREE_NULL_LIST(job_ptr->gres_list);
				gres_plugin_job_state_validate(
					&job_ptr->gres, &job_ptr->gres_list);
				assoc_mgr_lock(&locks);
				gres_set_job_tres_cnt(job_ptr->gres_list,
						      job_ptr->details ?
						      job_ptr->details->
						      min_nodes : 0,
						      job_ptr->tres_req_cnt,
						      true);
				xfree(job_ptr->tres_req_str);
				job_ptr->tres_req_str =
					assoc_mgr_make_tres_str_from_array(
						job_ptr->tres_req_cnt,
						TRES_STR_FLAG_SIMPLE, true);
				assoc_mgr_unlock(&locks);
			}
			if (dep_job_ptr) {	/* job still active */
				dep_ptr = xmalloc(sizeof(struct depend_spec));
				dep_ptr->array_task_id = array_task_id;
				dep_ptr->depend_type = depend_type;
				if (array_task_id == NO_VAL)
					dep_ptr->job_id  = dep_job_ptr->job_id;
				else {
					dep_ptr->job_id  =
						dep_job_ptr->array_job_id;
				}
				dep_ptr->job_ptr = dep_job_ptr;
				(void) list_append(new_depend_list, dep_ptr);
			}
			if (sep_ptr2[0] != ':')
				break;
			sep_ptr = sep_ptr2 + 1;	/* skip over ":" */
		}
		if (sep_ptr2 && (sep_ptr2[0] == ',')) {
			tok = sep_ptr2 + 1;
		} else if (sep_ptr2 && (sep_ptr2[0] == '?')) {
			tok = sep_ptr2 + 1;
			or_flag = true;
		} else {
			break;
		}
	}

	if (rc == SLURM_SUCCESS) {
		/* test for circular dependencies (e.g. A -> B -> A) */
		(void) _scan_depend(NULL, job_ptr->job_id);
		if (_scan_depend(new_depend_list, job_ptr->job_id))
			rc = ESLURM_CIRCULAR_DEPENDENCY;
	}

	if (rc == SLURM_SUCCESS) {
		FREE_NULL_LIST(job_ptr->details->depend_list);
		job_ptr->details->depend_list = new_depend_list;
		_depend_list2str(job_ptr, or_flag);
#if _DEBUG
		print_job_dependency(job_ptr);
#endif
	} else {
		FREE_NULL_LIST(new_depend_list);
	}
	xfree(new_array_dep);
	return rc;
}

/* Return true if job_id is found in dependency_list.
 * Pass NULL dependency list to clear the counter.
 * Execute recursively for each dependent job */
static bool _scan_depend(List dependency_list, uint32_t job_id)
{
	static time_t sched_update = 0;
	static int max_depend_depth = 10;
	static int job_counter = 0;
	bool rc = false;
	ListIterator iter;
	struct depend_spec *dep_ptr;

	if (sched_update != slurmctld_conf.last_update) {
		char *sched_params, *tmp_ptr;

		sched_params = slurm_get_sched_params();
		if (sched_params &&
		    (tmp_ptr = strstr(sched_params, "max_depend_depth="))) {
		/*                                   01234567890123456 */
			int i = atoi(tmp_ptr + 17);
			if (i < 0) {
				error("ignoring SchedulerParameters: "
				      "max_depend_depth value of %d", i);
			} else {
				      max_depend_depth = i;
			}
		}
		xfree(sched_params);
		sched_update = slurmctld_conf.last_update;
	}

	if (dependency_list == NULL) {
		job_counter = 0;
		return false;
	} else if (job_counter++ >= max_depend_depth) {
		return false;
	}

	xassert(job_id);
	iter = list_iterator_create(dependency_list);
	while (!rc && (dep_ptr = (struct depend_spec *) list_next(iter))) {
		if (dep_ptr->job_id == 0)	/* Singleton */
			continue;
		if (dep_ptr->job_id == job_id)
			rc = true;
		else if ((dep_ptr->job_id != dep_ptr->job_ptr->job_id) ||
			 (dep_ptr->job_ptr->magic != JOB_MAGIC))
			continue;	/* purged job, ptr not yet cleared */
		else if (!IS_JOB_FINISHED(dep_ptr->job_ptr) &&
			 dep_ptr->job_ptr->details &&
			 dep_ptr->job_ptr->details->depend_list) {
			rc = _scan_depend(dep_ptr->job_ptr->details->
					  depend_list, job_id);
			if (rc) {
				info("circular dependency: job %u is dependent "
				     "upon job %u", dep_ptr->job_id, job_id);
			}
		}
	}
	list_iterator_destroy(iter);
	return rc;
}

static void _pre_list_del(void *x)
{
	xfree(x);
}

/* If there are higher priority queued jobs in this job's partition, then
 * delay the job's expected initiation time as needed to run those jobs.
 * NOTE: This is only a rough estimate of the job's start time as it ignores
 * job dependencies, feature requirements, specific node requirements, etc. */
static void _delayed_job_start_time(struct job_record *job_ptr)
{
	uint32_t part_node_cnt, part_cpu_cnt, part_cpus_per_node;
	uint32_t job_size_cpus, job_size_nodes, job_time;
	uint64_t cume_space_time = 0;
	struct job_record *job_q_ptr;
	ListIterator job_iterator;

	if (job_ptr->part_ptr == NULL)
		return;
	part_node_cnt = job_ptr->part_ptr->total_nodes;
	part_cpu_cnt  = job_ptr->part_ptr->total_cpus;
	if (part_cpu_cnt > part_node_cnt)
		part_cpus_per_node = part_cpu_cnt / part_node_cnt;
	else
		part_cpus_per_node = 1;

	job_iterator = list_iterator_create(job_list);
	while ((job_q_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_PENDING(job_q_ptr) || !job_q_ptr->details ||
		    (job_q_ptr->part_ptr != job_ptr->part_ptr) ||
		    (job_q_ptr->priority < job_ptr->priority) ||
		    (job_q_ptr->job_id == job_ptr->job_id) ||
		    (IS_JOB_REVOKED(job_q_ptr)))
			continue;
		if (job_q_ptr->details->min_nodes == NO_VAL)
			job_size_nodes = 1;
		else
			job_size_nodes = job_q_ptr->details->min_nodes;
		if (job_q_ptr->details->min_cpus == NO_VAL)
			job_size_cpus = 1;
		else
			job_size_cpus = job_q_ptr->details->min_cpus;
		job_size_cpus = MAX(job_size_cpus,
				    (job_size_nodes * part_cpus_per_node));
		if (job_q_ptr->time_limit == NO_VAL)
			job_time = job_q_ptr->part_ptr->max_time;
		else
			job_time = job_q_ptr->time_limit;
		cume_space_time += job_size_cpus * job_time;
	}
	list_iterator_destroy(job_iterator);
	cume_space_time /= part_cpu_cnt;/* Factor out size */
	cume_space_time *= 60;		/* Minutes to seconds */
	debug2("Increasing estimated start of job %u by %"PRIu64" secs",
	       job_ptr->job_id, cume_space_time);
	job_ptr->start_time += cume_space_time;
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
	bitstr_t *active_bitmap = NULL, *avail_bitmap = NULL;
	bitstr_t *resv_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	int i, rc = SLURM_SUCCESS;
	time_t now = time(NULL), start_res, orig_start_time = (time_t) 0;
	List preemptee_candidates = NULL, preemptee_job_list = NULL;
	bool resv_overlap = false;

	job_ptr = find_job_record(job_desc_msg->job_id);
	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;

	part_ptr = job_ptr->part_ptr;
	if (part_ptr == NULL)
		return ESLURM_INVALID_PARTITION_NAME;

	if ((job_ptr->details == NULL) || (!IS_JOB_PENDING(job_ptr)))
		return ESLURM_DISABLED;

	if ((job_desc_msg->req_nodes == NULL) ||
	    (job_desc_msg->req_nodes[0] == '\0')) {
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
	if (job_req_node_filter(job_ptr, avail_bitmap, true))
		rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (job_ptr->details->exc_node_bitmap) {
		bit_and_not(avail_bitmap, job_ptr->details->exc_node_bitmap);
	}
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_bitmap)) {
			rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}
	}

	/* Enforce reservation: access control, time and nodes */
	if (job_ptr->details->begin_time &&
	    (job_ptr->details->begin_time > now))
		start_res = job_ptr->details->begin_time;
	else
		start_res = now;

	i = job_test_resv(job_ptr, &start_res, true, &resv_bitmap,
			  &exc_core_bitmap, &resv_overlap, false);
	if (i != SLURM_SUCCESS) {
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		return i;
	}
	bit_and(avail_bitmap, resv_bitmap);
	FREE_NULL_BITMAP(resv_bitmap);

	/* Only consider nodes that are not DOWN or DRAINED */
	bit_and(avail_bitmap, avail_node_bitmap);

	if (rc == SLURM_SUCCESS) {
		int test_fini = -1;
		uint8_t save_share_res, save_whole_node;
		/* On BlueGene systems don't adjust the min/max node limits
		   here.  We are working on midplane values. */
		min_nodes = MAX(job_ptr->details->min_nodes,
				part_ptr->min_nodes);
		if (job_ptr->details->max_nodes == 0)
			max_nodes = part_ptr->max_nodes;
		else
			max_nodes = MIN(job_ptr->details->max_nodes,
					part_ptr->max_nodes);
		max_nodes = MIN(max_nodes, 500000);	/* prevent overflows */
		if (!job_ptr->limit_set.tres[TRES_ARRAY_NODE] &&
		    job_ptr->details->max_nodes)
			req_nodes = max_nodes;
		else
			req_nodes = min_nodes;
		preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);

		/* The orig_start is based upon the backfill scheduler data
		 * and considers all higher priority jobs. The logic below
		 * only considers currently running jobs, so the expected
		 * start time will almost certainly be earlier and not as
		 * accurate, but this algorithm is much faster. */
		orig_start_time = job_ptr->start_time;
		build_active_feature_bitmap(job_ptr, avail_bitmap,
					    &active_bitmap);
		if (active_bitmap) {
			rc = select_g_job_test(job_ptr, active_bitmap,
					       min_nodes, max_nodes, req_nodes,
					       SELECT_MODE_WILL_RUN,
					       preemptee_candidates,
					       &preemptee_job_list,
					       exc_core_bitmap);
			if (rc == SLURM_SUCCESS) {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = active_bitmap;
				active_bitmap = NULL;
				test_fini = 1;
			} else {
				FREE_NULL_BITMAP(active_bitmap);
				save_share_res  = job_ptr->details->share_res;
				save_whole_node = job_ptr->details->whole_node;
				job_ptr->details->share_res = 0;
				job_ptr->details->whole_node = 1;
				test_fini = 0;
			}
		}
		if (test_fini != 1) {
			rc = select_g_job_test(job_ptr, avail_bitmap,
					       min_nodes, max_nodes, req_nodes,
					       SELECT_MODE_WILL_RUN,
					       preemptee_candidates,
					       &preemptee_job_list,
					       exc_core_bitmap);
			if (test_fini == 0) {
				job_ptr->details->share_res = save_share_res;
				job_ptr->details->whole_node = save_whole_node;
			}
		}
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
		resp_data->proc_cnt = job_ptr->total_cpus;
#endif
		_delayed_job_start_time(job_ptr);
		resp_data->start_time = MAX(job_ptr->start_time,
					    orig_start_time);
		resp_data->start_time = MAX(resp_data->start_time, start_res);
		job_ptr->start_time   = 0;  /* restore pending job start time */
		resp_data->node_list  = bitmap2node_name(avail_bitmap);

		if (preemptee_job_list) {
			ListIterator preemptee_iterator;
			uint32_t *preemptee_jid;
			struct job_record *tmp_job_ptr;
			resp_data->preemptee_job_id=list_create(_pre_list_del);
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

		resp_data->sys_usage_per = _get_system_usage();

		*resp = resp_data;
	} else {
		rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	FREE_NULL_LIST(preemptee_candidates);
	FREE_NULL_LIST(preemptee_job_list);
	FREE_NULL_BITMAP(avail_bitmap);
	FREE_NULL_BITMAP(exc_core_bitmap);
	return rc;
}

/*
 * epilog_slurmctld - execute the epilog_slurmctld for a job that has just
 *	terminated.
 * IN job_ptr - pointer to job that has been terminated
 */
extern void epilog_slurmctld(struct job_record *job_ptr)
{
	epilog_arg_t *epilog_arg;

	if ((slurmctld_conf.epilog_slurmctld == NULL) ||
	    (slurmctld_conf.epilog_slurmctld[0] == '\0'))
		return;

	if (access(slurmctld_conf.epilog_slurmctld, X_OK) < 0) {
		error("Invalid EpilogSlurmctld: %m");
		return;
	}

	epilog_arg = xmalloc(sizeof(epilog_arg_t));
	epilog_arg->job_id = job_ptr->job_id;
	epilog_arg->epilog_slurmctld = xstrdup(slurmctld_conf.epilog_slurmctld);
	epilog_arg->my_env = _build_env(job_ptr, true);

	job_ptr->epilog_running = true;
	slurm_thread_create_detached(NULL, _run_epilog, epilog_arg);
}

static char **_build_env(struct job_record *job_ptr, bool is_epilog)
{
	char **my_env, *name, *eq, buf[32];
	int exit_code, i, signal;

	my_env = xmalloc(sizeof(char *));
	my_env[0] = NULL;

	/* Set SPANK env vars first so that we can overrite as needed
	 * below. Prevent user hacking from setting SLURM_JOB_ID etc. */
	if (job_ptr->spank_job_env_size) {
		env_array_merge(&my_env,
				(const char **) job_ptr->spank_job_env);
	}

#ifdef HAVE_BG
	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID, &name);
	setenvf(&my_env, "MPIRUN_PARTITION", "%s", name);
	xfree(name);
#elif defined HAVE_ALPS_CRAY
	name = select_g_select_jobinfo_xstrdup(job_ptr->select_jobinfo,
						SELECT_PRINT_RESV_ID);
	setenvf(&my_env, "BASIL_RESERVATION_ID", "%s", name);
	xfree(name);
#endif
	setenvf(&my_env, "SLURM_JOB_ACCOUNT", "%s", job_ptr->account);
	if (job_ptr->details) {
		setenvf(&my_env, "SLURM_JOB_CONSTRAINTS",
			"%s", job_ptr->details->features);
	}

	if (is_epilog) {
		exit_code = signal = 0;
		if (WIFEXITED(job_ptr->exit_code)) {
			exit_code = WEXITSTATUS(job_ptr->exit_code);
		}
		if (WIFSIGNALED(job_ptr->exit_code)) {
			signal = WTERMSIG(job_ptr->exit_code);
		}
		snprintf(buf, sizeof(buf), "%d:%d", exit_code, signal);
		setenvf(&my_env, "SLURM_JOB_DERIVED_EC", "%u",
			job_ptr->derived_ec);
		setenvf(&my_env, "SLURM_JOB_EXIT_CODE2", "%s", buf);
		setenvf(&my_env, "SLURM_JOB_EXIT_CODE", "%u", job_ptr->exit_code);
	}

	if (job_ptr->array_task_id != NO_VAL) {
		setenvf(&my_env, "SLURM_ARRAY_JOB_ID", "%u",
			job_ptr->array_job_id);
		setenvf(&my_env, "SLURM_ARRAY_TASK_ID", "%u",
			job_ptr->array_task_id);
		if (job_ptr->details && job_ptr->details->env_sup &&
		    job_ptr->details->env_cnt) {
			for (i = 0; i < job_ptr->details->env_cnt; i++) {
				if (xstrncmp(job_ptr->details->env_sup[i],
					     "SLURM_ARRAY_TASK", 16))
					continue;
				eq = strchr(job_ptr->details->env_sup[i], '=');
				if (!eq)
					continue;
				eq[0] = '\0';
				setenvf(&my_env,
					job_ptr->details->env_sup[i],
				        "%s", eq + 1);
				eq[0] = '=';
			}
		}
	}

	if (slurmctld_conf.cluster_name) {
		setenvf(&my_env, "SLURM_CLUSTER_NAME", "%s",
			slurmctld_conf.cluster_name);
	}

	if (job_ptr->pack_job_id) {
		setenvf(&my_env, "SLURM_PACK_JOB_ID", "%u",
			job_ptr->pack_job_id);
		setenvf(&my_env, "SLURM_PACK_JOB_OFFSET", "%u",
			job_ptr->pack_job_offset);
		if ((job_ptr->pack_job_offset == 0) && job_ptr->pack_job_list) {
			struct job_record *pack_job = NULL;
			ListIterator iter;
			hostset_t hs = NULL;
			int hs_len = 0;
			iter = list_iterator_create(job_ptr->pack_job_list);
			while ((pack_job = (struct job_record *)
					   list_next(iter))) {
				if (job_ptr->pack_job_id !=
				    pack_job->pack_job_id) {
					error("%s: Bad pack_job_list for job %u",
					      __func__, job_ptr->pack_job_id);
					continue;
				}
				if (hs) {
					(void) hostset_insert(hs,
							      pack_job->nodes);
				} else {
					hs = hostset_create(pack_job->nodes);
				}
				hs_len += strlen(pack_job->nodes) + 2;
			}
			list_iterator_destroy(iter);
			if (hs) {
				char *buf = xmalloc(hs_len);
				(void) hostset_ranged_string(hs, hs_len, buf);
				setenvf(&my_env, "SLURM_PACK_JOB_NODELIST",
					"%s", buf);
				xfree(buf);
				hostset_destroy(hs);
			}
		}
	}
	setenvf(&my_env, "SLURM_JOB_GID", "%u", job_ptr->group_id);
	name = gid_to_string((gid_t) job_ptr->group_id);
	setenvf(&my_env, "SLURM_JOB_GROUP", "%s", name);
	xfree(name);
	setenvf(&my_env, "SLURM_JOBID", "%u", job_ptr->job_id);
	setenvf(&my_env, "SLURM_JOB_ID", "%u", job_ptr->job_id);
	if (job_ptr->licenses)
		setenvf(&my_env, "SLURM_JOB_LICENSES", "%s", job_ptr->licenses);
	setenvf(&my_env, "SLURM_JOB_NAME", "%s", job_ptr->name);
	setenvf(&my_env, "SLURM_JOB_NODELIST", "%s", job_ptr->nodes);
	if (job_ptr->part_ptr) {
		setenvf(&my_env, "SLURM_JOB_PARTITION", "%s",
			job_ptr->part_ptr->name);
	} else {
		setenvf(&my_env, "SLURM_JOB_PARTITION", "%s",
			job_ptr->partition);
	}
	setenvf(&my_env, "SLURM_JOB_UID", "%u", job_ptr->user_id);
	name = uid_to_string((uid_t) job_ptr->user_id);
	setenvf(&my_env, "SLURM_JOB_USER", "%s", name);
	xfree(name);
	if (job_ptr->wckey) {
		setenvf(&my_env, "SLURM_WCKEY", "%s", job_ptr->wckey);
	}

	return my_env;
}

static void *_run_epilog(void *arg)
{
	/* Locks: Write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK, NO_LOCK };
	struct job_record *job_ptr;
	epilog_arg_t *epilog_arg = (epilog_arg_t *) arg;
	pid_t cpid;
	int i, status, wait_rc;
	char *argv[2];
	uint16_t tm;

	argv[0] = epilog_arg->epilog_slurmctld;
	argv[1] = NULL;

	if ((cpid = fork()) < 0) {
		error("epilog_slurmctld fork error: %m");
		goto fini;
	}
	if (cpid == 0) {
		for (i = 0; i < 1024; i++)
			(void) close(i);
		setpgid(0, 0);
		execve(argv[0], argv, epilog_arg->my_env);
		exit(127);
	}

	/* Prolog and epilog use the same timeout
	 */
	tm = slurm_get_prolog_timeout();
	while (1) {
		wait_rc = waitpid_timeout(__func__, cpid, &status, tm);
		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("%s: waitpid error: %m", __func__);
			break;
		} else if (wait_rc > 0) {
			break;
		}
	}
	if (status != 0) {
		error("epilog_slurmctld job %u epilog exit status %u:%u",
		      epilog_arg->job_id, WEXITSTATUS(status),
		      WTERMSIG(status));
	} else {
		debug2("epilog_slurmctld job %u epilog completed",
		       epilog_arg->job_id);
	}

 fini:	lock_slurmctld(job_write_lock);
	job_ptr = find_job_record(epilog_arg->job_id);
	if (job_ptr) {
		job_ptr->epilog_running = false;
		/* Clean up the JOB_COMPLETING flag
		 * only if the node count is 0 meaning
		 * the slurmd epilog already completed.
		 */
		if (job_ptr->node_cnt == 0
		    && IS_JOB_COMPLETING(job_ptr))
			cleanup_completing(job_ptr);
	}
	unlock_slurmctld(job_write_lock);
	xfree(epilog_arg->epilog_slurmctld);
	for (i=0; epilog_arg->my_env[i]; i++)
		xfree(epilog_arg->my_env[i]);
	xfree(epilog_arg->my_env);
	xfree(epilog_arg);
	return NULL;
}

/* Determine which nodes must be rebooted for a job
 * IN job_ptr - pointer to job that will be initiated
 * RET bitmap of nodes requiring a reboot
 */
extern bitstr_t *node_features_reboot(struct job_record *job_ptr)
{
	bitstr_t *active_bitmap = NULL, *boot_node_bitmap = NULL;

	if (job_ptr->reboot) {
		boot_node_bitmap = bit_copy(job_ptr->node_bitmap);
		return boot_node_bitmap;
	}

	if ((node_features_g_count() == 0) ||
	    !node_features_g_user_update(job_ptr->user_id))
		return NULL;

	build_active_feature_bitmap(job_ptr, job_ptr->node_bitmap,
				    &active_bitmap);
	if (active_bitmap == NULL)	/* All have desired features */
		return NULL;

	boot_node_bitmap = bit_copy(job_ptr->node_bitmap);
	bit_and_not(boot_node_bitmap, active_bitmap);
	FREE_NULL_BITMAP(active_bitmap);
	if (bit_set_count(boot_node_bitmap) == 0)
		FREE_NULL_BITMAP(boot_node_bitmap);
	else
		job_ptr->wait_all_nodes = 1;

	return boot_node_bitmap;
}

/* Determine if node boot required for this job
 * IN job_ptr - pointer to job that will be initiated
 * IN node_bitmap - nodes to be allocated
 * RET - true if reboot required
 */
extern bool node_features_reboot_test(struct job_record *job_ptr,
				      bitstr_t *node_bitmap)
{
	bitstr_t *active_bitmap = NULL, *boot_node_bitmap = NULL;
	int node_cnt;

	if (job_ptr->reboot)
		return true;

	if ((node_features_g_count() == 0) ||
	    !node_features_g_user_update(job_ptr->user_id))
		return false;

	build_active_feature_bitmap(job_ptr, node_bitmap, &active_bitmap);
	if (active_bitmap == NULL)	/* All have desired features */
		return false;

	boot_node_bitmap = bit_copy(node_bitmap);
	bit_and_not(boot_node_bitmap, active_bitmap);
	node_cnt = bit_set_count(boot_node_bitmap);
	FREE_NULL_BITMAP(active_bitmap);
	FREE_NULL_BITMAP(boot_node_bitmap);

	if (node_cnt == 0)
		return false;
	return true;
}

/*
 * reboot_job_nodes - Reboot the compute nodes allocated to a job.
 * IN job_ptr - pointer to job that will be initiated
 * RET SLURM_SUCCESS(0) or error code
 */
#ifdef HAVE_FRONT_END
extern int reboot_job_nodes(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}
#else
extern int reboot_job_nodes(struct job_record *job_ptr)
{
	int i, i_first, i_last;
	agent_arg_t *reboot_agent_args = NULL;
	reboot_msg_t *reboot_msg;
	struct node_record *node_ptr;
	time_t now = time(NULL);
	bitstr_t *boot_node_bitmap = NULL;

	if ((job_ptr->details == NULL) || (job_ptr->node_bitmap == NULL))
		return SLURM_SUCCESS;
	if (power_save_test())
		return power_job_reboot(job_ptr);
	if ((slurmctld_conf.reboot_program == NULL) ||
	    (slurmctld_conf.reboot_program[0] == '\0'))
		return SLURM_SUCCESS;

	boot_node_bitmap = node_features_reboot(job_ptr);
	if (boot_node_bitmap == NULL)
		return SLURM_SUCCESS;

	reboot_agent_args = xmalloc(sizeof(agent_arg_t));
	reboot_agent_args->msg_type = REQUEST_REBOOT_NODES;
	reboot_agent_args->retry = 0;
	reboot_agent_args->protocol_version = SLURM_PROTOCOL_VERSION;
	reboot_agent_args->hostlist = hostlist_create(NULL);
	reboot_msg = xmalloc(sizeof(reboot_msg_t));
	if (job_ptr->details->features &&
	    node_features_g_user_update(job_ptr->user_id)) {
		reboot_msg->features = node_features_g_job_xlate(
					job_ptr->details->features);
	}
	reboot_agent_args->msg_args = reboot_msg;
	i_first = bit_ffs(boot_node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(boot_node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(boot_node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		if (reboot_agent_args->protocol_version
		    > node_ptr->protocol_version) {
			reboot_agent_args->protocol_version =
				node_ptr->protocol_version;
		}
		hostlist_push_host(reboot_agent_args->hostlist, node_ptr->name);
		reboot_agent_args->node_count++;
		node_ptr->node_state |= NODE_STATE_NO_RESPOND;
		node_ptr->node_state |= NODE_STATE_POWER_UP;
		bit_clear(avail_node_bitmap, i);
		bit_set(booting_node_bitmap, i);
		node_ptr->boot_req_time = now;
		node_ptr->last_response = now + slurmctld_conf.resume_timeout;
	}
	FREE_NULL_BITMAP(boot_node_bitmap);
	agent_queue_request(reboot_agent_args);

	job_ptr->details->prolog_running++;

	slurm_thread_create_detached(NULL, _wait_boot, job_ptr);

	return SLURM_SUCCESS;
}

static void *_wait_boot(void *arg)
{
	struct job_record *job_ptr = (struct job_record *) arg;
	/* Locks: Write jobs; read nodes */
	slurmctld_lock_t job_write_lock = {
		READ_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };
	/* Locks: Write jobs; write nodes */
	slurmctld_lock_t node_write_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	bitstr_t *boot_node_bitmap;
	uint16_t resume_timeout = slurm_get_resume_timeout();
	struct node_record *node_ptr;
	time_t start_time = time(NULL);
	int i, total_node_cnt, wait_node_cnt;
	uint32_t save_job_id = job_ptr->job_id;

	do {
		sleep(5);
		total_node_cnt = wait_node_cnt = 0;
		lock_slurmctld(job_write_lock);
		if ((job_ptr->magic != JOB_MAGIC) ||
		    (job_ptr->job_id != save_job_id)) {
			error("Job %u vanished while waiting for node boot",
			      save_job_id);
			unlock_slurmctld(job_write_lock);
			return NULL;
		}
		if (IS_JOB_PENDING(job_ptr) ||	/* Job requeued or killed */
		    IS_JOB_FINISHED(job_ptr) ||
		    !job_ptr->node_bitmap) {
			verbose("Job %u no longer waiting for node boot",
				save_job_id);
			unlock_slurmctld(job_write_lock);
			return NULL;
		}
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			total_node_cnt++;
			if (node_ptr->boot_time < start_time)
				wait_node_cnt++;
		}
		if (wait_node_cnt) {
			debug("Job %u still waiting for %d of %d nodes to boot",
			      job_ptr->job_id, wait_node_cnt, total_node_cnt);
		} else {
			info("Job %u boot complete for all %d nodes",
			     job_ptr->job_id, total_node_cnt);
		}
		i = (int) difftime(time(NULL), start_time);
		if (i >= resume_timeout) {
			error("Job %u timeout waiting for node %d of %d boots",
			      job_ptr->job_id, wait_node_cnt, total_node_cnt);
			wait_node_cnt = 0;
		}
		unlock_slurmctld(job_write_lock);
	} while (wait_node_cnt);

	/* Validate the nodes have desired features */
	lock_slurmctld(node_write_lock);
	boot_node_bitmap = node_features_reboot(job_ptr);
	if (boot_node_bitmap && bit_set_count(boot_node_bitmap)) {
		char *node_list = bitmap2node_name(boot_node_bitmap);
		error("Failed to reboot nodes %s into expected state for job %u",
		      node_list, job_ptr->job_id);
		(void) drain_nodes(node_list, "Node mode change failure",
				   getuid());
		xfree(node_list);
		(void) job_requeue(getuid(), job_ptr->job_id, NULL, false, 0);
	}
	FREE_NULL_BITMAP(boot_node_bitmap);

	prolog_running_decr(job_ptr);
	job_validate_mem(job_ptr);
	unlock_slurmctld(node_write_lock);

	return NULL;
}
#endif

/*
 * prolog_slurmctld - execute the prolog_slurmctld for a job that has just
 *	been allocated resources.
 * IN job_ptr - pointer to job that will be initiated
 */
extern void prolog_slurmctld(struct job_record *job_ptr)
{
	if ((slurmctld_conf.prolog_slurmctld == NULL) ||
	    (slurmctld_conf.prolog_slurmctld[0] == '\0'))
		return;

	if (access(slurmctld_conf.prolog_slurmctld, X_OK) < 0) {
		error("Invalid PrologSlurmctld: %m");
		return;
	}

	if (job_ptr->details) {
		job_ptr->details->prolog_running++;
		job_ptr->job_state |= JOB_CONFIGURING;
	}

	slurm_thread_create_detached(NULL, _run_prolog, job_ptr);
}

static void *_run_prolog(void *arg)
{
	struct job_record *job_ptr = (struct job_record *) arg;
	struct node_record *node_ptr;
	uint32_t job_id;
	pid_t cpid;
	int i, rc, status, wait_rc;
	char *argv[2], **my_env;
	/* Locks: Read config; Write jobs, nodes */
	slurmctld_lock_t config_read_lock = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	bitstr_t *node_bitmap = NULL;
	time_t now = time(NULL);
	uint16_t resume_timeout = slurm_get_resume_timeout();
	uint16_t tm;

	lock_slurmctld(config_read_lock);
	argv[0] = xstrdup(slurmctld_conf.prolog_slurmctld);
	argv[1] = NULL;
	my_env = _build_env(job_ptr, false);
	job_id = job_ptr->job_id;
	if (job_ptr->node_bitmap) {
		node_bitmap = bit_copy(job_ptr->node_bitmap);
		for (i = 0, node_ptr = node_record_table_ptr;
		     i < node_record_count; i++, node_ptr++) {
			if (!bit_test(node_bitmap, i))
				continue;
			/* Allow time for possible reboot */
			node_ptr->last_response = now + resume_timeout;
		}
	}
	unlock_slurmctld(config_read_lock);

	if ((cpid = fork()) < 0) {
		error("prolog_slurmctld fork error: %m");
		goto fini;
	}
	if (cpid == 0) {
		for (i = 0; i < 1024; i++)
			(void) close(i);
		setpgid(0, 0);
		execve(argv[0], argv, my_env);
		exit(127);
	}

	tm = slurm_get_prolog_timeout();
	while (1) {
		wait_rc = waitpid_timeout(__func__, cpid, &status, tm);
		if (wait_rc < 0) {
			if (errno == EINTR)
				continue;
			error("%s: waitpid error: %m", __func__);
			break;
		} else if (wait_rc > 0) {
			break;
		}
	}

	if (status != 0) {
		bool kill_job = false;
		slurmctld_lock_t job_write_lock = {
			NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
		error("prolog_slurmctld job %u prolog exit status %u:%u",
		      job_id, WEXITSTATUS(status), WTERMSIG(status));
		lock_slurmctld(job_write_lock);
		if ((rc = job_requeue(0, job_id, NULL, false, 0))) {
			info("unable to requeue job %u: %s", job_id,
			     slurm_strerror(rc));
			kill_job = true;
		}
		if (kill_job) {
			srun_user_message(job_ptr,
					  "PrologSlurmctld failed, job killed");
			if (job_ptr->pack_job_list) {
				(void) pack_job_signal(job_ptr, SIGKILL, 0, 0,
						       false);
			} else {
				(void) job_signal(job_id, SIGKILL, 0, 0, false);
			}
		}

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
	prolog_running_decr(job_ptr);
	if (power_save_test()) {
		/* Wait for node to register after booting */
	} else if (job_ptr && job_ptr->node_bitmap) {
		for (i=0; i<node_record_count; i++) {
			if (bit_test(job_ptr->node_bitmap, i) == 0)
				continue;
			bit_clear(booting_node_bitmap, i);
			node_record_table_ptr[i].node_state &=
				(~NODE_STATE_POWER_UP);
		}
	} else if (node_bitmap) {
		for (i=0; i<node_record_count; i++) {
			if (bit_test(node_bitmap, i) == 0)
				continue;
			bit_clear(booting_node_bitmap, i);
			node_record_table_ptr[i].node_state &=
				(~NODE_STATE_POWER_UP);
		}
	}
	unlock_slurmctld(config_read_lock);
	FREE_NULL_BITMAP(node_bitmap);

	return NULL;
}

/* Decrement a job's prolog_running counter and launch the job if zero */
extern void prolog_running_decr(struct job_record *job_ptr)
{
	if (!job_ptr)
		return;

	if (job_ptr->details && job_ptr->details->prolog_running &&
	    (--job_ptr->details->prolog_running > 0))
		return;

	/* Federated job notified the origin that the job is to be requeued,
	 * need to wait for this job to be cancelled. */
	if (job_ptr->job_state & JOB_REQUEUE_FED)
		return;

	if (IS_JOB_CONFIGURING(job_ptr) && test_job_nodes_ready(job_ptr)) {
		char job_id_buf[JBUFSIZ];
		info("%s: Configuration for %s is complete", __func__,
		     jobid2fmt(job_ptr, job_id_buf, sizeof(job_id_buf)));
		job_config_fini(job_ptr);
		if (job_ptr->batch_flag &&
		    (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr))) {
			launch_job(job_ptr);
		}
	}
}

/*
 * Copy a job's feature list
 * IN feature_list_src - a job's depend_lst
 * RET copy of feature_list_src, must be freed by caller
 */
extern List feature_list_copy(List feature_list_src)
{
	job_feature_t *feat_src, *feat_dest;
	ListIterator iter;
	List feature_list_dest = NULL;

	if (!feature_list_src)
		return feature_list_dest;

	feature_list_dest = list_create(_feature_list_delete);
	iter = list_iterator_create(feature_list_src);
	while ((feat_src = (job_feature_t *) list_next(iter))) {
		feat_dest = xmalloc(sizeof(job_feature_t));
		memcpy(feat_dest, feat_src, sizeof(job_feature_t));
		feat_dest->name = xstrdup(feat_src->name);
		list_append(feature_list_dest, feat_dest);
	}
	list_iterator_destroy(iter);
	return feature_list_dest;
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
	char *tmp_requested, *str_ptr, *feature = NULL;
	int bracket = 0, count = 0, i;
	bool have_count = false, have_or = false;
	job_feature_t *feat;

	if (!detail_ptr || !detail_ptr->features)	/* no constraints */
		return SLURM_SUCCESS;
	if (detail_ptr->feature_list)		/* already processed */
		return SLURM_SUCCESS;

	/* Use of commas separator is a common error. Replace them with '&' */
	while ((str_ptr = strstr(detail_ptr->features, ",")))
		str_ptr[0] = '&';

	tmp_requested = xstrdup(detail_ptr->features);
	detail_ptr->feature_list = list_create(_feature_list_delete);
	for (i=0; ; i++) {
		if (tmp_requested[i] == '*') {
			tmp_requested[i] = '\0';
			have_count = true;
			count = strtol(&tmp_requested[i+1], &str_ptr, 10);
			if ((feature == NULL) || (count <= 0)) {
				info("Job %u invalid constraint %s",
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_INVALID_FEATURE;
			}
			i = str_ptr - tmp_requested - 1;
		} else if (tmp_requested[i] == '&') {
			tmp_requested[i] = '\0';
			if (feature == NULL) {
				info("Job %u invalid constraint %s",
					job_ptr->job_id, detail_ptr->features);
				xfree(tmp_requested);
				return ESLURM_INVALID_FEATURE;
			}
			feat = xmalloc(sizeof(job_feature_t));
			feat->name = xstrdup(feature);
			feat->count = count;
			if (bracket)
				feat->op_code = FEATURE_OP_XAND;
			else
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
			feat = xmalloc(sizeof(job_feature_t));
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
				feat = xmalloc(sizeof(job_feature_t));
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

	return _valid_feature_list(job_ptr, detail_ptr->feature_list);
}

static void _feature_list_delete(void *x)
{
	job_feature_t *feature = (job_feature_t *)x;
	xfree(feature->name);
	xfree(feature);
}

static int _valid_feature_list(struct job_record *job_ptr, List feature_list)
{
	ListIterator feat_iter;
	job_feature_t *feat_ptr;
	char *buf = NULL, tmp[16];
	int bracket = 0;
	int rc = SLURM_SUCCESS;
	bool can_reboot;

	if (feature_list == NULL) {
		debug2("Job %u feature list is empty", job_ptr->job_id);
		return rc;
	}

	can_reboot = node_features_g_user_update(job_ptr->user_id);

	feat_iter = list_iterator_create(feature_list);
	while ((feat_ptr = (job_feature_t *)list_next(feat_iter))) {
		if ((feat_ptr->op_code == FEATURE_OP_XOR) ||
		    (feat_ptr->op_code == FEATURE_OP_XAND)) {
			if (bracket == 0)
				xstrcat(buf, "[");
			bracket = 1;
		}
		xstrcat(buf, feat_ptr->name);
		if (rc == SLURM_SUCCESS)
			rc = _valid_node_feature(feat_ptr->name, can_reboot);
		if (feat_ptr->count) {
			snprintf(tmp, sizeof(tmp), "*%u", feat_ptr->count);
			xstrcat(buf, tmp);
		}
		if (bracket &&
		    ((feat_ptr->op_code != FEATURE_OP_XOR) &&
		     (feat_ptr->op_code != FEATURE_OP_XAND))) {
			xstrcat(buf, "]");
			bracket = 0;
		}
		if ((feat_ptr->op_code == FEATURE_OP_AND) ||
		    (feat_ptr->op_code == FEATURE_OP_XAND))
			xstrcat(buf, "&");
		else if ((feat_ptr->op_code == FEATURE_OP_OR) ||
			 (feat_ptr->op_code == FEATURE_OP_XOR))
			xstrcat(buf, "|");
	}
	list_iterator_destroy(feat_iter);

	if (rc == SLURM_SUCCESS) {
		debug("Job %u feature list: %s", job_ptr->job_id, buf);
	} else {
		info("Job %u has invalid feature list: %s",
		     job_ptr->job_id, buf);
	}
	xfree(buf);

	return rc;
}

/* Validate that job's feature is available on some node(s) */
static int _valid_node_feature(char *feature, bool can_reboot)
{
	int rc = ESLURM_INVALID_FEATURE;
	node_feature_t *feature_ptr;
	ListIterator feature_iter;

	if (can_reboot)
		feature_iter = list_iterator_create(avail_feature_list);
	else
		feature_iter = list_iterator_create(active_feature_list);
	while ((feature_ptr = (node_feature_t *)list_next(feature_iter))) {
		if (xstrcmp(feature_ptr->name, feature))
			continue;
		rc = SLURM_SUCCESS;
		break;
	}
	list_iterator_destroy(feature_iter);

	return rc;
}

/* If a job can run in multiple partitions, when it is started we want to
 * put the name of the partition used _first_ in that list. When slurmctld
 * restarts, that will be used to set the job's part_ptr and that will be
 * reported to squeue. We leave all of the partitions in the list though,
 * so the job can be requeued and have access to them all. */
extern void rebuild_job_part_list(struct job_record *job_ptr)
{
	ListIterator part_iterator;
	struct part_record *part_ptr;

	if (!job_ptr->part_ptr_list)
		return;
	if (!job_ptr->part_ptr || !job_ptr->part_ptr->name) {
		error("Job %u has NULL part_ptr or the partition name is NULL",
		      job_ptr->job_id);
		return;
	}

	xfree(job_ptr->partition);
	job_ptr->partition = xstrdup(job_ptr->part_ptr->name);

	part_iterator = list_iterator_create(job_ptr->part_ptr_list);
	while ((part_ptr = (struct part_record *) list_next(part_iterator))) {
		if (part_ptr == job_ptr->part_ptr)
			continue;
		xstrcat(job_ptr->partition, ",");
		xstrcat(job_ptr->partition, part_ptr->name);
	}
	list_iterator_destroy(part_iterator);
}

/* cleanup_completing()
 *
 * Clean up the JOB_COMPLETING flag and eventually
 * requeue the job if there is a pending request
 * for it. This function assumes the caller has the
 * appropriate locks on the job_record.
 */
void
cleanup_completing(struct job_record *job_ptr)
{
	time_t delay;

	trace_job(job_ptr, __func__, "");

	delay = last_job_update - job_ptr->end_time;
	if (delay > 60) {
		info("%s: job %u completion process took %ld seconds",
		     __func__, job_ptr->job_id,(long) delay);
	}

	license_job_return(job_ptr);
	gs_job_fini(job_ptr);

	delete_step_records(job_ptr);
	job_ptr->job_state &= (~JOB_COMPLETING);
	job_hold_requeue(job_ptr);

	/* Job could be pending if the job was requeued due to a node failure */
	if (IS_JOB_COMPLETED(job_ptr))
		fed_mgr_job_complete(job_ptr, job_ptr->exit_code,
				     job_ptr->start_time);
}

/*
 * _waitpid_timeout()
 *
 *  Same as waitpid(2) but kill process group for pid after timeout secs.
 */
int
waitpid_timeout(const char *name, pid_t pid, int *pstatus, int timeout)
{
	int timeout_ms = 1000 * timeout; /* timeout in ms                   */
	int max_delay =  1000;           /* max delay between waitpid calls */
	int delay = 10;                  /* initial delay                   */
	int rc;
	int options = WNOHANG;

	if (timeout <= 0 || timeout == NO_VAL16)
		options = 0;

	while ((rc = waitpid (pid, pstatus, options)) <= 0) {
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			error("waidpid: %m");
			return -1;
		}
		else if (timeout_ms <= 0) {
			info("%s%stimeout after %ds: killing pgid %d",
			     name != NULL ? name : "",
			     name != NULL ? ": " : "",
			     timeout, pid);
			killpg(pid, SIGKILL);
			options = 0;
		}
		else {
			(void) poll(NULL, 0, delay);
			timeout_ms -= delay;
			delay = MIN (timeout_ms, MIN(max_delay, delay*2));
		}
	}

	killpg(pid, SIGKILL);  /* kill children too */
	return pid;
}
