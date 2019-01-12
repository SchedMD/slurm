/*****************************************************************************\
 *  slurm_persist_conn.h - Definitions for communicating over a persistent
 *                         connection within Slurm.
 ******************************************************************************
 *  Copyright (C) 2016 SchedMD LLC
 *  Written by Danny Auble da@schedmd.com, et. al.
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

#include <poll.h>
#include <pthread.h>

#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/xsignal.h"
#include "slurm_persist_conn.h"

#define MAX_THREAD_COUNT 100

/*
 *  Maximum message size. Messages larger than this value (in bytes)
 *  will not be received.
 */
#define MAX_MSG_SIZE     (16*1024*1024)

typedef struct {
	void *arg;
	slurm_persist_conn_t *conn;
	int thread_loc;
	pthread_t thread_id;
} persist_service_conn_t;

static persist_service_conn_t *persist_service_conn[MAX_THREAD_COUNT];
static int             thread_count = 0;
static pthread_mutex_t thread_count_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  thread_count_cond = PTHREAD_COND_INITIALIZER;
static int             shutdown_time = 0;

/* Return time in msec since "start time" */
static int _tot_wait (struct timeval *start_time)
{
	struct timeval end_time;
	int msec_delay;

	gettimeofday(&end_time, NULL);
	msec_delay =   (end_time.tv_sec  - start_time->tv_sec ) * 1000;
	msec_delay += ((end_time.tv_usec - start_time->tv_usec + 500) / 1000);
	return msec_delay;
}

/* close and fd and replace it with a -1 */
static void _close_fd(int *fd)
{
	if (*fd && *fd >= 0) {
		close(*fd);
		*fd = -1;
	}
}

/* Return true if communication failure should be logged. Only log failures
 * every 10 minutes to avoid filling logs */
static bool _comm_fail_log(slurm_persist_conn_t *persist_conn)
{
	time_t now = time(NULL);
	time_t old = now - 600;	/* Log failures once every 10 mins */

	if (persist_conn->comm_fail_time < old) {
		persist_conn->comm_fail_time = now;
		return true;
	}
	return false;
}

/* static void _reopen_persist_conn(slurm_persist_conn_t *persist_conn) */
/* { */
/*	xassert(persist_conn); */
/*	_close_fd(&persist_conn->fd); */
/*	slurm_persist_conn_open(persist_conn); */
/* } */

/* Wait until a file is readable,
 * RET false if can not be read */
static bool _conn_readable(slurm_persist_conn_t *persist_conn)
{
	struct pollfd ufds;
	int rc, time_left;

	xassert(persist_conn->shutdown);

	ufds.fd     = persist_conn->fd;
	ufds.events = POLLIN;
	while (!(*persist_conn->shutdown)) {
		if (persist_conn->timeout) {
			struct timeval tstart;
			gettimeofday(&tstart, NULL);
			time_left = persist_conn->timeout - _tot_wait(&tstart);
		} else
			time_left = -1;
		rc = poll(&ufds, 1, time_left);
		if (*persist_conn->shutdown)
			return false;
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("poll: %m");
			return false;
		}
		if (rc == 0)
			return false;
		if ((ufds.revents & POLLHUP) &&
		    ((ufds.revents & POLLIN) == 0)) {
			debug2("persistent connection closed");
			return false;
		}
		if (ufds.revents & POLLNVAL) {
			error("persistent connection is invalid");
			return false;
		}
		if (ufds.revents & POLLERR) {
			error("persistent connection experienced an error");
			return false;
		}
		if ((ufds.revents & POLLIN) == 0) {
			error("persistent connection %d events %d",
			      persist_conn->fd, ufds.revents);
			return false;
		}
		/* revents == POLLIN */
		errno = 0;
		return true;
	}
	return false;
}

static void _destroy_persist_service(persist_service_conn_t *persist_service)
{
	if (persist_service) {
		slurm_persist_conn_destroy(persist_service->conn);
		xfree(persist_service);
	}
}

static void _sig_handler(int signal)
{
}

