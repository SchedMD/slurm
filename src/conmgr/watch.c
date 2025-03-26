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
#include "config.h"

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <limits.h>
#include <stdint.h>
#include <sys/un.h>
#include <sys/socket.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/fd.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/timers.h"
#include "src/common/xmalloc.h"

#include "src/conmgr/conmgr.h"
#include "src/conmgr/delayed.h"
#include "src/conmgr/events.h"
#include "src/conmgr/mgr.h"
#include "src/conmgr/polling.h"
#include "src/conmgr/signals.h"

#define CTIME_STR_LEN 72

typedef struct {
#define MAGIC_HANDLE_CONNECTION 0xaaaffb03
	int magic; /* MAGIC_HANDLE_CONNECTION */
	/* output of timespec_now() in _inspect_connections() */
	timespec_t time;
} handle_connection_args_t;

static void _listen_accept(conmgr_callback_args_t conmgr_args, void *arg);

static bool _handle_time_limit(const handle_connection_args_t *args,
			       const struct timespec timestamp,
			       const struct timespec limit)
{
	const struct timespec deadline = timespec_add(timestamp, limit);

	if (timespec_is_after(args->time, deadline))
		return true;

	if (!mgr.watch_max_sleep.tv_sec ||
	    timespec_is_after(mgr.watch_max_sleep, deadline))
		mgr.watch_max_sleep = deadline;

	return false;
}

static void _on_finish_wrapper(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;

	if (con_flag(con, FLAG_IS_LISTEN)) {
		if (con->events->on_listen_finish)
			con->events->on_listen_finish(con, arg);
	} else if (con->events->on_finish) {
		con->events->on_finish(con, arg);
	}

	slurm_mutex_lock(&mgr.mutex);
	con_unset_flag(con, FLAG_WAIT_ON_FINISH);
	/* on_finish must free arg */
	con->arg = NULL;
	slurm_mutex_unlock(&mgr.mutex);
}

static void _on_write_complete_work(conmgr_callback_args_t conmgr_args,
				    void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;

	slurm_mutex_lock(&mgr.mutex);

	if (list_is_empty(con->write_complete_work)) {
		slurm_mutex_unlock(&mgr.mutex);

		log_flag(CONMGR, "%s: [%s] skipping with 0 write complete work pending",
			 __func__, con->name);
		return;
	}

	if ((con->polling_output_fd != PCTL_TYPE_UNSUPPORTED) &&
	    ((con->output_fd >= 0) && !con_flag(con, FLAG_CAN_WRITE))) {
		slurm_mutex_unlock(&mgr.mutex);

		/*
		 * if FLAG_CAN_WRITE is not set, then kernel is telling us that
		 * the outgoing buffer hasn't been flushed yet
		 */
		log_flag(CONMGR, "%s: [%s] waiting for FLAG_CAN_WRITE",
			 __func__, con->name);
		return;
	}

	if ((con->output_fd >= 0) &&
	    con_flag(con, FLAG_CAN_QUERY_OUTPUT_BUFFER)) {
		int rc = EINVAL;
		int bytes = -1;
		int output_fd = con->output_fd;

		slurm_mutex_unlock(&mgr.mutex);

		if (output_fd >= 0)
			rc = fd_get_buffered_output_bytes(output_fd, &bytes,
							  con->name);

		slurm_mutex_lock(&mgr.mutex);

		if (rc) {
			log_flag(CONMGR, "%s: [%s] unable to query output_fd[%d] outgoing buffer remaining: %s. Queuing pending %u write complete work",
				 __func__, con->name, output_fd,
				 slurm_strerror(rc),
				 list_count(con->write_complete_work));

			con_unset_flag(con, FLAG_CAN_QUERY_OUTPUT_BUFFER);
		} else if (bytes > 0) {
			log_flag(CONMGR, "%s: [%s] output_fd[%d] has %d bytes in outgoing buffer remaining. Retrying in %us",
				 __func__, con->name, output_fd, bytes,
				 mgr.conf_delay_write_complete);

			/* Turn off Nagle while we wait for buffer to flush */
			if (con_flag(con, FLAG_IS_SOCKET) &&
			    !con_flag(con, FLAG_TCP_NODELAY)) {
				slurm_mutex_unlock(&mgr.mutex);
				(void) net_set_nodelay(output_fd, true,
						       con->name);
				slurm_mutex_lock(&mgr.mutex);
			}

			add_work_con_delayed_fifo(true, con,
						  _on_write_complete_work, NULL,
						  mgr.conf_delay_write_complete,
						  0);
			slurm_mutex_unlock(&mgr.mutex);
			return;
		} else {
			xassert(!bytes);

			/* Turn back on Nagle every time in case it got set */
			if (con_flag(con, FLAG_IS_SOCKET) &&
			    !con_flag(con, FLAG_TCP_NODELAY)) {
				slurm_mutex_unlock(&mgr.mutex);
				(void) net_set_nodelay(output_fd, false,
						       con->name);
				slurm_mutex_lock(&mgr.mutex);
			}

			log_flag(CONMGR, "%s: [%s] output_fd[%d] has 0 bytes in outgoing buffer remaining. Queuing pending %u write complete work",
				 __func__, con->name, output_fd,
				 list_count(con->write_complete_work));
		}
	} else {
		log_flag(CONMGR, "%s: [%s] queuing pending %u write complete work",
			 __func__, con->name,
			 list_count(con->write_complete_work));
	}

	list_transfer(con->work, con->write_complete_work);

	EVENT_SIGNAL(&mgr.watch_sleep);
	slurm_mutex_unlock(&mgr.mutex);
}

