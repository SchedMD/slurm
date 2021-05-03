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

#include "config.h"

#define _GNU_SOURCE

#include <search.h>
#include <stdint.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/ref.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"

#include "src/plugins/openapi/v0.0.37/api.h"

typedef enum {
	URL_TAG_UNKNOWN = 0,
	URL_TAG_PARTITION,
	URL_TAG_PARTITIONS,
} url_tag_t;

static int _dump_part(data_t *p, partition_info_t *part)
{
	data_t *d = data_set_dict(data_list_append(p));
	data_t *flags = data_set_list(data_key_set(d, "flags"));
	data_t *pm = data_set_list(data_key_set(d, "preemption_mode"));

	data_set_string(data_key_set(d, "allowed_allocation_nodes"),
			part->allow_alloc_nodes);
	data_set_string(data_key_set(d, "allowed_accounts"),
			part->allow_accounts);
	data_set_string(data_key_set(d, "allowed_groups"), part->allow_groups);
	data_set_string(data_key_set(d, "allowed_qos"), part->allow_qos);
	data_set_string(data_key_set(d, "alternative"), part->alternate);
	data_set_string(data_key_set(d, "billing_weights"),
			part->billing_weights_str);

	data_set_int(data_key_set(d, "default_memory_per_cpu"),
		     part->def_mem_per_cpu);
	if (part->default_time == INFINITE)
		data_set_int(data_key_set(d, "default_time_limit"), -1);
	if (part->default_time == NO_VAL)
		data_set_null(data_key_set(d, "default_time_limit"));
	else
		data_set_int(data_key_set(d, "default_time_limit"),
			     part->def_mem_per_cpu);

	data_set_string(data_key_set(d, "denied_accounts"),
			part->deny_accounts);
	data_set_string(data_key_set(d, "denied_qos"), part->deny_qos);

	if (part->flags & PART_FLAG_DEFAULT)
		data_set_string(data_list_append(flags), "default");
	if (part->flags & PART_FLAG_HIDDEN)
		data_set_string(data_list_append(flags), "hidden");
	if (part->flags & PART_FLAG_NO_ROOT)
		data_set_string(data_list_append(flags), "no_root");
	if (part->flags & PART_FLAG_ROOT_ONLY)
		data_set_string(data_list_append(flags), "root_only");
	if (part->flags & PART_FLAG_REQ_RESV)
		data_set_string(data_list_append(flags),
				"reservation_required");
	if (part->flags & PART_FLAG_LLN)
		data_set_string(data_list_append(flags), "least_loaded_nodes");
	if (part->flags & PART_FLAG_EXCLUSIVE_USER)
		data_set_string(data_list_append(flags), "exclusive_user");

	data_set_int(data_key_set(d, "preemption_grace_time"),
		     part->grace_time);

	if (part->max_cpus_per_node == INFINITE)
		data_set_int(data_key_set(d, "maximum_cpus_per_node"), -1);
	else if (part->max_cpus_per_node == NO_VAL)
		data_set_null(data_key_set(d, "maximum_cpus_per_node"));
	else
		data_set_int(data_key_set(d, "maximum_cpus_per_node"),
			     part->max_cpus_per_node);

	data_set_int(data_key_set(d, "maximum_memory_per_node"),
		     part->max_mem_per_cpu);

	if (part->max_nodes == INFINITE)
		data_set_int(data_key_set(d, "maximum_nodes_per_job"), -1);
	else
		data_set_int(data_key_set(d, "maximum_nodes_per_job"),
			     part->max_nodes);

	if (part->max_time == INFINITE)
		data_set_int(data_key_set(d, "max_time_limit"), -1);
	else
		data_set_int(data_key_set(d, "max_time_limit"), part->max_time);
	data_set_int(data_key_set(d, "min nodes per job"), part->min_nodes);
	data_set_string(data_key_set(d, "name"), part->name);
	// TODO: int32_t *node_inx;	/* list index pairs into node_table:
	// 			 * start_range_1, end_range_1,
	// 			 * start_range_2, .., -1  */
	data_set_string(data_key_set(d, "nodes"), part->nodes);
	if (part->over_time_limit == NO_VAL16)
		data_set_null(data_key_set(d, "over_time_limit"));
	else
		data_set_int(data_key_set(d, "over_time_limit"),
			     part->over_time_limit);

	if (part->preempt_mode == PREEMPT_MODE_OFF)
		data_set_string(data_list_append(pm), "disabled");
	if (part->preempt_mode & PREEMPT_MODE_SUSPEND)
		data_set_string(data_list_append(pm), "suspend");
	if (part->preempt_mode & PREEMPT_MODE_REQUEUE)
		data_set_string(data_list_append(pm), "requeue");
	if (part->preempt_mode & PREEMPT_MODE_GANG)
		data_set_string(data_list_append(pm), "gang_schedule");

	data_set_int(data_key_set(d, "priority_job_factor"),
		     part->priority_job_factor);
	data_set_int(data_key_set(d, "priority_tier"), part->priority_tier);
	data_set_string(data_key_set(d, "qos"), part->qos_char);
	if (part->state_up == PARTITION_UP)
		data_set_string(data_key_set(d, "state"), "UP");
	else if (part->state_up == PARTITION_DOWN)
		data_set_string(data_key_set(d, "state"), "DOWN");
	else if (part->state_up == PARTITION_INACTIVE)
		data_set_string(data_key_set(d, "state"), "INACTIVE");
	else if (part->state_up == PARTITION_DRAIN)
		data_set_string(data_key_set(d, "state"), "DRAIN");
	else
		data_set_string(data_key_set(d, "state"), "UNKNOWN");

	data_set_int(data_key_set(d, "total_cpus"), part->total_cpus);
	data_set_int(data_key_set(d, "total_nodes"), part->total_nodes);
	data_set_string(data_key_set(d, "tres"), part->tres_fmt_str);

	return SLURM_SUCCESS;
}

