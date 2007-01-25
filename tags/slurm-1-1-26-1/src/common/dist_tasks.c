/*****************************************************************************\
 *  dist_tasks.c - function to distribute tasks over nodes.
 *  $Id$
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes, <cholmes@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  This file is patterned after hostlist.c, written by Mark Grondona and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STRING_H
#    include <string.h>
#  endif
#else                /* !HAVE_CONFIG_H */
#  include <string.h>
#endif                /* HAVE_CONFIG_H */


#include <slurm/slurm.h>

#include <stdlib.h>

#include <slurm/slurm_errno.h>

#include "src/common/dist_tasks.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static int _task_layout_block(slurm_step_layout_t *step_layout);
static int _task_layout_cyclic(slurm_step_layout_t *step_layout);
#ifndef HAVE_FRONT_END
static int _task_layout_hostfile(slurm_step_layout_t *step_layout);
#endif

/* 
 * distribute_tasks - determine how many tasks of a job will be run on each.
 *                    node. Distribution is influenced by number of cpus on
 *                    each host. 
 * IN mlist - hostlist corresponding to cpu arrays
 * IN num_cpu_groups - elements in below cpu arrays
 * IN cpus_per_node - cpus per node
 * IN cpu_count_reps - how many nodes have same cpu count
 * IN tlist - hostlist of nodes on which to distribute tasks
 * IN num_tasks - number of tasks to distribute across these cpus
 * RET a pointer to an integer array listing task counts per node
 * NOTE: allocates memory that should be xfreed by caller
 */
uint32_t *distribute_tasks(const char *mlist, uint16_t num_cpu_groups,
			uint32_t *cpus_per_node, uint32_t *cpu_count_reps,
			const char *tlist, uint32_t num_tasks) 
{
	hostlist_t master_hl = NULL, task_hl = NULL;
	int i, index, count, hostid, nnodes, ncpus;
	uint32_t *cpus, *ntask = NULL;
	char *this_node_name;
	
	if (!tlist || num_tasks == 0)
		return NULL;

	if ((master_hl = hostlist_create(mlist)) == NULL)
		fatal("hostlist_create error for %s: %m", mlist);

	if ((task_hl = hostlist_create(tlist)) == NULL)
		fatal("hostlist_create error for %s: %m", tlist);

	nnodes = hostlist_count(task_hl);
	ntask = (uint32_t *) xmalloc(sizeof(uint32_t *) * nnodes);
	if (!ntask) {
		hostlist_destroy(master_hl);
		hostlist_destroy(task_hl);
		slurm_seterrno(ENOMEM);
		return NULL;
	}

	index = 0;
	count = 1;
	i = 0;
	ncpus = 0;
	while ((this_node_name = hostlist_shift(master_hl))) {
		if (hostlist_find(task_hl, this_node_name) >= 0) {
			if (i >= nnodes) {
				fatal("Internal error: duplicate nodes? "
					"(%s)(%s):%m", mlist, tlist);
			}
			ntask[i++] = cpus_per_node[index];
			ncpus += cpus_per_node[index];
		}

		if (++count > cpu_count_reps[index]) {
			index++;
			count = 1;
		}
		free(this_node_name);
	}
	hostlist_destroy(master_hl);
	hostlist_destroy(task_hl);
	if (num_tasks >= ncpus) {
		/*
		 * Evenly overcommit tasks over the hosts
		 */
		int extra = num_tasks - ncpus;
		int add_to_all = extra / nnodes;
		int subset = extra % nnodes;
		for (i = 0; i < nnodes; i++) {
			ntask[i] += add_to_all;
			if (i < subset)
				ntask[i]++;
		}
		return ntask;
	}

	/*
	 * NOTE: num_tasks is less than ncpus here.
	 *
	 * In a cyclic fashion, place tasks on the nodes as permitted
	 * by the cpu constraints.
	 */
	cpus = ntask;
	ntask = (uint32_t *) xmalloc(sizeof(int *) * nnodes);
	if (!ntask) {
		slurm_seterrno(ENOMEM);
		xfree(cpus);
		return NULL;
	}

	for (i = 0; i < nnodes; i++)
		ntask[i] = 0;

	hostid = 0;
	for (i = 0; i < num_tasks;) {
		if (ntask[hostid] < cpus[hostid]) {
			ntask[hostid]++;
			i++;
		}
		if (++hostid >= nnodes)
			hostid = 0;
	}
	xfree(cpus);
	return ntask;
}
extern slurm_step_layout_t *step_layout_create(
	resource_allocation_response_msg_t *alloc_resp,
	job_step_create_response_msg_t *step_resp,
	job_step_create_request_msg_t *step_req)
{
	slurm_step_layout_t *step_layout = NULL;
	char *temp = NULL;
	
	if(step_req && step_resp) {
		temp = step_req->node_list;
		step_req->node_list = step_resp->node_list;
		step_resp->node_list = temp;
	}
	step_layout = xmalloc(sizeof(slurm_step_layout_t));
	if(!step_layout) {
		error("xmalloc error for step_layout");
		return NULL;
	}
	step_layout->hl = NULL;
	
	if(alloc_resp) {
		step_layout->alloc_nodes = 
			(char *)xstrdup(alloc_resp->node_list);
		step_layout->hl	= hostlist_create(alloc_resp->node_list);
		step_layout->cpus_per_node = alloc_resp->cpus_per_node;
		step_layout->cpu_count_reps = alloc_resp->cpu_count_reps;
#ifdef HAVE_FRONT_END	/* Limited job step support */
		/* All jobs execute through front-end on Blue Gene.
		 * Normally we would not permit execution of job steps,
		 * but can fake it by just allocating all tasks to
		 * one of the allocated nodes. */
		step_layout->num_hosts    = 1;
#else
		step_layout->num_hosts  = alloc_resp->node_cnt;
#endif
		step_layout->num_tasks  = alloc_resp->node_cnt;
	} else {
		debug("no alloc_resp given for step_layout_create");
		step_layout->alloc_nodes = NULL;
		step_layout->cpus_per_node = NULL;
		step_layout->cpu_count_reps = NULL;
	}

	if(step_resp) 
		step_layout->step_nodes = 
			(char *)xstrdup(step_resp->node_list);
	else {
		debug("no step_resp given for step_layout_create");
		step_layout->step_nodes = NULL;
	}

	if(step_req) {
		if(step_layout->hl)
			hostlist_destroy(step_layout->hl);
		step_layout->hl	= hostlist_create(step_req->node_list);
#ifdef HAVE_FRONT_END   /* Limited job step support */
		/* All jobs execute through front-end on Blue Gene.
		 * Normally we would not permit execution of job steps,
		 * but can fake it by just allocating all tasks to
		 * one of the allocated nodes. */
		step_layout->num_hosts = 1;
#else
		step_layout->num_hosts = hostlist_count(step_layout->hl);
#endif

		step_layout->task_dist	= step_req->task_dist;
		step_layout->num_tasks  = step_req->num_tasks;
	} else {
		debug("no step_req given for step_layout_create");
	}

	return step_layout;
}

