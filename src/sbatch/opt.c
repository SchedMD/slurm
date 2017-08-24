/*****************************************************************************\
 *  opt.c - options processing for sbatch
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2015 SchedMD LLC <https://www.schedmd.com>
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "config.h"

#define _GNU_SOURCE

#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>		/* va_start   */
#include <stdio.h>
#include <stdlib.h>		/* getenv     */
#include <string.h>		/* strcpy, strncasecmp */
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "src/common/cpu_frequency.h"
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

#include "src/sbatch/opt.h"

enum wrappers {
	WRPR_START,
	WRPR_BSUB,
	WRPR_PBS,
	WRPR_CNT
};

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
#define OPT_THREAD_SPEC 0x12
#define OPT_MEM_BIND    0x13
#define OPT_WCKEY       0x14
#define OPT_SIGNAL      0x15
#define OPT_GET_USER_ENV  0x16
#define OPT_EXPORT        0x17
#define OPT_GRES_FLAGS    0x18
#define OPT_TIME_VAL      0x19
#define OPT_CORE_SPEC     0x1a
#define OPT_CPU_FREQ      0x1b
#define OPT_POWER         0x1d
#define OPT_SPREAD_JOB    0x1e
#define OPT_ARRAY_INX     0x20
#define OPT_PROFILE       0x21
#define OPT_HINT	  0x22
#define OPT_DELAY_BOOT	  0x23
#define OPT_INT64	  0x24
#define OPT_USE_MIN_NODES 0x25

/* generic getopt_long flags, integers and *not* valid characters */
#define LONG_OPT_PROPAGATE   0x100
#define LONG_OPT_MEM_BIND    0x102
#define LONG_OPT_POWER       0x103
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
#define LONG_OPT_QOS             0x127
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
#define LONG_OPT_WCKEY           0x149
#define LONG_OPT_RESERVATION     0x14a
#define LONG_OPT_CHECKPOINT      0x14b
#define LONG_OPT_CHECKPOINT_DIR  0x14c
#define LONG_OPT_SIGNAL          0x14d
#define LONG_OPT_TIME_MIN        0x14e
#define LONG_OPT_GRES            0x14f
#define LONG_OPT_WAIT_ALL_NODES  0x150
#define LONG_OPT_EXPORT          0x151
#define LONG_OPT_REQ_SWITCH      0x152
#define LONG_OPT_EXPORT_FILE     0x153
#define LONG_OPT_PROFILE         0x154
#define LONG_OPT_IGNORE_PBS      0x155
#define LONG_OPT_TEST_ONLY       0x156
#define LONG_OPT_PARSABLE        0x157
#define LONG_OPT_CPU_FREQ        0x158
#define LONG_OPT_THREAD_SPEC     0x159
#define LONG_OPT_GRES_FLAGS      0x15a
#define LONG_OPT_PRIORITY        0x160
#define LONG_OPT_KILL_INV_DEP    0x161
#define LONG_OPT_SPREAD_JOB      0x162
#define LONG_OPT_USE_MIN_NODES   0x163
#define LONG_OPT_MCS_LABEL       0x165
#define LONG_OPT_DEADLINE        0x166
#define LONG_OPT_BURST_BUFFER_FILE 0x167
#define LONG_OPT_DELAY_BOOT      0x168

/*---- global variables, defined in opt.h ----*/
opt_t opt;
int error_exit = 1;
int ignore_pbs = 0;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

static void  _help(void);

/* fill in default options  */
static void _opt_default(void);

/* set options from batch script */
static void _opt_batch_script(const char *file, const void *body, int size);

/* set options from pbs batch script */
static bool _opt_wrpr_batch_script(const char *file, const void *body, int size,
				   int argc, char **argv, int magic);

/* Wrapper functions */
static void _set_pbs_options(int argc, char **argv);
static void _set_bsub_options(int argc, char **argv);

/* set options based upon env vars  */
static void _opt_env(void);
static void _proc_get_user_env(char *val);

/* list known options and their settings  */
static void  _opt_list(void);

/* verify options sanity  */
static bool _opt_verify(void);

static void _process_env_var(env_vars_t *e, const char *val);

static uint16_t _parse_pbs_mail_type(const char *arg);

static void  _usage(void);
static void _fullpath(char **filename, const char *cwd);
static char *_read_file(char *fname);
static void _set_options(int argc, char **argv);
static void _parse_pbs_resource_list(char *rl);

/*---[ end forward declarations of static functions ]---------------------*/

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
		opt.progname ? opt.progname : "sbatch", buf);
	va_end(ap);
}
#else
#  define argerror error
#endif				/* USE_ARGERROR */

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
 * _opt_default(): used by initialize_and_process_args to set defaults
 */
static void _opt_default(void)
{
	char buf[MAXPATHLEN + 1];
	int i;
	uid_t uid = getuid();

	opt.user = uid_to_string(uid);
	if (xstrcmp(opt.user, "nobody") == 0)
		fatal("Invalid user id: %u", uid);

	opt.script_argc = 0;
	opt.script_argv = NULL;

	opt.uid = uid;
	opt.gid = getgid();

	if ((getcwd(buf, MAXPATHLEN)) == NULL) {
		error("getcwd failed: %m");
		exit(error_exit);
	}
	opt.cwd = xstrdup(buf);

	opt.clusters = NULL;
	opt.progname = NULL;

	opt.ntasks = 1;
	opt.ntasks_set = false;
	opt.cpu_freq_min = NO_VAL;
	opt.cpu_freq_max = NO_VAL;
	opt.cpu_freq_gov = NO_VAL;
	opt.cpus_per_task = 0;
	opt.cpus_set = false;
	opt.hint_env = NULL;
	opt.hint_set = false;
	opt.min_nodes = 1;
	opt.max_nodes = 0;
	opt.nodes_set = false;
	opt.sockets_per_node = NO_VAL; /* requested sockets */
	opt.cores_per_socket = NO_VAL; /* requested cores */
	opt.threads_per_core = NO_VAL; /* requested threads */
	opt.threads_per_core_set = false;
	opt.ntasks_per_node      = 0;  /* ntask max limits */
	opt.ntasks_per_socket    = NO_VAL;
	opt.ntasks_per_core      = NO_VAL;
	opt.ntasks_per_core_set  = false;
	opt.mem_bind_type = 0;
	opt.mem_bind = NULL;
	opt.core_spec = (uint16_t) NO_VAL;
	opt.time_limit = NO_VAL;
	opt.time_min = NO_VAL;
	opt.partition = NULL;
	opt.power_flags = 0;

	opt.job_name = NULL;
	opt.jobid    = NO_VAL;
	opt.jobid_set = false;
	opt.dependency = NULL;
	opt.account  = NULL;
	opt.comment  = NULL;
	opt.qos      = NULL;

	opt.distribution = SLURM_DIST_UNKNOWN;
	opt.plane_size   = NO_VAL;

	opt.shared = (uint16_t)NO_VAL;
	opt.no_kill = false;

	opt.immediate	= false;
	opt.requeue	= NO_VAL;
	opt.overcommit	= false;

	opt.quiet = 0;
	opt.verbose = 0;
	opt.warn_flags = 0;
	opt.warn_signal = 0;
	opt.warn_time   = 0;
	opt.wait_all_nodes = (uint16_t) NO_VAL;

	/* constraint default (-1 is no constraint) */
	opt.mincpus	    = -1;
	opt.mem_per_cpu	    = -1;
	opt.realmem	    = -1;
	opt.tmpdisk	    = -1;

	opt.hold	    = false;
	opt.parsable	    = false;
	opt.constraints	    = NULL;
	opt.gres	    = NULL;
	opt.contiguous	    = false;
	opt.nodelist	    = NULL;
	opt.exc_nodes	    = NULL;

	for (i = 0; i < HIGHEST_DIMENSIONS; i++) {
		opt.conn_type[i]    = (uint16_t) NO_VAL;
		opt.geometry[i]	    = 0;
	}
	opt.reboot          = false;
	opt.no_rotate	    = false;

	opt.euid	    = (uid_t) -1;
	opt.egid	    = (gid_t) -1;

	opt.profile	    = ACCT_GATHER_PROFILE_NOT_SET;
	opt.propagate	    = NULL;  /* propagate specific rlimits */

	opt.ifname = xstrdup("/dev/null");
	opt.ofname = NULL;
	opt.efname = NULL;

	opt.export_env        = NULL;
	opt.export_file       = NULL;
	opt.get_user_env_time = -1;
	opt.get_user_env_mode = -1;
	opt.acctg_freq        = NULL;
	opt.reservation       = NULL;
	opt.req_switch        = -1;
	opt.umask             = -1;
	opt.wait              = false;
	opt.wait4switch       = -1;
	opt.wckey             = NULL;

	opt.ckpt_interval = 0;
	opt.ckpt_interval_str = NULL;
	opt.ckpt_dir = slurm_get_checkpoint_dir();

	opt.nice = NO_VAL;
	opt.priority = 0;

	opt.test_only   = false;
	opt.job_flags = 0;

	opt.mcs_label		= NULL;
	opt.delay_boot      = NO_VAL;
}

/* Read specified file's contents into a buffer.
 * Caller must xfree the buffer's contents */
