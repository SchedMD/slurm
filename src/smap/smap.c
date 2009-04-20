/*****************************************************************************\
 *  smap.c - Report overall state the system
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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

#include "config.h"

#include <stdio.h>
#include <signal.h>
#include "src/smap/smap.h"

#ifdef HAVE_3D
#define MIN_SCREEN_WIDTH 92
#else
#define MIN_SCREEN_WIDTH 72
#endif
/********************
 * Global Variables *
 ********************/
int text_line_cnt = 0;

smap_parameters_t params;

int quiet_flag = 0;
int grid_line_cnt = 0;
int max_display;
int resize_screen = 0;

int main_xcord = 1;
int main_ycord = 1;
WINDOW *grid_win = NULL;
WINDOW *text_win = NULL;

/************
 * Functions *
 ************/

static int _get_option();
static void *_resize_handler(int sig);
static int _set_pairs();

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	node_info_msg_t *node_info_ptr = NULL;
	node_info_msg_t *new_node_ptr = NULL;
	int error_code;
	int height = 40;
	int width = 100;
	int startx = 0;
	int starty = 0;
	int end = 0;
	int i;
	int rc;
#ifdef HAVE_BG
	int mapset = 0;	
#endif
	//char *name;
       
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);
	while (slurm_load_node((time_t) NULL, &new_node_ptr, SHOW_ALL)) { 
		error_code = slurm_get_errno();
		printf("slurm_load_node: %s\n", slurm_strerror(error_code));
		if (params.display == COMMANDS) {
			new_node_ptr = NULL;
			break;		/* just continue */
		}
		if (params.iterate == 0)
			exit(1);
		sleep(10);	/* keep trying to reconnect */
	}
	
	ba_init(new_node_ptr);
			
	if(params.partition) {
			
#ifdef HAVE_BG_FILES
		if (!have_db2) {
			printf("Required libraries can not be found "
			       "to access the Bluegene system.\nPlease "
			       "set your LD_LIBRARY_PATH correctly to "
			       "point to them.\n");
			goto part_fini;
		}

		if(!mapset)
			mapset = set_bp_map();
		if(params.partition[0] != 'R') {
			i = strlen(params.partition);
			i -= 3;
			if(i<0) {
				printf("No real block was entered\n");
				goto part_fini;
			}
			char *rack_mid = find_bp_rack_mid(params.partition+i);
			if(rack_mid)
				printf("X=%c Y=%c Z=%c resolves to %s\n",
				       params.partition[X+i],
				       params.partition[Y+i],
				       params.partition[Z+i], 
				       rack_mid);
			else
				printf("X=%c Y=%c Z=%c has no resolve\n",
				       params.partition[X+i],
				       params.partition[Y+i],
				       params.partition[Z+i]);
			
		} else {
			int *coord = find_bp_loc(params.partition);
			if(coord)
				printf("%s resolves to X=%d Y=%d Z=%d\n",
				       params.partition,
				       coord[X], coord[Y], coord[Z]);
			else
				printf("%s has no resolve.\n", 
				       params.partition);
		}
part_fini:
#else
		printf("Must be on BG System to resolve.\n");
#endif
		ba_fini();
		exit(0);
	}
	if(!params.commandline) {
		
		signal(SIGWINCH, (void (*)(int))_resize_handler);
		initscr();
		
#ifdef HAVE_3D
		height = DIM_SIZE[Y] * DIM_SIZE[Z] + DIM_SIZE[Y] + 3;
		width = DIM_SIZE[X] + DIM_SIZE[Z] + 3;
		if (COLS < (MIN_SCREEN_WIDTH + width) || LINES < height) {
			width += MIN_SCREEN_WIDTH;
#else
		height = 10;
		width = COLS;
	        if (COLS < MIN_SCREEN_WIDTH || LINES < height) {
			width = MIN_SCREEN_WIDTH;
#endif
			endwin();
			error("Screen is too small make sure the screen "
			      "is at least %dx%d\n"
			      "Right now it is %dx%d\n", 
			      width, 
			      height, 
			      COLS, 
			      LINES);
			ba_fini();
			exit(1);
		}
		
		raw();
		keypad(stdscr, TRUE);
		noecho();
		cbreak();
		curs_set(1);
		nodelay(stdscr, TRUE);
		start_color();
		_set_pairs();
		
		grid_win = newwin(height, width, starty, startx);
		max_display = grid_win->_maxy * grid_win->_maxx;
		//scrollok(grid_win, TRUE);
		
#ifdef HAVE_3D
		startx = width;
		COLS -= 2;
		width = COLS - width;
		height = LINES;
#else
		startx = 0;
		starty = height;
		height = LINES - height;
		
#endif
		
		text_win = newwin(height, width, starty, startx);
        }
	while (!end) {
		if(!params.commandline) {
			_get_option();
		redraw:
			
			clear_window(text_win);
			clear_window(grid_win);
			move(0,0);
			
			init_grid(new_node_ptr);
			main_xcord = 1;
			main_ycord = 1;
		}
		print_date();
		switch (params.display) {
		case JOBS:
			get_job();
			break;
		case RESERVATIONS:
			get_reservation();
			break;
		case SLURMPART:
			get_slurm_part();
			break;
#ifdef HAVE_BG
		case COMMANDS:
			if(!mapset) {
				mapset = set_bp_map();
				wclear(text_win);
				//doupdate();
				//move(0,0);
			}
			get_command();
			break;
		case BGPART:
			get_bg_part();
			break;
#else
		case COMMANDS:
		case BGPART:
			error("Must be on a BG SYSTEM to run this command");
			if(!params.commandline)
				endwin();
			ba_fini();
			exit(1);
			break;
#endif
		}
			
		if(!params.commandline) {
			//wscrl(grid_win,-1);
			box(text_win, 0, 0);
			wnoutrefresh(text_win);
			
			print_grid(grid_line_cnt * (grid_win->_maxx-1));
			box(grid_win, 0, 0);
			wnoutrefresh(grid_win);
			
			doupdate();
			
			node_info_ptr = new_node_ptr;
			if (node_info_ptr) {
				error_code = slurm_load_node(
					node_info_ptr->last_update, 
					&new_node_ptr, SHOW_ALL);
				if (error_code == SLURM_SUCCESS)
					slurm_free_node_info_msg(
						node_info_ptr);
				else if (slurm_get_errno() 
					 == SLURM_NO_CHANGE_IN_DATA) {
					error_code = SLURM_SUCCESS;
					new_node_ptr = node_info_ptr;
				}
			} else {
				error_code = slurm_load_node((time_t) NULL, 
						&new_node_ptr, SHOW_ALL);
			}
			if (error_code && (quiet_flag != 1)) {
				if(!params.commandline) {
					mvwprintw(
						text_win,
						main_ycord, 
						1,
						"slurm_load_node: %s",
						slurm_strerror(
							slurm_get_errno()));
					main_ycord++;
				} else {
					printf("slurm_load_node: %s",
					       slurm_strerror(
						       slurm_get_errno()));
				}
			}
		}

		if (params.iterate) {
			for (i = 0; i < params.iterate; i++) {

				sleep(1);
				if(!params.commandline) {
					if ((rc = _get_option()) == 1)
						goto redraw;
					else if (resize_screen) {
						resize_screen = 0;
						goto redraw;
					}
				}
			}
		} else
			break;
		
	}
	
	if(!params.commandline) {
		nodelay(stdscr, FALSE);
		getch();
		endwin();
	}
	ba_fini();
        
	exit(0);
}

