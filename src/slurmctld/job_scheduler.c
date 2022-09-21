/*****************************************************************************\
 * job_scheduler.c - manage the scheduling of pending jobs in priority order
 *	Note there is a global job list (job_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/prep.h"
#include "src/common/power.h"
#include "src/common/select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather.h"
#include "src/common/strlcpy.h"
#include "src/common/parse_time.h"
#include "src/common/timers.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/gres_ctld.h"
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

#ifndef CORRESPOND_ARRAY_TASK_CNT
#  define CORRESPOND_ARRAY_TASK_CNT 10
#endif
#define BUILD_TIMEOUT 2000000	/* Max build_job_queue() run time in usec */
#define MAX_FAILED_RESV 10

static batch_job_launch_msg_t *_build_launch_job_msg(job_record_t *job_ptr,
						     uint16_t protocol_version);
static void	_job_queue_append(List job_queue, job_record_t *job_ptr,
				  part_record_t *part_ptr, uint32_t priority);
static bool	_job_runnable_test1(job_record_t *job_ptr, bool clear_start);
static bool	_job_runnable_test2(job_record_t *job_ptr, time_t now,
				    bool check_min_time);
static bool	_scan_depend(List dependency_list, job_record_t *job_ptr);
static void *	_sched_agent(void *args);
static int	_schedule(bool full_queue);
static int	_valid_batch_features(job_record_t *job_ptr, bool can_reboot);
static int	_valid_feature_list(job_record_t *job_ptr, List feature_list,
				    bool can_reboot);
static int	_valid_node_feature(char *feature, bool can_reboot);
static int	build_queue_timeout = BUILD_TIMEOUT;
static int	correspond_after_task_cnt = CORRESPOND_ARRAY_TASK_CNT;

static pthread_mutex_t sched_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sched_cond = PTHREAD_COND_INITIALIZER;
static pthread_t thread_id_sched = 0;
static bool sched_full_queue = false;
static int sched_requests = 0;
static struct timeval sched_last = {0, 0};

static uint32_t max_array_size = NO_VAL;
static bool bf_hetjob_immediate = false;
static uint16_t bf_hetjob_prio = 0;
static int sched_min_interval = 2;

static int bb_array_stage_cnt = 10;
extern diag_stats_t slurmctld_diag_stats;

static int _find_singleton_job (void *x, void *key)
{
	job_record_t *qjob_ptr = (job_record_t *) x;
	job_record_t *job_ptr = (job_record_t *) key;

	xassert (qjob_ptr->magic == JOB_MAGIC);

	/*
	 * get user jobs with the same user and name
	 */
	if (qjob_ptr->user_id != job_ptr->user_id)
		return 0;
	if (qjob_ptr->name && job_ptr->name &&
	    xstrcmp(qjob_ptr->name, job_ptr->name))
		return 0;
	/*
	 * already running/suspended job or previously
	 * submitted pending job
	 */
	if (IS_JOB_RUNNING(qjob_ptr) || IS_JOB_SUSPENDED(qjob_ptr) ||
	    (IS_JOB_PENDING(qjob_ptr) &&
	     (qjob_ptr->job_id < job_ptr->job_id))) {
		return 1;
	}

	return 0;
}

/*
 * Calculate how busy the system is by figuring out how busy each node is.
 */
static double _get_system_usage(void)
{
	static double sys_usage_per = 0.0;
	static time_t last_idle_update = 0;

	if (last_idle_update < last_node_update) {
		int    i;
		double alloc_tres = 0;
		double tot_tres   = 0;
		node_record_t *node_ptr;

		select_g_select_nodeinfo_set_all();

		for (i = 0; (node_ptr = next_node(&i)); i++) {
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
					slurm_conf.priority_flags, false);

			alloc_tres += node_alloc_tres;
			tot_tres   += node_tot_tres;
		}
		last_idle_update = last_node_update;

		if (tot_tres)
			sys_usage_per = (alloc_tres / tot_tres) * 100;
	}

	return sys_usage_per;
}

static int _queue_resv_list(void *x, void *key)
{
	job_queue_req_t *job_queue_req = (job_queue_req_t *) key;

	job_queue_req->resv_ptr = (slurmctld_resv_t *) x;

	if ((job_queue_req->job_ptr->bit_flags & JOB_PART_ASSIGNED) &&
	    job_queue_req->resv_ptr->part_ptr)
		job_queue_req->part_ptr = job_queue_req->resv_ptr->part_ptr;

	job_queue_append_internal(job_queue_req);

	return 0;
}

static void _job_queue_append(List job_queue, job_record_t *job_ptr,
			      part_record_t *part_ptr, uint32_t prio)
{
	job_queue_req_t job_queue_req = { .job_ptr = job_ptr,
					  .job_queue = job_queue,
					  .part_ptr = part_ptr,
					  .prio = prio };

	/* We have multiple reservations, process and end here */
	if (job_ptr->resv_list) {
		list_for_each(job_ptr->resv_list, _queue_resv_list,
			      &job_queue_req);
		return;
	}

	job_queue_append_internal(&job_queue_req);

	/*
	 * This means we requested a specific reservation, don't do any magnetic
	 * ones
	 */
	if (job_ptr->resv_name)
		return;

	job_resv_append_magnetic(&job_queue_req);
}

/* Return true if the job has some step still in a cleaning state, which
 * can happen on a Cray if a job is requeued and the step NHC is still running
 * after the requeued job is eligible to run again */
static uint16_t _is_step_cleaning(job_record_t *job_ptr)
{
	ListIterator step_iterator;
	step_record_t *step_ptr;
	uint16_t cleaning = 0;

	step_iterator = list_iterator_create(job_ptr->step_list);
	while ((step_ptr = list_next(step_iterator))) {
		/* Only check if not a pending step */
		if (step_ptr->step_id.step_id != SLURM_PENDING_STEP) {
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
static bool _job_runnable_test1(job_record_t *job_ptr, bool sched_plugin)
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
		last_job_update = now;
		sched_debug3("%pJ. State=PENDING. Reason=Cleaning.", job_ptr);
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

	job_indepen = job_independent(job_ptr);
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
		sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
			     job_ptr,
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
		last_job_update = now;
	}

	if (!job_indepen)	/* can not run now */
		return false;

	return true;
}

/*
 * Job and partition tests for ability to run now
 * IN job_ptr - job to test
 * IN now - update time
 * IN check_min_time - If set, test job's minimum time limit
 *		otherwise test maximum time limit
 */
static bool _job_runnable_test2(job_record_t *job_ptr, time_t now,
				bool check_min_time)
{
	int reason;

	reason = job_limits_check(&job_ptr, check_min_time);
	if ((reason != job_ptr->state_reason) &&
	    ((reason != WAIT_NO_REASON) ||
	     (!part_policy_job_runnable_state(job_ptr)))) {
		job_ptr->state_reason = reason;
		xfree(job_ptr->state_desc);
		last_job_update = now;
	}
	if (reason != WAIT_NO_REASON)
		return false;
	return true;
}

/*
 * Job, reservation and partition tests for ability to run now.
 * If a job is submitted to multiple partitions, don't consider partitions
 * on which the job would not fit given the current set of nodes in the
 * reservation.
 * IN job_ptr - job to test
 * IN part_ptr - partition to test
 */
static bool _job_runnable_test3(job_record_t *job_ptr, part_record_t *part_ptr)
{
	if (job_ptr->resv_ptr && job_ptr->resv_ptr->node_bitmap &&
	    part_ptr && part_ptr->node_bitmap &&
	    (bit_overlap(job_ptr->resv_ptr->node_bitmap, part_ptr->node_bitmap)
	     < job_ptr->node_cnt_wag))
		return false;
	return true;
}

extern void job_queue_rec_magnetic_resv(job_queue_rec_t *job_queue_rec)
{
	job_record_t *job_ptr;

	if (!job_queue_rec->resv_ptr)
		return;

	xassert(job_queue_rec->job_ptr);
	xassert(!job_queue_rec->job_ptr->resv_name);

	job_ptr = job_queue_rec->job_ptr;
	job_ptr->resv_ptr = job_queue_rec->resv_ptr;
	job_ptr->resv_name = xstrdup(job_ptr->resv_ptr->name);
	job_ptr->resv_id = job_ptr->resv_ptr->resv_id;
	job_queue_rec->job_ptr->bit_flags |= JOB_MAGNETIC;
}

extern void job_queue_rec_resv_list(job_queue_rec_t *job_queue_rec)
{
	job_record_t *job_ptr;

	if (!job_queue_rec->resv_ptr)
		return;

	xassert(job_queue_rec->job_ptr);

	job_ptr = job_queue_rec->job_ptr;
	job_ptr->resv_ptr = job_queue_rec->resv_ptr;
	/*
	 * Do not set the name since we have multiple and we don't want to
	 * overwrite it.
	 */
	job_ptr->resv_id = job_ptr->resv_ptr->resv_id;
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
	job_record_t *job_ptr = NULL, *new_job_ptr;
	part_record_t *part_ptr;
	depend_spec_t *dep_ptr;
	int i, pend_cnt, dep_corr;
	struct timeval start_tv = {0, 0};
	int tested_jobs = 0;
	int job_part_pairs = 0;
	time_t now = time(NULL);

	/* init the timer */
	(void) slurm_delta_tv(&start_tv);
	job_queue = list_create(xfree_ptr);

	/*
	 * Create individual job records for job arrays that need burst buffer
	 * staging
	 *
	 * NOTE: You can not use list_for_each for these loops here because
	 * job_array_post_sched and job_array_split could eventually call
	 * _create_job_record which appends to job_list causing deadlock.  The
	 * last one calls job_independent from _job_runnable_test1 which
	 * eventually calls list_find_first on job_list so it is not able
	 * either.
	 */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
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
			(void) job_array_post_sched(job_ptr);
			if (job_ptr->details && job_ptr->details->dependency &&
			    job_ptr->details->depend_list)
				fed_mgr_submit_remote_dependencies(job_ptr,
								   false,
								   false);
			continue;
		}
		job_ptr->array_task_id = i;
		new_job_ptr = job_array_split(job_ptr);
		if (new_job_ptr) {
			debug("%s: Split out %pJ for burst buffer use",
			      __func__, job_ptr);
			new_job_ptr->job_state = JOB_PENDING;
			new_job_ptr->start_time = (time_t) 0;
			/* Do NOT clear db_index here, it is handled when
			 * task_id_str is created elsewhere */
			(void) bb_g_job_validate2(job_ptr, NULL);
		} else {
			error("%s: Unable to copy record for %pJ",
			      __func__, job_ptr);
		}
	}

	/* Create individual job records for job arrays with
	 * depend_type == SLURM_DEPEND_AFTER_CORRESPOND */
	list_iterator_reset(job_iterator);
	while ((job_ptr = list_next(job_iterator))) {
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
		depend_iter = list_iterator_create(
			job_ptr->details->depend_list);
		dep_corr = 0;
		while ((dep_ptr = list_next(depend_iter))) {
			if (dep_ptr->depend_type ==
			    SLURM_DEPEND_AFTER_CORRESPOND) {
				dep_corr = 1;
				break;
			}
		}
		if (!dep_corr)
			continue;
		pend_cnt = num_pending_job_array_tasks(job_ptr->array_job_id);
		if (pend_cnt >= correspond_after_task_cnt)
			continue;
		if (job_ptr->array_recs->task_cnt < 1)
			continue;
		if (job_ptr->array_recs->task_cnt == 1) {
			job_ptr->array_task_id = i;
			(void) job_array_post_sched(job_ptr);
			if (job_ptr->details && job_ptr->details->dependency &&
			    job_ptr->details->depend_list)
				fed_mgr_submit_remote_dependencies(job_ptr,
								   false,
								   false);
			continue;
		}
		job_ptr->array_task_id = i;
		new_job_ptr = job_array_split(job_ptr);
		if (new_job_ptr) {
			info("%s: Split out %pJ for SLURM_DEPEND_AFTER_CORRESPOND use",
			     __func__, job_ptr);
			new_job_ptr->job_state = JOB_PENDING;
			new_job_ptr->start_time = (time_t) 0;
			/* Do NOT clear db_index here, it is handled when
			 * task_id_str is created elsewhere */
		} else {
			error("%s: Unable to copy record for %pJ",
			      __func__, job_ptr);
		}
	}

	list_iterator_reset(job_iterator);
	while ((job_ptr = list_next(job_iterator))) {
		if (IS_JOB_PENDING(job_ptr)) {
			/* Remove backfill flag */
			job_ptr->bit_flags &= ~BACKFILL_SCHED;
			set_job_failed_assoc_qos_ptr(job_ptr);
			acct_policy_handle_accrue_time(job_ptr, false);
			if ((job_ptr->state_reason != WAIT_NO_REASON) &&
			    (job_ptr->state_reason != WAIT_PRIORITY) &&
			    (job_ptr->state_reason != WAIT_RESOURCES) &&
			    (job_ptr->state_reason !=
			     job_ptr->state_reason_prev_db)) {
				job_ptr->state_reason_prev_db =
					job_ptr->state_reason;
				last_job_update = now;
			}
		}

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
		if (job_ptr->array_recs)
			job_ptr->array_recs->pend_run_tasks = 0;
		if (job_ptr->resv_list)
			job_ptr->resv_ptr = NULL;
		if (!_job_runnable_test1(job_ptr, clear_start))
			continue;

		if (job_ptr->part_ptr_list) {
			int inx = -1;
			part_iterator = list_iterator_create(
				job_ptr->part_ptr_list);
			while ((part_ptr = list_next(part_iterator))) {
				job_ptr->part_ptr = part_ptr;

				/* priority_array index matches part_ptr_list
				 * position: increment inx */
				inx++;

				if (!_job_runnable_test2(
					    job_ptr, now, backfill))
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
					error("Could not find partition %s for %pJ",
					      job_ptr->partition, job_ptr);
					continue;
				}
				job_ptr->part_ptr = part_ptr;
				error("partition pointer reset for %pJ, part %s",
				      job_ptr, job_ptr->partition);
				job_ptr->bit_flags |= JOB_PART_ASSIGNED;

			}
			if (!_job_runnable_test2(job_ptr, now, backfill))
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
	job_record_t *job_ptr = NULL;
	time_t recent;

	if ((job_list == NULL) || (slurm_conf.complete_wait == 0))
		return completing;

	recent = time(NULL) - slurm_conf.complete_wait;
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (IS_JOB_COMPLETING(job_ptr) &&
		    (job_ptr->end_time >= recent)) {
			completing = true;
			/* can return after finding first completing job so long
			 * as a map of nodes in partitions affected by
			 * completing jobs is not required */
			if (!eff_cg_bitmap)
				break;
			else if (job_ptr->part_ptr)
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
	job_record_t *job_ptr = NULL;
	part_record_t *part_ptr = NULL;
	ListIterator job_iterator;
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	time_t now = time(NULL);

	lock_slurmctld(job_write_lock);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
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
		if ((job_ptr->details->max_nodes != 0) &&
		    ((job_ptr->details->max_nodes < part_ptr->min_nodes) ||
		     (job_ptr->details->min_nodes > part_ptr->max_nodes)))
			continue;
		/* Job's eligible time is set in job_independent() */
		if (!job_independent(job_ptr))
			continue;
	}
	list_iterator_destroy(job_iterator);
	unlock_slurmctld(job_write_lock);
}

/* Test of part_ptr can still run jobs or if its nodes have
 * already been reserved by higher priority jobs (those in
 * the failed_parts array) */
