/*****************************************************************************\
 *  configure_functions.c - Functions related to configure mode of smap.
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

typedef struct {
	int type;
	char str[80];
} command_info_t;

void print_header_command(void);
int print_text_command(void);

void get_command(void)
{
	command_info_t *com = xmalloc(sizeof(command_info_t));
	static node_info_msg_t *node_info_ptr;
	node_info_t *node_ptr;
	int text_height, text_width, text_starty, text_startx, error_code;
	WINDOW *command_win;

	text_height = text_win->_maxy;	// - text_win->_begy;
	text_width = text_win->_maxx;	// - text_win->_begx;
	text_starty = text_win->_begy;
	text_startx = text_win->_begx;
	command_win =
	    newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();
	error_code = slurm_load_node((time_t) NULL, &node_info_ptr, 0);
	if (error_code)
		if (quiet_flag != 1) {
			wclear(text_win);
			ycord = text_win->_maxy / 2;
			mvwprintw(text_win, ycord, 1, "slurm_load_node");
			return;
		}
	init_grid(node_info_ptr);

	if (!params.no_header)
		print_header_command();
	while (strcmp(com->str, "quit")) {
		print_grid();
		box(text_win, 0, 0);
		box(grid_win, 0, 0);
		wrefresh(text_win);
		wrefresh(grid_win);
		wclear(command_win);
		box(command_win, 0, 0);
		mvwprintw(command_win, 0, 3,
			  "Input Command: (type quit to change view, exit to exit)");
		wmove(command_win, 1, 1);
		wgetstr(command_win, com->str);

		if (!strcmp(com->str, "exit")) {
			endwin();
			exit(0);
		} else if (!strncmp(com->str, "resume", 6)) {
			mvwprintw(text_win, ycord, xcord, "%s", com->str);
		} else if (!strncmp(com->str, "drain", 5)) {
			mvwprintw(text_win, ycord, xcord, "%s", com->str);
		} else if (!strncmp(com->str, "create", 6)) {
			mvwprintw(text_win, ycord, xcord, "%s", com->str);
		} else if (!strncmp(com->str, "save", 4)) {
			mvwprintw(text_win, ycord, xcord, "%s", com->str);
		}
		ycord++;
		//wattron(text_win, COLOR_PAIR(fill_in_value[count].color));
		//print_text_command(&com);
		//wattroff(text_win, COLOR_PAIR(fill_in_value[count].color));
		//count++;

	}
	//slurm_free_node_info_msg(node_info_ptr);
	params.display = 0;
	noecho();
	init_grid(node_info_ptr);
	wclear(text_win);
	xcord = 1;
	ycord = 1;
	print_date();
	get_job();
	return;
}

void print_header_command(void)
{
	mvwprintw(text_win, ycord, xcord, "ID");
	xcord += 5;
	mvwprintw(text_win, ycord, xcord, "NODE");
	xcord += 8;
	mvwprintw(text_win, ycord, xcord, "STATE");
	xcord += 10;
	mvwprintw(text_win, ycord, xcord, "REASON");
	xcord = 1;
	ycord++;

}

int print_text_command()
{
	/*    time_t time;
	   int printed = 0;
	   int tempxcord;
	   int prefixlen;
	   int i = 0;
	   int width = 0;
	   struct passwd *user = NULL;
	   long days, hours, minutes, seconds;

	   mvwprintw(text_win, ycord, xcord, "%c", job_ptr->num_procs);
	   xcord += 8;
	   mvwprintw(text_win, ycord, xcord, "%d", job_ptr->job_id);
	   xcord += 8;
	   mvwprintw(text_win, ycord, xcord, "%s", job_ptr->partition);
	   xcord += 12;
	   user = getpwuid((uid_t) job_ptr->user_id);
	   mvwprintw(text_win, ycord, xcord, "%s", user->pw_name);
	   xcord += 10;
	   mvwprintw(text_win, ycord, xcord, "%s", job_ptr->name);
	   xcord += 12;
	   mvwprintw(text_win, ycord, xcord, "%s",
	   job_state_string(job_ptr->job_state));
	   xcord += 10;
	   time = now - job_ptr->start_time;

	   seconds = time % 60;
	   minutes = (time / 60) % 60;
	   hours = (time / 3600) % 24;
	   days = time / 86400;

	   if (days)
	   mvwprintw(text_win, ycord, xcord,
	   "%ld:%2.2ld:%2.2ld:%2.2ld", days, hours, minutes,
	   seconds);
	   else if (hours)
	   mvwprintw(text_win, ycord, xcord, "%ld:%2.2ld:%2.2ld",
	   hours, minutes, seconds);
	   else
	   mvwprintw(text_win, ycord, xcord, "%ld:%2.2ld", minutes,
	   seconds);

	   xcord += 12;
	   mvwprintw(text_win, ycord, xcord, "%d", job_ptr->num_nodes);
	   xcord += 8;
	   tempxcord = xcord;
	   width = text_win->_maxx - xcord;
	   while (job_ptr->nodes[i] != '\0') {
	   if ((printed =
	   mvwaddch(text_win, ycord, xcord,
	   job_ptr->nodes[i])) < 0)
	   return printed;
	   xcord++;
	   width = text_win->_maxx - xcord;
	   if (job_ptr->nodes[i] == '[')
	   prefixlen = i + 1;
	   else if (job_ptr->nodes[i] == ',' && (width - 9) <= 0) {
	   ycord++;
	   xcord = tempxcord + prefixlen;
	   }
	   i++;
	   }

	   xcord = 1;
	   ycord++;
	   return printed; */
}
