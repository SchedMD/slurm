/*****************************************************************************\
 *  backfill.c - simple backfill scheduler plugin.
 *
 *  If a partition does not have root only access and nodes are not shared
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

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"
#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/parse_time.h"
#include "src/common/power.h"
#include "src/common/read_config.h"
#include "src/common/select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_mcs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/acct_policy.h"
#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/fed_mgr.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/gres_ctld.h"
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
#define BF_MAX_JOB_ARRAY_RESV	20

#define SLURMCTLD_THREAD_LIMIT	5
#define YIELD_INTERVAL		2000000	/* time in micro-seconds */
#define YIELD_SLEEP		500000;	/* time in micro-seconds */

#define MAX_BACKFILL_INTERVAL          10800 /* 3 hours */
#define MAX_BACKFILL_RESOLUTION        3600 /* 1 hour */
#define MAX_BACKFILL_WINDOW            (30 * 24 * 60 * 60) /* 30 days */
#define MAX_BF_JOB_PART_COUNT_RESERVE  100000
#define MAX_BF_MAX_JOB_ARRAY_RESV      1000
#define MAX_BF_MAX_JOB_START           10000
#define DEF_BF_MAX_JOB_TEST            500
#define MAX_BF_MAX_JOB_TEST            1000000
#define MAX_BF_MAX_TIME                3600
#define MAX_BF_MIN_AGE_RESERVE         (30 * 24 * 60 * 60) /* 30 days */
#define MAX_BF_MIN_PRIO_RESERVE        INFINITE
#define MAX_BF_YIELD_INTERVAL          10000000 /* 10 seconds in usec */
#define MAX_MAX_RPC_CNT                1000
#define MAX_YIELD_SLEEP                10000000 /* 10 seconds in usec */

#define MAX_BF_MAX_JOB_ASSOC           MAX_BF_MAX_JOB_TEST
#define MAX_BF_MAX_JOB_USER            MAX_BF_MAX_JOB_TEST
#define MAX_BF_MAX_JOB_USER_PART       MAX_BF_MAX_JOB_TEST
#define MAX_BF_MAX_JOB_PART            MAX_BF_MAX_JOB_TEST

typedef struct {
	time_t begin_time;
	time_t end_time;
	bitstr_t *avail_bitmap;
	bf_licenses_t *licenses;
	int next;	/* next record, by time, zero termination */
} node_space_map_t;

typedef struct {
	node_space_map_t *node_space;
	int *node_space_recs;
} node_space_handler_t;

/*
 * HetJob scheduling structures
 * NOTE: An individial hetjob component can be submitted to multiple
 *       partitions and have different start times in each
 */
typedef struct {
	uint32_t job_id;
	job_record_t *job_ptr;
	time_t latest_start;		/* Time when expected to start */
	part_record_t *part_ptr;
} het_job_rec_t;

typedef struct {
	uint32_t comp_time_limit;	/* Time limit for hetjob */
	uint32_t het_job_id;
	List het_job_rec_list;		/* List of het_job_rec_t */
	time_t prev_start;		/* Expected start time from last test */
} het_job_map_t;

typedef struct {
	uint32_t het_job_id;
	time_t start_time;
} deadlock_job_struct_t;

typedef struct {
	List deadlock_job_list;
	part_record_t *part_ptr;
} deadlock_part_struct_t;

/* Diagnostic  statistics */
extern diag_stats_t slurmctld_diag_stats;
uint32_t bf_sleep_usec = 0;

typedef struct {
	slurmdb_bf_usage_t bf_usage;
	uid_t uid;
} bf_user_usage_t;

/*********************** local variables *********************/
static bool stop_backfill = false;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
static bool config_flag = false;
static int backfill_interval = BACKFILL_INTERVAL;
static int bf_max_time = BACKFILL_INTERVAL;
static int backfill_resolution = BACKFILL_RESOLUTION;
static int backfill_window = BACKFILL_WINDOW;
static int bf_job_part_count_reserve = 0;
static int bf_max_job_array_resv = BF_MAX_JOB_ARRAY_RESV;
static int bf_min_age_reserve = 0;
static int bf_node_space_size = 0;
static bool bf_running_job_reserve = false;
static bool bf_licenses = false;
static uint32_t bf_min_prio_reserve = 0;
static List deadlock_global_list;
static bool bf_hetjob_immediate = false;
static uint16_t bf_hetjob_prio = 0;
static bool bf_one_resv_per_job = false;
static uint32_t job_start_cnt = 0;
static int max_backfill_job_cnt = DEF_BF_MAX_JOB_TEST;
static int max_backfill_job_per_assoc = 0;
static int max_backfill_job_per_part = 0;
static int max_backfill_job_per_user = 0;
static int max_backfill_job_per_user_part = 0;
static int max_backfill_jobs_start = 0;
static bool backfill_continue = false;
static bool assoc_limit_stop = false;
static int max_rpc_cnt = 0;
static int yield_interval = YIELD_INTERVAL;
static int yield_sleep   = YIELD_SLEEP;
static List het_job_list = NULL;
static xhash_t *user_usage_map = NULL; /* look up user usage when no assoc */
static bitstr_t *planned_bitmap = NULL;

/*********************** local functions *********************/
static void _add_reservation(uint32_t start_time, uint32_t end_reserve,
			     bitstr_t *res_bitmap, job_record_t *job_ptr,
			     node_space_map_t *node_space,
			     int *node_space_recs);
static void _adjust_hetjob_prio(uint32_t *prio, uint32_t val);
static void _attempt_backfill(void);
static int  _clear_job_estimates(void *x, void *arg);
static int  _clear_qos_blocked_times(void *x, void *arg);
static void _do_diag_stats(struct timeval *tv1, struct timeval *tv2,
			   int node_space_recs);
static uint32_t _get_job_max_tl(job_record_t *job_ptr, time_t now,
				node_space_map_t *node_space);
static bool _hetjob_any_resv(job_record_t *het_leader);
static uint32_t _hetjob_calc_prio(job_record_t *het_leader);
static uint32_t _hetjob_calc_prio_tier(job_record_t *het_leader);
static void _het_job_deadlock_fini(void);
static bool _het_job_deadlock_test(job_record_t *job_ptr);
static bool _job_part_valid(job_record_t *job_ptr, part_record_t *part_ptr);
static void _load_config(void);
static bool _many_pending_rpcs(void);
static bool _more_work(time_t last_backfill_time);
static uint32_t _my_sleep(int64_t usec);
static int  _num_feature_count(job_record_t *job_ptr, bool *has_xand,
			       bool *has_xor);
static int  _het_job_find_map(void *x, void *key);
static void _het_job_map_del(void *x);
static void _het_job_start_clear(void);
static time_t _het_job_start_find(job_record_t *job_ptr);
static void _het_job_start_set(job_record_t *job_ptr, time_t latest_start,
			       uint32_t comp_time_limit);
static void _het_job_start_test_single(node_space_map_t *node_space,
				       het_job_map_t *map, bool single);
static int  _het_job_start_test_list(void *map, void *node_space);
static void _het_job_start_test(node_space_map_t *node_space,
				uint32_t het_job_id);
static void _reset_job_time_limit(job_record_t *job_ptr, time_t now,
				  node_space_map_t *node_space);
static int  _set_hetjob_details(void *x, void *arg);
static int  _start_job(job_record_t *job_ptr, bitstr_t *avail_bitmap);
static bool _test_resv_overlap(node_space_map_t *node_space,
			       bitstr_t *use_bitmap, job_record_t *job_ptr,
			       uint32_t start_time, uint32_t end_reserve);
static int  _try_sched(job_record_t *job_ptr, bitstr_t **avail_bitmap,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, bitstr_t *exc_core_bitmap);
static int  _yield_locks(int64_t usec);
static void _bf_map_key_id(void *item, const char **key, uint32_t *key_len);
static void _bf_map_free(void *item);

/* Log resources to be allocated to a pending job */
static void _dump_job_sched(job_record_t *job_ptr, time_t end_time,
			    bitstr_t *avail_bitmap)
{
	char begin_buf[32], end_buf[32], *node_list;

	slurm_make_time_str(&job_ptr->start_time, begin_buf, sizeof(begin_buf));
	slurm_make_time_str(&end_time, end_buf, sizeof(end_buf));
	node_list = bitmap2node_name(avail_bitmap);
	info("%pJ to start at %s, end at %s on nodes %s in partition %s",
	     job_ptr, begin_buf, end_buf, node_list, job_ptr->part_ptr->name);
	xfree(node_list);
}

static void _dump_job_test(job_record_t *job_ptr, bitstr_t *avail_bitmap,
			   time_t start_time)
{
	char begin_buf[32], *node_list;

	if (start_time == 0)
		strcpy(begin_buf, "NOW");
	else
		slurm_make_time_str(&start_time, begin_buf, sizeof(begin_buf));
	node_list = bitmap2node_name(avail_bitmap);
	info("Test %pJ at %s on %s", job_ptr, begin_buf, node_list);
	xfree(node_list);
}

/* Log resource allocate table */
static void _dump_node_space_table(node_space_map_t *node_space_ptr)
{
	int i = 0;
	char begin_buf[32], end_buf[32], *node_list, *licenses;

	info("=========================================");
	while (1) {
		slurm_make_time_str(&node_space_ptr[i].begin_time,
				    begin_buf, sizeof(begin_buf));
		slurm_make_time_str(&node_space_ptr[i].end_time,
				    end_buf, sizeof(end_buf));
		node_list = bitmap2node_name(node_space_ptr[i].avail_bitmap);
		licenses = bf_licenses_to_string(node_space_ptr[i].licenses);
		info("Begin:%s End:%s Nodes:%s Licenses:%s",
		     begin_buf, end_buf, node_list, licenses);
		xfree(node_list);
		xfree(licenses);
		if ((i = node_space_ptr[i].next) == 0)
			break;
	}
	info("=========================================");
}

static void _set_job_time_limit(job_record_t *job_ptr, uint32_t new_limit)
{
	job_ptr->time_limit = new_limit;
	/* reset flag if we have a NO_VAL time_limit */
	if (job_ptr->time_limit == NO_VAL)
		job_ptr->limit_set.time = 0;

}

/*
 * _many_pending_rpcs - Determine if slurmctld is busy with many active RPCs
 * RET - True if slurmctld currently has more than SLURMCTLD_THREAD_LIMIT
 *	 active RPCs
 */
static bool _many_pending_rpcs(void)
{
	bool many_pending_rpcs = false;

	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	//info("thread_count = %u", slurmctld_config.server_thread_count);
	if ((max_rpc_cnt > 0) &&
	    (slurmctld_config.server_thread_count >= max_rpc_cnt))
		many_pending_rpcs = true;
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

	return many_pending_rpcs;

}

/*
 * Report summary of job's feature specification
 * IN job_ptr - job to schedule
 * OUT has_xand - true if features are XANDed together
 * OUT has_xor - true if features are XORed together
 * RET Total count for ALL job features, even counts with XAND separator
 */
static int _num_feature_count(job_record_t *job_ptr, bool *has_xand,
			      bool *has_xor)
{
	struct job_details *detail_ptr = job_ptr->details;
	int rc = 0;
	ListIterator feat_iter;
	job_feature_t *feat_ptr;

	*has_xand = false;
	*has_xor = false;
	if (detail_ptr->feature_list_use == NULL)	/* no constraints */
		return rc;

	feat_iter = list_iterator_create(detail_ptr->feature_list_use);
	while ((feat_ptr = list_next(feat_iter))) {
		if (feat_ptr->count)
			rc++;
		if (feat_ptr->op_code == FEATURE_OP_XAND)
			*has_xand = true;
		if (feat_ptr->op_code == FEATURE_OP_XOR)
			*has_xor = true;
	}
	list_iterator_destroy(feat_iter);

	return rc;
}

static int _clear_qos_blocked_times(void *x, void *arg)
{
	slurmdb_qos_rec_t *qos_ptr = (slurmdb_qos_rec_t *) x;
	qos_ptr->blocked_until = 0;

	return 0;
}

/*
 * Attempt to schedule a specific job on specific available nodes
 * IN job_ptr - job to schedule
 * IN/OUT avail_bitmap - nodes available/selected to use
 * IN exc_core_bitmap - cores which can not be used
 * RET SLURM_SUCCESS on success, otherwise an error code
 */
static int  _try_sched(job_record_t *job_ptr, bitstr_t **avail_bitmap,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, bitstr_t *exc_core_bitmap)
{
	bitstr_t *low_bitmap = NULL, *tmp_bitmap = NULL;
	int rc = SLURM_SUCCESS;
	bool has_xand = false, has_xor = false;
	int feat_cnt = _num_feature_count(job_ptr, &has_xand, &has_xor);
	struct job_details *detail_ptr = job_ptr->details;
	List feature_cache = detail_ptr->feature_list_use;
	List preemptee_candidates = NULL;
	ListIterator feat_iter;
	job_feature_t *feat_ptr;
	job_feature_t *feature_base;

	if (has_xand || feat_cnt) {
		/*
		 * Cache the feature information and test the individual
		 * features (or sets of features in parenthesis), one at a time
		 */
		time_t high_start = 0;
		uint32_t feat_min_node;
		uint32_t feat_node_cnt;

		tmp_bitmap = bit_copy(*avail_bitmap);
		preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);
		feat_iter = list_iterator_create(feature_cache);
		while ((feat_ptr = list_next(feat_iter)) &&
		       (rc == SLURM_SUCCESS)) {
			detail_ptr->feature_list_use =
				list_create(feature_list_delete);
			feature_base = xmalloc(sizeof(job_feature_t));
			feature_base->name = xstrdup(feat_ptr->name);
			feature_base->op_code = feat_ptr->op_code;
			list_append(detail_ptr->feature_list_use, feature_base);
			feat_min_node = feat_ptr->count;
			while ((feat_ptr->paren > 0) &&
			       ((feat_ptr = list_next(feat_iter)))) {
				feature_base = xmalloc(sizeof(job_feature_t));
				feature_base->name = xstrdup(feat_ptr->name);
				feature_base->op_code = feat_ptr->op_code;
				feat_min_node = feat_ptr->count;
				list_append(detail_ptr->feature_list_use,
					    feature_base);
			}
			feature_base->op_code = FEATURE_OP_END;
			feat_min_node = MAX(1, feat_min_node);

			if ((job_req_node_filter(job_ptr, *avail_bitmap, true)
			     == SLURM_SUCCESS) &&
			    (bit_set_count(*avail_bitmap) >= feat_min_node)) {
				rc = select_g_job_test(job_ptr, *avail_bitmap,
						       feat_min_node, max_nodes,
						       feat_min_node,
						       SELECT_MODE_WILL_RUN,
						       preemptee_candidates,
						       NULL,
						       exc_core_bitmap);
				if (rc == SLURM_SUCCESS) {
					if ((high_start == 0) ||
					    (high_start < job_ptr->start_time))
						high_start =
							job_ptr->start_time;

					if (low_bitmap) {
						bit_or(low_bitmap,
							*avail_bitmap);
					} else {
						low_bitmap = *avail_bitmap;
						*avail_bitmap = NULL;
					}
				}
			} else {
				rc = ESLURM_NODES_BUSY;
			}
			FREE_NULL_BITMAP(*avail_bitmap);
			*avail_bitmap = bit_copy(tmp_bitmap);
			if (low_bitmap)
				bit_and_not(*avail_bitmap, low_bitmap);
			list_destroy(detail_ptr->feature_list_use);
		}
		list_iterator_destroy(feat_iter);

		if (low_bitmap)
			feat_node_cnt = bit_set_count(low_bitmap);
		else
			feat_node_cnt = 0;
		if (feat_node_cnt < req_nodes) {
			detail_ptr->feature_list_use = NULL;
			rc = select_g_job_test(job_ptr, *avail_bitmap,
					       min_nodes - feat_node_cnt,
					       max_nodes - feat_node_cnt,
					       req_nodes - feat_node_cnt,
					       SELECT_MODE_WILL_RUN,
					       preemptee_candidates,
					       NULL,
					       exc_core_bitmap);

			if (low_bitmap) {
				bit_or(low_bitmap, *avail_bitmap);
			} else {
				low_bitmap = *avail_bitmap;
				*avail_bitmap = NULL;
			}
		}
		FREE_NULL_LIST(preemptee_candidates);
		FREE_NULL_BITMAP(tmp_bitmap);
		if (high_start && rc == SLURM_SUCCESS) {
			job_ptr->start_time = high_start;
			FREE_NULL_BITMAP(*avail_bitmap);
			*avail_bitmap = low_bitmap;
		} else {
			rc = ESLURM_NODES_BUSY;
			job_ptr->start_time = 0;
			FREE_NULL_BITMAP(*avail_bitmap);
			FREE_NULL_BITMAP(low_bitmap);
		}

		/* Restore the original feature information */
		detail_ptr->feature_list_use = feature_cache;
	} else if (has_xor) {
		/*
		 * Cache the feature information and test the individual
		 * features (or sets of features in parenthesis), one at a time
		 */
		time_t low_start = 0;

		tmp_bitmap = bit_copy(*avail_bitmap);
		preemptee_candidates = slurm_find_preemptable_jobs(job_ptr);
		feat_iter = list_iterator_create(feature_cache);
		while ((feat_ptr = list_next(feat_iter))) {
			detail_ptr->feature_list_use =
				list_create(feature_list_delete);
			feature_base = xmalloc(sizeof(job_feature_t));
			feature_base->name = xstrdup(feat_ptr->name);
			feature_base->op_code = feat_ptr->op_code;
			list_append(detail_ptr->feature_list_use, feature_base);
			while ((feat_ptr->paren > 0) &&
			       ((feat_ptr = list_next(feat_iter)))) {
				feature_base = xmalloc(sizeof(job_feature_t));
				feature_base->name = xstrdup(feat_ptr->name);
				feature_base->op_code = feat_ptr->op_code;
				list_append(detail_ptr->feature_list_use,
					    feature_base);
			}
			feature_base->op_code = FEATURE_OP_END;

			if ((job_req_node_filter(job_ptr, *avail_bitmap, true)
			     == SLURM_SUCCESS) &&
			    (bit_set_count(*avail_bitmap) >= min_nodes)) {
				rc = select_g_job_test(job_ptr, *avail_bitmap,
						       min_nodes, max_nodes,
						       req_nodes,
						       SELECT_MODE_WILL_RUN,
						       preemptee_candidates,
						       NULL,
						       exc_core_bitmap);
				if ((rc == SLURM_SUCCESS) &&
				    ((low_start == 0) ||
				     (low_start > job_ptr->start_time))) {
					low_start = job_ptr->start_time;
					low_bitmap = *avail_bitmap;
					*avail_bitmap = NULL;
				}
			}
			FREE_NULL_BITMAP(*avail_bitmap);
			*avail_bitmap = bit_copy(tmp_bitmap);
			list_destroy(detail_ptr->feature_list_use);
		}
		list_iterator_destroy(feat_iter);
		FREE_NULL_LIST(preemptee_candidates);
		FREE_NULL_BITMAP(tmp_bitmap);
		if (low_start) {
			job_ptr->start_time = low_start;
			rc = SLURM_SUCCESS;
			FREE_NULL_BITMAP(*avail_bitmap);
			*avail_bitmap = low_bitmap;
		} else {
			rc = ESLURM_NODES_BUSY;
			FREE_NULL_BITMAP(low_bitmap);
		}

		/* Restore the original feature information */
		detail_ptr->feature_list_use = feature_cache;
	} else if (detail_ptr->feature_list_use) {
		if ((job_req_node_filter(job_ptr, *avail_bitmap, true) !=
		     SLURM_SUCCESS) ||
		    (bit_set_count(*avail_bitmap) < min_nodes)) {
			rc = ESLURM_NODES_BUSY;
		} else {
			preemptee_candidates =
					slurm_find_preemptable_jobs(job_ptr);
			rc = select_g_job_test(job_ptr, *avail_bitmap,
					       min_nodes, max_nodes, req_nodes,
					       SELECT_MODE_WILL_RUN,
					       preemptee_candidates,
					       NULL,
					       exc_core_bitmap);
		}
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
			debug2("exclude core bitmap: %s", str);
		}

		rc = select_g_job_test(job_ptr, *avail_bitmap, min_nodes,
				       max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       preemptee_candidates,
				       NULL,
				       exc_core_bitmap);

		job_ptr->details->share_res = orig_shared;

		if (((rc != SLURM_SUCCESS) || (job_ptr->start_time > now)) &&
		    (orig_shared != 0)) {
			FREE_NULL_BITMAP(*avail_bitmap);
			*avail_bitmap = tmp_bitmap;
			rc = select_g_job_test(job_ptr, *avail_bitmap,
					       min_nodes, max_nodes, req_nodes,
					       SELECT_MODE_WILL_RUN,
					       preemptee_candidates,
					       NULL,
					       exc_core_bitmap);
		} else
			FREE_NULL_BITMAP(tmp_bitmap);
	}

	FREE_NULL_LIST(preemptee_candidates);
	return rc;
}

