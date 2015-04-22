/*****************************************************************************\
 *  backfill.c - simple backfill scheduler plugin.
 *
 *  If a partition is does not have root only access and nodes are not shared
 *  then raise the priority of pending jobs if doing so does not adversely
 *  effect the expected initiation of any higher priority job. We do not alter
 *  a job's required or excluded node list, so this is a conservative
 *  algorithm.
 *
 *  For example, consider a cluster "lx[01-08]" with one job executing on
 *  nodes "lx[01-04]". The highest priority pending job requires five nodes
 *  including "lx05". The next highest priority pending job requires any
 *  three nodes. Without explicitly forcing the second job to use nodes
 *  "lx[06-08]", we can't start it without possibly delaying the higher
 *  priority job.
 *****************************************************************************
 *  Copyright (C) 2003-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/licenses.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/node_scheduler.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"
#include "backfill.h"

#define BACKFILL_INTERVAL	30
#define BACKFILL_RESOLUTION	60
#define BACKFILL_WINDOW		(24 * 60 * 60)
#define BF_MAX_USERS		1000
#define BF_MAX_JOB_ARRAY_RESV	20

#define SLURMCTLD_THREAD_LIMIT	5
#define SCHED_TIMEOUT		2000000	/* time in micro-seconds */
#define YIELD_SLEEP		500000;	/* time in micro-seconds */

typedef struct node_space_map {
	time_t begin_time;
	time_t end_time;
	bitstr_t *avail_bitmap;
	int next;	/* next record, by time, zero termination */
} node_space_map_t;

/* Diag statistics */
extern diag_stats_t slurmctld_diag_stats;
int bf_last_yields = 0;

/*********************** local variables *********************/
static bool stop_backfill = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
static bool config_flag = false;
static uint64_t debug_flags = 0;
static int backfill_interval = BACKFILL_INTERVAL;
static int backfill_resolution = BACKFILL_RESOLUTION;
static int backfill_window = BACKFILL_WINDOW;
static int bf_max_job_array_resv = BF_MAX_JOB_ARRAY_RESV;
static int bf_min_age_reserve = 0;
static int max_backfill_job_cnt = 100;
static int max_backfill_job_per_part = 0;
static int max_backfill_job_per_user = 0;
static int max_backfill_jobs_start = 0;
static bool backfill_continue = false;
static int defer_rpc_cnt = 0;
static int sched_timeout = SCHED_TIMEOUT;
static int yield_sleep   = YIELD_SLEEP;

/*********************** local functions *********************/
static void _add_reservation(uint32_t start_time, uint32_t end_reserve,
			     bitstr_t *res_bitmap,
			     node_space_map_t *node_space,
			     int *node_space_recs);
static int  _attempt_backfill(void);
static void _clear_job_start_times(void);
static int  _delta_tv(struct timeval *tv);
static bool _job_is_completing(void);
static bool _job_part_valid(struct job_record *job_ptr,
			    struct part_record *part_ptr);
static void _load_config(void);
static bool _many_pending_rpcs(void);
static bool _more_work(time_t last_backfill_time);
static void _my_sleep(int usec);
static int  _num_feature_count(struct job_record *job_ptr);
static void _reset_job_time_limit(struct job_record *job_ptr, time_t now,
				  node_space_map_t *node_space);
static int  _start_job(struct job_record *job_ptr, bitstr_t *avail_bitmap);
static bool _test_resv_overlap(node_space_map_t *node_space,
			       bitstr_t *use_bitmap, uint32_t start_time,
			       uint32_t end_reserve);
static int  _try_sched(struct job_record *job_ptr, bitstr_t **avail_bitmap,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, bitstr_t *exc_core_bitmap);
static int  _yield_locks(int usec);

/* Log recousrces to be allocated to a pending job */
static void _dump_job_sched(struct job_record *job_ptr, time_t end_time,
			    bitstr_t *avail_bitmap)
{
	char begin_buf[32], end_buf[32], *node_list;

	slurm_make_time_str(&job_ptr->start_time, begin_buf, sizeof(begin_buf));
	slurm_make_time_str(&end_time, end_buf, sizeof(end_buf));
	node_list = bitmap2node_name(avail_bitmap);
	info("Job %u to start at %s, end at %s on %s",
	     job_ptr->job_id, begin_buf, end_buf, node_list);
	xfree(node_list);
}

static void _dump_job_test(struct job_record *job_ptr, bitstr_t *avail_bitmap,
			   time_t start_time)
{
	char begin_buf[32], *node_list;

	if (start_time == 0)
		strcpy(begin_buf, "NOW");
	else
		slurm_make_time_str(&start_time, begin_buf, sizeof(begin_buf));
	node_list = bitmap2node_name(avail_bitmap);
	info("Test job %u at %s on %s", job_ptr->job_id, begin_buf, node_list);
	xfree(node_list);
}

/* Log resource allocate table */
static void _dump_node_space_table(node_space_map_t *node_space_ptr)
{
	int i = 0;
	char begin_buf[32], end_buf[32], *node_list;

	info("=========================================");
	while (1) {
		slurm_make_time_str(&node_space_ptr[i].begin_time,
				    begin_buf, sizeof(begin_buf));
		slurm_make_time_str(&node_space_ptr[i].end_time,
				    end_buf, sizeof(end_buf));
		node_list = bitmap2node_name(node_space_ptr[i].avail_bitmap);
		info("Begin:%s End:%s Nodes:%s",
		     begin_buf, end_buf, node_list);
		xfree(node_list);
		if ((i = node_space_ptr[i].next) == 0)
			break;
	}
	info("=========================================");
}

/*
 * _job_is_completing - Determine if jobs are in the process of completing.
 *	This is a variant of job_is_completing in slurmctld/job_scheduler.c.
 *	It always gives completing jobs at least 5 secs to complete.
 * RET - True if any job is in the process of completing
 */
