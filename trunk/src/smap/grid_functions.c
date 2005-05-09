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
	int x;
#ifdef HAVE_BGL
	int y, z;
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
					letters[count%62];
				pa_system_ptr->grid[x][y][z].color = 
					colors[count%6];
			}
		}
	}
#else
	for (x = 0; x < DIM_SIZE[X]; x++) {
		if ((pa_system_ptr->grid[x].indecies < start)
		    ||  (pa_system_ptr->grid[x].indecies > end)) 
			continue;
		if ((pa_system_ptr->grid[x].state == NODE_STATE_DOWN)
		    ||  (pa_system_ptr->grid[x].state == NODE_STATE_DRAINED)
		    ||  (pa_system_ptr->grid[x].state == NODE_STATE_DRAINING))
			continue;

		pa_system_ptr->grid[x].letter = 
			letters[count%62];
		pa_system_ptr->grid[x].color = 
			colors[count%6];
	}
#endif
	return 1;
}

extern int set_grid_bgl(int *start, int *end, int count, int set)
{
	int x=0;
	int i = 0;
#ifdef HAVE_BGL
	int y=0, z=0;
#endif

	assert(end[X] < DIM_SIZE[X]);
	assert(start[X] >= 0);
	assert(count >= 0);
	assert(set >= 0);
	assert(set <= 2);
#ifdef HAVE_BGL
	assert(end[Y] < DIM_SIZE[Y]);
	assert(start[Y] >= 0);
	assert(end[Z] < DIM_SIZE[Z]);
	assert(start[Z] >= 0);
	
	for (x = start[X]; x <= end[X]; x++) {
		for (y = start[Y]; y <= end[Y]; y++) {
			for (z = start[Z]; z <= end[Z]; z++) {
				if(!set) {
					pa_system_ptr->grid[x][y][z].letter = 
						letters[count%62];
					pa_system_ptr->grid[x][y][z].color = 
						colors[count%6];
				}
				i++;
			}
		}
	}
#else
	for (x = start[X]; x <= end[X]; x++) {
		if(!set) {
			pa_system_ptr->grid[x].letter = 
				letters[count%62];
			pa_system_ptr->grid[x].color = 
				colors[count%6];
		}
		i++;
	}
	
#endif

	return i;
}

/* print_grid - print values of every grid point */
extern void print_grid(int dir)
{
	int x;
	int grid_xcord, grid_ycord = 2;

#ifdef HAVE_BGL
	int y, z, offset = DIM_SIZE[Z];
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
			}
			grid_ycord++;
			offset--;
		}
		grid_ycord++;
	}
#else
	grid_xcord=1;
	grid_ycord=1;

	for (x = dir; x < DIM_SIZE[X]; x++) {
		if (pa_system_ptr->grid[x].color)
			init_pair(pa_system_ptr->grid[x].color,
				  pa_system_ptr->grid[x].color,
				  COLOR_BLACK);
		else
			init_pair(pa_system_ptr->grid[x].color,
				  pa_system_ptr->grid[x].color, 
				  7);
		
		wattron(pa_system_ptr->grid_win,
			COLOR_PAIR(pa_system_ptr->grid[x].color));
		
		mvwprintw(pa_system_ptr->grid_win,
			  grid_ycord, grid_xcord, "%c",
			  pa_system_ptr->grid[x].letter);
		wattroff(pa_system_ptr->grid_win,
			 COLOR_PAIR(pa_system_ptr->grid[x].color));
		
		grid_xcord++;
		if(grid_xcord==pa_system_ptr->grid_win->_maxx) {
			grid_xcord=1;
			grid_ycord++;
		}
		if(grid_ycord==pa_system_ptr->grid_win->_maxy) {
			break;
		}
	}
#endif
	return;
}


