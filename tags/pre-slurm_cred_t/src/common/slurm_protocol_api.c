/*****************************************************************************\
 *  slurm_protocol_api.c - high-level slurm communication functions
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

/* GLOBAL INCLUDES */

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PROJECT INCLUDES */
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_spec.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_common.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/log.h"

/* EXTERNAL VARIABLES */

/* #DEFINES */
#define CREDENTIAL_TTL_SEC 5
#define _DEBUG	0

/* STATIC VARIABLES */
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;
static slurm_protocol_config_t proto_conf_default;
static slurm_protocol_config_t *proto_conf = &proto_conf_default;
static slurm_ctl_conf_t slurmctld_conf;

/**********************************************************************\
 * protocol configuration functions                
\**********************************************************************/

#if _DEBUG
static void _print_data(char *data, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if ((i % 10 == 0) && (i != 0))
			printf("\n");
		printf("%2.2x ", ((int) data[i] & 0xff));
		if (i >= 200)
			break;
	}
	printf("\n\n");
}
#endif

/* slurm_set_api_config
 * sets the slurm_protocol_config object
 * NOT THREAD SAFE
 * IN protocol_conf		-  slurm_protocol_config object
 */
int slurm_set_api_config(slurm_protocol_config_t * protocol_conf)
{
	proto_conf = protocol_conf;
	return SLURM_SUCCESS;
}

/* slurm_get_api_config
 * returns a pointer to the current slurm_protocol_config object
 * RET slurm_protocol_config_t	- current slurm_protocol_config object
 */
slurm_protocol_config_t *slurm_get_api_config()
{
	return proto_conf;
}

/* slurm_api_set_default_config
 *	called by the send_controller_msg function to insure that at least 
 *	the compiled in default slurm_protocol_config object is initialized
 * RET int 		- return code
 */
int slurm_api_set_default_config()
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&config_lock);
	if ((slurmctld_conf.control_addr != NULL) &&
	    (slurmctld_conf.slurmctld_port != 0))
		goto cleanup;

	read_slurm_conf_ctl(&slurmctld_conf);
	if ((slurmctld_conf.control_addr == NULL) ||
	    (slurmctld_conf.slurmctld_port == 0)) {
		error("Unable to establish control machine or port");
		rc =SLURM_ERROR;
		goto cleanup;
	}

	slurm_set_addr(&proto_conf_default.primary_controller,
		       slurmctld_conf.slurmctld_port,
		       slurmctld_conf.control_addr);
	if (proto_conf_default.primary_controller.sin_port == 0) {
		error("Unable to establish control machine address");
		rc =SLURM_ERROR;
		goto cleanup;
	}

	if (slurmctld_conf.backup_addr) {
		slurm_set_addr(&proto_conf_default.secondary_controller,
			       slurmctld_conf.slurmctld_port,
			       slurmctld_conf.backup_addr);
	}
	proto_conf = &proto_conf_default;

      cleanup:
	slurm_mutex_unlock(&config_lock);
	return rc;
}

/* slurm_get_slurmd_port
 * returns slurmd port from slurmctld_conf object
 * RET short int	- slurmd port
 */
short int slurm_get_slurmd_port(void)
{
	if (slurmctld_conf.slurmd_port == 0)  /* ==0 if config unread */
		slurm_api_set_default_config();

	return slurmctld_conf.slurmd_port;
}

/* slurm_get_slurm_user_id
 * returns slurmd uid from slurmctld_conf object
 * RET uint32_t	- slurm user id
 */
uint32_t slurm_get_slurm_user_id(void)
{
	if (slurmctld_conf.slurmd_port == 0)  /* ==0 if config unread */
		slurm_api_set_default_config();

	return slurmctld_conf.slurm_user_id;
}

/**********************************************************************\
 * general message management functions used by slurmctld, slurmd
\**********************************************************************/