static void _persist_free_msg_members(slurm_persist_conn_t *persist_conn,
				      persist_msg_t *persist_msg)
{
	if (persist_conn->flags & PERSIST_FLAG_DBD)
		slurmdbd_free_msg((slurmdbd_msg_t *)persist_msg);
	else
		slurm_free_msg_data(persist_msg->msg_type, persist_msg->data);
}

static int _process_service_connection(
	slurm_persist_conn_t *persist_conn, void *arg)
{
	uint32_t nw_size = 0, msg_size = 0, uid = NO_VAL;
	char *msg_char = NULL;
	ssize_t msg_read = 0, offset = 0;
	bool first = true, fini = false;
	Buf buffer = NULL;
	int rc = SLURM_SUCCESS;

	xassert(persist_conn->callback_proc);
	xassert(persist_conn->shutdown);

	debug2("Opened connection %d from %s", persist_conn->fd,
	       persist_conn->rem_host);

	if (persist_conn->flags & PERSIST_FLAG_ALREADY_INITED)
		first = false;

	while (!(*persist_conn->shutdown) && !fini) {
		if (!_conn_readable(persist_conn))
			break;		/* problem with this socket */
		msg_read = read(persist_conn->fd, &nw_size, sizeof(nw_size));
		if (msg_read == 0)	/* EOF */
			break;
		if (msg_read != sizeof(nw_size)) {
			error("Could not read msg_size from "
			      "connection %d(%s) uid(%d)",
			      persist_conn->fd, persist_conn->rem_host, uid);
			break;
		}
		msg_size = ntohl(nw_size);
		if ((msg_size < 2) || (msg_size > MAX_MSG_SIZE)) {
			error("Invalid msg_size (%u) from "
			      "connection %d(%s) uid(%d)",
			      msg_size, persist_conn->fd,
			      persist_conn->rem_host, uid);
			break;
		}

		msg_char = xmalloc(msg_size);
		offset = 0;
		while (msg_size > offset) {
			if (!_conn_readable(persist_conn))
				break;		/* problem with this socket */
			msg_read = read(persist_conn->fd, (msg_char + offset),
					(msg_size - offset));
			if (msg_read <= 0) {
				error("read(%d): %m", persist_conn->fd);
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
				rc = (persist_conn->callback_proc)(
					arg, &msg, &buffer, &uid);
				_persist_free_msg_members(persist_conn, &msg);
				if (rc != SLURM_SUCCESS &&
				    rc != ACCOUNTING_FIRST_REG &&
				    rc != ACCOUNTING_TRES_CHANGE_DB &&
				    rc != ACCOUNTING_NODES_CHANGE_DB) {
					error("Processing last message from "
					      "connection %d(%s) uid(%d)",
					      persist_conn->fd,
					      persist_conn->rem_host, uid);
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
					debug("Problem sending response to "
					      "connection %d(%s) uid(%d)",
					      persist_conn->fd,
					      persist_conn->rem_host, uid);
				fini = true;
			}
			free_buf(buffer);
		}
	}

	debug2("Closed connection %d uid(%d)", persist_conn->fd, uid);

	return rc;
}

static void *_service_connection(void *arg)
{
	persist_service_conn_t *service_conn = arg;

	xassert(service_conn);
	xassert(service_conn->conn);

#if HAVE_SYS_PRCTL_H
	char *name = xstrdup_printf("p-%s",
				    service_conn->conn->cluster_name);
	if (prctl(PR_SET_NAME, name, NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, name);
	}
	xfree(name);
#endif

	service_conn->thread_id = pthread_self();

	_process_service_connection(service_conn->conn, service_conn->arg);

	if (service_conn->conn->callback_fini)
		(service_conn->conn->callback_fini)(service_conn->arg);
	else
		debug("Persist connection from cluster %s has disconnected",
		      service_conn->conn->cluster_name);

	/* service_conn is freed inside here */
	slurm_persist_conn_free_thread_loc(service_conn->thread_loc);
//	xfree(service_conn);

	/* In order to avoid zombie threads, detach the thread now before
	 * exiting.  slurm_persist_conn_recv_server_fini() will not try to join
	 * the thread because slurm_persist_conn_free_thread_loc() will have
	 * free'd the connection. If their are threads at shutdown, the join
	 * will happen before the detach so recv_fini() will wait until the
	 * thread is done.
	 *
	 * pthread_join man page:
	 * Failure to join with a thread that is joinable (i.e., one that is not
	 * detached), produces a "zombie thread". Avoid doing this, since each
	 * zombie thread consumes some system resources, and when enough zombie
	 * threads have accumulated, it will no longer be possible to create new
	 * threads (or processes).
	 */
	pthread_detach(pthread_self());

	return NULL;
}

extern void slurm_persist_conn_recv_server_init(void)
{
	int sigarray[] = {SIGUSR1, 0};

	shutdown_time = 0;

	(void) pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	(void) pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

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

	shutdown_time = time(NULL);
	slurm_mutex_lock(&thread_count_lock);
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
		if (persist_service_conn[i]->thread_id) {
			pthread_t thread_id =
				persist_service_conn[i]->thread_id;

			/* Let go of lock in case the persistent connection
			 * thread is cleaning itself up.
			 * slurm_persist_conn_free_thread_loc() may be trying to
			 * remove itself but could be waiting on the
			 * thread_count mutex which this has locked. */
			slurm_mutex_unlock(&thread_count_lock);
			pthread_join(thread_id, NULL);
			slurm_mutex_lock(&thread_count_lock);
		}
		_destroy_persist_service(persist_service_conn[i]);
		persist_service_conn[i] = NULL;
	}
	slurm_mutex_unlock(&thread_count_lock);
}

