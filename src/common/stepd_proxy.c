/*****************************************************************************\
 *  stepd_proxy.c
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

#include <sys/un.h>

#include "src/common/fd.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/stepd_api.h"
#include "src/common/stepd_proxy.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/tls.h"

static char *slurmd_spooldir = NULL;

static int _slurmd_pack_msg_to_stepd(slurm_msg_t *resp, buf_t *out)
{
	uint32_t length_position, end_position;

	/* save position of length for later */
	length_position = get_buf_offset(out);
	pack32(0, out);

	pack16(resp->msg_type, out);
	if (pack_msg(resp, out) != SLURM_SUCCESS) {
		FREE_NULL_BUFFER(out);
		return SLURM_ERROR;
	}

	/* write length then reset out to end of message */
	end_position = get_buf_offset(out);
	set_buf_offset(out, length_position);
	pack32((end_position - length_position - sizeof(uint32_t)), out);
	set_buf_offset(out, end_position);

	return SLURM_SUCCESS;
}

static int _slurmd_send_resp_to_stepd(conmgr_fd_t *con, slurm_msg_t *resp)
{
	buf_t *out = init_buf(BUF_SIZE);
	int rc = SLURM_SUCCESS;

	if (_slurmd_pack_msg_to_stepd(resp, out)) {
		error("%s: Failed to pack response to slurmstepd", __func__);
		rc = SLURM_ERROR;
		goto cleanup;
	}
	if (conmgr_fd_xfer_out_buffer(con, out)) {
		error("%s: Failed to transfer buffer for response to slurmstepd",
		      __func__);
		rc = SLURM_ERROR;
		goto cleanup;
	}

cleanup:
	FREE_NULL_BUFFER(out);
	return rc;
}

static int _slurmd_send_rc_to_stepd(conmgr_fd_t *con, int rc,
				    uint16_t protocol_version)
{
	slurm_msg_t resp;
	return_code_msg_t rc_msg = { 0 };

	/*
	 * It's possible we didn't even unpack slurmstepd's protocol version.
	 * In that case, just try to use slurmd's protocol version.
	 */
	if (!protocol_version)
		protocol_version = SLURM_PROTOCOL_VERSION;

	slurm_msg_t_init(&resp);
	resp.protocol_version = protocol_version;
	resp.msg_type = RESPONSE_SLURM_RC;
	rc_msg.return_code = rc;
	resp.data = &rc_msg;

	return _slurmd_send_resp_to_stepd(con, &resp);
}

static int _slurmd_send_recv_msg(conmgr_fd_t *con, slurm_msg_t *req,
				 slurm_msg_t *resp, int timeout,
				 uint16_t proxy_type)
{
	int rc = SLURM_SUCCESS;

	switch (proxy_type) {
	case PROXY_TO_CTLD_SEND_ONLY:
		rc = slurm_send_only_controller_msg(req, working_cluster_rec);
		break;
	case PROXY_TO_CTLD_SEND_RECV:
		rc = slurm_send_recv_controller_msg(req, resp,
						    working_cluster_rec);
		break;
	case PROXY_TO_NODE_SEND_RECV:
		rc = slurm_send_recv_node_msg(req, resp, timeout);
		break;
	case PROXY_TO_NODE_SEND_ONLY:
		rc = slurm_send_only_node_msg(req);
		break;
	default:
		rc = SLURM_ERROR;
		error("%s: Unknown proxy type %u", __func__, proxy_type);
		break;
	}

	if (rc) {
		error("%s: Failed to send/recv slurmstepd message %s using proxy_type %s: %m",
		      __func__, rpc_num2string(req->msg_type),
		      rpc_num2string(proxy_type));
	}

	return rc;
}

