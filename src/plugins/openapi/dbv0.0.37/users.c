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

#define MAGIC_FOREACH_USER 0xa13efef2
typedef struct {
	int magic;
	data_t *users;
	List tres_list;
	List qos_list;
} foreach_user_t;

static int _foreach_user(void *x, void *arg)
{
	slurmdb_user_rec_t *user = x;
	foreach_user_t *args = arg;
	parser_env_t penv = {
		.g_tres_list = args->tres_list,
		.g_qos_list = args->qos_list,
	};

	xassert(args->magic == MAGIC_FOREACH_USER);
	xassert(user);

	if (dump(PARSE_USER, user, data_set_dict(data_list_append(args->users)),
		 &penv))
		return -1;
	else
		return 0;
}

static int _dump_users(data_t *resp, data_t *errors, void *auth, char *user_name)
{
	int rc = SLURM_SUCCESS;
	List user_list = NULL;
	slurmdb_qos_cond_t qos_cond = {
		.with_deleted = 1,
	};
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};
	foreach_user_t args = {
		.magic = MAGIC_FOREACH_USER,
		.users = data_set_list(data_key_set(resp, "users")),
	};
	slurmdb_assoc_cond_t assoc_cond = { 0 };
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
		.with_assocs = true,
		.with_coords = true,
		.with_deleted = true,
		.with_wckeys = true,
	};

	if (user_name) {
		assoc_cond.user_list = list_create(NULL);
		list_append(assoc_cond.user_list, user_name);
	}

	if (!(rc = db_query_list(errors, auth, &user_list, slurmdb_users_get,
				 &user_cond)) &&
	    !(rc = db_query_list(errors, auth, &args.tres_list,
				 slurmdb_tres_get, &tres_cond)) &&
	    !(rc = db_query_list(errors, auth, &args.qos_list, slurmdb_qos_get,
				 &qos_cond)) &&
	    (list_for_each(user_list, _foreach_user, &args) < 0))
		resp_error(errors, ESLURM_DATA_CONV_FAILED, NULL,
			   "_foreach_user");

	FREE_NULL_LIST(args.tres_list);
	FREE_NULL_LIST(args.qos_list);
	FREE_NULL_LIST(user_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	return rc;
}

#define MAGIC_USER_COORD 0x8e8dbee1
typedef struct {
	int magic;
	List acct_list; /* list of char *'s of names of accounts */
	slurmdb_user_cond_t user_cond;
	slurmdb_assoc_cond_t assoc_cond;
} add_user_coord_t;

#define MAGIC_FOREACH_UP_USER 0xdbed1a12
typedef struct {
	int magic;
	List user_list;
	data_t *errors;
	rest_auth_context_t *auth;
} foreach_update_user_t;

static data_for_each_cmd_t _foreach_update_user(data_t *data, void *arg)
{
	foreach_update_user_t *args = arg;
	data_t *errors = args->errors;
	slurmdb_user_rec_t *user;
	parser_env_t penv = {
		.auth = args->auth,
	};

	xassert(args->magic == MAGIC_FOREACH_UP_USER);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		resp_error(errors, ESLURM_NOT_SUPPORTED,
			   "each user entry must be a dictionary", NULL);
		return DATA_FOR_EACH_FAIL;
	}

	user = xmalloc(sizeof(slurmdb_user_rec_t));
	user->assoc_list = list_create(slurmdb_destroy_assoc_rec);
	user->coord_accts = list_create(slurmdb_destroy_coord_rec);

	if (parse(PARSE_USER, user, data, args->errors, &penv)) {
		slurmdb_destroy_user_rec(user);
		return DATA_FOR_EACH_FAIL;
	} else {
		(void)list_append(args->user_list, user);
		return DATA_FOR_EACH_CONT;
	}
}

#define MAGIC_USER_COORD_SPLIT_COORD 0x8e8dbee3
typedef struct {
	int magic;
	add_user_coord_t *uc;
} _foreach_user_coord_split_coord_t;

static int _foreach_user_coord_split_coord(void *x, void *arg)
{
	slurmdb_coord_rec_t *coord = x;
	_foreach_user_coord_split_coord_t *args = arg;

	xassert(args->magic == MAGIC_USER_COORD_SPLIT_COORD);
	xassert(args->uc->magic == MAGIC_USER_COORD);

	if (coord->direct)
		list_append(args->uc->acct_list, xstrdup(coord->name));

	return 0;
}

#define MAGIC_USER_COORD_SPLIT 0x8e8dbee2
typedef struct {
	int magic;
	List list_coords; /* list of add_user_coord_t */
} _foreach_user_coord_split_t;

static int _foreach_user_coord_split(void *x, void *arg)
{
	slurmdb_user_rec_t *user = x;
	_foreach_user_coord_split_t *args = arg;
	add_user_coord_t *uc = NULL;
	_foreach_user_coord_split_coord_t c_args = {
		.magic = MAGIC_USER_COORD_SPLIT_COORD,
	};

	xassert(args->magic == MAGIC_USER_COORD_SPLIT);

	if (!user->coord_accts || list_is_empty(user->coord_accts))
		/* nothing to do here */
		return 0;

	c_args.uc = uc = xmalloc(sizeof(*uc));
	uc->magic = MAGIC_USER_COORD;
	uc->acct_list = list_create(xfree_ptr);
	uc->user_cond.assoc_cond = &uc->assoc_cond;
	uc->assoc_cond.user_list = list_create(xfree_ptr);
	list_append(uc->assoc_cond.user_list, xstrdup(user->name));

	if (list_for_each(user->coord_accts, _foreach_user_coord_split_coord,
			  &c_args) < 0)
		return -1;

	(void)list_append(args->list_coords, uc);

	return 1;
}

