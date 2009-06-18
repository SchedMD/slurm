/*****************************************************************************\
 *  src/srun/allocate.c - srun functions for managing node allocations
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include "src/srun/allocate.h"
#include "src/srun/opt.h"
#include "src/srun/debugger.h"

#ifdef HAVE_BG
#include "src/api/job_info.h"
#include "src/api/node_select_info.h"
#include "src/common/node_select.h"
#include "src/plugins/select/bluegene/plugin/bg_boot_time.h"
#include "src/plugins/select/bluegene/wrap_rm_api.h"
#endif

#ifdef HAVE_CRAY_XT
#include "src/common/node_select.h"
#endif


#define MAX_ALLOC_WAIT 60	/* seconds */
#define MIN_ALLOC_WAIT  5	/* seconds */
#define MAX_RETRIES    10

pthread_mutex_t msg_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t msg_cond = PTHREAD_COND_INITIALIZER;
allocation_msg_thread_t *msg_thr = NULL;
resource_allocation_response_msg_t *global_resp = NULL;
struct pollfd global_fds[1];

extern char **environ;

static bool exit_flag = false;
static uint32_t pending_job_id = 0;

/*
 * Static Prototypes
 */
static void _set_pending_job_id(uint32_t job_id);
static void _exit_on_signal(int signo);
static void _signal_while_allocating(int signo);
static void  _intr_handler(int signo);

#ifdef HAVE_BG
#define POLL_SLEEP 3			/* retry interval in seconds  */
static int _wait_bluegene_block_ready(
			resource_allocation_response_msg_t *alloc);
static int _blocks_dealloc();
#endif

#ifdef HAVE_CRAY_XT
static int  _claim_reservation(resource_allocation_response_msg_t *alloc);
#endif


static sig_atomic_t destroy_job = 0;

static void _set_pending_job_id(uint32_t job_id)
{
	debug2("Pending job allocation %u", job_id);
	pending_job_id = job_id;
}

static void _signal_while_allocating(int signo)
{
	destroy_job = 1;
	if (pending_job_id != 0) {
		slurm_complete_job(pending_job_id, NO_VAL);
	}
}

static void _exit_on_signal(int signo)
{
	exit_flag = true;
}

/* This typically signifies the job was cancelled by scancel */
static void _job_complete_handler(srun_job_complete_msg_t *msg)
{
	if((int)msg->step_id >= 0)
		info("Force Terminated job %u.%u", msg->job_id, msg->step_id);
	else
		info("Force Terminated job %u", msg->job_id);
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
			ctime(&msg->timeout));
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



