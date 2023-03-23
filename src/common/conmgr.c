/*****************************************************************************\
 *  conmgr.c - definitions for connection manager
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

#define _GNU_SOURCE

#include "config.h"

#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/conmgr.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/strlcpy.h"
#include "src/common/timers.h"
#include "src/common/workq.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MAGIC_CON_MGR_FD 0xD23444EF
#define MAGIC_CON_MGR 0xD232444A
#define MAGIC_WORK 0xD231444A
#define MAGIC_FOREACH_DELAYED_WORK 0xB233443A
#define MAGIC_DEFERRED_FUNC 0xA230403A
/* Default buffer to 1 page */
#define BUFFER_START_SIZE 4096
#define MAX_OPEN_CONNECTIONS 124

/*
 * there can only be 1 SIGNAL handler, so we are using a mutex to protect
 * the signal_fd for changes.
 */
pthread_mutex_t signal_mutex = PTHREAD_MUTEX_INITIALIZER;
int signal_fd[2] = { -1, -1 };

typedef void (*on_poll_event_t)(con_mgr_t *mgr, int fd, con_mgr_fd_t *con,
				short revents);

typedef struct {
	int magic;
	con_mgr_t *mgr;
	con_mgr_fd_t *con;
	con_mgr_work_func_t func;
	void *arg;
	const char *tag;
	con_mgr_work_status_t status;
	con_mgr_work_type_t type;
	struct {
		/* absolute time when to work can begin */
		time_t seconds;
		long nanoseconds; /* offset from seconds */
	} begin;
} work_t;

typedef struct {
	int magic; /* MAGIC_DEFERRED_FUNC */
	work_func_t func;
	void *arg;
	const char *tag;
} deferred_func_t;

struct {
	con_mgr_work_status_t status;
	const char *string;
} statuses[] = {
	{ CONMGR_WORK_STATUS_INVALID, "INVALID" },
	{ CONMGR_WORK_STATUS_PENDING, "PENDING" },
	{ CONMGR_WORK_STATUS_RUN, "RUN" },
	{ CONMGR_WORK_STATUS_CANCELLED, "CANCELLED" },
};

struct {
	con_mgr_work_type_t type;
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

/* simple struct to keep track of fds */
typedef struct {
	con_mgr_t *mgr;
	struct pollfd *fds;
	int nfds;
} poll_args_t;

static void _signal_handler(int signo);

/* array of all signals to be caught */
static struct {
	struct sigaction prior;
	struct sigaction new;
	int signal;
} catch_signals[] = {
	/* Catch SIGINT as a safe way to shutdown */
	{ {}, { .sa_handler = _signal_handler }, SIGINT },
	/* Catch SIGALRM for timers */
	{ {}, { .sa_handler = _signal_handler }, SIGALRM },
};

typedef struct {
	int magic; /* MAGIC_FOREACH_DELAYED_WORK */
	con_mgr_t *mgr;
	work_t *shortest;
} foreach_delayed_work_t;


typedef struct {
	con_mgr_events_t events;
	con_mgr_t *mgr;
	void *arg;
	con_mgr_con_type_t type;
} socket_listen_init_t;

static int _close_con_for_each(void *x, void *arg);
static void _listen_accept(con_mgr_t *mgr, con_mgr_fd_t *con,
			   con_mgr_work_type_t type,
			   con_mgr_work_status_t status, const char *tag,
			   void *arg);
static void _wrap_on_connection(con_mgr_t *mgr, con_mgr_fd_t *con,
				con_mgr_work_type_t type,
				con_mgr_work_status_t status, const char *tag,
				void *arg);
static void _add_work(bool locked, con_mgr_t *mgr, con_mgr_fd_t *con,
		      con_mgr_work_func_t func, con_mgr_work_type_t type,
		      void *arg, const char *tag);
static void _wrap_on_data(con_mgr_t *mgr, con_mgr_fd_t *con,
			  con_mgr_work_type_t type,
			  con_mgr_work_status_t status, const char *tag,
			  void *arg);
static void _on_finish_wrapper(con_mgr_t *mgr, con_mgr_fd_t *con,
			       con_mgr_work_type_t type,
			       con_mgr_work_status_t status, const char *tag,
			       void *arg);
static void _deferred_write_fd(con_mgr_t *mgr, con_mgr_fd_t *con,
			  con_mgr_work_type_t type,
			  con_mgr_work_status_t status, const char *tag,
			  void *arg);
static void _cancel_delayed_work(bool locked, con_mgr_t *mgr);
static void _handle_timer(void *x);
static void _handle_work(bool locked, work_t *work);
static void _queue_func(bool locked, con_mgr_t *mgr, work_func_t func,
			void *arg, const char *tag);

/*
 * Find by matching fd to connection
 */
static int _find_by_fd(void *x, void *key)
{
	con_mgr_fd_t *con = x;
	int fd = *(int *)key;
	return (con->input_fd == fd) || (con->output_fd == fd);
}

extern const char *con_mgr_work_status_string(con_mgr_work_status_t status)
{
	for (int i = 0; i < ARRAY_SIZE(statuses); i++)
		if (statuses[i].status == status)
			return statuses[i].string;

	fatal_abort("%s: invalid work status 0x%x", __func__, status);
}

extern const char *con_mgr_work_type_string(con_mgr_work_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(types); i++)
		if (types[i].type == type)
			return types[i].string;

	fatal_abort("%s: invalid work type 0x%x", __func__, type);
}

static void _connection_fd_delete(void *x)
{
	con_mgr_fd_t *con = x;

	log_flag(NET, "%s: [%s] free connection input_fd=%d output_fd=%d",
		 __func__, con->name, con->input_fd, con->output_fd);

	FREE_NULL_BUFFER(con->in);
	FREE_NULL_BUFFER(con->out);
	FREE_NULL_LIST(con->work);
	FREE_NULL_LIST(con->write_complete_work);
	FREE_NULL_LIST(con->deferred_out);
	xfree(con->name);
	xfree(con->unix_socket);

	con->magic = ~MAGIC_CON_MGR_FD;
	xfree(con);
}

static void _signal_handler(int signo)
{
try_again:
	if (write(signal_fd[1], &signo, sizeof(signo)) != sizeof(signo)) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			goto try_again;

		log_reinit();
		fatal("%s: unable to signal connection manager: %m", __func__);
	}
}

extern con_mgr_t *init_con_mgr(int thread_count, con_mgr_callbacks_t callbacks)
{
	con_mgr_t *mgr = xmalloc(sizeof(*mgr));

	mgr->magic = MAGIC_CON_MGR;
	mgr->connections = list_create(NULL);
	mgr->listen = list_create(NULL);
	mgr->complete = list_create(NULL);
	mgr->callbacks = callbacks;

	slurm_mutex_init(&mgr->mutex);
	slurm_cond_init(&mgr->cond, NULL);

	mgr->workq = new_workq(thread_count);
	mgr->deferred_funcs = list_create(NULL);

	if (pipe(mgr->event_fd))
		fatal("%s: unable to open unnamed pipe: %m", __func__);

	fd_set_nonblocking(mgr->event_fd[0]);
	fd_set_blocking(mgr->event_fd[1]);

	if (pipe(mgr->signal_fd))
		fatal("%s: unable to open unnamed pipe: %m", __func__);

	/* block for writes only */
	fd_set_nonblocking(mgr->signal_fd[0]);
	fd_set_blocking(mgr->signal_fd[1]);

	return mgr;
}

/*
 * Notify connection manager that there has been a change event
 */
static void _signal_change(con_mgr_t *mgr, bool locked)
{
	DEF_TIMERS;
	char buf[] = "1";
	int rc;

	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

	if (mgr->event_signaled) {
		mgr->event_signaled++;
		log_flag(NET, "%s: sent %d times",
			 __func__, mgr->event_signaled);
		goto done;
	} else {
		log_flag(NET, "%s: sending", __func__);
		mgr->event_signaled = 1;
	}

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);

try_again:
	START_TIMER;
	/* send 1 byte of trash */
	rc = write(mgr->event_fd[1], buf, 1);
	END_TIMER2("write to event_fd");
	if (rc != 1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			log_flag(NET, "%s: trying again: %m", __func__);
			goto try_again;
		}

		fatal("%s: unable to signal connection manager: %m", __func__);
	}

	log_flag(NET, "%s: sent in %s", __func__, TIME_STR);

	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

done:
	/* wake up _watch() */
	slurm_cond_broadcast(&mgr->cond);

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);
}

static void _close_all_connections(bool locked, con_mgr_t *mgr)
{
	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

	/* close all connections */
	list_for_each(mgr->connections, _close_con_for_each, NULL);
	list_for_each(mgr->listen, _close_con_for_each, NULL);

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);
}

extern void free_con_mgr(con_mgr_t *mgr)
{
	if (!mgr)
		return;

	log_flag(NET, "%s: connection manager shutting down", __func__);

	/* processing may still be running at this point in a thread */
	_close_all_connections(false, mgr);

	/* tell all timers about being canceled */
	_cancel_delayed_work(false, mgr);

	/*
	 * make sure WORKQ is done before making any changes encase there are
	 * any outstanding threads running
	 */
	FREE_NULL_WORKQ(mgr->workq);

	/* deferred_funcs should have been cleared by con_mgr_run() */
	FREE_NULL_LIST(mgr->deferred_funcs);

	/*
	 * At this point, there should be no threads running.
	 * It should be safe to shutdown the mgr.
	 */
	FREE_NULL_LIST(mgr->connections);
	FREE_NULL_LIST(mgr->listen);
	FREE_NULL_LIST(mgr->complete);

	if (mgr->delayed_work) {
		FREE_NULL_LIST(mgr->delayed_work);
		if (timer_delete(mgr->timer))
			fatal("%s: timer_delete() failed: %m", __func__);
	}

	slurm_mutex_destroy(&mgr->mutex);
	slurm_cond_destroy(&mgr->cond);

	if (close(mgr->event_fd[0]) || close(mgr->event_fd[1]))
		error("%s: unable to close event_fd: %m", __func__);

	if (close(mgr->signal_fd[0]) || close(mgr->signal_fd[1]))
		error("%s: unable to close signal_fd: %m", __func__);

	mgr->magic = ~MAGIC_CON_MGR;
	xfree(mgr);
}

