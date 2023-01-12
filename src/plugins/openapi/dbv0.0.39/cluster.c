/*****************************************************************************\
 *  cluster.c - Slurm REST API acct cluster http operations handlers
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

#define MAGIC_FOREACH_CLUSTER 0x2aa2faf2
#define MAGIC_FOREACH_DEL_CLUSTER 0xa3a2aa3a

typedef struct {
	int magic; /* MAGIC_FOREACH_CLUSTER */
	data_t *clusters;
	ctxt_t *ctxt;
} foreach_cluster_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_DEL_CLUSTER */
	data_t *clusters;
	ctxt_t *ctxt;
} foreach_del_cluster_t;

static int _foreach_cluster(void *x, void *arg)
{
	slurmdb_cluster_rec_t *cluster = x;
	foreach_cluster_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_CLUSTER);
	xassert(args->ctxt->magic == MAGIC_CTXT);

	if (DATA_DUMP(args->ctxt->parser, CLUSTER_REC, *cluster,
		      data_list_append(args->clusters)))
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static void _dump_clusters(ctxt_t *ctxt, char *cluster)
{
	slurmdb_cluster_cond_t cluster_cond = {
		.cluster_list = list_create(NULL),
		.with_deleted = true,
		.with_usage = true,
		.flags = NO_VAL,
	};
	foreach_cluster_t args = {
		.magic = MAGIC_FOREACH_CLUSTER,
		.ctxt = ctxt,
	};
	List cluster_list = NULL;

	args.clusters = data_set_list(data_key_set(ctxt->resp, "clusters"));

	if (cluster)
		list_append(cluster_cond.cluster_list, cluster);

	if (db_query_list(ctxt, &cluster_list, slurmdb_clusters_get,
			  &cluster_cond))
		/* no-op - error already logged */;
	else if (cluster_list)
		list_for_each(cluster_list, _foreach_cluster, &args);

	FREE_NULL_LIST(cluster_list);
	FREE_NULL_LIST(cluster_cond.cluster_list);
}

static int _foreach_del_cluster(void *x, void *arg)
{
	char *cluster = x;
	foreach_del_cluster_t *args = arg;

	data_set_string(data_list_append(args->clusters), cluster);
	return 1;
}

static void _delete_cluster(ctxt_t *ctxt, char *cluster)
{
	slurmdb_cluster_cond_t cluster_cond = {
		.cluster_list = list_create(NULL),
		.flags = NO_VAL,
	};
	foreach_del_cluster_t args = {
		.magic = MAGIC_FOREACH_DEL_CLUSTER,
		.ctxt = ctxt,
	};
	List cluster_list = NULL;

	args.clusters =
		data_set_list(data_key_set(ctxt->resp, "deleted_clusters"));

	if (!cluster || !cluster[0]) {
		resp_warn(ctxt, __func__,
			  "ignoring empty delete cluster request");
		goto cleanup;
	}

	list_append(cluster_cond.cluster_list, cluster);

	if (!db_query_list(ctxt, &cluster_list, slurmdb_clusters_remove,
			   &cluster_cond))
		db_query_commit(ctxt);

	if (cluster_list)
		list_for_each(cluster_list, _foreach_del_cluster, &args);

cleanup:
	FREE_NULL_LIST(cluster_list);
	FREE_NULL_LIST(cluster_cond.cluster_list);
}

static void _update_clusters(ctxt_t *ctxt, bool commit)
{
	data_t *parent_path = NULL;
	data_t *dclusters;
	List cluster_list = list_create(slurmdb_destroy_cluster_rec);

	dclusters = get_query_key_list("clusters", ctxt, &parent_path);

	if (!dclusters) {
		resp_warn(ctxt, __func__,
			  "ignoring non-existant clusters array");
	} else if (!data_get_list_length(dclusters)) {
		resp_warn(ctxt, __func__, "ignoring empty clusters array");
	} else if (DATA_PARSE(ctxt->parser, CLUSTER_REC_LIST, cluster_list,
			      dclusters, parent_path)) {
		/* no-op already logged */
	} else if (!db_query_rc(ctxt, cluster_list, slurmdb_clusters_add) &&
		   commit) {
		db_query_commit(ctxt);
	}

	FREE_NULL_LIST(cluster_list);
	FREE_NULL_DATA(parent_path);
}

extern int op_handler_cluster(const char *context_id,
			      http_request_method_t method, data_t *parameters,
			      data_t *query, int tag, data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);
	char *cluster = get_str_param("cluster_name", ctxt);

	if (ctxt->rc)
		/* no-op - already logged */;
	else if (method == HTTP_REQUEST_GET)
		_dump_clusters(ctxt, cluster);
	else if (method == HTTP_REQUEST_DELETE)
		_delete_cluster(ctxt, cluster);
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

extern int op_handler_clusters(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp,
			       void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc)
		/* no-op - already logged */;
	else if (method == HTTP_REQUEST_GET)
		_dump_clusters(ctxt, NULL);
	else if (method == HTTP_REQUEST_POST)
		_update_clusters(ctxt, (tag != CONFIG_OP_TAG));
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

extern void init_op_cluster(void)
{
	bind_operation_handler("/slurmdb/v0.0.39/clusters/",
			       op_handler_clusters, 0);
	bind_operation_handler("/slurmdb/v0.0.39/cluster/{cluster_name}",
			       op_handler_cluster, 0);
}

extern void destroy_op_cluster(void)
{
	unbind_operation_handler(op_handler_clusters);
	unbind_operation_handler(op_handler_clusters);
}