/* Terminate backfill_agent */
extern void stop_backfill_agent(void)
{
	slurm_mutex_lock(&term_lock);
	stop_backfill = true;
	slurm_cond_signal(&term_cond);
	slurm_mutex_unlock(&term_lock);
}

/* Sleep for at least specified time, returns actual sleep time in usec */
static uint32_t _my_sleep(int64_t usec)
{
	int64_t nsec;
	uint32_t sleep_time = 0;
	struct timespec ts = {0, 0};
	struct timeval  tv1 = {0, 0}, tv2 = {0, 0};

	if (gettimeofday(&tv1, NULL)) {		/* Some error */
		sleep(1);
		return 1000000;
	}

	nsec  = tv1.tv_usec + usec;
	nsec *= 1000;
	ts.tv_sec  = tv1.tv_sec + (nsec / 1000000000);
	ts.tv_nsec = nsec % 1000000000;
	slurm_mutex_lock(&term_lock);
	if (!stop_backfill)
		slurm_cond_timedwait(&term_cond, &term_lock, &ts);
	slurm_mutex_unlock(&term_lock);
	if (gettimeofday(&tv2, NULL))
		return usec;
	sleep_time = (tv2.tv_sec - tv1.tv_sec) * 1000000;
	sleep_time += tv2.tv_usec;
	sleep_time -= tv1.tv_usec;
	return sleep_time;
}

static void _load_config(void)
{
	char *sched_params = slurm_conf.sched_params, *tmp_ptr;

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_interval="))) {
		backfill_interval = atoi(tmp_ptr + 12);
		if (((backfill_interval != -1) && (backfill_interval < 1)) ||
		    backfill_interval > MAX_BACKFILL_INTERVAL) {
			error("Invalid SchedulerParameters bf_interval: %d",
			      backfill_interval);
			backfill_interval = BACKFILL_INTERVAL;
		}
	} else {
		backfill_interval = BACKFILL_INTERVAL;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_time="))) {
		bf_max_time = atoi(tmp_ptr + 12);
		if (bf_max_time < 1  || bf_max_time > MAX_BF_MAX_TIME) {
			error("Invalid SchedulerParameters bf_max_time:"
			      " %d", bf_max_time);
			bf_max_time = backfill_interval;
		}
	} else {
		bf_max_time = backfill_interval;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_window="))) {
		backfill_window = atoi(tmp_ptr + 10) * 60;  /* mins to secs */
		if (backfill_window < 1 ||
		    backfill_window > MAX_BACKFILL_WINDOW) {
			error("Invalid SchedulerParameters bf_window: %d",
			      backfill_window);
			backfill_window = BACKFILL_WINDOW;
		}
	} else {
		backfill_window = BACKFILL_WINDOW;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_job_test=")))
		max_backfill_job_cnt = atoi(tmp_ptr + 16);
	else if ((tmp_ptr = xstrcasestr(sched_params, "max_job_bf="))) {
		fatal("Invalid parameter max_job_bf. The option is no longer supported, please use bf_max_job_test instead.");
	}
	else
		max_backfill_job_cnt = DEF_BF_MAX_JOB_TEST;

	if (max_backfill_job_cnt < 1 ||
	    max_backfill_job_cnt > MAX_BF_MAX_JOB_TEST) {
		error("Invalid SchedulerParameters bf_max_job_test: %d",
		      max_backfill_job_cnt);
		max_backfill_job_cnt = DEF_BF_MAX_JOB_TEST;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_node_space_size=")))
		bf_node_space_size = atoi(tmp_ptr + 19);
	else
		bf_node_space_size = max_backfill_job_cnt;

	if (bf_node_space_size < 2 ||
	    bf_node_space_size > 2 * MAX_BF_MAX_JOB_TEST) {
		error("Invalid SchedulerParameters bf_node_space_size: %d",
		      bf_node_space_size);
		bf_node_space_size = max_backfill_job_cnt;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_resolution="))) {
		backfill_resolution = atoi(tmp_ptr + 14);
		if (backfill_resolution < 1 ||
		    backfill_resolution > MAX_BACKFILL_RESOLUTION) {
			error("Invalid SchedulerParameters bf_resolution: %d",
			      backfill_resolution);
			backfill_resolution = BACKFILL_RESOLUTION;
		}
	} else {
		backfill_resolution = BACKFILL_RESOLUTION;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_job_array_resv="))) {
		bf_max_job_array_resv = atoi(tmp_ptr + 22);
		if (bf_max_job_array_resv < 0 ||
		    bf_max_job_array_resv > MAX_BF_MAX_JOB_ARRAY_RESV) {
			error("Invalid SchedulerParameters bf_max_job_array_resv: %d",
			      bf_max_job_array_resv);
			bf_max_job_array_resv = BF_MAX_JOB_ARRAY_RESV;
		}
	} else {
		bf_max_job_array_resv = BF_MAX_JOB_ARRAY_RESV;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_job_part="))) {
		max_backfill_job_per_part = atoi(tmp_ptr + 16);
		if (max_backfill_job_per_part < 0) {
			error("Invalid SchedulerParameters bf_max_job_part: %d",
			      max_backfill_job_per_part);
			max_backfill_job_per_part = 0;
		}
	} else {
		max_backfill_job_per_part = 0;
	}
	if ((max_backfill_job_per_part != 0) &&
	    (max_backfill_job_per_part >= max_backfill_job_cnt)) {
		error("bf_max_job_part >= bf_max_job_test (%u >= %u)",
		      max_backfill_job_per_part, max_backfill_job_cnt);
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_job_start="))) {
		max_backfill_jobs_start = atoi(tmp_ptr + 17);
		if (max_backfill_jobs_start < 0 ||
		    max_backfill_jobs_start > MAX_BF_MAX_JOB_START) {
			error("Invalid SchedulerParameters bf_max_job_start: %d",
			      max_backfill_jobs_start);
			max_backfill_jobs_start = 0;
		}
	} else {
		max_backfill_jobs_start = 0;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_job_user="))) {
		max_backfill_job_per_user = atoi(tmp_ptr + 16);
		if (max_backfill_job_per_user < 0) {
			error("Invalid SchedulerParameters bf_max_job_user: %d",
			      max_backfill_job_per_user);
			max_backfill_job_per_user = 0;
		}
	} else {
		max_backfill_job_per_user = 0;
	}
	if ((max_backfill_job_per_user != 0) &&
	    (max_backfill_job_per_user > max_backfill_job_cnt)) {
		info("warning: bf_max_job_user > bf_max_job_test (%u > %u)",
		     max_backfill_job_per_user, max_backfill_job_cnt);
	}

	bf_job_part_count_reserve = 0;
	if ((tmp_ptr = xstrcasestr(sched_params, "bf_job_part_count_reserve="))) {
		int job_cnt = atoi(tmp_ptr + 26);
		if (job_cnt < 0 || job_cnt > MAX_BF_JOB_PART_COUNT_RESERVE) {
			error("Invalid SchedulerParameters bf_job_part_count_reserve: %d",
			      job_cnt);
		} else {
			bf_job_part_count_reserve = job_cnt;
		}
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_job_user_part="))) {
		max_backfill_job_per_user_part = atoi(tmp_ptr + 21);
		if (max_backfill_job_per_user_part < 0) {
			error("Invalid SchedulerParameters bf_max_job_user_part: %d",
			      max_backfill_job_per_user_part);
			max_backfill_job_per_user_part = 0;
		}
	} else {
		max_backfill_job_per_user_part = 0;
	}
	if ((max_backfill_job_per_user_part != 0) &&
	    (max_backfill_job_per_user_part > max_backfill_job_cnt)) {
		info("warning: bf_max_job_user_part > bf_max_job_test (%u > %u)",
		     max_backfill_job_per_user_part, max_backfill_job_cnt);
	}


	if ((tmp_ptr = xstrcasestr(sched_params, "bf_max_job_assoc="))) {
		max_backfill_job_per_assoc = atoi(tmp_ptr + 17);
		if (max_backfill_job_per_assoc < 0) {
			error("Invalid SchedulerParameters bf_max_job_assoc: %d",
			      max_backfill_job_per_assoc);
			max_backfill_job_per_assoc = 0;
		}
	} else {
		max_backfill_job_per_assoc = 0;
	}
	if ((max_backfill_job_per_assoc != 0) &&
	    (max_backfill_job_per_assoc > max_backfill_job_cnt)) {
		info("warning: bf_max_job_assoc > bf_max_job_test (%u > %u)",
		     max_backfill_job_per_assoc, max_backfill_job_cnt);
	}
	if ((max_backfill_job_per_assoc != 0) &&
	    (max_backfill_job_per_user != 0)) {
		error("Both bf_max_job_user and bf_max_job_assoc are set: "
		      "bf_max_job_assoc taking precedence.");
		max_backfill_job_per_user = 0;
	}

	bf_min_age_reserve = 0;
	if ((tmp_ptr = xstrcasestr(sched_params, "bf_min_age_reserve="))) {
		int min_age = atoi(tmp_ptr + 19);
		if (min_age < 0 || min_age > MAX_BF_MIN_AGE_RESERVE) {
			error("Invalid SchedulerParameters bf_min_age_reserve: %d",
			      min_age);
		} else {
			bf_min_age_reserve = min_age;
		}
	}

	bf_min_prio_reserve = 0;
	if ((tmp_ptr = xstrcasestr(sched_params, "bf_min_prio_reserve="))) {
		unsigned long long int min_prio;
		tmp_ptr += 20;
		min_prio = strtoull(tmp_ptr, NULL, 10);
		if (!min_prio || min_prio > MAX_BF_MIN_PRIO_RESERVE) {
			error("Invalid SchedulerParameters bf_min_prio_reserve: %llu",
			      min_prio);
		} else {
			bf_min_prio_reserve = (uint32_t) min_prio;
		}
	}

	/* bf_continue makes backfill continue where it was if interrupted */
	if (xstrcasestr(sched_params, "bf_continue")) {
		backfill_continue = true;
	} else {
		backfill_continue = false;
	}

	if (xstrcasestr(sched_params, "assoc_limit_stop")) {
		assoc_limit_stop = true;
	} else {
		assoc_limit_stop = false;
	}


	if ((tmp_ptr = xstrcasestr(sched_params, "bf_yield_interval="))) {
		yield_interval = atoi(tmp_ptr + 18);
		if ((yield_interval <= 0) ||
		    (yield_interval > MAX_BF_YIELD_INTERVAL)) {
			error("Invalid backfill scheduler bf_yield_interval: %d",
			      yield_interval);
			yield_interval = YIELD_INTERVAL;
		}
	} else {
		yield_interval = YIELD_INTERVAL;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "bf_yield_sleep="))) {
		yield_sleep = (int64_t) atoll(tmp_ptr + 15);
		if (yield_sleep <= 0 || yield_sleep > MAX_YIELD_SLEEP) {
			error("Invalid backfill scheduler bf_yield_sleep: %d",
			      yield_sleep);
			yield_sleep = YIELD_SLEEP;
		}
	} else {
		yield_sleep = YIELD_SLEEP;
	}

	bf_hetjob_prio = 0;
	if ((tmp_ptr = xstrcasestr(sched_params, "bf_hetjob_prio="))) {
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
	if (xstrcasestr(sched_params, "bf_hetjob_immediate"))
		bf_hetjob_immediate = true;

	if (bf_hetjob_immediate && !bf_hetjob_prio) {
		bf_hetjob_prio |= HETJOB_PRIO_MIN;
		info("bf_hetjob_immediate automatically sets bf_hetjob_prio=min");
	}

	if (xstrcasestr(sched_params, "bf_one_resv_per_job"))
		bf_one_resv_per_job = true;
	else
		bf_one_resv_per_job = false;

	if (xstrcasestr(sched_params, "bf_running_job_reserve"))
		bf_running_job_reserve = true;
	else
		bf_running_job_reserve = false;

	if (xstrcasestr(sched_params, "bf_licenses")) {
		bf_licenses = true;
		bf_running_job_reserve = true;
	} else {
		bf_licenses = false;
	}

	if ((tmp_ptr = xstrcasestr(sched_params, "max_rpc_cnt=")))
		max_rpc_cnt = atoi(tmp_ptr + 12);
	else if ((tmp_ptr = xstrcasestr(sched_params, "max_rpc_count=")))
		max_rpc_cnt = atoi(tmp_ptr + 14);
	else
		max_rpc_cnt = 0;
	if ((max_rpc_cnt < 0) || (max_rpc_cnt > MAX_MAX_RPC_CNT)) {
		error("Invalid SchedulerParameters max_rpc_cnt: %d",
		      max_rpc_cnt);
		max_rpc_cnt = 0;
	}
}

/* Note that slurm.conf has changed */
extern void backfill_reconfig(void)
{
	slurm_mutex_lock(&config_lock);
	config_flag = true;
	slurm_mutex_unlock(&config_lock);
}

/* Update backfill scheduling statistics
 * IN tv1 - start time
 * IN tv2 - end (current) time
 * IN node_space_recs - count of records in resouces/time table being tested
 */
static void _do_diag_stats(struct timeval *tv1, struct timeval *tv2,
			   int node_space_recs)
{
	uint32_t delta_t, real_time;

	delta_t  = (tv2->tv_sec - tv1->tv_sec) * 1000000;
	delta_t +=  tv2->tv_usec;
	delta_t -=  tv1->tv_usec;
	real_time = delta_t - bf_sleep_usec;

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
	slurmctld_diag_stats.bf_table_size = node_space_recs;
	slurmctld_diag_stats.bf_table_size_sum += node_space_recs;
}

/* backfill_agent - detached thread periodically attempts to backfill jobs */
extern void *backfill_agent(void *args)
{
	time_t now;
	double wait_time;
	static time_t last_backfill_time = 0;
	/* Read config and partitions; Write jobs and nodes */
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	bool load_config;
	bool short_sleep = false;
	int backfill_cnt = 0;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "bckfl", NULL, NULL, NULL) < 0) {
		error("cannot set my name to %s %m", "backfill");
	}
