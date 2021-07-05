/*****************************************************************************\
 *  wckeys.c - Slurm REST API acct wckey http operations handlers
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

#define MAGIC_FOREACH_WCKEY 0xb3a2faf2
typedef struct {
	int magic;
	data_t *wckeys;
} foreach_wckey_t;

static int _foreach_wckey(void *x, void *arg)
{
	slurmdb_wckey_rec_t *wckey = x;
	foreach_wckey_t *args = arg;
	parser_env_t penv = { 0 };

	xassert(args->magic == MAGIC_FOREACH_WCKEY);

	if (dump(PARSE_WCKEY, wckey,
		 data_set_dict(data_list_append(args->wckeys)), &penv))
		return -1;

	return 1;
}

static int _dump_wckeys(data_t *resp, data_t *errors, char *wckey,
			void *auth)
{
	int rc = SLURM_SUCCESS;
	slurmdb_wckey_cond_t wckey_cond = {
		.with_deleted = true,
	};
	foreach_wckey_t args = {
		.magic = MAGIC_FOREACH_WCKEY,
		.wckeys = data_set_list(data_key_set(resp, "wckeys")),
	};
	List wckey_list = NULL;

	if (wckey) {
		wckey_cond.name_list = list_create(NULL);
		list_append(wckey_cond.name_list, wckey);
	}

	if (!(rc = db_query_list(errors, auth, &wckey_list, slurmdb_wckeys_get,
				 &wckey_cond)) &&
	    (list_for_each(wckey_list, _foreach_wckey, &args) < 0))
		rc = ESLURM_DATA_CONV_FAILED;

	FREE_NULL_LIST(wckey_list);
	FREE_NULL_LIST(wckey_cond.name_list);

	return rc;
}

#define MAGIC_FOREACH_DEL_WCKEY 0xb3a2faf1
typedef struct {
	int magic;
	data_t *wckeys;
} foreach_del_wckey_t;

static int _foreach_del_wckey(void *x, void *arg)
{
	char *wckey = x;
	foreach_del_wckey_t *args = arg;

	data_set_string(data_list_append(args->wckeys), wckey);
	return 1;
}

static int _delete_wckey(data_t *resp, data_t *errors, char *wckey,
			 void *auth)
{
	int rc = SLURM_SUCCESS;
	slurmdb_wckey_cond_t wckey_cond = {
		.with_deleted = true,
		.name_list = list_create(NULL),
	};
	foreach_del_wckey_t args = {
		.magic = MAGIC_FOREACH_DEL_WCKEY,
		.wckeys = data_set_list(data_key_set(resp, "deleted_wckeys")),
	};
	List wckey_list = NULL;

	if (!wckey) {
		rc = ESLURM_REST_EMPTY_RESULT;
		goto cleanup;
	}

	list_append(wckey_cond.name_list, wckey);

	if (!(rc = db_query_list(errors, auth, &wckey_list,
				 slurmdb_wckeys_remove, &wckey_cond)))
		rc = db_query_commit(errors, auth);

	if (!rc && (list_for_each(wckey_list, _foreach_del_wckey, &args) < 0))
		rc = ESLURM_DATA_CONV_FAILED;
cleanup:
	FREE_NULL_LIST(wckey_list);
	FREE_NULL_LIST(wckey_cond.name_list);

	return rc;
}

#define MAGIC_FOREACH_UP_WCKEY 0xdabd1019
typedef struct {
	int magic;
	List wckey_list;
	data_t *errors;
	rest_auth_context_t *auth;
} foreach_update_wckey_t;

static data_for_each_cmd_t _foreach_update_wckey(data_t *data, void *arg)
{
	foreach_update_wckey_t *args = arg;
	slurmdb_wckey_rec_t *wckey;
	parser_env_t penv = {
		.auth = args->auth,
	};

	xassert(args->magic == MAGIC_FOREACH_UP_WCKEY);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		data_t *e = data_set_dict(data_list_append(args->errors));
		data_set_string(data_key_set(e, "field"), "wckey");
		data_set_string(data_key_set(e, "error"),
				"each wckey entry must be a dictionary");
		return DATA_FOR_EACH_FAIL;
	}

	wckey = xmalloc(sizeof(slurmdb_wckey_rec_t));
	wckey->accounting_list = list_create(slurmdb_destroy_account_rec);
	(void)list_append(args->wckey_list, wckey);

	if (parse(PARSE_WCKEY, wckey, data, args->errors, &penv))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _update_wckeys(data_t *query, data_t *resp, data_t *errors,
			  void *auth, bool commit)
{
	int rc = SLURM_SUCCESS;
	foreach_update_wckey_t args = {
		.magic = MAGIC_FOREACH_UP_WCKEY,
		.auth = auth,
		.errors = errors,
		.wckey_list = list_create(slurmdb_destroy_wckey_rec),
	};
	data_t *dwckeys = get_query_key_list("wckeys", errors, query);

	if (!dwckeys)
		rc = ESLURM_REST_INVALID_QUERY;
	else if (data_list_for_each(dwckeys, _foreach_update_wckey, &args) < 0)
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc &&
	    !(rc = db_query_rc(errors, auth, args.wckey_list,
			       slurmdb_wckeys_add)) &&
	    commit)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(args.wckey_list);

	return rc;
}

extern int op_handler_wckey(const char *context_id,
			    http_request_method_t method,
			    data_t *parameters, data_t *query, int tag,
			    data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	char *wckey = get_str_param("wckey", errors, parameters);

	if (!wckey)
		rc = ESLURM_REST_INVALID_QUERY;
	else if (method == HTTP_REQUEST_GET)
		rc = _dump_wckeys(resp, errors, wckey, auth);
	else if (!rc && (method == HTTP_REQUEST_DELETE))
		rc = _delete_wckey(resp, errors, wckey, auth);
	else
		rc = ESLURM_REST_INVALID_QUERY;

	return rc;
}

extern int op_handler_wckeys(const char *context_id,
			     http_request_method_t method, data_t *parameters,
			     data_t *query, int tag, data_t *resp,
			     void *auth)
{
	data_t *errors = populate_response_format(resp);
	int rc = SLURM_SUCCESS;

	if (method == HTTP_REQUEST_GET)
		rc = _dump_wckeys(resp, errors, NULL, auth);
	else if (method == HTTP_REQUEST_POST)
		rc = _update_wckeys(query, resp, errors, auth,
				    (tag != CONFIG_OP_TAG));
	else
		rc = ESLURM_REST_INVALID_QUERY;

	return rc;
}

extern void init_op_wckeys(void)
{
	bind_operation_handler("/slurmdb/v0.0.37/wckeys/", op_handler_wckeys,
			       0);
	bind_operation_handler("/slurmdb/v0.0.37/wckey/{wckey}",
			       op_handler_wckey, 0);
}

extern void destroy_op_wckeys(void)
{
	unbind_operation_handler(op_handler_wckeys);
	unbind_operation_handler(op_handler_wckey);
}
