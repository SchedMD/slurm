/*****************************************************************************\
 *  allocate.c - allocate nodes for a job or step with supplied contraints
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/poll.h>
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
#include "src/common/parse_time.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_defs.h"

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

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	/*
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	if ( (req->alloc_node == NULL)
	    && (gethostname_short(host, sizeof(host)) == 0) ) {
		req->alloc_node = host;
		host_set  = true;
	}

	req_msg.msg_type = REQUEST_RESOURCE_ALLOCATION;
	req_msg.data     = req;

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
 * IN timeout - amount of time, in seconds, to wait for a response before
 * 	giving up.
 *	A timeout of zero will wait indefinitely.
 * IN pending_callback - If the allocation cannot be granted immediately,
 *      the controller will put the job in the PENDING state.  If
 *      pending callback is not NULL, it will be called with the job_id
 *      of the pending job as the sole parameter.
 *
 * RET allocation structure on success, NULL on error set errno to
 *	indicate the error (errno will be ETIMEDOUT if the timeout is reached
 *      with no allocation granted)
 * NOTE: free the allocation structure using
 *	slurm_free_resource_allocation_response_msg
 */
resource_allocation_response_msg_t *
slurm_allocate_resources_blocking (const job_desc_msg_t *user_req,
				   time_t timeout,
				   void(*pending_callback)(uint32_t job_id))
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	resource_allocation_response_msg_t *resp = NULL;
	char *hostname = NULL;
	uint32_t job_id;
	job_desc_msg_t *req;
	listen_t *listen = NULL;
	int errnum = SLURM_SUCCESS;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	/* make a copy of the user's job description struct so that we
	 * can make changes before contacting the controller */
	req = (job_desc_msg_t *)xmalloc(sizeof(job_desc_msg_t));
	if (req == NULL)
		return NULL;
	memcpy(req, user_req, sizeof(job_desc_msg_t));

	/*
	 * set Node and session id for this request
	 */
	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	if (user_req->alloc_node != NULL) {
		req->alloc_node = xstrdup(user_req->alloc_node);
	} else if ((hostname = xshort_hostname()) != NULL) {
		req->alloc_node = hostname;
	} else {
		error("Could not get local hostname,"
		      " forcing immediate allocation mode.");
		req->immediate = 1;
	}

	if (!req->immediate) {
		listen = _create_allocation_response_socket(hostname);
		if (listen == NULL) {
			xfree(req);
			return NULL;
		}
		req->alloc_resp_port = listen->port;
	}

	req_msg.msg_type = REQUEST_RESOURCE_ALLOCATION;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);

	if (rc == SLURM_SOCKET_ERROR) {
		int errnum = errno;
		destroy_forward(&req_msg.forward);
		destroy_forward(&resp_msg.forward);
		if (!req->immediate)
			_destroy_allocation_response_socket(listen);
		xfree(req);
		errno = errnum;
		return NULL;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0) {
			/* will reach this when the allocation fails */
			errnum = errno;
		} else {
			/* shouldn't get here */
			errnum = -1;
		}
		break;
	case RESPONSE_RESOURCE_ALLOCATION:
		/* Yay, the controller has acknowledge our request!  But did
		   we really get an allocation yet? */
		resp = (resource_allocation_response_msg_t *) resp_msg.data;
		if (resp->node_cnt > 0) {
			/* yes, allocation has been granted */
			errno = SLURM_PROTOCOL_SUCCESS;
		} else if (!req->immediate) {
			if (resp->error_code != SLURM_SUCCESS)
				info("%s", slurm_strerror(resp->error_code));
			/* no, we need to wait for a response */
			job_id = resp->job_id;
			slurm_free_resource_allocation_response_msg(resp);
			if (pending_callback != NULL)
				pending_callback(job_id);
 			resp = _wait_for_allocation_response(job_id, listen,
							     timeout);
			/* If NULL, we didn't get the allocation in
			   the time desired, so just free the job id */
			if ((resp == NULL) && (errno != ESLURM_ALREADY_DONE)) {
				errnum = errno;
				slurm_complete_job(job_id, -1);
			}
		}
		break;
	default:
		errnum = SLURM_UNEXPECTED_MSG_ERROR;
		resp = NULL;
	}

	destroy_forward(&req_msg.forward);
	destroy_forward(&resp_msg.forward);
	if (!req->immediate)
		_destroy_allocation_response_socket(listen);
	xfree(req);
	errno = errnum;
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
	slurm_msg_t req_msg, resp_msg;
	will_run_response_msg_t *will_run_resp;
	char buf[64];
	bool host_set = false;
	int rc;

	/* req.immediate = true;    implicit */
	if ((req->alloc_node == NULL) &&
	    (gethostname_short(buf, sizeof(buf)) == 0)) {
		req->alloc_node = buf;
		host_set = true;
	}
	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = REQUEST_JOB_WILL_RUN;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);

	if (host_set)
		req->alloc_node = NULL;

	if (rc < 0)
		return SLURM_SOCKET_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_PROTOCOL_ERROR;
		break;
	case RESPONSE_JOB_WILL_RUN:
		will_run_resp = (will_run_response_msg_t *) resp_msg.data;
		slurm_make_time_str(&will_run_resp->start_time,
				    buf, sizeof(buf));
		info("Job %u to start at %s using %u "
#ifdef HAVE_BG
		     "cnodes"
#else
		     "processors"
#endif
		     " on %s",
		     will_run_resp->job_id, buf,
		     will_run_resp->proc_cnt,
		     will_run_resp->node_list);
		if (will_run_resp->preemptee_job_id) {
			ListIterator itr;
			uint32_t *job_id_ptr;
			char *job_list = NULL, *sep = "";
			itr = list_iterator_create(will_run_resp->
						   preemptee_job_id);
			while ((job_id_ptr = list_next(itr))) {
				if (job_list)
					sep = ",";
				xstrfmtcat(job_list, "%s%u", sep, *job_id_ptr);
			}
			info("  Preempts: %s", job_list);
			xfree(job_list);
		}

		slurm_free_will_run_response_msg(will_run_resp);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

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
	slurm_msg_t req_msg, resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_STEP_CREATE;
	req_msg.data     = req;

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
 * slurm_allocation_lookup - retrieve info for an existing resource allocation
 * IN jobid - job allocation identifier
 * OUT info - job allocation information
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the "resp" using slurm_free_resource_allocation_response_msg
 */
