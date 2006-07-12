/*****************************************************************************\
 *  slaunch.c - user command for launching parallel jobs
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Christopher J. Morrone <morrone2@llnl.gov>,
 *    Mark Grondona <grondona@llnl.gov>, et. al.
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

#include "src/slaunch/opt.h"
#include "src/slaunch/sigstr.h"
#include "src/slaunch/attach.h"
#include "src/slaunch/slaunch.h"
#include "src/slaunch/fname.h"
#include "src/slaunch/multi_prog.h"

/* FIXME doesn't belong here, we don't want to expose ctx contents */
#include "src/api/step_ctx.h"

/*
 * declaration of static funcs
 */
static void  _set_prio_process_env(void);
static int   _set_rlimit_env(void);
static int   _set_umask_env(void);
static int   _become_user (uid_t uid, gid_t gid);
static void  _run_srun_prolog (void);
static void  _run_srun_epilog (void);
static int   _run_srun_script (char *script);
static void  _setup_local_fds(slurm_step_io_fds_t *cio_fds, int jobid,
			      int stepid, slurm_step_layout_t *step_layout);
static void _task_start(launch_tasks_response_msg_t *msg);
static void _task_finish(task_exit_msg_t *msg);
static void _mpir_init(int num_tasks);
static void _mpir_cleanup(void);
static void _mpir_set_executable_names(const char *executable_name);
static void _mpir_dump_proctable(void);

