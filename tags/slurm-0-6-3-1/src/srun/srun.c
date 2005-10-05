/*****************************************************************************\
 *  srun.c - user interface to allocate resources, submit jobs, and execute 
 *	parallel jobs.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, et. al.
 *  UCRL-CODE-2002-040.
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

#include "src/srun/allocate.h"
#include "src/srun/io.h"
#include "src/srun/srun_job.h"
#include "src/srun/launch.h"
#include "src/srun/msg.h"
#include "src/srun/opt.h"
#include "src/srun/sigstr.h"
#include "src/srun/reattach.h"
#include "src/srun/attach.h"

#define MAX_RETRIES 20
#define MAX_ENTRIES 50

#define	TYPE_NOT_TEXT	0
#define	TYPE_TEXT	1
#define	TYPE_SCRIPT	2


typedef resource_allocation_response_msg_t         allocation_resp;
typedef resource_allocation_and_run_response_msg_t alloc_run_resp;

/*
 * forward declaration of static funcs
 */
static void  _print_job_information(allocation_resp *resp);
static char *_build_script (char *pathname, int file_type);
static char *_get_shell (void);
static void  _send_options(const int argc, char **argv);
static void  _get_options (const char *buffer);
static int   _is_file_text (char *, char**);
static int   _run_batch_job (void);
static int   _run_job_script(srun_job_t *job, env_t *env);
static int   _set_rlimit_env(void);
static char *_task_count_string(srun_job_t *job);
static void  _switch_standalone(srun_job_t *job);
static int   _become_user (void);
static int   _print_script_exit_status(const char *argv0, int status);
static void  _run_srun_prolog (srun_job_t *job);
static void  _run_srun_epilog (srun_job_t *job);
static int   _run_srun_script (srun_job_t *job, char *script);

int srun(int ac, char **av)
{
	allocation_resp *resp;
	srun_job_t *job;
	char *task_cnt, *bgl_part_id = NULL;
	int exitcode = 0;
	env_t *env = xmalloc(sizeof(env_t));
	char *prolog = NULL;
	char *epilog = NULL;

	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	env->stepid = -1;
	env->gmpi = -1;
	env->procid = -1;
	env->localid = -1;
	env->nodeid = -1;
	env->cli = NULL;
	env->env = NULL;
	
	log_init(xbasename(av[0]), logopt, 0, NULL);
		
	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);

	
	/* reinit log with new verbosity (if changed by command line)
	 */
	if (_verbose || opt.quiet) {
		logopt.stderr_level += _verbose;
		logopt.stderr_level -= opt.quiet;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	if (!opt.allocate)
		(void) _set_rlimit_env();
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

	} else if (opt.batch) {
		if (_run_batch_job() < 0)
			exit (1);
		exit (0);

	} else if (opt.no_alloc) {
		info("do not allocate resources");
		sig_setup_sigmask();
		job = job_create_noalloc(); 
		_switch_standalone(job);

	} else if (opt.allocate) {
		sig_setup_sigmask();
		if ( !(resp = allocate_nodes()) ) 
			exit(1);
		if (opt.noshell) {
			fprintf (stdout, "SLURM_JOBID=%u\n", resp->job_id);
			exit (0);
		}
		if (_become_user () < 0)
			info ("Warning: unable to assume uid=%lu\n", opt.uid);
		if (_verbose)
			_print_job_information(resp);
		job = job_create_allocation(resp); 
		if (msg_thr_create(job) < 0)
			job_fatal(job, "Unable to create msg thread");
		exitcode = _run_job_script(job, env);
		srun_job_destroy(job,exitcode);

		debug ("Spawned srun shell terminated");
		xfree(env->task_count);
		xfree(env);
		exit (exitcode);

	} else if ( (resp = existing_allocation()) ) {
		if (opt.allocate) {
			error("job %u already has an allocation",
				resp->job_id);
			exit(1);
		}
		if (job_resp_hack_for_step(resp))	/* FIXME */
			exit(1);
		job = job_create_allocation(resp);
		job->old_job = true;
		sig_setup_sigmask();
		if (create_job_step(job) < 0)
			exit(1);
		slurm_free_resource_allocation_response_msg(resp);
		
	} else if (mode == MODE_ATTACH) {
		reattach();
		exit (0);

	} else {
		sig_setup_sigmask();
		if ( !(resp = allocate_nodes()) ) 
			exit(1);
		if (_verbose)
			_print_job_information(resp);

		job = job_create_allocation(resp); 
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

	/*
	 *  Enhance environment for job
	 */
	env->nprocs = opt.nprocs;
	env->cpus_per_task = opt.cpus_per_task;
	env->distribution = opt.distribution;
	env->overcommit = opt.overcommit;
	env->slurmd_debug = opt.slurmd_debug;
	env->labelio = opt.labelio;
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

	if (slurm_mpi_thr_create(job) < 0)
		job_fatal (job, "Failed to initialize MPI");

	if (msg_thr_create(job) < 0)
		job_fatal(job, "Unable to create msg thread");

	if (io_thr_create(job) < 0) 
		job_fatal(job, "failed to initialize IO");

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
	 *  Wake up IO thread so it can clean up, then
	 *    wait for all output to complete
	 */
	debug("Waiting for IO thread");
	io_thr_wake(job);
	if (pthread_join(job->ioid, NULL) < 0)
		error ("Waiting on IO: %m");

	if (slurm_mpi_exit () < 0)
		; /* eh, ignore errors here */

	/* Tell slurmctld that job is done */
	srun_job_destroy(job, 0);

	_run_srun_epilog(job);

	log_fini();

	/* 
	 *  Let exit() clean up remaining threads.
	 */

	exit(job_rc(job));
}

