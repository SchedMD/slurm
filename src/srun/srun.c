/*****************************************************************************\
 *  srun.c - user interface to allocate resources, submit jobs, and execute
 *	parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
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

#include <ctype.h>
#include <fcntl.h>
#include <grp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "src/common/fd.h"

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/plugstack.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/switch.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/bcast/file_bcast.h"

#include "launch.h"
#include "allocate.h"
#include "srun_job.h"
#include "opt.h"
#include "debugger.h"
#include "src/srun/srun_pty.h"
#include "multi_prog.h"
#include "src/api/pmi_server.h"
#include "src/api/step_ctx.h"
#include "src/api/step_launch.h"

#ifndef OPEN_MPI_PORT_ERROR
/* This exit code indicates the launched Open MPI tasks could
 *	not open the reserved port. It was already open by some
 *	other process. */
#define OPEN_MPI_PORT_ERROR 108
#endif

static struct termios termdefaults;
static uint32_t global_rc = 0;
static srun_job_t *job = NULL;

extern char **environ;	/* job environment */
bool srun_max_timer = false;
bool srun_shutdown  = false;
int sig_array[] = {
	SIGINT,  SIGQUIT, SIGCONT, SIGTERM, SIGHUP,
	SIGALRM, SIGUSR1, SIGUSR2, SIGPIPE, 0 };

typedef struct _launch_app_data
{
	bool		got_alloc;
	srun_job_t *	job;
	slurm_opt_t	*opt_local;
	int *		step_cnt;
	pthread_cond_t *step_cond;
	pthread_mutex_t *step_mutex;
} _launch_app_data_t;

/*
 * forward declaration of static funcs
 */
static int   _file_bcast(slurm_opt_t *opt_local, srun_job_t *job);
static void  _launch_app(srun_job_t *job, List srun_job_list, bool got_alloc);
static void *_launch_one_app(void *data);
static void  _pty_restore(void);
static void  _set_exit_code(void);
static void  _set_node_alias(void);
static void  _setup_env_working_cluster(void);
static void  _setup_job_env(srun_job_t *job, List srun_job_list,
			    bool got_alloc);
static void  _setup_one_job_env(slurm_opt_t *opt_local, srun_job_t *job,
				bool got_alloc);
static int   _slurm_debug_env_val (void);
static char *_uint16_array_to_str(int count, const uint16_t *array);

/*
 * from libvirt-0.6.2 GPL2
 *
 * console.c: A dumb serial console client
 *
 * Copyright (C) 2007, 2008 Red Hat, Inc.
 *
 */
#ifndef HAVE_CFMAKERAW
void cfmakeraw(struct termios *attr)
{
	attr->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
				| INLCR | IGNCR | ICRNL | IXON);
	attr->c_oflag &= ~OPOST;
	attr->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	attr->c_cflag &= ~(CSIZE | PARENB);
	attr->c_cflag |= CS8;
}
#endif

static bool _enable_pack_steps(void)
{
	bool enabled = false;
	char *sched_params = slurm_get_sched_params();

	if (sched_params && strstr(sched_params, "disable_hetero_steps"))
		enabled = false;
	else if (sched_params && strstr(sched_params, "enable_hetero_steps"))
		enabled = true;
	else if (mpi_type && strstr(mpi_type, "none"))
		enabled = true;
	xfree(sched_params);
	return enabled;
}

