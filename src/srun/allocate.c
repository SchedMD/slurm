/*****************************************************************************\
 *  src/srun/allocate.c - srun functions for managing node allocations
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <poll.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/env.h"
#include "src/common/fd.h"
#include "src/common/forward.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_time.h"
#include "src/common/threadpool.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/auth.h"
#include "src/interfaces/gres.h"

#include "src/srun/allocate.h"
#include "src/srun/launch.h"
#include "src/srun/opt.h"
#include "src/srun/signals.h"

#define MAX_ALLOC_WAIT	60	/* seconds */
#define MIN_ALLOC_WAIT	5	/* seconds */
#define MAX_RETRIES	10
#define POLL_SLEEP	0.5	/* retry interval in seconds  */

pthread_mutex_t msg_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t msg_cond = PTHREAD_COND_INITIALIZER;
allocation_msg_thread_t *msg_thr = NULL;
struct pollfd global_fds[1];

extern char **environ;

/*
 * Static Prototypes
 */
static job_desc_msg_t *_job_desc_msg_create_from_opts(slurm_opt_t *opt_local);
static void _set_pending_job_id(slurm_step_id_t *step_id);
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc);

static bool is_het_job = false;

static void _set_pending_job_id(slurm_step_id_t *step_id)
{
	debug2("Pending job allocation %pI", step_id);
	slurm_mutex_lock(&pending_job_id_lock);
	pending_job_id = *step_id;
	slurm_mutex_unlock(&pending_job_id_lock);
}

/* This typically signifies the job was cancelled by scancel */
static void _job_complete_handler(srun_job_complete_msg_t *msg)
{
	slurm_step_id_t local_pending_job_id;

	slurm_mutex_lock(&pending_job_id_lock);
	local_pending_job_id = pending_job_id;
	slurm_mutex_unlock(&pending_job_id_lock);

	if (!is_het_job && (local_pending_job_id.job_id != NO_VAL) &&
	    (local_pending_job_id.job_id != msg->job_id)) {
		error("Ignoring job_complete for %pI because we are %pI",
		      msg, &local_pending_job_id);
		return;
	}

	/* Only print if we know we were signaled */
	slurm_mutex_lock(&srun_destroy_sig_lock);
	if (srun_destroy_sig)
		info("Force Terminated %ps", msg);
	srun_destroy_sig = SIGTERM;
	slurm_mutex_unlock(&srun_destroy_sig_lock);
}

/*
 * Job has been notified of it's approaching time limit.
 * Job will be killed shortly after timeout.
 * This RPC can arrive multiple times with the same or updated timeouts.
 * FIXME: We may want to signal the job or perform other action for this.
 * FIXME: How much lead time do we want for this message? Some jobs may
 *	require tens of minutes to gracefully terminate.
 */
static void _timeout_handler(srun_timeout_msg_t *msg)
{
	static time_t last_timeout = 0;

	if (msg->timeout != last_timeout) {
		last_timeout = msg->timeout;
		verbose("job time limit to be reached at %s",
			slurm_ctime2(&msg->timeout));
	}
}

static void _user_msg_handler(srun_user_msg_t *msg)
{
	info("%s", msg->msg);
}

static void _node_fail_handler(srun_node_fail_msg_t *msg)
{
	error("Node failure on %s", msg->nodelist);
}



