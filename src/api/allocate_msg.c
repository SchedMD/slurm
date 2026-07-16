/*****************************************************************************\
 *  allocate_msg.c - Message handler for communication with with
 *                       the slurmctld during an allocation.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "slurm/slurm.h"

#include "src/common/duplex_relay.h"
#include "src/common/events.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#define MAGIC_X11_CON 0xba59504c

typedef struct {
	int magic; /* MAGIC_X11_CON */
	conmgr_fd_ref_t *con;
	slurm_msg_t *resp_msg;
} x11_con_t;

typedef struct {
	void (*func)(slurm_allocation_callbacks_t *callbacks, slurm_msg_t *msg);
	uint16_t msg_type;
} alloc_rpc_t;

static void _handle_ping(slurm_allocation_callbacks_t *callbacks,
			 slurm_msg_t *msg)
{
	debug3("received ping message");
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void _handle_job_complete(slurm_allocation_callbacks_t *callbacks,
				 slurm_msg_t *msg)
{
	srun_job_complete_msg_t *comp = msg->data;
	debug3("job complete message received");

	if (callbacks->job_complete != NULL)
		(callbacks->job_complete)(comp);
}

/*
 * Job has been notified of it's approaching time limit.
 * Job will be killed shortly after timeout.
 * This RPC can arrive multiple times with the same or updated timeouts.
 */
static void _handle_timeout(slurm_allocation_callbacks_t *callbacks,
			    slurm_msg_t *msg)
{
	srun_timeout_msg_t *to = msg->data;

	debug3("received timeout message");

	if (callbacks->timeout != NULL)
		(callbacks->timeout)(to);
}

static void _handle_user_msg(slurm_allocation_callbacks_t *callbacks,
			     slurm_msg_t *msg)
{
	srun_user_msg_t *um = msg->data;
	debug3("received user message");

	if (callbacks->user_msg != NULL)
		(callbacks->user_msg)(um);
}

static void _handle_node_fail(slurm_allocation_callbacks_t *callbacks,
			      slurm_msg_t *msg)
{
	srun_node_fail_msg_t *nf = msg->data;

	if (callbacks->node_fail != NULL)
		(callbacks->node_fail)(nf);
}

static void _handle_suspend(slurm_allocation_callbacks_t *callbacks,
			    slurm_msg_t *msg)
{
	suspend_msg_t *sus_msg = msg->data;
	debug3("received suspend message");

	if (callbacks->job_suspend != NULL)
		(callbacks->job_suspend)(sus_msg);
}

static void _x11_con_free(x11_con_t *x11con)
{
	if (!x11con)
		return;

	xassert(x11con->magic == MAGIC_X11_CON);
	x11con->magic = ~MAGIC_X11_CON;
	conmgr_con_queue_close_free(&x11con->con);
	FREE_NULL_MSG(x11con->resp_msg);
	xfree(x11con);
}

static void _x11_con_duplex_relay_assign(conmgr_callback_args_t conmgr_args,
					 void *arg)
{
	x11_con_t *x11con = arg;
	conmgr_fd_ref_t *con = conmgr_args.ref;
	int rc = SLURM_ERROR;

	xassert(x11con->magic == MAGIC_X11_CON);
	if ((rc = duplex_relay_assign(con, x11con->con))) {
		error("%s: [%s] Failed to initialize second connection in duplex relay",
		      __func__, conmgr_con_get_name(con));
		/*
		 * duplex_relay_assign() failed before reassigning the
		 * connection's events, so con still references x11con through
		 * its arg and the _x11_con_on_finish() callback. Close con and
		 * let _x11_con_on_finish() free x11con to avoid a
		 * use-after-free.
		 */
		conmgr_con_queue_close(con);
		return;
	}

	log_flag(NET, "%s: [%s] Opened and initialized connection to local x11 server, X11 tunnel is ready now",
		 __func__, conmgr_con_get_name(con));

	/* Cleanup state but avoid closing the connections */
	CONMGR_CON_UNLINK(x11con->con);

	_x11_con_free(x11con);
}

static void *_x11_con_on_connection(conmgr_callback_args_t conmgr_args,
				    void *arg)
{
	x11_con_t *x11con = arg;
	int rc;

	if ((rc = conmgr_con_queue_write_msg(x11con->con, x11con->resp_msg))) {
		error("%s: [%s] Failed to write SLURM_SUCCESS back to slurmstepd: %s",
		      __func__, conmgr_con_get_name(conmgr_args.ref),
		      slurm_strerror(rc));
		_x11_con_free(x11con);
		return NULL;
	}

	conmgr_add_work_con_fifo(conmgr_args.con, _x11_con_duplex_relay_assign,
				 arg);

	return arg;
}

static void _x11_con_on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	x11_con_t *x11con = arg;

	if (!x11con)
		return;

	xassert(x11con->magic == MAGIC_X11_CON);

	_x11_con_free(x11con);
}

