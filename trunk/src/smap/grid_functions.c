/*****************************************************************************\
 *  grid_functions.c - Functions related to curses display of smap.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_3D
static int _coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return (coord - 'A');
	return -1;
}

#endif

/* Set grid color based upon node names containing X-, Y- and Z-
 * coordinates in last three positions. It is not based upon the
 * nodes in the node table being numerically ordered. */
extern int set_grid_name(char *nodes, int count)
{
#ifdef HAVE_3D
	hostlist_t hl;
	char *node;
	int i, x = 0, y = 0, z = 0;

	if (!nodes)
		return 1;

	hl = hostlist_create(nodes);
	while ((node = hostlist_shift(hl))) {
		i = strlen(node);
		if (i < 4)
			x = -1;
		else {
			x = _coord(node[i-3]);
			y = _coord(node[i-2]);
			z = _coord(node[i-1]);
		}
		if ((ba_system_ptr->grid[x][y][z].state 
				!= NODE_STATE_DOWN) &&
		    (!(ba_system_ptr->grid[x][y][z].state 
				& NODE_STATE_DRAIN)) &&
		    (x >= 0) && (x < DIM_SIZE[X]) && 
		    (y >= 0) && (y < DIM_SIZE[Y]) && 
		    (z >= 0) && (z < DIM_SIZE[Z])) {
			ba_system_ptr->grid[x][y][z].letter = 
				letters[count%62];
			ba_system_ptr->grid[x][y][z].color = 
				colors[count%6];
		}
		free(node);
	}
	hostlist_destroy(hl);
#endif
	return 1;
}

extern int set_grid_inx(int start, int end, int count)
{
	int x;
#ifdef HAVE_3D
	int y, z;
	for (y = DIM_SIZE[Y] - 1; y >= 0; y--) {
		for (z = 0; z < DIM_SIZE[Z]; z++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				if ((ba_system_ptr->grid[x][y][z].index 
				     < start)
				||  (ba_system_ptr->grid[x][y][z].index 
				     > end)) 
					continue;
				if ((ba_system_ptr->grid[x][y][z].state 
				     == NODE_STATE_DOWN)
				    ||  (ba_system_ptr->grid[x][y][z].state 
					 & NODE_STATE_DRAIN))
					continue;

				ba_system_ptr->grid[x][y][z].letter = 
					letters[count%62];
				ba_system_ptr->grid[x][y][z].color = 
					colors[count%6];
			}
		}
	}
#else
	for (x = 0; x < DIM_SIZE[X]; x++) {
		if ((ba_system_ptr->grid[x].index < start)
		    ||  (ba_system_ptr->grid[x].index > end)) 
			continue;
		if ((ba_system_ptr->grid[x].state == NODE_STATE_DOWN)
		    ||  (ba_system_ptr->grid[x].state & NODE_STATE_DRAIN))
			continue;

		ba_system_ptr->grid[x].letter = letters[count%62];
		ba_system_ptr->grid[x].color = colors[count%6];
	}
#endif
	return 1;
}

/* This function is only called when HAVE_BG is set */
extern int set_grid_bg(int *start, int *end, int count, int set)
{
	int x=0;
	int i = 0;
#ifdef HAVE_3D
	int y=0, z=0;
#endif
	assert(end[X] < DIM_SIZE[X]);
	assert(start[X] >= 0);
	assert(count >= 0);
	assert(set >= 0);
	assert(set <= 2);
#ifdef HAVE_3D
	assert(end[Y] < DIM_SIZE[Y]);
	assert(start[Y] >= 0);
	assert(end[Z] < DIM_SIZE[Z]);
	assert(start[Z] >= 0);
	
	for (x = start[X]; x <= end[X]; x++) {
		for (y = start[Y]; y <= end[Y]; y++) {
			for (z = start[Z]; z <= end[Z]; z++) {
				/* set the color and letter of the
				   block if the set flag is specified
				   or if the letter hasn't been set yet
				*/
				if(set 
				   || ((ba_system_ptr->grid[x][y][z].letter
					== '.')
				       && (ba_system_ptr->grid[x][y][z].letter 
					   != '#'))) {
					
						ba_system_ptr->
							grid[x][y][z].letter = 
							letters[count%62];
						ba_system_ptr->
							grid[x][y][z].color = 
							colors[count%6];
				} 
				i++;
			}
		}
	}
#else
	for (x = start[X]; x <= end[X]; x++) {
		if(!set) {
			ba_system_ptr->grid[x].letter = 
				letters[count%62];
			ba_system_ptr->grid[x].color = 
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

#ifdef HAVE_3D
	int y, z, offset = DIM_SIZE[Z];
	for (y = DIM_SIZE[Y] - 1; y >= 0; y--) {
		offset = DIM_SIZE[Z] + 1;
		for (z = 0; z < DIM_SIZE[Z]; z++) {
			grid_xcord = offset;

			for (x = 0; x < DIM_SIZE[X]; x++) {
				if (ba_system_ptr->grid[x][y][z].color)
					init_pair(ba_system_ptr->
						  grid[x][y][z].color,
						  ba_system_ptr->
						  grid[x][y][z].color,
						  COLOR_BLACK);
				else
					init_pair(ba_system_ptr->
						  grid[x][y][z].color,
						  ba_system_ptr->
						  grid[x][y][z].color, 
                                                  7);

				wattron(grid_win,
					COLOR_PAIR(ba_system_ptr->
						   grid[x][y][z].color));

				mvwprintw(grid_win,
					  grid_ycord, grid_xcord, "%c",
					  ba_system_ptr->grid[x][y][z].letter);
				wattroff(grid_win,
					 COLOR_PAIR(ba_system_ptr->
						    grid[x][y][z].color));
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
		if (ba_system_ptr->grid[x].color)
			init_pair(ba_system_ptr->grid[x].color,
				  ba_system_ptr->grid[x].color,
				  COLOR_BLACK);
		else
			init_pair(ba_system_ptr->grid[x].color,
				  ba_system_ptr->grid[x].color, 
				  7);
		
		wattron(grid_win,
			COLOR_PAIR(ba_system_ptr->grid[x].color));
		
		mvwprintw(grid_win,
			  grid_ycord, grid_xcord, "%c",
			  ba_system_ptr->grid[x].letter);
		wattroff(grid_win,
			 COLOR_PAIR(ba_system_ptr->grid[x].color));
		
		grid_xcord++;
		if(grid_xcord==grid_win->_maxx) {
			grid_xcord=1;
			grid_ycord++;
		}
		if(grid_ycord==grid_win->_maxy) {
			break;
		}
	}
#endif
	return;
}


