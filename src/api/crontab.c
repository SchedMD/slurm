/*****************************************************************************\
 *  crontab.c - get/set slurm crontab
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

#include "slurm/slurm.h"

#include "src/common/cron.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

extern int slurm_request_crontab(uid_t uid, char **crontab,
				 char **disabled_lines)
{
	crontab_request_msg_t req;
	crontab_response_msg_t *resp;
	slurm_msg_t request, response;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&request);
	slurm_msg_t_init(&response);

	req.uid = uid;
	request.msg_type = REQUEST_CRONTAB;
	request.data = &req;

	if (slurm_send_recv_controller_msg(&request, &response,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	if (response.msg_type == RESPONSE_CRONTAB) {
		resp = (crontab_response_msg_t *) response.data;
		*crontab = resp->crontab;
		resp->crontab = NULL;
		*disabled_lines = resp->disabled_lines;
		resp->disabled_lines = NULL;
	} else if (response.msg_type == RESPONSE_SLURM_RC) {
		rc = ((return_code_msg_t *) response.data)->return_code;
	} else {
		rc = SLURM_ERROR;
	}

	slurm_free_msg_data(response.msg_type, response.data);
	return rc;
}

extern crontab_update_response_msg_t *slurm_update_crontab(uid_t uid, gid_t gid,
							   char *crontab,
							   List jobs)
{
	crontab_update_request_msg_t req;
	crontab_update_response_msg_t *resp = NULL;
	slurm_msg_t request, response;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&request);
	slurm_msg_t_init(&response);

	req.crontab = crontab;
	req.jobs = jobs;
	req.uid = uid;
	req.gid = gid;
	request.msg_type = REQUEST_UPDATE_CRONTAB;
	request.data = &req;

	if (slurm_send_recv_controller_msg(&request, &response,
					   working_cluster_rec) < 0) {
		rc = SLURM_ERROR;
	} else if (response.msg_type == RESPONSE_UPDATE_CRONTAB) {
		resp = (crontab_update_response_msg_t *) response.data;
		if (!resp)
			rc = SLURM_ERROR;
	} else if (response.msg_type == RESPONSE_SLURM_RC) {
		rc = ((return_code_msg_t *) response.data)->return_code;
	} else {
		rc = SLURM_ERROR;
	}

	if (rc) {
		resp = xmalloc(sizeof(*resp));
		resp->return_code = rc;
	}

	return resp;
}

extern int slurm_remove_crontab(uid_t uid, gid_t gid)
{
	crontab_update_request_msg_t req;
	crontab_update_response_msg_t *resp;
	slurm_msg_t request, response;
	int rc = SLURM_SUCCESS;

	slurm_msg_t_init(&request);
	slurm_msg_t_init(&response);

	req.crontab = NULL;
	req.jobs = NULL;
	req.uid = uid;
	req.gid = gid;
	request.msg_type = REQUEST_UPDATE_CRONTAB;
	request.data = &req;

	if (slurm_send_recv_controller_msg(&request, &response,
					   working_cluster_rec) < 0)
		return SLURM_ERROR;

	if (response.msg_type == RESPONSE_UPDATE_CRONTAB) {
		resp = (crontab_update_response_msg_t *) response.data;
		rc = resp->return_code;
	} else if (response.msg_type == RESPONSE_SLURM_RC) {
		rc = ((return_code_msg_t *) response.data)->return_code;
	} else {
		rc = SLURM_ERROR;
	}

	slurm_free_msg_data(response.msg_type, response.data);
	return rc;
}
