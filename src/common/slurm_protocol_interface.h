/******************************************************************************\
 *  $Id$
 *    by Chris Dunlap <cdunlap@llnl.gov>
 \******************************************************************************/


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

#include <src/common/slurm_protocol_common.h>


/****************\
 **  Data Types  **
 \****************/



/*******************************\
 **  Must Have Functions  **
 \*******************************/
/* The must have funtions are required to implement a low level plugin for the slurm protocol
 * the genereal purpose functions just wrap standard socket calls, so if the underlying layer 
 * implements a socket like interface, it can be used as a low level transport plugin with slurm
 *
 * the _slurm_recv and _slurm_send functions are also needed
 */


typedef enum slurm_socket_type { SLURM_MESSAGE , SLURM_STREAM } slurm_socket_type_t;

/* high level interface */
slurm_fd _slurm_create_socket (slurm_socket_type_t type)  ;
/* msg functions */
slurm_fd _slurm_init_msg_engine ( slurm_addr * slurm_address ) ;
slurm_fd _slurm_open_msg_conn ( slurm_addr * slurm_address ) ;
ssize_t _slurm_msg_recvfrom ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address ) ;
ssize_t _slurm_msg_sendto ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address ) ;
/* this should be a no-op that just returns open_fd in a message implementation */
slurm_fd _slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
/* this should be a no-op in a message implementation */
int _slurm_close_accepted_conn ( slurm_fd open_fd ) ;



/* stream functions */
slurm_fd _slurm_listen_stream ( slurm_addr * slurm_address ) ;
slurm_fd _slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
slurm_fd _slurm_open_stream ( slurm_addr * slurm_address ) ;

/*******************************\
 **  General-Purpose Functions  **
 \*******************************/

/* Create a new socket of type TYPE in domain DOMAIN, using
 *    protocol PROTOCOL.  If PROTOCOL is zero, one is chosen automatically.
 *       Returns a file descriptor for the new socket, or -1 for errors.  */
extern int _slurm_socket (int __domain, int __type, int __protocol)  ;



/* Create two new sockets, of type TYPE in domain DOMAIN and using
 * protocol PROTOCOL, which are connected to each other, and put file
 * descriptors for them in FDS[0] and FDS[1].  If PROTOCOL is zero,
 * one will be chosen automatically.  Returns 0 on success, -1 for errors.  */
extern int _slurm_socketpair (int __domain, int __type, int __protocol, int __fds[2]) ;

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
extern int _slurm_bind (int __fd, struct sockaddr const * __addr, socklen_t __len) ;
	     
/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int _slurm_getsockname (int __fd, struct sockaddr * __addr, socklen_t *__restrict __len) ;

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
extern int _slurm_connect (int __fd, struct sockaddr const * __addr, socklen_t __len) ;

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

/* Read N bytes into BUF through socket FD.
 * If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 * the sender, and store the actual size of the address in *ADDR_LEN.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvfrom (int __fd, void *__restrict __buf, size_t __n, int __flags, struct sockaddr * __addr, socklen_t *__restrict __addr_len) ;

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes sent, or -1 for errors.  */
extern ssize_t _slurm_sendmsg (int __fd, __const struct msghdr *__msg, int __flags)  ;

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

/* Shut down all or part of the connection open on socket FD.
 * HOW determines what to shut down:
 * SHUT_RD   = No more receptions;
 * SHUT_WR   = No more transmissions;
 * SHUT_RDWR = No more receptions or transmissions.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_shutdown (int __fd, int __how) ;
extern int _slurm_close (int __fd ) ;

extern int _slurm_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
extern int _slurm_pselect(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, sigset_t * sigmask);

void _slurm_FD_CLR(int fd, fd_set *set);
int _slurm_FD_ISSET(int fd, fd_set *set);
void _slurm_FD_SET(int fd, fd_set *set);
void _slurm_FD_ZERO(fd_set *set);
extern int _slurm_fcntl(int fd, int cmd);
/* function overloading problems
extern int _slurm_fcntl(int fd, int cmd, long arg);
extern int _slurm_fcntl(int fd, int cmd, struct flock *lock);
*/
		     
extern int _slurm_ioctl(int d, int request, ...);

/* sets the fields of a slurm_addr */
extern void _slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;
extern void _slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
extern void _slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
extern void _slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , uint32_t buf_len ) ;

#endif /* !_SLURM_PROTOCOL_INTERFACE_H */
