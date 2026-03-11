/*****************************************************************************\
 *  partitions.c - Slurm REST API partitions http operations handlers
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

#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

typedef struct {
	openapi_ctxt_t *ctxt;
	partition_info_msg_t *part_info_ptr;
	uint16_t index;
} foreach_create_update_part_t;

static void _warn_on_ignore_values(update_part_msg_t *update_part,
				   uint16_t index, openapi_ctxt_t *ctxt)
{
	char *warn_str = NULL;
	char *pos = NULL;

	if (update_part->cluster_name)
		xstrcatat(warn_str, &pos, "cluster");
	if (update_part->cr_type)
		xstrfmtcatat(warn_str, &pos, "%s%s", (warn_str ? ", " : ""),
			     "select_type");
	if (update_part->resume_timeout)
		xstrfmtcatat(warn_str, &pos, "%s%s", (warn_str ? ", " : ""),
			     "timeouts/resume");
	if (update_part->suspend_time)
		xstrfmtcatat(warn_str, &pos, "%s%s", (warn_str ? ", " : ""),
			     "suspend_time");
	if (update_part->suspend_timeout)
		xstrfmtcatat(warn_str, &pos, "%s%s", (warn_str ? ", " : ""),
			     "timeouts/suspend");
	if (update_part->total_cpus)
		xstrfmtcatat(warn_str, &pos, "%s%s", (warn_str ? ", " : ""),
			     "cpus/total");
	if (update_part->total_nodes)
		xstrfmtcatat(warn_str, &pos, "%s%s", (warn_str ? ", " : ""),
			     "nodes/total");
	if (update_part->tres_fmt_str)
		xstrfmtcatat(warn_str, &pos, "%s%s", (warn_str ? ", " : ""),
			     "tres/configured");

	if (warn_str)
		resp_warn(
			ctxt, __func__,
			"The partition description at index %u contains the following ignored fields: %s",
			index, warn_str);
	xfree(warn_str);
}

static int _foreach_create_update_partition(void *x, void *arg)
{
	update_part_msg_t *update_part = x;
	foreach_create_update_part_t *args = arg;
	partition_info_msg_t *part_info_ptr = args->part_info_ptr;
	bool part_exists = false;
	int rc = SLURM_SUCCESS;

	xassert(update_part);

	if (!update_part->name) {
		resp_error(
			args->ctxt, ESLURM_INVALID_PARTITION_NAME, __func__,
			"The partition description at index %u must specify a name. Skipping",
			args->index);
		args->index++;
		return 0; /* Don't break list_for_each */
	}

	_warn_on_ignore_values(update_part, args->index, args->ctxt);

	/*
	 * RPC does not use nodesets field, append it to nodes instead unless
	 * it is "ALL" only set by the controller when partition is configured
	 * with Nodes=ALL. Nodesets should not be set to ALL by the user.
	 */
	if (update_part->nodesets && xstrcmp(update_part->nodesets, "ALL")) {
		xstrfmtcat(update_part->nodes, "%s%s",
			   (update_part->nodes ? "," : ""),
			   update_part->nodesets);
	}

	if (!xstrcasecmp(update_part->name, "default")) {
		part_exists = true; /* updating the default partition config */
	} else if (part_info_ptr->record_count) {
		partition_info_t *part_ptr = part_info_ptr->partition_array;

		for (int i = 0; i < part_info_ptr->record_count; i++) {
			if (xstrcmp(update_part->name, part_ptr[i].name))
				continue;

			part_exists = true;
			break;
		}
	}

	errno = SLURM_SUCCESS;

	if (part_exists)
		rc = slurm_update_partition(update_part);
	else
		rc = slurm_create_partition(update_part);

	if (rc) {
		char *func_name = part_exists ? "slurm_update_partition" :
						"slurm_create_partition";
		char *action_str = part_exists ? "update" : "create";

		/* slurm_[update|create]_partition both set error in errno */
		if ((rc == SLURM_ERROR) && errno)
			rc = errno;

		resp_error(
			args->ctxt, rc, func_name,
			"Failed to %s partition %s with description at index %u",
			action_str, update_part->name, args->index);
	}

	args->index++;
	return 0; /* Don't break list_for_each */
}

static int _partitions_mod(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	openapi_part_mod_req_t req = { 0 };
	openapi_part_mod_req_t *req_ptr = &req; /* required for free macro */
	partition_info_msg_t *part_info_ptr = NULL;
	foreach_create_update_part_t args = {
		.ctxt = ctxt,
	};

	if (DATA_PARSE(ctxt->parser, OPENAPI_PARTITIONS_MOD_REQ, req,
		       ctxt->query, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing query");
		goto done;
	}

	if (!req.partition_list) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "A partition description must be provided.");
		goto done;
	}

	rc = slurm_load_partitions(0, &part_info_ptr, 0);
	if (rc) {
		if ((rc == SLURM_ERROR) && errno)
			rc = errno;
		resp_error(ctxt, rc, "slurm_load_partitions()",
			   "Unable to query partitions");
		goto done;
	}

	args.part_info_ptr = part_info_ptr;
	list_for_each(req.partition_list, _foreach_create_update_partition,
		      &args);

done:
	slurm_free_partition_info_msg(part_info_ptr);
	FREE_OPENAPI_RESP_COMMON_CONTENTS(req_ptr);
	FREE_NULL_LIST(req.partition_list);
	return rc ? rc : ctxt->rc;
}

static int _partitions_get(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;
	partition_info_msg_t *part_info_ptr = NULL;
	openapi_partitions_query_t query = {0};
	openapi_resp_partitions_info_msg_t resp = {0};

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

extern int op_handler_partitions(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	switch (ctxt->method) {
	case HTTP_REQUEST_GET:
		rc = _partitions_get(ctxt);
		break;
	case HTTP_REQUEST_POST:
		rc = _partitions_mod(ctxt);
		break;
	default:
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

	return rc;
}

static int _partition_get(openapi_ctxt_t *ctxt, char *partition_name)
{
	openapi_partitions_query_t query = {0};
	partition_info_msg_t *part_info_ptr = NULL;
	int rc = SLURM_SUCCESS;

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
			if (!xstrcasecmp(partition_name, n)) {
				part = &part_info_ptr->partition_array[i];
				break;
			}
		}

		if (!part) {
			resp_warn(ctxt, __func__, "Unable to find partition %s",
				  partition_name);
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
	return rc;
}

extern int op_handler_partition(openapi_ctxt_t *ctxt)
{
	openapi_partition_param_t params = { 0 };
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

	rc = _partition_get(ctxt, params.partition_name);

done:
	xfree(params.partition_name);
	return rc;
}
