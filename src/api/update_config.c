/****************************************************************************\
 *  update_config.c - request that slurmctld update its configuration
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> and Kevin Tew <tew1@llnl.gov>.
 *  LLNL-CODE-402394.
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
#include <stdlib.h>
#include <string.h>

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"

static int _slurm_update (void * data, slurm_msg_type_t msg_type);

/*
 * slurm_update_job - issue RPC to a job's configuration per request, 
 *	only usable by user root or (for some parameters) the job's owner
 * IN job_msg - description of job updates
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
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
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_update_node ( update_node_msg_t * node_msg ) 
{
	return _slurm_update ((void *) node_msg, REQUEST_UPDATE_NODE);
}

/*
 * slurm_create_partition - create a new partition, only usable by user root
 * IN part_msg - description of partition configuration
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_create_partition ( update_part_msg_t * part_msg ) 
{
	return _slurm_update ((void *) part_msg, REQUEST_CREATE_PARTITION);
}

/*
 * slurm_update_partition - issue RPC to a partition's configuration per  
 *	request, only usable by user root
 * IN part_msg - description of partition updates
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_update_partition ( update_part_msg_t * part_msg ) 
{
	return _slurm_update ((void *) part_msg, REQUEST_UPDATE_PARTITION);
}

/*
 * slurm_delete_partition - issue RPC to delete a partition, only usable 
 *	by user root
 * IN part_msg - description of partition to delete
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_delete_partition ( delete_part_msg_t * part_msg ) 
{
	return _slurm_update ((void *) part_msg, REQUEST_DELETE_PARTITION);
}

/*
 * slurm_create_reservation - create a new reservation, only usable by user root
 * IN resv_msg - description of reservation
 * RET name of reservation on success (caller must free the memory),
 *	otherwise return NULL and set errno to indicate the error
 */
char * 
slurm_create_reservation (reserve_request_msg_t * resv_msg ) 
{
	int rc;
	char *resv_name = NULL;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	reservation_name_msg_t *resp;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req_msg.msg_type = REQUEST_CREATE_RESERVATION;
	req_msg.data     = resv_msg; 
			
	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg);
	switch (resp_msg.msg_type) {
	case RESPONSE_CREATE_RESERVATION:
		resp = (reservation_name_msg_t *) resp_msg.data;
		resv_name = strdup(resp->name);
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		if (rc) 
			slurm_seterrno(rc);
		break;
	default:
		slurm_seterrno(SLURM_UNEXPECTED_MSG_ERROR);
	}
	slurm_free_msg_data(resp_msg.msg_type, resp_msg.data);
	return resv_name;
}

/*
 * slurm_update_reservation - modify an existing reservation, only usable by 
 *	user root
 * IN resv_msg - description of reservation
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
extern int slurm_update_reservation ( reserve_request_msg_t * resv_msg )
{
	return _slurm_update ((void *) resv_msg, REQUEST_UPDATE_RESERVATION);
}

/*
 * slurm_delete_reservation - issue RPC to delete a reservation, only usable 
 *	by user root
 * IN resv_msg - description of reservation to delete
 * RET 0 on success, otherwise return -1 and set errno to indicate the error
 */
int 
slurm_delete_reservation ( reservation_name_msg_t * resv_msg ) 
{
	return _slurm_update ((void *) resv_msg, REQUEST_DELETE_RESERVATION);
}


/* _slurm_update - issue RPC for all update requests */
static int 
_slurm_update (void *data, slurm_msg_type_t msg_type)
{
	int rc;
	slurm_msg_t req_msg;

	slurm_msg_t_init(&req_msg);
	req_msg.msg_type = msg_type;
	req_msg.data     = data; 

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0)
		return SLURM_ERROR;

	if (rc != SLURM_SUCCESS)
		slurm_seterrno_ret(rc);

        return SLURM_PROTOCOL_SUCCESS;
}
