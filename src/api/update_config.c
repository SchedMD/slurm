/****************************************************************************\
 *  update_config.c - request that slurmctld update its configuration
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov.
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
#include <stdlib.h>

#include "src/common/slurm_protocol_api.h"
#include "src/slurm/slurm.h"

static int _slurm_update (void * data, slurm_msg_type_t msg_type);

/*
 * slurm_update_job - issue RPC to a job's configuration per request, 
 *	only usable by user root or (for some parameters) the job's owner
 * IN job_msg - description of job updates
 * RET 0 on success or slurm error code
 */
int 
slurm_update_job ( job_desc_msg_t * job_msg ) 
{
	return _slurm_update ((void *) job_msg, REQUEST_UPDATE_JOB);
}

/*
 * slurm_update_node - issue RPC to a node's configuration per request, 
 *	only usable by user root
 * IN node_msg - description of node updates
 * RET 0 on success or slurm error code
 */
int 
slurm_update_node ( update_node_msg_t * node_msg ) 
{
	return _slurm_update ((void *) node_msg, REQUEST_UPDATE_NODE);
}

/*
 * slurm_update_partition - issue RPC to a partition's configuration per  
 *	request, only usable by user root
 * IN part_msg - description of partition updates
 * RET 0 on success or slurm error code
 */
int 
slurm_update_partition ( update_part_msg_t * part_msg ) 
{
	return _slurm_update ((void *) part_msg, REQUEST_UPDATE_PARTITION);
}


/* _slurm_update - issue RPC for all update requests */
static int 
_slurm_update (void * data, slurm_msg_type_t msg_type)
{
	int msg_size;
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
	request_msg . msg_type = msg_type ;
	request_msg . data = data ; 
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
				( return_code_msg_t *) response_msg.data ;
			rc = slurm_rc_msg->return_code;
			slurm_free_return_code_msg ( slurm_rc_msg );	
			if (rc) {
				slurm_seterrno ( rc );
				return SLURM_PROTOCOL_ERROR;
			}
			break ;
		default:
			slurm_seterrno ( SLURM_UNEXPECTED_MSG_ERROR );
			return SLURM_PROTOCOL_ERROR;
			break ;
	}

        return SLURM_PROTOCOL_SUCCESS ;
}
