/*****************************************************************************\
 * job_scheduler.c - manage the scheduling of pending jobs in priority order
 *	Note there is a global job list (job_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) SchedMD LLC.
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
#include "src/common/cpu_frequency.h"
#include "src/common/env.h"
#include "src/common/group_cache.h"
#include "src/common/job_features.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/parse_time.h"
#include "src/common/strlcpy.h"
#include "src/common/timers.h"
#include "src/common/track_script.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/acct_gather.h"
#include "src/interfaces/burst_buffer.h"
#include "src/interfaces/gres.h"
#include "src/interfaces/node_features.h"
#include "src/interfaces/preempt.h"
#include "src/interfaces/prep.h"
#include "src/interfaces/select.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gang.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/state_save.h"

#include "src/stepmgr/gres_stepmgr.h"
#include "src/stepmgr/srun_comm.h"
#include "src/stepmgr/stepmgr.h"

#ifndef CORRESPOND_ARRAY_TASK_CNT
#  define CORRESPOND_ARRAY_TASK_CNT 10
#endif
#define BUILD_TIMEOUT 2000000	/* Max build_job_queue() run time in usec */

typedef enum {
	ARRAY_SPLIT_BURST_BUFFER,
	ARRAY_SPLIT_AFTER_CORR,
} array_split_type_t;

typedef struct {
	list_t *job_list;
	int pend_cnt_limit;
	char *reason_msg;
	array_split_type_t type;
} split_job_t;

typedef struct {
	bool backfill;
	int job_prio_pairs;
	job_record_t *job_ptr;
	list_t *job_queue;
	time_t now;
	int prio_inx;
} build_job_queue_for_part_t;

typedef struct {
	bool completing;
	bitstr_t *eff_cg_bitmap;
	time_t recent;
} job_is_comp_t;

typedef struct {
	uint32_t prio;
	bool set;
} part_prios_same_t;

typedef struct {
	char *cg_part_str;
	char *cg_part_str_pos;
	bitstr_t *eff_cg_bitmap;
} part_reduce_frag_t;

typedef struct {
	job_record_t *het_job;
	job_record_t *het_job_leader;
	job_record_t *job_ptr;
} het_job_ready_t;

typedef struct {
	job_record_t *het_job_leader;
	int het_job_offset;
	batch_job_launch_msg_t *launch_msg_ptr;
} het_job_env_t;

typedef struct {
	job_record_t *job_ptr;
	char *sep;
	bool set_or_flag;
} depend_str_t;

typedef struct {
	bool and_failed;
	bool changed;
	bool has_local_depend;
	bool has_unfulfilled;
	job_record_t *job_ptr;
	bool or_flag;
	bool or_satisfied;
} test_job_dep_t;

typedef struct {
	uint64_t cume_space_time;
	job_record_t *job_ptr;
	uint32_t part_cpus_per_node;
} delay_start_t;

typedef struct {
	job_record_t *job_ptr;
	time_t now;
	int rc;
	will_run_response_msg_t **resp;
} job_start_data_t;

typedef struct {
	int bracket;
	bool can_reboot;
	char *debug_str;
	char *features;
	list_t *feature_list;
	bool has_xand;
	bool has_mor;
	int paren;
	int rc;
	bool skip_validation;
} valid_feature_t;

static batch_job_launch_msg_t *_build_launch_job_msg(job_record_t *job_ptr,
						     uint16_t protocol_version);
static bool	_job_runnable_test1(job_record_t *job_ptr, bool clear_start);
static bool	_job_runnable_test2(job_record_t *job_ptr, time_t now,
				    bool check_min_time);
static bool _scan_depend(list_t *dependency_list, job_record_t *job_ptr);
static void *	_sched_agent(void *args);
static void _set_schedule_exit(schedule_exit_t code);
static int	_schedule(bool full_queue);
static int	_valid_batch_features(job_record_t *job_ptr, bool can_reboot);
static int _valid_feature_list(job_record_t *job_ptr,
			       valid_feature_t *valid_feature,
			       bool is_reservation);
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
	 * and not a het job, or not part of the same het job
	 */
	if ((IS_JOB_RUNNING(qjob_ptr) || IS_JOB_SUSPENDED(qjob_ptr) ||
	     (IS_JOB_PENDING(qjob_ptr) &&
	      (qjob_ptr->job_id < job_ptr->job_id))) &&
	    (!job_ptr->het_job_id ||
	     (job_ptr->het_job_id != qjob_ptr->het_job_id))) {
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

static void _job_queue_append(list_t *job_queue, job_record_t *job_ptr,
			      uint32_t prio)
{
	job_queue_req_t job_queue_req = { .job_ptr = job_ptr,
					  .job_queue = job_queue,
					  .part_ptr = job_ptr->part_ptr,
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

	/*
	 * For het jobs, backfill makes a plan for each component; however,
	 * backfill doesn't track magnetic reservations in the plan, so backfill
	 * can't start hetjobs in a magnetic reservation unless the het job
	 * explicitly requests the magnetic reservation.
	 *
	 * Also, if there is a magnetic reservation that starts in the future,
	 * backfill will not be able to start the het job if there is a separate
	 * magnetic reservation queue record for the component. So, don't create
	 * a separate magnetic reservation queue record for het jobs.
	 */
	if (job_ptr->het_job_id)
		return;

	job_resv_append_magnetic(&job_queue_req);
}

/* Job test for ability to run now, excludes partition specific tests */
static bool _job_runnable_test1(job_record_t *job_ptr, bool sched_plugin)
{
	bool job_indepen = false;
	time_t now = time(NULL);

	xassert(job_ptr->magic == JOB_MAGIC);
	if (!IS_JOB_PENDING(job_ptr) || IS_JOB_COMPLETING(job_ptr))
		return false;

	if (IS_JOB_REVOKED(job_ptr))
		return false;

	if ((job_ptr->details && job_ptr->details->prolog_running) ||
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
		if ((job_ptr->state_reason != FAIL_BAD_CONSTRAINTS) &&
		    (job_ptr->state_reason != FAIL_BURST_BUFFER_OP) &&
		    (job_ptr->state_reason != WAIT_HELD) &&
		    (job_ptr->state_reason != WAIT_HELD_USER) &&
		    (job_ptr->state_reason != WAIT_MAX_REQUEUE) &&
		    (job_ptr->state_reason != WAIT_RESV_INVALID) &&
		    (job_ptr->state_reason != WAIT_RESV_DELETED)) {
			job_ptr->state_reason = WAIT_HELD;
			xfree(job_ptr->state_desc);
			last_job_update = now;
		}
		sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
			     job_ptr,
			     job_state_string(job_ptr->job_state),
			     job_state_reason_string(job_ptr->state_reason),
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
	     (job_state_reason_check(job_ptr->state_reason, JSR_PART)))) {
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
	    !(job_ptr->resv_ptr->flags & RESERVE_FLAG_FLEX) &&
	    part_ptr && part_ptr->node_bitmap &&
	    (bit_overlap(job_ptr->resv_ptr->node_bitmap, part_ptr->node_bitmap)
	     < job_ptr->node_cnt_wag))
		return false;
	return true;
}

static int _find_depend_after_corr(void *x, void *arg)
{
	depend_spec_t *dep_ptr = x;

	if (dep_ptr->depend_type == SLURM_DEPEND_AFTER_CORRESPOND)
		return 1;

	return 0;
}

static job_record_t *_split_job_on_schedule_recurse(
	job_record_t *job_ptr, split_job_t *split_job)
{
	job_record_t *new_job_ptr;
	int array_task_id;

	if (num_pending_job_array_tasks(job_ptr->array_job_id) >=
	    split_job->pend_cnt_limit)
		return job_ptr;

	if (job_ptr->array_recs->task_cnt < 1)
		return job_ptr;

	array_task_id = bit_ffs(job_ptr->array_recs->task_id_bitmap);
	if (array_task_id < 0)
		return job_ptr;

	if (job_ptr->array_recs->task_cnt == 1) {
		job_ptr->array_task_id = array_task_id;
		new_job_ptr = job_array_post_sched(job_ptr, false);
		if (new_job_ptr != job_ptr) {
			if (!split_job->job_list)
				split_job->job_list = list_create(NULL);
			list_append(split_job->job_list, new_job_ptr);
		}
		if (job_ptr->details &&
		    job_ptr->details->dependency &&
		    job_ptr->details->depend_list)
			fed_mgr_submit_remote_dependencies(job_ptr,
							   false,
							   false);
		return new_job_ptr;
	}

	job_ptr->array_task_id = array_task_id;
	new_job_ptr = job_array_split(job_ptr, false);
	debug("%s: Split out %pJ for %s use",
	      __func__, job_ptr, split_job->reason_msg);
	job_state_set(new_job_ptr, JOB_PENDING);
	new_job_ptr->start_time = (time_t) 0;

	if (!split_job->job_list)
		split_job->job_list = list_create(NULL);
	list_append(split_job->job_list, new_job_ptr);

	/*
	 * Do NOT clear db_index here, it is handled when task_id_str
	 * is created elsewhere.
	 */

	if (split_job->type == ARRAY_SPLIT_BURST_BUFFER)
		(void) bb_g_job_validate2(new_job_ptr, NULL);

	/*
	 * See if we need to spawn off any more since the new_job_ptr now has
	 * ->array_recs.
	 */
	return _split_job_on_schedule_recurse(new_job_ptr, split_job);
}

static int _split_job_on_schedule(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	split_job_t *split_job = arg;

	if (!IS_JOB_PENDING(job_ptr) ||
	    !job_ptr->array_recs ||
	    !job_ptr->array_recs->task_id_bitmap ||
	    (job_ptr->array_task_id != NO_VAL))
		return 0;
	/*
	 * Create individual job records for job arrays that need burst buffer
	 * staging
	 */
	if (job_ptr->burst_buffer) {
		split_job->pend_cnt_limit = bb_array_stage_cnt;
		split_job->reason_msg = "burst buffer";
		split_job->type = ARRAY_SPLIT_BURST_BUFFER;
		job_ptr = _split_job_on_schedule_recurse(job_ptr, split_job);
	}

	/*
	 * Create individual job records for job arrays with
	 * depend_type == SLURM_DEPEND_AFTER_CORRESPOND
	 */
	if (job_ptr->details &&
	    job_ptr->details->depend_list &&
	    list_count(job_ptr->details->depend_list) &&
	    list_find_first(job_ptr->details->depend_list,
			    _find_depend_after_corr,
			    NULL)) {
		split_job->pend_cnt_limit = correspond_after_task_cnt;
		split_job->reason_msg = "SLURM_DEPEND_AFTER_CORRESPOND";
		split_job->type = ARRAY_SPLIT_AFTER_CORR;
		/* If another thing is added after this set job_ptr as above */
		(void) _split_job_on_schedule_recurse(job_ptr, split_job);
	}

	return 0;
}

static int _transfer_job_list(void *x, void *arg)
{
	list_append(job_list, x);

	return 0;
}

static int _build_job_queue_for_qos(void *x, void *arg)
{
	build_job_queue_for_part_t *setup_job = arg;
	job_record_t *job_ptr = setup_job->job_ptr;

	job_ptr->qos_ptr = x;

	/*
	 * priority_array index matches part_ptr_list * qos_list
	 * position: increment inx
	 */
	setup_job->prio_inx++;

	if (!_job_runnable_test2(job_ptr, setup_job->now, setup_job->backfill))
		return 0;

	setup_job->job_prio_pairs++;
	if (job_ptr->part_prio && job_ptr->part_prio->priority_array) {
		_job_queue_append(setup_job->job_queue, job_ptr,
				  job_ptr->part_prio->
				  priority_array[setup_job->prio_inx]);
	} else {
		_job_queue_append(setup_job->job_queue, job_ptr,
				  job_ptr->priority);
	}

	return 0;
}

static int _build_job_queue_for_part(void *x, void *arg)
{
	build_job_queue_for_part_t *setup_job = arg;
	job_record_t *job_ptr = setup_job->job_ptr;

	job_ptr->part_ptr = x;

	if (job_ptr->qos_list) {
		(void) list_for_each(job_ptr->qos_list,
				     _build_job_queue_for_qos,
				     setup_job);
	} else {
		(void) _build_job_queue_for_qos(job_ptr->qos_ptr, setup_job);
	}

	return 0;
}

static int _foreach_job_is_completing(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	job_is_comp_t *job_is_comp = arg;

	if (IS_JOB_COMPLETING(job_ptr) &&
	    (job_ptr->end_time >= job_is_comp->recent)) {
		job_is_comp->completing = true;

		/*
		 * Can return after finding first completing job so long
		 * as a map of nodes in partitions affected by
		 * completing jobs is not required.
		 */
		if (!job_is_comp->eff_cg_bitmap)
			return -1;
		else if (job_ptr->part_ptr)
			bit_or(job_is_comp->eff_cg_bitmap,
			       job_ptr->part_ptr->node_bitmap);
	}

	return 0;
}

static int _foreach_wait_front_end(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	time_t now = *(time_t *)arg;

	if (!IS_JOB_PENDING(job_ptr))
		return 0;

	if ((job_ptr->state_reason != WAIT_NO_REASON) &&
	    (job_ptr->state_reason != WAIT_RESOURCES) &&
	    (job_ptr->state_reason != WAIT_NODE_NOT_AVAIL))
		return 0;

	job_ptr->state_reason = WAIT_FRONT_END;
	xfree(job_ptr->state_desc);
	last_job_update = now;

	return 0;
}

static int _foreach_part_reduce_frag(void *x, void *arg)
{
	part_record_t *part_ptr = x;
	part_reduce_frag_t *part_reduce_frag = arg;

	if (bit_overlap_any(part_reduce_frag->eff_cg_bitmap,
			    part_ptr->node_bitmap) &&
	    (part_ptr->state_up & PARTITION_SCHED)) {
		part_ptr->flags |= PART_FLAG_SCHED_FAILED;
		if (slurm_conf.slurmctld_debug >= LOG_LEVEL_DEBUG) {
			xstrfmtcatat(part_reduce_frag->cg_part_str,
				     &part_reduce_frag->cg_part_str_pos,
				     "%s%s",
				     part_reduce_frag->cg_part_str ? "," : "",
				     part_ptr->name);
		}
	}

	return 0;
}

static int _foreach_setup_part_sched(void *x, void *arg)
{
	part_record_t *part_ptr = x;

	part_ptr->num_sched_jobs = 0;
	part_ptr->flags &= ~PART_FLAG_SCHED_FAILED;
	part_ptr->flags &= ~PART_FLAG_SCHED_CLEARED;

	return 0;
}

static int _foreach_setup_resv_sched(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = x;

	resv_ptr->flags &= ~RESERVE_FLAG_SCHED_FAILED;

	return 0;
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
extern list_t *build_job_queue(bool clear_start, bool backfill)
{
	static time_t last_log_time = 0;
	list_itr_t *job_iterator;
	job_record_t *job_ptr = NULL;
	struct timeval start_tv = {0, 0};
	int tested_jobs = 0;
	split_job_t split_job = { 0 };
	build_job_queue_for_part_t setup_job = {
		.backfill = backfill,
		.now = time(NULL),
	};
	/* init the timer */
	(void) slurm_delta_tv(&start_tv);
	setup_job.job_queue = list_create(xfree_ptr);

	(void) list_for_each(job_list, _split_job_on_schedule, &split_job);

	if (split_job.job_list) {
		/*
		 * We can't use list_transfer() because we don't have the same
		 * destroy function.
		 */
		(void) list_for_each(split_job.job_list,
				     _transfer_job_list, NULL);
		FREE_NULL_LIST(split_job.job_list);
	}

	/*
	 * This cannot be a list_for_each This calls _job_runnable_test1() ->
	 * job_independent() -> test_job_dependency() which needs to call
	 * list_find_first() on the job_list making it impossible to also have
	 * this a list_find_first() on job_list.
	 */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		setup_job.job_ptr = job_ptr;

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
				last_job_update = setup_job.now;
			}
		}

		if (((tested_jobs % 100) == 0) &&
		    (slurm_delta_tv(&start_tv) >= build_queue_timeout)) {
			if (difftime(setup_job.now, last_log_time) > 600) {
				/* Log at most once every 10 minutes */
				info("%s has run for %d usec, exiting with %d of %d jobs tested, %d job-partition-qos pairs added",
				     __func__, build_queue_timeout, tested_jobs,
				     list_count(job_list),
				     setup_job.job_prio_pairs);
				last_log_time = setup_job.now;
			}
			break;
		}
		tested_jobs++;
		job_ptr->preempt_in_progress = false;	/* initialize */
		if (job_ptr->array_recs && backfill)
			job_ptr->array_recs->pend_run_tasks = 0;
		if (job_ptr->resv_list)
			job_ptr->resv_ptr = NULL;
		if (!_job_runnable_test1(job_ptr, clear_start))
			continue;

		setup_job.prio_inx = -1;
		if (job_ptr->part_ptr_list) {
			(void) list_for_each(job_ptr->part_ptr_list,
					     _build_job_queue_for_part,
					     &setup_job);
		} else {
			if (job_ptr->part_ptr == NULL) {
				part_record_t *part_ptr =
					find_part_record(job_ptr->partition);
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
			(void) _build_job_queue_for_part(job_ptr->part_ptr,
							 &setup_job);
		}
	}
	list_iterator_destroy(job_iterator);

	return setup_job.job_queue;
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
	job_is_comp_t job_is_comp = {
		.eff_cg_bitmap = eff_cg_bitmap,
	};

	if ((job_list == NULL) || (slurm_conf.complete_wait == 0))
		return false;

	job_is_comp.recent = time(NULL) - slurm_conf.complete_wait;

	(void) list_for_each(job_list, _foreach_job_is_completing,
			     &job_is_comp);

	return job_is_comp.completing;
}

