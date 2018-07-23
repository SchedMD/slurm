/*****************************************************************************\
 *  slurm_protocol_socket_implementation.c - slurm communications interfaces
 *					     based upon sockets.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
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
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/log.h"
#include "src/common/fd.h"
#include "src/common/strlcpy.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/util-net.h"

#define PORT_RETRIES    3
#define MIN_USER_PORT   (IPPORT_RESERVED + 1)
#define MAX_USER_PORT   0xffff
#define RANDOM_USER_PORT ((uint16_t) ((lrand48() % \
		(MAX_USER_PORT - MIN_USER_PORT + 1)) + MIN_USER_PORT))

/*
 *  Maximum message size. Messages larger than this value (in bytes)
 *  will not be received.
 */
#define MAX_MSG_SIZE     (1024*1024*1024)


/* Static functions */
static int _slurm_connect(int __fd, struct sockaddr const * __addr,
			  socklen_t __len);

/****************************************************************
 * MIDDLE LAYER MSG FUNCTIONS
 ****************************************************************/

/*
 * Return time in msec since "start time"
 */
static int _tot_wait (struct timeval *start_time)
{
	struct timeval end_time;
	int msec_delay;

	gettimeofday(&end_time, NULL);
	msec_delay =   (end_time.tv_sec  - start_time->tv_sec ) * 1000;
	msec_delay += ((end_time.tv_usec - start_time->tv_usec + 500) / 1000);
	return msec_delay;
}

/*
 * Pick a random port number to use. Use this if the system
 * selected port can't connect. This may indicate that the
 * port/address of both the client and server match a defunct
 * socket record in TIME_WAIT state.
 */
static void _sock_bind_wild(int sockfd)
{
	int rc, retry;
	slurm_addr_t sin;
	static bool seeded = false;

	if (!seeded) {
		seeded = true;
		srand48((long int) (time(NULL) + getpid()));
	}

	slurm_setup_sockaddr(&sin, RANDOM_USER_PORT);

	for (retry=0; retry < PORT_RETRIES ; retry++) {
		rc = bind(sockfd, (struct sockaddr *) &sin, sizeof(sin));
		if (rc >= 0)
			break;
		sin.sin_port  = htons(RANDOM_USER_PORT);
	}
	return;
}

extern ssize_t slurm_msg_recvfrom_timeout(int fd, char **pbuf, size_t *lenp,
					  uint32_t flags, int tmout)
{
	ssize_t  len;
	uint32_t msglen;

	len = slurm_recv_timeout( fd, (char *)&msglen,
				  sizeof(msglen), 0, tmout );

	if (len < ((ssize_t) sizeof(msglen)))
		return SLURM_ERROR;

	msglen = ntohl(msglen);

	if (msglen > MAX_MSG_SIZE)
		slurm_seterrno_ret(SLURM_PROTOCOL_INSANE_MSG_LENGTH);

	/*
	 *  Allocate memory on heap for message
	 */
	*pbuf = xmalloc_nz(msglen);

	if (slurm_recv_timeout(fd, *pbuf, msglen, 0, tmout) != msglen) {
		xfree(*pbuf);
		*pbuf = NULL;
		return SLURM_ERROR;
	}

	*lenp = msglen;

	return (ssize_t) msglen;
}

extern ssize_t slurm_msg_sendto(int fd, char *buffer, size_t size)
{
	return slurm_msg_sendto_timeout(fd, buffer, size,
					(slurm_get_msg_timeout() * 1000));
}

ssize_t slurm_msg_sendto_timeout(int fd, char *buffer,
				 size_t size, int timeout)
{
	int   len;
	uint32_t usize;
	SigFunc *ohandler;

	/*
	 *  Ignore SIGPIPE so that send can return a error code if the
	 *    other side closes the socket
	 */
	ohandler = xsignal(SIGPIPE, SIG_IGN);

	usize = htonl(size);

	if ((len = slurm_send_timeout(
				fd, (char *)&usize, sizeof(usize), 0,
				timeout)) < 0)
		goto done;

	if ((len = slurm_send_timeout(fd, buffer, size, 0, timeout)) < 0)
		goto done;


     done:
	xsignal(SIGPIPE, ohandler);
	return len;
}

/* Send slurm message with timeout
 * RET message size (as specified in argument) or SLURM_ERROR on error */
