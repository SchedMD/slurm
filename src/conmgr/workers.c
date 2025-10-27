/*****************************************************************************\
 *  workers.c - definitions for worker thread handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
#include <sys/prctl.h>
#endif

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xsched.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/events.h"
#include "src/conmgr/mgr.h"

/* Limit automatically set default thread count */
#define THREAD_AUTO_MAX 32
/* Threads to create per kernel reported CPU */
#define CPU_THREAD_MULTIPLIER 2
#define CPU_THREAD_HIGH 2
#define CPU_THREAD_LOW 2

/*
 * From man prctl:
 *	If the length of the  string, including the terminating null byte,
 *	exceeds 16 bytes, the string is silently truncated.
 */
#define PRCTL_BUF_BYTES 17

/*
 * Amount of time to sleep while polling for all threads to have started up
 * during shutdown
 */
#define SHUTDOWN_WAIT_STARTUP_THREADS_SLEEP_NS 10

static void *_worker(void *arg);

static void _check_magic_workers(void)
{
	xassert(mgr.workers.workers);
	xassert(mgr.workers.active >= 0);
}

static void _check_magic_worker(worker_t *worker)
{
	xassert(worker);
	xassert(worker->magic == MAGIC_WORKER);
	xassert(worker->id > 0);
}

static int _find_worker(void *x, void *arg)
{
	return (x == arg);
}
static void _worker_free(void *x)
{
	worker_t *worker = x;

	if (!worker)
		return;

	_check_magic_worker(worker);

	log_flag(CONMGR, "%s: [%u] free worker", __func__, worker->id);

	worker->magic = ~MAGIC_WORKER;
	xfree(worker);
}

/* caller must own mgr.mutex lock */
static void _worker_delete(void *x)
{
	worker_t *worker = x;

	if (!worker)
		return;

	/* list_delete_first calls _worker_free() */
	list_delete_first(mgr.workers.workers, _find_worker, worker);
	mgr.workers.total--;
}

static int _detect_cpu_count(void)
{
	cpu_set_t mask = { { 0 } };
	int rc = EINVAL, count = 0;

	if ((rc = slurm_getaffinity(getpid(), sizeof(mask), &mask))) {
		error("%s: Unable to query assigned CPU mask: %s",
		      __func__, slurm_strerror(rc));
		return 0;
	}

	if ((count = task_cpuset_get_assigned_count(sizeof(mask), &mask)) < 0)
		return 0;

	log_flag(CONMGR, "%s: detected %d CPUs available from kernel",
		 __func__, count);
	return count;
}

extern void workers_init(int count, int default_count)
{
	const int detected_cpus = _detect_cpu_count();
	const int auto_threads_max = (detected_cpus * CPU_THREAD_MULTIPLIER);
	const int auto_threads = MIN(THREAD_AUTO_MAX, auto_threads_max);
	const int detected_threads_high = (detected_cpus * CPU_THREAD_HIGH);
	const int detected_threads_low = (detected_cpus / CPU_THREAD_LOW);
	const int warn_max_threads =
		MIN(CONMGR_THREAD_COUNT_MAX, detected_threads_high);
	const int min_def_threads =
		MIN(THREAD_AUTO_MAX,
		    MAX(CONMGR_THREAD_COUNT_MIN, default_count));
	const int warn_min_threads = MIN(detected_threads_low, min_def_threads);

	if (!count && (mgr.workers.conf_threads > 0)) {
		count = mgr.workers.conf_threads;
		log_flag(CONMGR, "%s: Setting thread count to %s%d threads",
			 __func__, CONMGR_PARAM_THREADS,
			 mgr.workers.conf_threads);
	}

	if (!count) {
		if ((default_count > 0)) {
			count = default_count;
			log_flag(CONMGR, "%s: Setting thread count to default %d threads",
				 __func__, default_count);
		} else {
			count = auto_threads;
			log_flag(CONMGR, "%s: Setting thread count to %d/%d for %d available CPUs",
				 __func__, auto_threads, auto_threads_max,
				 detected_cpus);
		}
	} else if (((count > warn_max_threads) || (count < warn_min_threads))) {
		warning("%s%d is configured outside of the suggested range of [%d, %d] for %d CPUs. Performance will be negatively impacted, potentially causing difficult to debug hangs. Please keep within the suggested range or use the automatically detected thread count of %d threads.",
			CONMGR_PARAM_THREADS, count, warn_min_threads,
			warn_max_threads, detected_cpus, auto_threads);
	}

	if (count < CONMGR_THREAD_COUNT_MIN) {
		error("%s: %s%d too low, increasing to %d",
		      __func__, CONMGR_PARAM_THREADS, count,
		      CONMGR_THREAD_COUNT_MIN);
		count = CONMGR_THREAD_COUNT_MIN;
	} else if (count > CONMGR_THREAD_COUNT_MAX) {
		error("%s: %s%d too high, decreasing to %d",
		      __func__, CONMGR_PARAM_THREADS, count,
		      CONMGR_THREAD_COUNT_MAX);
		count = CONMGR_THREAD_COUNT_MAX;
	}

	log_flag(CONMGR, "%s: Initializing with %d workers", __func__, count);
	xassert(!mgr.workers.workers);
	mgr.workers.workers = list_create(_worker_free);
	mgr.workers.threads = count;

	_check_magic_workers();

	for (int i = 0; i < count; i++) {
		worker_t *worker = xmalloc(sizeof(*worker));
		worker->magic = MAGIC_WORKER;
		worker->id = i + 1;

		slurm_thread_create(&worker->tid, _worker, worker);
		_check_magic_worker(worker);

		list_append(mgr.workers.workers, worker);
	}
}

