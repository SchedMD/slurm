/****************************************************************************\
 *  opts.c - smap command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/smap/smap.h"

/* FUNCTIONS */
static void _help(void);
static void _print_version(void);
static void _usage(void);

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char *argv[])
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
				tmp = BGPART;
			else if (!strcmp(optarg, "c"))
				tmp = COMMANDS;
			else if (!strcmp(optarg, "r"))
				tmp = RESERVATIONS;

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

extern void print_date()
{
	time_t now_time = time(NULL);

	if (params.commandline) {
		printf("%s", ctime(&now_time));
	} else {
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%s",
			  ctime(&now_time));
		main_ycord++;
	}
}

extern void clear_window(WINDOW *win)
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
#ifdef HAVE_BG
	printf("Usage: smap [-hVcp] [-D bcjrs] [-i seconds]\n");
#else
	printf("Usage: smap [-hVcp] [-D jrs] [-i seconds]\n");
#endif
}

static void _help(void)
{
	printf("\
Usage: smap [OPTIONS]\n\
  -D, --display              set which display mode to use\n\
                             b=bluegene blocks\n\
                             c=set bluegene configuration\n\
                             j=jobs\n\
                             r=reservations\n\
                             s=slurm partitions\n\
  -h, --noheader             no headers on output\n\
  -i, --iterate=seconds      specify an interation period\n\
  -V, --version              output version information and exit\n\
  -c, --commandline          output written with straight to the\n\
                             commandline.\n\
  -p, --parse                used with -c to not format output, but use\n\
                             single tab delimitation.\n\
  -R, --resolve              resolve an XYZ coord from a Rack/Midplane id \n\
                             or vice versa.\n\
                             (i.e. -R R101 for R/M input -R 101 for XYZ).\n\
\nHelp options:\n\
  --help                     show this help message\n\
  --usage                    display brief usage message\n");
}
