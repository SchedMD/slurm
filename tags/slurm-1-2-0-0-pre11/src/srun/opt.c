/*****************************************************************************\
 *  opt.c - options processing for srun
 *  $Id$
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
#include "src/common/slurm_protocol_interface.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/plugstack.h"
#include "src/common/optz.h"
#include "src/api/pmi_server.h"

#include "src/srun/opt.h"
#include "src/srun/attach.h"
#include "src/common/mpi.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_DISTRIB     0x04
#define OPT_NODES       0x05
#define OPT_OVERCOMMIT  0x06
#define OPT_CORE        0x07
#define OPT_CONN_TYPE	0x08
#define OPT_NO_ROTATE	0x0a
#define OPT_GEOMETRY	0x0b
#define OPT_MPI         0x0c
#define OPT_CPU_BIND    0x0d
#define OPT_MEM_BIND    0x0e
#define OPT_MULTI       0x0f
#define OPT_NSOCKETS    0x10
#define OPT_NCORES      0x11
#define OPT_NTHREADS    0x12

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_HELP        0x100
#define LONG_OPT_USAGE       0x101
#define LONG_OPT_XTO         0x102
#define LONG_OPT_LAUNCH      0x103
#define LONG_OPT_TIMEO       0x104
#define LONG_OPT_JOBID       0x105
#define LONG_OPT_TMP         0x106
#define LONG_OPT_MEM         0x107
#define LONG_OPT_MINCPUS     0x108
#define LONG_OPT_CONT        0x109
#define LONG_OPT_UID         0x10a
#define LONG_OPT_GID         0x10b
#define LONG_OPT_MPI         0x10c
#define LONG_OPT_CORE	     0x10e
#define LONG_OPT_NOSHELL     0x10f
#define LONG_OPT_DEBUG_TS    0x110
#define LONG_OPT_CONNTYPE    0x111
#define LONG_OPT_TEST_ONLY   0x113
#define LONG_OPT_NETWORK     0x114
#define LONG_OPT_EXCLUSIVE   0x115
#define LONG_OPT_PROPAGATE   0x116
#define LONG_OPT_PROLOG      0x117
#define LONG_OPT_EPILOG      0x118
#define LONG_OPT_BEGIN       0x119
#define LONG_OPT_MAIL_TYPE   0x11a
#define LONG_OPT_MAIL_USER   0x11b
#define LONG_OPT_TASK_PROLOG 0x11c
#define LONG_OPT_TASK_EPILOG 0x11d
#define LONG_OPT_NICE        0x11e
#define LONG_OPT_CPU_BIND    0x11f
#define LONG_OPT_MEM_BIND    0x120
#define LONG_OPT_CTRL_COMM_IFHN 0x121
#define LONG_OPT_MULTI       0x122
#define LONG_OPT_NO_REQUEUE  0x123
#define LONG_OPT_COMMENT     0x124
#define LONG_OPT_SOCKETSPERNODE  0x130
#define LONG_OPT_CORESPERSOCKET	 0x131
#define LONG_OPT_THREADSPERCORE  0x132
#define LONG_OPT_MINSOCKETS	 0x133
#define LONG_OPT_MINCORES	 0x134
#define LONG_OPT_MINTHREADS	 0x135
#define LONG_OPT_NTASKSPERNODE	 0x136
#define LONG_OPT_NTASKSPERSOCKET 0x137
#define LONG_OPT_NTASKSPERCORE	 0x138
#define LONG_OPT_JOBMEM	         0x13a
#define LONG_OPT_HINT	         0x13b
#define LONG_OPT_BLRTS_IMAGE     0x140
#define LONG_OPT_LINUX_IMAGE     0x141
#define LONG_OPT_MLOADER_IMAGE   0x142
#define LONG_OPT_RAMDISK_IMAGE   0x143
#define LONG_OPT_REBOOT          0x144

/*---- global variables, defined in opt.h ----*/
char **remote_argv;
int remote_argc;
int _verbose;
enum modes mode;
opt_t opt;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

/* return command name from its full path name */
static char * _base_name(char* command);

static List  _create_path_list(void);

/* Get a decimal integer from arg */
static int  _get_int(const char *arg, const char *what, bool positive);
static bool _get_resource_range(const char *arg, const char *what, 
                                int *min, int *max, bool isFatal);

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

static uint16_t _parse_mail_type(const char *arg);
static char *_print_mail_type(const uint16_t type);

/* search PATH for command returns full path */
static char *_search_path(char *, bool, int);

static long  _to_bytes(const char *arg);

static bool  _under_parallel_debugger(void);

static void  _usage(void);
static bool  _valid_node_list(char **node_list_pptr);
static task_dist_states_t _verify_dist_type(const char *arg, uint32_t *psize);
static bool  _verify_socket_core_thread_count(const char *arg,
					   int *min_sockets, int *max_sockets,
					   int *min_cores, int *max_cores,
					   int *min_threads, int  *max_threads,
					   cpu_bind_type_t *cpu_bind_type);
static bool  _verify_hint(const char *arg,
					   int *min_sockets, int *max_sockets,
					   int *min_cores, int *max_cores,
					   int *min_threads, int  *max_threads,
					   cpu_bind_type_t *cpu_bind_type);
static int   _verify_cpu_bind(const char *arg, char **cpu_bind,
			      cpu_bind_type_t *cpu_bind_type);
static int   _verify_geometry(const char *arg, uint16_t *geometry);
static int   _verify_mem_bind(const char *arg, char **mem_bind,
			      mem_bind_type_t *mem_bind_type);
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

	if (_verbose > 3)
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
 * verify that a resource counts in arg are of a known form X, X:X, X:X:X, or 
 * X:X:X:X, where X is defined as either (count, min-max, or '*')
 * RET true if valid
 */
