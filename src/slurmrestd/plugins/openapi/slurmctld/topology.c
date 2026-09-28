/*****************************************************************************\
 *  topology.c - Slurm REST API topology http operations handlers
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

#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

extern int op_handler_topology(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	topo_info_response_msg_t *topo_info_msg = NULL;
	openapi_string_param_t param = { 0 };

	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, (rc = ESLURM_REST_INVALID_QUERY), __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	} else if (ctxt->parameters &&
		   DATA_PARSE(ctxt->parser, OPENAPI_TOPO_INFO_PARAM, param,
			      ctxt->parameters, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing parameters");
	} else if (!(errno = 0) &&
		   (rc = slurm_load_topo(&topo_info_msg, param.string))) {
		if ((rc == SLURM_ERROR) && errno)
			rc = errno;
		/*
		 * The requested topology not being configured means the
		 * client asked for something that does not exist, which is
		 * only a "not found" for this lookup. Translate it here so
		 * that the other RPCs returning this error (job submission,
		 * node and partition updates) keep their HTTP status.
		 */
		if (rc == ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE)
			rc = ESLURM_REST_TOPO_NOT_FOUND;
		resp_error(ctxt, rc, __func__,
			   "slurm_load_topo() failed to load topology");
	}

	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_TOPO_INFO_RESP,
				 (topo_info_msg ? topo_info_msg->topo_info :
						  NULL),
				 ctxt);

	slurm_free_topo_info_msg(topo_info_msg);
	xfree(param.string);
	return rc;
}
