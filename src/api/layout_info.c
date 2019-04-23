/*****************************************************************************\
 *  layout_info.c - get/print the layout information of slurm
 *****************************************************************************
 *  Copyright (C) 2015
 *  Written by Bull - Thomas Cadeau
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

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
 * slurm_load_layout - issue RPC to get slurm specific layout information
 *	if changed since update_time
 * OUT resp - place to store a node configuration pointer
 * IN l_type - type of layout
 * RET 0 or a slurm error code
 * NOTE: free the response using slurm_free_layout_info_msg
 */
/*extern int slurm_load_layout (node_info_msg_t **resp, char* l_type)
{
	return SLURM_SUCCESS;
}*/

extern int slurm_load_layout (char *layout_type, char *entities, char *type,
			      uint32_t flags, layout_info_msg_t **resp)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;
	layout_info_request_msg_t req;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	memset(&req, 0, sizeof(req));
	req.layout_type  = layout_type;
	req.entities     = entities;
	req.type         = type;
	req.flags        = flags;
	req_msg.msg_type = REQUEST_LAYOUT_INFO;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
	case RESPONSE_LAYOUT_INFO:
		*resp = (layout_info_msg_t *) resp_msg.data;
		break;
	case RESPONSE_SLURM_RC:
		rc = ((return_code_msg_t *) resp_msg.data)->return_code;
		slurm_free_return_code_msg(resp_msg.data);
		if (rc)
			slurm_seterrno_ret(rc);
		*resp = NULL;
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

void slurm_print_layout_info ( FILE* out, layout_info_msg_t *layout_info_ptr,
			       int one_liner )
{
	char *nl;
	int i;
	for (i = 0; i < layout_info_ptr->record_count; i++) {
		if (one_liner) {
			while ((nl = strchr(layout_info_ptr->records[i], '\n')))
				nl[0] = ' ';
		}
		fprintf ( out, "%s", layout_info_ptr->records[i]);
	}
}
