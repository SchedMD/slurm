/*****************************************************************************\
 * src/srun/allocate.c - srun functions for managing node allocations
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <sys/poll.h>

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#include "src/srun/allocate.h"
#include "src/srun/msg.h"
#include "src/srun/opt.h"
#include "src/srun/env.h"
#include "src/srun/attach.h"

#define MAX_ALLOC_WAIT 60	/* seconds */
#define MIN_ALLOC_WAIT  5	/* seconds */
#define MAX_RETRIES    10

/*
 * Static Prototypes
 */
static int   _accept_msg_connection(slurm_fd slurmctld_fd,
		resource_allocation_response_msg_t **resp);
static int   _handle_msg(slurm_msg_t *msg, \
		resource_allocation_response_msg_t **resp);
static int   _wait_for_alloc_rpc(int sleep_time,
		resource_allocation_response_msg_t **resp);
static void  _wait_for_resources(resource_allocation_response_msg_t **resp);
static bool  _retry();
static void  _intr_handler(int signo);

static job_step_create_request_msg_t * _step_req_create(job_t *j);
static void _step_req_destroy(job_step_create_request_msg_t *r);

static sig_atomic_t destroy_job = 0;


resource_allocation_response_msg_t *
allocate_nodes(void)
{
	int rc = 0;
	static int sigarray[] = { SIGQUIT, SIGINT, SIGTERM, 0 };
	SigFunc *oquitf, *ointf, *otermf;
	sigset_t oset;
	resource_allocation_response_msg_t *resp = NULL;
	job_desc_msg_t *j = job_desc_msg_create_from_opts (NULL);

	oquitf = xsignal(SIGQUIT, _intr_handler);
	ointf  = xsignal(SIGINT,  _intr_handler);
	otermf = xsignal(SIGTERM, _intr_handler);

	xsignal_save_mask(&oset);
	xsignal_unblock(sigarray);

	while ((rc = slurm_allocate_resources(j, &resp) < 0) && _retry()) {
		if (destroy_job)
			goto done;
	} 

	if ((rc == 0) && (resp->node_list == NULL)) {
		if (resp->error_code)
			info("Warning: %s", slurm_strerror(resp->error_code));
		_wait_for_resources(&resp);
	}

    done:
	xsignal_set_mask(&oset);
	xsignal(SIGINT,  ointf);
	xsignal(SIGTERM, otermf);
	xsignal(SIGQUIT, oquitf);

	job_desc_msg_destroy(j);

	return resp;
}

/* 
 * Returns jobid if SLURM_JOBID was set in the user's environment
 *  or if --jobid option was given, else returns 0
 */
uint32_t
jobid_from_env(void)
{
	if (opt.jobid != NO_VAL)
		return ((uint32_t) opt.jobid);
	else 
		return (0);
}

resource_allocation_response_msg_t *
existing_allocation(void)
{
	old_job_alloc_msg_t job;
	resource_allocation_response_msg_t *resp = NULL;

	if ((job.job_id = jobid_from_env()) == 0)
		return NULL;
	job.uid = getuid();

	if (slurm_confirm_allocation(&job, &resp) < 0) {
		if (opt.parallel_debug)
			return NULL;    /* create new allocation as needed */
		if (errno == ESLURM_ALREADY_DONE) 
			error ("SLURM job %u has expired.", job.job_id); 
		else
			error ("Unable to confirm allocation for job %u: %m",
			      job.job_id);
		info ("Check SLURM_JOBID environment variable " 
		      "for expired or invalid job.");
		exit(1);
	}

	return resp;
}


static void
_wait_for_resources(resource_allocation_response_msg_t **resp)
{
	old_job_alloc_msg_t old;
	resource_allocation_response_msg_t *r = *resp;
	int sleep_time = MIN_ALLOC_WAIT;

	info ("job %u queued and waiting for resources", r->job_id);

	old.job_id = r->job_id;
	old.uid = (uint32_t) getuid();
	slurm_free_resource_allocation_response_msg(r);

	/* Keep polling until the job is allocated resources */
	while (_wait_for_alloc_rpc(sleep_time, resp) <= 0) {

		if (slurm_confirm_allocation(&old, resp) >= 0)
			break;

		if (slurm_get_errno() == ESLURM_JOB_PENDING) 
			debug3 ("Still waiting for allocation");
		else 
			fatal ("Unable to confirm allocation for job %u: %m", 
			       old.job_id);

		if (destroy_job) {
			verbose("cancelling job %u", old.job_id);
			slurm_complete_job(old.job_id, 0, 0);
			debugger_launch_failure();
			exit(0);
		}

		if (sleep_time < MAX_ALLOC_WAIT)
			sleep_time++;
	}
	info ("job %u has been allocated resources", (*resp)->job_id);
}

