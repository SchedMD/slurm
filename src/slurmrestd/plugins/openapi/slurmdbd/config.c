/*****************************************************************************\
 *  config.c - Slurm REST API config http operations handlers
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"
#include "api.h"
#include "structs.h"

//static const openapi_ctxt_handler_t ops[] = {
//	/* Warning: order matters */
//	op_handler_clusters,
//	op_handler_tres,
//	op_handler_accounts,
//	op_handler_users,
//	op_handler_qos,
//	op_handler_wckeys,
//	op_handler_associations,
//};

static int _op_handler_config(ctxt_t *ctxt)
{
	openapi_resp_slurmdbd_config_t resp = {0};

	if ((ctxt->method != HTTP_REQUEST_GET) &&
	    (ctxt->method != HTTP_REQUEST_POST)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
		goto cleanup;
	}

	//	for (int i = 0; (i < ARRAY_SIZE(ops)); i++) {
	//		int rc = ops[i](ctxt);
	//
	//		/* Ignore empty results */
	//		if (rc == ESLURM_REST_EMPTY_RESULT)
	//			rc = SLURM_SUCCESS;
	//
	//		if (rc) {
	//			if (!ctxt->rc)
	//				ctxt->rc = rc;
	//			break;
	//		}
	//	}

	if (ctxt->method == HTTP_REQUEST_GET)
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_SLURMDBD_CONFIG_RESP, &resp,
					 ctxt);
	else if (!ctxt->rc && (ctxt->method == HTTP_REQUEST_POST))
		db_query_commit(ctxt);

cleanup:
	return SLURM_SUCCESS;
}

extern void init_op_config(void)
{
	bind_handler("/slurmdb/{data_parser}/config", _op_handler_config,
		     CONFIG_OP_TAG);
}

extern void destroy_op_config(void)
{
	unbind_operation_ctxt_handler(_op_handler_config);
}