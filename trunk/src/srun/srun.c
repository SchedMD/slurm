/*****************************************************************************\
 *  srun.c - user interface to allocate resources, submit jobs, and execute 
 *	parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>, 
 *             Moe Jette <jette1@llnl.gov>, et. al.
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

#include "src/api/slurm.h"

#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/srun/env.h"
#include "src/srun/io.h"
#include "src/srun/job.h"
#include "src/srun/launch.h"
#include "src/srun/reattach.h"
#include "src/srun/opt.h"

#include "src/srun/net.h"
#include "src/srun/msg.h"
#include "src/srun/io.h"

#ifdef HAVE_TOTALVIEW
#  include "src/srun/attach.h"
#endif

#define MAX_RETRIES 20

typedef resource_allocation_response_msg_t         allocation_resp;
typedef resource_allocation_and_run_response_msg_t alloc_run_resp;

#define	TYPE_NOT_TEXT	0
#define	TYPE_TEXT	1
#define	TYPE_SCRIPT	2

/* number of active threads */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  active_cond  = PTHREAD_COND_INITIALIZER;
static int             active = 0;

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
} thd_t;

typedef struct task_info {
	slurm_msg_t *req_ptr;
	job_t *job_ptr;
	int host_inx;
} task_info_t;

/*
 * forward declaration of static funcs
 */
static allocation_resp	*_allocate_nodes(void);
static void              _print_job_information(allocation_resp *resp);
static void		 _create_job_step(job_t *job);
static void		 _sigterm_handler(int signum);
static void		 _sig_kill_alloc(int signum);
static void *            _sig_thr(void *arg);
static char 		*_build_script (char *pathname, int file_type);
static char 		*_get_shell (void);
static int 		 _is_file_text (char *fname, char** shell_ptr);
static int		 _run_batch_job (void);
static allocation_resp	*_existing_allocation(void);
static void		 _run_job_script(uint32_t jobid);
static void 		 _fwd_signal(job_t *job, int signo);
static void 		 _p_fwd_signal(slurm_msg_t *req_array_ptr, job_t *job);
static void 		*_p_signal_task(void *args);
static int               _set_batch_script_env(uint32_t jobid);

#ifdef HAVE_LIBELAN3
#  include "src/common/qsw.h"
   static void _qsw_standalone(job_t *job);
#endif

