/*****************************************************************************\
 *  opt.c - options processing for srun
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
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

#include <getopt.h>
#include <stdarg.h>		/* va_start   */
#include <stdlib.h>		/* getenv     */
#include <pwd.h>		/* getpwuid   */
#include <ctype.h>		/* isdigit    */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/srun/env.h"
#include "src/srun/opt.h"

#ifdef HAVE_TOTALVIEW
#  include "src/srun/attach.h"

/*
 *  Instantiate extern variables from attach.h
 */
   MPIR_PROCDESC *MPIR_proctable;
   int MPIR_proctable_size;
   VOLATILE int MPIR_debug_state;
   VOLATILE int MPIR_debug_gate;
   int MPIR_being_debugged;
   int MPIR_i_am_starter;
   int MPIR_acquired_pre_main;
   char *totalview_jobid;
#endif

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_DEBUG       0x03
#define OPT_DISTRIB     0x04
#define OPT_NODES       0x05
#define OPT_OVERCOMMIT  0x06

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_HELP     0x100
#define LONG_OPT_USAGE    0x101
#define LONG_OPT_XTO      0x102
#define LONG_OPT_LAUNCH   0x103
#define LONG_OPT_TIMEO    0x104
#define LONG_OPT_JOBID    0x105
#define LONG_OPT_TMP      0x106
#define LONG_OPT_MEM      0x107
#define LONG_OPT_MINCPU   0x108
#define LONG_OPT_CONT     0x109

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

/* return command name from its full path name */
static char * _base_name(char* command);

static List  _create_path_list(void);

/* Get a decimal integer from arg */
static int  _get_int(const char *arg, const char *what);

static void  _help(void);

/* set options based upon commandline args */
static void _opt_args(int, char **);

/* fill in default options  */
static void _opt_default(void);

/* set options based upon env vars  */
static void _opt_env(void);

/* list known options and their settings  */
static void  _opt_list(void);

/* verify options sanity  */
static bool _opt_verify(void);

static void  _print_version(void);

static void _process_env_var(env_vars_t *e, const char *val);

/* search PATH for command returns full path */
static char *_search_path(char *, bool, int);

static long  _to_bytes(const char *arg);

#ifdef HAVE_TOTALVIEW
   static bool _under_totalview(void);
#endif

static void  _usage(void);
static bool  _valid_node_list(char **node_list_pptr);
static enum  distribution_t _verify_dist_type(const char *arg);
static bool  _verify_node_count(const char *arg, int *min, int *max);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char *argv[])
{
	/* initialize option defaults */
	_opt_default();

	/* initialize options with env vars */
	_opt_env();

	/* initialize options with argv */
	_opt_args(argc, argv);

	if (_verbose > 2)
		_opt_list();

	return 1;

}

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

/*
 * If the node list supplied is a file name, translate that into 
 *	a list of nodes, we orphan the data pointed to
 * RET true if the node list is a valid one
 */
static bool _valid_node_list(char **node_list_pptr)
{
	FILE *fd;
	char *node_list;
	int c;
	bool last_space;

	if (strchr(*node_list_pptr, '/') == NULL)
		return true;	/* not a file name */

	fd = fopen(*node_list_pptr, "r");
	if (fd == NULL) {
		error ("Unable to open file %s: %m", *node_list_pptr);
		return false;
	}

	node_list = xstrdup("");
	last_space = false;
	while ((c = fgetc(fd)) != EOF) {
		if (isspace(c)) {
			last_space = true;
			continue;
		}
		if (last_space && (node_list[0] != '\0'))
			xstrcatchar(node_list, ',');
		last_space = false;
		xstrcatchar(node_list, (char)c);
	}
	(void) fclose(fd);

        /*  free(*node_list_pptr);	orphanned */
	*node_list_pptr = node_list;
	return true;
}

/* 
 * verify that a distribution type in arg is of a known form
 * returns the distribution_t or SRUN_DIST_UNKNOWN
 */
static enum distribution_t _verify_dist_type(const char *arg)
{
	enum distribution_t result = SRUN_DIST_UNKNOWN;

