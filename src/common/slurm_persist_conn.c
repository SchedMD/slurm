/*****************************************************************************\
 *  slurm_persist_conn.h - Definitions for communicating over a persistant
 *                         connection within Slurm.
 ******************************************************************************
 *  Copyright (C) 2016 SchedMD LLC
 *  Written by Danny Auble da@schedmd.com, et. al.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <poll.h>
#include <pthread.h>

#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/macros.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/slurmdbd_defs.h"
#include "slurm_persist_conn.h"

char *cluster_name = NULL;

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

/* static void _reopen_persist_conn(slurm_persist_conn_t *persist_conn) */
/* { */
/* 	xassert(persist_conn); */
/* 	_close_fd(&persist_conn->fd); */
/* 	slurm_persist_conn_open(persist_conn); */
/* } */

/* Wait until a file is readable,
 * RET false if can not be read */
static bool _conn_readable(slurm_persist_conn_t *persist_conn)
{
	struct pollfd ufds;
	int rc, time_left;
	struct timeval tstart;

	ufds.fd     = persist_conn->fd;
	ufds.events = POLLIN;
	gettimeofday(&tstart, NULL);
	while (persist_conn->inited) {
		time_left = persist_conn->read_timeout - _tot_wait(&tstart);
		rc = poll(&ufds, 1, time_left);
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
			debug2("persistant connection closed");
			return false;
		}
		if (ufds.revents & POLLNVAL) {
			error("persistant connection is invalid");
			return false;
		}
		if (ufds.revents & POLLERR) {
			error("persistant connection experienced an error");
			return false;
		}
		if ((ufds.revents & POLLIN) == 0) {
			error("persistant connection %d events %d",
			      persist_conn->fd, ufds.revents);
			return false;
		}
		/* revents == POLLIN */
		errno = 0;
		return true;
	}
	return false;
}

/* Wait until a file is writeable,
 * RET 1 if file can be written now,
 *     0 if can not be written to within 5 seconds
 *     -1 if file has been closed POLLHUP
 */
/* static int _conn_writeable(slurm_persist_conn_t *persist_conn) */
/* { */
/* 	struct pollfd ufds; */
/* 	int write_timeout = 5000; */
/* 	int rc, time_left; */
/* 	struct timeval tstart; */
/* 	char temp[2]; */

/* 	ufds.fd     = persist_conn->fd; */
/* 	ufds.events = POLLOUT; */
/* 	gettimeofday(&tstart, NULL); */
/* 	while (agent_shutdown == 0) { */
/* 		time_left = write_timeout - _tot_wait(&tstart); */
/* 		rc = poll(&ufds, 1, time_left); */
/* 		if (rc == -1) { */
/* 			if ((errno == EINTR) || (errno == EAGAIN)) */
/* 				continue; */
/* 			error("poll: %m"); */
/* 			return -1; */
/* 		} */
/* 		if (rc == 0) */
/* 			return 0; */
/* 		/\* */
/* 		 * Check here to make sure the socket really is there. */
/* 		 * If not then exit out and notify the sender.  This */
/*  		 * is here since a write doesn't always tell you the */
/* 		 * socket is gone, but getting 0 back from a */
/* 		 * nonblocking read means just that. */
/* 		 *\/ */
/* 		if (ufds.revents & POLLHUP || */
/* 		    (recv(persist_conn->fd, &temp, 1, 0) == 0)) { */
/* 			debug2("persistant connection is closed"); */
/* 			if (callbacks_requested) */
/* 				(callback.dbd_fail)(); */
/* 			return -1; */
/* 		} */
/* 		if (ufds.revents & POLLNVAL) { */
/* 			error("persistant connection is invalid"); */
/* 			return 0; */
/* 		} */
/* 		if (ufds.revents & POLLERR) { */
/* 			error("persistant connection experienced an error: %m"); */
/* 			if (callbacks_requested) */
/* 				(callback.dbd_fail)(); */
/* 			return 0; */
/* 		} */
/* 		if ((ufds.revents & POLLOUT) == 0) { */
/* 			error("persistant connection %d events %d", */
/* 			      persist_conn->fd, ufds.revents); */
/* 			return 0; */
/* 		} */
/* 		/\* revents == POLLOUT *\/ */
/* 		errno = 0; */
/* 		return 1; */
/* 	} */
/* 	return 0; */
/* } */

