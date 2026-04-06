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
#include <unistd.h>

#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/probes.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/spank.h"
#include "src/common/threadpool.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/cli_filter.h"
#include "src/interfaces/compress.h"
#include "src/interfaces/jobacct_gather.h"
#include "src/interfaces/switch.h"

#include "src/api/pmi_server.h"
#include "src/api/step_launch.h"

#include "src/srun/allocate.h"
#include "src/srun/debugger.h"
#include "src/srun/launch.h"
#include "src/srun/multi_prog.h"
#include "src/srun/opt.h"
#include "src/srun/signals.h"
#include "src/srun/srun_job.h"
#include "src/srun/srun_pty.h"
#include "src/srun/step_ctx.h"

#define THREAD_COUNT 3

static uint32_t global_rc = 0;
static uint32_t mpi_plugin_rc = 0;
static srun_job_t *job = NULL;

extern char **environ;	/* job environment */
bool srun_max_timer = false;
pthread_mutex_t srun_max_timer_lock = PTHREAD_MUTEX_INITIALIZER;
bitstr_t *g_het_grp_bits = NULL;

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
static void _launch_app(srun_job_t *job, list_t *srun_job_list, bool got_alloc);
static void *_launch_one_app(void *data);
static void  _set_exit_code(void);
static void  _setup_env_working_cluster(void);
static void _setup_job_env(srun_job_t *job, list_t *srun_job_list,
			   bool got_alloc);

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

static bool _enable_het_job_steps(void)
{
	/* Continue supporting old terminology */
	if (xstrcasestr(slurm_conf.sched_params, "disable_hetero_steps") ||
	    xstrcasestr(slurm_conf.sched_params, "disable_hetjob_steps"))
		return false;

	return true;
}

int srun(int ac, char **av)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	bool got_alloc = false;
	list_t *srun_job_list = NULL;

	slurm_init(NULL);
	if (auth_is_plugin_type_inited(AUTH_PLUGIN_JWT)) {
		/*
		 * Bail out if we're trying to use auth/jwt.
		 * Slurmd does not support this auth plugin, and srun needs
		 * to talk to slurmd.
		 */
		fatal("auth/jwt is not supported for use with srun.");
	}
	log_init(xbasename(av[0]), logopt, 0, NULL);
	_set_exit_code();

	/* Must be called before starting conmgr. */
	xsignal(SIGTTIN, SIG_IGN);

	probe_init();
	conmgr_init(0, THREAD_COUNT, 0);

	forward_init();

	conmgr_run(false);

	if (cli_filter_init() != SLURM_SUCCESS)
		fatal("failed to initialize cli_filter plugin");
	if (switch_g_init(false) != SLURM_SUCCESS )
		fatal("failed to initialize switch plugins");
	if (compress_g_init() != SLURM_SUCCESS)
		fatal("failed to initialize compress plugin");

	_setup_env_working_cluster();

	init_srun(ac, av, &logopt);
	if (opt_list) {
		if (!_enable_het_job_steps())
			fatal("Job steps that span multiple components of a heterogeneous job are not currently supported");
		create_srun_job((void **) &srun_job_list, &got_alloc);
	} else
		create_srun_job((void **) &job, &got_alloc);

	/*
	 * Detect is process is in non-matching user namespace or UIDs
	 * with controller are mismatching.
	 */
	if (job && (job->uid != getuid()))
		debug3("%s: %ps UID %u and srun process UID %u mismatch",
		       __func__, &job->step_id, job->uid, getuid());
	if (job && (job->gid != getgid()))
		debug3("%s: %ps GID %u and srun process GID %u mismatch",
		       __func__, &job->step_id, job->gid, getgid());

	_setup_job_env(job, srun_job_list, got_alloc);

	/*
	 * Determine if the first/only job was called with --pty and update
	 * logging if required
	 */
	if (srun_job_list) {
		srun_job_t *first_job = list_peek(srun_job_list);
		if (first_job->pty_port) {
			logopt.raw = true;
			log_alter(logopt, 0, NULL);
		}
	} else if (job && job->pty_port) {
		logopt.raw = true;
		log_alter(logopt, 0, NULL);
	}

	_launch_app(job, srun_job_list, got_alloc);

	if ((global_rc & 0xff) == SIG_OOM)
		global_rc = 1;	/* Exit code 1 */
	else if (mpi_plugin_rc) {
		/*
		 * MPI plugin might have more precise information in some cases.
		 * For example, if PMI[?] abort was by task X with return code
		 * RC, the expectation is that srun will return RC as srun's
		 * return code. However, to ensure proper cleanup, the plugin
		 * kills the job with SIGKILL which obscures the original reason
		 * for job exit.
		 */
		global_rc = mpi_plugin_rc;
	}

	conmgr_request_shutdown();
	forward_fini();
	conmgr_fini();

	if (job)
		step_ctx_destroy(job->step_ctx);

