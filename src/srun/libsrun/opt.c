/*****************************************************************************\
 *  opt.c - options processing for srun
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

#include <ctype.h>		/* isdigit() */
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
#include "src/common/optz.h"
#include "src/common/parse_time.h"
#include "src/common/plugstack.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_mpi.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_acct_gather_profile.h"
#include "src/common/uid.h"
#include "src/common/x11_util.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/util-net.h"

#include "src/api/pmi_server.h"

#include "debugger.h"
#include "launch.h"
#include "multi_prog.h"
#include "opt.h"

/* generic OPT_ definitions -- mainly for use with env vars  */
#define OPT_NONE        0x00
#define OPT_INT         0x01
#define OPT_STRING      0x02
#define OPT_COMPRESS	0x07
#define OPT_RESV_PORTS	0x09
#define OPT_MPI         0x0c
#define OPT_CPU_BIND    0x0d
#define OPT_MULTI       0x0f
#define OPT_NSOCKETS    0x10
#define OPT_NCORES      0x11
#define OPT_OPEN_MODE   0x14
#define OPT_BCAST       0x1e
#define OPT_EXPORT	0x21
#define OPT_INT64	0x25

extern char **environ;

/*---- global variables, defined in opt.h ----*/
int	error_exit = 1;
int	immediate_exit = 1;
char *	mpi_type = NULL;
srun_opt_t sropt;
slurm_opt_t opt = { .srun_opt = &sropt };
List 	opt_list = NULL;
int	pass_number = 0;
time_t	srun_begin_time = 0;
bool	tres_bind_err_log = true;
bool	tres_freq_err_log = true;

/*---- forward declarations of static variables and functions  ----*/
typedef struct env_vars env_vars_t;
struct option long_options[] = {
	{"error",            required_argument, 0, 'e'},
	{"preserve-env",     no_argument,       0, 'E'},
	{"preserve-slurm-env", no_argument,     0, 'E'},
	{"input",            required_argument, 0, 'i'},
	{"kill-on-bad-exit", optional_argument, 0, 'K'},
	{"label",            no_argument,       0, 'l'},
	{"output",           required_argument, 0, 'o'},
	{"relative",         required_argument, 0, 'r'},
	{"threads",          required_argument, 0, 'T'},
	{"unbuffered",       no_argument,       0, 'u'},
	{"wait",             required_argument, 0, 'W'},
	{"disable-status",   no_argument,       0, 'X'},
	{"no-allocate",      no_argument,       0, 'Z'},
	{"accel-bind",       required_argument, 0, LONG_OPT_ACCEL_BIND},
	{"bcast",            optional_argument, 0, LONG_OPT_BCAST},
	{"checkpoint",       required_argument, 0, LONG_OPT_CHECKPOINT},
	{"compress",         optional_argument, 0, LONG_OPT_COMPRESS},
	{"cores-per-socket", required_argument, 0, LONG_OPT_CORESPERSOCKET},
	{"cpu-bind",         required_argument, 0, LONG_OPT_CPU_BIND},
	{"debugger-test",    no_argument,       0, LONG_OPT_DEBUG_TS},
	{"epilog",           required_argument, 0, LONG_OPT_EPILOG},
	{"export",           required_argument, 0, LONG_OPT_EXPORT},
	{"jobid",            required_argument, 0, LONG_OPT_JOBID},
	{"mpi",              required_argument, 0, LONG_OPT_MPI},
	{"msg-timeout",      required_argument, 0, LONG_OPT_TIMEO},
	{"multi-prog",       no_argument,       0, LONG_OPT_MULTI},
	{"ntasks-per-node",  required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"ntasks-per-socket",required_argument, 0, LONG_OPT_NTASKSPERSOCKET},
	{"open-mode",        required_argument, 0, LONG_OPT_OPEN_MODE},
	{"pack-group",       required_argument, 0, LONG_OPT_PACK_GROUP},
	{"prolog",           required_argument, 0, LONG_OPT_PROLOG},
	{"propagate",        optional_argument, 0, LONG_OPT_PROPAGATE},
	{"pty",              no_argument,       0, LONG_OPT_PTY},
	{"quit-on-interrupt",no_argument,       0, LONG_OPT_QUIT_ON_INTR},
	{"restart-dir",      required_argument, 0, LONG_OPT_RESTART_DIR},
	{"resv-ports",       optional_argument, 0, LONG_OPT_RESV_PORTS},
	{"slurmd-debug",     required_argument, 0, LONG_OPT_DEBUG_SLURMD},
	{"sockets-per-node", required_argument, 0, LONG_OPT_SOCKETSPERNODE},
	{"task-epilog",      required_argument, 0, LONG_OPT_TASK_EPILOG},
	{"task-prolog",      required_argument, 0, LONG_OPT_TASK_PROLOG},
	{"tasks-per-node",   required_argument, 0, LONG_OPT_NTASKSPERNODE},
	{"test-only",        no_argument,       0, LONG_OPT_TEST_ONLY},
	{"threads-per-core", required_argument, 0, LONG_OPT_THREADSPERCORE},
	{NULL,               0,                 0, 0}
	};
char *opt_string =
	"+A:b:B:c:C:d:D:e:EF:G:hHi:I::jJ:k::K::lL:m:M:n:N:o:Op:q:Qr:sS:t:T:uvVw:W:x:XZ";

static slurm_opt_t *_get_first_opt(int pack_offset);
static slurm_opt_t *_get_next_opt(int pack_offset, slurm_opt_t *opt_last);

/* Get a decimal integer from arg */
static int  _get_int(const char *arg, const char *what, bool positive);

static bitstr_t *_get_pack_group(const int argc, char **argv,
				 int default_pack_offset, bool *opt_found);

/* fill in default options  */
static void _opt_default(void);

/* set options based upon env vars  */
static void _opt_env(int pack_offset);

static void _opt_args(int argc, char **argv, int pack_offset);

/* verify options sanity  */
static bool _opt_verify(void);

static void  _process_env_var(env_vars_t *e, const char *val);
static void  _set_options(const int argc, char **argv);
static bool  _under_parallel_debugger(void);
static bool  _valid_node_list(char **node_list_pptr);

/*---[ end forward declarations of static functions ]---------------------*/

/*
 * Find first option structure for a given pack job offset
 * pack_offset IN - Offset into pack job or -1 if regular job
 * RET - Pointer to option structure or NULL if none found
 */
static slurm_opt_t *_get_first_opt(int pack_offset)
{
	ListIterator opt_iter;
	slurm_opt_t *opt_local;

	if (!opt_list) {
		if (!sropt.pack_grp_bits && (pack_offset == -1))
			return &opt;
		if (sropt.pack_grp_bits &&
		    (pack_offset >= 0) &&
		    (pack_offset < bit_size(sropt.pack_grp_bits)) &&
		    bit_test(sropt.pack_grp_bits, pack_offset))
			return &opt;
		return NULL;
	}

	opt_iter = list_iterator_create(opt_list);
	while ((opt_local = list_next(opt_iter))) {
		srun_opt_t *srun_opt = opt_local->srun_opt;
		xassert(srun_opt);
		if (srun_opt->pack_grp_bits && (pack_offset >= 0)
		    && (pack_offset < bit_size(srun_opt->pack_grp_bits))
		    && bit_test(srun_opt->pack_grp_bits, pack_offset))
			break;
	}
	list_iterator_destroy(opt_iter);

	return opt_local;
}

/*
 * Find next option structure for a given pack job offset
 * pack_offset IN - Offset into pack job or -1 if regular job
 * opt_last IN - past option structure found for this pack offset
 * RET - Pointer to option structure or NULL if none found
 */
static slurm_opt_t *_get_next_opt(int pack_offset, slurm_opt_t *opt_last)
{
	ListIterator opt_iter;
	slurm_opt_t *opt_local;
	bool found_last = false;

	if (!opt_list)
		return NULL;

	opt_iter = list_iterator_create(opt_list);
	while ((opt_local = list_next(opt_iter))) {
		srun_opt_t *srun_opt = opt_local->srun_opt;
		xassert(srun_opt);
		if (!found_last) {
			if (opt_last == opt_local)
				found_last = true;
			continue;
		}

		if (srun_opt->pack_grp_bits && (pack_offset >= 0)
		    && (pack_offset < bit_size(srun_opt->pack_grp_bits))
		    && bit_test(srun_opt->pack_grp_bits, pack_offset))
			break;
	}
	list_iterator_destroy(opt_iter);

	return opt_local;
}

