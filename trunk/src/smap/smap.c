/*****************************************************************************\
 *  smap.c - Report overall state the system
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

#include <signal.h>
#include "src/common/xstring.h"
#include "src/smap/smap.h"

/********************
 * Global Variables *
 ********************/
struct smap_parameters params;
int quiet_flag = 0;
int xcord = 1;
int ycord = 1;
int X = 0;
int Y = 0;
int Z = 0;
int num_of_proc = 0;
int resize_screen = 0;
/************
 * Functions *
 ************/
int _get_option(void);
void *_resize_handler(int sig);

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	node_info_msg_t *node_info_ptr;
	node_info_t *node_ptr;
	int error_code;
	int height = 40;
	int width = 100;
	int startx = 0;
	int starty = 0;
	int end = 0;
	int i, j, start, temp;
	//char *name;

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);

#ifdef HAVE_BGL
	error_code = slurm_load_node((time_t) NULL, &node_info_ptr, 0);
	if (error_code) {
		slurm_perror("slurm_load_node");
		exit(0);
	} else {
		for (i = 0; i < node_info_ptr->record_count; i++) {
			node_ptr = &node_info_ptr->node_array[i];
			node_ptr->node_state = NODE_STATE_DRAINED;
			start = atoi(node_ptr->name + 3);
			temp = start / 100;
			if (X < temp)
				X = temp;
			temp = (start % 100) / 10;
			if (Y < temp)
				Y = temp;
			temp = start % 10;
			if (Z < temp)
				Z = temp;
		}
		X++;
		Y++;
		Z++;
		grid = (axis ***) xmalloc(sizeof(axis **) * X);
		for (i = 0; i < X; i++) {
			grid[i] = (axis **) xmalloc(sizeof(axis *) * Y);
			for (j = 0; j < Y; j++)
				grid[i][j] =
				    (axis *) xmalloc(sizeof(axis) * Z);
		}
		num_of_proc = node_info_ptr->record_count;

		fill_in_value =
		    (axis *) xmalloc(sizeof(axis) * num_of_proc);

		height = Y * Z + Y * 2;
		width = X * 2;
		init_grid(node_info_ptr);


	}
	signal(SIGWINCH, _resize_handler);
#else
	printf("This will only run on a BGL system right now.\n");
	exit(0);
#endif
	initscr();
	if (COLS < (75 + width) || LINES < height) {
		endwin();
		printf
		    ("Screen is too small make sure the screen is at least %dx%d\n",
		     84 + width, height);
		exit(0);
	}
	raw();
	keypad(stdscr, TRUE);
	noecho();
	cbreak();
	curs_set(1);
	nodelay(stdscr, TRUE);
	start_color();

	grid_win = newwin(height, width, starty, startx);
	box(grid_win, 0, 0);

	startx = width;
	COLS -= 2;
	width = COLS - width;
	height = LINES;
	text_win = newwin(height, width, starty, startx);
	box(text_win, 0, 0);
	wrefresh(text_win);
	wrefresh(grid_win);

	while (!end) {
		_get_option();
	      redraw:


		init_grid(node_info_ptr);
		wclear(text_win);
		//wclear(grid_win);        
		xcord = 1;
		ycord = 1;

		print_date();
		switch (params.display) {
		case JOBS:
			get_job();
			break;
		case COMMANDS:
			get_command();
			break;
		default:
			get_part();
			break;
		}

		print_grid();
		box(text_win, 0, 0);
		box(grid_win, 0, 0);
		wrefresh(text_win);
		wrefresh(grid_win);

		if (params.iterate) {
			for (i = 0; i < params.iterate; i++) {

				sleep(1);
				if (_get_option())
					goto redraw;
				else if (resize_screen) {
					resize_screen = 0;
					goto redraw;
				}
			}
		} else
			break;
	}

	nodelay(stdscr, FALSE);
	getch();
	endwin();
	for (i = 0; i < X; i++) {
		for (j = 0; j < Y; j++)
			xfree(grid[i][j]);
		xfree(grid[i]);
	}
	xfree(grid);
	xfree(fill_in_value);

	exit(0);
}

void print_date(void)
{
	now = time(NULL);
	mvwprintw(text_win, ycord, xcord, "%s", ctime(&now));
	ycord++;
}

int _get_option(void)
{
	char ch;

	ch = getch();
	switch (ch) {
	case 'b':
		params.display = BGLPART;
		return 1;
		break;
	case 's':
		params.display = SLURMPART;
		return 1;
		break;
	case 'j':
		params.display = JOBS;
		return 1;
		break;
	case 'c':
		params.display = COMMANDS;
		return 1;
		break;
	case 'q':
	case '\n':
		endwin();
		exit(0);
		break;
	}
	return 0;
}

void *_resize_handler(int sig)
{
	int height = Y * Z + Y * 2;
	int width = X * 2;
	int startx = 0;
	int starty = 0;

	ycord = 1;
	wclear(grid_win);
	wclear(text_win);
	endwin();
	initscr();
	getmaxyx(stdscr, LINES, COLS);
	wresize(grid_win, height, width);
	width = COLS - width;
	wresize(text_win, LINES, width);
	print_date();
	switch (params.display) {
	case JOBS:
		get_job();
		break;
	case COMMANDS:
		get_command();
		break;
	default:
		get_part();
		break;
	}

	print_grid();
	box(text_win, 0, 0);
	box(grid_win, 0, 0);
	wrefresh(text_win);
	wrefresh(grid_win);
	resize_screen = 1;
	return NULL;
}