static bool _job_is_completing(void)
{
	bool completing = false;
	ListIterator job_iterator;
	struct job_record *job_ptr = NULL;
	uint16_t complete_wait = slurm_get_complete_wait();
	time_t recent;

	if (job_list == NULL)
		return completing;

	recent = time(NULL) - MAX(complete_wait, 5);
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
 * _many_pending_rpcs - Determine if slurmctld is busy with many active RPCs
 * RET - True if slurmctld currently has more than SLURMCTLD_THREAD_LIMIT
 *	 active RPCs
 */
static bool _many_pending_rpcs(void)
{
	//info("thread_count = %u", slurmctld_config.server_thread_count);
	if ((defer_rpc_cnt > 0) &&
	    (slurmctld_config.server_thread_count >= defer_rpc_cnt))
		return true;
	return false;
}

/* test if job has feature count specification */
static int _num_feature_count(struct job_record *job_ptr)
{
	struct job_details *detail_ptr = job_ptr->details;
	int rc = 0;
	ListIterator feat_iter;
	struct feature_record *feat_ptr;

	if (detail_ptr->feature_list == NULL)	/* no constraints */
		return rc;

	feat_iter = list_iterator_create(detail_ptr->feature_list);
	while ((feat_ptr = (struct feature_record *) list_next(feat_iter))) {
		if (feat_ptr->count)
			rc++;
	}
	list_iterator_destroy(feat_iter);

	return rc;
}

/* Attempt to schedule a specific job on specific available nodes
 * IN job_ptr - job to schedule
 * IN/OUT avail_bitmap - nodes available/selected to use
 * IN exc_core_bitmap - cores which can not be used
 * RET SLURM_SUCCESS on success, otherwise an error code
 */
static int  _try_sched(struct job_record *job_ptr, bitstr_t **avail_bitmap,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, bitstr_t *exc_core_bitmap)
{
	bitstr_t *tmp_bitmap;
	int rc = SLURM_SUCCESS;
	int feat_cnt = _num_feature_count(job_ptr);
	List preemptee_candidates = NULL;
	List preemptee_job_list = NULL;

	if (feat_cnt) {
		/* Ideally schedule the job feature by feature,
		 * but I don't want to add that complexity here
		 * right now, so clear the feature counts and try
		 * to schedule. This will work if there is only
		 * one feature count. It should work fairly well
		 * in cases where there are multiple feature
		 * counts. */
		struct job_details *detail_ptr = job_ptr->details;
		ListIterator feat_iter;
		struct feature_record *feat_ptr;
		int i = 0, list_size;
		uint16_t *feat_cnt_orig = NULL, high_cnt = 0;

		/* Clear the feature counts */
		list_size = list_count(detail_ptr->feature_list);
		feat_cnt_orig = xmalloc(sizeof(uint16_t) * list_size);
		feat_iter = list_iterator_create(detail_ptr->feature_list);
		while ((feat_ptr =
			(struct feature_record *) list_next(feat_iter))) {
			high_cnt = MAX(high_cnt, feat_ptr->count);
			feat_cnt_orig[i++] = feat_ptr->count;
			feat_ptr->count = 0;
		}
		list_iterator_destroy(feat_iter);

		if ((job_req_node_filter(job_ptr, *avail_bitmap) !=
		     SLURM_SUCCESS) ||
		    (bit_set_count(*avail_bitmap) < high_cnt)) {
			rc = ESLURM_NODES_BUSY;
		} else {
			preemptee_candidates =
					slurm_find_preemptable_jobs(job_ptr);
			rc = select_g_job_test(job_ptr, *avail_bitmap,
					       high_cnt, max_nodes, req_nodes,
					       SELECT_MODE_WILL_RUN,
					       preemptee_candidates,
					       &preemptee_job_list,
					       exc_core_bitmap);
			if (preemptee_job_list) {
				list_destroy(preemptee_job_list);
				preemptee_job_list = NULL;
			}
		}

		/* Restore the feature counts */
		i = 0;
		feat_iter = list_iterator_create(detail_ptr->feature_list);
		while ((feat_ptr =
			(struct feature_record *) list_next(feat_iter))) {
			feat_ptr->count = feat_cnt_orig[i++];
		}
		list_iterator_destroy(feat_iter);
		xfree(feat_cnt_orig);
	} else {
		/* Try to schedule the job. First on dedicated nodes
		 * then on shared nodes (if so configured). */
		uint16_t orig_shared;
		time_t now = time(NULL);
		char str[100];

		preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);
		orig_shared = job_ptr->details->share_res;
		job_ptr->details->share_res = 0;
		tmp_bitmap = bit_copy(*avail_bitmap);

		if (exc_core_bitmap) {
			bit_fmt(str, (sizeof(str) - 1), exc_core_bitmap);
			debug2(" _try_sched with exclude core bitmap: %s",str);
		}

		rc = select_g_job_test(job_ptr, *avail_bitmap, min_nodes,
				       max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       preemptee_candidates,
				       &preemptee_job_list,
				       exc_core_bitmap);
		if (preemptee_job_list) {
			list_destroy(preemptee_job_list);
			preemptee_job_list = NULL;
		}

		job_ptr->details->share_res = orig_shared;

		if (((rc != SLURM_SUCCESS) || (job_ptr->start_time > now)) &&
		    (orig_shared != 0)) {
			FREE_NULL_BITMAP(*avail_bitmap);
			*avail_bitmap = tmp_bitmap;
			rc = select_g_job_test(job_ptr, *avail_bitmap,
					       min_nodes, max_nodes, req_nodes,
					       SELECT_MODE_WILL_RUN,
					       preemptee_candidates,
					       &preemptee_job_list,
					       exc_core_bitmap);
			if (preemptee_job_list) {
				list_destroy(preemptee_job_list);
				preemptee_job_list = NULL;
			}
		} else
			FREE_NULL_BITMAP(tmp_bitmap);
	}

	if (preemptee_candidates)
		list_destroy(preemptee_candidates);
	return rc;

}

/* Terminate backfill_agent */
extern void stop_backfill_agent(void)
{
	pthread_mutex_lock(&term_lock);
	stop_backfill = true;
	pthread_cond_signal(&term_cond);
	pthread_mutex_unlock(&term_lock);
}

/* Return the number of micro-seconds between now and argument "tv" */
static int _delta_tv(struct timeval *tv)
{
	struct timeval now = {0, 0};
	int delta_t;

	if (gettimeofday(&now, NULL))
		return 1;		/* Some error */

	delta_t  = (now.tv_sec - tv->tv_sec) * 1000000;
	delta_t += (now.tv_usec - tv->tv_usec);
	return delta_t;
}

static void _my_sleep(int usec)
{
	int64_t nsec;
	struct timespec ts = {0, 0};
	struct timeval  tv = {0, 0};

	if (gettimeofday(&tv, NULL))
		return;		/* Some error */

	nsec  = tv.tv_usec + usec;
	nsec *= 1000;
	ts.tv_sec  = tv.tv_sec + (nsec / 1000000000);
	ts.tv_nsec = nsec % 1000000000;
	pthread_mutex_lock(&term_lock);
	if (!stop_backfill)
		pthread_cond_timedwait(&term_cond, &term_lock, &ts);
	pthread_mutex_unlock(&term_lock);
}

