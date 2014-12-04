/*****************************************************************************\
 *  burst_buffer_cray.c - Plugin for managing a Cray burst_buffer
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/burst_buffer/common/burst_buffer_common.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "burst_buffer" for SLURM burst_buffer) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will only
 * load a burst_buffer plugin if the plugin_type string has a prefix of
 * "burst_buffer/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "burst_buffer cray plugin";
const char plugin_type[]        = "burst_buffer/cray";
const uint32_t plugin_version   = 100;

/* Most state information is in a common structure so that we can more
 * easily use common functions from multiple burst buffer plugins */
static bb_state_t 	bb_state;

static void *	_bb_agent(void *args);
static uint32_t	_get_bb_size(struct job_record *job_ptr);
static void	_load_state(uint32_t job_id);
static void 	_my_sleep(int add_secs);
static int	_test_size_limit(struct job_record *job_ptr,uint32_t add_space);
static void	_timeout_bb_rec(void);

/* Perform periodic background activities */
static void *_bb_agent(void *args)
{
	/* Locks: write job */
	slurmctld_lock_t job_write_lock = {
		NO_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };

	while (!bb_state.term_flag) {
		_my_sleep(AGENT_INTERVAL);
		if (bb_state.term_flag)
			break;
		lock_slurmctld(job_write_lock);
		pthread_mutex_lock(&bb_state.bb_mutex);
		_load_state(0);
		_timeout_bb_rec();
		pthread_mutex_unlock(&bb_state.bb_mutex);
		unlock_slurmctld(job_write_lock);
	}
	return NULL;
}

/* Return the burst buffer size requested by a job */
static uint32_t _get_bb_size(struct job_record *job_ptr)
{
	char *tok;
	uint32_t bb_size_u = 0;

	if (job_ptr->burst_buffer) {
		tok = strstr(job_ptr->burst_buffer, "size=");
		if (tok)
			bb_size_u = bb_get_size_num(tok + 5);
	}

	return bb_size_u;
}

/*
 * Determine the current actual burst buffer state.
 * Run the program "get_sys_state" and parse stdout for details.
 * job_id IN - specific job to get information about, or 0 for all jobs
 */
static void _load_state(uint32_t job_id)
{
// FIXME: Need Cray interface here...
	bb_state.last_load_time = time(NULL);
}

static void _start_stage_in(uint32_t job_id)
{
// FIXME: Need Cray interface here...
}

static void _start_stage_out(uint32_t job_id)
{
// FIXME: Need Cray interface here...
}

static void _stop_stage_in(uint32_t job_id)
{
// FIXME: Need Cray interface here...
}

static void _stop_stage_out(uint32_t job_id)
{
// FIXME: Need Cray interface here...
}

/* Local sleep function, handles termination signal */
static void _my_sleep(int add_secs)
{
	struct timespec ts = {0, 0};
	struct timeval  tv = {0, 0};

	if (gettimeofday(&tv, NULL)) {		/* Some error */
		sleep(1);
		return;
	}

	ts.tv_sec  = tv.tv_sec + add_secs;
	ts.tv_nsec = tv.tv_usec * 1000;
	pthread_mutex_lock(&bb_state.term_mutex);
	if (!bb_state.term_flag) {
		pthread_cond_timedwait(&bb_state.term_cond,
				       &bb_state.term_mutex, &ts);
	}
	pthread_mutex_unlock(&bb_state.term_mutex);
}

/* Test if a job can be allocated a burst buffer.
 * This may preempt currently active stage-in for higher priority jobs.
 *
 * RET 0: Job can be started now
 *     1: Job exceeds configured limits, continue testing with next job
 *     2: Job needs more resources than currently available can not start,
 *        skip all remaining jobs
 */
