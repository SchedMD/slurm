/****************************************************************************\
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
#  include <config.h>
#endif

#ifdef HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <src/api/slurm.h>

#include <src/common/log.h>
#include <src/common/xstring.h>
#include <src/common/xmalloc.h>
#include <src/common/slurm_protocol_api.h>

#include <src/srun/opt.h>
#include <src/srun/env.h>
#include <src/srun/job.h>
#include <src/srun/launch.h>

#include <src/srun/net.h>
#include <src/srun/msg.h>
#include <src/srun/io.h>

#define MAX_RETRIES 20

typedef resource_allocation_response_msg_t allocation_resp;

#define	TYPE_NOT_TEXT	0
#define	TYPE_TEXT	1
#define	TYPE_SCRIPT	2

/*
 * forward declaration of static funcs
 */
static allocation_resp	*allocate_nodes(void);
static void              print_job_information(allocation_resp *resp);
static void		 create_job_step(job_t *job);
static void		 sigterm_handler(int signum);
static void		 sig_kill_alloc(int signum);
void *                   sig_thr(void *arg);
void 			 fwd_signal(job_t *job, int signo);
static char 		*build_script (char *pathname, int file_type);
static char 		*get_shell (void);
static int 		 is_file_text (char *fname, char** shell_ptr);
static int		 run_batch_job (void);
static allocation_resp	*existing_allocation(void);
static void		 run_job_script(uint32_t job_id);

#if HAVE_LIBELAN3
#  include <src/common/qsw.h> 
static void qsw_standalone(job_t *job);
#endif
int
main(int ac, char **av)
{
	sigset_t sigset;
	allocation_resp *resp;
	job_t *job;
	pthread_attr_t attr, ioattr;
	struct sigaction action;
	int n, i;
	bool old_job = false;

	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	log_init(xbasename(av[0]), logopt, 0, NULL);

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);
	if (read_slurm_port_config() < 0)
		error("read_slurm_port_config: %s", slurm_strerror(errno));

	/* reinit log with new verbosity
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
		if (run_batch_job())
			exit (1);
		exit (0);
	} else if (opt.no_alloc) {
		printf("do not allocate resources\n");
		job = job_create(NULL); 
#if HAVE_LIBELAN3
		qsw_standalone(job);
#endif
	} else if ( (resp = existing_allocation()) ) {
		old_job = true;
		job = job_create(resp); 
		create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);
	} else if (opt.allocate) {
		if ( !(resp = allocate_nodes()) ) 
			exit(1);
		if (_verbose || _debug)
			print_job_information(resp);
		else
			printf("jobid %u\n", resp->job_id); 
		run_job_script(resp->job_id);
		slurm_complete_job(resp->job_id);
		if (_verbose || _debug)
			info ("Spawned srun shell terminated");
		exit (0);
	} else {
		if ( !(resp = allocate_nodes()) ) 
			exit(1);
		if (_verbose || _debug)
			print_job_information(resp);
		else
			printf("jobid %u\n", resp->job_id); 

		job = job_create(resp); 
		create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);
	}

	/* block most signals in all threads, except sigterm */
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGTSTP);
	sigaddset(&sigset, SIGSTOP);
	if (sigprocmask(SIG_BLOCK, &sigset, NULL) != 0)
		fatal("sigprocmask: %m");
	action.sa_handler = &sigterm_handler;
	action.sa_flags   = 0;
	sigaction(SIGTERM, &action, NULL);

	/* job structure should now be filled in */

	for (i = 0; i < job->njfds; i++) {
		if ((job->jfd[i] = slurm_init_msg_engine_port(0)) < 0)
			fatal("init_msg_engine_port: %m");
		if (slurm_get_stream_addr(job->jfd[i], &job->jaddr[i]) < 0)
			fatal("slurm_get_stream_addr: %m");
		debug("initialized job control port %d\n", 
			ntohs(((struct sockaddr_in)job->jaddr[i]).sin_port));
	}


	for (i = 0; i < job->niofds; i++) {
		if (net_stream_listen(&job->iofd[i], &job->ioport[i]) < 0)
			fatal("unable to initialize stdio server port: %m");
		debug("initialized stdio server port %d\n", 
				ntohs(job->ioport[i]));
	}

	pthread_attr_init(&attr);

	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	/* spawn msg server thread */
	if ((errno = pthread_create(&job->jtid, &attr, &msg_thr, (void *) job)))
		fatal("Unable to create message thread. %m\n");
	debug("Started msg server thread (%d)\n", job->jtid);

	pthread_attr_init(&ioattr);
	/* spawn io server thread */
	if ((errno = pthread_create(&job->ioid, &ioattr, &io_thr, (void *) job)))
		fatal("Unable to create io thread. %m\n");
	debug("Started IO server thread (%d)\n", job->ioid);

	/* spawn signal thread */
	if ((errno = pthread_create(&job->sigid, &attr, &sig_thr, (void *) job)))
		fatal("Unable to create signals thread. %m");
	debug("Started signals thread (%d)", job->sigid);


	/* launch jobs */
	if ((errno = pthread_create(&job->lid, &attr, &launch, (void *) job)))
		fatal("Unable to create launch thread. %m");
	debug("Started launch thread (%d)", job->lid);

	/* wait for job to terminate */
	xassert(pthread_mutex_lock(&job->state_mutex) == 0);
	debug3("before main state loop: state = %d", job->state);
	while (job->state != SRUN_JOB_OVERDONE) {
		pthread_cond_wait(&job->state_cond, &job->state_mutex);
		debug3("main thread woke up, state is now %d", job->state);
	}
	pthread_mutex_unlock(&job->state_mutex);

	/* job is now overdone, clean up  */

	/* kill launch thread */
	pthread_kill(job->lid, SIGTERM);

	/* kill msg server thread */
	pthread_kill(job->jtid, SIGTERM);

	if (old_job) {
		debug("cancelling job step %u.%u", job->jobid, job->stepid);
		slurm_complete_job_step(job->jobid, job->stepid);
	} else if (!opt.no_alloc) {
		debug("cancelling job %u", job->jobid);
		slurm_complete_job(job->jobid);
	}

	/* kill signal thread */
	pthread_kill(job->sigid, SIGTERM);

	/* wait for  stdio */
	n = 0;
	for (i = 0; i < opt.nprocs; i++) {
		if (job->out[i] == WAITING_FOR_IO)
			job->out[i] = IO_DONE;
		if (job->err[i] == WAITING_FOR_IO)
			job->err[i] = IO_DONE;
		if ((job->err[i] == IO_DONE) && 
		    (job->out[i] == IO_DONE))
			n++;
	}
	verbose("end of job n = %d", n);
	if (n < opt.nprocs)
		pthread_join(job->ioid, NULL);
	else
		pthread_kill(job->ioid, SIGTERM);


	exit(0);
}


