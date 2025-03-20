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

#include <signal.h>
#include <stdlib.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/delayed.h"
#include "src/conmgr/mgr.h"
#include "src/conmgr/polling.h"

#define MAX_CONNECTIONS_DEFAULT 150

conmgr_t mgr = CONMGR_DEFAULT;

static sig_atomic_t enabled_init = 0;
static bool enabled_status = false;

static void _atfork_child(void)
{
	/*
	 * Force conmgr to return to default state before it was initialized at
	 * forking as all of the prior state is completely unusable.
	 */
	mgr = CONMGR_DEFAULT;
	enabled_init = 0;
	enabled_status = false;
}

static void _at_exit(void)
{
	/* Skip locking mgr.mutex to avoid a deadlock */
	mgr.shutdown_requested = true;
}

extern void conmgr_init(int thread_count, int max_connections,
			conmgr_callbacks_t callbacks)
{
	/* The configured value takes the highest precedence */
	if (mgr.conf_max_connections > 0)
		max_connections = mgr.conf_max_connections;
	else if (max_connections < 1)
		max_connections = MAX_CONNECTIONS_DEFAULT;
	xassert(max_connections > 0);

	slurm_mutex_lock(&mgr.mutex);

	enabled_status = true;
	mgr.shutdown_requested = false;

	if (mgr.workers.conf_threads > 0)
		thread_count = mgr.workers.conf_threads;
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

	if (!mgr.conf_delay_write_complete)
		mgr.conf_delay_write_complete = slurm_conf.msg_timeout;
	if (!mgr.conf_read_timeout.tv_nsec && !mgr.conf_read_timeout.tv_sec)
		mgr.conf_read_timeout.tv_sec = slurm_conf.msg_timeout;
	if (!mgr.conf_write_timeout.tv_nsec && !mgr.conf_write_timeout.tv_sec)
		mgr.conf_write_timeout.tv_sec = slurm_conf.msg_timeout;
	if (!mgr.conf_connect_timeout.tv_nsec &&
	    !mgr.conf_connect_timeout.tv_sec)
		mgr.conf_connect_timeout.tv_sec = slurm_conf.msg_timeout;

	mgr.max_connections = max_connections;
	mgr.connections = list_create(NULL);
	mgr.listen_conns = list_create(NULL);
	mgr.complete_conns = list_create(NULL);
	mgr.callbacks = callbacks;
	mgr.work = list_create(NULL);
	init_delayed_work();

	pollctl_init(mgr.max_connections);

	mgr.initialized = true;
	slurm_mutex_unlock(&mgr.mutex);

	/* Hook into atexit() in always clean shutdown if exit() called */
	(void) atexit(_at_exit);
}

extern void conmgr_fini(void)
{
	slurm_mutex_lock(&mgr.mutex);

	if (!mgr.initialized)
		fatal_abort("%s: duplicate shutdown request", __func__);

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

	xassert(!mgr.quiesce.requested);
	xassert(!mgr.quiesce.active);

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
		slurm_thread_create(&mgr.watch_thread, watch_thread, NULL);
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
	if (enabled_init)
		return enabled_status;

	slurm_mutex_lock(&mgr.mutex);
	enabled_status = (mgr.one_time_initialized || mgr.initialized);
	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: enabled=%c",
		 __func__, BOOL_CHARIFY(enabled_status));

	enabled_init = true;
	return enabled_status;
}

