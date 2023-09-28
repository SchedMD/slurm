/*****************************************************************************\
 *  partitions.c - Slurm REST API partitions http operations handlers
 *****************************************************************************
 *  Copyright (C) 2019-2020 SchedMD LLC.
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

#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

extern int _op_handler_partitions(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	partition_info_msg_t *part_info_ptr = NULL;
	openapi_partitions_query_t query = {0};
	openapi_resp_partitions_info_msg_t resp = {0};

	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
		goto done;
	}

	if (DATA_PARSE(ctxt->parser, OPENAPI_PARTITIONS_QUERY, query,
		       ctxt->query, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing query");
		goto done;
	}

	errno = 0;
	if ((rc = slurm_load_partitions(query.update_time, &part_info_ptr,
					query.show_flags))) {
		if ((rc == SLURM_ERROR) && errno)
			rc = errno;

		goto done;
	}

	if (part_info_ptr) {
		resp.last_update = part_info_ptr->last_update;
		resp.partitions = part_info_ptr;
	}

	DATA_DUMP(ctxt->parser, OPENAPI_PARTITION_RESP, resp, ctxt->resp);

done:
	slurm_free_partition_info_msg(part_info_ptr);
	return rc;
}

extern int _op_handler_partition(openapi_ctxt_t *ctxt)
{
	openapi_partition_param_t params = {0};
	openapi_partitions_query_t query = {0};
	partition_info_msg_t *part_info_ptr = NULL;
	int rc = SLURM_SUCCESS;

	if (ctxt->method != HTTP_REQUEST_GET) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
		goto done;
	}

	if (DATA_PARSE(ctxt->parser, OPENAPI_PARTITION_PARAM, params,
		       ctxt->parameters, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing parameters");
		goto done;
	}

	if (DATA_PARSE(ctxt->parser, OPENAPI_PARTITIONS_QUERY, query,
		       ctxt->query, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing query");
		goto done;
	}

	if (!query.show_flags)
		query.show_flags = SHOW_ALL;

	errno = 0;
	if ((rc = slurm_load_partitions(query.update_time, &part_info_ptr,
					query.show_flags))) {
		if ((rc == SLURM_ERROR) && errno)
			rc = errno;

		resp_error(ctxt, rc, __func__, "Unable to query partitions");
		goto done;
	}

	if (part_info_ptr) {
		partition_info_t *part = NULL;

		for (int i = 0; !rc && i < part_info_ptr->record_count; i++) {
			const char *n = part_info_ptr->partition_array[i].name;
			if (!xstrcasecmp(params.partition_name, n)) {
				part = &part_info_ptr->partition_array[i];
				break;
			}
		}

		if (!part) {
			resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				   "Unable to find partition %s",
				   params.partition_name);
		} else {
			partition_info_msg_t p = {
				.last_update = part_info_ptr->last_update,
				.record_count = 1,
				.partition_array = part,
			};
			openapi_resp_partitions_info_msg_t resp = {
				.partitions = &p,
				.last_update = part_info_ptr->last_update,
			};

			DATA_DUMP(ctxt->parser, OPENAPI_PARTITION_RESP, resp,
				  ctxt->resp);
		}
	}

done:
	slurm_free_partition_info_msg(part_info_ptr);
	xfree(params.partition_name);
	return rc;
}

extern void init_op_partitions(void)
{
	bind_handler("/slurm/{data_parser}/partitions/",
		     _op_handler_partitions);
	bind_handler("/slurm/{data_parser}/partition/{partition_name}",
		     _op_handler_partition);
}

extern void destroy_op_partitions(void)
{
	unbind_operation_ctxt_handler(_op_handler_partitions);
}