static void _load_config(void)
{
	char *sched_params, *tmp_ptr;

	sched_params = slurm_get_sched_params();
	debug_flags  = slurm_get_debug_flags();

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_interval=")))
		backfill_interval = atoi(tmp_ptr + 12);
	if (backfill_interval < 1) {
		error("Invalid SchedulerParameters bf_interval: %d",
		      backfill_interval);
		backfill_interval = BACKFILL_INTERVAL;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_window=")))
		backfill_window = atoi(tmp_ptr + 10) * 60;  /* mins to secs */
	if (backfill_window < 1) {
		error("Invalid SchedulerParameters bf_window: %d",
		      backfill_window);
		backfill_window = BACKFILL_WINDOW;
	}

	/* "max_job_bf" replaced by "bf_max_job_test" in version 14.03 and
	 * can be removed later. Only "bf_max_job_test" is documented. */
	if (sched_params && (tmp_ptr=strstr(sched_params, "max_job_bf=")))
		max_backfill_job_cnt = atoi(tmp_ptr + 11);
	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_max_job_test=")))
		max_backfill_job_cnt = atoi(tmp_ptr + 16);
	if (max_backfill_job_cnt < 1) {
		error("Invalid SchedulerParameters bf_max_job_test: %d",
		      max_backfill_job_cnt);
		max_backfill_job_cnt = 50;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_resolution=")))
		backfill_resolution = atoi(tmp_ptr + 14);
	if (backfill_resolution < 1) {
		error("Invalid SchedulerParameters bf_resolution: %d",
		      backfill_resolution);
		backfill_resolution = BACKFILL_RESOLUTION;
	}

	if (sched_params &&
	    (tmp_ptr=strstr(sched_params, "bf_max_job_array_resv=")))
		bf_max_job_array_resv = atoi(tmp_ptr + 22);
	if (bf_max_job_array_resv < 0) {
		error("Invalid SchedulerParameters bf_max_job_array_resv: %d",
		      bf_max_job_array_resv);
		bf_max_job_array_resv = BF_MAX_JOB_ARRAY_RESV;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_max_job_part=")))
		max_backfill_job_per_part = atoi(tmp_ptr + 16);
	if (max_backfill_job_per_part < 0) {
		error("Invalid SchedulerParameters bf_max_job_part: %d",
		      max_backfill_job_per_part);
		max_backfill_job_per_part = 0;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_max_job_start=")))
		max_backfill_jobs_start = atoi(tmp_ptr + 17);
	if (max_backfill_jobs_start < 0) {
		error("Invalid SchedulerParameters bf_max_job_start: %d",
		      max_backfill_jobs_start);
		max_backfill_jobs_start = 0;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_max_job_user=")))
		max_backfill_job_per_user = atoi(tmp_ptr + 16);
	if (max_backfill_job_per_user < 0) {
		error("Invalid SchedulerParameters bf_max_job_user: %d",
		      max_backfill_job_per_user);
		max_backfill_job_per_user = 0;
	}

	if (sched_params &&
	    (tmp_ptr=strstr(sched_params, "bf_min_age_reserve=")))
		bf_min_age_reserve = atoi(tmp_ptr + 19);
	else
		bf_min_age_reserve = 0;
	if (bf_min_age_reserve < 0) {
		error("Invalid SchedulerParameters bf_min_age_reserve: %d",
		      bf_min_age_reserve);
		bf_min_age_reserve = 0;
	}

	/* bf_continue makes backfill continue where it was if interrupted
	 */
	if (sched_params && (strstr(sched_params, "bf_continue"))) {
		backfill_continue = true;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_yield_interval=")))
		sched_timeout = atoi(tmp_ptr + 18);
	if (sched_timeout <= 0) {
		error("Invalid backfill scheduler bf_sched_timeout: %d",
		      sched_timeout);
		sched_timeout = SCHED_TIMEOUT;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_yield_sleep=")))
		yield_sleep = atoi(tmp_ptr + 15);
	if (yield_sleep <= 0) {
		error("Invalid backfill scheduler bf_yield_sleep: %d",
		      yield_sleep);
		yield_sleep = YIELD_SLEEP;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "max_rpc_cnt=")))
		defer_rpc_cnt = atoi(tmp_ptr + 12);
	if (defer_rpc_cnt < 0) {
		error("Invalid SchedulerParameters max_rpc_cnt: %d",
		      defer_rpc_cnt);
		defer_rpc_cnt = 0;
	}

	xfree(sched_params);
}

/* Note that slurm.conf has changed */
extern void backfill_reconfig(void)
{
	slurm_mutex_lock(&config_lock);
	config_flag = true;
	slurm_mutex_unlock(&config_lock);
}

static void _do_diag_stats(struct timeval *tv1, struct timeval *tv2,
			   int yield_sleep)
{
	uint32_t yield_sleep_usecs = yield_sleep * 1000000;
	uint32_t delta_t, real_time;

	delta_t  = (tv2->tv_sec  - tv1->tv_sec) * 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;

	real_time = (delta_t - (bf_last_yields * yield_sleep_usecs));

	slurmctld_diag_stats.bf_cycle_counter++;
	slurmctld_diag_stats.bf_cycle_sum += real_time;
	slurmctld_diag_stats.bf_cycle_last = real_time;

	slurmctld_diag_stats.bf_depth_sum += slurmctld_diag_stats.bf_last_depth;
	slurmctld_diag_stats.bf_depth_try_sum +=
		slurmctld_diag_stats.bf_last_depth_try;
	if (slurmctld_diag_stats.bf_cycle_last >
	    slurmctld_diag_stats.bf_cycle_max) {
		slurmctld_diag_stats.bf_cycle_max = slurmctld_diag_stats.
						    bf_cycle_last;
	}

	slurmctld_diag_stats.bf_active = 0;
}


/* backfill_agent - detached thread periodically attempts to backfill jobs */
extern void *backfill_agent(void *args)
{
	time_t now;
	double wait_time;
	static time_t last_backfill_time = 0;
	/* Read config and partitions; Write jobs and nodes */
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	bool load_config;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "slurmctld_bckfl", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m",
		      __func__, "slurm_backfill");
	}
#endif
	_load_config();
	last_backfill_time = time(NULL);
	while (!stop_backfill) {
		_my_sleep(backfill_interval * 1000000);
		if (stop_backfill)
			break;
		slurm_mutex_lock(&config_lock);
		if (config_flag) {
			config_flag = false;
			load_config = true;
		} else {
			load_config = false;
		}
		slurm_mutex_unlock(&config_lock);
		if (load_config)
			_load_config();
		now = time(NULL);
		wait_time = difftime(now, last_backfill_time);
		if ((wait_time < backfill_interval) ||
		    _job_is_completing() || _many_pending_rpcs() ||
		    !avail_front_end(NULL) || !_more_work(last_backfill_time))
			continue;

		lock_slurmctld(all_locks);
		(void) _attempt_backfill();
		last_backfill_time = time(NULL);
		unlock_slurmctld(all_locks);
	}
	return NULL;
}

/* Clear the start_time for all pending jobs. This is used to insure that a job which
 * can run in multiple partitions has its start_time set to the smallest
 * value in any of those partitions. */
static void _clear_job_start_times(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_PENDING(job_ptr))
			job_ptr->start_time = 0;
	}
	list_iterator_destroy(job_iterator);
}

/* Return non-zero to break the backfill loop if change in job, node or
 * partition state or the backfill scheduler needs to be stopped. */
static int _yield_locks(int usec)
{
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };
	time_t job_update, node_update, part_update;
	bool load_config = false;

	job_update  = last_job_update;
	node_update = last_node_update;
	part_update = last_part_update;

	unlock_slurmctld(all_locks);
	bf_last_yields++;
	_my_sleep(usec);
	lock_slurmctld(all_locks);
	slurm_mutex_lock(&config_lock);
	if (config_flag)
		load_config = true;
	slurm_mutex_unlock(&config_lock);

	if ((last_job_update  == job_update)  &&
	    (last_node_update == node_update) &&
	    (last_part_update == part_update) &&
	    (! stop_backfill) && (! load_config))
		return 0;
	else
		return 1;
}

/* Test if this job still has access to the specified partition. The job's
 * available partitions may have changed when locks were released */
static bool _job_part_valid(struct job_record *job_ptr,
			    struct part_record *part_ptr)
{
	struct part_record *avail_part_ptr;
	ListIterator part_iterator;
	bool rc = false;