static char *_read_file(char *fname)
{
	int fd, i, offset = 0;
	struct stat stat_buf;
	char *file_buf;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		fatal("Could not open burst buffer specification file %s: %m",
		      fname);
	}
	if (fstat(fd, &stat_buf) < 0) {
		fatal("Could not stat burst buffer specification file %s: %m",
		      fname);
	}
	file_buf = xmalloc(stat_buf.st_size);
	while (stat_buf.st_size > offset) {
		i = read(fd, file_buf + offset, stat_buf.st_size - offset);
		if (i < 0) {
			if (errno == EAGAIN)
				continue;
			fatal("Could not read burst buffer specification "
			      "file %s: %m", fname);
		}
		if (i == 0)
			break;	/* EOF */
		offset += i;
	}
	close(fd);
	return file_buf;
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
  {"SBATCH_ACCOUNT",       OPT_STRING,     &opt.account,       NULL          },
  {"SBATCH_ARRAY_INX",     OPT_STRING,     &opt.array_inx,     NULL          },
  {"SBATCH_ACCTG_FREQ",    OPT_STRING,     &opt.acctg_freq,    NULL          },
  {"SBATCH_BLRTS_IMAGE",   OPT_STRING,     &opt.blrtsimage,    NULL          },
  {"SBATCH_CHECKPOINT",    OPT_STRING,     &opt.ckpt_interval_str, NULL      },
  {"SBATCH_CHECKPOINT_DIR",OPT_STRING,     &opt.ckpt_dir,      NULL          },
  {"SBATCH_CLUSTERS",      OPT_STRING,     &opt.clusters,      NULL          },
  {"SLURM_CLUSTERS",       OPT_STRING,     &opt.clusters,      NULL          },
  {"SBATCH_CNLOAD_IMAGE",  OPT_STRING,     &opt.linuximage,    NULL          },
  {"SBATCH_CONSTRAINT",    OPT_STRING,     &opt.constraints,   NULL          },
  {"SBATCH_CONN_TYPE",     OPT_CONN_TYPE,  NULL,               NULL          },
  {"SBATCH_CORE_SPEC",     OPT_INT,        &opt.core_spec,     NULL          },
  {"SBATCH_CPU_FREQ_REQ",  OPT_CPU_FREQ,   NULL,               NULL          },
  {"SBATCH_DEBUG",         OPT_DEBUG,      NULL,               NULL          },
  {"SBATCH_DELAY_BOOT",    OPT_DELAY_BOOT, NULL,               NULL          },
  {"SBATCH_DISTRIBUTION",  OPT_DISTRIB ,   NULL,               NULL          },
  {"SBATCH_EXCLUSIVE",     OPT_EXCLUSIVE,  NULL,               NULL          },
  {"SBATCH_EXPORT",        OPT_STRING,     &opt.export_env,    NULL          },
  {"SBATCH_GEOMETRY",      OPT_GEOMETRY,   NULL,               NULL          },
  {"SBATCH_GET_USER_ENV",  OPT_GET_USER_ENV, NULL,             NULL          },
  {"SBATCH_GRES_FLAGS",    OPT_GRES_FLAGS, NULL,               NULL          },
  {"SBATCH_HINT",          OPT_HINT,       NULL,               NULL          },
  {"SLURM_HINT",           OPT_HINT,       NULL,               NULL          },
  {"SBATCH_IMMEDIATE",     OPT_BOOL,       &opt.immediate,     NULL          },
  {"SBATCH_IOLOAD_IMAGE",  OPT_STRING,     &opt.ramdiskimage,  NULL          },
  {"SBATCH_JOBID",         OPT_INT,        &opt.jobid,         NULL          },
  {"SBATCH_JOB_NAME",      OPT_STRING,     &opt.job_name,      NULL          },
  {"SBATCH_LINUX_IMAGE",   OPT_STRING,     &opt.linuximage,    NULL          },
  {"SBATCH_MEM_BIND",      OPT_MEM_BIND,   NULL,               NULL          },
  {"SBATCH_MLOADER_IMAGE", OPT_STRING,     &opt.mloaderimage,  NULL          },
  {"SBATCH_NETWORK",       OPT_STRING,     &opt.network,       NULL          },
  {"SBATCH_NO_REQUEUE",    OPT_NO_REQUEUE, NULL,               NULL          },
  {"SBATCH_NO_ROTATE",     OPT_BOOL,       &opt.no_rotate,     NULL          },
  {"SBATCH_OPEN_MODE",     OPT_OPEN_MODE,  NULL,               NULL          },
  {"SBATCH_OVERCOMMIT",    OPT_OVERCOMMIT, NULL,               NULL          },
  {"SBATCH_PARTITION",     OPT_STRING,     &opt.partition,     NULL          },
  {"SBATCH_POWER",         OPT_POWER,      NULL,               NULL          },
  {"SBATCH_PROFILE",       OPT_PROFILE,    NULL,               NULL          },
  {"SBATCH_QOS",           OPT_STRING,     &opt.qos,           NULL          },
  {"SBATCH_RAMDISK_IMAGE", OPT_STRING,     &opt.ramdiskimage,  NULL          },
  {"SBATCH_REQ_SWITCH",    OPT_INT,        &opt.req_switch,    NULL          },
  {"SBATCH_REQUEUE",       OPT_REQUEUE,    NULL,               NULL          },
  {"SBATCH_RESERVATION",   OPT_STRING,     &opt.reservation,   NULL          },
  {"SBATCH_SIGNAL",        OPT_SIGNAL,     NULL,               NULL          },
  {"SBATCH_SPREAD_JOB",    OPT_SPREAD_JOB, NULL,               NULL          },
  {"SBATCH_THREAD_SPEC",   OPT_THREAD_SPEC,NULL,               NULL          },
  {"SBATCH_TIMELIMIT",     OPT_STRING,     &opt.time_limit_str,NULL          },
  {"SBATCH_USE_MIN_NODES", OPT_USE_MIN_NODES ,NULL,            NULL          },
  {"SBATCH_WAIT",          OPT_BOOL,       &opt.wait,          NULL          },
  {"SBATCH_WAIT_ALL_NODES",OPT_INT,        &opt.wait_all_nodes,NULL          },
  {"SBATCH_WAIT4SWITCH",   OPT_TIME_VAL,   NULL,               NULL          },
  {"SBATCH_WCKEY",         OPT_STRING,     &opt.wckey,         NULL          },

  {NULL, 0, NULL, NULL}
};


/*
 * _opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend srun to process different vars
 */
static void _opt_env(void)
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
	int i;

	debug2("now processing env var %s=%s", e->var, val);

	if (e->set_flag) {
		*((bool *) e->set_flag) = true;
	}

	switch (e->type) {
	case OPT_STRING:
		*((char **) e->arg) = xstrdup(val);
		break;
	case OPT_INT:
		if (val[0] != '\0') {
			*((int *) e->arg) = (int) strtol(val, &end, 10);
			if (!(end && *end == '\0')) {
				error("%s=%s invalid. ignoring...",
				      e->var, val);
			}
		}
		break;

        case OPT_INT64:
                if (val[0] != '\0') {
                        *((int64_t *) e->arg) = (int64_t) strtoll(val, &end, 10);
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
		if (val[0] == '\0') {
			*((bool *)e->arg) = true;
		} else if (xstrcasecmp(val, "yes") == 0) {
			*((bool *)e->arg) = true;
		} else if ((strtol(val, &end, 10) != 0)
			   && end != val) {
			*((bool *)e->arg) = true;
		} else {
			*((bool *)e->arg) = false;
		}
		break;

	case OPT_ARRAY_INX:
		xfree(opt.array_inx);
		opt.array_inx = xstrdup(val);
		break;

	case OPT_DEBUG:
		if (val[0] != '\0') {
			opt.verbose = (int) strtol(val, &end, 10);
			if (!(end && *end == '\0'))
				error("%s=%s invalid", e->var, val);
		}
		break;

	case OPT_HINT:
		opt.hint_env = xstrdup(val);
		break;

	case OPT_MEM_BIND:
		if (slurm_verify_mem_bind(val, &opt.mem_bind,
					  &opt.mem_bind_type))
			exit(error_exit);
		break;

	case OPT_DISTRIB:
		opt.distribution = verify_dist_type(val,
						    &opt.plane_size);
		if (opt.distribution == SLURM_DIST_UNKNOWN)
			error("distribution type `%s' is invalid", val);
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

	case OPT_GRES_FLAGS:
		if (!xstrcasecmp(val, "enforce-binding")) {
			opt.job_flags |= GRES_ENFORCE_BIND;
		} else {
			error("Invalid SBATCH_GRES_FLAGS specification: %s",
			      val);
			exit(error_exit);
		}
		break;

	case OPT_EXCLUSIVE:
		if (val[0] == '\0') {
			opt.shared = JOB_SHARED_NONE;
		} else if (!xstrcasecmp(val, "user")) {
			opt.shared = JOB_SHARED_USER;
		} else if (!xstrcasecmp(val, "mcs")) {
			opt.shared = JOB_SHARED_MCS;
		} else {
			error("\"%s=%s\" -- invalid value, ignoring...",
			      e->var, val);
		}
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
	case OPT_SPREAD_JOB:
		opt.job_flags |= SPREAD_JOB;
		break;
	case OPT_GET_USER_ENV:
		if (val)
			_proc_get_user_env((char *)val);
		else
			opt.get_user_env_time = 0;
		break;
	case OPT_TIME_VAL:
		opt.wait4switch = time_str2secs(val);
		break;
	case OPT_PROFILE:
		opt.profile = acct_gather_profile_from_string((char *)val);
		break;
	case OPT_CPU_FREQ:
		if (cpu_freq_verify_cmdline(val, &opt.cpu_freq_min,
					    &opt.cpu_freq_max, &opt.cpu_freq_gov))
			error("Invalid --cpu-freq argument: %s. Ignored", val);
		break;
	case OPT_POWER:
		opt.power_flags = power_flags_id((char *)val);
		break;
	case OPT_THREAD_SPEC:
		opt.core_spec = parse_int("thread_spec", val, false) |
			CORE_SPEC_THREAD;
		break;
	case OPT_DELAY_BOOT:
		i = time_str2secs(val);
		if (i == NO_VAL)
			error("Invalid SBATCH_DELAY_BOOT argument: %s. Ignored",
			      val);
		else
			opt.delay_boot = (uint32_t) i;
		break;
	case OPT_USE_MIN_NODES:
		opt.job_flags |= USE_MIN_NODES;
		break;
	default:
		/* do nothing */
		break;
	}
}


/*---[ command line option processing ]-----------------------------------*/