static int _test_size_limit(struct job_record *job_ptr, uint32_t add_space)
{
	struct preempt_bb_recs *preempt_ptr = NULL;
	List preempt_list;
	ListIterator preempt_iter;
	bb_user_t *user_ptr;
	uint32_t tmp_u, tmp_j, lim_u;
	int add_total_space_needed = 0, add_user_space_needed = 0;
	int add_total_space_avail  = 0, add_user_space_avail  = 0;
	time_t now = time(NULL);
	bb_alloc_t *bb_ptr = NULL;
	int i;

	/* Determine if burst buffer can be allocated now for the job.
	 * If not, determine how much space must be free. */
	if (((bb_state.bb_config.job_size_limit  != NO_VAL) &&
	     (add_space > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL) &&
	     (add_space > bb_state.bb_config.user_size_limit)))
		return 1;

	if (bb_state.bb_config.user_size_limit != NO_VAL) {
		user_ptr = bb_find_user_rec(job_ptr->user_id, bb_state.bb_uhash);
		tmp_u = user_ptr->size  & (~BB_SIZE_IN_NODES);
		tmp_j = add_space       & (~BB_SIZE_IN_NODES);
		lim_u = bb_state.bb_config.user_size_limit & (~BB_SIZE_IN_NODES);

		add_user_space_needed = tmp_u + tmp_j - lim_u;
	}
	add_total_space_needed = bb_state.used_space + add_space - bb_state.total_space;
	if ((add_total_space_needed <= 0) &&
	    (add_user_space_needed  <= 0))
		return 0;

	/* Identify candidate burst buffers to revoke for higher priority job */
	preempt_list = list_create(bb_job_queue_del);
	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_ptr = bb_state.bb_hash[i];
		while (bb_ptr) {
			if (bb_ptr->job_id &&
			    (bb_ptr->use_time > now) &&
			    (bb_ptr->use_time > job_ptr->start_time)) {
				preempt_ptr = xmalloc(sizeof(
						struct preempt_bb_recs));
				preempt_ptr->bb_ptr = bb_ptr;
				preempt_ptr->job_id = bb_ptr->job_id;
				preempt_ptr->size = bb_ptr->size;
				preempt_ptr->use_time = bb_ptr->use_time;
				preempt_ptr->user_id = bb_ptr->user_id;
				list_push(preempt_list, preempt_ptr);

				add_total_space_avail += bb_ptr->size;
				if (bb_ptr->user_id == job_ptr->user_id);
					add_user_space_avail += bb_ptr->size;
			}
			bb_ptr = bb_ptr->next;
		}
	}

	if ((add_total_space_avail >= add_total_space_needed) &&
	    (add_user_space_avail  >= add_user_space_needed)) {
		list_sort(preempt_list, bb_preempt_queue_sort);
		preempt_iter = list_iterator_create(preempt_list);
		while ((preempt_ptr = list_next(preempt_iter)) &&
		       (add_total_space_needed || add_user_space_needed)) {
			if (add_user_space_needed &&
			    (preempt_ptr->user_id == job_ptr->user_id)) {
				_stop_stage_in(preempt_ptr->job_id);
				preempt_ptr->bb_ptr->cancelled = true;
				preempt_ptr->bb_ptr->end_time = 0;
				if (bb_state.bb_config.debug_flag) {
					info("%s: %s: Preempting stage-in of "
					     "job %u for job %u", plugin_type,
					     __func__, preempt_ptr->job_id,
					     job_ptr->job_id);
				}
				add_user_space_needed  -= preempt_ptr->size;
				add_total_space_needed -= preempt_ptr->size;
			}
			if ((add_total_space_needed > add_user_space_needed) &&
			    (preempt_ptr->user_id != job_ptr->user_id)) {
				_stop_stage_in(preempt_ptr->job_id);
				preempt_ptr->bb_ptr->cancelled = true;
				preempt_ptr->bb_ptr->end_time = 0;
				if (bb_state.bb_config.debug_flag) {
					info("%s: %s: Preempting stage-in of "
					     "job %u for job %u", plugin_type,
					     __func__, preempt_ptr->job_id,
					     job_ptr->job_id);
				}
				add_total_space_needed -= preempt_ptr->size;
			}
		}
		list_iterator_destroy(preempt_iter);
	}
	list_destroy(preempt_list);

	return 2;
}

