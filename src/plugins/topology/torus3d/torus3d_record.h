/*****************************************************************************\
 *  torus3d_record.h - Parse and validate 3D torus configuration.
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

#ifndef _TOPO_TORUS3D_RECORD_H
#define _TOPO_TORUS3D_RECORD_H

#include "../common/common_topo.h"

typedef struct torus3d_placement {
	bitstr_t **anchor_bitmaps;
	uint32_t *anchor_nodes;
	int anchor_count;
	slurm_conf_torus3d_dims_t anchor_spacing;
	slurm_conf_torus3d_dims_t dims;
	uint32_t size;
	uint16_t *xs;
	uint16_t *ys;
	uint16_t *zs;
	uint16_t x_count;
	uint16_t y_count;
	uint16_t z_count;
} torus3d_placement_t;

typedef struct {
	char *name;
	uint32_t node_count;
	bitstr_t *nodes_bitmap;
	uint32_t *nodes_map;
	int placement_count;
	torus3d_placement_t *placements;
	uint16_t x;
	uint16_t y;
	uint16_t z;
} torus3d_record_t;

typedef struct {
	bitstr_t *placement_nodes_bitmap;
	int record_count;
	torus3d_record_t *records;
} torus3d_context_t;

extern uint32_t torus3d_coord_to_index(torus3d_record_t *torus, uint16_t x,
				       uint16_t y, uint16_t z);

extern void torus3d_index_to_coord(torus3d_record_t *torus, uint32_t index,
				   uint16_t *x, uint16_t *y, uint16_t *z);

extern int torus3d_record_validate(topology_ctx_t *tctx);

extern void torus3d_record_update_torus_config(topology_ctx_t *tctx, int idx);

extern void torus3d_record_table_destroy(torus3d_context_t *ctx);

#endif
