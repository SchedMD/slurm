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

#include "src/plugins/openapi/dbv0.0.38/api.h"

typedef struct {
	size_t offset;
	char *parameter;
} assoc_parameter_t;

static const assoc_parameter_t assoc_parameters[] = {
	{
		offsetof(slurmdb_assoc_cond_t, partition_list),
		"partition"
	},
	{
		offsetof(slurmdb_assoc_cond_t, cluster_list),
		"cluster"
	},
	{
		offsetof(slurmdb_assoc_cond_t, acct_list),
		"account"
	},
	{
		offsetof(slurmdb_assoc_cond_t, user_list),
		"user"
	},
};

static int _populate_assoc_cond(data_t *errors, data_t *query,
				slurmdb_assoc_cond_t *assoc_cond)
{
	if (!query)
		return SLURM_SUCCESS;

	for (int i = 0; i < ARRAY_SIZE(assoc_parameters); i++) {
		char *value = NULL;
		const assoc_parameter_t *ap = &assoc_parameters[i];
		List *list = ((void *) assoc_cond) + ap->offset;
		int rc = data_retrieve_dict_path_string(query, ap->parameter,
							&value);

		if (rc == ESLURM_DATA_PATH_NOT_FOUND) {
			/* parameter not in query */
			continue;
		} else if (rc) {
			char *err = xstrdup_printf("Invalid format for query parameter %s",
						   ap->parameter);
			rc = resp_error(errors, rc, err, "HTTP query");
			xfree(err);
			return rc;
		}

		*list = list_create(xfree_ptr);
		(void) slurm_addto_char_list(*list, value);

		xfree(value);
	}

	return SLURM_SUCCESS;
}

static int _foreach_delete_assoc(void *x, void *arg)
{
	char *assoc = x;
	data_t *assocs = arg;

	data_set_string(data_list_append(assocs), assoc);

	return DATA_FOR_EACH_CONT;
}

static int _dump_assoc_cond(data_t *resp, void *auth, data_t *errors,
			    slurmdb_assoc_cond_t *cond, bool only_one)
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

		if (only_one && list_count(assoc_list) > 1) {
			rc = resp_error(
				errors, ESLURM_REST_INVALID_QUERY,
				"Ambiguous request: More than 1 association would have been dumped.",
				NULL);
		}

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

static int _delete_assoc(data_t *resp, void *auth, data_t *errors,
			 slurmdb_assoc_cond_t *assoc_cond, bool only_one)
{
	int rc = SLURM_SUCCESS;
	List removed = NULL;
	data_t *drem = data_set_list(data_key_set(resp, "removed_associations"));

	rc = db_query_list(errors, auth, &removed, slurmdb_associations_remove,
			   assoc_cond);
	if (rc) {
		(void) resp_error(errors, rc, "unable to query associations",
				NULL);
	} else if (only_one && list_count(removed) > 1) {
		rc = resp_error(errors, ESLURM_REST_INVALID_QUERY,
				"ambiguous request: More than 1 association would have been deleted.",
				NULL);
	} else if (list_for_each(removed, _foreach_delete_assoc, drem) < 0) {
		rc = resp_error(errors, ESLURM_REST_INVALID_QUERY,
				"unable to delete associations", NULL);
	} else if (!rc) {
		rc = db_query_commit(errors, auth);
	}

	FREE_NULL_LIST(removed);

	return rc;
}

#define MAGIC_FOREACH_UP_ASSOC 0xbaed2a12
typedef struct {
	int magic;
	List tres_list;
	List qos_list;
	data_t *errors;
	rest_auth_context_t *auth;
} foreach_update_assoc_t;