static int _on_data_local_socket(conmgr_fd_t *con, void *arg)
{
	buf_t *in = NULL;
	uint32_t length, timeout, r_uid;
	uint16_t msg_type, protocol_version, proxy_type;
	int rc = SLURM_SUCCESS;
	slurm_addr_t req_address = { 0 };
	slurm_msg_t req, resp;
	return_code_msg_t *rc_msg;
	char *req_tls_cert = NULL;

	uid_t uid = SLURM_AUTH_NOBODY;
	gid_t gid = SLURM_AUTH_NOBODY;
	pid_t pid = 0;

	if (!(in = conmgr_fd_shadow_in_buffer(con))) {
		error("%s: conmgr_fd_shadow_in_buffer() failed", __func__);
		rc = ESLURMD_STEPD_PROXY_FAILED;
		goto unpack_error;
	}

	safe_unpack16(&protocol_version, in);
	safe_unpack32(&length, in);
	safe_unpack16(&msg_type, in);
	safe_unpack32(&timeout, in);
	safe_unpack16(&proxy_type, in);

	switch (proxy_type) {
	case PROXY_TO_NODE_SEND_RECV:
	case PROXY_TO_NODE_SEND_ONLY:
		if (slurm_unpack_addr_no_alloc(&req_address, in))
			goto unpack_error;
		safe_unpackstr(&req_tls_cert, in);
		break;
	default:
		/* don't need address for ctld messages */
		break;
	}
	safe_unpack32(&r_uid, in);

	if (conmgr_get_fd_auth_creds(con, &uid, &gid, &pid)) {
		error("%s: conmgr_get_fd_auth_creds() failed", __func__);
		rc = ESLURMD_STEPD_PROXY_FAILED;
		goto unpack_error;
	}

	if (uid != slurm_conf.slurmd_user_id) {
		error("%s: uid %u does not match slurmd user %u",
		      __func__, uid, slurm_conf.slurmd_user_id);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto unpack_error;
	}

	if (size_buf(in) < (length + sizeof(uint16_t))) {
		log_flag(TLS, "incomplete message, only %u bytes available of %u bytes",
			 size_buf(in), length);
		FREE_NULL_BUFFER(in);
		/* Do not close connection */
		return SLURM_SUCCESS;
	}
	conmgr_fd_mark_consumed_in_buffer(con, length);

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	req.protocol_version = protocol_version;
	req.msg_type = msg_type;
	req.address = req_address;
	req.tls_cert = req_tls_cert;
	slurm_msg_set_r_uid(&req, r_uid);

	if (unpack_msg(&req, in) != SLURM_SUCCESS) {
		error("%s: Failed to unpack message from slurmstepd to relay to slurmctld",
		      __func__);
		rc = ESLURMD_STEPD_PROXY_FAILED;
		goto unpack_error;
	}

	if (_slurmd_send_recv_msg(con, &req, &resp, timeout, proxy_type)) {
		rc = ESLURMD_STEPD_PROXY_FAILED;
		goto unpack_error;
	}

	/*
	 * Send success rc back to slurmstepd for SEND_ONLY messages so
	 * slurmstepd knows its message was successfully sent.
	 */
	switch (proxy_type) {
	case PROXY_TO_NODE_SEND_ONLY:
	case PROXY_TO_CTLD_SEND_ONLY:
		rc_msg = xmalloc(sizeof(*rc_msg));
		resp.protocol_version = protocol_version;
		resp.msg_type = RESPONSE_SLURM_RC;
		rc_msg->return_code = SLURM_SUCCESS;
		resp.data = rc_msg;
		break;
	}

	_slurmd_send_resp_to_stepd(con, &resp);

unpack_error:
	/*
	 * attempt to send rc back to slurmstepd so that slurmstepd knows an
	 * error occurred and its message was not actually sent
	 */
	if (rc && _slurmd_send_rc_to_stepd(con, rc, protocol_version)) {
		error("%s: Failed to send rc to slurmstepd saying that the proxy failed",
		      __func__);
	}

	xfree(req_tls_cert);
	slurm_free_msg_data(req.msg_type, req.data);
	slurm_free_msg_data(resp.msg_type, resp.data);

	conmgr_queue_close_fd(con);
	FREE_NULL_BUFFER(in);

	return rc;
}

extern void stepd_proxy_slurmd_init(char *spooldir)
{
	static const conmgr_events_t events = {
		.on_data = _on_data_local_socket,
	};
	static char *path = NULL;
	int rc;

	if (!path)
		xstrfmtcat(path, "unix:%s/slurmd.socket", spooldir);

	if ((rc = conmgr_create_listen_socket(CON_TYPE_RAW, CON_FLAG_NONE, path,
					      &events, NULL)))
		fatal("%s: [%s] unable to create socket: %s",
		      __func__, path, slurm_strerror(rc));
}

extern void stepd_proxy_stepd_init(char *spooldir)
{
	slurmd_spooldir = xstrdup(spooldir);
}

static int _stepd_connect_to_slurmd(void)
{
	struct sockaddr_un slurmd_addr = { .sun_family = AF_UNIX };
	size_t len;
	int fd;

	(void) snprintf(slurmd_addr.sun_path, sizeof(slurmd_addr.sun_path),
			"%s/slurmd.socket", slurmd_spooldir);

	len = strlen(slurmd_addr.sun_path) + 1 + sizeof(slurmd_addr.sun_family);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		error("%s: socket() failed: %m", __func__);
		return -1;
	}

	if (connect(fd, (struct sockaddr *) &slurmd_addr, len) < 0) {
		error("%s: connect() failed for %s: %m",
		      __func__, slurmd_addr.sun_path);
		close(fd);
		return -1;
	}

	log_flag(NET, "%s: Opened connection to slurmd listening socket at '%s'",
		 __func__, slurmd_addr.sun_path);

	return fd;
}

