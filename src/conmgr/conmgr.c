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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/param.h>
#include <sys/ucred.h>
#endif

#include "slurm/slurm.h"

#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/strlcpy.h"
#include "src/common/timers.h"
#include "src/common/util-net.h"
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

typedef struct {
	conmgr_events_t events;
	void *arg;
	conmgr_con_type_t type;
} socket_listen_init_t;

static int _close_con_for_each(void *x, void *arg);
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

/* mgr.mutex must be locked when calling this function */
extern void close_all_connections(void)
{
	/* close all connections */
	list_for_each(mgr.connections, _close_con_for_each, NULL);
	list_for_each(mgr.listen_conns, _close_con_for_each, NULL);
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
 * Stop reading from connection but write out the remaining buffer and finish
 * any queued work
 */
extern void close_con(bool locked, conmgr_fd_t *con)
{
	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	if (con->read_eof) {
		log_flag(NET, "%s: [%s] ignoring duplicate close request",
			 __func__, con->name);
		goto cleanup;
	}

	log_flag(NET, "%s: [%s] closing input", __func__, con->name);

	/* unlink listener sockets to avoid leaving ghost socket */
	if (con->is_listen && con->unix_socket &&
	    (unlink(con->unix_socket) == -1))
		error("%s: unable to unlink %s: %m",
		      __func__, con->unix_socket);

	/* mark it as EOF even if it hasn't */
	con->read_eof = true;

	if (con->is_listen) {
		if (close(con->input_fd) == -1)
			log_flag(NET, "%s: [%s] unable to close listen fd %d: %m",
				 __func__, con->name, con->output_fd);
		con->output_fd = -1;
	} else if (con->input_fd != con->output_fd) {
		/* different input FD, we can close it now */
		if (close(con->input_fd) == -1)
			log_flag(NET, "%s: [%s] unable to close input fd %d: %m",
				 __func__, con->name, con->output_fd);
	} else if (con->is_socket && shutdown(con->input_fd, SHUT_RD) == -1) {
		/* shutdown input on sockets */
		log_flag(NET, "%s: [%s] unable to shutdown read: %m",
			 __func__, con->name);
	}

	/* forget the now invalid FD */
	con->input_fd = -1;

	signal_change(true);
cleanup:
	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

extern conmgr_fd_t *add_connection(conmgr_con_type_t type,
				   conmgr_fd_t *source, int input_fd,
				   int output_fd,
				   const conmgr_events_t events,
				   const slurm_addr_t *addr,
				   socklen_t addrlen, bool is_listen,
				   const char *unix_socket_path, void *arg)
{
	struct stat fbuf = { 0 };
	conmgr_fd_t *con = NULL;
	bool set_keep_alive;

	xassert((type == CON_TYPE_RAW && events.on_data && !events.on_msg) ||
		(type == CON_TYPE_RPC && !events.on_data && events.on_msg));

	/* verify FD is valid and still open */
	if (fstat(input_fd, &fbuf) == -1) {
		log_flag(NET, "%s: invalid fd: %m", __func__);
		return NULL;
	}

	set_keep_alive =
		!unix_socket_path && S_ISSOCK(fbuf.st_mode) && !is_listen;

	/* all connections are non-blocking */
	if (set_keep_alive)
		net_set_keep_alive(input_fd);
	fd_set_nonblocking(input_fd);
	if (input_fd != output_fd) {
		fd_set_nonblocking(output_fd);

		if (set_keep_alive)
			net_set_keep_alive(output_fd);
	}

	con = xmalloc(sizeof(*con));
	*con = (conmgr_fd_t) {
		.magic = MAGIC_CON_MGR_FD,

		.input_fd = input_fd,
		.output_fd = output_fd,
		.events = events,
		/* save socket type to avoid calling fstat() again */
		.is_socket = (addr && S_ISSOCK(fbuf.st_mode)),
		.mss = NO_VAL,
		.is_listen = is_listen,
		.work = list_create(NULL),
		.write_complete_work = list_create(NULL),
		.new_arg = arg,
		.type = type,
	};

	if (!is_listen) {
		con->in = create_buf(xmalloc(BUFFER_START_SIZE),
				     BUFFER_START_SIZE);
		con->out = list_create((ListDelF) free_buf);
	}

	/* listen on unix socket */
	if (unix_socket_path) {
		xassert(con->is_socket);
		xassert(addr->ss_family == AF_LOCAL);
		con->unix_socket = xstrdup(unix_socket_path);

		/* try to resolve client directly if possible */
		con->name = sockaddr_to_string(addr, addrlen);

		if (!con->name) {
			char *outfd = fd_resolve_path(output_fd);

			if (!outfd)
				/* out of options to query */
				outfd = xstrdup_printf("fd:%u", output_fd);

			xstrfmtcat(con->name, "%s->%s (fd %u)",
				   source->unix_socket, outfd, output_fd);

			xfree(outfd);
		}
	}

#ifndef NDEBUG
	if (source && source->unix_socket && con->unix_socket)
		xassert(!xstrcmp(source->unix_socket, con->unix_socket));
#endif

	if (source && source->unix_socket && !con->unix_socket)
		con->unix_socket = xstrdup(source->unix_socket);

	if (con->name) {
		/* do nothing - connection already named */
	} else if (addr) {
		xassert(con->is_socket);

		memcpy(&con->address, addr, addrlen);

		con->name = sockaddr_to_string(addr, addrlen);

		if (!con->name && source && source->unix_socket) {
			/*
			 * if this is a unix socket, we at the very least know
			 * the source address
			 */
			char *outfd = fd_resolve_path(output_fd);

			if (!outfd)
				/* out of options to query */
				outfd = xstrdup_printf("fd:%u", output_fd);

			xstrfmtcat(con->name, "%s->%s (fd %u)",
				   source->unix_socket, outfd, output_fd);

			xfree(outfd);
		}
	} else if (input_fd == output_fd &&
		   !(con->name = fd_resolve_path(input_fd)))
		xstrfmtcat(con->name, "fd:%u", input_fd);

	if (!con->name) {
		/* different input than output */
		char *infd = fd_resolve_path(input_fd);
		char *outfd = fd_resolve_path(output_fd);

		if (!infd)
			infd = xstrdup_printf("fd:%u", input_fd);
		if (!outfd)
			outfd = xstrdup_printf("fd:%u", output_fd);

		xstrfmtcat(con->name, "%s->%s", infd, outfd);

		xfree(infd);
		xfree(outfd);
	}

	if (!con->is_listen && con->is_socket)
		con->mss = fd_get_maxmss(con->output_fd, con->name);

	log_flag(NET, "%s: [%s] new connection input_fd=%u output_fd=%u",
		 __func__, con->name, input_fd, output_fd);

	slurm_mutex_lock(&mgr.mutex);
	if (is_listen)
		list_append(mgr.listen_conns, con);
	else
		list_append(mgr.connections, con);
	slurm_mutex_unlock(&mgr.mutex);

	return con;
}

static void _wrap_con_work(work_t *work, conmgr_fd_t *con)
{
	work->func(work->con, work->type, work->status, work->tag, work->arg);

	slurm_mutex_lock(&mgr.mutex);
	con->work_active = false;
	slurm_mutex_unlock(&mgr.mutex);
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
		_wrap_con_work(work, con);
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

extern void wrap_on_connection(conmgr_fd_t *con, conmgr_work_type_t type,
			       conmgr_work_status_t status, const char *tag,
			       void *arg)
{
	if (con->events.on_connection) {
		log_flag(NET, "%s: [%s] BEGIN func=0x%"PRIxPTR,
			 __func__, con->name,
			 (uintptr_t) con->events.on_connection);

		arg = con->events.on_connection(con, con->new_arg);

		log_flag(NET, "%s: [%s] END func=0x%"PRIxPTR" arg=0x%"PRIxPTR,
			 __func__, con->name,
			 (uintptr_t) con->events.on_connection,
			 (uintptr_t) arg);

		if (!arg) {
			error("%s: [%s] closing connection due to NULL return from on_connection",
			      __func__, con->name);
			close_con(false, con);
			return;
		}
	}

	slurm_mutex_lock(&mgr.mutex);
	con->arg = arg;
	con->is_connected = true;
	slurm_mutex_unlock(&mgr.mutex);
}

extern int conmgr_process_fd(conmgr_con_type_t type, int input_fd,
			     int output_fd, const conmgr_events_t events,
			     const slurm_addr_t *addr, socklen_t addrlen,
			     void *arg)
{
	conmgr_fd_t *con;

	con = add_connection(type, NULL, input_fd, output_fd, events, addr,
			     addrlen, false, NULL, arg);

	if (!con)
		return SLURM_ERROR;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	add_work(false, con, wrap_on_connection,
		 CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
		 XSTRINGIFY(wrap_on_connection));

	return SLURM_SUCCESS;
}

extern int conmgr_process_fd_listen(int fd, conmgr_con_type_t type,
				    const conmgr_events_t events,
				    const slurm_addr_t *addr,
				    socklen_t addrlen, void *arg)
{
	conmgr_fd_t *con;

	con = add_connection(type, NULL, fd, fd, events, addr, addrlen, true,
			     NULL, arg);
	if (!con)
		return SLURM_ERROR;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	signal_change(false);

	return SLURM_SUCCESS;
}

extern int conmgr_process_fd_unix_listen(conmgr_con_type_t type, int fd,
					  const conmgr_events_t events,
					  const slurm_addr_t *addr,
					  socklen_t addrlen, const char *path,
					  void *arg)
{
	conmgr_fd_t *con;

	con = add_connection(type, NULL, fd, fd, events, addr, addrlen, true,
			     path, arg);
	if (!con)
		return SLURM_ERROR;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	signal_change(false);

	return SLURM_SUCCESS;
}

/*
 * Close all connections (for_each)
 * NOTE: must hold mgr.mutex
 */
static int _close_con_for_each(void *x, void *arg)
{
	conmgr_fd_t *con = x;
	close_con(true, con);
	return 1;
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

static void _deferred_close_fd(conmgr_fd_t *con, conmgr_work_type_t type,
			       conmgr_work_status_t status, const char *tag,
			       void *arg)
{
	slurm_mutex_lock(&mgr.mutex);
	if (con->work_active) {
		slurm_mutex_unlock(&mgr.mutex);
		conmgr_queue_close_fd(con);
	} else {
		close_con(true, con);
		slurm_mutex_unlock(&mgr.mutex);
	}
}

extern void conmgr_queue_close_fd(conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);

	slurm_mutex_lock(&mgr.mutex);
	if (!con->work_active) {
		/*
		 * Defer request to close connection until connection is no
		 * longer actively doing work as closing connection would change
		 * several variables guarenteed to not change while work is
		 * active.
		 */
		add_work(true, con, _deferred_close_fd,
			 CONMGR_WORK_TYPE_CONNECTION_FIFO, NULL,
			 XSTRINGIFY(_deferred_close_fd));
	} else {
		close_con(true, con);
	}
	slurm_mutex_unlock(&mgr.mutex);
}

static int _match_socket_address(void *x, void *key)
{
	conmgr_fd_t *con = x;
	const slurm_addr_t *addr1 = key;
	const slurm_addr_t *addr2 = &con->address;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	if (addr1->ss_family != addr2->ss_family)
		return 0;

	switch (addr1->ss_family) {
		case AF_INET:
		{
			const struct sockaddr_in *a1 =
				(const struct sockaddr_in *) addr1;
			const struct sockaddr_in *a2 =
				(const struct sockaddr_in *) addr2;

			if (a1->sin_port != a2->sin_port)
				return 0;

			return !memcmp(&a1->sin_addr.s_addr,
				       &a2->sin_addr.s_addr,
				       sizeof(a2->sin_addr.s_addr));
		}
		case AF_INET6:
		{
			const struct sockaddr_in6 *a1 =
				(const struct sockaddr_in6 *) addr1;
			const struct sockaddr_in6 *a2 =
				(const struct sockaddr_in6 *) addr2;

			if (a1->sin6_port != a2->sin6_port)
				return 0;
			if (a1->sin6_scope_id != a2->sin6_scope_id)
				return 0;
			return !memcmp(&a1->sin6_addr.s6_addr,
				       &a2->sin6_addr.s6_addr,
				       sizeof(a2->sin6_addr.s6_addr));
		}
		case AF_UNIX:
		{
			const struct sockaddr_un *a1 =
				(const struct sockaddr_un *) addr1;
			const struct sockaddr_un *a2 =
				(const struct sockaddr_un *) addr2;

			return !xstrcmp(a1->sun_path, a2->sun_path);
		}
		default:
		{
			fatal_abort("Unexpected ss family type %u",
				    (uint32_t) addr1->ss_family);
		}
	}
	/* Unreachable */
	fatal_abort("This should never happen");
}

static bool _is_listening(const slurm_addr_t *addr, socklen_t addrlen)
{
	/* use address to ensure memory size is correct */
	slurm_addr_t address = {0};

	memcpy(&address, addr, addrlen);

	if (list_find_first_ro(mgr.listen_conns, _match_socket_address,
			       &address))
		return true;

	return false;
}

static int _create_socket(void *x, void *arg)
{
	static const char UNIX_PREFIX[] = "unix:";
	const char *hostport = (const char *)x;
	const char *unixsock = xstrstr(hostport, UNIX_PREFIX);
	socket_listen_init_t *init = arg;
	int rc = SLURM_SUCCESS;
	struct addrinfo *addrlist = NULL;
	parsed_host_port_t *parsed_hp;
	conmgr_callbacks_t callbacks;

	slurm_mutex_lock(&mgr.mutex);
	callbacks = mgr.callbacks;
	slurm_mutex_unlock(&mgr.mutex);

	/* check for name local sockets */
	if (unixsock) {
		int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		struct sockaddr_un addr = {
			.sun_family = AF_UNIX,
		};

		unixsock += sizeof(UNIX_PREFIX) - 1;
		if (unixsock[0] == '\0')
			fatal("%s: [%s] Invalid UNIX socket",
			      __func__, hostport);

		if (unlink(unixsock) && (errno != ENOENT))
			error("Error unlink(%s): %m", unixsock);

		/* set value of socket path */
		strlcpy(addr.sun_path, unixsock, sizeof(addr.sun_path));
		if ((rc = bind(fd, (const struct sockaddr *) &addr,
			       sizeof(addr))))
			fatal("%s: [%s] Unable to bind UNIX socket: %m",
			      __func__, hostport);

		fd_set_oob(fd, 0);

		rc = listen(fd, SLURM_DEFAULT_LISTEN_BACKLOG);
		if (rc < 0)
			fatal("%s: [%s] unable to listen(): %m",
			      __func__, hostport);

		return conmgr_process_fd_unix_listen(init->type, fd,
			init->events, (const slurm_addr_t *) &addr,
			sizeof(addr), unixsock, init->arg);
	} else {
		/* split up host and port */
		if (!(parsed_hp = callbacks.parse(hostport)))
			fatal("%s: Unable to parse %s", __func__, hostport);

		/* resolve out the host and port if provided */
		if (!(addrlist = xgetaddrinfo(parsed_hp->host,
					      parsed_hp->port)))
			fatal("Unable to listen on %s", hostport);
	}

	/*
	 * Create a socket for every address returned
	 * ipv6 clone of net_stream_listen_ports()
	 */
	for (struct addrinfo *addr = addrlist; !rc && addr != NULL;
	     addr = addr->ai_next) {
		/* clone the address since it will be freed at
		 * end of this loop
		 */
		int fd;
		int one = 1;

		if (_is_listening((const slurm_addr_t *) addr->ai_addr,
				  addr->ai_addrlen)) {
			verbose("%s: ignoring duplicate listen request for %pA",
				__func__, (const slurm_addr_t *) addr->ai_addr);
			continue;
		}

		fd = socket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC,
			    addr->ai_protocol);
		if (fd < 0)
			fatal("%s: [%s] Unable to create socket: %m",
			      __func__, addrinfo_to_string(addr));

		/*
		 * activate socket reuse to avoid annoying timing issues
		 * with daemon restarts
		 */
		if (setsockopt(fd, addr->ai_socktype, SO_REUSEADDR,
			       &one, sizeof(one)))
			fatal("%s: [%s] setsockopt(SO_REUSEADDR) failed: %m",
			      __func__, addrinfo_to_string(addr));

		if (bind(fd, addr->ai_addr, addr->ai_addrlen) != 0)
			fatal("%s: [%s] Unable to bind socket: %m",
			      __func__, addrinfo_to_string(addr));

		fd_set_oob(fd, 0);

		rc = listen(fd, SLURM_DEFAULT_LISTEN_BACKLOG);
		if (rc < 0)
			fatal("%s: [%s] unable to listen(): %m",
			      __func__, addrinfo_to_string(addr));

		rc = conmgr_process_fd_listen(fd, init->type, init->events,
			(const slurm_addr_t *)addr->ai_addr, addr->ai_addrlen,
			init->arg);
	}

	freeaddrinfo(addrlist);
	callbacks.free_parse(parsed_hp);

	return rc;
}

extern int conmgr_create_sockets(conmgr_con_type_t type, list_t *hostports,
				 conmgr_events_t events, void *arg)
{
	int rc;
	socket_listen_init_t *init = xmalloc(sizeof(*init));
	init->events = events;
	init->arg = arg;
	init->type = type;

	if (list_for_each(hostports, _create_socket, init) > 0)
		rc = SLURM_SUCCESS;
	else
		rc = SLURM_ERROR;

	xfree(init);

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

extern int conmgr_get_fd_auth_creds(conmgr_fd_t *con,
				     uid_t *cred_uid, gid_t *cred_gid,
				     pid_t *cred_pid)
{
	int fd, rc = ESLURM_NOT_SUPPORTED;

	xassert(cred_uid);
	xassert(cred_gid);
	xassert(cred_pid);

	if (!con || !cred_uid || !cred_gid || !cred_pid)
		return EINVAL;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	if (((fd = con->input_fd) == -1) && ((fd = con->output_fd) == -1))
		return SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR;

#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__NetBSD__)
	struct ucred cred = { 0 };
	socklen_t len = sizeof(cred);
	if (!getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len)) {
		*cred_uid = cred.uid;
		*cred_gid = cred.gid;
		*cred_pid = cred.pid;
		return SLURM_SUCCESS;
	} else {
		rc = errno;
	}
#else
	struct xucred cred = { 0 };
	socklen_t len = sizeof(cred);
	if (!getsockopt(fd, 0, LOCAL_PEERCRED, &cred, &len)) {
		*cred_uid = cred.cr_uid;
		*cred_gid = cred.cr_groups[0];
		*cred_pid = cred.cr_pid;
		return SLURM_SUCCESS;
	} else {
		rc = errno;
	}
#endif

	return rc;
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

extern const char *conmgr_fd_get_name(const conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->name && con->name[0]);
	return con->name;
}

extern conmgr_fd_status_t conmgr_fd_get_status(conmgr_fd_t *con)
{
	conmgr_fd_status_t status = {
		.is_socket = con->is_socket,
		.unix_socket = con->unix_socket,
		.is_listen = con->is_listen,
		.read_eof = con->read_eof,
		.is_connected = con->is_connected,
	};

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->work_active);
	return status;
}