static int _x11_con_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	/*
	 * This should never be called, connection should be quiesced until
	 * on_data() is reassigned by duplex_relay.
	 */
	xassert(false);

	return SLURM_ERROR;
}

static void _handle_net_forward(slurm_allocation_callbacks_t *callbacks,
				slurm_msg_t *msg)
{
	conmgr_fd_ref_t *con = msg->conmgr_con;
	net_forward_msg_t *forward_msg = msg->data;
	slurm_addr_t local_x11_server_addr = { 0 };
	socklen_t addrlen = sizeof(local_x11_server_addr);
	static const conmgr_events_t events = {
		.on_connection = _x11_con_on_connection,
		.on_data = _x11_con_on_data,
		.on_finish = _x11_con_on_finish,
	};
	int rc;
	x11_con_t *x11con = NULL;
	return_code_msg_t *success_rc_msg;

	if (forward_msg->port) {
		slurm_set_addr(&local_x11_server_addr, forward_msg->port,
			       forward_msg->target);
	} else if (forward_msg->target) {
		local_x11_server_addr =
			sockaddr_from_unix_path(forward_msg->target);
		addrlen = sizeof(struct sockaddr_un);
	}

	x11con = xmalloc(sizeof(*x11con));
	*x11con = (x11_con_t) {
		.magic = MAGIC_X11_CON,
		.resp_msg = xmalloc(sizeof(*x11con->resp_msg)),
	};

	CONMGR_CON_LINK(con, x11con->con);

	/*
	 * Prepare SLURM_SUCCESS response message now to be sent later in
	 * _x11_con_on_connection().
	 */
	success_rc_msg = xmalloc(sizeof(*success_rc_msg));
	success_rc_msg->return_code = SLURM_SUCCESS;
	slurm_resp_msg_init(x11con->resp_msg, msg, RESPONSE_SLURM_RC,
			    success_rc_msg);

	if ((rc = conmgr_create_connect_socket(CON_TYPE_RAW, CON_FLAG_QUIESCE,
					       &local_x11_server_addr, addrlen,
					       &events, NULL, x11con))) {
		error("Failed to connect to local X11 server at '%pA': %s",
		      &local_x11_server_addr, slurm_strerror(rc));
		/*
		 * Send the error rc to slurmstepd before _x11_con_free() closes
		 * the shared connection, otherwise the queued close may tear it
		 * down before the response is flushed.
		 */
		slurm_send_rc_msg(msg, SLURM_ERROR);
		_x11_con_free(x11con);
		return;
	}

	/*
	 * _x11_con_on_connection() will send SLURM_SUCCESS back, this ensures
	 * that it sends the rc back *before* starting duplex_relay.
	 */
	log_flag(NET, "%s: [%s] Connected to local X11 server, will send SLURM_SUCCESS back to slurmstepd to continue setting up X11 tunnel",
		 __func__, conmgr_con_get_name(con));

	/*
	 * make sure _on_msg() doesn't close the connection as it will stay
	 * alive for the duplex relay.
	 */
	CONMGR_CON_UNLINK(msg->conmgr_con);
}

static alloc_rpc_t alloc_rpcs[] = {
	{
		.msg_type = SRUN_PING,
		.func = _handle_ping,
	},
	{
		.msg_type = SRUN_JOB_COMPLETE,
		.func = _handle_job_complete,
	},
	{
		.msg_type = SRUN_TIMEOUT,
		.func = _handle_timeout,
	},
	{
		.msg_type = SRUN_USER_MSG,
		.func = _handle_user_msg,
	},
	{
		.msg_type = SRUN_NODE_FAIL,
		.func = _handle_node_fail,
	},
	{
		.msg_type = SRUN_REQUEST_SUSPEND,
		.func = _handle_suspend,
	},
	{
		.msg_type = SRUN_NET_FORWARD,
		.func = _handle_net_forward,
	},
	{
		/* terminate the array. this must be last. */
		.msg_type = 0,
		.func = NULL,
	}
};

