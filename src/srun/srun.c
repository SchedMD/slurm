/*****************************************************************************\
 *  srun.c - user interface to allocate resources, submit jobs, and execute 
 *	parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
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
#include "src/common/plugstack.h"

#include "src/srun/allocate.h"
#include "src/srun/srun_job.h"
#include "src/srun/launch.h"
#include "src/srun/msg.h"
#include "src/srun/opt.h"
#include "src/srun/sigstr.h"
#include "src/srun/debugger.h"
#include "src/srun/srun.h"
#include "src/srun/signals.h"

#define MAX_RETRIES 20
#define MAX_ENTRIES 50

#define	TYPE_NOT_TEXT	0
#define	TYPE_TEXT	1
#define	TYPE_SCRIPT	2

mpi_plugin_client_info_t mpi_job_info[1];

/*
 * forward declaration of static funcs
 */
static void  _print_job_information(resource_allocation_response_msg_t *resp);
static void  _set_prio_process_env(void);
static int   _set_rlimit_env(void);
static int   _set_umask_env(void);
static char *_uint16_array_to_str(int count, const uint16_t *array);
static void  _switch_standalone(srun_job_t *job);
static int   _become_user (void);
static void  _run_srun_prolog (srun_job_t *job);
static void  _run_srun_epilog (srun_job_t *job);
static int   _run_srun_script (srun_job_t *job, char *script);
static int   _change_rlimit_rss(void);
static int   _slurm_debug_env_val (void);
static int   _call_spank_local_user (srun_job_t *job);
static void  _define_symbols(void);

