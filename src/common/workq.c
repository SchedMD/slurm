/*****************************************************************************\
 *  workq.c - definitions for work queue manager
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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
#include "src/common/workq.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
	/* workq that controls this worker */
	workq_t *workq;
	/* unique id for tracking */
	int id;
} workq_worker_t;

#define MAGIC_WORKQ 0xD23424EF
#define MAGIC_WORKER 0xD2342412
#define MAGIC_WORK 0xD23AB412

static void *_worker(void *arg);

static inline void _check_magic_workq(workq_t *workq)
{
	xassert(workq);
	xassert(workq->magic == MAGIC_WORKQ);
	xassert(workq->workers);
}

static inline void _check_magic_worker(workq_worker_t *worker)
{
	xassert(worker);
	xassert(worker->magic == MAGIC_WORKER);
	xassert(worker->id > 0);
	_check_magic_workq(worker->workq);
}

static inline void _check_magic_work(workq_work_t *work)
{
	xassert(work);
	xassert(work->magic == MAGIC_WORK);
	xassert(work->func);
}

static int _find_worker(void *x, void *arg)
{
	return (x == arg);
}

static void _worker_delete(void *x)
{
	workq_worker_t *worker = x;

	if (!worker)
		return;

	_check_magic_worker(worker);

	slurm_mutex_lock(&worker->workq->mutex);
	worker = list_remove_first(worker->workq->workers, _find_worker,
				   worker);

	worker->workq->total--;

	/* workq may get freed at any time after unlocking */
	slurm_mutex_unlock(&worker->workq->mutex);
	xassert(worker == x);

	log_flag(WORKQ, "%s: [%u] free worker", __func__, worker->id);

	worker->magic = ~MAGIC_WORKER;
	xfree(worker);
}

static void _work_delete(void *x)
{
	workq_work_t *work = x;

	if (!work)
		return;

	_check_magic_work(work);

	log_flag(WORKQ, "%s: free work", __func__);

	work->magic = ~MAGIC_WORK;
	xfree(work);
}

extern workq_t *new_workq(int count)
{
	workq_t *workq = xmalloc(sizeof(*workq));

	xassert(count < 1024);

	workq->magic = MAGIC_WORKQ;
	workq->workers = list_create(NULL);
	workq->work = list_create(_work_delete);

	slurm_mutex_init(&workq->mutex);
	slurm_cond_init(&workq->cond, NULL);

	_check_magic_workq(workq);

	for (int i = 0; i < count; i++) {
		workq_worker_t *worker = xmalloc(sizeof(*worker));
		worker->magic = MAGIC_WORKER;
		worker->workq = workq;
		worker->id = i + 1;

		slurm_thread_create(&worker->tid, _worker, worker);
		_check_magic_worker(worker);

		list_append(workq->workers, worker);
	}

	return workq;
}

extern void quiesce_workq(workq_t *workq)
{
	if (!workq)
		return;

	_check_magic_workq(workq);

	slurm_mutex_lock(&workq->mutex);

	log_flag(WORKQ, "%s: shutting down with %u queued jobs",
		 __func__, list_count(workq->work));

	/* notify of shutdown */
	workq->shutdown = true;
	slurm_cond_broadcast(&workq->cond);
	slurm_mutex_unlock(&workq->mutex);

	while (true) {
		int count;
		pthread_t tid;
		workq_worker_t *worker;

		slurm_mutex_lock(&workq->mutex);
		if ((count = list_count(workq->workers)) == 0) {
			slurm_mutex_unlock(&workq->mutex);
			log_flag(WORKQ, "%s: all workers are done", __func__);
			break;
		}
		worker = list_peek(workq->workers);
		tid = worker->tid;
		slurm_mutex_unlock(&workq->mutex);

		log_flag(WORKQ, "%s: waiting on %d workers", __func__, count);
		pthread_join(tid, NULL);
	}

	xassert(list_count(workq->workers) == 0);
	xassert(list_count(workq->work) == 0);
}

extern void free_workq(workq_t *workq)
{
	if (!workq)
		return;

	_check_magic_workq(workq);

	quiesce_workq(workq);

	FREE_NULL_LIST(workq->workers);
	FREE_NULL_LIST(workq->work);
	workq->magic = ~MAGIC_WORKQ;
	xfree(workq);
}

extern int workq_add_work(workq_t *workq, work_func_t func, void *arg,
			  const char *tag)
{
	int rc = SLURM_SUCCESS;

	workq_work_t *work = xmalloc(sizeof(*work));
	_check_magic_workq(workq);

	work->magic = MAGIC_WORK;
	work->func = func;
	work->arg = arg;
	work->tag = tag;

	_check_magic_work(work);

	slurm_mutex_lock(&workq->mutex);
	/* add to work list and signal a thread */
	if (workq->shutdown)
		rc = ESLURM_DISABLED;
	else { /* workq is not shutdown */
		list_append(workq->work, work);
		slurm_cond_signal(&workq->cond);
	}
	slurm_mutex_unlock(&workq->mutex);

	if (rc)
		xfree(work);

	return rc;
}

static void *_worker(void *arg)
{
	workq_worker_t *worker = arg;
	workq_t *workq = worker->workq;
	_check_magic_worker(worker);

	slurm_mutex_lock(&workq->mutex);
	worker->workq->total++;
	slurm_mutex_unlock(&workq->mutex);

	while (true) {
		workq_work_t *work = NULL;
		slurm_mutex_lock(&workq->mutex);

		work = list_pop(workq->work);

		/* wait for work if nothing to do */
		if (!work) {
			if (workq->shutdown) {
				/* give up lock as we are about to be deleted */
				slurm_mutex_unlock(&workq->mutex);

				log_flag(WORKQ, "%s: [%u] shutting down",
					 __func__, worker->id);
				_worker_delete(worker);
				break;
			}

			log_flag(WORKQ, "%s: [%u] waiting for work. Current active workers %u/%u",
				 __func__, worker->id, worker->workq->active,
				 worker->workq->total);
			slurm_cond_wait(&workq->cond, &workq->mutex);
			slurm_mutex_unlock(&workq->mutex);
			continue;
		}

		/* got work, run it! */
		worker->workq->active++;

		log_flag(WORKQ, "%s: [%u->%s] running active_workers=%u/%u queue=%u",
			 __func__, worker->id, work->tag,
			 worker->workq->active, worker->workq->total,
			 list_count(workq->work));

		slurm_mutex_unlock(&workq->mutex);

		/* run work now */
		_check_magic_work(work);
		work->func(work->arg);

		slurm_mutex_lock(&workq->mutex);
		workq->active--;

		log_flag(WORKQ, "%s: [%u->%s] finished active_workers=%u/%u queue=%u",
			 __func__, worker->id, work->tag,
			 worker->workq->active, worker->workq->total,
			 list_count(workq->work));
		slurm_mutex_unlock(&workq->mutex);

		_work_delete(work);
	}

	return NULL;
}

extern int workq_get_active(workq_t *workq)
{
	int active;

	_check_magic_workq(workq);

	slurm_mutex_lock(&workq->mutex);
	active = workq->active;
	slurm_mutex_unlock(&workq->mutex);

	return active;
}
