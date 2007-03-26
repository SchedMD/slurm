/*****************************************************************************\
 *  slaunch.c - user command for launching parallel jobs
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>,
 *    Mark Grondona <grondona@llnl.gov>, et. al.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#ifdef HAVE_AIX
#  undef HAVE_UNSETENV
#  include <sys/checkpnt.h>
#endif
#ifndef HAVE_UNSETENV
#  include "src/common/unsetenv.h"
#endif

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>

#include <slurm/slurm.h>

#include "src/common/macros.h"
#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/switch.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/net.h"
#include "src/common/mpi.h"
#include "src/common/slurm_rlimits_info.h"
#include "src/common/plugstack.h"
#include "src/common/env.h"

#include "src/slaunch/opt.h"
#include "src/slaunch/sigstr.h"
#include "src/slaunch/attach.h"
#include "src/slaunch/slaunch.h"
#include "src/slaunch/fname.h"
#include "src/slaunch/multi_prog.h"
#include "src/api/pmi_server.h"

/* FIXME doesn't belong here, we don't want to expose ctx contents */
#include "src/api/step_ctx.h"

extern char **environ;
slurm_step_ctx step_ctx;
int global_rc;
struct {
	bitstr_t *start_success;
	bitstr_t *start_failure;
	bitstr_t *finish_normal;
	bitstr_t *finish_abnormal;
} task_state;

/*
 * declaration of static funcs
 */
static void _set_prio_process_env(char ***env);
static int  _set_rlimit_env(char ***env);
static int  _set_umask_env(char ***env);
static char **_init_task_environment(void);
#if 0
static int  _become_user(uid_t uid, gid_t gid);
#endif
static void _run_slaunch_prolog(char **env);
static void _run_slaunch_epilog(char **env);
static int  _run_slaunch_script(char *script, char **env);
static void _setup_local_fds(slurm_step_io_fds_t *cio_fds, slurm_step_ctx ctx);
static void _task_start(launch_tasks_response_msg_t *msg);
static void _task_finish(task_exit_msg_t *msg);
static void _task_state_struct_init(int num_tasks);
static void _task_state_struct_print(void);
static void _task_state_struct_free(void);
static void _mpir_init(int num_tasks);
static void _mpir_cleanup(void);
static void _mpir_set_executable_names(const char *executable_name);
static void _mpir_dump_proctable(void);
static void _ignore_signal(int signo);
static void _exit_on_signal(int signo);
static int _call_spank_local_user(slurm_step_ctx step_ctx,
				  slurm_step_launch_params_t *step_params);