static void _update_mss(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;

	if (con_flag(con, FLAG_IS_SOCKET) && (con->output_fd != -1))
		con->mss = fd_get_maxmss(con->output_fd, con->name);
}

static void _close_output_fd(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	int output_fd = (uint64_t) arg;
	int rc = SLURM_SUCCESS;

	xassert(output_fd >= 0);
	xassert(output_fd < NO_VAL64);

	log_flag(CONMGR, "%s: [%s] closing connection output_fd=%d",
		 __func__, con->name, output_fd);

	/*
	 * From man 2 close:
	 * > A careful programmer who wants to know about I/O errors may precede
	 * > close() with a call to fsync(2)
	 *
	 * Avoid fsync() on pipe()s and chr devices per man page:
	 * > fd is bound to a special file (e.g., a pipe, FIFO, or socket) which
	 * > does not support synchronization.
	 */
	if (!con_flag(con, FLAG_IS_SOCKET) && !con_flag(con, FLAG_IS_FIFO) &&
	    !con_flag(con, FLAG_IS_CHR)) {
		do {
			if (fsync(output_fd)) {
				rc = errno;
				log_flag(CONMGR, "%s: [%s] unable to fsync(fd:%d): %s",
					 __func__, con->name, output_fd,
					 slurm_strerror(rc));

				if (rc == EBADF)
					output_fd = -1;
			}
		} while (rc == EINTR);
	}

	if ((output_fd >= 0) && close(output_fd)) {
		rc = errno;
		log_flag(CONMGR, "%s: [%s] unable to close output fd:%d: %s",
			 __func__, con->name, output_fd,
			 slurm_strerror(rc));
	}
}

static void _on_close_output_fd(conmgr_fd_t *con)
{
	con_set_polling(con, PCTL_TYPE_NONE, __func__);

	list_flush(con->out);

	add_work_con_fifo(true, con, _close_output_fd,
			  ((void *) (uint64_t) con->output_fd));

	con->output_fd = -1;
}

extern void close_con_output(bool locked, conmgr_fd_t *con)
{
	if (!locked)
		slurm_mutex_lock(&mgr.mutex);

	_on_close_output_fd(con);

	if (!locked)
		slurm_mutex_unlock(&mgr.mutex);
}

static void _wrap_on_connect_timeout(conmgr_callback_args_t conmgr_args,
				     void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	int rc;

	if (con->events->on_connect_timeout)
		rc = con->events->on_connect_timeout(con, con->arg);
	else
		rc = SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT;

	if (rc) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char str[CTIME_STR_LEN];

			timespec_ctime(mgr.conf_connect_timeout, false, str,
				       sizeof(str));

			log_flag(CONMGR, "%s: [%s] closing due to connect %s timeout failed: %s",
				 __func__, con->name, str, slurm_strerror(rc));
		}
		close_con(false, con);
	} else {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char str[CTIME_STR_LEN];

			timespec_ctime(mgr.conf_connect_timeout, false, str,
				       sizeof(str));

			log_flag(CONMGR, "%s: [%s] connect %s timeout resetting",
				 __func__, con->name, str);
		}

		slurm_mutex_lock(&mgr.mutex);
		con->last_read = timespec_now();
		slurm_mutex_unlock(&mgr.mutex);
	}
}

static void _on_connect_timeout(handle_connection_args_t *args,
				conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(args->magic == MAGIC_HANDLE_CONNECTION);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		char time_str[CTIME_STR_LEN], total_str[CTIME_STR_LEN];

		timespec_ctime(timespec_diff_ns(con->last_read,
						args->time).diff, false,
			       time_str, sizeof(time_str));
		timespec_ctime(mgr.conf_connect_timeout, false, total_str,
			       sizeof(total_str));

		log_flag(CONMGR, "%s: [%s] connect timed out at %s/%s",
			 __func__, con->name, time_str, total_str);
	}

	add_work_con_fifo(true, con, _wrap_on_connect_timeout, NULL);
}

