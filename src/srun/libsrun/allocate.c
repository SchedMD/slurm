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
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_time.h"
#include "src/common/tres_bind.h"
#include "src/common/tres_frequency.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "allocate.h"
#include "opt.h"
#include "launch.h"

#if defined HAVE_ALPS_CRAY && defined HAVE_REAL_CRAY
/*
 * On Cray installations, the libjob headers are not automatically installed
 * by default, while libjob.so always is, and kernels are > 2.6. Hence it is
 * simpler to just duplicate the single declaration here.
 */
extern uint64_t job_getjid(pid_t pid);
#endif

#define MAX_ALLOC_WAIT	60	/* seconds */
#define MIN_ALLOC_WAIT	5	/* seconds */
#define MAX_RETRIES	10
#define POLL_SLEEP	0.1	/* retry interval in seconds  */

pthread_mutex_t msg_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t msg_cond = PTHREAD_COND_INITIALIZER;
allocation_msg_thread_t *msg_thr = NULL;
struct pollfd global_fds[1];

extern char **environ;

static uint32_t pending_job_id = 0;

/*
 * Static Prototypes
 */
static job_desc_msg_t *_job_desc_msg_create_from_opts(slurm_opt_t *opt_local);
static void _set_pending_job_id(uint32_t job_id);
static void _signal_while_allocating(int signo);
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc);

static sig_atomic_t destroy_job = 0;

static void _set_pending_job_id(uint32_t job_id)
{
	debug2("Pending job allocation %u", job_id);
	pending_job_id = job_id;
}

static void *_safe_signal_while_allocating(void *in_data)
{
	int signo = *(int *)in_data;

	debug("Got signal %d", signo);
	xfree(in_data);
	if (signo == SIGCONT)
		return NULL;

	destroy_job = 1;
	if (pending_job_id != 0) {
		info("Job allocation %u has been revoked", pending_job_id);
		slurm_complete_job(pending_job_id, NO_VAL);
		destroy_job = 1;
	}

	return NULL;
}

static void _signal_while_allocating(int signo)
{
	int *local_signal;

	/*
	 * There are places where _signal_while_allocating() can't be
	 * put into a thread, but if this isn't on a separate thread
	 * and we try to print something using the log functions and
	 * it just so happens to be in a poll or something we can get
	 * deadlock. So after the signal happens we are able to spawn
	 * a thread here and avoid the deadlock.
	 *
	 * SO, DON'T PRINT ANYTHING IN THIS FUNCTION.
	 */
	local_signal = xmalloc(sizeof(int));
	*local_signal = signo;
	slurm_thread_create_detached(NULL, _safe_signal_while_allocating,
				     local_signal);
}