/* destroys structure for step layout */
extern int step_layout_destroy(slurm_step_layout_t *step_layout)
{
	int i=0;
	if(step_layout) {
		xfree(step_layout->alloc_nodes);
		xfree(step_layout->step_nodes);
		for (i=0; i<step_layout->num_hosts; i++) {
			free(step_layout->host[i]);
			xfree(step_layout->tids[i]);
		}
		xfree(step_layout->host);
		xfree(step_layout->tids);
		xfree(step_layout->cpus);
		xfree(step_layout->tasks);
		xfree(step_layout->hostids);
		
		hostlist_destroy(step_layout->hl);
		xfree(step_layout);
	}
		
	return SLURM_SUCCESS;
}

/* build maps for task layout on nodes */
extern int task_layout(slurm_step_layout_t *step_layout)
{
	int cpu_cnt = 0, cpu_inx = 0, i;
	
	/* if we have more hosts than tasks we will set num_hosts
	 * to be num_tasks.
	 */
	if(step_layout->num_tasks < step_layout->num_hosts)
		step_layout->num_hosts = step_layout->num_tasks;
	
	debug("laying out the %d tasks on %d hosts\n", 
	      step_layout->num_tasks, step_layout->num_hosts);
	
	if (step_layout->cpus)	/* layout already completed */
		return SLURM_SUCCESS;
	
	step_layout->cpus  = xmalloc(sizeof(uint32_t) 
				     * step_layout->num_hosts);
	step_layout->tasks = xmalloc(sizeof(uint32_t) 
				     * step_layout->num_hosts);
	step_layout->host  = xmalloc(sizeof(char *)
				     * step_layout->num_hosts);
	step_layout->tids  = xmalloc(sizeof(uint32_t *) 
				     * step_layout->num_hosts);
	step_layout->hostids = xmalloc(sizeof(uint32_t) 
				     * step_layout->num_tasks);

	for (i=0; i<step_layout->num_hosts; i++) {
		step_layout->host[i] = hostlist_shift(step_layout->hl);
		step_layout->cpus[i] = step_layout->cpus_per_node[cpu_inx];
		if ((++cpu_cnt) >= step_layout->cpu_count_reps[cpu_inx]) {
			/* move to next record */
			cpu_inx++;
			cpu_cnt = 0;
		}
	}

	if (step_layout->task_dist == SLURM_DIST_CYCLIC)
		return _task_layout_cyclic(step_layout);
#ifndef HAVE_FRONT_END
	else if(step_layout->task_dist == SLURM_DIST_ARBITRARY)
		return _task_layout_hostfile(step_layout);
#endif
	else
		return _task_layout_block(step_layout);
}

int 
step_layout_host_id (slurm_step_layout_t *s, int taskid)
{
	if (taskid > s->num_tasks - 1)
		return SLURM_ERROR;

	return (s->hostids[taskid]);
}