static void _wrap_on_write_timeout(conmgr_callback_args_t conmgr_args,
				   void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	int rc;

	if (con->events->on_write_timeout)
		rc = con->events->on_write_timeout(con, con->arg);
	else
		rc = SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT;

	if (rc) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char str[CTIME_STR_LEN];

			timespec_ctime(mgr.conf_write_timeout, false, str,
				       sizeof(str));

			log_flag(CONMGR, "%s: [%s] closing due to write %s timeout failed: %s",
				 __func__, con->name, str, slurm_strerror(rc));
		}

		slurm_mutex_lock(&mgr.mutex);

		/* Close read and write file descriptors */
		close_con(true, con);
		_on_close_output_fd(con);

		slurm_mutex_unlock(&mgr.mutex);
	} else {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char str[CTIME_STR_LEN];

			timespec_ctime(mgr.conf_write_timeout, false, str,
				       sizeof(str));

			log_flag(CONMGR, "%s: [%s] write %s timeout resetting",
				 __func__, con->name, str);
		}

		slurm_mutex_lock(&mgr.mutex);
		con->last_write = timespec_now();
		slurm_mutex_unlock(&mgr.mutex);
	}
}

static void _on_write_timeout(handle_connection_args_t *args, conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(args->magic == MAGIC_HANDLE_CONNECTION);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		char time_str[CTIME_STR_LEN], total_str[CTIME_STR_LEN];

		timespec_ctime(timespec_diff_ns(con->last_write,
						args->time).diff, false,
			       time_str, sizeof(time_str));
		timespec_ctime(mgr.conf_write_timeout, false, total_str,
			       sizeof(total_str));

		log_flag(CONMGR, "%s: [%s] write timed out at %s/%s",
			 __func__, con->name, time_str, total_str);
	}

	add_work_con_fifo(true, con, _wrap_on_write_timeout, NULL);
}

static void _wrap_on_read_timeout(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	int rc;

	if (con->events->on_read_timeout)
		rc = con->events->on_read_timeout(con, con->arg);
	else
		rc = SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT;

	if (rc) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char str[CTIME_STR_LEN];

			timespec_ctime(mgr.conf_read_timeout, false, str,
				       sizeof(str));

			log_flag(CONMGR, "%s: [%s] closing due to read %s timeout failed: %s",
				 __func__, con->name, str, slurm_strerror(rc));
		}

		close_con(false, con);
	} else {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char str[CTIME_STR_LEN];

			timespec_ctime(mgr.conf_read_timeout, false, str,
				       sizeof(str));

			log_flag(CONMGR, "%s: [%s] read %s timeout resetting",
				 __func__, con->name, str);
		}

		slurm_mutex_lock(&mgr.mutex);
		con->last_read = timespec_now();
		slurm_mutex_unlock(&mgr.mutex);
	}
}

static void _on_read_timeout(handle_connection_args_t *args, conmgr_fd_t *con)
{
	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(args->magic == MAGIC_HANDLE_CONNECTION);

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		char time_str[CTIME_STR_LEN], total_str[CTIME_STR_LEN];

		timespec_ctime(timespec_diff_ns(con->last_read, args->time).diff,
			       false, time_str, sizeof(time_str));
		timespec_ctime(mgr.conf_read_timeout, false, total_str,
			       sizeof(total_str));

		log_flag(CONMGR, "%s: [%s] read timed out at %s/%s",
			 __func__, con->name, time_str, total_str);
	}

	add_work_con_fifo(true, con, _wrap_on_read_timeout, NULL);
}

/*
 * handle connection states and apply actions required.
 * mgr mutex must be locked.
 *
 * RET 1 to remove or 0 to remain in list
 */
