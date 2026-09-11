/*****************************************************************************\
 *  hres.c - Slurm REST API HRES/license http operations handlers
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"

#include "api.h"

static int _update_hres(openapi_ctxt_t *ctxt)
{
	int rc;
	data_t *ppath = data_set_list(data_new());
	hres_update_msg_t *msg = xmalloc(sizeof(*msg));

	msg->count = NO_VAL;
	msg->disable_hres = NO_VAL16;
	msg->disable_layer = NO_VAL16;

	if ((rc = DATA_PARSE(ctxt->parser, HRES_UPDATE_MSG, *msg, ctxt->query,
			     ppath)))
		goto cleanup;
	if ((rc = slurm_update_hres(msg)))
		rc = resp_error(ctxt, rc, __func__, "Failure to update HRES");

cleanup:
	slurm_free_hres_update_msg(msg);
	FREE_NULL_DATA(ppath);
	return rc;
}

/* based on _print_license_info() from scontrol */
extern int op_handler_licenses(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	license_info_msg_t *msg = NULL;
	openapi_resp_license_info_msg_t resp = { 0 };

	if (ctxt->method != HTTP_REQUEST_GET)
		resp_error(ctxt, (rc = ESLURM_REST_INVALID_QUERY), __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	else if ((rc = slurm_load_licenses(0, &msg, 0))) {
		if (errno)
			rc = errno;
		resp_error(ctxt, rc, __func__,
			   "slurm_load_licenses() was unable to load licenses");
	}

	if (msg) {
		resp.licenses = msg;
		resp.last_update = msg->last_update;
	}

	DATA_DUMP(ctxt->parser, OPENAPI_LICENSES_RESP, resp, ctxt->resp);

	slurm_free_license_info_msg(msg);
	return rc;
}

extern int op_handler_hres(openapi_ctxt_t *ctxt)
{
	if (ctxt->method != HTTP_REQUEST_POST)
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Unsupported HTTP method requested: %s",
				  get_http_method_string(ctxt->method));
	return _update_hres(ctxt);
}