static bool _failed_partition(part_record_t *part_ptr,
			      part_record_t **failed_parts,
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

/* Return true of all partitions have the same priority, otherwise false. */
static bool _all_partition_priorities_same(void)
{
	part_record_t *part_ptr;
	ListIterator iter;
	bool part_priority_set = false;
	uint32_t part_priority = 0;
	bool result = true;

	iter = list_iterator_create(part_list);
	while ((part_ptr = list_next(iter))) {
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
 * Queue requests of job scheduler
 */
extern void schedule(bool full_queue)
{

	if (slurmctld_config.scheduling_disabled)
		return;

	slurm_mutex_lock(&sched_mutex);
	sched_full_queue |= full_queue;
	slurm_cond_broadcast(&sched_cond);
	sched_requests++;
	slurm_mutex_unlock(&sched_mutex);
}

/* detached thread periodically attempts to schedule jobs */
static void *_sched_agent(void *args)
{
	long delta_t;
	struct timeval now;
	int job_cnt;
	bool full_queue;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "sched_agent", NULL, NULL, NULL) < 0) {
		error("cannot set my name to _sched_agent %m");
	}
#endif

	while (!slurmctld_config.shutdown_time) {

		slurm_mutex_lock(&sched_mutex);
		while (1) {
			if (slurmctld_config.shutdown_time) {
				slurm_mutex_unlock(&sched_mutex);
				return NULL;
			}

			gettimeofday(&now, NULL);
			delta_t  = (now.tv_sec  - sched_last.tv_sec) *
				   USEC_IN_SEC;
			delta_t +=  now.tv_usec - sched_last.tv_usec;

			if (sched_requests && delta_t > sched_min_interval ) {
				break;
			} else if (sched_requests) {
				struct timespec ts = {0, 0};
				int64_t nsec;

				nsec = sched_min_interval + sched_last.tv_usec;
				nsec *= NSEC_IN_USEC;
				nsec += NSEC_IN_USEC;
				ts.tv_sec = sched_last.tv_sec +
					    (nsec / NSEC_IN_SEC);
				ts.tv_nsec = nsec % NSEC_IN_SEC;
				slurm_cond_timedwait(&sched_cond,
						     &sched_mutex, &ts);
			} else {
				slurm_cond_wait(&sched_cond, &sched_mutex);
			}
		}

		full_queue = sched_full_queue;
		sched_full_queue = false;
		sched_requests = 0;
		slurm_mutex_unlock(&sched_mutex);

		job_cnt = _schedule(full_queue);
		gettimeofday(&now, NULL);
		sched_last.tv_sec  = now.tv_sec;
		sched_last.tv_usec = now.tv_usec;
		if (job_cnt) {
			/* jobs were started, save state */
			schedule_node_save();		/* Has own locking */
			schedule_job_save();		/* Has own locking */
		}
	}

	return NULL;
}

/* Determine if job's deadline specification is still valid, kill job if not
 * job_ptr IN - Job to test
 * func IN - function named used for logging, "sched" or "backfill"
 * RET - true of valid, false if invalid and job cancelled
 */
extern bool deadline_ok(job_record_t *job_ptr, char *func)
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
			info("%s: %pJ with time_min %u exceeded deadline %s and cancelled",
			     __func__, job_ptr, job_ptr->time_min,
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
			info("%s: %pJ with time_limit %u exceeded deadline %s and cancelled",
			     __func__, job_ptr, job_ptr->time_limit,
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

/*
 * When an array job is rejected for some reason, the remaining array tasks will
 * get skipped by both the main scheduler and the backfill scheduler (it's an
 * optimization). Hence, their reasons should match the reason of the first job.
 * This function sets those reasons.
 *
 * job_ptr		(IN) The current job being evaluated, after it has gone
 * 			through the scheduling loop.
 * reject_array_job	(IN) A pointer to the first job (array task) in the most
 * 			recently rejected array job. If job_ptr belongs to the
 * 			same array as reject_array_job, then set job_ptr's
 * 			reason to match reject_array_job.
 */
extern void fill_array_reasons(job_record_t *job_ptr,
			       job_record_t *reject_array_job)
{
	if (!reject_array_job || !reject_array_job->array_job_id)
		return;

	if (job_ptr == reject_array_job)
		return;

	/*
	 * If the current job is part of the rejected job array...
	 * And if the reason isn't properly set yet...
	 */
	if ((job_ptr->array_job_id == reject_array_job->array_job_id) &&
	    (job_ptr->state_reason != reject_array_job->state_reason)) {
		/* Set the reason for the subsequent array task */
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = reject_array_job->state_reason;
		last_job_update = time(NULL);
		debug3("%s: Setting reason of array task %pJ to %s",
		       __func__, job_ptr,
		       job_reason_string(job_ptr->state_reason));
	}
}

static job_queue_rec_t *_create_job_queue_rec(job_queue_req_t *job_queue_req)
{
	job_queue_rec_t *job_queue_rec = xmalloc(sizeof(*job_queue_rec));
	job_queue_rec->array_task_id = job_queue_req->job_ptr->array_task_id;
	job_queue_rec->job_id   = job_queue_req->job_ptr->job_id;
	job_queue_rec->job_ptr  = job_queue_req->job_ptr;
	job_queue_rec->part_ptr = job_queue_req->part_ptr;
	job_queue_rec->priority = job_queue_req->prio;
	job_queue_rec->resv_ptr = job_queue_req->resv_ptr;

	return job_queue_rec;
}

extern void job_queue_append_internal(job_queue_req_t *job_queue_req)
{
	job_queue_rec_t *job_queue_rec;

	xassert(job_queue_req);
	xassert(job_queue_req->job_ptr);
	xassert(job_queue_req->job_queue);
	xassert(job_queue_req->part_ptr);

	if (job_queue_req->job_ptr->details &&
	    job_queue_req->job_ptr->details->prefer) {
		job_queue_rec = _create_job_queue_rec(job_queue_req);
		job_queue_rec->use_prefer = true;
		list_append(job_queue_req->job_queue, job_queue_rec);
	}

	job_queue_rec = _create_job_queue_rec(job_queue_req);

	list_append(job_queue_req->job_queue, job_queue_rec);
}

static int _schedule(bool full_queue)
{
	ListIterator job_iterator = NULL, part_iterator = NULL;
	List job_queue = NULL;
	int failed_part_cnt = 0, failed_resv_cnt = 0, job_cnt = 0;
	int error_code, i, j, part_cnt, time_limit, pend_time;
	uint32_t job_depth = 0, array_task_id;
	job_queue_rec_t *job_queue_rec;
	job_record_t *job_ptr = NULL;
	part_record_t *part_ptr, **failed_parts = NULL, *skip_part_ptr = NULL;
	slurmctld_resv_t **failed_resv = NULL;
	bitstr_t *save_avail_node_bitmap;
	part_record_t **sched_part_ptr = NULL;
	int *sched_part_jobs = NULL, bb_wait_cnt = 0;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	bool is_job_array_head;
	static time_t sched_update = 0;
	static bool fifo_sched = false;
	static bool assoc_limit_stop = false;
	static int sched_timeout = 0;
	static int sched_max_job_start = 0;
	static int bf_min_age_reserve = 0;
	static uint32_t bf_min_prio_reserve = 0;
	static bool bf_licenses = false;
	static int def_job_limit = 100;
	static int max_jobs_per_part = 0;
	static int defer_rpc_cnt = 0;
	static bool reduce_completing_frag = false;
	time_t now, last_job_sched_start, sched_start;
	job_record_t *reject_array_job = NULL;
	part_record_t *reject_array_part = NULL;
	slurmctld_resv_t *reject_array_resv = NULL;
	bool fail_by_part, wait_on_resv;
	uint32_t deadline_time_limit, save_time_limit = 0;
	uint32_t prio_reserve;
	DEF_TIMERS;

	if (slurmctld_config.shutdown_time)
		return 0;

	if (sched_update != slurm_conf.last_update) {
		char *tmp_ptr;
		if (!xstrcmp(slurm_conf.schedtype, "sched/builtin") &&
		    !xstrcmp(slurm_conf.priority_type, "priority/basic") &&
		    _all_partition_priorities_same())
			fifo_sched = true;
		else
			fifo_sched = false;

		if (xstrcasestr(slurm_conf.sched_params, "assoc_limit_stop"))
			assoc_limit_stop = true;
		else
			assoc_limit_stop = false;

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "batch_sched_delay="))) {
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
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "bb_array_stage_cnt="))) {
			int task_cnt = atoi(tmp_ptr + 19);
			if (task_cnt > 0)
				bb_array_stage_cnt = task_cnt;
		}

		bf_min_age_reserve = 0;
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "bf_min_age_reserve="))) {
			int min_age = atoi(tmp_ptr + 19);
			if (min_age > 0)
				bf_min_age_reserve = min_age;
		}

		bf_min_prio_reserve = 0;
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "bf_min_prio_reserve="))) {
			int64_t min_prio = (int64_t) atoll(tmp_ptr + 20);
			if (min_prio > 0)
				bf_min_prio_reserve = (uint32_t) min_prio;
		}

		bf_licenses = false;
		if (xstrcasestr(slurm_conf.sched_params, "bf_licenses")) {
			if (!xstrcmp(slurm_conf.schedtype, "sched/builtin"))
				error("Ignoring SchedulerParameters=bf_licenses, this option is incompatible with sched/builtin.");
			else
				bf_licenses = true;
		}

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "build_queue_timeout="))) {
			build_queue_timeout = atoi(tmp_ptr + 20);
			if (build_queue_timeout < 100) {
				error("Invalid build_queue_time: %d",
				      build_queue_timeout);
				build_queue_timeout = BUILD_TIMEOUT;
			}
		} else {
			build_queue_timeout = BUILD_TIMEOUT;
		}

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "correspond_after_task_cnt="))) {
			correspond_after_task_cnt = atoi(tmp_ptr + 26);
			if (correspond_after_task_cnt <
			    CORRESPOND_ARRAY_TASK_CNT) {
				error("Invalid correspond_after_task_cnt: %d, the value can't be lower than %d",
				      correspond_after_task_cnt,
				      CORRESPOND_ARRAY_TASK_CNT);
				correspond_after_task_cnt =
					CORRESPOND_ARRAY_TASK_CNT;
			}
		} else {
			correspond_after_task_cnt = CORRESPOND_ARRAY_TASK_CNT;
		}


		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "default_queue_depth="))) {
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

		bf_hetjob_prio = 0;
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "bf_hetjob_prio="))) {
			tmp_ptr += 15;
			if (!xstrncasecmp(tmp_ptr, "min", 3))
				bf_hetjob_prio |= HETJOB_PRIO_MIN;
			else if (!xstrncasecmp(tmp_ptr, "max", 3))
				bf_hetjob_prio |= HETJOB_PRIO_MAX;
			else if (!xstrncasecmp(tmp_ptr, "avg", 3))
				bf_hetjob_prio |= HETJOB_PRIO_AVG;
			else
				error("Invalid SchedulerParameters bf_hetjob_prio: %s",
				      tmp_ptr);
		}

		bf_hetjob_immediate = false;
		if (xstrcasestr(slurm_conf.sched_params, "bf_hetjob_immediate"))
			bf_hetjob_immediate = true;

		if (bf_hetjob_immediate && !bf_hetjob_prio) {
			bf_hetjob_prio |= HETJOB_PRIO_MIN;
			info("bf_hetjob_immediate automatically sets bf_hetjob_prio=min");
		}

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "partition_job_depth="))) {
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

		if (xstrcasestr(slurm_conf.sched_params,
		                "reduce_completing_frag"))
			reduce_completing_frag = true;
		else
			reduce_completing_frag = false;

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "max_rpc_cnt=")))
			defer_rpc_cnt = atoi(tmp_ptr + 12);
		else if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
						"max_rpc_count=")))
			defer_rpc_cnt = atoi(tmp_ptr + 14);
		else
			defer_rpc_cnt = 0;
		if (defer_rpc_cnt < 0) {
			error("Invalid max_rpc_cnt: %d", defer_rpc_cnt);
			defer_rpc_cnt = 0;
		}

		time_limit = slurm_conf.msg_timeout / 2;
		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "max_sched_time="))) {
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

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
		                           "sched_interval="))) {
			sched_interval = atoi(tmp_ptr + 15);
			if (sched_interval == -1) {
				sched_debug("schedule() returning, sched_interval=-1");
				/*
				 * Exit without setting sched_update.  This gets
				 * verbose, but makes this setting easy to
				 * happen.
				 */
				goto out;
			} else if (sched_interval < 0) {
				error("Invalid sched_interval: %d",
				      sched_interval);
				sched_interval = 60;
			}
		} else {
			sched_interval = 60;
		}

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "sched_min_interval="))) {
			i = atoi(tmp_ptr + 19);
			if (i < 0)
				error("Invalid sched_min_interval: %d", i);
			else
				sched_min_interval = i;
		} else {
			sched_min_interval = 2;
		}

		if ((tmp_ptr = xstrcasestr(slurm_conf.sched_params,
					   "sched_max_job_start="))) {
			sched_max_job_start = atoi(tmp_ptr + 20);
			if (sched_max_job_start < 0) {
				error("Invalid sched_max_job_start: %d",
				      sched_max_job_start);
				sched_max_job_start = 0;
			}
		} else {
			sched_max_job_start = 0;
		}

		sched_update = slurm_conf.last_update;
		info("SchedulerParameters=default_queue_depth=%d,"
		     "max_rpc_cnt=%d,max_sched_time=%d,partition_job_depth=%d,"
		     "sched_max_job_start=%d,sched_min_interval=%d%s",
		     def_job_limit, defer_rpc_cnt, sched_timeout,
		     max_jobs_per_part, sched_max_job_start,
		     sched_min_interval, (bf_licenses ? ",bf_licenses" : ""));
	}

	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if ((defer_rpc_cnt > 0) &&
	    (slurmctld_config.server_thread_count >= defer_rpc_cnt)) {
		sched_debug("schedule() returning, too many RPCs");
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
		goto out;
	}
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

	if (!fed_mgr_sibs_synced()) {
		sched_info("schedule() returning, federation siblings not synced yet");
		goto out;
	}

	lock_slurmctld(job_write_lock);
	now = time(NULL);
	sched_start = now;
	last_job_sched_start = now;
	START_TIMER;
	if (!avail_front_end(NULL)) {
		ListIterator job_iterator = list_iterator_create(job_list);
		while ((job_ptr = list_next(job_iterator))) {
			if (!IS_JOB_PENDING(job_ptr))
				continue;
			if ((job_ptr->state_reason != WAIT_NO_REASON) &&
			    (job_ptr->state_reason != WAIT_RESOURCES) &&
			    (job_ptr->state_reason != WAIT_NODE_NOT_AVAIL))
				continue;
			job_ptr->state_reason = WAIT_FRONT_END;
			xfree(job_ptr->state_desc);
			last_job_update = now;
		}
		list_iterator_destroy(job_iterator);

		unlock_slurmctld(job_write_lock);
		sched_debug("schedule() returning, no front end nodes are available");
		goto out;
	}

	if (!reduce_completing_frag && job_is_completing(NULL)) {
		unlock_slurmctld(job_write_lock);
		sched_debug("schedule() returning, some job is still completing");
		goto out;
	}

	part_cnt = list_count(part_list);
	failed_parts = xcalloc(part_cnt, sizeof(part_record_t *));
	failed_resv = xcalloc(MAX_FAILED_RESV, sizeof(slurmctld_resv_t *));
	save_avail_node_bitmap = bit_copy(avail_node_bitmap);
	bit_or(avail_node_bitmap, rs_node_bitmap);

	/* Avoid resource fragmentation if important */
	if (reduce_completing_frag) {
		bitstr_t *eff_cg_bitmap = bit_alloc(node_record_count);
		if (job_is_completing(eff_cg_bitmap)) {
			ListIterator part_iterator;
			part_record_t *part_ptr = NULL;
			char *cg_part_str = NULL;

			part_iterator = list_iterator_create(part_list);
			while ((part_ptr = list_next(part_iterator))) {
				if (bit_overlap_any(eff_cg_bitmap,
						    part_ptr->node_bitmap) &&
				    (part_ptr->state_up & PARTITION_SCHED)) {
					failed_parts[failed_part_cnt++] =
						part_ptr;
					bit_and_not(avail_node_bitmap,
						    part_ptr->node_bitmap);
					if (slurm_conf.slurmctld_debug >=
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
				sched_debug("some job is still completing, skipping partitions '%s'",
					    cg_part_str);
				xfree(cg_part_str);
			}
		}
		bit_free(eff_cg_bitmap);
	}

	if (max_jobs_per_part) {
		ListIterator part_iterator;
		sched_part_ptr  = xcalloc(part_cnt, sizeof(part_record_t *));
		sched_part_jobs = xmalloc(sizeof(int) * part_cnt);
		part_iterator = list_iterator_create(part_list);
		i = 0;
		while ((part_ptr = list_next(part_iterator))) {
			sched_part_ptr[i++] = part_ptr;
		}
		list_iterator_destroy(part_iterator);
	}

	sched_debug("Running job scheduler %s.", full_queue ? "for full queue":"for default depth");
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

	job_ptr = NULL;
	wait_on_resv = false;
	while (1) {
		/* Run some final guaranteed logic after each job iteration */
		if (job_ptr) {
			job_resv_clear_magnetic_flag(job_ptr);
			fill_array_reasons(job_ptr, reject_array_job);
		}

		if (fifo_sched) {
			if (job_ptr && part_iterator &&
			    IS_JOB_PENDING(job_ptr)) /* test job in next part */
				goto next_part;
			job_ptr = list_next(job_iterator);
			if (!job_ptr)
				break;

			/* When not fifo we do this in build_job_queue(). */
			if (IS_JOB_PENDING(job_ptr)) {
				set_job_failed_assoc_qos_ptr(job_ptr);
				acct_policy_handle_accrue_time(job_ptr, false);
			}

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
next_part:
				part_ptr = list_next(part_iterator);

				if (!_job_runnable_test3(job_ptr, part_ptr))
					continue;

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
				if (!_job_runnable_test2(job_ptr, now, false))
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

			/*
			 * feature_list_use is a temporary variable and should
			 * be reset before each use.
			 */
			if (job_queue_rec->use_prefer) {
				job_ptr->details->features_use =
					job_ptr->details->prefer;
				job_ptr->details->feature_list_use =
					job_ptr->details->prefer_list;
			} else {
				job_ptr->details->features_use =
					job_ptr->details->features;
				job_ptr->details->feature_list_use =
					job_ptr->details->feature_list;
			}

			if (!avail_front_end(job_ptr)) {
				job_ptr->state_reason = WAIT_FRONT_END;
				xfree(job_ptr->state_desc);
				last_job_update = now;
				xfree(job_queue_rec);
				continue;
			}
			if ((job_ptr->array_task_id != array_task_id) &&
			    (array_task_id == NO_VAL)) {
				/* Job array element started in other partition,
				 * reset pointer to "master" job array record */
				job_ptr = find_job_record(job_ptr->array_job_id);
				job_queue_rec->job_ptr = job_ptr;
			}
			if (!job_ptr || !IS_JOB_PENDING(job_ptr)) {
				xfree(job_queue_rec);
				continue;	/* started in other partition */
			}

			if (job_ptr->resv_list)
				job_queue_rec_resv_list(job_queue_rec);
			else
				job_queue_rec_magnetic_resv(job_queue_rec);
			xfree(job_queue_rec);

			if (!_job_runnable_test3(job_ptr, part_ptr))
				continue;

			job_ptr->part_ptr = part_ptr;
		}

		job_ptr->last_sched_eval = time(NULL);

		if (job_ptr->preempt_in_progress)
			continue;	/* scheduled in another partition */

		if (job_ptr->het_job_id) {
			fail_by_part = true;
			goto fail_this_part;
		}

		if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL))
			is_job_array_head = true;
		else
			is_job_array_head = false;

next_task:
		if ((time(NULL) - sched_start) >= sched_timeout) {
			sched_debug("loop taking too long, breaking out");
			break;
		}
		if (sched_max_job_start && (job_cnt >= sched_max_job_start)) {
			sched_debug("sched_max_job_start reached, breaking out");
			break;
		}

		if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
			if (reject_array_job &&
			    (reject_array_job->array_job_id ==
				job_ptr->array_job_id) &&
			    (reject_array_part == part_ptr) &&
			    (reject_array_resv == job_ptr->resv_ptr))
				continue;  /* already rejected array element */

			/* assume reject whole array for now, clear if OK */
			reject_array_job = job_ptr;
			reject_array_part = part_ptr;
			reject_array_resv = job_ptr->resv_ptr;

			if (!job_array_start_test(job_ptr))
				continue;
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
				if (job_ptr->state_reason == WAIT_NO_REASON) {
					xfree(job_ptr->state_desc);
					job_ptr->state_reason = WAIT_PRIORITY;
					last_job_update = now;
				}
				if (job_ptr->part_ptr == skip_part_ptr)
					continue;
				sched_debug2("reached partition %s job limit",
					     job_ptr->part_ptr->name);
				skip_part_ptr = job_ptr->part_ptr;
				continue;
			}
		}
		if (!full_queue && (job_depth++ > def_job_limit)) {
			sched_debug("already tested %u jobs, breaking out",
				    job_depth);
			break;
		}

		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		if ((defer_rpc_cnt > 0) &&
		    (slurmctld_config.server_thread_count >= defer_rpc_cnt)) {
			sched_debug("schedule() returning, too many RPCs");
			slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
			break;
		}
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

		if (job_limits_check(&job_ptr, false) != WAIT_NO_REASON) {
			/* should never happen */
			continue;
		}

		slurmctld_diag_stats.schedule_cycle_depth++;

		if (job_ptr->resv_name) {
			bool found_resv = false;

			/*
			 * If we have a MaxStartDelay we need to make sure we
			 * don't schedule any jobs that could potentially run to
			 * avoid starvation of this job.
			 */
			if (job_ptr->resv_ptr &&
			    job_ptr->resv_ptr->max_start_delay)
				wait_on_resv = true;

			for (i = 0; i < failed_resv_cnt; i++) {
				if (failed_resv[i] == job_ptr->resv_ptr) {
					found_resv = true;
					break;
				}
			}
			if (found_resv) {
				job_ptr->state_reason = WAIT_PRIORITY;
				xfree(job_ptr->state_desc);
				last_job_update = now;
				sched_debug3("%pJ. State=PENDING. Reason=Priority. Priority=%u. Resv=%s.",
					     job_ptr,
					     job_ptr->priority,
					     job_ptr->resv_name);
				continue;
			}
		} else if (_failed_partition(job_ptr->part_ptr, failed_parts,
					     failed_part_cnt)) {
			if ((job_ptr->state_reason == WAIT_NO_REASON) ||
			    (job_ptr->state_reason == WAIT_RESOURCES)) {
				sched_debug("%pJ unable to schedule in Partition=%s (per _failed_partition()). State=PENDING. Previous-Reason=%s. Previous-Desc=%s. New-Reason=Priority. Priority=%u.",
					    job_ptr,
					    job_ptr->part_ptr->name,
					    job_reason_string(
						    job_ptr->state_reason),
					    job_ptr->state_desc,
					    job_ptr->priority);
				job_ptr->state_reason = WAIT_PRIORITY;
				xfree(job_ptr->state_desc);
				last_job_update = now;
			} else {
				/*
				 * Log job can not run even though we are not
				 * overriding the reason */
				sched_debug2("%pJ. unable to schedule in Partition=%s (per _failed_partition()). Retaining previous scheduling Reason=%s. Desc=%s. Priority=%u.",
					     job_ptr,
					     job_ptr->part_ptr->name,
					     job_reason_string(
						     job_ptr->state_reason),
					     job_ptr->state_desc,
					     job_ptr->priority);
			}
			last_job_update = now;

			continue;
		} else if (wait_on_resv &&
			   (job_ptr->warn_flags & KILL_JOB_RESV)) {
			sched_debug("%pJ. State=PENDING. Reason=Priority, Priority=%u. May be able to backfill on MaxStartDelay reservations.",
				    job_ptr, job_ptr->priority);
			continue;

		}

		/* Test for valid QOS and required nodes on each pass */
		if (job_ptr->qos_id) {
			assoc_mgr_lock_t locks =
				{ .assoc = READ_LOCK, .qos = READ_LOCK };

			assoc_mgr_lock(&locks);
			if (job_ptr->assoc_ptr
			    && (accounting_enforce & ACCOUNTING_ENFORCE_QOS)
			    && ((job_ptr->qos_id >= g_qos_count) ||
				!job_ptr->assoc_ptr->usage ||
				!job_ptr->assoc_ptr->usage->valid_qos ||
				!bit_test(job_ptr->assoc_ptr->usage->valid_qos,
					  job_ptr->qos_id))
			    && !job_ptr->limit_set.qos) {
				assoc_mgr_unlock(&locks);
				sched_debug("%pJ has invalid QOS", job_ptr);
				job_fail_qos(job_ptr, __func__);
				last_job_update = now;
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
			/*
			 * Too many nodes DRAIN, DOWN, or
			 * reserved for jobs in higher priority partition
			 */
			job_ptr->state_reason = WAIT_RESOURCES;
			xfree(job_ptr->state_desc);
			job_ptr->state_desc = xstrdup("Nodes required for job are DOWN, DRAINED or reserved for jobs in higher priority partitions");
			last_job_update = now;
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u. Partition=%s.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_reason_string(job_ptr->state_reason),
				     job_ptr->priority, job_ptr->partition);
			fail_by_part = true;
			goto fail_this_part;
		}
		if (license_job_test(job_ptr, time(NULL), true) !=
		    SLURM_SUCCESS) {
			job_ptr->state_reason = WAIT_LICENSES;
			xfree(job_ptr->state_desc);
			last_job_update = now;
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_reason_string(job_ptr->state_reason),
				     job_ptr->priority);
			if (bf_licenses) {
				sched_debug("%pJ is blocked on licenses. Stopping scheduling so license backfill can handle this",
					    job_ptr);
				break;
			}
			continue;
		}

		if (assoc_mgr_validate_assoc_id(acct_db_conn,
						job_ptr->assoc_id,
						accounting_enforce)) {
			/* NOTE: This only happens if a user's account is
			 * disabled between when the job was submitted and
			 * the time we consider running it. It should be
			 * very rare. */
			sched_info("%pJ has invalid account", job_ptr);
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

		error_code = select_nodes(job_ptr, false, NULL, NULL, false,
					  SLURMDB_JOB_FLAG_SCHED);

		if (error_code == SLURM_SUCCESS) {
			/*
			 * If the following fails because of network
			 * connectivity, the origin cluster should ask
			 * when it comes back up if the cluster_lock
			 * cluster actually started the job
			 */
			fed_mgr_job_start(job_ptr, job_ptr->start_time);
		} else {
			fed_mgr_job_unlock(job_ptr);
		}

skip_start:

		fail_by_part = false;
		if ((error_code != SLURM_SUCCESS) && deadline_time_limit)
			job_ptr->time_limit = save_time_limit;
		if (error_code == ESLURM_NODES_BUSY) {
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u. Partition=%s.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_reason_string(job_ptr->state_reason),
				     job_ptr->priority, job_ptr->partition);
			fail_by_part = true;
		} else if (error_code == ESLURM_BURST_BUFFER_WAIT) {
			if (job_ptr->start_time == 0) {
				job_ptr->start_time = last_job_sched_start;
				bb_wait_cnt++;
				/*
				 * Since start time wasn't set yet until this
				 * point, this means that the job hasn't had a
				 * chance to start stage-in yet. Clear
				 * reject_array_job so that other jobs in this
				 * array (if it was an array) may also have
				 * a chance to have a start time set and
				 * therefore have a chance to start stage-in.
				 */
				reject_array_job = NULL;
				reject_array_part = NULL;
				reject_array_resv = NULL;
			}
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_reason_string(job_ptr->state_reason),
				     job_ptr->priority);
			continue;
		} else if ((error_code == ESLURM_RESERVATION_BUSY) ||
			   (error_code == ESLURM_RESERVATION_NOT_USABLE)) {
			if (job_ptr->resv_ptr &&
			    job_ptr->resv_ptr->node_bitmap) {
				sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
					     job_ptr,
					     job_state_string(job_ptr->job_state),
					     job_reason_string(job_ptr->state_reason),
					     job_ptr->priority);
				bit_and_not(avail_node_bitmap,
					    job_ptr->resv_ptr->node_bitmap);
			} else {
				/*
				 * The job has no reservation but requires
				 * nodes that are currently in some reservation
				 * so just skip over this job and try running
				 * the next lower priority job
				 */
				sched_debug3("%pJ. State=%s. Reason=Required nodes are reserved. Priority=%u",
					     job_ptr,
					     job_state_string(job_ptr->job_state),
					     job_ptr->priority);
			}
		} else if (error_code == ESLURM_FED_JOB_LOCK) {
			job_ptr->state_reason = WAIT_FED_JOB_LOCK;
			xfree(job_ptr->state_desc);
			last_job_update = now;
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u. Partition=%s.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_reason_string(job_ptr->state_reason),
				     job_ptr->priority, job_ptr->partition);
			fail_by_part = true;
		} else if (error_code == SLURM_SUCCESS) {
			/* job initiated */
			sched_debug3("%pJ initiated", job_ptr);
			last_job_update = now;

			/* Clear assumed rejected array status */
			reject_array_job = NULL;
			reject_array_part = NULL;
			reject_array_resv = NULL;

			sched_info("Allocate %pJ NodeList=%s #CPUs=%u Partition=%s",
				   job_ptr, job_ptr->nodes,
				   job_ptr->total_cpus,
				   job_ptr->part_ptr->name);
			if (job_ptr->batch_flag == 0)
				srun_allocate(job_ptr);
			else if (!IS_JOB_CONFIGURING(job_ptr))
				launch_job(job_ptr);
			rebuild_job_part_list(job_ptr);
			job_cnt++;
			if (is_job_array_head &&
			    (job_ptr->array_task_id != NO_VAL)) {
				/* Try starting another task of the job array */
				job_record_t *tmp = job_ptr;
				job_ptr = find_job_record(job_ptr->array_job_id);
				if (job_ptr && (job_ptr != tmp) &&
				    IS_JOB_PENDING(job_ptr) &&
				    (bb_g_job_test_stage_in(job_ptr,false) ==1))
					goto next_task;
			}
			continue;
		} else if ((error_code ==
			    ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			   job_ptr->part_ptr_list) {
			debug("%pJ non-runnable in partition %s: %s",
			      job_ptr, job_ptr->part_ptr->name,
			      slurm_strerror(error_code));
		} else if (error_code == ESLURM_ACCOUNTING_POLICY) {
			sched_debug3("%pJ delayed for accounting policy",
				     job_ptr);
			/* potentially starve this job */
			if (assoc_limit_stop)
				fail_by_part = true;
		} else if ((error_code !=
			    ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
			   (error_code != ESLURM_NODE_NOT_AVAIL)      &&
			   (error_code != ESLURM_INVALID_BURST_BUFFER_REQUEST)){
			sched_info("schedule: %pJ non-runnable: %s",
				   job_ptr, slurm_strerror(error_code));
			last_job_update = now;
			job_ptr->job_state = JOB_PENDING;
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_ptr->priority = 0;
			debug2("%s: setting %pJ to \"%s\" (%s)",
			       __func__, job_ptr,
			       job_reason_string(job_ptr->state_reason),
			       slurm_strerror(error_code));
		}

		if (job_ptr->details && job_ptr->details->req_node_bitmap &&
		    (bit_set_count(job_ptr->details->req_node_bitmap) >=
		     job_ptr->details->min_nodes)) {
			fail_by_part = false;
			/* Do not schedule more jobs on nodes required by this
			 * job, but don't block the entire queue/partition. */
			bit_and_not(avail_node_bitmap,
				    job_ptr->details->req_node_bitmap);
		}
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
				pend_time = difftime(
					now, job_ptr->details->begin_time);
				if (pend_time < bf_min_age_reserve)
					fail_by_part = false;
			}
		}

		if (!(prio_reserve = acct_policy_get_prio_thresh(
			      job_ptr, false)))
			prio_reserve = bf_min_prio_reserve;

		if (fail_by_part && prio_reserve &&
		    (job_ptr->priority < prio_reserve))
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
	}

	if (bb_wait_cnt)
		(void) bb_g_job_try_stage_in();

	if (job_ptr)
		job_resv_clear_magnetic_flag(job_ptr);
	FREE_NULL_BITMAP(avail_node_bitmap);
	avail_node_bitmap = save_avail_node_bitmap;
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
	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if ((slurmctld_config.server_thread_count >= 150) &&
	    (defer_rpc_cnt == 0)) {
		sched_info("%d pending RPCs at cycle end, consider configuring max_rpc_cnt",
			   slurmctld_config.server_thread_count);
	}
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
	unlock_slurmctld(job_write_lock);
	END_TIMER2("schedule");

	_do_diag_stats(DELTA_TIMER);

