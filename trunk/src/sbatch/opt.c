/*****************************************************************************\
 *  opt.c - options processing for sbatch
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
#include "src/common/proc_args.h"
#include "src/common/read_config.h" /* contains getnodename() */
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/sbatch/opt.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_DEBUG       0x03
#define OPT_NODES       0x04
#define OPT_BOOL        0x05
#define OPT_CORE        0x06
#define OPT_CONN_TYPE	0x07
#define OPT_DISTRIB	0x08
#define OPT_NO_ROTATE	0x09
#define OPT_GEOMETRY	0x0a
#define OPT_MULTI	0x0b
#define OPT_EXCLUSIVE	0x0c
#define OPT_OVERCOMMIT	0x0d
#define OPT_OPEN_MODE	0x0e
#define OPT_ACCTG_FREQ  0x0f
#define OPT_NO_REQUEUE  0x10
#define OPT_REQUEUE     0x11
#define OPT_CPU_BIND    0x12
#define OPT_MEM_BIND    0x13

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_PROPAGATE   0x100
#define LONG_OPT_CPU_BIND    0x101
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
#define LONG_OPT_NO_REQUEUE  0x116
#define LONG_OPT_COMMENT     0x117
#define LONG_OPT_WRAP        0x118
#define LONG_OPT_REQUEUE     0x119
#define LONG_OPT_NETWORK     0x120
#define LONG_OPT_SOCKETSPERNODE  0x130
#define LONG_OPT_CORESPERSOCKET  0x131
#define LONG_OPT_THREADSPERCORE  0x132
#define LONG_OPT_NTASKSPERNODE   0x136
#define LONG_OPT_NTASKSPERSOCKET 0x137
#define LONG_OPT_NTASKSPERCORE   0x138
#define LONG_OPT_MEM_PER_CPU     0x13a
#define LONG_OPT_HINT            0x13b
#define LONG_OPT_BLRTS_IMAGE     0x140
#define LONG_OPT_LINUX_IMAGE     0x141
#define LONG_OPT_MLOADER_IMAGE   0x142
#define LONG_OPT_RAMDISK_IMAGE   0x143
#define LONG_OPT_REBOOT          0x144
#define LONG_OPT_GET_USER_ENV    0x146
#define LONG_OPT_OPEN_MODE       0x147
#define LONG_OPT_ACCTG_FREQ      0x148

/*---- global variables, defined in opt.h ----*/
opt_t opt;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;


/* Get a decimal integer from arg */
static int  _get_int(const char *arg, const char *what);

static void  _help(void);

/* fill in default options  */
static void _opt_default(void);

/* set options from batch script */
static void _opt_batch_script(const void *body, int size);

/* set options from pbs batch script */
static void _opt_pbs_batch_script(const void *body, int size);

/* set options based upon env vars  */
static void _opt_env(void);
static void _proc_get_user_env(char *optarg);

/* list known options and their settings  */
static void  _opt_list(void);

/* verify options sanity  */
static bool _opt_verify(void);

static void _process_env_var(env_vars_t *e, const char *val);

static uint16_t _parse_pbs_mail_type(const char *arg);

static void  _usage(void);
static  void _fullpath(char **filename, const char *cwd);
static void _set_options(int argc, char **argv);
static void _set_pbs_options(int argc, char **argv);
static void _parse_pbs_resource_list(char *rl);