int srun(int ac, char **av)
{
	int debug_level;
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	bool got_alloc = false;
	List srun_job_list = NULL;

	slurm_conf_init(NULL);
	debug_level = _slurm_debug_env_val();
	logopt.stderr_level += debug_level;
	log_init(xbasename(av[0]), logopt, 0, NULL);
	_set_exit_code();

	if (slurm_select_init(1) != SLURM_SUCCESS )
		fatal( "failed to initialize node selection plugin" );

	if (switch_init(0) != SLURM_SUCCESS )
		fatal("failed to initialize switch plugins");

	_setup_env_working_cluster();

	init_srun(ac, av, &logopt, debug_level, 1);
	if (opt_list) {
		if (!_enable_pack_steps())
			fatal("Job steps that span multiple components of a heterogeneous job are not currently supported");
		create_srun_job((void **) &srun_job_list, &got_alloc, 0, 1);
	} else
		create_srun_job((void **) &job, &got_alloc, 0, 1);

	_setup_job_env(job, srun_job_list, got_alloc);
	_set_node_alias();
	_launch_app(job, srun_job_list, got_alloc);

	if ((global_rc & 0xff) == SIG_OOM)
		global_rc = 1;	/* Exit code 1 */

	return (int)global_rc;
}

static void *_launch_one_app(void *data)
{
	static pthread_mutex_t launch_mutex = PTHREAD_MUTEX_INITIALIZER;
	static pthread_cond_t  launch_cond  = PTHREAD_COND_INITIALIZER;
	static bool            launch_begin = false;
	static bool            launch_fini  = false;
	_launch_app_data_t *opts = (_launch_app_data_t *) data;
	slurm_opt_t *opt_local = opts->opt_local;
	srun_job_t *job  = opts->job;
	bool got_alloc   = opts->got_alloc;
	slurm_step_io_fds_t cio_fds = SLURM_STEP_IO_FDS_INITIALIZER;
	slurm_step_launch_callbacks_t step_callbacks;

	memset(&step_callbacks, 0, sizeof(step_callbacks));
	step_callbacks.step_signal = launch_g_fwd_signal;

	/*
	 * Run pre-launch once for entire pack job
	 */
	slurm_mutex_lock(&launch_mutex);
	if (!launch_begin) {
		launch_begin = true;
		slurm_mutex_unlock(&launch_mutex);

		pre_launch_srun_job(job, 0, 1, opt_local);

		slurm_mutex_lock(&launch_mutex);
		launch_fini = true;
		slurm_cond_broadcast(&launch_cond);
	} else {
		while (!launch_fini)
			slurm_cond_wait(&launch_cond, &launch_mutex);
	}
	slurm_mutex_unlock(&launch_mutex);

relaunch:
	launch_common_set_stdio_fds(job, &cio_fds, opt_local);

	if (!launch_g_step_launch(job, &cio_fds, &global_rc, &step_callbacks,
				  opt_local)) {
		if (launch_g_step_wait(job, got_alloc, opt_local) == -1)
			goto relaunch;
	}

	if (opts->step_mutex) {
		slurm_mutex_lock(opts->step_mutex);
		(*opts->step_cnt)--;
		slurm_cond_broadcast(opts->step_cond);
		slurm_mutex_unlock(opts->step_mutex);
	}
	xfree(data);
	return NULL;
}

/*
 * The pack_node_list may not be ordered across multiple components, which can
 * cause problems for some MPI implementations. Put the pack_node_list records
 * in alphabetic order and reorder pack_task_cnts pack_tids to match
 */
