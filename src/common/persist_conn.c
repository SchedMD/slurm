/*****************************************************************************\
 *  persist_conn.c - Definitions for communicating over a persistent
 *                   connection within Slurm.
 ******************************************************************************
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

#include "config.h"

#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>

#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/persist_conn.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurm_time.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/threadpool.h"
#include "src/common/xsignal.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/conn.h"

#define MAX_THREAD_COUNT 100

typedef struct {
	void *arg;
	persist_conn_t *pcon;
	int fd;
	int thread_loc;
	pthread_t thread_id;
} persist_service_conn_t;

static persist_service_conn_t *persist_service_conn[MAX_THREAD_COUNT];
static int             thread_count = 0;
static pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  thread_count_cond = PTHREAD_COND_INITIALIZER;
static time_t          shutdown_time = 0;

static buf_t *_slurm_persist_recv_msg(persist_conn_t *persist_conn,
				      bool reopen);

static void _conn_destroy(persist_conn_t *persist_conn)
{
	if (!persist_conn->conn)
		return;

	/*
	 * Clear last_fd before any teardown so that external wake paths
	 * (e.g. _connection_fini_callback() in slurmdbd) won't see a
	 * stale fd that the kernel might subsequently reuse.
	 */
	persist_conn->last_fd = -1;

	if (!persist_conn->skip_conn_shutdown)
		(void) conn_blocking_g_shutdown(persist_conn->conn);

	/* reset to default */
	persist_conn->skip_conn_shutdown = false;

	conn_g_destroy(persist_conn->conn, true);
}

/* Return true if communication failure should be logged. Only log failures
 * every 10 minutes to avoid filling logs */
static bool _comm_fail_log(persist_conn_t *persist_conn)
{
	time_t now = time(NULL);
	time_t old = now - 600;	/* Log failures once every 10 mins */

	if (persist_conn->comm_fail_time < old) {
		persist_conn->comm_fail_time = now;
		return true;
	}
	return false;
}

/* static void _reopen_persist_conn(persist_conn_t *persist_conn) */
/* { */
/*	xassert(persist_conn); */
/*	fd_close(&persist_conn->fd); */
/*	slurm_persist_conn_open(persist_conn); */
/* } */

/* Wait until a file is readable,
 * RET false if can not be read */
static bool _conn_readable(persist_conn_t *persist_conn)
{
	struct pollfd ufds;
	int rc, time_left;

	xassert(persist_conn->shutdown);

	/*
	 * The tls layer may already have data buffered, which could lead to
	 * poll blocking indefinitely.
	 */
	if (conn_g_peek(persist_conn->conn))
		return true;

	ufds.fd = conn_g_get_fd(persist_conn->conn);
	ufds.events = POLLIN;
	while (!(*persist_conn->shutdown)) {
		if (persist_conn->timeout) {
			struct timeval tstart;
			gettimeofday(&tstart, NULL);
			time_left = persist_conn->timeout -
				    timeval_tot_wait(&tstart);
		} else
			time_left = -1;
		rc = poll(&ufds, 1, time_left);
		if (*persist_conn->shutdown)
			break;
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN)) {
				debug3("%s: retrying poll for fd %d: %m",
					__func__, ufds.fd);
				continue;
			}
			error("%s: poll error for fd %d: %m",
			      __func__, ufds.fd);
			return false;
		}
		if (rc == 0) {
			debug("%s: poll for fd %d timeout after %d msecs of total wait %d msecs.",
			      __func__, ufds.fd, time_left,
			      persist_conn->timeout);
			return false;
		}
		if (ufds.revents & POLLHUP) {
			log_flag(NET, "%s: persistent connection for fd %d closed",
				 __func__, ufds.fd);
			return false;
		}
		if (ufds.revents & POLLNVAL) {
			error("%s: persistent connection for fd %d is invalid",
			       __func__, ufds.fd);
			return false;
		}
		if (ufds.revents & POLLERR) {
			int sockerr, fd_rc;
			if (!(fd_rc = fd_get_socket_error(ufds.fd, &sockerr)))
				error("%s: persistent connection for fd %d experienced error[%d]: %s",
				      __func__, ufds.fd, sockerr,
				      slurm_strerror(sockerr));
			else
				error("%s: persistent connection for fd %d experienced an error getting socket error: %s",
				      __func__, ufds.fd, slurm_strerror(fd_rc));

			return false;
		}
		if ((ufds.revents & POLLIN) == 0) {
			error("%s: persistent connection for fd %d missing POLLIN flag with revents 0x%"PRIx64,
			      __func__, ufds.fd, (uint64_t) ufds.revents);
			return false;
		}
		if (ufds.revents == POLLIN) {
			errno = 0;
			return true;
		}

		fatal_abort("%s: poll returned unexpected revents: 0x%"PRIx64,
			    __func__, (uint64_t) ufds.revents);
	}

	debug("%s: shutdown request detected for fd %d",
	      __func__, ufds.fd);
	return false;
}

static void _destroy_persist_service(persist_service_conn_t *persist_service)
{
	if (persist_service) {
		slurm_persist_conn_destroy(persist_service->pcon);
		xfree(persist_service);
	}
}

static void _sig_handler(int signal)
{
}

static void _persist_free_msg_members(persist_conn_t *persist_conn,
				      persist_msg_t *persist_msg)
{
	if (persist_conn->flags & PERSIST_FLAG_DBD) {
		slurmdbd_msg_t dbd_msg = {
			.data = persist_msg->data,
			.msg_type = persist_msg->msg_type,
		};
		slurmdbd_free_msg(&dbd_msg);
	} else
		slurm_free_msg_data(persist_msg->msg_type, persist_msg->data);
}

