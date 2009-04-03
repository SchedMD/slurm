/*****************************************************************************\
 *  opt.c - options processing for srun
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <fcntl.h>
#include <stdarg.h>		/* va_start   */
#include <stdio.h>
#include <stdlib.h>		/* getenv     */
#include <pwd.h>		/* getpwuid   */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/mpi.h"
#include "src/common/optz.h"
#include "src/common/parse_time.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/api/pmi_server.h"

#include "src/srun/multi_prog.h"
#include "src/srun/opt.h"
#include "src/srun/debugger.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_DISTRIB     0x04
#define OPT_NODES       0x05
#define OPT_OVERCOMMIT  0x06
#define OPT_CORE        0x07
#define OPT_CONN_TYPE	0x08
#define OPT_RESV_PORTS	0x09
#define OPT_NO_ROTATE	0x0a
#define OPT_GEOMETRY	0x0b
#define OPT_MPI         0x0c
#define OPT_CPU_BIND    0x0d
#define OPT_MEM_BIND    0x0e
#define OPT_MULTI       0x0f
#define OPT_NSOCKETS    0x10
#define OPT_NCORES      0x11
#define OPT_NTHREADS    0x12
#define OPT_EXCLUSIVE   0x13
#define OPT_OPEN_MODE   0x14
#define OPT_ACCTG_FREQ  0x15
#define OPT_WCKEY       0x16

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
#define LONG_OPT_RESV_PORTS  0x10d
#define LONG_OPT_CORE        0x10e
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
#define LONG_OPT_MULTI       0x122
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
#define LONG_OPT_MEM_PER_CPU     0x13a
#define LONG_OPT_HINT	         0x13b
#define LONG_OPT_BLRTS_IMAGE     0x140
#define LONG_OPT_LINUX_IMAGE     0x141
#define LONG_OPT_MLOADER_IMAGE   0x142
#define LONG_OPT_RAMDISK_IMAGE   0x143
#define LONG_OPT_REBOOT          0x144
#define LONG_OPT_GET_USER_ENV    0x145
#define LONG_OPT_PTY             0x146
#define LONG_OPT_CHECKPOINT      0x147
#define LONG_OPT_CHECKPOINT_DIR  0x148
#define LONG_OPT_OPEN_MODE       0x149
#define LONG_OPT_ACCTG_FREQ      0x14a
#define LONG_OPT_WCKEY           0x14b
#define LONG_OPT_RESERVATION     0x14c
#define LONG_OPT_RESTART_DIR     0x14d

/*---- global variables, defined in opt.h ----*/
int _verbose;
opt_t opt;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;


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

static void _process_env_var(env_vars_t *e, const char *val);

static bool  _under_parallel_debugger(void);
static void  _usage(void);
static bool  _valid_node_list(char **node_list_pptr);

/*---[ end forward declarations of static functions ]---------------------*/

