/* $Id$ */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <src/common/log.c>
#include <src/api/slurm.h>

#include <opt.h>
#include <env.h>

/*
 * forward declaration of static funcs
 */
static void create_job_spec(void);

int
main(int ac, char **av)
{
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
	 * create a job_spec from opt
	 */
	if (!opt.no_alloc) {
		create_job_spec();
	}

	exit(0);
}


static void
create_job_spec(void)
{
	int rc;
	int i;
	job_desc_msg_t job;
	resource_allocation_response_msg_t *resp;

	slurm_init_job_desc_msg(&job);

	job.contiguous     = opt.contiguous;
	job.features       = opt.constraints;

	job.name           = opt.job_name;

	job.partition      = opt.partition;
	
	if (opt.mincpus > -1)
		job.min_procs      = opt.mincpus;
	if (opt.realmem > -1)
		job.min_memory     = opt.realmem;
	if (opt.tmpdisk > -1)
		job.min_tmp_disk   = opt.tmpdisk;

	job.req_nodes      = opt.nodelist;

	job.num_procs      = opt.nprocs;

	if (opt.nodes > -1)
		job.num_nodes      = opt.nodes;

	job.user_id        = opt.uid;

	rc = slurm_allocate_resources(&job, &resp, opt.immediate);

	if (rc) {
		error("slurm_allocate_resources returned %d", rc);
		exit(1);
	}

	printf("jobid %d: `%s', cpu counts: ", resp->job_id, resp->node_list);

	for (i = 0; i < resp->num_cpu_groups; i++) {
		printf("%u(x%u), ", resp->cpus_per_node[i], resp->cpu_count_reps[i]);
	}
	printf("\n");
		       
}
