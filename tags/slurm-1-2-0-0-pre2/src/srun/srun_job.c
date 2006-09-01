/****************************************************************************\
 *  srun_job.c - job data structure creation functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "src/common/bitstring.h"
#include "src/common/cbuf.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_cred.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/io_hdr.h"
#include "src/common/global_srun.h"

#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/fname.h"
#include "src/srun/attach.h"
#include "src/srun/msg.h"


/*
 * allocation information structure used to store general information
 * about node allocation to be passed to _job_create_structure()
 */
typedef struct allocation_info {
	uint32_t                jobid;
	uint32_t                stepid;
	char                   *nodelist;
	uint32_t                nnodes;
	uint16_t                num_cpu_groups;
	uint32_t               *cpus_per_node;
	uint32_t               *cpu_count_reps;
	select_jobinfo_t select_jobinfo;
} allocation_info_t;

/*
 * Prototypes:
 */
static inline int _estimate_nports(int nclients, int cli_per_port);
static int        _compute_task_count(allocation_info_t *info);
static void       _set_nprocs(allocation_info_t *info);
static srun_job_t *_job_create_structure(allocation_info_t *info);
static void       _job_fake_cred(srun_job_t *job);
static char *     _task_state_name(srun_task_state_t state_inx);
static char *     _host_state_name(srun_host_state_t state_inx);
static char *     _normalize_hostlist(const char *hostlist);


/* 
 * Create an srun job structure w/out an allocation response msg.
 * (i.e. use the command line options)
 */
srun_job_t *
job_create_noalloc(void)
{
	srun_job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(*ai));
	uint32_t cpn = 1;
	hostlist_t  hl = hostlist_create(opt.nodelist);

	if (!hl) {
		error("Invalid node list `%s' specified", opt.nodelist);
		goto error;
	}

	srand48(getpid());
	ai->jobid          = MIN_NOALLOC_JOBID +
				((uint32_t) lrand48() % 
				(MAX_NOALLOC_JOBID - MIN_NOALLOC_JOBID + 1));
	ai->stepid         = (uint32_t) (lrand48());
	ai->nodelist       = opt.nodelist;
	ai->nnodes         = hostlist_count(hl);

	hostlist_destroy(hl);
	
	cpn = (opt.nprocs + ai->nnodes - 1) / ai->nnodes;
	ai->cpus_per_node  = &cpn;
	ai->cpu_count_reps = &ai->nnodes;
	
	/* 
	 * Create job, then fill in host addresses
	 */
	job = _job_create_structure(ai);
	job->step_layout = fake_slurm_step_layout_create(job->nodelist, 
							 NULL, NULL,
							 job->nhosts,
							 job->ntasks);
		
	_job_fake_cred(job);
	job_update_io_fnames(job);

   error:
	xfree(ai);
	return (job);

}

/* 
 * Create an srun job structure for a step w/out an allocation response msg.
 * (i.e. inside an allocation)
 */
srun_job_t *
job_step_create_allocation(uint32_t job_id)
{
	srun_job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(*ai));
	hostlist_t hl = NULL;
	char buf[8192];
	int count = 0;
	char *tasks_per_node = xstrdup(getenv("SLURM_TASKS_PER_NODE"));
	
	ai->jobid          = job_id;
	ai->stepid         = NO_VAL;

	if(!opt.max_nodes)
		opt.max_nodes = opt.min_nodes;

	if (opt.nodelist == NULL) {
		char *nodelist = NULL;
		char *hostfile = getenv("SLURM_HOSTFILE");
		
		if (hostfile != NULL) {
			nodelist = slurm_read_hostfile(hostfile, opt.nprocs);
			if (nodelist == NULL) {
				error("Failure getting NodeNames from "
				      "hostfile");
				/* FIXME - need to fail somehow */
			} else {
				debug("loading nodes from hostfile %s",
				      hostfile);
				opt.nodelist = xstrdup(nodelist);
				free(nodelist);
				opt.distribution = SLURM_DIST_ARBITRARY;
			}
		}
	}
	ai->nodelist       = opt.alloc_nodelist;
	/* hl = hostlist_create(ai->nodelist); */
