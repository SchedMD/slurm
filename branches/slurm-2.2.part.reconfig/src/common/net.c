/*****************************************************************************\
 *  net.c - basic network communications for user application I/O
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, Kevin Tew <tew1@llnl.gov>,
 *  et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "src/common/macros.h"
#include "src/common/log.h"
#include "src/common/net.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(net_stream_listen,		slurm_net_stream_listen);
strong_alias(net_accept_stream,		slurm_net_accept_stream);
strong_alias(net_set_low_water,		slurm_net_set_low_water);

#ifndef NET_DEFAULT_BACKLOG
#  define NET_DEFAULT_BACKLOG	1024
#endif

/*
 * Returns the port number in host byte order.
 */
static short _sock_bind_wild(int sockfd)
{
	socklen_t len;
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(0);	/* bind ephemeral port */

	if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
		return (-1);
	len = sizeof(sin);
	if (getsockname(sockfd, (struct sockaddr *) &sin, &len) < 0)
		return (-1);
	return ntohs(sin.sin_port);
}

/* open a stream socket on an ephemereal port and put it into
 * the listen state. fd and port are filled in with the new
 * socket's file descriptor and port #.
 *
 * OUT fd - listening socket file descriptor number
 * OUT port - TCP port number in host byte order
 */
int net_stream_listen(int *fd, short *port)
{
	int rc, val;

	if ((*fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return -1;

	val = 1;
	rc = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (rc > 0)
		goto cleanup;

	*port = _sock_bind_wild(*fd);
#undef SOMAXCONN
#define SOMAXCONN	1024
	rc = listen(*fd, NET_DEFAULT_BACKLOG);
	if (rc < 0)
		goto cleanup;

	return 1;

  cleanup:
	close(*fd);
	return -1;
}


int net_accept_stream(int fd)
{
	int sd;

	while ((sd = accept(fd, NULL, NULL)) < 0) {
		if (errno == EINTR)
			continue;
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			return -1;
		if (errno == ECONNABORTED)
			return -1;
		error("Unable to accept new connection");
	}

	return sd;
}

int readn(int fd, void *buf, size_t nbytes)
{
	int n = 0;
	char *pbuf = (char *)buf;
	size_t nleft = nbytes;

	while (nleft > 0) {
		n = read(fd, (void *)pbuf, nleft);
		if (n > 0) {
			pbuf+=n;
			nleft-=n;
		} else if (n == 0) 	/* EOF */
			break;
		else if (errno == EINTR)
			continue;
		else {
			debug("read error: %m");
			break;
		}
	}
	return(n);
}

int net_set_low_water(int sock, size_t size)
{
	if (setsockopt(sock, SOL_SOCKET, SO_RCVLOWAT,
	  (const void *) &size, sizeof(size)) < 0) {
		error("Unable to set low water socket option: %m");
		return -1;
	}

	return 0;
}
