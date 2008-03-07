/****************************************************************************\
 *  slurmdbd_defs.c - functions for use with Slurm DBD RPCs
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H 
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/jobacct_common.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#define DBD_MAGIC	0xDEAD3219
#define MAX_AGENT_QUEUE	10000
#define MAX_DBD_MSG_LEN 16384

static pthread_mutex_t agent_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cond = PTHREAD_COND_INITIALIZER;
static List      agent_list     = (List) NULL;
static pthread_t agent_tid      = 0;
static time_t    agent_shutdown = 0;

static pthread_mutex_t slurmdbd_lock = PTHREAD_MUTEX_INITIALIZER;
static slurm_fd  slurmdbd_fd         = -1;
static char *    slurmdbd_auth_info  = NULL;

static void * _agent(void *x);
static void   _agent_queue_del(void *x);
static void   _close_slurmdbd_fd(void);
static void   _create_agent(void);
static bool   _fd_readable(slurm_fd fd);
static int    _fd_writeable(slurm_fd fd);
static int    _get_return_code(void);
static Buf    _load_dbd_rec(int fd);
static void   _load_dbd_state(void);
static void   _open_slurmdbd_fd(void);
static int    _purge_job_start_req(void);
static Buf    _recv_msg(void);
static void   _reopen_slurmdbd_fd(void);
static int    _save_dbd_rec(int fd, Buf buffer);
static void   _save_dbd_state(void);
static int    _send_init_msg(void);
static int    _send_msg(Buf buffer);
static void   _sig_handler(int signal);
static void   _shutdown_agent(void);
static int    _tot_wait (struct timeval *start_time);

/****************************************************************************
 * Socket open/close/read/write functions
 ****************************************************************************/

/* Open a socket connection to SlurmDbd */
extern int slurm_open_slurmdbd_conn(char *auth_info)
{
	slurm_mutex_lock(&agent_lock);
	if ((agent_tid == 0) || (agent_list == NULL))
		_create_agent();
	slurm_mutex_unlock(&agent_lock);

	slurm_mutex_lock(&slurmdbd_lock);
	xfree(slurmdbd_auth_info);
	if (auth_info)
		slurmdbd_auth_info = xstrdup(auth_info);
	if (slurmdbd_fd < 0)
		_open_slurmdbd_fd();
	slurm_mutex_unlock(&slurmdbd_lock);

	return SLURM_SUCCESS;
}

/* Close the SlurmDBD socket connection */
extern int slurm_close_slurmdbd_conn(void)
{
	/* NOTE: agent_lock not needed for _shutdown_agent() */
	_shutdown_agent();

	slurm_mutex_lock(&slurmdbd_lock);
	_close_slurmdbd_fd();
	xfree(slurmdbd_auth_info);
	slurm_mutex_unlock(&slurmdbd_lock);

	return SLURM_SUCCESS;
}

/* Send an RPC to the SlurmDBD and wait for the return code reply.
 * The RPC will not be queued if an error occurs.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_slurmdbd_recv_rc_msg(slurmdbd_msg_t *req, int *resp_code)
{
	int rc;
	slurmdbd_msg_t *resp;

	xassert(req);
	xassert(resp_code);

	resp = xmalloc(sizeof(slurmdbd_msg_t));
	rc = slurm_send_recv_slurmdbd_msg(req, resp);
	if (rc != SLURM_SUCCESS)
		;	/* error message already sent */
	else if (resp->msg_type != DBD_RC) {
		error("slurmdbd: response is type DBD_RC: %d", resp->msg_type);
		rc = SLURM_ERROR;
	} else {	/* resp->msg_type == DBD_RC */
		dbd_rc_msg_t *msg = resp->data;
		*resp_code = msg->return_code;
		slurm_dbd_free_rc_msg(msg);
	}
	xfree(resp);

	return rc;
}

/* Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_recv_slurmdbd_msg(slurmdbd_msg_t *req, 
					slurmdbd_msg_t *resp)
{
	int rc;
	Buf buffer;

	xassert(req);
	xassert(resp);

	slurm_mutex_lock(&slurmdbd_lock);
	if (slurmdbd_fd < 0) {
		/* Either slurm_open_slurmdbd_conn() was not executed or
		 * the connection to Slurm DBD has been closed */
		_open_slurmdbd_fd();
		if (slurmdbd_fd < 0) {
			slurm_mutex_unlock(&slurmdbd_lock);
			return SLURM_ERROR;
		}
	}

	buffer = init_buf(MAX_DBD_MSG_LEN);
	pack16(req->msg_type, buffer);
	switch (req->msg_type) {
		case DBD_CLUSTER_PROCS:
			slurm_dbd_pack_cluster_procs_msg(
				(dbd_cluster_procs_msg_t *) req->data, buffer);
			break;
		case DBD_GET_JOBS:
			slurm_dbd_pack_get_jobs_msg(
				(dbd_get_jobs_msg_t *) req->data, buffer);
			break;
		case DBD_INIT:
			slurm_dbd_pack_init_msg(
				(dbd_init_msg_t *) req->data, buffer, 
				slurmdbd_auth_info);
			break;
		case DBD_JOB_COMPLETE:
			slurm_dbd_pack_job_complete_msg(
				(dbd_job_comp_msg_t *) req->data, buffer);
			break;
		case DBD_JOB_START:
			slurm_dbd_pack_job_start_msg(
				(dbd_job_start_msg_t *) req->data, buffer);
			break;
		case DBD_JOB_SUSPEND:
			slurm_dbd_pack_job_suspend_msg(
				(dbd_job_suspend_msg_t *) req->data, buffer);
			break;
		case DBD_NODE_STATE:
			slurm_dbd_pack_node_state_msg(
				(dbd_node_state_msg_t *) req->data, buffer);
			break;
		case DBD_STEP_COMPLETE:
			slurm_dbd_pack_step_complete_msg(
				(dbd_step_comp_msg_t *) req->data, buffer);
			break;
		case DBD_STEP_START:
			slurm_dbd_pack_step_start_msg(
				(dbd_step_start_msg_t *) req->data, buffer);
			break;
		default:
			error("slurmdbd: Invalid message type %u",
			      req->msg_type);
			free_buf(buffer);
			slurm_mutex_unlock(&slurmdbd_lock);
			return SLURM_ERROR;
	}

	rc = _send_msg(buffer);
	free_buf(buffer);
	if (rc != SLURM_SUCCESS) {
		error("slurmdbd: Sending message type %u", req->msg_type);
		slurm_mutex_unlock(&slurmdbd_lock);
		return SLURM_ERROR;
	}

	buffer = _recv_msg();
	if (buffer == NULL) {
		error("slurmdbd: Getting response to message type %u", 
		      req->msg_type);
		slurm_mutex_unlock(&slurmdbd_lock);
		return SLURM_ERROR;
	}


	safe_unpack16(&resp->msg_type, buffer);
	switch (resp->msg_type) {
		case DBD_RC:
			if (slurm_dbd_unpack_rc_msg(
						(dbd_rc_msg_t **)
						&resp->data, buffer))
				rc = SLURM_ERROR;
			else
				rc = SLURM_SUCCESS;
			break;
		case DBD_GOT_JOBS:
			if (slurm_dbd_unpack_got_jobs_msg(
						(dbd_got_jobs_msg_t **)
						&resp->data, buffer))
				rc = SLURM_ERROR;
			else
				rc = SLURM_SUCCESS;
			break;
		case DBD_JOB_START_RC:
			if (slurm_dbd_unpack_job_start_rc_msg(
						(dbd_job_start_rc_msg_t **)
						&resp->data, buffer))
				rc = SLURM_ERROR;
			else
				rc = SLURM_SUCCESS;
			break;
		default:
			error("slurmdbd: bad message type %d", resp->msg_type);
			rc = SLURM_ERROR;
	}

	free_buf(buffer);
	slurm_mutex_unlock(&slurmdbd_lock);
	return rc;

 unpack_error:
	free_buf(buffer);
	slurm_mutex_unlock(&slurmdbd_lock);
	return SLURM_ERROR;
}

/* Send an RPC to the SlurmDBD. Do not wait for the reply. The RPC
 * will be queued and processed later if the SlurmDBD is not responding.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_slurmdbd_msg(slurmdbd_msg_t *req)
{
	Buf buffer;
	int cnt, rc = SLURM_SUCCESS;
	static time_t syslog_time = 0;

	buffer = init_buf(MAX_DBD_MSG_LEN);
	pack16(req->msg_type, buffer);
	switch (req->msg_type) {
		case DBD_CLUSTER_PROCS:
			slurm_dbd_pack_cluster_procs_msg(
				(dbd_cluster_procs_msg_t *) req->data, buffer);
			break;
		case DBD_JOB_COMPLETE:
			slurm_dbd_pack_job_complete_msg(
				(dbd_job_comp_msg_t *) req->data, buffer);
			break;
		case DBD_JOB_START:
			slurm_dbd_pack_job_start_msg(
				(dbd_job_start_msg_t *) req->data, buffer);
			break;
		case DBD_JOB_SUSPEND:
			slurm_dbd_pack_job_suspend_msg(
				(dbd_job_suspend_msg_t *) req->data, buffer);
			break;
		case DBD_NODE_STATE:
			slurm_dbd_pack_node_state_msg(
				(dbd_node_state_msg_t *) req->data, buffer);
			break;
		case DBD_STEP_COMPLETE:
			slurm_dbd_pack_step_complete_msg(
				(dbd_step_comp_msg_t *) req->data, buffer);
			break;
		case DBD_STEP_START:
			slurm_dbd_pack_step_start_msg(
				(dbd_step_start_msg_t *) req->data, buffer);
			break;
		default:
			error("slurmdbd: Invalid send message type %u",
			      req->msg_type);
			free_buf(buffer);
			return SLURM_ERROR;
	}

	slurm_mutex_lock(&agent_lock);
	if ((agent_tid == 0) || (agent_list == NULL)) {
		_create_agent();
		if ((agent_tid == 0) || (agent_list == NULL)) {
			slurm_mutex_unlock(&agent_lock);
			free_buf(buffer);
			return SLURM_ERROR;
		}
	}
	cnt = list_count(agent_list);
	if ((cnt >= (MAX_AGENT_QUEUE / 2)) &&
	    (difftime(time(NULL), syslog_time) > 120)) {
		/* Record critical error every 120 seconds */
		syslog_time = time(NULL);
		error("slurmdbd: agent queue filling, RESTART SLURM DBD NOW");
		syslog(LOG_CRIT, "*** RESTART SLURM DBD NOW ***");
	}
	if (cnt == (MAX_AGENT_QUEUE - 1))
		cnt -= _purge_job_start_req();
	if (cnt < MAX_AGENT_QUEUE) {
		if (list_enqueue(agent_list, buffer) == NULL)
			fatal("list_enqueue: memory allocation failure");
	} else {
		error("slurmdbd: agent queue is full, discarding request");
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&agent_lock);
	pthread_cond_broadcast(&agent_cond);
	return rc;
}

