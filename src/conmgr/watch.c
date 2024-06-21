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
#include <poll.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "src/common/fd.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

typedef void (*on_poll_event_t)(int fd, conmgr_fd_t *con, short revents);

/* simple struct to keep track of fds */
typedef struct {
#define MAGIC_POLL_ARGS 0xB201444A
	int magic; /* MAGIC_POLL_ARGS */
	struct pollfd *fds;
	int nfds;
} poll_args_t;

static void _handle_poll_event_error(int fd, conmgr_fd_t *con, short revents)
{
	int err = SLURM_ERROR;
	int rc;

	if (revents & POLLNVAL) {
		error("%s: [%s] %sconnection invalid",
		      __func__, (con->is_listen ? "listening " : ""),
		      con->name);
	} else if (con->is_socket && (rc = fd_get_socket_error(fd, &err))) {
		/* connection may have got RST */
		error("%s: [%s] poll error: fd_get_socket_error() failed %s",
		      __func__, con->name, slurm_strerror(rc));
	} else {
		error("%s: [%s] poll error: %s",
		      __func__, con->name, slurm_strerror(err));
	}

	/*
	 * Socket must not continue to be considered valid to avoid a
	 * infinite calls to poll() which will immidiatly fail. Close
	 * the relavent file descriptor and remove from connection.
	 */
	if (close(fd)) {
		log_flag(CONMGR, "%s: [%s] input_fd=%d output_fd=%d calling close(%d) failed after poll() returned %s%s%s: %m",
			 __func__, con->name, con->input_fd, con->output_fd, fd,
			 ((revents & POLLNVAL) ? "POLLNVAL" : ""),
			 ((revents & POLLNVAL) && (revents & POLLERR) ? "&" : ""),
			 ((revents & POLLERR) ? "POLLERR" : ""));
	}

	if (con->input_fd == fd)
		con->input_fd = -1;
	if (con->output_fd == fd)
		con->output_fd = -1;

	close_con(true, con);
}

/*
 * Event on a processing socket.
 * mgr must be locked.
 */
static void _handle_poll_event(int fd, conmgr_fd_t *con, short revents)
{
	con->can_read = false;
	con->can_write = false;

	if ((revents & POLLNVAL) || (revents & POLLERR)) {
		_handle_poll_event_error(fd, con, revents);
		return;
	}

	if (fd == con->input_fd)
		con->can_read = revents & POLLIN || revents & POLLHUP;
	if (fd == con->output_fd)
		con->can_write = revents & POLLOUT;

	log_flag(CONMGR, "%s: [%s] fd=%u can_read=%s can_write=%s",
		 __func__, con->name, fd, (con->can_read ? "T" : "F"),
		 (con->can_write ? "T" : "F"));
}

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
	if (!con->is_listen && con->output_fd != -1 &&
	    !list_is_empty(con->out)) {
		if (con->can_write) {
			log_flag(CONMGR, "%s: [%s] %u pending writes",
				 __func__, con->name, list_count(con->out));
			add_work(true, con, handle_write,
				 CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
				 XSTRINGIFY(handle_write));
		} else {
			/* must wait until poll allows write of this socket */
			log_flag(CONMGR, "%s: [%s] waiting for %u writes",
				 __func__, con->name, list_count(con->out));
		}
		return 0;
	}

	if ((count = list_count(con->write_complete_work))) {
		log_flag(CONMGR, "%s: [%s] queuing pending write complete work: %u total",
			 __func__, con->name, count);

		list_transfer(con->work, con->write_complete_work);
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
		/* must wait until poll allows read from this socket */
		if (con->is_listen)
			log_flag(CONMGR, "%s: [%s] waiting for new connection",
				 __func__, con->name);
		else
			log_flag(CONMGR, "%s: [%s] waiting to read pending_read=%u pending_writes=%u work_active=%c",
				 __func__, con->name, get_buf_offset(con->in),
				 list_count(con->out),
				 (con->work_active ? 'T' : 'F'));
		return 0;
	}

	/*
	 * Close out the incoming to avoid any new work coming into the
	 * connection.
	 */
	if (con->input_fd != -1) {
		log_flag(CONMGR, "%s: [%s] closing incoming on connection input_fd=%d",
			 __func__, con->name, con->input_fd);

		if (close(con->input_fd) == -1)
			log_flag(CONMGR, "%s: [%s] unable to close input fd %d: %m",
				 __func__, con->name, con->input_fd);

		/* if there is only 1 fd: forget it too */
		if (con->input_fd == con->output_fd)
			con->output_fd = -1;

		/* forget invalid fd */
		con->input_fd = -1;
	}

	if (con->wait_on_finish) {
		log_flag(CONMGR, "%s: [%s] waiting for on_finish()",
			 __func__, con->name);
		return 0;
	}

	if (!con->is_listen && con->arg) {
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
static void _inspect_connections(void *x)
{
	slurm_mutex_lock(&mgr.mutex);

	if (list_transfer_match(mgr.connections, mgr.complete_conns,
				_handle_connection, NULL))
		slurm_cond_broadcast(&mgr.cond);
	mgr.inspecting = false;

	slurm_mutex_unlock(&mgr.mutex);
}

/*
 * Event on a listen only socket
 * mgr must be locked.
 */
static void _handle_listen_event(int fd, conmgr_fd_t *con, short revents)
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
		log_flag(CONMGR, "%s: [%s] listen has incoming connection",
			 __func__, con->name);
		add_work(true, con, _listen_accept,
			 CONMGR_WORK_TYPE_CONNECTION_FIFO, con,
			 XSTRINGIFY(_listen_accept));
		return;
	} else /* should never happen */
		log_flag(CONMGR, "%s: [%s] listen unexpected revents: 0x%04x",
			 __func__, con->name, revents);

	close_con(true, con);
}

