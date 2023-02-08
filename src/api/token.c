/*****************************************************************************\
 *  auth.c - Request authentication tokens from slurmctld
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

/*
 * slurm_fetch_token
 * IN lifespan - 0 for default, or specific value in seconds
 * RET xmalloc'd string or NULL on error
 */
extern char *slurm_fetch_token(char *username, int lifespan)
{
	slurm_msg_t req, resp;
	token_request_msg_t token_req;
	token_response_msg_t *token_resp;
	char *token = NULL;

	slurm_msg_t_init(&req);
	slurm_msg_t_init(&resp);
	/*
	 * Request message:
	 */
	memset(&token_req, 0, sizeof(token_req));
	token_req.lifespan = lifespan;
	token_req.username = username;
	req.msg_type = REQUEST_AUTH_TOKEN;
	req.data = &token_req;

	if (slurm_send_recv_controller_msg(&req, &resp, working_cluster_rec)) {
		error("%s: error receiving response: %m", __func__);
		return NULL;
	}

	switch (resp.msg_type) {
	case RESPONSE_SLURM_RC:
	{
		int rc = ((return_code_msg_t *) resp.data)->return_code;
		if (rc)
			slurm_seterrno(rc);
		error("%s: error with request: %m", __func__);
                break;
	}
	case RESPONSE_AUTH_TOKEN:
		token_resp = (token_response_msg_t *) resp.data;
		token = token_resp->token;
		token_resp->token = NULL;
		slurm_free_token_response_msg(token_resp);
		if (!token)
			error("%s: no token returned", __func__);
		break;
	}

	return token;
}
