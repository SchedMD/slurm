/****************************************************************************\
 *  srun.c - user interface to allocate resources, submit jobs, and execute 
 *	parallel jobs.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>


#include <src/common/log.h>
#include <src/common/xstring.h>
#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

#include <src/srun/opt.h>
#include <src/srun/env.h>
#include <src/srun/job.h>
#include <src/srun/launch.h>

#include <src/srun/net.h>
#include <src/srun/msg.h>
#include <src/srun/io.h>

typedef resource_allocation_response_msg_t allocation_resp;

/*
 * forward declaration of static funcs
 */
static allocation_resp * allocate_nodes(void);
static void              print_job_information(allocation_resp *resp);
static void		 create_job_step(job_t *job);
static void		 sigterm_handler(int signum);
void *                   sig_thr(void *arg);
void 			 fwd_signal(job_t *job, int signo);

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
	struct sigaction action;
	int n, i;

	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	log_init(xbasename(av[0]), logopt, 0, NULL);

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);

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
	if (opt.no_alloc) {
		printf("do not allocate resources\n");
		job = job_create(NULL); 
#if HAVE_LIBELAN3
		qsw_standalone(job);
#endif
	} else {
		if (!(resp = allocate_nodes()) || (resp->node_list == NULL)) {
			info("No nodes allocated. exiting");
			exit(1);
		}
		if (_verbose || _debug)
			print_job_information(resp);
		else
			printf("jobid %d\n", resp->job_id); 

		job = job_create(resp); 
		create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);
	}

	/* block most signals in all threads, except sigterm */
	sigfillset(&sigset);
	sigdelset(&sigset, SIGTERM);
	sigdelset(&sigset, SIGABRT);
	sigdelset(&sigset, SIGSEGV);
	sigdelset(&sigset, SIGQUIT);
	if (sigprocmask(SIG_BLOCK, &sigset, NULL) != 0)
		fatal("sigprocmask: %m");
	action.sa_handler = &sigterm_handler;
	action.sa_flags   = 0;
	sigaction(SIGTERM, &action, NULL);

	/* job structure should now be filled in */

	if ((job->jfd = slurm_init_msg_engine_port(0)) == SLURM_SOCKET_ERROR)
		fatal("init_msg_engine_port: %s", slurm_strerror(errno));

	if (slurm_get_stream_addr(job->jfd, &job->jaddr) < 0)
		fatal("slurm_get_stream_addr: %m");

	debug("initialized job control port %d\n", 
			ntohs(((struct sockaddr_in)job->jaddr).sin_port));

	if (net_stream_listen(&job->iofd, &job->ioport) < 0)
		fatal("unable to initialize stdio server port: %m");

	debug("initialized stdio server port %d\n", ntohs(job->ioport));

						
	/* spawn io server thread */

	if (pthread_create(&job->ioid, NULL, &io_thr, (void *) job))
		fatal("Unable to create io thread. %m\n");
	debug("Started IO server thread (%d)\n", job->ioid);

	/* spawn msg server thread */

	if (pthread_create(&job->jtid, NULL, &msg_thr, (void *) job))
		fatal("Unable to create message thread. %m\n");
	debug("Started msg server thread (%d)\n", job->jtid);

	/* spawn signal thread */
	if (pthread_create(&job->sigid, NULL, &sig_thr, (void *) job))
		fatal("Unable to create signals thread. %m");
	debug("Started signals thread (%d)", job->sigid);

	/* launch jobs */
	if (pthread_create(&job->lid, NULL, &launch, (void *) job))
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

	if (!opt.no_alloc) {
		debug("cancelling job %d", job->jobid);
		slurm_complete_job(job->jobid);
	}

	/* kill signal thread */
	pthread_kill(job->sigid, SIGTERM);

	/* wait for  stdio */
	n = 0;
	for (i = 0; i < opt.nprocs; i++) {
		if (job->out[i] == -1)
			job->out[i] = -9;
		if (job->err[i] == -1)
			job->err[i] = -9;
		if (job->err[i] == -9 && job->out[i] == -9)
			n++;
	}
	if (n < opt.nprocs)
		pthread_join(job->ioid, NULL);
	else
		pthread_kill(job->ioid, SIGTERM);


	exit(0);
}


/* allocate nodes from slurm controller via slurm api
 * will malloc memory for allocation response, which caller must free
 */
static allocation_resp *
allocate_nodes(void)
{
	int rc;
	job_desc_msg_t job;
	resource_allocation_response_msg_t *resp;

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

	job.num_procs      = opt.nprocs;

	if (opt.nodes > -1)
		job.num_nodes = opt.nodes;

	job.user_id        = opt.uid;

	rc = slurm_allocate_resources(&job, &resp, opt.immediate);

	if (rc == SLURM_FAILURE) {
		error("Unable to allocate resources: %s", 
				slurm_strerror(errno));
		return NULL;
	}

	return resp;

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
	req.cpu_count  = opt.nprocs;
	req.node_list  = job->nodelist;
	req.relative   = false;

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

		slurm_set_addr_uint(&req.address, slurm_get_slurmd_port(),
				ntohl(job->iaddr[i]));
		debug("sending kill req to %s", job->host[i]);
		if (slurm_send_recv_node_msg(&req, &resp) < 0)
			error("Unable to send signal to host %s", 
					job->host[i]);
	}

}
