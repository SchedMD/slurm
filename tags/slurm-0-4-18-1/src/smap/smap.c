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

#include "config.h"

#include <signal.h>
#include "src/smap/smap.h"

/********************
 * Global Variables *
 ********************/
smap_parameters_t params;

int quiet_flag = 0;
int line_cnt = 0;
int max_display;
		
/************
 * Functions *
 ************/

static int _get_option();
static void *_resize_handler(int sig);
static int _set_pairs();
static int _scroll_grid(int dir);

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	node_info_msg_t *node_info_ptr=NULL;
	node_info_msg_t *new_node_ptr=NULL;
	int error_code;
	int height = 40;
	int width = 100;
	int startx = 0;
	int starty = 0;
	int end = 0;
	int i;
	int rc;
		
	//char *name;	
        	
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);
	error_code = slurm_load_node((time_t) NULL, &new_node_ptr, 0);

	if (error_code) {
#ifdef HAVE_BGL_FILES
		rm_BGL_t *bgl;
		rm_size3D_t bp_size;
		if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
			exit(-1);
		}
		
		if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
			exit(-1);
		}
		
		if ((rc = rm_get_data(bgl, RM_Msize, &bp_size)) != STATUS_OK) {
			exit(-1);
		}
		verbose("BlueGene configured with %d x %d x %d base partitions",
			bp_size.X, bp_size.Y, bp_size.Z);
		DIM_SIZE[X]=bp_size.X;
		DIM_SIZE[Y]=bp_size.Y;
		DIM_SIZE[Z]=bp_size.Z;
		rm_free_BGL(bgl);
#else
		slurm_perror("slurm_load_node");
		exit(0);
