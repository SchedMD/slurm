/*****************************************************************************\
 *  track_script.c - Track scripts running asynchronously
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC.
 *  Written by Felip Moll <felip@schedmd.com>
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

#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>

#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/track_script.h"

static List track_script_thd_list = NULL;
static pthread_mutex_t flush_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t flush_cond = PTHREAD_COND_INITIALIZER;
static int flush_cnt = 0;

typedef struct {
	uint32_t job_id;
	pid_t cpid;
	pthread_t tid;
	pthread_mutex_t timer_mutex;
	pthread_cond_t timer_cond;
} track_script_rec_t;

typedef struct {
	pthread_t tid;
	int status;
	bool rc;
} foreach_broadcast_rec_t;

static void _track_script_rec_destroy(void *arg)
{
	track_script_rec_t *r = (track_script_rec_t *)arg;
	debug3("destroying job %u script thread, tid %lu",
	       r->job_id, (unsigned long) r->tid);
	pthread_detach(r->tid);
	slurm_cond_destroy(&r->timer_cond);
	slurm_mutex_destroy(&r->timer_mutex);
	xfree(r);
}

static void _kill_script(track_script_rec_t *r)
{
	pid_t pid_to_kill;

	if (r->cpid <= 0)
		return;

	pid_to_kill = r->cpid;
	r->cpid = -1;
	/* The script could have spawned processes. Kill the process group. */
	killpg(pid_to_kill, SIGKILL);
}

/*
 * Kill the prolog process forked by the _run_[prolog|epilog] thread,
 * this will make the caller thread to finalize, so wait also for it to
 * avoid any zombies.
 */
static void *_track_script_rec_cleanup(void *arg)
{
	int rc = 1;
	struct timeval tvnow;
	struct timespec abs;
	track_script_rec_t *r = (track_script_rec_t *)arg;

	info("Script for jobid=%u found running, tid=%lu, force ending. Ignore errors about not finding this thread id after this.",
	      r->job_id, (unsigned long)r->tid);

	_kill_script(r);

	/* setup timer */
	gettimeofday(&tvnow, NULL);
	abs.tv_sec = tvnow.tv_sec + 5; /* We may want to tune this */
	abs.tv_nsec = tvnow.tv_usec * 1000;

	/*
	 * This wait covers the case where we try to kill an unkillable process.
	 * In such situation the pthread_join would cause us to wait here
	 * indefinitely. So we do a pthread_cancel after some time (5 sec)
	 * in the case the process isn't gone yet.
	 */
	if (r->cpid != 0) {
		slurm_mutex_lock(&r->timer_mutex);
		rc = pthread_cond_timedwait(&r->timer_cond, &r->timer_mutex,
					    &abs);
		slurm_mutex_unlock(&r->timer_mutex);
	}

	if (rc)
		pthread_cancel(r->tid);

	pthread_join(r->tid, NULL);

	slurm_mutex_lock(&flush_mutex);
	flush_cnt++;
	slurm_cond_signal(&flush_cond);
	slurm_mutex_unlock(&flush_mutex);

	return NULL;
}

static void _make_cleanup_thread(track_script_rec_t *r)
{
	slurm_thread_create_detached(NULL, _track_script_rec_cleanup, r);
}

static int _flush_job(void *r, void *x)
{
	track_script_rec_t *rec = (track_script_rec_t *) r;
	uint32_t job_id = *(uint32_t *) x;

	if (rec->job_id != job_id)
		return 0;

	debug("%s: killing running script for completed job %u, pid %u",
	      __func__, job_id, rec->cpid);

	_kill_script(r);

	/*
	 * From now on the launcher thread should detect the pid as
	 * dead and continue doing cleanup and removing itself from
	 * the running scripts list.
	 */

	return 0;
}

static int _match_tid(void *object, void *key)
{
	pthread_t tid0 = ((track_script_rec_t *)object)->tid;
	pthread_t tid1 = *(pthread_t *)key;

	return (tid0 == tid1);
}

static int _reset_cpid(void *object, void *key)
{
	if (!_match_tid(object, &((track_script_rec_t *)key)->tid))
		return 0;

	((track_script_rec_t *)object)->cpid =
		((track_script_rec_t *)key)->cpid;

	/* Exit for_each after we found the one we care about. */
	return -1;
}

