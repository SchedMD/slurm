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
#include <stdarg.h>

#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_util.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_errno.h>

/******************************************/
/* Morris Jette's configuration stuff */
/******************************************/

/* slurmctld_conf must be loaded with port numbers and controller names */
extern slurm_ctl_conf_t slurmctld_conf;
/* function to read hostname and port numbers from configuration file */
int read_slurm_port_config ( );


/***************************************************/
/* protocol configuration functions                */
/***************************************************/

/* slurm_set_api_config
 * sets the slurm_protocol_config object
 * NOT THREAD SAFE
 * IN protocol_conf		-  slurm_protocol_config object
 */
int inline slurm_set_api_config ( slurm_protocol_config_t * protocol_conf );

/* slurm_get_api_config
 * returns a pointer to the current slurm_protocol_config object
 * RET slurm_protocol_config_t	- current slurm_protocol_config object
 */
inline slurm_protocol_config_t * slurm_get_api_config ( );

/* slurm_api_set_default_config
 * called by the send_controller_msg function to insure that at least the compiled in default slurm_protocol_config object is initialized
 * RET int 		- return code
 */
int inline slurm_api_set_default_config ( );

/* slurm_set_default_controllers
 * sets the controller info members of the default slurm_protocol_config object
 * NOT THREAD SAFE
 * IN primary_controller_hostname	- primary controller hostname
 * IN secondary_controller_hostnme	- secondary controller hostnme
 * IN pri_port				- primary controller port
 * IN sec_port				- secondary controller port
 * RET int				- retrun code
 */
int inline slurm_set_default_controllers ( char * primary_controller_hostname , char * secondary_controller_hostnme, uint16_t pri_port , uint16_t sec_port );

/* slurm_get_slurmd_port
 * returns slurmd port from slurmctld_conf object
 * RET short int	- slurmd port
 */
short int inline slurm_get_slurmd_port ( ) ;

/***************************************************/
/* server msg functions used by slurmctld, slurmd */
/***************************************************/

/* In the socket implementation it creates a socket, binds to it, and listens for connections.
 * In the mongo implemenetation is should just create a mongo socket , binds and return.
 * IN port		- port to bind the msg server to
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_init_msg_engine_port ( uint16_t port ) ; 

/* In the socket implementation it creates a socket, binds to it, and listens for connections.
 * In the mongo implemenetation is should just create a mongo socket , bind and return.
 * IN slurm_address 	- slurm_addr to bind the msg server to 
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_init_msg_engine ( slurm_addr * slurm_address ) ;

/* In the bsd implmentation maps directly to a accept call 
 * In the mongo it returns the open_fd and is essentially a no-op function call
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address ) ;

/* In the bsd implmentation maps directly to a close call, to close the socket that was accepted
 * In the mongo it is a no-op (  slurm_shutdown_msg_engine should be called to close the mongo socket since there is no "accept" phase for datagrams )
 * IN open_fd		- an open file descriptor to close
 * RET int		- the return code
 */
int inline slurm_close_accepted_conn ( slurm_fd open_fd ) ;

/* just calls close on an established msg connection
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int inline slurm_shutdown_msg_engine ( slurm_fd open_fd ) ;

/* slurm_send_rc_msg
 * given the original request message this function sends a slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc 		- the return_code to send back to the client
 */
void slurm_send_rc_msg ( slurm_msg_t * request_msg , int rc );

/**********************************************************************/
/* msg send and receive functions used by msg servers and msg clients */ 
/**********************************************************************/

/*
 * NOTE: memory is allocated for the returned msg and must be freed at some point using the slurm_free_functions
 * IN open_fd 		- file descriptor to receive msg on
 * OUT msg 		- a slurm_msg struct to be filled in by the function
 * RET int		- size of msg received in bytes before being unpacked
 */
int slurm_receive_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ; 