#endif
		pa_init(NULL);
	} else {
		pa_init(new_node_ptr);
	}	
	if(params.partition) {
		if(params.partition[0] == 'r')
			params.partition[0] = 'R';
		if(params.partition[0] != 'R') {
			char *rack_mid = find_bp_rack_mid(params.partition);
			if(rack_mid)
				printf("X=%c Y=%c Z=%c resolves to %s\n",
				       params.partition[X],
				       params.partition[Y],
				       params.partition[Z], 
				       rack_mid);
			else
				printf("X=%c Y=%c Z=%c has no resolve\n",
				       params.partition[X],
				       params.partition[Y],
				       params.partition[Z]);
			
		} else {
			int *coord = find_bp_loc(params.partition);
			if(coord)
				printf("%s resolves to X=%d Y=%d Z=%d\n",
				       params.partition,
				       coord[X],
				       coord[Y],
				       coord[Z]);
			else
				printf("%s has no resolve.\n", params.partition);
		}
		exit(0);
	}
	if(!params.commandline) {
		
		signal(SIGWINCH, (sighandler_t) _resize_handler);
		initscr();
		
#if HAVE_BGL
		height = DIM_SIZE[Y] * DIM_SIZE[Z] + DIM_SIZE[Y] + 3;
		width = DIM_SIZE[X] + DIM_SIZE[Z] + 3;
		if (COLS < (75 + width) || LINES < height) {
#else
		height = 10;
		width = COLS;
	        if (COLS < 75 || LINES < height) {
#endif
			
			endwin();
			error("Screen is too small make sure the screen is at least %dx%d\n"
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
		_set_pairs();
		
		pa_system_ptr->grid_win = newwin(height, width, starty, startx);
		max_display = pa_system_ptr->grid_win->_maxy*pa_system_ptr->grid_win->_maxx;
		//scrollok(pa_system_ptr->grid_win, TRUE);
		
#if HAVE_BGL
		startx = width;
		COLS -= 2;
		width = COLS - width;
		height = LINES;
#else
		startx = 0;
		starty = height;
		height = LINES - height;
		
#endif
		
		pa_system_ptr->text_win = newwin(height, width, starty, startx);
        }
	while (!end) {
		if(!params.commandline) {
			_get_option();
		redraw:
			
			line_cnt = 0;
			clear_window(pa_system_ptr->text_win);
			clear_window(pa_system_ptr->grid_win);
			doupdate();
			move(0,0);
			
			init_grid(new_node_ptr);
			pa_system_ptr->xcord = 1;
			pa_system_ptr->ycord = 1;
		}
		print_date();
		switch (params.display) {
		case JOBS:
			get_job();
			break;
		case SLURMPART:
			get_slurm_part();
			break;
#if HAVE_BGL
		case COMMANDS:
			get_command();
			break;
		case BGLPART:
			get_bgl_part();
			break;
#endif
		}
			
		if(!params.commandline) {
			//wscrl(pa_system_ptr->grid_win,-1);
			box(pa_system_ptr->text_win, 0, 0);
			wnoutrefresh(pa_system_ptr->text_win);
			
			print_grid(0);
			box(pa_system_ptr->grid_win, 0, 0);
			wnoutrefresh(pa_system_ptr->grid_win);
			
			doupdate();
			
			node_info_ptr = new_node_ptr;
			if (node_info_ptr) {
				error_code = slurm_load_node(node_info_ptr->last_update, 
								   &new_node_ptr, 0);
				if (error_code == SLURM_SUCCESS)
					slurm_free_node_info_msg(node_info_ptr);
				else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
					error_code = SLURM_SUCCESS;
					new_node_ptr = node_info_ptr;
				}
			} else {
				error_code = slurm_load_node((time_t) NULL, 
								   &new_node_ptr, 0);
			}
			if (error_code) {
				if (quiet_flag != 1) {
					if(!params.commandline) {
						mvwprintw(pa_system_ptr->text_win,
							  pa_system_ptr->ycord, 1,
							  "slurm_load_node: %s",
							  slurm_strerror(slurm_get_errno()));
						pa_system_ptr->ycord++;
					} else {
						printf("slurm_load_node: %s",
						       slurm_strerror(slurm_get_errno()));
					}
				}
			}
		}
	scrolling_grid:
		if (params.iterate) {
			for (i = 0; i < params.iterate; i++) {

				sleep(1);
				if(!params.commandline) {
					if ((rc = _get_option()) == 1)
						goto redraw;
					else if (rc == 2)
						goto scrolling_grid;
					else if (pa_system_ptr->resize_screen) {
						pa_system_ptr->resize_screen = 0;
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
	pa_fini();
        
	exit(0);
}

static int _get_option()
{
	char ch;

	ch = getch();
	switch (ch) {
	case 's':
		params.display = SLURMPART;
		return 1;
		break;
	case 'j':
		params.display = JOBS;
		return 1;
		break;
#if HAVE_BGL
	case 'b':
		params.display = BGLPART;
		return 1;
		break;
	case 'c':
		params.display = COMMANDS;
		return 1;
		break;
#endif
	case 'u':
	case KEY_UP:
		line_cnt--;
		if(line_cnt<0)
			line_cnt = 0;
		_scroll_grid(line_cnt*(pa_system_ptr->grid_win->_maxx-1));
		return 2;
		break;
	case 'd':
	case KEY_DOWN:
		if((((line_cnt-1)*(pa_system_ptr->grid_win->_maxx-1)) + 
		    max_display) > DIM_SIZE[X]) {
			line_cnt--;
			return 2;
		}
		line_cnt++;
		_scroll_grid(line_cnt*(pa_system_ptr->grid_win->_maxx-1));
		return 2;
		break;
	case 'q':
	case '\n':
		endwin();
		exit(0);
		break;
	}
	return 0;
}

static void *_resize_handler(int sig)
{
	int startx=0, starty=0;
	pa_system_ptr->ycord = 1;
	
	delwin(pa_system_ptr->grid_win);
	delwin(pa_system_ptr->text_win);
	
	endwin();
	initscr();

#if HAVE_BGL
	int height = DIM_SIZE[Y] * DIM_SIZE[Z] + DIM_SIZE[Y] + 3;
	int width = DIM_SIZE[X] + DIM_SIZE[Z] + 3;
	if (COLS < (75 + width) || LINES < height) {
#else
	int height = 10;
	int width = COLS;
	if (COLS < 75 || LINES < height) {
#endif

		endwin();
		error("Screen is too small make sure the screen is at least %dx%d\n"
		      "Right now it is %dx%d\n", 75 + width, height, COLS, LINES);
		exit(0);
	}
        
	pa_system_ptr->grid_win = newwin(height, width, starty, startx);
	max_display = pa_system_ptr->grid_win->_maxy*
		pa_system_ptr->grid_win->_maxx;
		
#if HAVE_BGL
	startx = width;
	COLS -= 2;
	width = COLS - width;
	height = LINES;
#else
	startx = 0;
	starty = height;
	height = LINES - height;
	
#endif
	
	pa_system_ptr->text_win = newwin(height, width, starty, startx);
	
	print_date();
	switch (params.display) {
	case JOBS:
		get_job();
		break;
	case SLURMPART:
		get_slurm_part();
		break;
#if HAVE_BGL
	case COMMANDS:
		get_command();
		break;
	case BGLPART:
		get_bgl_part();
		break;
#endif
	}

	print_grid(0);
	box(pa_system_ptr->text_win, 0, 0);
	box(pa_system_ptr->grid_win, 0, 0);
	wnoutrefresh(pa_system_ptr->text_win);
	wnoutrefresh(pa_system_ptr->grid_win);
	doupdate();
	pa_system_ptr->resize_screen = 1;
	return NULL;
}

static int _set_pairs()
{
	int x,y,z;

	z = 0;
	y = 65;
	for (x = 0; x < 128; x++) {
		if (y == 91)
			y = 97;
		else if(y == 123)
			y = 48;
		else if(y == 58)
			y = 65;
		pa_system_ptr->fill_in_value[x].letter = y;
		y++;
		if(z == 4)
			z++;
		z = z % 7;
		if (z == 0)
			z = 1;
		
		pa_system_ptr->fill_in_value[x].color = z;
		z++;
		
		init_pair(pa_system_ptr->fill_in_value[x].color,
			  pa_system_ptr->fill_in_value[x].color,
			  COLOR_BLACK);
	}
	return 1;
}

static int _scroll_grid(int dir)
{
	print_grid(dir);
	wnoutrefresh(pa_system_ptr->grid_win);
	doupdate();
	return 1;
}
