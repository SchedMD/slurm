/*****************************************************************************\
 *  slurm_protocol_api.h - high-level slurm communication functions 
 *	definitions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  UCRL-CODE-217948.
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

#ifndef _SLURM_PROTOCOL_API_H
#define _SLURM_PROTOCOL_API_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <sys/types.h>
#include <stdarg.h>

#include <slurm/slurm_errno.h>

#include "src/common/pack.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_util.h"

#define MIN_NOALLOC_JOBID ((uint32_t) 0xffff0000)
#define MAX_NOALLOC_JOBID ((uint32_t) 0xfffffffd)

enum controller_id {
	PRIMARY_CONTROLLER = 1,
	SECONDARY_CONTROLLER = 2
};

/**********************************************************************\
 * protocol configuration functions                
\**********************************************************************/

/* slurm_set_api_config
 * sets the slurm_protocol_config object
 * IN protocol_conf		-  slurm_protocol_config object
 */
int inline slurm_set_api_config(slurm_protocol_config_t * protocol_conf);

/* slurm_get_api_config
 * returns a pointer to the current slurm_protocol_config object
 * RET slurm_protocol_config_t	- current slurm_protocol_config object
 */
inline slurm_protocol_config_t *slurm_get_api_config();

/* slurm_get_mpi_default
 * get default mpi value from slurmctld_conf object
 * RET char *   - mpi default value from slurm.conf,  MUST be xfreed by caller
 */
char *slurm_get_mpi_default(void);

/* slurm_api_set_conf_file
 *      set slurm configuration file to a non-default value
 * pathname IN - pathname of slurm configuration file to be used
 */
extern void slurm_api_set_conf_file(char *pathname);

/* slurm_api_set_default_config
 *	called by the send_controller_msg function to insure that at least 
 *	the compiled in default slurm_protocol_config object is initialized
 * RET int 		- return code
 */
int inline slurm_api_set_default_config();

/* slurm_api_clear_config
 * execute this only at program termination to free all memory */
void inline slurm_api_clear_config(void);

/* slurm_get_plugin_dir
 * get plugin directory from slurmctld_conf object from slurmctld_conf object 
 * RET char *   - plugin directory, MUST be xfreed by caller
 */
char *slurm_get_plugin_dir(void);

/* slurm_get_auth_type
 * returns the authentication type from slurmctld_conf object
 * RET char *    - auth type, MUST be xfreed by caller
 */
extern char *slurm_get_auth_type(void);

/* slurm_set_auth_type
 * set the authentication type in slurmctld_conf object
 * used for security testing purposes
 * RET 0 or error code
 */
extern int slurm_set_auth_type(char *auth_type);

/* slurm_get_fast_schedule
 * returns the value of fast_schedule in slurmctld_conf object
 */
extern uint16_t slurm_get_fast_schedule(void);

/* slurm_get_jobacct_loc
 * returns the job accounting loc from slurmctld_conf object
 * RET char *    - job accounting location,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_loc(void);

/* slurm_get_jobacct_parameters
 * returns the job accounting parameters from slurmctld_conf object
 * RET char *    - job accounting parameters,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_parameters(void);

/* slurm_get_jobacct_type
 * returns the job accounting type from slurmctld_conf object
 * RET char *    - job accounting type,  MUST be xfreed by caller
 */
char *slurm_get_jobacct_type(void);

/* slurm_get_jobcomp_type
 * returns the job completion logger type from slurmctld_conf object
 * RET char *    - job completion type,  MUST be xfreed by caller
 */
char *slurm_get_jobcomp_type(void);

/* slurm_get_proctrack_type
 * get ProctrackType from slurmctld_conf object
 * RET char *   - proctrack type, MUST be xfreed by caller
 */
char *slurm_get_proctrack_type(void);

/* slurm_get_slurmd_port
 * returns slurmd port from slurmctld_conf object
 * RET uint16_t	- slurmd port
 */