extern void track_script_init(void)
{
	FREE_NULL_LIST(track_script_thd_list);
	track_script_thd_list = list_create(_track_script_rec_destroy);
}

extern void track_script_flush(void)
{
	int count;
	List tmp_list = list_create(_track_script_rec_destroy);

	/*
	 * Transfer list within mutex and work off of copy to prevent race
	 * condition of track_script_remove() removing track_script_rec_t while
	 * in cleanup thread.
	 */
	slurm_mutex_lock(&flush_mutex);

	list_transfer(tmp_list, track_script_thd_list);

	count = list_count(tmp_list);
	if (!count) {
		FREE_NULL_LIST(tmp_list);
		slurm_mutex_unlock(&flush_mutex);
		return;
	}

	flush_cnt = 0;
	/* kill all scripts we are tracking */
	(void) list_for_each(tmp_list,
			     (ListForF)_make_cleanup_thread, NULL);

	while (flush_cnt < count) {
		slurm_cond_wait(&flush_cond, &flush_mutex);
		debug("%s: got %d scripts out of %d flushed",
		      __func__, flush_cnt, count);
	}

	FREE_NULL_LIST(tmp_list);
	slurm_mutex_unlock(&flush_mutex);

}

extern void track_script_flush_job(uint32_t job_id)
{
	(void)list_for_each(track_script_thd_list,
			    (ListFindF) _flush_job,
			    &job_id);
}

extern void track_script_fini(void)
{
	FREE_NULL_LIST(track_script_thd_list);
}

extern void track_script_rec_add(uint32_t job_id, pid_t cpid, pthread_t tid)
{
	track_script_rec_t *track_script_rec =
		xmalloc(sizeof(track_script_rec_t));

	track_script_rec->job_id = job_id;
	track_script_rec->cpid = cpid;
	track_script_rec->tid = tid;
	slurm_mutex_init(&track_script_rec->timer_mutex);
	slurm_cond_init(&track_script_rec->timer_cond, NULL);
	list_append(track_script_thd_list, track_script_rec);
}

static int _script_broadcast(void *object, void *key)
{
	track_script_rec_t *track_script_rec = (track_script_rec_t *)object;
	foreach_broadcast_rec_t *tmp_rec = (foreach_broadcast_rec_t *)key;

	if (!_match_tid(object, &tmp_rec->tid))
		return 0;

	bool rc = false;

	/* I was killed by slurmtrack, bail out right now */
	slurm_mutex_lock(&track_script_rec->timer_mutex);
	if (WIFSIGNALED(tmp_rec->status) &&
	    (WTERMSIG(tmp_rec->status) == SIGKILL) &&
	    (track_script_rec->cpid == -1)) {
		slurm_cond_broadcast(&track_script_rec->timer_cond);
		rc = true;
	}
	slurm_mutex_unlock(&track_script_rec->timer_mutex);

	tmp_rec->rc = rc;

	/* Exit for_each after we found the one we care about. */
	return -1;
}

extern bool track_script_broadcast(pthread_t tid, int status)
{
	foreach_broadcast_rec_t tmp_rec;

	memset(&tmp_rec, 0, sizeof(tmp_rec));
	tmp_rec.tid = tid;
	tmp_rec.status = status;

	if (list_for_each(track_script_thd_list, _script_broadcast, &tmp_rec))
		return tmp_rec.rc;

	debug("%s: didn't find track_script for tid %lu",
	      __func__, (unsigned long) tid);
	return true;
}

/* Remove this job from the list of jobs currently running a script */
extern void track_script_remove(pthread_t tid)
{
	if (!list_delete_all(track_script_thd_list, _match_tid, &tid))
		error("%s: thread %lu not found",
		      __func__, (unsigned long) tid);
	else
		debug2("%s: thread running script from job removed",
		       __func__);
}

extern void track_script_reset_cpid(pthread_t tid, pid_t cpid)
{
	track_script_rec_t tmp_rec;
	tmp_rec.tid = tid;
	tmp_rec.cpid = cpid;

	(void)list_for_each(track_script_thd_list, _reset_cpid, &tmp_rec);
}
