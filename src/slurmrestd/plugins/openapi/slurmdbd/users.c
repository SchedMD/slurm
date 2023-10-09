/*****************************************************************************\
 *  users.c - Slurm REST API acct user http operations handlers
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

#include "slurm/slurmdb.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/operations.h"
#include "api.h"

static void _dump_users(ctxt_t *ctxt, slurmdb_user_cond_t *user_cond)
{
	List user_list = NULL;

	if (!db_query_list(ctxt, &user_list, slurmdb_users_get, user_cond))
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_USERS_RESP, user_list, ctxt);

	FREE_NULL_LIST(user_list);
}

static int _foreach_wckey_set_def(void *x, void *key)
{
	slurmdb_wckey_rec_t *wckey = x;

	xassert(wckey->name && wckey->name[0]);

	wckey->is_def = (wckey == key);

	return SLURM_SUCCESS;
}

static int _match_wckey_name(void *x, void *key)
{
	slurmdb_wckey_rec_t *wckey = x;
	char *name = key;

	xassert(name && name[0]);
	xassert(wckey->name && wckey->name[0]);

	return !xstrcmp(wckey->name, name);
}

static int _foreach_update_user(void *x, void *arg)
{
	slurmdb_user_rec_t *user = x;
	ctxt_t *ctxt = arg;
	list_t *user_list = NULL;
	bool modify;

	slurmdb_assoc_cond_t assoc_cond = {};
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
		.with_deleted = true,
	};

	assoc_cond.user_list = list_create(NULL);

	if (user->old_name && !user->old_name[0]) {
		/*
		 * Ignore empty string since the client is not asking to change
		 * user from nothing.
		 */
		xfree(user->old_name);
	}

	if (user->old_name) {
		list_append(assoc_cond.user_list, user->old_name);

		if (db_query_list_xempty(ctxt, &user_list, slurmdb_users_get,
					 &user_cond))
			goto cleanup;

		if (!user_list || list_is_empty(user_list)) {
			resp_error(ctxt, ESLURM_USER_ID_MISSING, __func__,
			   "Unable to rename non-existant user %s to %s",
			   user->old_name, user->name);
			goto cleanup;
		}

		list_flush(assoc_cond.user_list);
		FREE_NULL_LIST(user_list);

		list_append(assoc_cond.user_list, user->name);

		if (db_query_list_xempty(ctxt, &user_list, slurmdb_users_get,
					 &user_cond))
			goto cleanup;

		if (user_list && !list_is_empty(user_list)) {
			resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_MODIFY, __func__,
			   "Unable to rename user %s to existing %s",
			   user->old_name, user->name);
			goto cleanup;
		}

		list_append(assoc_cond.user_list, user->old_name);
		modify = true;
	} else {
		list_append(assoc_cond.user_list, user->name);

		if (db_query_list_xempty(ctxt, &user_list, slurmdb_users_get,
					 &user_cond))
			goto cleanup;

		if (!user_list || list_is_empty(user_list)) {
			modify = false;
		} else if (list_count(user_list) > 1) {
			resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_MODIFY, __func__,
				   "ambiguous user modify request");
			goto cleanup;
		} else {
			modify = true;
		}
	}

	if (user->assoc_list && list_count(user->assoc_list)) {
		resp_warn(ctxt, __func__, "User %s associations list ignored. They must be set via the associations end point.",
			  user->name);
		FREE_NULL_LIST(user->assoc_list);
	}

	if (user->coord_accts && list_count(user->coord_accts)) {
		resp_warn(ctxt, __func__, "User %s coordinators list ignored. They must be set via the coordinators or accounts end point.",
			  user->name);
		FREE_NULL_LIST(user->coord_accts);
	}

	if (user->default_wckey && user->default_wckey[0]) {
		/*
		 * User may provide wckey as default but not in the list of
		 * wckeys. Automatically add it.
		 */
		slurmdb_wckey_rec_t *key = NULL;

		if (user->wckey_list) {
			key = list_find_first(user->wckey_list,
					      _match_wckey_name,
					      user->default_wckey);

			/* Avoid any conflicting defaults */
			list_for_each(user->wckey_list, _foreach_wckey_set_def,
				      key);
		}

		if (!key) {
			if (!user->wckey_list)
				user->wckey_list =
					list_create(slurmdb_destroy_wckey_rec);

			key = xmalloc(sizeof(*key));
			slurmdb_init_wckey_rec(key, false);
			key->name = xstrdup(user->default_wckey);
			key->user = xstrdup(user->name);
			key->cluster = xstrdup(slurm_conf.cluster_name);
			key->is_def = true;

			list_append(user->wckey_list, key);
		}
	}

	if (user->flags & SLURMDB_USER_FLAG_DELETED) {
		resp_warn(ctxt, __func__,
			  "Ignoring request to set flag: DELETED");
		user->flags &= ~SLURMDB_USER_FLAG_DELETED;
	}

	if (!modify) {
		debug("%s: [%s] add user request: user=%s",
		      __func__, ctxt->id, user->name);

		if (!user_list)
			user_list = list_create(NULL);
		list_append(user_list, user);

		db_query_rc(ctxt, user_list, slurmdb_users_add);
	} else {
		debug("%s: [%s] modifying user request: user=%s%s%s",
		      __func__,
		      ctxt->id, (user->old_name ? user->old_name : ""),
		      (user->old_name ? "->" : ""), user->name);

		db_modify_rc(ctxt, &user_cond, user, slurmdb_users_modify);
	}

