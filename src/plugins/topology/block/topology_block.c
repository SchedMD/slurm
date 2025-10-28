/*****************************************************************************\
 *  topology_block.c
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

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_protocol_pack.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#include "../common/common_topo.h"

#include "eval_nodes_block.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
#else
node_record_t **node_record_table_ptr;
int node_record_count;
#endif

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "topology block plugin";
const char plugin_type[]        = "topology/block";
const uint32_t plugin_id = TOPOLOGY_PLUGIN_BLOCK;
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;
const bool supports_exclusive_topo = true;

typedef struct topoinfo_bblock {
	bool aggregated;
	uint16_t block_index;
	char *name;
	char *nodes;
	uint32_t size;
} topoinfo_bblock_t;

typedef struct topoinfo_block {
	uint32_t record_count; /* number of records */
	topoinfo_bblock_t *topo_array;/* the block topology records */
} topoinfo_block_t;

static void _print_topo_record(topoinfo_bblock_t * topo_ptr, char **out)
{
	char *env, *line = NULL, *pos = NULL;

	/****** Line 1 ******/
	xstrfmtcatat(line, &pos, "%s=%s BlockIndex=%u",
		     topo_ptr->aggregated ? "AggregatedBlock" : "BlockName",
		     topo_ptr->name, topo_ptr->block_index);

	if (topo_ptr->nodes)
		xstrfmtcatat(line, &pos, " Nodes=%s", topo_ptr->nodes);

	xstrfmtcatat(line, &pos, " BlockSize=%u", topo_ptr->size);

	if ((env = getenv("SLURM_TOPO_LEN")))
		xstrfmtcat(*out, "%.*s\n", atoi(env), line);
	else
		xstrfmtcat(*out, "%s\n", line);

	xfree(line);

}

extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	return;
}

extern int topology_p_add_rm_node(node_record_t *node_ptr, char *unit,
				  topology_ctx_t *tctx)
{
	block_context_t *ctx = tctx->plugin_ctx;
	int *change;

	bit_clear(ctx->blocks_nodes_bitmap, node_ptr->index);

	if (unit) {
		/*
		 * Check for a valid block first before being destructive on
		 * bitmaps.
		 */
		bool found_one_block = false;
		for (int i = 0; i < ctx->block_count; i++) {
			if (!xstrcmp(ctx->block_record_table[i].name, unit)) {
				found_one_block = true;
				break;
			}
		}
		if (!found_one_block) {
			/* Tried to add to a block that doesn't exist */
			return SLURM_ERROR;
		}
	}

	change = xcalloc(ctx->block_count + ctx->ablock_count, sizeof(int));

	for (int i = 0; i < ctx->block_count; i++) {
		bool in_block = bit_test(ctx->block_record_table[i].node_bitmap,
					 node_ptr->index);
		bool add = (!xstrcmp(ctx->block_record_table[i].name, unit));

		if (add && !in_block) {
			debug2("%s: add %s to %s",
			       __func__, node_ptr->name,
			       ctx->block_record_table[i].name);
			bit_set(ctx->block_record_table[i].node_bitmap,
				node_ptr->index);
			bit_set(ctx->blocks_nodes_bitmap, node_ptr->index);
			change[i] = 1;
		} else if (!add && in_block) {
			debug2("%s: remove %s from %s",
			       __func__, node_ptr->name,
			       ctx->block_record_table[i].name);
			bit_clear(ctx->block_record_table[i].node_bitmap,
				  node_ptr->index);
			change[i] = -1;
		}
	}

	for (int i = 0; i < ctx->block_count; i++) {
		if (!change[i])
			continue;

		xfree(ctx->block_record_table[i].nodes);
		ctx->block_record_table[i].nodes =
			bitmap2node_name(ctx->block_record_table[i]
						 .node_bitmap);
		block_record_update_block_config(tctx, i);

		for (int j = ctx->block_count;
		     j < ctx->block_count + ctx->ablock_count; j++) {
			char *tmp_list = ctx->block_record_table[j].name;
			hostlist_t *hl = NULL;

			if (change[j])
				continue;

			hl = hostlist_create(tmp_list);

			if (hl == NULL)
				fatal("Invalid BlockName: %s", tmp_list);

			if (hostlist_find(hl,
					  ctx->block_record_table[i].name) >=
			    0) {
				if (change[i] > 0) {
					bit_set(ctx->block_record_table[j]
							.node_bitmap,
						node_ptr->index);
					change[j] = 1;
				} else {
					bit_clear(ctx->block_record_table[j]
							  .node_bitmap,
						  node_ptr->index);
				}

				xfree(ctx->block_record_table[j].nodes);
				ctx->block_record_table[j]
					.nodes = bitmap2node_name(
					ctx->block_record_table[j].node_bitmap);
			}
			hostlist_destroy(hl);
		}
	}
	xfree(change);

	return SLURM_SUCCESS;
}