/*
 * Stop reading from connection but write out the remaining buffer and finish
 * any queued work
 */
static void _close_con(bool locked, con_mgr_fd_t *con)
{
	if (!locked)
		slurm_mutex_lock(&con->mgr->mutex);

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

	_signal_change(con->mgr, true);
cleanup:
	if (!locked)
		slurm_mutex_unlock(&con->mgr->mutex);
}

static con_mgr_fd_t *_add_connection(
	con_mgr_t *mgr, con_mgr_con_type_t type, con_mgr_fd_t *source,
	int input_fd, int output_fd, const con_mgr_events_t events,
	const slurm_addr_t *addr, socklen_t addrlen, bool is_listen,
	const char *unix_socket_path, void *arg)
{
	struct stat fbuf = { 0 };
	con_mgr_fd_t *con = NULL;

	xassert(mgr->magic == MAGIC_CON_MGR);
	xassert((type == CON_TYPE_RAW && events.on_data && !events.on_msg) ||
		(type == CON_TYPE_RPC && !events.on_data && events.on_msg));

	/* verify FD is valid and still open */
	if (fstat(input_fd, &fbuf) == -1) {
		log_flag(NET, "%s: invalid fd: %m", __func__);
		return NULL;
	}

	/* all connections are non-blocking */
	net_set_keep_alive(input_fd);
	fd_set_nonblocking(input_fd);
	if (input_fd != output_fd) {
		fd_set_nonblocking(output_fd);
		net_set_keep_alive(output_fd);
	}

	con = xmalloc(sizeof(*con));
	*con = (con_mgr_fd_t){
		.magic = MAGIC_CON_MGR_FD,

		.input_fd = input_fd,
		.output_fd = output_fd,
		.events = events,
		/* save socket type to avoid calling fstat() again */
		.is_socket = (addr && S_ISSOCK(fbuf.st_mode)),
		.is_listen = is_listen,
		.mgr = mgr,
		.work = list_create(NULL),
		.write_complete_work = list_create(NULL),
		.new_arg = arg,
		.type = type,
		.deferred_out = list_create((ListDelF) free_buf),
	};

	if (!is_listen) {
		con->in = create_buf(xmalloc(BUFFER_START_SIZE),
				     BUFFER_START_SIZE);
		con->out = create_buf(xmalloc(BUFFER_START_SIZE),
				      BUFFER_START_SIZE);
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

			xstrfmtcat(con->name, "%s->%s", unix_socket_path,
				   outfd);

			xfree(outfd);
		}
	}

	if (source && source->unix_socket)
		con->unix_socket = xstrdup(source->unix_socket);

	if (con->name) {
		/* do nothing - connection already named */
	} else if (addr) {
		xassert(con->is_socket);
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

			xstrfmtcat(con->name, "%s->%s", source->unix_socket,
				   outfd);

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

	log_flag(NET, "%s: [%s] new connection input_fd=%u output_fd=%u",
		 __func__, con->name, input_fd, output_fd);

	slurm_mutex_lock(&mgr->mutex);
	if (is_listen)
		list_append(mgr->listen, con);
	else
		list_append(mgr->connections, con);
	slurm_mutex_unlock(&mgr->mutex);

	return con;
}

static void _wrap_con_work(work_t *work, con_mgr_fd_t *con, con_mgr_t *mgr)
{
	work->func(work->mgr, work->con, work->type, work->status, work->tag,
		   work->arg);

	slurm_mutex_lock(&mgr->mutex);
	con->work_active = false;
	slurm_mutex_unlock(&mgr->mutex);
}

/*
 * Wrap work requested to notify mgr when that work is complete
 */
static void _wrap_work(void *x)
{
	work_t *work = x;
	con_mgr_fd_t *con = work->con;
	con_mgr_t *mgr = work->mgr;

	log_flag(NET, "%s: %s%s%sBEGIN work=0x%"PRIxPTR" %s@0x%"PRIxPTR" type=%s status=%s arg=0x%"PRIxPTR,
		 __func__, (con ? "[" : ""), (con ? con->name : ""),
		 (con ? "] " : ""), (uintptr_t) work, work->tag,
		 (uintptr_t) work->func, con_mgr_work_type_string(work->type),
		 con_mgr_work_status_string(work->status),
		 (uintptr_t) work->arg);

	switch (work->type) {
	case CONMGR_WORK_TYPE_FIFO:
	case CONMGR_WORK_TYPE_TIME_DELAY_FIFO:
		xassert(!con);
		work->func(work->mgr, NULL, work->type, work->status, work->tag,
			   work->arg);
		break;
	case CONMGR_WORK_TYPE_CONNECTION_WRITE_COMPLETE:
	case CONMGR_WORK_TYPE_CONNECTION_FIFO:
	case CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO:
		_wrap_con_work(work, con, mgr);
		break;
	default:
		fatal_abort("%s: invalid work type 0x%x", __func__, work->type);
	}

	log_flag(NET, "%s: %s%s%sEND work=0x%"PRIxPTR" %s@0x%"PRIxPTR" type=%s status=%s arg=0x%"PRIxPTR,
		 __func__, (con ? "[" : ""), (con ? con->name : ""),
		 (con ? "] " : ""), (uintptr_t) work, work->tag,
		 (uintptr_t) work->func, con_mgr_work_type_string(work->type),
		 con_mgr_work_status_string(work->status),
		 (uintptr_t) work->arg);

	_signal_change(mgr, false);

	work->magic = ~MAGIC_WORK;
	xfree(work);
}

static void _handle_read(con_mgr_t *mgr, con_mgr_fd_t *con,
			 con_mgr_work_type_t type, con_mgr_work_status_t status,
			 const char *tag, void *arg)
{
	ssize_t read_c;
	int readable;

	con->can_read = false;
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->mgr->magic == MAGIC_CON_MGR);

	if (con->input_fd < 0) {
		log_flag(NET, "%s: [%s] called on closed connection",
			 __func__, con->name);
		return;
	}

#ifdef FIONREAD
	/* request kernel tell us the size of the incoming buffer */
	if (ioctl(con->input_fd, FIONREAD, &readable))
		log_flag(NET, "%s: [%s] unable to call FIONREAD: %m",
			 __func__, con->name);
	else if (readable == 0) {
		/* Didn't fail but buffer is empty so this may be EOF */
		readable = 1;
	}
#else
	/* default to at least 512 available in buffer */
	readable = 512;
#endif /* FIONREAD */

	/* Grow buffer as needed to handle the incoming data */
	if (remaining_buf(con->in) < readable) {
		int need = readable - remaining_buf(con->in);

		if ((need + size_buf(con->in)) >= MAX_BUF_SIZE) {
			error("%s: [%s] out of buffer space.",
			      __func__, con->name);

			_close_con(false, con);
			return;
		}

		grow_buf(con->in, need);
	}

	/* check for errors with a NULL read */
	read_c = read(con->input_fd,
		      (get_buf_data(con->in) + get_buf_offset(con->in)),
		      readable);
	if (read_c == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			log_flag(NET, "%s: [%s] socket would block on read",
				 __func__, con->name);
			return;
		}

		log_flag(NET, "%s: [%s] error while reading: %m",
			 __func__, con->name);
		_close_con(false, con);
		return;
	} else if (read_c == 0) {
		log_flag(NET, "%s: [%s] read %zd bytes and EOF with %u bytes to process already in buffer",
			 __func__, con->name, read_c, get_buf_offset(con->in));

		slurm_mutex_lock(&con->mgr->mutex);
		/* lock to tell mgr that we are done */
		con->read_eof = true;
		slurm_mutex_unlock(&con->mgr->mutex);
	} else {
		log_flag(NET, "%s: [%s] read %zd bytes with %u bytes to process already in buffer",
			 __func__, con->name, read_c, get_buf_offset(con->in));
		log_flag_hex(NET_RAW,
			     (get_buf_data(con->in) + get_buf_offset(con->in)),
			     read_c, "%s: [%s] read", __func__, con->name);

		get_buf_offset(con->in) += read_c;
	}
}

static void _handle_write(con_mgr_t *mgr, con_mgr_fd_t *con,
			  con_mgr_work_type_t type,
			  con_mgr_work_status_t status, const char *tag,
			  void *arg)
{
	ssize_t wrote;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->mgr->magic == MAGIC_CON_MGR);

	if (get_buf_offset(con->out) == 0) {
		log_flag(NET, "%s: [%s] skipping attempt to write 0 bytes",
			 __func__, con->name);
		return;
	}

	log_flag(NET, "%s: [%s] attempting to write %u bytes to fd %u",
		 __func__, con->name, get_buf_offset(con->out), con->output_fd);

	/* write in non-blocking fashion as we can always continue later */
	if (con->is_socket)
		/* avoid ESIGPIPE on sockets and never block */
		wrote = send(con->output_fd, get_buf_data(con->out),
			     get_buf_offset(con->out),
			     (MSG_DONTWAIT | MSG_NOSIGNAL));
	else /* normal write for non-sockets */
		wrote = write(con->output_fd, get_buf_data(con->out),
			      get_buf_offset(con->out));

	if (wrote == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			log_flag(NET, "%s: [%s] retry write: %m",
				 __func__, con->name);
			return;
		}

		error("%s: [%s] error while write: %m", __func__, con->name);
		/* drop outbound data on the floor */
		set_buf_offset(con->out, 0);
		_close_con(false, con);
		return;
	} else if (wrote == 0) {
		log_flag(NET, "%s: [%s] write 0 bytes", __func__, con->name);
		return;
	}

	log_flag(NET, "%s: [%s] wrote %zu/%u bytes",
		 __func__, con->name, wrote, get_buf_offset(con->out));
	log_flag_hex(NET_RAW, get_buf_data(con->out), wrote,
		     "%s: [%s] wrote", __func__, con->name);

	if (wrote != get_buf_offset(con->out)) {
		/*
		 * not all data written, need to shift it to start of
		 * buffer and fix offset
		 */
		memmove(get_buf_data(con->out),
			(get_buf_data(con->out) + wrote),
			(get_buf_offset(con->out) - wrote));

		/* reset start of offset to end of previous data */
		set_buf_offset(con->out, (get_buf_offset(con->out) - wrote));
	} else
		set_buf_offset(con->out, 0);
}