static int _handle_connection(conmgr_fd_t *con, handle_connection_args_t *args)
{
	int count;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(!args || (args->magic == MAGIC_HANDLE_CONNECTION));

	/* connection may have a running thread, do nothing */
	if (con_flag(con, FLAG_WORK_ACTIVE)) {
		log_flag(CONMGR, "%s: [%s] connection has work to do",
			 __func__, con->name);
		return 0;
	}

	if (con_flag(con, FLAG_IS_CONNECTED)) {
		/* continue on to follow other checks */
	} else if (!con_flag(con, FLAG_IS_SOCKET) ||
		   con_flag(con, FLAG_CAN_READ) ||
		   con_flag(con, FLAG_CAN_WRITE) ||
		   con_flag(con, FLAG_IS_LISTEN)) {
		/*
		 * Only sockets need special handling to know when they are
		 * connected. Enqueue on_connect callback if defined.
		 */
		con_set_flag(con, FLAG_IS_CONNECTED);

		if (con_flag(con, FLAG_WATCH_READ_TIMEOUT)) {
			if (args)
				con->last_read = args->time;
			else
				con->last_read = timespec_now();
		}

		if (con_flag(con, FLAG_IS_SOCKET) && (con->output_fd != -1)) {
			/* Query outbound MSS now kernel should know the answer */
			add_work_con_fifo(true, con, _update_mss, NULL);
		}

		if (con_flag(con, FLAG_IS_LISTEN)) {
			if (con->events->on_listen_connect) {
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
		} else if (con->events->on_connection) {
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
		xassert(!con_flag(con, FLAG_CAN_READ) &&
			!con_flag(con, FLAG_CAN_WRITE));

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

		if (con_flag(con, FLAG_WATCH_CONNECT_TIMEOUT) && args &&
		    _handle_time_limit(args, con->last_read,
				       mgr.conf_connect_timeout)) {
			_on_connect_timeout(args, con);
			return 0;
		}

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
		/* unset by _wrap_con_work() */
		xassert(!con_flag(con, FLAG_WORK_ACTIVE));
		con_set_flag(con, FLAG_WORK_ACTIVE);

		handle_work(true, work);
		return 0;
	}

	/*
	 * Skip all monitoring when FLAG_QUIESCE set but only if there is
	 * at least 1 file descriptor to avoid stopping a closed connection.
	 */
	if (con_flag(con, FLAG_QUIESCE) && ((con->input_fd >= 0) ||
					    (con->output_fd >= 0))) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char *flags = con_flags_string(con->flags);
			log_flag(CONMGR, "%s: connection is quiesced flags=%s",
				 __func__, flags);
			xfree(flags);
		}
		con_set_polling(con, PCTL_TYPE_NONE, __func__);
		return 0;
	}

	if (con->extract) {
		/* extraction of file descriptors requested */
		extract_con_fd(con);
		return 0;
	}

	/* handle out going data */
	if (!con_flag(con, FLAG_IS_LISTEN) && (con->output_fd >= 0) &&
	    !list_is_empty(con->out)) {
		if (con_flag(con, FLAG_CAN_WRITE) ||
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

			if (con_flag(con, FLAG_WATCH_WRITE_TIMEOUT) && args &&
			    _handle_time_limit(args, con->last_write,
					       mgr.conf_write_timeout)) {
				_on_write_timeout(args, con);
				return 0;
			}

			/* must wait until poll allows write of this socket */
			log_flag(CONMGR, "%s: [%s] waiting for %u writes",
				 __func__, con->name, list_count(con->out));
		}
		return 0;
	}

	if (!con_flag(con, FLAG_IS_LISTEN) &&
	    (count = list_count(con->write_complete_work))) {
		bool queue_work = false;

		if (con->output_fd < 0) {
			/* output_fd is already closed so no more write()s */
			queue_work = true;
		} else if (con->polling_output_fd == PCTL_TYPE_UNSUPPORTED) {
			/* output_fd can't be polled for CAN_WRITE */
			queue_work = true;
		} else if ((con->polling_output_fd == PCTL_TYPE_NONE) &&
			   con_flag(con, FLAG_CAN_WRITE)) {
			/* poll() already marked connection as CAN_WRITE */
			queue_work = true;
		}

		if (queue_work) {
			log_flag(CONMGR, "%s: [%s] waiting for %u write_complete work",
				 __func__, con->name, count);
			add_work_con_fifo(true, con, _on_write_complete_work,
					  NULL);
		} else {
			log_flag(CONMGR, "%s: [%s] waiting for FLAG_CAN_WRITE to queue %u write_complete work",
				 __func__, con->name, count);

			/*
			 * Always unset FLAG_CAN_WRITE if we are not queuing up
			 * _on_write_complete_work() as we want to trigger on
			 * the next edge activation of FLAG_CAN_WRITE to avoid
			 * wasting time calling ioctl(TIOCOUTQ) when nothing has
			 * changed
			 */
			con_unset_flag(con, FLAG_CAN_WRITE);

			/*
			 * Existing polling either did not set FLAG_CAN_WRITE or
			 * the polling type was not monitoring for
			 * FLAG_CAN_WRITE. output_fd is still valid and we need
			 * to change the polling to monitor outbound buffer
			 * (indirectly) to queue up _on_write_complete_work() on
			 * when FLAG_CAN_WRITE is set.
			 */
			con_set_polling(con, PCTL_TYPE_READ_WRITE, __func__);
		}

		return 0;
	}

	/* check if there is new connection waiting   */
	if (con_flag(con, FLAG_IS_LISTEN) && !con_flag(con, FLAG_READ_EOF) &&
	    con_flag(con, FLAG_CAN_READ)) {
		/* disable polling until _listen_accept() completes */
		con_set_polling(con, PCTL_TYPE_CONNECTED, __func__);
		con_unset_flag(con, FLAG_CAN_READ);

		if (list_count(mgr.connections) >= mgr.max_connections) {
			warning("%s: [%s] Deferring incoming connection due to %d/%d connections",
				 __func__, con->name,
				 list_count(mgr.connections),
				 mgr.max_connections);
		} else {
			log_flag(CONMGR, "%s: [%s] listener has incoming connection",
				 __func__, con->name);
			add_work_con_fifo(true, con, _listen_accept, con);
		}
		return 0;
	}

	/* read as much data as possible before processing */
	if (!con_flag(con, FLAG_IS_LISTEN) && !con_flag(con, FLAG_READ_EOF) &&
	    (con_flag(con, FLAG_CAN_READ) ||
	     (con->polling_input_fd == PCTL_TYPE_UNSUPPORTED))) {
		log_flag(CONMGR, "%s: [%s] queuing read", __func__, con->name);
		/* reset if data has already been tried if about to read data */
		con_unset_flag(con, FLAG_ON_DATA_TRIED);
		add_work_con_fifo(true, con, handle_read, con);
		return 0;
	}

	/* handle already read data */
	if (!con_flag(con, FLAG_IS_LISTEN) && get_buf_offset(con->in) &&
	    !con_flag(con, FLAG_ON_DATA_TRIED)) {
		log_flag(CONMGR, "%s: [%s] need to process %u bytes",
			 __func__, con->name, get_buf_offset(con->in));
		add_work_con_fifo(true, con, wrap_on_data, con);
		return 0;
	}

	if (!con_flag(con, FLAG_READ_EOF)) {
		xassert(con->input_fd != -1);

		/* must wait until poll allows read from this socket */
		if (con_flag(con, FLAG_IS_LISTEN)) {
			if (list_count(mgr.connections) >= mgr.max_connections) {
				warning("%s: [%s] Deferring polling for new connections due to %d/%d connections",
					 __func__, con->name,
					 list_count(mgr.connections),
					 mgr.max_connections);

				con_set_polling(con, PCTL_TYPE_CONNECTED,
						__func__);
			} else {
				con_set_polling(con, PCTL_TYPE_LISTEN,
						__func__);
				log_flag(CONMGR, "%s: [%s] waiting for new connection",
					 __func__, con->name);
			}
		} else {
			con_set_polling(con, PCTL_TYPE_READ_ONLY, __func__);

			if (con_flag(con, CON_FLAG_WATCH_READ_TIMEOUT) &&
			    args && list_is_empty(con->write_complete_work) &&
			    _handle_time_limit(args, con->last_read,
					       mgr.conf_read_timeout)) {
				_on_read_timeout(args, con);
				return 0;
			}

			if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
				char *flags = con_flags_string(con->flags);
				log_flag(CONMGR, "%s: [%s] waiting for events: pending_read=%u pending_writes=%u work=%d write_complete_work=%d flags=%s",
					 __func__, con->name,
					 get_buf_offset(con->in),
					 list_count(con->out),
					 list_count(con->work),
					 list_count(con->write_complete_work),
					 flags);
				xfree(flags);
			}
		}
		return 0;
	}

	/*
	 * Close out the incoming to avoid any new work coming into the
	 * connection.
	 */
	if (con->input_fd != -1) {
		log_flag(CONMGR, "%s: [%s] queuing close of incoming on connection input_fd=%d",
			 __func__, con->name, con->input_fd);
		xassert(con_flag(con, FLAG_READ_EOF));
		add_work_con_fifo(true, con, work_close_con, NULL);
		return 0;
	}

	if (con_flag(con, FLAG_WAIT_ON_FINISH)) {
		log_flag(CONMGR, "%s: [%s] waiting for %s",
			 __func__, con->name,
			 (con_flag(con, FLAG_IS_LISTEN) ? "on_finish()" :
			  "on_listen_finish()"));
		return 0;
	}

	if (con->arg) {
		log_flag(CONMGR, "%s: [%s] queuing up %s",
			 __func__, con->name,
			 (con_flag(con, FLAG_IS_LISTEN) ? "on_finish()" :
			  "on_listen_finish()"));

		con_set_flag(con, FLAG_WAIT_ON_FINISH);

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

	xassert(con->refs < INT_MAX);
	xassert(con->refs >= 0);
	if (con->refs > 0) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char *flags = con_flags_string(con->flags);
			log_flag(CONMGR, "%s: [%s] waiting on outstanding references:%d flags=%s",
				 __func__, con->name, con->refs, flags);
			xfree(flags);
		}

		return 0;
	}

	/*
	 * This connection has no more pending work or possible IO:
	 * Remove the connection and close everything.
	 */

	if (con->output_fd != -1) {
		log_flag(CONMGR, "%s: [%s] waiting to close output_fd=%d",
			 __func__, con->name, con->output_fd);
		_on_close_output_fd(con);
		return 0;
	}

	log_flag(CONMGR, "%s: [%s] closed connection", __func__, con->name);

	/* mark this connection for cleanup */
	return 1;
}

