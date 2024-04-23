/*****************************************************************************\
 *  part_record.h - PARTITION parameters and data structures
 *****************************************************************************
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

#include "part_record.h"

#include "src/common/read_config.h"
#include "src/common/xmalloc.h"

/*
 * Free memory for cached backfill data in partition record
 */
static void _bf_data_free(bf_part_data_t **datap)
{
	bf_part_data_t *data;
	if (!datap || !*datap)
		return;

	data = *datap;

	slurmdb_destroy_bf_usage(data->job_usage);
        slurmdb_destroy_bf_usage(data->resv_usage);
	xhash_free(data->user_usage);
	xfree(data);

	*datap = NULL;
}

/*
 * Sync with _init_conf_part().
 *
 * _init_conf_part() initializes default values from slurm.conf parameters.
 * After parsing slurm.conf, _build_single_partitionline_info() copies
 * slurm_conf_partition_t to part_record_t. Default values between
 * slurm_conf_partition_t and part_record_t should stay in sync in case a
 * part_record_t is created outside of slurm.conf parsing.
 */
static void _init_part_record(part_record_t *part_ptr)
{
	part_ptr->magic = PART_MAGIC;
	if (slurm_conf.conf_flags & CONF_FLAG_DRJ)
		part_ptr->flags |= PART_FLAG_NO_ROOT;
	part_ptr->max_nodes_orig = INFINITE;
	part_ptr->min_nodes = 1;
	part_ptr->min_nodes_orig = 1;

	/* sync with slurm_conf_partition_t */
	part_ptr->default_time = NO_VAL;
	part_ptr->max_cpus_per_node = INFINITE;
	part_ptr->max_cpus_per_socket = INFINITE;
	part_ptr->max_nodes = INFINITE;
	part_ptr->max_share = 1;
	part_ptr->max_time = INFINITE;
	part_ptr->over_time_limit = NO_VAL16;
	part_ptr->preempt_mode = NO_VAL16;
	part_ptr->priority_job_factor = 1;
	part_ptr->priority_tier = 1;
	part_ptr->resume_timeout = NO_VAL16;
	part_ptr->state_up = PARTITION_UP;
	part_ptr->suspend_time = NO_VAL;
	part_ptr->suspend_timeout = NO_VAL16;
}

extern part_record_t *part_record_create(void)
{
	part_record_t *part_ptr = xmalloc(sizeof(*part_ptr));

	_init_part_record(part_ptr);

	return part_ptr;
}

extern void part_record_delete(part_record_t *part_ptr)
{
	if (!part_ptr)
		return;

	xfree(part_ptr->allow_accounts);
	FREE_NULL_LIST(part_ptr->allow_accts_list);
	xfree(part_ptr->allow_alloc_nodes);
	xfree(part_ptr->allow_groups);
	xfree(part_ptr->allow_uids);
	xfree(part_ptr->allow_qos);
	FREE_NULL_BITMAP(part_ptr->allow_qos_bitstr);
	xfree(part_ptr->alternate);
	xfree(part_ptr->billing_weights_str);
	xfree(part_ptr->billing_weights);
	xfree(part_ptr->deny_accounts);
	FREE_NULL_LIST(part_ptr->deny_accts_list);
	xfree(part_ptr->deny_qos);
	FREE_NULL_BITMAP(part_ptr->deny_qos_bitstr);
	FREE_NULL_LIST(part_ptr->job_defaults_list);
	xfree(part_ptr->name);
	xfree(part_ptr->orig_nodes);
	xfree(part_ptr->nodes);
	xfree(part_ptr->nodesets);
	FREE_NULL_BITMAP(part_ptr->node_bitmap);
	xfree(part_ptr->qos_char);
	xfree(part_ptr->tres_cnt);
	xfree(part_ptr->tres_fmt_str);
	_bf_data_free(&part_ptr->bf_data);

	xfree(part_ptr);
}

extern void part_record_pack(part_record_t *part_ptr,
			     buf_t *buffer,
			     uint16_t protocol_version)
{
	if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		pack32(part_ptr->cpu_bind, buffer);
		packstr(part_ptr->name, buffer);
		pack32(part_ptr->grace_time, buffer);
		pack32(part_ptr->max_time, buffer);
		pack32(part_ptr->default_time, buffer);
		pack32(part_ptr->max_cpus_per_node, buffer);
		pack32(part_ptr->max_cpus_per_socket, buffer);
		pack32(part_ptr->max_nodes_orig, buffer);
		pack32(part_ptr->min_nodes_orig, buffer);

		pack32(part_ptr->flags, buffer);
		pack16(part_ptr->max_share, buffer);
		pack16(part_ptr->over_time_limit,buffer);
		pack16(part_ptr->preempt_mode, buffer);
		pack16(part_ptr->priority_job_factor, buffer);
		pack16(part_ptr->priority_tier, buffer);

		pack16(part_ptr->state_up, buffer);
		pack16(part_ptr->cr_type, buffer);

		packstr(part_ptr->allow_accounts, buffer);
		packstr(part_ptr->allow_groups, buffer);
		packstr(part_ptr->allow_qos, buffer);
		packstr(part_ptr->qos_char, buffer);
		packstr(part_ptr->allow_alloc_nodes, buffer);
		packstr(part_ptr->alternate, buffer);
		packstr(part_ptr->deny_accounts, buffer);
		packstr(part_ptr->deny_qos, buffer);
		/* Save orig_nodes as nodes will be built from orig_nodes */
		packstr(part_ptr->orig_nodes, buffer);
	}
}

