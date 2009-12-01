/****************************************************************************\
 *  opts.c - sfree command line option processing functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
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

#include "sfree.h"

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

	static struct option long_options[] = {
		{"all",       no_argument,       0, 'a'},
		{"bgblock",   required_argument, 0, 'b'},
		{"partition", required_argument, 0, 'p'},
		{"remove",    no_argument,       0, 'r'},
		{"wait",      no_argument,       0, 'w'},
		{"version",   no_argument,       0, 'V'},
		{"help",      no_argument,       0, 'h'},
		{"usage",     no_argument,       0, 'u'},
		{NULL, 0, 0, 0}
	};

	while ((opt_char =
		getopt_long(argc, argv, "ab:hp:ruVw",
			    long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int) '?':
			fprintf(stderr,
				"Try \"sfree --help\" for more information\n");
		        exit(1);
			break;
		case (int) 'a':
			all_blocks = 1;
		        break;
		case (int) 'b':
		case (int) 'p':
			if(!block_list)
				block_list = list_create(slurm_destroy_char);
		        slurm_addto_char_list(block_list, optarg);
			break;
		case (int) 'h':
		case (int) OPT_LONG_HELP:
			_help();
			exit(0);
		case (int) 'r':
			remove_blocks = 1;
			break;
		case (int) 'u':
		case (int) OPT_LONG_USAGE:
			_usage();
			exit(0);
		case (int) 'V':
			_print_version();
			exit(0);
		case (int) 'w':
			wait_full = true;
			break;
		}
	}

}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage(void)
{
	printf("Usage: sfree [-ahruVw] [-b <name>]\n");
}

static void _help(void)
{
	/* We still honor -p and --partition,
	 * but don't tell users about them here */

	printf("\
Usage: sfree [OPTIONS]\n\
  -a, --all                    free all bgblocks\n\
  -b <name>, --bgblock=<name>  free specific bgblock named\n\
  -r, --remove                 On Dynamic systems this option will remove the\n\
                               block from the system after they are freed.\n\
  -V, --version                output version information and exit\n\
  -w, --wait                   wait to make sure all blocks have been freed\n\
                               (Otherwise sfree will start the free and once\n\
                               sure the block(s) have started to free will\n\
                               exit)\n\
\nHelp options:\n\
  --help                       show this help message\n\
  --usage                      display brief usage message\n");
}