static int _list_transfer_handle_connection(void *x, void *arg)
{
	conmgr_fd_t *con = x;
	handle_connection_args_t *args = arg;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(args->magic == MAGIC_HANDLE_CONNECTION);

	return _handle_connection(con, args);
}

/*
 * listen socket is ready to accept
 */
static void _listen_accept(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_t *con = conmgr_args.con;
	slurm_addr_t addr = {0};
	socklen_t addrlen = sizeof(addr);
	int input_fd = -1, fd = -1, rc = EINVAL;
	const char *unix_path = NULL;
	conmgr_con_type_t type = CON_TYPE_INVALID;
	con_flags_t flags = FLAG_NONE;

	slurm_mutex_lock(&mgr.mutex);

	if ((input_fd = con->input_fd) < 0) {
		slurm_mutex_unlock(&mgr.mutex);
		log_flag(CONMGR, "%s: [%s] skipping accept on closed connection",
			 __func__, con->name);
		return;
	} else if (con_flag(con, FLAG_QUIESCE)) {
		slurm_mutex_unlock(&mgr.mutex);
		log_flag(CONMGR, "%s: [%s] skipping accept on quiesced connection",
			 __func__, con->name);
		return;
	} else
		log_flag(CONMGR, "%s: [%s] attempting to accept new connection",
			 __func__, con->name);

	type = con->type;
	flags = con->flags;

	slurm_mutex_unlock(&mgr.mutex);

	/* try to get the new file descriptor and retry on errors */
	if ((fd = accept4(input_fd, (struct sockaddr *) &addr, &addrlen,
			  SOCK_CLOEXEC)) < 0) {
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
		struct sockaddr_un *usock = (struct sockaddr_un *) &addr;

		xassert(usock->sun_family == AF_UNIX);

		if (!usock->sun_path[0]) {
			/*
			 * Attempt to use parent socket's path.
			 * Need to lock to access con->address safely.
			 */
			slurm_mutex_lock(&mgr.mutex);

			if (con->address.ss_family == AF_UNIX) {
				struct sockaddr_un *psock =
					(struct sockaddr_un *) &con->address;

				if (psock->sun_path[0])
					(void) memcpy(&usock->sun_path,
						      &psock->sun_path,
						      sizeof(usock->sun_path));
			}

			slurm_mutex_unlock(&mgr.mutex);
		}

		/* address may not be populated by kernel */
		if (usock->sun_path[0])
			unix_path = usock->sun_path;
	}

	if (conmgr_args.status == CONMGR_WORK_STATUS_CANCELLED) {
		log_flag(CONMGR, "%s: [%s] closing new connection to %pA during shutdown",
				 __func__, con->name, &addr);
		fd_close(&fd);
		return;
	}

	/* hand over FD for normal processing */
	if ((rc = add_connection(type, con, fd, fd, con->events,
				 (conmgr_con_flags_t) flags, &addr, addrlen,
				 false, unix_path, con->new_arg))) {
		log_flag(CONMGR, "%s: [fd:%d] unable to a register new connection: %s",
			 __func__, fd, slurm_strerror(rc));
		return;
	}

	log_flag(CONMGR, "%s: [%s->fd:%d] registered newly accepted connection",
		 __func__, con->name, fd);
}

