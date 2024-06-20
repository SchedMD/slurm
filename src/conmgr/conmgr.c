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

#define _GNU_SOURCE

#include "config.h"

#include <limits.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/strlcpy.h"
#include "src/common/timers.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"
#include "src/conmgr/workq.h"

#define MAX_CONNECTIONS_DEFAULT 150

conmgr_t mgr = CONMGR_DEFAULT;

static struct {
	conmgr_work_status_t status;
	const char *string;
} statuses[] = {
	{ CONMGR_WORK_STATUS_INVALID, "INVALID" },
	{ CONMGR_WORK_STATUS_PENDING, "PENDING" },
	{ CONMGR_WORK_STATUS_RUN, "RUN" },
	{ CONMGR_WORK_STATUS_CANCELLED, "CANCELLED" },
};

static struct {
	conmgr_work_type_t type;
	const char *string;
} types[] = {
	{ CONMGR_WORK_TYPE_INVALID, "INVALID" },
	{ CONMGR_WORK_TYPE_CONNECTION_FIFO, "CONNECTION_FIFO" },
	{ CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO, "DELAY_CONNECTION_FIFO" },
	{ CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE,
	  "CONNECTION_WRITE_COMPLETE" },
	{ CONMGR_WORK_TYPE_FIFO, "FIFO" },
	{ CONMGR_WORK_TYPE_TIME_DELAY_FIFO, "TIME_DELAY_FIFO" },
};

static void _requeue_deferred_funcs(void);

extern const char *conmgr_work_status_string(conmgr_work_status_t status)
{
	for (int i = 0; i < ARRAY_SIZE(statuses); i++)
		if (statuses[i].status == status)
			return statuses[i].string;

	fatal_abort("%s: invalid work status 0x%x", __func__, status);
}

extern const char *conmgr_work_type_string(conmgr_work_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(types); i++)
		if (types[i].type == type)
			return types[i].string;

	fatal_abort("%s: invalid work type 0x%x", __func__, type);
}

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
		log_flag(NET, "%s: Ignoring duplicate shutdown request",
			 __func__);
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	mgr.initialized = false;
	mgr.shutdown_requested = true;
	mgr.quiesced = false;

	/* run all deferred work if there is any */
	_requeue_deferred_funcs();

	log_flag(NET, "%s: connection manager shutting down", __func__);

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

/*
 * Wrap work requested to notify mgr when that work is complete
 */
static void _wrap_work(void *x)
{
	work_t *work = x;
	conmgr_fd_t *con = work->con;

	log_flag(NET, "%s: %s%s%sBEGIN work=0x%"PRIxPTR" %s@0x%"PRIxPTR" type=%s status=%s arg=0x%"PRIxPTR,
		 __func__, (con ? "[" : ""), (con ? con->name : ""),
		 (con ? "] " : ""), (uintptr_t) work, work->tag,
		 (uintptr_t) work->func, conmgr_work_type_string(work->type),
		 conmgr_work_status_string(work->status),
		 (uintptr_t) work->arg);

	switch (work->type) {
	case CONMGR_WORK_TYPE_FIFO:
	case CONMGR_WORK_TYPE_TIME_DELAY_FIFO:
		xassert(!con);
		work->func(NULL, work->type, work->status, work->tag,
			   work->arg);
		break;
	case CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE:
	case CONMGR_WORK_TYPE_CONNECTION_FIFO:
	case CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO:
		wrap_con_work(work, con);
		break;
	default:
		fatal_abort("%s: invalid work type 0x%x", __func__, work->type);
	}

	log_flag(NET, "%s: %s%s%sEND work=0x%"PRIxPTR" %s@0x%"PRIxPTR" type=%s status=%s arg=0x%"PRIxPTR,
		 __func__, (con ? "[" : ""), (con ? con->name : ""),
		 (con ? "] " : ""), (uintptr_t) work, work->tag,
		 (uintptr_t) work->func, conmgr_work_type_string(work->type),
		 conmgr_work_status_string(work->status),
		 (uintptr_t) work->arg);

	signal_change(false);

	work->magic = ~MAGIC_WORK;
	xfree(work);
}

/*
 * Re-queue all deferred functions
 * WARNING: caller must hold mgr.mutex
 */
static void _requeue_deferred_funcs(void)
{
	deferred_func_t *df;

	if (mgr.quiesced)
		return;

	while ((df = list_pop(mgr.deferred_funcs))) {
		queue_func(true, df->func, df->arg, df->tag);
		xassert(df->magic == MAGIC_DEFERRED_FUNC);
		df->magic = ~MAGIC_DEFERRED_FUNC;
		xfree(df);
	}
}

