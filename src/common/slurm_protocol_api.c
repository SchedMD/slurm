/* GLOBAL INCLUDES */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* PROJECT INCLUDES */
#include <src/common/slurm_protocol_interface.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/xmalloc.h>
#include <src/common/log.h>

/* EXTERNAL VARIABLES */

/* #DEFINES */


/************************/
/***** msg functions */
/************************/

/* In the socket implementation it creates a socket, binds to it, and listens for connections.
 * In the mongo implemenetation is should just create a mongo socket , binds and return.
 * slurm_address 	- for now it is really just a sockaddr_in
 * slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine_port ( uint16_t port )
{
	slurm_addr slurm_address ;
	slurm_set_addr_any ( &slurm_address , port ) ;
	return _slurm_init_msg_engine ( & slurm_address ) ;
}

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
 * int	- the return code
 */
int slurm_shutdown_msg_engine ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

/* just calls close on an established msg connection
 * open_fd	- an open file descriptor
 * int	- the return code
 */
int slurm_shutdown_msg_conn ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

/* In the bsd implementation it creates a SOCK_STREAM socket and calls connect on it
 * In the mongo implementation it creates a SOCK_DGRAM socket and calls connect on it
 * a SOCK_DGRAM socket called with connect is defined to only receive messages from the address/port pair argument of the connect call
 * slurm_address 	- for now it is really just a sockaddr_in
 * int	- the return code
 */
slurm_fd slurm_open_msg_conn ( slurm_addr * slurm_address ) 
{
	return _slurm_open_msg_conn ( slurm_address ) ;
}

/* calls connect to make a connection-less datagram connection to the the primary or secondary slurmctld message engine
 * slurm_address 	- for now it is really just a sockaddr_in
 * int	- the return code
 */
