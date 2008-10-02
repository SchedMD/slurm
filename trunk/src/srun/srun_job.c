/****************************************************************************\
 *  srun_job.c - job data structure creation functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona@llnl.gov>.
 *  LLNL-CODE-402394.
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
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/io_hdr.h"
#include "src/common/forward.h"
#include "src/common/fd.h"

#include "src/srun/srun_job.h"
#include "src/srun/opt.h"
#include "src/srun/fname.h"
#include "src/srun/debugger.h"

/*
 * allocation information structure used to store general information
 * about node allocation to be passed to _job_create_structure()
 */
typedef struct allocation_info {
	uint32_t                jobid;
	uint32_t                stepid;
	char                   *nodelist;
	uint32_t                nnodes;
	uint32_t                num_cpu_groups;
	uint16_t               *cpus_per_node;
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
	uint16_t cpn = 1;
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
job_step_create_allocation(resource_allocation_response_msg_t *resp)
{
	uint32_t job_id = resp->job_id;
	srun_job_t *job = NULL;
	allocation_info_t *ai = xmalloc(sizeof(*ai));
	hostlist_t hl = NULL;
	char buf[8192];
	int count = 0;
	uint32_t alloc_count = 0;
	
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

	ai->num_cpu_groups = resp->num_cpu_groups;
	ai->cpus_per_node  = resp->cpus_per_node;
	ai->cpu_count_reps = resp->cpu_count_reps;

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

void
update_job_state(srun_job_t *job, srun_job_state_t state)
{
	pthread_mutex_lock(&job->state_mutex);
	if (job->state < state) {
		job->state = state;
		pthread_cond_signal(&job->state_cond);
		
	}
	pthread_mutex_unlock(&job->state_mutex);
	return;
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
	static int kill_sent = 0;
	static time_t last_msg = 0;

	if (kill_sent == 0) {
		info("forcing job termination");
		/* Sends SIGKILL to tasks directly */
		update_job_state(job, SRUN_JOB_FORCETERM);
	} else {
		time_t now = time(NULL);
		if (last_msg != now) {
			info("job abort in progress");
			last_msg = now;
		}
		if (kill_sent == 1) {
			/* Try sending SIGKILL through slurmctld */
			slurm_kill_job_step(job->jobid, job->stepid, SIGKILL);
		}
	}
	kill_sent++;
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

/*
 * Create an srun job structure from a resource allocation response msg
 */
static srun_job_t *
_job_create_structure(allocation_info_t *ainfo)
{
	srun_job_t *job = xmalloc(sizeof(srun_job_t));
	int i;

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
		if (opt.exc_nodes) {
			/* When resources are pre-allocated and some nodes
			 * are explicitly excluded, this error can occur. */
			error("Are required nodes explicitly excluded?");
		}
		return NULL;
	}
	if ((ainfo->cpus_per_node == NULL) || 
	    (ainfo->cpu_count_reps == NULL)) {
		error("cpus_per_node array is not set");
		return NULL;
	}
#endif
	job->select_jobinfo = ainfo->select_jobinfo;
	job->jobid   = ainfo->jobid;
	
	job->ntasks  = opt.nprocs;
	for (i=0; i<ainfo->num_cpu_groups; i++) {
		job->cpu_count += ainfo->cpus_per_node[i] *
				  ainfo->cpu_count_reps[i];
	}

	job->rc       = -1;
	
	job_update_io_fnames(job);
	
	return (job);	
}

void
job_update_io_fnames(srun_job_t *job)
{
	job->ifname = fname_create(job, opt.ifname);
	job->ofname = fname_create(job, opt.ofname);
	job->efname = opt.efname ? fname_create(job, opt.efname) : job->ofname;
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

