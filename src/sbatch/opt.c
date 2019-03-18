/*****************************************************************************\
 *  opt.c - options processing for sbatch
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2018 SchedMD LLC <https://www.schedmd.com>
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#define _GNU_SOURCE

#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>		/* getenv     */
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

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_NODES       0x04
#define OPT_BOOL        0x05
#define OPT_CORE        0x06
#define OPT_MULTI	0x0b
#define OPT_OPEN_MODE	0x0e
#define OPT_NO_REQUEUE  0x10
#define OPT_REQUEUE     0x11
#define OPT_EXPORT        0x17
#define OPT_ARRAY_INX     0x20
#define OPT_INT64	  0x24

/*---- global variables, defined in opt.h ----*/
sbatch_opt_t sbopt;
slurm_opt_t opt = { .sbatch_opt = &sbopt };
sbatch_env_t pack_env;
int   error_exit = 1;
int   ignore_pbs = 0;
bool  is_pack_job = false;

/*---- forward declarations of static functions  ----*/

typedef struct env_vars env_vars_t;

static void  _help(void);

/* fill in default options  */
static void _opt_default(bool first_pass);

/* set options from batch script */
static bool _opt_batch_script(const char *file, const void *body, int size,
			      int pack_inx);

/* set options based upon env vars  */
static void _opt_env(void);

/* verify options sanity  */
static bool _opt_verify(void);

static void _process_env_var(env_vars_t *e, const char *val);

static void _fullpath(char **filename, const char *cwd);
static char *_read_file(char *fname);
static void _set_options(int argc, char **argv);
static void _usage(void);

