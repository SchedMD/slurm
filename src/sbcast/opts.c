/****************************************************************************\
 *  opts.c - sbcast command line option processing functions
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2016 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "config.h"

#define _GNU_SOURCE

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/sbcast/sbcast.h"

#define OPT_LONG_EXCLUDE   0x100
#define OPT_LONG_HELP      0x101
#define OPT_LONG_USAGE     0x102
#define OPT_LONG_SEND_LIBS 0x103


/* getopt_long options, integers but not characters */

/* FUNCTIONS */
static void     _help( void );
static uint32_t _map_size( char *buf );
static void     _print_options( void );
static void     _usage( void );

/*
 * parse_command_line, fill in params data structure with data
 */
extern void parse_command_line(int argc, char **argv)
{
	char *env_val = NULL, *sep, *tmp;
	int opt_char, ret;
	int option_index;
	static struct option long_options[] = {
		{"compress",  optional_argument, 0, 'C'},
		{"exclude",   required_argument, 0, OPT_LONG_EXCLUDE},
		{"fanout",    required_argument, 0, 'F'},
		{"force",     no_argument,       0, 'f'},
		{"jobid",     required_argument, 0, 'j'},
		{"send-libs", optional_argument, 0, OPT_LONG_SEND_LIBS},
		{"preserve",  no_argument,       0, 'p'},
		{"size",      required_argument, 0, 's'},
		{"timeout",   required_argument, 0, 't'},
		{"verbose",   no_argument,       0, 'v'},
		{"version",   no_argument,       0, 'V'},
		{"help",      no_argument,       0, OPT_LONG_HELP},
		{"usage",     no_argument,       0, OPT_LONG_USAGE},
		{NULL,        0,                 0, 0}
	};

	if ((tmp = xstrcasestr(slurm_conf.bcast_parameters, "Compression="))) {
		tmp += 12;
		sep = strchr(tmp, ',');
		if (sep)
			sep[0] = '\0';
		params.compress = parse_compress_type(tmp);
		if (sep)
			sep[0] = ',';
	}

	if (slurm_conf.bcast_exclude)
		params.exclude = xstrdup(slurm_conf.bcast_exclude);

	if ((env_val = getenv("SBCAST_COMPRESS")))
		params.compress = parse_compress_type(env_val);
	if ((env_val = getenv("SBCAST_EXCLUDE"))) {
		xfree(params.exclude);
		params.exclude = xstrdup(env_val);
	}
	if ( ( env_val = getenv("SBCAST_FANOUT") ) )
		params.fanout = atoi(env_val);
	if (getenv("SBCAST_FORCE"))
		params.flags |= BCAST_FLAG_FORCE;

	if (getenv("SBCAST_PRESERVE"))
		params.flags |= BCAST_FLAG_PRESERVE;

	if (xstrcasestr(slurm_conf.bcast_parameters, "send_libs"))
		params.flags |= BCAST_FLAG_SEND_LIBS;

	if ((env_val = getenv("SBCAST_SEND_LIBS"))) {
		ret = parse_send_libs(env_val);
		if (ret == -1)
			error("Ignoring unrecognized SBCAST_SEND_LIBS value '%s'",
			      env_val);
		else if (ret)
			params.flags |= BCAST_FLAG_SEND_LIBS;
		else
			params.flags &= ~BCAST_FLAG_SEND_LIBS;
	}

	if ( ( env_val = getenv("SBCAST_SIZE") ) )
		params.block_size = _map_size(env_val);
	else
		params.block_size = 8 * 1024 * 1024;
	if ( ( env_val = getenv("SBCAST_TIMEOUT") ) )
		params.timeout = (atoi(env_val) * 1000);

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, "C::fF:j:ps:t:vV",
			long_options, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'?':
			fprintf(stderr,
				"Try \"sbcast --help\" for more information\n");
			exit(1);
			break;
		case (int)'C':
			params.compress = parse_compress_type(optarg);
			break;
		case (int) OPT_LONG_EXCLUDE:
			xfree(params.exclude);
			params.exclude = xstrdup(optarg);
			break;
		case (int)'f':
			params.flags |= BCAST_FLAG_FORCE;
			break;
		case (int)'F':
			params.fanout = atoi(optarg);
			break;
		case (int)'j':
			params.selected_step = slurm_parse_step_str(optarg);
			break;
		case (int)'p':
			params.flags |= BCAST_FLAG_PRESERVE;
			break;
		case (int) OPT_LONG_SEND_LIBS:
			ret = parse_send_libs(optarg);
			if (ret == -1)
				error("Ignoring unrecognized --send-libs value '%s'",
				      optarg);
			else if (ret)
				params.flags |= BCAST_FLAG_SEND_LIBS;
			else if (params.flags & BCAST_FLAG_SEND_LIBS)
				params.flags &= ~BCAST_FLAG_SEND_LIBS;
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
			print_slurm_version();
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

	if (!params.selected_step ||
	    (params.selected_step->step_id.job_id == NO_VAL)) {
		if (!(env_val = getenv("SLURM_JOB_ID"))) {
			error("Need a job id to run this command.  "
			      "Run from within a Slurm job or use the "
			      "--jobid option.");
			exit(1);
		}
		slurm_destroy_selected_step(params.selected_step);
		params.selected_step = slurm_parse_step_str(env_val);
	}

	params.src_fname = xstrdup(argv[optind]);

	if (argv[optind+1][0] == '/') {
		params.dst_fname = xstrdup(argv[optind+1]);
	} else if ((tmp = xstrcasestr(slurm_conf.bcast_parameters,
				      "DestDir="))) {
		tmp += 8;
		sep = strchr(tmp, ',');
		if (sep)
			sep[0] = '\0';
		xstrfmtcat(params.dst_fname, "%s/%s", tmp, argv[optind+1]);
		if (sep)
			sep[0] = ',';
	} else {
#ifdef HAVE_GET_CURRENT_DIR_NAME
		tmp = get_current_dir_name();
#else
		tmp = malloc(PATH_MAX);
		tmp = getcwd(tmp, PATH_MAX);
#endif
		xstrfmtcat(params.dst_fname, "%s/%s", tmp, argv[optind+1]);
		free(tmp);
	}

	if (params.dst_fname[strlen(params.dst_fname) - 1] == '/') {
		error("Target filename cannot be a directory.");
		exit(1);
	}

	if (params.verbose)
		_print_options();
}