/* static int _send_msg(slurm_persist_conn_t *persist_conn, Buf buffer) */
/* { */
/* 	uint32_t msg_size, nw_size; */
/* 	char *msg; */
/* 	ssize_t msg_wrote; */
/* 	int rc, retry_cnt = 0; */

/* 	if (persist_conn->fd < 0) */
/* 		return EAGAIN; */

/* 	rc = _conn_writeable(persist_conn); */
/* 	if (rc == -1) { */
/* 	re_open: */
/* 		if (retry_cnt++ > 3) */
/* 			return EAGAIN; */
/* 		/\* if errno is ACCESS_DENIED do not try to reopen to */
/* 		   connection just return that *\/ */
/* 		if (errno == ESLURM_ACCESS_DENIED) */
/* 			return ESLURM_ACCESS_DENIED; */

/* 		if (persist_conn->reconnect) { */
/* 			_reopen_fd(persist_conn); */
/* 			rc = _conn_writeable(persist_conn); */
/* 		} */
/* 	} */
/* 	if (rc < 1) */
/* 		return EAGAIN; */

/* 	msg_size = get_buf_offset(buffer); */
/* 	nw_size = htonl(msg_size); */
/* 	msg_wrote = write(persist_conn->fd, &nw_size, sizeof(nw_size)); */
/* 	if (msg_wrote != sizeof(nw_size)) */
/* 		return EAGAIN; */

/* 	msg = get_buf_data(buffer); */
/* 	while (msg_size > 0) { */
/* 		rc = _conn_writeable(persist_conn); */
/* 		if (rc == -1) */
/* 			goto re_open; */
/* 		if (rc < 1) */
/* 			return EAGAIN; */
/* 		msg_wrote = write(persist_conn->fd, msg, msg_size); */
/* 		if (msg_wrote <= 0) */
/* 			return EAGAIN; */
/* 		msg += msg_wrote; */
/* 		msg_size -= msg_wrote; */
/* 	} */

/* 	return SLURM_SUCCESS; */
/* } */

static Buf _recv_msg(slurm_persist_conn_t *persist_conn)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_read, offset;
	Buf buffer;

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
		error("Persistant Conn: Invalid msg_size (%u)", msg_size);
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
			error("Persistant Conn: read: %m");
			break;
		}
		offset += msg_read;
	}
	if (msg_size != offset) {
		if (!persist_conn->inited) {
			error("Persistant Conn: only read %zd of %d bytes",
			      offset, msg_size);
		}	/* else in shutdown mode */
		xfree(msg);
		goto endit;
	}

	buffer = create_buf(msg, msg_size);
	return buffer;

endit:
	/* Close it since we abondoned it.  If the connection does still exist
	 * on the other end we can't rely on it after this point since we didn't
	 * listen long enough for this response.
	 */
	_close_fd(&persist_conn->fd);

	return NULL;
}

/* Open a persistant socket connection
 * IN/OUT - persistant connection needing host and port filled in.  Returned
 * completely filled in.
 * Returns SLURM_SUCCESS on success or SLURM_ERROR on failure */
