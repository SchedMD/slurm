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

#include "src/plugins/openapi/dbv0.0.38/api.h"

#define MAGIC_FOREACH_ACCOUNT 0xaefefef0
typedef struct {
	int magic;
	data_t *accts;
	List tres_list;
	List qos_list;
} foreach_account_t;

typedef struct {
	data_t *errors;
	slurmdb_account_cond_t *account_cond;
} foreach_query_search_t;

/* Change the account search conditions based on input parameters */
static data_for_each_cmd_t _foreach_query_search(const char *key,
						 data_t *data,
						 void *arg)
{
	foreach_query_search_t *args = arg;
	data_t *errors = args->errors;

	if (!xstrcasecmp("with_deleted", key)) {
		if (data_convert_type(data, DATA_TYPE_BOOL) != DATA_TYPE_BOOL) {
			resp_error(errors, ESLURM_REST_INVALID_QUERY,
				   "must be a Boolean", NULL);
			return DATA_FOR_EACH_FAIL;
		}

		if (data->data.bool_u)
			args->account_cond->with_deleted = true;
		else
			args->account_cond->with_deleted = false;

		return DATA_FOR_EACH_CONT;
	}

	resp_error(errors, ESLURM_REST_INVALID_QUERY, "Unknown query field",
		   NULL);
	return DATA_FOR_EACH_FAIL;
}

static int _parse_other_params(data_t *query,
			       slurmdb_account_cond_t *cond,
			       data_t *errors)
{
	if (!query || !data_get_dict_length(query))
		return SLURM_SUCCESS;

	foreach_query_search_t args = {
		.errors = errors,
		.account_cond = cond,
	};

	if (data_dict_for_each(query, _foreach_query_search, &args) < 0)
		return SLURM_ERROR;
	else
		return SLURM_SUCCESS;
}

static int _foreach_account(void *x, void *arg)
{
	parser_env_t penv = { 0 };

	slurmdb_account_rec_t *acct = x;
	foreach_account_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_ACCOUNT);

	if (dump(PARSE_ACCOUNT, acct,
		 data_set_dict(data_list_append(args->accts)), &penv))
		return DATA_FOR_EACH_FAIL;
	else
		return DATA_FOR_EACH_CONT;
}

/* based on sacctmgr_list_account() */
static int _dump_accounts(data_t *resp, void *auth,
			  slurmdb_account_cond_t *acct_cond)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	slurmdb_qos_cond_t qos_cond = {
		.with_deleted = 1,
	};
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};
	foreach_account_t args = {
		.magic = MAGIC_FOREACH_ACCOUNT,
		.accts = data_set_list(data_key_set(resp, "accounts")),
	};
	List acct_list = NULL;

	if (!(rc = db_query_list(errors, auth, &args.tres_list,
				 slurmdb_tres_get, &tres_cond)) &&
	    !(rc = db_query_list(errors, auth, &args.qos_list, slurmdb_qos_get,
				 &qos_cond)) &&
	    !(rc = db_query_list(errors, auth, &acct_list, slurmdb_accounts_get,
				 acct_cond)) &&
	    (list_for_each(acct_list, _foreach_account, &args) < 0))
		rc = ESLURM_REST_INVALID_QUERY;

	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(args.tres_list);
	FREE_NULL_LIST(args.qos_list);

	return rc;
}

#define MAGIC_FOREACH_UP_ACCT 0xefad1a19
typedef struct {
	int magic;
	List acct_list;
	data_t *errors;
	rest_auth_context_t *auth;
} foreach_update_acct_t;

static data_for_each_cmd_t _foreach_update_acct(data_t *data, void *arg)
{
	foreach_update_acct_t *args = arg;
	data_t *errors = args->errors;
	slurmdb_account_rec_t *acct;
	parser_env_t penv = {
		.auth = args->auth,
	};

	xassert(args->magic == MAGIC_FOREACH_UP_ACCT);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "each account entry must be a dictionary", NULL);
		return DATA_FOR_EACH_FAIL;
	}

	acct = xmalloc(sizeof(slurmdb_account_rec_t));
	acct->assoc_list = list_create(slurmdb_destroy_assoc_rec);
	acct->coordinators = list_create(slurmdb_destroy_coord_rec);

	if (parse(PARSE_ACCOUNT, acct, data, args->errors, &penv)) {
		slurmdb_destroy_account_rec(acct);
		return DATA_FOR_EACH_FAIL;
	} else {
		/* sacctmgr will set the org/desc as name if NULL */
		if (!acct->organization)
			acct->organization = xstrdup(acct->name);
		if (!acct->description)
			acct->description = xstrdup(acct->name);

		(void)list_append(args->acct_list, acct);
		return DATA_FOR_EACH_CONT;
	}
}

