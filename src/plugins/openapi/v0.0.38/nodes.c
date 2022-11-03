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
#include "src/interfaces/select.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_defs.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.38/api.h"

typedef enum {
	URL_TAG_UNKNOWN = 0,
	URL_TAG_NODE,
	URL_TAG_NODES,
} url_tag_t;

static char *_get_long_node_state(uint32_t state)
{
	char *state_str = NULL;
	/* caller must free */
	state_str = xstrdup(node_state_base_string(state));
	xstrtolower(state_str);
	return state_str;
}

static void _add_node_state_flags(data_t *flags, uint32_t state)
{
	xassert(data_get_type(flags) == DATA_TYPE_LIST);

	/* Only give flags if state is known */
	if (!valid_base_state(state))
		return;

	const char *flag_str = NULL;
	while ((flag_str = node_state_flag_string_single(&state))) {
		data_set_string(data_list_append(flags),
				flag_str);
	}
}

static int _dump_node(data_t *p, node_info_t *node)
{
	int rc;
	uint16_t alloc_cpus = 0;
	uint64_t alloc_memory = 0;
	char *node_alloc_tres = NULL;
	double node_tres_weighted = 0;
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
	data_set_string(data_key_set(d, "extra"), node->extra);
	data_set_int(data_key_set(d, "free_memory"), node->free_mem);
	data_set_int(data_key_set(d, "cpus"), node->cpus);
	data_set_int(data_key_set(d, "last_busy"), node->last_busy);
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
	data_set_string_own(data_key_set(d, "next_state_after_reboot"),
			    _get_long_node_state(node->next_state));
	data_set_string(data_key_set(d, "address"), node->node_addr);
	data_set_string(data_key_set(d, "hostname"), node->node_hostname);

	data_set_string_own(data_key_set(d, "state"),
			    _get_long_node_state(node->node_state));
	_add_node_state_flags(data_set_list(data_key_set(d, "state_flags")),
			      node->node_state);

	data_set_string_own(data_key_set(d, "next_state_after_reboot"),
			    _get_long_node_state(node->next_state));
	_add_node_state_flags(
		data_set_list(data_key_set(d, "next_state_after_reboot_flags")),
		node->next_state);

	data_set_string(data_key_set(d, "operating_system"), node->os);
	if (node->owner == NO_VAL) {
		data_set_null(data_key_set(d, "owner"));
	} else {
		data_set_string_own(data_key_set(d, "owner"),
				    uid_to_string_or_null(node->owner));
	}

	if (node->partitions) {
		data_t *p = data_set_list(data_key_set(d, "partitions"));
		char *str = xstrdup(node->partitions);
		char *save_ptr = NULL;
		char *token = NULL;

		/* API provides as a CSV list */
		token = strtok_r(str, ",", &save_ptr);
		while (token) {
			data_set_string(data_list_append(p), token);
			token = strtok_r(NULL, ",", &save_ptr);
		}

		xfree(str);
	} else {
		data_set_list(data_key_set(d, "partitions"));
	}

	data_set_int(data_key_set(d, "port"), node->port);
	data_set_int(data_key_set(d, "real_memory"), node->real_memory);
	data_set_string(data_key_set(d, "reason"), node->reason);
	data_set_int(data_key_set(d, "reason_changed_at"), node->reason_time);
	data_set_string_own(data_key_set(d, "reason_set_by_user"),
			    uid_to_string_or_null(node->reason_uid));
	data_set_int(data_key_set(d, "slurmd_start_time"), node->slurmd_start_time);
	data_set_int(data_key_set(d, "sockets"), node->sockets);
	data_set_int(data_key_set(d, "threads"), node->threads);
	data_set_int(data_key_set(d, "temporary_disk"), node->tmp_disk);
	data_set_int(data_key_set(d, "weight"), node->weight);
	data_set_string(data_key_set(d, "tres"), node->tres_fmt_str);
	data_set_string(data_key_set(d, "slurmd_version"), node->version);

	/* Data from node->select_nodeinfo */
	if ((rc = slurm_get_select_nodeinfo(
		     node->select_nodeinfo, SELECT_NODEDATA_SUBCNT,
		     NODE_STATE_ALLOCATED, &alloc_cpus))) {
		error("%s: slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_SUBCNT): %s",
		      __func__, node->node_hostname, slurm_strerror(rc));
		return rc;
	}
	if ((rc = slurm_get_select_nodeinfo(
		     node->select_nodeinfo, SELECT_NODEDATA_MEM_ALLOC,
		     NODE_STATE_ALLOCATED, &alloc_memory))) {
		error("%s: slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_MEM_ALLOC): %s",
		      __func__, node->node_hostname, slurm_strerror(rc));
		return rc;
	}
	if ((rc = select_g_select_nodeinfo_get(
		     node->select_nodeinfo, SELECT_NODEDATA_TRES_ALLOC_FMT_STR,
		     NODE_STATE_ALLOCATED, &node_alloc_tres))) {
		error("%s: slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_TRES_ALLOC_FMT_STR): %s",
		      __func__, node->node_hostname, slurm_strerror(rc));
		return rc;
	}
	if ((rc = select_g_select_nodeinfo_get(
		     node->select_nodeinfo, SELECT_NODEDATA_TRES_ALLOC_WEIGHTED,
		     NODE_STATE_ALLOCATED, &node_tres_weighted))) {
		error("%s: slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_TRES_ALLOC_WEIGHTED): %s",
		      __func__, node->node_hostname, slurm_strerror(rc));
		return rc;
	}

	data_set_int(data_key_set(d, "alloc_memory"), alloc_memory);
	data_set_int(data_key_set(d, "alloc_cpus"), alloc_cpus);
	data_set_int(data_key_set(d, "idle_cpus"), (node->cpus - alloc_cpus));
	if (node_alloc_tres)
		data_set_string_own(data_key_set(d, "tres_used"),
				    node_alloc_tres);
	else
		data_set_null(data_key_set(d, "tres_used"));
	data_set_float(data_key_set(d, "tres_weighted"), node_tres_weighted);

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
	time_t update_time = 0;

	if (tag == URL_TAG_NODES) {
		if ((rc = get_date_param(query, "update_time", &update_time)))
			goto done;
		rc = slurm_load_node(update_time, &node_info_ptr,
				     SHOW_ALL|SHOW_DETAIL|SHOW_MIXED);
	} else if (tag == URL_TAG_NODE) {
		const data_t *node_name = data_key_get_const(parameters,
							     "node_name");
		char *name = NULL;

		if (!node_name || data_get_string_converted(node_name, &name))
			rc = ESLURM_INVALID_NODE_NAME;
		else
			rc = slurm_load_node_single(&node_info_ptr, name,
				SHOW_ALL|SHOW_DETAIL|SHOW_MIXED);

		xfree(name);
	} else
		rc = SLURM_ERROR;

	if (errno == SLURM_NO_CHANGE_IN_DATA) {
		/* no-op: nothing to do here */
		rc = errno;
		goto done;
	} else if (!rc && node_info_ptr && node_info_ptr->record_count) {
		partition_info_msg_t *part_info_ptr = NULL;

		if (!(rc = slurm_load_partitions(update_time, &part_info_ptr,
						 SHOW_ALL))) {
			slurm_populate_node_partitions(node_info_ptr,
						       part_info_ptr);

			slurm_free_partition_info_msg(part_info_ptr);
		}

		for (int i = 0; !rc && i < node_info_ptr->record_count; i++)
			rc = _dump_node(nodes,
					   &node_info_ptr->node_array[i]);
	}

	if (!rc && (!node_info_ptr || node_info_ptr->record_count == 0))
		rc = ESLURM_INVALID_NODE_NAME;

	if (rc) {
		data_t *e = data_set_dict(data_list_append(errors));
		data_set_string(data_key_set(e, "error"),
				slurm_strerror(rc));
		data_set_int(data_key_set(e, "errno"), rc);
	}

done:
	slurm_free_node_info_msg(node_info_ptr);
	return rc;
}

extern void init_op_nodes(void)
{
	bind_operation_handler("/slurm/v0.0.38/nodes/", _op_handler_nodes,
			       URL_TAG_NODES);
	bind_operation_handler("/slurm/v0.0.38/node/{node_name}",
			       _op_handler_nodes, URL_TAG_NODE);
}

extern void destroy_op_nodes(void)
{
	unbind_operation_handler(_op_handler_nodes);
}