	if (strncasecmp(arg, "cyclic", strlen(arg)) == 0)
		result = SRUN_DIST_CYCLIC;
	else if (strncasecmp(arg, "block", strlen(arg)) == 0)
		result = SRUN_DIST_BLOCK;

	return result;
}

/* 
 * verify that a node count in arg is of a known form (count or min-max)
 * OUT min, max specified minimum and maximum node counts
 * RET true if valid
 */
static bool 
_verify_node_count(const char *arg, int *min_nodes, int *max_nodes)
{
	char *end_ptr;
	int val1, val2;

	val1 = strtol(arg, &end_ptr, 10);
	if (end_ptr[0] == '\0') {
		*min_nodes = val1;
		return true;
	}

	if (end_ptr[0] != '-')
		return false;

	val2 = strtol(&end_ptr[1], &end_ptr, 10);
	if (end_ptr[0] == '\0') {
		*min_nodes = val1;
		*max_nodes = val2;
		return true;
	} else
		return false;

}

/* return command name from its full path name */
static char * _base_name(char* command)
{
	char *char_ptr, *name;
	int i;

	if (command == NULL)
		return NULL;

	char_ptr = strrchr(command, (int)'/');
	if (char_ptr == NULL)
		char_ptr = command;
	else
		char_ptr++;

	i = strlen(char_ptr);
	name = xmalloc(i+1);
	strcpy(name, char_ptr);
	return name;
}

/*
 * _to_bytes(): verify that arg is numeric with optional "G" or "M" at end
 * if "G" or "M" is there, multiply by proper power of 2 and return
 * number in bytes
 */
static long _to_bytes(const char *arg)
{
	char *buf;
	char *endptr;
	int end;
	int multiplier = 1;
	long result;

	buf = xstrdup(arg);

	end = strlen(buf) - 1;

	if (isdigit(buf[end])) {
		result = strtol(buf, &endptr, 10);

		if (*endptr != '\0')
			result = -result;

	} else {

		switch (toupper(buf[end])) {

		case 'G':
			multiplier = 1024;
			break;

		case 'M':
			/* do nothing */
			break;

		default:
			multiplier = -1;
		}

		buf[end] = '\0';

		result = multiplier * strtol(buf, &endptr, 10);

		if (*endptr != '\0')
			result = -result;
	}

	return result;
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
		opt.progname ? opt.progname : "srun", buf);
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

	if ((pw = getpwuid(getuid())) != NULL) {
		strncpy(opt.user, pw->pw_name, MAX_USERNAME);
		opt.uid = pw->pw_uid;
	} else
		error("who are you?");


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
	opt.time_limit = -1;
	opt.partition = NULL;
	opt.max_threads = MAX_THREADS;

	opt.job_name = NULL;
	opt.jobid    = NO_VAL;

	opt.distribution = SRUN_DIST_UNKNOWN;

	opt.ofname = NULL;
	opt.ifname = NULL;
	opt.efname = NULL;

	opt.core_format = "normal";

	opt.labelio = false;
	opt.unbuffered = false;
	opt.overcommit = false;
	opt.batch = false;
	opt.share = false;
	opt.no_kill = false;

	opt.immediate	= false;

	opt.allocate	= false;
	opt.attach	= NULL;
	opt.join	= false;
	opt.max_wait	= slurm_get_wait_time();

	_verbose = 0;
	opt.slurmd_debug = LOG_LEVEL_QUIET;

	/* constraint default (-1 is no constraint) */
	opt.mincpus	    = -1;
	opt.realmem	    = -1;
	opt.tmpdisk	    = -1;

	opt.hold	    = false;
	opt.constraints	    = NULL;
	opt.contiguous	    = false;
	opt.nodelist	    = NULL;
	opt.exc_nodes	    = NULL;
	opt.max_launch_time = 60; /* 60 seconds to launch job             */
	opt.max_exit_timeout= 60; /* Warn user 60 seconds after task exit */
	opt.msg_timeout     = 5;  /* Default launch msg timeout           */

	mode	= MODE_NORMAL;

#ifdef HAVE_TOTALVIEW
	/*
	 * Reset some default values if running under TotalView:
	 */
	if ((opt.totalview = _under_totalview())) {
		opt.max_launch_time = 120;
		opt.max_threads     = 1;
		opt.msg_timeout     = 15;
	}

