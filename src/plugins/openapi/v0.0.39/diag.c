/*****************************************************************************\
 *  diag.c - Slurm REST API diag http operations handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "src/plugins/openapi/v0.0.39/api.h"

static int _op_handler_diag(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp, void *auth)
{
	int rc;
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	} else {
		stats_info_response_msg_t *stats = NULL;
		stats_info_request_msg_t req = {
			.command_id = STAT_COMMAND_GET,
		};
		data_t *dstats = data_key_set(resp, "statistics");

		if ((rc = slurm_get_statistics(&stats, &req)))
			resp_error(ctxt, rc, __func__,
				   "slurm_get_statistics() failed to get slurmctld statistics");
		else
			DATA_DUMP(ctxt->parser, STATS_MSG, *stats, dstats);

		slurm_free_stats_response_msg(stats);
	}

	return fini_connection(ctxt);
}

static int _op_handler_ping(const char *context_id,
			    http_request_method_t method, data_t *parameters,
			    data_t *query, int tag, data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	} else {
		controller_ping_t *pings = ping_all_controllers();

		DATA_DUMP(ctxt->parser, CONTROLLER_PING_ARRAY, pings,
			  data_key_set(resp, "pings"));

		xfree(pings);
	}

	return fini_connection(ctxt);
}

/* based on _print_license_info() from scontrol */
static int _op_handler_licenses(const char *context_id,
				http_request_method_t method,
				data_t *parameters, data_t *query, int tag,
				data_t *resp, void *auth)
{
	int rc;
	license_info_msg_t *msg = NULL;
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (method != HTTP_REQUEST_GET)
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	else if ((rc = slurm_load_licenses(0, &msg, 0)))
		resp_error(ctxt, rc, __func__,
			   "slurm_load_licenses() was unable to load licenses");
	else
		DATA_DUMP(ctxt->parser, LICENSES, *msg,
			  data_key_set(resp, "licenses"));

	slurm_free_license_info_msg(msg);
	return fini_connection(ctxt);
}

extern void init_op_diag(void)
{
	bind_operation_handler("/slurm/v0.0.39/diag/", _op_handler_diag, 0);
	bind_operation_handler("/slurm/v0.0.39/ping/", _op_handler_ping, 0);
	bind_operation_handler("/slurm/v0.0.39/licenses/", _op_handler_licenses,
			       0);
}

extern void destroy_op_diag(void)
{
	unbind_operation_handler(_op_handler_diag);
	unbind_operation_handler(_op_handler_ping);
	unbind_operation_handler(_op_handler_licenses);
}
