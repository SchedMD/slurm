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

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"

#include "src/srun/allocate.h"
#include "src/srun/opt.h"

#if HAVE_TOTALVIEW
#  include "src/srun/attach.h"
#endif

#define MAX_ALLOC_WAIT 60	/* seconds */
#define MIN_ALLOC_WAIT  2	/* seconds */
#define MAX_RETRIES    10

/*
 * Static Prototypes
 */
static void  _wait_for_resources(resource_allocation_response_msg_t **rp);
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
	job_desc_msg_t *j = job_desc_msg_create();

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
		if (errno == ESLURM_ALREADY_DONE)
			error ("SLURM job %u has expired. Check for allocation or job "
			       "that has exceeded timelimit.", job.job_id);
		else
			error("Unable to confirm resource allocation for job %u: %m",
			      job.job_id);
		exit(1);
	}

	return resp;
}


static void
_wait_for_resources(resource_allocation_response_msg_t **resp)
{
	old_job_alloc_msg_t old_job;
	resource_allocation_response_msg_t *r = *resp;
	int sleep_time = MIN_ALLOC_WAIT;

	info ("job %u queued and waiting for resources", r->job_id);

	old_job.job_id = r->job_id;
	old_job.uid = (uint32_t) getuid();
	slurm_free_resource_allocation_response_msg(r);
	sleep (sleep_time);

	/* Keep polling until the job is allocated resources */
	while (slurm_confirm_allocation(&old_job, resp) < 0) {
		if (slurm_get_errno() == ESLURM_JOB_PENDING) {
			debug3("Still waiting for allocation");
			sleep_time = MIN((++sleep_time), MAX_ALLOC_WAIT);
			sleep(sleep_time);
		} else {
			error("Unable to confirm resource allocation for "
			      "job %u: %m", old_job.job_id);
			exit (1);
		}

		if (destroy_job) {
			verbose("cancelling job %u", old_job.job_id);
			slurm_complete_job(old_job.job_id, 0, 0);
#ifdef HAVE_TOTALVIEW
			tv_launch_failure();
#endif
			exit(0);
		}

	}
	info ("job %u has been allocated resources", (*resp)->job_id);
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
job_desc_msg_create(void)
{
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

	return (j);
}

void
job_desc_msg_destroy(job_desc_msg_t *j)
{
	xfree(j);
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

	return(r);
}

static void
_step_req_destroy(job_step_create_request_msg_t *r)
{
	xfree(r);
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
#ifdef HAVE_ELAN
	job->qsw_job = resp->qsw_job;
#endif
	/* 
	 * Recreate filenames which may depend upon step id
	 */
	job_update_io_fnames(job);

	_step_req_destroy(req);
}

