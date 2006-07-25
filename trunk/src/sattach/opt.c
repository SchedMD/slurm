/*****************************************************************************\
 *  opt.c - options processing for sbatch
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
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

#include <string.h>		/* strcpy, strncasecmp */

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <fcntl.h>
#include <stdarg.h>		/* va_start   */
#include <stdio.h>
#include <stdlib.h>		/* getenv     */
#include <pwd.h>		/* getpwuid   */
#include <ctype.h>		/* isdigit    */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/read_config.h" /* contains getnodename() */

#include "src/sbatch/opt.h"

#include "src/common/mpi.h"

/*---- global variables, defined in opt.h ----*/
char **remote_argv;
int remote_argc;
int _verbose;
opt_t opt;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

static List  _create_path_list(void);

/* Get a decimal integer from arg */
static int  _get_int(const char *arg, const char *what);

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

static void  _print_version(void);

static void _process_env_var(env_vars_t *e, const char *val);

static uint16_t _parse_mail_type(const char *arg);

/* search PATH for command returns full path */
static char *_search_path(char *, bool, int);

static void  _usage(void);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char *argv[])
{
	/* initialize option defaults */
	_opt_default();

	/* initialize options with env vars */
	_opt_env();

	/* initialize options with argv */
	_opt_args(argc, argv);

	if (_verbose > 3)
		_opt_list();

	return 1;

}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

/*
 * print error message to stderr with opt.progname prepended
 */
#undef USE_ARGERROR
#if USE_ARGERROR
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
 * _opt_default(): used by initialize_and_process_args to set defaults
 */