uint16_t inline slurm_get_slurmd_port(void);

/* slurm_get_slurm_user_id
 * returns slurmd uid from slurmctld_conf object 
 * RET uint32_t	- slurm user id
 */
uint32_t slurm_get_slurm_user_id(void);

/* slurm_get_sched_type
 * get sched type from slurmctld_conf object
 * RET char *   - sched type, MUST be xfreed by caller
 */
char *slurm_get_sched_type(void);

/* slurm_get_select_type
 * get select_type from slurmctld_conf object
 * RET char *   - select_type, MUST be xfreed by caller
 */
char *slurm_get_select_type(void);

/* slurm_get_switch_type
 * get switch type from slurmctld_conf object
 * RET char *   - switch type, MUST be xfreed by caller
 */
char *slurm_get_switch_type(void);

/* slurm_get_wait_time
 * returns wait_time from slurmctld_conf object
 * RET uint16_t        - wait_time
 */
uint16_t slurm_get_wait_time(void);

/* slurm_get_srun_prolog
 * return the name of the srun prolog program
 * RET char *   - name of prolog program, must be xfreed by caller
 */
char *slurm_get_srun_prolog(void);

/* slurm_get_srun_epilog
 * return the name of the srun epilog program
 * RET char *   - name of epilog program, must be xfreed by caller
 */
char *slurm_get_srun_epilog(void);

/* slurm_get_task_epilog
 * RET task_epilog name, must be xfreed by caller */
char *slurm_get_task_epilog(void);

/* slurm_get_task_prolog
 * RET task_prolog name, must be xfreed by caller */
char *slurm_get_task_prolog(void);

/* slurm_get_task_plugin
 * RET task_plugin name, must be xfreed by caller */
char *slurm_get_task_plugin(void);

/**********************************************************************\
 * general message management functions used by slurmctld, slurmd
\**********************************************************************/

/* In the socket implementation it creates a socket, binds to it, and 
 *	listens for connections.
 * IN port		- port to bind the msg server to
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_init_msg_engine_port(uint16_t port);

/* In the socket implementation it creates a socket, binds to it, and 
 *	listens for connections.
 * IN slurm_address 	- slurm_addr to bind the msg server to 
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_init_msg_engine(slurm_addr * slurm_address);

/* In the bsd implmentation maps directly to a accept call 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_accept_msg_conn(slurm_fd open_fd,
				      slurm_addr * slurm_address);

/* In the bsd implmentation maps directly to a close call, to close 
 *	the socket that was accepted
 * IN open_fd		- an open file descriptor to close
 * RET int		- the return code
 */
int inline slurm_close_accepted_conn(slurm_fd open_fd);

/* just calls close on an established msg connection
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int inline slurm_shutdown_msg_engine(slurm_fd open_fd);

/**********************************************************************\
 * receive message functions
\**********************************************************************/

/*
 *  Receive a slurm message on the open slurm descriptor "fd" waiting
 *    at most "timeout" seconds for the message data. If timeout is
 *    zero, a default timeout is used. Memory for the message data
 *    (msg->data) is allocated from within this function, and must be
 *    freed at some point using one of the slurm_free* functions.
 *
 *  Returns SLURM_SUCCESS if an entire message is successfully 
 *    received. Otherwise SLURM_ERROR is returned.
 */
int slurm_receive_msg(slurm_fd fd, slurm_msg_t *msg, int timeout);

/**********************************************************************\
 * send message functions
\**********************************************************************/

/* sends a message to an arbitrary node
 *
 * IN open_fd		- file descriptor to send msg on
 * IN msg		- a slurm msg struct to be sent
 * RET int		- size of msg sent in bytes
 */
int slurm_send_node_msg(slurm_fd open_fd, slurm_msg_t * msg);

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/* calls connect to make a connection-less datagram connection to the 
 *	primary or secondary slurmctld message engine
 * RET slurm_fd	- file descriptor of the connection created
 * IN dest 	- controller to contact, primary or secondary
 */