static int _on_rpc_connection_data(con_mgr_fd_t *con, void *arg)
{
	int rc = SLURM_ERROR;
	uint32_t need;
	slurm_msg_t *msg = NULL;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	/* based on slurm_msg_recvfrom_timeout() */
	if (!con->msglen) {
		log_flag(NET, "%s: [%s] got %d bytes pending for RPC connection",
			 __func__, con->name, size_buf(con->in));

		xassert(sizeof(con->msglen) == sizeof(uint32_t));
		if (size_buf(con->in) >= sizeof(con->msglen)) {
			con->msglen = ntohl(
				*(uint32_t *) get_buf_data(con->in));
			log_flag(NET, "%s: [%s] got message length %u for RPC connection with %d bytes pending",
				 __func__, con->name, con->msglen, size_buf(con->in));
		} else {
			log_flag(NET, "%s: [%s] waiting for message length for RPC connection",
				 __func__, con->name);
			return SLURM_SUCCESS;
		}

		if (con->msglen > MAX_MSG_SIZE) {
			log_flag(NET, "%s: [%s] rejecting RPC message length: %u",
				 __func__, con->name, con->msglen);
			return SLURM_PROTOCOL_INSANE_MSG_LENGTH;
		}
	}

	need = sizeof(con->msglen) + con->msglen;
	if (size_buf(con->in) < need) {
		uint32_t delta = (need - size_buf(con->in));
		log_flag(NET, "%s: [%s] increasing buffer %u bytes for  RPC message length: %u",
			 __func__, con->name, delta, con->msglen);

		grow_buf(con->in, delta);
	}

	if (size_buf(con->in) >= need) {
		/* there is enough data to unpack now */
		msg = xmalloc(sizeof(*msg));
		slurm_msg_t_init(msg);

		/* shift the data pointer up by sizeof(msglen) */
		get_buf_data(con->in) += sizeof(con->msglen);

		log_flag_hex(NET_RAW, get_buf_data(con->in),
			     size_buf(con->in), "%s: [%s] unpacking RPC",
			     __func__, con->name);

		if ((rc = slurm_unpack_received_msg(msg, con->input_fd,
						    con->in))) {
			rc = errno;
			error("%s: [%s] unpack_msg() failed: %s",
			      __func__, con->name, slurm_strerror(rc));
			slurm_free_msg(msg);
			msg = NULL;
		} else {
			log_flag(NET, "%s: [%s] unpacked %u bytes containing %s RPC",
				 __func__, con->name, need,
				 rpc_num2string(msg->msg_type));
		}

		/* unshift the data pointer */
		get_buf_data(con->in) -= sizeof(con->msglen);

		/* notify conmgr we processed some data */
		set_buf_offset(con->in, need);

		/* reset message length to start all over again */
		con->msglen = 0;
	} else {
		log_flag(NET, "%s: [%s] waiting for message length %u/%u for RPC message",
			 __func__, con->name, size_buf(con->in), need);
		return SLURM_SUCCESS;
	}

	if (!rc && msg) {
		log_flag(PROTOCOL, "%s: [%s] received RPC %s",
			 __func__, con->name, rpc_num2string(msg->msg_type));
		log_flag(NET, "%s: [%s] RPC BEGIN func=0x%"PRIxPTR" arg=0x%"PRIxPTR,
			 __func__, con->name, (uintptr_t) con->events.on_msg,
			 (uintptr_t) con->arg);
		rc = con->events.on_msg(con, msg, con->arg);
		log_flag(NET, "%s: [%s] RPC END func=0x%"PRIxPTR" arg=0x%"PRIxPTR" rc=%s",
			 __func__, con->name, (uintptr_t) con->events.on_msg,
			 (uintptr_t) con->arg, slurm_strerror(rc));
	}

	return rc;
}

static void _wrap_on_data(con_mgr_t *mgr, con_mgr_fd_t *con,
			  con_mgr_work_type_t type,
			  con_mgr_work_status_t status, const char *tag,
			  void *arg)
{
	int avail = get_buf_offset(con->in);
	int size = size_buf(con->in);
	int rc;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(mgr->magic == MAGIC_CON_MGR);

	/* override buffer offset to allow reading */
	set_buf_offset(con->in, 0);
	/* override buffer size to only read upto previous offset */
	con->in->size = avail;

	log_flag(NET, "%s: [%s] BEGIN func=0x%"PRIxPTR" arg=0x%"PRIxPTR,
		 __func__, con->name, (uintptr_t) con->events.on_data,
		 (uintptr_t) con->arg);

	if (con->type == CON_TYPE_RAW)
		rc = con->events.on_data(con, con->arg);
	else if (con->type == CON_TYPE_RPC)
		rc = _on_rpc_connection_data(con, con->arg);
	else
		fatal("%s: invalid type", __func__);

	log_flag(NET, "%s: [%s] END func=0x%"PRIxPTR" arg=0x%"PRIxPTR" rc=%s",
		 __func__, con->name, (uintptr_t) con->events.on_data,
		 (uintptr_t) con->arg, slurm_strerror(rc));

	if (rc) {
		error("%s: [%s] on_data returned rc: %s",
		      __func__, con->name, slurm_strerror(rc));

		slurm_mutex_lock(&mgr->mutex);
		if (mgr->exit_on_error)
			mgr->shutdown = true;

		if (!mgr->error)
			mgr->error = rc;
		slurm_mutex_unlock(&mgr->mutex);

		/*
		 * processing data failed so drop any
		 * pending data on the floor
		 */
		log_flag(NET, "%s: [%s] on_data callback failed. Purging the remaining %d bytes of pending input.",
			 __func__, con->name, get_buf_offset(con->in));
		set_buf_offset(con->in, 0);

		_close_con(false, con);
		return;
	}

	if (get_buf_offset(con->in) < size_buf(con->in)) {
		if (get_buf_offset(con->in) > 0) {
			/*
			 * not all data read, need to shift it to start of
			 * buffer and fix offset
			 */
			memmove(get_buf_data(con->in),
				(get_buf_data(con->in) +
				 get_buf_offset(con->in)),
				remaining_buf(con->in));

			/* reset start of offset to end of previous data */
			set_buf_offset(con->in, remaining_buf(con->in));
		} else {
			/* need more data for parser to read */
			log_flag(NET, "%s: [%s] parser refused to read data. Waiting for more data.",
				 __func__, con->name);
			con->on_data_tried = true;
		}
	} else
		/* buffer completely read: reset it */
		set_buf_offset(con->in, 0);

	/* restore original size */
	con->in->size = size;
}

static void _wrap_on_connection(con_mgr_t *mgr, con_mgr_fd_t *con,
				con_mgr_work_type_t type,
				con_mgr_work_status_t status, const char *tag,
				void *arg)
{
	log_flag(NET, "%s: [%s] BEGIN func=0x%"PRIxPTR,
		 __func__, con->name, (uintptr_t) con->events.on_connection);

	arg = con->events.on_connection(con, con->new_arg);

	log_flag(NET, "%s: [%s] END func=0x%"PRIxPTR" arg=0x%"PRIxPTR,
		 __func__, con->name, (uintptr_t) con->events.on_connection,
		 (uintptr_t) arg);

	if (!arg) {
		error("%s: [%s] closing connection due to NULL return from on_connection",
		      __func__, con->name);
		_close_con(false, con);
	} else {
		slurm_mutex_lock(&mgr->mutex);
		con->arg = arg;
		con->is_connected = true;
		slurm_mutex_unlock(&mgr->mutex);
	}
}

extern int _con_mgr_process_fd_internal(con_mgr_t *mgr, con_mgr_con_type_t type,
					con_mgr_fd_t *source, int input_fd,
					int output_fd,
					const con_mgr_events_t events,
					const slurm_addr_t *addr,
					socklen_t addrlen, void *arg)
{
	con_mgr_fd_t *con;
	xassert(mgr->magic == MAGIC_CON_MGR);

	con = _add_connection(mgr, type, source, input_fd, output_fd, events,
			      addr, addrlen, false, NULL, arg);

	if (!con)
		return SLURM_ERROR;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	_add_work(false, mgr, con, _wrap_on_connection,
		  CONMGR_WORK_TYPE_CONNECTION_FIFO, con, "_wrap_on_connection");

	return SLURM_SUCCESS;
}

extern int con_mgr_process_fd(con_mgr_t *mgr, con_mgr_con_type_t type,
			      int input_fd, int output_fd,
			      const con_mgr_events_t events,
			      const slurm_addr_t *addr, socklen_t addrlen,
			      void *arg)
{
	con_mgr_fd_t *con;
	xassert(mgr->magic == MAGIC_CON_MGR);

	con = _add_connection(mgr, type, NULL, input_fd, output_fd, events,
			      addr, addrlen, false, NULL, arg);

	if (!con)
		return SLURM_ERROR;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	_add_work(false, mgr, con, _wrap_on_connection,
		  CONMGR_WORK_TYPE_CONNECTION_FIFO, con, "_wrap_on_connection");

	return SLURM_SUCCESS;
}