static char *
_task_count_string (srun_job_t *job)
{
	int i, last_val, last_cnt;
	char tmp[16];
	char *str = xstrdup ("");

	last_val = job->ntask[0];
	last_cnt = 1;
	for (i=1; i<job->nhosts; i++) {
		if (last_val == job->ntask[i])
			last_cnt++;
		else {
			if (last_cnt > 1)
				sprintf(tmp, "%d(x%d),", last_val, last_cnt);
			else
				sprintf(tmp, "%d,", last_val);
			xstrcat(str, tmp);
			last_val = job->ntask[i];
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
	int cyclic = (opt.distribution == SRUN_DIST_CYCLIC);

	if (switch_alloc_jobinfo(&job->switch_job) < 0)
		fatal("switch_alloc_jobinfo: %m");
	if (switch_build_jobinfo(job->switch_job, job->nodelist, job->ntask, 
				cyclic, opt.network) < 0)
		fatal("switch_build_jobinfo: %m");
}


static void 
_print_job_information(allocation_resp *resp)
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
	info("%s",job_details);
}


/* submit a batch job and return error code */
static int
_run_batch_job(void)
{
	int file_type, retries;
	int rc = SLURM_SUCCESS;
	job_desc_msg_t *req;
	submit_response_msg_t *resp;
	char *script;
	void (*log_msg) (const char *fmt, ...) = (void (*)) &error;

	if ((remote_argc == 0) || (remote_argv[0] == NULL))
		return SLURM_ERROR;

	file_type = _is_file_text (remote_argv[0], NULL);

	/* if (file_type == TYPE_NOT_TEXT) {
         *	error ("file %s is not script", remote_argv[0]);
	 *	return SLURM_ERROR;
	 * }
	 */

	if ((script = _build_script (remote_argv[0], file_type)) == NULL) {
		error ("unable to build script from file %s", remote_argv[0]);
		return SLURM_ERROR;
	}

	if (!(req = job_desc_msg_create_from_opts (script)))
		fatal ("Unable to create job request");

	if (opt.jobid != NO_VAL)
		req->job_id = (uint32_t)opt.jobid;

	retries = 0;
	while (  (retries < MAX_RETRIES)
              && (rc = slurm_submit_batch_job(req, &resp)) < 0) {

		if (errno != ESLURM_ERROR_ON_DESC_TO_RECORD_COPY)
			return (error("Unable to submit batch job: %m"));
		
		(*log_msg) ("Controller not responding, retrying...");
		log_msg = &debug;
		sleep (++retries);
	}

	
	if (rc == SLURM_SUCCESS) {
		if (resp->step_id == NO_VAL)
			info ("jobid %u submitted",resp->job_id);
		else
			info ("jobid %u.%u submitted",resp->job_id,
							resp->step_id);
		if (resp->error_code)
			info("Warning: %s", slurm_strerror(resp->error_code));
		slurm_free_submit_response_response_msg (resp);
	}

	job_desc_msg_destroy (req);
	xfree (script);

	return (rc);
}

static void _send_options(const int argc, char **argv)
{
	int i;
	
	set_options(argc, argv, 0);
	for(i=1; i<argc; i++) {
		debug3("argv[%d] = %s.",i,argv[i]);
		xfree(argv[i]);
	}
}

/* _get_shell - return a string containing the default shell for this user
 * NOTE: This function is NOT reentrant (see getpwuid_r if needed) */
static char *
_get_shell (void)
{
	struct passwd *pw_ent_ptr;

	pw_ent_ptr = getpwuid (getuid ());
	if ( ! pw_ent_ptr ) {
		pw_ent_ptr = getpwnam( "nobody" );
		info( "warning - no user information for user %d", getuid() );
	}
	return pw_ent_ptr->pw_shell;
}

/* _get_opts - gather options put in user script.  Used for batch scripts. */

static void
_get_options (const char *buffer)
{
	int i=0, i2=0;
	int argc = 1;
	char *argv[MAX_ENTRIES];
	
	while(buffer[i]) {
		if(!strncmp(buffer+i, "#SLURM ",7)) {
			i += 7;
			i2 = i;
			while(buffer[i2]!= '\n') {
				if(buffer[i2] == '-') {
					i = i2;
					while(buffer[i] != '\n') {
						if(i != i2 && i != (i2+1) 
						   && buffer[i] == '-') {
							argv[argc] = xmalloc(
								sizeof(char)
								*(i-i2));
							memset(argv[argc], 0,
							       (i-i2));
							strncpy(argv[argc],
								buffer+i2,
								(i-i2-1));
							argc++;
							if(argc>=MAX_ENTRIES) {
								_send_options(
									argc, 
									argv);
								argc = 1;
							}
							i2 = i;
						}
						i++;
						
					}
					argv[argc] = xmalloc(
						sizeof(char)
						*(i-i2+1));
					memset(argv[argc], 0,
					       (i-i2+1));
					strncpy(argv[argc],
						buffer+i2,
						(i-i2));
					i2 = i;
					argc++;
					if(argc>=MAX_ENTRIES) {
						_send_options(argc, argv);
						argc = 1;
					}
							
					break;
				} else
					i2++;				
			}
			i = i2;
		}
			
		i++;
	}
	if(argc > 1)
		_send_options(argc, argv);
	return;
}

#define F 0	/* char never appears in text */
#define T 1	/* character appears in plain ASCII text */
#define I 2	/* character appears in ISO-8859 text */
#define X 3     /* character appears in non-ISO extended ASCII */
static char text_chars[256] = {
	/*                  BEL BS HT LF    FF CR    */
	F, F, F, F, F, F, F, T, T, T, T, F, T, T, F, F,  /* 0x0X */
	/*                              ESC          */
	F, F, F, F, F, F, F, F, F, F, F, T, F, F, F, F,  /* 0x1X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x2X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x3X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x4X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x5X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, T,  /* 0x6X */
	T, T, T, T, T, T, T, T, T, T, T, T, T, T, T, F,  /* 0x7X */
	/*            NEL                            */
	X, X, X, X, X, T, X, X, X, X, X, X, X, X, X, X,  /* 0x8X */
	X, X, X, X, X, X, X, X, X, X, X, X, X, X, X, X,  /* 0x9X */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xaX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xbX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xcX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xdX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,  /* 0xeX */
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I   /* 0xfX */
};


/* _is_file_text - determine if specified file is a script
 * shell_ptr - if not NULL, set to pointer to pathname of specified shell 
 *		(if any, ie. return code of 2)
 *	return 0 if the specified file can not be read or does not contain text
 *	returns 2 if file contains text starting with "#!", otherwise
 *	returns 1 if file contains text, but lacks "#!" header 
 */
static int
_is_file_text (char *fname, char **shell_ptr)
{
	int buf_size, fd, i;
	int rc = 1;	/* initially assume the file contains text */
	unsigned char buffer[8192];

	if (fname[0] != '/') {
		info("warning: %s not found in local path", fname);
		return 0;
	}

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		error ("Unable to open file %s: %m", fname);
		return 0;
	}

	buf_size = read (fd, buffer, sizeof (buffer));
	if (buf_size < 0) {
		error ("Unable to read file %s: %m", fname);
		rc = 0;
	}
	(void) close (fd);

	for (i=0; i<buf_size; i++) {
		if ((int) text_chars[buffer[i]] != T) {
			rc = 0;
			break;
		}
	}

	if ((rc == 1) && (buf_size > 2)) {
		if ((buffer[0] == '#') && (buffer[1] == '!'))
			rc = 2;
	}

	if ((rc == 2) && shell_ptr) {
		shell_ptr[0] = xmalloc (sizeof (buffer));
		for (i=2; i<sizeof(buffer); i++) {
			if (iscntrl (buffer[i])) {
				shell_ptr[0][i-2] = '\0';
				break;
			} else
				shell_ptr[0][i-2] = buffer[i];
		}
		if (i == sizeof(buffer)) {
			error ("shell specified in script too long, not used");
			xfree (shell_ptr[0]);
			shell_ptr[0] = NULL;
		}
	}

	return rc;
}

