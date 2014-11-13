/*****************************************************************************\
 *  opt.c - options processing for salloc
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
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

#ifdef HAVE_LIMITS_H
#  include <limits.h>
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
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h" /* contains getnodename() */
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/util-net.h"
#include "src/salloc/salloc.h"
#include "src/salloc/opt.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_DEBUG       0x03
#define OPT_NODES       0x04
#define OPT_BOOL        0x05
#define OPT_CORE        0x06
#define OPT_CONN_TYPE	0x07
#define OPT_NO_ROTATE	0x08
#define OPT_GEOMETRY	0x09
#define OPT_BELL        0x0a
#define OPT_NO_BELL     0x0b
#define OPT_JOBID       0x0c
#define OPT_EXCLUSIVE   0x0d
#define OPT_OVERCOMMIT  0x0e
#define OPT_ACCTG_FREQ  0x0f

#define OPT_MEM_BIND    0x11
#define OPT_IMMEDIATE   0x12
#define OPT_WCKEY       0x14
#define OPT_SIGNAL      0x15
#define OPT_KILL_CMD    0x16
#define OPT_TIME_VAL	0x17
#define OPT_PROFILE     0x18
#define OPT_CORE_SPEC   0x19
#define OPT_HINT	0x1a

/* generic getopt_long flags, integers and *not* valid characters */

#define LONG_OPT_MEM_BIND    0x102
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
#define LONG_OPT_QOS             0x127
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
#define LONG_OPT_RESERVATION     0x13e
#define LONG_OPT_SIGNAL          0x13f
#define LONG_OPT_TIME_MIN        0x140
#define LONG_OPT_GRES            0x141
#define LONG_OPT_WAIT_ALL_NODES  0x142
#define LONG_OPT_REQ_SWITCH      0x143
#define LONG_OPT_PROFILE         0x144
#define LONG_OPT_PRIORITY        0x160


/*---- global variables, defined in opt.h ----*/
opt_t opt;
int error_exit = 1;
int immediate_exit = 1;

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
 * If the node list supplied is a file name, translate that into
 *	a list of nodes, we orphan the data pointed to
 * RET true if the node list is a valid one
 */
static bool _valid_node_list(char **node_list_pptr)
{
	int count = NO_VAL;

	/* If we are using Arbitrary and we specified the number of
	   procs to use then we need exactly this many since we are
	   saying, lay it out this way!  Same for max and min nodes.
	   Other than that just read in as many in the hostfile */
	if (opt.ntasks_set)
		count = opt.ntasks;
	else if (opt.nodes_set) {
		if (opt.max_nodes)
			count = opt.max_nodes;
		else if (opt.min_nodes)
			count = opt.min_nodes;
	}

	return verify_node_list(node_list_pptr, opt.distribution, count);
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
	int i;
	uid_t uid = getuid();

	opt.user = uid_to_string(uid);
	if (strcmp(opt.user, "nobody") == 0)
		fatal("Invalid user id: %u", uid);

	opt.uid = uid;
	opt.gid = getgid();

	opt.cwd = NULL;
	opt.progname = NULL;

	opt.ntasks = 1;
	opt.ntasks_set = false;
	opt.cpus_per_task = 0;
	opt.cpus_set = false;
	opt.min_nodes = 1;
	opt.max_nodes = 0;
	opt.nodes_set = false;
	opt.sockets_per_node = NO_VAL; /* requested sockets */
	opt.cores_per_socket = NO_VAL; /* requested cores */
	opt.threads_per_core = NO_VAL; /* requested threads */
	opt.ntasks_per_node      = 0;  /* ntask max limits */
	opt.ntasks_per_socket    = NO_VAL;
	opt.ntasks_per_core      = NO_VAL;
	opt.mem_bind_type = 0;
	opt.mem_bind = NULL;
	opt.core_spec = (uint16_t) NO_VAL;
	opt.time_limit = NO_VAL;
	opt.time_limit_str = NULL;
	opt.time_min = NO_VAL;
	opt.time_min_str = NULL;
	opt.partition = NULL;
	opt.profile   = ACCT_GATHER_PROFILE_NOT_SET;

	opt.job_name = NULL;
	opt.jobid = NO_VAL;
	opt.dependency = NULL;
	opt.account  = NULL;
	opt.comment  = NULL;
	opt.qos      = NULL;

	opt.distribution = SLURM_DIST_UNKNOWN;
	opt.plane_size   = NO_VAL;

	opt.shared = (uint16_t)NO_VAL;
	opt.no_kill = false;
	opt.kill_command_signal = SIGTERM;
	opt.kill_command_signal_set = false;

	opt.immediate	= 0;
	opt.overcommit	= false;

	opt.quiet = 0;
	opt.verbose = 0;
	opt.warn_flags  = 0;
	opt.warn_signal = 0;
	opt.warn_time   = 0;

	/* constraint default (-1 is no constraint) */
	opt.mincpus	    = -1;
	opt.mem_per_cpu	    = -1;
	opt.realmem	    = -1;
	opt.tmpdisk	    = -1;

	opt.hold	    = false;
	opt.constraints	    = NULL;
	opt.gres            = NULL;
	opt.contiguous	    = false;
	opt.nodelist	    = NULL;
	opt.exc_nodes	    = NULL;

	for (i=0; i<HIGHEST_DIMENSIONS; i++) {
		opt.conn_type[i]    = (uint16_t) NO_VAL;
		opt.geometry[i]	    = 0;
	}
	opt.reboot          = false;
	opt.no_rotate	    = false;

	opt.euid	    = (uid_t) -1;
	opt.egid	    = (gid_t) -1;

	opt.bell            = BELL_AFTER_DELAY;
	opt.acctg_freq      = NULL;
	opt.no_shell	    = false;
	opt.get_user_env_time = -1;
	opt.get_user_env_mode = -1;
	opt.reservation     = NULL;
	opt.wait_all_nodes  = (uint16_t) NO_VAL;
	opt.wckey           = NULL;
	opt.req_switch      = -1;
	opt.wait4switch     = -1;

	opt.nice = 0;
	opt.priority = 0;
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
  {"SALLOC_ACCOUNT",       OPT_STRING,     &opt.account,       NULL          },
  {"SALLOC_ACCTG_FREQ",    OPT_STRING,     &opt.acctg_freq,    NULL          },
  {"SALLOC_BELL",          OPT_BELL,       NULL,               NULL          },
  {"SALLOC_CONN_TYPE",     OPT_CONN_TYPE,  NULL,               NULL          },
  {"SALLOC_CORE_SPEC",     OPT_INT,        &opt.core_spec,     NULL          },
  {"SALLOC_DEBUG",         OPT_DEBUG,      NULL,               NULL          },
  {"SALLOC_EXCLUSIVE",     OPT_EXCLUSIVE,  NULL,               NULL          },
  {"SALLOC_GEOMETRY",      OPT_GEOMETRY,   NULL,               NULL          },
  {"SALLOC_IMMEDIATE",     OPT_IMMEDIATE,  NULL,               NULL          },
  {"SALLOC_HINT",          OPT_HINT,       NULL,               NULL          },
  {"SLURM_HINT",           OPT_HINT,       NULL,               NULL          },
  {"SALLOC_JOBID",         OPT_JOBID,      NULL,               NULL          },
  {"SALLOC_KILL_CMD",      OPT_KILL_CMD,   NULL,               NULL          },
  {"SALLOC_MEM_BIND",      OPT_MEM_BIND,   NULL,               NULL          },
  {"SALLOC_NETWORK",       OPT_STRING    , &opt.network,       NULL          },
  {"SALLOC_NO_BELL",       OPT_NO_BELL,    NULL,               NULL          },
  {"SALLOC_NO_ROTATE",     OPT_NO_ROTATE,  NULL,               NULL          },
  {"SALLOC_OVERCOMMIT",    OPT_OVERCOMMIT, NULL,               NULL          },
  {"SALLOC_PARTITION",     OPT_STRING,     &opt.partition,     NULL          },
  {"SALLOC_PROFILE",       OPT_PROFILE,    NULL,               NULL          },
  {"SALLOC_QOS",           OPT_STRING,     &opt.qos,           NULL          },
  {"SALLOC_RESERVATION",   OPT_STRING,     &opt.reservation,   NULL          },
  {"SALLOC_SIGNAL",        OPT_SIGNAL,     NULL,               NULL          },
  {"SALLOC_TIMELIMIT",     OPT_STRING,     &opt.time_limit_str,NULL          },
  {"SALLOC_WAIT",          OPT_IMMEDIATE,  NULL,               NULL          },
  {"SALLOC_WAIT_ALL_NODES",OPT_INT,        &opt.wait_all_nodes,NULL          },
  {"SALLOC_WCKEY",         OPT_STRING,     &opt.wckey,         NULL          },
  {"SALLOC_REQ_SWITCH",    OPT_INT,        &opt.req_switch,    NULL          },
  {"SALLOC_WAIT4SWITCH",   OPT_TIME_VAL,   NULL,               NULL          },
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
			if (!(end && *end == '\0')) {
				error("%s=%s invalid. ignoring...",
				      e->var, val);
			}
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
		verify_conn_type(val, opt.conn_type);
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

	case OPT_IMMEDIATE:
		if (val)
			opt.immediate = strtol(val, NULL, 10);
		else
			opt.immediate = DEFAULT_IMMEDIATE;
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
	case OPT_HINT:
		/* Keep after other options filled in */
		if (verify_hint(val,
				&opt.sockets_per_node,
				&opt.cores_per_socket,
				&opt.threads_per_core,
				&opt.ntasks_per_core,
				NULL)) {
			exit(error_exit);
		}
		break;
	case OPT_MEM_BIND:
		if (slurm_verify_mem_bind(val, &opt.mem_bind,
					  &opt.mem_bind_type))
			exit(error_exit);
		break;
	case OPT_WCKEY:
		xfree(opt.wckey);
		opt.wckey = xstrdup(val);
		break;
	case OPT_SIGNAL:
		if (get_signal_opts((char *)val, &opt.warn_signal,
				    &opt.warn_time, &opt.warn_flags)) {
			error("Invalid signal specification: %s", val);
			exit(error_exit);
		}
		break;
	case OPT_KILL_CMD:
		if (val) {
			opt.kill_command_signal = sig_name2num((char *) val);
			if (opt.kill_command_signal == 0) {
				error("Invalid signal name %s", val);
				exit(error_exit);
			}
		}
		opt.kill_command_signal_set = true;
		break;

	case OPT_TIME_VAL:
		opt.wait4switch = time_str2secs(val);
		break;
	case OPT_PROFILE:
		opt.profile = acct_gather_profile_from_string((char *)val);
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
		exit(error_exit);
	}

	if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
	}

	return (int) result;
}