static int _process_service_connection(persist_conn_t *persist_conn, int fd,
				       void *arg)
{
	uint32_t nw_size = 0, msg_size = 0;
	char *msg_char = NULL;
	ssize_t msg_read = 0, offset = 0;
	bool first = true, fini = false;
	buf_t *buffer = NULL;
	int rc = SLURM_SUCCESS;
	conn_args_t conn_args = {
		.input_fd = fd,
		.output_fd = fd,
		.mode = CONN_SERVER,
	};

	xassert(persist_conn->callback_proc);
	xassert(persist_conn->shutdown);

	log_flag(NET, "%s: Opened connection %d from %s",
		 __func__, fd, persist_conn->rem_host);

	if (persist_conn->flags & PERSIST_FLAG_ALREADY_INITED)
		first = false;

	if (first && !(persist_conn->conn = conn_g_create(&conn_args))) {
		error("%s: conn_g_create() failed negotiation, closing connection %d(%s)",
		      __func__, fd, persist_conn->rem_host);
		(void) close(fd);
		return SLURM_ERROR;
	}

	while (!(*persist_conn->shutdown) && !fini) {
		if (!_conn_readable(persist_conn))
			break;		/* problem with this socket */

		msg_read = conn_g_recv(persist_conn->conn, &nw_size,
				       sizeof(nw_size));
		if (msg_read == 0)	/* EOF */
			break;
		if (msg_read != sizeof(nw_size)) {
			error("Could not read msg_size from connection %d(%s) uid(%u)",
			      fd, persist_conn->rem_host,
			      persist_conn->auth.uid);
			break;
		}
		msg_size = ntohl(nw_size);
		if ((msg_size < 2) || (msg_size > MAX_MSG_SIZE)) {
			error("Invalid msg_size (%u) from connection %d(%s) uid(%u)",
			      msg_size, fd, persist_conn->rem_host,
			      persist_conn->auth.uid);
			break;
		}

		msg_char = xmalloc(msg_size);
		offset = 0;
		while (msg_size > offset) {
			if (!_conn_readable(persist_conn))
				break;		/* problem with this socket */
			msg_read = conn_g_recv(persist_conn->conn,
					       (msg_char + offset),
					       (msg_size - offset));
			if (msg_read <= 0) {
				error("read(%d): %m", fd);
				break;
			}
			offset += msg_read;
		}
		if (msg_size == offset) {
			persist_msg_t msg;

			rc = slurm_persist_conn_process_msg(
				persist_conn, &msg,
				msg_char, msg_size,
				&buffer, first);

			if (rc == SLURM_SUCCESS) {
				rc = (persist_conn->callback_proc)(arg, &msg,
								   &buffer);
				_persist_free_msg_members(persist_conn, &msg);
				if ((rc != SLURM_SUCCESS) &&
				    (rc != SLURM_NO_CHANGE_IN_DATA) &&
				    (rc != ACCOUNTING_FIRST_REG) &&
				    (rc != ACCOUNTING_TRES_CHANGE_DB) &&
				    (rc != ACCOUNTING_NODES_CHANGE_DB)) {
					error("Processing last message from connection %d(%s) uid(%u)",
					      fd, persist_conn->rem_host,
					      persist_conn->auth.uid);
					if (rc == ESLURM_ACCESS_DENIED ||
					    rc == SLURM_PROTOCOL_VERSION_ERROR)
						fini = true;
				}
			}
			first = false;
		} else {
			buffer = slurm_persist_make_rc_msg(
				persist_conn, SLURM_ERROR, "Bad offset", 0);
			fini = true;
		}

		xfree(msg_char);
		if (buffer) {
			if (slurm_persist_send_msg(persist_conn, buffer)
			    != SLURM_SUCCESS) {
				/* This is only an issue on persistent
				 * connections, and really isn't that big of a
				 * deal as the slurmctld will just send the
				 * message again. */
				if (persist_conn->rem_port)
					log_flag(NET, "%s: Problem sending response to connection host:%s fd:%d uid:%u",
						 __func__,
						 persist_conn->rem_host, fd,
						 persist_conn->auth.uid);
				fini = true;
			}
			FREE_NULL_BUFFER(buffer);
		}
	}

	log_flag(NET, "%s: Closed connection host:%s fd:%d uid:%u",
		 __func__, persist_conn->rem_host, fd, persist_conn->auth.uid);

	return rc;
}

static void *_service_connection(void *arg)
{
	persist_service_conn_t *service_conn = arg;
	int thread_loc;
	persist_conn_callback_fini_t callback_fini = NULL;
	void *callback_fini_arg = NULL;

	xassert(service_conn);
	xassert(service_conn->pcon);

	/*
	 * Backup this value for later usage, service_conn can be already freed
	 * after leaving _process_service_connection on shutdown
	 */
	thread_loc = service_conn->thread_loc;
	xassert(!service_conn->thread_id ||
		pthread_equal(service_conn->thread_id, pthread_self()));
	service_conn->thread_id = pthread_self();

	_process_service_connection(service_conn->pcon, service_conn->fd,
				    service_conn->arg);

	/*
	 * service_conn may have already been freed by
	 * slurm_persist_conn_recv_server_fini(), so re-fetch from the
	 * global array under lock and null-check before dereferencing.
	 *
	 * Copy callback info and unlock before invoking callback_fini to
	 * avoid lock-ordering inversion with slurmctld federation locks
	 * (fed_mgr_add_sibling_conn holds fed_read_lock then acquires
	 * thread_count_lock; calling callback_fini under thread_count_lock
	 * would acquire fed_write_lock — ABBA deadlock).
	 *
	 * The copied pointers remain valid because recv_server_fini() joins
	 * this thread before destroying the slot, and free_thread_loc()
	 * defers cleanup to fini during shutdown.
	 */
	slurm_mutex_lock(&thread_count_lock);
	service_conn = persist_service_conn[thread_loc];
	if (service_conn && service_conn->pcon &&
	    service_conn->pcon->callback_fini) {
		SWAP(callback_fini, service_conn->pcon->callback_fini);
		SWAP(callback_fini_arg, service_conn->arg);
	} else if (service_conn && service_conn->pcon) {
		log_flag(NET, "%s: Persist connection from cluster %s has disconnected",
			 __func__, service_conn->pcon->cluster_name);
	} else {
		log_flag(NET, "%s: Persist connection has disconnected",
			 __func__);
	}
	slurm_mutex_unlock(&thread_count_lock);

	if (callback_fini)
		callback_fini(callback_fini_arg);

	/* service_conn is freed inside here, unless already freed */
	slurm_persist_conn_free_thread_loc(thread_loc, true);

	return NULL;
}

extern void slurm_persist_conn_recv_server_init(void)
{
	int sigarray[] = {SIGUSR1, 0};

	shutdown_time = 0;

	/* Prepare to catch SIGUSR1 to interrupt accept().
	 * This signal is generated by the slurmdbd signal
	 * handler thread upon receipt of SIGABRT, SIGINT,
	 * or SIGTERM. That thread does all processing of
	 * all signals. */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);
}

extern void slurm_persist_conn_recv_server_fini(void)
{
	int i;

	slurm_mutex_lock(&thread_count_lock);
	shutdown_time = time(NULL);
	for (i=0; i<MAX_THREAD_COUNT; i++) {
		if (!persist_service_conn[i])
			continue;
		if (persist_service_conn[i]->thread_id)
			pthread_kill(persist_service_conn[i]->thread_id,
				     SIGUSR1);
	}
	/* It is faster to signal then wait since the threads would end serially
	 * instead of parallel if you did it all in one loop.
	 */
	for (i=0; i<MAX_THREAD_COUNT; i++) {
		if (!persist_service_conn[i])
			continue;
		pthread_t thread_id = persist_service_conn[i]->thread_id;
		if (thread_id && !pthread_equal(pthread_self(), thread_id)) {
			/*
			 * Let go of lock in case the persistent connection
			 * thread is cleaning itself up.
			 * slurm_persist_conn_free_thread_loc() may be trying to
			 * remove itself but could be waiting on the
			 * thread_count mutex which this has locked.
			 * After joining persist_service_conn[i] could be NULL
			 */
			slurm_mutex_unlock(&thread_count_lock);
			slurm_thread_join(thread_id);
			slurm_mutex_lock(&thread_count_lock);
		}
		if (persist_service_conn[i] && persist_service_conn[i]->pcon) {
			persist_service_conn[i]->pcon->skip_conn_shutdown =
				true;
		}

		_destroy_persist_service(persist_service_conn[i]);
		persist_service_conn[i] = NULL;
	}
	slurm_mutex_unlock(&thread_count_lock);
}