extern int con_mgr_process_fd_listen(con_mgr_t *mgr, int fd,
				     con_mgr_con_type_t type,
				     const con_mgr_events_t events,
				     const slurm_addr_t *addr,
				     socklen_t addrlen, void *arg)
{
	con_mgr_fd_t *con;
	xassert(mgr->magic == MAGIC_CON_MGR);

	con = _add_connection(mgr, type, NULL, fd, fd, events, addr, addrlen,
			      true, NULL, arg);
	if (!con)
		return SLURM_ERROR;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	_signal_change(mgr, false);

	return SLURM_SUCCESS;
}

extern int con_mgr_process_fd_unix_listen(con_mgr_t *mgr,
					  con_mgr_con_type_t type, int fd,
					  const con_mgr_events_t events,
					  const slurm_addr_t *addr,
					  socklen_t addrlen, const char *path,
					  void *arg)
{
	con_mgr_fd_t *con;
	xassert(mgr->magic == MAGIC_CON_MGR);

	con = _add_connection(mgr, type, NULL, fd, fd, events, addr, addrlen,
			      true, path, arg);
	if (!con)
		return SLURM_ERROR;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	_signal_change(mgr, false);

	return SLURM_SUCCESS;
}

/*
 * Event on a processing socket.
 * mgr must be locked.
 */
static void _handle_poll_event(con_mgr_t *mgr, int fd, con_mgr_fd_t *con,
			       short revents)
{
	con->can_read = false;
	con->can_write = false;

	if (revents & POLLNVAL) {
		error("%s: [%s] connection invalid", __func__, con->name);
		_close_con(true, con);
		return;
	}
	if (revents & POLLERR) {
		int err = SLURM_ERROR;
		int rc;

		if (con->is_socket) {
			/* connection may have got RST */
			if ((rc = fd_get_socket_error(con->input_fd, &err))) {
				error("%s: [%s] poll error: fd_get_socket_error failed %s",
				      __func__, con->name, slurm_strerror(rc));
			} else {
				error("%s: [%s] poll error: %s",
				      __func__, con->name, slurm_strerror(err));
			}
		}


		_close_con(true, con);
		return;
	}

	if (fd == con->input_fd)
		con->can_read = revents & POLLIN || revents & POLLHUP;
	if (fd == con->output_fd)
		con->can_write = revents & POLLOUT;

	log_flag(NET, "%s: [%s] fd=%u can_read=%s can_write=%s",
		 __func__, con->name, fd, (con->can_read ? "T" : "F"),
		 (con->can_write ? "T" : "F"));
}

static void _on_finish_wrapper(con_mgr_t *mgr, con_mgr_fd_t *con,
			       con_mgr_work_type_t type,
			       con_mgr_work_status_t status, const char *tag,
			       void *arg)
{
	con->events.on_finish(arg);

	slurm_mutex_lock(&mgr->mutex);
	con->wait_on_finish = false;
	/* on_finish must free arg */
	con->arg = NULL;
	slurm_mutex_unlock(&mgr->mutex);
}

/*
 * handle connection states and apply actions required.
 * mgr mutex must be locked.
 *
 * RET 1 to remove or 0 to remain in list
 */
static int _handle_connection(void *x, void *arg)
{
	con_mgr_fd_t *con = x;
	con_mgr_t *mgr = con->mgr;
	int count;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(mgr->magic == MAGIC_CON_MGR);

	/* connection may have a running thread, do nothing */
	if (con->work_active) {
		log_flag(NET, "%s: [%s] connection has work to do",
			 __func__, con->name);
		return 0;
	}

	/* always do work first */
	if ((count = list_count(con->work))) {
		work_t *work = list_pop(con->work);

		log_flag(NET, "%s: [%s] queuing pending work: %u total",
			 __func__, con->name, count);

		work->status = CONMGR_WORK_STATUS_RUN;
		con->work_active = true; /* unset by _wrap_con_work() */

		log_flag(NET, "%s: [%s] queuing work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, con->name, (uintptr_t) work,
			con_mgr_work_status_string(work->status),
			con_mgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);

		_handle_work(true, work);
		return 0;
	}

	/* make sure the connection has finished on_connection */
	if (!con->is_listen && !con->is_connected) {
		log_flag(NET, "%s: [%s] waiting for on_connection to complete",
			 __func__, con->name);
		return 0;
	}

	/* handle out going data */
	if (!con->is_listen && con->output_fd != -1 &&
	    (count = get_buf_offset(con->out))) {
		if (con->can_write) {
			log_flag(NET, "%s: [%s] need to write %u bytes",
				 __func__, con->name, count);
			_add_work(true, mgr, con, _handle_write,
				  CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
				  "_handle_write");
		} else {
			/* must wait until poll allows write of this socket */
			log_flag(NET, "%s: [%s] waiting to write %u bytes",
				 __func__, con->name, get_buf_offset(con->out));
		}
		return 0;
	}

	if ((count = list_count(con->write_complete_work))) {
		log_flag(NET, "%s: [%s] queuing pending write complete work: %u total",
			 __func__, con->name, count);

		list_transfer(con->work, con->write_complete_work);
		return 0;
	}

	/* read as much data as possible before processing */
	if (!con->is_listen && !con->read_eof && con->can_read) {
		log_flag(NET, "%s: [%s] queuing read", __func__, con->name);
		/* reset if data has already been tried if about to read data */
		con->on_data_tried = false;
		_add_work(true, mgr, con, _handle_read,
			  CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
			  "_handle_read");
		return 0;
	}

	/* handle already read data */
	if (!con->is_listen && get_buf_offset(con->in) && !con->on_data_tried) {
		log_flag(NET, "%s: [%s] need to process %u bytes",
			 __func__, con->name, get_buf_offset(con->in));

		_add_work(true, mgr, con, _wrap_on_data,
			  CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
			  "_wrap_on_data");
		return 0;
	}

	if (!con->read_eof) {
		/* must wait until poll allows read from this socket */
		if (con->is_listen)
			log_flag(NET, "%s: [%s] waiting for new connection",
				 __func__, con->name);
		else
			log_flag(NET, "%s: [%s] waiting to read pending_read=%u pending_write=%u work_active=%c",
				 __func__, con->name, get_buf_offset(con->in),
				 get_buf_offset(con->out),
				 (con->work_active ? 'T' : 'F'));
		return 0;
	}

	/*
	 * Close out the incoming to avoid any new work coming into the
	 * connection.
	 */
	if (con->input_fd != -1) {
		log_flag(NET, "%s: [%s] closing incoming on connection input_fd=%d",
			 __func__, con->name, con->input_fd);

		if (close(con->input_fd) == -1)
			log_flag(NET, "%s: [%s] unable to close input fd %d: %m",
				 __func__, con->name, con->input_fd);

		/* if there is only 1 fd: forget it too */
		if (con->input_fd == con->output_fd)
			con->output_fd = -1;

		/* forget invalid fd */
		con->input_fd = -1;
	}

	if (con->wait_on_finish) {
		log_flag(NET, "%s: [%s] waiting for on_finish()",
			 __func__, con->name);
		return 0;
	}

	if (!con->is_listen && con->arg) {
		log_flag(NET, "%s: [%s] queuing up on_finish",
			 __func__, con->name);

		con->wait_on_finish = true;

		/* notify caller of closing */
		_add_work(true, mgr, con, _on_finish_wrapper,
			  CONMGR_WORK_TYPE_CONNECTION_FIFO, con->arg,
			  "on_finish");

		return 0;
	}

	if (!list_is_empty(con->work) || !list_is_empty(con->write_complete_work)) {
		log_flag(NET, "%s: [%s] outstanding work for connection output_fd=%d work=%u write_complete_work=%u",
			 __func__, con->name, con->output_fd,
			 list_count(con->work),
			 list_count(con->write_complete_work));

		/*
		 * Must finish all outstanding work before deletion.
		 * Work must have been added by on_finish()
		 */
		return 0;
	}

	/*
	 * This connection has no more pending work or possible IO:
	 * Remove the connection and close everything.
	 */
	log_flag(NET, "%s: [%s] closing connection input_fd=%d output_fd=%d",
		 __func__, con->name, con->input_fd, con->output_fd);

	if (con->output_fd != -1) {
		if (close(con->output_fd) == -1)
			log_flag(NET, "%s: [%s] unable to close output fd %d: %m",
				 __func__, con->name, con->output_fd);

		con->output_fd = -1;
	}

	log_flag(NET, "%s: [%s] closed connection", __func__, con->name);

	/* mark this connection for cleanup */
	return 1;
}

/*
 * Close all connections (for_each)
 * NOTE: must hold mgr->mutex
 */
static int _close_con_for_each(void *x, void *arg)
{
	con_mgr_fd_t *con = x;
	_close_con(true, con);
	return 1;
}

/*
 * Inspect all connection states and apply actions required
 */
static void _inspect_connections(void *x)
{
	con_mgr_t *mgr = x;
	xassert(mgr->magic == MAGIC_CON_MGR);

	slurm_mutex_lock(&mgr->mutex);

	if (list_transfer_match(mgr->connections, mgr->complete,
				_handle_connection, NULL))
		slurm_cond_broadcast(&mgr->cond);
	mgr->inspecting = false;

	slurm_mutex_unlock(&mgr->mutex);
}

/*
 * Event on a listen only socket
 * mgr must be locked.
 */
static void _handle_listen_event(con_mgr_t *mgr, int fd, con_mgr_fd_t *con,
				 short revents)
{
	if (revents & POLLHUP) {
		/* how can a listening socket hang up? */
		error("%s: [%s] listen received POLLHUP", __func__, con->name);
	} else if (revents & POLLNVAL) {
		error("%s: [%s] listen connection invalid",
		      __func__, con->name);
	} else if (revents & POLLERR) {
		int err = SLURM_ERROR;
		int rc;
		if ((rc = fd_get_socket_error(con->input_fd, &err))) {
			error("%s: [%s] listen poll error: %s fd_get_socket_error failed:",
			      __func__, con->name, slurm_strerror(rc));
		} else {
			error("%s: [%s] listen poll error: %s",
			      __func__, con->name, slurm_strerror(err));
		}
	} else if (revents & POLLIN) {
		log_flag(NET, "%s: [%s] listen has incoming connection",
			 __func__, con->name);
		_add_work(true, mgr, con, _listen_accept,
			  CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
			  "_listen_accept");
		return;
	} else /* should never happen */
		log_flag(NET, "%s: [%s] listen unexpected revents: 0x%04x",
			 __func__, con->name, revents);

	_close_con(true, con);
}

