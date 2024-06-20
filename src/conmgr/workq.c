/*****************************************************************************\
 *  workq.c - definitions for work queue manager
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

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/workq.h"
#include "src/conmgr/mgr.h"

static void *_worker(void *arg);

static void _check_magic_workq(void)
{
	xassert(mgr.workq.workers);
	xassert(mgr.workq.active >= 0);
}

static void _check_magic_worker(workq_worker_t *worker)
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
	workq_worker_t *worker = x;

	if (!worker)
		return;

	_check_magic_worker(worker);

	log_flag(CONMGR, "%s: [%u] free worker", __func__, worker->id);

	worker->magic = ~MAGIC_WORKER;
	xfree(worker);
}

static void _worker_delete(void *x)
{
	workq_worker_t *worker = x;

	if (!worker)
		return;

	_check_magic_worker(worker);

	slurm_mutex_lock(&mgr.mutex);
	worker = list_remove_first(mgr.workq.workers, _find_worker, worker);

	mgr.workq.total--;

	/* workq may get freed at any time after unlocking */
	slurm_mutex_unlock(&mgr.mutex);
	xassert(worker == x);

	_worker_free(worker);
}

static void _increase_thread_count(int count)
{
	for (int i = 0; i < count; i++) {
		workq_worker_t *worker = xmalloc(sizeof(*worker));
		worker->magic = MAGIC_WORKER;
		worker->id = i + 1;

		slurm_thread_create(&worker->tid, _worker, worker);
		_check_magic_worker(worker);

		list_append(mgr.workq.workers, worker);
	}
}

extern void workq_init(int count)
{
	if (!count) {
		count = CONMGR_THREAD_COUNT_DEFAULT;
	} else if ((count < CONMGR_THREAD_COUNT_MIN) ||
		   (count > CONMGR_THREAD_COUNT_MAX)) {
		fatal("%s: Invalid thread count=%d; thread count must be between %d and %d",
		      __func__, count, CONMGR_THREAD_COUNT_MIN,
		      CONMGR_THREAD_COUNT_MAX);
	}

	if (mgr.workq.threads) {
		_check_magic_workq();

		if (mgr.workq.threads >= count) {
			int threads = mgr.workq.threads;
			log_flag(CONMGR, "%s: ignoring duplicate init request with thread count=%d, current thread count=%d",
				 __func__, count, threads);
		} else {
			int prev = mgr.workq.threads;

			/* Need to increase thread count to match count */
			_increase_thread_count(count - mgr.workq.threads);
			mgr.workq.threads = count;

			log_flag(CONMGR, "%s: increased thread count from %d to %d",
				 __func__, prev, count);
		}
		return;
	}

	xassert(!mgr.workq.workers);
	mgr.workq.workers = list_create(_worker_free);
	mgr.workq.threads = count;

	_check_magic_workq();

	_increase_thread_count(count);
}

/* Note: caller must hold conmgr lock */
static void _wait_workers_idle(void)
{
	_check_magic_workq();
	log_flag(CONMGR, "%s: checking %u workers",
		 __func__, list_count(mgr.work));

	while (mgr.workq.active)
		slurm_cond_wait(&mgr.cond, &mgr.mutex);

	log_flag(CONMGR, "%s: all workers are idle", __func__);
}

static void _wait_work_complete()
{
	xassert(mgr.shutdown_requested);
	_check_magic_workq();
	log_flag(CONMGR, "%s: waiting for %u queued workers",
		 __func__, list_count(mgr.work));

	while (true) {
		int count;
		pthread_t tid;
		workq_worker_t *worker;

		if ((count = list_count(mgr.workq.workers)) == 0) {
			log_flag(CONMGR, "%s: all workers are done", __func__);
			break;
		}
		worker = list_peek(mgr.workq.workers);
		xassert(worker->magic == MAGIC_WORKER);
		tid = worker->tid;

		log_flag(CONMGR, "%s: waiting on %d workers", __func__, count);
		slurm_thread_join(tid);
	}
}

/*
 * Stop all work (eventually) and reject new requests
 * This will block until all work is complete.
 * Note: calling thread must hold conmgr lock
 */
static void _quiesce(void)
{
	_check_magic_workq();

	log_flag(CONMGR, "%s: shutting down with %u queued jobs",
		 __func__, list_count(mgr.work));

	/* notify workers of shutdown */
	xassert(mgr.shutdown_requested);
	slurm_cond_broadcast(&mgr.cond);

	_wait_work_complete();

	xassert(list_count(mgr.workq.workers) == 0);
	xassert(list_count(mgr.work) == 0);
}

extern void workq_fini(void)
{
	int threads;

	threads = mgr.workq.threads;

	if (!threads)
		return;

	_wait_workers_idle();
	_quiesce();

	xassert(!mgr.workq.active);
	xassert(!mgr.workq.total);
	xassert(mgr.shutdown_requested);

	FREE_NULL_LIST(mgr.workq.workers);

	mgr.workq.threads = 0;
}

static void *_worker(void *arg)
{
	workq_worker_t *worker = arg;
	_check_magic_worker(worker);

	slurm_mutex_lock(&mgr.mutex);
	mgr.workq.total++;
	slurm_mutex_unlock(&mgr.mutex);

	while (true) {
		work_t *work = NULL;

		slurm_mutex_lock(&mgr.mutex);

		work = list_pop(mgr.work);

		/* wait for work if nothing to do */
		if (!work) {
			if (mgr.shutdown_requested) {
				/* give up lock as we are about to be deleted */
				slurm_mutex_unlock(&mgr.mutex);

				log_flag(CONMGR, "%s: [%u] shutting down",
					 __func__, worker->id);
				_worker_delete(worker);
				break;
			}

			log_flag(CONMGR, "%s: [%u] waiting for work. Current active workers %u/%u",
				 __func__, worker->id, mgr.workq.active,
				 mgr.workq.total);
			slurm_cond_wait(&mgr.cond, &mgr.mutex);
			slurm_mutex_unlock(&mgr.mutex);
			continue;
		}

		xassert(work->magic == MAGIC_WORK);

		/* got work, run it! */
		mgr.workq.active++;

		log_flag(CONMGR, "%s: [%u->%s] running active_workers=%u/%u queue=%u",
			 __func__, worker->id, work->tag, mgr.workq.active,
			 mgr.workq.total, list_count(mgr.work));

		slurm_mutex_unlock(&mgr.mutex);

		/* run work via wrap_work() which will xfree(work) */
		wrap_work(work);
		work = NULL;

		slurm_mutex_lock(&mgr.mutex);

		mgr.workq.active--;

		log_flag(CONMGR, "%s: [%u] finished active_workers=%u/%u queue=%u",
			 __func__, worker->id, mgr.workq.active,
			 mgr.workq.total, list_count(mgr.work));

		slurm_cond_broadcast(&mgr.cond);
		slurm_mutex_unlock(&mgr.mutex);
	}

	return NULL;
}
