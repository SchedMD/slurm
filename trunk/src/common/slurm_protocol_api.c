/* GLOBAL INCLUDES */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

/* PROJECT INCLUDES */
#include <src/common/parse_spec.h>
#include <src/common/slurm_protocol_interface.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/xmalloc.h>
#include <src/common/log.h>

/* EXTERNAL VARIABLES */

/* #DEFINES */
#define BUF_SIZE 1024

/* STATIC VARIABLES */
static slurm_protocol_config_t proto_conf_default ;
static slurm_protocol_config_t * proto_conf = & proto_conf_default ;
static slurm_ctl_conf_t slurmctld_conf;

/************************/
/***** API init functions */
/************************/
int slurm_set_api_config ( slurm_protocol_config_t * protocol_conf )
{
	proto_conf = protocol_conf ;
	return SLURM_SUCCESS ;
}

slurm_protocol_config_t * slurm_get_api_config ( )
{
	return proto_conf ;
}

int slurm_api_set_default_config ( )
{
	if ( (slurmctld_conf . control_machine == NULL) ||
	     (slurmctld_conf . backup_controller == NULL) ||
	     (slurmctld_conf . slurmctld_port == 0) )
		read_slurm_port_config ( );

	slurm_set_addr ( & proto_conf_default . primary_controller , 
		slurmctld_conf . slurmctld_port ,
		slurmctld_conf . control_machine ) ;
	slurm_set_addr ( & proto_conf_default . secondary_controller , 
		slurmctld_conf . slurmctld_port ,
		slurmctld_conf . backup_controller ) ;
	proto_conf = & proto_conf_default ;

	return SLURM_SUCCESS ;
}

/*
 * read_slurm_port_config - get the slurmctld and slurmd port numbers
 * NOTE: slurmctld and slurmd ports are built thus:
 *	if SLURMCTLD_PORT/SLURMD_PORT are set then
 *		get the port number based upon a look-up in /etc/services
 *		if the lookup fails, translate SLURMCTLD_PORT/SLURMD_PORT into a number
 *	These port numbers are over-ridden if set in the configuration file
 */
int read_slurm_port_config ( )
{
	FILE *slurm_spec_file;	/* pointer to input data file */
	char in_line[BUF_SIZE];	/* input line */
	char * control_machine = NULL;
	char * backup_controller = NULL;
	int error_code, i, j, line_num = 0;
	int slurmctld_port = 0, slurmd_port = 0;
	struct servent *servent;

	servent = getservbyname (SLURMCTLD_PORT, NULL);
	if (servent)
		slurmctld_conf.slurmctld_port   = servent -> s_port;
	else
		slurmctld_conf.slurmctld_port   = strtol (SLURMCTLD_PORT, (char **) NULL, 10);
	endservent ();

	servent = getservbyname (SLURMD_PORT, NULL);
	if (servent)
		slurmctld_conf.slurmd_port   = servent -> s_port;
	else
		slurmctld_conf.slurmd_port   = strtol (SLURMD_PORT, (char **) NULL, 10);
	endservent ();

	slurm_spec_file = fopen (SLURM_CONFIG_FILE, "r");
	if (slurm_spec_file == NULL) {
		error ( "read_slurm_conf error %d opening file %s", 
			errno, SLURM_CONFIG_FILE);
		return SLURM_ERROR ;
	}

	while (fgets (in_line, BUF_SIZE, slurm_spec_file) != NULL) {
		line_num++;
		if (strlen (in_line) >= (BUF_SIZE - 1)) {
			error ("read_slurm_conf line %d, of input file %s too long\n",
				 line_num, SLURM_CONFIG_FILE);
			fclose (slurm_spec_file);
			return E2BIG;
			break;
		}		


		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		for (i = 0; i < BUF_SIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {	/* escaped "#" */
				for (j = i; j < BUF_SIZE; j++) {
					in_line[j - 1] = in_line[j];
				}	
				continue;
			}	
			in_line[i] = (char) NULL;
			break;
		}		

		/* parse what is left */
		/* overall slurm configuration parameters */
		error_code = slurm_parser(in_line,
			"ControlMachine=", 's', &control_machine, 
			"BackupController=", 's', &backup_controller, 
			"SlurmctldPort=", 'd', &slurmctld_port,
			"SlurmdPort=", 'd', &slurmd_port,
			"END");
		if (error_code) {
			fclose (slurm_spec_file);
			return error_code;
		}		

		if ( slurmctld_conf.control_machine == NULL )
			slurmctld_conf.control_machine = control_machine;
		if ( slurmctld_conf.backup_controller == NULL )
			slurmctld_conf.backup_controller = backup_controller;
		if (slurmctld_port)
			slurmctld_conf.slurmctld_port = slurmctld_port;
		if (slurmd_port)
			slurmctld_conf.slurmd_port = slurmd_port;
	}			
	fclose (slurm_spec_file);
	return SLURM_SUCCESS;
}