/* Handle timeout of burst buffer events:
 * 1. Purge per-job burst buffer records when the stage-out has completed and
 *    the job has been purged from Slurm
 * 2. Test for StageInTimeout events
 * 3. Test for StageOutTimeout events
 */
static void _timeout_bb_rec(void)
{
	struct job_record *job_ptr;
	bb_alloc_t **bb_pptr, *bb_ptr = NULL;
	uint32_t age;
	time_t now = time(NULL);
	int i;

	for (i = 0; i < BB_HASH_SIZE; i++) {
		bb_pptr = &bb_state.bb_hash[i];
		bb_ptr = bb_state.bb_hash[i];
		while (bb_ptr) {
			if (bb_ptr->seen_time < bb_state.last_load_time) {
				if (bb_ptr->job_id == 0) {
					info("%s: Persistent burst buffer %s "
					     "purged",
					     __func__, bb_ptr->name);
				} else if (bb_state.bb_config.debug_flag) {
					info("%s: burst buffer for job %u "
					     "purged",
					     __func__, bb_ptr->job_id);
				}
				bb_remove_user_load(bb_ptr, &bb_state);
				*bb_pptr = bb_ptr->next;
				xfree(bb_ptr);
				break;
			}
			if ((bb_ptr->job_id != 0) &&
			    (bb_ptr->state >= BB_STATE_STAGED_OUT) &&
			    !find_job_record(bb_ptr->job_id)) {
				_stop_stage_out(bb_ptr->job_id);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
				*bb_pptr = bb_ptr->next;
				xfree(bb_ptr);
				break;
			}
			age = difftime(now, bb_ptr->state_time);
			if ((bb_ptr->job_id != 0) &&
			    (bb_ptr->state == BB_STATE_STAGING_IN) &&
			    (bb_state.bb_config.stage_in_timeout != 0) &&
			    (!bb_ptr->cancelled) &&
			    (age >= bb_state.bb_config.stage_in_timeout)) {
				_stop_stage_in(bb_ptr->job_id);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
				job_ptr = find_job_record(bb_ptr->job_id);
				if (job_ptr) {
					error("%s: StageIn timed out, holding "
					      "job %u",
					      __func__, bb_ptr->job_id);
					job_ptr->priority = 0;
					job_ptr->direct_set_prio = 1;
					job_ptr->state_reason = WAIT_HELD;
					xfree(job_ptr->state_desc);
					job_ptr->state_desc = xstrdup(
						"Burst buffer stage-in timeout");
					last_job_update = now;
				} else {
					error("%s: StageIn timed out for "
					      "vestigial job %u ",
					      __func__, bb_ptr->job_id);
				}
			}
			if ((bb_ptr->job_id != 0) &&
			    (bb_ptr->state == BB_STATE_STAGING_OUT) &&
			    (bb_state.bb_config.stage_out_timeout != 0) &&
			    (!bb_ptr->cancelled) &&
			    (age >= bb_state.bb_config.stage_out_timeout)) {
				error("%s: StageOut for job %u timed out",
				      __func__, bb_ptr->job_id);
				_stop_stage_out(bb_ptr->job_id);
				bb_ptr->cancelled = true;
				bb_ptr->end_time = 0;
			}
			bb_pptr = &bb_ptr->next;
			bb_ptr = bb_ptr->next;
		}
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	pthread_attr_t attr;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_load_config(&bb_state);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	bb_alloc_cache(&bb_state);
	slurm_attr_init(&attr);
	if (pthread_create(&bb_state.bb_thread, &attr, _bb_agent, NULL))
		error("Unable to start backfill thread: %m");
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is unloaded. Free all memory.
 */
extern int fini(void)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);

	pthread_mutex_lock(&bb_state.term_mutex);
	bb_state.term_flag = true;
	pthread_cond_signal(&bb_state.term_cond);
	pthread_mutex_unlock(&bb_state.term_mutex);

	if (bb_state.bb_thread) {
		pthread_join(bb_state.bb_thread, NULL);
		bb_state.bb_thread = 0;
	}
	bb_clear_config(&bb_state.bb_config);
	bb_clear_cache(&bb_state);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Load the current burst buffer state (e.g. how much space is available now).
 * Run at the beginning of each scheduling cycle in order to recognize external
 * changes to the burst buffer state (e.g. capacity is added, removed, fails,
 * etc.)
 *
 * init_config IN - true if called as part of slurmctld initialization
 * Returns a SLURM errno.
 */
extern int bb_p_load_state(bool init_config)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	_load_state(0);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Note configuration may have changed. Handle changes in BurstBufferParameters.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_reconfig(void)
{
	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);
	bb_load_config(&bb_state);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Pack current burst buffer state information for network transmission to
 * user (e.g. "scontrol show burst")
 *
 * Returns a SLURM errno.
 */
extern int bb_p_state_pack(Buf buffer, uint16_t protocol_version)
{
	uint32_t rec_count = 0;
	int eof, offset;

	pthread_mutex_lock(&bb_state.bb_mutex);
	if (bb_state.bb_config.debug_flag)
		info("%s: %s",  __func__, plugin_type);
	packstr((char *)plugin_type, buffer);	/* Remove "const" qualifier */
	offset = get_buf_offset(buffer);
	pack32(rec_count,        buffer);
	pack32(bb_state.total_space,      buffer);
	pack32(bb_state.used_space,       buffer);
	bb_pack_config(&bb_state.bb_config, buffer, protocol_version);
//FIXME: Add private data option
	rec_count = bb_pack_bufs(bb_state.bb_hash, buffer, protocol_version);
	if (rec_count != 0) {
		eof = get_buf_offset(buffer);
		set_buf_offset(buffer, offset);
		pack32(rec_count, buffer);
		set_buf_offset(buffer, eof);
	}
	if (bb_state.bb_config.debug_flag)
		info("%s: record_count:%u",  __func__, rec_count);
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Validate a job submit request with respect to burst buffer options.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_validate(struct job_descriptor *job_desc,
			     uid_t submit_uid)
{
	int32_t bb_size = 0;
	char *key;
	int i;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_user_id:%u, submit_uid:%d",
		     plugin_type, __func__, job_desc->user_id, submit_uid);
		info("%s: burst_buffer:%s", __func__, job_desc->burst_buffer);
		info("%s: script:%s", __func__, job_desc->script);
	}

//FIXME: Add function callout to set job_desc->burst_buffer based upon job_desc->script
	if (job_desc->burst_buffer) {
		key = strstr(job_desc->burst_buffer, "size=");
		if (key)
			bb_size = bb_get_size_num(key + 5);
	}
	if (bb_size == 0)
		return SLURM_SUCCESS;
	if (bb_size < 0)
		return ESLURM_BURST_BUFFER_LIMIT;

	pthread_mutex_lock(&bb_state.bb_mutex);
	if (((bb_state.bb_config.job_size_limit  != NO_VAL) &&
	     (bb_size > bb_state.bb_config.job_size_limit)) ||
	    ((bb_state.bb_config.user_size_limit != NO_VAL) &&
	     (bb_size > bb_state.bb_config.user_size_limit))) {
		pthread_mutex_unlock(&bb_state.bb_mutex);
		return ESLURM_BURST_BUFFER_LIMIT;
	}

	if (bb_state.bb_config.allow_users) {
		for (i = 0; bb_state.bb_config.allow_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.allow_users[i])
				break;
		}
		if (bb_state.bb_config.allow_users[i] == 0) {
			pthread_mutex_unlock(&bb_state.bb_mutex);
			return ESLURM_BURST_BUFFER_PERMISSION;
		}
	}

	if (bb_state.bb_config.deny_users) {
		for (i = 0; bb_state.bb_config.deny_users[i]; i++) {
			if (job_desc->user_id ==
			    bb_state.bb_config.deny_users[i])
				break;
		}
		if (bb_state.bb_config.deny_users[i] != 0) {
			pthread_mutex_unlock(&bb_state.bb_mutex);
			return ESLURM_BURST_BUFFER_PERMISSION;
		}
	}

	if (bb_size > bb_state.total_space) {
		info("Job from user %u requested burst buffer size of %u, "
		     "but total space is only %u",
		     job_desc->user_id, bb_size, bb_state.total_space);
	}

	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * For a given job, return our best guess if when it might be able to start
 */