/*---[ end forward declarations of static functions ]---------------------*/

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

	opt.script_argc = 0;
	opt.script_argv = NULL;

	opt.gid = getgid();

	if ((getcwd(buf, MAXPATHLEN)) == NULL) 
		fatal("getcwd failed: %m");
	opt.cwd = xstrdup(buf);

	opt.progname = NULL;

	opt.nprocs = 1;
	opt.nprocs_set = false;
	opt.cpus_per_task = 1; 
	opt.cpus_set = false;
	opt.min_nodes = 0;
	opt.max_nodes = 0;
	opt.nodes_set = false;
	opt.min_sockets_per_node = NO_VAL; /* requested min/maxsockets */
	opt.max_sockets_per_node = NO_VAL;
	opt.min_cores_per_socket = NO_VAL; /* requested min/maxcores */
	opt.max_cores_per_socket = NO_VAL;
	opt.min_threads_per_core = NO_VAL; /* requested min/maxthreads */
	opt.max_threads_per_core = NO_VAL;
	opt.ntasks_per_node      = 0;      /* ntask max limits */
	opt.ntasks_per_socket    = NO_VAL;
	opt.ntasks_per_core      = NO_VAL;
	opt.cpu_bind_type = 0;
	opt.cpu_bind = NULL;
	opt.mem_bind_type = 0;
	opt.mem_bind = NULL;
	opt.time_limit = NO_VAL;
	opt.partition = NULL;

	opt.job_name = NULL;
	opt.jobid    = NO_VAL;
	opt.jobid_set = false;
	opt.dependency = NULL;
	opt.account  = NULL;
	opt.comment  = NULL;

	opt.distribution = SLURM_DIST_UNKNOWN;
	opt.plane_size   = NO_VAL;

	opt.shared = (uint16_t)NO_VAL;
	opt.no_kill = false;

	opt.immediate	= false;
	opt.requeue	= NO_VAL;
	opt.overcommit	= false;

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
	
	opt.propagate	    = NULL;  /* propagate specific rlimits */

	opt.ifname = xstrdup("/dev/null");
	opt.ofname = NULL;
	opt.efname = NULL;

	opt.get_user_env_time = -1;
	opt.get_user_env_mode = -1;
	opt.acctg_freq        = -1;
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
  {"SBATCH_ACCOUNT",       OPT_STRING,     &opt.account,       NULL           },
  {"SBATCH_ACCTG_FREQ",    OPT_INT,        &opt.acctg_freq,    NULL           },
  {"SBATCH_BLRTS_IMAGE",   OPT_STRING,     &opt.blrtsimage,    NULL           },
  {"SBATCH_CONN_TYPE",     OPT_CONN_TYPE,  NULL,               NULL           },
  {"SBATCH_CPU_BIND",      OPT_CPU_BIND,   NULL,               NULL           },
  {"SBATCH_DEBUG",         OPT_DEBUG,      NULL,               NULL           },
  {"SBATCH_DISTRIBUTION",  OPT_DISTRIB ,   NULL,               NULL           },
  {"SBATCH_EXCLUSIVE",     OPT_EXCLUSIVE,  NULL,               NULL           },
  {"SBATCH_GEOMETRY",      OPT_GEOMETRY,   NULL,               NULL           },
  {"SBATCH_IMMEDIATE",     OPT_BOOL,       &opt.immediate,     NULL           },
  {"SBATCH_JOBID",         OPT_INT,        &opt.jobid,         NULL           },
  {"SBATCH_JOB_NAME",      OPT_STRING,     &opt.job_name,      NULL           },
  {"SBATCH_LINUX_IMAGE",   OPT_STRING,     &opt.linuximage,    NULL           },
  {"SBATCH_MEM_BIND",      OPT_MEM_BIND,   NULL,               NULL           },
  {"SBATCH_MLOADER_IMAGE", OPT_STRING,     &opt.mloaderimage,  NULL           },
  {"SBATCH_NETWORK",       OPT_STRING,     &opt.network,       NULL           },
  {"SBATCH_NO_REQUEUE",    OPT_NO_REQUEUE, NULL,               NULL           },
  {"SBATCH_NO_ROTATE",     OPT_BOOL,       &opt.no_rotate,     NULL           },
  {"SBATCH_OPEN_MODE",     OPT_OPEN_MODE,  NULL,               NULL           },
  {"SBATCH_OVERCOMMIT",    OPT_OVERCOMMIT, NULL,               NULL           },
  {"SBATCH_PARTITION",     OPT_STRING,     &opt.partition,     NULL           },
  {"SBATCH_RAMDISK_IMAGE", OPT_STRING,     &opt.ramdiskimage,  NULL           },
  {"SBATCH_TIMELIMIT",     OPT_STRING,     &opt.time_limit_str,NULL           },
  {"SBATCH_REQUEUE",       OPT_REQUEUE,    NULL,               NULL           },
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

	case OPT_CPU_BIND:
		if (slurm_verify_cpu_bind(val, &opt.cpu_bind,
					  &opt.cpu_bind_type))
			exit(1);
		break;

	case OPT_MEM_BIND:
		if (slurm_verify_mem_bind(val, &opt.mem_bind,
					  &opt.mem_bind_type))
			exit(1);
		break;

	case OPT_DISTRIB:
		opt.distribution = verify_dist_type(optarg, 
						    &opt.plane_size);
		if (opt.distribution == SLURM_DIST_UNKNOWN)
			error("distribution type `%s' is invalid", optarg);
		else
			setenv("SLURM_DISTRIBUTION", optarg, 1);
		break;

	case OPT_NODES:
		opt.nodes_set = verify_node_count( val, 
						   &opt.min_nodes, 
						   &opt.max_nodes );
		if (opt.nodes_set == false) {
			error("\"%s=%s\" -- invalid node count. ignoring...",
			      e->var, val);
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

	case OPT_EXCLUSIVE:
		opt.shared = 0;
		break;

	case OPT_OVERCOMMIT:
		opt.overcommit = true;
		break;

	case OPT_OPEN_MODE:
		if ((val[0] == 'a') || (val[0] == 'A'))
			opt.open_mode = OPEN_MODE_APPEND;
		else if ((val[0] == 't') || (val[0] == 'T'))
			opt.open_mode = OPEN_MODE_TRUNCATE;
		else
			error("Invalid SBATCH_OPEN_MODE: %s. Ignored", val);
		break;

	case OPT_NO_REQUEUE:
		opt.requeue = 0;
		break;

	case OPT_REQUEUE:
		opt.requeue = 1;
		break;

	default:
		/* do nothing */
		break;
	}
}


/*---[ command line option processing ]-----------------------------------*/

