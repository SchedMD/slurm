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

#include "config.h"

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

#include "src/plugins/openapi/dbv0.0.38/api.h"

#define MAGIC_FOREACH_CLUSTER 0x2aa2faf2
typedef struct {
	int magic;
	data_t *clusters;
	List tres_list;
} foreach_cluster_t;

static int _foreach_cluster(void *x, void *arg)
{
	slurmdb_cluster_rec_t *cluster = x;
	foreach_cluster_t *args = arg;
	parser_env_t penv = {
		.g_tres_list = args->tres_list,
	};

	xassert(args->magic == MAGIC_FOREACH_CLUSTER);

	if (dump(PARSE_CLUSTER_REC, cluster,
		 data_set_dict(data_list_append(args->clusters)), &penv))
		return -1;

	return 1;
}

static int _dump_clusters(data_t *resp, data_t *errors, char *cluster,
			  void *auth)
{
	int rc = SLURM_SUCCESS;
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};
	slurmdb_cluster_cond_t cluster_cond = {
		.cluster_list = list_create(NULL),
		.with_deleted = true,
		.with_usage = true,
		.flags = NO_VAL,
	};
	foreach_cluster_t args = {
		.magic = MAGIC_FOREACH_CLUSTER,
		.clusters = data_set_list(data_key_set(resp, "clusters")),
	};
	List cluster_list = NULL;

	if (cluster)
		list_append(cluster_cond.cluster_list, cluster);

	if (!(rc = db_query_list(errors, auth, &args.tres_list,
				 slurmdb_tres_get, &tres_cond)) &&
	    !(rc = db_query_list(errors, auth, &cluster_list,
				 slurmdb_clusters_get, &cluster_cond)) &&
	    (list_for_each(cluster_list, _foreach_cluster, &args) < 0))
		rc = ESLURM_DATA_CONV_FAILED;

	FREE_NULL_LIST(cluster_list);
	FREE_NULL_LIST(cluster_cond.cluster_list);
	FREE_NULL_LIST(args.tres_list);

	return rc;
}

#define MAGIC_FOREACH_DEL_CLUSTER 0xa3a2aa3a
typedef struct {
	int magic;
	data_t *clusters;
} foreach_del_cluster_t;

static int _foreach_del_cluster(void *x, void *arg)
{
	char *cluster = x;
	foreach_del_cluster_t *args = arg;

	data_set_string(data_list_append(args->clusters), cluster);
	return 1;
}

static int _delete_cluster(data_t *resp, data_t *errors, char *cluster,
			   void *auth)
{
	int rc = SLURM_SUCCESS;
	slurmdb_cluster_cond_t cluster_cond = {
		.with_deleted = true,
		.cluster_list = list_create(NULL),
	};
	foreach_del_cluster_t args = {
		.magic = MAGIC_FOREACH_DEL_CLUSTER,
		.clusters = data_set_list(
			data_key_set(resp, "deleted_clusters")),
	};
	List cluster_list = NULL;

	if (!cluster) {
		rc = ESLURM_REST_EMPTY_RESULT;
		goto cleanup;
	}

	list_append(cluster_cond.cluster_list, cluster);

	if (!(rc = db_query_list(errors, auth, &cluster_list,
				 slurmdb_clusters_remove, &cluster_cond)))
		rc = db_query_commit(errors, auth);

	if (!rc &&
	    (list_for_each(cluster_list, _foreach_del_cluster, &args) < 0))
		rc = ESLURM_DATA_CONV_FAILED;
cleanup:
	FREE_NULL_LIST(cluster_list);
	FREE_NULL_LIST(cluster_cond.cluster_list);

	return rc;
}

#define MAGIC_FOREACH_UP_CLUSTER 0xdaba3019
typedef struct {
	int magic;
	List cluster_list;
	List tres_list;
	data_t *errors;
	rest_auth_context_t *auth;
} foreach_update_cluster_t;

static data_for_each_cmd_t _foreach_update_cluster(data_t *data, void *arg)
{
	foreach_update_cluster_t *args = arg;
	slurmdb_cluster_rec_t *cluster;
	parser_env_t penv = {
		.auth = args->auth,
		.g_tres_list = args->tres_list,
	};

	xassert(args->magic == MAGIC_FOREACH_UP_CLUSTER);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		resp_error(args->errors, ESLURM_REST_INVALID_QUERY,
			   "each cluster entry must be a dictionary", NULL);
		return DATA_FOR_EACH_FAIL;
	}

	cluster = xmalloc(sizeof(slurmdb_cluster_rec_t));
	slurmdb_init_cluster_rec(cluster, false);

	cluster->accounting_list = list_create(
		slurmdb_destroy_cluster_accounting_rec);
	(void)list_append(args->cluster_list, cluster);

	if (parse(PARSE_CLUSTER_REC, cluster, data, args->errors, &penv))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _update_clusters(data_t *query, data_t *resp, data_t *errors,
			    void *auth, bool commit)
{
	int rc = SLURM_SUCCESS;
	foreach_update_cluster_t args = {
		.magic = MAGIC_FOREACH_UP_CLUSTER,
		.auth = auth,
		.errors = errors,
		.cluster_list = list_create(slurmdb_destroy_cluster_rec),
	};
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};
	data_t *dclusters = get_query_key_list("clusters", errors, query);

	if (!(rc = db_query_list(errors, auth, &args.tres_list,
				 slurmdb_tres_get, &tres_cond)) &&
	    (data_list_for_each(dclusters, _foreach_update_cluster, &args) < 0))
		rc = ESLURM_REST_INVALID_QUERY;

	if (!(rc = db_query_rc(errors, auth, args.cluster_list,
			       slurmdb_clusters_add)) &&
	    commit)
		db_query_commit(errors, auth);

	FREE_NULL_LIST(args.cluster_list);
	FREE_NULL_LIST(args.tres_list);

	return rc;
}

extern int op_handler_cluster(const char *context_id,
			      http_request_method_t method, data_t *parameters,
			      data_t *query, int tag, data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	char *cluster = get_str_param("cluster_name", errors, parameters);

	if (!rc && (method == HTTP_REQUEST_GET))
		rc = _dump_clusters(resp, errors, cluster, auth);
	else if (!rc && (method == HTTP_REQUEST_DELETE))
		rc = _delete_cluster(resp, errors, cluster, auth);
	else
		rc = ESLURM_REST_INVALID_QUERY;

	return rc;
}

extern int op_handler_clusters(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp,
			       void *auth)
{
	data_t *errors = populate_response_format(resp);
	int rc = SLURM_SUCCESS;

	if (method == HTTP_REQUEST_GET)
		rc = _dump_clusters(resp, errors, NULL, auth);
	else if (method == HTTP_REQUEST_POST)
		rc = _update_clusters(query, resp, errors, auth,
				      (tag != CONFIG_OP_TAG));
	else
		rc = ESLURM_REST_INVALID_QUERY;

	return rc;
}

extern void init_op_cluster(void)
{
	bind_operation_handler("/slurmdb/v0.0.38/clusters/",
			       op_handler_clusters, 0);
	bind_operation_handler("/slurmdb/v0.0.38/cluster/{cluster_name}",
			       op_handler_cluster, 0);
}

extern void destroy_op_cluster(void)
{
	unbind_operation_handler(op_handler_clusters);
	unbind_operation_handler(op_handler_clusters);
}