int srun(int ac, char **av)
{
	resource_allocation_response_msg_t *resp;
	srun_job_t *job = NULL;
	int exitcode = 0;
	env_t *env = xmalloc(sizeof(env_t));
	uint32_t job_id = 0;
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;
	slurm_step_io_fds_t fds = SLURM_STEP_IO_FDS_INITIALIZER;
	char **mpi_env = NULL;
	mpi_plugin_client_state_t *mpi_state;
	
	env->stepid = -1;
	env->procid = -1;
	env->localid = -1;
	env->nodeid = -1;
	env->cli = NULL;
	env->env = NULL;

	logopt.stderr_level += _slurm_debug_env_val();
	log_init(xbasename(av[0]), logopt, 0, NULL);

	/* Initialize plugin stack, read options from plugins, etc.
	 */
	if (spank_init(NULL) < 0) {
		fatal("Plug-in initialization failed");
		_define_symbols();
	}

	/* Be sure to call spank_fini when srun exits.
	 */
	if (atexit((void (*) (void)) spank_fini) < 0)
		error("Failed to register atexit handler for plugins: %m");
		
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
		/* If log level is already increased, only increment the
		 *   level to the difference of _verbose an LOG_LEVEL_INFO
		 */
		if ((_verbose -= (logopt.stderr_level - LOG_LEVEL_INFO)) > 0)
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
	if (opt.test_only) {
		int rc = allocate_test();
		if (rc) {
			slurm_perror("allocation failure");
			exit (1);
		}
		info("allocation success");
		exit (0);

	} else if (opt.no_alloc) {
		info("do not allocate resources");
		sig_setup_sigmask();
		job = job_create_noalloc(); 
		_switch_standalone(job);

	} else if ((resp = existing_allocation())) {
		job_id = resp->job_id;
		if (opt.alloc_nodelist == NULL)
                       opt.alloc_nodelist = xstrdup(resp->node_list);

		slurm_free_resource_allocation_response_msg(resp);

		job = job_step_create_allocation(job_id);

		if(!job)
			exit(1);
		
		job->old_job = true;
		sig_setup_sigmask();
			
		if (create_job_step(job) < 0)
			exit(1);
	} else {
		/* Combined job allocation and job step launch */
#ifdef HAVE_FRONT_END
		uid_t my_uid = getuid();
		if ((my_uid != 0)
		&&  (my_uid != slurm_get_slurm_user_id())) {
			error("srun task launch not supported on this system");
			exit(1);
		}
#endif
		if (opt.job_max_memory > 0) {		
			(void) _change_rlimit_rss();
		}
		sig_setup_sigmask();
		if ( !(resp = allocate_nodes()) ) 
			exit(1);
		_print_job_information(resp);
		job = job_create_allocation(resp);
		if(!job)
			exit(1);
		if (create_job_step(job) < 0) {
			srun_job_destroy(job, 0);
			exit(1);
		}
		
		slurm_free_resource_allocation_response_msg(resp);
	}

	/*
	 *  Become --uid user
	 */
	if (_become_user () < 0)
		info ("Warning: Unable to assume uid=%lu\n", opt.uid);

	/* job structure should now be filled in */

	if (_call_spank_local_user (job) < 0)
		job_fatal(job, "Failure in local plugin stack");

	/*
	 *  Enhance environment for job
	 */
	env->nprocs = opt.nprocs;
	env->cpus_per_task = opt.cpus_per_task;
	if (opt.ntasks_per_node != NO_VAL)
		env->ntasks_per_node = opt.ntasks_per_node;
	if (opt.ntasks_per_socket != NO_VAL)
		env->ntasks_per_socket = opt.ntasks_per_socket;
	if (opt.ntasks_per_core != NO_VAL)
		env->ntasks_per_core = opt.ntasks_per_core;
	env->distribution = opt.distribution;
	if (opt.plane_size != NO_VAL)
		env->plane_size = opt.plane_size;
	env->cpu_bind_type = opt.cpu_bind_type;
	env->cpu_bind = opt.cpu_bind;
	env->mem_bind_type = opt.mem_bind_type;
	env->mem_bind = opt.mem_bind;
	env->overcommit = opt.overcommit;
	env->slurmd_debug = opt.slurmd_debug;
	env->labelio = opt.labelio;
	env->comm_port = slurmctld_comm_addr.port;
	env->comm_hostname = slurmctld_comm_addr.hostname;
	if(job) {
		env->select_jobinfo = job->select_jobinfo;
		env->nhosts = job->nhosts;
		env->nodelist = job->nodelist;
		env->task_count = _uint16_array_to_str(
			job->nhosts, job->step_layout->tasks);
		env->jobid = job->jobid;
		env->stepid = job->stepid;
	}
	setup_env(env);
	xfree(env->task_count);
	xfree(env);
	
	_run_srun_prolog(job);

	if (msg_thr_create(job) < 0)
		job_fatal(job, "Unable to create msg thread");

	mpi_job_info->jobid = job->jobid;
	mpi_job_info->stepid = job->stepid;
	mpi_job_info->step_layout = job->step_layout;
	if (!(mpi_state = mpi_hook_client_prelaunch(mpi_job_info, &mpi_env)))
		job_fatal (job, "Failed to initialize MPI");
	env_array_set_environment(mpi_env);
	env_array_free(mpi_env);

	srun_set_stdio_fds(job, &fds);
	job->client_io = client_io_handler_create(fds,
						  job->step_layout->task_cnt,
						  job->step_layout->node_cnt,
						  job->cred,
						  opt.labelio);
	if (!job->client_io
	    || (client_io_handler_start(job->client_io)	!= SLURM_SUCCESS))
		job_fatal(job, "failed to start IO handler");

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
	if (job->state == SRUN_JOB_FORCETERM) {
		info("Force Terminated job");
		srun_job_destroy(job, 0);
		exit(1);
	} else if (job->state == SRUN_JOB_CANCELLED) {
		info("Cancelling job");
		srun_job_destroy(job, NO_VAL);
		exit(1);
	} else if (job->state == SRUN_JOB_FAILED) {
		/* This check here is to check if the job failed
		   because we (srun or slurmd or slurmstepd wasn't
		   able to fork or make a thread or something we still
		   need the job failed check below incase the job
		   failed on it's own.
		*/
		info("Job Failed");
		srun_job_destroy(job, NO_VAL);
		exit(1);
	}

	/*
	 *  We want to make sure we get the correct state of the job
	 *  and not finish before all the messages have been sent.
	 */
/* FIXME - need a new way to tell the message thread to shutdown */
/* 	if (job->state == SRUN_JOB_FAILED) */
/* 		close(job->forked_msg->msg_par->msg_pipe[1]); */
	debug("Waiting for message thread");
	if (pthread_join(job->msg_tid, NULL) < 0)
		error ("Waiting on message thread: %m");
	debug("done");
	
	/* have to check if job was cancelled here just to make sure 
	   state didn't change when we were waiting for the message thread */
	exitcode = set_job_rc(job);
	if (job->state == SRUN_JOB_CANCELLED) {
		info("Cancelling job");
		srun_job_destroy(job, NO_VAL);
	} else if (job->state == SRUN_JOB_FAILED) {
		info("Terminating job");
		srun_job_destroy(job, job->rc);
	} else 
		srun_job_destroy(job, job->rc);
		
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
	debug("done");
	
	
	if (mpi_hook_client_fini (mpi_state) < 0)
		; /* eh, ignore errors here */

	_run_srun_epilog(job);

	/* 
	 *  Let exit() clean up remaining threads.
	 */
	log_fini();
	exit(exitcode);
}