extern int slurm_send_timeout(int fd, char *buf, size_t size,
			      uint32_t flags, int timeout)
{
	int rc;
	int sent = 0;
	int fd_flags;
	struct pollfd ufds;
	struct timeval tstart;
	int timeleft = timeout;
	char temp[2];

	ufds.fd     = fd;
	ufds.events = POLLOUT;

	fd_flags = fcntl(fd, F_GETFL);
	fd_set_nonblocking(fd);

	gettimeofday(&tstart, NULL);

	while (sent < size) {
		timeleft = timeout - _tot_wait(&tstart);
		if (timeleft <= 0) {
			debug("slurm_send_timeout at %d of %zu, timeout",
				sent, size);
			slurm_seterrno(SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT);
			sent = SLURM_ERROR;
			goto done;
		}

		if ((rc = poll(&ufds, 1, timeleft)) <= 0) {
			if ((rc == 0) || (errno == EINTR) || (errno == EAGAIN))
 				continue;
			else {
				debug("slurm_send_timeout at %d of %zu, "
					"poll error: %s",
					sent, size, strerror(errno));
				slurm_seterrno(SLURM_COMMUNICATIONS_SEND_ERROR);
				sent = SLURM_ERROR;
				goto done;
			}
		}

		/*
		 * Check here to make sure the socket really is there.
		 * If not then exit out and notify the sender.  This
 		 * is here since a write doesn't always tell you the
		 * socket is gone, but getting 0 back from a
		 * nonblocking read means just that.
		 */
		if (ufds.revents & POLLERR) {
			debug("slurm_send_timeout: Socket POLLERR");
			slurm_seterrno(ENOTCONN);
			sent = SLURM_ERROR;
			goto done;
		}
		if ((ufds.revents & POLLHUP) || (ufds.revents & POLLNVAL) ||
		    (recv(fd, &temp, 1, flags) == 0)) {
			debug2("slurm_send_timeout: Socket no longer there");
			slurm_seterrno(ENOTCONN);
			sent = SLURM_ERROR;
			goto done;
		}
		if ((ufds.revents & POLLOUT) != POLLOUT) {
			error("slurm_send_timeout: Poll failure, revents:%d",
			      ufds.revents);
		}

		rc = send(fd, &buf[sent], (size - sent), flags);
		if (rc < 0) {
 			if (errno == EINTR)
				continue;
			debug("slurm_send_timeout at %d of %zu, "
				"send error: %s",
				sent, size, strerror(errno));
 			if (errno == EAGAIN) {	/* poll() lied to us */
				usleep(10000);
				continue;
			}
 			slurm_seterrno(SLURM_COMMUNICATIONS_SEND_ERROR);
			sent = SLURM_ERROR;
			goto done;
		}
		if (rc == 0) {
			debug("slurm_send_timeout at %d of %zu, "
				"sent zero bytes", sent, size);
			slurm_seterrno(SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT);
			sent = SLURM_ERROR;
			goto done;
		}

		sent += rc;
	}

    done:
	/* Reset fd flags to prior state, preserve errno */
	if (fd_flags != -1) {
		int slurm_err = slurm_get_errno();
		if (fcntl(fd, F_SETFL, fd_flags) < 0)
			error("%s: fcntl(F_SETFL) error: %m", __func__);
		slurm_seterrno(slurm_err);
	}

	return sent;

}

/* Get slurm message with timeout
 * RET message size (as specified in argument) or SLURM_ERROR on error */
