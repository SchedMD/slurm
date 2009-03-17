/*****************************************************************************\
 *  opt.c - options processing for salloc
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
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
#include <stdlib.h>		/* getenv, strtol, etc. */
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
#include "src/common/proc_args.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/read_config.h" /* contains getnodename() */

#include "src/salloc/salloc.h"
#include "src/salloc/opt.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_DEBUG       0x03
#define OPT_NODES       0x05
#define OPT_BOOL        0x06
#define OPT_CORE        0x07
#define OPT_CONN_TYPE	0x08
#define OPT_NO_ROTATE	0x0a
#define OPT_GEOMETRY	0x0b
#define OPT_BELL        0x0f
#define OPT_NO_BELL     0x10
#define OPT_JOBID       0x11
#define OPT_EXCLUSIVE   0x12
#define OPT_OVERCOMMIT  0x13
#define OPT_ACCTG_FREQ  0x14
#define OPT_WCKEY       0x15

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_JOBID       0x105
#define LONG_OPT_TMP         0x106
#define LONG_OPT_MEM         0x107
#define LONG_OPT_MINCPU      0x108
#define LONG_OPT_CONT        0x109
#define LONG_OPT_UID         0x10a
#define LONG_OPT_GID         0x10b
#define LONG_OPT_MINSOCKETS  0x10c
#define LONG_OPT_MINCORES    0x10d
#define LONG_OPT_MINTHREADS  0x10e
#define LONG_OPT_CORE	     0x10f
#define LONG_OPT_CONNTYPE    0x110
#define LONG_OPT_EXCLUSIVE   0x111
#define LONG_OPT_BEGIN       0x112
#define LONG_OPT_MAIL_TYPE   0x113
#define LONG_OPT_MAIL_USER   0x114
#define LONG_OPT_NICE        0x115
#define LONG_OPT_BELL        0x116
#define LONG_OPT_NO_BELL     0x117
#define LONG_OPT_COMMENT     0x118
#define LONG_OPT_REBOOT      0x119
#define LONG_OPT_BLRTS_IMAGE     0x120
#define LONG_OPT_LINUX_IMAGE     0x121
#define LONG_OPT_MLOADER_IMAGE   0x122
#define LONG_OPT_RAMDISK_IMAGE   0x123
#define LONG_OPT_NOSHELL         0x124
#define LONG_OPT_GET_USER_ENV    0x125
#define LONG_OPT_NETWORK         0x126
#define LONG_OPT_SOCKETSPERNODE  0x130
#define LONG_OPT_CORESPERSOCKET  0x131
#define LONG_OPT_THREADSPERCORE  0x132
#define LONG_OPT_NTASKSPERNODE   0x136
#define LONG_OPT_NTASKSPERSOCKET 0x137
#define LONG_OPT_NTASKSPERCORE   0x138
#define LONG_OPT_MEM_PER_CPU     0x13a
#define LONG_OPT_HINT            0x13b
#define LONG_OPT_ACCTG_FREQ      0x13c
#define LONG_OPT_WCKEY           0x13d

/*---- global variables, defined in opt.h ----*/
opt_t opt;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

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

