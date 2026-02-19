/*****************************************************************************\
 *  workq.c - work queue
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <stdint.h>

#include "src/common/events.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/probes.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"
#include "src/common/workq.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#define WORKQ_MAX_ALLOCATORS 10

#define WORK_MAGIC 0xA241444F

typedef struct workq_work_s workq_work_t;

typedef struct workq_work_s {
	int magic; /* WORK_MAGIC */

	enum {
		WORKQ_WORK_STATE_INVALID = 0,
		WORKQ_WORK_STATE_FREE,
		WORKQ_WORK_STATE_ASSIGNED,
		WORKQ_WORK_STATE_RUNNING,
		WORKQ_WORK_STATE_INVALID_MAX
	} state;

	work_func_t func;
	void *arg;
	const char *func_name;
	timespec_t ts_submitted;
	/* Owning allocator or NULL (if directly xmalloc()ed) */
	workq_allocator_t *alloc;
	workq_work_t *next;
} workq_work_t;

typedef struct {
	int count;
	workq_work_t *begin;
	workq_work_t *end;
} workq_queue_t;

#define ALLOC_MAGIC 0xCC414DDF

typedef struct workq_allocator_s {
	int magic; /* ALLOC_MAGIC */
	pthread_mutex_t mutex;

	const char *name;

	/* Requested count of work to preallocate */
	int count;

	/* Queue of unassigned work */
	workq_queue_t queue;

	struct {
		size_t hit;
		size_t miss;
	} stats;
} workq_allocator_t;

#define WORKQ_MAGIC 0xB2414440

typedef struct workq_s {
	int magic; /* WORKQ_MAGIC */
	pthread_mutex_t mutex;

	/* True if workq is shutting down */
	bool shutdown;
	/* True to xfree workq pointer */
	bool release;
	/* Probe name or NULL */
	const char *probe_name;

	/* Assigned allocators */
	workq_allocator_t *allocs[WORKQ_MAX_ALLOCATORS];

	/* Priority queues */
	workq_queue_t queues[WORKQ_PRIORITY_INVALID_MAX - 1];

	/* Number of active threads running work */
	int workers;

	/* Latency from submission to starting func() */
	latency_histogram_t start_delay_histogram;
	/* Latency for func() run */
	latency_histogram_t runtime_histogram;

	event_signal_t run_sleep;
	event_signal_t run_complete;
} workq_t;

#define T(x) [x] = { x, #x }

static const struct {
	const workq_priority_t priority;
	const char *str;
} priority_names[] = {
	T(WORKQ_PRIORITY_IDLE),
	T(WORKQ_PRIORITY_NORMAL),
};

#undef T

static workq_queue_t *_priority_to_queue(workq_t *workq,
					 const workq_priority_t priority)
{
	const int i = (priority - WORKQ_PRIORITY_IDLE);

	xassert(i >= 0);
	xassert(i < ARRAY_SIZE(workq->queues));

	return &workq->queues[i];
}

static const char *_priority_str(const workq_priority_t priority)
{
	xassert(priority > WORKQ_PRIORITY_INVALID);
	xassert(priority < WORKQ_PRIORITY_INVALID_MAX);

	return priority_names[priority].str;
}

extern const size_t workq_bytes(void)
{
	return sizeof(workq_t);
}

static int _workq_count(const workq_t *workq)
{
	int count = 0;

	for (int i = 0; i < ARRAY_SIZE(workq->queues); i++) {
		xassert(workq->queues[i].count >= 0);
		count += workq->queues[i].count;
	}

	xassert(count >= 0);
	return count;
}

static workq_work_t *_queue_pop(workq_queue_t *queue)
{
	workq_work_t *work = queue->begin;

	if (!work) {
		xassert(!queue->end);
		xassert(!queue->count);
		return NULL;
	}

	xassert(work->magic == WORK_MAGIC);
	xassert(work->state > WORKQ_WORK_STATE_INVALID);
	xassert(work->state < WORKQ_WORK_STATE_INVALID_MAX);

	if (work->next) {
		xassert(work->next->magic == WORK_MAGIC);
		xassert(queue->end != work);

		queue->begin = work->next;
		work->next = NULL;
	} else {
		/* List will be empty */
		xassert(queue->end == work);

		queue->begin = NULL;
		queue->end = NULL;
	}

	queue->count--;
	xassert(queue->count >= 0);
	return work;
}

