/*****************************************************************************\
 *  watch.c - definitions for watch loop in connection manager
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
#include <limits.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"
#include "src/conmgr/signals.h"

static void _listen_accept(conmgr_fd_t *con, conmgr_work_type_t type,
			   conmgr_work_status_t status, const char *tag,
			   void *arg);

static void _on_finish_wrapper(conmgr_fd_t *con, conmgr_work_type_t type,
			       conmgr_work_status_t status, const char *tag,
			       void *arg)
{
	if (con->events.on_finish)
		con->events.on_finish(arg);

	slurm_mutex_lock(&mgr.mutex);
	con->wait_on_finish = false;
	/* on_finish must free arg */
	con->arg = NULL;
	slurm_mutex_unlock(&mgr.mutex);
}

/*
 * handle connection states and apply actions required.
 * mgr mutex must be locked.
 *
 * RET 1 to remove or 0 to remain in list
 */
static int _handle_connection(void *x, void *arg)
{
	conmgr_fd_t *con = x;
	int count;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	/* connection may have a running thread, do nothing */
	if (con->work_active) {
		log_flag(CONMGR, "%s: [%s] connection has work to do",
			 __func__, con->name);
		return 0;
	}

	/* always do work first */
	if ((count = list_count(con->work))) {
		work_t *work = list_pop(con->work);

		log_flag(CONMGR, "%s: [%s] queuing pending work: %u total",
			 __func__, con->name, count);

		work->status = CONMGR_WORK_STATUS_RUN;
		con->work_active = true; /* unset by _wrap_con_work() */

		log_flag(CONMGR, "%s: [%s] queuing work=0x%"PRIxPTR" status=%s type=%s func=%s@0x%"PRIxPTR,
			 __func__, con->name, (uintptr_t) work,
			conmgr_work_status_string(work->status),
			conmgr_work_type_string(work->type),
			work->tag, (uintptr_t) work->func);

		handle_work(true, work);
		return 0;
	}

	/* make sure the connection has finished on_connection */
	if (!con->is_listen && !con->is_connected && (con->input_fd != -1)) {
		log_flag(CONMGR, "%s: [%s] waiting for on_connection to complete",
			 __func__, con->name);
		return 0;
	}

	/* handle out going data */
	if (!con->is_listen && (con->output_fd >= 0) &&
	    !list_is_empty(con->out)) {
		if (con->can_write) {
			log_flag(CONMGR, "%s: [%s] %u pending writes",
				 __func__, con->name, list_count(con->out));
			add_work(true, con, handle_write,
				 CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
				 XSTRINGIFY(handle_write));
		} else {
			/*
			 * Only monitor for when connection is ready for writes
			 * as there is no point reading until the write is
			 * complete since it will be ignored.
			 */
			con_set_polling(true, con, PCTL_TYPE_WRITE_ONLY, __func__);

			/* must wait until poll allows write of this socket */
			log_flag(CONMGR, "%s: [%s] waiting for %u writes",
				 __func__, con->name, list_count(con->out));
		}
		return 0;
	}

	if (!con->is_listen && (count = list_count(con->write_complete_work))) {
		log_flag(CONMGR, "%s: [%s] queuing pending write complete work: %u total",
			 __func__, con->name, count);

		list_transfer(con->work, con->write_complete_work);
		return 0;
	}

	/* check if there is new connection waiting   */
	if (con->is_listen && !con->read_eof && con->can_read) {
		log_flag(CONMGR, "%s: [%s] listener has incoming connection",
			 __func__, con->name);

		/* disable polling until _listen_accept() completes */
		con_set_polling(true, con, PCTL_TYPE_INVALID, __func__);
		con->can_read = false;

		add_work(true, con, _listen_accept,
			 CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
			 XSTRINGIFY(_listen_accept));
		return 0;
	}

	/* read as much data as possible before processing */
	if (!con->is_listen && !con->read_eof && con->can_read) {
		log_flag(CONMGR, "%s: [%s] queuing read", __func__, con->name);
		/* reset if data has already been tried if about to read data */
		con->on_data_tried = false;
		add_work(true, con, handle_read,
			 CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
			 XSTRINGIFY(handle_read));
		return 0;
	}

	/* handle already read data */
	if (!con->is_listen && get_buf_offset(con->in) && !con->on_data_tried) {
		log_flag(CONMGR, "%s: [%s] need to process %u bytes",
			 __func__, con->name, get_buf_offset(con->in));

		add_work(true, con, wrap_on_data,
			 CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
			 XSTRINGIFY(wrap_on_data));
		return 0;
	}

	if (!con->read_eof) {
		xassert(con->input_fd != -1);

		/* must wait until poll allows read from this socket */
		if (con->is_listen) {
			con_set_polling(true, con, PCTL_TYPE_LISTEN, __func__);
			log_flag(CONMGR, "%s: [%s] waiting for new connection",
				 __func__, con->name);
		} else {
			con_set_polling(true, con, PCTL_TYPE_READ_WRITE, __func__);
			log_flag(CONMGR, "%s: [%s] waiting to read pending_read=%u pending_writes=%u work_active=%c",
				 __func__, con->name, get_buf_offset(con->in),
				 list_count(con->out),
				 (con->work_active ? 'T' : 'F'));
		}
		return 0;
	}

	/*
	 * Close out the incoming to avoid any new work coming into the
	 * connection.
	 */
	if (con->input_fd != -1) {
		log_flag(CONMGR, "%s: [%s] closing incoming on connection input_fd=%d",
			 __func__, con->name, con->input_fd);
		xassert(con->read_eof);
		close_con(true, con);
		return 0;
	}

	if (con->wait_on_finish) {
		log_flag(CONMGR, "%s: [%s] waiting for on_finish()",
			 __func__, con->name);
		return 0;
	}

	if (con->arg) {
		log_flag(CONMGR, "%s: [%s] queuing up on_finish",
			 __func__, con->name);

		con->wait_on_finish = true;

		/* notify caller of closing */
		add_work(true, con, _on_finish_wrapper,
			 CONMGR_WORK_TYPE_CONNECTION_FIFO, con->arg,
			 XSTRINGIFY(_on_finish_wrapper));

		return 0;
	}

	if (!list_is_empty(con->work) || !list_is_empty(con->write_complete_work)) {
		log_flag(CONMGR, "%s: [%s] outstanding work for connection output_fd=%d work=%u write_complete_work=%u",
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
	log_flag(CONMGR, "%s: [%s] closing connection input_fd=%d output_fd=%d",
		 __func__, con->name, con->input_fd, con->output_fd);

	if (con->output_fd != -1) {
		con_set_polling(true, con, PCTL_TYPE_INVALID, __func__);

		if (close(con->output_fd) == -1)
			log_flag(CONMGR, "%s: [%s] unable to close output fd %d: %m",
				 __func__, con->name, con->output_fd);

		con->output_fd = -1;
	}

	log_flag(CONMGR, "%s: [%s] closed connection", __func__, con->name);

	/* mark this connection for cleanup */
	return 1;
}

/*
 * listen socket is ready to accept
 */
static void _listen_accept(conmgr_fd_t *con, conmgr_work_type_t type,
			   conmgr_work_status_t status, const char *tag,
			   void *arg)
{
	slurm_addr_t addr = {0};
	socklen_t addrlen = sizeof(addr);
	int fd;
	conmgr_fd_t *child = NULL;
	const char *unix_path = NULL;

	if (con->input_fd == -1) {
		log_flag(CONMGR, "%s: [%s] skipping accept on closed connection",
			 __func__, con->name);
		return;
	} else
		log_flag(CONMGR, "%s: [%s] attempting to accept new connection",
			 __func__, con->name);

	/* try to get the new file descriptor and retry on errors */
	if ((fd = accept4(con->input_fd, (struct sockaddr *) &addr,
			  &addrlen, SOCK_CLOEXEC)) < 0) {
		if (errno == EINTR) {
			log_flag(CONMGR, "%s: [%s] interrupt on accept(). Retrying.",
				 __func__, con->name);
			return;
		}
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			log_flag(CONMGR, "%s: [%s] retry: %m", __func__, con->name);
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
		close_con(false, con);
		return;
	}

	if (addrlen <= 0)
		fatal("%s: empty address returned from accept()",
		      __func__);
	if (addrlen > sizeof(addr))
		fatal("%s: unexpected large address returned from accept(): %u bytes",
		      __func__, addrlen);

	if (addr.ss_family == AF_UNIX) {
		const struct sockaddr_un *usock = (struct sockaddr_un *) &addr;

		xassert(usock->sun_family == AF_UNIX);

		/* address may not be populated by kernel */
		if (usock->sun_path[0])
			unix_path = usock->sun_path;
		else
			unix_path = con->unix_socket;
	}

	/* hand over FD for normal processing */
	if (!(child = add_connection(con->type, con, fd, fd, con->events,
				      &addr, addrlen, false, unix_path,
				      con->new_arg))) {
		log_flag(CONMGR, "%s: [fd:%d] unable to a register new connection",
			 __func__, fd);
		return;
	}

	xassert(child->magic == MAGIC_CON_MGR_FD);

	add_work(false, child, wrap_on_connection,
		 CONMGR_WORK_TYPE_CONNECTION_FIFO, child,
		 XSTRINGIFY(wrap_on_connection));
}

/*
 * Inspect all connection states and apply actions required
 */

static void _inspect_connections(conmgr_fd_t *con, conmgr_work_type_t type,
				 conmgr_work_status_t status, const char *tag,
				 void *arg)
{
	slurm_mutex_lock(&mgr.mutex);
	if (list_transfer_match(mgr.listen_conns, mgr.complete_conns,
				_handle_connection, NULL))
		slurm_cond_broadcast(&mgr.cond);
	if (list_transfer_match(mgr.connections, mgr.complete_conns,
				_handle_connection, NULL))
		slurm_cond_broadcast(&mgr.cond);
	mgr.inspecting = false;

	slurm_mutex_unlock(&mgr.mutex);
}

/* caller (or thread) must hold mgr.mutex lock */
static int _handle_poll_event(int fd, pollctl_events_t events, void *arg)
{
	conmgr_fd_t *con = NULL;

	xassert(fd >= 0);

	if (!(con = con_find_by_fd(fd))) {
		/* close_con() was called during poll() was running */
		log_flag(CONMGR, "%s: Ignoring events for unknown fd:%d",
			 __func__, fd);
		return SLURM_SUCCESS;
	}

	con->can_read = false;
	con->can_write = false;

	if (pollctl_events_has_error(events)) {
		con_close_on_poll_error(true, con, fd);
		/* connection errored but not handling of the connection */
		return SLURM_SUCCESS;
	}

	if (con->is_listen) {
		/* Special handling for listening sockets */
		if (pollctl_events_has_hangup(events)) {
			log_flag(CONMGR, "%s: [%s] listener HANGUP",
				 __func__, con->name);
			con->read_eof = true;
		} else if (pollctl_events_can_read(events)) {
			con->can_read = true;
		} else {
			fatal_abort("should never happen");
		}

		log_flag(CONMGR, "%s: [%s] listener fd=%u can_read=%s read_eof=%s",
			 __func__, con->name, fd, (con->can_read ? "T" : "F"),
			 (con->read_eof ? "T" : "F"));

		return SLURM_SUCCESS;
	}

	if (fd == con->input_fd) {
		con->can_read = pollctl_events_can_read(events);

		if (!con->read_eof)
			con->read_eof = pollctl_events_has_hangup(events);
	}
	if (fd == con->output_fd)
		con->can_write = pollctl_events_can_write(events);

	log_flag(CONMGR, "%s: [%s] fd=%u can_read=%s can_write=%s read_eof=%s",
		 __func__, con->name, fd, (con->can_read ? "T" : "F"),
		 (con->can_write ? "T" : "F"), (con->read_eof ? "T" : "F"));

	return SLURM_SUCCESS;
}

/* Poll all connections */
static void _poll_connections(conmgr_fd_t *con, conmgr_work_type_t type,
			      conmgr_work_status_t status, const char *tag,
			      void *arg)
{
	int rc;

	xassert(!con);

	slurm_mutex_lock(&mgr.mutex);
	xassert(mgr.poll_active);

	if (mgr.quiesced) {
		log_flag(CONMGR, "%s: skipping poll() while quiesced",
			 __func__);
		goto done;
	} else if (list_is_empty(mgr.connections) &&
		   list_is_empty(mgr.listen_conns)) {
		log_flag(CONMGR, "%s: skipping poll() with 0 connections",
			 __func__);
		goto done;
	}

	slurm_mutex_unlock(&mgr.mutex);

	if ((rc = pollctl_poll(__func__)))
		fatal_abort("%s: should never fail: pollctl_poll()=%s",
			    __func__, slurm_strerror(rc));

	slurm_mutex_lock(&mgr.mutex);

	if ((rc = pollctl_for_each_event(_handle_poll_event, NULL,
					 XSTRINGIFY(_handle_poll_event),
					 __func__)))
		fatal_abort("%s: should never fail: pollctl_for_each_event()=%s",
			    __func__, slurm_strerror(rc));

done:
	xassert(mgr.poll_active);
	mgr.poll_active = false;
	signal_change(true, __func__);
	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: poll done", __func__);
}

extern void wait_for_watch(void)
{
	if (!mgr.watching)
		return;

	slurm_mutex_lock(&mgr.watch_mutex);
	slurm_mutex_unlock(&mgr.mutex);
	slurm_cond_wait(&mgr.watch_cond, &mgr.watch_mutex);
	slurm_mutex_unlock(&mgr.watch_mutex);
}

static void _connection_fd_delete(conmgr_fd_t *wrong_con,
				  conmgr_work_type_t type,
				  conmgr_work_status_t status, const char *tag,
				  void *arg)
{
	conmgr_fd_t *con = arg;

	xassert(con->magic == MAGIC_CON_MGR_FD);

	log_flag(CONMGR, "%s: [%s] free connection input_fd=%d output_fd=%d",
		 __func__, con->name, con->input_fd, con->output_fd);

	FREE_NULL_BUFFER(con->in);
	FREE_NULL_LIST(con->out);
	FREE_NULL_LIST(con->work);
	FREE_NULL_LIST(con->write_complete_work);
	xfree(con->name);
	xfree(con->unix_socket);

	con->magic = ~MAGIC_CON_MGR_FD;
	xfree(con);
}

static void _handle_complete_conns(void)
{
	if (mgr.poll_active) {
		/*
		 * Must wait for all poll() calls to complete or
		 * there may be a use after free of a connection.
		 *
		 * Send signal to break out of any active poll()s.
		 */
		signal_change(true, __func__);
	} else {
		conmgr_fd_t *con;

		/*
		 * Memory cleanup of connections can be done entirely
		 * independently as there should be nothing left in
		 * conmgr that references the connection.
		 */

		while ((con = list_pop(mgr.complete_conns))) {
			/*
			 * Not adding work against connection since this is just
			 * to delete the connection and cleanup and it should
			 * not queue into the connection work queue itself
			 */
			add_work(true, NULL, _connection_fd_delete,
				 CONMGR_WORK_TYPE_FIFO, con,
				 XSTRINGIFY(_connection_fd_delete));
		}
	}
}

static void _handle_new_conns(void)
{
	if (!mgr.inspecting) {
		mgr.inspecting = true;
		add_work(true, NULL, _inspect_connections,
			 CONMGR_WORK_TYPE_FIFO, NULL,
			 XSTRINGIFY(_inspect_connections));
	}

	if (!mgr.poll_active) {
		/* request a listen thread to run */
		log_flag(CONMGR, "%s: queuing up poll", __func__);
		mgr.poll_active = true;
		add_work(true, NULL, _poll_connections, CONMGR_WORK_TYPE_FIFO,
			 NULL, XSTRINGIFY(_poll_connections));
	} else
		log_flag(CONMGR, "%s: poll active already", __func__);
}

static void _handle_events(bool *work)
{
	/* grab counts once */
	int count = list_count(mgr.connections) + list_count(mgr.listen_conns);

	log_flag(CONMGR, "%s: starting connections=%u listen_conns=%u",
		 __func__, list_count(mgr.connections),
		 list_count(mgr.listen_conns));

	if (!list_is_empty(mgr.complete_conns))
		_handle_complete_conns();

	/* start poll thread if needed */
	if (count) {
		_handle_new_conns();
		*work = true;
	}
}

static bool _watch_loop(void)
{
	bool work = false; /* is there any work to do? */

	if (mgr.shutdown_requested) {
		close_all_connections();
		signal_mgr_stop();
	} else if (mgr.quiesced) {
		if (mgr.poll_active) {
			/*
			 * poll() hasn't returned yet so signal it to
			 * stop again and wait for the thread to return
			 */
			pollctl_interrupt(__func__);
			slurm_cond_wait(&mgr.cond, &mgr.mutex);
			return true;
		}

		log_flag(CONMGR, "%s: quiesced", __func__);
		return false;
	}

	_handle_events(&work);

	if (!work && mgr.poll_active) {
		/*
		 * poll() hasn't returned yet so signal it to stop again
		 * and wait for the thread to return
		 */
		signal_change(true, __func__);
		pollctl_interrupt(__func__);
		slurm_cond_wait(&mgr.cond, &mgr.mutex);
		return true;
	}

	if (!work) {
		const int non_watch_workers =
			(mgr.workers.active - mgr.watch_on_worker);

		/*
		 * Avoid watch() ending if there are any other active workers or
		 * any queued work.
		 */

		if ((non_watch_workers > 0) || !list_is_empty(mgr.work)) {
			/* Need to wait for all work to complete */
			work = true;
			log_flag(CONMGR, "%s: waiting on workers:%d work:%d",
				 __func__, non_watch_workers,
				 list_count(mgr.work));
		}
	}

	if (work) {
		/* wait until something happens */
		slurm_cond_wait(&mgr.cond, &mgr.mutex);
		return true;
	}

	log_flag(CONMGR, "%s: cleaning up", __func__);
	signal_change(true, __func__);

	xassert(!mgr.poll_active);
	return false;
}

/* caller must hold conmgr.mutex */
static void _release_watch_request(watch_request_t **wreq_ptr)
{
	watch_request_t *wreq = *wreq_ptr;

	xassert(wreq->magic == MAGIC_WATCH_REQUEST);

	if (wreq->running_on_worker)
		mgr.watch_on_worker--;

	xassert(mgr.watch_on_worker >= 0);
	xassert(mgr.watch_on_worker <= mgr.workers.active);

	wreq->magic = ~MAGIC_WATCH_REQUEST;
	xfree(wreq);
	*wreq_ptr = NULL;
}

extern void watch(conmgr_fd_t *con, conmgr_work_type_t type,
		  conmgr_work_status_t status, const char *tag, void *arg)
{
	watch_request_t *wreq = arg;
	bool shutdown_requested;

	xassert(wreq->magic == MAGIC_WATCH_REQUEST);

	slurm_mutex_lock(&mgr.mutex);

	if (wreq->running_on_worker) {
		mgr.watch_on_worker++;
		xassert(mgr.watch_on_worker > 0);
		xassert(mgr.watch_on_worker <= mgr.workers.active);
	}

	if (mgr.shutdown_requested) {
		_release_watch_request(&wreq);
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	if (mgr.watching) {
		slurm_mutex_unlock(&mgr.mutex);

		if (wreq->blocking)
			wait_for_watch();

		slurm_mutex_lock(&mgr.mutex);
		_release_watch_request(&wreq);
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	mgr.watching = true;

	signal_mgr_start(true);

	while (_watch_loop());

	xassert(mgr.watching);
	mgr.watching = false;

	/* wake all waiting threads */
	slurm_mutex_lock(&mgr.watch_mutex);
	slurm_cond_broadcast(&mgr.watch_cond);
	slurm_mutex_unlock(&mgr.watch_mutex);

	/* Get the value of shutdown_requested while mutex is locked */
	shutdown_requested = mgr.shutdown_requested;

	_release_watch_request(&wreq);
	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: returning shutdown_requested=%c quiesced=%c connections=%u listen_conns=%u",
		 __func__, (shutdown_requested ? 'T' : 'F'),
		 (mgr.quiesced ?  'T' : 'F'), list_count(mgr.connections),
		 list_count(mgr.listen_conns));
}

/*
 * Notify connection manager that there has been a change event
 */
extern void signal_change(bool locked, const char *caller)
{
	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	log_flag(CONMGR, "%s: %s() triggered change event", __func__, caller);

	/* wake up _watch() */
	slurm_cond_broadcast(&mgr.cond);

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}
