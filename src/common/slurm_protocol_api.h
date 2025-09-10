/*****************************************************************************\
 *  slurm_protocol_api.h - high-level slurm communication functions
 *	definitions
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013      Intel, Inc.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _SLURM_PROTOCOL_API_H
#define _SLURM_PROTOCOL_API_H

#include <inttypes.h>
#include <sys/types.h>
#include <stdarg.h>

#include "slurm/slurm_errno.h"

#include "src/common/pack.h"
#include "src/interfaces/auth.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_socket.h"
#include "src/common/slurm_protocol_util.h"

#define MIN_NOALLOC_JOBID ((uint32_t) 0xffff0000)
#define MAX_NOALLOC_JOBID ((uint32_t) 0xfffffffd)

#define CONVERT_NUM_UNIT_EXACT 0x00000001
#define CONVERT_NUM_UNIT_NO    0x00000002
#define CONVERT_NUM_UNIT_RAW   0x00000004

/* unit types */
enum {
	UNIT_NONE,
	UNIT_KILO,
	UNIT_MEGA,
	UNIT_GIGA,
	UNIT_TERA,
	UNIT_PETA,
	UNIT_UNKNOWN
};

/**********************************************************************\
 * protocol configuration functions
\**********************************************************************/

/*
 * slurm_auth_opts_to_socket
 * Convert AuthInfo to a socket path. Accepts two input formats:
 * 1) <path>           (Old format)
 * 2) socket=<path>[,] (New format)
 * RET char * - socket path,	MUST be xfreed by caller
 */
char *slurm_auth_opts_to_socket(char *opts);

/* slurm_get_auth_ttl
 * returns the credential Time To Live option from the AuthInfo parameter
 * cache value in local buffer for best performance
 * RET int - Time To Live in seconds or 0 if not specified
 */
int slurm_get_auth_ttl(void);

/* slurm_get_tres_weight_array
 * IN weights_str - string of tres and weights to be parsed.
 * IN tres_cnt - count of how many tres' are on the system (e.g.
 * 		slurmctld_tres_cnt).
 * IN fail - whether to fatal or not if there are parsing errors.
 * RET double* of tres weights.
 */
double *slurm_get_tres_weight_array(char *weights_str, int tres_cnt, bool fail);

/* slurm_get_stepd_loc
 * get path to the slurmstepd
 * RET char * - absolute path to the slurmstepd, MUST be xfreed by caller
 */
extern char *slurm_get_stepd_loc(void);

/* slurm_get_tmp_fs
 * returns the TmpFS configuration parameter from slurm_conf object
 * RET char *    - tmp_fs, MUST be xfreed by caller
 */
extern char *slurm_get_tmp_fs(char *node_name);

/* slurm_get_track_wckey
 * returns the value of track_wckey in slurm_conf object
 */
extern uint16_t slurm_get_track_wckey(void);

/* slurm_with_slurmdbd
 * returns true if operating with slurmdbd
 */
bool slurm_with_slurmdbd(void);

/* slurm_get_preempt_type
 * get PreemptType from slurm_conf object
 * RET char *   - preempt type, MUST be xfreed by caller
 */
char *slurm_get_preempt_type(void);

/* slurm_get_acct_interconnect_profile_type
 * get InterconnectAccountingType from slurm_conf object
 * RET char *   - acct_gather_interconnect_type, MUST be xfreed by caller
 */
char *slurm_get_acct_gather_interconnect_type(void);

/* slurm_get_select_type
 * get select_type from slurm_conf object
 * RET char *   - select_type, MUST be xfreed by caller
 */
char *slurm_get_select_type(void);

/* slurm_get_srun_port_range()
 *
 * Return the array with 2 members indicating the
 * min and max ports that srun should use to listen to.
 */
uint16_t *slurm_get_srun_port_range(void);

/*
 * slurm_get_jobcomp_type
 * returns the configured GpuFreqDef value
 * RET char *    - GpuFreqDef value,  MUST be xfreed by caller
 */
char *slurm_get_gpu_freq_def(void);

