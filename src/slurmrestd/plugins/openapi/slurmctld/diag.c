/*****************************************************************************\
 *  diag.c - Slurm REST API diag http operations handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

extern int op_handler_diag(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, (rc = ESLURM_REST_INVALID_QUERY), __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	} else {
		stats_info_response_msg_t *stats = NULL;
		stats_info_request_msg_t req = {
			.command_id = STAT_COMMAND_GET,
		};

		if ((rc = slurm_get_statistics(&stats, &req))) {
			if (errno)
				rc = errno;
			resp_error(ctxt, rc, __func__,
				   "slurm_get_statistics() failed to get slurmctld statistics");
		}
		else
			DUMP_OPENAPI_RESP_SINGLE(OPENAPI_DIAG_RESP, stats,
						 ctxt);

		slurm_free_stats_response_msg(stats);
	}

	return rc;
}

extern int op_handler_ping(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, (rc = ESLURM_REST_INVALID_QUERY), __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	} else {
		controller_ping_t *pings = ping_all_controllers();

		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_PING_ARRAY_RESP, pings, ctxt);

		xfree(pings);
	}

	return rc;
}

/* based on _print_license_info() from scontrol */
extern int op_handler_licenses(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	license_info_msg_t *msg = NULL;
	openapi_resp_license_info_msg_t resp = {0};

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