	if (job_ptr->part_ptr_list) {
		part_iterator = list_iterator_create(job_ptr->part_ptr_list);
		while ((avail_part_ptr = (struct part_record *)
				list_next(part_iterator))) {
			if (avail_part_ptr == part_ptr) {
				rc = true;
				break;
			}
		}
		list_iterator_destroy(part_iterator);
	} else if (job_ptr->part_ptr == part_ptr) {
		rc = true;
	}

	return rc;
}

static int _attempt_backfill(void)
{
	DEF_TIMERS;
	bool filter_root = false;
	List job_queue;
	job_queue_rec_t *job_queue_rec;
	slurmdb_qos_rec_t *qos_ptr = NULL;
	int i, j, node_space_recs;
	struct job_record *job_ptr;
	struct part_record *part_ptr, **bf_part_ptr = NULL;
	uint32_t end_time, end_reserve;
	uint32_t time_limit, comp_time_limit, orig_time_limit, part_time_limit;
	uint32_t min_nodes, max_nodes, req_nodes;
	bitstr_t *avail_bitmap = NULL, *resv_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL, *non_cg_bitmap = NULL;
	time_t now, sched_start, later_start, start_res, resv_end, window_end;
	time_t orig_start_time = (time_t) 0;
	node_space_map_t *node_space;
	struct timeval bf_time1, bf_time2;
	int rc = 0;
	int job_test_count = 0, pend_time;
	uint32_t *uid = NULL, nuser = 0, bf_parts = 0, *bf_part_jobs = NULL;
	uint16_t *njobs = NULL;
	bool already_counted;
	uint32_t reject_array_job_id = 0;
	struct part_record *reject_array_part = NULL;
	uint32_t job_start_cnt = 0, start_time;
	time_t config_update = slurmctld_conf.last_update;
	time_t part_update = last_part_update;
	struct timeval start_tv;
	uint32_t test_array_job_id = 0;
	uint32_t test_array_count = 0;
	bool resv_overlap = false;

	bf_last_yields = 0;
#ifdef HAVE_ALPS_CRAY
	/*
	 * Run a Basil Inventory immediately before setting up the schedule
	 * plan, to avoid race conditions caused by ALPS node state change.
	 * Needs to be done with the node-state lock taken.
	 */
	START_TIMER;
	if (select_g_update_block(NULL)) {
		debug4("backfill: not scheduling due to ALPS");
		return SLURM_SUCCESS;
	}
	END_TIMER;
	if (debug_flags & DEBUG_FLAG_BACKFILL)
		info("backfill: ALPS inventory completed, %s", TIME_STR);

	/* The Basil inventory can take a long time to complete. Process
	 * pending RPCs before starting the backfill scheduling logic */
	_yield_locks(1000000);
#endif

	START_TIMER;
	if (debug_flags & DEBUG_FLAG_BACKFILL)
		info("backfill: beginning");
	else
		debug("backfill: beginning");
	sched_start = now = time(NULL);
	gettimeofday(&start_tv, NULL);

	if (slurm_get_root_filter())
		filter_root = true;

	job_queue = build_job_queue(true, true);
	if (list_count(job_queue) == 0) {
		if (debug_flags & DEBUG_FLAG_BACKFILL)
			info("backfill: no jobs to backfill");
		else
			debug("backfill: no jobs to backfill");
		list_destroy(job_queue);
		return 0;
	}

	if (backfill_continue)
		_clear_job_start_times();

	gettimeofday(&bf_time1, NULL);

	non_cg_bitmap = bit_copy(cg_node_bitmap);
	bit_not(non_cg_bitmap);

	slurmctld_diag_stats.bf_queue_len = list_count(job_queue);
	slurmctld_diag_stats.bf_queue_len_sum += slurmctld_diag_stats.
						 bf_queue_len;
	slurmctld_diag_stats.bf_last_depth = 0;
	slurmctld_diag_stats.bf_last_depth_try = 0;
	slurmctld_diag_stats.bf_when_last_cycle = now;
	slurmctld_diag_stats.bf_active = 1;

	node_space = xmalloc(sizeof(node_space_map_t) *
			     (max_backfill_job_cnt * 2 + 1));
	node_space[0].begin_time = sched_start;
	window_end = sched_start + backfill_window;
	node_space[0].end_time = window_end;
	node_space[0].avail_bitmap = bit_copy(avail_node_bitmap);
	node_space[0].next = 0;
	node_space_recs = 1;
	if (debug_flags & DEBUG_FLAG_BACKFILL_MAP)
		_dump_node_space_table(node_space);

	if (max_backfill_job_per_part) {
		ListIterator part_iterator;
		struct part_record *part_ptr;
		bf_parts = list_count(part_list);
		bf_part_ptr  = xmalloc(sizeof(struct part_record *) * bf_parts);
		bf_part_jobs = xmalloc(sizeof(int) * bf_parts);
		part_iterator = list_iterator_create(part_list);
		i = 0;
		while ((part_ptr = (struct part_record *)
				   list_next(part_iterator))) {
			bf_part_ptr[i++] = part_ptr;
		}
		list_iterator_destroy(part_iterator);
	}
	if (max_backfill_job_per_user) {
		uid = xmalloc(BF_MAX_USERS * sizeof(uint32_t));
		njobs = xmalloc(BF_MAX_USERS * sizeof(uint16_t));
	}
	sort_job_queue(job_queue);
	while (1) {
		job_queue_rec = (job_queue_rec_t *) list_pop(job_queue);
		if (!job_queue_rec) {
			if (debug_flags & DEBUG_FLAG_BACKFILL)
				info("backfill: reached end of job queue");
			break;
		}
		if (slurmctld_config.shutdown_time) {
			xfree(job_queue_rec);
			break;
		}
		if (((defer_rpc_cnt > 0) &&
		     (slurmctld_config.server_thread_count >= defer_rpc_cnt)) ||
		    (_delta_tv(&start_tv) >= sched_timeout)) {
			if (debug_flags & DEBUG_FLAG_BACKFILL) {
				END_TIMER;
				info("backfill: completed yielding locks "
				     "after testing %u(%d) jobs, %s",
				     slurmctld_diag_stats.bf_last_depth,
				     job_test_count, TIME_STR);
			}
			if ((_yield_locks(yield_sleep) && !backfill_continue) ||
			    (slurmctld_conf.last_update != config_update) ||
			    (last_part_update != part_update)) {
				if (debug_flags & DEBUG_FLAG_BACKFILL) {
					info("backfill: system state changed, "
					     "breaking out after testing "
					     "%u(%d) jobs",
					     slurmctld_diag_stats.bf_last_depth,
					     job_test_count);
				}
				rc = 1;
				xfree(job_queue_rec);
				break;
			}
			/* cg_node_bitmap may be changed */
			bit_copybits(non_cg_bitmap, cg_node_bitmap);
			bit_not(non_cg_bitmap);
			/* Reset backfill scheduling timers, resume testing */
			sched_start = time(NULL);
			gettimeofday(&start_tv, NULL);
			job_test_count = 0;
			START_TIMER;
		}

		job_ptr  = job_queue_rec->job_ptr;
		/* With bf_continue configured, the original job could have
		 * been cancelled and purged. Validate pointer here. */
		if ((job_ptr->magic  != JOB_MAGIC) ||
		    (job_ptr->job_id != job_queue_rec->job_id)) {
			xfree(job_queue_rec);
			continue;
		}
		orig_start_time = job_ptr->start_time;
		orig_time_limit = job_ptr->time_limit;
		part_ptr = job_queue_rec->part_ptr;
		xfree(job_queue_rec);

next_task:
		job_test_count++;
		slurmctld_diag_stats.bf_last_depth++;
		already_counted = false;

		if (!IS_JOB_PENDING(job_ptr))
			continue;	/* started in another partition */
		if (job_ptr->preempt_in_progress)
			continue; 	/* scheduled in another partition */
		if (!avail_front_end(job_ptr))
			continue;	/* No available frontend for this job */
		if (!_job_part_valid(job_ptr, part_ptr))
			continue;	/* Partition change during lock yield */
		if ((job_ptr->array_task_id != NO_VAL) || job_ptr->array_recs) {
			if ((reject_array_job_id == job_ptr->array_job_id) &&
			    (reject_array_part   == part_ptr))
				continue;  /* already rejected array element */

			/* assume reject whole array for now, clear if OK */
			reject_array_job_id = job_ptr->array_job_id;
			reject_array_part   = part_ptr;

			if (!job_array_start_test(job_ptr))
				continue;
		}
		job_ptr->part_ptr = part_ptr;

		if (debug_flags & DEBUG_FLAG_BACKFILL) {
			info("backfill test for JobID=%u Prio=%u Partition=%s",
			     job_ptr->job_id, job_ptr->priority,
			     job_ptr->part_ptr->name);
		}

		if (max_backfill_job_per_part) {
			bool skip_job = false;
			for (j = 0; j < bf_parts; j++) {
				if (bf_part_ptr[j] != job_ptr->part_ptr)
					continue;
				if (bf_part_jobs[j]++ >=
				    max_backfill_job_per_part)
					skip_job = true;
				break;
			}
			if (skip_job) {
				if (debug_flags & DEBUG_FLAG_BACKFILL)
					info("backfill: have already "
					     "checked %u jobs for "
					     "partition %s; skipping "
					     "job %u",
					     max_backfill_job_per_part,
					     job_ptr->part_ptr->name,
					     job_ptr->job_id);
				continue;
			}
		}
		if (max_backfill_job_per_user) {
			for (j = 0; j < nuser; j++) {
				if (job_ptr->user_id == uid[j]) {
					njobs[j]++;
					if (debug_flags & DEBUG_FLAG_BACKFILL)
						debug("backfill: user %u: "
						      "#jobs %u",
						      uid[j], njobs[j]);
					break;
				}
			}
			if (j == nuser) { /* user not found */
				static bool bf_max_user_msg = true;
				if (nuser < BF_MAX_USERS) {
					uid[j] = job_ptr->user_id;
					njobs[j] = 1;
					nuser++;
				} else if (bf_max_user_msg) {
					bf_max_user_msg = false;
					error("backfill: too many users in "
					      "queue. Consider increasing "
					      "BF_MAX_USERS");
				}
				if (debug_flags & DEBUG_FLAG_BACKFILL)
					debug2("backfill: found new user %u. "
					       "Total #users now %u",
					       job_ptr->user_id, nuser);
			} else {
				if (njobs[j] >= max_backfill_job_per_user) {
					/* skip job */
					if (debug_flags & DEBUG_FLAG_BACKFILL)
						info("backfill: have already "
						     "checked %u jobs for "
						     "user %u; skipping "
						     "job %u",
						     max_backfill_job_per_user,
						     job_ptr->user_id,
						     job_ptr->job_id);
					continue;
				}
			}
		}

		if (((part_ptr->state_up & PARTITION_SCHED) == 0) ||
		    (part_ptr->node_bitmap == NULL) ||
		    ((part_ptr->flags & PART_FLAG_ROOT_ONLY) && filter_root)) {
			if (debug_flags & DEBUG_FLAG_BACKFILL)
				info("backfill: partition %s not usable",
				     job_ptr->part_ptr->name);
			continue;
		}

		if ((!job_independent(job_ptr, 0)) ||
		    (license_job_test(job_ptr, time(NULL)) != SLURM_SUCCESS)) {
			if (debug_flags & DEBUG_FLAG_BACKFILL)
				info("backfill: job %u not runable now",
				     job_ptr->job_id);
			continue;
		}

		/* Determine minimum and maximum node counts */
		min_nodes = MAX(job_ptr->details->min_nodes,
				part_ptr->min_nodes);
		if (job_ptr->details->max_nodes == 0)
			max_nodes = part_ptr->max_nodes;
		else
			max_nodes = MIN(job_ptr->details->max_nodes,
					part_ptr->max_nodes);
		max_nodes = MIN(max_nodes, 500000);     /* prevent overflows */
		if (job_ptr->details->max_nodes)
			req_nodes = max_nodes;
		else
			req_nodes = min_nodes;
		if (min_nodes > max_nodes) {
			if (debug_flags & DEBUG_FLAG_BACKFILL)
				info("backfill: job %u node count too high",
				     job_ptr->job_id);
			continue;
		}

		/* Determine job's expected completion time */
		if (part_ptr->max_time == INFINITE)
			part_time_limit = YEAR_MINUTES;
		else
			part_time_limit = part_ptr->max_time;
		if (job_ptr->time_limit == NO_VAL) {
			time_limit = part_time_limit;
		} else {
			if (part_ptr->max_time == INFINITE)
				time_limit = job_ptr->time_limit;
			else
				time_limit = MIN(job_ptr->time_limit,
						 part_time_limit);
		}
		comp_time_limit = time_limit;
		qos_ptr = job_ptr->qos_ptr;
		if (qos_ptr && (qos_ptr->flags & QOS_FLAG_NO_RESERVE) &&
		    slurm_get_preempt_mode())
			time_limit = job_ptr->time_limit = 1;
		else if (job_ptr->time_min && (job_ptr->time_min < time_limit))
			time_limit = job_ptr->time_limit = job_ptr->time_min;

		/* Determine impact of any resource reservations */
		later_start = now;
 TRY_LATER:
		if (slurmctld_config.shutdown_time)
			break;
		if (((defer_rpc_cnt > 0) &&
		     (slurmctld_config.server_thread_count >= defer_rpc_cnt)) ||
		    (_delta_tv(&start_tv) >= sched_timeout)) {
			uint32_t save_job_id = job_ptr->job_id;
			uint32_t save_time_limit = job_ptr->time_limit;
			job_ptr->time_limit = orig_time_limit;
			if (debug_flags & DEBUG_FLAG_BACKFILL) {
				END_TIMER;
				info("backfill: completed yielding locks "
				     "after testing %u(%d) jobs, %s",
				     slurmctld_diag_stats.bf_last_depth,
				     job_test_count, TIME_STR);
			}
			if ((_yield_locks(yield_sleep) && !backfill_continue) ||
			    (slurmctld_conf.last_update != config_update) ||
			    (last_part_update != part_update)) {
				if (debug_flags & DEBUG_FLAG_BACKFILL) {
					info("backfill: system state changed, "
					     "breaking out after testing "
					     "%u(%d) jobs",
					     slurmctld_diag_stats.bf_last_depth,
					     job_test_count);
				}
				rc = 1;
				break;
			}
			/* cg_node_bitmap may be changed */
			bit_copybits(non_cg_bitmap, cg_node_bitmap);
			bit_not(non_cg_bitmap);

			/* With bf_continue configured, the original job could
			 * have been scheduled or cancelled and purged.
			 * Revalidate job the record here. */
			if ((job_ptr->magic  != JOB_MAGIC) ||
			    (job_ptr->job_id != save_job_id))
				continue;
			if (!IS_JOB_PENDING(job_ptr))
				continue;
			if (!avail_front_end(job_ptr))
				continue;	/* No available frontend */

			job_ptr->time_limit = save_time_limit;
			/* Reset backfill scheduling timers, resume testing */
			sched_start = time(NULL);
			gettimeofday(&start_tv, NULL);
			job_test_count = 1;
			START_TIMER;
		}

		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		start_res   = later_start;
		later_start = 0;
		j = job_test_resv(job_ptr, &start_res, true, &avail_bitmap,
				  &exc_core_bitmap, &resv_overlap);
		if (j != SLURM_SUCCESS) {
			if (debug_flags & DEBUG_FLAG_BACKFILL)
				info("backfill: job %u reservation defer",
				     job_ptr->job_id);
			job_ptr->time_limit = orig_time_limit;
			continue;
		}
		if (start_res > now)
			end_time = (time_limit * 60) + start_res;
		else
			end_time = (time_limit * 60) + now;
		resv_end = find_resv_end(start_res);
		/* Identify usable nodes for this job */
		bit_and(avail_bitmap, part_ptr->node_bitmap);
		bit_and(avail_bitmap, up_node_bitmap);
		bit_and(avail_bitmap, non_cg_bitmap);
		for (j=0; ; ) {
			if ((node_space[j].end_time > start_res) &&
			     node_space[j].next && (later_start == 0))
				later_start = node_space[j].end_time;
			if (node_space[j].end_time <= start_res)
				;
			else if (node_space[j].begin_time <= end_time) {
				bit_and(avail_bitmap,
					node_space[j].avail_bitmap);
			} else
				break;
			if ((j = node_space[j].next) == 0)
				break;
		}
		if (resv_end && (++resv_end < window_end) &&
		    ((later_start == 0) || (resv_end < later_start))) {
			later_start = resv_end;
		}

		if (job_ptr->details->exc_node_bitmap) {
			bit_not(job_ptr->details->exc_node_bitmap);
			bit_and(avail_bitmap,
				job_ptr->details->exc_node_bitmap);
			bit_not(job_ptr->details->exc_node_bitmap);
		}

		/* Test if insufficient nodes remain OR
		 *	required nodes missing OR
		 *	nodes lack features OR
		 *	no change since previously tested nodes (only changes
		 *	in other partition nodes) */
		if ((bit_set_count(avail_bitmap) < min_nodes) ||
		    ((job_ptr->details->req_node_bitmap) &&
		     (!bit_super_set(job_ptr->details->req_node_bitmap,
				     avail_bitmap))) ||
		    (job_req_node_filter(job_ptr, avail_bitmap))) {
			if (later_start) {
				job_ptr->start_time = 0;
				goto TRY_LATER;
			}

			/* Job can not start until too far in the future */
			job_ptr->time_limit = orig_time_limit;
			job_ptr->start_time = sched_start + backfill_window;
			if ((orig_start_time != 0) &&
			    (orig_start_time < job_ptr->start_time)) {
				/* Can start earlier in different partition */
				job_ptr->start_time = orig_start_time;
			}
			continue;
		}

		/* Identify nodes which are definitely off limits */
		FREE_NULL_BITMAP(resv_bitmap);
		resv_bitmap = bit_copy(avail_bitmap);
		bit_not(resv_bitmap);

		/* this is the time consuming operation */
		debug2("backfill: entering _try_sched for job %u.",
		       job_ptr->job_id);

		if (!already_counted) {
			slurmctld_diag_stats.bf_last_depth_try++;
			already_counted = true;
		}

		if (debug_flags & DEBUG_FLAG_BACKFILL_MAP)
			_dump_job_test(job_ptr, avail_bitmap, start_res);
		j = _try_sched(job_ptr, &avail_bitmap, min_nodes, max_nodes,
			       req_nodes, exc_core_bitmap);

		now = time(NULL);
		if (j != SLURM_SUCCESS) {
			job_ptr->time_limit = orig_time_limit;
			if (orig_start_time != 0)  /* Can start in other part */
				job_ptr->start_time = orig_start_time;
			else
				job_ptr->start_time = 0;
			continue;	/* not runable in this partition */
		}

		if (start_res > job_ptr->start_time) {
			job_ptr->start_time = start_res;
			last_job_update = now;
		}
		if (job_ptr->start_time <= now) {	/* Can start now */
			uint32_t save_time_limit = job_ptr->time_limit;
			uint32_t hard_limit;
			bool reset_time = false;
			int rc = _start_job(job_ptr, resv_bitmap);
			if (qos_ptr && (qos_ptr->flags & QOS_FLAG_NO_RESERVE)) {
				if (orig_time_limit == NO_VAL) {
					acct_policy_alter_job(
						job_ptr, comp_time_limit);
					job_ptr->time_limit = comp_time_limit;
				} else {
					acct_policy_alter_job(
						job_ptr, orig_time_limit);
					job_ptr->time_limit = orig_time_limit;
				}
			} else if ((rc == SLURM_SUCCESS) && job_ptr->time_min) {
				/* Set time limit as high as possible */
				acct_policy_alter_job(job_ptr, comp_time_limit);
				job_ptr->time_limit = comp_time_limit;
				reset_time = true;
			} else if (orig_time_limit == NO_VAL) {
				acct_policy_alter_job(job_ptr, comp_time_limit);
				job_ptr->time_limit = comp_time_limit;
			} else {
				acct_policy_alter_job(job_ptr, orig_time_limit);
				job_ptr->time_limit = orig_time_limit;

			}
			/* Only set end_time if start_time is set,
			 * or else end_time will be small (ie. 1969). */
			if (job_ptr->start_time) {
				if (job_ptr->time_limit == INFINITE)
					hard_limit = YEAR_MINUTES;
				else
					hard_limit = job_ptr->time_limit;
				job_ptr->end_time = job_ptr->start_time +
					(hard_limit * 60);
				/* Only set if start_time. end_time must be set
				 * beforehand for _reset_job_time_limit. */
				if (reset_time) {
					_reset_job_time_limit(job_ptr, now,
							      node_space);
					time_limit = job_ptr->time_limit;
				}
			} else if (rc == SLURM_SUCCESS) {
				error("%s: start_time of 0 on successful "
				      "backfill. This shouldn't happen. :)",
				      __func__);
			}

			if ((rc == ESLURM_ACCOUNTING_POLICY) ||
			    (rc == ESLURM_RESERVATION_BUSY)) {
				/* Unknown future start time, just skip job */
				if (orig_start_time != 0) {
					/* Can start in different partition */
					job_ptr->start_time = orig_start_time;
				} else
					job_ptr->start_time = 0;
				continue;
			} else if (rc != SLURM_SUCCESS) {
				if (debug_flags & DEBUG_FLAG_BACKFILL) {
					info("backfill: planned start of job %u"
					     " failed: %s", job_ptr->job_id,
					     slurm_strerror(rc));
				}
				/* Drop through and reserve these resources.
				 * Likely due to state changes during sleep.
				 * Make best-effort based upon original state */
				job_ptr->time_limit = orig_time_limit;
				later_start = 0;
			} else {
				/* Started this job, move to next one */
				reject_array_job_id = 0;
				reject_array_part   = NULL;

				/* Update the database if job time limit
				 * changed and move to next job */
				if (save_time_limit != job_ptr->time_limit &&
				    (!with_slurmdbd || job_ptr->db_index))
					jobacct_storage_g_job_start(acct_db_conn,
								    job_ptr);
				job_start_cnt++;
				if (max_backfill_jobs_start &&
				    (job_start_cnt >= max_backfill_jobs_start)){
					if (debug_flags & DEBUG_FLAG_BACKFILL) {
						info("backfill: bf_max_job_start"
						     " limit of %d reached",
						     max_backfill_jobs_start);
					}
					break;
				}
				if (job_ptr->array_task_id != NO_VAL) {
					/* Try starting next task of job array */
					job_ptr = find_job_record(job_ptr->
								  array_job_id);
					if (job_ptr && IS_JOB_PENDING(job_ptr))
						goto next_task;
				}
				continue;
			}
		} else {
			job_ptr->time_limit = orig_time_limit;
		}

		start_time  = job_ptr->start_time;
		end_reserve = job_ptr->start_time + (time_limit * 60);
		start_time  = (start_time / backfill_resolution) *
			      backfill_resolution;
		end_reserve = (end_reserve / backfill_resolution) *
			      backfill_resolution;

		if (later_start && (start_time > later_start)) {
			/* Try later when some nodes currently reserved for
			 * pending jobs are free */
			job_ptr->start_time = 0;
			goto TRY_LATER;
		}

		if (job_ptr->start_time > (sched_start + backfill_window)) {
			/* Starts too far in the future to worry about */
			if (debug_flags & DEBUG_FLAG_BACKFILL)
				_dump_job_sched(job_ptr, end_reserve,
						avail_bitmap);
			if ((orig_start_time != 0) &&
			    (orig_start_time < job_ptr->start_time)) {
				/* Can start earlier in different partition */
				job_ptr->start_time = orig_start_time;
			}
			continue;
		}

		if (node_space_recs >= max_backfill_job_cnt) {
			if (debug_flags & DEBUG_FLAG_BACKFILL) {
				info("backfill: table size limit of %u reached",
				     max_backfill_job_cnt);
			}
			break;
		}

		if ((job_ptr->start_time > now) &&
		    _test_resv_overlap(node_space, avail_bitmap,
				       start_time, end_reserve)) {
			/* This job overlaps with an existing reservation for
			 * job to be backfill scheduled, which the sched
			 * plugin does not know about. Try again later. */
			later_start = job_ptr->start_time;
			job_ptr->start_time = 0;
			goto TRY_LATER;
		}

		/*
		 * Add reservation to scheduling table if appropriate
		 */
		if (debug_flags & DEBUG_FLAG_BACKFILL)
			_dump_job_sched(job_ptr, end_reserve, avail_bitmap);
		if (qos_ptr && (qos_ptr->flags & QOS_FLAG_NO_RESERVE))
			continue;
		if (bf_min_age_reserve && job_ptr->details->begin_time) {
			pend_time = difftime(time(NULL),
					     job_ptr->details->begin_time);
			if (pend_time < bf_min_age_reserve)
				continue;
		}
		reject_array_job_id = 0;
		reject_array_part   = NULL;
		xfree(job_ptr->sched_nodes);
		job_ptr->sched_nodes = bitmap2node_name(avail_bitmap);
		bit_not(avail_bitmap);
		_add_reservation(start_time, end_reserve,
				 avail_bitmap, node_space, &node_space_recs);
		if (debug_flags & DEBUG_FLAG_BACKFILL_MAP)
			_dump_node_space_table(node_space);
		if ((orig_start_time != 0) &&
		    (orig_start_time < job_ptr->start_time)) {
			/* Can start earlier in different partition */
			job_ptr->start_time = orig_start_time;
		}
		if ((job_ptr->array_task_id != NO_VAL) && job_ptr->array_recs) {
			/* Try making reservation for next task of job array */
			if (test_array_job_id != job_ptr->array_job_id) {
				test_array_job_id = job_ptr->array_job_id;
				test_array_count = 1;
			} else {
				test_array_count++;
			}
			if ((test_array_count < bf_max_job_array_resv) &&
			    (test_array_count < job_ptr->array_recs->task_cnt))
				goto next_task;
		}
	}
	xfree(bf_part_jobs);
	xfree(bf_part_ptr);
	xfree(uid);
	xfree(njobs);
	FREE_NULL_BITMAP(avail_bitmap);
	FREE_NULL_BITMAP(exc_core_bitmap);
	FREE_NULL_BITMAP(resv_bitmap);
	FREE_NULL_BITMAP(non_cg_bitmap);

	for (i=0; ; ) {
		FREE_NULL_BITMAP(node_space[i].avail_bitmap);
		if ((i = node_space[i].next) == 0)
			break;
	}
	xfree(node_space);
	list_destroy(job_queue);
	gettimeofday(&bf_time2, NULL);
	_do_diag_stats(&bf_time1, &bf_time2, yield_sleep);
	if (debug_flags & DEBUG_FLAG_BACKFILL) {
		END_TIMER;
		info("backfill: completed testing %u(%d) jobs, %s",
		     slurmctld_diag_stats.bf_last_depth,
		     job_test_count, TIME_STR);
	}
	return rc;
}