/* map size in string to number, interpret suffix of "k" or "m" */
static uint32_t _map_size( char *buf )
{
	long b_size;
	char *end_ptr;

	b_size = strtol(buf, &end_ptr, 10);
	if ((b_size == LONG_MIN) || (b_size == LONG_MAX) || (b_size < 0)) {
		fprintf(stderr, "size specification is invalid, ignored\n");
		b_size = 0;
	} else if (end_ptr[0] == '\0')
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
	info("compress   = %u", params.compress);
	info("exclude    = %s", params.exclude);
	info("force      = %s",
	     (params.flags & BCAST_FLAG_FORCE) ? "true" : "false");
	info("fanout     = %d", params.fanout);
	info("preserve   = %s",
	     (params.flags & BCAST_FLAG_PRESERVE) ? "true" : "false");
	info("send_libs  = %s",
	     (params.flags & BCAST_FLAG_SEND_LIBS) ? "true" : "false");
	info("timeout    = %d", params.timeout);
	info("verbose    = %d", params.verbose);
	info("source     = %s", params.src_fname);
	info("dest       = %s", params.dst_fname);
	info("-----------------------------");
}


static void _usage( void )
{
	printf("Usage: sbcast [--exclude] [-CfFjpvV] [--send-libs] SOURCE DEST\n");
}

static void _help( void )
{
	printf ("\
Usage: sbcast [OPTIONS] SOURCE DEST\n\
  -C, --compress[=lib]  compress the file being transmitted\n\
  --exclude=<path_list> shared object paths to be excluded\n\
  -f, --force           replace destination file as required\n\
  -F, --fanout=num      specify message fanout\n\
  -j, --jobid=#[+#][.#] specify job ID with optional hetjob offset and/or step ID\n\
  -p, --preserve        preserve modes and times of source file\n\
  --send-libs[=yes|no]  autodetect and broadcast executable's shared objects\n\
  -s, --size=num        block size in bytes (rounded off)\n\
  -t, --timeout=secs    specify message timeout (seconds)\n\
  -v, --verbose         provide detailed event logging\n\
  -V, --version         print version information and exit\n\
\nHelp options:\n\
  --help                show this help message\n\
  --usage               display brief usage message\n");
}
