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

static int v;
static char job_env_file[PATH_MAX];
static char signal_buf[BUFSIZ];

static inline int _want_handle_failed(struct key_val *);
static inline int _want_handle_failing(struct key_val *);
static inline int _want_replace(struct key_val *);
static inline int _want_drop(struct key_val *);
static inline int _want_exit(struct key_val *);
static inline int _want_extend(struct key_val *, int *);
static inline int _want_delay(struct key_val *, int *);

static int is_failed(struct list_ *, int);
static inline int _want_time_limit_drop(struct key_val *, int *);
static inline char *_fail_type(int);
static int _handle_fault(struct list_ *, struct key_val *);
static int _try_replace(struct list_ *, struct key_val *);
static int _drop_nodes(struct list_ *, struct key_val *);
static int _exit_job(void);
static int _generate_node_file(struct new_node_set *);
static int _increase_job_runtime(struct job_time_extend_request *);
static int _time_limit_extend(int);

/* automatic()
 * Given the users' specification what to do in
 * case of node failure perform the requested
 * action automatically.
 */
int
automatic(void)
{
	struct key_val **kv;
	struct list_ *l;
	int cc;

	v = 0;
	if (params->verbose)
		v = 1;

	/* Working data structures, the list
	 * of failed/failing nodes and the
	 * actions to take as requested by
	 * the user.
	 */
	cc = 0;
	/* kv[0] = failed
	 * kv[1] = failing
	 * These arrays hold the user desired
	 * behavior in case of node failure.
	 */
	kv = get_key_val();

	/* The user is not interested in handling
	 * failures?
	 */
	assert(_want_handle_failed(kv[0])
	       || _want_handle_failing(kv[1]));

	l = listmake("nodes");

	/* Handle failed nodes.
	 */
	if (_want_handle_failed(kv[0])
	    && is_failed(l, FAILED_NODES)) {

		cc = _handle_fault(l, kv[0]);
		listfree(l, smd_free_node_state);
	}

	/* Handle failing nodes.
	 */
	if (_want_handle_failing(kv[1])
	    && is_failed(l, FAILING_NODES)) {

		cc = _handle_fault(l, kv[1]);
		listfree(l, smd_free_node_state);
	}

	return cc;
}

/* _handle_faults()
 */
static int
_handle_fault(struct list_ *l, struct key_val *kv)
{
	int cc;
	char *p;

	if (kv[failure_type].val == failed_hosts)
		p = "failed_hosts";
	else
		p = "failing_hosts";

	smd_log(stderr, "\
%s: job %u handle %s", __func__, params->job_id, p);

	/* Is it your desire to replace them?
	 */
	if (_want_replace(kv)) {

		/* Let's try to do our best to
		 * replace the troubled nodes.
		 */
		cc = _try_replace(l, kv);
		if (cc == 0)
			goto cya;
	}

	/* Is it your desire to drop them?
	 */
	if (_want_drop(kv)) {

		cc = _drop_nodes(l, kv);
		if (cc == 0)
			goto cya;
	}

	if (_want_exit(kv)) {
		_exit_job();
		/* Race against SLURM killing
		 * this process.
		 */
		exit(-1);
	}

cya:
	return cc;
}

/* is_failed()
 */
static int
is_failed(struct list_ *l, int option)
{
	int cc;
	struct faulty_node_request r;
	struct faulty_node_reply rep;

	smd_log(stderr, "\
%s: job %u searching for %s hosts", __func__, params->job_id,
	        _fail_type(option));

	r.job_id = params->job_id;
	r.options = option;

	cc = smd_get_job_faulty_nodes(&r, &rep);
	if (cc < 0) {
		smd_log(stderr, "\
%s: smd_get_failed_nodes() error job_id %d: %s",
		      __func__, r.job_id, smd_nonstop_errstr(errno));
		return 0;
	}

	if (rep.num == 0) {
		smd_log(stderr, "\
%s: job %u has no %s nodes", __func__, rep.job_id, _fail_type(option));
		return 0;
	}

	smd_log(stderr, "\
%s: job %u has %d %s nodes", __func__, rep.job_id, rep.num, _fail_type(option));

	for (cc = 0; cc < rep.num; cc++) {
		struct node_state *n;
		struct liste *e;

		e = calloc(1, sizeof(struct liste));
		/* dup() the node structure as rep.nodes is an array
		 * allocated by the library and we dont want to carry
		 * around pointers inside the array.
		 */
		n = calloc(1, sizeof(struct node_state));
		n->node_name = strdup(rep.nodes[cc].node_name);
		n->cpu_cnt = rep.nodes[cc].cpu_cnt;
		n->state = rep.nodes[cc].state;
		e->data = n;
		assert(n->state == option);
		/* Hop in da list for further processing
		 */
		listenque(l, (struct list_ *)e);

		smd_log(stderr, "\
%s: job %d %s node %s cpu_count %d",  __func__, params->job_id,
		        _fail_type(option), n->node_name, n->cpu_cnt);
	} /* for () */

	smd_free_job_faulty_nodes_reply(&rep);

	return 1;
}

