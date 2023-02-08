/*****************************************************************************\
 *  rpc.c - rpc handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "scrun.h"

extern int send_rpc(slurm_msg_t *msg, slurm_msg_t **ptr_resp, const char *id,
		    int *conn_fd)
{
	int rc;
	slurm_msg_t *resp_msg = NULL;
	int fd = conn_fd ? *conn_fd : -1;
	const char *sock = state.anchor_socket;

	/*
	 * TODO: better handling of start request which can take potentially
	 * forever.
	 */
	slurm_conf.msg_timeout = 500;

	xassert(ptr_resp && !*ptr_resp);
	xassert(!msg->conn);

	if (fd == -1) {
		struct sockaddr_un addr = {
			.sun_family = AF_UNIX,
		};

		if (strlcpy(addr.sun_path, sock, sizeof(addr.sun_path)) !=
		    strlen(sock)) {
			debug("Unable to copy socket path: %s", sock);
			rc = ESLURMD_INVALID_SOCKET_NAME_LEN;
			goto cleanup;
		}
		if ((fd = socket(AF_UNIX, (SOCK_STREAM|SOCK_CLOEXEC), 0)) == -1) {
			rc = errno;
			debug("Unable to create socket: %m");
			goto cleanup;
		}
		if ((connect(fd, (struct sockaddr*) &addr, sizeof(addr))) < 0) {
			rc = errno;
			debug("Unable to connect to socket %s: %m", sock);
			goto cleanup;
		}
	}

	fd_set_blocking(fd);
	fd_set_close_on_exec(fd);

	if ((rc = slurm_send_node_msg(fd, msg)) == -1) {
		/* capture real error */
		rc = errno;

		debug("%s: unable to send RPC to socket %s: %s",
		      __func__, sock, slurm_strerror(rc));

		goto cleanup;
	} else {
		log_flag(NET, "%s: sent %d bytes %s RPC to socket %s",
			 __func__, rc, rpc_num2string(msg->msg_type), sock);
	}

	if (read(fd, NULL, 0) == -1) {
		rc = errno;
		debug("%s: connection failed while waiting for response to RPC on socket %s: %s",
		      __func__, sock, slurm_strerror(rc));
		goto cleanup;
	}

	resp_msg = xmalloc(sizeof(*resp_msg));
	slurm_msg_t_init(resp_msg);

	wait_fd_readable(fd, slurm_conf.msg_timeout);

	if ((rc = slurm_receive_msg(fd, resp_msg, INFINITE))) {
		/* capture real error */
		rc = errno;

		debug("%s: unable to receive RPC response from socket %s: %s",
		      __func__, sock, slurm_strerror(rc));

		goto cleanup;
	}

	log_flag(NET, "%s: received %s RPC from socket %s",
		 __func__, rpc_num2string(resp_msg->msg_type), sock);

	if (!rc && conn_fd)
		*conn_fd = fd;

cleanup:
	if (rc) {
		slurm_free_msg(resp_msg);
		resp_msg = NULL;
	} else {
		*ptr_resp = resp_msg;
	}

	if (!conn_fd && close(fd)) {
		if (!rc)
			rc = errno;

		debug("%s: unable to close RPC socket %s: %m",
		      __func__, sock);
	}

	return rc;
}
