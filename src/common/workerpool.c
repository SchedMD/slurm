/*****************************************************************************\
 *  workerpool.c - worker pool
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

#include "src/common/events.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/probes.h"
#include "src/common/read_config.h"
#include "src/common/threadpool.h"
#include "src/common/workerpool.h"
#include "src/common/workq.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xsched.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

/* Limit automatically set default thread count */
#define THREAD_AUTO_MAX 32
/* Threads to create per kernel reported CPU */
#define CPU_THREAD_MULTIPLIER 2
#define CPU_THREAD_HIGH 2
#define CPU_THREAD_LOW 2

/* Count of workerpool_work_t* to preallocate */
#define ALLOC_WORK_COUNT 256

static struct {
	pthread_mutex_t mutex;

	/* Event when worker finishes */
	event_signal_t worker_fini;
	/* Event when worker initializes */
	event_signal_t worker_init;

	/* true if shutdown requested */
	bool shutdown;

	/* Number of active threads */
	int thread_count;

	/* Work queue shared by all threads */
	workq_t *workq;

	/* Allocator for workerpool_work_t* */
	workq_allocator_t *alloc;

	struct {
		int thread_count;
	} config;
} workerpool = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.worker_init = EVENT_INITIALIZER("WORKERPOOL-THREAD-INITIALIZE"),
	.worker_fini = EVENT_INITIALIZER("WORKERPOOL-THREAD-FINISH"),
	.shutdown = true,
	.config = {
		.thread_count = 0,
	},
};

static void _parse_params(const int default_count, const char *params)
{
	char *tmp_str = NULL, *tok = NULL, *saveptr = NULL;

	if (default_count > 0)
		workerpool.config.thread_count = default_count;

	if (!params)
		return;

	tmp_str = xstrdup(params);
	tok = strtok_r(tmp_str, ",", &saveptr);
	while (tok) {
		/*
		 * conmgr_threads= is treated as an alias as workerpool_threads=
		 */
		if (!xstrncasecmp(tok, CONMGR_PARAM_THREADS,
				  strlen(CONMGR_PARAM_THREADS))) {
			const unsigned long count =
				slurm_atoul(tok + strlen(CONMGR_PARAM_THREADS));

			if (count > WORKERPOOL_THREAD_COUNT_MAX)
				fatal("%s: invalid parameter %s",
				      __func__, tok);

			workerpool.config.thread_count = count;

			log_flag(THREAD, "%s: %lu threads", __func__, count);
		} else if (!xstrncasecmp(tok, WORKERPOOL_PARAM_THREADS,
					 strlen(WORKERPOOL_PARAM_THREADS))) {
			const unsigned long count =
				slurm_atoul(tok +
					    strlen(WORKERPOOL_PARAM_THREADS));

			if (count > WORKERPOOL_THREAD_COUNT_MAX)
				fatal("%s: invalid parameter %s",
				      __func__, tok);

			workerpool.config.thread_count = count;

			log_flag(THREAD, "%s: %lu threads", __func__, count);
		} else {
			log_flag(THREAD, "%s: workerpool ignoring parameter %s",
				 __func__, tok);
		}

		tok = strtok_r(NULL, ",", &saveptr);
	}

	xfree(tmp_str);
}

static int _detect_cpu_count(void)
{
	int count = get_assigned_cpu_count();

	log_flag(THREAD, "%s: detected %d CPUs available from kernel",
		 __func__, count);
	return count;
}

static int _resolve_thread_count(int count, const int default_count)
{
	const int detected_cpus = _detect_cpu_count();
	const int auto_threads_max = (detected_cpus * CPU_THREAD_MULTIPLIER);
	const int auto_threads = MIN(THREAD_AUTO_MAX, auto_threads_max);
	const int detected_threads_high = (detected_cpus * CPU_THREAD_HIGH);
	const int detected_threads_low = (detected_cpus / CPU_THREAD_LOW);
	const int warn_max_threads =
		MIN(WORKERPOOL_THREAD_COUNT_MAX, detected_threads_high);
	const int min_def_threads =
		MIN(THREAD_AUTO_MAX,
		    MAX(WORKERPOOL_THREAD_COUNT_MIN, default_count));
	const int warn_min_threads = MIN(detected_threads_low, min_def_threads);

	if (!count && (workerpool.config.thread_count > 0)) {
		count = workerpool.config.thread_count;
		log_flag(THREAD, "%s: Setting thread count to %s%d threads",
			 __func__, WORKERPOOL_PARAM_THREADS,
			 workerpool.config.thread_count);
	}

	if (!count) {
		if (default_count > 0) {
			count = default_count;
			log_flag(THREAD, "%s: Setting thread count to default %d threads",
				 __func__, default_count);
		} else {
			count = auto_threads;
			log_flag(THREAD, "%s: Setting thread count to %d/%d for %d available CPUs",
				 __func__, auto_threads, auto_threads_max,
				 detected_cpus);
		}
	} else if ((count > warn_max_threads) || (count < warn_min_threads)) {
		warning("%s%d is configured outside of the suggested range of [%d, %d] for %d CPUs. Performance will be negatively impacted, potentially causing difficult to debug hangs. Please keep within the suggested range or use the automatically detected thread count of %d threads.",
			WORKERPOOL_PARAM_THREADS, count, warn_min_threads,
			warn_max_threads, detected_cpus, auto_threads);
	}

	if (count < WORKERPOOL_THREAD_COUNT_MIN) {
		error("%s: %s%d too low, increasing to %d",
		      __func__, WORKERPOOL_PARAM_THREADS, count,
		      WORKERPOOL_THREAD_COUNT_MIN);
		count = WORKERPOOL_THREAD_COUNT_MIN;
	} else if (count > WORKERPOOL_THREAD_COUNT_MAX) {
		error("%s: %s%d too high, decreasing to %d",
		      __func__, WORKERPOOL_PARAM_THREADS, count,
		      WORKERPOOL_THREAD_COUNT_MAX);
		count = WORKERPOOL_THREAD_COUNT_MAX;
	}

	return count;
}

