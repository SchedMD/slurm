/*****************************************************************************\
 *  submit.c - submit a job with supplied contraints
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
#include <unistd.h>
#include <sys/types.h>

#ifndef __USE_XOPEN_EXTENDED
extern pid_t getsid(pid_t pid);		/* missing from <unistd.h> */
#endif

#include <slurm/slurm.h>

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"

/*
 * slurm_submit_batch_job - issue RPC to submit a job for later execution
 * NOTE: free the response using slurm_free_submit_response_response_msg
 * IN job_desc_msg - description of batch job request
 * OUT slurm_alloc_msg - response to request
 * RET 0 on success or slurm error code
 */
int
slurm_submit_batch_job (job_desc_msg_t * job_desc_msg, 
		submit_response_msg_t ** slurm_alloc_msg )
{
        int msg_size ;
        int rc ;
        slurm_fd sockfd ;
        slurm_msg_t request_msg ;
        slurm_msg_t response_msg ;
        return_code_msg_t * slurm_rc_msg ;
	bool set_alloc_node = false;
	char this_node_name[64];

	/* init message connection for message communication with controller */
	if ( ( sockfd = slurm_open_controller_conn ( ) ) 
			== SLURM_SOCKET_ERROR ) {
		slurm_seterrno ( SLURM_COMMUNICATIONS_CONNECTION_ERROR );
		return SLURM_SOCKET_ERROR ;
	}

	/* set alloc node/sid info as needed for request */
	if (job_desc_msg->alloc_sid == NO_VAL)
		job_desc_msg->alloc_sid = getsid(0);
	if ((job_desc_msg->alloc_node == NULL) &&
	    (getnodename(this_node_name, sizeof(this_node_name)) == 0)) {
		job_desc_msg->alloc_node = this_node_name;
		set_alloc_node = true;
	}

        /* send request message */
	/* job_desc_msg.immediate = false;	implicit */
        request_msg . msg_type = REQUEST_SUBMIT_BATCH_JOB ;
        request_msg . data = job_desc_msg ; 
	rc = slurm_send_controller_msg ( sockfd , & request_msg );
	if (set_alloc_node)	/* reset (clear) alloc_node */
		job_desc_msg->alloc_node = NULL;
	if ( rc == SLURM_SOCKET_ERROR ) {
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
			break ;
		case RESPONSE_SUBMIT_BATCH_JOB:
                        *slurm_alloc_msg = 
				( submit_response_msg_t * ) 
				response_msg . data ;
			return SLURM_PROTOCOL_SUCCESS;
			break;
                default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
        }

        return SLURM_PROTOCOL_SUCCESS ;
}