#endif
	_load_config();
	last_backfill_time = time(NULL);
	planned_bitmap = bit_alloc(node_record_count);
	het_job_list = list_create(_het_job_map_del);
	while (!stop_backfill) {
		if (short_sleep)
			_my_sleep(USEC_IN_SEC);
		else if (backfill_interval == -1)
			_my_sleep(BACKFILL_INTERVAL * USEC_IN_SEC);
		else
			_my_sleep((int64_t) backfill_interval * USEC_IN_SEC);
		if (stop_backfill)
			break;

		if (slurmctld_config.scheduling_disabled)
			continue;

		list_flush(het_job_list);
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
		if (backfill_interval == -1) {
			log_flag(BACKFILL, "skipping backfill cycle for %ds",
				 BACKFILL_INTERVAL);
			continue;
		}
		now = time(NULL);
		wait_time = difftime(now, last_backfill_time);
		if ((wait_time < backfill_interval) ||
		    job_is_completing(NULL) || _many_pending_rpcs() ||
		    !avail_front_end(NULL) || !_more_work(last_backfill_time)) {
			short_sleep = true;
			continue;
		}

		slurm_mutex_lock(&check_bf_running_lock);
		slurmctld_diag_stats.bf_active = 1;
		slurm_mutex_unlock(&check_bf_running_lock);

		lock_slurmctld(all_locks);
		if ((backfill_cnt++ % 2) == 0)
			_het_job_start_clear();
		_attempt_backfill();
		last_backfill_time = time(NULL);
		(void) bb_g_job_try_stage_in();
		unlock_slurmctld(all_locks);

		slurm_mutex_lock(&check_bf_running_lock);
		slurmctld_diag_stats.bf_active = 0;
		slurm_mutex_unlock(&check_bf_running_lock);

		short_sleep = false;
	}
	FREE_NULL_LIST(het_job_list);
	xhash_free(user_usage_map); /* May have been init'ed if used */
	FREE_NULL_BITMAP(planned_bitmap);

	return NULL;
}

/*
 * Clear the start_time and sched_nodes for all pending jobs. This is used to
 * ensure that a job which can run in multiple partitions has its start_time and
 * sched_nodes set to the partition offering the earliest start_time.
 */
static int _clear_job_estimates(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	if (IS_JOB_PENDING(job_ptr)) {
		job_ptr->start_time = 0;
		xfree(job_ptr->sched_nodes);
	}
	return SLURM_SUCCESS;
}

/*
 * Return non-zero to break the backfill loop if change in job, node,
 * reservation or partition state or the backfill scheduler needs to be stopped.
 */
static int _yield_locks(int64_t usec)
{
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };
	time_t job_update, node_update, part_update, config_update, resv_update;
	bool load_config = false;
	int yield_rpc_cnt;

	yield_rpc_cnt = MAX((max_rpc_cnt / 10), 20);
	job_update  = last_job_update;
	node_update = last_node_update;
	part_update = last_part_update;
	config_update = slurm_conf.last_update;
	resv_update = last_resv_update;

	unlock_slurmctld(all_locks);
	while (!stop_backfill) {
		bf_sleep_usec += _my_sleep(usec);
		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		if ((max_rpc_cnt == 0) ||
		    (slurmctld_config.server_thread_count <= yield_rpc_cnt)) {
			slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
			break;
		}
		verbose("continuing to yield locks, %d RPCs pending",
			slurmctld_config.server_thread_count);
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);
	}
	lock_slurmctld(all_locks);
	slurm_mutex_lock(&config_lock);
	if (config_flag)
		load_config = true;
	slurm_mutex_unlock(&config_lock);

	if (((!backfill_continue) && ((last_job_update != job_update) ||
	     (last_node_update != node_update))) ||
	    (last_part_update != part_update) ||
	    (slurm_conf.last_update != config_update) ||
	    (last_resv_update != resv_update) ||
	    stop_backfill || load_config)
		return 1;
	else
		return 0;
}

/* Test if this job still has access to the specified partition. The job's
 * available partitions may have changed when locks were released */
