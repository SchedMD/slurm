/*****************************************************************************\
 *  allocate.c - allocate nodes for a job or step with supplied contraints
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by moe jette <jette1@llnl.gov>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>

#include "src/common/slurm_protocol_api.h"
#include "src/slurm/slurm.h"

/*
 * slurm_allocate_resources - allocate resources for a job request
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the allocated using slurm_free_resource_allocation_response_msg
 */
int
slurm_allocate_resources (job_desc_msg_t * job_desc_msg , 
			resource_allocation_response_msg_t ** slurm_alloc_msg )
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;

	/* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	request_msg . msg_type = REQUEST_RESOURCE_ALLOCATION ;
	request_msg . data = job_desc_msg ; 
	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SHUTDOWN_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = 
				( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			*slurm_alloc_msg = NULL;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION:
			/* Calling method is responsible to free this memory */
			*slurm_alloc_msg = 
				( resource_allocation_response_msg_t * ) 
				response_msg . data ;
			return SLURM_PROTOCOL_SUCCESS;
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * slurm_job_will_run - determine if a job would execute immediately if 
 *	submitted now
 * IN job_desc_msg - description of resource allocation request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the allocated using slurm_free_resource_allocation_response_msg
 */
int slurm_job_will_run (job_desc_msg_t * job_desc_msg , 
			resource_allocation_response_msg_t ** slurm_alloc_msg)
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;

	/* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	/* job_desc_msg.immediate = true;	implicit */
	request_msg . msg_type = REQUEST_JOB_WILL_RUN ;
	request_msg . data = job_desc_msg ; 
	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SHUTDOWN_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = 
				( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			*slurm_alloc_msg = NULL;
			break ;
		case RESPONSE_JOB_WILL_RUN:
			*slurm_alloc_msg = 
				( resource_allocation_response_msg_t * ) 
				response_msg . data ;
			return SLURM_PROTOCOL_SUCCESS;
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

	return SLURM_PROTOCOL_SUCCESS ;
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
slurm_allocate_resources_and_run (job_desc_msg_t * job_desc_msg , 
		resource_allocation_and_run_response_msg_t ** slurm_alloc_msg )
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;

	/* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	request_msg . msg_type = REQUEST_ALLOCATION_AND_RUN_JOB_STEP ;
	request_msg . data = job_desc_msg ; 
	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SHUTDOWN_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = 
				( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			*slurm_alloc_msg = NULL;
			break ;
		case RESPONSE_ALLOCATION_AND_RUN_JOB_STEP:
			/* Calling method is responsible to free this memory */
			*slurm_alloc_msg = 
				( resource_allocation_and_run_response_msg_t * ) 
				response_msg . data ;
			return SLURM_PROTOCOL_SUCCESS;
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * slurm_job_step_create - create a job step for a given job id
 * IN slurm_step_alloc_req_msg - description of job step request
 * OUT slurm_step_alloc_resp_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the response using slurm_free_job_step_create_response_msg
 */
int
slurm_job_step_create (
		job_step_create_request_msg_t * slurm_step_alloc_req_msg, 
		job_step_create_response_msg_t ** slurm_step_alloc_resp_msg )
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;

	/* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	request_msg . msg_type = REQUEST_JOB_STEP_CREATE ;
	request_msg . data = slurm_step_alloc_req_msg ; 
	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SHUTDOWN_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = 
				( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			*slurm_step_alloc_resp_msg = NULL;
			}
			break ;
		case RESPONSE_JOB_STEP_CREATE:
			/* Calling method is responsible to free this memory */
			*slurm_step_alloc_resp_msg = 
				( job_step_create_response_msg_t * ) 
				response_msg . data ;
			return SLURM_PROTOCOL_SUCCESS;
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * slurm_confirm_allocation - confirm an existing resource allocation
 * IN job_desc_msg - description of existing job request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 * NOTE: free the response using slurm_free_resource_allocation_response_msg
 */
int 
slurm_confirm_allocation (old_job_alloc_msg_t * job_desc_msg , 
			resource_allocation_response_msg_t ** slurm_alloc_msg ) 
{
	int msg_size ;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * slurm_rc_msg ;

	/* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* send request message */
	request_msg . msg_type = REQUEST_OLD_JOB_RESOURCE_ALLOCATION ;
	request_msg . data = job_desc_msg ; 
	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SEND_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_RECEIVE_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_SHUTDOWN_ERROR );
		return SLURM_SOCKET_ERROR ;
	}
	if ( msg_size )
		return msg_size;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			slurm_rc_msg = 
				( return_code_msg_t * ) response_msg . data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			*slurm_alloc_msg = NULL;
			break ;
		case RESPONSE_RESOURCE_ALLOCATION:
			/* Calling methos is responsible to free this memory */
			*slurm_alloc_msg = 
				( resource_allocation_response_msg_t * ) 
				response_msg . data ;
			return SLURM_PROTOCOL_SUCCESS;
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}
