/*****************************************************************************\
 *  src/srun/allocate.c - srun functions for managing node allocations
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <pwd.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/forward.h"
#include "src/common/env.h"
#include "src/common/fd.h"

#include "allocate.h"
#include "opt.h"
#include "launch.h"

#ifdef HAVE_BG
#include "src/common/node_select.h"
#include "src/plugins/select/bluegene/bg_enums.h"
#endif

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
#define POLL_SLEEP	3	/* retry interval in seconds  */

pthread_mutex_t msg_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t msg_cond = PTHREAD_COND_INITIALIZER;
allocation_msg_thread_t *msg_thr = NULL;
struct pollfd global_fds[1];

extern char **environ;

static uint32_t pending_job_id = 0;

/*
 * Static Prototypes
 */
static void _set_pending_job_id(uint32_t job_id);
static void _signal_while_allocating(int signo);

#ifdef HAVE_BG
static int _wait_bluegene_block_ready(
	resource_allocation_response_msg_t *alloc);
static int _blocks_dealloc(void);
#else
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc);
#endif

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
	if (signo == SIGCONT)
		return NULL;

	destroy_job = 1;
	if (pending_job_id != 0) {
		info("Job allocation %u has been revoked", pending_job_id);
		slurm_complete_job(pending_job_id, NO_VAL);
	}

	return NULL;
}

static void _signal_while_allocating(int signo)
{
	pthread_t thread_id;
	pthread_attr_t thread_attr;

	/* There are places where _signal_while_allocating can't be
	 * put into a thread, but if this isn't on a separate thread
	 * and we try to print something using the log functions and
	 * it just so happens to be in a poll or something we can get
	 * deadlock. So after the signal happens we are able to spawn
	 * a thread here and avoid the deadlock.
	 *
	 * SO, DON'T PRINT ANYTHING IN THIS FUNCTION.
	 */

	slurm_attr_init(&thread_attr);
	pthread_create(&thread_id, &thread_attr,
		       _safe_signal_while_allocating,
		       (void *)&signo);
	slurm_attr_destroy(&thread_attr);
}

/* This typically signifies the job was cancelled by scancel */
static void _job_complete_handler(srun_job_complete_msg_t *msg)
{
	if (pending_job_id && (pending_job_id != msg->job_id)) {
		error("Ignoring bogus job_complete call: job %u is not "
		      "job %u", pending_job_id, msg->job_id);
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
			slurm_ctime(&msg->timeout));
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

#ifdef HAVE_BG
/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_bluegene_block_ready(resource_allocation_response_msg_t *alloc)
{
	int is_ready = 0, i, rc;
	char *block_id = NULL;
	int cur_delay = 0;
	int max_delay = BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
		(BG_INCR_BLOCK_BOOT * alloc->node_cnt);

	select_g_select_jobinfo_get(alloc->select_jobinfo,
				    SELECT_JOBDATA_BLOCK_ID,
				    &block_id);

	for (i=0; (cur_delay < max_delay); i++) {
		if (i == 1)
			debug("Waiting for block %s to become ready for job",
			      block_id);
		if (i) {
			sleep(POLL_SLEEP);
			rc = _blocks_dealloc();
			if ((rc == 0) || (rc == -1))
				cur_delay += POLL_SLEEP;
			debug2("still waiting");
		}

		rc = slurm_job_node_ready(alloc->job_id);

		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0)	/* job killed */
			break;
		if (rc & READY_NODE_STATE) {		/* job and node ready */
			is_ready = 1;
			break;
		}
		if (destroy_job)
			break;
	}
	if (is_ready)
     		debug("Block %s is ready for job", block_id);
	else if (!destroy_job)
		error("Block %s still not ready", block_id);
	else	/* destroy_job set and slurmctld not responing */
		is_ready = 0;

	xfree(block_id);

	return is_ready;
}

/*
 * Test if any BG blocks are in deallocating state since they are
 * probably related to this job we will want to sleep longer
 * RET	1:  deallocate in progress
 *	0:  no deallocate in progress
 *     -1: error occurred
 */
