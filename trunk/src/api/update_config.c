/****************************************************************************\
 * update_config.c - request that slurmctld update its configuration
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
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

int slurm_update (void * data, slurm_msg_type_t msg_type);

/* slurm_update_job - update a job's configuration per request */
int 
slurm_update_job ( job_desc_msg_t * job_msg ) 
{
	return slurm_update ((void *) job_msg, REQUEST_UPDATE_JOB);
}

/* slurm_update_node - update slurmctld node configuration per request */
int 
slurm_update_node ( update_node_msg_t * node_msg ) 
{
	return slurm_update ((void *) node_msg, REQUEST_UPDATE_NODE);
}

/* slurm_update_partition - update slurmctld partition configuration per request */
int 
slurm_update_partition ( update_part_msg_t * part_msg ) 
{
	return slurm_update ((void *) part_msg, REQUEST_UPDATE_PARTITION);
}


/* slurm_update - perform RPC for all update requests */
int 
slurm_update (void * data, slurm_msg_type_t msg_type)
{
	int msg_size;
	int rc ;
	slurm_fd sockfd ;
	slurm_msg_t request_msg ;
	slurm_msg_t response_msg ;
	return_code_msg_t * rc_msg ;

	/* init message connection for message communication with controller */

	if ( ( sockfd = slurm_open_controller_conn ( ) ) == SLURM_SOCKET_ERROR )
		return SLURM_SOCKET_ERROR ;

	/* send request message */
	request_msg . msg_type = msg_type ;
	request_msg . data = data ; 

	if ( ( rc = slurm_send_controller_msg ( sockfd , & request_msg ) ) == SLURM_SOCKET_ERROR )
		return SLURM_SOCKET_ERROR ;

	/* receive message */
	if ( ( msg_size = slurm_receive_msg ( sockfd , & response_msg ) ) == SLURM_SOCKET_ERROR )
		return SLURM_SOCKET_ERROR ;
	/* shutdown message connection */
	if ( ( rc = slurm_shutdown_msg_conn ( sockfd ) ) == SLURM_SOCKET_ERROR )
		return SLURM_SOCKET_ERROR ;

	switch ( response_msg . msg_type )
	{
		case RESPONSE_SLURM_RC:
			rc_msg = ( return_code_msg_t * ) response_msg . data ;
			return (int) rc_msg->return_code ;
			break ;
		default:
			return SLURM_UNEXPECTED_MSG_ERROR ;
			break ;
	}

	return SLURM_SUCCESS ;
}
