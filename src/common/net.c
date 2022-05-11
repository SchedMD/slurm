/*****************************************************************************\
 *  net.c - basic network communications for user application I/O
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, Kevin Tew <tew1@llnl.gov>,
 *  et. al.
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "src/common/read_config.h"

#if defined(__FreeBSD__) || defined(__NetBSD__)
#define	SOL_TCP		IPPROTO_TCP
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif /* NI_MAXHOST */
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif /* NI_MAXSERV */

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/net.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(net_stream_listen,		slurm_net_stream_listen);

/* open a stream socket on an ephemereal port and put it into
 * the listen state. fd and port are filled in with the new
 * socket's file descriptor and port #.
 *
 * OUT fd - listening socket file descriptor number
 * OUT port - TCP port number in host byte order
 */
int net_stream_listen(int *fd, uint16_t *port)
{
	slurm_addr_t sin;
	socklen_t len = sizeof(sin);
	int val = 1;

	/* bind ephemeral port */
	slurm_setup_addr(&sin, 0);

	if ((*fd = socket(sin.ss_family, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return -1;

	if (setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0)
		goto cleanup;
	if (bind(*fd, (struct sockaddr *) &sin, len) < 0)
		goto cleanup;
	if (getsockname(*fd, (struct sockaddr *) &sin, &len) < 0)
		goto cleanup;

	*port = slurm_get_port(&sin);
	if (listen(*fd, SLURM_DEFAULT_LISTEN_BACKLOG) < 0)
		goto cleanup;

	return 1;

cleanup:
	close(*fd);
	return -1;
}

/* set keepalive time on socket */
extern int net_set_keep_alive(int sock)
{
	int opt_int;
	socklen_t opt_len;
	struct linger opt_linger;

	if (slurm_conf.keepalive_time == NO_VAL)
		return 0;

	opt_len = sizeof(struct linger);
	opt_linger.l_onoff = 1;
	opt_linger.l_linger = slurm_conf.keepalive_time;
	if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &opt_linger, opt_len) < 0)
		error("Unable to set linger socket option: %m");

	opt_len = sizeof(opt_int);
	opt_int = slurm_conf.keepalive_time;
	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &opt_int, opt_len) < 0) {
		error("Unable to set keepalive socket option: %m");
		return -1;
	}

/*
 * TCP_KEEPIDLE used to be defined in FreeBSD, then went away, then came
 * back in 9.0.
 *
 * Removing this call might decrease the robustness of communications,
 * but will probably have no noticable effect.
 */
#if !defined (__APPLE__) && (! defined(__FreeBSD__) || (__FreeBSD_version > 900000))
	if (slurm_conf.keepalive_interval != NO_VAL) {
		opt_int = slurm_conf.keepalive_interval;
		if (setsockopt(sock, SOL_TCP, TCP_KEEPINTVL,
			       &opt_int, opt_len) < 0) {
			error("Unable to set keepalive interval: %m");
			return -1;
		}
	}
	if (slurm_conf.keepalive_probes != NO_VAL) {
		opt_int = (int) slurm_conf.keepalive_probes;
		if (setsockopt(sock, SOL_TCP, TCP_KEEPCNT,
			       &opt_int, opt_len) < 0) {
			error("Unable to set keepalive probes: %m");
			return -1;
		}
	}
	opt_int = slurm_conf.keepalive_time;
	if (setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, &opt_int, opt_len) < 0) {
		error("Unable to set keepalive socket time: %m");
		return -1;
	}
#endif

#if 0
	/* Used to validate above operations for testing purposes */
	opt_linger.l_onoff = 0;
	opt_linger.l_linger = 0;
	opt_len = sizeof(struct linger);
	getsockopt(sock, SOL_SOCKET, SO_LINGER, &opt_linger, &opt_len);
	info("got linger time of %d:%d on fd %d", opt_linger.l_onoff,
	     opt_linger.l_linger, sock);

	opt_len = sizeof(opt_len);
	getsockopt(sock, SOL_TCP, TCP_KEEPINTVL, &opt_int, &opt_len);
	info("got keepalive_interval is %d on fd %d", opt_int, sock);
	getsockopt(sock, SOL_TCP, TCP_KEEPCNT, &opt_int, &opt_len);
	info("got keepalive_probes is %d on fd %d", opt_int, sock);
	getsockopt(sock, SOL_TCP, TCP_KEEPIDLE, &opt_int, &opt_len);
	info("got keepalive_time is %d on fd %d", opt_int, sock);
#endif

	return 0;
}

/* net_stream_listen_ports()
 */
int net_stream_listen_ports(int *fd, uint16_t *port, uint16_t *ports, bool local)
{
	slurm_addr_t sin;
	int cc;
	int val;

	slurm_setup_addr(&sin, 0); /* Decide on IPv4 or IPv6 */

	if ((*fd = socket(sin.ss_family, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return -1;

	val = 1;
	cc = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (cc < 0) {
		close(*fd);
		return -1;
	}

	if ((cc = sock_bind_listen_range(*fd, ports, local)) < 0)
		return -1;
	*port = cc;

	return *fd;
}

extern char *sockaddr_to_string(const slurm_addr_t *addr, socklen_t addrlen)
{
	int rc, prev_errno = errno;
	char *resp = NULL;
	char host[NI_MAXHOST] = { 0 };
	char serv[NI_MAXSERV] = { 0 };

	if (addr->ss_family == AF_UNIX) {
		const struct sockaddr_un *addr_un =
			(const struct sockaddr_un *) addr;

		/* path may not be set */
		if (addr_un->sun_path[0])
			return xstrdup_printf("unix:%s", addr_un->sun_path);
		else
			return NULL;
	}

	resp = xmalloc(NI_MAXHOST + NI_MAXSERV);
	rc = getnameinfo((const struct sockaddr *) addr, addrlen, host,
			 NI_MAXHOST, serv, NI_MAXSERV, NI_NUMERICSERV);
	if (rc == EAI_SYSTEM) {
		error("Unable to get address: %m");
	} else if (rc) {
		error("Unable to get address: %s", gai_strerror(rc));
	} else {
		/* construct RFC3986 host port pair */
		if (host[0] != '\0' && serv[0] != '\0')
			xstrfmtcat(resp, "[%s]:%s", host, serv);
		else if (serv[0] != '\0')
			xstrfmtcat(resp, "[::]:%s", serv);
	}

	/*
	 * Avoid clobbering errno as this function is likely to be used for
	 * error logging, and stepping on errno prevents %m from working.
	 */
	errno = prev_errno;
	return resp;
}

extern char *addrinfo_to_string(const struct addrinfo *addr)
{
	return sockaddr_to_string((const slurm_addr_t *) addr->ai_addr,
				  addr->ai_addrlen);
}