/* Try to start the job on any non-reserved nodes */
static int _start_job(struct job_record *job_ptr, bitstr_t *resv_bitmap)
{
	int rc;
	bitstr_t *orig_exc_nodes = NULL;
	bool is_job_array_head = false;
	static uint32_t fail_jobid = 0;

	if (job_ptr->details->exc_node_bitmap) {
		orig_exc_nodes = bit_copy(job_ptr->details->exc_node_bitmap);
		bit_or(job_ptr->details->exc_node_bitmap, resv_bitmap);
	} else
		job_ptr->details->exc_node_bitmap = bit_copy(resv_bitmap);
	if (job_ptr->array_recs)
		is_job_array_head = true;
	rc = select_nodes(job_ptr, false, NULL, NULL);
	if (is_job_array_head && job_ptr->details) {
		struct job_record *base_job_ptr;
		base_job_ptr = find_job_record(job_ptr->array_job_id);
		if (base_job_ptr && base_job_ptr != job_ptr
				 && base_job_ptr->array_recs) {
			FREE_NULL_BITMAP(
					base_job_ptr->details->exc_node_bitmap);
			if (orig_exc_nodes)
				base_job_ptr->details->exc_node_bitmap =
					bit_copy(orig_exc_nodes);
		}
	}
	if (job_ptr->details) { /* select_nodes() might cancel the job! */
		FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
		job_ptr->details->exc_node_bitmap = orig_exc_nodes;
	} else
		FREE_NULL_BITMAP(orig_exc_nodes);
	if (rc == SLURM_SUCCESS) {
		/* job initiated */
		last_job_update = time(NULL);
		if (job_ptr->array_task_id == NO_VAL) {
			info("backfill: Started JobId=%u on %s",
			     job_ptr->job_id, job_ptr->nodes);
		} else {
			info("backfill: Started JobId=%u_%u (%u) on %s",
			     job_ptr->array_job_id, job_ptr->array_task_id,
			     job_ptr->job_id, job_ptr->nodes);
		}
		if (job_ptr->batch_flag == 0)
			srun_allocate(job_ptr->job_id);
		else if ((job_ptr->details == NULL) ||
			 (job_ptr->details->prolog_running == 0))
			launch_job(job_ptr);
		slurmctld_diag_stats.backfilled_jobs++;
		slurmctld_diag_stats.last_backfilled_jobs++;
		if (debug_flags & DEBUG_FLAG_BACKFILL) {
			info("backfill: Jobs backfilled since boot: %u",
			     slurmctld_diag_stats.backfilled_jobs);
		}
	} else if ((job_ptr->job_id != fail_jobid) &&
		   (rc != ESLURM_ACCOUNTING_POLICY)) {
		char *node_list;
		bit_not(resv_bitmap);
		node_list = bitmap2node_name(resv_bitmap);
		/* This happens when a job has sharing disabled and
		 * a selected node is still completing some job,
		 * which should be a temporary situation. */
		verbose("backfill: Failed to start JobId=%u on %s: %s",
			job_ptr->job_id, node_list, slurm_strerror(rc));
		xfree(node_list);
		fail_jobid = job_ptr->job_id;
	} else {
		debug3("backfill: Failed to start JobId=%u: %s",
		       job_ptr->job_id, slurm_strerror(rc));
	}

	return rc;
}

