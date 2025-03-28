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

#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

static int _set_unused_flag(void *x, void *args)
{
	resv_desc_msg_t *resv_msg = x;
	if (!resv_msg->flags)
		resv_msg->flags = NO_VAL64;
	return SLURM_SUCCESS;
}

static int _parse_resv_desc_list(openapi_ctxt_t *ctxt,
				 openapi_reservation_mod_request_t *resv_req)
{
	int rc = SLURM_SUCCESS;
	char *empty_list_msg =
		"No reservation descriptions specified in reservations array";

	xassert(resv_req && !resv_req->reservations);

	if (!ctxt->query) {
		rc = ESLURM_REST_INVALID_QUERY;
		resp_error(ctxt, rc, __func__,
			   "unexpected empty query for reservation creation");
		return rc;
	}

	if (DATA_PARSE(ctxt->parser, RESERVATION_MOD_REQ, *resv_req,
		       ctxt->query, ctxt->parent_path)) {
		rc = ESLURM_REST_INVALID_QUERY;
		resp_error(ctxt, rc, __func__,
			   "Rejecting request. Failure parsing parameters");
		FREE_NULL_LIST(resv_req->reservations);
		return rc;
	}

	if (resv_req->reservations && list_count(resv_req->reservations))
		list_for_each(resv_req->reservations, _set_unused_flag, NULL);
	else if (resv_req->reservations)
		resp_warn(ctxt, __func__, "%s", empty_list_msg);
	else {
		rc = ESLURM_REST_INVALID_QUERY;
		resp_error(ctxt, rc, __func__, "%s", empty_list_msg);
	}

	return rc;
}

/*
 * Create or update reservations
 * IN: validate_func - function for use in list_find_first() to validate
 *		       list of parsed resv_desc_msg_t *
 * IN: mod_func - function for use in list_find_first() to send the requests to
 *		  the controller
 */
static int _mod_reservations(openapi_ctxt_t *ctxt, ListFindF validate_func,
			     ListFindF mod_func)
{
	openapi_reservation_mod_request_t resv_req = { 0 };
	int rc = SLURM_SUCCESS;

	if ((rc = _parse_resv_desc_list(ctxt, &resv_req)))
		return rc;

	if (!list_find_first(resv_req.reservations, validate_func, ctxt) &&
	    !list_find_first(resv_req.reservations, mod_func, ctxt)) {
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_RESERVATION_MOD_RESP,
					 resv_req.reservations, ctxt);
	}

	FREE_NULL_LIST(resv_req.reservations);
	return ctxt->rc;
}

/* return true on error else false, meant for use in list_find_first() */
static int _validate_each_resv_desc_msg(void *x, void *args)
{
	resv_desc_msg_t *resv_msg = x;
	openapi_ctxt_t *ctxt = args;
	char *error_msg;

	if (validate_resv_create_desc(resv_msg, &error_msg)) {
		resp_error(ctxt, ESLURM_RESERVATION_INVALID,
			   "validate_resv_create_desc", "%s", error_msg);
		return true;
	}

	return false;
}

/* Create reservation from desc - return true on error, list_find_first func */
static int _create_each_resv(void *x, void *args)
{
	resv_desc_msg_t *resv_msg = x;
	openapi_ctxt_t *ctxt = args;
	char *new_res_name = NULL;

	if (!(new_res_name = slurm_create_reservation(resv_msg))) {
		int rc = errno;
		if (((rc == ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) ||
		     (rc == ESLURM_NODES_BUSY)) && !resv_msg->node_list) {
			resp_error(ctxt, rc, "slurm_create_reservation",
				   "Error creating reservation %s. Note, unless nodes are directly requested a reservation must exist in a single partition. If no partition is requested the default partition is assumed.",
				   resv_msg->name);
		} else {
			resp_error(ctxt, rc, "slurm_create_reservation",
				   "Error creating reservation  %s",
				   resv_msg->name);
		}
		return true;
	}

	free(new_res_name);
	return false;
}