/*
 * set_job_elig_time - set the eligible time for pending jobs once their
 *      dependencies are lifted (in job->details->begin_time)
 */
extern void set_job_elig_time(void)
{
	job_record_t *job_ptr = NULL;
	part_record_t *part_ptr = NULL;
	list_itr_t *job_iterator;
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, NO_LOCK };
	time_t now = time(NULL);

	lock_slurmctld(job_write_lock);

	/*
	 * This cannot be a list_for_each. This calls _job_runnable_test1() ->
	 * job_independent() -> test_job_dependency() which needs to call
	 * list_find_first() on the job_list making it impossible to also have
	 * this a list_find_first() on job_list.
	 */
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

static void _do_diag_stats(long delta_t)
{
	if (delta_t > slurmctld_diag_stats.schedule_cycle_max)
		slurmctld_diag_stats.schedule_cycle_max = delta_t;

	slurmctld_diag_stats.schedule_cycle_sum += delta_t;
	slurmctld_diag_stats.schedule_cycle_last = delta_t;
	slurmctld_diag_stats.schedule_cycle_counter++;
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

	while (true) {
		slurm_mutex_lock(&sched_mutex);
		while (true) {
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
 * func IN - function name used for logging
 * RET - true of valid, false if invalid and job cancelled
 */
extern bool deadline_ok(job_record_t *job_ptr, const char *func)
{
	time_t now;
	char time_str_deadline[256];
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
			     func, job_ptr, job_ptr->time_min,
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
			     func, job_ptr, job_ptr->time_limit,
			     time_str_deadline);
			fail_job = true;
		}
	}
	if (fail_job) {
		last_job_update = now;
		job_state_set(job_ptr, JOB_DEADLINE);
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
		       job_state_reason_string(job_ptr->state_reason));
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
	job_queue_rec->qos_ptr = job_queue_req->job_ptr->qos_ptr;
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

static void _set_features(job_record_t *job_ptr, bool use_prefer)
{
	/*
	 * feature_list_use is a temporary variable and should
	 * be reset before each use. Do this after the check for
	 * pending because the job could have started with
	 * "preferred" job_queue_rec.
	 */
	if (use_prefer) {
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
}

static void _set_schedule_exit(schedule_exit_t code)
{
	xassert(code < SCHEDULE_EXIT_COUNT);

	slurmctld_diag_stats.schedule_exit[code]++;
}

static int _get_nodes_in_reservations(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = x;
	bitstr_t *node_bitmap = arg;

	xassert(resv_ptr);
	xassert(node_bitmap);

	if (resv_ptr->node_bitmap)
		bit_or(node_bitmap, resv_ptr->node_bitmap);
	return SLURM_SUCCESS;
}

static int _schedule(bool full_queue)
{
	list_t *job_queue = NULL;
	int job_cnt = 0;
	int error_code, i, time_limit, pend_time;
	uint32_t job_depth = 0, array_task_id;
	job_queue_rec_t *job_queue_rec;
	job_record_t *job_ptr = NULL;
	part_record_t *part_ptr, *skip_part_ptr = NULL;
	bitstr_t *save_avail_node_bitmap;
	int bb_wait_cnt = 0;
	/* Locks: Read config, write job, write node, read partition */
	slurmctld_lock_t job_write_lock =
		{ READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	bool is_job_array_head;
	static time_t sched_update = 0;
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
	bool reject_array_use_prefer = false;
	bool use_prefer;
	bool fail_by_part, wait_on_resv, fail_by_part_non_reserve;
	uint32_t deadline_time_limit, save_time_limit = 0;
	uint32_t prio_reserve;
	DEF_TIMERS;
	job_node_select_t job_node_select = { 0 };
	static bool ignore_prefer_val = false;

	if (slurmctld_config.shutdown_time)
		return 0;

	if (sched_update != slurm_conf.last_update) {
		char *tmp_ptr;

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
				 *
				 * No memory is allocated above this.
				 */
				return 0;
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

		if (xstrcasestr(slurm_conf.sched_params,
				"ignore_prefer_validation"))
			ignore_prefer_val = true;
		else
			ignore_prefer_val = false;

		sched_update = slurm_conf.last_update;
		if (slurm_conf.sched_params && strlen(slurm_conf.sched_params))
			info("SchedulerParameters=%s", slurm_conf.sched_params);
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
		(void) list_for_each(job_list, _foreach_wait_front_end, &now);
		unlock_slurmctld(job_write_lock);
		sched_debug("schedule() returning, no front end nodes are available");
		goto out;
	}

	if (!reduce_completing_frag && job_is_completing(NULL)) {
		unlock_slurmctld(job_write_lock);
		sched_debug("schedule() returning, some job is still completing");
		goto out;
	}

	(void) list_for_each(part_list, _foreach_setup_part_sched, NULL);
	(void) list_for_each(resv_list, _foreach_setup_resv_sched, NULL);

	save_avail_node_bitmap = bit_copy(avail_node_bitmap);

	/* Avoid resource fragmentation if important */
	if (reduce_completing_frag) {
		bitstr_t *eff_cg_bitmap = bit_alloc(node_record_count);
		if (job_is_completing(eff_cg_bitmap)) {
			part_reduce_frag_t part_reduce_frag = {
				.eff_cg_bitmap = eff_cg_bitmap,
			};
			(void) list_for_each(part_list,
					     _foreach_part_reduce_frag,
					     &part_reduce_frag);
			if (part_reduce_frag.cg_part_str) {
				sched_debug("some job is still completing, skipping partitions '%s'",
					    part_reduce_frag.cg_part_str);
				xfree(part_reduce_frag.cg_part_str);
			}
		}
		FREE_NULL_BITMAP(eff_cg_bitmap);
	}

	sched_debug("Running job scheduler %s.", full_queue ? "for full queue":"for default depth");
	job_queue = build_job_queue(false, false);
	slurmctld_diag_stats.schedule_queue_len = list_count(job_queue);
	sort_job_queue(job_queue);

	job_ptr = NULL;
	wait_on_resv = false;
	while (1) {
		/* Run some final guaranteed logic after each job iteration */
		if (job_ptr) {
			job_resv_clear_magnetic_flag(job_ptr);
			fill_array_reasons(job_ptr, reject_array_job);
		}

		job_queue_rec = list_pop(job_queue);
		if (!job_queue_rec) {
			_set_schedule_exit(SCHEDULE_EXIT_END);
			break;
		}
		array_task_id = job_queue_rec->array_task_id;
		job_ptr = job_queue_rec->job_ptr;
		part_ptr = job_queue_rec->part_ptr;

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
		if (!job_ptr ||
		    !IS_JOB_PENDING(job_ptr) || /* started in other part/qos */
		    !job_ptr->priority) { /* held from fail in other part/qos */
			xfree(job_queue_rec);
			continue;
		}

		use_prefer = job_queue_rec->use_prefer;
		_set_features(job_ptr, use_prefer);

		if (job_ptr->resv_list)
			job_queue_rec_resv_list(job_queue_rec);
		else
			job_queue_rec_magnetic_resv(job_queue_rec);

		if (!_job_runnable_test3(job_ptr, part_ptr)) {
			xfree(job_queue_rec);
			continue;
		}

		job_ptr->qos_ptr = job_queue_rec->qos_ptr;
		job_ptr->part_ptr = part_ptr;
		job_ptr->priority = job_queue_rec->priority;

		xfree(job_queue_rec);

		job_ptr->last_sched_eval = time(NULL);

		if (job_ptr->preempt_in_progress)
			continue;	/* scheduled in another partition */

		if (job_ptr->het_job_id) {
			fail_by_part = true;
			fail_by_part_non_reserve = false;
			goto fail_this_part;
		}

		if (job_ptr->array_recs && (job_ptr->array_task_id == NO_VAL))
			is_job_array_head = true;
		else
			is_job_array_head = false;

next_task:
		if ((time(NULL) - sched_start) >= sched_timeout) {
			sched_debug("loop taking too long, breaking out");
			_set_schedule_exit(SCHEDULE_EXIT_TIMEOUT);
			break;
		}
		if (sched_max_job_start && (job_cnt >= sched_max_job_start)) {
			sched_debug("sched_max_job_start reached, breaking out");
			_set_schedule_exit(SCHEDULE_EXIT_MAX_JOB_START);
			break;
		}

		if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
			if (reject_array_job &&
			    (reject_array_job->array_job_id ==
				job_ptr->array_job_id) &&
			    (reject_array_part == part_ptr) &&
			    (reject_array_resv == job_ptr->resv_ptr) &&
			    (reject_array_use_prefer == use_prefer))
				continue;  /* already rejected array element */


			/* assume reject whole array for now, clear if OK */
			reject_array_job = job_ptr;
			reject_array_part = part_ptr;
			reject_array_resv = job_ptr->resv_ptr;
			reject_array_use_prefer = use_prefer;

			if (!job_array_start_test(job_ptr))
				continue;
		}
		if (max_jobs_per_part &&
		    (max_jobs_per_part < ++job_ptr->part_ptr->num_sched_jobs)) {
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
		if (!full_queue && (job_depth++ > def_job_limit)) {
			sched_debug("already tested %u jobs, breaking out",
				    job_depth);
			_set_schedule_exit(SCHEDULE_EXIT_MAX_DEPTH);
			break;
		}

		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		if ((defer_rpc_cnt > 0) &&
		    (slurmctld_config.server_thread_count >= defer_rpc_cnt)) {
			sched_debug("schedule() returning, too many RPCs");
			slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
			_set_schedule_exit(SCHEDULE_EXIT_RPC_CNT);
			break;
		}
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

		if (job_limits_check(&job_ptr, false) != WAIT_NO_REASON) {
			/* should never happen */
			continue;
		}

		slurmctld_diag_stats.schedule_cycle_depth++;

		if (job_ptr->resv_name) {
			/*
			 * If we have a MaxStartDelay we need to make sure we
			 * don't schedule any jobs that could potentially run to
			 * avoid starvation of this job.
			 */
			if (job_ptr->resv_ptr &&
			    job_ptr->resv_ptr->max_start_delay)
				wait_on_resv = true;

			if (job_ptr->resv_ptr->flags &
			    RESERVE_FLAG_SCHED_FAILED) {
				job_ptr->state_reason = WAIT_PRIORITY;
				xfree(job_ptr->state_desc);
				last_job_update = now;
				sched_debug3("%pJ. State=PENDING. Reason=Priority. Priority=%u. Resv=%s.",
					     job_ptr,
					     job_ptr->priority,
					     job_ptr->resv_name);
				continue;
			}
		} else if (job_ptr->part_ptr->flags & PART_FLAG_SCHED_FAILED) {
			if (!(job_ptr->part_ptr->flags &
			      PART_FLAG_SCHED_CLEARED)) {
				bit_and_not(avail_node_bitmap,
					    part_ptr->node_bitmap);
				job_ptr->part_ptr->flags |=
					PART_FLAG_SCHED_CLEARED;
			}

			if ((job_ptr->state_reason == WAIT_NO_REASON) ||
			    (job_ptr->state_reason == WAIT_RESOURCES)) {
				sched_debug("%pJ unable to schedule in Partition=%s (per PART_FLAG_SCHED_FAILED). State=PENDING. Previous-Reason=%s. Previous-Desc=%s. New-Reason=Priority. Priority=%u.",
					    job_ptr,
					    job_ptr->part_ptr->name,
					    job_state_reason_string(
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
				sched_debug2("%pJ. unable to schedule in Partition=%s (per PART_FLAG_SCHED_FAILED). Retaining previous scheduling Reason=%s. Desc=%s. Priority=%u.",
					     job_ptr,
					     job_ptr->part_ptr->name,
					     job_state_reason_string(
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
		if (job_ptr->qos_ptr) {
			assoc_mgr_lock_t locks =
				{ .assoc = READ_LOCK, .qos = READ_LOCK };

			assoc_mgr_lock(&locks);
			if (job_ptr->assoc_ptr
			    && (accounting_enforce & ACCOUNTING_ENFORCE_QOS)
			    && ((job_ptr->qos_ptr->id >= g_qos_count) ||
				!job_ptr->assoc_ptr->usage ||
				!job_ptr->assoc_ptr->usage->valid_qos ||
				!bit_test(job_ptr->assoc_ptr->usage->valid_qos,
					  job_ptr->qos_ptr->id))
			    && !job_ptr->limit_set.qos) {
				assoc_mgr_unlock(&locks);
				sched_debug("%pJ has invalid QOS", job_ptr);
				job_fail_qos(job_ptr, __func__, false);
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
			if (!deadline_ok(job_ptr, __func__))
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

		if (job_state_reason_check(job_ptr->state_reason,
					   JSR_QOS_ASSOC) &&
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
			job_ptr->state_desc =
				xstrdup_printf("Nodes required for job are DOWN, DRAINED%s or reserved for jobs in higher priority partitions",
					       bit_overlap(rs_node_bitmap,
							   job_ptr->part_ptr->
							   node_bitmap) ? ", REBOOTING" : "");
			last_job_update = now;
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u. Partition=%s.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_state_reason_string(
					     job_ptr->state_reason),
				     job_ptr->priority, job_ptr->partition);
			fail_by_part = true;
			fail_by_part_non_reserve = false;
			goto fail_this_part;
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

		job_node_select.job_ptr = job_ptr;
		error_code = select_nodes(&job_node_select,
					  false, false,
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
			/*
			 * Node config unavailable plus state_reason
			 * FAIL_BAD_CONSTRAINTS causes the job to be held
			 * later. If job specs were unsatisfied due to
			 * --prefer, give the opportunity to test the record
			 * without it in a second attempt by resetting
			 * state_reason to FAIL_CONSTRAINTS.
			 */
			if (ignore_prefer_val && job_ptr->details->prefer &&
			    job_ptr->details->prefer_list &&
			    (job_ptr->details->prefer_list ==
			     job_ptr->details->feature_list_use) &&
			    (error_code ==
			     ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			    (job_ptr->state_reason == FAIL_BAD_CONSTRAINTS)) {
				sched_debug2("StateReason='%s' set after evaluating %pJ in partition %s (maybe unsatisfied due to --prefer while ignore_prefer_validation configured). Re-testing without --prefer if needed.",
					     job_state_reason_string(job_ptr->state_reason), job_ptr, job_ptr->part_ptr->name);
				job_ptr->state_reason = FAIL_CONSTRAINTS;
			}

			fed_mgr_job_unlock(job_ptr);
		}

skip_start:

		fail_by_part = false;
		fail_by_part_non_reserve = false;
		if ((error_code != SLURM_SUCCESS) && deadline_time_limit)
			job_ptr->time_limit = save_time_limit;
		if (error_code == ESLURM_NODES_BUSY) {
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u. Partition=%s.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_state_reason_string(
					     job_ptr->state_reason),
				     job_ptr->priority, job_ptr->partition);
			fail_by_part = true;
		} else if (error_code == ESLURM_LICENSES_UNAVAILABLE) {
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_state_reason_string(
					     job_ptr->state_reason),
				     job_ptr->priority);
			if (bf_licenses) {
				sched_debug("%pJ is blocked on licenses. Stopping scheduling so license backfill can handle this",
					    job_ptr);
				_set_schedule_exit(SCHEDULE_EXIT_LIC);
				break;
			}
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
				     job_state_reason_string(
					     job_ptr->state_reason),
				     job_ptr->priority);
			continue;
		} else if ((error_code == ESLURM_RESERVATION_BUSY) ||
			   (error_code == ESLURM_RESERVATION_NOT_USABLE)) {
			if (job_ptr->resv_ptr &&
			    job_ptr->resv_ptr->node_bitmap) {
				sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
					     job_ptr,
					     job_state_string(job_ptr->job_state),
					     job_state_reason_string(
						     job_ptr->state_reason),
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
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u. Partition=%s. Couldn't get federation job lock.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_state_reason_string(
					     job_ptr->state_reason),
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
				    (bb_g_job_test_stage_in(job_ptr, false) ==
				     1)) {
					_set_features(job_ptr, use_prefer);
					goto next_task;
				}
			}
			continue;
		} else if ((error_code ==
			    ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			   (job_ptr->resv_ptr)) {
			debug("%pJ non-runnable in reservation %s: %s",
			      job_ptr, job_ptr->resv_ptr->name,
			      slurm_strerror(error_code));
		} else if ((error_code ==
			    ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			   job_ptr->part_ptr_list) {
			debug("%pJ non-runnable in partition %s: %s",
			      job_ptr, job_ptr->part_ptr->name,
			      slurm_strerror(error_code));
		} else if ((error_code ==
			    ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) &&
			   (job_ptr->state_reason == FAIL_CONSTRAINTS)) {
			sched_info("%pJ current node constraints not satisfied",
				   job_ptr);
			/*
			 * Future node updates may satisfy the constraints, so
			 * do not hold the job.
			 */
		} else if (error_code == ESLURM_ACCOUNTING_POLICY) {
			sched_debug3("%pJ delayed for accounting policy",
				     job_ptr);
			/* potentially starve this job */
			if (assoc_limit_stop)
				fail_by_part = true;
		} else if (error_code == ESLURM_MAX_POWERED_NODES) {
			sched_debug2("%pJ cannot start: %s",
				   job_ptr, slurm_strerror(error_code));
			job_ptr->state_reason = WAIT_MAX_POWERED_NODES;
			xfree(job_ptr->state_desc);
		} else if (error_code == ESLURM_PORTS_BUSY) {
			/*
			 * This can only happen if using stepd step manager.
			 * The nodes selected for the job ran out of ports.
			 */
			fail_by_part = true;
			job_ptr->state_reason = WAIT_MPI_PORTS_BUSY;
			xfree(job_ptr->state_desc);
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u. Partition=%s.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_state_reason_string(
					     job_ptr->state_reason),
				     job_ptr->priority, job_ptr->partition);
		} else if ((error_code !=
			    ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) &&
			   (error_code != ESLURM_NODE_NOT_AVAIL)      &&
			   (error_code != ESLURM_INVALID_BURST_BUFFER_REQUEST)){
			sched_info("schedule: %pJ non-runnable: %s",
				   job_ptr, slurm_strerror(error_code));
			last_job_update = now;
			job_state_set(job_ptr, JOB_PENDING);
			job_ptr->state_reason = FAIL_BAD_CONSTRAINTS;
			xfree(job_ptr->state_desc);
			job_ptr->start_time = job_ptr->end_time = now;
			job_ptr->priority = 0;
			debug2("%s: setting %pJ to \"%s\" (%s)",
			       __func__, job_ptr,
			       job_state_reason_string(job_ptr->state_reason),
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
			/*
			 * If the reservation is not FLEX or ANY_NODES, other
			 * jobs in this partition can be scheduled.
			 *
			 * Jobs submitted to FLEX or ANY_NODES reservations can
			 * use nodes outside of the reservation. If the
			 * reservation is FLEX or ANY_NODES, other jobs in
			 * this partition submitted to other reservations can
			 * be scheduled.
			 *
			 * In both cases, do not schedule more jobs in this
			 * reservation.
			 */
			if ((job_ptr->resv_ptr->flags & RESERVE_FLAG_FLEX) ||
			    (job_ptr->resv_ptr->flags & RESERVE_FLAG_ANY_NODES))
				fail_by_part_non_reserve = true;
			else
				fail_by_part = false;

			job_ptr->resv_ptr->flags |= RESERVE_FLAG_SCHED_FAILED;
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
			if (job_ptr->part_ptr->flags & PART_FLAG_SCHED_FAILED) {
				fail_by_part = false;
				break;
			}
		}
		if (fail_by_part) {
			/*
			 * Do not schedule more jobs in this partition or on
			 * nodes in this partition
			 */
			job_ptr->part_ptr->flags |= PART_FLAG_SCHED_FAILED;

			if (fail_by_part_non_reserve) {
				/*
				 * If a FLEX or ANY_NODES reservation job fails
				 * by part, remove all nodes that are not in
				 * reservations from avail_node_bitmap.
				 *
				 * Jobs submitted to FLEX or ANY_NODES
				 * reservations can be scheduled on nodes
				 * outside of the reservation. If we allowed
				 * lower priority jobs to be scheduled on nodes
				 * not in this reservation, they could delay
				 * the higher priority job submitted to this
				 * reservation.
				 *
				 * We only remove nodes not in reservations,
				 * so lower priority jobs submitted to other
				 * reservations can still be scheduled.
				 *
				 * We don't mark the partition as being
				 * cleared. Once the first non-reservation job
				 * in the partition gets evaluated, which cannot
				 * be scheduled since non-reserved nodes have
				 * been removed, the partition's reserved nodes
				 * will be removed and it will be marked as
				 * cleared.
				 */
				bitstr_t *remove_nodes =
					bit_alloc(node_record_count);

				list_for_each(resv_list,
					      _get_nodes_in_reservations,
					      remove_nodes);
				bit_not(remove_nodes);
				bit_and(remove_nodes,
					job_ptr->part_ptr->node_bitmap);
				bit_and_not(avail_node_bitmap, remove_nodes);
				FREE_NULL_BITMAP(remove_nodes);
			} else {
				job_ptr->part_ptr->flags |=
					PART_FLAG_SCHED_CLEARED;
				bit_and_not(avail_node_bitmap,
					    job_ptr->part_ptr->node_bitmap);
			}
		}
	}

	if (bb_wait_cnt)
		(void) bb_g_job_try_stage_in();

	if (job_ptr)
		job_resv_clear_magnetic_flag(job_ptr);
	FREE_NULL_BITMAP(avail_node_bitmap);
	avail_node_bitmap = save_avail_node_bitmap;
	FREE_NULL_LIST(job_queue);

	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if ((slurmctld_config.server_thread_count >= 150) &&
	    (defer_rpc_cnt == 0)) {
		sched_info("%d pending RPCs at cycle end, consider configuring max_rpc_cnt",
			   slurmctld_config.server_thread_count);
	}
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
	unlock_slurmctld(job_write_lock);
	END_TIMER2(__func__);

	_do_diag_stats(DELTA_TIMER);

out:
	return job_cnt;
}

/*
 * sort_job_queue - sort job_queue in descending priority order
 * IN/OUT job_queue - sorted job queue
 */
extern void sort_job_queue(list_t *job_queue)
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
			    job_rec1->job_ptr->part_prio &&
			    job_rec1->job_ptr->part_prio->priority_array)
				p1 = job_rec1->priority;
			else
				p1 = job_rec1->job_ptr->priority;
		}
	} else {
		if (job_rec1->job_ptr->part_ptr_list &&
		    job_rec1->job_ptr->part_prio &&
		    job_rec1->job_ptr->part_prio->priority_array)
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
			    job_rec2->job_ptr->part_prio &&
			    job_rec2->job_ptr->part_prio->priority_array)
				p2 = job_rec2->priority;
			else
				p2 = job_rec2->job_ptr->priority;
		}
	} else {
		if (job_rec2->job_ptr->part_ptr_list &&
		    job_rec2->job_ptr->part_prio &&
		    job_rec2->job_ptr->part_prio->priority_array)
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

	if (job_rec1->use_prefer && !job_rec2->use_prefer)
		return -1;
	else if (!job_rec1->use_prefer && job_rec2->use_prefer)
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
	launch_msg_ptr->batch_uid_deprecated = job_ptr->user_id;
	launch_msg_ptr->batch_gid_deprecated = job_ptr->group_id;

	if (!(launch_msg_ptr->script_buf = get_job_script(job_ptr))) {
		fail_why = "Unable to load job batch script";
		goto job_failed;
	}

	/*
	 * We only want send the number of tasks if we explicitly requested
	 * them: num_tasks could be set (job_mgr.c
	 * _figure_out_num_tasks()). Otherwise a step requesting less than the
	 * allocation will be polluted with this calculated task count
	 * erroneously.
	 */
	if (job_ptr->bit_flags & JOB_NTASKS_SET)
		launch_msg_ptr->ntasks = job_ptr->details->num_tasks;
	launch_msg_ptr->alias_list = xstrdup(job_ptr->alias_list);
	launch_msg_ptr->container = xstrdup(job_ptr->container);
	launch_msg_ptr->cpu_freq_min = job_ptr->details->cpu_freq_min;
	launch_msg_ptr->cpu_freq_max = job_ptr->details->cpu_freq_max;
	launch_msg_ptr->cpu_freq_gov = job_ptr->details->cpu_freq_gov;
	launch_msg_ptr->nodes = xstrdup(job_ptr->nodes);
	launch_msg_ptr->overcommit = job_ptr->details->overcommit;
	launch_msg_ptr->open_mode  = job_ptr->details->open_mode;
	launch_msg_ptr->cpus_per_task = job_ptr->details->cpus_per_task;
	launch_msg_ptr->pn_min_memory = job_ptr->details->pn_min_memory;
	launch_msg_ptr->restart_cnt   = job_ptr->restart_cnt;
	launch_msg_ptr->profile       = job_ptr->profile;

	if (make_batch_job_cred(launch_msg_ptr, job_ptr, protocol_version)) {
		error("%s: slurm_cred_create failure for %pJ, holding job",
		      __func__, job_ptr);
		slurm_free_job_launch_msg(launch_msg_ptr);
		job_mgr_handle_cred_failure(job_ptr);
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

	if (job_ptr->bit_flags & STEPMGR_ENABLED) {
		env_array_overwrite(&launch_msg_ptr->environment,
				    "SLURM_STEPMGR", job_ptr->batch_host);
		/* Update envc if env was added to */
		launch_msg_ptr->envc =
			PTR_ARRAY_SIZE(launch_msg_ptr->environment) - 1;
	}

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

	launch_msg_ptr->account = xstrdup(job_ptr->account);
	if (job_ptr->qos_ptr)
		launch_msg_ptr->qos = xstrdup(job_ptr->qos_ptr->name);

	if (job_ptr->details->oom_kill_step != NO_VAL16)
		launch_msg_ptr->oom_kill_step = job_ptr->details->oom_kill_step;
	else
		launch_msg_ptr->oom_kill_step =
			slurm_conf.task_plugin_param & OOM_KILL_STEP;
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

static int _foreach_het_job_ready(void *x, void *arg)
{
	job_record_t *het_job = x;
	het_job_ready_t *ready_struct = arg;
	bool prolog = false;

	if (ready_struct->het_job_leader->het_job_id != het_job->het_job_id) {
		error("%s: Bad het_job_list for %pJ",
		      __func__, ready_struct->het_job_leader);
		return 0;
	}

	ready_struct->het_job = het_job;

	if (het_job->details)
		prolog = het_job->details->prolog_running;
	if (prolog || IS_JOB_CONFIGURING(het_job) ||
	    !test_job_nodes_ready(het_job)) {
		ready_struct->het_job_leader = NULL;
		return -1;
	}
	if (!ready_struct->job_ptr->batch_flag ||
	    (!IS_JOB_RUNNING(ready_struct->job_ptr) &&
	     !IS_JOB_SUSPENDED(ready_struct->job_ptr))) {
		ready_struct->het_job_leader = NULL;
		return -1;
	}

	ready_struct->het_job = NULL;
	return 0;
}

/* Validate the job is ready for launch
 * RET pointer to batch job to launch or NULL if not ready yet */
static job_record_t *_het_job_ready(job_record_t *job_ptr)
{
	het_job_ready_t ready_struct = { 0 };

	if (job_ptr->het_job_id == 0)	/* Not a hetjob */
		return job_ptr;
	ready_struct.het_job_leader = find_job_record(job_ptr->het_job_id);
	if (!ready_struct.het_job_leader) {
		error("Hetjob leader %pJ not found", job_ptr);
		return NULL;
	}
	if (!ready_struct.het_job_leader->het_job_list) {
		error("Hetjob leader %pJ lacks het_job_list", job_ptr);
		return NULL;
	}

	ready_struct.job_ptr = job_ptr;
	(void) list_for_each(ready_struct.het_job_leader->het_job_list,
			     _foreach_het_job_ready, &ready_struct);

	if (ready_struct.het_job_leader)
		log_flag(HETJOB, "Batch hetjob %pJ being launched",
			 ready_struct.het_job_leader);
	else if (ready_struct.het_job)
		log_flag(HETJOB, "Batch hetjob %pJ waiting for job to be ready",
			 ready_struct.het_job);

	return ready_struct.het_job_leader;
}

static void _set_job_env(job_record_t *job, batch_job_launch_msg_t *launch)
{
	if (job->name)
		env_array_overwrite(&launch->environment, "SLURM_JOB_NAME",
				    job->name);

	if (job->details->open_mode) {
		/* Propagate mode to spawned job using environment variable */
		if (job->details->open_mode == OPEN_MODE_APPEND)
			env_array_overwrite(&launch->environment,
					    "SLURM_OPEN_MODE", "a");
		else
			env_array_overwrite(&launch->environment,
					    "SLURM_OPEN_MODE", "t");
	}

	if (job->details->dependency)
		env_array_overwrite(&launch->environment,
				    "SLURM_JOB_DEPENDENCY",
				    job->details->dependency);

	/* intentionally skipping SLURM_EXPORT_ENV */

	if (job->profile) {
		char tmp[128] = {0};
		acct_gather_profile_to_string_r(job->profile, tmp);
		env_array_overwrite(&launch->environment, "SLURM_PROFILE", tmp);
	}

	if (job->details->acctg_freq)
		env_array_overwrite(&launch->environment, "SLURM_ACCTG_FREQ",
				    job->details->acctg_freq);

#ifdef HAVE_NATIVE_CRAY
	if (job->network)
		env_array_overwrite(&launch->environment, "SLURM_NETWORK",
				    job->network);
#endif

	if (job->details->cpu_freq_min || job->details->cpu_freq_max ||
	    job->details->cpu_freq_gov) {
		char *tmp = cpu_freq_to_cmdline(job->details->cpu_freq_min,
						job->details->cpu_freq_max,
						job->details->cpu_freq_gov);

		if (tmp)
			env_array_overwrite(&launch->environment,
					    "SLURM_CPU_FREQ_REQ", tmp);

		xfree(tmp);
	}

	/* update size of env in case it changed */
	if (launch->environment)
		launch->envc = PTR_ARRAY_SIZE(launch->environment) - 1;
}

static int _foreach_set_het_job_env(void *x, void *arg)
{
	job_record_t *het_job = x;
	het_job_env_t *het_job_env = arg;
	job_record_t *het_job_leader = het_job_env->het_job_leader;
	int het_job_offset = het_job_env->het_job_offset;
	batch_job_launch_msg_t *launch_msg_ptr = het_job_env->launch_msg_ptr;
	uint32_t num_cpus = 0;
	uint64_t tmp_mem = 0;
	char *tmp_str = NULL;

	if (het_job_leader->het_job_id != het_job->het_job_id) {
		error("%s: Bad het_job_list for %pJ",
		      __func__, het_job_leader);
		return 0;
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
		(void) env_array_overwrite_het_fmt(
			&launch_msg_ptr->environment,
			"SLURM_JOB_QOS",
			het_job_offset, "%s", het_job->qos_ptr->name);
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

	if (het_job->details && het_job->job_resrcs) {
		/* Both should always be set for active jobs */
		struct job_resources *resrcs_ptr = het_job->job_resrcs;
		slurm_step_layout_t *step_layout = NULL;
		uint16_t cpus_per_task_array[1];
		uint32_t cpus_task_reps[1], task_dist;
		uint16_t cpus_per_task = 1;
		slurm_step_layout_req_t step_layout_req = {
			.cpu_count_reps = resrcs_ptr->cpu_array_reps,
			.cpus_per_node = resrcs_ptr->cpu_array_value,
			.cpus_per_task = cpus_per_task_array,
			.cpus_task_reps = cpus_task_reps,
			.num_hosts = het_job->node_cnt,
			.plane_size = NO_VAL16,
		};

		cpus_task_reps[0] = het_job->node_cnt;

		for (int i = 0; i < resrcs_ptr->cpu_array_cnt; i++) {
			num_cpus += resrcs_ptr->cpu_array_value[i] *
				resrcs_ptr->cpu_array_reps[i];
		}

		if ((het_job->details->cpus_per_task > 0) &&
		    (het_job->details->cpus_per_task != NO_VAL16))
			cpus_per_task = het_job->details->cpus_per_task;

		cpus_per_task_array[0] = cpus_per_task;
		if (het_job->details->num_tasks) {
			step_layout_req.num_tasks =
				het_job->details->num_tasks;
		} else {
			step_layout_req.num_tasks = num_cpus /
				cpus_per_task;
		}

		if ((step_layout_req.node_list =
		     getenvp(launch_msg_ptr->environment,
			     "SLURM_ARBITRARY_NODELIST"))) {
			task_dist = SLURM_DIST_ARBITRARY;
		} else {
			step_layout_req.node_list = het_job->nodes;
			task_dist = SLURM_DIST_BLOCK;
		}
		step_layout_req.task_dist = task_dist;
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
	het_job_env->het_job_offset++;

	return 0;
}

/*
 * Set some hetjob environment variables. This will include information
 * about multiple job components (i.e. different slurmctld job records).
 */
static void _set_het_job_env(job_record_t *het_job_leader,
			     batch_job_launch_msg_t *launch_msg_ptr)
{
	int i;
	het_job_env_t het_job_env = {
		.het_job_leader = het_job_leader,
		.het_job_offset = 0,
		.launch_msg_ptr = launch_msg_ptr,
	};

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

	(void) list_for_each(het_job_leader->het_job_list,
			     _foreach_set_het_job_env,
			     &het_job_env);

	/* Continue support for old hetjob terminology. */
	(void) env_array_overwrite_fmt(&launch_msg_ptr->environment,
				       "SLURM_PACK_SIZE", "%d",
				       het_job_env.het_job_offset);
	(void) env_array_overwrite_fmt(&launch_msg_ptr->environment,
				       "SLURM_HET_SIZE", "%d",
				       het_job_env.het_job_offset);

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

	_set_job_env(launch_job_ptr, launch_msg_ptr);

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

	launch_msg_ptr->cred = slurm_cred_create(&cred_arg, false,
						 protocol_version);
	xfree(cred_arg.job_mem_alloc);
	xfree(cred_arg.job_mem_alloc_rep_count);

	if (launch_msg_ptr->cred)
		return SLURM_SUCCESS;
	error("slurm_cred_create failure for batch job %u",
	      cred_arg.step_id.job_id);
	return SLURM_ERROR;
}

static int _foreach_depend_list_copy(void *x, void *arg)
{
	depend_spec_t *dep_src = x;
	list_t **depend_list_dest = arg;
	depend_spec_t *dep_dest = xmalloc(sizeof(depend_spec_t));

	memcpy(dep_dest, dep_src, sizeof(depend_spec_t));
	list_append(*depend_list_dest, dep_dest);

	return 0;
}

/*
 * Copy a job's dependency list
 * IN depend_list_src - a job's depend_lst
 * RET copy of depend_list_src, must bee freed by caller
 */
extern list_t *depended_list_copy(list_t *depend_list_src)
{
	list_t *depend_list_dest = NULL;

	if (!depend_list_src)
		return depend_list_dest;

	depend_list_dest = list_create(xfree_ptr);
	(void) list_for_each(depend_list_src, _foreach_depend_list_copy,
			     &depend_list_dest);
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

static int _foreach_depend_list2str(void *x, void *arg)
{
	depend_spec_t *dep_ptr = x;
	depend_str_t *depend_str = arg;
	job_record_t *job_ptr = depend_str->job_ptr;

	/*
	 * Show non-fulfilled (including failed) dependencies, but don't
	 * show fulfilled dependencies.
	 */
	if (dep_ptr->depend_state == DEPEND_FULFILLED)
		return 0;
	if (dep_ptr->depend_type == SLURM_DEPEND_SINGLETON) {
		xstrfmtcat(job_ptr->details->dependency,
			   "%ssingleton(%s)",
			   depend_str->sep, _depend_state2str(dep_ptr));
	} else {
		char *dep_str = _depend_type2str(dep_ptr);

		if (dep_ptr->array_task_id == INFINITE)
			xstrfmtcat(job_ptr->details->dependency, "%s%s:%u_*",
				   depend_str->sep, dep_str, dep_ptr->job_id);
		else if (dep_ptr->array_task_id == NO_VAL)
			xstrfmtcat(job_ptr->details->dependency, "%s%s:%u",
				   depend_str->sep, dep_str, dep_ptr->job_id);
		else
			xstrfmtcat(job_ptr->details->dependency, "%s%s:%u_%u",
				   depend_str->sep, dep_str, dep_ptr->job_id,
				   dep_ptr->array_task_id);

		if (dep_ptr->depend_time)
			xstrfmtcat(job_ptr->details->dependency,
				   "+%u", dep_ptr->depend_time / 60);
		xstrfmtcat(job_ptr->details->dependency, "(%s)",
			   _depend_state2str(dep_ptr));
	}
	if (depend_str->set_or_flag)
		dep_ptr->depend_flags |= SLURM_FLAGS_OR;
	if (dep_ptr->depend_flags & SLURM_FLAGS_OR)
		depend_str->sep = "?";
	else
		depend_str->sep = ",";

	return 0;
}

static void _depend_list2str(job_record_t *job_ptr, bool set_or_flag)
{
	depend_str_t depend_str = {
		.job_ptr = job_ptr,
		.sep = "",
		.set_or_flag = set_or_flag,
	};

	if (job_ptr->details == NULL)
		return;

	xfree(job_ptr->details->dependency);

	if (job_ptr->details->depend_list == NULL
	    || list_count(job_ptr->details->depend_list) == 0)
		return;

	(void) list_for_each(job_ptr->details->depend_list,
			     _foreach_depend_list2str,
			     &depend_str);
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

static void _test_dependency_state(depend_spec_t *dep_ptr,
				   test_job_dep_t *test_job_dep)
{
	xassert(test_job_dep);

	test_job_dep->or_flag =
		(dep_ptr->depend_flags & SLURM_FLAGS_OR) ? true : false;

	if (test_job_dep->or_flag) {
		if (dep_ptr->depend_state == DEPEND_FULFILLED)
			test_job_dep->or_satisfied = true;
		else if (dep_ptr->depend_state == DEPEND_NOT_FULFILLED)
			test_job_dep->has_unfulfilled = true;
	} else { /* AND'd dependencies */
		if (dep_ptr->depend_state == DEPEND_FAILED)
			test_job_dep->and_failed = true;
		else if (dep_ptr->depend_state == DEPEND_NOT_FULFILLED)
			test_job_dep->has_unfulfilled = true;
	}
}

static int _foreach_test_job_dependency(void *x, void *arg)
{
	depend_spec_t *dep_ptr = x;
	test_job_dep_t *test_job_dep = arg;
	job_record_t *job_ptr = test_job_dep->job_ptr;
	job_record_t *djob_ptr;
	bool clear_dep = false, failure = false;
	bool remote = (dep_ptr->depend_flags & SLURM_FLAGS_REMOTE) ?
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
			test_job_dep->changed = true;
			dep_ptr->depend_state = DEPEND_FAILED;
		}
	}
	if ((dep_ptr->depend_state != DEPEND_NOT_FULFILLED) || remote) {
		_test_dependency_state(dep_ptr, test_job_dep);
		return 0;
	}

	/* Test local, unfulfilled dependency: */
	test_job_dep->has_local_depend = true;
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
	} else if (!djob_ptr || (djob_ptr->magic != JOB_MAGIC) ||
		   ((djob_ptr->job_id != dep_ptr->job_id) &&
		    (djob_ptr->array_job_id != dep_ptr->job_id))) {
		/* job is gone, dependency lifted */
		clear_dep = true;
	} else {
		bool is_complete, is_completed, is_pending;

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
		test_job_dep->changed = true;
		log_flag(DEPENDENCY, "%s: %pJ dependency %s:%u failed.",
			 __func__, job_ptr, _depend_type2str(dep_ptr),
			 dep_ptr->job_id);
	} else if (clear_dep) {
		dep_ptr->depend_state = DEPEND_FULFILLED;
		test_job_dep->changed = true;
		log_flag(DEPENDENCY, "%s: %pJ dependency %s:%u fulfilled.",
			 __func__, job_ptr, _depend_type2str(dep_ptr),
			 dep_ptr->job_id);
	}

	_test_dependency_state(dep_ptr, test_job_dep);

	return 0;
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
	test_job_dep_t test_job_dep = {
		.job_ptr = job_ptr,
	};
	int results = NO_DEPEND;

	if ((job_ptr->details == NULL) ||
	    (job_ptr->details->depend_list == NULL) ||
	    (list_count(job_ptr->details->depend_list) == 0)) {
		job_ptr->bit_flags &= ~JOB_DEPENDENT;
		if (was_changed)
			*was_changed = false;
		return NO_DEPEND;
	}

	(void) list_for_each(job_ptr->details->depend_list,
			     _foreach_test_job_dependency,
			     &test_job_dep);

	if (test_job_dep.or_satisfied &&
	    (job_ptr->state_reason == WAIT_DEP_INVALID)) {
		job_ptr->state_reason = WAIT_NO_REASON;
		xfree(job_ptr->state_desc);
		last_job_update = time(NULL);
	}

	if (test_job_dep.or_satisfied ||
	    (!test_job_dep.or_flag &&
	     !test_job_dep.and_failed &&
	     !test_job_dep.has_unfulfilled)) {
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
		if (test_job_dep.changed) {
			_depend_list2str(job_ptr, false);
			if (slurm_conf.debug_flags & DEBUG_FLAG_DEPENDENCY)
				print_job_dependency(job_ptr, __func__);
		}
		job_ptr->bit_flags |= JOB_DEPENDENT;
		acct_policy_remove_accrue_time(job_ptr, false);
		if (test_job_dep.and_failed ||
		    (test_job_dep.or_flag && !test_job_dep.has_unfulfilled))
			/* Dependency failed */
			results = FAIL_DEPEND;
		else
			/* Still dependent */
			results = test_job_dep.has_local_depend ? LOCAL_DEPEND :
				REMOTE_DEPEND;
	}

	if (was_changed)
		*was_changed = test_job_dep.changed;
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
				FREE_NULL_BITMAP(array_bitmap);
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
			FREE_NULL_BITMAP(array_bitmap);
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
static void _add_dependency_to_list(list_t *depend_list,
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
					list_t *new_depend_list, char *sep_ptr,
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

		dep_job_ptr = _find_dependent_job_ptr(job_id, &array_task_id);

		if (!dep_job_ptr && fed_mgr_is_origin_job_id(job_id) &&
		    ((depend_type == SLURM_DEPEND_AFTER_OK) ||
		     (depend_type == SLURM_DEPEND_AFTER_NOT_OK))) {
			/*
			 * Reject the job since we won't be able to check if
			 * job dependency was fulfilled or not.
			 */
			*rc = ESLURM_DEPENDENCY;
			break;
		}

		/*
		 * _find_dependent_job_ptr() may modify array_task_id, so check
		 * if the job is the same after that.
		 */
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
			 * Expand only jobs in the same QOS and partition
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
			job_details_t *detail_ptr = job_ptr->details;
			multi_core_data_t *mc_ptr = detail_ptr->mc_ptr;
			gres_job_state_validate_t gres_js_val = {
				.cpus_per_task =
				&detail_ptr->orig_cpus_per_task,
				.max_nodes = &detail_ptr->max_nodes,
				.min_cpus = &detail_ptr->min_cpus,
				.min_nodes = &detail_ptr->min_nodes,
				.ntasks_per_node = &detail_ptr->ntasks_per_node,
				.ntasks_per_socket = &mc_ptr->ntasks_per_socket,
				.ntasks_per_tres = &detail_ptr->ntasks_per_tres,
				.num_tasks = &detail_ptr->num_tasks,
				.sockets_per_node = &mc_ptr->sockets_per_node,

				.gres_list = &job_ptr->gres_list_req,
			};

			job_ptr->details->expanding_jobid = job_id;
			if (select_hetero == 0) {
				/*
				 * GRES per node of this job must match
				 * the job being expanded. Other options
				 * are ignored.
				 */
				_copy_tres_opts(job_ptr, dep_job_ptr);
			}

			gres_js_val.cpus_per_tres = job_ptr->cpus_per_tres;
			gres_js_val.mem_per_tres = job_ptr->mem_per_tres;
			gres_js_val.tres_freq = job_ptr->tres_freq;
			gres_js_val.tres_per_job = job_ptr->tres_per_job;
			gres_js_val.tres_per_node = job_ptr->tres_per_node;
			gres_js_val.tres_per_socket = job_ptr->tres_per_socket;
			gres_js_val.tres_per_task = job_ptr->tres_per_task;

			FREE_NULL_LIST(job_ptr->gres_list_req);
			(void) gres_job_state_validate(&gres_js_val);
			assoc_mgr_lock(&locks);
			gres_stepmgr_set_job_tres_cnt(
				job_ptr->gres_list_req,
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
					list_t *new_depend_list, char **sep_ptr,
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

static int _foreach_update_job_depenency_list(void *x, void *arg)
{
	depend_spec_t *dep_ptr = x, *job_depend;
	test_job_dep_t *test_job_dep = arg;
	job_record_t *job_ptr = test_job_dep->job_ptr;

	/*
	 * If the dependency is marked as remote, then it wasn't updated
	 * by the sibling cluster. Skip it.
	 */
	if (dep_ptr->depend_flags & SLURM_FLAGS_REMOTE)
		return 0;

	/*
	 * Find the dependency in job_ptr that matches this one.
	 * Then update job_ptr's dependency state (not fulfilled,
	 * fulfilled, or failed) to match this one.
	 */
	job_depend = list_find_first(job_ptr->details->depend_list,
				     _find_dependency,
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
		return 0;
	}

	/*
	 * If the dependency is already fulfilled, don't update it.
	 * Otherwise update the dependency state.
	 */
	if ((job_depend->depend_state == DEPEND_FULFILLED) ||
	    (job_depend->depend_state == dep_ptr->depend_state))
		return 0;
	if (job_depend->depend_type == SLURM_DEPEND_SINGLETON) {
		/*
		 * We need to update the singleton dependency with
		 * the cluster bit, but test_job_dependency() will test
		 * if it is fulfilled, so don't change the depend_state
		 * here.
		 */
		job_depend->singleton_bits |= dep_ptr->singleton_bits;
		if (!fed_mgr_is_singleton_satisfied(job_ptr, job_depend,
						    false))
			return 0;
	}
	job_depend->depend_state = dep_ptr->depend_state;
	test_job_dep->changed = true;

	return 0;
}

extern bool update_job_dependency_list(job_record_t *job_ptr,
				       list_t *new_depend_list)
{
	test_job_dep_t test_job_dep = {
		.job_ptr = job_ptr,
	};

	xassert(job_ptr);
	xassert(job_ptr->details);
	xassert(job_ptr->details->depend_list);

	(void) list_for_each(new_depend_list,
			     _foreach_update_job_depenency_list,
			     &test_job_dep);

	return test_job_dep.changed;
}

static int _foreach_handle_job_dependency_updates(void *x, void *arg)
{
	depend_spec_t *dep_ptr = x;
	test_job_dep_t *test_job_dep = arg;

	_test_dependency_state(dep_ptr, test_job_dep);

	return 0;
}

extern int handle_job_dependency_updates(void *object, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) object;
	time_t now = time(NULL);
	test_job_dep_t test_job_dep = {
		.job_ptr = job_ptr,
	};
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
	(void) list_for_each(job_ptr->details->depend_list,
			     _foreach_handle_job_dependency_updates,
			     &test_job_dep);

	if (test_job_dep.or_satisfied ||
	    (!test_job_dep.or_flag &&
	     !test_job_dep.and_failed &&
	     !test_job_dep.has_unfulfilled)) {
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
		if (test_job_dep.and_failed ||
		    (test_job_dep.or_flag && !test_job_dep.has_unfulfilled)) {
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
	list_t *new_depend_list = NULL;
	depend_spec_t *dep_ptr;
	bool or_flag = false;

	if (job_ptr->details == NULL)
		return EINVAL;

	if (select_hetero == -1) {
		/*
		 * Determine if the select plugin supports heterogeneous
		 * GRES allocations (count differ by node): 1=yes, 0=no
		 */
		if (xstrstr(slurm_conf.select_type, "cons_tres"))
			select_hetero = 1;
		else
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

static int _foreach_scan_depend(void *x, void *arg)
{
	depend_spec_t *dep_ptr = x;
	test_job_dep_t *test_job_dep = arg;
	job_record_t *job_ptr = test_job_dep->job_ptr;

	if (dep_ptr->job_id == 0)	/* Singleton */
		return 0;
	/*
	 * We can't test for circular dependencies if the job_ptr
	 * wasn't found - the job may not be on this cluster, or the
	 * job was already purged when the dependency submitted,
	 * or the job just didn't exist.
	 */
	if (!dep_ptr->job_ptr)
		return 0;
	if ((test_job_dep->changed = _depends_on_same_job(
		     job_ptr, dep_ptr->job_ptr,
		     dep_ptr->job_id,
		     dep_ptr->array_task_id)))
		return -1;
	else if (dep_ptr->job_ptr->magic != JOB_MAGIC)
		return 0;	/* purged job, ptr not yet cleared */
	else if (!IS_JOB_FINISHED(dep_ptr->job_ptr) &&
		 dep_ptr->job_ptr->details &&
		 dep_ptr->job_ptr->details->depend_list) {
		test_job_dep->changed = _scan_depend(
			dep_ptr->job_ptr->details->depend_list,
			job_ptr);
		if (test_job_dep->changed) {
			info("circular dependency: %pJ is dependent upon %pJ",
			     dep_ptr->job_ptr, job_ptr);
			return -1;
		}
	}
	return 0;
}

/* Return true if the job job_ptr is found in dependency_list.
 * Pass NULL dependency list to clear the counter.
 * Execute recursively for each dependent job */
static bool _scan_depend(list_t *dependency_list, job_record_t *job_ptr)
{
	static int job_counter = 0;
	test_job_dep_t test_job_dep = {
		.job_ptr = job_ptr,
	};

	if (dependency_list == NULL) {
		job_counter = 0;
		return false;
	} else if (job_counter++ >= max_depend_depth) {
		return false;
	}

	xassert(job_ptr);

	(void) list_for_each(dependency_list,
			     _foreach_scan_depend,
			     &test_job_dep);

	return test_job_dep.changed;
}

static int _foreach_delayed_job_start_time(void *x, void *arg)
{
	job_record_t *job_q_ptr = x;
	delay_start_t *delay_start = arg;
	job_record_t *job_ptr = delay_start->job_ptr;
	uint32_t job_size_cpus, job_size_nodes, job_time;

	if (!IS_JOB_PENDING(job_q_ptr) || !job_q_ptr->details ||
	    (job_q_ptr->part_ptr != job_ptr->part_ptr) ||
	    (job_q_ptr->priority < job_ptr->priority) ||
	    (job_q_ptr->job_id == job_ptr->job_id) ||
	    (IS_JOB_REVOKED(job_q_ptr)))
		return 0;

	if (job_q_ptr->details->min_nodes == NO_VAL)
		job_size_nodes = 1;
	else
		job_size_nodes = job_q_ptr->details->min_nodes;
	if (job_q_ptr->details->min_cpus == NO_VAL)
		job_size_cpus = 1;
	else
		job_size_cpus = job_q_ptr->details->min_cpus;
	job_size_cpus = MAX(job_size_cpus,
			    (job_size_nodes * delay_start->part_cpus_per_node));
	if (job_q_ptr->time_limit == NO_VAL)
		job_time = job_q_ptr->part_ptr->max_time;
	else
		job_time = job_q_ptr->time_limit;
	delay_start->cume_space_time += job_size_cpus * job_time;

	return 0;
}

/* If there are higher priority queued jobs in this job's partition, then
 * delay the job's expected initiation time as needed to run those jobs.
 * NOTE: This is only a rough estimate of the job's start time as it ignores
 * job dependencies, feature requirements, specific node requirements, etc. */
static void _delayed_job_start_time(job_record_t *job_ptr)
{
	uint32_t part_node_cnt, part_cpu_cnt;
	delay_start_t delay_start = {
		.job_ptr = job_ptr,
		.part_cpus_per_node = 1,
	};

	if (job_ptr->part_ptr == NULL)
		return;
	part_node_cnt = job_ptr->part_ptr->total_nodes;
	part_cpu_cnt  = job_ptr->part_ptr->total_cpus;
	if (part_cpu_cnt > part_node_cnt)
		delay_start.part_cpus_per_node = part_cpu_cnt / part_node_cnt;

	(void) list_for_each(job_list,
			     _foreach_delayed_job_start_time,
			     &delay_start);
	delay_start.cume_space_time /= part_cpu_cnt;/* Factor out size */
	delay_start.cume_space_time *= 60;		/* Minutes to seconds */
	debug2("Increasing estimated start of %pJ by %"PRIu64" secs",
	       job_ptr, delay_start.cume_space_time);
	job_ptr->start_time += delay_start.cume_space_time;
}

static int _foreach_add_to_preemptee_job_id(void *x, void *arg)
{
	job_record_t *job_ptr = x;
	will_run_response_msg_t *resp_data = arg;
	uint32_t *preemptee_jid = xmalloc(sizeof(uint32_t));

	(*preemptee_jid) = job_ptr->job_id;

	if (!resp_data->preemptee_job_id)
		resp_data->preemptee_job_id = list_create(xfree_ptr);

	list_append(resp_data->preemptee_job_id, preemptee_jid);

	return 0;
}

static int _foreach_job_start_data_part(void *x, void *arg)
{
	part_record_t *part_ptr = x;
	job_start_data_t *job_start_data = arg;
	job_record_t *job_ptr = job_start_data->job_ptr;

	bitstr_t *active_bitmap = NULL, *avail_bitmap = NULL;
	bitstr_t *resv_bitmap = NULL;
	uint32_t min_nodes, max_nodes, req_nodes;
	int rc2 = SLURM_SUCCESS;
	time_t start_res, orig_start_time = (time_t) 0;
	list_t *preemptee_candidates = NULL, *preemptee_job_list = NULL;
	bool resv_overlap = false;
	resv_exc_t resv_exc = { 0 };

	job_start_data->rc = SLURM_SUCCESS;
	if (!part_ptr) {
		job_start_data->rc = ESLURM_INVALID_PARTITION_NAME;
		return -1;
	}

	if (job_ptr->details->req_nodes && job_ptr->details->req_nodes[0]) {
		if (node_name2bitmap(job_ptr->details->req_nodes, false,
				     &avail_bitmap, NULL)) {
			job_start_data->rc = ESLURM_INVALID_NODE_NAME;
			return -1;
		}
	} else {
		/* assume all nodes available to job for testing */
		avail_bitmap = node_conf_get_active_bitmap();
	}

	/* Consider only nodes in this job's partition */
	if (part_ptr->node_bitmap)
		bit_and(avail_bitmap, part_ptr->node_bitmap);
	else
		job_start_data->rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (job_req_node_filter(job_ptr, avail_bitmap, true))
		job_start_data->rc = ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
	if (job_ptr->details->exc_node_bitmap) {
		bit_and_not(avail_bitmap, job_ptr->details->exc_node_bitmap);
	}
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   avail_bitmap)) {
			job_start_data->rc =
				ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE;
		}
	}

	/* Enforce reservation: access control, time and nodes */
	if (job_ptr->details->begin_time &&
	    (job_ptr->details->begin_time > job_start_data->now))
		start_res = job_ptr->details->begin_time;
	else
		start_res = job_start_data->now;

	rc2 = job_test_resv(job_ptr, &start_res, true, &resv_bitmap,
			    &resv_exc, &resv_overlap, false);
	if (rc2 != SLURM_SUCCESS) {
		FREE_NULL_BITMAP(avail_bitmap);
		reservation_delete_resv_exc_parts(&resv_exc);
		job_start_data->rc = rc2;
		return -1;
	}

	bit_and(avail_bitmap, resv_bitmap);
	FREE_NULL_BITMAP(resv_bitmap);

	/* Only consider nodes that are not DOWN or DRAINED */
	bit_and(avail_bitmap, avail_node_bitmap);

	if (job_start_data->rc == SLURM_SUCCESS) {
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
			job_start_data->rc = select_g_job_test(
				job_ptr, active_bitmap,
				min_nodes, max_nodes, req_nodes,
				SELECT_MODE_WILL_RUN,
				preemptee_candidates,
				&preemptee_job_list,
				&resv_exc,
				NULL);
			if (job_start_data->rc == SLURM_SUCCESS) {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = active_bitmap;
				active_bitmap = NULL;
				test_fini = 1;
			} else {
				FREE_NULL_BITMAP(active_bitmap);
				save_share_res  = job_ptr->details->share_res;
				save_whole_node = job_ptr->details->whole_node;
				job_ptr->details->share_res = 0;
				job_ptr->details->whole_node |=
					WHOLE_NODE_REQUIRED;
				test_fini = 0;
			}
		}
		if (test_fini != 1) {
			job_start_data->rc = select_g_job_test(
				job_ptr, avail_bitmap,
				min_nodes, max_nodes, req_nodes,
				SELECT_MODE_WILL_RUN,
				preemptee_candidates,
				&preemptee_job_list,
				&resv_exc,
				NULL);
			if (test_fini == 0) {
				job_ptr->details->share_res = save_share_res;
				job_ptr->details->whole_node = save_whole_node;
			}
		}
	}

	if (job_start_data->rc == SLURM_SUCCESS) {
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

		if (preemptee_job_list)
			(void) list_for_each(preemptee_job_list,
					     _foreach_add_to_preemptee_job_id,
					     resp_data);

		resp_data->sys_usage_per = _get_system_usage();

		*job_start_data->resp = resp_data;
	} else {
		job_start_data->rc = ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE;
	}

	FREE_NULL_LIST(preemptee_candidates);
	FREE_NULL_LIST(preemptee_job_list);
	FREE_NULL_BITMAP(avail_bitmap);
	reservation_delete_resv_exc_parts(&resv_exc);

	if (job_start_data->rc)
		return 0;

	return -1;
}

/*
 * Determine if a pending job will run using only the specified nodes, build
 * response message and return SLURM_SUCCESS on success. Otherwise return an
 * error code. Caller must free response message.
 */
extern int job_start_data(job_record_t *job_ptr,
			  will_run_response_msg_t **resp)
{
	job_start_data_t job_start_data = {
		.job_ptr = job_ptr,
		.now = time(NULL),
		.resp = resp,
	};

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

	if (job_ptr->part_ptr_list)
		(void) list_for_each(job_ptr->part_ptr_list,
				     _foreach_job_start_data_part,
				     &job_start_data);
	else
		(void) _foreach_job_start_data_part(job_ptr->part_ptr,
						    &job_start_data);

	return job_start_data.rc;
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
 * IN/OUT reboot_features - features that should be applied to the node on
 *                          reboot. Caller must xfree().
 * RET bitmap of nodes requiring a reboot for NodeFeaturesPlugin or NULL if none
 */
extern bitstr_t *node_features_reboot(job_record_t *job_ptr,
				      char **reboot_features)
{
	bitstr_t *active_bitmap = NULL, *boot_node_bitmap = NULL;
	bitstr_t *feature_node_bitmap, *tmp_bitmap;

	xassert(reboot_features);
	xassert(!(*reboot_features)); /* It needs to start out NULL */

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
	FREE_NULL_BITMAP(active_bitmap);

	/*
	 * If some MOR/XAND option, filter out only first set of features
	 * for NodeFeaturesPlugin
	 */
	feature_node_bitmap = node_features_g_get_node_bitmap();
	if (feature_node_bitmap == NULL) /* No nodes under NodeFeaturesPlugin */
		return NULL;

	*reboot_features = node_features_g_job_xlate(
		job_ptr->details->features_use,
		job_ptr->details->feature_list_use,
		job_ptr->node_bitmap);
	tmp_bitmap = build_active_feature_bitmap2(*reboot_features);
	boot_node_bitmap = bit_copy(job_ptr->node_bitmap);
	bit_and(boot_node_bitmap, feature_node_bitmap);
	FREE_NULL_BITMAP(feature_node_bitmap);
	if (tmp_bitmap) {
		bit_and_not(boot_node_bitmap, tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);
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
	hostlist_t *hostlist;

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
	xassert(node_bitmap);

	if (bit_ffs(node_bitmap) == -1)
		return;

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
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		char *tmp_feature;

		tmp_feature = node_features_g_node_xlate(reboot_features,
							 node_ptr->features_act,
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
	node_record_t *node_ptr;
	time_t now = time(NULL);
	bitstr_t *boot_node_bitmap = NULL, *feature_node_bitmap = NULL;
	bitstr_t *non_feature_node_bitmap = NULL;
	char *reboot_features = NULL;
	uint16_t protocol_version = SLURM_PROTOCOL_VERSION;
	static bool power_save_on = false;
	static time_t sched_update = 0;
	static bool logged = false;

	if (sched_update != slurm_conf.last_update) {
		power_save_on = power_save_test();
		sched_update = slurm_conf.last_update;
	}

	if ((job_ptr->details == NULL) || (job_ptr->node_bitmap == NULL))
		return;

	if (job_ptr->reboot)
		boot_node_bitmap = bit_copy(job_ptr->node_bitmap);
	else
		boot_node_bitmap = node_features_reboot(job_ptr,
							&reboot_features);

	if (!logged && boot_node_bitmap &&
	    (!power_save_on &&
	     ((slurm_conf.reboot_program == NULL) ||
	      (slurm_conf.reboot_program[0] == '\0')))) {
		info("%s: Preparing node reboot without power saving and RebootProgram",
		     __func__);
		logged = true;
	}

	if (boot_node_bitmap &&
	    job_ptr->details->features_use &&
	    node_features_g_user_update(job_ptr->user_id)) {
		non_feature_node_bitmap = bit_copy(boot_node_bitmap);
		/*
		 * node_features_g_job_xlate is called from
		 * node_features_reboot, which we may have already called.
		 * Avoid calling node_features_g_job_xlate twice.
		 */
		if (!reboot_features) {
			reboot_features = node_features_g_job_xlate(
				job_ptr->details->features_use,
				job_ptr->details->feature_list_use,
				job_ptr->node_bitmap);
		}
		if (reboot_features)
			feature_node_bitmap = node_features_g_get_node_bitmap();
		if (feature_node_bitmap)
			bit_and(feature_node_bitmap, non_feature_node_bitmap);
		if (!feature_node_bitmap ||
		    (bit_ffs(feature_node_bitmap) == -1)) {
			/* No KNL nodes to reboot */
			FREE_NULL_BITMAP(feature_node_bitmap);
		} else {
			bit_and_not(non_feature_node_bitmap,
				    feature_node_bitmap);
			if (bit_ffs(non_feature_node_bitmap) == -1) {
				/* No non-KNL nodes to reboot */
				FREE_NULL_BITMAP(non_feature_node_bitmap);
			}
		}
	}

	if (feature_node_bitmap) {
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

	/*
	 * Assume the power save thread will handle the boot if any of the nodes
	 * are cloud nodes. In KNL/features, the node is being rebooted and not
	 * brought up from being powered down.
	 */
	if ((boot_node_bitmap == NULL) ||
	    bit_overlap_any(cloud_node_bitmap, job_ptr->node_bitmap)) {
		/* launch_job() when all nodes have booted */
		if (bit_overlap_any(power_down_node_bitmap,
		                    job_ptr->node_bitmap) ||
		    bit_overlap_any(booting_node_bitmap,
				    job_ptr->node_bitmap)) {
			/* Reset job start time when nodes are booted */
			job_state_set_flag(job_ptr, (JOB_CONFIGURING |
						     JOB_POWER_UP_NODE));
			job_ptr->wait_all_nodes = 1;
		}

		goto cleanup;
	}

	/* Reset job start time when nodes are booted */
	job_state_set_flag(job_ptr, (JOB_CONFIGURING | JOB_POWER_UP_NODE));
	/* launch_job() when all nodes have booted */
	job_ptr->wait_all_nodes = 1;

	/* Modify state information for all nodes, KNL and others */
	for (int i = 0; (node_ptr = next_node_bitmap(boot_node_bitmap, &i));
	     i++) {
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
		bit_clear(power_down_node_bitmap, i);
		bit_set(booting_node_bitmap, i);
		node_ptr->boot_req_time = now;
	}

	if (feature_node_bitmap) {
		/* Reboot nodes to change KNL NUMA and/or MCDRAM mode */
		_do_reboot(power_save_on, feature_node_bitmap, job_ptr,
			   reboot_features, protocol_version);
		bit_and_not(boot_node_bitmap, feature_node_bitmap);
	}

	if (non_feature_node_bitmap) {
		/* Reboot nodes with no feature changes */
		_do_reboot(power_save_on, non_feature_node_bitmap, job_ptr,
			   NULL, protocol_version);
		bit_and_not(boot_node_bitmap, non_feature_node_bitmap);
	}

	if (job_ptr->reboot) {
		/* Reboot the remaining nodes blindly as per direct request */
		_do_reboot(power_save_on, boot_node_bitmap, job_ptr, NULL,
			   protocol_version);
	}

cleanup:
	xfree(reboot_features);
	FREE_NULL_BITMAP(boot_node_bitmap);
	FREE_NULL_BITMAP(non_feature_node_bitmap);
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
	job_state_set_flag(job_ptr, JOB_CONFIGURING);

	job_id = xmalloc(sizeof(*job_id));
	*job_id = job_ptr->job_id;
	slurm_thread_create_detached(_start_prolog_slurmctld_thread, job_id);
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

static int _foreach_feature_list_copy(void *x, void *arg)
{
	job_feature_t *feat_src = x, *feat_dest;
	list_t **feature_list_dest = arg;

	feat_dest = xmalloc(sizeof(job_feature_t));
	memcpy(feat_dest, feat_src, sizeof(job_feature_t));
	if (feat_src->node_bitmap_active)
		feat_dest->node_bitmap_active =
			bit_copy(feat_src->node_bitmap_active);
	if (feat_src->node_bitmap_avail)
		feat_dest->node_bitmap_avail =
			bit_copy(feat_src->node_bitmap_avail);
	feat_dest->name = xstrdup(feat_src->name);
	list_append(*feature_list_dest, feat_dest);

	return 0;
}

/*
 * Copy a job's feature list
 * IN feature_list_src - a job's depend_lst
 * RET copy of feature_list_src, must be freed by caller
 */
extern list_t *feature_list_copy(list_t *feature_list_src)
{
	list_t *feature_list_dest = NULL;

	if (!feature_list_src)
		return feature_list_dest;

	feature_list_dest = list_create(feature_list_delete);
	(void) list_for_each(feature_list_src,
			     _foreach_feature_list_copy,
			     &feature_list_dest);

	return feature_list_dest;
}

/*
 * IN/OUT convert_to_matching_or -
 * If at least one changeable feature is requested, then all the nodes
 * in the job allocation need to match the same feature set.
 *
 * As an input: if true, then mark all '|' operators as matching OR, and also
 * imply that it is surrounded by brackets by setting bracket=1 for all the
 * features except the last one. The AND operators are still treated as normal
 * AND (not XAND), as if they were surrounded by parentheses within the
 * brackets.
 *
 * As an output: if mutiple changeable features are requested,
 * and bar (OR) was requested, then set this to true.
 *
 * This is needed for the scheduling logic with parentheses and matching OR.
 */
static int _feature_string2list(char *features, char *debug_str,
				list_t **feature_list,
				bool *convert_to_matching_or)
{
	int rc = SLURM_SUCCESS;
	int bracket = 0, count = 0, i, paren = 0;
	int brack_set_count = 0;
	char *tmp_requested;
	char *str_ptr, *feature = NULL;
	bool has_changeable = false;
	bool has_or = false;
	bool has_asterisk = false;

	xassert(feature_list);

	/* Use of commas separator is a common error. Replace them with '&' */
	while ((str_ptr = strstr(features, ",")))
		str_ptr[0] = '&';

	tmp_requested = xstrdup(features);
	*feature_list = list_create(feature_list_delete);

	for (i = 0; ; i++) {
		job_feature_t *feat;

		if (tmp_requested[i] == '*') {
			tmp_requested[i] = '\0';
			count = strtol(&tmp_requested[i+1], &str_ptr, 10);
			if (!bracket)
				has_asterisk = true;
			if ((feature == NULL) || (count <= 0) || (paren != 0)) {
				verbose("%s constraint invalid, '*' must be requested with a positive integer, and after a feature or parentheses: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
			i = str_ptr - tmp_requested - 1;
		} else if (tmp_requested[i] == '&') {
			tmp_requested[i] = '\0';
			if (feature == NULL) {
				verbose("%s constraint requested '&' without a feature: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
			feat = xmalloc(sizeof(job_feature_t));
			feat->bracket = *convert_to_matching_or ? 1 : bracket;
			feat->name = xstrdup(feature);
			feat->changeable = node_features_g_changeable_feature(
				feature);
			feat->count = count;
			feat->paren = paren;

			has_changeable |= feat->changeable;

			if (paren || *convert_to_matching_or)
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
				verbose("%s constraint requested '|' without a feature: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
			changeable = node_features_g_changeable_feature(
				feature);
			feat = xmalloc(sizeof(job_feature_t));
			feat->bracket = *convert_to_matching_or ? 1 : bracket;
			feat->name = xstrdup(feature);
			feat->changeable = changeable;
			feat->count = count;
			feat->paren = paren;

			has_changeable |= changeable;
			has_or = true;

			/*
			 * The if-else-if is like this for priority:
			 * - paren is highest priority
			 * - then bracket
			 * - then outside of paren/bracket
			 */
			if (paren && !(*convert_to_matching_or))
				feat->op_code = FEATURE_OP_OR;
			else if (bracket || changeable ||
				 (*convert_to_matching_or))
				feat->op_code = FEATURE_OP_MOR;
			else
				feat->op_code = FEATURE_OP_OR;
			list_append(*feature_list, feat);
			feature = NULL;
			count = 0;
		} else if (tmp_requested[i] == '[') {
			tmp_requested[i] = '\0';
			if ((feature != NULL) || bracket || paren) {
				verbose("%s constraint has imbalanced brackets: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
			bracket++;
			brack_set_count++;
			if (brack_set_count > 1) {
				verbose("%s constraint has more than one set of brackets: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
		} else if (tmp_requested[i] == ']') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (bracket == 0) || paren) {
				verbose("%s constraint has imbalanced brackets: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
			bracket--;
		} else if (tmp_requested[i] == '(') {
			tmp_requested[i] = '\0';
			if ((feature != NULL) || paren) {
				verbose("%s constraint has imbalanced parentheses: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
			paren++;
		} else if (tmp_requested[i] == ')') {
			tmp_requested[i] = '\0';
			if ((feature == NULL) || (paren == 0)) {
				verbose("%s constraint has imbalanced parentheses: %s",
					debug_str, features);
				rc = ESLURM_INVALID_FEATURE;
				goto fini;
			}
			paren--;
		} else if (tmp_requested[i] == '\0') {
			if (feature) {
				feat = xmalloc(sizeof(job_feature_t));
				feat->bracket = bracket;
				feat->name = xstrdup(feature);
				feat->changeable = node_features_g_changeable_feature(
					feature);
				feat->count = count;
				feat->paren = paren;
				feat->op_code = FEATURE_OP_END;
				list_append(*feature_list, feat);

				has_changeable |= feat->changeable;
			}
			break;
		} else if (feature == NULL) {
			feature = &tmp_requested[i];
		} else if (i && (tmp_requested[i - 1] == '\0')) {
			/* ')' and ']' should be followed by a token. */
			verbose("%s constraint has an unexpected character: %s",
				debug_str, features);
			rc = ESLURM_INVALID_FEATURE;
			goto fini;
		}
	}

	if (bracket != 0) {
		verbose("%s constraint has unbalanced brackets: %s",
			debug_str, features);
		rc = ESLURM_INVALID_FEATURE;
		goto fini;
	}
	if (paren != 0) {
		verbose("%s constraint has unbalanced parenthesis: %s",
			debug_str, features);
		rc = ESLURM_INVALID_FEATURE;
		goto fini;
	}
	if (has_asterisk && (list_count(*feature_list) > 1)) {
		verbose("%s constraint has '*' outside of brackets with more than one feature: %s",
			debug_str, features);
		rc = ESLURM_INVALID_FEATURE;
		goto fini;
	}

	*convert_to_matching_or = (has_changeable && has_or);

fini:
	if (rc != SLURM_SUCCESS) {
		FREE_NULL_LIST(*feature_list);
		info("%s invalid constraint: %s",
		     debug_str, features);
	}
	xfree(tmp_requested);

	return rc;
}

/*
 * build_feature_list - Translate a job's feature string into a feature_list
 * NOTE: This function is also used for reservations if is_reservation is true
 * and for job_desc_msg_t if job_id == 0
 * IN  details->features
 * OUT details->feature_list
 * RET error code
 */
extern int build_feature_list(job_record_t *job_ptr, bool prefer,
			      bool is_reservation)
{
	job_details_t *detail_ptr = job_ptr->details;
	list_t **feature_list;
	int rc;
	int feature_err;
	bool convert_to_matching_or = false;
	valid_feature_t valid_feature = {
		.rc = SLURM_SUCCESS,
	};

	/* no hard constraints */
	if (!detail_ptr || (!detail_ptr->features && !detail_ptr->prefer)) {
		if (job_ptr->batch_features)
			return ESLURM_BATCH_CONSTRAINT;
		return SLURM_SUCCESS;
	}

	if (prefer) {
		valid_feature.features = detail_ptr->prefer;
		feature_list = &detail_ptr->prefer_list;
		feature_err = ESLURM_INVALID_PREFER;
	} else {
		valid_feature.features = detail_ptr->features;
		feature_list = &detail_ptr->feature_list;
		feature_err = ESLURM_INVALID_FEATURE;
	}

	if (!valid_feature.features) /* The other constraint is non NULL. */
		return SLURM_SUCCESS;

	if (*feature_list)		/* already processed */
		return SLURM_SUCCESS;

	if (is_reservation)
		valid_feature.debug_str = xstrdup("Reservation");
	else if (!job_ptr->job_id)
		valid_feature.debug_str = xstrdup("Job specs");
	else
		valid_feature.debug_str =
			xstrdup_printf("JobId=%u", job_ptr->job_id);

	valid_feature.can_reboot =
		node_features_g_user_update(job_ptr->user_id);
	rc = _feature_string2list(valid_feature.features,
				  valid_feature.debug_str,
				  feature_list, &convert_to_matching_or);
	if (rc != SLURM_SUCCESS) {
		rc = feature_err;
		goto fini;
	}

	if (convert_to_matching_or) {
		char *str = NULL;
		list_t *feature_sets;

		/*
		 * Restructure the list into a format of AND'ing features in
		 * parentheses and matching OR each parentheses together. The
		 * current scheduling logic does not know how to handle matching
		 * OR inside of parentheses; however, it does know how to handle
		 * matching OR outside of parentheses, so we restructure the
		 * feature list to a format the scheduling logic understands.
		 * This is needed for changeable features which need all nodes
		 * in the job allocation to match the same feature set, so they
		 * cannot have any boolean OR in the feature list.
		 *
		 * For example, "(a|b)&c" becomes "(a&c)|(b&c)"
		 *
		 * Restructure only the feature list; leave the original
		 * constraint expression intact.
		 */
		feature_sets = job_features_list2feature_sets(
			valid_feature.features,
			*feature_list,
			false);
		list_for_each(feature_sets, job_features_set2str, &str);
		FREE_NULL_LIST(feature_sets);
		FREE_NULL_LIST(*feature_list);
		rc = _feature_string2list(str, valid_feature.debug_str,
					  feature_list,
					  &convert_to_matching_or);
		if (rc != SLURM_SUCCESS) {
			/*
			 * Something went wrong - we should have caught this
			 * error the first time we called _feature_string2list.
			 */
			error("%s: Problem converting feature string %s to matching OR list",
			      __func__, str);
			rc = feature_err;
			xfree(str);
			goto fini;
		}
		log_flag(NODE_FEATURES, "%s: Converted %sfeature list:'%s' to matching OR:'%s'",
			 __func__, prefer ? "prefer " : "",
			 valid_feature.features, str);
		xfree(str);
	}

	if (job_ptr->batch_features) {
		detail_ptr->feature_list_use = *feature_list;
		detail_ptr->features_use = valid_feature.features;
		rc = _valid_batch_features(job_ptr, valid_feature.can_reboot);
		detail_ptr->feature_list_use = NULL;
		detail_ptr->features_use = NULL;
		if (rc != SLURM_SUCCESS)
			goto fini;
	}

	valid_feature.feature_list = *feature_list;
	rc = _valid_feature_list(job_ptr, &valid_feature, is_reservation);
	if (rc != SLURM_SUCCESS) {
		rc = feature_err;
		goto fini;
	}

fini:
	xfree(valid_feature.debug_str);
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

static int _foreach_valid_feature_list(void *x, void *arg)
{
	job_feature_t *feat_ptr = x;
	valid_feature_t *valid_feature = arg;

	if ((feat_ptr->op_code == FEATURE_OP_MOR) ||
	    (feat_ptr->op_code == FEATURE_OP_XAND)) {
		valid_feature->bracket = feat_ptr->paren + 1;
	}
	if (feat_ptr->paren > valid_feature->paren) {
		valid_feature->paren = feat_ptr->paren;
	}
	if (feat_ptr->paren < valid_feature->paren) {
		valid_feature->paren = feat_ptr->paren;
	}
	if ((valid_feature->rc == SLURM_SUCCESS) &&
	    !valid_feature->skip_validation) {
		valid_feature->rc =
			_valid_node_feature(feat_ptr->name,
					    valid_feature->can_reboot);
		if (valid_feature->rc != SLURM_SUCCESS)
			verbose("%s feature %s is not usable on any node: %s",
				valid_feature->debug_str, feat_ptr->name,
				valid_feature->features);
	}
	if ((feat_ptr->op_code == FEATURE_OP_XAND) && !feat_ptr->count) {
		verbose("%s feature %s invalid, count must be used with XAND: %s",
			valid_feature->debug_str, feat_ptr->name,
			valid_feature->features);
		valid_feature->rc = ESLURM_INVALID_FEATURE;
	}
	if ((feat_ptr->op_code == FEATURE_OP_MOR) && feat_ptr->count) {
		verbose("%s feature %s invalid, count must not be used with MOR: %s",
			valid_feature->debug_str, feat_ptr->name,
			valid_feature->features);
		valid_feature->rc = ESLURM_INVALID_FEATURE;
	}

	/* In brackets, outside of paren */
	if ((valid_feature->bracket > valid_feature->paren) &&
	    ((feat_ptr->op_code != FEATURE_OP_MOR) &&
	     (feat_ptr->op_code != FEATURE_OP_XAND))) {
		if (valid_feature->has_xand && !feat_ptr->count) {
			valid_feature->rc = ESLURM_INVALID_FEATURE;
			verbose("%s feature %s invalid, count must be used with XAND: %s",
				valid_feature->debug_str, feat_ptr->name,
				valid_feature->features);
		}
		if (valid_feature->has_mor && feat_ptr->count) {
			valid_feature->rc = ESLURM_INVALID_FEATURE;
			verbose("%s feature %s invalid, count must not be used with MOR: %s",
				valid_feature->debug_str, feat_ptr->name,
				valid_feature->features);
		}
		valid_feature->bracket = 0;
		valid_feature->has_xand = false;
		valid_feature->has_mor = false;
	}
	if (feat_ptr->op_code == FEATURE_OP_XAND)
		valid_feature->has_xand = true;
	if (feat_ptr->op_code == FEATURE_OP_MOR)
		valid_feature->has_mor = true;

	return 0;
}

static int _valid_feature_list(job_record_t *job_ptr,
			       valid_feature_t *valid_feature,
			       bool is_reservation)
{
	static time_t sched_update = 0;
	static bool ignore_prefer_val = false, ignore_constraint_val = false;
	bool is_prefer_list, skip_validation;

	if (!valid_feature->feature_list) {
		debug2("%s feature list is empty",
		       valid_feature->debug_str);
		return valid_feature->rc;
	}

	if (sched_update != slurm_conf.last_update) {
		sched_update = slurm_conf.last_update;
		if (xstrcasestr(slurm_conf.sched_params,
				"ignore_prefer_validation"))
			ignore_prefer_val = true;
		else
			ignore_prefer_val = false;
		if (xstrcasestr(slurm_conf.sched_params,
				"ignore_constraint_validation"))
			ignore_constraint_val = true;
		else
			ignore_constraint_val = false;

	}

	is_prefer_list = (valid_feature->feature_list ==
			  job_ptr->details->prefer_list);
	skip_validation = (is_prefer_list && ignore_prefer_val) ||
			  (!is_prefer_list && ignore_constraint_val);

	valid_feature->skip_validation = skip_validation;

	(void) list_for_each(valid_feature->feature_list,
			     _foreach_valid_feature_list,
			     valid_feature);

	if (valid_feature->rc == SLURM_SUCCESS) {
		debug("%s feature list: %s",
		      valid_feature->debug_str, valid_feature->features);
	} else {
		if (is_reservation) {
			info("Reservation has invalid feature list: %s",
			     valid_feature->features);
		} else {
			if (valid_feature->can_reboot)
				info("%s has invalid feature list: %s",
				     valid_feature->debug_str,
				     valid_feature->features);
			else
				info("%s has invalid feature list (%s) or the features are not active and this user cannot reboot to update node features",
				     valid_feature->debug_str,
				     valid_feature->features);
		}
	}

	return valid_feature->rc;
}

static int _find_feature_in_list(void *x, void *arg)
{
	node_feature_t *feature_ptr = x;
	char *feature = arg;

	if (!xstrcmp(feature_ptr->name, feature))
		return 1;

	return 0;
}

/* Validate that job's feature is available on some node(s) */
static int _valid_node_feature(char *feature, bool can_reboot)
{
	int rc = ESLURM_INVALID_FEATURE;
	list_t *use_list =
		can_reboot ? avail_feature_list : active_feature_list;

	if (list_find_first(use_list, _find_feature_in_list, feature))
		rc = SLURM_SUCCESS;

	return rc;
}

#define REBUILD_PENDING SLURM_BIT(0)
#define REBUILD_ACTIVE SLURM_BIT(1)

typedef struct {
	uint16_t flags;
	job_record_t *job_ptr;
} rebuild_args_t;

static int _build_partition_string(void *object, void *arg) {
	part_record_t *part_ptr = object;
	rebuild_args_t *args = arg;
	uint16_t flags = args->flags;
	job_record_t *job_ptr = args->job_ptr;

	if (flags & REBUILD_PENDING) {
		job_ptr->part_ptr = part_ptr;
		flags &= ~(REBUILD_PENDING);
	}
	if ((flags & REBUILD_ACTIVE) && (part_ptr == job_ptr->part_ptr))
		return SLURM_SUCCESS;       /* already added */
	if (job_ptr->partition)
		xstrcat(job_ptr->partition, ",");
	xstrcat(job_ptr->partition, part_ptr->name);
	return SLURM_SUCCESS;
}

/* If a job can run in multiple partitions, when it is started we want to
 * put the name of the partition used _first_ in that list. When slurmctld
 * restarts, that will be used to set the job's part_ptr and that will be
 * reported to squeue. We leave all of the partitions in the list though,
 * so the job can be requeued and have access to them all. */
extern void rebuild_job_part_list(job_record_t *job_ptr)
{
	rebuild_args_t arg = {
		.job_ptr = job_ptr,
	};

	xfree(job_ptr->partition);

	if (!job_ptr->part_ptr_list) {
		job_ptr->partition = xstrdup(job_ptr->part_ptr->name);
		last_job_update = time(NULL);
		return;
	}

	if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) {
		arg.flags |= REBUILD_ACTIVE;
		job_ptr->partition = xstrdup(job_ptr->part_ptr->name);
	} else if (IS_JOB_PENDING(job_ptr))
		arg.flags |= REBUILD_PENDING;
	list_for_each(job_ptr->part_ptr_list, _build_partition_string, &arg);
	last_job_update = time(NULL);
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
	job_state_unset_flag(job_ptr, JOB_COMPLETING);
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
	slurm_mutex_lock(&sched_mutex);
	slurm_cond_broadcast(&sched_cond);
	slurm_mutex_unlock(&sched_mutex);
	slurm_thread_join(thread_id_sched);
}
