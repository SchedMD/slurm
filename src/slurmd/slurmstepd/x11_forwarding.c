/*****************************************************************************\
 *  x11_forwarding.c - setup ssh port forwarding
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <libssh2.h>

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

#define SSH_PORT 22

/*
 * Ideally these would be selected at run time. Unfortunately,
 * only ssh-rsa and ssh-dss are supported by libssh2 at this time,
 * and ssh-dss is deprecated.
 */
static char *hostkey_priv = "/etc/ssh/ssh_host_rsa_key";
static char *hostkey_pub = "/etc/ssh/ssh_host_rsa_key.pub";
static char *priv_format = "%s/.ssh/id_rsa";
static char *pub_format = "%s/.ssh/id_rsa.pub";

static bool local_xauthority = false;
static char *xauthority = NULL;

static int x11_display = 0;

void *_handle_channel(void *x);
void *_keepalive_engine(void *x);
void *_accept_engine(void *x);

/*
 * libssh2 has some quirks with the mixed use of blocking vs. non-blocking
 * operations within each session. Certain calls, such as channel creation
 * and destruction, are best done as blocking operations. But read/write
 * to the individual channels needs to be non-blocking. As the state applies
 * to the entire session, locks are needed to avoid interacting with the
 * channels when the session is temporarily switched into blocking operation.
 * This also avoids multiple threads interacting with their channels
 * concurrently - as all interation is serialized/deserialized into/from
 * a single TCP stream this should not really affect throughput much, as
 * these operations cannot really act concurrently anyways.
 */
pthread_mutex_t ssh_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Used to break out of the keepalive thread.
 */
static bool running = true;

/*
 * Target TCP port on target_host to connect to. Needs to be used in
 * to create each separate channel, so set as a global to avoid having
 * to pass it down through several calls.
 */
static uint16_t x11_target_port;

static int ssh_socket = -1, listen_socket = -1;

static LIBSSH2_SESSION *session = NULL;

typedef struct channel_info {
	LIBSSH2_CHANNEL *channel;
	int socket;
} channel_info_t;

/*
 * Get home directory for a given uid to find their SSH private keys.
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

static void _shutdown_x11(int signal)
{
	if (signal != SIGTERM)
		return;

	debug("x11 forwarding shutdown in progress");

	if (listen_socket)
		close(listen_socket);

	if (session) {
		libssh2_session_disconnect(session,
					   "Disconnecting due to shutdown.");
		libssh2_session_free(session);
	}

	if (ssh_socket)
		close(ssh_socket);

	libssh2_exit();

	if (xauthority) {
		if (local_xauthority && unlink(xauthority))
			error("%s: problem unlinking xauthority file %s: %m",
			      __func__, xauthority);
		else
			x11_delete_xauth(xauthority, conf->hostname, x11_display);

		xfree(xauthority);
	}

	info("x11 forwarding shutdown complete");

	exit(0);
}

/*
 * Bind to a local port and forward to the x11_target_port on
 * x11_target_host. Relies on the user having working password-less SSH
 * pre-shared keys setup in $HOME/.ssh/ that are accepted by x11_target_host.
 *
 * IN: job
 * IN/OUT: display - local X11 display number
 * OUT: SLURM_SUCCESS or SLURM_ERROR
 */