extern int slurm_recv_timeout(int fd, char *buffer, size_t size,
			      uint32_t flags, int timeout )
{
	int rc;
	int recvlen = 0;
	int fd_flags;
	struct pollfd  ufds;
	struct timeval tstart;
	int timeleft = timeout;

	ufds.fd     = fd;
	ufds.events = POLLIN;

	fd_flags = fcntl(fd, F_GETFL);
	fd_set_nonblocking(fd);

	gettimeofday(&tstart, NULL);

	while (recvlen < size) {
		timeleft = timeout - _tot_wait(&tstart);
		if (timeleft <= 0) {
			debug("%s at %d of %zu, timeout", __func__, recvlen,
			      size);
			slurm_seterrno(SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT);
			recvlen = SLURM_ERROR;
			goto done;
		}

		if ((rc = poll(&ufds, 1, timeleft)) <= 0) {
			if ((errno == EINTR) || (errno == EAGAIN) || (rc == 0))
				continue;
			else {
				debug("%s at %d of %zu, poll error: %m",
				      __func__, recvlen, size);
 				slurm_seterrno(
					SLURM_COMMUNICATIONS_RECEIVE_ERROR);
 				recvlen = SLURM_ERROR;
  				goto done;
			}
		}

		if (ufds.revents & POLLERR) {
			debug("%s: Socket POLLERR", __func__);
			slurm_seterrno(ENOTCONN);
			recvlen = SLURM_ERROR;
			goto done;
		}
		if ((ufds.revents & POLLNVAL) ||
		    ((ufds.revents & POLLHUP) &&
		     ((ufds.revents & POLLIN) == 0))) {
			debug2("%s: Socket no longer there", __func__);
			slurm_seterrno(ENOTCONN);
			recvlen = SLURM_ERROR;
			goto done;
		}
		if ((ufds.revents & POLLIN) != POLLIN) {
			error("%s: Poll failure, revents:%d",
			      __func__, ufds.revents);
			continue;
		}

		rc = recv(fd, &buffer[recvlen], (size - recvlen), flags);
		if (rc < 0)  {
			if (errno == EINTR)
				continue;
			else {
				debug("%s at %d of %zu, recv error: %m",
				      __func__, recvlen, size);
				slurm_seterrno(
					SLURM_COMMUNICATIONS_RECEIVE_ERROR);
				recvlen = SLURM_ERROR;
				goto done;
			}
		}
		if (rc == 0) {
			debug("%s at %d of %zu, recv zero bytes",
			      __func__, recvlen, size);
			slurm_seterrno(SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT);
			recvlen = SLURM_ERROR;
			goto done;
		}
		recvlen += rc;
	}


    done:
	/* Reset fd flags to prior state, preserve errno */
	if (fd_flags != -1) {
		int slurm_err = slurm_get_errno();
		if (fcntl(fd, F_SETFL, fd_flags) < 0)
			error("%s: fcntl(F_SETFL) error: %m", __func__);
		slurm_seterrno(slurm_err);
	}

	return recvlen;
}

extern int slurm_init_msg_engine(slurm_addr_t *addr)
{
	int rc;
	int fd;
	const int one = 1;
	const size_t sz1 = sizeof(one);

	if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		error("Error creating slurm stream socket: %m");
		return fd;
	}

	rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sz1);
	if (rc < 0) {
		error("setsockopt SO_REUSEADDR failed: %m");
		goto error;
	}

	rc = bind(fd, (struct sockaddr const *) addr, sizeof(*addr));
	if (rc < 0) {
		error("Error binding slurm stream socket: %m");
		goto error;
	}

	if (listen(fd, SLURM_DEFAULT_LISTEN_BACKLOG) < 0) {
		error( "Error listening on slurm stream socket: %m" ) ;
		rc = SLURM_ERROR;
		goto error;
	}

	return fd;

error:
	(void) close(fd);
	return rc;
}

/* Await a connection on socket FD.
 * When a connection arrives, open a new socket to communicate with it,
 * set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
 * peer and *ADDR_LEN to the address's actual length, and return the
 * new socket's descriptor, or -1 for errors.  */
extern int slurm_accept_msg_conn(int fd, slurm_addr_t *addr)
{
	socklen_t len = sizeof(slurm_addr_t);
	return accept(fd, (struct sockaddr *)addr, &len);
}

extern int slurm_open_stream(slurm_addr_t *addr, bool retry)
{
	int retry_cnt;
	int fd;
	uint16_t port;
	char ip[32];

#ifdef HAVE_NATIVE_CRAY
	static int check_quiesce = -1;
	if (check_quiesce == -1) {
		char *comm_params = slurm_get_comm_parameters();
		if (xstrcasestr(comm_params, "CheckGhalQuiesce"))
			check_quiesce = 1;
		else
			check_quiesce = 0;
		xfree(comm_params);
	}

	if (check_quiesce) {
		char buffer[20];
		char *quiesce_status = "/sys/class/gni/ghal0/quiesce_status";
		int max_retry = 300;
		int quiesce_fd = open(quiesce_status, O_RDONLY);

		retry_cnt = 0;
		while (quiesce_fd >= 0 && retry_cnt < max_retry) {
			if (read(quiesce_fd, buffer, sizeof(buffer)) > 0) {
				if (buffer[0] == '0')
					break;
			}
			usleep(500000);
			if (retry_cnt % 10 == 0)
				debug3("WARNING: ghal0 quiesce status: %c, retry count %d",
				       buffer[0], retry_cnt);
			retry_cnt++;
			close(quiesce_fd);
			quiesce_fd = open(quiesce_status, O_RDONLY);
		}
		if (quiesce_fd >= 0)
			close(quiesce_fd);
	}
#endif

	if ( (addr->sin_family == 0) || (addr->sin_port  == 0) ) {
		error("Error connecting, bad data: family = %u, port = %u",
			addr->sin_family, addr->sin_port);
		return SLURM_SOCKET_ERROR;
	}

	for (retry_cnt=0; ; retry_cnt++) {
		int rc;
		if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			error("Error creating slurm stream socket: %m");
			slurm_seterrno(errno);
			return SLURM_SOCKET_ERROR;
		}

		if (retry_cnt) {
			if (retry_cnt == 1) {
				debug3("Error connecting, "
				       "picking new stream port");
			}
			_sock_bind_wild(fd);
		}

		rc = _slurm_connect(fd, (struct sockaddr const *)addr,
				    sizeof(*addr));
		if (rc >= 0)		    /* success */
			break;
		if (((errno != ECONNREFUSED) && (errno != ETIMEDOUT)) ||
		    (!retry) || (retry_cnt >= PORT_RETRIES)) {
			slurm_seterrno(errno);
			goto error;
		}

		(void) close(fd);
	}

	return fd;

