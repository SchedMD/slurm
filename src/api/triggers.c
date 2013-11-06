/*****************************************************************************\
 *  trigger.c - Event trigger management functions
 *****************************************************************************
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "slurm/slurm.h"

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

/*
 * slurm_set_trigger - Set an event trigger
 * RET 0 or a slurm error code
 */
extern int slurm_set_trigger (trigger_info_t *trigger_set)
{
	int rc;
	slurm_msg_t msg;
	trigger_info_msg_t req;

	slurm_msg_t_init(&msg);
	/*
	 * Request message:
	 */
	req.record_count  = 1;
	req.trigger_array = trigger_set;
	msg.msg_type      = REQUEST_TRIGGER_SET;
        msg.data          = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc) < 0)
		return SLURM_FAILURE;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}

/*
 * slurm_clear_trigger - Clear (remove) an existing event trigger
 * RET 0 or a slurm error code
 */
extern int slurm_clear_trigger (trigger_info_t *trigger_clear)
{
	int rc;
	slurm_msg_t msg;
	trigger_info_msg_t req;

	slurm_msg_t_init(&msg);
	/*
	 * Request message:
	 */
	req.record_count  = 1;
	req.trigger_array = trigger_clear;
	msg.msg_type      = REQUEST_TRIGGER_CLEAR;
        msg.data          = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc) < 0)
		return SLURM_FAILURE;

	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}

/*
 * slurm_get_triggers - Get all event trigger information
 * Use slurm_free_trigger() to free the memory allocated by this function
 * RET 0 or a slurm error code
 */
extern int slurm_get_triggers (trigger_info_msg_t ** trigger_get)
{
	int rc;
	slurm_msg_t resp_msg;
	slurm_msg_t req_msg;
	trigger_info_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req.record_count  = 0;
	req.trigger_array = NULL;
	req_msg.msg_type  = REQUEST_TRIGGER_GET;
	req_msg.data      = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_TRIGGER_GET:
		*trigger_get = (trigger_info_msg_t *)resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS ;
}

/*
 * slurm_pull_trigger - Pull (fire) an event trigger
 * RET 0 or a slurm error code
 */
extern int slurm_pull_trigger (trigger_info_t *trigger_pull)
{
	int rc;
	slurm_msg_t msg;
	trigger_info_msg_t req;

	/*
	 * Request message:
	 */
	slurm_msg_t_init(&msg);
	memset(&req, 0, sizeof(trigger_info_msg_t));
	req.record_count  = 1;
	req.trigger_array = trigger_pull;
	msg.msg_type      = REQUEST_TRIGGER_PULL;
	msg.data	  = &req;

	if (slurm_send_recv_controller_rc_msg(&msg, &rc) < 0)
		return SLURM_FAILURE;
	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}