extern time_t bb_p_job_get_est_start(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	time_t est_start = time(NULL);
	uint32_t bb_size;
	int rc;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}
	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    ((bb_size = _get_bb_size(job_ptr)) == 0))
		return est_start;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		rc = _test_size_limit(job_ptr, bb_size);
		if (rc == 0) {		/* Could start now */
			;
		} else if (rc == 1) {	/* Exceeds configured limits */
			est_start += 365 * 24 * 60 * 60;
		} else {		/* No space currently available */
			est_start = MAX(est_start, bb_state.next_end_time);
		}
	} else if (bb_ptr->state < BB_STATE_STAGED_IN) {
		est_start++;
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return est_start;
}

/*
 * Validate a job submit request with respect to burst buffer options.
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_try_stage_in(List job_queue)
{
	job_queue_rec_t *job_rec;
	List job_candidates;
	ListIterator job_iter;
	struct job_record *job_ptr;
	uint32_t bb_size;
	int rc;

	if (bb_state.bb_config.debug_flag)
		info("%s: %s", plugin_type,  __func__);

	/* Identify candidates to be allocated burst buffers */
	job_candidates = list_create(bb_job_queue_del);
	job_iter = list_iterator_create(job_queue);
	while ((job_ptr = list_next(job_iter))) {
		if (!IS_JOB_PENDING(job_ptr) ||
		    (job_ptr->start_time == 0) ||
		    (job_ptr->burst_buffer == NULL) ||
		    (job_ptr->burst_buffer[0] == '\0'))
			continue;
		bb_size = _get_bb_size(job_ptr);
		if (bb_size == 0)
			continue;
		job_rec = xmalloc(sizeof(job_queue_rec_t));
		job_rec->job_ptr = job_ptr;
		job_rec->bb_size = bb_size;
		list_push(job_candidates, job_rec);
	}
	list_iterator_destroy(job_iter);

	/* Sort in order of expected start time */
	list_sort(job_candidates, bb_job_queue_sort);

	pthread_mutex_lock(&bb_state.bb_mutex);
