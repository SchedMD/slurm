/*****************************************************************************\
 *  jobs.c - Slurm REST API jobs http operations handlers
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

static void _handle_get(ctxt_t *ctxt, slurm_selected_step_t *job_id)
{
	int rc = SLURM_SUCCESS;
	resource_layout_msg_t *resp = NULL;
	list_t *nodes = NULL;

	if ((rc = slurm_get_resource_layout(&job_id->step_id,
					    (void **) &resp))) {
		char *id = NULL;

		fmt_job_id_string(job_id, &id);
		resp_error(ctxt, rc, __func__, "Unable to query JobId=%s", id);

		xfree(id);
	}

	if (resp)
		nodes = resp->nodes;

	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_RESOURCE_LAYOUT_RESP, nodes, ctxt);

	slurm_free_resource_layout_msg(resp);
}

extern int op_handler_resources(openapi_ctxt_t *ctxt)
{
	openapi_job_info_param_t params = { { 0 } };
	slurm_selected_step_t *job_id;

	if (DATA_PARSE(ctxt->parser, OPENAPI_JOB_INFO_PARAM, params,
		       ctxt->parameters, ctxt->parent_path)) {
		return resp_error(
			ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			"Rejecting request. Failure parsing parameters");
	}

	job_id = &params.job_id;

	if ((job_id->step_id.job_id == NO_VAL) ||
	    (job_id->step_id.job_id <= 0) ||
	    (job_id->step_id.job_id >= MAX_JOB_ID)) {
		return resp_error(ctxt, ESLURM_INVALID_JOB_ID, __func__,
				  "Invalid JobID=%u rejected",
				  job_id->step_id.job_id);
	}

	if (ctxt->method == HTTP_REQUEST_GET) {
		_handle_get(ctxt, job_id);
	} else {
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Unsupported HTTP method requested: %s",
				  get_http_method_string(ctxt->method));
	}

	return SLURM_SUCCESS;
}