/*
 * Inspect all connection states and apply actions required
 */

static void _inspect_connections(conmgr_callback_args_t conmgr_args, void *arg)
{
	bool send_signal = false;
	handle_connection_args_t args = {
		.magic = MAGIC_HANDLE_CONNECTION,
	};

	slurm_mutex_lock(&mgr.mutex);
	xassert(mgr.inspecting);

	/*
	 * Always clear max watch sleep as it will be set before releasing lock
	 */
	mgr.watch_max_sleep = (struct timespec) {0};
	args.time = timespec_now();

	/*
	 * Always check mgr.connections list first to avoid
	 * _is_accept_deferred() returning a different answer which could result
	 * in listeners not being set to PCTL_TYPE_LISTEN after enough
	 * connections were closed to fall below the max connection count.
	 */

	if (list_transfer_match(mgr.connections, mgr.complete_conns,
				_list_transfer_handle_connection, &args))
		send_signal = true;
	if (list_transfer_match(mgr.listen_conns, mgr.complete_conns,
				_list_transfer_handle_connection, &args))
		send_signal = true;

	if ((slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) &&
	    mgr.watch_max_sleep.tv_sec) {
		char str[CTIME_STR_LEN];

		timespec_ctime(mgr.watch_max_sleep, true, str, sizeof(str));

		log_flag(CONMGR, "%s: set max watch sleep wait: %s",
			 __func__, str);
	}

	mgr.inspecting = false;

	if (send_signal)
		EVENT_SIGNAL(&mgr.watch_sleep);
	slurm_mutex_unlock(&mgr.mutex);
}

