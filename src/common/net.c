/*****************************************************************************\
 *  net.c - basic network communications for user application I/O
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, Kevin Tew <tew1@llnl.gov>, 
 *  et. al.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
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

#include "src/common/log.h"
#include "src/common/net.h"

#ifndef NET_DEFAULT_BACKLOG
#  define NET_DEFAULT_BACKLOG	1024
#endif 

static int _sock_bind_wild(int sockfd)
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
	return (sin.sin_port);
}



int net_stream_listen(int *fd, int *port)
{
	int rc, val;

	if ((*fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return -1;

	val = 1;
	rc = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(int));
	if (rc > 0) 
		goto cleanup;

	*port = _sock_bind_wild(*fd);
	if (*port < 0)
		goto cleanup;
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


int accept_stream(int fd)
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