static int _get_option()
{
	int ch;

	ch = getch();
	switch (ch) {
	case KEY_RIGHT:
	case '-':
	case '_':
		text_line_cnt++;		
		return 1;
		break;
	case KEY_LEFT:
	case '=':
	case '+':
		text_line_cnt--;
		if(text_line_cnt<0) {
			text_line_cnt = 0;
			return 0;		
	
		}
		return 1;
		break;
		
	case 's':
		text_line_cnt = 0;
		grid_line_cnt = 0;
		params.display = SLURMPART;
		return 1;
		break;
	case 'j':
		text_line_cnt = 0;
		grid_line_cnt = 0;
		params.display = JOBS;
		return 1;
		break;
	case 'r':
		text_line_cnt = 0;
		grid_line_cnt = 0;
		params.display = RESERVATIONS;
		return 1;
		break;
#ifdef HAVE_BG
	case 'b':
		text_line_cnt = 0;
		grid_line_cnt = 0;
		params.display = BGPART;
		return 1;
		break;
	case 'c':
		params.display = COMMANDS;
		return 1;
		break;
#endif

#ifndef HAVE_BG
	case 'u':
	case KEY_UP:
		grid_line_cnt--;
		if(grid_line_cnt<0) {
			grid_line_cnt = 0;
			return 0;
		}
		return 1;
		break;
	case 'd':
	case KEY_DOWN:
		grid_line_cnt++;
		if((((grid_line_cnt-2) * (grid_win->_maxx-1)) + 
		    max_display) > DIM_SIZE[X]) {
			grid_line_cnt--;
			return 0;		
		}
		
		return 1;
		break;
#endif
	case 'q':
	case '\n':
		endwin();
		ba_fini();
		exit(0);
		break;
	}
	return 0;
}