int
srun(int ac, char **av)
{
	sigset_t sigset;
	allocation_resp *resp;
	job_t *job;
	pthread_attr_t attr;
	struct sigaction action;
	bool old_job = false;
	struct rlimit rlim;

	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	log_init(xbasename(av[0]), logopt, 0, NULL);

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		rlim.rlim_cur = rlim.rlim_max;
		setrlimit(RLIMIT_NOFILE, &rlim);
	}

	/* reinit log with new verbosity (if changed by command line)
	 */
	if (_verbose || _debug) {
		if (_verbose) 
			logopt.stderr_level+=_verbose;
		logopt.prefix_level = 1;
		log_alter(logopt, 0, NULL);
	}

	/* now global "opt" should be filled in and available,
	 * create a job from opt
	 */
	if (opt.batch) {
		if (_run_batch_job())
			exit (1);
		exit (0);

	} else if (opt.no_alloc) {
		info("do not allocate resources");
		job = job_create_noalloc(); 
#ifdef HAVE_LIBELAN3
		_qsw_standalone(job);
#endif

	} else if ( (resp = _existing_allocation()) ) {
		old_job = true;
		job = job_create_allocation(resp); 
		_create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);

	} else if (opt.allocate) {
		if ( !(resp = _allocate_nodes()) ) 
			exit(1);
		if (_verbose || _debug)
			_print_job_information(resp);
		job = job_create_allocation(resp); 
		_run_job_script(resp->job_id);
		slurm_complete_job(resp->job_id, 0, 0);

		debug ("Spawned srun shell terminated");
		exit (0);

	} else if (mode == MODE_ATTACH) {
		reattach();
		exit (0);

	} else {
		if ( !(resp = _allocate_nodes()) ) 
			exit(1);
		if (_verbose || _debug)
			_print_job_information(resp);

		job = job_create_allocation(resp); 
		_create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);
	}

	/* block most signals in all threads, except sigterm */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGTSTP);
	sigaddset(&sigset, SIGSTOP);
	if (sigprocmask(SIG_BLOCK, &sigset, NULL) != 0)
		fatal("sigprocmask: %m");
	action.sa_handler = &_sigterm_handler;
	action.sa_flags   = 0;
	sigaction(SIGTERM, &action, NULL);

	/* job structure should now be filled in */

	if (msg_thr_create(job) < 0)
		fatal("Unable to create msg thread");

	if (io_thr_create(job) < 0) 
		fatal("Unable to create IO thread");

	pthread_attr_init(&attr);
	/* spawn signal thread */
	if ((errno = pthread_create(&job->sigid, &attr, &_sig_thr, 
	                            (void *) job)) != 0)
		fatal("Unable to create signal thread. %m");
	debug("Started signals thread (%d)", job->sigid);


	/* launch jobs */
	if ((errno = pthread_create(&job->lid, &attr, &launch, (void *) job)))
		fatal("Unable to create launch thread. %m");
	debug("Started launch thread (%d)", job->lid);

	/* wait for job to terminate */
	pthread_mutex_lock(&job->state_mutex);
	debug3("before main state loop: state = %d", job->state);
	while ((job->state != SRUN_JOB_OVERDONE) && 
	       (job->state != SRUN_JOB_FAILED  )) {
		pthread_cond_wait(&job->state_cond, &job->state_mutex);
	}
	pthread_mutex_unlock(&job->state_mutex);

	/* job is now overdone, clean up  */
	if (job->state == SRUN_JOB_FAILED) {
		info("sending SIGINT to job");
		_fwd_signal(job, SIGINT);
	}

	/* kill launch thread */
	pthread_kill(job->lid, SIGTERM);

	/* kill msg server thread */
	pthread_kill(job->jtid, SIGTERM);

	/* wait for stdio */
	if (pthread_join(job->ioid, NULL) < 0) {
		error ("Waiting on IO: %m");
	}

	/* kill signal thread */
	pthread_cancel(job->sigid);

	if (old_job) {
		debug("cancelling job step %u.%u", job->jobid, job->stepid);
		slurm_complete_job_step(job->jobid, job->stepid, 0, 0);
	} else if (!opt.no_alloc) {
		debug("cancelling job %u", job->jobid);
		slurm_complete_job(job->jobid, 0, 0);
	}

	log_fini();

	exit(0);
}


/*
 * allocate nodes from slurm controller via slurm api
 * will xmalloc memory for allocation response, which caller must free
 *	initialization if set
 */
static allocation_resp *
_allocate_nodes(void)
{
	int rc, retries;
	job_desc_msg_t job;
	resource_allocation_response_msg_t *resp;
	old_job_alloc_msg_t old_job;

	slurm_init_job_desc_msg(&job);

	job.contiguous     = opt.contiguous;
	job.features       = opt.constraints;
	job.immediate      = opt.immediate;
	job.name           = opt.job_name;
	job.req_nodes      = opt.nodelist;
	job.partition      = opt.partition;
	job.num_nodes      = opt.nodes;
	job.num_tasks      = opt.nprocs;
	job.user_id        = opt.uid;
	if (opt.mincpus > -1)
		job.min_procs = opt.mincpus;
	if (opt.realmem > -1)
		job.min_memory = opt.realmem;
	if (opt.tmpdisk > -1)
		job.min_tmp_disk = opt.tmpdisk;

	if (opt.overcommit)
		job.num_procs      = opt.nodes;
	else
		job.num_procs      = opt.nprocs * opt.cpus_per_task;

	if (opt.no_kill)
 		job.kill_on_node_fail	= 0;
	if (opt.time_limit > -1)
		job.time_limit		= opt.time_limit;
	if (opt.share)
		job.shared		= 1;

	retries = 0;
	while ((rc = slurm_allocate_resources(&job, &resp)) < 0) {
		if ((slurm_get_errno() == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) 
		    && (retries < MAX_RETRIES)) {
			char *msg = "Slurm controller not responding, " 
				    "sleeping and retrying";
			if (retries == 0)
				error (msg);
			else
				debug (msg);

			sleep (++retries);
		} else {
			error("Unable to allocate resources: %m");
			return NULL;
		}			
	}

	if ((rc == 0) && (resp->node_list == NULL)) {
		struct sigaction action, old_action;
		int fake_job_id = (0 - resp->job_id);
		info ("Job %u queued and waiting for resources", resp->job_id);
		_sig_kill_alloc(fake_job_id);
		action.sa_handler = &_sig_kill_alloc;
		/* action.sa_flags   = SA_ONESHOT; */
		sigaction(SIGINT, &action, &old_action);
		old_job.job_id = resp->job_id;
		old_job.uid = (uint32_t) getuid();
		slurm_free_resource_allocation_response_msg (resp);
		sleep (2);

		/* Keep polling until the job is allocated resources */
		while (slurm_confirm_allocation(&old_job, &resp) < 0) {
			if (slurm_get_errno() == ESLURM_JOB_PENDING) {
				debug3("Still waiting for allocation");
				sleep (10);
			} else {
				error("Unable to confirm resource "
				      "allocation for job %u: %m", 
				      old_job.job_id);
				exit (1);
			}
		}
		sigaction(SIGINT, &old_action, NULL);
	}

	return resp;
}

