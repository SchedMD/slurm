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

#include "stdlib.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/delayed.h"
#include "src/conmgr/mgr.h"

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

	slurm_mutex_lock(&mgr.mutex);

	mgr.shutdown_requested = false;

	workers_init(thread_count);

	if (!mgr.one_time_initialized) {
		int rc;

		if ((rc = pthread_atfork(NULL, NULL, _atfork_child)))
			fatal_abort("%s: pthread_atfork() failed: %s",
				    __func__, slurm_strerror(rc));

		add_work(true, NULL, (conmgr_callback_t) {
				.func = on_signal_alarm,
				.func_name = XSTRINGIFY(on_signal_alarm),
			 }, (conmgr_work_control_t) {
				.depend_type = CONMGR_WORK_DEP_SIGNAL,
				.on_signal_number = SIGALRM,
				.schedule_type = CONMGR_WORK_SCHED_FIFO,
			 }, 0, __func__);

		mgr.one_time_initialized = true;
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
	mgr.work = list_create(NULL);
	mgr.quiesced_work = list_create(NULL);
	init_delayed_work();

	pollctl_init(mgr.max_connections);

	mgr.initialized = true;
	slurm_mutex_unlock(&mgr.mutex);

	/* Hook into atexit() in always clean shutdown if exit() called */
	(void) atexit(conmgr_request_shutdown);
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

	mgr.shutdown_requested = true;

	if (mgr.watch_thread) {
		slurm_mutex_unlock(&mgr.mutex);
		wait_for_watch();
		slurm_mutex_lock(&mgr.mutex);
	}

	mgr.initialized = false;

	log_flag(CONMGR, "%s: connection manager shutting down", __func__);

	/* processing may still be running at this point in a thread */
	close_all_connections();

	/* tell all timers about being canceled */
	cancel_delayed_work();

	/* wait until all workers are done */
	workers_shutdown();

	/*
	 * At this point, there should be no threads running.
	 * It should be safe to shutdown the mgr.
	 */
	FREE_NULL_LIST(mgr.connections);
	FREE_NULL_LIST(mgr.listen_conns);
	FREE_NULL_LIST(mgr.complete_conns);

	free_delayed_work();

	workers_fini();

	xassert(!mgr.quiesced);
	xassert(list_is_empty(mgr.quiesced_work));
	FREE_NULL_LIST(mgr.quiesced_work);

	/* work should have been cleared by workers_fini() */
	xassert(list_is_empty(mgr.work));
	FREE_NULL_LIST(mgr.work);

	pollctl_fini();

	/*
	 * Do not destroy the mutex or cond so that this function does not
	 * crash when it tries to lock mgr.mutex if called more than once.
	 */
	/* slurm_mutex_destroy(&mgr.mutex); */

	slurm_mutex_unlock(&mgr.mutex);
}

extern int conmgr_run(bool blocking)
{
	int rc = SLURM_SUCCESS;
	bool running = false;

	slurm_mutex_lock(&mgr.mutex);

	if (mgr.shutdown_requested) {
		log_flag(CONMGR, "%s: refusing to run when conmgr is shutdown",
			 __func__);

		rc = mgr.error;
		slurm_mutex_unlock(&mgr.mutex);
		return rc;
	}

	xassert(!mgr.error || !mgr.exit_on_error);

	if (mgr.watch_thread)
		running = true;
	else if (!blocking)
		slurm_thread_create(&mgr.watch_thread, watch, NULL);
	else
		mgr.watch_thread = pthread_self();

	slurm_mutex_unlock(&mgr.mutex);

	if (blocking) {
		if (running)
			wait_for_watch();
		else
			(void) watch(NULL);
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
	if (mgr.initialized) {
		mgr.shutdown_requested = true;
		EVENT_SIGNAL(&mgr.watch_sleep);
	}
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

extern bool conmgr_enabled(void)
{
	static bool init = false;
	static bool enabled = false;

	if (init)
		return enabled;

	slurm_mutex_lock(&mgr.mutex);
	enabled = (mgr.one_time_initialized || mgr.initialized);
	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: enabled=%c", __func__, (enabled ? 'T' : 'F'));

	init = true;
	return enabled;
}
