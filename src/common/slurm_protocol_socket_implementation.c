/*****************************************************************************\
 *  slurm_protocol_socket_implementation.c - slurm communications interfaces 
 *	based upon sockets
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
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

#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <arpa/inet.h>

#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#else
#  if HAVE_SOCKET_H
#    include <socket.h>
#  endif
#endif

#include <src/common/slurm_protocol_interface.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/log.h>
#include <src/common/pack.h>

#define TEMP_BUFFER_SIZE 1024

/* global constants */
struct timeval SLURM_MESSGE_TIMEOUT_SEC_STATIC = { tv_sec:10L , tv_usec:0 } ;


/* internal static prototypes */
/*****************************************************************
 * MIDDLE LAYER MSG FUNCTIONS
 ****************************************************************/
slurm_fd _slurm_init_msg_engine ( slurm_addr * slurm_address )
{
	return _slurm_listen_stream ( slurm_address ) ;
}

slurm_fd _slurm_open_msg_conn ( slurm_addr * slurm_address )
{
	return _slurm_open_stream ( slurm_address ) ;
}

/* this should be a no-op that just returns open_fd in a message implementation */
slurm_fd _slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address )
{
	return _slurm_accept_stream ( open_fd , slurm_address ) ;
}	
/* this should be a no-op in a message implementation */
int _slurm_close_accepted_conn ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

ssize_t _slurm_msg_recvfrom ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address )
{
	struct timeval SLURM_MESSGE_TIMEOUT_SEC = SLURM_MESSGE_TIMEOUT_SEC_STATIC ;
	return _slurm_msg_recvfrom_timeout ( open_fd , buffer , size , flags , slurm_address , & SLURM_MESSGE_TIMEOUT_SEC ) ;
}

ssize_t _slurm_msg_recvfrom_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address , struct timeval * timeout)
{
	size_t recv_len ;

	char size_buffer_temp [TEMP_BUFFER_SIZE] ;
	char * size_buffer = size_buffer_temp ;
	char * moving_buffer = NULL ;
	unsigned int size_buffer_len = 8 ;
	uint32_t transmit_size, nw_format_size ;
	unsigned int total_len ;
	unsigned int excess_len = 0 ;

	moving_buffer = (char *)&nw_format_size ;
	total_len = 0 ;
	while ( total_len < sizeof ( uint32_t ) )
	{	
		if ( ( recv_len = _slurm_recv_timeout ( open_fd , moving_buffer , 
				(sizeof ( uint32_t ) - total_len), 
				SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , timeout ) ) == SLURM_SOCKET_ERROR  )
		{
			if ( errno ==  EINTR )
				continue ;
			else
				return SLURM_PROTOCOL_ERROR ;
		}
		else if ( recv_len > 0 )
		{
			total_len += recv_len ;
			moving_buffer += recv_len ;
		}
		else if ( recv_len == 0 )
		{
			/*debug ( "Error receiving length of datagram. recv_len = 0 ") ; */
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
		else 
		{
			/*debug ( "We don't handle negative return codes > -1") ;*/
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
	}

	transmit_size = ntohl(nw_format_size);
	if (transmit_size > size) {
		error ("_slurm_msg_recvfrom_timeout buffer too small (%d of %u), excess discarded", 
			size, transmit_size);
		excess_len = transmit_size - size;
		transmit_size = size;

	}

	moving_buffer = buffer ;
	total_len = 0 ;
	while ( total_len < transmit_size )
	{
		if ( ( recv_len = _slurm_recv_timeout ( open_fd , moving_buffer , (transmit_size-total_len) , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , timeout ) ) == SLURM_SOCKET_ERROR )
		{
			if ( errno ==  EINTR )
			{
				continue ;
			}
			else
			{
				return SLURM_PROTOCOL_ERROR ;
			}
			return recv_len ;
		}
		else if ( recv_len > 0 )
		{
			total_len += recv_len ;
			moving_buffer += recv_len ;
		}
		else if ( recv_len == 0 )
		{
			/*debug ( "Error receiving length of datagram. recv_len = 0 ") ; */
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
		else 
		{
			/*debug ( "We don't handle negative return codes > -1") ;*/
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
	}
	
	if ( excess_len ) {
		/* read and toss any data transmitted that we lack the buffer for */
		moving_buffer = size_buffer ;
		size_buffer_len = TEMP_BUFFER_SIZE;
		while ( excess_len )
		{
			if (size_buffer_len > excess_len)
				size_buffer_len = excess_len;
			if ( ( recv_len = _slurm_recv_timeout ( open_fd , moving_buffer , size_buffer_len , 
						SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , timeout ) ) == SLURM_SOCKET_ERROR  )
			{
				if ( errno ==  EINTR )
					continue ;
				else
					return SLURM_PROTOCOL_ERROR ;
			}
			else if ( recv_len > 0 )
			{
				excess_len -= recv_len ;
			}
			else if ( recv_len == 0 )
			{
				/*debug ( "Error receiving length of datagram. recv_len = 0 ") ; */
				slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH ) ;
				return SLURM_PROTOCOL_ERROR ;
			}
			else 
			{
				/*debug ( "We don't handle negative return codes > -1") ;*/
				slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH ) ;
				return SLURM_PROTOCOL_ERROR ;
			}
		}
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR ) ;
		return SLURM_PROTOCOL_ERROR ;
	}

	return total_len ;
}

ssize_t _slurm_msg_sendto ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address )
{
	struct timeval SLURM_MESSGE_TIMEOUT_SEC = SLURM_MESSGE_TIMEOUT_SEC_STATIC ;
	return _slurm_msg_sendto_timeout ( open_fd, buffer , size , flags, slurm_address , & SLURM_MESSGE_TIMEOUT_SEC ) ;
}

