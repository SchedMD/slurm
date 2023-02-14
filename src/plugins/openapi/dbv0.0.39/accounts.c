/*****************************************************************************\
 *  accounts.c - Slurm REST API accounting accounts http operations handlers
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
#include "src/interfaces/openapi.h"
#include "src/common/parse_time.h"
#include "src/common/ref.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/dbv0.0.39/api.h"

#define MAGIC_FOREACH_ACCOUNT 0xaefefef0
#define MAGIC_FOREACH_SEARCH 0xaefef9fa
#define MAGIC_FOREACH_COORD 0xabfbf9fa

typedef struct {
	int magic; /* MAGIC_FOREACH_ACCOUNT */
	ctxt_t *ctxt;
	data_t *accts;
} foreach_account_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_SEARCH */
	ctxt_t *ctxt;
	slurmdb_account_cond_t *account_cond;
} foreach_query_search_t;

/* Change the account search conditions based on input parameters */
static data_for_each_cmd_t _foreach_query_search(const char *key, data_t *data,
						 void *arg)
{
	foreach_query_search_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_SEARCH);
	xassert(args->ctxt->magic == MAGIC_CTXT);

	if (!xstrcasecmp("with_deleted", key)) {
		if (data_convert_type(data, DATA_TYPE_BOOL) != DATA_TYPE_BOOL) {
			char *str = NULL;

			data_get_string_converted(data, &str);

			resp_error(args->ctxt, ESLURM_REST_INVALID_QUERY,
				   __func__, "Query %s=%s must be a Boolean",
				   key,
				   (str ? str :
					  data_type_to_string(
						  data_get_type(data))));

			xfree(str);
			return DATA_FOR_EACH_FAIL;
		}

		if (data->data.bool_u)
			args->account_cond->with_deleted = true;
		else
			args->account_cond->with_deleted = false;

		return DATA_FOR_EACH_CONT;
	}

	resp_error(args->ctxt, ESLURM_REST_INVALID_QUERY, __func__,
		   "Unknown query key %s field value", key);
	return DATA_FOR_EACH_FAIL;
}

static int _parse_other_params(ctxt_t *ctxt, slurmdb_account_cond_t *cond)
{
	foreach_query_search_t args;

	if (!ctxt->query || !data_get_dict_length(ctxt->query))
		return SLURM_SUCCESS;

	xassert(ctxt->magic == MAGIC_CTXT);

	args.magic = MAGIC_FOREACH_SEARCH;
	args.ctxt = ctxt;
	args.account_cond = cond;

	if (data_dict_for_each(ctxt->query, _foreach_query_search, &args) < 0)
		return ESLURM_REST_INVALID_QUERY;
	else
		return SLURM_SUCCESS;
}

static int _foreach_account(void *x, void *arg)
{
	slurmdb_account_rec_t *acct = x;
	foreach_account_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_ACCOUNT);
	xassert(args->ctxt->magic == MAGIC_CTXT);

	DATA_DUMP(args->ctxt->parser, ACCOUNT, *acct,
		  data_list_append(args->accts));

	return (!args->ctxt->rc ? SLURM_SUCCESS : SLURM_ERROR);
}

/* based on sacctmgr_list_account() */
static void _dump_accounts(ctxt_t *ctxt, slurmdb_account_cond_t *acct_cond)
{
	foreach_account_t args = {
		.magic = MAGIC_FOREACH_ACCOUNT,
		.ctxt = ctxt,
	};
	List acct_list = NULL;

	args.accts = data_set_list(data_key_set(ctxt->resp, "accounts"));

	if (!db_query_list(ctxt, &acct_list, slurmdb_accounts_get, acct_cond) &&
	    acct_list)
		list_for_each(acct_list, _foreach_account, &args);

	FREE_NULL_LIST(acct_list);
}

typedef struct {
	int magic; /* MAGIC_FOREACH_COORD */
	ctxt_t *ctxt;
	slurmdb_account_rec_t *acct;
	slurmdb_account_rec_t *orig_acct;
} foreach_update_acct_coord_t;

static int _foreach_match_coord(void *x, void *key)
{
	slurmdb_coord_rec_t *coord1 = x;
	slurmdb_coord_rec_t *coord2 = key;

	return !xstrcasecmp(coord1->name, coord2->name) ? 1 : 0;
}