int slurm_set_default_controllers ( char * primary_controller_hostname , char * secondary_controller_hostname, uint16_t pri_port , uint16_t sec_port )
{
	slurm_set_addr ( & proto_conf_default . primary_controller , pri_port , primary_controller_hostname ) ;
	slurm_set_addr ( & proto_conf_default . secondary_controller , sec_port , secondary_controller_hostname ) ;

	return SLURM_SUCCESS ;
}

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
 * open_fd		- an open file descriptor
 * int 			- the return code
 */
int slurm_shutdown_msg_engine ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

/* just calls close on an established msg connection
 * open_fd		- an open file descriptor
 * int			- the return code
 */
int slurm_shutdown_msg_conn ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

/* In the bsd implementation it creates a SOCK_STREAM socket and calls connect on it
 * In the mongo implementation it creates a SOCK_DGRAM socket and calls connect on it
 * a SOCK_DGRAM socket called with connect is defined to only receive messages from the address/port pair argument of the connect call
 * slurm_address 	- for now it is really just a sockaddr_in
 * int			- the return code
 */
slurm_fd slurm_open_msg_conn ( slurm_addr * slurm_address ) 
{
	return _slurm_open_msg_conn ( slurm_address ) ;
}

/* calls connect to make a connection-less datagram connection to the the primary or secondary slurmctld message engine
 * slurm_address 	- for now it is really just a sockaddr_in
 * int			- the return code
 */
slurm_fd slurm_open_controller_conn ( )
{
	slurm_fd connection_fd ;
	slurm_api_set_default_config ( ) ;
	/* try to send to primary first then secondary */	
	if ( ( connection_fd = slurm_open_msg_conn ( & proto_conf -> primary_controller ) ) == SLURM_SOCKET_ERROR )
	{
		int local_errno = errno ;
		debug ( "Open connection to primary controller failed errno: %i", local_errno ) ;
		
		if ( ( connection_fd = slurm_open_msg_conn ( & proto_conf -> secondary_controller ) ) ==  SLURM_SOCKET_ERROR )	
		{
			int local_errno = errno ;
			debug ( "Open connection to secondary controller failed errno: %i", local_errno ) ;
		}
	}
	return connection_fd ;
}

/* In the bsd implmentation maps directly to a accept call 
 * In the mongo it returns the open_fd and is essentially a no-op function call
 * open_fd		- file descriptor to accept connection on
 * slurm_address 	- for now it is really just a sockaddr_in
 * int			- the return code
 */
slurm_fd slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address ) 
{
	return _slurm_accept_msg_conn ( open_fd , slurm_address ) ;
}

/* In the bsd implmentation maps directly to a close call, to close the socket that was accepted
 * In the mongo it is a no-op (  slurm_shutdown_msg_engine should be called to close the mongo socket since there is no "accept" phase for datagrams
 * open_fd		- file descriptor to accept connection on
 * int			- the return code
 */
int slurm_close_accepted_conn ( slurm_fd open_fd ) 
{
	return _slurm_close_accepted_conn ( open_fd ) ;
}