/* _try_replace()
 */
static int
_try_replace(struct list_ *l, struct key_val *kv)
{
	struct liste *e;
	struct liste *e2;
	struct replace_node_request req;
	struct replace_node_reply reply;
	struct replace_node_reply reply2;
	struct node_state *ns;
	int cc;
	int wait_until;
	int max_wait;
	int udelay;
	int num_replace;
	int uextend;
	int cnt;
	int retry_intvl;

	num_replace = 0;
	cnt = wait_until = max_wait = 0;
	retry_intvl = 20;

	do  {

		smd_log(stderr, "\
%s: job %d trying to replace %d nodes", __func__,
		        params->job_id, LIST_NUM_ENTS(l));

		memset(&reply2, 0, sizeof(struct replace_node_reply));
		e2 = NULL;
		for (e = (struct liste *)l->forw; e != (struct liste *)l; e = e2) {

			/* save next in case we pop
			 * and free the current element.
			 */
			e2 = (struct liste *)e->forw;
			ns = (struct node_state *)e->data;
			req.job_id = params->job_id;
			/* dup it as we free the objects
			 * separately
			 */
			req.node = strdup(ns->node_name);

			cc = smd_replace_job_node(&req, &reply);
			if (cc == 0) {
				/* The node has been replaced all right
				 */
				smd_log(stderr, "\
%s: job %d node %s replaced by %s", __func__, params->job_id,
					        reply.failed_node, reply.replacement_node);
				smd_free_node_state(ns);
				listrm(l, (struct list_ *)e);
				list_element_free(e);
				/* as we keep replacing nodes the reply structure
				 * gets updated by the controller so we only
				 * need to keep the last one.
				 */
				smd_free_replace_job_node_reply(&reply2);
				reply2.replacement_node = strdup(reply.replacement_node);
				reply2.new_set.new_nodelist = strdup(reply.new_set.new_nodelist);
				reply2.new_set.new_node_cnt = reply.new_set.new_node_cnt;
				reply2.new_set.new_cpus_per_node = strdup(reply.new_set.new_cpus_per_node);
				/* and free the working copy...
				 */
				smd_free_replace_job_node_reply(&reply);
				++num_replace;
				continue;
			}

			/* Either there is an I/O error with the controller
			 * or the controller could not perform the requested
			 * operation, the erron tells the reason.
			 */
			smd_log(stderr, "\
%s: smd_replace_node() error job_id %u: %s",
			        __func__, req.job_id, smd_nonstop_errstr(errno));

			if (errno == ENSTOP_REPLACELATER) {
				/* We are eligible to increment our run time
				 * TimiLimitDelay.
				 */
				smd_log(stderr, "\
%s: job %u will have replacement available at %.15s", __func__,
				        reply.job_id, ctime(&reply.when_available) + 4);

				/* Save the max waiting time from slurmctld
				 */
				if (reply.when_available > max_wait)
					max_wait = reply.when_available;
			}

		} /* for (e = l->forw; e != l; e2) */

		/* We could be waiting for resources for some time
		 * and suddenly they become available. We want to
		 * break right away otherwise we may be stuck looping
		 * since wait_until > 0.
		 */
		if (LIST_NUM_ENTS(l) == 0)
			break;

		/* Some nodes are still left on the list meaning
		 * we were not able to replace them all.
		 * See for how long the user wants to wait and
		 * poll the controller meanwhile to see if some
		 * new resources are available.
		 */
		if (LIST_NUM_ENTS(l) > 0) {
			time_t t;

			/* The user does not want to wait so
			 * break from the timing loop for good.
			 */
			if (!_want_delay(kv, &udelay))
				break;

			t = time(NULL);
			if (wait_until == 0) {

				/* Convert the delay in seconds since
				 * we use time().
				 */
				udelay = kv[time_limit_delay].val * 60;

				if (max_wait > 0)
					wait_until = MIN(udelay, max_wait) + t;
				else
					wait_until = udelay + t;

				/* TimeLimitDelay in minutes
				 */
				_time_limit_extend(udelay/60);
			}

			smd_log(stderr, "\
%s: job %d waited for %d sec cnt %d trying every %d sec...",
			        __func__, params->job_id,
			        cnt * 20, cnt, retry_intvl);
			++cnt;

			/* In case the user specify 0 min
			 * wait time.
			 */
			if (udelay > 0)
				millisleep_(retry_intvl * 1000);
		}

	} while (wait_until > time(NULL));

	if (LIST_NUM_ENTS(l) > 0) {
		/* Replacement attempt failed.
		 */
		smd_log(stderr, "\
%s: job %d failed to replace down or failing nodes:", __func__, params->job_id);
		for (e = (struct liste *)l->forw;
		     e != (struct liste *)l;
		     e = (struct liste *)e->forw) {
			ns = (struct node_state *)e->data;
			smd_log(stderr, "   %s", ns->node_name);
		}
		return -1;
	}

	/* yahoo!! all hosts were replaced.
	 */
	_generate_node_file(&reply2.new_set);
	smd_free_replace_job_node_reply(&reply2);

	/* TimeLimitExtend
	 * Specifies the number of minutes that a job can extend it’s time limit
	 * for each replaced  node.
	 */
	if (_want_extend(kv, &uextend))
		_time_limit_extend(uextend * num_replace);

	smd_log(stderr, "\
%s: job %d all nodes replaced all right", __func__, params->job_id);

	return 0;
}

