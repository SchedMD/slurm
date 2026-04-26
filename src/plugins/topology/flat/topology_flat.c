/*****************************************************************************\
 *  topology_flat.c - Default for system topology
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <signal.h>
#include <sys/types.h>

#include "src/common/slurm_xlator.h"

#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/strnatcmp.h"
#include "src/common/xstring.h"

#include "../common/common_topo.h"

typedef struct {
	uint32_t pos;
	char *name;
} alpha_sort_t;

/* Required Slurm plugin symbols: */
const char plugin_name[] = "topology Flat plugin";
const char plugin_type[] = "topology/flat";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Required for topology plugins: */
const uint32_t plugin_id = TOPOLOGY_PLUGIN_FLAT;
const bool supports_exclusive_topo = false;

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
	return SLURM_SUCCESS;
}

extern bool topology_p_allow_one_node(void *tctx)
{
	return true;
}

extern int topology_p_build_config(topology_ctx_t *tctx)
{
	/* No runtime state; expose parsed config directly via plugin_ctx. */
	tctx->plugin_ctx = tctx->config;
	return SLURM_SUCCESS;
}

extern int topology_p_destroy_config(topology_ctx_t *tctx)
{
	tctx->plugin_ctx = NULL;
	return SLURM_SUCCESS;
}

extern int topology_p_eval_node(topology_eval_t *topo_eval, int node_idx)
{
	return common_test_node(topo_eval, node_idx);
}

extern int topology_p_eval_nodes(topology_eval_t *topo_eval)
{
	return common_topo_choose_nodes(topo_eval);
}

extern int topology_p_whole_topo(bitstr_t *node_mask, void *tctx)
{
	return SLURM_SUCCESS;
}

/*
 * Compare two nodes by name using the same natural ordering used by
 * _sort_node_record_table_ptr() to sort the node table and by hostrange_cmp()
 * to sort hostlists, so that alphabetical order is consistent across Slurm.
 */
static int _sort_by_name(const void *a, const void *b)
{
	const alpha_sort_t *n1 = a;
	const alpha_sort_t *n2 = b;

	return strnatcmp(n1->name, n2->name);
}

extern int topology_p_get_rank(bitstr_t *node_bitmap, uint32_t **node_rank,
			       uint32_t *size, void *tctx)
{
	topology_flat_config_t *cfg = tctx;
	uint32_t count;
	uint32_t pos = 0;
	alpha_sort_t *order_map;

	xassert(node_rank);
	xassert(size);

	*node_rank = NULL;
	*size = 0;

	if (!cfg || !cfg->alpha_step_rank)
		return SLURM_SUCCESS;

	if (!node_bitmap)
		return SLURM_SUCCESS;

	count = bit_set_count(node_bitmap);
	if (!count)
		return SLURM_SUCCESS;

	order_map = xcalloc(count, sizeof(*order_map));
	for (int i = 0; next_node_bitmap(node_bitmap, &i); i++) {
		order_map[pos].pos = pos;
		order_map[pos].name = node_record_table_ptr[i]->name;
		pos++;
	}

	/* Sort by name to determine alpha position of each node. */
	qsort(order_map, count, sizeof(*order_map), _sort_by_name);
	*node_rank = xcalloc(count, sizeof(**node_rank));

	for (uint32_t i = 0; i < count; i++)
		(*node_rank)[order_map[i].pos] = i;

	*size = count;
	xfree(order_map);

	return SLURM_SUCCESS;
}

extern bitstr_t *topology_p_get_bitmap(char *name)
{
	return NULL;
}

extern bool topology_p_generate_node_ranking(topology_ctx_t *tctx)
{
	return false;
}

extern int topology_p_get_node_addr(
	char *node_name, char **paddr, char **ppattern, void *tctx)
{
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
	switch (type) {
	case TOPO_DATA_TOPOLOGY_PTR:
	{
		dynamic_plugin_data_t **topoinfo_pptr = data;

		*topoinfo_pptr = xmalloc(sizeof(dynamic_plugin_data_t));
		(*topoinfo_pptr)->data = NULL;
		(*topoinfo_pptr)->plugin_id = plugin_id;

		break;
	}
	case TOPO_DATA_REC_CNT:
	{
		int *rec_cnt = data;
		*rec_cnt = 0;
		break;
	}
	case TOPO_DATA_EXCLUSIVE_TOPO:
	{
		int *exclusive_topo = data;
		*exclusive_topo = 0;
		break;
	}
	default:
		error("Unsupported option %d", type);
	}

	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_free(void *topoinfo_ptr)
{
	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_pack(void *topoinfo_ptr, buf_t *buffer,
				    uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_print(void *topoinfo_ptr, char *nodes_list,
				     char *unit, char **out)
{
	*out = NULL;
	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_unpack(void **topoinfo_pptr, buf_t *buffer,
				      uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern void topology_p_jobinfo_free(
	void *topo_jobinfo)
{
	return;
}

extern void topology_p_jobinfo_pack(
	void *topo_jobinfo,
	buf_t *buffer,
	uint16_t protocol_version)
{
	return;
}

extern int topology_p_jobinfo_unpack(
	void **topo_jobinfo,
	buf_t *buffer,
	uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int topology_p_jobinfo_get(
	topology_jobinfo_type_t type,
	void *topo_jobinfo,
	void *data)
{
	return ESLURM_NOT_SUPPORTED;
}

extern uint32_t topology_p_get_fragmentation(bitstr_t *node_mask)
{
	return 0;
}

extern void topology_p_get_topology_str(node_record_t *node_ptr,
					char **topology_str_ptr,
					topology_ctx_t *tctx)
{
	return;
}
