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

enum {
	TAG_ALL_QOS = 0,
	TAG_SINGLE_QOS,
};

#define MAGIC_FOREACH_SEARCH 0xde0d0ee8
typedef struct {
	int magic; /* MAGIC_FOREACH_SEARCH */
	ctxt_t *ctxt;
	slurmdb_qos_cond_t *qos_cond;
} foreach_query_search_t;

static data_for_each_cmd_t _foreach_query_search(const char *key,
						 data_t *data,
						 void *arg)
{
	foreach_query_search_t *args = arg;
	xassert(args->magic == MAGIC_FOREACH_SEARCH);

	if (!xstrcasecmp("with_deleted", key)) {
		if (data_convert_type(data, DATA_TYPE_BOOL) != DATA_TYPE_BOOL) {
			resp_error(args->ctxt, ESLURM_REST_INVALID_QUERY,
				   __func__,
				   "Field %s must be a Boolean instead of %s",
				   key,
				   data_type_to_string(data_get_type(data)));
			return DATA_FOR_EACH_FAIL;
		}

		if (data->data.bool_u)
			args->qos_cond->with_deleted = true;
		else
			args->qos_cond->with_deleted = false;

		return DATA_FOR_EACH_CONT;
	}

	resp_error(args->ctxt, ESLURM_REST_INVALID_QUERY, __func__,
		   "Unknown Query field: %s", key);
	return DATA_FOR_EACH_FAIL;
}

static void _dump_qos(ctxt_t *ctxt, list_t *qos_list, char *qos_name)
{
	slurmdb_qos_rec_t *qos;
	ListIterator iter = list_iterator_create(qos_list);
	data_t *dqos_list = data_key_set(ctxt->resp, "qos");

	if (data_get_type(dqos_list) != DATA_TYPE_LIST)
		data_set_list(dqos_list);

	/*
	 * We are forced to use iterator here due to calls inside of
	 * _foreach_qos() that attempt to lock qos_list.
	 */
	while ((qos = list_next(iter))) {
		if (!qos_name || !xstrcmp(qos->name, qos_name)) {
			data_t *q = data_list_append(dqos_list);

			debug("%s: [%s] dumping QOS %s",
			      __func__, ctxt->id,
			      (qos_name ? qos_name : ""));

			if (DATA_DUMP(ctxt->parser, QOS, *qos, q))
				break;
		}
	}

	list_iterator_destroy(iter);
}

static int _foreach_delete_qos(void *x, void *arg)
{
	char *qos = x;
	data_t *qoslist = arg;

	data_set_string(data_list_append(qoslist), qos);

	return DATA_FOR_EACH_CONT;
}

static void _delete_qos(ctxt_t *ctxt, slurmdb_qos_cond_t *qos_cond)
{
	List qos_list = NULL;
	data_t *dremoved =
		data_set_list(data_key_set(ctxt->resp, "removed_qos"));

	if (!db_query_list(ctxt, &qos_list, slurmdb_qos_remove, qos_cond) &&
	    qos_list)
		list_for_each(qos_list, _foreach_delete_qos, dremoved);

	if (!ctxt->rc)
		db_query_commit(ctxt);

	FREE_NULL_LIST(qos_list);
}

/* If the QOS already exists, update it. If not, create it */
static int _foreach_update_qos(void *x, void *arg)
{
	ctxt_t *ctxt = arg;
	int rc;
	slurmdb_qos_rec_t *qos = x, *found_qos = NULL;
	slurmdb_qos_cond_t cond = { 0 };

	xassert(ctxt->magic == MAGIC_CTXT);

	/* Search for a QOS with the same id and/or name, if set */
	if (qos->id || qos->name) {
		List qos_list = NULL;

		if (qos->id) {
			/* Need to free string copy of id with xfree_ptr */
			cond.id_list = list_create(xfree_ptr);
			list_append(cond.id_list,
				    xstrdup_printf("%u", qos->id));
		}
		if (qos->name) {
			/* Temporarily alias/borrow qos->name into cond */
			cond.name_list = list_create(NULL);
			list_append(cond.name_list, qos->name);
		}

		/* See if QOS already exists */
		rc = db_query_list_xempty(ctxt, &qos_list, slurmdb_qos_get,
					  &cond);

		if (!rc && qos_list && (list_count(qos_list) == 1))
			found_qos = list_pop(qos_list);

		FREE_NULL_LIST(qos_list);
	}

	if (!found_qos && qos->id) {
		/* No QOS exists for qos->id. Can't update */
		rc = resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				"QOS was not found for the requested ID");
	} else if (!found_qos && !qos->name) {
		/* Can't create a QOS without a name */
		rc = resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				"Cannot create a QOS without a name");
	} else if (!found_qos) {
		List qos_add_list = list_create(NULL);

		/* The QOS was not found, so create a new QOS */
		debug("%s: adding qos request: name=%s description=%s",
		      __func__, qos->name, qos->description);

		list_append(qos_add_list, qos);
		rc = db_query_rc(ctxt, qos_add_list, slurmdb_qos_add);

		FREE_NULL_LIST(qos_add_list);
	} else {
		/* Exactly one QOS was found: let's update it */
		debug("%s: modifying qos request: id=%u name=%s",
		      __func__, found_qos->id, found_qos->name);

		xassert(!qos->name || !xstrcmp(found_qos->name, qos->name));
		xassert(!qos->id || (found_qos->id == qos->id));

		if (!qos->id)
			qos->id = found_qos->id;

		rc = db_modify_rc(ctxt, &cond, qos, slurmdb_qos_modify);
	}

	slurmdb_destroy_qos_rec(found_qos);
	FREE_NULL_LIST(cond.id_list);
	FREE_NULL_LIST(cond.name_list);

	return (rc != SLURM_SUCCESS) ? DATA_FOR_EACH_FAIL : DATA_FOR_EACH_CONT;
}