/* In the socket implementation it creates a socket, binds to it, and 
 *	listens for connections.
 * IN port		- port to bind the msg server to
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine_port(uint16_t port)
{
	slurm_addr slurm_address;

	slurm_set_addr_any(&slurm_address, port);
	return _slurm_init_msg_engine(&slurm_address);
}

/* In the socket implementation it creates a socket, binds to it, and 
 *	listens for connections.
 * IN slurm_address 	- slurm_addr to bind the msg server to 
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_init_msg_engine(slurm_addr * slurm_address)
{
	return _slurm_init_msg_engine(slurm_address);
}

/* just calls close on an established msg connection
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int slurm_shutdown_msg_engine(slurm_fd open_fd)
{
	return _slurm_close(open_fd);
}

/* just calls close on an established msg connection to close
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int slurm_shutdown_msg_conn(slurm_fd open_fd)
{
	return _slurm_close(open_fd);
}

/**********************************************************************\
 * msg connection establishment functions used by msg clients
\**********************************************************************/

/* In the bsd socket implementation it creates a SOCK_STREAM socket  
 *	and calls connect on it a SOCK_DGRAM socket called with connect   
 *	is defined to only receive messages from the address/port pair  
 *	argument of the connect call slurm_address - for now it is  
 *	really just a sockaddr_in
 * IN slurm_address 	- slurm_addr of the connection destination
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_open_msg_conn(slurm_addr * slurm_address)
{
	return _slurm_open_msg_conn(slurm_address);
}

/* calls connect to make a connection-less datagram connection to the 
 *	primary or secondary slurmctld message engine
 * RET slurm_fd	- file descriptor of the connection created
 */
slurm_fd slurm_open_controller_conn()
{
	slurm_fd connection_fd;

	connection_fd = slurm_api_set_default_config();

	/* try to send to primary first then secondary */
	if ((connection_fd == SLURM_SUCCESS) &&
	    ((connection_fd =
	      slurm_open_msg_conn(&proto_conf->primary_controller)) ==
	      SLURM_SOCKET_ERROR)) {
		debug("Open connection to primary controller failed: %m");

		if ((slurmctld_conf.backup_controller) &&
		    ((connection_fd =
		      slurm_open_msg_conn(&proto_conf->
					  secondary_controller))
		     == SLURM_SOCKET_ERROR))
			debug
			    ("Open connection to secondary controller failed: %m");
	}

	return connection_fd;
}

/* calls connect to make a connection-less datagram connection to the 
 *	primary or secondary slurmctld message engine
 * RET slurm_fd	- file descriptor of the connection created
 * IN dest 	- controller to contact, primary or secondary
 */
slurm_fd slurm_open_controller_conn_spec(enum controller_id dest)
{
	slurm_fd connection_fd;

	connection_fd = slurm_api_set_default_config();

	if (connection_fd != SLURM_SUCCESS) {
		debug3("slurm_api_set_default_config error");
	} else if (dest == PRIMARY_CONTROLLER) {
		if ((connection_fd =
		     slurm_open_msg_conn(&proto_conf->
					 primary_controller)) ==
		    SLURM_SOCKET_ERROR)
			debug
			    ("Open connection to primary controller failed: %m");
	} else if (slurmctld_conf.backup_controller) {
		if ((connection_fd =
		     slurm_open_msg_conn(&proto_conf->
					 secondary_controller)) ==
		    SLURM_SOCKET_ERROR)
			debug
			    ("Open connection to secondary controller failed: %m");
	} else {
		debug("No secondary controller to contact");
		connection_fd = SLURM_SOCKET_ERROR;
	}

	return connection_fd;
}

/* In the bsd implmentation maps directly to a accept call 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the connection created
 */
slurm_fd slurm_accept_msg_conn(slurm_fd open_fd,
			       slurm_addr * slurm_address)
{
	return _slurm_accept_msg_conn(open_fd, slurm_address);
}

/* In the bsd implmentation maps directly to a close call, to close 
 *	the socket that was accepted
 * IN open_fd		- an open file descriptor to close
 * RET int		- the return code
 */
