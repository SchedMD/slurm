/*****************************************************************************\
 *  convert_format.c - Slurm REST API conversion http operations handlers
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

#include "src/common/xmalloc.h"

#include "src/interfaces/data_parser.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

static int _convert_hostnames_hostlist(openapi_ctxt_t *ctxt,
				       data_parser_type_t input_parser,
				       data_parser_type_t output_parser)
{
	openapi_resp_single_t req = { 0 };
	openapi_resp_single_t *req_ptr = &req; /* required for free macro */
	openapi_resp_single_t resp = { 0 };
	int rc = SLURM_SUCCESS;

	if (ctxt->method != HTTP_REQUEST_POST)
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Unsupported HTTP method requested: %s",
				  get_http_method_string(ctxt->method));

	if (!ctxt->query)
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "unexpected empty query");

	if (data_parser_g_parse(ctxt->parser, input_parser, &req, sizeof(req),
				ctxt->query, ctxt->parent_path)) {
		xfree(req.response);
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Rejecting request. Failure parsing request");
	}

	SWAP(resp.response, req.response);
	rc = data_parser_g_dump(ctxt->parser, output_parser, &resp,
				sizeof(resp), ctxt->resp);
	xfree(resp.response);
	FREE_OPENAPI_RESP_COMMON_CONTENTS(req_ptr);
	return rc;
}

extern int op_handler_hostlist(openapi_ctxt_t *ctxt)
{
	return _convert_hostnames_hostlist(
		ctxt, DATA_PARSER_OPENAPI_HOSTNAMES_REQ_RESP,
		DATA_PARSER_OPENAPI_HOSTLIST_REQ_RESP);
}

extern int op_handler_hostnames(openapi_ctxt_t *ctxt)
{
	return _convert_hostnames_hostlist(
		ctxt, DATA_PARSER_OPENAPI_HOSTLIST_REQ_RESP,
		DATA_PARSER_OPENAPI_HOSTNAMES_REQ_RESP);
}