static void
_sig_kill_alloc(int signum)
{
	static uint32_t job_id = 0;

	if (signum == SIGINT) {			/* <Control-C> */
		slurm_complete_job (job_id, 0, 0);
#ifdef HAVE_TOTALVIEW
		tv_launch_failure();
#endif
		exit (0);
	} else if (signum < 0)
		job_id = (uint32_t) (0 - signum); /* kluge to pass job id */
	else
		fatal ("_sig_kill_alloc called with invalid argument", signum);

}



#ifdef HAVE_LIBELAN3
static void
_qsw_standalone(job_t *job)
{
	int i;
	bitstr_t bit_decl(nodeset, QSW_MAX_TASKS);

	for (i = 0; i < job->nhosts; i++) {
		int nodeid;
		if ((nodeid = qsw_getnodeid_byhost(job->host[i])) < 0)
			fatal("qsw_getnodeid_byhost: %m");
		bit_set(nodeset, nodeid);
	}

	if (qsw_alloc_jobinfo(&job->qsw_job) < 0)
		fatal("qsw_alloc_jobinfo: %m");
	if (qsw_setup_jobinfo(job->qsw_job, opt.nprocs, nodeset, 0) < 0)
		fatal("qsw_setup_jobinfo: %m");

}
#endif /* HAVE_LIBELAN3 */