static struct option long_options[] = {
	{"batch",         no_argument,       0, 'b'}, /* batch option
							 is only here for
							 moab tansition
							 doesn't do anything */
	{"extra-node-info", required_argument, 0, 'B'},
	{"cpus-per-task", required_argument, 0, 'c'},
	{"constraint",    required_argument, 0, 'C'},
	{"workdir",       required_argument, 0, 'D'},
	{"error",         required_argument, 0, 'e'},
	{"nodefile",      required_argument, 0, 'F'},
	{"geometry",      required_argument, 0, 'g'},
	{"help",          no_argument,       0, 'h'},
	{"hold",          no_argument,       0, 'H'}, /* undocumented */
	{"input",         required_argument, 0, 'i'},
	{"immediate",     no_argument,       0, 'I'},
	{"job-name",      required_argument, 0, 'J'},
	{"no-kill",       no_argument,       0, 'k'},
	{"licenses",      required_argument, 0, 'L'},
	{"distribution",  required_argument, 0, 'm'},
	{"tasks",         required_argument, 0, 'n'},	
	{"ntasks",        required_argument, 0, 'n'},
	{"nodes",         required_argument, 0, 'N'},
	{"output",        required_argument, 0, 'o'},
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
	{"exclude",       required_argument, 0, 'x'},
	{"contiguous",    no_argument,       0, LONG_OPT_CONT},
	{"exclusive",     no_argument,       0, LONG_OPT_EXCLUSIVE},
	{"mincpus",       required_argument, 0, LONG_OPT_MINCPU},
	{"minsockets",    required_argument, 0, LONG_OPT_MINSOCKETS},
	{"mincores",      required_argument, 0, LONG_OPT_MINCORES},
	{"minthreads",    required_argument, 0, LONG_OPT_MINTHREADS},
	{"mem",           required_argument, 0, LONG_OPT_MEM},
	{"mem-per-cpu",   required_argument, 0, LONG_OPT_MEM_PER_CPU},
	{"hint",          required_argument, 0, LONG_OPT_HINT},
	{"tmp",           required_argument, 0, LONG_OPT_TMP},
	{"jobid",         required_argument, 0, LONG_OPT_JOBID},
	{"uid",           required_argument, 0, LONG_OPT_UID},
	{"gid",           required_argument, 0, LONG_OPT_GID},
	{"conn-type",     required_argument, 0, LONG_OPT_CONNTYPE},
	{"begin",         required_argument, 0, LONG_OPT_BEGIN},
	{"mail-type",     required_argument, 0, LONG_OPT_MAIL_TYPE},
	{"mail-user",     required_argument, 0, LONG_OPT_MAIL_USER},
	{"nice",          optional_argument, 0, LONG_OPT_NICE},
	{"no-requeue",    no_argument,       0, LONG_OPT_NO_REQUEUE},
	{"requeue",       no_argument,       0, LONG_OPT_REQUEUE},
	{"comment",       required_argument, 0, LONG_OPT_COMMENT},
	{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
	{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
	{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
	{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
	{"ntasks-per-core",  required_argument, 0, LONG_OPT_NTASKSPERCORE},
	{"blrts-image",   required_argument, 0, LONG_OPT_BLRTS_IMAGE},
	{"linux-image",   required_argument, 0, LONG_OPT_LINUX_IMAGE},
	{"mloader-image", required_argument, 0, LONG_OPT_MLOADER_IMAGE},
	{"ramdisk-image", required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
	{"reboot",        no_argument,       0, LONG_OPT_REBOOT},
	{"tasks-per-node",required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"wrap",          required_argument, 0, LONG_OPT_WRAP},
	{"get-user-env",  optional_argument, 0, LONG_OPT_GET_USER_ENV},
	{"open-mode",     required_argument, 0, LONG_OPT_OPEN_MODE},
	{"acctg-freq",    required_argument, 0, LONG_OPT_ACCTG_FREQ},
	{"propagate",     optional_argument, 0, LONG_OPT_PROPAGATE},
	{"network",       required_argument, 0, LONG_OPT_NETWORK},
	{"cpu_bind",      required_argument, 0, LONG_OPT_CPU_BIND},
	{"mem_bind",      required_argument, 0, LONG_OPT_MEM_BIND},
	{NULL,            0,                 0, 0}
};

static char *opt_string =
	"+a:bB:c:C:d:D:e:F:g:hHi:IJ:kL:m:n:N:o:Op:P:qR:st:uU:vVw:x:";


/*
 * process_options_first_pass()
 *
 * In this first pass we only look at the command line options, and we
 * will only handle a few options (help, usage, quiet, verbose, version),
 * and look for the script name and arguments (if provided).
 *
 * We will parse the environment variable options, batch script options,
 * and all of the rest of the command line options in
 * process_options_second_pass().
 *
 * Return a pointer to the batch script file name is provided on the command
 * line, otherwise return NULL, and the script will need to be read from
 * standard input.
 */
char *process_options_first_pass(int argc, char **argv)
{
	int opt_char, option_index = 0;
	char *str = NULL;

	/* initialize option defaults */
	_opt_default();

	opt.progname = xbasename(argv[0]);
	optind = 0;

	while((opt_char = getopt_long(argc, argv, opt_string,
				      long_options, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			fprintf(stderr, "Try \"sbatch --help\" for more "
				"information\n");
			exit(1);
			break;
		case 'h':
			_help();
			exit(0);
			break;
		case 'q':
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
		case LONG_OPT_WRAP:
			opt.wrap = xstrdup(optarg);
			break;
		default:
			/* will be parsed in second pass function */
			break;
		}
	}
	xfree(str);

	if (argc > optind && opt.wrap != NULL) {
		fatal("Script arguments are not permitted with the"
		      " --wrap option.");
	}
	if (argc > optind) {
		int i;
		char **leftover;

		opt.script_argc = argc - optind;
		leftover = argv + optind;
		opt.script_argv = (char **) xmalloc((opt.script_argc + 1)
						    * sizeof(char *));
		for (i = 0; i < opt.script_argc; i++)
			opt.script_argv[i] = xstrdup(leftover[i]);
		opt.script_argv[i] = NULL;
	}
	if (opt.script_argc > 0) {
		char *fullpath;
		char *cmd       = opt.script_argv[0];
		int  mode       = R_OK;

		if ((fullpath = search_path(opt.cwd, cmd, true, mode))) {
			xfree(opt.script_argv[0]);
			opt.script_argv[0] = fullpath;
		} 

		return opt.script_argv[0];
	} else {
		return NULL;
	}
}

/* process options:
 * 1. update options with option set in the script
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int process_options_second_pass(int argc, char *argv[],
				const void *script_body, int script_size)
{
	/* set options from batch script */
	_opt_batch_script(script_body, script_size);

	/* set options from pbs batch script */
	_opt_pbs_batch_script(script_body, script_size);

	/* set options from env vars */
	_opt_env();

	/* set options from command line */
	_set_options(argc, argv);

	if (!_opt_verify())
		exit(1);

	if (opt.verbose > 3)
		_opt_list();

	return 1;

}

/*
 * _next_line - Interpret the contents of a byte buffer as characters in
 *	a file.  _next_line will find and return the next line in the buffer.
 *
 *	If "state" is NULL, it will start at the beginning of the buffer.
 *	_next_line will update the "state" pointer to point at the
 *	spot in the buffer where it left off.
 *
 * IN buf - buffer containing file contents
 * IN size - size of buffer "buf"
 * IN/OUT state - used by _next_line to determine where the last line ended
 *
 * RET - xmalloc'ed character string, or NULL if no lines remaining in buf.
 */
static char *_next_line(const void *buf, int size, void **state)
{
	char *line;
	char *current, *ptr;

	if (*state == NULL) /* initial state */
		*state = (void *)buf;

	if ((*state - buf) >= size) /* final state */
		return NULL;

	ptr = current = (char *)*state;
	while ((*ptr != '\n') && (ptr < ((char *)buf+size)))
		ptr++;
	if (*ptr == '\n')
		ptr++;
	
	line = xstrndup(current, (ptr-current));

	*state = (void *)ptr;
	return line;
}

/*
 * _get_argument - scans a line for something that looks like a command line
 *	argument, and return an xmalloc'ed string containing the argument.
 *	Quotes can be used to group characters, including whitespace.
 *	Quotes can be included in an argument be escaping the quotes,
 *	preceding the quote with a backslash (\").
 *
 * IN - line
 * OUT - skipped - number of characters parsed from line
 * RET - xmalloc'ed argument string (may be shorter than "skipped")
 *       or NULL if no arguments remaining
 */
static char *_get_argument(const char *line, int *skipped)
{
	char *ptr;
	char argument[BUFSIZ];
	bool escape_flag = false;
	bool no_isspace_check = false;
	int i;

	ptr = (char *)line;
	*skipped = 0;

	/* skip whitespace */
	while (isspace(*ptr) && *ptr != '\0') {
		ptr++;
	}

	if (*ptr == '\0')
		return NULL;

	/* copy argument into "argument" buffer, */
	i = 0;
	while ((no_isspace_check || !isspace(*ptr))
	       && *ptr != '\n'
	       && *ptr != '\0') {

		if (escape_flag) {
			escape_flag = false;
			argument[i] = *ptr;
			ptr++;
			i++;
		} else if (*ptr == '\\') {
			escape_flag = true;
			ptr++;
		} else if (*ptr == '"') {
			/* toggle the no_isspace_check flag */
			no_isspace_check = no_isspace_check? false : true;
			ptr++;
		} else if (*ptr == '#') {
			/* found an un-escaped #, rest of line is a comment */
			break;
		} else {
			argument[i] = *ptr;
			ptr++;
			i++;
		}
	}

	*skipped = ptr - line;
	if (i > 0) {
		return xstrndup(argument, i);
	} else {
		return NULL;
	}
}

/*
 * set options from batch script
 *
 * Build an argv-style array of options from the script "body",
 * then pass the array to _set_options for() further parsing.
 */
static void _opt_batch_script(const void *body, int size)
{
	char *magic_word1 = "#SBATCH";
	char *magic_word2 = "#SLURM";
	int magic_word_len1, magic_word_len2;
	int argc;
	char **argv;
	void *state = NULL;
	char *line;
	char *option;
	char *ptr;
	int skipped = 0, warned = 0;
	int i;

	magic_word_len1 = strlen(magic_word1);
	magic_word_len2 = strlen(magic_word2);

	/* getopt_long skips over the first argument, so fill it in */
	argc = 1;
	argv = xmalloc(sizeof(char *));
	argv[0] = "sbatch";

	while((line = _next_line(body, size, &state)) != NULL) {
		if (!strncmp(line, magic_word1, magic_word_len1))
			ptr = line + magic_word_len1;
		else if (!strncmp(line, magic_word2, magic_word_len2)) {
			ptr = line + magic_word_len2;
			if (!warned) {
				error("Change from #SLURM to #SBATCH in your "
				      "script and verify the options are "
				      "valid in sbatch");
				warned = 1;
			}
		} else {
			xfree(line);
			continue;
		}

		/* this line starts with the magic word */
		while ((option = _get_argument(ptr, &skipped)) != NULL) {
			debug2("Found in script, argument \"%s\"", option);
			argc += 1;
			xrealloc(argv, sizeof(char*) * argc);
			argv[argc-1] = option;
			ptr += skipped;
		}
		xfree(line);
	}

	if (argc > 0)
		_set_options(argc, argv);

	for (i = 1; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);
}

/*
 * set pbs options from batch script
 *
 * Build an argv-style array of options from the script "body",
 * then pass the array to _set_options for() further parsing.
 */
static void _opt_pbs_batch_script(const void *body, int size)
{
	char *magic_word = "#PBS";
	int magic_word_len;
	int argc;
	char **argv;
	void *state = NULL;
	char *line;
	char *option;
	char *ptr;
	int skipped = 0;
	int i;

	magic_word_len = strlen(magic_word);
	/* getopt_long skips over the first argument, so fill it in */
	argc = 1;
	argv = xmalloc(sizeof(char *));
	argv[0] = "sbatch";

	while((line = _next_line(body, size, &state)) != NULL) {
		if (strncmp(line, magic_word, magic_word_len) != 0) {
			xfree(line);
			continue;
		}

		/* this line starts with the magic word */
		ptr = line + magic_word_len;
		while ((option = _get_argument(ptr, &skipped)) != NULL) {
			debug2("Found in script, argument \"%s\"", option);
			argc += 1;
			xrealloc(argv, sizeof(char*) * argc);
			argv[argc-1] = option;
			ptr += skipped;
		}
		xfree(line);
	}

	if (argc > 0)
		_set_pbs_options(argc, argv);

	for (i = 1; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);
}

static void _set_options(int argc, char **argv)
{
	int opt_char, option_index = 0;
	char *tmp;

	optind = 0;
	while((opt_char = getopt_long(argc, argv, opt_string,
				      long_options, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			fatal("Try \"sbatch --help\" for more information");
			break;
		case 'b':
			/* Only here for Moab transition not suppose
			   to do anything */
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
/*		case 'd': See 'P' below */
		case 'D':
			xfree(opt.cwd);
			opt.cwd = xstrdup(optarg);
			break;
		case 'e':
			xfree(opt.efname);
			if (strncasecmp(optarg, "none", (size_t)4) == 0)
				opt.efname = xstrdup("/dev/null");
			else
				opt.efname = xstrdup(optarg);
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
		case 'i':
			xfree(opt.ifname);
			if (strncasecmp(optarg, "none", (size_t)4) == 0)
				opt.ifname = xstrdup("/dev/null");
			else
				opt.ifname = xstrdup(optarg);
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
			setenv("SLURM_DISTRIBUTION", optarg, 1);
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
				error("invalid node count `%s'", 
				      optarg);
				exit(1);
			}
			break;
		case 'o':
			xfree(opt.ofname);
			if (strncasecmp(optarg, "none", (size_t)4) == 0)
				opt.ofname = xstrdup("/dev/null");
			else
				opt.ofname = xstrdup(optarg);
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
			/* use -P instead */
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
                case LONG_OPT_CPU_BIND:
			if (slurm_verify_cpu_bind(optarg, &opt.cpu_bind,
						  &opt.cpu_bind_type))
				exit(1);
			break;
		case LONG_OPT_MEM_BIND:
			if (slurm_verify_mem_bind(optarg, &opt.mem_bind,
						  &opt.mem_bind_type))
				exit(1);
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
		case LONG_OPT_JOBID:
			opt.jobid = _get_int(optarg, "jobid");
			opt.jobid_set = true;
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
		case LONG_OPT_NO_REQUEUE:
			opt.requeue = 0;
			break;
		case LONG_OPT_REQUEUE:
			opt.requeue = 1;
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
			setenvf(NULL, "SLURM_NTASKS_PER_NODE", "%d",
				opt.ntasks_per_node);
			break;
		case LONG_OPT_NTASKSPERSOCKET:
			opt.ntasks_per_socket = _get_int(optarg, 
				"ntasks-per-socket");
			setenvf(NULL, "SLURM_NTASKS_PER_SOCKET", "%d",
				opt.ntasks_per_socket);
			break;
		case LONG_OPT_NTASKSPERCORE:
			opt.ntasks_per_core = _get_int(optarg,
				"ntasks-per-core");
			setenvf(NULL, "SLURM_NTASKS_PER_CORE", "%d",
				opt.ntasks_per_socket);
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
		case LONG_OPT_REBOOT:
			opt.reboot = true;
			break;
		case LONG_OPT_WRAP:
			/* handled in process_options_first_pass() */
			break;
		case LONG_OPT_GET_USER_ENV:
			if (optarg)
				_proc_get_user_env(optarg);
			else
				opt.get_user_env_time = 0;
			break;
		case LONG_OPT_OPEN_MODE:
			if ((optarg[0] == 'a') || (optarg[0] == 'A'))
				opt.open_mode = OPEN_MODE_APPEND;
			else if ((optarg[0] == 't') || (optarg[0] == 'T'))
				opt.open_mode = OPEN_MODE_TRUNCATE;
			else {
				error("Invalid --open-mode argument: %s. "
				      "Ignored", optarg);
			}
			break;
		case LONG_OPT_ACCTG_FREQ:
			opt.acctg_freq = _get_int(optarg, "acctg-freq");
			break;
		case LONG_OPT_PROPAGATE:
			xfree(opt.propagate);
			if (optarg)
				opt.propagate = xstrdup(optarg);
			else
				opt.propagate = xstrdup("ALL");
			break;
		case LONG_OPT_NETWORK:
			xfree(opt.network);
			opt.network = xstrdup(optarg);
			break;
		default:
			fatal("Unrecognized command line parameter %c",
			      opt_char);
		}
	}

	if (optind < argc) {
		fatal("Invalid argument: %s", argv[optind]);
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

static void _set_pbs_options(int argc, char **argv)
{
	int opt_char, option_index = 0;
	
	char *pbs_opt_string = "+a:A:c:C:e:hIj:k:l:m:M:N:o:p:q:r:S:u:v:VWz";

	struct option pbs_long_options[] = {
		{"start_time", required_argument, 0, 'a'},
		{"account", required_argument, 0, 'A'},
		{"checkpoint", required_argument, 0, 'c'},
		{"working_dir", required_argument, 0, 'C'},
		{"error", required_argument, 0, 'e'},
		{"hold", no_argument, 0, 'h'},
		{"interactive", no_argument, 0, 'I'},
		{"join", optional_argument, 0, 'j'},
		{"keep", required_argument, 0, 'k'},
		{"resource_list", required_argument, 0, 'l'},
		{"mail_options", required_argument, 0, 'm'},
		{"mail_user_list", required_argument, 0, 'M'},
		{"job_name", required_argument, 0, 'N'},
		{"out", required_argument, 0, 'o'},
		{"priority", required_argument, 0, 'p'},
		{"destination", required_argument, 0, 'q'},
		{"rerunable", required_argument, 0, 'r'},
		{"script_path", required_argument, 0, 'S'},
		{"running_user", required_argument, 0, 'u'},
		{"variable_list", required_argument, 0, 'v'},
		{"all_env", no_argument, 0, 'V'},
		{"attributes", no_argument, 0, 'W'},
		{"no_std", no_argument, 0, 'z'},
		{NULL, 0, 0, 0}
	};


	optind = 0;
	while((opt_char = getopt_long(argc, argv, pbs_opt_string,
				      pbs_long_options, &option_index))
	      != -1) {
		switch (opt_char) {
		case 'a':
			opt.begin = parse_time(optarg, 0);			
			break;
		case 'A':
			xfree(opt.account);
			opt.account = xstrdup(optarg);
			break;
		case 'c':
			break;
		case 'C':
			xfree(opt.cwd);
			opt.cwd = xstrdup(optarg);
			break;
		case 'e':
			xfree(opt.efname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.efname = xstrdup("/dev/null");
			else
				opt.efname = xstrdup(optarg);
			break;
		case 'h':
			opt.hold = true;
			break;
		case 'I':
			break;
		case 'j':
			break;
		case 'k':
			break;
		case 'l':
			_parse_pbs_resource_list(optarg);
			break;
		case 'm':
			opt.mail_type |= _parse_pbs_mail_type(optarg);
			if (opt.mail_type == 0)
				fatal("-m=%s invalid", optarg);
			break;
		case 'M':
			xfree(opt.mail_user);
			opt.mail_user = xstrdup(optarg);
			break;
		case 'N':
			xfree(opt.job_name);
			opt.job_name = xstrdup(optarg);
			break;
		case 'o':
			xfree(opt.ofname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.ofname = xstrdup("/dev/null");
			else
				opt.ofname = xstrdup(optarg);
			break;
		case 'p':
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
		case 'q':
			xfree(opt.partition);
			opt.partition = xstrdup(optarg);
			break;
		case 'r':
			break;
		case 'S':
			break;
		case 'u':
			break;
		case 'v':
			break;
		case 'V':
			break;
		case 'W':
			xfree(opt.constraints);
			opt.constraints = xstrdup(optarg);
			break;
		case 'z':
			break;
		default:
			fatal("Unrecognized command line parameter %c",
			      opt_char);
		}
	}

	if (optind < argc) {
		fatal("Invalid argument: %s", argv[optind]);
	}
}

static char *_get_pbs_node_name(char *node_options, int *i)
{
	int start = (*i);
	char *value = NULL;

	while(node_options[*i] 
	      && node_options[*i] != '+'
	      && node_options[*i] != ':') 
		(*i)++;

	value = xmalloc((*i)-start+1);		
	memcpy(value, node_options+start, (*i)-start);

	if(node_options[*i])
		(*i)++;

	return value;
}

static void _get_next_pbs_node_part(char *node_options, int *i)
{
	while(node_options[*i] 
	      && node_options[*i] != '+'
	      && node_options[*i] != ':') 
		(*i)++;
	if(node_options[*i])
		(*i)++;
}

static void _parse_pbs_nodes_opts(char *node_opts)
{
	int i = 0;
	char *temp = NULL;
	char buf[255];
	int ppn = 0;
	int node_cnt = 0;
	hostlist_t hl = hostlist_create(NULL);

	while(node_opts[i]) {
		if(!strncmp(node_opts+i, "ppn=", 4)) {
			i+=4;
			ppn += strtol(node_opts+i, NULL, 10);
			_get_next_pbs_node_part(node_opts, &i);
		} else if(isdigit(node_opts[i])) {
			node_cnt += strtol(node_opts+i, NULL, 10);
			_get_next_pbs_node_part(node_opts, &i);
		} else if(isalpha(node_opts[i])) {
			temp = _get_pbs_node_name(node_opts, &i);
			hostlist_push(hl, temp);
			xfree(temp);
		} else 
			i++;
		
	}

	if(!node_cnt) 
		node_cnt = 1;
	else {
		opt.nodes_set = true;
		opt.min_nodes = opt.max_nodes = node_cnt;
	}
	
	if(ppn) {
		ppn *= node_cnt;
		opt.nprocs_set = true;
		opt.nprocs = ppn;
	}

	if(hostlist_count(hl) > 0) {
		hostlist_ranged_string(hl, sizeof(buf), buf);
		xfree(opt.nodelist);
		opt.nodelist = xstrdup(buf);
#ifdef HAVE_BG
		info("\tThe nodelist option should only be used if\n"
		     "\tthe block you are asking for can be created.\n"
		     "\tPlease consult smap before using this option\n"
		     "\tor your job may be stuck with no way to run.");
#endif
	}

	hostlist_destroy(hl);
}

static void _get_next_pbs_option(char *pbs_options, int *i)
{
	while(pbs_options[*i] && pbs_options[*i] != ',') 
		(*i)++;
	if(pbs_options[*i])
		(*i)++;
}

static char *_get_pbs_option_value(char *pbs_options, int *i)
{
	int start = (*i);
	char *value = NULL;

	while(pbs_options[*i] && pbs_options[*i] != ',') 
		(*i)++;
	value = xmalloc((*i)-start+1);		
	memcpy(value, pbs_options+start, (*i)-start);

	if(pbs_options[*i])
		(*i)++;

	return value;
}

static void _parse_pbs_resource_list(char *rl)
{
	int i = 0;
	char *temp = NULL;

	while(rl[i]) {
		if(!strncmp(rl+i, "arch=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "cput=", 5)) {
			i+=5;
			temp = _get_pbs_option_value(rl, &i);
			if(!temp) 
				fatal("No value given for cput");
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(temp);
			xfree(temp);
		} else if(!strncmp(rl+i, "file=", 5)) {
			int end = 0;

			i+=5;
			temp = _get_pbs_option_value(rl, &i);
			if(!temp) 
				fatal("No value given for file");

			end = strlen(temp) - 1;
			if (toupper(temp[end]) == 'B') {
				/* In Torque they do GB or MB on the
				 * end of size, we just want G or M so
				 * we will remove the b on the end
				 */
				temp[end] = '\0';
			}
			opt.tmpdisk = str_to_bytes(temp);
			if (opt.tmpdisk < 0) {
				error("invalid tmp value %s", temp);
				exit(1);
			}
			xfree(temp);
		} else if(!strncmp(rl+i, "host=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "mem=", 4)) {
			int end = 0;

			i+=4;
			temp = _get_pbs_option_value(rl, &i);
			if(!temp) 
				fatal("No value given for mem");
			end = strlen(temp) - 1;
			if (toupper(temp[end]) == 'B') {
				/* In Torque they do GB or MB on the
				 * end of size, we just want G or M so
				 * we will remove the b on the end
				 */
				temp[end] = '\0';
			}
			opt.realmem = (int) str_to_bytes(temp);
			if (opt.realmem < 0) {
				error("invalid memory constraint %s", 
				      temp);
				exit(1);
			}

			xfree(temp);
		} else if(!strncmp(rl+i, "nice=", 5)) {
			i+=5;
			temp = _get_pbs_option_value(rl, &i);
			if (temp)
				opt.nice = strtol(temp, NULL, 10);
			else
				opt.nice = 100;
			if (abs(opt.nice) > NICE_OFFSET) {
				error("Invalid nice value, must be between "
				      "-%d and %d", NICE_OFFSET, NICE_OFFSET);
				exit(1);
			}
			xfree(temp);
		} else if(!strncmp(rl+i, "nodes=", 6)) {
			i+=6;
			temp = _get_pbs_option_value(rl, &i);
			if(!temp) 
				fatal("No value given for nodes");
			
			_parse_pbs_nodes_opts(temp);
			xfree(temp);
		} else if(!strncmp(rl+i, "opsys=", 6)) {
 			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "other=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "pcput=", 6)) {
			i+=6;
			temp = _get_pbs_option_value(rl, &i);
			if(!temp) 
				fatal("No value given for pcput");
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(temp);
			xfree(temp);
		} else if(!strncmp(rl+i, "pmem=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "pvmem=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "software=", 9)) {
			i+=9;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "vmem=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if(!strncmp(rl+i, "walltime=", 9)) {
			i+=9;
			temp = _get_pbs_option_value(rl, &i);
			if(!temp) 
				fatal("No value given for walltime");
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(temp);
			xfree(temp);
		} else 
			i++;
	}
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

	_fullpath(&opt.efname, opt.cwd);
	_fullpath(&opt.ifname, opt.cwd);
	_fullpath(&opt.ofname, opt.cwd);

	if (opt.mincpus < opt.cpus_per_task)
		opt.mincpus = opt.cpus_per_task;

	if ((opt.job_name == NULL) && (opt.script_argc > 0))
		opt.job_name = base_name(opt.script_argv[0]);
	if (opt.job_name)
		setenv("SLURM_JOB_NAME", opt.job_name, 0);

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
		if ((opt.min_nodes <= 0) ||	
		    ((opt.nprocs/opt.plane_size) < opt.min_nodes)) {
			if (((opt.min_nodes-1)*opt.plane_size) >= opt.nprocs) {
#if(0)
				info("Too few processes ((n/plane_size) %d < N %d) "
				     "and ((N-1)*(plane_size) %d >= n %d)) ",
				     opt.nprocs/opt.plane_size, opt.min_nodes, 
				     (opt.min_nodes-1)*opt.plane_size, opt.nprocs);
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
	if ((opt.nodes_set || opt.extra_set) && !opt.nprocs_set) {
		/* 1 proc / node default */
		opt.nprocs = MAX(opt.min_nodes, 1);

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

	if ((opt.euid != (uid_t) -1) && (opt.euid != opt.uid)) 
		opt.uid = opt.euid;

	if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid)) 
		opt.gid = opt.egid;

	if (opt.immediate) {
		char *sched_name = slurm_get_sched_type();
		if (strcmp(sched_name, "sched/wiki") == 0) {
			info("WARNING: Ignoring the -I/--immediate option "
				"(not supported by Maui)");
			opt.immediate = false;
		}
		xfree(sched_name);
	}

	if (opt.open_mode) {
		/* Propage mode to spawned job using environment variable */
		if (opt.open_mode == OPEN_MODE_APPEND)
			setenvf(NULL, "SLURM_OPEN_MODE", "a");
		else
			setenvf(NULL, "SLURM_OPEN_MODE", "t");
	}
	if (opt.cpus_per_task > 1)
		setenvfs("SLURM_CPUS_PER_TASK=%d", opt.cpus_per_task); 
	if (opt.dependency)
		setenvfs("SLURM_JOB_DEPENDENCY=%s", opt.dependency);

	if (opt.acctg_freq >= 0)
		setenvf(NULL, "SLURM_ACCTG_FREQ", "%d", opt.acctg_freq); 

#ifdef HAVE_AIX
	if (opt.network == NULL)
		opt.network = "us,sn_all,bulk_xfer";
	setenv("SLURM_NETWORK", opt.network, 1);
#endif

	if (slurm_verify_cpu_bind(NULL, &opt.cpu_bind,
				  &opt.cpu_bind_type))
		exit(1);
	if (opt.cpu_bind_type && (getenv("SLURM_CPU_BIND") == NULL)) {
		char tmp[64];
		slurm_sprint_cpu_bind_type(tmp, opt.cpu_bind_type);
		if (opt.cpu_bind) {
			setenvf(NULL, "SLURM_CPU_BIND", "%s:%s", 
				tmp, opt.cpu_bind);
		} else {
			setenvf(NULL, "SLURM_CPU_BIND", "%s", tmp);
		}
	}
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

	return verified;
}

static uint16_t _parse_pbs_mail_type(const char *arg)
{
	uint16_t rc;

	if (strcasecmp(arg, "b") == 0)
		rc = MAIL_JOB_BEGIN;
	else if  (strcasecmp(arg, "e") == 0)
		rc = MAIL_JOB_END;
	else if (strcasecmp(arg, "a") == 0)
		rc = MAIL_JOB_FAIL;
	else if (strcasecmp(arg, "bea") == 0
		|| strcasecmp(arg, "eba") == 0
		|| strcasecmp(arg, "eab") == 0
		|| strcasecmp(arg, "bae") == 0)
		rc = MAIL_JOB_BEGIN | MAIL_JOB_END |  MAIL_JOB_FAIL;
	else if (strcasecmp(arg, "be") == 0
		|| strcasecmp(arg, "eb") == 0)
		rc = MAIL_JOB_BEGIN | MAIL_JOB_END;
	else if (strcasecmp(arg, "ba") == 0
		|| strcasecmp(arg, "ab") == 0)
		rc = MAIL_JOB_BEGIN | MAIL_JOB_FAIL;
	else if (strcasecmp(arg, "ea") == 0
		|| strcasecmp(arg, "ae") == 0)
		rc = MAIL_JOB_END |  MAIL_JOB_FAIL;
	else
		rc = 0;		/* arg="n" or failure */

	return rc;
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
 * Return an absolute path for the "filename".  If "filename" is already
 * an absolute path, it returns a copy.  Free the returned with xfree().
 */
static void _fullpath(char **filename, const char *cwd)
{
	char *ptr = NULL;

	if ((*filename == NULL) || (*filename[0] == '/'))
		return;

	ptr = xstrdup(cwd);
	xstrcat(ptr, "/");
	xstrcat(ptr, *filename);
	xfree(*filename);
	*filename = ptr;
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
	info("cwd            : %s", opt.cwd);
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
	info("jobid          : %u %s", opt.jobid, 
		opt.jobid_set ? "(set)" : "(default)");
	info("partition      : %s",
		opt.partition == NULL ? "default" : opt.partition);
	info("job name       : `%s'", opt.job_name);
	info("distribution   : %s", format_task_dist_states(opt.distribution));
	if(opt.distribution == SLURM_DIST_PLANE)
		info("plane size   : %u", opt.plane_size);
	info("verbose        : %d", opt.verbose);
	info("immediate      : %s", tf_(opt.immediate));
	if (opt.requeue != NO_VAL)
		info("requeue        : %u", opt.requeue);
	info("overcommit     : %s", tf_(opt.overcommit));
	if (opt.time_limit == INFINITE)
		info("time_limit     : INFINITE");
	else if (opt.time_limit != NO_VAL)
		info("time_limit     : %d", opt.time_limit);
	if (opt.nice)
		info("nice           : %d", opt.nice);
	info("account        : %s", opt.account);
	info("comment        : %s", opt.comment);
	info("dependency     : %s", opt.dependency);
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
	info("network        : %s", opt.network);

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
	info("mail_type         : %s", print_mail_type(opt.mail_type));
	info("mail_user         : %s", opt.mail_user);
	info("sockets-per-node  : %d - %d", opt.min_sockets_per_node,
					    opt.max_sockets_per_node);
	info("cores-per-socket  : %d - %d", opt.min_cores_per_socket,
					    opt.max_cores_per_socket);
	info("threads-per-core  : %d - %d", opt.min_threads_per_core,
					    opt.max_threads_per_core);
	info("ntasks-per-node   : %d", opt.ntasks_per_node);
	info("ntasks-per-socket : %d", opt.ntasks_per_socket);
	info("ntasks-per-core   : %d", opt.ntasks_per_core);
	info("cpu_bind          : %s", 
	     opt.cpu_bind == NULL ? "default" : opt.cpu_bind);
	info("mem_bind          : %s",
	     opt.mem_bind == NULL ? "default" : opt.mem_bind);
	info("plane_size        : %u", opt.plane_size);
	info("propagate         : %s",
	     opt.propagate == NULL ? "NONE" : opt.propagate);
	str = print_commandline(opt.script_argc, opt.script_argv);
	info("remote command    : `%s'", str);
	xfree(str);

}

static void _usage(void)
{
 	printf(
"Usage: sbatch [-N nnodes] [-n ntasks]\n"
"              [-c ncpus] [-r n] [-p partition] [--hold] [-t minutes]\n"
"              [-D path] [--immediate] [--no-kill] [--overcommit]\n"
"              [--input file] [--output file] [--error file]  [--licenses=names]\n"
"              [--workdir=directory] [--share] [-m dist] [-J jobname]\n"
"              [--jobid=id] [--verbose] [--gid=group] [--uid=user]\n"
"              [-W sec] [--minsockets=n] [--mincores=n] [--minthreads=n]\n"
"              [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"              [--account=name] [--dependency=type:jobid] [--comment=name]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
"              [--geometry=XxYxZ] [--conn-type=type] [--no-rotate] [ --reboot]\n"
"              [--blrts-image=path] [--linux-image=path]\n"
"              [--mloader-image=path] [--ramdisk-image=path]\n"
#endif
"              [--mail-type=type] [--mail-user=user][--nice[=value]]\n"
"              [--requeue] [--no-requeue] [--ntasks-per-node=n] [--propagate]\n"
"              [--nodefile=file] [--nodelist=hosts] [--exclude=hosts]\n"
"              [--network=type] [--mem-per-cpu=MB]\n"
"              [--cpu_bind=...] [--mem_bind=...]\n"
"              executable [args...]\n");
}

static void _help(void)
{
	slurm_ctl_conf_t *conf;

        printf (
"Usage: sbatch [OPTIONS...] executable [args...]\n"
"\n"
"Parallel run options:\n"
"  -n, --ntasks=ntasks         number of tasks to run\n"
"  -N, --nodes=N               number of nodes on which to run (N = min[-max])\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"      --ntasks-per-node=n     number of tasks to invoke on each node\n"
"  -i, --input=in              file for batch script's standard input\n"
"  -o, --output=out            file for batch script's standard output\n"
"  -e, --error=err             file for batch script's standard error\n"
"  -p, --partition=partition   partition requested\n"
"  -H, --hold                  submit job in held state\n"
"  -t, --time=minutes          time limit\n"
"  -D, --chdir=path            change remote current working directory\n"
"  -I, --immediate             exit if resources are not immediately available\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -s, --share                 share nodes with other jobs\n"
"  -J, --job-name=jobname      name of job\n"
"  -m, --distribution=type     distribution method for processes to nodes\n"
"                              (type = block|cyclic|arbitrary)\n"
"      --jobid=id              run under already allocated job\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -q, --quiet                 quiet mode (suppress informational messages)\n"
"  -P, --dependency=type:jobid defer job until condition on jobid is satisfied\n"
"  -D, --workdir=directory     set working directory for batch script\n"
"      --nice[=value]          decrease secheduling priority by value\n"
"  -O, --overcommit            overcommit resources\n"
"  -U, --account=name          charge job to specified account\n"
"      --begin=time            defer job until HH:MM DD/MM/YY\n"
"      --comment=name          arbitrary comment\n"
"  -L, --licenses=names        required license, comma separated\n"
"      --mail-type=type        notify on state change: BEGIN, END, FAIL or ALL\n"
"      --mail-user=user        who to send email notification for job state changes\n"
"      --gid=group_id          group ID to run job as (user root only)\n"
"      --uid=user_id           user ID to run job as (user root only)\n"
"      --get-user-env          used by Moab.  See srun man page.\n"
"      --no-requeue            if set, do not permit the job to be requeued\n"
"      --requeue               if set, permit the job to be requeued\n"
"      --propagate[=rlimits]   propagate all [or specific list of] rlimits\n"
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
"      --mem-per-cpu=MB        maximum amount of real memory per CPU\n"
"                              allocated to the job.\n" 
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
"                              (see \"--hint=help\" for options)\n"
"      --cpu_bind=             Bind tasks to CPUs\n"
"                              (see \"--cpu_bind=help\" for options)\n"
"      --mem_bind=             Bind memory to locality domains (ldom)\n"
"                              (see \"--mem_bind=help\" for options)\n");
	}
	slurm_conf_unlock();

        printf("\n"
#ifdef HAVE_AIX				/* AIX/Federation specific options */
"AIX related options:\n"
"      --network=type          communication protocol to be used\n"
"\n"
#endif
#ifdef HAVE_BG				/* Blue gene specific options */
"Blue Gene related options:\n"
"  -g, --geometry=XxYxZ        geometry constraints of the job\n"
"  -R, --no-rotate             disable geometry rotation\n"
"      --reboot                reboot block before starting job\n"
"      --conn-type=type        constraint on type of connection, MESH or TORUS\n"
"                              if not set, then tries to fit TORUS else MESH\n"
"      --blrts-image=path      path to blrts image for bluegene block.  Default if not set\n"
"      --linux-image=path      path to linux image for bluegene block.  Default if not set\n"
"      --mloader-image=path    path to mloader image for bluegene block.  Default if not set\n"
"      --ramdisk-image=path    path to ramdisk image for bluegene block.  Default if not set\n"
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