/***** send msg functions */
/* sends a slurm_protocol msg to the slurmctld based on location information retrieved from the slurmd.conf
 * if unable to contant the primary slurmctld attempts will be made to contact the backup controller
 * 
 * IN open_fd	- file descriptor to send msg on
 * IN msg	- a slurm msg struct to be sent
 * RET int	- size of msg sent in bytes
 */
int slurm_send_controller_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ;

/* sends a message to an arbitrary node
 *
 * IN open_fd		- file descriptor to send msg on
 * IN msg		- a slurm msg struct to be sent
 * RET int		- size of msg sent in bytes
 */
int slurm_send_node_msg ( slurm_fd open_fd , slurm_msg_t * msg ) ;

/**********************************************************************/
/* msg connection establishment functions used by msg clients         */
/**********************************************************************/

/* calls connect to make a connection-less datagram connection to the the primary or secondary slurmctld message engine
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_open_controller_conn ( ) ;

/* In the bsd socket implementation it creates a SOCK_STREAM socket and calls connect on it
 * In the mongo implementation it creates a SOCK_DGRAM socket and calls connect on it
 * a SOCK_DGRAM socket called with connect is defined to only receive messages from the 
 * address/port pair argument of the connect call slurm_address	- for now it is really 
 * just a sockaddr_in
 * IN slurm_address 	- slurm_addr of the connection destination
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_open_msg_conn ( slurm_addr * slurm_address ) ;

/* just calls close on an established msg connection to close
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int inline slurm_shutdown_msg_conn ( slurm_fd open_fd ) ;


/**********************************************************************/
/* stream functions*/
/**********************************************************************/

/* slurm_listen_stream
 * opens a stream server and listens on it
 * IN slurm_address 	- slurm_addr to bind the server stream to
 * RET slurm_fd		- file descriptor of the stream created
 */
slurm_fd inline slurm_listen_stream ( slurm_addr * slurm_address ) ;

/* slurm_accept_stream
 * accepts a incomming stream connection on a stream server slurm_fd 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the accepted connection 
 */
slurm_fd inline slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address ) ;

/* slurm_open_stream
 * opens a client connection to stream server
 * IN slurm_address 	- slurm_addr of the connection destination
 * RET slurm_fd         - file descriptor of the connection created
 */
slurm_fd inline slurm_open_stream ( slurm_addr * slurm_address ) ;

/* slurm_close_stream
 * closes either a server or client stream file_descriptor
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int inline slurm_close_stream ( slurm_fd open_fd ) ;

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * RET size_t		- bytes sent , or -1 on errror
 */
size_t inline slurm_write_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN open_fd		- file descriptor to read from
 * OUT buffer	- buffer to receive into
 * IN size		- size of buffer
 * RET size_t		- bytes read , or -1 on errror
 */
size_t inline slurm_read_stream ( slurm_fd open_fd , char * buffer , size_t size ) ;

/* slurm_get_stream_addr
 * esentially a encapsilated get_sockname  
 * IN open_fd 		- file descriptor to retreive slurm_addr for
 * OUT address		- address that open_fd to bound to
 */
int inline slurm_get_stream_addr  ( slurm_fd open_fd , slurm_addr * address ) ;
	
int inline slurm_select(int n, slurm_fd_set *readfds, slurm_fd_set *writefds, slurm_fd_set *exceptfds, struct timeval *timeout) ;
void inline slurm_FD_CLR(slurm_fd, slurm_fd_set *set) ;
int inline slurm_FD_ISSET(slurm_fd, slurm_fd_set *set) ;
void inline slurm_FD_SET(slurm_fd, slurm_fd_set *set) ;
void inline slurm_FD_ZERO(slurm_fd_set *set) ;

int inline slurm_set_stream_non_blocking ( slurm_fd open_fd ) ;
int inline slurm_set_stream_blocking ( slurm_fd open_fd ) ;

