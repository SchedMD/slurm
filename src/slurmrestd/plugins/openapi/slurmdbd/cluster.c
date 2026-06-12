/*****************************************************************************\
 *  cluster.c - Slurm REST API acct cluster http operations handlers
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

static void _dump_clusters(ctxt_t *ctxt, slurmdb_cluster_cond_t *cluster_cond)
{
	list_t *cluster_list = NULL;

	(void) db_query_list(ctxt, &cluster_list, slurmdb_clusters_get,
			     cluster_cond);
	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_CLUSTERS_RESP, cluster_list, ctxt);

	FREE_NULL_LIST(cluster_list);
}

static void _delete_cluster(ctxt_t *ctxt, slurmdb_cluster_cond_t *cluster_cond)
{
	list_t *cluster_list = NULL;

	if (!db_query_list(ctxt, &cluster_list, slurmdb_clusters_remove,
			   cluster_cond))
		db_query_commit(ctxt);

	if (cluster_list)
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_CLUSTERS_REMOVED_RESP,
					 cluster_list, ctxt);

	FREE_NULL_LIST(cluster_list);
}

extern int update_clusters(ctxt_t *ctxt, bool commit, list_t *cluster_list)
{
	if (!db_query_rc(ctxt, cluster_list, slurmdb_clusters_add) && commit)
		db_query_commit(ctxt);

	return ctxt->rc;
}

static void _update_clusters(ctxt_t *ctxt)
{
	openapi_resp_single_t resp = {0};
	openapi_resp_single_t *resp_ptr = &resp;

	if (!DATA_PARSE(ctxt->parser, OPENAPI_CLUSTERS_RESP, resp, ctxt->query,
			ctxt->parent_path)) {
		list_t *cluster_list = resp.response;
		update_clusters(ctxt, true, cluster_list);
		FREE_NULL_LIST(cluster_list);
	}

	FREE_OPENAPI_RESP_COMMON_CONTENTS(resp_ptr);
}

extern int op_handler_cluster(ctxt_t *ctxt)
{
	openapi_cluster_param_t params = {0};
	slurmdb_cluster_cond_t cluster_cond = {
		.flags = NO_VAL,
	};

	if (DATA_PARSE(ctxt->parser, OPENAPI_CLUSTER_PARAM, params,
		       ctxt->parameters, ctxt->parent_path))
		goto cleanup;

	if (!params.name) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unable to parse cluster name");
		goto cleanup;
	}

	cluster_cond.cluster_list = list_create(NULL);
	list_append(cluster_cond.cluster_list, params.name);

	if (ctxt->method == HTTP_REQUEST_GET) {
		_dump_clusters(ctxt, &cluster_cond);
	} else if (ctxt->method == HTTP_REQUEST_DELETE) {
		_delete_cluster(ctxt, &cluster_cond);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	FREE_NULL_LIST(cluster_cond.cluster_list);
	xfree(params.name);
	return SLURM_SUCCESS;
}

extern int op_handler_clusters(ctxt_t *ctxt)
{
	slurmdb_cluster_cond_t *cluster_cond = NULL;

	if (((ctxt->method == HTTP_REQUEST_GET) ||
	     (ctxt->method == HTTP_REQUEST_DELETE)) &&
	    DATA_PARSE(ctxt->parser, CLUSTER_CONDITION_PTR, cluster_cond,
		       ctxt->query, ctxt->parent_path))
		goto cleanup;

	if (ctxt->method == HTTP_REQUEST_GET)
		_dump_clusters(ctxt, cluster_cond);
	else if (ctxt->method == HTTP_REQUEST_DELETE)
		_delete_cluster(ctxt, cluster_cond);
	else if (ctxt->method == HTTP_REQUEST_POST)
		_update_clusters(ctxt);
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	slurmdb_destroy_cluster_cond(cluster_cond);
	return SLURM_SUCCESS;
}