/* Reset a job's time limit (and end_time) as high as possible
 *	within the range job_ptr->time_min and job_ptr->time_limit.
 *	Avoid using resources reserved for pending jobs or in resource
 *	reservations */
static void _reset_job_time_limit(struct job_record *job_ptr, time_t now,
				  node_space_map_t *node_space)
{
	int32_t j, resv_delay;
	uint32_t orig_time_limit = job_ptr->time_limit;
	uint32_t new_time_limit;

	for (j=0; ; ) {
		if ((node_space[j].begin_time != now) &&
		    (node_space[j].begin_time < job_ptr->end_time) &&
		    (!bit_super_set(job_ptr->node_bitmap,
				    node_space[j].avail_bitmap))) {
			/* Job overlaps pending job's resource reservation */
			resv_delay = difftime(node_space[j].begin_time, now);
			resv_delay /= 60;	/* seconds to minutes */
			if (resv_delay < job_ptr->time_limit)
				job_ptr->time_limit = resv_delay;
		}
		if ((j = node_space[j].next) == 0)
			break;
	}
	new_time_limit = MAX(job_ptr->time_min, job_ptr->time_limit);
	acct_policy_alter_job(job_ptr, new_time_limit);
	job_ptr->time_limit = new_time_limit;
	job_ptr->end_time = job_ptr->start_time + (job_ptr->time_limit * 60);

	job_time_adj_resv(job_ptr);

	if (orig_time_limit != job_ptr->time_limit) {
		info("backfill: job %u time limit changed from %u to %u",
		     job_ptr->job_id, orig_time_limit, job_ptr->time_limit);
	}
}