extern int conmgr_run(bool blocking)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&mgr.mutex);

	if (mgr.shutdown_requested) {
		log_flag(NET, "%s: refusing to run when conmgr is shutdown",
			 __func__);

		rc = mgr.error;
		slurm_mutex_unlock(&mgr.mutex);
		return rc;
	}

	xassert(!mgr.error || !mgr.exit_on_error);
	mgr.quiesced = false;
	_requeue_deferred_funcs();
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
	log_flag(NET, "%s: shutdown requested", __func__);

	slurm_mutex_lock(&mgr.mutex);
	mgr.shutdown_requested = true;
	signal_change(true);
	slurm_mutex_unlock(&mgr.mutex);
}

extern void conmgr_quiesce(bool wait)
{
	log_flag(NET, "%s: quiesce requested", __func__);

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

/* Single point to queue internal function callback via workq. */
extern void queue_func(bool locked, work_func_t func, void *arg,
		       const char *tag)
{
	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	if (!mgr.quiesced) {
		if (workq_add_work(func, arg, tag))
			fatal_abort("%s: workq_add_work() failed", __func__);
	} else {
		/*
		 * Defer all funcs until conmgr_run() as adding new connections
		 * will call queue_func() including on_connection() callback
		 * which is very surprising before conmgr is running and can
		 * cause locking conflicts.
		 */
		deferred_func_t *df = xmalloc(sizeof(*df));
		*df = (deferred_func_t) {
			.magic = MAGIC_DEFERRED_FUNC,
			.func = func,
			.arg = arg,
			.tag = tag,
		};

		list_append(mgr.deferred_funcs, df);
	}

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

/* mgr must be locked */
static void _handle_work_run(work_t *work)
{
	queue_func(true, _wrap_work, work, work->tag);
}

/* mgr must be locked */
static void _handle_work_pending(work_t *work)
{
	conmgr_fd_t *con = work->con;

	switch (work->type) {
	case CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO:
		if (!work->con)
			fatal_abort("%s: CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO requires a connection",
				    __func__);
		/* fall through */
	case CONMGR_WORK_TYPE_TIME_DELAY_FIFO:
	{
		update_last_time(true);
		work->begin.seconds += mgr.last_time.tv_sec;
		list_append(mgr.delayed_work, work);
		update_timer(true);
		break;
	}
	case CONMGR_WORK_TYPE_CONNECTION_FIFO:
	{
		if (!con)
			fatal_abort("%s: CONMGR_WORK_TYPE_CONNECTION_FIFO requires a connection",
				    __func__);
		log_flag(NET, "%s: [%s] work_active=%c queuing \"%s\" pending work: %u total",
			 __func__, con->name, (con->work_active ? 'T' : 'F'),
			 work->tag, list_count(con->work));
		list_append(con->work, work);
		break;
	}
	case CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE:
		if (!con)
			fatal_abort("%s: CONMGR_WORK_TYPE_CONNECTION_FIFO requires a connection",
				    __func__);
		list_append(con->write_complete_work, work);
		break;
	case CONMGR_WORK_TYPE_FIFO:
		/* can be run now */
		xassert(!con);
		work->status = CONMGR_WORK_STATUS_RUN;
		handle_work(true, work);
		break;
	case CONMGR_WORK_TYPE_INVALID:
	case CONMGR_WORK_TYPE_MAX:
		fatal("%s: invalid type", __func__);
	}
}

extern void handle_work(bool locked, work_t *work)
{
	conmgr_fd_t *con = work->con;

	if (con)
		log_flag(NET, "%s: [%s] work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, con->name, (uintptr_t) work,
			conmgr_work_status_string(work->status),
			conmgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);
	else
		log_flag(NET, "%s: work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, (uintptr_t) work,
			conmgr_work_status_string(work->status),
			conmgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);

	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	switch (work->status) {
	case CONMGR_WORK_STATUS_PENDING:
		_handle_work_pending(work);
		break;
	case CONMGR_WORK_STATUS_RUN:
		_handle_work_run(work);
		break;
	case CONMGR_WORK_STATUS_CANCELLED:
		if (con)
			list_append(con->work, work);
		else
			_handle_work_run(work);
		break;
	case CONMGR_WORK_STATUS_MAX:
	case CONMGR_WORK_STATUS_INVALID:
		fatal_abort("%s: invalid work status 0x%x",
			    __func__, work->status);
	}

	signal_change(true);

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

extern void add_work(bool locked, conmgr_fd_t *con, conmgr_work_func_t func,
		     conmgr_work_type_t type, void *arg, const char *tag)
{
	work_t *work = xmalloc(sizeof(*work));
	*work = (work_t) {
		.magic = MAGIC_WORK,
		.con = con,
		.func = func,
		.arg = arg,
		.tag = tag,
		.type = type,
		.status = CONMGR_WORK_STATUS_PENDING,
	};

	handle_work(locked, work);
}

extern void conmgr_add_work(conmgr_fd_t *con, conmgr_work_func_t func,
			    conmgr_work_type_t type, void *arg,
			    const char *tag)
{
	add_work(false, con, func, type, arg, tag);
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