out:
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
	het_job_details_t *details = NULL;
	bool has_resv1, has_resv2;
	static time_t config_update = 0;
	static bool preemption_enabled = true;
	uint32_t job_id1, job_id2;
	uint32_t p1, p2;

	/* The following block of code is designed to minimize run time in
	 * typical configurations for this frequently executed function. */
	if (config_update != slurm_conf.last_update) {
		preemption_enabled = slurm_preemption_enabled();
		config_update = slurm_conf.last_update;
	}
	if (preemption_enabled) {
		if (preempt_g_job_preempt_check(job_rec1, job_rec2))
			return -1;
		if (preempt_g_job_preempt_check(job_rec2, job_rec1))
			return 1;
	}

	if (bf_hetjob_prio && job_rec1->job_ptr->het_job_id &&
	    (job_rec1->job_ptr->het_job_id !=
	     job_rec2->job_ptr->het_job_id)) {
		if ((details = job_rec1->job_ptr->het_details))
			has_resv1 = details->any_resv;
		else
			has_resv1 = (job_rec1->job_ptr->resv_id != 0) ||
				job_rec1->resv_ptr;
	} else
		has_resv1 = (job_rec1->job_ptr->resv_id != 0) ||
			job_rec1->resv_ptr;

	if (bf_hetjob_prio && job_rec2->job_ptr->het_job_id &&
	    (job_rec2->job_ptr->het_job_id !=
	     job_rec1->job_ptr->het_job_id)) {
		if ((details = job_rec2->job_ptr->het_details))
			has_resv2 = details->any_resv;
		else
			has_resv2 = (job_rec2->job_ptr->resv_id != 0) ||
				job_rec2->resv_ptr;
	} else
		has_resv2 = (job_rec2->job_ptr->resv_id != 0) ||
			job_rec2->resv_ptr;

	if (has_resv1 && !has_resv2)
		return -1;
	if (!has_resv1 && has_resv2)
		return 1;

	if (job_rec1->part_ptr && job_rec2->part_ptr) {
		if (bf_hetjob_prio && job_rec1->job_ptr->het_job_id &&
		    (job_rec1->job_ptr->het_job_id !=
		     job_rec2->job_ptr->het_job_id)) {
			if ((details = job_rec1->job_ptr->het_details))
				p1 = details->priority_tier;
			else
				p1 = job_rec1->part_ptr->priority_tier;
		} else
			p1 = job_rec1->part_ptr->priority_tier;

		if (bf_hetjob_prio && job_rec2->job_ptr->het_job_id &&
		    (job_rec2->job_ptr->het_job_id !=
		     job_rec1->job_ptr->het_job_id)) {
			if ((details = job_rec2->job_ptr->het_details))
				p2 = details->priority_tier;
			else
				p2 = job_rec2->part_ptr->priority_tier;
		} else
			p2 = job_rec2->part_ptr->priority_tier;

		if (p1 < p2)
			return 1;
		if (p1 > p2)
			return -1;
	}

	if (bf_hetjob_prio && job_rec1->job_ptr->het_job_id &&
	    (job_rec1->job_ptr->het_job_id !=
	     job_rec2->job_ptr->het_job_id)) {
		if ((details = job_rec1->job_ptr->het_details))
			p1 = details->priority;
		else {
			if (job_rec1->job_ptr->part_ptr_list &&
			    job_rec1->job_ptr->priority_array)
				p1 = job_rec1->priority;
			else
				p1 = job_rec1->job_ptr->priority;
		}
	} else {
		if (job_rec1->job_ptr->part_ptr_list &&
		    job_rec1->job_ptr->priority_array)
			p1 = job_rec1->priority;
		else
			p1 = job_rec1->job_ptr->priority;
	}

	if (bf_hetjob_prio && job_rec2->job_ptr->het_job_id &&
	    (job_rec2->job_ptr->het_job_id !=
	     job_rec1->job_ptr->het_job_id)) {
		if ((details = job_rec2->job_ptr->het_details))
			p2 = details->priority;
		else {
			if (job_rec2->job_ptr->part_ptr_list &&
			    job_rec2->job_ptr->priority_array)
				p2 = job_rec2->priority;
			else
				p2 = job_rec2->job_ptr->priority;
		}
	} else {
		if (job_rec2->job_ptr->part_ptr_list &&
		    job_rec2->job_ptr->priority_array)
			p2 = job_rec2->priority;
		else
			p2 = job_rec2->job_ptr->priority;
	}

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

	/* Magnetic or multi-reservation. */
	if (job_rec1->resv_ptr && job_rec2->resv_ptr &&
	    (job_rec1->resv_ptr->start_time > job_rec2->resv_ptr->start_time))
		return 1;

	return -1;
}