void set_options(const int argc, char **argv)
{
	int opt_char, option_index = 0, max_val = 0;
	char *tmp;
	static struct option long_options[] = {
		{"account",       required_argument, 0, 'A'},
		{"extra-node-info", required_argument, 0, 'B'},
		{"cpus-per-task", required_argument, 0, 'c'},
		{"constraint",    required_argument, 0, 'C'},
		{"dependency",    required_argument, 0, 'd'},
		{"chdir",         required_argument, 0, 'D'},
		{"nodefile",      required_argument, 0, 'F'},
		{"geometry",      required_argument, 0, 'g'},
		{"help",          no_argument,       0, 'h'},
		{"hold",          no_argument,       0, 'H'},
		{"immediate",     optional_argument, 0, 'I'},
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
		{"quiet",         no_argument,       0, 'Q'},
		{"no-rotate",     no_argument,       0, 'R'},
		{"share",         no_argument,       0, 's'},
		{"core-spec",     required_argument, 0, 'S'},
		{"time",          required_argument, 0, 't'},
		{"usage",         no_argument,       0, 'u'},
		{"verbose",       no_argument,       0, 'v'},
		{"version",       no_argument,       0, 'V'},
		{"nodelist",      required_argument, 0, 'w'},
		{"wait",          required_argument, 0, 'W'},
		{"exclude",       required_argument, 0, 'x'},
		{"acctg-freq",    required_argument, 0, LONG_OPT_ACCTG_FREQ},
		{"begin",         required_argument, 0, LONG_OPT_BEGIN},
		{"bell",          no_argument,       0, LONG_OPT_BELL},
		{"blrts-image",   required_argument, 0, LONG_OPT_BLRTS_IMAGE},
		{"cnload-image",  required_argument, 0, LONG_OPT_LINUX_IMAGE},
		{"comment",       required_argument, 0, LONG_OPT_COMMENT},
		{"conn-type",     required_argument, 0, LONG_OPT_CONNTYPE},
		{"contiguous",    no_argument,       0, LONG_OPT_CONT},
		{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
		{"exclusive",     no_argument,       0, LONG_OPT_EXCLUSIVE},
		{"get-user-env",  optional_argument, 0, LONG_OPT_GET_USER_ENV},
		{"gid",           required_argument, 0, LONG_OPT_GID},
		{"gres",          required_argument, 0, LONG_OPT_GRES},
		{"hint",          required_argument, 0, LONG_OPT_HINT},
		{"ioload-image",  required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
		{"jobid",         required_argument, 0, LONG_OPT_JOBID},
		{"linux-image",   required_argument, 0, LONG_OPT_LINUX_IMAGE},
		{"mail-type",     required_argument, 0, LONG_OPT_MAIL_TYPE},
		{"mail-user",     required_argument, 0, LONG_OPT_MAIL_USER},
		{"mem",           required_argument, 0, LONG_OPT_MEM},
		{"mem-per-cpu",   required_argument, 0, LONG_OPT_MEM_PER_CPU},
		{"mem_bind",      required_argument, 0, LONG_OPT_MEM_BIND},
		{"mincores",      required_argument, 0, LONG_OPT_MINCORES},
		{"mincpus",       required_argument, 0, LONG_OPT_MINCPU},
		{"minsockets",    required_argument, 0, LONG_OPT_MINSOCKETS},
		{"minthreads",    required_argument, 0, LONG_OPT_MINTHREADS},
		{"mloader-image", required_argument, 0, LONG_OPT_MLOADER_IMAGE},
		{"network",       required_argument, 0, LONG_OPT_NETWORK},
		{"nice",          optional_argument, 0, LONG_OPT_NICE},
		{"priority",      required_argument, 0, LONG_OPT_PRIORITY},
		{"no-bell",       no_argument,       0, LONG_OPT_NO_BELL},
		{"no-shell",      no_argument,       0, LONG_OPT_NOSHELL},
		{"ntasks-per-core",  required_argument, 0, LONG_OPT_NTASKSPERCORE},
		{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
		{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
		{"qos",		  required_argument, 0, LONG_OPT_QOS},
		{"profile",       required_argument, 0, LONG_OPT_PROFILE},
		{"ramdisk-image", required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
		{"reboot",	  no_argument,       0, LONG_OPT_REBOOT},
		{"reservation",   required_argument, 0, LONG_OPT_RESERVATION},
		{"signal",        required_argument, 0, LONG_OPT_SIGNAL},
		{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
		{"tasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
		{"time-min",      required_argument, 0, LONG_OPT_TIME_MIN},
		{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
		{"tmp",           required_argument, 0, LONG_OPT_TMP},
		{"uid",           required_argument, 0, LONG_OPT_UID},
		{"wait-all-nodes",required_argument, 0, LONG_OPT_WAIT_ALL_NODES},
		{"wckey",         required_argument, 0, LONG_OPT_WCKEY},
		{"switches",      required_argument, 0, LONG_OPT_REQ_SWITCH},
		{NULL,            0,                 0, 0}
	};
	char *opt_string =
		"+A:B:c:C:d:D:F:g:hHIJ:kK::L:m:n:N:Op:P:QRsS:t:uU:vVw:W:x:";
	char *pos_delimit;

	struct option *optz = spank_option_table_create(long_options);

	if (!optz) {
		error("Unable to create options table");
		exit(error_exit);
	}

	opt.progname = xbasename(argv[0]);
	optind = 0;
	while((opt_char = getopt_long(argc, argv, opt_string,
				      optz, &option_index)) != -1) {
		switch (opt_char) {

		case '?':
			fprintf(stderr, "Try \"salloc --help\" for more "
				"information\n");
			exit(error_exit);
			break;
		case 'A':
		case 'U':	/* backwards compatibility */
			xfree(opt.account);
			opt.account = xstrdup(optarg);
			break;
		case 'B':
			opt.extra_set = verify_socket_core_thread_count(
						optarg,
						&opt.sockets_per_node,
						&opt.cores_per_socket,
						&opt.threads_per_core,
						NULL);

			if (opt.extra_set == false) {
				error("invalid resource allocation -B `%s'",
					optarg);
				exit(error_exit);
			}
			break;
		case 'c':
			opt.cpus_set = true;
			opt.cpus_per_task = _get_int(optarg, "cpus-per-task");
			break;
		case 'C':
			xfree(opt.constraints);
			opt.constraints = xstrdup(optarg);
			break;
		case 'd':
			xfree(opt.dependency);
			opt.dependency = xstrdup(optarg);
			break;
		case 'D':
			xfree(opt.cwd);
			if (is_full_path(optarg))
				opt.cwd = xstrdup(optarg);
			else
				opt.cwd = make_full_path(optarg);
			break;
		case 'F':
			xfree(opt.nodelist);
			tmp = slurm_read_hostfile(optarg, 0);
			if (tmp != NULL) {
				opt.nodelist = xstrdup(tmp);
				free(tmp);
			} else {
				error("\"%s\" is not a valid node file",
				      optarg);
				exit(error_exit);
			}
			break;
		case 'g':
			if (verify_geometry(optarg, opt.geometry))
				exit(error_exit);
			break;
		case 'h':
			_help();
			exit(0);
		case 'H':
			opt.hold = true;
			break;
		case 'I':
			if (optarg)
				opt.immediate = _get_int(optarg, "immediate");
			else
				opt.immediate = DEFAULT_IMMEDIATE;
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
				opt.kill_command_signal = sig_name2num(optarg);
				if (opt.kill_command_signal == 0) {
					error("Invalid signal name %s", optarg);
					exit(error_exit);
				}
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
				exit(error_exit);
			}
			break;
		case 'n':
			opt.ntasks_set = true;
			opt.ntasks =
				_get_int(optarg, "number of tasks");
			break;
		case 'N':
			opt.nodes_set =
				verify_node_count(optarg,
						  &opt.min_nodes,
						  &opt.max_nodes);
			if (opt.nodes_set == false) {
				exit(error_exit);
			}
			break;
		case 'O':
			opt.overcommit = true;
			break;
		case 'p':
			xfree(opt.partition);
			opt.partition = xstrdup(optarg);
			break;
		case 'P':
			verbose("-P option is deprecated, use -d instead");
			xfree(opt.dependency);
			opt.dependency = xstrdup(optarg);
			break;
		case 'Q':
			opt.quiet++;
			break;
		case 'R':
			opt.no_rotate = true;
			break;
		case 's':
			opt.shared = 1;
			break;
		case 'S':
			opt.core_spec = _get_int(optarg, "core_spec");
			break;
		case 't':
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(optarg);
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
			verbose("wait option has been deprecated, use "
				"immediate option");
			opt.immediate = _get_int(optarg, "wait");
			break;
		case 'x':
			xfree(opt.exc_nodes);
			opt.exc_nodes = xstrdup(optarg);
			if (!_valid_node_list(&opt.exc_nodes))
				exit(error_exit);
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
				exit(error_exit);
			}
			break;
		case LONG_OPT_MINCORES:
			verbose("mincores option has been deprecated, use "
				"cores-per-socket");
			opt.cores_per_socket = _get_int(optarg, "mincores");
			if (opt.cores_per_socket < 0) {
				error("invalid mincores constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MINSOCKETS:
			verbose("minsockets option has been deprecated, use "
				"sockets-per-node");
			opt.sockets_per_node = _get_int(optarg, "minsockets");
			if (opt.sockets_per_node < 0) {
				error("invalid minsockets constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MINTHREADS:
			verbose("minthreads option has been deprecated, use "
				"threads-per-core");
			opt.threads_per_core = _get_int(optarg, "minthreads");
			if (opt.threads_per_core < 0) {
				error("invalid minthreads constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MEM:
			opt.realmem = (int) str_to_mbytes(optarg);
			if (opt.realmem < 0) {
				error("invalid memory constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MEM_PER_CPU:
			opt.mem_per_cpu = (int) str_to_mbytes(optarg);
			if (opt.mem_per_cpu < 0) {
				error("invalid memory constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_TMP:
			opt.tmpdisk = str_to_mbytes(optarg);
			if (opt.tmpdisk < 0) {
				error("invalid tmp value %s", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_UID:
			if (opt.euid != (uid_t) -1) {
				error("duplicate --uid option");
				exit(error_exit);
			}
			if (uid_from_string (optarg, &opt.euid) < 0) {
				error("--uid=\"%s\" invalid", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_GID:
			if (opt.egid != (gid_t) -1) {
				error("duplicate --gid option");
				exit(error_exit);
			}
			if (gid_from_string (optarg, &opt.egid) < 0) {
				error("--gid=\"%s\" invalid", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_CONNTYPE:
			verify_conn_type(optarg, opt.conn_type);
			break;
		case LONG_OPT_BEGIN:
			opt.begin = parse_time(optarg, 0);
			if (opt.begin == 0) {
				error("Invalid time specification %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MAIL_TYPE:
			opt.mail_type |= parse_mail_type(optarg);
			if (opt.mail_type == 0) {
				error("--mail-type=%s invalid", optarg);
				exit(error_exit);
			}
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
				exit(error_exit);
			}
			if (opt.nice < 0) {
				uid_t my_uid = getuid();
				if ((my_uid != 0) &&
				    (my_uid != slurm_get_slurm_user_id())) {
					error("Nice value must be "
					      "non-negative, value ignored");
					opt.nice = 0;
				}
			}
			break;
		case LONG_OPT_PRIORITY: {
			long long priority = strtoll(optarg, NULL, 10);
			if (priority < 0) {
				error("Priority must be >= 0");
				exit(error_exit);
			}
			if (priority >= NO_VAL) {
				error("Priority must be < %i", NO_VAL);
				exit(error_exit);
			}
			opt.priority = priority;
			break;
		}
		case LONG_OPT_BELL:
			opt.bell = BELL_ALWAYS;
			break;
		case LONG_OPT_NO_BELL:
			opt.bell = BELL_NEVER;
			break;
		case LONG_OPT_JOBID:
			opt.jobid = _get_int(optarg, "jobid");
			break;
		case LONG_OPT_PROFILE:
			opt.profile = acct_gather_profile_from_string(optarg);
			break;
		case LONG_OPT_COMMENT:
			xfree(opt.comment);
			opt.comment = xstrdup(optarg);
			break;
		case LONG_OPT_QOS:
			xfree(opt.qos);
			opt.qos = xstrdup(optarg);
			break;
		case LONG_OPT_SOCKETSPERNODE:
			max_val = 0;
			get_resource_arg_range( optarg, "sockets-per-node",
						&opt.sockets_per_node,
						&max_val, true );
			if ((opt.sockets_per_node == 1) &&
			    (max_val == INT_MAX))
				opt.sockets_per_node = NO_VAL;
			break;
		case LONG_OPT_CORESPERSOCKET:
			max_val = 0;
			get_resource_arg_range( optarg, "cores-per-socket",
						&opt.cores_per_socket,
						&max_val, true );
			if ((opt.cores_per_socket == 1) &&
			    (max_val == INT_MAX))
				opt.cores_per_socket = NO_VAL;
			break;
		case LONG_OPT_THREADSPERCORE:
			max_val = 0;
			get_resource_arg_range( optarg, "threads-per-core",
						&opt.threads_per_core,
						&max_val, true );
			if ((opt.threads_per_core == 1) &&
			    (max_val == INT_MAX))
				opt.threads_per_core = NO_VAL;
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
		case LONG_OPT_HINT:
			/* Keep after other options filled in */
			if (verify_hint(optarg,
					&opt.sockets_per_node,
					&opt.cores_per_socket,
					&opt.threads_per_core,
					&opt.ntasks_per_core,
					NULL)) {
				exit(error_exit);
			}
			break;
		case LONG_OPT_REBOOT:
#if defined HAVE_BG && !defined HAVE_BG_L_P
			info("WARNING: If your job is smaller than the block "
			     "it is going to run on and other jobs are "
			     "running on it the --reboot option will not be "
			     "honored.  If this is the case, contact your "
			     "admin to reboot the block for you.");
#endif
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
			xfree(opt.acctg_freq);
			opt.acctg_freq = xstrdup(optarg);
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
		case LONG_OPT_MEM_BIND:
			if (slurm_verify_mem_bind(optarg, &opt.mem_bind,
						  &opt.mem_bind_type))
				exit(error_exit);
			break;
		case LONG_OPT_WCKEY:
			xfree(opt.wckey);
			opt.wckey = xstrdup(optarg);
			break;
		case LONG_OPT_RESERVATION:
			xfree(opt.reservation);
			opt.reservation = xstrdup(optarg);
			break;
		case LONG_OPT_SIGNAL:
			if (get_signal_opts(optarg, &opt.warn_signal,
					    &opt.warn_time, &opt.warn_flags)) {
				error("Invalid signal specification: %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_TIME_MIN:
			xfree(opt.time_min_str);
			opt.time_min_str = xstrdup(optarg);
			break;
		case LONG_OPT_GRES:
			if (!strcasecmp(optarg, "help") ||
			    !strcasecmp(optarg, "list")) {
				print_gres_help();
				exit(0);
			}
			xfree(opt.gres);
			opt.gres = xstrdup(optarg);
			break;
		case LONG_OPT_WAIT_ALL_NODES:
			opt.wait_all_nodes = strtol(optarg, NULL, 10);
			break;
		case LONG_OPT_REQ_SWITCH:
			pos_delimit = strstr(optarg,"@");
			if (pos_delimit != NULL) {
				pos_delimit[0] = '\0';
				pos_delimit++;
				opt.wait4switch = time_str2secs(pos_delimit);
			}
			opt.req_switch = _get_int(optarg, "switches");
			break;
		default:
			if (spank_process_option(opt_char, optarg) < 0) {
				error("Unrecognized command line parameter %c",
				      opt_char);
				exit(error_exit);
			}
		}
	}

	spank_option_table_destroy(optz);
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
	for (i = 0; i < command_argc; i++) {
		if ((i == 0) && (rest == NULL))
			break;	/* Fix for CLANG false positive */
		command_argv[i] = xstrdup(rest[i]);
	}
	command_argv[i] = NULL;	/* End of argv's (for possible execv) */

	if (!_opt_verify())
		exit(error_exit);
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
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	if (cluster_flags & CLUSTER_FLAG_BGQ)
		bg_figure_nodes_tasks(&opt.min_nodes, &opt.max_nodes,
				      &opt.ntasks_per_node, &opt.ntasks_set,
				      &opt.ntasks, opt.nodes_set, opt.nodes_set,
				      opt.overcommit, 0);

	if ((opt.ntasks_per_node > 0) && (!opt.ntasks_set)) {
		opt.ntasks = opt.min_nodes * opt.ntasks_per_node;
		opt.ntasks_set = 1;
	}

	if (opt.cpus_set && (opt.mincpus < opt.cpus_per_task))
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
	if (opt.ntasks <= 0) {
		error("invalid number of tasks (-n %d)",
		      opt.ntasks);
		verified = false;
	}

	if (opt.cpus_set && (opt.cpus_per_task <= 0)) {
		error("invalid number of cpus per task (-c %d)",
		      opt.cpus_per_task);
		verified = false;
	}

	if ((opt.min_nodes < 0) || (opt.max_nodes < 0) ||
	    (opt.max_nodes && (opt.min_nodes > opt.max_nodes))) {
		error("invalid number of nodes (-N %d-%d)",
		      opt.min_nodes, opt.max_nodes);
		verified = false;
	}

#if defined(HAVE_ALPS_CRAY)
	if (getenv("BASIL_RESERVATION_ID") != NULL) {
		error("BASIL_RESERVATION_ID already set - running salloc "
		      "within salloc?");
		return false;
	}
	if (opt.no_shell) {
		/*
		 * As long as we are not using srun instead of aprun, this flag
		 * makes no difference for the operational behaviour of aprun.
		 */
		error("--no-shell mode is not supported on Cray (due to srun)");
		return false;
	}
	if (opt.shared && opt.shared != (uint16_t)NO_VAL) {
		info("Space sharing nodes is not supported on Cray systems");
		opt.shared = false;
	}
	if (opt.overcommit) {
		info("Oversubscribing is not supported on Cray.");
		opt.overcommit = false;
	}
	if (!opt.wait_all_nodes)
		info("Cray needs --wait-all-nodes to wait on ALPS reservation");
	opt.wait_all_nodes = true;
	if (opt.kill_command_signal_set) {
		/*
		 * Disabled to avoid that the user supplies a weaker signal that
		 * could cause the child processes not to terminate.
		 */
		info("The --kill-command is not supported on Cray.");
		opt.kill_command_signal_set = false;
	}
#elif defined(HAVE_BGL)
	if (opt.blrtsimage && strchr(opt.blrtsimage, ' ')) {
		error("invalid BlrtsImage given '%s'", opt.blrtsimage);
		verified = false;
	}
#endif

	if (opt.linuximage && strchr(opt.linuximage, ' ')) {
#ifdef HAVE_BGL
		error("invalid LinuxImage given '%s'", opt.linuximage);
#else
		error("invalid CnloadImage given '%s'", opt.linuximage);
#endif
		verified = false;
	}

	if (opt.mloaderimage && strchr(opt.mloaderimage, ' ')) {
		error("invalid MloaderImage given '%s'", opt.mloaderimage);
		verified = false;
	}

	if (opt.ramdiskimage && strchr(opt.ramdiskimage, ' ')) {
#ifdef HAVE_BGL
		error("invalid RamDiskImage given '%s'", opt.ramdiskimage);
#else
		error("invalid IoloadImage given '%s'", opt.ramdiskimage);
#endif
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
		if ((opt.ntasks/opt.plane_size) < opt.min_nodes) {
			if (((opt.min_nodes-1)*opt.plane_size) >= opt.ntasks) {
#if (0)
				info("Too few processes ((n/plane_size) %d < N %d) "
				     "and ((N-1)*(plane_size) %d >= n %d)) ",
				     opt.ntasks/opt.plane_size, opt.min_nodes,
				     (opt.min_nodes-1)*opt.plane_size,
				     opt.ntasks);
#endif
				error("Too few processes for the requested "
				      "{plane,node} distribution");
				exit(error_exit);
			}
		}
	}

	/* massage the numbers */
	if ((opt.nodes_set || opt.extra_set)				&&
	    ((opt.min_nodes == opt.max_nodes) || (opt.max_nodes == 0))	&&
	    !opt.ntasks_set) {
		/* 1 proc / node default */
		opt.ntasks = opt.min_nodes;

		/* 1 proc / min_[socket * core * thread] default */
		if (opt.sockets_per_node != NO_VAL) {
			opt.ntasks *= opt.sockets_per_node;
			opt.ntasks_set = true;
		}
		if (opt.cores_per_socket != NO_VAL) {
			opt.ntasks *= opt.cores_per_socket;
			opt.ntasks_set = true;
		}
		if (opt.threads_per_core != NO_VAL) {
			opt.ntasks *= opt.threads_per_core;
			opt.ntasks_set = true;
		}

	} else if (opt.nodes_set && opt.ntasks_set) {

		/*
		 *  make sure # of procs >= min_nodes
		 */
		if (opt.ntasks < opt.min_nodes) {

			info ("Warning: can't run %d processes on %d "
			      "nodes, setting nnodes to %d",
			      opt.ntasks, opt.min_nodes, opt.ntasks);

			opt.min_nodes = opt.ntasks;
			if (   opt.max_nodes
			       && (opt.min_nodes > opt.max_nodes) )
				opt.max_nodes = opt.min_nodes;
		}

	} /* else if (opt.ntasks_set && !opt.nodes_set) */

	if (!opt.nodelist) {
		if ((opt.nodelist = xstrdup(getenv("SLURM_HOSTFILE")))) {
			/* make sure the file being read in has a / in
			   it to make sure it is a file in the
			   valid_node_list function */
			if (!strstr(opt.nodelist, "/")) {
				char *add_slash = xstrdup("./");
				xstrcat(add_slash, opt.nodelist);
				xfree(opt.nodelist);
				opt.nodelist = add_slash;
			}
			opt.distribution = SLURM_DIST_ARBITRARY;
			if (!_valid_node_list(&opt.nodelist)) {
				error("Failure getting NodeNames from "
				      "hostfile");
				exit(error_exit);
			} else {
				debug("loaded nodes (%s) from hostfile",
				      opt.nodelist);
			}
		}
	} else {
		if (!_valid_node_list(&opt.nodelist))
			exit(error_exit);
	}

	/* set up the proc and node counts based on the arbitrary list
	   of nodes */
	if ((opt.distribution == SLURM_DIST_ARBITRARY)
	   && (!opt.nodes_set || !opt.ntasks_set)) {
		hostlist_t hl = hostlist_create(opt.nodelist);
		if (!opt.ntasks_set) {
			opt.ntasks_set = 1;
			opt.ntasks = hostlist_count(hl);
		}
		if (!opt.nodes_set) {
			opt.nodes_set = 1;
			hostlist_uniq(hl);
			opt.min_nodes = opt.max_nodes = hostlist_count(hl);
		}
		hostlist_destroy(hl);
	}

	if (opt.time_limit_str) {
		opt.time_limit = time_str2mins(opt.time_limit_str);
		if ((opt.time_limit < 0) && (opt.time_limit != INFINITE)) {
			error("Invalid time limit specification");
			exit(error_exit);
		}
		if (opt.time_limit == 0)
			opt.time_limit = INFINITE;
	}
	if (opt.time_min_str) {
		opt.time_min = time_str2mins(opt.time_min_str);
		if ((opt.time_min < 0) && (opt.time_min != INFINITE)) {
			error("Invalid min-time specification");
			exit(error_exit);
		}
		if (opt.time_min == 0)
			opt.time_min = INFINITE;
	}

#ifdef HAVE_AIX
	if (opt.network == NULL)
		opt.network = "us,sn_all,bulk_xfer";
#endif

#ifdef HAVE_NATIVE_CRAY
	if (opt.network && opt.shared)
		fatal("Requesting network performance counters requires "
		      "exclusive access.  Please add the --exclusive option "
		      "to your request.");
#endif

	if (opt.mem_bind_type && (getenv("SLURM_MEM_BIND") == NULL)) {
		char tmp[64];
		slurm_sprint_mem_bind_type(tmp, opt.mem_bind_type);
		if (opt.mem_bind) {
			setenvf(NULL, "SLURM_MEM_BIND", "%s:%s",
				tmp, opt.mem_bind);
		} else {
			setenvf(NULL, "SLURM_MEM_BIND", "%s", tmp);
		}
	}
	if ((opt.ntasks_per_node > 0) &&
	    (getenv("SLURM_NTASKS_PER_NODE") == NULL)) {
		setenvf(NULL, "SLURM_NTASKS_PER_NODE", "%d",
			opt.ntasks_per_node);
	}

	if (opt.profile)
		setenvfs("SLURM_PROFILE=%s",
			 acct_gather_profile_to_string(opt.profile));

	return verified;
}

/* Functions used by SPANK plugins to read and write job environment
 * variables for use within job's Prolog and/or Epilog */
extern char *spank_get_job_env(const char *name)
{
	int i, len;
	char *tmp_str = NULL;

	if ((name == NULL) || (name[0] == '\0') ||
	    (strchr(name, (int)'=') != NULL)) {
		slurm_seterrno(EINVAL);
		return NULL;
	}

	xstrcat(tmp_str, name);
	xstrcat(tmp_str, "=");
	len = strlen(tmp_str);

	for (i=0; i<opt.spank_job_env_size; i++) {
		if (strncmp(opt.spank_job_env[i], tmp_str, len))
			continue;
		xfree(tmp_str);
		return (opt.spank_job_env[i] + len);
	}

	return NULL;
}

extern int   spank_set_job_env(const char *name, const char *value,
			       int overwrite)
{
	int i, len;
	char *tmp_str = NULL;

	if ((name == NULL) || (name[0] == '\0') ||
	    (strchr(name, (int)'=') != NULL)) {
		slurm_seterrno(EINVAL);
		return -1;
	}

	xstrcat(tmp_str, name);
	xstrcat(tmp_str, "=");
	len = strlen(tmp_str);
	xstrcat(tmp_str, value);

	for (i=0; i<opt.spank_job_env_size; i++) {
		if (strncmp(opt.spank_job_env[i], tmp_str, len))
			continue;
		if (overwrite) {
			xfree(opt.spank_job_env[i]);
			opt.spank_job_env[i] = tmp_str;
		} else
			xfree(tmp_str);
		return 0;
	}

	/* Need to add an entry */
	opt.spank_job_env_size++;
	xrealloc(opt.spank_job_env, sizeof(char *) * opt.spank_job_env_size);
	opt.spank_job_env[i] = tmp_str;
	return 0;
}

extern int   spank_unset_job_env(const char *name)
{
	int i, j, len;
	char *tmp_str = NULL;

	if ((name == NULL) || (name[0] == '\0') ||
	    (strchr(name, (int)'=') != NULL)) {
		slurm_seterrno(EINVAL);
		return -1;
	}

	xstrcat(tmp_str, name);
	xstrcat(tmp_str, "=");
	len = strlen(tmp_str);

	for (i=0; i<opt.spank_job_env_size; i++) {
		if (strncmp(opt.spank_job_env[i], tmp_str, len))
			continue;
		xfree(opt.spank_job_env[i]);
		for (j=(i+1); j<opt.spank_job_env_size; i++, j++)
			opt.spank_job_env[i] = opt.spank_job_env[j];
		opt.spank_job_env_size--;
		if (opt.spank_job_env_size == 0)
			xfree(opt.spank_job_env);
		return 0;
	}

	return 0;	/* not found */
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

#define tf_(b) (b == true) ? "true" : "false"

static void _opt_list(void)
{
	char *str;

	info("defined options for program `%s'", opt.progname);
	info("--------------- ---------------------");

	info("user           : `%s'", opt.user);
	info("uid            : %ld", (long) opt.uid);
	info("gid            : %ld", (long) opt.gid);
	info("ntasks         : %d %s", opt.ntasks,
		opt.ntasks_set ? "(set)" : "(default)");
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
	info("reservation    : `%s'", opt.reservation);
	info("wckey          : `%s'", opt.wckey);
	if (opt.jobid != NO_VAL)
		info("jobid          : %u", opt.jobid);
	info("distribution   : %s", format_task_dist_states(opt.distribution));
	if (opt.distribution == SLURM_DIST_PLANE)
		info("plane size   : %u", opt.plane_size);
	info("verbose        : %d", opt.verbose);
	if (opt.immediate <= 1)
		info("immediate      : %s", tf_(opt.immediate));
	else
		info("immediate      : %d secs", (opt.immediate - 1));
	info("overcommit     : %s", tf_(opt.overcommit));
	if (opt.time_limit == INFINITE)
		info("time_limit     : INFINITE");
	else if (opt.time_limit != NO_VAL)
		info("time_limit     : %d", opt.time_limit);
	if (opt.time_min != NO_VAL)
		info("time_min       : %d", opt.time_min);
	if (opt.nice)
		info("nice           : %d", opt.nice);
	info("account        : %s", opt.account);
	info("comment        : %s", opt.comment);
	info("dependency     : %s", opt.dependency);
	if (opt.gres != NULL)
		info("gres           : %s", opt.gres);
	info("network        : %s", opt.network);
	info("profile        : `%s'",
	     acct_gather_profile_to_string(opt.profile));
	info("qos            : %s", opt.qos);
	str = print_constraints();
	info("constraints    : %s", str);
	xfree(str);
	if (opt.conn_type[0] != (uint16_t) NO_VAL) {
		str = conn_type_string_full(opt.conn_type);
		info("conn_type      : %s", str);
		xfree(str);
	}
	str = print_geometry(opt.geometry);
	info("geometry       : %s", str);
	xfree(str);
	info("reboot         : %s", opt.reboot ? "no" : "yes");
	info("rotate         : %s", opt.no_rotate ? "yes" : "no");
#ifdef HAVE_BGL
	if (opt.blrtsimage)
		info("BlrtsImage     : %s", opt.blrtsimage);
#endif
	if (opt.linuximage)
#ifdef HAVE_BGL
		info("LinuxImage     : %s", opt.linuximage);
#else
		info("CnloadImage    : %s", opt.linuximage);
#endif
	if (opt.mloaderimage)
		info("MloaderImage   : %s", opt.mloaderimage);
	if (opt.ramdiskimage)
#ifdef HAVE_BGL
		info("RamDiskImage   : %s", opt.ramdiskimage);
#else
		info("IoloadImage   : %s", opt.ramdiskimage);
#endif

	if (opt.begin) {
		char time_str[32];
		slurm_make_time_str(&opt.begin, time_str, sizeof(time_str));
		info("begin          : %s", time_str);
	}
	info("mail_type      : %s", print_mail_type(opt.mail_type));
	info("mail_user      : %s", opt.mail_user);
	info("sockets-per-node  : %d", opt.sockets_per_node);
	info("cores-per-socket  : %d", opt.cores_per_socket);
	info("threads-per-core  : %d", opt.threads_per_core);
	info("ntasks-per-node   : %d", opt.ntasks_per_node);
	info("ntasks-per-socket : %d", opt.ntasks_per_socket);
	info("ntasks-per-core   : %d", opt.ntasks_per_core);
	info("plane_size        : %u", opt.plane_size);
	info("mem_bind          : %s",
	     opt.mem_bind == NULL ? "default" : opt.mem_bind);
	str = print_commandline(command_argc, command_argv);
	info("user command   : `%s'", str);
	info("switches          : %d", opt.req_switch);
	info("wait-for-switches : %d", opt.wait4switch);
	info("core-spec         : %d", opt.core_spec);
	xfree(str);

}

static void _usage(void)
{
 	printf(
"Usage: salloc [-N numnodes|[min nodes]-[max nodes]] [-n num-processors]\n"
"              [[-c cpus-per-node] [-r n] [-p partition] [--hold] [-t minutes]\n"
"              [--immediate[=secs]] [--no-kill] [--overcommit] [-D path]\n"
"              [--share] [-J jobname] [--jobid=id] [-W sec]\n"
"              [--verbose] [--gid=group] [--uid=user] [--licenses=names]\n"
"              [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"              [--account=name] [--dependency=type:jobid] [--comment=name]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
#ifdef HAVE_BG_L_P
"              [--geometry=XxYxZ] "
#else
"              [--geometry=AxXxYxZ] "
#endif
"[--conn-type=type] [--no-rotate]\n"
#ifdef HAVE_BGL
"              [--blrts-image=path] [--linux-image=path]\n"
"              [--mloader-image=path] [--ramdisk-image=path]\n"
#else
"              [--cnload-image=path]\n"
"              [--mloader-image=path] [--ioload-image=path]\n"
#endif
#endif
"              [--mail-type=type] [--mail-user=user][--nice[=value]]\n"
"              [--bell] [--no-bell] [--kill-command[=signal]]\n"
"              [--nodefile=file] [--nodelist=hosts] [--exclude=hosts]\n"
"              [--network=type] [--mem-per-cpu=MB] [--qos=qos]\n"
"              [--mem_bind=...] [--reservation=name]\n"
"              [--time-min=minutes] [--gres=list] [--profile=...]\n"
"              [--switches=max-switches[@max-time-to-wait]]\n"
"              [--core-spec=cores]  [--reboot]\n"
"              [executable [args...]]\n");
}

static void _help(void)
{
	slurm_ctl_conf_t *conf;

        printf (
"Usage: salloc [OPTIONS...] [executable [args...]]\n"
"\n"
"Parallel run options:\n"
"  -A, --account=name          charge job to specified account\n"
"      --begin=time            defer job until HH:MM MM/DD/YY\n"
"      --bell                  ring the terminal bell when the job is allocated\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"      --comment=name          arbitrary comment\n"
"  -d, --dependency=type:jobid defer job until condition on jobid is satisfied\n"
"  -D, --chdir=path            change working directory\n"
"      --get-user-env          used by Moab.  See srun man page.\n"
"      --gid=group_id          group ID to run job as (user root only)\n"
"      --gres=list             required generic resources\n"
"  -H, --hold                  submit job in held state\n"
"  -I, --immediate[=secs]      exit if resources not available in \"secs\"\n"
"      --jobid=id              specify jobid to use\n"
"  -J, --job-name=jobname      name of job\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -K, --kill-command[=signal] signal to send terminating job\n"
"  -L, --licenses=names        required license, comma separated\n"
"  -m, --distribution=type     distribution method for processes to nodes\n"
"                              (type = block|cyclic|arbitrary)\n"
"      --mail-type=type        notify on state change: BEGIN, END, FAIL or ALL\n"
"      --mail-user=user        who to send email notification for job state\n"
"                              changes\n"
"  -n, --tasks=N               number of processors required\n"
"      --nice[=value]          decrease scheduling priority by value\n"
"      --no-bell               do NOT ring the terminal bell\n"
"      --ntasks-per-node=n     number of tasks to invoke on each node\n"
"  -N, --nodes=N               number of nodes on which to run (N = min[-max])\n"
"  -O, --overcommit            overcommit resources\n"
"      --priority=value        set the priority of the job to value\n"
"      --profile=value         enable acct_gather_profile for detailed data\n"
"                              value is all or none or any combination of\n"
"                              energy, lustre, network or task\n"
"  -p, --partition=partition   partition requested\n"
"      --qos=qos               quality of service\n"
"  -Q, --quiet                 quiet mode (suppress informational messages)\n"
"      --reboot                reboot compute nodes before starting job\n"
"  -s, --share                 share nodes with other jobs\n"
"      --signal=[B:]num[@time] send signal when time limit within time seconds\n"
"      --switches=max-switches{@max-time-to-wait}\n"
"                              Optimum switches and max time to wait for optimum\n"
"  -S, --core-spec=cores       count of reserved cores\n"
"  -t, --time=minutes          time limit\n"
"      --time-min=minutes      minimum time limit (if distinct)\n"
"      --uid=user_id           user ID to run job as (user root only)\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"      --wckey=wckey           wckey to run job under\n"
"\n"
"Constraint options:\n"
"      --contiguous            demand a contiguous range of nodes\n"
"  -C, --constraint=list       specify a list of constraints\n"
"  -F, --nodefile=filename     request a specific list of hosts\n"
"      --mem=MB                minimum amount of real memory\n"
"      --mincpus=n             minimum number of logical processors (threads)\n"
"                              per node\n"
"      --reservation=name      allocate resources from named reservation\n"
"      --tmp=MB                minimum amount of temporary disk\n"
"  -w, --nodelist=hosts...     request a specific list of hosts\n"
"  -x, --exclude=hosts...      exclude a specific list of hosts\n"
"\n"
"Consumable resources related options:\n"
"      --exclusive             allocate nodes in exclusive mode when\n"
"                              cpu consumable resource is enabled\n"
"      --mem-per-cpu=MB        maximum amount of real memory per allocated\n"
"                              cpu required by the job.\n"
"                              --mem >= --mem-per-cpu if --mem is specified.\n"
"\n"
"Affinity/Multi-core options: (when the task/affinity plugin is enabled)\n"
"  -B  --extra-node-info=S[:C[:T]]            Expands to:\n"
"       --sockets-per-node=S   number of sockets per node to allocate\n"
"       --cores-per-socket=C   number of cores per socket to allocate\n"
"       --threads-per-core=T   number of threads per core to allocate\n"
"                              each field can be 'min' or wildcard '*'\n"
"                              total cpus requested = (N x S x C x T)\n"
"\n"
"      --ntasks-per-core=n     number of tasks to invoke on each core\n"
"      --ntasks-per-socket=n   number of tasks to invoke on each socket\n");
	conf = slurm_conf_lock();
	if (conf->task_plugin != NULL
	    && strcasecmp(conf->task_plugin, "task/affinity") == 0) {
		printf(
"      --hint=                 Bind tasks according to application hints\n"
"                              (see \"--hint=help\" for options)\n"
"      --mem_bind=             Bind memory to locality domains (ldom)\n"
"                              (see \"--mem_bind=help\" for options)\n");
	}
	slurm_conf_unlock();

	spank_print_options(stdout, 6, 30);

	printf("\n"
#ifdef HAVE_AIX				/* AIX/Federation specific options */
"AIX related options:\n"
"      --network=type          communication protocol to be used\n"
"\n"
#endif
#ifdef HAVE_NATIVE_CRAY			/* Native Cray specific options */
"Cray related options:\n"
"      --network=type          Use network performace counters\n"
"                              (system, network, or processor)\n"
"\n"
#endif
#ifdef HAVE_BG				/* Blue gene specific options */
"Blue Gene related options:\n"
#ifdef HAVE_BG_L_P
"  -g, --geometry=XxYxZ        geometry constraints of the job\n"
#else
"  -g, --geometry=AxXxYxZ      Midplane geometry constraints of the job,\n"
"                              sub-block allocations can not be allocated\n"
"                              with the geometry option\n"
#endif
"  -R, --no-rotate             disable geometry rotation\n"
"      --conn-type=type        constraint on type of connection, MESH or TORUS\n"
"                              if not set, then tries to fit TORUS else MESH\n"
#ifndef HAVE_BGL
"                              If wanting to run in HTC mode (only for 1\n"
"                              midplane and below).  You can use HTC_S for\n"
"                              SMP, HTC_D for Dual, HTC_V for\n"
"                              virtual node mode, and HTC_L for Linux mode.\n"
"      --cnload-image=path     path to compute node image for bluegene block.  Default if not set\n"
"      --mloader-image=path    path to mloader image for bluegene block.  Default if not set\n"
"      --ioload-image=path     path to ioload image for bluegene block.  Default if not set\n"
#else
"      --blrts-image=path      path to blrts image for bluegene block.\n"
"                              Default if not set\n"
"      --linux-image=path      path to linux image for bluegene block.  Default\n"
"                              if not set\n"
"      --mloader-image=path    path to mloader image for bluegene block.\n"
"                              Default if not set\n"
"      --ramdisk-image=path    path to ramdisk image for bluegene block.\n"
"                              Default if not set\n"
#endif
#endif
"\n"
"Help options:\n"
"  -h, --help                  show this help message\n"
"  -u, --usage                 display brief usage message\n"
"\n"
"Other options:\n"
"  -V, --version               output version information and exit\n"
"\n"
		);

}
