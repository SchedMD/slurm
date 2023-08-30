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

/* Turn *dst into a TRES string that will turn submitted *dst to match mod */
static void _diff_tres(char **dst, char *mod)
{
	ListIterator itr;
	List dst_list = NULL;
	List mod_list = NULL;
	slurmdb_tres_rec_t *tres;

	if (!*dst || !*dst[0]) {
		/* direct assignment when dst is empty */
		xfree(*dst);
		*dst = xstrdup(mod);
		return;
	}

	slurmdb_tres_list_from_string(&dst_list, *dst, TRES_STR_FLAG_REPLACE);
	xfree(*dst);
	slurmdb_tres_list_from_string(&mod_list, mod, TRES_STR_FLAG_REPLACE);

	/* find all removed or tres with updated counts */
	itr = list_iterator_create(dst_list);
	while ((tres = list_next(itr))) {
		slurmdb_tres_rec_t *m  =
			list_find_first(mod_list, slurmdb_find_tres_in_list,
					&tres->id);

		if (!m) {
			/* mark TRES for removal in slurmdbd */
			tres->count = -1;
		} else {
			tres->count = m->count;
		}
	}
	list_iterator_destroy(itr);

	/* add any new tres */
	itr = list_iterator_create(mod_list);
	while ((tres = list_next(itr))) {
		slurmdb_tres_rec_t *d  =
			list_find_first(dst_list, slurmdb_find_tres_in_list,
					&tres->id);

		if (!d) {
			list_append(dst_list, slurmdb_copy_tres_rec(tres));
		} else {
			xassert(tres->count == d->count);
		}
	}
	list_iterator_destroy(itr);

	*dst = slurmdb_make_tres_string(dst_list, TRES_STR_FLAG_SIMPLE);

	FREE_NULL_LIST(dst_list);
	FREE_NULL_LIST(mod_list);
}

#if SLURM_VERSION_NUMBER < SLURM_VERSION_NUM(23, 2, 0)
// TODO: remove in 23.02 as already defined in macros.h
#define SWAP(x, y)              \
do {                            \
	__typeof__(x) b = x;    \
	x = y;                  \
	y = b;                  \
} while (0)
#endif

/*
 * Create a diff of the current association and the requested destination state.
 *
 * Feed that diff to slurmdbd to get the dst state.
 */
static slurmdb_assoc_rec_t *_diff_assoc(slurmdb_assoc_rec_t *assoc,
					slurmdb_assoc_rec_t *dst)
{
	if (dst->accounting_list)
		SWAP(assoc->accounting_list, dst->accounting_list);
	if (dst->acct)
		SWAP(assoc->acct, dst->acct);

	/* skip assoc_next */
	/* skip assoc_next_id */
	/* skip bf_usage */

	if (dst->cluster)
		SWAP(assoc->cluster, dst->cluster);

	assoc->def_qos_id = dst->def_qos_id;

	/* skip flags */

	assoc->grp_jobs = dst->grp_jobs;
	assoc->grp_jobs_accrue = dst->grp_jobs_accrue;
	assoc->grp_submit_jobs = dst->grp_submit_jobs;

	_diff_tres(&assoc->grp_tres, dst->grp_tres);

	/* skip grp_tres_ctld */

	_diff_tres(&assoc->grp_tres_mins, dst->grp_tres_mins);

	/* skip grp_tres_mins_ctld */

	_diff_tres(&assoc->grp_tres_run_mins, dst->grp_tres_run_mins);

	/* skip grp_tres_run_mins_ctld */

	assoc->grp_wall = dst->grp_wall;

	/* skip id */

	assoc->is_def = dst->is_def;


	/* skip leaf_usage */
	/* skip lft */

	assoc->max_jobs = dst->max_jobs;
	assoc->max_jobs_accrue = dst->max_jobs_accrue;
	assoc->max_submit_jobs = dst->max_submit_jobs;

	_diff_tres(&assoc->max_tres_mins_pj, dst->max_tres_mins_pj);

	/* skip max_tres_mins_ctld */

	_diff_tres(&assoc->max_tres_run_mins, dst->max_tres_run_mins);

	/* skip max_tres_run_mins_ctld */

	_diff_tres(&assoc->max_tres_pj, dst->max_tres_pj);

	/* skip max_tres_ctld */

	_diff_tres(&assoc->max_tres_pn, dst->max_tres_pn);

	/* skip max_tres_pn_ctld */

	assoc->max_wall_pj = dst->max_wall_pj;
	assoc->min_prio_thresh = dst->min_prio_thresh;

	if (dst->parent_acct)
		SWAP(assoc->parent_acct, dst->parent_acct);

	/* skip parent_id */

	if (dst->partition)
		SWAP(assoc->partition, dst->partition);

	assoc->priority = dst->priority;

	if (dst->qos_list)
		SWAP(assoc->qos_list, dst->qos_list);

	/* skip rgt */

	assoc->shares_raw = dst->shares_raw;

	/* skip uid */
	/* skip usage */

	if (dst->user)
		SWAP(assoc->user, dst->user);

	/* skip user_rec */

	return assoc;
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
	data_t *query_errors = data_set_list(data_new());

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
		slurmdb_assoc_rec_t *diff_assoc;

		debug("%s: modifying association request: acct=%s cluster=%s partition=%s user=%s",
		      __func__, assoc->acct, assoc->cluster, assoc->partition,
		      assoc->user);

		/*
		 * slurmdb requires that the modify request will be a list of
		 * diffs instead of the final state of the assoc unlike add
		 */
		diff_assoc = _diff_assoc(list_pop(assoc_list), assoc);

		rc = db_modify_rc(errors, args->auth, &cond, diff_assoc,
				  slurmdb_associations_modify);

		slurmdb_destroy_assoc_rec(diff_assoc);
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
