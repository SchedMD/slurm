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
#include "src/smap/smap.h"

/********************
 * Global Variables *
 ********************/
struct smap_parameters params;

smap_info *smap_info_ptr;

int quiet_flag = 0;

/************
 * Functions *
 ************/
int _get_option(void);

typedef void (*sighandler_t) (int);

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

	smap_info_ptr = xmalloc(sizeof(smap_info));
	smap_info_ptr->xcord = 1;
	smap_info_ptr->ycord = 1;
	smap_info_ptr->X = 0;
	smap_info_ptr->Y = 0;
	smap_info_ptr->Z = 0;
	smap_info_ptr->num_of_proc = 0;
	smap_info_ptr->resize_screen = 0;

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
			if (smap_info_ptr->X < temp)
				smap_info_ptr->X = temp;
			temp = (start % 100) / 10;
			if (smap_info_ptr->Y < temp)
				smap_info_ptr->Y = temp;
			temp = start % 10;
			if (smap_info_ptr->Z < temp)
				smap_info_ptr->Z = temp;
		}
		smap_info_ptr->X++;
		smap_info_ptr->Y++;
		smap_info_ptr->Z++;
		smap_info_ptr->grid =
		    (axis ***) xmalloc(sizeof(axis **) * smap_info_ptr->X);
		for (i = 0; i < smap_info_ptr->X; i++) {
			smap_info_ptr->grid[i] =
			    (axis **) xmalloc(sizeof(axis *) *
					      smap_info_ptr->Y);
			for (j = 0; j < smap_info_ptr->Y; j++)
				smap_info_ptr->grid[i][j] =
				    (axis *) xmalloc(sizeof(axis) *
						     smap_info_ptr->Z);
		}
		smap_info_ptr->num_of_proc = node_info_ptr->record_count;

		smap_info_ptr->fill_in_value =
		    (axis *) xmalloc(sizeof(axis) *
				     smap_info_ptr->num_of_proc);

		height =
		    smap_info_ptr->Y * smap_info_ptr->Z +
		    smap_info_ptr->Y * 2;
		width = smap_info_ptr->X * 2;
		init_grid(node_info_ptr);
	}
	signal(SIGWINCH, (sighandler_t) _resize_handler);
#else
	printf("This will only run on a BGL system right now.\n");
	exit(0);
#endif
	initscr();
	if (COLS < (75 + width) || LINES < height) {
		endwin();
		printf
		    ("Screen is too small make sure the screen is at least %dx%d\nRight now it is %dx%d\n",
		     75 + width, height, COLS, LINES);
		exit(0);
	}
	raw();
	keypad(stdscr, TRUE);
	noecho();
	cbreak();
	curs_set(1);
	nodelay(stdscr, TRUE);
	start_color();

	smap_info_ptr->grid_win = newwin(height, width, starty, startx);
	box(smap_info_ptr->grid_win, 0, 0);

	startx = width;
	COLS -= 2;
	width = COLS - width;
	height = LINES;
	smap_info_ptr->text_win = newwin(height, width, starty, startx);
	box(smap_info_ptr->text_win, 0, 0);
	wrefresh(smap_info_ptr->text_win);
	wrefresh(smap_info_ptr->grid_win);

	while (!end) {
		_get_option();
	      redraw:


		init_grid(node_info_ptr);
		wclear(smap_info_ptr->text_win);
		//wclear(smap_info_ptr->grid_win);        
		smap_info_ptr->xcord = 1;
		smap_info_ptr->ycord = 1;

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
		box(smap_info_ptr->text_win, 0, 0);
		box(smap_info_ptr->grid_win, 0, 0);
		wrefresh(smap_info_ptr->text_win);
		wrefresh(smap_info_ptr->grid_win);

		if (params.iterate) {
			for (i = 0; i < params.iterate; i++) {

				sleep(1);
				if (_get_option())
					goto redraw;
				else if (smap_info_ptr->resize_screen) {
					smap_info_ptr->resize_screen = 0;
					goto redraw;
				}
			}
		} else
			break;
	}

	nodelay(stdscr, FALSE);
	getch();
	endwin();
	for (i = 0; i < smap_info_ptr->X; i++) {
		for (j = 0; j < smap_info_ptr->Y; j++)
			xfree(smap_info_ptr->grid[i][j]);
		xfree(smap_info_ptr->grid[i]);
	}
	xfree(smap_info_ptr->grid);
	xfree(smap_info_ptr->fill_in_value);

	exit(0);
}

void print_date(void)
{
	smap_info_ptr->now_time = time(NULL);
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "%s",
		  ctime(&smap_info_ptr->now_time));
	smap_info_ptr->ycord++;
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
	int height =
	    smap_info_ptr->Y * smap_info_ptr->Z + smap_info_ptr->Y * 2;
	int width = smap_info_ptr->X * 2;

	smap_info_ptr->ycord = 1;
	wclear(smap_info_ptr->grid_win);
	wclear(smap_info_ptr->text_win);

	endwin();
	initscr();

	getmaxyx(stdscr, LINES, COLS);

	delwin(smap_info_ptr->grid_win);
	smap_info_ptr->grid_win = newwin(height, width, 0, 0);

	width = COLS - width;
	delwin(smap_info_ptr->text_win);
	smap_info_ptr->text_win =
	    newwin(LINES, width, 0, smap_info_ptr->X * 2);

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
	box(smap_info_ptr->text_win, 0, 0);
	box(smap_info_ptr->grid_win, 0, 0);
	wrefresh(smap_info_ptr->text_win);
	wrefresh(smap_info_ptr->grid_win);
	smap_info_ptr->resize_screen = 1;
	return NULL;
}
