/*****************************************************************************\
 *  slurm_protocol_interface.h - mid-level slurm communication definitions
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _SLURM_PROTOCOL_INTERFACE_H
#define _SLURM_PROTOCOL_INTERFACE_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

/* WHAT ABOUT THESE INCLUDES */
#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>


#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#else
#  if HAVE_SOCKET_H
#    include <socket.h>
#  endif
#endif


#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_common.h"

/****************\
 **  Data Types  **
 \****************/

typedef enum slurm_socket_type {
	SLURM_MESSAGE ,
	SLURM_STREAM
} slurm_socket_type_t;

/*******************************\
 **  MIDDLE LAYER FUNCTIONS  **
 \*******************************/

/* The must have funtions are required to implement a low level plugin
 * for the slurm protocol the general purpose functions just wrap
 * standard socket calls, so if the underlying layer implements a
 * socket like interface, it can be used as a low level transport
 * plugin with slurm the _slurm_recv and _slurm_send functions are
 * also needed
 */


/*****************************/
/* socket creation functions */
/*****************************/

/* Create a socket of the specified type
 * IN type - SLURM_STREAM or SLURM_MESSAGE
 */
slurm_fd_t _slurm_create_socket (slurm_socket_type_t type)  ;

/*****************/
/* msg functions */
/*****************/

/* _slurm_msg_recvfrom
 * Get message over the given connection, default timeout value
 * IN  fd     - an open file descriptor
 * OUT pbuf   - xmalloc'd buffer, loaded with message data
 * OUT buflen - size of allocated buffer in bytes
 * IN  flags  - communication specific flags
 *
 * RET number of bytes read
 */
ssize_t _slurm_msg_recvfrom(slurm_fd_t fd, char **pbuf, size_t *buflen,
		            uint32_t flags);

/* _slurm_msg_recvfrom_timeout reads len bytes from file descriptor fd
 * timing out after `timeout' milliseconds.
 *
 */
ssize_t _slurm_msg_recvfrom_timeout(slurm_fd_t fd, char **buf, size_t *len,
		                    uint32_t flags, int timeout);

/* _slurm_msg_sendto
 * Send message over the given connection, default timeout value
 * IN open_fd - an open file descriptor
 * IN buffer - data to transmit
 * IN size - size of buffer in bytes
 * IN flags - communication specific flags
 * RET number of bytes written
 */
ssize_t _slurm_msg_sendto ( slurm_fd_t open_fd, char *buffer ,
			    size_t size , uint32_t flags ) ;
/* _slurm_msg_sendto_timeout is identical to _slurm_msg_sendto except
 * IN timeout - maximum time to wait for a message in milliseconds */
ssize_t _slurm_msg_sendto_timeout ( slurm_fd_t open_fd, char *buffer,
				    size_t size, uint32_t flags, int timeout );

/* _slurm_close_accepted_conn
 * In the bsd implmentation maps directly to a close call, to close
 *	the socket that was accepted
 * IN open_fd		- an open file descriptor to close
 * RET int		- the return code
 */
int _slurm_close_accepted_conn ( slurm_fd_t open_fd ) ;

/********************/
/* stream functions */
/********************/

/* slurm_init_msg_engine
 * opens a stream server and listens on it
 * IN slurm_address 	- slurm_addr_t to bind the server stream to
 * RET slurm_fd		- file descriptor of the stream created
 */
slurm_fd_t slurm_init_msg_engine ( slurm_addr_t * slurm_address ) ;

/* slurm_accept_msg_conn
 * accepts a incoming stream connection on a stream server slurm_fd
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr_t of the accepted connection
 * RET slurm_fd		- file descriptor of the accepted connection
 */
slurm_fd_t slurm_accept_msg_conn ( slurm_fd_t open_fd ,
				slurm_addr_t * slurm_address ) ;

