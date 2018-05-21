/****************************************************************************\
 *  opts.c - functions for processing sdiag parameters
 *****************************************************************************
 *  Produced at Barcelona Supercomputing Center, December 2011
 *  Written by Alejandro Lucero <alucero@bsc.es>
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

#define _GNU_SOURCE

#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include "src/common/xstring.h"
#include "src/common/proc_args.h"

#define OPT_LONG_USAGE 0x101

static void  _help( void );
static void  _usage( void );

extern int  sdiag_param;
extern bool sort_by_id;
extern bool sort_by_time;
extern bool sort_by_time2;

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char **argv)
{
	int opt_char;
	int option_index;
	static struct option long_options[] = {
		{"all",		no_argument,	0,	'a'},
		{"help",	no_argument,	0,	'h'},
		{"reset",	no_argument,	0,	'r'},
		{"sort-by-id",	no_argument,	0,	'i'},
		{"sort-by-time",no_argument,	0,	't'},
		{"sort-by-time2",no_argument,	0,	'T'},
		{"usage",	no_argument,	0,	OPT_LONG_USAGE},
		{"version",     no_argument,	0,	'V'},
		{NULL,		0,		0,	0}
	};

	while ((opt_char = getopt_long(argc, argv, "ahirtTV", long_options,
				       &option_index)) != -1) {
		switch (opt_char) {
			case (int)'a':
				sdiag_param = STAT_COMMAND_GET;
				break;
			case (int)'h':
				_help();
				exit(0);
				break;
			case (int)'i':
				sort_by_id = true;
				break;
			case (int)'r':
				sdiag_param = STAT_COMMAND_RESET;
				break;
			case (int)'t':
				sort_by_time = true;
				break;
			case (int)'T':
				sort_by_time2 = true;
				break;
			case (int) 'V':
				print_slurm_version();
				exit(0);
				break;
			case (int)OPT_LONG_USAGE:
				_usage();
				exit(0);
				break;
		}
	}
}


static void _usage( void )
{
	printf("\nUsage: sdiag [-ar] \n");
}

static void _help( void )
{
	printf ("\
Usage: sdiag [OPTIONS]\n\
  -a              all statistics\n\
  -r              reset statistics\n\
\nHelp options:\n\
  --help          show this help message\n\
  --sort-by-id    sort RPCs by id\n\
  --sort-by-time  sort RPCs by total run time\n\
  --sort-by-time2 sort RPCs by average run time\n\
  --usage         display brief usage message\n\
  --version       display current version number\n");
}