cleanup:
	FREE_NULL_LIST(assoc_cond.user_list);
	FREE_NULL_LIST(user_list);
	return ctxt->rc ? SLURM_ERROR : SLURM_SUCCESS;
}

extern int update_users(ctxt_t *ctxt, bool commit, list_t *user_list)
{
	if (!(list_for_each(user_list, _foreach_update_user, ctxt) < 0) &&
	    !ctxt->rc && commit)
		db_query_commit(ctxt);

	return ctxt->rc;
}

static void _add_users_association(ctxt_t *ctxt,
				   slurmdb_add_assoc_cond_t *add_assoc,
				   slurmdb_user_rec_t *user)
{
	int rc = SLURM_SUCCESS;
	char *ret_str = NULL;

	/* Mimick sacctmgr/user_functions.c _set_add_cond() */
	if (user->default_acct) {
		if (!add_assoc->acct_list)
			add_assoc->acct_list = list_create(xfree_ptr);

		if (list_is_empty(add_assoc->acct_list))
			slurm_addto_char_list(add_assoc->acct_list,
					      user->default_acct);
	}

	/* Mimick sacctmgr/user_functions.c _set_add_cond() */
	if (user->default_wckey) {
		if (!add_assoc->wckey_list)
			add_assoc->wckey_list = list_create(xfree_ptr);

		if (list_is_empty(add_assoc->wckey_list))
			slurm_addto_char_list(add_assoc->wckey_list,
					      user->default_wckey);
	}

	/*
	 * Mimick sacctmgr/user_functions.c sacctmgr_add_user().
	 *
	 * The add_assoc.user_list is a requirement by itself, already handled
	 * by the add_parse_req() in the USERS_ADD_COND parser array.
	 *
	 * But add_assoc.[acct|wckey]_list are required at least one or the
	 * other. Since there's no way to do it at present via data_parser
	 * functionality, checking the requirement here, mimicking sacctmgr.
	 *
	 * These checks should arguably happen inside the API call.
	 */
	if (!list_count(add_assoc->acct_list) &&
	    !list_count(add_assoc->wckey_list)) {
		rc = ESLURM_DATA_AMBIGUOUS_QUERY;
		resp_error(ctxt, rc, __func__,
			   "Need name of account(s) or wckey(s) to add");
		goto cleanup;
	}

	/*
	 * sacctmgr performs a uid_to_string() check on the requested user list
	 * and emits a warning letting user decide to proceed if any fails,
	 * but there's no such commit check mechanism for rest POST.
	 */

	errno = 0;
	ret_str = slurmdb_users_add_cond(ctxt->db_conn, add_assoc, user);

	if ((rc = errno))
		resp_error(ctxt, rc, __func__,
			   "slurmdb_users_add_cond() failed");
	else
		db_query_commit(ctxt);

cleanup:
	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_USERS_ADD_COND_RESP_STR, ret_str,
				 ctxt);
	xfree(ret_str);
}

static void _update_users(ctxt_t *ctxt)
{
	openapi_resp_single_t resp = {0};
	openapi_resp_single_t *resp_ptr = &resp;

	if (!DATA_PARSE(ctxt->parser, OPENAPI_USERS_RESP, resp, ctxt->query,
			ctxt->parent_path)) {
		list_t *user_list = resp.response;
		update_users(ctxt, true, user_list);
		FREE_NULL_LIST(user_list);
	}

	FREE_OPENAPI_RESP_COMMON_CONTENTS(resp_ptr);
}