static void _handle_event_pipe(const struct pollfd *fds_ptr, const char *tag,
			       const char *name)
{
	if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
		char *flags = poll_revents_to_str(fds_ptr->revents);

		log_flag(CONMGR, "%s: [%s] signal pipe %s flags:%s",
			 __func__, tag, name, flags);

		/* _watch() will actually read the input */

		xfree(flags);
	}
}

/*
 * Find by matching fd to connection
 */
static int _find_by_fd(void *x, void *key)
{
	conmgr_fd_t *con = x;
	int fd = *(int *)key;
	return (con->input_fd == fd) || (con->output_fd == fd);
}

/*
 * Handle poll and events
 *
 * NOTE: mgr mutex must not be locked but will be locked upon return
 */
static void _poll(poll_args_t *args, list_t *fds, on_poll_event_t on_poll,
		  const char *tag)
{
	int rc = SLURM_SUCCESS;
	struct pollfd *fds_ptr = NULL;
	conmgr_fd_t *con;
	int signal_fd, event_fd;

again:
	xassert(args->magic == MAGIC_POLL_ARGS);
	rc = poll(args->fds, args->nfds, -1);
	if (rc == -1) {
		bool exit_on_error;

		slurm_mutex_lock(&mgr.mutex);
		exit_on_error = mgr.exit_on_error;
		slurm_mutex_unlock(&mgr.mutex);

		if ((errno == EINTR) && !exit_on_error) {
			log_flag(CONMGR, "%s: [%s] poll interrupted. Trying again.",
				 __func__, tag);
			goto again;
		}

		fatal("%s: [%s] unable to poll listening sockets: %m",
		      __func__, tag);
	}

	if (rc == 0) {
		log_flag(CONMGR, "%s: [%s] poll timed out", __func__, tag);
		return;
	}

	slurm_mutex_lock(&mgr.mutex);
	signal_fd = mgr.signal_fd[0];
	event_fd = mgr.event_fd[0];
	slurm_mutex_unlock(&mgr.mutex);

	fds_ptr = args->fds;
	for (int i = 0; i < args->nfds; i++, fds_ptr++) {

		if (!fds_ptr->revents)
			continue;

		if (fds_ptr->fd == signal_fd) {
			mgr.signaled = true;
			_handle_event_pipe(fds_ptr, tag, "CAUGHT_SIGNAL");
		} else if (fds_ptr->fd == event_fd)
			_handle_event_pipe(fds_ptr, tag, "CHANGE_EVENT");
		else if ((con = list_find_first(fds, _find_by_fd,
						&fds_ptr->fd))) {
			if (slurm_conf.debug_flags & DEBUG_FLAG_NET) {
				char *flags = poll_revents_to_str(
					fds_ptr->revents);
				log_flag(CONMGR, "%s: [%s->%s] poll event detect flags:%s",
					 __func__, tag, con->name, flags);
				xfree(flags);
			}
			slurm_mutex_lock(&mgr.mutex);
			on_poll(fds_ptr->fd, con, fds_ptr->revents);
			/*
			 * signal that something might have happened and to
			 * restart listening
			 * */
			signal_change(true);
			slurm_mutex_unlock(&mgr.mutex);
		} else
			/* FD probably got closed between poll start and now */
			log_flag(CONMGR, "%s: [%s] unable to find connection for fd=%u",
				 __func__, tag, fds_ptr->fd);
	}
}