/*---[ end forward declarations of static functions ]---------------------*/

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
static void _opt_default(bool first_pass)
{
	uid_t uid = getuid();

	/* Some options will persist for all components of a heterogeneous job
	 * once specified for one, but will be overwritten with new values if
	 * specified on the command line */
	if (first_pass) {
		sbopt.ckpt_interval	= 0;
		xfree(sbopt.ckpt_interval_str);
		opt.egid		= (gid_t) -1;
		xfree(sbopt.efname);
		xfree(opt.extra);
		xfree(sbopt.export_env);
		xfree(sbopt.export_file);
		opt.euid		= (uid_t) -1;
		opt.gid			= getgid();
		sbopt.ifname		= xstrdup("/dev/null");
		xfree(sbopt.ofname);
		sbopt.parsable		= false;
		xfree(sbopt.propagate); 	 /* propagate specific rlimits */
		sbopt.requeue		= NO_VAL;
		sbopt.test_only		= false;
		opt.uid			= uid;
		sbopt.umask		= -1;
		sbopt.wait		= false;
		opt.x11			= 0;
	}

	/* All other options must be specified individually for each component
	 * of the job */
	opt.cores_per_socket		= NO_VAL; /* requested cores */
	opt.cpus_per_task		= 0;
	opt.cpus_set			= false;
	opt.job_flags			= 0;
	opt.max_nodes			= 0;
	opt.min_nodes			= 1;
	opt.nodes_set			= false;
	opt.ntasks			= 1;
	opt.ntasks_per_core		= NO_VAL;
	opt.ntasks_per_core_set		= false;
	opt.ntasks_per_node		= 0;	/* ntask max limits */
	opt.ntasks_per_socket		= NO_VAL;
	opt.ntasks_set			= false;
	opt.sockets_per_node		= NO_VAL; /* requested sockets */
	opt.threads_per_core		= NO_VAL; /* requested threads */
	opt.threads_per_core_set	= false;

	slurm_reset_all_options(&opt, first_pass);
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
	file_buf = xmalloc(stat_buf.st_size + 1);
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
	file_buf[stat_buf.st_size] = '\0';
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
  { "SBATCH_ACCOUNT", 'A' },
  {"SBATCH_ARRAY_INX",     OPT_STRING,     &sbopt.array_inx,   NULL          },
  { "SBATCH_ACCTG_FREQ", LONG_OPT_ACCTG_FREQ },
  {"SBATCH_BATCH",         OPT_STRING,     &sbopt.batch_features, NULL       },
  { "SBATCH_BURST_BUFFER", LONG_OPT_BURST_BUFFER_SPEC },
  {"SBATCH_CHECKPOINT",    OPT_STRING,     &sbopt.ckpt_interval_str, NULL    },
  { "SBATCH_CLUSTER_CONSTRAINT", LONG_OPT_CLUSTER_CONSTRAINT },
  { "SBATCH_CLUSTERS", 'M' },
  { "SLURM_CLUSTERS", 'M' },
  { "SBATCH_CONSTRAINT", 'C' },
  { "SBATCH_CORE_SPEC", 'S' },
  { "SBATCH_CPU_FREQ_REQ", LONG_OPT_CPU_FREQ },
  { "SBATCH_CPUS_PER_GPU", LONG_OPT_CPUS_PER_GPU },
  { "SBATCH_DEBUG", 'v' },
  { "SBATCH_DELAY_BOOT", LONG_OPT_DELAY_BOOT },
  { "SBATCH_DISTRIBUTION", 'm' },
  { "SBATCH_EXCLUSIVE", LONG_OPT_EXCLUSIVE },
  {"SBATCH_EXPORT",        OPT_STRING,     &sbopt.export_env,  NULL          },
  { "SBATCH_GET_USER_ENV", LONG_OPT_GET_USER_ENV },
  { "SBATCH_GRES", LONG_OPT_GRES },
  { "SBATCH_GRES_FLAGS", LONG_OPT_GRES_FLAGS },
  { "SBATCH_GPUS", 'G' },
  { "SBATCH_GPU_BIND", LONG_OPT_GPU_BIND },
  { "SBATCH_GPU_FREQ", LONG_OPT_GPU_FREQ },
  { "SBATCH_GPUS_PER_NODE", LONG_OPT_GPUS_PER_NODE },
  { "SBATCH_GPUS_PER_SOCKET", LONG_OPT_GPUS_PER_SOCKET },
  { "SBATCH_GPUS_PER_TASK", LONG_OPT_GPUS_PER_TASK },
  { "SLURM_HINT", LONG_OPT_HINT },
  { "SBATCH_HINT", LONG_OPT_HINT },
  { "SBATCH_JOB_NAME", 'J' },
  { "SBATCH_MEM_BIND", LONG_OPT_MEM_BIND },
  { "SBATCH_MEM_PER_GPU", LONG_OPT_MEM_PER_GPU },
  { "SBATCH_NETWORK", LONG_OPT_NETWORK },
  { "SBATCH_NO_KILL", 'k' },
  {"SBATCH_NO_REQUEUE",    OPT_NO_REQUEUE, NULL,               NULL          },
  {"SBATCH_OPEN_MODE",     OPT_OPEN_MODE,  NULL,               NULL          },
  { "SBATCH_OVERCOMMIT", 'O' },
  { "SBATCH_PARTITION", 'p' },
  { "SBATCH_POWER", LONG_OPT_POWER },
  { "SBATCH_PROFILE", LONG_OPT_PROFILE },
  { "SBATCH_QOS", 'q' },
  { "SBATCH_REQ_SWITCH", LONG_OPT_SWITCH_REQ },
  {"SBATCH_REQUEUE",       OPT_REQUEUE,    NULL,               NULL          },
  { "SBATCH_RESERVATION", LONG_OPT_RESERVATION },
  { "SBATCH_SIGNAL", LONG_OPT_SIGNAL },
  { "SBATCH_SPREAD_JOB", LONG_OPT_SPREAD_JOB },
  { "SBATCH_THREAD_SPEC", LONG_OPT_THREAD_SPEC },
  { "SBATCH_TIMELIMIT", 't' },
  { "SBATCH_USE_MIN_NODES", LONG_OPT_USE_MIN_NODES },
  {"SBATCH_WAIT",          OPT_BOOL,       &sbopt.wait,        NULL          },
  { "SBATCH_WAIT_ALL_NODES", LONG_OPT_WAIT_ALL_NODES },
  { "SBATCH_WAIT4SWITCH", LONG_OPT_SWITCH_WAIT },
  { "SBATCH_WCKEY", LONG_OPT_WCKEY },

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

	/* Process spank env options */
	if (spank_process_env_options())
		exit(error_exit);
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
		xfree(sbopt.array_inx);
		sbopt.array_inx = xstrdup(val);
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
	case OPT_OPEN_MODE:
		if ((val[0] == 'a') || (val[0] == 'A'))
			sbopt.open_mode = OPEN_MODE_APPEND;
		else if ((val[0] == 't') || (val[0] == 'T'))
			sbopt.open_mode = OPEN_MODE_TRUNCATE;
		else
			error("Invalid SBATCH_OPEN_MODE: %s. Ignored", val);
		break;
	case OPT_NO_REQUEUE:
		sbopt.requeue = 0;
		break;

	case OPT_REQUEUE:
		sbopt.requeue = 1;
		break;
	default:
		/*
		* assume this was meant to be processed by
		 * slurm_process_option() instead.
		 */
		slurm_process_option(&opt, e->type, val, true, false);
		break;
	}
}


/*---[ command line option processing ]-----------------------------------*/