static void _queue_push(workq_queue_t *queue, workq_work_t *work)
{
	xassert(work->magic == WORK_MAGIC);
	xassert(work->state > WORKQ_WORK_STATE_INVALID);
	xassert(work->state < WORKQ_WORK_STATE_INVALID_MAX);
	xassert(!work->next);

	if (!queue->end) {
		xassert(!queue->begin);
		xassert(!queue->count);

		queue->begin = work;
		queue->end = work;
	} else {
		workq_work_t *last = queue->end;

		xassert(last->magic == WORK_MAGIC);
		xassert(last->state > WORKQ_WORK_STATE_INVALID);
		xassert(last->state < WORKQ_WORK_STATE_INVALID_MAX);
		xassert(!last->next);

		last->next = work;
		queue->end = work;
	}

	queue->count++;
	xassert(queue->count > 0);
}

static timespec_t _work_run(workq_t *workq, workq_work_t *work, const int id,
			    const bool shutdown)
{
	timespec_t ts_diff = { 0, 0 };
	timespec_t ts_started = timespec_now();

	xassert(workq->magic == WORKQ_MAGIC);
	xassert(work->magic == WORK_MAGIC);
	xassert(work->state == WORKQ_WORK_STATE_ASSIGNED);
	work->state = WORKQ_WORK_STATE_RUNNING;

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		char str[TIMESPEC_CTIME_STR_LEN] = "INVALID";

		(void) timespec_ctime(timespec_diff_ns(ts_started,
						       work->ts_submitted)
					      .diff,
				      false, str, sizeof(str));

		log_flag(THREAD, "%s: [workq@%p+%d] BEGIN: %s %s()@%p after %s",
			 __func__, workq, id,
			 (shutdown ? "shutting down" : "running"),
			 work->func_name, work, str);
	}

	work->func(shutdown, work->arg);
	ts_diff = timespec_diff_ns(timespec_now(), ts_started).diff;

	if (slurm_conf.debug_flags & DEBUG_FLAG_THREAD) {
		char str[TIMESPEC_CTIME_STR_LEN] = "INVALID";

		(void) timespec_ctime(ts_diff, false, str, sizeof(str));

		log_flag(THREAD, "%s: [workq@%p+%d] END: %s %s()@%p after %s",
			 __func__, workq, id,
			 (shutdown ? "shutting down" : "running"),
			 work->func_name, work, str);
	}

	xassert(workq->magic == WORKQ_MAGIC);
	xassert(work->magic == WORK_MAGIC);
	xassert(work->state == WORKQ_WORK_STATE_RUNNING);
	work->state = WORKQ_WORK_STATE_FREE;

	if (work->alloc) {
		slurm_mutex_lock(&work->alloc->mutex);

		xassert(work->alloc->magic == ALLOC_MAGIC);
		_queue_push(&work->alloc->queue, work);

		slurm_mutex_unlock(&work->alloc->mutex);
	} else {
		work->magic = ~WORK_MAGIC;
		xfree(work);
	}

	return ts_diff;
}

static bool _worker_run_work(workq_t *workq, const int id)
{
	workq_work_t *work = NULL;
	timespec_t ts_diff = { 0, 0 };
	const char shutdown = workq->shutdown;

	for (int i = (ARRAY_SIZE(workq->queues) - 1); !work && (i >= 0); i--)
		work = _queue_pop(&workq->queues[i]);

	if (!work)
		return false;

	HISTOGRAM_ADD_DURATION(&workq->start_delay_histogram,
			       work->ts_submitted);

	slurm_mutex_unlock(&workq->mutex);

	ts_diff = _work_run(workq, work, id, shutdown);

	slurm_mutex_lock(&workq->mutex);

	latency_metric_add_histogram_value(&workq->runtime_histogram, ts_diff);

	return true;
}

static bool _worker_run(workq_t *workq, const bool blocking, const int id)
{
	xassert(workq->magic == WORKQ_MAGIC);

	log_flag(THREAD, "%s: [workq@%p+%d] queued:%d",
		 __func__, workq, id, _workq_count(workq));

	while (_worker_run_work(workq, id))
		; /* do nothing */

	if (workq->shutdown || !blocking)
		return false;

	log_flag(THREAD, "%s: [workq@%p+%d] waiting for work",
		 __func__, workq, id);

	EVENT_WAIT(&workq->run_sleep, &workq->mutex);
	return true;
}

extern void workq_run(workq_t *workq, const bool blocking)
{
	int id = 0;

	slurm_mutex_lock(&workq->mutex);

	xassert(workq->magic == WORKQ_MAGIC);

	id = ++workq->workers;
	xassert(workq->workers > 0);

	while (_worker_run(workq, blocking, id))
		; /* do nothing */

	xassert(workq->workers > 0);

	log_flag(THREAD, "%s: [workq@%p+%d] stopped", __func__, workq, id);

	workq->workers--;
	xassert(workq->workers >= 0);

	EVENT_BROADCAST(&workq->run_complete);

	slurm_mutex_unlock(&workq->mutex);
}