static void _handle_event_pipe(con_mgr_t *mgr, const struct pollfd *fds_ptr,
			       const char *tag, const char *name)
{
	if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
		char *flags = poll_revents_to_str(fds_ptr->revents);

		log_flag(NET, "%s: [%s] signal pipe %s flags:%s",
			 __func__, tag, name, flags);

		/* _watch() will actually read the input */

		xfree(flags);
	}
}

static int _read_signal(con_mgr_t *mgr)
{
	int sig;

#ifdef FIONREAD
	int readable;

	/* request kernel tell us the size of the incoming buffer */
	if (ioctl(mgr->signal_fd[0], FIONREAD, &readable))
		log_flag(NET, "%s: [fd:%d] unable to call FIONREAD: %m",
			 __func__, mgr->signal_fd[0]);

	if (!readable) {
		/* Didn't fail but buffer is empty so no more signals */
		return -1;
	} else if (readable < sizeof(sig)) {
		/* write() must have not completed */
		return -1;
	}
#endif /* FIONREAD */

	safe_read(mgr->signal_fd[0], &sig, sizeof(sig));

	return sig;
rwfail:
	if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
		return -1;

	fatal("%s: unable to read(signal_fd[0]=%d): %m",
	      __func__, mgr->signal_fd[0]);
}

static void _handle_signals(con_mgr_t *mgr)
{
	bool caught_sigalrm = false;
	bool caught_sigint = false;
	int sig, count = 0;

	while ((sig = _read_signal(mgr)) > 0) {
		count++;
		if (sig == SIGINT) {
			caught_sigint = true;
		} else if (sig == SIGALRM) {
			caught_sigalrm = true;
		} else {
			info("%s: caught and ignoring signal %s",
			     __func__, strsignal(sig));
		}
	}

	log_flag(NET, "%s: caught %d signals", __func__, count);
	mgr->signaled = false;

	if (caught_sigint) {
		if (!mgr->shutdown)
			info("%s: caught SIGINT. Shutting down.",
			     __func__);
		mgr->shutdown = true;
	}

	if (caught_sigalrm) {
		log_flag(NET, "%s: caught SIGALRM", __func__);
		_queue_func(true, mgr, _handle_timer, mgr, "_handle_timer");
	}

	if (caught_sigint || caught_sigalrm)
		_signal_change(mgr, true);
}

/*
 * Handle poll and events
 *
 * NOTE: mgr mutex must not be locked but will be locked upon return
 */
static void _poll(con_mgr_t *mgr, poll_args_t *args, list_t *fds,
		  on_poll_event_t on_poll, const char *tag)
{
	int rc = SLURM_SUCCESS;
	struct pollfd *fds_ptr = NULL;
	con_mgr_fd_t *con;
	int signal_fd, event_fd;

again:
	rc = poll(args->fds, args->nfds, -1);
	if (rc == -1) {
		bool exit_on_error;

		slurm_mutex_lock(&mgr->mutex);
		exit_on_error = mgr->exit_on_error;
		slurm_mutex_unlock(&mgr->mutex);

		if ((errno == EINTR) && !exit_on_error) {
			log_flag(NET, "%s: [%s] poll interrupted. Trying again.",
				 __func__, tag);
			goto again;
		}

		fatal("%s: [%s] unable to poll listening sockets: %m",
		      __func__, tag);
	}

	if (rc == 0) {
		log_flag(NET, "%s: [%s] poll timed out", __func__, tag);
		return;
	}

	slurm_mutex_lock(&mgr->mutex);
	signal_fd = mgr->signal_fd[0];
	event_fd = mgr->event_fd[0];
	slurm_mutex_unlock(&mgr->mutex);

	fds_ptr = args->fds;
	for (int i = 0; i < args->nfds; i++, fds_ptr++) {

		if (!fds_ptr->revents)
			continue;

		if (fds_ptr->fd == signal_fd) {
			mgr->signaled = true;
			_handle_event_pipe(mgr, fds_ptr, tag, "CAUGHT_SIGNAL");
		} else if (fds_ptr->fd == event_fd)
			_handle_event_pipe(mgr, fds_ptr, tag, "CHANGE_EVENT");
		else if ((con = list_find_first(fds, _find_by_fd,
						&fds_ptr->fd))) {
			if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
				char *flags = poll_revents_to_str(
					fds_ptr->revents);
				log_flag(NET, "%s: [%s->%s] poll event detect flags:%s",
					 __func__, tag, con->name, flags);
				xfree(flags);
			}
			slurm_mutex_lock(&mgr->mutex);
			on_poll(mgr, fds_ptr->fd, con, fds_ptr->revents);
			/*
			 * signal that something might have happened and to
			 * restart listening
			 * */
			_signal_change(mgr, true);
			slurm_mutex_unlock(&mgr->mutex);
		} else
			/* FD probably got closed between poll start and now */
			log_flag(NET, "%s: [%s] unable to find connection for fd=%u",
				 __func__, tag, fds_ptr->fd);
	}
}

/*
 * Poll all processing connections sockets and
 * signal_fd and event_fd.
 */
static void _poll_connections(void *x)
{
	poll_args_t *args = x;
	con_mgr_t *mgr = args->mgr;
	struct pollfd *fds_ptr = NULL;
	con_mgr_fd_t *con;
	int count;
	list_itr_t *itr;

	xassert(mgr->magic == MAGIC_CON_MGR);

	slurm_mutex_lock(&mgr->mutex);

	/* grab counts once */
	if (!(count = list_count(mgr->connections))) {
		log_flag(NET, "%s: no connections to poll()", __func__);
		goto done;
	}

	if (mgr->signaled) {
		log_flag(NET, "%s: skipping poll() due to signal", __func__);
		goto done;
	}

	fds_ptr = args->fds;

	xrecalloc(args->fds, ((count * 2) + 2), sizeof(*args->fds));

	args->nfds = 0;
	fds_ptr = args->fds;

	/* Add signal fd */
	fds_ptr->fd = mgr->signal_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	/* Add event fd */
	fds_ptr->fd = mgr->event_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	/*
	 * populate sockets with !work_active
	 */
	itr = list_iterator_create(mgr->connections);
	while ((con = list_next(itr))) {
		if (con->work_active)
			continue;

		log_flag(NET, "%s: [%s] poll read_eof=%s input=%u output=%u work_active=%c",
			 __func__, con->name, (con->read_eof ? "T" : "F"),
			 get_buf_offset(con->in), get_buf_offset(con->out),
			 (con->work_active ? 'T' : 'F'));

		if (con->input_fd == con->output_fd) {
			/* if fd is same, only poll it */
			fds_ptr->fd = con->input_fd;
			fds_ptr->events = 0;

			if (con->input_fd != -1)
				fds_ptr->events |= POLLIN;
			if (get_buf_offset(con->out))
				fds_ptr->events |= POLLOUT;

			fds_ptr++;
			args->nfds++;
		} else {
			/*
			 * Account for fd being different
			 * for input and output.
			 */
			if (con->input_fd != -1) {
				fds_ptr->fd = con->input_fd;
				fds_ptr->events = POLLIN;
				fds_ptr++;
				args->nfds++;
			}

			if (get_buf_offset(con->out)) {
				fds_ptr->fd = con->output_fd;
				fds_ptr->events = POLLOUT;
				fds_ptr++;
				args->nfds++;
			}
		}
	}
	list_iterator_destroy(itr);

	if (args->nfds == 2) {
		log_flag(NET, "%s: skipping poll() due to no open file descriptors for %d connections",
			 __func__, count);
		goto done;
	}

	slurm_mutex_unlock(&mgr->mutex);

	log_flag(NET, "%s: polling %u file descriptors for %u connections",
		 __func__, args->nfds, count);

	_poll(mgr, args, mgr->connections, _handle_poll_event, __func__);

	slurm_mutex_lock(&mgr->mutex);
done:
	mgr->poll_active = false;
	/* notify _watch it can run but don't send signal to event PIPE*/
	slurm_cond_broadcast(&mgr->cond);
	slurm_mutex_unlock(&mgr->mutex);

	log_flag(NET, "%s: poll done", __func__);
}

/*
 * Poll all listening sockets
 */
static void _listen(void *x)
{
	poll_args_t *args = x;
	con_mgr_t *mgr = args->mgr;
	struct pollfd *fds_ptr = NULL;
	con_mgr_fd_t *con;
	int count;
	list_itr_t *itr;

	xassert(mgr->magic == MAGIC_CON_MGR);

	slurm_mutex_lock(&mgr->mutex);

	/* if shutdown has been requested: then don't listen() anymore */
	if (mgr->shutdown) {
		log_flag(NET, "%s: caught shutdown. closing %u listeners",
			 __func__, list_count(mgr->listen));
		goto cleanup;
	}

	if (mgr->signaled) {
		log_flag(NET, "%s: skipping poll() to pending signal",
			 __func__);
		goto cleanup;
	}

	/* grab counts once */
	count = list_count(mgr->listen);

	log_flag(NET, "%s: listeners=%u", __func__, count);

	if (count == 0) {
		/* nothing to do here */
		log_flag(NET, "%s: no listeners found", __func__);
		goto cleanup;
	}

	xrecalloc(args->fds, (count + 2), sizeof(*args->fds));
	fds_ptr = args->fds;
	args->nfds = 0;

	/* Add signal fd */
	fds_ptr->fd = mgr->signal_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	/* Add event fd */
	fds_ptr->fd = mgr->event_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	/* populate listening sockets */
	itr = list_iterator_create(mgr->listen);
	while ((con = list_next(itr))) {
		/* already accept queued or listener already closed */
		if (con->work_active || con->read_eof)
			continue;

		fds_ptr->fd = con->input_fd;
		fds_ptr->events = POLLIN;

		log_flag(NET, "%s: [%s] listening", __func__, con->name);

		fds_ptr++;
		args->nfds++;
	}
	list_iterator_destroy(itr);

	if (args->nfds == 2) {
		log_flag(NET, "%s: deferring listen due to all sockets are queued to call accept or closed",
			 __func__);
		goto cleanup;
	}

	slurm_mutex_unlock(&mgr->mutex);

	log_flag(NET, "%s: polling %u/%u file descriptors",
		 __func__, args->nfds, (count + 2));

	/* _poll() will lock mgr->mutex */
	_poll(mgr, args, mgr->listen, _handle_listen_event, __func__);

	slurm_mutex_lock(&mgr->mutex);
cleanup:
	mgr->listen_active = false;
	_signal_change(mgr, true);
	slurm_mutex_unlock(&mgr->mutex);
}