char *
step_layout_host_name (slurm_step_layout_t *s, int taskid)
{
	int hostid = step_layout_host_id (s, taskid);

	if (hostid < 0)
		return NULL;
	
	return (s->host[hostid]);
}

#ifndef HAVE_FRONT_END
/* use specific set run tasks on each host listed in hostfile
 * XXX: Need to handle over-subscribe.
 */
static int _task_layout_hostfile(slurm_step_layout_t *step_layout)
{
	int i=0, j, taskid = 0;
	hostlist_iterator_t itr = NULL, itr_task = NULL;
	char *host = NULL;
	char *host_task = NULL;
	hostlist_t job_alloc_hosts = NULL;
	hostlist_t step_alloc_hosts = NULL;
	
	job_alloc_hosts = hostlist_create(step_layout->alloc_nodes);
	itr = hostlist_iterator_create(job_alloc_hosts);
	step_alloc_hosts = hostlist_create(step_layout->step_nodes);
	itr_task = hostlist_iterator_create(step_alloc_hosts);
	while((host = hostlist_next(itr))) {
		step_layout->tasks[i] = 0;
		while((host_task = hostlist_next(itr_task))) {
			if(!strcmp(host, host_task))
				step_layout->tasks[i]++;
			free(host_task);
		}
		debug2("%s got %d tasks\n",
		       host,
		       step_layout->tasks[i]);
		if(step_layout->tasks[i] == 0)
			goto reset_hosts;
		step_layout->tids[i] = xmalloc(sizeof(uint32_t) 
					       * step_layout->tasks[i]);
		taskid = 0;
		j = 0;
		hostlist_iterator_reset(itr_task);
		while((host_task = hostlist_next(itr_task))) {
			if(!strcmp(host, host_task)) {
				step_layout->tids[i][j] = taskid;
				step_layout->hostids[taskid] = i;
				j++;
			}
			taskid++;
			free(host_task);
		}
		
	reset_hosts:
		i++;
		hostlist_iterator_reset(itr_task);	
		free(host);
	}
	hostlist_iterator_destroy(itr);
	hostlist_iterator_destroy(itr_task);
	hostlist_destroy(job_alloc_hosts);
	hostlist_destroy(step_alloc_hosts);

	return SLURM_SUCCESS;
}
#endif

/* to effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many tasks go on each node and
 * then make those assignments in a block fashion */
static int _task_layout_block(slurm_step_layout_t *step_layout)
{
	int i, j, taskid = 0;
	bool over_subscribe = false;

	/* figure out how many tasks go to each node */
	for (j=0; (taskid<step_layout->num_tasks); j++) {   /* cycle counter */
		bool space_remaining = false;
		for (i=0; ((i<step_layout->num_hosts)
			   && (taskid<step_layout->num_tasks)); i++) {
			if ((j<step_layout->cpus[i]) || over_subscribe) {
				taskid++;
				step_layout->tasks[i]++;
				if ((j+1) < step_layout->cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}

	/* now distribute the tasks */
	taskid = 0;
	for (i=0; i < step_layout->num_hosts; i++) {
		step_layout->tids[i] = xmalloc(sizeof(uint32_t) 
					       * step_layout->tasks[i]);
		if (step_layout->tids[i] == NULL) {
			slurm_seterrno(ENOMEM);
			return SLURM_ERROR;
		}
		for (j=0; j<step_layout->tasks[i]; j++) {
			step_layout->tids[i][j] = taskid;
			step_layout->hostids[taskid] = i;
			taskid++;
		}
	}
	return SLURM_SUCCESS;
}


/* distribute tasks across available nodes: allocate tasks to nodes
 * in a cyclic fashion using available processors. once all available
 * processors are allocated, continue to allocate task over-subscribing
 * nodes as needed. for example
 * cpus per node        4  2  4  2
 *                     -- -- -- --
 * task distribution:   0  1  2  3
 *                      4  5  6  7
 *                      8     9
 *                     10    11     all processors allocated now
 *                     12 13 14 15  etc.
 */
static int _task_layout_cyclic(slurm_step_layout_t *step_layout)
{
	int i, j, taskid = 0;
	bool over_subscribe = false;

	for (i=0; i<step_layout->num_hosts; i++) {
		step_layout->tids[i] = xmalloc(sizeof(uint32_t) 
					       * step_layout->num_tasks);
		if (step_layout->tids[i] == NULL) {
			slurm_seterrno(ENOMEM);
			return SLURM_ERROR;
		}
	}
	for (j=0; taskid<step_layout->num_tasks; j++) {   /* cycle counter */
		bool space_remaining = false;
		for (i=0; ((i<step_layout->num_hosts) 
			   && (taskid<step_layout->num_tasks)); i++) {
			if ((j<step_layout->cpus[i]) || over_subscribe) {
				step_layout->tids[i][step_layout->tasks[i]] = 
					taskid;
				step_layout->hostids[taskid] = i;
				taskid++;
				step_layout->tasks[i]++;
				if ((j+1) < step_layout->cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}
	return SLURM_SUCCESS;
}

