/*****************************************************************************\
 *  srun.c - user interface to allocate resources, submit jobs, and execute 
 *	parallel jobs.
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

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
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

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/srun/allocate.h"
#include "src/srun/env.h"
#include "src/srun/io.h"
#include "src/srun/job.h"
#include "src/srun/launch.h"
#include "src/srun/msg.h"
#include "src/srun/net.h"
#include "src/srun/opt.h"
#include "src/srun/signals.h"
#include "src/srun/sigstr.h"
#include "src/srun/reattach.h"

#ifdef HAVE_TOTALVIEW
#  include "src/srun/attach.h"
#endif

#define MAX_RETRIES 20

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
static int   _is_file_text (char *, char**);
static int   _run_batch_job (void);
static void  _run_job_script(job_t *job);
static int   _set_batch_script_env(job_t *job);
static int   _set_rlimit_env(void);

#ifdef HAVE_ELAN
#  include "src/common/qsw.h"
   static void _qsw_standalone(job_t *job);
#endif


#if HAVE_TOTALVIEW
int srun(int ac, char **av)
#else
int main(int ac, char **av)
#endif  /* HAVE_TOTALVIEW */
{
	allocation_resp *resp;
	job_t *job;

	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	log_init(xbasename(av[0]), logopt, 0, NULL);

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);

	if (!opt.allocate)
		(void) _set_rlimit_env();

	/* reinit log with new verbosity (if changed by command line)
	 */
	if (_verbose) {
		logopt.stderr_level+=_verbose;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}


	/* now global "opt" should be filled in and available,
	 * create a job from opt
	 */
	if (opt.batch) {
		if (_run_batch_job() < 0)
			exit (1);
		exit (0);

	} else if (opt.no_alloc) {
		info("do not allocate resources");
		sig_setup_sigmask();
		job = job_create_noalloc(); 
#ifdef HAVE_ELAN
		_qsw_standalone(job);
#endif

	} else if ( (resp = existing_allocation()) ) {
		if (opt.allocate) {
			error("job %u already has an allocation", resp->job_id);
			exit(1);
		}
		if (job_resp_hack_for_step(resp))	/* FIXME */
			exit(1);
		job = job_create_allocation(resp); 
		job->old_job = true;
		sig_setup_sigmask();
		create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);

	} else if (opt.allocate) {
		sig_setup_sigmask();
		if ( !(resp = allocate_nodes()) ) 
			exit(1);
		if (_verbose)
			_print_job_information(resp);
		job = job_create_allocation(resp); 
		_run_job_script(job);
		job_destroy(job, 0);

		debug ("Spawned srun shell terminated");
		exit (0);

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
		create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);
	}

	/* job structure should now be filled in */

	/*
	 *  Enhance environment for job
	 */
	setenvf("SLURM_NODELIST=%s", job->nodelist);
	setenvf("SLURM_JOBID=%u",    job->jobid);
	setenvf("SLURM_NPROCS=%d",   opt.nprocs);
	setenvf("SLURM_NNODES=%d",   job->nhosts);

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
	while (job->state < SRUN_JOB_TERMINATED) 
		pthread_cond_wait(&job->state_cond, &job->state_mutex);
	slurm_mutex_unlock(&job->state_mutex);

	/* job is now overdone, clean up  
	 *
	 * If job is "forcefully terminated" exit immediately.
	 *
	 */
	if (job->state == SRUN_JOB_FAILED) {
		info("Terminating job");
		job_destroy(job, 0);
	} else if (job->state == SRUN_JOB_FORCETERM) {
		job_destroy(job, 0);
		exit(1);
	}

	/* wait for launch thread */
	if (pthread_join(job->lid, NULL) < 0)
		error ("Waiting on launch thread: %m");

	/*
	 *  Send SIGHUP to IO thread so it can clean up, then
	 *    wait for all output to complete
	 */
	debug("Waiting for IO thread");
	pthread_kill(job->ioid, SIGHUP);
	if (pthread_join(job->ioid, NULL) < 0)
		error ("Waiting on IO: %m");

	/* Tell slurmctld that job is done */
	job_destroy(job, 0);

	log_fini();

	/* 
	 *  Let exit() clean up remaining threads.
	 */

	exit(job_rc(job));
}


