/****************************************************************************\
 *  srun_job.c - job data structure creation functions
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
 *  UCRL-CODE-226842.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
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
#include "src/common/forward.h"

#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/fname.h"
#include "src/srun/attach.h"
#include "src/srun/msg.h"

typedef enum {DSH_NEW, DSH_ACTIVE, DSH_DONE, DSH_FAILED} state_t;

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

typedef struct thd {
        pthread_t	thread;			/* thread ID */
        pthread_attr_t	attr;			/* thread attributes */
        state_t		state;      		/* thread state */
} thd_t;

int message_thread = 0;
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
	uint32_t alloc_count = 0;
	char *tasks_per_node = xstrdup(getenv("SLURM_TASKS_PER_NODE"));
	
	ai->jobid          = job_id;
	ai->stepid         = NO_VAL;
	ai->nodelist = opt.alloc_nodelist;
	hl = hostlist_create(ai->nodelist);
	hostlist_uniq(hl);
	alloc_count = hostlist_count(hl);
	ai->nnodes = alloc_count;
	hostlist_destroy(hl);
	
	if (opt.exc_nodes) {
		hostlist_t exc_hl = hostlist_create(opt.exc_nodes);
		hostlist_t inc_hl = NULL;
		char *node_name = NULL;
		
		hl = hostlist_create(ai->nodelist);
		if(opt.nodelist) {
			inc_hl = hostlist_create(opt.nodelist);
		}
		hostlist_uniq(hl);
		//info("using %s or %s", opt.nodelist, ai->nodelist);
		while ((node_name = hostlist_shift(exc_hl))) {
			int inx = hostlist_find(hl, node_name);
			if (inx >= 0) {
				debug("excluding node %s", node_name);
				hostlist_delete_nth(hl, inx);
				ai->nnodes--;	/* decrement node count */
			}
			if(inc_hl) {
				inx = hostlist_find(inc_hl, node_name);
				if (inx >= 0) {
					error("Requested node %s is also "
					      "in the excluded list.",
					      node_name);
					error("Job not submitted.");
					hostlist_destroy(exc_hl);
					hostlist_destroy(inc_hl);
					goto error;
				}
			}
			free(node_name);
		}
		hostlist_destroy(exc_hl);

		/* we need to set this here so if there are more nodes
		 * available than we requested we can set it
		 * straight. If there is no exclude list then we set
		 * the vars then.
		 */
		if (!opt.nodes_set) {
			/* we don't want to set the number of nodes =
			 * to the number of requested processes unless we
			 * know it is less than the number of nodes
			 * in the allocation
			 */
			if(opt.nprocs_set && (opt.nprocs < ai->nnodes))
				opt.min_nodes = opt.nprocs;
			else
				opt.min_nodes = ai->nnodes;
			opt.nodes_set = true;
		}
		if(!opt.max_nodes)
			opt.max_nodes = opt.min_nodes;
		if((opt.max_nodes > 0) && (opt.max_nodes < ai->nnodes))
			ai->nnodes = opt.max_nodes;

		count = hostlist_count(hl);
		if(!count) {
			error("Hostlist is now nothing!  Can't run job.");
			hostlist_destroy(hl);
			goto error;
		}
		if(inc_hl) {
			count = hostlist_count(inc_hl);
			if(count < ai->nnodes) {
				/* add more nodes to get correct number for
				   allocation */
				hostlist_t tmp_hl = hostlist_copy(hl);
				int i=0;
				int diff = ai->nnodes - count;
				hostlist_ranged_string(inc_hl,
						       sizeof(buf), buf);
				hostlist_delete(tmp_hl, buf);
				while((node_name = hostlist_shift(tmp_hl))
				      && (i < diff)) {
					hostlist_push(inc_hl, node_name);
					i++;
				}
				hostlist_destroy(tmp_hl);
			}
			hostlist_ranged_string(inc_hl, sizeof(buf), buf);
			hostlist_destroy(inc_hl);
			xfree(opt.nodelist);
			opt.nodelist = xstrdup(buf);
		} else {
			if(count > ai->nnodes) {
				/* remove more nodes than needed for
				   allocation */
				int i=0;
				for(i=count; i>ai->nnodes; i--) 
					hostlist_delete_nth(hl, i);
			}
			hostlist_ranged_string(hl, sizeof(buf), buf);
			xfree(opt.nodelist);
			opt.nodelist = xstrdup(buf);
		}

		hostlist_destroy(hl);			
	} else {
		if (!opt.nodes_set) {
			/* we don't want to set the number of nodes =
			 * to the number of requested processes unless we
			 * know it is less than the number of nodes
			 * in the allocation
			 */
			if(opt.nprocs_set && (opt.nprocs < ai->nnodes))
				opt.min_nodes = opt.nprocs;
			else
				opt.min_nodes = ai->nnodes;
			opt.nodes_set = true;
		}
		if(!opt.max_nodes)
			opt.max_nodes = opt.min_nodes;
		if((opt.max_nodes > 0) && (opt.max_nodes < ai->nnodes))
			ai->nnodes = opt.max_nodes;
		/* Don't reset the ai->nodelist because that is the
		 * nodelist we want to say the allocation is under
		 * opt.nodelist is what is used for the allocation.
		 */
		/* xfree(ai->nodelist); */
/* 		ai->nodelist = xstrdup(buf); */
	}
	
	/* get the correct number of hosts to run tasks on */
	if(opt.nodelist) { 
		hl = hostlist_create(opt.nodelist);
		if(opt.distribution != SLURM_DIST_ARBITRARY)
			hostlist_uniq(hl);
		if(!hostlist_count(hl)) {
			error("Hostlist is now nothing!  Can not run job.");
			hostlist_destroy(hl);
			goto error;
		}
		
		hostlist_ranged_string(hl, sizeof(buf), buf);
		count = hostlist_count(hl);
		hostlist_destroy(hl);
		/* Don't reset the ai->nodelist because that is the
		 * nodelist we want to say the allocation is under
		 * opt.nodelist is what is used for the allocation.
		 */
		/* xfree(ai->nodelist); */
/* 		ai->nodelist = xstrdup(buf); */
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

	if (ai->nnodes == 0) {
		error("No nodes in allocation, can't run job");
		goto error;
	}

	if(tasks_per_node) {
		int i = 0;
		
		ai->num_cpu_groups = 0;
		ai->cpus_per_node = xmalloc(sizeof(uint32_t) * alloc_count);
		ai->cpu_count_reps = xmalloc(sizeof(uint32_t) * alloc_count);
		
		while(tasks_per_node[i]
		      && (ai->num_cpu_groups < alloc_count)) {
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
		uint32_t cpn = (opt.nprocs + alloc_count - 1) / alloc_count;
		debug("SLURM_TASKS_PER_NODE not set! "
		      "Guessing %d cpus per node", cpn);
		ai->cpus_per_node  = &cpn;
		ai->cpu_count_reps = &alloc_count;
	}

/* 	info("looking for %d nodes out of %s with a must list of %s", */
/* 	     ai->nnodes, ai->nodelist, opt.nodelist); */
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

#ifndef HAVE_FRONT_END
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
set_job_rc(srun_job_t *job)
{
	int i, rc = 0, task_failed = 0;

	/*
	 *  return code set to at least one if any tasks failed launch
	 */
	for (i = 0; i < opt.nprocs; i++) {
		if (job->task_state[i] == SRUN_TASK_FAILED)
			task_failed = 1; 
		if (job->rc < job->tstatus[i])
			job->rc = job->tstatus[i];
	}
	if (task_failed && (job->rc <= 0)) {
		job->rc = 1;
		return 1;
	}

	if ((rc = WEXITSTATUS(job->rc)))
		return rc;
	if (WIFSIGNALED(job->rc))
		return (128 + WTERMSIG(job->rc));
	return job->rc;
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
	char buf[MAXHOSTRANGELEN+2];
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
			hostlist_ranged_string(hl[i], MAXHOSTRANGELEN, buf);
			info("%s: %s", buf, _task_state_name(i));
		}
		hostlist_destroy(hl[i]);
	}

}

