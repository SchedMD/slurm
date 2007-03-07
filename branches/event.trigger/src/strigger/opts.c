/****************************************************************************\
 *  opts.c - strigger command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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
#include "src/strigger/strigger.h"

#define OPT_LONG_HELP   0x100
#define OPT_LONG_USAGE  0x101
#define OPT_LONG_SET    0x102
#define OPT_LONG_GET    0x103
#define OPT_LONG_CLEAR  0x104

/* getopt_long options, integers but not characters */

/* FUNCTIONS */
static void     _help( void );
static void     _init_options( void );
static void     _print_options( void );
static void     _print_version( void );
static void     _usage( void );
static void     _validate_version( void );

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char *argv[])
{
	char *env_val = NULL;
	int first = 1;
	int opt_char;
	int option_index;
	static struct option long_options[] = {
		{"down",      no_argument,       0, 'd'},
		{"fini",      no_argument,       0, 'f'},
		{"id",        required_argument, 0, 'i'},
		{"jobid",     required_argument, 0, 'j'},
		{"node",      optional_argument, 0, 'n'},
		{"offset",    required_argument, 0, 'o'},
		{"program",   required_argument, 0, 'p'},
		{"time",      no_argument,       0, 't'},
		{"up",        no_argument,       0, 'u'},
		{"verbose",   no_argument,       0, 'v'},
		{"version",   no_argument,       0, 'V'},
		{"help",      no_argument,       0, OPT_LONG_HELP},
		{"usage",     no_argument,       0, OPT_LONG_USAGE},
		{"set",       no_argument,       0, OPT_LONG_SET},
		{"get",       no_argument,       0, OPT_LONG_GET},
		{"clear",     no_argument,       0, OPT_LONG_CLEAR},
		{NULL,        0,                 0, 0}
	};

	_init_options();

	optind = 0;
	while((opt_char = getopt_long(argc, argv, "dfi:j:no:p:tuvV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			if (first) {
				first = 0;
				fprintf(stderr, "Try \"strigger --help\" for "
					"more information\n");
			}
			exit(1);
			break;
		case (int)'d':
			params.node_down = true;
			break;
		case (int)'f':
			params.job_fini = true;
			break;
		case (int)'i':
			params.trigger_id = atoi(optarg);
			break;
		case (int)'j':
			params.job_id = atoi(optarg);
			break;
		case (int)'n':
			xfree(params.node_id);
			if (optarg)
				params.node_id = xstrcpy(optarg);
			else
				params.node_id = xstrcpy("*");
			break;
		case (int)'o':
			params.offset = atoi(optarg);
			break;
		case (int)'p':
			xfree(params.program);
			params.program = xstrcpy(optarg);
			break;
		case (int)'t':
			params.time_limit = true;
			break;
		case (int)'u':
			params.node_up = true;
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
		case (int) OPT_LONG_SET:
			params.mode_set = true;
			break;
		case (int) OPT_LONG_GET:
			params.mode_get = true;
			break;
		case (int) OPT_LONG_CLEAR:
			params.mode_clear = true;
			break;
		}
	}

	if (params.verbose)
		_print_options();
	_validate_options();
}

/* initialize the parameters */
static void _print_options( void )
{
	params.mode_set   = false;
	params.mode_get   = false;
	params.mode_clear = false;

	params.node_down  = false;
	params.trigger_id = 0;
	params.job_fini   = false;
	params.job_id     = 0;
	params.node_id    = NULL;
	params.offset     = 0;
	params.program    = NULL;
	params.time_limit = false;
	params.node_up    = false;
	params.verbose    = 0;
}

/* print the parameters specified */
static void _print_options( void )
{
	info("-----------------------------");
	info("set        = %s", params.mode_set ? "true" : "false");
	info("get        = %s", params.mode_get ? "true" : "false");
	info("clear      = %s", params.mode_clear ? "true" : "false");
	info("node_down  = %s", params.node_down ? "true" : "false");
	info("trigger_id = %u", params.trigger_id);
	info("job_fini   = %s", params.job_fini ? "true" : "false");
	info("job_id     = %u", params.job_id);
	info("node       = %s", params.node_id);
	info("offset     = %d secs", params.offset);
	info("program    = %s", params.program);
	info("time_limit = %s", params.time_limit ? "true" : "false");
	info("node_up    = %s", params.node_up ? "true" : "false");
	info("verbose    = %d", params.verbose);
	info("-----------------------------");
}

static void _validate_options( void )
{
	if ((params.mode_set + params.mode_get + params.mode_clear) != 1) {
		error("You must use exactly one of the following options: "
			"--set, --get or --clear");
		exit(1);
	}
	
	if (params.mode_clear
	&&  ((params.trigger_id + params.job_id) == 0)) {
		error("You must specify a --id or --jobid to clear");
		exit(1);
	}

	if (params.mode_set
	&&  ((params.node_down + params.node_up +
	      params.job_fini  + params.time_limit) == 0)) {
		error("You must specify a trigger (--down, --up, --time or --fini)");
		exit(1);
	}
	if (params.mode_set && (params.program == NULL)) {
		error("You must specify a --program value");
		exit(1);
	}
}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

static void _usage( void )
{
	printf("Usage: strigger [--set | --get | --clear | --version] [-dfijnoptuv]\n");
}

static void _help( void )
{
	printf ("\
Usage: strigger [--set | --get | --clear] [OPTIONS]\n\
      --set           create a trigger\n\
      --get           get trigger information\n\
      --clear         delete a trigger\n\n\
  -d, --down          trigger event when node goes DOWN\n\
  -f, --fini          trigger event when job finishes\n\
  -i, --id=#          a trigger's ID number\n\
  -j, --jobid=#       trigger related to specific jobid\n\
  -n, --node[=host]   trigger related to specific node, all nodes by default\n\
  -o, --offset=#      trigger's offset time from event, negative to preceed\n\
  -p, --program=path  pathname of program to execute when triggered\n\
  -t, --time          trigger event on job's time limit\n\
  -u, --up            trigger event when node returned to service from DOWN state\n\
  -v, --verbose       print detailed event logging\n\
  -V, --version       print version information and exit\n\
\nHelp options:\n\
  --help              show this help message\n\
  --usage             display brief usage message\n");
}
