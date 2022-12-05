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

#include "src/common/data.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"
#include "src/interfaces/serializer.h"

#include "sdiag.h"

#define OPT_LONG_USAGE 0x101
#define OPT_LONG_JSON 0x102
#define OPT_LONG_YAML 0x103
#define OPT_LONG_AUTOCOMP 0x104

static void  _help( void );
static void  _usage( void );

static void _opt_env(void)
{
	char *env_val;

	if ((env_val = getenv("SLURM_CLUSTERS"))) {
		if (!(params.clusters = slurmdb_get_info_cluster(env_val))) {
			print_db_notok(env_val, 1);
			exit(1);
		}
	}
}

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char **argv)
{
	int opt_char;
	int option_index;
	static struct option long_options[] = {
		{"autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP},
		{"all",		no_argument,	0,	'a'},
		{"help",	no_argument,	0,	'h'},
		{"reset",	no_argument,	0,	'r'},
		{"sort-by-id",	no_argument,	0,	'i'},
		{"cluster",     required_argument, 0,   'M'},
		{"clusters",    required_argument, 0,   'M'},
		{"sort-by-time",no_argument,	0,	't'},
		{"sort-by-time2",no_argument,	0,	'T'},
		{"usage",	no_argument,	0,	OPT_LONG_USAGE},
		{"version",     no_argument,	0,	'V'},
		{"json", no_argument, 0, OPT_LONG_JSON},
		{"yaml", no_argument, 0, OPT_LONG_YAML},
		{NULL,		0,		0,	0}
	};

	/* default options */
	params.mode = STAT_COMMAND_GET;
	params.sort = SORT_COUNT;

	/* get defaults from environment */
	_opt_env();

	while ((opt_char = getopt_long(argc, argv, "ahiM:rtTV", long_options,
				       &option_index)) != -1) {
		switch (opt_char) {
			case (int)'?':
				fprintf(stderr,
					"Try \"sdiag --help\" for more information\n");
				exit(1);
				break;
			case (int)'a':
				params.mode = STAT_COMMAND_GET;
				break;
			case (int)'h':
				_help();
				exit(0);
				break;
			case (int)'i':
				params.sort = SORT_ID;
				break;
			case (int)'M':
				if (params.clusters)
					FREE_NULL_LIST(params.clusters);
				if (!(params.clusters = slurmdb_get_info_cluster(optarg))) {
					print_db_notok(optarg, 0);
					exit(1);
				}
				break;
			case (int)'r':
				params.mode = STAT_COMMAND_RESET;
				break;
			case (int)'t':
				params.sort = SORT_TIME;
				break;
			case (int)'T':
				params.sort = SORT_TIME2;
				break;
			case (int) 'V':
				print_slurm_version();
				exit(0);
				break;
			case (int)OPT_LONG_USAGE:
				_usage();
				exit(0);
				break;
			case OPT_LONG_JSON:
				params.mimetype = MIME_TYPE_JSON;
				(void) data_init();
				(void) serializer_g_init(MIME_TYPE_JSON_PLUGIN,
							 NULL);
				break;
			case OPT_LONG_YAML:
				params.mimetype = MIME_TYPE_YAML;
				(void) data_init();
				(void) serializer_g_init(MIME_TYPE_YAML_PLUGIN,
							 NULL);
				break;
			case OPT_LONG_AUTOCOMP:
				suggest_completion(long_options, optarg);
				exit(0);
				break;
		}
	}

	if (params.clusters) {
		if (list_count(params.clusters) > 1) {
			fatal("Only one cluster can be used at a time with sdiag");
		}
		working_cluster_rec = list_peek(params.clusters);
	}
}


static void _usage( void )
{
	printf("Usage: sdiag [-M cluster] [-aritT]\n");
}

static void _help( void )
{
	printf ("\
Usage: sdiag [OPTIONS]\n\
  -a, --all           all statistics\n\
  -r, --reset         reset statistics\n\
  -M, --cluster       direct the request to a specific cluster\n\
  -i, --sort-by-id    sort RPCs by id\n\
  -t, --sort-by-time  sort RPCs by total run time\n\
  -T, --sort-by-time2 sort RPCs by average run time\n\
  -V, --version       display current version number\n\
  --json              Produce JSON output\n\
  --yaml              Produce YAML output\n\
\nHelp options:\n\
  --help          show this help message\n\
  --usage         display brief usage message\n");
}