int slurm_close_accepted_conn(slurm_fd open_fd)
{
	return _slurm_close_accepted_conn(open_fd);
}

/**********************************************************************\
 * receive message functions
\**********************************************************************/

/*
 * NOTE: memory is allocated for the returned msg and must be freed at 
 *	some point using the slurm_free_functions
 * IN open_fd 		- file descriptor to receive msg on
 * OUT msg 		- a slurm_msg struct to be filled in by the function
 * RET int		- size of msg received in bytes before being unpacked
 */
int slurm_receive_msg(slurm_fd open_fd, slurm_msg_t * msg)
{
	char *buftemp;
	header_t header;
	int rc;
	void *auth_cred;
	Buf buffer;

	buftemp = xmalloc(SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE);
	if ((rc = _slurm_msg_recvfrom(open_fd, buftemp,
				      SLURM_PROTOCOL_MAX_MESSAGE_BUFFER_SIZE,
				      SLURM_PROTOCOL_NO_SEND_RECV_FLAGS))
	    == SLURM_SOCKET_ERROR) {
		xfree(buftemp);
		return rc;
	}

	auth_cred = g_slurm_auth_alloc();
	if ( auth_cred == NULL ) {
		xfree( buftemp );
		return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	}
	
#if	_DEBUG
	 _print_data (buftemp,rc);
#endif
	buffer = create_buf(buftemp, rc);

	/* unpack header */
	unpack_header(&header, buffer);
	if ((rc = check_header_version(&header)) != SLURM_SUCCESS) {
		free_buf(buffer);
		slurm_seterrno_ret(SLURM_PROTOCOL_VERSION_ERROR);
	}

	/* unpack authentication cred */
	if (g_slurm_auth_unpack( auth_cred, buffer)) {
		free_buf(buffer);
		slurm_seterrno_ret(ESLURM_PROTOCOL_INCOMPLETE_PACKET);
	}

	/* verify credentials */
	if ((rc = g_slurm_auth_verify(auth_cred)) != SLURM_SUCCESS) {
		g_slurm_auth_free(auth_cred);
		free_buf(buffer);
		slurm_seterrno_ret(SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}

	/* unpack msg body */
	msg->msg_type = header.msg_type;
	if ((header.body_length > remaining_buf(buffer)) ||
	    (unpack_msg(msg, buffer) != SLURM_SUCCESS)) {
		g_slurm_auth_free(auth_cred);
		free_buf(buffer);
		slurm_seterrno_ret(ESLURM_PROTOCOL_INCOMPLETE_PACKET);
	}

	msg->cred = (void *) auth_cred;

	free_buf(buffer);
	return SLURM_SUCCESS;
}

/**********************************************************************\
 * send message functions
\**********************************************************************/

/* sends a slurm_protocol msg to the slurmctld based on location 
 *	information retrieved from the slurmd.conf. if unable to contant 
 *	the primary slurmctld attempts will be made to contact the backup 
 *	controller
 * 
 * IN open_fd	- file descriptor to send msg on
 * IN msg	- a slurm msg struct to be sent
 * RET int	- size of msg sent in bytes
 */
int slurm_send_controller_msg(slurm_fd open_fd, slurm_msg_t * msg)
{
	int rc;
	/* try to send to primary first then secondary */
	msg->address = proto_conf->primary_controller;
	if ((rc = slurm_send_node_msg(open_fd, msg)) == SLURM_SOCKET_ERROR) {
		debug("Send message to primary controller failed: %m");
		msg->address = proto_conf->secondary_controller;
		if ((rc = slurm_send_node_msg(open_fd, msg))
		    == SLURM_SOCKET_ERROR)
			debug
			    ("Send messge to secondary controller failed: %m");
	}
	return rc;
}

/* sends a message to an arbitrary node
 *
 * IN open_fd		- file descriptor to send msg on
 * IN msg		- a slurm msg struct to be sent
 * RET int		- size of msg sent in bytes
 */
int slurm_send_node_msg(slurm_fd open_fd, slurm_msg_t * msg)
{
	header_t header;
	int rc;
	unsigned int msg_len, tmp_len;
	Buf buffer;
	void *auth_cred;

	/* initialize header */
	auth_cred = g_slurm_auth_alloc();
	if ( auth_cred == NULL ) return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
	init_header(&header, msg->msg_type, SLURM_PROTOCOL_NO_FLAGS);
	rc = g_slurm_auth_activate(auth_cred, CREDENTIAL_TTL_SEC);
	if (rc != SLURM_SUCCESS)	/* Try once more */
		rc = g_slurm_auth_activate(auth_cred, CREDENTIAL_TTL_SEC);
	if (rc != SLURM_SUCCESS) {
		error
		    ("slurm_send_node_msg: sending msg with unsigned credential, rc=%d)",
		     rc);
	}

	/* pack header */
	buffer = init_buf(0);
	pack_header(&header, buffer);

	/* pack creds */
	g_slurm_auth_pack(auth_cred, buffer);
	g_slurm_auth_free(auth_cred);

	/* pack msg */
	tmp_len = get_buf_offset(buffer);
	pack_msg(msg, buffer);
	msg_len = get_buf_offset(buffer) - tmp_len;

	/* update header with correct cred and msg lengths */
	update_header(&header, msg_len);
	
	/* repack updated header */
	tmp_len = get_buf_offset(buffer);
	set_buf_offset(buffer, 0);
	pack_header(&header, buffer);
	set_buf_offset(buffer, tmp_len);

	/* send msg */
#if	_DEBUG
	_print_data (get_buf_data(buffer),get_buf_offset(buffer));
#endif
	if ((rc = _slurm_msg_sendto(open_fd, get_buf_data(buffer),
				    get_buf_offset(buffer),
				    SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ))
	     == SLURM_SOCKET_ERROR)
		error("Error sending msg socket: %m");

	free_buf(buffer);
	return rc;
}

/**********************************************************************\
 * stream functions
\**********************************************************************/

/* slurm_listen_stream
 * opens a stream server and listens on it
 * IN slurm_address 	- slurm_addr to bind the server stream to
 * RET slurm_fd		- file descriptor of the stream created
 */
slurm_fd slurm_listen_stream(slurm_addr * slurm_address)
{
	return _slurm_listen_stream(slurm_address);
}

/* slurm_accept_stream
 * accepts a incomming stream connection on a stream server slurm_fd 
 * IN open_fd		- file descriptor to accept connection on
 * OUT slurm_address 	- slurm_addr of the accepted connection
 * RET slurm_fd		- file descriptor of the accepted connection 
 */
slurm_fd slurm_accept_stream(slurm_fd open_fd, slurm_addr * slurm_address)
{
	return _slurm_accept_stream(open_fd, slurm_address);
}

/* slurm_open_stream
 * opens a client connection to stream server
 * IN slurm_address 	- slurm_addr of the connection destination
 * RET slurm_fd         - file descriptor of the connection created
 */
slurm_fd slurm_open_stream(slurm_addr * slurm_address)
{
	return _slurm_open_stream(slurm_address);
}

/* slurm_write_stream
 * writes a buffer out a stream file descriptor
 * IN open_fd		- file descriptor to write on
 * IN buffer		- buffer to send
 * IN size		- size of buffer send
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes sent , or -1 on errror
 */
size_t slurm_write_stream(slurm_fd open_fd, char *buffer, size_t size)
{
	return _slurm_send_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   SLURM_MESSGE_TIMEOUT_MSEC_STATIC);
}
size_t slurm_write_stream_timeout(slurm_fd open_fd, char *buffer,
				  size_t size, int timeout)
{
	return _slurm_send_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   timeout);
}