int slaunch(int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_step_ctx_params_t ctx_params[1];
	slurm_step_launch_params_t launch_params[1];
	slurm_step_launch_callbacks_t callbacks[1];
	char **env;
	uint32_t job_id, step_id;
	int i;

	log_init(xbasename(argv[0]), logopt, 0, NULL);

	xsignal(SIGQUIT, _ignore_signal);
	xsignal(SIGPIPE, _ignore_signal);
	xsignal(SIGUSR1, _ignore_signal);
	xsignal(SIGUSR2, _ignore_signal);
	
	/* Initialize plugin stack, read options from plugins, etc. */
	if (spank_init(NULL) < 0)
		fatal("Plug-in initialization failed");

	/* Be sure to call spank_fini when slaunch exits. */
	if (atexit((void (*) (void)) spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	if (initialize_and_process_args(argc, argv) < 0) {
		error ("slaunch initialization failed");
		exit (1);
	}
	
	/* reinit log with new verbosity (if changed by command line) */
	if (opt.verbose || opt.quiet) {
		logopt.stderr_level += opt.verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}
	debug("slaunch pid %d", getpid());

	/*
	 * Create a job step context.
	 */
	slurm_step_ctx_params_t_init(ctx_params);
	ctx_params->job_id = opt.jobid;
	totalview_jobid = NULL;
	xstrfmtcat(totalview_jobid, "%u", ctx_params->job_id);
	ctx_params->node_count = opt.num_nodes;
	ctx_params->task_count = opt.num_tasks;
	if (opt.cpus_per_task_set) {
		ctx_params->cpu_count = opt.num_tasks * opt.cpus_per_task;
	} else if (opt.overcommit) {
		ctx_params->cpu_count = 0;
	} else {
		ctx_params->cpu_count = opt.num_tasks;
	}
	ctx_params->relative = opt.relative;
	switch (opt.distribution) {
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_BLOCK_CYCLIC:
	case SLURM_DIST_BLOCK_BLOCK:
		ctx_params->task_dist = opt.distribution;
		break;
	case SLURM_DIST_PLANE:
		ctx_params->task_dist = SLURM_DIST_PLANE;
		ctx_params->plane_size = opt.plane_size;
		break;
	default:
		ctx_params->task_dist = (ctx_params->task_count <= 
			ctx_params->node_count) 
			? SLURM_DIST_CYCLIC : SLURM_DIST_BLOCK;
		break;

	}
	ctx_params->overcommit = opt.overcommit;

	/* SLURM overloads the node_list parameter in the
	 * job_step_create_request_msg_t.  It can either be a node list,
	 * or when distribution type is SLURM_DIST_ARBITRARY, it is a list
	 * of repeated nodenames which represent to which node each task
	 * is assigned.
	 */
	if (opt.task_layout_byid_set
	    || opt.task_layout_byname_set
	    || opt.task_layout_file_set) {
		ctx_params->node_list = opt.task_layout;
	} else if (opt.nodelist != NULL) {
		ctx_params->node_list = opt.nodelist;
	} else {
		ctx_params->node_list = NULL; /* let the controller pick nodes */
	}

	ctx_params->network = opt.network;
	ctx_params->name = opt.job_name;
	
	for (i=0; ;i++) {
		step_ctx = slurm_step_ctx_create(ctx_params);
		if (step_ctx != NULL)
			break;
		if (slurm_get_errno() != ESLURM_DISABLED) {
			error("Failed creating job step context: %m");
			exit(1);
		}
		if (i == 0)
			info("Job step creation temporarily disabled, retrying");
		sleep(MIN((i*10), 60));
	}

	/* Now we can register a few more signal handlers.  It
	 * is only safe to have _exit_on_signal call
	 * slurm_step_launch_abort after the the step context
	 * has been created.
	 */
	xsignal(SIGHUP, _exit_on_signal);
	xsignal(SIGINT, _exit_on_signal);
	xsignal(SIGTERM, _exit_on_signal);

	/* set up environment variables */
	env = _init_task_environment();

	/*
	 * Use the job step context to launch the tasks.
	 */
	_task_state_struct_init(opt.num_tasks);
	slurm_step_launch_params_t_init(launch_params);
	launch_params->gid = opt.gid;
	launch_params->argc = opt.argc;
	launch_params->argv = opt.argv;
	launch_params->multi_prog = opt.multi_prog ? true : false;
	launch_params->envc = envcount(env);
	launch_params->env = env;
	launch_params->cwd = opt.cwd;
	launch_params->slurmd_debug = opt.slurmd_debug;
	launch_params->buffered_stdio = opt.unbuffered ? false : true;
	launch_params->labelio = opt.labelio ? true : false;
	launch_params->remote_output_filename = opt.remote_ofname;
	launch_params->remote_input_filename = opt.remote_ifname;
	launch_params->remote_error_filename = opt.remote_efname;
	launch_params->task_prolog = opt.task_prolog;
	launch_params->task_epilog = opt.task_epilog;
	launch_params->cpu_bind = opt.cpu_bind;
	launch_params->cpu_bind_type = opt.cpu_bind_type;
	launch_params->mem_bind = opt.mem_bind;
	launch_params->mem_bind_type = opt.mem_bind_type;
	
	_setup_local_fds(&launch_params->local_fds, step_ctx);
	if (MPIR_being_debugged) {
		launch_params->parallel_debug = true;
		pmi_server_max_threads(1);
	} else {
		launch_params->parallel_debug = false;
	}
	callbacks->task_start = _task_start;
	callbacks->task_finish = _task_finish;

	_run_slaunch_prolog(env);

	_mpir_init(ctx_params->task_count);

	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_JOBID, &job_id);
	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_STEPID, &step_id);
	verbose("Launching job step %u.%u", job_id, step_id);

	_call_spank_local_user(step_ctx, launch_params);

	if (slurm_step_launch(step_ctx, launch_params, callbacks)
	    != SLURM_SUCCESS) {
		error("Application launch failed: %m");
		goto cleanup;
	}

	if (slurm_step_launch_wait_start(step_ctx) == SLURM_SUCCESS) {
		/* Only set up MPIR structures if the step launched
		   correctly. */
		if (opt.multi_prog)
			mpir_set_multi_name(ctx_params->task_count,
					    launch_params->argv[0]);
		else
			_mpir_set_executable_names(launch_params->argv[0]);
		MPIR_debug_state = MPIR_DEBUG_SPAWNED;
		MPIR_Breakpoint();
		if (opt.debugger_test)
			_mpir_dump_proctable();
	} else {
		info("Job step aborted before step completely launched.");
	}

	slurm_step_launch_wait_finish(step_ctx);

cleanup:
	_run_slaunch_epilog(env);
	slurm_step_ctx_destroy(step_ctx);
	_mpir_cleanup();
	_task_state_struct_free();

	return global_rc;
}

