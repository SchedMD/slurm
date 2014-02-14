/*****************************************************************************\
 *  smd.c - Command interface for fault tolerant application support
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *  Written by David Bigagli <david@schedmd.com>
 *  All rights reserved
\*****************************************************************************/

#include "slurm/smd_ns.h"
#include "src/smd/smd.h"

/* These are the client requests for the controller.
 */
static struct faulty_node_request faulty_request;
static struct drain_node_request drain_request;
static struct replace_node_request replace_request;
static struct job_time_extend_request extend_request;
static struct drop_node_request drop_request;
static struct job_nonstop_info_request job_request;

/* Wrappers for API that process the RPC request
 * reply.
 */
static int do_faulty(struct faulty_node_request *);
static int do_drain(struct drain_node_request *);
static int do_replace(struct replace_node_request *, uint16_t);
static int do_extend(struct job_time_extend_request *);
static int do_drop(struct drop_node_request *, uint16_t);
static int do_showconfig(void);
static int do_jobinfo(struct job_nonstop_info_request *);

/* manual()
 *
 * Manually process all options and take actions.
 *
 */
int
manual(void)
{
	int cc = 0;

	if (params->failed) {

		faulty_request.job_id = params->job_id;
		/* Go and do handle the API call details.
		 */
		cc = do_faulty(&faulty_request);
		goto bye;
	}

	if (params->drain) {
		drain_request.job_id = params->job_id;
		drain_request.node = params->node;
		drain_request.reason = params->reason;
		cc = do_drain(&drain_request);
		goto bye;
	}

	if (params->replace) {
		replace_request.job_id = params->job_id;
		replace_request.node = params->node;
		cc = do_replace(&replace_request, params->env_vars);
		goto bye;
	}

	if (params->extend) {
		extend_request.job_id = params->job_id;
		extend_request.minutes = params->extend;
		cc = do_extend(&extend_request);
		goto bye;
	}

	if (params->drop) {
		drop_request.job_id = params->job_id;
		drop_request.node = params->node;
		cc = do_drop(&drop_request, params->env_vars);
		goto bye;
	}

	if (params->sconfig) {
		do_showconfig();
		goto bye;
	}

	if (params->jinfo) {
		job_request.job_id = params->job_id;
		cc = do_jobinfo(&job_request);
		goto bye;
	}

bye:
	return cc;
}

static int
do_faulty(struct faulty_node_request *r)
{
	int cc;
	struct faulty_node_reply rep;
	struct node_state *n;

	cc = smd_get_job_faulty_nodes(r, &rep);
	if (cc < 0) {
		fprintf(stderr,
			"%s: smd_get_failed_nodes() error job_id %d: %s\n",
			__func__, r->job_id, smd_nonstop_errstr(errno));
		return -1;
	}

	if (rep.num == 0) {
		printf("Job %u has no failed or failing hosts\n", rep.job_id);
		return 0;
	}

	n = rep.nodes;
	printf("Job %u has %d failed or failing hosts:\n", rep.job_id, rep.num);

	/* First check for failed nodes, then for failing.
	 */
	for (cc = 0; cc < rep.num; cc++) {
		if (n[cc].state & FAILED_NODES)
			printf("  node %s cpu_count %d state FAILED\n",
			       n[cc].node_name, n[cc].cpu_cnt);
	}

	for (cc = 0; cc < rep.num; cc++) {
		if (n[cc].state & FAILING_NODES)
			printf("  node %s cpu_count %d state FAILING\n",
			       n[cc].node_name, n[cc].cpu_cnt);
	}

	/* Free the reply data structure content.
	 */
	smd_free_job_faulty_nodes_reply(&rep);

	return 0;
}

static int
do_drain(struct drain_node_request *r)
{
	int cc;

	cc = smd_drain_job_node(r);
	if (cc < 0) {
		fprintf(stderr, "%s: smd_drain_node() failed job_id %d: %s\n",
			__func__, r->job_id, smd_nonstop_errstr(errno));
		return -1;
	}

	printf("Job %u node %s is being drained\n", r->job_id, r->node);

	return 0;
}

static int
do_replace(struct replace_node_request *r, uint16_t env_vars)
{
	int cc;
	struct replace_node_reply rep;

	memset(&rep, 0, sizeof(rep));
	cc = smd_replace_job_node(r, &rep);
	if (cc < 0) {
		fprintf(stderr,
			"%s: smd_replace_node() error job_id %u: %s\n",
			__func__, r->job_id, smd_nonstop_errstr(errno));
		return -1;
	}

	if (rep.replacement_node != NULL) {
		printf("Job %u got node %s replaced with node %s\n",
		       rep.job_id, rep.failed_node, rep.replacement_node);
		if (env_vars) {
			printf("export SLURM_NODELIST=%s\n",
			       rep.new_set.new_nodelist);
			printf("export SLURM_JOB_NODELIST=%s\n",
			       rep.new_set.new_nodelist);
			printf("export SLURM_NNODES=%d\n", rep.new_set.new_node_cnt);
			printf("export SLURM_JOB_NUM_NODES=%d\n",
			       rep.new_set.new_node_cnt);
			printf("export SLURM_JOB_CPUS_PER_NODE=%s\n",
			       rep.new_set.new_cpus_per_node);
			printf("unset SLURM_TASKS_PER_NODE\n");
		}
	} else {
		printf("Job %u will have replacement available at %.15s\n",
		       rep.job_id, ctime(&rep.when_available) + 4);
	}

	/* Free reply */
	smd_free_replace_job_node_reply(&rep);
	return 0;
}

