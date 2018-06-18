/*****************************************************************************\
 *  grid_functions.c - Functions related to curses display of smap.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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

#include "src/smap/smap.h"

static void _calc_coord_3d(int x, int y, int z, int default_y_offset,
			   int *coord_x, int *coord_y, int *dim_size)
{
	int y_offset;

	*coord_x = (x + (dim_size[2] - 1)) - z + 2;
	y_offset = default_y_offset - (dim_size[2] * y);
	*coord_y = (y_offset - y) + z;
}

extern int *get_cluster_dims(node_info_msg_t *node_info_ptr)
{
	int *dim_size = slurmdb_setup_cluster_dim_size();

	if ((params.cluster_flags & CLUSTER_FLAG_CRAY) && dim_size) {
		static int cray_dim_size[3] = {-1, -1, -1};
		/* For now, assume one node per coordinate all
		 * May need to refine. */
		cray_dim_size[0] = dim_size[0];
		cray_dim_size[1] = dim_size[1];
		cray_dim_size[2] = dim_size[2];
		return cray_dim_size;
	}

	if ((dim_size == NULL) && node_info_ptr) {
		static int default_dim_size[1];
		default_dim_size[0] = node_info_ptr->record_count;
		return default_dim_size;
	}

	return dim_size;
}

extern void set_grid_inx(int start, int end, int count)
{
	int i;

	if (!smap_system_ptr || !smap_system_ptr->grid)
		return;

	for (i = 0; i < smap_system_ptr->node_cnt; i++) {
		if (!smap_system_ptr->grid[i])		/* Null node name */
			continue;
		if ((smap_system_ptr->grid[i]->index < start) ||
		    (smap_system_ptr->grid[i]->index > end))
			continue;
		if ((smap_system_ptr->grid[i]->state == NODE_STATE_DOWN) ||
		    (smap_system_ptr->grid[i]->state & NODE_STATE_DRAIN))
			continue;

		smap_system_ptr->grid[i]->letter = letters[count%62];
		smap_system_ptr->grid[i]->color  = colors[count%6];
	}
}

/* Build the smap_system_ptr structure from the node records */
extern void init_grid(node_info_msg_t *node_info_ptr, int cols)
{
	int i, j, len;
	int default_y_offset = 0;
	smap_node_t *smap_node;

	smap_system_ptr = xmalloc(sizeof(smap_system_t));

	if (!node_info_ptr) {
		return;
	} else {
		smap_system_ptr->grid = xmalloc(sizeof(smap_node_t *) *
						node_info_ptr->record_count);
		for (i = 0; i < node_info_ptr->record_count; i++) {
			node_info_t *node_ptr = &node_info_ptr->node_array[i];

			if ((node_ptr->name == NULL) ||
			    (node_ptr->name[0] == '\0'))
				continue;

			smap_node = xmalloc(sizeof(smap_node_t));

			len = strlen(node_ptr->name);
			if (params.cluster_dims == 1) {
				smap_node->coord = xmalloc(sizeof(uint16_t));
				j = len - 1;
				while ((node_ptr->name[j] >= '0') &&
				       (node_ptr->name[j] <= '9')) {
					smap_node->coord[0] *= 10;
					smap_node->coord[0] +=
						node_ptr->name[j] - '0';
					j++;
				}
			} else if (params.cluster_flags & CLUSTER_FLAG_CRAY) {
				int len_a, len_h;
				len_a = strlen(node_ptr->node_addr);
				len_h = strlen(node_ptr->node_hostname);
				if (len_a < params.cluster_dims) {
					printf("Invalid node addr %s\n",
					       node_ptr->node_addr);
					xfree(smap_node);
					continue;
				}
				if (len_h < 1) {
					printf("Invalid node hostname %s\n",
					       node_ptr->node_hostname);
					xfree(smap_node);
					continue;
				}
				smap_node->coord = xmalloc(sizeof(uint16_t) *
							   params.cluster_dims);
				len_a -= params.cluster_dims;
				for (j = 0; j < params.cluster_dims; j++) {
					smap_node->coord[j] = select_char2coord(
						node_ptr->node_addr[len_a+j]);
				}
			} else {
				len -= params.cluster_dims;
				if (len < 0) {
					printf("Invalid node name: %s.\n",
					       node_ptr->name);
					xfree(smap_node);
					continue;
				}
				smap_node->coord = xmalloc(sizeof(uint16_t) *
							   params.cluster_dims);
				for (j = 0; j < params.cluster_dims; j++) {
					smap_node->coord[j] = select_char2coord(
						node_ptr->name[len+j]);
				}
			}
			smap_node->index = i;
			smap_node->state = node_ptr->node_state;
			smap_system_ptr->grid[i] = smap_node;
			smap_system_ptr->node_cnt++;
		}
	}

	if (params.cluster_dims == 3) {
		default_y_offset = (dim_size[2] * dim_size[1]) +
				   (dim_size[1] - dim_size[2]);
	}
	if (cols == 0)
		cols = 80;
	for (i = 0; i < smap_system_ptr->node_cnt; i++) {
		smap_node = smap_system_ptr->grid[i];
		if (!smap_node)		/* Null node name */
			continue;
		if (params.cluster_dims == 1) {
			smap_node->grid_xcord = (i % cols) + 1;
			smap_node->grid_ycord = (i / cols) + 1;
		} else if (params.cluster_dims == 2) {
			smap_node->grid_xcord = smap_node->coord[0] + 1;
			smap_node->grid_ycord =
				dim_size[1] - smap_node->coord[1];
		} else if (params.cluster_dims == 3) {
			_calc_coord_3d(smap_node->coord[0], smap_node->coord[1],
				       smap_node->coord[2],
				       default_y_offset,
				       &smap_node->grid_xcord,
				       &smap_node->grid_ycord, dim_size);
		}
	}
}