/**********************************************************************\
 * general message management functions used by slurmctld, slurmd
\**********************************************************************/

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections.
 *
 * IN port		- port to bind the msg server to
 * RET slurm_fd		- file descriptor of the connection created
 */
extern int slurm_init_msg_engine_port(uint16_t port);

/* Creates a TCP socket and binds to a port in the given
 * range.
 *
 * IN ports - range of ports from which to select the one
 *            to bind
 * RET slurm_fd - file descriptor of the listening socket
 */
extern int slurm_init_msg_engine_ports(uint16_t *);

/* In the socket implementation it creates a socket, binds to it, and
 *	listens for connections.
 * IN slurm_address 	- slurm_addr_t to bind the msg server to
 * IN permissive 	- log failure errors at "error" or "debug" level
 * RET slurm_fd		- file descriptor of the connection created
 */
extern int slurm_init_msg_engine(slurm_addr_t *slurm_address, bool permissive);

/**********************************************************************\
 * receive message functions
\**********************************************************************/

/*
 * unpack a complete received message
 * OUT msg - a slurm_msg struct to be filled in by the function
 * IN  fd - file descriptor the message came from
 * IN  buffer - Buf we will fill in the message with
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */

extern int slurm_unpack_received_msg(slurm_msg_t *msg, int fd, buf_t *buffer);

/*
 *  Receive a slurm message on the open slurm descriptor "fd" waiting
 *    at most "timeout" seconds for the message data. If timeout is
 *    zero, a default timeout is used. Memory for the message data
 *    (msg->data) is allocated from within this function, and must be
 *    freed at some point using one of the slurm_free* functions.
 *    Also a slurm_cred is allocated (msg->auth_cred) which must be
 *    freed with auth_g_destroy() if it exists.
 *
 * IN tls_conn
 * OUT msg	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_receive_msg(void *tls_conn, slurm_msg_t *msg, int timeout);

/*
 *  Receive a slurm message on the open slurm descriptor "fd" waiting
 *    at most "timeout" seconds for the message data. If timeout is
 *    zero, a default timeout is used. Memory is allocated for the
 *    returned list and must be freed at some point using the
 *    list_destroy function.
 *
 * IN tls_conn
 * IN steps	- how many steps down the tree we have to wait for
 * IN timeout	- how long to wait in milliseconds
 * RET list	- list containing the responses of the children (if any) we
 *                forwarded the message to. list containing type
 *                (ret_data_info_t). NULL is returned on failure. and
 *                errno set.
 */
extern list_t *slurm_receive_msgs(void *tls_conn, int steps, int timeout);
extern list_t *slurm_receive_resp_msgs(void *tls_conn, int steps, int timeout);

/*
 *  Unpack a Slurm message from the supplied 'buffer' and forward.
 *  This will forward the message to the nodes contained in the forward_t
 *  structure inside the header of the message. The 'msg' is the actual message
 *  received and contains the ret_list of it's children and the
 *  forward_structure_t containing information about it's children also. Memory
 *  is allocated for the returned msg and the returned list both must be freed
 *  at some point using the slurm_free_functions and list_destroy function.
 *
 * OUT msg	- a slurm_msg struct to be filled in by the function
 * IN orig_addr - Original address of the connection msg was received on
 * IN open_fd	- file descriptor msg was received on
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_unpack_msg_and_forward(slurm_msg_t *msg,
					slurm_addr_t *orig_addr, int fd,
					buf_t *buffer);

/**********************************************************************\
 * send message functions
\**********************************************************************/

/* sends a message to an arbitrary node
 *
 * IN tls_conn
 * IN msg		- a slurm msg struct to be sent
 * RET int		- size of msg sent in bytes
 */
extern int slurm_send_node_msg(void *tls_conn, slurm_msg_t *msg);

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/*
 * Calls connect to make a connection-less datagram connection to a specific
 *	primary or backup slurmctld message engine
 * IN dest      - controller to contact (0=primary, 1=backup, 2=backup2, etc.)
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET tls_conn
 */
extern void *slurm_open_controller(int dest,
				   slurmdb_cluster_rec_t *comm_cluster_rec);