void 
fwd_signal(srun_job_t *job, int signo, int max_threads)
{
	int i;
	slurm_msg_t req;
	kill_tasks_msg_t msg;
	static pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;
	pipe_enum_t pipe_enum = PIPE_SIGNALED;
	hostlist_t hl;
	char *name = NULL;
	char buf[8192];
	List ret_list = NULL;
	ListIterator itr;
	ret_data_info_t *ret_data_info = NULL;
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&sig_mutex);

	if (signo == SIGKILL || signo == SIGINT || signo == SIGTERM) {
		slurm_mutex_lock(&job->state_mutex);
		job->signaled = true;
		slurm_mutex_unlock(&job->state_mutex);
		if(message_thread) {
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &pipe_enum,sizeof(int));
			write(job->forked_msg->par_msg->msg_pipe[1],
			      &job->signaled,sizeof(int));
		}
	}

	debug2("forward signal %d to job", signo);

	/* common to all tasks */
	msg.job_id      = job->jobid;
	msg.job_step_id = job->stepid;
	msg.signal      = (uint32_t) signo;
	
	hl = hostlist_create("");
	for (i = 0; i < job->nhosts; i++) {
		if (job->host_state[i] != SRUN_HOST_REPLIED) {
			name = nodelist_nth_host(
				job->step_layout->node_list, i);
			debug2("%s has not yet replied\n", name);
			free(name);
			continue;
		}
		if (job_active_tasks_on_host(job, i) == 0)
			continue;
		name = nodelist_nth_host(job->step_layout->node_list, i);
		hostlist_push(hl, name);
		free(name);
	}
	if(!hostlist_count(hl)) {
		hostlist_destroy(hl);
		goto nothing_left;
	}
	hostlist_ranged_string(hl, sizeof(buf), buf);
	hostlist_destroy(hl);
	name = xstrdup(buf);

	slurm_msg_t_init(&req);	
	req.msg_type = REQUEST_SIGNAL_TASKS;
	req.data     = &msg;
	
	debug3("sending signal to host %s", name);
	
	if (!(ret_list = slurm_send_recv_msgs(name, &req, 0))) { 
		error("fwd_signal: slurm_send_recv_msgs really failed bad");
		xfree(name);
		slurm_mutex_unlock(&sig_mutex);
		return;
	}
	xfree(name);
	itr = list_iterator_create(ret_list);		
	while((ret_data_info = list_next(itr))) {
		rc = slurm_get_return_code(ret_data_info->type, 
					   ret_data_info->data);
		/*
		 *  Report error unless it is "Invalid job id" which 
		 *    probably just means the tasks exited in the meanwhile.
		 */
		if ((rc != 0) && (rc != ESLURM_INVALID_JOB_ID)
		    &&  (rc != ESLURMD_JOB_NOTRUNNING) && (rc != ESRCH)) {
			error("%s: signal: %s", 
			      ret_data_info->node_name, 
			      slurm_strerror(rc));
			destroy_data_info(ret_data_info);
		}
	}
	list_iterator_destroy(itr);
	list_destroy(ret_list);
nothing_left:
	debug2("All tasks have been signalled");
	
	slurm_mutex_unlock(&sig_mutex);
}

int
job_active_tasks_on_host(srun_job_t *job, int hostid)
{
	int i;
	int retval = 0;

	slurm_mutex_lock(&job->task_mutex);
	for (i = 0; i < job->step_layout->tasks[hostid]; i++) {
		uint32_t *tids = job->step_layout->tids[hostid];
		xassert(tids != NULL);
		debug("Task %d state: %d", tids[i], job->task_state[tids[i]]);
		if (job->task_state[tids[i]] == SRUN_TASK_RUNNING) 
			retval++;
	}
	slurm_mutex_unlock(&job->task_mutex);
	return retval;
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
        arg.alloc_lps_cnt = 0;    
        arg.alloc_lps     =  NULL; 
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