static void _reorder_pack_recs(char **in_node_list, uint16_t **in_task_cnts,
			       uint32_t ***in_tids, int total_nnodes)
{
	hostlist_t in_hl, out_hl;
	uint16_t *out_task_cnts = NULL;
	uint32_t **out_tids = NULL;
	char *hostname;
	int i, j;

	in_hl = hostlist_create(*in_node_list);
	if (!in_hl) {
		error("%s: Invalid hostlist(%s)", __func__, *in_node_list);
		return;
	}
	out_hl = hostlist_copy(in_hl);
	hostlist_sort(out_hl);
	hostlist_uniq(out_hl);
	i = hostlist_count(out_hl);
	if (i != total_nnodes) {
		error("%s: Invalid hostlist(%s) count(%d)", __func__,
		      *in_node_list, total_nnodes);
		goto fini;
	}

	out_task_cnts = xmalloc(sizeof(uint16_t) * total_nnodes);
	out_tids = xmalloc(sizeof(uint32_t *) * total_nnodes);
	for (i = 0; i < total_nnodes; i++) {
		hostname = hostlist_nth(out_hl, i);
		if (!hostname) {
			error("%s: Invalid hostlist(%s) count(%d)", __func__,
			      *in_node_list, total_nnodes);
			break;
		}
		j = hostlist_find(in_hl, hostname);
		if (j == -1) {
			error("%s: Invalid hostlist(%s) parsing", __func__,
			      *in_node_list);
			free(hostname);
			break;
		}
		out_task_cnts[i] = in_task_cnts[0][j];
		out_tids[i] = in_tids[0][j];
		free(hostname);
	}

	if (i >= total_nnodes) {	/* Success */
		xfree(*in_node_list);
		*in_node_list = hostlist_ranged_string_xmalloc(out_hl);
		xfree(*in_task_cnts);
		*in_task_cnts = out_task_cnts;
		out_task_cnts = NULL;
		xfree(*in_tids);
		*in_tids = out_tids;
		out_tids = NULL;
	}

#if 0
	info("NODE_LIST[%d]:%s", total_nnodes, *in_node_list);
	for (i = 0; i < total_nnodes; i++) {
		info("TASK_CNT[%d]:%u", i, in_task_cnts[0][i]);
		for (j = 0; j < in_task_cnts[0][i]; j++) {
			info("TIDS[%d][%d]: %u", i, j, in_tids[0][i][j]);
		}
	}
#endif

fini:	hostlist_destroy(in_hl);
	hostlist_destroy(out_hl);
	xfree(out_task_cnts);
	xfree(out_tids);
}