/* Wait up to sleep_time for RPC from slurmctld indicating resource allocation
 * has occured.
 * IN sleep_time: delay in seconds
 * OUT resp: resource allocation response message
 * RET 1 if resp is filled in, 0 otherwise */
static int
_wait_for_alloc_rpc(int sleep_time, resource_allocation_response_msg_t **resp)
{
	struct pollfd fds[1];
	slurm_fd slurmctld_fd;

	if ((slurmctld_fd = slurmctld_msg_init()) < 0) {
		sleep (sleep_time);
		return (0);
	}

	fds[0].fd = slurmctld_fd;
	fds[0].events = POLLIN;

	while (poll (fds, 1, (sleep_time * 1000)) < 0) {
		switch (errno) {
			case EAGAIN:
			case EINTR:
				return (-1);
			case ENOMEM:
			case EINVAL:
			case EFAULT:
				fatal("poll: %m");
			default:
				error("poll: %m. Continuing...");
		}
	}

	if (fds[0].revents & POLLIN)
		return (_accept_msg_connection(slurmctld_fd, resp));

	return (0);
}

/* Accept RPC from slurmctld and process it.
 * IN slurmctld_fd: file descriptor for slurmctld communications
 * OUT resp: resource allocation response message
 * RET 1 if resp is filled in, 0 otherwise */
static int 
_accept_msg_connection(slurm_fd slurmctld_fd, 
		resource_allocation_response_msg_t **resp)
{
	slurm_fd     fd;
	slurm_msg_t *msg = NULL;
	slurm_addr   cli_addr;
	char         host[256];
	short        port;
	int          rc = 0;

	fd = slurm_accept_msg_conn(slurmctld_fd, &cli_addr);
	if (fd < 0) {
		error("Unable to accept connection: %m");
		return rc;
	}

	slurm_get_addr(&cli_addr, &port, host, sizeof(host));
	debug2("got message connection from %s:%d", host, ntohs(port));

	msg = xmalloc(sizeof(*msg));

  again:
	if (slurm_receive_msg(fd, msg, 0) < 0) {
		if (errno == EINTR)
			goto again;
		error("slurm_receive_msg[%s]: %m", host);
		xfree(msg);
	} else {
		msg->conn_fd = fd;
		rc = _handle_msg(msg, resp); /* handle_msg frees msg */
	}

	slurm_close_accepted_conn(fd);
	return rc;
}

/* process RPC from slurmctld
 * IN msg: message recieved
 * OUT resp: resource allocation response message
 * RET 1 if resp is filled in, 0 otherwise */
static int
_handle_msg(slurm_msg_t *msg, resource_allocation_response_msg_t **resp)
{
	uid_t req_uid   = g_slurm_auth_get_uid(msg->cred);
	uid_t uid       = getuid();
	uid_t slurm_uid = (uid_t) slurm_get_slurm_user_id();
	int rc = 0;

	if ((req_uid != slurm_uid) && (req_uid != 0) && (req_uid != uid)) {
		error ("Security violation, slurm message from uid %u",
			(unsigned int) req_uid);
		return 0;
	}

	switch (msg->msg_type) {
		case SRUN_PING:
			debug3("slurmctld ping received");
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			slurm_free_srun_ping_msg(msg->data);
			break;
		case RESPONSE_RESOURCE_ALLOCATION:
			debug2("resource allocation response received");
			slurm_send_rc_msg(msg, SLURM_SUCCESS);
			*resp = msg->data;
			rc = 1;
			break;
		default:
			error("received spurious message type: %d\n",
				 msg->msg_type);
	}
	slurm_free_msg(msg);
	return rc;
}

static bool
_retry()
{
	static int  retries = 0;
	static char *msg = "Slurm controller not responding, "
		           "sleeping and retrying.";

	if (errno == ESLURM_ERROR_ON_DESC_TO_RECORD_COPY) {
		if (retries == 0)
			error (msg);
		else if (retries < MAX_RETRIES)
			debug (msg);
		else
			return false;
		sleep (++retries);
	} else {
		error("Unable to allocate resources: %m");
		return false;
	}

	return true;
}

/*
 * SIGINT handler while waiting for resources to become available.
 */
static void
_intr_handler(int signo)
{
	destroy_job = 1;
}