static void _opt_default()
{
	char buf[MAXPATHLEN + 1];
	struct passwd *pw;
	int i;

	if ((pw = getpwuid(getuid())) != NULL) {
		strncpy(opt.user, pw->pw_name, MAX_USERNAME);
		opt.uid = pw->pw_uid;
	} else
		error("who are you?");

	opt.gid = getgid();

	if ((getcwd(buf, MAXPATHLEN)) == NULL) 
		fatal("getcwd failed: %m");
	opt.cwd = xstrdup(buf);

	opt.progname = NULL;

	opt.nprocs = 1;
	opt.nprocs_set = false;
	opt.cpus_per_task = 1; 
	opt.cpus_set = false;
	opt.min_nodes = 1;
	opt.max_nodes = 0;
	opt.nodes_set = false;
	opt.cpu_bind_type = 0;
	opt.cpu_bind = NULL;
	opt.mem_bind_type = 0;
	opt.mem_bind = NULL;
	opt.time_limit = -1;
	opt.partition = NULL;

	opt.job_name = NULL;
	opt.jobid    = NO_VAL;
	opt.jobid_set = false;
	opt.dependency = NO_VAL;
	opt.account  = NULL;

	opt.distribution = SLURM_DIST_CYCLIC;

	opt.overcommit = false;
	opt.share = false;
	opt.no_kill = false;
	opt.kill_bad_exit = false;

	opt.immediate	= false;
	opt.no_requeue	= false;

	opt.noshell	= false;
	opt.max_wait	= slurm_get_wait_time();

	opt.quit_on_intr = false;
	opt.disable_status = false;
	opt.test_only   = false;

	opt.quiet = 0;
	_verbose = 0;

	/* constraint default (-1 is no constraint) */
	opt.mincpus	    = -1;
	opt.realmem	    = -1;
	opt.tmpdisk	    = -1;

	opt.hold	    = false;
	opt.constraints	    = NULL;
	opt.contiguous	    = false;
        opt.exclusive       = false;
	opt.nodelist	    = NULL;
	opt.exc_nodes	    = NULL;
	opt.max_launch_time = 120;/* 120 seconds to launch job             */
	opt.max_exit_timeout= 60; /* Warn user 60 seconds after task exit */
	opt.msg_timeout     = 5;  /* Default launch msg timeout           */

	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		opt.geometry[i]	    = (uint16_t) NO_VAL;
	opt.no_rotate	    = false;
	opt.conn_type	    = -1;

	opt.euid	    = (uid_t) -1;
	opt.egid	    = (gid_t) -1;
	
	opt.propagate	    = NULL;  /* propagate specific rlimits */

	opt.task_prolog     = NULL;
	opt.task_epilog     = NULL;

	opt.ctrl_comm_ifhn  = xshort_hostname();

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
 *            extend srun to process different vars
 */
static void _opt_env()
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
	char *end = NULL;
	enum task_dist_states dt;

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

/*
 *  Get a decimal integer from arg.
 *
 *  Returns the integer on success, exits program on failure.
 * 
 */
static int
_get_int(const char *arg, const char *what)
{
	char *p;
	long int result = strtol(arg, &p, 10);

	if ((*p != '\0') || (result < 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		exit(1);
	}

	if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
	}

	return (int) result;
}

void set_options(const int argc, char **argv, int first)
{
	int opt_char, option_index = 0;
	static struct option long_options[] = {
		{"help",          no_argument,       0, 'h'},
		{"quiet",         no_argument,       0, 'Q'},
		{"verbose",       no_argument,       0, 'v'},
		{"version",       no_argument,       0, 'V'},
		{NULL}
	};
	char *opt_string = "+hQvV";

	if(opt.progname == NULL)
		opt.progname = xbasename(argv[0]);
	else if(!first)
		argv[0] = opt.progname;
	else
		error("opt.progname is set but it is the first time through.");
	optind = 0;		
	while((opt_char = getopt_long(argc, argv, opt_string,
				      long_options, &option_index)) != -1) {
		switch (opt_char) {
			
		case (int)'?':
			if(first) {
				fprintf(stderr, "Try \"sbatch --help\" for more "
					"information\n");
				exit(1);
			} 
			break;
		case (int)'h':
			_help();
			exit(0);
		case (int) 'Q':
			if(!first && opt.quiet)
				break;
			
			opt.quiet++;
			break;
		case (int)'v':
			if(!first && _verbose)
				break;
			
			_verbose++;
			break;
		case (int)'V':
			_print_version();
			exit(0);
			break;
		default:
			fatal("Unrecognized command line parameter %c",
			      opt_char);
		}
	}

	if (!first) {
		if (!_opt_verify())
			exit(1);
		if (_verbose > 3)
			_opt_list();
	}
}

/*
 * _opt_args() : set options via commandline args and popt
 */
static void _opt_args(int argc, char **argv)
{
	int i;
	char **rest = NULL;

	set_options(argc, argv, 1);	

#ifdef HAVE_AIX
	if (opt.network == NULL) {
		opt.network = "us,sn_all,bulk_xfer";
		setenv("SLURM_NETWORK", opt.network, 1);
	}
#endif

	remote_argc = 0;
	if (optind < argc) {
		rest = argv + optind;
		while (rest[remote_argc] != NULL)
			remote_argc++;
	}
	remote_argv = (char **) xmalloc((remote_argc + 1) * sizeof(char *));
	for (i = 0; i < remote_argc; i++)
		remote_argv[i] = xstrdup(rest[i]);
	remote_argv[i] = NULL;	/* End of argv's (for possible execv) */

	if (opt.multi_prog) {
		if (remote_argc < 1) {
			error("configuration file not specified");
			exit(1);
		}
		_load_multi(&remote_argc, remote_argv);

	}
	else if (remote_argc > 0) {
		char *fullpath;
		char *cmd       = remote_argv[0];
		int  mode       = R_OK;

		if ((fullpath = _search_path(cmd, true, mode))) {
			xfree(remote_argv[0]);
			remote_argv[0] = fullpath;
		} 
	}
	if (!_opt_verify())
		exit(1);
}

/* 
 * _opt_verify : perform some post option processing verification
 *
 */
static bool _opt_verify(void)
{
	bool verified = true;

	if (opt.quiet && _verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	return verified;
}

#define tf_(b) (b == true) ? "true" : "false"

static void _opt_list()
{
	char *str;

	info("defined options for program `%s'", opt.progname);
	info("--------------- ---------------------");

	info("user           : `%s'", opt.user);
	info("uid            : %ld", (long) opt.uid);
	info("gid            : %ld", (long) opt.gid);
	info("verbose        : %d", _verbose);
	str = print_commandline();
	xfree(str);

}

static void _usage(void)
{
 	printf("Usage: sattach <jobid.stepid>\n");
}

static void _help(void)
{
        printf("Usage: sattach <jobid.stepid>\n");
	printf(
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -Q, --quiet                 quiet mode (suppress informational messages)\n"
		);
	printf("\n");

	printf(
"Help options:\n"
"      -h, --help              show this help message\n"
"      --usage                 display brief usage message\n"
"\n"
"Other options:\n"
"  -V, --version               output version information and exit\n"
"\n"
);

}