static bool _job_part_valid(job_record_t *job_ptr, part_record_t *part_ptr)
{
	part_record_t *avail_part_ptr;
	ListIterator part_iterator;
	bool rc = false;

	if (job_ptr->part_ptr_list) {
		part_iterator = list_iterator_create(job_ptr->part_ptr_list);
		while ((avail_part_ptr = list_next(part_iterator))) {
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

/* Determine if job in the backfill queue is still runnable.
 * Job state could change when lock are periodically released */
static bool _job_runnable_now(job_record_t *job_ptr)
{
	uint16_t cleaning = 0;

	if (IS_JOB_REVOKED(job_ptr)) {
		log_flag(BACKFILL, "%pJ revoked during bf yield", job_ptr);
		return false;
	}
	if (!IS_JOB_PENDING(job_ptr)) {	/* Started in other partition */
		log_flag(BACKFILL, "%pJ started in other partition during bf yield",
			 job_ptr);
		return false;
	}
	if (job_ptr->priority == 0) {	/* Job has been held */
		log_flag(BACKFILL, "%pJ job held during bf yield", job_ptr);
		return false;
	}
	if (IS_JOB_COMPLETING(job_ptr)) { /* Started, requeue and completing */
		log_flag(BACKFILL, "%pJ job started during bf yield", job_ptr);
		return false;
	}
	/*
	 * Already reserved resources for either bf_max_job_array_resv or
	 * max_run_tasks number of jobs in the array. If max_run_tasks is 0, it
	 * wasn't set, so ignore it.
	 */
	if (job_ptr->array_recs &&
	    ((job_ptr->array_recs->pend_run_tasks >= bf_max_job_array_resv) ||
	     (job_ptr->array_recs->max_run_tasks &&
	      (job_ptr->array_recs->pend_run_tasks >=
	     job_ptr->array_recs->max_run_tasks))))
		return false;

	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_CLEANING, &cleaning);
	if (cleaning) {			/* Started, requeue and completing */
		log_flag(BACKFILL, "%pJ job cleaning after bf yield", job_ptr);
		return false;
	}

	return true;
}

static void _restore_preempt_state(job_record_t *job_ptr,
				   time_t *tmp_preempt_start_time,
				   bool *tmp_preempt_in_progress)
{
	if ((*tmp_preempt_start_time != 0)
	    && (job_ptr->details->preempt_start_time == 0)) {
		job_ptr->details->preempt_start_time =
			*tmp_preempt_start_time;
		job_ptr->preempt_in_progress = *tmp_preempt_in_progress;
	}

	*tmp_preempt_start_time = 0;
	*tmp_preempt_in_progress = false;
}

/*
 * IN/OUT: prio to be adjusted
 * IN: value from current component partition
 */
static void _adjust_hetjob_prio(uint32_t *prio, uint32_t val)
{
	if (!*prio)
		*prio = val;
	else if (bf_hetjob_prio & HETJOB_PRIO_MIN)
		*prio = MIN(*prio, val);
	else if (bf_hetjob_prio & HETJOB_PRIO_MAX)
		*prio = MAX(*prio, val);
	else if (bf_hetjob_prio & HETJOB_PRIO_AVG)
		*prio += val;
}

/*
 * IN: job_record pointer of a hetjob leader (caller responsible)
 * RET: [min|max|avg] Priority of all components from same hetjob
 */
static uint32_t _hetjob_calc_prio(job_record_t *het_leader)
{
	job_record_t *het_comp = NULL;
	uint32_t prio = 0, tmp = 0, cnt = 0, i = 0, nparts = 0;
	ListIterator iter = NULL;

	if (bf_hetjob_prio & HETJOB_PRIO_MIN)
		prio = INFINITE;

	iter = list_iterator_create(het_leader->het_job_list);
	while ((het_comp = list_next(iter))) {
		if (het_comp->part_ptr_list && het_comp->priority_array &&
		    (nparts = list_count(het_comp->part_ptr_list))) {
			for (i = 0; i < nparts; i++) {
				tmp = het_comp->priority_array[i];
				if (tmp == 0) { /* job held */
					prio = 0;
					break;
				}
				_adjust_hetjob_prio(&prio, tmp);
				cnt++;
			}
			if (prio == 0) /* job held */
				break;
		} else {
			tmp = het_comp->priority;
			if (tmp == 0) { /* job held */
				prio = 0;
				break;
			}
			_adjust_hetjob_prio(&prio, tmp);
			cnt++;
		}
		if ((bf_hetjob_prio & HETJOB_PRIO_MIN) && (prio == 1))
			break; /* Can not get lower */
	}
	list_iterator_destroy(iter);
	if (prio && cnt && (bf_hetjob_prio & HETJOB_PRIO_AVG))
		prio /= cnt;

	return prio;
}

/*
 * IN: job_record pointer of a hetjob leader (caller responsible)
 * RET: [min|max|avg] PriorityTier of all components from same hetjob
 */
static uint32_t _hetjob_calc_prio_tier(job_record_t *het_leader)
{
	job_record_t *het_comp = NULL;
	part_record_t *part_ptr = NULL;
	uint32_t prio_tier = 0, tmp = 0, cnt = 0;
	ListIterator iter = NULL, iter2 = NULL;

	if (bf_hetjob_prio & HETJOB_PRIO_MIN)
		prio_tier = NO_VAL16 - 1;

	iter = list_iterator_create(het_leader->het_job_list);
	while ((het_comp = list_next(iter))) {
		if (het_comp->part_ptr_list &&
		    list_count(het_comp->part_ptr_list)) {
			iter2 = list_iterator_create(het_comp->part_ptr_list);
			while ((part_ptr = list_next(iter2))) {
				tmp = part_ptr->priority_tier;
				_adjust_hetjob_prio(&prio_tier, tmp);
				cnt++;
			}
			list_iterator_destroy(iter2);
		} else {
			tmp = het_comp->part_ptr->priority_tier;
			_adjust_hetjob_prio(&prio_tier, tmp);
			cnt++;
		}
		if ((bf_hetjob_prio & HETJOB_PRIO_MIN) && (prio_tier == 0))
			break; /* Minimum found. */
		if ((bf_hetjob_prio & HETJOB_PRIO_MAX) &&
		    (prio_tier == (NO_VAL16 - 1)))
			break; /* Maximum found. */
	}
	list_iterator_destroy(iter);
	if (prio_tier && cnt && (bf_hetjob_prio & HETJOB_PRIO_AVG))
		prio_tier /= cnt;

	return prio_tier;
}

/*
 * IN: job_record pointer of a hetjob leader (caller responsible)
 * RET: true if any component from same hetjob has a reservation
 */
static bool _hetjob_any_resv(job_record_t *het_leader)
{
	job_record_t *het_comp = NULL;
	ListIterator iter = NULL;
	bool any_resv = false;

	iter = list_iterator_create(het_leader->het_job_list);
	while (!any_resv && (het_comp = list_next(iter))) {
		if (het_comp->resv_id != 0)
			any_resv = true;
	}
	list_iterator_destroy(iter);

	return any_resv;
}

static int _foreach_het_job_details(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	job_ptr->het_details = (het_job_details_t *)arg;

	return SLURM_SUCCESS;
}

static int _bf_reserve_resv_licenses(void *x, void *arg)
{
	slurmctld_resv_t *resv_ptr = x;
	node_space_handler_t *ns_h = arg;
	node_space_map_t *node_space = ns_h->node_space;
	int *ns_recs_ptr = ns_h->node_space_recs;
	time_t start_time, end_time;
	job_record_t fake_job = {
		.license_list = resv_ptr->license_list,
		.resv_ptr = resv_ptr,
	};

	if (!resv_ptr->license_list)
		return 0;

	if (resv_ptr->end_time < node_space[0].begin_time)
		return 0;

	/* treat flex reservations as always active */
	if (resv_ptr->flags & RESERVE_FLAG_FLEX) {
		start_time = 0;
		end_time = INFINITE;
	} else {
		/* align to resolution */

		start_time = resv_ptr->start_time / backfill_resolution;
		start_time *= backfill_resolution;
		end_time = resv_ptr->end_time / backfill_resolution;
		end_time *= backfill_resolution;
	}

	_add_reservation(start_time, end_time, NULL, &fake_job, node_space,
			 ns_recs_ptr);

	return 0;
}

static int _bf_reserve_running(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	node_space_handler_t *ns_h = (node_space_handler_t *) arg;
	node_space_map_t *node_space = ns_h->node_space;
	int *ns_recs_ptr = ns_h->node_space_recs;
	time_t end_time = job_ptr->end_time;
	bool licenses, whole, preemptable;
	bitstr_t *tmp_bitmap;

	if (!job_ptr || !IS_JOB_RUNNING(job_ptr) || !job_ptr->job_resrcs)
		return SLURM_SUCCESS;

	whole = (job_ptr->job_resrcs->whole_node == WHOLE_NODE_REQUIRED);
	licenses = (job_ptr->license_list);

	if (!whole && !licenses)
		return SLURM_SUCCESS;

	preemptable = (slurm_job_preempt_mode(job_ptr) != PREEMPT_MODE_OFF);

	if (preemptable && !licenses)
		return SLURM_SUCCESS;

	if (*ns_recs_ptr >= bf_node_space_size)
		return SLURM_ERROR;

	end_time = (end_time / backfill_resolution) * backfill_resolution;

	if (preemptable || !whole) {
		/* Reservation only needed for licenses. */
		tmp_bitmap = bit_alloc(node_record_count);
	} else {
		tmp_bitmap = bit_copy(job_ptr->node_bitmap);
	}

	bit_not(tmp_bitmap);

	/*
	 * Ensure reservation start time is aligned to the start of the
	 * backfill map by sending 0 in instead of the actual start time.
	 * A long-running backfill cycle could lead to a skew of a few
	 * seconds - or significantly longer with bf_continue set - which
	 * would fragment the start of the backfill map.
	 */
	_add_reservation(0, end_time, tmp_bitmap, job_ptr, node_space,
			 ns_recs_ptr);

	FREE_NULL_BITMAP(tmp_bitmap);

	return SLURM_SUCCESS;
}

static int _set_hetjob_details(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *) x;
	het_job_details_t *details = NULL;

	if (IS_JOB_PENDING(job_ptr) && job_ptr->het_job_id &&
	    !job_ptr->het_job_offset && job_ptr->het_job_list) {
		/*
		 * Pending hetjob leader component. Do calculations only once
		 * for whole hetjob. xmalloc memory for 1 het_details struct,
		 * but make the pointer accessible in all hetjob components.
		 */
		if (!job_ptr->het_details)
			job_ptr->het_details =
				xmalloc(sizeof(het_job_details_t));

		details = job_ptr->het_details;
		details->any_resv = _hetjob_any_resv(job_ptr);
		details->priority_tier = _hetjob_calc_prio_tier(job_ptr);
		details->priority = _hetjob_calc_prio(job_ptr);

		list_for_each(job_ptr->het_job_list,
			      _foreach_het_job_details, details);
	}

	return SLURM_SUCCESS;
}

/* Fetch key from xhash_t item. Called from function ptr */
static void _bf_map_key_id(void *item, const char **key, uint32_t *key_len)
{
	bf_user_usage_t *user = (bf_user_usage_t *)item;

	xassert(user);

	*key = (char *)&user->uid;
	*key_len = sizeof(uid_t);
}

/* Free item from xhash_t. Called from function ptr */
static void _bf_map_free(void *item)
{
	bf_user_usage_t *user = (bf_user_usage_t *)item;

	if (!user)
		return;

	slurmdb_destroy_bf_usage_members(&user->bf_usage);
	xfree(user);
}

/* Allocate new user and add to xhash_t map */
static bf_user_usage_t *_bf_map_add_user(xhash_t *map, uid_t uid)
{
	bf_user_usage_t *user = xmalloc(sizeof(bf_user_usage_t));
	user->uid = uid;
	xhash_add(map, user);
	return user;
}

/* Find user usage from uid. Add new empty entry to map if not found */
static slurmdb_bf_usage_t *_bf_map_find_add(xhash_t* map, uid_t uid)
{
	bf_user_usage_t *user;
	xassert(map != NULL);

	if (!(user = xhash_get(map, (char *)&uid, sizeof(uid_t))))
		user = _bf_map_add_user(map, uid);
	return &user->bf_usage;
}

/*
 * Check if limit exceeded. Reset usage if usage time is before current
 * scheduling iteration time
 */
static bool _check_bf_usage(
	slurmdb_bf_usage_t *usage, int limit, time_t sched_time)
{
	if (usage->last_sched < sched_time) {
		usage->last_sched = sched_time;
		usage->count = 0;
		return false;
	}
	return usage->count >= limit;
}

/*
 * Check if job exceeds configured count limits
 * returns true if count exceeded
 */
static bool _job_exceeds_max_bf_param(job_record_t *job_ptr,
				      time_t sched_start)
{
	slurmdb_bf_usage_t *part_usage = NULL, *user_usage = NULL,
		*assoc_usage = NULL, *user_part_usage = NULL;

	slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;
	part_record_t *part_ptr = job_ptr->part_ptr;

	if (max_backfill_job_per_user_part) {
		xassert(part_ptr->bf_data);
		user_part_usage = _bf_map_find_add(
			part_ptr->bf_data->user_usage,
			job_ptr->user_id);
		if (_check_bf_usage(user_part_usage,
				    max_backfill_job_per_user_part,
				    sched_start)) {
			log_flag(BACKFILL, "have already checked %u jobs for user %u on partition %s; skipping job %u, %pJ",
				 max_backfill_job_per_user_part,
				 job_ptr->user_id, job_ptr->part_ptr->name,
				 job_ptr->job_id, job_ptr);
			return true;
		}
	}

	if (max_backfill_job_per_part) {
		xassert(part_ptr->bf_data);
		part_usage = part_ptr->bf_data->job_usage;
		if (_check_bf_usage(part_usage, max_backfill_job_per_part,
				    sched_start)) {
			log_flag(BACKFILL, "have already checked %u jobs for partition %s; skipping %pJ",
				 max_backfill_job_per_part,
				 job_ptr->part_ptr->name, job_ptr);
			return true;
		}
	}

	if (max_backfill_job_per_assoc) {
		if (assoc_ptr) {
			if (!assoc_ptr->bf_usage)
				assoc_ptr->bf_usage =
					xmalloc(sizeof(slurmdb_bf_usage_t));
			assoc_usage = assoc_ptr->bf_usage;

			if (_check_bf_usage(assoc_usage,
					    max_backfill_job_per_assoc,
					    sched_start)) {
				log_flag(BACKFILL, "have already checked %u jobs for user %u, assoc %u; skipping %pJ",
					 max_backfill_job_per_assoc,
					 job_ptr->user_id, job_ptr->assoc_id,
					 job_ptr);
				return true;
			}
		} else {
			/* Null assoc_ptr indicates no database */
			log_flag(BACKFILL, "no assoc for job %u, required for parameter bf_max_job_per_assoc",
				 job_ptr->job_id);
			assoc_usage = NULL;
		}
	}

	if (max_backfill_job_per_user) {
		if (assoc_ptr && assoc_ptr->user_rec) {
			if (!assoc_ptr->user_rec->bf_usage)
				assoc_ptr->user_rec->bf_usage =
					xmalloc(sizeof(slurmdb_bf_usage_t));
			user_usage = assoc_ptr->user_rec->bf_usage;
		} else {
			/* No database, or user rec missing from assoc */
			if (!user_usage_map)
				user_usage_map = xhash_init(_bf_map_key_id,
							    _bf_map_free);
			user_usage = _bf_map_find_add(user_usage_map,
						      job_ptr->user_id);
		}

		if (_check_bf_usage(user_usage, max_backfill_job_per_user,
				    sched_start)) {
			log_flag(BACKFILL, "have already checked %u jobs for user %u; skipping %pJ",
				 max_backfill_job_per_user, job_ptr->user_id,
				 job_ptr);
			return true;
		}
	}

	/* Increment our user/partition limit counters as needed */
	if (user_part_usage)
		user_part_usage->count++;
	if (part_usage)
		part_usage->count++;
	if (user_usage)
		user_usage->count++;
	if (assoc_usage)
		assoc_usage->count++;
	return false;
}

/*
 * Handle the planned list.
 * set - If true we are setting bits, else we clear them.
 */
static void _handle_planned(bool set)
{
	int n_first, n_last;
	node_record_t *node_ptr;
	bool node_update = false;

	if (!planned_bitmap)
		return;

	n_first = bit_ffs(planned_bitmap);
	if (n_first == -1)
		return;

	n_last = bit_fls(planned_bitmap);

	for (int n = n_first; n <= n_last; n++) {
		if (!bit_test(planned_bitmap, n))
			continue;
		node_ptr = node_record_table_ptr[n];
		if (set) {
			/*
			 * If the node is allocated ignore this flag. This only
			 * really matters for IDLE and MIXED.
			 */
			if (!IS_NODE_ALLOCATED(node_ptr)) {
				node_ptr->node_state |= NODE_STATE_PLANNED;
				node_update = true;
			} else
				bit_clear(planned_bitmap, n);
		} else {
			node_ptr->node_state &= ~NODE_STATE_PLANNED;
			node_update = true;
			bit_clear(planned_bitmap, n);
		}

		log_flag(BACKFILL, "%s: %s state is %s",
			 set ? "cleared" : "set",
			 node_ptr->name,
			 node_state_string(node_ptr->node_state));
	}

	if (node_update)
		last_node_update = time(NULL);
}

static void _attempt_backfill(void)
{
	DEF_TIMERS;
	List job_queue;
	job_queue_rec_t *job_queue_rec = NULL;
	int bb, i, j, node_space_recs, mcs_select = 0;
	slurmdb_qos_rec_t *qos_ptr = NULL;
	job_record_t *job_ptr = NULL;
	part_record_t *part_ptr;
	uint32_t end_time, end_reserve, deadline_time_limit, boot_time;
	uint32_t orig_end_time;
	uint32_t time_limit, comp_time_limit, orig_time_limit = 0, part_time_limit;
	uint32_t min_nodes, max_nodes, req_nodes;
	bitstr_t *active_bitmap = NULL, *avail_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL, *resv_bitmap = NULL;
	time_t now, sched_start, later_start, start_res, resv_end, window_end;
	time_t het_job_time, orig_sched_start, orig_start_time = (time_t) 0;
	node_space_map_t *node_space;
	struct timeval bf_time1, bf_time2;
	int error_code;
	int job_test_count = 0, test_time_count = 0, pend_time;
	bool already_counted, many_rpcs = false;
	job_record_t *reject_array_job = NULL;
	part_record_t *reject_array_part = NULL;
	slurmctld_resv_t *reject_array_resv = NULL;
	uint32_t start_time;
	struct timeval start_tv;
	uint32_t test_array_job_id = 0;
	uint32_t test_array_count = 0;
	uint32_t job_no_reserve;
	bool is_job_array_head, resv_overlap = false;
	uint8_t save_share_res = 0, save_whole_node = 0;
	int test_fini;
	uint32_t qos_flags = 0;
	time_t qos_blocked_until = 0, qos_part_blocked_until = 0;
	time_t tmp_preempt_start_time = 0;
	bool tmp_preempt_in_progress = false;
	bitstr_t *tmp_bitmap = NULL;
	/* QOS Read lock */
	assoc_mgr_lock_t qos_read_lock =
		{ NO_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
		  NO_LOCK, NO_LOCK, NO_LOCK };

	bf_sleep_usec = 0;
	job_start_cnt = 0;

	if (!fed_mgr_sibs_synced()) {
		info("returning, federation siblings not synced yet");
		return;
	}

	(void) bb_g_load_state(false);

	START_TIMER;
	if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL)
		info("beginning");
	else
		debug("beginning");
	sched_start = orig_sched_start = now = time(NULL);
	gettimeofday(&start_tv, NULL);

	_handle_planned(false);

	job_queue = build_job_queue(true, true);
	job_test_count = list_count(job_queue);
	if (job_test_count == 0) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL)
			info("no jobs to backfill");
		else
			debug("no jobs to backfill");
		FREE_NULL_LIST(job_queue);
		return;
	} else
		debug("%u jobs to backfill", job_test_count);

	list_for_each(job_list, _clear_job_estimates, NULL);

	if (bf_hetjob_prio)
		list_for_each(job_list, _set_hetjob_details, NULL);

	gettimeofday(&bf_time1, NULL);

	slurmctld_diag_stats.bf_queue_len = job_test_count;
	slurmctld_diag_stats.bf_queue_len_sum += slurmctld_diag_stats.
						 bf_queue_len;
	job_test_count = 0;

	slurmctld_diag_stats.bf_last_depth = 0;
	slurmctld_diag_stats.bf_last_depth_try = 0;
	slurmctld_diag_stats.bf_when_last_cycle = now;

	node_space = xcalloc((bf_node_space_size + 1),
			     sizeof(node_space_map_t));
	node_space[0].begin_time = sched_start / backfill_resolution;
	node_space[0].begin_time *= backfill_resolution;
	window_end = (sched_start + backfill_window) / backfill_resolution;
	window_end *= backfill_resolution;
	node_space[0].end_time = window_end;

	node_space[0].avail_bitmap = bit_copy(avail_node_bitmap);
	/* Make "resuming" nodes available to be scheduled in backfill */
	bit_or(node_space[0].avail_bitmap, rs_node_bitmap);

	if (bf_licenses)
		node_space[0].licenses =
			bf_licenses_initial(bf_running_job_reserve);

	node_space[0].next = 0;
	node_space_recs = 1;

	if (bf_running_job_reserve) {
		node_space_handler_t node_space_handler;
		node_space_handler.node_space = node_space;
		node_space_handler.node_space_recs = &node_space_recs;

		if (bf_licenses)
			list_for_each(resv_list, _bf_reserve_resv_licenses,
				      &node_space_handler);

		list_for_each(job_list, _bf_reserve_running,
			      &node_space_handler);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL_MAP)
		_dump_node_space_table(node_space);

	if (assoc_limit_stop) {
		assoc_mgr_lock(&qos_read_lock);
		list_for_each(assoc_mgr_qos_list,
			      _clear_qos_blocked_times, NULL);
		assoc_mgr_unlock(&qos_read_lock);
	}

	sort_job_queue(job_queue);

	/* Ignore nodes that have been set as available during this cycle. */
	bit_clear_all(bf_ignore_node_bitmap);

	while (1) {
		uint32_t bf_job_priority, prio_reserve;
		bool get_boot_time = false;
		bool licenses_unavail;

		/* Run some final guaranteed logic after each job iteration */
		if (job_ptr) {
			job_resv_clear_magnetic_flag(job_ptr);
			fill_array_reasons(job_ptr, reject_array_job);

			/* Restore preemption state if needed. */
			_restore_preempt_state(job_ptr, &tmp_preempt_start_time,
			                       &tmp_preempt_in_progress);

			/*
			 * Restore the original time limit in every corner case
			 * we didn't have done yet, like when we are looping
			 * through array tasks.
			*/
			if ((qos_flags & QOS_FLAG_NO_RESERVE) &&
			    slurm_conf.preempt_mode && orig_time_limit &&
			    (orig_time_limit != job_ptr->time_limit))
				job_ptr->time_limit = orig_time_limit;
		}
		xfree(job_queue_rec);
		job_queue_rec = list_pop(job_queue);
		if (!job_queue_rec) {
			log_flag(BACKFILL, "reached end of job queue");
			break;
		}

		if (slurmctld_diag_stats.bf_last_depth_try >=
		    max_backfill_job_cnt) {
			log_flag(BACKFILL, "bf_max_job_test: limit of %d reached",
				 max_backfill_job_cnt);
			break;
		}

		job_ptr          = job_queue_rec->job_ptr;
		part_ptr         = job_queue_rec->part_ptr;
		bf_job_priority  = job_queue_rec->priority;

		if (job_ptr->array_recs &&
		    (job_queue_rec->array_task_id == NO_VAL))
			is_job_array_head = true;
		else
			is_job_array_head = false;

		if (slurmctld_config.shutdown_time ||
		    (difftime(time(NULL),orig_sched_start) >= bf_max_time)){
			break;
		}

		many_rpcs = false;
		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		if ((max_rpc_cnt > 0) &&
		    (slurmctld_config.server_thread_count >= max_rpc_cnt))
			many_rpcs = true;
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

		if (many_rpcs || (slurm_delta_tv(&start_tv) >= yield_interval)) {
			if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL) {
				END_TIMER;
				info("yielding locks after testing %u(%d) jobs, %s",
				     slurmctld_diag_stats.bf_last_depth,
				     job_test_count, TIME_STR);
			}
			if (_yield_locks(yield_sleep)) {
				log_flag(BACKFILL, "system state changed, breaking out after testing %u(%d) jobs",
					 slurmctld_diag_stats.bf_last_depth,
					 job_test_count);
				break;
			}
			/* Reset backfill scheduling timers, resume testing */
			sched_start = time(NULL);
			gettimeofday(&start_tv, NULL);
			job_test_count = 0;
			test_time_count = 0;
			START_TIMER;
		}

		if (is_job_array_head &&
		    (job_ptr->array_task_id != NO_VAL)) {
			/* Job array element started in other partition,
			 * reset pointer to "master" job array record */
			log_flag(BACKFILL, "%pJ array scheduled during bf yield, try master",
				 job_ptr);
			job_ptr = find_job_record(job_ptr->array_job_id);
			if (!job_ptr)	/* All task array elements started */
				continue;
			job_queue_rec->job_ptr = job_ptr;
		}

		/*
		 * Establish baseline (worst case) start time for hetjob
		 * Update time once start time estimate established
		 */
		_het_job_start_set(job_ptr, (now + YEAR_SECONDS), NO_VAL);

		if (job_ptr->het_job_id &&
		    (job_ptr->state_reason == WAIT_NO_REASON)) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = WAIT_RESOURCES;
		}

		if (!_job_runnable_now(job_ptr))
			continue;
		if (!part_ptr)
			continue;

		if (job_ptr->resv_list)
			job_queue_rec_resv_list(job_queue_rec);
		else
			job_queue_rec_magnetic_resv(job_queue_rec);
		xfree(job_queue_rec);

		job_ptr->bit_flags |= BACKFILL_SCHED;
		job_ptr->last_sched_eval = now;
		job_ptr->part_ptr = part_ptr;
		job_ptr->priority = bf_job_priority;
		mcs_select = slurm_mcs_get_select(job_ptr);
		het_job_time = _het_job_start_find(job_ptr);
		if (het_job_time > (now + backfill_window))
			continue;

		if (job_ptr->qos_id) {
			assoc_mgr_lock_t locks = {
				READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				NO_LOCK, NO_LOCK, NO_LOCK };

			assoc_mgr_lock(&locks);
			if (job_ptr->assoc_ptr
			    && (accounting_enforce & ACCOUNTING_ENFORCE_QOS)
			    && ((job_ptr->qos_id >= g_qos_count) ||
				!job_ptr->assoc_ptr->usage ||
				!job_ptr->assoc_ptr->usage->valid_qos ||
				!bit_test(job_ptr->assoc_ptr->usage->valid_qos,
					  job_ptr->qos_id))
			    && !job_ptr->limit_set.qos) {
				debug("%pJ has invalid QOS",
				      job_ptr);
				assoc_mgr_unlock(&locks);
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

		assoc_mgr_lock(&qos_read_lock);
		if (job_ptr->qos_ptr) {
			qos_flags = job_ptr->qos_ptr->flags;
			qos_blocked_until = job_ptr->qos_ptr->blocked_until;
		} else {
			qos_flags = 0;
			qos_blocked_until = 0;
		}

		if (job_ptr->part_ptr->qos_ptr)
			qos_part_blocked_until =
				job_ptr->part_ptr->qos_ptr->blocked_until;
		else
			qos_part_blocked_until = 0;

		if (part_policy_valid_qos(job_ptr->part_ptr, job_ptr->qos_ptr,
					  job_ptr) != SLURM_SUCCESS) {
			assoc_mgr_unlock(&qos_read_lock);
			continue;
		}
		assoc_mgr_unlock(&qos_read_lock);

		if (!assoc_limit_stop &&
		    !acct_policy_job_runnable_pre_select(job_ptr, false)) {
			continue;
		}

		if (!(prio_reserve = acct_policy_get_prio_thresh(
			      job_ptr, false)))
			prio_reserve = bf_min_prio_reserve;

		if (prio_reserve)
			log_flag(BACKFILL, "%pJ has a prio_reserve of %u",
				 job_ptr, prio_reserve);

		job_no_reserve = 0;
		if (prio_reserve &&
		    (job_ptr->priority < prio_reserve)) {
			job_no_reserve = TEST_NOW_ONLY;
		} else if (bf_min_age_reserve && job_ptr->details->begin_time) {
			pend_time = difftime(time(NULL),
					     job_ptr->details->begin_time);
			if (pend_time < bf_min_age_reserve)
				job_no_reserve = TEST_NOW_ONLY;
		}

		if (bf_one_resv_per_job && job_ptr->start_time) {
			log_flag(BACKFILL, "%pJ already added a backfill reservation. Test immediate start only for partition %s",
				 job_ptr, job_ptr->part_ptr->name);
			job_no_reserve = TEST_NOW_ONLY;
		}

		/*
		 * If we are trying to schedule preferred features don't
		 * reserve.
		 */
		if (job_ptr->details->prefer &&
		    (job_ptr->details->features_use ==
		     job_ptr->details->prefer))
			job_no_reserve = TEST_NOW_ONLY;

		/* If partition data is needed and not yet initialized, do so */
		if (!job_ptr->part_ptr->bf_data &&
		    (bf_job_part_count_reserve ||
		     max_backfill_job_per_user_part ||
		     max_backfill_job_per_part)) {
			bf_part_data_t *part_data =
				xmalloc(sizeof(bf_part_data_t));
			part_data->job_usage =
				xmalloc(sizeof(slurmdb_bf_usage_t));
			part_data->resv_usage =
				xmalloc(sizeof(slurmdb_bf_usage_t));
			part_data->user_usage = xhash_init(_bf_map_key_id,
							   _bf_map_free);
			job_ptr->part_ptr->bf_data = part_data;
		}

		if ((job_no_reserve == 0) && bf_job_part_count_reserve) {
			if (_check_bf_usage(
				    job_ptr->part_ptr->bf_data->resv_usage,
				    bf_job_part_count_reserve,
				    orig_sched_start))
				job_no_reserve = TEST_NOW_ONLY;
		}

		if (job_ptr->preempt_in_progress)
			continue; 	/* scheduled in another partition */

		orig_start_time = job_ptr->start_time;
		orig_time_limit = job_ptr->time_limit;

next_task:
		/*
		 * Save the current preemption state. Reset preemption state
		 * in the job_ptr so a job array can preempt multiple jobs.
		 */
		if (job_ptr->preempt_in_progress) {
			tmp_preempt_in_progress = job_ptr->preempt_in_progress;
			tmp_preempt_start_time = job_ptr->details->preempt_start_time;
			job_ptr->details->preempt_start_time = 0;
			job_ptr->preempt_in_progress = false;
		}

		job_test_count++;
		slurmctld_diag_stats.bf_last_depth++;
		already_counted = false;

		if (!IS_JOB_PENDING(job_ptr) ||	/* Started in other partition */
		    (job_ptr->priority == 0))	/* Job has been held */
			continue;
		if (!avail_front_end(job_ptr))
			continue;	/* No available frontend for this job */
		if (!_job_part_valid(job_ptr, part_ptr))
			continue;	/* Partition change during lock yield */
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
		job_ptr->part_ptr = part_ptr;
		if (job_limits_check(&job_ptr, true) != WAIT_NO_REASON) {
			/* should never happen */
			continue;
		}

		log_flag(BACKFILL, "test for %pJ Prio=%u Partition=%s",
			 job_ptr, job_ptr->priority, job_ptr->part_ptr->name);

		/* Test to see if we've exceeded any per user/partition limit */
		if (_job_exceeds_max_bf_param(job_ptr, orig_sched_start))
			continue;

		if (((part_ptr->state_up & PARTITION_SCHED) == 0) ||
		    (part_ptr->node_bitmap == NULL)) {
			log_flag(BACKFILL, "partition %s not usable",
				 job_ptr->part_ptr->name);
			continue;
		}

		if (!bf_licenses &&
		    license_job_test(job_ptr, time(NULL), true)) {
			log_flag(BACKFILL, "%pJ not runable now due to licenses",
				 job_ptr);
			continue;
		}

		if (!job_independent(job_ptr)) {
			log_flag(BACKFILL, "%pJ not runable now",
				 job_ptr);
			continue;
		}

		/* Determine minimum and maximum node counts */
		error_code = get_node_cnts(job_ptr, qos_flags, part_ptr,
					   &min_nodes, &req_nodes, &max_nodes);

		if (error_code == ESLURM_ACCOUNTING_POLICY) {
			log_flag(BACKFILL, "%pJ acct policy node limit",
				 job_ptr);
			continue;
		} else if (error_code ==
			   ESLURM_REQUESTED_PART_CONFIG_UNAVAILABLE) {
			log_flag(BACKFILL, "%pJ node count too high",
				 job_ptr);
			continue;
		} else if (error_code != SLURM_SUCCESS) {
			log_flag(BACKFILL, "error setting nodes for %pJ: %s",
				 job_ptr, slurm_strerror(error_code));
			continue;
		}

		/* test of deadline */
		now = time(NULL);
		deadline_time_limit = 0;
		if ((job_ptr->deadline) && (job_ptr->deadline != NO_VAL)) {
			if (!deadline_ok(job_ptr, "backfill"))
				continue;

			deadline_time_limit = (job_ptr->deadline - now) / 60;
		}

		/* Determine job's expected completion time */
		if (part_ptr->max_time == INFINITE)
			part_time_limit = YEAR_MINUTES;
		else
			part_time_limit = part_ptr->max_time;
		if ((job_ptr->time_limit == NO_VAL) ||
		    (job_ptr->time_limit == INFINITE)) {
			time_limit = part_time_limit;
			job_ptr->limit_set.time = 1;
		} else {
			if (part_ptr->max_time == INFINITE)
				time_limit = job_ptr->time_limit;
			else
				time_limit = MIN(job_ptr->time_limit,
						 part_time_limit);
		}
		if (deadline_time_limit)
			comp_time_limit = MIN(time_limit, deadline_time_limit);
		else if (job_ptr->time_min &&
			 (job_ptr->time_min < time_limit)) {
			time_limit = job_ptr->time_limit = job_ptr->time_min;
			comp_time_limit = time_limit;
		} else
			comp_time_limit = time_limit;
		if ((qos_flags & QOS_FLAG_NO_RESERVE) &&
		    slurm_conf.preempt_mode)
			time_limit = job_ptr->time_limit = 1;

		later_start = now;

		if (assoc_limit_stop) {
			if (qos_blocked_until > later_start) {
				later_start = qos_blocked_until;
				log_flag(BACKFILL, "QOS blocked_until move start_res to %ld",
					 later_start);
			}
			if (qos_part_blocked_until > later_start) {
				later_start = qos_part_blocked_until;
				log_flag(BACKFILL, "Part QOS blocked_until move start_res to %ld",
					 later_start);
			}
		}

 TRY_LATER:
		if (slurmctld_config.shutdown_time ||
		    (difftime(time(NULL), orig_sched_start) >=
		     bf_max_time)) {
			_set_job_time_limit(job_ptr, orig_time_limit);
			break;
		}
		test_time_count++;

		many_rpcs = false;
		slurm_mutex_lock(&slurmctld_config.thread_count_lock);
		if ((max_rpc_cnt > 0) &&
		    (slurmctld_config.server_thread_count >= max_rpc_cnt))
			many_rpcs = true;
		slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

		if (many_rpcs || (slurm_delta_tv(&start_tv) >= yield_interval)) {
			uint32_t save_time_limit = job_ptr->time_limit;
			slurmctld_resv_t *save_resv_ptr = job_ptr->resv_ptr;
			_set_job_time_limit(job_ptr, orig_time_limit);
			if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL) {
				END_TIMER;
				info("yielding locks after testing "
				     "%u(%d) jobs tested, %u time slots, %s",
				     slurmctld_diag_stats.bf_last_depth,
				     job_test_count, test_time_count, TIME_STR);
			}
			if (_yield_locks(yield_sleep)) {
				log_flag(BACKFILL, "system state changed, breaking out after testing %u(%d) jobs",
					 slurmctld_diag_stats.bf_last_depth,
					 job_test_count);
				break;
			}

			/* Reset backfill scheduling timers, resume testing */
			sched_start = time(NULL);
			gettimeofday(&start_tv, NULL);
			job_test_count = 1;
			test_time_count = 0;
			START_TIMER;

			if (is_job_array_head &&
			    (job_ptr->array_task_id != NO_VAL)) {
				/*
				 * Job array element started in other partition,
				 * reset pointer to "master" job array record
				 */
				log_flag(BACKFILL, "%pJ array scheduled during bf yield, try master",
					 job_ptr);
				job_ptr = find_job_record(
						job_ptr->array_job_id);
				if (!job_ptr)
					/* All task array elements started */
					continue;
			}

			/*
			 * With bf_continue configured, the original job could
			 * have been scheduled. Revalidate the job record here.
			 */
			if (!_job_runnable_now(job_ptr))
				continue;
			if (!avail_front_end(job_ptr)) {
				log_flag(BACKFILL, "%pJ no frontend available after bf yield",
					 job_ptr);
				continue;	/* No available frontend */
			}
			job_ptr->resv_ptr = save_resv_ptr;
			if (!job_independent(job_ptr)) {
				log_flag(BACKFILL, "%pJ no longer independent after bf yield",
					 job_ptr);
				/* No longer independent
				 * (e.g. another singleton started) */
				continue;
			}

			job_ptr->time_limit = save_time_limit;
			job_ptr->part_ptr = part_ptr;
		}

		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);
		start_res = MAX(later_start, het_job_time);
		resv_end = 0;
		later_start = 0;
		licenses_unavail = false;
		/* Determine impact of any advance reservations */
		j = job_test_resv(job_ptr, &start_res, true, &avail_bitmap,
				  &exc_core_bitmap, &resv_overlap, false);
		if (j != SLURM_SUCCESS) {
			log_flag(BACKFILL, "%pJ reservation defer",
				 job_ptr);
			_set_job_time_limit(job_ptr, orig_time_limit);
			continue;
		}
		if (start_res > now)
			end_time = (time_limit * 60) + start_res;
		else
			end_time = (time_limit * 60) + now;
		if (end_time < now)	/* Overflow 32-bits */
			end_time = INFINITE;
		if (resv_overlap)
			resv_end = find_resv_end(start_res,
						 backfill_resolution);
		/* Identify usable nodes for this job */
		bit_and(avail_bitmap, part_ptr->node_bitmap);
		bit_and(avail_bitmap, up_node_bitmap);
		bit_and_not(avail_bitmap, bf_ignore_node_bitmap);
		filter_by_node_owner(job_ptr, avail_bitmap);
		filter_by_node_mcs(job_ptr, mcs_select, avail_bitmap);
		tmp_bitmap = bit_copy(avail_bitmap);
		for (j = 0; ; ) {
			if ((node_space[j].end_time > start_res) &&
			     node_space[j].next && (later_start == 0)) {
				int tmp = node_space[j].next;
				bitstr_t *next_bitmap = bit_copy(tmp_bitmap);
				bitstr_t *current_bitmap =
					bit_copy(avail_bitmap);
				bit_and(next_bitmap,
					node_space[tmp].avail_bitmap);
				bit_and(current_bitmap,
					node_space[j].avail_bitmap);
				/*
				 * Normally later_start is set at the end of the
				 * first backfill reservation when the select
				 * plugin predicts start time after later_start.
				 * Then it goes to TRY_LATER and tries again on
				 * a new set of nodes to check if the job can
				 * start earlier. But if the next set of nodes
				 * is a subset of the currently tested ones then
				 * calling _try_sched (expensive function) would
				 * be useless and would impact performance.
				 */
				if (!bit_super_set(next_bitmap, current_bitmap))
					later_start = node_space[j].end_time;
				FREE_NULL_BITMAP(next_bitmap);
				FREE_NULL_BITMAP(current_bitmap);
			}
			if (node_space[j].end_time <= start_res)
				;
			else if (node_space[j].begin_time <= end_time) {
				bit_and(avail_bitmap,
					node_space[j].avail_bitmap);
				if (!bf_licenses_avail(node_space[j].licenses,
						       job_ptr)) {
					licenses_unavail = true;
					later_start = node_space[j].end_time;
				}
			} else
				break;
			if ((j = node_space[j].next) == 0)
				break;
		}
		FREE_NULL_BITMAP(tmp_bitmap);
		if (resv_end && (++resv_end < window_end) &&
		    ((later_start == 0) || (resv_end < later_start))) {
			later_start = resv_end;
		}

		if (job_ptr->details->exc_node_bitmap) {
			bit_and_not(avail_bitmap,
				job_ptr->details->exc_node_bitmap);
		}

		/* Test if licenses are unavailable OR
		 *	insufficient nodes remain OR
		 *	required nodes missing OR
		 *	nodes lack features OR
		 *	no change since previously tested nodes (only changes
		 *	in other partition nodes) */
		if (licenses_unavail ||
		    (bit_set_count(avail_bitmap) < min_nodes) ||
		    ((job_ptr->details->req_node_bitmap) &&
		     (!bit_super_set(job_ptr->details->req_node_bitmap,
				     avail_bitmap))) ||
		    (job_req_node_filter(job_ptr, avail_bitmap, true))) {
			if (later_start && !job_no_reserve) {
				job_ptr->start_time = 0;
				goto TRY_LATER;
			}

			/* Job can not start until too far in the future */
			_set_job_time_limit(job_ptr, orig_time_limit);
			/*
			 * Use orig_start_time if job can't
			 * start in different partition it will be 0
			 */
			job_ptr->start_time = orig_start_time;
			continue;
		}

		/* Identify nodes which are definitely off limits */
		FREE_NULL_BITMAP(resv_bitmap);
		resv_bitmap = bit_copy(avail_bitmap);
		bit_not(resv_bitmap);

		/* this is the time consuming operation */
		debug2("entering _try_sched for %pJ.",
		       job_ptr);

		if (!already_counted) {
			slurmctld_diag_stats.bf_last_depth_try++;
			already_counted = true;
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL_MAP)
			_dump_job_test(job_ptr, avail_bitmap, start_res);
		test_fini = -1;
		build_active_feature_bitmap(job_ptr, avail_bitmap,
					    &active_bitmap);
		job_ptr->bit_flags |= BACKFILL_TEST;
		job_ptr->bit_flags |= job_no_reserve;	/* 0 or TEST_NOW_ONLY */

		if (active_bitmap) {
			j = _try_sched(job_ptr, &active_bitmap, min_nodes,
				       max_nodes, req_nodes, exc_core_bitmap);
			if (j == SLURM_SUCCESS) {
				FREE_NULL_BITMAP(avail_bitmap);
				avail_bitmap = active_bitmap;
				active_bitmap = NULL;
				test_fini = 1;
			} else {
				if (node_features_g_overlap(active_bitmap))
					get_boot_time = true;
				FREE_NULL_BITMAP(active_bitmap);
				save_share_res  = job_ptr->details->share_res;
				save_whole_node = job_ptr->details->whole_node;
				job_ptr->details->share_res = 0;
				job_ptr->details->whole_node = 1;
				if (!save_whole_node)
					job_ptr->bit_flags |= BF_WHOLE_NODE_TEST;
				test_fini = 0;
			}
		}
		boot_time = 0;
		if (test_fini == 0) {
			/* Unable to start job using currently active features,
			 * need to try using features which can be made
			 * available after node reboot */
			bitstr_t *tmp_core_bitmap = NULL;
			bitstr_t *tmp_node_bitmap = NULL;
			debug2("entering _try_sched for %pJ. Need to use features which can be made available after node reboot",
			       job_ptr);
			/* Determine impact of any advance reservations */
			resv_end = 0;
			j = job_test_resv(job_ptr, &start_res, false,
					  &tmp_node_bitmap, &tmp_core_bitmap,
					  &resv_overlap, true);
			if (resv_overlap)
				resv_end = find_resv_end(start_res,
							 backfill_resolution);

			if (resv_end && (++resv_end < window_end) &&
			    ((later_start == 0) || (resv_end < later_start))) {
				later_start = resv_end;
			}
			if (j == SLURM_SUCCESS) {
				FREE_NULL_BITMAP(exc_core_bitmap);
				exc_core_bitmap = tmp_core_bitmap;
				bit_and(avail_bitmap, tmp_node_bitmap);
				FREE_NULL_BITMAP(tmp_node_bitmap);
			}
			if (get_boot_time)
				boot_time = node_features_g_boot_time();
			orig_end_time = end_time;
			end_time += boot_time;

			for (j = 0; ; ) {
				if (node_space[j].end_time <= start_res)
					;
				else if (node_space[j].begin_time <= end_time) {
					if (node_space[j].begin_time >
					    orig_end_time)
						bit_and(avail_bitmap,
						node_space[j].avail_bitmap);
				} else
					break;
				if ((j = node_space[j].next) == 0)
					break;
			}
		}
		if (test_fini != 1) {
			/* Either active_bitmap was NULL or not usable by the
			 * job. Test using avail_bitmap instead */
			j = _try_sched(job_ptr, &avail_bitmap, min_nodes,
				       max_nodes, req_nodes, exc_core_bitmap);
			if (test_fini == 0) {
				job_ptr->details->share_res = save_share_res;
				job_ptr->details->whole_node = save_whole_node;
			}
		}
		job_ptr->bit_flags &= ~BACKFILL_TEST;
		job_ptr->bit_flags &= ~BF_WHOLE_NODE_TEST;
		job_ptr->bit_flags &= ~TEST_NOW_ONLY;

		now = time(NULL);
		if (j != SLURM_SUCCESS) {
			_set_job_time_limit(job_ptr, orig_time_limit);
			if (later_start && !job_no_reserve) {
				job_ptr->start_time = 0;
				goto TRY_LATER;
			}
			job_ptr->start_time = orig_start_time;
			continue;	/* not runable in this partition */
		}

		if (start_res > job_ptr->start_time) {
			job_ptr->start_time = start_res;
			last_job_update = now;
		}
		/*
		 * avail_bitmap at this point contains a bitmap of nodes
		 * selected for this job to be allocated
		 */
		if ((job_ptr->start_time <= now) &&
		    (bit_overlap_any(avail_bitmap, cg_node_bitmap) ||
		     bit_overlap_any(avail_bitmap, rs_node_bitmap))) {
			/* Need to wait for in-progress completion/epilog */
			job_ptr->start_time = now + 1;
			later_start = 0;
		}
		if ((job_ptr->start_time <= now) &&
		    ((bb = bb_g_job_test_stage_in(job_ptr, true)) != 1)) {
			if (job_ptr->state_reason != WAIT_NO_REASON) {
				/*
				 * Don't change state_reason if it was already
				 * set.
				 */
				;
			} else if (bb == -1) {
				/*
				 * Set reason now instead of in if (bb == -1)
				 * below for the sched_debug3()
				 */
				xfree(job_ptr->state_desc);
				job_ptr->state_reason =
					WAIT_BURST_BUFFER_RESOURCE;
			} else {	/* bb == 0 */
				xfree(job_ptr->state_desc);
				job_ptr->state_reason=WAIT_BURST_BUFFER_STAGING;
				/*
				 * Cannot start now, set start time in the
				 * future.
				 */
				job_ptr->start_time = now + 1;
			}
			sched_debug3("%pJ. State=%s. Reason=%s. Priority=%u.",
				     job_ptr,
				     job_state_string(job_ptr->job_state),
				     job_reason_string(job_ptr->state_reason),
				     job_ptr->priority);
			last_job_update = now;
			_set_job_time_limit(job_ptr, orig_time_limit);
			later_start = 0;
			if (bb == -1) {
				/*
				 * bb == -1 means that burst buffer stage-in
				 * hasn't started yet. Set an estimated start
				 * time so stage-in can start.
				 *
				 * Clear reject_array_job; otherwise we'll skip
				 * looking at other jobs in this array (if this
				 * is a job array), therefore we won't set
				 * estimated start times, therefore we won't be
				 * able to start stage-in for any other jobs in
				 * this array.
				 */
				job_ptr->start_time =
					bb_g_job_get_est_start(job_ptr);
				reject_array_job = NULL;
				reject_array_part = NULL;
				reject_array_resv = NULL;
				continue;
			}
		} else if ((job_ptr->het_job_id == 0) &&
			   (job_ptr->start_time <= now)) { /* Can start now */
			uint32_t save_time_limit = job_ptr->time_limit;
			uint32_t hard_limit;
			bool reset_time = false;
			int rc;

			/* get fed job lock from origin cluster */
			if (fed_mgr_job_lock(job_ptr)) {
				log_flag(BACKFILL, "%pJ can't get fed job lock from origin cluster to backfill job",
					 job_ptr);
				rc = ESLURM_FED_JOB_LOCK;
				goto skip_start;
			}

			rc = _start_job(job_ptr, resv_bitmap);

			if (rc == SLURM_SUCCESS) {
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
			if (qos_flags & QOS_FLAG_NO_RESERVE) {
				if (orig_time_limit == NO_VAL) {
					acct_policy_alter_job(
						job_ptr, comp_time_limit);
					job_ptr->time_limit = comp_time_limit;
					job_ptr->limit_set.time = 1;
				} else {
					acct_policy_alter_job(
						job_ptr, orig_time_limit);
					_set_job_time_limit(job_ptr,
							    orig_time_limit);
				}
			} else if ((rc == SLURM_SUCCESS) && job_ptr->time_min) {
				/* Set time limit as high as possible */
				acct_policy_alter_job(job_ptr, comp_time_limit);
				job_ptr->time_limit = comp_time_limit;
				reset_time = true;
			} else if (orig_time_limit == NO_VAL) {
				acct_policy_alter_job(job_ptr, comp_time_limit);
				job_ptr->time_limit = comp_time_limit;
				job_ptr->limit_set.time = 1;
			} else if (deadline_time_limit &&
				   (rc == SLURM_SUCCESS)) {
				acct_policy_alter_job(job_ptr, comp_time_limit);
				job_ptr->time_limit = comp_time_limit;
				reset_time = true;
			} else {
				acct_policy_alter_job(job_ptr, orig_time_limit);
				_set_job_time_limit(job_ptr, orig_time_limit);
			}
			/*
			 * Only set end_time if start_time is set,
			 * or else end_time will be small (ie. 1969).
			 */
			if (IS_JOB_FINISHED(job_ptr)) {
				/* Zero size or killed on startup */
			} else if (job_ptr->start_time) {
				node_space_handler_t ns_handler = {
					.node_space = node_space,
					.node_space_recs = &node_space_recs,
				};

				if (job_ptr->time_limit == INFINITE)
					hard_limit = YEAR_SECONDS;
				else
					hard_limit = job_ptr->time_limit * 60;
				job_ptr->end_time = job_ptr->start_time +
						    hard_limit;
				/*
				 * Only set if start_time. end_time must be set
				 * beforehand for _reset_job_time_limit.
				 */
				if (reset_time) {
					_reset_job_time_limit(job_ptr, now,
							      node_space);
					time_limit = job_ptr->time_limit;
				}

				_bf_reserve_running(job_ptr, &ns_handler);
			} else if (rc == SLURM_SUCCESS) {
				error("start_time of 0 on successful backfill. This shouldn't happen. :)");
			}

			if ((rc == ESLURM_RESERVATION_BUSY) ||
			    (rc == ESLURM_ACCOUNTING_POLICY &&
			     !assoc_limit_stop)) {
				/* Unknown future start time, just skip job */
				job_ptr->start_time = orig_start_time;
				_set_job_time_limit(job_ptr, orig_time_limit);
				continue;
			} else if (rc == ESLURM_ACCOUNTING_POLICY) {
				/* Unknown future start time. Determining
				 * when it can start with certainty requires
				 * when every running and pending job starts
				 * and ends and tracking all of there resources.
				 * That requires very high overhead, that we
				 * don't want to add. Estimate that it can start
				 * after the next job ends (or in 5 minutes if
				 * we don't have that information yet). */
				if (later_start)
					job_ptr->start_time = later_start;
				else
					job_ptr->start_time = now + 500;
				if (job_ptr->qos_blocking_ptr &&
				    job_state_qos_grp_limit(
					    job_ptr->state_reason)) {
					assoc_mgr_lock(&qos_read_lock);
					qos_ptr = job_ptr->qos_blocking_ptr;
					if (qos_ptr->blocked_until <
					    job_ptr->start_time) {
						qos_ptr->blocked_until =
						job_ptr->start_time;
					}
					assoc_mgr_unlock(&qos_read_lock);
				}
			} else if (rc != SLURM_SUCCESS) {
				log_flag(BACKFILL, "planned start of %pJ failed: %s",
					 job_ptr, slurm_strerror(rc));
				/* Drop through and reserve these resources.
				 * Likely due to state changes during sleep.
				 * Make best-effort based upon original state */
				_set_job_time_limit(job_ptr, orig_time_limit);
				later_start = 0;
			} else {
				/* Started this job, move to next one */

				/* Clear assumed rejected array status */
				reject_array_job = NULL;
				reject_array_part = NULL;
				reject_array_resv = NULL;

				/* Update the database if job time limit
				 * changed and move to next job */
				if (save_time_limit != job_ptr->time_limit)
					jobacct_storage_job_start_direct(
							acct_db_conn, job_ptr);
				job_start_cnt++;
				if (max_backfill_jobs_start &&
				    (job_start_cnt >= max_backfill_jobs_start)){
					log_flag(BACKFILL, "bf_max_job_start limit of %d reached",
						 max_backfill_jobs_start);
					break;
				}
				if (is_job_array_head &&
				    (job_ptr->array_task_id != NO_VAL)) {
					/* Try starting next task of job array */
					job_record_t *tmp = job_ptr;
					job_ptr = find_job_record(job_ptr->
								  array_job_id);
					if (job_ptr && (job_ptr != tmp) &&
					    IS_JOB_PENDING(job_ptr) &&
					    (bb_g_job_test_stage_in(
						    job_ptr, false) == 1))
						goto next_task;
				}
				continue;
			}
		} else if (job_ptr->het_job_id != 0) {
			uint32_t max_time_limit;
			max_time_limit =_get_job_max_tl(job_ptr, now,
						        node_space);
			comp_time_limit = MIN(comp_time_limit, max_time_limit);
			job_ptr->node_cnt_wag =
					MAX(bit_set_count(avail_bitmap), 1);
			_het_job_start_set(job_ptr, job_ptr->start_time,
					   comp_time_limit);
			_set_job_time_limit(job_ptr, orig_time_limit);
			if (bf_hetjob_immediate &&
			    (!max_backfill_jobs_start ||
			     (job_start_cnt < max_backfill_jobs_start)))
				_het_job_start_test(node_space,
						    job_ptr->het_job_id);
		}

		if ((job_ptr->start_time > now) && (job_no_reserve != 0)) {
			if ((orig_start_time != 0) &&
			    (orig_start_time < job_ptr->start_time)) {
				/* Can start earlier in different partition */
				job_ptr->start_time = orig_start_time;
			} else {
				log_flag(BACKFILL, "%pJ StartTime set but no backfill reservation created.",
					 job_ptr);
			}
			_set_job_time_limit(job_ptr, orig_time_limit);
			continue;
		}

		if (later_start && (job_ptr->start_time > later_start)) {
			/* Try later when some nodes currently reserved for
			 * pending jobs are free */
			log_flag(BACKFILL, "Try later %pJ later_start %ld",
			         job_ptr, later_start);
			job_ptr->start_time = 0;
			goto TRY_LATER;
		}

		start_time  = job_ptr->start_time;
		end_reserve = job_ptr->start_time + boot_time +
			      (time_limit * 60);
		start_time  = (start_time / backfill_resolution) *
			      backfill_resolution;
		end_reserve = (end_reserve / backfill_resolution) *
			      backfill_resolution;

		if (job_ptr->start_time > (sched_start + backfill_window)) {
			/* Starts too far in the future to worry about */
			if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL)
				_dump_job_sched(job_ptr, end_reserve,
						avail_bitmap);
			if ((orig_start_time != 0) &&
			    (orig_start_time < job_ptr->start_time)) {
				/* Can start earlier in different partition */
				job_ptr->start_time = orig_start_time;
			} else {
				log_flag(BACKFILL, "%pJ StartTime set to time after current backfill window. No reservation created",
					 job_ptr);
			}
			_set_job_time_limit(job_ptr, orig_time_limit);
			continue;
		}

		if ((job_ptr->start_time > now) &&
		    (job_ptr->state_reason != WAIT_BURST_BUFFER_RESOURCE) &&
		    (job_ptr->state_reason != WAIT_BURST_BUFFER_STAGING) &&
		    _test_resv_overlap(node_space, avail_bitmap, job_ptr,
				       start_time, end_reserve)) {
			/* This job overlaps with an existing reservation for
			 * job to be backfill scheduled, which the sched
			 * plugin does not know about. Try again later. */
			later_start = job_ptr->start_time;
			job_ptr->start_time = 0;
			log_flag(BACKFILL, "%pJ overlaps with existing reservation start_time=%u end_reserve=%u boot_time=%u later_start %ld",
				 job_ptr, start_time, end_reserve, boot_time,
				 later_start);
			goto TRY_LATER;
		}

		if (_het_job_deadlock_test(job_ptr)) {
			_set_job_time_limit(job_ptr, orig_time_limit);
			continue;
		}

		/*
		 * Add reservation to scheduling table if appropriate
		 */
		if (!assoc_limit_stop) {
			uint32_t selected_node_cnt;
			uint64_t tres_req_cnt[slurmctld_tres_cnt];
			uint16_t sockets_per_node;
			assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK
			};

			selected_node_cnt = bit_set_count(avail_bitmap);
			memcpy(tres_req_cnt, job_ptr->tres_req_cnt,
			       sizeof(tres_req_cnt));
			tres_req_cnt[TRES_ARRAY_CPU] =
				(uint64_t)(job_ptr->total_cpus ?
					   job_ptr->total_cpus :
					   job_ptr->details->min_cpus);

			sockets_per_node = job_get_sockets_per_node(job_ptr);
			tres_req_cnt[TRES_ARRAY_MEM] = job_get_tres_mem(
						job_ptr->job_resrcs,
						job_ptr->details->pn_min_memory,
						tres_req_cnt[TRES_ARRAY_CPU],
						selected_node_cnt,
						job_ptr->part_ptr,
						job_ptr->gres_list_req,
						(job_ptr->bit_flags &
						JOB_MEM_SET), sockets_per_node,
						job_ptr->details->num_tasks);

			tres_req_cnt[TRES_ARRAY_NODE] =
				(uint64_t)selected_node_cnt;

			assoc_mgr_lock(&locks);
			gres_ctld_set_job_tres_cnt(job_ptr->gres_list_req,
						   selected_node_cnt,
						   tres_req_cnt,
						   true);

			tres_req_cnt[TRES_ARRAY_BILLING] =
				assoc_mgr_tres_weighted(
					tres_req_cnt,
					job_ptr->part_ptr->billing_weights,
					slurm_conf.priority_flags, true);

			if (!acct_policy_job_runnable_post_select(job_ptr,
							  tres_req_cnt, true)) {
				assoc_mgr_unlock(&locks);
				log_flag(BACKFILL, "adding reservation for %pJ blocked by acct_policy_job_runnable_post_select",
					 job_ptr);
				_set_job_time_limit(job_ptr, orig_time_limit);
				continue;
			}
			assoc_mgr_unlock(&locks);
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL)
			_dump_job_sched(job_ptr, end_reserve, avail_bitmap);
		if (qos_flags & QOS_FLAG_NO_RESERVE) {
			_set_job_time_limit(job_ptr, orig_time_limit);
			continue;
		}

		if (bf_job_part_count_reserve) {
			if (_check_bf_usage(
				    job_ptr->part_ptr->bf_data->resv_usage,
				    bf_job_part_count_reserve,
				    orig_sched_start)) {
				_set_job_time_limit(job_ptr, orig_time_limit);
				continue;
			}
			job_ptr->part_ptr->bf_data->resv_usage->count++;
		}

		/* Clear assumed rejected array status */
		reject_array_job = NULL;
		reject_array_part = NULL;
		reject_array_resv = NULL;

		if ((orig_start_time == 0) ||
		    (job_ptr->start_time < orig_start_time)) {
			/* Can't start earlier in different partition. */
			xfree(job_ptr->sched_nodes);
			job_ptr->sched_nodes = bitmap2node_name(avail_bitmap);
			/*
			 * These nodes are planned.  We will set the state
			 * afterwards.
			 */
			bit_or(planned_bitmap, avail_bitmap);
		}
		bit_not(avail_bitmap);
		if ((!bf_one_resv_per_job || !orig_start_time) &&
		    !(job_ptr->bit_flags & JOB_MAGNETIC)) {
			if (node_space_recs >= bf_node_space_size) {
				log_flag(BACKFILL, "table size limit of %u reached",
					 bf_node_space_size);
				if ((max_backfill_job_per_part != 0) &&
				    (max_backfill_job_per_part >=
				     (bf_node_space_size / 2))) {
					error("bf_max_job_part >= bf_node_space_size / 2 (%u >= %u)",
					      max_backfill_job_per_part,
					      (bf_node_space_size / 2));
				} else if ((max_backfill_job_per_user != 0) &&
					   (max_backfill_job_per_user >
					    (bf_node_space_size / 2))) {
					info("warning: bf_max_job_user > bf_node_space_size / 2 (%u > %u)",
					     max_backfill_job_per_user,
					     (bf_node_space_size / 2));
				} else if  ((max_backfill_job_per_assoc != 0) &&
					    (max_backfill_job_per_assoc >
					     (bf_node_space_size / 2))) {
					info("warning: bf_max_job_assoc > bf_node_space_size / 2 (%u > %u)",
					     max_backfill_job_per_assoc,
					     (bf_node_space_size / 2));
				}
				_set_job_time_limit(job_ptr, orig_time_limit);
				break;
			}
			_add_reservation(start_time, end_reserve, avail_bitmap,
					 job_ptr, node_space, &node_space_recs);
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL_MAP)
			_dump_node_space_table(node_space);
		if ((orig_start_time != 0) &&
		    (orig_start_time < job_ptr->start_time)) {
			/* Can start earlier in different partition */
			job_ptr->start_time = orig_start_time;
		}
		_set_job_time_limit(job_ptr, orig_time_limit);
		if (job_ptr->array_recs) {
			/* Try making reservation for next task of job array */
			if (test_array_job_id != job_ptr->array_job_id) {
				test_array_job_id = job_ptr->array_job_id;
				test_array_count = 1;
			} else {
				test_array_count++;
			}

			/*
			 * Don't consider the next task if it would exceed the
			 * maximum number of runnable tasks. If max_run_tasks is
			 * 0, then it wasn't set, so ignore it.
			 */
			if ((test_array_count < bf_max_job_array_resv) &&
			    (test_array_count <
			     job_ptr->array_recs->task_cnt) &&
			    (!job_ptr->array_recs->max_run_tasks ||
			     (job_ptr->array_recs->pend_run_tasks <
			     job_ptr->array_recs->max_run_tasks)))
				goto next_task;
		}
	}

	_handle_planned(true);

	xfree(job_queue_rec);

	if (job_ptr) {
		/* Restore preemption state if needed. */
		_restore_preempt_state(job_ptr, &tmp_preempt_start_time,
				       &tmp_preempt_in_progress);
		job_resv_clear_magnetic_flag(job_ptr);
	}

	_het_job_deadlock_fini();
	if (!bf_hetjob_immediate &&
	    (!max_backfill_jobs_start ||
	     (job_start_cnt < max_backfill_jobs_start)))
		_het_job_start_test(node_space, 0);

	FREE_NULL_BITMAP(avail_bitmap);
	FREE_NULL_BITMAP(exc_core_bitmap);
	FREE_NULL_BITMAP(resv_bitmap);

	for (i = 0; ; ) {
		FREE_NULL_BITMAP(node_space[i].avail_bitmap);
		FREE_NULL_BF_LICENSES(node_space[i].licenses);
		if ((i = node_space[i].next) == 0)
			break;
	}
	xfree(node_space);
	FREE_NULL_LIST(job_queue);

	gettimeofday(&bf_time2, NULL);
	_do_diag_stats(&bf_time1, &bf_time2, node_space_recs);
	if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL) {
		END_TIMER;
		info("completed testing %u(%d) jobs, %s",
		     slurmctld_diag_stats.bf_last_depth,
		     job_test_count, TIME_STR);
	}

	slurm_mutex_lock(&slurmctld_config.thread_count_lock);
	if (slurmctld_config.server_thread_count >= 150) {
		info("%d pending RPCs at cycle end, consider "
		     "configuring max_rpc_cnt",
		     slurmctld_config.server_thread_count);
	}
	slurm_mutex_unlock(&slurmctld_config.thread_count_lock);

	return;
}