static void _launch_app(srun_job_t *job, List srun_job_list, bool got_alloc)
{
	ListIterator opt_iter, job_iter;
	slurm_opt_t *opt_local = NULL;
	_launch_app_data_t *opts;
	int total_ntasks = 0, total_nnodes = 0, step_cnt = 0, node_offset = 0;
	pthread_mutex_t step_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t step_cond   = PTHREAD_COND_INITIALIZER;
	srun_job_t *first_job = NULL;
	char *launch_type, *pack_node_list = NULL;
	bool need_mpir = false;
	uint16_t *tmp_task_cnt = NULL, *pack_task_cnts = NULL;
	uint32_t **tmp_tids = NULL, **pack_tids = NULL;

	launch_type = slurm_get_launch_type();
	if (launch_type && strstr(launch_type, "slurm"))
		need_mpir = true;
	xfree(launch_type);

	if (srun_job_list) {
		int pack_step_cnt = list_count(srun_job_list);
		first_job = (srun_job_t *) list_peek(srun_job_list);
		if (!opt_list) {
			if (first_job)
				fini_srun(first_job, got_alloc, &global_rc, 0);
			fatal("%s: have srun_job_list, but no opt_list",
			      __func__);
		}

		job_iter = list_iterator_create(srun_job_list);
		while ((job = (srun_job_t *) list_next(job_iter))) {
			char *node_list = NULL;
			int i, node_inx;
			total_ntasks += job->ntasks;
			total_nnodes += job->nhosts;

			xrealloc(pack_task_cnts, sizeof(uint16_t)*total_nnodes);
			(void) slurm_step_ctx_get(job->step_ctx,
						  SLURM_STEP_CTX_TASKS,
						  &tmp_task_cnt);
			if (!tmp_task_cnt) {
				fatal("%s: job %u has NULL task array",
				      __func__, job->jobid);
				break;	/* To eliminate CLANG error */
			}
			memcpy(pack_task_cnts + node_offset, tmp_task_cnt,
			       sizeof(uint16_t) * job->nhosts);

			xrealloc(pack_tids, sizeof(uint32_t *) * total_nnodes);
			(void) slurm_step_ctx_get(job->step_ctx,
						  SLURM_STEP_CTX_TIDS,
						  &tmp_tids);
			if (!tmp_tids) {
				fatal("%s: job %u has NULL task ID array",
				      __func__, job->jobid);
				break;	/* To eliminate CLANG error */
			}
			for (node_inx = 0; node_inx < job->nhosts; node_inx++) {
				uint32_t *node_tids;
				node_tids = xmalloc(sizeof(uint32_t) *
						    tmp_task_cnt[node_inx]);
				for (i = 0; i < tmp_task_cnt[node_inx]; i++) {
					node_tids[i] = tmp_tids[node_inx][i] +
						       job->pack_task_offset;
				}
				pack_tids[node_offset + node_inx] =
					node_tids;
			}

			(void) slurm_step_ctx_get(job->step_ctx,
						  SLURM_STEP_CTX_NODE_LIST,
						  &node_list);
			if (!node_list) {
				fatal("%s: job %u has NULL hostname",
				      __func__, job->jobid);
			}
			if (pack_node_list)
				xstrfmtcat(pack_node_list, ",%s", node_list);
			else
				pack_node_list = xstrdup(node_list);
			xfree(node_list);
			node_offset += job->nhosts;
		}
		list_iterator_reset(job_iter);
		_reorder_pack_recs(&pack_node_list, &pack_task_cnts,
				   &pack_tids, total_nnodes);

		if (need_mpir)
			mpir_init(total_ntasks);

		opt_iter = list_iterator_create(opt_list);
		while ((opt_local = list_next(opt_iter))) {
			srun_opt_t *srun_opt = opt_local->srun_opt;
			xassert(srun_opt);
			job = (srun_job_t *) list_next(job_iter);
			if (!job) {
				slurm_mutex_lock(&step_mutex);
				while (step_cnt > 0)
					slurm_cond_wait(&step_cond,&step_mutex);
				slurm_mutex_unlock(&step_mutex);
				if (first_job) {
					fini_srun(first_job, got_alloc,
						  &global_rc, 0);
				}
				fatal("%s: job allocation count does not match request count (%d != %d)",
				      __func__, list_count(srun_job_list),
				      list_count(opt_list));
				break;	/* To eliminate CLANG error */
			}

			slurm_mutex_lock(&step_mutex);
			step_cnt++;
			slurm_mutex_unlock(&step_mutex);
			job->pack_node_list = xstrdup(pack_node_list);
			if ((pack_step_cnt > 1) && pack_task_cnts) {
				xassert(node_offset == job->pack_nnodes);
				job->pack_task_cnts = xmalloc(sizeof(uint16_t) *
							      job->pack_nnodes);
				memcpy(job->pack_task_cnts, pack_task_cnts,
				       sizeof(uint16_t) * job->pack_nnodes);
				job->pack_tids = xmalloc(sizeof(uint32_t *) *
							 job->pack_nnodes);
				memcpy(job->pack_tids, pack_tids,
				       sizeof(uint32_t *) * job->pack_nnodes);
			}
			opts = xmalloc(sizeof(_launch_app_data_t));
			opts->got_alloc   = got_alloc;
			opts->job         = job;
			opts->opt_local   = opt_local;
			opts->step_cond   = &step_cond;
			opts->step_cnt    = &step_cnt;
			opts->step_mutex  = &step_mutex;
			srun_opt->pack_step_cnt = pack_step_cnt;

			slurm_thread_create_detached(NULL, _launch_one_app,
						     opts);
		}
		xfree(pack_node_list);
		xfree(pack_task_cnts);
		list_iterator_destroy(job_iter);
		list_iterator_destroy(opt_iter);
		slurm_mutex_lock(&step_mutex);
		while (step_cnt > 0)
			slurm_cond_wait(&step_cond, &step_mutex);
		slurm_mutex_unlock(&step_mutex);

		if (first_job)
			fini_srun(first_job, got_alloc, &global_rc, 0);
	} else {
		if (need_mpir)
			mpir_init(job->ntasks);
		opts = xmalloc(sizeof(_launch_app_data_t));
		opts->got_alloc   = got_alloc;
		opts->job         = job;
		opts->opt_local   = &opt;
		sropt.pack_step_cnt = 1;
		_launch_one_app(opts);
		fini_srun(job, got_alloc, &global_rc, 0);
	}
}

