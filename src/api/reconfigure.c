/*****************************************************************************\
 *  reconfigure.c - request that slurmctld shutdown or re-read the 
 *	            configuration files
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov> et. al.
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
#include <unistd.h>

#include <slurm/slurm.h>

#include "src/common/slurm_protocol_api.h"

static int _send_message_controller (	enum controller_id dest, 
					slurm_msg_t *request_msg );

/*
 * slurm_reconfigure - issue RPC to have Slurm controller (slurmctld)
 *	reload its configuration file 
 * RET 0 or a slurm error code
 */
int
slurm_reconfigure ( void )
{
	int rc;
	slurm_msg_t req;

	req.msg_type = REQUEST_RECONFIGURE;

	if (slurm_send_recv_controller_rc_msg(&req, &rc) < 0)
		return SLURM_ERROR;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_PROTOCOL_SUCCESS;
}

/*
 * slurm_ping - issue RPC to have Slurm controller (slurmctld)
 * IN controller - 1==primary controller, 2==secondary controller
 * RET 0 or a slurm error code
 */
int
slurm_ping (int primary)
{
	int rc ;
	slurm_msg_t request_msg ;

	request_msg . msg_type = REQUEST_PING ;

	if (primary == 1)
		rc = _send_message_controller ( PRIMARY_CONTROLLER,
						&request_msg );
	else if (primary == 2)
		rc = _send_message_controller ( SECONDARY_CONTROLLER,
						&request_msg );
	else
		rc = SLURM_ERROR;

	return rc;
}

/*
 * slurm_shutdown - issue RPC to have Slurm controller (slurmctld)
 *	cease operations, both the primary and backup controller 
 *	are shutdown.
 * IN core - controller generates a core file if set
 * RET 0 or a slurm error code
 */
int
slurm_shutdown (uint16_t core)
{
	slurm_msg_t req_msg;
	shutdown_msg_t shutdown_msg;

	shutdown_msg.core = core;
	req_msg.msg_type  = REQUEST_SHUTDOWN;
	req_msg.data      = &shutdown_msg;

	/* 
	 * Explicity send the message to both primary 
	 *   and backup controllers 
	 */
	(void) _send_message_controller(SECONDARY_CONTROLLER, &req_msg);
	return _send_message_controller(PRIMARY_CONTROLLER,   &req_msg);
}

int
_send_message_controller (enum controller_id dest, slurm_msg_t *req) 
{
	int rc;
	slurm_fd fd = -1;
	slurm_msg_t resp_msg ;

	if ((fd = slurm_open_controller_conn_spec(dest)) < 0)
		slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_CONNECTION_ERROR);

	if (slurm_send_node_msg(fd, req) < 0) 
		slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_SEND_ERROR);

	if ((rc = slurm_receive_msg(fd, &resp_msg, 0)) < 0)
		slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_RECEIVE_ERROR);

	slurm_free_cred(resp_msg.cred);
	if (slurm_shutdown_msg_conn(fd) != SLURM_SUCCESS)
		slurm_seterrno_ret(SLURMCTLD_COMMUNICATIONS_SHUTDOWN_ERROR);

	if (resp_msg.msg_type != RESPONSE_SLURM_RC)
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);

	rc = ((return_code_msg_t *) resp_msg.data)->return_code;

	slurm_free_return_code_msg(resp_msg.data);

	if (rc) slurm_seterrno_ret(rc);

        return SLURM_PROTOCOL_SUCCESS;
}