/* slurm_read_stream
 * read into buffer grom a stream file descriptor
 * IN open_fd		- file descriptor to read from
 * OUT buffer	- buffer to receive into
 * IN size		- size of buffer
 * IN timeout		- how long to wait in milliseconds
 * RET size_t		- bytes read , or -1 on errror
 */
size_t slurm_read_stream(slurm_fd open_fd, char *buffer, size_t size)
{
	return _slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   SLURM_MESSGE_TIMEOUT_MSEC_STATIC);
}
size_t slurm_read_stream_timeout(slurm_fd open_fd, char *buffer,
				 size_t size, int timeout)
{
	return _slurm_recv_timeout(open_fd, buffer, size,
				   SLURM_PROTOCOL_NO_SEND_RECV_FLAGS,
				   timeout);
}

/* slurm_get_stream_addr
 * esentially a encapsilated get_sockname  
 * IN open_fd 		- file descriptor to retreive slurm_addr for
 * OUT address		- address that open_fd to bound to
 */
int slurm_get_stream_addr(slurm_fd open_fd, slurm_addr * address)
{
	return _slurm_get_stream_addr(open_fd, address);
}

/* slurm_close_stream
 * closes either a server or client stream file_descriptor
 * IN open_fd	- an open file descriptor to close
 * RET int	- the return code
 */