static void *_resize_handler(int sig)
{
	int startx=0, starty=0;
	int height, width;
	main_ycord = 1;
	
	/* clear existing data and update to avoid ghost during resize */
	clear_window(text_win);
	clear_window(grid_win);
	doupdate();
	delwin(grid_win);
	delwin(text_win);
	
	endwin();
	COLS=0;
	LINES=0;
	initscr();
	doupdate();	/* update now to make sure we get the new size */
	getmaxyx(stdscr,LINES,COLS);

#ifdef HAVE_3D
	height = DIM_SIZE[Y] * DIM_SIZE[Z] + DIM_SIZE[Y] + 3;
	width = DIM_SIZE[X] + DIM_SIZE[Z] + 3;
	if (COLS < (MIN_SCREEN_WIDTH + width) || LINES < height) {
		width += MIN_SCREEN_WIDTH;
#else
	height = 10;
	width = COLS;
	if (COLS < MIN_SCREEN_WIDTH || LINES < height) {
		width = MIN_SCREEN_WIDTH;
#endif

		endwin();
		error("Screen is too small make sure "
		      "the screen is at least %dx%d\n"
		      "Right now it is %dx%d\n", width, height, COLS, LINES);
		ba_fini();
		exit(0);
	}
        
	grid_win = newwin(height, width, starty, startx);
	max_display = grid_win->_maxy * grid_win->_maxx;
		
#ifdef HAVE_3D
	startx = width;
	COLS -= 2;
	width = COLS - width;
	height = LINES;
#else
	startx = 0;
	starty = height;
	height = LINES - height;
	
#endif
	
	text_win = newwin(height, width, starty, startx);
	
	print_date();
	switch (params.display) {
	case JOBS:
		get_job();
		break;
	case RESERVATIONS:
		get_reservation();
		break;
	case SLURMPART:
		get_slurm_part();
		break;
#ifdef HAVE_BG
	case COMMANDS:
		get_command();
		break;
	case BGPART:
		get_bg_part();
		break;
#endif
	}

	print_grid(grid_line_cnt * (grid_win->_maxx-1));
	box(text_win, 0, 0);
	box(grid_win, 0, 0);
	wnoutrefresh(text_win);
	wnoutrefresh(grid_win);
	doupdate();
	resize_screen = 1;
	return NULL;
}

static int _set_pairs()
{
	int x;

	for (x = 0; x < 6; x++) {
		init_pair(colors[x], colors[x], COLOR_BLACK);
	}
	return 1;
}