/* Set SLURM_UMASK environment variable with current state */
static int _set_umask_env(char ***env)
{
	char mask_char[5];
	mode_t mask = (int)umask(0);
	umask(mask);

	sprintf(mask_char, "0%d%d%d", 
		((mask>>6)&07), ((mask>>3)&07), mask&07);
	if (!env_array_overwrite_fmt(env, "SLURM_UMASK", "%s", mask_char)) {
		error ("unable to set SLURM_UMASK in environment");
		return SLURM_FAILURE;
	}
	debug ("propagating UMASK=%s", mask_char); 
	return SLURM_SUCCESS;
}

/*
 * _set_prio_process_env
 *
 * Set the internal SLURM_PRIO_PROCESS environment variable to support
 * the propagation of the users nice value and the "PropagatePrioProcess"
 * config keyword.
 */
static void _set_prio_process_env(char ***env)
{
	int retval;

	errno = 0; /* needed to detect a real failure since prio can be -1 */

	if ((retval = getpriority (PRIO_PROCESS, 0)) == -1)  {
		if (errno) {
			error ("getpriority(PRIO_PROCESS): %m");
			return;
		}
	}

	if (!env_array_overwrite_fmt(env, "SLURM_PRIO_PROCESS",
				     "%d", retval)) {
		error ("unable to set SLURM_PRIO_PROCESS in environment");
		return;
	}

	debug ("propagating SLURM_PRIO_PROCESS=%d", retval);
}

/* Set SLURM_RLIMIT_* environment variables with current resource 
 * limit values, reset RLIMIT_NOFILE to maximum possible value */
static int _set_rlimit_env(char ***env)
{
	int                  rc = SLURM_SUCCESS;
	struct rlimit        rlim[1];
	unsigned long        cur;
	char                 name[64], *format;
	slurm_rlimits_info_t *rli;

	for (rli = get_slurm_rlimits_info(); rli->name != NULL; rli++ ) {

		if (getrlimit (rli->resource, rlim) < 0) {
			error ("getrlimit (RLIMIT_%s): %m", rli->name);
			rc = SLURM_FAILURE;
			continue;
		}
		
		cur = (unsigned long) rlim->rlim_cur;
		snprintf(name, sizeof(name), "SLURM_RLIMIT_%s", rli->name);
		if (opt.propagate && rli->propagate_flag == PROPAGATE_RLIMITS)
			/*
			 * Prepend 'U' to indicate user requested propagate
			 */
			format = "U%lu";
		else
			format = "%lu";
		
		if (!env_array_overwrite_fmt(env, name, format, cur)) {
			error ("unable to set %s in environment", name);
			rc = SLURM_FAILURE;
			continue;
		}
		
		debug ("propagating RLIMIT_%s=%lu", rli->name, cur);
	}

	/* 
	 *  Now increase NOFILE to the max available for this slaunch
	 */
	if (getrlimit (RLIMIT_NOFILE, rlim) < 0)
	 	return (error ("getrlimit (RLIMIT_NOFILE): %m"));

	if (rlim->rlim_cur < rlim->rlim_max) {
		rlim->rlim_cur = rlim->rlim_max;
		if (setrlimit (RLIMIT_NOFILE, rlim) < 0) 
			return (error ("Unable to increase max no. files: %m"));
	}

	return rc;
}

static char **_init_task_environment(void)
{
	char **env;

	env = env_array_copy((const char **)environ);

	(void)_set_rlimit_env(&env);
	_set_prio_process_env(&env);
	(void)_set_umask_env(&env);

	env_array_overwrite_fmt(&env, "SLURM_CPUS_PER_TASK",
				"%d", opt.cpus_per_task);

	return env;
}

