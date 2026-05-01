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

typedef struct {
	uint16_t x_size;
	uint16_t y_size;
	uint16_t z_size;
	uint32_t anchor_count;
} topoinfo_torus3d_placement_t;

typedef struct {
	char *name;
	char *nodes;
	uint16_t x_size;
	uint16_t y_size;
	uint16_t z_size;
	uint32_t placement_count;
	topoinfo_torus3d_placement_t *placements;
} topoinfo_torus3d_record_t;

typedef struct {
	uint32_t record_count;
	topoinfo_torus3d_record_t *topo_array;
} topoinfo_torus3d_t;

static void _print_topo_record(topoinfo_torus3d_record_t *rec, char **out)
{
	char *env, *line = NULL, *pos = NULL;

	xstrfmtcatat(line, &pos, "TorusName=%s Dims=%ux%ux%u", rec->name,
		     rec->x_size, rec->y_size, rec->z_size);

	if (rec->nodes)
		xstrfmtcatat(line, &pos, " Nodes=%s", rec->nodes);

	if ((env = getenv("SLURM_TOPO_LEN")))
		xstrfmtcat(*out, "%.*s\n", atoi(env), line);
	else
		xstrfmtcat(*out, "%s\n", line);
	xfree(line);

	for (uint32_t i = 0; i < rec->placement_count; i++) {
		topoinfo_torus3d_placement_t *p = &rec->placements[i];
		line = NULL;
		pos = NULL;
		xstrfmtcatat(line, &pos,
			     "  TorusName=%s PlacementDims=%ux%ux%u(%u)"
			     " Anchors=%u",
			     rec->name, p->x_size, p->y_size, p->z_size,
			     (uint32_t) p->x_size * p->y_size * p->z_size,
			     p->anchor_count);

		if ((env = getenv("SLURM_TOPO_LEN")))
			xstrfmtcat(*out, "%.*s\n", atoi(env), line);
		else
			xstrfmtcat(*out, "%s\n", line);
		xfree(line);
	}
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

static torus3d_record_t *_find_torus_by_node(torus3d_context_t *ctx,
					     node_record_t *node_ptr)
{
	for (int i = 0; i < ctx->record_count; i++) {
		torus3d_record_t *torus = &ctx->records[i];
		if (!bit_test(torus->nodes_bitmap, node_ptr->index))
			continue;
		return torus;
	}

	return NULL;
}

static torus3d_record_t *_find_torus_by_name(torus3d_context_t *ctx,
					     char *torus_name)
{
	for (int i = 0; i < ctx->record_count; i++) {
		torus3d_record_t *torus = &ctx->records[i];
		if (xstrcmp(torus->name, torus_name))
			continue;
		return torus;
	}

	return NULL;
}

static void _remove_node_from_placements(torus3d_record_t *torus,
					 torus3d_context_t *ctx, int node_idx)
{
	if (!bit_test(ctx->placement_nodes_bitmap, node_idx))
		return;

	for (int j = 0; j < torus->placement_count; j++) {
		torus3d_placement_t *p = &torus->placements[j];
		for (int k = 0; k < p->anchor_count; k++) {
			if (bit_test(p->anchor_bitmaps[k], node_idx)) {
				bit_clear(p->anchor_bitmaps[k], node_idx);
				p->anchor_nodes[k]--;
			}
		}
	}
	bit_clear(ctx->placement_nodes_bitmap, node_idx);
}

static bool _coord_in_range(uint16_t pos, uint16_t start, uint16_t size,
			    uint16_t limit)
{
	bool wrap = ((start + size) > limit);

	if (wrap)
		return ((pos >= start) || pos < ((start + size) % limit));

	return (pos >= start && pos < (start + size));
}

static void _add_node_to_placements(torus3d_record_t *torus,
				    torus3d_context_t *ctx, int node_idx,
				    uint16_t x, uint16_t y, uint16_t z)
{
	bool added = false;
	for (int j = 0; j < torus->placement_count; j++) {
		torus3d_placement_t *p = &torus->placements[j];
		uint16_t *match_x, *match_y, *match_z;
		int nx = 0, ny = 0, nz = 0;

		match_x = xcalloc(p->x_count, sizeof(*match_x));
		match_y = xcalloc(p->y_count, sizeof(*match_y));
		match_z = xcalloc(p->z_count, sizeof(*match_z));

		for (uint16_t i = 0; i < p->x_count; i++)
			if (_coord_in_range(x, p->xs[i], p->dims.x, torus->x))
				match_x[nx++] = i;
		for (uint16_t i = 0; i < p->y_count; i++)
			if (_coord_in_range(y, p->ys[i], p->dims.y, torus->y))
				match_y[ny++] = i;
		for (uint16_t i = 0; i < p->z_count; i++)
			if (_coord_in_range(z, p->zs[i], p->dims.z, torus->z))
				match_z[nz++] = i;

		for (int mx = 0; mx < nx; mx++) {
			for (int my = 0; my < ny; my++) {
				for (int mz = 0; mz < nz; mz++) {
					int idx = match_x[mx] * p->y_count *
							  p->z_count +
						  match_y[my] * p->z_count +
						  match_z[mz];
					bit_set(p->anchor_bitmaps[idx],
						node_idx);
					p->anchor_nodes[idx]++;
					added = true;
				}
			}
		}

		xfree(match_x);
		xfree(match_y);
		xfree(match_z);
	}
	if (added)
		bit_set(ctx->placement_nodes_bitmap, node_idx);
}

static void _remove_node_from_torus(torus3d_record_t *torus,
				    torus3d_context_t *ctx,
				    node_record_t *node_ptr)
{
	for (uint32_t idx = 0; idx < torus->node_count; idx++) {
		if (torus->nodes_map[idx] == node_ptr->index) {
			torus->nodes_map[idx] = NO_VAL;
			break;
		}
	}

	bit_clear(torus->nodes_bitmap, node_ptr->index);

	_remove_node_from_placements(torus, ctx, node_ptr->index);
}

extern int topology_p_add_rm_node(node_record_t *node_ptr, char *unit,
				  topology_ctx_t *tctx)
{
	torus3d_context_t *ctx = tctx->plugin_ctx;
	char *tmp, *torus_name;
	char *tok, *endptr;
	unsigned long val;
	uint16_t coord[3] = { 0 };
	uint32_t linear_idx;
	bool in_torus;
	torus3d_record_t *torus_dst = NULL;
	torus3d_record_t *torus_src = NULL;

	if (!unit) {
		torus3d_record_t *torus = _find_torus_by_node(ctx, node_ptr);

		if (!torus)
			return SLURM_SUCCESS;

		debug2("remove %s from torus3d:%s", node_ptr->name, tctx->name);

		_remove_node_from_torus(torus, ctx, node_ptr);
		torus3d_record_update_torus_config(tctx, torus - ctx->records);

		return SLURM_SUCCESS;
	}

	tmp = xstrdup(unit);
	torus_name = tmp;
	tok = strchr(torus_name, ':');
	if (!tok) {
		error("invalid torus3d unit '%s' for %s (expected torus_name:x:y:z)",
		      unit, node_ptr->name);
		xfree(tmp);
		return SLURM_ERROR;
	}
	*tok++ = '\0';

	for (int i = 0; i < 3; i++) {
		val = strtoul(tok, &endptr, 0);
		if (tok == endptr || val > UINT16_MAX ||
		    ((*endptr != ':') && ((i < 2) || (*endptr != '\0')))) {
			error("invalid coordinate in '%s' for %s", unit,
			      node_ptr->name);
			xfree(tmp);
			return SLURM_ERROR;
		}
		coord[i] = (uint16_t) val;
		tok = endptr + 1;
	}

	torus_dst = _find_torus_by_name(ctx, torus_name);

	if (!torus_dst) {
		error("torus3d '%s' not found for %s", torus_name,
		      node_ptr->name);
		xfree(tmp);
		return SLURM_ERROR;
	}

	xfree(tmp);

	/* Validate coordinates */
	if (coord[0] >= torus_dst->x || coord[1] >= torus_dst->y ||
	    coord[2] >= torus_dst->z) {
		error("Can't add node %s, coordinates (%u,%u,%u) out of bounds for torus %s (%ux%ux%u)",
		      node_ptr->name, coord[0], coord[1], coord[2],
		      torus_dst->name, torus_dst->x, torus_dst->y,
		      torus_dst->z);
		return SLURM_ERROR;
	}

	linear_idx =
		torus3d_coord_to_index(torus_dst, coord[0], coord[1], coord[2]);

	if (torus_dst->nodes_map[linear_idx] == node_ptr->index) {
		debug2("node %s already at (%u,%u,%u) in torus %s",
		       node_ptr->name, coord[0], coord[1], coord[2],
		       torus_dst->name);
		return SLURM_SUCCESS;
	}

	/* Check if cell is occupied by a different node */
	if (torus_dst->nodes_map[linear_idx] != NO_VAL) {
		uint32_t current_idx = torus_dst->nodes_map[linear_idx];
		error("torus3d %s cell (%u,%u,%u) already occupied by %s, cannot add %s",
		      torus_dst->name, coord[0], coord[1], coord[2],
		      node_record_table_ptr[current_idx]->name,
		      node_ptr->name);
		return SLURM_ERROR;
	}

	debug2("add %s to torus3d %s at (%u,%u,%u)",
	       node_ptr->name, torus_dst->name, coord[0], coord[1], coord[2]);

	in_torus = bit_test(torus_dst->nodes_bitmap, node_ptr->index);

	if (!in_torus && (torus_src = _find_torus_by_node(ctx, node_ptr))) {
		_remove_node_from_torus(torus_src, ctx, node_ptr);
		torus3d_record_update_torus_config(tctx,
						   torus_src - ctx->records);
	}

	if (in_torus) {
		for (uint32_t idx = 0; idx < torus_dst->node_count; idx++) {
			if (torus_dst->nodes_map[idx] == node_ptr->index) {
				torus_dst->nodes_map[idx] = NO_VAL;
				break;
			}
		}
	}

	torus_dst->nodes_map[linear_idx] = node_ptr->index;
	bit_set(torus_dst->nodes_bitmap, node_ptr->index);

	_remove_node_from_placements(torus_dst, ctx, node_ptr->index);

	_add_node_to_placements(torus_dst, ctx, node_ptr->index, coord[0],
				coord[1], coord[2]);

	torus3d_record_update_torus_config(tctx, torus_dst - ctx->records);

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
	torus3d_context_t *ctx = tctx;

	for (int i = 0; i < ctx->record_count; i++) {
		if (bit_overlap_any(ctx->records[i].nodes_bitmap, node_mask))
			bit_or(node_mask, ctx->records[i].nodes_bitmap);
	}

	return SLURM_SUCCESS;
}

extern bitstr_t *topology_p_get_bitmap(char *name, void *tctx)
{
	torus3d_context_t *ctx = tctx;

	for (int i = 0; i < ctx->record_count; i++) {
		if (!xstrcmp(ctx->records[i].name, name))
			return ctx->records[i].nodes_bitmap;
	}

	return NULL;
}

extern bool topology_p_generate_node_ranking(topology_ctx_t *tctx)
{
	return false;
}

extern int topology_p_get_node_addr(char *node_name, char **paddr,
				    char **ppattern, void *tctx)
{
	node_record_t *node_ptr = find_node_record(node_name);
	torus3d_context_t *ctx = tctx;

	if (!node_ptr)
		return SLURM_ERROR;

	for (int i = 0; i < ctx->record_count; i++) {
		torus3d_record_t *torus = &ctx->records[i];
		if (!bit_test(torus->nodes_bitmap, node_ptr->index))
			continue;
		for (uint32_t idx = 0; idx < torus->node_count; idx++) {
			uint16_t x, y, z;
			if (torus->nodes_map[idx] != node_ptr->index)
				continue;
			torus3d_index_to_coord(torus, idx, &x, &y, &z);
			*paddr = xstrdup_printf("%s:%u:%u:%u.%s", torus->name,
						x, y, z, node_name);
			*ppattern = xstrdup("torus:x:y:z.node");
			return SLURM_SUCCESS;
		}
	}

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
		topoinfo_torus3d_t *topoinfo = xmalloc(sizeof(*topoinfo));

		*topoinfo_pptr = xmalloc(sizeof(**topoinfo_pptr));
		(*topoinfo_pptr)->data = topoinfo;
		(*topoinfo_pptr)->plugin_id = plugin_id;

		topoinfo->record_count = ctx ? ctx->record_count : 0;
		topoinfo->topo_array = xcalloc(topoinfo->record_count,
					       sizeof(*topoinfo->topo_array));

		for (uint32_t i = 0; i < topoinfo->record_count; i++) {
			torus3d_record_t *torus = &ctx->records[i];
			topoinfo_torus3d_record_t *rec =
				&topoinfo->topo_array[i];

			rec->name = xstrdup(torus->name);
			rec->nodes = bitmap2node_name(torus->nodes_bitmap);
			rec->x_size = torus->x;
			rec->y_size = torus->y;
			rec->z_size = torus->z;
			rec->placement_count = torus->placement_count;
			rec->placements = xcalloc(rec->placement_count,
						  sizeof(*rec->placements));
			for (uint32_t j = 0; j < rec->placement_count; j++) {
				torus3d_placement_t *p = &torus->placements[j];
				rec->placements[j].x_size = p->dims.x;
				rec->placements[j].y_size = p->dims.y;
				rec->placements[j].z_size = p->dims.z;
				rec->placements[j].anchor_count =
					p->anchor_count;
			}
		}

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
	topoinfo_torus3d_t *topoinfo = topoinfo_ptr;
	if (topoinfo) {
		for (uint32_t i = 0; i < topoinfo->record_count; i++) {
			xfree(topoinfo->topo_array[i].name);
			xfree(topoinfo->topo_array[i].nodes);
			xfree(topoinfo->topo_array[i].placements);
		}
		xfree(topoinfo->topo_array);
		xfree(topoinfo);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_pack(void *topoinfo_ptr, buf_t *buffer,
				    uint16_t protocol_version)
{
	topoinfo_torus3d_t *topoinfo = topoinfo_ptr;

	if (protocol_version >= SLURM_26_05_PROTOCOL_VERSION) {
		pack32(topoinfo->record_count, buffer);
		for (uint32_t i = 0; i < topoinfo->record_count; i++) {
			topoinfo_torus3d_record_t *rec =
				&topoinfo->topo_array[i];
			packstr(rec->name, buffer);
			packstr(rec->nodes, buffer);
			pack16(rec->x_size, buffer);
			pack16(rec->y_size, buffer);
			pack16(rec->z_size, buffer);
			pack32(rec->placement_count, buffer);
			for (uint32_t j = 0; j < rec->placement_count; j++) {
				pack16(rec->placements[j].x_size, buffer);
				pack16(rec->placements[j].y_size, buffer);
				pack16(rec->placements[j].z_size, buffer);
				pack32(rec->placements[j].anchor_count, buffer);
			}
		}
	} else {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int topology_p_topoinfo_print(void *topoinfo_ptr, char *nodes_list,
				     char *unit, char **out)
{
	uint32_t i, match, match_cnt = 0;
	topoinfo_torus3d_t *topoinfo = topoinfo_ptr;

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
		error("Topology information contains no torus%s%s%s%s",
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
	topoinfo_torus3d_t *topoinfo = xmalloc(sizeof(*topoinfo));

	*topoinfo_pptr = topoinfo;
	if (protocol_version >= SLURM_26_05_PROTOCOL_VERSION) {
		safe_unpack32(&topoinfo->record_count, buffer);
		safe_xcalloc(topoinfo->topo_array, topoinfo->record_count,
			     sizeof(*topoinfo->topo_array));
		for (uint32_t i = 0; i < topoinfo->record_count; i++) {
			topoinfo_torus3d_record_t *rec =
				&topoinfo->topo_array[i];
			safe_unpackstr(&rec->name, buffer);
			safe_unpackstr(&rec->nodes, buffer);
			safe_unpack16(&rec->x_size, buffer);
			safe_unpack16(&rec->y_size, buffer);
			safe_unpack16(&rec->z_size, buffer);
			safe_unpack32(&rec->placement_count, buffer);
			safe_xcalloc(rec->placements, rec->placement_count,
				     sizeof(*rec->placements));
			for (uint32_t j = 0; j < rec->placement_count; j++) {
				safe_unpack16(&rec->placements[j].x_size,
					      buffer);
				safe_unpack16(&rec->placements[j].y_size,
					      buffer);
				safe_unpack16(&rec->placements[j].z_size,
					      buffer);
				safe_unpack32(&rec->placements[j].anchor_count,
					      buffer);
			}
		}
	} else {
		goto unpack_error;
	}

	return SLURM_SUCCESS;

unpack_error:
	topology_p_topoinfo_free(topoinfo);
	*topoinfo_pptr = NULL;
	return SLURM_ERROR;
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
	torus3d_context_t *ctx = tctx;
	uint32_t frag = 0;

	if (!ctx)
		return 0;

	for (int i = 0; i < ctx->record_count; i++) {
		torus3d_record_t *torus = &ctx->records[i];
		for (int j = 0; j < torus->placement_count; j++) {
			torus3d_placement_t *p = &torus->placements[j];
			for (int k = 0; k < p->anchor_count; k++) {
				if (!bit_super_set(p->anchor_bitmaps[k],
						   node_mask))
					frag += p->size;
			}
		}
	}

	return frag;
}

/*
 * Find the minimal wrapping span for a set of coordinates on a circular axis.
 * Sets the start position and span size.
 */
static void _min_wrap_span(bitstr_t *axis_bitmap, uint16_t *start,
			   uint16_t *span)
{
	int axis_size = bit_size(axis_bitmap);
	int coord_set;
	int box_start, box_end;
	int prev_coord;
	int max_gap;

	coord_set = bit_set_count(axis_bitmap);

	/* Single coordinate */
	if (coord_set == 1) {
		*start = bit_ffs(axis_bitmap);
		*span = 1;
		return;
		/* All positions occupied */
	} else if (coord_set == axis_size) {
		*start = 0;
		*span = axis_size;
		return;
	}

	box_start = bit_ffs(axis_bitmap);
	box_end = bit_fls(axis_bitmap);

	/*
	 * If internal gaps can't exceed the wrap-around gap, the linear
	 * [box_start, box_end] span is already optimal.
	 */
	if ((2 * (box_end - box_start)) < (axis_size + coord_set)) {
		*start = box_start;
		*span = (box_end - box_start + 1);
		return;
	}

	max_gap = axis_size - (box_end - box_start + 1);
	prev_coord = box_start;
	for (int i = prev_coord + 1;
	     (i = bit_ffs_from_bit(axis_bitmap, i)) >= 0; i++) {
		int gap = i - prev_coord - 1;
		if (gap > max_gap) {
			box_start = i;
			box_end = prev_coord;
			max_gap = gap;
		}
		prev_coord = i;
	}

	*start = box_start;
	*span = axis_size - max_gap;
}

static uint32_t _morton_encode(uint16_t x, uint16_t y, uint16_t z)
{
	uint32_t result = 0;
	for (int i = 0; i < 5; i++) {
		result |= ((uint32_t) ((x >> i) & 1)) << (3 * i);
		result |= ((uint32_t) ((y >> i) & 1)) << (3 * i + 1);
		result |= ((uint32_t) ((z >> i) & 1)) << (3 * i + 2);
	}
	return result;
}

extern int topology_p_get_rank(bitstr_t *node_bitmap, uint32_t **node_rank,
			       uint32_t *size, void *tctx)
{
	uint32_t count = 0;
	torus3d_context_t *ctx = tctx;

	xassert(node_rank);
	xassert(size);

	*node_rank = NULL;
	*size = 0;

	if (!node_bitmap)
		return SLURM_SUCCESS;

	count = bit_set_count(node_bitmap);

	if (!count)
		return SLURM_SUCCESS;

	*node_rank = xcalloc(count, sizeof(**node_rank));
	*size = count;

	for (int t = 0; t < ctx->record_count; t++) {
		torus3d_record_t *torus = &ctx->records[t];
		bitstr_t *x_bitmap, *y_bitmap, *z_bitmap;
		uint16_t x_start, y_start, z_start;
		uint16_t x_span, y_span, z_span;
		uint32_t rank_idx = 0;

		if (!bit_overlap_any(torus->nodes_bitmap, node_bitmap))
			continue;

		if (bit_super_set(torus->nodes_bitmap, node_bitmap)) {
			x_start = 0;
			y_start = 0;
			z_start = 0;
			x_span = torus->x;
			y_span = torus->y;
			z_span = torus->z;
			goto whole_torus;
		}

		x_bitmap = bit_alloc(torus->x);
		y_bitmap = bit_alloc(torus->y);
		z_bitmap = bit_alloc(torus->z);

		for (uint32_t idx = 0; idx < torus->node_count; idx++) {
			uint32_t node_idx = torus->nodes_map[idx];
			uint16_t x, y, z;
			if (node_idx == NO_VAL)
				continue;
			if (!bit_test(node_bitmap, node_idx))
				continue;
			torus3d_index_to_coord(torus, idx, &x, &y, &z);
			bit_set(x_bitmap, x);
			bit_set(y_bitmap, y);
			bit_set(z_bitmap, z);
		}

		_min_wrap_span(x_bitmap, &x_start, &x_span);
		_min_wrap_span(y_bitmap, &y_start, &y_span);
		_min_wrap_span(z_bitmap, &z_start, &z_span);

		FREE_NULL_BITMAP(x_bitmap);
		FREE_NULL_BITMAP(y_bitmap);
		FREE_NULL_BITMAP(z_bitmap);
whole_torus:
		for (int i = 0; next_node_bitmap(node_bitmap, &i); i++) {
			if (!bit_test(torus->nodes_bitmap, i)) {
				rank_idx++;
				continue;
			}
			for (uint32_t idx = 0; idx < torus->node_count; idx++) {
				uint16_t x, y, z;
				if (torus->nodes_map[idx] != (uint32_t) i)
					continue;
				torus3d_index_to_coord(torus, idx, &x, &y, &z);
				uint16_t rx = (x + torus->x - x_start) % x_span;
				uint16_t ry = (y + torus->y - y_start) % y_span;
				uint16_t rz = (z + torus->z - z_start) % z_span;
				(*node_rank)[rank_idx] =
					((uint32_t) (t + 1)
					 << TOPO_RANK_ID_SHIFT) |
					_morton_encode(rx, ry, rz);
				break;
			}
			rank_idx++;
		}
	}

	return SLURM_SUCCESS;
}

extern void topology_p_get_topology_str(node_record_t *node_ptr,
					char **topology_str_ptr,
					topology_ctx_t *tctx)
{
	torus3d_context_t *ctx = tctx->plugin_ctx;

	if (!ctx)
		return;

	for (int i = 0; i < ctx->record_count; i++) {
		torus3d_record_t *torus = &ctx->records[i];
		if (!bit_test(torus->nodes_bitmap, node_ptr->index))
			continue;
		for (uint32_t idx = 0; idx < torus->node_count; idx++) {
			uint16_t x, y, z;
			if (torus->nodes_map[idx] != node_ptr->index)
				continue;
			torus3d_index_to_coord(torus, idx, &x, &y, &z);
			xstrfmtcat(*topology_str_ptr, "%s%s:%s:%u:%u:%u",
				   *topology_str_ptr ? "," : "", tctx->name,
				   torus->name, x, y, z);
			return;
		}
	}
}
