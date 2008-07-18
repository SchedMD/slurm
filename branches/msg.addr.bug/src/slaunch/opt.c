/*****************************************************************************\
 *  opt.c - options processing for slaunch
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
#include <stdlib.h>		/* getenv     */
#include <pwd.h>		/* getpwuid   */
#include <ctype.h>		/* isdigit    */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <regex.h>

#include "src/slaunch/opt.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/plugstack.h"
#include "src/common/optz.h"
#include "src/common/read_config.h" /* getnodename() */
#include "src/common/hostlist.h"
#include "src/common/mpi.h"
#include "src/api/pmi_server.h"

#include "src/slaunch/attach.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE	0x00
#define OPT_INT		0x01
#define OPT_STRING	0x02
#define OPT_DEBUG	0x03
#define OPT_DISTRIB	0x04
#define OPT_BOOL	0x06
#define OPT_CORE	0x07
#define OPT_MPI		0x0c
#define OPT_CPU_BIND	0x0d
#define OPT_MEM_BIND	0x0e
#define OPT_MULTI	0x0f

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_USAGE			0x100
#define LONG_OPT_LAUNCH			0x103
#define LONG_OPT_JOBID			0x105
#define LONG_OPT_UID			0x106
#define LONG_OPT_GID			0x107
#define LONG_OPT_MPI			0x108
#define LONG_OPT_CORE			0x109
#define LONG_OPT_DEBUG_TS		0x10a
#define LONG_OPT_NETWORK		0x10b
#define LONG_OPT_PROPAGATE		0x10c
#define LONG_OPT_PROLOG			0x10d
#define LONG_OPT_EPILOG			0x10e
#define LONG_OPT_TASK_PROLOG		0x10f
#define LONG_OPT_TASK_EPILOG		0x110
#define LONG_OPT_CPU_BIND		0x111
#define LONG_OPT_MEM_BIND		0x112
#define LONG_OPT_COMM_HOSTNAME		0x113
#define LONG_OPT_MULTI			0x114
#define LONG_OPT_PMI_THREADS		0x115
#define LONG_OPT_LIN_FILTER		0x116
#define LONG_OPT_LOUT_FILTER		0x117
#define LONG_OPT_LERR_FILTER		0x118
#define LONG_OPT_RIN_FILTER		0x119
#define LONG_OPT_ROUT_FILTER		0x11a
#define LONG_OPT_RERR_FILTER		0x11b

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

/* return command name from its full path name */
static char * _base_name(char* command);

static List  _create_path_list(void);

/* Get a POSITIVE decimal integer from arg */
static int  _get_pos_int(const char *arg, const char *what);

/* Get a decimal integer from arg */
static int  _get_int(const char *arg, const char *what, bool positive);

static void  _help(void);

/* load a multi-program configuration file */
static void _load_multi(int *argc, char **argv);

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

/* search PATH for command returns full path */
static char *_search_path(char *, int);

static void  _usage(void);
static int   _verify_cpu_bind(const char *arg, char **cpu_bind,
			      cpu_bind_type_t *cpu_bind_type);
static int   _verify_mem_bind(const char *arg, char **mem_bind,
			      mem_bind_type_t *mem_bind_type);
static task_dist_states_t _verify_dist_type(const char *arg, uint32_t *psize);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char *argv[])
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

static void _print_version(void)
{
	printf("%s %s\n", PACKAGE, SLURM_VERSION);
}

/*
 * _isvalue
 * returns 1 is the argument appears to be a value, 0 otherwise
 */
static int _isvalue(char *arg) {
    	if (isdigit(*arg)) {	 /* decimal values and 0x... hex values */
	    	return 1;
	}

	while (isxdigit(*arg)) { /* hex values not preceded by 0x */
		arg++;
	}
	if (*arg == ',' || *arg == '\0') { /* end of field or string */
	    	return 1;
	}

	return 0;	/* not a value */
}

/*
 * verify cpu_bind arguments
 * returns -1 on error, 0 otherwise
 */
static int _verify_cpu_bind(const char *arg, char **cpu_bind, 
			    cpu_bind_type_t *cpu_bind_type)
{
	char *buf, *p, *tok;
	if (!arg) {
	    	return 0;
	}
	/* we support different launch policy names
	 * we also allow a verbose setting to be specified
	 *     --cpu_bind=threads
	 *     --cpu_bind=cores
	 *     --cpu_bind=sockets
	 *     --cpu_bind=v
	 *     --cpu_bind=rank,v
	 *     --cpu_bind=rank
	 *     --cpu_bind={MAP_CPU|MASK_CPU}:0,1,2,3,4
	 */
    	buf = xstrdup(arg);
    	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
	    	if ((p[0] == ',') && (!_isvalue(&(p[1]))))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((tok = strsep(&p, ";"))) {
		if (strcasecmp(tok, "help") == 0) {
			printf(
"CPU bind options:\n"
"    --cpu_bind=         Bind tasks to CPUs\n"
"        q[uiet]         quietly bind before task runs (default)\n"
"        v[erbose]       verbosely report binding before task runs\n"
"        no[ne]          don't bind tasks to CPUs (default)\n"
"        rank            bind by task rank\n"
"        map_cpu:<list>  specify a CPU ID binding for each task\n"
"                        where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"        mask_cpu:<list> specify a CPU ID binding mask for each task\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        sockets         auto-generated masks bind to sockets\n"
"        cores           auto-generated masks bind to cores\n"
"        threads         auto-generated masks bind to threads\n"
"        help            show this help message\n");
			return 1;
		} else if ((strcasecmp(tok, "q") == 0) ||
			   (strcasecmp(tok, "quiet") == 0)) {
		        *cpu_bind_type &= ~CPU_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "v") == 0) ||
			   (strcasecmp(tok, "verbose") == 0)) {
		        *cpu_bind_type |= CPU_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "no") == 0) ||
			   (strcasecmp(tok, "none") == 0)) {
			*cpu_bind_type |=  CPU_BIND_NONE;
			*cpu_bind_type &= ~CPU_BIND_RANK;
			*cpu_bind_type &= ~CPU_BIND_MAP;
			*cpu_bind_type &= ~CPU_BIND_MASK;
			xfree(*cpu_bind);
		} else if (strcasecmp(tok, "rank") == 0) {
			*cpu_bind_type &= ~CPU_BIND_NONE;
			*cpu_bind_type |=  CPU_BIND_RANK;
			*cpu_bind_type &= ~CPU_BIND_MAP;
			*cpu_bind_type &= ~CPU_BIND_MASK;
			xfree(*cpu_bind);
		} else if ((strncasecmp(tok, "map_cpu", 7) == 0) ||
		           (strncasecmp(tok, "mapcpu", 6) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			*cpu_bind_type &= ~CPU_BIND_NONE;
			*cpu_bind_type &= ~CPU_BIND_RANK;
			*cpu_bind_type |=  CPU_BIND_MAP;
			*cpu_bind_type &= ~CPU_BIND_MASK;
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = xstrdup(list);
			} else {
				error("missing list for \"--cpu_bind=map_cpu:<list>\"");
				xfree(buf);
				return 1;
			}
		} else if ((strncasecmp(tok, "mask_cpu", 8) == 0) ||
		           (strncasecmp(tok, "maskcpu", 7) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			*cpu_bind_type &= ~CPU_BIND_NONE;
			*cpu_bind_type &= ~CPU_BIND_RANK;
			*cpu_bind_type &= ~CPU_BIND_MAP;
			*cpu_bind_type |=  CPU_BIND_MASK;
			xfree(*cpu_bind);
			if (list && *list) {
				*cpu_bind = xstrdup(list);
			} else {
				error("missing list for \"--cpu_bind=mask_cpu:<list>\"");
				xfree(buf);
				return 1;
			}
		} else if ((strcasecmp(tok, "socket") == 0) ||
		           (strcasecmp(tok, "sockets") == 0)) {
			*cpu_bind_type |=  CPU_BIND_TO_SOCKETS;
			*cpu_bind_type &= ~CPU_BIND_TO_CORES;
			*cpu_bind_type &= ~CPU_BIND_TO_THREADS;
		} else if ((strcasecmp(tok, "core") == 0) ||
		           (strcasecmp(tok, "cores") == 0)) {
			*cpu_bind_type &= ~CPU_BIND_TO_SOCKETS;
			*cpu_bind_type |=  CPU_BIND_TO_CORES;
			*cpu_bind_type &= ~CPU_BIND_TO_THREADS;
		} else if ((strcasecmp(tok, "thread") == 0) ||
		           (strcasecmp(tok, "threads") == 0)) {
			*cpu_bind_type &= ~CPU_BIND_TO_SOCKETS;
			*cpu_bind_type &= ~CPU_BIND_TO_CORES;
			*cpu_bind_type |=  CPU_BIND_TO_THREADS;
		} else {
			error("unrecognized --cpu_bind argument \"%s\"", tok);
			xfree(buf);
			return 1;
		}
	}

	xfree(buf);
	return 0;
}