/* allocate and build a string containing a script for a batch job */
static char *
_build_script (char *fname, int file_type)
{
	cbuf_t cb = cbuf_create(512, 1048576);
	int   fd     = -1;
	int   i      =  0;
	char *buffer = NULL;

	if (file_type != 0) {
		if ((fd = open(fname, O_RDONLY)) < 0) {
			error ("Unable to open file %s: %m", fname);
			return NULL;
		}
	}

	if (file_type != TYPE_SCRIPT) {
		xstrfmtcat(buffer, "#!%s\n", _get_shell());
		if (file_type == 0) {
			xstrcat(buffer, "srun ");
			for (i = 0; i < remote_argc; i++)
				xstrfmtcat(buffer, "%s ", remote_argv[i]);
			xstrcatchar(buffer, '\n');
		}
	} 
	
	if (file_type != 0) {
		int len = buffer ? strlen(buffer) : 0;
		int size;

		while ((size = cbuf_write_from_fd(cb, fd, -1, NULL)) > 0) 
			;
		
		if (size < 0) {
			error ("unable to read %s: %m", fname);
			cbuf_destroy(cb);
			return NULL;
		}

		cbuf_write(cb, "\0", 1, NULL);

		xrealloc(buffer, cbuf_used(cb) + len +1);

		cbuf_read(cb, buffer+len, cbuf_used(cb));

		if (close(fd) < 0)
			error("close: %m");
	}
	
	cbuf_destroy(cb);

	_get_options(buffer);

	return buffer;
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

static int
_print_script_exit_status(const char *argv0, int status)
{
	char *corestr = "";
	int exitcode = 0;

	if (status == 0) {
		verbose("%s: Done", argv0);
		return exitcode;
	}

#ifdef WCOREDUMP
	if (WCOREDUMP(status))
		corestr = " (core dumped)";
#endif

	if (WIFSIGNALED(status)) {
		error("%s: %s%s", argv0, sigstr(status), corestr);
		return WTERMSIG(status) + 128;
	}
	if (WEXITSTATUS(status))
		error("%s: Exit %d", argv0, WEXITSTATUS(status));
	return WEXITSTATUS(status);
}

/* allocation option specified, spawn a script and wait for it to exit */
static int _run_job_script (srun_job_t *job, env_t *env)
{
	int   status, exitcode;
	pid_t cpid;
	char **argv = (remote_argv[0] ? remote_argv : NULL);

	if (opt.nprocs_set)
		env->nprocs = opt.nprocs;
	if (opt.cpus_set)
		env->cpus_per_task = opt.cpus_per_task;
	env->distribution = opt.distribution;
	env->overcommit = opt.overcommit;
	env->slurmd_debug = opt.slurmd_debug;
	env->labelio = opt.labelio;
	if(job) {
		env->select_jobinfo = job->select_jobinfo;
		env->jobid = job->jobid;
		env->nhosts = job->nhosts;
		env->nodelist = job->nodelist;
		env->task_count = _task_count_string (job);
	}
	
	if (setup_env(env) != SLURM_SUCCESS) 
		return SLURM_ERROR;

	if (!argv) {
		/*
		 *  If no arguments were supplied, spawn a shell
		 *    for the user.
		 */
		argv = xmalloc(2 * sizeof(char *));
		argv[0] = _get_shell();
		argv[1] = NULL;
	}

	if ((cpid = fork()) < 0) {
		error("fork: %m");
		exit(1);
	} 

	if (cpid == 0) { 
		/*
		 *  Child.
		 */
#ifdef HAVE_AIX
		(void) mkcrid(0);
#endif
		log_fini();
		sig_unblock_signals();
		execvp(argv[0], argv);
		exit(1);
	}

	/* 
	 *  Parent continues.
	 */

    again:
	if (waitpid(cpid, &status, 0) < (pid_t) 0) {
		if (errno == EINTR)
			goto again;
		error("waitpid: %m");
	}

	exitcode = _print_script_exit_status(xbasename(argv[0]), status); 

	if (unsetenv("SLURM_JOBID")) {
		error("Unable to clear SLURM_JOBID environment variable");
	}
	return exitcode;
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