static int _foreach_add_acct_coord(void *x, void *arg)
{
	int rc;
	slurmdb_coord_rec_t *coord = x;
	foreach_update_acct_coord_t *args = arg;
	ctxt_t *ctxt = args->ctxt;
	list_t *acct_list;
	slurmdb_assoc_cond_t assoc_cond = {};
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
	};

	xassert(args->magic == MAGIC_FOREACH_COORD);
	xassert(args->ctxt->magic == MAGIC_CTXT);

	if (args->orig_acct &&
	    list_find_first(args->orig_acct->coordinators, _foreach_match_coord,
			    coord)) {
		/* account already has coordinator -> nothing to do here */
		return SLURM_SUCCESS;
	}

	acct_list = list_create(NULL);
	list_append(acct_list, args->acct->name);
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, coord->name);

	errno = 0;
	if ((rc = slurmdb_coord_add(ctxt->db_conn, acct_list, &user_cond))) {
		if (errno)
			rc = errno;

		resp_error(ctxt, rc, "slurmdb_coord_add()",
			   "adding coordinator %s to account %s failed",
			   coord->name, args->acct->name);
	}

	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	return rc ? SLURM_ERROR : SLURM_SUCCESS;
}

static int _foreach_rm_acct_coord(void *x, void *arg)
{
	int rc = SLURM_SUCCESS;
	slurmdb_coord_rec_t *coord = x;
	foreach_update_acct_coord_t *args = arg;
	ctxt_t *ctxt = args->ctxt;
	list_t *acct_list = NULL, *rm_list = NULL;
	slurmdb_assoc_cond_t assoc_cond = {};
	slurmdb_user_cond_t user_cond = {
		.assoc_cond = &assoc_cond,
	};

	xassert(args->magic == MAGIC_FOREACH_COORD);
	xassert(args->ctxt->magic == MAGIC_CTXT);

	if (args->acct->coordinators &&
	    list_find_first(args->acct->coordinators, _foreach_match_coord,
			    coord)) {
		/* account already has coordinator -> nothing to do here */
		return SLURM_SUCCESS;
	}

	/* coordinator not in new acct list -> must be removed */
	acct_list = list_create(NULL);
	list_append(acct_list, args->acct->name);
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, coord->name);

	errno = 0;
	if (!(rm_list = slurmdb_coord_remove(ctxt->db_conn, acct_list, &user_cond))) {
		if (errno)
			rc = errno;
		else
			rc = SLURM_ERROR;

		resp_error(ctxt, rc, "slurmdb_coord_remove()",
			   "removing coordinator %s from account %s failed",
			   coord->name, args->acct->name);
	} else {
		xassert(list_count(rm_list) == 1);
	}

	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(rm_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	return rc ? SLURM_ERROR : SLURM_SUCCESS;
}

static int _foreach_update_acct(void *x, void *arg)
{
	slurmdb_account_rec_t *acct = x;
	ctxt_t *ctxt = arg;
	List acct_list = NULL;
	slurmdb_assoc_cond_t assoc_cond = {};
	slurmdb_account_cond_t acct_cond = {
		.assoc_cond = &assoc_cond,
		.with_coords = true,
	};
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, acct->name);

	if (db_query_list_xempty(ctxt, &acct_list, slurmdb_accounts_get,
				 &acct_cond))
		goto cleanup;

	if (acct->assoc_list && list_count(acct->assoc_list)) {
		resp_warn(ctxt, __func__, "Account associations ignored. They must be set via the associations end point.");
	}

	if (acct->flags & SLURMDB_ACCT_FLAG_DELETED) {
		resp_warn(ctxt, __func__,
			  "Ignoring request to set flag: DELETED");
	}

	if (!acct_list || list_is_empty(acct_list)) {
		debug("%s: [%s] add account request: acct=%s",
		      __func__, ctxt->id, acct->name);

		if (!acct_list)
			acct_list = list_create(NULL);
		list_append(acct_list, acct);

		db_query_rc(ctxt, acct_list, slurmdb_accounts_add);

		if (acct->coordinators) {
			foreach_update_acct_coord_t cargs = {
				.magic = MAGIC_FOREACH_COORD,
				.ctxt = ctxt,
				.acct = acct,
			};

			list_for_each(acct->coordinators,
				      _foreach_add_acct_coord, &cargs);
		}
	} else if (list_count(acct_list) > 1) {
		resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_MODIFY, __func__,
			   "ambiguous account modify request");
	} else {
		debug("%s: [%s] modifying account request: acct=%s", __func__,
		      ctxt->id, acct->name);

		if (!db_modify_rc(ctxt, &acct_cond, acct,
				  slurmdb_accounts_modify)) {
			foreach_update_acct_coord_t cargs = {
				.magic = MAGIC_FOREACH_COORD,
				.ctxt = ctxt,
				.acct = acct,
				.orig_acct = list_peek(acct_list),
			};

			if (acct->coordinators)
				list_for_each(acct->coordinators,
					      _foreach_add_acct_coord, &cargs);
			if (cargs.orig_acct->coordinators)
				list_for_each(cargs.orig_acct->coordinators,
					      _foreach_rm_acct_coord, &cargs);
		}
	}

