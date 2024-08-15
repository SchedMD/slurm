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
#include "src/conmgr/delayed.h"
#include "src/conmgr/events.h"
#include "src/conmgr/mgr.h"
#include "src/conmgr/poll.h"
#include "src/conmgr/signals.h"

static void _listen_accept(conmgr_callback_args_t conmgr_args, void *arg);

static void _on_finish_wrapper(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;

	if (con->is_listen) {
		if (con->events.on_listen_finish)
			con->events.on_listen_finish(con, arg);
	} else if (con->events.on_finish) {
		con->events.on_finish(con, arg);
	}

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

	if (con->is_connected) {
		/* continue on to follow other checks */
	} else if (!con->is_socket || con->can_read || con->can_write ||
		   con->is_listen) {
		/*
		 * Only sockets need special handling to know when they are
		 * connected. Enqueue on_connect callback if defined.
		 */
		con->is_connected = true;

		/* Query outbound MSS now kernel should know the answer */
		if (con->is_socket && (con->output_fd != -1))
			con->mss = fd_get_maxmss(con->output_fd, con->name);

		if (con->is_listen) {
			if (con->events.on_listen_connect) {
				/* disable polling until on_listen_connect() */
				con_set_polling(con, PCTL_TYPE_CONNECTED,
						__func__);
				add_work_con_fifo(true, con, wrap_on_connection,
						  con);

				log_flag(CONMGR, "%s: [%s] Fully connected. Queuing on_listen_connect() callback.",
					 __func__, con->name);
				return 0;
			} else {
				/* follow normal checks */
			}
		} else if (con->events.on_connection) {
			/* disable polling until on_connect() is done */
			con_set_polling(con, PCTL_TYPE_CONNECTED, __func__);
			add_work_con_fifo(true, con, wrap_on_connection, con);

			log_flag(CONMGR, "%s: [%s] Fully connected. Queuing on_connect() callback.",
				 __func__, con->name);
			return 0;
		} else {
			/*
			 * Only watch for incoming data as there can't be any
			 * outgoing data yet
			 */
			xassert(list_is_empty(con->out));

			/*
			 * Continue on to follow other checks as nothing special
			 * needs to be done
			 */
		}
	} else {
		xassert(!con->can_read && !con->can_write);

		/*
		 * Need to wait for connection to establish or fail.
		 *
		 * From man 2 connect:
		 *
		 * It is possible to select(2) or poll(2) for completion by
		 * selecting the socket for writing. After select(2) indicates
		 * writability, use getsockopt(2) to read the SO_ERROR option at
		 * level SOL_SOCKET to determine whether connect() completed
		 * success‐ fully (SO_ERROR is zero) or unsuccessfully
		 */
		con_set_polling(con, PCTL_TYPE_READ_WRITE, __func__);

		log_flag(CONMGR, "%s: [%s] waiting for connection to establish",
			 __func__, con->name);
		return 0;
	}

	/* always do work first once connected */
	if ((count = list_count(con->work))) {
		work_t *work = list_pop(con->work);

		log_flag(CONMGR, "%s: [%s] queuing pending work: %u total",
			 __func__, con->name, count);

		work->status = CONMGR_WORK_STATUS_RUN;
		con->work_active = true; /* unset by _wrap_con_work() */

		handle_work(true, work);
		return 0;
	}

	if (con->extract) {
		/* extraction of file descriptors requested */
		extract_con_fd(con);
		return 0;
	}

	/* handle out going data */
	if (!con->is_listen && (con->output_fd >= 0) &&
	    !list_is_empty(con->out)) {
		if (con->can_write ||
		    (con->polling_output_fd == PCTL_TYPE_UNSUPPORTED)) {
			log_flag(CONMGR, "%s: [%s] %u pending writes",
				 __func__, con->name, list_count(con->out));
			add_work_con_fifo(true, con, handle_write, con);
		} else {
			/*
			 * Only monitor for when connection is ready for writes
			 * as there is no point reading until the write is
			 * complete since it will be ignored.
			 */
			con_set_polling(con, PCTL_TYPE_WRITE_ONLY, __func__);

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
		con_set_polling(con, PCTL_TYPE_CONNECTED, __func__);
		con->can_read = false;

		add_work_con_fifo(true, con, _listen_accept, con);
		return 0;
	}

	/* read as much data as possible before processing */
	if (!con->is_listen && !con->read_eof &&
	    (con->can_read ||
	     (con->polling_input_fd == PCTL_TYPE_UNSUPPORTED))) {
		log_flag(CONMGR, "%s: [%s] queuing read", __func__, con->name);
		/* reset if data has already been tried if about to read data */
		con->on_data_tried = false;
		add_work_con_fifo(true, con, handle_read, con);
		return 0;
	}

	/* handle already read data */
	if (!con->is_listen && get_buf_offset(con->in) && !con->on_data_tried) {
		log_flag(CONMGR, "%s: [%s] need to process %u bytes",
			 __func__, con->name, get_buf_offset(con->in));
		add_work_con_fifo(true, con, wrap_on_data, con);
		return 0;
	}

	if (!con->read_eof) {
		xassert(con->input_fd != -1);

		/* must wait until poll allows read from this socket */
		if (con->is_listen) {
			con_set_polling(con, PCTL_TYPE_LISTEN, __func__);
			log_flag(CONMGR, "%s: [%s] waiting for new connection",
				 __func__, con->name);
		} else {
			con_set_polling(con, PCTL_TYPE_READ_ONLY, __func__);
			log_flag(CONMGR, "%s: [%s] waiting for events: pending_read=%u pending_writes=%u work_active=%c can_read=%c can_write=%c on_data_tried=%c work=%d write_complete_work=%d",
				 __func__, con->name, get_buf_offset(con->in),
				 list_count(con->out),
				 (con->work_active ? 'T' : 'F'),
				 (con->can_read ? 'T' : 'F'),
				 (con->can_write ? 'T' : 'F'),
				 (con->on_data_tried ? 'T' : 'F'),
				 list_count(con->work),
				 list_count(con->write_complete_work));
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
		log_flag(CONMGR, "%s: [%s] waiting for %s",
			 __func__, con->name,
			 (con->is_listen ? "on_finish()" :
			  "on_listen_finish()"));
		return 0;
	}

	if (con->arg) {
		log_flag(CONMGR, "%s: [%s] queuing up %s",
			 __func__, con->name, (con->is_listen ? "on_finish()" :
			  "on_listen_finish()"));

		con->wait_on_finish = true;

		/* notify caller of closing */
		add_work_con_fifo(true, con, _on_finish_wrapper, con->arg);
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
		con_set_polling(con, PCTL_TYPE_NONE, __func__);

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
static void _listen_accept(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	slurm_addr_t addr = {0};
	socklen_t addrlen = sizeof(addr);
	int fd;
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

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED) {
		log_flag(CONMGR, "%s: [%s] closing new connection to %pA during shutdown",
				 __func__, con->name, &addr);
		fd_close(&fd);
		return;
	}

	/* hand over FD for normal processing */
	if (add_connection(con->type, con, fd, fd, con->events,
			   &addr, addrlen, false, unix_path,
			   con->new_arg) != SLURM_SUCCESS) {
		log_flag(CONMGR, "%s: [fd:%d] unable to a register new connection",
			 __func__, fd);
		return;
	}
}

/*
 * Inspect all connection states and apply actions required
 */

static void _inspect_connections(conmgr_callback_args_t conmgr_args, void *arg)
{
	bool send_signal = false;

	slurm_mutex_lock(&mgr.mutex);
	xassert(mgr.inspecting);

	if (list_transfer_match(mgr.listen_conns, mgr.complete_conns,
				_handle_connection, NULL))
		send_signal = true;
	if (list_transfer_match(mgr.connections, mgr.complete_conns,
				_handle_connection, NULL))
		send_signal = true;

	mgr.inspecting = false;

	if (send_signal)
		EVENT_SIGNAL(&mgr.watch_sleep);
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
		con_close_on_poll_error(con, fd);
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

		/* Avoid setting read_eof if can_read=true */
		if (!con->can_read && !con->read_eof)
			con->read_eof = pollctl_events_has_hangup(events);
	}
	if (fd == con->output_fd)
		con->can_write = pollctl_events_can_write(events);

	log_flag(CONMGR, "%s: [%s] fd=%u can_read=%s can_write=%s read_eof=%s",
		 __func__, con->name, fd, (con->can_read ? "T" : "F"),
		 (con->can_write ? "T" : "F"), (con->read_eof ? "T" : "F"));

	return SLURM_SUCCESS;
}

/* caller must hold mgr.mutex lock */
static bool _is_poll_interrupt(void)
{
	return (mgr.quiesced || mgr.shutdown_requested ||
		(mgr.waiting_on_work && (mgr.workers.active == 1)));
}

/* Poll all connections */
static void _poll_connections(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc;

	xassert(!conmgr_args.con);

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

	EVENT_SIGNAL(&mgr.watch_sleep);
	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: poll done", __func__);
}

extern void wait_for_watch(void)
{
	slurm_mutex_lock(&mgr.mutex);
	while (mgr.watch_thread)
		EVENT_WAIT(&mgr.watch_return, &mgr.mutex);
	slurm_mutex_unlock(&mgr.mutex);
}

static void _connection_fd_delete(conmgr_callback_args_t conmgr_args, void *arg)
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
		add_work_fifo(true, _connection_fd_delete, con);
	}
}