/* Run and clear persist_conn->callback_fini().  */
static void _run_callback_fini(persist_conn_t *persist_conn, void *arg)
{
	persist_conn_callback_fini_t callback_fini = NULL;

	if (!persist_conn)
		return;

	SWAP(callback_fini, persist_conn->callback_fini);

	if (callback_fini)
		callback_fini(arg);
}

extern void slurm_persist_conn_recv_thread_init(persist_conn_t *persist_conn,
						int fd, int thread_loc,
						void *arg)
{
	char name[PRCTL_BUF_BYTES] = "INVALID";
	persist_service_conn_t *service_conn;

	if (thread_loc < 0)
		thread_loc = slurm_persist_conn_wait_for_thread_loc();
	if (thread_loc < 0) {
		/*
		 * Shutdown started before a slot could be claimed, so no thread
		 * will ever service this connection.
		 */
		_run_callback_fini(persist_conn, arg);
		return;
	}

	slurm_mutex_lock(&thread_count_lock);
	service_conn = persist_service_conn[thread_loc];
	/*
	 * Between call for slurm_persist_conn_wait_for_thread_loc and this
	 * point we have unlocked and locked again, so we may have let the
	 * shutdown procedure to start.
	 *
	 * This causes persist_service_conn to be freed, thus service_conn may
	 * be NULL at this point.
	 *
	 * If this is the case, just return immediately.
	 */
	if (!service_conn) {
		xassert(thread_count > 0);
		thread_count--;

		slurm_cond_broadcast(&thread_count_cond);
		slurm_mutex_unlock(&thread_count_lock);

		_run_callback_fini(persist_conn, arg);
		slurm_persist_conn_destroy(persist_conn);
		fd_close(&fd);
		return;
	}
	xassert(!service_conn->arg);

	service_conn->arg = arg;
	service_conn->pcon = persist_conn;
	service_conn->fd = fd;
	service_conn->thread_loc = thread_loc;

	persist_conn->timeout = 0; /* If this isn't zero we won't wait forever
				      like we want to.
				   */

	(void) snprintf(name, sizeof(name), "p-%s",
			service_conn->pcon->cluster_name);

	/*
	 * Hold thread_count_lock through slurm_thread_create() so that
	 * recv_server_fini() cannot destroy this slot before thread_id is
	 * set. Without the lock, fini sees thread_id == 0, skips the join,
	 * and frees persist_service_conn[thread_loc] while we are still
	 * writing to it.
	 */
	slurm_thread_create(name, &persist_service_conn[thread_loc]->thread_id,
			    _service_connection, service_conn);
	slurm_mutex_unlock(&thread_count_lock);
}

/* Increment thread_count and don't return until its value is no larger
 *	than MAX_THREAD_COUNT,
 * RET index of free index in persist_service_conn or -1 to exit */
extern int slurm_persist_conn_wait_for_thread_loc(void)
{
	bool print_it = true;
	int i, rc = -1;

	slurm_mutex_lock(&thread_count_lock);
	while (1) {
		if (shutdown_time)
			break;

		if (thread_count < MAX_THREAD_COUNT) {
			thread_count++;
			for (i=0; i<MAX_THREAD_COUNT; i++) {
				if (persist_service_conn[i])
					continue;

				persist_service_conn[i] =
					xmalloc(sizeof(persist_service_conn_t));
				rc = i;
				break;
			}
			if (rc == -1) {
				/* thread_count and persist_thread_id
				 * out of sync */
				fatal("No free persist_thread_id");
			}
			break;
		} else {
			/* wait for state change and retry,
			 * just a delay and not an error.
			 * This can happen when the epilog completes
			 * on a bunch of nodes at the same time, which
			 * can easily happen for highly parallel jobs. */
			if (print_it) {
				static time_t last_print_time = 0;
				time_t now = time(NULL);
				if (difftime(now, last_print_time) > 2) {
					verbose("thread_count over "
						"limit (%d), waiting",
						thread_count);
					last_print_time = now;
				}
				print_it = false;
			}
			slurm_cond_wait(&thread_count_cond, &thread_count_lock);
		}
	}
	slurm_mutex_unlock(&thread_count_lock);
	return rc;
}

/* my_tid IN - Thread ID of spawned thread, 0 if no thread spawned */
extern void slurm_persist_conn_free_thread_loc(int thread_loc, bool detach)
{
	pthread_t thread_id;

	slurm_mutex_lock(&thread_count_lock);

	/* we will handle this in the fini */
	if (shutdown_time)
		goto end;

	if (thread_count > 0)
		thread_count--;
	else
		error("thread_count underflow");

	thread_id = persist_service_conn[thread_loc]->thread_id;
	_destroy_persist_service(persist_service_conn[thread_loc]);
	persist_service_conn[thread_loc] = NULL;

	/*
	 * In order to avoid zombie threads, detach the thread now before
	 * exiting. slurm_persist_conn_recv_server_fini() will not try to join
	 * the thread because here we have just free'd the connection. If the
	 * threads are at shutdown, the join will happen before the detach so
	 * slurm_persist_conn_recv_server_fini() will wait at the join until the
	 * thread is done.
	 */
	if (detach)
		slurm_thread_detach(thread_id);

end:
	/*
	 * We need to ensure we broadcast cond even if no thread is cleaned,
	 * something that happens if we delegate cleanup to the shutdown
	 * procedure; or we, otherwise, can cause slurm_cond_wait in
	 * slurm_persist_conn_wait_for_thread_loc to wait forever.
	 *
	 * If this happens, the daemon will never finish.
	 */
	slurm_cond_broadcast(&thread_count_cond);
	slurm_mutex_unlock(&thread_count_lock);
}

static int _open_persist_conn(persist_conn_t *persist_conn)
{
	slurm_addr_t addr;
	int fd;

	xassert(persist_conn);
	xassert(persist_conn->rem_host);
	xassert(persist_conn->rem_port);
	xassert(persist_conn->cluster_name);

	if (persist_conn->conn) {
		persist_conn->conn = NULL;
	}

	if (!persist_conn->inited)
		persist_conn->inited = true;

	if (!persist_conn->version) {
		/* Set to MIN_PROTOCOL so that a higher version controller can
		 * talk to a lower protocol version controller. When talking to
		 * the DBD, the protocol version should be set to the current
		 * protocol version prior to calling this. */
		persist_conn->version = SLURM_MIN_PROTOCOL_VERSION;
	}
	if (persist_conn->timeout < 0)
		persist_conn->timeout = slurm_conf.msg_timeout * 1000;

	slurm_set_addr(&addr, persist_conn->rem_port, persist_conn->rem_host);

	if (!(persist_conn->conn = slurm_open_msg_conn(&addr, NULL))) {
		if (_comm_fail_log(persist_conn)) {
			if (persist_conn->flags & PERSIST_FLAG_SUPPRESS_ERR)
				log_flag(NET, "%s: failed to open persistent connection (with error suppression active) to host:%s:%d: %m",
					 __func__, persist_conn->rem_host,
					 persist_conn->rem_port);
			else
				error("%s: failed to open persistent connection to host:%s:%d: %m",
				      __func__, persist_conn->rem_host,
				      persist_conn->rem_port);
		}
		return SLURM_ERROR;
	}

	fd = conn_g_get_fd(persist_conn->conn);

	/*
	 * Publish the fd in a stable scalar field readable lock-free by
	 * external wake paths. last_fd is cleared back to -1 in
	 * _conn_destroy() before the conn is torn down.
	 */
	persist_conn->last_fd = fd;

	fd_set_nonblocking(fd);
	net_set_keep_alive(fd);

	return SLURM_SUCCESS;
}


