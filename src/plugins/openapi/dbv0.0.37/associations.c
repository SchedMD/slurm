/*****************************************************************************\
 *  associations.c - Slurm REST API acct associations http operations handlers
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

#include "src/plugins/openapi/dbv0.0.37/api.h"

static int _foreach_delete_assoc(void *x, void *arg)
{
	char *assoc = x;
	data_t *assocs = arg;

	data_set_string(data_list_append(assocs), assoc);

	return DATA_FOR_EACH_CONT;
}

static int _dump_assoc_cond(data_t *resp, void *auth,
			    data_t *errors, slurmdb_assoc_cond_t *cond)
{
	int rc = SLURM_SUCCESS;
	List assoc_list = NULL;
	List tres_list = NULL;
	List qos_list = NULL;
	slurmdb_qos_cond_t qos_cond = {
		.with_deleted = 1,
	};
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};

	if (!(rc = db_query_list(errors, auth, &assoc_list,
				 slurmdb_associations_get, cond)) &&
	    !(rc = db_query_list(errors, auth, &tres_list, slurmdb_tres_get,
				 &tres_cond)) &&
	    !(rc = db_query_list(errors, auth, &qos_list, slurmdb_qos_get,
				 &qos_cond))) {
		ListIterator itr = list_iterator_create(assoc_list);
		data_t *dassocs = data_set_list(
			data_key_set(resp, "associations"));
		slurmdb_assoc_rec_t *assoc;
		parser_env_t penv = {
			.g_tres_list = tres_list,
			.g_qos_list = qos_list,
			.g_assoc_list = assoc_list,
		};

		while (!rc && (assoc = list_next(itr)))
			rc = dump(PARSE_ASSOC, assoc,
				  data_set_dict(data_list_append(dassocs)),
				  &penv);

		list_iterator_destroy(itr);
	}

	FREE_NULL_LIST(assoc_list);
	FREE_NULL_LIST(tres_list);
	FREE_NULL_LIST(qos_list);

	return rc;
}

/* based on sacctmgr_list_assoc() */
static int _dump_associations(const char *context_id,
			      http_request_method_t method, data_t *parameters,
			      data_t *query, int tag, data_t *resp,
			      void *auth, data_t *errors)
{
	int rc;
	slurmdb_assoc_cond_t assoc_cond = {
		.with_deleted = true,
	};

	rc = _dump_assoc_cond(resp, auth, errors, &assoc_cond);

	return rc;
}

static int _dump_association(data_t *resp, void *auth,
			     data_t *errors, char *account, char *cluster,
			     char *user, char *partition)
{
	int rc = SLURM_SUCCESS;
	slurmdb_assoc_cond_t *assoc_cond = xmalloc(sizeof(*assoc_cond));

	if (account) {
		assoc_cond->acct_list = list_create(NULL);
		list_append(assoc_cond->acct_list, account);
	}
	if (cluster) {
		assoc_cond->cluster_list = list_create(NULL);
		list_append(assoc_cond->cluster_list, cluster);
	}
	if (user) {
		assoc_cond->user_list = list_create(NULL);
		list_append(assoc_cond->user_list, user);
	}
	if (partition) {
		assoc_cond->partition_list = list_create(NULL);
		list_append(assoc_cond->partition_list, partition);
	}

	rc = _dump_assoc_cond(resp, auth, errors, assoc_cond);
	slurmdb_destroy_assoc_cond(assoc_cond);

	return rc;
}

static int _delete_assoc(data_t *resp, void *auth, data_t *errors,
			 char *account, char *cluster, char *user,
			 char *partition)
{
	int rc = SLURM_SUCCESS;
	List removed = NULL;
	slurmdb_assoc_cond_t assoc_cond = {
		.acct_list = list_create(NULL),
		.user_list = list_create(NULL),
	};

	list_append(assoc_cond.acct_list, account);
	if (cluster) {
		assoc_cond.cluster_list = list_create(NULL);
		list_append(assoc_cond.cluster_list, cluster);
	}
	list_append(assoc_cond.user_list, user);
	if (partition) {
		assoc_cond.partition_list = list_create(NULL);
		list_append(assoc_cond.partition_list, partition);
	}

	if (!(rc = db_query_list(errors, auth, &removed,
				 slurmdb_associations_remove, &assoc_cond)) &&
	    (list_for_each(removed, _foreach_delete_assoc,
			   data_set_list(data_key_set(
				   resp, "removed_associations"))) < 0))
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "unable to delete associations", NULL);

	if (!rc)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(removed);
	FREE_NULL_LIST(assoc_cond.acct_list);
	FREE_NULL_LIST(assoc_cond.cluster_list);
	FREE_NULL_LIST(assoc_cond.user_list);
	FREE_NULL_LIST(assoc_cond.partition_list);

	return rc;
}