#ifdef MEMORY_LEAK_DEBUG
	probe_fini();
	compress_g_fini();
	cli_filter_fini();
	mpi_fini();
	switch_g_fini();
	slurm_reset_all_options(&opt, false);
	slurm_fini();
	log_fini();
#endif /* MEMORY_LEAK_DEBUG */

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
	step_callbacks.step_signal = launch_fwd_signal;

	/*
	 * Run pre-launch once for entire hetjob
	 */
	slurm_mutex_lock(&launch_mutex);
	if (!launch_begin) {
		launch_begin = true;
		slurm_mutex_unlock(&launch_mutex);

		pre_launch_srun_job(job, opt_local);

		slurm_mutex_lock(&launch_mutex);
		launch_fini = true;
		slurm_cond_broadcast(&launch_cond);
	} else {
		while (!launch_fini)
			slurm_cond_wait(&launch_cond, &launch_mutex);
	}
	slurm_mutex_unlock(&launch_mutex);

	/*
	 * Update argv[0] after spank_local_user() so that S_JOB_ARGV holds the
	 * original command line args in local context only.
	 */
	if (opt_local->srun_opt->bcast_flag) {
		xfree(opt_local->argv[0]);
		opt_local->argv[0] = xstrdup(opt_local->srun_opt->bcast_file);
	}