/* caller (or thread) must hold mgr.mutex lock */
static int _handle_poll_event(int fd, pollctl_events_t events, void *arg)
{
	conmgr_fd_t *con = NULL;
	con_flags_t old_flags;

	xassert(fd >= 0);

	if (!(con = con_find_by_fd(fd))) {
		/* close_con() was called during poll() was running */
		log_flag(CONMGR, "%s: Ignoring events for unknown fd:%d",
			 __func__, fd);
		return SLURM_SUCCESS;
	}

	/* record prior flags to know if something changed */
	old_flags = con->flags;

	con_unset_flag(con, FLAG_CAN_READ);
	con_unset_flag(con, FLAG_CAN_WRITE);

	if (pollctl_events_has_error(events)) {
		con_close_on_poll_error(con, fd);
		/* connection errored but not handling of the connection */
		return SLURM_SUCCESS;
	}

	/*
	 * Avoid poll()ing the connection until we handle the flags via
	 * _handle_connection() to avoid the kernel thinking we successfully
	 * received the new edge triggered events.
	 */
	con_set_polling(con, PCTL_TYPE_NONE, __func__);

	if (con_flag(con, FLAG_IS_LISTEN)) {
		/* Special handling for listening sockets */
		if (pollctl_events_has_hangup(events)) {
			log_flag(CONMGR, "%s: [%s] listener HANGUP",
				 __func__, con->name);
			con_set_flag(con, FLAG_READ_EOF);
		} else if (pollctl_events_can_read(events)) {
			con_set_flag(con, FLAG_CAN_READ);
		} else {
			fatal_abort("should never happen");
		}


		if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
			char *flags = con_flags_string(con->flags);
			log_flag(CONMGR, "%s: [%s] listener fd=%u flags=%s",
				 __func__, con->name, fd, flags);
			xfree(flags);
		}

		return SLURM_SUCCESS;
	}

	if (fd == con->input_fd) {
		con_assign_flag(con, FLAG_CAN_READ,
				pollctl_events_can_read(events));

		/* Avoid setting FLAG_READ_EOF if FLAG_CAN_READ */
		if (!con_flag(con, FLAG_CAN_READ) &&
		    !con_flag(con, FLAG_READ_EOF))
			con_assign_flag(con, FLAG_READ_EOF,
					pollctl_events_has_hangup(events));
	}
	if (fd == con->output_fd)
		con_assign_flag(con, FLAG_CAN_WRITE,
				pollctl_events_can_write(events));

	if (slurm_conf.debug_flags & DEBUG_FLAG_CONMGR) {
		char *flags = con_flags_string(con->flags);
		log_flag(CONMGR, "%s: [%s] fd=%d flags=%s",
			 __func__, con->name, fd, flags);
		xfree(flags);
	}

	/* Attempt to changed connection state immediately */
	if ((con->flags & FLAGS_MASK_STATE) != (old_flags & FLAGS_MASK_STATE))
		(void) _handle_connection(con, NULL);

	return SLURM_SUCCESS;
}

/* caller must hold mgr.mutex lock */
static bool _is_poll_interrupt(void)
{
	return (mgr.shutdown_requested ||
		(mgr.waiting_on_work && (mgr.workers.active == 1)));
}

