#ifndef _SLURM_PROTOCOL_API_H
#define _SLURM_PROTOCOL_API_H

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

#include <sys/types.h>

#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_errno.h>


#define SLURM_PORT 7000
#define SLURM_PROTOCOL_DEFAULT_PORT 7000
#define SLURM_PROTOCOL_DEFAULT_PRIMARY_CONTROLLER "localhost"
#define SLURM_PROTOCOL_DEFAULT_SECONDARY_CONTROLLER "localhost"


/* high level routines */
/* API init routines */
int inline slurm_api_init ( slurm_protocol_config_t * protocol_conf ) ;
int inline slurm_api_cleanup ( ) ;
int inline slurm_api_set_defaults ( ) ;

/* msg functions */

/* In the socket implementation it creates a socket, binds to it, and listens for connections.
 * In the mongo implemenetation is should just create a mongo socket , binds and return.
 * slurm_address 	- for now it is really just a sockaddr_in
 * slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_init_msg_engine_port ( uint16_t port ) ; 

/* In the socket implementation it creates a socket, binds to it, and listens for connections.
 * In the mongo implemenetation is should just create a mongo socket , bind and return.
 * slurm_address 	- for now it is really just a sockaddr_in
 * slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_init_msg_engine ( slurm_addr * slurm_address ) ;

/* In the bsd implmentation maps directly to a accept call 
 * In the mongo it returns the open_fd and is essentially a no-op function call
 * open_fd		- file descriptor to accept connection on
 * slurm_address 	- for now it is really just a sockaddr_in
 * int	- the return code
 */
slurm_fd inline slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address ) ;

/* In the bsd implmentation maps directly to a close call, to close the socket that was accepted
 * In the mongo it is a no-op (  slurm_shutdown_msg_engine should be called to close the mongo socket since there is no "accept" phase for datagrams
 * open_fd		- file descriptor to accept connection on
 * int	- the return code
 */
int inline slurm_close_accepted_conn ( slurm_fd open_fd ) ;

/* just calls close on an established msg connection
 * open_fd	- an open file descriptor
 * int	- the return code
 */
int inline slurm_shutdown_msg_engine ( slurm_fd open_fd ) ;

/***** recv msg functions */
/*
 * note that a memory is allocated for the returned msg and must be freed at some point 
 * open_fd 		- file descriptor to receive msg on
 * msg 		- a slurm msg struct
 * int		- size of msg received in bytes before being unpacked
 */
int slurm_receive_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ; 

/***** send msg functions */
/* sends a slurm_protocol msg to the slurmctld based on location information retrieved from the slurmd.conf
 * if unable to contant the primary slurmctld attempts will be made to contact the backup controller
 * 
 * open_fd	- file descriptor to send msg on
 * msg	- a slurm msg struct
 * int	- size of msg sent in bytes
 */
int slurm_send_controller_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ;

/* sends a message to an arbitrary node
 *
 * open_fd		- file descriptor to send msg on
 * msg			- a slurm msg struct
 * int		- size of msg sent in bytes
 */
int slurm_send_node_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ;

/* calls connect to make a connection-less datagram connection to the the primary or secondary slurmctld message engine
 * slurm_address 	- for now it is really just a sockaddr_in
 * int	- the return code
 */
slurm_fd inline slurm_open_controller_conn ( ) ;

/* In the bsd socket implementation it creates a SOCK_STREAM socket and calls connect on it
 * In the mongo implementation it creates a SOCK_DGRAM socket and calls connect on it
 * a SOCK_DGRAM socket called with connect is defined to only receive messages from the 
 * address/port pair argument of the connect call slurm_address	- for now it is really 
 * just a sockaddr_in
 * int	- the return code
 */
slurm_fd inline slurm_open_msg_conn ( slurm_addr * slurm_address ) ;

/* just calls close on an established msg connection
 * open_fd	- an open file descriptor
 * int	- the return code
 */
int inline slurm_shutdown_msg_conn ( slurm_fd open_fd ) ;


/* send msg functions */

/* stream functions */
slurm_fd inline slurm_listen_stream ( slurm_addr * slurm_address ) ;
slurm_fd inline slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;
slurm_fd inline slurm_open_stream ( slurm_addr * slurm_address ) ;

size_t inline slurm_write_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
size_t inline slurm_read_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;
int inline slurm_close_stream ( slurm_fd open_fd ) ;
	

/* Low level routines */
/* msg functions 
*/

int slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_type_t * msg_type , char * data_buffer , size_t buf_len ) ;
int slurm_send_controller_buffer ( slurm_fd open_fd , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;
int slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;

/* Address Conversion Functions */

void inline slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;
void inline slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
void inline slurm_set_addr_any ( slurm_addr * slurm_address , uint16_t port ) ;
void inline slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host ) ;
void inline slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , uint32_t buf_len ) ;

/* Slurm message functions */
void slurm_free_msg ( slurm_msg_t * msg ) ;

/*******************************************/
/***** slurm send highlevel msg  functions */
/*******************************************/
void slurm_send_rc_msg ( slurm_msg_t * request_msg , int rc ); /* sends a return code to the client that sent the request_msg */
int slurm_send_recv_controller_msg ( slurm_msg_t * request_msg , slurm_msg_t * response_msg ) ;
int slurm_send_only_controller_msg ( slurm_msg_t * request_msg ) ;
int slurm_send_only_node_msg ( slurm_msg_t * request_msg ) ;
#endif
