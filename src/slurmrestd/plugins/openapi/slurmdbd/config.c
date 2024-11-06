/*****************************************************************************\
 *  config.c - Slurm REST API config http operations handlers
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

static void _dump(ctxt_t *ctxt, openapi_resp_slurmdbd_config_t *resp)
{
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = true,
		.count = NO_VAL,
	};
	slurmdb_cluster_cond_t cluster_cond = {
		.flags = NO_VAL,
		.with_deleted = true,
		.with_usage = true,
	};
	slurmdb_assoc_cond_t assoc_cond = {
		.flags = ASSOC_COND_FLAG_WITH_DELETED |
		ASSOC_COND_FLAG_WITH_USAGE | ASSOC_COND_FLAG_RAW_QOS |
		ASSOC_COND_FLAG_SUB_ACCTS,
	};
	slurmdb_account_cond_t acct_cond = {
		.assoc_cond = &assoc_cond,
		.flags = SLURMDB_ACCT_FLAG_DELETED |
		SLURMDB_ACCT_FLAG_WASSOC | SLURMDB_ACCT_FLAG_WCOORD,
	};
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
		.with_deleted = true,
		.with_assocs = true,
		.with_coords = true,
	};
	slurmdb_qos_cond_t qos_cond = {
		.flags = QOS_COND_FLAG_WITH_DELETED,
	};
	slurmdb_wckey_cond_t wckey_cond = {
		.with_deleted = true,
		.with_usage = true,
	};

	if (!db_query_list(ctxt, &resp->clusters, slurmdb_clusters_get,
			   &cluster_cond) &&
	    !db_query_list(ctxt, &resp->tres, slurmdb_tres_get, &tres_cond) &&
	    !db_query_list(ctxt, &resp->accounts, slurmdb_accounts_get,
			   &acct_cond) &&
	    !db_query_list(ctxt, &resp->users, slurmdb_users_get, &user_cond) &&
	    !db_query_list(ctxt, &resp->qos, slurmdb_qos_get, &qos_cond) &&
	    !db_query_list_xempty(ctxt, &resp->wckeys, slurmdb_wckeys_get,
				  &wckey_cond) &&
	    !db_query_list(ctxt, &resp->associations, slurmdb_associations_get,
			   &assoc_cond))
		DATA_DUMP(ctxt->parser, OPENAPI_SLURMDBD_CONFIG_RESP_PTR, resp,
			  ctxt->resp);
}

extern int op_handler_config(ctxt_t *ctxt)
{
	openapi_resp_slurmdbd_config_t resp = {0};
	openapi_resp_slurmdbd_config_t *resp_ptr = &resp;

	if ((ctxt->method != HTTP_REQUEST_GET) &&
	    (ctxt->method != HTTP_REQUEST_POST)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
		goto cleanup;
	}

	if (ctxt->method == HTTP_REQUEST_GET) {
		_dump(ctxt, &resp);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		if (DATA_PARSE(ctxt->parser, OPENAPI_SLURMDBD_CONFIG_RESP, resp,
			       ctxt->query, ctxt->parent_path))
			goto cleanup;

		if (!update_clusters(ctxt, false, resp.clusters) &&
		    !update_tres(ctxt, false, resp.tres) &&
		    !update_accounts(ctxt, false, resp.accounts) &&
		    !update_users(ctxt, false, resp.users) &&
		    !update_qos(ctxt, false, resp.qos) &&
		    !update_wckeys(ctxt, false, resp.wckeys) &&
		    !update_associations(ctxt, false, resp.associations) &&
		    !ctxt->rc)
			db_query_commit(ctxt);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	FREE_NULL_LIST(resp.clusters);
	FREE_NULL_LIST(resp.tres);
	FREE_NULL_LIST(resp.accounts);
	FREE_NULL_LIST(resp.users);
	FREE_NULL_LIST(resp.qos);
	FREE_NULL_LIST(resp.wckeys);
	FREE_NULL_LIST(resp.associations);
	FREE_OPENAPI_RESP_COMMON_CONTENTS(resp_ptr);
	return SLURM_SUCCESS;
}
