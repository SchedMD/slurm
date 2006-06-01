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
#include "src/common/global_srun.h"

#include "src/slaunch/launch.h"
#include "src/slaunch/msg.h"
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


typedef resource_allocation_response_msg_t         allocation_resp;

/*
 * forward declaration of static funcs
 */
static resource_allocation_response_msg_t *_existing_allocation(uint32_t jobid);
static int   _create_job_step(srun_job_t *job,
			      resource_allocation_response_msg_t *alloc_resp);
static void  _set_prio_process_env(void);
static int   _set_rlimit_env(void);
static int   _set_umask_env(void);
static char *_task_count_string(srun_job_t *job);
static void  _switch_standalone(srun_job_t *job);
static int   _become_user (void);
static void  _run_srun_prolog (srun_job_t *job);
static void  _run_srun_epilog (srun_job_t *job);
static int   _run_srun_script (srun_job_t *job, char *script);

int slaunch(int ac, char **av)
{
	allocation_resp *resp;
	srun_job_t *job = NULL;
	int exitcode = 0;
	env_t *env = xmalloc(sizeof(env_t));

	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	
	env->stepid = -1;
	env->procid = -1;
	env->localid = -1;
	env->nodeid = -1;
	env->cli = NULL;
	env->env = NULL;
	
	log_init(xbasename(av[0]), logopt, 0, NULL);
		
	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	if (initialize_and_process_args(ac, av) < 0) {
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

	(void) _set_rlimit_env();
	_set_prio_process_env();
	(void) _set_umask_env();

	/* Set up slurmctld message handler */
	slurmctld_msg_init();
	
	/* now global "opt" should be filled in and available,
	 * create a job from opt
	 */
	if (opt.no_alloc) {
		info("do not allocate resources");
		sig_setup_sigmask();
		job = job_create_noalloc(); 
		_switch_standalone(job);

	} else if (opt.jobid_set && (resp = _existing_allocation(opt.jobid)) ) {
		if (job_resp_hack_for_step(resp))	/* FIXME */
			exit(1);
		
		job = job_create_allocation(resp);
		
		job->old_job = true;
		sig_setup_sigmask();
		
		if (_create_job_step(job, resp) < 0)
			exit(1);
		
		slurm_free_resource_allocation_response_msg(resp);

	} else {
		fatal("You MUST specify a job allocation ID");
	}

	/*
	 *  Become --uid user
	 */
	if (_become_user () < 0)
		info ("Warning: Unable to assume uid=%lu\n", opt.uid);

	/* job structure should now be filled in */

	/*
	 *  Enhance environment for job
	 */
	env->nprocs = opt.nprocs;
	env->cpus_per_task = opt.cpus_per_task;
	env->distribution = opt.distribution;
	env->overcommit = opt.overcommit;
	env->slurmd_debug = opt.slurmd_debug;
	env->labelio = opt.labelio;
	env->comm_port = slurmctld_comm_addr.port;
	env->comm_hostname = slurmctld_comm_addr.hostname;
	if(job) {
		env->select_jobinfo = job->select_jobinfo;
		env->nhosts = job->nhosts;
		env->nodelist = job->nodelist;
		env->task_count = _task_count_string (job);
		env->jobid = job->jobid;
		env->stepid = job->stepid;
	}
	setup_env(env);
	xfree(env->task_count);
	xfree(env);
	
	_run_srun_prolog(job);

	if (msg_thr_create(job) < 0)
		job_fatal(job, "Unable to create msg thread");

	if (slurm_mpi_thr_create(job) < 0)
		job_fatal (job, "Failed to initialize MPI");

	{
		int siglen;
		char *sig;
		client_io_fds_t fds = CLIENT_IO_FDS_INITIALIZER;

		slaunch_set_stdio_fds(job, &fds);

		if (slurm_cred_get_signature(job->cred, &sig, &siglen)
		    < 0) {
			job_fatal(job, "Couldn't get cred signature");
		}
		
		job->client_io = client_io_handler_create(
			fds,
			job->step_layout->num_tasks,
			job->step_layout->num_hosts,
			sig,
			opt.labelio);
		if (!job->client_io
		    || client_io_handler_start(job->client_io) != SLURM_SUCCESS)
			job_fatal(job, "failed to start IO handler");
	}

	if (sig_thr_create(job) < 0)
		job_fatal(job, "Unable to create signals thread: %m");
	
	if (launch_thr_create(job) < 0)
 		job_fatal(job, "Unable to create launch thread: %m");
	
	/* wait for job to terminate 
	 */
	slurm_mutex_lock(&job->state_mutex);
	while (job->state < SRUN_JOB_TERMINATED) {
		pthread_cond_wait(&job->state_cond, &job->state_mutex);
	}
	slurm_mutex_unlock(&job->state_mutex);
	
	/* job is now overdone, clean up  
	 *
	 * If job is "forcefully terminated" exit immediately.
	 *
	 */
	if (job->state == SRUN_JOB_FAILED) {
		info("Terminating job");
		srun_job_destroy(job, 0);
	} else if (job->state == SRUN_JOB_FORCETERM) {
		srun_job_destroy(job, 0);
		exit(1);
	}

	/* wait for launch thread */
	if (pthread_join(job->lid, NULL) < 0)
		error ("Waiting on launch thread: %m");

	/*
	 *  Signal the IO thread to shutdown, which will stop
	 *  the listening socket and file read (stdin) event
	 *  IO objects, but allow file write (stdout) objects to
	 *  complete any writing that remains.
	 */
	debug("Waiting for IO thread");
	if (client_io_handler_finish(job->client_io) != SLURM_SUCCESS)
		error ("IO handler did not finish correctly: %m");
	client_io_handler_destroy(job->client_io);

	if (slurm_mpi_exit () < 0)
		; /* eh, ignore errors here */

	/* Tell slurmctld that job is done */
	srun_job_destroy(job, 0);

	_run_srun_epilog(job);

	/* 
	 *  Let exit() clean up remaining threads.
	 */
	exitcode = job_rc(job);
	log_fini();
	exit(exitcode);
}

static resource_allocation_response_msg_t *
_existing_allocation(uint32_t jobid)
{
	resource_allocation_response_msg_t *resp = NULL;

	if (slurm_allocation_lookup(jobid, &resp) < 0) {
		if (opt.parallel_debug)
			return NULL;	/* create new allocation as needed */
		if (errno == ESLURM_ALREADY_DONE) 
			error ("SLURM job %u has expired.", jobid); 
		else
			error ("Unable to confirm allocation for job %u: %m",
			      jobid);
		info ("Check SLURM_JOBID environment variable " 
		      "for expired or invalid job.");
		exit(1);
	}

	return resp;
}

static job_step_create_request_msg_t *
_step_req_create(srun_job_t *j)
{
	job_step_create_request_msg_t *r = xmalloc(sizeof(*r));
	r->job_id     = j->jobid;
	r->user_id    = opt.uid;
	r->node_count = j->nhosts;
	r->cpu_count  = opt.overcommit ? j->nhosts
		                       : (opt.nprocs*opt.cpus_per_task);
	r->num_tasks  = opt.nprocs;
	r->node_list  = xstrdup(j->nodelist);
	r->network    = xstrdup(opt.network);
	r->name       = xstrdup(opt.job_name);
	r->relative   = false;      /* XXX fix this oneday */
	
	/* CJM - why are "UNKNOWN" and "default" behaviours different? */
	/*       why do we even HAVE the SLURM_DIST_UNKNOWN state?? */
	switch (opt.distribution) {
	case SLURM_DIST_UNKNOWN:
		r->task_dist = (opt.nprocs <= j->nhosts) ? SLURM_DIST_CYCLIC
			                                 : SLURM_DIST_BLOCK;
		break;
	case SLURM_DIST_CYCLIC:
		r->task_dist = SLURM_DIST_CYCLIC;
		break;
	case SLURM_DIST_ARBITRARY:
		r->task_dist = SLURM_DIST_ARBITRARY;
		break;
	case SLURM_DIST_BLOCK:
	default:
		r->task_dist = SLURM_DIST_BLOCK;
		break;
	}

	if (slurmctld_comm_addr.port) {
		r->host = xstrdup(slurmctld_comm_addr.hostname);
		r->port = slurmctld_comm_addr.port;
	}

	return(r);
}

static int
_create_job_step(srun_job_t *job,
		resource_allocation_response_msg_t *alloc_resp)
{
	job_step_create_request_msg_t  *req  = NULL;
	job_step_create_response_msg_t *resp = NULL;
	
	if (!(req = _step_req_create(job))) {
		error ("Unable to allocate step request message");
		return -1;
	}

	if ((slurm_job_step_create(req, &resp) < 0) || (resp == NULL)) {
		error ("Unable to create job step: %m");
		return -1;
	}
	
	job->stepid  = resp->job_step_id;
	job->cred    = resp->cred;
	job->switch_job = resp->switch_job;
	job->step_layout = step_layout_create(alloc_resp, resp, req);
	if(!job->step_layout) {
		error("step_layout not created correctly");
		return 1;
	}
	if(task_layout(job->step_layout) != SLURM_SUCCESS) {
		error("problem with task layout");
		return 1;
	}
	
	/*
	 * Recreate filenames which may depend upon step id
	 */
	job_update_io_fnames(job);

	slurm_free_job_step_create_request_msg(req);
	
	return 0;
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

static int _become_user (void)
{
	struct passwd *pwd = getpwuid (opt.uid);

	if (opt.uid == getuid ())
		return (0);

	if ((opt.egid != (gid_t) -1) && (setgid (opt.egid) < 0))
		return (error ("setgid: %m"));

	initgroups (pwd->pw_name, pwd->pw_gid); /* Ignore errors */

	if (setuid (opt.uid) < 0)
		return (error ("setuid: %m"));

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
		for (i = 0; i < remote_argc; i++) {
			args[i+1] = remote_argv[i];
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
slaunch_set_stdio_fds(srun_job_t *job, client_io_fds_t *cio_fds)
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