static bool _handle_events(void)
{
	/* grab counts once */
	int count = list_count(mgr.connections) + list_count(mgr.listen_conns);

	log_flag(CONMGR, "%s: connections=%u listen_conns=%u complete_conns=%u",
		 __func__, list_count(mgr.connections),
		 list_count(mgr.listen_conns), list_count(mgr.complete_conns));

	if (!list_is_empty(mgr.complete_conns))
		_handle_complete_conns();

	if (!count)
		return false;

	if (!mgr.inspecting) {
		mgr.inspecting = true;
		add_work_fifo(true, _inspect_connections, NULL);
	}

	/* start poll thread if needed */
	if (!mgr.poll_active) {
		/* request a listen thread to run */
		log_flag(CONMGR, "%s: queuing up poll", __func__);
		mgr.poll_active = true;

		add_work_fifo(true, _poll_connections, NULL);
	} else
		log_flag(CONMGR, "%s: poll active already", __func__);

	return true;
}

static bool _watch_loop(void)
{
	if (mgr.shutdown_requested) {
		signal_mgr_stop();
		cancel_delayed_work();
		close_all_connections();
	}

	if (!mgr.quiesced && !list_is_empty(mgr.quiesced_work)) {
		mgr.quiesced = true;
		log_flag(CONMGR, "%s: BEGIN: quiesced state", __func__);
	}

	if (mgr.quiesced) {
		xassert(!list_is_empty(mgr.quiesced_work));

		if (mgr.workers.active) {
			log_flag(CONMGR, "%s: quiesced state waiting on workers:%d quiesced_work:%u",
				 __func__, mgr.workers.active,
				 list_count(mgr.quiesced_work));
			mgr.waiting_on_work = true;
			return true;
		}

		run_quiesced_work();
		mgr.quiesced = false;
		log_flag(CONMGR, "%s: END: quiesced state", __func__);
		return true;
	}

	xassert(list_is_empty(mgr.quiesced_work));

	if (_handle_events())
		return true;

	/*
	 * Avoid watch() ending if there are any other active workers or
	 * any queued work.
	 */

	if (mgr.workers.active || !list_is_empty(mgr.work) ||
	    !list_is_empty(mgr.delayed_work)) {
		/* Need to wait for all work/workers to complete */
		log_flag(CONMGR, "%s: waiting on workers:%d work:%d delayed_work:%d",
			 __func__, mgr.workers.active,
			 list_count(mgr.delayed_work), list_count(mgr.work));
		mgr.waiting_on_work = true;
		return true;
	}

	log_flag(CONMGR, "%s: cleaning up", __func__);

	xassert(!mgr.poll_active);
	return false;
}