/* Open a connection to the Slurm DBD and set slurmdbd_fd */
static void _open_slurmdbd_fd(void)
{
	slurm_addr dbd_addr;
	uint16_t slurmdbd_port;
	char *   slurmdbd_host;

	if (slurmdbd_fd >= 0) {
		debug("Attempt to re-open slurmdbd socket");
		return;
	}

	slurmdbd_host = slurm_get_accounting_storage_host();
	slurmdbd_port = slurm_get_accounting_storage_port();
	if ((slurmdbd_host == NULL) || (slurmdbd_port == 0)) {
		error("Invalid SlurmDbd address %s:%u",
			slurmdbd_host, slurmdbd_port);
		xfree(slurmdbd_host);
		return;
	}

	slurm_set_addr(&dbd_addr, slurmdbd_port, slurmdbd_host);
	if (dbd_addr.sin_port == 0)
		error("Unable to locate SlurmDBD host %s:%u", 
		      slurmdbd_host, slurmdbd_port);
	else {
		slurmdbd_fd = slurm_open_msg_conn(&dbd_addr);
		if (slurmdbd_fd < 0)
			error("slurmdbd: slurm_open_msg_conn: %m");
		else {
			fd_set_nonblocking(slurmdbd_fd);
			if (_send_init_msg() != SLURM_SUCCESS)
				error("slurmdbd: Sending DdbInit msg: %m");
			else
				debug("slurmdbd: Sent DbdInit msg");
		}
	}
	xfree(slurmdbd_host);
}

static int _send_init_msg(void)
{
	int rc;
	Buf buffer;
	dbd_init_msg_t req;

	buffer = init_buf(1024);
	pack16((uint16_t) DBD_INIT, buffer);
	req.version  = SLURM_DBD_VERSION;
	slurm_dbd_pack_init_msg(&req, buffer, slurmdbd_auth_info);

	rc = _send_msg(buffer);
	free_buf(buffer);
	if (rc != SLURM_SUCCESS) {
		error("slurmdbd: Sending DBD_INIT message");
		return rc;
	}

	rc = _get_return_code();
	return rc;
}

/* Close the SlurmDbd connection */
static void _close_slurmdbd_fd(void)
{
	if (slurmdbd_fd >= 0) {
		close(slurmdbd_fd);
		slurmdbd_fd = -1;
	}
}

/* Reopen the Slurm DBD connection due to some error */
static void _reopen_slurmdbd_fd(void)
{
	info("slurmdbd: reopening connection");
	_close_slurmdbd_fd();
	_open_slurmdbd_fd();
}

static int _send_msg(Buf buffer)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_wrote;
	int rc;

	if (slurmdbd_fd < 0)
		return SLURM_ERROR;

	rc =_fd_writeable(slurmdbd_fd);
	if (rc == -1) {
re_open:	/* SlurmDBD shutdown, try to reopen a connection now */
		_reopen_slurmdbd_fd();
		rc = _fd_writeable(slurmdbd_fd);
	}
	if (rc < 1)
		return SLURM_ERROR;

	msg_size = get_buf_offset(buffer);
	nw_size = htonl(msg_size);
	msg_wrote = write(slurmdbd_fd, &nw_size, sizeof(nw_size));
	if (msg_wrote != sizeof(nw_size))
		return SLURM_ERROR;

	msg = get_buf_data(buffer);
	while (msg_size > 0) {
		rc = _fd_writeable(slurmdbd_fd);
		if (rc == -1)
			goto re_open;
		if (rc < 1)
			return SLURM_ERROR;
		msg_wrote = write(slurmdbd_fd, msg, msg_size);
		if (msg_wrote <= 0)
			return SLURM_ERROR;
		msg += msg_wrote;
		msg_size -= msg_wrote;
	}

	return SLURM_SUCCESS;
}

static int _get_return_code(void)
{
	Buf buffer;
	uint16_t msg_type;
	dbd_rc_msg_t *msg;
	dbd_job_start_rc_msg_t *js_msg;
	int rc = SLURM_ERROR;

	buffer = _recv_msg();
	if (buffer == NULL)
		return rc;

	safe_unpack16(&msg_type, buffer);
	switch(msg_type) {
	case DBD_JOB_START_RC:
		if (slurm_dbd_unpack_job_start_rc_msg(&js_msg, buffer)
		    == SLURM_SUCCESS) {
			rc = js_msg->return_code;
			slurm_dbd_free_job_start_rc_msg(js_msg);
			if (rc != SLURM_SUCCESS)
				error("slurmdbd: DBD_JOB_START_RC is %d", rc);
		} else
			error("slurmdbd: unpack message error");
		break;
	case DBD_RC:
		if (slurm_dbd_unpack_rc_msg(&msg, buffer) == SLURM_SUCCESS) {
			rc = msg->return_code;
			slurm_dbd_free_rc_msg(msg);
			if (rc != SLURM_SUCCESS)
				error("slurmdbd: DBD_RC is %d", rc);
		} else
			error("slurmdbd: unpack message error");
		break;
	default:
		error("slurmdbd: bad message type %d != DBD_RC", msg_type);
	}

 unpack_error:
	free_buf(buffer);
	return rc;
}

