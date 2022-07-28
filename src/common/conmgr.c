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
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/conmgr.h"
#include "src/common/fd.h"
#include "src/common/http.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/strlcpy.h"
#include "src/common/timers.h"
#include "src/common/workq.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MAGIC_CON_MGR_FD 0xD23444EF
#define MAGIC_CON_MGR 0xD232444A
#define MAGIC_WRAP_WORK 0xD231444A
/* Default buffer to 1 page */
#define BUFFER_START_SIZE 4096
#define MAX_OPEN_CONNECTIONS 124

/*
 * there can only be 1 SIGINT handler, so we are using a mutex to protect
 * the sigint_fd for changes.
 */
pthread_mutex_t sigint_mutex = PTHREAD_MUTEX_INITIALIZER;
int sigint_fd[2] = { -1, -1 };

static int _close_con_for_each(void *x, void *arg);
static void _listen_accept(void *x);
static void _wrap_on_connection(void *x);
static inline void _add_con_work(bool locked, con_mgr_fd_t *con,
				 work_func_t func, void *arg, const char *tag);
static void _wrap_on_data(void *x);

typedef void (*on_poll_event_t)(con_mgr_t *mgr, int fd, con_mgr_fd_t *con,
				short revents);

typedef struct {
	int magic;
	con_mgr_fd_t *con;
	work_func_t func;
	void *arg;
	const char *tag;
} wrap_work_arg_t;

/* simple struct to keep track of fds */
typedef struct {
	con_mgr_t *mgr;
	struct pollfd *fds;
	int nfds;
} poll_args_t;

#ifndef NDEBUG
static int _find_by_ptr(void *x, void *key)
{
	return (x == key);
}
#endif /*!NDEBUG */

/*
 * Find by matching fd to connection
 */
static int _find_by_fd(void *x, void *key)
{
	con_mgr_fd_t *con = x;
	int fd = *(int *)key;
	return (con->input_fd == fd) || (con->output_fd == fd);
}

static inline void _check_magic_mgr(con_mgr_t *mgr)
{
	xassert(mgr);
	xassert(mgr->magic == MAGIC_CON_MGR);
	xassert(mgr->connections);
}

static inline void _check_magic_fd(con_mgr_fd_t *con)
{
	xassert(con);
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->events.on_connection);
	xassert(con->events.on_data);
	xassert(con->name && con->name[0]);

	/*
	 * any positive non-zero fd is plausible but
	 * only use -1 when fd is invalid
	 * */
	xassert(con->input_fd >= 0 || con->input_fd == -1);
	/* only expect a output obj if output fd is diff */
	xassert((con->input_fd == con->output_fd) || (con->output_fd > 0) ||
		con->output_fd == -1);

	xassert(con->is_listen || (con->in && con->out));

	if (con->is_listen) {
		/* listening con should only be in listen list */
		xassert(list_find_first(con->mgr->listen, _find_by_ptr, con));
		xassert(!list_find_first(con->mgr->connections, _find_by_ptr,
					 con));
	} else {
		/* process con should only be in connections list */
		xassert(!list_find_first(con->mgr->listen, _find_by_ptr, con));
		xassert(list_find_first(con->mgr->connections, _find_by_ptr,
					con));
	}
}

static void _connection_fd_delete(void *x)
{
	con_mgr_fd_t *con = x;
	con_mgr_t *mgr;

	if (!con)
		return;
	mgr = con->mgr;

	_check_magic_mgr(mgr);
	log_flag(NET, "%s: [%s] free connection input_fd=%d output_fd=%d",
		 __func__, con->name, con->input_fd, con->output_fd);

	xassert(list_is_empty(con->work));

	/* make sure this isn't a dangling pointer */
	if (con->is_listen)
		xassert(!list_remove_first(mgr->listen, _find_by_ptr, con));
	else
		xassert(!list_remove_first(mgr->connections, _find_by_ptr,
					   con));
	FREE_NULL_BUFFER(con->in);
	FREE_NULL_BUFFER(con->out);
	FREE_NULL_LIST(con->work);
	xfree(con->name);
	xfree(con->unix_socket);

	xassert(!con->arg);
	con->magic = ~MAGIC_CON_MGR_FD;
	xfree(con);
}