static void _probe_verbose(probe_log_t *log, workq_t *workq)
{
	char histogram[LATENCY_METRIC_HISTOGRAM_STR_LEN] = { 0 };

	probe_log(log, "state: workers:%d shutdown:%c queued=%d",
		  workq->workers, BOOL_CHARIFY(workq->shutdown),
		  _workq_count(workq));

	(void) latency_histogram_print_labels(histogram, sizeof(histogram));
	probe_log(log, "histogram: %s", histogram);

	(void) latency_histogram_print(&workq->start_delay_histogram, histogram,
				       sizeof(histogram));
	probe_log(log, "start delay histogram: %s", histogram);

	(void) latency_histogram_print(&workq->runtime_histogram, histogram,
				       sizeof(histogram));
	probe_log(log, "runtime histogram: %s", histogram);

	for (int i = 0; i < ARRAY_SIZE(workq->queues); i++) {
		workq_queue_t *queue = &workq->queues[i];
		workq_priority_t priority = (i + WORKQ_PRIORITY_IDLE);
		int count = 0;

		probe_log(log, "%s queue: queued=%d",
			  _priority_str(priority), queue->count);

		for (workq_work_t *work = workq->queues[i].begin; work;
		     work = work->next) {
			xassert(work->magic == WORK_MAGIC);
			xassert(work->next || (workq->queues[i].end == work));

			count++;

			probe_log(log, "queue[%s]+%d: %s() allocator:%s",
				  _priority_str(priority), count,
				  work->func_name,
				  (work->alloc ? work->alloc->name : "N/A"));
		}
	}
}

static probe_status_t _probe(probe_log_t *log, void *arg)
{
	probe_status_t status = PROBE_RC_UNKNOWN;
	workq_t *workq = arg;

	slurm_mutex_lock(&workq->mutex);

	xassert(workq->magic == WORKQ_MAGIC);

	if (log)
		_probe_verbose(log, workq);

	if (workq->shutdown)
		status = PROBE_RC_ONLINE;
	else
		status = PROBE_RC_READY;

	slurm_mutex_unlock(&workq->mutex);

	return status;
}

extern workq_t *workq_init(workq_t *workq, const char *name)
{
	bool release = false;

	if (!workq) {
		workq = xmalloc(workq_bytes());
		release = true;
	}

	xassert(workq->magic != WORKQ_MAGIC);

	*workq = (workq_t) {
		.magic = WORKQ_MAGIC,
		.shutdown = false,
		.release = release,
		.probe_name = name,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.run_sleep = EVENT_INITIALIZER("WORKQ_RUN_SLEEP"),
		.run_complete = EVENT_INITIALIZER("WORKQ_RUN_COMPLETE"),
		.start_delay_histogram = LATENCY_HISTOGRAM_INITIALIZER,
		.runtime_histogram = LATENCY_HISTOGRAM_INITIALIZER,
	};

	if (name)
		probe_register(name, _probe, workq);

	return workq;
}

extern void workq_fini(workq_t *workq)
{
	DEF_TIMERS;
	bool release = false;

	xassert(workq);

	START_TIMER;

	slurm_mutex_lock(&workq->mutex);

	xassert(workq->magic == WORKQ_MAGIC);

	workq->shutdown = true;

	while (workq->workers) {
		xassert(workq->workers > 0);

		/* Wake up any sleeping workers to cleanup */
		EVENT_BROADCAST(&workq->run_sleep);

		log_flag(THREAD, "%s: [workq@%p] waiting on queued=%d workers=%d",
			 __func__, workq, _workq_count(workq),
			 workq->workers);

		EVENT_WAIT(&workq->run_complete, &workq->mutex);
	}

	xassert(workq->magic == WORKQ_MAGIC);
	xassert(workq->shutdown);
	xassert(!workq->workers);
	for (int i = 0; i < ARRAY_SIZE(workq->queues); i++) {
		xassert(!workq->queues[i].count);
		xassert(!workq->queues[i].begin);
		xassert(!workq->queues[i].end);
	}

	/*
	 * Free the allocators and their preallocated work. The probe only
	 * walks workq->queues[] (drained above), never workq->allocs[], so
	 * this is safe while the workq stays probe-registered.
	 */
	for (int i = 0; i < ARRAY_SIZE(workq->allocs); i++) {
		if (!workq->allocs[i])
			continue;

		xassert(workq->allocs[i]->magic == ALLOC_MAGIC);
		workq->allocs[i]->magic = ~ALLOC_MAGIC;
		xfree(workq->allocs[i]);
	}

	/* workq with a probe must never be xfree()ed */
	if (!workq->probe_name) {
		release = workq->release;

		EVENT_FREE_MEMBERS(&workq->run_sleep);
		EVENT_FREE_MEMBERS(&workq->run_complete);
		workq->magic = ~WORKQ_MAGIC;
	}

	slurm_mutex_unlock(&workq->mutex);

	END_TIMER2(__func__);

	log_flag(THREAD, "%s: [workq@%p] shutdown complete after %s",
		 __func__, workq, TIMER_STR());

	if (release)
		xfree(workq);
}