/*
 * verify mem_bind arguments
 * returns -1 on error, 0 otherwise
 */
static int _verify_mem_bind(const char *arg, char **mem_bind, 
			    mem_bind_type_t *mem_bind_type)
{
	char *buf, *p, *tok;
	if (!arg) {
	    	return 0;
	}
	/* we support different memory binding names
	 * we also allow a verbose setting to be specified
	 *     --mem_bind=v
	 *     --mem_bind=rank,v
	 *     --mem_bind=rank
	 *     --mem_bind={MAP_MEM|MASK_MEM}:0,1,2,3,4
	 */
    	buf = xstrdup(arg);
    	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
	    	if ((p[0] == ',') && (!_isvalue(&(p[1]))))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((tok = strsep(&p, ";"))) {
		if (strcasecmp(tok, "help") == 0) {
			printf(
"Memory bind options:\n"
"    --mem_bind=         Bind memory to locality domains (ldom)\n"
"        q[uiet]         quietly bind before task runs (default)\n"
"        v[erbose]       verbosely report binding before task runs\n"
"        no[ne]          don't bind tasks to memory (default)\n"
"        rank            bind by task rank\n"
"        local           bind to memory local to processor\n"
"        map_mem:<list>  specify a memory binding for each task\n"
"                        where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"        mask_mem:<list> specify a memory binding mask for each tasks\n"
"                        where <list> is <mask1>,<mask2>,...<maskN>\n"
"        help            show this help message\n");
			return 1;
			
		} else if ((strcasecmp(tok, "q") == 0) ||
			   (strcasecmp(tok, "quiet") == 0)) {
		        *mem_bind_type &= ~MEM_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "v") == 0) ||
			   (strcasecmp(tok, "verbose") == 0)) {
		        *mem_bind_type |= MEM_BIND_VERBOSE;
		} else if ((strcasecmp(tok, "no") == 0) ||
			   (strcasecmp(tok, "none") == 0)) {
			*mem_bind_type |=  MEM_BIND_NONE;
			*mem_bind_type &= ~MEM_BIND_RANK;
			*mem_bind_type &= ~MEM_BIND_LOCAL;
			*mem_bind_type &= ~MEM_BIND_MAP;
			*mem_bind_type &= ~MEM_BIND_MASK;
			xfree(*mem_bind);
		} else if (strcasecmp(tok, "rank") == 0) {
			*mem_bind_type &= ~MEM_BIND_NONE;
			*mem_bind_type |=  MEM_BIND_RANK;
			*mem_bind_type &= ~MEM_BIND_LOCAL;
			*mem_bind_type &= ~MEM_BIND_MAP;
			*mem_bind_type &= ~MEM_BIND_MASK;
			xfree(*mem_bind);
		} else if (strcasecmp(tok, "local") == 0) {
			*mem_bind_type &= ~MEM_BIND_NONE;
			*mem_bind_type &= ~MEM_BIND_RANK;
			*mem_bind_type |=  MEM_BIND_LOCAL;
			*mem_bind_type &= ~MEM_BIND_MAP;
			*mem_bind_type &= ~MEM_BIND_MASK;
			xfree(*mem_bind);
		} else if ((strncasecmp(tok, "map_mem", 7) == 0) ||
		           (strncasecmp(tok, "mapmem", 6) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			*mem_bind_type &= ~MEM_BIND_NONE;
			*mem_bind_type &= ~MEM_BIND_RANK;
			*mem_bind_type &= ~MEM_BIND_LOCAL;
			*mem_bind_type |=  MEM_BIND_MAP;
			*mem_bind_type &= ~MEM_BIND_MASK;
			xfree(*mem_bind);
			if (list && *list) {
				*mem_bind = xstrdup(list);
			} else {
				error("missing list for \"--mem_bind=map_mem:<list>\"");
				xfree(buf);
				return 1;
			}
		} else if ((strncasecmp(tok, "mask_mem", 8) == 0) ||
		           (strncasecmp(tok, "maskmem", 7) == 0)) {
			char *list;
			list = strsep(&tok, ":=");
			list = strsep(&tok, ":=");
			*mem_bind_type &= ~MEM_BIND_NONE;
			*mem_bind_type &= ~MEM_BIND_RANK;
			*mem_bind_type &= ~MEM_BIND_LOCAL;
			*mem_bind_type &= ~MEM_BIND_MAP;
			*mem_bind_type |=  MEM_BIND_MASK;
			xfree(*mem_bind);
			if (list && *list) {
				*mem_bind = xstrdup(list);
			} else {
				error("missing list for \"--mem_bind=mask_mem:<list>\"");
				xfree(buf);
				return 1;
			}
		} else {
			error("unrecognized --mem_bind argument \"%s\"", tok);
			xfree(buf);
			return 1;
		}
	}

	xfree(buf);
	return 0;
}
/* 
 * verify that a distribution type in arg is of a known form
 * returns the task_dist_states, or -1 if state is unknown
 */
static task_dist_states_t _verify_dist_type(const char *arg, 
					    uint32_t *plane_size)
{
	int len = strlen(arg);
	char *dist_str = NULL;
	task_dist_states_t result = SLURM_DIST_UNKNOWN;
	bool lllp_dist = false, plane_dist = false;

	dist_str = strchr(arg,':');
	if (dist_str != NULL) {
		/* -m cyclic|block:cyclic|block */
		lllp_dist = true;
	} else {
		/* -m plane=<plane_size> */
		dist_str = strchr(arg,'=');
		if(dist_str != NULL) {
			*plane_size=atoi(dist_str+1);
			len = dist_str-arg;
			plane_dist = true;
		}
	}

	if (lllp_dist) {
		if (strcasecmp(arg, "cyclic:cyclic") == 0) {
			result = SLURM_DIST_CYCLIC_CYCLIC;
		} else if (strcasecmp(arg, "cyclic:block") == 0) {
			result = SLURM_DIST_CYCLIC_BLOCK;
		} else if (strcasecmp(arg, "block:block") == 0) {
			result = SLURM_DIST_BLOCK_BLOCK;
		} else if (strcasecmp(arg, "block:cyclic") == 0) {
			result = SLURM_DIST_BLOCK_CYCLIC;
		}
	} else if (plane_dist) {
		if (strncasecmp(arg, "plane", len) == 0) {
			result = SLURM_DIST_PLANE;
		}
	} else {
		if (strncasecmp(arg, "cyclic", len) == 0) {
			result = SLURM_DIST_CYCLIC;
		} else if (strncasecmp(arg, "block", len) == 0) {
			result = SLURM_DIST_BLOCK;
		} else if ((strncasecmp(arg, "arbitrary", len) == 0) ||
		           (strncasecmp(arg, "hostfile", len) == 0)) {
			result = SLURM_DIST_ARBITRARY;
		}
	}

	return result;
}

/*
 *  Parse the next greatest of:
 *     CPUS(REPS),
 *  or
 *     CPUS(REPS)
 *  or
 *     CPUS,
 *  or
 *     CPUS
 *  moving "ptr" past the parsed cpu/reps pair
 *
 * Return 1 after succesfully parsing a new number or pair, and 0 otherwise.
 */
