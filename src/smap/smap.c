/*****************************************************************************\
 *  smap.c - Report overall state the system
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Copyright (C) 2011 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <stdio.h>
#include <signal.h>
#include "src/smap/smap.h"

static int min_screen_width = 80;

/********************
 * Global Variables *
 ********************/
int text_line_cnt = 0;

smap_parameters_t params;
smap_system_t *smap_system_ptr;

int quiet_flag = 0;
int grid_line_cnt = 0;
int max_display;
int resize_screen = 0;

int *dim_size = NULL;
char letters[62];
char colors[6];
int main_xcord = 1;
int main_ycord = 1;
WINDOW *grid_win = NULL;
WINDOW *text_win = NULL;

/************
 * Functions *
 ************/

static int  _get_option(void);
static void _init_colors(void);
static void *_resize_handler(int sig);
static int  _set_pairs(void);
static void _smap_exit(int rc);

int main(int argc, char **argv)
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

	slurm_conf_init(NULL);
	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_USER, NULL);
	}

	if (params.cluster_dims == 3)
		min_screen_width = 92;

	while (slurm_load_node((time_t) NULL,
			       &new_node_ptr, SHOW_ALL)) {
		error_code = slurm_get_errno();
		printf("slurm_load_node: %s\n",
		       slurm_strerror(error_code));
		if (params.iterate == 0)
			exit(1);
		sleep(10);	/* keep trying to reconnect */
	}

	if (dim_size == NULL) {
		dim_size = get_cluster_dims(new_node_ptr);
		if ((dim_size == NULL) || (dim_size[0] < 1))
			fatal("Invalid system dimensions");
	}
	_init_colors();

	if (!params.commandline) {
		int check_width = min_screen_width;

		initscr();
		init_grid(new_node_ptr, COLS);
		signal(SIGWINCH, (void (*)(int))_resize_handler);

		if (params.cluster_dims == 3) {
			height = dim_size[1] * dim_size[2] + dim_size[1] + 3;
			width = dim_size[0] + dim_size[2] + 3;
			check_width += width;
		} else {
			height = 10;
			width = COLS;
		}

	        if ((COLS < check_width) || (LINES < height)) {
			endwin();
			error("Screen is too small make sure the screen "
			      "is at least %dx%d\n"
			      "Right now it is %dx%d\n",
			      check_width,
			      height,
			      COLS,
			      LINES);
			_smap_exit(1);	/* Calls exit(), no return */
		}

		raw();
		keypad(stdscr, true);
		noecho();
		cbreak();
		curs_set(0);
		nodelay(stdscr, true);
		start_color();
		_set_pairs();

		grid_win = newwin(height, width, starty, startx);
		max_display = (getmaxy(grid_win) - 1) * (getmaxx(grid_win) - 1);

		if (params.cluster_dims == 3) {
			startx = width;
			width = COLS - width - 2;
			height = LINES;
		} else {
			startx = 0;
			starty = height;
			height = LINES - height;
		}

		text_win = newwin(height, width, starty, startx);
        }
	while (!end) {
		if (!params.commandline) {
			_get_option();
redraw:
			clear_window(text_win);
			clear_window(grid_win);
			move(0, 0);

			update_grid(new_node_ptr);
			main_xcord = 1;
			main_ycord = 1;
		}

		if (!params.no_header)
			print_date();

		clear_grid();
		switch (params.display) {
		case JOBS:
			get_job();
			break;
		case RESERVATIONS:
			get_reservation();
			break;
		case PARTITION:
			get_slurm_part();
			break;
		}

		if (!params.commandline) {
			box(text_win, 0, 0);
			wnoutrefresh(text_win);

			print_grid();
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
				error_code = slurm_load_node(
					(time_t) NULL,
					&new_node_ptr, SHOW_ALL);
			}
			if (error_code && (quiet_flag != 1)) {
				if (!params.commandline) {
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
				if (!params.commandline) {
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

	if (!params.commandline) {
		nodelay(stdscr, false);
		getch();
		endwin();
	}

	_smap_exit(0);	/* Calls exit(), no return */
	exit(0);	/* Redundant, but eliminates compiler warning */
}

static void _init_colors(void)
{
	int x, y, z;
	/* make the letters array only contain letters upper and lower (62) */
	y = 'A';
	for (x = 0; x < 62; x++) {
		if (y == '[')
			y = 'a';
		else if (y == '{')
			y = '0';
		else if (y == ':')
			y = 'A';
		letters[x] = y;
		y++;
	}

	z = 1;
	for (x = 0; x < 6; x++) {
		if (z == 4)
			z++;
		colors[x] = z;
		z++;
	}
}

/* Variation of exit() that releases memory as needed for memory leak test */
static void _smap_exit(int rc)
{
#ifdef MEMORY_LEAK_DEBUG
	uid_cache_clear();
	free_grid();

#endif
	if (!params.commandline)
		curs_set(1);
	exit(rc);
}

static int _get_option(void)
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
		if (text_line_cnt < 0) {
			text_line_cnt = 0;
			return 0;
		}
		return 1;
		break;
	case 'H':
	case 'h':
		if (params.all_flag)
			params.all_flag = 0;
		else
			params.all_flag = 1;
		return 1;
		break;
	case 's':
		text_line_cnt = 0;
		grid_line_cnt = 0;
		params.display = PARTITION;
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
	case 'q':
	case '\n':
		endwin();
		_smap_exit(0);	/* Calls exit(), no return */
		break;
	default:
		break;
	}
	return 0;
}

static void *_resize_handler(int sig)
{
	int startx = 0, starty = 0;
	int height = 40, width = 100;
	int check_width = min_screen_width;
	main_ycord = 1;

	/* clear existing data and update to avoid ghost during resize */
	clear_window(text_win);
	clear_window(grid_win);
	doupdate();
	delwin(grid_win);
	delwin(text_win);

	endwin();
	initscr();
	doupdate();	/* update now to make sure we get the new size */

	if (params.cluster_dims == 3) {
		height = dim_size[1] * dim_size[2] + dim_size[1] + 3;
		width = dim_size[0] + dim_size[2] + 3;
		check_width += width;
	} else {
		height = 10;
		width = COLS;
	}

	if (COLS < check_width || LINES < height) {
		endwin();
		error("Screen is too small make sure "
		      "the screen is at least %dx%d\n"
		      "Right now it is %dx%d\n", width, height, COLS, LINES);
		_smap_exit(0);	/* Calls exit(), no return */
	}

	grid_win = newwin(height, width, starty, startx);
	max_display = (getmaxy(grid_win) - 1) * (getmaxx(grid_win) - 1);

	if (params.cluster_dims == 3) {
		startx = width;
		width = COLS - width - 2;
		height = LINES;
	} else {
		startx = 0;
		starty = height;
		height = LINES - height;
	}

	text_win = newwin(height, width, starty, startx);

	print_date();
	switch (params.display) {
	case JOBS:
		get_job();
		break;
	case RESERVATIONS:
		get_reservation();
		break;
	case PARTITION:
		get_slurm_part();
		break;
	}

	print_grid();
	box(text_win, 0, 0);
	box(grid_win, 0, 0);
	wnoutrefresh(text_win);
	wnoutrefresh(grid_win);
	doupdate();
	resize_screen = 1;
	return NULL;
}

static int _set_pairs(void)
{
	int x;

	for (x = 0; x < 6; x++) {
		init_pair(colors[x], colors[x], COLOR_BLACK);
	}
	return 1;
}