extern void workq_enqueue(workq_t *workq, workq_allocator_t *alloc,
			  const workq_priority_t priority, work_func_t func,
			  const char *func_name, const char *caller, void *arg)
{
	workq_work_t *work = NULL;

	xassert(priority > WORKQ_PRIORITY_INVALID);
	xassert(priority < WORKQ_PRIORITY_INVALID_MAX);
	xassert(workq->magic == WORKQ_MAGIC);
	xassert(!work || (work->magic != WORK_MAGIC));
	xassert(alloc);

	slurm_mutex_lock(&alloc->mutex);

	xassert(alloc->magic == ALLOC_MAGIC);

	if ((work = _queue_pop(&alloc->queue)))
		alloc->stats.hit++;
	else
		alloc->stats.miss++;

	slurm_mutex_unlock(&alloc->mutex);

	if (!work) {
		work = xmalloc(sizeof(*work));
	} else {
		xassert(work->magic == WORK_MAGIC);
		xassert(work->alloc == alloc);
		xassert(work->state == WORKQ_WORK_STATE_FREE);
	}

	work->magic = WORK_MAGIC;
	work->state = WORKQ_WORK_STATE_ASSIGNED;
	work->func = func;
	work->arg = arg;
	work->func_name = func_name;
	work->ts_submitted = timespec_now();

	slurm_mutex_lock(&workq->mutex);

	_queue_push(_priority_to_queue(workq, priority), work);

	/* Wake up 1 sleeping thread to run the work */
	EVENT_SIGNAL(&workq->run_sleep);

	log_flag(THREAD, "%s->%s: [workq@%p] SUBMIT: %s()@%p allocator:%s",
		 caller, __func__, workq, func_name, work,
		 (work->alloc ? work->alloc->name : "N/A"));

	slurm_mutex_unlock(&workq->mutex);
}

/* Caller must hold alloc->mutex lock */
static workq_work_t *_alloc_get(workq_allocator_t *alloc, const int index)
{
	void *ptr = (((void *) alloc) + sizeof(workq_allocator_t));
	workq_work_t *work = NULL;

	xassert(alloc->magic == ALLOC_MAGIC);

	xassert(index >= 0);
	if ((index >= alloc->count) || (index < 0))
		return NULL;

	work = (ptr + (index * sizeof(workq_work_t)));
	return work;
}

extern workq_allocator_t *workq_allocator(workq_t *workq, const int count,
					  const char *name)
{
	const size_t alloc_bytes =
		(sizeof(workq_allocator_t) + (count * sizeof(workq_work_t)));
	workq_allocator_t *alloc = xmalloc(alloc_bytes);

	xassert(count > 0);

	*alloc = (workq_allocator_t) {
		.magic = ALLOC_MAGIC,
		.mutex = PTHREAD_MUTEX_INITIALIZER,
		.name = name,
		.count = count,
	};

	for (int i = 0; i < count; i++) {
		workq_work_t *work = _alloc_get(alloc, i);

		*work = (workq_work_t) {
			.magic = WORK_MAGIC,
			.state = WORKQ_WORK_STATE_FREE,
			.alloc = alloc,
		};

		_queue_push(&alloc->queue, work);
	}

	slurm_mutex_lock(&workq->mutex);

	for (int i = 0; i <= ARRAY_SIZE(workq->allocs); i++) {
		if (i >= ARRAY_SIZE(workq->allocs))
			fatal_abort("too many allocators!");

		if (!workq->allocs[i]) {
			workq->allocs[i] = alloc;
			break;
		}
	}

	slurm_mutex_unlock(&workq->mutex);

	log_flag(THREAD, "%s: [%s] allocator created count=%d",
		 __func__, name, count);

	return alloc;
}
