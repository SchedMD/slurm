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

#include "config.h"

#define _GNU_SOURCE

#include <search.h>
#include <stdint.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/ref.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.36/api.h"

typedef enum {
	URL_TAG_UNKNOWN = 0,
	URL_TAG_NODE,
	URL_TAG_NODES,
} url_tag_t;

static const char *_get_long_node_state(uint32_t state)
{
	switch (state & NODE_STATE_BASE) {
	case NODE_STATE_DOWN:
		return "down";
	case NODE_STATE_IDLE:
		return "idle";
	case NODE_STATE_ALLOCATED:
		return "allocated";
	case NODE_STATE_ERROR:
		return "error";
	case NODE_STATE_MIXED:
		return "mixed";
	case NODE_STATE_FUTURE:
		return "future";
	default:
		return "invalid";
	}
}

static int _dump_node(data_t *p, node_info_t *node)
{
	data_t *d;

	if (!node->name) {
		debug2("%s: ignoring defunct node: %s",
		       __func__, node->node_hostname);
		return SLURM_SUCCESS;
	}

	d = data_set_dict(data_list_append(p));

	data_set_string(data_key_set(d, "architecture"), node->arch);
	data_set_string(data_key_set(d, "burstbuffer_network_address"),
			node->bcast_address);
	data_set_int(data_key_set(d, "boards"), node->boards);
	data_set_int(data_key_set(d, "boot_time"), node->boot_time);
	/* cluster_name intentionally omitted */
	data_set_string(data_key_set(d, "comment"), node->comment);
	data_set_int(data_key_set(d, "cores"), node->cores);
	/* core_spec_cnt intentionally omitted */
	data_set_int(data_key_set(d, "cpu_binding"), node->cpu_bind);
	data_set_int(data_key_set(d, "cpu_load"), node->cpu_load);
	data_set_int(data_key_set(d, "free_memory"), node->free_mem);
	data_set_int(data_key_set(d, "cpus"), node->cpus);
	/* cpu_spec_list intentionally omitted */
	/* energy intentionally omitted */
	/* ext_sensors intentionally omitted */
	/* power intentionally omitted */
	data_set_string(data_key_set(d, "features"), node->features);
	data_set_string(data_key_set(d, "active_features"), node->features_act);
	data_set_string(data_key_set(d, "gres"), node->gres);
	data_set_string(data_key_set(d, "gres_drained"), node->gres_drain);
	data_set_string(data_key_set(d, "gres_used"), node->gres_used);
	data_set_string(data_key_set(d, "mcs_label"), node->mcs_label);
	/* mem_spec_limit intentionally omitted */
	data_set_string(data_key_set(d, "name"), node->name);
	data_set_string(data_key_set(d, "next_state_after_reboot"),
			_get_long_node_state(node->next_state));
	data_set_string(data_key_set(d, "address"), node->node_addr);
	data_set_string(data_key_set(d, "hostname"), node->node_hostname);
	data_set_string(data_key_set(d, "state"),
			_get_long_node_state(node->node_state));
	data_set_string(data_key_set(d, "operating_system"), node->os);
	if (node->owner == NO_VAL) {
		data_set_null(data_key_set(d, "owner"));
	} else {
		data_set_string_own(data_key_set(d, "owner"),
				    uid_to_string_or_null(node->owner));
	}
	// FIXME: data_set_string(data_key_set(d, "partitions"), node->partitions);
	data_set_int(data_key_set(d, "port"), node->port);
	data_set_int(data_key_set(d, "real_memory"), node->real_memory);
	data_set_string(data_key_set(d, "reason"), node->reason);
	data_set_int(data_key_set(d, "reason_changed_at"), node->reason_time);
	data_set_string_own(data_key_set(d, "reason_set_by_user"),
			    uid_to_string_or_null(node->reason_uid));
	// TODO: dynamic_plugin_data_t *select_nodeinfo;  /* opaque data structure,
	data_set_int(data_key_set(d, "slurmd_start_time"), node->slurmd_start_time);
	data_set_int(data_key_set(d, "sockets"), node->sockets);
	data_set_int(data_key_set(d, "threads"), node->threads);
	data_set_int(data_key_set(d, "temporary_disk"), node->tmp_disk);
	data_set_int(data_key_set(d, "weight"), node->weight);
	data_set_string(data_key_set(d, "tres"), node->tres_fmt_str);
	data_set_string(data_key_set(d, "slurmd_version"), node->version);

	return SLURM_SUCCESS;
}

static int _op_handler_nodes(const char *context_id,
			     http_request_method_t method, data_t *parameters,
			     data_t *query, int tag, data_t *d, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(d);
	data_t *nodes = data_set_list(data_key_set(d, "nodes"));
	node_info_msg_t *node_info_ptr = NULL;

	if (tag == URL_TAG_NODES)
		rc = slurm_load_node(0, &node_info_ptr, SHOW_ALL|SHOW_DETAIL);
	else if (tag == URL_TAG_NODE) {
		const data_t *node_name = data_key_get_const(parameters,
							     "node_name");
		char *name = NULL;

		if (!node_name || data_get_string_converted(node_name, &name))
			rc = ESLURM_INVALID_NODE_NAME;
		else
			rc = slurm_load_node_single(&node_info_ptr, name,
						       SHOW_ALL|SHOW_DETAIL);

		xfree(name);
	} else
		rc = SLURM_ERROR;

	if (!rc && node_info_ptr)
		for (int i = 0; !rc && i < node_info_ptr->record_count; i++)
			rc = _dump_node(nodes,
					   &node_info_ptr->node_array[i]);

	if (!node_info_ptr || node_info_ptr->record_count == 0)
		rc = ESLURM_INVALID_NODE_NAME;

	if (rc) {
		data_t *e = data_set_dict(data_list_append(errors));
		data_set_string(data_key_set(e, "error"),
				slurm_strerror(rc));
		data_set_int(data_key_set(e, "errno"), rc);
	}

	slurm_free_node_info_msg(node_info_ptr);
	return rc;
}

extern void init_op_nodes(void)
{
	bind_operation_handler("/slurm/v0.0.36/nodes/", _op_handler_nodes,
			       URL_TAG_NODES);
	bind_operation_handler("/slurm/v0.0.36/node/{node_name}",
			       _op_handler_nodes, URL_TAG_NODE);
}

extern void destroy_op_nodes(void)
{
	unbind_operation_handler(_op_handler_nodes);
}