static int _call_spank_local_user (srun_job_t *job)
{
	struct spank_launcher_job_info info[1];

	info->uid = opt.uid;
	info->gid = opt.gid;
	info->jobid = job->jobid;
	info->stepid = job->stepid;
	info->step_layout = job->step_layout;	
	info->argc = opt.argc;
	info->argv = opt.argv;

	return spank_local_user(info);
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
 * are seperated by a comma.  If sequential elements in the array
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

	if(array == NULL)
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


static void 
_print_job_information(resource_allocation_response_msg_t *resp)
{
	int i;
	char tmp_str[10], job_details[4096];

	sprintf(job_details, "jobid %d: nodes(%d):`%s', cpu counts: ", 
	        resp->job_id, resp->node_cnt, resp->node_list);

	for (i = 0; i < resp->num_cpu_groups; i++) {
		sprintf(tmp_str, ",%u(x%u)", resp->cpus_per_node[i], 
		        resp->cpu_count_reps[i]);
		if (i == 0)
			strcat(job_details, &tmp_str[1]);
		else if ((strlen(tmp_str) + strlen(job_details)) < 
		         sizeof(job_details))
			strcat(job_details, tmp_str);
		else
			break;
	}
	verbose("%s",job_details);
}

/* Set SLURM_UMASK environment variable with current state */
static int _set_umask_env(void)
{
	char mask_char[5];
	mode_t mask;

	if (getenv("SLURM_UMASK"))	/* use this value */
		return SLURM_SUCCESS;

	mask = (int)umask(0);
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

/* 
 *  Change SLURM_RLIMIT_RSS to the user specified value --job-mem 
 *  or opt.job_max_memory 
 */
static int _change_rlimit_rss(void)
{
	struct rlimit        rlim[1];
	long                 new_cur;
	int                  rc = SLURM_SUCCESS;
	
	if (getrlimit (RLIMIT_RSS, rlim) < 0)
		return (error ("getrlimit (RLIMIT_RSS): %m"));

	new_cur = opt.job_max_memory*1024; 
	if((new_cur > rlim->rlim_max) || (new_cur < 0))
		rlim->rlim_cur = rlim->rlim_max;
	else
		rlim->rlim_cur = new_cur;

	if (setenvf (NULL, "SLURM_RLIMIT_RSS", "%lu", rlim->rlim_cur) < 0)
		error ("unable to set %s in environment", "RSS");

	if (setrlimit (RLIMIT_RSS, rlim) < 0) 
		return (error ("Unable to change memoryuse: %m"));

	return rc;
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
srun_set_stdio_fds(srun_job_t *job, slurm_step_io_fds_t *cio_fds)
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
			cio_fds->in.nodeid = slurm_step_layout_host_id(
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

/* Plugins must be able to resolve symbols.
 * Since srun statically links with src/api/libslurmhelper rather than 
 * dynamicaly linking with libslurm, we need to reference all needed 
 * symbols within srun. None of the functions below are actually 
 * used, but we need to load the symbols. */
static void _define_symbols(void)
{
	slurm_signal_job_step(0,0,0);	/* needed by mvapich and mpichgm */
}
