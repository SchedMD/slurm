/*****************************************************************************\
 *  smap.c - -*- linux-c -*- Report overall state the system
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov>, 
 *             Morris Jette <jette1@llnl.gov>, et. al.
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

#include "src/common/xstring.h"
#include "src/common/macros.h"
#include "src/smap/smap.h"

/********************
 * Global Variables *
 ********************/
struct smap_parameters params;
int quiet_flag = 0;
int xcord = 1;
int ycord = 1;
WINDOW *grid_win;
WINDOW *text_win;
time_t now;
axis grid[X][Y][Z];
axis fill_in_value[num_of_proc];

/************
 * Functions *
 ************/
static void _init_window(WINDOW * win);
static void _init_grid(void);
static void _print_grid(void);
static void _init_grid(void);
static void _get_job(void);
static void _get_part(void);
static int _print_job(job_info_t * job_ptr);
static int _print_part(partition_info_t * part_ptr);
void print_date(void);
void print_header_part(void);
void print_header_job(void);
int set_grid_bgl(int startx, int starty, int startz, int endx, int endy,
		 int endz, int count);
int set_grid(int start, int end, int count);

int main(int argc, char *argv[])
{
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	char ch;

	int height = Y * Z + Y * 2;
	int width = X * 2;
	int startx = 0;
	int starty = 0;
	int end = 0;
	int i;

	log_init(xbasename(argv[0]), opts, SYSLOG_FACILITY_DAEMON, NULL);
	parse_command_line(argc, argv);
	initscr();
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
	width = COLS - width;
	height = LINES;
	text_win = newwin(height, width, starty, startx);
	box(text_win, 0, 0);

	while (!end) {
		ch = getch();
		switch (ch) {
		case 'b':
			params.display = BGLPART;
			break;
		case 's':
			params.display = SLURMPART;
			break;
		case 'j':
			params.display = JOBS;
			break;
		case 'q':
		case '\n':
			endwin();
			exit(0);
			break;
		}
	      redraw:
		_init_grid();
		_init_window(text_win);
		xcord = 1;
		ycord = 1;
		//if (params.iterate && params.long_output)
		print_date();
		switch (params.display) {
		case JOBS:
			_get_job();
			break;
		default:
			_get_part();
			break;
		}

		_print_grid();
		box(text_win, 0, 0);
		box(grid_win, 0, 0);
		wrefresh(text_win);
		wrefresh(grid_win);
		//sleep(5);
		if (params.iterate) {
			for (i = 0; i < params.iterate; i++) {
				sleep(1);
				ch = getch();
				switch (ch) {
				case 'b':
					params.display = BGLPART;
					goto redraw;
					break;
				case 's':
					params.display = SLURMPART;
					goto redraw;
					break;
				case 'j':
					params.display = JOBS;
					goto redraw;
					break;
				case 'q':
				case '\n':
					endwin();
					exit(0);
					break;
				}
			}
		} else
			break;
	}

	nodelay(stdscr, FALSE);
	getch();
	endwin();

	exit(0);
}

void print_date(void)
{
	now = time(NULL);
	mvwprintw(text_win, ycord, xcord, "%s", ctime(&now));
	ycord++;
}

void print_header_part(void)
{
	mvwprintw(text_win, ycord, xcord, "IDENT");
	xcord += 8;
	mvwprintw(text_win, ycord, xcord, "PARTITION");
	xcord += 12;
	mvwprintw(text_win, ycord, xcord, "AVAIL");
	xcord += 10;
	mvwprintw(text_win, ycord, xcord, "TIMELIMIT");
	xcord += 12;
	mvwprintw(text_win, ycord, xcord, "NODES");
	xcord += 8;
	mvwprintw(text_win, ycord, xcord, "NODELIST");
	xcord = 1;
	ycord++;
}
void print_header_job(void)
{
	mvwprintw(text_win, ycord, xcord, "IDENT");
	xcord += 8;
	mvwprintw(text_win, ycord, xcord, "JOBID");
	xcord += 8;
	mvwprintw(text_win, ycord, xcord, "PARTITION");
	xcord += 12;
	mvwprintw(text_win, ycord, xcord, "USER");
	xcord += 10;
	mvwprintw(text_win, ycord, xcord, "NAME");
	xcord += 12;
	mvwprintw(text_win, ycord, xcord, "STATE");
	xcord += 10;
	mvwprintw(text_win, ycord, xcord, "TIME");
	xcord += 12;
	mvwprintw(text_win, ycord, xcord, "NODES");
	xcord += 8;
	mvwprintw(text_win, ycord, xcord, "NODELIST");
	xcord = 1;
	ycord++;

}

static void _get_job()
{
	int error_code = -1, i, j, count = 0;

	job_info_msg_t *job_info_ptr = NULL;
	job_info_t job;

	error_code = slurm_load_jobs((time_t) NULL, &job_info_ptr, 0);
	if (error_code) {
		if (quiet_flag != 1)
			slurm_perror("slurm_load_jobs error");
		return;
	}
	if (job_info_ptr->record_count && !params.no_header)
		print_header_job();
	for (i = 0; i < job_info_ptr->record_count; i++) {
		job = job_info_ptr->job_array[i];
		if (job.node_inx[0] != -1) {
			job.num_nodes = 0;
			j = 0;
			while (job.node_inx[j] >= 0) {
				job.num_nodes +=
				    (job.node_inx[j + 1] + 1) -
				    job.node_inx[j];
				set_grid(job.node_inx[j],
					 job.node_inx[j + 1], count);
				j += 2;
			}
			job.num_procs = (int) fill_in_value[count].letter;
			wattron(text_win,
				COLOR_PAIR(fill_in_value[count].color));
			_print_job(&job);
			wattroff(text_win,
				 COLOR_PAIR(fill_in_value[count].color));
			count++;
		}
	}
	return;
}