/* Report if any changes occurred to job, node or partition information */
static bool _more_work (time_t last_backfill_time)
{
	bool rc = false;

	pthread_mutex_lock( &thread_flag_mutex );
	if ( (last_job_update  >= last_backfill_time ) ||
	     (last_node_update >= last_backfill_time ) ||
	     (last_part_update >= last_backfill_time ) ) {
		rc = true;
	}
	pthread_mutex_unlock( &thread_flag_mutex );
	return rc;
}

/* Create a reservation for a job in the future */
static void _add_reservation(uint32_t start_time, uint32_t end_reserve,
			     bitstr_t *res_bitmap,
			     node_space_map_t *node_space,
			     int *node_space_recs)
{
	bool placed = false;
	int i, j;

#if 0	
	info("add job start:%u end:%u", start_time, end_reserve);
	for (j = 0; ; ) {
		info("node start:%u end:%u",
		     (uint32_t) node_space[j].begin_time,
		     (uint32_t) node_space[j].end_time);
		if ((j = node_space[j].next) == 0)
			break;
	}
#endif

	start_time = MAX(start_time, node_space[0].begin_time);
	for (j = 0; ; ) {
		if (node_space[j].end_time > start_time) {
			/* insert start entry record */
			i = *node_space_recs;
			node_space[i].begin_time = start_time;
			node_space[i].end_time = node_space[j].end_time;
			node_space[j].end_time = start_time;
			node_space[i].avail_bitmap =
				bit_copy(node_space[j].avail_bitmap);
			node_space[i].next = node_space[j].next;
			node_space[j].next = i;
			(*node_space_recs)++;
			placed = true;
		}
		if (node_space[j].end_time == start_time) {
			/* no need to insert new start entry record */
			placed = true;
		}
		if (placed == true) {
			while ((j = node_space[j].next)) {
				if (end_reserve < node_space[j].end_time) {
					/* insert end entry record */
					i = *node_space_recs;
					node_space[i].begin_time = end_reserve;
					node_space[i].end_time = node_space[j].
								 end_time;
					node_space[j].end_time = end_reserve;
					node_space[i].avail_bitmap =
						bit_copy(node_space[j].
							 avail_bitmap);
					node_space[i].next = node_space[j].next;
					node_space[j].next = i;
					(*node_space_recs)++;
					break;
				}
				if (end_reserve == node_space[j].end_time) {
					break;
				}
			}
			break;
		}
		if ((j = node_space[j].next) == 0)
			break;
	}

	for (j = 0; ; ) {
		if ((node_space[j].begin_time >= start_time) &&
		    (node_space[j].end_time <= end_reserve))
			bit_and(node_space[j].avail_bitmap, res_bitmap);
		if ((node_space[j].begin_time >= end_reserve) ||
		    ((j = node_space[j].next) == 0))
			break;
	}

	/* Drop records with identical bitmaps (up to one record).
	 * This can significantly improve performance of the backfill tests. */
	for (i = 0; ; ) {
		if ((j = node_space[i].next) == 0)
			break;
		if (!bit_equal(node_space[i].avail_bitmap,
			       node_space[j].avail_bitmap)) {
			i = j;
			continue;
		}
		node_space[i].end_time = node_space[j].end_time;
		node_space[i].next = node_space[j].next;
		FREE_NULL_BITMAP(node_space[j].avail_bitmap);
		break;
	}
}

/*
 * Determine if the resource specification for a new job overlaps with a
 *	reservation that the backfill scheduler has made for a job to be
 *	started in the future.
 * IN use_bitmap - nodes to be allocated
 * IN start_time - start time of job
 * IN end_reserve - end time of job
 */
static bool _test_resv_overlap(node_space_map_t *node_space,
			       bitstr_t *use_bitmap, uint32_t start_time,
			       uint32_t end_reserve)
{
	bool overlap = false;
	int j;

	for (j=0; ; ) {
		if ((node_space[j].end_time   > start_time) &&
		    (node_space[j].begin_time < end_reserve) &&
		    (!bit_super_set(use_bitmap, node_space[j].avail_bitmap))) {
			overlap = true;
			break;
		}
		if ((j = node_space[j].next) == 0)
			break;
	}
	return overlap;
}