extern void update_grid(node_info_msg_t *node_info_ptr)
{
	int i;

	if (!node_info_ptr)
		return;

	for (i = 0; i < node_info_ptr->record_count; i++) {
		node_info_t *node_ptr = &node_info_ptr->node_array[i];
		smap_node_t *smap_node;

		if (!node_info_ptr->node_array[i].name
		    || (node_info_ptr->node_array[i].name[0] == '\0'))
			continue;
		smap_node = smap_system_ptr->grid[i];
		if (smap_node)
			smap_node->state = node_ptr->node_state;
	}
}

extern void clear_grid(void)
{
	smap_node_t *smap_node;
	int i;

	if (!smap_system_ptr || !smap_system_ptr->grid)
		return;

	for (i = 0; i < smap_system_ptr->node_cnt; i++) {
		smap_node = smap_system_ptr->grid[i];
		if (!smap_node)		/* Null node name */
			continue;
		if ((smap_node->state == NODE_STATE_DOWN)
		    || (smap_node->state & NODE_STATE_DRAIN)) {
			smap_node->color = COLOR_BLACK;
			smap_node->letter = '#';
		} else {
			smap_node->color = COLOR_WHITE;
			smap_node->letter = '.';
		}
	}
}

extern void free_grid(void)
{
	int i;

	if (!smap_system_ptr)
		return;

	if (smap_system_ptr->grid) {
		for (i = 0; i < smap_system_ptr->node_cnt; i++) {
			smap_node_t *smap_node = smap_system_ptr->grid[i];
			if (!smap_node)		/* Null node name */
				continue;
			xfree(smap_node->coord);
			xfree(smap_node);
		}
		xfree(smap_system_ptr->grid);
	}
	xfree(smap_system_ptr);
}


/* print_grid - print values of every grid point */
extern void print_grid(void)
{
	int i;

	if (!smap_system_ptr || !smap_system_ptr->grid)
		return;

	for (i = 0; i < smap_system_ptr->node_cnt; i++) {
		if (!smap_system_ptr->grid[i])		/* Null node name */
			continue;
		if (smap_system_ptr->grid[i]->color)
			init_pair(smap_system_ptr->grid[i]->color,
				  smap_system_ptr->grid[i]->color, COLOR_BLACK);
		else
			init_pair(smap_system_ptr->grid[i]->color,
				  smap_system_ptr->grid[i]->color, 7);
		wattron(grid_win, COLOR_PAIR(smap_system_ptr->grid[i]->color));
		mvwprintw(grid_win,
			  smap_system_ptr->grid[i]->grid_ycord,
			  smap_system_ptr->grid[i]->grid_xcord, "%c",
			  smap_system_ptr->grid[i]->letter);
		wattroff(grid_win, COLOR_PAIR(smap_system_ptr->grid[i]->color));
	}
	return;
}

bitstr_t *get_requested_node_bitmap(void)
{
	static bitstr_t *bitmap = NULL;
	static node_info_msg_t *old_node_ptr = NULL, *new_node_ptr;
	int error_code;
	int i = 0;
	node_info_t *node_ptr = NULL;

	if (!params.hl)
		return NULL;

	if (old_node_ptr) {
		error_code = slurm_load_node(old_node_ptr->last_update,
					     &new_node_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg(old_node_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA)
			return bitmap;
	} else {
		error_code = slurm_load_node((time_t) NULL, &new_node_ptr,
					     SHOW_ALL);
	}

	if (bitmap)
		FREE_NULL_BITMAP(bitmap);

	if (error_code) {
		slurm_perror("slurm_load_node");
		return NULL;
	}

	old_node_ptr = new_node_ptr;

	bitmap = bit_alloc(old_node_ptr->record_count);
	for (i = 0; i < old_node_ptr->record_count; i++) {
		node_ptr = &(old_node_ptr->node_array[i]);
		if (hostlist_find(params.hl, node_ptr->name) != -1)
			bit_set(bitmap, i);
	}
	return bitmap;
}