static void _setup_one_job_env(slurm_opt_t *opt_local, srun_job_t *job,
			       bool got_alloc)
{
	env_t *env = xmalloc(sizeof(env_t));
	uint16_t *tasks = NULL;
	srun_opt_t *srun_opt = opt_local->srun_opt;
	xassert(srun_opt);

	xassert(job);

	env->localid = -1;
	env->nodeid  = -1;
	env->procid  = -1;
	env->stepid  = -1;

	if (srun_opt->bcast_flag)
		_file_bcast(opt_local, job);
	if (opt_local->cpus_set)
		env->cpus_per_task = opt_local->cpus_per_task;
	if (opt_local->ntasks_per_node != NO_VAL)
		env->ntasks_per_node = opt_local->ntasks_per_node;
	if (opt_local->ntasks_per_socket != NO_VAL)
		env->ntasks_per_socket = opt_local->ntasks_per_socket;
	if (opt_local->ntasks_per_core != NO_VAL)
		env->ntasks_per_core = opt_local->ntasks_per_core;
	env->distribution = opt_local->distribution;
	if (opt_local->plane_size != NO_VAL)
		env->plane_size = opt_local->plane_size;
	env->cpu_bind_type = srun_opt->cpu_bind_type;
	env->cpu_bind = srun_opt->cpu_bind;

	env->cpu_freq_min = opt_local->cpu_freq_min;
	env->cpu_freq_max = opt_local->cpu_freq_max;
	env->cpu_freq_gov = opt_local->cpu_freq_gov;
	env->mem_bind_type = opt_local->mem_bind_type;
	env->mem_bind = opt_local->mem_bind;
	env->overcommit = opt_local->overcommit;
	env->slurmd_debug = srun_opt->slurmd_debug;
	env->labelio = srun_opt->labelio;
	env->comm_port = slurmctld_comm_addr.port;
	if (opt_local->job_name)
		env->job_name = opt_local->job_name;

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_TASKS, &tasks);

	env->select_jobinfo = job->select_jobinfo;
	if (job->pack_node_list)
		env->nodelist = job->pack_node_list;
	else
		env->nodelist = job->nodelist;
	env->partition = job->partition;
	if (job->pack_nnodes != NO_VAL)
		env->nhosts = job->pack_nnodes;
	else if (got_alloc)	/* Don't overwrite unless we got allocation */
		env->nhosts = job->nhosts;
	if (job->pack_ntasks != NO_VAL)
		env->ntasks = job->pack_ntasks;
	else
		env->ntasks = job->ntasks;
	env->task_count = _uint16_array_to_str(job->nhosts, tasks);
	if (job->pack_jobid != NO_VAL)
		env->jobid = job->pack_jobid;
	else
		env->jobid = job->jobid;
	env->ntasks = job->ntasks;
	env->stepid = job->stepid;
	env->account = job->account;
	env->qos = job->qos;
	env->resv_name = job->resv_name;

	if (srun_opt->pty && (set_winsize(job) < 0)) {
		error("Not using a pseudo-terminal, disregarding --pty option");
		srun_opt->pty = false;
	}
	if (srun_opt->pty) {
		struct termios term;
		int fd = STDIN_FILENO;

		/* Save terminal settings for restore */
		tcgetattr(fd, &termdefaults);
		tcgetattr(fd, &term);
		/* Set raw mode on local tty */
		cfmakeraw(&term);
		/* Re-enable output processing such that debug() and
		 * and error() work properly. */
		term.c_oflag |= OPOST;
		tcsetattr(fd, TCSANOW, &term);
		atexit(&_pty_restore);

		block_sigwinch();
		pty_thread_create(job);
		env->pty_port = job->pty_port;
		env->ws_col   = job->ws_col;
		env->ws_row   = job->ws_row;
	}

	setup_env(env, srun_opt->preserve_env);
	job->env = environ;
	xfree(env->task_count);
	xfree(env);
}