/* allocate nodes from slurm controller via slurm api
 * will xmalloc memory for allocation response, which caller must free
 */
static allocation_resp *
allocate_nodes(void)
{
	int rc, retries;
	job_desc_msg_t job;
	resource_allocation_response_msg_t *resp;
	old_job_alloc_msg_t old_job;

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

	job.num_procs      = opt.nprocs * opt.cpus_per_task;

	if (opt.nodes > -1)
		job.num_nodes = opt.nodes;

	job.user_id        = opt.uid;

	if (opt.fail_kill)
 		job.kill_on_node_fail	= 0;
	if (opt.time_limit > -1)
		job.time_limit		= opt.time_limit;
	if (opt.share)
		job.shared		= 1;

	retries = 0;
	while ((rc = slurm_allocate_resources(&job, &resp, opt.immediate))
					== SLURM_FAILURE) {
		if ((slurm_get_errno() == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) &&
		    (retries < MAX_RETRIES)) {
			if (retries == 0)
				error ("Slurm controller not responding, sleeping and retrying");
			else
				debug ("Slurm controller not responding, sleeping and retrying");

			sleep (++retries);
		}
		else {
			error("Unable to allocate resources: %s", slurm_strerror(errno));
			return NULL;
		}			
	}

	if ((rc == 0) && (resp->node_list == NULL)) {
		struct sigaction action, old_action;
		int fake_job_id = (0 - resp->job_id);
		info ("Job %u queued and waiting for resources", resp->job_id);
		sig_kill_alloc(fake_job_id);
		action.sa_handler = &sig_kill_alloc;
		action.sa_flags   = SA_ONESHOT;
		sigaction(SIGINT, &action, &old_action);
		old_job.job_id = resp->job_id;
		old_job.uid = (uint32_t) getuid();
		slurm_free_resource_allocation_response_msg (resp);
		sleep (2);
		/* Keep polling until the job is allocated resources */
		while (slurm_confirm_allocation(&old_job, &resp) == SLURM_FAILURE) {
			if (slurm_get_errno() == ESLURM_JOB_PENDING)
				sleep (10);
			else {
				error("Unable to confirm resource allocation for job %u: %s", 
					old_job.job_id, slurm_strerror(errno));
				exit (1);
			}
		}
		sigaction(SIGINT, &old_action, NULL);
	}

	return resp;

}

static void
sig_kill_alloc(int signum)
{
	static uint32_t job_id = 0;

	if (signum == SIGINT) {			/* <Control-C> */
		slurm_complete_job (job_id);
		exit (0);
	} else if (signum < 0)
		job_id = (uint32_t) (0 - signum);	/* kluge to pass the job id */
	else
		fatal ("sig_kill_alloc called with invalid argument", signum);

}



