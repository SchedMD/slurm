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

#include "config.h"

#include <pthread.h>

#include "slurm/slurm.h"

#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/workq.h"

#define WORKQ_DEFAULT                                \
	(struct workq_s) {                           \
		.mutex = PTHREAD_MUTEX_INITIALIZER,  \
		.cond = PTHREAD_COND_INITIALIZER,    \
		.shutdown = true,                    \
	}

struct workq_s {
	/* list of workq_worker_t */
	list_t *workers;
	/* list of workq_work_t */
	list_t *work;

	/* track simple stats for logging */
	int active;
	int total;

	/* manger is actively shutting down */
	bool shutdown;

	/* number of threads */
	int threads;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
} workq = WORKQ_DEFAULT;

typedef struct {
	int magic;
	work_func_t func;
	void *arg;
	/* tag for logging */
	const char *tag;
} workq_work_t;

typedef struct {
	int magic;
	/* thread id of worker */
	pthread_t tid;
	/* unique id for tracking */
	int id;
} workq_worker_t;

#define MAGIC_WORKQ 0xD23424EF
#define MAGIC_WORKER 0xD2342412
#define MAGIC_WORK 0xD23AB412

static void *_worker(void *arg);

static void _check_magic_workq(void)
{
	xassert(workq.workers);
	xassert(workq.active >= 0);
}

static void _check_magic_worker(workq_worker_t *worker)
{
	xassert(worker);
	xassert(worker->magic == MAGIC_WORKER);
	xassert(worker->id > 0);
}

static void _check_magic_work(workq_work_t *work)
{
	xassert(work);
	xassert(work->magic == MAGIC_WORK);
	xassert(work->func);
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

	slurm_mutex_lock(&workq.mutex);
	worker = list_remove_first(workq.workers, _find_worker, worker);

	workq.total--;

	/* workq may get freed at any time after unlocking */
	slurm_mutex_unlock(&workq.mutex);
	xassert(worker == x);

	_worker_free(worker);
}

static void _work_delete(void *x)
{
	workq_work_t *work = x;

	if (!work)
		return;

	_check_magic_work(work);

	log_flag(CONMGR, "%s: free work", __func__);

	work->magic = ~MAGIC_WORK;
	xfree(work);
}

static void _atfork_child(void)
{
	/*
	 * Force workq to return to default state before it was initialized at
	 * forking as all of the prior state is completely unusable.
	 */
	workq = WORKQ_DEFAULT;
}

static void _increase_thread_count(int count)
{
	for (int i = 0; i < count; i++) {
		workq_worker_t *worker = xmalloc(sizeof(*worker));
		worker->magic = MAGIC_WORKER;
		worker->id = i + 1;

		slurm_thread_create(&worker->tid, _worker, worker);
		_check_magic_worker(worker);

		list_append(workq.workers, worker);
	}
}

extern void workq_init(int count)
{
	static bool at_fork_installed = false;

	if (!count) {
		count = CONMGR_THREAD_COUNT_DEFAULT;
	} else if ((count < CONMGR_THREAD_COUNT_MIN) ||
		   (count > CONMGR_THREAD_COUNT_MAX)) {
		fatal("%s: Invalid thread count=%d; thread count must be between %d and %d",
		      __func__, count, CONMGR_THREAD_COUNT_MIN,
		      CONMGR_THREAD_COUNT_MAX);
	}

	slurm_mutex_lock(&workq.mutex);

	if (workq.threads) {
		_check_magic_workq();

		if (workq.threads >= count) {
			int threads = workq.threads;
			slurm_mutex_unlock(&workq.mutex);
			log_flag(CONMGR, "%s: ignoring duplicate init request with thread count=%d, current thread count=%d",
				 __func__, count, threads);
		} else {
			int prev = workq.threads;

			/* Need to increase thread count to match count */
			_increase_thread_count(count - workq.threads);
			workq.threads = count;
			slurm_mutex_unlock(&workq.mutex);

			log_flag(CONMGR, "%s: increased thread count from %d to %d",
				 __func__, prev, count);
		}
		return;
	}

	if (!at_fork_installed) {
		int rc;

		/*
		 * at_fork_installed == true here will only happen with the
		 * following sequence of calls:
		 *
		 * workq_init(), workq_fini(), workq_init().
		 *
		 * If this ever happens, avoid installing multiple atfork
		 * handlers.
		 */

		if ((rc = pthread_atfork(NULL, NULL, _atfork_child)))
			fatal_abort("%s: pthread_atfork() failed: %s",
				    __func__, slurm_strerror(rc));

		at_fork_installed = true;
	}

	xassert(!workq.workers);
	workq.workers = list_create(_worker_free);
	xassert(!workq.work);
	workq.work = list_create(_work_delete);
	workq.threads = count;

	_check_magic_workq();

	_increase_thread_count(count);

	workq.shutdown = false;

	slurm_mutex_unlock(&workq.mutex);
}

static void _wait_workers_idle(void)
{
	slurm_mutex_lock(&workq.mutex);
	_check_magic_workq();
	log_flag(CONMGR, "%s: checking %u workers",
		 __func__, list_count(workq.work));

	while (workq.active)
		slurm_cond_wait(&workq.cond, &workq.mutex);

	slurm_mutex_unlock(&workq.mutex);
	log_flag(CONMGR, "%s: all workers are idle", __func__);
}

