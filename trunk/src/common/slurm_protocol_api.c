#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <src/common/slurm_protocol_interface.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/xmalloc.h>


extern uint32_t debug ;

extern int errno ;

/***** high level routines */
/* message functions */

/* In the socket implementation it creates a socket, binds to it, and listens for connections.
 * In the mongo implemenetation is should just create a mongo socket , bind and return.
 * slurm_address 	- for now it is really just a sockaddr_in
 * slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_init_message_engine ( slurm_addr * slurm_address )
{
	return _slurm_init_message_engine ( slurm_address ) ;
}

/* just calls close on an established message connection
 * open_fd	- an open file descriptor
 * uint32_t	- the return code
 */
uint32_t slurm_shutdown_message_engine ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

/***** recv message functions */
/*
 * note that a memory is allocated for the returned message and must be freed at some point 
 * open_fd 		- file descriptor to receive message on
 * source_address 	- address of the source of the message for now it is really just a sockaddr_in
 * message 		- a slurm message struct
 * uint32_t		- size of message received in bytes
 */
uint32_t slurm_receive_message ( slurm_fd open_fd , slurm_addr * source_address , slurm_message_t ** message ) 
{
	char buftemp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	slurm_message_t * new_message ;
	int32_t rc ;
	uint32_t unpack_len ;
	uint32_t receive_len = MAX_MESSAGE_BUFFER_SIZE ;

	rc = _slurm_message_recvfrom ( open_fd , buffer , receive_len, NO_SEND_RECV_FLAGS , source_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error recieving message socket: errno %i\n", errno ) ;
		}
		return rc ;
	}

	/* unpack header */
	unpack_len = receive_len ;
	unpack_header ( & buffer , & unpack_len , & header ) ;

	rc = check_header_version ( & header ) ;
	if ( rc < 0 ) return rc ;

	/* unpack message body */
	new_message = xmalloc ( sizeof ( slurm_message_t ) ) ;
	new_message -> message_type = header . message_type ;
	unpack_message ( & buffer , & unpack_len , new_message ) ;

	*message = new_message ;	
	return receive_len ;
}

/***** send message functions */
/* sends a slurm_protocol message to the slurmctld based on location information retrieved from the slurmd.conf
 * if unable to contant the primary slurmctld attempts will be made to contact the backup controller
 * 
 * open_fd	- file descriptor to send message on
 * message_type	- type of message to be sent ( see slurm_protocol_defs.h for message types )
 * message	- a slurm message struct
 * uint32_t	- size of message sent in bytes
 */
uint32_t slurm_send_server_message ( slurm_fd open_fd , slurm_message_type_t message_type , slurm_message_t const * message )
{
	return SLURM_NOT_IMPLEMENTED ;	
}

uint32_t slurm_send_node_message ( slurm_fd open_fd , slurm_addr * destination_address , slurm_message_type_t message_type , slurm_message_t const * message )
{
	char buf_temp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	uint32_t rc ;
	uint32_t pack_len ;

	/* initheader */
	init_header ( & header , message_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = 0 ;
	pack_header ( & buffer , & pack_len , & header ) ;

	/* pack message */
	pack_message ( & buffer , & pack_len , message ) ;

	/* send message */
	rc = _slurm_message_sendto ( open_fd , buf_temp , pack_len , NO_SEND_RECV_FLAGS , destination_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error sending message socket: errno %i\n", errno ) ;
		}
		return rc ;
	}
	return pack_len ;
}

uint32_t slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_message_type_t * message_type , char * data_buffer , size_t buf_len )
{
	char buftemp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	int32_t rc ;
	uint32_t unpack_len ;
	uint32_t receive_len = MAX_MESSAGE_BUFFER_SIZE ;

	rc = _slurm_message_recvfrom ( open_fd , buffer , receive_len, NO_SEND_RECV_FLAGS , source_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error recieving message socket: errno %i\n", errno ) ;
		}
		return rc ;
	}

	/* unpack header */
	unpack_len = rc ;
	unpack_header ( & buffer , & unpack_len , & header ) ;

	rc = check_header_version ( & header ) ;
	if ( rc < 0 ) return rc ;
	*message_type = header . message_type ;
	*data_buffer = buffer ;
	return unpack_len ;
}

uint32_t slurm_send_server_buffer ( slurm_fd open_fd , slurm_message_type_t message_type , char * data_buffer , size_t buf_len )
{
	return SLURM_NOT_IMPLEMENTED ;	
}

uint32_t slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_message_type_t message_type , char * data_buffer , size_t buf_len )
{
	char buf_temp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	uint32_t rc ;
	uint32_t pack_len ;

	/* initheader */
	init_header ( & header , message_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = 0 ;
	pack_header ( & buffer , & pack_len , & header ) ;

	/* pack message */
	memcpy ( buffer , data_buffer , buf_len ) ;
	pack_len += buf_len ;

	rc = _slurm_message_sendto ( open_fd , buf_temp , pack_len , NO_SEND_RECV_FLAGS , destination_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		if ( debug )
		{
			fprintf( stderr, "Error sending message socket: errno %i", errno ) ;
		}
		return rc ;
	}
	return rc ;
}

/***** stream functions */
slurm_fd slurm_listen_stream ( slurm_addr * slurm_address )
{
	return _slurm_listen_stream ( slurm_address ) ;
}

slurm_fd slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address )
{
	return _slurm_accept_stream ( open_fd , slurm_address ) ;
}

slurm_fd slurm_open_stream ( slurm_addr * slurm_address )
{
	return _slurm_open_stream ( slurm_address ) ;
}

size_t slurm_write_stream ( slurm_fd open_fd , char * buffer , size_t size )
{
	return _slurm_send ( open_fd , buffer , size , NO_SEND_RECV_FLAGS ) ;
}

size_t slurm_read_stream ( slurm_fd open_fd , char * buffer , size_t size )
{
	return _slurm_recv ( open_fd , buffer , size , NO_SEND_RECV_FLAGS ) ;
}

uint32_t slurm_close_stream ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}