slurm_fd slurm_open_controller_conn ( )
{
	slurm_fd connection_fd ;
	slurm_addr primary_destination_address ;
	slurm_addr secondary_destination_address ;

	/* set slurm_addr structures */
	slurm_set_addr ( & primary_destination_address , SLURM_PORT , PRIMARY_SLURM_CONTROLLER ) ;
	slurm_set_addr ( & secondary_destination_address , SLURM_PORT , SECONDARY_SLURM_CONTROLLER ) ;
	
	/* try to send to primary first then secondary */	
	if ( ( connection_fd = slurm_open_msg_conn ( & primary_destination_address ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Send message to primary controller failed" ) ;
		
		if ( ( connection_fd = slurm_open_msg_conn ( & secondary_destination_address ) ) ==  SLURM_SOCKET_ERROR )	
		{
			debug ( "Send messge to secondary controller failed" ) ;
		}
	}
	return connection_fd ;
}

/* In the bsd implmentation maps directly to a accept call 
 * In the mongo it returns the open_fd and is essentially a no-op function call
 * open_fd		- file descriptor to accept connection on
 * slurm_address 	- for now it is really just a sockaddr_in
 * int	- the return code
 */
slurm_fd slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address ) 
{
	return _slurm_accept_msg_conn ( open_fd , slurm_address ) ;
}

/* In the bsd implmentation maps directly to a close call, to close the socket that was accepted
 * In the mongo it is a no-op (  slurm_shutdown_msg_engine should be called to close the mongo socket since there is no "accept" phase for datagrams
 * open_fd		- file descriptor to accept connection on
 * int	- the return code
 */
int slurm_close_accepted_conn ( slurm_fd open_fd ) 
{
	return _slurm_close_accepted_conn ( open_fd ) ;
}

/***** recv msg functions */
/*
 * note that a memory is allocated for the returned msg and must be freed at some point 
 * open_fd 		- file descriptor to receive msg on
 * msg 		- a slurm msg struct
 * int		- size of msg received in bytes before being unpacked
 */
int slurm_receive_msg ( slurm_fd open_fd , slurm_msg_t * msg ) 
{
	char buftemp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	int rc ;
	unsigned int unpack_len ;
	unsigned int receive_len = MAX_MESSAGE_BUFFER_SIZE ;

	if ( ( rc = _slurm_msg_recvfrom ( open_fd , buffer , receive_len, NO_SEND_RECV_FLAGS , & msg->address ) ) == SLURM_SOCKET_ERROR ) 
	{
		debug ( "Error recieving msg socket: errno %i\n", errno ) ;
		return rc ;
	}

	/* unpack header */
	unpack_len = rc ;
	unpack_header ( &header , & buffer , & unpack_len ) ;

	if ( (rc = check_header_version ( & header ) ) < 0 ) 
	{
		return rc;
	}

	/* unpack msg body */
	msg -> msg_type = header . msg_type ;
	unpack_msg ( msg , & buffer , & unpack_len ) ;

	return rc ;
}

/***** send msg functions */
/* sends a slurm_protocol msg to the slurmctld based on location information retrieved from the slurmd.conf
 * if unable to contant the primary slurmctld attempts will be made to contact the backup controller
 * 
 * open_fd	- file descriptor to send msg on
 * msg	- a slurm msg struct
 * int	- size of msg sent in bytes
 */
int slurm_send_controller_msg ( slurm_fd open_fd , slurm_msg_t * msg )
{
	int rc ;
	slurm_addr primary_destination_address ;
	slurm_addr secondary_destination_address ;

	/* set slurm_addr structures */
	slurm_set_addr ( & primary_destination_address , SLURM_PORT , PRIMARY_SLURM_CONTROLLER ) ;
	slurm_set_addr ( & secondary_destination_address , SLURM_PORT , SECONDARY_SLURM_CONTROLLER ) ;
	
	/* try to send to primary first then secondary */	
	msg -> address = primary_destination_address ;
	if ( (rc = slurm_send_node_msg ( open_fd , msg ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Send message to primary controller failed" ) ;
		msg -> address = secondary_destination_address ;
		if ( (rc = slurm_send_node_msg ( open_fd , msg ) ) ==  SLURM_SOCKET_ERROR )	
		{
			debug ( "Send messge to secondary controller failed" ) ;
		}
	}
	return rc ;
}

/* sends a message to an arbitrary node
 *
 * open_fd		- file descriptor to send msg on
 * msg			- a slurm msg struct
 * int		- size of msg sent in bytes
 */
int slurm_send_node_msg ( slurm_fd open_fd ,  slurm_msg_t * msg )
{
	char buf_temp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	int rc ;
	unsigned int pack_len ;

	/* initheader */
	init_header ( & header , msg->msg_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = MAX_MESSAGE_BUFFER_SIZE ;
	pack_header ( &header , & buffer , & pack_len ) ;

	/* pack msg */
	pack_msg ( msg , & buffer , & pack_len ) ;

	/* send msg */
	if (  ( rc = _slurm_msg_sendto ( open_fd , buf_temp , MAX_MESSAGE_BUFFER_SIZE - pack_len , NO_SEND_RECV_FLAGS , &msg->address ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error sending msg socket: errno %i\n", errno ) ;
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
 * int             - size of msg received in bytes
 */
int slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_type_t * msg_type , char * data_buffer , size_t buf_len )
{
	char buftemp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	int rc ;
       	int bytes_read;
	unsigned int unpack_len ;
	unsigned int receive_len = MAX_MESSAGE_BUFFER_SIZE ;

	if ( ( rc = _slurm_msg_recvfrom ( open_fd , buffer , receive_len, NO_SEND_RECV_FLAGS , source_address ) ) == SLURM_SOCKET_ERROR ) ;
	{
		debug ( "Error recieving msg socket: errno %i\n", errno ) ;
		return rc ;
	}

	/* unpack header */
	bytes_read = rc ;
	unpack_len = rc ;
	unpack_header ( &header , & buffer , & unpack_len ) ;

	rc = check_header_version ( & header ) ;
	if ( rc < 0 ) return rc ;
	*msg_type = header . msg_type ;
	data_buffer = buffer ;
	return bytes_read ;
}

/*
 *
 * open_fd              - file descriptor to send buffer on
 * msg_type             - type of msg to be sent ( see slurm_protocol_defs.h for msg types )
 * data_buffer          - buffer to be sent
 * buf_len              - length of buffer to be sent
 * int             - size of msg sent in bytes
 */
int slurm_send_controller_buffer ( slurm_fd open_fd , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len )
{
	int rc ;
	slurm_addr primary_destination_address ;
	slurm_addr secondary_destination_address ;

	/* set slurm_addr structures */
	slurm_set_addr ( & primary_destination_address , SLURM_PORT , PRIMARY_SLURM_CONTROLLER ) ;
	slurm_set_addr ( & secondary_destination_address , SLURM_PORT , SECONDARY_SLURM_CONTROLLER ) ;

	/* try to send to primary first then secondary */	
	if ( ( rc = slurm_send_node_buffer ( open_fd , & primary_destination_address , msg_type , data_buffer , buf_len ) ) == SLURM_SOCKET_ERROR )	
	{
		debug ( "Send message to primary controller failed" ) ;
		
		if ( ( rc = slurm_send_node_buffer ( open_fd , & secondary_destination_address , msg_type , data_buffer , buf_len ) ) == SLURM_SOCKET_ERROR )
		{
			debug ( "Send messge to secondary controller failed" ) ;
		}
	}
	return rc ;
}

/* sends a buffer to an arbitrary node
 *
 * open_fd		- file descriptor to send buffer on
 * destination_address	- address of destination nodes
 * msg_type		- type of msg to be sent ( see slurm_protocol_defs.h for msg types )
 * data_buffer		- buffer to be sent
 * buf_len		- length of buffer to be sent 
 * int		- size of msg sent in bytes
 */
int slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len )
{
	char buf_temp[MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	unsigned int rc ;
	unsigned int pack_len ;

	/* initheader */
	init_header ( & header , msg_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = MAX_MESSAGE_BUFFER_SIZE ;
	pack_header ( &header, & buffer , & pack_len ) ;

	/* pack msg */
	memcpy ( buffer , data_buffer , buf_len ) ;
	pack_len -= buf_len ;

	if ( ( rc = _slurm_msg_sendto ( open_fd , buf_temp , MAX_MESSAGE_BUFFER_SIZE - pack_len , NO_SEND_RECV_FLAGS , destination_address ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error sending msg socket: errno %i", errno ) ;
	}
	return rc ;
}

/************************/
/***** stream functions */
/************************/
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

int slurm_close_stream ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}

/************************/
/***** slurm addr functions */
/************************/
/* sets/gets the fields of a slurm_addr */
void slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address )
{
	_slurm_set_addr_uint ( slurm_address , port , ip_address ) ;
}

void slurm_set_addr_any ( slurm_addr * slurm_address , uint16_t port ) 
{
	_slurm_set_addr_uint ( slurm_address , port , SLURM_INADDR_ANY ) ;
}

void slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	_slurm_set_addr ( slurm_address , port , host ) ;
}

void slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	_slurm_set_addr_char ( slurm_address , port , host ) ;
}

void slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , unsigned int buf_len )
{
	_slurm_get_addr ( slurm_address , port , host , buf_len ) ;
}

/* slurm msg type */
/* frees the inner message data then frees the msg struct */
void slurm_msg_destroy ( slurm_msg_t * location , int destroy_data )
{
	if ( destroy_data )
	{
		free ( location->data ) ;
	}
	free ( location ) ;
}