/* This typically signifies the job was cancelled by scancel */
static void _job_complete_handler(srun_job_complete_msg_t *msg)
{
	if (pending_job_id && (pending_job_id != msg->job_id)) {
		error("Ignoring job_complete for job %u because our job ID is %u",
		      msg->job_id, pending_job_id);
		return;
	}

	if (msg->step_id == NO_VAL)
		info("Force Terminated job %u", msg->job_id);
	else
		info("Force Terminated job %u.%u", msg->job_id, msg->step_id);
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

static void _ping_handler(srun_ping_msg_t *msg)
{
	/* the api will respond so there really isn't anything to do
	   here */
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
		   ((errno == ETIMEDOUT) || (errno == ESLURM_NODES_BUSY))) {
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
	int is_ready = 0, i, rc;
	double cur_delay = 0;
	double cur_sleep = 0;
	int suspend_time, resume_time, max_delay;
	bool job_killed = false;

	suspend_time = slurm_get_suspend_timeout();
	resume_time  = slurm_get_resume_timeout();
	if ((suspend_time == 0) || (resume_time == 0))
		return 1;	/* Power save mode disabled */
	max_delay = suspend_time + resume_time;
	max_delay *= 5;		/* Allow for ResumeRate support */

	pending_job_id = alloc->job_id;

	for (i = 0; cur_delay < max_delay; i++) {
		if (i) {
			cur_sleep = POLL_SLEEP * i;
			if (i == 1) {
				verbose("Waiting for nodes to boot (delay looping %d times @ %f secs x index)",
					max_delay, POLL_SLEEP);
			} else {
				debug("Waited %f sec and still waiting: next sleep for %f sec",
				      cur_delay, cur_sleep);
			}
			usleep(1000000 * cur_sleep);
			cur_delay += cur_sleep;
		}

		rc = slurm_job_node_ready(alloc->job_id);
		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0) {	/* job killed */
			job_killed = true;
			break;
		}
		if (rc & READY_NODE_STATE) {		/* job and node ready */
			is_ready = 1;
			break;
		}
		if (destroy_job)
			break;
	}
	if (is_ready) {
		resource_allocation_response_msg_t *resp;
		char *tmp_str;
		if (i > 0)
     			verbose("Nodes %s are ready for job", alloc->node_list);
		if (alloc->alias_list && !xstrcmp(alloc->alias_list, "TBD") &&
		    (slurm_allocation_lookup(pending_job_id, &resp)
		     == SLURM_SUCCESS)) {
			tmp_str = alloc->alias_list;
			alloc->alias_list = resp->alias_list;
			resp->alias_list = tmp_str;
			slurm_free_resource_allocation_response_msg(resp);
		}
	} else if (!destroy_job) {
		if (job_killed) {
			error("Job allocation %u has been revoked",
			      alloc->job_id);
			destroy_job = true;
		} else
			error("Nodes %s are still not ready", alloc->node_list);
	} else	/* allocation_interrupted and slurmctld not responing */
		is_ready = 0;

	pending_job_id = 0;

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
	ListIterator iter;
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
extern resource_allocation_response_msg_t *
	allocate_nodes(bool handle_signals, slurm_opt_t *opt_local)

{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	resource_allocation_response_msg_t *resp = NULL;
	job_desc_msg_t *j;
	slurm_allocation_callbacks_t callbacks;
	int i;

	xassert(srun_opt);

	if (srun_opt->relative_set && srun_opt->relative)
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

	j->origin_cluster = xstrdup(slurmctld_conf.cluster_name);

	/* Do not re-use existing job id when submitting new job
	 * from within a running job */
	if ((j->job_id != NO_VAL) && !opt_local->jobid_set) {
		info("WARNING: Creating Slurm job allocation from within "
		     "another allocation");
		info("WARNING: You are attempting to initiate a second job");
		if (!opt_local->jobid_set)	/* Let slurmctld set jobid */
			j->job_id = NO_VAL;
	}
	callbacks.ping = _ping_handler;
	callbacks.timeout = _timeout_handler;
	callbacks.job_complete = _job_complete_handler;
	callbacks.job_suspend = NULL;
	callbacks.user_msg = _user_msg_handler;
	callbacks.node_fail = _node_fail_handler;

	/* create message thread to handle pings and such from slurmctld */
	msg_thr = slurm_allocation_msg_thr_create(&j->other_port, &callbacks);

	/* NOTE: Do not process signals in separate pthread. The signal will
	 * cause slurm_allocate_resources_blocking() to exit immediately. */
	if (handle_signals) {
		xsignal_unblock(sig_array);
		for (i = 0; sig_array[i]; i++)
			xsignal(sig_array[i], _signal_while_allocating);
	}

	while (!resp) {
		resp = slurm_allocate_resources_blocking(j,
							 opt_local->immediate,
							 _set_pending_job_id);
		if (destroy_job) {
			/* cancelled by signal */
			break;
		} else if (!resp && !_retry()) {
			break;
		}
	}

	if (resp)
		print_multi_line_string(resp->job_submit_user_msg, -1);

	if (resp && !destroy_job) {
		/*
		 * Allocation granted!
		 */
		pending_job_id = resp->job_id;

		/*
		 * These values could be changed while the job was
		 * pending so overwrite the request with what was
		 * allocated so we don't have issues when we use them
		 * in the step creation.
		 */
		opt_local->pn_min_memory = NO_VAL64;
		opt_local->mem_per_cpu   = NO_VAL64;
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

		if (resp->working_cluster_rec)
			slurm_setup_remote_working_cluster(resp);

		if (!_wait_nodes_ready(resp)) {
			if (!destroy_job)
				error("Something is wrong with the boot of the nodes.");
			goto relinquish;
		}
	} else if (destroy_job) {
		goto relinquish;
	}

	if (handle_signals)
		xsignal_block(sig_array);

	job_desc_msg_destroy(j);

	return resp;

relinquish:
	if (resp) {
		if (!destroy_job)
			slurm_complete_job(resp->job_id, 1);
		slurm_free_resource_allocation_response_msg(resp);
	}
	exit(error_exit);
	return NULL;
}

/*
 * Allocate nodes for heterogeneous/pack job from the slurm controller -- 
 * retrying the attempt if the controller appears to be down, and optionally
 * waiting for resources if none are currently available (see opt.immediate)
 *
 * Returns a pointer to a resource_allocation_response_msg which must
 * be freed with slurm_free_resource_allocation_response_msg()
 */
List allocate_pack_nodes(bool handle_signals)
{
	resource_allocation_response_msg_t *resp = NULL;
	bool jobid_log = true;
	job_desc_msg_t *j, *first_job = NULL;
	slurm_allocation_callbacks_t callbacks;
	ListIterator opt_iter, resp_iter;
	slurm_opt_t *opt_local, *first_opt = NULL;
	List job_req_list = NULL, job_resp_list = NULL;
	uint32_t my_job_id = 0;
	int i, k;

	job_req_list = list_create(NULL);
	opt_iter = list_iterator_create(opt_list);
	while ((opt_local = list_next(opt_iter))) {
		srun_opt_t *srun_opt = opt_local->srun_opt;
		xassert(srun_opt);
		if (!first_opt)
			first_opt = opt_local;
		if (srun_opt->relative_set && srun_opt->relative)
			fatal("--relative option invalid for job allocation request");

		if ((j = _job_desc_msg_create_from_opts(opt_local)) == NULL)
			return NULL;
		if (!first_job)
			first_job = j;

		j->origin_cluster = xstrdup(slurmctld_conf.cluster_name);

		/* Do not re-use existing job id when submitting new job
		 * from within a running job */
		if ((j->job_id != NO_VAL) && !opt_local->jobid_set) {
			if (jobid_log) {
				jobid_log = false;	/* log once */
				info("WARNING: Creating Slurm job allocation from within "
				     "another allocation");
				info("WARNING: You are attempting to initiate a second job");
			}
			if (!opt_local->jobid_set) /* Let slurmctld set jobid */
				j->job_id = NO_VAL;
		}

		list_append(job_req_list, j);
	}
	list_iterator_destroy(opt_iter);

	if (!first_job) {
		error("%s: No job requests found", __func__);
		return NULL;
	}

	if (first_opt && first_opt->clusters &&
	    (slurmdb_get_first_pack_cluster(job_req_list, first_opt->clusters,
					    &working_cluster_rec)
	     != SLURM_SUCCESS)) {
		print_db_notok(first_opt->clusters, 0);
		return NULL;
	}

	callbacks.ping = _ping_handler;
	callbacks.timeout = _timeout_handler;
	callbacks.job_complete = _job_complete_handler;
	callbacks.job_suspend = NULL;
	callbacks.user_msg = _user_msg_handler;
	callbacks.node_fail = _node_fail_handler;

	/* create message thread to handle pings and such from slurmctld */
	msg_thr = slurm_allocation_msg_thr_create(&first_job->other_port,
						  &callbacks);

	/* NOTE: Do not process signals in separate pthread. The signal will
	 * cause slurm_allocate_resources_blocking() to exit immediately. */
	if (handle_signals) {
		xsignal_unblock(sig_array);
		for (i = 0; sig_array[i]; i++)
			xsignal(sig_array[i], _signal_while_allocating);
	}

	while (first_opt && !job_resp_list) {
		job_resp_list = slurm_allocate_pack_job_blocking(job_req_list,
				 first_opt->immediate, _set_pending_job_id);
		if (destroy_job) {
			/* cancelled by signal */
			break;
		} else if (!job_resp_list && !_retry()) {
			break;
		}
	}

	if (job_resp_list && !destroy_job) {
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

			if (pending_job_id == 0)
				pending_job_id = resp->job_id;
			if (my_job_id == 0) {
				my_job_id = resp->job_id;
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
			 *
			 * NOTE: pn_min_memory here is an int64, not uint64.
			 * These operations may have some bizarre side effects
			 */
			if (opt_local->pn_min_memory != NO_VAL64)
				opt_local->pn_min_memory =
					(resp->pn_min_memory & (~MEM_PER_CPU));
			else if (opt_local->mem_per_cpu != NO_VAL64)
				opt_local->mem_per_cpu =
					(resp->pn_min_memory & (~MEM_PER_CPU));

			opt_local->min_nodes = resp->node_cnt;
			opt_local->max_nodes = resp->node_cnt;

			if (resp->working_cluster_rec)
				slurm_setup_remote_working_cluster(resp);

			if (!_wait_nodes_ready(resp)) {
				if (!destroy_job)
					error("Something is wrong with the "
					      "boot of the nodes.");
				goto relinquish;
			}
		}
		list_iterator_destroy(resp_iter);
		list_iterator_destroy(opt_iter);
	} else if (destroy_job) {
		goto relinquish;
	}

	if (handle_signals)
		xsignal_block(sig_array);

	return job_resp_list;

relinquish:
	if (job_resp_list) {
		if (!destroy_job && my_job_id)
			slurm_complete_job(my_job_id, 1);
		list_destroy(job_resp_list);
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

extern List existing_allocation(void)
{
	uint32_t old_job_id;
	List job_resp_list = NULL;

	if (opt.jobid == NO_VAL)
		return NULL;

	old_job_id = (uint32_t) opt.jobid;
	if (slurm_pack_job_lookup(old_job_id, &job_resp_list) < 0) {
		if (opt.srun_opt->parallel_debug || opt.jobid_set)
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

/* Set up port to handle messages from slurmctld */
int slurmctld_msg_init(void)
{
	slurm_addr_t slurm_address;
	uint16_t port;
	static int slurmctld_fd = -1;
	uint16_t *ports;

	if (slurmctld_fd >= 0)	/* May set early for queued job allocation */
		return slurmctld_fd;

	slurmctld_comm_addr.port = 0;

	if ((ports = slurm_get_srun_port_range()))
		slurmctld_fd = slurm_init_msg_engine_ports(ports);
	else
		slurmctld_fd = slurm_init_msg_engine_port(0);

	if (slurmctld_fd < 0) {
		error("slurm_init_msg_engine_port error %m");
		exit(error_exit);
	}

	if (slurm_get_stream_addr(slurmctld_fd, &slurm_address) < 0) {
		error("slurm_get_stream_addr error %m");
		exit(error_exit);
	}
	fd_set_nonblocking(slurmctld_fd);
	/* hostname is not set,  so slurm_get_addr fails
	   slurm_get_addr(&slurm_address, &port, hostname, sizeof(hostname)); */
	port = ntohs(slurm_address.sin_port);
	slurmctld_comm_addr.port     = port;
	debug2("srun PMI messages to port=%u", slurmctld_comm_addr.port);

	return slurmctld_fd;
}

/*
 * Create job description structure based off srun options
 * (see opt.h)
 */
static job_desc_msg_t *_job_desc_msg_create_from_opts(slurm_opt_t *opt_local)
{
	srun_opt_t *srun_opt = opt_local->srun_opt;
	job_desc_msg_t *j = xmalloc(sizeof(*j));
	hostlist_t hl = NULL;
	xassert(srun_opt);

	slurm_init_job_desc_msg(j);
#if defined HAVE_ALPS_CRAY && defined HAVE_REAL_CRAY
	static bool sgi_err_logged = false;
	uint64_t pagg_id = job_getjid(getpid());
	/*
	 * Interactive sessions require pam_job.so in /etc/pam.d/common-session
	 * since creating sgi_job containers requires root permissions. This is
	 * the only exception where we allow the fallback of using the SID to
	 * confirm the reservation (caught later, in do_basil_confirm).
	 */
	if (pagg_id != (uint64_t) -1) {
		if (!j->select_jobinfo)
			j->select_jobinfo = select_g_select_jobinfo_alloc();

		select_g_select_jobinfo_set(j->select_jobinfo,
					    SELECT_JOBDATA_PAGG_ID, &pagg_id);
	} else if (!sgi_err_logged) {
		error("No SGI job container ID detected - please enable the "
		      "Cray job service via /etc/init.d/job");
		sgi_err_logged = true;
	}
#endif

	j->contiguous     = opt_local->contiguous;
	if (opt_local->core_spec != NO_VAL16)
		j->core_spec      = opt_local->core_spec;
	j->features       = opt_local->constraints;
	j->cluster_features = opt_local->c_constraints;
	if (opt_local->immediate == 1)
		j->immediate = opt_local->immediate;
	if (opt_local->job_name)
		j->name   = opt_local->job_name;
	else
		j->name = srun_opt->cmd_name;
	if (srun_opt->argc > 0) {
		j->argc    = 1;
		j->argv    = (char **) xmalloc(sizeof(char *) * 2);
		j->argv[0] = xstrdup(srun_opt->argv[0]);
	}
	if (opt_local->acctg_freq)
		j->acctg_freq     = xstrdup(opt_local->acctg_freq);
	j->reservation    = opt_local->reservation;
	j->wckey          = opt_local->wckey;
	j->x11 = opt.x11;
	if (j->x11) {
		j->x11_magic_cookie = xstrdup(opt.x11_magic_cookie);
		j->x11_target_port = opt.x11_target_port;
	}

	j->req_nodes      = xstrdup(opt_local->nodelist);

	/* simplify the job allocation nodelist,
	 * not laying out tasks until step */
	if (j->req_nodes) {
		hl = hostlist_create(j->req_nodes);
		xfree(opt_local->nodelist);
		opt_local->nodelist = hostlist_ranged_string_xmalloc(hl);
		hostlist_uniq(hl);
		xfree(j->req_nodes);
		j->req_nodes = hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);

	}

	if (((opt_local->distribution & SLURM_DIST_STATE_BASE) ==
	     SLURM_DIST_ARBITRARY) && !j->req_nodes) {
		error("With Arbitrary distribution you need to "
		      "specify a nodelist or hostfile with the -w option");
		return NULL;
	}
	j->extra = opt_local->extra;
	j->exc_nodes      = opt_local->exc_nodes;
	j->partition      = opt_local->partition;
	j->min_nodes      = opt_local->min_nodes;
	if (opt_local->sockets_per_node != NO_VAL)
		j->sockets_per_node    = opt_local->sockets_per_node;
	if (opt_local->cores_per_socket != NO_VAL)
		j->cores_per_socket      = opt_local->cores_per_socket;
	if (opt_local->threads_per_core != NO_VAL) {
		j->threads_per_core    = opt_local->threads_per_core;
		/* if 1 always make sure affinity knows about it */
		if (j->threads_per_core == 1)
			srun_opt->cpu_bind_type |= CPU_BIND_ONE_THREAD_PER_CORE;
	}
	j->user_id        = opt_local->uid;
	j->dependency     = opt_local->dependency;
	if (opt_local->nice != NO_VAL)
		j->nice   = NICE_OFFSET + opt_local->nice;
	if (opt_local->priority)
		j->priority = opt_local->priority;
	if (srun_opt->cpu_bind)
		j->cpu_bind = srun_opt->cpu_bind;
	if (srun_opt->cpu_bind_type)
		j->cpu_bind_type = srun_opt->cpu_bind_type;
	if (opt_local->delay_boot != NO_VAL)
		j->delay_boot = opt_local->delay_boot;
	if (opt_local->mem_bind)
		j->mem_bind       = opt_local->mem_bind;
	if (opt_local->mem_bind_type)
		j->mem_bind_type  = opt_local->mem_bind_type;
	if (opt_local->plane_size != NO_VAL)
		j->plane_size     = opt_local->plane_size;
	j->task_dist      = opt_local->distribution;

	j->group_id       = opt_local->gid;
	j->mail_type      = opt_local->mail_type;

	if (opt_local->ntasks_per_node != NO_VAL)
		j->ntasks_per_node   = opt_local->ntasks_per_node;
	if (opt_local->ntasks_per_socket != NO_VAL)
		j->ntasks_per_socket = opt_local->ntasks_per_socket;
	if (opt_local->ntasks_per_core != NO_VAL)
		j->ntasks_per_core   = opt_local->ntasks_per_core;

	if (opt_local->mail_user)
		j->mail_user = opt_local->mail_user;
	if (opt_local->burst_buffer)
		j->burst_buffer = opt_local->burst_buffer;
	if (opt_local->begin)
		j->begin_time = opt_local->begin;
	if (opt_local->deadline)
		j->deadline = opt_local->deadline;
	if (opt_local->licenses)
		j->licenses = opt_local->licenses;
	if (opt_local->network)
		j->network = opt_local->network;
	if (opt_local->profile)
		j->profile = opt_local->profile;
	if (opt_local->account)
		j->account = opt_local->account;
	if (opt_local->comment)
		j->comment = opt_local->comment;
	if (opt_local->qos)
		j->qos = opt_local->qos;
	if (opt_local->cwd)
		j->work_dir = opt_local->cwd;

	if (opt_local->hold)
		j->priority     = 0;
	if (opt_local->jobid != NO_VAL)
		j->job_id	= opt_local->jobid;
	if (opt_local->reboot)
		j->reboot = 1;

	if (opt_local->max_nodes)
		j->max_nodes    = opt_local->max_nodes;
	else if (opt_local->nodes_set) {
		/* On an allocation if the max nodes isn't set set it
		 * to do the same behavior as with salloc or sbatch.
		 */
		j->max_nodes    = opt_local->min_nodes;
	}
	if (opt_local->pn_min_cpus != NO_VAL)
		j->pn_min_cpus = opt_local->pn_min_cpus;
	if (opt_local->pn_min_memory != NO_VAL64)
		j->pn_min_memory = opt_local->pn_min_memory;
	else if (opt_local->mem_per_cpu != NO_VAL64)
		j->pn_min_memory = opt_local->mem_per_cpu | MEM_PER_CPU;
	if (opt_local->pn_min_tmp_disk != NO_VAL)
		j->pn_min_tmp_disk = opt_local->pn_min_tmp_disk;
	if (opt_local->overcommit) {
		j->min_cpus    = opt_local->min_nodes;
		j->overcommit  = opt_local->overcommit;
	} else if (opt_local->cpus_set)
		j->min_cpus    = opt_local->ntasks * opt_local->cpus_per_task;
	else
		j->min_cpus    = opt_local->ntasks;
	if (opt_local->ntasks_set)
		j->num_tasks   = opt_local->ntasks;

	if (opt_local->cpus_set)
		j->cpus_per_task = opt_local->cpus_per_task;

	if (opt_local->no_kill)
		j->kill_on_node_fail   = 0;
	if (opt_local->time_limit != NO_VAL)
		j->time_limit          = opt_local->time_limit;
	if (opt_local->time_min != NO_VAL)
		j->time_min            = opt_local->time_min;
	if (opt_local->shared != NO_VAL16)
		j->shared = opt_local->shared;

	if (opt_local->warn_signal)
		j->warn_signal = opt_local->warn_signal;
	if (opt_local->warn_time)
		j->warn_time = opt_local->warn_time;
	if (opt_local->job_flags)
		j->bitflags = opt_local->job_flags;

	if (opt_local->cpu_freq_min != NO_VAL)
		j->cpu_freq_min = opt_local->cpu_freq_min;
	if (opt_local->cpu_freq_max != NO_VAL)
		j->cpu_freq_max = opt_local->cpu_freq_max;
	if (opt_local->cpu_freq_gov != NO_VAL)
		j->cpu_freq_gov = opt_local->cpu_freq_gov;

	if (opt_local->req_switch >= 0)
		j->req_switch = opt_local->req_switch;
	if (opt_local->wait4switch >= 0)
		j->wait4switch = opt_local->wait4switch;

	/* srun uses the same listening port for the allocation response
	 * message as all other messages */
	j->alloc_resp_port = slurmctld_comm_addr.port;
	j->other_port = slurmctld_comm_addr.port;

	if (opt_local->spank_job_env_size) {
		j->spank_job_env      = opt_local->spank_job_env;
		j->spank_job_env_size = opt_local->spank_job_env_size;
	}

	if (opt_local->power_flags)
		j->power_flags = opt_local->power_flags;
	if (opt_local->mcs_label)
		j->mcs_label = opt_local->mcs_label;
	j->wait_all_nodes = 1;

	/* If can run on multiple clusters find the earliest run time
	 * and run it there */
	j->clusters = xstrdup(opt_local->clusters);

	if (opt.cpus_per_gpu)
		xstrfmtcat(j->cpus_per_tres, "gpu:%d", opt.cpus_per_gpu);
	if (opt.gpu_bind)
		xstrfmtcat(opt.tres_bind, "gpu:%s", opt.gpu_bind);
	if (tres_bind_verify_cmdline(opt.tres_bind)) {
		if (tres_bind_err_log) {	/* Log once */
			error("Invalid --tres-bind argument: %s. Ignored",
			      opt.tres_bind);
			tres_bind_err_log = false;
		}
		xfree(opt.tres_bind);
	}
	j->tres_bind = xstrdup(opt.tres_bind);
	xfmt_tres(&opt.tres_freq, "gpu", opt.gpu_freq);
	if (tres_freq_verify_cmdline(opt.tres_freq)) {
		if (tres_freq_err_log) {	/* Log once */
			error("Invalid --tres-freq argument: %s. Ignored",
			      opt.tres_freq);
			tres_freq_err_log = false;
		}
		xfree(opt.tres_freq);
	}
	j->tres_freq = xstrdup(opt.tres_freq);
	xfmt_tres(&j->tres_per_job,    "gpu", opt.gpus);
	xfmt_tres(&j->tres_per_node,   "gpu", opt.gpus_per_node);
	if (opt_local->gres && xstrcasecmp(opt_local->gres, "NONE")) {
		if (j->tres_per_node)
			xstrfmtcat(j->tres_per_node, ",%s", opt_local->gres);
		else
			j->tres_per_node = xstrdup(opt_local->gres);
	}
	xfmt_tres(&j->tres_per_socket, "gpu", opt.gpus_per_socket);
	xfmt_tres(&j->tres_per_task,   "gpu", opt.gpus_per_task);
	if (opt.mem_per_gpu)
		xstrfmtcat(j->mem_per_tres, "gpu:%"PRIi64, opt.mem_per_gpu);

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

extern int create_job_step(srun_job_t *job, bool use_all_cpus,
			   slurm_opt_t *opt_local)
{
	return launch_g_create_job_step(job, use_all_cpus,
					_signal_while_allocating,
					&destroy_job, opt_local);
}
