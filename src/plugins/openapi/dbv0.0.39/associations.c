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

#define FOREACH_ASSOC_MAGIC 0x13113114

typedef struct {
	size_t offset;
	char *parameter;
} assoc_parameter_t;

typedef struct {
	int magic; /* FOREACH_ASSOC_MAGIC */
	ctxt_t *ctxt;
	data_t *dassocs;
} foreach_assoc_t;

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

static int _populate_assoc_cond(ctxt_t *ctxt, slurmdb_assoc_cond_t *assoc_cond)
{
	if (!ctxt->query)
		return SLURM_SUCCESS;

	for (int i = 0; i < ARRAY_SIZE(assoc_parameters); i++) {
		char *value = NULL;
		const assoc_parameter_t *ap = &assoc_parameters[i];
		List *list = ((void *) assoc_cond) + ap->offset;
		int rc = data_retrieve_dict_path_string(ctxt->query,
							ap->parameter, &value);

		if (rc == ESLURM_DATA_PATH_NOT_FOUND) {
			/* parameter not in query */
			continue;
		} else if (rc) {
			return resp_error(ctxt, rc, __func__,
					"Invalid format for query parameter %s",
					ap->parameter);
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

static int _foreach_assoc(void *x, void *arg)
{
	int rc;
	slurmdb_assoc_rec_t *assoc = x;
	foreach_assoc_t *args = arg;

	xassert(args->magic == FOREACH_ASSOC_MAGIC);
	xassert(args->ctxt->magic == MAGIC_CTXT);

	if ((rc = DATA_DUMP(args->ctxt->parser, ASSOC, *assoc,
			    data_list_append(args->dassocs)))) {
		resp_error(args->ctxt, rc, __func__,
			   "Unable to dump association id#%u account=%s cluster=%s partition=%s user=%s",
			   assoc->id, assoc->acct, assoc->cluster,
			   assoc->partition, assoc->user);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void _dump_assoc_cond(ctxt_t *ctxt, slurmdb_assoc_cond_t *cond,
			     bool only_one)
{
	List assoc_list = NULL;
	foreach_assoc_t args = {
		.magic = FOREACH_ASSOC_MAGIC,
		.ctxt = ctxt,
	};

	xassert(!data_key_get(ctxt->resp, "associations"));

	if (db_query_list(ctxt, &assoc_list, slurmdb_associations_get, cond))
		goto cleanup;

	xassert(!data_key_get(ctxt->resp, "associations"));
	args.dassocs = data_set_list(data_key_set(ctxt->resp, "associations"));

	if (only_one && (list_count(assoc_list) > 1)) {
		resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_QUERY, __func__,
			   "Ambiguous request: More than 1 association would have been dumped.");
		goto cleanup;
	}

	if (assoc_list)
		list_for_each(assoc_list, _foreach_assoc, &args);

cleanup:
	FREE_NULL_LIST(assoc_list);
}

static void _delete_assoc(ctxt_t *ctxt, slurmdb_assoc_cond_t *assoc_cond,
			  bool only_one)
{
	int rc = SLURM_SUCCESS;
	List removed = NULL;
	data_t *drem =
		data_set_list(data_key_set(ctxt->resp, "removed_associations"));

	rc = db_query_list(ctxt, &removed, slurmdb_associations_remove,
			   assoc_cond);
	if (rc) {
		resp_error(ctxt, rc, __func__, "remove associations failed");
	} else if (only_one && list_count(removed) > 1) {
		resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_MODIFY, __func__,
				"ambiguous request: More than 1 association would have been deleted.");
	} else if (list_for_each(removed, _foreach_delete_assoc, drem) < 0) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "unable to list deleted associations");
	} else if (!rc) {
		db_query_commit(ctxt);
	}

	FREE_NULL_LIST(removed);
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
		slurmdb_tres_rec_t *m = NULL;

		if (mod_list)
			m = list_find_first(mod_list, slurmdb_find_tres_in_list,
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
	if (mod_list) {
		itr = list_iterator_create(mod_list);
		while ((tres = list_next(itr))) {
			slurmdb_tres_rec_t *d = NULL;

			if (dst_list)
				d = list_find_first(dst_list,
						    slurmdb_find_tres_in_list,
						    &tres->id);

			if (!d) {
				list_append(dst_list,
					    slurmdb_copy_tres_rec(tres));
			} else {
				xassert(tres->count == d->count);
			}
		}
		list_iterator_destroy(itr);
	}

	*dst = slurmdb_make_tres_string(dst_list, TRES_STR_FLAG_SIMPLE);

	FREE_NULL_LIST(dst_list);
	FREE_NULL_LIST(mod_list);
}

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

static int _foreach_update_assoc(void *x, void *arg)
{
	int rc;
	ctxt_t *ctxt = arg;
	slurmdb_assoc_rec_t *assoc = x;
	slurmdb_assoc_cond_t cond = {
		/* all values are symlinks so don't double free */
		.acct_list = list_create(NULL),
		.cluster_list = list_create(NULL),
		.partition_list = list_create(NULL),
		.user_list = list_create(NULL),
	};
	List assoc_list = NULL;

	xassert(ctxt->magic == MAGIC_CTXT);

	if (assoc->parent_acct && !assoc->parent_acct[0])
		xfree(assoc->parent_acct);

	/*
	 * slurmdbd will treat an empty list as a wildcard so we must place
	 * empty string values to for unset fields
	 */
	list_append(cond.acct_list, (assoc->acct ? assoc->acct : ""));
	list_append(cond.cluster_list, (assoc->cluster ? assoc->cluster : ""));
	list_append(cond.partition_list,
		    (assoc->partition ? assoc->partition : ""));
	list_append(cond.user_list, (assoc->user ? assoc->user : ""));

	/* first query is an existence check and we don't care about errors */
	if ((rc = db_query_list_xempty(ctxt, &assoc_list,
				       slurmdb_associations_get, &cond)) ||
	    !assoc_list || list_is_empty(assoc_list)) {
		debug("%s: [%s] adding association request: acct=%s cluster=%s partition=%s user=%s existence_check[%d]:%s",
		      __func__, ctxt->id, assoc->acct, assoc->cluster,
		      assoc->partition, assoc->user, rc, slurm_strerror(rc));

		FREE_NULL_LIST(assoc_list);
		/* avoid double free of assoc */
		assoc_list = list_create(NULL);
		list_append(assoc_list, assoc);
		(void) db_query_rc(ctxt, assoc_list, slurmdb_associations_add);
	} else if (list_count(assoc_list) > 1) {
		rc = resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_MODIFY, __func__,
				"ambiguous association modify request");
	} else {
		slurmdb_assoc_rec_t *diff_assoc;

		debug("%s: [%s] modifying association request: acct=%s cluster=%s partition=%s user=%s",
		      __func__, ctxt->id, assoc->acct, assoc->cluster,
		      assoc->partition, assoc->user);

		/*
		 * slurmdb requires that the modify request will be a list of
		 * diffs instead of the final state of the assoc unlike add
		 */
		diff_assoc = _diff_assoc(list_pop(assoc_list), assoc);

		rc = db_modify_rc(ctxt, &cond, diff_assoc,
				  slurmdb_associations_modify);

		slurmdb_destroy_assoc_rec(diff_assoc);
	}

	FREE_NULL_LIST(assoc_list);
	FREE_NULL_LIST(cond.acct_list);
	FREE_NULL_LIST(cond.cluster_list);
	FREE_NULL_LIST(cond.partition_list);
	FREE_NULL_LIST(cond.user_list);

	return rc ? SLURM_ERROR : SLURM_SUCCESS;
}

static void _update_associations(ctxt_t *ctxt, bool commit)
{
	data_t *parent_path = NULL;
	data_t *dassoc = get_query_key_list("associations", ctxt, &parent_path);
	List assoc_list = NULL;

	if (!dassoc) {
		resp_warn(ctxt, __func__,
			  "ignoring empty or non-existant associations array");
		goto cleanup;
	}

	if (DATA_PARSE(ctxt->parser, ASSOC_LIST, assoc_list, dassoc,
		       parent_path))
		goto cleanup;

	if (list_for_each(assoc_list, _foreach_update_assoc, ctxt) < 0)
		goto cleanup;

	if (!ctxt->rc && commit)
		db_query_commit(ctxt);

cleanup:
	FREE_NULL_LIST(assoc_list);
	FREE_NULL_DATA(parent_path);
}

static int op_handler_association(const char *context_id,
				  http_request_method_t method,
				  data_t *parameters, data_t *query, int tag,
				  data_t *resp, void *auth)
{
	slurmdb_assoc_cond_t *assoc_cond = xmalloc(sizeof(*assoc_cond));
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc || _populate_assoc_cond(ctxt, assoc_cond))
		/* no-op - already logged */;
	else if (method == HTTP_REQUEST_GET)
		_dump_assoc_cond(ctxt, assoc_cond, true);
	else if (method == HTTP_REQUEST_DELETE)
		_delete_assoc(ctxt, assoc_cond, true);
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	slurmdb_destroy_assoc_cond(assoc_cond);
	return fini_connection(ctxt);
}

extern int op_handler_associations(const char *context_id,
				   http_request_method_t method,
				   data_t *parameters, data_t *query, int tag,
				   data_t *resp, void *auth)
{
	slurmdb_assoc_cond_t *assoc_cond = xmalloc(sizeof(*assoc_cond));
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc || _populate_assoc_cond(ctxt, assoc_cond))
		/* no-op - already logged */;
	else if (method == HTTP_REQUEST_GET)
		_dump_assoc_cond(ctxt, assoc_cond, false);
	else if (method == HTTP_REQUEST_POST)
		_update_associations(ctxt, (tag != CONFIG_OP_TAG));
	else if (method == HTTP_REQUEST_DELETE)
		_delete_assoc(ctxt, assoc_cond, false);
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	slurmdb_destroy_assoc_cond(assoc_cond);
	return fini_connection(ctxt);
}

extern void init_op_associations(void)
{
	bind_operation_handler("/slurmdb/v0.0.39/associations/",
			       op_handler_associations, 0);
	bind_operation_handler("/slurmdb/v0.0.39/association/",
			       op_handler_association, 0);
}

extern void destroy_op_associations(void)
{
	unbind_operation_handler(op_handler_associations);
	unbind_operation_handler(op_handler_association);
}