static void _sig_int_handler(int signo)
{
	char buf[] = "1";
	xassert(signo == SIGINT);
	xassert(sigint_fd[0] != -1);
	xassert(sigint_fd[1] != -1);

try_again:
	/* send 1 byte of trash */
	if (write(sigint_fd[1], buf, 1) != 1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			log_flag(NET, "%s: trying again: %m", __func__);
			goto try_again;
		}

		fatal("%s: unable to signal connection manager: %m", __func__);
	}
}

extern con_mgr_t *init_con_mgr(int thread_count, con_mgr_callbacks_t callbacks)
{
	con_mgr_t *mgr = xmalloc(sizeof(*mgr));

	mgr->magic = MAGIC_CON_MGR;
	mgr->connections = list_create(NULL);
	mgr->listen = list_create(NULL);
	mgr->callbacks = callbacks;

	slurm_mutex_init(&mgr->mutex);
	slurm_cond_init(&mgr->cond, NULL);

	mgr->workq = new_workq(thread_count);

	if (pipe(mgr->event_fd))
		fatal("%s: unable to open unnamed pipe: %m", __func__);

	fd_set_blocking(mgr->event_fd[0]);
	fd_set_blocking(mgr->event_fd[1]);

	if (pipe(mgr->sigint_fd))
		fatal("%s: unable to open unnamed pipe: %m", __func__);

	fd_set_blocking(mgr->sigint_fd[0]);
	fd_set_blocking(mgr->sigint_fd[1]);

	_check_magic_mgr(mgr);

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

	_check_magic_mgr(mgr);

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

static void _close_all_connections(void *x)
{
	con_mgr_t *mgr = x;
	_check_magic_mgr(mgr);

	slurm_mutex_lock(&mgr->mutex);

	/* close all connections */
	list_for_each(mgr->connections, _close_con_for_each, NULL);
	list_for_each(mgr->listen, _close_con_for_each, NULL);

	/* break out of any poll() calls */
	_signal_change(mgr, true);

	slurm_mutex_unlock(&mgr->mutex);
}

extern void free_con_mgr(con_mgr_t *mgr)
{
	if (!mgr)
		return;

	log_flag(NET, "%s: connection manager shutting down", __func__);

	/* processing may still be running at this point in a thread */
	_close_all_connections(mgr);

	/*
	 * make sure WORKQ is done before making any changes encase there are
	 * any outstanding threads running
	 */
	FREE_NULL_WORKQ(mgr->workq);

	/*
	 * At this point, there should be no threads running.
	 * It should be safe to shutdown the mgr.
	 */
	xassert(mgr->shutdown);
	xassert(!mgr->poll_active);
	xassert(!mgr->listen_active);
	xassert(!mgr->inspecting);

	xassert(list_is_empty(mgr->connections));
	xassert(list_is_empty(mgr->listen));
	FREE_NULL_LIST(mgr->connections);
	FREE_NULL_LIST(mgr->listen);

	slurm_mutex_destroy(&mgr->mutex);
	slurm_cond_destroy(&mgr->cond);

	if (close(mgr->event_fd[0]) || close(mgr->event_fd[1]))
		error("%s: unable to close event_fd: %m", __func__);

	if (close(mgr->sigint_fd[0]) || close(mgr->sigint_fd[1]))
		error("%s: unable to close sigint_fd: %m", __func__);

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

	if (!locked) {
		/* avoid mutex deadlock */
		_check_magic_fd(con);
		_check_magic_mgr(con->mgr);
	}

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
cleanup:
	if (!locked)
		slurm_mutex_unlock(&con->mgr->mutex);
}

static con_mgr_fd_t *_add_connection(con_mgr_t *mgr, con_mgr_fd_t *source,
				     int input_fd, int output_fd,
				     const con_mgr_events_t events,
				     const slurm_addr_t *addr,
				     socklen_t addrlen, bool is_listen,
				     const char *unix_socket_path)
{
	struct stat fbuf = { 0 };
	con_mgr_fd_t *con = NULL;

	_check_magic_mgr(mgr);

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

	_check_magic_fd(con);

	return con;
}

/*
 * Wrap work requested to notify mgr when that work is complete
 */
static void _wrap_work(void *x)
{
	wrap_work_arg_t *args = x;
	con_mgr_fd_t *con = args->con;
	con_mgr_t *mgr = con->mgr;
#ifndef NDEBUG
	/* detect any changes to connection that are not allowed */
	con_mgr_fd_t change;
#endif /* !NDEBUG */

	_check_magic_fd(con);
	_check_magic_mgr(mgr);

	xassert(args->magic == MAGIC_WRAP_WORK);
#ifndef NDEBUG
	slurm_mutex_lock(&mgr->mutex);
	xassert(con->has_work);
	memcpy(&change, con, sizeof(change));

	/* con may get deleted by func */
	log_flag(NET, "%s: [%s] BEGIN func=0x%"PRIxPTR" arg=0x%"PRIxPTR,
		 __func__, con->name, (uintptr_t) args->func,
		 (uintptr_t) args->arg);
	slurm_mutex_unlock(&mgr->mutex);
#endif /* !NDEBUG */

	/* catch cyclic calls */
	xassert(args->func != &_wrap_work);

	args->func(args->arg);

	_check_magic_fd(con);
	_check_magic_mgr(mgr);

	slurm_mutex_lock(&mgr->mutex);
#ifndef NDEBUG
	log_flag(NET, "%s: [%s] END func=0x%"PRIxPTR" arg=0x%"PRIxPTR" queued_work=%u",
		 __func__, con->name, (uintptr_t) args->func,
		 (uintptr_t) args->arg, list_count(con->work));

	/* verify nothing has changed in the promised members */
	xassert(change.in == con->in);
	xassert(change.out == con->out);
	xassert(change.name == con->name);
	xassert(change.mgr == con->mgr);
	xassert(change.arg == con->arg || (args->func == &_wrap_on_connection));
	xassert(change.on_data_tried == con->on_data_tried ||
		(args->func == &_wrap_on_data));
#endif /* !NDEBUG */
	xassert(con->has_work);
	con->has_work = false;

	_signal_change(mgr, true);
	slurm_mutex_unlock(&mgr->mutex);

	args->magic = ~MAGIC_WRAP_WORK;
	xfree(args);
}

/*
 * Add work to connection with existing args struct
 */
static inline void _add_con_work_args(bool locked, con_mgr_fd_t *con,
				      wrap_work_arg_t *args)
{
	log_flag(NET, "%s: [%s] locked=%s func=%s",
		 __func__, con->name, (locked ? "T" : "F"), args->tag);

	if (!locked)
		slurm_mutex_lock(&con->mgr->mutex);

	xassert(args->func != &_wrap_work);

	if (!con->has_work) {
		con->has_work = true;
		workq_add_work(con->mgr->workq, _wrap_work, args, args->tag);
	} else {
		log_flag(NET, "%s: [%s] queuing \"%s\" pending work: %u total",
			 __func__, con->name, args->tag, list_count(con->work));

		list_append(con->work, args);
	}

	_signal_change(con->mgr, true);

	if (!locked)
		slurm_mutex_unlock(&con->mgr->mutex);
}

/*
 * Add work to connection
 */
static inline void _add_con_work(bool locked, con_mgr_fd_t *con,
				 work_func_t func, void *arg, const char *tag)
{
	wrap_work_arg_t *args = xmalloc(sizeof(*args));
	*args = (wrap_work_arg_t){ .magic = MAGIC_WRAP_WORK,
				   .con = con,
				   .func = func,
				   .arg = arg,
				   .tag = tag };

	_add_con_work_args(locked, con, args);
}

static void _handle_read(void *x)
{
	con_mgr_fd_t *con = x;
	ssize_t read_c;
	int readable;

	con->can_read = false;
	_check_magic_fd(con);
	_check_magic_mgr(con->mgr);

	if (con->input_fd < 0) {
		xassert(con->read_eof);
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

	xassert(fcntl(con->input_fd, F_GETFL) & O_NONBLOCK);
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

		error("%s: [%s] error while reading: %m", __func__, con->name);
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

static void _handle_write(void *x)
{
	con_mgr_fd_t *con = x;
	ssize_t wrote;

	_check_magic_fd(con);
	_check_magic_mgr(con->mgr);

	if (get_buf_offset(con->out) == 0) {
		log_flag(NET, "%s: [%s] skipping attempt to write 0 bytes",
			 __func__, con->name);
		return;
	}

	log_flag(NET, "%s: [%s] attempting to write %u bytes to fd %u",
		 __func__, con->name, get_buf_offset(con->out), con->output_fd);

	xassert(fcntl(con->output_fd, F_GETFL) & O_NONBLOCK);
	xassert(con->output_fd != -1);
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

static void _wrap_on_data(void *x)
{
	con_mgr_fd_t *con = x;
	con_mgr_t *mgr = con->mgr;
	int avail = get_buf_offset(con->in);
	int size = size_buf(con->in);
	int rc;

	_check_magic_fd(con);
	_check_magic_mgr(mgr);
	xassert(con->arg);

	/* override buffer offset to allow reading */
	set_buf_offset(con->in, 0);
	/* override buffer size to only read upto previous offset */
	con->in->size = avail;

	log_flag(NET, "%s: [%s] BEGIN func=0x%"PRIxPTR" arg=0x%"PRIxPTR,
		 __func__, con->name, (uintptr_t) con->events.on_data,
		 (uintptr_t) con->arg);

	rc = con->events.on_data(con, con->arg);

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

static void _wrap_on_connection(void *x)
{
	con_mgr_fd_t *con = x;
	con_mgr_t *mgr = con->mgr;
	void *arg;

	_check_magic_fd(con);
	_check_magic_mgr(mgr);

	log_flag(NET, "%s: [%s] BEGIN func=0x%"PRIxPTR,
		 __func__, con->name, (uintptr_t) con->events.on_connection);

	arg = con->events.on_connection(con);

	log_flag(NET, "%s: [%s] END func=0x%"PRIxPTR" arg=0x%"PRIxPTR,
		 __func__, con->name, (uintptr_t) con->events.on_connection,
		 (uintptr_t) arg);

	if (!arg) {
		error("%s: [%s] closing connection due to NULL return from on_connection",
		      __func__, con->name);
		_close_con(false, con);
	} else {
		slurm_mutex_lock(&mgr->mutex);
		xassert(!con->is_connected);
		xassert(!con->arg);
		con->arg = arg;
		con->is_connected = true;
		slurm_mutex_unlock(&mgr->mutex);
	}

	_check_magic_fd(con);
	_check_magic_mgr(mgr);
}

extern int _con_mgr_process_fd_internal(con_mgr_t *mgr, con_mgr_fd_t *source,
					int input_fd, int output_fd,
					const con_mgr_events_t events,
					const slurm_addr_t *addr,
					socklen_t addrlen)
{
	con_mgr_fd_t *con;
	_check_magic_mgr(mgr);

	con = _add_connection(mgr, source, input_fd, output_fd, events, addr,
			      addrlen, false, NULL);

	if (!con)
		return SLURM_ERROR;

	_check_magic_fd(con);

	_add_con_work(false, con, _wrap_on_connection, con,
		      "_wrap_on_connection");

	return SLURM_SUCCESS;
}

extern int con_mgr_process_fd(con_mgr_t *mgr, int input_fd, int output_fd,
			      const con_mgr_events_t events,
			      const slurm_addr_t *addr, socklen_t addrlen)
{
	con_mgr_fd_t *con;
	_check_magic_mgr(mgr);

	con = _add_connection(mgr, NULL, input_fd, output_fd, events, addr,
			      addrlen, false, NULL);

	if (!con)
		return SLURM_ERROR;

	_check_magic_fd(con);

	_add_con_work(false, con, _wrap_on_connection, con,
		      "_wrap_on_connection");

	return SLURM_SUCCESS;
}

extern int con_mgr_process_fd_listen(con_mgr_t *mgr, int fd,
				     const con_mgr_events_t events,
				     const slurm_addr_t *addr,
				     socklen_t addrlen)
{
	con_mgr_fd_t *con;
	_check_magic_mgr(mgr);

	con = _add_connection(mgr, NULL, fd, fd, events, addr, addrlen, true,
			      NULL);
	if (!con)
		return SLURM_ERROR;

	_check_magic_fd(con);

	_signal_change(mgr, false);

	return SLURM_SUCCESS;
}

extern int con_mgr_process_fd_unix_listen(con_mgr_t *mgr, int fd,
					  const con_mgr_events_t events,
					  const slurm_addr_t *addr,
					  socklen_t addrlen, const char *path)
{
	con_mgr_fd_t *con;
	_check_magic_mgr(mgr);

	con = _add_connection(mgr, NULL, fd, fd, events, addr, addrlen, true,
			      path);
	if (!con)
		return SLURM_ERROR;

	_check_magic_fd(con);

	_signal_change(mgr, false);

	return SLURM_SUCCESS;
}

/*
 * Event on a processing socket.
 * mgr must be locked.
 */
static inline void _handle_poll_event(con_mgr_t *mgr, int fd, con_mgr_fd_t *con,
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
	int count, rc;

	/* cant run full magic checks inside of list lock */
	xassert(mgr->magic == MAGIC_CON_MGR);
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(con->name && con->name[0]);

	/* make sure the connection has finished on_connection */
	if (!con->is_listen && !con->is_connected) {
		xassert(con->output_fd != -1);
		log_flag(NET, "%s: [%s] waiting for on_connection to complete",
			 __func__, con->name);
		return 0;
	}

	/* connection may have a running thread, do nothing */
	if (con->has_work) {
		log_flag(NET, "%s: [%s] connection has work to do",
			 __func__, con->name);
		return 0;
	}

	/* always do work first */
	if ((count = list_count(con->work))) {
		wrap_work_arg_t *args = list_pop(con->work);
		xassert(args);

		log_flag(NET, "%s: [%s] queuing pending work: %u total",
			 __func__, con->name, count);

		_add_con_work_args(true, con, args);
		return 0;
	}

	/* handle out going data */
	if (!con->is_listen && con->output_fd != -1 &&
	    (count = get_buf_offset(con->out))) {
		xassert(con->output_fd != -1);
		if (con->can_write) {
			log_flag(NET, "%s: [%s] need to write %u bytes",
				 __func__, con->name, count);
			_add_con_work(true, con, _handle_write, con,
				      "_handle_write");
		} else {
			/* must wait until poll allows write of this socket */
			log_flag(NET, "%s: [%s] waiting to write %u bytes",
				 __func__, con->name, get_buf_offset(con->out));
		}
		return 0;
	}

	/* read as much data as possible before processing */
	if (!con->is_listen && !con->read_eof && con->can_read) {
		xassert(con->input_fd != -1);
		log_flag(NET, "%s: [%s] queuing read", __func__, con->name);
		/* reset if data has already been tried if about to read data */
		con->on_data_tried = false;
		_add_con_work(true, con, _handle_read, con, "_handle_read");
		return 0;
	}

	/* handle already read data */
	if (!con->is_listen && get_buf_offset(con->in) && !con->on_data_tried) {
		log_flag(NET, "%s: [%s] need to process %u bytes",
			 __func__, con->name, get_buf_offset(con->in));

		_add_con_work(true, con, _wrap_on_data, con, "_wrap_on_data");
		return 0;
	}

	if (!con->read_eof) {
		xassert(con->input_fd != -1);
		/* must wait until poll allows read from this socket */
		if (con->is_listen)
			log_flag(NET, "%s: [%s] waiting for new connection",
				 __func__, con->name);
		else
			log_flag(NET, "%s: [%s] waiting to read pending_read=%u pending_write=%u has_work=%c",
				 __func__, con->name, get_buf_offset(con->in),
				 get_buf_offset(con->out),
				 (con->has_work ? 'T' : 'F'));
		return 0;
	}

	if (!con->is_listen && con->arg) {
		log_flag(NET, "%s: [%s] queuing up on_finish",
			 __func__, con->name);

		/* notify caller of closing */
		if (con->is_connected) {
			_add_con_work(true, con, con->events.on_finish, con->arg,
				      "on_finish");
			/* on_finish must free arg */
			con->arg = NULL;
		} else {
			/* arg should only be set if after on_connection() */
			xassert(!con->arg);
		}

		return 0;
	}

	/*
	 * This connection has no more pending work or possible IO:
	 * Remove the connection and close everything.
	 */
	log_flag(NET, "%s: [%s] closing connection input_fd=%d output_fd=%d",
		 __func__, con->name, con->input_fd, con->output_fd);

	/* close any open file descriptors */
	if (con->input_fd != -1) {
		if (close(con->input_fd) == -1)
			log_flag(NET, "%s: [%s] unable to close input fd %d: %m",
				 __func__, con->name, con->input_fd);

		/* if there is only 1 fd: forget it too */
		if (con->input_fd == con->output_fd)
			con->output_fd = -1;

		/* forget invalid fd */
		con->input_fd = -1;
	}
	if (con->output_fd != -1) {
		if (close(con->output_fd) == -1)
			log_flag(NET, "%s: [%s] unable to close output fd %d: %m",
				 __func__, con->name, con->output_fd);

		con->output_fd = -1;
	}

	log_flag(NET, "%s: [%s] closed connection", __func__, con->name);

	/* have a thread free all the memory */
	xassert(list_is_empty(con->work));
	xassert(!con->has_work);
	if ((rc = workq_add_work(mgr->workq, _connection_fd_delete, con,
				 "_connection_fd_delete"))) {
		log_flag(NET, "%s: [%s] direct cleanup as workq rejected _connection_fd_delete(): %s",
			 __func__, con->name, slurm_strerror(rc));
		_connection_fd_delete(con);
	}

	/* remove this connection */
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
	_check_magic_mgr(mgr);

	slurm_mutex_lock(&mgr->mutex);

	if (list_delete_all(mgr->connections, _handle_connection, NULL))
		slurm_cond_broadcast(&mgr->cond);
	mgr->inspecting = false;

	slurm_mutex_unlock(&mgr->mutex);
}

/*
 * Event on a listen only socket
 * mgr must be locked.
 */
static inline void _handle_listen_event(con_mgr_t *mgr, int fd,
					con_mgr_fd_t *con, short revents)
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
		_add_con_work(true, con, _listen_accept, con, "_listen_accept");
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

/*
 * Handle poll and events
 *
 * NOTE: mgr mutex must not be locked but will be locked upon return
 */
static inline void _poll(con_mgr_t *mgr, poll_args_t *args, List fds,
			 on_poll_event_t on_poll, const char *tag)
{
	int rc = SLURM_SUCCESS;
	struct pollfd *fds_ptr = NULL;
	con_mgr_fd_t *con;

again:
	rc = poll(args->fds, args->nfds, -1);
	if (rc == -1) {
		if ((errno == EINTR) && !mgr->exit_on_error) {
			log_flag(NET, "%s: [%s] poll interrupted. Trying again.",
				 __func__, tag);
			goto again;
		}

		fatal("%s: [%s] unable to poll listening sockets: %m",
		      __func__, tag);
	}

	slurm_mutex_lock(&mgr->mutex);

	if (rc == 0) {
		log_flag(NET, "%s: [%s] poll timed out", __func__, tag);
		return;
	}

	fds_ptr = args->fds;
	for (int i = 0; i < args->nfds; i++, fds_ptr++) {
		if (!fds_ptr->revents)
			continue;

		if (fds_ptr->fd == mgr->sigint_fd[0]) {
			if (!mgr->shutdown)
				info("%s: [%s] caught SIGINT. Shutting down.",
				     __func__, tag);
			mgr->shutdown = true;
			_handle_event_pipe(mgr, fds_ptr, tag, "SIGINT");
			_signal_change(mgr, true);
		}

		if (fds_ptr->fd == mgr->event_fd[0])
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
			on_poll(mgr, fds_ptr->fd, con, fds_ptr->revents);
			/*
			 * signal that something might have happened and to
			 * restart listening
			 * */
			_signal_change(mgr, true);
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
	ListIterator itr;

	_check_magic_mgr(mgr);

	slurm_mutex_lock(&mgr->mutex);

	/* grab counts once */
	count = list_count(mgr->connections);

	fds_ptr = args->fds;

	xrecalloc(args->fds, ((count * 2) + 2), sizeof(*args->fds));
	xassert(sizeof(*fds_ptr) == sizeof(*args->fds));

	args->nfds = 0;
	fds_ptr = args->fds;

	/* Add signal fd */
	fds_ptr->fd = mgr->sigint_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	/* Add event fd */
	fds_ptr->fd = mgr->event_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	/*
	 * populate sockets with !has_work
	 */
	itr = list_iterator_create(mgr->connections);
	while ((con = list_next(itr))) {
		_check_magic_fd(con);

		if (con->has_work)
			continue;

		log_flag(NET, "%s: [%s] poll read_eof=%s input=%u output=%u has_work=%c",
			 __func__, con->name, (con->read_eof ? "T" : "F"),
			 get_buf_offset(con->in), get_buf_offset(con->out),
			 (con->has_work ? 'T' : 'F'));

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

	xassert(args->nfds >= 2);
	xassert(args->nfds <= ((count * 2) + 2));
	slurm_mutex_unlock(&mgr->mutex);

	log_flag(NET, "%s: polling %u file descriptors for %u connections",
		 __func__, args->nfds, count);

	_poll(mgr, args, mgr->connections, &_handle_poll_event, __func__);

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
	ListIterator itr;

	_check_magic_mgr(mgr);

	slurm_mutex_lock(&mgr->mutex);
	xassert(mgr->listen_active);

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
	fds_ptr->fd = mgr->sigint_fd[0];
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
		_check_magic_fd(con);

		/* already accept queued or listener already closed */
		if (con->has_work || con->read_eof)
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

	xassert(args->nfds <= ((count * 2) + 2));
	xassert(args->nfds);
	slurm_mutex_unlock(&mgr->mutex);

	log_flag(NET, "%s: polling %u/%u file descriptors",
		 __func__, args->nfds, (count + 2));

	_poll(mgr, args, mgr->listen, &_handle_listen_event, __func__);
cleanup:
	mgr->listen_active = false;
	_signal_change(mgr, true);
	slurm_mutex_unlock(&mgr->mutex);
}

/*
 * Poll all sockets non-listen connections
 */
static inline int _watch(con_mgr_t *mgr)
{
	poll_args_t *listen_args = NULL;
	poll_args_t *poll_args = NULL;
	int count, event_read;
	char buf[100]; /* buffer for event_read */
	bool work; /* is there any work to do? */

	_check_magic_mgr(mgr);
	slurm_mutex_lock(&mgr->mutex);
watch:
	if (mgr->shutdown) {
		slurm_mutex_unlock(&mgr->mutex);
		_close_all_connections(mgr);
		slurm_mutex_lock(&mgr->mutex);
	}

	/* grab counts once */
	count = list_count(mgr->connections);

	log_flag(NET, "%s: starting connections=%u listen=%u",
		 __func__, count, list_count(mgr->listen));

	if (!mgr->poll_active && !mgr->listen_active) {
		/* only clear event pipe once both polls are done */
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
	}

	work = false;

	/* start listen thread if needed */
	if (!list_is_empty(mgr->listen)) {
		if (!listen_args) {
			listen_args = xmalloc(sizeof(*listen_args));
			listen_args->mgr = mgr;
		}

		/* run any queued work */
		list_delete_all(mgr->listen, _handle_connection, NULL);

		if (!mgr->listen_active) {
			/* only try to listen if number connections is below limit */
			if (count >= MAX_OPEN_CONNECTIONS)
				log_flag(NET, "%s: deferring accepting new connections until count is below max: %u/%u",
					 __func__, count, MAX_OPEN_CONNECTIONS);
			else { /* request a listen thread to run */
				log_flag(NET, "%s: queuing up listen", __func__);
				mgr->listen_active = true;
				workq_add_work(mgr->workq, _listen, listen_args,
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
			workq_add_work(mgr->workq, _inspect_connections, mgr,
				       "_inspect_connections");
		}

		if (!mgr->poll_active) {
			/* request a listen thread to run */
			log_flag(NET, "%s: queuing up poll", __func__);
			mgr->poll_active = true;
			workq_add_work(mgr->workq, _poll_connections, poll_args,
				       "_poll_connections");
		} else
			log_flag(NET, "%s: poll active already", __func__);

		work = true;
	}

	if (work) {
		/* wait until something happens */
		slurm_cond_wait(&mgr->cond, &mgr->mutex);
		_check_magic_mgr(mgr);
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
	struct sigaction old_sa, sa = { .sa_handler = _sig_int_handler };

	_check_magic_mgr(mgr);

	slurm_mutex_lock(&sigint_mutex);
	//TODO: allow for multiple conmgrs to run at once
	xassert(sigint_fd[0] == -1);
	xassert(sigint_fd[1] == -1);
	sigint_fd[0] = mgr->sigint_fd[0];
	sigint_fd[1] = mgr->sigint_fd[1];
	slurm_mutex_unlock(&sigint_mutex);

	/*
	 * Catch SIGINT as a safe way to shutdown
	 */
	if (sigaction(SIGINT, &sa, &old_sa))
		fatal("%s: unable to catch SIGINT: %m", __func__);

	rc = _watch(mgr);
	xassert(mgr->shutdown);

	if (sigaction(SIGINT, &old_sa, NULL))
		fatal("%s: unable to return SIGINT to default: %m", __func__);

	slurm_mutex_lock(&sigint_mutex);
	sigint_fd[0] = -1;
	sigint_fd[1] = -1;
	slurm_mutex_unlock(&sigint_mutex);

	return rc;
}

/*
 * listen socket is ready to accept
 */
static void _listen_accept(void *x)
{
	con_mgr_fd_t *con = x;
	con_mgr_t *mgr = con->mgr;
	int rc;
	slurm_addr_t addr = {0};
	socklen_t addrlen = sizeof(addr);
	int fd;

	_check_magic_fd(con);
	_check_magic_mgr(mgr);

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
	if ((rc = _con_mgr_process_fd_internal(mgr, con, fd, fd, con->events,
					       &addr, addrlen))) {
		log_flag(NET, "%s: [fd:%d] _con_mgr_process_fd_internal rejected: %s",
			 __func__, fd, slurm_strerror(rc));
		_close_con(false, con);
	}
}

extern int con_mgr_queue_write_fd(con_mgr_fd_t *con, const void *buffer,
				  const size_t bytes)
{
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
	con->out->processed += bytes;

	log_flag(NET, "%s: [%s] queued %zu/%u bytes in outgoing buffer",
		 __func__, con->name, bytes, get_buf_offset(con->out));

	_signal_change(con->mgr, false);

	return SLURM_SUCCESS;
}

extern void con_mgr_queue_close_fd(con_mgr_fd_t *con)
{
	xassert(con->has_work);

	_check_magic_fd(con);
	_close_con(false, con);
}

typedef struct {
	con_mgr_events_t events;
	con_mgr_t *mgr;
} socket_listen_init_t;

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
			init->mgr, fd, init->events,
			(const slurm_addr_t *) &addr, sizeof(addr),
			unixsock);
	} else {
		/* split up host and port */
		if (!(parsed_hp = init->mgr->callbacks.parse(hostport)))
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

		rc = con_mgr_process_fd_listen(init->mgr, fd, init->events,
			(const slurm_addr_t *) addr->ai_addr,
			addr->ai_addrlen);
	}

	freeaddrinfo(addrlist);
	init->mgr->callbacks.free_parse(parsed_hp);

	return rc;
}

extern int con_mgr_create_sockets(con_mgr_t *mgr, List hostports,
				  con_mgr_events_t events)
{
	int rc;
	socket_listen_init_t *init = xmalloc(sizeof(*init));
	init->events = events;
	init->mgr = mgr;

	if (list_for_each(hostports, _create_socket, init) > 0)
		rc = SLURM_SUCCESS;
	else
		rc = SLURM_ERROR;

	xfree(init);

	return rc;
}

extern void con_mgr_request_shutdown(con_mgr_fd_t *con)
{
	con_mgr_t *mgr = con->mgr;

	_check_magic_fd(con);

	log_flag(NET, "%s: shutdown requested", __func__);

	slurm_mutex_lock(&mgr->mutex);
	mgr->shutdown = true;
	_signal_change(mgr, true);
	slurm_mutex_unlock(&mgr->mutex);
}
