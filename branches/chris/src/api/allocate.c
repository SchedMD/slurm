/*****************************************************************************\
 *  allocate.c - allocate nodes for a job or step with supplied contraints
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <poll.h>
#include <stdbool.h>
#include <time.h>
#include <netinet/in.h> /* for ntohs() */

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
#endif

#include <slurm/slurm.h>
#include <stdlib.h>

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/forward.h"
#include "src/common/fd.h"
#include "src/common/slurm_auth.h"

#define BUFFER_SIZE 1024
#define MAX_ALLOC_WAIT 60	/* seconds */
#define MIN_ALLOC_WAIT  5	/* seconds */

typedef struct {
	slurm_addr address;
	int fd;
	char *hostname;
	uint16_t port;
} listen_t;

static int _handle_rc_msg(slurm_msg_t *msg);
static listen_t *_create_allocation_response_socket();
static void _destroy_allocation_response_socket(listen_t *listen);
static resource_allocation_response_msg_t *_wait_for_allocation_response(
	uint32_t job_id, const listen_t *listen, int timeout);

/*
 * slurm_allocate_resources - allocate resources for a job request
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the allocated using slurm_free_resource_allocation_response_msg
 */
int
slurm_allocate_resources (job_desc_msg_t *req, 
			  resource_allocation_response_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	bool host_set = false;
	char host[64];
	/* 
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	if ( (req->alloc_node == NULL) 
	    && (getnodename(host, sizeof(host)) == 0) ) {
		req->alloc_node = host;
		host_set  = true;
	}

	req_msg.msg_type = REQUEST_RESOURCE_ALLOCATION;
	req_msg.data     = req; 
	forward_init(&req_msg.forward, NULL);
	forward_init(&resp_msg.forward, NULL);
	req_msg.ret_list = NULL;
	resp_msg.ret_list = NULL;
	req_msg.forward_struct_init = 0;
	resp_msg.forward_struct_init = 0;
		
	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);

	/*
	 *  Clear this hostname if set internally to this function
	 *    (memory is on the stack)
	 */
	if (host_set)
		req->alloc_node = NULL;

	if (rc == SLURM_SOCKET_ERROR) 
		return SLURM_SOCKET_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_PROTOCOL_ERROR;
		*resp = NULL;
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		*resp = (resource_allocation_response_msg_t *) resp_msg.data;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return SLURM_PROTOCOL_SUCCESS;
}


/*
 * slurm_allocate_resources_blocking
 *	allocate resources for a job request.  This call will block until
 *	the allocation is granted, or the specified timeout limit is reached.
 * IN req - description of resource allocation request
 * IN timeout - amount of time to wait for a response before giving up.
 *	A timeout of zero will wait indefinitely.
 * 
 * RET allocation structure on success, NULL on error set errno to
 *	indicate the error
 * NOTE: free the allocation structure using
 *	slurm_free_resource_allocation_response_msg
 */
resource_allocation_response_msg_t *
slurm_allocate_resources_blocking (const job_desc_msg_t *user_req, time_t timeout)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	resource_allocation_response_msg_t *resp = NULL;
	char hostname[64];
	bool hostname_is_set = false;
	uint32_t job_id;
	job_desc_msg_t *req;
	listen_t *listen = NULL;

	if (timeout == 0)
		timeout = (time_t)-1;

	/* make a copy of the user's job description struct so that we
	 * can make changes before contacting the controller */
	req = (job_desc_msg_t *)xmalloc(sizeof(job_desc_msg_t));
	if (req == NULL)
		return NULL;
	*req = *user_req;

	/* 
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	hostname[0] = '\0';
	if (getnodename(hostname, sizeof(hostname)) == 0)
	    hostname_is_set = true;
	if ((req->alloc_node == NULL) && hostname_is_set) {
		req->alloc_node = hostname;
	}

	if (hostname_is_set) {
		listen = _create_allocation_response_socket(hostname);
		req->host = listen->hostname;
		req->port = listen->port;
		req->immediate = 0;
	}

	req_msg.msg_type = REQUEST_RESOURCE_ALLOCATION;
	req_msg.data     = req; 
	forward_init(&req_msg.forward, NULL);
	forward_init(&resp_msg.forward, NULL);
	req_msg.ret_list = NULL;
	resp_msg.ret_list = NULL;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);

	if (rc == SLURM_SOCKET_ERROR) {
		errno = SLURM_SOCKET_ERROR;
		return NULL;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0) {
			errno = SLURM_PROTOCOL_ERROR;
			return NULL;
		}
		errno = -1; /* FIXME - need to figure out what the correct
			       error code would be */
		return NULL;
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		/* Yay, the controller has acknowledge our request!  But did
		   we really get an allocation yet? */
		resp = (resource_allocation_response_msg_t *) resp_msg.data;
		if (resp->node_cnt > 0) {
			/* yes, allocation has been granted */
			errno = SLURM_PROTOCOL_SUCCESS;
		} else {
			/* no, we need to wait for a response */
			job_id = resp->job_id;
			slurm_free_resource_allocation_response_msg(resp);
			verbose("Allocation request enqueued, "
				"listening for response on port %u",
				listen->port);
			printf("Allocation request enqueued, "
				"listening for response on port %u\n",
				listen->port);
 			resp = _wait_for_allocation_response(job_id, listen,
							     timeout);
			/* If NULL, we didn't get the allocation in 
			   the time desired, so just free the job id */
			if (resp == NULL)
				slurm_complete_job(job_id, -1);
		}
		break;
	default:
		errno = SLURM_UNEXPECTED_MSG_ERROR;
		return NULL;
	}

	_destroy_allocation_response_socket(listen);
	errno = SLURM_PROTOCOL_SUCCESS;
	return resp;
}