/*
 * Poll all sockets non-listen connections
 */
static int _watch(con_mgr_t *mgr)
{
	poll_args_t *listen_args = NULL;
	poll_args_t *poll_args = NULL;
	int count, event_read;
	char buf[100]; /* buffer for event_read */
	bool work; /* is there any work to do? */

	xassert(mgr->magic == MAGIC_CON_MGR);
	slurm_mutex_lock(&mgr->mutex);
watch:
	if (mgr->shutdown)
		_close_all_connections(true, mgr);

	/* grab counts once */
	count = list_count(mgr->connections);

	log_flag(NET, "%s: starting connections=%u listen=%u",
		 __func__, count, list_count(mgr->listen));

	if (!mgr->poll_active && !mgr->listen_active) {
		/* only clear signal and event pipes once both polls are done */
		event_read = read(mgr->event_fd[0], buf, sizeof(buf));
		if (event_read > 0) {
			log_flag(NET, "%s: detected %u events from event fd",
				 __func__, event_read);
			mgr->event_signaled = 0;
		} else if (event_read == 0)
			log_flag(NET, "%s: nothing to read from event fd", __func__);
		else if (errno == EAGAIN || errno == EWOULDBLOCK ||
			 errno == EINTR)
			log_flag(NET, "%s: try again on read of event fd: %m",
				 __func__);
		else
			fatal("%s: unable to read from event fd: %m", __func__);

		if (mgr->signaled) {
			_handle_signals(mgr);
			goto watch;
		}
	}

	work = false;

	/* start listen thread if needed */
	if (!list_is_empty(mgr->listen)) {
		if (!listen_args) {
			listen_args = xmalloc(sizeof(*listen_args));
			listen_args->mgr = mgr;
		}

		/* run any queued work */
		list_transfer_match(mgr->listen, mgr->complete,
				    _handle_connection, NULL);

		if (!mgr->listen_active) {
			/* only try to listen if number connections is below limit */
			if (count >= MAX_OPEN_CONNECTIONS)
				log_flag(NET, "%s: deferring accepting new connections until count is below max: %u/%u",
					 __func__, count, MAX_OPEN_CONNECTIONS);
			else { /* request a listen thread to run */
				log_flag(NET, "%s: queuing up listen", __func__);
				mgr->listen_active = true;
				_queue_func(true, mgr, _listen, listen_args,
					    "_listen");
			}
		} else
			log_flag(NET, "%s: listeners active already", __func__);

		work = true;
	}

	/* start poll thread if needed */
	if (count) {
		if (!poll_args) {
			poll_args = xmalloc(sizeof(*poll_args));
			poll_args->mgr = mgr;
		}

		if (!mgr->inspecting) {
			mgr->inspecting = true;
			_queue_func(true, mgr, _inspect_connections, mgr,
				    "_inspect_connections");
		}

		if (!mgr->poll_active) {
			/* request a listen thread to run */
			log_flag(NET, "%s: queuing up poll", __func__);
			mgr->poll_active = true;
			_queue_func(true, mgr, _poll_connections, poll_args,
				    "_poll_connections");
		} else
			log_flag(NET, "%s: poll active already", __func__);

		work = true;
	}

	if (!list_is_empty(mgr->complete)) {
		con_mgr_fd_t *con;

		while ((con = list_pop(mgr->complete)))
			_queue_func(true, mgr, _connection_fd_delete, con,
				    "_connection_fd_delete");
	}

	if (work) {
		/* wait until something happens */
		slurm_cond_wait(&mgr->cond, &mgr->mutex);
		goto watch;
	}

	_signal_change(mgr, true);
	slurm_mutex_unlock(&mgr->mutex);

	mgr->shutdown = true;
	log_flag(NET, "%s: cleaning up", __func__);

	log_flag(NET, "%s: begin waiting for all workers", __func__);
	/* _watch() is never in the workq so it can wait */
	quiesce_workq(mgr->workq);
	log_flag(NET, "%s: end waiting for all workers", __func__);

	if (poll_args) {
		xfree(poll_args->fds);
		xfree(poll_args);
	}

	if (listen_args) {
		xfree(listen_args->fds);
		xfree(listen_args);
	}

	return SLURM_SUCCESS;
}

extern int con_mgr_run(con_mgr_t *mgr)
{
	int rc = SLURM_SUCCESS;

	xassert(mgr->magic == MAGIC_CON_MGR);

	slurm_mutex_lock(&mgr->mutex);
	slurm_mutex_lock(&signal_mutex);
	//TODO: allow for multiple conmgrs to run at once
	signal_fd[0] = mgr->signal_fd[0];
	signal_fd[1] = mgr->signal_fd[1];

	for (int i = 0; i < ARRAY_SIZE(catch_signals); i++) {
		if (sigaction(catch_signals[i].signal, &catch_signals[i].new,
			      &catch_signals[i].prior))
			fatal("%s: unable to catch %s: %m",
			      __func__, strsignal(catch_signals[i].signal));
	}
	slurm_mutex_unlock(&signal_mutex);

	if (mgr->deferred_funcs) {
		list_t *deferred_funcs = NULL;
		deferred_func_t *df;

		SWAP(deferred_funcs, mgr->deferred_funcs);
		while ((df = list_pop(deferred_funcs))) {
			_queue_func(true, mgr, df->func, df->arg, df->tag);
			df->magic = ~MAGIC_DEFERRED_FUNC;
			xfree(df);
		}
		FREE_NULL_LIST(deferred_funcs);
	}
	slurm_mutex_unlock(&mgr->mutex);

	rc = _watch(mgr);

	slurm_mutex_lock(&mgr->mutex);
	slurm_mutex_lock(&signal_mutex);
	for (int i = 0; i < ARRAY_SIZE(catch_signals); i++) {
		if (sigaction(catch_signals[i].signal, &catch_signals[i].prior,
			      NULL))
			fatal("%s: unable to restore %s: %m",
			      __func__, strsignal(catch_signals[i].signal));
	}

	signal_fd[0] = -1;
	signal_fd[1] = -1;
	slurm_mutex_unlock(&signal_mutex);
	slurm_mutex_unlock(&mgr->mutex);

	return rc;
}

/*
 * listen socket is ready to accept
 */
static void _listen_accept(con_mgr_t *mgr, con_mgr_fd_t *con,
			   con_mgr_work_type_t type,
			   con_mgr_work_status_t status, const char *tag,
			   void *arg)
{
	int rc;
	slurm_addr_t addr = {0};
	socklen_t addrlen = sizeof(addr);
	int fd;

	if (con->input_fd == -1) {
		log_flag(NET, "%s: [%s] skipping accept on closed connection",
			 __func__, con->name);
		return;
	} else
		log_flag(NET, "%s: [%s] attempting to accept new connection",
			 __func__, con->name);

	/* try to get the new file descriptor and retry on errors */
	if ((fd = accept4(con->input_fd, (struct sockaddr *) &addr,
			  &addrlen, SOCK_CLOEXEC)) < 0) {
		if (errno == EINTR) {
			log_flag(NET, "%s: [%s] interrupt on accept()",
				 __func__, con->name);
			_close_con(false, con);
			return;
		}
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			log_flag(NET, "%s: [%s] retry: %m", __func__, con->name);
			rc = SLURM_SUCCESS;
			return;
		}

		error("%s: [%s] Error on accept socket: %m",
		      __func__, con->name);

		if ((errno == EMFILE) || (errno == ENFILE) ||
		    (errno == ENOBUFS) || (errno == ENOMEM)) {
			error("%s: [%s] retry on error: %m",
			      __func__, con->name);
			return;
		}

		/* socket is likely dead: fail out */
		_close_con(false, con);
		return;
	}

	if (addrlen <= 0)
		fatal("%s: empty address returned from accept()",
		      __func__);
	if (addrlen > sizeof(addr))
		fatal("%s: unexpected large address returned from accept(): %u bytes",
		      __func__, addrlen);

	/* hand over FD for normal processing */
	if ((rc = _con_mgr_process_fd_internal(mgr, con->type, con, fd, fd,
					       con->events, &addr, addrlen,
					       con->new_arg))) {
		log_flag(NET, "%s: [fd:%d] _con_mgr_process_fd_internal rejected: %s",
			 __func__, fd, slurm_strerror(rc));
		_close_con(false, con);
	}
}

static void _deferred_write_fd(con_mgr_t *mgr, con_mgr_fd_t *con,
			       con_mgr_work_type_t type,
			       con_mgr_work_status_t status, const char *tag,
			       void *arg)
{
	/*
	 * make sure to trigger a write as the deferred buffers will get
	 * written first before anything else to maintain order.
	 */

	(void) con_mgr_queue_write_fd(con, NULL, 0);
}