/*
 * Create job description structure based off srun options
 * (see opt.h)
 */
job_desc_msg_t *
job_desc_msg_create_from_opts (char *script)
{
	extern char **environ;
	job_desc_msg_t *j = xmalloc(sizeof(*j));

	slurm_init_job_desc_msg(j);

	j->contiguous     = opt.contiguous;
	j->features       = opt.constraints;
	j->immediate      = opt.immediate;
	j->name           = opt.job_name;
	j->req_nodes      = opt.nodelist;
	j->exc_nodes      = opt.exc_nodes;
	j->partition      = opt.partition;
	j->min_nodes      = opt.min_nodes;
	j->num_tasks      = opt.nprocs;
	j->user_id        = opt.uid;

	if (opt.egid == (gid_t) -1)
		j->group_id = getgid ();
	else
		j->group_id = opt.egid;

	if (opt.hold)
		j->priority     = 0;
	if (opt.max_nodes)
		j->max_nodes    = opt.max_nodes;
	if (opt.mincpus > -1)
		j->min_procs    = opt.mincpus;
	if (opt.realmem > -1)
		j->min_memory   = opt.realmem;
	if (opt.tmpdisk > -1)
		j->min_tmp_disk = opt.tmpdisk;

	if (opt.overcommit)
		j->num_procs    = opt.min_nodes;
	else
		j->num_procs    = opt.nprocs * opt.cpus_per_task;

	if (opt.no_kill)
		j->kill_on_node_fail   = 0;
	if (opt.time_limit > -1)
		j->time_limit          = opt.time_limit;
	if (opt.share)
		j->shared              = 1;

	j->port = slurmctld_comm_addr.port;
	if (slurmctld_comm_addr.hostname)
		j->host = xstrdup(slurmctld_comm_addr.hostname);
	else
		j->host = NULL;

	if (script) {
		/*
		 * If script is set then we are building a request for
		 *  a batch job
		 */
		xassert (opt.batch);

		j->environment = environ;
		j->env_size = envcount (environ);
		j->script = script;
		j->argv = remote_argv;
		j->argc = remote_argc;
		j->err  = opt.efname;
		j->in   = opt.ifname;
		j->out  = opt.ofname;
		j->work_dir = opt.cwd;
	}

	return (j);
}

void
job_desc_msg_destroy(job_desc_msg_t *j)
{
	if (j) {
		xfree(j->host);
		xfree(j);
	}
}

static job_step_create_request_msg_t *
_step_req_create(job_t *j)
{
	job_step_create_request_msg_t *r = xmalloc(sizeof(*r));
	r->job_id     = j->jobid;
	r->user_id    = opt.uid;
	r->node_count = j->nhosts; 
	r->cpu_count  = opt.overcommit ? j->nhosts 
		                       : (opt.nprocs*opt.cpus_per_task);
	r->num_tasks  = opt.nprocs;
	r->node_list  = j->nodelist;
	r->relative   = false;      /* XXX fix this oneday */

	switch (opt.distribution) {
	case SRUN_DIST_UNKNOWN:
		r->task_dist = (opt.nprocs <= j->nhosts) ? SLURM_DIST_CYCLIC
			                                 : SLURM_DIST_BLOCK;
		break;
	case SRUN_DIST_CYCLIC:
		r->task_dist = SLURM_DIST_CYCLIC;
		break;
	default: /* (opt.distribution == SRUN_DIST_BLOCK) */
		r->task_dist = SLURM_DIST_BLOCK;
		break;
	}

	if (slurmctld_comm_addr.port) {
		r->host = xstrdup(slurmctld_comm_addr.hostname);
		r->port = slurmctld_comm_addr.port;
	}

	return(r);
}

static void
_step_req_destroy(job_step_create_request_msg_t *r)
{
	if (r) {
		xfree(r->host);
		xfree(r);
	}
}

void
create_job_step(job_t *job)
{
	job_step_create_request_msg_t  *req  = NULL;
	job_step_create_response_msg_t *resp = NULL;

	if (!(req = _step_req_create(job))) 
		fatal ("Unable to allocate step request message");

	if ((slurm_job_step_create(req, &resp) < 0) || (resp == NULL)) 
		fatal ("Unable to create job step: %m");

	job->stepid  = resp->job_step_id;
	job->cred    = resp->cred;
	job->switch_job = resp->switch_job;
	/* 
	 * Recreate filenames which may depend upon step id
	 */
	job_update_io_fnames(job);

	_step_req_destroy(req);
}