static int _parse_cpu_rep_pair(char **ptr, uint32_t *cpu, uint32_t *rep)
{
	char *endptr;

	*rep = 1;
	*cpu = strtol(*ptr, &endptr, 10);
	if (*cpu == 0 && endptr == *ptr) {
		/* no more numbers */
		return 0;
	}

	if (endptr[0] == (char)',') {
		*ptr = endptr+1;
		return 1;
	} else if (endptr[0] == (char)'('
		   && endptr[1] == (char)'x') {
		*ptr = endptr+2;
		*rep = strtol(*ptr, &endptr, 10);
		if (*rep == 0 && endptr == *ptr) {
			error("was expecting a number at \"%s\"", *ptr);
			return 0;
		}
		if (endptr[0] != (char)')') {
			error("was expecting a closing parenthasis at \"%s\"",
			      endptr);
			return 0;
		}
		endptr = endptr+1;

		/* finally, swallow the next comma, if there is one */
		if (endptr[0] == (char)',') {
			*ptr = endptr + 1;
		} else {
			*ptr = endptr;
		}
		return 1;
	} else {
		*ptr = endptr;
		return 1;
	}
}


/* Take a string representing cpus-per-node in compressed representation,
 * and set variables in "alloc_info" pertaining to cpus-per-node.
 */
static int _set_cpus_per_node(const char *str,
			      resource_allocation_response_msg_t *alloc_info)
{
	char *ptr = (char *)str;
	uint16_t num_cpus_groups = 0;
	uint32_t *cpus = NULL;
	uint32_t *cpus_reps = NULL;
	uint32_t cpu, rep;

	while (_parse_cpu_rep_pair(&ptr, &cpu, &rep)) {
		num_cpus_groups++;
		xrealloc(cpus, sizeof(uint32_t)*num_cpus_groups);
		xrealloc(cpus_reps, sizeof(uint32_t)*num_cpus_groups);
		cpus[num_cpus_groups-1] = cpu;
		cpus_reps[num_cpus_groups-1] = rep;
	}
	if (num_cpus_groups == 0)
		return 0;

	alloc_info->num_cpu_groups = num_cpus_groups;
	alloc_info->cpus_per_node = cpus;
	alloc_info->cpu_count_reps = cpus_reps;

	return 1;
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
		opt.progname ? opt.progname : "slaunch", buf);
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

	opt.gid = getgid();

	if ((getcwd(buf, MAXPATHLEN)) == NULL) 
		fatal("getcwd failed: %m");
	opt.cwd = xstrdup(buf);

	opt.progname = NULL;

	opt.num_tasks = 1;
	opt.num_tasks_set = false;
	opt.cpus_per_task = 1; 
	opt.cpus_per_task_set = false;
	opt.num_nodes = 1;
	opt.num_nodes_set = false;
	opt.cpu_bind_type = 0;
	opt.cpu_bind = NULL;
	opt.mem_bind_type = 0;
	opt.mem_bind = NULL;
	opt.relative = (uint16_t)NO_VAL;
	opt.relative_set = false;

	opt.job_name = NULL;
	opt.jobid    = NO_VAL;
	opt.jobid_set = false;

	opt.distribution      = SLURM_DIST_UNKNOWN;
	opt.plane_size        = NO_VAL;

	opt.local_ofname = NULL;
	opt.local_ifname = NULL;
	opt.local_efname = NULL;
	opt.remote_ofname = NULL;
	opt.remote_ifname = NULL;
	opt.remote_efname = NULL;
	opt.local_input_filter = (uint32_t)-1;
	opt.local_input_filter_set = false;
	opt.local_output_filter = (uint32_t)-1;
	opt.local_output_filter_set = false;
	opt.local_error_filter = (uint32_t)-1;
	opt.local_error_filter_set = false;
	opt.remote_input_filter = (uint32_t)-1;
	opt.remote_output_filter = (uint32_t)-1;
	opt.remote_error_filter = (uint32_t)-1;

	opt.core_type = CORE_DEFAULT;

	opt.labelio = false;
	opt.unbuffered = false;
	opt.overcommit = false;
	opt.no_kill = false;
	opt.kill_bad_exit = false;
	opt.max_wait	= slurm_get_wait_time();
	opt.quiet = 0;
	opt.verbose = 0;
	opt.slurmd_debug = LOG_LEVEL_QUIET;
	opt.nodelist	    = NULL;
	opt.nodelist_byid   = NULL;
	opt.task_layout = NULL;
	opt.task_layout_file_set = false;

	opt.euid	    = (uid_t) -1;
	opt.egid	    = (gid_t) -1;
	
	opt.propagate	    = NULL;  /* propagate specific rlimits */

	opt.prolog = slurm_get_srun_prolog();
	opt.epilog = slurm_get_srun_epilog();

	opt.task_prolog     = NULL;
	opt.task_epilog     = NULL;

	opt.comm_hostname  = xshort_hostname();
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
  /* SLURM_JOBID is handled like SLAUNCH_JOBID as backwards compatibility
     with LCRM.  If we get LCRM to call a slurm API function which
     tells LCRM which variables to set for a particular jobid number,
     then there would be no need for LCRM's static SLURM_JOBID code or
     the handling of SLURM_JOBID below.*/
  {"SLURM_JOBID",          OPT_INT,       &opt.jobid,         &opt.jobid_set },
  {"SLAUNCH_JOBID",        OPT_INT,       &opt.jobid,         &opt.jobid_set },
  {"SLURMD_DEBUG",         OPT_INT,       &opt.slurmd_debug,  NULL           },
  {"SLAUNCH_CORE_FORMAT",  OPT_CORE,      NULL,               NULL           },
  {"SLAUNCH_CPU_BIND",     OPT_CPU_BIND,  NULL,               NULL           },
  {"SLAUNCH_MEM_BIND",     OPT_MEM_BIND,  NULL,               NULL           },
  {"SLAUNCH_DEBUG",        OPT_DEBUG,     NULL,               NULL           },
  {"SLAUNCH_DISTRIBUTION", OPT_DISTRIB,   NULL,               NULL           },
  {"SLAUNCH_KILL_BAD_EXIT",OPT_BOOL,      &opt.kill_bad_exit, NULL           },
  {"SLAUNCH_LABELIO",      OPT_BOOL,      &opt.labelio,       NULL           },
  {"SLAUNCH_OVERCOMMIT",   OPT_BOOL,      &opt.overcommit,    NULL           },
  {"SLAUNCH_WAIT",         OPT_INT,       &opt.max_wait,      NULL           },
  {"SLAUNCH_MPI_TYPE",     OPT_MPI,       NULL,               NULL           },
  {"SLAUNCH_COMM_HOSTNAME",OPT_STRING,    &opt.comm_hostname, NULL           },
  {"SLAUNCH_PROLOG",       OPT_STRING,    &opt.prolog,        NULL           },
  {"SLAUNCH_EPILOG",       OPT_STRING,    &opt.epilog,        NULL           },
  {"SLAUNCH_TASK_PROLOG",  OPT_STRING,    &opt.task_prolog,   NULL           },
  {"SLAUNCH_TASK_EPILOG",  OPT_STRING,    &opt.task_epilog,   NULL           },

  {NULL, 0, NULL, NULL}
};


/*
 * _opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend slaunch to process different vars
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

	case OPT_DISTRIB:
	        opt.plane_size = 0;
		opt.distribution = _verify_dist_type(val, &opt.plane_size);
		if (opt.distribution == SLURM_DIST_UNKNOWN) {
			error("\"%s=%s\" -- invalid distribution type. ",
			      e->var, val);
			exit(1);
		}
		break;

	case OPT_CPU_BIND:
		if (_verify_cpu_bind(val, &opt.cpu_bind,
				     &opt.cpu_bind_type))
			exit(1);
		break;

	case OPT_MEM_BIND:
		if (_verify_mem_bind(val, &opt.mem_bind,
				     &opt.mem_bind_type))
			exit(1);
		break;

	case OPT_CORE:
		opt.core_type = core_format_type (val);
		break;
	    
	case OPT_MPI:
		if (mpi_hook_client_init((char *)val) == SLURM_ERROR) {
			fatal("\"%s=%s\" -- invalid MPI type, "
			      "--mpi=list for acceptable types.",
			      e->var, val);
		}
		break;

	default:
		/* do nothing */
		break;
	}
}

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
		exit(1);
	}

	if (result > INT_MAX) {
		error ("Numeric argument %ld to big for %s.", result, what);
		exit(1);
	}

	return (int) result;
}

