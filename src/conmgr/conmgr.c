/*****************************************************************************\
 *  conmgr.c - definitions for connection manager
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

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"
#include "src/conmgr/workq.h"

#define MAX_CONNECTIONS_DEFAULT 150

conmgr_t mgr = CONMGR_DEFAULT;

static void _atfork_child(void)
{
	/*
	 * Force conmgr to return to default state before it was initialized at
	 * forking as all of the prior state is completely unusable.
	 */
	mgr = CONMGR_DEFAULT;
}

extern void conmgr_init(int thread_count, int max_connections,
			conmgr_callbacks_t callbacks)
{
	if (max_connections < 1)
		max_connections = MAX_CONNECTIONS_DEFAULT;

	workq_init(thread_count);

	slurm_mutex_lock(&mgr.mutex);

	mgr.shutdown_requested = false;

	if (!mgr.at_fork_installed) {
		int rc;

		if ((rc = pthread_atfork(NULL, NULL, _atfork_child)))
			fatal_abort("%s: pthread_atfork() failed: %s",
				    __func__, slurm_strerror(rc));

		mgr.at_fork_installed = true;
	} else {
		/* already initialized */
		mgr.max_connections = MAX(max_connections, mgr.max_connections);

		/* Catch if callbacks are different while ignoring NULLS */
		xassert(!callbacks.parse || !mgr.callbacks.parse);
		xassert(!callbacks.free_parse || !mgr.callbacks.free_parse);

		if (callbacks.parse)
			mgr.callbacks.parse = callbacks.parse;
		if (callbacks.free_parse)
			mgr.callbacks.free_parse = callbacks.free_parse;

		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	mgr.max_connections = max_connections;
	mgr.connections = list_create(NULL);
	mgr.listen_conns = list_create(NULL);
	mgr.complete_conns = list_create(NULL);
	mgr.callbacks = callbacks;
	mgr.deferred_funcs = list_create(NULL);

	if (pipe(mgr.event_fd))
		fatal("%s: unable to open unnamed pipe: %m", __func__);

	fd_set_nonblocking(mgr.event_fd[0]);
	fd_set_blocking(mgr.event_fd[1]);

	if (pipe(mgr.signal_fd))
		fatal("%s: unable to open unnamed pipe: %m", __func__);

	/* block for writes only */
	fd_set_blocking(mgr.signal_fd[0]);
	fd_set_blocking(mgr.signal_fd[1]);

	add_signal_work(SIGALRM, on_signal_alarm, NULL,
			XSTRINGIFY(on_signal_alarm));

	mgr.initialized = true;
	slurm_mutex_unlock(&mgr.mutex);
}

extern void conmgr_fini(void)
{
	slurm_mutex_lock(&mgr.mutex);

	if (!mgr.initialized) {
		log_flag(CONMGR, "%s: Ignoring duplicate shutdown request",
			 __func__);
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	mgr.initialized = false;
	mgr.shutdown_requested = true;
	mgr.quiesced = false;

	/* run all deferred work if there is any */
	requeue_deferred_funcs();

	log_flag(CONMGR, "%s: connection manager shutting down", __func__);

	/* processing may still be running at this point in a thread */
	close_all_connections();

	/* tell all timers about being canceled */
	cancel_delayed_work();

	/* deferred_funcs should have been cleared by conmgr_run() */
	xassert(list_is_empty(mgr.deferred_funcs));
	FREE_NULL_LIST(mgr.deferred_funcs);

	/*
	 * At this point, there should be no threads running.
	 * It should be safe to shutdown the mgr.
	 */
	FREE_NULL_LIST(mgr.connections);
	FREE_NULL_LIST(mgr.listen_conns);
	FREE_NULL_LIST(mgr.complete_conns);

	free_delayed_work();

	if (((mgr.event_fd[0] >= 0) && close(mgr.event_fd[0])) ||
	    ((mgr.event_fd[1] >= 0) && close(mgr.event_fd[1])))
		error("%s: unable to close event_fd: %m", __func__);

	if (((mgr.signal_fd[0] >= 0) && close(mgr.signal_fd[0])) ||
	    ((mgr.signal_fd[1] >= 0) && close(mgr.signal_fd[1])))
		error("%s: unable to close signal_fd: %m", __func__);

	xfree(mgr.signal_work);

	slurm_mutex_unlock(&mgr.mutex);
	/*
	 * Do not destroy the mutex or cond so that this function does not
	 * crash when it tries to lock mgr.mutex if called more than once.
	 */
	/* slurm_mutex_destroy(&mgr.mutex); */
	/* slurm_cond_destroy(&mgr.cond); */

	workq_fini();
}

extern int conmgr_run(bool blocking)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&mgr.mutex);

	if (mgr.shutdown_requested) {
		log_flag(CONMGR, "%s: refusing to run when conmgr is shutdown",
			 __func__);

		rc = mgr.error;
		slurm_mutex_unlock(&mgr.mutex);
		return rc;
	}

	xassert(!mgr.error || !mgr.exit_on_error);
	mgr.quiesced = false;
	requeue_deferred_funcs();
	slurm_mutex_unlock(&mgr.mutex);

	if (blocking) {
		watch((void *) 1);
	} else {
		slurm_mutex_lock(&mgr.mutex);
		if (!mgr.watching)
			queue_func(true, watch, NULL, XSTRINGIFY(watch));
		slurm_mutex_unlock(&mgr.mutex);
	}

	slurm_mutex_lock(&mgr.mutex);
	rc = mgr.error;
	slurm_mutex_unlock(&mgr.mutex);

	return rc;
}

extern void conmgr_request_shutdown(void)
{
	log_flag(CONMGR, "%s: shutdown requested", __func__);

	slurm_mutex_lock(&mgr.mutex);
	mgr.shutdown_requested = true;
	signal_change(true);
	slurm_mutex_unlock(&mgr.mutex);
}

extern void conmgr_quiesce(bool wait)
{
	log_flag(CONMGR, "%s: quiesce requested", __func__);

	slurm_mutex_lock(&mgr.mutex);
	if (mgr.quiesced || mgr.shutdown_requested) {
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	mgr.quiesced = true;
	signal_change(true);

	if (wait)
		wait_for_watch();
	else
		slurm_mutex_unlock(&mgr.mutex);
}

extern void conmgr_set_exit_on_error(bool exit_on_error)
{
	slurm_mutex_lock(&mgr.mutex);
	mgr.exit_on_error = exit_on_error;
	slurm_mutex_unlock(&mgr.mutex);
}

extern bool conmgr_get_exit_on_error(void)
{
	bool exit_on_error;

	slurm_mutex_lock(&mgr.mutex);
	exit_on_error = mgr.exit_on_error;
	slurm_mutex_unlock(&mgr.mutex);

	return exit_on_error;
}

extern int conmgr_get_error(void)
{
	int rc;

	slurm_mutex_lock(&mgr.mutex);
	rc = mgr.error;
	slurm_mutex_unlock(&mgr.mutex);

	return rc;
}
