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

extern int set_grid(int start, int end, int count)
{
	int x, y, z;

	for (y = DIM_SIZE[Y] - 1; y >= 0; y--) {
		for (z = 0; z < DIM_SIZE[Z]; z++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				if ((pa_system_ptr->grid[x][y][z].indecies < start)
				||  (pa_system_ptr->grid[x][y][z].indecies > end)) 
					continue;
				if ((pa_system_ptr->grid[x][y][z].state == NODE_STATE_DOWN)
				||  (pa_system_ptr->grid[x][y][z].state == NODE_STATE_DRAINED)
				||  (pa_system_ptr->grid[x][y][z].state == NODE_STATE_DRAINING))
					continue;

				pa_system_ptr->grid[x][y][z].letter = 
					pa_system_ptr->
					fill_in_value[count].letter;
				pa_system_ptr->grid[x][y][z].color = 
					pa_system_ptr->
					fill_in_value[count].color;
			}
		}
	}

	return 1;
}

extern int set_grid_bgl(int *start, int *end, int count, int set)
{
	int x, y, z;
	int i = 0;
	assert(end[X] < DIM_SIZE[X]);
	assert(start[X] >= 0);
	assert(end[Y] < DIM_SIZE[Y]);
	assert(start[Y] >= 0);
	assert(end[Z] < DIM_SIZE[Z]);
	assert(start[Z] >= 0);
	assert(count < pa_system_ptr->num_of_proc);
	assert(count >= 0);
	assert(set >= 0);
	assert(set <= 2);

	for (x = start[X]; x <= end[X]; x++) {
		for (y = start[Y]; y <= end[Y]; y++) {
			for (z = start[Z]; z <= end[Z]; z++) {
				if(!set) {
					pa_system_ptr->grid[x][y][z].letter = 
						pa_system_ptr->
						fill_in_value[count].letter;
					pa_system_ptr->grid[x][y][z].color = 
						pa_system_ptr->
						fill_in_value[count].color;
				}
				i++;
			}
		}
	}

	return i;
}

/* print_grid - print values of every grid point */
extern void print_grid(void)
{
	int x, y, z, i = 0, offset = DIM_SIZE[Z];
	int grid_xcord, grid_ycord = 2;
	for (y = DIM_SIZE[Y] - 1; y >= 0; y--) {
		offset = DIM_SIZE[Z] + 1;
		for (z = 0; z < DIM_SIZE[Z]; z++) {
			grid_xcord = offset;

			for (x = 0; x < DIM_SIZE[X]; x++) {
				if (pa_system_ptr->grid[x][y][z].color)
					init_pair(pa_system_ptr->grid[x][y][z].color,
						  pa_system_ptr->grid[x][y][z].color,
						  COLOR_BLACK);
				else
					init_pair(pa_system_ptr->grid[x][y][z].color,
						  pa_system_ptr->grid[x][y][z].color, 
                                                  7);

				wattron(pa_system_ptr->grid_win,
					COLOR_PAIR(pa_system_ptr->grid[x][y][z].color));

				mvwprintw(pa_system_ptr->grid_win,
					  grid_ycord, grid_xcord, "%c",
					  pa_system_ptr->grid[x][y][z].letter);
				wattroff(pa_system_ptr->grid_win,
					 COLOR_PAIR(pa_system_ptr->grid[x][y][z].color));
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


