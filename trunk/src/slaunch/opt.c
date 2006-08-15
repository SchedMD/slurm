/*****************************************************************************\
 *  opt.c - options processing for slaunch
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

#include "src/slaunch/attach.h"
#include "src/common/mpi.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_DEBUG       0x03
#define OPT_DISTRIB     0x04
#define OPT_OVERCOMMIT  0x06
#define OPT_CORE        0x07
#define OPT_CONN_TYPE	0x08
#define OPT_NO_ROTATE	0x0a
#define OPT_GEOMETRY	0x0b
#define OPT_MPI         0x0c
#define OPT_CPU_BIND    0x0d
#define OPT_MEM_BIND    0x0e
#define OPT_MULTI       0x0f
#define OPT_CPUS_PER_NODE 0x10

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_HELP        0x100
#define LONG_OPT_USAGE       0x101
#define LONG_OPT_XTO         0x102
#define LONG_OPT_LAUNCH      0x103
#define LONG_OPT_TIMEO       0x104
#define LONG_OPT_JOBID       0x105
#define LONG_OPT_TMP         0x106
#define LONG_OPT_MEM         0x107
#define LONG_OPT_MINCPU      0x108
#define LONG_OPT_UID         0x10a
#define LONG_OPT_GID         0x10b
#define LONG_OPT_MPI         0x10c
#define LONG_OPT_CORE	     0x10e
#define LONG_OPT_DEBUG_TS    0x110
#define LONG_OPT_CONNTYPE    0x111
#define LONG_OPT_NETWORK     0x114
#define LONG_OPT_EXCLUSIVE   0x115
#define LONG_OPT_PROPAGATE   0x116
#define LONG_OPT_PROLOG      0x117
#define LONG_OPT_EPILOG      0x118
#define LONG_OPT_TASK_PROLOG 0x11c
#define LONG_OPT_TASK_EPILOG 0x11d
#define LONG_OPT_CPU_BIND    0x11f
#define LONG_OPT_MEM_BIND    0x120
#define LONG_OPT_CTRL_COMM_IFHN 0x121
#define LONG_OPT_MULTI       0x122

typedef struct resource_allocation_response_msg_flags {
	bool job_id;
	bool node_list;
	bool cpu_info;
	bool node_cnt;
} resource_allocation_response_msg_flags_t;

resource_allocation_response_msg_t alloc_info;
resource_allocation_response_msg_t *alloc_info_ptr = &alloc_info;
resource_allocation_response_msg_flags_t alloc_info_set;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

/* return command name from its full path name */
static char * _base_name(char* command);

static List  _create_path_list(void);

/* Get a decimal integer from arg */
static int  _get_int(const char *arg, const char *what);

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
static char *_search_path(char *, bool, int);

static long  _to_bytes(const char *arg);

static bool  _under_parallel_debugger(void);

static void  _usage(void);
static bool  _valid_node_list(char **node_list_pptr);
static enum  task_dist_states _verify_dist_type(const char *arg);
static int   _verify_cpu_bind(const char *arg, char **cpu_bind,
					cpu_bind_type_t *cpu_bind_type);
static int   _verify_geometry(const char *arg, uint16_t *geometry);
static int   _verify_mem_bind(const char *arg, char **mem_bind,
                                        mem_bind_type_t *mem_bind_type);