static int _op_handler_partitions(const char *context_id,
				  http_request_method_t method,
				  data_t *parameters, data_t *query,
				  int tag, data_t *d, void *auth)
{
	int rc = SLURM_SUCCESS;
	data_t *errors = populate_response_format(d);
	data_t *partitions = data_set_list(data_key_set(d, "partitions"));
	char *name = NULL;
	partition_info_msg_t *part_info_ptr = NULL;
	time_t update_time = 0;

	if ((rc = get_date_param(query, "update_time", &update_time)))
		goto done;

	if (tag == URL_TAG_PARTITION) {
		const data_t *part_name = data_key_get_const(parameters,
							     "partition_name");
		if (!part_name || data_get_string_converted(part_name, &name) ||
		    !name)
			rc = ESLURM_INVALID_PARTITION_NAME;
	}

	if (!rc)
		rc = slurm_load_partitions(update_time, &part_info_ptr,
					   SHOW_ALL);
	if (errno == SLURM_NO_CHANGE_IN_DATA) {
		/* no-op: nothing to do here */
		rc = errno;
		goto done;
	} else if (!rc && part_info_ptr) {
		int found = 0;
		for (int i = 0; !rc && i < part_info_ptr->record_count; i++) {
			if (tag == URL_TAG_PARTITIONS ||
			    !xstrcasecmp(
				    name,
				    part_info_ptr->partition_array[i].name)) {
				rc = _dump_part(
					partitions,
					&part_info_ptr->partition_array[i]);
				found++;
			}
		}

		if (!found)
			rc = ESLURM_INVALID_PARTITION_NAME;
	}

	if (!rc && (!part_info_ptr || part_info_ptr->record_count == 0))
		rc = ESLURM_INVALID_PARTITION_NAME;

	if (rc) {
		data_t *e = data_set_dict(data_list_append(errors));
		data_set_string(data_key_set(e, "error"), slurm_strerror(rc));
		data_set_int(data_key_set(e, "errno"), rc);
	}

done:
	slurm_free_partition_info_msg(part_info_ptr);
	xfree(name);
	return rc;
}

extern void init_op_partitions(void)
{
	bind_operation_handler("/slurm/v0.0.37/partitions/",
			       _op_handler_partitions, URL_TAG_PARTITIONS);
	bind_operation_handler("/slurm/v0.0.37/partition/{partition_name}",
			       _op_handler_partitions, URL_TAG_PARTITION);
}

extern void destroy_op_partitions(void)
{
	unbind_operation_handler(_op_handler_partitions);
}