int slaunch(int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_step_create_request_msg_t step_req;
	slurm_step_ctx step_ctx;
	slurm_job_step_launch_t params;
	int rc;

	log_init(xbasename(argv[0]), logopt, 0, NULL);

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
	if (_verbose || opt.quiet) {
		logopt.stderr_level += _verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	(void)_set_rlimit_env();
	_set_prio_process_env();
	(void)_set_umask_env();

	/*
	 * Create a job step context.
	 */
	step_req.job_id = opt.jobid;
	totalview_jobid = NULL;
	xstrfmtcat(totalview_jobid, "%u", step_req.job_id);
	step_req.user_id = getuid();
	step_req.node_count = opt.num_nodes;
	if (opt.num_tasks_set)
		step_req.num_tasks = opt.num_tasks;
	else
		step_req.num_tasks = opt.num_nodes;
/* 	step_req.cpu_count = opt.cpus_per_task; */
	step_req.cpu_count = 0;
	step_req.relative = opt.relative;
	step_req.task_dist = SLURM_DIST_CYCLIC;
	step_req.port = 0;      /* historical, used by srun */
	step_req.host = NULL;   /* historical, used by srun */
	step_req.node_list = NULL;
	step_req.network = NULL;
	step_req.name = opt.job_name;
	
	step_ctx = slurm_step_ctx_create(&step_req);
	if (step_ctx == NULL) {
		error("Failed creating job step context: %m");
		exit(1);
	}

	/*
	 * Use the job step context to launch the tasks.
	 */
	slurm_job_step_launch_t_init(&params);
	params.gid = opt.gid;
	params.argc = opt.argc;
	params.argv = opt.argv;
	params.multi_prog = opt.multi_prog ? true : false;
	params.envc = 0; /* FIXME */
	params.env = NULL; /* FIXME */
	params.cwd = opt.cwd;
	params.slurmd_debug = opt.slurmd_debug;
	params.buffered_stdio = opt.unbuffered ? false : true;
	params.labelio = opt.labelio ? true : false;
	params.remote_output_filename = opt.remote_ofname;
	params.remote_input_filename = opt.remote_ifname;
	params.remote_error_filename = opt.remote_efname;
	/* FIXME - don't peek into the step context, that's cheating! */
	_setup_local_fds(&params.local_fds, (int)step_ctx->job_id,
			 (int)step_ctx->step_resp->job_step_id,
			 step_ctx->step_layout);
	params.parallel_debug = opt.parallel_debug ? true : false;
	params.task_start_callback = _task_start;
	params.task_finish_callback = _task_finish;

	_mpir_init(step_req.num_tasks);

	rc = slurm_step_launch(step_ctx, &params);
	if (rc != SLURM_SUCCESS) {
		error("Application launch failed: %m");
		goto cleanup;
	}

	slurm_step_launch_wait_start(step_ctx);

	if (opt.multi_prog)
		mpir_set_multi_name(step_req.num_tasks);
	else
		_mpir_set_executable_names(params.argv[0]);
	MPIR_debug_state = MPIR_DEBUG_SPAWNED;
	MPIR_Breakpoint();
	if (opt.debugger_test)
		_mpir_dump_proctable();

	slurm_step_launch_wait_finish(step_ctx);

cleanup:
	/* Clean up. */
	slurm_step_ctx_destroy(step_ctx);
	_mpir_cleanup();

	return 0;
}

/* Set SLURM_UMASK environment variable with current state */
static int _set_umask_env(void)
{
	char mask_char[5];
	mode_t mask = (int)umask(0);
	umask(mask);

	sprintf(mask_char, "0%d%d%d", 
		((mask>>6)&07), ((mask>>3)&07), mask&07);
	if (setenvf(NULL, "SLURM_UMASK", "%s", mask_char) < 0) {
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
static void  _set_prio_process_env(void)
{
	int retval;

	errno = 0; /* needed to detect a real failure since prio can be -1 */

	if ((retval = getpriority (PRIO_PROCESS, 0)) == -1)  {
		if (errno) {
			error ("getpriority(PRIO_PROCESS): %m");
			return;
		}
	}

	if (setenvf (NULL, "SLURM_PRIO_PROCESS", "%d", retval) < 0) {
		error ("unable to set SLURM_PRIO_PROCESS in environment");
		return;
	}

	debug ("propagating SLURM_PRIO_PROCESS=%d", retval);
}

/* Set SLURM_RLIMIT_* environment variables with current resource 
 * limit values, reset RLIMIT_NOFILE to maximum possible value */
static int _set_rlimit_env(void)
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
		
		if (setenvf (NULL, name, format, cur) < 0) {
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

static void _run_srun_prolog (void)
{
	int rc;

	if (opt.prolog && strcasecmp(opt.prolog, "none") != 0) {
		rc = _run_srun_script(opt.prolog);
		debug("srun prolog rc = %d", rc);
	}
}

static void _run_srun_epilog (void)
{
	int rc;

	if (opt.epilog && strcasecmp(opt.epilog, "none") != 0) {
		rc = _run_srun_script(opt.epilog);
		debug("srun epilog rc = %d", rc);
	}
}

static int _run_srun_script (char *script)
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
		error ("run_srun_script: fork: %m");
		return -1;
	}
	if (cpid == 0) {

		/* set the scripts command line arguments to the arguments
		 * for the application, but shifted one higher
		 */
		args = xmalloc(sizeof(char *) * 1024);
		args[0] = script;
		for (i = 0; i < opt.argc; i++) {
			args[i+1] = opt.argv[i];
		}
		args[i+1] = NULL;
		execv(script, args);
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

static void
_setup_local_fds(slurm_step_io_fds_t *cio_fds, int jobid, int stepid,
		 slurm_step_layout_t *step_layout)
{
	bool err_shares_out = false;
	fname_t *ifname, *ofname, *efname;

	ifname = fname_create(opt.local_ifname, jobid, stepid);
	ofname = fname_create(opt.local_ofname, jobid, stepid);
	efname = fname_create(opt.local_efname, jobid, stepid);

	/*
	 * create stdin file descriptor
	 */
	if (ifname->name == NULL) {
		cio_fds->in.fd = STDIN_FILENO;
	} else if (ifname->type == IO_ONE) {
		cio_fds->in.taskid = ifname->taskid;
		cio_fds->in.nodeid = step_layout_host_id(
			step_layout, ifname->taskid);
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
	}

}

static void
_task_finish(task_exit_msg_t *msg)
{
	verbose("%d tasks finished (rc=%d)",
		msg->num_tasks, msg->return_code);
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
	
