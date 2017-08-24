/*****************************************************************************\
 *  opt.c - options processing for sattach
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#define _GNU_SOURCE

#include <ctype.h>		/* isdigit    */
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>		/* va_start   */
#include <stdio.h>
#include <stdlib.h>		/* getenv     */
#include <string.h>		/* strcpy, strncasecmp */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h" /* contains getnodename() */
#include "src/common/slurm_mpi.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/sattach/opt.h"

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_LAYOUT_ONLY   0x100
#define LONG_OPT_DEBUGGER_TEST 0x101
#define LONG_OPT_IN_FILTER     0x102
#define LONG_OPT_OUT_FILTER    0x103
#define LONG_OPT_ERR_FILTER    0x104
#define LONG_OPT_PTY           0x105

/*---- global variables, defined in opt.h ----*/
opt_t opt;
int error_exit = 1;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

static void  _help(void);

/* fill in default options  */
static void _opt_default(void);

/* set options based upon env vars  */
static void _opt_env(void);

static void _opt_args(int argc, char **argv);

/* list known options and their settings  */
static void  _opt_list(void);

/* verify options sanity  */
static bool _opt_verify(void);

static void _process_env_var(env_vars_t *e, const char *val);

/* Get a POSITIVE decimal integer from arg */
static int  _get_pos_int(const char *arg, const char *what);

static void  _usage(void);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char **argv)
{
	/* initialize option defaults */
	_opt_default();

	/* initialize options with env vars */
	_opt_env();

	/* initialize options with argv */
	_opt_args(argc, argv);

	if (opt.verbose > 1)
		_opt_list();

	return 1;

}

/*
 * print error message to stderr with opt.progname prepended
 */
#undef USE_ARGERROR
#if USE_ARGERROR
static void argerror(const char *msg, ...)
  __attribute__ ((format (printf, 1, 2)));
static void argerror(const char *msg, ...)
{
	va_list ap;
	char buf[256];

	va_start(ap, msg);
	vsnprintf(buf, sizeof(buf), msg, ap);

	fprintf(stderr, "%s: %s\n",
		opt.progname ? opt.progname : "sbatch", buf);
	va_end(ap);
}
#else
#  define argerror error
#endif				/* USE_ARGERROR */

/*
 *  Get a POSITIVE decimal integer from arg.
 *
 *  Returns the integer on success, exits program on failure.
 *
 */