/* Open a persistent socket connection
 * IN/OUT - persistent connection needing rem_host and rem_port filled in.
 * Returned completely filled in.
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on failure */
extern int slurm_persist_conn_open(persist_conn_t *persist_conn)
{
	int rc = SLURM_ERROR;
	slurm_msg_t req_msg;
	persist_init_req_msg_t req;
	persist_rc_msg_t *resp = NULL;

	if (!persist_conn->shutdown)
		persist_conn->shutdown = &shutdown_time;

	if (_open_persist_conn(persist_conn) != SLURM_SUCCESS)
		return rc;

	slurm_msg_t_init(&req_msg);

	/* Always send the lowest protocol since we don't know what version the
	 * other side is running yet.
	 */
	req_msg.protocol_version = persist_conn->version;
	req_msg.msg_type = REQUEST_PERSIST_INIT;
	req_msg.flags |= SLURM_GLOBAL_AUTH_KEY;
	if (persist_conn->flags & PERSIST_FLAG_DBD)
		req_msg.flags |= SLURMDBD_CONNECTION;
	slurm_msg_set_r_uid(&req_msg, persist_conn->r_uid);
	req_msg.conn = persist_conn->conn;

	if (slurm_conf.debug_flags & DEBUG_FLAG_NET)
		(void) slurm_msg_t_init_address(&req_msg);

	memset(&req, 0, sizeof(persist_init_req_msg_t));
	req.cluster_name = persist_conn->cluster_name;
	req.persist_type = persist_conn->persist_type;
	req.port = persist_conn->my_port;
	req.version = SLURM_PROTOCOL_VERSION;

	req_msg.data = &req;

	if (slurm_send_node_msg(persist_conn->conn, &req_msg) < 0) {
		error("%s: failed to send persistent connection init message to %s:%d",
		      __func__, persist_conn->rem_host, persist_conn->rem_port);
		_conn_destroy(persist_conn);
		persist_conn->conn = NULL;
	} else {
		buf_t *buffer = NULL;
		persist_msg_t msg;
		persist_conn_t persist_conn_tmp;

		buffer = _slurm_persist_recv_msg(persist_conn, false);

		if (!buffer) {
			if (_comm_fail_log(persist_conn)) {
				error("%s: No response to persist_init",
				      __func__);
			}

			_conn_destroy(persist_conn);
			persist_conn->conn = NULL;

			if (!errno)
				errno = SLURM_ERROR;
			goto end_it;
		}
		memset(&msg, 0, sizeof(persist_msg_t));
		memcpy(&persist_conn_tmp, persist_conn, sizeof(persist_conn_t));
		/* The first unpack is done the same way for dbd or normal
		 * communication . */
		persist_conn_tmp.flags &= (~PERSIST_FLAG_DBD);
		rc = slurm_persist_msg_unpack(&persist_conn_tmp, &msg, buffer);
		FREE_NULL_BUFFER(buffer);

		resp = (persist_rc_msg_t *)msg.data;
		/*
		 * On an internal error from REQUEST_PERSIST_INIT, the value of
		 * resp->ret_info will not be the protocol version but instead
		 * the type of RPC sent. Handle this after we look at resp->rc.
		 */
		if (resp && (rc == SLURM_SUCCESS))
			rc = resp->rc;

		if (rc != SLURM_SUCCESS) {
			if (resp) {
				error("%s: Something happened with the receiving/processing of the persistent connection init message to %s:%d: %s",
				      __func__, persist_conn->rem_host,
				      persist_conn->rem_port, resp->comment);
			} else {
				error("%s: Failed to unpack persistent connection init resp message from %s:%d",
				      __func__,
				      persist_conn->rem_host,
				      persist_conn->rem_port);
			}

			_conn_destroy(persist_conn);
			persist_conn->conn = NULL;
		} else if (resp) {
			persist_conn->version = resp->ret_info;
			persist_conn->flags |= resp->flags;
		}
	}

end_it:

	slurm_free_persist_rc_msg(resp);

	return rc;
}

extern void slurm_persist_conn_close(persist_conn_t *persist_conn)
{
	if (!persist_conn)
		return;

	_conn_destroy(persist_conn);

	persist_conn->conn = NULL;
}

extern int slurm_persist_conn_reopen(persist_conn_t *persist_conn)
{
	slurm_persist_conn_close(persist_conn);
	return slurm_persist_conn_open(persist_conn);
}

/* Close the persistent connection */
extern void slurm_persist_conn_members_destroy(persist_conn_t *persist_conn)
{
	if (!persist_conn)
		return;

	persist_conn->inited = false;
	slurm_persist_conn_close(persist_conn);

	if (persist_conn->auth_cred) {
		auth_g_destroy(persist_conn->auth_cred);
		persist_conn->auth_cred = NULL;
		persist_conn->auth.uid = SLURM_AUTH_NOBODY;
		persist_conn->auth.gid = SLURM_AUTH_NOBODY;
		persist_conn->auth.ids_set = false;
	}
	xfree(persist_conn->cluster_name);
	xfree(persist_conn->rem_host);
}

/* Close the persistent connection */
extern void slurm_persist_conn_destroy(persist_conn_t *persist_conn)
{
	if (!persist_conn)
		return;
	slurm_persist_conn_members_destroy(persist_conn);
	xfree(persist_conn);
}

