/*****************************************************************************\
 *  partition_functions.c - Functions related to partition display 
 *  mode of smap.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
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
			wclear(pa_system_ptr->text_win);
			pa_system_ptr->ycord =
			    pa_system_ptr->text_win->_maxy / 2;
			pa_system_ptr->xcord =
			    pa_system_ptr->text_win->_maxx;
			mvwprintw(pa_system_ptr->text_win,
				  pa_system_ptr->ycord, 1,
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
					    (int) pa_system_ptr->
					    fill_in_value[count].letter;
					wattron(pa_system_ptr->text_win,
						COLOR_PAIR(pa_system_ptr->
							   fill_in_value
							   [count].color));
					print_text_part(&part);
					wattroff(pa_system_ptr->text_win,
						 COLOR_PAIR(pa_system_ptr->
							    fill_in_value
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
				    (int) pa_system_ptr->
				    fill_in_value[count].letter;
				wattron(pa_system_ptr->text_win,
					COLOR_PAIR(pa_system_ptr->
						   fill_in_value[count].
						   color));
				print_text_part(&part);
				wattroff(pa_system_ptr->text_win,
					 COLOR_PAIR(pa_system_ptr->
						    fill_in_value[count].
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
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "ID");
	pa_system_ptr->xcord += 4;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "PARTITION");
	pa_system_ptr->xcord += 10;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "AVAIL");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "TIMELIMIT");
	pa_system_ptr->xcord += 11;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODES");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODELIST");
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;
}

int print_text_part(partition_info_t * part_ptr)
{
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	char *nodes, time_buf[20];

	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%c", part_ptr->root_only);
	pa_system_ptr->xcord += 4;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%.9s", part_ptr->name);
	pa_system_ptr->xcord += 10;
	if (part_ptr->state_up)
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "UP");
	else
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "DOWN");
	pa_system_ptr->xcord += 7;

	if (part_ptr->max_time == INFINITE)
		snprintf(time_buf, sizeof(time_buf), "UNLIMITED");
	else {
		snprint_time(time_buf, sizeof(time_buf), 
			(part_ptr->max_time * 60));
	}
	width = strlen(time_buf);
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		pa_system_ptr->xcord + (9 - width), "%s", 
		time_buf);
	pa_system_ptr->xcord += 11;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%5d", part_ptr->total_nodes);
	pa_system_ptr->xcord += 7;

	tempxcord = pa_system_ptr->xcord;
	//width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;
	if (params.display == BGLPART)
		nodes = part_ptr->allow_groups;
	else
		nodes = part_ptr->nodes;
	prefixlen = i;
	while (nodes[i] != '\0') {
		width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;

		if (!prefixlen && nodes[i] == '[' && nodes[i - 1] == ',')
			prefixlen = i + 1;

		if (nodes[i - 1] == ',' && (width - 12) <= 0) {
			pa_system_ptr->ycord++;
			pa_system_ptr->xcord = tempxcord + prefixlen;
		} else if (pa_system_ptr->xcord >
			   pa_system_ptr->text_win->_maxx) {
			pa_system_ptr->ycord++;
			pa_system_ptr->xcord = tempxcord + prefixlen;
		}


		if ((printed =
		     mvwaddch(pa_system_ptr->text_win,
			      pa_system_ptr->ycord, pa_system_ptr->xcord,
			      nodes[i])) < 0)
			return printed;
		pa_system_ptr->xcord++;

		i++;
	}

	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;

	return printed;
}
