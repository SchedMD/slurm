/*****************************************************************************\
 *  nodes.c - Slurm REST API nodes http operations handlers
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

#include "src/common/data.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"

#include "src/slurmrestd/operations.h"

#include "api.h"

static void _update_node(ctxt_t *ctxt, char *name)
{
	int rc;
	data_t *ppath = data_set_list(data_new());
	update_node_msg_t *node_msg = xmalloc(sizeof(*node_msg));

	slurm_init_update_node_msg(node_msg);

	if ((rc = DATA_PARSE(ctxt->parser, UPDATE_NODE_MSG, *node_msg,
			     ctxt->query, ppath)))
		goto cleanup;

	if (node_msg->node_names) {
		resp_warn(ctxt, __func__,
			  "node_names field %s ignored for singular node update",
			  node_msg->node_names);
		xfree(node_msg->node_names);
	}

	node_msg->node_names = xstrdup(name);

	if (!rc && slurm_update_node(node_msg))
		resp_error(ctxt, errno, __func__,
			   "Failure to update node %s", name);

cleanup:
	slurm_free_update_node_msg(node_msg);
	FREE_NULL_DATA(ppath);
}

static void _dump_nodes(ctxt_t *ctxt, char *name)
{
	openapi_nodes_query_t query = {0};
	node_info_msg_t *node_info_ptr = NULL;
	openapi_resp_node_info_msg_t resp = {0};

	if (DATA_PARSE(ctxt->parser, OPENAPI_NODES_QUERY, query, ctxt->query,
		       ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing query.");
		goto done;
	}

	if (!query.show_flags)
		query.show_flags = SHOW_ALL | SHOW_DETAIL | SHOW_MIXED;

	if (!name) {
		if ((slurm_load_node(query.update_time, &node_info_ptr,
				     query.show_flags))) {
			resp_error(ctxt, errno, __func__,
				   "Failure to query nodes");
			goto done;
		}
	} else {
		if (slurm_load_node_single(&node_info_ptr, name,
					   query.show_flags) ||
		    !node_info_ptr || !node_info_ptr->record_count) {
			resp_error(ctxt, errno, __func__,
				   "Failure to query node %s", name);
			goto done;
		}
	}

	if (node_info_ptr && node_info_ptr->record_count) {
		int rc;
		partition_info_msg_t *part_info_ptr = NULL;

		if ((rc = slurm_load_partitions(query.update_time,
						&part_info_ptr,
						query.show_flags))) {
			resp_error(ctxt, rc, __func__,
				   "Unable to query partitions");
			goto done;
		}

		slurm_populate_node_partitions(node_info_ptr, part_info_ptr);
		slurm_free_partition_info_msg(part_info_ptr);

		resp.last_update = node_info_ptr->last_update;
		resp.nodes = node_info_ptr;
	}

	DATA_DUMP(ctxt->parser, OPENAPI_NODES_RESP, resp, ctxt->resp);

done:
	slurm_free_node_info_msg(node_info_ptr);
}

static int _op_handler_nodes(openapi_ctxt_t *ctxt)
{
	int rc = SLURM_SUCCESS;

	if (ctxt->method == HTTP_REQUEST_GET) {
		_dump_nodes(ctxt, NULL);
	} else {
		rc = resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
				"Unsupported HTTP method requested: %s",
				get_http_method_string(ctxt->method));
	}

	return rc;
}

static int _op_handler_node(openapi_ctxt_t *ctxt)
{
	openapi_node_param_t params = {0};

	if (DATA_PARSE(ctxt->parser, OPENAPI_NODE_PARAM, params,
		       ctxt->parameters, ctxt->parent_path)) {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Rejecting request. Failure parsing parameters");
		goto done;
	}

	if (ctxt->method == HTTP_REQUEST_GET) {
		_dump_nodes(ctxt, params.node_name);
	} else if (ctxt->method == HTTP_REQUEST_DELETE) {
		update_node_msg_t *node_msg = xmalloc(sizeof(*node_msg));
		slurm_init_update_node_msg(node_msg);

		SWAP(node_msg->node_names, params.node_name);

		if (slurm_delete_node(node_msg)) {
			resp_error(ctxt, errno, __func__,
				   "Failure to update node %s",
				   node_msg->node_names);
		}

		slurm_free_update_node_msg(node_msg);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
		_update_node(ctxt, params.node_name);
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

done:
	xfree(params.node_name);
	return SLURM_SUCCESS;
}

extern void init_op_nodes(void)
{
	bind_handler("/slurm/{data_parser}/nodes/", _op_handler_nodes);
	bind_handler("/slurm/{data_parser}/node/{node_name}", _op_handler_node);
}

extern void destroy_op_nodes(void)
{
	unbind_operation_ctxt_handler(_op_handler_nodes);
}