/* The environment" variable is points to one big xmalloc. In order to
 * manipulate the array for a hetjob, we need to split it into an array
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
static batch_job_launch_msg_t *_build_launch_job_msg(job_record_t *job_ptr,
						     uint16_t protocol_version)
{
	char *fail_why = NULL;
	batch_job_launch_msg_t *launch_msg_ptr;

	/* Initialization of data structures */
	launch_msg_ptr = (batch_job_launch_msg_t *)
		xmalloc(sizeof(batch_job_launch_msg_t));
	launch_msg_ptr->job_id = job_ptr->job_id;
	launch_msg_ptr->het_job_id = job_ptr->het_job_id;
	launch_msg_ptr->array_job_id = job_ptr->array_job_id;
	launch_msg_ptr->array_task_id = job_ptr->array_task_id;
	launch_msg_ptr->uid = job_ptr->user_id;
	launch_msg_ptr->gid = job_ptr->group_id;

	if (!(launch_msg_ptr->script_buf = get_job_script(job_ptr))) {
		fail_why = "Unable to load job batch script";
		goto job_failed;
	}

	launch_msg_ptr->ntasks = job_ptr->details->num_tasks;
	launch_msg_ptr->alias_list = xstrdup(job_ptr->alias_list);
	launch_msg_ptr->container = xstrdup(job_ptr->container);
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
		error("Can not create job credential, attempting to requeue batch %pJ",
		      job_ptr);
		slurm_free_job_launch_msg(launch_msg_ptr);
		job_ptr->batch_flag = 1;	/* Allow repeated requeue */
		job_ptr->details->begin_time = time(NULL) + 120;
		job_complete(job_ptr->job_id, slurm_conf.slurm_user_id,
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

	launch_msg_ptr->argc = job_ptr->details->argc;
	launch_msg_ptr->argv = xduparray(job_ptr->details->argc,
					 job_ptr->details->argv);
	launch_msg_ptr->spank_job_env_size = job_ptr->spank_job_env_size;
	launch_msg_ptr->spank_job_env = xduparray(job_ptr->spank_job_env_size,
						  job_ptr->spank_job_env);
	launch_msg_ptr->environment = get_job_env(job_ptr,
						  &launch_msg_ptr->envc);
	if (!launch_msg_ptr->container && !launch_msg_ptr->environment) {
		fail_why = "Unable to load job environment";
		goto job_failed;
	}

	_split_env(launch_msg_ptr);
	launch_msg_ptr->job_mem = job_ptr->details->pn_min_memory;
	launch_msg_ptr->num_cpu_groups = job_ptr->job_resrcs->cpu_array_cnt;
	launch_msg_ptr->cpus_per_node  = xmalloc(
		sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt);
	memcpy(launch_msg_ptr->cpus_per_node,
	       job_ptr->job_resrcs->cpu_array_value,
	       (sizeof(uint16_t) * job_ptr->job_resrcs->cpu_array_cnt));
	launch_msg_ptr->cpu_count_reps  = xmalloc(
		sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt);
	memcpy(launch_msg_ptr->cpu_count_reps,
	       job_ptr->job_resrcs->cpu_array_reps,
	       (sizeof(uint32_t) * job_ptr->job_resrcs->cpu_array_cnt));

	launch_msg_ptr->select_jobinfo = select_g_select_jobinfo_copy(
		job_ptr->select_jobinfo);

	if (job_ptr->qos_ptr) {
		if (!xstrcmp(job_ptr->qos_ptr->description,
			     "Normal QOS default"))
			launch_msg_ptr->qos = xstrdup("normal");
		else
			launch_msg_ptr->qos = xstrdup(
				job_ptr->qos_ptr->description);
	}
	launch_msg_ptr->account = xstrdup(job_ptr->account);

	/*
	 * Use resv_ptr->name instead of job_ptr->resv_name as the job
	 * could contain multiple reservation names.
	 */
	if (job_ptr->resv_ptr)
		launch_msg_ptr->resv_name = xstrdup(job_ptr->resv_ptr->name);

	xassert(!fail_why);
	return launch_msg_ptr;

job_failed:
	/* fatal or kill the job as it can never be recovered */
	if (!ignore_state_errors)
		fatal("%s: %s for %pJ. Check file system serving StateSaveLocation as that directory may be missing or corrupted. Start with '-i' to ignore this error and kill the afflicted jobs.",
		      __func__, fail_why, job_ptr);

	error("%s: %s for %pJ. %pJ will be killed due to system error.",
	      __func__, fail_why, job_ptr, job_ptr);
	xfree(job_ptr->state_desc);
	job_ptr->state_desc = xstrdup(fail_why);
	job_ptr->state_reason = FAIL_SYSTEM;
	last_job_update = time(NULL);
	slurm_free_job_launch_msg(launch_msg_ptr);
	/* ignore the return as job is in an unknown state anyway */
	job_complete(job_ptr->job_id, slurm_conf.slurm_user_id, false, false,
	             1);
	return NULL;
}

/* Validate the job is ready for launch
 * RET pointer to batch job to launch or NULL if not ready yet */
static job_record_t *_het_job_ready(job_record_t *job_ptr)
{
	job_record_t *het_job_leader, *het_job;
	ListIterator iter;

	if (job_ptr->het_job_id == 0)	/* Not a hetjob */
		return job_ptr;

	het_job_leader = find_job_record(job_ptr->het_job_id);
	if (!het_job_leader) {
		error("Hetjob leader %pJ not found", job_ptr);
		return NULL;
	}
	if (!het_job_leader->het_job_list) {
		error("Hetjob leader %pJ lacks het_job_list", job_ptr);
		return NULL;
	}

	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		uint8_t prolog = 0;
		if (het_job_leader->het_job_id != het_job->het_job_id) {
			error("%s: Bad het_job_list for %pJ",
			      __func__, het_job_leader);
			continue;
		}
		if (job_ptr->details)
			prolog = het_job->details->prolog_running;
		if (prolog || IS_JOB_CONFIGURING(het_job) ||
		    !test_job_nodes_ready(het_job)) {
			het_job_leader = NULL;
			break;
		}
		if ((job_ptr->batch_flag == 0) ||
		    (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))) {
			het_job_leader = NULL;
			break;
		}
	}
	list_iterator_destroy(iter);

	if (het_job_leader)
		log_flag(HETJOB, "Batch hetjob %pJ being launched",
			 het_job_leader);
	else if (het_job)
		log_flag(HETJOB, "Batch hetjob %pJ waiting for job to be ready",
			 het_job);

	return het_job_leader;
}

/*
 * Set some hetjob environment variables. This will include information
 * about multiple job components (i.e. different slurmctld job records).
 */
static void _set_het_job_env(job_record_t *het_job_leader,
			     batch_job_launch_msg_t *launch_msg_ptr)
{
	job_record_t *het_job;
	int i, het_job_offset = 0;
	ListIterator iter;

	if (het_job_leader->het_job_id == 0)
		return;
	if (!launch_msg_ptr->environment) {
		error("%pJ lacks environment", het_job_leader);
		return;
	}
	if (!het_job_leader->het_job_list) {
		error("Hetjob leader %pJ lacks het_job_list",
		      het_job_leader);
		return;
	}

	/* "environment" needs NULL terminator */
	xrealloc(launch_msg_ptr->environment,
		 sizeof(char *) * (launch_msg_ptr->envc + 1));
	iter = list_iterator_create(het_job_leader->het_job_list);
	while ((het_job = list_next(iter))) {
		uint16_t cpus_per_task = 1;
		uint32_t num_cpus = 0;
		uint64_t tmp_mem = 0;
		char *tmp_str = NULL;

		if (het_job_leader->het_job_id != het_job->het_job_id) {
			error("%s: Bad het_job_list for %pJ",
			      __func__, het_job_leader);
			continue;
		}
		if (het_job->details &&
		    (het_job->details->cpus_per_task > 0) &&
		    (het_job->details->cpus_per_task != NO_VAL16)) {
			cpus_per_task = het_job->details->cpus_per_task;
		}
		if (het_job->account) {
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_ACCOUNT",
				het_job_offset, "%s", het_job->account);
		}

		if (het_job->job_resrcs) {
			tmp_str = uint32_compressed_to_str(
				het_job->job_resrcs->cpu_array_cnt,
				het_job->job_resrcs->cpu_array_value,
				het_job->job_resrcs->cpu_array_reps);
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_CPUS_PER_NODE",
				het_job_offset, "%s", tmp_str);
			xfree(tmp_str);
		}
		(void) env_array_overwrite_het_fmt(
			&launch_msg_ptr->environment,
			"SLURM_JOB_ID",
			het_job_offset, "%u", het_job->job_id);
		(void) env_array_overwrite_het_fmt(
			&launch_msg_ptr->environment,
			"SLURM_JOB_NAME",
			het_job_offset, "%s", het_job->name);
		(void) env_array_overwrite_het_fmt(
			&launch_msg_ptr->environment,
			"SLURM_JOB_NODELIST",
			het_job_offset, "%s", het_job->nodes);
		(void) env_array_overwrite_het_fmt(
			&launch_msg_ptr->environment,
			"SLURM_JOB_NUM_NODES",
			het_job_offset, "%u", het_job->node_cnt);
		if (het_job->partition) {
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_PARTITION",
				het_job_offset, "%s", het_job->partition);
		}
		if (het_job->qos_ptr) {
			slurmdb_qos_rec_t *qos;
			char *qos_name;

			qos = (slurmdb_qos_rec_t *) het_job->qos_ptr;
			if (!xstrcmp(qos->description, "Normal QOS default"))
				qos_name = "normal";
			else
				qos_name = qos->description;
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_QOS",
				het_job_offset, "%s", qos_name);
		}
		if (het_job->resv_ptr) {
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_JOB_RESERVATION",
				het_job_offset, "%s", het_job->resv_ptr->name);
		}
		if (het_job->details)
			tmp_mem = het_job->details->pn_min_memory;
		if (tmp_mem & MEM_PER_CPU) {
			tmp_mem &= (~MEM_PER_CPU);
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_MEM_PER_CPU",
				het_job_offset, "%"PRIu64"", tmp_mem);
		} else if (tmp_mem) {
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_MEM_PER_NODE",
				het_job_offset, "%"PRIu64"", tmp_mem);
		}
		if (het_job->alias_list) {
			(void) env_array_overwrite_het_fmt(
				&launch_msg_ptr->environment,
				"SLURM_NODE_ALIASES", het_job_offset,
				"%s", het_job->alias_list);
		}
		if (het_job->details && het_job->job_resrcs) {
			/* Both should always be set for active jobs */
			struct job_resources *resrcs_ptr = het_job->job_resrcs;
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

			if (het_job->details->num_tasks) {
				step_layout_req.num_tasks =
					het_job->details->num_tasks;
			} else {
				step_layout_req.num_tasks = num_cpus /
					cpus_per_task;
			}
			step_layout_req.num_hosts = het_job->node_cnt;

			if ((step_layout_req.node_list =
			     getenvp(launch_msg_ptr->environment,
				     "SLURM_ARBITRARY_NODELIST"))) {
				task_dist = SLURM_DIST_ARBITRARY;
			} else {
				step_layout_req.node_list = het_job->nodes;
				task_dist = SLURM_DIST_BLOCK;
			}
			step_layout_req.cpus_per_node =
				het_job->job_resrcs->cpu_array_value;
			step_layout_req.cpu_count_reps =
				het_job->job_resrcs->cpu_array_reps;
			cpus_per_task_array[0] = cpus_per_task;
			step_layout_req.cpus_per_task = cpus_per_task_array;
			cpus_task_reps[0] = het_job->node_cnt;
			step_layout_req.cpus_task_reps = cpus_task_reps;
			step_layout_req.task_dist = task_dist;
			step_layout_req.plane_size = NO_VAL16;
			step_layout = slurm_step_layout_create(&step_layout_req);
			if (step_layout) {
				tmp_str = uint16_array_to_str(
					step_layout->node_cnt,
					step_layout->tasks);
				slurm_step_layout_destroy(step_layout);
				(void) env_array_overwrite_het_fmt(
					&launch_msg_ptr->environment,
					"SLURM_TASKS_PER_NODE",
					het_job_offset,"%s", tmp_str);
				xfree(tmp_str);
			}
		} else if (IS_JOB_RUNNING(het_job)) {
			if (!het_job->details)
				error("%s: %pJ has null details member",
				      __func__, het_job);
			if (!het_job->job_resrcs)
				error("%s: %pJ has null job_resrcs member",
				      __func__, het_job);
		}
		het_job_offset++;
	}
	list_iterator_destroy(iter);
	/* Continue support for old hetjob terminology. */
	(void) env_array_overwrite_fmt(&launch_msg_ptr->environment,
				       "SLURM_PACK_SIZE", "%d", het_job_offset);
	(void) env_array_overwrite_fmt(&launch_msg_ptr->environment,
				       "SLURM_HET_SIZE", "%d", het_job_offset);

	for (i = 0; launch_msg_ptr->environment[i]; i++)
		;
	launch_msg_ptr->envc = i;
}

/*
 * launch_job - send an RPC to a slurmd to initiate a batch job
 * IN job_ptr - pointer to job that will be initiated
 */
extern void launch_job(job_record_t *job_ptr)
{
	batch_job_launch_msg_t *launch_msg_ptr;
	uint16_t protocol_version = NO_VAL16;
	agent_arg_t *agent_arg_ptr;
	job_record_t *launch_job_ptr;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#else
	node_record_t *node_ptr;
#endif

	xassert(job_ptr);
	xassert(job_ptr->batch_flag);

	if (job_ptr->total_cpus == 0)
		return;

	launch_job_ptr = _het_job_ready(job_ptr);
	if (!launch_job_ptr)
		return;

	if (pick_batch_host(launch_job_ptr) != SLURM_SUCCESS)
		return;

#ifdef HAVE_FRONT_END
	front_end_ptr = find_front_end_record(job_ptr->batch_host);
	if (front_end_ptr)
		protocol_version = front_end_ptr->protocol_version;
#else
	node_ptr = find_node_record(job_ptr->batch_host);
	if (node_ptr)
		protocol_version = node_ptr->protocol_version;
#endif

	(void)build_batch_step(job_ptr);

	launch_msg_ptr = _build_launch_job_msg(launch_job_ptr,protocol_version);
	if (launch_msg_ptr == NULL)
		return;
	if (launch_job_ptr->het_job_id)
		_set_het_job_env(launch_job_ptr, launch_msg_ptr);

	agent_arg_ptr = xmalloc(sizeof(agent_arg_t));
	agent_arg_ptr->protocol_version = protocol_version;
	agent_arg_ptr->node_count = 1;
	agent_arg_ptr->retry = 0;
	xassert(job_ptr->batch_host);
	agent_arg_ptr->hostlist = hostlist_create(launch_job_ptr->batch_host);
	agent_arg_ptr->msg_type = REQUEST_BATCH_JOB_LAUNCH;
	agent_arg_ptr->msg_args = (void *) launch_msg_ptr;
	set_agent_arg_r_uid(agent_arg_ptr, SLURM_AUTH_UID_ANY);

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
			       job_record_t *job_ptr,
			       uint16_t protocol_version)
{
	slurm_cred_arg_t cred_arg;
	job_resources_t *job_resrcs_ptr;

	xassert(job_ptr->job_resrcs);
	job_resrcs_ptr = job_ptr->job_resrcs;

	if (job_ptr->job_resrcs == NULL) {
		error("%s: %pJ is missing job_resrcs info",
		      __func__, job_ptr);
		return SLURM_ERROR;
	}

	setup_cred_arg(&cred_arg, job_ptr);

	cred_arg.step_id.job_id = launch_msg_ptr->job_id;
	cred_arg.step_id.step_id = SLURM_BATCH_SCRIPT;
	cred_arg.step_id.step_het_comp = NO_VAL;
	if (job_resrcs_ptr->memory_allocated) {
		int batch_inx = job_get_node_inx(
			job_ptr->batch_host, job_ptr->node_bitmap);

		if (batch_inx == -1) {
			error("%s: Invalid batch host %s for %pJ; this should never happen",
			      __func__, job_ptr->batch_host, job_ptr);
			batch_inx = 0;
		}
		cred_arg.job_mem_alloc = xmalloc(sizeof(uint64_t));
		cred_arg.job_mem_alloc[0] =
			job_resrcs_ptr->memory_allocated[batch_inx];
		cred_arg.job_mem_alloc_rep_count = xmalloc(sizeof(uint64_t));
		cred_arg.job_mem_alloc_rep_count[0] = 1;
		cred_arg.job_mem_alloc_size = 1;
	}
/*	cred_arg.step_gres_list      = NULL; */

	xassert(job_ptr->batch_host);
	cred_arg.step_hostlist       = job_ptr->batch_host;
	cred_arg.step_core_bitmap    = job_resrcs_ptr->core_bitmap;
	cred_arg.step_mem_limit      = job_ptr->details->pn_min_memory;

	launch_msg_ptr->cred = slurm_cred_create(slurmctld_config.cred_ctx,
						 &cred_arg, false,
						 protocol_version);
	xfree(cred_arg.job_mem_alloc);
	xfree(cred_arg.job_mem_alloc_rep_count);

	if (launch_msg_ptr->cred)
		return SLURM_SUCCESS;
	error("slurm_cred_create failure for batch job %u",
	      cred_arg.step_id.job_id);
	return SLURM_ERROR;
}

/*
 * Copy a job's dependency list
 * IN depend_list_src - a job's depend_lst
 * RET copy of depend_list_src, must bee freed by caller
 */
extern List depended_list_copy(List depend_list_src)
{
	depend_spec_t *dep_src, *dep_dest;
	ListIterator iter;
	List depend_list_dest = NULL;

	if (!depend_list_src)
		return depend_list_dest;

	depend_list_dest = list_create(xfree_ptr);
	iter = list_iterator_create(depend_list_src);
	while ((dep_src = list_next(iter))) {
		dep_dest = xmalloc(sizeof(depend_spec_t));
		memcpy(dep_dest, dep_src, sizeof(depend_spec_t));
		list_append(depend_list_dest, dep_dest);
	}
	list_iterator_destroy(iter);
	return depend_list_dest;
}

static char *_depend_type2str(depend_spec_t *dep_ptr)
{
	xassert(dep_ptr);

	switch (dep_ptr->depend_type) {
	case SLURM_DEPEND_AFTER:
		return "after";
	case SLURM_DEPEND_AFTER_ANY:
		return "afterany";
	case SLURM_DEPEND_AFTER_NOT_OK:
		return "afternotok";
	case SLURM_DEPEND_AFTER_OK:
		return "afterok";
	case SLURM_DEPEND_AFTER_CORRESPOND:
		return "aftercorr";
	case SLURM_DEPEND_EXPAND:
		return "expand";
	case SLURM_DEPEND_BURST_BUFFER:
		return "afterburstbuffer";
	case SLURM_DEPEND_SINGLETON:
		return "singleton";
	default:
		return "unknown";
	}
}

