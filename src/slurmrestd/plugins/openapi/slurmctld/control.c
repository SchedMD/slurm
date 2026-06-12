/*****************************************************************************\
 *  control.c - slurmctld control operations handlers
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
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

extern int op_handler_reconfigure(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	if (ctxt->method != HTTP_REQUEST_GET)
		resp_error(ctxt, (rc = ESLURM_REST_INVALID_QUERY), __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	else if ((rc = slurm_reconfigure())) {
		if (errno)
			rc = errno;
		resp_error(ctxt, rc, __func__, "slurm_reconfigure() failed");
	}

	return rc;
}

static int _dump_config(openapi_ctxt_t *ctxt)
{
	int rc = EINVAL;
	openapi_resp_config_t resp = { 0 };
	openapi_config_query_t query = { 0 };

	if (DATA_PARSE(ctxt->parser, OPENAPI_CONF_QUERY, query, ctxt->query,
		       ctxt->parent_path))
		return resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				  "Rejecting request. Failure parsing query.");
	errno = SLURM_SUCCESS;
	if ((rc = slurm_load_ctl_conf(query.update_time, &resp.slurm_conf)) &&
	    (rc == SLURM_ERROR) && errno)
		rc = errno;

	if (!rc) {
		rc = DATA_DUMP(ctxt->parser, OPENAPI_CONF_RESP, resp,
			       ctxt->resp);
	} else if (rc == SLURM_NO_CHANGE_IN_DATA) {
		char ts[64] = "INVALID";
		slurm_make_time_str(&query.update_time, ts, sizeof(ts));
		resp_warn(ctxt, __func__,
			  "No config changes since update_time[%s]=%s",
			  TIMESPEC_STR(((timespec_t) {
					       .tv_sec = query.update_time }),
				       true),
			  ts);
		rc = SLURM_SUCCESS;
	} else {
		resp_error(ctxt, rc, __func__, "slurm_load_ctl_conf() failed");
	}

	slurm_free_conf(resp.slurm_conf);
	return rc;
}

extern int op_handler_config(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	if (ctxt->method != HTTP_REQUEST_GET)
		resp_error(ctxt, (rc = ESLURM_REST_INVALID_QUERY), __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	else
		rc = _dump_config(ctxt);

	return rc;
}