static void 
_create_job_step(job_t *job)
{
	job_step_create_request_msg_t req;
	job_step_create_response_msg_t *resp;

	req.job_id     = job->jobid;
	req.user_id    = opt.uid;
	req.node_count = job->nhosts;
	if (opt.overcommit)
		req.cpu_count  = job->nhosts;
	else
		req.cpu_count  = opt.nprocs * opt.cpus_per_task;
	req.num_tasks  = opt.nprocs;
	req.node_list  = job->nodelist;
	req.relative   = false;

	if (opt.distribution == SRUN_DIST_UNKNOWN) {
		if (opt.nprocs <= job->nhosts)
			opt.distribution = SRUN_DIST_CYCLIC;
		else
			opt.distribution = SRUN_DIST_BLOCK;
	}
	if (opt.distribution == SRUN_DIST_BLOCK)
		req.task_dist  = SLURM_DIST_BLOCK;
	else 	/* (opt.distribution == SRUN_DIST_CYCLIC) */
		req.task_dist  = SLURM_DIST_CYCLIC;

	if (slurm_job_step_create(&req, &resp) || (resp == NULL)) {
		error("unable to create job step: %s", slurm_strerror(errno));
		slurm_complete_job(job->jobid, 0, errno);
#ifdef HAVE_TOTALVIEW
		tv_launch_failure();
#endif
		exit(1);
	}

	job->stepid = resp->job_step_id;
	job->cred   = resp->credentials;
#ifdef HAVE_LIBELAN3	
	job->qsw_job= resp->qsw_job;
#endif

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


static void
_sigterm_handler(int signum)
{
	if (signum == SIGTERM) {
		pthread_exit(0);
	}
}

static void
_handle_intr(job_t *job, time_t *last_intr, time_t *last_intr_sent)
{

	if ((time(NULL) - *last_intr) > 1) {
		info("interrupt (one more within 1 sec to abort)");
		report_task_status(job);
		*last_intr = time(NULL);
	} else  { /* second Ctrl-C in half as many seconds */

		/* terminate job */
		if (job->state != SRUN_JOB_OVERDONE) {

			info("sending Ctrl-C to job");
			*last_intr = time(NULL);
			_fwd_signal(job, SIGINT);

			if ((time(NULL) - *last_intr_sent) < 1)
				job_force_termination(job);
			else
				*last_intr_sent = time(NULL);
		} else {
			job_force_termination(job);
		}
	}
}

static void
_sig_thr_setup(sigset_t *set)
{
	int rc;

	sigemptyset(set);
	sigaddset(set, SIGINT);
	sigaddset(set, SIGQUIT);
	sigaddset(set, SIGTSTP);
	sigaddset(set, SIGSTOP);
	if ((rc = pthread_sigmask(SIG_BLOCK, set, NULL)) != 0)
		error ("pthread_sigmask: %s", slurm_strerror(rc));
}


/* simple signal handling thread */
static void *
_sig_thr(void *arg)
{
	job_t *job = (job_t *)arg;
	sigset_t set;
	time_t last_intr      = 0;
	time_t last_intr_sent = 0;
	int signo;

	while (1) {

		_sig_thr_setup(&set);

		sigwait(&set, &signo);
		debug2("recvd signal %d", signo);
		switch (signo) {
		  case SIGINT:
			  _handle_intr(job, &last_intr, &last_intr_sent);
			  break;
		  case SIGSTOP:
		  case SIGTSTP:
			debug3("Ignoring SIGSTOP");
			break;
		  case SIGQUIT:
			info("Quit");
			job_force_termination(job);
			break;
		  default:
			break;
		}
	}

	pthread_exit(0);
}

static void 
_fwd_signal(job_t *job, int signo)
{
	int i;
	slurm_msg_t *req_array_ptr;
	kill_tasks_msg_t msg;

	debug("forward signal %d to job", signo);

	/* common to all tasks */
	msg.job_id      = job->jobid;
	msg.job_step_id = job->stepid;
	msg.signal      = (uint32_t) signo;

	req_array_ptr = (slurm_msg_t *) 
			xmalloc(sizeof(slurm_msg_t) * job->nhosts);
	for (i = 0; i < job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			debug2("%s has not yet replied\n", job->host[i]);
			continue;
		}

		req_array_ptr[i].msg_type = REQUEST_KILL_TASKS;
		req_array_ptr[i].data = &msg;
		memcpy(&req_array_ptr[i].address, 
		       &job->slurmd_addr[i], sizeof(slurm_addr));
	}

	_p_fwd_signal(req_array_ptr, job);

	debug("All tasks have been signalled");
	xfree(req_array_ptr);
}

/* _p_fwd_signal - parallel (multi-threaded) task signaller */
static void _p_fwd_signal(slurm_msg_t *req_array_ptr, job_t *job)
{
	int i;
	task_info_t *task_info_ptr;
	thd_t *thread_ptr;

	thread_ptr = xmalloc (job->nhosts * sizeof (thd_t));
	for (i = 0; i < job->nhosts; i++) {
		if (req_array_ptr[i].msg_type == 0)
			continue;	/* inactive task */

		pthread_mutex_lock(&active_mutex);
		while (active >= opt.max_threads) {
			pthread_cond_wait(&active_cond, &active_mutex);
		}
		active++;
		pthread_mutex_unlock(&active_mutex);

		task_info_ptr = (task_info_t *)xmalloc(sizeof(task_info_t));
		task_info_ptr->req_ptr  = &req_array_ptr[i];
		task_info_ptr->job_ptr  = job;
		task_info_ptr->host_inx = i;

		if (pthread_attr_init (&thread_ptr[i].attr))
			fatal ("pthread_attr_init error %m");
		if (pthread_attr_setdetachstate (&thread_ptr[i].attr, 
		                                 PTHREAD_CREATE_DETACHED))
			error ("pthread_attr_setdetachstate error %m");
#ifdef PTHREAD_SCOPE_SYSTEM
		if (pthread_attr_setscope (&thread_ptr[i].attr, 
		                           PTHREAD_SCOPE_SYSTEM))
			error ("pthread_attr_setscope error %m");
#endif
		while ( pthread_create (&thread_ptr[i].thread, 
		                        &thread_ptr[i].attr, 
		                        _p_signal_task, 
		                        (void *) task_info_ptr) ) {
			error ("pthread_create error %m");
			/* just run it under this thread */
			_p_signal_task((void *) task_info_ptr);
		}
	}


	pthread_mutex_lock(&active_mutex);
	while (active > 0) {
		pthread_cond_wait(&active_cond, &active_mutex);
	}
	pthread_mutex_unlock(&active_mutex);
	xfree(thread_ptr);
}