static bool _retry()
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
	} else if (errno == EINTR) {
		/* srun may be interrupted by the BLCR checkpoint signal */
		/*
		 * XXX: this will cause the old job cancelled and a new job allocated
		 */
		debug("Syscall interrupted while allocating resources, retrying.");
		return true;
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

#ifdef HAVE_BG
/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _wait_bluegene_block_ready(resource_allocation_response_msg_t *alloc)
{
	int is_ready = 0, i, rc;
	char *block_id = NULL;
	int cur_delay = 0;
	int max_delay = BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
		(BG_INCR_BLOCK_BOOT * alloc->node_cnt);

	pending_job_id = alloc->job_id;
	select_g_get_jobinfo(alloc->select_jobinfo, SELECT_DATA_BLOCK_ID,
			     &block_id);

	for (i=0; (cur_delay < max_delay); i++) {
		if(i == 1)
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
		if (rc == READY_JOB_ERROR)		/* error */
			continue;			/* retry */
		if ((rc & READY_JOB_STATE) == 0)	/* job killed */
			break;
		if (rc & READY_NODE_STATE) {		/* job and node ready */
			is_ready = 1;
			break;
		}
	}
	if (is_ready)
     		debug("Block %s is ready for job", block_id);
	else if(!destroy_job)
		error("Block %s still not ready", block_id);
	else /* this should never happen, but if destroy_job
		send back not ready */
		is_ready = 0;

	xfree(block_id);
	pending_job_id = 0;

	return is_ready;
}

/*
 * Test if any BG blocks are in deallocating state since they are
 * probably related to this job we will want to sleep longer
 * RET	1:  deallocate in progress
 *	0:  no deallocate in progress
 *     -1: error occurred
 */
static int _blocks_dealloc()
{
	static node_select_info_msg_t *bg_info_ptr = NULL, *new_bg_ptr = NULL;
	int rc = 0, error_code = 0, i;
	
	if (bg_info_ptr) {
		error_code = slurm_load_node_select(bg_info_ptr->last_update, 
						   &new_bg_ptr);
		if (error_code == SLURM_SUCCESS)
			select_g_free_node_info(&bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, &new_bg_ptr);
	}

	if (error_code) {
		error("slurm_load_partitions: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return -1;
	}
	for (i=0; i<new_bg_ptr->record_count; i++) {
		if(new_bg_ptr->bg_info_array[i].state 
		   == RM_PARTITION_DEALLOCATING) {
			rc = 1;
			break;
		}
	}
	bg_info_ptr = new_bg_ptr;
	return rc;
}
#endif	/* HAVE_BG */

#ifdef HAVE_CRAY_XT
/* returns 1 if job and nodes are ready for job to begin, 0 otherwise */
static int _claim_reservation(resource_allocation_response_msg_t *alloc)
{
	int rc = 0;
	char *resv_id = NULL;

	select_g_get_jobinfo(alloc->select_jobinfo, SELECT_DATA_RESV_ID,
			     &resv_id);
	if (resv_id == NULL)
		return rc;
	if (basil_resv_conf(resv_id, alloc->job_id) == SLURM_SUCCESS)
		rc = 1;
	xfree(resv_id);
	return rc;
}
#endif


int
allocate_test(void)
{
	int rc;
	job_desc_msg_t *j = job_desc_msg_create_from_opts();
	if(!j)
		return SLURM_ERROR;
	
	rc = slurm_job_will_run(j);
	job_desc_msg_destroy(j);
	return rc;
}

resource_allocation_response_msg_t *
allocate_nodes(void)
{
	resource_allocation_response_msg_t *resp = NULL;
	job_desc_msg_t *j = job_desc_msg_create_from_opts();
	slurm_allocation_callbacks_t callbacks;

	if(!j)
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
	callbacks.user_msg = _user_msg_handler;
	callbacks.node_fail = _node_fail_handler;

	/* create message thread to handle pings and such from slurmctld */
	msg_thr = slurm_allocation_msg_thr_create(&j->other_port, &callbacks);

	xsignal(SIGHUP, _signal_while_allocating);
	xsignal(SIGINT, _signal_while_allocating);
	xsignal(SIGQUIT, _signal_while_allocating);
	xsignal(SIGPIPE, _signal_while_allocating);
	xsignal(SIGTERM, _signal_while_allocating);
	xsignal(SIGUSR1, _signal_while_allocating);
	xsignal(SIGUSR2, _signal_while_allocating);

	while (!resp) {
		resp = slurm_allocate_resources_blocking(j, 0,
							 _set_pending_job_id);
		if (destroy_job) {
			/* cancelled by signal */
			break;
		} else if(!resp && !_retry()) {
			break;		
		}
	}
	
	if(resp && !destroy_job) {
		/*
		 * Allocation granted!
		 */
#ifdef HAVE_BG
		if (!_wait_bluegene_block_ready(resp)) {
			if(!destroy_job)
				error("Something is wrong with the "
				      "boot of the block.");
			goto relinquish;
		}
#endif
#ifdef HAVE_CRAY_XT
		if (!_claim_reservation(resp)) {
			if(!destroy_job)
				error("Something is wrong with the ALPS "
				      "resource reservation.");
			goto relinquish;
		}
#endif
	} else if (destroy_job) {
		goto relinquish;
	}

	xsignal(SIGHUP, _exit_on_signal);
	xsignal(SIGINT, ignore_signal);
	xsignal(SIGQUIT, ignore_signal);
	xsignal(SIGPIPE, ignore_signal);
	xsignal(SIGTERM, ignore_signal);
	xsignal(SIGUSR1, ignore_signal);
	xsignal(SIGUSR2, ignore_signal);

	job_desc_msg_destroy(j);

	return resp;

relinquish:

	slurm_free_resource_allocation_response_msg(resp);
	if(!destroy_job)
		slurm_complete_job(resp->job_id, 1);
	exit(1);
	return NULL;
}

void
ignore_signal(int signo)
{
	/* do nothing */
}

int 
cleanup_allocation()
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
                exit(1);
        }

        return resp;
}

