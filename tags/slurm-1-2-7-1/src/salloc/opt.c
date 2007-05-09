/*****************************************************************************\
 *  opt.c - options processing for salloc
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
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

/*---- global variables, defined in opt.h ----*/
opt_t opt;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

/* return command name from its full path name */
static char * _base_name(char* command);

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
static char *_print_mail_type(const uint16_t type);
static int _parse_signal(const char *signal_name);
static long  _to_bytes(const char *arg);
static void  _usage(void);
static bool  _verify_node_count(const char *arg, int *min, int *max);
static int   _verify_geometry(const char *arg, uint16_t *geometry);
static int   _verify_conn_type(const char *arg);

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

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

/*
 * verify that a connection type in arg is of known form
 * returns the connection_type or -1 if not recognized
 */
static int _verify_conn_type(const char *arg)
{
	int len = strlen(arg);

	if (!strncasecmp(arg, "MESH", len))
		return SELECT_MESH;
	else if (!strncasecmp(arg, "TORUS", len))
		return SELECT_TORUS;
	else if (!strncasecmp(arg, "NAV", len))
		return SELECT_NAV;

	error("invalid --conn-type argument %s ignored.", arg);
	return -1;
}

/*
 * verify geometry arguments, must have proper count
 * returns -1 on error, 0 otherwise
 */
static int _verify_geometry(const char *arg, uint16_t *geometry)
{
	char* token, *delimiter = ",x", *next_ptr;
	int i, rc = 0;
	char* geometry_tmp = xstrdup(arg);
	char* original_ptr = geometry_tmp;

	token = strtok_r(geometry_tmp, delimiter, &next_ptr);
	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		if (token == NULL) {
			error("insufficient dimensions in --geometry");
			rc = -1;
			break;
		}
		geometry[i] = (uint16_t)atoi(token);
		if (geometry[i] == 0 || geometry[i] == (uint16_t)NO_VAL) {
			error("invalid --geometry argument");
			rc = -1;
			break;
		}
		geometry_tmp = next_ptr;
		token = strtok_r(geometry_tmp, delimiter, &next_ptr);
	}
	if (token != NULL) {
		error("too many dimensions in --geometry");
		rc = -1;
	}

	if (original_ptr)
		xfree(original_ptr);

	return rc;
}

/* Convert a string into a node count */
static int
_str_to_nodes(const char *num_str, char **leftover)
{
	long int num;
	char *endptr;

	num = strtol(num_str, &endptr, 10);
	if (endptr == num_str) { /* no valid digits */
		*leftover = (char *)num_str;
		return 0;
	} 
	if (*endptr != '\0' && (*endptr == 'k' || *endptr == 'K')) {
		num *= 1024;
		endptr++;
	}
	*leftover = endptr;

	return (int)num;
}

/* 
 * verify that a node count in arg is of a known form (count or min-max)
 * OUT min, max specified minimum and maximum node counts
 * RET true if valid
 */