static Buf _recv_msg(void)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_read, offset;
	Buf buffer;

	if (slurmdbd_fd < 0)
		return NULL;

	if (!_fd_readable(slurmdbd_fd))
		return NULL;
	msg_read = read(slurmdbd_fd, &nw_size, sizeof(nw_size));
	if (msg_read != sizeof(nw_size))
		return NULL;
	msg_size = ntohl(nw_size);
	if ((msg_size < 2) || (msg_size > 1000000)) {
		error("slurmdbd: Invalid msg_size (%u)");
		return NULL;
	}

	msg = xmalloc(msg_size);
	offset = 0;
	while (msg_size > offset) {
		if (!_fd_readable(slurmdbd_fd))
			break;		/* problem with this socket */
		msg_read = read(slurmdbd_fd, (msg + offset), 
				(msg_size - offset));
		if (msg_read <= 0) {
			error("slurmdbd: read: %m");
			break;
		}
		offset += msg_read;
	}
	if (msg_size != offset) {
		if (agent_shutdown == 0) {
			error("slurmdbd: only read %d of %d bytes", 
			      offset, msg_size);
		}	/* else in shutdown mode */
		xfree(msg);
		return NULL;
	}

	buffer = create_buf(msg, msg_size);
	if (buffer == NULL)
		fatal("create_buf: malloc failure");
	return buffer;
}

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

/* Wait until a file is readable, 
 * RET false if can not be read */
static bool _fd_readable(slurm_fd fd)
{
	struct pollfd ufds;
	static int msg_timeout = -1;
	int rc, time_left;
	struct timeval tstart;

	if (msg_timeout == -1)
		msg_timeout = slurm_get_msg_timeout() * 1000;

	ufds.fd     = fd;
	ufds.events = POLLIN;
	gettimeofday(&tstart, NULL);
	while (agent_shutdown == 0) {
		time_left = msg_timeout - _tot_wait(&tstart);
		rc = poll(&ufds, 1, time_left);
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("poll: %m");
			return false;
		}
		if (rc == 0)
			return false;
		if (ufds.revents & POLLHUP) {
			debug2("SlurmDBD connection closed");
			return false;
		}
		if (ufds.revents & POLLNVAL) {
			error("SlurmDBD connection is invalid");
			return false;
		}
		if (ufds.revents & POLLERR) {
			error("SlurmDBD connection experienced an error");
			return false;
		}
		if ((ufds.revents & POLLIN) == 0) {
			error("SlurmDBD connection %d events %d", 
				fd, ufds.revents);
			return false;
		}
		/* revents == POLLIN */
		return true;
	}
	return false;
}

/* Wait until a file is writable, 
 * RET 1 if file can be written now,
 *     0 if can not be written to within 5 seconds
 *     -1 if file has been closed POLLHUP
 */
static int _fd_writeable(slurm_fd fd)
{
	struct pollfd ufds;
	int msg_timeout = 5000;
	int rc, time_left;
	struct timeval tstart;

	ufds.fd     = fd;
	ufds.events = POLLOUT;
	gettimeofday(&tstart, NULL);
	while (agent_shutdown == 0) {
		time_left = msg_timeout - _tot_wait(&tstart);
		rc = poll(&ufds, 1, time_left);
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("poll: %m");
			return -1;
		}
		if (rc == 0)
			return 0;
		if (ufds.revents & POLLHUP) {
			debug2("SlurmDBD connection is closed");
			return -1;
		}
		if (ufds.revents & POLLNVAL) {
			error("SlurmDBD connection is invalid");
			return 0;
		}
		if (ufds.revents & POLLERR) {
			error("SlurmDBD connection experienced an error: %m");
			return 0;
		}
		if ((ufds.revents & POLLOUT) == 0) {
			error("SlurmDBD connection %d events %d", 
				fd, ufds.revents);
			return 0;
		}
		/* revents == POLLOUT */
		return 1;
	}
	return 0;
}

/****************************************************************************
 * Functions for agent to manage queue of pending message for the Slurm DBD
 ****************************************************************************/
static void _create_agent(void)
{
	if (agent_list == NULL) {
		agent_list = list_create(_agent_queue_del);
		if (agent_list == NULL)
			fatal("list_create: malloc failure");
		_load_dbd_state();
	}

	if (agent_tid == 0) {
		pthread_attr_t agent_attr;
		slurm_attr_init(&agent_attr);
		pthread_attr_setdetachstate(&agent_attr, 
					    PTHREAD_CREATE_DETACHED);
		if (pthread_create(&agent_tid, &agent_attr, _agent, NULL) ||
		    (agent_tid == 0))
			fatal("pthread_create: %m");
	}
}

static void _agent_queue_del(void *x)
{
	Buf buffer = (Buf) x;
	free_buf(buffer);
}

static void _shutdown_agent(void)
{
	int i;

	if (agent_tid) {
		agent_shutdown = time(NULL);
		pthread_cond_broadcast(&agent_cond);
		for (i=0; ((i<10) && agent_tid); i++) {
			usleep(10000);
			pthread_cond_broadcast(&agent_cond);
			if (pthread_kill(agent_tid, SIGUSR1))
				agent_tid = 0;
		}
		if (agent_tid) {
			error("slurmdbd: agent failed to shutdown gracefully");
		} else
			agent_shutdown = 0;
	}
}