/* Try to start the job on any non-reserved nodes */
static int _start_job(job_record_t *job_ptr, bitstr_t *resv_bitmap)
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
	rc = select_nodes(job_ptr, false, NULL, NULL, false,
			  SLURMDB_JOB_FLAG_BACKFILL);
	if (is_job_array_head && job_ptr->details) {
		job_record_t *base_job_ptr;
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
	if (job_ptr->details) { /* select_nodes() might reset exc_node_bitmap */
		FREE_NULL_BITMAP(job_ptr->details->exc_node_bitmap);
		job_ptr->details->exc_node_bitmap = orig_exc_nodes;
	} else
		FREE_NULL_BITMAP(orig_exc_nodes);
	if (rc == SLURM_SUCCESS) {
		/* job initiated */
		last_job_update = time(NULL);
		info("Started %pJ in %s on %s",
		     job_ptr, job_ptr->part_ptr->name, job_ptr->nodes);
		power_g_job_start(job_ptr);
		if (job_ptr->batch_flag == 0)
			srun_allocate(job_ptr);
		else if (!IS_JOB_CONFIGURING(job_ptr))
			launch_job(job_ptr);
		slurmctld_diag_stats.backfilled_jobs++;
		slurmctld_diag_stats.last_backfilled_jobs++;
		if (job_ptr->het_job_id)
			slurmctld_diag_stats.backfilled_het_jobs++;
		log_flag(BACKFILL, "Jobs backfilled since boot: %u",
			 slurmctld_diag_stats.backfilled_jobs);
	} else if ((job_ptr->job_id != fail_jobid) &&
		   (rc != ESLURM_ACCOUNTING_POLICY)) {
		char *node_list;
		bit_not(resv_bitmap);
		node_list = bitmap2node_name(resv_bitmap);
		/* This happens when a job has sharing disabled and
		 * a selected node is still completing some job,
		 * which should be a temporary situation. */
		verbose("Failed to start %pJ with %s avail: %s",
			job_ptr, node_list, slurm_strerror(rc));
		xfree(node_list);
		fail_jobid = job_ptr->job_id;
	} else {
		debug3("Failed to start %pJ: %s",
		       job_ptr, slurm_strerror(rc));
	}

	return rc;
}

