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

int
main(int ac, char **av)
{
	allocation_resp *resp;
	job_t *job;

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

	/* launch jobs */

	launch(job);

	/* wait on and process signals */

	sleep(120);

	if (!opt.no_alloc)
		slurm_complete_job(job->jobid);

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

static void create_job_step(job_t *job)
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