static struct option long_options[] = {
	{"array",         required_argument, 0, 'a'},
	{"cpus-per-task", required_argument, 0, 'c'},
	{"error",         required_argument, 0, 'e'},
	{"help",          no_argument,       0, 'h'},
	{"input",         required_argument, 0, 'i'},
	{"kill-on-invalid-dep", required_argument, 0, LONG_OPT_KILL_INV_DEP},
	{"tasks",         required_argument, 0, 'n'},
	{"ntasks",        required_argument, 0, 'n'},
	{"nodes",         required_argument, 0, 'N'},
	{"output",        required_argument, 0, 'o'},
	{"usage",         no_argument,       0, 'u'},
	{"version",       no_argument,       0, 'V'},
	{"wait",          no_argument,       0, 'W'},
	{"batch",         required_argument, 0, LONG_OPT_BATCH},
	{"bbf",           required_argument, 0, LONG_OPT_BURST_BUFFER_FILE},
	{"checkpoint",    required_argument, 0, LONG_OPT_CHECKPOINT},
	{"checkpoint-dir",required_argument, 0, LONG_OPT_CHECKPOINT_DIR},
	{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
	{"export",        required_argument, 0, LONG_OPT_EXPORT},
	{"export-file",   required_argument, 0, LONG_OPT_EXPORT_FILE},
	{"gid",           required_argument, 0, LONG_OPT_GID},
	{"ignore-pbs",    no_argument,       0, LONG_OPT_IGNORE_PBS},
	{"no-requeue",    no_argument,       0, LONG_OPT_NO_REQUEUE},
	{"ntasks-per-core",  required_argument, 0, LONG_OPT_NTASKSPERCORE},
	{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
	{"open-mode",     required_argument, 0, LONG_OPT_OPEN_MODE},
	{"parsable",      optional_argument, 0, LONG_OPT_PARSABLE},
	{"propagate",     optional_argument, 0, LONG_OPT_PROPAGATE},
	{"requeue",       no_argument,       0, LONG_OPT_REQUEUE},
	{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
	{"tasks-per-node",required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"test-only",     no_argument,       0, LONG_OPT_TEST_ONLY},
	{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
	{"uid",           required_argument, 0, LONG_OPT_UID},
	{"wrap",          required_argument, 0, LONG_OPT_WRAP},
	{NULL,            0,                 0, 0}
};

static char *opt_string =
	"+a:A:b:B:c:C:d:D:e:F:G:hHi:IJ:k::L:m:M:n:N:o:Op:q:QsS:t:uvVw:Wx:";

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
extern char *process_options_first_pass(int argc, char **argv)
{
	int opt_char, option_index = 0;
	struct option *common_options, *optz;
	int i, local_argc = 0;
	char **local_argv, *script_file = NULL;

	/* initialize option defaults */
	_opt_default(true);

	common_options = slurm_option_table_create(long_options, &opt);
	optz = spank_option_table_create(common_options);
	slurm_option_table_destroy(common_options);

	if (!optz) {
		error("Unable to create options table");
		exit(error_exit);
	}

	/* Remove pack job separator and capture all options of interest from
	 * all job components (e.g. "sbatch -N1 -v : -N2 -v tmp" -> "-vv") */
	local_argv = xmalloc(sizeof(char *) * argc);
	for (i = 0; i < argc; i++) {
		if (xstrcmp(argv[i], ":"))
			local_argv[local_argc++] = argv[i];
	}

	optind = 0;
	while ((opt_char = getopt_long(local_argc, local_argv, opt_string,
				       optz, &option_index)) != -1) {
		switch (opt_char) {
		case '?':
			fprintf(stderr,
				"Try \"sbatch --help\" for more information\n");
			exit(error_exit);
			break;
		case 'h':
			_help();
			exit(0);
			break;
		case 'u':
			_usage();
			exit(0);
		case 'V':
			print_slurm_version();
			exit(0);
			break;
		case LONG_OPT_WRAP:
			xfree(opt.job_name);
			xfree(sbopt.wrap);
			opt.job_name = xstrdup("wrap");
			sbopt.wrap = xstrdup(optarg);
			break;
		default:
			slurm_process_option(&opt, opt_char, optarg, true, true);
			break;
		}
	}
	spank_option_table_destroy(optz);

	if ((local_argc > optind) && (sbopt.wrap != NULL)) {
		error("Script arguments not permitted with --wrap option");
		exit(error_exit);
	}
	if (local_argc > optind) {
		int i;
		char **leftover;

		sbopt.script_argc = local_argc - optind;
		leftover = local_argv + optind;
		sbopt.script_argv = xmalloc((sbopt.script_argc + 1)
						 * sizeof(char *));
		for (i = 0; i < sbopt.script_argc; i++)
			sbopt.script_argv[i] = xstrdup(leftover[i]);
		sbopt.script_argv[i] = NULL;
	}
	if (sbopt.script_argc > 0) {
		char *fullpath;
		char *cmd       = sbopt.script_argv[0];
		int  mode       = R_OK;

		if ((fullpath = search_path(opt.chdir, cmd, false, mode, false))) {
			xfree(sbopt.script_argv[0]);
			sbopt.script_argv[0] = fullpath;
		}
		script_file = sbopt.script_argv[0];
	}

	xfree(local_argv);
	return script_file;
}

/* process options:
 * 1. update options with option set in the script
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 *
 * argc IN - Count of elements in argv
 * argv IN - Array of elements to parse
 * argc_off OUT - Offset of first non-parsable element
 * pack_inx IN - pack job component ID, zero origin
 * more_packs OUT - more packs job specifications in script to process
 */
extern void process_options_second_pass(int argc, char **argv, int *argc_off,
					int pack_inx, bool *more_packs,
					const char *file,
					const void *script_body,
					int script_size)
{
	static bool first_pass = true;
	int i;

	/* initialize option defaults */
	_opt_default(first_pass);
	first_pass = false;

	/* set options from batch script */
	*more_packs = _opt_batch_script(file, script_body, script_size,
				        pack_inx);

	for (i = WRPR_START + 1; i < WRPR_CNT; i++) {
		/* Convert command from batch script to sbatch command */
		bool stop = xlate_batch_script(file, script_body, script_size,
					       argc, argv, i);

		if (stop)
			break;
	}

	/* set options from env vars */
	_opt_env();

	/* set options from command line */
	_set_options(argc, argv);
	*argc_off = optind;

	if (!_opt_verify())
		exit(error_exit);

	if (opt.verbose)
		slurm_print_set_options(&opt);
}

/*
 * next_line - Interpret the contents of a byte buffer as characters in
 *	a file. next_line will find and return the next line in the buffer.
 *
 *	If "state" is NULL, it will start at the beginning of the buffer.
 *	next_line will update the "state" pointer to point at the
 *	spot in the buffer where it left off.
 *
 * IN buf - buffer containing file contents
 * IN size - size of buffer "buf"
 * IN/OUT state - used by next_line to determine where the last line ended
 *
 * RET - xmalloc'ed character string, or NULL if no lines remaining in buf.
 */
extern char *next_line(const void *buf, int size, void **state)
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
 * get_argument - scans a line for something that looks like a command line
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
extern char *get_argument(const char *file, int lineno, const char *line,
			  int *skipped)
{
	const char *ptr;
	char *argument = NULL;
	char q_char = '\0';
	bool escape_flag = false;
	bool quoted = false;
	int i;

	ptr = line;
	*skipped = 0;

	if ((argument = strcasestr(line, "packjob")))
		memcpy(argument, "       ", 7);

	/* skip whitespace */
	while (isspace(*ptr) && *ptr != '\0') {
		ptr++;
	}

	if (*ptr == ':') {
		fatal("%s: line %d: Unexpected `:` in [%s]",
		      file, lineno, line);
	}

	if (*ptr == '\0')
		return NULL;

	/* copy argument into "argument" buffer, */
	i = 0;
	while ((quoted || !isspace(*ptr)) && (*ptr != '\n') && (*ptr != '\0')) {
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
		} else if ((*ptr == '"') || (*ptr == '\'')) {
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

	if (quoted) {	/* Unmatched quote */
		fatal("%s: line %d: Unmatched `%c` in [%s]",
		      file, lineno, q_char, line);
	}

	*skipped = ptr - line;

	return argument;
}

/*
 * set options from batch script
 *
 * Build an argv-style array of options from the script "body",
 * then pass the array to _set_options for() further parsing.
 * RET - True if more pack job specifications to process
 */
static bool _opt_batch_script(const char * file, const void *body, int size,
			      int pack_inx)
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
	int i, pack_scan_inx = 0;
	bool more_packs = false;

	magic_word_len1 = strlen(magic_word1);
	magic_word_len2 = strlen(magic_word2);

	/* getopt_long skips over the first argument, so fill it in */
	argc = 1;
	argv = xmalloc(sizeof(char *));
	argv[0] = "sbatch";

	while ((line = next_line(body, size, &state)) != NULL) {
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
		if (strcasestr(line, "packjob"))
			pack_scan_inx++;
		if (pack_scan_inx < pack_inx) {
			xfree(line);
			continue;
		}
		if (pack_scan_inx > pack_inx) {
			more_packs = true;
			xfree(line);
			break;
		}

		while ((option = get_argument(file, lineno, ptr, &skipped))) {
			debug2("Found in script, argument \"%s\"", option);
			argc++;
			xrealloc(argv, sizeof(char *) * argc);
			argv[argc - 1] = option;
			ptr += skipped;
		}
		xfree(line);
	}

	if (argc > 0)
		_set_options(argc, argv);

	for (i = 1; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);

	return more_packs;
}

static void _set_options(int argc, char **argv)
{
	int opt_char, option_index = 0, max_val = 0;

	struct option *common_options = slurm_option_table_create(long_options,
								  &opt);
	struct option *optz = spank_option_table_create(common_options);
	slurm_option_table_destroy(common_options);

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
			xfree(sbopt.array_inx);
			sbopt.array_inx = xstrdup(optarg);
			break;
		case 'c':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			opt.cpus_set = true;
			opt.cpus_per_task = parse_int("cpus-per-task",
						      optarg, true);
			break;
		case 'e':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sbopt.efname);
			if (xstrcasecmp(optarg, "none") == 0)
				sbopt.efname = xstrdup("/dev/null");
			else
				sbopt.efname = xstrdup(optarg);
			break;
		case 'h':
			/* handled in process_options_first_pass() */
			break;
		case 'i':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sbopt.ifname);
			if (xstrcasecmp(optarg, "none") == 0)
				sbopt.ifname = xstrdup("/dev/null");
			else
				sbopt.ifname = xstrdup(optarg);
			break;
		case 'n':
			opt.ntasks_set = true;
			opt.ntasks =
				parse_int("number of tasks", optarg, true);
			break;
		case 'N':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
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
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sbopt.ofname);
			if (xstrcasecmp(optarg, "none") == 0)
				sbopt.ofname = xstrdup("/dev/null");
			else
				sbopt.ofname = xstrdup(optarg);
			break;
		case 'u':
		case 'V':
			/* handled in process_options_first_pass() */
			break;
		case 'W':
			sbopt.wait = true;
			break;
		case LONG_OPT_UID:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (getuid() != 0) {
				error("--uid only permitted by root user");
				exit(error_exit);
			}
			if (opt.euid != (uid_t) -1) {
				error("duplicate --uid option");
				exit(error_exit);
			}
			if (uid_from_string(optarg, &opt.euid) < 0) {
				error("--uid=\"%s\" invalid", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_GID:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (getuid() != 0) {
				error("--gid only permitted by root user");
				exit(error_exit);
			}
			if (opt.egid != (gid_t) -1) {
				error("duplicate --gid option");
				exit(error_exit);
			}
			if (gid_from_string(optarg, &opt.egid) < 0) {
				error("--gid=\"%s\" invalid", optarg);
				exit(error_exit);
			}
			break;
		case LONG_OPT_BURST_BUFFER_FILE:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sbopt.burst_buffer_file);
			sbopt.burst_buffer_file = _read_file(optarg);
			break;
		case LONG_OPT_NO_REQUEUE:
			sbopt.requeue = 0;
			break;
		case LONG_OPT_REQUEUE:
			sbopt.requeue = 1;
			break;
		case LONG_OPT_SOCKETSPERNODE:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			max_val = 0;
			get_resource_arg_range( optarg, "sockets-per-node",
						&opt.sockets_per_node,
						&max_val, true );
			if ((opt.sockets_per_node == 1) &&
			    (max_val == INT_MAX))
				opt.sockets_per_node = NO_VAL;
			break;
		case LONG_OPT_CORESPERSOCKET:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			max_val = 0;
			get_resource_arg_range( optarg, "cores-per-socket",
						&opt.cores_per_socket,
						&max_val, true );
			if ((opt.cores_per_socket == 1) &&
			    (max_val == INT_MAX))
				opt.cores_per_socket = NO_VAL;
			break;
		case LONG_OPT_THREADSPERCORE:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
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
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			opt.ntasks_per_node = parse_int("ntasks-per-node",
							optarg, true);
			if (opt.ntasks_per_node > 0)
				pack_env.ntasks_per_node = opt.ntasks_per_node;
			break;
		case LONG_OPT_NTASKSPERSOCKET:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			opt.ntasks_per_socket = parse_int("ntasks-per-socket",
							  optarg, true);
			pack_env.ntasks_per_socket = opt.ntasks_per_socket;
			break;
		case LONG_OPT_NTASKSPERCORE:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			opt.ntasks_per_core = parse_int("ntasks-per-core",
							optarg, true);
			pack_env.ntasks_per_core = opt.ntasks_per_core;
			opt.ntasks_per_core_set = true;
			break;
		case LONG_OPT_BATCH:
			xfree(sbopt.batch_features);
			sbopt.batch_features = xstrdup(optarg);
			break;
		case LONG_OPT_WRAP:
			/* handled in process_options_first_pass() */
			break;
		case LONG_OPT_OPEN_MODE:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if ((optarg[0] == 'a') || (optarg[0] == 'A'))
				sbopt.open_mode = OPEN_MODE_APPEND;
			else if ((optarg[0] == 't') || (optarg[0] == 'T'))
				sbopt.open_mode = OPEN_MODE_TRUNCATE;
			else {
				error("Invalid --open-mode argument: %s. "
				      "Ignored", optarg);
			}
			break;
		case LONG_OPT_PROPAGATE:
			xfree(sbopt.propagate);
			if (optarg)
				sbopt.propagate = xstrdup(optarg);
			else
				sbopt.propagate = xstrdup("ALL");
			break;
		case LONG_OPT_CHECKPOINT:
			xfree(sbopt.ckpt_interval_str);
			sbopt.ckpt_interval_str = xstrdup(optarg);
			break;
		case LONG_OPT_EXPORT:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sbopt.export_env);
			sbopt.export_env = xstrdup(optarg);
			if (!xstrcasecmp(sbopt.export_env, "ALL"))
				; /* srun ignores "ALL", it is the default */
			else
				setenv("SLURM_EXPORT_ENV", sbopt.export_env, 0);
			break;
		case LONG_OPT_EXPORT_FILE:
			xfree(sbopt.export_file);
			sbopt.export_file = xstrdup(optarg);
			break;
		case LONG_OPT_IGNORE_PBS:
			ignore_pbs = 1;
			break;
		case LONG_OPT_TEST_ONLY:
			sbopt.test_only = true;
			break;
		case LONG_OPT_PARSABLE:
			sbopt.parsable = true;
			break;
		case LONG_OPT_KILL_INV_DEP:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (xstrcasecmp(optarg, "yes") == 0)
				opt.job_flags |= KILL_INV_DEP;
			if (xstrcasecmp(optarg, "no") == 0)
				opt.job_flags |= NO_KILL_INV_DEP;
			break;
		default:
			if (slurm_process_option(&opt, opt_char, optarg, false, false) < 0)
				if (spank_process_option(opt_char, optarg) < 0)
					exit(error_exit);
		}
	}

	spank_option_table_destroy(optz);
}