ssize_t _slurm_msg_sendto_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address , struct timeval * timeout )
{
	size_t send_len ;

	uint32_t usize;

	struct sigaction newaction ;
	struct sigaction oldaction ;

	newaction . sa_handler = SIG_IGN ;

	/* ignore SIGPIPE so that send can return a error code if the other side closes the socket */
	sigaction(SIGPIPE, &newaction , & oldaction );

	usize = htonl(size);

	while ( true )
	{
		if ( ( send_len = _slurm_send_timeout ( open_fd , 
					(char *) &usize , sizeof ( uint32_t ) , 
					SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , timeout ) )  == 
						SLURM_PROTOCOL_ERROR )
		{
			if ( errno ==  EINTR )
				continue ;
			else
				goto _slurm_msg_sendto_exit_error ;
		}
		else if ( send_len != sizeof ( uint32_t ) )
		{
			/*debug ( "_slurm_msg_sendto only transmitted %i of %i bytes", send_len , sizeof ( uint32_t ) ) ;*/
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT ) ;
			goto _slurm_msg_sendto_exit_error ;
		}
		else
			break ;
	}
	while ( true )
	{
		if ( ( send_len = _slurm_send_timeout ( open_fd ,  buffer , size , 
				SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , timeout ) ) == SLURM_PROTOCOL_ERROR )
		{
			if ( errno ==  EINTR )
				continue ;
			else
				goto _slurm_msg_sendto_exit_error ;
		}
		else if ( send_len != size )
		{
			/*debug ( "_slurm_msg_sendto only transmitted %i of %i bytes", send_len , size ) ;*/
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT ) ;
			goto _slurm_msg_sendto_exit_error ;
		}
		else
			break ;
	}
	
	sigaction(SIGPIPE, &oldaction , & newaction );
	return send_len ;

	_slurm_msg_sendto_exit_error:	
	sigaction(SIGPIPE, &oldaction , & newaction );
	return SLURM_PROTOCOL_ERROR ;
}