extern void slurm_persist_conn_recv_thread_init(slurm_persist_conn_t *persist_conn,
						int thread_loc, void *arg)
{
	persist_service_conn_t *service_conn;

	if (thread_loc < 0)
		thread_loc = slurm_persist_conn_wait_for_thread_loc();
	if (thread_loc < 0)
		return;

	service_conn = xmalloc(sizeof(persist_service_conn_t));

	slurm_mutex_lock(&thread_count_lock);
	persist_service_conn[thread_loc] = service_conn;
	slurm_mutex_unlock(&thread_count_lock);

	service_conn->arg = arg;
	service_conn->conn = persist_conn;
	service_conn->thread_loc = thread_loc;

	persist_conn->timeout = 0; /* If this isn't zero we won't wait forever
				      like we want to.
				   */

	//_service_connection(service_conn);
	slurm_thread_create(&persist_service_conn[thread_loc]->thread_id,
			    _service_connection, service_conn);
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
extern void slurm_persist_conn_free_thread_loc(int thread_loc)
{
	/* we will handle this in the fini */
	if (shutdown_time)
		return;

	slurm_mutex_lock(&thread_count_lock);
	if (thread_count > 0)
		thread_count--;
	else
		error("thread_count underflow");

	_destroy_persist_service(persist_service_conn[thread_loc]);
	persist_service_conn[thread_loc] = NULL;

	slurm_cond_broadcast(&thread_count_cond);
	slurm_mutex_unlock(&thread_count_lock);
}

extern int slurm_persist_conn_open_without_init(
	slurm_persist_conn_t *persist_conn)
{
	slurm_addr_t addr;

	xassert(persist_conn);
	xassert(persist_conn->rem_host);
	xassert(persist_conn->rem_port);
	xassert(persist_conn->cluster_name);

	if (persist_conn->fd > 0)
		_close_fd(&persist_conn->fd);
	else
		persist_conn->fd = -1;

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
		persist_conn->timeout = slurm_get_msg_timeout() * 1000;

	slurm_set_addr_char(&addr, persist_conn->rem_port,
			    persist_conn->rem_host);
	if ((persist_conn->fd = slurm_open_msg_conn(&addr)) < 0) {
		if (_comm_fail_log(persist_conn)) {
			char *s = xstrdup_printf("%s: failed to open persistent connection to %s:%d: %m",
						 __func__,
						 persist_conn->rem_host,
						 persist_conn->rem_port);
			if (persist_conn->flags & PERSIST_FLAG_SUPPRESS_ERR)
				debug2("%s", s);
			else
				error("%s", s);
			xfree(s);
		}
		return SLURM_ERROR;
	}
	fd_set_nonblocking(persist_conn->fd);
	fd_set_close_on_exec(persist_conn->fd);

	return SLURM_SUCCESS;
}


/* Open a persistent socket connection
 * IN/OUT - persistent connection needing rem_host and rem_port filled in.
 * Returned completely filled in.
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on failure */
extern int slurm_persist_conn_open(slurm_persist_conn_t *persist_conn)
{
	int rc = SLURM_ERROR;
	slurm_msg_t req_msg;
	persist_init_req_msg_t req;
	persist_rc_msg_t *resp = NULL;

	if (slurm_persist_conn_open_without_init(persist_conn) != SLURM_SUCCESS)
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

	memset(&req, 0, sizeof(persist_init_req_msg_t));
	req.cluster_name = persist_conn->cluster_name;
	req.persist_type = persist_conn->persist_type;
	req.port = persist_conn->my_port;
	req.version = SLURM_PROTOCOL_VERSION;

	req_msg.data = &req;

	if (slurm_send_node_msg(persist_conn->fd, &req_msg) < 0) {
		error("%s: failed to send persistent connection init message to %s:%d",
		      __func__, persist_conn->rem_host, persist_conn->rem_port);
		_close_fd(&persist_conn->fd);
	} else {
		Buf buffer = slurm_persist_recv_msg(persist_conn);
		persist_msg_t msg;
		slurm_persist_conn_t persist_conn_tmp;

		if (!buffer) {
			if (_comm_fail_log(persist_conn)) {
				error("%s: No response to persist_init",
				      __func__);
			}
			_close_fd(&persist_conn->fd);
			goto end_it;
		}
		memset(&msg, 0, sizeof(persist_msg_t));
		memcpy(&persist_conn_tmp, persist_conn,
		       sizeof(slurm_persist_conn_t));
		/* The first unpack is done the same way for dbd or normal
		 * communication . */
		persist_conn_tmp.flags &= (~PERSIST_FLAG_DBD);
		rc = slurm_persist_msg_unpack(&persist_conn_tmp, &msg, buffer);
		free_buf(buffer);

		resp = (persist_rc_msg_t *)msg.data;
		if (resp && (rc == SLURM_SUCCESS)) {
			rc = resp->rc;
			persist_conn->version = resp->ret_info;
			persist_conn->flags |= resp->flags;
		}

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
			_close_fd(&persist_conn->fd);
		}
	}

end_it:

	slurm_persist_free_rc_msg(resp);

	return rc;
}