static int _update_accts(data_t *query, data_t *resp, void *auth,
			 bool commit)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	foreach_update_acct_t args = {
		.magic = MAGIC_FOREACH_UP_ACCT,
		.auth = auth,
		.errors = errors,
		.acct_list = list_create(slurmdb_destroy_account_rec),
	};
	data_t *daccts = get_query_key_list("accounts", errors, query);

	if (daccts &&
	    (data_list_for_each(daccts, _foreach_update_acct, &args) < 0))
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc &&
	    !(rc = db_query_rc(errors, auth, args.acct_list,
			       slurmdb_accounts_add)) &&
	    commit)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(args.acct_list);

	return rc;
}

static int _foreach_delete_acct(void *x, void *arg)
{
	char *acct = x;
	data_t *accts = arg;

	data_set_string(data_list_append(accts), acct);

	return DATA_FOR_EACH_CONT;
}

static int _delete_account(data_t *resp, void *auth, char *account)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	List removed = NULL;
	slurmdb_assoc_cond_t assoc_cond = {
		.acct_list = list_create(NULL),
		.user_list = list_create(NULL),
	};
	slurmdb_account_cond_t acct_cond = {
		.assoc_cond = &assoc_cond,
	};

	list_append(assoc_cond.acct_list, account);

	if (!db_query_list(errors, auth, &removed, slurmdb_accounts_remove,
			   &acct_cond) &&
	    (list_for_each(removed, _foreach_delete_acct,
			   data_set_list(data_key_set(
				   resp, "removed_associations"))) < 0))
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "unable to delete accounts", NULL);

	if (!rc)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(removed);
	FREE_NULL_LIST(assoc_cond.acct_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	return rc;
}

extern int op_handler_account(const char *context_id,
			      http_request_method_t method,
			      data_t *parameters, data_t *query, int tag,
			      data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	char *acct = get_str_param("account_name", errors, parameters);

	if (!acct) {
		/* no-op */;
	} else if (method == HTTP_REQUEST_GET) {
		slurmdb_assoc_cond_t assoc_cond = {
			.acct_list = list_create(NULL),
		};
		slurmdb_account_cond_t acct_cond = {
			.assoc_cond = &assoc_cond,
			.with_assocs = true,
			.with_coords = true,
			/* with_deleted defaults to false */
		};

		list_append(assoc_cond.acct_list, acct);

		/* Change search conditions based on parameters */
		if (_parse_other_params(query, &acct_cond, errors) !=
		    SLURM_SUCCESS)
			rc = ESLURM_REST_INVALID_QUERY;
		else
			rc = _dump_accounts(resp, auth, &acct_cond);

		FREE_NULL_LIST(assoc_cond.acct_list);

		return rc;
	} else if (method == HTTP_REQUEST_DELETE) {
		return _delete_account(resp, auth, acct);
	}

	return ESLURM_REST_INVALID_QUERY;
}

/* based on sacctmgr_list_account() */
extern int op_handler_accounts(const char *context_id,
			       http_request_method_t method, data_t *parameters,
			       data_t *query, int tag, data_t *resp, void *auth)
{
	data_t *errors = populate_response_format(resp);
	if (method == HTTP_REQUEST_GET) {
		slurmdb_account_cond_t acct_cond = {
			.with_assocs = true,
			.with_coords = true,
			/* with_deleted defaults to false */
		};

		/* Change search conditions based on parameters */
		_parse_other_params(query, &acct_cond, errors);

		return _dump_accounts(resp, auth, &acct_cond);
	} else if (method == HTTP_REQUEST_POST) {
		return _update_accts(query, resp, auth, (tag != CONFIG_OP_TAG));
	}

	return ESLURM_REST_INVALID_QUERY;
}

extern void init_op_accounts(void)
{
	bind_operation_handler("/slurmdb/v0.0.38/accounts/",
			       op_handler_accounts, 0);
	bind_operation_handler("/slurmdb/v0.0.38/account/{account_name}/",
			       op_handler_account, 0);
}

extern void destroy_op_accounts(void)
{
	unbind_operation_handler(op_handler_accounts);
	unbind_operation_handler(op_handler_account);
}