extern int slurm_persist_conn_open(slurm_persist_conn_t *persist_conn)
{
	int rc = SLURM_ERROR;
	slurm_msg_t req_msg;
	slurm_addr_t addr;
	persist_init_req_msg_t req;
	persist_init_resp_msg_t *resp = NULL;

	xassert(persist_conn);
	xassert(persist_conn->host);
	xassert(persist_conn->port);
	xassert(persist_conn->cluster_name);

	if (persist_conn->fd > 0)
		_close_fd(&persist_conn->fd);
	else
		persist_conn->fd = -1;

	if (!persist_conn->inited) {
		slurm_mutex_init(&persist_conn->lock);
		slurm_cond_init(&persist_conn->cond, NULL);
		persist_conn->inited = true;
	}

	if (!persist_conn->version)
		persist_conn->version = SLURM_MIN_PROTOCOL_VERSION;
	if (persist_conn->read_timeout < 0)
		persist_conn->read_timeout = slurm_get_msg_timeout() * 1000;

	slurm_set_addr_char(&addr, persist_conn->port, persist_conn->host);
	if ((persist_conn->fd = slurm_open_msg_conn(&addr)) < 0) {
		error("%s: failed to open persistant connection to %s:%d",
		      __func__, persist_conn->host, persist_conn->port);
		return rc;
	}
	fd_set_nonblocking(persist_conn->fd);
	fd_set_close_on_exec(persist_conn->fd);

	slurm_msg_t_init(&req_msg);

	/* Always send the lowest protocol since we don't know what version the
	 * other side is running yet.
	 */
	req_msg.protocol_version = persist_conn->version;
	req_msg.msg_type = REQUEST_PERSIST_INIT;
	if (persist_conn->dbd_conn)
		req_msg.flags |= SLURMDBD_CONNECTION;

	memset(&req, 0, sizeof(persist_init_req_msg_t));
	req.cluster_name = persist_conn->cluster_name;
	req.version  = SLURM_PROTOCOL_VERSION;

	req_msg.data = &req;

	if (slurm_send_node_msg(persist_conn->fd, &req_msg) < 0) {
		error("%s: failed to send persistent connection init message to %s:%d",
		      __func__, persist_conn->host, persist_conn->port);
		_close_fd(&persist_conn->fd);
	} else {
		Buf buffer = _recv_msg(persist_conn);
		persist_msg_t msg;
		bool dbd_conn = persist_conn->dbd_conn;

		if (!buffer) {
			error("%s: No response to persist_init", __func__);
			_close_fd(&persist_conn->fd);
			goto end_it;
		}
		memset(&msg, 0, sizeof(persist_msg_t));
		/* The first unpack is done the same way for dbd or normal
		 * communication . */
		persist_conn->dbd_conn = false;
		rc = slurm_persist_msg_unpack(persist_conn, &msg, buffer);
		persist_conn->dbd_conn = dbd_conn;
		free_buf(buffer);

		resp = (persist_init_resp_msg_t *)msg.data;
		if (resp && rc == SLURM_SUCCESS)
			rc = resp->rc;

		if (rc != SLURM_SUCCESS) {
			if (resp)
				error("%s: Something happened with the receiving/processing of the persistent connection init message to %s:%d: %s",
				      __func__, persist_conn->host,
				      persist_conn->port, resp->comment);
			else
				error("%s: Failed to unpack persistent connection init resp message from %s:%d",
			      __func__, persist_conn->host, persist_conn->port);
			_close_fd(&persist_conn->fd);
		} else
			persist_conn->version = resp->version;
	}

end_it:

	slurm_persist_free_init_resp_msg(resp);

	return rc;
}

/* Close the persistant connection */
extern void slurm_persist_conn_close(slurm_persist_conn_t *persist_conn)
{
	if (persist_conn) {
		persist_conn->inited = false;
		slurm_mutex_lock(&persist_conn->lock);
		_close_fd(&persist_conn->fd);
		xfree(persist_conn->cluster_name);
		xfree(persist_conn->host);
		slurm_mutex_unlock(&persist_conn->lock);
		slurm_mutex_destroy(&persist_conn->lock);
		slurm_cond_destroy(&persist_conn->cond);
		xfree(persist_conn);
	}
}

extern Buf slurm_persist_msg_pack(slurm_persist_conn_t *persist_conn,
				  persist_msg_t *req_msg)
{
	Buf buffer;

	xassert(persist_conn);

	if (persist_conn->dbd_conn)
		buffer = pack_slurmdbd_msg((slurmdbd_msg_t *)req_msg,
					   persist_conn->version);
	else {
		slurm_msg_t msg;

		slurm_msg_t_init(&msg);

		msg.data = req_msg->data;
		msg.protocol_version = persist_conn->version;

		buffer = init_buf(BUF_SIZE);

		pack16(req_msg->msg_type, buffer);
		pack_msg(&msg, buffer);
	}

	return buffer;
}


extern int slurm_persist_msg_unpack(slurm_persist_conn_t *persist_conn,
				    persist_msg_t *resp_msg, Buf buffer)
{
	int rc;

	xassert(persist_conn);
	xassert(resp_msg);

	if (persist_conn->dbd_conn)
		rc = unpack_slurmdbd_msg((slurmdbd_msg_t *)resp_msg,
					 persist_conn->version,
					 buffer);
	else {
		slurm_msg_t msg;

		slurm_msg_t_init(&msg);

		msg.protocol_version = persist_conn->version;

		safe_unpack16(&msg.msg_type, buffer);

		rc = unpack_msg(&msg, buffer);

		resp_msg->msg_type = msg.msg_type;
		resp_msg->data = msg.data;
	}

	return rc;
unpack_error:
	return SLURM_ERROR;
}