#if HAVE_LIBELAN3
static void
qsw_standalone(job_t *job)
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
create_job_step(job_t *job)
{
	job_step_create_request_msg_t req;
	job_step_create_response_msg_t *resp;
	slurm_msg_t req_msg;
	slurm_msg_t resp_msg;

	req.job_id     = job->jobid;
	req.user_id    = opt.uid;
	req.node_count = job->nhosts;
	req.cpu_count  = opt.nprocs * opt.cpus_per_task;
	req.node_list  = job->nodelist;
	req.relative   = false;
	if (opt.distribution == SRUN_DIST_BLOCK)
		req.task_dist  = SLURM_DIST_BLOCK;
	else 	/* (opt.distribution == SRUN_DIST_CYCLIC) */
		req.task_dist  = SLURM_DIST_CYCLIC;

	req_msg.msg_type = REQUEST_JOB_STEP_CREATE;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0) {
		error("unable to create job step: %s", slurm_strerror(errno));
		exit(1);
	}

	if (resp_msg.msg_type == RESPONSE_SLURM_RC) {
		return_code_msg_t *rcmsg = (return_code_msg_t *) resp_msg.data;
		error("unable to create job step: %s", 
				slurm_strerror(rcmsg->return_code));
		slurm_complete_job(job->jobid);
		exit(1);
	}

	resp = (job_step_create_response_msg_t *) resp_msg.data;

	job->stepid = resp->job_step_id;
	job->cred   = resp->credentials;
#if HAVE_LIBELAN3	
	job->qsw_job= resp->qsw_job;
#endif

}


static void 
print_job_information(allocation_resp *resp)
{
	int i;
	printf("jobid %d: `%s', cpu counts: ", resp->job_id, resp->node_list);

	for (i = 0; i < resp->num_cpu_groups; i++) {
		printf("%u(x%u), ", resp->cpus_per_node[i], resp->cpu_count_reps[i]);
	}
	printf("\n");
}

static void
sigterm_handler(int signum)
{
	if (signum == SIGTERM) {
		pthread_exit(0);
	}
}


/* simple signal handling thread */
void *
sig_thr(void *arg)
{
	job_t *job = (job_t *)arg;
	sigset_t set;
	time_t last_intr = 0;
	bool suddendeath = false;
	int signo;

	while (1) {
		sigfillset(&set);
		sigdelset(&set, SIGTERM);
		sigdelset(&set, SIGABRT);
		sigdelset(&set, SIGSEGV);
		sigdelset(&set, SIGQUIT);
		pthread_sigmask(SIG_BLOCK, &set, NULL);
		sigwait(&set, &signo);
		debug2("recvd signal %d", signo);
		switch (signo) {
		  case SIGTERM:
			  pthread_exit(0);
			  break;
		  case SIGINT:
			  if (time(NULL) - last_intr > 1) {
				  if (job->state != SRUN_JOB_OVERDONE) {
					  info("sending Ctrl-C to job");
					  last_intr = time(NULL);
					  fwd_signal(job, signo);
				  } else
					  info("attempting cleanup");

			  } else  { /* second Ctrl-C in half as many seconds */
				    /* terminate job */
				  pthread_mutex_lock(&job->state_mutex);
				  if (job->state != SRUN_JOB_OVERDONE) {
					  info("forcing termination");
					  job->state = SRUN_JOB_OVERDONE;
				  } else 
					  info("attempting cleanup");
				  pthread_cond_signal(&job->state_cond);
				  pthread_mutex_unlock(&job->state_mutex);
				  suddendeath = true;
			  }
			  break;
		  default:
			  fwd_signal(job, signo);
			  break;
		}
	}

	pthread_exit(0);
}

void 
fwd_signal(job_t *job, int signo)
{
	int i;
	slurm_msg_t req;
	slurm_msg_t resp;
	kill_tasks_msg_t msg;

	debug("forward signal %d to job", signo);

	req.msg_type = REQUEST_KILL_TASKS;
	req.data = &msg;

	msg.job_id      = job->jobid;
	msg.job_step_id = job->stepid;
	msg.signal      = (uint32_t) signo;

	for (i = 0; i < job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			debug2("%s has not yet replied\n", job->host[i]);
			continue;
		}

		memcpy(&req.address, &job->slurmd_addr[i], sizeof(slurm_addr));
		debug("sending kill req to %s", job->host[i]);
		if (slurm_send_recv_node_msg(&req, &resp) < 0)
			error("Unable to send signal to host %s", 
					job->host[i]);
	}

}