/* Set up port to handle messages from slurmctld */
slurm_fd
slurmctld_msg_init(void)
{
	slurm_addr slurm_address;
	uint16_t port;
	static slurm_fd slurmctld_fd   = (slurm_fd) NULL;

	if (slurmctld_fd)	/* May set early for queued job allocation */
		return slurmctld_fd;

	slurmctld_fd = -1;
	slurmctld_comm_addr.port = 0;

	if ((slurmctld_fd = slurm_init_msg_engine_port(0)) < 0)
		fatal("slurm_init_msg_engine_port error %m");
	if (slurm_get_stream_addr(slurmctld_fd, &slurm_address) < 0)
		fatal("slurm_get_stream_addr error %m");
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
job_desc_msg_create_from_opts ()
{
	job_desc_msg_t *j = xmalloc(sizeof(*j));
	char buf[8192];
	hostlist_t hl = NULL;
	
	slurm_init_job_desc_msg(j);
	
	j->contiguous     = opt.contiguous;
	j->features       = opt.constraints;
	j->immediate      = opt.immediate;
	if (opt.job_name)
		j->name   = xstrdup(opt.job_name);
	else
		j->name   = xstrdup(opt.cmd_name);
	j->reservation    = xstrdup(opt.reservation);
	j->wckey          = xstrdup(opt.wckey);
	
	j->req_nodes      = xstrdup(opt.nodelist);
	
	/* simplify the job allocation nodelist, 
	  not laying out tasks until step */
	if(j->req_nodes) {
		hl = hostlist_create(j->req_nodes);
		hostlist_ranged_string(hl, sizeof(buf), buf);
		xfree(opt.nodelist);
		opt.nodelist = xstrdup(buf);
		hostlist_uniq(hl);
		hostlist_ranged_string(hl, sizeof(buf), buf);
		hostlist_destroy(hl);

		xfree(j->req_nodes);
		j->req_nodes = xstrdup(buf);
	}
	
	if(opt.distribution == SLURM_DIST_ARBITRARY
	   && !j->req_nodes) {
		error("With Arbitrary distribution you need to "
		      "specify a nodelist or hostfile with the -w option");
		return NULL;
	}
	j->exc_nodes      = opt.exc_nodes;
	j->partition      = opt.partition;
	j->min_nodes      = opt.min_nodes;
	if (opt.min_sockets_per_node != NO_VAL)
		j->min_sockets    = opt.min_sockets_per_node;
	if (opt.min_cores_per_socket != NO_VAL)
		j->min_cores      = opt.min_cores_per_socket;
	if (opt.min_threads_per_core != NO_VAL)
		j->min_threads    = opt.min_threads_per_core;
	j->user_id        = opt.uid;
	j->dependency     = opt.dependency;
	if (opt.nice)
		j->nice   = NICE_OFFSET + opt.nice;

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
		j->mail_user = xstrdup(opt.mail_user);
	if (opt.begin)
		j->begin_time = opt.begin;
	if (opt.licenses)
		j->licenses = xstrdup(opt.licenses);
	if (opt.network)
		j->network = xstrdup(opt.network);
	if (opt.account)
		j->account = xstrdup(opt.account);
	if (opt.comment)
		j->comment = xstrdup(opt.comment);

	if (opt.hold)
		j->priority     = 0;
	if (opt.jobid != NO_VAL)
		j->job_id	= opt.jobid;
#if SYSTEM_DIMENSIONS
	if (opt.geometry[0] > 0) {
		int i;
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			j->geometry[i] = opt.geometry[i];
	}
#endif

	if (opt.conn_type != (uint16_t) NO_VAL)
		j->conn_type = opt.conn_type;
			
	if (opt.reboot)
		j->reboot = 1;
	if (opt.no_rotate)
		j->rotate = 0;

	if (opt.blrtsimage)
		j->blrtsimage = xstrdup(opt.blrtsimage);
	if (opt.linuximage)
		j->linuximage = xstrdup(opt.linuximage);
	if (opt.mloaderimage)
		j->mloaderimage = xstrdup(opt.mloaderimage);
	if (opt.ramdiskimage)
		j->ramdiskimage = xstrdup(opt.ramdiskimage);

	if (opt.max_nodes)
		j->max_nodes    = opt.max_nodes;
	if (opt.max_sockets_per_node)
		j->max_sockets  = opt.max_sockets_per_node;
	if (opt.max_cores_per_socket)
		j->max_cores    = opt.max_cores_per_socket;
	if (opt.max_threads_per_core)
		j->max_threads  = opt.max_threads_per_core;

	if (opt.job_min_cpus != NO_VAL)
		j->job_min_procs    = opt.job_min_cpus;
	if (opt.job_min_sockets != NO_VAL)
		j->job_min_sockets  = opt.job_min_sockets;
	if (opt.job_min_cores != NO_VAL)
		j->job_min_cores    = opt.job_min_cores;
	if (opt.job_min_threads != NO_VAL)
		j->job_min_threads  = opt.job_min_threads;
	if (opt.job_min_memory != NO_VAL)
		j->job_min_memory = opt.job_min_memory;
	else if (opt.mem_per_cpu != NO_VAL)
		j->job_min_memory = opt.mem_per_cpu | MEM_PER_CPU;
	if (opt.job_min_tmp_disk != NO_VAL)
		j->job_min_tmp_disk = opt.job_min_tmp_disk;
	if (opt.overcommit) {
		j->num_procs    = opt.min_nodes;
		j->overcommit	= opt.overcommit;
	} else
		j->num_procs    = opt.nprocs * opt.cpus_per_task;
	if (opt.nprocs_set)
		j->num_tasks    = opt.nprocs;

	if (opt.cpus_set)
		j->cpus_per_task = opt.cpus_per_task;

	if (opt.no_kill)
		j->kill_on_node_fail   = 0;
	if (opt.time_limit != NO_VAL)
		j->time_limit          = opt.time_limit;
	j->shared = opt.shared;

	/* srun uses the same listening port for the allocation response
	 * message as all other messages */
	j->alloc_resp_port = slurmctld_comm_addr.port;
	j->other_port = slurmctld_comm_addr.port;

	return (j);
}

void
job_desc_msg_destroy(job_desc_msg_t *j)
{
	if (j) {
		xfree(j->account);
		xfree(j->comment);
		xfree(j);
	}
}

extern int
create_job_step(srun_job_t *job, bool use_all_cpus)
{
	int i, rc;
	SigFunc *oquitf = NULL, *ointf = NULL, *otermf = NULL;
	unsigned long my_sleep = 0;

	slurm_step_ctx_params_t_init(&job->ctx_params);
	job->ctx_params.job_id = job->jobid;
	job->ctx_params.uid = opt.uid;

	/* set the jobid for totalview */
	totalview_jobid = NULL;
	xstrfmtcat(totalview_jobid, "%u", job->ctx_params.job_id);

	job->ctx_params.node_count = job->nhosts;
	if (!opt.nprocs_set && (opt.ntasks_per_node != NO_VAL))
		job->ntasks = opt.nprocs = job->nhosts * opt.ntasks_per_node;
	job->ctx_params.task_count = opt.nprocs;

	if (use_all_cpus)
		job->ctx_params.cpu_count = job->cpu_count;
	else if (opt.overcommit)
		job->ctx_params.cpu_count = job->ctx_params.node_count;
	else
		job->ctx_params.cpu_count = opt.nprocs*opt.cpus_per_task;
	
	job->ctx_params.relative = (uint16_t)opt.relative;
	job->ctx_params.ckpt_interval = (uint16_t)opt.ckpt_interval;
	job->ctx_params.ckpt_dir = opt.ckpt_dir;
	job->ctx_params.exclusive = (uint16_t)opt.exclusive;
	job->ctx_params.immediate = (uint16_t)opt.immediate;
	job->ctx_params.verbose_level = (uint16_t)_verbose;
	if (opt.resv_port_cnt != NO_VAL)
		job->ctx_params.resv_port_cnt = (uint16_t) opt.resv_port_cnt;

	switch (opt.distribution) {
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_CYCLIC_BLOCK:
	case SLURM_DIST_BLOCK_CYCLIC:
	case SLURM_DIST_BLOCK_BLOCK:
		job->ctx_params.task_dist = opt.distribution;
		break;
	case SLURM_DIST_PLANE:
		job->ctx_params.task_dist = SLURM_DIST_PLANE;
		job->ctx_params.plane_size = opt.plane_size;
		break;
	default:
		job->ctx_params.task_dist = (job->ctx_params.task_count <= 
			job->ctx_params.node_count) 
			? SLURM_DIST_CYCLIC : SLURM_DIST_BLOCK;
		opt.distribution = job->ctx_params.task_dist;
		break;

	}
	job->ctx_params.overcommit = opt.overcommit ? 1 : 0;

	job->ctx_params.node_list = opt.nodelist;
	
	job->ctx_params.network = opt.network;
	job->ctx_params.no_kill = opt.no_kill;
	if (opt.job_name_set_cmd && opt.job_name)
		job->ctx_params.name = opt.job_name;
	else
		job->ctx_params.name = opt.cmd_name;
	
	debug("requesting job %u, user %u, nodes %u including (%s)", 
	      job->ctx_params.job_id, job->ctx_params.uid,
	      job->ctx_params.node_count, job->ctx_params.node_list);
	debug("cpus %u, tasks %u, name %s, relative %u", 
	      job->ctx_params.cpu_count, job->ctx_params.task_count,
	      job->ctx_params.name, job->ctx_params.relative);

	for (i=0; (!destroy_job); i++) {
		if(opt.no_alloc) {
			job->step_ctx = slurm_step_ctx_create_no_alloc(
				&job->ctx_params, job->stepid);
		} else
			job->step_ctx = slurm_step_ctx_create(
				&job->ctx_params);
		if (job->step_ctx != NULL) {
			if (i > 0)
				info("Job step created");
			
			break;
		}
		rc = slurm_get_errno();

		if (opt.immediate ||
		    ((rc != ESLURM_NODES_BUSY) && (rc != ESLURM_PORTS_BUSY) &&
		     (rc != ESLURM_PROLOG_RUNNING) && 
		     (rc != ESLURM_DISABLED))) {
			error ("Unable to create job step: %m");
			return -1;
		}
		
		if (i == 0) {
			info("Job step creation temporarily disabled, "
			     "retrying");
			ointf  = xsignal(SIGINT,  _intr_handler);
			otermf  = xsignal(SIGTERM, _intr_handler);
			oquitf  = xsignal(SIGQUIT, _intr_handler);
			my_sleep = (getpid() % 1000) * 100 + 100000;
		} else {
			verbose("Job step creation still disabled, retrying");
			my_sleep = MIN((my_sleep * 2), 60000000);
		}
		/* sleep 0.1 to 60 secs with exponential back-off */
		usleep(my_sleep);
	}
	if (i > 0) {
		xsignal(SIGINT,  ointf);
		xsignal(SIGQUIT, oquitf);
		xsignal(SIGTERM, otermf);
		if (destroy_job) {
			info("Cancelled pending job step");
			return -1;
		}
	}

	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_STEPID, &job->stepid);
	/*  Number of hosts in job may not have been initialized yet if 
	 *    --jobid was used or only SLURM_JOB_ID was set in user env.
	 *    Reset the value here just in case.
	 */
	slurm_step_ctx_get(job->step_ctx, SLURM_STEP_CTX_NUM_HOSTS,
			   &job->nhosts);
	
	/*
	 * Recreate filenames which may depend upon step id
	 */
	job_update_io_fnames(job);

	return 0;
}


