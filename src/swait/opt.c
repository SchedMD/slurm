/*****************************************************************************\
 *  opt.c - swait command line option parsing.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

#include "src/common/proc_args.h"
#include "src/common/ref.h"
#include "src/common/slurm_opt.h"
#include "src/common/xmalloc.h"

#include "src/swait/opt.h"

/* getopt_long codes for long-only options */
#define OPT_LONG_HELP 0x100
#define OPT_LONG_USAGE 0x101
#define OPT_LONG_AUTOCOMP 0x102

swait_opt_t opt = { 0 };

decl_static_data(help_txt);
decl_static_data(usage_txt);

/*
 * Print full help text to stdout.
 */
static void _help(void)
{
	char *txt;
	static_ref_to_cstring(txt, help_txt);
	printf("%s", txt);
	xfree(txt);
}

/*
 * Print short usage line to stdout.
 */
static void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	printf("%s", txt);
	xfree(txt);
}

/*
 * Parse command line, fill in the global opt struct, and exit on errors.
 *
 * IN argc - argument count
 * IN argv - argument vector
 */
extern void parse_command_line(int argc, char **argv)
{
	int opt_char = 0, option_index = 0;
	static struct option long_options[] = {
		{ "autocomplete", required_argument, 0, OPT_LONG_AUTOCOMP },
		{ "help", no_argument, 0, OPT_LONG_HELP },
		{ "usage", no_argument, 0, OPT_LONG_USAGE },
		{ "version", no_argument, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, "hV", long_options,
				       &option_index)) != -1) {
		switch (opt_char) {
		case 'h':
		case OPT_LONG_HELP:
			_help();
			exit(0);
		case 'V':
			print_slurm_version();
			exit(0);
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
		default:
			fprintf(stderr,
				"Try \"swait --help\" for more information\n");
			exit(2);
		}
	}
}