/*
 *  Get a decimal integer from arg.
 *
 *  Returns the integer on success, exits program on failure.
 * 
 */
static int
_get_int(const char *arg, const char *what, bool positive)
{
	char *p;
	long int result = strtol(arg, &p, 10);

	if ((p == arg) || (!xstring_is_whitespace(p))
		||  (positive && (result <= 0L))) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		exit(1);
	}

	if (result > INT_MAX) {
		error ("Numeric argument %ld to big for %s.", result, what);
	} else if (result < INT_MIN) {
		error ("Numeric argument %ld to small for %s.", result, what);
	}

	return (int) result;
}

void set_options(const int argc, char **argv)
{
	int opt_char, option_index = 0;
	char *tmp;
	static struct option long_options[] = {
		{"cpus-per-task", required_argument, 0, 'c'},
		{"overcommit",    no_argument,       0, 'C'},
		{"slurmd-debug",  required_argument, 0, 'd'},
		{"workdir",       required_argument, 0, 'D'},
		{"slaunch-error", required_argument, 0, 'e'},
		{"task-error",    required_argument, 0, 'E'},
		{"task-layout-file",required_argument,0,'F'},
		{"help",          no_argument,       0, 'h'},
		{"slaunch-input", required_argument, 0, 'i'},
		{"task-input",    required_argument, 0, 'I'},
		{"name",          required_argument, 0, 'J'},
		{"kill-on-bad-exit", no_argument,    0, 'K'},
		{"label",         no_argument,       0, 'l'},
		{"nodelist-byid", required_argument, 0, 'L'},
		{"distribution",  required_argument, 0, 'm'},
		{"tasks",         required_argument, 0, 'n'},
		{"nodes",         required_argument, 0, 'N'},
		{"slaunch-output",required_argument, 0, 'o'},
		{"task-output",   required_argument, 0, 'O'},
		{"quiet",         no_argument,       0, 'q'},
		{"relative",      required_argument, 0, 'r'},
		{"unbuffered",    no_argument,       0, 'u'},
		{"task-layout-byid", required_argument, 0, 'T'},
		{"verbose",       no_argument,       0, 'v'},
		{"version",       no_argument,       0, 'V'},
		{"nodelist-byname", required_argument, 0, 'w'},
		{"wait",          required_argument, 0, 'W'},
		{"task-layout-byname", required_argument, 0, 'Y'},
		{"cpu_bind",         required_argument, 0, LONG_OPT_CPU_BIND},
		{"mem_bind",         required_argument, 0, LONG_OPT_MEM_BIND},
		{"core",             required_argument, 0, LONG_OPT_CORE},
		{"mpi",              required_argument, 0, LONG_OPT_MPI},
		{"jobid",            required_argument, 0, LONG_OPT_JOBID},
		{"uid",              required_argument, 0, LONG_OPT_UID},
		{"gid",              required_argument, 0, LONG_OPT_GID},
		/* debugger-test intentionally undocumented in the man page */
		{"debugger-test",    no_argument,       0, LONG_OPT_DEBUG_TS},
		{"usage",            no_argument,       0, LONG_OPT_USAGE},
		{"network",          required_argument, 0, LONG_OPT_NETWORK},
		{"propagate",        optional_argument, 0, LONG_OPT_PROPAGATE},
		{"prolog",           required_argument, 0, LONG_OPT_PROLOG},
		{"epilog",           required_argument, 0, LONG_OPT_EPILOG},
		{"task-prolog",      required_argument, 0, LONG_OPT_TASK_PROLOG},
		{"task-epilog",      required_argument, 0, LONG_OPT_TASK_EPILOG},
		{"ctrl-comm-ifhn",   required_argument, 0, LONG_OPT_COMM_HOSTNAME},
		{"multi-prog",       no_argument,       0, LONG_OPT_MULTI},
		/* pmi-threads intentionally undocumented in the man page */
		{"pmi-threads",	     required_argument, 0, LONG_OPT_PMI_THREADS},
		{"slaunch-input-filter",required_argument,0, LONG_OPT_LIN_FILTER},
		{"slaunch-output-filter",required_argument,0,LONG_OPT_LOUT_FILTER},
		{"slaunch-error-filter",required_argument,0, LONG_OPT_LERR_FILTER},
		/* task-*-filter are not yet functional, and intentionally
		   undocumented in the man page */
		{"task-input-filter", required_argument, 0, LONG_OPT_RIN_FILTER},
		{"task-output-filter",required_argument, 0, LONG_OPT_ROUT_FILTER},
		{"task-error-filter", required_argument, 0, LONG_OPT_RERR_FILTER},
		{NULL,                0,                 0, 0}
	};
	char *opt_string =
		"+c:Cd:D:e:E:F:hi:I:J:KlL:m:n:N:o:O:qr:T:uvVw:W:Y:";

	struct option *optz = spank_option_table_create (long_options);

	if (!optz) {
		error ("Unable to create option table");
		exit (1);
	}

	opt.progname = xbasename(argv[0]);

	optind = 0;		
	while((opt_char = getopt_long(argc, argv, opt_string,
				      optz, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			fprintf(stderr, "Try \"slaunch --help\" for more "
				"information\n");
			exit(1);
			break;
		case 'c':
			opt.cpus_per_task_set = true;
			opt.cpus_per_task = 
				_get_pos_int(optarg, "cpus-per-task");
			break;
		case 'C':
			opt.overcommit = true;
			break;
		case 'd':
			opt.slurmd_debug = 
				_get_pos_int(optarg, "slurmd-debug");
			break;
		case 'D':
			xfree(opt.cwd);
			opt.cwd = xstrdup(optarg);
			break;
		case 'e':
			xfree(opt.local_efname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.local_efname = xstrdup("/dev/null");
			else
				opt.local_efname = xstrdup(optarg);
			break;
		case 'F':
			xfree(opt.task_layout);
			tmp = slurm_read_hostfile(optarg, 0);
			if (tmp != NULL) {
				opt.task_layout = xstrdup(tmp);
				free(tmp);
				opt.task_layout_file_set = true;
			} else {
				error("\"%s\" is not a valid task layout file");
				exit(1);
			}			
			break;
		case 'E':
			xfree(opt.remote_efname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.remote_efname = xstrdup("/dev/null");
			else
				opt.remote_efname = xstrdup(optarg);
			break;
		case 'h':
			_help();
			exit(0);
		case 'i':
			xfree(opt.local_ifname);
			opt.local_ifname = xstrdup(optarg);
			break;
		case 'I':
			xfree(opt.remote_ifname);
			opt.remote_ifname = xstrdup(optarg);
			break;
		case 'J':
			xfree(opt.job_name);
			opt.job_name = xstrdup(optarg);
			break;
		case 'K':
			opt.kill_bad_exit = true;
			break;
		case 'l':
			opt.labelio = true;
			break;
		case 'L':
			xfree(opt.nodelist_byid);
			opt.nodelist_byid = xstrdup(optarg);
			break;
		case 'm':
			opt.plane_size = 0;
			opt.distribution = _verify_dist_type(optarg, 
							     &opt.plane_size);
			if (opt.distribution == SLURM_DIST_UNKNOWN) {
				error("distribution type `%s' " 
				      "is not recognized", optarg);
				exit(1);
			}
			break;
		case 'n':
			opt.num_tasks_set = true;
			opt.num_tasks = _get_pos_int(optarg, "number of tasks");
			break;
		case 'N':
			opt.num_nodes_set = true;
			opt.num_nodes = _get_pos_int(optarg, "number of nodes");
			break;
		case 'o':
			xfree(opt.local_ofname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.local_ofname = xstrdup("/dev/null");
			else
				opt.local_ofname = xstrdup(optarg);
			break;
		case 'O':
			xfree(opt.remote_ofname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.remote_ofname = xstrdup("/dev/null");
			else
				opt.remote_ofname = xstrdup(optarg);
			break;
		case 'q':
			opt.quiet++;
			break;
		case 'r':
			opt.relative_set = true;
			opt.relative = _get_int(optarg, "relative start node", 
				false);
			break;
		case 'T':
			xfree(opt.task_layout);
			opt.task_layout_byid = xstrdup(optarg);
			opt.task_layout_byid_set = true;
			break;
		case 'u':
			opt.unbuffered = true;
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
			     "\tIt should also include all the midplanes you\n"
			     "\twant to use, partial lists may not\n"
			     "\twork correctly.\n"
			     "\tPlease consult smap before using this option\n"
			     "\tor your job may be stuck with no way to run.");
#endif
			break;
		case 'W':
			opt.max_wait = _get_pos_int(optarg, "wait");
			break;
		case 'Y':
			xfree(opt.task_layout);
			opt.task_layout = xstrdup(optarg);
			opt.task_layout_byname_set = true;
			break;
                case LONG_OPT_CPU_BIND:
			if (_verify_cpu_bind(optarg, &opt.cpu_bind,
					     &opt.cpu_bind_type))
				exit(1);
			break;
		case LONG_OPT_MEM_BIND:
			if (_verify_mem_bind(optarg, &opt.mem_bind,
					     &opt.mem_bind_type))
				exit(1);
			break;
		case LONG_OPT_CORE:
			opt.core_type = core_format_type (optarg);
			if (opt.core_type == CORE_INVALID)
				error ("--core=\"%s\" Invalid -- ignoring.\n",
				       optarg);
			break;
		case LONG_OPT_MPI:
			if (mpi_hook_client_init((char *)optarg)
			    == SLURM_ERROR) {
				fatal("\"--mpi=%s\" -- long invalid MPI type, "
				      "--mpi=list for acceptable types.",
				      optarg);
			}
			break;
		case LONG_OPT_JOBID:
			opt.jobid = _get_pos_int(optarg, "jobid");
			opt.jobid_set = true;
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
		case LONG_OPT_DEBUG_TS:
			/* simulate running under a parallel debugger */
			opt.debugger_test    = true;
			MPIR_being_debugged  = 1;
			break;
		case LONG_OPT_USAGE:
			_usage();
			exit(0);
		case LONG_OPT_NETWORK:
			xfree(opt.network);
			opt.network = xstrdup(optarg);
#ifdef HAVE_AIX
			setenv("SLURM_NETWORK", opt.network, 1);
#endif
			break;
		case LONG_OPT_PROPAGATE:
			xfree(opt.propagate);
			if (optarg) opt.propagate = xstrdup(optarg);
			else	    opt.propagate = xstrdup("ALL");
			break;
		case LONG_OPT_PROLOG:
			xfree(opt.prolog);
			opt.prolog = xstrdup(optarg);
			break;
		case LONG_OPT_EPILOG:
			xfree(opt.epilog);
			opt.epilog = xstrdup(optarg);
			break;
		case LONG_OPT_TASK_PROLOG:
			xfree(opt.task_prolog);
			opt.task_prolog = xstrdup(optarg);
			break;
		case LONG_OPT_TASK_EPILOG:
			xfree(opt.task_epilog);
			opt.task_epilog = xstrdup(optarg);
			break;
		case LONG_OPT_COMM_HOSTNAME:
			xfree(opt.comm_hostname);
			opt.comm_hostname = xstrdup(optarg);
			break;
		case LONG_OPT_MULTI:
			opt.multi_prog = true;
			break;
		case LONG_OPT_PMI_THREADS: /* undocumented option */
			pmi_server_max_threads(_get_pos_int(optarg,
							    "pmi-threads"));
			break;
		case LONG_OPT_LIN_FILTER:
			if (strcmp(optarg, "-") != 0) {
				opt.local_input_filter =
					_get_pos_int(optarg,
						     "slaunch-input-filter");
			}
			opt.local_input_filter_set = true;
			break;
		case LONG_OPT_LOUT_FILTER:
			if (strcmp(optarg, "-") != 0) {
				opt.local_output_filter =
					_get_pos_int(optarg,
						     "slaunch-output-filter");
			}
			opt.local_output_filter_set = true;
			break;
		case LONG_OPT_LERR_FILTER:
			if (strcmp(optarg, "-") != 0) {
				opt.local_error_filter =
					_get_pos_int(optarg,
						     "slaunch-error-filter");
			}
			opt.local_error_filter_set = true;
			break;
		case LONG_OPT_RIN_FILTER:
			opt.remote_input_filter =
				_get_pos_int(optarg, "task-input-filter");
			error("task-input-filter not yet implemented");
			break;
		case LONG_OPT_ROUT_FILTER:
			opt.remote_output_filter =
				_get_pos_int(optarg, "task-output-filter");
			error("task-output-filter not yet implemented");
			break;
		case LONG_OPT_RERR_FILTER:
			opt.remote_error_filter =
				_get_pos_int(optarg, "task-error-filter");
			error("task-error-filter not yet implemented");
			break;
		default:
			if (spank_process_option (opt_char, optarg) < 0) {
				exit (1);
			}
		}
	}

	spank_option_table_destroy (optz);
}

/*
 * Use the supplied compiled regular expression "re" to convert a string
 * into first and last numbers in the range.
 *
 * If there is only a single number in the "token" string, both
 * "first" and "last" will be set to the same value.
 *
 * Returns 1 on success, 0 on failure
 */
static int _get_range(regex_t *re, char *token, int *first, int *last,
		      int num_nodes)
{
	size_t nmatch = 8;
	regmatch_t pmatch[8];
	long f, l;
	bool first_set = false;
	char *ptr;
	
	*first = *last = 0;
	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);
	if (regexec(re, token, nmatch, pmatch, 0) == REG_NOMATCH) {
		error("\"%s\" is not a valid node index range", token);
		return 0;
	}

	/* convert the second, possibly only, number */
	ptr = (char *)(xstrndup(token + pmatch[3].rm_so,
				pmatch[3].rm_eo - pmatch[3].rm_so));
	l = strtol(ptr, NULL, 10);
	xfree(ptr);
	if ((l >= 0 && l >= num_nodes)
	    || (l < 0 && l < -num_nodes)) {
		error("\"%ld\" is beyond the range of the"
		      " %d available nodes", l, num_nodes);
		return 0;
	}
	*last = (int)l;
	*first = (int)l;

	/* convert the first number, if it exists */
	if (pmatch[2].rm_so != -1) {
		first_set = true;
		ptr = (char *)(xstrndup(token + pmatch[2].rm_so,
					pmatch[2].rm_eo - pmatch[2].rm_so));
		f = strtol(ptr, NULL, 10);
		xfree(ptr);
		if ((f >= 0 && f >= num_nodes)
		    || (f < 0 && f < -num_nodes)) {
			error("\"%ld\" is beyond the range of the"
			      " %d available nodes", f, num_nodes);
			return 0;
		}
		*first = (int)f;
	}

	return 1;
}

/*
 * Convert a node index string into a nodelist string.
 *
 * A node index string is a string of single numbers and/or ranges seperated
 * by commas.  For instance:  2,6,-3,8,-3-2,16,2--4,7-9,0
 *
 * If both numbers in a range are of the same sign (both positive, or both
 * negative), then the range counts directly from the first number to the
 * second number; it will not wrap around the "end" of the node list.
 *
 * If the numbers in a range differ in sign, the range wraps around the
 * end of the list of nodes.
 *
 * Examples: Given a node allocation of foo[1-16]:
 *
 *	-2-3  (negative 2 to positive 3) becomes foo[15-16,1-4]
 *	3--2  (positive 3 to negative 2) becomes foo[4,3,2,1,16,15]
 *	-3--2 becomes foo[14-15]
 *	-2--3 becomes foo[15,14]
 *	2-3   becomes foo[3-4]
 *	3-2   becomes foo[4,3]
 */
static char *_node_indices_to_nodelist(const char *indices_list,
				       resource_allocation_response_msg_t *alloc_info)
{
	char *list;
	int list_len;
	char *start, *end;
	hostlist_t node_l, alloc_l;
	regex_t range_re;
	char *range_re_pattern =
		"^[[:space:]]*"
		"((-?[[:digit:]]+)[[:space:]]*-)?" /* optional start */
		"[[:space:]]*"
		"(-?[[:digit:]]+)[[:space:]]*$";
	char *nodelist = NULL;
	int i, idx;

	/* intialize the regular expression */
	if (regcomp(&range_re, range_re_pattern, REG_EXTENDED) != 0) {
		error("Node index range regex compilation failed\n");
		return NULL;
	}

	/* Now break the string up into tokens between commas,
	   feed each token into the regular expression, and make
	   certain that the range numbers are valid. */
	node_l = hostlist_create(NULL);
	alloc_l = hostlist_create(alloc_info->node_list);
	list = xstrdup(indices_list);
	start = (char *)list;
	list_len = strlen(list);
	while (start != NULL && start < (list + list_len)) {
		int first = 0;
		int last = 0;

		/* Find the next index range in the list */
		end = strchr(start,',');
		if (end == NULL) {
			end = list + list_len;
		}
		*end = '\0';

		/* Use the regexp to get the range numbers */
		if (!_get_range(&range_re, start, &first, &last,
				hostlist_count(alloc_l))) {
			goto cleanup;
		}
		
		/* Now find all nodes in this range, and add them to node_l */
		if (first <= last) {
			char *node;
			for (i = first; i <= last; i++) {
				if (i < 0)
					idx = i + hostlist_count(alloc_l);
				else
					idx = i;
				node = hostlist_nth(alloc_l, idx);
				hostlist_push(node_l, node);
				free(node);
			}
		} else { /* first > last */
			char *node;
			for (i = first; i >= last; i--) {
				if (i < 0)
					idx = i + hostlist_count(alloc_l);
				else
					idx = i;
				node = hostlist_nth(alloc_l, idx);
				hostlist_push(node_l, node);
				free(node);
			}
		}
		start = end+1;
	}

	i = 2048;
	nodelist = NULL;
	do {
		i *= 2;
		xrealloc(nodelist, i);
	} while (hostlist_ranged_string(node_l, i, nodelist) == -1);

cleanup:
	xfree(list);
	hostlist_destroy(alloc_l);
	hostlist_destroy(node_l);
	regfree(&range_re);

	return nodelist;
}


/* Load the multi_prog config file into argv, pass the  entire file contents 
 * in order to avoid having to read the file on every node. We could parse
 * the infomration here too for loading the MPIR records for TotalView */
static void _load_multi(int *argc, char **argv)
{
	int config_fd, data_read = 0, i;
	struct stat stat_buf;
	char *data_buf;

	if ((config_fd = open(argv[0], O_RDONLY)) == -1) {
		error("Could not open multi_prog config file %s",
		      argv[0]);
		exit(1);
	}
	if (fstat(config_fd, &stat_buf) == -1) {
		error("Could not stat multi_prog config file %s",
		      argv[0]);
		exit(1);
	}
	if (stat_buf.st_size > 60000) {
		error("Multi_prog config file %s is too large",
		      argv[0]);
		exit(1);
	}
	data_buf = xmalloc(stat_buf.st_size);
	while ((i = read(config_fd, &data_buf[data_read], stat_buf.st_size 
			 - data_read)) != 0) {
		if (i < 0) {
			error("Error reading multi_prog config file %s", 
			      argv[0]);
			exit(1);
		} else
			data_read += i;
	}
	close(config_fd);
	for (i=1; i<*argc; i++)
		xfree(argv[i]);
	argv[1] = data_buf;
	*argc = 2;
}

/*
 * _opt_args() : set options via commandline args and popt
 */
static void _opt_args(int argc, char **argv)
{
	int i;
	char **rest = NULL;

	set_options(argc, argv);

#ifdef HAVE_AIX
	if (opt.network == NULL) {
		opt.network = "us,sn_all,bulk_xfer";
		setenv("SLURM_NETWORK", opt.network, 1);
	}
#endif

	opt.argc = 0;
	if (optind < argc) {
		rest = argv + optind;
		while (rest[opt.argc] != NULL)
			opt.argc++;
	}
	opt.argv = (char **) xmalloc((opt.argc + 1) * sizeof(char *));
	for (i = 0; i < opt.argc; i++)
		opt.argv[i] = xstrdup(rest[i]);
	opt.argv[i] = NULL;	/* End of argv's (for possible execv) */

	if (opt.multi_prog) {
		if (opt.argc < 1) {
			error("configuration file not specified");
			exit(1);
		}
		_load_multi(&opt.argc, opt.argv);

	}
	else if (opt.argc > 0) {
		char *fullpath;

		if ((fullpath = _search_path(opt.argv[0], R_OK|X_OK))) {
			xfree(opt.argv[0]);
			opt.argv[0] = fullpath;
		} 
	}

	if (!_opt_verify())
		exit(1);
}

static bool
_allocation_lookup_env(resource_allocation_response_msg_t **alloc_info)
{
	char *ptr, *val;
	resource_allocation_response_msg_t *alloc;
	long l;

	alloc = (resource_allocation_response_msg_t *)xmalloc(
		sizeof(resource_allocation_response_msg_t));

	/* get SLURM_JOB_ID */
	val = getenv("SLURM_JOB_ID");
	if (val == NULL)
		goto fail1;
	l = strtol(val, &ptr, 10);
	if (ptr == val || !xstring_is_whitespace(ptr) || l < 0)
		goto fail1;
	alloc->job_id = (uint32_t)l;

	/* get SLURM_JOB_NUM_NODES */
	val = getenv("SLURM_JOB_NUM_NODES");
	if (val == NULL)
		goto fail1;
	l = strtol(val, &ptr, 10);
	if (ptr == val || !xstring_is_whitespace(ptr) || l < 1)
		goto fail1;
	alloc->node_cnt = (uint16_t)l;

	/* get SLURM_JOB_NODELIST */
	val = getenv("SLURM_JOB_NODELIST");
	if (val == NULL)
		goto fail1;
	alloc->node_list = xstrdup(val);

	/* get SLURM_JOB_CPUS_PER_NODE */
	val = getenv("SLURM_JOB_CPUS_PER_NODE");
	if (val == NULL)
		goto fail2;
	if (!_set_cpus_per_node(val, alloc))
		goto fail2;

	*alloc_info = alloc;
	return true;

fail2:
	xfree(alloc->node_list);
fail1:
	xfree(alloc);
	*alloc_info = NULL;
	return false;
}

static bool
_set_allocation_info(resource_allocation_response_msg_t **alloc_info)
{
	bool env_flag;

	/* First, try to set the allocation info from the environment */
	env_flag = _allocation_lookup_env(alloc_info);

	/* If that fails, we need to try to get the allocation info
	 * from the slurmctld.  We also need to talk to the slurmctld if
	 * opt.job_id is set and does not match the information from the
	 * environment variables.
	 */
	if (!env_flag || (env_flag
			  && opt.jobid_set
			  && opt.jobid != (*alloc_info)->job_id)) {
		verbose("Need to look up allocation info with the controller");
		if (slurm_allocation_lookup_lite(opt.jobid, alloc_info) < 0) {
			error("Unable to look up job ID %u: %m", opt.jobid);
			return false;
		}
	} else if (!env_flag && !opt.jobid_set) {
		error("A job ID MUST be specified on the command line,");
		error("or through the SLAUNCH_JOBID environment variable.");
		return false;
	}

	return true;
}


/* 
 * _opt_verify : perform some post option processing verification
 *
 */
static bool _opt_verify(void)
{
	bool verified = true;
	hostlist_t task_l = NULL;
	hostlist_t node_l = NULL;
	resource_allocation_response_msg_t *alloc_info;

	if (!_set_allocation_info(&alloc_info)) {
		/* error messages printed under _set_allocation_info */
		exit(1);
	}

	/*
	 * Now set default options based on allocation info.
	 */
	if (!opt.jobid_set)
		opt.jobid = alloc_info->job_id;
	if (!opt.num_nodes_set)
		opt.num_nodes = alloc_info->node_cnt;

	if (opt.task_layout_byid_set && opt.task_layout == NULL) {
		opt.task_layout = _node_indices_to_nodelist(
			opt.task_layout_byid, alloc_info);
		if (opt.task_layout == NULL)
			verified = false;
	}
	if (opt.nodelist_byid != NULL && opt.nodelist == NULL) {
		hostlist_t hl;
		char *nodenames;

		nodenames = _node_indices_to_nodelist(opt.nodelist_byid,
						      alloc_info);
		if (nodenames == NULL) {
			verified = false;
		} else {
			hl = hostlist_create(nodenames);
			hostlist_uniq(hl);
			/* assumes that the sorted unique hostlist must be a
			   shorter string than unsorted (or equal lenght) */
			hostlist_ranged_string(hl, strlen(nodenames)+1,
					       nodenames);
			opt.nodelist = nodenames;
		}
	}

	/*
	 * Now, all the rest of the checks and setup.
	 */
	if (opt.task_layout_byid_set && opt.task_layout_file_set) {
		error("-T/--task-layout-byid and -F/--task-layout-file"
		      " are incompatible.");
		verified = false;
	}
	if (opt.task_layout_byname_set && opt.task_layout_file_set) {
		error("-Y/--task-layout-byname and -F/--task-layout-file"
		      " are incompatible.");
		verified = false;
	}
	if (opt.task_layout_byname_set && opt.task_layout_byid_set) {
		error("-Y/--task-layout-byname and -T/--task-layout-byid"
		      " are incompatible.");
		verified = false;
	}

	if (opt.nodelist && (opt.task_layout_byid_set
			     || opt.task_layout_byname_set
			     || opt.task_layout_file_set)) {
		error("-w/--nodelist is incompatible with task layout"
		      " options.");
		verified = false;
	}
	if (opt.nodelist && opt.task_layout_file_set) {
		error("Only one of -w/--nodelist or -F/--task-layout-file"
		      " may be used.");
		verified = false;
	}
	if (opt.num_nodes_set && (opt.task_layout_byid_set
				  || opt.task_layout_byname_set
				  || opt.task_layout_file_set)) {
		error("-N/--node is incompatible with task layout options.");
		verified = false;
	}

	if (opt.task_layout != NULL) {
		task_l = hostlist_create(opt.task_layout);
		if (opt.num_tasks_set) {
			if (opt.num_tasks < hostlist_count(task_l)) {
				/* shrink the hostlist */
				int i, shrink;
				char buf[8192];
				shrink = hostlist_count(task_l) - opt.num_tasks;
				for (i = 0; i < shrink; i++)
					free(hostlist_pop(task_l));
				xfree(opt.task_layout);
				hostlist_ranged_string(task_l, 8192, buf);
				opt.task_layout = xstrdup(buf);
			} else if (opt.num_tasks > hostlist_count(task_l)) {
				error("Asked for more tasks (%d) than listed"
				      " in the task layout (%d)",
				      opt.num_tasks, hostlist_count(task_l));
				verified = false;
			} else {
				/* they are equal, no problemo! */
			}
		} else {
			opt.num_tasks = hostlist_count(task_l);
			opt.num_tasks_set = true;
		}
		node_l = hostlist_copy(task_l);
		hostlist_uniq(node_l);
		opt.num_nodes = hostlist_count(node_l);
		opt.num_nodes_set = true;
		/* task_layout parameters implicitly trigger
		   arbitrary task layout mode */
		opt.distribution = SLURM_DIST_ARBITRARY;

	} else if (opt.nodelist != NULL) {
		hostlist_t tmp;
		tmp = hostlist_create(opt.nodelist);
		node_l = hostlist_copy(tmp);
		hostlist_uniq(node_l);
		if (hostlist_count(node_l) != hostlist_count(tmp)) {
			error("Node names may only appear once in the"
			      " nodelist (-w/--nodelist)");
			verified = false;
		}
		hostlist_destroy(tmp);

		if (opt.num_nodes_set
		    && (opt.num_nodes != hostlist_count(node_l))) {
			error("You asked for %d nodes (-N/--nodes), but there"
			      " are %d nodes in the nodelist (-w/--nodelist)",
			      opt.num_nodes, hostlist_count(node_l));
			verified = false;
		} else {
			opt.num_nodes = hostlist_count(node_l);
			opt.num_nodes_set = true;
		}
	}

	if (opt.overcommit && opt.cpus_per_task_set) {
		error("--overcommit/-C and --cpus-per-task/-c are incompatible");
		verified = false;
	}

	if (!opt.num_nodes_set && opt.num_tasks_set
	    && opt.num_tasks < opt.num_nodes)
		opt.num_nodes = opt.num_tasks;

	if (!opt.num_tasks_set) {
		if (opt.nodelist != NULL) {
			opt.num_tasks = hostlist_count(node_l);
		} else {
			opt.num_tasks = opt.num_nodes;
		}
	}

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-q)");
		verified = false;
	}

	if (opt.relative_set) {
		if (opt.nodelist != NULL) {
			error("-r/--relative not allowed with"
			      " -w/--nodelist.");
			verified = false;
		}

		if (opt.task_layout_byid_set) {
			error("-r/--relative not allowed with"
			      " -T/--task-layout-byid");
			verified = false;
		}

		if (opt.task_layout_byname_set) {
			error("-r/--relative not allowed with"
			      " -Y/--task-layout-byname");
			verified = false;
		}

		if (opt.task_layout_file_set) {
			error("-r/--relative not allowed with"
			      " -F/--task-layout-file");
			verified = false;
		}

		/* convert a negative relative number into a positive number
		   that the slurmctld will accept */
		if (opt.relative < 0 && opt.relative >= -(alloc_info->node_cnt))
			opt.relative += alloc_info->node_cnt;
	}
	if ((opt.job_name == NULL) && (opt.argc > 0))
		opt.job_name = _base_name(opt.argv[0]);

	if (opt.argc == 0) {
		error("must supply remote command");
		verified = false;
	}


	/* check for realistic arguments */
	if (opt.num_tasks <= 0) {
		error("%s: invalid number of tasks (-n %d)",
		      opt.progname, opt.num_tasks);
		verified = false;
	}

	if (opt.cpus_per_task <= 0) {
		error("%s: invalid number of cpus per task (-c %d)\n",
		      opt.progname, opt.cpus_per_task);
		verified = false;
	}

	if (opt.num_nodes <= 0) {
		error("%s: invalid number of nodes (-N %d)\n",
		      opt.progname, opt.num_nodes);
		verified = false;
	}

	core_format_enable (opt.core_type);

	if (opt.labelio && opt.unbuffered) {
		error("Do not specify both -l (--label) and " 
		      "-u (--unbuffered)");
		exit(1);
	}

	if ((opt.euid != (uid_t) -1) && (opt.euid != opt.uid)) 
		opt.uid = opt.euid;

	if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid)) 
		opt.gid = opt.egid;

        if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid))
	        opt.gid = opt.egid;

	if (opt.propagate && parse_rlimits( opt.propagate, PROPAGATE_RLIMITS)) {
		error( "--propagate=%s is not valid.", opt.propagate );
		verified = false;
	}

	/* FIXME - figure out the proper way to free alloc_info */
	hostlist_destroy(task_l);
	hostlist_destroy(node_l);
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
	List l;
	char *path = xstrdup(getenv("PATH"));
	char *c, *lc;

	if (path == NULL) {
		error("No PATH environment variable (or empty PATH)");
		return NULL;
	}

	l = list_create(_freeF);
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
_search_path(char *cmd, int access_mode)
{
	List         l        = _create_path_list();
	ListIterator i        = NULL;
	char *path, *fullpath = NULL;

	if (l == NULL)
		return NULL;

	if ((cmd[0] == '.' || cmd[0] == '/')
	    && (access(cmd, access_mode) == 0 ) ) {
		if (cmd[0] == '.')
			xstrfmtcat(fullpath, "%s/", opt.cwd);
		xstrcat(fullpath, cmd);
		goto done;
	}

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


static char *
print_remote_command()
{
	int i;
	char *buf = NULL;
	char *space;

	for (i = 0; i < opt.argc; i++) {
		if (i == opt.argc-1) {
			space = "";
		} else {
			space = " ";
		}

		xstrfmtcat(buf, "\"%s\"%s", opt.argv[i], space);
	}

	return buf;
}

#define tf_(b) (b == true) ? "true" : "false"

static void _opt_list()
{
	char *str;

	info("defined options for program \"%s\"", opt.progname);
	info("--------------- ---------------------");

	info("user              : \"%s\"", opt.user);
	info("uid               : %ld", (long) opt.uid);
	info("gid               : %ld", (long) opt.gid);
	info("cwd               : %s", opt.cwd);
	info("num_tasks         : %d %s", opt.num_tasks,
	     opt.num_tasks_set ? "(set)" : "(default)");
	info("cpus_per_task     : %d %s", opt.cpus_per_task,
	     opt.cpus_per_task_set ? "(set)" : "(default)");
	info("nodes             : %d %s",
	     opt.num_nodes, opt.num_nodes_set ? "(set)" : "(default)");
	info("jobid             : %u %s", opt.jobid, 
	     opt.jobid_set ? "(set)" : "(default)");
	info("job name          : \"%s\"", opt.job_name);
	info("distribution      : %s",
	     format_task_dist_states(opt.distribution));
	info("cpu_bind          : %s", 
	     opt.cpu_bind == NULL ? "default" : opt.cpu_bind);
	info("mem_bind          : %s",
	     opt.mem_bind == NULL ? "default" : opt.mem_bind);
	info("core format       : %s", core_format_name (opt.core_type));
	info("verbose           : %d", opt.verbose);
	info("slurmd_debug      : %d", opt.slurmd_debug);
	info("label output      : %s", tf_(opt.labelio));
	info("unbuffered IO     : %s", tf_(opt.unbuffered));
	info("overcommit        : %s", tf_(opt.overcommit));
	info("wait              : %d", opt.max_wait);
	info("required nodes    : %s", opt.nodelist);
	info("network           : %s", opt.network);
	info("propagate         : %s",
	     opt.propagate == NULL ? "NONE" : opt.propagate);
	info("prolog            : %s", opt.prolog);
	info("epilog            : %s", opt.epilog);
	info("task_prolog       : %s", opt.task_prolog);
	info("task_epilog       : %s", opt.task_epilog);
	info("comm_hostname     : %s", opt.comm_hostname);
	info("multi_prog        : %s", opt.multi_prog ? "yes" : "no");
	info("plane_size        : %u", opt.plane_size);
	str = print_remote_command();
	info("remote command : %s", str);
	xfree(str);

}

static void _usage(void)
{
 	printf(
"Usage: slaunch [-N nnodes] [-n ntasks] [-i in] [-o out] [-e err]\n"
"               [-c ncpus] [-r n] [-t minutes]\n"
"               [-D path] [--overcommit] [--no-kill]\n"
"               [--label] [--unbuffered] [-m dist] [-J jobname]\n"
"               [--jobid=id] [--batch] [--verbose] [--slurmd_debug=#]\n"
"               [--core=type] [-W sec]\n"
"               [--mpi=type]\n"
"               [--kill-on-bad-exit] [--propagate[=rlimits] ]\n"
"               [--cpu_bind=...] [--mem_bind=...]\n"
"               [--prolog=fname] [--epilog=fname]\n"
"               [--task-prolog=fname] [--task-epilog=fname]\n"
"               [--comm-hostname=<hostname|address>] [--multi-prog]\n"
"               [-w hosts...] [-L hostids...] executable [args...]\n");
}

static void _help(void)
{
	slurm_ctl_conf_t *conf;

        printf (
"Usage: slaunch [OPTIONS...] executable [args...]\n"
"\n"
"Parallel run options:\n"
"  -n, --ntasks=ntasks         number of tasks to run\n"
"  -N, --nodes=N               number of nodes on which to run\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"  -i, --slaunch-input=file    slaunch will read stdin from \"file\"\n"
"  -o, --slaunch-output=file   slaunch will write stdout to \"file\"\n"
"  -e, --slaunch-error=file    slaunch will write stderr to \"file\"\n"
"      --slaunch-input-filter=taskid  send stdin to only the specified task\n"
"      --slaunch-output-filter=taskid only print stdout from the specified task\n"
"      --slaunch-error-filter=taskid  only print stderr from the specified task\n"
"  -I, --task-input=file       connect task stdin to \"file\"\n"
"  -O, --task-output=file      connect task stdout to \"file\"\n"
"  -E, --task-error=file       connect task stderr to \"file\"\n"
"  -r, --relative=n            run job step relative to node n of allocation\n"
"  -t, --time=minutes          time limit\n"
"  -D, --workdir=path          the working directory for the launched tasks\n"
"  -C, --overcommit            overcommit resources\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -K, --kill-on-bad-exit      kill the job if any task terminates with a\n"
"                              non-zero exit code\n"
"  -l, --label                 prepend task number to lines of stdout/err\n"
"  -u, --unbuffered            do not line-buffer stdout/err\n"
"  -m, --distribution=type     distribution method for processes to nodes\n"
"                              (type = block|cyclic|hostfile)\n"
"  -J, --job-name=jobname      name of job\n"
"      --jobid=id              run under already allocated job\n"
"  -W, --wait=sec              seconds to wait after first task exits\n"
"                              before killing job\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -q, --quiet                 quiet mode (suppress informational messages)\n"
"  -d, --slurmd-debug=level    slurmd debug level\n"
"      --core=type             change default corefile format type\n"
"                              (type=\"list\" to list of valid formats)\n"
"      --propagate[=rlimits]   propagate all [or specific list of] rlimits\n"
"      --mpi=type              specifies version of MPI to use\n"
"      --prolog=program        run \"program\" before launching job step\n"
"      --epilog=program        run \"program\" after launching job step\n"
"      --task-prolog=program   run \"program\" before launching task\n"
"      --task-epilog=program   run \"program\" after launching task\n"
"      --comm-hostname=hostname hostname for PMI communications with slaunch\n"
"      --multi-prog            if set the program name specified is the\n"
"                              configuration specificaiton for multiple programs\n"
"  -w, --nodelist-byname=hosts...   request a specific list of hosts\n"
"  -L, --nodelist-byid=hosts...     request a specific list of hosts\n");
	conf = slurm_conf_lock();
	if (conf->task_plugin != NULL
	    && strcasecmp(conf->task_plugin, "task/affinity") == 0) {
		printf(
			"      --cpu_bind=             Bind tasks to CPUs\n"
			"                              (see \"--cpu_bind=help\" for options)\n"
			"      --mem_bind=             Bind memory to locality domains (ldom)\n"
			"                              (see \"--mem_bind=help\" for options)\n"
			);
	}
	slurm_conf_unlock();
	spank_print_options (stdout, 6, 30);
	printf("\n");

        printf(
#ifdef HAVE_AIX				/* AIX/Federation specific options */
		"AIX related options:\n"
		"  --network=type              communication protocol to be used\n"
		"\n"
#endif

		"Help options:\n"
		"  -h, --help                  show this help message\n"
		"      --usage                 display brief usage message\n"
		"\n"
		"Other options:\n"
		"  -V, --version               output version information and exit\n"
		"\n"
		);

}