#endif

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
	{"SLURM_JOBID",         OPT_INT, &opt.jobid,         NULL           },
	{"SLURM_NPROCS",        OPT_INT, &opt.nprocs,        &opt.nprocs_set},
	{"SLURM_CPUS_PER_TASK", OPT_INT, &opt.cpus_per_task, &opt.cpus_set  },
	{"SLURM_PARTITION",     OPT_STRING,  &opt.partition, NULL           },
	{"SLURM_IMMEDIATE",     OPT_INT,     &opt.immediate, NULL           },
	{"SLURM_DEBUG",         OPT_DEBUG,       NULL,       NULL           },
	{"SLURMD_DEBUG",        OPT_INT, &opt.slurmd_debug,  NULL           }, 
	{"SLURM_NNODES",        OPT_NODES,       NULL,       NULL           },
	{"SLURM_OVERCOMMIT",    OPT_OVERCOMMIT,  NULL,       NULL           },
	{"SLURM_DISTRIBUTION",  OPT_DISTRIB,     NULL,       NULL           },
	{"SLURM_WAIT",          OPT_INT,     &opt.max_wait,  NULL           },
	{"SLURM_STDINMODE",     OPT_STRING,  &opt.ifname,    NULL           },
	{"SLURM_STDERRMODE",    OPT_STRING,  &opt.efname,    NULL           },
	{"SLURM_STDOUTMODE",    OPT_STRING,  &opt.ofname,    NULL           },
	{"SLURM_TIMELIMIT",     OPT_INT,     &opt.time_limit,NULL           },
	{"SLURM_LABELIO",       OPT_INT,     &opt.labelio,   NULL           },
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
	enum distribution_t dt;

	debug2("now processing env var %s=%s", e->var, val);

	if (e->set_flag) {
		*((bool *) e->set_flag) = true;
	}

	switch (e->type) {
	case OPT_STRING:
	    *((char **) e->arg) = xstrdup(val);
	    break;
	case OPT_INT:
	    if (val != NULL) {
		    *((int *) e->arg) = (int) strtol(val, &end, 10);
		    if (!(end && *end == '\0')) 
			    error("%s=%s invalid. ignoring...", e->var, val);
	    }
	    break;

	case OPT_DEBUG:
	    if (val != NULL) {
		    _verbose = (int) strtol(val, &end, 10);
		    if (!(end && *end == '\0')) 
			    error("%s=%s invalid", e->var, val);
	    }
	    break;

	case OPT_DISTRIB:
	    dt = _verify_dist_type(val);
	    if (dt == SRUN_DIST_UNKNOWN) {
		    error("\"%s=%s\" -- invalid distribution type. " 
		          "ignoring...", e->var, val);
	    } else 
		    opt.distribution = dt;
	    break;

	case OPT_NODES:
	    opt.nodes_set = _verify_node_count( val, 
			                        &opt.min_nodes, 
					        &opt.max_nodes );
	    if (opt.nodes_set == false) {
		    error("\"%s=%s\" -- invalid node count. ignoring...",
			  e->var, val);
	    }
	    break;

	case OPT_OVERCOMMIT:
	    opt.overcommit = true;
	    break;
	    
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

/*
 * _opt_args() : set options via commandline args and popt
 */
static void _opt_args(int argc, char **argv)
{
	int opt_char, i;
	int option_index;
	static struct option long_options[] = {
		{"attach",        required_argument, 0, 'a'},
		{"allocate",      no_argument,       0, 'A'},
		{"batch",         no_argument,       0, 'b'},
		{"cpus-per-task", required_argument, 0, 'c'},
		{"constraint",    required_argument, 0, 'C'},
		{"slurmd-debug",  required_argument, 0, 'd'},
		{"chdir",         required_argument, 0, 'D'},
		{"error",         required_argument, 0, 'e'},
		{"hold",          no_argument,       0, 'H'},
		{"input",         required_argument, 0, 'i'},
		{"immediate",     no_argument,       0, 'I'},
		{"join",          no_argument,       0, 'j'},
		{"job-name",      required_argument, 0, 'J'},
		{"no-kill",       no_argument,       0, 'k'},
		{"label",         no_argument,       0, 'l'},
		{"distribution",  required_argument, 0, 'm'},
		{"ntasks",        required_argument, 0, 'n'},
		{"nodes",         required_argument, 0, 'N'},
		{"output",        required_argument, 0, 'o'},
		{"overcommit",    no_argument,       0, 'O'},
		{"partition",     required_argument, 0, 'p'},
		{"relative",      required_argument, 0, 'r'},
		{"share",         no_argument,       0, 's'},
		{"time",          required_argument, 0, 't'},
		{"threads",       required_argument, 0, 'T'},
		{"unbuffered",    no_argument,       0, 'u'},
		{"verbose",       no_argument,       0, 'v'},
		{"version",       no_argument,       0, 'V'},
		{"nodelist",      required_argument, 0, 'w'},
		{"wait",          required_argument, 0, 'W'},
		{"exclude",       required_argument, 0, 'x'},
		{"no-allocate",   no_argument,       0, 'Z'},

		{"contiguous",       no_argument,       0, LONG_OPT_CONT},
		{"mincpus",          required_argument, 0, LONG_OPT_MINCPU},
		{"mem",              required_argument, 0, LONG_OPT_MEM},
		{"tmp",              required_argument, 0, LONG_OPT_TMP},
		{"jobid",            required_argument, 0, LONG_OPT_JOBID},
		{"msg-timeout",      required_argument, 0, LONG_OPT_TIMEO},
		{"max-launch-time",  required_argument, 0, LONG_OPT_LAUNCH},
		{"max-exit-timeout", required_argument, 0, LONG_OPT_XTO},
		{"help",             no_argument,       0, LONG_OPT_HELP},
		{"usage",            no_argument,       0, LONG_OPT_USAGE}
	};
	char *opt_string = "+a:Abc:C:d:D:e:Hi:IjJ:klm:n:N:"
		"o:Op:r:st:T:uvVw:W:x:Z";
	char **rest = NULL;

	opt.progname = xbasename(argv[0]);

	while((opt_char = getopt_long(argc, argv, opt_string,
			long_options, &option_index)) != -1) {
		switch (opt_char) {
			case (int)'?':
				fprintf(stderr, "Try \"srun --help\" for more "
					"information\n");
				exit(1);
				break;
			case (int)'a':
				if (opt.allocate || opt.batch) {
					error("can only specify one mode: "
					      "allocate, attach or batch.");
					exit(1);
				}
				mode = MODE_ATTACH;
				opt.attach = strdup(optarg);
				break;
			case (int)'A':
				if (opt.attach || opt.batch) {
					error("can only specify one mode: "
				   	   "allocate, attach or batch.");
					exit(1);
				}
				mode = MODE_ALLOCATE;
				opt.allocate = true;
				break;
			case (int)'b':
				if (opt.allocate || opt.attach) {
					error("can only specify one mode: "
					      "allocate, attach or batch.");
					exit(1);
				}
				mode = MODE_BATCH;
				opt.batch = true;
				break;
			case (int)'c':
				opt.cpus_set = true;
				opt.cpus_per_task = 
					_get_int(optarg, "cpus-per-task");
				break;
			case (int)'C':
				xfree(opt.constraints);
				opt.constraints = xstrdup(optarg);
				break;
			case (int)'d':
				opt.slurmd_debug = 
					_get_int(optarg, "slurmd-debug");
				break;
			case (int)'D':
				xfree(opt.cwd);
				opt.cwd = xstrdup(optarg);
				break;
			case (int)'e':
				xfree(opt.efname);
				opt.efname = xstrdup(optarg);
				break;
			case (int)'H':
				opt.hold = true;
				break;
			case (int)'i':
				xfree(opt.ifname);
				opt.ifname = xstrdup(optarg);
				break;
			case (int)'I':
				opt.immediate = true;
				break;
			case (int)'j':
				opt.join = true;
				break;
			case (int)'J':
				xfree(opt.job_name);
				opt.job_name = xstrdup(optarg);
				break;
			case (int)'k':
				opt.no_kill = true;
				break;
			case (int)'l':
				opt.labelio = true;
				break;
			case (int)'m':
				opt.distribution = _verify_dist_type(optarg);
				if (opt.distribution == SRUN_DIST_UNKNOWN) {
					error("distribution type `%s' " 
				  	    "is not recognized", optarg);
					exit(1);
				}
				break;
			case (int)'n':
				opt.nprocs_set = true;
				opt.nprocs = 
					_get_int(optarg, "number of tasks");
				break;
			case (int)'N':
				opt.nodes_set = 
					_verify_node_count(optarg, 
						&opt.min_nodes,
						&opt.max_nodes);
				if (opt.nodes_set == false) {
					error("invalid node count `%s'", 
						optarg);
					exit(1);
				}
				break;
			case (int)'o':
				xfree(opt.ofname);
				opt.ofname = xstrdup(optarg);
				break;
			case (int)'O':
				opt.overcommit = true;
				break;
			case (int)'p':
				xfree(opt.partition);
				opt.partition = xstrdup(optarg);
				break;
			case (int)'r':
				xfree(opt.relative);
				opt.relative = xstrdup(optarg);
				break;
			case (int)'s':
				opt.share = true;
				break;
			case (int)'t':
				opt.time_limit = _get_int(optarg, "time");
				break;
			case (int)'T':
				opt.max_threads = 
					_get_int(optarg, "max_threads");
				break;
			case (int)'u':
				opt.unbuffered = true;
				break;
			case (int)'v':
				_verbose++;
				break;
			case (int)'V':
				_print_version();
				exit(0);
				break;
			case (int)'w':
				xfree(opt.nodelist);
				opt.nodelist = xstrdup(optarg);
				if (!_valid_node_list(&opt.nodelist))
					exit(1);
				break;
			case (int)'W':
				opt.max_wait = _get_int(optarg, "wait");
				break;
			case (int)'x':
				xfree(opt.exc_nodes);
				opt.exc_nodes = xstrdup(optarg);
				if (!_valid_node_list(&opt.exc_nodes))
					exit(1);
				break;
			case (int)'Z':
				opt.no_alloc = true;
				break;
			case LONG_OPT_CONT:
				opt.contiguous = true;
				break;
			case LONG_OPT_MINCPU:
				opt.mincpus = _get_int(optarg, "mincpus");
				break;
			case LONG_OPT_MEM:
				opt.realmem = (int) _to_bytes(optarg);
				if (opt.realmem < 0) {
					error("invalid memory constraint %s", 
						optarg);
					exit(1);
				}
				break;
			case LONG_OPT_TMP:
				opt.tmpdisk = _to_bytes(optarg);
				if (opt.tmpdisk < 0) {
					error("invalid tmp value %s", optarg);
					exit(1);
				}
				break;
			case LONG_OPT_JOBID:
				opt.jobid = _get_int(optarg, "jobid");
				break;
			case LONG_OPT_TIMEO:
				opt.msg_timeout = 
					_get_int(optarg, "msg-timeout");
				break;
			case LONG_OPT_LAUNCH:
				opt.max_launch_time = 
					_get_int(optarg, "max-launch-time");
				break;
			case LONG_OPT_XTO:
				opt.max_exit_timeout = 
					_get_int(optarg, "max-exit-timeout");
				break;
			case LONG_OPT_HELP:
				_help();
				exit(0);
			case LONG_OPT_USAGE:
				_usage();
				exit(0);
		}
	}

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

	if (remote_argc > 0) {
		char *fullpath;
		char *cmd       = remote_argv[0];
		bool search_cwd = (opt.batch || opt.allocate);
		int  mode       = (search_cwd) ? R_OK : R_OK | X_OK;

		if ((fullpath = _search_path(cmd, search_cwd, mode))) {
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

	if (opt.slurmd_debug + LOG_LEVEL_ERROR > LOG_LEVEL_DEBUG2)
		opt.slurmd_debug = LOG_LEVEL_DEBUG2 - LOG_LEVEL_ERROR;

	if (opt.no_alloc && !opt.nodelist) {
		error("must specify a node list with -Z, --no-allocate.");
		verified = false;
	}

	if (opt.no_alloc && opt.exc_nodes) {
		error("can not specify --exclude list with -Z, --no-allocate.");
		verified = false;
	}

	if (opt.no_alloc && opt.relative) {
		error("do not specify -r,--relative with -Z,--no-allocate.");
		verified = false;
	}

	if (opt.relative && (opt.exc_nodes || opt.nodelist)) {
		error("-r,--relative not allowed with "
		      "-w,--nodelist or -x,--exclude.");
		verified = false;
	}

	if (opt.mincpus < opt.cpus_per_task)
		opt.mincpus = opt.cpus_per_task;

	if ((opt.job_name == NULL) && (remote_argc > 0))
		opt.job_name = _base_name(remote_argv[0]);

	if (mode == MODE_ATTACH) {	/* attach to a running job */
		if (opt.nodes_set || opt.cpus_set || opt.nprocs_set) {
			error("do not specific a node allocation "
			      "with --attach (-a)");
			verified = false;
		}

		/* if (constraints_given()) {
		 *	error("do not specify any constraints with "
		 *	      "--attach (-a)");
		 *	verified = false;
		 *}
		 */

	} else { /* mode != MODE_ATTACH */

		if ((remote_argc == 0) && (mode != MODE_ALLOCATE)) {
			error("must supply remote command");
			verified = false;
		}


		/* check for realistic arguments */
		if (opt.nprocs <= 0) {
			error("%s: invalid number of processes (-n %d)",
				opt.progname, opt.nprocs);
			verified = false;
		}

		if (opt.cpus_per_task <= 0) {
			error("%s: invalid number of cpus per task (-c %d)\n",
				opt.progname, opt.cpus_per_task);
			verified = false;
		}

		if ((opt.min_nodes <= 0) || (opt.max_nodes < 0) || 
		    (opt.max_nodes && (opt.min_nodes > opt.max_nodes))) {
			error("%s: invalid number of nodes (-N %d-%d)\n",
			      opt.progname, opt.min_nodes, opt.max_nodes);
			verified = false;
		}

		/* massage the numbers */
		if (opt.nodes_set && !opt.nprocs_set) {
			/* 1 proc / node default */
			opt.nprocs = opt.min_nodes;

		} else if (opt.nodes_set && opt.nprocs_set) {

			/* 
			 *  make sure # of procs >= min_nodes 
			 */
			if (opt.nprocs < opt.min_nodes) {

				info ("Warning: can't run %d processes on %d " 
				      "nodes, setting nnodes to %d", 
				      opt.nprocs, opt.min_nodes, opt.nprocs);

				opt.min_nodes = opt.nprocs;
				if (   opt.max_nodes 
				   && (opt.min_nodes > opt.max_nodes) )
					opt.max_nodes = opt.min_nodes;
			}

		} /* else if (opt.nprocs_set && !opt.nodes_set) */

	}

	if (opt.max_threads <= 0) {	/* set default */
		error("Thread value invalid, reset to 1");
		opt.max_threads = 1;
	} else if (opt.max_threads > MAX_THREADS) {
		error("Thread value exceeds defined limit, reset to %d", 
			MAX_THREADS);
		opt.max_threads = MAX_THREADS;
	}

	if (opt.labelio && opt.unbuffered) {
		error("Do not specify both -l (--label) and " 
		      "-u (--unbuffered)");
		exit(1);
	}

	/*
	 * --wait always overrides hidden max_exit_timeout
	 */
	if (opt.max_wait)
		opt.max_exit_timeout = opt.max_wait;

	return verified;
}

static void
_freeF(void *data)
{
	xfree(data);
}

static List
_create_path_list(void)
{
	List l = list_create(&_freeF);
	char *path = xstrdup(getenv("PATH"));
	char *c, *lc;

	if (!path) {
		error("Error in PATH environment variable");
		list_destroy(l);
		return NULL;
	}

	c = lc = path;

	while (*c != '\0') {
		if (*c == ':') {
			/* nullify and push token onto list */
			*c = '\0';
			if (lc != NULL && strlen(lc) > 0)
				list_append(l, xstrdup(lc));
			lc = ++c;
		} else
			c++;
	}

	if (strlen(lc) > 0)
		list_append(l, xstrdup(lc));

	xfree(path);

	return l;
}

static char *
_search_path(char *cmd, bool check_current_dir, int access_mode)
{
	List         l        = _create_path_list();
	ListIterator i        = NULL;
	char *path, *fullpath = NULL;

	if (  (cmd[0] == '.' || cmd[0] == '/') 
           && (access(cmd, access_mode) == 0 ) ) {
		if (cmd[0] == '.')
			xstrfmtcat(fullpath, "%s/", opt.cwd);
		xstrcat(fullpath, cmd);
		goto done;
	}

	if (check_current_dir) 
		list_prepend(l, xstrdup(opt.cwd));

	i = list_iterator_create(l);
	while ((path = list_next(i))) {
		xstrfmtcat(fullpath, "%s/%s", path, cmd);

		if (access(fullpath, access_mode) == 0)
			goto done;

		xfree(fullpath);
		fullpath = NULL;
	}
  done:
	list_destroy(l);
	return fullpath;
}

/* helper function for printing options
 * 
 * warning: returns pointer to memory allocated on the stack.
 */
static char *print_constraints()
{
	char *buf = xstrdup("");

	if (opt.mincpus > 0)
		xstrfmtcat(buf, "mincpus=%d ", opt.mincpus);

	if (opt.realmem > 0)
		xstrfmtcat(buf, "mem=%dM ", opt.realmem);

	if (opt.tmpdisk > 0)
		xstrfmtcat(buf, "tmp=%ld ", opt.tmpdisk);

	if (opt.contiguous == true)
		xstrcat(buf, "contiguous ");

	if (opt.nodelist != NULL)
		xstrfmtcat(buf, "nodelist=%s ", opt.nodelist);

	if (opt.exc_nodes != NULL)
		xstrfmtcat(buf, "exclude=%s ", opt.exc_nodes);

	if (opt.constraints != NULL)
		xstrfmtcat(buf, "constraints=`%s' ", opt.constraints);

	return buf;
}

static char * 
print_commandline()
{
	int i;
	char buf[256];

	buf[0] = '\0';
	for (i = 0; i < remote_argc; i++)
		snprintf(buf, 256,  "%s", remote_argv[i]);
	return xstrdup(buf);
}

#define tf_(b) (b == true) ? "true" : "false"

static void _opt_list()
{
	char *str;

	info("defined options for program `%s'", opt.progname);
	info("--------------- ---------------------");

	info("user           : `%s'", opt.user);
	info("uid            : %ld", (long) opt.uid);
	info("cwd            : %s", opt.cwd);
	info("nprocs         : %d", opt.nprocs);
	info("cpus_per_task  : %d", opt.cpus_per_task);
	if (opt.max_nodes)
		info("nodes          : %d-%d", opt.min_nodes, opt.max_nodes);
	else
		info("nodes          : %d", opt.min_nodes);
	info("partition      : %s",
	     opt.partition == NULL ? "default" : opt.partition);
	info("job name       : `%s'", opt.job_name);
	info("distribution   : %s", format_distribution_t(opt.distribution));
	info("core format    : %s", opt.core_format);
	info("verbose        : %d", _verbose);
	info("slurmd_debug   : %d", opt.slurmd_debug);
	info("immediate      : %s", tf_(opt.immediate));
	info("label output   : %s", tf_(opt.labelio));
	info("unbuffered IO  : %s", tf_(opt.unbuffered));
	info("allocate       : %s", tf_(opt.allocate));
	info("attach         : `%s'", opt.attach);
	info("overcommit     : %s", tf_(opt.overcommit));
	info("batch          : %s", tf_(opt.batch));
	info("threads        : %d", opt.max_threads);
	info("wait           : %d", opt.max_wait);
	str = print_constraints();
	info("constraints    : %s", str);
	xfree(str);
	str = print_commandline();
	info("remote command : `%s'", str);
	xfree(str);

}

#ifdef HAVE_TOTALVIEW
/* Determine if srun is under the control of a TotalView debugger or not */
static bool _under_totalview(void)
{
	return (MPIR_being_debugged != 0);
}
#endif

static void _usage(void)
{
	printf("Usage: srun [-N nnodes] [-n ntasks] [-i in] [-i in] [-e err] [-e err]\n");
	printf("            [-c ncpus] [-r n] [-p partition] [--hold] [-t minutes]\n");
	printf("            [-D path] [--immediate] [--overcommit] [--no-kill]\n");
	printf("            [--share] [--label] [--unbuffered] [-m dist] [-J jobname]\n");
	printf("            [--jobid=id] [--batch] [--verbose] [--slurmd_debug=#]\n");
	printf("            [-T threads] [-W sec] [--attach] [--join] [--contiguous]\n");
	printf("            [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list] \n");
	printf("            [-w hosts...] [-x hosts...] [--usage] [OPTIONS...] \n");
 	printf("            executable [args...]\n");
}

static void _help(void)
{
	printf("Usage: srun [OPTIONS...] executable [args...]");
	printf("\nParallel run options:\n");
	printf("  -n, --ntasks=ntasks           number of tasks to run\n");
	printf("  -N, --nodes=nnodes            number of nodes on which to run\n");
	printf("                                (nnodes = min[-max])\n");
	printf("  -i, --input=in                location of stdin redirection\n");
	printf("  -o, --output=out              location of stdout redirection\n");
	printf("  -e, --error=err               location of stderr redirection\n");
	printf("  -c, --cpus-per-task=ncpus     number of cpus required per task\n");

	printf("  -r, --relative=n              run job step relative to node n of allocation\n");
	printf("  -p, --partition=partition     partition requested\n");
	printf("  -H, --hold                    submit job in held state\n");
	printf("  -t, --time=minutes            time limit\n");
	printf("  -D, --chdir=path              change current working directory of\n");
	printf("                                remote processes\n");
	printf("  -I, --immediate               exit if resources are not immediately available\n");
	printf("  -O, --overcommit              overcommit resources\n");
	printf("  -k, --no-kill                 do not kill job on node failure\n");
	printf("  -s, --share                   share nodes with other jobs\n");
	printf("  -l, --label                   prepend task number to lines of stdout/err\n");
	printf("  -u, --unbuffered              do not line-buffer stdout/err\n");
	printf("  -m, --distribution=type       distribution method for processes\n");
	printf("                                (type = block|cyclic)\n");
	printf("  -J, --job-name=jobname        name of job\n");
	printf("      --jobid=id                run under already allocated job\n");
	printf("  -b, --batch                   submit as batch job for later execution\n");
	printf("  -v, --verbose                 verbose operation (multiple -v's\n");
	printf("                                increase verbosity)\n");
 	printf("  -d, --slurmd-debug=value      slurmd debug level\n");
	printf("  -T, --threads=threads         set srun launch fanout\n");
	printf("  -W, --wait=sec                seconds to wait after first task ends\n");
 	printf("                                before killing job\n");

	printf("\nAllocate only:\n");
	printf("  -A, --allocate                allocate resources and spawn a shell\n");

	printf("\nAttach to running job:\n");
	printf("  -a, --attach=jobid            attach to running job with specified id\n");
	printf("  -j, --join                    when used with --attach, allow\n");
 	printf("                                forwarding of signals and stdin\n");

	printf("\nConstraint options:\n");
	printf("      --mincpus=n               minimum number of cpus per node\n");
	printf("      --mem=MB                  minimum amount of real memory\n");
 	printf("      --tmp=MB                  minimum amount of temporary disk\n");
	printf("  -C, --constraint=list         specify a list of constraints\n");
	printf("  --contiguous                  demand a contiguous range of nodes\n");
	printf("  -w, --nodelist=hosts...       request a specific list of hosts\n");
	printf("  -x, --exclude=hosts...        exclude a specific list of hosts\n");
	printf("  -Z, --no-allocate             don't allocate nodes (must supply -w)\n");

	printf("\nHelp options:\n");
	printf("      --help                    show this help message\n");
 	printf("      --usage                   display brief usage message\n");

	printf("\nOther options:\n");
	printf("  -V, --version                 output version information and exit\n");

}