#ifdef HAVE_ELAN
static void
_qsw_standalone(job_t *job)
{
	int i;
	bitstr_t bit_decl(nodeset, QSW_MAX_TASKS);
	bool cyclic = (opt.distribution == SRUN_DIST_CYCLIC);

	for (i = 0; i < job->nhosts; i++) {
		int nodeid;
		if ((nodeid = qsw_getnodeid_byhost(job->host[i])) < 0)
			fatal("qsw_getnodeid_byhost: %m");
		bit_set(nodeset, nodeid);
	}

	if (qsw_alloc_jobinfo(&job->qsw_job) < 0)
		fatal("qsw_alloc_jobinfo: %m");
	if (qsw_setup_jobinfo(job->qsw_job, opt.nprocs, nodeset, cyclic) < 0)
		fatal("qsw_setup_jobinfo: %m");
}
#endif /* HAVE_ELAN */


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
	job_desc_msg_t job;
	submit_response_msg_t *resp;
	extern char **environ;
	char *job_script;

	if ((remote_argc == 0) || (remote_argv[0] == NULL))
		return SLURM_ERROR;

	file_type = _is_file_text (remote_argv[0], NULL);

	/* if (file_type == TYPE_NOT_TEXT) {
         *	error ("file %s is not script", remote_argv[0]);
	 *	return SLURM_ERROR;
	 * }
	 */

	job_script = _build_script (remote_argv[0], file_type);
	if (job_script == NULL) {
		error ("unable to build script from file %s", remote_argv[0]);
		return SLURM_ERROR;
	}

	slurm_init_job_desc_msg(&job);

	job.contiguous     = opt.contiguous;
	job.features       = opt.constraints;

	job.name           = opt.job_name;

	job.partition      = opt.partition;

	if (opt.hold)
		job.priority = 0;
	if (opt.mincpus > -1)
		job.min_procs = opt.mincpus;
	if (opt.realmem > -1)
		job.min_memory = opt.realmem;
	if (opt.tmpdisk > -1)
		job.min_tmp_disk = opt.tmpdisk;

	job.req_nodes      = opt.nodelist;
	job.exc_nodes      = opt.exc_nodes;

	if (opt.overcommit)
		job.num_procs      = opt.min_nodes;
	else
		job.num_procs      = opt.nprocs * opt.cpus_per_task;

	job.min_nodes      = opt.min_nodes;
	if (opt.max_nodes)
		job.max_nodes      = opt.max_nodes;

	job.num_tasks      = opt.nprocs;

	job.user_id        = opt.uid;

	if (opt.hold)
		job.priority 		= 0;
	if (opt.no_kill)
 		job.kill_on_node_fail	= 0;
	if (opt.time_limit > -1)
		job.time_limit		= opt.time_limit;
	if (opt.share)
		job.shared		= 1;

	/* _set_batch_script_env(job); */
	job.environment		= environ;

	job.env_size            = 0;
	while (environ[job.env_size] != NULL)
		job.env_size++;

	job.script		= job_script;
	job.err		        = opt.efname;
	job.in        		= opt.ifname;
	job.out	        	= opt.ofname;
	job.work_dir		= opt.cwd;

	retries = 0;
	while ((rc = slurm_submit_batch_job(&job, &resp)) < 0) {
		if (   (errno == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) 
		    && (retries < MAX_RETRIES) ) {
			if (retries == 0)
				error ("Slurm controller not responding, "
						"sleeping and retrying");
			else
				debug ("Slurm controller not responding, "
						"sleeping and retrying");

			sleep (++retries);
		}
		else {
			error("Unable to submit batch job resources: %s", 
					slurm_strerror(errno));
			return SLURM_ERROR;
		}			
	}

	
	if (rc == SLURM_SUCCESS) {
		info("jobid %u submitted",resp->job_id);
		if (resp->error_code)
			info("Warning: %s", slurm_strerror(resp->error_code));
		slurm_free_submit_response_response_msg (resp);
	}
	xfree (job_script);
	return rc;
}

/* _get_shell - return a string containing the default shell for this user
 * NOTE: This function is NOT reentrant (see getpwuid_r if needed) */
static char *
_get_shell (void)
{
	struct passwd *pw_ent_ptr;

	pw_ent_ptr = getpwuid (getuid ());
	return pw_ent_ptr->pw_shell;
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

	return buffer;
}