/*
 * Find option structure for a given pack job offset
 * pack_offset IN - Offset into pack job, -1 if regular job, -2 to reset
 * RET - Pointer to next matching option structure or NULL if none found
 */
extern slurm_opt_t *get_next_opt(int pack_offset)
{
	static int offset_last = -2;
	static slurm_opt_t *opt_last = NULL;

	if (pack_offset == -2) {
		offset_last = -2;
		opt_last = NULL;
		return NULL;
	}

	if (offset_last != pack_offset) {
		offset_last = pack_offset;
		opt_last = _get_first_opt(pack_offset);
	} else {
		opt_last = _get_next_opt(pack_offset, opt_last);
	}
	return opt_last;
}

/*
 * Return maximum pack_group value for any step launch option request
 */
extern int get_max_pack_group(void)
{
	ListIterator opt_iter;
	slurm_opt_t *opt_local;
	int max_pack_offset = 0, pack_offset = 0;

	if (opt_list) {
		opt_iter = list_iterator_create(opt_list);
		while ((opt_local = list_next(opt_iter))) {
			srun_opt_t *srun_opt = opt_local->srun_opt;
			xassert(srun_opt);
			if (srun_opt->pack_grp_bits)
				pack_offset = bit_fls(srun_opt->pack_grp_bits);
			if (pack_offset >= max_pack_offset)
				max_pack_offset = pack_offset;
		}
		list_iterator_destroy(opt_iter);
	} else {
		if (sropt.pack_grp_bits)
			max_pack_offset = bit_fls(sropt.pack_grp_bits);
	}

	return max_pack_offset;
}

/*
 * Copy the last option record:
 * Copy strings if the original values will be preserved and
 *   reused for additional heterogeneous job/steps
 * Otherwise clear/NULL the pointer so it does not get re-used
 *   and freed, which will render the copied pointer bad
 */
static slurm_opt_t *_opt_copy(void)
{
	slurm_opt_t *opt_dup;
	int i;

	opt_dup = xmalloc(sizeof(slurm_opt_t));
	memcpy(opt_dup, &opt, sizeof(slurm_opt_t));
	opt_dup->srun_opt = xmalloc(sizeof(srun_opt_t));
	memcpy(opt_dup->srun_opt, &sropt, sizeof(srun_opt_t));

	opt_dup->account = xstrdup(opt.account);
	opt_dup->acctg_freq = xstrdup(opt.acctg_freq);
	opt_dup->srun_opt->argv = xmalloc(sizeof(char *) * sropt.argc);
	for (i = 0; i < sropt.argc; i++)
		opt_dup->srun_opt->argv[i] = xstrdup(sropt.argv[i]);
	sropt.bcast_file = NULL;	/* Moved by memcpy */
	opt.burst_buffer = NULL;	/* Moved by memcpy */
	opt_dup->c_constraint = xstrdup(opt.c_constraint);
	opt_dup->srun_opt->ckpt_interval_str =
		xstrdup(sropt.ckpt_interval_str);
	opt_dup->clusters = xstrdup(opt.clusters);
	opt_dup->srun_opt->cmd_name = xstrdup(sropt.cmd_name);
	opt_dup->comment = xstrdup(opt.comment);
	opt.constraint = NULL;		/* Moved by memcpy */
	opt_dup->srun_opt->cpu_bind = xstrdup(sropt.cpu_bind);
	opt_dup->chdir = xstrdup(opt.chdir);
	opt_dup->dependency = xstrdup(opt.dependency);
	opt_dup->srun_opt->efname = xstrdup(sropt.efname);
	opt_dup->srun_opt->epilog = xstrdup(sropt.epilog);
	opt_dup->exclude = xstrdup(opt.exclude);
	opt_dup->srun_opt->export_env = xstrdup(sropt.export_env);
	opt_dup->extra = xstrdup(opt.extra);
	opt.gres = NULL;		/* Moved by memcpy */
	opt_dup->gpus = xstrdup(opt.gpus);
	opt_dup->gpu_bind = xstrdup(opt.gpu_bind);
	opt_dup->gpu_freq = xstrdup(opt.gpu_freq);
	opt_dup->gpus_per_node = xstrdup(opt.gpus_per_node);
	opt_dup->gpus_per_socket = xstrdup(opt.gpus_per_socket);
	opt_dup->gpus_per_task = xstrdup(opt.gpus_per_task);
	sropt.hostfile = NULL;		/* Moved by memcpy */
	opt_dup->srun_opt->ifname = xstrdup(sropt.ifname);
	opt_dup->job_name = xstrdup(opt.job_name);
	opt_dup->srun_opt->ofname = xstrdup(sropt.ofname);
	opt.licenses = NULL;		/* Moved by memcpy */
	opt.mail_user = NULL;		/* Moved by memcpy */
	opt_dup->mcs_label = xstrdup(opt.mcs_label);
	opt.mem_bind = NULL;		/* Moved by memcpy */
	opt_dup->mpi_type = xstrdup(opt.mpi_type);
	opt.network = NULL;		/* Moved by memcpy */
	opt.nodelist = NULL;		/* Moved by memcpy */
	sropt.pack_group = NULL;	/* Moved by memcpy */
	sropt.pack_grp_bits = NULL;	/* Moved by memcpy */
	opt.partition = NULL;		/* Moved by memcpy */
	opt_dup->srun_opt->prolog = xstrdup(sropt.prolog);
	opt_dup->srun_opt->propagate = xstrdup(sropt.propagate);
	opt_dup->qos = xstrdup(opt.qos);
	opt_dup->reservation = xstrdup(opt.reservation);
	opt.spank_job_env = NULL;	/* Moved by memcpy */
	opt_dup->srun_opt->task_epilog = xstrdup(sropt.task_epilog);
	opt_dup->srun_opt->task_prolog = xstrdup(sropt.task_prolog);
	opt_dup->tres_bind = xstrdup(opt.tres_bind);
	opt_dup->tres_freq = xstrdup(opt.tres_freq);
	opt_dup->wckey = xstrdup(opt.wckey);

	return opt_dup;
}

/*
 * process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 *
 * argc IN - Count of elements in argv
 * argv IN - Array of elements to parse
 * argc_off OUT - Offset of first non-parsable element
 */
extern int initialize_and_process_args(int argc, char **argv, int *argc_off)
{
	static int default_pack_offset = 0;
	static bool pending_append = false;
	bitstr_t *pack_grp_bits;
	int i, i_first, i_last;
	bool opt_found = false;

	pack_grp_bits = _get_pack_group(argc, argv, default_pack_offset++,
					&opt_found);
	i_first = bit_ffs(pack_grp_bits);
	i_last  = bit_fls(pack_grp_bits);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(pack_grp_bits, i))
			continue;
		pass_number++;
		if (pending_append) {
			if (!opt_list)
				opt_list = list_create(NULL);
			list_append(opt_list, _opt_copy());
			pending_append = false;
		}

		/* initialize option defaults */
		_opt_default();
		if (opt_found || (i > 0)) {
			xstrfmtcat(sropt.pack_group, "%d", i);
			sropt.pack_grp_bits = bit_alloc(MAX_PACK_COUNT);
			bit_set(sropt.pack_grp_bits, i);
		}

		/* initialize options with env vars */
		_opt_env(i);

		/* initialize options with argv */
		_set_options(argc, argv);
		_opt_args(argc, argv, i);

		if (argc_off)
			*argc_off = optind;

		if (!_opt_verify())
			exit(error_exit);

		if (opt.verbose)
			slurm_print_set_options(&opt);

		if (spank_init_post_opt() < 0) {
			error("Plugin stack post-option processing failed.");
			exit(error_exit);
		}
		pending_append = true;
	}
	bit_free(pack_grp_bits);

	if (opt_list && pending_append) {		/* Last record */
		list_append(opt_list, _opt_copy());
		pending_append = false;
	}

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
 * _opt_default(): used by initialize_and_process_args to set defaults
 */