static void _init_poll_fds(poll_args_t *args, struct pollfd **fds_ptr_p,
			   int conn_count)
{
	struct pollfd *fds_ptr = NULL;

	xrecalloc(args->fds, ((conn_count * 2) + 2), sizeof(*args->fds));

	args->nfds = 0;
	fds_ptr = args->fds;

	/* Add signal fd */
	fds_ptr->fd = mgr.signal_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	/* Add event fd */
	fds_ptr->fd = mgr.event_fd[0];
	fds_ptr->events = POLLIN;
	fds_ptr++;
	args->nfds++;

	*fds_ptr_p = fds_ptr;
}

/*
 * Poll all processing connections sockets and
 * signal_fd and event_fd.
 */
static void _poll_connections(void *x)
{
	poll_args_t *args = x;
	struct pollfd *fds_ptr = NULL;
	conmgr_fd_t *con;
	int count;
	list_itr_t *itr;

	xassert(args->magic == MAGIC_POLL_ARGS);

	slurm_mutex_lock(&mgr.mutex);

	/* grab counts once */
	if (!(count = list_count(mgr.connections))) {
		log_flag(CONMGR, "%s: no connections to poll()", __func__);
		goto done;
	}

	if (mgr.signaled) {
		log_flag(CONMGR, "%s: skipping poll() due to signal", __func__);
		goto done;
	}

	if (mgr.quiesced) {
		log_flag(CONMGR, "%s: skipping poll() while quiesced",
			 __func__);
		goto done;
	}

	_init_poll_fds(args, &fds_ptr, count);

	/*
	 * populate sockets with !work_active
	 */
	itr = list_iterator_create(mgr.connections);
	while ((con = list_next(itr))) {
		if (con->work_active)
			continue;

		log_flag(CONMGR, "%s: [%s] poll read_eof=%s input=%u outputs=%u work_active=%c",
			 __func__, con->name, (con->read_eof ? "T" : "F"),
			 get_buf_offset(con->in), list_count(con->out),
			 (con->work_active ? 'T' : 'F'));

		if (con->input_fd == con->output_fd) {
			/* if fd is same, only poll it */
			fds_ptr->fd = con->input_fd;
			fds_ptr->events = 0;

			if (con->input_fd != -1)
				fds_ptr->events |= POLLIN;
			if (!list_is_empty(con->out))
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

			if (!list_is_empty(con->out)) {
				fds_ptr->fd = con->output_fd;
				fds_ptr->events = POLLOUT;
				fds_ptr++;
				args->nfds++;
			}
		}
	}
	list_iterator_destroy(itr);

	if (args->nfds == 2) {
		log_flag(CONMGR, "%s: skipping poll() due to no open file descriptors for %d connections",
			 __func__, count);
		goto done;
	}

	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: polling %u file descriptors for %u connections",
		 __func__, args->nfds, count);

	_poll(args, mgr.connections, _handle_poll_event, __func__);

	slurm_mutex_lock(&mgr.mutex);
done:
	mgr.poll_active = false;
	/* notify _watch it can run but don't send signal to event PIPE*/
	slurm_cond_broadcast(&mgr.cond);
	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: poll done", __func__);
}

/*
 * Poll all listening sockets
 */
