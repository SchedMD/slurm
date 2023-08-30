/*****************************************************************************\
 *  tres.c - Slurm REST API accounting TRES http operations handlers
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

#include <stdint.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/dbv0.0.39/api.h"

static void _dump_tres(ctxt_t *ctxt)
{
	list_t *tres_list = NULL;
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};

	if (db_query_list(ctxt, &tres_list, slurmdb_tres_get, &tres_cond))
		return;

	DATA_DUMP(ctxt->parser, TRES_LIST, tres_list,
		  data_key_set(ctxt->resp, "TRES"));
}

static void _update_tres(ctxt_t *ctxt, bool commit)
{
#ifdef NDEBUG
	/*
	 * Updating TRES is not currently supported and is disabled
	 * except for developer testing as the TRES id can not be maintained
	 * while updating or adding new TRES.
	 */
	if (commit)
		resp_error(ctxt, ESLURM_NOT_SUPPORTED, __func__,
			   "Updating TRES is not currently supported");
#else
	data_t *dtres = NULL;
	int rc = SLURM_SUCCESS;
	List tres_list = NULL;
	data_t *parent_path = NULL;

	tres_list = list_create(slurmdb_destroy_tres_rec);

	if (!(dtres = get_query_key_list("TRES", ctxt, &parent_path))) {
		resp_warn(ctxt, __func__,
			  "ignoring empty or non-existant TRES array");
		goto cleanup;
	}

	if ((rc = DATA_PARSE(ctxt->parser, TRES_LIST, tres_list, dtres,
			     parent_path)))
		goto cleanup;

	if (!(rc = db_query_rc(ctxt, tres_list, slurmdb_tres_add)) && commit)
		db_query_commit(ctxt);

cleanup:
	FREE_NULL_LIST(tres_list);
	xfree(parent_path);
#endif /*!NDEBUG*/
}

extern int op_handler_tres(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc)
		/* no-op already logged */;
	else if (method == HTTP_REQUEST_GET)
		_dump_tres(ctxt);
	else if (method == HTTP_REQUEST_POST)
		_update_tres(ctxt, (tag != CONFIG_OP_TAG));
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

extern void init_op_tres(void)
{
	bind_operation_handler("/slurmdb/v0.0.39/tres/", op_handler_tres, 0);
}

extern void destroy_op_tres(void)
{
	unbind_operation_handler(op_handler_tres);
}