static bool _retry(void)
{
	static int  retries = 0;
	static char *msg = "Slurm controller not responding, "
		"sleeping and retrying.";

	if ((errno == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) || (errno == EAGAIN)) {
		if (retries == 0)
			error("%s", msg);
		else if (retries < MAX_RETRIES)
			debug("%s", msg);
		else
			return false;
		sleep (++retries);
	} else if (errno == EINTR) {
		/* srun may be interrupted by the BLCR checkpoint signal */
		/*
		 * XXX: this will cause the old job cancelled and a new
		 * job allocated
		 */
		debug("Syscall interrupted while allocating resources, "
		      "retrying.");
		return true;
	} else if (opt.immediate &&
		   ((errno == ETIMEDOUT) || (errno == ESLURM_NODES_BUSY) ||
		    (errno == ESLURM_PORTS_BUSY))) {
		error("Unable to allocate resources: %s",
		      slurm_strerror(ESLURM_NODES_BUSY));
		error_exit = immediate_exit;
		return false;
	} else if ((errno == SLURM_PROTOCOL_AUTHENTICATION_ERROR) ||
		   (errno == SLURM_UNEXPECTED_MSG_ERROR) ||
		   (errno == SLURM_PROTOCOL_INSANE_MSG_LENGTH)) {
		static int external_msg_count = 0;
		error("Srun communication socket apparently being written to "
		      "by something other than Slurm");
		if (external_msg_count++ < 4)
			return true;
		error("Unable to allocate resources: %m");
		return false;
	} else {
		error("Unable to allocate resources: %m");
		return false;
	}

	return true;
}

/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc)
{
	double cur_delay = 0;
	double cur_sleep = 0;
	int is_ready = 0, i = 0, rc;
	bool job_killed = false;

	slurm_mutex_lock(&pending_job_id_lock);
	pending_job_id = alloc->step_id;
	slurm_mutex_unlock(&pending_job_id_lock);

	while (true) {
		int tmp_srun_destroy_sig = 0;

		if (i) {
			/*
			 * First sleep should be very quick to improve
			 * responsiveness.
			 *
			 * Otherwise, increment by POLL_SLEEP for every loop.
			 */
			if (cur_delay == 0)
				cur_sleep = 0.1;
			else if (cur_sleep < 300)
				cur_sleep = POLL_SLEEP * i;
			if (i == 1)
				verbose("Waiting for resource configuration");
			else
				debug("Waited %f sec and still waiting: next sleep for %f sec",
				      cur_delay, cur_sleep);
			usleep(USEC_IN_SEC * cur_sleep);
			cur_delay += cur_sleep;
		}
		i += 1;

		rc = slurm_job_node_ready(alloc->step_id);
		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */

		slurm_mutex_lock(&srun_destroy_sig_lock);
		tmp_srun_destroy_sig = srun_destroy_sig;
		slurm_mutex_unlock(&srun_destroy_sig_lock);

		if (tmp_srun_destroy_sig)
			break;
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0) {	/* job killed */
			job_killed = true;
			break;
		}
		if ((rc & READY_NODE_STATE) &&
		    (rc & READY_PROLOG_STATE)) {
			is_ready = 1;
			break;
		}
	}
	slurm_mutex_lock(&srun_destroy_sig_lock);
	if (is_ready) {
		if (i > 0)
     			verbose("Nodes %s are ready for job", alloc->node_list);
	} else if (!srun_destroy_sig) {
		if (job_killed) {
			error("Job allocation %u has been revoked",
			      alloc->step_id.job_id);
			srun_destroy_sig = SIGTERM;
		} else
			error("Nodes %s are still not ready", alloc->node_list);
	} else /* allocation_interrupted and slurmctld not responing */
		is_ready = 0;
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	slurm_mutex_lock(&pending_job_id_lock);
	pending_job_id = SLURM_STEP_ID_INITIALIZER;
	slurm_mutex_unlock(&pending_job_id_lock);

	return is_ready;
}

static int _allocate_test(slurm_opt_t *opt_local)
{
	job_desc_msg_t *j;
	int rc;

	if ((j = _job_desc_msg_create_from_opts(opt_local)) == NULL)
		return SLURM_ERROR;

	if (opt_local->clusters &&
	    (slurmdb_get_first_avail_cluster(j, opt_local->clusters,
					     &working_cluster_rec)
	     != SLURM_SUCCESS)) {
		print_db_notok(opt_local->clusters, 0);
		return SLURM_ERROR;
	}

	rc = slurm_job_will_run(j);
	job_desc_msg_destroy(j);
	return rc;

}