/* _generate_node_file()
 */
static int
_generate_node_file(struct new_node_set *new_set)
{
	FILE *fp;

	sprintf(job_env_file, "/tmp/smd_job_%u_nodes.sh", params->job_id);

	fp = fopen(job_env_file, "w");
	if (fp == NULL) {
		smd_log(stderr, "\
%s: failed opening %s %s", __func__, job_env_file, strerror(errno));
		return -1;
	}

	fprintf(fp, "export SLURM_NODELIST=%s\n", new_set->new_nodelist);
	fprintf(fp, "export SLURM_JOB_NODELIST=%s\n", new_set->new_nodelist);
	fprintf(fp, "export SLURM_NNODES=%d\n", new_set->new_node_cnt);
	fprintf(fp, "export SLURM_JOB_NUM_NODES=%d\n", new_set->new_node_cnt);
	fprintf(fp, "export SLURM_JOB_CPUS_PER_NODE=%s\n", new_set->new_cpus_per_node);
	fprintf(fp, "unset SLURM_TASKS_PER_NODE\n");

	fclose(fp);

	smd_log(stderr, "\
%s: job %d all nodes replaced\n\
source the %s hostfile to get the new job environment", __func__,
	        params->job_id, job_env_file);

	return 0;
}

/* _try_drop()
 */
static int
_drop_nodes(struct list_ *l, struct key_val *kv)
{
	struct liste *e;
	struct liste *e2;
	struct drop_node_request r;
	struct drop_node_reply reply;
	int cc;
	int num_drop;
	int tdrop;

	num_drop = 0;
	e2 = NULL;
	for (e = (struct liste *)l->forw; e != (struct liste *)l; e = e2) {
		struct node_state *ns;

		/* save next in case we pop
		 * and free the current element.
		 */
		e2 = (struct liste *)e->forw;
		ns = (struct node_state *)e->data;

		r.job_id = params->job_id;
		r.node = ns->node_name;

		cc = smd_drop_job_node(&r, &reply);
		if (cc < 0) {
			smd_log(stderr, "\
%s: job %d failed to drop node %s: %s", __func__,
			        r.job_id, r.node, smd_nonstop_errstr(errno));
			/* Check if user wants to exit if a failure happens
			 * in the sytem in which case return right away and
			 * let the job be killed.
			 */
			if (_want_exit(kv))
				return -1;
		}

		smd_log(stderr, "\
%s: job %d node %s dropped all right", __func__, r.job_id, r.node);
		/* Let's generate a new job environment file
		 * which has to be sourced by the user job before
		 * starting next step.
		 */
		_generate_node_file(&reply.new_set);
		++num_drop;
		/* free all allocated ram at this point
		 * regardless if the operation went ok
		 * or not as we are going to exit anyway.
		 */
		smd_free_drop_job_node_reply(&reply);
		smd_free_node_state(ns);
		listrm(l, (struct list_ *)e);
		list_element_free(e);

	} /* for(;;) */

	/* TimeLimitDrop
	 * Specifies the number of minutes that a job can
	 * extend it’s time limit for  each  failed or failing
	 * node  removed  from the job’s allocation.
	 */
	if (_want_time_limit_drop(kv, &tdrop))
		_time_limit_extend(tdrop * num_drop);

	return 0;
}