extern int slurm_persist_conn_process_msg(persist_conn_t *persist_conn,
					  persist_msg_t *persist_msg,
					  char *msg_char, uint32_t msg_size,
					  buf_t **out_buffer, bool first)
{
	int rc;
	buf_t *recv_buffer = NULL;
	char *comment = NULL;
	bool init_msg = false;

	/* puts msg_char into buffer struct */
	recv_buffer = create_buf(msg_char, msg_size);

	memset(persist_msg, 0, sizeof(persist_msg_t));
	rc = slurm_persist_msg_unpack(persist_conn, persist_msg, recv_buffer);
	xfer_buf_data(recv_buffer); /* delete in_buffer struct
				     * without xfree of msg_char
				     * (done later in this
				     * function). */

	if (rc != SLURM_SUCCESS) {
		comment = xstrdup_printf("Failed to unpack %s message",
					 slurmdbd_msg_type_2_str(
						 persist_msg->msg_type, true));
		error("CONN:%u %s",
		      conn_g_get_fd(persist_conn->conn), comment);
		*out_buffer = slurm_persist_make_rc_msg(
			persist_conn, rc, comment, persist_msg->msg_type);
		xfree(comment);

		return rc;
	}

	if (persist_msg->msg_type == REQUEST_PERSIST_INIT)
		init_msg = true;

	if (first && !init_msg) {
		comment = "Initial RPC not REQUEST_PERSIST_INIT";
		error("CONN:%u %s type (%d)",
		      conn_g_get_fd(persist_conn->conn), comment,
		      persist_msg->msg_type);
		rc = EINVAL;
		*out_buffer = slurm_persist_make_rc_msg(
			persist_conn, rc, comment,
			REQUEST_PERSIST_INIT);
	} else if (!first && init_msg) {
		comment = "REQUEST_PERSIST_INIT sent after connection established";
		error("CONN:%u %s",
		      conn_g_get_fd(persist_conn->conn),
		      comment);
		rc = EINVAL;
		*out_buffer =
			slurm_persist_make_rc_msg(persist_conn, rc, comment,
						  REQUEST_PERSIST_INIT);
	}

	return rc;
}

/* Wait until a file is writeable,
 * RET 1 if file can be written now,
 *     0 if can not be written to within 5 seconds
 *     -1 if file has been closed POLLHUP
 */
extern int slurm_persist_conn_writeable(persist_conn_t *persist_conn)
{
	struct pollfd ufds;
	int write_timeout = 5000;
	int rc, time_left;
	struct timeval tstart;
	char temp[2];
	int fd;

	if (!persist_conn || !persist_conn->shutdown)
		fatal("%s: unexpected NULL persist_conn", __func__);

	if (!persist_conn->conn) {
		log_flag(NET, "%s: called on invalid connection to host %s:%hu",
		         __func__, (persist_conn->rem_host ?
				    persist_conn->rem_host :
				    "unknown"),
			 persist_conn->rem_port);
		return -1;
	}
	fd = conn_g_get_fd(persist_conn->conn);

	if (*persist_conn->shutdown) {
		log_flag(NET, "%s: called on shutdown fd:%d to host %s:%hu",
		         __func__, fd, (persist_conn->rem_host ?
					persist_conn->rem_host :
					"unknown"),
		         persist_conn->rem_port);
		return -1;
	}

	ufds.fd = fd;
	ufds.events = POLLOUT;
	gettimeofday(&tstart, NULL);
	while (!*persist_conn->shutdown) {
		time_left = write_timeout - timeval_tot_wait(&tstart);
		rc = poll(&ufds, 1, time_left);
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("%s: poll error: %m", __func__);
			return -1;
		}
		if (rc == 0)
			return 0;
		/*
		 * Check here to make sure the socket really is there.
		 * If not then exit out and notify the conn.  This
		 * is here since a write doesn't always tell you the
		 * socket is gone, but getting 0 back from a
		 * nonblocking read means just that.
		 */
		if (ufds.revents & POLLHUP ||
		    (recv(ufds.fd, &temp, 1, 0) == 0)) {
			log_flag(NET, "%s: persistent connection %d is closed for writes",
				 __func__, ufds.fd);
			if (persist_conn->trigger_callbacks.dbd_fail)
				(persist_conn->trigger_callbacks.dbd_fail)();
			persist_conn->skip_conn_shutdown = true;
			return -1;
		}
		if (ufds.revents & POLLNVAL) {
			error("%s: persistent connection %d is invalid",
			      __func__, ufds.fd);
			return 0;
		}
		if (ufds.revents & POLLERR) {
			if (_comm_fail_log(persist_conn)) {
				int rc, err;
				if ((rc = fd_get_socket_error(ufds.fd, &err)))
					error("%s: unable to get error for persistent connection %d: %s",
					      __func__, ufds.fd,
					      strerror(rc));
				else
					error("%s: persistent connection %d experienced an error: %s",
					      __func__, ufds.fd,
					      strerror(err));
				errno = err;
			}
			if (persist_conn->trigger_callbacks.dbd_fail)
				(persist_conn->trigger_callbacks.dbd_fail)();
			return 0;
		}
		if ((ufds.revents & POLLOUT) == 0) {
			error("%s: persistent connection %d events %d",
			      __func__, ufds.fd, ufds.revents);
			return 0;
		}
		/* revents == POLLOUT */
		errno = 0;
		return 1;
	}
	return 0;
}

extern int slurm_persist_send_msg(persist_conn_t *persist_conn,
				  buf_t *buffer)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_wrote;
	int rc, retry_cnt = 0;

	xassert(persist_conn);

	if (!persist_conn->conn)
		return EAGAIN;

	if (!buffer)
		return SLURM_ERROR;

	rc = slurm_persist_conn_writeable(persist_conn);
	if (rc == -1) {
	re_open:
		/* if errno is ACCESS_DENIED do not try to reopen to
		   connection just return that */
		if (errno == ESLURM_ACCESS_DENIED)
			return ESLURM_ACCESS_DENIED;

		if (retry_cnt++ > 3)
			return SLURM_COMMUNICATIONS_SEND_ERROR;

		if (persist_conn->flags & PERSIST_FLAG_RECONNECT) {
			slurm_persist_conn_reopen(persist_conn);
			rc = slurm_persist_conn_writeable(persist_conn);
		} else
			return SLURM_ERROR;
	}
	if (rc < 1)
		return EAGAIN;

	msg_size = get_buf_offset(buffer);
	nw_size = htonl(msg_size);

	msg_wrote = conn_g_send(persist_conn->conn, &nw_size, sizeof(nw_size));
	if (msg_wrote != sizeof(nw_size))
		return EAGAIN;

	msg = get_buf_data(buffer);
	while (msg_size > 0) {
		rc = slurm_persist_conn_writeable(persist_conn);
		if (rc == -1)
			goto re_open;
		if (rc < 1)
			return EAGAIN;
		msg_wrote = conn_g_send(persist_conn->conn, msg, msg_size);
		if (msg_wrote <= 0)
			return EAGAIN;
		msg += msg_wrote;
		msg_size -= msg_wrote;
	}

	return SLURM_SUCCESS;
}