extern void slurm_persist_conn_close(slurm_persist_conn_t *persist_conn)
{
	if (!persist_conn)
		return;

	_close_fd(&persist_conn->fd);
}

extern int slurm_persist_conn_reopen(slurm_persist_conn_t *persist_conn,
				     bool with_init)
{
	slurm_persist_conn_close(persist_conn);

	if (with_init)
		return slurm_persist_conn_open(persist_conn);
	else
		return slurm_persist_conn_open_without_init(persist_conn);
}

/* Close the persistent connection */
extern void slurm_persist_conn_members_destroy(
	slurm_persist_conn_t *persist_conn)
{
	if (!persist_conn)
		return;

	persist_conn->inited = false;
	slurm_persist_conn_close(persist_conn);

	if (persist_conn->auth_cred) {
		g_slurm_auth_destroy(persist_conn->auth_cred);
		persist_conn->auth_cred = NULL;
	}
	xfree(persist_conn->cluster_name);
	xfree(persist_conn->rem_host);
}

/* Close the persistent connection */
extern void slurm_persist_conn_destroy(slurm_persist_conn_t *persist_conn)
{
	if (!persist_conn)
		return;
	slurm_persist_conn_members_destroy(persist_conn);
	xfree(persist_conn);
}

extern int slurm_persist_conn_process_msg(slurm_persist_conn_t *persist_conn,
					  persist_msg_t *persist_msg,
					  char *msg_char, uint32_t msg_size,
					  Buf *out_buffer, bool first)
{
	int rc;
	Buf recv_buffer = NULL;
	char *comment = NULL;

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
		error("CONN:%u %s", persist_conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			persist_conn, rc, comment, persist_msg->msg_type);
		xfree(comment);
	}
	/* 2 versions after 17.02 code refering to DBD_INIT can be removed as it
	   will no longer be suppported.
	*/
	else if (first &&
		 (persist_msg->msg_type != REQUEST_PERSIST_INIT) &&
		 (persist_msg->msg_type != DBD_INIT)) {
		comment = "Initial RPC not REQUEST_PERSIST_INIT";
		error("CONN:%u %s type (%d)",
		      persist_conn->fd, comment, persist_msg->msg_type);
		rc = EINVAL;
		*out_buffer = slurm_persist_make_rc_msg(
			persist_conn, rc, comment,
			REQUEST_PERSIST_INIT);
	} else if (!first &&
		   ((persist_msg->msg_type == REQUEST_PERSIST_INIT) ||
		    (persist_msg->msg_type == DBD_INIT))) {
		comment = "REQUEST_PERSIST_INIT sent after connection established";
		error("CONN:%u %s", persist_conn->fd, comment);
		rc = EINVAL;
		*out_buffer = slurm_persist_make_rc_msg(
			persist_conn, rc, comment, REQUEST_PERSIST_INIT);
	}

	return rc;
}

