#include <stdlib.h>
#include <string.h>

#include <src/common/slurm_protocol_interface.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/xmalloc.h>
#include <src/common/log.h>

extern int errno ;

/***** high level routines */
/* msg functions */

/* In the socket implementation it creates a socket, binds to it, and listens for connections.
 * In the mongo implemenetation is should just create a mongo socket , bind and return.
 * slurm_address 	- for now it is really just a sockaddr_in
 * slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine ( slurm_addr * slurm_address )
{
	return _slurm_init_msg_engine ( slurm_address ) ;
}

/* just calls close on an established msg connection
 * open_fd	- an open file descriptor
 * uint32_t	- the return code
 */
uint32_t slurm_shutdown_msg_engine ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

/***** recv msg functions */
/*
 * note that a memory is allocated for the returned msg and must be freed at some point 
 * open_fd 		- file descriptor to receive msg on
 * source_address 	- address of the source of the msg for now it is really just a sockaddr_in
 * msg 		- a slurm msg struct
 * uint32_t		- size of msg received in bytes before being unpacked
 */
uint32_t slurm_receive_msg ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_t ** msg ) 
{
	char buftemp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	slurm_msg_t * new_msg ;
	int32_t rc ;
	uint32_t unpack_len ;
	uint32_t receive_len = MAX_MESSAGE_BUFFER_SIZE ;

	rc = _slurm_msg_recvfrom ( open_fd , buffer , receive_len, NO_SEND_RECV_FLAGS , source_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		debug ( "Error recieving msg socket: errno %i\n", errno ) ;
		return rc ;
	}

	/* unpack header */
	unpack_len = rc ;
	unpack_header ( & buffer , & unpack_len , & header ) ;

	rc = check_header_version ( & header ) ;
	if ( rc < 0 ) return rc ;

	/* unpack msg body */
	new_msg = xmalloc ( sizeof ( slurm_msg_t ) ) ;
	new_msg -> msg_type = header . msg_type ;
	unpack_msg ( & buffer , & unpack_len , new_msg ) ;

	*msg = new_msg ;	
	return rc ;
}

/***** send msg functions */
/* sends a slurm_protocol msg to the slurmctld based on location information retrieved from the slurmd.conf
 * if unable to contant the primary slurmctld attempts will be made to contact the backup controller
 * 
 * open_fd	- file descriptor to send msg on
 * msg_type	- type of msg to be sent ( see slurm_protocol_defs.h for msg types )
 * msg	- a slurm msg struct
 * uint32_t	- size of msg sent in bytes
 */
uint32_t slurm_send_controller_msg ( slurm_fd open_fd , slurm_msg_type_t msg_type , slurm_msg_t const * msg )
{
	return SLURM_NOT_IMPLEMENTED ;	
}

/* sends a message to an arbitrary node
 *
 * open_fd		- file descriptor to send msg on
 * destination_address	- address of destination nodes
 * msg_type		- type of msg to be sent ( see slurm_protocol_defs.h for msg types )
 * msg			- a slurm msg struct
 * uint32_t		- size of msg sent in bytes
 */
uint32_t slurm_send_node_msg ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , slurm_msg_t const * msg )
{
	char buf_temp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	uint32_t rc ;
	uint32_t pack_len ;

	/* initheader */
	init_header ( & header , msg_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = MAX_MESSAGE_BUFFER_SIZE ;
	pack_header ( & buffer , & pack_len , & header ) ;

	/* pack msg */
	pack_msg ( & buffer , & pack_len , msg ) ;

	/* send msg */
	rc = _slurm_msg_sendto ( open_fd , buf_temp , MAX_MESSAGE_BUFFER_SIZE - pack_len , NO_SEND_RECV_FLAGS , destination_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		debug ( "Error sending msg socket: errno %i\n", errno ) ;
		return rc ;
	}
	return rc ;
}

/*
 *
 * open_fd              - file descriptor to receive msg on
 * destination_address  - address of destination nodes
 * msg_type             - type of msg to be sent ( see slurm_protocol_defs.h for msg types )
 * data_buffer		- buffer for data to be received into
 * buf_len		- length of data buffer 
 * uint32_t             - size of msg received in bytes
 */
uint32_t slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_type_t * msg_type , char * data_buffer , size_t buf_len )
{
	char buftemp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	int32_t rc , bytes_read;
	uint32_t unpack_len ;
	uint32_t receive_len = MAX_MESSAGE_BUFFER_SIZE ;

	rc = _slurm_msg_recvfrom ( open_fd , buffer , receive_len, NO_SEND_RECV_FLAGS , source_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		debug ( "Error recieving msg socket: errno %i\n", errno ) ;
		return rc ;
	}

	/* unpack header */
	bytes_read = rc ;
	unpack_len = rc ;
	unpack_header ( & buffer , & unpack_len , & header ) ;

	rc = check_header_version ( & header ) ;
	if ( rc < 0 ) return rc ;
	*msg_type = header . msg_type ;
	data_buffer = buffer ;
	return bytes_read ;
}

uint32_t slurm_send_controller_buffer ( slurm_fd open_fd , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len )
{
	return SLURM_NOT_IMPLEMENTED ;	
}

/* sends a buffer to an arbitrary node
 *
 * open_fd		- file descriptor to send msg on
 * destination_address	- address of destination nodes
 * msg_type		- type of msg to be sent ( see slurm_protocol_defs.h for msg types )
 * data_buffer		- buffer to be sent
 * buf_len		- length of buffer to be sent 
 * uint32_t		- size of msg sent in bytes
 */
uint32_t slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len )
{
	char buf_temp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	uint32_t rc ;
	uint32_t pack_len ;

	/* initheader */
	init_header ( & header , msg_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = MAX_MESSAGE_BUFFER_SIZE ;
	pack_header ( & buffer , & pack_len , & header ) ;

	/* pack msg */
	memcpy ( buffer , data_buffer , buf_len ) ;
	pack_len -= buf_len ;

	rc = _slurm_msg_sendto ( open_fd , buf_temp , MAX_MESSAGE_BUFFER_SIZE - pack_len , NO_SEND_RECV_FLAGS , destination_address ) ;
	if ( rc == SLURM_SOCKET_ERROR )
	{
		debug ( "Error sending msg socket: errno %i", errno ) ;
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

/* sets the fields of a slurm_addr */
void slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address )
{
	_slurm_set_addr_uint ( slurm_address , port , ip_address ) ;
}

void slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	_slurm_set_addr ( slurm_address , port , host ) ;
}

void slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	_slurm_set_addr_char ( slurm_address , port , host ) ;
}

void slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , uint32_t buf_len )
{
	_slurm_get_addr ( slurm_address , port , host , buf_len ) ;
}