static int _stepd_send_to_slurmd(int fd, slurm_msg_t *req, int timeout,
				 uint16_t proxy_type)
{
	uint32_t length_position, end_position;
	buf_t *buffer = init_buf(BUF_SIZE);

	pack16(SLURM_PROTOCOL_VERSION, buffer);

	/* save position of length for later */
	length_position = get_buf_offset(buffer);
	pack32(0, buffer);

	pack16(req->msg_type, buffer);
	pack32(timeout, buffer);
	pack16(proxy_type, buffer);

	switch (proxy_type) {
	case PROXY_TO_NODE_SEND_RECV:
	case PROXY_TO_NODE_SEND_ONLY:
		slurm_pack_addr(&req->address, buffer);
		packstr(req->tls_cert, buffer);
		break;
	default:
		/* don't need address for ctld messages */
		break;
	}
	pack32(req->restrict_uid, buffer);

	if (pack_msg(req, buffer) != SLURM_SUCCESS) {
		error("%s: could not pack req", __func__);
		goto pack_error;
	}

	/* write length then reset buffer to end of message */
	end_position = get_buf_offset(buffer);
	set_buf_offset(buffer, length_position);
	pack32(end_position - length_position, buffer);
	set_buf_offset(buffer, end_position);

	/* send to slurmd */
	safe_write(fd, get_buf_data(buffer), get_buf_offset(buffer));
	FREE_NULL_BUFFER(buffer);

	log_flag(NET, "%s: sent message %s using proxy_type %s (via slurmd)",
		 __func__, rpc_num2string(req->msg_type),
		 rpc_num2string(proxy_type));

	return SLURM_SUCCESS;

rwfail:
pack_error:
	FREE_NULL_BUFFER(buffer);
	return SLURM_ERROR;
}

static int _stepd_recv_from_slurmd(int fd, slurm_msg_t *resp)
{
	uint32_t len;
	buf_t *buffer = NULL;

	/* read response from slurmd */
	safe_read(fd, &len, sizeof(uint32_t));
	if (!(len = ntohl(len)))
		goto rwfail;
	buffer = init_buf(len);
	safe_read(fd, buffer->head, len);

	slurm_msg_t_init(resp);

	safe_unpack16(&resp->msg_type, buffer);
	if (unpack_msg(resp, buffer) != SLURM_SUCCESS) {
		error("%s: could not unpack resp for %s message",
		      __func__, rpc_num2string(resp->msg_type));
		goto unpack_error;
	}
	FREE_NULL_BUFFER(buffer);

	log_flag(NET, "%s: received message %s (via slurmd)",
		 __func__, rpc_num2string(resp->msg_type));

	return SLURM_SUCCESS;
rwfail:
unpack_error:
	FREE_NULL_BUFFER(buffer);
	return SLURM_ERROR;
}

static int _stepd_send_recv_msg(slurm_msg_t *req, slurm_msg_t *resp,
				int timeout, uint16_t proxy_type)
{
	int fd;

	xassert(req);

	if ((fd = _stepd_connect_to_slurmd()) < 0) {
		error("%s: failed to connect to slurmd socket", __func__);
		return SLURM_ERROR;
	}

	if (_stepd_send_to_slurmd(fd, req, timeout, proxy_type)) {
		error("%s: failed to send %s message to slurmd using proxy_type %s",
		      __func__, rpc_num2string(req->msg_type),
		      rpc_num2string(proxy_type));
		close(fd);
		return SLURM_ERROR;
	}

	if (_stepd_recv_from_slurmd(fd, resp)) {
		error("%s: failed to receive response from slurmd proxy for %s message", __func__,
		      rpc_num2string(req->msg_type));
		close(fd);
		return SLURM_ERROR;
	}
	close(fd);

	/* Check if slurmd hit any errors trying to send our request message */
	if (resp->msg_type == RESPONSE_SLURM_RC) {
		switch (((return_code_msg_t *) resp->data)->return_code) {
		case ESLURMD_STEPD_PROXY_FAILED:
			error("%s: slurmd was unable to proxy request message to its final destination",
			      __func__);
			return SLURM_ERROR;
		case SLURM_PROTOCOL_AUTHENTICATION_ERROR:
			error("%s: slurmd was unable to authenticate message we sent",
			      __func__);
			return SLURM_ERROR;
		default:
			/* No proxy related errors */
			break;
		}
	}

	return SLURM_SUCCESS;
}

extern int stepd_proxy_send_only_ctld_msg(slurm_msg_t *req)
{
	int rc;
	/*
	 * need response message to see if slurmd successfully sent message to
	 * its final destination.
	 */
	slurm_msg_t resp;

	xassert(running_in_slurmstepd());

	slurm_msg_t_init(&resp);
	rc = _stepd_send_recv_msg(req, &resp, 0, PROXY_TO_CTLD_SEND_ONLY);
	slurm_free_msg_data(resp.msg_type, resp.data);

	return rc;
}