extern int conmgr_set_params(const char *params)
{
	char *tmp_str = NULL, *tok = NULL, *saveptr = NULL;

	slurm_mutex_lock(&mgr.mutex);
	/*
	 * This should be called before conmgr is initialized so that params
	 * are applied on initialization.
	 */
	xassert(!mgr.initialized);

	tmp_str = xstrdup(params);
	tok = strtok_r(tmp_str, ",", &saveptr);
	while (tok) {
		if (!xstrncasecmp(tok, CONMGR_PARAM_THREADS,
				  strlen(CONMGR_PARAM_THREADS))) {
			const unsigned long count =
				slurm_atoul(tok + strlen(CONMGR_PARAM_THREADS));

			mgr.workers.conf_threads = count;

			log_flag(CONMGR, "%s: %s set %lu threads",
				 __func__, tok, count);
		} else if (!xstrncasecmp(tok, CONMGR_PARAM_MAX_CONN,
				  strlen(CONMGR_PARAM_MAX_CONN))) {
			const unsigned long count =
				slurm_atoul(tok + strlen(CONMGR_PARAM_MAX_CONN));

			if (count < 1)
				fatal("%s: There must be atleast 1 max connection",
				      __func__);

			mgr.conf_max_connections = count;

			log_flag(CONMGR, "%s: %s activated with %lu max connections",
				 __func__, tok, count);
		} else if (!xstrcasecmp(tok, CONMGR_PARAM_POLL_ONLY)) {
			log_flag(CONMGR, "%s: %s activated", __func__, tok);
			pollctl_set_mode(POLL_MODE_POLL);
		} else if (!xstrcasecmp(tok, CONMGR_PARAM_WAIT_WRITE_DELAY)) {
			const unsigned long count = slurm_atoul(tok +
				strlen(CONMGR_PARAM_WAIT_WRITE_DELAY));
			log_flag(CONMGR, "%s: %s activated", __func__, tok);
			mgr.conf_delay_write_complete = count;
		} else if (!xstrcasecmp(tok, CONMGR_PARAM_READ_TIMEOUT)) {
			const unsigned long count = slurm_atoul(tok +
				strlen(CONMGR_PARAM_READ_TIMEOUT));
			log_flag(CONMGR, "%s: %s activated", __func__, tok);
			mgr.conf_read_timeout.tv_sec = count;
		} else if (!xstrcasecmp(tok, CONMGR_PARAM_WRITE_TIMEOUT)) {
			const unsigned long count = slurm_atoul(tok +
				strlen(CONMGR_PARAM_WRITE_TIMEOUT));
			log_flag(CONMGR, "%s: %s activated", __func__, tok);
			mgr.conf_write_timeout.tv_sec = count;
		} else if (!xstrcasecmp(tok, CONMGR_PARAM_CONNECT_TIMEOUT)) {
			const unsigned long count = slurm_atoul(tok +
				strlen(CONMGR_PARAM_CONNECT_TIMEOUT));
			log_flag(CONMGR, "%s: %s activated", __func__, tok);
			mgr.conf_connect_timeout.tv_sec = count;
		} else {
			log_flag(CONMGR, "%s: Ignoring parameter %s",
				 __func__, tok);
		}

		tok = strtok_r(NULL, ",", &saveptr);
	}

	slurm_mutex_unlock(&mgr.mutex);
	xfree(tmp_str);
	return SLURM_SUCCESS;
}

extern void conmgr_quiesce(const char *caller)
{
	slurm_mutex_lock(&mgr.mutex);

	log_flag(CONMGR, "%s->%s: quiesce requested", caller, __func__);

	/* wait until other request has completed */
	while (mgr.quiesce.requested)
		EVENT_WAIT(&mgr.quiesce.on_stop_quiesced, &mgr.mutex);

	xassert(!mgr.quiesce.active);
	mgr.quiesce.requested = true;

	while (!mgr.quiesce.active) {
		EVENT_SIGNAL(&mgr.watch_sleep);
		EVENT_WAIT(&mgr.quiesce.on_start_quiesced, &mgr.mutex);
	}

	slurm_mutex_unlock(&mgr.mutex);
}

extern void conmgr_unquiesce(const char *caller)
{
	slurm_mutex_lock(&mgr.mutex);

	xassert(mgr.quiesce.requested);
	xassert(mgr.quiesce.active);

	mgr.quiesce.requested = false;
	mgr.quiesce.active = false;

	EVENT_BROADCAST(&mgr.quiesce.on_stop_quiesced);

	/*
	 * If watch() never gets to an active quiesce then watch() may not be
	 * waiting on on_stop_quiesced event before conmgr_unquiesce() is
	 * called. Then watch() could still be waiting for a watch_sleep event
	 * and not a on_stop_quiesced event which could result it in never
	 * waking up.
	 */
	EVENT_SIGNAL(&mgr.watch_sleep);

	slurm_mutex_unlock(&mgr.mutex);
}
