/****************************************************************************\
 *  slurm_get_statistics.c - functions for sdiag command
 *****************************************************************************
 *  Produced at Barcelona Supercomputing Center, December 2011
 *  Written by Alejandro Lucero <alucero@bsc.es>
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

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"


extern int slurm_reset_statistics(stats_info_request_msg_t *req)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req_msg.msg_type = REQUEST_STATS_INFO;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);

	if (rc == SLURM_SOCKET_ERROR)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
		case RESPONSE_STATS_INFO:
			break;
		case RESPONSE_SLURM_RC:
			rc = ((return_code_msg_t *) resp_msg.data)->return_code;
			if (rc)
				slurm_seterrno_ret(rc);
			break;
		default:
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return SLURM_PROTOCOL_SUCCESS;

}

extern int slurm_get_statistics(stats_info_response_msg_t **buf,
				stats_info_request_msg_t *req)
{
	int rc;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	slurm_msg_t_init(&req_msg);
	slurm_msg_t_init(&resp_msg);

	req_msg.msg_type = REQUEST_STATS_INFO;
	req_msg.data     = req;

	rc = slurm_send_recv_controller_msg(&req_msg, &resp_msg,
					    working_cluster_rec);

	if (rc == SLURM_SOCKET_ERROR)
		return SLURM_ERROR;

	switch (resp_msg.msg_type) {
		case RESPONSE_STATS_INFO:
			*buf = (stats_info_response_msg_t *)resp_msg.data;
			break;
		case RESPONSE_SLURM_RC:
			rc = ((return_code_msg_t *) resp_msg.data)->return_code;
			if (rc)
				slurm_seterrno_ret(rc);
			buf = NULL;
			break;
		default:
			slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
	}

	return SLURM_PROTOCOL_SUCCESS;
}