static void _opt_default(void)
{
	if (pass_number == 1) {
		sropt.allocate		= false;
		sropt.ckpt_interval		= 0;
		xfree(sropt.ckpt_interval_str);
		xfree(sropt.cmd_name);
		sropt.debugger_test	= false;
		sropt.disable_status	= false;
		xfree(sropt.efname);
		xfree(sropt.epilog);
		sropt.epilog		= slurm_get_srun_epilog();
		xfree(sropt.export_env);
		xfree(sropt.ifname);
		sropt.jobid		= NO_VAL;
		sropt.kill_bad_exit	= NO_VAL;
		sropt.labelio		= false;
		sropt.max_wait		= slurm_get_wait_time();
		/* Default launch msg timeout           */
		sropt.msg_timeout		= slurm_get_msg_timeout();
		sropt.no_alloc		= false;
		sropt.noshell		= false;
		xfree(sropt.ofname);
		sropt.open_mode		= 0;
		sropt.parallel_debug	= false;
		sropt.pty			= false;
		sropt.preserve_env	= false;
		xfree(sropt.prolog);
		sropt.prolog		= slurm_get_srun_prolog();
		xfree(sropt.propagate); 	 /* propagate specific rlimits */
		sropt.quit_on_intr	= false;
		sropt.slurmd_debug	= LOG_LEVEL_QUIET;
		xfree(sropt.task_epilog);
		xfree(sropt.task_prolog);
		sropt.test_only		= false;
		sropt.test_exec		= false;
		sropt.unbuffered	= false;
		sropt.user_managed_io	= false;
	}

	/*
	 * All other options must be specified individually for each component
	 * of the job/step. Do not use xfree() as the pointers have been copied.
	 * See initialize_and_process_args() above.
	 */
	sropt.accel_bind_type		= 0;
	sropt.bcast_file		= NULL;
	sropt.bcast_flag		= false;
	sropt.accel_bind_type		= 0;
	sropt.compress			= 0;
	opt.cores_per_socket		= NO_VAL; /* requested cores */
	sropt.cpu_bind			= NULL;
	sropt.cpu_bind_type		= 0;
	sropt.cpu_bind_type_set		= false;
	sropt.hostfile			= NULL;
	opt.job_flags			= 0;
	sropt.max_threads		= MAX_THREADS;
	pmi_server_max_threads(sropt.max_threads);
	sropt.multi_prog			= false;
	sropt.multi_prog_cmds		= 0;
	opt.ntasks_per_node		= NO_VAL; /* ntask max limits */
	opt.ntasks_per_socket		= NO_VAL;
	sropt.pack_group		= NULL;
	sropt.pack_grp_bits		= NULL;
	sropt.relative			= NO_VAL;
	sropt.relative_set		= false;
	sropt.resv_port_cnt		= NO_VAL;
	opt.sockets_per_node		= NO_VAL; /* requested sockets */
	opt.spank_job_env_size		= 0;
	opt.spank_job_env		= NULL;
	opt.threads_per_core		= NO_VAL; /* requested threads */

	/*
	 * Reset some default values if running under a parallel debugger
	 */
	if ((sropt.parallel_debug = _under_parallel_debugger())) {
		sropt.max_threads		= 1;
		pmi_server_max_threads(sropt.max_threads);
		sropt.msg_timeout		= 15;
	}

	slurm_reset_all_options(&opt, (pass_number == 1));
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
{"SLURMD_DEBUG",        OPT_INT,        &sropt.slurmd_debug,NULL             },
  { "SLURM_ACCOUNT", 'A' },
  { "SLURM_ACCTG_FREQ", LONG_OPT_ACCTG_FREQ },
{"SLURM_BCAST",         OPT_BCAST,      NULL,               NULL             },
  { "SLURM_BURST_BUFFER", LONG_OPT_BURST_BUFFER_SPEC },
  { "SLURM_CLUSTERS", 'M' },
{"SLURM_CHECKPOINT",    OPT_STRING,     &sropt.ckpt_interval_str, NULL       },
  { "SLURM_CLUSTER_CONSTRAINT", LONG_OPT_CLUSTER_CONSTRAINT },
{"SLURM_COMPRESS",      OPT_COMPRESS,   NULL,               NULL             },
  { "SLURM_CONSTRAINT", 'C' },
  { "SLURM_CORE_SPEC", 'S' },
  { "SLURM_CPUS_PER_TASK", 'c' },
{"SLURM_CPU_BIND",      OPT_CPU_BIND,   NULL,               NULL             },
  { "SLURM_CPU_FREQ_REQ", LONG_OPT_CPU_FREQ },
  { "SLURM_CPUS_PER_GPU", LONG_OPT_CPUS_PER_GPU },
  { "SLURM_DELAY_BOOT", LONG_OPT_DELAY_BOOT },
  { "SLURM_DEPENDENCY", 'd' },
{"SLURM_DISABLE_STATUS",OPT_INT,        &sropt.disable_status,NULL           },
  { "SLURM_DISTRIBUTION", 'm' },
{"SLURM_EPILOG",        OPT_STRING,     &sropt.epilog,      NULL             },
  { "SLURM_EXCLUSIVE", LONG_OPT_EXCLUSIVE },
{"SLURM_EXPORT_ENV",    OPT_STRING,     &sropt.export_env,  NULL             },
  { "SLURM_GPUS", 'G' },
  { "SLURM_GPU_BIND", LONG_OPT_GPU_BIND },
  { "SLURM_GPU_FREQ", LONG_OPT_GPU_FREQ },
  { "SLURM_GPUS_PER_NODE", LONG_OPT_GPUS_PER_NODE },
  { "SLURM_GPUS_PER_SOCKET", LONG_OPT_GPUS_PER_SOCKET },
  { "SLURM_GPUS_PER_TASK", LONG_OPT_GPUS_PER_TASK },
  { "SLURM_GRES", LONG_OPT_GRES },
  { "SLURM_GRES_FLAGS", LONG_OPT_GRES_FLAGS },
  { "SLURM_HINT", LONG_OPT_HINT },
{"SLURM_JOB_ID",        OPT_INT,        &sropt.jobid,       NULL             },
  { "SLURM_JOB_NAME", 'J' },
  { "SLURM_JOB_NODELIST", LONG_OPT_ALLOC_NODELIST },
  { "SLURM_JOB_NUM_NODES", 'N' },
{"SLURM_KILL_BAD_EXIT", OPT_INT,        &sropt.kill_bad_exit,NULL            },
{"SLURM_LABELIO",       OPT_INT,        &sropt.labelio,     NULL             },
  { "SLURM_MEM_PER_GPU", LONG_OPT_MEM_PER_GPU },
  { "SLURM_MEM_BIND", LONG_OPT_MEM_BIND },
  { "SLURM_MEM_PER_CPU", LONG_OPT_MEM_PER_CPU },
  { "SLURM_MEM_PER_NODE", LONG_OPT_MEM },
{"SLURM_MPI_TYPE",      OPT_MPI,        NULL,               NULL             },
{"SLURM_NCORES_PER_SOCKET",OPT_NCORES,  NULL,               NULL             },
  { "SLURM_NETWORK", LONG_OPT_NETWORK },
  { "SLURM_NO_KILL", 'k' },
  { "SLURM_NPROCS", 'n' },	/* deprecated, should be removed */
				/* listed first so SLURM_NTASKS overrides */
  { "SLURM_NTASKS", 'n' },
{"SLURM_NSOCKETS_PER_NODE",OPT_NSOCKETS,NULL,               NULL             },
{"SLURM_NTASKS_PER_NODE", OPT_INT,      &opt.ntasks_per_node,NULL            },
{"SLURM_OPEN_MODE",     OPT_OPEN_MODE,  NULL,               NULL             },
  { "SLURM_OVERCOMMIT", 'O' },
  { "SLURM_PARTITION", 'p' },
  { "SLURM_POWER", LONG_OPT_POWER },
  { "SLURM_PROFILE", LONG_OPT_PROFILE },
{"SLURM_PROLOG",        OPT_STRING,     &sropt.prolog,      NULL             },
  { "SLURM_QOS", 'q' },
  { "SLURM_REMOTE_CWD", 'D' },
  { "SLURM_REQ_SWITCH", LONG_OPT_SWITCH_REQ },
  { "SLURM_RESERVATION", LONG_OPT_RESERVATION },
{"SLURM_RESV_PORTS",    OPT_RESV_PORTS, NULL,               NULL             },
  { "SLURM_SIGNAL", LONG_OPT_SIGNAL },
  { "SLURM_SPREAD_JOB", LONG_OPT_SPREAD_JOB },
{"SLURM_SRUN_MULTI",    OPT_MULTI,      NULL,               NULL             },
{"SLURM_STDERRMODE",    OPT_STRING,     &sropt.efname,      NULL             },
{"SLURM_STDINMODE",     OPT_STRING,     &sropt.ifname,      NULL             },
{"SLURM_STDOUTMODE",    OPT_STRING,     &sropt.ofname,      NULL             },
{"SLURM_TASK_EPILOG",   OPT_STRING,     &sropt.task_epilog, NULL             },
{"SLURM_TASK_PROLOG",   OPT_STRING,     &sropt.task_prolog, NULL             },
  { "SLURM_THREAD_SPEC", LONG_OPT_THREAD_SPEC },
{"SLURM_THREADS",       OPT_INT,        &sropt.max_threads, NULL             },
  { "SLURM_TIMELIMIT", 't' },
{"SLURM_UNBUFFEREDIO",  OPT_INT,        &sropt.unbuffered,  NULL             },
  { "SLURM_USE_MIN_NODES", LONG_OPT_USE_MIN_NODES },
{"SLURM_WAIT",          OPT_INT,        &sropt.max_wait,    NULL             },
  { "SLURM_WAIT4SWITCH", LONG_OPT_SWITCH_WAIT },
  { "SLURM_WCKEY", LONG_OPT_WCKEY },
  { "SLURM_WORKING_DIR", 'D' },
{NULL, 0, NULL, NULL}
};


