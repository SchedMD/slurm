/*****************************************************************************\
 *  reservations.c - Slurm REST API reservations http operations handlers
 *****************************************************************************
 *  Copyright (C) 2020 UT-Battelle, LLC.
 *  Written by Matt Ezell <ezellma@ornl.gov>
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

#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

extern int _op_handler_reservations(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	reserve_info_msg_t *res_info_ptr = NULL;
	openapi_reservation_query_t query = {0};
	openapi_resp_reserve_info_msg_t resp = {0};

	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
		goto done;
	}

	if (DATA_PARSE(ctxt->parser, OPENAPI_RESERVATION_QUERY, query,
		       ctxt->query, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing query");
		goto done;
	}

	errno = 0;
	if ((rc = slurm_load_reservations(query.update_time, &res_info_ptr))) {
		if (rc == SLURM_ERROR)
			rc = errno;

		resp_error(ctxt, rc, "slurm_load_reservations()",
			   "Unable to query reservations");

		goto done;
	}

	if (res_info_ptr) {
		resp.last_update = res_info_ptr->last_update;
		resp.reservations = res_info_ptr;
	}

	DATA_DUMP(ctxt->parser, OPENAPI_RESERVATION_RESP, resp, ctxt->resp);

done:
	slurm_free_reservation_info_msg(res_info_ptr);
	return rc;
}

extern int _op_handler_reservation(openapi_ctxt_t *ctxt)
{
	openapi_reservation_param_t params = {0};
	openapi_reservation_query_t query = {0};
	int rc = SLURM_SUCCESS;
	reserve_info_msg_t *res_info_ptr = NULL;
	reserve_info_t *res = NULL;

	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
		goto done;
	}

	if (DATA_PARSE(ctxt->parser, OPENAPI_RESERVATION_PARAM, params,
		       ctxt->parameters, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing parameters");
		goto done;
	}

	if (DATA_PARSE(ctxt->parser, OPENAPI_RESERVATION_QUERY, query,
		       ctxt->query, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing query");
		goto done;
	}

	errno = 0;
	if ((rc = slurm_load_reservations(query.update_time, &res_info_ptr)) ||
	    !res_info_ptr || !res_info_ptr->record_count) {
		if (rc == SLURM_ERROR)
			rc = errno;

		resp_error(ctxt, rc, "slurm_load_reservations()",
			   "Unable to query reservations");
		goto done;
	}

	for (int i = 0; !rc && i < res_info_ptr->record_count; i++) {
		const char *n = res_info_ptr->reservation_array[i].name;
		if (!xstrcasecmp(params.reservation_name, n)) {
			res = &res_info_ptr->reservation_array[i];
			break;
		}
	}

	if (!res && params.reservation_name) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unable to find reservation %s",
			   params.reservation_name);
	} else {
		reserve_info_msg_t r = {
			.last_update = res_info_ptr->last_update,
			.record_count = 1,
			.reservation_array = res,
		};
		openapi_resp_reserve_info_msg_t resp = {
			.reservations = &r,
			.last_update = res_info_ptr->last_update,
		};

		DATA_DUMP(ctxt->parser, OPENAPI_RESERVATION_RESP, resp,
			  ctxt->resp);
	}

done:
	slurm_free_reservation_info_msg(res_info_ptr);
	xfree(params.reservation_name);
	return rc;
}

extern void init_op_reservations(void)
{
	bind_handler("/slurm/{data_parser}/reservations/",
		     _op_handler_reservations);
	bind_handler("/slurm/{data_parser}/reservation/{reservation_name}",
		     _op_handler_reservation);
}

extern void destroy_op_reservations(void)
{
	unbind_operation_ctxt_handler(_op_handler_reservations);
}