int _slurm_send_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, struct timeval * timeout )
{
	int rc ;
	int bytes_sent = 0 ;
	int fd_flags ;
	_slurm_fd_set set ;

	_slurm_FD_ZERO ( & set ) ;
	fd_flags = _slurm_fcntl ( open_fd , F_GETFL ) ;
	_slurm_set_stream_non_blocking ( open_fd ) ;
	while ( bytes_sent < size )
	{
		_slurm_FD_SET ( open_fd , &set ) ;
		rc = _slurm_select ( open_fd + 1 , NULL , & set, NULL , timeout ) ;
		if ( (rc == SLURM_PROTOCOL_ERROR) || (rc < 0) )
		{
			if ( errno == EINTR )
			{
				continue ;
			}
			else
			{
				goto _slurm_send_timeout_exit_error;
			}
				
		}
		else if  ( rc == 0 )
		{
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT ) ;
			goto _slurm_send_timeout_exit_error;
		}
		else 
		{
			rc = _slurm_send ( open_fd, &buffer[bytes_sent] , (size-bytes_sent) , flags ) ;
			if ( rc  == SLURM_PROTOCOL_ERROR || rc < 0 )
			{
				if ( errno == EINTR )
				{
					continue ;
				}
				else
				{
					goto _slurm_send_timeout_exit_error;
				}
			}
			else if ( rc == 0 )
			{
				slurm_seterrno ( SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT ) ;
				goto _slurm_send_timeout_exit_error;
			}
			else
			{
				bytes_sent+=rc ;
			}
		}
	}

	if ( fd_flags != SLURM_PROTOCOL_ERROR )
	{
		_slurm_fcntl ( open_fd , F_SETFL , fd_flags ) ;
	}
	return bytes_sent ;

	_slurm_send_timeout_exit_error:
	if ( fd_flags != SLURM_PROTOCOL_ERROR )
	{
		_slurm_fcntl ( open_fd , F_SETFL , fd_flags ) ;
	}
	return SLURM_PROTOCOL_ERROR ;
	
}

int _slurm_recv_timeout ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, struct timeval * timeout )
{
	int rc ;
	int bytes_recv = 0 ;
	int fd_flags ;
	_slurm_fd_set set ;
	

	_slurm_FD_ZERO ( & set ) ;
	fd_flags = _slurm_fcntl ( open_fd , F_GETFL ) ;
	_slurm_set_stream_non_blocking ( open_fd ) ;
	while ( bytes_recv < size )
	{
		_slurm_FD_SET ( open_fd , &set ) ;
		rc = _slurm_select ( open_fd + 1 , & set , NULL , NULL , timeout ) ;
		if ( (rc == SLURM_PROTOCOL_ERROR) || (rc < 0) )
		{
			if ( errno == EINTR )
			{
				continue ;
			}
			else
			{
				goto _slurm_recv_timeout_exit_error;
			}
				
		}
		else if  ( rc == 0 )
		{
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_TIMEOUT ) ;
			goto _slurm_recv_timeout_exit_error;
		}
		else 
		{
			rc = _slurm_recv ( open_fd, &buffer[bytes_recv], (size-bytes_recv), flags ) ;
			if ( (rc  == SLURM_PROTOCOL_ERROR) || (rc < 0) )
			{

				if ( errno == EINTR )
				{
					continue ;
				}
				else
				{
					goto _slurm_recv_timeout_exit_error;
				}
			}
			else if ( rc == 0 )
			{
				slurm_seterrno ( SLURM_PROTOCOL_SOCKET_ZERO_BYTES_SENT ) ;
				goto _slurm_recv_timeout_exit_error;
			}
			else
			{
				bytes_recv+=rc ;
				break ;
			}
		}
	}
	if ( fd_flags != SLURM_PROTOCOL_ERROR )
	{
		_slurm_fcntl ( open_fd , F_SETFL , fd_flags ) ;
	}
	return bytes_recv ;

	_slurm_recv_timeout_exit_error:
	if ( fd_flags != SLURM_PROTOCOL_ERROR )
	{
		_slurm_fcntl ( open_fd , F_SETFL , fd_flags ) ;
	}
	return SLURM_PROTOCOL_ERROR ;
}

int _slurm_shutdown_msg_engine ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}