slurm_fd inline slurm_open_controller_conn();
slurm_fd inline slurm_open_controller_conn_spec(enum controller_id dest);

/* In the bsd socket implementation it creates a SOCK_STREAM socket  
 *	and calls connect on it a SOCK_DGRAM socket called with connect   
 *	is defined to only receive messages from the address/port pair  
 *	argument of the connect call slurm_address - for now it is  
 *	really just a sockaddr_in
 * IN slurm_address 	- slurm_addr of the connection destination
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd inline slurm_open_msg_conn(slurm_addr * slurm_address);

/* just calls close on an established msg connection to close
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int inline slurm_shutdown_msg_conn(slurm_fd open_fd);


/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_listen_stream
 * opens a stream server and listens on it
 * IN slurm_address 	- slurm_addr to bind the server stream to
 * RET slurm_fd		- file descriptor of the stream created
 */
slurm_fd inline slurm_listen_stream(slurm_addr * slurm_address);

/* slurm_accept_stream
 * accepts a incomming stream connection on a stream server slurm_fd 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the accepted connection 
 */
slurm_fd inline slurm_accept_stream(slurm_fd open_fd,
				    slurm_addr * slurm_address);

/* slurm_open_stream
 * opens a client connection to stream server
 * IN slurm_address 	- slurm_addr of the connection destination
 * RET slurm_fd         - file descriptor of the connection created
 */
slurm_fd inline slurm_open_stream(slurm_addr * slurm_address);

/* slurm_close_stream
 * closes either a server or client stream file_descriptor
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int inline slurm_close_stream(slurm_fd open_fd);

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes sent , or -1 on errror
 */
size_t inline slurm_write_stream(slurm_fd open_fd, char *buffer,
				 size_t size);
size_t inline slurm_write_stream_timeout(slurm_fd open_fd,
					 char *buffer, size_t size,
					 int timeout);

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN open_fd		- file descriptor to read from
 * OUT buffer	- buffer to receive into
 * IN size		- size of buffer
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes read , or -1 on errror
 */
size_t inline slurm_read_stream(slurm_fd open_fd, char *buffer,
				size_t size);
size_t inline slurm_read_stream_timeout(slurm_fd open_fd,
					char *buffer, size_t size,
					int timeout);

/* slurm_get_stream_addr
 * esentially a encapsilated get_sockname  
 * IN open_fd 		- file descriptor to retreive slurm_addr for
 * OUT address		- address that open_fd to bound to
 */
int inline slurm_get_stream_addr(slurm_fd open_fd, slurm_addr * address);

/* make an open slurm connection blocking or non-blocking
 *	(i.e. wait or do not wait for i/o completion )
 * IN open_fd	- an open file descriptor to change the effect
 * RET int	- the return code
 */
int inline slurm_set_stream_non_blocking(slurm_fd open_fd);
int inline slurm_set_stream_blocking(slurm_fd open_fd);

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_set_addr_uint
 * initializes the slurm_address with the supplied port and ip_address
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN ip_address	- ipv4 address in uint32 host order form
 */
void inline slurm_set_addr_uint(slurm_addr * slurm_address,
				uint16_t port, uint32_t ip_address);

/* reset_slurm_addr
 * resets the address field of a slurm_addr, port and family unchanged
 * OUT slurm_address	- slurm_addr to be reset in
 * IN new_address	- source of address to write into slurm_address
 */
void reset_slurm_addr(slurm_addr * slurm_address, slurm_addr new_address);

/* slurm_set_addr
 * initializes the slurm_address with the supplied port and ip_address
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void inline slurm_set_addr(slurm_addr * slurm_address,
			   uint16_t port, char *host);

/* slurm_set_addr_any
 * initialized the slurm_address with the supplied port on INADDR_ANY
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 */
void inline slurm_set_addr_any(slurm_addr * slurm_address, uint16_t port);