static void  _proc_get_user_env(char *optarg);
static void _process_env_var(env_vars_t *e, const char *val);
static int _parse_signal(const char *signal_name);
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

	if (opt.verbose > 3)
		_opt_list();

	return 1;

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
		opt.progname ? opt.progname : "salloc", buf);
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
	struct passwd *pw;
	int i;

	if ((pw = getpwuid(getuid())) != NULL) {
		strncpy(opt.user, pw->pw_name, MAX_USERNAME);
		opt.uid = pw->pw_uid;
	} else
		error("who are you?");

	opt.gid = getgid();

	opt.cwd = NULL;
	opt.progname = NULL;

	opt.nprocs = 1;
	opt.nprocs_set = false;
	opt.cpus_per_task = 1; 
	opt.cpus_set = false;
	opt.min_nodes = 1;
	opt.max_nodes = 0;
	opt.nodes_set = false;
	opt.min_sockets_per_node = NO_VAL; /* requested min/maxsockets */
	opt.max_sockets_per_node = NO_VAL;
	opt.min_cores_per_socket = NO_VAL; /* requested min/maxcores */
	opt.max_cores_per_socket = NO_VAL;
	opt.min_threads_per_core = NO_VAL; /* requested min/maxthreads */
	opt.max_threads_per_core = NO_VAL;
	opt.ntasks_per_node      = NO_VAL; /* ntask max limits */
	opt.ntasks_per_socket    = NO_VAL;
	opt.ntasks_per_core      = NO_VAL;
	opt.cpu_bind_type = 0;		/* local dummy variable for now */
	opt.time_limit = NO_VAL;
	opt.time_limit_str = NULL;
	opt.partition = NULL;

	opt.job_name = NULL;
	opt.jobid = NO_VAL;
	opt.dependency = NULL;
	opt.account  = NULL;
	opt.comment  = NULL;

	opt.distribution = SLURM_DIST_UNKNOWN;
	opt.plane_size   = NO_VAL;

	opt.shared = (uint16_t)NO_VAL;
	opt.no_kill = false;
	opt.kill_command_signal = SIGTERM;
	opt.kill_command_signal_set = false;

	opt.immediate	= false;
	opt.overcommit	= false;
	opt.max_wait	= 0;

	opt.quiet = 0;
	opt.verbose = 0;

	/* constraint default (-1 is no constraint) */
	opt.mincpus	    = -1;
	opt.minsockets      = -1;
	opt.mincores        = -1;
	opt.minthreads      = -1;
	opt.mem_per_cpu	    = -1;
	opt.realmem	    = -1;
	opt.tmpdisk	    = -1;

	opt.hold	    = false;
	opt.constraints	    = NULL;
	opt.contiguous	    = false;
	opt.nodelist	    = NULL;
	opt.exc_nodes	    = NULL;

	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		opt.geometry[i]	    = (uint16_t) NO_VAL;
	opt.reboot          = false;
	opt.no_rotate	    = false;
	opt.conn_type	    = (uint16_t) NO_VAL;

	opt.euid	    = (uid_t) -1;
	opt.egid	    = (gid_t) -1;
	
	opt.bell            = BELL_AFTER_DELAY;
	opt.acctg_freq      = -1;
	opt.no_shell	    = false;
	opt.get_user_env_time = -1;
	opt.get_user_env_mode = -1;
	opt.wckey = NULL;
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
  {"SALLOC_ACCOUNT",       OPT_STRING,     &opt.account,       NULL           },
  {"SALLOC_CONN_TYPE",     OPT_CONN_TYPE,  NULL,               NULL           },
  {"SALLOC_DEBUG",         OPT_DEBUG,      NULL,               NULL           },
  {"SALLOC_GEOMETRY",      OPT_GEOMETRY,   NULL,               NULL           },
  {"SALLOC_IMMEDIATE",     OPT_BOOL,       &opt.immediate,     NULL           },
  {"SALLOC_JOBID",         OPT_JOBID,      NULL,               NULL           },
  {"SALLOC_NO_ROTATE",     OPT_NO_ROTATE,  NULL,               NULL           },
  {"SALLOC_PARTITION",     OPT_STRING,     &opt.partition,     NULL           },
  {"SALLOC_TIMELIMIT",     OPT_STRING,     &opt.time_limit_str,NULL           },
  {"SALLOC_WAIT",          OPT_INT,        &opt.max_wait,      NULL           },
  {"SALLOC_BELL",          OPT_BELL,       NULL,               NULL           },
  {"SALLOC_NO_BELL",       OPT_NO_BELL,    NULL,               NULL           },
  {"SALLOC_EXCLUSIVE",     OPT_EXCLUSIVE,  NULL,               NULL           },
  {"SALLOC_OVERCOMMIT",    OPT_OVERCOMMIT, NULL,               NULL           },
  {"SALLOC_ACCTG_FREQ",    OPT_INT,        &opt.acctg_freq,    NULL           },
  {"SALLOC_NETWORK",       OPT_STRING,     &opt.network,       NULL           },
  {"SALLOC_WCKEY",         OPT_STRING,     &opt.wckey,         NULL           },
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

	case OPT_BOOL:
		/* A boolean env variable is true if:
		 *  - set, but no argument
		 *  - argument is "yes"
		 *  - argument is a non-zero number
		 */
		if (val == NULL || strcmp(val, "") == 0) {
			*((bool *)e->arg) = true;
		} else if (strcasecmp(val, "yes") == 0) {
			*((bool *)e->arg) = true;
		} else if ((strtol(val, &end, 10) != 0)
			   && end != val) {
			*((bool *)e->arg) = true;
		} else {
			*((bool *)e->arg) = false;
		}
		break;

	case OPT_DEBUG:
		if (val != NULL) {
			opt.verbose = (int) strtol(val, &end, 10);
			if (!(end && *end == '\0')) 
				error("%s=%s invalid", e->var, val);
		}
		break;

	case OPT_NODES:
		opt.nodes_set = verify_node_count( val, 
						   &opt.min_nodes, 
						   &opt.max_nodes );
		if (opt.nodes_set == false) {
			error("invalid node count in env variable, ignoring");
		}
		break;

	case OPT_CONN_TYPE:
		opt.conn_type = verify_conn_type(val);
		break;

	case OPT_NO_ROTATE:
		opt.no_rotate = true;
		break;

	case OPT_GEOMETRY:
		if (verify_geometry(val, opt.geometry)) {
			error("\"%s=%s\" -- invalid geometry, ignoring...",
			      e->var, val);
		}
		break;
	case OPT_BELL:
		opt.bell = BELL_ALWAYS;
		break;
	case OPT_NO_BELL:
		opt.bell = BELL_NEVER;
		break;
	case OPT_JOBID:
		info("WARNING: Creating SLURM job allocation from within "
			"another allocation");
		info("WARNING: You are attempting to initiate a second job");
		break;
	case OPT_EXCLUSIVE:
		opt.shared = 0;
		break;
	case OPT_OVERCOMMIT:
		opt.overcommit = true;
		break;
	case OPT_WCKEY:
		xfree(opt.wckey);
		opt.wckey = xstrdup(optarg);
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