static uint32_t _depend_state_str2state(char *state_str)
{
	if (!xstrcasecmp(state_str, "fulfilled"))
		return DEPEND_FULFILLED;
	if (!xstrcasecmp(state_str, "failed"))
		return DEPEND_FAILED;
	/* Default to not fulfilled */
	return DEPEND_NOT_FULFILLED;
}

static char *_depend_state2str(depend_spec_t *dep_ptr)
{
	xassert(dep_ptr);

	switch(dep_ptr->depend_state) {
	case DEPEND_NOT_FULFILLED:
		return "unfulfilled";
	case DEPEND_FULFILLED:
		return "fulfilled";
	case DEPEND_FAILED:
		return "failed";
	default:
		return "unknown";
	}
}

static void _depend_list2str(job_record_t *job_ptr, bool set_or_flag)
{
	ListIterator depend_iter;
	depend_spec_t *dep_ptr;
	char *dep_str, *sep = "";

	if (job_ptr->details == NULL)
		return;

	xfree(job_ptr->details->dependency);

	if (job_ptr->details->depend_list == NULL
	    || list_count(job_ptr->details->depend_list) == 0)
		return;

	depend_iter = list_iterator_create(job_ptr->details->depend_list);
	while ((dep_ptr = list_next(depend_iter))) {
		/*
		 * Show non-fulfilled (including failed) dependencies, but don't
		 * show fulfilled dependencies.
		 */
		if (dep_ptr->depend_state == DEPEND_FULFILLED)
			continue;
		if      (dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) {
			xstrfmtcat(job_ptr->details->dependency,
				   "%ssingleton(%s)",
				   sep, _depend_state2str(dep_ptr));
		} else {
			dep_str = _depend_type2str(dep_ptr);

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

			if (dep_ptr->depend_time)
				xstrfmtcat(job_ptr->details->dependency,
					   "+%u", dep_ptr->depend_time / 60);
			xstrfmtcat(job_ptr->details->dependency, "(%s)",
				   _depend_state2str(dep_ptr));
		}
		if (set_or_flag)
			dep_ptr->depend_flags |= SLURM_FLAGS_OR;
		if (dep_ptr->depend_flags & SLURM_FLAGS_OR)
			sep = "?";
		else
			sep = ",";
	}
	list_iterator_destroy(depend_iter);
}

/* Print a job's dependency information based upon job_ptr->depend_list */
extern void print_job_dependency(job_record_t *job_ptr, const char *func)
{
	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->depend_list == NULL)) {
		info("%s: %pJ has no dependency.", func, job_ptr);
		return;
	}
	_depend_list2str(job_ptr, false);
	info("%s: Dependency information for %pJ:\n  %s",
	     func, job_ptr, job_ptr->details->dependency);
}

static int _test_job_dependency_common(
	bool is_complete, bool is_completed, bool is_pending,
	bool *clear_dep, bool *failure,
	job_record_t *job_ptr, struct depend_spec *dep_ptr)
{
	int rc = 0;
	job_record_t *djob_ptr = dep_ptr->job_ptr;
	time_t now = time(NULL);

	xassert(clear_dep);
	xassert(failure);

	if (dep_ptr->depend_type == SLURM_DEPEND_AFTER) {
		if (!is_pending) {
			if (!dep_ptr->depend_time ||
			    (djob_ptr->start_time &&
			     ((now - djob_ptr->start_time) >=
			      dep_ptr->depend_time)) ||
			    fed_mgr_job_started_on_sib(djob_ptr)) {
				*clear_dep = true;
			} /* else still depends */
		} /* else still depends */
		rc = 1;
	} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_ANY) {
		if (is_completed)
			*clear_dep = true;
		/* else still depends */
		rc = 1;
	} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_NOT_OK) {
		if (djob_ptr->job_state & JOB_SPECIAL_EXIT)
			*clear_dep = true;
		else if (!is_completed) { /* Still depends */
		} else if (!is_complete)
			*clear_dep = true;
		else
			*failure = true;
		rc = 1;
	} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_OK) {
		if (!is_completed) { /* Still depends */
		} else if (is_complete)
			*clear_dep = true;
		else
			*failure = true;
		rc = 1;
	} else if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_CORRESPOND) {
		job_record_t *dcjob_ptr = NULL;
		if ((job_ptr->array_task_id == NO_VAL) ||
		    (job_ptr->array_task_id == INFINITE))
			dcjob_ptr = NULL;
		else
			dcjob_ptr = find_job_array_rec(dep_ptr->job_id,
						       job_ptr->array_task_id);

		if (dcjob_ptr) {
			if (!IS_JOB_COMPLETED(dcjob_ptr)) { /* Still depends */
			} else if (IS_JOB_COMPLETE(dcjob_ptr))
				*clear_dep = true;
			else
				*failure = true;
		} else {
			if (!is_completed) { /* Still depends */
			} else if (is_complete)
				*clear_dep = true;
			else if (job_ptr->array_recs &&
				 (job_ptr->array_task_id == NO_VAL)) {
				/* Still depends */
			} else
				*failure = true;
		}
		rc = 1;
	} else if (dep_ptr->depend_type == SLURM_DEPEND_BURST_BUFFER) {
		if (is_completed &&
		    (bb_g_job_test_stage_out(djob_ptr) == 1))
			*clear_dep = true;
		/* else still depends */
		rc = 1;
	} else if (dep_ptr->depend_type == SLURM_DEPEND_EXPAND) {
		time_t now = time(NULL);
		if (is_pending) { /* Still depends */
		} else if (is_completed)
			*failure = true;
		else if ((djob_ptr->end_time != 0) &&
			 (djob_ptr->end_time > now)) {
			job_ptr->time_limit = djob_ptr->end_time - now;
			job_ptr->time_limit /= 60;  /* sec to min */
			*clear_dep = true;
		}
		if (!*failure && job_ptr->details && djob_ptr->details) {
			job_ptr->details->share_res =
				djob_ptr->details->share_res;
			job_ptr->details->whole_node =
				djob_ptr->details->whole_node;
		}
		rc = 1;
	}

	return rc;
}

static void _test_dependency_state(depend_spec_t *dep_ptr, bool *or_satisfied,
				   bool *and_failed, bool *or_flag,
				   bool *has_unfulfilled)
{
	xassert(or_satisfied);
	xassert(and_failed);
	xassert(or_flag);
	xassert(has_unfulfilled);

	*or_flag = (dep_ptr->depend_flags & SLURM_FLAGS_OR) ? true : false;

	if (*or_flag) {
		if (dep_ptr->depend_state == DEPEND_FULFILLED)
			*or_satisfied = true;
		else if (dep_ptr->depend_state == DEPEND_NOT_FULFILLED)
			*has_unfulfilled = true;
	} else { /* AND'd dependencies */
		if (dep_ptr->depend_state == DEPEND_FAILED)
			*and_failed = true;
		else if (dep_ptr->depend_state == DEPEND_NOT_FULFILLED)
			*has_unfulfilled = true;
	}
}

/*
 * Determine if a job's dependencies are met
 * Inputs: job_ptr
 * Outputs: was_changed (optional) -
 *          If it exists, set it to true if at least 1 dependency changed
 *          state, otherwise false.
 * RET: NO_DEPEND = no dependencies
 *      LOCAL_DEPEND = local dependencies remain
 *      FAIL_DEPEND = failure (job completion code not per dependency),
 *                    delete the job
 *      REMOTE_DEPEND = only remote dependencies remain
 */
extern int test_job_dependency(job_record_t *job_ptr, bool *was_changed)
{
	ListIterator depend_iter;
	depend_spec_t *dep_ptr;
	bool has_local_depend = false;
	int results = NO_DEPEND;
	job_record_t  *djob_ptr;
	bool is_complete, is_completed, is_pending;
	bool or_satisfied = false, and_failed = false, or_flag = false,
	     has_unfulfilled = false, changed = false;

	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->depend_list == NULL) ||
	    (list_count(job_ptr->details->depend_list) == 0)) {
		job_ptr->bit_flags &= ~JOB_DEPENDENT;
		if (was_changed)
			*was_changed = changed;
		return NO_DEPEND;
	}

	depend_iter = list_iterator_create(job_ptr->details->depend_list);
	while ((dep_ptr = list_next(depend_iter))) {
		bool clear_dep = false, failure = false;
		bool remote;

		remote = (dep_ptr->depend_flags & SLURM_FLAGS_REMOTE) ?
			true : false;
		/*
		 * If the job id is for a cluster that's not in the federation
		 * (it's likely the cluster left the federation), then set
		 * this dependency's state to failed.
		 */
		if (remote) {
			if (fed_mgr_is_origin_job(job_ptr) &&
			    (dep_ptr->depend_state == DEPEND_NOT_FULFILLED) &&
			    (dep_ptr->depend_type != SLURM_DEPEND_SINGLETON) &&
			    (!fed_mgr_is_job_id_in_fed(dep_ptr->job_id))) {
				log_flag(DEPENDENCY, "%s: %pJ dependency %s:%u failed due to job_id not in federation.",
					 __func__, job_ptr,
					 _depend_type2str(dep_ptr),
					 dep_ptr->job_id);
				changed = true;
				dep_ptr->depend_state = DEPEND_FAILED;
			}
		}
		if ((dep_ptr->depend_state != DEPEND_NOT_FULFILLED) || remote) {
			_test_dependency_state(dep_ptr, &or_satisfied,
					       &and_failed, &or_flag,
					       &has_unfulfilled);
			continue;
		}

		/* Test local, unfulfilled dependency: */
		has_local_depend = true;
		dep_ptr->job_ptr = find_job_array_rec(dep_ptr->job_id,
						      dep_ptr->array_task_id);
		djob_ptr = dep_ptr->job_ptr;
		if ((dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) &&
		    job_ptr->name) {
			if (list_find_first(job_list, _find_singleton_job,
					    job_ptr) ||
			    !fed_mgr_is_singleton_satisfied(job_ptr,
							    dep_ptr, true)) {
				/* Still depends */
			} else
				clear_dep = true;
		} else if ((djob_ptr == NULL) ||
			   (djob_ptr->magic != JOB_MAGIC) ||
			   ((djob_ptr->job_id != dep_ptr->job_id) &&
			    (djob_ptr->array_job_id != dep_ptr->job_id))) {
			/* job is gone, dependency lifted */
			clear_dep = true;
		} else {
			/* Special case, apply test to job array as a whole */
			if (dep_ptr->array_task_id == INFINITE) {
				is_complete = test_job_array_complete(
					dep_ptr->job_id);
				is_completed = test_job_array_completed(
					dep_ptr->job_id);
				is_pending = test_job_array_pending(
					dep_ptr->job_id);
			} else {
				/* Normal job */
				is_complete = IS_JOB_COMPLETE(djob_ptr);
				is_completed = IS_JOB_COMPLETED(djob_ptr);
				is_pending = IS_JOB_PENDING(djob_ptr);
			}

			if (!_test_job_dependency_common(
				    is_complete, is_completed, is_pending,
				    &clear_dep, &failure,
				    job_ptr, dep_ptr))
				failure = true;
		}

		if (failure) {
			dep_ptr->depend_state = DEPEND_FAILED;
			changed = true;
			log_flag(DEPENDENCY, "%s: %pJ dependency %s:%u failed.",
				 __func__, job_ptr, _depend_type2str(dep_ptr),
				 dep_ptr->job_id);
		} else if (clear_dep) {
			dep_ptr->depend_state = DEPEND_FULFILLED;
			changed = true;
			log_flag(DEPENDENCY, "%s: %pJ dependency %s:%u fulfilled.",
				 __func__, job_ptr, _depend_type2str(dep_ptr),
				 dep_ptr->job_id);
		}

		_test_dependency_state(dep_ptr, &or_satisfied, &and_failed,
				       &or_flag, &has_unfulfilled);
	}
	list_iterator_destroy(depend_iter);

	if (or_satisfied && (job_ptr->state_reason == WAIT_DEP_INVALID)) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
		last_job_update = time(NULL);
	}

	if (or_satisfied || (!or_flag && !and_failed && !has_unfulfilled)) {
		/* Dependency fulfilled */
		fed_mgr_remove_remote_dependencies(job_ptr);
		job_ptr->bit_flags &= ~JOB_DEPENDENT;
		/*
		 * Don't flush the list if this job isn't on the origin - that
		 * means that we were called from
		 * fed_mgr_test_remote_dependencies() and need to send back the
		 * dependency list to the origin.
		 */
		if (fed_mgr_is_origin_job(job_ptr))
			list_flush(job_ptr->details->depend_list);
		_depend_list2str(job_ptr, false);
		results = NO_DEPEND;
		log_flag(DEPENDENCY, "%s: %pJ dependency fulfilled",
			 __func__, job_ptr);
	} else {
		if (changed) {
			_depend_list2str(job_ptr, false);
			if (slurm_conf.debug_flags & DEBUG_FLAG_DEPENDENCY)
				print_job_dependency(job_ptr, __func__);
		}
		job_ptr->bit_flags |= JOB_DEPENDENT;
		acct_policy_remove_accrue_time(job_ptr, false);
		if (and_failed || (or_flag && !has_unfulfilled))
			/* Dependency failed */
			results = FAIL_DEPEND;
		else
			/* Still dependent */
			results = has_local_depend ? LOCAL_DEPEND :
				REMOTE_DEPEND;
	}

	if (was_changed)
		*was_changed = changed;
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
		max_array_size = slurm_conf.max_array_sz;
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

/* Copy dependent job's TRES options into another job's options  */
static void _copy_tres_opts(job_record_t *job_ptr, job_record_t *dep_job_ptr)
{
	xfree(job_ptr->cpus_per_tres);
	job_ptr->cpus_per_tres = xstrdup(dep_job_ptr->cpus_per_tres);
	xfree(job_ptr->tres_per_job);
	job_ptr->tres_per_job = xstrdup(dep_job_ptr->tres_per_job);
	xfree(job_ptr->tres_per_node);
	job_ptr->tres_per_node = xstrdup(dep_job_ptr->tres_per_node);
	xfree(job_ptr->tres_per_socket);
	job_ptr->tres_per_socket = xstrdup(dep_job_ptr->tres_per_socket);
	xfree(job_ptr->tres_per_task);
	job_ptr->tres_per_task = xstrdup(dep_job_ptr->tres_per_task);
	xfree(job_ptr->mem_per_tres);
	job_ptr->mem_per_tres = xstrdup(dep_job_ptr->mem_per_tres);
}

static int _find_dependency(void *arg, void *key)
{
	/* Does arg (dependency in the list) match key (new dependency)? */
	depend_spec_t *dep_ptr = (depend_spec_t *)arg;
	depend_spec_t *new_dep = (depend_spec_t *)key;
	return (dep_ptr->job_id == new_dep->job_id) &&
		(dep_ptr->array_task_id == new_dep->array_task_id) &&
		(dep_ptr->depend_type == new_dep->depend_type);
}

extern depend_spec_t *find_dependency(job_record_t *job_ptr,
				      depend_spec_t *dep_ptr)
{
	if (!job_ptr->details || !job_ptr->details->depend_list)
		return NULL;
	return list_find_first(job_ptr->details->depend_list,
			       _find_dependency, dep_ptr);
}

/*
 * Add a new dependency to the list, ensuring that the list is unique.
 * Dependencies are uniquely identified by a combination of job_id and
 * depend_type.
 */
static void _add_dependency_to_list(List depend_list,
				    depend_spec_t *dep_ptr)
{
	if (!list_find_first(depend_list, _find_dependency, dep_ptr))
		list_append(depend_list, dep_ptr);
}

static int _parse_depend_state(char **str_ptr, uint32_t *depend_state)
{
	char *sep_ptr;

	if ((sep_ptr = strchr(*str_ptr, '('))) {
		/* Get the whole string before ")", convert to state */
		char *paren = strchr(*str_ptr, ')');
		if (!paren)
			return SLURM_ERROR;
		else
			*paren = '\0';
		sep_ptr++; /* skip over "(" */
		*depend_state = _depend_state_str2state(sep_ptr);
		/* Don't allow depend_fulfilled as a string. */
		if (*depend_state != DEPEND_FAILED)
			*depend_state = DEPEND_NOT_FULFILLED;
		*str_ptr = paren + 1; /* skip over ")" */
	} else
		*depend_state = DEPEND_NOT_FULFILLED;

	return SLURM_SUCCESS;
}

static job_record_t *_find_dependent_job_ptr(uint32_t job_id,
					     uint32_t *array_task_id)
{
	job_record_t *dep_job_ptr;

	if (*array_task_id == NO_VAL) {
		dep_job_ptr = find_job_record(job_id);
		if (!dep_job_ptr)
			dep_job_ptr = find_job_array_rec(job_id, INFINITE);
		if (dep_job_ptr &&
		    (dep_job_ptr->array_job_id == job_id) &&
		    ((dep_job_ptr->array_task_id != NO_VAL) ||
		     (dep_job_ptr->array_recs != NULL)))
			*array_task_id = INFINITE;
	} else
		dep_job_ptr = find_job_array_rec(job_id, *array_task_id);

	return dep_job_ptr;
}

/*
 * job_ptr - job that is getting a new dependency
 * dep_job_ptr - pointer to the job that job_ptr wants to depend on
 *   - This can be NULL, for example if it's a remote dependency. That's okay.
 * job_id - job_id of the dependency string
 * array_task_id - array_task_id of the dependency string
 *   - Equals NO_VAL if the dependency isn't a job array.
 *   - Equals INFINITE if the dependency is the whole job array.
 *   - Otherwise this equals a specific task of the job array (0, 1, 2, etc.)
 *
 * RET true if job_ptr is the same job as the new dependency, false otherwise.
 *
 * Example:
 *   scontrol update jobid=123 dependency=afterok:456_5
 *
 * job_ptr points to the job record for jobid=123.
 * dep_job_ptr points to the job record for 456_5.
 * job_id == 456. (This is probably different from dep_job_ptr->job_id.)
 * array_task_id == 5.
 */