/* slurm_open_stream
 * opens a client connection to stream server
 * IN slurm_address 	- slurm_addr_t of the connection destination
 * IN retry             - if true, retry as needed with various ports
 *                        to avoid socket address collision
 * RET slurm_fd_t         - file descriptor of the connection created
 */
slurm_fd_t slurm_open_stream ( slurm_addr_t * slurm_address, bool retry ) ;

/* _slurm_get_stream_addr
 * esentially a encapsilated get_sockname
 * IN open_fd 		- file descriptor to retreive slurm_addr_t for
 * OUT address		- address that open_fd to bound to
 */
extern int _slurm_get_stream_addr ( slurm_fd_t open_fd ,
				    slurm_addr_t * address ) ;

/* _slurm_close_stream
 * closes either a server or client stream file_descriptor
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
extern int _slurm_close_stream ( slurm_fd_t open_fd ) ;

/* make an open slurm connection blocking or non-blocking
 *	(i.e. wait or do not wait for i/o completion )
 * IN open_fd	- an open file descriptor to change the effect
 * RET int	- the return code
 */
extern int _slurm_set_stream_non_blocking ( slurm_fd_t open_fd ) ;
extern int _slurm_set_stream_blocking ( slurm_fd_t open_fd ) ;

int _slurm_send_timeout ( slurm_fd_t open_fd, char *buffer ,
			  size_t size , uint32_t flags, int timeout ) ;
int _slurm_recv_timeout ( slurm_fd_t open_fd, char *buffer ,
			  size_t size , uint32_t flags, int timeout ) ;

/***************************/
/* slurm address functions */
/***************************/
/* build a slurm address bassed upon ip address and port number
 * OUT slurm_address - the constructed slurm_address
 * IN port - port to be used
 * IN ip_address - the IP address to connect with
 */
extern void _slurm_set_addr_uint ( slurm_addr_t * slurm_address ,
				   uint16_t port , uint32_t ip_address ) ;

/* resets the address field of a slurm_addr, port and family are unchanged */
extern void _reset_slurm_addr ( slurm_addr_t * slurm_address ,
				slurm_addr_t new_address );


/* build a slurm address bassed upon host name and port number
 * OUT slurm_address - the constructed slurm_address
 * IN port - port to be used
 * IN host - name of host to connect with
 */
extern void _slurm_set_addr_char ( slurm_addr_t * slurm_address ,
				   uint16_t port , char * host ) ;

/* given a slurm_address it returns its port and hostname
 * IN slurm_address	- slurm_addr_t to be queried
 * OUT port		- port number
 * OUT host		- hostname
 * IN buf_len		- length of hostname buffer
 */
extern void _slurm_get_addr ( slurm_addr_t * slurm_address ,
			      uint16_t * port , char * host ,
			      uint32_t buf_len ) ;

/* prints a slurm_addr_t into a buf
 * IN address		- slurm_addr_t to print
 * IN buf		- space for string representation of slurm_addr
 * IN n			- max number of bytes to write (including NUL)
 */
extern void _slurm_print_slurm_addr ( slurm_addr_t * address,
				      char *buf, size_t n ) ;

/*****************************/
/* slurm addr pack functions */
/*****************************/
extern void _slurm_pack_slurm_addr ( slurm_addr_t * slurm_address ,
				     Buf buffer ) ;
extern int _slurm_unpack_slurm_addr_no_alloc (
	slurm_addr_t * slurm_address , Buf buffer ) ;


/*******************************\
 ** BSD LINUX SOCKET FUNCTIONS  **
 \*******************************/

/* Create a new socket of type TYPE in domain DOMAIN, using
 * protocol PROTOCOL.  If PROTOCOL is zero, one is chosen automatically.
 * Returns a file descriptor for the new socket, or -1 for errors.  */
extern int _slurm_socket (int __domain, int __type, int __protocol)  ;

/* Create two new sockets, of type TYPE in domain DOMAIN and using
 * protocol PROTOCOL, which are connected to each other, and put file
 * descriptors for them in FDS[0] and FDS[1].  If PROTOCOL is zero,
 * one will be chosen automatically.  Returns 0 on success, -1 for errors.  */