int initialize_and_process_args(int argc, char *argv[])
{
	/* initialize option defaults */
	_opt_default();

	/* initialize options with env vars */
	_opt_env();

	/* initialize options with argv */
	_opt_args(argc, argv);

	if (!_opt_verify())
		exit(1);

	if (_verbose > 3)
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
	char *nodelist = NULL;
	
	if (strchr(*node_list_pptr, '/') == NULL)
		return true;	/* not a file name */

	/* If we are using Arbitrary and we specified the number of
	   procs to use then we need exactly this many since we are
	   saying, lay it out this way!  Same for max and min nodes.  
	   Other than that just read in as many in the hostfile */
	if(opt.distribution == SLURM_DIST_ARBITRARY) {
		if(opt.nprocs_set) 
			nodelist = slurm_read_hostfile(*node_list_pptr,
						       opt.nprocs);
		else if(opt.max_nodes)
			nodelist = slurm_read_hostfile(*node_list_pptr,
						       opt.max_nodes);
		else if(opt.min_nodes)
			nodelist = slurm_read_hostfile(*node_list_pptr,
						       opt.min_nodes);
	 } else
		nodelist = slurm_read_hostfile(*node_list_pptr, NO_VAL);
		
	if (nodelist == NULL) 
		return false;
	xfree(*node_list_pptr);
	*node_list_pptr = xstrdup(nodelist);
	free(nodelist);
	return true;
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

	if ((pw = getpwuid(getuid())) != NULL) {
		strncpy(opt.user, pw->pw_name, MAX_USERNAME);
		opt.uid = pw->pw_uid;
	} else
		error("who are you?");

	opt.gid = getgid();

	if ((getcwd(buf, MAXPATHLEN)) == NULL) 
		fatal("getcwd failed: %m");
	opt.cwd = xstrdup(buf);
	opt.cwd_set = false;

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
	opt.time_limit_str = NULL;
	opt.ckpt_interval = 0;
	opt.ckpt_interval_str = NULL;
	opt.ckpt_dir = NULL;
	opt.restart_dir = NULL;
	opt.partition = NULL;
	opt.max_threads = MAX_THREADS;
	pmi_server_max_threads(opt.max_threads);

	opt.relative = NO_VAL;
	opt.relative_set = false;
	opt.resv_port_cnt = NO_VAL;
	opt.cmd_name = NULL;
	opt.job_name = NULL;
	opt.job_name_set_cmd = false;
	opt.job_name_set_env = false;
	opt.jobid    = NO_VAL;
	opt.jobid_set = false;
	opt.dependency = NULL;
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
	opt.shared = (uint16_t)NO_VAL;
	opt.exclusive = false;
	opt.no_kill = false;
	opt.kill_bad_exit = false;

	opt.immediate	= false;

	opt.join	= false;
	opt.max_wait	= slurm_get_wait_time();

	opt.quit_on_intr = false;
	opt.disable_status = false;
	opt.test_only   = false;
	opt.preserve_env = false;

	opt.quiet = 0;
	_verbose = 0;
	opt.slurmd_debug = LOG_LEVEL_QUIET;

	opt.job_min_cpus    = NO_VAL;
	opt.job_min_sockets = NO_VAL;
	opt.job_min_cores   = NO_VAL;
	opt.job_min_threads = NO_VAL;
	opt.job_min_memory  = NO_VAL;
	opt.mem_per_cpu     = NO_VAL;
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

	/*
	 * Reset some default values if running under a parallel debugger
	 */
	if ((opt.parallel_debug = _under_parallel_debugger())) {
		opt.max_launch_time = 120;
		opt.max_threads     = 1;
		pmi_server_max_threads(opt.max_threads);
		opt.msg_timeout     = 15;
	}
	
	opt.pty = false;
	opt.open_mode = 0;
	opt.acctg_freq = -1;
	opt.reservation = NULL;
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
{"SLURM_ACCOUNT",       OPT_STRING,     &opt.account,       NULL             },
{"SLURMD_DEBUG",        OPT_INT,        &opt.slurmd_debug,  NULL             },
{"SLURM_BLRTS_IMAGE",   OPT_STRING,     &opt.blrtsimage,    NULL             },
{"SLURM_CPUS_PER_TASK", OPT_INT,        &opt.cpus_per_task, &opt.cpus_set    },
{"SLURM_CONN_TYPE",     OPT_CONN_TYPE,  NULL,               NULL             },
{"SLURM_CORE_FORMAT",   OPT_CORE,       NULL,               NULL             },
{"SLURM_CPU_BIND",      OPT_CPU_BIND,   NULL,               NULL             },
{"SLURM_MEM_BIND",      OPT_MEM_BIND,   NULL,               NULL             },
{"SLURM_DEPENDENCY",    OPT_STRING,     &opt.dependency,    NULL             },
{"SLURM_DISTRIBUTION",  OPT_DISTRIB,    NULL,               NULL             },
{"SLURM_GEOMETRY",      OPT_GEOMETRY,   NULL,               NULL             },
{"SLURM_IMMEDIATE",     OPT_INT,        &opt.immediate,     NULL             },
{"SLURM_JOB_NAME",      OPT_STRING,     &opt.job_name,      
					&opt.job_name_set_env},
{"SLURM_JOBID",         OPT_INT,        &opt.jobid,         NULL             },
{"SLURM_KILL_BAD_EXIT", OPT_INT,        &opt.kill_bad_exit, NULL             },
{"SLURM_LABELIO",       OPT_INT,        &opt.labelio,       NULL             },
{"SLURM_LINUX_IMAGE",   OPT_STRING,     &opt.linuximage,    NULL             },
{"SLURM_CNLOAD_IMAGE",  OPT_STRING,     &opt.linuximage,    NULL             },
{"SLURM_MLOADER_IMAGE", OPT_STRING,     &opt.mloaderimage,  NULL             },
{"SLURM_NNODES",        OPT_NODES,      NULL,               NULL             },
{"SLURM_NSOCKETS_PER_NODE",OPT_NSOCKETS,NULL,               NULL             },
{"SLURM_NCORES_PER_SOCKET",OPT_NCORES,  NULL,               NULL             },
{"SLURM_NTHREADS_PER_CORE",OPT_NTHREADS,NULL,               NULL             },
{"SLURM_NO_ROTATE",     OPT_NO_ROTATE,  NULL,               NULL             },
{"SLURM_NPROCS",        OPT_INT,        &opt.nprocs,        &opt.nprocs_set  },
{"SLURM_OVERCOMMIT",    OPT_OVERCOMMIT, NULL,               NULL             },
{"SLURM_PARTITION",     OPT_STRING,     &opt.partition,     NULL             },
{"SLURM_RAMDISK_IMAGE", OPT_STRING,     &opt.ramdiskimage,  NULL             },
{"SLURM_IOLOAD_IMAGE",  OPT_STRING,     &opt.ramdiskimage,  NULL             },
{"SLURM_REMOTE_CWD",    OPT_STRING,     &opt.cwd,           NULL             },
{"SLURM_RESV_PORTS",    OPT_RESV_PORTS, NULL,               NULL             },
{"SLURM_STDERRMODE",    OPT_STRING,     &opt.efname,        NULL             },
{"SLURM_STDINMODE",     OPT_STRING,     &opt.ifname,        NULL             },
{"SLURM_STDOUTMODE",    OPT_STRING,     &opt.ofname,        NULL             },
{"SLURM_THREADS",       OPT_INT,        &opt.max_threads,   NULL             },
{"SLURM_TIMELIMIT",     OPT_STRING,     &opt.time_limit_str,NULL             },
{"SLURM_CHECKPOINT",    OPT_STRING,     &opt.ckpt_interval_str, NULL         },
{"SLURM_CHECKPOINT_DIR",OPT_STRING,     &opt.ckpt_dir,      NULL             },
{"SLURM_RESTART_DIR",   OPT_STRING,     &opt.restart_dir ,  NULL             },
{"SLURM_WAIT",          OPT_INT,        &opt.max_wait,      NULL             },
{"SLURM_DISABLE_STATUS",OPT_INT,        &opt.disable_status,NULL             },
{"SLURM_MPI_TYPE",      OPT_MPI,        NULL,               NULL             },
{"SLURM_SRUN_MULTI",    OPT_MULTI,      NULL,               NULL             },
{"SLURM_UNBUFFEREDIO",  OPT_INT,        &opt.unbuffered,    NULL             },
{"SLURM_NODELIST",      OPT_STRING,     &opt.alloc_nodelist,NULL             },
{"SLURM_PROLOG",        OPT_STRING,     &opt.prolog,        NULL             },
{"SLURM_EPILOG",        OPT_STRING,     &opt.epilog,        NULL             },
{"SLURM_TASK_PROLOG",   OPT_STRING,     &opt.task_prolog,   NULL             },
{"SLURM_TASK_EPILOG",   OPT_STRING,     &opt.task_epilog,   NULL             },
{"SLURM_WORKING_DIR",   OPT_STRING,     &opt.cwd,           &opt.cwd_set     },
{"SLURM_EXCLUSIVE",     OPT_EXCLUSIVE,  NULL,               NULL             },
{"SLURM_OPEN_MODE",     OPT_OPEN_MODE,  NULL,               NULL             },
{"SLURM_ACCTG_FREQ",    OPT_INT,        &opt.acctg_freq,    NULL             },
{"SLURM_NETWORK",       OPT_STRING,     &opt.network,       NULL             },
{"SLURM_WCKEY",         OPT_STRING,     &opt.wckey,         NULL             },
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
			if (!(end && *end == '\0')) {
				error("%s=%s invalid. ignoring...", 
				      e->var, val);
			}
		}
		break;

	case OPT_DISTRIB:
		if (strcmp(val, "unknown") == 0)
			break;	/* ignore it, passed from salloc */
		dt = verify_dist_type(val, &opt.plane_size);
		if (dt == SLURM_DIST_UNKNOWN) {
			error("\"%s=%s\" -- invalid distribution type. " 
			      "ignoring...", e->var, val);
		} else 
			opt.distribution = dt;
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

	case OPT_NODES:
		opt.nodes_set = get_resource_arg_range( val ,"OPT_NODES", 
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

	case OPT_EXCLUSIVE:
		opt.exclusive = true;
		opt.shared = 0;
		break;

	case OPT_RESV_PORTS:
		if (val)
			opt.resv_port_cnt = strtol(val, NULL, 10);
		else
			opt.resv_port_cnt = 0;
		break;

	case OPT_OPEN_MODE:
		if ((val[0] == 'a') || (val[0] == 'A'))
			opt.open_mode = OPEN_MODE_APPEND;
		else if ((val[0] == 't') || (val[0] == 'T'))
			opt.open_mode = OPEN_MODE_TRUNCATE;
		else
			error("Invalid SLURM_OPEN_MODE: %s. Ignored", val);
		break;

	case OPT_CORE:
		opt.core_type = core_format_type (val);
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

static void set_options(const int argc, char **argv)
{
	int opt_char, option_index = 0;
	struct utsname name;
	static struct option long_options[] = {
		{"attach",        no_argument,       0, 'a'},
		{"allocate",      no_argument,       0, 'A'},
		{"batch",         no_argument,       0, 'b'},
		{"extra-node-info", required_argument, 0, 'B'},
		{"cpus-per-task", required_argument, 0, 'c'},
		{"constraint",    required_argument, 0, 'C'},
		{"slurmd-debug",  required_argument, 0, 'd'},
		{"chdir",         required_argument, 0, 'D'},
		{"error",         required_argument, 0, 'e'},
		{"preserve-env",  no_argument,       0, 'E'},
		{"preserve-slurm-env", no_argument,  0, 'E'},
		{"geometry",      required_argument, 0, 'g'},
		{"hold",          no_argument,       0, 'H'},
		{"input",         required_argument, 0, 'i'},
		{"immediate",     no_argument,       0, 'I'},
		{"join",          no_argument,       0, 'j'},
		{"job-name",      required_argument, 0, 'J'},
		{"no-kill",       no_argument,       0, 'k'},
		{"kill-on-bad-exit", no_argument,    0, 'K'},
		{"label",         no_argument,       0, 'l'},
		{"licenses",      required_argument, 0, 'L'},
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
		{"mem-per-cpu",      required_argument, 0, LONG_OPT_MEM_PER_CPU},
		{"hint",             required_argument, 0, LONG_OPT_HINT},
		{"mpi",              required_argument, 0, LONG_OPT_MPI},
		{"resv-ports",       optional_argument, 0, LONG_OPT_RESV_PORTS},
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
		{"multi-prog",       no_argument,       0, LONG_OPT_MULTI},
		{"comment",          required_argument, 0, LONG_OPT_COMMENT},
		{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
		{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
		{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
		{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
		{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
		{"ntasks-per-core",  required_argument, 0, LONG_OPT_NTASKSPERCORE},
		{"tasks-per-node",   required_argument, 0, LONG_OPT_NTASKSPERNODE},
		{"blrts-image",      required_argument, 0, LONG_OPT_BLRTS_IMAGE},
		{"linux-image",      required_argument, 0, LONG_OPT_LINUX_IMAGE},
		{"cnload-image",     required_argument, 0, LONG_OPT_LINUX_IMAGE},
		{"mloader-image",    required_argument, 0, LONG_OPT_MLOADER_IMAGE},
		{"ramdisk-image",    required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
		{"ioload-image",     required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
		{"reboot",           no_argument,       0, LONG_OPT_REBOOT},            
		{"get-user-env",     optional_argument, 0, LONG_OPT_GET_USER_ENV},
		{"pty",              no_argument,       0, LONG_OPT_PTY},
		{"checkpoint",       required_argument, 0, LONG_OPT_CHECKPOINT},
		{"checkpoint-dir",   required_argument, 0, LONG_OPT_CHECKPOINT_DIR},
		{"open-mode",        required_argument, 0, LONG_OPT_OPEN_MODE},
		{"acctg-freq",       required_argument, 0, LONG_OPT_ACCTG_FREQ},
		{"wckey",            required_argument, 0, LONG_OPT_WCKEY},
		{"reservation",      required_argument, 0, LONG_OPT_RESERVATION},
		{"restart-dir",      required_argument, 0, LONG_OPT_RESTART_DIR},
		{NULL,               0,                 0, 0}
	};
	char *opt_string = "+aAbB:c:C:d:D:e:Eg:Hi:IjJ:kKlL:m:n:N:"
		"o:Op:P:qQr:R:st:T:uU:vVw:W:x:XZ";

	struct option *optz = spank_option_table_create (long_options);

	if (!optz) {
		error ("Unable to create option table");
		exit (1);
	}

	if(opt.progname == NULL)
		opt.progname = xbasename(argv[0]);
	else
		error("opt.progname is already set.");
	optind = 0;		
	while((opt_char = getopt_long(argc, argv, opt_string,
				      optz, &option_index)) != -1) {
		switch (opt_char) {

		case (int)'?':
			fprintf(stderr,
				"Try \"srun --help\" for more information\n");
			exit(1);
			break;
		case (int)'a':
			error("Please use the \"sattach\" command instead of "
			      "\"srun -a/--attach\".");
			exit(1);
		case (int)'A':
			error("Please use the \"salloc\" command instead of "
			      "\"srun -A/--allocate\".");
			exit(1);
		case (int)'b':
			error("Please use the \"sbatch\" command instead of "
			      "\"srun -b/--batch\".");
			exit(1);
		case (int)'B':
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
		case (int)'c':
			opt.cpus_set = true;
			opt.cpus_per_task = 
				_get_int(optarg, "cpus-per-task", false);
			break;
		case (int)'C':
			xfree(opt.constraints);
			opt.constraints = xstrdup(optarg);
			break;
		case (int)'d':
			opt.slurmd_debug = 
				_get_int(optarg, "slurmd-debug", false);
			break;
		case (int)'D':
			opt.cwd_set = true;
			xfree(opt.cwd);
			opt.cwd = xstrdup(optarg);
			break;
		case (int)'e':
			if (opt.pty)
				fatal("--error incompatable with --pty option");
			xfree(opt.efname);
			if (strncasecmp(optarg, "none", (size_t) 4) == 0)
				opt.efname = xstrdup("/dev/null");
			else
				opt.efname = xstrdup(optarg);
			break;
		case (int)'E':
			opt.preserve_env = true;
			break;
		case (int)'g':
			if (verify_geometry(optarg, opt.geometry))
				exit(1);
			break;
		case (int)'H':
			opt.hold = true;
			break;
		case (int)'i':
			if (opt.pty)
				fatal("--input incompatable with --pty option");
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
			opt.job_name_set_cmd = true;
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
		case 'L':
			xfree(opt.licenses);
			opt.licenses = xstrdup(optarg);
			break;
		case (int)'m':
			opt.distribution = verify_dist_type(optarg, 
							     &opt.plane_size);
			if (opt.distribution == SLURM_DIST_UNKNOWN) {
				error("distribution type `%s' " 
				      "is not recognized", optarg);
				exit(1);
			}
			break;
		case (int)'n':
			opt.nprocs_set = true;
			opt.nprocs = 
				_get_int(optarg, "number of tasks", true);
			break;
		case (int)'N':
			opt.nodes_set = 
				get_resource_arg_range( optarg, 
							"requested node count",
							&opt.min_nodes,
							&opt.max_nodes, true );
			
			if (opt.nodes_set == false) {
				error("invalid resource allocation -N `%s'", 
				      optarg);
				exit(1);
			}
			break;
		case (int)'o':
			if (opt.pty)
				fatal("--output incompatable with --pty option");
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
			xfree(opt.partition);
			opt.partition = xstrdup(optarg);
			break;
		case (int)'P':
			xfree(opt.dependency);
			opt.dependency = xstrdup(optarg);
			break;
		case (int)'q':
			opt.quit_on_intr = true;
			break;
		case (int) 'Q':
			opt.quiet++;
			break;
		case (int)'r':
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
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(optarg);
			break;
		case (int)'T':
			opt.max_threads = 
				_get_int(optarg, "max_threads", true);
			pmi_server_max_threads(opt.max_threads);
			break;
		case (int)'u':
			opt.unbuffered = true;
			break;
		case (int)'U':
			xfree(opt.account);
			opt.account = xstrdup(optarg);
			break;
		case (int)'v':
			_verbose++;
			break;
		case (int)'V':
			print_slurm_version();
			exit(0);
			break;
		case (int)'w':
			xfree(opt.nodelist);
			opt.nodelist = xstrdup(optarg);
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
			opt.exclusive = true;
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
			opt.job_min_memory = (int) str_to_bytes(optarg);
			if (opt.job_min_memory < 0) {
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
		case LONG_OPT_MPI:
			if (mpi_hook_client_init((char *)optarg)
			    == SLURM_ERROR) {
				fatal("\"--mpi=%s\" -- long invalid MPI type, "
				      "--mpi=list for acceptable types.",
				      optarg);
			}
			break;
		case LONG_OPT_RESV_PORTS:
			if (optarg)
				opt.resv_port_cnt = strtol(optarg, NULL, 10);
			else
				opt.resv_port_cnt = 0;
			break;
		case LONG_OPT_TMP:
			opt.job_min_tmp_disk = str_to_bytes(optarg);
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
			opt.conn_type = verify_conn_type(optarg);
			break;
		case LONG_OPT_TEST_ONLY:
			opt.test_only = true;
			break;
		case LONG_OPT_NETWORK:
			xfree(opt.network);
			opt.network = xstrdup(optarg);
			setenv("SLURM_NETWORK", opt.network, 1);
			break;
		case LONG_OPT_PROPAGATE:
			xfree(opt.propagate);
			if (optarg)
				opt.propagate = xstrdup(optarg);
			else
				opt.propagate = xstrdup("ALL");
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
		case LONG_OPT_MULTI:
			opt.multi_prog = true;
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
		case LONG_OPT_GET_USER_ENV:
			error("--get-user-env is no longer supported in srun, "
			      "use sbatch");
			break;
		case LONG_OPT_PTY:
#ifdef HAVE_PTY_H
			opt.pty = true;
			opt.unbuffered = true;	/* implicit */
			if (opt.ifname)
				fatal("--input incompatable with --pty option");
			if (opt.ofname)
				fatal("--output incompatable with --pty option");
			if (opt.efname)
				fatal("--error incompatable with --pty option");
#else
			error("--pty not currently supported on this system type");
#endif
			break;
		case LONG_OPT_CHECKPOINT:
			xfree(opt.ckpt_interval_str);
			opt.ckpt_interval_str = xstrdup(optarg);
			break;
		case LONG_OPT_OPEN_MODE:
			if ((optarg[0] == 'a') || (optarg[0] == 'A'))
				opt.open_mode = OPEN_MODE_APPEND;
			else if ((optarg[0] == 't') || (optarg[0] == 'T'))
				opt.open_mode = OPEN_MODE_TRUNCATE;
			else {
				error("Invalid --open-mode argument: %s. Ignored", 
				      optarg);
			}
			break;
		case LONG_OPT_ACCTG_FREQ:
			opt.acctg_freq = _get_int(optarg, "acctg-freq",
                                false);
			break;
		case LONG_OPT_WCKEY:
			xfree(opt.wckey);
			opt.wckey = xstrdup(optarg);
			break;
		case LONG_OPT_RESERVATION:
			xfree(opt.reservation);
			opt.reservation = xstrdup(optarg);
			break;
		case LONG_OPT_CHECKPOINT_DIR:
			xfree(opt.ckpt_dir);
			opt.ckpt_dir = xstrdup(optarg);
			break;
		case LONG_OPT_RESTART_DIR:
			xfree(opt.restart_dir);
			opt.restart_dir = xstrdup(optarg);
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

	set_options(argc, argv);

	if ((opt.job_min_memory > -1) && (opt.mem_per_cpu > -1)) {
		if (opt.job_min_memory < opt.mem_per_cpu) {
			info("mem < mem-per-cpu - resizing mem to be equal "
			     "to mem-per-cpu");
			opt.job_min_memory = opt.mem_per_cpu;
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
	if (opt.dependency)
		setenvfs("SLURM_JOB_DEPENDENCY=%s", opt.dependency);

	if (opt.nodelist && (!opt.test_only)) {
#ifdef HAVE_BG
		info("\tThe nodelist option should only be used if\n"
		     "\tthe block you are asking for can be created.\n"
		     "\tIt should also include all the midplanes you\n"
		     "\twant to use, partial lists will not work correctly.\n"
		     "\tPlease consult smap before using this option\n"
		     "\tor your job may be stuck with no way to run.");
#endif
	}

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

		if ((fullpath = search_path(opt.cwd, opt.argv[0], false, X_OK))) {
			xfree(opt.argv[0]);
			opt.argv[0] = fullpath;
		} 
	}

	if (opt.multi_prog && verify_multi_name(opt.argv[0], opt.nprocs))
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
	if (opt.slurmd_debug + LOG_LEVEL_ERROR > LOG_LEVEL_DEBUG2) {
		opt.slurmd_debug = LOG_LEVEL_DEBUG2 - LOG_LEVEL_ERROR;
		info("Using srun's max debug increment of %d", opt.slurmd_debug);
	}

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

	if (opt.argc > 0)
		opt.cmd_name = base_name(opt.argv[0]);

	if(!opt.nodelist) {
		if((opt.nodelist = xstrdup(getenv("SLURM_HOSTFILE")))) {
			/* make sure the file being read in has a / in
			   it to make sure it is a file in the
			   valid_node_list function */
			if(!strstr(opt.nodelist, "/")) {
				char *add_slash = xstrdup("./");
				xstrcat(add_slash, opt.nodelist);
				xfree(opt.nodelist);
				opt.nodelist = add_slash;
			}
			opt.distribution = SLURM_DIST_ARBITRARY;
			if (!_valid_node_list(&opt.nodelist)) {
				error("Failure getting NodeNames from "
				      "hostfile");
				exit(1);
			} else {
				debug("loaded nodes (%s) from hostfile",
				      opt.nodelist);
			}
		}
	} else {
		if (!_valid_node_list(&opt.nodelist))
			exit(1);
	}
	
	/* now if max is set make sure we have <= max_nodes in the
	 * nodelist but only if it isn't arbitrary since the user has
	 * laid it out how it should be so don't mess with it print an
	 * error later if it doesn't work the way they wanted */
	if(opt.max_nodes && opt.nodelist
	   && opt.distribution != SLURM_DIST_ARBITRARY) {
		hostlist_t hl = hostlist_create(opt.nodelist);
		int count = hostlist_count(hl);
		if(count > opt.max_nodes) {
			int i = 0;
			char buf[8192];
			count -= opt.max_nodes;
			while(i<count) {
				char *name = hostlist_pop(hl);
				if(name)
					free(name);
				else 
					break;
				i++;
			}
			hostlist_ranged_string(hl, sizeof(buf), buf);
			xfree(opt.nodelist);
			opt.nodelist = xstrdup(buf);
		}
		hostlist_destroy(hl);
	} 


	if ((opt.argc == 0) && (opt.test_only == false)) {
		error("must supply remote command");
		verified = false;
	}

	/* check for realistic arguments */
	if (opt.nprocs <= 0) {
		error("%s: invalid number of processes (-n %d)",
		      opt.progname, opt.nprocs);
		verified = false;
	}

	if (opt.cpus_per_task < 0) {
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
					   CPU_BIND_TO_THREADS |
					   CPU_BIND_TO_LDOMS))) {
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
					   CPU_BIND_TO_THREADS |
					   CPU_BIND_TO_LDOMS))) {
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

		core_format_enable (opt.core_type);
		/* massage the numbers */
		if (opt.nodelist) {
			hl = hostlist_create(opt.nodelist);
			if (!hl)
				fatal("memory allocation failure");
			if(opt.distribution == SLURM_DIST_ARBITRARY
			   && !opt.nprocs_set) {
				opt.nprocs = hostlist_count(hl);
				opt.nprocs_set = true;
			}
			hostlist_uniq(hl);
			hl_cnt = hostlist_count(hl);
			if (opt.nodes_set)
				opt.min_nodes = MAX(hl_cnt, opt.min_nodes);
			else {
				opt.min_nodes = hl_cnt;
				opt.nodes_set = true;
			}
			/* don't destroy hl here since it could be
			   used later
			*/
		}
	} else if (opt.nodes_set && opt.nprocs_set) {

		/*
		 * Make sure that the number of 
		 * max_nodes is <= number of tasks
		 */
		if (opt.nprocs < opt.max_nodes) 
			opt.max_nodes = opt.nprocs;
		
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
				hostlist_ranged_string(hl, 
						       strlen(opt.nodelist)+1, 
						       opt.nodelist);
			}
		}

	} /* else if (opt.nprocs_set && !opt.nodes_set) */

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

	if (opt.time_limit_str) {
		opt.time_limit = time_str2mins(opt.time_limit_str);
		if ((opt.time_limit < 0) && (opt.time_limit != INFINITE)) {
			error("Invalid time limit specification");
			exit(1);
		}
		if (opt.time_limit == 0)
			opt.time_limit = INFINITE;
	}

	if (opt.ckpt_interval_str) {
		opt.ckpt_interval = time_str2mins(opt.ckpt_interval_str);
		if ((opt.ckpt_interval < 0) && (opt.ckpt_interval != INFINITE)) {
			error("Invalid checkpoint interval specification");
			exit(1);
		}
	}

	if (! opt.ckpt_dir)
		opt.ckpt_dir = xstrdup(opt.cwd);

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

	 if (slurm_verify_cpu_bind(NULL, &opt.cpu_bind,
				   &opt.cpu_bind_type))
		exit(1);

	return verified;
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

	if (opt.mem_per_cpu > 0)
		xstrfmtcat(buf, "mem-per-cpu=%dM ", opt.mem_per_cpu);

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
	info("reservation    : `%s'", opt.reservation);
	info("wckey          : `%s'", opt.wckey);
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
	info("label output   : %s", tf_(opt.labelio));
	info("unbuffered IO  : %s", tf_(opt.unbuffered));
	info("overcommit     : %s", tf_(opt.overcommit));
	info("threads        : %d", opt.max_threads);
	if (opt.time_limit == INFINITE)
		info("time_limit     : INFINITE");
	else if (opt.time_limit != NO_VAL)
		info("time_limit     : %d", opt.time_limit);
	if (opt.ckpt_interval)
		info("checkpoint     : %d secs", opt.ckpt_interval);
	info("checkpoint_dir : %s", opt.ckpt_dir);
	if (opt.restart_dir)
		info("restart_dir    : %s", opt.restart_dir);
	info("wait           : %d", opt.max_wait);
	if (opt.nice)
		info("nice           : %d", opt.nice);
	info("account        : %s", opt.account);
	info("comment        : %s", opt.comment);

	info("dependency     : %s", opt.dependency);
	info("exclusive      : %s", tf_(opt.exclusive));
	if (opt.shared != (uint16_t) NO_VAL)
		info("shared         : %u", opt.shared);
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
	info("preserve_env   : %s", tf_(opt.preserve_env));
	
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
	info("mail_type      : %s", print_mail_type(opt.mail_type));
	info("mail_user      : %s", opt.mail_user);
	info("task_prolog    : %s", opt.task_prolog);
	info("task_epilog    : %s", opt.task_epilog);
	info("multi_prog     : %s", opt.multi_prog ? "yes" : "no");
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
	if (opt.resv_port_cnt != NO_VAL)
		info("resv_port_cnt     : %d", opt.resv_port_cnt);
	str = print_commandline(opt.argc, opt.argv);
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
"            [--jobid=id] [--verbose] [--slurmd_debug=#]\n"
"            [--core=type] [-T threads] [-W sec] [--checkpoint=time]\n"
"            [--checkpoint-dir=dir]  [--licenses=names]\n"
"            [--restart-dir=dir]\n"
"            [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"            [--mpi=type] [--account=name] [--dependency=type:jobid]\n"
"            [--kill-on-bad-exit] [--propagate[=rlimits] [--comment=name]\n"
"            [--cpu_bind=...] [--mem_bind=...] [--network=type]\n"
"            [--ntasks-per-node=n] [--ntasks-per-socket=n] [reservation=name]\n"
"            [--ntasks-per-core=n] [--mem-per-cpu=MB] [--preserve-env]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
"            [--geometry=XxYxZ] [--conn-type=type] [--no-rotate] [--reboot]\n"
#ifdef HAVE_BGL
"            [--blrts-image=path] [--linux-image=path]\n"
"            [--mloader-image=path] [--ramdisk-image=path]\n"
#else
"            [--cnload-image=path]\n"
"            [--mloader-image=path] [--ioload-image=path]\n"
#endif
#endif
"            [--mail-type=type] [--mail-user=user] [--nice[=value]]\n"
"            [--prolog=fname] [--epilog=fname]\n"
"            [--task-prolog=fname] [--task-epilog=fname]\n"
"            [--ctrl-comm-ifhn=addr] [--multi-prog]\n"
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
"  -P, --dependency=type:jobid defer job until condition on jobid is satisfied\n"
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
"      --mail-user=user        who to send email notification for job state\n" "                              changes\n"
"      --multi-prog            if set the program name specified is the\n"
"                              configuration specification for multiple programs\n"
"      --get-user-env          used by Moab.  See srun man page.\n"
"  -L, --licenses=names        required license, comma separated\n"
"      --checkpoint=time       job step checkpoint interval\n"
"      --checkpoint-dir=dir    directory to store job step checkpoint image \n"
"                              files\n"
"      --restart-dir=dir       directory of checkpoint image files to restart "
"                              from\n"
"  -E, --preserve-env          env vars for node and task counts override "
"                              command-line flags\n"
#ifdef HAVE_PTY_H
"      --pty                   run task zero in pseudo terminal\n"
#endif
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
"      --reservation=name      allocate resources from named reservation\n"
"\n"
"Consumable resources related options:\n" 
"      --exclusive             allocate nodes in exclusive mode when\n" 
"                              cpu consumable resource is enabled\n"
"                              or don't share CPUs for job steps\n"
"      --mem-per-cpu=MB        maximum amount of real memory per allocated\n"
"                              CPU required by the job.\n" 
"                              --mem >= --mem-per-cpu if --mem is specified.\n" 
"      --resv-ports            reserve communication ports\n" 
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

	printf("\n"
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
#ifndef HAVE_BGL
"                              If wanting to run in HTC mode (only for 1\n"
"                              midplane and below).  You can use HTC_S for\n"
"                              SMP, HTC_D for Dual, HTC_V for\n"
"                              virtual node mode, and HTC_L for Linux mode.\n" 
"      --cnload-image=path     path to compute node image for bluegene block.  Default if not set\n"
"      --mloader-image=path    path to mloader image for bluegene block.  Default if not set\n"
"      --ioload-image=path     path to ioload image for bluegene block.  Default if not set\n"
#else
"      --blrts-image=path      path to blrts image for bluegene block.  Default if not set\n"
"      --linux-image=path      path to linux image for bluegene block.  Default if not set\n"
"      --mloader-image=path    path to mloader image for bluegene block.  Default if not set\n"
"      --ramdisk-image=path    path to ramdisk image for bluegene block.  Default if not set\n"
#endif
#endif
"\n"
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