relaunch:
	slurm_mutex_lock(&srun_sig_forward_lock);
	srun_sig_forward = true;
	slurm_mutex_unlock(&srun_sig_forward_lock);

	launch_set_stdio_fds(job, &cio_fds, opt_local);

	if (!launch_step_launch(job, &cio_fds, &global_rc, &step_callbacks,
				opt_local)) {
		if (launch_step_wait(job, got_alloc, opt_local) == -1)
			goto relaunch;
		if (job->step_ctx->launch_state->ret_code > mpi_plugin_rc)
			mpi_plugin_rc = job->step_ctx->launch_state->ret_code;
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
 * The het_job_node_list may not be ordered across multiple components, which
 * can cause problems for some MPI implementations. Put the het_job_node_list
 * records in alphabetic order and reorder het_job_task_cnts het_job_tids to
 * match
 */
static void _reorder_het_job_recs(char **in_node_list, uint16_t **in_task_cnts,
			       uint32_t ***in_tids, int total_nnodes)
{
	hostlist_t *in_hl, *out_hl;
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

static void _launch_app(srun_job_t *job, list_t *srun_job_list, bool got_alloc)
{
	list_itr_t *opt_iter, *job_iter;
	slurm_opt_t *opt_local = NULL;
	_launch_app_data_t *opts;
	int total_ntasks = 0, total_nnodes = 0, step_cnt = 0, node_offset = 0;
	pthread_mutex_t step_mutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t step_cond   = PTHREAD_COND_INITIALIZER;
	srun_job_t *first_job = NULL;
	char *het_job_node_list = NULL;
	uint16_t *tmp_task_cnt = NULL, *het_job_task_cnts = NULL;
	uint32_t **tmp_tids = NULL, **het_job_tids = NULL;
	uint32_t *het_job_tid_offsets = NULL;
	uint32_t *het_job_step_task_cnts = NULL; /* task count per component */
	int comp = 0;

	if (srun_job_list) {
		int het_job_step_cnt = list_count(srun_job_list);
		first_job = (srun_job_t *) list_peek(srun_job_list);
		if (!opt_list) {
			if (first_job)
				fini_srun(first_job, got_alloc, &global_rc);
			fatal("%s: have srun_job_list, but no opt_list",
			      __func__);
		}

		/* Record the number of tasks for each het component */
		het_job_step_task_cnts =
			xcalloc(het_job_step_cnt, sizeof(uint32_t));
		comp = 0;

		job_iter = list_iterator_create(srun_job_list);
		while ((job = list_next(job_iter))) {
			char *node_list = NULL;
			int i, node_inx;
			total_ntasks += job->ntasks;
			total_nnodes += job->nhosts;

			het_job_step_task_cnts[comp++] = job->ntasks;

			xrealloc(het_job_task_cnts,
				 sizeof(uint16_t)*total_nnodes);
			tmp_task_cnt =
				job->step_ctx->step_resp->step_layout->tasks;
			xrealloc(het_job_tid_offsets,
				 sizeof(uint32_t) * total_ntasks);

			for (i = total_ntasks - job->ntasks;
			     i < total_ntasks;
			     i++)
				het_job_tid_offsets[i] = job->het_job_offset;

			if (!tmp_task_cnt) {
				fatal("%s: job %u has NULL task array",
				      __func__, job->step_id.job_id);
				break;	/* To eliminate CLANG error */
			}
			memcpy(het_job_task_cnts + node_offset, tmp_task_cnt,
			       sizeof(uint16_t) * job->nhosts);

			xrealloc(het_job_tids,
				 sizeof(uint32_t *) * total_nnodes);
			tmp_tids = job->step_ctx->step_resp->step_layout->tids;
			if (!tmp_tids) {
				fatal("%s: job %u has NULL task ID array",
				      __func__, job->step_id.job_id);
				break;	/* To eliminate CLANG error */
			}
			for (node_inx = 0; node_inx < job->nhosts; node_inx++) {
				uint32_t *node_tids;
				node_tids = xmalloc(sizeof(uint32_t) *
						    tmp_task_cnt[node_inx]);
				for (i = 0; i < tmp_task_cnt[node_inx]; i++) {
					node_tids[i] = tmp_tids[node_inx][i] +
						       job->het_job_task_offset;
				}
				het_job_tids[node_offset + node_inx] =
					node_tids;
			}

			node_list = job->step_ctx->step_resp->
				step_layout->node_list;
			if (!node_list) {
				fatal("%s: job %u has NULL hostname",
				      __func__, job->step_id.job_id);
			}
			if (het_job_node_list)
				xstrfmtcat(het_job_node_list, ",%s", node_list);
			else
				het_job_node_list = xstrdup(node_list);
			node_offset += job->nhosts;
		}
		list_iterator_reset(job_iter);
		_reorder_het_job_recs(&het_job_node_list, &het_job_task_cnts,
				   &het_job_tids, total_nnodes);

		mpir_init(total_ntasks);

		opt_iter = list_iterator_create(opt_list);

		/* copy aggregated hetjob data back into each sub-job */
		while ((opt_local = list_next(opt_iter))) {
			srun_opt_t *srun_opt = opt_local->srun_opt;
			xassert(srun_opt);
			job = list_next(job_iter);
			if (!job) {
				slurm_mutex_lock(&step_mutex);
				while (step_cnt > 0)
					slurm_cond_wait(&step_cond,&step_mutex);
				slurm_mutex_unlock(&step_mutex);
				if (first_job) {
					fini_srun(first_job, got_alloc,
						  &global_rc);
				}
				fatal("%s: job allocation count does not match request count (%d != %d)",
				      __func__, list_count(srun_job_list),
				      list_count(opt_list));
				break;	/* To eliminate CLANG error */
			}

			slurm_mutex_lock(&step_mutex);
			step_cnt++;
			slurm_mutex_unlock(&step_mutex);
			job->het_job_node_list = xstrdup(het_job_node_list);
			if ((het_job_step_cnt > 1) && het_job_task_cnts &&
			    het_job_tid_offsets) {
				xassert(node_offset == job->het_job_nnodes);
				job->het_job_task_cnts =
					xcalloc(job->het_job_nnodes,
						sizeof(uint16_t));
				memcpy(job->het_job_task_cnts,
				       het_job_task_cnts,
				       sizeof(uint16_t) * job->het_job_nnodes);
				job->het_job_tids = xcalloc(job->het_job_nnodes,
							    sizeof(uint32_t *));
				memcpy(job->het_job_tids, het_job_tids,
				       sizeof(uint32_t *) *
				       job->het_job_nnodes);

				job->het_job_tid_offsets = xcalloc(
					total_ntasks, sizeof(uint32_t));
				memcpy(job->het_job_tid_offsets,
				       het_job_tid_offsets,
				       sizeof(uint32_t) * total_ntasks);

				job->het_job_step_task_cnts =
					xcalloc(het_job_step_cnt,
						sizeof(uint32_t));
				memcpy(job->het_job_step_task_cnts,
				       het_job_step_task_cnts,
				       sizeof(uint32_t) * het_job_step_cnt);
			}
			opts = xmalloc(sizeof(_launch_app_data_t));
			opts->got_alloc   = got_alloc;
			opts->job         = job;
			opts->opt_local   = opt_local;
			opts->step_cond   = &step_cond;
			opts->step_cnt    = &step_cnt;
			opts->step_mutex  = &step_mutex;
			srun_opt->het_step_cnt = het_job_step_cnt;

			slurm_thread_create_detached(NULL, _launch_one_app,
						     opts);
		}
		xfree(het_job_node_list);
		xfree(het_job_task_cnts);
		xfree(het_job_tid_offsets);
		xfree(het_job_step_task_cnts);
		list_iterator_destroy(job_iter);
		list_iterator_destroy(opt_iter);
		slurm_mutex_lock(&step_mutex);
		while (step_cnt > 0)
			slurm_cond_wait(&step_cond, &step_mutex);
		slurm_mutex_unlock(&step_mutex);

		if (first_job)
			fini_srun(first_job, got_alloc, &global_rc);
	} else {
		int i;
		mpir_init(job->ntasks);
		if (job->het_job_id && (job->het_job_id != NO_VAL)) {
			job->het_job_task_cnts = xcalloc(job->het_job_nnodes,
							 sizeof(uint16_t));
			memcpy(job->het_job_task_cnts,
			       job->step_ctx->step_resp->step_layout->tasks,
			       sizeof(uint16_t) * job->het_job_nnodes);

			job->het_job_tids = xcalloc(job->het_job_nnodes,
						    sizeof(uint32_t *));
			memcpy(job->het_job_tids,
			       job->step_ctx->step_resp->step_layout->tids,
			       sizeof(uint32_t *) * job->het_job_nnodes);
			job->het_job_node_list =
				xstrdup(job->step_ctx->step_resp->
					step_layout->node_list);
			if (!job->het_job_node_list)
				fatal("%s: job %u has NULL hostname",
				      __func__, job->step_id.job_id);

			job->het_job_tid_offsets = xcalloc(job->ntasks,
							   sizeof(uint32_t));
			if (job->het_job_offset) {
				/*
				 * Only starting one hetjob component,
				 * het_job_offset should be zero
				 */
				for (i = 0; i < job->ntasks; i++) {
					job->het_job_tid_offsets[i] =
						job->het_job_offset;
				}
			}
		}
		opts = xmalloc(sizeof(_launch_app_data_t));
		opts->got_alloc   = got_alloc;
		opts->job         = job;
		opts->opt_local   = &opt;
		sropt.het_step_cnt = 1;
		_launch_one_app(opts);
		fini_srun(job, got_alloc, &global_rc);
	}
}

static void _setup_job_env(srun_job_t *job, list_t *srun_job_list, bool got_alloc)
{
	list_itr_t *opt_iter, *job_iter;
	slurm_opt_t *opt_local;

	if (srun_job_list) {
		srun_job_t *first_job = list_peek(srun_job_list);
		if (!opt_list) {
			if (first_job)
				fini_srun(first_job, got_alloc, &global_rc);
			fatal("%s: have srun_job_list, but no opt_list",
			      __func__);
		}
		job_iter  = list_iterator_create(srun_job_list);
		opt_iter  = list_iterator_create(opt_list);
		while ((opt_local = list_next(opt_iter))) {
			job = list_next(job_iter);
			if (!job) {
				if (first_job) {
					fini_srun(first_job, got_alloc,
						  &global_rc);
				}
				fatal("%s: job allocation count does not match request count (%d != %d)",
				      __func__, list_count(srun_job_list),
				      list_count(opt_list));
			}
			setup_one_job_env(opt_local, job, got_alloc);
		}
		list_iterator_destroy(job_iter);
		list_iterator_destroy(opt_iter);
	} else if (job) {
		setup_one_job_env(&opt, job, got_alloc);
	} else {
		fatal("%s: No job information", __func__);
	}
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

static void _setup_env_working_cluster(void)
{
	char *working_env, *addr_ptr, *port_ptr, *rpc_ptr, *tmp = NULL;

	if ((working_env = xstrdup(getenv("SLURM_WORKING_CLUSTER"))) == NULL)
		return;

	/*
	 * Format is cluster_name:[address]:port:rpc in 24.11+ or
	 * cluster_name:address:port:rpc for older versions.
	 * When 24.11 is no longer supported this can be removed.
	 */
	if (!(addr_ptr = strchr(working_env,  ':')))
		goto error;
	/* check for [] around the address */
	if (addr_ptr[1] == '[') {
		if (!(tmp = strchr(addr_ptr, ']')))
			goto error;
		port_ptr = strchr(tmp + 1, ':');
	} else {
		port_ptr = strchr(addr_ptr + 1, ':');
	}
	if (!port_ptr)
		goto error;
	if (!(rpc_ptr  = strchr(port_ptr + 1, ':')))
		goto error;

	if (tmp) {
		/*
		 * Delay increments add_ptr till now for new format to preserve
		 * working_env in error message if failed earlier.
		 */
		*addr_ptr++ = '\0';
		*tmp = '\0';
	}
	*addr_ptr++ = '\0';
	*port_ptr++ = '\0';
	*rpc_ptr++  = '\0';

	if (xstrcmp(slurm_conf.cluster_name, working_env)) {
		working_cluster_rec = xmalloc(sizeof(slurmdb_cluster_rec_t));
		slurmdb_init_cluster_rec(working_cluster_rec, false);

		working_cluster_rec->name = xstrdup(working_env);
		working_cluster_rec->control_host = xstrdup(addr_ptr);
		working_cluster_rec->control_port = strtol(port_ptr, NULL, 10);
		working_cluster_rec->rpc_version  = strtol(rpc_ptr, NULL, 10);
		slurm_set_addr(&working_cluster_rec->control_addr,
			       working_cluster_rec->control_port,
			       working_cluster_rec->control_host);
	}
	xfree(working_env);
	unsetenv("SLURM_WORKING_CLUSTER");
	return;

error:
	error("malformed cluster addr and port in SLURM_WORKING_CLUSTER env var: '%s'",
	      working_env);
	exit(1);
}