#define MAGIC_USER_COORD_ADD 0x8e8ffee2
typedef struct {
	int magic;
	rest_auth_context_t *auth;
	int rc;
	data_t *errors;
} _foreach_user_coord_add_t;

static int _foreach_user_coord_add(void *x, void *arg)
{
	int rc = SLURM_SUCCESS;
	add_user_coord_t *uc = x;
	_foreach_user_coord_add_t *args = arg;

	xassert(uc->magic == MAGIC_USER_COORD);
	xassert(args->magic == MAGIC_USER_COORD_ADD);

	if ((args->rc = slurmdb_coord_add(openapi_get_db_conn(args->auth),
					  uc->acct_list, &uc->user_cond)))
		rc = resp_error(args->errors, args->rc, NULL,
				"slurmdb_coord_add");

	return (rc ? -1 : 0);
}

static void _destroy_user_coord_t(void *x)
{
	add_user_coord_t *uc = x;
	xassert(uc->magic == MAGIC_USER_COORD);

	FREE_NULL_LIST(uc->acct_list);
	FREE_NULL_LIST(uc->assoc_cond.user_list);

	xfree(uc);
}

static int _update_users(data_t *query, data_t *resp, void *auth,
			 bool commit)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	foreach_update_user_t args = {
		.magic = MAGIC_FOREACH_UP_USER,
		.auth = auth,
		.errors = errors,
		.user_list = list_create(slurmdb_destroy_user_rec),
	};
	_foreach_user_coord_split_t c_args = {
		.magic = MAGIC_USER_COORD_SPLIT,
		.list_coords = list_create(_destroy_user_coord_t),
	};
	_foreach_user_coord_add_t add_args = {
		.magic = MAGIC_USER_COORD_ADD,
		.auth = auth,
		.errors = errors,
	};
	data_t *dusers = get_query_key_list("users", errors, query);

	if (!dusers)
		rc = ESLURM_REST_INVALID_QUERY;
	else if (data_list_for_each(dusers, _foreach_update_user, &args) < 0)
		rc = ESLURM_REST_INVALID_QUERY;
	/* split out the coordinators until after the users are done */
	else if (list_for_each(args.user_list, _foreach_user_coord_split,
			       &c_args) < 0)
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc && !(rc = db_query_rc(errors, auth, args.user_list,
				      slurmdb_users_add))) {
		(void)list_for_each(c_args.list_coords, _foreach_user_coord_add,
				    &add_args);
		rc = add_args.rc;
	}

	if (!rc && commit)
		db_query_commit(errors, auth);

	FREE_NULL_LIST(args.user_list);
	FREE_NULL_LIST(c_args.list_coords);

	return rc;
}

static int _foreach_delete_user(void *x, void *arg)
{
	char *user = x;
	data_t *users = arg;

	data_set_string(data_list_append(users), user);

	return DATA_FOR_EACH_CONT;
}

static int _delete_user(data_t *resp, void *auth,
			char *user_name, data_t *errors)
{
	int rc = SLURM_SUCCESS;
	slurmdb_assoc_cond_t assoc_cond = { .user_list = list_create(NULL) };
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
		.with_assocs = true,
		.with_coords = true,
		.with_deleted = false,
		.with_wckeys = true,
	};
	List user_list;

	list_append(assoc_cond.user_list, user_name);

	if (!(rc = db_query_list(errors, auth, &user_list, slurmdb_users_remove,
				 &user_cond)) &&
	    (list_for_each(user_list, _foreach_delete_user,
			   data_set_list(data_key_set(resp, "removed_users"))) <
	     0))
		rc = resp_error(errors, ESLURM_REST_INVALID_QUERY,
				"_foreach_delete_user unexpectedly failed",
				NULL);

	if (!rc)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(user_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	return rc;
}

/* based on sacctmgr_list_user() */
extern int op_handler_users(const char *context_id,
			    http_request_method_t method,
			    data_t *parameters, data_t *query, int tag,
			    data_t *resp, void *auth)
{
	data_t *errors = populate_response_format(resp);

	if (method == HTTP_REQUEST_GET)
		return _dump_users(resp, errors, auth, NULL);
	else if (method == HTTP_REQUEST_POST)
		return _update_users(query, resp, auth, (tag != CONFIG_OP_TAG));
	else
		return ESLURM_REST_INVALID_QUERY;
}

static int op_handler_user(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	char *user_name = get_str_param("user_name", errors, parameters);

	if (!user_name)
		rc = ESLURM_REST_INVALID_QUERY;
	else if (method == HTTP_REQUEST_GET)
		rc = _dump_users(resp, errors, auth, user_name);
	else if (method == HTTP_REQUEST_DELETE)
		rc = _delete_user(resp, auth, user_name, errors);
	else
		rc = ESLURM_REST_INVALID_QUERY;

	return rc;
}

extern void init_op_users(void)
{
	bind_operation_handler("/slurmdb/v0.0.37/users/", op_handler_users, 0);
	bind_operation_handler("/slurmdb/v0.0.37/user/{user_name}",
			       op_handler_user, 0);
}

extern void destroy_op_users(void)
{
	unbind_operation_handler(op_handler_users);
	unbind_operation_handler(op_handler_user);
}
