/* Author Kevin Tew
 * May 17, 2002
 */
#include <src/common/slurm_protocol_interface.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_defs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>

extern int debug ;

/* high level calls */
uint32_t _slurm_init_message_engine ( slurm_addr * slurm_address )
{
	return _slurm_listen_stream ( slurm_address ) ;
}

ssize_t _slurm_message_recvfrom ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address )
{
	slurm_fd connection_fd ;
	size_t recv_len ;
	connection_fd = _slurm_accept_stream ( open_fd , slurm_address ) ;
	if ( connection_fd == SLURM_SOCKET_ERROR )
	{
		fprintf ( stderr , "Error opening stream socket to receive message datagram emulation layeri\n" ) ;
		return connection_fd ;
	}
	recv_len = _slurm_recv ( connection_fd , buffer , size , NO_SEND_RECV_FLAGS ) ;
	_slurm_close ( connection_fd ) ;
	return recv_len ;
}

ssize_t _slurm_message_sendto ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address )
{
	slurm_fd connection_fd ;
	size_t send_len ;
	connection_fd = _slurm_open_stream ( slurm_address ) ;
	if ( connection_fd == SLURM_SOCKET_ERROR )
	{
		fprintf ( stderr , "Error opening stream socket to send message datagram emulation layer\n" ) ;
		return connection_fd ;
	}
	send_len = _slurm_send ( connection_fd ,  buffer , size , NO_SEND_RECV_FLAGS ) ;
	_slurm_close ( connection_fd ) ;
	return send_len ;
}

uint32_t _slurm_shutdown_message_engine ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}

uint32_t _slurm_listen_stream ( slurm_addr * slurm_address )
{
	uint32_t rc ;
	slurm_fd connection_fd ;
	rc =_slurm_create_socket ( SLURM_STREAM ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error creating slurm stream socket: errno %i\n", errno ) ;
		}
		return rc ;
	}
	else
	{
		connection_fd = rc ;
	}

	rc = _slurm_bind ( connection_fd , ( struct sockaddr const * ) slurm_address , sizeof ( slurm_addr ) ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error binding slurm stream socket: errno %i\n" , errno ) ;
		}
		return rc ;
	}

	rc = _slurm_listen ( connection_fd , DEFAULT_LISTEN_BACKLOG ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error listening on slurm stream socket: errno %i\n" , errno ) ;
		}
		return rc ;
	}

	return connection_fd ;
}

uint32_t _slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address )
{
	uint32_t rc ;
	uint32_t addr_len = sizeof ( slurm_addr ) ;
	slurm_fd connection_fd ;
	rc =_slurm_accept ( open_fd , ( struct sockaddr * ) slurm_address , & addr_len ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error accepting slurm stream socket: errno %i\n", errno ) ;
		}
		return rc ;
	}
	else
	{
		connection_fd = rc ;
	}
	return connection_fd ;

}

uint32_t _slurm_open_stream ( slurm_addr * slurm_address )
{
	uint32_t rc ;
	slurm_fd connection_fd ;
	rc =_slurm_create_socket ( SLURM_STREAM ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error creating slurm stream socket: errno %i\n", errno ) ;
		}
		return rc ;
	}
	else
	{
		connection_fd = rc ;
	}

	rc = _slurm_connect ( connection_fd , ( struct sockaddr const * ) slurm_address , sizeof ( slurm_addr ) ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error listening on slurm stream socket: errno %i\n" , errno ) ;
		}
		return rc ;
	}

	return connection_fd ;

}

int _slurm_close_stream ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}

extern int _slurm_socket (int __domain, int __type, int __protocol)
{
	return socket ( __domain, __type, __protocol ) ;
}	

extern int _slurm_create_socket ( slurm_socket_type_t type )
{
	switch ( type )
	{
		case SLURM_STREAM :
			return _slurm_socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP) ;
			break;
		case SLURM_MESSAGE :
			return _slurm_socket ( AF_INET, SOCK_DGRAM, IPPROTO_UDP ) ;
			break;
		default :
			return SLURM_SOCKET_ERROR;
	}
}

/* Create two new sockets, of type TYPE in domain DOMAIN and using
 *    protocol PROTOCOL, which are connected to each other, and put file
 *       descriptors for them in FDS[0] and FDS[1].  If PROTOCOL is zero,
 *          one will be chosen automatically.  Returns 0 on success, -1 for errors.  */
extern int _slurm_socketpair (int __domain, int __type, int __protocol, int __fds[2])
{
	return SLURM_NOT_IMPLEMENTED ;
}

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
extern int _slurm_bind (int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len)
{
	return bind ( __fd , __addr , __len ) ;
}
	     
/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int _slurm_getsockname (int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __len)
{
	return getsockname ( __fd , __addr , __len ) ;	
}

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
extern int _slurm_connect (int __fd, __CONST_SOCKADDR_ARG __addr, socklen_t __len)
{
	return connect ( __fd , __addr , __len ) ;
}