/*
 * _opt_env(): used by initialize_and_process_args to set options via
 *            environment variables. See comments above for how to
 *            extend srun to process different vars
 */
static void _opt_env(int pack_offset)
{
	char       key[64], *val = NULL;
	env_vars_t *e   = env_vars;

	while (e->var) {
		if ((val = getenv(e->var)))
			_process_env_var(e, val);
		if ((pack_offset >= 0) &&
		    strcmp(e->var, "SLURM_JOBID") &&
		    strcmp(e->var, "SLURM_JOB_ID")) {
			snprintf(key, sizeof(key), "%s_PACK_GROUP_%d",
				 e->var, pack_offset);
			if ((val = getenv(key)))
				_process_env_var(e, val);
		}
		e++;
	}

	/* Running srun within an existing srun. Don't inherit values. */
	if (getenv("SLURM_STEP_ID")) {
		xfree(sropt.cpu_bind);
		sropt.cpu_bind_type = 0;
		xfree(opt.mem_bind);
		opt.mem_bind_type = 0;
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

	case OPT_COMPRESS:
		sropt.compress = parse_compress_type(val);
		break;
	case OPT_CPU_BIND:
		xfree(sropt.cpu_bind);
		if (slurm_verify_cpu_bind(val, &sropt.cpu_bind,
					  &sropt.cpu_bind_type, 0))
			exit(error_exit);
		break;
	case OPT_EXPORT:
		xfree(sropt.export_env);
		sropt.export_env = xstrdup(val);
		break;

	case OPT_BCAST:
		if (val) {
			xfree(sropt.bcast_file);
			sropt.bcast_file = xstrdup(val);
		}
		sropt.bcast_flag = true;
		break;

	case OPT_RESV_PORTS:
		if (val)
			sropt.resv_port_cnt = strtol(val, NULL, 10);
		else
			sropt.resv_port_cnt = 0;
		break;

	case OPT_OPEN_MODE:
		if ((val[0] == 'a') || (val[0] == 'A'))
			sropt.open_mode = OPEN_MODE_APPEND;
		else if ((val[0] == 't') || (val[0] == 'T'))
			sropt.open_mode = OPEN_MODE_TRUNCATE;
		else
			error("Invalid SLURM_OPEN_MODE: %s. Ignored", val);
		break;
	case OPT_MPI:
		xfree(mpi_type);
		mpi_type = xstrdup(val);
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

/*
 *  Get a decimal integer from arg.
 *
 *  Returns the integer on success, exits program on failure.
 *
 */
static int
_get_int(const char *arg, const char *what, bool positive)
{
	return parse_int(what, arg, positive);
}

/*
 * If --pack-group option found, return a bitmap representing their IDs
 * argc IN - Argument count
 * argv IN - Arguments
 * default_pack_offset IN - Default offset
 * opt_found OUT - Set to true if --pack-group option found
 * RET bitmap if pack groups to run
 */
static bitstr_t *_get_pack_group(const int argc, char **argv,
				 int default_pack_offset, bool *opt_found)
{
	int i, opt_char, option_index = 0;
	char *tmp = NULL;
	bitstr_t *pack_grp_bits = bit_alloc(MAX_PACK_COUNT);
	hostlist_t hl;
	struct option *common_options, *optz;

	common_options = slurm_option_table_create(long_options, &opt);
	optz = spank_option_table_create(common_options);
	slurm_option_table_destroy(common_options);
	if (!optz) {
		error("Unable to create option table");
		exit(error_exit);
	}

	*opt_found = false;
	optind = 0;
	while ((opt_char = getopt_long(argc, argv, opt_string,
				       optz, &option_index)) != -1) {
		switch (opt_char) {
		case LONG_OPT_PACK_GROUP:
			xfree(sropt.pack_group);
			sropt.pack_group = xstrdup(optarg);
			*opt_found = true;
		}
	}
	spank_option_table_destroy(optz);

	if (*opt_found == false) {
		bit_set(pack_grp_bits, default_pack_offset);
		return pack_grp_bits;
	}

	if (sropt.pack_group[0] == '[')
		tmp = xstrdup(sropt.pack_group);
	else
		xstrfmtcat(tmp, "[%s]", sropt.pack_group);
	hl = hostlist_create(tmp);
	if (!hl) {
		error("Invalid --pack-group value: %s", sropt.pack_group);
		exit(error_exit);
	}
	xfree(tmp);

	while ((tmp = hostlist_shift(hl))) {
		char *end_ptr = NULL;
		i = strtol(tmp, &end_ptr, 10);
		if ((i < 0) || (i >= MAX_PACK_COUNT) || (end_ptr[0] != '\0')) {
			error("Invalid --pack-group value: %s",
			       sropt.pack_group);
			exit(error_exit);
		}
		bit_set(pack_grp_bits, i);
		free(tmp);
	}
	hostlist_destroy(hl);
	if (bit_ffs(pack_grp_bits) == -1) {	/* No bits set */
		error("Invalid --pack-group value: %s", sropt.pack_group);
		exit(error_exit);
	}

	return pack_grp_bits;
}

static void _set_options(const int argc, char **argv)
{
	int opt_char, option_index = 0, max_val = 0;
	struct utsname name;

#ifdef HAVE_PTY_H
	char *tmp_str;
#endif
	struct option *common_options = slurm_option_table_create(long_options,
								  &opt);
	struct option *optz = spank_option_table_create(common_options);
	slurm_option_table_destroy(common_options);

	if (!optz) {
		error("Unable to create option table");
		exit(error_exit);
	}

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, opt_string,
				       optz, &option_index)) != -1) {
		switch (opt_char) {
		case (int)'e':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (sropt.pty) {
				fatal("--error incompatible with --pty "
				      "option");
				exit(error_exit);
			}
			xfree(sropt.efname);
			if (xstrcasecmp(optarg, "none") == 0)
				sropt.efname = xstrdup("/dev/null");
			else
				sropt.efname = xstrdup(optarg);
			break;
		case (int)'E':
			sropt.preserve_env = true;
			break;
		case (int)'i':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (sropt.pty) {
				fatal("--input incompatible with --pty option");
				exit(error_exit);
			}
			xfree(sropt.ifname);
			if (xstrcasecmp(optarg, "none") == 0)
				sropt.ifname = xstrdup("/dev/null");
			else
				sropt.ifname = xstrdup(optarg);
			break;
		case (int)'K':
			if (optarg)
				sropt.kill_bad_exit = strtol(optarg, NULL, 10);
			else
				sropt.kill_bad_exit = 1;
			break;
		case (int)'l':
			sropt.labelio = true;
			break;
		case (int)'o':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (sropt.pty) {
				error("--output incompatible with --pty "
				      "option");
				exit(error_exit);
			}
			xfree(sropt.ofname);
			if (xstrcasecmp(optarg, "none") == 0)
				sropt.ofname = xstrdup("/dev/null");
			else
				sropt.ofname = xstrdup(optarg);
			break;
		case (int)'r':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			sropt.relative = _get_int(optarg, "relative", false);
			sropt.relative_set = true;
			break;
		case (int)'T':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			sropt.max_threads =
				_get_int(optarg, "max_threads", true);
			pmi_server_max_threads(sropt.max_threads);
			break;
		case (int)'u':
			sropt.unbuffered = true;
			break;
		case (int)'W':
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			sropt.max_wait = _get_int(optarg, "wait", false);
			break;
		case (int)'X':
			sropt.disable_status = true;
			break;
		case (int)'Z':
			sropt.no_alloc = true;
			uname(&name);
			break;
		case LONG_OPT_EXPORT:
			xfree(sropt.export_env);
			sropt.export_env = xstrdup(optarg);
			break;
                case LONG_OPT_BCAST:
			if (optarg) {
				xfree(sropt.bcast_file);
				sropt.bcast_file = xstrdup(optarg);
			}
			sropt.bcast_flag = true;
                        break;
                case LONG_OPT_CPU_BIND:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sropt.cpu_bind);
			if (slurm_verify_cpu_bind(optarg, &sropt.cpu_bind,
						  &sropt.cpu_bind_type, 0))
				exit(error_exit);
			sropt.cpu_bind_type_set = true;
			break;
		case LONG_OPT_MPI:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(mpi_type);
			mpi_type = xstrdup(optarg);
			break;
		case LONG_OPT_PACK_GROUP:
			/* Already parsed in _get_pack_group() */
			break;
		case LONG_OPT_RESV_PORTS:
			if (optarg)
				sropt.resv_port_cnt = strtol(optarg, NULL, 10);
			else
				sropt.resv_port_cnt = 0;
			break;
		case LONG_OPT_JOBID:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			sropt.jobid = _get_int(optarg, "jobid", true);
			break;
		case LONG_OPT_TIMEO:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			sropt.msg_timeout =
				_get_int(optarg, "msg-timeout", true);
			break;
		case LONG_OPT_DEBUG_SLURMD:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (isdigit(optarg[0]))
				sropt.slurmd_debug =
					_get_int(optarg, "slurmd-debug", false);
			else
				sropt.slurmd_debug = log_string2num(optarg);
			break;
		case LONG_OPT_DEBUG_TS:
			sropt.debugger_test    = true;
			/* make other parameters look like debugger
			 * is really attached */
			sropt.parallel_debug   = true;
			sropt.max_threads     = 1;
			pmi_server_max_threads(sropt.max_threads);
			sropt.msg_timeout     = 15;
			break;
		case LONG_OPT_TEST_ONLY:
			sropt.test_only = true;
			break;
		case LONG_OPT_PROPAGATE:
			xfree(sropt.propagate);
			if (optarg)
				sropt.propagate = xstrdup(optarg);
			else
				sropt.propagate = xstrdup("ALL");
			break;
		case LONG_OPT_PROLOG:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sropt.prolog);
			sropt.prolog = xstrdup(optarg);
			break;
		case LONG_OPT_EPILOG:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sropt.epilog);
			sropt.epilog = xstrdup(optarg);
			break;
		case LONG_OPT_TASK_PROLOG:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sropt.task_prolog);
			sropt.task_prolog = xstrdup(optarg);
			break;
		case LONG_OPT_TASK_EPILOG:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			xfree(sropt.task_epilog);
			sropt.task_epilog = xstrdup(optarg);
			break;
		case LONG_OPT_MULTI:
			sropt.multi_prog = true;
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
			break;
		case LONG_OPT_NTASKSPERNODE:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			opt.ntasks_per_node = _get_int(optarg,
						       "ntasks-per-node", true);
			break;
		case LONG_OPT_NTASKSPERSOCKET:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			opt.ntasks_per_socket = _get_int(optarg,
							 "ntasks-per-socket",
							 true);
			break;
		case LONG_OPT_PTY:
