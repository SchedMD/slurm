
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

#include <src/common/log.h>
#include "net.h"

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

	if ((*fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
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
	int n;
	int pbuf = (int)buf;
	size_t nleft = nbytes;

	while (nleft > 0) {
		if ((n = read(fd, (void *)pbuf, nleft)) < 0 && (errno != EINTR)) {
			/* eof */
			return(0);
		}
		pbuf+=n;
		nleft-=n;
	}
	return(n);
}