/* Wait until a file is writeable,
 * RET 1 if file can be written now,
 *     0 if can not be written to within 5 seconds
 *     -1 if file has been closed POLLHUP
 */
extern int slurm_persist_conn_writeable(slurm_persist_conn_t *persist_conn)
{
	struct pollfd ufds;
	int write_timeout = 5000;
	int rc, time_left;
	struct timeval tstart;
	char temp[2];

	xassert(persist_conn->shutdown);

	if (persist_conn->fd < 0)
		return -1;

	ufds.fd     = persist_conn->fd;
	ufds.events = POLLOUT;
	gettimeofday(&tstart, NULL);
	while ((*persist_conn->shutdown) == 0) {
		time_left = write_timeout - _tot_wait(&tstart);
		rc = poll(&ufds, 1, time_left);
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("poll: %m");
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
		    (recv(persist_conn->fd, &temp, 1, 0) == 0)) {
			debug2("persistent connection is closed");
			if (persist_conn->trigger_callbacks.dbd_fail)
				(persist_conn->trigger_callbacks.dbd_fail)();
			return -1;
		}
		if (ufds.revents & POLLNVAL) {
			error("persistent connection is invalid");
			return 0;
		}
		if (ufds.revents & POLLERR) {
			if (_comm_fail_log(persist_conn)) {
				error("persistent connection experienced an error: %m");
			}
			if (persist_conn->trigger_callbacks.dbd_fail)
				(persist_conn->trigger_callbacks.dbd_fail)();
			return 0;
		}
		if ((ufds.revents & POLLOUT) == 0) {
			error("persistent connection %d events %d",
			      persist_conn->fd, ufds.revents);
			return 0;
		}
		/* revents == POLLOUT */
		errno = 0;
		return 1;
	}
	return 0;
}