#ifdef HAVE_PTY_H
			sropt.pty = true;
			sropt.unbuffered = true;	/* implicit */
			if (sropt.ifname)
				tmp_str = "--input";
			else if (sropt.ofname)
				tmp_str = "--output";
			else if (sropt.efname)
				tmp_str = "--error";
			else
				tmp_str = NULL;
			if (tmp_str) {
				error("%s incompatible with --pty option",
				      tmp_str);
				exit(error_exit);
			}
#else
			error("--pty not currently supported on this system "
			      "type, ignoring option");
#endif
			break;
		case LONG_OPT_CHECKPOINT:
			xfree(sropt.ckpt_interval_str);
			sropt.ckpt_interval_str = xstrdup(optarg);
			break;
		case LONG_OPT_OPEN_MODE:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if ((optarg[0] == 'a') || (optarg[0] == 'A'))
				sropt.open_mode = OPEN_MODE_APPEND;
			else if ((optarg[0] == 't') || (optarg[0] == 'T'))
				sropt.open_mode = OPEN_MODE_TRUNCATE;
			else {
				error("Invalid --open-mode argument: %s. Ignored",
				      optarg);
			}
			break;
		case LONG_OPT_ACCEL_BIND:
			if (!optarg)
				break;	/* Fix for Coverity false positive */
			if (strchr(optarg, 'v'))
				sropt.accel_bind_type |= ACCEL_BIND_VERBOSE;
			if (strchr(optarg, 'g'))
				sropt.accel_bind_type |= ACCEL_BIND_CLOSEST_GPU;
			if (strchr(optarg, 'm'))
				sropt.accel_bind_type |= ACCEL_BIND_CLOSEST_MIC;
			if (strchr(optarg, 'n'))
				sropt.accel_bind_type |= ACCEL_BIND_CLOSEST_NIC;
			break;
		case LONG_OPT_COMPRESS:
			sropt.compress = parse_compress_type(optarg);
			break;
		case LONG_OPT_QUIT_ON_INTR:
			sropt.quit_on_intr = true;
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
 * _opt_args() : set options via commandline args and popt
 */
