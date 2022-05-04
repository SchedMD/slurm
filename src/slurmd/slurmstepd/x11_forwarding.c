/*****************************************************************************\
 *  x11_forwarding.c - setup x11 port forwarding
 *****************************************************************************
 *  Copyright (C) 2017-2019 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include "src/common/eio.h"
#include "src/common/half_duplex.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/uid.h"
#include "src/common/x11_util.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

static uint32_t job_id = NO_VAL;

static bool local_xauthority = false;
static char hostname[256] = {0};

static eio_handle_t *eio_handle;

/* Target salloc/srun host/port */
static slurm_addr_t alloc_node;
/* Target UID */
static uid_t job_uid;
/* X11 display hostname on target, or UNIX socket. */
static char *x11_target = NULL;
/* X11 display port on target (if not a UNIX socket). */
static uint16_t x11_target_port = 0;

static void *_eio_thread(void *arg)
{
	eio_handle_mainloop(eio_handle);
	return NULL;
}

static bool _x11_socket_readable(eio_obj_t *obj)
{
        if (obj->shutdown) {
		if (obj->fd != -1)
			close(obj->fd);
		obj->fd = -1;
                return false;
	}
        return true;
}

static int _x11_socket_read(eio_obj_t *obj, List objs)
{
	eio_obj_t *e1, *e2;
	slurm_msg_t req, resp;
	net_forward_msg_t rpc;
	slurm_addr_t sin;
	int *local, *remote;
	int rc;

	local = xmalloc(sizeof(*local));
	remote = xmalloc(sizeof(*remote));

	if ((*local = slurm_accept_msg_conn(obj->fd, &sin)) == -1) {
		error("accept call failure, shutting down");
		goto shutdown;
	}

	*remote = slurm_open_msg_conn(&alloc_node);
	if (*remote < 0) {
		error("%s: slurm_open_msg_conn: %m", __func__);
		goto shutdown;
	}

	rpc.job_id = job_id;
	rpc.flags = 0;
	rpc.port = x11_target_port;
	rpc.target = x11_target;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);

	req.msg_type = SRUN_NET_FORWARD;
	slurm_msg_set_r_uid(&req, job_uid);
	req.data = &rpc;

	slurm_send_recv_msg(*remote, &req, &resp, 0);

	if (resp.msg_type != RESPONSE_SLURM_RC) {
		error("Unexpected response on setup, forwarding failed.");
		slurm_free_msg_members(&resp);
		goto shutdown;
	}

	if ((rc = slurm_get_return_code(resp.msg_type, resp.data))) {
		error("Error setting up X11 forwarding from remote: %s",
		      slurm_strerror(rc));
		slurm_free_msg_members(&resp);
		goto shutdown;
	}

	slurm_free_msg_members(&resp);

	/* setup eio to handle both sides of the connection now */
	e1 = eio_obj_create(*local, &half_duplex_ops, remote);
	e2 = eio_obj_create(*remote, &half_duplex_ops, local);
	eio_new_obj(eio_handle, e1);
	eio_new_obj(eio_handle, e2);

	debug("%s: X11 forwarding setup successful", __func__);

	return SLURM_SUCCESS;

shutdown:
	debug2("%s: error, shutting down", __func__);
	if (*local != -1)
		close(*local);
	xfree(local);
	xfree(remote);

	return SLURM_ERROR;
}

/*
 * Get home directory for a given uid.
 *
 * IN: uid
 * OUT: an xmalloc'd string, or NULL on error.
 */
static char *_get_home(uid_t uid)
{
	struct passwd pwd, *pwd_ptr = NULL;
	char pwd_buf[PW_BUF_SIZE];

	if (slurm_getpwuid_r(uid, &pwd, pwd_buf, PW_BUF_SIZE, &pwd_ptr)
	    || (pwd_ptr == NULL)) {
		error("%s: getpwuid_r(%u):%m", __func__, uid);
		return NULL;
	}

	return xstrdup(pwd.pw_dir);
}

extern int shutdown_x11_forward(stepd_step_rec_t *job)
{
	debug("x11 forwarding shutdown in progress");
	eio_signal_shutdown(eio_handle);

	if (job->x11_xauthority) {
		if (local_xauthority) {
			if (unlink(job->x11_xauthority))
				error("%s: problem unlinking xauthority file %s: %m",
				      __func__, job->x11_xauthority);
		} else
			x11_delete_xauth(job->x11_xauthority, hostname,
					 job->x11_display);
	}

	info("x11 forwarding shutdown complete");
	_exit(0);
}

/*
 * Bind to a local port for X11 connections. Each connection will setup a
 * separate tunnel through the remote salloc/srun process.
 *
 * IN: job
 * OUT: SLURM_SUCCESS or SLURM_ERROR
 */
extern int setup_x11_forward(stepd_step_rec_t *job)
{
	int listen_socket = -1;
	uint16_t port;
	/*
	 * Range of ports we'll accept locally. This corresponds to X11
	 * displays of 20 through 99. Intentionally skipping [10 - 19]
	 * as 'ssh -X' will start at 10 and work up from there.
	 */
	uint16_t ports[2] = {6020, 6099};

	/*
	 * EIO handles both the local listening socket, as well as the individual
	 * forwarded connections.
	 */
	eio_obj_t *obj;
	static struct io_operations x11_socket_ops = {
		.readable = _x11_socket_readable,
		.handle_read = _x11_socket_read,
	};

	job_id = job->step_id.job_id;
	x11_target = xstrdup(job->x11_target);
	x11_target_port = job->x11_target_port;

	slurm_set_addr(&alloc_node, job->x11_alloc_port, job->x11_alloc_host);
	job_uid = job->uid;

	debug("X11Parameters: %s", slurm_conf.x11_params);

	if (xstrcasestr(slurm_conf.x11_params, "home_xauthority")) {
		char *home = NULL;
		if (!(home = _get_home(job->uid))) {
			error("could not find HOME in environment");
			goto shutdown;
		}
		job->x11_xauthority = xstrdup_printf("%s/.Xauthority", home);
		xfree(home);
	} else {
		/* use a node-local XAUTHORITY file instead of ~/.Xauthority */
		int fd;
		local_xauthority = true;
		job->x11_xauthority = xstrdup_printf("%s/.Xauthority-XXXXXX",
						     slurm_conf.tmp_fs);

		/* protect against weak file permissions in old glibc */
		umask(0077);
		if ((fd = mkstemp(job->x11_xauthority)) == -1) {
			error("%s: failed to create temporary XAUTHORITY file: %m",
			      __func__);
			goto shutdown;
		}
		close(fd);
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

	job->x11_display = port - X11_TCP_PORT_OFFSET;
	if (x11_set_xauth(job->x11_xauthority, job->x11_magic_cookie,
			  hostname, job->x11_display)) {
		error("%s: failed to run xauth", __func__);
		goto shutdown;
	}

	info("X11 forwarding established on DISPLAY=%s:%d.0",
	     hostname, job->x11_display);

	eio_handle = eio_handle_create(0);
	obj = eio_obj_create(listen_socket, &x11_socket_ops, NULL);
	eio_new_initial_obj(eio_handle, obj);
	slurm_thread_create_detached(NULL, _eio_thread, NULL);

	return SLURM_SUCCESS;

shutdown:
	xfree(x11_target);
	job->x11_display = 0;
	xfree(job->x11_xauthority);
	if (listen_socket != -1)
		close(listen_socket);

	return SLURM_ERROR;
}
