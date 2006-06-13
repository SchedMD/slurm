/*****************************************************************************\
 *  slaunch.c - user command for launching parallel jobs
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
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

#define MAX_RETRIES 20
#define MAX_ENTRIES 50

#define	TYPE_NOT_TEXT	0
#define	TYPE_TEXT	1
#define	TYPE_SCRIPT	2

/*
 * declaration of static funcs
 */
static void  _set_prio_process_env(void);
static int   _set_rlimit_env(void);
static int   _set_umask_env(void);
static char *_task_count_string(srun_job_t *job);
static void  _switch_standalone(srun_job_t *job);
static int   _become_user (uid_t uid, gid_t gid);
static void  _run_srun_prolog (srun_job_t *job);
static void  _run_srun_epilog (srun_job_t *job);
static int   _run_srun_script (srun_job_t *job, char *script);

int slaunch(int argc, char **argv)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	job_step_create_request_msg_t step_req;
	slurm_step_ctx step_ctx;
	slurm_job_step_launch_t params;
	slurm_step_io_fds_t fds = SLURM_STEP_IO_FDS_INITIALIZER;
	int rc;

	log_init(xbasename(argv[0]), logopt, 0, NULL);
		
	/* Initialize plugin stack, read options from plugins, etc.
	 */
	if (spank_init(NULL) < 0)
		fatal("Plug-in initialization failed");

	/* Be sure to call spank_fini when srun exits.
	 */
	if (atexit((void (*) (void)) spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	if (initialize_and_process_args(argc, argv) < 0) {
		error ("srun initialization failed");
		exit (1);
	}
	
	/* reinit log with new verbosity (if changed by command line)
	 */
	if (_verbose || opt.quiet) {
		logopt.stderr_level += _verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	if (!opt.jobid_set) {
		error("Must specify a job ID");
		exit(1);
	}

	step_req.job_id = opt.jobid;
	step_req.user_id = getuid();
	step_req.node_count = 1;
	step_req.cpu_count = 1;
	step_req.num_tasks = 1;
	step_req.relative = 0;
	step_req.task_dist = SLURM_DIST_CYCLIC;
	step_req.port = 0;      /* port to contact initiating srun */
	step_req.host = NULL;   /* host to contact initiating srun */
	step_req.node_list = NULL;
	step_req.network = NULL;
	step_req.name = "slaunch";
	
	step_ctx = slurm_step_ctx_create(&step_req);
	if (step_ctx == NULL) {
		error("Could not create job step context: %m");
		exit(1);
	}

	params.gid = opt.gid;
	params.argc = opt.argc;
	params.argv = opt.argv;
	params.multi_prog = opt.multi_prog;
	params.envc = 0; /* FIXME */
	params.env = NULL; /* FIXME */
	params.cwd = opt.cwd;
	params.slurmd_debug = opt.slurmd_debug;
	params.buffered_stdio = !opt.unbuffered;
	params.labelio = opt.labelio;
	params.output_filename = NULL; /* FIXME */
	params.input_filename = NULL; /* FIXME */
	params.error_filename = NULL; /* FIXME */
	params.fds = &fds;

	rc = slurm_step_launch(step_ctx, &params);
	if (rc != SLURM_SUCCESS) {
		error("Application launch failed: %m");
		slurm_step_ctx_destroy(step_ctx);
		exit(1);
	}

	sleep(2);
}

static char *
_task_count_string (srun_job_t *job)
{
	int i, last_val, last_cnt;
	char tmp[16];
	char *str = xstrdup ("");
	if(job->step_layout->tasks == NULL)
		return (str);
	last_val = job->step_layout->cpus[0];
	last_cnt = 1;
	for (i=1; i<job->nhosts; i++) {
		if (last_val == job->step_layout->cpus[i])
			last_cnt++;
		else {
			if (last_cnt > 1)
				sprintf(tmp, "%d(x%d),", last_val, last_cnt);
			else
				sprintf(tmp, "%d,", last_val);
			xstrcat(str, tmp);
			last_val = job->step_layout->cpus[i];
			last_cnt = 1;
		}
	}
	if (last_cnt > 1)
		sprintf(tmp, "%d(x%d)", last_val, last_cnt);
	else
		sprintf(tmp, "%d", last_val);
	xstrcat(str, tmp);
	return (str);
}

static void
_switch_standalone(srun_job_t *job)
{
	int cyclic = (opt.distribution == SLURM_DIST_CYCLIC);

	if (switch_alloc_jobinfo(&job->switch_job) < 0)
		fatal("switch_alloc_jobinfo: %m");
	if (switch_build_jobinfo(job->switch_job, 
				 job->nodelist, 
				 job->step_layout->tasks, 
				 cyclic, opt.network) < 0)
		fatal("switch_build_jobinfo: %m");
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
	 *  Now increase NOFILE to the max available for this srun
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

static void _run_srun_prolog (srun_job_t *job)
{
	int rc;

	if (opt.prolog && strcasecmp(opt.prolog, "none") != 0) {
		rc = _run_srun_script(job, opt.prolog);
		debug("srun prolog rc = %d", rc);
	}
}

static void _run_srun_epilog (srun_job_t *job)
{
	int rc;

	if (opt.epilog && strcasecmp(opt.epilog, "none") != 0) {
		rc = _run_srun_script(job, opt.epilog);
		debug("srun epilog rc = %d", rc);
	}
}

static int _run_srun_script (srun_job_t *job, char *script)
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

static int
_is_local_file (io_filename_t *fname)
{
	if (fname->name == NULL)
		return 1;
	
	if (fname->taskid != -1)
		return 1;

	return ((fname->type != IO_PER_TASK) && (fname->type != IO_ONE));
}

void
slaunch_set_stdio_fds(srun_job_t *job, slurm_step_io_fds_t *cio_fds)
{
	bool err_shares_out = false;

	/*
	 * create stdin file descriptor
	 */
	if (_is_local_file(job->ifname)) {
		if (job->ifname->name == NULL || job->ifname->taskid != -1) {
			cio_fds->in.fd = STDIN_FILENO;
		} else {
			cio_fds->in.fd = open(job->ifname->name, O_RDONLY);
			if (cio_fds->in.fd == -1)
				fatal("Could not open stdin file: %m");
		}
		if (job->ifname->type == IO_ONE) {
			cio_fds->in.taskid = job->ifname->taskid;
			cio_fds->in.nodeid = step_layout_host_id(
				job->step_layout, job->ifname->taskid);
		}
	}

	/*
	 * create stdout file descriptor
	 */
	if (_is_local_file(job->ofname)) {
		if (job->ofname->name == NULL) {
			cio_fds->out.fd = STDOUT_FILENO;
		} else {
			cio_fds->out.fd = open(job->ofname->name,
					       O_CREAT|O_WRONLY|O_TRUNC, 0644);
			if (cio_fds->out.fd == -1)
				fatal("Could not open stdout file: %m");
		}
		if (job->ofname->name != NULL
		    && job->efname->name != NULL
		    && !strcmp(job->ofname->name, job->efname->name)) {
			err_shares_out = true;
		}
	}

	/*
	 * create seperate stderr file descriptor only if stderr is not sharing
	 * the stdout file descriptor
	 */
	if (err_shares_out) {
		debug3("stdout and stderr sharing a file");
		cio_fds->err.fd = cio_fds->out.fd;
	} else if (_is_local_file(job->efname)) {
		if (job->efname->name == NULL) {
			cio_fds->err.fd = STDERR_FILENO;
		} else {
			cio_fds->err.fd = open(job->efname->name,
					       O_CREAT|O_WRONLY|O_TRUNC, 0644);
			if (cio_fds->err.fd == -1)
				fatal("Could not open stderr file: %m");
		}
	}
}