static bool 
_verify_node_count(const char *arg, int *min_nodes, int *max_nodes)
{
	char *ptr, *min_str, *max_str;
	char *leftover;
	
	/* Does the string contain a "-" character?  If so, treat as a range.
	 * otherwise treat as an absolute node count. */
	if ((ptr = index(arg, '-')) != NULL) {
		min_str = xstrndup(arg, ptr-arg);
		*min_nodes = _str_to_nodes(min_str, &leftover);
		if (!xstring_is_whitespace(leftover)) {
			error("\"%s\" is not a valid node count", min_str);
			xfree(min_str);
			return false;
		}
		xfree(min_str);
		if (*min_nodes == 0)
			*min_nodes = 1;

		max_str = xstrndup(ptr+1, strlen(arg)-((ptr+1)-arg));
		*max_nodes = _str_to_nodes(max_str, &leftover);
		if (!xstring_is_whitespace(leftover)) {
			error("\"%s\" is not a valid node count", max_str);
			xfree(max_str);
			return false;
		}
		xfree(max_str);
	} else {
		*min_nodes = *max_nodes = _str_to_nodes(arg, &leftover);
		if (!xstring_is_whitespace(leftover)) {
			error("\"%s\" is not a valid node count", arg);
			return false;
		}
		if (*min_nodes == 0) {
			/* whitespace does not a valid node count make */
			error("\"%s\" is not a valid node count", arg);
			return false;
		}
	}

	if ((*max_nodes != 0) && (*max_nodes < *min_nodes)) {
		error("Maximum node count %d is less than"
		      " minimum node count %d",
		      *max_nodes, *min_nodes);
		return false;
	}

	return true;
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

	opt.job_name = NULL;
	opt.jobid = NO_VAL;
	opt.dependency = NO_VAL;
	opt.account  = NULL;
	opt.comment  = NULL;

	opt.shared = (uint16_t)NO_VAL;
	opt.no_kill = false;
	opt.kill_command_signal = SIGTERM;
	opt.kill_command_signal_set = false;

	opt.immediate	= false;
	opt.max_wait	= 0;

	opt.quiet = 0;
	opt.verbose = 0;

	/* constraint default (-1 is no constraint) */
	opt.mincpus	    = -1;
	opt.minsockets      = -1;
	opt.mincores        = -1;
	opt.minthreads      = -1;
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
	opt.conn_type	    = -1;

	opt.euid	    = (uid_t) -1;
	opt.egid	    = (gid_t) -1;
	
	opt.bell            = BELL_AFTER_DELAY;
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
  {"SALLOC_TIMELIMIT",     OPT_INT,        &opt.time_limit,    NULL           },
  {"SALLOC_WAIT",          OPT_INT,        &opt.max_wait,      NULL           },
  {"SALLOC_BELL",          OPT_BELL,       NULL,               NULL           },
  {"SALLOC_NO_BELL",       OPT_NO_BELL,    NULL,               NULL           },
  {"SALLOC_EXCLUSIVE",     OPT_EXCLUSIVE,  NULL,               NULL           },
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
		opt.nodes_set = _verify_node_count( val, 
						    &opt.min_nodes, 
						    &opt.max_nodes );
		if (opt.nodes_set == false) {
			error("invalid node count in env variable, ignoring");
		}
		break;

	case OPT_CONN_TYPE:
		opt.conn_type = _verify_conn_type(val);
		break;

	case OPT_NO_ROTATE:
		opt.no_rotate = true;
		break;

	case OPT_GEOMETRY:
		if (_verify_geometry(val, opt.geometry)) {
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
		{"cpus-per-task", required_argument, 0, 'c'},
		{"constraint",    required_argument, 0, 'C'},
		{"dependency",    required_argument, 0, 'd'},
		{"nodefile",      required_argument, 0, 'F'},
		{"geometry",      required_argument, 0, 'g'},
		{"help",          no_argument,       0, 'h'},
		{"hold",          no_argument,       0, 'H'},
		{"immediate",     no_argument,       0, 'I'},
		{"job-name",      required_argument, 0, 'J'},
		{"no-kill",       no_argument,       0, 'k'},
		{"kill-command",  optional_argument, 0, 'K'},
		{"tasks",         required_argument, 0, 'n'},
		{"nodes",         required_argument, 0, 'N'},
		{"partition",     required_argument, 0, 'p'},
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
		{NULL,            0,                 0, 0}
	};
	char *opt_string = "+a:c:C:d:F:g:hHIJ:kK::n:N:p:qR:st:uU:vVw:W:x:";

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
		case 'c':
			opt.cpus_set = true;
			opt.cpus_per_task = 
				_get_int(optarg, "cpus-per-task");
			break;
		case 'C':
			xfree(opt.constraints);
			opt.constraints = xstrdup(optarg);
			break;
		case 'd':
			opt.dependency = _get_int(optarg, "dependency");
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
			if (_verify_geometry(optarg, opt.geometry))
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
		case 'n':
			opt.nprocs_set = true;
			opt.nprocs = 
				_get_int(optarg, "number of tasks");
			break;
		case 'N':
			opt.nodes_set = 
				_verify_node_count(optarg, 
						   &opt.min_nodes,
						   &opt.max_nodes);
			if (opt.nodes_set == false) {
				exit(1);
			}
			break;
		case 'p':
			xfree(opt.partition);
			opt.partition = xstrdup(optarg);
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
			opt.time_limit = _get_int(optarg, "time");
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
			_print_version();
			exit(0);
			break;
		case 'w':
			xfree(opt.nodelist);
			opt.nodelist = xstrdup(optarg);
#ifdef HAVE_BG
			info("\tThe nodelist option should only be used if\n"
			     "\tthe block you are asking for can be created.\n"
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
		case LONG_OPT_UID:
			opt.euid = uid_from_string (optarg);
			if (opt.euid == (uid_t) -1)
				fatal ("--uid=\"%s\" invalid", optarg);
			break;
		case LONG_OPT_GID:
			opt.egid = gid_from_string (optarg);
			if (opt.egid == (gid_t) -1)
				fatal ("--gid=\"%s\" invalid", optarg);
			break;
		case LONG_OPT_CONNTYPE:
			opt.conn_type = _verify_conn_type(optarg);
			break;
		case LONG_OPT_BEGIN:
			opt.begin = parse_time(optarg);
			if (opt.begin == 0) {
				error("Invalid time specification %s",
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_MAIL_TYPE:
			opt.mail_type |= _parse_mail_type(optarg);
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
		case LONG_OPT_REBOOT:
			opt.reboot = true;
			break;
		default:
			fatal("Unrecognized command line parameter %c",
			      opt_char);
		}
	}
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

	if ((opt.job_name == NULL) && (command_argc > 0))
		opt.job_name = _base_name(command_argv[0]);

	if (command_argc == 0) {
		error("A local command is a required parameter!");
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

	if ((opt.min_nodes < 0) || (opt.max_nodes < 0) || 
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

	if (opt.time_limit == 0)
		opt.time_limit = INFINITE;

	if ((opt.euid != (uid_t) -1) && (opt.euid != opt.uid)) 
		opt.uid = opt.euid;

	if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid)) 
		opt.gid = opt.egid;

        if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid))
	        opt.gid = opt.egid;

	return verified;
}