static int
_set_batch_script_env(job_t *job)
{
	int rc = SLURM_SUCCESS;
	char *dist = NULL;

	if (job->jobid > 0) {
		if (setenvf("SLURM_JOBID=%u", job->jobid)) {
			error("Unable to set SLURM_JOBID environment");
			rc = SLURM_FAILURE;
		}
	}

	if (job->nhosts > 0) {
		if (setenvf("SLURM_NNODES=%u", job->nhosts)) {
			error("Unable to set SLURM_NNODES environment var");
			rc = SLURM_FAILURE;
		}
	}

	if (job->nodelist) {
		if (setenvf("SLURM_NODELIST=%s", job->nodelist)) {
			error("Unable to set SLURM_NODELIST environment var.");
			rc = SLURM_FAILURE;
		}
	}

	if (opt.nprocs_set && setenvf("SLURM_NPROCS=%u", opt.nprocs)) {
		error("Unable to set SLURM_NPROCS environment variable");
		rc = SLURM_FAILURE;
	}

	
	if ( opt.cpus_set 
	   && setenvf("SLURM_CPUS_PER_TASK=%u", opt.cpus_per_task) ) {
		error("Unable to set SLURM_CPUS_PER_TASK");
		rc = SLURM_FAILURE;
	}
	 

	if (opt.distribution != SRUN_DIST_UNKNOWN) {
		dist = (opt.distribution == SRUN_DIST_BLOCK) ?  
		       "block" : "cyclic";

		if (setenvf("SLURM_DISTRIBUTION=%s", dist)) {
			error("Can't set SLURM_DISTRIBUTION env variable");
			rc = SLURM_FAILURE;
		}
	}

	if ((opt.overcommit) &&
	    (setenvf("SLURM_OVERCOMMIT=1"))) {
		error("Unable to set SLURM_OVERCOMMIT environment variable");
		rc = SLURM_FAILURE;
	}

	if ((opt.slurmd_debug) 
	    && setenvf("SLURMD_DEBUG=%d", opt.slurmd_debug)) {
		error("Can't set SLURMD_DEBUG environment variable");
		rc = SLURM_FAILURE;
	}

	if (opt.labelio 
	   && setenvf("SLURM_LABELIO=1")) {
		error("Unable to set SLURM_LABELIO environment variable");
		rc = SLURM_FAILURE;
	}

	return rc;
}

/* Set SLURM_RLIMIT_* environment variables with current resource 
 * limit values, reset RLIMIT_NOFILE to maximum possible value */
static int _set_rlimit_env(void)
{
	int rc = SLURM_SUCCESS;
	struct rlimit my_rlimit;

	if (getrlimit(RLIMIT_FSIZE, &my_rlimit) ||
	    setenvf("SLURM_RLIMIT_FSIZE=%ld", (long)my_rlimit.rlim_cur)) {
		error("Can't set SLURM_RLIMIT_FSIZE environment variable");
		rc = SLURM_FAILURE;
	}

	if (getrlimit(RLIMIT_STACK, &my_rlimit) ||
	    setenvf("SLURM_RLIMIT_STACK=%ld", (long)my_rlimit.rlim_cur)) {
		error("Can't set SLURM_RLIMIT_STACK environment variable");
		rc = SLURM_FAILURE;
	}

	if (getrlimit(RLIMIT_CORE, &my_rlimit) ||
	    setenvf("SLURM_RLIMIT_CORE=%ld", (long)my_rlimit.rlim_cur)) {
		error("Can't set SLURM_RLIMIT_CORE environment variable");
		rc = SLURM_FAILURE;
	}

	if (getrlimit(RLIMIT_NPROC, &my_rlimit) ||
	    setenvf("SLURM_RLIMIT_NPROC=%ld", (long)my_rlimit.rlim_cur)) {
		error("Can't set SLURM_RLIMIT_NPROC environment variable");
		rc = SLURM_FAILURE;
	}

	if (getrlimit(RLIMIT_NOFILE, &my_rlimit) == 0) {
		if (setenvf("SLURM_RLIMIT_NOFILE=%ld", 
			    (long)my_rlimit.rlim_cur)) {
			error("Can't set SLURM_RLIMIT_NOFILE environment variable");
			rc = SLURM_FAILURE;
		}
		my_rlimit.rlim_cur = my_rlimit.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &my_rlimit)) {
			error("Can't set SLURM_RLIMIT_NOFILE value");
			rc = SLURM_FAILURE;
		}
	} else {
		error("Can't get RLIMIT_NOFILE value");
		rc = SLURM_FAILURE;
	}


	return rc;
}

static void
_print_script_exit_status(const char *argv0, int status)
{
	char *corestr = "";

	if (status == 0) {
		verbose("%s: Done", argv0);
		return;
	}

#ifdef WCOREDUMP
	if (WCOREDUMP(status))
		corestr = " (core dumped)";
#endif

	if (WIFSIGNALED(status))
		error("%s: %s%s", argv0, sigstr(status), corestr);
	else
		error("%s: Exit %d", argv0, WEXITSTATUS(status));

	return;
}

/* allocation option specified, spawn a script and wait for it to exit */
static void _run_job_script (job_t *job)
{
	int   status;
	pid_t cpid;
	char **argv = (remote_argv[0] ? remote_argv : NULL);

	if (_set_batch_script_env(job) < 0) 
		return;

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

	_print_script_exit_status(xbasename(argv[0]), status); 

	if (unsetenv("SLURM_JOBID")) {
		error("Unable to clear SLURM_JOBID environment variable");
		return;
	}

}
