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

/************
 * Functions *
 ************/

int _get_option();
void *_resize_handler(int sig);

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
	//char *name;
	
        	
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);
#ifdef HAVE_BGL
	error_code = slurm_load_node((time_t) NULL, &new_node_ptr, 0);

	if (error_code) {
#ifdef HAVE_BGL_FILES
		int rc;
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
	
#else
	error("This will only run on a BGL system right now.\n");
	exit(0);
#endif
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
		height = DIM_SIZE[Y] * DIM_SIZE[Z] + DIM_SIZE[Y] + 3;
		width = DIM_SIZE[X] + DIM_SIZE[Z] + 3;
			
		signal(SIGWINCH, (sighandler_t) _resize_handler);
			
		initscr();
		if (COLS < (75 + width) || LINES < height) {
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
			
		pa_system_ptr->grid_win = newwin(height, width, starty, startx);
		box(pa_system_ptr->grid_win, 0, 0);
			
		startx = width;
		COLS -= 2;
		width = COLS - width;
		height = LINES;
		pa_system_ptr->text_win = newwin(height, width, starty, startx);
		box(pa_system_ptr->text_win, 0, 0);
	}
	while (!end) {
		if(!params.commandline) {
			_get_option();
		redraw:
			
			init_grid(new_node_ptr);
			wclear(pa_system_ptr->text_win);
			wclear(pa_system_ptr->grid_win);        
			
			pa_system_ptr->xcord = 1;
			pa_system_ptr->ycord = 1;
		}
		print_date();
		switch (params.display) {
		case JOBS:
			get_job();
			break;
		case COMMANDS:
			get_command();
			break;
		case BGLPART:
			get_bgl_part();
			break;
		default:
			get_slurm_part();
			break;
		}
			
		if(!params.commandline) {
			print_grid();
			move(0,0);
			box(pa_system_ptr->text_win, 0, 0);
			box(pa_system_ptr->grid_win, 0, 0);
			wnoutrefresh(pa_system_ptr->text_win);
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
		if (params.iterate) {
			for (i = 0; i < params.iterate; i++) {

				sleep(1);
				if(!params.commandline) {
					if (_get_option())
						goto redraw;
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
		if (params.iterate) 
			endwin();
	}
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
		error("Screen is too small make sure the screen is at least %dx%d\n"
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
	case BGLPART:
		get_bgl_part();
		break;
	default:
		get_slurm_part();
		break;
	}

	print_grid();
	box(pa_system_ptr->text_win, 0, 0);
	box(pa_system_ptr->grid_win, 0, 0);
	wnoutrefresh(pa_system_ptr->text_win);
	wnoutrefresh(pa_system_ptr->grid_win);
	doupdate();
	pa_system_ptr->resize_screen = 1;
	return NULL;
}