static void *_agent(void *x)
{
	int cnt, rc;
	Buf buffer;
	struct timespec abs_time;
	static time_t fail_time = 0;
	int sigarray[] = {SIGUSR1, 0};

	/* Prepare to catch SIGUSR1 to interrupt pending
	 * I/O and terminate in a timely fashion. */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);

	while (agent_shutdown == 0) {

		slurm_mutex_lock(&slurmdbd_lock);
		if ((slurmdbd_fd < 0) && 
		    (difftime(time(NULL), fail_time) >= 10)) {
			/* The connection to Slurm DBD is not open */
			_open_slurmdbd_fd();
			if (slurmdbd_fd < 0)
				fail_time = time(NULL);
		}

		slurm_mutex_lock(&agent_lock);
		if (agent_list && slurmdbd_fd)
			cnt = list_count(agent_list);
		else
			cnt = 0;
		if ((cnt == 0) || (slurmdbd_fd < 0) ||
		    (fail_time && (difftime(time(NULL), fail_time) < 10))) {
			slurm_mutex_unlock(&slurmdbd_lock);
			abs_time.tv_sec  = time(NULL) + 10;
			abs_time.tv_nsec = 0;
			rc = pthread_cond_timedwait(&agent_cond, &agent_lock,
						    &abs_time);
			slurm_mutex_unlock(&agent_lock);
			continue;
		} else if ((cnt > 0) && ((cnt % 50) == 0))
			info("slurmdbd: agent queue size %u", cnt);
		/* Leave item on the queue until processing complete */
		if (agent_list)
			buffer = (Buf) list_peek(agent_list);
		else
			buffer = NULL;
		slurm_mutex_unlock(&agent_lock);
		if (buffer == NULL) {
			slurm_mutex_unlock(&slurmdbd_lock);
			continue;
		}

		/* NOTE: agent_lock is clear here, so we can add more
		 * requests to the queue while waiting for this RPC to 
		 * complete. */
		rc = _send_msg(buffer);
		if (rc != SLURM_SUCCESS) {
			if (agent_shutdown)
				break;
			error("slurmdbd: Failure sending message");
		} else {
			rc = _get_return_code();
			if (rc != SLURM_SUCCESS) {
				if (agent_shutdown)
					break;
				error("slurmdbd: Failure getting response");
			}
		}
		slurm_mutex_unlock(&slurmdbd_lock);

		slurm_mutex_lock(&agent_lock);
		if (agent_list && (rc == SLURM_SUCCESS)) {
			buffer = (Buf) list_dequeue(agent_list);
			free_buf(buffer);
			fail_time = 0;
		} else {
			fail_time = time(NULL);
		}
		slurm_mutex_unlock(&agent_lock);
	}

	slurm_mutex_lock(&agent_lock);
	_save_dbd_state();
	if (agent_list) {
		list_destroy(agent_list);
		agent_list = NULL;
	}
	slurm_mutex_unlock(&agent_lock);
	return NULL;
}

static void _save_dbd_state(void)
{
	char *dbd_fname;
	Buf buffer;
	int fd, rc, wrote = 0;

	dbd_fname = slurm_get_state_save_location();
	xstrcat(dbd_fname, "/dbd.messages");
	fd = open(dbd_fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error("slurmdbd: Creating state save file %s", dbd_fname);
	} else if (agent_list) {
		while ((buffer = list_dequeue(agent_list))) {
			rc = _save_dbd_rec(fd, buffer);
			free_buf(buffer);
			if (rc != SLURM_SUCCESS)
				break;
			wrote++;
		}
	}
	if (fd >= 0) {
		verbose("slurmdbd: saved %d pending RPCs", wrote);
		(void) close(fd);
	}
	xfree(dbd_fname);
}

static void _load_dbd_state(void)
{
	char *dbd_fname;
	Buf buffer;
	int fd, recovered = 0;

	dbd_fname = slurm_get_state_save_location();
	xstrcat(dbd_fname, "/dbd.messages");
	fd = open(dbd_fname, O_RDONLY);
	if (fd < 0) {
		error("slurmdbd: Opening state save file %s", dbd_fname);
	} else {
		while (1) {
			buffer = _load_dbd_rec(fd);
			if (buffer == NULL)
				break;
			if (list_enqueue(agent_list, buffer) == NULL)
				fatal("slurmdbd: list_enqueue, no memory");
			recovered++;
		}
	}
	if (fd >= 0) {
		verbose("slurmdbd: recovered %d pending RPCs", recovered);
		(void) close(fd);
		(void) unlink(dbd_fname);	/* clear save state */
	}
	xfree(dbd_fname);
}