static void _listen(void *x)
{
	poll_args_t *args = x;
	struct pollfd *fds_ptr = NULL;
	conmgr_fd_t *con;
	int count;
	list_itr_t *itr;

	xassert(args->magic == MAGIC_POLL_ARGS);

	slurm_mutex_lock(&mgr.mutex);

	/* if shutdown has been requested: then don't listen() anymore */
	if (mgr.shutdown_requested) {
		log_flag(CONMGR, "%s: caught shutdown. closing %u listeners",
			 __func__, list_count(mgr.listen_conns));
		goto cleanup;
	}

	if (mgr.signaled) {
		log_flag(CONMGR, "%s: skipping poll() to pending signal",
			 __func__);
		goto cleanup;
	}

	if (mgr.quiesced) {
		log_flag(CONMGR, "%s: skipping poll() while quiesced",
			 __func__);
		goto cleanup;
	}

	/* grab counts once */
	count = list_count(mgr.listen_conns);

	log_flag(CONMGR, "%s: listeners=%u", __func__, count);

	if (count == 0) {
		/* nothing to do here */
		log_flag(CONMGR, "%s: no listeners found", __func__);
		goto cleanup;
	}

	_init_poll_fds(args, &fds_ptr, count);

	/* populate listening sockets */
	itr = list_iterator_create(mgr.listen_conns);
	while ((con = list_next(itr))) {
		/* already accept queued or listener already closed */
		if (con->work_active || con->read_eof)
			continue;

		fds_ptr->fd = con->input_fd;
		fds_ptr->events = POLLIN;

		log_flag(CONMGR, "%s: [%s] listening", __func__, con->name);

		fds_ptr++;
		args->nfds++;
	}
	list_iterator_destroy(itr);

	if (args->nfds == 2) {
		log_flag(CONMGR, "%s: deferring listen due to all sockets are queued to call accept or closed",
			 __func__);
		goto cleanup;
	}

	slurm_mutex_unlock(&mgr.mutex);

	log_flag(CONMGR, "%s: polling %u/%u file descriptors",
		 __func__, args->nfds, (count + 2));

	/* _poll() will lock mgr.mutex */
	_poll(args, mgr.listen_conns, _handle_listen_event, __func__);

	slurm_mutex_lock(&mgr.mutex);
cleanup:
	mgr.listen_active = false;
	signal_change(true);
	slurm_mutex_unlock(&mgr.mutex);
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

static void _connection_fd_delete(void *x)
{
	conmgr_fd_t *con = x;

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
	if (mgr.listen_active || mgr.poll_active) {
		/*
		 * Must wait for all poll() calls to complete or
		 * there may be a use after free of a connection.
		 *
		 * Send signal to break out of any active poll()s.
		 */
		signal_change(true);
	} else {
		conmgr_fd_t *con;

		/*
		 * Memory cleanup of connections can be done entirely
		 * independently as there should be nothing left in
		 * conmgr that references the connection.
		 */

		while ((con = list_pop(mgr.complete_conns)))
			queue_func(true, _connection_fd_delete, con,
				   XSTRINGIFY(_connection_fd_delete));
	}
}

static void _handle_listen_conns(poll_args_t **listen_args_p, int conn_count)
{
	if (!*listen_args_p) {
		*listen_args_p = xmalloc(sizeof(**listen_args_p));
		(*listen_args_p)->magic = MAGIC_POLL_ARGS;
	}

	/* run any queued work */
	list_transfer_match(mgr.listen_conns, mgr.complete_conns,
			    _handle_connection, NULL);

	if (!mgr.listen_active) {
		/* only try to listen if number connections is below limit */
		if (conn_count >= mgr.max_connections)
			log_flag(CONMGR, "%s: deferring accepting new connections until count is below max: %u/%u",
				 __func__, conn_count, mgr.max_connections);
		else { /* request a listen thread to run */
			log_flag(CONMGR, "%s: queuing up listen",
				 __func__);
			mgr.listen_active = true;
			queue_func(true, _listen, *listen_args_p,
				   XSTRINGIFY(_listen));
		}
	} else
		log_flag(CONMGR, "%s: listeners active already", __func__);
}

static void _handle_new_conns(poll_args_t **poll_args_p)
{
	if (!*poll_args_p) {
		*poll_args_p = xmalloc(sizeof(**poll_args_p));
		(*poll_args_p)->magic = MAGIC_POLL_ARGS;
	}

	if (!mgr.inspecting) {
		mgr.inspecting = true;
		queue_func(true, _inspect_connections, NULL,
			   XSTRINGIFY(_inspect_connections));
	}

	if (!mgr.poll_active) {
		/* request a listen thread to run */
		log_flag(CONMGR, "%s: queuing up poll", __func__);
		mgr.poll_active = true;
		queue_func(true, _poll_connections, *poll_args_p,
			   XSTRINGIFY(_poll_connections));
	} else
		log_flag(CONMGR, "%s: poll active already", __func__);
}

static void _handle_events(poll_args_t **listen_args_p,
			   poll_args_t **poll_args_p,
			   bool *work)
{
	int count;

	/* grab counts once */
	count = list_count(mgr.connections);

	log_flag(CONMGR, "%s: starting connections=%u listen_conns=%u",
		 __func__, count, list_count(mgr.listen_conns));

	*work = false;

	if (!list_is_empty(mgr.complete_conns))
		_handle_complete_conns();

	/* start listen thread if needed */
	if (!list_is_empty(mgr.listen_conns)) {
		_handle_listen_conns(listen_args_p, count);
		*work = true;
	}

	/* start poll thread if needed */
	if (count) {
		_handle_new_conns(poll_args_p);
		*work = true;
	}
}

static void _read_event_fd(void)
{
	int event_read;
	char buf[100]; /* buffer for event_read */

	/*
	 * Only clear signal and event pipes once both polls
	 * are done.
	 */
	event_read = read(mgr.event_fd[0], buf, sizeof(buf));
	if (event_read > 0) {
		log_flag(CONMGR, "%s: detected %u events from event fd",
			 __func__, event_read);
		mgr.event_signaled = 0;
	} else if (!event_read || (errno == EWOULDBLOCK) ||
		   (errno == EAGAIN))
		log_flag(CONMGR, "%s: nothing to read from event fd", __func__);
	else if (errno == EINTR)
		log_flag(CONMGR, "%s: try again on read of event fd: %m",
			 __func__);
	else
		fatal("%s: unable to read from event fd: %m",
		      __func__);
}

static bool _watch_loop(poll_args_t **listen_args_p, poll_args_t **poll_args_p)
{
	bool work = false; /* is there any work to do? */

	if (mgr.shutdown_requested)
		close_all_connections();
	else if (mgr.quiesced) {
		if (mgr.poll_active || mgr.listen_active) {
			/*
			 * poll() hasn't returned yet so signal it to
			 * stop again and wait for the thread to return
			 */
			signal_change(true);
			slurm_cond_wait(&mgr.cond, &mgr.mutex);
			return true;
		}

		log_flag(CONMGR, "%s: quiesced", __func__);
		return false;
	}

	if (!mgr.poll_active && !mgr.listen_active) {
		_read_event_fd();
		if (mgr.signaled) {
			if (!mgr.read_signals_active) {
				mgr.read_signals_active = true;
				queue_func(true, handle_signals, NULL,
					   XSTRINGIFY(handle_signals));
			}
			work = true;
		}
	}

	_handle_events(listen_args_p, poll_args_p, &work);

	if (!work && (mgr.poll_active || mgr.listen_active)) {
		/*
		 * poll() hasn't returned yet so signal it to stop again
		 * and wait for the thread to return
		 */
		signal_change(true);
		slurm_cond_wait(&mgr.cond, &mgr.mutex);
		return true;
	}

	if (work) {
		/* wait until something happens */
		slurm_cond_wait(&mgr.cond, &mgr.mutex);
		return true;
	}

	log_flag(CONMGR, "%s: cleaning up", __func__);
	signal_change(true);

	xassert(!mgr.poll_active);
	xassert(!mgr.listen_active);
	return false;
}

extern void watch(void *blocking)
{
	bool shutdown_requested;
	poll_args_t *listen_args = NULL;
	poll_args_t *poll_args = NULL;

	slurm_mutex_lock(&mgr.mutex);

	if (mgr.shutdown_requested) {
		slurm_mutex_unlock(&mgr.mutex);
		return;
	}

	if (mgr.watching) {
		if (blocking) {
			wait_for_watch();
		} else {
			slurm_mutex_unlock(&mgr.mutex);
		}

		return;
	}

	mgr.watching = true;

	init_signal_handler();

	while (_watch_loop(&listen_args, &poll_args));

	fini_signal_handler();

	xassert(mgr.watching);
	mgr.watching = false;

	/* wake all waiting threads */
	slurm_mutex_lock(&mgr.watch_mutex);
	slurm_cond_broadcast(&mgr.watch_cond);
	slurm_mutex_unlock(&mgr.watch_mutex);

	/* Get the value of shutdown_requested while mutex is locked */
	shutdown_requested = mgr.shutdown_requested;
	slurm_mutex_unlock(&mgr.mutex);

	if (poll_args) {
		xassert(poll_args->magic == MAGIC_POLL_ARGS);
		poll_args->magic = ~MAGIC_POLL_ARGS;
		xfree(poll_args->fds);
		xfree(poll_args);
	}

	if (listen_args) {
		xassert(listen_args->magic == MAGIC_POLL_ARGS);
		listen_args->magic = ~MAGIC_POLL_ARGS;
		xfree(listen_args->fds);
		xfree(listen_args);
	}

	log_flag(CONMGR, "%s: returning shutdown_requested=%c quiesced=%c connections=%u listen_conns=%u",
		 __func__, (shutdown_requested ? 'T' : 'F'),
		 (mgr.quiesced ?  'T' : 'F'), list_count(mgr.connections),
		 list_count(mgr.listen_conns));
}