#if 0
static int _become_user (uid_t uid, gid_t gid)
{
	struct passwd *pwd = getpwuid (opt.uid);

	if (uid == getuid())
		return (0);

	if ((gid != (gid_t)-1) && (setgid(gid) < 0))
		return (error("setgid: %m"));

	initgroups(pwd->pw_name, pwd->pw_gid); /* Ignore errors */

	if (setuid(uid) < 0)
		return (error("setuid: %m"));

	return (0);
}
#endif

static void _run_slaunch_prolog (char **env)
{
	int rc;

	if (opt.prolog && strcasecmp(opt.prolog, "none") != 0) {
		rc = _run_slaunch_script(opt.prolog, env);
		debug("slaunch prolog rc = %d", rc);
	}
}

static void _run_slaunch_epilog (char **env)
{
	int rc;

	if (opt.epilog && strcasecmp(opt.epilog, "none") != 0) {
		rc = _run_slaunch_script(opt.epilog, env);
		debug("slaunch epilog rc = %d", rc);
	}
}

static int _run_slaunch_script (char *script, char **env)
{
	int status;
	pid_t cpid;
	int i;
	char **args = NULL;

	if (script == NULL || script[0] == '\0')
		return 0;

	if (access(script, R_OK | X_OK) < 0) {
		info("Access denied for %s: %m", script);
		return 0;
	}

	if ((cpid = fork()) < 0) {
		error ("run_slaunch_script: fork: %m");
		return -1;
	}
	if (cpid == 0) {
		/* set the script's command line arguments to the arguments
		 * for the application, but shifted one higher
		 */
		args = xmalloc(sizeof(char *) * (opt.argc+2));
		args[0] = script;
		for (i = 0; i < opt.argc; i++) {
			args[i+1] = opt.argv[i];
		}
		args[i+1] = NULL;
		execve(script, args, env);
		error("help! %m");
		exit(127);
	}

	do {
		if (waitpid(cpid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			error("waidpid: %m");
			return 0;
		} else
			return status;
	} while(1);

	/* NOTREACHED */
}

static int
_taskid_to_nodeid(slurm_step_layout_t *layout, int taskid)
{
	int i, nodeid;

	for (nodeid = 0; nodeid < layout->node_cnt; nodeid++) {
		for (i = 0; i < layout->tasks[nodeid]; i++) {
			if (layout->tids[nodeid][i] == taskid) {
				debug3("task %d is on node %d",
				       taskid, nodeid);
				return nodeid;
			}
		}
	}

	return -1; /* node ID not found */
}

static void
_setup_local_fds(slurm_step_io_fds_t *cio_fds, slurm_step_ctx ctx)
{
	bool err_shares_out = false;
	fname_t *ifname, *ofname, *efname;
	uint32_t job_id, step_id;

	slurm_step_ctx_get(ctx, SLURM_STEP_CTX_JOBID, &job_id);
	slurm_step_ctx_get(ctx, SLURM_STEP_CTX_STEPID, &step_id);

	ifname = fname_create(opt.local_ifname, (int)job_id, (int)step_id);
	ofname = fname_create(opt.local_ofname, (int)job_id, (int)step_id);
	efname = fname_create(opt.local_efname, (int)job_id, (int)step_id);

	/*
	 * create stdin file descriptor
	 */
	if (ifname->name == NULL) {
		cio_fds->in.fd = STDIN_FILENO;
	} else {
		cio_fds->in.fd = open(ifname->name, O_RDONLY);
		if (cio_fds->in.fd == -1)
			fatal("Could not open stdin file: %m");
	}
	/*
	 * create stdout file descriptor
	 */
	if (ofname->name == NULL) {
		cio_fds->out.fd = STDOUT_FILENO;
	} else {
		cio_fds->out.fd = open(ofname->name,
				       O_CREAT|O_WRONLY|O_TRUNC, 0644);
		if (cio_fds->out.fd == -1)
			fatal("Could not open stdout file: %m");
	}
	/* FIXME - need to change condition for shared output and error */
	if (ofname->name != NULL
	    && efname->name != NULL
	    && !strcmp(ofname->name, efname->name)) {
		err_shares_out = true;
	}

	/*
	 * create seperate stderr file descriptor only if stderr is not sharing
	 * the stdout file descriptor
	 */
	if (err_shares_out) {
		debug3("stdout and stderr sharing a file");
		cio_fds->err.fd = cio_fds->out.fd;
		cio_fds->err.taskid = cio_fds->out.taskid;
	} else {
		if (efname->name == NULL) {
			cio_fds->err.fd = STDERR_FILENO;
		} else {
			cio_fds->err.fd = open(efname->name,
					       O_CREAT|O_WRONLY|O_TRUNC, 0644);
			if (cio_fds->err.fd == -1)
				fatal("Could not open stderr file: %m");
		}
	}


	/*
	 * set up local standard IO filters
	 */
	if (opt.local_input_filter_set) {
		cio_fds->in.taskid = opt.local_input_filter;
	}
	/* FIXME - don't peek into the step context, that's cheating! */
	if (opt.local_input_filter != (uint32_t)-1) {
		cio_fds->in.nodeid =
			_taskid_to_nodeid(step_ctx->step_resp->step_layout,
					  opt.local_input_filter);
	}
	if (opt.local_output_filter_set) {
		cio_fds->out.taskid = opt.local_output_filter;
	}
	if (opt.local_error_filter_set) {
		cio_fds->err.taskid = opt.local_error_filter;
	} else if (opt.local_output_filter_set) {
		cio_fds->err.taskid = opt.local_output_filter;
	}
}

static void
_task_start(launch_tasks_response_msg_t *msg)
{
	MPIR_PROCDESC *table;
	int taskid;
	int i;

	verbose("Node %s (%d), %d tasks started",
		msg->node_name, msg->srun_node_id, msg->count_of_pids);

	for (i = 0; i < msg->count_of_pids; i++) {
		taskid = msg->task_ids[i];
		table = &MPIR_proctable[taskid];
		table->host_name = xstrdup(msg->node_name);
		/* table->executable_name is set elsewhere */
		table->pid = msg->local_pids[i];

		if (msg->return_code == 0) {
			bit_set(task_state.start_success, taskid);
		} else {
			bit_set(task_state.start_failure, taskid);
		}
	}

}

static void
_terminate_job_step(slurm_step_ctx ctx)
{
	uint32_t job_id, step_id;

	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_JOBID, &job_id);
	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_STEPID, &step_id);
	info("Terminating job step %u.%u", job_id, step_id);
	slurm_kill_job_step(job_id, step_id, SIGKILL);
}

