/****************************************************************************\
 *  opts.c - sbcast command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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
#include "src/sbcast/sbcast.h"

#define OPT_LONG_HELP   0x100
#define OPT_LONG_USAGE  0x101

/* getopt_long options, integers but not characters */

/* FUNCTIONS */
static void  _help( void );
static void  _print_options( void );
static void  _print_version( void );
static void  _usage( void );

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char *argv[])
{
	char *env_val = NULL;
	int opt_char;
	int option_index;
	static struct option long_options[] = {
		{"compress",  no_argument,       0, 'C'},
		{"force",     no_argument,       0, 'f'},
		{"preserve",  no_argument,       0, 'p'},
		{"verbose",   no_argument,       0, 'v'},
		{"version",   no_argument,       0, 'V'},
		{"help",      no_argument,       0, OPT_LONG_HELP},
		{"usage",     no_argument,       0, OPT_LONG_USAGE},
		{NULL,        0,                 0, 0}
	};

	if (getenv("SBCAST_COMPRESS"))
		params.compress = true;
	if ( ( env_val = getenv("SBCAST_FORCE") ) )
		params.force = true;
	if ( ( env_val = getenv("SBCAST_PRESERVE") ) )
		params.preserve = true;

	optind = 0;
	while((opt_char = getopt_long(argc, argv, "CfpvV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr, 
				"Try \"sbcast --help\" for more information\n");
			exit(1);
			break;
		case (int)'C':
			params.compress = true;
			break;
		case (int)'f':
			params.force = true;
			break;
		case (int)'p':
			params.preserve = true;
			break;
		case (int) 'v':
			params.verbose++;
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
		}
	}

	if ((argc - optind) != 2) {
		fprintf(stderr, "Missing file arguments\n");
		fprintf(stderr, "Try \"sbcast --help\" for more information\n");
		exit(1);
	}
	params.src_fname = xstrdup(argv[optind]);
	params.dst_fname = xstrdup(argv[optind+1]);

	if (params.verbose)
		_print_options();
#ifdef HAVE_BG
	fprintf(stderr, "sbcast not supported on BlueGene systems\n");
	exit(1);
#endif
}

/* print the parameters specified */
void _print_options( void )
{
	info("-----------------------------");
	info("compress  = %s", params.compress ? "true" : "false");
	info("force     = %s", params.force ? "true" : "false");
	info("preserve  = %s", params.preserve ? "true" : "false");
	info("verbose   = %d", params.verbose);
	info("source    = %s", params.src_fname);
	info("dest      = %s", params.dst_fname);
	info("-----------------------------");
}


static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage( void )
{
	printf("Usage: sbcast [-CfpvV] SOURCE DEST\n");
}

static void _help( void )
{
	printf ("\
Usage: sbcast [OPTIONS] SOURCE DEST\n\
  -C, --compress      compress the file being transmitted\n\
  -f, --force         replace destination file as required\n\
  -p, --preserve      preserve modes and times of source file\n\
  -v, --verbose       provide detailed event logging\n\
  -V, --version       print version information and exit\n\
\nHelp options:\n\
  --help              show this help message\n\
  --usage             display brief usage message\n");
}