static data_for_each_cmd_t _foreach_update_assoc(data_t *data, void *arg)
{
	foreach_update_assoc_t *args = arg;
	data_t *errors = args->errors;
	slurmdb_assoc_rec_t *assoc = NULL;
	parser_env_t penv = {
		.g_tres_list = args->tres_list,
		.g_qos_list = args->qos_list,
		.auth = args->auth,
	};
	int rc;
	List assoc_list = NULL;
	slurmdb_assoc_cond_t cond = {0};
	data_t *query_errors = data_new();

	xassert(args->magic == MAGIC_FOREACH_UP_ASSOC);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "Associations must be a list of dictionaries", NULL);
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);

	if (parse(PARSE_ASSOC, assoc, data, args->errors, &penv)) {
		rc = DATA_FOR_EACH_FAIL;
		goto cleanup;
	}

	cond.acct_list = list_create(NULL);
	cond.cluster_list = list_create(NULL);
	cond.partition_list = list_create(NULL);
	cond.user_list = list_create(NULL);

	if (assoc->acct)
		list_append(cond.acct_list, assoc->acct);
	else
		list_append(cond.acct_list, "");

	if (assoc->cluster)
		list_append(cond.cluster_list, assoc->cluster);
	else
		list_append(cond.cluster_list, "");

	if (assoc->partition)
		list_append(cond.partition_list, assoc->partition);
	else
		list_append(cond.partition_list, "");

	if (assoc->user)
		list_append(cond.user_list, assoc->user);
	else
		list_append(cond.user_list, "");

	if ((rc = db_query_list(query_errors, args->auth, &assoc_list,
				 slurmdb_associations_get, &cond)) ||
	    list_is_empty(assoc_list)) {
		FREE_NULL_LIST(assoc_list);
		assoc_list = list_create(slurmdb_destroy_assoc_rec);
		list_append(assoc_list, assoc);

		debug("%s: adding association request: acct=%s cluster=%s partition=%s user=%s",
		      __func__, assoc->acct, assoc->cluster, assoc->partition,
		      assoc->user);

		assoc = NULL;
		rc = db_query_rc(errors, args->auth, assoc_list,
				 slurmdb_associations_add);
	} else if (list_count(assoc_list) > 1) {
		rc = resp_error(errors, ESLURM_REST_INVALID_QUERY,
				"ambiguous modify request",
				"slurmdb_associations_get");
	} else {
		debug("%s: modifying association request: acct=%s cluster=%s partition=%s user=%s",
		      __func__, assoc->acct, assoc->cluster, assoc->partition,
		      assoc->user);

		rc = db_modify_rc(errors, args->auth, &cond, assoc,
				  slurmdb_associations_modify);
	}

cleanup:

	FREE_NULL_LIST(assoc_list);
	FREE_NULL_LIST(cond.acct_list);
	FREE_NULL_LIST(cond.cluster_list);
	FREE_NULL_LIST(cond.partition_list);
	FREE_NULL_LIST(cond.user_list);
	FREE_NULL_DATA(query_errors);
	slurmdb_destroy_assoc_rec(assoc);

	return rc ? DATA_FOR_EACH_FAIL : DATA_FOR_EACH_CONT;
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
	};
	data_t *dassoc = get_query_key_list("associations", errors, query);

	if (dassoc &&
	    !(rc = db_query_list(errors, auth, &args.tres_list,
				 slurmdb_tres_get, &tres_cond)) &&
	    !(rc = db_query_list(errors, auth, &args.qos_list, slurmdb_qos_get,
				 &qos_cond)) &&
	    (data_list_for_each(dassoc, _foreach_update_assoc, &args) < 0))
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc && commit)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(args.tres_list);
	FREE_NULL_LIST(args.qos_list);

	return rc;
}

static int op_handler_association(const char *context_id,
				  http_request_method_t method,
				  data_t *parameters, data_t *query, int tag,
				  data_t *resp, void *auth)
{
	int rc;
	data_t *errors = populate_response_format(resp);
	slurmdb_assoc_cond_t *assoc_cond = xmalloc(sizeof(*assoc_cond));

	if ((rc = _populate_assoc_cond(errors, query, assoc_cond)))
		/* no-op - already logged */;
	if (method == HTTP_REQUEST_GET)
		rc = _dump_assoc_cond(resp, auth, errors, assoc_cond, true);
	else if (method == HTTP_REQUEST_DELETE)
		rc = _delete_assoc(resp, auth, errors, assoc_cond, true);

	slurmdb_destroy_assoc_cond(assoc_cond);
	return rc;
}

extern int op_handler_associations(const char *context_id,
				   http_request_method_t method,
				   data_t *parameters, data_t *query, int tag,
				   data_t *resp, void *auth)
{
	int rc;
	data_t *errors = populate_response_format(resp);
	slurmdb_assoc_cond_t *assoc_cond = xmalloc(sizeof(*assoc_cond));

	if ((rc = _populate_assoc_cond(errors, query, assoc_cond)))
		/* no-op - already logged */;
	if (method == HTTP_REQUEST_GET)
		rc = _dump_assoc_cond(resp, auth, errors, assoc_cond, false);
	else if (method == HTTP_REQUEST_POST)
		rc = _update_assocations(query, resp, auth,
					 (tag != CONFIG_OP_TAG));
	else if (method == HTTP_REQUEST_DELETE)
		rc = _delete_assoc(resp, auth, errors, assoc_cond, false);

	slurmdb_destroy_assoc_cond(assoc_cond);
	return rc;
}

extern void init_op_associations(void)
{
	bind_operation_handler("/slurmdb/v0.0.38/associations/",
			       op_handler_associations, 0);
	bind_operation_handler("/slurmdb/v0.0.38/association/",
			       op_handler_association, 0);
}

extern void destroy_op_associations(void)
{
	unbind_operation_handler(op_handler_associations);
	unbind_operation_handler(op_handler_association);
}