static int _get_reservations(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	reserve_info_msg_t *res_info_ptr = NULL;
	openapi_reservation_query_t query = {0};
	openapi_resp_reserve_info_msg_t resp = {0};

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

extern int op_handler_reservations(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	if (ctxt->method == HTTP_REQUEST_GET) {
		rc = _get_reservations(ctxt);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		rc = _mod_reservations(ctxt, _validate_each_resv_desc_msg,
				       _create_each_resv);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

	return rc;
}

/* return true on error else false */
static int _check_resv_name(void *x, void *args)
{
	resv_desc_msg_t *resv_msg = x;
	openapi_ctxt_t *ctxt = args;

	if (resv_msg->name == NULL) {
		resp_error(ctxt, ESLURM_RESERVATION_INVALID, __func__,
			   "Reservation must be given.  No reservation update.");
		return true;
	}
	return false;
}

/* Update reservation from desc - return true on error, list_find_first func */
static int _update_each_resv(void *x, void *args)
{
	resv_desc_msg_t *resv_msg = x;
	openapi_ctxt_t *ctxt = args;
	int rc;

	if ((rc = slurm_update_reservation(resv_msg))) {
		if (errno)
			rc = errno;
		resp_error(ctxt, rc, "slurm_update_reservation",
			   "Error updating reservation %s", resv_msg->name);
		return true;
	}

	return false;
}

extern int op_handler_reservations_update(openapi_ctxt_t *ctxt)
{
	if (ctxt->method != HTTP_REQUEST_POST)
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Unsupported HTTP method requested: %s",
				  get_http_method_string(ctxt->method));

	return _mod_reservations(ctxt, _check_resv_name, _update_each_resv);
}

static int _parse_resv_name_param(openapi_ctxt_t *ctxt,
				  openapi_reservation_param_t *params)
{
	xassert(params);

	if (DATA_PARSE(ctxt->parser, OPENAPI_RESERVATION_PARAM, *params,
		       ctxt->parameters, ctxt->parent_path)) {
		xfree(params->reservation_name);
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Rejecting request. Failure parsing parameters");
	}

	return SLURM_SUCCESS;
}

static int _get_single_reservation(openapi_ctxt_t *ctxt)
{
	openapi_reservation_param_t params = { 0 };
	openapi_reservation_query_t query = { 0 };
	int rc = SLURM_SUCCESS;
	reserve_info_msg_t *res_info_ptr = NULL;
	reserve_info_t *res = NULL;

	if (_parse_resv_name_param(ctxt, &params))
		goto done;

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

static int _parse_resv_desc(openapi_ctxt_t *ctxt, resv_desc_msg_t *resv_msg)
{
	int rc = SLURM_SUCCESS;
	xassert(resv_msg);

	if (!ctxt->query) {
		rc = ESLURM_REST_INVALID_QUERY;
		resp_error(ctxt, rc, __func__,
			   "unexpected empty query for reservation creation");
		return rc;
	}

	slurm_init_resv_desc_msg(resv_msg);
	resv_msg->flags = 0; /* required for parsing */

	if (DATA_PARSE(ctxt->parser, RESERVATION_DESC_MSG, *resv_msg,
		       ctxt->query, ctxt->parent_path)) {
		rc = ESLURM_REST_INVALID_QUERY;
		resp_error(ctxt, rc, __func__,
			   "Rejecting request. Failure parsing parameters");
		slurm_free_resv_desc_members(resv_msg);
		return rc;
	}

	if (!resv_msg->flags)
		resv_msg->flags = NO_VAL64;

	return rc;
}

extern int _reservation_update(openapi_ctxt_t *ctxt)
{
	openapi_reservation_param_t params = { 0 };
	resv_desc_msg_t resv_msg = { 0 };
	int rc = SLURM_SUCCESS;

	if (ctxt->method != HTTP_REQUEST_POST)
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Unsupported HTTP method requested: %s",
				  get_http_method_string(ctxt->method));

	if ((rc = _parse_resv_name_param(ctxt, &params)) ||
	    (rc = _parse_resv_desc(ctxt, &resv_msg)))
		return rc;

	xfree(resv_msg.name);
	SWAP(resv_msg.name, params.reservation_name);

	if (resv_msg.name == NULL) {
		resp_error(ctxt, ESLURM_RESERVATION_INVALID, __func__,
			   "Reservation must be given.  No reservation update.");
	} else if ((rc = slurm_update_reservation(&resv_msg))) {
		if (errno)
			rc = errno;
		resp_error(ctxt, rc, "slurm_update_reservation",
			   "Error updating the reservation");
	} else {
		list_t *resv_list = list_create(NULL);
		list_append(resv_list, &resv_msg);
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_RESERVATION_MOD_RESP,
					 resv_list, ctxt);
		FREE_NULL_LIST(resv_list);
	}

	slurm_free_resv_desc_members(&resv_msg);
	return rc ? rc : ctxt->rc;
}

extern int op_handler_reservation(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	if (ctxt->method == HTTP_REQUEST_GET) {
		rc = _get_single_reservation(ctxt);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		rc = _reservation_update(ctxt);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

	return rc;
}