/*
 * Creates a SOCK_STREAM (TCP) socket and calls connect() on it.
 * Will only receive messages from the address (w/port) argument.
 * IN addr - slurm_addr_t of the connection destination
 * IN tls_cert - public certificate for destination if not already trusted
 * RET tls_conn	- TLS connection to destination
 */
extern void *slurm_open_msg_conn(slurm_addr_t *addr, char *tls_cert);

/*
 * Same as slurm_open_msg_conn, but errors are not logged.
 */
extern void *slurm_open_msg_conn_maybe(slurm_addr_t *addr, char *tls_cert);

/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN tls_conn
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * RET size_t		- bytes sent , or -1 on error
 */
extern size_t slurm_write_stream(void *tls_conn, char *buffer, size_t size);

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN tls_conn
 * OUT buffer	- buffer to receive into
 * IN size		- size of buffer
 * RET size_t		- bytes read , or -1 on error
 */
extern size_t slurm_read_stream(void *tls_conn, char *buffer, size_t size);

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_set_addr
 * initializes the slurm_address with the supplied port and ip_address
 * OUT slurm_address	- slurm_addr_t to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name
 */
extern void slurm_set_addr(slurm_addr_t * slurm_address,
			   uint16_t port, char *host);

/* slurm_get_ip_str
 * given a slurm_address it returns its ip address as a string
 * IN addr		- slurm_addr_t to be queried
 * OUT ip		- ip address in dotted-quad string form
 * IN buf_len		- length of ip buffer
 */
extern void slurm_get_ip_str(slurm_addr_t *addr, char *ip,
			     unsigned int buf_len);

/* slurm_get_peer_addr
 * get the slurm address of the peer connection, similar to getpeeraddr
 * IN fd		- an open connection
 * OUT slurm_address	- place to park the peer's slurm_addr
 */
extern int slurm_get_peer_addr(int fd, slurm_addr_t * slurm_address);

/**********************************************************************\
 * slurm_addr_t pack routines
\**********************************************************************/

/* slurm_pack_addr_array
 * packs an array of slurm_addrs into a buffer
 * OUT addr_array	- slurm_addr_t[] to pack
 * IN size_val  	- how many to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr_t from
 * returns		- Slurm error code
 */
extern void slurm_pack_addr_array(slurm_addr_t *addr_array, uint32_t size_val,
				  buf_t *buffer);

/* slurm_unpack_addr_array
 * unpacks an array of slurm_addrs from a buffer
 * OUT addr_array_ptr	- slurm_addr_t[] to unpack to
 * IN/OUT size_val  	- how many to unpack
 * IN/OUT buffer	- buffer to unpack the slurm_addr_t from
 * returns		- Slurm error code
 */
extern int slurm_unpack_addr_array(slurm_addr_t **addr_array_ptr,
				   uint32_t *size_val, buf_t *buffer);

/**********************************************************************\
 * message packing routines
\**********************************************************************/

/*
 * Pack message into buffers to be ready to send.
 *
 * IN msg - message to pack
 * IN buffers - buffers to populate with packed message
 * IN block_for_forwarding - call the forward_wait() which blocks until
 *    forwarding
 * RET SLURM_SUCCESS or error
 */
extern int slurm_buffers_pack_msg(slurm_msg_t *msg, msg_bufs_t *buffers,
				  bool block_for_forwarding);

/**********************************************************************\
 * simplified communication routines
 * They open a connection do work then close the connection all within
 * the function
\**********************************************************************/

/*
 * Initialize response msg
 *
 * NOTE: This sets SLURM_NO_AUTH_CRED on resp_msg
 *
 * OUT resp_msg - pointer to response message
 * IN source_msg - create response to this message
 * IN msg_type - RPC message type
 * IN data - pointer to message data which corresponds to msg_type
 */
extern void slurm_resp_msg_init(slurm_msg_t *resp_msg, slurm_msg_t *msg,
				uint16_t msg_type, void *data);

/*
 * Send message in response to a source message
 *
 * Handles creating response slurm_msg_t and sending based on source_msg.
 *
 * IN source_msg - Message to send response about
 * IN msg_type - RPC message type
 * IN data - pointer to message data which corresponds to msg_type
 * RET SLURM_SUCCESS or error
 */