int
slurm_allocation_lookup(uint32_t jobid,
			job_alloc_info_response_msg_t **info)
{
	job_alloc_info_msg_t req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	req.job_id = jobid;
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_ALLOCATION_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch(resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*info = NULL;
		break;
	case RESPONSE_JOB_ALLOCATION_INFO:
		*info = (job_alloc_info_response_msg_t *)resp_msg.data;
		return SLURM_PROTOCOL_SUCCESS;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_allocation_lookup_lite - retrieve info for an existing resource
 *                                allocation without the addrs and such
 * IN jobid - job allocation identifier
 * OUT info - job allocation information
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the "resp" using slurm_free_resource_allocation_response_msg
 */
int
slurm_allocation_lookup_lite(uint32_t jobid,
			     resource_allocation_response_msg_t **info)
{
	job_alloc_info_msg_t req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	req.job_id = jobid;
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_ALLOCATION_INFO_LITE;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch(resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*info = NULL;
		break;
	case RESPONSE_JOB_ALLOCATION_INFO_LITE:
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
 * slurm_sbcast_lookup - retrieve info for an existing resource allocation
 *	including a credential needed for sbcast
 * IN jobid - job allocation identifier
 * OUT info - job allocation information including a credential for sbcast
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 * NOTE: free the "resp" using slurm_free_sbcast_cred_msg
 */
int slurm_sbcast_lookup(uint32_t jobid, job_sbcast_cred_msg_t **info)
{
	job_alloc_info_msg_t req;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	req.job_id = jobid;
	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);
	req_msg.msg_type = REQUEST_JOB_SBCAST_CRED;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch(resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_ERROR;
		*info = NULL;
		break;
	case RESPONSE_JOB_SBCAST_CRED:
		*info = (job_sbcast_cred_msg_t *)resp_msg.data;
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
 * than "n" hostnames in the file, or if an error occurs.  If "n" ==
 * NO_VAL then the entire file is read in
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

	if ((fp = fopen(filename, "r")) == NULL) {
		error("slurm_allocate_resources error opening file %s, %m",
		      filename);
		return NULL;
	}

	hostlist = hostlist_create(NULL);
	if (hostlist == NULL) {
		fclose(fp);
		return NULL;
	}

	while (fgets(in_line, BUFFER_SIZE, fp) != NULL) {
		line_num++;
		line_size = strlen(in_line);
		if (line_size == (BUFFER_SIZE - 1)) {
			error ("Line %d, of hostfile %s too long",
			       line_num, filename);
			fclose (fp);
			hostlist_destroy(hostlist);
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
		if (n != (int)NO_VAL && hostlist_count(hostlist) == n)
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
 ***************************************************************************/
static listen_t *_create_allocation_response_socket(char *interface_hostname)
{
	listen_t *listen = NULL;

	listen = xmalloc(sizeof(listen_t));

	/* port "0" lets the operating system pick any port */
	if ((listen->fd = slurm_init_msg_engine_port(0)) < 0) {
		error("slurm_init_msg_engine_port error %m");
		return NULL;
	}
	if (slurm_get_stream_addr(listen->fd, &listen->address) < 0) {
		error("slurm_get_stream_addr error %m");
		slurm_shutdown_msg_engine(listen->fd);
		return NULL;
	}
	listen->hostname = xstrdup(interface_hostname);
	/* FIXME - screw it!  I can't seem to get the port number through
	   slurm_* functions */
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
static int
_handle_msg(slurm_msg_t *msg, resource_allocation_response_msg_t **resp)
{
	uid_t req_uid   = g_slurm_auth_get_uid(msg->auth_cred, NULL);
	uid_t uid       = getuid();
	uid_t slurm_uid = (uid_t) slurm_get_slurm_user_id();
	int rc = 0;

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u",
			(unsigned int) req_uid);
		return 0;
	}

	switch (msg->msg_type) {
		case RESPONSE_RESOURCE_ALLOCATION:
			debug2("resource allocation response received");
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			*resp = msg->data;
			rc = 1;
			break;
		case SRUN_JOB_COMPLETE:
			info("Job has been cancelled");
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
_accept_msg_connection(int listen_fd,
		       resource_allocation_response_msg_t **resp)
{
	int	     conn_fd;
	slurm_msg_t  *msg = NULL;
	slurm_addr   cli_addr;
	char         host[256];
	uint16_t     port;
	int          rc = 0;

	conn_fd = slurm_accept_msg_conn(listen_fd, &cli_addr);
	if (conn_fd < 0) {
		error("Unable to accept connection: %m");
		return rc;
	}

	slurm_get_addr(&cli_addr, &port, host, sizeof(host));
	debug2("got message connection from %s:%hu", host, port);

	msg = xmalloc(sizeof(slurm_msg_t));
	slurm_msg_t_init(msg);

	if((rc = slurm_receive_msg(conn_fd, msg, 0)) != 0) {
		slurm_free_msg(msg);

		if (errno == EINTR) {
			slurm_close_accepted_conn(conn_fd);
			*resp = NULL;
			return 0;
		}

		error("_accept_msg_connection[%s]: %m", host);
		slurm_close_accepted_conn(conn_fd);
		return SLURM_ERROR;
	}

	rc = _handle_msg(msg, resp); /* handle_msg frees msg */
	slurm_free_msg(msg);

	slurm_close_accepted_conn(conn_fd);
	return rc;
}

/* Wait up to sleep_time for RPC from slurmctld indicating resource allocation
 * has occured.
 * IN sleep_time: delay in seconds (0 means unbounded wait)
 * OUT resp: resource allocation response message
 * RET 1 if resp is filled in, 0 otherwise */
static int
_wait_for_alloc_rpc(const listen_t *listen, int sleep_time,
		    resource_allocation_response_msg_t **resp)
{
	struct pollfd fds[1];
	int rc;
	int timeout_ms;

	fds[0].fd = listen->fd;
	fds[0].events = POLLIN;

	if (sleep_time != 0) {
		timeout_ms = sleep_time * 1000;
	} else {
		timeout_ms = -1;
	}
	while ((rc = poll(fds, 1, timeout_ms)) < 0) {
		switch (errno) {
			case EAGAIN:
			case EINTR:
				*resp = NULL;
				return -1;
			case EBADF:
			case ENOMEM:
			case EINVAL:
			case EFAULT:
				error("poll: %m");
				*resp = NULL;
				return -1;
			default:
				error("poll: %m. Continuing...");
		}
	}

	if (rc == 0) { /* poll timed out */
		errno = ETIMEDOUT;
	} else if (fds[0].revents & POLLIN) {
		return (_accept_msg_connection(listen->fd, resp));
	}

	return 0;
}

static resource_allocation_response_msg_t *
_wait_for_allocation_response(uint32_t job_id, const listen_t *listen,
			      int timeout)
{
	resource_allocation_response_msg_t *resp = NULL;
	int errnum;

	info("job %u queued and waiting for resources", job_id);
	if (_wait_for_alloc_rpc(listen, timeout, &resp) <= 0) {
		errnum = errno;
		/* Maybe the resource allocation response RPC got lost
		 * in the mail; surely it should have arrived by now.
		 * Let's see if the controller thinks that the allocation
		 * has been granted.
		 */
		if (slurm_allocation_lookup_lite(job_id, &resp) >= 0) {
			return resp;
		}
		if (slurm_get_errno() == ESLURM_JOB_PENDING) {
			debug3("Still waiting for allocation");
			errno = errnum;
			return NULL;
		} else {
			debug3("Unable to confirm allocation for job %u: %m",
			       job_id);
			return NULL;
		}
	}
	info("job %u has been allocated resources", job_id);
	return resp;
}