error:
	slurm_get_ip_str(addr, &port, ip, sizeof(ip));
	debug2("Error connecting slurm stream socket at %s:%d: %m",
	       ip, ntohs(port));
	(void) close(fd);
	return SLURM_SOCKET_ERROR;
}

/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int slurm_get_stream_addr(int fd, slurm_addr_t *addr )
{
	socklen_t size = sizeof(addr);
	return getsockname(fd, (struct sockaddr *)addr, &size);
}

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
static int _slurm_connect (int __fd, struct sockaddr const * __addr,
			   socklen_t __len)
{
#if 0
	return connect ( __fd , __addr , __len ) ;
#else
	/* From "man connect": Note that for IP sockets the timeout
	 * may be very long when syncookies are enabled on the server.
	 *
	 * Timeouts in excess of 3 minutes have been observed, resulting
	 * in serious problems for slurmctld. Making the connect call
	 * non-blocking and polling seems to fix the problem. */
	static int timeout = 0;
	int rc, flags, flags_save, err;
	socklen_t len;
	struct pollfd ufds;

	flags = fcntl(__fd, F_GETFL);
	flags_save = flags;
	if (flags == -1) {
		error("%s: fcntl(F_GETFL) error: %m", __func__);
		flags = 0;
	}
	if (fcntl(__fd, F_SETFL, flags | O_NONBLOCK) < 0)
		error("%s: fcntl(F_SETFL) error: %m", __func__);

	err = 0;
	rc = connect(__fd , __addr , __len);
	if ((rc < 0) && (errno != EINPROGRESS))
		return -1;
	if (rc == 0)
		goto done;  /* connect completed immediately */

	ufds.fd = __fd;
	ufds.events = POLLIN | POLLOUT;
	ufds.revents = 0;

	if (timeout == 0)
		timeout = slurm_get_tcp_timeout() * 1000;

again:	rc = poll(&ufds, 1, timeout);
	if (rc == -1) {
		/* poll failed */
		if (errno == EINTR) {
			/* NOTE: connect() is non-interruptible in Linux */
			debug2("slurm_connect poll failed: %m");
			goto again;
		} else
			error("slurm_connect poll failed: %m");
		return -1;
	} else if (rc == 0) {
		/* poll timed out before any socket events */
		slurm_seterrno(ETIMEDOUT);
		debug2("slurm_connect poll timeout: %m");
		return -1;
	} else {
		/* poll saw some event on the socket
		 * We need to check if the connection succeeded by
		 * using getsockopt.  The revent is not necessarily
		 * POLLERR when the connection fails! */
		len = sizeof(err);
		if (getsockopt(__fd, SOL_SOCKET, SO_ERROR,
			       &err, &len) < 0)
			return -1; /* solaris pending error */
	}

done:
	if (flags_save != -1) {
		if (fcntl(__fd, F_SETFL, flags_save) < 0)
			error("%s: fcntl(F_SETFL) error: %m", __func__);
	}

	/* NOTE: Connection refused is typically reported for
	 * non-responsived nodes plus attempts to communicate
	 * with terminated srun commands. */
	if (err) {
		slurm_seterrno(err);
		debug2("slurm_connect failed: %m");
		slurm_seterrno(err);
		return -1;
	}

	return 0;
#endif
}