static void *_worker(void *arg)
{
	workq_t *workq = NULL;

	slurm_mutex_lock(&workerpool.mutex);

	workerpool.thread_count++;
	xassert(workerpool.thread_count > 0);

	workq = workerpool.workq;

	EVENT_BROADCAST(&workerpool.worker_init);

	slurm_mutex_unlock(&workerpool.mutex);

	workq_run(workq, true);

	slurm_mutex_lock(&workerpool.mutex);

	xassert(workq == workerpool.workq);

	workerpool.thread_count--;
	xassert(workerpool.thread_count >= 0);

	EVENT_BROADCAST(&workerpool.worker_fini);

	slurm_mutex_unlock(&workerpool.mutex);

	return NULL;
}

static void _probe_verbose(probe_log_t *log)
{
	probe_log(log, "state: shutdown=%c thread_count=%d",
		  BOOL_CHARIFY(workerpool.shutdown), workerpool.thread_count);
}

static probe_status_t _probe(probe_log_t *log, void *arg)
{
	probe_status_t status = PROBE_RC_UNKNOWN;

	slurm_mutex_lock(&workerpool.mutex);

	if (log)
		_probe_verbose(log);

	if (workerpool.shutdown)
		status = PROBE_RC_ONLINE;
	else
		status = PROBE_RC_READY;

	slurm_mutex_unlock(&workerpool.mutex);

	return status;
}

extern void workerpool_init(const int thread_count,
			    const int default_thread_count, const char *params)
{
	int count = -1;

	slurm_mutex_lock(&workerpool.mutex);

	if (!workerpool.shutdown) {
		slurm_mutex_unlock(&workerpool.mutex);
		return;
	}

	xassert(workerpool.thread_count <= 0);

	if (!workerpool.workq)
		probe_register("workerpool", _probe, NULL);

	_parse_params(default_thread_count, params);
	workerpool.shutdown = false;
	count = _resolve_thread_count(thread_count, default_thread_count);
	workerpool.workq = workq_init(workerpool.workq, "workerpool");
	workerpool.alloc = workq_allocator(workerpool.workq, ALLOC_WORK_COUNT,
					   "workerpool");

	for (int i = 0; i < count; i++)
		slurm_thread_create_detached(NULL, _worker, NULL);

	/* Wait for all threads to start up */
	while (workerpool.thread_count < count) {
		log_flag(THREAD, "%s: waiting for workers %d/%d to initialize",
			 __func__, workerpool.thread_count, count);
		EVENT_WAIT(&workerpool.worker_init, &workerpool.mutex);
	}

	log_flag(THREAD, "%s: started %d worker threads", __func__, count);

	slurm_mutex_unlock(&workerpool.mutex);
}

extern void workerpool_fini(void)
{
	slurm_mutex_lock(&workerpool.mutex);

	if (workerpool.shutdown) {
		slurm_mutex_unlock(&workerpool.mutex);
		return;
	}

	log_flag(THREAD, "%s: shutting down %d worker threads",
		 __func__, workerpool.thread_count);

	workerpool.shutdown = true;

	/*
	 * workq_fini() tells the workq to shutdown and waits for every worker
	 * to leave workq_run(). The workers then reacquire workerpool.mutex
	 * (held here) and assert workq == workerpool.workq in _worker(), so
	 * workerpool.workq must stay valid until they have fully exited.
	 *
	 * The probe-named workq is intentionally left allocated by workq_fini()
	 * (the probe still references it), so keep the pointer here. A later
	 * workerpool_init() reuses it in place instead of leaking it and
	 * registering a duplicate "workerpool" probe.
	 */
	workq_fini(workerpool.workq);

	/* Wait for all threads to finish */
	while (workerpool.thread_count > 0) {
		log_flag(THREAD, "%s: waiting for workers %d to finish",
			 __func__, workerpool.thread_count);
		EVENT_WAIT(&workerpool.worker_fini, &workerpool.mutex);
	}

	slurm_mutex_unlock(&workerpool.mutex);
}

extern void workerpool_enqueue(workq_allocator_t *alloc,
			       workq_priority_t priority, work_func_t func,
			       const char *func_name, void *arg,
			       const char *caller)
{
	slurm_mutex_lock(&workerpool.mutex);

	xassert(!workerpool.shutdown);

	if (!alloc)
		alloc = workerpool.alloc;

	workq_enqueue(workerpool.workq, alloc, priority, func, func_name,
		      caller, arg);

	slurm_mutex_unlock(&workerpool.mutex);
}
