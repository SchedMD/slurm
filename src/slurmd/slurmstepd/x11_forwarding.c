/*****************************************************************************\
 *  x11_forwarding.c - setup x11 port forwarding
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

#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <grp.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"

#include "src/common/duplex_relay.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/uid.h"
#include "src/common/x11_util.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * This file implements Slurm's built-in X11 forwarding on the slurmstepd side.
 *
 * Typical X11 environment
 * - X11 client <-> X11 server
 *
 * Slurm's builtin X11 forwarding
 * - X11 client <-> slurmstepd <-> srun <-> X11 server
 *
 * Setup Sequence:
 * 1. X11 client in user app connects to slurmstepd's "X11 server"
 * 2. slurmstepd connects to srun and sends SRUN_NET_FORWARD message
 * 3. srun connects to local X11 server
 *
 * Data exchange:
 *   X11 client writing to X11 server:
 *   - X11 client (compute host) -> slurmstepd -> srun -> X11 server (srun host)
 *
 *   X11 server writing to X11 client:
 *   - X11 server (srun host) -> srun -> slurmstepd -> X11 client (compute host)
 */

static uint32_t job_id = NO_VAL;
static uid_t job_uid;

static bool local_xauthority = false;
static char hostname[HOST_NAME_MAX] = {0};

/* Target salloc/srun host/port */
static slurm_addr_t alloc_node;
/* X11 display hostname on target, or UNIX socket. */
static char *x11_target = NULL;
/* X11 display port on target (if not a UNIX socket). */
static uint16_t x11_target_port = 0;
static char *srun_tls_cert = NULL;

static uint16_t protocol_version = SLURM_PROTOCOL_VERSION;

#define MAGIC_X11_CON 0xba59504c

typedef struct {
	int magic; /* MAGIC_X11_CON */
	conmgr_fd_ref_t *con;
} x11_con_t;

static void _x11_con_free(x11_con_t *x11con)
{
	if (!x11con)
		return;

	xassert(x11con->magic == MAGIC_X11_CON);
	x11con->magic = ~MAGIC_X11_CON;
	conmgr_con_queue_close_free(&x11con->con);
	xfree(x11con);
}

static void *_x11_con_on_connection(conmgr_callback_args_t conmgr_args,
				    void *arg)
{
	x11_con_t *x11con = arg;
	conmgr_fd_ref_t *con = conmgr_args.ref;
	int rc = EINVAL;
	slurm_msg_t req;
	net_forward_msg_t rpc;

	xassert(x11con->magic == MAGIC_X11_CON);

	log_flag(NET, "%s: [%s] Connected to srun, will send SRUN_NET_FORWARD to continue setting up X11 tunnel",
		 __func__, conmgr_con_get_name(con));

	rpc.step_id.job_id = job_id;
	rpc.flags = 0;
	rpc.port = x11_target_port;
	rpc.target = x11_target;

	slurm_msg_t_init(&req);

	req.msg_type = SRUN_NET_FORWARD;
	req.protocol_version = protocol_version;
	slurm_msg_set_r_uid(&req, job_uid);
	req.data = &rpc;

	if (!(rc = conmgr_con_queue_write_msg(con, &req)))
		return x11con;

	error("%s: [%s] Failed to write SRUN_NET_FORWARD rpc: %s",
	      __func__, conmgr_con_get_name(con), slurm_strerror(rc));
	_x11_con_free(x11con);
	return NULL;
}

static int _x11_con_on_msg(conmgr_callback_args_t conmgr_args, slurm_msg_t *msg,
			   int unpack_rc, void *arg)
{
	x11_con_t *x11con = arg;
	conmgr_fd_ref_t *con = conmgr_args.ref;
	int rc = SLURM_ERROR;
	int msg_rc;

	xassert(x11con->magic == MAGIC_X11_CON);

	/* Ensure that peer is ready to start x11 tunnel */

	if (msg->msg_type != RESPONSE_SLURM_RC) {
		error("%s: [%s] Unexpected response type %s. Unable to start x11 forwarding.",
		      __func__, conmgr_con_get_name(con),
		      rpc_num2string(msg->msg_type));
		rc = SLURM_UNEXPECTED_MSG_ERROR;
	} else if ((msg_rc = slurm_get_return_code(msg->msg_type, msg->data))) {
		error("%s: [%s] Error setting up X11 forwarding from remote: %s",
		      __func__, conmgr_con_get_name(con),
		      slurm_strerror(msg_rc));
		rc = ESLURM_X11_NOT_AVAIL;
	} else if ((rc = duplex_relay_assign(con, x11con->con))) {
		error("%s: [%s] Failed to initialize second connection in duplex relay ",
		      __func__, conmgr_con_get_name(con));
	} else {
		log_flag(NET, "%s: [%s] srun responded to SRUN_NET_FORWARD successfully, X11 tunnel is ready now",
			 __func__, conmgr_con_get_name(con));

		/* Cleanup state but avoid closing the connections */
		CONMGR_CON_UNLINK(x11con->con);
		_x11_con_free(x11con);
	}

	FREE_NULL_MSG(msg);
	return rc;
}

static void _x11_con_on_finish(conmgr_callback_args_t conmgr_args, void *arg)
{
	x11_con_t *x11con = arg;

	xassert(x11con->magic == MAGIC_X11_CON);

	_x11_con_free(x11con);
}

