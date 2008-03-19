/****************************************************************************\
 *  opts.c - sbcast command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/common/xstring.h"
#include "src/sbcast/sbcast.h"

#define OPT_LONG_HELP   0x100
#define OPT_LONG_USAGE  0x101

/* getopt_long options, integers but not characters */

/* FUNCTIONS */
static void     _help( void );
static uint32_t _map_size( char *buf );
static void     _print_options( void );
static void     _print_version( void );
static void     _usage( void );

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
		{"fanout",    required_argument, 0, 'F'},
		{"force",     no_argument,       0, 'f'},
		{"preserve",  no_argument,       0, 'p'},
		{"size",      required_argument, 0, 's'},
		{"timeout",   required_argument, 0, 't'},
		{"verbose",   no_argument,       0, 'v'},
		{"version",   no_argument,       0, 'V'},
		{"help",      no_argument,       0, OPT_LONG_HELP},
		{"usage",     no_argument,       0, OPT_LONG_USAGE},
		{NULL,        0,                 0, 0}
	};

	if (getenv("SBCAST_COMPRESS"))
		params.compress = true;
	if ( ( env_val = getenv("SBCAST_FANOUT") ) )
		params.fanout = atoi(env_val);
	if (getenv("SBCAST_FORCE"))
		params.force = true;
	if (getenv("SBCAST_PRESERVE"))
		params.preserve = true;
	if ( ( env_val = getenv("SBCAST_SIZE") ) )
		params.block_size = _map_size(env_val);
	if ( ( env_val = getenv("SBCAST_TIMEOUT") ) )
		params.timeout = (atoi(env_val) * 1000);

	optind = 0;
	while((opt_char = getopt_long(argc, argv, "CfF:ps:t:vV",
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
		case (int)'F':
			params.fanout = atoi(optarg);
			break;
		case (int)'p':
			params.preserve = true;
			break;
		case (int) 's':
			params.block_size = _map_size(optarg);
			break;
		case (int)'t':
			params.timeout = (atoi(optarg) * 1000);
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
		fprintf(stderr, "Need two file names, have %d names\n", 
			(argc - optind));
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

/* map size in string to number, interpret suffix of "k" or "m" */
static uint32_t _map_size( char *buf )
{
	long b_size;
	char *end_ptr;

	b_size = strtol(buf, &end_ptr, 10);
	if ((b_size == LONG_MIN) || (b_size == LONG_MAX)
	||  (b_size < 0)) {
		fprintf(stderr, "size specification is invalid, ignored\n");
		b_size = 0;
	}
	else if (end_ptr[0] == '\0')
		;
	else if ((end_ptr[0] == 'k') || (end_ptr[0] == 'K'))
		b_size *= 1024;
	else if ((end_ptr[0] == 'm') || (end_ptr[0] == 'M'))
		b_size *= (1024 * 1024);
	else {
		fprintf(stderr, "size specification is invalid, ignored\n");
		b_size = 0;
	}
	return (uint32_t) b_size;
}

/* print the parameters specified */
static void _print_options( void )
{
	info("-----------------------------");
	info("block_size = %u", params.block_size);
	info("compress   = %s", params.compress ? "true" : "false");
	info("force      = %s", params.force ? "true" : "false");
	info("fanout     = %d", params.fanout);
	info("preserve   = %s", params.preserve ? "true" : "false");
	info("timeout    = %d", params.timeout);
	info("verbose    = %d", params.verbose);
	info("source     = %s", params.src_fname);
	info("dest       = %s", params.dst_fname);
	info("-----------------------------");
}


static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage( void )
{
	printf("Usage: sbcast [-CfFpvV] SOURCE DEST\n");
}

static void _help( void )
{
	printf ("\
Usage: sbcast [OPTIONS] SOURCE DEST\n\
  -C, --compress      compress the file being transmitted\n\
  -f, --force         replace destination file as required\n\
  -F, --fanout=num    specify message fanout\n\
  -p, --preserve      preserve modes and times of source file\n\
  -s, --size=num      block size in bytes (rounded off)\n\
  -t, --timeout=secs  specify message timeout (seconds)\n\
  -v, --verbose       provide detailed event logging\n\
  -V, --version       print version information and exit\n\
\nHelp options:\n\
  --help              show this help message\n\
  --usage             display brief usage message\n");
}