/* 	hostlist_uniq(hl); */
/* 	ai->nnodes = hostlist_count(hl); */
/* 	hostlist_destroy(hl); */
/* 	info("using %s %d not %d", ai->nodelist, ai->nnodes, opt.min_nodes); */

	if (opt.exc_nodes) {
		hostlist_t exc_hl = hostlist_create(opt.exc_nodes);
		char *node_name = NULL;
		if(opt.nodelist)
			hl = hostlist_create(opt.nodelist);
		else
			hl = hostlist_create(ai->nodelist);
		info("using %s or %s", opt.nodelist, ai->nodelist);
		while ((node_name = hostlist_shift(exc_hl))) {
			int inx = hostlist_find(hl, node_name);
			if (inx >= 0) {
				debug("excluding node %s", node_name);
				hostlist_delete_nth(hl, inx);
			}
			free(node_name);
		}
		if(!hostlist_count(hl)) {
			error("Hostlist is now nothing!  Can't run job.");
			return NULL;
		}
		hostlist_destroy(exc_hl);
		hostlist_ranged_string(hl, sizeof(buf), buf);
		hostlist_destroy(hl);
		xfree(opt.nodelist);
		opt.nodelist = xstrdup(buf);
		xfree(ai->nodelist);
		ai->nodelist = xstrdup(buf);
	}
	
/* 	if(!opt.nodelist)  */
/* 		opt.nodelist = ai->nodelist; */
	if(opt.nodelist) { 
		hl = hostlist_create(opt.nodelist);
		if(!hostlist_count(hl)) {
			error("1 Hostlist is now nothing!  Can't run job.");
			return NULL;
		}
		hostlist_ranged_string(hl, sizeof(buf), buf);
		count = hostlist_count(hl);
		hostlist_destroy(hl);
		xfree(ai->nodelist);
		ai->nodelist = xstrdup(buf);
		xfree(opt.nodelist);
		opt.nodelist = xstrdup(buf);
	}
	if(opt.distribution == SLURM_DIST_ARBITRARY) {
		if(count != opt.nprocs) {
			error("You asked for %d tasks but specified %d nodes",
			      opt.nprocs, count);
			goto error;
		}
	}

	hl = hostlist_create(ai->nodelist);
	hostlist_uniq(hl);
	ai->nnodes = hostlist_count(hl);
	hostlist_destroy(hl);
	if (ai->nnodes == 0) {
		error("No nodes in allocation, can't run job");
		goto error;
	}

	//ai->nnodes         = opt.min_nodes;
	/* info("node list is now %s %s %d procs",  */