static void _wait_work_complete(void)
{
	slurm_mutex_lock(&workq.mutex);
	xassert(workq.shutdown);
	_check_magic_workq();
	log_flag(CONMGR, "%s: waiting for %u queued workers",
		 __func__, list_count(workq.work));
	slurm_mutex_unlock(&workq.mutex);

	while (true) {
		int count;
		pthread_t tid;
		workq_worker_t *worker;

		slurm_mutex_lock(&workq.mutex);
		if ((count = list_count(workq.workers)) == 0) {
			slurm_mutex_unlock(&workq.mutex);
			log_flag(CONMGR, "%s: all workers are done", __func__);
			break;
		}
		worker = list_peek(workq.workers);
		xassert(worker->magic == MAGIC_WORKER);
		tid = worker->tid;
		slurm_mutex_unlock(&workq.mutex);

		log_flag(CONMGR, "%s: waiting on %d workers", __func__, count);
		slurm_thread_join(tid);
	}
}

extern void workq_quiesce(void)
{
	slurm_mutex_lock(&workq.mutex);
	_check_magic_workq();

	log_flag(CONMGR, "%s: shutting down with %u queued jobs",
		 __func__, list_count(workq.work));

	/* notify of shutdown */
	workq.shutdown = true;
	slurm_cond_broadcast(&workq.cond);
	slurm_mutex_unlock(&workq.mutex);

	_wait_work_complete();

	xassert(list_count(workq.workers) == 0);
	xassert(list_count(workq.work) == 0);
}

extern void workq_fini(void)
{
	int threads;

	slurm_mutex_lock(&workq.mutex);
	threads = workq.threads;
	slurm_mutex_unlock(&workq.mutex);

	if (!threads)
		return;

	_wait_workers_idle();
	workq_quiesce();

	slurm_mutex_lock(&workq.mutex);

	xassert(!workq.active);
	xassert(!workq.total);
	xassert(workq.shutdown);

	FREE_NULL_LIST(workq.workers);
	FREE_NULL_LIST(workq.work);

	workq.threads = 0;

	slurm_mutex_unlock(&workq.mutex);
}

extern int workq_add_work(work_func_t func, void *arg, const char *tag)
{
	int rc = SLURM_SUCCESS;

	workq_work_t *work = xmalloc(sizeof(*work));

	work->magic = MAGIC_WORK;
	work->func = func;
	work->arg = arg;
	work->tag = tag;

	_check_magic_work(work);

	slurm_mutex_lock(&workq.mutex);
	_check_magic_workq();
	/* add to work list and signal a thread */
	if (workq.shutdown)
		rc = ESLURM_DISABLED;
	else { /* workq is not shutdown */
		list_append(workq.work, work);
		slurm_cond_signal(&workq.cond);
	}
	slurm_mutex_unlock(&workq.mutex);

	if (rc)
		_work_delete(work);

	return rc;
}

static void *_worker(void *arg)
{
	workq_worker_t *worker = arg;
	_check_magic_worker(worker);

	slurm_mutex_lock(&workq.mutex);
	workq.total++;
	slurm_mutex_unlock(&workq.mutex);

	while (true) {
		workq_work_t *work = NULL;
		slurm_mutex_lock(&workq.mutex);

		work = list_pop(workq.work);

		/* wait for work if nothing to do */
		if (!work) {
			if (workq.shutdown) {
				/* give up lock as we are about to be deleted */
				slurm_mutex_unlock(&workq.mutex);

				log_flag(CONMGR, "%s: [%u] shutting down",
					 __func__, worker->id);
				_worker_delete(worker);
				break;
			}

			log_flag(CONMGR, "%s: [%u] waiting for work. Current active workers %u/%u",
				 __func__, worker->id, workq.active,
				 workq.total);
			slurm_cond_wait(&workq.cond, &workq.mutex);
			slurm_mutex_unlock(&workq.mutex);
			continue;
		}

		/* got work, run it! */
		workq.active++;

		log_flag(CONMGR, "%s: [%u->%s] running active_workers=%u/%u queue=%u",
			 __func__, worker->id, work->tag, workq.active,
			 workq.total, list_count(workq.work));

		slurm_mutex_unlock(&workq.mutex);

		/* run work now */
		_check_magic_work(work);
		work->func(work->arg);

		slurm_mutex_lock(&workq.mutex);

		workq.active--;

		log_flag(CONMGR, "%s: [%u->%s] finished active_workers=%u/%u queue=%u",
			 __func__, worker->id, work->tag, workq.active,
			 workq.total, list_count(workq.work));

		slurm_cond_broadcast(&workq.cond);
		slurm_mutex_unlock(&workq.mutex);

		_work_delete(work);
	}

	return NULL;
}

extern int workq_get_active(void)
{
	int active;

	slurm_mutex_lock(&workq.mutex);
	_check_magic_workq();
	active = workq.active;
	slurm_mutex_unlock(&workq.mutex);

	return active;
}
