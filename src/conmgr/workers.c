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
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/events.h"
#include "src/conmgr/mgr.h"

/*
 * From man prctl:
 *	If the length of the  string, including the terminating null byte,
 *	exceeds 16 bytes, the string is silently truncated.
 */
#define PRCTL_BUF_BYTES 17

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

static void _increase_thread_count(int count)
{
	for (int i = 0; i < count; i++) {
		worker_t *worker = xmalloc(sizeof(*worker));
		worker->magic = MAGIC_WORKER;
		worker->id = i + 1;

		slurm_thread_create(&worker->tid, _worker, worker);
		_check_magic_worker(worker);

		list_append(mgr.workers.workers, worker);
	}
}

extern void workers_init(int count)
{
	if (!count) {
		count = CONMGR_THREAD_COUNT_DEFAULT;
	} else if ((count < CONMGR_THREAD_COUNT_MIN) ||
		   (count > CONMGR_THREAD_COUNT_MAX)) {
		fatal("%s: Invalid thread count=%d; thread count must be between %d and %d",
		      __func__, count, CONMGR_THREAD_COUNT_MIN,
		      CONMGR_THREAD_COUNT_MAX);
	}

	if (mgr.workers.threads) {
		_check_magic_workers();

		if (mgr.workers.threads >= count) {
			int threads = mgr.workers.threads;
			log_flag(CONMGR, "%s: ignoring duplicate init request with thread count=%d, current thread count=%d",
				 __func__, count, threads);
		} else {
			int prev = mgr.workers.threads;

			/* Need to increase thread count to match count */
			_increase_thread_count(count - mgr.workers.threads);
			mgr.workers.threads = count;

			log_flag(CONMGR, "%s: increased thread count from %d to %d",
				 __func__, prev, count);
		}
		return;
	}

	log_flag(CONMGR, "%s: Initializing with %d workers", __func__, count);
	xassert(!mgr.workers.workers);
	mgr.workers.workers = list_create(_worker_free);
	mgr.workers.threads = count;

	_check_magic_workers();

	_increase_thread_count(count);
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
			if (mgr.workers.shutdown_requested) {
				log_flag(CONMGR, "%s: [%u] shutting down",
					 __func__, worker->id);
				_worker_delete(worker);
				break;
			}

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

	EVENT_SIGNAL(&mgr.worker_return);
	slurm_mutex_unlock(&mgr.mutex);
	return NULL;
}

extern void wait_for_workers_idle(const char *caller)
{
	while (mgr.workers.active > 0) {
		log_flag(CONMGR, "%s->%s: waiting for workers=%u/%u",
			 caller, __func__, mgr.workers.active,
			 mgr.workers.total);

		EVENT_WAIT(&mgr.worker_return, &mgr.mutex);
	}
}

extern void workers_shutdown(void)
{
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