static void _setup_job_env(srun_job_t *job, List srun_job_list, bool got_alloc)
{
	ListIterator opt_iter, job_iter;
	slurm_opt_t *opt_local;

	if (srun_job_list) {
		srun_job_t *first_job = list_peek(srun_job_list);
		if (!opt_list) {
			if (first_job)
				fini_srun(first_job, got_alloc, &global_rc, 0);
			fatal("%s: have srun_job_list, but no opt_list",
			      __func__);
		}
		job_iter  = list_iterator_create(srun_job_list);
		opt_iter  = list_iterator_create(opt_list);
		while ((opt_local = list_next(opt_iter))) {
			job = (srun_job_t *) list_next(job_iter);
			if (!job) {
				if (first_job) {
					fini_srun(first_job, got_alloc,
						  &global_rc, 0);
				}
				fatal("%s: job allocation count does not match request count (%d != %d)",
				      __func__, list_count(srun_job_list),
				      list_count(opt_list));
			}
			_setup_one_job_env(opt_local, job, got_alloc);
		}
		list_iterator_destroy(job_iter);
		list_iterator_destroy(opt_iter);
	} else if (job) {
		_setup_one_job_env(&opt, job, got_alloc);
	} else {
		fatal("%s: No job information", __func__);
	}
}

static int _file_bcast(slurm_opt_t *opt_local, srun_job_t *job)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	struct bcast_parameters *params;
	int rc;
	xassert(srun_opt);

	if ((srun_opt->argc == 0) || (srun_opt->argv[0] == NULL)) {
		error("No command name to broadcast");
		return SLURM_ERROR;
	}
	params = xmalloc(sizeof(struct bcast_parameters));
	params->block_size = 8 * 1024 * 1024;
	params->compress = srun_opt->compress;
	if (srun_opt->bcast_file) {
		params->dst_fname = xstrdup(srun_opt->bcast_file);
	} else {
		xstrfmtcat(params->dst_fname, "%s/slurm_bcast_%u.%u",
			   opt_local->cwd, job->jobid, job->stepid);
	}
	params->fanout = 0;
	params->job_id = job->jobid;
	params->force = true;
	if (srun_opt->pack_grp_bits)
		params->pack_job_offset = bit_ffs(srun_opt->pack_grp_bits);
	else
		params->pack_job_offset = NO_VAL;
	params->preserve = true;
	params->src_fname = srun_opt->argv[0];
	params->step_id = job->stepid;
	params->timeout = 0;
	params->verbose = 0;

	rc = bcast_file(params);
	if (rc == SLURM_SUCCESS) {
		xfree(srun_opt->argv[0]);
		srun_opt->argv[0] = params->dst_fname;
	} else {
		xfree(params->dst_fname);
	}
	xfree(params);

	return rc;
}

static int _slurm_debug_env_val (void)
{
	long int level = 0;
	const char *val;

	if ((val = getenv ("SLURM_DEBUG"))) {
		char *p;
		if ((level = strtol (val, &p, 10)) < -LOG_LEVEL_INFO)
			level = -LOG_LEVEL_INFO;
		if (p && *p != '\0')
			level = 0;
	}
	return ((int) level);
}

/*
 * Return a string representation of an array of uint32_t elements.
 * Each value in the array is printed in decimal notation and elements
 * are separated by a comma.  If sequential elements in the array
 * contain the same value, the value is written out just once followed
 * by "(xN)", where "N" is the number of times the value is repeated.
 *
 * Example:
 *   The array "1, 2, 1, 1, 1, 3, 2" becomes the string "1,2,1(x3),3,2"
 *
 * Returns an xmalloc'ed string.  Free with xfree().
 */
