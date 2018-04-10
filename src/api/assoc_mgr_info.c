/*****************************************************************************\
 *  assoc_mgr_info.c - get the current slurmctld assoc_mgr information
 *****************************************************************************
 *  Copyright (C) 2014 CSCS
 *  Copyright (C) 2015 SchedMD LLC.
 *  Produced at CSCS
 *  Written by Stephen Trofinoff
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_protocol_api.h"

/* slurm_load_assoc_mgr_info()
 *
 * Load requested controller assoc_mgr state.
 *
 */
extern int
slurm_load_assoc_mgr_info(assoc_mgr_info_request_msg_t *req,
			  assoc_mgr_info_msg_t **resp)
{
	int cc;
	slurm_msg_t msg_request;
	slurm_msg_t msg_reply;

	slurm_msg_t_init(&msg_request);
	slurm_msg_t_init(&msg_reply);

	msg_request.msg_type = REQUEST_ASSOC_MGR_INFO;
	msg_request.data = req;

	cc = slurm_send_recv_controller_msg(&msg_request, &msg_reply,
					    working_cluster_rec);
	if (cc < 0)
		return SLURM_ERROR;

	switch (msg_reply.msg_type) {
		case RESPONSE_ASSOC_MGR_INFO:
			*resp = msg_reply.data;
			break;
		case RESPONSE_SLURM_RC:
			cc = ((return_code_msg_t *)msg_reply.data)->return_code;
			slurm_free_return_code_msg(msg_reply.data);
			if (cc) /* slurm_seterrno_ret() is a macro ... sigh */
				slurm_seterrno(cc);
			return -1;
		default:
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_PROTOCOL_SUCCESS;
}