int slurm_close_stream(slurm_fd open_fd)
{
	return _slurm_close_stream(open_fd);
}

/* make an open slurm connection blocking or non-blocking
 *	(i.e. wait or do not wait for i/o completion )
 * IN open_fd	- an open file descriptor to change the effect
 * RET int	- the return code
 */
int slurm_set_stream_non_blocking(slurm_fd open_fd)
{
	return _slurm_set_stream_non_blocking(open_fd);
}
int slurm_set_stream_blocking(slurm_fd open_fd)
{
	return _slurm_set_stream_blocking(open_fd);
}

/**********************************************************************\
 * address conversion and management functions
\**********************************************************************/

/* slurm_set_addr_uint
 * initializes the slurm_address with the supplied port and ip_address
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN ip_address	- ipv4 address in uint32 host order form
 */
void slurm_set_addr_uint(slurm_addr * slurm_address, uint16_t port,
			 uint32_t ip_address)
{
	_slurm_set_addr_uint(slurm_address, port, ip_address);
}

/* slurm_set_addr_any
 * initialized the slurm_address with the supplied port on INADDR_ANY
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 */
void slurm_set_addr_any(slurm_addr * slurm_address, uint16_t port)
{
	_slurm_set_addr_uint(slurm_address, port, SLURM_INADDR_ANY);
}

/* slurm_set_addr
 * initializes the slurm_address with the supplied port and host name
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void slurm_set_addr(slurm_addr * slurm_address, uint16_t port, char *host)
{
	_slurm_set_addr_char(slurm_address, port, host);
}

/* reset_slurm_addr
 * resets the address field of a slurm_addr, port and family unchanged
 * OUT slurm_address	- slurm_addr to be reset in
 * IN new_address	- source of address to write into slurm_address
 */
void reset_slurm_addr(slurm_addr * slurm_address, slurm_addr new_address)
{
	_reset_slurm_addr(slurm_address, new_address);
}

/* slurm_set_addr_char
 * initializes the slurm_address with the supplied port and host
 * OUT slurm_address	- slurm_addr to be filled in
 * IN port		- port in host order
 * IN host		- hostname or dns name 
 */
void slurm_set_addr_char(slurm_addr * slurm_address, uint16_t port,
			 char *host)
{
	_slurm_set_addr_char(slurm_address, port, host);
}

/* slurm_get_addr 
 * given a slurm_address it returns its port and hostname
 * IN slurm_address	- slurm_addr to be queried
 * OUT port		- port number
 * OUT host		- hostname
 * IN buf_len		- length of hostname buffer
 */
void slurm_get_addr(slurm_addr * slurm_address, uint16_t * port,
		    char *host, unsigned int buf_len)
{
	_slurm_get_addr(slurm_address, port, host, buf_len);
}

