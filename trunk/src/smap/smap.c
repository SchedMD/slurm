/*****************************************************************************\
 *  smap.c - Report overall state the system
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

#include <signal.h>
#include "src/smap/smap.h"

/********************
 * Global Variables *
 ********************/
smap_parameters_t params;

int quiet_flag = 0;

/************
 * Functions *
 ************/

int _get_option();
void *_resize_handler(int sig);

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	node_info_msg_t *node_info_ptr;
	int error_code;
	int height = 40;
	int width = 100;
	int startx = 0;
	int starty = 0;
	int end = 0;
	int i;
	//char *name;
	
        	
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);
#ifdef HAVE_BGL
	error_code = slurm_load_node((time_t) NULL, &node_info_ptr, 0);
	if (error_code) {
		slurm_perror("slurm_load_node");
		exit(0);
	} else {
		pa_init(node_info_ptr);
        }
#else
	printf("This will only run on a BGL system right now.\n");
	exit(0);
#endif
	height = DIM_SIZE[Y] * DIM_SIZE[Z] + DIM_SIZE[Y] + 3;
	width = DIM_SIZE[X] + DIM_SIZE[Z] + 3;
	
	signal(SIGWINCH, (sighandler_t) _resize_handler);

	initscr();
	if (COLS < (75 + width) || LINES < height) {
		endwin();
		printf("Screen is too small make sure the screen is at least %dx%d\n"
			"Right now it is %dx%d\n", 75 + width, height, COLS, LINES);
		exit(0);
	}
	raw();
	keypad(stdscr, TRUE);
	noecho();
	cbreak();
	curs_set(1);
	nodelay(stdscr, TRUE);
	start_color();
	
	pa_system_ptr->grid_win = newwin(height, width, starty, startx);
	box(pa_system_ptr->grid_win, 0, 0);

	startx = width;
	COLS -= 2;
	width = COLS - width;
	height = LINES;
	pa_system_ptr->text_win = newwin(height, width, starty, startx);
	box(pa_system_ptr->text_win, 0, 0);
	wrefresh(pa_system_ptr->text_win);
	wrefresh(pa_system_ptr->grid_win);
	
	while (!end) {
		_get_option();
	      redraw:

		init_grid(node_info_ptr);
		wclear(pa_system_ptr->text_win);
		//wclear(pa_system_ptr->grid_win);        
		pa_system_ptr->xcord = 1;
		pa_system_ptr->ycord = 1;

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
		box(pa_system_ptr->text_win, 0, 0);
		box(pa_system_ptr->grid_win, 0, 0);
		wrefresh(pa_system_ptr->text_win);
		wrefresh(pa_system_ptr->grid_win);

		if (params.iterate) {
			for (i = 0; i < params.iterate; i++) {

				sleep(1);
				if (_get_option())
					goto redraw;
				else if (pa_system_ptr->resize_screen) {
					pa_system_ptr->resize_screen = 0;
					goto redraw;
				}
			}
		} else
			break;
	}

	nodelay(stdscr, FALSE);
	getch();
	endwin();
	pa_fini();
        
	exit(0);
}


int _get_option()
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
        int height = DIM_SIZE[Y] * DIM_SIZE[Z] + DIM_SIZE[Y] + 3;
        int width = DIM_SIZE[X] + DIM_SIZE[Z] + 3;
        int tempwidth = width;
        pa_system_ptr->ycord = 1;
	
	delwin(pa_system_ptr->grid_win);
	delwin(pa_system_ptr->text_win);
	
	endwin();
	initscr();
	if (COLS < (75 + width) || LINES < height) {
		endwin();
		printf("Screen is too small make sure the screen is at least %dx%d\n"
			"Right now it is %dx%d\n", 75 + width, height, COLS, LINES);
		exit(0);
	}
	
	pa_system_ptr->grid_win = newwin(height, width, 0, 0);

	width = COLS - width;
	pa_system_ptr->text_win = newwin(LINES, width, 0, tempwidth);

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
	box(pa_system_ptr->text_win, 0, 0);
	box(pa_system_ptr->grid_win, 0, 0);
	wrefresh(pa_system_ptr->text_win);
	wrefresh(pa_system_ptr->grid_win);
	pa_system_ptr->resize_screen = 1;
	return NULL;
}