extern int _slurm_socketpair (int __domain, int __type, int __protocol,
			      int __fds[2]) ;

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
extern int _slurm_bind (int __fd, struct sockaddr const * __addr,
			socklen_t __len) ;

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
extern int _slurm_connect (int __fd, struct sockaddr const * __addr,
			   socklen_t __len) ;

/* Prepare to accept connections on socket FD.
 * N connection requests will be queued before further requests are refused.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_listen (int __fd, int __n) ;

/* Await a connection on socket FD.
 * When a connection arrives, open a new socket to communicate with it,
 * set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
 * peer and *ADDR_LEN to the address's actual length, and return the
 * new socket's descriptor, or -1 for errors.  */
extern int _slurm_accept (int __fd, struct sockaddr * __addr,
			  socklen_t *__restrict __addr_len) ;

/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int _slurm_getsockname (int __fd, struct sockaddr * __addr,
			       socklen_t *__restrict __len) ;

/* Put the address of the peer connected to socket FD into *ADDR
 * (which is *LEN bytes long), and its actual length into *LEN.  */
extern int _slurm_getpeername (int __fd, struct sockaddr * __addr,
			       socklen_t *__restrict __len) ;

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
extern ssize_t _slurm_send (int __fd, __const void *__buf,
			    size_t __n, int __flags) ;
extern ssize_t _slurm_write (int __fd, __const void *__buf, size_t __n) ;

/* Read N bytes into BUF from socket FD.
 * Returns the number read or -1 for errors.  */
extern ssize_t _slurm_recv (int __fd, void *__buf, size_t __n, int __flags) ;
extern ssize_t _slurm_read (int __fd, void *__buf, size_t __n) ;

/* Send N bytes of BUF on socket FD to peer at address ADDR (which is
 * ADDR_LEN bytes long).  Returns the number sent, or -1 for errors.  */
extern ssize_t _slurm_sendto (int __fd, __const void *__buf, size_t __n,
			      int __flags, struct sockaddr const * __addr,
			      socklen_t __addr_len) ;

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes sent, or -1 for errors.  */
extern ssize_t _slurm_sendmsg (int __fd, __const struct msghdr *__msg,
			       int __flags)  ;

/* Read N bytes into BUF through socket FD.
 * If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 * the sender, and store the actual size of the address in *ADDR_LEN.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvfrom (int __fd, void *__restrict __buf,
				size_t __n, int __flags,
				struct sockaddr * __addr,
				socklen_t *__restrict __addr_len) ;

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvmsg (int __fd, struct msghdr *__msg,
			       int __flags)  ;

/* Put the current value for socket FD's option OPTNAME at protocol level LEVEL
 * into OPTVAL (which is *OPTLEN bytes long), and set *OPTLEN to the value's
 * actual length.  Returns 0 on success, -1 for errors.  */
extern int _slurm_getsockopt (int __fd, int __level, int __optname,
			      void *__restrict __optval,
			      socklen_t *__restrict __optlen) ;

/* Set socket FD's option OPTNAME at protocol level LEVEL
 * to *OPTVAL (which is OPTLEN bytes long).
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_setsockopt (int __fd, int __level, int __optname,
			      __const void *__optval, socklen_t __optlen) ;

/* Shut down all or part of the connection open on socket FD.
 * HOW determines what to shut down:
 * SHUT_RD   = No more receptions;
 * SHUT_WR   = No more transmissions;
 * SHUT_RDWR = No more receptions or transmissions.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_shutdown (int __fd, int __how) ;
extern int _slurm_close (int __fd ) ;

extern int _slurm_fcntl(int fd, int cmd, ... );
extern int _slurm_vfcntl(int fd, int cmd, va_list va );

extern int _slurm_ioctl(int d, int request, ...);
#endif /* !_SLURM_PROTOCOL_INTERFACE_H */