/*
 * topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topology_p_build_config(topology_ctx_t *tctx)
{
	if (node_record_count)
		return block_record_validate(tctx);
	return SLURM_SUCCESS;
}

extern int topology_p_destroy_config(topology_ctx_t *tctx)
{
	block_context_t *ctx = tctx->plugin_ctx;

	block_record_table_destroy(ctx);
	FREE_NULL_BITMAP(ctx->blocks_nodes_bitmap);
	xfree(tctx->plugin_ctx);

	return SLURM_SUCCESS;
}

extern int topology_p_eval_nodes(topology_eval_t *topo_eval)
{
	block_context_t *ctx = topo_eval->tctx->plugin_ctx;
	/*
	 * Don't use eval_nodes_block() when there isn't any block node on
	 * node_map. This allows the allocation of nodes not connected by block
	 * topology (separated by partition or constraints).
	 */
	if (ctx->blocks_nodes_bitmap &&
	    bit_overlap_any(ctx->blocks_nodes_bitmap, topo_eval->node_map)) {
		topo_eval->eval_nodes = eval_nodes_block;
		topo_eval->trump_others = true;
	}

	xassert(!topo_eval->job_ptr->topo_jobinfo);

	topo_eval->job_ptr->topo_jobinfo =
		xmalloc(sizeof(*topo_eval->job_ptr->topo_jobinfo));
	topo_eval->job_ptr->topo_jobinfo->plugin_id = plugin_id;

	return common_topo_choose_nodes(topo_eval);
}

extern int topology_p_whole_topo(bitstr_t *node_mask, void *tctx)
{
	block_context_t *ctx = tctx;
	for (int i = 0; i < ctx->block_count; i++) {
		if (bit_overlap_any(ctx->block_record_table[i].node_bitmap,
				    node_mask)) {
			bit_or(node_mask,
			       ctx->block_record_table[i].node_bitmap);
		}
	}
	return SLURM_SUCCESS;
}

/*
 * Get bitmap of nodes in block
 *
 * IN name of block
 * RET bitmap of nodes from block_record_table (do not free)
 */
extern bitstr_t *topology_p_get_bitmap(char *name, void *tctx)
{
	block_context_t *ctx = tctx;
	for (int i = 0; i < ctx->block_count + ctx->ablock_count; i++) {
		if (!xstrcmp(ctx->block_record_table[i].name, name)) {
			return ctx->block_record_table[i].node_bitmap;
		}
	}

	return NULL;
}