extern int slurm_persist_send_msg(
	slurm_persist_conn_t *persist_conn, Buf buffer)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_wrote;
	int rc, retry_cnt = 0;

	xassert(persist_conn);

	if (persist_conn->fd < 0)
		return EAGAIN;

	if (!buffer)
		return SLURM_ERROR;

	rc = slurm_persist_conn_writeable(persist_conn);
	if (rc == -1) {
	re_open:
		if (retry_cnt++ > 3)
			return EAGAIN;
		/* if errno is ACCESS_DENIED do not try to reopen to
		   connection just return that */
		if (errno == ESLURM_ACCESS_DENIED)
			return ESLURM_ACCESS_DENIED;

		if (persist_conn->flags & PERSIST_FLAG_RECONNECT) {
			slurm_persist_conn_reopen(persist_conn, true);
			rc = slurm_persist_conn_writeable(persist_conn);
		} else
			return SLURM_ERROR;
	}
	if (rc < 1)
		return EAGAIN;

	msg_size = get_buf_offset(buffer);
	nw_size = htonl(msg_size);
	msg_wrote = write(persist_conn->fd, &nw_size, sizeof(nw_size));
	if (msg_wrote != sizeof(nw_size))
		return EAGAIN;

	msg = get_buf_data(buffer);
	while (msg_size > 0) {
		rc = slurm_persist_conn_writeable(persist_conn);
		if (rc == -1)
			goto re_open;
		if (rc < 1)
			return EAGAIN;
		msg_wrote = write(persist_conn->fd, msg, msg_size);
		if (msg_wrote <= 0)
			return EAGAIN;
		msg += msg_wrote;
		msg_size -= msg_wrote;
	}

	return SLURM_SUCCESS;
}

extern Buf slurm_persist_recv_msg(slurm_persist_conn_t *persist_conn)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_read, offset;
	Buf buffer;

	xassert(persist_conn);

	if (persist_conn->fd < 0)
		return NULL;

	if (!_conn_readable(persist_conn))
		goto endit;

	msg_read = read(persist_conn->fd, &nw_size, sizeof(nw_size));
	if (msg_read != sizeof(nw_size))
		goto endit;
	msg_size = ntohl(nw_size);
	/* We don't error check for an upper limit here
	 * since size could possibly be massive */
	if (msg_size < 2) {
		error("Persistent Conn: Invalid msg_size (%u)", msg_size);
		goto endit;
	}

	msg = xmalloc(msg_size);
	offset = 0;
	while (msg_size > offset) {
		if (!_conn_readable(persist_conn))
			break;		/* problem with this socket */
		msg_read = read(persist_conn->fd, (msg + offset),
				(msg_size - offset));
		if (msg_read <= 0) {
			error("Persistent Conn: read: %m");
			break;
		}
		offset += msg_read;
	}
	if (msg_size != offset) {
		if (!(*persist_conn->shutdown)) {
			error("Persistent Conn: only read %zd of %d bytes",
			      offset, msg_size);
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
	if (!(*persist_conn->shutdown) &&
	    persist_conn->flags & PERSIST_FLAG_RECONNECT)
		slurm_persist_conn_reopen(persist_conn, true);

	return NULL;
}

extern Buf slurm_persist_msg_pack(slurm_persist_conn_t *persist_conn,
				  persist_msg_t *req_msg)
{
	Buf buffer;

	xassert(persist_conn);

	if (persist_conn->flags & PERSIST_FLAG_DBD)
		buffer = pack_slurmdbd_msg((slurmdbd_msg_t *)req_msg,
					   persist_conn->version);
	else {
		slurm_msg_t msg;

		slurm_msg_t_init(&msg);

		msg.data      = req_msg->data;
		msg.data_size = req_msg->data_size;
		msg.msg_type  = req_msg->msg_type;
		msg.protocol_version = persist_conn->version;

		buffer = init_buf(BUF_SIZE);

		pack16(req_msg->msg_type, buffer);
		if (pack_msg(&msg, buffer) != SLURM_SUCCESS) {
			free_buf(buffer);
			return NULL;
                }
	}

	return buffer;
}


extern int slurm_persist_msg_unpack(slurm_persist_conn_t *persist_conn,
				    persist_msg_t *resp_msg, Buf buffer)
{
	int rc;

	xassert(persist_conn);
	xassert(resp_msg);

	if (persist_conn->flags & PERSIST_FLAG_DBD) {
		rc = unpack_slurmdbd_msg((slurmdbd_msg_t *)resp_msg,
					 persist_conn->version,
					 buffer);
	} else {
		slurm_msg_t msg;

		slurm_msg_t_init(&msg);

		msg.protocol_version = persist_conn->version;

		safe_unpack16(&msg.msg_type, buffer);

		rc = unpack_msg(&msg, buffer);

		resp_msg->msg_type = msg.msg_type;
		resp_msg->data = msg.data;
	}

	/* Here we transfer the auth_cred to the persist_conn just in case in the
	 * future we need to use it in some way to verify things for messages
	 * that don't have on that will follow on the connection.
	 */
	if (resp_msg->msg_type == REQUEST_PERSIST_INIT) {
		slurm_msg_t *msg = resp_msg->data;
		if (persist_conn->auth_cred)
			g_slurm_auth_destroy(persist_conn->auth_cred);

		persist_conn->auth_cred = msg->auth_cred;
		msg->auth_cred = NULL;
	}

	return rc;
unpack_error:
	return SLURM_ERROR;
}

extern void slurm_persist_pack_init_req_msg(
	persist_init_req_msg_t *msg, Buf buffer)
{
	/* always send version field first for backwards compatibility */
	pack16(msg->version, buffer);

	if (msg->version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->cluster_name, buffer);
		pack16(msg->persist_type, buffer);
		pack16(msg->port, buffer);
	} else {
		error("%s: invalid protocol version %u",
		      __func__, msg->version);
	}
}