static bool _depends_on_same_job(job_record_t *job_ptr,
				 job_record_t *dep_job_ptr,
				 uint32_t job_id, uint32_t array_task_id)
{
	if (array_task_id == INFINITE) {
		/* job_ptr wants to set a dependency on a whole job array */
		if ((job_ptr->array_task_id != NO_VAL) ||
		    (job_ptr->array_recs)) {
			/*
			 * job_ptr is a specific task in a job array, or is
			 * the meta job of a job array.
			 * Test if job_ptr belongs to the array indicated by
			 * the dependency string's "job_id"
			 */
			return (job_ptr->array_job_id == job_id);
		} else {
			/* job_ptr is a normal job */
			return (job_ptr == dep_job_ptr);
		}
	} else {
		/* Doesn't depend on a whole job array; test normally */
		return (job_ptr == dep_job_ptr);
	}
}

/*
 * The new dependency format is:
 *
 * <type:job_id[:job_id][,type:job_id[:job_id]]> or
 * <type:job_id[:job_id][?type:job_id[:job_id]]>
 *
 * This function parses the all job id's within a single dependency type.
 * One char past the end of valid job id's is returned in (*sep_ptr2).
 * Set (*rc) to ESLURM_DEPENDENCY for invalid job id's.
 */
static void _parse_dependency_jobid_new(job_record_t *job_ptr,
					List new_depend_list, char *sep_ptr,
					char **sep_ptr2, char *tok,
					uint16_t depend_type, int select_hetero,
					int *rc)
{
	depend_spec_t *dep_ptr;
	job_record_t *dep_job_ptr = NULL;
	int expand_cnt = 0;
	uint32_t job_id, array_task_id, depend_state;
	char *tmp = NULL;
	int depend_time = 0;

	while (!(*rc)) {
		job_id = strtol(sep_ptr, &tmp, 10);
		if ((tmp != NULL) && (tmp[0] == '_')) {
			if (tmp[1] == '*') {
				array_task_id = INFINITE;
				tmp += 2;	/* Past "_*" */
			} else {
				array_task_id = strtol(tmp+1,
						       &tmp, 10);
			}
		} else
			array_task_id = NO_VAL;
		if ((tmp == NULL) || (job_id == 0) ||
		    ((tmp[0] != '\0') && (tmp[0] != ',') &&
		     (tmp[0] != '?')  && (tmp[0] != ':') &&
		     (tmp[0] != '+') && (tmp[0] != '('))) {
			*rc = ESLURM_DEPENDENCY;
			break;
		}
		/*
		 * _find_dependent_job_ptr() may modify array_task_id, so check
		 * if the job is the same after that.
		 */
		dep_job_ptr = _find_dependent_job_ptr(job_id, &array_task_id);
		if (_depends_on_same_job(job_ptr, dep_job_ptr, job_id,
					 array_task_id)) {
			*rc = ESLURM_DEPENDENCY;
			break;
		}
		if ((depend_type == SLURM_DEPEND_EXPAND) &&
		    ((expand_cnt++ > 0) || (dep_job_ptr == NULL) ||
		     (!IS_JOB_RUNNING(dep_job_ptr))		||
		     (dep_job_ptr->qos_id != job_ptr->qos_id)	||
		     (dep_job_ptr->part_ptr == NULL)		||
		     (job_ptr->part_ptr     == NULL)		||
		     (dep_job_ptr->part_ptr != job_ptr->part_ptr))) {
			/*
			 * Expand only jobs in the same QOS and
			 * and partition
			 */
			*rc = ESLURM_DEPENDENCY;
			break;
		}

		if (tmp[0] == '+') {
			sep_ptr = &tmp[1]; /* skip over "+" */
			depend_time = strtol(sep_ptr, &tmp, 10);

			if (depend_time <= 0) {
				*rc = ESLURM_DEPENDENCY;
				break;
			}
			depend_time *= 60;
		}

		if (_parse_depend_state(&tmp, &depend_state)) {
			*rc = ESLURM_DEPENDENCY;
			break;
		}

		if (depend_type == SLURM_DEPEND_EXPAND) {
			assoc_mgr_lock_t locks = { .tres = READ_LOCK };
			uint16_t sockets_per_node = NO_VAL16;
			multi_core_data_t *mc_ptr;

			if ((mc_ptr = job_ptr->details->mc_ptr)) {
				sockets_per_node =
					mc_ptr->sockets_per_node;
			}
			job_ptr->details->expanding_jobid = job_id;
			if (select_hetero == 0) {
				/*
				 * GRES per node of this job must match
				 * the job being expanded. Other options
				 * are ignored.
				 */
				_copy_tres_opts(job_ptr, dep_job_ptr);
			}
			FREE_NULL_LIST(job_ptr->gres_list_req);
			(void) gres_job_state_validate(
				job_ptr->cpus_per_tres,
				job_ptr->tres_freq,
				job_ptr->tres_per_job,
				job_ptr->tres_per_node,
				job_ptr->tres_per_socket,
				job_ptr->tres_per_task,
				job_ptr->mem_per_tres,
				&job_ptr->details->num_tasks,
				&job_ptr->details->min_nodes,
				&job_ptr->details->max_nodes,
				&job_ptr->details->
				ntasks_per_node,
				&job_ptr->details->mc_ptr->
				ntasks_per_socket,
				&sockets_per_node,
				&job_ptr->details->
				orig_cpus_per_task,
				&job_ptr->details->ntasks_per_tres,
				&job_ptr->gres_list_req);
			if (mc_ptr && (sockets_per_node != NO_VAL16)) {
				mc_ptr->sockets_per_node =
					sockets_per_node;
			}
			assoc_mgr_lock(&locks);
			gres_ctld_set_job_tres_cnt(job_ptr->gres_list_req,
						   job_ptr->details->min_nodes,
						   job_ptr->tres_req_cnt,
						   true);
			xfree(job_ptr->tres_req_str);
			job_ptr->tres_req_str =
				assoc_mgr_make_tres_str_from_array(
					job_ptr->tres_req_cnt,
					TRES_STR_FLAG_SIMPLE, true);
			assoc_mgr_unlock(&locks);
		}

		dep_ptr = xmalloc(sizeof(depend_spec_t));
		dep_ptr->array_task_id = array_task_id;
		dep_ptr->depend_type = depend_type;
		if (job_ptr->fed_details && !fed_mgr_is_origin_job_id(job_id)) {
			if (depend_type == SLURM_DEPEND_EXPAND) {
				error("%s: Job expansion not permitted for remote jobs",
				      __func__);
				*rc = ESLURM_DEPENDENCY;
				xfree(dep_ptr);
				break;
			}
			/* The dependency is on a remote cluster */
			dep_ptr->depend_flags |= SLURM_FLAGS_REMOTE;
			dep_job_ptr = NULL;
		}
		if (dep_job_ptr) {	/* job still active */
			if (array_task_id == NO_VAL)
				dep_ptr->job_id = dep_job_ptr->job_id;
			else
				dep_ptr->job_id = dep_job_ptr->array_job_id;
		} else
			dep_ptr->job_id = job_id;
		dep_ptr->job_ptr = dep_job_ptr;
		dep_ptr->depend_time = depend_time;
		dep_ptr->depend_state = depend_state;
		_add_dependency_to_list(new_depend_list, dep_ptr);
		if (tmp[0] != ':')
			break;
		sep_ptr = tmp + 1;	/* skip over ":" */

	}
	*sep_ptr2 = tmp;
}

/*
 * The old dependency format is a comma-separated list of job id's.
 * Parse a single jobid.
 * One char past the end of a valid job id will be returned in (*sep_ptr).
 * For an invalid job id, (*rc) will be set to ESLURM_DEPENDENCY.
 */
static void _parse_dependency_jobid_old(job_record_t *job_ptr,
					List new_depend_list, char **sep_ptr,
					char *tok, int *rc)
{
	depend_spec_t *dep_ptr;
	job_record_t *dep_job_ptr = NULL;
	uint32_t job_id, array_task_id;
	char *tmp = NULL;

	job_id = strtol(tok, &tmp, 10);
	if ((tmp != NULL) && (tmp[0] == '_')) {
		if (tmp[1] == '*') {
			array_task_id = INFINITE;
			tmp += 2;	/* Past "_*" */
		} else {
			array_task_id = strtol(tmp+1, &tmp, 10);
		}
	} else {
		array_task_id = NO_VAL;
	}
	*sep_ptr = tmp;
	if ((tmp == NULL) || (job_id == 0) ||
	    ((tmp[0] != '\0') && (tmp[0] != ','))) {
		*rc = ESLURM_DEPENDENCY;
		return;
	}
	/*
	 * _find_dependent_job_ptr() may modify array_task_id, so check
	 * if the job is the same after that.
	 */
	dep_job_ptr = _find_dependent_job_ptr(job_id, &array_task_id);
	if (_depends_on_same_job(job_ptr, dep_job_ptr, job_id, array_task_id)) {
		*rc = ESLURM_DEPENDENCY;
		return;
	}

	dep_ptr = xmalloc(sizeof(depend_spec_t));
	dep_ptr->array_task_id = array_task_id;
	dep_ptr->depend_type = SLURM_DEPEND_AFTER_ANY;
	if (job_ptr->fed_details &&
	    !fed_mgr_is_origin_job_id(job_id)) {
		/* The dependency is on a remote cluster */
		dep_ptr->depend_flags |= SLURM_FLAGS_REMOTE;
		dep_job_ptr = NULL;
	}
	if (dep_job_ptr) {
		if (array_task_id == NO_VAL) {
			dep_ptr->job_id = dep_job_ptr->job_id;
		} else {
			dep_ptr->job_id = dep_job_ptr->array_job_id;
		}
	} else
		dep_ptr->job_id = job_id;
	dep_ptr->job_ptr = dep_job_ptr; /* Can be NULL */
	_add_dependency_to_list(new_depend_list, dep_ptr);
}

extern bool update_job_dependency_list(job_record_t *job_ptr,
				       List new_depend_list)
{
	depend_spec_t *dep_ptr, *job_depend;
	ListIterator itr;
	List job_depend_list;
	bool was_changed = false;

	xassert(job_ptr);
	xassert(job_ptr->details);
	xassert(job_ptr->details->depend_list);

	job_depend_list = job_ptr->details->depend_list;

	itr = list_iterator_create(new_depend_list);
	while ((dep_ptr = list_next(itr))) {
		/*
		 * If the dependency is marked as remote, then it wasn't updated
		 * by the sibling cluster. Skip it.
		 */
		if (dep_ptr->depend_flags & SLURM_FLAGS_REMOTE) {
			continue;
		}
		/*
		 * Find the dependency in job_ptr that matches this one.
		 * Then update job_ptr's dependency state (not fulfilled,
		 * fulfilled, or failed) to match this one.
		 */
		job_depend = list_find_first(job_depend_list, _find_dependency,
					     dep_ptr);
		if (!job_depend) {
			/*
			 * This can happen if the job's dependency is updated
			 * and the update doesn't get to the sibling before
			 * the sibling sends back an update to the origin (us).
			 */
			log_flag(DEPENDENCY, "%s: Cannot find dependency %s:%u for %pJ, it may have been cleared before we got here.",
				 __func__, _depend_type2str(dep_ptr),
				 dep_ptr->job_id, job_ptr);
			continue;
		}

		/*
		 * If the dependency is already fulfilled, don't update it.
		 * Otherwise update the dependency state.
		 */
		if ((job_depend->depend_state == DEPEND_FULFILLED) ||
		    (job_depend->depend_state == dep_ptr->depend_state))
			continue;
		if (job_depend->depend_type == SLURM_DEPEND_SINGLETON) {
			/*
			 * We need to update the singleton dependency with
			 * the cluster bit, but test_job_dependency() will test
			 * if it is fulfilled, so don't change the depend_state
			 * here.
			 */
			job_depend->singleton_bits |=
				dep_ptr->singleton_bits;
			if (!fed_mgr_is_singleton_satisfied(job_ptr, job_depend,
							    false))
			    continue;
		}
		job_depend->depend_state = dep_ptr->depend_state;
		was_changed = true;
	}
	list_iterator_destroy(itr);
	return was_changed;
}

extern int handle_job_dependency_updates(void *object, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) object;
	depend_spec_t *dep_ptr = NULL;
	ListIterator itr;
	bool or_satisfied = false, and_failed = false, or_flag = false,
	     has_unfulfilled = false;
	time_t now = time(NULL);

	xassert(job_ptr->details);
	xassert(job_ptr->details->depend_list);

	/*
	 * Check the depend_state of each dependency.
	 * All dependencies are OR'd or AND'd - we don't allow a mix.
	 * OR'd dependencies:
	 *   - If one dependency succeeded, the whole thing passes.
	 *   - If there is at least one unfulfilled dependency,
	 *     the job is still dependent.
	 *   - All dependencies failed == dependency never satisfied.
	 * AND'd dependencies:
	 *   - One failure == dependency never satisfied
	 *   - One+ not fulfilled == still dependent
	 *   - All succeeded == dependency fulfilled
	 */
	itr = list_iterator_create(job_ptr->details->depend_list);
	while ((dep_ptr = list_next(itr))) {
		_test_dependency_state(dep_ptr, &or_satisfied, &and_failed,
				       &or_flag, &has_unfulfilled);
	}
	list_iterator_destroy(itr);

	if (or_satisfied || (!or_flag && !and_failed && !has_unfulfilled)) {
		/* Dependency fulfilled */
		fed_mgr_remove_remote_dependencies(job_ptr);
		job_ptr->bit_flags &= ~JOB_DEPENDENT;
		list_flush(job_ptr->details->depend_list);
		if ((job_ptr->state_reason == WAIT_DEP_INVALID) ||
		    (job_ptr->state_reason == WAIT_DEPENDENCY)) {
			job_ptr->state_reason = WAIT_NO_REASON;
			xfree(job_ptr->state_desc);
			last_job_update = now;
		}
		_depend_list2str(job_ptr, false);
		fed_mgr_job_requeue(job_ptr);
	} else {
		_depend_list2str(job_ptr, false);
		job_ptr->bit_flags |= JOB_DEPENDENT;
		acct_policy_remove_accrue_time(job_ptr, false);
		if (and_failed || (or_flag && !has_unfulfilled)) {
			/* Dependency failed */
			handle_invalid_dependency(job_ptr);
		} else {
			/* Still dependent */
			job_ptr->state_reason = WAIT_DEPENDENCY;
			xfree(job_ptr->state_desc);
			last_job_update = now;
		}
	}
	if (slurm_conf.debug_flags & DEBUG_FLAG_DEPENDENCY)
		print_job_dependency(job_ptr, __func__);

	return SLURM_SUCCESS;
}

/*
 * Parse a job dependency string and use it to establish a "depend_spec"
 * list of dependencies. We accept both old format (a single job ID) and
 * new format (e.g. "afterok:123:124,after:128").
 * IN job_ptr - job record to have dependency and depend_list updated
 * IN new_depend - new dependency description
 * RET returns an error code from slurm_errno.h
 */