static buf_t *_slurm_persist_recv_msg(persist_conn_t *persist_conn,
				      bool reopen)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_read, offset;
	buf_t *buffer;

	xassert(persist_conn);

	if (!persist_conn->conn) {
		if (!persist_conn->shutdown || *persist_conn->shutdown)
			log_flag(NET, "%s: Invalid connection to host:%s port:%u",
				 __func__,
				 persist_conn->rem_host,
				 persist_conn->rem_port);
		return NULL;
	}

	if (!_conn_readable(persist_conn)) {
		log_flag(NET, "%s: Unable to read from file descriptor (%d)",
			 __func__, conn_g_get_fd(persist_conn->conn));
		goto endit;
	}

	msg_read = conn_g_recv(persist_conn->conn, &nw_size, sizeof(nw_size));
	if (msg_read != sizeof(nw_size)) {
		log_flag(NET, "%s: Unable to read message size: only read %zd bytes of expected %zu.",
			 __func__, msg_read, sizeof(nw_size));
		goto endit;
	}
	msg_size = ntohl(nw_size);
	/* Sanity check size is not too small or the max possible */
	if ((msg_size == INFINITE) || (msg_size == NO_VAL) || (msg_size < 2)) {
		error("%s: Invalid msg_size: %u bytes",
		      __func__, msg_size);
		goto endit;
	}

	msg = try_xmalloc(msg_size);
	if (!msg) {
		error("%s: Unable to allocate msg with %u bytes",
		      __func__, msg_size);
		goto endit;
	}

	offset = 0;
	while (msg_size > offset) {
		if (!_conn_readable(persist_conn))
			break;		/* problem with this socket */
		msg_read = conn_g_recv(persist_conn->conn, (msg + offset),
				       (msg_size - offset));
		if (msg_read <= 0) {
			error("%s: read of fd %u failed: %m",
			      __func__,
			      conn_g_get_fd(persist_conn->conn));
			break;
		}
		offset += msg_read;
	}
	if (msg_size != offset) {
		if (!(*persist_conn->shutdown)) {
			error("%s: only read %zd of %d bytes",
			      __func__, offset, msg_size);
		}	/* else in shutdown mode */
		xfree(msg);
		goto endit;
	}

	buffer = create_buf(msg, msg_size);
	return buffer;

endit:
	/* Close it since we abandoned it.  If the connection does still exist
	 * on the other end we can't rely on it after this point since we didn't
	 * listen long enough for this response.
	 */
	if (reopen && !(*persist_conn->shutdown) &&
	    persist_conn->flags & PERSIST_FLAG_RECONNECT) {
		log_flag(NET, "%s: reopening persistent connection after error",
			 __func__);
		slurm_persist_conn_reopen(persist_conn);
	}

	return NULL;
}

extern buf_t *slurm_persist_recv_msg(persist_conn_t *persist_conn)
{
	return _slurm_persist_recv_msg(persist_conn, true);
}

extern buf_t *slurm_persist_msg_pack(persist_conn_t *persist_conn,
				     persist_msg_t *req_msg)
{
	buf_t *buffer;

	xassert(persist_conn);

	if (persist_conn->flags & PERSIST_FLAG_DBD) {
		slurmdbd_msg_t dbd_msg = {
			.data = req_msg->data,
			.msg_type = req_msg->msg_type,
		};
		buffer = pack_slurmdbd_msg(&dbd_msg, persist_conn->version);
	} else {
		slurm_msg_t msg;

		slurm_msg_t_init(&msg);

		msg.data      = req_msg->data;
		msg.msg_type  = req_msg->msg_type;
		msg.protocol_version = persist_conn->version;

		buffer = init_buf(INFINITE);

		pack16(req_msg->msg_type, buffer);
		if (pack_msg(&msg, buffer) != SLURM_SUCCESS) {
			FREE_NULL_BUFFER(buffer);
			return NULL;
                }
	}

	return buffer;
}

extern int slurm_persist_msg_unpack(persist_conn_t *persist_conn,
				    persist_msg_t *resp_msg, buf_t *buffer)
{
	int rc;

	xassert(persist_conn);
	xassert(resp_msg);

	if (persist_conn->flags & PERSIST_FLAG_DBD) {
		slurmdbd_msg_t dbd_msg = { 0 };

		rc = unpack_slurmdbd_msg(&dbd_msg, persist_conn->version,
					 buffer);
		resp_msg->msg_type = dbd_msg.msg_type;
		resp_msg->data = dbd_msg.data;
	} else {
		slurm_msg_t msg;

		slurm_msg_t_init(&msg);

		msg.protocol_version = persist_conn->version;

		safe_unpack16(&msg.msg_type, buffer);

		rc = unpack_msg(&msg, buffer);

		resp_msg->msg_type = msg.msg_type;
		resp_msg->data = msg.data;
	}

	if (rc != SLURM_SUCCESS)
		return rc;

	/* Here we transfer the auth_cred to the persist_conn just in case in the
	 * future we need to use it in some way to verify things for messages
	 * that don't have on that will follow on the connection.
	 */
	if (resp_msg->msg_type == REQUEST_PERSIST_INIT) {
		slurm_msg_t *msg = resp_msg->data;
		if (persist_conn->auth_cred)
			auth_g_destroy(persist_conn->auth_cred);

		persist_conn->auth_cred = msg->auth_cred;
		persist_conn->auth = msg->auth;
		msg->auth_cred = NULL;
	}

	return rc;
unpack_error:
	return SLURM_ERROR;
}

extern buf_t *slurm_persist_make_rc_msg(persist_conn_t *persist_conn,
					uint32_t rc, char *comment,
					uint16_t ret_info)
{
	persist_rc_msg_t msg;
	persist_msg_t resp;

	memset(&msg, 0, sizeof(persist_rc_msg_t));
	memset(&resp, 0, sizeof(persist_msg_t));

	msg.rc = rc;
	msg.comment = comment;
	msg.ret_info = ret_info;

	resp.msg_type = PERSIST_RC;
	resp.data = &msg;

	return slurm_persist_msg_pack(persist_conn, &resp);
}

/*
 * Parse and dispatch the frame at the start of data.
 * IN ref - connection
 * IN handler - called with the borrowed payload
 * IN arg - passed to handler
 * IN data - buffered input, starting at a frame header
 * IN available - bytes buffered at data
 * OUT consumed - bytes of the frame handled; 0 while the frame is incomplete
 * RET SLURM_SUCCESS, or the parser or handler error (the frame is consumed
 *	even when the handler fails)
 */
static int _parse_frame(conmgr_fd_ref_t *ref,
			persist_conn_frame_handler_t handler, void *arg,
			const void *data, size_t available, size_t *consumed)
{
	uint32_t msglen;
	size_t frame_bytes;
	buf_t frame;
	int rc;

	*consumed = 0;

	/* The length may arrive across reads like the rest. */
	if (available < sizeof(msglen)) {
		log_flag(NET, "%s: [%s] partial frame header buffered (%zu of %zu bytes); awaiting remainder",
			 __func__, conmgr_con_get_name(ref), available,
			 sizeof(msglen));
		return SLURM_SUCCESS;
	}

	memcpy(&msglen, data, sizeof(msglen));
	msglen = ntohl(msglen);

	if ((msglen < 2) || (msglen > MAX_MSG_SIZE)) {
		error("%s: [%s] invalid persist frame length: %u",
		      __func__, conmgr_con_get_name(ref), msglen);
		return SLURM_PROTOCOL_INSANE_MSG_LENGTH;
	}

	frame_bytes = sizeof(msglen) + msglen;

	if (available < frame_bytes) {
		log_flag(NET, "%s: [%s] partial frame buffered (%zu of %zu bytes); awaiting remainder",
			 __func__, conmgr_con_get_name(ref), available,
			 frame_bytes);
		return SLURM_SUCCESS;
	}

	frame = (buf_t) SHADOW_BUF_INITIALIZER((char *) data + sizeof(msglen),
					       msglen);

	set_buf_offset(&frame, 0);

	log_flag_hex(NET_RAW, get_buf_data(&frame), size_buf(&frame),
		     "%s: [%s] dispatching persist frame",
		     __func__, conmgr_con_get_name(ref));

	rc = handler(ref, &frame, arg);

	*consumed = frame_bytes;

	if (rc != SLURM_SUCCESS)
		log_flag(NET, "%s: [%s] handler returned %s; aborting parse",
			 __func__, conmgr_con_get_name(ref),
			 slurm_strerror(rc));

	return rc;
}