extern int setup_x11_forward(stepd_step_rec_t *job, int *display,
			     char **tmp_xauthority)
{
	int rc, hostauth_failed = 1;
	struct sockaddr_in sin;
	char *home = NULL, *keypub = NULL, *keypriv = NULL;
	char *user_auth_methods;
	uint16_t port;
	/*
	 * Range of ports we'll accept locally. This corresponds to X11
	 * displays of 20 through 99. Intentionally skipping [10 - 19]
	 * as 'ssh -X' will start at 10 and work up from there.
	 */
	uint16_t ports[2] = {6020, 6099};
	int sig_array[2] = {SIGTERM, 0};
	*tmp_xauthority = NULL;
	x11_target_port = job->x11_target_port;

	xsignal(SIGTERM, _shutdown_x11);
	xsignal_unblock(sig_array);

	debug("X11Parameters: %s", conf->x11_params);

	if (!(home = _get_home(job->uid))) {
		error("could not find HOME in environment");
		return SLURM_ERROR;
	}

	keypub = xstrdup_printf(pub_format, home);
	keypriv = xstrdup_printf(priv_format, home);

	if (libssh2_init(0)) {
		error("libssh2 initialization failed");
		return SLURM_ERROR;
	}

	slurm_set_addr(&sin, SSH_PORT, job->x11_target_host);
	if ((ssh_socket = slurm_open_msg_conn(&sin)) == -1) {
		error("Failed to connect to %s port %u.",
		      job->x11_target_host, SSH_PORT);
		return SLURM_ERROR;
	}

	if (!(session = libssh2_session_init())) {
		error("Failed to start SSH session.");
		goto shutdown;
	}

	if ((rc = libssh2_session_handshake(session, ssh_socket))) {
		error("Problem starting SSH session: %d", rc);
		goto shutdown;
	}

	/* skip ssh fingerprint verification */

	user_auth_methods = libssh2_userauth_list(session, job->user_name,
						  strlen(job->user_name));
	debug2("remote accepted auth methods: %s", user_auth_methods);

	/* try hostbased authentication first if available */
	if (strstr(user_auth_methods, "hostbased")) {
		/* returns 0 on success */
		hostauth_failed = libssh2_userauth_hostbased_fromfile(
			session, job->user_name, hostkey_pub,
			hostkey_priv, NULL, conf->node_name);

		if (hostauth_failed) {
			char *err;
			libssh2_session_last_error(session, &err, NULL, 0);
			error("hostkey authentication failed: %s", err);
		} else {
			debug("hostkey authentication successful");
		}
	}

	/*
	 * Switch uid/gid to the user using seteuid/setegid.
	 * DO NOT use setuid/setgid as a user could then attach to this
	 * process and try to recover the ssh hostauth private key from memory.
	 * We do need to switch euid in case the user's ssh keys are
	 * on a root_squash filesystem and inaccessible to root.
	 */
	if (setegid(job->gid)) {
		error("%s: setegid failed: %m", __func__);
		goto shutdown;
	}
	if (setgroups(1, &job->gid)) {
		error("%s: setgroups failed: %m", __func__);
		goto shutdown;
	}
	if (seteuid(job->uid)) {
		error("%s: seteuid failed: %m", __func__);
		goto shutdown;
	}

	/* use a node-local XAUTHORITY file instead of ~/.Xauthority */
	if (xstrcasestr(conf->x11_params, "local_xauthority")) {
		int fd;
		local_xauthority = true;
		xauthority = xstrdup_printf("%s/.Xauthority-XXXXXX",
					    conf->tmpfs);

		/* protect against weak file permissions in old glibc */
		umask(0077);
		if ((fd = mkstemp(xauthority)) == -1) {
			error("%s: failed to create temporary XAUTHORITY file: %m",
			      __func__);
			goto shutdown;
		}
		close(fd);
	} else {
		xauthority = xstrdup_printf("%s/.Xauthority", home);
	}

	xfree(home);

	/*
	 * If hostbased failed or was unavailable, try publickey instead.
	 */
	if (hostauth_failed
	    && libssh2_userauth_publickey_fromfile(session, job->user_name,
						   keypub, keypriv, NULL)) {
		char *err;
		libssh2_session_last_error(session, &err, NULL, 0);
		error("ssh public key authentication failure: %s", err);

		goto shutdown;
	}
	xfree(keypub);
	xfree(keypriv);
	debug("public key auth successful");

	if (net_stream_listen_ports(&listen_socket, &port, ports, true) == -1) {
		error("failed to open local socket");
		goto shutdown;
	}

	x11_display = port - X11_TCP_PORT_OFFSET;
	if (x11_set_xauth(xauthority, job->x11_magic_cookie,
			  conf->hostname, x11_display)) {
		error("%s: failed to run xauth", __func__);
		goto shutdown;
	}

	info("X11 forwarding established on DISPLAY=%s:%d.0",
	     conf->hostname, x11_display);

	/*
	 * Send keepalives every 60 seconds, and have the server
	 * send a reply as well. Since we're running async, a separate
	 * thread will need to handle sending these periodically per
	 * the libssh2 documentation, as the library itself won't manage
	 * this for us.
	 */
	libssh2_keepalive_config(session, 1, 60);

	slurm_thread_create_detached(NULL, _keepalive_engine, NULL);
	slurm_thread_create_detached(NULL, _accept_engine, NULL);

	/*
	 * Connection handling threads are still running. Return now to signal
	 * that the forwarding code setup has completed successfully, and let
	 * steps needing X11 forwarding service launch.
	 */
	*display = x11_display;
	*tmp_xauthority = xstrdup(xauthority);

	return SLURM_SUCCESS;

shutdown:
	xfree(keypub);
	xfree(keypriv);
	xfree(xauthority);
	close(listen_socket);
	libssh2_session_disconnect(session, "Disconnecting due to error.");
	libssh2_session_free(session);
	close(ssh_socket);
	libssh2_exit();

	return SLURM_ERROR;
}

void *_keepalive_engine(void *x)
{
	int delay;

	while (running) {
		debug("x11 forwarding - sending keepalive message");
		slurm_mutex_lock(&ssh_lock);
		libssh2_keepalive_send(session, &delay);
		slurm_mutex_unlock(&ssh_lock);
		sleep(delay);
	}

	debug2("exiting %s", __func__);
	return NULL;
}