static void _update_qos(ctxt_t *ctxt, bool commit)
{
	List qos_list = NULL;
	data_t *parent_path = NULL;
	data_t *dqos;

	if (!(dqos = get_query_key_list("QOS", ctxt, &parent_path))) {
		resp_warn(
			ctxt, __func__,
			"ignoring empty or non-existant QOS array for update");
		FREE_NULL_DATA(parent_path);
		return;
	}

	qos_list = list_create(slurmdb_destroy_qos_rec);

	if (!DATA_PARSE(ctxt->parser, QOS_LIST, qos_list, dqos, parent_path)) {
		if (qos_list)
			list_for_each_ro(qos_list, _foreach_update_qos, ctxt);

		if (!ctxt->rc && commit)
			db_query_commit(ctxt);
	}

	FREE_NULL_LIST(qos_list);
	xfree(parent_path);
}

extern int op_handler_qos(const char *context_id, http_request_method_t method,
			  data_t *parameters, data_t *query, int tag,
			  data_t *resp, void *auth)
{
	char *qos_name = NULL;
	slurmdb_qos_cond_t qos_cond = { 0 };
	List qos_list = NULL;
	ctxt_t *ctxt = init_connection(context_id, method, parameters, query,
				       tag, resp, auth);

	if (ctxt->rc)
		goto cleanup;

	if (method == HTTP_REQUEST_GET) {
		/* Update qos_cond with requested search parameters */
		if (query && data_get_dict_length(query)) {
			foreach_query_search_t args = {
				.magic = MAGIC_FOREACH_SEARCH,
				.ctxt = ctxt,
				.qos_cond = &qos_cond,
			};

			if (data_dict_for_each(query, _foreach_query_search,
					       &args) < 0)
				goto cleanup;
		}

		if (db_query_list(ctxt, &qos_list, slurmdb_qos_get, &qos_cond))
			goto cleanup;
	}

	if (tag == TAG_SINGLE_QOS) {
		qos_name = get_str_param("qos_name", ctxt);

		if (qos_name) {
			qos_cond.name_list = list_create(NULL);
			list_append(qos_cond.name_list, qos_name);
		} else {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, "qos_name",
				   "QOS name must be given for single QOS query");
			goto cleanup;
		}
	}

	if (method == HTTP_REQUEST_GET)
		_dump_qos(ctxt, qos_list, qos_name);
	else if (method == HTTP_REQUEST_DELETE && (tag == TAG_SINGLE_QOS))
		_delete_qos(ctxt, &qos_cond);
	else if (method == HTTP_REQUEST_POST &&
		 ((tag == TAG_ALL_QOS) || (tag == CONFIG_OP_TAG)))
		_update_qos(ctxt, (tag != CONFIG_OP_TAG));
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(method));
	}

cleanup:
	FREE_NULL_LIST(qos_cond.name_list);
	FREE_NULL_LIST(qos_list);
	return fini_connection(ctxt);
}

extern void init_op_qos(void)
{
	bind_operation_handler("/slurmdb/v0.0.39/qos/", op_handler_qos,
			       TAG_ALL_QOS);
	bind_operation_handler("/slurmdb/v0.0.39/qos/{qos_name}",
			       op_handler_qos, TAG_SINGLE_QOS);
}

extern void destroy_op_qos(void)
{
	unbind_operation_handler(op_handler_qos);
}