/* slurm_set_addr_char
 * initializes the slurm_address with the supplied port and host
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void inline slurm_set_addr_char(slurm_addr * slurm_address,
				uint16_t port, char *host);

/* slurm_get_addr 
 * given a slurm_address it returns to port and hostname
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT host		- hostname
 * IN buf_len		- length of hostname buffer
 */
void inline slurm_get_addr(slurm_addr * slurm_address,
			   uint16_t * port, char *host, uint32_t buf_len);

/* slurm_get_ip_str 
 * given a slurm_address it returns its port and ip address string
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT ip		- ip address in dotted-quad string form
 * IN buf_len		- length of ip buffer
 */
void inline slurm_get_ip_str(slurm_addr * slurm_address, uint16_t * port,
			     char *ip, unsigned int buf_len);

/* slurm_get_peer_addr
 * get the slurm address of the peer connection, similar to getpeeraddr
 * IN fd		- an open connection
 * OUT slurm_address	- place to park the peer's slurm_addr
 */
int inline slurm_get_peer_addr(slurm_fd fd, slurm_addr * slurm_address);

/* slurm_print_slurm_addr
 * prints a slurm_addr into a buf
 * IN address		- slurm_addr to print
 * IN buf		- space for string representation of slurm_addr
 * IN n			- max number of bytes to write (including NUL)
 */
void inline slurm_print_slurm_addr(slurm_addr * address,
				   char *buf, size_t n);

/**********************************************************************\
 * slurm_addr pack routines
\**********************************************************************/

/* slurm_pack_slurm_addr
 * packs a slurm_addr into a buffer to serialization transport
 * IN slurm_address	- slurm_addr to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr into
 */
void inline slurm_pack_slurm_addr(slurm_addr * slurm_address, Buf buffer);

/* slurm_pack_slurm_addr
 * unpacks a buffer into a slurm_addr after serialization transport
 * OUT slurm_address	- slurm_addr to unpack to
 * IN/OUT buffer	- buffer to upack the slurm_addr from
 * returns 		- SLURM error code
 */
int inline slurm_unpack_slurm_addr_no_alloc(slurm_addr * slurm_address,
					    Buf buffer);

/**********************************************************************\
 * simplified communication routines 
 * They open a connection do work then close the connection all within 
 * the function
\**********************************************************************/

/* slurm_send_rc_msg
 * given the original request message this function sends a 
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc 		- the return_code to send back to the client
 */
int slurm_send_rc_msg(slurm_msg_t * request_msg, int rc);

/* slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message, 
 * listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- return code
 */
int slurm_send_recv_controller_msg(slurm_msg_t * request_msg,
				   slurm_msg_t * response_msg);

/* slurm_send_recv_node_msg
 * opens a connection to node, sends the node a message, listens 
 * for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- return code
 */
int slurm_send_recv_node_msg(slurm_msg_t * request_msg,
			     slurm_msg_t * response_msg, int timeout);

/*
 *  Open a connection to req->address, send message and receive 
 *    a "return code" message, returning return code in "rc"
 */
int slurm_send_recv_rc_msg(slurm_msg_t *req, int *rc, int timeout);

/*
 *  Same as above, but send to controller
 */
int slurm_send_recv_controller_rc_msg(slurm_msg_t *req, int *rc);

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the node a message then, 
 * closes the connection
 * IN request_msg	- slurm_msg request
 * RET int 		- return code
 */
int slurm_send_only_controller_msg(slurm_msg_t * request_msg);

/* slurm_send_only_node_msg
 * opens a connection to node, sends the node a message then, 
 * closes the connection
 * IN request_msg	- slurm_msg request
 * RET int 		- return code
 */
int slurm_send_only_node_msg(slurm_msg_t * request_msg);

/* Slurm message functions */
void slurm_free_msg(slurm_msg_t * msg);
void slurm_free_cred(void *cred);
#endif