extern int allocate_test(void)
{
	int rc = SLURM_SUCCESS;
	list_itr_t *iter;
	slurm_opt_t *opt_local;

	if (opt_list) {
		iter = list_iterator_create(opt_list);
		while ((opt_local = list_next(iter))) {
			if ((rc = _allocate_test(opt_local)) != SLURM_SUCCESS)
				break;
 		}
		list_iterator_destroy(iter);
	} else {
		rc = _allocate_test(&opt);
	}

	return rc;
}

/*
 * Allocate nodes from the slurm controller -- retrying the attempt
 * if the controller appears to be down, and optionally waiting for
 * resources if none are currently available (see opt.immediate)
 *
 * Returns a pointer to a resource_allocation_response_msg which must
 * be freed with slurm_free_resource_allocation_response_msg()
 */
extern resource_allocation_response_msg_t *allocate_nodes(
	slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	resource_allocation_response_msg_t *resp = NULL;
	job_desc_msg_t *j;
	slurm_allocation_callbacks_t callbacks;
	int tmp_srun_destroy_sig;

	xassert(srun_opt);

	if (srun_opt->relative != NO_VAL)
		fatal("--relative option invalid for job allocation request");

	if ((j = _job_desc_msg_create_from_opts(&opt)) == NULL)
		return NULL;

	if (opt_local->clusters &&
	    (slurmdb_get_first_avail_cluster(j, opt_local->clusters,
					     &working_cluster_rec)
	     != SLURM_SUCCESS)) {
		print_db_notok(opt_local->clusters, 0);
		return NULL;
	}

	j->origin_cluster = xstrdup(slurm_conf.cluster_name);

	callbacks.timeout = _timeout_handler;
	callbacks.job_complete = _job_complete_handler;
	callbacks.job_suspend = NULL;
	callbacks.user_msg = _user_msg_handler;
	callbacks.node_fail = _node_fail_handler;

	/* create message thread to handle pings and such from slurmctld */
	msg_thr = slurm_allocation_msg_thr_create(&j->other_port, &callbacks);

	while (!resp) {
		resp = slurm_allocate_resources_blocking(j,
							 opt_local->immediate,
							 _set_pending_job_id,
							 srun_sig_eventfd);

		slurm_mutex_lock(&srun_destroy_sig_lock);
		tmp_srun_destroy_sig = srun_destroy_sig;
		slurm_mutex_unlock(&srun_destroy_sig_lock);

		if (tmp_srun_destroy_sig) {
			slurm_mutex_lock(&pending_job_id_lock);
			if (pending_job_id.job_id != NO_VAL)
				info("Job allocation %u has been revoked",
				     pending_job_id.job_id);
			slurm_mutex_unlock(&pending_job_id_lock);

			/* cancelled by signal */
			break;
		} else if (!resp && !_retry()) {
			break;
		}
	}

	if (resp)
		print_multi_line_string(resp->job_submit_user_msg,
					-1, LOG_LEVEL_INFO);

	slurm_mutex_lock(&srun_destroy_sig_lock);
	tmp_srun_destroy_sig = srun_destroy_sig;
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	if (resp && !tmp_srun_destroy_sig) {
		/*
		 * Allocation granted!
		 */
		slurm_mutex_lock(&pending_job_id_lock);
		pending_job_id = resp->step_id;
		slurm_mutex_unlock(&pending_job_id_lock);

		/*
		 * These values could be changed while the job was
		 * pending so overwrite the request with what was
		 * allocated so we don't have issues when we use them
		 * in the step creation.
		 */
		opt_local->pn_min_memory = NO_VAL64;
		opt_local->mem_per_cpu = NO_VAL64;
		if (resp->pn_min_memory != NO_VAL64) {
			if (resp->pn_min_memory & MEM_PER_CPU) {
				opt_local->mem_per_cpu = (resp->pn_min_memory &
							 (~MEM_PER_CPU));
			} else {
				opt_local->pn_min_memory = resp->pn_min_memory;
			}
		}

		opt_local->min_nodes = resp->node_cnt;
		opt_local->max_nodes = resp->node_cnt;
		xfree(opt_local->gres);
		opt_local->gres = xstrdup(resp->tres_per_node);

		if (resp->working_cluster_rec)
			slurm_setup_remote_working_cluster(resp);

		if (!_wait_nodes_ready(resp)) {
			slurm_mutex_lock(&srun_destroy_sig_lock);
			if (!srun_destroy_sig)
				error("Something is wrong with the boot of the nodes.");
			slurm_mutex_unlock(&srun_destroy_sig_lock);
			goto relinquish;
		}
	} else if (tmp_srun_destroy_sig) {
		goto relinquish;
	}

	job_desc_msg_destroy(j);

	return resp;

relinquish:
	if (resp) {
		slurm_mutex_lock(&srun_destroy_sig_lock);
		if (srun_destroy_sig)
			slurm_complete_job(&resp->step_id, 1);
		slurm_mutex_unlock(&srun_destroy_sig_lock);

		slurm_free_resource_allocation_response_msg(resp);
	}
	exit(error_exit);
	return NULL;
}