/*
 * Compute a job's maximum time based upon conflicts in resources
 * planned for use by other jobs and that job's min/max time limit
 * Return NO_VAL if no restriction
 */
static uint32_t _get_job_max_tl(job_record_t *job_ptr, time_t now,
				node_space_map_t *node_space)
{
	int32_t j;
	time_t comp_time = 0;
	uint32_t max_tl = NO_VAL;

	if (job_ptr->time_min == 0)
		return max_tl;

	for (j = 0; ; ) {
		if ((node_space[j].begin_time != now) && // No current conflicts
		    (node_space[j].begin_time < job_ptr->end_time) &&
		    (!bit_super_set(job_ptr->node_bitmap,
				    node_space[j].avail_bitmap) ||
		     !bf_licenses_avail(node_space[j].licenses, job_ptr))) {
			/* Job overlaps pending job's resource reservation */
			if ((comp_time == 0) ||
			    (comp_time > node_space[j].begin_time))
				comp_time = node_space[j].begin_time;
		}
		if ((j = node_space[j].next) == 0)
			break;
	}

	if (comp_time != 0)
		max_tl = (comp_time - now + 59) / 60;

	return max_tl;
}

/*
 * Reset a job's time limit (and end_time) as high as possible
 *	within the range job_ptr->time_min and job_ptr->time_limit.
 *	Avoid using resources reserved for pending jobs or in resource
 *	reservations
 */
