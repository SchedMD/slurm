/*****************************************************************************\
 *  partition_functions.c - Functions related to partition display 
 *  mode of smap.
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

void print_header_part(void);
int print_text_part(partition_info_t * part_ptr);

void get_part(void)
{
	int error_code, i, j, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL, *new_part_ptr;
	partition_info_t part;
	char node_entry[13];
	int start, startx, starty, startz, endx, endy, endz;
	if (part_info_ptr) {
		error_code =
		    slurm_load_partitions(part_info_ptr->last_update,
					  &new_part_ptr, 0);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else
		error_code =
		    slurm_load_partitions((time_t) NULL, &new_part_ptr, 0);
	if (error_code) {
		if (quiet_flag != 1) {
			clear_window(text_win);
			ycord = text_win->_maxy / 2;
			xcord = text_win->_maxx;
			mvwprintw(text_win, ycord, 1,
				  "slurm_load_partitions error");

		}
		return;
	}

	if (new_part_ptr->record_count && !params.no_header)
		print_header_part();
	for (i = 0; i < new_part_ptr->record_count; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];

		if (params.display == BGLPART) {
			memset(node_entry, 0, 13);
			memcpy(node_entry, part.nodes, 12);
			part.allow_groups = node_entry;
			while (part.nodes[j] != '\0') {
				if (part.nodes[j] == '[') {
					j++;
					start = atoi(part.nodes + j);
					startx = start / 100;
					starty = (start % 100) / 10;
					startz = (start % 10);
					j += 4;
					start = atoi(part.nodes + j);
					endx = start / 100;
					endy = (start % 100) / 10;
					endz = (start % 10);
					j += 5;

					part.total_nodes =
					    set_grid_bgl(startx, starty,
							 startz, endx,
							 endy, endz,
							 count);
					part.root_only =
					    (int) fill_in_value[count].
					    letter;
					wattron(text_win,
						COLOR_PAIR(fill_in_value
							   [count].color));
					print_text_part(&part);
					wattroff(text_win,
						 COLOR_PAIR(fill_in_value
							    [count].
							    color));
					count++;
					memset(node_entry, 0, 13);
					memcpy(node_entry, part.nodes + j,
					       12);
					part.allow_groups = node_entry;

				}
				j++;
			}
		} else {
			while (part.node_inx[j] >= 0) {

				set_grid(part.node_inx[j],
					 part.node_inx[j + 1], count);
				j += 2;

				part.root_only =
				    (int) fill_in_value[count].letter;
				wattron(text_win,
					COLOR_PAIR(fill_in_value[count].
						   color));
				print_text_part(&part);
				wattroff(text_win,
					 COLOR_PAIR(fill_in_value[count].
						    color));
				count++;
			}
		}
	}

	part_info_ptr = new_part_ptr;
	return;
}

void print_header_part(void)
{
	mvwprintw(text_win, ycord, xcord, "ID");
	xcord += 4;
	mvwprintw(text_win, ycord, xcord, "PARTITION");
	xcord += 10;
	mvwprintw(text_win, ycord, xcord, "AVAIL");
	xcord += 7;
	mvwprintw(text_win, ycord, xcord, "TIMELIMIT");
	xcord += 11;
	mvwprintw(text_win, ycord, xcord, "NODES");
	xcord += 7;
	mvwprintw(text_win, ycord, xcord, "NODELIST");
	xcord = 1;
	ycord++;
}

int print_text_part(partition_info_t * part_ptr)
{
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	char *nodes;

	mvwprintw(text_win, ycord, xcord, "%c", part_ptr->root_only);
	xcord += 4;
	mvwprintw(text_win, ycord, xcord, "%s", part_ptr->name);
	xcord += 10;
	if (part_ptr->state_up)
		mvwprintw(text_win, ycord, xcord, "UP");
	else
		mvwprintw(text_win, ycord, xcord, "DOWN");
	xcord += 7;
	if (part_ptr->max_time == INFINITE)
		mvwprintw(text_win, ycord, xcord, "UNLIMITED");
	else
		mvwprintw(text_win, ycord, xcord, "%u",
			  part_ptr->max_time);

	xcord += 11;
	mvwprintw(text_win, ycord, xcord, "%d", part_ptr->total_nodes);
	xcord += 7;

	tempxcord = xcord;
	width = text_win->_maxx - xcord;
	if (params.display == BGLPART)
		nodes = part_ptr->allow_groups;
	else
		nodes = part_ptr->nodes;
	prefixlen = i;
	while (nodes[i] != '\0') {
		if ((printed =
		     mvwaddch(text_win, ycord, xcord, nodes[i])) < 0)
			return printed;
		xcord++;
		width = text_win->_maxx - xcord;
		if (nodes[i] == '[' && nodes[i - 1] == ',')
			prefixlen = i + 1;
		else if (nodes[i] == ',' && (width - 9) <= 0) {
			ycord++;
			xcord = tempxcord + prefixlen;
		}
		i++;
	}

	xcord = 1;
	ycord++;

	return printed;
}