static void
_handle_max_wait(int signo)
{
	info("First task exited %ds ago", opt.max_wait);
	_task_state_struct_print();
	_terminate_job_step(step_ctx);
}

static void
_task_finish(task_exit_msg_t *msg)
{
	static bool first_done = true;
	static bool first_error = true;
	int rc = 0;
	int i;

	verbose("%d tasks finished (rc=%u)",
		msg->num_tasks, msg->return_code);
	if (WIFEXITED(msg->return_code)) {
		rc = WEXITSTATUS(msg->return_code);
		if (rc != 0) {
			for (i = 0; i < msg->num_tasks; i++) {
				error("task %u exited with exit code %d",
				      msg->task_id_list[i], rc);
				bit_set(task_state.finish_abnormal,
					msg->task_id_list[i]);
			}
		} else {
			for (i = 0; i < msg->num_tasks; i++) {
				bit_set(task_state.finish_normal,
					msg->task_id_list[i]);
			}
		}
	} else if (WIFSIGNALED(msg->return_code)) {
		for (i = 0; i < msg->num_tasks; i++) {
			verbose("task %u killed by signal %d",
				msg->task_id_list[i],
				WTERMSIG(msg->return_code));
			bit_set(task_state.finish_abnormal,
				msg->task_id_list[i]);
		}
		rc = 1;
	}
	global_rc = MAX(global_rc, rc);

	if (first_error && rc > 0 && opt.kill_bad_exit) {
		first_error = false;
		_terminate_job_step(step_ctx);
	} else if (first_done && opt.max_wait > 0) {
		/* If these are the first tasks to finish we need to
		 * start a timer to kill off the job step if the other
		 * tasks don't finish within opt.max_wait seconds.
		 */
		first_done = false;
		debug2("First task has exited");
		xsignal(SIGALRM, _handle_max_wait);
		verbose("starting alarm of %d seconds", opt.max_wait);
		alarm(opt.max_wait);
	}
}

static void
_task_state_struct_init(int num_tasks)
{
	task_state.start_success = bit_alloc(num_tasks);
	task_state.start_failure = bit_alloc(num_tasks);
	task_state.finish_normal = bit_alloc(num_tasks);
	task_state.finish_abnormal = bit_alloc(num_tasks);
}