void *_accept_engine(void *x)
{
	if (listen(listen_socket, 2) == -1) {
		error("listening socket returned an error: %m");
		goto shutdown;
	}

	while (true) {
		slurm_addr_t sin;
		channel_info_t *ci = xmalloc(sizeof(channel_info_t));

		if ((ci->socket = slurm_accept_msg_conn(listen_socket, &sin))
		    == -1) {
			xfree(ci);
			error("accept call failure, shutting down");
			goto shutdown;
		}

		/* libssh2_channel_direct_tcpip needs blocking I/O */
		slurm_mutex_lock(&ssh_lock);
		libssh2_session_set_blocking(session, 1);
		if (!(ci->channel = libssh2_channel_direct_tcpip(session,
								 "localhost",
								 x11_target_port))) {
			char *ssh_error = NULL;
			libssh2_session_last_error(session, &ssh_error, NULL, 0);
			libssh2_session_set_blocking(session, 0);
			slurm_mutex_unlock(&ssh_lock);
			error("broken channel call: %s", ssh_error);
			close(ci->socket);
			xfree(ci);
		} else {
			libssh2_session_set_blocking(session, 0);
			slurm_mutex_unlock(&ssh_lock);

			slurm_thread_create_detached(NULL, _handle_channel, ci);
		}
	}

shutdown:
	debug2("exiting %s", __func__);
	running = false;
	close(listen_socket);
	libssh2_session_disconnect(session, "Client disconnecting normally");
	libssh2_session_free(session);
	close(ssh_socket);
	libssh2_exit();

	return NULL;
}

/*
 * Handle forwarding for an individual local connect and SSH channel.
 * Use poll() to sleep until needed.
 */
void *_handle_channel(void *x) {
	channel_info_t *ci = (channel_info_t *) x;
	int i, rc;
	char buf[16384];
	ssize_t len, wr;
	struct pollfd fds[2];

	/*
	 * Since libssh2 multiplexes channels onto a single socket, there is no
	 * way to poll only our individual channel. Instead, poll on the SSH
	 * connection socket, and deal with being woken up even if no data is
	 * present on our channel. In such an instance, we'll run through the
	 * loop once then go back to blocking on the poll() call.
	 */
	fds[0].fd = ssh_socket;
	fds[0].events = POLLIN | POLLOUT;
	fds[1].fd = ci->socket;
	fds[1].events = POLLIN;

	while (true) {
		if ((rc = poll(fds, 2, 10000)) == -1) {
			error("%s: poll returned %d, %m", __func__, rc);
			goto shutdown;
		}
		/*
		 * read on socket is blocking,
		 * so make sure it has data ready for us
		 */
		if (rc && (fds[1].revents & POLLIN)) {
			len = recv(ci->socket, buf, sizeof(buf), 0);
			if (len < 0) {
				error("%s: failed to read on inbound socket",
				      __func__);
				goto shutdown;
			} else if (0 == len) {
				error("%s: client disconnected", __func__);
				goto shutdown;
			}
			wr = 0;
			while(wr < len) {
				slurm_mutex_lock(&ssh_lock);
				i = libssh2_channel_write(ci->channel,
							  buf + wr, len - wr);
				slurm_mutex_unlock(&ssh_lock);
				if (LIBSSH2_ERROR_EAGAIN == i) {
					continue;
				}
				if (i < 0) {
					error("%s: libssh2_channel_write: %d\n",
					      __func__, i);
					goto shutdown;
				}
				wr += i;
			}
		}
		while (true) {
			slurm_mutex_lock(&ssh_lock);
			len = libssh2_channel_read(ci->channel, buf, sizeof(buf));
			slurm_mutex_unlock(&ssh_lock);
			if (len == LIBSSH2_ERROR_EAGAIN)
				break;
			else if (len < 0) {
				error("%s: libssh2_channel_read: %d",
				      __func__, (int)len);
				goto shutdown;
			}
			wr = 0;
			while (wr < len) {
				i = send(ci->socket, buf + wr, len - wr, 0);
				if (i <= 0) {
					error("%s: write failed", __func__);
					goto shutdown;
				}
				wr += i;
			}

			slurm_mutex_lock(&ssh_lock);
			if (libssh2_channel_eof(ci->channel)) {
				slurm_mutex_unlock(&ssh_lock);
				error("%s: remote disconnected", __func__);
				goto shutdown;
			}
			slurm_mutex_unlock(&ssh_lock);
		}

	}

shutdown:
	close(ci->socket);
	slurm_mutex_lock(&ssh_lock);
	libssh2_channel_close(ci->channel);
	slurm_mutex_unlock(&ssh_lock);
	error("%s: exiting thread", __func__);
	xfree(ci);
	return NULL;
}