/* slurm_get_peer_addr
 * get the slurm address of the peer connection, similar to getpeeraddr
 * IN fd		- an open connection
 * OUT slurm_address	- place to park the peer's slurm_addr
 */
int slurm_get_peer_addr(slurm_fd fd, slurm_addr * slurm_address)
{
	struct sockaddr name;
	socklen_t namelen = (socklen_t) sizeof(struct sockaddr);
	int rc;

	if ((rc = _slurm_getpeername((int) fd, &name, &namelen)))
		return rc;
	memcpy(slurm_address, &name, sizeof(slurm_addr));
	return 0;
}

/* slurm_print_slurm_addr
 * prints a slurm_addr into a buf
 * IN address		- slurm_addr to print
 * IN buf		- space for string representation of slurm_addr
 * IN n			- max number of bytes to write (including NUL)
 */
void slurm_print_slurm_addr(slurm_addr * address, char *buf, size_t n)
{
	_slurm_print_slurm_addr(address, buf, n);
}

/**********************************************************************\
 * slurm_addr pack routines
\**********************************************************************/

/* slurm_pack_slurm_addr
 * packs a slurm_addr into a buffer to serialization transport
 * IN slurm_address	- slurm_addr to pack
 * IN/OUT buffer	- buffer to pack the slurm_addr into
 */
void slurm_pack_slurm_addr(slurm_addr * slurm_address, Buf buffer)
{
	_slurm_pack_slurm_addr(slurm_address, buffer);
}

/* slurm_pack_slurm_addr
 * unpacks a buffer into a slurm_addr after serialization transport
 * OUT slurm_address	- slurm_addr to unpack to
 * IN/OUT buffer	- buffer to upack the slurm_addr from
 * returns 		- SLURM error code
 */
int slurm_unpack_slurm_addr_no_alloc(slurm_addr * slurm_address,
				     Buf buffer)
{
	return _slurm_unpack_slurm_addr_no_alloc(slurm_address, buffer);
}

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
void slurm_send_rc_msg(slurm_msg_t * request_msg, int rc)
{
	slurm_msg_t response_msg;
	return_code_msg_t rc_msg;

	/* no change */
	rc_msg.return_code = rc;
	/* init response_msg structure */
	response_msg.address = request_msg->address;
	response_msg.msg_type = RESPONSE_SLURM_RC;
	response_msg.data = &rc_msg;

	/* send message */
	slurm_send_node_msg(request_msg->conn_fd, &response_msg);
}

/* slurm_send_recv_controller_msg
 * opens a connection to the controller, sends the controller a message, 
 * listens for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- return code
 */
int slurm_send_recv_controller_msg(slurm_msg_t * request_msg,
				   slurm_msg_t * response_msg)
{
	int msg_size;
	int rc;
	slurm_fd sockfd;
	int error_code = 0;

	/* init message connection for communication with controller */
	if ((sockfd = slurm_open_controller_conn()) == SLURM_SOCKET_ERROR) {
		return SLURM_SOCKET_ERROR;
	}

	/* send request message */
	if ((rc =
	     slurm_send_controller_msg(sockfd,
				       request_msg)) ==
	    SLURM_SOCKET_ERROR) {
		error_code = 1;
		goto slurm_send_recv_controller_msg_cleanup;
	}

	/* receive message */
	if ((msg_size =
	     slurm_receive_msg(sockfd,
			       response_msg)) == SLURM_SOCKET_ERROR) {
		error_code = 1;
		goto slurm_send_recv_controller_msg_cleanup;
	}

      slurm_send_recv_controller_msg_cleanup:
	/* shutdown message connection */
	if ((rc = slurm_shutdown_msg_conn(sockfd)) == SLURM_SOCKET_ERROR)
		return SLURM_SOCKET_ERROR;
	if (error_code)
		return SLURM_SOCKET_ERROR;

	return SLURM_SUCCESS;
}