static int _blocks_dealloc(void)
{
	static block_info_msg_t *bg_info_ptr = NULL, *new_bg_ptr = NULL;
	int rc = 0, error_code = 0, i;

	if (bg_info_ptr) {
		error_code = slurm_load_block_info(bg_info_ptr->last_update,
						   &new_bg_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_block_info_msg(bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_block_info((time_t) NULL,
						   &new_bg_ptr, SHOW_ALL);
	}

	if (error_code) {
		error("slurm_load_partitions: %s",
		      slurm_strerror(slurm_get_errno()));
		return -1;
	}
	for (i=0; i<new_bg_ptr->record_count; i++) {
		if (new_bg_ptr->block_array[i].state == BG_BLOCK_TERM) {
			rc = 1;
			break;
		}
	}
	bg_info_ptr = new_bg_ptr;
	return rc;
}
#else
/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_nodes_ready(resource_allocation_response_msg_t *alloc)
{
	int is_ready = 0, i, rc;
	int cur_delay = 0;
	int suspend_time, resume_time, max_delay;

	suspend_time = slurm_get_suspend_timeout();
	resume_time  = slurm_get_resume_timeout();
	if ((suspend_time == 0) || (resume_time == 0))
		return 1;	/* Power save mode disabled */
	max_delay = suspend_time + resume_time;
	max_delay *= 5;		/* Allow for ResumeRate support */

	pending_job_id = alloc->job_id;

	for (i = 0; (cur_delay < max_delay); i++) {
		if (i) {
			if (i == 1)
				verbose("Waiting for nodes to boot");
			else
				debug("still waiting");
			sleep(POLL_SLEEP);
			cur_delay += POLL_SLEEP;
		}

		rc = slurm_job_node_ready(alloc->job_id);

		if (rc == READY_JOB_FATAL)
			break;				/* fatal error */
		if ((rc == READY_JOB_ERROR) || (rc == EAGAIN))
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0)	/* job killed */
			break;
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
		if (alloc->alias_list && !strcmp(alloc->alias_list, "TBD") &&
		    (slurm_allocation_lookup_lite(pending_job_id, &resp)
		     == SLURM_SUCCESS)) {
			tmp_str = alloc->alias_list;
			alloc->alias_list = resp->alias_list;
			resp->alias_list = tmp_str;
			slurm_free_resource_allocation_response_msg(resp);
		}
	} else if (!destroy_job)
		error("Nodes %s are still not ready", alloc->node_list);
	else	/* allocation_interrupted and slurmctld not responing */
		is_ready = 0;

	pending_job_id = 0;

	return is_ready;
}
#endif	/* HAVE_BG */

int
allocate_test(void)
{
	int rc;
	job_desc_msg_t *j = job_desc_msg_create_from_opts();
	if (!j)
		return SLURM_ERROR;

	rc = slurm_job_will_run(j);
	job_desc_msg_destroy(j);
	return rc;
}

resource_allocation_response_msg_t *
allocate_nodes(bool handle_signals)
{
	resource_allocation_response_msg_t *resp = NULL;
	job_desc_msg_t *j = job_desc_msg_create_from_opts();
	slurm_allocation_callbacks_t callbacks;
	int i;

	if (!j)
		return NULL;

	/* Do not re-use existing job id when submitting new job
	 * from within a running job */
	if ((j->job_id != NO_VAL) && !opt.jobid_set) {
		info("WARNING: Creating SLURM job allocation from within "
		     "another allocation");
		info("WARNING: You are attempting to initiate a second job");
		if (!opt.jobid_set)	/* Let slurmctld set jobid */
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
		resp = slurm_allocate_resources_blocking(j, opt.immediate,
							 _set_pending_job_id);
		if (destroy_job) {
			/* cancelled by signal */
			break;
		} else if (!resp && !_retry()) {
			break;
		}
	}

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
		if (opt.pn_min_memory != NO_VAL)
			opt.pn_min_memory = (resp->pn_min_memory &
					     (~MEM_PER_CPU));
		else if (opt.mem_per_cpu != NO_VAL)
			opt.mem_per_cpu = (resp->pn_min_memory &
					   (~MEM_PER_CPU));
		/*
		 * FIXME: timelimit should probably also be updated
		 * here since it could also change.
		 */

#ifdef HAVE_BG
		uint32_t node_cnt = 0;
		select_g_select_jobinfo_get(resp->select_jobinfo,
					    SELECT_JOBDATA_NODE_CNT,
					    &node_cnt);
		if ((node_cnt == 0) || (node_cnt == NO_VAL)) {
			opt.min_nodes = node_cnt;
			opt.max_nodes = node_cnt;
		} /* else we just use the original request */

		if (!_wait_bluegene_block_ready(resp)) {
			if (!destroy_job)
				error("Something is wrong with the "
				      "boot of the block.");
			goto relinquish;
		}
#else
		opt.min_nodes = resp->node_cnt;
		opt.max_nodes = resp->node_cnt;

		if (!_wait_nodes_ready(resp)) {
			if (!destroy_job)
				error("Something is wrong with the "
				      "boot of the nodes.");
			goto relinquish;
		}
#endif
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

resource_allocation_response_msg_t *
existing_allocation(void)
{
	uint32_t old_job_id;
        resource_allocation_response_msg_t *resp = NULL;

	if (opt.jobid != NO_VAL)
		old_job_id = (uint32_t)opt.jobid;
	else
                return NULL;

        if (slurm_allocation_lookup_lite(old_job_id, &resp) < 0) {
                if (opt.parallel_debug || opt.jobid_set)
                        return NULL;    /* create new allocation as needed */
                if (errno == ESLURM_ALREADY_DONE)
                        error ("SLURM job %u has expired.", old_job_id);
                else
                        error ("Unable to confirm allocation for job %u: %m",
			       old_job_id);
                info ("Check SLURM_JOB_ID environment variable "
                      "for expired or invalid job.");
                exit(error_exit);
        }

        return resp;
}

/* Set up port to handle messages from slurmctld */
slurm_fd_t
slurmctld_msg_init(void)
{
	slurm_addr_t slurm_address;
	uint16_t port;
	static slurm_fd_t slurmctld_fd   = (slurm_fd_t) 0;
	uint16_t *ports;

	if (slurmctld_fd)	/* May set early for queued job allocation */
		return slurmctld_fd;

	slurmctld_fd = -1;
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
job_desc_msg_t *
job_desc_msg_create_from_opts (void)
{
	job_desc_msg_t *j = xmalloc(sizeof(*j));
	hostlist_t hl = NULL;

	slurm_init_job_desc_msg(j);
#if defined HAVE_ALPS_CRAY && defined HAVE_REAL_CRAY
	uint64_t pagg_id = job_getjid(getpid());
	/*
	 * Interactive sessions require pam_job.so in /etc/pam.d/common-session
	 * since creating sgi_job containers requires root permissions. This is
	 * the only exception where we allow the fallback of using the SID to
	 * confirm the reservation (caught later, in do_basil_confirm).
	 */
	if (pagg_id == (uint64_t)-1) {
		error("No SGI job container ID detected - please enable the "
		      "Cray job service via /etc/init.d/job");
	} else {
		if (!j->select_jobinfo)
			j->select_jobinfo = select_g_select_jobinfo_alloc();

		select_g_select_jobinfo_set(j->select_jobinfo,
					    SELECT_JOBDATA_PAGG_ID, &pagg_id);
	}
#endif

	j->contiguous     = opt.contiguous;
	if (opt.core_spec != (uint16_t) NO_VAL)
		j->core_spec      = opt.core_spec;
	j->features       = opt.constraints;
	if (opt.gres && strcasecmp(opt.gres, "NONE"))
		j->gres   = opt.gres;
	if (opt.immediate == 1)
		j->immediate = opt.immediate;
	if (opt.job_name)
		j->name   = opt.job_name;
	else
		j->name   = opt.cmd_name;
	if (opt.argc > 0) {
		j->argc    = 1;
		j->argv    = (char **) xmalloc(sizeof(char *) * 2);
		j->argv[0] = xstrdup(opt.argv[0]);
	}
	if (opt.acctg_freq)
		j->acctg_freq     = xstrdup(opt.acctg_freq);
	j->reservation    = opt.reservation;
	j->wckey          = opt.wckey;

	j->req_nodes      = xstrdup(opt.nodelist);

	/* simplify the job allocation nodelist,
	 * not laying out tasks until step */
	if (j->req_nodes) {
		hl = hostlist_create(j->req_nodes);
		xfree(opt.nodelist);
		opt.nodelist = hostlist_ranged_string_xmalloc(hl);
		hostlist_uniq(hl);
		xfree(j->req_nodes);
		j->req_nodes = hostlist_ranged_string_xmalloc(hl);
		hostlist_destroy(hl);

	}

	if (opt.distribution == SLURM_DIST_ARBITRARY
	   && !j->req_nodes) {
		error("With Arbitrary distribution you need to "
		      "specify a nodelist or hostfile with the -w option");
		return NULL;
	}
	j->exc_nodes      = opt.exc_nodes;
	j->partition      = opt.partition;
	j->min_nodes      = opt.min_nodes;
	if (opt.sockets_per_node != NO_VAL)
		j->sockets_per_node    = opt.sockets_per_node;
	if (opt.cores_per_socket != NO_VAL)
		j->cores_per_socket      = opt.cores_per_socket;
	if (opt.threads_per_core != NO_VAL) {
		j->threads_per_core    = opt.threads_per_core;
		/* if 1 always make sure affinity knows about it */
		if (j->threads_per_core == 1)
			opt.cpu_bind_type |= CPU_BIND_ONE_THREAD_PER_CORE;
	}
	j->user_id        = opt.uid;
	j->dependency     = opt.dependency;
	if (opt.nice)
		j->nice   = NICE_OFFSET + opt.nice;
	if (opt.priority)
		j->priority = opt.priority;

	if (opt.cpu_bind)
		j->cpu_bind       = opt.cpu_bind;
	if (opt.cpu_bind_type)
		j->cpu_bind_type  = opt.cpu_bind_type;
	if (opt.mem_bind)
		j->mem_bind       = opt.mem_bind;
	if (opt.mem_bind_type)
		j->mem_bind_type  = opt.mem_bind_type;
	if (opt.plane_size != NO_VAL)
		j->plane_size     = opt.plane_size;
	j->task_dist      = opt.distribution;

	j->group_id       = opt.gid;
	j->mail_type      = opt.mail_type;

	if (opt.ntasks_per_node != NO_VAL)
		j->ntasks_per_node   = opt.ntasks_per_node;
	if (opt.ntasks_per_socket != NO_VAL)
		j->ntasks_per_socket = opt.ntasks_per_socket;
	if (opt.ntasks_per_core != NO_VAL)
		j->ntasks_per_core   = opt.ntasks_per_core;

	if (opt.mail_user)
		j->mail_user = opt.mail_user;
	if (opt.begin)
		j->begin_time = opt.begin;
	if (opt.licenses)
		j->licenses = opt.licenses;
	if (opt.network)
		j->network = opt.network;
	if (opt.profile)
		j->profile = opt.profile;
	if (opt.account)
		j->account = opt.account;
	if (opt.comment)
		j->comment = opt.comment;
	if (opt.qos)
		j->qos = opt.qos;
	if (opt.cwd)
		j->work_dir = opt.cwd;

	if (opt.hold)
		j->priority     = 0;
	if (opt.jobid != NO_VAL)
		j->job_id	= opt.jobid;
#ifdef HAVE_BG
	if (opt.geometry[0] > 0) {
		int i;
		for (i = 0; i < SYSTEM_DIMENSIONS; i++)
			j->geometry[i] = opt.geometry[i];
	}
#endif

	memcpy(j->conn_type, opt.conn_type, sizeof(j->conn_type));

	if (opt.reboot)
		j->reboot = 1;
	if (opt.no_rotate)
		j->rotate = 0;

	if (opt.blrtsimage)
		j->blrtsimage = opt.blrtsimage;
	if (opt.linuximage)
		j->linuximage = opt.linuximage;
	if (opt.mloaderimage)
		j->mloaderimage = opt.mloaderimage;
	if (opt.ramdiskimage)
		j->ramdiskimage = opt.ramdiskimage;

	if (opt.max_nodes)
		j->max_nodes    = opt.max_nodes;
	else if (opt.nodes_set) {
		/* On an allocation if the max nodes isn't set set it
		 * to do the same behavior as with salloc or sbatch.
		 */
		j->max_nodes    = opt.min_nodes;
	}
	if (opt.pn_min_cpus != NO_VAL)
		j->pn_min_cpus    = opt.pn_min_cpus;
	if (opt.pn_min_memory != NO_VAL)
		j->pn_min_memory = opt.pn_min_memory;
	else if (opt.mem_per_cpu != NO_VAL)
		j->pn_min_memory = opt.mem_per_cpu | MEM_PER_CPU;
	if (opt.pn_min_tmp_disk != NO_VAL)
		j->pn_min_tmp_disk = opt.pn_min_tmp_disk;
	if (opt.overcommit) {
		j->min_cpus    = opt.min_nodes;
		j->overcommit  = opt.overcommit;
	} else if (opt.cpus_set)
		j->min_cpus    = opt.ntasks * opt.cpus_per_task;
	else
		j->min_cpus    = opt.ntasks;
	if (opt.ntasks_set)
		j->num_tasks   = opt.ntasks;

	if (opt.cpus_set)
		j->cpus_per_task = opt.cpus_per_task;

	if (opt.no_kill)
		j->kill_on_node_fail   = 0;
	if (opt.time_limit != NO_VAL)
		j->time_limit          = opt.time_limit;
	if (opt.time_min != NO_VAL)
		j->time_min            = opt.time_min;
	j->shared = opt.shared;

	if (opt.warn_signal)
		j->warn_signal = opt.warn_signal;
	if (opt.warn_time)
		j->warn_time = opt.warn_time;

	if (opt.req_switch >= 0)
		j->req_switch = opt.req_switch;
	if (opt.wait4switch >= 0)
		j->wait4switch = opt.wait4switch;

	/* srun uses the same listening port for the allocation response
	 * message as all other messages */
	j->alloc_resp_port = slurmctld_comm_addr.port;
	j->other_port = slurmctld_comm_addr.port;

	if (opt.spank_job_env_size) {
		j->spank_job_env      = opt.spank_job_env;
		j->spank_job_env_size = opt.spank_job_env_size;
	}

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

extern int
create_job_step(srun_job_t *job, bool use_all_cpus)
{
	return launch_g_create_job_step(job, use_all_cpus,
					_signal_while_allocating,
					&destroy_job);
}


