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

#include "config.h"

#include <stdint.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
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

#define MAGIC_QUERY_SEARCH 0x9e8dbee1

typedef struct {
	int magic; /* MAGIC_QUERY_SEARCH */
	ctxt_t *ctxt;
	slurmdb_user_cond_t *user_cond;
} foreach_query_search_t;

static data_for_each_cmd_t _foreach_query_search(const char *key, data_t *data,
						 void *arg)
{
	foreach_query_search_t *args = arg;

	xassert(args->magic == MAGIC_QUERY_SEARCH);
	xassert(args->ctxt->magic == MAGIC_CTXT);

	if (!xstrcasecmp("with_deleted", key)) {
		if (data_convert_type(data, DATA_TYPE_BOOL) != DATA_TYPE_BOOL) {
			resp_error(args->ctxt, ESLURM_REST_INVALID_QUERY, key,
				   "%s must be a Boolean instead of %s", key,
				   data_type_to_string(data_get_type(data)));
			return DATA_FOR_EACH_FAIL;
		}

		if (data->data.bool_u)
			args->user_cond->with_deleted = true;
		else
			args->user_cond->with_deleted = false;

		return DATA_FOR_EACH_CONT;
	}

	resp_error(args->ctxt, ESLURM_REST_INVALID_QUERY, key,
		   "Unknown query field %s", key);
	return DATA_FOR_EACH_FAIL;
}

static void _dump_users(ctxt_t *ctxt, char *user_name,
			slurmdb_user_cond_t *user_cond)
{
	data_t *dusers;
	List user_list = NULL;
	slurmdb_assoc_cond_t assoc_cond = { 0 };

	dusers = data_key_set(ctxt->resp, "users");

	user_cond->assoc_cond = &assoc_cond;
	user_cond->with_assocs = true;
	user_cond->with_coords = true;
	/* with_deleted defaults to false */
	user_cond->with_wckeys = true;

	if (user_name) {
		assoc_cond.user_list = list_create(NULL);
		list_append(assoc_cond.user_list, user_name);
	}

	if (!db_query_list(ctxt, &user_list, slurmdb_users_get, user_cond))
		DATA_DUMP(ctxt->parser, USER_LIST, user_list, dusers);

	FREE_NULL_LIST(user_list);
	FREE_NULL_LIST(assoc_cond.user_list);
	user_cond->assoc_cond = NULL;
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
	};

	assoc_cond.user_list = list_create(NULL);

	if (user->old_name && !user->old_name[0]) {
		/*
		 * Ignore empty string since they client is not asking to change
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

		if (user->wckey_list)
			key = list_find_first(user->wckey_list,
					      _match_wckey_name,
					      user->default_wckey);

		if (!key) {
			if (!user->wckey_list)
				user->wckey_list =
					list_create(slurmdb_destroy_wckey_rec);

			key = xmalloc(sizeof(*key));
			slurmdb_init_wckey_rec(key, false);
			key->name = xstrdup(user->default_wckey);
			key->user = xstrdup(user->name);
			key->cluster = xstrdup(slurm_conf.cluster_name);

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

static void _update_users(ctxt_t *ctxt, bool commit)
{
	data_t *parent_path = NULL;
	data_t *dusers = get_query_key_list("users", ctxt, &parent_path);
	List user_list = list_create(slurmdb_destroy_user_rec);

	if (!dusers) {
		resp_warn(ctxt, __func__,
			  "ignoring empty or non-existant users array");
		goto cleanup;
	}

	if (DATA_PARSE(ctxt->parser, USER_LIST, user_list, dusers, parent_path))
		goto cleanup;

	if (list_for_each(user_list, _foreach_update_user, ctxt) < 0)
		goto cleanup;

	if (!ctxt->rc && commit)
		db_query_commit(ctxt);

cleanup:
	FREE_NULL_LIST(user_list);
	FREE_NULL_DATA(parent_path);
}

static int _foreach_delete_user(void *x, void *arg)
{
	char *user = x;
	data_t *users = arg;

	data_set_string(data_list_append(users), user);

	return DATA_FOR_EACH_CONT;
}

static void _delete_user(ctxt_t *ctxt, char *user_name)
{
	slurmdb_assoc_cond_t assoc_cond = {
		.user_list = list_create(NULL),
	};
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
		.with_assocs = true,
		.with_coords = true,
		.with_deleted = false,
		.with_wckeys = true,
	};
	List user_list = NULL;
	data_t *dremoved_users =
		data_set_list(data_key_set(ctxt->resp, "removed_users"));

	list_append(assoc_cond.user_list, user_name);

	if (!db_query_list(ctxt, &user_list, slurmdb_users_remove,
			   &user_cond) &&
	    (list_for_each(user_list, _foreach_delete_user, dremoved_users) >=
	     0))
		db_query_commit(ctxt);

	FREE_NULL_LIST(user_list);
	FREE_NULL_LIST(assoc_cond.user_list);
	assoc_cond.user_list = NULL;
}

/* based on sacctmgr_list_user() */
extern int op_handler_users(const char *context_id,
			    http_request_method_t method,
			    data_t *parameters, data_t *query, int tag,
			    data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc) {
		/* no-op - already logged */
	} else if (method == HTTP_REQUEST_GET) {
		slurmdb_user_cond_t user_cond = { 0 };

		if (query && data_get_dict_length(query)) {
			/* Default to no deleted users */
			foreach_query_search_t args = {
				.magic = MAGIC_QUERY_SEARCH,
				.ctxt = ctxt,
				.user_cond = &user_cond,
			};

			(void) data_dict_for_each(query, _foreach_query_search,
						  &args);
		}

		if (!ctxt->rc)
			_dump_users(ctxt, NULL, &user_cond);

	} else if (method == HTTP_REQUEST_POST) {
		_update_users(ctxt, (tag != CONFIG_OP_TAG));
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

static int op_handler_user(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);
	char *user_name = get_str_param("user_name", ctxt);

	if (ctxt->rc) {
		/* no-op - already logged */
	} else if (!user_name) {
		resp_error(ctxt, ESLURM_USER_ID_MISSING, __func__,
			   "User name must be provided singular query");
	} else if (method == HTTP_REQUEST_GET) {
		slurmdb_user_cond_t user_cond = {0};
		if (query && data_get_dict_length(query)) {
			/* Default to no deleted users */
			foreach_query_search_t args = {
				.magic = MAGIC_QUERY_SEARCH,
				.ctxt = ctxt,
				.user_cond = &user_cond,
			};

			if (data_dict_for_each(query, _foreach_query_search,
					       &args) < 0)
				return ESLURM_REST_INVALID_QUERY;
		}

		_dump_users(ctxt, user_name, &user_cond);
	} else if (method == HTTP_REQUEST_DELETE) {
		_delete_user(ctxt, user_name);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

extern void init_op_users(void)
{
	bind_operation_handler("/slurmdb/v0.0.39/users/", op_handler_users, 0);
	bind_operation_handler("/slurmdb/v0.0.39/user/{user_name}",
			       op_handler_user, 0);
}

extern void destroy_op_users(void)
{
	unbind_operation_handler(op_handler_users);
	unbind_operation_handler(op_handler_user);
}