#define MAGIC_FOREACH_UP_ASSOC 0xbaed2a12
typedef struct {
	int magic;
	List assoc_list;
	List tres_list;
	List qos_list;
	data_t *errors;
	rest_auth_context_t *auth;
} foreach_update_assoc_t;

static data_for_each_cmd_t _foreach_update_assoc(data_t *data, void *arg)
{
	foreach_update_assoc_t *args = arg;
	data_t *errors = args->errors;
	slurmdb_assoc_rec_t *assoc;
	parser_env_t penv = {
		.g_tres_list = args->tres_list,
		.g_qos_list = args->qos_list,
		.auth = args->auth,
	};

	xassert(args->magic == MAGIC_FOREACH_UP_ASSOC);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "Associations must be a list of dictionaries", NULL);
		return DATA_FOR_EACH_FAIL;
	}

	assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);

	list_append(args->assoc_list, assoc);

	if (parse(PARSE_ASSOC, assoc, data, args->errors, &penv))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _update_assocations(data_t *query, data_t *resp,
			       void *auth, bool commit)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};
	slurmdb_qos_cond_t qos_cond = {
		.with_deleted = 1,
	};
	foreach_update_assoc_t args = {
		.magic = MAGIC_FOREACH_UP_ASSOC,
		.auth = auth,
		.errors = errors,
		.assoc_list = list_create(slurmdb_destroy_assoc_rec),
	};
	data_t *dassoc = get_query_key_list("associations", errors, query);

	if (dassoc &&
	    !(rc = db_query_list(errors, auth, &args.tres_list,
				 slurmdb_tres_get, &tres_cond)) &&
	    !(rc = db_query_list(errors, auth, &args.qos_list, slurmdb_qos_get,
				 &qos_cond)) &&
	    (data_list_for_each(dassoc, _foreach_update_assoc, &args) < 0))
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc &&
	    !(rc = db_query_rc(errors, auth, args.assoc_list,
			       slurmdb_associations_add)) &&
	    commit)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(args.assoc_list);
	FREE_NULL_LIST(args.tres_list);

	return rc;
}

static int op_handler_association(const char *context_id,
				  http_request_method_t method,
				  data_t *parameters, data_t *query, int tag,
				  data_t *resp, void *auth)
{
	data_t *errors = populate_response_format(resp);
	char *user = NULL; /* optional */
	char *account = NULL; /* optional */
	char *cluster = NULL; /* optional */
	char *partition = NULL; /* optional */
	int rc = ESLURM_REST_INVALID_QUERY;

	if (!query)
		return resp_error(errors, ESLURM_REST_EMPTY_RESULT,
				  "query is missing", "HTTP query");

	(void)data_retrieve_dict_path_string(query, "partition", &partition);
	(void)data_retrieve_dict_path_string(query, "cluster", &cluster);
	(void)data_retrieve_dict_path_string(query, "user", &user);
	(void)data_retrieve_dict_path_string(query, "account", &account);

	if (method == HTTP_REQUEST_GET)
		rc = _dump_association(resp, auth, errors, account, cluster,
				       user, partition);
	else if (method == HTTP_REQUEST_DELETE)
		rc = _delete_assoc(resp, auth, errors, account, cluster, user,
				   partition);

	xfree(partition);
	xfree(cluster);
	xfree(user);
	xfree(account);

	return rc;
}

extern int op_handler_associations(const char *context_id,
				   http_request_method_t method,
				   data_t *parameters, data_t *query, int tag,
				   data_t *resp, void *auth)
{
	data_t *errors = populate_response_format(resp);

	if (method == HTTP_REQUEST_GET)
		return _dump_associations(context_id, method, parameters, query,
					  tag, resp, auth, errors);
	else if (method == HTTP_REQUEST_POST)
		return _update_assocations(query, resp, auth,
					   (tag != CONFIG_OP_TAG));

	return ESLURM_REST_INVALID_QUERY;
}

extern void init_op_associations(void)
{
	bind_operation_handler("/slurmdb/v0.0.37/associations/",
			       op_handler_associations, 0);
	bind_operation_handler("/slurmdb/v0.0.37/association/",
			       op_handler_association, 0);
}

extern void destroy_op_associations(void)
{
	unbind_operation_handler(op_handler_associations);
	unbind_operation_handler(op_handler_association);
}