extern void *watch(void *arg)
{
	slurm_mutex_lock(&mgr.mutex);

	xassert(mgr.watch_thread == pthread_self());

	if (mgr.shutdown_requested) {
		slurm_mutex_unlock(&mgr.mutex);
		return NULL;
	}

	add_work_fifo(true, signal_mgr_start, NULL);

	while (_watch_loop()) {
		if (mgr.poll_active && _is_poll_interrupt()) {
			/*
			 * poll() hasn't returned yet but we want to
			 * shutdown. Send interrupt before sleeping or
			 * watch() may end up sleeping forever.
			 */
			pollctl_interrupt(__func__);
		}

		log_flag(CONMGR, "%s: waiting for new events: workers:%d/%d work:%d delayed_work:%d connections:%d listeners:%d complete:%d polling:%c inspecting:%c shutdown_requested:%c quiesced:%c[%u] waiting_on_work:%c",
				 __func__, mgr.workers.active,
				 mgr.workers.total, list_count(mgr.work),
				 list_count(mgr.delayed_work),
				 list_count(mgr.connections),
				 list_count(mgr.listen_conns),
				 list_count(mgr.complete_conns),
				 (mgr.poll_active ? 'T' : 'F'),
				 (mgr.inspecting ? 'T' : 'F'),
				 (mgr.shutdown_requested ? 'T' : 'F'),
				 (mgr.quiesced ? 'T' : 'F'),
				 list_count(mgr.quiesced_work),
				 (mgr.waiting_on_work ? 'T' : 'F'));

		EVENT_WAIT(&mgr.watch_sleep, &mgr.mutex);
		mgr.waiting_on_work = false;
	}

	log_flag(CONMGR, "%s: returning shutdown_requested=%c connections=%u listen_conns=%u",
		 __func__, (mgr.shutdown_requested ? 'T' : 'F'),
		 list_count(mgr.connections), list_count(mgr.listen_conns));

	xassert(mgr.watch_thread == pthread_self());
	mgr.watch_thread = 0;

	EVENT_BROADCAST(&mgr.watch_return);
	slurm_mutex_unlock(&mgr.mutex);

	return NULL;
}