//FIXME	_set_bb_use_time(bb_state.bb_hash);
	job_iter = list_iterator_create(job_candidates);
	while ((job_rec = list_next(job_iter))) {
		job_ptr = job_rec->job_ptr;
		bb_size = job_rec->bb_size;

		if (bb_find_job_rec(job_ptr, bb_state.bb_hash))
			continue;

		rc = _test_size_limit(job_ptr, bb_size);
		if (rc == 1)
			continue;
		else if (rc == 2)
			break;
_start_stage_in(job_ptr->job_id);
//FIXME		_alloc_job_bb(job_ptr, bb_size);
	}
	list_iterator_destroy(job_iter);
	pthread_mutex_unlock(&bb_state.bb_mutex);
	list_destroy(job_candidates);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer stage-in is complete
 * job_ptr IN - Job to test
 * test_only IN - If false, then attempt to allocate burst buffer if possible
 *
 * RET: 0 - stage-in is underway
 *      1 - stage-in complete
 *     -1 - stage-in not started or burst buffer in some unexpected state
 */
extern int bb_p_job_test_stage_in(struct job_record *job_ptr, bool test_only)
{
	bb_alloc_t *bb_ptr;
	uint32_t bb_size = 0;
	int rc = 1;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}
	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    ((bb_size = _get_bb_size(job_ptr)) == 0))
		return rc;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		debug("%s: job_id:%u bb_rec not found",
		      __func__, job_ptr->job_id);
		rc = -1;
		if ((test_only == false) &&
		    (_test_size_limit(job_ptr, bb_size) == 0))