/* _p_signal_task - parallelized signal of a specific task */
static void * _p_signal_task(void *args)
{
	task_info_t *task_info_ptr = (task_info_t *)args;
	slurm_msg_t *req_ptr = task_info_ptr->req_ptr;
	job_t *job_ptr = task_info_ptr->job_ptr;
	int host_inx = task_info_ptr->host_inx;
	slurm_msg_t resp;

	debug3("sending signal to host %s", job_ptr->host[host_inx]);
	if (slurm_send_recv_node_msg(req_ptr, &resp) < 0)  /* Has timeout */
		error("signal %s: %m", job_ptr->host[host_inx]);
	else if (resp.msg_type == RESPONSE_SLURM_RC)
		slurm_free_return_code_msg(resp.data);

	pthread_mutex_lock(&active_mutex);
	active--;
	pthread_cond_signal(&active_cond);
	pthread_mutex_unlock(&active_mutex);
	xfree(args);
	return NULL;
}

/* submit a batch job and return error code */
static int
_run_batch_job(void)
{
	int file_type, rc, retries;
	job_desc_msg_t job;
	submit_response_msg_t *resp;
	extern char **environ;
	char *job_script;

	if ((remote_argc == 0) || (remote_argv[0] == NULL))
		return 1;
	file_type = _is_file_text (remote_argv[0], NULL);
	if (file_type == TYPE_NOT_TEXT) {
		error ("file %s is not script", remote_argv[0]);
		return 1;
	}
	job_script = _build_script (remote_argv[0], file_type);
	if (job_script == NULL) {
		error ("unable to build script from file %s", remote_argv[0]);
		return 1;
	}

	slurm_init_job_desc_msg(&job);

	job.contiguous     = opt.contiguous;
	job.features       = opt.constraints;

	job.name           = opt.job_name;

	job.partition      = opt.partition;

	if (opt.mincpus > -1)
		job.min_procs = opt.mincpus;
	if (opt.realmem > -1)
		job.min_memory = opt.realmem;
	if (opt.tmpdisk > -1)
		job.min_tmp_disk = opt.tmpdisk;

	job.req_nodes      = opt.nodelist;

	if (opt.overcommit)
		job.num_procs      = opt.nodes;
	else
		job.num_procs      = opt.nprocs * opt.cpus_per_task;

	job.num_nodes      = opt.nodes;

	job.num_tasks      = opt.nprocs;

	job.user_id        = opt.uid;

	if (opt.no_kill)
 		job.kill_on_node_fail	= 0;
	if (opt.time_limit > -1)
		job.time_limit		= opt.time_limit;
	if (opt.share)
		job.shared		= 1;

	_set_batch_script_env(0);
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
	while ((rc = slurm_submit_batch_job(&job, &resp)) == SLURM_FAILURE) {
		if ((slurm_get_errno() == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) 
		    && (retries < MAX_RETRIES)) {
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
			return 1;
		}			
	}

	
	if (rc == 0) {
		info("jobid %u submitted",resp->job_id);
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
	char buffer[256];

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
		if (((buffer[i] >= 0x00) && (buffer[i] <= 0x06)) ||
		    ((buffer[i] >= 0x0e) && (buffer[i] <= 0x1f)) ||
		     (buffer[i] >= 0x7f)) {
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
	char *buffer, *shell;
	int buf_size, buf_used = 0, fd, data_size, i;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		error ("Unable to open file %s: %m", fname);
		return NULL;
	}

	buf_size = 8096;
	buffer = xmalloc (buf_size);
	buf_used = 0;
	if (file_type != TYPE_SCRIPT) {
		shell = _get_shell();
		strcpy (buffer, "#!");
		strcat (buffer, shell);
		strcat (buffer, "\n");
		buf_used = strlen(buffer);
	}

	while (1) {
		i = buf_size - buf_used;
		if (i < 1024) {
			buf_size += 8096;
			xrealloc (buffer, buf_size);
			i = buf_size - buf_used;
		}
		data_size = read (fd, &buffer[buf_used], i);
		if (data_size <= 0)
			break;
		buf_used += i;
	}
	buffer[buf_used] = '\0';
	(void) close (fd);

	return buffer;
}

/* If this is a valid job then return a (psuedo) allocation response pointer, 
 * otherwise return NULL */
static allocation_resp *
_existing_allocation( void )
{
	char * jobid_str, *end_ptr;
	uint32_t jobid_uint;
	old_job_alloc_msg_t job;
	allocation_resp *resp;

	/* Load SLURM_JOBID environment variable */
	jobid_str = getenv( "SLURM_JOBID" );
	if (jobid_str == NULL)
		return NULL;
	jobid_uint = (uint32_t) strtoul( jobid_str, &end_ptr, 10 );
	if (end_ptr[0] != '\0') {
		error( "Invalid SLURM_JOBID environment variable: %s", 
		       jobid_str );
		exit( 1 );
	}

	/* Confirm that this job_id is legitimate */
	job.job_id = jobid_uint;
	job.uid = (uint32_t) getuid();

	if (slurm_confirm_allocation(&job, &resp) == SLURM_FAILURE) {
		error("Unable to confirm resource allocation for job %u: %s", 
			jobid_uint, slurm_strerror(errno));
		exit( 1 );
	}

	return resp;
}

static int
_set_batch_script_env(uint32_t jobid)
{
	char *dist = NULL;

	if (jobid > 0) {
		if (setenvf("SLURM_JOBID=%u", jobid)) {
			error("Unable to set SLURM_JOBID env var");
			return -1;
		}
	}

	if (setenvf("SLURM_NNODES=%u", opt.nodes)) {
		error("Unable to set SLURM_NNODES environment variable");
		return -1;
	}

	if (setenvf("SLURM_NPROCS=%u", opt.nprocs)) {
		error("Unable to set SLURM_NPROCS environment variable");
		return -1;
	}

	if (opt.distribution != SRUN_DIST_UNKNOWN) {
		dist = (opt.distribution == SRUN_DIST_BLOCK) ? 
							"block" : "cyclic";

		if (setenvf("SLURM_DISTRIBUTION=%s", dist)) {
			error("Can't set SLURM_DISTRIBUTION environment variable");
			return -1;
		}
	}

	if ((opt.overcommit) &&
	    (setenvf("SLURM_OVERCOMMIT=1"))) {
		error("Unable to set SLURM_OVERCOMMIT environment variable");
		return -1;
	}

	return 0;
}

/* allocation option specified, spawn a script and wait for it to exit */
static void _run_job_script (uint32_t jobid)
{
	char *shell = NULL;
	int   i;
	pid_t child;

	if (_set_batch_script_env(jobid) < 0) 
		return;

	/* determine shell from script (if any) or user default */
	if (remote_argc) {
		char ** new_argv;
		(void) _is_file_text (remote_argv[0], &shell);
		if (shell == NULL)
			shell = _get_shell ();	/* user's default shell */
		new_argv = (char **) xmalloc ((remote_argc + 2) * 
						sizeof(char *));
		new_argv[0] = xstrdup (shell);
		for (i=0; i<remote_argc; i++)
			new_argv[i+1] = remote_argv[i];
		xfree (remote_argv);
		remote_argc++;
		remote_argv = new_argv;
	} else {
		shell = _get_shell ();	/* user's default shell */
		remote_argc = 1;
		remote_argv = (char **) xmalloc((remote_argc + 1) * 
						sizeof(char *));
		remote_argv[0] = strdup (shell);
		remote_argv[1] = NULL;	/* End of argv's (for execv) */
	}

	/* spawn the shell with arguments (if any) */
	if (_verbose || _debug)
		info ("Spawning srun shell %s", shell);

	switch ( (child = fork()) ) {
		case -1:
			fatal("Fork error %m");
		case 0:
			execv(shell, remote_argv);
			fatal("exec error %m");
			exit(1);
		default:
			while ( (i = wait(NULL)) ) {
				if (i == -1)
					fatal("wait error %m");
				if (i == child)
					break;
			}
	}

	if (unsetenv("SLURM_JOBID")) {
		error("Unable to clear SLURM_JOBID environment variable");
		return;
	}
}