static int _save_dbd_rec(int fd, Buf buffer)
{
	ssize_t size, wrote;
	uint32_t msg_size = get_buf_offset(buffer);
	uint32_t magic = DBD_MAGIC;
	char *msg = get_buf_data(buffer);

	size = sizeof(msg_size);
	wrote = write(fd, &msg_size, size);
	if (wrote != size) {
		error("slurmdbd: state save error: %m");
		return SLURM_ERROR;
	}

	wrote = 0;
	while (wrote < msg_size) {
		wrote = write(fd, msg, msg_size);
		if (wrote > 0) {
			msg += wrote;
			msg_size -= wrote;
		} else if ((wrote == -1) && (errno == EINTR))
			continue;
		else {
			error("slurmdbd: state save error: %m");
			return SLURM_ERROR;
		}
	}	

	size = sizeof(magic);
	wrote = write(fd, &magic, size);
	if (wrote != size) {
		error("slurmdbd: state save error: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static Buf _load_dbd_rec(int fd)
{
	ssize_t size, rd_size;
	uint32_t msg_size, magic;
	char *msg;
	Buf buffer;

	size = sizeof(msg_size);
	rd_size = read(fd, &msg_size, size);
	if (rd_size == 0)
		return (Buf) NULL;
	if (rd_size != size) {
		error("slurmdbd: state recover error: %m");
		return (Buf) NULL;
	}
	if (msg_size > MAX_DBD_MSG_LEN) {
		error("slurmdbd: state recover error, msg_size=%u", msg_size);
		return (Buf) NULL;
	}

	buffer = init_buf((int) msg_size);
	if (buffer == NULL)
		fatal("slurmdbd: create_buf malloc failure");
	set_buf_offset(buffer, msg_size);
	msg = get_buf_data(buffer);
	size = msg_size;
	while (size) {
		rd_size = read(fd, msg, size);
		if (rd_size > 0) {
			msg += rd_size;
			size -= rd_size;
		} else if ((rd_size == -1) && (errno == EINTR))
			continue;
		else {
			error("slurmdbd: state recover error: %m");
			free_buf(buffer);
			return (Buf) NULL;
		}
	}

	size = sizeof(magic);
	rd_size = read(fd, &magic, size);
	if ((rd_size != size) || (magic != DBD_MAGIC)) {
		error("slurmdbd: state recover error");
		free_buf(buffer);
		return (Buf) NULL;
	}

	return buffer;
}

static void _sig_handler(int signal)
{
}

/* Purge queued job/step start records from the agent queue
 * RET number of records purged */
static int _purge_job_start_req(void)
{
	int purged = 0;
	ListIterator iter;
	uint16_t msg_type;
	uint32_t offset;
	Buf buffer;

	iter = list_iterator_create(agent_list);
	while ((buffer = list_next(iter))) {
		offset = get_buf_offset(buffer);
		if (offset < 2)
			continue;
		set_buf_offset(buffer, 0);
		unpack16(&msg_type, buffer);
		set_buf_offset(buffer, offset);
		if ((msg_type == DBD_JOB_START) ||
		    (msg_type == DBD_STEP_START)) {
			list_remove(iter);
			purged++;
		}
	}
	list_iterator_destroy(iter);
	info("slurmdbd: purge %d job/step start records", purged);
	return purged;
}

/****************************************************************************\
 * Free data structures
\****************************************************************************/
void inline slurm_dbd_free_cluster_procs_msg(dbd_cluster_procs_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		xfree(msg);
	}
}

void inline slurm_dbd_free_get_jobs_msg(dbd_get_jobs_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		if(msg->selected_steps)
			list_destroy(msg->selected_steps);
		if(msg->selected_parts)
			list_destroy(msg->selected_parts);
		xfree(msg);
	}
}

void inline slurm_dbd_free_got_jobs_msg(dbd_got_jobs_msg_t *msg)
{
	if (msg) {
		if(msg->jobs)
			list_destroy(msg->jobs);
		xfree(msg);
	}
}

void inline slurm_dbd_free_init_msg(dbd_init_msg_t *msg)
{
	xfree(msg);
}

void inline slurm_dbd_free_job_complete_msg(dbd_job_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg);
	}
}

void inline slurm_dbd_free_job_start_msg(dbd_job_start_msg_t *msg)
{
	if (msg) {
		xfree(msg->block_id);
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg);
	}
}

void inline slurm_dbd_free_job_start_rc_msg(dbd_job_start_rc_msg_t *msg)
{
	xfree(msg);
}

void inline slurm_dbd_free_job_suspend_msg(dbd_job_suspend_msg_t *msg)
{
	xfree(msg);
}

void inline slurm_dbd_free_rc_msg(dbd_rc_msg_t *msg)
{
	xfree(msg);
}

void inline slurm_dbd_free_node_state_msg(dbd_node_state_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		xfree(msg->hostlist);
		xfree(msg->reason);
		xfree(msg);
	}
}

void inline slurm_dbd_free_step_complete_msg(dbd_step_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg);
	}
}

void inline slurm_dbd_free_step_start_msg(dbd_step_start_msg_t *msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg);
	}
}