static void _reset_job_time_limit(job_record_t *job_ptr, time_t now,
				  node_space_map_t *node_space)
{
	int32_t j, resv_delay;
	uint32_t orig_time_limit = job_ptr->time_limit;
	uint32_t new_time_limit;

	for (j = 0; ; ) {
		if ((node_space[j].begin_time != now) && // No current conflicts
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
		info("%pJ time limit changed from %u to %u",
		     job_ptr, orig_time_limit, job_ptr->time_limit);
	}
}

/*
 * Report if any changes occurred to job, node, reservation
 * or partition information
 */
static bool _more_work(time_t last_backfill_time)
{
	bool rc = false;

	if ((last_job_update  >= last_backfill_time) ||
	    (last_node_update >= last_backfill_time) ||
	    (last_part_update >= last_backfill_time) ||
	    (last_resv_update >= last_backfill_time)) {
		rc = true;
	}

	return rc;
}

/* Create a reservation for a job in the future */
static void _add_reservation(uint32_t start_time, uint32_t end_reserve,
			     bitstr_t *res_bitmap, job_record_t *job_ptr,
			     node_space_map_t *node_space,
			     int *node_space_recs)
{
	bool placed = false;
	int i, j, one_before = 0, one_after = -1;

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
	/*
	 * Ensure that the job always occupies at least one bf_resolution
	 * slot within the map. This also fixes potential issues when
	 * running with bf_running_job_reserve if jobs have run past
	 * their timelimit but have not yet been terminated.
	 */
	if (end_reserve < (start_time + backfill_resolution))
		end_reserve = start_time + backfill_resolution;
	for (j = 0; ; ) {
		if (node_space[j].end_time > start_time) {
			/* insert start entry record */
			i = *node_space_recs;
			node_space[i].begin_time = start_time;
			node_space[i].end_time = node_space[j].end_time;
			node_space[j].end_time = start_time;
			node_space[i].avail_bitmap =
				bit_copy(node_space[j].avail_bitmap);
			node_space[i].licenses =
				bf_licenses_copy(node_space[j].licenses);
			node_space[i].next = node_space[j].next;
			node_space[j].next = i;
			(*node_space_recs)++;
			placed = true;
			break;
		}
		if (node_space[j].end_time == start_time) {
			/* no need to insert new start entry record */
			placed = true;
			break;
		}
		one_before = j;
		if ((j = node_space[j].next) == 0)
			break;
	}

	while (placed && (j = node_space[j].next)) {
		if (end_reserve < node_space[j].end_time) {
			/* insert end entry record */
			i = *node_space_recs;
			node_space[i].begin_time = end_reserve;
			node_space[i].end_time = node_space[j].end_time;
			node_space[j].end_time = end_reserve;
			node_space[i].avail_bitmap =
				bit_copy(node_space[j].avail_bitmap);
			node_space[i].licenses =
				bf_licenses_copy(node_space[j].licenses);
			node_space[i].next = node_space[j].next;
			node_space[j].next = i;
			(*node_space_recs)++;
		}

		/* merge in new usage with this record */
		if (res_bitmap) {
			bit_and(node_space[j].avail_bitmap, res_bitmap);
			bf_licenses_deduct(node_space[j].licenses, job_ptr);
		} else {
			/* setting up reservation licenses */
			bf_licenses_transfer(node_space[j].licenses, job_ptr);
		}

		if (end_reserve == node_space[j].end_time) {
			if (node_space[j].next)
				one_after = node_space[j].next;
			break;
		}
	}

	/* Drop records with identical bitmaps (up to one record).
	 * This can significantly improve performance of the backfill tests. */
	for (i = one_before; i != one_after; ) {
		if ((j = node_space[i].next) == 0)
			break;
		if (!bf_licenses_equal(node_space[i].licenses,
				       node_space[j].licenses)) {
			i = j;
			continue;
		}
		if (!bit_equal(node_space[i].avail_bitmap,
			       node_space[j].avail_bitmap)) {
			i = j;
			continue;
		}
		node_space[i].end_time = node_space[j].end_time;
		node_space[i].next = node_space[j].next;
		FREE_NULL_BITMAP(node_space[j].avail_bitmap);
		FREE_NULL_BF_LICENSES(node_space[j].licenses);
		break;
	}
}

/*
 * Determine if the resource specification for a new job overlaps with a
 *	reservation that the backfill scheduler has made for a job to be
 *	started in the future.
 * IN use_bitmap - nodes to be allocated
 * IN job_ptr - used for license and reservation info
 * IN start_time - start time of job
 * IN end_reserve - end time of job
 */
static bool _test_resv_overlap(node_space_map_t *node_space,
			       bitstr_t *use_bitmap, job_record_t *job_ptr,
			       uint32_t start_time, uint32_t end_reserve)
{
	bool overlap = false;
	int j = 0;

	while (true) {
		if ((node_space[j].end_time > start_time) &&
		    (node_space[j].begin_time < end_reserve)) {
			/*
			 * Jobs will run concurrently.
			 * Do they conflict for resources?
			 */
			if (!bit_super_set(use_bitmap,
					   node_space[j].avail_bitmap)) {
				overlap = true;
				break;
			}
			if (!bf_licenses_avail(node_space[j].licenses,
					       job_ptr)) {
				overlap = true;
				break;
			}
		}

		if ((j = node_space[j].next) == 0)
			break;
	}
	return overlap;
}

/*
 * Delete het_job_map_t record from het_job_list
 */
static void _het_job_map_del(void *x)
{
	het_job_map_t *map = (het_job_map_t *) x;
	FREE_NULL_LIST(map->het_job_rec_list);
	xfree(map);
}

/*
 * Return 1 if a het_job_map_t record with a specific het_job_id is found.
 * Always return 1 if "key" is zero.
 */
static int _het_job_find_map(void *x, void *key)
{
	het_job_map_t *map = (het_job_map_t *) x;
	uint32_t *het_job_id = (uint32_t *) key;

	if ((het_job_id == NULL) ||
	    (map->het_job_id == *het_job_id))
		return 1;
	return 0;
}

/*
 * Return 1 if a het_job_rec_t record with a specific job_id is found.
 */
static int _het_job_find_rec(void *x, void *key)
{
	het_job_rec_t *rec = (het_job_rec_t *) x;
	uint32_t *job_id = (uint32_t *) key;

	if (rec->job_id == *job_id)
		return 1;
	return 0;
}

/*
 * Remove vestigial elements from het_job_list. For still active element,
 * clear the previously computted start time. This is used to periodically clear
 * history so that heterogeneous jobs do not keep getting deferred based
 * upon old system state
 */
static void _het_job_start_clear(void)
{
	het_job_map_t *map;
	ListIterator iter;

	iter = list_iterator_create(het_job_list);
	while ((map = list_next(iter))) {
		if (map->prev_start == 0) {
			list_delete_item(iter);
		} else {
			map->prev_start = 0;
			list_flush(map->het_job_rec_list);
		}
	}
	list_iterator_destroy(iter);
}

/*
 * For a given het_job_map_t record, determine the earliest that it can start,
 * which is the time at which it's latest starting component begins. The
 * "exclude_job_id" is used to exclude a hetjob component currntly being
 * tested to start, presumably in a different partition.
 */
static time_t _het_job_start_compute(het_job_map_t *map,
				     uint32_t exclude_job_id)
{
	ListIterator iter;
	het_job_rec_t *rec;
	time_t latest_start = map->prev_start;

	iter = list_iterator_create(map->het_job_rec_list);
	while ((rec = list_next(iter))) {
		if (rec->job_id == exclude_job_id)
			continue;
		latest_start = MAX(latest_start, rec->latest_start);
	}
	list_iterator_destroy(iter);

	return latest_start;
}

/*
 * Return the earliest that a job can start based upon _other_ components of
 * that same heterogeneous job. Return 0 if no limitation.
 *
 * If the job's state reason is BeginTime (the way all hetjobs start) and that
 * time is passed, then clear the reason field.
 */
static time_t _het_job_start_find(job_record_t *job_ptr)
{
	het_job_map_t *map;
	time_t latest_start = (time_t) 0;

	if (job_ptr->het_job_id) {
		map = list_find_first(het_job_list, _het_job_find_map,
				      &job_ptr->het_job_id);
		if (map) {
			latest_start = _het_job_start_compute(map,
							      job_ptr->job_id);
		}

		log_flag(HETJOB, "%pJ in partition %s expected to start in %ld secs",
			 job_ptr, job_ptr->part_ptr->name,
			 MAX(0, latest_start - time(NULL)));
	}

	return latest_start;
}

/*
 * Record the earliest that a hetjob component can start. If it can be
 * started in multiple partitions, we only record the earliest start time
 * for the job in any partition.
 */
static void _het_job_start_set(job_record_t *job_ptr, time_t latest_start,
			       uint32_t comp_time_limit)
{
	het_job_map_t *map;
	het_job_rec_t *rec;

	if (comp_time_limit == NO_VAL)
		comp_time_limit = job_ptr->time_limit;
	if (job_ptr->het_job_id) {
		map = list_find_first(het_job_list, _het_job_find_map,
				      &job_ptr->het_job_id);
		if (map) {
			if (!map->comp_time_limit) {
				map->comp_time_limit = comp_time_limit;
			} else {
				map->comp_time_limit = MIN(map->comp_time_limit,
							   comp_time_limit);
			}
			rec = list_find_first(map->het_job_rec_list,
					      _het_job_find_rec,
					      &job_ptr->job_id);
			if (rec && (rec->latest_start <= latest_start)) {
				/*
				 * This job can start an earlier time in
				 * some other partition, so ignore new info
				 */
			} else if (rec) {
				rec->latest_start = latest_start;
				rec->part_ptr = job_ptr->part_ptr;
			} else {
				rec = xmalloc(sizeof(het_job_rec_t));
				rec->job_id = job_ptr->job_id;
				rec->job_ptr = job_ptr;
				rec->latest_start = latest_start;
				rec->part_ptr = job_ptr->part_ptr;
				list_append(map->het_job_rec_list, rec);
			}
		} else {
			rec = xmalloc(sizeof(het_job_rec_t));
			rec->job_id = job_ptr->job_id;
			rec->job_ptr = job_ptr;
			rec->latest_start = latest_start;
			rec->part_ptr = job_ptr->part_ptr;

			map = xmalloc(sizeof(het_job_map_t));
			map->comp_time_limit = comp_time_limit;
			map->het_job_id = job_ptr->het_job_id;
			map->het_job_rec_list = list_create(xfree_ptr);
			list_append(map->het_job_rec_list, rec);
			list_append(het_job_list, map);
		}

		log_flag(HETJOB, "%pJ in partition %s set to start in %ld secs",
			 job_ptr, job_ptr->part_ptr->name,
			 MAX(0, _het_job_start_compute(map, 0) - time(NULL)));
	}
}

/*
 * Return TRUE if we have expected start times for all components of a hetjob
 * and all components are valid and runable.
 *
 * NOTE: This should never happen, but we will also start the job if all of the
 * other components are already running,
 */
static bool _het_job_full(het_job_map_t *map)
{
	job_record_t *het_job_ptr, *job_ptr;
	ListIterator iter;
	bool rc = true;

	het_job_ptr = find_job_record(map->het_job_id);
	if (!het_job_ptr || !het_job_ptr->het_job_list ||
	    (!IS_JOB_RUNNING(het_job_ptr) &&
	     !_job_runnable_now(het_job_ptr))) {
		return false;
	}

	iter = list_iterator_create(het_job_ptr->het_job_list);
	while ((job_ptr = list_next(iter))) {
		if ((job_ptr->magic != JOB_MAGIC) ||
		    (job_ptr->het_job_id != map->het_job_id)) {
			rc = false;	/* bad job pointer */
			break;
		}
		if (IS_JOB_RUNNING(job_ptr))
			continue;
		if (!list_find_first(map->het_job_rec_list, _het_job_find_rec,
				     &job_ptr->job_id) ||
		    !_job_runnable_now(job_ptr)) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(iter);

	return rc;
}

/*
 * Determine if all components of a hetjob can be started now or are
 * prevented from doing so because of association or QOS limits.
 * Return true if they can all start.
 *
 * NOTE: That a hetjob passes this test does not mean that it will be able
 * to run. For example, this test assumues resource allocation at the CPU level.
 * If each task is allocated one core, with 2 CPUs, then the CPU limit test
 * would not be accurate.
 */
static bool _het_job_limit_check(het_job_map_t *map, time_t now)
{
	job_record_t *job_ptr;
	het_job_rec_t *rec;
	ListIterator iter;
	int begun_jobs = 0, fini_jobs = 0, slurmctld_tres_size;
	bool runnable = true;
	uint32_t selected_node_cnt;
	uint64_t tres_req_cnt[slurmctld_tres_cnt];
	uint64_t **tres_alloc_save = NULL;

	tres_alloc_save = xcalloc(list_count(map->het_job_rec_list),
				  sizeof(uint64_t *));
	slurmctld_tres_size = sizeof(uint64_t) * slurmctld_tres_cnt;
	iter = list_iterator_create(map->het_job_rec_list);
	while ((rec = list_next(iter))) {
		uint16_t sockets_per_node;
		assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
			READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK, NO_LOCK };

		job_ptr = rec->job_ptr;
		job_ptr->part_ptr = rec->part_ptr;
		selected_node_cnt = job_ptr->node_cnt_wag;
		memcpy(tres_req_cnt, job_ptr->tres_req_cnt,
		       slurmctld_tres_size);
		tres_req_cnt[TRES_ARRAY_CPU] = (uint64_t)(job_ptr->total_cpus ?
					       job_ptr->total_cpus :
					       job_ptr->details->min_cpus);
		sockets_per_node = job_get_sockets_per_node(job_ptr);
		tres_req_cnt[TRES_ARRAY_MEM] = job_get_tres_mem(
					       job_ptr->job_resrcs,
					       job_ptr->details->pn_min_memory,
					       tres_req_cnt[TRES_ARRAY_CPU],
					       selected_node_cnt,
					       job_ptr->part_ptr,
					       job_ptr->gres_list_req,
					       (job_ptr->bit_flags &
						JOB_MEM_SET), sockets_per_node,
					       job_ptr->details->num_tasks);
		tres_req_cnt[TRES_ARRAY_NODE] = (uint64_t)selected_node_cnt;

		assoc_mgr_lock(&locks);
		gres_ctld_set_job_tres_cnt(job_ptr->gres_list_req,
					   selected_node_cnt,
					   tres_req_cnt, true);

		tres_req_cnt[TRES_ARRAY_BILLING] =
			assoc_mgr_tres_weighted(
				tres_req_cnt,
				job_ptr->part_ptr->billing_weights,
				slurm_conf.priority_flags, true);

		if (acct_policy_job_runnable_pre_select(job_ptr, true) &&
		    acct_policy_job_runnable_post_select(job_ptr,
							 tres_req_cnt, true)) {
			assoc_mgr_unlock(&locks);
			tres_alloc_save[begun_jobs++] = job_ptr->tres_alloc_cnt;
			job_ptr->tres_alloc_cnt = xmalloc(slurmctld_tres_size);
			memcpy(job_ptr->tres_alloc_cnt, tres_req_cnt,
			       slurmctld_tres_size);
			acct_policy_job_begin(job_ptr);

		} else {
			assoc_mgr_unlock(&locks);
			runnable = false;
			break;
		}
	}

	list_iterator_reset(iter);
	while ((rec = list_next(iter))) {
		job_ptr = rec->job_ptr;
		if (begun_jobs > fini_jobs) {
			time_t end_time_exp = job_ptr->end_time_exp;
			time_t end_time = job_ptr->end_time;
			uint32_t job_state = job_ptr->job_state;
			/* Simulate normal job completion */
			job_ptr->end_time_exp = now;
			job_ptr->end_time = job_ptr->start_time;
			job_ptr->job_state = JOB_COMPLETE | JOB_COMPLETING;
			acct_policy_job_fini(job_ptr);
			job_ptr->end_time_exp = end_time_exp;
			job_ptr->end_time = end_time;
			job_ptr->job_state = job_state;
			xfree(job_ptr->tres_alloc_cnt);
			job_ptr->tres_alloc_cnt = tres_alloc_save[fini_jobs++];
		}
	}
	list_iterator_destroy(iter);
	xfree(tres_alloc_save);

	return runnable;
}

/*
 * Start all components of a hetjob now
 */
static int _het_job_start_now(het_job_map_t *map, node_space_map_t *node_space)
{
	job_record_t *job_ptr;
	bitstr_t *avail_bitmap = NULL, *exc_core_bitmap = NULL;
	bitstr_t *resv_bitmap = NULL, *used_bitmap = NULL;
	het_job_rec_t *rec;
	ListIterator iter;
	int mcs_select, rc = SLURM_SUCCESS;
	bool resv_overlap = false;
	time_t now = time(NULL), start_res;
	uint32_t hard_limit;

	iter = list_iterator_create(map->het_job_rec_list);
	while ((rec = list_next(iter))) {
		bool reset_time = false;
		job_ptr = rec->job_ptr;
		job_ptr->part_ptr = rec->part_ptr;

		/*
		 * Identify the nodes which this job can use
		 */
		start_res = now;
		rc = job_test_resv(job_ptr, &start_res, true, &avail_bitmap,
				   &exc_core_bitmap, &resv_overlap, false);
		FREE_NULL_BITMAP(exc_core_bitmap);
		if (rc != SLURM_SUCCESS) {
			error("%pJ failed to start due to reservation",
			      job_ptr);
			FREE_NULL_BITMAP(avail_bitmap);
			break;
		}
		bit_and(avail_bitmap, job_ptr->part_ptr->node_bitmap);
		bit_and(avail_bitmap, up_node_bitmap);
		if (used_bitmap)
			bit_and_not(avail_bitmap, used_bitmap);
		filter_by_node_owner(job_ptr, avail_bitmap);
		mcs_select = slurm_mcs_get_select(job_ptr);
		filter_by_node_mcs(job_ptr, mcs_select, avail_bitmap);
		if (job_ptr->details->exc_node_bitmap) {
			bit_and_not(avail_bitmap,
				job_ptr->details->exc_node_bitmap);
		}

		if (fed_mgr_job_lock(job_ptr)) {
			error("%pJ failed to start due to fed job lock",
			      job_ptr);
			FREE_NULL_BITMAP(avail_bitmap);
			continue;
		}

		resv_bitmap = avail_bitmap;
		avail_bitmap = NULL;
		bit_not(resv_bitmap);
		rc = _start_job(job_ptr, resv_bitmap);
		FREE_NULL_BITMAP(resv_bitmap);
		if (rc == SLURM_SUCCESS) {
			/*
			 * If the following fails because of network
			 * connectivity, the origin cluster should ask
			 * when it comes back up if the cluster_lock
			 * cluster actually started the job
			 */
			fed_mgr_job_start(job_ptr, job_ptr->start_time);
			log_flag(HETJOB, "%pJ started", job_ptr);
			if (!used_bitmap && job_ptr->node_bitmap)
				used_bitmap = bit_copy(job_ptr->node_bitmap);
			else if (job_ptr->node_bitmap)
				bit_or(used_bitmap, job_ptr->node_bitmap);
		} else {
			fed_mgr_job_unlock(job_ptr);
			break;
		}
		if (job_ptr->time_min) {
			/* Set time limit as high as possible */
			acct_policy_alter_job(job_ptr, map->comp_time_limit);
			job_ptr->time_limit = map->comp_time_limit;
			reset_time = true;
		}
		if (job_ptr->start_time) {
			if (job_ptr->time_limit == INFINITE)
				hard_limit = YEAR_SECONDS;
			else
				hard_limit = job_ptr->time_limit * 60;
			job_ptr->end_time = job_ptr->start_time + hard_limit;
			/*
			 * Only set if start_time. end_time must be set
			 * beforehand for _reset_job_time_limit.
			 */
			if (reset_time)
				_reset_job_time_limit(job_ptr, now, node_space);
		}
		if (reset_time)
			jobacct_storage_job_start_direct(acct_db_conn, job_ptr);
	}
	list_iterator_destroy(iter);
	FREE_NULL_BITMAP(used_bitmap);

	return rc;
}

/*
 * Deallocate all components if failed hetjob start
 */
static void _het_job_kill_now(het_job_map_t *map)
{
	job_record_t *job_ptr;
	het_job_rec_t *rec;
	ListIterator iter;
	time_t now = time(NULL);
	int cred_lifetime = 1200;
	uint32_t save_bitflags;

	(void) slurm_cred_ctx_get(slurmctld_config.cred_ctx,
				  SLURM_CRED_OPT_EXPIRY_WINDOW,
				  &cred_lifetime);
	iter = list_iterator_create(map->het_job_rec_list);
	while ((rec = list_next(iter))) {
		job_ptr = rec->job_ptr;
		if (IS_JOB_PENDING(job_ptr))
			continue;
		info("Deallocate %pJ due to hetjob start failure",
		     job_ptr);
		job_ptr->details->begin_time = now + cred_lifetime + 1;
		job_ptr->end_time   = now;
		job_ptr->job_state  = JOB_PENDING | JOB_COMPLETING;
		last_job_update     = now;
		build_cg_bitmap(job_ptr);
		job_completion_logger(job_ptr, false);
		deallocate_nodes(job_ptr, false, false, false);
		/*
		 * Since the job_completion_logger() removes the submit,
		 * we need to add it again, but don't stage-out burst buffer
		 */
		save_bitflags = job_ptr->bit_flags;
		job_ptr->bit_flags |= JOB_KILL_HURRY;
		acct_policy_add_job_submit(job_ptr);
		job_ptr->bit_flags = save_bitflags;
		if (!job_ptr->node_bitmap_cg ||
		    (bit_set_count(job_ptr->node_bitmap_cg) == 0))
			batch_requeue_fini(job_ptr);
	}
	list_iterator_destroy(iter);
}

/*
 * If all components of a heterogeneous job can start now, then do so
 * node_space IN - map of available resources through time
 * map IN - info about this heterogeneous job
 * single IN - true if testing single heterogeneous jobs
 */
static void _het_job_start_test_single(node_space_map_t *node_space,
				       het_job_map_t *map, bool single)
{
	time_t now = time(NULL);
	int rc;

	if (!map)
		return;

	if (!_het_job_full(map)) {
		log_flag(HETJOB, "Hetjob %u has indefinite start time",
			 map->het_job_id);
		if (!single)
			map->prev_start = now + YEAR_SECONDS;
		return;
	}

	map->prev_start = _het_job_start_compute(map, 0);
	if (map->prev_start > now) {
		log_flag(HETJOB, "Hetjob %u should be able to start in %u seconds",
			 map->het_job_id, (uint32_t) (map->prev_start - now));
		return;
	}

	if (!_het_job_limit_check(map, now)) {
		log_flag(HETJOB, "Hetjob %u prevented from starting by account/QOS limit",
			 map->het_job_id);

		map->prev_start = now + YEAR_SECONDS;
		return;
	}

	log_flag(HETJOB, "Attempting to start hetjob %u", map->het_job_id);

	rc = _het_job_start_now(map, node_space);
	if (rc != SLURM_SUCCESS) {
		log_flag(HETJOB, "Failed to start hetjob %u", map->het_job_id);
		_het_job_kill_now(map);
	} else {
		job_start_cnt += list_count(map->het_job_rec_list);
		if (max_backfill_jobs_start &&
		    (job_start_cnt >= max_backfill_jobs_start)) {
			log_flag(BACKFILL, "bf_max_job_start limit of %d reached",
				 max_backfill_jobs_start);
		}
	}

}

static int _het_job_start_test_list(void *map, void *node_space)
{
	if (!max_backfill_jobs_start ||
	    (job_start_cnt < max_backfill_jobs_start))
		_het_job_start_test_single(node_space, map, false);

	return SLURM_SUCCESS;
}


/*
 * If all components of a heterogeneous job can start now, then do so
 * node_space IN - map of available resources through time
 * het_job_id IN - the ID of the heterogeneous job to evaluate,
 *		    if zero then evaluate all heterogeneous jobs
 */
static void _het_job_start_test(node_space_map_t *node_space, uint32_t het_job_id)
{
	het_job_map_t *map = NULL;

	if (!het_job_id) {
		/* Test all maps. */
		(void)list_for_each(het_job_list,
				    _het_job_start_test_list, node_space);
	} else {
		/* Test single map. */
		map = list_find_first(het_job_list, _het_job_find_map,
				      &het_job_id);
		_het_job_start_test_single(node_space, map, true);
	}
}

static void _deadlock_global_list_del(void *x)
{
	deadlock_part_struct_t *dl_part_ptr = (deadlock_part_struct_t *) x;
	FREE_NULL_LIST(dl_part_ptr->deadlock_job_list);
	xfree(dl_part_ptr);
}

static int _deadlock_part_list_srch(void *x, void *key)
{
	deadlock_job_struct_t *dl_job = (deadlock_job_struct_t *) x;
	job_record_t *job_ptr = (job_record_t *) key;
	if (dl_job->het_job_id == job_ptr->het_job_id)
		return 1;
	return 0;
}

static int _deadlock_part_list_srch2(void *x, void *key)
{
	deadlock_job_struct_t *dl_job = (deadlock_job_struct_t *) x;
	deadlock_job_struct_t *dl_job2 = (deadlock_job_struct_t *) key;
	if (dl_job->het_job_id == dl_job2->het_job_id)
		return 1;
	return 0;
}

static int _deadlock_global_list_srch(void *x, void *key)
{
	deadlock_part_struct_t *dl_part = (deadlock_part_struct_t *) x;
	if (dl_part->part_ptr == (part_record_t *) key)
		return 1;
	return 0;
}

static int _deadlock_job_list_sort(void *x, void *y)
{
	deadlock_job_struct_t *dl_job_ptr1 = *(deadlock_job_struct_t **) x;
	deadlock_job_struct_t *dl_job_ptr2 = *(deadlock_job_struct_t **) y;
	if (dl_job_ptr1->start_time > dl_job_ptr2->start_time)
		return -1;
	else if (dl_job_ptr1->start_time < dl_job_ptr2->start_time)
		return 1;
	return 0;
}

/*
 * Call at end of backup execution to release memory allocated by
 * _het_job_deadlock_test()
 */
static void _het_job_deadlock_fini(void)
{
	FREE_NULL_LIST(deadlock_global_list);
}

/*
 * Determine if job can run at it's "start_time" or later.
 * job_ptr IN - job to test, set reason to "HET_JOB_DEADLOCK" if it will deadlock
 * RET true if the job can not run due to possible deadlock with other hetjob
 *
 * NOTE: If there are a large number of hetjobs this will be painfully slow
 *       as the algorithm must be order n^2
 */
static bool _het_job_deadlock_test(job_record_t *job_ptr)
{
	deadlock_job_struct_t  *dl_job_ptr  = NULL, *dl_job_ptr2 = NULL;
	deadlock_job_struct_t  *dl_job_ptr3 = NULL;
	deadlock_part_struct_t *dl_part_ptr = NULL, *dl_part_ptr2 = NULL;
	ListIterator job_iter, part_iter;
	bool have_deadlock = false;

	if (!job_ptr->het_job_id || !job_ptr->part_ptr)
		return false;

	/*
	 * Find the list representing the ordering of jobs in this specific
	 * partition and add this job in the list, sorted by job start time
	 */
	if (!deadlock_global_list) {
		deadlock_global_list = list_create(_deadlock_global_list_del);
	} else {
		dl_part_ptr = list_find_first(deadlock_global_list,
					      _deadlock_global_list_srch,
					      job_ptr->part_ptr);
	}
	if (!dl_part_ptr) {
		dl_part_ptr = xmalloc(sizeof(deadlock_part_struct_t));
		dl_part_ptr->deadlock_job_list = list_create(xfree_ptr);
		dl_part_ptr->part_ptr = job_ptr->part_ptr;
		list_append(deadlock_global_list, dl_part_ptr);
	} else {
		dl_job_ptr = list_find_first(dl_part_ptr->deadlock_job_list,
					     _deadlock_part_list_srch,
					     job_ptr);
	}
	if (!dl_job_ptr) {
		dl_job_ptr = xmalloc(sizeof(deadlock_job_struct_t));
		dl_job_ptr->het_job_id = job_ptr->het_job_id;
		dl_job_ptr->start_time = job_ptr->start_time;
		list_append(dl_part_ptr->deadlock_job_list, dl_job_ptr);
	} else if (dl_job_ptr->start_time < job_ptr->start_time) {
		dl_job_ptr->start_time = job_ptr->start_time;
	}
	list_sort(dl_part_ptr->deadlock_job_list, _deadlock_job_list_sort);

	/*
	 * Log current table of hetjob start times by partition
	 */
	if (slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL) {
		part_iter = list_iterator_create(deadlock_global_list);
		while ((dl_part_ptr2 = list_next(part_iter))){
			info("Partition %s Hetjobs:",
			     dl_part_ptr2->part_ptr->name);
			job_iter = list_iterator_create(dl_part_ptr2->
							deadlock_job_list);
			while ((dl_job_ptr2 = list_next(job_iter))) {
				info("   Hetjob %u to start at %"PRIu64,
				     dl_job_ptr2->het_job_id,
				     (uint64_t) dl_job_ptr2->start_time);
			}
			list_iterator_destroy(job_iter);
		}
		list_iterator_destroy(part_iter);
	}

	/*
	 * Determine if any hetjobs scheduled to start earlier than this job
	 * in this partition are scheduled to start after it in some other
	 * partition
	 */
	part_iter = list_iterator_create(deadlock_global_list);
	while ((dl_part_ptr2 = list_next(part_iter))){
		if (dl_part_ptr2 == dl_part_ptr) /* Current partition, skip it */
			continue;
		dl_job_ptr2 = list_find_first(dl_part_ptr2->deadlock_job_list,
					      _deadlock_part_list_srch,
					      job_ptr);
		if (!dl_job_ptr2) /* Hetjob not in this partition, no check */
			continue;
		job_iter = list_iterator_create(dl_part_ptr->deadlock_job_list);
		while ((dl_job_ptr2 = list_next(job_iter))) {
			if (dl_job_ptr2->het_job_id == dl_job_ptr->het_job_id)
				break;	/* Self */
			dl_job_ptr3 = list_find_first(
						dl_part_ptr2->deadlock_job_list,
						_deadlock_part_list_srch2,
						dl_job_ptr2);
			if (dl_job_ptr3 &&
			    (dl_job_ptr3->start_time < dl_job_ptr->start_time)){
				have_deadlock = true;
				break;
			}
		}
		list_iterator_destroy(job_iter);

		if (have_deadlock)
			log_flag(HETJOB, "Hetjob %u in partition %s would deadlock with hetjob %u in partition %s, skipping it",
				 dl_job_ptr->het_job_id,
				 dl_part_ptr->part_ptr->name,
				 dl_job_ptr3->het_job_id,
				 dl_part_ptr2->part_ptr->name);
		if (have_deadlock)
			break;
	}
	list_iterator_destroy(part_iter);

	return have_deadlock;
}