static void _delete_user(ctxt_t *ctxt, char *user_name)
{
	slurmdb_assoc_cond_t assoc_cond = {0};
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
	};
	List user_list = NULL;

	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, user_name);

	if (!db_query_list(ctxt, &user_list, slurmdb_users_remove, &user_cond))
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_USERS_REMOVED_RESP, user_list,
					 ctxt);

	if (!ctxt->rc)
		db_query_commit(ctxt);

	FREE_NULL_LIST(user_list);
	FREE_NULL_LIST(assoc_cond.user_list);
}

static void _parse_add_users_assoc(ctxt_t *ctxt)
{
	openapi_resp_users_add_cond_t resp = {0};
	openapi_resp_users_add_cond_t *resp_ptr = &resp;

	if (!DATA_PARSE(ctxt->parser, OPENAPI_USERS_ADD_COND_RESP, resp,
			ctxt->query, ctxt->parent_path)) {
		_add_users_association(ctxt, resp_ptr->add_assoc,
				       resp_ptr->user);
		slurmdb_destroy_add_assoc_cond(resp_ptr->add_assoc);
		slurmdb_destroy_user_rec(resp_ptr->user);
	}

	FREE_OPENAPI_RESP_COMMON_CONTENTS(resp_ptr);
}

/* based on sacctmgr_list_user() */
static int _op_handler_users(ctxt_t *ctxt)
{
	if (ctxt->method == HTTP_REQUEST_GET) {
		slurmdb_user_cond_t *user_cond = NULL;

		if (!DATA_PARSE(ctxt->parser, USER_CONDITION_PTR, user_cond,
				ctxt->query, ctxt->parent_path))
			_dump_users(ctxt, user_cond);

		slurmdb_destroy_user_cond(user_cond);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		_update_users(ctxt);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

	return SLURM_SUCCESS;
}

static int _op_handler_user(ctxt_t *ctxt)
{
	openapi_user_param_t params = {0};

	if (DATA_PARSE(ctxt->parser, OPENAPI_USER_PARAM, params,
		       ctxt->parameters, ctxt->parent_path))
		goto cleanup;

	if (!params.name || !params.name[0]) {
		resp_error(ctxt, ESLURM_USER_ID_MISSING, __func__,
			   "User name must be provided for singular query");
	} else if (ctxt->method == HTTP_REQUEST_GET) {
		openapi_user_query_t query = {0};
		slurmdb_assoc_cond_t assoc_cond = {0};
		slurmdb_user_cond_t user_cond = {
			.assoc_cond = &assoc_cond,
		};

		if (DATA_PARSE(ctxt->parser, OPENAPI_USER_QUERY, query,
			       ctxt->query, ctxt->parent_path))
			goto cleanup;

		user_cond.with_deleted = query.with_deleted;
		user_cond.with_assocs = query.with_assocs;
		user_cond.with_coords = query.with_coords;
		user_cond.with_wckeys = query.with_wckeys;

		assoc_cond.user_list = list_create(NULL);
		list_append(assoc_cond.user_list, params.name);

		_dump_users(ctxt, &user_cond);

		FREE_NULL_LIST(assoc_cond.user_list);
	} else if (ctxt->method == HTTP_REQUEST_DELETE) {
		_delete_user(ctxt, params.name);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	xfree(params.name);
	return SLURM_SUCCESS;
}

static int _op_handler_users_association(ctxt_t *ctxt)
{
	if (ctxt->method == HTTP_REQUEST_POST)
		_parse_add_users_assoc(ctxt);
	else
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));

	return SLURM_SUCCESS;
}

extern void init_op_users(void)
{
	bind_handler("/slurmdb/{data_parser}/users_association/",
		     _op_handler_users_association, 0);
	bind_handler("/slurmdb/{data_parser}/users/", _op_handler_users, 0);
	bind_handler("/slurmdb/{data_parser}/user/{name}", _op_handler_user, 0);
}

extern void destroy_op_users(void)
{
	unbind_operation_ctxt_handler(_op_handler_users_association);
	unbind_operation_ctxt_handler(_op_handler_users);
	unbind_operation_ctxt_handler(_op_handler_user);
}