static int _copy_other_port(void *x, void *arg)
{
	job_desc_msg_t *desc = x;
	desc->other_port = *(uint16_t *)arg;

	return SLURM_SUCCESS;
}

/*
 * Allocate nodes for heterogeneous job from the slurm controller --
 * retrying the attempt if the controller appears to be down, and optionally
 * waiting for resources if none are currently available (see opt.immediate)
 *
 * Returns a pointer to a resource_allocation_response_msg which must
 * be freed with slurm_free_resource_allocation_response_msg()
 */
list_t *allocate_het_job_nodes(void)
{
	resource_allocation_response_msg_t *resp = NULL;
	job_desc_msg_t *j, *first_job = NULL;
	slurm_allocation_callbacks_t callbacks;
	list_itr_t *opt_iter, *resp_iter;
	slurm_opt_t *opt_local, *first_opt = NULL;
	list_t *job_req_list = NULL, *job_resp_list = NULL;
	slurm_step_id_t my_step_id = SLURM_STEP_ID_INITIALIZER;
	int i, k;
	int tmp_srun_destroy_sig;

	job_req_list = list_create(NULL);
	opt_iter = list_iterator_create(opt_list);
	while ((opt_local = list_next(opt_iter))) {
		srun_opt_t *srun_opt = opt_local->srun_opt;
		xassert(srun_opt);
		if (!first_opt)
			first_opt = opt_local;
		if (srun_opt->relative != NO_VAL)
			fatal("--relative option invalid for job allocation request");

		if ((j = _job_desc_msg_create_from_opts(opt_local)) == NULL) {
			FREE_NULL_LIST(job_req_list);
			return NULL;
		}
		if (!first_job)
			first_job = j;

		j->origin_cluster = xstrdup(slurm_conf.cluster_name);

		list_append(job_req_list, j);
	}
	list_iterator_destroy(opt_iter);

	if (!first_job) {
		error("%s: No job requests found", __func__);
		FREE_NULL_LIST(job_req_list);
		return NULL;
	}

	if (first_opt && first_opt->clusters &&
	    (slurmdb_get_first_het_job_cluster(job_req_list,
					       first_opt->clusters,
					       &working_cluster_rec)
	     != SLURM_SUCCESS)) {
		print_db_notok(first_opt->clusters, 0);
		FREE_NULL_LIST(job_req_list);
		return NULL;
	}

	callbacks.timeout = _timeout_handler;
	callbacks.job_complete = _job_complete_handler;
	callbacks.job_suspend = NULL;
	callbacks.user_msg = _user_msg_handler;
	callbacks.node_fail = _node_fail_handler;

	/* create message thread to handle pings and such from slurmctld */
	msg_thr = slurm_allocation_msg_thr_create(&first_job->other_port,
						  &callbacks);
	list_for_each(job_req_list, _copy_other_port, &first_job->other_port);

	is_het_job = true;

	while (first_opt && !job_resp_list) {
		job_resp_list =
			slurm_allocate_het_job_blocking(job_req_list,
							first_opt->immediate,
							_set_pending_job_id,
							srun_sig_eventfd);

		slurm_mutex_lock(&srun_destroy_sig_lock);
		tmp_srun_destroy_sig = srun_destroy_sig;
		slurm_mutex_unlock(&srun_destroy_sig_lock);

		if (tmp_srun_destroy_sig) {
			/* cancelled by signal */
			slurm_mutex_lock(&pending_job_id_lock);
			if (pending_job_id.job_id != NO_VAL)
				info("Job allocation %u has been revoked",
				     pending_job_id.job_id);
			slurm_mutex_unlock(&pending_job_id_lock);
			break;
		} else if (!job_resp_list && !_retry()) {
			break;
		}
	}
	FREE_NULL_LIST(job_req_list);

	slurm_mutex_lock(&srun_destroy_sig_lock);
	tmp_srun_destroy_sig = srun_destroy_sig;
	slurm_mutex_unlock(&srun_destroy_sig_lock);

	if (job_resp_list && !tmp_srun_destroy_sig) {
		/*
		 * Allocation granted!
		 */

		opt_iter  = list_iterator_create(opt_list);
		resp_iter = list_iterator_create(job_resp_list);
		while ((opt_local = list_next(opt_iter))) {
			resp = (resource_allocation_response_msg_t *)
			       list_next(resp_iter);
			if (!resp)
				break;

			slurm_mutex_lock(&pending_job_id_lock);
			if (pending_job_id.job_id == NO_VAL)
				pending_job_id = resp->step_id;
			slurm_mutex_unlock(&pending_job_id_lock);
			if (my_step_id.job_id == NO_VAL) {
				my_step_id = resp->step_id;
				i = list_count(opt_list);
				k = list_count(job_resp_list);
				if (i != k) {
					error("%s: request count != response count (%d != %d)",
					      __func__, i, k);
					goto relinquish;
				}
			}

			/*
			 * These values could be changed while the job was
			 * pending so overwrite the request with what was
			 * allocated so we don't have issues when we use them
			 * in the step creation.
			 */
			if (opt_local->pn_min_memory != NO_VAL64)
				opt_local->pn_min_memory =
					(resp->pn_min_memory & (~MEM_PER_CPU));
			else if (opt_local->mem_per_cpu != NO_VAL64)
				opt_local->mem_per_cpu =
					(resp->pn_min_memory & (~MEM_PER_CPU));

			opt_local->min_nodes = resp->node_cnt;
			opt_local->max_nodes = resp->node_cnt;

			xfree(opt_local->gres);
			opt_local->gres = xstrdup(resp->tres_per_node);

			if (resp->working_cluster_rec)
				slurm_setup_remote_working_cluster(resp);

			if (!_wait_nodes_ready(resp)) {
				slurm_mutex_lock(&srun_destroy_sig_lock);
				if (!srun_destroy_sig)
					error("Something is wrong with the "
					      "boot of the nodes.");
				slurm_mutex_unlock(&srun_destroy_sig_lock);
				goto relinquish;
			}
		}
		list_iterator_destroy(resp_iter);
		list_iterator_destroy(opt_iter);
	} else if (tmp_srun_destroy_sig) {
		goto relinquish;
	}

	return job_resp_list;

relinquish:
	if (job_resp_list) {
		if (my_step_id.job_id == NO_VAL) {
			resp = list_peek(job_resp_list);
			my_step_id = resp->step_id;
		}

		slurm_mutex_lock(&srun_destroy_sig_lock);
		if (srun_destroy_sig && (my_step_id.job_id != NO_VAL)) {
			slurm_complete_job(&my_step_id, 1);
		}
		slurm_mutex_unlock(&srun_destroy_sig_lock);

		FREE_NULL_LIST(job_resp_list);
	}
	exit(error_exit);
	return NULL;
}