/****************************************************************************\
 * Pack and unpack data structures
\****************************************************************************/
void inline
slurm_dbd_pack_cluster_procs_msg(dbd_cluster_procs_msg_t *msg, Buf buffer)
{
	packstr(msg->cluster_name, buffer);
	pack32(msg->proc_count,    buffer);
	pack_time(msg->event_time, buffer);
}
int inline
slurm_dbd_unpack_cluster_procs_msg(dbd_cluster_procs_msg_t **msg, Buf buffer)
{
	dbd_cluster_procs_msg_t *msg_ptr;
	uint32_t uint32_tmp;

	msg_ptr = xmalloc(sizeof(dbd_cluster_procs_msg_t));
	*msg = msg_ptr;
	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->proc_count, buffer);
	safe_unpack_time(&msg_ptr->event_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->cluster_name);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline slurm_dbd_pack_get_jobs_msg(dbd_get_jobs_msg_t *msg, Buf buffer)
{
	uint32_t i = 0;
	ListIterator itr = NULL;
	jobacct_selected_step_t *job = NULL;
	char *part = NULL;

	packstr(msg->cluster_name, buffer);

	if(msg->selected_steps) 
		i = list_count(msg->selected_steps);
			
	pack32(i, buffer);
	if(i) {
		itr = list_iterator_create(msg->selected_steps);
		while((job = list_next(itr))) {
			pack_jobacct_selected_step(job, buffer);
		}
		list_iterator_destroy(itr);
	}

	i = 0;
	if(msg->selected_parts) 
		i = list_count(msg->selected_parts);
			
	pack32(i, buffer);
	if(i) {
		itr = list_iterator_create(msg->selected_parts);
		while((part = list_next(itr))) {
			packstr(part, buffer);
		}
		list_iterator_destroy(itr);
	}
}

int inline slurm_dbd_unpack_get_jobs_msg(dbd_get_jobs_msg_t **msg, Buf buffer)
{
	int i;
	uint32_t count = 0;
	uint32_t uint32_tmp;
	dbd_get_jobs_msg_t *msg_ptr;
	jobacct_selected_step_t *job = NULL;
	char *part = NULL;

	msg_ptr = xmalloc(sizeof(dbd_get_jobs_msg_t));
	*msg = msg_ptr;

	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &uint32_tmp, buffer);

	safe_unpack32(&count, buffer);
	msg_ptr->selected_steps = list_create(destroy_jobacct_selected_step);
	for(i=0; i<count; i++) {
		unpack_jobacct_selected_step(&job, buffer);
		list_append(msg_ptr->selected_steps, job);
	}

	safe_unpack32(&count, buffer);
	msg_ptr->selected_parts = list_create(slurm_destroy_char);
	for(i=0; i<count; i++) {
		safe_unpackstr_xmalloc(&part, &uint32_tmp, buffer);
		list_append(msg_ptr->selected_parts, part);
	}

	return SLURM_SUCCESS;

unpack_error:
	slurm_dbd_free_get_jobs_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline slurm_dbd_pack_got_jobs_msg(dbd_got_jobs_msg_t *msg, Buf buffer)
{
	uint32_t count = 0;
	ListIterator itr = NULL;
	jobacct_job_rec_t *job = NULL;

	if(msg->jobs) 
		count = list_count(msg->jobs);
			
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(msg->jobs);
		while((job = list_next(itr))) {
			pack_jobacct_job_rec(job, buffer);
		}
		list_iterator_destroy(itr);
	}
}