/* Poll all connections */
static void _poll_connections(conmgr_callback_args_t conmgr_args, void *arg)
{
	int rc;

	xassert(!conmgr_args.con);

	slurm_mutex_lock(&mgr.mutex);
	xassert(mgr.poll_active);

	if (_is_poll_interrupt()) {
		log_flag(CONMGR, "%s: skipping poll()", __func__);
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
	xassert(!con->refs);

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

static int _foreach_is_waiter(void *x, void *arg)
{
	int *waiters_ptr = arg;
	conmgr_fd_t *con = x;
	bool skip = false;

	xassert(con->magic == MAGIC_CON_MGR_FD);
	xassert(*waiters_ptr >= 0);

	if (is_signal_connection(con)) {
		skip = true;
	} else if (con_flag(con, FLAG_WORK_ACTIVE)) {
		/* never skip when connection has active work */
	} else if (con_flag(con, FLAG_IS_LISTEN)) {
		/*
		 * listeners don't matter if they are not running
		 * _listen_accept() as work
		 */
		skip = true;
	} else if (con_flag(con, FLAG_QUIESCE)) {
		/*
		 * Individually quiesced will not do anything and need to be
		 * skipped or the global quiesce will never happen
		 */
		skip = true;
	}

	if (!skip)
		(*waiters_ptr)++;

	return SLURM_SUCCESS;
}

/* Get count of connections that quiesce is waiting to complete */
static int _get_quiesced_waiter_count(void)
{
	int waiters = 0;

	(void) list_for_each_ro(mgr.connections, _foreach_is_waiter, &waiters);
	(void) list_for_each_ro(mgr.listen_conns, _foreach_is_waiter, &waiters);

	return waiters;
}

static bool _watch_loop(void)
{
	if (mgr.shutdown_requested) {
		signal_mgr_stop();
		cancel_delayed_work();
		close_all_connections();
	}

	if (mgr.quiesce.requested) {
		int waiters;

		if (signal_mgr_has_incoming()) {
			/*
			 * Must wait for any outstanding incoming signals to be
			 * processed or a pending signal may be deferred until
			 * after quiesce that may never come
			 */
			log_flag(CONMGR, "%s: quiesced state deferred due to pending incoming POSIX signal(s)",
				 __func__);
		} else if ((waiters = _get_quiesced_waiter_count())) {
			log_flag(CONMGR, "%s: quiesced state deferred to process connections:%d/%d",
				 __func__, waiters,
				 (list_count(mgr.connections) +
				  list_count(mgr.listen_conns)));
		} else if (mgr.workers.active) {
			log_flag(CONMGR, "%s: quiesced state waiting on workers:%d/%d",
				 __func__, mgr.workers.active,
				 mgr.workers.total);
			mgr.waiting_on_work = true;
			return true;
		}  else {
			log_flag(CONMGR, "%s: BEGIN: quiesced state", __func__);
			mgr.quiesce.active = true;

			EVENT_BROADCAST(&mgr.quiesce.on_start_quiesced);

			while (mgr.quiesce.active)
				EVENT_WAIT(&mgr.quiesce.on_stop_quiesced,
					   &mgr.mutex);

			log_flag(CONMGR, "%s: END: quiesced state", __func__);

			/*
			 * All the worker threads may be waiting for a
			 * worker_sleep event and not an on_start_quiesced
			 * event. Wake them all up right now if there is any
			 * pending work queued to avoid workers remaining
			 * sleeping until add_work() is called enough times to
			 * wake them all up independent of the size of the
			 * mgr.work queue.
			 */
			if (!list_is_empty(mgr.work))
				EVENT_BROADCAST(&mgr.worker_sleep);
		}
	}

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

		log_flag(CONMGR, "%s: waiting for new events: workers:%d/%d work:%d delayed_work:%d connections:%d listeners:%d complete:%d polling:%c inspecting:%c shutdown_requested:%c quiesce_requested:%c waiting_on_work:%c",
				 __func__, mgr.workers.active,
				 mgr.workers.total, list_count(mgr.work),
				 list_count(mgr.delayed_work),
				 list_count(mgr.connections),
				 list_count(mgr.listen_conns),
				 list_count(mgr.complete_conns),
				 BOOL_CHARIFY(mgr.poll_active),
				 BOOL_CHARIFY(mgr.inspecting),
				 BOOL_CHARIFY(mgr.shutdown_requested),
				 BOOL_CHARIFY(mgr.quiesce.requested),
				 BOOL_CHARIFY(mgr.waiting_on_work));

		EVENT_WAIT_TIMED(&mgr.watch_sleep, mgr.watch_max_sleep,
				 &mgr.mutex);
		mgr.waiting_on_work = false;
	}

	log_flag(CONMGR, "%s: returning shutdown_requested=%c connections=%u listen_conns=%u",
		 __func__, BOOL_CHARIFY(mgr.shutdown_requested),
		 list_count(mgr.connections), list_count(mgr.listen_conns));

	xassert(mgr.watch_thread == pthread_self());
	mgr.watch_thread = 0;

	EVENT_BROADCAST(&mgr.watch_return);
	slurm_mutex_unlock(&mgr.mutex);

	return NULL;
}

extern void *watch_thread(void *arg)
{
#if HAVE_SYS_PRCTL_H
	static char title[] = "watch";

	if (prctl(PR_SET_NAME, title, NULL, NULL, NULL)) {
		error("%s: cannot set process name to %s %m",
		      __func__, title);
	}
#endif

	return watch(NULL);
}