extern int send_msg_response(slurm_msg_t *source_msg, slurm_msg_type_t msg_type,
			     void *data);

/* slurm_send_rc_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc 		- the return_code to send back to the client
 */
int slurm_send_rc_msg(slurm_msg_t * request_msg, int rc);

/* slurm_send_rc_err_msg
 * given the original request message this function sends a
 *	slurm_return_code message back to the client that made the request
 * IN request_msg	- slurm_msg the request msg
 * IN rc		- the return_code to send back to the client
 * IN err_msg		- message for user
 */
int slurm_send_rc_err_msg(slurm_msg_t *msg, int rc, char *err_msg);

/*
 * slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message,
 * listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * IN comm_cluster_rec	- Communication record (host/port/version)/
 * RET int 		- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_send_recv_controller_msg(slurm_msg_t * request_msg,
				slurm_msg_t * response_msg,
				slurmdb_cluster_rec_t *comm_cluster_rec);


/* slurm_send_recv_node_msg
 * opens a connection to node,
 * and sends the nodes a message, listens
 * for the response, then closes the connections
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- returns 0 on success, -1 on failure and sets errno
 */
int slurm_send_recv_node_msg(slurm_msg_t * request_msg,
			     slurm_msg_t * response_msg,
			     int timeout);

/*
 *  Send a message to the nodelist specified using fanout
 *    Then return list containing type (ret_data_info_t).
 * IN nodelist	    - list of nodes to send to.
 * IN msg           - a slurm_msg struct to be sent by the function
 * IN timeout	    - how long to wait in milliseconds
 * RET list	    - list containing the responses of the children
 *                    (if any) we forwarded the message to. list
 *                    containing type (ret_types_t).
 */
extern list_t *slurm_send_recv_msgs(const char *nodelist, slurm_msg_t *msg,
				    int timeout);

/*
 * Sends back reroute_msg_t which directs the client to make the request to
 * another cluster.
 *
 * IN msg	  - msg to respond to.
 * IN cluster_rec - cluster to direct msg to.
 */
int slurm_send_reroute_msg(slurm_msg_t *msg,
			   slurmdb_cluster_rec_t *cluster_rec,
			   char *stepmgr);

/*
 * Open a connection to the "address" specified in the slurm msg `req'
 * Then, immediately close the connection w/out waiting for a reply.
 * Ignore any errors. This should only be used when you do not care if
 * the message is ever actually received.
 */
extern void slurm_send_msg_maybe(slurm_msg_t *req);

/*
 *  Send a message to msg->address
 *    Then return list containing type (ret_data_info_t).
 * IN msg           - a slurm_msg struct to be sent by the function
 * IN name          - the name of the node the message is being sent to
 * IN timeout	    - how long to wait in milliseconds
 * RET list	    - list containing the responses of the children
 *                    (if any) we forwarded the message to. list
 *                    containing type (ret_types_t).
 */
extern list_t *slurm_send_addr_recv_msgs(slurm_msg_t *msg, char *name,
					 int timeout);

/*
 *  Same as above, but only to one node
 *  returns 0 on success, -1 on failure and sets errno
 */

int slurm_send_recv_rc_msg_only_one(slurm_msg_t *req, int *rc, int timeout);

/*
 * Send message to controller and get return code.
 * Make use of slurm_send_recv_controller_msg(), which handles
 * support for backup controller and retry during transition.
 * IN req - request to send
 * OUT rc - return code
 * IN comm_cluster_rec	- Communication record (host/port/version)
 * RET - 0 on success, -1 on failure
 */
extern int slurm_send_recv_controller_rc_msg(slurm_msg_t *req, int *rc,
				       slurmdb_cluster_rec_t *comm_cluster_rec);

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a
 * message then, closes the connection
 * IN request_msg	- slurm_msg request
 * IN comm_cluster_rec	- Communication record (host/port/version)
 * RET int		- return code
 * NOTE: NOT INTENDED TO BE CROSS-CLUSTER
 */