static void _opt_args(int argc, char **argv, int pack_offset)
{
	int i, command_pos = 0, command_args = 0;
	char **rest = NULL;
	char *fullpath, *launch_params;

	sropt.pack_grp_bits = bit_alloc(MAX_PACK_COUNT);
	bit_set(sropt.pack_grp_bits, pack_offset);

	if ((opt.pn_min_memory != NO_VAL64) && (opt.mem_per_cpu != NO_VAL64)) {
		if (opt.pn_min_memory < opt.mem_per_cpu) {
			info("mem < mem-per-cpu - resizing mem to be equal "
			     "to mem-per-cpu");
			opt.pn_min_memory = opt.mem_per_cpu;
		}
		error("--mem and --mem-per-cpu are mutually exclusive.");
	}

#ifdef HAVE_NATIVE_CRAY
	/* only fatal on the allocation */
	if (opt.network && opt.shared && (sropt.jobid == NO_VAL))
		fatal("Requesting network performance counters requires "
		      "exclusive access.  Please add the --exclusive option "
		      "to your request.");
	if (opt.network)
		setenv("SLURM_NETWORK", opt.network, 1);
#endif

	if (opt.dependency)
		setenvfs("SLURM_JOB_DEPENDENCY=%s", opt.dependency);

	sropt.argc = 0;
	if (optind < argc) {
		rest = argv + optind;
		while ((rest[sropt.argc] != NULL) && strcmp(rest[sropt.argc], ":"))
			sropt.argc++;
	}

	command_args = sropt.argc;

	if (!xstrcmp(mpi_type, "list"))
		(void) mpi_hook_client_init(mpi_type);
	if (!rest && !sropt.test_only)
		fatal("No command given to execute.");

	if (launch_init() != SLURM_SUCCESS) {
		fatal("Unable to load launch plugin, check LaunchType "
		      "configuration");
	}
	command_pos = launch_g_setup_srun_opt(rest, &opt);

	/* make sure we have allocated things correctly */
	if (command_args)
		xassert((command_pos + command_args) <= sropt.argc);

	for (i = command_pos; i < sropt.argc; i++) {
		if (!rest || !rest[i-command_pos])
			break;
		sropt.argv[i] = xstrdup(rest[i-command_pos]);
		// info("argv[%d]='%s'", i, sropt.argv[i]);
	}
	sropt.argv[i] = NULL;	/* End of argv's (for possible execv) */

	if (getenv("SLURM_TEST_EXEC")) {
		sropt.test_exec = true;
	} else {
		launch_params = slurm_get_launch_params();
		if (launch_params && strstr(launch_params, "test_exec"))
			sropt.test_exec = true;
		xfree(launch_params);
	}

	if (sropt.test_exec) {
		/* Validate command's existence */
		if (sropt.prolog && xstrcasecmp(sropt.prolog, "none")) {
			if ((fullpath = search_path(opt.chdir, sropt.prolog,
						    true, R_OK|X_OK, true)))
				sropt.prolog = fullpath;
			else
				error("prolog '%s' not found in PATH or CWD (%s), or wrong permissions",
				      sropt.prolog, opt.chdir);
		}
		if (sropt.epilog && xstrcasecmp(sropt.epilog, "none")) {
			if ((fullpath = search_path(opt.chdir, sropt.epilog,
						    true, R_OK|X_OK, true)))
				sropt.epilog = fullpath;
			else
				error("epilog '%s' not found in PATH or CWD (%s), or wrong permissions",
				      sropt.epilog, opt.chdir);
		}
		if (sropt.task_prolog) {
			if ((fullpath = search_path(opt.chdir, sropt.task_prolog,
						    true, R_OK|X_OK, true)))
				sropt.task_prolog = fullpath;
			else
				error("task-prolog '%s' not found in PATH or CWD (%s), or wrong permissions",
				      sropt.task_prolog, opt.chdir);
		}
		if (sropt.task_epilog) {
			if ((fullpath = search_path(opt.chdir, sropt.task_epilog,
						    true, R_OK|X_OK, true)))
				sropt.task_epilog = fullpath;
			else
				error("task-epilog '%s' not found in PATH or CWD (%s), or wrong permissions",
				      sropt.task_epilog, opt.chdir);
		}
	}

	/* may exit() if an error with the multi_prog script */
	(void) launch_g_handle_multi_prog_verify(command_pos, &opt);

	if (!sropt.multi_prog && (sropt.test_exec || sropt.bcast_flag) &&
	    sropt.argv && sropt.argv[command_pos]) {

		if ((fullpath = search_path(opt.chdir, sropt.argv[command_pos],
					    true, X_OK, true))) {
			xfree(sropt.argv[command_pos]);
			sropt.argv[command_pos] = fullpath;
		} else {
			fatal("Can not execute %s", sropt.argv[command_pos]);
		}
	}
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
	if (sropt.slurmd_debug + LOG_LEVEL_ERROR > LOG_LEVEL_DEBUG2) {
		sropt.slurmd_debug = LOG_LEVEL_DEBUG2 - LOG_LEVEL_ERROR;
		info("Using srun's max debug increment of %d",
		     sropt.slurmd_debug);
	}

	if (opt.quiet && opt.verbose) {
		error ("don't specify both --verbose (-v) and --quiet (-Q)");
		verified = false;
	}

	if (opt.burst_buffer && opt.burst_buffer_file) {
		error("Cannot specify both --burst-buffer and --bbf");
		exit(error_exit);
	} else if (opt.burst_buffer_file) {
		Buf buf = create_mmap_buf(opt.burst_buffer_file);
		if (!buf) {
			error("Invalid --bbf specification");
			exit(error_exit);
		}
		opt.burst_buffer = xstrdup(get_buf_data(buf));
		free_buf(buf);
		xfree(opt.burst_buffer_file);
	}

	if (sropt.no_alloc && !opt.nodelist) {
		error("must specify a node list with -Z, --no-allocate.");
		verified = false;
	}

	if (sropt.no_alloc && opt.exclude) {
		error("can not specify --exclude list with -Z, --no-allocate.");
		verified = false;
	}

	if (sropt.no_alloc && sropt.relative_set) {
		error("do not specify -r,--relative with -Z,--no-allocate.");
		verified = false;
	}

	if (sropt.relative_set && (opt.exclude || opt.nodelist)) {
		error("-r,--relative not allowed with "
		      "-w,--nodelist or -x,--exclude.");
		verified = false;
	}

	/*
	 * This means --ntasks was read from the environment.
	 * We will override it with what the user specified in the hostlist.
	 */
	if (((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_ARBITRARY)) {
		if (slurm_option_set_by_env('n'))
			opt.ntasks_set = false;
		if (slurm_option_set_by_env('N'))
			opt.nodes_set = false;
	}

	if (opt.hint &&
	    ((sropt.cpu_bind_type == CPU_BIND_VERBOSE) ||
	     !sropt.cpu_bind_type_set) &&
	    (opt.ntasks_per_core == NO_VAL) &&
	    (opt.threads_per_core == NO_VAL)) {
		if (verify_hint(opt.hint,
				&opt.sockets_per_node,
				&opt.cores_per_socket,
				&opt.threads_per_core,
				&opt.ntasks_per_core,
				&sropt.cpu_bind_type)) {
			exit(error_exit);
		}
	}

	if (opt.cpus_set && (opt.pn_min_cpus < opt.cpus_per_task))
		opt.pn_min_cpus = opt.cpus_per_task;

	if ((sropt.argc > 0) && xstrcmp(sropt.argv[0], ":")) {
		xfree(sropt.cmd_name);
		sropt.cmd_name = base_name(sropt.argv[0]);
	}

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
			xfree(sropt.hostfile);
			sropt.hostfile = xstrdup(opt.nodelist);
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
		xfree(sropt.hostfile);
		if (strstr(opt.nodelist, "/"))
			sropt.hostfile = xstrdup(opt.nodelist);
		if (!_valid_node_list(&opt.nodelist))
			exit(error_exit);
	}

	/* set proc and node counts based on the arbitrary list of nodes */
	if (((opt.distribution & SLURM_DIST_STATE_BASE) == SLURM_DIST_ARBITRARY)
	   && (!opt.nodes_set || !opt.ntasks_set)) {
		hostlist_t hl = hostlist_create(opt.nodelist);
		if (!opt.ntasks_set) {
			opt.ntasks_set = true;
			opt.ntasks = hostlist_count(hl);
		}
		if (!opt.nodes_set) {
			opt.nodes_set = true;
			hostlist_uniq(hl);
			opt.min_nodes = opt.max_nodes = hostlist_count(hl);
		}
		hostlist_destroy(hl);
	}

	/*
	 * now if max is set make sure we have <= max_nodes in the
	 * nodelist but only if it isn't arbitrary since the user has
	 * laid it out how it should be so don't mess with it print an
	 * error later if it doesn't work the way they wanted
	 */
	if (opt.max_nodes && opt.nodelist &&
	    ((opt.distribution & SLURM_DIST_STATE_BASE)!=SLURM_DIST_ARBITRARY)) {
		hostlist_t hl = hostlist_create(opt.nodelist);
		int count = hostlist_count(hl);
		if (count > opt.max_nodes) {
			int i = 0;
			error("Required nodelist includes more nodes than "
			      "permitted by max-node count (%d > %d). "
			      "Eliminating nodes from the nodelist.",
			      count, opt.max_nodes);
			count -= opt.max_nodes;
			while (i<count) {
				char *name = hostlist_pop(hl);
				if (name)
					free(name);
				else
					break;
				i++;
			}
			xfree(opt.nodelist);
			opt.nodelist = hostlist_ranged_string_xmalloc(hl);
		}
		hostlist_destroy(hl);
	}

	/* check for realistic arguments */
	if (opt.ntasks <= 0) {
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

	if (!opt.ntasks_per_node) {
		error("ntasks-per-node is 0");
		verified = false;
	}


	/* bound max_threads/cores from ntasks_cores/sockets */
	if (opt.ntasks_per_core > 0) {
		/* if cpu_bind_type doesn't already have a auto pref,
		 * choose the level based on the level of ntasks
		 */
		if (!(sropt.cpu_bind_type & (CPU_BIND_TO_SOCKETS |
					   CPU_BIND_TO_CORES |
					   CPU_BIND_TO_THREADS |
					   CPU_BIND_TO_LDOMS |
					   CPU_BIND_TO_BOARDS))) {
			sropt.cpu_bind_type |= CPU_BIND_TO_CORES;
		}
	}
	if (opt.ntasks_per_socket > 0) {
		/* if cpu_bind_type doesn't already have a auto pref,
		 * choose the level based on the level of ntasks
		 */
		if (!(sropt.cpu_bind_type & (CPU_BIND_TO_SOCKETS |
					   CPU_BIND_TO_CORES |
					   CPU_BIND_TO_THREADS |
					   CPU_BIND_TO_LDOMS |
					   CPU_BIND_TO_BOARDS))) {
			sropt.cpu_bind_type |= CPU_BIND_TO_SOCKETS;
		}
	}

	/* massage the numbers */
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

	if ((opt.nodes_set || opt.extra_set)				&&
	    ((opt.min_nodes == opt.max_nodes) || (opt.max_nodes == 0))	&&
	    !opt.ntasks_set) {
		/* 1 proc / node default */
		opt.ntasks = opt.min_nodes;

		/* 1 proc / min_[socket * core * thread] default */
		if ((opt.sockets_per_node != NO_VAL) &&
		    (opt.cores_per_socket != NO_VAL) &&
		    (opt.threads_per_core != NO_VAL)) {
			opt.ntasks *= opt.sockets_per_node;
			opt.ntasks *= opt.cores_per_socket;
			opt.ntasks *= opt.threads_per_core;
			opt.ntasks_set = true;
		} else if (opt.ntasks_per_node != NO_VAL) {
			opt.ntasks *= opt.ntasks_per_node;
			opt.ntasks_set = true;
		}

		/* massage the numbers */
		if (opt.nodelist) {
			if (hl)	/* possibly built above */
				hostlist_destroy(hl);
			hl = hostlist_create(opt.nodelist);
			if (!hl) {
				error("memory allocation failure");
				exit(error_exit);
			}
			if (((opt.distribution & SLURM_DIST_STATE_BASE) ==
			     SLURM_DIST_ARBITRARY) && !opt.ntasks_set) {
				opt.ntasks = hostlist_count(hl);
				opt.ntasks_set = true;
			}
			hostlist_uniq(hl);
			hl_cnt = hostlist_count(hl);
			if (opt.nodes_set)
				opt.min_nodes = MAX(hl_cnt, opt.min_nodes);
			else
				opt.min_nodes = hl_cnt;
			/* Don't destroy hl here since it may be used later */
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
		if ((opt.ntasks < opt.min_nodes) && (opt.ntasks > 0)) {
			char *tmp = NULL;
			info ("Warning: can't run %d processes on %d "
			      "nodes, setting nnodes to %d",
			      opt.ntasks, opt.min_nodes, opt.ntasks);
			opt.min_nodes = opt.ntasks;
			if (opt.max_nodes
			    &&  (opt.min_nodes > opt.max_nodes) )
				opt.max_nodes = opt.min_nodes;
			/*
			 * This will force the set_by_env flag to false,
			 * which influences future decisions.
			 */
			xstrfmtcat(tmp, "%d", opt.min_nodes);
			slurm_process_option(&opt, 'N', tmp, false, false);
			xfree(tmp);
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

		if ((opt.ntasks_per_node != NO_VAL) && opt.min_nodes &&
		    (opt.ntasks_per_node != (opt.ntasks / opt.min_nodes))) {
			if (opt.ntasks > opt.ntasks_per_node)
				info("Warning: can't honor --ntasks-per-node "
				     "set to %u which doesn't match the "
				     "requested tasks %u with the number of "
				     "requested nodes %u. Ignoring "
				     "--ntasks-per-node.", opt.ntasks_per_node,
				     opt.ntasks, opt.min_nodes);
			opt.ntasks_per_node = NO_VAL;
		}

	} /* else if (opt.ntasks_set && !opt.nodes_set) */

	if ((opt.ntasks_per_node != NO_VAL) && (!opt.ntasks_set)) {
		opt.ntasks = opt.min_nodes * opt.ntasks_per_node;
		opt.ntasks_set = 1;
	}

	if (hl)
		hostlist_destroy(hl);

	if (sropt.max_threads <= 0) {	/* set default */
		error("Thread value invalid, reset to 1");
		sropt.max_threads = 1;
		pmi_server_max_threads(sropt.max_threads);
	} else if (sropt.max_threads > MAX_THREADS) {
		error("Thread value exceeds defined limit, reset to %d",
		      MAX_THREADS);
	}

	if ((opt.deadline) && (opt.begin) && (opt.deadline < opt.begin)) {
		error("Incompatible begin and deadline time specification");
		exit(error_exit);
	}
	if (sropt.ckpt_interval_str) {
		sropt.ckpt_interval = time_str2mins(sropt.ckpt_interval_str);
		if ((sropt.ckpt_interval < 0) &&
		    (sropt.ckpt_interval != INFINITE)) {
			error("Invalid checkpoint interval specification");
			exit(error_exit);
		}
	}

	if (!mpi_type)
		mpi_type = slurm_get_mpi_default();
	if (mpi_hook_client_init(mpi_type) == SLURM_ERROR) {
		error("invalid MPI type '%s', --mpi=list for acceptable types",
		      mpi_type);
		exit(error_exit);
	}

	if (!opt.job_name)
		opt.job_name = xstrdup(sropt.cmd_name);

	if (opt.x11) {
		x11_get_display(&opt.x11_target_port, &opt.x11_target);
		opt.x11_magic_cookie = x11_get_xauth();
	}

	return verified;
}

/* Initialize the spank_job_env based upon environment variables set
 *	via salloc or sbatch commands */
extern void init_spank_env(void)
{
	int i;
	char *name, *eq, *value;

	if (environ == NULL)
		return;

	for (i = 0; environ[i]; i++) {
		if (xstrncmp(environ[i], "SLURM_SPANK_", 12))
			continue;
		name = xstrdup(environ[i] + 12);
		eq = strchr(name, (int)'=');
		if (eq == NULL) {
			xfree(name);
			break;
		}
		eq[0] = '\0';
		value = eq + 1;
		spank_set_job_env(name, value, 1);
		xfree(name);
	}

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

	for (i = 0; i < opt.spank_job_env_size; i++) {
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

	for (i = 0; i < opt.spank_job_env_size; i++) {
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

	for (i = 0; i < opt.spank_job_env_size; i++) {
		if (xstrncmp(opt.spank_job_env[i], tmp_str, len))
			continue;
		xfree(opt.spank_job_env[i]);
		for (j = (i+1); j < opt.spank_job_env_size; i++, j++)
			opt.spank_job_env[i] = opt.spank_job_env[j];
		opt.spank_job_env_size--;
		if (opt.spank_job_env_size == 0)
			xfree(opt.spank_job_env);
		return 0;
	}

	return 0;	/* not found */
}

/* Determine if srun is under the control of a parallel debugger or not */
static bool _under_parallel_debugger (void)
{
	return (MPIR_being_debugged != 0);
}

extern void srun_usage(void)
{
 	printf(
"Usage: srun [-N nnodes] [-n ntasks] [-i in] [-o out] [-e err]\n"
"            [-c ncpus] [-r n] [-p partition] [--hold] [-t minutes]\n"
"            [-D path] [--immediate[=secs]] [--overcommit] [--no-kill]\n"
"            [--oversubscribe] [--label] [--unbuffered] [-m dist] [-J jobname]\n"
"            [--jobid=id] [--verbose] [--slurmd_debug=#] [--gres=list]\n"
"            [-T threads] [-W sec] [--checkpoint=time] [--gres-flags=opts]\n"
"            [--checkpoint-dir=dir] [--licenses=names] [--clusters=cluster_names]\n"
"            [--restart-dir=dir] [--qos=qos] [--time-min=minutes]\n"
"            [--contiguous] [--mincpus=n] [--mem=MB] [--tmp=MB] [-C list]\n"
"            [--mpi=type] [--account=name] [--dependency=type:jobid]\n"
"            [--kill-on-bad-exit] [--propagate[=rlimits] [--comment=name]\n"
"            [--cpu-bind=...] [--mem-bind=...] [--network=type]\n"
"            [--ntasks-per-node=n] [--ntasks-per-socket=n] [reservation=name]\n"
"            [--ntasks-per-core=n] [--mem-per-cpu=MB] [--preserve-env]\n"
"            [--profile=...]\n"
"            [--mail-type=type] [--mail-user=user] [--nice[=value]]\n"
"            [--prolog=fname] [--epilog=fname]\n"
"            [--task-prolog=fname] [--task-epilog=fname]\n"
"            [--ctrl-comm-ifhn=addr] [--multi-prog] [--mcs-label=mcs]\n"
"            [--cpu-freq=min[-max[:gov]] [--power=flags] [--spread-job]\n"
"            [--switches=max-switches{@max-time-to-wait}] [--reboot]\n"
"            [--core-spec=cores] [--thread-spec=threads]\n"
"            [--bb=burst_buffer_spec] [--bbf=burst_buffer_file]\n"
"            [--bcast=<dest_path>] [--compress[=library]]\n"
"            [--acctg-freq=<datatype>=<interval>] [--delay-boot=mins]\n"
"            [-w hosts...] [-x hosts...] [--use-min-nodes]\n"
"            [--mpi-combine=yes|no] [--pack-group=value]\n"
"            [--cpus-per-gpu=n] [--gpus=n] [--gpu-bind=...] [--gpu-freq=...]\n"
"            [--gpus-per-node=n] [--gpus-per-socket=n]  [--gpus-per-task=n]\n"
"            [--mem-per-gpu=MB]\n"
"            executable [args...]\n");

}

extern void srun_help(void)
{
	slurm_ctl_conf_t *conf;

        printf (
"Usage: srun [OPTIONS...] executable [args...]\n"
"\n"
"Parallel run options:\n"
"  -A, --account=name          charge job to specified account\n"
"      --acctg-freq=<datatype>=<interval> accounting and profiling sampling\n"
"                              intervals. Supported datatypes:\n"
"                              task=<interval> energy=<interval>\n"
"                              network=<interval> filesystem=<interval>\n"
"      --bb=<spec>             burst buffer specifications\n"
"      --bbf=<file_name>       burst buffer specification file\n"
"      --bcast=<dest_path>     Copy executable file to compute nodes\n"
"  -b, --begin=time            defer job until HH:MM MM/DD/YY\n"
"  -c, --cpus-per-task=ncpus   number of cpus required per task\n"
"      --checkpoint=time       job step checkpoint interval\n"
"      --checkpoint-dir=dir    directory to store job step checkpoint image \n"
"                              files\n"
"      --comment=name          arbitrary comment\n"
"      --compress[=library]    data compression library used with --bcast\n"
"      --cpu-freq=min[-max[:gov]] requested cpu frequency (and governor)\n"
"  -d, --dependency=type:jobid defer job until condition on jobid is satisfied\n"
"      --deadline=time         remove the job if no ending possible before\n"
"                              this deadline (start > (deadline - time[-min]))\n"
"      --delay-boot=mins       delay boot for desired node features\n"
"  -D, --chdir=path            change remote current working directory\n"
"      --export=env_vars|NONE  environment variables passed to launcher with\n"
"                              optional values or NONE (pass no variables)\n"
"  -e, --error=err             location of stderr redirection\n"
"      --epilog=program        run \"program\" after launching job step\n"
"  -E, --preserve-env          env vars for node and task counts override\n"
"                              command-line flags\n"
"      --gres=list             required generic resources\n"
"      --gres-flags=opts       flags related to GRES management\n"
"  -H, --hold                  submit job in held state\n"
"  -i, --input=in              location of stdin redirection\n"
"  -I, --immediate[=secs]      exit if resources not available in \"secs\"\n"
"      --jobid=id              run under already allocated job\n"
"  -J, --job-name=jobname      name of job\n"
"  -k, --no-kill               do not kill job on node failure\n"
"  -K, --kill-on-bad-exit      kill the job if any task terminates with a\n"
"                              non-zero exit code\n"
"  -l, --label                 prepend task number to lines of stdout/err\n"
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
"      --mpi=type              type of MPI being used\n"
"      --multi-prog            if set the program name specified is the\n"
"                              configuration specification for multiple programs\n"
"  -n, --ntasks=ntasks         number of tasks to run\n"
"      --nice[=value]          decrease scheduling priority by value\n"
"      --ntasks-per-node=n     number of tasks to invoke on each node\n"
"  -N, --nodes=N               number of nodes on which to run (N = min[-max])\n"
"  -o, --output=out            location of stdout redirection\n"
"  -O, --overcommit            overcommit resources\n"
"      --pack-group=value      pack job allocation(s) in which to launch\n"
"                              application\n"
"  -p, --partition=partition   partition requested\n"
"      --power=flags           power management options\n"
"      --priority=value        set the priority of the job to value\n"
"      --prolog=program        run \"program\" before launching job step\n"
"      --profile=value         enable acct_gather_profile for detailed data\n"
"                              value is all or none or any combination of\n"
"                              energy, lustre, network or task\n"
"      --propagate[=rlimits]   propagate all [or specific list of] rlimits\n"
#ifdef HAVE_PTY_H
"      --pty                   run task zero in pseudo terminal\n"
#endif
"      --quit-on-interrupt     quit on single Ctrl-C\n"
"  -q, --qos=qos               quality of service\n"
"  -Q, --quiet                 quiet mode (suppress informational messages)\n"
"      --reboot                reboot block before starting job\n"
"  -r, --relative=n            run job step relative to node n of allocation\n"
"      --restart-dir=dir       directory of checkpoint image files to restart\n"
"                              from\n"
"  -s, --oversubscribe         over-subscribe resources with other jobs\n"
"  -S, --core-spec=cores       count of reserved cores\n"
"      --signal=[B:]num[@time] send signal when time limit within time seconds\n"
"      --slurmd-debug=level    slurmd debug level\n"
"      --spread-job            spread job across as many nodes as possible\n"
"      --switches=max-switches{@max-time-to-wait}\n"
"                              Optimum switches and max time to wait for optimum\n"
"      --task-epilog=program   run \"program\" after launching task\n"
"      --task-prolog=program   run \"program\" before launching task\n"
"      --thread-spec=threads   count of reserved threads\n"
"  -T, --threads=threads       set srun launch fanout\n"
"  -t, --time=minutes          time limit\n"
"      --time-min=minutes      minimum time limit (if distinct)\n"
"  -u, --unbuffered            do not line-buffer stdout/err\n"
"      --use-min-nodes         if a range of node counts is given, prefer the\n"
"                              smaller count\n"
"  -v, --verbose               verbose mode (multiple -v's increase verbosity)\n"
"  -W, --wait=sec              seconds to wait after first task exits\n"
"                              before killing job\n"
"      --wckey=wckey           wckey to run job under\n"
"  -X, --disable-status        Disable Ctrl-C status feature\n"
"\n"
"Constraint options:\n"
"      --cluster-constraint=list specify a list of cluster-constraints\n"
"      --contiguous            demand a contiguous range of nodes\n"
"  -C, --constraint=list       specify a list of constraints\n"
"      --mem=MB                minimum amount of real memory\n"
"      --mincpus=n             minimum number of logical processors (threads)\n"
"                              per node\n"
"      --reservation=name      allocate resources from named reservation\n"
"      --tmp=MB                minimum amount of temporary disk\n"
"  -w, --nodelist=hosts...     request a specific list of hosts\n"
"  -x, --exclude=hosts...      exclude a specific list of hosts\n"
"  -Z, --no-allocate           don't allocate nodes (must supply -w)\n"
"\n"
"Consumable resources related options:\n"
"      --exclusive[=user]      allocate nodes in exclusive mode when\n"
"                              cpu consumable resource is enabled\n"
"                              or don't share CPUs for job steps\n"
"      --exclusive[=mcs]       allocate nodes in exclusive mode when\n"
"                              cpu consumable resource is enabled\n"
"                              and mcs plugin is enabled\n"
"                              or don't share CPUs for job steps\n"
"      --mem-per-cpu=MB        maximum amount of real memory per allocated\n"
"                              cpu required by the job.\n"
"                              --mem >= --mem-per-cpu if --mem is specified.\n"
"      --resv-ports            reserve communication ports\n"
"\n"
"Affinity/Multi-core options: (when the task/affinity plugin is enabled)\n"
"  -B, --extra-node-info=S[:C[:T]]           Expands to:\n"
"      --sockets-per-node=S    number of sockets per node to allocate\n"
"      --cores-per-socket=C    number of cores per socket to allocate\n"
"      --threads-per-core=T    number of threads per core to allocate\n"
"                              each field can be 'min' or wildcard '*'\n"
"                              total cpus requested = (N x S x C x T)\n"
"\n"
"      --ntasks-per-core=n     number of tasks to invoke on each core\n"
"      --ntasks-per-socket=n   number of tasks to invoke on each socket\n");
	conf = slurm_conf_lock();
	if (xstrstr(conf->task_plugin, "affinity") ||
	    xstrstr(conf->task_plugin, "cgroup")) {
		printf(
"      --cpu-bind=             Bind tasks to CPUs\n"
"                              (see \"--cpu-bind=help\" for options)\n"
"      --hint=                 Bind tasks according to application hints\n"
"                              (see \"--hint=help\" for options)\n");
	}
	if (xstrstr(conf->task_plugin, "affinity")) {
		printf(
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
"      --usage                 display brief usage message\n"
"\n"
"Other options:\n"
"  -V, --version               output version information and exit\n"
"\n"
		);

}