static char *_uint16_array_to_str(int array_len, const uint16_t *array)
{
	int i;
	int previous = 0;
	char *sep = ",";  /* seperator */
	char *str = xstrdup("");

	if (array == NULL)
		return str;

	for (i = 0; i < array_len; i++) {
		if ((i+1 < array_len)
		    && (array[i] == array[i+1])) {
				previous++;
				continue;
		}

		if (i == array_len-1) /* last time through loop */
			sep = "";
		if (previous > 0) {
			xstrfmtcat(str, "%u(x%u)%s",
				   array[i], previous+1, sep);
		} else {
			xstrfmtcat(str, "%u%s", array[i], sep);
		}
		previous = 0;
	}

	return str;
}

static void _set_exit_code(void)
{
	int i;
	char *val;

	if ((val = getenv("SLURM_EXIT_ERROR"))) {
		i = atoi(val);
		if (i == 0)
			error("SLURM_EXIT_ERROR has zero value");
		else
			error_exit = i;
	}

	if ((val = getenv("SLURM_EXIT_IMMEDIATE"))) {
		i = atoi(val);
		if (i == 0)
			error("SLURM_EXIT_IMMEDIATE has zero value");
		else
			immediate_exit = i;
	}
}

static void _set_node_alias(void)
{
	char *aliases, *save_ptr = NULL, *tmp;
	char *addr, *hostname, *slurm_name;

	tmp = getenv("SLURM_NODE_ALIASES");
	if (!tmp)
		return;
	aliases = xstrdup(tmp);
	slurm_name = strtok_r(aliases, ":", &save_ptr);
	while (slurm_name) {
		addr = strtok_r(NULL, ":", &save_ptr);
		if (!addr)
			break;
		slurm_reset_alias(slurm_name, addr, addr);
		hostname = strtok_r(NULL, ",", &save_ptr);
		if (!hostname)
			break;
		slurm_name = strtok_r(NULL, ":", &save_ptr);
	}
	xfree(aliases);
}

static void _pty_restore(void)
{
	/* STDIN is probably closed by now */
	if (tcsetattr(STDOUT_FILENO, TCSANOW, &termdefaults) < 0)
		fprintf(stderr, "tcsetattr: %s\n", strerror(errno));
}

static void _setup_env_working_cluster(void)
{
	char *working_env  = NULL;

	if ((working_env = xstrdup(getenv("SLURM_WORKING_CLUSTER")))) {
		char *addr_ptr, *port_ptr, *rpc_ptr;

		if (!(addr_ptr = strchr(working_env,  ':')) ||
		    !(port_ptr = strchr(addr_ptr + 1, ':')) ||
		    !(rpc_ptr  = strchr(port_ptr + 1, ':'))) {
			error("malformed cluster addr and port in SLURM_WORKING_CLUSTER env var: '%s'",
			      working_env);
			exit(1);
		}

		*addr_ptr++ = '\0';
		*port_ptr++ = '\0';
		*rpc_ptr++  = '\0';

		if (xstrcmp(slurmctld_conf.cluster_name, working_env)) {
			working_cluster_rec =
				xmalloc(sizeof(slurmdb_cluster_rec_t));
			slurmdb_init_cluster_rec(working_cluster_rec, false);

			working_cluster_rec->control_host = xstrdup(addr_ptr);;
			working_cluster_rec->control_port = strtol(port_ptr,
								   NULL, 10);
			working_cluster_rec->rpc_version  = strtol(rpc_ptr,
								   NULL, 10);
			slurm_set_addr(&working_cluster_rec->control_addr,
				       working_cluster_rec->control_port,
				       working_cluster_rec->control_host);
		}
		xfree(working_env);
	}
}
