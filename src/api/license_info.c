/*****************************************************************************\
 *  job_info.c - get/print the job state information of slurm
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Copyright (C) 2013 SchedMD
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bigagli <david@schedmd.com>
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

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/forward.h"
#include "src/common/parse_time.h"
#include "src/common/select.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

/* slurm_load_licenses()
 *
 * Load requested licenses from the controller.
 *
 */
extern int
slurm_load_licenses(time_t t,
                    license_info_msg_t **lic_info,
                    uint16_t show_flags)
{
	int cc;
	slurm_msg_t msg_request;
	slurm_msg_t msg_reply;
	struct license_info_request_msg req;

	memset(&req, 0, sizeof(struct license_info_request_msg));
	slurm_msg_t_init(&msg_request);
	slurm_msg_t_init(&msg_reply);

	msg_request.msg_type = REQUEST_LICENSE_INFO;
	req.last_update = t;
	req.show_flags = show_flags;
	msg_request.data = &req;

	cc = slurm_send_recv_controller_msg(&msg_request, &msg_reply,
					    working_cluster_rec);
	if (cc < 0)
		return SLURM_ERROR;

	switch (msg_reply.msg_type) {
		case RESPONSE_LICENSE_INFO:
			*lic_info = msg_reply.data;
			break;
		case RESPONSE_SLURM_RC:
			cc = ((return_code_msg_t *)msg_reply.data)->return_code;
			slurm_free_return_code_msg(msg_reply.data);
			if (cc) /* slurm_seterrno_ret() is a macro ... sigh */
				slurm_seterrno(cc);
			*lic_info = NULL;
			return -1;
		default:
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}
