/******************************************************************************\
 *  $Id$
 *    by Chris Dunlap <cdunlap@llnl.gov>
 \******************************************************************************/


#ifndef _SLURM_PROTOCOL_INTERFACE_H
#define _SLURM_PROTOCOL_INTERFACE_H

#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
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
extern int _slurm_create_socket (slurm_socket_type_t type) __THROW ;
/* message functions */
uint32_t _slurm_init_message_engine ( slurm_addr * slurm_address ) ;
ssize_t _slurm_message_recvfrom ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address ) ;
ssize_t _slurm_message_sendto ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address ) ;
/* uint32_t _slurm_shutdown_message_engine ( slurm_fd open_fd ) ; */
/* stream functions */
uint32_t _slurm_listen_stream ( slurm_addr * slurm_address ) ;
uint32_t _slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
uint32_t _slurm_open_stream ( slurm_addr * slurm_address ) ;
/*uint32_t _slurm_close_stream ( slurm_fd open_fd ) ;*/

/*******************************\
 **  General-Purpose Functions  **
 \*******************************/

/* Create a new socket of type TYPE in domain DOMAIN, using
 *    protocol PROTOCOL.  If PROTOCOL is zero, one is chosen automatically.
 *       Returns a file descriptor for the new socket, or -1 for errors.  */
extern int _slurm_socket (int __domain, int __type, int __protocol) __THROW ;



/* Create two new sockets, of type TYPE in domain DOMAIN and using
 *    protocol PROTOCOL, which are connected to each other, and put file
 *       descriptors for them in FDS[0] and FDS[1].  If PROTOCOL is zero,
 *          one will be chosen automatically.  Returns 0 on success, -1 for errors.  */
extern int _slurm_socketpair (int __domain, int __type, int __protocol, int __fds[2]) __THROW;

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
extern int _slurm_bind (int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len) __THROW;
	     
/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int _slurm_getsockname (int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __len) __THROW;

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
extern int _slurm_connect (int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len) __THROW;

/* Put the address of the peer connected to socket FD into *ADDR
 *    (which is *LEN bytes long), and its actual length into *LEN.  */
extern int _slurm_getpeername (int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __len) __THROW;

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
extern ssize_t _slurm_send (int __fd, __const void *__buf, size_t __n, int __flags) __THROW;
extern ssize_t _slurm_write (int __fd, __const void *__buf, size_t __n) __THROW;

/* Read N bytes into BUF from socket FD.
 *    Returns the number read or -1 for errors.  */
extern ssize_t _slurm_recv (int __fd, void *__buf, size_t __n, int __flags) __THROW;
extern ssize_t _slurm_read (int __fd, void *__buf, size_t __n) __THROW;

/* Send N bytes of BUF on socket FD to peer at address ADDR (which is
 *    ADDR_LEN bytes long).  Returns the number sent, or -1 for errors.  */
extern ssize_t _slurm_sendto (int __fd, __const void *__buf, size_t __n, int __flags, __CONST_SOCKADDR_ARG __addr, socklen_t __addr_len) __THROW;
/* Read N bytes into BUF through socket FD.
 *    If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 *       the sender, and store the actual size of the address in *ADDR_LEN.
 *          Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvfrom (int __fd, void *__restrict __buf, size_t __n, int __flags, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len) __THROW;

/* Send a message described MESSAGE on socket FD.
 *    Returns the number of bytes sent, or -1 for errors.  */
extern ssize_t _slurm_sendmsg (int __fd, __const struct msghdr *__message, int __flags) __THROW ;

/* Send a message described MESSAGE on socket FD.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvmsg (int __fd, struct msghdr *__message, int __flags) __THROW ;

/* Put the current value for socket FD's option OPTNAME at protocol level LEVEL
 *    into OPTVAL (which is *OPTLEN bytes long), and set *OPTLEN to the value's
 *       actual length.  Returns 0 on success, -1 for errors.  */
extern int _slurm_getsockopt (int __fd, int __level, int __optname, void *__restrict __optval, socklen_t *__restrict __optlen) __THROW;

/* Set socket FD's option OPTNAME at protocol level LEVEL
 *    to *OPTVAL (which is OPTLEN bytes long).
 *       Returns 0 on success, -1 for errors.  */
extern int _slurm_setsockopt (int __fd, int __level, int __optname, __const void *__optval, socklen_t __optlen) __THROW;

/* Prepare to accept connections on socket FD.
 *    N connection requests will be queued before further requests are refused.
 *       Returns 0 on success, -1 for errors.  */
extern int _slurm_listen (int __fd, int __n) __THROW;

/* Await a connection on socket FD.
 *    When a connection arrives, open a new socket to communicate with it,
 *       set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
 *          peer and *ADDR_LEN to the address's actual length, and return the
 *             new socket's descriptor, or -1 for errors.  */
extern int _slurm_accept (int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len) __THROW;

/* Shut down all or part of the connection open on socket FD.
 *    HOW determines what to shut down:
 *         SHUT_RD   = No more receptions;
 *              SHUT_WR   = No more transmissions;
 *                   SHUT_RDWR = No more receptions or transmissions.
 *                      Returns 0 on success, -1 for errors.  */
extern int _slurm_shutdown (int __fd, int __how) __THROW;


extern int _slurm_close (int __fd ) __THROW;


extern int _slurm_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

extern int _slurm_pselect(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, sigset_t * sigmask);

void _slurm_FD_CLR(int fd, fd_set *set);
void _slurm_FD_ISSET(int fd, fd_set *set);
void _slurm_FD_SET(int fd, fd_set *set);
void _slurm_FD_ZERO(fd_set *set);
extern int _slurm_fcntl(int fd, int cmd);
/* function overloading problems
extern int _slurm_fcntl(int fd, int cmd, long arg);
extern int _slurm_fcntl(int fd, int cmd, struct flock *lock);
*/
		     
extern int _slurm_ioctl(int d, int request, ...);
#endif /* !_SLURM_PROTOCOL_INTERFACE_H */