static int _for_each_deferred_write(void *x, void *arg)
{
	buf_t *buf = x;
	con_mgr_fd_t *con = arg;
	xassert(con->magic == MAGIC_CON_MGR_FD);

	(void) con_mgr_queue_write_fd(con, get_buf_data(buf),
				  get_buf_offset(buf));

	return SLURM_SUCCESS;
}

extern int con_mgr_queue_write_fd(con_mgr_fd_t *con, const void *buffer,
				  const size_t bytes)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);

	if (list_count(con->deferred_out)) {
		/* handle deferred first */
		list_t *deferred = list_create((ListDelF) free_buf);
		list_transfer(deferred, con->deferred_out);

		(void) list_for_each_ro(deferred, _for_each_deferred_write,
					con);

		FREE_NULL_LIST(deferred);
	}

	if (!bytes) {
		log_flag(NET, "%s: [%s] write 0 bytes ignored",
			 __func__, con->name);
		return SLURM_SUCCESS;
	}

	if (con->work_active) {
		/* Grow buffer as needed to handle the outgoing data */
		if (remaining_buf(con->out) < bytes) {
			int need = bytes - remaining_buf(con->out);

			if ((need + size_buf(con->out)) >= MAX_BUF_SIZE) {
				error("%s: [%s] out of buffer space.",
				      __func__, con->name);

				return SLURM_ERROR;
			}

			grow_buf(con->out, need);
		}

		memmove((get_buf_data(con->out) + get_buf_offset(con->out)), buffer,
			bytes);

		log_flag_hex(NET_RAW,
			     (get_buf_data(con->out) + get_buf_offset(con->out)), bytes,
			     "%s: queued up write", __func__);

		con->out->processed += bytes;

		log_flag(NET, "%s: [%s] queued %zu/%u bytes in outgoing buffer",
			 __func__, con->name, bytes, get_buf_offset(con->out));
	} else {
		/* we must ensure that all deferred writes maintain
		 * their order or rpcs may get sliced.
		 */
		buf_t *buf = init_buf(bytes);

		/* TODO: would be nice to avoid this copy */
		memmove(get_buf_data(buf), buffer, bytes);
		set_buf_offset(buf, bytes);

		log_flag(NET, "%s: [%s] deferred write of %zu bytes queued",
			 __func__, con->name, bytes);

		log_flag_hex(NET_RAW, get_buf_data(buf), get_buf_offset(buf),
			     "%s: queuing up deferred write", __func__);

		list_append(con->deferred_out, buf);

		_add_work(false, con->mgr, con, _deferred_write_fd,
			  CONMGR_WORK_TYPE_CONNECTION_FIFO, NULL, __func__);
	}

	_signal_change(con->mgr, false);
	return SLURM_SUCCESS;
}

/*
 * based on _pack_msg() and slurm_send_node_msg() in slurm_protocol_api.c
 */
extern int con_mgr_queue_write_msg(con_mgr_fd_t *con, slurm_msg_t *msg)
{
	int rc;
	msg_bufs_t buffers = {0};
	uint32_t msglen = 0;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	if ((rc = slurm_buffers_pack_msg(msg, &buffers, false)))
		goto cleanup;

	msglen = get_buf_offset(buffers.auth) + get_buf_offset(buffers.body) +
		get_buf_offset(buffers.header);

	/* switch to network order */
	msglen = htonl(msglen);

	//TODO: handing over the buffers would be better than copying

	if ((rc = con_mgr_queue_write_fd(con, &msglen, sizeof(msglen))))
		goto cleanup;

	if ((rc = con_mgr_queue_write_fd(con, get_buf_data(buffers.header),
					 get_buf_offset(buffers.header))))
		goto cleanup;

	if ((rc = con_mgr_queue_write_fd(con, get_buf_data(buffers.auth),
					 get_buf_offset(buffers.auth))))
		goto cleanup;

	rc = con_mgr_queue_write_fd(con, get_buf_data(buffers.body),
				    get_buf_offset(buffers.body));
cleanup:
	if (!rc) {
		log_flag(PROTOCOL, "%s: [%s] sending RPC %s",
			 __func__, con->name, rpc_num2string(msg->msg_type));
		log_flag(NET, "%s: [%s] sending RPC %s packed into %u bytes",
			 __func__, con->name, rpc_num2string(msg->msg_type),
			 ntohl(msglen));
		log_flag_hex(NET_RAW, get_buf_data(con->out),
			     get_buf_offset(con->out),
			     "%s: [%s] sending RPC %s", __func__, con->name,
			     rpc_num2string(msg->msg_type));
	} else {
		log_flag(NET, "%s: [%s] error packing RPC %s: %s",
			 __func__, con->name, rpc_num2string(msg->msg_type),
			 slurm_strerror(rc));
	}

	FREE_NULL_BUFFER(buffers.auth);
	FREE_NULL_BUFFER(buffers.body);
	FREE_NULL_BUFFER(buffers.header);

	return rc;
}

static void _deferred_close_fd(con_mgr_t *mgr, con_mgr_fd_t *con,
			       con_mgr_work_type_t type,
			       con_mgr_work_status_t status,
			       const char *tag, void *arg)
{
	slurm_mutex_lock(&mgr->mutex);
	if (con->work_active) {
		slurm_mutex_unlock(&mgr->mutex);
		con_mgr_queue_close_fd(con);
	} else {
		_close_con(true, con);
		slurm_mutex_unlock(&mgr->mutex);
	}
}

extern void con_mgr_queue_close_fd(con_mgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);

	slurm_mutex_lock(&con->mgr->mutex);
	if (!con->work_active) {
		/*
		 * Defer request to close connection until connection is no
		 * longer actively doing work as closing connection would change
		 * several variables guarenteed to not change while work is
		 * active.
		 */
		_add_work(true, con->mgr, con, _deferred_close_fd,
			  CONMGR_WORK_TYPE_CONNECTION_FIFO, NULL, __func__);
	} else {
		_close_con(true, con);
	}
	slurm_mutex_unlock(&con->mgr->mutex);
}

static int _create_socket(void *x, void *arg)
{
	static const char UNIX_PREFIX[] = "unix:";
	const char *hostport = (const char *)x;
	const char *unixsock = xstrstr(hostport, UNIX_PREFIX);
	socket_listen_init_t *init = arg;
	int rc;
	struct addrinfo hints = { .ai_family = AF_UNSPEC,
				  .ai_socktype = SOCK_STREAM,
				  .ai_protocol = 0,
				  .ai_flags = AI_PASSIVE | AI_ADDRCONFIG };
	struct addrinfo *addrlist = NULL;
	parsed_host_port_t *parsed_hp;
	con_mgr_callbacks_t callbacks;

	slurm_mutex_lock(&init->mgr->mutex);
	callbacks = init->mgr->callbacks;
	slurm_mutex_unlock(&init->mgr->mutex);

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

		return con_mgr_process_fd_unix_listen(
			init->mgr, init->type, fd, init->events,
			(const slurm_addr_t *)&addr, sizeof(addr), unixsock,
			init->arg);
	} else {
		/* split up host and port */
		if (!(parsed_hp = callbacks.parse(hostport)))
			fatal("%s: Unable to parse %s", __func__, hostport);

		/* resolve out the host and port if provided */
		rc = getaddrinfo(parsed_hp->host, parsed_hp->port, &hints,
				 &addrlist);
		if (rc) {
			if (rc == EAI_SYSTEM) /* error held in errno */
				fatal("%s: Unable to parse %s due to system issue: %m",
				      __func__, hostport);
			else
				fatal("%s: Unable to parse %s: %s",
				      __func__, hostport, gai_strerror(rc));
		}
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

		rc = con_mgr_process_fd_listen(
			init->mgr, fd, init->type, init->events,
			(const slurm_addr_t *)addr->ai_addr, addr->ai_addrlen,
			init->arg);
	}

	freeaddrinfo(addrlist);
	callbacks.free_parse(parsed_hp);

	return rc;
}

extern int con_mgr_create_sockets(con_mgr_t *mgr, con_mgr_con_type_t type,
				  list_t *hostports, con_mgr_events_t events,
				  void *arg)
{
	int rc;
	socket_listen_init_t *init = xmalloc(sizeof(*init));
	init->events = events;
	init->mgr = mgr;
	init->arg = arg;
	init->type = type;

	if (list_for_each(hostports, _create_socket, init) > 0)
		rc = SLURM_SUCCESS;
	else
		rc = SLURM_ERROR;

	xfree(init);

	return rc;
}

extern void con_mgr_request_shutdown(con_mgr_t *mgr)
{
	xassert(mgr->magic == MAGIC_CON_MGR);

	log_flag(NET, "%s: shutdown requested", __func__);

	slurm_mutex_lock(&mgr->mutex);
	mgr->shutdown = true;
	_signal_change(mgr, true);
	slurm_mutex_unlock(&mgr->mutex);
}

static void _cancel_delayed_work(bool locked, con_mgr_t *mgr)
{
	xassert(mgr->magic == MAGIC_CON_MGR);

	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

	if (mgr->delayed_work && !list_is_empty(mgr->delayed_work)) {
		work_t *work;

		log_flag(NET, "%s: cancelling %d delayed work",
			 __func__, list_count(mgr->delayed_work));

		/* run everything immediately but with cancelled status */
		while ((work = list_pop(mgr->delayed_work))) {
			work->status = CONMGR_WORK_STATUS_CANCELLED;
			_handle_work(true, work);
		}
	}

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);
}

static void _update_last_time(bool locked, con_mgr_t *mgr)
{
	int rc;

	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

	if (!mgr->delayed_work) {
		struct sigevent sevp = {
			.sigev_notify = SIGEV_SIGNAL,
			.sigev_signo = SIGALRM,
			.sigev_value.sival_ptr = &mgr->timer,
		};

		mgr->delayed_work = list_create(xfree_ptr);

again:
		if ((rc = timer_create(CLOCK_MONOTONIC, &sevp, &mgr->timer))) {
			if ((rc == -1) && errno)
				rc = errno;

			if (rc == EAGAIN)
				goto again;
			else if (rc)
				fatal("%s: timer_create() failed: %s",
				      __func__, slurm_strerror(rc));
		}
	}

	if ((rc = clock_gettime(CLOCK_MONOTONIC, &mgr->last_time))) {
		if (rc == -1)
			rc = errno;

		fatal("%s: clock_gettime() failed: %s",
		      __func__, slurm_strerror(rc));
	}

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);
}

