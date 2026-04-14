/*****************************************************************************\
 *  torus3d_record.c - Parse and validate 3D torus configuration.
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

#include "torus3d_record.h"

#include <inttypes.h>
#include <limits.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/xstring.h"

extern uint32_t torus3d_coord_to_index(torus3d_record_t *torus, uint16_t x,
				       uint16_t y, uint16_t z)
{
	return x + (y * torus->x) + (z * torus->x * torus->y);
}

extern void torus3d_index_to_coord(torus3d_record_t *torus, uint32_t index,
				   uint16_t *x, uint16_t *y, uint16_t *z)
{
	*x = index % torus->x;
	*y = (index / torus->x) % torus->y;
	*z = index / ((uint32_t) torus->x * torus->y);
}

static uint16_t _wrap_add(uint16_t base, uint16_t delta, uint16_t limit)
{
	return (uint16_t) (((uint32_t) base + delta) % limit);
}

static void _log_placement(torus3d_placement_t *placement)
{
	debug("\tPlacement size:%u dims:%ux%ux%u", placement->size,
		      placement->dims.x, placement->dims.y, placement->dims.z);
	if (placement->anchor_bitmaps) {
		for (int i = 0; i < placement->anchor_count; i++) {
			char *tmp_str =
				bitmap2node_name(placement->anchor_bitmaps[i]);
			debug("\t\tnodes: %s",tmp_str);
			xfree(tmp_str);
		}
	}
}

static void _log_node_coordinates(torus3d_record_t *torus)
{
	for (uint32_t idx = 0; idx < torus->node_count; idx++) {
		uint16_t x, y, z;

		torus3d_index_to_coord(torus, idx, &x, &y, &z);

		if (torus->nodes_map[idx] == NO_VAL) {
			debug("\t(%u,%u,%u) -> unmapped", x, y, z);
			continue;
		}
		debug("\t(%u,%u,%u) -> %s",
		      x, y, z,
		      node_record_table_ptr[torus->nodes_map[idx]]->name);
	}
}

static void _log_toruses(torus3d_context_t *ctx)
{
	torus3d_record_t *torus;

	if (!ctx || !ctx->records)
		return;

	torus = ctx->records;
	for (int i = 0; i < ctx->record_count; i++, torus++) {
		char *nodes_str = bitmap2node_name(torus->nodes_bitmap);
		debug("Torus3d idx:%d name:%s dims:%ux%ux%u nodes:%s",
		      i, torus->name,
		      torus->x, torus->y, torus->z, nodes_str);
		xfree(nodes_str);
		_log_node_coordinates(torus);
		for (int j = 0; j < torus->placement_count; j++) {
			_log_placement(&(torus->placements[j]));
		}
	}
}

static uint16_t *_build_axis_positions(uint16_t size, uint16_t stride,
				       uint16_t *count)
{
	uint16_t *positions;
	uint16_t pos = 0;
	uint16_t idx = 0;

	positions = xcalloc(size, sizeof(*positions));
	positions[idx++] = 0;

	if (stride == 0) {
		*count = 1;
		return positions;
	}

	while (1) {
		pos = _wrap_add(pos, stride, size);
		if (pos == 0)
			break;
		positions[idx++] = pos;
	}

	*count = idx;
	return positions;
}

static bitstr_t *_build_anchor_bitmap(torus3d_record_t *torus,
				      slurm_conf_torus3d_placement_t *src,
				      uint16_t ax, uint16_t ay, uint16_t az)
{
	bitstr_t *bitmap = bit_alloc(node_record_count);

	for (uint16_t dz = 0; dz < src->dims.z; dz++) {
		uint16_t z = _wrap_add(az, dz, torus->z);
		for (uint16_t dy = 0; dy < src->dims.y; dy++) {
			uint16_t y = _wrap_add(ay, dy, torus->y);
			for (uint16_t dx = 0; dx < src->dims.x; dx++) {
				uint16_t x = _wrap_add(ax, dx, torus->x);
				uint32_t linear_idx =
					torus3d_coord_to_index(torus, x, y, z);
				uint32_t node_idx =
					torus->nodes_map[linear_idx];

				if (node_idx != NO_VAL)
					bit_set(bitmap, node_idx);
			}
		}
	}

	return bitmap;
}

static void _free_placement(torus3d_placement_t *placement)
{
	if (!placement)
		return;

	if (placement->anchor_bitmaps) {
		for (int i = 0; i < placement->anchor_count; i++)
			FREE_NULL_BITMAP(placement->anchor_bitmaps[i]);
	}
	xfree(placement->anchor_bitmaps);
	xfree(placement->anchor_nodes);
	xfree(placement->xs);
	xfree(placement->ys);
	xfree(placement->zs);
	placement->anchor_count = 0;
}

static int _build_placement_anchors(torus3d_record_t *torus,
				    slurm_conf_torus3d_placement_t *src,
				    torus3d_placement_t *placement)
{
	uint16_t spacing_x = src->anchor_spacing.x;
	uint16_t spacing_y = src->anchor_spacing.y;
	uint16_t spacing_z = src->anchor_spacing.z;
	uint16_t count_x = 0, count_y = 0, count_z = 0;
	uint16_t *xs = NULL, *ys = NULL, *zs = NULL;
	uint64_t anchor_total = 0;
	int idx = 0;

	if (!spacing_x || !spacing_y || !spacing_z) {
		error("Torus3d placement anchor_spacing can't be 0");
		return EINVAL;
	}

	if (spacing_x > torus->x || spacing_y > torus->y ||
	    spacing_z > torus->z) {
		error("Torus3d placement anchor_spacing must be within torus dimensions");
		return EINVAL;
	}

	xs = _build_axis_positions(torus->x, spacing_x, &count_x);
	ys = _build_axis_positions(torus->y, spacing_y, &count_y);
	zs = _build_axis_positions(torus->z, spacing_z, &count_z);

	anchor_total = (uint64_t) count_x * count_y * count_z;
	if (anchor_total == 0 || anchor_total > INT_MAX) {
		error("Torus3d placement has no anchors");
		xfree(xs);
		xfree(ys);
		xfree(zs);
		return EINVAL;
	}

	placement->anchor_count = (int) anchor_total;
	placement->anchor_bitmaps = xcalloc(placement->anchor_count,
					    sizeof(*placement->anchor_bitmaps));
	placement->anchor_nodes = xcalloc(placement->anchor_count,
					  sizeof(*placement->anchor_nodes));

	placement->xs = xs;
	placement->ys = ys;
	placement->zs = zs;
	placement->x_count = count_x;
	placement->y_count = count_y;
	placement->z_count = count_z;

	for (uint16_t ix = 0; ix < count_x; ix++) {
		for (uint16_t iy = 0; iy < count_y; iy++) {
			for (uint16_t iz = 0; iz < count_z; iz++) {
				bitstr_t *bitmap =
					_build_anchor_bitmap(torus, src, xs[ix],
							     ys[iy], zs[iz]);
				placement->anchor_bitmaps[idx] = bitmap;
				placement->anchor_nodes[idx] =
					bit_set_count(bitmap);
				idx++;
			}
		}
	}

	return SLURM_SUCCESS;
}

static int _validate_placement(torus3d_record_t *torus,
			       slurm_conf_torus3d_placement_t *src,
			       torus3d_placement_t *placement)
{
	uint64_t expected_size;

	if (!src->dims.x || !src->dims.y || !src->dims.z) {
		error("Torus3d placement dims must be non-zero");
		return EINVAL;
	}

	expected_size = (uint64_t) src->dims.x * src->dims.y * src->dims.z;
	if (expected_size > UINT32_MAX) {
		error("Torus3d placement dims %ux%ux%u exceed size limit",
		      src->dims.x, src->dims.y, src->dims.z);
		return EINVAL;
	}

	if (src->dims.x > torus->x || src->dims.y > torus->y ||
	    src->dims.z > torus->z) {
		error("Torus3d placement dims exceed torus dimensions");
		return EINVAL;
	}

	placement->size = (uint32_t) expected_size;
	placement->dims = src->dims;
	if ((src->anchor_spacing.x == 0) && (src->anchor_spacing.y == 0) &&
	    (src->anchor_spacing.z == 0)) {
		placement->anchor_spacing = src->dims;
		src->anchor_spacing = src->dims;
	} else {
		placement->anchor_spacing = src->anchor_spacing;
	}

	return _build_placement_anchors(torus, src, placement);
}

static int _validate_regions_config(slurm_conf_torus3d_t *config,
				    torus3d_record_t *torus,
				    hostlist_t **invalid_hl_ptr)
{
	for (int r = 0; r < config->region_count; r++) {
		slurm_conf_torus3d_region_t *region = &config->regions[r];
		uint64_t region_size = (uint64_t) region->dims.x *
				       region->dims.y * region->dims.z;
		uint32_t xy = (uint32_t) region->dims.x * region->dims.y;
		uint64_t idx;
		hostlist_t *host_list = NULL;

		if (!region->nodes || !region->nodes[0]) {
			error("Torus3d region missing nodes");
			return EINVAL;
		}
		if ((region->anchor.x + region->dims.x) > config->dims.x ||
		    (region->anchor.y + region->dims.y) > config->dims.y ||
		    (region->anchor.z + region->dims.z) > config->dims.z) {
			error("Torus3d region dims exceed torus dimensions");
			return EINVAL;
		}

		host_list = hostlist_create(region->nodes);
		if (!host_list) {
			error("hostlist_create error on torus3d region nodes");
			return EINVAL;
		}
		if (hostlist_count(host_list) != region_size) {
			error("Torus3d region node count does not match dims");
			hostlist_destroy(host_list);
			return EINVAL;
		}

		for (idx = 0; idx < region_size; idx++) {
			uint16_t x = idx % region->dims.x;
			uint16_t y = (idx / region->dims.x) % region->dims.y;
			uint16_t z = idx / xy;
			uint32_t linear_idx;
			node_record_t *node_ptr;
			char *node_name;

			node_name = hostlist_shift(host_list);
			xassert(node_name);

			node_ptr = find_node_record(node_name);
			if (!node_ptr) {
				if (!*invalid_hl_ptr)
					*invalid_hl_ptr = hostlist_create(NULL);
				hostlist_push_host(*invalid_hl_ptr, node_name);
				free(node_name);
				continue;
			}

			linear_idx =
				torus3d_coord_to_index(torus,
						       region->anchor.x + x,
						       region->anchor.y + y,
						       region->anchor.z + z);
			if (torus->nodes_map[linear_idx] != NO_VAL) {
				error("Torus3d region overlaps existing nodes");
				free(node_name);
				hostlist_destroy(host_list);
				return EINVAL;
			}

			torus->nodes_map[linear_idx] = node_ptr->index;
			bit_set(torus->nodes_bitmap, node_ptr->index);
			free(node_name);
		}

		hostlist_destroy(host_list);
	}

	return SLURM_SUCCESS;
}

static int _placement_cmp(const void *a, const void *b)
{
	const torus3d_placement_t *pa = a;
	const torus3d_placement_t *pb = b;

	if (pa->size < pb->size)
		return -1;
	if (pa->size > pb->size)
		return 1;
	return 0;
}

static int _validate_config(slurm_conf_torus3d_t *config,
			    torus3d_record_t *torus)
{
	hostlist_t *host_list;
	hostlist_t *invalid_hl = NULL;
	char *node_name;
	uint32_t node_count;
	uint64_t expected_count;
	uint32_t node_index = 0;
	int rc = SLURM_SUCCESS;

	if (!config->name || !config->name[0]) {
		error("Torus3d configuration missing name");
		return SLURM_ERROR;
	}

	if (config->nodes && config->region_count) {
		error("Torus3d configuration cannot specify both nodes and regions");
		return SLURM_ERROR;
	}

	if (!config->dims.x || !config->dims.y || !config->dims.z) {
		error("Torus3d configuration requires non-zero x,y,z");
		return SLURM_ERROR;
	}

	expected_count =
		((uint64_t) config->dims.x * config->dims.y * config->dims.z);

	torus->x = config->dims.x;
	torus->y = config->dims.y;
	torus->z = config->dims.z;
	torus->node_count = expected_count;
	torus->name = xstrdup(config->name);
	torus->nodes_map = xcalloc(expected_count, sizeof(*torus->nodes_map));

	for (uint32_t i = 0; i < expected_count; i++)
		torus->nodes_map[i] = NO_VAL;
	torus->nodes_bitmap = bit_alloc(node_record_count);

	if (config->region_count) {
		rc = _validate_regions_config(config, torus, &invalid_hl);
		if (rc != SLURM_SUCCESS)
			goto end;
	} else {
		if (!config->nodes || !config->nodes[0]) {
			error("Torus3d configuration missing nodes");
			return SLURM_ERROR;
		}

		if (!(host_list = hostlist_create(config->nodes))) {
			error("hostlist_create error on torus3d nodes");
			return SLURM_ERROR;
		}

		node_count = hostlist_count(host_list);
		if (expected_count != node_count) {
			error("Torus3d dimensions %ux%ux%u=%" PRIu64 " do not match node count %u",
			      config->dims.x, config->dims.y, config->dims.z,
			      expected_count, node_count);
			hostlist_destroy(host_list);
			return SLURM_ERROR;
		}

		while ((node_name = hostlist_shift(host_list))) {
			node_record_t *node_ptr = find_node_record(node_name);
			if (node_ptr) {
				torus->nodes_map[node_index++] =
					node_ptr->index;
				bit_set(torus->nodes_bitmap, node_ptr->index);
			} else {
				if (!invalid_hl)
					invalid_hl = hostlist_create(NULL);
				hostlist_push_host(invalid_hl, node_name);
				node_index++;
			}
			free(node_name);
		}
		hostlist_destroy(host_list);
	}

	if (config->placement_count > 0) {
		torus->placement_count = config->placement_count;
		torus->placements = xcalloc(torus->placement_count,
					    sizeof(*torus->placements));
		for (int i = 0; i < torus->placement_count; i++) {
			rc = _validate_placement(torus, &config->placements[i],
						 &torus->placements[i]);
			if (rc != SLURM_SUCCESS)
				goto end;
		}
		qsort(torus->placements, torus->placement_count,
		      sizeof(*torus->placements), _placement_cmp);
	}

end:
	if (invalid_hl) {
		char *buf = hostlist_ranged_string_xmalloc(invalid_hl);
		warning("Invalid hostnames in torus3d configuration: %s",
			buf);
		xfree(buf);
		hostlist_destroy(invalid_hl);
	}

	return rc;
}

extern int torus3d_record_validate(topology_ctx_t *tctx)
{
	topology_torus3d_config_t *config = tctx->config;
	torus3d_context_t *ctx = NULL;
	int rc;

	if (!config || (config->config_cnt == 0))
		fatal("No torus3d configurations found");

	ctx = xmalloc(sizeof(*ctx));
	ctx->record_count = config->config_cnt;
	ctx->records = xcalloc(ctx->record_count, sizeof(*ctx->records));
	ctx->placement_nodes_bitmap = bit_alloc(node_record_count);

	for (int i = 0; i < ctx->record_count; i++) {
		rc = _validate_config(&config->torus3d_configs[i],
				      &ctx->records[i]);
		if (rc != SLURM_SUCCESS) {
			fatal("Torus3d (%s) has invalid configuration",
			      config->torus3d_configs[i].name);
		}
		for (int j = 0; j < i; j++) {
			if (!xstrcmp(ctx->records[i].name,
				     ctx->records[j].name))
				fatal("Torus3d (%s) has already been defined",
				      ctx->records[i].name);
			if (bit_overlap_any(ctx->records[i].nodes_bitmap,
					    ctx->records[j].nodes_bitmap))
				fatal("Torus3d (%s) and (%s) share nodes",
				      ctx->records[i].name,
				      ctx->records[j].name);
		}
	}

	for (int i = 0; i < ctx->record_count; i++) {
		if (ctx->records[i].placement_count > 0) {
			bit_or(ctx->placement_nodes_bitmap,
			       ctx->records[i].nodes_bitmap);
		}
	}

	_log_toruses(ctx);

	tctx->plugin_ctx = ctx;
	return SLURM_SUCCESS;
}

static void _build_region(torus3d_record_t *torus, bitstr_t *visited,
			  uint16_t ax, uint16_t ay, uint16_t az,
			  slurm_conf_torus3d_region_t **regions_ptr,
			  int *region_count, int *region_alloc)
{
	uint16_t ex, ey, ez;
	slurm_conf_torus3d_region_t *r;
	hostlist_t *hl;

	ex = ax + 1;
	while (ex < torus->x) {
		uint32_t idx = torus3d_coord_to_index(torus, ex, ay, az);
		if (torus->nodes_map[idx] == NO_VAL || bit_test(visited, idx))
			break;
		ex++;
	}

	ey = ay + 1;
	while (ey < torus->y) {
		bool full = true;
		for (uint16_t x = ax; x < ex; x++) {
			uint32_t idx = torus3d_coord_to_index(torus, x, ey, az);
			if (torus->nodes_map[idx] == NO_VAL ||
			    bit_test(visited, idx)) {
				full = false;
				break;
			}
		}
		if (!full)
			break;
		ey++;
	}

	ez = az + 1;
	while (ez < torus->z) {
		bool full = true;
		for (uint16_t y = ay; y < ey && full; y++) {
			for (uint16_t x = ax; x < ex; x++) {
				uint32_t idx =
					torus3d_coord_to_index(torus, x, y, ez);
				if (torus->nodes_map[idx] == NO_VAL ||
				    bit_test(visited, idx)) {
					full = false;
					break;
				}
			}
		}
		if (!full)
			break;
		ez++;
	}

	for (uint16_t z = az; z < ez; z++)
		for (uint16_t y = ay; y < ey; y++)
			for (uint16_t x = ax; x < ex; x++)
				bit_set(visited,
					torus3d_coord_to_index(torus, x, y, z));

	if (*region_count >= *region_alloc) {
		*region_alloc *= 2;
		xrecalloc(*regions_ptr, *region_alloc, sizeof(**regions_ptr));
	}

	r = &(*regions_ptr)[(*region_count)++];
	r->anchor.x = ax;
	r->anchor.y = ay;
	r->anchor.z = az;
	r->dims.x = ex - ax;
	r->dims.y = ey - ay;
	r->dims.z = ez - az;

	hl = hostlist_create(NULL);
	for (uint16_t z = az; z < ez; z++) {
		for (uint16_t y = ay; y < ey; y++) {
			for (uint16_t x = ax; x < ex; x++) {
				uint32_t linear_idx =
					torus3d_coord_to_index(torus, x, y, z);
				uint32_t node_index =
					torus->nodes_map[linear_idx];
				node_record_t *node_ptr =
					node_record_table_ptr[node_index];
				hostlist_push_host(hl, node_ptr->name);
			}
		}
	}

	r->nodes = hostlist_ranged_string_xmalloc(hl);
	hostlist_destroy(hl);
}

static void _rebuild_regions(torus3d_record_t *torus,
			     slurm_conf_torus3d_t *config)
{
	bitstr_t *visited = bit_alloc(torus->node_count);
	int region_count = 0;
	int region_alloc = 8;
	slurm_conf_torus3d_region_t *regions =
		xcalloc(region_alloc, sizeof(*regions));

	for (uint16_t az = 0; az < torus->z; az++) {
		for (uint16_t ay = 0; ay < torus->y; ay++) {
			for (uint16_t ax = 0; ax < torus->x; ax++) {
				uint32_t li = torus3d_coord_to_index(torus, ax,
								     ay, az);
				if (torus->nodes_map[li] == NO_VAL ||
				    bit_test(visited, li))
					continue;
				_build_region(torus, visited, ax, ay, az,
					      &regions, &region_count,
					      &region_alloc);
			}
		}
	}

	FREE_NULL_BITMAP(visited);

	config->regions = regions;
	config->region_count = region_count;
}

extern void torus3d_record_update_torus_config(topology_ctx_t *tctx, int idx)
{
	torus3d_context_t *ctx = tctx->plugin_ctx;
	topology_torus3d_config_t *torus_config = tctx->config;
	slurm_conf_torus3d_t *config;

	if (!torus_config)
		return;

	config = &torus_config->torus3d_configs[idx];

	xfree(config->nodes);
	if (config->regions) {
		for (int i = 0; i < config->region_count; i++)
			xfree(config->regions[i].nodes);
		xfree(config->regions);
	}

	_rebuild_regions(&ctx->records[idx],
			 &torus_config->torus3d_configs[idx]);
}

extern void torus3d_record_table_destroy(torus3d_context_t *ctx)
{
	if (!ctx)
		return;

	if (ctx->records) {
		for (int i = 0; i < ctx->record_count; i++) {
			xfree(ctx->records[i].nodes_map);
			FREE_NULL_BITMAP(ctx->records[i].nodes_bitmap);
			for (int j = 0; j < ctx->records[i].placement_count;
			     j++)
				_free_placement(&ctx->records[i].placements[j]);
			xfree(ctx->records[i].name);
			xfree(ctx->records[i].placements);
		}
	}
	FREE_NULL_BITMAP(ctx->placement_nodes_bitmap);
	xfree(ctx->records);
	xfree(ctx);
}