/* Put the address of the peer connected to socket FD into *ADDR
 *    (which is *LEN bytes long), and its actual length into *LEN.  */
extern int _slurm_getpeername (int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __len)
{
	return getpeername ( __fd , __addr , __len ) ;
}

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
extern ssize_t _slurm_send (int __fd, __const void *__buf, size_t __n, int __flags)
{
	return send ( __fd , __buf , __n , __flags ) ;
}

/* Read N bytes into BUF from socket FD.
 *    Returns the number read or -1 for errors.  */
extern ssize_t _slurm_recv (int __fd, void *__buf, size_t __n, int __flags)
{
	return recv ( __fd , __buf , __n , __flags ) ;
}

/* Send N bytes of BUF on socket FD to peer at address ADDR (which is
 *    ADDR_LEN bytes long).  Returns the number sent, or -1 for errors.  */
extern ssize_t _slurm_sendto (int __fd, __const void *__buf, size_t __n, int __flags, __CONST_SOCKADDR_ARG __addr, socklen_t __addr_len)
{
	return sendto ( __fd , __buf , __n , __flags , __addr, __addr_len) ;
}
/* Read N bytes into BUF through socket FD.
 *    If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 *       the sender, and store the actual size of the address in *ADDR_LEN.
 *          Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvfrom (int __fd, void *__restrict __buf, size_t __n, int __flags, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len)
{
	return recvfrom ( __fd , __buf , __n , __flags , __addr, __addr_len) ;
}

/* Send a message described MESSAGE on socket FD.
 *    Returns the number of bytes sent, or -1 for errors.  */
extern ssize_t _slurm_sendmsg (int __fd, __const struct msghdr *__message, int __flags)
{
	return sendmsg ( __fd , __message , __flags ) ;
}

/* Send a message described MESSAGE on socket FD.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvmsg (int __fd, struct msghdr *__message, int __flags)
{
	return recvmsg ( __fd , __message , __flags );
}

/* Put the current value for socket FD's option OPTNAME at protocol level LEVEL
 *    into OPTVAL (which is *OPTLEN bytes long), and set *OPTLEN to the value's
 *       actual length.  Returns 0 on success, -1 for errors.  */
extern int _slurm_getsockopt (int __fd, int __level, int __optname, void *__restrict __optval, socklen_t *__restrict __optlen)
{
	return getsockopt ( __fd , __level , __optname , __optval , __optlen ) ;
}

/* Set socket FD's option OPTNAME at protocol level LEVEL
 *    to *OPTVAL (which is OPTLEN bytes long).
 *       Returns 0 on success, -1 for errors.  */
extern int _slurm_setsockopt (int __fd, int __level, int __optname, __const void *__optval, socklen_t __optlen)
{
	return setsockopt ( __fd , __level , __optname , __optval , __optlen ) ;
}


/* Prepare to accept connections on socket FD.
 *    N connection requests will be queued before further requests are refused.
 *       Returns 0 on success, -1 for errors.  */
extern int _slurm_listen (int __fd, int __n)
{
	return listen ( __fd , __n ) ;
}

/* Await a connection on socket FD.
 *    When a connection arrives, open a new socket to communicate with it,
 *       set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
 *          peer and *ADDR_LEN to the address's actual length, and return the
 *             new socket's descriptor, or -1 for errors.  */
extern int _slurm_accept (int __fd, __SOCKADDR_ARG __addr, socklen_t *__restrict __addr_len)
{
	return accept ( __fd , __addr , __addr_len ) ;
}

/* Shut down all or part of the connection open on socket FD.
 *    HOW determines what to shut down:
 *         SHUT_RD   = No more receptions;
 *              SHUT_WR   = No more transmissions;
 *                   SHUT_RDWR = No more receptions or transmissions.
 *                      Returns 0 on success, -1 for errors.  */
extern int _slurm_shutdown (int __fd, int __how)
{
	return shutdown ( __fd , __how );
}

extern int _slurm_close (int __fd )
{
	return close ( __fd ) ;
}

/*
extern int _slurm_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
}

extern int _slurm_pselect(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, sigset_t * sigmask)
{
}
*/
/* THESE ARE MACROS
extern _slurm_FD_CLR(int fd, fd_set *set)
{
}
extern _slurm_FD_ISSET(int fd, fd_set *set)
{
}
extern _slurm_FD_SET(int fd, fd_set *set)
{
}
extern _slurm_FD_ZERO(fd_set *set)
{
}
*/

/*
extern int _slurm_fcntl(int fd, int cmd)
{
}
extern int _slurm_fcntl(int fd, int cmd, long arg)
{
}
extern int _slurm_fcntl(int fd, int cmd, struct flock *lock)
{
}
extern int _slurm_ioctl(int d, int request, ...)
{
}
*/