/***** recv msg functions */
/*
 * note that a memory is allocated for the returned msg and must be freed at some point 
 * open_fd		- file descriptor to receive msg on
 * msg			- a slurm msg struct
 * int			- size of msg received in bytes before being unpacked
 */
int slurm_receive_msg ( slurm_fd open_fd , slurm_msg_t * msg ) 
{
	char buftemp[SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	int rc ;
	unsigned int unpack_len ;
	unsigned int receive_len = SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE ;

	if ( ( rc = _slurm_msg_recvfrom ( open_fd , buffer , receive_len, SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , & (msg)->address ) ) == SLURM_SOCKET_ERROR ) 
	{
		int local_errno = errno ;
		debug ( "Error receiving msg socket: errno %i", local_errno ) ;
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
	(msg) -> msg_type = header . msg_type ;
	unpack_msg ( msg , & buffer , & unpack_len ) ;

	return rc ;
}

/***** send msg functions */
/* sends a slurm_protocol msg to the slurmctld based on location information retrieved from the slurmd.conf
 * if unable to contant the primary slurmctld attempts will be made to contact the backup controller
 * 
 * open_fd		- file descriptor to send msg on
 * msg			- a slurm msg struct
 * int			- size of msg sent in bytes
 */
int slurm_send_controller_msg ( slurm_fd open_fd , slurm_msg_t * msg )
{
	int rc ;
	/* try to send to primary first then secondary */	
	msg -> address = proto_conf -> primary_controller ; 
	if ( (rc = slurm_send_node_msg ( open_fd , msg ) ) == SLURM_SOCKET_ERROR )
	{
		int local_errno = errno ;
		debug ( "Send message to primary controller failed errno: %i", local_errno ) ;
		msg -> address = proto_conf -> secondary_controller ;
		if ( (rc = slurm_send_node_msg ( open_fd , msg ) ) ==  SLURM_SOCKET_ERROR )	
		{
			int local_errno = errno ;
			debug ( "Send messge to secondary controller failed errno: %i", local_errno ) ;
		}
	}
	return rc ;
}

/* sends a message to an arbitrary node
 *
 * open_fd		- file descriptor to send msg on
 * msg			- a slurm msg struct
 * int			- size of msg sent in bytes
 */
int slurm_send_node_msg ( slurm_fd open_fd ,  slurm_msg_t * msg )
{
	char buf_temp[SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	int rc ;
	unsigned int pack_len ;

	/* initheader */
	init_header ( & header , msg->msg_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE ;
	pack_header ( &header , & buffer , & pack_len ) ;

	/* pack msg */
	pack_msg ( msg , & buffer , & pack_len ) ;

	/* send msg */
	if (  ( rc = _slurm_msg_sendto ( open_fd , buf_temp , SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE - pack_len , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , &msg->address ) ) == SLURM_SOCKET_ERROR )
	{
		int local_errno = errno ;
		debug ( "Error sending msg socket: errno %i", local_errno ) ;
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
 * int			- size of msg received in bytes
 */
int slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_type_t * msg_type , char * data_buffer , size_t buf_len )
{
	char buftemp[SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buftemp ;
	header_t header ;
	int rc ;
	unsigned int unpack_len ; /* length left to upack */
	unsigned int receive_len = SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE ; /* buffer size */

	if ( ( rc = _slurm_msg_recvfrom ( open_fd , buffer , receive_len, SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , source_address ) ) == SLURM_SOCKET_ERROR ) ;
	{
		int local_errno = errno ;
		debug ( "Error receiving msg socket: errno %i", local_errno ) ;
		return rc ;
	}

	/* unpack header */
	unpack_len = rc ;
	unpack_header ( &header , & buffer , & unpack_len ) ;

	/* unpack_header decrements the unpack_len by the size of the header, so 
	 * unpack_len not holds the size of the data left in the buffer */
	if ( ( rc = check_header_version ( & header ) ) < 0 ) 
	{
		return rc ;
	}

	*msg_type = header . msg_type ;
	/* assumes buffer is already allocated by calling function */
	/* *data_buffer = xmalloc ( unpack_len ) ; */
	memcpy ( data_buffer , buffer , unpack_len ) ;
	return unpack_len  ;
}

/*
 *
 * open_fd              - file descriptor to send buffer on
 * msg_type             - type of msg to be sent ( see slurm_protocol_defs.h for msg types )
 * data_buffer          - buffer to be sent
 * buf_len              - length of buffer to be sent
 * int			- size of msg sent in bytes
 */
int slurm_send_controller_buffer ( slurm_fd open_fd , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len )
{
	int rc ;

	/* try to send to primary first then secondary */	
	if ( ( rc = slurm_send_node_buffer ( open_fd ,  & proto_conf -> primary_controller , msg_type , data_buffer , buf_len ) ) == SLURM_SOCKET_ERROR )	
	{

		int local_errno = errno ;
		debug ( "Send message to primary controller failed errno: %i", local_errno ) ;
		if ( ( rc = slurm_send_node_buffer ( open_fd ,  & proto_conf -> secondary_controller , msg_type , data_buffer , buf_len ) ) == SLURM_SOCKET_ERROR )
		{
			int local_errno = errno ;
			debug ( "Send message to secondary controller failed errno: %i", local_errno ) ;
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
 * int			- size of msg sent in bytes
 */
int slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len )
{
	char buf_temp[SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE] ;
	char * buffer = buf_temp ;
	header_t header ;
	unsigned int rc ;
	unsigned int pack_len ;

	/* initheader */
	init_header ( & header , msg_type , SLURM_PROTOCOL_NO_FLAGS ) ;

	/* pack header */
	pack_len = SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE ;
	pack_header ( &header, & buffer , & pack_len ) ;

	/* pack msg */
	memcpy ( buffer , data_buffer , buf_len ) ;
	pack_len -= buf_len ;

	if ( ( rc = _slurm_msg_sendto ( open_fd , buf_temp , SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE - pack_len , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS , destination_address ) ) == SLURM_SOCKET_ERROR )
	{
		int local_errno = errno ;
		debug ( "Error sending msg socket: errno %i", local_errno ) ;
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
	int rc ;
	while ( true )
	{
		if ( ( rc = _slurm_send ( open_fd , buffer , size , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) ) == SLURM_PROTOCOL_ERROR )
		{
			if ( errno == EINTR )
			{
				continue ;
			}
			else
			{
				return rc ;
			}
		}
		else
		{
			return rc ;
		}
	}
}

size_t slurm_read_stream ( slurm_fd open_fd , char * buffer , size_t size )
{
	int rc ;
	while ( true )
	{
		if (( rc = _slurm_recv ( open_fd , buffer , size , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) ) == SLURM_PROTOCOL_ERROR )
		{
			if ( errno == EINTR )
			{
				continue ;
			}
			else
			{
				return rc ;
			}
		}
		else
		{
			return rc ;
		}
	}
}

int slurm_get_stream_addr ( slurm_fd open_fd , slurm_addr * address )
{
	return _slurm_get_stream_addr ( open_fd , address ) ;
}

int slurm_close_stream ( slurm_fd open_fd )
{
	return _slurm_close_stream ( open_fd ) ;
}

int slurm_select(int n, slurm_fd_set *readfds, slurm_fd_set *writefds, slurm_fd_set *exceptfds, struct timeval *timeout)
{
	return _slurm_select(n, readfds, writefds, exceptfds, timeout);
}

void slurm_FD_CLR(slurm_fd fd, slurm_fd_set *set)
{
	return _slurm_FD_CLR(fd, set);
}

int slurm_FD_ISSET(slurm_fd fd, slurm_fd_set *set)
{
	return _slurm_FD_ISSET(fd, set);
}

void slurm_FD_SET(slurm_fd fd, slurm_fd_set *set)
{
	return _slurm_FD_SET(fd, set);
}

void slurm_FD_ZERO(slurm_fd_set *set)
{
	return _slurm_FD_ZERO(set);
}

int slurm_fcntl ( slurm_fd fd , int cmd , ... )
{
	va_list va ;
	int rc ;
	
	va_start ( va , cmd );
	rc = _slurm_vfcntl ( fd , cmd , va ) ;
	va_end ( va ) ;

	return rc ;
}
	
int slurm_set_stream_non_blocking ( slurm_fd open_fd )
{
	return _slurm_set_stream_non_blocking (  open_fd ) ;
}

int slurm_set_stream_blocking ( slurm_fd open_fd ) 
{
	return _slurm_set_stream_blocking (  open_fd ) ;
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

void slurm_pack_slurm_addr ( slurm_addr * slurm_address , void ** buffer , int * length )
{
	_slurm_pack_slurm_addr ( slurm_address , buffer , length ) ;
}

void slurm_unpack_slurm_addr_no_alloc ( slurm_addr * slurm_address , void ** buffer , int * length )
{
	_slurm_unpack_slurm_addr_no_alloc ( slurm_address , buffer , length ) ;
}

void slurm_print_slurm_addr ( slurm_addr * address, char *buf, size_t n )
{
	_slurm_print_slurm_addr ( address, buf, n ) ;
}

/*******************************************/
/***** slurm send highlevel msg  functions */
/*******************************************/
void slurm_send_rc_msg ( slurm_msg_t * request_msg , int rc )
{
	slurm_msg_t response_msg ;
	return_code_msg_t rc_msg ;

	/* no change */
	rc_msg . return_code = rc ;
	/* init response_msg structure */
	response_msg . address = request_msg -> address ;
	response_msg . msg_type = RESPONSE_SLURM_RC ;
	response_msg . data = & rc_msg ;

	/* send message */
	slurm_send_node_msg( request_msg -> conn_fd , &response_msg ) ;
}

int slurm_send_recv_controller_msg ( slurm_msg_t * request_msg , slurm_msg_t * response_msg )
{
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;

        /* init message connection for message communication with controller */
        if ( ( sockfd = slurm_open_controller_conn ( ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* send request message */
        if ( ( rc = slurm_send_controller_msg ( sockfd , request_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* receive message */
        if ( ( msg_size = slurm_receive_msg ( sockfd , response_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;
        /* shutdown message connection */
        if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        return SLURM_SUCCESS ;
}

int slurm_send_recv_node_msg ( slurm_msg_t * request_msg , slurm_msg_t * response_msg )
{
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;

        /* init message connection for message communication with controller */
        if ( ( sockfd = slurm_open_msg_conn ( & request_msg -> address ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* send request message */
        if ( ( rc = slurm_send_node_msg ( sockfd , request_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* receive message */
        if ( ( msg_size = slurm_receive_msg ( sockfd , response_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;
        /* shutdown message connection */
        if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        return SLURM_SUCCESS ;
}

int slurm_send_only_controller_msg ( slurm_msg_t * request_msg )
{
        int rc ;
        slurm_fd sockfd ;

        /* init message connection for message communication with controller */
        if ( ( sockfd = slurm_open_controller_conn ( ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* send request message */
        if ( ( rc = slurm_send_controller_msg ( sockfd , request_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* shutdown message connection */
        if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        return SLURM_SUCCESS ;
}

int slurm_send_only_node_msg ( slurm_msg_t * request_msg )
{
        int rc ;
        slurm_fd sockfd ;

        /* init message connection for message communication with controller */
        if ( ( sockfd = slurm_open_msg_conn ( & request_msg -> address ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* send request message */
        if ( ( rc = slurm_send_node_msg ( sockfd , request_msg ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        /* shutdown message connection */
        if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
                return SLURM_SOCKET_ERROR ;

        return SLURM_SUCCESS ;
}

short int slurm_get_slurmd_port ( )
{
	return slurmctld_conf . slurmd_port ;
}

void slurm_free_msg ( slurm_msg_t * msg )
{
	        xfree ( msg ) ;
}
