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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include "src/common/xstring.h"
#include "src/smap/smap.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP  0x100
#define OPT_LONG_USAGE 0x101
#define OPT_LONG_HIDE	0x102

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
		{"help", no_argument, 0, OPT_LONG_HELP},
		{"usage", no_argument, 0, OPT_LONG_USAGE},
		{"hide", no_argument, 0, OPT_LONG_HIDE},
		{NULL, 0, 0, 0}
	};

	while ((opt_char =
		getopt_long(argc, argv, "D:hi:V",
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



static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage(void)
{
	printf("\
Usage: smap [-hV] [-D jsbc] [-i seconds]\n");
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
\nHelp options:\n\
  --help                     show this help message\n\
  --usage                    display brief usage message\n");
}