void
ignore_signal(int signo)
{
	/* do nothing */
}

int
cleanup_allocation(void)
{
	slurm_allocation_msg_thr_destroy(msg_thr);
	return SLURM_SUCCESS;
}

extern list_t *existing_allocation(void)
{
	uint32_t old_job_id;
	list_t *job_resp_list = NULL;

	if (sropt.jobid == NO_VAL)
		return NULL;

	if (opt.clusters) {
		list_t *clusters = NULL;
		if (slurm_get_cluster_info(&(clusters), opt.clusters, 0)) {
			print_db_notok(opt.clusters, 0);
			fatal("Could not get cluster information");
		}
		working_cluster_rec = list_peek(clusters);
		debug2("Looking for job %d on cluster %s (addr: %s)",
		       sropt.jobid,
		       working_cluster_rec->name,
		       working_cluster_rec->control_host);
	}

	old_job_id = (uint32_t) sropt.jobid;
	if (slurm_het_job_lookup(old_job_id, &job_resp_list) < 0) {
		if (sropt.parallel_debug)
			return NULL;    /* create new allocation as needed */
		if (errno == ESLURM_ALREADY_DONE)
			error("Slurm job %u has expired", old_job_id);
		else
			error("Unable to confirm allocation for job %u: %m",
			      old_job_id);
		info("Check SLURM_JOB_ID environment variable. Expired or invalid job %u",
		     old_job_id);
		exit(error_exit);
	}

	return job_resp_list;
}