/* slurm_send_recv_node_msg
 * opens a connection to node, sends the node a message, listens 
 * for the response, then closes the connection
 * IN request_msg	- slurm_msg request
 * OUT response_msg	- slurm_msg response
 * RET int 		- return code
 */
int slurm_send_recv_node_msg(slurm_msg_t * request_msg,
			     slurm_msg_t * response_msg)
{
	int msg_size;
	int rc;
	slurm_fd sockfd;
	int error_code = 0;

	/* init message connection for communication with controller */
	if ((sockfd =
	     slurm_open_msg_conn(&request_msg->address)) ==
	    SLURM_SOCKET_ERROR)
		return SLURM_SOCKET_ERROR;

	/* send request message */
	if ((rc =
	     slurm_send_node_msg(sockfd,
				 request_msg)) == SLURM_SOCKET_ERROR) {
		error_code = 1;
		goto slurm_send_recv_node_msg_cleanup;
	}

	/* receive message */
	if ((msg_size =
	     slurm_receive_msg(sockfd,
			       response_msg)) == SLURM_SOCKET_ERROR) {
		error_code = 1;
		goto slurm_send_recv_node_msg_cleanup;
	}

      slurm_send_recv_node_msg_cleanup:
	/* shutdown message connection */
	if ((rc = slurm_shutdown_msg_conn(sockfd)) == SLURM_SOCKET_ERROR)
		return SLURM_SOCKET_ERROR;
	if (error_code)
		return SLURM_SOCKET_ERROR;

	return SLURM_SUCCESS;

}

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a 
 * message then, closes the connection
 * IN request_msg	- slurm_msg request
 * RET int 		- return code
 */
int slurm_send_only_controller_msg(slurm_msg_t * request_msg)
{
	int rc;
	slurm_fd sockfd;
	int error_code = 0;

	/* init message connection for communication with controller */
	if ((sockfd = slurm_open_controller_conn()) == SLURM_SOCKET_ERROR)
		return SLURM_SOCKET_ERROR;

	/* send request message */
	if ((rc =
	     slurm_send_controller_msg(sockfd,
				       request_msg)) ==
	    SLURM_SOCKET_ERROR) {
		error_code = 1;
		goto slurm_send_only_controller_msg_cleanup;
	}
      slurm_send_only_controller_msg_cleanup:
	/* shutdown message connection */
	if ((rc = slurm_shutdown_msg_conn(sockfd)) == SLURM_SOCKET_ERROR)
		return SLURM_SOCKET_ERROR;
	if (error_code)
		return SLURM_SOCKET_ERROR;


	return SLURM_SUCCESS;

}

/* slurm_send_only_controller_msg
 * opens a connection to the controller, sends the controller a 
 * message then, closes the connection
 * IN request_msg	- slurm_msg request
 * RET int 		- return code
 */
int slurm_send_only_node_msg(slurm_msg_t * request_msg)
{
	int rc;
	slurm_fd sockfd;
	int error_code = 0;

	/* init message connection for communication with controller */
	if ((sockfd = slurm_open_msg_conn(&request_msg->address)) ==
	    SLURM_SOCKET_ERROR)
		return SLURM_SOCKET_ERROR;

	/* send request message */
	if ((rc = slurm_send_node_msg(sockfd, request_msg)) < 0) {
		error_code = 1;
		goto slurm_send_only_node_msg_cleanup;
	}
      slurm_send_only_node_msg_cleanup:
	/* shutdown message connection */
	if ((rc = slurm_shutdown_msg_conn(sockfd)) == SLURM_SOCKET_ERROR)
		return SLURM_SOCKET_ERROR;
	if (error_code)
		return SLURM_SOCKET_ERROR;


	return SLURM_SUCCESS;
}

/* Slurm message functions */
void slurm_free_msg(slurm_msg_t * msg)
{
	g_slurm_auth_free(msg->cred);
	xfree(msg);
}