extern bool topology_p_generate_node_ranking(topology_ctx_t *tctx)
{
	/* By default, node_rank is 0, so start at 1 */
	int block_rank = 1;
	block_context_t *ctx;
	node_record_t *node_ptr;

	if (!xstrcasestr(slurm_conf.topology_param, "BlockAsNodeRank"))
		return false;

	block_record_validate(tctx);

	ctx = tctx->plugin_ctx;

	if (ctx->block_count == 0) {
		topology_p_destroy_config(tctx);
		return false;
	}

	for (int i = 0; i < ctx->block_count; i++) {
		for (int n = 0;
		     (node_ptr = next_node_bitmap(ctx->block_record_table[i]
							  .node_bitmap,
						  &n));
		     n++) {
			node_ptr->node_rank = block_rank;
			debug("node=%s rank=%d", node_ptr->name, block_rank);
		}
		block_rank++;
	}

	/* Discard the temporary topology */
	topology_p_destroy_config(tctx);

	return true;
}

/*
 * topo_get_node_addr - build node address and the associated pattern
 *      based on the topology information
 *
 * example of output :
 *      address : b8.tux1
 *      pattern : block.node
 */
extern int topology_p_get_node_addr(char *node_name, char **paddr,
				    char **ppattern, void *tctx)
{
	node_record_t *node_ptr = find_node_record(node_name);
	block_context_t *ctx = tctx;

	/* node not found in configuration */
	if (!node_ptr)
		return SLURM_ERROR;

	for (int i = 0; i < ctx->block_count; i++) {
		if (bit_test(ctx->block_record_table[i].node_bitmap,
			     node_ptr->index)) {
			*paddr = xstrdup_printf("%s.%s",
						ctx->block_record_table[i].name,
						node_name);
			*ppattern = xstrdup("block.node");
			return SLURM_SUCCESS;
		}
	}

	return common_topo_get_node_addr(node_name, paddr, ppattern);
}

extern int topology_p_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				     int *count, uint16_t tree_width,
				     void *tctx)
{
	return common_topo_split_hostlist_treewidth(
		hl, sp_hl, count, tree_width);
}