extern int part_record_unpack(part_record_t **part,
			      buf_t *buffer,
			      uint16_t protocol_version)
{
	part_record_t *part_ptr = part_record_create();

	*part = part_ptr;

	if (protocol_version >= SLURM_24_05_PROTOCOL_VERSION) {
		safe_unpack32(&part_ptr->cpu_bind, buffer);
		safe_unpackstr(&part_ptr->name, buffer);
		safe_unpack32(&part_ptr->grace_time, buffer);
		safe_unpack32(&part_ptr->max_time, buffer);
		safe_unpack32(&part_ptr->default_time, buffer);
		safe_unpack32(&part_ptr->max_cpus_per_node, buffer);
		safe_unpack32(&part_ptr->max_cpus_per_socket, buffer);
		safe_unpack32(&part_ptr->max_nodes, buffer);
		safe_unpack32(&part_ptr->min_nodes, buffer);

		safe_unpack32(&part_ptr->flags, buffer);
		safe_unpack16(&part_ptr->max_share, buffer);
		safe_unpack16(&part_ptr->over_time_limit, buffer);
		safe_unpack16(&part_ptr->preempt_mode, buffer);

		safe_unpack16(&part_ptr->priority_job_factor, buffer);
		safe_unpack16(&part_ptr->priority_tier, buffer);

		safe_unpack16(&part_ptr->state_up, buffer);
		safe_unpack16(&part_ptr->cr_type, buffer);

		safe_unpackstr(&part_ptr->allow_accounts, buffer);
		safe_unpackstr(&part_ptr->allow_groups, buffer);
		safe_unpackstr(&part_ptr->allow_qos, buffer);
		safe_unpackstr(&part_ptr->qos_char, buffer);
		safe_unpackstr(&part_ptr->allow_alloc_nodes, buffer);
		safe_unpackstr(&part_ptr->alternate, buffer);
		safe_unpackstr(&part_ptr->deny_accounts, buffer);
		safe_unpackstr(&part_ptr->deny_qos, buffer);
		safe_unpackstr(&part_ptr->nodes, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		uint16_t tmp_uint16;
		safe_unpack32(&part_ptr->cpu_bind, buffer);
		safe_unpackstr(&part_ptr->name, buffer);
		safe_unpack32(&part_ptr->grace_time, buffer);
		safe_unpack32(&part_ptr->max_time, buffer);
		safe_unpack32(&part_ptr->default_time, buffer);
		safe_unpack32(&part_ptr->max_cpus_per_node, buffer);
		safe_unpack32(&part_ptr->max_cpus_per_socket, buffer);
		safe_unpack32(&part_ptr->max_nodes, buffer);
		safe_unpack32(&part_ptr->min_nodes, buffer);

		safe_unpack16(&tmp_uint16, buffer);
		part_ptr->flags = tmp_uint16;
		safe_unpack16(&part_ptr->max_share, buffer);
		safe_unpack16(&part_ptr->over_time_limit, buffer);
		safe_unpack16(&part_ptr->preempt_mode, buffer);

		safe_unpack16(&part_ptr->priority_job_factor, buffer);
		safe_unpack16(&part_ptr->priority_tier, buffer);

		safe_unpack16(&part_ptr->state_up, buffer);
		safe_unpack16(&part_ptr->cr_type, buffer);

		safe_unpackstr(&part_ptr->allow_accounts, buffer);
		safe_unpackstr(&part_ptr->allow_groups, buffer);
		safe_unpackstr(&part_ptr->allow_qos, buffer);
		safe_unpackstr(&part_ptr->qos_char, buffer);
		safe_unpackstr(&part_ptr->allow_alloc_nodes, buffer);
		safe_unpackstr(&part_ptr->alternate, buffer);
		safe_unpackstr(&part_ptr->deny_accounts, buffer);
		safe_unpackstr(&part_ptr->deny_qos, buffer);
		safe_unpackstr(&part_ptr->nodes, buffer);
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:

	part_record_delete(part_ptr);
	*part = NULL;
	return SLURM_ERROR;
}
