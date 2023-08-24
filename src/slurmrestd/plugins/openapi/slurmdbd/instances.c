/*****************************************************************************\
 *  instances.c - Slurm REST API acct instances http operations handlers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Ben Glines <ben.glines@schedmd.com>
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"
#include "api.h"

static void _dump_instance_cond(ctxt_t *ctxt, slurmdb_instance_cond_t *cond,
				bool only_one)
{
	List instance_list = NULL;

	if (db_query_list(ctxt, &instance_list, slurmdb_instances_get, cond))
		goto cleanup;

	if (only_one && (list_count(instance_list) > 1)) {
		resp_error(ctxt, ESLURM_DATA_AMBIGUOUS_QUERY, __func__,
			   "Ambiguous request: More than 1 instance would have been dumped.");
		goto cleanup;
	}

	if (instance_list)
		DUMP_OPENAPI_RESP_SINGLE(OPENAPI_INSTANCES_RESP, instance_list, ctxt);

cleanup:
	FREE_NULL_LIST(instance_list);
}

static int _op_handler_instance(ctxt_t *ctxt)
{
	slurmdb_instance_cond_t *instance_cond = NULL;

	if (DATA_PARSE(ctxt->parser, INSTANCE_CONDITION_PTR, instance_cond,
		       ctxt->query, ctxt->parent_path))
		goto cleanup;

	if (ctxt->method == HTTP_REQUEST_GET)
		_dump_instance_cond(ctxt, instance_cond, true);
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	slurmdb_destroy_instance_cond(instance_cond);
	return SLURM_SUCCESS;
}

static int _op_handler_instances(ctxt_t *ctxt)
{
	slurmdb_instance_cond_t *instance_cond = NULL;

	if (DATA_PARSE(ctxt->parser, INSTANCE_CONDITION_PTR, instance_cond,
		       ctxt->query, ctxt->parent_path))
		goto cleanup;

	if (ctxt->method == HTTP_REQUEST_GET)
		_dump_instance_cond(ctxt, instance_cond, false);
	else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

cleanup:
	slurmdb_destroy_instance_cond(instance_cond);
	return SLURM_SUCCESS;
}

extern void init_op_instances(void)
{
	bind_handler("/slurmdb/{data_parser}/instances/",
		     _op_handler_instances, 0);
	bind_handler("/slurmdb/{data_parser}/instance/",
		     _op_handler_instance, 0);
}

extern void destroy_op_instances(void)
{
	unbind_operation_ctxt_handler(_op_handler_instances);
	unbind_operation_ctxt_handler(_op_handler_instance);
}