extern int update_job_dependency(job_record_t *job_ptr, char *new_depend)
{
	static int select_hetero = -1;
	int rc = SLURM_SUCCESS;
	uint16_t depend_type = 0;
	char *tok, *new_array_dep, *sep_ptr, *sep_ptr2 = NULL;
	List new_depend_list = NULL;
	depend_spec_t *dep_ptr;
	bool or_flag = false;

	if (job_ptr->details == NULL)
		return EINVAL;

	if (select_hetero == -1) {
		/*
		 * Determine if the select plugin supports heterogeneous
		 * GRES allocations (count differ by node): 1=yes, 0=no
		 */
		if ((xstrstr(slurm_conf.select_type, "cons_tres") ||
		     (xstrstr(slurm_conf.select_type, "cray_aries") &&
		      (slurm_conf.select_type_param & CR_OTHER_CONS_TRES)))) {
			select_hetero = 1;
		} else
			select_hetero = 0;
	}

	/* Clear dependencies on NULL, "0", or empty dependency input */
	job_ptr->details->expanding_jobid = 0;
	if ((new_depend == NULL) || (new_depend[0] == '\0') ||
	    ((new_depend[0] == '0') && (new_depend[1] == '\0'))) {
		xfree(job_ptr->details->dependency);
		FREE_NULL_LIST(job_ptr->details->depend_list);
		return rc;

	}

	new_depend_list = list_create(xfree_ptr);
	if ((new_array_dep = _xlate_array_dep(new_depend)))
		tok = new_array_dep;
	else
		tok = new_depend;
	/* validate new dependency string */
	while (rc == SLURM_SUCCESS) {
		/* test singleton dependency flag */
		if (xstrncasecmp(tok, "singleton", 9) == 0) {
			uint32_t state;
			tok += 9; /* skip past "singleton" */
			depend_type = SLURM_DEPEND_SINGLETON;
			if (_parse_depend_state(&tok, &state)) {
				rc = ESLURM_DEPENDENCY;
				break;
			}
			if (disable_remote_singleton &&
			    !fed_mgr_is_origin_job(job_ptr)) {
				/* Singleton disabled for non-origin cluster */
			} else {
				dep_ptr = xmalloc(sizeof(depend_spec_t));
				dep_ptr->depend_state = state;
				dep_ptr->depend_type = depend_type;
				/* dep_ptr->job_id = 0;	set by xmalloc */
				/* dep_ptr->job_ptr = NULL; set by xmalloc */
				/* dep_ptr->singleton_bits = 0;set by xmalloc */
				_add_dependency_to_list(new_depend_list,
							dep_ptr);
			}
			if (tok[0] == ',') {
				tok++;
				continue;
			} else if (tok[0] == '?') {
				tok++;
				or_flag = true;
				continue;
			}
			if (tok[0] != '\0')
				rc = ESLURM_DEPENDENCY;
			break;
		}

		/* Test for old format, just a job ID */
		sep_ptr = strchr(tok, ':');
		if ((sep_ptr == NULL) && (tok[0] >= '0') && (tok[0] <= '9')) {
			_parse_dependency_jobid_old(job_ptr, new_depend_list,
					      &sep_ptr, tok, &rc);
			if (rc)
				break;
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
		if (!xstrncasecmp(tok, "afternotok:", 11))
			depend_type = SLURM_DEPEND_AFTER_NOT_OK;
		else if (!xstrncasecmp(tok, "aftercorr:", 10))
			depend_type = SLURM_DEPEND_AFTER_CORRESPOND;
		else if (!xstrncasecmp(tok, "afterany:", 9))
			depend_type = SLURM_DEPEND_AFTER_ANY;
		else if (!xstrncasecmp(tok, "afterok:", 8))
			depend_type = SLURM_DEPEND_AFTER_OK;
		else if (!xstrncasecmp(tok, "afterburstbuffer:", 11))
			depend_type = SLURM_DEPEND_BURST_BUFFER;
		else if (!xstrncasecmp(tok, "after:", 6))
			depend_type = SLURM_DEPEND_AFTER;
		else if (!xstrncasecmp(tok, "expand:", 7)) {
			if (!permit_job_expansion()) {
				rc = ESLURM_NOT_SUPPORTED;
				break;
			}
			depend_type = SLURM_DEPEND_EXPAND;
		} else {
			rc = ESLURM_DEPENDENCY;
			break;
		}
		sep_ptr++;	/* skip over ":" */
		_parse_dependency_jobid_new(job_ptr, new_depend_list, sep_ptr,
				      &sep_ptr2, tok, depend_type,
				      select_hetero, &rc);

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
		(void) _scan_depend(NULL, job_ptr);
		if (_scan_depend(new_depend_list, job_ptr))
			rc = ESLURM_CIRCULAR_DEPENDENCY;
	}

	if (rc == SLURM_SUCCESS) {
		FREE_NULL_LIST(job_ptr->details->depend_list);
		job_ptr->details->depend_list = new_depend_list;
		_depend_list2str(job_ptr, or_flag);
		if (slurm_conf.debug_flags & DEBUG_FLAG_DEPENDENCY)
			print_job_dependency(job_ptr, __func__);
	} else {
		FREE_NULL_LIST(new_depend_list);
	}
	xfree(new_array_dep);
	return rc;
}

/* Return true if the job job_ptr is found in dependency_list.
 * Pass NULL dependency list to clear the counter.
 * Execute recursively for each dependent job */
static bool _scan_depend(List dependency_list, job_record_t *job_ptr)
{
	static int job_counter = 0;
	bool rc = false;
	ListIterator iter;
	depend_spec_t *dep_ptr;

	if (dependency_list == NULL) {
		job_counter = 0;
		return false;
	} else if (job_counter++ >= max_depend_depth) {
		return false;
	}

	xassert(job_ptr);
	iter = list_iterator_create(dependency_list);
	while (!rc && (dep_ptr = list_next(iter))) {
		if (dep_ptr->job_id == 0)	/* Singleton */
			continue;
		/*
		 * We can't test for circular dependencies if the job_ptr
		 * wasn't found - the job may not be on this cluster, or the
		 * job was already purged when the dependency submitted,
		 * or the job just didn't exist.
		 */
		if (!dep_ptr->job_ptr)
			continue;
		if ((rc = _depends_on_same_job(job_ptr, dep_ptr->job_ptr,
					       dep_ptr->job_id,
					       dep_ptr->array_task_id)))
			break;
		else if (dep_ptr->job_ptr->magic != JOB_MAGIC)
			continue;	/* purged job, ptr not yet cleared */
		else if (!IS_JOB_FINISHED(dep_ptr->job_ptr) &&
			 dep_ptr->job_ptr->details &&
			 dep_ptr->job_ptr->details->depend_list) {
			rc = _scan_depend(dep_ptr->job_ptr->details->
					  depend_list, job_ptr);
			if (rc) {
				info("circular dependency: %pJ is dependent upon %pJ",
				     dep_ptr->job_ptr, job_ptr);
			}
		}
	}
	list_iterator_destroy(iter);
	return rc;
}

/* If there are higher priority queued jobs in this job's partition, then
 * delay the job's expected initiation time as needed to run those jobs.
 * NOTE: This is only a rough estimate of the job's start time as it ignores
 * job dependencies, feature requirements, specific node requirements, etc. */
static void _delayed_job_start_time(job_record_t *job_ptr)
{
	uint32_t part_node_cnt, part_cpu_cnt, part_cpus_per_node;
	uint32_t job_size_cpus, job_size_nodes, job_time;
	uint64_t cume_space_time = 0;
	job_record_t *job_q_ptr;
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
	while ((job_q_ptr = list_next(job_iterator))) {
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
	debug2("Increasing estimated start of %pJ by %"PRIu64" secs",
	       job_ptr, cume_space_time);
	job_ptr->start_time += cume_space_time;
}

static int _part_weight_sort(void *x, void *y)
{
	part_record_t *parta = *(part_record_t **) x;
	part_record_t *partb = *(part_record_t **) y;

	if (parta->priority_tier > partb->priority_tier)
		return -1;
	if (parta->priority_tier < partb->priority_tier)
		return 1;

	return 0;
}

/*
 * Determine if a pending job will run using only the specified nodes, build
 * response message and return SLURM_SUCCESS on success. Otherwise return an
 * error code. Caller must free response message.
 */
extern int job_start_data(job_record_t *job_ptr,
			  will_run_response_msg_t **resp)
{
	part_record_t *part_ptr;
	bitstr_t *active_bitmap = NULL, *avail_bitmap = NULL;
	bitstr_t *resv_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	int i, rc = SLURM_SUCCESS;
	time_t now = time(NULL), start_res, orig_start_time = (time_t) 0;
	List preemptee_candidates = NULL, preemptee_job_list = NULL;
	bool resv_overlap = false;
	ListIterator iter = NULL;

	if (job_ptr == NULL)
		return ESLURM_INVALID_JOB_ID;

	/*
	 * NOTE: Do not use IS_JOB_PENDING since that doesn't take
	 * into account the COMPLETING FLAG which we need to since we don't want
	 * to schedule a requeued job until it is actually done completing
	 * the first time.
	 */
	if ((job_ptr->details == NULL) || (job_ptr->job_state != JOB_PENDING))
		return ESLURM_DISABLED;

	if (job_ptr->part_ptr_list) {
		list_sort(job_ptr->part_ptr_list, _part_weight_sort);
		iter = list_iterator_create(job_ptr->part_ptr_list);
		part_ptr = list_next(iter);
	} else
		part_ptr = job_ptr->part_ptr;
next_part:
	rc = SLURM_SUCCESS;
	if (part_ptr == NULL) {
		if (iter)
			list_iterator_destroy(iter);
		return ESLURM_INVALID_PARTITION_NAME;
	}

	if (job_ptr->details->req_nodes && job_ptr->details->req_nodes[0]) {
		if (node_name2bitmap(job_ptr->details->req_nodes, false,
				     &avail_bitmap) != 0) {
			if (iter)
				list_iterator_destroy(iter);
			return ESLURM_INVALID_NODE_NAME;
		}
	} else {
		/* assume all nodes available to job for testing */
		avail_bitmap = node_conf_get_active_bitmap();
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
		if (job_ptr->part_ptr_list && (part_ptr = list_next(iter)))
			goto next_part;

		if (iter)
			list_iterator_destroy(iter);
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
		resp_data->proc_cnt = job_ptr->total_cpus;
		_delayed_job_start_time(job_ptr);
		resp_data->start_time = MAX(job_ptr->start_time,
					    orig_start_time);
		resp_data->start_time = MAX(resp_data->start_time, start_res);
		job_ptr->start_time   = 0;  /* restore pending job start time */
		resp_data->node_list  = bitmap2node_name(avail_bitmap);
		resp_data->part_name  = xstrdup(part_ptr->name);

		if (preemptee_job_list) {
			ListIterator preemptee_iterator;
			uint32_t *preemptee_jid;
			job_record_t *tmp_job_ptr;
			resp_data->preemptee_job_id = list_create(xfree_ptr);
			preemptee_iterator = list_iterator_create(
				preemptee_job_list);
			while ((tmp_job_ptr = list_next(preemptee_iterator))) {
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

	if (rc && job_ptr->part_ptr_list && (part_ptr = list_next(iter)))
		goto next_part;

	if (iter)
		list_iterator_destroy(iter);

	return rc;
}

/*
 * epilog_slurmctld - execute the epilog_slurmctld for a job that has just
 *	terminated.
 * IN job_ptr - pointer to job that has been terminated
 */
extern void epilog_slurmctld(job_record_t *job_ptr)
{
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	prep_g_epilog_slurmctld(job_ptr);
}

/*
 * Determine which nodes must be rebooted for a job
 * IN job_ptr - pointer to job that will be initiated
 * RET bitmap of nodes requiring a reboot for NodeFeaturesPlugin or NULL if none
 */
extern bitstr_t *node_features_reboot(job_record_t *job_ptr)
{
	bitstr_t *active_bitmap = NULL, *boot_node_bitmap = NULL;
	bitstr_t *feature_node_bitmap, *tmp_bitmap;
	char *reboot_features;

	if ((node_features_g_count() == 0) ||
	    !node_features_g_user_update(job_ptr->user_id))
		return NULL;

	/*
	 * Check if all features supported with AND/OR combinations
	 */
	build_active_feature_bitmap(job_ptr, job_ptr->node_bitmap,
				    &active_bitmap);
	if (active_bitmap == NULL)	/* All nodes have desired features */
		return NULL;
	bit_free(active_bitmap);

	/*
	 * If some XOR/XAND option, filter out only first set of features
	 * for NodeFeaturesPlugin
	 */
	feature_node_bitmap = node_features_g_get_node_bitmap();
	if (feature_node_bitmap == NULL) /* No nodes under NodeFeaturesPlugin */
		return NULL;

	reboot_features = node_features_g_job_xlate(
		job_ptr->details->features_use);
	tmp_bitmap = build_active_feature_bitmap2(reboot_features);
	xfree(reboot_features);
	boot_node_bitmap = bit_copy(job_ptr->node_bitmap);
	bit_and(boot_node_bitmap, feature_node_bitmap);
	bit_free(feature_node_bitmap);
	if (tmp_bitmap) {
		bit_and_not(boot_node_bitmap, tmp_bitmap);
		bit_free(tmp_bitmap);
	}
	if (bit_ffs(boot_node_bitmap) == -1)
		FREE_NULL_BITMAP(boot_node_bitmap);

	return boot_node_bitmap;
}

/*
 * reboot_job_nodes - Reboot the compute nodes allocated to a job.
 * Also change the modes of KNL nodes for node_features/knl_generic plugin.
 * IN job_ptr - pointer to job that will be initiated
 * RET SLURM_SUCCESS(0) or error code
 */
#ifdef HAVE_FRONT_END
extern void reboot_job_nodes(job_record_t *job_ptr)
{
	return;
}
#else
static void _send_reboot_msg(bitstr_t *node_bitmap, char *features,
			     uint16_t protocol_version)
{
	agent_arg_t *reboot_agent_args = NULL;
	reboot_msg_t *reboot_msg;
	hostlist_t hostlist;

	reboot_agent_args = xmalloc(sizeof(agent_arg_t));
	reboot_agent_args->msg_type = REQUEST_REBOOT_NODES;
	reboot_agent_args->retry = 0;
	reboot_agent_args->node_count = 0;
	reboot_agent_args->protocol_version = protocol_version;

	if ((hostlist = bitmap2hostlist(node_bitmap))) {
		reboot_agent_args->hostlist = hostlist;
		reboot_agent_args->node_count = hostlist_count(hostlist);
	}

	reboot_msg = xmalloc(sizeof(reboot_msg_t));
	slurm_init_reboot_msg(reboot_msg, false);
	reboot_agent_args->msg_args = reboot_msg;
	reboot_msg->features = xstrdup(features);

	set_agent_arg_r_uid(reboot_agent_args, SLURM_AUTH_UID_ANY);
	agent_queue_request(reboot_agent_args);
}

static void _do_reboot(bool power_save_on, bitstr_t *node_bitmap,
		       job_record_t *job_ptr, char *reboot_features,
		       uint16_t protocol_version)
{
	if (power_save_on)
		power_job_reboot(node_bitmap, job_ptr, reboot_features);
	else
		_send_reboot_msg(node_bitmap, reboot_features,
				 protocol_version);
	if (get_log_level() >= LOG_LEVEL_DEBUG) {
		char *nodes = bitmap2node_name(node_bitmap);
		if (nodes) {
			debug("%s: reboot nodes %s features %s",
			      __func__, nodes,
			      reboot_features ? "reboot_features" : "N/A");
		} else {
			error("%s: bitmap2nodename", __func__);
		}
		xfree(nodes);
	}
}

static void _set_reboot_features_active(bitstr_t *node_bitmap,
					char *reboot_features)
{
	int i, i_first, i_last;
	node_record_t *node_ptr;

	i_first = bit_ffs(node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(node_bitmap);
	else
		i_last = i_first - 1;

	for (i = i_first; i <= i_last; i++) {
		char *tmp_feature, *orig_features_act;

		if (!bit_test(node_bitmap, i))
			continue;
		if (!(node_ptr = node_record_table_ptr[i]))
			continue;
		/* Point to node features, don't copy */
		orig_features_act =
			node_ptr->features_act ?
			node_ptr->features_act : node_ptr->features;
		tmp_feature = node_features_g_node_xlate(reboot_features,
							 orig_features_act,
							 node_ptr->features, i);
		xfree(node_ptr->features_act);
		node_ptr->features_act = tmp_feature;
		(void) update_node_active_features(node_ptr->name,
						   node_ptr->features_act,
						   FEATURE_MODE_IND);
	}
}

extern void reboot_job_nodes(job_record_t *job_ptr)
{
	int i, i_first, i_last;
	node_record_t *node_ptr;
	time_t now = time(NULL);
	bitstr_t *boot_node_bitmap = NULL, *feature_node_bitmap = NULL;
	char *reboot_features = NULL;
	uint16_t protocol_version = SLURM_PROTOCOL_VERSION;
	static bool power_save_on = false;
	static time_t sched_update = 0;

	if (sched_update != slurm_conf.last_update) {
		power_save_on = power_save_test();
		sched_update = slurm_conf.last_update;
	}

	if ((job_ptr->details == NULL) || (job_ptr->node_bitmap == NULL))
		return;
	if (!power_save_on &&
	    ((slurm_conf.reboot_program == NULL) ||
	     (slurm_conf.reboot_program[0] == '\0')))
		return;

	if (job_ptr->reboot)
		boot_node_bitmap = bit_copy(job_ptr->node_bitmap);
	else
		boot_node_bitmap = node_features_reboot(job_ptr);
	if (boot_node_bitmap == NULL) {
		/* launch_job() when all nodes have booted */
		if (bit_overlap_any(power_node_bitmap, job_ptr->node_bitmap) ||
		    bit_overlap_any(booting_node_bitmap,
				    job_ptr->node_bitmap)) {
			job_ptr->job_state |= JOB_CONFIGURING;
			/* Reset job start time when nodes are booted */
			job_ptr->job_state |= JOB_POWER_UP_NODE;
			job_ptr->wait_all_nodes = 1;
		}
		return;
	}

	job_ptr->job_state |= JOB_CONFIGURING;
	/* Reset job start time when nodes are booted */
	job_ptr->job_state |= JOB_POWER_UP_NODE;
	/* launch_job() when all nodes have booted */
	job_ptr->wait_all_nodes = 1;

	/* Modify state information for all nodes, KNL and others */
	i_first = bit_ffs(boot_node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(boot_node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(boot_node_bitmap, i))
			continue;
		if (!(node_ptr = node_record_table_ptr[i]))
			continue;
		if (protocol_version > node_ptr->protocol_version)
			protocol_version = node_ptr->protocol_version;

		if (IS_NODE_POWERED_DOWN(node_ptr)) {
			node_ptr->node_state &= (~NODE_STATE_POWERED_DOWN);
			clusteracct_storage_g_node_up(acct_db_conn, node_ptr,
						      now);
		}
		node_ptr->node_state |= NODE_STATE_NO_RESPOND;
		node_ptr->node_state |= NODE_STATE_POWERING_UP;
		bit_clear(avail_node_bitmap, i);
		bit_clear(power_node_bitmap, i);
		bit_set(booting_node_bitmap, i);
		node_ptr->boot_req_time = now;
	}

	if (job_ptr->details->features_use &&
	    node_features_g_user_update(job_ptr->user_id)) {
		reboot_features = node_features_g_job_xlate(
			job_ptr->details->features_use);
		if (reboot_features)
			feature_node_bitmap = node_features_g_get_node_bitmap();
		if (feature_node_bitmap)
			bit_and(feature_node_bitmap, boot_node_bitmap);
		if (!feature_node_bitmap ||
		    (bit_ffs(feature_node_bitmap) == -1)) {
			/* No KNL nodes to reboot */
			FREE_NULL_BITMAP(feature_node_bitmap);
		} else {
			bit_and_not(boot_node_bitmap, feature_node_bitmap);
			if (bit_ffs(boot_node_bitmap) == -1) {
				/* No non-KNL nodes to reboot */
				FREE_NULL_BITMAP(boot_node_bitmap);
			}
		}
	}

	if (feature_node_bitmap) {
		/* Reboot nodes to change KNL NUMA and/or MCDRAM mode */
		_do_reboot(power_save_on, feature_node_bitmap, job_ptr,
			   reboot_features, protocol_version);

		/*
		 * Update node features now to avoid a race where a
		 * second job may request that this node gets rebooted
		 * (in order to get a new active feature) *after* the
		 * first reboot request but *before* slurmd actually
		 * starts up. If that would happen then the second job
		 * would stay configuring forever, waiting for the node
		 * to reboot even though the node already rebooted.
		 *
		 * By setting the node's active features right now, any
		 * other job that wants that active feature can be
		 * scheduled onto this node, which will also already be
		 * rebooting, so those other jobs won't send additional
		 * reboot requests to change the feature.
		 */
		_set_reboot_features_active(feature_node_bitmap,
					    reboot_features);
	}

	if (boot_node_bitmap) {
		/* Reboot nodes with no feature changes */
		_do_reboot(power_save_on, boot_node_bitmap, job_ptr, NULL,
			   protocol_version);
	}

	xfree(reboot_features);
	FREE_NULL_BITMAP(boot_node_bitmap);
	FREE_NULL_BITMAP(feature_node_bitmap);
}
#endif

/*
 * Deferring this setup ensures that all calling paths into select_nodes()
 * have had a chance to update all appropriate job records.
 * This works since select_nodes() will always be holding the job_write lock,
 * and thus this new thread will be blocked waiting to acquire job_write
 * until that has completed.
 * For HetJobs in particular, this is critical to ensure that all components
 * have been setup properly before prolog_slurmctld actually runs.
 */
static void *_start_prolog_slurmctld_thread(void *x)
{
	slurmctld_lock_t node_write_lock = {
		.conf = READ_LOCK, .job = WRITE_LOCK,
		.node = WRITE_LOCK, .fed = READ_LOCK };
	uint32_t *job_id = (uint32_t *) x;
	job_record_t *job_ptr;

	lock_slurmctld(node_write_lock);
	if (!(job_ptr = find_job_record(*job_id))) {
		error("%s: missing JobId=%u", __func__, *job_id);
		unlock_slurmctld(node_write_lock);
		return NULL;
	}
	prep_g_prolog_slurmctld(job_ptr);

	/*
	 * No async prolog_slurmctld threads running, so decrement now to move
	 * on with the job launch.
	 */
	if (!job_ptr->prep_prolog_cnt) {
		debug2("%s: no async prolog_slurmctld running", __func__);
		prolog_running_decr(job_ptr);
	}

	unlock_slurmctld(node_write_lock);
	xfree(job_id);

	return NULL;
}

/*
 * prolog_slurmctld - execute the prolog_slurmctld for a job that has just
 *	been allocated resources.
 * IN job_ptr - pointer to job that will be initiated
 */
extern void prolog_slurmctld(job_record_t *job_ptr)
{
	uint32_t *job_id;
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));

	if (!prep_g_required(PREP_PROLOG_SLURMCTLD))
		return;
	job_ptr->details->prolog_running++;
	job_ptr->job_state |= JOB_CONFIGURING;

	job_id = xmalloc(sizeof(*job_id));
	*job_id = job_ptr->job_id;
	slurm_thread_create_detached(NULL, _start_prolog_slurmctld_thread, job_id);
}

/* Decrement a job's prolog_running counter and launch the job if zero */
extern void prolog_running_decr(job_record_t *job_ptr)
{
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

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
		info("%s: Configuration for %pJ is complete",
		     __func__, job_ptr);
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

	feature_list_dest = list_create(feature_list_delete);
	iter = list_iterator_create(feature_list_src);
	while ((feat_src = list_next(iter))) {
		feat_dest = xmalloc(sizeof(job_feature_t));
		memcpy(feat_dest, feat_src, sizeof(job_feature_t));
		if (feat_src->node_bitmap_active)
			feat_dest->node_bitmap_active =
				bit_copy(feat_src->node_bitmap_active);
		if (feat_src->node_bitmap_avail)
			feat_dest->node_bitmap_avail =
				bit_copy(feat_src->node_bitmap_avail);
		feat_dest->name = xstrdup(feat_src->name);
		list_append(feature_list_dest, feat_dest);
	}
	list_iterator_destroy(iter);
	return feature_list_dest;
}

/*
 * build_feature_list - Translate a job's feature string into a feature_list
 * NOTE: This function is also used for reservations if job_id == 0
 * IN  details->features
 * OUT details->feature_list
 * RET error code
 */
extern int build_feature_list(job_record_t *job_ptr, bool prefer)
{
	struct job_details *detail_ptr = job_ptr->details;
	char *tmp_requested, *str_ptr, *feature = NULL;
	char *features;
	List *feature_list;
	int feature_err;
	int bracket = 0, count = 0, i, paren = 0, rc;
	int brack_set_count = 0;
	bool fail = false;
	job_feature_t *feat;
	bool can_reboot;

	/* no hard constraints */
	if (!detail_ptr || (!detail_ptr->features && !detail_ptr->prefer)) {
		if (job_ptr->batch_features)
			return ESLURM_BATCH_CONSTRAINT;
		return SLURM_SUCCESS;
	}

	if (prefer) {
		features = detail_ptr->prefer;
		feature_list = &detail_ptr->prefer_list;
		feature_err = ESLURM_INVALID_PREFER;
	} else {
		features = detail_ptr->features;
		feature_list = &detail_ptr->feature_list;
		feature_err = ESLURM_INVALID_FEATURE;
	}

	if (!features) /* The other constraint is non NULL. */
		return SLURM_SUCCESS;

	if (*feature_list)		/* already processed */
		return SLURM_SUCCESS;

	/* Use of commas separator is a common error. Replace them with '&' */
	while ((str_ptr = strstr(features, ",")))
		str_ptr[0] = '&';

	can_reboot = node_features_g_user_update(job_ptr->user_id);
	tmp_requested = xstrdup(features);
	*feature_list = list_create(feature_list_delete);
	for (i = 0; ; i++) {
		if (tmp_requested[i] == '*') {
			tmp_requested[i] = '\0';
			count = strtol(&tmp_requested[i+1], &str_ptr, 10);
			if ((feature == NULL) || (count <= 0) || (paren != 0)) {
				fail = true;
				break;
			}
			i = str_ptr - tmp_requested - 1;
		} else if (tmp_requested[i] == '&') {
			tmp_requested[i] = '\0';
			if (feature == NULL) {
				fail = true;
				break;
			}
			feat = xmalloc(sizeof(job_feature_t));
			feat->name = xstrdup(feature);
			feat->changeable = node_features_g_changeable_feature(
				feature);
			feat->count = count;
			feat->paren = paren;
			if (paren)
				feat->op_code = FEATURE_OP_AND;
			else if (bracket)
				feat->op_code = FEATURE_OP_XAND;
			else
				feat->op_code = FEATURE_OP_AND;
			list_append(*feature_list, feat);
			feature = NULL;
			count = 0;
		} else if (tmp_requested[i] == '|') {
			bool changeable;

			tmp_requested[i] = '\0';
			if (feature == NULL) {
				fail = true;
				break;
			}
			changeable = node_features_g_changeable_feature(
				feature);
			if (paren && changeable) {
				/*
				 * Most (but not all) of the logic to support
				 * OR within parenthesis works today except when
				 * trying to use available (not active) features
				 * srun -C "(hemi|snc2|snc4|quad)&(flat|cache)" ...
				 */
				fail = true;
				break;
			}
			feat = xmalloc(sizeof(job_feature_t));
			feat->name = xstrdup(feature);
			feat->changeable = changeable;
			feat->count = count;
			feat->paren = paren;
			if (paren)
				feat->op_code = FEATURE_OP_OR;
			else if (bracket)
				feat->op_code = FEATURE_OP_XOR;
			else
				feat->op_code = FEATURE_OP_OR;
			list_append(*feature_list, feat);
			feature = NULL;
			count = 0;
		} else if (tmp_requested[i] == '[') {
			tmp_requested[i] = '\0';
			if ((feature != NULL) || bracket) {
				fail = true;
				break;
			}
			bracket++;
			brack_set_count++;
			if (brack_set_count > 1)
				break;
		} else if (tmp_requested[i] == ']') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (bracket == 0)) {
				if (job_ptr->job_id) {
					verbose("%pJ invalid constraint %s",
						job_ptr, features);
				} else {
					verbose("Reservation invalid constraint %s",
						features);
				}
				xfree(tmp_requested);
				return feature_err;
			}
			bracket--;
		} else if (tmp_requested[i] == '(') {
			tmp_requested[i] = '\0';
			if ((feature != NULL) || paren) {
				fail = true;
				break;
			}
			paren++;
		} else if (tmp_requested[i] == ')') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (paren == 0)) {
				fail = true;
				break;
			}
			paren--;
		} else if (tmp_requested[i] == '\0') {
			if (feature) {
				feat = xmalloc(sizeof(job_feature_t));
				feat->name = xstrdup(feature);
				feat->changeable = node_features_g_changeable_feature(
					feature);
				feat->count = count;
				feat->paren = paren;
				feat->op_code = FEATURE_OP_END;
				list_append(*feature_list, feat);
			}
			break;
		} else if (feature == NULL) {
			feature = &tmp_requested[i];
		}
	}
	xfree(tmp_requested);
	if (fail) {
		if (job_ptr->job_id) {
			verbose("%pJ invalid constraint %s",
				job_ptr, features);
		} else {
			verbose("Reservation invalid constraint %s",
				features);
		}
		return ESLURM_INVALID_FEATURE;
	}
	if (brack_set_count > 1) {
		if (job_ptr->job_id) {
			verbose("%pJ constraint has more than one set of brackets: %s",
				job_ptr, detail_ptr->features);
		} else {
			verbose("Reservation constraint has more than one set of brackets: %s",
				detail_ptr->features);
		}
		return ESLURM_INVALID_FEATURE;
	}
	if (bracket != 0) {
		if (job_ptr->job_id) {
			verbose("%pJ constraint has unbalanced brackets: %s",
				job_ptr, features);
		} else {
			verbose("Reservation constraint has unbalanced brackets: %s",
				features);
		}
		return ESLURM_INVALID_FEATURE;
	}
	if (paren != 0) {
		if (job_ptr->job_id) {
			verbose("%pJ constraint has unbalanced parenthesis: %s",
				job_ptr, features);
		} else {
			verbose("Reservation constraint has unbalanced parenthesis: %s",
				features);
		}
		return feature_err;
	}

	if (job_ptr->batch_features) {
		detail_ptr->feature_list_use = *feature_list;
		detail_ptr->features_use = features;
		rc = _valid_batch_features(job_ptr, can_reboot);
		detail_ptr->feature_list_use = NULL;
		detail_ptr->features_use = NULL;
		if (rc != SLURM_SUCCESS)
			return rc;
	}

	rc = _valid_feature_list(job_ptr, *feature_list, can_reboot);
	if (rc == ESLURM_INVALID_FEATURE)
		return feature_err;
	return rc;
}