extern void slurm_set_addr_char (slurm_addr_t * addr, uint16_t port, char *host)
{
#if 1
/* NOTE: gethostbyname() is obsolete, but the alternative function (below)
 * does not work reliably. See bug 2186. */
	struct hostent * he    = NULL;
	int	   h_err = 0;
	char *	   h_buf[4096];

	/*
	 * If NULL hostname passed in, we only update the port of addr
	 */
	addr->sin_family = AF_INET;
	addr->sin_port   = htons(port);
	if (host == NULL)
		return;

	he = get_host_by_name(host, (void *)&h_buf, sizeof(h_buf), &h_err);

	if (he != NULL)
		memcpy (&addr->sin_addr.s_addr, he->h_addr, he->h_length);
	else {
		error("Unable to resolve \"%s\": %s", host, hstrerror(h_err));
		addr->sin_family = 0;
		addr->sin_port = 0;
	}
	return;
#else
/* NOTE: getaddrinfo() currently does not support aliases and is failing with
 * EAGAIN repeatedly in some cases. Comment out this logic until the function
 * works as designed. See bug 2186. */
	struct addrinfo *addrs;
	struct addrinfo *addr_ptr;

	/*
	 * If NULL hostname passed in, we only update the port of addr
	 */
	addr->sin_family = AF_INET;
	addr->sin_port   = htons(port);
	if (host == NULL)
		return;

	addrs = get_addr_info(host);
	for (addr_ptr = addrs; addr_ptr != NULL; addr_ptr = addr_ptr->ai_next) {
		if (addr_ptr->ai_family == AF_INET)
			break;
	}
	if (addr_ptr) {
		struct sockaddr_in *addr2;
		addr2 = (struct sockaddr_in *)addr_ptr->ai_addr;
		memcpy(&addr->sin_addr.s_addr,
		       &addr2->sin_addr.s_addr, sizeof(addr2->sin_addr.s_addr));
	} else {
		error("%s: Unable to resolve \"%s\"", __func__, host);
		addr->sin_family = 0;
		addr->sin_port = 0;
	}

	if (addrs)
		free_addr_info(addrs);
#endif
}

extern void slurm_get_addr (slurm_addr_t *addr, uint16_t *port, char *host,
			    unsigned int buflen )
{
	struct hostent *he;
	char   h_buf[4096];
	int    h_err  = 0;
	char * tmp_s_addr = (char *) &addr->sin_addr.s_addr;
	int    len    = sizeof(addr->sin_addr.s_addr);

	he = get_host_by_addr( tmp_s_addr, len, AF_INET,
			       (void *) &h_buf, sizeof(h_buf), &h_err );

	if (he != NULL) {
		*port = ntohs(addr->sin_port);
		strlcpy(host, he->h_name, buflen);
	} else {
		error("Lookup failed: %s", host_strerror(h_err));
		*port = 0;
		host[0] = '\0';
	}
	return;
}

extern void slurm_print_slurm_addr ( slurm_addr_t * address, char *buf,
				     size_t n )
{
	char addrbuf[INET_ADDRSTRLEN];

	if (!address) {
		snprintf(buf, n, "NULL");
		return;
	}

	inet_ntop(AF_INET, &address->sin_addr, addrbuf, INET_ADDRSTRLEN);
	/* warning: silently truncates */
	snprintf(buf, n, "%s:%d", addrbuf, ntohs(address->sin_port));
}

/* Given a file descriptor, write the peer connection's
 * IP address and port into the supplied buffer */
extern void slurm_print_peer_addr(int fd, char *buf, int buf_size)
{
	char ipstr[INET6_ADDRSTRLEN];
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	int port = -1;

	if (getpeername(fd, (struct sockaddr*)&addr, &addrlen) == 0) {
		if (addr.ss_family == AF_INET) {
			struct sockaddr_in *s = (struct sockaddr_in *)&addr;
			port = ntohs(s->sin_port);
			inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
			snprintf(buf, buf_size, "%s:%d", ipstr, port);
		} else if (addr.ss_family == AF_INET6) {
			struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
			port = ntohs(s->sin6_port);
			inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
			snprintf(buf, buf_size, "[%s]:%d", ipstr, port);
		}
	}
	if (port < 0)
		snprintf(buf, buf_size, "%s", "<getpeername error>");
}

extern void slurm_pack_slurm_addr(slurm_addr_t *addr, Buf buffer)
{
	pack32( ntohl( addr->sin_addr.s_addr ), buffer );
	pack16( ntohs( addr->sin_port ), buffer );
}

extern int slurm_unpack_slurm_addr_no_alloc(slurm_addr_t *addr, Buf buffer)
{
	addr->sin_family = AF_INET;
	safe_unpack32(&addr->sin_addr.s_addr, buffer);
	safe_unpack16(&addr->sin_port, buffer);

	addr->sin_addr.s_addr = htonl(addr->sin_addr.s_addr);
	addr->sin_port = htons(addr->sin_port);
	return SLURM_SUCCESS;

    unpack_error:
	return SLURM_ERROR;
}