extern int slurm_persist_unpack_init_req_msg(
	persist_init_req_msg_t **msg, Buf buffer)
{
	uint32_t tmp32;

	persist_init_req_msg_t *msg_ptr =
		xmalloc(sizeof(persist_init_req_msg_t));

	*msg = msg_ptr;

	safe_unpack16(&msg_ptr->version, buffer);

	if (msg_ptr->version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &tmp32, buffer);
		safe_unpack16(&msg_ptr->persist_type, buffer);
		safe_unpack16(&msg_ptr->port, buffer);
	} else {
		error("%s: invalid protocol_version %u",
		      __func__, msg_ptr->version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_persist_free_init_req_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void slurm_persist_free_init_req_msg(persist_init_req_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		xfree(msg);
	}
}

extern void slurm_persist_pack_rc_msg(
	persist_rc_msg_t *msg, Buf buffer, uint16_t protocol_version)
{
	if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
		packstr(msg->comment, buffer);
		pack16(msg->flags, buffer);
		pack32(msg->rc, buffer);
		pack16(msg->ret_info, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(msg->comment, buffer);
		pack32(msg->rc, buffer);
		pack16(msg->ret_info, buffer);
	} else {
		error("%s: invalid protocol version %u",
		      __func__, protocol_version);
	}
}

extern int slurm_persist_unpack_rc_msg(
	persist_rc_msg_t **msg, Buf buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	persist_rc_msg_t *msg_ptr = xmalloc(sizeof(persist_rc_msg_t));

	*msg = msg_ptr;

	if (protocol_version >= SLURM_18_08_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->comment, &uint32_tmp, buffer);
		safe_unpack16(&msg_ptr->flags, buffer);
		safe_unpack32(&msg_ptr->rc, buffer);
		safe_unpack16(&msg_ptr->ret_info, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpackstr_xmalloc(&msg_ptr->comment, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->rc, buffer);
		safe_unpack16(&msg_ptr->ret_info, buffer);
	} else {
		error("%s: invalid protocol_version %u",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_persist_free_rc_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void slurm_persist_free_rc_msg(persist_rc_msg_t *msg)
{
	if (msg) {
		xfree(msg->comment);
		xfree(msg);
	}
}

extern Buf slurm_persist_make_rc_msg(slurm_persist_conn_t *persist_conn,
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

extern Buf slurm_persist_make_rc_msg_flags(slurm_persist_conn_t *persist_conn,
					   uint32_t rc, char *comment,
					   uint16_t flags,
					   uint16_t ret_info)
{
	persist_rc_msg_t msg;
	persist_msg_t resp;

	memset(&msg, 0, sizeof(persist_rc_msg_t));
	memset(&resp, 0, sizeof(persist_msg_t));

	msg.rc = rc;
	msg.flags = flags;
	msg.comment = comment;
	msg.ret_info = ret_info;

	resp.msg_type = PERSIST_RC;
	resp.data = &msg;

	return slurm_persist_msg_pack(persist_conn, &resp);
}
