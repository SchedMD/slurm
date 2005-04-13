/****************************************************************************\
 *  opts.c - smap command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

/* FUNCTIONS */
static void _help(void);
static void _print_version(void);
static void _usage(void);

/*
 * parse_command_line, fill in params data structure with data
 */
void parse_command_line(int argc, char *argv[])
{
	int opt_char;
	int option_index;
	int tmp = 0;
	static struct option long_options[] = {
		{"display", required_argument, 0, 'D'},
		{"noheader", no_argument, 0, 'h'},
		{"iterate", required_argument, 0, 'i'},
		{"version", no_argument, 0, 'V'},
		{"commandline", no_argument, 0, 'c'},
		{"parse", no_argument, 0, 'p'},
		{"resolve", required_argument, 0, 'R'},
		{"help", no_argument, 0, OPT_LONG_HELP},
		{"usage", no_argument, 0, OPT_LONG_USAGE},
		{"hide", no_argument, 0, OPT_LONG_HIDE},
		{NULL, 0, 0, 0}
	};

	while ((opt_char =
		getopt_long(argc, argv, "D:hi:VcpR:",
			    long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int) '?':
			fprintf(stderr,
				"Try \"smap --help\" for more information\n");
			exit(1);
			break;
		case (int) 'D':
			if (!strcmp(optarg, "j"))
				tmp = JOBS;
			else if (!strcmp(optarg, "s"))
				tmp = SLURMPART;
			else if (!strcmp(optarg, "b"))
				tmp = BGLPART;
			else if (!strcmp(optarg, "c"))
				tmp = COMMANDS;

			params.display = tmp;
			break;
		case (int) 'h':
			params.no_header = true;
			break;
		case (int) 'i':
			params.iterate = atoi(optarg);
			if (params.iterate <= 0) {
				error("Error: --iterate=%s");
				exit(1);
			}
			break;
		case (int) 'V':
			_print_version();
			exit(0);
		case (int) 'c':
			params.commandline = TRUE;
			break;
		case (int) 'p':
			params.parse = TRUE;
			break;
		case (int) 'R':
			params.commandline = TRUE;
			params.partition = strdup(optarg);
			break;
		case (int) OPT_LONG_HELP:
			_help();
			exit(0);
		case (int) OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_HIDE:
			params.all_flag = false;
			break;
		}
	}

}

void snprint_time(char *buf, size_t buf_size, time_t time)
{
	if (time == INFINITE) {
		snprintf(buf, buf_size, "UNLIMITED");
	} else {
		long days, hours, minutes, seconds;
		seconds = time % 60;
		minutes = (time / 60) % 60;
		hours = (time / 3600) % 24;
		days = time / 86400;

		if (days)
			snprintf(buf, buf_size,
				"%ld:%2.2ld:%2.2ld:%2.2ld",
				days, hours, minutes, seconds);
		else if (hours)
			snprintf(buf, buf_size,
				"%ld:%2.2ld:%2.2ld", 
				hours, minutes, seconds);
		else
			snprintf(buf, buf_size,
				"%ld:%2.2ld", minutes,seconds);
	}
}

void print_date()
{
	pa_system_ptr->now_time = time(NULL);
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%s",
		  ctime(&pa_system_ptr->now_time));
	pa_system_ptr->ycord++;
}

void clear_window(WINDOW *win)
{
	int x,y;
	for(x=0; x<=win->_maxx; x++)
		for(y=0; y<win->_maxy; y++) {
			mvwaddch(win, y, x, ' ');
		}
	wmove(win, 1, 1);
	wnoutrefresh(win);
}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage(void)
{
	printf("\
Usage: smap [-hVcp] [-D jsbc] [-i seconds]\n");
}

static void _help(void)
{
	printf("\
Usage: smap [OPTIONS]\n\
  -D, --display              set which Display mode to use\n\
      j=jobs\n\
      s=slurm partitions\n\
      b=BG/L partitions\n\
      c=set configuration\n\
  -h, --noheader             no headers on output\n\
  -i, --iterate=seconds      specify an interation period\n\
  -V, --version              output version information and exit\n\
  -c, --commandline          output written with straight to the commandline.\n\
  -p, --parse                used with -c to not format output, but use single tab delimitation.\n\
  -R, --resolve              resolve an XYZ coord from a Rack/Midplane id or vice versa.\n\
                             (i.e. -R R101 for R/M input -R 101 for XYZ).\n\
\nHelp options:\n\
  --help                     show this help message\n\
  --usage                    display brief usage message\n");
}