static uint16_t _parse_mail_type(const char *arg)
{
	uint16_t rc;

	if (strcasecmp(arg, "BEGIN") == 0)
		rc = MAIL_JOB_BEGIN;
	else if  (strcasecmp(arg, "END") == 0)
		rc = MAIL_JOB_END;
	else if (strcasecmp(arg, "FAIL") == 0)
		rc = MAIL_JOB_FAIL;
	else if (strcasecmp(arg, "ALL") == 0)
		rc = MAIL_JOB_BEGIN |  MAIL_JOB_END |  MAIL_JOB_FAIL;
	else
		rc = 0;		/* failure */

	return rc;
}
static char *_print_mail_type(const uint16_t type)
{
	if (type == 0)
		return "NONE";

	if (type == MAIL_JOB_BEGIN)
		return "BEGIN";
	if (type == MAIL_JOB_END)
		return "END";
	if (type == MAIL_JOB_FAIL)
		return "FAIL";
	if (type == (MAIL_JOB_BEGIN |  MAIL_JOB_END |  MAIL_JOB_FAIL))
		return "ALL";

	return "MULTIPLE";
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
	for (i = 0; i < command_argc; i++)
		snprintf(buf, 256,  "%s", command_argv[i]);
	return xstrdup(buf);
}

static char *
print_geometry()
{
	int i;
	char buf[32], *rc = NULL;

	if ((SYSTEM_DIMENSIONS == 0)
	||  (opt.geometry[0] == (uint16_t)NO_VAL))
		return NULL;

	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		if (i > 0)
			snprintf(buf, sizeof(buf), "x%u", opt.geometry[i]);
		else
			snprintf(buf, sizeof(buf), "%u", opt.geometry[i]);
		xstrcat(rc, buf);
	}

	return rc;
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
	if (opt.jobid != NO_VAL)
		info("jobid          : %u", opt.jobid);
	info("verbose        : %d", opt.verbose);
	info("immediate      : %s", tf_(opt.immediate));
	if (opt.time_limit == INFINITE)
		info("time_limit     : INFINITE");
	else
		info("time_limit     : %d", opt.time_limit);
	info("wait           : %d", opt.max_wait);
	if (opt.nice)
		info("nice           : %d", opt.nice);
	info("account        : %s", opt.account);
	info("comment        : %s", opt.comment);
	if (opt.dependency == NO_VAL)
		info("dependency     : none");
	else
		info("dependency     : %u", opt.dependency);
	str = print_constraints();
	info("constraints    : %s", str);
	xfree(str);
	if (opt.conn_type >= 0)
		info("conn_type      : %u", opt.conn_type);
	str = print_geometry();
	info("geometry       : %s", str);
	xfree(str);
	info("reboot         : %s", opt.reboot ? "no" : "yes");
	info("rotate         : %s", opt.no_rotate ? "yes" : "no");
	if (opt.begin) {
		char time_str[32];
		slurm_make_time_str(&opt.begin, time_str, sizeof(time_str));
		info("begin          : %s", time_str);
	}
	info("mail_type      : %s", _print_mail_type(opt.mail_type));
	info("mail_user      : %s", opt.mail_user);
	str = print_commandline();
	info("user command   : `%s'", str);
	xfree(str);

}

