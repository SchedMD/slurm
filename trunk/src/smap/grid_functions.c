/*****************************************************************************\
 *  grid_functions.c - Functions related to curses display of smap.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/smap/smap.h"

/* _init_grid - set values of every grid point */
void init_grid(node_info_msg_t * node_info_ptr)
{
	node_info_t *node_ptr;
	int x, y, z, i = 0;
	int c[PA_SYSTEM_DIMENSIONS];
	uint16_t node_base_state;

	for (x = 0; x < smap_info_ptr->X; x++)
		for (y = 0; y < smap_info_ptr->Y; y++)
			for (z = 0; z < smap_info_ptr->Z; z++) {
				node_ptr = &node_info_ptr->node_array[i];
				node_base_state = (node_ptr->node_state) & (~NODE_STATE_NO_RESPOND);
				smap_info_ptr->grid[x][y][z].color = 7;
				if ((node_base_state == NODE_STATE_DOWN) ||  (node_base_state == NODE_STATE_DRAINED) || (node_base_state == NODE_STATE_DRAINING)) {
					smap_info_ptr->grid[x][y][z].color = 0;
					smap_info_ptr->grid[x][y][z].letter = '#';
					if(_initialized) {
						c[0] = x;
						c[1] = y;
						c[2] = z;
						set_node_down(c);
					}
				} else {
					smap_info_ptr->grid[x][y][z].color = 7;
					smap_info_ptr->grid[x][y][z].letter = '.';
				}
				smap_info_ptr->grid[x][y][z].state = node_ptr->node_state;
				smap_info_ptr->grid[x][y][z].indecies = i++;
			}
	y = 65;
	z = 0;
	for (x = 0; x < smap_info_ptr->num_of_proc; x++) {
		y = y % 128;
		if (y == 0)
			y = 65;
		smap_info_ptr->fill_in_value[x].letter = y;
		z = z % 7;
		if (z == 0)
			z = 1;
		smap_info_ptr->fill_in_value[x].color = z;
		z++;
		y++;
	}
	return;
}

int set_grid(int start, int end, int count)
{
	int x, y, z;
	for (y = smap_info_ptr->Y - 1; y >= 0; y--)
		for (z = 0; z < smap_info_ptr->Z; z++)
			for (x = 0; x < smap_info_ptr->X; x++) {
				if (smap_info_ptr->grid[x][y][z].indecies >= start && smap_info_ptr->grid[x][y][z].indecies <= end) {
					if (smap_info_ptr->grid[x][y][z].state != NODE_STATE_DOWN || smap_info_ptr->grid[x][y][z].state != NODE_STATE_DRAINED || smap_info_ptr->grid[x][y][z].state != NODE_STATE_DRAINING) {
						smap_info_ptr->grid[x][y][z].letter = smap_info_ptr->fill_in_value[count].letter;
						smap_info_ptr->grid[x][y][z].color = smap_info_ptr->fill_in_value[count].color;
					}
				}
			}

	return 1;
}

int set_grid_bgl(int startx, int starty, int startz, int endx, int endy,
		 int endz, int count)
{
	int x, y, z;
	int i = 0;
	assert(endx < smap_info_ptr->X);
	assert(startx >= 0);
	assert(endy < smap_info_ptr->Y);
	assert(starty >= 0);
	assert(endz < smap_info_ptr->Z);
	assert(startz >= 0);
	assert(count < smap_info_ptr->num_of_proc);
	assert(count >= 0);
	for (x = startx; x <= endx; x++)
		for (y = starty; y <= endy; y++)
			for (z = startz; z <= endz; z++) {
				smap_info_ptr->grid[x][y][z].letter = smap_info_ptr->fill_in_value[count].letter;
				smap_info_ptr->grid[x][y][z].color = smap_info_ptr->fill_in_value[count].color;
				i++;
			}

	return i;
}

/* _print_grid - print values of every grid point */
void print_grid(void)
{
	int x, y, z, i = 0, offset = smap_info_ptr->Z;
	int grid_xcord, grid_ycord = 2;
	for (y = smap_info_ptr->Y - 1; y >= 0; y--) {
		offset = smap_info_ptr->Z + 1;
		for (z = 0; z < smap_info_ptr->Z; z++) {
			grid_xcord = offset;

			for (x = 0; x < smap_info_ptr->X; x++) {
				if (smap_info_ptr->grid[x][y][z].color)
					init_pair(smap_info_ptr->grid[x][y][z].color,
						  smap_info_ptr->grid[x][y][z].color,
						  COLOR_BLACK);
				else
					init_pair(smap_info_ptr->grid[x][y][z].color,
						  smap_info_ptr->grid[x][y][z].color, 
                                                  7);

				wattron(smap_info_ptr->grid_win,
					COLOR_PAIR(smap_info_ptr->grid[x][y][z].color));

				mvwprintw(smap_info_ptr->grid_win,
					  grid_ycord, grid_xcord, "%c",
					  smap_info_ptr->grid[x][y][z].letter);
				wattroff(smap_info_ptr->grid_win,
					 COLOR_PAIR(smap_info_ptr->grid[x][y][z].color));
				grid_xcord++;
				i++;
			}
			grid_ycord++;
			offset--;
		}
		grid_ycord++;
	}
	return;
}