int inline slurm_dbd_unpack_got_jobs_msg(dbd_got_jobs_msg_t **msg, Buf buffer)
{
	int i;
	uint32_t count;
	dbd_got_jobs_msg_t *msg_ptr = NULL;
	jobacct_job_rec_t *job = NULL;

	msg_ptr = xmalloc(sizeof(dbd_got_jobs_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&count, buffer);
	msg_ptr->jobs = list_create(destroy_jobacct_job_rec);
	for(i=0; i<count; i++) {
		unpack_jobacct_job_rec(&job, buffer);
		list_append(msg_ptr->jobs, job);
	}
	
	return SLURM_SUCCESS;

unpack_error:
	if(msg_ptr->jobs)
		list_destroy(msg_ptr->jobs);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_init_msg(dbd_init_msg_t *msg, Buf buffer, char *auth_info)
{
	int rc;
	void *auth_cred;

	pack16(msg->version, buffer);
	auth_cred = g_slurm_auth_create(NULL, 2, auth_info);
	if (auth_cred == NULL) {
		error("Creating authentication credential: %s",
			 g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
	} else {
		rc = g_slurm_auth_pack(auth_cred, buffer);
		(void) g_slurm_auth_destroy(auth_cred);
		if (rc) {
			error("Packing authentication credential: %s",
			      g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		}
	}

}

int inline 
slurm_dbd_unpack_init_msg(dbd_init_msg_t **msg, Buf buffer, char *auth_info)
{
	void *auth_cred;

	dbd_init_msg_t *msg_ptr = xmalloc(sizeof(dbd_init_msg_t));
	*msg = msg_ptr;
	safe_unpack16(&msg_ptr->version, buffer);
	auth_cred = g_slurm_auth_unpack(buffer);
	if (auth_cred == NULL) {
		error("Unpacking authentication credential: %s",
			g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
		goto unpack_error;
	}
	msg_ptr->uid = g_slurm_auth_get_uid(auth_cred, auth_info);
	g_slurm_auth_destroy(auth_cred);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_job_complete_msg(dbd_job_comp_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack_time(msg->end_time, buffer);
	pack32(msg->exit_code, buffer);
	pack32(msg->job_id, buffer);
	pack16(msg->job_state, buffer);
	packstr(msg->name, buffer);
	packstr(msg->nodes, buffer);
	pack32(msg->priority, buffer);
	pack_time(msg->start_time, buffer);
	pack_time(msg->submit_time, buffer);
	pack32(msg->total_procs, buffer);
}

int inline 
slurm_dbd_unpack_job_complete_msg(dbd_job_comp_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_job_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_comp_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack_time(&msg_ptr->end_time, buffer);
	safe_unpack32(&msg_ptr->exit_code, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack16(&msg_ptr->job_state, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->priority, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack_time(&msg_ptr->submit_time, buffer);
	safe_unpack32(&msg_ptr->total_procs, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->name);
	xfree(msg_ptr->nodes);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_job_start_msg(dbd_job_start_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	packstr(msg->block_id, buffer);
	pack_time(msg->eligible_time, buffer);
	pack32(msg->job_id, buffer);
	pack16(msg->job_state, buffer);
	packstr(msg->name, buffer);
	packstr(msg->nodes, buffer);
	pack32(msg->priority, buffer);
	pack_time(msg->start_time, buffer);
	pack_time(msg->submit_time, buffer);
	pack32(msg->total_procs, buffer);
}

int inline 
slurm_dbd_unpack_job_start_msg(dbd_job_start_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_job_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_start_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->block_id, &uint32_tmp, buffer);
	safe_unpack_time(&msg_ptr->eligible_time, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack16(&msg_ptr->job_state, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->priority, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack_time(&msg_ptr->submit_time, buffer);
	safe_unpack32(&msg_ptr->total_procs, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->block_id);
	xfree(msg_ptr->name);
	xfree(msg_ptr->nodes);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_job_start_rc_msg(dbd_job_start_rc_msg_t *msg, Buf buffer)
{
	pack32(msg->db_index, buffer);
	pack32(msg->return_code, buffer);
}

int inline 
slurm_dbd_unpack_job_start_rc_msg(dbd_job_start_rc_msg_t **msg, Buf buffer)
{
	dbd_job_start_rc_msg_t *msg_ptr = 
		xmalloc(sizeof(dbd_job_start_rc_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack32(&msg_ptr->return_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_job_suspend_msg(dbd_job_suspend_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack32(msg->job_id, buffer);
	pack16(msg->job_state, buffer);
	pack_time(msg->submit_time, buffer);
	pack_time(msg->suspend_time, buffer);
}

int inline 
slurm_dbd_unpack_job_suspend_msg(dbd_job_suspend_msg_t **msg, Buf buffer)
{
	dbd_job_suspend_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_suspend_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack16(&msg_ptr->job_state, buffer);
	safe_unpack_time(&msg_ptr->submit_time, buffer);
	safe_unpack_time(&msg_ptr->suspend_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_rc_msg(dbd_rc_msg_t *msg, Buf buffer)
{
	pack32(msg->return_code, buffer);
}

int inline 
slurm_dbd_unpack_rc_msg(dbd_rc_msg_t **msg, Buf buffer)
{
	dbd_rc_msg_t *msg_ptr = xmalloc(sizeof(dbd_rc_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->return_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_node_state_msg(dbd_node_state_msg_t *msg, Buf buffer)
{
	packstr(msg->cluster_name, buffer);
	packstr(msg->hostlist, buffer);
	packstr(msg->reason, buffer);
	pack16(msg->new_state, buffer);
	pack_time(msg->event_time, buffer);
}

int inline
slurm_dbd_unpack_node_state_msg(dbd_node_state_msg_t **msg, Buf buffer)
{
	dbd_node_state_msg_t *msg_ptr;
	uint32_t uint32_tmp;

	msg_ptr = xmalloc(sizeof(dbd_node_state_msg_t));
	*msg = msg_ptr;
	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->hostlist, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->reason,   &uint32_tmp, buffer);
	safe_unpack16(&msg_ptr->new_state, buffer);
	safe_unpack_time(&msg_ptr->event_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->cluster_name);
	xfree(msg_ptr->hostlist);
	xfree(msg_ptr->reason);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_step_complete_msg(dbd_step_comp_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack_time(msg->end_time, buffer);
	pack32(msg->job_id, buffer);
	packstr(msg->name, buffer);
	packstr(msg->nodes, buffer);
	pack32(msg->req_uid, buffer);
	pack_time(msg->start_time, buffer);
	pack_time(msg->job_submit_time, buffer);
	pack32(msg->step_id, buffer);
	pack32(msg->total_procs, buffer);
}

int inline 
slurm_dbd_unpack_step_complete_msg(dbd_step_comp_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_step_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_comp_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack_time(&msg_ptr->end_time, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->req_uid, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack_time(&msg_ptr->job_submit_time, buffer);
	safe_unpack32(&msg_ptr->step_id, buffer);
	safe_unpack32(&msg_ptr->total_procs, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->name);
	xfree(msg_ptr->nodes);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurm_dbd_pack_step_start_msg(dbd_step_start_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack32(msg->job_id, buffer);
	packstr(msg->name, buffer);
	packstr(msg->nodes, buffer);
	pack32(msg->req_uid, buffer);
	pack_time(msg->start_time, buffer);
	pack32(msg->step_id, buffer);
	pack32(msg->total_procs, buffer);
}

int inline 
slurm_dbd_unpack_step_start_msg(dbd_step_start_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_step_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_start_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->req_uid, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack32(&msg_ptr->step_id, buffer);
	safe_unpack32(&msg_ptr->total_procs, buffer);
	return SLURM_SUCCESS;

unpack_error:
	xfree(msg_ptr->name);
	xfree(msg_ptr->nodes);
	xfree(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}