/* 	     ai->nodelist, opt.nodelist, */
/* 	     opt.nprocs); */
	if(tasks_per_node) {
		int i = 0;
		
		ai->num_cpu_groups = 0;
		ai->cpus_per_node = xmalloc(sizeof(uint32_t) * ai->nnodes);
		ai->cpu_count_reps =xmalloc(sizeof(uint32_t) * ai->nnodes);
		
		while(tasks_per_node[i]) {
			if(tasks_per_node[i] >= '0' 
			   && tasks_per_node[i] <= '9')
				ai->cpus_per_node[ai->num_cpu_groups] =
					atoi(&tasks_per_node[i]);
			else {
				error("problem with tasks_per_node %s", 
				      tasks_per_node);
				goto error;
			}
			while(tasks_per_node[i]!='x' 
			      && tasks_per_node[i]!=',' 
			      && tasks_per_node[i])
				i++;

			if(tasks_per_node[i] == ',' || !tasks_per_node[i]) {
				if(tasks_per_node[i])
					i++;	
				ai->cpu_count_reps[ai->num_cpu_groups] = 1;
				ai->num_cpu_groups++;
				continue;
			}

			i++;
			if(tasks_per_node[i] >= '0' 
			   && tasks_per_node[i] <= '9')
				ai->cpu_count_reps[ai->num_cpu_groups] = 
					atoi(&tasks_per_node[i]);
			else {
				error("1 problem with tasks_per_node %s", 
				      tasks_per_node);
				goto error;
			}
				
			while(tasks_per_node[i]!=',' && tasks_per_node[i])
				i++;
			if(tasks_per_node[i] == ',') {
				i++;	
			}
			ai->num_cpu_groups++;
		}
		xfree(tasks_per_node);
	} else {
		uint32_t cpn = (opt.nprocs + ai->nnodes - 1) / ai->nnodes;
		info("SLURM_TASKS_PER_NODE not set! "
		     "Guessing %d cpus per node", cpn);
		ai->cpus_per_node  = &cpn;
		ai->cpu_count_reps = &ai->nnodes;
	}

	/* get the correct number of hosts to run tasks on */
	if(opt.nodelist) {
		hl = hostlist_create(opt.nodelist);
		hostlist_uniq(hl);
		ai->nnodes = hostlist_count(hl);
		hostlist_destroy(hl);
	} else if((opt.max_nodes > 0) && (opt.max_nodes <ai->nnodes))
		ai->nnodes = opt.max_nodes;
	
	/* 
	 * Create job
	 */
	job = _job_create_structure(ai);
error:
   	xfree(ai);
	return (job);

}

/*
 * Create an srun job structure from a resource allocation response msg
 */
extern srun_job_t *
job_create_allocation(resource_allocation_response_msg_t *resp)
{
	srun_job_t *job;
	allocation_info_t *i = xmalloc(sizeof(*i));
		
	i->nodelist       = _normalize_hostlist(resp->node_list);
	i->nnodes	  = resp->node_cnt;
	i->jobid          = resp->job_id;
	i->stepid         = NO_VAL;
	i->num_cpu_groups = resp->num_cpu_groups;
	i->cpus_per_node  = resp->cpus_per_node;
	i->cpu_count_reps = resp->cpu_count_reps;
	i->select_jobinfo = select_g_copy_jobinfo(resp->select_jobinfo);

	job = _job_create_structure(i);

	xfree(i->nodelist);
	xfree(i);

	return (job);
}

/*
 * Create an srun job structure from a resource allocation response msg
 */
static srun_job_t *
_job_create_structure(allocation_info_t *ainfo)
{
	srun_job_t *job = xmalloc(sizeof(srun_job_t));
	
	_set_nprocs(ainfo);
	debug2("creating job with %d tasks", opt.nprocs);

	slurm_mutex_init(&job->state_mutex);
	pthread_cond_init(&job->state_cond, NULL);
	job->state = SRUN_JOB_INIT;

 	job->nodelist = xstrdup(ainfo->nodelist); 
	job->stepid  = ainfo->stepid;
	
#ifdef HAVE_FRONT_END	/* Limited job step support */
	opt.overcommit = true;
	job->nhosts = 1;
#else
	job->nhosts   = ainfo->nnodes;
#endif

#ifndef HAVE_BG
	if(opt.min_nodes > job->nhosts) {
		error("Only allocated %d nodes asked for %d",
		      job->nhosts, opt.min_nodes);
		return NULL;
	}	
#endif
	job->select_jobinfo = ainfo->select_jobinfo;
	job->jobid   = ainfo->jobid;
	
	job->ntasks  = opt.nprocs;
	job->task_prolog = xstrdup(opt.task_prolog);
	job->task_epilog = xstrdup(opt.task_epilog);
	/* Compute number of file descriptors / Ports needed for Job 
	 * control info server
	 */
	job->njfds = _estimate_nports(opt.nprocs, 48);
	debug3("njfds = %d", job->njfds);
	job->jfd = (slurm_fd *)
		xmalloc(job->njfds * sizeof(slurm_fd));
	job->jaddr = (slurm_addr *) 
		xmalloc(job->njfds * sizeof(slurm_addr));

 	slurm_mutex_init(&job->task_mutex);
	
	job->old_job = false;
	job->removed = false;
	job->signaled = false;
	job->rc       = -1;
	
	/* 
	 *  Initialize Launch and Exit timeout values
	 */
	job->ltimeout = 0;
	job->etimeout = 0;
	
	job->host_state =  xmalloc(job->nhosts * sizeof(srun_host_state_t));
	
	/* ntask task states and statii*/
	job->task_state  =  xmalloc(opt.nprocs * sizeof(srun_task_state_t));
	job->tstatus	 =  xmalloc(opt.nprocs * sizeof(int));
	
	job_update_io_fnames(job);
	
	return (job);	
}