extern void workers_fini(void)
{
	/* all workers should have already exited by now */

	xassert(mgr.workers.shutdown_requested);
	xassert(!mgr.workers.active);
	xassert(!mgr.workers.total);

	FREE_NULL_LIST(mgr.workers.workers);

	mgr.workers.threads = 0;
}

static void *_worker(void *arg)
{
	worker_t *worker = arg;
	_check_magic_worker(worker);

#if HAVE_SYS_PRCTL_H
	{
		char title[PRCTL_BUF_BYTES];
		int id;

		slurm_mutex_lock(&mgr.mutex);
		id = worker->id;
		slurm_mutex_unlock(&mgr.mutex);

		snprintf(title, sizeof(title), "worker[%d]", id);

		if (prctl(PR_SET_NAME, title, NULL, NULL, NULL)) {
			error("%s: cannot set process name to %s %m",
			      __func__, title);
		}
	}
#endif


	slurm_mutex_lock(&mgr.mutex);
	mgr.workers.total++;
	/*
	 * mgr.mutex should be locked at the beginning of this loop. It should
	 * also be locked when exiting the loop. It may be unlocked and
	 * relocked during the loop.
	 */
	while (true) {
		work_t *work = NULL;

		while (mgr.quiesce.active)
			EVENT_WAIT(&mgr.quiesce.on_stop_quiesced, &mgr.mutex);

		work = list_pop(mgr.work);

		/* wait for work if nothing to do */
		if (!work) {
			if (mgr.workers.shutdown_requested)
				break;

			log_flag(CONMGR, "%s: [%u] waiting for work. Current active workers %u/%u",
				 __func__, worker->id, mgr.workers.active,
				 mgr.workers.total);
			EVENT_WAIT(&mgr.worker_sleep, &mgr.mutex);
			continue;
		}

		xassert(work->magic == MAGIC_WORK);

		if (mgr.shutdown_requested) {
			log_flag(CONMGR, "%s: [%u->%s] setting work status as cancelled after shutdown requested",
				 __func__, worker->id,
				 work->callback.func_name);
			work->status = CONMGR_WORK_STATUS_CANCELLED;
		}

		/* got work, run it! */
		mgr.workers.active++;

		log_flag(CONMGR, "%s: [%u] %s() running active_workers=%u/%u queue=%u",
			 __func__, worker->id, work->callback.func_name,
			 mgr.workers.active, mgr.workers.total,
			 list_count(mgr.work));

		/* Unlock mutex before running work */
		slurm_mutex_unlock(&mgr.mutex);

		/* run work via wrap_work() which will xfree(work) */
		wrap_work(work);
		work = NULL;

		/* Lock mutex after running work */
		slurm_mutex_lock(&mgr.mutex);

		mgr.workers.active--;

		log_flag(CONMGR, "%s: [%u] finished active_workers=%u/%u queue=%u",
			 __func__, worker->id, mgr.workers.active,
			 mgr.workers.total, list_count(mgr.work));

		/* wake up watch for all ending work on shutdown */
		if (mgr.shutdown_requested || mgr.waiting_on_work)
			EVENT_SIGNAL(&mgr.watch_sleep);
	}

	log_flag(CONMGR, "%s: [%u] shutting down",
		 __func__, worker->id);
	_worker_delete(worker);
	EVENT_SIGNAL(&mgr.worker_return);
	slurm_mutex_unlock(&mgr.mutex);
	return NULL;
}

extern void workers_shutdown(void)
{
	/*
	 * Wait until all threads have started up fully to avoid a thread
	 * starting after shutdown and hanging forever
	 */
	while (mgr.workers.threads &&
	       (mgr.workers.threads != mgr.workers.total)) {
		EVENT_BROADCAST(&mgr.worker_sleep);
		slurm_mutex_unlock(&mgr.mutex);
		(void) slurm_nanosleep(0,
				       SHUTDOWN_WAIT_STARTUP_THREADS_SLEEP_NS);
		slurm_mutex_lock(&mgr.mutex);
	}

	mgr.workers.shutdown_requested = true;

	do {
		log_flag(CONMGR, "%s: waiting for work=%u workers=%u/%u",
			 __func__, list_count(mgr.work), mgr.workers.active,
			 mgr.workers.total);

		if (mgr.workers.total > 0) {
			EVENT_BROADCAST(&mgr.worker_sleep);
			EVENT_WAIT(&mgr.worker_return, &mgr.mutex);
		}
	} while (mgr.workers.total);
}

extern void conmgr_log_workers(void)
{
	info("workers: threads:%d/%d active:%d/%d shutdown_requested:%c",
	     list_count(mgr.workers.workers), mgr.workers.threads,
	     mgr.workers.active, mgr.workers.total,
	     BOOL_CHARIFY(mgr.workers.shutdown_requested));
}
