/*****************************************************************************\
 *  slurm_protocol_interface.j - mid-level slurm communication definitions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
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


#ifndef _SLURM_PROTOCOL_INTERFACE_H
#define _SLURM_PROTOCOL_INTERFACE_H

#if HAVE_CONFIG_H
#  include <config.h>
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

#include <src/common/pack.h>
#include <src/common/slurm_protocol_common.h>

extern struct timeval SLURM_MESSGE_TIMEOUT_SEC_STATIC ;

/****************\
 **  Data Types  **
 \****************/

typedef enum slurm_socket_type { SLURM_MESSAGE , SLURM_STREAM } slurm_socket_type_t;

/*******************************\
 **  MIDDLE LAYER FUNCTIONS  **
 \*******************************/
/* The must have funtions are required to implement a low level plugin for the slurm protocol
 * the general purpose functions just wrap standard socket calls, so if the underlying layer 
 * implements a socket like interface, it can be used as a low level transport plugin with slurm
 * the _slurm_recv and _slurm_send functions are also needed
 */


/*****************************/
/* socket creation functions */
/*****************************/
slurm_fd _slurm_create_socket (slurm_socket_type_t type)  ;

/*****************/
/* msg functions */
/*****************/
slurm_fd _slurm_init_msg_engine ( slurm_addr * slurm_address ) ;
slurm_fd _slurm_open_msg_conn ( slurm_addr * slurm_address ) ;
ssize_t _slurm_msg_recvfrom ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address ) ;
ssize_t _slurm_msg_recvfrom_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address , struct timeval * timeout) ;
ssize_t _slurm_msg_sendto ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address ) ;
ssize_t _slurm_msg_sendto_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address , struct timeval * timeout ) ;
slurm_fd _slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
int _slurm_close_accepted_conn ( slurm_fd open_fd ) ;

/********************/
/* stream functions */
/********************/
slurm_fd _slurm_listen_stream ( slurm_addr * slurm_address ) ;
slurm_fd _slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
slurm_fd _slurm_open_stream ( slurm_addr * slurm_address ) ;
extern int _slurm_get_stream_addr ( slurm_fd open_fd , slurm_addr * address ) ;
extern int _slurm_close_stream ( slurm_fd open_fd ) ;

extern inline int _slurm_set_stream_non_blocking ( slurm_fd open_fd ) ;
extern inline int _slurm_set_stream_blocking ( slurm_fd open_fd ) ;

int _slurm_send_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, struct timeval * timeout ) ;
int _slurm_recv_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, struct timeval * timeout ) ;
	
/***************************/
/* slurm address functions */
/***************************/
extern void _slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;
extern void _slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
extern void _slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
extern void _slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , uint32_t buf_len ) ;
extern void _slurm_print_slurm_addr ( slurm_addr * address, char *buf, size_t n ) ;

/*****************************/
/* slurm addr pack functions */
/*****************************/
extern void _slurm_pack_slurm_addr ( slurm_addr * slurm_address , Buf buffer ) ;
extern void _slurm_unpack_slurm_addr_no_alloc ( slurm_addr * slurm_address , Buf buffer ) ;


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
extern int _slurm_socketpair (int __domain, int __type, int __protocol, int __fds[2]) ;

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
extern int _slurm_bind (int __fd, struct sockaddr const * __addr, socklen_t __len) ;

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
extern int _slurm_connect (int __fd, struct sockaddr const * __addr, socklen_t __len) ;

/* Prepare to accept connections on socket FD.
 * N connection requests will be queued before further requests are refused.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_listen (int __fd, int __n) ;

/* Await a connection on socket FD.
 * When a connection arrives, open a new socket to communicate with it,
 * set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
 * peer and *ADDR_LEN to the address's actual length, and return the
 * new socket's descriptor, or -1 for errors.  */
extern int _slurm_accept (int __fd, struct sockaddr * __addr, socklen_t *__restrict __addr_len) ;

/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int _slurm_getsockname (int __fd, struct sockaddr * __addr, socklen_t *__restrict __len) ;

/* Put the address of the peer connected to socket FD into *ADDR
 * (which is *LEN bytes long), and its actual length into *LEN.  */
extern int _slurm_getpeername (int __fd, struct sockaddr * __addr, socklen_t *__restrict __len) ;

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
extern ssize_t _slurm_send (int __fd, __const void *__buf, size_t __n, int __flags) ;
extern ssize_t _slurm_write (int __fd, __const void *__buf, size_t __n) ;

/* Read N bytes into BUF from socket FD.
 * Returns the number read or -1 for errors.  */
extern ssize_t _slurm_recv (int __fd, void *__buf, size_t __n, int __flags) ;
extern ssize_t _slurm_read (int __fd, void *__buf, size_t __n) ;

/* Send N bytes of BUF on socket FD to peer at address ADDR (which is
 * ADDR_LEN bytes long).  Returns the number sent, or -1 for errors.  */
extern ssize_t _slurm_sendto (int __fd, __const void *__buf, size_t __n, int __flags, struct sockaddr const * __addr, socklen_t __addr_len) ;

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes sent, or -1 for errors.  */
extern ssize_t _slurm_sendmsg (int __fd, __const struct msghdr *__msg, int __flags)  ;

/* Read N bytes into BUF through socket FD.
 * If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 * the sender, and store the actual size of the address in *ADDR_LEN.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvfrom (int __fd, void *__restrict __buf, size_t __n, int __flags, struct sockaddr * __addr, socklen_t *__restrict __addr_len) ;

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvmsg (int __fd, struct msghdr *__msg, int __flags)  ;

/* Put the current value for socket FD's option OPTNAME at protocol level LEVEL
 * into OPTVAL (which is *OPTLEN bytes long), and set *OPTLEN to the value's
 * actual length.  Returns 0 on success, -1 for errors.  */
extern int _slurm_getsockopt (int __fd, int __level, int __optname, void *__restrict __optval, socklen_t *__restrict __optlen) ;

/* Set socket FD's option OPTNAME at protocol level LEVEL
 * to *OPTVAL (which is OPTLEN bytes long).
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_setsockopt (int __fd, int __level, int __optname, __const void *__optval, socklen_t __optlen) ;

/* Shut down all or part of the connection open on socket FD.
 * HOW determines what to shut down:
 * SHUT_RD   = No more receptions;
 * SHUT_WR   = No more transmissions;
 * SHUT_RDWR = No more receptions or transmissions.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_shutdown (int __fd, int __how) ;
extern int _slurm_close (int __fd ) ;


/* Non-blocking calls */
extern int _slurm_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
/* extern int _slurm_pselect(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, sigset_t * sigmask); */
void _slurm_FD_CLR(int fd, fd_set *set);
int _slurm_FD_ISSET(int fd, fd_set *set);
void _slurm_FD_SET(int fd, fd_set *set);
void _slurm_FD_ZERO(fd_set *set);

extern int _slurm_fcntl(int fd, int cmd, ... );
extern int _slurm_vfcntl(int fd, int cmd, va_list va );

extern int _slurm_ioctl(int d, int request, ...);
#endif /* !_SLURM_PROTOCOL_INTERFACE_H */