/*
 * Tasks will most likely have bits set in multiple of the task_state
 * bit strings (e.g. a task can start normally and then later exit normally)
 * so we ensure that a task is only "seen" once.
 */
static void
_task_state_struct_print(void)
{
	bitstr_t *tmp, *seen, *not_seen;
	char buf[BUFSIZ];
	int len;

	len = bit_size(task_state.finish_abnormal); /* all the same length */
	tmp = bit_alloc(len);
	seen = bit_alloc(len);
	not_seen = bit_alloc(len);
	bit_not(not_seen);

	if (bit_set_count(task_state.finish_abnormal) > 0) {
		bit_copybits(tmp, task_state.finish_abnormal);
		bit_and(tmp, not_seen);
		bit_fmt(buf, BUFSIZ, tmp);
		info("task%s: exited abnormally", buf);
		bit_or(seen, tmp);
		bit_copybits(not_seen, seen);
		bit_not(not_seen);
	}

	if (bit_set_count(task_state.finish_normal) > 0) {
		bit_copybits(tmp, task_state.finish_normal);
		bit_and(tmp, not_seen);
		bit_fmt(buf, BUFSIZ, tmp);
		info("task%s: exited", buf);
		bit_or(seen, tmp);
		bit_copybits(not_seen, seen);
		bit_not(not_seen);
	}

	if (bit_set_count(task_state.start_failure) > 0) {
		bit_copybits(tmp, task_state.start_failure);
		bit_and(tmp, not_seen);
		bit_fmt(buf, BUFSIZ, tmp);
		info("task%s: failed to start", buf);
		bit_or(seen, tmp);
		bit_copybits(not_seen, seen);
		bit_not(not_seen);
	}

	if (bit_set_count(task_state.start_success) > 0) {
		bit_copybits(tmp, task_state.start_success);
		bit_and(tmp, not_seen);
		bit_fmt(buf, BUFSIZ, tmp);
		info("task%s: running", buf);
		bit_or(seen, tmp);
		bit_copybits(not_seen, seen);
		bit_not(not_seen);
	}
}

static void
_task_state_struct_free(void)
{
	bit_free(task_state.start_success);
	bit_free(task_state.start_failure);
	bit_free(task_state.finish_normal);
	bit_free(task_state.finish_abnormal);
}


/* FIXME - maybe we can push this under the step_launch function? */
static int _call_spank_local_user (slurm_step_ctx step_ctx,
				   slurm_step_launch_params_t *step_params)
{
	struct spank_launcher_job_info info[1];
	job_step_create_response_msg_t *step_resp;

	info->uid = getuid();
	info->gid = step_params->gid;
	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_JOBID, &info->jobid);
	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_STEPID, &info->stepid);
	slurm_step_ctx_get(step_ctx, SLURM_STEP_CTX_RESP, &step_resp);
	info->step_layout = step_resp->step_layout;
	info->argc = step_params->argc;
	info->argv = step_params->argv;

	return spank_local_user(info);
}


/**********************************************************************
 * Functions for manipulating the MPIR_* global variables which
 * are accessed by parallel debuggers which trace slaunch.
 **********************************************************************/
static void
_mpir_init(int num_tasks)
{
	MPIR_proctable_size = num_tasks;
	MPIR_proctable = xmalloc(sizeof(MPIR_PROCDESC) * num_tasks);
	if (MPIR_proctable == NULL)
		fatal("Unable to initialize MPIR_proctable: %m");
}

static void
_mpir_cleanup()
{
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		xfree(MPIR_proctable[i].host_name);
		xfree(MPIR_proctable[i].executable_name);
	}
	xfree(MPIR_proctable);
}

static void
_mpir_set_executable_names(const char *executable_name)
{
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		MPIR_proctable[i].executable_name = xstrdup(executable_name);
		if (MPIR_proctable[i].executable_name == NULL)
			fatal("Unable to set MPI_proctable executable_name:"
			      " %m");
	}
}

static void
_mpir_dump_proctable()
{
	MPIR_PROCDESC *tv;
	int i;

	for (i = 0; i < MPIR_proctable_size; i++) {
		tv = &MPIR_proctable[i];
		if (!tv)
			break;
		info("task:%d, host:%s, pid:%d, executable:%s",
		     i, tv->host_name, tv->pid, tv->executable_name);
	}
}
	
static void _ignore_signal(int signo)
{
	/* do nothing */
}

static void _exit_on_signal(int signo)
{
	slurm_step_launch_abort(step_ctx);
}
