/*****************************************************************************\
 *  wckeys.c - Slurm REST API acct wckey http operations handlers
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
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"
#include "api.h"

static void _dump_wckeys(ctxt_t *ctxt, slurmdb_wckey_cond_t *wckey_cond)
{
	list_t *wckey_list = NULL;

	if (!db_query_list(ctxt, &wckey_list, slurmdb_wckeys_get, wckey_cond))
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_WCKEY_RESP, wckey_list, ctxt);

	FREE_NULL_LIST(wckey_list);
}

static void _delete_wckey(ctxt_t *ctxt, slurmdb_wckey_cond_t *wckey_cond)
{
	list_t *wckey_list = NULL;

	if (!db_query_list(ctxt, &wckey_list, slurmdb_wckeys_remove,
			   wckey_cond))
		db_query_commit(ctxt);

	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_WCKEY_REMOVED_RESP, wckey_list, ctxt);

	FREE_NULL_LIST(wckey_list);
}

extern int update_wckeys(ctxt_t *ctxt, bool commit, list_t *wckey_list)
{
	if (!db_query_rc(ctxt, wckey_list, slurmdb_wckeys_add) && !ctxt->rc &&
	    commit)
		db_query_commit(ctxt);

	return ctxt->rc;
}

static void _update_wckeys(ctxt_t *ctxt)
{
	openapi_resp_single_t resp = {0};
	openapi_resp_single_t *resp_ptr = &resp;

	if (!DATA_PARSE(ctxt->parser, OPENAPI_WCKEY_RESP, resp, ctxt->query,
			ctxt->parent_path)) {
		list_t *wckey_list = resp.response;

		if (!wckey_list || list_is_empty(wckey_list)) {
			resp_warn(ctxt, __func__,
				  "ignoring empty or non-existant wckeys array for update");
		} else {
			update_wckeys(ctxt, true, wckey_list);
		}

		FREE_NULL_LIST(wckey_list);
	}

	FREE_OPENAPI_RESP_COMMON_CONTENTS(resp_ptr);
}

extern int op_handler_wckey(ctxt_t *ctxt)
{
	slurmdb_wckey_cond_t wckey_cond = {0};
	openapi_wckey_param_t params = {0};

	if (DATA_PARSE(ctxt->parser, OPENAPI_WCKEY_PARAM, params,
		       ctxt->parameters, ctxt->parent_path))
		goto cleanup;

	if (!params.wckey || !params.wckey[0]) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "wckey required for singular query");
		goto cleanup;
	}

	wckey_cond.name_list = list_create(NULL);
	list_append(wckey_cond.name_list, params.wckey);

	if (ctxt->method == HTTP_REQUEST_GET) {
		_dump_wckeys(ctxt, &wckey_cond);
	} else if (ctxt->method == HTTP_REQUEST_DELETE) {
		_delete_wckey(ctxt, &wckey_cond);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	FREE_NULL_LIST(wckey_cond.name_list);
	xfree(params.wckey);

	return SLURM_SUCCESS;
}

extern int op_handler_wckeys(ctxt_t *ctxt)
{
	slurmdb_wckey_cond_t *wckey_cond = NULL;

	if (ctxt->method == HTTP_REQUEST_GET) {
		if (DATA_PARSE(ctxt->parser, WCKEY_CONDITION_PTR, wckey_cond,
			       ctxt->query, ctxt->parent_path))
			goto cleanup;

		_dump_wckeys(ctxt, wckey_cond);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		_update_wckeys(ctxt);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	slurmdb_destroy_wckey_rec(wckey_cond);
	return SLURM_SUCCESS;
}