extern int persist_conn_parse(conmgr_fd_ref_t *ref,
			      persist_conn_frame_handler_t handler, void *arg)
{
	const void *base_data = NULL;
	size_t total = 0;
	size_t consumed = 0;
	int rc = SLURM_SUCCESS;

	xassert(ref);
	xassert(handler);

	/* Track consumption locally; conmgr reports the full buffer. */
	if ((rc = conmgr_con_get_input_buffer(ref, &base_data, &total)))
		return rc;

	while (consumed < total) {
		size_t frame_consumed = 0;

		rc = _parse_frame(ref, handler, arg,
				  (const char *) base_data + consumed,
				  total - consumed, &frame_consumed);
		consumed += frame_consumed;

		/* Stop on error, or until the rest of a frame arrives. */
		if (rc || !frame_consumed)
			break;
	}

	if (consumed > 0)
		(void) conmgr_con_mark_consumed_input_buffer(ref, consumed);

	return rc;
}

/* Initialize msg on conn, before or after it is authenticated. */
static void _msg_init(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg)
{
	slurm_msg_t_init(msg);
	msg->auth = pcon_ctx->auth;
	msg->protocol_version = pcon_ctx->version;
	msg->conmgr_con = conmgr_con_link(pcon_ctx->ref);
	msg->pcon_ctx = pcon_ctx;
	/* The session authenticated at REQUEST_PERSIST_INIT. */
	msg->flags |= SLURM_PERSIST_MSG;
	slurm_msg_set_r_uid(msg, SLURM_AUTH_UID_ANY);
}

extern void persist_conn_msg_init(persist_conn_ctx_t *pcon_ctx,
				  slurm_msg_t *msg)
{
	xassert(pcon_ctx->auth.ids_set);

	_msg_init(pcon_ctx, msg);
}

/*
 * Reply to the session with a PERSIST_RC when there is no request to reply
 * through, such as for a frame that cannot be unpacked.
 * IN ret_info - message type (or version for init) this replies to
 */
static void _reply_rc(persist_conn_ctx_t *pcon_ctx, uint32_t rc,
		      const char *comment, uint16_t ret_info)
{
	slurm_msg_t msg;

	_msg_init(pcon_ctx, &msg);
	msg.msg_type = ret_info;
	(void) slurm_send_rc_err_msg(&msg, rc, (char *) comment);
	CONMGR_CON_UNLINK(msg.conmgr_con);
}

/*
 * Unpack [msg_type][body]. Only DBD clients send REQUEST_PERSIST_INIT this
 * way: [REQUEST_PERSIST_INIT][authenticated Slurm message].
 */
static int _unpack(persist_conn_ctx_t *pcon_ctx, buf_t *frame,
		   uint16_t *msg_type, void **data)
{
	uint16_t type = 0;
	int fd = -1, rc;

	if (unpack16(&type, frame))
		return SLURM_ERROR;
	*msg_type = type;

	if ((type == REQUEST_PERSIST_INIT) &&
	    (pcon_ctx->ops->flags & PERSIST_FLAG_DBD)) {
		slurm_msg_t *msg = xmalloc(sizeof(*msg));

		slurm_msg_t_init(msg);
		(void) conmgr_con_get_input_fd(pcon_ctx->ref, &fd);
		if ((rc = slurm_unpack_received_msg(msg, fd, frame))) {
			slurm_free_msg(msg);
			return rc;
		}
		*data = msg;
		return SLURM_SUCCESS;
	}

	if (pcon_ctx->ops->flags & PERSIST_FLAG_DBD) {
		slurmdbd_msg_t msg = { 0 };

		/* unpack_slurmdbd_msg() reads the type itself. */
		set_buf_offset(frame, 0);
		rc = unpack_slurmdbd_msg(&msg, pcon_ctx->version, frame);
		*data = msg.data;
		return rc;
	} else {
		slurm_msg_t msg;

		slurm_msg_t_init(&msg);
		msg.msg_type = type;
		msg.protocol_version = pcon_ctx->version;
		rc = unpack_msg(&msg, frame);
		*data = msg.data;
		return rc;
	}
}

static void _free_data(persist_conn_ctx_t *pcon_ctx, uint16_t msg_type,
		       void *data)
{
	if (!data)
		return;

	if ((msg_type == REQUEST_PERSIST_INIT) &&
	    (pcon_ctx->ops->flags & PERSIST_FLAG_DBD)) {
		slurm_free_msg(data);
		return;
	}

	if (pcon_ctx->ops->flags & PERSIST_FLAG_DBD) {
		slurmdbd_msg_t msg = {
			.msg_type = msg_type,
			.data = data,
		};

		slurmdbd_free_msg(&msg);
	} else {
		slurm_free_msg_data(msg_type, data);
	}
}

/* Validate and accept REQUEST_PERSIST_INIT, caching its identity. */
static int _init(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg)
{
	persist_init_req_msg_t *req = msg->data;
	persist_rc_msg_t reply = { 0 };
	const char *comment = NULL;
	slurm_msg_t init;
	int rc;

	if (pcon_ctx->auth.ids_set) {
		comment =
			"REQUEST_PERSIST_INIT sent after connection established";
	} else if (msg->msg_type != REQUEST_PERSIST_INIT) {
		/* Outer type is unauthenticated; inner type is not. */
		comment = "Invalid REQUEST_PERSIST_INIT";
	} else if (!msg->auth.ids_set) {
		/* SLURM_NO_AUTH_CRED can unpack without an identity. */
		comment = "REQUEST_PERSIST_INIT not authenticated";
	}

	if (comment) {
		error("[%s] %s", conmgr_con_get_name(pcon_ctx->ref), comment);
		_reply_rc(pcon_ctx, SLURM_PROTOCOL_AUTHENTICATION_ERROR,
			  comment, REQUEST_PERSIST_INIT);
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}

	pcon_ctx->auth_cred = msg->auth_cred;
	pcon_ctx->auth = msg->auth;
	msg->auth_cred = NULL;

	/* Replies use the version the client asked for. */
	pcon_ctx->version = req->version;
	pcon_ctx->cluster_name = xstrdup(req->cluster_name);
	pcon_ctx->persist_type = req->persist_type;

	reply.ret_info = pcon_ctx->version;

	persist_conn_msg_init(pcon_ctx, &init);
	init.msg_type = REQUEST_PERSIST_INIT;
	init.data = req;
	rc = pcon_ctx->ops->on_init(pcon_ctx, &init, &reply.flags);

	/* The reply carries the negotiated version in ret_info. */
	reply.rc = rc;
	if (rc)
		reply.comment = slurm_strerror(rc);
	(void) send_msg_response(&init, PERSIST_RC, &reply);
	CONMGR_CON_UNLINK(init.conmgr_con);

	return rc;
}