static void _usage(void)
{
 	printf(
"Usage: salloc [-N numnodes|[min nodes]-[max nodes]] [-n num-processors]\n"
"              [[-c cpus-per-node] [-r n] [-p partition] [--hold] [-t minutes]\n"
"              [--immediate] [--no-kill]\n"
"              [--share] [-J jobname] [--jobid=id]\n"
"              [--verbose]\n"
"              [-W sec] [--minsockets=n] [--mincores=n] [--minthreads=n]\n"
"              [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"              [--account=name] [--dependency=jobid] [--comment=name]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
"              [--geometry=XxYxZ] [--conn-type=type] [--no-rotate] [ --reboot]\n"
#endif
"              [--mail-type=type] [--mail-user=user][--nice[=value]]\n"
"              [-w hosts...] [-x hosts...] executable [args...]\n");
}

static void _help(void)
{
        printf (
"Usage: salloc [OPTIONS...] executable [args...]\n"
"\n"
"Parallel run options:\n"
"  -N, --nodes=N               number of nodes on which to run (N = min[-max])\n"
"  -n, --procs=N               number of processors required\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"  -p, --partition=partition   partition requested\n"
"  -H, --hold                  submit job in held state\n"
"  -t, --time=minutes          time limit\n"
"  -I, --immediate             exit if resources are not immediately available\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -s, --share                 share nodes with other jobs\n"
"  -J, --job-name=jobname      name of job\n"
"      --jobid=id              specify jobid to use\n"
"  -W, --wait=sec              seconds to wait for allocation if not\n"
"                              immediately available\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -q, --quiet                 quiet mode (suppress informational messages)\n"
"  -d, --dependency=jobid      defer job until specified jobid completes\n"
"      --nice[=value]          decrease secheduling priority by value\n"
"  -U, --account=name          charge job to specified account\n"
"      --begin=time            defer job until HH:MM DD/MM/YY\n"
"      --comment=name          arbitrary comment\n"
"      --mail-type=type        notify on state change: BEGIN, END, FAIL or ALL\n"
"      --mail-user=user        who to send email notification for job state changes\n"
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
"  -w, --nodelist=hosts...     request a specific list of hosts\n"
"  -x, --exclude=hosts...      exclude a specific list of hosts\n"
"\n"
"Consumable resources related options:\n" 
"      --exclusive             allocate nodes in exclusive mode when\n" 
"                              cpu consumable resource is enabled\n"
"\n"
#ifdef HAVE_BG				/* Blue gene specific options */
  "Blue Gene related options:\n"
  "  -g, --geometry=XxYxZ        geometry constraints of the job\n"
  "  -R, --no-rotate             disable geometry rotation\n"
  "      --reboot                reboot nodes before starting job\n"
  "      --conn-type=type        constraint on type of connection, MESH or TORUS\n"
  "                              if not set, then tries to fit TORUS else MESH\n"
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