extern int topology_p_get(topology_data_t type, void *data, void *tctx)
{
	int rc = SLURM_SUCCESS;
	block_context_t *ctx = tctx;

	switch (type) {
	case TOPO_DATA_TOPOLOGY_PTR:
	{
		dynamic_plugin_data_t **topoinfo_pptr = data;
		topoinfo_block_t *topoinfo_ptr =
			xmalloc(sizeof(topoinfo_block_t));

		*topoinfo_pptr = xmalloc(sizeof(dynamic_plugin_data_t));
		(*topoinfo_pptr)->data = topoinfo_ptr;
		(*topoinfo_pptr)->plugin_id = plugin_id;

		topoinfo_ptr->record_count =
			ctx->block_count + ctx->ablock_count;
		topoinfo_ptr->topo_array = xcalloc(topoinfo_ptr->record_count,
						   sizeof(topoinfo_bblock_t));

		for (int i = 0; i < topoinfo_ptr->record_count; i++) {
			topoinfo_ptr->topo_array[i].block_index =
				ctx->block_record_table[i].block_index;
			topoinfo_ptr->topo_array[i].name =
				xstrdup(ctx->block_record_table[i].name);
			topoinfo_ptr->topo_array[i].nodes =
				xstrdup(ctx->block_record_table[i].nodes);
			if (ctx->block_record_table[i].level)
				topoinfo_ptr->topo_array[i].aggregated = true;
			topoinfo_ptr->topo_array[i].size =
				ctx->bblock_node_cnt *
				ctx->block_sizes[ctx->block_record_table[i]
							 .level];
		}

		break;
	}
	case TOPO_DATA_REC_CNT:
	{
		int *rec_cnt = data;
		*rec_cnt = ctx->block_count;
		break;
	}
	case TOPO_DATA_EXCLUSIVE_TOPO:
	{
		int *exclusive_topo = data;
		*exclusive_topo = 1;
		break;
	}
	default:
		error("Unsupported option %d", type);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int topology_p_topoinfo_free(void *topoinfo_ptr)
{
	int i = 0;
	topoinfo_block_t *topoinfo = topoinfo_ptr;
	if (topoinfo) {
		if (topoinfo->topo_array) {
			for (i = 0; i < topoinfo->record_count; i++) {
				xfree(topoinfo->topo_array[i].name);
				xfree(topoinfo->topo_array[i].nodes);
			}
			xfree(topoinfo->topo_array);
		}
		xfree(topoinfo);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_pack(void *topoinfo_ptr, buf_t *buffer,
				    uint16_t protocol_version)
{
	int i;
	topoinfo_block_t *topoinfo = topoinfo_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack32(topoinfo->record_count, buffer);
		for (i = 0; i < topoinfo->record_count; i++) {
			packbool(topoinfo->topo_array[i].aggregated, buffer);
			pack16(topoinfo->topo_array[i].block_index, buffer);
			packstr(topoinfo->topo_array[i].name, buffer);
			packstr(topoinfo->topo_array[i].nodes, buffer);
			pack32(topoinfo->topo_array[i].size, buffer);
		}
	} else {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_print(void *topoinfo_ptr, char *nodes_list,
				     char *unit, char **out)
{
	int i, match, match_cnt = 0;;
	topoinfo_block_t *topoinfo = topoinfo_ptr;

	*out = NULL;

	if ((!nodes_list || (nodes_list[0] == '\0')) &&
	    (!unit || (unit[0] == '\0'))) {
		if (topoinfo->record_count == 0) {
			error("No topology information available");
			return SLURM_SUCCESS;
		}

		for (i = 0; i < topoinfo->record_count; i++)
			_print_topo_record(&topoinfo->topo_array[i], out);

		return SLURM_SUCCESS;
	}

	/* Search for matching node name and  block name */
	for (i = 0; i < topoinfo->record_count; i++) {
		hostset_t *hs;

		if (unit && xstrcmp(topoinfo->topo_array[i].name, unit))
			continue;

		if (nodes_list) {
			if ((topoinfo->topo_array[i].nodes == NULL) ||
			    (topoinfo->topo_array[i].nodes[0] == '\0'))
				continue;
			hs = hostset_create(topoinfo->topo_array[i].nodes);
			if (hs == NULL)
				fatal("hostset_create: memory allocation failure");
			match = hostset_within(hs, nodes_list);
			hostset_destroy(hs);
			if (!match)
				continue;
		}
		match_cnt++;
		_print_topo_record(&topoinfo->topo_array[i], out);
	}

	if (match_cnt == 0) {
		error("Topology information contains no block%s%s%s%s",
		      unit ? " named " : "",
		      unit ? unit : "",
		      nodes_list ? " with nodes " : "",
		      nodes_list ? nodes_list : "");
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_unpack(void **topoinfo_pptr, buf_t *buffer,
				      uint16_t protocol_version)
{
	int i = 0;
	topoinfo_block_t *topoinfo_ptr =
		xmalloc(sizeof(topoinfo_block_t));

	*topoinfo_pptr = topoinfo_ptr;
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack32(&topoinfo_ptr->record_count, buffer);
		safe_xcalloc(topoinfo_ptr->topo_array,
			     topoinfo_ptr->record_count,
			     sizeof(topoinfo_bblock_t));
		for (i = 0; i < topoinfo_ptr->record_count; i++) {
			safe_unpackbool(&topoinfo_ptr->topo_array[i].aggregated,
					buffer);
			safe_unpack16(&topoinfo_ptr->topo_array[i].block_index,
				      buffer);
			safe_unpackstr(&topoinfo_ptr->topo_array[i].name,
				       buffer);
			safe_unpackstr(&topoinfo_ptr->topo_array[i].nodes,
				       buffer);
			safe_unpack32(&topoinfo_ptr->topo_array[i].size,
				      buffer);
		}
	} else {
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	topology_p_topoinfo_free(topoinfo_ptr);
	*topoinfo_pptr = NULL;
	return SLURM_ERROR;
}

extern void topology_p_jobinfo_free(
	topology_jobinfo_t *topo_jobinfo)
{
	if (!topo_jobinfo)
		return;

	FREE_NULL_LIST(topo_jobinfo->segment_list);
	xfree(topo_jobinfo);

	return;
}

extern void topology_p_jobinfo_pack(
	topology_jobinfo_t *topo_jobinfo,
	buf_t *buffer,
	uint16_t protocol_version)
{
	xassert(topo_jobinfo);

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		list_t *segment_list = NULL;

		if (topo_jobinfo)
			segment_list = topo_jobinfo->segment_list;

		slurm_pack_list(segment_list, packstr_with_version, buffer,
				protocol_version);
	}
}

extern int topology_p_jobinfo_unpack(
	topology_jobinfo_t **topo_jobinfo,
	buf_t *buffer,
	uint16_t protocol_version)
{
	xassert(topo_jobinfo);

	if (protocol_version >= SLURM_25_11_PROTOCOL_VERSION) {
		*topo_jobinfo = xmalloc(sizeof(**topo_jobinfo));
		if (slurm_unpack_list(&((*topo_jobinfo)->segment_list),
				      unpackstr_with_version, xfree_ptr, buffer,
				      protocol_version))
			goto unpack_error;
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
		goto unpack_error;
	}

	return SLURM_SUCCESS;
unpack_error:
	error("%s: unpack error", __func__);
	xfree(*topo_jobinfo);

	return SLURM_ERROR;
}

extern int topology_p_jobinfo_get(
	topology_jobinfo_type_t type,
	topology_jobinfo_t *topo_jobinfo,
	void *data)
{
	if (!topo_jobinfo)
		return SLURM_ERROR;

	switch (type) {
	case TOPO_JOBINFO_SEGMENT_LIST:
		*(list_t **) data = topo_jobinfo->segment_list;
		return SLURM_SUCCESS;
	default:
		error("Unknown topology_jobinfo_type_t: %u", type);
		break;
	}

	return SLURM_ERROR;
}

extern uint32_t topology_p_get_fragmentation(bitstr_t *node_mask, void *tctx)
{
	uint32_t frag = 0;
	bool bset[MAX_BLOCK_LEVELS] = {0};
	block_context_t *ctx = tctx;

	/*
	 * Calculate fragmentation as the sum of sizes of all unavailable
	 * base and aggregate blocks.
	 */
	for (int i = 0; i < ctx->block_count; i++) {
		if (bit_overlap(ctx->block_record_table[i].node_bitmap,
				node_mask) >= ctx->bblock_node_cnt) {
			for (int j = 1; j < ctx->block_sizes_cnt; j++) {
				if (!(i % ctx->block_sizes[j]) &&
				    (ctx->block_sizes[j] <=
				     (ctx->block_count - i)))
					bset[j] = true;
			}
		} else {
			for (int j = 0; j < ctx->block_sizes_cnt; j++) {
				if (bset[j] || (!(i % ctx->block_sizes[j]) &&
						(ctx->block_sizes[j] <=
						 (ctx->block_count - i)))) {
					frag += ctx->block_sizes[j];
					bset[j] = false;
				}
			}
		}
	}

	frag *= ctx->bblock_node_cnt;
	frag += ctx->blocks_nodes_cnt;
	frag -= bit_overlap(node_mask, ctx->blocks_nodes_bitmap);

	return frag;
}

extern void topology_p_get_topology_str(node_record_t *node_ptr,
					char **topology_str_ptr,
					topology_ctx_t *tctx)
{
	block_context_t *ctx = tctx->plugin_ctx;

	for (int i = 0; i < ctx->block_count; i++) {
		if (bit_test(ctx->block_record_table[i].node_bitmap,
			     node_ptr->index)) {
			xstrfmtcat(*topology_str_ptr, "%s%s:%s",
				   *topology_str_ptr ? "," : "", tctx->name,
				   ctx->block_record_table[i].name);
			break;
		}
	}
	return;
}