/* submit a batch job and return error code */
static int
run_batch_job(void)
{
	int i, file_type, rc, retries;
	job_desc_msg_t job;
	submit_response_msg_t *resp;
	extern char **environ;
	char *job_script;

	if ((remote_argc == 0) || (remote_argv[0] == NULL))
		return 1;
	file_type = is_file_text (remote_argv[0], NULL);
	if (file_type == TYPE_NOT_TEXT) {
		error ("file %s is not script", remote_argv[0]);
		return 1;
	}
	job_script = build_script (remote_argv[0], file_type);
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

	job.num_procs      = opt.nprocs * opt.cpus_per_task;

	if (opt.nodes > -1)
		job.num_nodes = opt.nodes;

	job.user_id        = opt.uid;

	if (opt.fail_kill)
 		job.kill_on_node_fail	= 0;
	if (opt.time_limit > -1)
		job.time_limit		= opt.time_limit;
	if (opt.share)
		job.shared		= 1;

	job.environment		= environ;
	for (i=0; ; i++) {
		if (environ[i] == NULL) {
			job.env_size	= (i - 1);
			break;
		}
	}
	job.script		= job_script;
	if (opt.efname)
		job.stderr	= opt.efname;
	if (opt.ifname)
		job.stdin	= opt.ifname;
	if (opt.ofname)
		job.stdout	= opt.ofname;
	job.work_dir		= opt.cwd;

	retries = 0;
	while ((rc = slurm_submit_batch_job(&job, &resp)) == SLURM_FAILURE) {
		if ((slurm_get_errno() == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) &&
		    (retries < MAX_RETRIES)) {
			if (retries == 0)
				error ("Slurm controller not responding, sleeping and retrying");
			else
				debug ("Slurm controller not responding, sleeping and retrying");

			sleep (++retries);
		}
		else {
			error("Unable to submit batch job resources: %s", 
					slurm_strerror(errno));
			return 1;
		}			
	}

	
	if (rc == 0) {
		printf ("jobid %u\n",resp->job_id);
		slurm_free_submit_response_response_msg (resp);
	}
	xfree (job_script);
	return rc;
}

/* get_shell - return a string containing the default shell for this user
 * NOTE: This function is NOT reentrant (see getpwuid_r if needed) */
char *
get_shell (void)
{
	struct passwd *pw_ent_ptr;

	pw_ent_ptr = getpwuid (getuid ());
	return pw_ent_ptr->pw_shell;
}

/* is_file_text - determine if specified file is a script
 * shell_ptr - if not NULL, set to pointer to pathname of specified shell 
 *		(if any, ie. return code of 2)
 *	return 0 if the specified file can not be read or does not contain text
 *	returns 2 if file contains text starting with "#!", otherwise
 *	returns 1 if file contains text, but lacks "#!" header 
 */
int
is_file_text (char *fname, char **shell_ptr)
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
char *
build_script (char *fname, int file_type)
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
		shell = get_shell();
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
existing_allocation( void )
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
		error( "Invalid SLURM_JOBID environment variable: %s", jobid_str );
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

/* allocation option specified, spawn a script and wait for it to exit */
void run_job_script (uint32_t job_id)
{
	char *shell = NULL;
	int   i;
	pid_t child;

	if (setenvf("SLURM_JOBID=%u", job_id)) {
		error("Unable to set SLURM_JOBID environment variable");
		return;
	}

	if (setenvf("SLURM_NNODES=%u", opt.nodes)) {
		error("Unable to set SLURM_NNODES environment variable");
		return;
	}

	if (setenvf("SLURM_NPROCS=%u", opt.nprocs)) {
		error("Unable to set SLURM_NPROCS environment variable");
		return;
	}

	if (setenvf("SLURM_DISTRIBUTION=%s", 
		    opt.distribution == SRUN_DIST_BLOCK ?  "block" : "cyclic")) {
		error("Unable to set SLURM_DISTRIBUTION environment variable");
		return;
	}

	/* determine shell from script (if any) or user default */
	if (remote_argc) {
		char ** new_argv;
		(void) is_file_text (remote_argv[0], &shell);
		if (shell == NULL)
			shell = get_shell ();	/* user's default shell */
		new_argv = (char **) xmalloc ((remote_argc + 2) * sizeof(char *));
		new_argv[0] = strdup (shell);
		for (i=0; i<remote_argc; i++)
			new_argv[i+1] = remote_argv[i];
		xfree (remote_argv);
		remote_argc++;
		remote_argv = new_argv;
	} else {
		shell = get_shell ();	/* user's default shell */
		remote_argc = 1;
		remote_argv = (char **) xmalloc((remote_argc + 1) * sizeof(char *));
		remote_argv[0] = strdup (shell);
		remote_argv[1] = NULL;	/* End of argv's (for possible execv) */
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