/*
 * _opt_verify : perform some post option processing verification
 *
 */
static bool _opt_verify(void)
{
	bool verified = true;
	char *dist = NULL, *dist_lllp = NULL;
	hostlist_t hl = NULL;
	int hl_cnt = 0;

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	if (opt.hint &&
	    !opt.ntasks_per_core_set && !opt.threads_per_core_set) {
		if (verify_hint(opt.hint,
				&opt.sockets_per_node,
				&opt.cores_per_socket,
				&opt.threads_per_core,
				&opt.ntasks_per_core,
				NULL)) {
			exit(error_exit);
		}
	}

	_fullpath(&sbopt.efname, opt.chdir);
	_fullpath(&sbopt.ifname, opt.chdir);
	_fullpath(&sbopt.ofname, opt.chdir);

	if (opt.exclude && !_valid_node_list(&opt.exclude))
		exit(error_exit);

	if (opt.nodefile) {
		char *tmp;
		xfree(opt.nodelist);
		if (!(tmp = slurm_read_hostfile(opt.nodefile, 0))) {
			error("Invalid --nodefile node file");
			exit(-1);
		}
		opt.nodelist = xstrdup(tmp);
		free(tmp);
	}

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
				error("Failure getting NodeNames from hostfile");
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

	if ((opt.ntasks_per_node > 0) && (!opt.ntasks_set) &&
	    ((opt.max_nodes == 0) || (opt.min_nodes == opt.max_nodes))) {
		opt.ntasks = opt.min_nodes * opt.ntasks_per_node;
		opt.ntasks_set = 1;
	}

	if (opt.cpus_set && (opt.pn_min_cpus < opt.cpus_per_task))
		opt.pn_min_cpus = opt.cpus_per_task;

	if ((opt.job_name == NULL) && (sbopt.script_argc > 0))
		opt.job_name = base_name(sbopt.script_argv[0]);
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

	if ((opt.pn_min_memory != NO_VAL64) && (opt.mem_per_cpu != NO_VAL64)) {
		if (opt.pn_min_memory < opt.mem_per_cpu) {
			info("mem < mem-per-cpu - resizing mem to be equal "
			     "to mem-per-cpu");
			opt.pn_min_memory = opt.mem_per_cpu;
		}
		error("--mem and --mem-per-cpu are mutually exclusive.");
	}

	/* Check to see if user has specified enough resources to
	 * satisfy the plane distribution with the specified
	 * plane_size.
	 * if (n/plane_size < N) and ((N-1) * plane_size >= n) -->
	 * problem Simple check will not catch all the problem/invalid
	 * cases.
	 * The limitations of the plane distribution in the cons_res
	 * environment are more extensive and are documented in the
	 * Slurm reference guide.  */
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

	if (opt.cpus_set)
		 pack_env.cpus_per_task = opt.cpus_per_task;

	set_distribution(opt.distribution, &dist, &dist_lllp);
	if (dist)
		 pack_env.dist = xstrdup(dist);
	if ((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_PLANE)
		 pack_env.plane_size = opt.plane_size;
	if (dist_lllp)
		 pack_env.dist_lllp = xstrdup(dist_lllp);

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

	if (opt.ntasks_set && (opt.ntasks > 0))
		pack_env.ntasks = opt.ntasks;

	if (hl)
		hostlist_destroy(hl);

	if ((opt.deadline) && (opt.begin) && (opt.deadline < opt.begin)) {
		error("Incompatible begin and deadline time specification");
		exit(error_exit);
	}

	if (sbopt.ckpt_interval_str) {
		sbopt.ckpt_interval = time_str2mins(sbopt.ckpt_interval_str);
		if ((sbopt.ckpt_interval < 0) &&
		    (sbopt.ckpt_interval != INFINITE)) {
			error("Invalid checkpoint interval specification");
			exit(error_exit);
		}
	}

	if ((opt.euid != (uid_t) -1) && (opt.euid != opt.uid))
		opt.uid = opt.euid;

	if ((opt.egid != (gid_t) -1) && (opt.egid != opt.gid))
		opt.gid = opt.egid;

	if (sbopt.open_mode) {
		/* Propage mode to spawned job using environment variable */
		if (sbopt.open_mode == OPEN_MODE_APPEND)
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
		char *tmp = slurm_xstr_mem_bind_type(opt.mem_bind_type);
		if (opt.mem_bind) {
			xstrfmtcat(pack_env.mem_bind, "%s:%s",
				   tmp, opt.mem_bind);
		} else {
			pack_env.mem_bind = xstrdup(tmp);
		}
		xfree(tmp);
	}
	if (opt.mem_bind_type && (getenv("SLURM_MEM_BIND_SORT") == NULL) &&
	    (opt.mem_bind_type & MEM_BIND_SORT)) {
		pack_env.mem_bind_sort = xstrdup("sort");
	}

	if (opt.mem_bind_type && (getenv("SLURM_MEM_BIND_VERBOSE") == NULL)) {
		if (opt.mem_bind_type & MEM_BIND_VERBOSE) {
			pack_env.mem_bind_verbose = xstrdup("verbose");
		} else {
			pack_env.mem_bind_verbose = xstrdup("quiet");
		}
	}

	cpu_freq_set_env("SLURM_CPU_FREQ_REQ",
			 opt.cpu_freq_min, opt.cpu_freq_max, opt.cpu_freq_gov);

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

static void _usage(void)
{
	printf(
"Usage: sbatch [-N nnodes] [-n ntasks]\n"
"              [-c ncpus] [-r n] [-p partition] [--hold] [--parsable] [-t minutes]\n"
"              [-D path] [--no-kill] [--overcommit]\n"
"              [--input file] [--output file] [--error file]\n"
"              [--time-min=minutes] [--licenses=names] [--clusters=cluster_names]\n"
"              [--chdir=directory] [--oversubscibe] [-m dist] [-J jobname]\n"
"              [--verbose] [--gid=group] [--uid=user]\n"
"              [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"              [--account=name] [--dependency=type:jobid] [--comment=name]\n"
"              [--mail-type=type] [--mail-user=user][--nice[=value]] [--wait]\n"
"              [--requeue] [--no-requeue] [--ntasks-per-node=n] [--propagate]\n"
"              [--nodefile=file] [--nodelist=hosts] [--exclude=hosts]\n"
"              [--network=type] [--mem-per-cpu=MB] [--qos=qos] [--gres=list]\n"
"              [--mem-bind=...] [--reservation=name] [--mcs-label=mcs]\n"
"              [--cpu-freq=min[-max[:gov]] [--power=flags] [--gres-flags=opts]\n"
"              [--switches=max-switches{@max-time-to-wait}] [--reboot]\n"
"              [--core-spec=cores] [--thread-spec=threads]\n"
"              [--bb=burst_buffer_spec] [--bbf=burst_buffer_file]\n"
"              [--array=index_values] [--profile=...] [--ignore-pbs] [--spread-job]\n"
"              [--export[=names]] [--export-file=file|fd] [--delay-boot=mins]\n"
"              [--use-min-nodes]\n"
"              [--cpus-per-gpu=n] [--gpus=n] [--gpu-bind=...] [--gpu-freq=...]\n"
"              [--gpus-per-node=n] [--gpus-per-socket=n]  [--gpus-per-task=n]\n"
"              [--mem-per-gpu=MB]\n"
"              executable [args...]\n");
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
"  -b, --begin=time            defer job until HH:MM MM/DD/YY\n"
"      --comment=name          arbitrary comment\n"
"      --cpu-freq=min[-max[:gov]] requested cpu frequency (and governor)\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"

"  -d, --dependency=type:jobid defer job until condition on jobid is satisfied\n"
"      --deadline=time         remove the job if no ending possible before\n"
"                              this deadline (start > (deadline - time[-min]))\n"
"      --delay-boot=mins       delay boot for desired node features\n"
"  -D, --chdir=directory       set working directory for batch script\n"
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
"  -q, --qos=qos               quality of service\n"
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
"      --cluster-constraint=[!]list specify a list of cluster constraints\n"
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
	if (xstrstr(conf->task_plugin, "affinity")) {
		printf(
"      --hint=                 Bind tasks according to application hints\n"
"                              (see \"--hint=help\" for options)\n"
"      --mem-bind=             Bind memory to locality domains (ldom)\n"
"                              (see \"--mem-bind=help\" for options)\n");
	}
	slurm_conf_unlock();

	spank_print_options(stdout, 6, 30);

	printf("\n"
"GPU scheduling options:\n"
"      --cpus-per-gpu=n        number of CPUs required per allocated GPU\n"
"  -G, --gpus=n                count of GPUs required for the job\n"
"      --gpu-bind=...          task to gpu binding options\n"
"      --gpu-freq=...          frequency and voltage of GPUs\n"
"      --gpus-per-node=n       number of GPUs required per allocated node\n"
"      --gpus-per-socket=n     number of GPUs required per allocated socket\n"
"      --gpus-per-task=n       number of GPUs required per spawned task\n"
"      --mem-per-gpu=n         real memory required per allocated GPU\n"
		);

	printf("\n"
#ifdef HAVE_NATIVE_CRAY			/* Native Cray specific options */
"Cray related options:\n"
"      --network=type          Use network performance counters\n"
"                              (system, network, or processor)\n"
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

extern void init_envs(sbatch_env_t *local_env)
{
	local_env->cpus_per_task	= NO_VAL;
	local_env->dist			= NULL;
	local_env->dist_lllp		= NULL;
	local_env->mem_bind		= NULL;
	local_env->mem_bind_sort	= NULL;
	local_env->mem_bind_verbose	= NULL;
	local_env->ntasks		= NO_VAL;
	local_env->ntasks_per_core	= NO_VAL;
	local_env->ntasks_per_node	= NO_VAL;
	local_env->ntasks_per_socket	= NO_VAL;
	local_env->plane_size		= NO_VAL;
}

extern void set_envs(char ***array_ptr, sbatch_env_t *local_env,
		     int pack_offset)
{
	if ((local_env->cpus_per_task != NO_VAL) &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_CPUS_PER_TASK",
					  pack_offset, "%u",
					  local_env->cpus_per_task)) {
		error("Can't set SLURM_CPUS_PER_TASK env variable");
	}
	if (local_env->dist &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_DISTRIBUTION",
					  pack_offset, "%s",
					  local_env->dist)) {
		error("Can't set SLURM_DISTRIBUTION env variable");
	}
	if (local_env->mem_bind &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_MEM_BIND",
					  pack_offset, "%s",
					  local_env->mem_bind)) {
		error("Can't set SLURM_MEM_BIND env variable");
	}
	if (local_env->mem_bind_sort &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_MEM_BIND_SORT",
					  pack_offset, "%s",
					  local_env->mem_bind_sort)) {
		error("Can't set SLURM_MEM_BIND_SORT env variable");
	}
	if (local_env->mem_bind_verbose &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_MEM_BIND_VERBOSE",
					  pack_offset, "%s",
					  local_env->mem_bind_verbose)) {
		error("Can't set SLURM_MEM_BIND_VERBOSE env variable");
	}
	if (local_env->dist_lllp &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_DIST_LLLP",
					  pack_offset, "%s",
					  local_env->dist_lllp)) {
		error("Can't set SLURM_DIST_LLLP env variable");
	}
	if (local_env->ntasks != NO_VAL) {
		if (!env_array_overwrite_pack_fmt(array_ptr, "SLURM_NPROCS",
						  pack_offset, "%u",
						  local_env->ntasks))
			error("Can't set SLURM_NPROCS env variable");
		if (!env_array_overwrite_pack_fmt(array_ptr, "SLURM_NTASKS",
						  pack_offset, "%u",
						  local_env->ntasks))
			error("Can't set SLURM_NTASKS env variable");
	}
	if ((local_env->ntasks_per_core != NO_VAL) &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_NTASKS_PER_CORE",
					  pack_offset, "%u",
					  local_env->ntasks_per_core)) {
		error("Can't set SLURM_NTASKS_PER_CORE env variable");
	}
	if ((local_env->ntasks_per_node != NO_VAL) &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_NTASKS_PER_NODE",
					  pack_offset, "%u",
					  local_env->ntasks_per_node)) {
		error("Can't set SLURM_NTASKS_PER_NODE env variable");
	}
	if ((local_env->ntasks_per_socket != NO_VAL) &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_NTASKS_PER_SOCKET",
					  pack_offset, "%u",
					  local_env->ntasks_per_socket)) {
		error("Can't set SLURM_NTASKS_PER_SOCKET env variable");
	}
	if ((local_env->plane_size != NO_VAL) &&
	    !env_array_overwrite_pack_fmt(array_ptr, "SLURM_DIST_PLANESIZE",
					  pack_offset, "%u",
					  local_env->plane_size)) {
		error("Can't set SLURM_DIST_PLANESIZE env variable");
	}
}