void
update_job_state(srun_job_t *job, srun_job_state_t state)
{
	pipe_enum_t pipe_enum = PIPE_JOB_STATE;
	pthread_mutex_lock(&job->state_mutex);
	if (job->state < state) {
		job->state = state;
		if(message_thread) {
			safe_write(job->forked_msg->par_msg->msg_pipe[1],
				   &pipe_enum, sizeof(int));
			safe_write(job->forked_msg->par_msg->msg_pipe[1],
				   &job->state, sizeof(int));
		}
		pthread_cond_signal(&job->state_cond);
		
	}
	pthread_mutex_unlock(&job->state_mutex);
	return;
rwfail:
	pthread_mutex_unlock(&job->state_mutex);
	error("update_job_state: "
	      "write from srun message-handler process failed");

}

srun_job_state_t 
job_state(srun_job_t *job)
{
	srun_job_state_t state;
	slurm_mutex_lock(&job->state_mutex);
	state = job->state;
	slurm_mutex_unlock(&job->state_mutex);
	return state;
}


void 
job_force_termination(srun_job_t *job)
{
	if (mode == MODE_ATTACH) {
		info ("forcing detach");
		update_job_state(job, SRUN_JOB_DETACHED);
	} else {
		info ("forcing job termination");
		update_job_state(job, SRUN_JOB_FORCETERM);
	}

	client_io_handler_finish(job->client_io);
}


int
job_rc(srun_job_t *job)
{
	int i;
	int rc = 0;

	if (job->rc >= 0) return(job->rc);

	/*
	 *  return (1) if any tasks failed launch
	 */
	for (i = 0; i < opt.nprocs; i++) {
		if (job->task_state[i] == SRUN_TASK_FAILED) 
			return (job->rc = 1);
	}

	for (i = 0; i < opt.nprocs; i++) {
		if (job->rc < job->tstatus[i])
			job->rc = job->tstatus[i];
	}

	if ((rc = WEXITSTATUS(job->rc)))
		job->rc = rc;
	else if (WIFSIGNALED(job->rc))
		job->rc = 128 + WTERMSIG(job->rc);

	return(job->rc);
}


void job_fatal(srun_job_t *job, const char *msg)
{
	if (msg) error(msg);

	srun_job_destroy(job, errno);

	exit(1);
}


void 
srun_job_destroy(srun_job_t *job, int error)
{
	if (job->removed)
		return;

	if (job->old_job) {
		debug("cancelling job step %u.%u", job->jobid, job->stepid);
		slurm_kill_job_step(job->jobid, job->stepid, SIGKILL);
	} else if (!opt.no_alloc) {
		debug("cancelling job %u", job->jobid);
		slurm_complete_job(job->jobid, error);
	} else {
		debug("no allocation to cancel, killing remote tasks");
		fwd_signal(job, SIGKILL, opt.max_threads); 
		return;
	}

	if (error) debugger_launch_failure(job);

	job->removed = true;
}


void
srun_job_kill(srun_job_t *job)
{
	if (!opt.no_alloc) {
		if (slurm_kill_job_step(job->jobid, job->stepid, SIGKILL) < 0)
			error ("slurm_kill_job_step: %m");
	}
	update_job_state(job, SRUN_JOB_FAILED);
}
	