extern void slurm_persist_pack_init_req_msg(
	persist_init_req_msg_t *msg, Buf buffer)
{
	pack16(msg->version, buffer);

	/* Adding anything to this needs to happen after the version
	   since this is where the receiver gets the version from. */
	packstr(msg->cluster_name, buffer);
}

extern int slurm_persist_unpack_init_req_msg(
	persist_init_req_msg_t **msg, Buf buffer)
{
	uint32_t tmp32;

	persist_init_req_msg_t *msg_ptr =
		xmalloc(sizeof(persist_init_req_msg_t));

	*msg = msg_ptr;

	safe_unpack16(&msg_ptr->version, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &tmp32, buffer);

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

extern void slurm_persist_pack_init_resp_msg(
	persist_init_resp_msg_t *msg, Buf buffer, uint16_t protocol_version)
{
	packstr(msg->comment, buffer);
	pack32(msg->rc, buffer);
	pack16(msg->version, buffer);
}

extern int slurm_persist_unpack_init_resp_msg(
	persist_init_resp_msg_t **msg, Buf buffer, uint16_t protocol_version)
{
	uint32_t uint32_tmp;

	persist_init_resp_msg_t *msg_ptr =
		xmalloc(sizeof(persist_init_resp_msg_t));

	*msg = msg_ptr;

	safe_unpackstr_xmalloc(&msg_ptr->comment, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->rc, buffer);
	safe_unpack16(&msg_ptr->version, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurm_persist_free_init_resp_msg(msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

extern void slurm_persist_free_init_resp_msg(persist_init_resp_msg_t *msg)
{
	if (msg) {
		xfree(msg->comment);
		xfree(msg);
	}
}

/* extern int slurm_persist_send_recv_msg(slurm_persist_conn_t *persist_conn, */
/* 				       persist_msg_t *req, persist_msg_t *resp) */
/* { */
/* 	int rc = SLURM_SUCCESS, read_timeout; */
/* 	Buf buffer; */

/* 	xassert(persist_conn); */
/* 	xassert(req); */
/* 	xassert(resp); */

/* 	/\* To make sure we can get this to send instead of the agent */
/* 	   sending stuff that can happen anytime we set halt_agent and */
/* 	   then after we get into the mutex we unset. */
/* 	*\/ */
/* 	halt_agent = 1; */
/* 	slurm_mutex_lock(&persist_conn->lock); */
/* 	halt_agent = 0; */
/* 	if (persist_conn->fd < 0) { */
/* 		if (!persist_conn->reconnect) { */
/* 			rc = SLURM_ERROR; */
/* 			goto end_it; */
/* 		} */
/* 		/\* Either slurm_open_slurmdbd_conn() was not executed or */
/* 		 * the connection to Slurm DBD has been closed *\/ */
/* 		if (req->msg_type == DBD_GET_CONFIG) */
/* 			_open_slurmdbd_fd(0); */
/* 		else */
/* 			_open_slurmdbd_fd(1); */
/* 		if (slurmdbd_fd < 0) { */
/* 			rc = SLURM_ERROR; */
/* 			goto end_it; */
/* 		} */
/* 	} */

/* 	if (!(buffer = pack_slurmdbd_msg(req, rpc_version))) { */
/* 		rc = SLURM_ERROR; */
/* 		goto end_it; */
/* 	} */

/* 	rc = _send_msg(buffer); */
/* 	free_buf(buffer); */
/* 	if (rc != SLURM_SUCCESS) { */
/* 		error("slurmdbd: Sending message type %s: %d: %m", */
/* 		      rpc_num2string(req->msg_type), rc); */
/* 		goto end_it; */
/* 	} */

/* 	buffer = _recv_msg(persist_conn); */
/* 	if (buffer == NULL) { */
/* 		error("slurmdbd: Getting response to message type %u", */
/* 		      req->msg_type); */
/* 		rc = SLURM_ERROR; */
/* 		goto end_it; */
/* 	} */

/* 	rc = unpack_slurmdbd_msg(resp, rpc_version, buffer); */
/* 	/\* check for the rc of the start job message *\/ */
/* 	if (rc == SLURM_SUCCESS && resp->msg_type == DBD_ID_RC) */
/* 		rc = ((dbd_id_rc_msg_t *)resp->data)->return_code; */

/* 	free_buf(buffer); */
/* end_it: */
/* 	slurm_cond_signal(&persist_conn->cond); */
/* 	slurm_mutex_unlock(&persist_conn->lock); */

/* 	return rc; */
/* } */