static int
do_extend(struct job_time_extend_request *r)
{
	int cc;

	cc = smd_extend_job_time(r);
	if (cc < 0) {
		fprintf(stderr,
			"%s: smd_time_incr() () failed job_id %d: %s\n",
			__func__, r->job_id, smd_nonstop_errstr(errno));
		return -1;
	}

	printf("Job %u run time increased by %dmin successfully\n",
	       r->job_id, r->minutes);

	return 0;
}

static int
do_drop(struct drop_node_request *r, uint16_t env_vars)
{
	int cc;
	struct drop_node_reply rep;

	memset(&rep, 0, sizeof(rep));

	cc = smd_drop_job_node(r, &rep);
	if (cc < 0) {
		fprintf(stderr, "%s: smd_drop_node() failed job_id %d: %s\n",
			__func__, r->job_id, smd_nonstop_errstr(errno));
		return -1;
	} else {
		printf("Job %u node %s dropped successfully\n", r->job_id,
		       r->node);
		if (env_vars) {
			printf("export SLURM_NODELIST=%s\n", rep.new_set.new_nodelist);
			printf("export SLURM_JOB_NODELIST=%s\n",
			       rep.new_set.new_nodelist);
			printf("export SLURM_NNODES=%d\n", rep.new_set.new_node_cnt);
			printf("export SLURM_JOB_NUM_NODES=%d\n",
			       rep.new_set.new_node_cnt);
			printf("export SLURM_JOB_CPUS_PER_NODE=%s\n",
			       rep.new_set.new_cpus_per_node);
			printf("unset SLURM_TASKS_PER_NODE\n");
		}
	}

	/* Free reply */
	smd_free_drop_job_node_reply(&rep);

	return 0;
}

static int
do_showconfig(void)
{
	int cc;
	struct nonstop_config config;

	if (params->verbose)
		printf("Reading configuration\n");
	cc = smd_get_nonstopconfig(&config);
	if (cc < 0) {
		fprintf(stderr, "%s: smd_get_nonstopconfig() failed: %s.\n",
			__func__, smd_nonstop_errstr(errno));
		return -1;
	}

	printf("System Configuration:\n");
	if (config.backup_addr)
		printf("  BackupControllerAddress: %s\n", config.backup_addr);
	printf("  ConfigurationFile: %s\n", config.conf_fname);
	printf("  ControllerAddress: %s\n", config.control_addr);
	printf("  ControllerPort: %u\n", config.port);
	printf("  HotSpareCount: %s\n", config.hot_spare_count);
	printf("  LibraryDebug: %u\n", config.debug);
	printf("  MaxSpareNodeCount: %u\n", config.max_spare_node_count);
	printf("  ReadTimeout: %u\n", config.read_timeout);
	printf("  TimeLimitDelay: %u\n", config.time_limit_delay);
	printf("  TimeLimitDrop: %u\n", config.time_limit_drop);
	printf("  TimeLimitExtend: %u\n", config.time_limit_extend);
	printf("  UserDrainAllow: %s\n", config.user_drain_allow);
	printf("  UserDrainDeny: %s\n", config.user_drain_deny);
	printf("  WriteTimeout: %u\n", config.write_timeout);

	/* Free the dynamic elements of the config structure.
	 */
	smd_free_nonstop_config(&config);

	return 0;
}

static int
do_jobinfo(struct job_nonstop_info_request *r)
{
	int cc;
	struct job_nonstop_info_reply j;

	cc = smd_nonstop_get_failed_jobinfo(r, &j);
	if (cc < 0) {
		fprintf(stderr,
			"%s: smd_nonstop_get_jobinfo() failed job_id %d: %s\n",
			__func__, r->job_id, smd_nonstop_errstr(errno));
		return -1;
	}

	switch (errno) {
		case ENSTOP_NONODEFAIL:
			printf("Job %u has no failed or failing nodes\n", j.job_id);
			return 0;
		case ENSTOP_JOBID:
			printf("No such job %u\n", j.job_id);
			return 0;
	}

	printf("Job %u information:\n", j.job_id);
	printf("  FaileNodeCount: %u\n", j.failed_node_cnt);
	for (cc = 0; cc < j.failed_node_cnt; cc++) {
		printf("    NodeName: %s CPU_Count: %u\n",
		       j.failed_nodes[cc].node_name,
		       j.failed_nodes[cc].cpu_cnt);
	}
	printf("  PendingJobDelay: %u\n", j.pending_job_delay);
	printf("  PendingJobID: %u\n", j.pending_job_id);
	printf("  PendingNodeName: %s\n", j.pending_node_name);
	printf("  ReplaceNodeCount: %u\n", j.replace_node_cnt);
	printf("  TimeExtendAvail: %u\n", j.time_extend_avail);

	/* Free the dynamic elements of the reply structure.
	 */
	smd_nonstop_free_failed_jobinfo(&j);

	return 0;
}