void set_options(const int argc, char **argv)
{
	int opt_char, option_index = 0;
	char *tmp;
	static struct option long_options[] = {
		{"extra-node-info", required_argument, 0, 'B'},
		{"cpus-per-task", required_argument, 0, 'c'},
		{"constraint",    required_argument, 0, 'C'},
		{"chdir",         required_argument, 0, 'D'},
		{"nodefile",      required_argument, 0, 'F'},
		{"geometry",      required_argument, 0, 'g'},
		{"help",          no_argument,       0, 'h'},
		{"hold",          no_argument,       0, 'H'},
		{"immediate",     no_argument,       0, 'I'},
		{"job-name",      required_argument, 0, 'J'},
		{"no-kill",       no_argument,       0, 'k'},
		{"kill-command",  optional_argument, 0, 'K'},
		{"licenses",      required_argument, 0, 'L'},
		{"distribution",  required_argument, 0, 'm'},
		{"tasks",         required_argument, 0, 'n'},
		{"ntasks",        required_argument, 0, 'n'},
		{"nodes",         required_argument, 0, 'N'},
		{"overcommit",    no_argument,       0, 'O'},
		{"partition",     required_argument, 0, 'p'},
		{"dependency",    required_argument, 0, 'P'},
		{"quiet",         no_argument,       0, 'q'},
		{"no-rotate",     no_argument,       0, 'R'},
		{"share",         no_argument,       0, 's'},
		{"time",          required_argument, 0, 't'},
		{"usage",         no_argument,       0, 'u'},
		{"account",       required_argument, 0, 'U'},
		{"verbose",       no_argument,       0, 'v'},
		{"version",       no_argument,       0, 'V'},
		{"nodelist",      required_argument, 0, 'w'},
		{"wait",          required_argument, 0, 'W'},
		{"exclude",       required_argument, 0, 'x'},
		{"contiguous",    no_argument,       0, LONG_OPT_CONT},
		{"exclusive",     no_argument,       0, LONG_OPT_EXCLUSIVE},
		{"mincpus",       required_argument, 0, LONG_OPT_MINCPU},
		{"minsockets",    required_argument, 0, LONG_OPT_MINSOCKETS},
		{"mincores",      required_argument, 0, LONG_OPT_MINCORES},
		{"minthreads",    required_argument, 0, LONG_OPT_MINTHREADS},
		{"mem",           required_argument, 0, LONG_OPT_MEM},
		{"job-mem",       required_argument, 0, LONG_OPT_MEM_PER_CPU},
		{"task-mem",      required_argument, 0, LONG_OPT_MEM_PER_CPU},
		{"mem-per-cpu",   required_argument, 0, LONG_OPT_MEM_PER_CPU},
		{"hint",          required_argument, 0, LONG_OPT_HINT},
		{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
		{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
		{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
		{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
		{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
		{"ntasks-per-core",  required_argument, 0, LONG_OPT_NTASKSPERCORE},
		{"tasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
		{"tmp",           required_argument, 0, LONG_OPT_TMP},
		{"uid",           required_argument, 0, LONG_OPT_UID},
		{"gid",           required_argument, 0, LONG_OPT_GID},
		{"conn-type",     required_argument, 0, LONG_OPT_CONNTYPE},
		{"begin",         required_argument, 0, LONG_OPT_BEGIN},
		{"mail-type",     required_argument, 0, LONG_OPT_MAIL_TYPE},
		{"mail-user",     required_argument, 0, LONG_OPT_MAIL_USER},
		{"nice",          optional_argument, 0, LONG_OPT_NICE},
		{"bell",          no_argument,       0, LONG_OPT_BELL},
		{"no-bell",       no_argument,       0, LONG_OPT_NO_BELL},
		{"jobid",         required_argument, 0, LONG_OPT_JOBID},
		{"comment",       required_argument, 0, LONG_OPT_COMMENT},
		{"reboot",	  no_argument,       0, LONG_OPT_REBOOT},
		{"blrts-image",   required_argument, 0, LONG_OPT_BLRTS_IMAGE},
		{"linux-image",   required_argument, 0, LONG_OPT_LINUX_IMAGE},
		{"mloader-image", required_argument, 0, LONG_OPT_MLOADER_IMAGE},
		{"ramdisk-image", required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
		{"acctg-freq",    required_argument, 0, LONG_OPT_ACCTG_FREQ},
		{"no-shell",      no_argument,       0, LONG_OPT_NOSHELL},
		{"get-user-env",  optional_argument, 0, LONG_OPT_GET_USER_ENV},
		{"network",       required_argument, 0, LONG_OPT_NETWORK},
		{"wckey",         required_argument, 0, LONG_OPT_WCKEY},
		{NULL,            0,                 0, 0}
	};
	char *opt_string = "+a:B:c:C:d:D:F:g:hHIJ:kK:L:m:n:N:Op:P:qR:st:uU:vVw:W:x:";

	opt.progname = xbasename(argv[0]);
	optind = 0;		
	while((opt_char = getopt_long(argc, argv, opt_string,
				      long_options, &option_index)) != -1) {
		switch (opt_char) {
			
		case '?':
			fprintf(stderr, "Try \"salloc --help\" for more "
				"information\n");
			exit(1);
			break;
		case 'B':
			opt.extra_set = verify_socket_core_thread_count(
				optarg,
				&opt.min_sockets_per_node,
				&opt.max_sockets_per_node,
				&opt.min_cores_per_socket,
				&opt.max_cores_per_socket,
				&opt.min_threads_per_core,
				&opt.max_threads_per_core,
				&opt.cpu_bind_type);


			if (opt.extra_set == false) {
				error("invalid resource allocation -B `%s'",
					optarg);
				exit(1);
			}
			break;
		case 'c':
			opt.cpus_set = true;
			opt.cpus_per_task = 
				_get_int(optarg, "cpus-per-task");
			break;
		case 'C':
			xfree(opt.constraints);
			opt.constraints = xstrdup(optarg);
			break;
/* 		case 'd':	see 'P' below */
		case 'D':
			xfree(opt.cwd);
			opt.cwd = xstrdup(optarg);
			break;
		case 'F':
			xfree(opt.nodelist);
			tmp = slurm_read_hostfile(optarg, 0);
			if (tmp != NULL) {
				opt.nodelist = xstrdup(tmp);
				free(tmp);
			} else {
				error("\"%s\" is not a valid node file");
				exit(1);
			}
			break;
		case 'g':
			if (verify_geometry(optarg, opt.geometry))
				exit(1);
			break;
		case 'h':
			_help();
			exit(0);
		case 'H':
			opt.hold = true;
			break;
		case 'I':
			opt.immediate = true;
			break;
		case 'J':
			xfree(opt.job_name);
			opt.job_name = xstrdup(optarg);
			break;
		case 'k':
			opt.no_kill = true;
			break;
		case 'K': /* argument is optional */
			if (optarg) {
				opt.kill_command_signal = _parse_signal(optarg);
				if (opt.kill_command_signal == 0)
					exit(1);
			}
			opt.kill_command_signal_set = true;
			break;
		case 'L':
			xfree(opt.licenses);
			opt.licenses = xstrdup(optarg);
			break;
		case 'm':
			opt.distribution = verify_dist_type(optarg, 
							    &opt.plane_size);
			if (opt.distribution == SLURM_DIST_UNKNOWN) {
				error("distribution type `%s' " 
				      "is not recognized", optarg);
				exit(1);
			}
			break;
		case 'n':
			opt.nprocs_set = true;
			opt.nprocs = 
				_get_int(optarg, "number of tasks");
			break;
		case 'N':
			opt.nodes_set = 
				verify_node_count(optarg, 
						  &opt.min_nodes,
						  &opt.max_nodes);
			if (opt.nodes_set == false) {
				exit(1);
			}
			break;
		case 'O':
			opt.overcommit = true;
			break;
		case 'p':
			xfree(opt.partition);
			opt.partition = xstrdup(optarg);
			break;
		case 'd':
		case 'P':
			xfree(opt.dependency);
			opt.dependency = xstrdup(optarg);
			break;
		case 'q':
			opt.quiet++;
			break;
		case 'R':
			opt.no_rotate = true;
			break;
		case 's':
			opt.shared = 1;
			break;
		case 't':
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(optarg);
			break;
		case 'u':
			_usage();
			exit(0);
		case 'U':
			xfree(opt.account);
			opt.account = xstrdup(optarg);
			break;
		case 'v':
			opt.verbose++;
			break;
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		case 'w':
			xfree(opt.nodelist);
			opt.nodelist = xstrdup(optarg);
#ifdef HAVE_BG
			info("\tThe nodelist option should only be used if\n"
			     "\tthe block you are asking for can be created.\n"
			     "\tIt should also include all the midplanes you\n"
			     "\twant to use, partial lists may not\n"
			     "\twork correctly.\n"
			     "\tPlease consult smap before using this option\n"
			     "\tor your job may be stuck with no way to run.");
#endif
			break;
		case 'W':
			opt.max_wait = _get_int(optarg, "wait");
			break;
		case 'x':
			xfree(opt.exc_nodes);
			opt.exc_nodes = xstrdup(optarg);
			break;
		case LONG_OPT_CONT:
			opt.contiguous = true;
			break;
                case LONG_OPT_EXCLUSIVE:
                        opt.shared = 0;
                        break;
		case LONG_OPT_MINCPU:
			opt.mincpus = _get_int(optarg, "mincpus");
			if (opt.mincpus < 0) {
				error("invalid mincpus constraint %s", 
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_MINSOCKETS:
			opt.minsockets = _get_int(optarg, "minsockets");
			if (opt.minsockets < 0) {
				error("invalid minsockets constraint %s", 
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_MINCORES:
			opt.mincores = _get_int(optarg, "mincores");
			if (opt.mincores < 0) {
				error("invalid mincores constraint %s", 
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_MINTHREADS:
			opt.minthreads = _get_int(optarg, "minthreads");
			if (opt.minthreads < 0) {
				error("invalid minthreads constraint %s", 
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_MEM:
			opt.realmem = (int) str_to_bytes(optarg);
			if (opt.realmem < 0) {
				error("invalid memory constraint %s", 
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_MEM_PER_CPU:
			opt.mem_per_cpu = (int) str_to_bytes(optarg);
			if (opt.mem_per_cpu < 0) {
				error("invalid memory constraint %s", 
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_TMP:
			opt.tmpdisk = str_to_bytes(optarg);
			if (opt.tmpdisk < 0) {
				error("invalid tmp value %s", optarg);
				exit(1);
			}
			break;
		case LONG_OPT_UID:
			if (opt.euid != (uid_t) -1)
				fatal ("duplicate --uid option");
			opt.euid = uid_from_string (optarg);
			if (opt.euid == (uid_t) -1)
				fatal ("--uid=\"%s\" invalid", optarg);
			break;
		case LONG_OPT_GID:
			if (opt.egid != (gid_t) -1)
				fatal ("duplicate --gid option");
			opt.egid = gid_from_string (optarg);
			if (opt.egid == (gid_t) -1)
				fatal ("--gid=\"%s\" invalid", optarg);
			break;
		case LONG_OPT_CONNTYPE:
			opt.conn_type = verify_conn_type(optarg);
			break;
		case LONG_OPT_BEGIN:
			opt.begin = parse_time(optarg, 0);
			if (opt.begin == 0) {
				fatal("Invalid time specification %s",
				      optarg);
			}
			break;
		case LONG_OPT_MAIL_TYPE:
			opt.mail_type |= parse_mail_type(optarg);
			if (opt.mail_type == 0)
				fatal("--mail-type=%s invalid", optarg);
			break;
		case LONG_OPT_MAIL_USER:
			xfree(opt.mail_user);
			opt.mail_user = xstrdup(optarg);
			break;
		case LONG_OPT_NICE:
			if (optarg)
				opt.nice = strtol(optarg, NULL, 10);
			else
				opt.nice = 100;
			if (abs(opt.nice) > NICE_OFFSET) {
				error("Invalid nice value, must be between "
					"-%d and %d", NICE_OFFSET, NICE_OFFSET);
				exit(1);
			}
			if (opt.nice < 0) {
				uid_t my_uid = getuid();
				if ((my_uid != 0) &&
				    (my_uid != slurm_get_slurm_user_id())) {
					error("Nice value must be non-negative, "
					      "value ignored");
					opt.nice = 0;
				}
			}
			break;
		case LONG_OPT_BELL:
			opt.bell = BELL_ALWAYS;
			break;
		case LONG_OPT_NO_BELL:
			opt.bell = BELL_NEVER;
			break;
		case LONG_OPT_JOBID:
			opt.jobid = _get_int(optarg, "jobid");
			break;
		case LONG_OPT_COMMENT:
			xfree(opt.comment);
			opt.comment = xstrdup(optarg);
			break;
		case LONG_OPT_SOCKETSPERNODE:
			get_resource_arg_range( optarg, "sockets-per-node",
						&opt.min_sockets_per_node,
						&opt.max_sockets_per_node,
						true );
			break;
		case LONG_OPT_CORESPERSOCKET:
			get_resource_arg_range( optarg, "cores-per-socket",
						&opt.min_cores_per_socket,
						&opt.max_cores_per_socket,
						true);
			break;
		case LONG_OPT_THREADSPERCORE:
			get_resource_arg_range( optarg, "threads-per-core",
						&opt.min_threads_per_core,
						&opt.max_threads_per_core,
						true );
			break;
		case LONG_OPT_HINT:
			if (verify_hint(optarg,
				&opt.min_sockets_per_node,
				&opt.max_sockets_per_node,
				&opt.min_cores_per_socket,
				&opt.max_cores_per_socket,
				&opt.min_threads_per_core,
				&opt.max_threads_per_core,
				&opt.cpu_bind_type)) {
				exit(1);
			}
			break;
		case LONG_OPT_NTASKSPERNODE:
			opt.ntasks_per_node = _get_int(optarg,
				"ntasks-per-node");
			break;
		case LONG_OPT_NTASKSPERSOCKET:
			opt.ntasks_per_socket = _get_int(optarg, 
				"ntasks-per-socket");
			break;
		case LONG_OPT_NTASKSPERCORE:
			opt.ntasks_per_core = _get_int(optarg,
				"ntasks-per-core");
			break;
		case LONG_OPT_REBOOT:
			opt.reboot = true;
			break;
		case LONG_OPT_BLRTS_IMAGE:
			xfree(opt.blrtsimage);
			opt.blrtsimage = xstrdup(optarg);
			break;
		case LONG_OPT_LINUX_IMAGE:
			xfree(opt.linuximage);
			opt.linuximage = xstrdup(optarg);
			break;
		case LONG_OPT_MLOADER_IMAGE:
			xfree(opt.mloaderimage);
			opt.mloaderimage = xstrdup(optarg);
			break;
		case LONG_OPT_RAMDISK_IMAGE:
			xfree(opt.ramdiskimage);
			opt.ramdiskimage = xstrdup(optarg);
			break;
		case LONG_OPT_ACCTG_FREQ:
			opt.acctg_freq = _get_int(optarg, "acctg-freq");
			break;
		case LONG_OPT_NOSHELL:
			opt.no_shell = true;
			break;
		case LONG_OPT_GET_USER_ENV:
			if (optarg)
				_proc_get_user_env(optarg);
			else
				opt.get_user_env_time = 0;
			break;
		case LONG_OPT_NETWORK:
			xfree(opt.network);
			opt.network = xstrdup(optarg);
			break;
		case LONG_OPT_WCKEY:
			xfree(opt.wckey);
			opt.wckey = xstrdup(optarg);
			break;
		default:
			fatal("Unrecognized command line parameter %c",
			      opt_char);
		}
	}
}

static void _proc_get_user_env(char *optarg)
{
	char *end_ptr;

	if ((optarg[0] >= '0') && (optarg[0] <= '9'))
		opt.get_user_env_time = strtol(optarg, &end_ptr, 10);
	else {
		opt.get_user_env_time = 0;
		end_ptr = optarg;
	}
 
	if ((end_ptr == NULL) || (end_ptr[0] == '\0'))
		return;
	if      ((end_ptr[0] == 's') || (end_ptr[0] == 'S'))
		opt.get_user_env_mode = 1;
	else if ((end_ptr[0] == 'l') || (end_ptr[0] == 'L'))
		opt.get_user_env_mode = 2;
}

/*
 * _opt_args() : set options via commandline args and popt
 */
static void _opt_args(int argc, char **argv)
{
	int i;
	char **rest = NULL;

	set_options(argc, argv);

	command_argc = 0;
	if (optind < argc) {
		rest = argv + optind;
		while (rest[command_argc] != NULL)
			command_argc++;
	}
	command_argv = (char **) xmalloc((command_argc + 1) * sizeof(char *));
	for (i = 0; i < command_argc; i++)
		command_argv[i] = xstrdup(rest[i]);
	command_argv[i] = NULL;	/* End of argv's (for possible execv) */

	if (!_opt_verify())
		exit(1);
}

/* _get_shell - return a string containing the default shell for this user
 * NOTE: This function is NOT reentrant (see getpwuid_r if needed) */
static char *_get_shell(void)
{
	struct passwd *pw_ent_ptr;

	pw_ent_ptr = getpwuid(opt.uid);
	if (!pw_ent_ptr) {
		pw_ent_ptr = getpwnam("nobody");
		error("warning - no user information for user %d", opt.uid);
	}
	return pw_ent_ptr->pw_shell;
}

static int _salloc_default_command (int *argcp, char **argvp[])
{
	slurm_ctl_conf_t *cf = slurm_conf_lock();

	if (cf->salloc_default_command) {
		/*
		 *  Set argv to "/bin/sh -c 'salloc_default_command'"
		 */
		*argcp = 3;
		*argvp = xmalloc (sizeof (char *) * 4);
		(*argvp)[0] = "/bin/sh";
		(*argvp)[1] = "-c";
		(*argvp)[2] = xstrdup (cf->salloc_default_command);
		(*argvp)[3] = NULL;
	}
	else {
		*argcp = 1;
		*argvp = xmalloc (sizeof (char *) * 2);
		(*argvp)[0] = _get_shell ();
		(*argvp)[1] = NULL;
	}

	slurm_conf_unlock();
	return (0);
}

/* 
 * _opt_verify : perform some post option processing verification
 *
 */
static bool _opt_verify(void)
{
	bool verified = true;

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-q)");
		verified = false;
	}

	if (opt.mincpus < opt.cpus_per_task)
		opt.mincpus = opt.cpus_per_task;

	if ((opt.euid != (uid_t) -1) && (opt.euid != opt.uid)) 
		opt.uid = opt.euid;

	if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid)) 
		opt.gid = opt.egid;

	if ((opt.no_shell == false) && (command_argc == 0))
		_salloc_default_command (&command_argc, &command_argv);

	if ((opt.job_name == NULL) && (command_argc > 0))
		opt.job_name = base_name(command_argv[0]);

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

	if ((opt.min_nodes < 0) || (opt.max_nodes < 0) || 
	    (opt.max_nodes && (opt.min_nodes > opt.max_nodes))) {
		error("%s: invalid number of nodes (-N %d-%d)\n",
		      opt.progname, opt.min_nodes, opt.max_nodes);
		verified = false;
	}

	if ((opt.realmem > -1) && (opt.mem_per_cpu > -1)) {
		if (opt.realmem < opt.mem_per_cpu) {
			info("mem < mem-per-cpu - resizing mem to be equal "
			     "to mem-per-cpu");
			opt.realmem = opt.mem_per_cpu;
		}
	}
	
        /* Check to see if user has specified enough resources to
	 * satisfy the plane distribution with the specified
	 * plane_size.  
	 * if (n/plane_size < N) and ((N-1) * plane_size >= n) -->
	 * problem Simple check will not catch all the problem/invalid
	 * cases.
	 * The limitations of the plane distribution in the cons_res
	 * environment are more extensive and are documented in the
	 * SLURM reference guide.  */
	if (opt.distribution == SLURM_DIST_PLANE && opt.plane_size) {
		if ((opt.nprocs/opt.plane_size) < opt.min_nodes) {
			if (((opt.min_nodes-1)*opt.plane_size) >= opt.nprocs) {
#if(0)
				info("Too few processes ((n/plane_size) %d < N %d) "
				     "and ((N-1)*(plane_size) %d >= n %d)) ",
				     opt.nprocs/opt.plane_size, opt.min_nodes, 
				     (opt.min_nodes-1)*opt.plane_size, 
				     opt.nprocs);
#endif
				error("Too few processes for the requested "
				      "{plane,node} distribution");
				exit(1);
			}
		}
	}

	/* bound max_threads/cores from ntasks_cores/sockets */ 
	if ((opt.max_threads_per_core <= 0) &&
	    (opt.ntasks_per_core > 0)) {
		opt.max_threads_per_core = opt.ntasks_per_core;
		/* if cpu_bind_type doesn't already have a auto pref,
		 * choose the level based on the level of ntasks
		 */
		if (!(opt.cpu_bind_type & (CPU_BIND_TO_SOCKETS |
					   CPU_BIND_TO_CORES |
					   CPU_BIND_TO_THREADS))) {
			opt.cpu_bind_type |= CPU_BIND_TO_CORES;
		}
	}
	if ((opt.max_cores_per_socket <= 0) &&
	    (opt.ntasks_per_socket > 0)) {
		opt.max_cores_per_socket = opt.ntasks_per_socket;
		/* if cpu_bind_type doesn't already have a auto pref,
		 * choose the level based on the level of ntasks
		 */
		if (!(opt.cpu_bind_type & (CPU_BIND_TO_SOCKETS |
					   CPU_BIND_TO_CORES |
					   CPU_BIND_TO_THREADS))) {
			opt.cpu_bind_type |= CPU_BIND_TO_SOCKETS;
		}
	}

	/* massage the numbers */
	if (opt.nodes_set && !opt.nprocs_set) {
		/* 1 proc / node default */
		opt.nprocs = opt.min_nodes;

		/* 1 proc / min_[socket * core * thread] default */
		if (opt.min_sockets_per_node > 0) {
			opt.nprocs *= opt.min_sockets_per_node;
			opt.nprocs_set = true;
		}
		if (opt.min_cores_per_socket > 0) {
			opt.nprocs *= opt.min_cores_per_socket;
			opt.nprocs_set = true;
		}
		if (opt.min_threads_per_core > 0) {
			opt.nprocs *= opt.min_threads_per_core;
			opt.nprocs_set = true;
		}

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

	if (opt.time_limit_str) {
		opt.time_limit = time_str2mins(opt.time_limit_str);
		if ((opt.time_limit < 0) && (opt.time_limit != INFINITE)) {
			error("Invalid time limit specification");
			exit(1);
		}
		if (opt.time_limit == 0)
			opt.time_limit = INFINITE;
	}

	if (opt.immediate) {
		char *sched_name = slurm_get_sched_type();
		if (strcmp(sched_name, "sched/wiki") == 0) {
			info("WARNING: Ignoring the -I/--immediate option "
				"(not supported by Maui)");
			opt.immediate = false;
		}
		xfree(sched_name);
	}

#ifdef HAVE_AIX
	if (opt.network == NULL)
		opt.network = "us,sn_all,bulk_xfer";
#endif

	return verified;
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

	if (opt.minsockets > 0)
		xstrfmtcat(buf, "minsockets=%d ", opt.minsockets);

	if (opt.mincores > 0)
		xstrfmtcat(buf, "mincores=%d ", opt.mincores);

	if (opt.minthreads > 0)
		xstrfmtcat(buf, "minthreads=%d ", opt.minthreads);

	if (opt.realmem > 0)
		xstrfmtcat(buf, "mem=%dM ", opt.realmem);

	if (opt.mem_per_cpu > 0)
		xstrfmtcat(buf, "mem-per-cpu=%dM ", opt.mem_per_cpu);

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

/*
 * Takes a string containing the number or name of a signal and returns
 * the signal number.  The signal name is case insensitive, and may be of
 * the form "SIGHUP" or just "HUP".
 *
 * Allowed signal names are HUP, INT, QUIT, KILL, TERM, USR1, USR2, and CONT.
 */
static int _parse_signal(const char *signal_name)
{
	char *sig_name[] = {"HUP", "INT", "QUIT", "KILL", "TERM",
			    "USR1", "USR2", "CONT", NULL};
	int sig_num[] = {SIGHUP, SIGINT, SIGQUIT, SIGKILL, SIGTERM,
			       SIGUSR1, SIGUSR2, SIGCONT};
	char *ptr;
	long tmp;
	int sig;
	int i;

	tmp = strtol(signal_name, &ptr, 10);
	if (ptr != signal_name) { /* found a number */
		if (xstring_is_whitespace(ptr)) {
			sig = (int)tmp;
		} else {
			goto fail;
		}
	} else {
		ptr = (char *)signal_name;
		while (isspace(*ptr))
			ptr++;
		if (strncasecmp(ptr, "SIG", 3) == 0)
			ptr += 3;
		for (i = 0; ; i++) {
			if (sig_name[i] == NULL) {
				goto fail;
			}
			if (strncasecmp(ptr, sig_name[i],
					strlen(sig_name[i])) == 0) {
				/* found the signal name */
				if (!xstring_is_whitespace(
					    ptr + strlen(sig_name[i]))) {
					goto fail;
				}
				sig = sig_num[i];
				break;
			}
		}
	}

	return sig;

fail:
	error("\"%s\" is not a valid signal", signal_name);
	return 0;
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
	info("nprocs         : %d %s", opt.nprocs,
		opt.nprocs_set ? "(set)" : "(default)");
	info("cpus_per_task  : %d %s", opt.cpus_per_task,
		opt.cpus_set ? "(set)" : "(default)");
	if (opt.max_nodes)
		info("nodes          : %d-%d", opt.min_nodes, opt.max_nodes);
	else {
		info("nodes          : %d %s", opt.min_nodes,
			opt.nodes_set ? "(set)" : "(default)");
	}
	info("partition      : %s",
		opt.partition == NULL ? "default" : opt.partition);
	info("job name       : `%s'", opt.job_name);
	info("wckey          : `%s'", opt.wckey);
	if (opt.jobid != NO_VAL)
		info("jobid          : %u", opt.jobid);
	info("distribution   : %s", format_task_dist_states(opt.distribution));
	if(opt.distribution == SLURM_DIST_PLANE)
		info("plane size   : %u", opt.plane_size);
	info("verbose        : %d", opt.verbose);
	info("immediate      : %s", tf_(opt.immediate));
	info("overcommit     : %s", tf_(opt.overcommit));
	if (opt.time_limit == INFINITE)
		info("time_limit     : INFINITE");
	else if (opt.time_limit != NO_VAL)
		info("time_limit     : %d", opt.time_limit);
	info("wait           : %d", opt.max_wait);
	if (opt.nice)
		info("nice           : %d", opt.nice);
	info("account        : %s", opt.account);
	info("comment        : %s", opt.comment);
	info("dependency     : %s", opt.dependency);
	info("network        : %s", opt.network);
	str = print_constraints();
	info("constraints    : %s", str);
	xfree(str);
	if (opt.conn_type != (uint16_t) NO_VAL)
		info("conn_type      : %u", opt.conn_type);
	str = print_geometry(opt.geometry);
	info("geometry       : %s", str);
	xfree(str);
	info("reboot         : %s", opt.reboot ? "no" : "yes");
	info("rotate         : %s", opt.no_rotate ? "yes" : "no");
	if (opt.blrtsimage)
		info("BlrtsImage     : %s", opt.blrtsimage);
	if (opt.linuximage)
		info("LinuxImage     : %s", opt.linuximage);
	if (opt.mloaderimage)
		info("MloaderImage   : %s", opt.mloaderimage);
	if (opt.ramdiskimage)
		info("RamDiskImage   : %s", opt.ramdiskimage);
	if (opt.begin) {
		char time_str[32];
		slurm_make_time_str(&opt.begin, time_str, sizeof(time_str));
		info("begin          : %s", time_str);
	}
	info("mail_type      : %s", print_mail_type(opt.mail_type));
	info("mail_user      : %s", opt.mail_user);
	info("sockets-per-node  : %d - %d", opt.min_sockets_per_node,
					    opt.max_sockets_per_node);
	info("cores-per-socket  : %d - %d", opt.min_cores_per_socket,
					    opt.max_cores_per_socket);
	info("threads-per-core  : %d - %d", opt.min_threads_per_core,
					    opt.max_threads_per_core);
	info("ntasks-per-node   : %d", opt.ntasks_per_node);
	info("ntasks-per-socket : %d", opt.ntasks_per_socket);
	info("ntasks-per-core   : %d", opt.ntasks_per_core);
	info("plane_size        : %u", opt.plane_size);
	str = print_commandline(command_argc, command_argv);
	info("user command   : `%s'", str);
	xfree(str);

}

static void _usage(void)
{
 	printf(
"Usage: salloc [-N numnodes|[min nodes]-[max nodes]] [-n num-processors]\n"
"              [[-c cpus-per-node] [-r n] [-p partition] [--hold] [-t minutes]\n"
"              [--immediate] [--no-kill] [--overcommit] [-D path]\n"
"              [--share] [-J jobname] [--jobid=id]\n"
"              [--verbose] [--gid=group] [--uid=user] [--licenses=names]\n"
"              [-W sec] [--minsockets=n] [--mincores=n] [--minthreads=n]\n"
"              [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"              [--account=name] [--dependency=type:jobid] [--comment=name]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
"              [--geometry=XxYxZ] [--conn-type=type] [--no-rotate] [ --reboot]\n"
"              [--blrts-image=path] [--linux-image=path]\n"
"              [--mloader-image=path] [--ramdisk-image=path]\n"
#endif
"              [--mail-type=type] [--mail-user=user][--nice[=value]]\n"
"              [--bell] [--no-bell] [--kill-command[=signal]]\n"
"              [--nodefile=file] [--nodelist=hosts] [--exclude=hosts]\n"
"              [--network=type] [--mem-per-cpu=MB]\n"
"              [executable [args...]]\n");
}

static void _help(void)
{
	slurm_ctl_conf_t *conf;

        printf (
"Usage: salloc [OPTIONS...] [executable [args...]]\n"
"\n"
"Parallel run options:\n"
"  -N, --nodes=N               number of nodes on which to run (N = min[-max])\n"
"  -n, --tasks=N               number of processors required\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"      --ntasks-per-node=n     number of tasks to invoke on each node\n"
"  -p, --partition=partition   partition requested\n"
"  -H, --hold                  submit job in held state\n"
"  -t, --time=minutes          time limit\n"
"  -D, --chdir=path            change working directory\n"
"  -I, --immediate             exit if resources are not immediately available\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -K, --kill-command[=signal] signal to send terminating job\n"
"  -O, --overcommit            overcommit resources\n"
"  -s, --share                 share nodes with other jobs\n"
"  -m, --distribution=type     distribution method for processes to nodes\n"
"                              (type = block|cyclic|arbitrary)\n"
"  -J, --job-name=jobname      name of job\n"
"      --jobid=id              specify jobid to use\n"
"  -W, --wait=sec              seconds to wait for allocation if not\n"
"                              immediately available\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -q, --quiet                 quiet mode (suppress informational messages)\n"
"  -P, --dependency=type:jobid defer job until condition on jobid is satisfied\n"
"      --nice[=value]          decrease secheduling priority by value\n"
"  -U, --account=name          charge job to specified account\n"
"      --begin=time            defer job until HH:MM DD/MM/YY\n"
"      --comment=name          arbitrary comment\n"
"  -L, --licenses=names        required license, comma separated\n"
"      --mail-type=type        notify on state change: BEGIN, END, FAIL or ALL\n"
"      --mail-user=user        who to send email notification for job state changes\n"
"      --bell                  ring the terminal bell when the job is allocated\n"
"      --no-bell               do NOT ring the terminal bell\n"
"      --gid=group_id          group ID to run job as (user root only)\n"
"      --uid=user_id           user ID to run job as (user root only)\n"
"      --get-user-env          used by Moab.  See srun man page.\n"
"\n"
"Constraint options:\n"
"      --mincpus=n             minimum number of cpus per node\n"
"      --minsockets=n          minimum number of sockets per node\n"
"      --mincores=n            minimum number of cores per cpu\n"
"      --minthreads=n          minimum number of threads per core\n"
"      --mem=MB                minimum amount of real memory\n"
"      --tmp=MB                minimum amount of temporary disk\n"
"      --contiguous            demand a contiguous range of nodes\n"
"  -C, --constraint=list       specify a list of constraints\n"
"  -F, --nodefile=filename     request a specific list of hosts\n"
"  -w, --nodelist=hosts...     request a specific list of hosts\n"
"  -x, --exclude=hosts...      exclude a specific list of hosts\n"
"\n"
"Consumable resources related options:\n" 
"      --exclusive             allocate nodes in exclusive mode when\n" 
"                              cpu consumable resource is enabled\n"
"      --mem-per-cpu=MB        maximum amount of real memory per allocated\n"
"                              cpu required by the job.\n" 
"                              --mem >= --job-mem if --mem is specified.\n" 
"\n"
"Affinity/Multi-core options: (when the task/affinity plugin is enabled)\n" 
"  -B --extra-node-info=S[:C[:T]]            Expands to:\n"
"      --sockets-per-node=S    number of sockets per node to allocate\n"
"      --cores-per-socket=C    number of cores per socket to allocate\n"
"      --threads-per-core=T    number of threads per core to allocate\n"
"                              each field can be 'min[-max]' or wildcard '*'\n"
"                              total cpus requested = (N x S x C x T)\n"
"\n"
"      --ntasks-per-socket=n   number of tasks to invoke on each socket\n"
"      --ntasks-per-core=n     number of tasks to invoke on each core\n");
	conf = slurm_conf_lock();
	if (conf->task_plugin != NULL
	    && strcasecmp(conf->task_plugin, "task/affinity") == 0) {
		printf(
"      --hint=                 Bind tasks according to application hints\n"
"                              (see \"--hint=help\" for options)\n");
	}
	slurm_conf_unlock();

        printf("\n"
#ifdef HAVE_AIX				/* AIX/Federation specific options */
"AIX related options:\n"
"  --network=type              communication protocol to be used\n"
"\n"
#endif
#ifdef HAVE_BG				/* Blue gene specific options */
"\n"
"Blue Gene related options:\n"
"  -g, --geometry=XxYxZ        geometry constraints of the job\n"
"  -R, --no-rotate             disable geometry rotation\n"
"      --reboot                reboot nodes before starting job\n"
"      --conn-type=type        constraint on type of connection, MESH or TORUS\n"
"                              if not set, then tries to fit TORUS else MESH\n"
#ifndef HAVE_BGL
"                              If wanting to run in HTC mode (only for 1\n"
"                              midplane and below).  You can use HTC_S for\n"
"                              SMP, HTC_D for Dual, HTC_V for\n"
"                              virtual node mode, and HTC_L for Linux mode.\n" 
#endif
"      --blrts-image=path      path to blrts image for bluegene block.  Default if not set\n"
"      --linux-image=path      path to linux image for bluegene block.  Default if not set\n"
"      --mloader-image=path    path to mloader image for bluegene block.  Default if not set\n"
"      --ramdisk-image=path    path to ramdisk image for bluegene block.  Default if not set\n"
"\n"
#endif
"Help options:\n"
"  -h, --help                  show this help message\n"
"  -u, --usage                 display brief usage message\n"
"\n"
"Other options:\n"
"  -V, --version               output version information and exit\n"
"\n"
);

}