static void _get_part(void)
{
	int error_code, i, j, count = 0;
	partition_info_msg_t *part_info_ptr;
	partition_info_t part;
	char node_entry[13];
	int start, startx, starty, startz, endx, endy, endz;
	error_code =
	    slurm_load_partitions((time_t) NULL, &part_info_ptr, 0);
	if (error_code) {
		if (quiet_flag != 1)
			slurm_perror("slurm_load_partitions error");
		return;
	}

	if (part_info_ptr->record_count && !params.no_header)
		print_header_part();
	for (i = 0; i < part_info_ptr->record_count; i++) {
		j = 0;
		part = part_info_ptr->partition_array[i];

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
					_print_part(&part);
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
				_print_part(&part);
				wattroff(text_win,
					 COLOR_PAIR(fill_in_value[count].
						    color));
				count++;
			}
		}
	}

	return;
}

static int _print_job(job_info_t * job_ptr)
{
	time_t time;
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
	return printed;
}

static int _print_part(partition_info_t * part_ptr)
{
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	char *nodes;

	mvwprintw(text_win, ycord, xcord, "%c", part_ptr->root_only);
	xcord += 8;
	mvwprintw(text_win, ycord, xcord, "%s", part_ptr->name);
	xcord += 12;
	if (part_ptr->state_up)
		mvwprintw(text_win, ycord, xcord, "UP");
	else
		mvwprintw(text_win, ycord, xcord, "DOWN");
	xcord += 10;
	if (part_ptr->max_time == INFINITE)
		mvwprintw(text_win, ycord, xcord, "UNLIMITED");
	else
		mvwprintw(text_win, ycord, xcord, "%u",
			  part_ptr->max_time);

	xcord += 12;
	mvwprintw(text_win, ycord, xcord, "%d", part_ptr->total_nodes);
	xcord += 8;

	tempxcord = xcord;
	width = text_win->_maxx - xcord;
	if (params.display == BGLPART)
		nodes = part_ptr->allow_groups;
	else
		nodes = part_ptr->nodes;
	while (nodes[i] != '\0') {
		if ((printed =
		     mvwaddch(text_win, ycord, xcord, nodes[i])) < 0)
			return printed;
		xcord++;
		width = text_win->_maxx - xcord;
		if (nodes[i] == '[')
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

int set_grid_bgl(int startx, int starty, int startz, int endx, int endy,
		 int endz, int count)
{
	int x, y, z;
	int i = 0;
	for (x = startx; x <= endx; x++)
		for (y = starty; y <= endy; y++)
			for (z = startz; z <= endz; z++) {
				grid[x][y][z].letter =
				    fill_in_value[count].letter;
				grid[x][y][z].color =
				    fill_in_value[count].color;
				i++;
			}

	return i;
}

int set_grid(int start, int end, int count)
{
	int x, y, z;
	for (y = Y - 1; y >= 0; y--)
		for (z = 0; z < Z; z++)
			for (x = 0; x < X; x++) {
				if (grid[x][y][z].indecies >= start
				    && grid[x][y][z].indecies <= end) {
					grid[x][y][z].letter =
					    fill_in_value[count].letter;
					grid[x][y][z].color =
					    fill_in_value[count].color;
				}
			}

	return 1;
}

/* _init_window - clear window */
static void _init_window(WINDOW * win)
{
	int x, y;
	for (x = 1; x < win->_maxx; x++)
		for (y = 1; y < ycord; y++)
			mvwaddch(win, y, x, ' ');
	return;
}

/* _init_grid - set values of every grid point */
static void _init_grid(void)
{
	int x, y, z, i = 0;
	for (x = 0; x < X; x++)
		for (y = 0; y < Y; y++)
			for (z = 0; z < Z; z++) {
				grid[x][y][z].color = 7;
				grid[x][y][z].letter = '.';
				grid[x][y][z].indecies = i++;
			}
	y = 65;
	z = 0;
	for (x = 0; x < num_of_proc; x++) {
		fill_in_value[x].letter = y;
		z = z % 7;
		if (z == 0)
			z = 1;
		fill_in_value[x].color = z;
		z++;
		y++;
	}
	return;
}

/* _print_grid - print values of every grid point */
static void _print_grid(void)
{
	int x, y, z, i = 0, offset = Z;
	int grid_xcord, grid_ycord = 2;
	for (y = Y - 1; y >= 0; y--) {
		offset = Z + 1;
		for (z = 0; z < Z; z++) {
			grid_xcord = offset;

			for (x = 0; x < X; x++) {
				init_pair(grid[x][y][z].color,
					  grid[x][y][z].color,
					  COLOR_BLACK);
				wattron(grid_win,
					COLOR_PAIR(grid[x][y][z].color));
				//printf("%d%d%d %c",x,y,z,grid[x][y][z].letter);
				mvwprintw(grid_win, grid_ycord, grid_xcord,
					  "%c", grid[x][y][z].letter);
				wattroff(grid_win,
					 COLOR_PAIR(grid[x][y][z].color));
				grid_xcord++;
				i++;
			}
			grid_ycord++;
			offset--;
		}
		grid_ycord++;
	}
	return;
}
