/*****************************************************************************\
 *  assoc_mgr.c - assoc_mgr operations handlers
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

#include "src/common/env.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

static void _dump_shares(openapi_ctxt_t *ctxt)
{
	int rc;

	shares_request_msg_t *req = NULL;
	shares_response_msg_t *resp = NULL;

	if (DATA_PARSE(ctxt->parser, SHARES_REQ_MSG_PTR, req, ctxt->parameters,
		       ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing parameters.");
		return;
	} else if ((rc = slurm_associations_get_shares(req, &resp))) {
		resp_error(ctxt, rc, __func__,
			   "slurm_associations_get_shares() failed: %s",
			   get_http_method_string(ctxt->method));
	} else {
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_SHARES_RESP, resp, ctxt);
	}

	slurm_free_shares_request_msg(req);
	slurm_free_shares_response_msg(resp);
}

extern int op_handler_shares(openapi_ctxt_t *ctxt)
{
	if (ctxt->method == HTTP_REQUEST_GET) {
		_dump_shares(ctxt);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

	return SLURM_SUCCESS;
}