static struct option long_options[] = {
	{"account",       required_argument, 0, 'A'},
	{"array",         required_argument, 0, 'a'},
	{"batch",         no_argument,       0, 'b'}, /* batch option
							 is only here for
							 moab translation
							 doesn't do anything */
	{"extra-node-info", required_argument, 0, 'B'},
	{"cpus-per-task", required_argument, 0, 'c'},
	{"constraint",    required_argument, 0, 'C'},
	{"dependency",    required_argument, 0, 'd'},
	{"workdir",       required_argument, 0, 'D'},
	{"error",         required_argument, 0, 'e'},
	{"nodefile",      required_argument, 0, 'F'},
	{"geometry",      required_argument, 0, 'g'},
	{"help",          no_argument,       0, 'h'},
	{"hold",          no_argument,       0, 'H'},
	{"input",         required_argument, 0, 'i'},
	{"immediate",     no_argument,       0, 'I'},
	{"job-name",      required_argument, 0, 'J'},
	{"kill-on-invalid-dep", required_argument, 0, LONG_OPT_KILL_INV_DEP},
	{"no-kill",       no_argument,       0, 'k'},
	{"licenses",      required_argument, 0, 'L'},
	{"distribution",  required_argument, 0, 'm'},
	{"cluster",       required_argument, 0, 'M'},
	{"clusters",      required_argument, 0, 'M'},
	{"tasks",         required_argument, 0, 'n'},
	{"ntasks",        required_argument, 0, 'n'},
	{"nodes",         required_argument, 0, 'N'},
	{"output",        required_argument, 0, 'o'},
	{"overcommit",    no_argument,       0, 'O'},
	{"oversubscribe", no_argument,       0, 's'},
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
	{"wait",          no_argument,       0, 'W'},
	{"exclude",       required_argument, 0, 'x'},
	{"acctg-freq",    required_argument, 0, LONG_OPT_ACCTG_FREQ},
	{"bbf",           required_argument, 0, LONG_OPT_BURST_BUFFER_FILE},
	{"begin",         required_argument, 0, LONG_OPT_BEGIN},
	{"blrts-image",   required_argument, 0, LONG_OPT_BLRTS_IMAGE},
	{"checkpoint",    required_argument, 0, LONG_OPT_CHECKPOINT},
	{"checkpoint-dir",required_argument, 0, LONG_OPT_CHECKPOINT_DIR},
	{"cnload-image",  required_argument, 0, LONG_OPT_LINUX_IMAGE},
	{"comment",       required_argument, 0, LONG_OPT_COMMENT},
	{"conn-type",     required_argument, 0, LONG_OPT_CONNTYPE},
	{"contiguous",    no_argument,       0, LONG_OPT_CONT},
	{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
	{"cpu-freq",         required_argument, 0, LONG_OPT_CPU_FREQ},
	{"deadline",      required_argument, 0, LONG_OPT_DEADLINE},
	{"delay-boot",    required_argument, 0, LONG_OPT_DELAY_BOOT},
	{"exclusive",     optional_argument, 0, LONG_OPT_EXCLUSIVE},
	{"export",        required_argument, 0, LONG_OPT_EXPORT},
	{"export-file",   required_argument, 0, LONG_OPT_EXPORT_FILE},
	{"get-user-env",  optional_argument, 0, LONG_OPT_GET_USER_ENV},
	{"gres",          required_argument, 0, LONG_OPT_GRES},
	{"gres-flags",    required_argument, 0, LONG_OPT_GRES_FLAGS},
	{"gid",           required_argument, 0, LONG_OPT_GID},
	{"hint",          required_argument, 0, LONG_OPT_HINT},
	{"ignore-pbs",    no_argument,       0, LONG_OPT_IGNORE_PBS},
	{"ioload-image",  required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
	{"jobid",         required_argument, 0, LONG_OPT_JOBID},
	{"linux-image",   required_argument, 0, LONG_OPT_LINUX_IMAGE},
	{"mail-type",     required_argument, 0, LONG_OPT_MAIL_TYPE},
	{"mail-user",     required_argument, 0, LONG_OPT_MAIL_USER},
	{"mcs-label",     required_argument, 0, LONG_OPT_MCS_LABEL},
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
	{"no-requeue",    no_argument,       0, LONG_OPT_NO_REQUEUE},
	{"ntasks-per-core",  required_argument, 0, LONG_OPT_NTASKSPERCORE},
	{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
	{"open-mode",     required_argument, 0, LONG_OPT_OPEN_MODE},
	{"parsable",      optional_argument, 0, LONG_OPT_PARSABLE},
	{"power",         required_argument, 0, LONG_OPT_POWER},
	{"propagate",     optional_argument, 0, LONG_OPT_PROPAGATE},
	{"profile",       required_argument, 0, LONG_OPT_PROFILE},
	{"priority",      required_argument, 0, LONG_OPT_PRIORITY},
	{"qos",		  required_argument, 0, LONG_OPT_QOS},
	{"ramdisk-image", required_argument, 0, LONG_OPT_RAMDISK_IMAGE},
	{"reboot",        no_argument,       0, LONG_OPT_REBOOT},
	{"requeue",       no_argument,       0, LONG_OPT_REQUEUE},
	{"reservation",   required_argument, 0, LONG_OPT_RESERVATION},
	{"signal",        required_argument, 0, LONG_OPT_SIGNAL},
	{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
	{"spread-job",    no_argument,       0, LONG_OPT_SPREAD_JOB},
	{"switches",      required_argument, 0, LONG_OPT_REQ_SWITCH},
	{"tasks-per-node",required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"test-only",     no_argument,       0, LONG_OPT_TEST_ONLY},
	{"thread-spec",   required_argument, 0, LONG_OPT_THREAD_SPEC},
	{"time-min",      required_argument, 0, LONG_OPT_TIME_MIN},
	{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
	{"tmp",           required_argument, 0, LONG_OPT_TMP},
	{"uid",           required_argument, 0, LONG_OPT_UID},
	{"use-min-nodes", no_argument,       0, LONG_OPT_USE_MIN_NODES},
	{"wait-all-nodes",required_argument, 0, LONG_OPT_WAIT_ALL_NODES},
	{"wckey",         required_argument, 0, LONG_OPT_WCKEY},
	{"wrap",          required_argument, 0, LONG_OPT_WRAP},
	{NULL,            0,                 0, 0}
};

static char *opt_string =
	"+ba:A:B:c:C:d:D:e:F:g:hHi:IJ:kL:m:M:n:N:o:Op:P:QRsS:t:uU:vVw:Wx:";
char *pos_delimit;


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

	struct option *optz = spank_option_table_create(long_options);

	if (!optz) {
		error("Unable to create options table");
		exit(error_exit);
	}

	/* initialize option defaults */
	_opt_default();

	opt.progname = xbasename(argv[0]);
	optind = 0;

	while ((opt_char = getopt_long(argc, argv, opt_string,
				       optz, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			fprintf(stderr, "Try \"sbatch --help\" for more "
				"information\n");
			exit(error_exit);
			break;
		case 'h':
			_help();
			exit(0);
			break;
		case 'Q':
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
			opt.job_name = xstrdup("wrap");
			break;
		default:
			/* will be parsed in second pass function */
			break;
		}
	}
	xfree(str);
	spank_option_table_destroy(optz);

	if (argc > optind && opt.wrap != NULL) {
		error("Script arguments are not permitted with the"
		      " --wrap option.");
		exit(error_exit);
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

		if ((fullpath = search_path(opt.cwd, cmd, true, mode, false))) {
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
int process_options_second_pass(int argc, char **argv, const char *file,
				const void *script_body, int script_size)
{
	int i;

	/* set options from batch script */
	_opt_batch_script(file, script_body, script_size);

	for (i = WRPR_START + 1; i < WRPR_CNT; i++) {
		/* Convert command from batch script to sbatch command */
		bool stop = _opt_wrpr_batch_script(file, script_body,
						   script_size, argc, argv, i);

		if (stop) {
			break;
		}

	}

	/* set options from env vars */
	_opt_env();

	/* set options from command line */
	_set_options(argc, argv);

	if (!_opt_verify())
		exit(error_exit);

	if (opt.verbose)
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

	line = xstrndup(current, ptr-current);

	/*
	 *  Advance state past newline
	 */
	*state = (ptr < ((char *) buf + size)) ? ptr+1 : ptr;
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
static char *
_get_argument(const char *file, int lineno, const char *line, int *skipped)
{
	const char *ptr;
	char *argument = NULL;
	char q_char = '\0';
	bool escape_flag = false;
	bool quoted = false;
	int i;

	ptr = line;
	*skipped = 0;

	/* skip whitespace */
	while (isspace(*ptr) && *ptr != '\0') {
		ptr++;
	}

	if (*ptr == '\0')
		return NULL;

	/* copy argument into "argument" buffer, */
	i = 0;
	while ((quoted || !isspace(*ptr)) && *ptr != '\n' && *ptr != '\0') {
		if (escape_flag) {
			escape_flag = false;
		} else if (*ptr == '\\') {
			escape_flag = true;
			ptr++;
			continue;
		} else if (quoted) {
			if (*ptr == q_char) {
				quoted = false;
				ptr++;
				continue;
			}
		} else if (*ptr == '"' || *ptr == '\'') {
			quoted = true;
			q_char = *(ptr++);
			continue;
		} else if (*ptr == '#') {
			/* found an un-escaped #, rest of line is a comment */
			break;
		}

		if (!argument)
			argument = xmalloc(strlen(line) + 1);
		argument[i++] = *(ptr++);
	}

	if (quoted) /* Unmatched quote */
		fatal("%s: line %d: Unmatched `%c` in [%s]",
		      file, lineno, q_char, line);

	*skipped = ptr - line;

	return argument;
}

/*
 * set options from batch script
 *
 * Build an argv-style array of options from the script "body",
 * then pass the array to _set_options for() further parsing.
 */
static void _opt_batch_script(const char * file, const void *body, int size)
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
	int skipped = 0, warned = 0, lineno = 0;
	int i;

	magic_word_len1 = strlen(magic_word1);
	magic_word_len2 = strlen(magic_word2);

	/* getopt_long skips over the first argument, so fill it in */
	argc = 1;
	argv = xmalloc(sizeof(char *));
	argv[0] = "sbatch";

	while ((line = _next_line(body, size, &state)) != NULL) {
		lineno++;
		if (!xstrncmp(line, magic_word1, magic_word_len1))
			ptr = line + magic_word_len1;
		else if (!xstrncmp(line, magic_word2, magic_word_len2)) {
			ptr = line + magic_word_len2;
			if (!warned) {
				error("Change from #SLURM to #SBATCH in your "
				      "script and verify the options are "
				      "valid in sbatch");
				warned = 1;
			}
		} else {
			/* Stop parsing script if not a comment */
			bool is_cmd = false;
			for (i = 0; line[i]; i++) {
				if (isspace(line[i]))
					continue;
				if (line[i] == '#')
					break;
				is_cmd = true;
				break;
			}
			xfree(line);
			if (is_cmd)
				break;
			continue;
		}

		/* this line starts with the magic word */
		while ((option = _get_argument(file, lineno, ptr, &skipped))) {
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
 * set wrapper (ie. pbs, bsub) options from batch script
 *
 * Build an argv-style array of options from the script "body",
 * then pass the array to _set_options for() further parsing.
 */
static bool _opt_wrpr_batch_script(const char *file, const void *body,
				   int size, int cmd_argc, char **cmd_argv,
				   int magic)
{
	char *magic_word;
	void (*wrp_func) (int,char**) = NULL;
	int magic_word_len;
	int argc;
	char **argv;
	void *state = NULL;
	char *line;
	char *option;
	char *ptr;
	int skipped = 0;
	int lineno = 0;
	int non_comments = 0;
	int i;
	bool found = false;

	if (ignore_pbs)
		return false;
	if (getenv("SBATCH_IGNORE_PBS"))
		return false;
	for (i = 0; i < cmd_argc; i++) {
		if (!xstrcmp(cmd_argv[i], "--ignore-pbs"))
			return false;
	}

	/* Check what command it is */
	switch (magic) {
	case WRPR_BSUB:
		magic_word = "#BSUB";
		wrp_func = _set_bsub_options;
		break;
	case WRPR_PBS:
		magic_word = "#PBS";
		wrp_func = _set_pbs_options;
		break;

	default:
		return false;
	}

	magic_word_len = strlen(magic_word);
	/* getopt_long skips over the first argument, so fill it in */
	argc = 1;
	argv = xmalloc(sizeof(char *));
	argv[0] = "sbatch";

	while ((line = _next_line(body, size, &state)) != NULL) {
		lineno++;
		if (xstrncmp(line, magic_word, magic_word_len) != 0) {
			if (line[0] != '#')
				non_comments++;
			xfree(line);
			if (non_comments > 100)
				break;
			continue;
		}

		/* Set found to be true since we found a valid command */
		found = true;
		/* this line starts with the magic word */
		ptr = line + magic_word_len;
		while ((option = _get_argument(file, lineno, ptr, &skipped))) {
			debug2("Found in script, argument \"%s\"", option);
			argc += 1;
			xrealloc(argv, sizeof(char*) * argc);

			/* Only check the even options here (they are
			 * the - options) */
			if (magic == WRPR_BSUB && !(argc%2)) {
				/* Since Slurm doesn't allow long
				 * names with a single '-' we must
				 * translate before hand.
				 */
				if (!xstrcmp("-cwd", option)) {
					xfree(option);
					option = xstrdup("-c");
				}
			}

			argv[argc-1] = option;
			ptr += skipped;
		}
		xfree(line);
	}

	if (argc > 0 && wrp_func != NULL)
		wrp_func(argc, argv);

	for (i = 1; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);

	return found;
}

static void _set_options(int argc, char **argv)
{
	int opt_char, option_index = 0, max_val = 0, i;
	long long priority;
	char *tmp;

	struct option *optz = spank_option_table_create(long_options);

	if (!optz) {
		error("Unable to create options table");
		exit(error_exit);
	}

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, opt_string,
				       optz, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			/* handled in process_options_first_pass() */
			break;
		case 'a':
			xfree(opt.array_inx);
			opt.array_inx = xstrdup(optarg);
			break;
		case 'A':
		case 'U':	/* backwards compatibility */
			xfree(opt.account);
			opt.account = xstrdup(optarg);
			break;
		case 'b':
			/* Only here for Moab transition not suppose
			   to do anything */
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
			opt.threads_per_core_set = true;
			break;
		case 'c':
			opt.cpus_set = true;
			opt.cpus_per_task = parse_int("cpus-per-task",
						      optarg, true);
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
		case 'e':
			xfree(opt.efname);
			if (xstrcasecmp(optarg, "none") == 0)
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
			/* handled in process_options_first_pass() */
			break;
		case 'H':
			opt.hold = true;
			break;
		case 'i':
			xfree(opt.ifname);
			if (xstrcasecmp(optarg, "none") == 0)
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
				exit(error_exit);
			}
			break;
		case 'M':
			xfree(opt.clusters);
			opt.clusters = xstrdup(optarg);
			break;
		case 'n':
			opt.ntasks_set = true;
			opt.ntasks =
				parse_int("number of tasks", optarg, true);
			break;
		case 'N':
			opt.nodes_set =
				verify_node_count(optarg,
						  &opt.min_nodes,
						  &opt.max_nodes);
			if (opt.nodes_set == false) {
				error("invalid node count `%s'",
				      optarg);
				exit(error_exit);
			}
			break;
		case 'o':
			xfree(opt.ofname);
			if (xstrcasecmp(optarg, "none") == 0)
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
		case 'P':
			verbose("-P option is deprecated, use -d instead");
			xfree(opt.dependency);
			opt.dependency = xstrdup(optarg);
			break;
		case 'Q':
			/* handled in process_options_first_pass() */
			break;
		case 'R':
			opt.no_rotate = true;
			break;
		case 's':
			opt.shared = 1;
			break;
		case 'S':
			opt.core_spec = parse_int("core_spec", optarg, false);
			break;
		case 't':
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(optarg);
			break;
		case 'u':
		case 'v':
		case 'V':
			/* handled in process_options_first_pass() */
			break;
		case 'w':
			xfree(opt.nodelist);
			opt.nodelist = xstrdup(optarg);
			break;
		case 'W':
			opt.wait = true;
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
		case LONG_OPT_DEADLINE:
			opt.deadline = parse_time(optarg, 0);
			if (errno == ESLURM_INVALID_TIME_VALUE) {
				error("Invalid deadline specification %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_DELAY_BOOT:
			i = time_str2secs(optarg);
			if (i == NO_VAL) {
				error("Invalid delay-boot specification %s",
				      optarg);
				exit(error_exit);
			}
			opt.delay_boot = (uint32_t) i;
			break;
		case LONG_OPT_EXCLUSIVE:
			if (optarg == NULL) {
				opt.shared = JOB_SHARED_NONE;
			} else if (!xstrcasecmp(optarg, "user")) {
				opt.shared = JOB_SHARED_USER;
			} else if (!xstrcasecmp(optarg, "mcs")) {
				opt.shared = JOB_SHARED_MCS;
			} else {
				error("invalid exclusive option %s", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MEM_BIND:
			if (slurm_verify_mem_bind(optarg, &opt.mem_bind,
						  &opt.mem_bind_type))
				exit(error_exit);
			break;
		case LONG_OPT_MINCPU:
			opt.mincpus = parse_int("mincpus", optarg, true);
			if (opt.mincpus < 0) {
				error("invalid mincpus constraint %s", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MINCORES:
			verbose("mincores option has been deprecated, use "
				"cores-per-socket");
			opt.cores_per_socket = parse_int("mincores",
							 optarg, true);
			if (opt.cores_per_socket < 0) {
				error("invalid mincores constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MINSOCKETS:
			verbose("minsockets option has been deprecated, use "
				"sockets-per-node");
			opt.sockets_per_node = parse_int("minsockets",
							 optarg, true);
			if (opt.sockets_per_node < 0) {
				error("invalid minsockets constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MINTHREADS:
			verbose("minthreads option has been deprecated, use "
				"threads-per-core");
			opt.threads_per_core = parse_int("minthreads",
							 optarg, true);
			if (opt.threads_per_core < 0) {
				error("invalid minthreads constraint %s",
				      optarg);
				exit(error_exit);
			}
			opt.threads_per_core_set = true;
			break;
		case LONG_OPT_MEM:
			opt.realmem = (int64_t) str_to_mbytes2(optarg);
			if (opt.realmem < 0) {
				error("invalid memory constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MEM_PER_CPU:
			opt.mem_per_cpu = (int64_t) str_to_mbytes2(optarg);
			if (opt.mem_per_cpu < 0) {
				error("invalid memory constraint %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_TMP:
			opt.tmpdisk = str_to_mbytes2(optarg);
			if (opt.tmpdisk < 0) {
				error("invalid tmp value %s", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_JOBID:
			opt.jobid = parse_int("jobid", optarg, true);
			opt.jobid_set = true;
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
				error("Invalid time specification %s", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MAIL_TYPE:
			opt.mail_type |= parse_mail_type(optarg);
			if (opt.mail_type == (uint16_t)INFINITE) {
				error("--mail-type=%s invalid", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_MAIL_USER:
			xfree(opt.mail_user);
			opt.mail_user = xstrdup(optarg);
			break;
		case LONG_OPT_MCS_LABEL: {
			xfree(opt.mcs_label);
			opt.mcs_label = xstrdup(optarg);
			break;
		}
		case LONG_OPT_BURST_BUFFER_FILE:
			xfree(opt.burst_buffer_file);
			opt.burst_buffer_file = _read_file(optarg);
			break;
		case LONG_OPT_NICE: {
			long long tmp_nice;
			if (optarg)
				tmp_nice = strtoll(optarg, NULL, 10);
			else
				tmp_nice = 100;
			if (llabs(tmp_nice) > (NICE_OFFSET - 3)) {
				error("Nice value out of range (+/- %u). Value "
				      "ignored", NICE_OFFSET - 3);
				tmp_nice = 0;
			}
			if (tmp_nice < 0) {
				uid_t my_uid = getuid();
				if ((my_uid != 0) &&
				    (my_uid != slurm_get_slurm_user_id())) {
					error("Nice value must be "
					      "non-negative, value ignored");
					tmp_nice = 0;
				}
			}
			opt.nice = (int) tmp_nice;
			break;
		}
		case LONG_OPT_PRIORITY:
			if (!optarg) { /* CLANG Fix */
				;
			} else if (strcasecmp(optarg, "TOP") == 0) {
				opt.priority = NO_VAL - 1;
			} else {
				priority = strtoll(optarg, NULL, 10);
				if (priority < 0) {
					error("Priority must be >= 0");
					exit(error_exit);
				}
				if (priority >= NO_VAL) {
					error("Priority must be < %i", NO_VAL);
					exit(error_exit);
				}
				opt.priority = priority;

			}
			break;
		case LONG_OPT_NO_REQUEUE:
			opt.requeue = 0;
			break;
		case LONG_OPT_REQUEUE:
			opt.requeue = 1;
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
			opt.threads_per_core_set = true;
			break;
		case LONG_OPT_NTASKSPERNODE:
			opt.ntasks_per_node = parse_int("ntasks-per-node",
							optarg, true);
			if (opt.ntasks_per_node > 0)
				setenvf(NULL, "SLURM_NTASKS_PER_NODE", "%d",
					opt.ntasks_per_node);
			break;
		case LONG_OPT_NTASKSPERSOCKET:
			opt.ntasks_per_socket = parse_int("ntasks-per-socket",
							  optarg, true);
			setenvf(NULL, "SLURM_NTASKS_PER_SOCKET", "%d",
				opt.ntasks_per_socket);
			break;
		case LONG_OPT_NTASKSPERCORE:
			opt.ntasks_per_core = parse_int("ntasks-per-core",
							optarg, true);
			setenvf(NULL, "SLURM_NTASKS_PER_CORE", "%d",
				opt.ntasks_per_core);
			opt.ntasks_per_core_set = true;
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
			opt.hint_set = true;
			opt.ntasks_per_core_set = true;
			opt.threads_per_core_set = true;
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
#if defined HAVE_BG
			info("WARNING: If your job is smaller than the block "
			     "it is going to run on and other jobs are "
			     "running on it the --reboot option will not be "
			     "honored.  If this is the case, contact your "
			     "admin to reboot the block for you.");
#endif
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
			if (!optarg) /* CLANG Fix */
				break;
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
			xfree(opt.acctg_freq);
			opt.acctg_freq = xstrdup(optarg);
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
		case LONG_OPT_WCKEY:
			xfree(opt.wckey);
			opt.wckey = xstrdup(optarg);
			break;
		case LONG_OPT_RESERVATION:
			xfree(opt.reservation);
			opt.reservation = xstrdup(optarg);
			break;
		case LONG_OPT_CHECKPOINT:
			xfree(opt.ckpt_interval_str);
			opt.ckpt_interval_str = xstrdup(optarg);
			break;
		case LONG_OPT_CHECKPOINT_DIR:
			xfree(opt.ckpt_dir);
			opt.ckpt_dir = xstrdup(optarg);
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
			if (!xstrcasecmp(optarg, "help") ||
			    !xstrcasecmp(optarg, "list")) {
				print_gres_help();
				exit(0);
			}
			xfree(opt.gres);
			opt.gres = xstrdup(optarg);
			break;
		case LONG_OPT_GRES_FLAGS:
			if (!xstrcasecmp(optarg, "enforce-binding")) {
				opt.job_flags |= GRES_ENFORCE_BIND;
			} else {
				error("Invalid gres-flags specification: %s",
				      optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_WAIT_ALL_NODES:
			if (!optarg) /* CLANG Fix */
				break;
			if ((optarg[0] < '0') || (optarg[0] > '9')) {
				error("Invalid --wait-all-nodes argument: %s",
				      optarg);
				exit(1);
			}
			opt.wait_all_nodes = strtol(optarg, NULL, 10);
			break;
		case LONG_OPT_EXPORT:
			xfree(opt.export_env);
			opt.export_env = xstrdup(optarg);
			if (!xstrcasecmp(opt.export_env, "ALL"))
				; /* srun ignores "ALL", it is the default */
			else
				setenv("SLURM_EXPORT_ENV", opt.export_env, 0);
			break;
		case LONG_OPT_EXPORT_FILE:
			xfree(opt.export_file);
			opt.export_file = xstrdup(optarg);
			break;
		case LONG_OPT_CPU_FREQ:
			if (cpu_freq_verify_cmdline(optarg, &opt.cpu_freq_min,
						    &opt.cpu_freq_max, &opt.cpu_freq_gov))
				error("Invalid --cpu-freq argument: %s. "
				      "Ignored", optarg);
			break;
		case LONG_OPT_REQ_SWITCH:
			if (!optarg) /* CLANG Fix */
				break;
			pos_delimit = strstr(optarg,"@");
			if (pos_delimit != NULL) {
				pos_delimit[0] = '\0';
				pos_delimit++;
				opt.wait4switch = time_str2secs(pos_delimit);
			}
			opt.req_switch = parse_int("switches", optarg, true);
			break;
		case LONG_OPT_IGNORE_PBS:
			ignore_pbs = 1;
			break;
		case LONG_OPT_TEST_ONLY:
			opt.test_only = true;
			break;
		case LONG_OPT_PARSABLE:
			opt.parsable = true;
			break;
		case LONG_OPT_POWER:
			opt.power_flags = power_flags_id(optarg);
			break;
		case LONG_OPT_THREAD_SPEC:
			opt.core_spec = parse_int("thread_spec",
						  optarg, false) |
				CORE_SPEC_THREAD;
			break;
		case LONG_OPT_KILL_INV_DEP:
			if (xstrcasecmp(optarg, "yes") == 0)
				opt.job_flags |= KILL_INV_DEP;
			if (xstrcasecmp(optarg, "no") == 0)
				opt.job_flags |= NO_KILL_INV_DEP;
			break;
		case LONG_OPT_SPREAD_JOB:
			opt.job_flags |= SPREAD_JOB;
			break;
		case LONG_OPT_USE_MIN_NODES:
			opt.job_flags |= USE_MIN_NODES;
			break;
		default:
			if (spank_process_option (opt_char, optarg) < 0) {
				error("Unrecognized command line parameter %c",
				      opt_char);
				exit(error_exit);
			}
		}
	}

	if (optind < argc) {
		error("Invalid argument: %s", argv[optind]);
		exit(error_exit);
	}

	spank_option_table_destroy (optz);
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
static void _set_bsub_options(int argc, char **argv) {

	int opt_char, option_index = 0;
	char *bsub_opt_string = "+c:e:J:m:M:n:o:q:W:x";
	char *tmp_str, *char_ptr;

	struct option bsub_long_options[] = {
		{"cwd", required_argument, 0, 'c'},
		{"error_file", required_argument, 0, 'e'},
		{"job_name", required_argument, 0, 'J'},
		{"hostname", required_argument, 0, 'm'},
		{"memory_limit", required_argument, 0, 'M'},
		{"memory_limit", required_argument, 0, 'M'},
		{"output_file", required_argument, 0, 'o'},
		{"queue_name", required_argument, 0, 'q'},
		{"time", required_argument, 0, 'W'},
		{"exclusive", no_argument, 0, 'x'},
		{NULL, 0, 0, 0}
	};

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, bsub_opt_string,
				       bsub_long_options, &option_index))
	       != -1) {
		switch (opt_char) {
		case 'c':
			xfree(opt.cwd);
			if (is_full_path(optarg))
				opt.cwd = xstrdup(optarg);
			else
				opt.cwd = make_full_path(optarg);
			break;
		case 'e':
			xfree(opt.efname);
			if (xstrcasecmp(optarg, "none") == 0)
				opt.efname = xstrdup("/dev/null");
			else
				opt.efname = xstrdup(optarg);
			break;
		case 'J':
			opt.job_name = xstrdup(optarg);
			break;
		case 'm':
			/* Since BSUB requires a list of space
			   sperated host we need to replace the spaces
			   with , */
			tmp_str = xstrdup(optarg);
			char_ptr = strstr(tmp_str, " ");

			while (char_ptr != NULL) {
				*char_ptr = ',';
				char_ptr = strstr(tmp_str, " ");
			}
			opt.nodelist = xstrdup(tmp_str);
			xfree(tmp_str);
			break;
		case 'M':
			opt.mem_per_cpu = xstrntol(optarg,
						   NULL, strlen(optarg), 10);
			break;
		case 'n':
			opt.ntasks_set = true;
			/* Since it is value in bsub to give a min and
			 * max task count we will only read the max if
			 * it exists.
			 */
			char_ptr = strstr(optarg, ",");
			if (char_ptr) {
				char_ptr++;
				if (!char_ptr[0]) {
					error("#BSUB -n format not correct "
					      "given: '%s'",
					      optarg);
					exit(error_exit);
				}
			} else
				char_ptr = optarg;

			opt.ntasks =
				parse_int("number of tasks", char_ptr, true);

			break;
		case 'o':
			xfree(opt.ofname);
			opt.ofname = xstrdup(optarg);
			break;
		case 'q':
			opt.partition = xstrdup(optarg);
			break;
		case 'W':
			opt.time_limit = xstrntol(optarg, NULL,
						  strlen(optarg), 10);
			break;
		case 'x':
			opt.shared = 0;
			break;
		default:
			error("Unrecognized command line parameter %c",
			      opt_char);
			exit(error_exit);
		}
	}


	if (optind < argc) {
		error("Invalid argument: %s", argv[optind]);
		exit(error_exit);
	}

}

static void _set_pbs_options(int argc, char **argv)
{
	int opt_char, option_index = 0;
	char *sep = "";
	char *pbs_opt_string = "+a:A:c:C:e:hIj:J:k:l:m:M:N:o:p:q:r:S:t:u:v:VW:z";

	struct option pbs_long_options[] = {
		{"start_time", required_argument, 0, 'a'},
		{"account", required_argument, 0, 'A'},
		{"checkpoint", required_argument, 0, 'c'},
		{"working_dir", required_argument, 0, 'C'},
		{"error", required_argument, 0, 'e'},
		{"hold", no_argument, 0, 'h'},
		{"interactive", no_argument, 0, 'I'},
		{"join", optional_argument, 0, 'j'},
		{"job_array", required_argument, 0, 'J'},
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
		{"array", required_argument, 0, 't'},
		{"running_user", required_argument, 0, 'u'},
		{"variable_list", required_argument, 0, 'v'},
		{"all_env", no_argument, 0, 'V'},
		{"attributes", required_argument, 0, 'W'},
		{"no_std", no_argument, 0, 'z'},
		{NULL, 0, 0, 0}
	};

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, pbs_opt_string,
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
			break;
		case 'e':
			xfree(opt.efname);
			if (xstrcasecmp(optarg, "none") == 0)
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
		case 'J':
		case 't':
			/* PBS Pro uses -J. Torque uses -t. */
			xfree(opt.array_inx);
			opt.array_inx = xstrdup(optarg);
			break;
		case 'k':
			break;
		case 'l':
			_parse_pbs_resource_list(optarg);
			break;
		case 'm':
			if (!optarg) /* CLANG Fix */
				break;
			opt.mail_type |= _parse_pbs_mail_type(optarg);
			if (opt.mail_type == (uint16_t)INFINITE) {
				error("-m=%s invalid", optarg);
				exit(error_exit);
			}
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
			if (xstrcasecmp(optarg, "none") == 0)
				opt.ofname = xstrdup("/dev/null");
			else
				opt.ofname = xstrdup(optarg);
			break;
		case 'p': {
			long long tmp_nice;
			if (optarg)
				tmp_nice = strtoll(optarg, NULL, 10);
			else
				tmp_nice = 100;
			if (llabs(tmp_nice) > (NICE_OFFSET - 3)) {
				error("Nice value out of range (+/- %u). Value "
				      "ignored", NICE_OFFSET - 3);
				tmp_nice = 0;
			}
			if (tmp_nice < 0) {
				uid_t my_uid = getuid();
				if ((my_uid != 0) &&
				    (my_uid != slurm_get_slurm_user_id())) {
					error("Nice value must be "
					      "non-negative, value ignored");
					tmp_nice = 0;
				}
			}
			opt.nice = (int) tmp_nice;
			break;
		}
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
			if (opt.export_env)
				sep = ",";
			/* CLANG false positive */
			xstrfmtcat(opt.export_env, "%s%s", sep, optarg);
			break;
		case 'V':
			break;
		case 'W':
			if (!optarg) /* CLANG Fix */
				break;
			if (!strncasecmp(optarg, "umask=", 6)) {
				opt.umask = strtol(optarg+6, NULL, 0);
				if ((opt.umask < 0) || (opt.umask > 0777)) {
					error("Invalid umask ignored");
					opt.umask = -1;
				}
			} else if (!strncasecmp(optarg, "depend=", 7)) {
				xfree(opt.dependency);
				opt.dependency = xstrdup(optarg+7);
			} else {
				verbose("Ignored PBS attributes: %s", optarg);
			}
			break;
		case 'z':
			break;
		default:
			error("Unrecognized command line parameter %c",
			      opt_char);
			exit(error_exit);
		}
	}

	if (optind < argc) {
		error("Invalid argument: %s", argv[optind]);
		exit(error_exit);
	}
}

static char *_get_pbs_node_name(char *node_options, int *i)
{
	int start = (*i);
	char *value = NULL;

	while (node_options[*i] &&
	       (node_options[*i] != '+') &&
	       (node_options[*i] != ':'))
		(*i)++;

	value = xmalloc((*i)-start+1);
	memcpy(value, node_options+start, (*i)-start);

	if (node_options[*i])
		(*i)++;

	return value;
}

static void _get_next_pbs_node_part(char *node_options, int *i)
{
	while (node_options[*i] &&
	       (node_options[*i] != '+') &&
	       (node_options[*i] != ':'))
		(*i)++;
	if (node_options[*i])
		(*i)++;
}

static void _parse_pbs_nodes_opts(char *node_opts)
{
	int i = 0;
	char *temp = NULL;
	int ppn = 0;
	int node_cnt = 0;
	hostlist_t hl = hostlist_create(NULL);

	while (node_opts[i]) {
		if (!xstrncmp(node_opts+i, "ppn=", 4)) {
			i+=4;
			ppn += strtol(node_opts+i, NULL, 10);
			_get_next_pbs_node_part(node_opts, &i);
		} else if (isdigit(node_opts[i])) {
			node_cnt += strtol(node_opts+i, NULL, 10);
			_get_next_pbs_node_part(node_opts, &i);
		} else if (isalpha(node_opts[i])) {
			temp = _get_pbs_node_name(node_opts, &i);
			hostlist_push_host(hl, temp);
			xfree(temp);
		} else
			i++;

	}

	if (!node_cnt)
		node_cnt = 1;
	else {
		opt.nodes_set = true;
		opt.min_nodes = opt.max_nodes = node_cnt;
	}

	if (ppn) {
		ppn *= node_cnt;
		opt.ntasks_set = true;
		opt.ntasks = ppn;
	}

	if (hostlist_count(hl) > 0) {
		xfree(opt.nodelist);
		opt.nodelist = hostlist_ranged_string_xmalloc(hl);
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
	while (pbs_options[*i] && pbs_options[*i] != ',')
		(*i)++;
	if (pbs_options[*i])
		(*i)++;
}

static char *_get_pbs_option_value(char *pbs_options, int *i, char sep)
{
	int start = (*i);
	char *value = NULL;

	while (pbs_options[*i] && pbs_options[*i] != sep)
		(*i)++;
	value = xmalloc((*i)-start+1);
	memcpy(value, pbs_options+start, (*i)-start);

	if (pbs_options[*i])
		(*i)++;

	return value;
}

static void _parse_pbs_resource_list(char *rl)
{
	int i = 0;
	int gpus = 0;
	char *temp = NULL;
	int pbs_pro_flag = 0;	/* Bits: select:1 ncpus:2 mpiprocs:4 */

	while (rl[i]) {
		if (!strncasecmp(rl+i, "accelerator=", 12)) {
			i += 12;
			if (!strncasecmp(rl+i, "true", 4) && (gpus < 1))
				gpus = 1;
			/* Also see "naccelerators=" below */
		} else if (!xstrncmp(rl+i, "arch=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "cput=", 5)) {
			i+=5;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for cput");
				exit(error_exit);
			}
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(temp);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "file=", 5)) {
			int end = 0;

			i+=5;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for file");
				exit(error_exit);
			}
			end = strlen(temp) - 1;
			if (toupper(temp[end]) == 'B') {
				/* In Torque they do GB or MB on the
				 * end of size, we just want G or M so
				 * we will remove the b on the end
				 */
				temp[end] = '\0';
			}
			opt.tmpdisk = str_to_mbytes(temp);
			if (opt.tmpdisk < 0) {
				error("invalid tmp value %s", temp);
				exit(error_exit);
			}
			xfree(temp);
		} else if (!xstrncmp(rl+i, "host=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "mem=", 4)) {
			int end = 0;

			i+=4;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for mem");
				exit(error_exit);
			}
			end = strlen(temp) - 1;
			if (toupper(temp[end]) == 'B') {
				/* In Torque they do GB or MB on the
				 * end of size, we just want G or M so
				 * we will remove the b on the end
				 */
				temp[end] = '\0';
			}
			opt.realmem = (int) str_to_mbytes(temp);
			if (opt.realmem < 0) {
				error("invalid memory constraint %s", temp);
				exit(error_exit);
			}

			xfree(temp);
		} else if (!strncasecmp(rl+i, "mpiprocs=", 9)) {
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ':');
			if (temp) {
				pbs_pro_flag |= 4;
				opt.ntasks_per_node = parse_int("mpiprocs",
								temp, true);
				xfree(temp);
			}
#if defined(HAVE_ALPS_CRAY) || defined(HAVE_NATIVE_CRAY)
			/*
			 * NB: no "mppmem" here since it specifies per-PE memory units,
			 *     whereas SLURM uses per-node and per-CPU memory units.
			 */
		} else if (!xstrncmp(rl + i, "mppdepth=", 9)) {
			/* Cray: number of CPUs (threads) per processing element */
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp) {
				opt.cpus_per_task = parse_int("mppdepth",
							      temp, false);
				opt.cpus_set	  = true;
			}
			xfree(temp);
		} else if (!xstrncmp(rl + i, "mppnodes=", 9)) {
			/* Cray `nodes' variant: hostlist without prefix */
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for mppnodes");
				exit(error_exit);
			}
			xfree(opt.nodelist);
			opt.nodelist = temp;
		} else if (!xstrncmp(rl + i, "mppnppn=", 8)) {
			/* Cray: number of processing elements per node */
			i += 8;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp)
				opt.ntasks_per_node = parse_int("mppnppn",
								temp, true);
			xfree(temp);
		} else if (!xstrncmp(rl + i, "mppwidth=", 9)) {
			/* Cray: task width (number of processing elements) */
			i += 9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp) {
				opt.ntasks = parse_int("mppwidth", temp, true);
				opt.ntasks_set = true;
			}
			xfree(temp);
#endif	/* HAVE_ALPS_CRAY || HAVE_NATIVE_CRAY */
		} else if (!strncasecmp(rl+i, "naccelerators=", 14)) {
			i += 14;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp) {
				gpus = parse_int("naccelerators", temp, true);
				xfree(temp);
			}
		} else if (!strncasecmp(rl+i, "ncpus=", 6)) {
			i += 6;
			temp = _get_pbs_option_value(rl, &i, ':');
			if (temp) {
				pbs_pro_flag |= 2;
				opt.mincpus = parse_int("ncpus", temp, true);
				xfree(temp);
			}
		} else if (!xstrncmp(rl+i, "nice=", 5)) {
			long long tmp_nice;
			i += 5;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (temp)
				tmp_nice = strtoll(temp, NULL, 10);
			else
				tmp_nice = 100;
			if (llabs(tmp_nice) > (NICE_OFFSET - 3)) {
				error("Nice value out of range (+/- %u). Value "
				      "ignored", NICE_OFFSET - 3);
				tmp_nice = 0;
			}
			if (tmp_nice < 0) {
				uid_t my_uid = getuid();
				if ((my_uid != 0) &&
				    (my_uid != slurm_get_slurm_user_id())) {
					error("Nice value must be "
					      "non-negative, value ignored");
					tmp_nice = 0;
				}
			}
			opt.nice = (int) tmp_nice;
			xfree(temp);
		} else if (!xstrncmp(rl+i, "nodes=", 6)) {
			i+=6;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for nodes");
				exit(error_exit);
			}
			_parse_pbs_nodes_opts(temp);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "opsys=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "other=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "pcput=", 6)) {
			i+=6;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for pcput");
				exit(error_exit);
			}
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(temp);
			xfree(temp);
		} else if (!xstrncmp(rl+i, "pmem=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "proc=", 5)) {
			i += 5;
			if (opt.constraints)
				xstrcat(opt.constraints, ",");
			temp = _get_pbs_option_value(rl, &i, ',');
			xstrcat(opt.constraints, temp);
			xfree(temp);
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "pvmem=", 6)) {
			i+=6;
			_get_next_pbs_option(rl, &i);
		} else if (!strncasecmp(rl+i, "select=", 7)) {
			i += 7;
			temp = _get_pbs_option_value(rl, &i, ':');
			if (temp) {
				pbs_pro_flag |= 1;
				opt.min_nodes = parse_int("select", temp, true);
				opt.max_nodes = opt.min_nodes;
				opt.nodes_set = true;
				xfree(temp);
			}
		} else if (!xstrncmp(rl+i, "software=", 9)) {
			i+=9;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "vmem=", 5)) {
			i+=5;
			_get_next_pbs_option(rl, &i);
		} else if (!xstrncmp(rl+i, "walltime=", 9)) {
			i+=9;
			temp = _get_pbs_option_value(rl, &i, ',');
			if (!temp) {
				error("No value given for walltime");
				exit(error_exit);
			}
			xfree(opt.time_limit_str);
			opt.time_limit_str = xstrdup(temp);
			xfree(temp);
		} else
			i++;
	}

	if ((pbs_pro_flag == 7) && (opt.mincpus > opt.ntasks_per_node)) {
		/* This logic will allocate the proper CPU count on each
		 * node if the CPU count per node is evenly divisible by
		 * the task count on each node. Slurm can't handle something
		 * like cpus_per_node=10 and ntasks_per_node=8 */
		opt.cpus_per_task = opt.mincpus / opt.ntasks_per_node;
		opt.cpus_set = true;
	}
	if (gpus > 0) {
		char *sep = "";
		if (opt.gres)
			sep = ",";
		xstrfmtcat(opt.gres, "%sgpu:%d", sep, gpus);
	}
}

/*
 * _opt_verify : perform some post option processing verification
 *
 */
static bool _opt_verify(void)
{
	bool verified = true;
	char *dist = NULL, *lllp_dist = NULL;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();
	hostlist_t hl = NULL;
	int hl_cnt = 0;

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	if (opt.hint_env &&
	    (!opt.hint_set && !opt.ntasks_per_core_set &&
	     !opt.threads_per_core_set)) {
		if (verify_hint(opt.hint_env,
				&opt.sockets_per_node,
				&opt.cores_per_socket,
				&opt.threads_per_core,
				&opt.ntasks_per_core,
				NULL)) {
			exit(error_exit);
		}
	}

	if (opt.ntasks_set && (opt.ntasks > 0)) {
		setenvf(NULL, "SLURM_NPROCS", "%d", opt.ntasks);
		setenvf(NULL, "SLURM_NTASKS", "%d", opt.ntasks);
	}

	_fullpath(&opt.efname, opt.cwd);
	_fullpath(&opt.ifname, opt.cwd);
	_fullpath(&opt.ofname, opt.cwd);

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
			opt.distribution &= SLURM_DIST_STATE_FLAGS;
			opt.distribution |= SLURM_DIST_ARBITRARY;
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

	if (opt.nodelist) {
		hl = hostlist_create(opt.nodelist);

		if (!hl) {
			error("memory allocation failure");
			exit(error_exit);
		}
		hostlist_uniq(hl);
		hl_cnt = hostlist_count(hl);
		if (opt.nodes_set)
			opt.min_nodes = MAX(hl_cnt, opt.min_nodes);
		else
			opt.min_nodes = hl_cnt;
		opt.nodes_set = true;
	}

	if (cluster_flags & CLUSTER_FLAG_BGQ)
		bg_figure_nodes_tasks(&opt.min_nodes, &opt.max_nodes,
				      &opt.ntasks_per_node, &opt.ntasks_set,
				      &opt.ntasks, opt.nodes_set, opt.nodes_set,
				      opt.overcommit, 0);

	if ((opt.ntasks_per_node > 0) && (!opt.ntasks_set) &&
	    ((opt.max_nodes == 0) || (opt.min_nodes == opt.max_nodes))) {
		opt.ntasks = opt.min_nodes * opt.ntasks_per_node;
		opt.ntasks_set = 1;
	}

	if (opt.cpus_set && (opt.mincpus < opt.cpus_per_task))
		opt.mincpus = opt.cpus_per_task;

	if ((opt.job_name == NULL) && (opt.script_argc > 0))
		opt.job_name = base_name(opt.script_argv[0]);
	if (opt.job_name)
		setenv("SLURM_JOB_NAME", opt.job_name, 1);

	/* check for realistic arguments */
	if (opt.ntasks < 0) {
		error("invalid number of tasks (-n %d)", opt.ntasks);
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

	if (opt.linuximage && strchr(opt.linuximage, ' ')) {
		error("invalid CnloadImage given '%s'", opt.linuximage);
		verified = false;
	}

	if (opt.mloaderimage && strchr(opt.mloaderimage, ' ')) {
		error("invalid MloaderImage given '%s'", opt.mloaderimage);
		verified = false;
	}

	if (opt.ramdiskimage && strchr(opt.ramdiskimage, ' ')) {
		error("invalid IoloadImage given '%s'", opt.ramdiskimage);
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
	if ((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_PLANE &&
	    opt.plane_size) {
		if ((opt.min_nodes <= 0) ||
		    ((opt.ntasks/opt.plane_size) < opt.min_nodes)) {
			if (((opt.min_nodes-1)*opt.plane_size) >= opt.ntasks) {
#if (0)
				info("Too few processes ((n/plane_size) %d < N %d) "
				     "and ((N-1)*(plane_size) %d >= n %d)) ",
				     opt.ntasks/opt.plane_size, opt.min_nodes,
				     (opt.min_nodes-1)*opt.plane_size, opt.ntasks);
#endif
				error("Too few processes for the requested "
				      "{plane,node} distribution");
				exit(error_exit);
			}
		}
	}

	if (opt.cpus_set &&
	    setenvf(NULL, "SLURM_CPUS_PER_TASK", "%d", opt.cpus_per_task)) {
		error("Can't set SLURM_CPUS_PER_TASK env variable");
	}

	set_distribution(opt.distribution, &dist, &lllp_dist);
	if (dist &&
	    setenvf(NULL, "SLURM_DISTRIBUTION", "%s", dist)) {
		error("Can't set SLURM_DISTRIBUTION env variable");
	}

	if (((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_PLANE) &&
	    setenvf(NULL, "SLURM_DIST_PLANESIZE", "%d", opt.plane_size)) {
		error("Can't set SLURM_DIST_PLANESIZE env variable");
	}

	if (lllp_dist && setenvf(NULL, "SLURM_DIST_LLLP", "%s", lllp_dist)) {
		error("Can't set SLURM_DIST_LLLP env variable");
	}

	/* massage the numbers */
	if ((opt.nodes_set || opt.extra_set)				&&
	    ((opt.min_nodes == opt.max_nodes) || (opt.max_nodes == 0))	&&
	    !opt.ntasks_set) {
		/* 1 proc / node default */
		opt.ntasks = MAX(opt.min_nodes, 1);

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
		 * Make sure that the number of
		 * max_nodes is <= number of tasks
		 */
		if (opt.ntasks < opt.max_nodes)
			opt.max_nodes = opt.ntasks;

		/*
		 *  make sure # of procs >= min_nodes
		 */
		if (opt.ntasks < opt.min_nodes) {

			info ("Warning: can't run %d processes on %d "
			      "nodes, setting nnodes to %d",
			      opt.ntasks, opt.min_nodes, opt.ntasks);

			opt.min_nodes = opt.max_nodes = opt.ntasks;

			if (hl_cnt > opt.min_nodes) {
				int del_cnt, i;
				char *host;
				del_cnt = hl_cnt - opt.min_nodes;
				for (i=0; i<del_cnt; i++) {
					host = hostlist_pop(hl);
					free(host);
				}
				xfree(opt.nodelist);
				opt.nodelist =
					hostlist_ranged_string_xmalloc(hl);
			}

		}

	} /* else if (opt.ntasks_set && !opt.nodes_set) */

	/* set up the proc and node counts based on the arbitrary list
	   of nodes */
	if (((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_ARBITRARY)
	    && (!opt.nodes_set || !opt.ntasks_set)) {
		if (!hl)
			hl = hostlist_create(opt.nodelist);
		if (!opt.ntasks_set) {
			opt.ntasks_set = 1;
			opt.ntasks = hostlist_count(hl);
		}
		if (!opt.nodes_set) {
			opt.nodes_set = 1;
			hostlist_uniq(hl);
			opt.min_nodes = opt.max_nodes = hostlist_count(hl);
		}
	}

	if (hl)
		hostlist_destroy(hl);

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
			error("Invalid time-min specification");
			exit(error_exit);
		}
		if (opt.time_min == 0)
			opt.time_min = INFINITE;
	}
	if ((opt.deadline) && (opt.begin) && (opt.deadline < opt.begin)) {
		error("Incompatible begin and deadline time specification");
		exit(error_exit);
	}

	if (opt.ckpt_interval_str) {
		opt.ckpt_interval = time_str2mins(opt.ckpt_interval_str);
		if ((opt.ckpt_interval < 0) &&
		    (opt.ckpt_interval != INFINITE)) {
			error("Invalid checkpoint interval specification");
			exit(error_exit);
		}
	}

	if ((opt.euid != (uid_t) -1) && (opt.euid != opt.uid))
		opt.uid = opt.euid;

	if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid))
		opt.gid = opt.egid;

	if (opt.open_mode) {
		/* Propage mode to spawned job using environment variable */
		if (opt.open_mode == OPEN_MODE_APPEND)
			setenvf(NULL, "SLURM_OPEN_MODE", "a");
		else
			setenvf(NULL, "SLURM_OPEN_MODE", "t");
	}
	if (opt.dependency)
		setenvfs("SLURM_JOB_DEPENDENCY=%s", opt.dependency);

	if (opt.profile)
		setenvfs("SLURM_PROFILE=%s",
			 acct_gather_profile_to_string(opt.profile));


	if (opt.acctg_freq)
		setenvf(NULL, "SLURM_ACCTG_FREQ", "%s", opt.acctg_freq);

#ifdef HAVE_NATIVE_CRAY
	if (opt.network && opt.shared)
		fatal("Requesting network performance counters requires "
		      "exclusive access.  Please add the --exclusive option "
		      "to your request.");
	if (opt.network)
		setenv("SLURM_NETWORK", opt.network, 1);
#endif

	if (opt.mem_bind_type && (getenv("SBATCH_MEM_BIND") == NULL)) {
		char tmp[64];
		slurm_sprint_mem_bind_type(tmp, opt.mem_bind_type);
		if (opt.mem_bind) {
			setenvf(NULL, "SBATCH_MEM_BIND", "%s:%s",
				tmp, opt.mem_bind);
		} else {
			setenvf(NULL, "SBATCH_MEM_BIND", "%s", tmp);
		}
	}
	if (opt.mem_bind_type && (getenv("SLURM_MEM_BIND_SORT") == NULL) &&
	    (opt.mem_bind_type & MEM_BIND_SORT)) {
		setenvf(NULL, "SLURM_MEM_BIND_SORT", "sort");
	}

	if (opt.mem_bind_type && (getenv("SLURM_MEM_BIND_VERBOSE") == NULL)) {
		if (opt.mem_bind_type & MEM_BIND_VERBOSE) {
			setenvf(NULL, "SLURM_MEM_BIND_VERBOSE", "verbose");
		} else {
			setenvf(NULL, "SLURM_MEM_BIND_VERBOSE", "quiet");
		}
	}

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

	cpu_freq_set_env("SLURM_CPU_FREQ_REQ",
			 opt.cpu_freq_min, opt.cpu_freq_max, opt.cpu_freq_gov);

	return verified;
}

static uint16_t _parse_pbs_mail_type(const char *arg)
{
	uint16_t rc = 0;

	if (strchr(arg, 'b') || strchr(arg, 'B'))
		rc |= MAIL_JOB_BEGIN;
	if (strchr(arg, 'e') || strchr(arg, 'E'))
		rc |= MAIL_JOB_END;
	if (strchr(arg, 'a') || strchr(arg, 'A'))
		rc |= MAIL_JOB_FAIL;

	if (strchr(arg, 'n') || strchr(arg, 'N'))
		rc = 0;
	else if (!rc)
		rc = (uint16_t)INFINITE;

	return rc;
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
		if (xstrncmp(opt.spank_job_env[i], tmp_str, len))
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
		if (xstrncmp(opt.spank_job_env[i], tmp_str, len))
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
		if (xstrncmp(opt.spank_job_env[i], tmp_str, len))
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
static char *print_constraints(void)
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
		xstrfmtcat(buf, "mem=%"PRIi64"M ", opt.realmem);

	if (opt.mem_per_cpu > 0)
		xstrfmtcat(buf, "mem-per-cpu=%"PRIi64"M ", opt.mem_per_cpu);

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

static void _opt_list(void)
{
	char *str;

	info("defined options for program `%s'", opt.progname);
	info("----------------- ---------------------");

	info("user              : `%s'", opt.user);
	info("uid               : %ld", (long) opt.uid);
	info("gid               : %ld", (long) opt.gid);
	info("cwd               : %s", opt.cwd);
	info("ntasks            : %d %s", opt.ntasks,
	     opt.ntasks_set ? "(set)" : "(default)");
	if (opt.cpus_set)
		info("cpus_per_task     : %d", opt.cpus_per_task);
	if (opt.max_nodes) {
		info("nodes             : %d-%d",
		     opt.min_nodes, opt.max_nodes);
	} else {
		info("nodes             : %d %s", opt.min_nodes,
		     opt.nodes_set ? "(set)" : "(default)");
	}
	info("jobid             : %u %s", opt.jobid,
	     opt.jobid_set ? "(set)" : "(default)");
	info("partition         : %s",
	     opt.partition == NULL ? "default" : opt.partition);
	info("profile           : `%s'",
	     acct_gather_profile_to_string(opt.profile));
	info("job name          : `%s'", opt.job_name);
	info("reservation       : `%s'", opt.reservation);
	info("wckey             : `%s'", opt.wckey);
	info("distribution      : %s",
	     format_task_dist_states(opt.distribution));
	if ((opt.distribution  & SLURM_DIST_STATE_BASE) == SLURM_DIST_PLANE)
		info("plane size        : %u", opt.plane_size);
	info("verbose           : %d", opt.verbose);
	info("immediate         : %s", tf_(opt.immediate));
	if (opt.requeue != NO_VAL)
		info("requeue           : %u", opt.requeue);
	info("overcommit        : %s", tf_(opt.overcommit));
	if (opt.time_limit == INFINITE)
		info("time_limit        : INFINITE");
	else if (opt.time_limit != NO_VAL)
		info("time_limit        : %d", opt.time_limit);
	if (opt.time_min != NO_VAL)
		info("time_min          : %d", opt.time_min);
	if (opt.nice)
		info("nice              : %d", opt.nice);
	info("account           : %s", opt.account);
	info("comment           : %s", opt.comment);
	info("dependency        : %s", opt.dependency);
	if (opt.gres)
		info("gres              : %s", opt.gres);
	info("qos               : %s", opt.qos);
	str = print_constraints();
	info("constraints       : %s", str);
	xfree(str);
	if (opt.conn_type[0] != (uint16_t) NO_VAL) {
		str = conn_type_string_full(opt.conn_type);
		info("conn_type      : %s", str);
		xfree(str);
	}
	str = print_geometry(opt.geometry);
	info("geometry          : %s", str);
	xfree(str);
	info("reboot            : %s", opt.reboot ? "no" : "yes");
	info("rotate            : %s", opt.no_rotate ? "yes" : "no");
	info("network           : %s", opt.network);

	if (opt.linuximage)
		info("CnloadImage       : %s", opt.linuximage);
	if (opt.mloaderimage)
		info("MloaderImage      : %s", opt.mloaderimage);
	if (opt.ramdiskimage)
		info("IoloadImage       : %s", opt.ramdiskimage);
	if (opt.begin) {
		char time_str[32];
		slurm_make_time_str(&opt.begin, time_str, sizeof(time_str));
		info("begin             : %s", time_str);
	}
	if (opt.deadline) {
		char time_str[32];
		slurm_make_time_str(&opt.deadline, time_str, sizeof(time_str));
		info("deadline          : %s", time_str);
	}
	info("array             : %s",
	     opt.array_inx == NULL ? "N/A" : opt.array_inx);
	info("cpu_freq_min      : %u", opt.cpu_freq_min);
	info("cpu_freq_max      : %u", opt.cpu_freq_max);
	info("cpu_freq_gov      : %u", opt.cpu_freq_gov);
	if (opt.delay_boot != NO_VAL)
		info("delay_boot        : %u", opt.delay_boot);
	info("mail_type         : %s", print_mail_type(opt.mail_type));
	info("mail_user         : %s", opt.mail_user);
	info("sockets-per-node  : %d", opt.sockets_per_node);
	info("cores-per-socket  : %d", opt.cores_per_socket);
	info("threads-per-core  : %d", opt.threads_per_core);
	info("ntasks-per-node   : %d", opt.ntasks_per_node);
	info("ntasks-per-socket : %d", opt.ntasks_per_socket);
	info("ntasks-per-core   : %d", opt.ntasks_per_core);
	info("mem_bind          : %s",
	     opt.mem_bind == NULL ? "default" : opt.mem_bind);
	info("plane_size        : %u", opt.plane_size);
	info("propagate         : %s",
	     opt.propagate == NULL ? "NONE" : opt.propagate);
	info("switches          : %d", opt.req_switch);
	info("wait-for-switches : %d", opt.wait4switch);
	str = print_commandline(opt.script_argc, opt.script_argv);
	if (opt.core_spec == (uint16_t) NO_VAL)
		info("core-spec         : NA");
	else if (opt.core_spec & CORE_SPEC_THREAD) {
		info("thread-spec       : %d",
		     opt.core_spec & (~CORE_SPEC_THREAD));
	} else
		info("core-spec         : %d", opt.core_spec);
	info("burst_buffer_file : `%s'", opt.burst_buffer_file);
	info("remote command    : `%s'", str);
	info("power             : %s", power_flags_str(opt.power_flags));
	info("wait              : %s", opt.wait ? "no" : "yes");
	if (opt.mcs_label)
		info("mcs-label         : %s",opt.mcs_label);
	xfree(str);

}

static void _usage(void)
{
	printf(
"Usage: sbatch [-N nnodes] [-n ntasks]\n"
"              [-c ncpus] [-r n] [-p partition] [--hold] [--parsable] [-t minutes]\n"
"              [-D path] [--immediate] [--no-kill] [--overcommit]\n"
"              [--input file] [--output file] [--error file]\n"
"              [--time-min=minutes] [--licenses=names] [--clusters=cluster_names]\n"
"              [--workdir=directory] [--oversubscibe] [-m dist] [-J jobname]\n"
"              [--jobid=id] [--verbose] [--gid=group] [--uid=user]\n"
"              [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"              [--account=name] [--dependency=type:jobid] [--comment=name]\n"
#ifdef HAVE_BG		/* Blue gene specific options */
"              [--geometry=AxXxYxZ] [--conn-type=type] [--no-rotate]\n"
"              [--cnload-image=path]\n"
"              [--mloader-image=path] [--ioload-image=path]\n"
#endif
"              [--mail-type=type] [--mail-user=user][--nice[=value]] [--wait]\n"
"              [--requeue] [--no-requeue] [--ntasks-per-node=n] [--propagate]\n"
"              [--nodefile=file] [--nodelist=hosts] [--exclude=hosts]\n"
"              [--network=type] [--mem-per-cpu=MB] [--qos=qos] [--gres=list]\n"
"              [--mem_bind=...] [--reservation=name] [--mcs-label=mcs]\n"
"              [--cpu-freq=min[-max[:gov]] [--power=flags] [--gres-flags=opts]\n"
"              [--switches=max-switches{@max-time-to-wait}] [--reboot]\n"
"              [--core-spec=cores] [--thread-spec=threads] [--bbf=burst_buffer_file]\n"
"              [--array=index_values] [--profile=...] [--ignore-pbs] [--spread-job]\n"
"              [--export[=names]] [--export-file=file|fd] [--delay-boot=mins]\n"
"              [--use-min-nodes] executable [args...]\n");
}

static void _help(void)
{
	slurm_ctl_conf_t *conf;

	printf (
"Usage: sbatch [OPTIONS...] executable [args...]\n"
"\n"
"Parallel run options:\n"
"  -a, --array=indexes         job array index values\n"
"  -A, --account=name          charge job to specified account\n"
"      --bb=<spec>             burst buffer specifications\n"
"      --bbf=<file_name>       burst buffer specification file\n"
"      --begin=time            defer job until HH:MM MM/DD/YY\n"
"      --comment=name          arbitrary comment\n"
"      --cpu-freq=min[-max[:gov]] requested cpu frequency (and governor)\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"

"  -d, --dependency=type:jobid defer job until condition on jobid is satisfied\n"
"      --deadline=time         remove the job if no ending possible before\n"
"                              this deadline (start > (deadline - time[-min]))\n"
"      --delay-boot=mins       delay boot for desired node features\n"
"  -D, --workdir=directory     set working directory for batch script\n"
"  -e, --error=err             file for batch script's standard error\n"
"      --export[=names]        specify environment variables to export\n"
"      --export-file=file|fd   specify environment variables file or file\n"
"                              descriptor to export\n"
"      --get-user-env          load environment from local cluster\n"
"      --gid=group_id          group ID to run job as (user root only)\n"
"      --gres=list             required generic resources\n"
"      --gres-flags=opts       flags related to GRES management\n"
"  -H, --hold                  submit job in held state\n"
"      --ignore-pbs            Ignore #PBS options in the batch script\n"
"  -i, --input=in              file for batch script's standard input\n"
"  -I, --immediate             exit if resources are not immediately available\n"
"      --jobid=id              run under already allocated job\n"
"  -J, --job-name=jobname      name of job\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -L, --licenses=names        required license, comma separated\n"
"  -M, --clusters=names        Comma separated list of clusters to issue\n"
"                              commands to.  Default is current cluster.\n"
"                              Name of 'all' will submit to run on all clusters.\n"
"                              NOTE: SlurmDBD must up.\n"
"  -m, --distribution=type     distribution method for processes to nodes\n"
"                              (type = block|cyclic|arbitrary)\n"
"      --mail-type=type        notify on state change: BEGIN, END, FAIL or ALL\n"
"      --mail-user=user        who to send email notification for job state\n"
"                              changes\n"
"      --mcs-label=mcs         mcs label if mcs plugin mcs/group is used\n"
"  -n, --ntasks=ntasks         number of tasks to run\n"
"      --nice[=value]          decrease scheduling priority by value\n"
"      --no-requeue            if set, do not permit the job to be requeued\n"
"      --ntasks-per-node=n     number of tasks to invoke on each node\n"
"  -N, --nodes=N               number of nodes on which to run (N = min[-max])\n"
"  -o, --output=out            file for batch script's standard output\n"
"  -O, --overcommit            overcommit resources\n"
"  -p, --partition=partition   partition requested\n"
"      --parsable              outputs only the jobid and cluster name (if present),\n"
"                              separated by semicolon, only on successful submission.\n"
"      --power=flags           power management options\n"
"      --priority=value        set the priority of the job to value\n"
"      --profile=value         enable acct_gather_profile for detailed data\n"
"                              value is all or none or any combination of\n"
"                              energy, lustre, network or task\n"
"      --propagate[=rlimits]   propagate all [or specific list of] rlimits\n"
"      --qos=qos               quality of service\n"
"  -Q, --quiet                 quiet mode (suppress informational messages)\n"
"      --reboot                reboot compute nodes before starting job\n"
"      --requeue               if set, permit the job to be requeued\n"
"  -s, --oversubscribe         over subscribe resources with other jobs\n"
"  -S, --core-spec=cores       count of reserved cores\n"
"      --signal=[B:]num[@time] send signal when time limit within time seconds\n"
"      --spread-job            spread job across as many nodes as possible\n"
"      --switches=max-switches{@max-time-to-wait}\n"
"                              Optimum switches and max time to wait for optimum\n"
"      --thread-spec=threads   count of reserved threads\n"
"  -t, --time=minutes          time limit\n"
"      --time-min=minutes      minimum time limit (if distinct)\n"
"      --uid=user_id           user ID to run job as (user root only)\n"
"      --use-min-nodes         if a range of node counts is given, prefer the\n"
"                              smaller count\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -W, --wait                  wait for completion of submitted job\n"
"      --wckey=wckey           wckey to run job under\n"
"      --wrap[=command string] wrap command string in a sh script and submit\n"

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
"      --exclusive[=user]      allocate nodes in exclusive mode when\n"
"                              cpu consumable resource is enabled\n"
"      --exclusive[=mcs]       allocate nodes in exclusive mode when\n"
"                              cpu consumable resource is enabled\n"
"                              and mcs plugin is enabled\n"
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
	    && xstrcasecmp(conf->task_plugin, "task/affinity") == 0) {
		printf(
"      --hint=                 Bind tasks according to application hints\n"
"                              (see \"--hint=help\" for options)\n"
"      --mem_bind=             Bind memory to locality domains (ldom)\n"
"                              (see \"--mem_bind=help\" for options)\n");
	}
	slurm_conf_unlock();

	spank_print_options(stdout, 6, 30);

	printf("\n"
#ifdef HAVE_NATIVE_CRAY			/* Native Cray specific options */
"Cray related options:\n"
"      --network=type          Use network performance counters\n"
"                              (system, network, or processor)\n"
"\n"
#endif
#ifdef HAVE_BG				/* Blue gene specific options */
"Blue Gene related options:\n"
"  -g, --geometry=AxXxYxZ      Midplane geometry constraints of the job,\n"
"                              sub-block allocations can not be allocated\n"
"                              with the geometry option\n"
"  -R, --no-rotate             disable geometry rotation\n"
"      --conn-type=type        constraint on type of connection, MESH or TORUS\n"
"                              if not set, then tries to fit TORUS else MESH\n"
"                              If wanting to run in HTC mode (only for 1\n"
"                              midplane and below).  You can use HTC_S for\n"
"                              SMP, HTC_D for Dual, HTC_V for\n"
"                              virtual node mode, and HTC_L for Linux mode.\n"
"      --cnload-image=path     path to compute node image for bluegene block.  Default if not set\n"
"      --mloader-image=path    path to mloader image for bluegene block.  Default if not set\n"
"      --ioload-image=path     path to ioload image for bluegene block.  Default if not set\n"
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