void 
report_job_status(srun_job_t *job)
{
	int i;
	hostlist_t hl = hostlist_create(job->nodelist);
	char *name = NULL;

	for (i = 0; i < job->nhosts; i++) {
		name = hostlist_shift(hl);
		info ("host:%s state:%s", name, 
		      _host_state_name(job->host_state[i]));
		free(name);
	}
}


#define NTASK_STATES 6
void 
report_task_status(srun_job_t *job)
{
	int i;
	char buf[1024];
	hostlist_t hl[NTASK_STATES];

	for (i = 0; i < NTASK_STATES; i++)
		hl[i] = hostlist_create(NULL);

	for (i = 0; i < opt.nprocs; i++) {
		int state = job->task_state[i];
		debug3("  state of task %d is %d", i, state);
		snprintf(buf, 256, "task%d", i);
		hostlist_push(hl[state], buf); 
	}

	for (i = 0; i< NTASK_STATES; i++) {
		if (hostlist_count(hl[i]) > 0) {
			hostlist_ranged_string(hl[i], 1022, buf);
			info("%s: %s", buf, _task_state_name(i));
		}
		hostlist_destroy(hl[i]);
	}

}


static inline int
_estimate_nports(int nclients, int cli_per_port)
{
	div_t d;
	d = div(nclients, cli_per_port);
	return d.rem > 0 ? d.quot + 1 : d.quot;
}

static int
_compute_task_count(allocation_info_t *ainfo)
{
	int i, cnt = 0;

	if (opt.cpus_set) {
		for (i = 0; i < ainfo->num_cpu_groups; i++)
			cnt += ( ainfo->cpu_count_reps[i] *
				 (ainfo->cpus_per_node[i]/opt.cpus_per_task));
	}

	return (cnt < ainfo->nnodes) ? ainfo->nnodes : cnt;
}

static void
_set_nprocs(allocation_info_t *info)
{
	if (!opt.nprocs_set) {
		opt.nprocs = _compute_task_count(info);
		if (opt.cpus_set)
			opt.nprocs_set = true;	/* implicit */
	}
}

void
job_update_io_fnames(srun_job_t *job)
{
	job->ifname = fname_create(job, opt.ifname);
	job->ofname = fname_create(job, opt.ofname);
	job->efname = opt.efname ? fname_create(job, opt.efname) : job->ofname;
}

static void
_job_fake_cred(srun_job_t *job)
{
	slurm_cred_arg_t arg;
	arg.jobid    = job->jobid;
	arg.stepid   = job->stepid;
	arg.uid      = opt.uid;
	arg.hostlist = job->nodelist;
        arg.ntask_cnt = 0;    
        arg.ntask    =  NULL; 
	job->cred = slurm_cred_faker(&arg);
}

static char *
_task_state_name(srun_task_state_t state_inx)
{
	switch (state_inx) {
		case SRUN_TASK_INIT:
			return "initializing";
		case SRUN_TASK_RUNNING:
			return "running";
		case SRUN_TASK_FAILED:
			return "failed";
		case SRUN_TASK_EXITED:
			return "exited";
		case SRUN_TASK_IO_WAIT:
			return "waiting for io";
		case SRUN_TASK_ABNORMAL_EXIT:
			return "exited abnormally";
		default:
			return "unknown";
	}
}

static char *
_host_state_name(srun_host_state_t state_inx)
{
	switch (state_inx) {
		case SRUN_HOST_INIT:
			return "initial";
		case SRUN_HOST_CONTACTED:
			return "contacted";
		case SRUN_HOST_UNREACHABLE:
			return "unreachable";
		case SRUN_HOST_REPLIED:
			return "replied";
		default:
			return "unknown";
	}
}

static char *
_normalize_hostlist(const char *hostlist)
{
	hostlist_t hl = hostlist_create(hostlist);
	char buf[4096];

	if (!hl ||  (hostlist_ranged_string(hl, 4096, buf) < 0))
		return xstrdup(hostlist);

	return xstrdup(buf);
}

