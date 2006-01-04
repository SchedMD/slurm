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

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
#endif

#include <slurm/slurm.h>
#include <stdlib.h>

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"

#define BUF_SIZE 1024

static int _handle_rc_msg(slurm_msg_t *msg);
static int _nodelist_from_hostfile(job_step_create_request_msg_t *req);

/*
 * slurm_allocate_resources - allocate resources for a job request
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
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

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);

	/*
	 *  Clear this hostname if set internally to this function
	 *    (memory is on the stack)
	 */
	if (host_set)
		req->alloc_node = NULL;

	if (rc == SLURM_SOCKET_ERROR) 
		return SLURM_SOCKET_ERROR;

	slurm_free_cred(resp_msg.cred);
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
 * RET 0 on success or slurm error code
 */
int slurm_job_will_run (job_desc_msg_t *req)
{
	slurm_msg_t req_msg;
	int rc;

	/* req.immediate = true;    implicit */

	req_msg.msg_type = REQUEST_JOB_WILL_RUN;
	req_msg.data     = req; 

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
		return SLURM_SOCKET_ERROR;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_allocate_resources_and_run - allocate resources for a job request and 
 *	initiate a job step
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the response using 
 *	slurm_free_resource_allocation_and_run_response_msg
 */
int
slurm_allocate_resources_and_run (job_desc_msg_t *req, 
      resource_allocation_and_run_response_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	bool host_set = false;
	char host[64];

	if (req->alloc_sid == NO_VAL)
		req->alloc_sid = getsid(0);

	if ( (req->alloc_node == NULL) 
	    && (getnodename(host, sizeof(host)) == 0) ) {
		req->alloc_node = host;
		host_set = true;
	}

	req_msg.msg_type = REQUEST_ALLOCATION_AND_RUN_JOB_STEP;
	req_msg.data     = req; 

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);

	if (host_set)	/* reset (clear) alloc_node */
		req->alloc_node = NULL;

	if (rc == SLURM_SOCKET_ERROR) 
		return SLURM_SOCKET_ERROR;

	slurm_free_cred(resp_msg.cred);
	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		if (_handle_rc_msg(&resp_msg) < 0)
			return SLURM_PROTOCOL_ERROR;
		*resp = NULL;
		break ;
	case RESPONSE_ALLOCATION_AND_RUN_JOB_STEP:
		*resp = (resource_allocation_and_run_response_msg_t *) 
			resp_msg.data;
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
 * RET 0 on success or slurm error code
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
	
	if(_nodelist_from_hostfile(req) == 0) 
		debug("nodelist was NULL");  

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	slurm_free_cred(resp_msg.cred);
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
 * RET 0 on success or slurm error code
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

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	slurm_free_cred(resp_msg.cred);
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
 * RET 0 on success or slurm error code
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

	slurm_free_cred(resp_msg.cred);
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

static int _nodelist_from_hostfile(job_step_create_request_msg_t *req)
{
	char *hostfile = NULL;
	char *hostname = NULL;
	FILE *hostfilep = NULL;
	char in_line[BUF_SIZE];	/* input line */
	int i, j;
	int line_size;
	hostlist_t hostlist = NULL;
	int count;
	int len = 0;
	int ret = 0;
	int line_num = 0;
	char *nodelist = NULL;
	
	if (hostfile = getenv("MP_HOSTFILE")) {
		if(strlen(hostfile)<1 || !strcmp(hostfile,"NULL")) 
			goto no_hostfile;
		if((hostfilep = fopen(hostfile, "r")) == NULL) {
			error("slurm_allocate_resources "
			      "error opening file %s, %m", 
			      hostfile);
			goto no_hostfile;
		}
		hostlist = hostlist_create(NULL);
		
		while (fgets (in_line, BUF_SIZE, hostfilep) != NULL) {
			line_num++;
			line_size = strlen(in_line);
			if (line_size >= (BUF_SIZE - 1)) {
				error ("Line %d, of hostfile %s too long",
				       line_num, hostfile);
				fclose (hostfilep);
				goto no_hostfile;
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
			
			len += strlen(in_line)+1;
			hostlist_push(hostlist,in_line);	
			if(req->num_tasks && (line_num+1)>req->num_tasks) 
  				break; 
		}
		fclose (hostfilep);
		
		nodelist = (char *)xmalloc(sizeof(char)*len);
		memset(nodelist, 0, len);

		count = hostlist_count(hostlist);
		if (count <= 0) {
			error("Hostlist is empty!\n");
			xfree(*nodelist);
			goto cleanup_hostfile;
		}
		
		len = 0;
		while (hostname = hostlist_shift(hostlist)) {
			line_num = strlen(hostname)+1;
			ret = sprintf(nodelist+len, 
				       "%s,", hostname);
			if (ret < 0 || ret > line_num) {
				error("bad snprintf only %d printed",ret);
				xfree(*nodelist);
				goto cleanup_hostfile;
			}
			len += ret;
		}
		nodelist[--len] = '\0';
		debug2("Hostlist from MP_HOSTFILE = %s\n",
		     nodelist);
					
	cleanup_hostfile:
		hostlist_destroy(hostlist);
		
	}
no_hostfile:
	if(nodelist) {
		if(req->node_list)
			xfree(req->node_list);
		req->node_list = nodelist;
		req->num_tasks = count;
		req->task_dist = SLURM_DIST_HOSTFILE;
	}
	return count;
}
