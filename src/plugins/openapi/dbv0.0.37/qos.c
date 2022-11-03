/*****************************************************************************\
 *  qos.c - Slurm REST API accounting QOS http operations handlers
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

#include "src/plugins/openapi/dbv0.0.37/api.h"

enum {
	TAG_ALL_QOS = 0,
	TAG_SINGLE_QOS,
};

static int _foreach_qos(slurmdb_qos_rec_t *qos, data_t *dqos_list,
			List qos_list, List g_tres_list)
{
	parser_env_t penv = {
		.g_qos_list = qos_list,
		.g_tres_list = g_tres_list,
	};

	return dump(PARSE_QOS, qos, data_set_dict(data_list_append(dqos_list)),
		    &penv);
}

static int _dump_qos(data_t *resp, void *auth, List g_qos_list, char *qos_name)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	slurmdb_qos_rec_t *qos;
	ListIterator iter = list_iterator_create(g_qos_list);
	data_t *dqos_list = data_set_list(data_key_set(resp, "QOS"));
	List tres_list = NULL;
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};

	rc = db_query_list(errors, auth, &tres_list, slurmdb_tres_get,
			   &tres_cond);

	/*
	 * We are forced to use iterator here due to calls inside of
	 * _foreach_qos() that attempt to lock qos_list.
	 */
	while ((!rc) && (qos = list_next(iter)))
		if (!qos_name || !xstrcmp(qos->name, qos_name))
			rc = _foreach_qos(qos, dqos_list, g_qos_list,
					  tres_list);

	list_iterator_destroy(iter);
	FREE_NULL_LIST(tres_list);

	return SLURM_SUCCESS;
}

static int _foreach_delete_qos(void *x, void *arg)
{
	char *qos = x;
	data_t *qoslist = arg;

	data_set_string(data_list_append(qoslist), qos);

	return DATA_FOR_EACH_CONT;
}

static int _delete_qos(data_t *resp, void *auth, data_t *errors,
		       slurmdb_qos_cond_t *qos_cond)
{
	int rc = SLURM_SUCCESS;
	List qos_list = NULL;

	if (!(rc = db_query_list(errors, auth, &qos_list, slurmdb_qos_remove,
				 qos_cond)) &&
	    (list_for_each(qos_list, _foreach_delete_qos,
			   data_set_list(data_key_set(resp, "removed_qos"))) <
	     0)) {
		resp_error(errors, ESLURM_REST_INVALID_QUERY,
			   "unable to delete QOS", NULL);
	}

	if (!rc)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(qos_list);

	return rc;
}

#define MAGIC_FOREACH_UP_QOS 0xdaebfae8
typedef struct {
	int magic;
	List qos_list;
	List g_tres_list;
	data_t *errors;
	rest_auth_context_t *auth;
} foreach_update_qos_t;

static data_for_each_cmd_t _foreach_update_qos(data_t *data, void *arg)
{
	foreach_update_qos_t *args = arg;
	slurmdb_qos_rec_t *qos;
	parser_env_t penv = {
		.auth = args->auth,
		.g_tres_list = args->g_tres_list,
	};

	xassert(args->magic == MAGIC_FOREACH_UP_QOS);

	if (data_get_type(data) != DATA_TYPE_DICT) {
		resp_error(args->errors, ESLURM_REST_INVALID_QUERY,
			   "each QOS entry must be a dictionary", NULL);
		return DATA_FOR_EACH_FAIL;
	}

	qos = xmalloc(sizeof(slurmdb_qos_rec_t));

	if (parse(PARSE_QOS, qos, data, args->errors, &penv)) {
		slurmdb_destroy_qos_rec(qos);
		return DATA_FOR_EACH_FAIL;
	} else {
		(void)list_append(args->qos_list, qos);
		return DATA_FOR_EACH_CONT;
	}
}

static int _update_qos(data_t *query, data_t *resp, void *auth, bool commit)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	foreach_update_qos_t args = {
		.magic = MAGIC_FOREACH_UP_QOS,
		.auth = auth,
		.errors = errors,
		.qos_list = list_create(slurmdb_destroy_qos_rec),
	};
	slurmdb_tres_cond_t tres_cond = {
		.with_deleted = 1,
	};
	data_t *dqos = get_query_key_list("QOS", errors, query);

	if (!dqos)
		return ESLURM_REST_INVALID_QUERY;

	if (!(rc = db_query_list(errors, auth, &args.g_tres_list,
				 slurmdb_tres_get, &tres_cond)) &&
	    (data_list_for_each(dqos, _foreach_update_qos, &args) < 0))
		rc = ESLURM_REST_INVALID_QUERY;

	if (!rc &&
	    !(rc = db_query_rc(errors, auth, args.qos_list, slurmdb_qos_add)) &&
	    commit)
		rc = db_query_commit(errors, auth);

	FREE_NULL_LIST(args.qos_list);

	return rc;
}

extern int op_handler_qos(const char *context_id, http_request_method_t method,
			  data_t *parameters, data_t *query, int tag,
			  data_t *resp, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(resp);
	List g_qos_list = NULL;
	char *qos_name = NULL;
	slurmdb_qos_cond_t qos_cond = {
		.with_deleted = 1,
	};

	if (method == HTTP_REQUEST_GET) {
		/* need global list of QOS to dump even a single QOS */
		rc = db_query_list(errors, auth, &g_qos_list, slurmdb_qos_get,
				   &qos_cond);
	}

	if (!rc && (tag == TAG_SINGLE_QOS)) {
		qos_name = get_str_param("qos_name", errors, parameters);

		if (qos_name) {
			qos_cond.name_list = list_create(NULL);
			list_append(qos_cond.name_list, qos_name);
		} else
			rc = ESLURM_REST_INVALID_QUERY;
	}

	if (rc)
		/* no-op */;
	else if (method == HTTP_REQUEST_GET)
		rc = _dump_qos(resp, auth, g_qos_list, qos_name);
	else if (method == HTTP_REQUEST_DELETE && (tag == TAG_SINGLE_QOS))
		rc = _delete_qos(resp, auth, errors, &qos_cond);
	else if (method == HTTP_REQUEST_POST &&
		 ((tag == TAG_ALL_QOS) || (tag == CONFIG_OP_TAG)))
		rc = _update_qos(query, resp, auth, (tag != CONFIG_OP_TAG));
	else
		rc = ESLURM_REST_INVALID_QUERY;

	FREE_NULL_LIST(qos_cond.name_list);
	FREE_NULL_LIST(g_qos_list);

	return rc;
}

extern void init_op_qos(void)
{
	bind_operation_handler("/slurmdb/v0.0.37/qos/", op_handler_qos,
			       TAG_ALL_QOS);
	bind_operation_handler("/slurmdb/v0.0.37/qos/{qos_name}",
			       op_handler_qos, TAG_SINGLE_QOS);
}

extern void destroy_op_qos(void)
{
	unbind_operation_handler(op_handler_qos);
}