/*
 * Delete a record from a job's feature_list
 */
extern void feature_list_delete(void *x)
{
	job_feature_t *feature_ptr = (job_feature_t *)x;
	xfree(feature_ptr->name);
	FREE_NULL_BITMAP(feature_ptr->node_bitmap_active);
	FREE_NULL_BITMAP(feature_ptr->node_bitmap_avail);
	xfree(feature_ptr);
}

static int _match_job_feature(void *x, void *key)
{
	job_feature_t *feat = (job_feature_t *) x;
	char *tok = (char *) key;

	if (!xstrcmp(feat->name, tok))	/* Found matching feature name */
		return 1;
	return 0;
}

static int _valid_batch_features(job_record_t *job_ptr, bool can_reboot)
{
	char *tmp, *tok, *save_ptr = NULL;
	int rc = SLURM_SUCCESS;
	bool have_or = false, success_or = false;

	if (!job_ptr->batch_features)
		return SLURM_SUCCESS;
	if (!job_ptr->details || !job_ptr->details->feature_list_use)
		return ESLURM_BATCH_CONSTRAINT;

	if (strchr(job_ptr->batch_features, '|'))
		have_or = true;
	tmp = xstrdup(job_ptr->batch_features);
	tok = strtok_r(tmp, "&|", &save_ptr);
	while (tok) {
		if (!list_find_first(job_ptr->details->feature_list_use,
				     _match_job_feature, tok)) {
			rc = ESLURM_BATCH_CONSTRAINT;
			break;
		}
		rc = _valid_node_feature(tok, can_reboot);
		if (have_or) {
			if (rc == SLURM_SUCCESS)
				success_or = true;
			/* Ignore failure on some OR components */
		} else if (rc != SLURM_SUCCESS) {
			rc = ESLURM_BATCH_CONSTRAINT;
			break;
		}
		tok = strtok_r(NULL, "&|", &save_ptr);
	}
	xfree(tmp);

	if (have_or && success_or)
		return SLURM_SUCCESS;
	return rc;
}

static int _valid_feature_list(job_record_t *job_ptr, List feature_list,
			       bool can_reboot)
{
	ListIterator feat_iter;
	job_feature_t *feat_ptr;
	char *buf = NULL;
	int bracket = 0, paren = 0;
	int rc = SLURM_SUCCESS;
	bool has_xand = false, has_xor = false;

	if (feature_list == NULL) {
		if (job_ptr->job_id)
			debug2("%pJ feature list is empty", job_ptr);
		else
			debug2("Reservation feature list is empty");
		return rc;
	}

	feat_iter = list_iterator_create(feature_list);
	while ((feat_ptr = list_next(feat_iter))) {
		if ((feat_ptr->op_code == FEATURE_OP_XOR) ||
		    (feat_ptr->op_code == FEATURE_OP_XAND)) {
			if (bracket == 0)
				xstrcat(buf, "[");
			bracket = feat_ptr->paren + 1;
		}
		if (feat_ptr->paren > paren) {
			xstrcat(buf, "(");
			paren = feat_ptr->paren;
		}
		xstrcat(buf, feat_ptr->name);
		if (feat_ptr->paren < paren) {
			xstrcat(buf, ")");
			paren = feat_ptr->paren;
		}
		if (rc == SLURM_SUCCESS)
			rc = _valid_node_feature(feat_ptr->name, can_reboot);
		if (feat_ptr->count)
			xstrfmtcat(buf, "*%u", feat_ptr->count);
		if (feat_ptr->op_code == FEATURE_OP_XAND && !feat_ptr->count)
			rc = ESLURM_INVALID_FEATURE;
		if (feat_ptr->op_code == FEATURE_OP_XOR && feat_ptr->count)
			rc = ESLURM_INVALID_FEATURE;
		if ((bracket > paren) &&
		    ((feat_ptr->op_code != FEATURE_OP_XOR) &&
		     (feat_ptr->op_code != FEATURE_OP_XAND))) {
			if ((has_xand && !feat_ptr->count) ||
			    (has_xor && feat_ptr->count))
				rc = ESLURM_INVALID_FEATURE;
			xstrcat(buf, "]");
			bracket = 0;
			has_xand = false;
			has_xor = false;
		}
		if ((feat_ptr->op_code == FEATURE_OP_AND) ||
		    (feat_ptr->op_code == FEATURE_OP_XAND))
			xstrcat(buf, "&");
		else if ((feat_ptr->op_code == FEATURE_OP_OR) ||
			 (feat_ptr->op_code == FEATURE_OP_XOR))
			xstrcat(buf, "|");
		if (feat_ptr->op_code == FEATURE_OP_XAND)
			has_xand = true;
		if (feat_ptr->op_code == FEATURE_OP_XOR)
			has_xor = true;
	}
	list_iterator_destroy(feat_iter);

	if (rc == SLURM_SUCCESS) {
		if (job_ptr->job_id)
			debug("%pJ feature list: %s", job_ptr, buf);
		else
			debug("Reservation feature list: %s", buf);
	} else {
		if (job_ptr->job_id) {
			info("%pJ has invalid feature list: %s",
			     job_ptr, buf);
		} else {
			info("Reservation has invalid feature list: %s", buf);
		}
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
	while ((feature_ptr = list_next(feature_iter))) {
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
extern void rebuild_job_part_list(job_record_t *job_ptr)
{
	ListIterator part_iterator;
	part_record_t *part_ptr;

	if (!job_ptr->part_ptr_list)
		return;
	if (!job_ptr->part_ptr || !job_ptr->part_ptr->name) {
		error("%pJ has NULL part_ptr or the partition name is NULL",
		      job_ptr);
		return;
	}

	xfree(job_ptr->partition);
	job_ptr->partition = xstrdup(job_ptr->part_ptr->name);

	part_iterator = list_iterator_create(job_ptr->part_ptr_list);
	while ((part_ptr = list_next(part_iterator))) {
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
void cleanup_completing(job_record_t *job_ptr)
{
	time_t delay;

	if (job_ptr->epilog_running)
		return;

	log_flag(TRACE_JOBS, "%s: %pJ", __func__, job_ptr);

	delay = last_job_update - job_ptr->end_time;
	if (delay > 60) {
		info("%s: %pJ completion process took %ld seconds",
		     __func__, job_ptr, (long) delay);
	}

	license_job_return(job_ptr);
	gs_job_fini(job_ptr);

	delete_step_records(job_ptr);
	job_ptr->job_state &= (~JOB_COMPLETING);
	job_hold_requeue(job_ptr);

	/*
	 * Clear alloc tres fields after a requeue. job_set_alloc_tres will
	 * clear the fields when the job is pending and not completing.
	 */
	if (IS_JOB_PENDING(job_ptr))
		job_set_alloc_tres(job_ptr, false);

	/* Job could be pending if the job was requeued due to a node failure */
	if (IS_JOB_COMPLETED(job_ptr))
		fed_mgr_job_complete(job_ptr, job_ptr->exit_code,
				     job_ptr->start_time);
}

void main_sched_init(void)
{
	if (thread_id_sched)
		return;
	slurm_thread_create(&thread_id_sched, _sched_agent, NULL);
}

void main_sched_fini(void)
{
	if (!thread_id_sched)
		return;
	slurm_cond_broadcast(&sched_cond);
	pthread_join(thread_id_sched, NULL);
}