extern int slurm_send_only_controller_msg(slurm_msg_t *req,
				slurmdb_cluster_rec_t *comm_cluster_rec);

/* DO NOT USE THIS. See comment in slurm_protocol_api.c for further info. */
extern int slurm_send_only_node_msg(slurm_msg_t *request_msg);

/* Send and recv a slurm request and response on the open slurm descriptor
 * Doesn't close the connection.
 * IN tls_conn
 * IN req	- a slurm_msg struct to be sent by the function
 * OUT resp	- a slurm_msg struct to be filled in by the function
 * IN timeout	- how long to wait in milliseconds
 * RET int	- returns 0 on success, -1 on failure and sets errno
 */
extern int slurm_send_recv_msg(void *tls_conn, slurm_msg_t *req,
			       slurm_msg_t *resp, int timeout);

/* Slurm message functions */

extern void slurm_free_msg_members(slurm_msg_t *msg);
extern void slurm_free_msg(slurm_msg_t * msg);

#define FREE_NULL_MSG(_X)                   \
	do {                                \
		if (_X)                     \
			slurm_free_msg(_X); \
		_X = NULL;                  \
	} while (0)

extern void slurm_msg_set_r_uid(slurm_msg_t *msg, uid_t r_uid);

/* must free this memory with free not xfree */
extern char *nodelist_nth_host(const char *nodelist, int inx);
extern int nodelist_find(const char *nodelist, const char *name);
extern void convert_num_unit2(double num, char *buf, int buf_size,
			      int orig_type, int spec_type, int divisor,
			      uint32_t flags);
extern void convert_num_unit(double num, char *buf, int buf_size,
			     int orig_type, int spec_type, uint32_t flags);
extern int revert_num_unit(const char *buf);
extern int get_convert_unit_val(int base_type, char convert_to);
extern int get_unit_type(char unit);

/*
 * slurm_job_step_create - Ask the slurm controller for a new job step
 *	credential.
 * IN slurm_step_alloc_req_msg - description of job step request
 * OUT slurm_step_alloc_resp_msg - response to request
 * RET SLURM_SUCCESS on success, otherwise return SLURM_ERROR with errno set
 * NOTE: free the response using slurm_free_job_step_create_response_msg
 */
extern int slurm_job_step_create (
	job_step_create_request_msg_t *slurm_step_alloc_req_msg,
	job_step_create_response_msg_t **slurm_step_alloc_resp_msg);


/* Should this be in <slurm/slurm.h> ? */
/*
 * slurm_forward_data - forward arbitrary data to unix domain sockets on nodes
 * IN/OUT nodelist: Nodes to forward data to (if failure this list is changed to
 *                  reflect the failed nodes).
 * IN address: address of unix domain socket
 * IN len: length of data
 * IN data: real data
 * RET: error code
 */
extern int slurm_forward_data(
	char **nodelist, char *address, uint32_t len, const char *data);

/*
 * Setup a slurm_addr_t to be used for communication.
 * If TopologyParameters has NoInAddrAny set it will work off the interface
 * given from gethostname from the hostname of the node.
 * IN/OUT sin - uninitialized slurm_addr_t to initialize
 * IN port - port to use, we will call htons on it
 */
extern void slurm_setup_addr(slurm_addr_t *sin, uint16_t port);

/*
 * slurm_val_to_char - convert an int value (0-16) to a hex value (0-F) char.
 * IN v - value of number (0-16)
 * RET ascii value of int given, -1 on error.
 */
extern int slurm_hex_to_char(int v);

/*
 * slurm_val_to_char - convert a hex value (0-F) char to an int value (0-16).
 * IN c - char value (0-F)
 * RET value as an int, -1 on error.
 */
extern int slurm_char_to_hex(int c);

/*
 * Query assoc share info from slurmctld
 * IN shares_req - which accounts and users to query
 * IN/OUT shares_resp - populated result
 * RET: SLURM_SUCCESS or error
 */
extern int slurm_associations_get_shares(shares_request_msg_t *shares_req,
					 shares_response_msg_t **shares_resp);

#endif
