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
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/smap/smap.h"

extern void set_grid_inx(int start, int end, int count)
{
	int i;

	for (i = 0; i < ba_system_ptr->node_cnt; i++) {
		if ((ba_system_ptr->grid[i]->index < start) ||
		    (ba_system_ptr->grid[i]->index > end))
			continue;
		if ((ba_system_ptr->grid[i]->state == NODE_STATE_DOWN) ||
		    (ba_system_ptr->grid[i]->state & NODE_STATE_DRAIN))
			continue;

		ba_system_ptr->grid[i]->letter = letters[count%62];
		ba_system_ptr->grid[i]->color  = colors[count%6];
	}
}

/* This function is only called when HAVE_BG is set */
extern int set_grid_bg(int *start, int *end, int count, int set)
{
	int node_cnt = 0, i, j;

	for (i = 0; i < ba_system_ptr->node_cnt; i++) {
		for (j = 0; j < params.cluster_dims; j++) {
			if ((ba_system_ptr->grid[i]->coord[j] < start[i]) ||
			    (ba_system_ptr->grid[i]->coord[j] > end[i]))
				break;
		}
		if (j < params.cluster_dims)
			break;	/* outside of boundary */
		if (set ||
		    ((ba_system_ptr->grid[i]->letter == '.') &&
		     (ba_system_ptr->grid[i]->letter != '#'))) {
			ba_system_ptr->grid[i]->letter = letters[count%62];
			ba_system_ptr->grid[i]->color  = colors[count%6];
		}
		node_cnt++;
	}
	return node_cnt;
}

static int _coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return (coord - 'A') + 10;
	return -1;
}

/* Build the ba_system_ptr structure from the node records */
extern void init_grid(node_info_msg_t *node_info_ptr)
{
	int i, j, len;
	ba_node_t *node_ptr;

	ba_system_ptr = xmalloc(sizeof(ba_system_t));
	ba_system_ptr->grid = xmalloc(sizeof(ba_node_t *) *
				      node_info_ptr->record_count);
	for (i = 0; i < node_info_ptr->record_count; i++) {
		if ((node_info_ptr->node_array[i].name == NULL) ||
		    (node_info_ptr->node_array[i].name[0] == '\0'))
			continue;
		node_ptr = xmalloc(sizeof(ba_node_t));
		len = strlen(node_info_ptr->node_array[i].name);
		if (params.cluster_dims == 1) {
			node_ptr->coord = xmalloc(sizeof(uint16_t));
			j = len - 1;
			while ((node_info_ptr->node_array[i].name[j] >= '0') &&
			       (node_info_ptr->node_array[i].name[j] <= '9')) {
				node_ptr->coord[j] *= 10;
				node_ptr->coord[j] +=
					node_info_ptr->node_array[i].name[j]
					- '0';
				j++;
			}
				

		} else {
			len -= params.cluster_dims;
			if (len < 0) {
				printf("Invalid node name: %s.\n",
				       node_info_ptr->node_array[i].name);
				xfree(node_ptr);
				continue;
			}
			node_ptr->coord = xmalloc(sizeof(uint16_t) *
						  params.cluster_dims);
			for (j = 0; j < params.cluster_dims; j++) {
				node_ptr->coord[j] = _coord(node_info_ptr->
							    node_array[i].
							    name[len+j]);
			}
		}
		node_ptr->index = i;
		ba_system_ptr->grid[ba_system_ptr->node_cnt] = node_ptr;
		ba_system_ptr->node_cnt++;
	}

	for (i = 0; i < ba_system_ptr->node_cnt; i++) {
		node_ptr = ba_system_ptr->grid[i];
//FIXME
		node_ptr->grid_xcord = i + 1;
		node_ptr->grid_ycord = 1;
	}
}

extern void free_grid(void)
{
	int i;

	if (ba_system_ptr == NULL)
		return;

	for (i = 0; i < ba_system_ptr->node_cnt; i++) {
		ba_node_t *node_ptr = ba_system_ptr->grid[i];
		xfree(node_ptr->coord);
		xfree(node_ptr);
	}
	xfree(ba_system_ptr->grid);
	xfree(ba_system_ptr);
}


/* print_grid - print values of every grid point */
extern void print_grid(void)
{
	int i;

	for (i = 0; i < ba_system_ptr->node_cnt; i++) {
		if (ba_system_ptr->grid[i]->color)
			init_pair(ba_system_ptr->grid[i]->color,
				  ba_system_ptr->grid[i]->color, COLOR_BLACK);
		else
			init_pair(ba_system_ptr->grid[i]->color,
				  ba_system_ptr->grid[i]->color, 7);
		mvwprintw(grid_win,
			  ba_system_ptr->grid[i]->grid_ycord, 
			  ba_system_ptr->grid[i]->grid_xcord, "%c",
			  ba_system_ptr->grid[i]->letter);
		wattroff(grid_win, COLOR_PAIR(ba_system_ptr->grid[i]->color));
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
