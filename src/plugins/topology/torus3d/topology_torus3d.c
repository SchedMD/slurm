/*****************************************************************************\
 *  topology_torus3d.c - 3D torus topology plugin
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/xstring.h"

#include "../common/common_topo.h"
#include "eval_nodes_torus3d.h"
#include "torus3d_record.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined(__APPLE__)
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
#else
node_record_t **node_record_table_ptr;
int node_record_count;
#endif

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 */
const char plugin_name[] = "topology 3D torus plugin";
const char plugin_type[] = "topology/torus3d";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const uint32_t plugin_id = TOPOLOGY_PLUGIN_3DTORUS;
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

extern int topology_p_build_config(topology_ctx_t *tctx)
{
	return torus3d_record_validate(tctx);
}

extern int topology_p_destroy_config(topology_ctx_t *tctx)
{
	torus3d_record_table_destroy(tctx->plugin_ctx);
	tctx->plugin_ctx = NULL;
	return SLURM_SUCCESS;
}

extern int topology_p_eval_node(topology_eval_t *topo_eval, int node_idx)
{
	return common_test_node(topo_eval, node_idx);
}

extern int topology_p_eval_nodes(topology_eval_t *topo_eval)
{
	torus3d_context_t *ctx = topo_eval->tctx->plugin_ctx;
	if (ctx->placement_nodes_bitmap &&
	    bit_overlap_any(ctx->placement_nodes_bitmap, topo_eval->node_map)) {
		topo_eval->eval_nodes = eval_nodes_torus3d;
		topo_eval->trump_others = true;
	}

	return common_topo_choose_nodes(topo_eval);
}

extern int topology_p_whole_topo(bitstr_t *node_mask, void *tctx)
{
	return SLURM_SUCCESS;
}

extern bitstr_t *topology_p_get_bitmap(char *name, void *tctx)
{
	return NULL;
}

extern bool topology_p_generate_node_ranking(topology_ctx_t *tctx)
{
	return false;
}

extern int topology_p_get_node_addr(char *node_name, char **paddr,
				    char **ppattern, void *tctx)
{
	return common_topo_get_node_addr(node_name, paddr, ppattern);
}

extern int topology_p_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				     int *count, uint16_t tree_width,
				     void *tctx)
{
	return common_topo_split_hostlist_treewidth(hl, sp_hl, count,
						    tree_width);
}

extern int topology_p_get(topology_data_t type, void *data, void *tctx)
{
	torus3d_context_t *ctx = tctx;

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
		*rec_cnt = ctx ? ctx->record_count : 0;
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

extern void topology_p_jobinfo_free(void *topo_jobinfo)
{
	return;
}

extern void topology_p_jobinfo_pack(void *topo_jobinfo, buf_t *buffer,
				    uint16_t protocol_version)
{
	return;
}

extern int topology_p_jobinfo_unpack(void **topo_jobinfo, buf_t *buffer,
				     uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int topology_p_jobinfo_get(topology_jobinfo_type_t type,
				  void *topo_jobinfo, void *data)
{
	return ESLURM_NOT_SUPPORTED;
}

extern uint32_t topology_p_get_fragmentation(bitstr_t *node_mask, void *tctx)
{
	return 0;
}

extern void topology_p_get_topology_str(node_record_t *node_ptr,
					char **topology_str_ptr,
					topology_ctx_t *tctx)
{
	return;
}