/*
 * slurm_job_will_run - determine if a job would execute immediately if 
 *	submitted now
 * IN job_desc_msg - description of resource allocation request
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int slurm_job_will_run (job_desc_msg_t *req)
{
	slurm_msg_t req_msg;
	int rc;

	/* req.immediate = true;    implicit */

	req_msg.msg_type = REQUEST_JOB_WILL_RUN;
	req_msg.data     = req; 
	forward_init(&req_msg.forward, NULL);
	req_msg.ret_list = NULL;
	req_msg.forward_struct_init = 0;
	
	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
		return SLURM_SOCKET_ERROR;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_job_step_create - create a job step for a given job id
 * IN slurm_step_alloc_req_msg - description of job step request
 * OUT slurm_step_alloc_resp_msg - response to request
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the response using slurm_free_job_step_create_response_msg
 */
int
slurm_job_step_create (job_step_create_request_msg_t *req, 
                       job_step_create_response_msg_t **resp)
{
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	req_msg.msg_type = REQUEST_JOB_STEP_CREATE;
	req_msg.data     = req; 
	forward_init(&req_msg.forward, NULL);
	req_msg.ret_list = NULL;
	req_msg.forward_struct_init = 0;
	forward_init(&resp_msg.forward, NULL);
	resp_msg.ret_list = NULL;
	resp_msg.forward_struct_init = 0;
	
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_PROTOCOL_ERROR;
		*resp = NULL;
		break;
	case RESPONSE_JOB_STEP_CREATE:
		*resp = (job_step_create_response_msg_t *) resp_msg.data;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * OBSOLETE! This function, along with the old_job_alloc_msg_t
 *           structure, will go away in a future version of SLURM.  Use
 *           slurm_allocation_lookup() instead.
 * slurm_confirm_allocation - confirm an existing resource allocation
 * IN job_desc_msg - description of existing job request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the response using slurm_free_resource_allocation_response_msg
 */
int 
slurm_confirm_allocation (old_job_alloc_msg_t *req, 
			  resource_allocation_response_msg_t **resp) 
{
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	req_msg.msg_type = REQUEST_OLD_JOB_RESOURCE_ALLOCATION;
	req_msg.data     = req; 
	forward_init(&req_msg.forward, NULL);
	req_msg.ret_list = NULL;
	req_msg.forward_struct_init = 0;
	forward_init(&resp_msg.forward, NULL);
	resp_msg.ret_list = NULL;
	resp_msg.forward_struct_init = 0;
	
	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch(resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*resp = NULL;
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		*resp = (resource_allocation_response_msg_t *) resp_msg.data;
		return SLURM_PROTOCOL_SUCCESS;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_allocation_lookup - retrieve info for an existing resource allocation
 * IN jobid - job allocation identifier
 * OUT info - job allocation information
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the "resp" using slurm_free_resource_allocation_response_msg
 */
int
slurm_allocation_lookup(uint32_t jobid,
			resource_allocation_response_msg_t **info)
{
	old_job_alloc_msg_t req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	req.job_id = jobid;
	req_msg.msg_type = REQUEST_OLD_JOB_RESOURCE_ALLOCATION;
	req_msg.data     = &req; 

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch(resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*info = NULL;
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		*info = (resource_allocation_response_msg_t *) resp_msg.data;
		return SLURM_PROTOCOL_SUCCESS;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 *  Handle a return code message type. 
 *    if return code is nonzero, sets errno to return code and returns < 0.
 *    Otherwise, returns 0 (SLURM_SUCCES)
 */
static int
_handle_rc_msg(slurm_msg_t *msg)
{
	int rc = ((return_code_msg_t *) msg->data)->return_code;
	slurm_free_return_code_msg(msg->data);

	if (rc) 
		slurm_seterrno_ret(rc);
	else
		return SLURM_SUCCESS;
}

/*
 * Read a SLURM hostfile specified by "filename".  "filename" must contain
 * a list of SLURM NodeNames, one per line.  Reads up to "n" number of hostnames
 * from the file. Returns a string representing a hostlist ranged string of 
 * the contents of the file.  This is a helper function, it does not
 * contact any SLURM daemons.
 *
 * Returns a string representing the hostlist.  Returns NULL if there are fewer
 * than "n" hostnames in the file, or if an error occurs.
 *
 * Returned string must be freed with free().
 */
char *slurm_read_hostfile(char *filename, int n)
{
	FILE *fp = NULL;
	char in_line[BUFFER_SIZE];	/* input line */
	int i, j;
	int line_size;
	int line_num = 0;
	hostlist_t hostlist = NULL;
	char *nodelist = NULL;
	
	if (filename == NULL || strlen(filename) == 0)
		return NULL;

	if((fp = fopen(filename, "r")) == NULL) {
		error("slurm_allocate_resources error opening file %s, %m",
		      filename);
		return NULL;
	}

	hostlist = hostlist_create(NULL);
	if (hostlist == NULL)
		return NULL;

	while (fgets(in_line, BUFFER_SIZE, fp) != NULL) {
		line_num++;
		line_size = strlen(in_line);
		if (line_size == (BUFFER_SIZE - 1)) {
			error ("Line %d, of hostfile %s too long",
			       line_num, filename);
			fclose (fp);
			return NULL;
		}

		for (i = 0; i < line_size; i++) {
			if (in_line[i] == '\n') {
				in_line[i] = '\0';
				break;
			}
			if (in_line[i] == '\0')
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {
				for (j = i; j < line_size; j++) {
					in_line[j - 1] = in_line[j];
				}
				line_size--;
				continue;
			}	
			in_line[i] = '\0';
			break;
		}
			
		hostlist_push(hostlist, in_line);
		if(hostlist_count(hostlist) == n) 
			break; 
	}
	fclose(fp);

	if (hostlist_count(hostlist) <= 0) {
		error("Hostlist is empty!\n");
		goto cleanup_hostfile;
	}
	if (hostlist_count(hostlist) < n) {
		error("Too few NodeNames in SLURM Hostfile");
		goto cleanup_hostfile;
	}		

	nodelist = (char *)malloc(0xffff);
	if (!nodelist) {
		error("Nodelist xmalloc failed");
		goto cleanup_hostfile;
	}

	if (hostlist_ranged_string(hostlist, 0xffff, nodelist) == -1) {
		error("Hostlist is too long for the allocate RPC!");
		free(nodelist);
		nodelist = NULL;
		goto cleanup_hostfile;
	}

	debug2("Hostlist from SLURM_HOSTFILE = %s\n", nodelist);

cleanup_hostfile:
	hostlist_destroy(hostlist);

	return nodelist;
}

/***************************************************************************
 * Support functions for slurm_allocate_resources_blocking()
 * FIXME - get rid of all fatal() calls
 ***************************************************************************/
static listen_t *_create_allocation_response_socket(char *interface_hostname)
{
	listen_t *listen = NULL;

	listen = xmalloc(sizeof(listen_t));
	if (listen == NULL)
		return NULL;

	/* port "0" lets the operating system pick any port */
	slurm_set_addr(&listen->address, 0, interface_hostname);
	if ((listen->fd = slurm_init_msg_engine(&listen->address)) < 0)
		fatal("slurm_init_msg_engine_port error %m");
	if (slurm_get_stream_addr(listen->fd, &listen->address) < 0)
		fatal("slurm_get_stream_addr error %m");
	listen->hostname = xstrdup(interface_hostname);
	/* FIXME - screw it!  I can't seem to get the port number through slurm_*
	   functions */
	listen->port = ntohs(listen->address.sin_port);
	fd_set_nonblocking(listen->fd);

	return listen;
}

static void _destroy_allocation_response_socket(listen_t *listen)
{
	xassert(listen != NULL);

	slurm_shutdown_msg_engine(listen->fd);
	if (listen->hostname)
		xfree(listen->hostname);
	xfree(listen);
}

/* process RPC from slurmctld
 * IN msg: message recieved
 * OUT resp: resource allocation response message
 * RET 1 if resp is filled in, 0 otherwise */
/* FIXME - If the controller and protocol allowed seperate hostname/port
   for allocation response and another for the pinger, then we wouldn't
   need to handle the ping rpc here.  In fact, since this listening socket
   goes away when the allocation is granted, we will probably trigger the
   inactive limit prematurely! */
static int
_handle_msg(slurm_msg_t *msg, resource_allocation_response_msg_t **resp)
{
	uid_t req_uid   = g_slurm_auth_get_uid(msg->auth_cred);
	uid_t uid       = getuid();
	uid_t slurm_uid = (uid_t) slurm_get_slurm_user_id();
	int rc = 0;

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u",
			(unsigned int) req_uid);
		return 0;
	}

	switch (msg->msg_type) {
		case SRUN_PING:
			debug3("slurmctld ping received");
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			slurm_free_srun_ping_msg(msg->data);
			break;
		case RESPONSE_RESOURCE_ALLOCATION:
			debug2("resource allocation response received");
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			*resp = msg->data;
			rc = 1;
			break;
		default:
			error("received spurious message type: %d\n",
				 msg->msg_type);
	}
	return rc;
}

/* Accept RPC from slurmctld and process it.
 * IN slurmctld_fd: file descriptor for slurmctld communications
 * OUT resp: resource allocation response message
 * RET 1 if resp is filled in, 0 otherwise */
static int 
_accept_msg_connection(int listen_fd, resource_allocation_response_msg_t **resp)
{
	int	     conn_fd;
	slurm_msg_t *msg = NULL;
	slurm_addr   cli_addr;
	char         host[256];
	uint16_t     port;
	int          rc = 0;
	List ret_list;

	conn_fd = slurm_accept_msg_conn(listen_fd, &cli_addr);
	if (conn_fd < 0) {
		error("Unable to accept connection: %m");
		return rc;
	}

	slurm_get_addr(&cli_addr, &port, host, sizeof(host));
	debug2("got message connection from %s:%d", host, port);

	msg = xmalloc(sizeof(slurm_msg_t));
	forward_init(&msg->forward, NULL);
	msg->ret_list = NULL;
	msg->conn_fd = conn_fd;
	
  again:
	ret_list = slurm_receive_msg(conn_fd, msg, 0);

	if (!ret_list || errno != SLURM_SUCCESS) {
		if (errno == EINTR) {
			goto again;
		}
		if (ret_list)
			list_destroy(ret_list);
			
		error("_accept_msg_connection[%s]: %m", host);
		slurm_free_msg(msg);
		return SLURM_ERROR;
	}
	if (list_count(ret_list)>0) {
		error("_accept_msg_connection: "
		      "got %d from receive, expecting 0",
		      list_count(ret_list));
	}
	msg->ret_list = ret_list;
	
	rc = _handle_msg(msg, resp); /* handle_msg frees msg */
	slurm_free_msg(msg);
		
	slurm_close_accepted_conn(conn_fd);
	return rc;
}

/* Wait up to sleep_time for RPC from slurmctld indicating resource allocation
 * has occured.
 * IN sleep_time: delay in seconds
 * OUT resp: resource allocation response message
 * RET 1 if resp is filled in, 0 otherwise */
static int
_wait_for_alloc_rpc(const listen_t *listen, int sleep_time,
		    resource_allocation_response_msg_t **resp)
{
	struct pollfd fds[1];

	fds[0].fd = listen->fd;
	fds[0].events = POLLIN;

	while (poll (fds, 1, (sleep_time * 1000)) < 0) {
		switch (errno) {
			case EAGAIN:
			case EINTR:
				*resp = NULL;
				return (-1);
			case ENOMEM:
			case EINVAL:
			case EFAULT:
				fatal("poll: %m");
			default:
				error("poll: %m. Continuing...");
		}
	}

	if (fds[0].revents & POLLIN)
		return (_accept_msg_connection(listen->fd, resp));

	return (0);
}

static resource_allocation_response_msg_t *
_wait_for_allocation_response(uint32_t job_id, const listen_t *listen,
			      int timeout)
{
	resource_allocation_response_msg_t *resp;

	debug ("job %u queued and waiting for resources", job_id);
	if (_wait_for_alloc_rpc(listen, timeout, &resp) <= 0) {
		/* Maybe the resource allocation response RPC got lost
		 * in the mail; surely it should have arrived by now.
		 * Let's see if the controller thinks that the allocation
		 * has been granted.
		 */
		if (slurm_allocation_lookup(job_id, &resp) >= 0)
			return resp;

		if (slurm_get_errno() == ESLURM_JOB_PENDING) 
			debug3 ("Still waiting for allocation");
		else {
			debug3 ("Unable to confirm allocation for job %u: %m", 
			       job_id);
			return NULL;
		}
	}

	return resp;
}