extern int stepd_proxy_send_recv_ctld_msg(slurm_msg_t *req, slurm_msg_t *resp)
{
	xassert(running_in_slurmstepd());
	return _stepd_send_recv_msg(req, resp, 0, PROXY_TO_CTLD_SEND_RECV);
}

extern int stepd_proxy_send_only_node_msg(slurm_msg_t *req)
{
	int rc;
	/*
	 * need response message to see if  slurmd successfully sent message to
	 * its final destination.
	 */
	slurm_msg_t resp;

	xassert(running_in_slurmstepd());

	slurm_msg_t_init(&resp);
	rc = _stepd_send_recv_msg(req, &resp, 0, PROXY_TO_NODE_SEND_ONLY);
	slurm_free_msg_data(resp.msg_type, resp.data);

	return rc;
}

extern int stepd_proxy_send_recv_node_msg(slurm_msg_t *req, slurm_msg_t *resp,
					  int timeout)
{
	xassert(running_in_slurmstepd());
	return _stepd_send_recv_msg(req, resp, timeout,
				    PROXY_TO_NODE_SEND_RECV);
}

static int _slurmd_send_msg_to_stepd(int fd, slurm_msg_t *req)
{
	int req_msg_type = req->msg_type;
	uint32_t buf_size;

	safe_write(fd, &req_msg_type, sizeof(int));

	buf_size = get_buf_offset(req->buffer) - req->body_offset;

	safe_write(fd, &req->protocol_version, sizeof(uint16_t));
	safe_write(fd, &buf_size, sizeof(uint32_t));
	safe_write(fd, &req->buffer->head[req->body_offset], buf_size);

	return SLURM_SUCCESS;

rwfail:
	error("%s: Failed to write to stepd: %m", __func__);
	return SLURM_ERROR;
}

static int _slurmd_recv_msg_from_stepd(int fd, buf_t **resp_buf)
{
	uint32_t data_size;
	char *data = NULL;
	buf_t *buf;

	/* see _stepd_send_resp_to_slurmd() */
	safe_read(fd, &data_size, sizeof(uint32_t));
	data_size = ntohl(data_size);
	data = xmalloc(data_size);
	safe_read(fd, data, data_size);

	buf = create_buf(data, data_size);

	*resp_buf = buf;

	return SLURM_SUCCESS;
rwfail:
	error("%s: Failed to read from stepd: %m", __func__);
	return SLURM_ERROR;
}

extern int stepd_proxy_send_recv_to_stepd(slurm_msg_t *req, buf_t **resp_buf,
					  slurm_step_id_t *step_id,
					  int stepd_fd, bool reply)
{
	xassert(running_in_slurmd());

	fd_set_nonblocking(stepd_fd);

	if (_slurmd_send_msg_to_stepd(stepd_fd, req)) {
		error("%s: Failed to send msg to stepd", __func__);
		return SLURM_ERROR;
	}

	if (!reply)
		return SLURM_SUCCESS;

	if (_slurmd_recv_msg_from_stepd(stepd_fd, resp_buf)) {
		error("%s: Failed to receive response from stepd", __func__);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _stepd_send_resp_to_slurmd(int fd, uint32_t msglen,
				      msg_bufs_t *buffers)
{
	xassert(buffers);

	/* see _slurmd_recv_msg_from_stepd() */
	safe_write(fd, &msglen, sizeof(msglen));
	safe_write(fd, get_buf_data(buffers->header),
		   get_buf_offset(buffers->header));
	/* No auth, SLURM_NO_AUTH_CRED is set */
	safe_write(fd, get_buf_data(buffers->body),
		   get_buf_offset(buffers->body));

	return SLURM_SUCCESS;
rwfail:
	return SLURM_ERROR;
}

extern int stepd_proxy_send_resp_to_slurmd(int fd, slurm_msg_t *source_msg,
					   slurm_msg_type_t msg_type,
					   void *data)
{
	msg_bufs_t buffers = { 0 };
	uint32_t msglen = 0;
	int rc;
	slurm_msg_t resp_msg;

	xassert(running_in_slurmstepd());

	slurm_resp_msg_init(&resp_msg, source_msg, msg_type, data);

	if (slurm_buffers_pack_msg(&resp_msg, &buffers, true)) {
		rc = SLURM_ERROR;
		goto end;
	}

	msglen = get_buf_offset(buffers.body) + get_buf_offset(buffers.header);
	/* No auth, SLURM_NO_AUTH_CRED is set */

	msglen = htonl(msglen);

	rc = _stepd_send_resp_to_slurmd(fd, msglen, &buffers);

end:
	FREE_NULL_BUFFER(buffers.auth);
	FREE_NULL_BUFFER(buffers.body);
	FREE_NULL_BUFFER(buffers.header);

	return rc;
}