static int
_get_pos_int(const char *arg, const char *what)
{
	char *p;
	long int result = strtol(arg, &p, 10);

	if (p == arg || !xstring_is_whitespace(p) || (result < 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		exit(error_exit);
	}

	if (result > INT_MAX) {
		error ("Numeric argument %ld to big for %s.", result, what);
		exit(error_exit);
	}

	return (int) result;
}

/*
 * _opt_default(): used by initialize_and_process_args to set defaults
 */
static void _opt_default()
{
	static slurm_step_io_fds_t fds = SLURM_STEP_IO_FDS_INITIALIZER;
	uid_t uid = getuid();

	opt.user = uid_to_string(uid);
	if (xstrcmp(opt.user, "nobody") == 0)
		fatal("Invalid user id: %u", uid);

	opt.uid = uid;
	opt.gid = getgid();

	opt.progname = NULL;

	opt.jobid = NO_VAL;
	opt.jobid_set = false;

	opt.quiet = 0;
	opt.verbose = 0;

	opt.euid = (uid_t) -1;
	opt.egid = (gid_t) -1;

	opt.labelio = false;
	opt.ctrl_comm_ifhn  = xshort_hostname();
	memcpy(&opt.fds, &fds, sizeof(fds));
	opt.layout_only = false;
	opt.debugger_test = false;
	opt.input_filter = (uint32_t)-1;
	opt.input_filter_set = false;
	opt.output_filter = (uint32_t)-1;
	opt.output_filter_set = false;
	opt.error_filter = (uint32_t)-1;
	opt.error_filter_set = false;
	opt.pty = false;
}

/*---[ env var processing ]-----------------------------------------------*/

/*
 * try to use a similar scheme as popt.
 *
 * in order to add a new env var (to be processed like an option):
 *
 * define a new entry into env_vars[], if the option is a simple int
 * or string you may be able to get away with adding a pointer to the
 * option to set. Otherwise, process var based on "type" in _opt_env.
 */
struct env_vars {
	const char *var;
	int type;
	void *arg;
	void *set_flag;
};

env_vars_t env_vars[] = {
  {NULL, 0, NULL, NULL}
};


/*
 * _opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend sattach to process different vars
 */
static void _opt_env(void)
{
	char       *val = NULL;
	env_vars_t *e   = env_vars;

	while (e->var) {
		if ((val = getenv(e->var)) != NULL)
			_process_env_var(e, val);
		e++;
	}
}


static void
_process_env_var(env_vars_t *e, const char *val)
{
	debug2("now processing env var %s=%s", e->var, val);

	if (e->set_flag) {
		*((bool *) e->set_flag) = true;
	}

	switch (e->type) {
	default:
		/* do nothing */
		break;
	}
}

void set_options(const int argc, char **argv)
{
	int opt_char, option_index = 0;
	static struct option long_options[] = {
		{"help", 	no_argument,       0, 'h'},
		{"label",       no_argument,       0, 'l'},
		{"quiet",       no_argument,       0, 'Q'},
		{"usage",       no_argument,       0, 'u'},
		{"verbose",     no_argument,       0, 'v'},
		{"version",     no_argument,       0, 'V'},
		{"layout",      no_argument,       0, LONG_OPT_LAYOUT_ONLY},
		{"debugger-test",no_argument,      0, LONG_OPT_DEBUGGER_TEST},
		{"input-filter", required_argument,0, LONG_OPT_IN_FILTER},
		{"output-filter",required_argument,0, LONG_OPT_OUT_FILTER},
		{"error-filter", required_argument,0, LONG_OPT_ERR_FILTER},
		{"pty",          no_argument,      0, LONG_OPT_PTY},
		{NULL}
	};
	char *opt_string = "+hlQuvV";

	opt.progname = xbasename(argv[0]);
	optind = 0;
	while((opt_char = getopt_long(argc, argv, opt_string,
				      long_options, &option_index)) != -1) {
		switch (opt_char) {

		case '?':
			fprintf(stderr, "Try \"sattach --help\" for more "
				"information\n");
			exit(error_exit);
			break;
		case 'h':
			_help();
			exit(0);
		case 'l':
			opt.labelio = true;
			break;
		case 'Q':
			opt.quiet++;
			break;
		case 'u':
			_usage();
			exit(0);
		case 'v':
			opt.verbose++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		case LONG_OPT_IN_FILTER:
			if (xstrcmp(optarg, "-") != 0) {
				opt.input_filter = (uint32_t)
					_get_pos_int(optarg, "input-filter");
			}
			opt.input_filter_set = true;
			break;
		case LONG_OPT_OUT_FILTER:
			if (xstrcmp(optarg, "-") != 0) {
				opt.output_filter = (uint32_t)
					_get_pos_int(optarg, "output-filter");
			}
			opt.output_filter_set = true;
			break;
		case LONG_OPT_ERR_FILTER:
			if (xstrcmp(optarg, "-") != 0) {
				opt.error_filter = (uint32_t)
					_get_pos_int(optarg, "error-filter");
			}
			opt.error_filter_set = true;
			break;
		case LONG_OPT_LAYOUT_ONLY:
			opt.layout_only = true;
			break;
		case LONG_OPT_DEBUGGER_TEST:
			opt.debugger_test = true;
			break;
		case LONG_OPT_PTY:
#ifdef HAVE_PTY_H
			opt.pty = true;
#else
			error("--pty not currently supported on this system "
			      "type");
#endif
			break;
		default:
			error("Unrecognized command line parameter %c",
			      opt_char);
			exit(error_exit);
		}
	}
}

static void _parse_jobid_stepid(char *jobid_str)
{
	char *ptr, *job, *step;
	long jobid, stepid;

	verbose("jobid/stepid string = %s\n", jobid_str);
	job = xstrdup(jobid_str);
	ptr = index(job, '.');
	if (ptr == NULL) {
		error("Did not find a period in the step ID string");
		_usage();
		xfree(job);
		exit(error_exit);
	} else {
		*ptr = '\0';
		step = ptr + 1;
	}

	jobid = slurm_xlate_job_id(job);
	if (jobid == 0) {
		error("\"%s\" does not look like a jobid", job);
		_usage();
		xfree(job);
		exit(error_exit);
	}

	stepid = strtol(step, &ptr, 10);
	if (!xstring_is_whitespace(ptr)) {
		error("\"%s\" does not look like a stepid", step);
		_usage();
		xfree(job);
		exit(error_exit);
	}

	opt.jobid = (uint32_t) jobid;
	opt.stepid = (uint32_t) stepid;

	xfree(job);
}

/*
 * _opt_args() : set options via commandline args and popt
 */
static void _opt_args(int argc, char **argv)
{
	char **rest = NULL;
	int leftover;

	set_options(argc, argv);

	leftover = 0;
	if (optind < argc) {
		rest = argv + optind;
		while (rest[leftover] != NULL)
			leftover++;
	}
	if (leftover != 1) {
		error("too many parameters");
		_usage();
		exit(error_exit);
	}

	_parse_jobid_stepid(*(argv + optind));

	if (!_opt_verify())
		exit(error_exit);
}

/*
 * _opt_verify : perform some post option processing verification
 *
 */
static bool _opt_verify(void)
{
	bool verified = true;

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	/*
	 * set up standard IO filters
	 */
	if ((opt.input_filter_set || opt.output_filter_set ||
	     opt.error_filter_set) && opt.pty) {
		error("don't specify both --pty and I/O filtering");
		verified = false;
	}
	if (opt.input_filter_set)
		opt.fds.input.taskid = opt.input_filter;
	if (opt.output_filter_set)
		opt.fds.out.taskid = opt.output_filter;
	if (opt.error_filter_set) {
		opt.fds.err.taskid = opt.error_filter;
	} else if (opt.output_filter_set) {
		opt.fds.err.taskid = opt.output_filter;
	}


	return verified;
}

#define tf_(b) (b == true) ? "true" : "false"

static void _opt_list()
{
	info("defined options for program `%s'", opt.progname);
	info("--------------- ---------------------");
	info("job ID         : %u", opt.jobid);
	info("step ID        : %u", opt.stepid);
	info("user           : `%s'", opt.user);
	info("uid            : %ld", (long) opt.uid);
	info("gid            : %ld", (long) opt.gid);
	info("verbose        : %d", opt.verbose);
}

static void _usage(void)
{
 	printf("Usage: sattach [options] <jobid.stepid>\n");
}

static void _help(void)
{
        printf("Usage: sattach [options] <jobid.stepid>\n");
	printf(
"      --input-filter=taskid  send stdin to only the specified task\n"
"      --output-filter=taskid only print stdout from the specified task\n"
"      --error-filter=taskid  only print stderr from the specified task\n"
"  -l, --label        prepend task number to lines of stdout & stderr\n"
"      --layout       print task layout info and exit (does not attach to tasks)\n"
"  -Q, --quiet        quiet mode (suppress informational messages)\n"
"  -v, --verbose      verbose mode (multiple -v's increase verbosity)\n"
"  -V, --version      print the SLURM version and exit\n\n"
"Help options:\n"
"  -h, --help         print this help message\n"
"  -u, --usage        print a brief usage message\n"

		);
}