static void *_on_connection(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *con = conmgr_args.ref;

	log_flag(NET, "%s: [%s] New connection accepted by alloc_msg listener",
		 __func__, conmgr_con_get_name(con));

	return arg;
}

static void _on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *con = conmgr_args.ref;

	log_flag(NET, "%s: [%s] connection finished",
		 __func__, conmgr_con_get_name(con));
}

static int _on_msg(conmgr_callback_args_t conmgr_args, slurm_msg_t *msg,
		   int unpack_rc, void *arg)
{
	slurm_allocation_callbacks_t *callbacks = arg;
	conmgr_fd_ref_t *con = conmgr_args.ref;
	uid_t uid = getuid();
	alloc_rpc_t *this_rpc = NULL;
	int rc = SLURM_SUCCESS;

	if (!msg->auth_ids_set) {
		error("%s: [%s] Security violation, rejecting unauthenticated slurm message",
		      __func__, conmgr_con_get_name(con));
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto end;
	}

	log_flag(AUDIT_RPCS, "allocate_msg _on_msg: [%s] msg_type=%s uid=%u client=[%pA] protocol=%u",
		 conmgr_con_get_name(con), rpc_num2string(msg->msg_type),
		 msg->auth_uid, &msg->address, msg->protocol_version);

	if (unpack_rc) {
		error("%s: [%s] rejecting malformed RPC and closing connection: %s",
		      __func__, conmgr_con_get_name(con),
		      slurm_strerror(unpack_rc));
		rc = unpack_rc;
		goto end;
	}

	if ((msg->auth_uid != slurm_conf.slurm_user_id) &&
	    (msg->auth_uid != 0) && (msg->auth_uid != uid)) {
		error ("Security violation, slurm message from uid %u",
		       msg->auth_uid);
		rc = SLURM_PROTOCOL_AUTHENTICATION_ERROR;
		goto end;
	}

	for (this_rpc = alloc_rpcs; this_rpc->msg_type; this_rpc++) {
		if (this_rpc->msg_type == msg->msg_type)
			break;
	}

	if (!this_rpc->msg_type) {
		error("%s: received spurious message type: %s",
		      __func__, rpc_num2string(msg->msg_type));
		goto end;
	}

	this_rpc->func(callbacks, msg);

end:
	conmgr_con_queue_close(msg->conmgr_con);
	FREE_NULL_MSG(msg);
	return rc;
}

static void *_on_listen_connect(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *con = conmgr_args.ref;

	log_flag(NET, "%s: [%s] alloc_msg listener now open",
		 __func__, conmgr_con_get_name(con));

	return arg;
}

static void _on_listen_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_fd_ref_t *con = conmgr_args.ref;

	log_flag(NET, "%s: [%s] alloc_msg listener connection finished",
		 __func__, conmgr_con_get_name(con));
}

extern int slurm_alloc_msg_listener_create(uint16_t *port,
					   slurm_allocation_callbacks_t
						   *callbacks)
{
	int listen_fd = -1;
	static const conmgr_events_t events = {
		.on_listen_connect = _on_listen_connect,
		.on_listen_finish = _on_listen_finish,
		.on_connection = _on_connection,
		.on_msg = _on_msg,
		.on_finish = _on_finish,
	};
	conmgr_con_flags_t flags = CON_FLAG_NONE;
	int rc = SLURM_SUCCESS;

	if (conn_tls_enabled())
		flags |= CON_FLAG_TLS_FINGERPRINT;

	if (slurm_init_msg_engine_srun_ports(&listen_fd, port) !=
	    SLURM_SUCCESS) {
		fatal("Unable to open listening socket for messages");
	}
	if ((rc = conmgr_process_fd_listen(listen_fd, CON_TYPE_RPC, NULL,
					   &events, flags, callbacks))) {
		fatal("%s: unable to process fd:%d error:%s",
		      __func__, listen_fd, slurm_strerror(rc));
	}

	return rc;
}
