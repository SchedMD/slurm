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

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
#endif

#include <slurm/slurm.h>
#include <stdlib.h>

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/forward.h"

#define BUFFER_SIZE 1024

static int _handle_rc_msg(slurm_msg_t *msg);

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
		if(n != (int)NO_VAL && hostlist_count(hostlist) == n) 
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