/* _exit_job()
 */
static int
_exit_job(void)
{
	int cc;

	smd_log(stderr, "\
%s: job %u asking SLURM to cancel myself", __func__, params->job_id);

	/* Terminate the current job. Call the command to
	 * avoid linking with the SLURM library.
	 */
	sprintf(signal_buf, "scancel %d", params->job_id);

	cc = system(signal_buf);
	if (WIFEXITED(cc)) {
		if (WEXITSTATUS(cc) == 0)
			smd_log(stderr, "\
%s: jobID %d signaled all right", __func__, params->job_id);
		else
			smd_log(stderr, "\
%s: jobID %d error signaling job", __func__, params->job_id);
	} else if (WIFSIGNALED(cc)) {
		smd_log(stderr, "\
%s: jobID %d scancel killed by signal %d", __func__,
		        params->job_id, WTERMSIG(cc));
	}

	return 0;
}

/* _time_limit_extend()
 */
static int
_time_limit_extend(int extend)
{
	int cc;
	struct job_time_extend_request r;

	smd_log(stderr, "\
%s: job %d extending job time limit by %d minutes",
	        __func__, params->job_id, extend);

	r.job_id = params->job_id;
	r.minutes = extend;

	cc = _increase_job_runtime(&r);
	return cc;
}

/* _increase_job_time()
 */
static int
_increase_job_runtime(struct job_time_extend_request *r)
{
	int cc;

	cc = smd_extend_job_time(r);
	if (cc < 0) {
		smd_log(stderr, "\
%s: smd_time_incr() () failed job_id %d: %s",
		        __func__, r->job_id, smd_nonstop_errstr(errno));
		return -1;
	}

	smd_log(stderr, "\
%s: job %u run time limit extended by %dmin successfully",
	        __func__, r->job_id, r->minutes);

	return 0;
}
/* _want_handle_failed()
 */
static inline int
_want_handle_failed(struct key_val *kv)
{
	return (kv[0].val != UINT32_MAX);
}

/* _want_handle_failing()
 */
static inline int
_want_handle_failing(struct key_val *kv)
{
	return (kv[0].val != UINT32_MAX);
}

/* _want_replace()
 */
static inline int
_want_replace(struct key_val *kv)
{
	return (kv[replace].val != UINT32_MAX);
}

/* _want_drop()
 */
static inline int
_want_drop(struct key_val *kv)
{
	return (kv[time_limit_drop].val != UINT32_MAX);
}

/* _want_exit()
 */
static inline int
_want_exit(struct key_val *kv)
{
	return (kv[exit_job].val != UINT32_MAX);
}

/* _want_delay()
 */
static inline int
_want_extend(struct key_val *kv, int *ext)
{
	*ext = kv[time_limit_extend].val;
	return (kv[time_limit_extend].val != UINT32_MAX);
}

/* _want_time_limit_drop()
 */
static inline int
_want_time_limit_drop(struct key_val *kv, int *tdrop)
{
	*tdrop = kv[time_limit_drop].val;
	return (kv[time_limit_drop].val != UINT32_MAX);
}

/* _want_delay()
 */
static inline int
_want_delay(struct key_val *kv, int *delay)
{
	*delay = kv[time_limit_delay].val;
	return (kv[time_limit_delay].val != UINT32_MAX);
}

/* _fail_type()
 */
static inline char *
_fail_type(int option)
{
	char *p;

	if (option & FAILED_NODES)
		p = "FAILED";
	else
		p = "FAILING";

	return p;
}