static void *_x11_server_on_connection(conmgr_callback_args_t conmgr_args,
				       void *arg)
{
	static const conmgr_events_t events = {
		.on_connection = _x11_con_on_connection,
		.on_msg = _x11_con_on_msg,
		.on_finish = _x11_con_on_finish,
	};
	conmgr_fd_ref_t *con = conmgr_args.ref;
	int rc = EINVAL;
	x11_con_t *x11con = NULL;

	log_flag(NET, "%s: [%s] User application X11 client connected to our fake X11 server, setting up X11 tunnel now",
		 __func__, conmgr_con_get_name(con));

	x11con = xmalloc(sizeof(*x11con));
	*x11con = (x11_con_t) {
		.magic = MAGIC_X11_CON,
	};

	CONMGR_CON_LINK(con, x11con->con);

	/*
	 * Setup forwarding tunnel for srun. All data exchanged by x11 client
	 * (user application) with slurmstepd's "x11 server" on the compute host
	 * will be forwarded to the host running srun, as if the application
	 * were running on the host running srun.
	 */
	if ((rc = conmgr_create_connect_socket(CON_TYPE_RPC, CON_FLAG_NONE,
					       &alloc_node, sizeof(alloc_node),
					       &events, srun_tls_cert,
					       x11con))) {
		error("%s: [%s] Failed to connect to srun at %pA: %s",
		      __func__, conmgr_con_get_name(con), &alloc_node,
		      slurm_strerror(rc));
	} else if ((rc = conmgr_quiesce_con(con))) {
		error("%s: [%s] Failed to quiesce connection: %s",
		      __func__, conmgr_con_get_name(con), slurm_strerror(rc));
	} else {
		log_flag(NET, "%s: [%s] Waiting for tunnel to srun at %pA connect",
			 __func__, conmgr_con_get_name(con), &alloc_node);
		return con;
	}

	_x11_con_free(x11con);
	return NULL;
}

static int _x11_server_on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	fatal_abort("should never happen");
}

extern int shutdown_x11_forward(void)
{
	int rc = SLURM_SUCCESS;

	debug("x11 forwarding shutdown in progress");

	if (step->x11_xauthority) {
		if (local_xauthority) {
			if (unlink(step->x11_xauthority)) {
				error("%s: problem unlinking xauthority file %s: %m",
				      __func__, step->x11_xauthority);
				rc = SLURM_ERROR;
			}
		} else
			rc = x11_delete_xauth(step->x11_xauthority, hostname,
					      step->x11_display);
	}

	info("x11 forwarding shutdown complete");
	return rc;
}

/*
 * Bind to a local port for X11 connections. Each connection will setup a
 * separate tunnel through the remote salloc/srun process.
 *
 * IN: job
 * OUT: SLURM_SUCCESS or SLURM_ERROR
 */
extern int setup_x11_forward(void)
{
	static const conmgr_events_t events = {
		.on_connection = _x11_server_on_connection,
		.on_data = _x11_server_on_data,
	};
	int rc = EINVAL;

	int listen_socket = -1;
	uint16_t port;
	/*
	 * Range of ports we'll accept locally. This corresponds to X11
	 * displays of 20 through 99. Intentionally skipping [10 - 19]
	 * as 'ssh -X' will start at 10 and work up from there.
	 */
	uint16_t ports[2] = {6020, 6099};

	srun_info_t *srun = list_peek(step->sruns);
	/* This should always be set to something else we have a bug. */
	xassert(srun && srun->protocol_version);
	protocol_version = srun->protocol_version;

	job_id = step->step_id.job_id;
	job_uid = step->uid;
	x11_target = xstrdup(step->x11_target);
	x11_target_port = step->x11_target_port;
	srun_tls_cert = xstrdup(srun->tls_cert);

	slurm_set_addr(&alloc_node, step->x11_alloc_port, step->x11_alloc_host);

	debug("X11Parameters: %s", slurm_conf.x11_params);

	if (xstrcasestr(slurm_conf.x11_params, "home_xauthority")) {
		char *home = xstrdup(step->pw_dir);
		if (!home && !(home = uid_to_dir(step->uid))) {
			error("Could not look up user home directory");
			goto shutdown;
		}
		step->x11_xauthority = xstrdup_printf("%s/.Xauthority", home);
		xfree(home);
	} else {
		/* use a node-local XAUTHORITY file instead of ~/.Xauthority */
		local_xauthority = true;
		step->x11_xauthority = slurm_get_tmp_fs(conf->node_name);
		xstrcat(step->x11_xauthority, "/.Xauthority-XXXXXX");
	}

	/*
	 * Slurm uses the shortened hostname by default (and discards any
	 * domain component), which can cause problems for some sites.
	 * So retrieve the raw value from gethostname() again.
	 */
	if (gethostname(hostname, sizeof(hostname)))
		fatal("%s: gethostname failed: %m", __func__);

	if (net_stream_listen_ports(&listen_socket, &port, ports, true) == -1) {
		error("failed to open local socket");
		goto shutdown;
	}

	step->x11_display = port - X11_TCP_PORT_OFFSET;

	info("X11 forwarding established on DISPLAY=%s:%d.0",
	     hostname, step->x11_display);

	if ((rc = conmgr_process_fd_listen(listen_socket, CON_TYPE_RAW, NULL,
					   &events, CON_FLAG_TCP_NODELAY,
					   NULL))) {
		error("%s: [fd:%d] Unable to process listening socket: %s",
		      __func__, listen_socket, slurm_strerror(rc));
		goto shutdown;
	}

	return SLURM_SUCCESS;

shutdown:
	xfree(x11_target);
	step->x11_display = 0;
	xfree(step->x11_xauthority);
	xfree(srun_tls_cert);
	if (listen_socket != -1)
		close(listen_socket);

	return SLURM_ERROR;
}