static bool
_verify_socket_core_thread_count(const char *arg, 
			      int *min_sockets, int *max_sockets,
			      int *min_cores, int *max_cores,
			      int *min_threads, int  *max_threads,
			      cpu_bind_type_t *cpu_bind_type)
{
	bool tmp_val,ret_val;
	int i,j;
	const char *cur_ptr = arg;
	char buf[3][48]; /* each can hold INT64_MAX - INT64_MAX */
	buf[0][0] = '\0';
	buf[1][0] = '\0';
	buf[2][0] = '\0';

 	for (j=0;j<3;j++) {	
		for (i=0;i<47;i++) {
			if (*cur_ptr == '\0' || *cur_ptr ==':') break;
			buf[j][i] = *cur_ptr++;
		}
		if (*cur_ptr == '\0') break;
		xassert(*cur_ptr == ':');
		buf[j][i] = '\0';
		cur_ptr++;
	}
	/* if cpu_bind_type doesn't already have a auto preference, choose
	 * the level based on the level of the -E specification
	 */
	if (!(*cpu_bind_type & (CPU_BIND_TO_SOCKETS |
				CPU_BIND_TO_CORES |
				CPU_BIND_TO_THREADS))) {
		if (j == 0) {
			*cpu_bind_type |= CPU_BIND_TO_SOCKETS;
		} else if (j == 1) {
			*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (j == 2) {
			*cpu_bind_type |= CPU_BIND_TO_THREADS;
		}
        }
	buf[j][i] = '\0';

	ret_val = true;
	tmp_val = _get_resource_range(&buf[0][0], "first arg of -B", 
				      min_sockets, max_sockets, true);
	ret_val = ret_val && tmp_val;
	tmp_val = _get_resource_range(&buf[1][0], "second arg of -B", 
				      min_cores, max_cores, true);
	ret_val = ret_val && tmp_val;
	tmp_val = _get_resource_range(&buf[2][0], "third arg of -B", 
				      min_threads, max_threads, true);
	ret_val = ret_val && tmp_val;

	return ret_val;
}

/* 
 * verify that a hint is valid and convert it into the implied settings
 * RET true if valid
 */
static bool
_verify_hint(const char *arg, 
			      int *min_sockets, int *max_sockets,
			      int *min_cores, int *max_cores,
			      int *min_threads, int  *max_threads,
			      cpu_bind_type_t *cpu_bind_type)
{
	char *buf, *p, *tok;
	if (!arg) {
		return true;
	}

	buf = xstrdup(arg);
	p = buf;
	/* change all ',' delimiters not followed by a digit to ';'  */
	/* simplifies parsing tokens while keeping map/mask together */
	while (p[0] != '\0') {
		if ((p[0] == ',') && (!isdigit(p[1])))
			p[0] = ';';
		p++;
	}

	p = buf;
	while ((tok = strsep(&p, ";"))) {
		if (strcasecmp(tok, "help") == 0) {
			printf(
"Application hint options:\n"
"    --hint=             Bind tasks according to application hints\n"
"        compute_bound   use all cores in each physical CPU\n"
"        memory_bound    use only one core in each physical CPU\n"
"        [no]multithread [don't] use extra threads with in-core multi-threading\n"
"        help            show this help message\n");
			return 1;
		} else if (strcasecmp(tok, "compute_bound") == 0) {
		        *min_sockets = 1;
		        *max_sockets = INT_MAX;
		        *min_cores   = 1;
		        *max_cores   = INT_MAX;
			*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (strcasecmp(tok, "memory_bound") == 0) {
		        *min_cores = 1;
		        *max_cores = 1;
			*cpu_bind_type |= CPU_BIND_TO_CORES;
		} else if (strcasecmp(tok, "multithread") == 0) {
		        *min_threads = 1;
		        *max_threads = INT_MAX;
			*cpu_bind_type |= CPU_BIND_TO_THREADS;
		} else if (strcasecmp(tok, "nomultithread") == 0) {
		        *min_threads = 1;
		        *max_threads = 1;
			*cpu_bind_type |= CPU_BIND_TO_THREADS;
		} else {
			error("unrecognized --hint argument \"%s\", see --hint=help", tok);
			xfree(buf);
			return 1;
		}
	}

	xfree(buf);
	return 0;
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
	int i;
	char hostname[64];

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
	opt.min_sockets_per_node = NO_VAL; /* requested min/maxsockets */
	opt.max_sockets_per_node = NO_VAL;
	opt.min_cores_per_socket = NO_VAL; /* requested min/maxcores */
	opt.max_cores_per_socket = NO_VAL;
	opt.min_threads_per_core = NO_VAL; /* requested min/maxthreads */
	opt.max_threads_per_core = NO_VAL; 
	opt.ntasks_per_node      = NO_VAL; /* ntask max limits */
	opt.ntasks_per_socket    = NO_VAL; 
	opt.ntasks_per_core      = NO_VAL; 
	opt.nodes_set = false;
	opt.cpu_bind_type = 0;
	opt.cpu_bind = NULL;
	opt.mem_bind_type = 0;
	opt.mem_bind = NULL;
	opt.time_limit = NO_VAL;
	opt.partition = NULL;
	opt.max_threads = MAX_THREADS;
	pmi_server_max_threads(opt.max_threads);

	opt.relative = NO_VAL;
	opt.relative_set = false;
	opt.job_name = NULL;
	opt.jobid    = NO_VAL;
	opt.jobid_set = false;
	opt.dependency = NO_VAL;
	opt.account  = NULL;
	opt.comment  = NULL;

	opt.distribution = SLURM_DIST_UNKNOWN;
	opt.plane_size   = NO_VAL;

	opt.ofname = NULL;
	opt.ifname = NULL;
	opt.efname = NULL;

	opt.core_type = CORE_DEFAULT;

	opt.labelio = false;
	opt.unbuffered = false;
	opt.overcommit = false;
	opt.batch = false;
	opt.shared = (uint16_t)NO_VAL;
	opt.no_kill = false;
	opt.kill_bad_exit = false;

	opt.immediate	= false;
	opt.no_requeue	= false;

	opt.allocate	= false;
	opt.noshell	= false;
	opt.attach	= NULL;
	opt.join	= false;
	opt.max_wait	= slurm_get_wait_time();

	opt.quit_on_intr = false;
	opt.disable_status = false;
	opt.test_only   = false;

	opt.quiet = 0;
	_verbose = 0;
	opt.slurmd_debug = LOG_LEVEL_QUIET;

	opt.job_min_cpus    = NO_VAL;
	opt.job_min_sockets = NO_VAL;
	opt.job_min_cores   = NO_VAL;
	opt.job_min_threads = NO_VAL;
	opt.job_min_memory  = NO_VAL;
	opt.job_max_memory  = NO_VAL;
	opt.job_min_tmp_disk= NO_VAL;

	opt.hold	    = false;
	opt.constraints	    = NULL;
	opt.contiguous	    = false;
	opt.nodelist	    = NULL;
	opt.exc_nodes	    = NULL;
	opt.max_launch_time = 120;/* 120 seconds to launch job             */
	opt.max_exit_timeout= 60; /* Warn user 60 seconds after task exit */
	/* Default launch msg timeout           */
	opt.msg_timeout     = slurm_get_msg_timeout();  

	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		opt.geometry[i]	    = (uint16_t) NO_VAL;
	opt.reboot          = false;
	opt.no_rotate	    = false;
	opt.conn_type	    = (uint16_t) NO_VAL;
	opt.blrtsimage = NULL;
	opt.linuximage = NULL;
	opt.mloaderimage = NULL;
	opt.ramdiskimage = NULL;

	opt.euid	    = (uid_t) -1;
	opt.egid	    = (gid_t) -1;
	
	opt.propagate	    = NULL;  /* propagate specific rlimits */

	opt.prolog = slurm_get_srun_prolog();
	opt.epilog = slurm_get_srun_epilog();

	opt.task_prolog     = NULL;
	opt.task_epilog     = NULL;

	mode	= MODE_NORMAL;

	gethostname_short(hostname, sizeof(hostname));
	opt.ctrl_comm_ifhn  = xstrdup(hostname);

	/*
	 * Reset some default values if running under a parallel debugger
	 */
	if ((opt.parallel_debug = _under_parallel_debugger())) {
		opt.max_launch_time = 120;
		opt.max_threads     = 1;
		pmi_server_max_threads(opt.max_threads);
		opt.msg_timeout     = 15;
	}

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
	{"SLURM_ACCOUNT",       OPT_STRING,     &opt.account,       NULL           },
	{"SLURMD_DEBUG",        OPT_INT,        &opt.slurmd_debug,  NULL           },
	{"SLURM_BLRTS_IMAGE",   OPT_STRING,     &opt.blrtsimage,    NULL           },
	{"SLURM_CPUS_PER_TASK", OPT_INT,        &opt.cpus_per_task, &opt.cpus_set  },
	{"SLURM_CONN_TYPE",     OPT_CONN_TYPE,  NULL,               NULL           },
	{"SLURM_CORE_FORMAT",   OPT_CORE,       NULL,               NULL           },
	{"SLURM_CPU_BIND",      OPT_CPU_BIND,   NULL,               NULL           },
	{"SLURM_MEM_BIND",      OPT_MEM_BIND,   NULL,               NULL           },
	{"SLURM_DISTRIBUTION",  OPT_DISTRIB,    NULL,               NULL           },
	{"SLURM_GEOMETRY",      OPT_GEOMETRY,   NULL,               NULL           },
	{"SLURM_IMMEDIATE",     OPT_INT,        &opt.immediate,     NULL           },
	{"SLURM_JOBID",         OPT_INT,        &opt.jobid,         NULL           },
	{"SLURM_KILL_BAD_EXIT", OPT_INT,        &opt.kill_bad_exit, NULL           },
	{"SLURM_LABELIO",       OPT_INT,        &opt.labelio,       NULL           },
	{"SLURM_LINUX_IMAGE",   OPT_STRING,     &opt.linuximage,    NULL           },
	{"SLURM_MLOADER_IMAGE", OPT_STRING,     &opt.mloaderimage,  NULL           },
	{"SLURM_NNODES",        OPT_NODES,      NULL,               NULL           },
	{"SLURM_NSOCKETS_PER_NODE",OPT_NSOCKETS,NULL,               NULL           },
	{"SLURM_NCORES_PER_SOCKET",OPT_NCORES,  NULL,               NULL           },
	{"SLURM_NTHREADS_PER_CORE",OPT_NTHREADS,NULL,               NULL           },
	{"SLURM_NO_REQUEUE",    OPT_INT,        &opt.no_requeue,    NULL           },
	{"SLURM_NO_ROTATE",     OPT_NO_ROTATE,  NULL,               NULL           },
	{"SLURM_NPROCS",        OPT_INT,        &opt.nprocs,        &opt.nprocs_set},
	{"SLURM_OVERCOMMIT",    OPT_OVERCOMMIT, NULL,               NULL           },
	{"SLURM_PARTITION",     OPT_STRING,     &opt.partition,     NULL           },
	{"SLURM_RAMDISK_IMAGE", OPT_STRING,     &opt.ramdiskimage,  NULL           },
	{"SLURM_REMOTE_CWD",    OPT_STRING,     &opt.cwd,           NULL           },
	{"SLURM_STDERRMODE",    OPT_STRING,     &opt.efname,        NULL           },
	{"SLURM_STDINMODE",     OPT_STRING,     &opt.ifname,        NULL           },
	{"SLURM_STDOUTMODE",    OPT_STRING,     &opt.ofname,        NULL           },
	{"SLURM_TIMELIMIT",     OPT_INT,        &opt.time_limit,    NULL           },
	{"SLURM_WAIT",          OPT_INT,        &opt.max_wait,      NULL           },
	{"SLURM_DISABLE_STATUS",OPT_INT,        &opt.disable_status,NULL           },
	{"SLURM_MPI_TYPE",      OPT_MPI,        NULL,               NULL           },
	{"SLURM_SRUN_COMM_IFHN",OPT_STRING,     &opt.ctrl_comm_ifhn,NULL           },
	{"SLURM_SRUN_MULTI",    OPT_MULTI,      NULL,               NULL           },
	{"SLURM_UNBUFFEREDIO",  OPT_INT,        &opt.unbuffered,    NULL           },
	{"SLURM_NODELIST",      OPT_STRING,     &opt.alloc_nodelist,NULL           },
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
	task_dist_states_t dt;

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

	case OPT_DISTRIB:
		if (strcmp(val, "unknown") == 0)
			break;	/* ignore it, passed from salloc */
		dt = _verify_dist_type(val, &opt.plane_size);
		if (dt == SLURM_DIST_UNKNOWN) {
			error("\"%s=%s\" -- invalid distribution type. " 
			      "ignoring...", e->var, val);
		} else 
			opt.distribution = dt;
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

	case OPT_NODES:
		opt.nodes_set = _get_resource_range( val ,"OPT_NODES", 
						     &opt.min_nodes, 
						     &opt.max_nodes, false);
		if (opt.nodes_set == false) {
			error("\"%s=%s\" -- invalid node count. ignoring...",
			      e->var, val);
		}
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
_get_int(const char *arg, const char *what, bool positive)
{
	char *p;
	long int result = strtol(arg, &p, 10);

	if ((*p != '\0') || (result < 0L)
	||  (positive && (result <= 0L))) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		exit(1);
	} else if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
	} else if (result < INT_MIN) {
		error ("Numeric argument %ld to small for %s.", result, what);
	}

	return (int) result;
}

/* 
 * get either 1 or 2 integers for a resource count in the form of either
 * (count, min-max, or '*')
 * A partial error message is passed in via the 'what' param.
 * RET true if valid
 */
static bool
_get_resource_range(const char *arg, const char *what, int* min, int *max, 
              	    bool isFatal)
{
	char *p;
	long int result;

	if (*arg == '\0') return true;

	/* wildcard meaning every possible value in range */
	if (*arg == '*' ) {
		*min = 1;
		*max = INT_MAX;
		return true;
	}

	result = strtol(arg, &p, 10);
        if (*p == 'k' || *p == 'K') {
		result *= 1024;
		p++;
	}

	if (((*p != '\0')&&(*p != '-')) || (result <= 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		if (isFatal) exit(1);
		return false;
	} else if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
		if (isFatal) exit(1);
		return false;
	}

	*min = (int) result;

	if (*p == '\0') return true;
	if (*p == '-') p++;

	result = strtol(p, &p, 10);
        if (*p == 'k' || *p == 'K') {
		result *= 1024;
		p++;
	}
	
	if (((*p != '\0')&&(*p != '-')) || (result <= 0L)) {
		error ("Invalid numeric value \"%s\" for %s.", arg, what);
		if (isFatal) exit(1);
		return false;
	} else if (result > INT_MAX) {
		error ("Numeric argument (%ld) to big for %s.", result, what);
		if (isFatal) exit(1);
		return false;
	}

	*max = (int) result;

	return true;
}

void set_options(const int argc, char **argv, int first)
{
	int opt_char, option_index = 0;
	static bool set_cwd=false, set_name=false;
	struct utsname name;
	static struct option long_options[] = {
		{"attach",        required_argument, 0, 'a'},
		{"allocate",      no_argument,       0, 'A'},
		{"batch",         no_argument,       0, 'b'},
		{"extra-node-info", required_argument, 0, 'B'},
		{"cpus-per-task", required_argument, 0, 'c'},
		{"constraint",    required_argument, 0, 'C'},
		{"slurmd-debug",  required_argument, 0, 'd'},
		{"chdir",         required_argument, 0, 'D'},
		{"error",         required_argument, 0, 'e'},
		{"geometry",      required_argument, 0, 'g'},
		{"hold",          no_argument,       0, 'H'},
		{"input",         required_argument, 0, 'i'},
		{"immediate",     no_argument,       0, 'I'},
		{"join",          no_argument,       0, 'j'},
		{"job-name",      required_argument, 0, 'J'},
		{"no-kill",       no_argument,       0, 'k'},
		{"kill-on-bad-exit", no_argument,    0, 'K'},
		{"label",         no_argument,       0, 'l'},
		{"distribution",  required_argument, 0, 'm'},
		{"ntasks",        required_argument, 0, 'n'},
		{"nodes",         required_argument, 0, 'N'},
		{"output",        required_argument, 0, 'o'},
		{"overcommit",    no_argument,       0, 'O'},
		{"partition",     required_argument, 0, 'p'},
		{"dependency",    required_argument, 0, 'P'},
		{"quit-on-interrupt", no_argument,   0, 'q'},
		{"quiet",            no_argument,    0, 'Q'},
		{"relative",      required_argument, 0, 'r'},
		{"no-rotate",     no_argument,       0, 'R'},
		{"share",         no_argument,       0, 's'},
		{"time",          required_argument, 0, 't'},
		{"threads",       required_argument, 0, 'T'},
		{"unbuffered",    no_argument,       0, 'u'},
		{"account",       required_argument, 0, 'U'},
		{"verbose",       no_argument,       0, 'v'},
		{"version",       no_argument,       0, 'V'},
		{"nodelist",      required_argument, 0, 'w'},
		{"wait",          required_argument, 0, 'W'},
		{"exclude",       required_argument, 0, 'x'},
		{"disable-status", no_argument,      0, 'X'},
		{"no-allocate",   no_argument,       0, 'Z'},
		{"contiguous",       no_argument,       0, LONG_OPT_CONT},
		{"exclusive",        no_argument,       0, LONG_OPT_EXCLUSIVE},
		{"cpu_bind",         required_argument, 0, LONG_OPT_CPU_BIND},
		{"mem_bind",         required_argument, 0, LONG_OPT_MEM_BIND},
		{"core",             required_argument, 0, LONG_OPT_CORE},
		{"mincpus",          required_argument, 0, LONG_OPT_MINCPUS},
		{"minsockets",       required_argument, 0, LONG_OPT_MINSOCKETS},
		{"mincores",         required_argument, 0, LONG_OPT_MINCORES},
		{"minthreads",       required_argument, 0, LONG_OPT_MINTHREADS},
		{"mem",              required_argument, 0, LONG_OPT_MEM},
		{"job-mem",          required_argument, 0, LONG_OPT_JOBMEM},
		{"hint",             required_argument, 0, LONG_OPT_HINT},
		{"mpi",              required_argument, 0, LONG_OPT_MPI},
		{"no-shell",         no_argument,       0, LONG_OPT_NOSHELL},
		{"tmp",              required_argument, 0, LONG_OPT_TMP},
		{"jobid",            required_argument, 0, LONG_OPT_JOBID},
		{"msg-timeout",      required_argument, 0, LONG_OPT_TIMEO},
		{"max-launch-time",  required_argument, 0, LONG_OPT_LAUNCH},
		{"max-exit-timeout", required_argument, 0, LONG_OPT_XTO},
		{"uid",              required_argument, 0, LONG_OPT_UID},
		{"gid",              required_argument, 0, LONG_OPT_GID},
		{"debugger-test",    no_argument,       0, LONG_OPT_DEBUG_TS},
		{"help",             no_argument,       0, LONG_OPT_HELP},
		{"usage",            no_argument,       0, LONG_OPT_USAGE},
		{"conn-type",        required_argument, 0, LONG_OPT_CONNTYPE},
		{"test-only",        no_argument,       0, LONG_OPT_TEST_ONLY},
		{"network",          required_argument, 0, LONG_OPT_NETWORK},
		{"propagate",        optional_argument, 0, LONG_OPT_PROPAGATE},
		{"prolog",           required_argument, 0, LONG_OPT_PROLOG},
		{"epilog",           required_argument, 0, LONG_OPT_EPILOG},
		{"begin",            required_argument, 0, LONG_OPT_BEGIN},
		{"mail-type",        required_argument, 0, LONG_OPT_MAIL_TYPE},
		{"mail-user",        required_argument, 0, LONG_OPT_MAIL_USER},
		{"task-prolog",      required_argument, 0, LONG_OPT_TASK_PROLOG},
		{"task-epilog",      required_argument, 0, LONG_OPT_TASK_EPILOG},
		{"nice",             optional_argument, 0, LONG_OPT_NICE},
		{"ctrl-comm-ifhn",   required_argument, 0, LONG_OPT_CTRL_COMM_IFHN},
		{"multi-prog",       no_argument,       0, LONG_OPT_MULTI},
		{"no-requeue",       no_argument,       0, LONG_OPT_NO_REQUEUE},
		{"comment",          required_argument, 0, LONG_OPT_COMMENT},
		{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
		{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
		{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
		{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
		{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
		{"ntasks-per-core",  required_argument, 0, LONG_OPT_NTASKSPERCORE},
		{"blrts-image",      required_argument, 0, LONG_OPT_BLRTS_IMAGE},
		{"linux-image",      required_argument, 0, LONG_OPT_LINUX_IMAGE},
		{"mloader-image",    required_argument, 0, LONG_OPT_MLOADER_IMAGE},
		{"ramdisk-image",    required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
		{"reboot",           no_argument,       0, LONG_OPT_REBOOT},            
		{NULL,               0,                 0, 0}
	};
	char *opt_string = "+a:AbB:c:C:d:D:e:g:Hi:IjJ:kKlm:n:N:"
		"o:Op:P:qQr:R:st:T:uU:vVw:W:x:XZ";

	struct option *optz = spank_option_table_create (long_options);

	if (!optz) {
		error ("Unable to create option table");
		exit (1);
	}

	if(opt.progname == NULL)
		opt.progname = xbasename(argv[0]);
	else if(!first)
		argv[0] = opt.progname;
	else
		error("opt.progname is set but it is the first time through.");
	optind = 0;		
	while((opt_char = getopt_long(argc, argv, opt_string,
				      optz, &option_index)) != -1) {
		switch (opt_char) {

		case (int)'?':
			if(first) {
				fprintf(stderr, "Try \"srun --help\" for more "
					"information\n");
				exit(1);
			} 
			break;
		case (int)'a':
			if(first) {
				if (opt.allocate || opt.batch) {
					error("can only specify one mode: "
					      "allocate, attach or batch.");
					exit(1);
				}
				mode = MODE_ATTACH;
				opt.attach = strdup(optarg);
			} else {
				error("Option '%c' can only be set "
				      "from srun commandline.", opt_char);
			}
			break;
		case (int)'A':
			if(first) {
				if (opt.attach || opt.batch) {
					error("can only specify one mode: "
					      "allocate, attach or batch.");
					exit(1);
				}
				mode = MODE_ALLOCATE;
				opt.allocate = true;
			} else {
				error("Option '%c' can only be set "
				      "from srun commandline.", opt_char);
			}
			break;
		case (int)'b':
			if(first) {
				if (opt.allocate || opt.attach) {
					error("can only specify one mode: "
					      "allocate, attach or batch.");
					exit(1);
				}
				mode = MODE_BATCH;
				opt.batch = true;
			} else {
				error("Option '%c' can only be set "
				      "from srun commandline.", opt_char);
			}
			break;
		case (int)'B':
			if(!first && opt.extra_set)
				break;

			opt.extra_set = _verify_socket_core_thread_count(
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
		case (int)'c':
			if(!first && opt.cpus_set)
				break;
			opt.cpus_set = true;
			opt.cpus_per_task = 
				_get_int(optarg, "cpus-per-task", true);
			break;
		case (int)'C':
			if(!first && opt.constraints)
				break;
			xfree(opt.constraints);
			opt.constraints = xstrdup(optarg);
			break;
		case (int)'d':
			if(!first && opt.slurmd_debug)
				break;
			
			opt.slurmd_debug = 
				_get_int(optarg, "slurmd-debug", false);
			break;
		case (int)'D':
			if(!first && set_cwd)
				break;

			set_cwd = true;
			xfree(opt.cwd);
			opt.cwd = xstrdup(optarg);
			break;
		case (int)'e':
			if(!first && opt.efname)
				break;
			
			xfree(opt.efname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.efname = xstrdup("/dev/null");
			else
				opt.efname = xstrdup(optarg);
			break;
		case (int)'g':
			if(!first && opt.geometry)
				break;
			if (_verify_geometry(optarg, opt.geometry))
				exit(1);
			break;
		case (int)'H':
			opt.hold = true;
			break;
		case (int)'i':
			if(!first && opt.ifname)
				break;
			
			xfree(opt.ifname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.ifname = xstrdup("/dev/null");
			else
				opt.ifname = xstrdup(optarg);
			break;
		case (int)'I':
			opt.immediate = true;
			break;
		case (int)'j':
			opt.join = true;
			break;
		case (int)'J':
			if(!first && set_name)
				break;

			set_name = true;
			xfree(opt.job_name);
			opt.job_name = xstrdup(optarg);
			break;
		case (int)'k':
			opt.no_kill = true;
			break;
		case (int)'K':
			opt.kill_bad_exit = true;
			break;
		case (int)'l':
			opt.labelio = true;
			break;
		case (int)'m':
			if(!first && opt.distribution)
				break;
			opt.distribution = _verify_dist_type(optarg, 
							     &opt.plane_size);
			if (opt.distribution == SLURM_DIST_UNKNOWN) {
				error("distribution type `%s' " 
				      "is not recognized", optarg);
				exit(1);
			}
			break;
		case (int)'n':
			if(!first && opt.nprocs_set)
				break;
						
			opt.nprocs_set = true;
			opt.nprocs = 
				_get_int(optarg, "number of tasks", true);
			break;
		case (int)'N':
			if(!first && opt.nodes_set)
				break;
						
			opt.nodes_set = 
				_get_resource_range(optarg, 
						    "requested node count",
						    &opt.min_nodes,
						    &opt.max_nodes, true);
			
			if (opt.nodes_set == false) {
				error("invalid resource allocation -N `%s'", 
				      optarg);
				exit(1);
			}
			break;
		case (int)'o':
			if(!first && opt.ofname)
				break;			
			
			xfree(opt.ofname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.ofname = xstrdup("/dev/null");
			else
				opt.ofname = xstrdup(optarg);
			break;
		case (int)'O':
			opt.overcommit = true;
			break;
		case (int)'p':
			if(!first && opt.partition)
				break;
						
			xfree(opt.partition);
			opt.partition = xstrdup(optarg);
			break;
		case (int)'P':
			if(!first && opt.dependency)
				break;
						
			opt.dependency = _get_int(optarg, "dependency", true);
			break;
		case (int)'q':
			opt.quit_on_intr = true;
			break;
		case (int) 'Q':
			if(!first && opt.quiet)
				break;
			
			opt.quiet++;
			break;
		case (int)'r':
			if(!first && opt.relative)
				break;
			
			//xfree(opt.relative);
			opt.relative = _get_int(optarg, "relative", false);
			opt.relative_set = true;
			break;
		case (int)'R':
			opt.no_rotate = true;
			break;
		case (int)'s':
			opt.shared = 1;
			break;
		case (int)'t':
			if(!first && opt.time_limit)
				break;
			
			opt.time_limit = _get_int(optarg, "time", true);
			break;
		case (int)'T':
			if(!first && opt.max_threads)
				break;
			
			opt.max_threads = 
				_get_int(optarg, "max_threads", true);
			pmi_server_max_threads(opt.max_threads);
			break;
		case (int)'u':
			opt.unbuffered = true;
			break;
		case (int)'U':
			if(!first && opt.account)
				break;
			xfree(opt.account);
			opt.account = xstrdup(optarg);
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
		case (int)'w':
			if(!first && opt.nodelist)
				break;
			
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
		case (int)'W':
			opt.max_wait = _get_int(optarg, "wait", false);
			break;
		case (int)'x':
			xfree(opt.exc_nodes);
			opt.exc_nodes = xstrdup(optarg);
			if (!_valid_node_list(&opt.exc_nodes))
				exit(1);
			break;
		case (int)'X': 
			opt.disable_status = true;
			break;
		case (int)'Z':
			opt.no_alloc = true;
			uname(&name);
			if (strcasecmp(name.sysname, "AIX") == 0)
				opt.network = xstrdup("ip");
			break;
		case LONG_OPT_CONT:
			opt.contiguous = true;
			break;
                case LONG_OPT_EXCLUSIVE:
                        opt.shared = 0;
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
		case LONG_OPT_MINCPUS:
			opt.job_min_cpus = _get_int(optarg, "mincpus", true);
			break;
		case LONG_OPT_MINSOCKETS:
			opt.job_min_sockets = _get_int(optarg, "minsockets", 
				true);
			break;
		case LONG_OPT_MINCORES:
			opt.job_min_cores = _get_int(optarg, "mincores", true);
			break;
		case LONG_OPT_MINTHREADS:
			opt.job_min_threads = _get_int(optarg, "minthreads", 
				true);
			break;
		case LONG_OPT_MEM:
			opt.job_min_memory = (int) _to_bytes(optarg);
			if (opt.job_min_memory < 0) {
				error("invalid memory constraint %s", 
				      optarg);
				exit(1);
			}
			break;
		case LONG_OPT_JOBMEM:
			opt.job_max_memory = (int) _to_bytes(optarg);
			if (opt.job_max_memory < 0) {
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
		case LONG_OPT_NOSHELL:
			opt.noshell = true;
			break;
		case LONG_OPT_TMP:
			opt.job_min_tmp_disk = _to_bytes(optarg);
			if (opt.job_min_tmp_disk < 0) {
				error("invalid tmp value %s", optarg);
				exit(1);
			}
			break;
		case LONG_OPT_JOBID:
			opt.jobid = _get_int(optarg, "jobid", true);
			opt.jobid_set = true;
			break;
		case LONG_OPT_TIMEO:
			opt.msg_timeout = 
				_get_int(optarg, "msg-timeout", true);
			break;
		case LONG_OPT_LAUNCH:
			opt.max_launch_time = 
				_get_int(optarg, "max-launch-time", true);
			break;
		case LONG_OPT_XTO:
			opt.max_exit_timeout = 
				_get_int(optarg, "max-exit-timeout", true);
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
			opt.max_threads     = 1;
			pmi_server_max_threads(opt.max_threads);
			opt.msg_timeout     = 15;
			break;
		case LONG_OPT_HELP:
			_help();
			exit(0);
		case LONG_OPT_USAGE:
			_usage();
			exit(0);
		case LONG_OPT_CONNTYPE:
			opt.conn_type = _verify_conn_type(optarg);
			break;
		case LONG_OPT_TEST_ONLY:
			opt.test_only = true;
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
		case LONG_OPT_BEGIN:
			opt.begin = parse_time(optarg);
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
		case LONG_OPT_TASK_PROLOG:
			xfree(opt.task_prolog);
			opt.task_prolog = xstrdup(optarg);
			break;
		case LONG_OPT_TASK_EPILOG:
			xfree(opt.task_epilog);
			opt.task_epilog = xstrdup(optarg);
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
		case LONG_OPT_CTRL_COMM_IFHN:
			xfree(opt.ctrl_comm_ifhn);
			opt.ctrl_comm_ifhn = xstrdup(optarg);
			break;
		case LONG_OPT_MULTI:
			opt.multi_prog = true;
			break;
		case LONG_OPT_NO_REQUEUE:
			opt.no_requeue = true;
			break;
		case LONG_OPT_COMMENT:
			if(!first && opt.comment)
				break;
			xfree(opt.comment);
			opt.comment = xstrdup(optarg);
			break;
		case LONG_OPT_SOCKETSPERNODE:
			_get_resource_range( optarg, "sockets-per-node",
				             &opt.min_sockets_per_node,
				             &opt.max_sockets_per_node, true );
			break;
		case LONG_OPT_CORESPERSOCKET:
			_get_resource_range( optarg, "cores-per-socket",
				             &opt.min_cores_per_socket,
				             &opt.max_cores_per_socket, true);
			break;
		case LONG_OPT_THREADSPERCORE:
			_get_resource_range( optarg, "threads-per-core",
				             &opt.min_threads_per_core,
				             &opt.max_threads_per_core, true );
			break;
		case LONG_OPT_HINT:
			if (_verify_hint(optarg,
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
			opt.ntasks_per_node = _get_int(optarg, "ntasks-per-node",
				true);
			break;
		case LONG_OPT_NTASKSPERSOCKET:
			opt.ntasks_per_socket = _get_int(optarg, 
				"ntasks-per-socket", true);
			break;
		case LONG_OPT_NTASKSPERCORE:
			opt.ntasks_per_core = _get_int(optarg, "ntasks-per-core",
				true);
			break;
		case LONG_OPT_BLRTS_IMAGE:
			if(!first && opt.blrtsimage)
				break;			
			xfree(opt.blrtsimage);
			opt.blrtsimage = xstrdup(optarg);
			break;
		case LONG_OPT_LINUX_IMAGE:
			if(!first && opt.linuximage)
				break;			
			xfree(opt.linuximage);
			opt.linuximage = xstrdup(optarg);
			break;
		case LONG_OPT_MLOADER_IMAGE:
			if(!first && opt.mloaderimage)
				break;			
			xfree(opt.mloaderimage);
			opt.mloaderimage = xstrdup(optarg);
			break;
		case LONG_OPT_RAMDISK_IMAGE:
			if(!first && opt.ramdiskimage)
				break;			
			xfree(opt.ramdiskimage);
			opt.ramdiskimage = xstrdup(optarg);
			break;
		case LONG_OPT_REBOOT:
			opt.reboot = true;
			break;
		default:
			if (spank_process_option (opt_char, optarg) < 0) {
				exit (1);
			}
		}
	}

	if (!first) {
		if (!_opt_verify())
			exit(1);
		if (_verbose > 3)
			_opt_list();
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
	data_buf = xmalloc(stat_buf.st_size + 1);
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

	set_options(argc, argv, 1);	

        /* When CR with memory as a CR is enabled we need to assign
	   adequate value or check the value to opt.mem */
	if ((opt.job_min_memory >= -1) && (opt.job_max_memory > 0)) {
		if (opt.job_min_memory == -1) {
			opt.job_min_memory = opt.job_max_memory;
		} else if (opt.job_min_memory < opt.job_max_memory) {
			info("mem < job-mem - resizing mem to be equal to job-mem");
			opt.job_min_memory = opt.job_max_memory;
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
				     (opt.min_nodes-1)*opt.plane_size, opt.nprocs);
#endif
				error("Too few processes for the requested "
				      "{plane,node} distribution");
				exit(1);
			}
		}
	}
	
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
	hostlist_t hl = NULL;
	int hl_cnt = 0;

	/*
	 *  Do not set slurmd debug level higher than DEBUG2,
	 *   as DEBUG3 is used for slurmd IO operations, which
	 *   are not appropriate to be sent back to srun. (because
	 *   these debug messages cause the generation of more
	 *   debug messages ad infinitum)
	 */
	if (opt.slurmd_debug + LOG_LEVEL_ERROR > LOG_LEVEL_DEBUG2)
		opt.slurmd_debug = LOG_LEVEL_DEBUG2 - LOG_LEVEL_ERROR;

	if (opt.quiet && _verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	if (opt.no_alloc && !opt.nodelist) {
		error("must specify a node list with -Z, --no-allocate.");
		verified = false;
	}

	if (opt.no_alloc && opt.exc_nodes) {
		error("can not specify --exclude list with -Z, --no-allocate.");
		verified = false;
	}

	if (opt.no_alloc && opt.relative_set) {
		error("do not specify -r,--relative with -Z,--no-allocate.");
		verified = false;
	}

	if (opt.relative_set && (opt.exc_nodes || opt.nodelist)) {
		error("-r,--relative not allowed with "
		      "-w,--nodelist or -x,--exclude.");
		verified = false;
	}

	if (opt.job_min_cpus < opt.cpus_per_task)
		opt.job_min_cpus = opt.cpus_per_task;

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

		core_format_enable (opt.core_type);

		/* massage the numbers */
		if (opt.nodelist) {
			hl = hostlist_create(opt.nodelist);
			if (!hl)
				fatal("memory allocation failure");
			hostlist_uniq(hl);
			hl_cnt = hostlist_count(hl);
			if (opt.nodes_set)
				opt.min_nodes = MAX(hl_cnt, opt.min_nodes);
			else {
				opt.min_nodes = hl_cnt;
				opt.nodes_set = true;
			}
		}
		if ((opt.nodes_set || opt.extra_set) && !opt.nprocs_set) {
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
				if (opt.max_nodes 
				&&  (opt.min_nodes > opt.max_nodes) )
					opt.max_nodes = opt.min_nodes;
				if (hl_cnt > opt.min_nodes) {
					int del_cnt, i;
					char *host;
					del_cnt = hl_cnt - opt.min_nodes;
					for (i=0; i<del_cnt; i++) {
						host = hostlist_pop(hl);
						free(host);
					}
					hostlist_ranged_string(hl, strlen(opt.nodelist)+1, 
							       opt.nodelist);
				}
			}

		} /* else if (opt.nprocs_set && !opt.nodes_set) */
	}
	if (hl)
		hostlist_destroy(hl);

	if (opt.max_threads <= 0) {	/* set default */
		error("Thread value invalid, reset to 1");
		opt.max_threads = 1;
		pmi_server_max_threads(opt.max_threads);
	} else if (opt.max_threads > MAX_THREADS) {
		error("Thread value exceeds defined limit, reset to %d", 
		      MAX_THREADS);
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

	if (opt.time_limit == 0)
		opt.time_limit = INFINITE;

	if ((opt.euid != (uid_t) -1) && (opt.euid != opt.uid)) 
		opt.uid = opt.euid;

	if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid)) 
		opt.gid = opt.egid;

        if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid))
	        opt.gid = opt.egid;

	if (opt.noshell && !opt.allocate) {
		error ("--no-shell only valid with -A (--allocate)");
		verified = false;
	}

	if (opt.propagate && parse_rlimits( opt.propagate, PROPAGATE_RLIMITS)) {
		error( "--propagate=%s is not valid.", opt.propagate );
		verified = false;
	}

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

static void
_freeF(void *data)
{
	xfree(data);
}

static List
_create_path_list(void)
{
	List l = list_create(_freeF);
	char *path = xstrdup(getenv("PATH"));
	char *c, *lc;

	if (!path) {
		error("Error in PATH environment variable");
		return l;
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

	if (opt.job_min_cpus > 0)
		xstrfmtcat(buf, "mincpus=%d ", opt.job_min_cpus);

	if (opt.job_min_sockets > 0)
		xstrfmtcat(buf, "minsockets=%d ", opt.job_min_sockets);

	if (opt.job_min_cores > 0)
		xstrfmtcat(buf, "mincores=%d ", opt.job_min_cores);

	if (opt.job_min_threads > 0)
		xstrfmtcat(buf, "minthreads=%d ", opt.job_min_threads);

	if (opt.job_min_memory > 0)
		xstrfmtcat(buf, "mem=%dM ", opt.job_min_memory);

	if (opt.job_max_memory > 0)
		xstrfmtcat(buf, "job-mem=%dM ", opt.job_max_memory);

	if (opt.job_min_tmp_disk > 0)
		xstrfmtcat(buf, "tmp=%ld ", opt.job_min_tmp_disk);

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
	info("cpu_bind       : %s", 
	     opt.cpu_bind == NULL ? "default" : opt.cpu_bind);
	info("mem_bind       : %s",
	     opt.mem_bind == NULL ? "default" : opt.mem_bind);
	info("core format    : %s", core_format_name (opt.core_type));
	info("verbose        : %d", _verbose);
	info("slurmd_debug   : %d", opt.slurmd_debug);
	info("immediate      : %s", tf_(opt.immediate));
	info("no-requeue     : %s", tf_(opt.no_requeue));
	info("label output   : %s", tf_(opt.labelio));
	info("unbuffered IO  : %s", tf_(opt.unbuffered));
	info("allocate       : %s", tf_(opt.allocate));
	info("attach         : `%s'", opt.attach);
	info("overcommit     : %s", tf_(opt.overcommit));
	info("batch          : %s", tf_(opt.batch));
	info("threads        : %d", opt.max_threads);
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
	if (opt.conn_type != (uint16_t) NO_VAL)
		info("conn_type      : %u", opt.conn_type);
	str = print_geometry();
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

	info("network        : %s", opt.network);
	info("propagate      : %s",
	     opt.propagate == NULL ? "NONE" : opt.propagate);
	if (opt.begin) {
		char time_str[32];
		slurm_make_time_str(&opt.begin, time_str, sizeof(time_str));
		info("begin          : %s", time_str);
	}
	info("prolog         : %s", opt.prolog);
	info("epilog         : %s", opt.epilog);
	info("mail_type      : %s", _print_mail_type(opt.mail_type));
	info("mail_user      : %s", opt.mail_user);
	info("task_prolog    : %s", opt.task_prolog);
	info("task_epilog    : %s", opt.task_epilog);
	info("ctrl_comm_ifhn : %s", opt.ctrl_comm_ifhn);
	info("multi_prog     : %s", opt.multi_prog ? "yes" : "no");
	info("ntasks-per-node   : %d", opt.ntasks_per_node);
	info("ntasks-per-socket : %d", opt.ntasks_per_socket);
	info("ntasks-per-core   : %d", opt.ntasks_per_core);
	info("plane_size        : %u", opt.plane_size);
	str = print_commandline();
	info("remote command    : `%s'", str);
	xfree(str);

}

/* Determine if srun is under the control of a parallel debugger or not */
static bool _under_parallel_debugger (void)
{
	return (MPIR_being_debugged != 0);
}

static void _usage(void)
{
 	printf(
"Usage: srun [-N nnodes] [-n ntasks] [-i in] [-o out] [-e err]\n"
"            [-c ncpus] [-r n] [-p partition] [--hold] [-t minutes]\n"
"            [-D path] [--immediate] [--overcommit] [--no-kill]\n"
"            [--share] [--label] [--unbuffered] [-m dist] [-J jobname]\n"
"            [--jobid=id] [--batch] [--verbose] [--slurmd_debug=#]\n"
"            [--core=type] [-T threads] [-W sec] [--attach] [--join] \n"
"            [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"            [--mpi=type] [--account=name] [--dependency=jobid]\n"
"            [--kill-on-bad-exit] [--propagate[=rlimits] [--comment=name]\n"
"            [--cpu_bind=...] [--mem_bind=...]\n"
"            [--ntasks-per-node=n] [--ntasks-per-socket=n]\n"
"            [--ntasks-per-core=n]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
"            [--geometry=XxYxZ] [--conn-type=type] [--no-rotate] [--reboot]\n"
"            [--blrts-image=path] [--linux-image=path]\n"
"            [--mloader-image=path] [--ramdisk-image=path]\n"
#endif
		"            [--mail-type=type] [--mail-user=user] [--nice[=value]]\n"
		"            [--prolog=fname] [--epilog=fname]\n"
		"            [--task-prolog=fname] [--task-epilog=fname]\n"
		"            [--ctrl-comm-ifhn=addr] [--multi-prog] [--no-requeue]\n"
		"            [-w hosts...] [-x hosts...] executable [args...]\n");
}

static void _help(void)
{
	slurm_ctl_conf_t *conf;

        printf (
"Usage: srun [OPTIONS...] executable [args...]\n"
"\n"
"Parallel run options:\n"
"  -n, --ntasks=ntasks         number of tasks to run\n"
"  -N, --nodes=N               number of nodes on which to run (N = min[-max])\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"      --ntasks-per-node=n     number of tasks to invoke on each node\n"
"  -i, --input=in              location of stdin redirection\n"
"  -o, --output=out            location of stdout redirection\n"
"  -e, --error=err             location of stderr redirection\n"
"  -r, --relative=n            run job step relative to node n of allocation\n"
"  -p, --partition=partition   partition requested\n"
"  -H, --hold                  submit job in held state\n"
"  -t, --time=minutes          time limit\n"
"  -D, --chdir=path            change remote current working directory\n"
"  -I, --immediate             exit if resources are not immediately available\n"
"  -O, --overcommit            overcommit resources\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -K, --kill-on-bad-exit      kill the job if any task terminates with a\n"
"                              non-zero exit code\n"
"  -s, --share                 share nodes with other jobs\n"
"  -l, --label                 prepend task number to lines of stdout/err\n"
"  -u, --unbuffered            do not line-buffer stdout/err\n"
"  -m, --distribution=type     distribution method for processes to nodes\n"
"                              (type = block|cyclic|arbitrary)\n"
"  -J, --job-name=jobname      name of job\n"
"      --jobid=id              run under already allocated job\n"
"      --mpi=type              type of MPI being used\n"
"  -b, --batch                 submit as batch job for later execution\n"
"  -T, --threads=threads       set srun launch fanout\n"
"  -W, --wait=sec              seconds to wait after first task exits\n"
"                              before killing job\n"
"  -q, --quit-on-interrupt     quit on single Ctrl-C\n"
"  -X, --disable-status        Disable Ctrl-C status feature\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -Q, --quiet                 quiet mode (suppress informational messages)\n"
"  -d, --slurmd-debug=level    slurmd debug level\n"
"      --core=type             change default corefile format type\n"
"                              (type=\"list\" to list of valid formats)\n"
"  -P, --dependency=jobid      defer job until specified jobid completes\n"
"      --nice[=value]          decrease secheduling priority by value\n"
"  -U, --account=name          charge job to specified account\n"
"      --comment=name          arbitrary comment\n"
"      --propagate[=rlimits]   propagate all [or specific list of] rlimits\n"
"      --mpi=type              specifies version of MPI to use\n"
"      --prolog=program        run \"program\" before launching job step\n"
"      --epilog=program        run \"program\" after launching job step\n"
"      --task-prolog=program   run \"program\" before launching task\n"
"      --task-epilog=program   run \"program\" after launching task\n"
"      --begin=time            defer job until HH:MM DD/MM/YY\n"
"      --mail-type=type        notify on state change: BEGIN, END, FAIL or ALL\n"
"      --mail-user=user        who to send email notification for job state changes\n"
"      --ctrl-comm-ifhn=addr   interface hostname for PMI commaunications from srun\n"
"      --multi-prog            if set the program name specified is the\n"
"                              configuration specificaiton for multiple programs\n"
"      --no-requeue            if set, do not permit the job to be requeued\n"
"\n"
"Allocate only:\n"
"  -A, --allocate              allocate resources and spawn a shell\n"
"      --no-shell              don't spawn shell in allocate mode\n"
"\n"
"Attach to running job:\n"
"  -a, --attach=jobid          attach to running job with specified id\n"
"  -j, --join                  when used with --attach, allow forwarding of\n"
"                              signals and stdin.\n"
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
"  -Z, --no-allocate           don't allocate nodes (must supply -w)\n"
"\n"
"Consumable resources related options:\n" 
"      --exclusive             allocate nodes in exclusive mode when\n" 
"                              cpu consumable resource is enabled\n"
"      --job-mem=MB            maximum amount of real memory per node\n"
"                              required by the job.\n" 
"                              --mem >= --job-mem if --mem is specified.\n" 
"\n"
"Affinity/Multi-core options: (when the task/affinity plugin is enabled)\n" 
"  -B --extra-node-info=S[:C[:T]]            Expands to:\n"
"      --sockets-per-node=S      number of sockets per node to allocate\n"
"      --cores-per-socket=C      number of cores per socket to allocate\n"
"      --threads-per-core=T      number of threads per core to allocate\n"
"                              each field can be 'min[-max]' or wildcard '*'\n"
"                                total cpus requested = (N x S x C x T)\n"
"\n"
"      --ntasks-per-socket=n   number of tasks to invoke on each socket\n"
"      --ntasks-per-core=n     number of tasks to invoke on each core\n"
"\n");
	conf = slurm_conf_lock();
	if (conf->task_plugin != NULL
	    && strcasecmp(conf->task_plugin, "task/affinity") == 0) {
		printf(
"      --hint=                 Bind tasks according to application hints\n"
"                              (see \"--hint=help\" for options)\n"
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
		"\n"
#endif
		"Help options:\n"
		"      --help                  show this help message\n"
		"      --usage                 display brief usage message\n"
		"      --print-request         Display job's layout without scheduling it\n"
		"\n"
		"Other options:\n"
		"  -V, --version               output version information and exit\n"
		"\n"
		);

}