cleanup:
	FREE_NULL_LIST(assoc_cond.acct_list);
	FREE_NULL_LIST(acct_list);
	return ctxt->rc ? SLURM_ERROR : SLURM_SUCCESS;
}

static void _update_accts(ctxt_t *ctxt, bool commit)
{
	data_t *parent_path = NULL;
	List acct_list = list_create(slurmdb_destroy_account_rec);
	data_t *daccts = get_query_key_list("accounts", ctxt, &parent_path);

	if (DATA_PARSE(ctxt->parser, ACCOUNT_LIST, acct_list, daccts,
		       parent_path))
		goto cleanup;

	if (list_for_each(acct_list, _foreach_update_acct, ctxt) < 0)
		goto cleanup;

	if (!ctxt->rc && commit)
		db_query_commit(ctxt);

cleanup:
	FREE_NULL_LIST(acct_list);
	FREE_NULL_DATA(parent_path);
}

static int _foreach_delete_acct(void *x, void *arg)
{
	char *acct = x;
	data_t *accts = arg;

	data_set_string(data_list_append(accts), acct);

	return DATA_FOR_EACH_CONT;
}

static void _delete_account(ctxt_t *ctxt, char *account)
{
	data_t *dremoved;
	List removed = NULL;
	slurmdb_assoc_cond_t assoc_cond = {
		.acct_list = list_create(NULL),
		.user_list = list_create(NULL),
	};
	slurmdb_account_cond_t acct_cond = {
		.assoc_cond = &assoc_cond,
	};

	list_append(assoc_cond.acct_list, account);

	if (db_query_list(ctxt, &removed, slurmdb_accounts_remove, &acct_cond))
		goto cleanup;

	dremoved = data_set_list(data_key_set(ctxt->resp, "removed_accounts"));

	if (list_for_each(removed, _foreach_delete_acct, dremoved) >= 0)
		db_query_commit(ctxt);

cleanup:
	FREE_NULL_LIST(removed);
	FREE_NULL_LIST(assoc_cond.acct_list);
	FREE_NULL_LIST(assoc_cond.user_list);
}

extern int op_handler_account(const char *context_id,
			      http_request_method_t method,
			      data_t *parameters, data_t *query, int tag,
			      data_t *resp, void *auth)
{
	char *acct;
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc) {
		/* no-op already logged */
	} else if (!(acct = get_str_param("account_name", ctxt))) {
		/* no-op already logged */
	} else if (method == HTTP_REQUEST_GET) {
		slurmdb_assoc_cond_t assoc_cond = {};
		slurmdb_account_cond_t acct_cond = {
			.assoc_cond = &assoc_cond,
			.with_assocs = true,
			.with_coords = true,
			/* with_deleted defaults to false */
		};

		assoc_cond.acct_list = list_create(NULL);

		/* Change search conditions based on parameters */
		if (!_parse_other_params(ctxt, &acct_cond)) {
			list_append(assoc_cond.acct_list, acct);

			_dump_accounts(ctxt, &acct_cond);
		}

		FREE_NULL_LIST(assoc_cond.acct_list);
	} else if (method == HTTP_REQUEST_DELETE) {
		_delete_account(ctxt, acct);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

/* based on sacctmgr_list_account() */
extern int op_handler_accounts(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp, void *auth)
{
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc) {
		/* no-op already logged */
	} else if (method == HTTP_REQUEST_GET) {
		slurmdb_account_cond_t acct_cond = {
			.with_assocs = true,
			.with_coords = true,
			/* with_deleted defaults to false */
		};

		/* Change search conditions based on parameters */
		if (!_parse_other_params(ctxt, &acct_cond))
			_dump_accounts(ctxt, &acct_cond);
	} else if (method == HTTP_REQUEST_POST) {
		_update_accts(ctxt, (tag != CONFIG_OP_TAG));
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

	return fini_connection(ctxt);
}

extern void init_op_accounts(void)
{
	bind_operation_handler("/slurmdb/v0.0.39/accounts/",
			       op_handler_accounts, 0);
	bind_operation_handler("/slurmdb/v0.0.39/account/{account_name}/",
			       op_handler_account, 0);
}

extern void destroy_op_accounts(void)
{
	unbind_operation_handler(op_handler_accounts);
	unbind_operation_handler(op_handler_account);
}
