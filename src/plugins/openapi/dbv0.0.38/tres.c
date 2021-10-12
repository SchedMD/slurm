/*****************************************************************************\
 *  tres.c - Slurm REST API accounting TRES http operations handlers
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

static int _foreach_dump_tres(void *x, void *arg)
{
	slurmdb_tres_rec_t *t = (slurmdb_tres_rec_t *)x;
	parser_env_t penv = { 0 };

	if (dump(PARSE_TRES, t, data_set_dict(data_list_append(arg)), &penv))
		return -1;

	return 0;
}

static int _dump_tres(data_t *resp, rest_auth_context_t *auth)
{
	data_t *errors = populate_response_format(resp);
	int rc = SLURM_SUCCESS;
	List tres_list = NULL;
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};

	if (!(rc = db_query_list(errors, auth, &tres_list, slurmdb_tres_get,
				 &tres_cond)) &&
	    (list_for_each(tres_list, _foreach_dump_tres,
			   data_set_list(data_key_set(resp, "TRES"))) < 0))
		rc = ESLURM_DATA_CONV_FAILED;

	FREE_NULL_LIST(tres_list);

	return SLURM_SUCCESS;
}

#define MAGIC_FOREACH_TRES 0xdeed1a11
typedef struct {
	int magic;
	List tres_list;
	data_t *errors;
} foreach_tres_t;

static data_for_each_cmd_t _foreach_tres(data_t *data, void *arg)
{
	foreach_tres_t *args = arg;
	data_t *errors = args->errors;
	parser_env_t penv = { 0 };
	slurmdb_tres_rec_t *tres;

	xassert(args->magic == MAGIC_FOREACH_TRES);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		resp_error(errors, ESLURM_NOT_SUPPORTED,
			   "each TRES entry must be a dictionary", "TRES");
		return DATA_FOR_EACH_FAIL;
	}

	tres = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(args->tres_list, tres);

	if (parse(PARSE_TRES, tres, data, args->errors, &penv))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _update_tres(data_t *query, data_t *resp, void *auth,
			bool commit)
{
	data_t *dtres = NULL;
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	List tres_list = list_create(slurmdb_destroy_tres_rec);
	foreach_tres_t args = {
		.magic = MAGIC_FOREACH_TRES,
		.tres_list = tres_list,
		.errors = errors,
	};

#ifdef NDEBUG
	/*
	 * Updating TRES is not currently supported and is disabled
	 * except for developer testing
	 */
	if (!commit)
		return SLURM_SUCCESS;
	else
		return resp_error(errors, ESLURM_NOT_SUPPORTED,
				  "Updating TRES is not currently supported.",
				  NULL);
#endif /*!NDEBUG*/

	if (!(dtres = get_query_key_list("TRES", errors, query)))
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc && (data_list_for_each(dtres, _foreach_tres, &args) < 0))
		rc = ESLURM_REST_INVALID_QUERY;

	if (!(rc = db_query_rc(errors, auth, tres_list, slurmdb_tres_add)) &&
	     commit)
		db_query_commit(errors, auth);

	FREE_NULL_LIST(tres_list);

	return SLURM_SUCCESS;
}

extern int op_handler_tres(const char *context_id, http_request_method_t method,
			   data_t *parameters, data_t *query, int tag,
			   data_t *resp, void *auth)
{
	if (method == HTTP_REQUEST_GET)
		return _dump_tres(resp, auth);
	else if (method == HTTP_REQUEST_POST)
		return _update_tres(query, resp, auth, (tag != CONFIG_OP_TAG));
	else
		return ESLURM_REST_INVALID_QUERY;
}

extern void init_op_tres(void)
{
	bind_operation_handler("/slurmdb/v0.0.38/tres/", op_handler_tres, 0);
}

extern void destroy_op_tres(void)
{
	unbind_operation_handler(op_handler_tres);
}