static int   _verify_conn_type(const char *arg);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char *argv[])
{
	if (spank_init (NULL) < 0)
		return (-1);

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
 * returns the task_dist_states, or -1 if unrecognized dist method
 */
static enum task_dist_states _verify_dist_type(const char *arg)
{
	int len = strlen(arg);
	enum task_dist_states result = -1;

	if (strncasecmp(arg, "cyclic", len) == 0)
		result = SLURM_DIST_CYCLIC;
	else if (strncasecmp(arg, "block", len) == 0)
		result = SLURM_DIST_BLOCK;
	else if (strncasecmp(arg, "arbitrary", len) == 0)
		result = SLURM_DIST_ARBITRARY;

	return result;
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

/*
 * verify cpu_bind arguments
 * returns -1 on error, 0 otherwise
 */
static int _verify_cpu_bind(const char *arg, char **cpu_bind, 
		cpu_bind_type_t *cpu_bind_type)
{
    	char *buf = xstrdup(arg);
	char *pos = buf;
	/* we support different launch policy names
	 * we also allow a verbose setting to be specified
	 *     --cpu_bind=v
	 *     --cpu_bind=rank,v
	 *     --cpu_bind=rank
	 *     --cpu_bind={MAP_CPU|MAP_MASK}:0,1,2,3,4
	 */
	if (*pos) {
		/* parse --cpu_bind command line arguments */
		bool fl_cpubind_verbose = 0;
	        char *cmd_line_affinity = NULL;
	        char *cmd_line_mapping  = NULL;
		char *mappos = strchr(pos,':');
		if (!mappos) {
		    	mappos = strchr(pos,'=');
		}
		if (strncasecmp(pos, "quiet", 5) == 0) {
			fl_cpubind_verbose=0;
			pos+=5;
		} else if (*pos=='q' || *pos=='Q') {
			fl_cpubind_verbose=0;
			pos++;
		}
		if (strncasecmp(pos, "verbose", 7) == 0) {
			fl_cpubind_verbose=1;
			pos+=7;
		} else if (*pos=='v' || *pos=='V') {
			fl_cpubind_verbose=1;
			pos++;
		}
		if (*pos==',') {
			pos++;
		}
		if (*pos) {
			char *vpos=NULL;
			cmd_line_affinity = pos;
			if (((vpos=strstr(pos,",q")) !=0  ) ||
			    ((vpos=strstr(pos,",Q")) !=0  )) {
				*vpos='\0';
				fl_cpubind_verbose=0;
			}
			if (((vpos=strstr(pos,",v")) !=0  ) ||
			    ((vpos=strstr(pos,",V")) !=0  )) {
				*vpos='\0';
				fl_cpubind_verbose=1;
			}
		}
		if (mappos) {
			*mappos='\0'; 
			mappos++;
			cmd_line_mapping=mappos;
		}

		/* convert parsed command line args into interface */
		if (cmd_line_mapping) {
			xfree(*cpu_bind);
			*cpu_bind = xstrdup(cmd_line_mapping);
		}
		if (fl_cpubind_verbose) {
		        *cpu_bind_type |= CPU_BIND_VERBOSE;
		}
		if (cmd_line_affinity) {
			*cpu_bind_type &= CPU_BIND_VERBOSE;	/* clear any
								 * previous type */
			if ((strcasecmp(cmd_line_affinity, "no") == 0) ||
			    (strcasecmp(cmd_line_affinity, "none") == 0)) {
				*cpu_bind_type |= CPU_BIND_NONE;
			} else if (strcasecmp(cmd_line_affinity, "rank") == 0) {
				*cpu_bind_type |= CPU_BIND_RANK;
			} else if ((strcasecmp(cmd_line_affinity, "map_cpu") == 0) ||
			           (strcasecmp(cmd_line_affinity, "mapcpu") == 0)) {
				*cpu_bind_type |= CPU_BIND_MAPCPU;
			} else if ((strcasecmp(cmd_line_affinity, "mask_cpu") == 0) ||
			           (strcasecmp(cmd_line_affinity, "maskcpu") == 0)) {
				*cpu_bind_type |= CPU_BIND_MASKCPU;
			} else {
				error("unrecognized --cpu_bind argument \"%s\"", 
					cmd_line_affinity);
				xfree(buf);
				return 1;
			}
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
	char *buf = xstrdup(arg);
	char *pos = buf;
	/* we support different launch policy names
	 * we also allow a verbose setting to be specified
	 *     --mem_bind=v
	 *     --mem_bind=rank,v
	 *     --mem_bind=rank
	 *     --mem_bind={MAP_CPU|MAP_MASK}:0,1,2,3,4
	 */
	if (*pos) {
		/* parse --mem_bind command line arguments */
		bool fl_membind_verbose = 0;
		char *cmd_line_affinity = NULL;
		char *cmd_line_mapping  = NULL;
		char *mappos = strchr(pos,':');
		if (!mappos) {
			mappos = strchr(pos,'=');
		}
		if (strncasecmp(pos, "quiet", 5) == 0) {
			fl_membind_verbose = 0;
			pos+=5;
		} else if (*pos=='q' || *pos=='Q') {
			fl_membind_verbose = 0;
			pos++;
		}
		if (strncasecmp(pos, "verbose", 7) == 0) {
			fl_membind_verbose = 1;
			pos+=7;
		} else if (*pos=='v' || *pos=='V') {
			fl_membind_verbose = 1;
			pos++;
		}
		if (*pos==',') {
			pos++;
		}
		if (*pos) {
			char *vpos=NULL;
			cmd_line_affinity = pos;
			if (((vpos=strstr(pos,",q")) !=0  ) ||
			    ((vpos=strstr(pos,",Q")) !=0  )) {
				*vpos='\0';
				fl_membind_verbose = 0;
			}
			if (((vpos=strstr(pos,",v")) !=0  ) ||
			    ((vpos=strstr(pos,",V")) !=0  )) {
				*vpos='\0';
				fl_membind_verbose = 1;
			}
		}
		if (mappos) {
			*mappos='\0';
			mappos++;
			cmd_line_mapping=mappos;
		}

		/* convert parsed command line args into interface */
		if (cmd_line_mapping) {
			xfree(*mem_bind);
			*mem_bind = xstrdup(cmd_line_mapping);
		}
		if (fl_membind_verbose) {
			*mem_bind_type |= MEM_BIND_VERBOSE;
		}
		if (cmd_line_affinity) {
			*mem_bind_type &= MEM_BIND_VERBOSE;	/* clear any
								 * previous type */
			if ((strcasecmp(cmd_line_affinity, "no") == 0) ||
			    (strcasecmp(cmd_line_affinity, "none") == 0)) {
				*mem_bind_type |= MEM_BIND_NONE;
			} else if (strcasecmp(cmd_line_affinity, "rank") == 0) {
				*mem_bind_type |= MEM_BIND_RANK;
			} else if (strcasecmp(cmd_line_affinity, "local") == 0) {
				*mem_bind_type |= MEM_BIND_LOCAL;
			} else if ((strcasecmp(cmd_line_affinity, "map_mem") == 0) ||
			           (strcasecmp(cmd_line_affinity, "mapmem") == 0)) {
				*mem_bind_type |= MEM_BIND_MAPCPU;
			} else if ((strcasecmp(cmd_line_affinity, "mask_mem") == 0) ||
			           (strcasecmp(cmd_line_affinity, "maskmem") == 0)) {
				*mem_bind_type |= MEM_BIND_MASKCPU;
			} else {
				error("unrecognized --mem_bind argument \"%s\"",
					cmd_line_affinity);
				xfree(buf);
				return 1;
			}
		}
	}

	xfree(buf);
	return 0;
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
 * and set alloc_info variables pertaining to cpus-per-node.
 */
static int _set_cpus_per_node(const char *str)
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

	alloc_info.num_cpu_groups = num_cpus_groups;
	alloc_info.cpus_per_node = cpus;
	alloc_info.cpu_count_reps = cpus_reps;

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
	opt.time_limit = -1;
	opt.relative = (uint16_t)NO_VAL;
	opt.relative_set = false;

	opt.job_name = NULL;
	opt.jobid    = NO_VAL;
	opt.jobid_set = false;

	opt.distribution = SLURM_DIST_CYCLIC;
	opt.distribution_set = false;

	opt.local_ofname = NULL;
	opt.local_ifname = NULL;
	opt.local_efname = NULL;
	opt.remote_ofname = NULL;
	opt.remote_ifname = NULL;
	opt.remote_efname = NULL;

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

	/* constraint default (-1 is no constraint) */
	opt.mincpus	    = -1;
	opt.realmem	    = -1;
	opt.tmpdisk	    = -1;

	opt.constraints	    = NULL;
        opt.exclusive       = false;
	opt.nodelist	    = NULL;
	opt.task_layout	    = NULL;
	opt.task_layout_set = false;
	opt.task_layout_file_set = false;
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

	opt.prolog = slurm_get_srun_prolog();
	opt.epilog = slurm_get_srun_epilog();

	opt.task_prolog     = NULL;
	opt.task_epilog     = NULL;

	opt.ctrl_comm_ifhn  = xshort_hostname();

	/*
	 * Reset some default values if running under a parallel debugger
	 */
	if ((opt.parallel_debug = _under_parallel_debugger())) {
		opt.max_launch_time = 120;
		opt.msg_timeout     = 15;
	}

	opt.no_alloc = false;
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
  {"SLURM_JOB_ID",        OPT_INT,
   &alloc_info.job_id, &alloc_info_set.job_id},
  {"SLURM_JOB_NUM_NODES", OPT_INT,
   &alloc_info.node_cnt, &alloc_info_set.node_cnt},
  {"SLURM_JOB_NODELIST",  OPT_STRING,
   &alloc_info.node_list, &alloc_info_set.node_list},
  {"SLURM_JOB_CPUS_PER_NODE", OPT_CPUS_PER_NODE, NULL, NULL},
  {"SLAUNCH_JOBID",        OPT_INT,       &opt.jobid,         &opt.jobid_set },
  {"SLURMD_DEBUG",         OPT_INT,       &opt.slurmd_debug,  NULL           }, 
  {"SLAUNCH_CPUS_PER_TASK",OPT_INT,       &opt.cpus_per_task, &opt.cpus_per_task_set},
  {"SLAUNCH_CONN_TYPE",    OPT_CONN_TYPE, NULL,               NULL           },
  {"SLAUNCH_CORE_FORMAT",  OPT_CORE,      NULL,               NULL           },
  {"SLAUNCH_CPU_BIND",     OPT_CPU_BIND,  NULL,               NULL           },
  {"SLAUNCH_MEM_BIND",     OPT_MEM_BIND,  NULL,               NULL           },
  {"SLAUNCH_DEBUG",        OPT_DEBUG,     NULL,               NULL           },
  {"SLAUNCH_DISTRIBUTION", OPT_DISTRIB,   NULL,               NULL           },
  {"SLAUNCH_GEOMETRY",     OPT_GEOMETRY,  NULL,               NULL           },
  {"SLAUNCH_KILL_BAD_EXIT",OPT_INT,       &opt.kill_bad_exit, NULL           },
  {"SLAUNCH_LABELIO",      OPT_INT,       &opt.labelio,       NULL           },
  {"SLAUNCH_NUM_NODES",    OPT_INT,       &opt.num_nodes,  &opt.num_nodes_set},
  {"SLAUNCH_NO_ROTATE",    OPT_NO_ROTATE, NULL,               NULL           },
  {"SLAUNCH_NPROCS",       OPT_INT,       &opt.num_tasks,  &opt.num_tasks_set},
  {"SLAUNCH_OVERCOMMIT",   OPT_OVERCOMMIT,NULL,               NULL           },
  {"SLAUNCH_REMOTE_CWD",   OPT_STRING,    &opt.cwd,           NULL           },
  {"SLAUNCH_STDERRMODE",   OPT_STRING,    &opt.local_efname,  NULL           },
  {"SLAUNCH_STDINMODE",    OPT_STRING,    &opt.local_ifname,  NULL           },
  {"SLAUNCH_STDOUTMODE",   OPT_STRING,    &opt.local_ofname,  NULL           },
  {"SLAUNCH_TIMELIMIT",    OPT_INT,       &opt.time_limit,    NULL           },
  {"SLAUNCH_WAIT",         OPT_INT,       &opt.max_wait,      NULL           },
  {"SLAUNCH_MPI_TYPE",     OPT_MPI,       NULL,               NULL           },
  {"SLAUNCH_SRUN_COMM_IFHN",OPT_STRING,   &opt.ctrl_comm_ifhn,NULL           },
  {"SLAUNCH_SRUN_MULTI",   OPT_MULTI,     NULL,               NULL           },
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

	alloc_info_set.job_id = false;
	alloc_info_set.node_list = false;
	alloc_info_set.cpu_info = false;
	alloc_info_set.node_cnt = false;
	
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
			opt.verbose = (int) strtol(val, &end, 10);
			if (!(end && *end == '\0')) 
				error("%s=%s invalid", e->var, val);
		}
		break;

	case OPT_DISTRIB:
		dt = _verify_dist_type(val);
		if (dt == -1) {
			error("\"%s=%s\" -- invalid distribution type. " 
			      "ignoring...", e->var, val);
		} else {
			opt.distribution = dt;
			opt.distribution_set = true;
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

	case OPT_OVERCOMMIT:
		opt.overcommit = true;
		break;

	case OPT_CORE:
		opt.core_type = core_format_type (val);
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

	case OPT_MPI:
		if (srun_mpi_init((char *)val) == SLURM_ERROR) {
			fatal("\"%s=%s\" -- invalid MPI type, "
			      "--mpi=list for acceptable types.",
			      e->var, val);
		}
		break;

	case OPT_CPUS_PER_NODE:
		if (_set_cpus_per_node(val)) {
			alloc_info_set.cpu_info = true;
		}
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
	static bool set_cwd=false, set_name=false;
	struct utsname name;
	static struct option long_options[] = {
		{"cpus-per-task", required_argument, 0, 'c'},
		{"slurmd-debug",  required_argument, 0, 'd'},
		{"chdir",         required_argument, 0, 'D'},
		{"local-error",   required_argument, 0, 'e'},
		{"remote-error",  required_argument, 0, 'E'},
		{"task-layout-file",required_argument,0,'F'},
		{"geometry",      required_argument, 0, 'g'},
		{"help",          no_argument,       0, 'h'},
		{"local-input",   required_argument, 0, 'i'},
		{"remote-input",  required_argument, 0, 'I'},
		{"job-name",      required_argument, 0, 'J'},
		{"no-kill",       no_argument,       0, 'k'},
		{"kill-on-bad-exit", no_argument,    0, 'K'},
		{"label",         no_argument,       0, 'l'},
		{"distribution",  required_argument, 0, 'm'},
		{"ntasks",        required_argument, 0, 'n'},
		{"nodes",         required_argument, 0, 'N'},
		{"local-output",  required_argument, 0, 'o'},
		{"remote-output", required_argument, 0, 'O'},
		{"overcommit",    no_argument,       0, 'C'},
		{"quiet",            no_argument,    0, 'q'},
		{"relative",      required_argument, 0, 'r'},
		{"no-rotate",     no_argument,       0, 'R'},
		{"time",          required_argument, 0, 't'},
		{"unbuffered",    no_argument,       0, 'u'},
		{"task-layout",   required_argument, 0, 'T'},
		{"verbose",       no_argument,       0, 'v'},
		{"version",       no_argument,       0, 'V'},
		{"nodelist",      required_argument, 0, 'w'},
		{"wait",          required_argument, 0, 'W'},
		{"no-allocate",   no_argument,       0, 'Z'},
		{"exclusive",        no_argument,       0, LONG_OPT_EXCLUSIVE},
		{"cpu_bind",         required_argument, 0, LONG_OPT_CPU_BIND},
		{"mem_bind",         required_argument, 0, LONG_OPT_MEM_BIND},
		{"core",             required_argument, 0, LONG_OPT_CORE},
		{"mincpus",          required_argument, 0, LONG_OPT_MINCPU},
		{"mem",              required_argument, 0, LONG_OPT_MEM},
		{"mpi",              required_argument, 0, LONG_OPT_MPI},
		{"tmp",              required_argument, 0, LONG_OPT_TMP},
		{"jobid",            required_argument, 0, LONG_OPT_JOBID},
		{"msg-timeout",      required_argument, 0, LONG_OPT_TIMEO},
		{"max-launch-time",  required_argument, 0, LONG_OPT_LAUNCH},
		{"max-exit-timeout", required_argument, 0, LONG_OPT_XTO},
		{"uid",              required_argument, 0, LONG_OPT_UID},
		{"gid",              required_argument, 0, LONG_OPT_GID},
		{"debugger-test",    no_argument,       0, LONG_OPT_DEBUG_TS},
		{"usage",            no_argument,       0, LONG_OPT_USAGE},
		{"conn-type",        required_argument, 0, LONG_OPT_CONNTYPE},
		{"network",          required_argument, 0, LONG_OPT_NETWORK},
		{"propagate",        optional_argument, 0, LONG_OPT_PROPAGATE},
		{"prolog",           required_argument, 0, LONG_OPT_PROLOG},
		{"epilog",           required_argument, 0, LONG_OPT_EPILOG},
		{"task-prolog",      required_argument, 0, LONG_OPT_TASK_PROLOG},
		{"task-epilog",      required_argument, 0, LONG_OPT_TASK_EPILOG},
		{"ctrl-comm-ifhn",   required_argument, 0, LONG_OPT_CTRL_COMM_IFHN},
		{"multi-prog",       no_argument,       0, LONG_OPT_MULTI},
		{NULL,               0,                 0, 0}
	};
	char *opt_string = "+c:Cd:D:e:E:F:g:hi:I:J:kKlm:n:N:"
		"o:O:qr:R:t:T:uvVw:W:Z";

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
				_get_int(optarg, "cpus-per-task");
			break;
		case 'C':
			opt.overcommit = true;
			break;
		case 'd':
			opt.slurmd_debug = 
				_get_int(optarg, "slurmd-debug");
			break;
		case 'D':
			set_cwd = true;
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
		{
			char *tmp;
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
		}
			break;
		case 'E':
			xfree(opt.remote_efname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.remote_efname = xstrdup("/dev/null");
			else
				opt.remote_efname = xstrdup(optarg);
			break;
		case 'g':
			if (_verify_geometry(optarg, opt.geometry))
				exit(1);
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
			set_name = true;
			xfree(opt.job_name);
			opt.job_name = xstrdup(optarg);
			break;
		case 'k':
			opt.no_kill = true;
			break;
		case 'K':
			opt.kill_bad_exit = true;
			break;
		case 'l':
			opt.labelio = true;
			break;
		case 'm':
			opt.distribution = _verify_dist_type(optarg);
			if (opt.distribution == -1) {
				error("distribution type `%s' " 
				      "is not recognized", optarg);
				exit(1);
			} else {
				opt.distribution_set = true;
			}
			break;
		case 'n':
			opt.num_tasks_set = true;
			opt.num_tasks = _get_int(optarg, "number of tasks");
			break;
		case 'N':
			opt.num_nodes_set = true;
			opt.num_nodes = _get_int(optarg, "number of nodes");
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
			opt.relative = _get_int(optarg, "relative start node");
			break;
		case 'R':
			opt.no_rotate = true;
			break;
		case 't':
			opt.time_limit = _get_int(optarg, "time");
			break;
		case 'T':
			xfree(opt.task_layout);
			opt.task_layout = xstrdup(optarg);
			opt.task_layout_set = true;
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
			if (!_valid_node_list(&opt.nodelist))
				exit(1);
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
		case 'Z':
			opt.no_alloc = true;
			uname(&name);
			if (strcasecmp(name.sysname, "AIX") == 0)
				opt.network = xstrdup("ip");
			break;
                case LONG_OPT_EXCLUSIVE:
                        opt.exclusive = true;
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
		case LONG_OPT_MPI:
			if (srun_mpi_init((char *)optarg) == SLURM_ERROR) {
				fatal("\"--mpi=%s\" -- long invalid MPI type, "
				      "--mpi=list for acceptable types.",
				      optarg);
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
			opt.jobid_set = true;
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
			opt.debugger_test    = true;
			/* make other parameters look like debugger 
			 * is really attached */
			opt.parallel_debug   = true;
			MPIR_being_debugged  = 1;
			opt.max_launch_time = 120;
			opt.msg_timeout     = 15;
			break;
		case LONG_OPT_USAGE:
			_usage();
			exit(0);
		case LONG_OPT_CONNTYPE:
			opt.conn_type = _verify_conn_type(optarg);
			break;
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
		case LONG_OPT_CTRL_COMM_IFHN:
			xfree(opt.ctrl_comm_ifhn);
			opt.ctrl_comm_ifhn = xstrdup(optarg);
			break;
		case LONG_OPT_MULTI:
			opt.multi_prog = true;
			break;
		default:
			if (spank_process_option (opt_char, optarg) < 0) {
				exit (1);
			}
		}
	}

	spank_option_table_destroy (optz);
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
		char *cmd       = opt.argv[0];
		bool search_cwd = false; /* was: (opt.batch || opt.allocate); */
		int  mode       = (search_cwd) ? R_OK : R_OK | X_OK;

		if ((fullpath = _search_path(cmd, search_cwd, mode))) {
			xfree(opt.argv[0]);
			opt.argv[0] = fullpath;
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
	hostlist_t task_l = NULL;
	hostlist_t node_l = NULL;
	
	/* This rather confusing "if" statement assures that we look up
	 * the resource_allocation_response_msg_t structure on the
	 * controller if either of the following situations exist:
	 * 
	 * 1) We could not completely construct the
	 *    resource_allocation_response_msg_t structure from SLURM_JOB_*
	 *    environment variables.
	 * 2) The user specified a job id other than the one found
	 *    in the SLURM_JOB_* environment variables.
	 */
	if (!(alloc_info_set.job_id
	      && alloc_info_set.node_list
	      && alloc_info_set.cpu_info
	      && alloc_info_set.node_cnt)
	    || (opt.jobid_set
		&& alloc_info_set.job_id
		&& (opt.jobid != alloc_info.job_id))
	    || (opt.jobid_set && !alloc_info_set.job_id)) {

		uint32_t jobid;
		if (opt.jobid_set) {
			jobid = opt.jobid;
		} else if (alloc_info_set.job_id) {
			jobid = alloc_info.job_id;
		} else {
			error("No job id specified!");
			verified = false;
			exit(1);
		}
		verbose("Need to look up allocation info with the controller");

		if (slurm_allocation_lookup_lite(jobid, &alloc_info_ptr) < 0) {
			error("Unable to look up job ID %u: %m", jobid);
			verified = false;
			exit(1);
		} else {
			alloc_info_set.job_id = true;
			alloc_info_set.node_list = true;
			alloc_info_set.cpu_info = true;
			alloc_info_set.node_cnt = true;
		}

	} else {
		alloc_info_ptr = &alloc_info;
	}

	/*
	 * Now set default options based on allocation info.
	 */
	if (!opt.jobid_set && alloc_info_set.job_id)
		opt.jobid = alloc_info_ptr->job_id;
	if (!opt.num_nodes_set && alloc_info_set.node_cnt)
		opt.num_nodes = alloc_info_ptr->node_cnt;


	/*
	 * Finally, make sure that all of the other options play well together.
	 */
	if (opt.task_layout_set && opt.task_layout_file_set) {
		error("Only one of -T/--task-layout or -F/--task-layout-file"
		      " may be used.");
		verified = false;
	}
	if (opt.task_layout_set && opt.nodelist) {
		error("Only one of -T/--task-layout or -w/--nodelist"
		      " may be used.");
		verified = false;
	}
	if (opt.nodelist && opt.task_layout_file_set) {
		error("Only one of -w/--nodelist or -F/--task-layout-file"
		      " may be used.");
		verified = false;
	}
	if (opt.task_layout_set && opt.num_nodes_set) {
		error("only one of -F/--task-layout-file and -N/--node"
		      " may be used.");
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
		/* task_layout and task_layout_file both implicitly trigger
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

	if (!opt.jobid_set && !alloc_info_set.job_id) {
		error("A job ID MUST be specified on the command line,");
		error("or through the SLAUNCH_JOBID environment variable.");
		verified = false;
	}

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	if (opt.no_alloc && !opt.nodelist) {
		error("must specify a node list with -Z/--no-allocate.");
		verified = false;
	}

	if (opt.relative_set) {
		if (opt.no_alloc) {
			error("-r/--relative not allowed with"
			      " -Z/--no-allocate.");
			verified = false;
		}

		if (opt.nodelist != NULL) {
			error("-r/--relative not allowed with"
			      " -w/--nodelist.");
			verified = false;
		}

		if (opt.task_layout_set) {
			error("-r/--relative not allowed with"
			      " -T/--task-layout");
			verified = false;
		}

		if (opt.task_layout_file_set) {
			error("-r/--relative not allowed with"
			      " -F/--task-layout-file");
			verified = false;
		}
	}
	if (opt.mincpus < opt.cpus_per_task)
		opt.mincpus = opt.cpus_per_task;

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

	/*
	 * --wait always overrides hidden max_exit_timeout
	 */
	if (opt.max_wait)
		opt.max_exit_timeout = opt.max_wait;

	if (opt.time_limit == 0)
		opt.time_limit = INFINITE;

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
_search_path(char *cmd, bool check_current_dir, int access_mode)
{
	List         l        = _create_path_list();
	ListIterator i        = NULL;
	char *path, *fullpath = NULL;

	if (l == NULL)
		return NULL;

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

        if (opt.exclusive == true)
                xstrcat(buf, "exclusive ");

	if (opt.nodelist != NULL)
		xstrfmtcat(buf, "nodelist=%s ", opt.nodelist);

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
	for (i = 0; i < opt.argc; i++)
		snprintf(buf, 256,  "%s", opt.argv[i]);
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

#define tf_(b) (b == true) ? "true" : "false"

static void _opt_list()
{
	char *str;

	info("defined options for program \"%s\"", opt.progname);
	info("--------------- ---------------------");

	info("user           : \"%s\"", opt.user);
	info("uid            : %ld", (long) opt.uid);
	info("gid            : %ld", (long) opt.gid);
	info("cwd            : %s", opt.cwd);
	info("num_tasks      : %d %s", opt.num_tasks,
		opt.num_tasks_set ? "(set)" : "(default)");
	info("cpus_per_task  : %d %s", opt.cpus_per_task,
		opt.cpus_per_task_set ? "(set)" : "(default)");
	info("nodes          : %d %s",
	     opt.num_nodes, opt.num_nodes_set ? "(set)" : "(default)");
	info("jobid          : %u %s", opt.jobid, 
		opt.jobid_set ? "(set)" : "(default)");
	info("job name       : \"%s\"", opt.job_name);
	info("distribution   : %s %s",
	     format_task_dist_states(opt.distribution),
	     opt.distribution_set ? "(set)" : "(default)");
	info("cpu_bind       : %s", 
	     opt.cpu_bind == NULL ? "default" : opt.cpu_bind);
	info("mem_bind       : %s",
	     opt.mem_bind == NULL ? "default" : opt.mem_bind);
	info("core format    : %s", core_format_name (opt.core_type));
	info("verbose        : %d", opt.verbose);
	info("slurmd_debug   : %d", opt.slurmd_debug);
	info("label output   : %s", tf_(opt.labelio));
	info("unbuffered IO  : %s", tf_(opt.unbuffered));
	info("overcommit     : %s", tf_(opt.overcommit));
	if (opt.time_limit == INFINITE)
		info("time_limit     : INFINITE");
	else
		info("time_limit     : %d", opt.time_limit);
	info("wait           : %d", opt.max_wait);
	str = print_constraints();
	info("constraints    : %s", str);
	xfree(str);
	if (opt.conn_type >= 0)
		info("conn_type      : %u", opt.conn_type);
	str = print_geometry();
	info("geometry       : %s", str);
	xfree(str);
	info("rotate         : %s", opt.no_rotate ? "yes" : "no");
	info("network        : %s", opt.network);
	info("propagate      : %s",
	     opt.propagate == NULL ? "NONE" : opt.propagate);
	info("prolog         : %s", opt.prolog);
	info("epilog         : %s", opt.epilog);
	info("task_prolog    : %s", opt.task_prolog);
	info("task_epilog    : %s", opt.task_epilog);
	info("ctrl_comm_ifhn : %s", opt.ctrl_comm_ifhn);
	info("multi_prog     : %s", opt.multi_prog ? "yes" : "no");
	str = print_commandline();
	info("remote command : \"%s\"", str);
	xfree(str);

}

/* Determine if slaunch is under the control of a parallel debugger or not */
static bool _under_parallel_debugger (void)
{
	return (MPIR_being_debugged != 0);
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
"               [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"               [--mpi=type]\n"
"               [--kill-on-bad-exit] [--propagate[=rlimits] ]\n"
"               [--cpu_bind=...] [--mem_bind=...]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
"               [--geometry=XxYxZ] [--conn-type=type] [--no-rotate]\n"
#endif
"               [--prolog=fname] [--epilog=fname]\n"
"               [--task-prolog=fname] [--task-epilog=fname]\n"
"               [--ctrl-comm-ifhn=addr] [--multi-prog]\n"
"               [-w hosts...] executable [args...]\n");
}

static void _help(void)
{
        printf (
"Usage: slaunch [OPTIONS...] executable [args...]\n"
"\n"
"Parallel run options:\n"
"  -n, --ntasks=ntasks         number of tasks to run\n"
"  -N, --nodes=N               number of nodes on which to run\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"  -i, --local-input=in        location of local stdin redirection\n"
"  -o, --local-output=out      location of local stdout redirection\n"
"  -e, --local-error=err       location of local stderr redirection\n"
"  -I, --remote-input=in       location of remote stdin redirection\n"
"  -O, --remote-output=out     location of remote stdout redirection\n"
"  -E, --remote-error=err      location of remote stderr redirection\n"
"  -r, --relative=n            run job step relative to node n of allocation\n"
"  -t, --time=minutes          time limit\n"
"  -D, --chdir=path            change remote current working directory\n"
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
"      --mpi=type              type of MPI being used\n"
"  -b, --batch                 submit as batch job for later execution\n"
"  -W, --wait=sec              seconds to wait after first task exits\n"
"                              before killing job\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -Q, --quiet                 quiet mode (suppress informational messages)\n"
"  -d, --slurmd-debug=level    slurmd debug level\n"
"      --core=type             change default corefile format type\n"
"                              (type=\"list\" to list of valid formats)\n"
"      --propagate[=rlimits]   propagate all [or specific list of] rlimits\n"
"      --mpi=type              specifies version of MPI to use\n"
"      --prolog=program        run \"program\" before launching job step\n"
"      --epilog=program        run \"program\" after launching job step\n"
"      --task-prolog=program   run \"program\" before launching task\n"
"      --task-epilog=program   run \"program\" after launching task\n"
"      --ctrl-comm-ifhn=addr   interface hostname for PMI commaunications from slaunch\n"
"      --multi-prog            if set the program name specified is the\n"
"                              configuration specificaiton for multiple programs\n"
"\n"
"Constraint options:\n"
"      --mincpus=n             minimum number of cpus per node\n"
"      --mem=MB                minimum amount of real memory\n"
"      --tmp=MB                minimum amount of temporary disk\n"
"  -C, --constraint=list       specify a list of constraints\n"
"  -w, --nodelist=hosts...     request a specific list of hosts\n"
"  -Z, --no-allocate           don't allocate nodes (must supply -w)\n"
"\n"
"Consumable resources related options:\n" 
"      --exclusive             allocate nodes in exclusive mode when\n" 
"                              cpu consumable resource is enabled\n"
"\n"
"Affinity/Multi-core options: (when the task/affinity plugin is enabled)\n" 
"      --cpu_bind=             Bind tasks to CPUs\n" 
"             q[uiet],           quietly bind before task runs (default)\n"
"             v[erbose],         verbosely report binding before task runs\n"
"             no[ne]             don't bind tasks to CPUs (default)\n"
"             rank               bind by task rank\n"
"             map_cpu:<list>     bind by mapping CPU IDs to tasks as specified\n"
"                                where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"             mask_cpu:<list>    bind by setting CPU masks on tasks as specified\n"
"                                where <list> is <mask1>,<mask2>,...<maskN>\n"
"      --mem_bind=             Bind tasks to memory\n"
"             q[uiet],           quietly bind before task runs (default)\n"
"             v[erbose],         verbosely report binding before task runs\n"
"             no[ne]             don't bind tasks to memory (default)\n"
"             rank               bind by task rank\n"
"             local              bind to memory local to processor\n"
"             map_mem:<list>     bind by mapping memory of CPU IDs to tasks as specified\n"
"                                where <list> is <cpuid1>,<cpuid2>,...<cpuidN>\n"
"             mask_mem:<list>    bind by setting menory of CPU masks on tasks as specified\n"
"                                where <list> is <mask1>,<mask2>,...<maskN>\n");

	spank_print_options (stdout, 6, 30);
	printf("\n");

        printf(
#ifdef HAVE_AIX				/* AIX/Federation specific options */
  "AIX related options:\n"
  "  --network=type              communication protocol to be used\n"
  "\n"
#endif

#ifdef HAVE_BG				/* Blue gene specific options */
  "Blue Gene related options:\n"
  "  -g, --geometry=XxYxZ        geometry constraints of the job\n"
  "  -R, --no-rotate             disable geometry rotation\n"
  "      --conn-type=type        constraint on type of connection, MESH or TORUS\n"
  "                              if not set, then tries to fit TORUS else MESH\n"
  "\n"
#endif
"Help options:\n"
"      --help                  show this help message\n"
"      --usage                 display brief usage message\n"
"\n"
"Other options:\n"
"  -V, --version               output version information and exit\n"
"\n"
);

}