/* Errors that end the connection after their reply. */
static bool _close_on_rc(int rc)
{
	return ((rc == ESLURM_ACCESS_DENIED) ||
		(rc == SLURM_PROTOCOL_AUTHENTICATION_ERROR) ||
		(rc == SLURM_PROTOCOL_VERSION_ERROR));
}

static int _on_frame(conmgr_fd_ref_t *ref, buf_t *frame, void *arg)
{
	persist_conn_ctx_t *pcon_ctx = arg;
	uint16_t msg_type = 0;
	void *data = NULL;
	slurm_msg_t msg;
	int rc;

	xassert(pcon_ctx->magic == PERSIST_CONN_CTX_MAGIC);

	log_flag(NET, "%s: [%s] received %u byte frame",
		 __func__, conmgr_con_get_name(ref), size_buf(frame));

	if ((rc = _unpack(pcon_ctx, frame, &msg_type, &data))) {
		char *comment =
			xstrdup_printf("Failed to unpack %s message",
				       slurmdbd_msg_type_2_str(msg_type, true));

		error("[%s] %s", conmgr_con_get_name(ref), comment);
		_reply_rc(pcon_ctx, rc, comment, msg_type);
		xfree(comment);
		_free_data(pcon_ctx, msg_type, data);
		/* Before init, any failure closes; after, the reply suffices. */
		if (!pcon_ctx->auth.ids_set)
			return rc;
		goto done;
	}

	/* Framed init from DBD clients; _init() rejects a second. */
	if (msg_type == REQUEST_PERSIST_INIT) {
		log_flag(NET, "%s: [%s] received framed REQUEST_PERSIST_INIT",
			 __func__, conmgr_con_get_name(ref));

		rc = _init(pcon_ctx, data);
		_free_data(pcon_ctx, msg_type, data);
		return rc;
	}

	/* A session's first frame must be REQUEST_PERSIST_INIT. */
	if (!pcon_ctx->auth.ids_set) {
		const char *comment = "Initial RPC not REQUEST_PERSIST_INIT";

		error("[%s] %s (got %u)", conmgr_con_get_name(ref), comment,
		      msg_type);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		_reply_rc(pcon_ctx, rc, comment, REQUEST_PERSIST_INIT);
		_free_data(pcon_ctx, msg_type, data);
		return rc;
	}

	log_flag(NET, "%s: [%s] dispatching msg_type=%s",
		 __func__, conmgr_con_get_name(ref),
		 slurmdbd_msg_type_2_str(msg_type, true));

	persist_conn_msg_init(pcon_ctx, &msg);
	msg.msg_type = msg_type;
	msg.data = data;
	rc = pcon_ctx->ops->on_msg(pcon_ctx, &msg);
	xassert(msg.data == data);
	CONMGR_CON_UNLINK(msg.conmgr_con);

	_free_data(pcon_ctx, msg_type, data);

done:
	/* Close on auth or version errors. */
	if (_close_on_rc(rc))
		return rc;

	return SLURM_SUCCESS;
}

static int _on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	return persist_conn_parse(conmgr_args.ref, _on_frame, arg);
}

static void _free_conn(persist_conn_ctx_t *pcon_ctx)
{
	xassert(pcon_ctx->magic == PERSIST_CONN_CTX_MAGIC);

	if (pcon_ctx->auth_cred)
		auth_g_destroy(pcon_ctx->auth_cred);
	xfree(pcon_ctx->cluster_name);
	xfree(pcon_ctx->rem_host);
	CONMGR_CON_UNLINK(pcon_ctx->ref);

	pcon_ctx->magic = ~PERSIST_CONN_CTX_MAGIC;
	xfree(pcon_ctx);
}

static void _on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	persist_conn_ctx_t *pcon_ctx = arg;

	xassert(pcon_ctx->magic == PERSIST_CONN_CTX_MAGIC);

	if (pcon_ctx->ops->on_fini)
		pcon_ctx->ops->on_fini(pcon_ctx);

	_free_conn(pcon_ctx);
}

static const conmgr_events_t events = {
	.on_data = _on_data,
	.on_finish = _on_finish,
};

extern int persist_conn_assign(conmgr_fd_ref_t *ref,
			       const persist_conn_ops_t *ops,
			       const conmgr_timeouts_t *timeouts, void *arg,
			       slurm_msg_t *init)
{
	persist_conn_ctx_t *pcon_ctx = xmalloc(sizeof(*pcon_ctx));
	slurm_addr_t addr = { 0 };
	int rc;

	xassert(ops && ops->on_init && ops->on_msg);

	*pcon_ctx = (persist_conn_ctx_t) {
		.magic = PERSIST_CONN_CTX_MAGIC,
		.ref = conmgr_con_link(ref),
		.ops = ops,
		.fd = -1,
		.rem_host = xmalloc(INET6_ADDRSTRLEN),
		.auth.uid = SLURM_AUTH_NOBODY,
		.auth.gid = SLURM_AUTH_NOBODY,
		.version = SLURM_MIN_PROTOCOL_VERSION,
		.arg = arg,
	};

	(void) conmgr_con_get_input_fd(pcon_ctx->ref, &pcon_ctx->fd);
	if (!slurm_get_peer_addr(pcon_ctx->fd, &addr))
		slurm_get_ip_str(&addr, pcon_ctx->rem_host, INET6_ADDRSTRLEN);

	if ((rc = conmgr_con_set_events(ref, &events, pcon_ctx, __func__))) {
		_on_finish((conmgr_callback_args_t) { 0 }, pcon_ctx);
		return rc;
	}

	/* pcon_ctx now belongs to the connection; _on_finish() frees it. */
	if ((timeouts &&
	     (rc = conmgr_con_set_timeouts(ref, timeouts, __func__))) ||
	    (rc = conmgr_con_change_mode(ref, CON_TYPE_RAW))) {
		conmgr_con_queue_close(ref);
		return rc;
	}

	log_flag(NET, "%s: [%s] persistent connection from %s",
		 __func__, conmgr_con_get_name(ref), pcon_ctx->rem_host);

	/* Without init, the first frame must be REQUEST_PERSIST_INIT. */
	if (!init)
		return SLURM_SUCCESS;

	xassert(init->msg_type == REQUEST_PERSIST_INIT);

	if ((rc = _init(pcon_ctx, init)))
		conmgr_con_queue_close(ref);

	return rc;
}