static int _foreach_delayed_work(void *x, void *arg)
{
	work_t *work = x;
	foreach_delayed_work_t *args = arg;
	con_mgr_t *mgr = args->mgr;

	xassert(args->magic == MAGIC_FOREACH_DELAYED_WORK);
	xassert(work->magic == MAGIC_WORK);

	if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
		int64_t remain_sec, remain_nsec;

		remain_sec = work->begin.seconds - mgr->last_time.tv_sec;
		if (remain_sec == 0) {
			remain_nsec = work->begin.nanoseconds - mgr->last_time.tv_nsec;
		} else if (remain_sec < 0) {
			remain_nsec = NO_VAL64;
		} else {
			remain_nsec = NO_VAL64;
		}

		log_flag(NET, "%s: evaluating delayed work ETA %"PRId64"s %"PRId64"ns for %s@0x%"PRIxPTR,
			 __func__, remain_sec,
			 (remain_nsec == NO_VAL64 ? 0 : remain_nsec),
			 work->tag, (uintptr_t) work->func);
	}

	if (!args->shortest) {
		args->shortest = work;
		return SLURM_SUCCESS;
	}

	if (args->shortest->begin.seconds == work->begin.seconds) {
		if (args->shortest->begin.nanoseconds > work->begin.nanoseconds)
			args->shortest = work;
	} else if (args->shortest->begin.seconds > work->begin.seconds) {
		args->shortest = work;
	}

	return SLURM_SUCCESS;
}

static void _update_timer(bool locked, con_mgr_t *mgr)
{
	int rc;
	struct itimerspec spec = {{0}};

	foreach_delayed_work_t args = {
		.magic = MAGIC_FOREACH_DELAYED_WORK,
		.mgr = mgr,
	};

	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

	if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
		/* get updated clock for logging but not needed otherwise */
		_update_last_time(true, mgr);
	}

	list_for_each(mgr->delayed_work, _foreach_delayed_work, &args);

	if (args.shortest) {
		work_t *work = args.shortest;

		spec.it_value.tv_sec = work->begin.seconds;
		spec.it_value.tv_nsec = work->begin.nanoseconds;

		if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
			int64_t remain_sec, remain_nsec;

			remain_sec = work->begin.seconds - mgr->last_time.tv_sec;
			if (remain_sec == 0) {
				remain_nsec = work->begin.nanoseconds -
					mgr->last_time.tv_nsec;
			} else if (remain_sec < 0) {
				remain_nsec = NO_VAL64;
			} else {
				remain_nsec = NO_VAL64;
			}

			log_flag(NET, "%s: setting conmgr timer for %"PRId64"s %"PRId64"ns for %s@0x%"PRIxPTR,
				 __func__, remain_sec,
				 (remain_nsec == NO_VAL64 ? 0 : remain_nsec),
				 work->tag, (uintptr_t) work->func);
		}
	} else {
		log_flag(NET, "%s: disabling conmgr timer", __func__);
	}

	if ((rc = timer_settime(mgr->timer, TIMER_ABSTIME, &spec, NULL))) {
		if ((rc == -1) && errno)
			rc = errno;
	}

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);
}

/* check begin times to see if the work delay has elapsed */
static int _match_work_elapsed(void *x, void *key)
{
	bool trigger;
	work_t *work = x;
	con_mgr_t *mgr = key;
	int64_t remain_sec, remain_nsec;

	xassert(work->magic == MAGIC_WORK);
	xassert(mgr->magic == MAGIC_CON_MGR);

	remain_sec = work->begin.seconds - mgr->last_time.tv_sec;
	if (remain_sec == 0) {
		remain_nsec = work->begin.nanoseconds - mgr->last_time.tv_nsec;
		trigger = (remain_nsec <= 0);
	} else if (remain_sec < 0) {
		trigger = true;
		remain_nsec = NO_VAL64;
	} else {
		remain_nsec = NO_VAL64;
		trigger = false;
	}

	log_flag(NET, "%s: %s %s@0x%"PRIxPTR" ETA in %"PRId64"s %"PRId64"ns",
		 __func__, (trigger ? "triggering" : "deferring"),
		 work->tag, (uintptr_t) work->func,
		 remain_sec, (remain_nsec == NO_VAL64 ? 0 : remain_nsec));

	return trigger ? 1 : 0;
}

static void _handle_timer(void *x)
{
	int count, total;
	con_mgr_t *mgr = x;
	work_t *work;
	list_t *elapsed = list_create(xfree_ptr);

	slurm_mutex_lock(&mgr->mutex);
	_update_last_time(true, mgr);

	total = list_count(mgr->delayed_work);
	count = list_transfer_match(mgr->delayed_work, elapsed,
				    _match_work_elapsed, mgr);

	_update_timer(true, mgr);

	while ((work = list_pop(elapsed))) {
		work->status = CONMGR_WORK_STATUS_RUN;
		_handle_work(true, work);
	}

	if (count > 0)
		_signal_change(mgr, true);
	slurm_mutex_unlock(&mgr->mutex);

	log_flag(NET, "%s: checked all timers and triggered %d/%d delayed work",
		 __func__, count, total);

	FREE_NULL_LIST(elapsed);
}

/* Single point to queue internal function callback via mgr->workq. */
static void _queue_func(bool locked, con_mgr_t *mgr, work_func_t func,
			void *arg, const char *tag)
{
	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

	if (!mgr->deferred_funcs) {
		/* this should never fail here */
		workq_add_work(mgr->workq, func, arg, tag);
	} else {
		/*
		 * Defer all funcs until con_mgr_run() as adding new connections
		 * will call _queue_func() including on_connection() callback
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
		list_append(mgr->deferred_funcs, df);
	}

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);
}

/* mgr must be locked */
static void _handle_work_run(work_t *work)
{
	_queue_func(true, work->mgr, _wrap_work, work, work->tag);
}

/* mgr must be locked */
static void _handle_work_pending(work_t *work)
{
	con_mgr_t *mgr = work->mgr;
	con_mgr_fd_t *con = work->con;

	switch (work->type) {
	case CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO:
		if (!work->con)
			fatal_abort("%s: CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO requires a connection",
				    __func__);
		/* fall through */
	case CONMGR_WORK_TYPE_TIME_DELAY_FIFO:
	{
		_update_last_time(true, work->mgr);
		work->begin.seconds += mgr->last_time.tv_sec;
		list_append(mgr->delayed_work, work);
		_update_timer(true, mgr);
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
		_handle_work(true, work);
		break;
	case CONMGR_WORK_TYPE_INVALID:
	case CONMGR_WORK_TYPE_MAX:
		fatal("%s: invalid type", __func__);
	}
}

static void _handle_work(bool locked, work_t *work)
{
	con_mgr_t *mgr = work->mgr;
	con_mgr_fd_t *con = work->con;

	if (con)
		log_flag(NET, "%s: [%s] work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, con->name, (uintptr_t) work,
			con_mgr_work_status_string(work->status),
			con_mgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);
	else
		log_flag(NET, "%s: work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, (uintptr_t) work,
			con_mgr_work_status_string(work->status),
			con_mgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);

	if (!locked)
		slurm_mutex_lock(&mgr->mutex);

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

	_signal_change(mgr, true);

	if (!locked)
		slurm_mutex_unlock(&mgr->mutex);
}

static void _add_work(bool locked, con_mgr_t *mgr, con_mgr_fd_t *con,
		      con_mgr_work_func_t func, con_mgr_work_type_t type,
		      void *arg, const char *tag)
{
	work_t *work = xmalloc(sizeof(*work));
	*work = (work_t) {
		.magic = MAGIC_WORK,
		.mgr = mgr,
		.con = con,
		.func = func,
		.arg = arg,
		.tag = tag,
		.type = type,
		.status = CONMGR_WORK_STATUS_PENDING,
	};

	_handle_work(locked, work);
}

extern void con_mgr_add_work(con_mgr_t *mgr, con_mgr_fd_t *con,
			     con_mgr_work_func_t func, con_mgr_work_type_t type,
			     void *arg, const char *tag)
{
	_add_work(false, mgr, con, func, type, arg, tag);
}

extern void con_mgr_add_delayed_work(con_mgr_t *mgr, con_mgr_fd_t *con,
				     con_mgr_work_func_t func, time_t seconds,
				     long nanoseconds, void *arg,
				     const char *tag)
{
	work_t *work;

	/*
	 * Renormalize ns into seconds to only have partial seconds in
	 * nanoseconds. Nanoseconds won't matter with a larger number of
	 * seconds.
	 */
	seconds += nanoseconds / NSEC_IN_SEC;
	nanoseconds = nanoseconds % NSEC_IN_SEC;

	work = xmalloc(sizeof(*work));
	*work = (work_t){
		.magic = MAGIC_WORK,
		.mgr = mgr,
		.con = con,
		.func = func,
		.arg = arg,
		.tag = tag,
		.status = CONMGR_WORK_STATUS_PENDING,
		.begin.seconds = seconds,
		.begin.nanoseconds = nanoseconds,
	};

	if (con)
		work->type = CONMGR_WORK_TYPE_CONNECTION_DELAY_FIFO;
	else
		work->type = CONMGR_WORK_TYPE_TIME_DELAY_FIFO;

	log_flag(NET, "%s: adding %lds %ldns delayed work %s@0x%"PRIxPTR,
		 __func__, seconds, nanoseconds, work->tag,
		 (uintptr_t) work->func);

	_handle_work(false, work);
}