/*
 * Create job description structure based off srun options
 * (see opt.h)
 */
static job_desc_msg_t *_job_desc_msg_create_from_opts(slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	job_desc_msg_t *j = slurm_opt_create_job_desc(opt_local, true);

	if (!j) {
		return NULL;
	}

	/*
	 * The controller rejects any non-stepmgr allocation requesting
	 * resv-ports. To allow srun to request --resv-ports outside of stepmgr
	 * jobs, clear resv_port_cnt when creating a non-stepmgr allocation.
	 */
	if ((opt_local->resv_port_cnt != NO_VAL) &&
	    !(opt_local->job_flags & STEPMGR_ENABLED) &&
	    !xstrstr(slurm_conf.slurmctld_params, "enable_stepmgr"))
		j->resv_port_cnt = NO_VAL16;

	xassert(srun_opt);

	if (!j->name)
		j->name = xstrdup(srun_opt->cmd_name);

	if (opt_local->argc > 0) {
		j->argc = opt_local->argc;
		j->argv = opt_local->argv;
	}

	j->container = xstrdup(opt_local->container);
	j->container_id = xstrdup(opt_local->container_id);
	j->container_type = xstrdup(opt_local->container_type);

	if (srun_opt->cpu_bind)
		j->cpu_bind = xstrdup(srun_opt->cpu_bind);
	if (srun_opt->cpu_bind_type)
		j->cpu_bind_type = srun_opt->cpu_bind_type;

	if (!j->x11 && opt.x11) {
		j->x11_magic_cookie = xstrdup(opt.x11_magic_cookie);
		j->x11_target = xstrdup(opt.x11_target);
		j->x11_target_port = opt.x11_target_port;
	}

	j->wait_all_nodes = 1;

	return j;
}

void
job_desc_msg_destroy(job_desc_msg_t *j)
{
	if (j) {
		xfree(j->req_nodes);
		xfree(j);
	}
}
