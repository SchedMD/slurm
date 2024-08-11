/*****************************************************************************\
 *  qos.c - Slurm REST API accounting QOS http operations handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
#include "src/common/slurmdbd_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/operations.h"
#include "api.h"

/*
 * Modify request for QOS will ignore an empty List. This allows slurmdbd to
 * know we want this field to be empty.
 */
#define EMPTY_QOS_ID_ENTRY "\'\'"

/* If the QOS already exists, update it. If not, create it */
static int _foreach_update_qos(void *x, void *arg)
{
	ctxt_t *ctxt = arg;
	int rc;
	slurmdb_qos_rec_t *qos = x, *found_qos = NULL;
	slurmdb_qos_cond_t cond = { 0 };

	/* Search for a QOS with the same id and/or name, if set */
	if (qos->id || qos->name) {
		list_t *qos_list = NULL;

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
		list_t *qos_add_list = list_create(NULL);

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

		if (qos->preempt_list && list_is_empty(qos->preempt_list) &&
		    found_qos->preempt_list &&
		    !list_is_empty(found_qos->preempt_list)) {
			/*
			 * If the new QOS list is empty but the QOS had a
			 * preempt list before, then we need to set this special
			 * entry to notify slurmdbd that this is explicilty
			 * empty and not a no change request.
			 *
			 * If we always set this value, then slurmdbd will
			 * return ESLURM_QOS_PREEMPTION_LOOP.
			 */
			list_append(qos->preempt_list, EMPTY_QOS_ID_ENTRY);
		}

		rc = db_modify_rc(ctxt, &cond, qos, slurmdb_qos_modify);
	}

	slurmdb_destroy_qos_rec(found_qos);
	FREE_NULL_LIST(cond.id_list);
	FREE_NULL_LIST(cond.name_list);

	return (rc != SLURM_SUCCESS) ? DATA_FOR_EACH_FAIL : DATA_FOR_EACH_CONT;
}

extern int update_qos(ctxt_t *ctxt, bool commit, list_t *qos_list)
{
	if (!(list_for_each_ro(qos_list, _foreach_update_qos, ctxt) < 0) &&
	    !ctxt->rc && commit)
		db_query_commit(ctxt);

	return ctxt->rc;
}

static int _op_handler_qos(ctxt_t *ctxt, slurmdb_qos_cond_t *qos_cond)
{
	list_t *qos_list = NULL;

	if (ctxt->method == HTTP_REQUEST_GET) {
		db_query_list(ctxt, &qos_list, slurmdb_qos_get, qos_cond);
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_SLURMDBD_QOS_RESP, qos_list,
					 ctxt);
	} else if (ctxt->method == HTTP_REQUEST_DELETE) {
		if (!qos_cond->name_list ||
		    list_is_empty(qos_cond->name_list)) {
			resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_MODIFY, __func__,
				   "QOS name must be provided for DELETE");
			goto cleanup;
		}

		(void) db_query_list(ctxt, &qos_list, slurmdb_qos_remove,
				     qos_cond);

		if (qos_list && !ctxt->rc)
			db_query_commit(ctxt);

		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_SLURMDBD_QOS_REMOVED_RESP,
					 qos_list, ctxt);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		openapi_resp_single_t post = { 0 };

		if (!DATA_PARSE(ctxt->parser, OPENAPI_SLURMDBD_QOS_RESP, post,
				ctxt->query, ctxt->parent_path) &&
		    post.response) {
			qos_list = post.response;
			update_qos(ctxt, true, qos_list);
		}
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	FREE_NULL_LIST(qos_list);
	return SLURM_SUCCESS;
}

extern int op_handler_single_qos(ctxt_t *ctxt)
{
	int rc;
	openapi_qos_param_t params = {0};
	openapi_qos_query_t query = {0};
	slurmdb_qos_cond_t *qos_cond = NULL;

	if ((rc = DATA_PARSE(ctxt->parser, OPENAPI_SLURMDBD_QOS_QUERY, query,
			     ctxt->query, ctxt->parent_path)))
		return rc;
	if ((rc = DATA_PARSE(ctxt->parser, OPENAPI_SLURMDBD_QOS_PARAM, params,
			     ctxt->parameters, ctxt->parent_path)))
		return rc;

	qos_cond = xmalloc(sizeof(*qos_cond));
	qos_cond->name_list = list_create(xfree_ptr);
	list_append(qos_cond->name_list, params.name);
	if (query.with_deleted)
		qos_cond->flags |= QOS_COND_FLAG_WITH_DELETED;

	rc = _op_handler_qos(ctxt, qos_cond);

	slurmdb_destroy_qos_cond(qos_cond);

	return rc;
}

extern int op_handler_multi_qos(ctxt_t *ctxt)
{
	int rc;
	slurmdb_qos_cond_t *qos_cond = NULL;

	if (((ctxt->method == HTTP_REQUEST_GET) ||
	     (ctxt->method == HTTP_REQUEST_DELETE)) &&
	    (rc = DATA_PARSE(ctxt->parser, QOS_CONDITION_PTR, qos_cond,
			     ctxt->parameters, ctxt->parent_path)))
		return rc;

	rc = _op_handler_qos(ctxt, qos_cond);

	slurmdb_destroy_qos_cond(qos_cond);

	return rc;
}