_start_stage_in(job_ptr->job_id);
//FIXME			_alloc_job_bb(job_ptr, bb_size);
	} else {
		if (bb_ptr->state < BB_STATE_STAGED_IN)
			_load_state(job_ptr->job_id);
		if (bb_ptr->state < BB_STATE_STAGED_IN) {
			rc = 0;
		} else if (bb_ptr->state == BB_STATE_STAGED_IN) {
			rc = 1;
		} else {
			error("%s: job_id:%u bb_state:%u",
			      __func__, job_ptr->job_id, bb_ptr->state);
			rc = -1;
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);
	return rc;
}

/*
 * Trigger a job's burst buffer stage-out to begin
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_start_stage_out(struct job_record *job_ptr)
{
//FIXME: How to handle various job terminate states (e.g. requeue, failure), user script controlled?
	bb_alloc_t *bb_ptr;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: job_id:%u bb_rec not found",
		      __func__, job_ptr->job_id);
	} else {
		_start_stage_out(job_ptr->job_id);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}

/*
 * Determine if a job's burst buffer stage-out is complete
 *
 * RET: 0 - stage-out is underway
 *      1 - stage-out complete
 *     -1 - fatal error
 */
extern int bb_p_job_test_stage_out(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;
	int rc = -1;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s: job_id:%u",
		     plugin_type, __func__, job_ptr->job_id);
	}
	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return 1;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	if (!bb_ptr) {
		/* No job buffers. Assuming use of persistent buffers only */
		debug("%s: job_id:%u bb_rec not found",
		      __func__, job_ptr->job_id);
		rc =  1;
	} else {
		if (bb_ptr->state < BB_STATE_STAGED_OUT)
			_load_state(job_ptr->job_id);
		if (bb_ptr->state == BB_STATE_STAGING_OUT) {
			rc =  0;
		} else if (bb_ptr->state == BB_STATE_STAGED_OUT) {
			if (bb_ptr->size != 0) {
				bb_remove_user_load(bb_ptr, &bb_state);
				bb_ptr->size = 0;
			}
			rc =  1;
		} else {
			error("%s: job_id:%u bb_state:%u",
			      __func__, job_ptr->job_id, bb_ptr->state);
			rc = -1;
		}
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return rc;
}

/*
 * Terminate any file staging and completely release burst buffer resources
 *
 * Returns a SLURM errno.
 */
extern int bb_p_job_cancel(struct job_record *job_ptr)
{
	bb_alloc_t *bb_ptr;

	if (bb_state.bb_config.debug_flag) {
		info("%s: %s",  __func__, plugin_type);
		info("%s: job_id:%u", __func__, job_ptr->job_id);
	}

	if ((job_ptr->burst_buffer == NULL) ||
	    (job_ptr->burst_buffer[0] == '\0') ||
	    (_get_bb_size(job_ptr) == 0))
		return SLURM_SUCCESS;

	pthread_mutex_lock(&bb_state.bb_mutex);
	bb_ptr = bb_find_job_rec(job_ptr, bb_state.bb_hash);
	_stop_stage_out(job_ptr->job_id);
	if (bb_ptr) {
		_stop_stage_out(job_ptr->job_id);
		bb_ptr->cancelled = true;
		bb_ptr->end_time = 0;
		bb_ptr->state = BB_STATE_STAGED_OUT;
		bb_ptr->state_time = time(NULL);
	}
	pthread_mutex_unlock(&bb_state.bb_mutex);

	return SLURM_SUCCESS;
}
