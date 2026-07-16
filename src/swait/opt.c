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

#include "slurm/slurm.h"

#include "src/common/log.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"

#include "src/swait/opt.h"

/* getopt_long codes for long-only options */
#define OPT_LONG_HELP 0x100
#define OPT_LONG_USAGE 0x101
#define OPT_LONG_AUTOCOMP 0x102
#define OPT_LONG_TIMEOUT 0x103

swait_opt_t opt = {
	.array_job_id = NO_VAL,
	.array_task_id = NO_VAL,
};

decl_static_data(help_txt);
decl_static_data(usage_txt);

static void _help(void)
{
	char *txt;
	static_ref_to_cstring(txt, help_txt);
	printf("%s", txt);
	xfree(txt);
}

static void _usage(void)
{
	char *txt;
	static_ref_to_cstring(txt, usage_txt);
	printf("%s", txt);
	xfree(txt);
}

/*
 * Validate a parsed selected-step descriptor as suitable for swait.
 *
 * IN id - parsed selected-step descriptor
 * IN src - original input string (for error messages)
 * RET SLURM_SUCCESS if id describes a single ordinary job; SLURM_ERROR
 *     otherwise (with an error message printed).
 */
static int _validate_selected_step(const slurm_selected_step_t *id,
				   const char *src)
{
	if (id->het_job_offset != NO_VAL) {
		error("%s: het-job offsets are not supported", src);
		return SLURM_ERROR;
	}
	if (id->array_bitmap) {
		error("%s: array-task ranges are not supported, pass one task offset",
		      src);
		return SLURM_ERROR;
	}
	if (id->step_id.step_id != NO_VAL) {
		error("%s: swait operates on a job, not a step", src);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

/*
 * IN src - input string from argv or environment
 * OUT id - populated selected-step descriptor (caller must free
 *          id->array_bitmap if any survives)
 */
static void _parse_jobid_or_die(const char *src, slurm_selected_step_t *id)
{
	if (unfmt_job_id_string(src, id, slurm_conf.max_array_sz) !=
	    SLURM_SUCCESS) {
		error("%s: cannot parse job id", src);
		FREE_NULL_BITMAP(id->array_bitmap);
		exit(2);
	}
	if (_validate_selected_step(id, src) != SLURM_SUCCESS) {
		FREE_NULL_BITMAP(id->array_bitmap);
		exit(2);
	}
}

/*
 * Resolve the target job identifier from argv, or if no positional argument
 * was given, fall back to $SLURM_JOB_SLUID (s<sluid> form), then $SLURM_JOB_ID.
 *
 * IN  argv_jobid - positional argument or NULL
 * OUT id         - populated selected-step descriptor (caller frees any
 *                  array_bitmap)
 */
static void _resolve_target(const char *argv_jobid, slurm_selected_step_t *id)
{
	const char *src = NULL;

	if (argv_jobid && *argv_jobid) {
		src = argv_jobid;
	} else if ((src = getenv("SLURM_JOB_SLUID")) && *src) {
		;
	} else if ((src = getenv("SLURM_JOB_ID")) && *src) {
		;
	} else {
		error("no job id given and SLURM_JOB_SLUID/SLURM_JOB_ID are not set");
		exit(2);
	}

	_parse_jobid_or_die(src, id);
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
		{ "quiet", no_argument, 0, 'Q' },
		{ "timeout", required_argument, 0, OPT_LONG_TIMEOUT },
		{ "usage", no_argument, 0, OPT_LONG_USAGE },
		{ "verbose", no_argument, 0, 'v' },
		{ "version", no_argument, 0, 'V' },
		{ NULL, 0, 0, 0 }
	};

	const char *argv_jobid = NULL;
	slurm_selected_step_t id = SLURM_SELECTED_STEP_INITIALIZER;

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, "hQvV", long_options,
				       &option_index)) != -1) {
		switch (opt_char) {
		case 'h':
		case OPT_LONG_HELP:
			_help();
			exit(0);
		case 'Q':
			opt.quiet = true;
			break;
		case 'v':
			opt.verbose++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
		case OPT_LONG_USAGE:
			_usage();
			exit(0);
		case OPT_LONG_AUTOCOMP:
			suggest_completion(long_options, optarg);
			exit(0);
		case OPT_LONG_TIMEOUT:
			if (!optarg || !*optarg ||
			    parse_uint32(optarg, &opt.timeout)) {
				error("--timeout: invalid value '%s' (must be a non-negative integer)",
				      optarg ? optarg : "");
				exit(2);
			}
			break;
		default:
			info("Try \"swait --help\" for more information");
			exit(2);
		}
	}

	if (opt.quiet && opt.verbose) {
		error("--verbose (-v) and --quiet (-Q) are mutually exclusive");
		exit(2);
	}

	if ((argc - optind) > 1) {
		error("too many positional arguments (expected at most one job id)");
		exit(2);
	}
	if ((argc - optind) == 1)
		argv_jobid = argv[optind];

	_resolve_target(argv_jobid, &id);
	opt.target = id.step_id;
	if (id.array_task_id != NO_VAL) {
		opt.array_job_id = id.step_id.job_id;
		opt.array_task_id = id.array_task_id;
	}
	FREE_NULL_BITMAP(id.array_bitmap);
}
