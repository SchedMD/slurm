/* $Id$ */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>


#include <src/common/log.c>
#include <src/api/slurm.h>
#include <src/common/slurm_protocol_api.h>

#include <src/srun/opt.h>
#include <src/srun/env.h>
#include <src/srun/job.h>
#include <src/srun/launch.h>

#include "net.h"
#include "msg.h"
#include "io.h"

typedef resource_allocation_response_msg_t allocation_resp;

/*
 * forward declaration of static funcs
 */
static allocation_resp * allocate_nodes(void);
static void              print_job_information(allocation_resp *resp);
static void		 create_job_step(job_t *job);
static void		 sigterm_handler(int signum);
void *                   sig_thr(void *arg);

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

	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	log_init(xbasename(av[0]), logopt, 0, NULL);

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);

	/* reinit log with new verbosity
	 */
	if (_verbose || _debug) {
		if (_verbose) logopt.stderr_level++;
		verbose("verbose mode on");
		logopt.stderr_level += _debug;
		debug("setting debug to level %d.", _debug);
		logopt.prefix_level = 1;
		log_init(xbasename(av[0]), logopt, 0, NULL);
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
		resp = allocate_nodes();
		if (_verbose || _debug)
			print_job_information(resp);
		else
			printf("jobid %d\n", resp->job_id); 
		
		if (!resp->node_list) {
		 	info("No nodes allocated. exiting");
			exit(1);
		}

		job = job_create(resp); 
		create_job_step(job);
		slurm_free_resource_allocation_response_msg(resp);
	}

	/* block all signals in all threads, except sigterm */
	sigfillset(&sigset);
	sigdelset(&sigset, SIGTERM);
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
	launch(job);

	/* wait for job to terminate */
	while (job->state != SRUN_JOB_OVERDONE) {
		pthread_cond_wait(&job->state_cond, &job->state_mutex);
		debug("main thread woke up, state is now %d", job->state);
		if (errno == EINTR)
			debug("got signal");
	}

	/* job is now overdone, blow this popsicle stand  */
	
	if (!opt.no_alloc)
		slurm_complete_job(job->jobid);

	pthread_kill(job->jtid, SIGTERM);
	pthread_kill(job->ioid, SIGTERM);
	pthread_kill(job->sigid, SIGTERM);

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
		exit(1);
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
	req.node_list  = job->nodelist;
	req.relative   = false;

	req_msg.msg_type = REQUEST_JOB_STEP_CREATE;
	req_msg.data     = &req;

	if (slurm_send_recv_controller_msg(&req_msg, &resp_msg) < 0)
		error("unable to create job step: %s", slurm_strerror(errno));

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
		debug2("thread %d canceled\n", pthread_self());
		pthread_exit(0);
	}
}


/* simple signal handling thread */
void *
sig_thr(void *arg)
{
	job_t *job = (job_t *)arg;
	sigset_t set;
	int signo;
	struct sigaction action;


	while (1) {
		sigfillset(&set);
		pthread_sigmask(SIG_UNBLOCK, &set, NULL);
		sigwait(&set, &signo);
		debug2("recvd signal %d", signo);
		switch (signo) {
		  case SIGINT:
			  pthread_mutex_lock(&job->state_mutex);
			  job->state = SRUN_JOB_OVERDONE;
			  pthread_cond_signal(&job->state_cond);
			  pthread_mutex_unlock(&job->state_mutex);
			  pthread_exit(0);
			  break;
		  default:
			  /* fwd_signal(job, signo); */
			  break;
		}
	}

	pthread_exit(0);
}