slurm_fd _slurm_listen_stream ( slurm_addr * slurm_address )
{
	int rc ;
	slurm_fd connection_fd ;
	const int one = 1;
	if ( ( connection_fd =_slurm_create_socket ( SLURM_STREAM ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error creating slurm stream socket: %m" ) ;
		return connection_fd ;
	}

	if ( ( rc = _slurm_setsockopt(connection_fd , SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one) ) ) ) 
	{
		debug ("setsockopt SO_REUSEADDR failed");
		goto error_cleanup ; 
	}

	if ( ( rc = _slurm_bind ( connection_fd , ( struct sockaddr const * ) slurm_address , sizeof ( slurm_addr ) ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error binding slurm stream socket: %m" ) ;
		goto error_cleanup ; 
	}

	if ( ( rc = _slurm_listen ( connection_fd , SLURM_PROTOCOL_DEFAULT_LISTEN_BACKLOG ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error listening on slurm stream socket: %m" ) ;
		goto error_cleanup ; 
	}

	
	return connection_fd ;

	error_cleanup:
	_slurm_close_stream ( connection_fd ) ;
	return rc;
	
}

slurm_fd _slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address )
{
	size_t addr_len = sizeof ( slurm_addr ) ;
	slurm_fd connection_fd ;
	if ( ( connection_fd = _slurm_accept ( open_fd , ( struct sockaddr * ) slurm_address , & addr_len ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error accepting slurm stream socket: %m" ) ;
	}
	return connection_fd ;

}

slurm_fd _slurm_open_stream ( slurm_addr * slurm_address )
{
	int rc ;
	slurm_fd connection_fd ;

	if ( (slurm_address->sin_family == 0) &&
	     (slurm_address->sin_port == 0) ) 
	{
		error ( "Attempt to open socket with null address" );
		return SLURM_SOCKET_ERROR;
	}

	if ( ( connection_fd =_slurm_create_socket ( SLURM_STREAM ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error creating slurm stream socket: %m" ) ;
		return connection_fd ;
	}

	if ( ( rc = _slurm_connect ( connection_fd , ( struct sockaddr const * ) slurm_address , sizeof ( slurm_addr ) ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error connecting on slurm stream socket: %m" ) ;
		goto error_cleanup ; 
	}

	return connection_fd ;

	error_cleanup:
	_slurm_close_stream ( connection_fd ) ;
	return rc;
}
	
int _slurm_get_stream_addr ( slurm_fd open_fd , slurm_addr * address )
{
	int size ;
	
	size = sizeof ( address ) ;
	return _slurm_getsockname ( open_fd , ( struct sockaddr * ) address , & size ) ;
}

int _slurm_close_stream ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}


int _slurm_set_stream_non_blocking ( slurm_fd open_fd )
{
	int flags ;
	if ( ( flags = _slurm_fcntl ( open_fd , F_GETFL ) ) == SLURM_SOCKET_ERROR )
	{
		return SLURM_SOCKET_ERROR ;
	}
	flags |= O_NONBLOCK ;
	return _slurm_fcntl ( open_fd , F_SETFL , flags )  ;
}

int _slurm_set_stream_blocking ( slurm_fd open_fd ) 
{
	int flags ;
	if ( ( flags = _slurm_fcntl ( open_fd , F_GETFL ) ) == SLURM_SOCKET_ERROR )
	{
		return SLURM_SOCKET_ERROR ;
	}
	flags &= !O_NONBLOCK ;
	return _slurm_fcntl ( open_fd , F_SETFL , flags ) ;
}

extern int _slurm_socket (int __domain, int __type, int __protocol)
{
	return socket ( __domain, __type, __protocol ) ;
}	

extern slurm_fd _slurm_create_socket ( slurm_socket_type_t type )
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
 * protocol PROTOCOL, which are connected to each other, and put file
 * descriptors for them in FDS[0] and FDS[1].  If PROTOCOL is zero,
 * one will be chosen automatically.  Returns 0 on success, -1 for errors.  */
extern int _slurm_socketpair (int __domain, int __type, int __protocol, int __fds[2])
{
	return SLURM_PROTOCOL_FUNCTION_NOT_IMPLEMENTED ;
}

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
extern int _slurm_bind (int __fd, struct sockaddr const * __addr, socklen_t __len)
{
	return bind ( __fd , __addr , __len ) ;
}

/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int _slurm_getsockname (int __fd, struct sockaddr * __addr, socklen_t *__restrict __len)
{
	return getsockname ( __fd , __addr , __len ) ;	
}

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
extern int _slurm_connect (int __fd, struct sockaddr const * __addr, socklen_t __len)
{
	return connect ( __fd , __addr , __len ) ;
}

/* Put the address of the peer connected to socket FD into *ADDR
 * (which is *LEN bytes long), and its actual length into *LEN.  */
extern int _slurm_getpeername (int __fd, struct sockaddr * __addr, socklen_t *__restrict __len)
{
	return getpeername ( __fd , __addr , __len ) ;
}

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
extern ssize_t _slurm_send (int __fd, __const void *__buf, size_t __n, int __flags)
{
	return send ( __fd , __buf , __n , __flags ) ;
}

/* Read N bytes into BUF from socket FD.
 * Returns the number read or -1 for errors.  */
extern ssize_t _slurm_recv (int __fd, void *__buf, size_t __n, int __flags)
{
	return recv ( __fd , __buf , __n , __flags ) ;
}

/* Send N bytes of BUF on socket FD to peer at address ADDR (which is
 * ADDR_LEN bytes long).  Returns the number sent, or -1 for errors.  */
extern ssize_t _slurm_sendto (int __fd, __const void *__buf, size_t __n, int __flags, struct sockaddr const * __addr, socklen_t __addr_len)
{
	return sendto ( __fd , __buf , __n , __flags , __addr, __addr_len) ;
}
/* Read N bytes into BUF through socket FD.
 * If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 * the sender, and store the actual size of the address in *ADDR_LEN.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvfrom (int __fd, void *__restrict __buf, size_t __n, int __flags, struct sockaddr * __addr, socklen_t *__restrict __addr_len)
{
	return recvfrom ( __fd , __buf , __n , __flags , __addr, __addr_len) ;
}

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes sent, or -1 for errors.  */
extern ssize_t _slurm_sendmsg (int __fd, __const struct msghdr *__msg, int __flags)
{
	return sendmsg ( __fd , __msg , __flags ) ;
}

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvmsg (int __fd, struct msghdr *__msg, int __flags)
{
	return recvmsg ( __fd , __msg , __flags );
}

/* Put the current value for socket FD's option OPTNAME at protocol level LEVEL
 * into OPTVAL (which is *OPTLEN bytes long), and set *OPTLEN to the value's
 * actual length.  Returns 0 on success, -1 for errors.  */
extern int _slurm_getsockopt (int __fd, int __level, int __optname, void *__restrict __optval, socklen_t *__restrict __optlen)
{
	return getsockopt ( __fd , __level , __optname , __optval , __optlen ) ;
}

/* Set socket FD's option OPTNAME at protocol level LEVEL
 * to *OPTVAL (which is OPTLEN bytes long).
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_setsockopt (int __fd, int __level, int __optname, __const void *__optval, socklen_t __optlen)
{
	return setsockopt ( __fd , __level , __optname , __optval , __optlen ) ;
}


/* Prepare to accept connections on socket FD.
 * N connection requests will be queued before further requests are refused.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_listen (int __fd, int __n)
{
	return listen ( __fd , __n ) ;
}

/* Await a connection on socket FD.
 * When a connection arrives, open a new socket to communicate with it,
 * set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
 * peer and *ADDR_LEN to the address's actual length, and return the
 * new socket's descriptor, or -1 for errors.  */
extern int _slurm_accept (int __fd, struct sockaddr * __addr, socklen_t *__restrict __addr_len)
{
	return accept ( __fd , __addr , __addr_len ) ;
}

/* Shut down all or part of the connection open on socket FD.
 * HOW determines what to shut down:
 * SHUT_RD   = No more receptions;
 * SHUT_WR   = No more transmissions;
 * SHUT_RDWR = No more receptions or transmissions.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_shutdown (int __fd, int __how)
{
	return shutdown ( __fd , __how );
}

extern int _slurm_close (int __fd )
{
	return close ( __fd ) ;
}

extern int _slurm_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	return select ( n , readfds , writefds , exceptfds , timeout ) ;
}
/*
   extern int _slurm_pselect(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, sigset_t * sigmask)
   {
   return pselect ( n , readfds , writefds , exceptfds , timeout , sigmask ) ;
   }
   */
extern void _slurm_FD_CLR(int fd, fd_set *set)
{
	FD_CLR ( fd , set ) ;
}
extern int _slurm_FD_ISSET(int fd, fd_set *set)
{
	return FD_ISSET ( fd , set ) ;
}
extern void _slurm_FD_SET(int fd, fd_set *set)
{
	FD_SET ( fd , set ) ;
}
extern void _slurm_FD_ZERO(fd_set *set)
{
	FD_ZERO ( set ) ;
}

extern int _slurm_fcntl(int fd, int cmd, ... )
{
	int rc ;
	va_list va ;

	va_start ( va , cmd ) ;
	rc =_slurm_vfcntl ( fd , cmd , va ) ;
	va_end ( va ) ;
	return rc ;
}

extern int _slurm_vfcntl(int fd, int cmd, va_list va )
{
	long arg ;

	switch ( cmd )
	{
		case F_GETFL :
			return fcntl ( fd , cmd ) ;
			break ;
		case F_SETFL :
			arg = va_arg ( va , long ) ;
			return fcntl ( fd , cmd , arg) ;
			break ;
		default :
			return SLURM_PROTOCOL_ERROR ;
			break ;
	}
}

/* sets the fields of a slurm_addr */
void _slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address )
{
	slurm_address -> sin_family = AF_SLURM ;
	slurm_address -> sin_port = htons ( port ) ;
	slurm_address -> sin_addr.s_addr = htonl ( ip_address ) ;
}

/* resets the address field of a slurm_addr, port and family remain unchanged */
void _reset_slurm_addr ( slurm_addr * slurm_address , slurm_addr new_address )
{
	slurm_address -> sin_addr.s_addr = new_address.sin_addr.s_addr ;
}

/* sets the fields of a slurm_addr */
void _slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	_slurm_set_addr_char ( slurm_address , port , host ) ;		
}
void _slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	/* If NULL hostname passed in, we only update the port
	 * of slurm_address
	 */
	if (host != NULL) {
		struct hostent * host_info = gethostbyname( host );
		if (host_info == NULL) {
			error ("gethostbyname failure on %s", host);
			slurm_address->sin_family = 0;
			slurm_address->sin_port = 0;
			return;
		} 
		memcpy ( & slurm_address -> sin_addr . s_addr , 
			host_info -> h_addr , host_info -> h_length ) ;
	}
	slurm_address -> sin_family = AF_SLURM ;
	slurm_address -> sin_port = htons ( port ) ;
}

void _slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , unsigned int buf_len )
{
	struct hostent * host_info = gethostbyaddr ( ( char * ) &( slurm_address -> sin_addr . s_addr ) , sizeof ( slurm_address ->  sin_addr . s_addr ) , AF_SLURM ) ;
	*port = slurm_address -> sin_port ;
	strncpy ( host , host_info -> h_name , buf_len ) ;
}

void _slurm_print_slurm_addr ( slurm_addr * address, char *buf, size_t n )
{
	char addrbuf[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &address->sin_addr, addrbuf, INET_ADDRSTRLEN);
	/* warning: silently truncates */
	snprintf(buf, n, "%s:%d", addrbuf, ntohs(address->sin_port));
}
	
void _slurm_pack_slurm_addr ( slurm_addr * slurm_address , Buf buffer )
{
	pack32 ( ntohl ( slurm_address -> sin_addr.s_addr ) , buffer ) ;
	pack16 ( ntohs ( slurm_address -> sin_port ) , buffer ) ;
}

int _slurm_unpack_slurm_addr_no_alloc ( slurm_addr * slurm_address , Buf buffer )
{
	slurm_address -> sin_family = AF_SLURM ;
	safe_unpack32 ( & slurm_address -> sin_addr.s_addr , buffer ) ;
	slurm_address -> sin_addr.s_addr = htonl ( slurm_address -> sin_addr.s_addr );
	safe_unpack16 ( & slurm_address -> sin_port , buffer ) ;
	slurm_address -> sin_port = htons ( slurm_address -> sin_port ) ;
	return SLURM_SUCCESS;

    unpack_error:
	return SLURM_ERROR;
}