/**********************************************************************/
/* raw msg buffer send functions */
/* Allows the user to send a raw buffer */
/**********************************************************************/
/*
int slurm_receive_buffer ( slurm_fd open_fd , slurm_addr * source_address , slurm_msg_type_t * msg_type , char * data_buffer , size_t buf_len ) ;
int slurm_send_controller_buffer ( slurm_fd open_fd , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;
int slurm_send_node_buffer ( slurm_fd open_fd , slurm_addr * destination_address , slurm_msg_type_t msg_type , char * data_buffer , size_t buf_len ) ;
*/

/**********************************************************************/
/* Address Conversion Functions */
/**********************************************************************/

/* slurm_set_addr_uint
 * initializes the slurm_address with the port and ip_addrss passed to it
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN ip_address	- ipv4 address in uint32 host order form
 */
void inline slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address ) ;

/* slurm_set_addr
 * initializes the slurm_address with the port and ip_addrss passed to it
 * NOT THREAD SAFE
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void inline slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host ) ;

/* slurm_set_addr_any
 * initialized the slurm_address with the port passed to in on INADDR_ANY
 * NOT THREAD SAFE
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 */
void inline slurm_set_addr_any ( slurm_addr * slurm_address , uint16_t port ) ;

/* slurm_set_addr
 * initializes the slurm_address with the port and ip_addrss passed to it
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void inline slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host ) ;

/* slurm_get_addr 
 * given a slurm_address it returns to port and hostname
 * NOT THREAD SAFE
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT host		- hostname
 * IN buf_len		- length of hostname buffer
 */
void inline slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , uint32_t buf_len ) ;
/* slurm_print_slurm_addr
 * prints a slurm_addr into a buf
 * IN address		- slurm_addr to print
 * IN buf		- space for string representation of slurm_addr
 * IN n			- max number of bytes to write (including NUL)
 */
void inline slurm_print_slurm_addr ( slurm_addr * address, char *buf, size_t n ) ;

/**********************************************************************/
/* slurm_addr pack routines*/ 
/**********************************************************************/

/* slurm_pack_slurm_addr
 * packs a slurm_addr into a buffer to serialization transport
 * IN slurm_address	- slurm_addr to pack
 * NOTE : the buffer and length parameters are modified by the function 
 * NOTE : *buffer is incremented and *length is decremented SEE pack.c
 * IN/OUT buffer	- buffer to pack the slurm_addr into
 * IN/OUT length	- size of the buffer
 */
void inline slurm_pack_slurm_addr ( slurm_addr * slurm_address , void ** buffer , int * length ) ;

/* slurm_pack_slurm_addr
 * unpacks a buffer into a slurm_addr after serialization transport
 * OUT slurm_address	- slurm_addr to unpack to
 * NOTE : the buffer and length parameters are modified by the function 
 * NOTE : *buffer is incremented and *length is decremented SEE pack.c
 * IN/OUT buffer	- buffer to upack the slurm_addr from
 * IN/OUT length	- size of the buffer
 */
void inline slurm_unpack_slurm_addr_no_alloc ( slurm_addr * slurm_address , void ** buffer , int * length ) ;

/*******************************************/
/* simplified communication routines 
 * They open a connection do work then close the connection all within the function*/
/*******************************************/

/* slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message, listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- return code
 */
int slurm_send_recv_controller_msg ( slurm_msg_t * request_msg , slurm_msg_t * response_msg ) ;

/* slurm_send_recv_node_msg
 * opens a connection to node, sends the node a message, listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- return code
 */
int slurm_send_recv_node_msg ( slurm_msg_t * request_msg , slurm_msg_t * response_msg ) ;

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a message then, closes the connection
 * IN request_msg	- slurm_msg request
 * RET int 		- return code
 */
int slurm_send_only_node_msg ( slurm_msg_t * request_msg ) ;

/* slurm_send_only_controller_msg
 * opens a connection to node, sends the node a message then, closes the connection
 * IN request_msg	- slurm_msg request
 * RET int 		- return code
 */
int slurm_send_only_node_msg ( slurm_msg_t * request_msg ) ;

/* Slurm message functions */
void slurm_free_msg ( slurm_msg_t * msg ) ;
#endif
