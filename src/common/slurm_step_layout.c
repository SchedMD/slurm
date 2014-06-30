/*****************************************************************************\
 *  slurm_step_layout.c - functions to distribute tasks over nodes.
 *  $Id$
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes, <cholmes@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
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

#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_step_layout.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(pack_slurm_step_layout, slurm_pack_slurm_step_layout);
strong_alias(unpack_slurm_step_layout, slurm_unpack_slurm_step_layout);

/* build maps for task layout on nodes */
static int _init_task_layout(slurm_step_layout_t *step_layout,
			     const char *arbitrary_nodes,
			     uint16_t *cpus_per_node, uint32_t *cpu_count_reps,
			     uint16_t cpus_per_task,
			     uint16_t task_dist, uint16_t plane_size);

static int _task_layout_block(slurm_step_layout_t *step_layout,
			      uint16_t *cpus);
static int _task_layout_cyclic(slurm_step_layout_t *step_layout,
			       uint16_t *cpus);
static int _task_layout_plane(slurm_step_layout_t *step_layout,
			      uint16_t *cpus);
static int _task_layout_hostfile(slurm_step_layout_t *step_layout,
				 const char *arbitrary_nodes);

/*
 * slurm_step_layout_create - determine how many tasks of a job will be
 *                    run on each node. Distribution is influenced
 *                    by number of cpus on each host.
 * IN tlist - hostlist corresponding to task layout
 * IN cpus_per_node - cpus per node
 * IN cpu_count_reps - how many nodes have same cpu count
 * IN num_hosts - number of hosts we have
 * IN num_tasks - number of tasks to distribute across these cpus
 * IN cpus_per_task - number of cpus per task
 * IN task_dist - type of distribution we are using
 * IN plane_size - plane size (only needed for the plane distribution)
 * RET a pointer to an slurm_step_layout_t structure
 * NOTE: allocates memory that should be xfreed by caller
 */
slurm_step_layout_t *slurm_step_layout_create(
	const char *tlist,
	uint16_t *cpus_per_node, uint32_t *cpu_count_reps,
	uint32_t num_hosts,
	uint32_t num_tasks,
	uint16_t cpus_per_task,
	uint16_t task_dist,
	uint16_t plane_size)
{
	char *arbitrary_nodes = NULL;
	slurm_step_layout_t *step_layout =
		xmalloc(sizeof(slurm_step_layout_t));
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

	step_layout->task_dist = task_dist;
	if (task_dist == SLURM_DIST_ARBITRARY) {
		hostlist_t hl = NULL;
		char *buf = NULL;
		/* set the node list for the task layout later if user
		 * supplied could be different that the job allocation */
		arbitrary_nodes = xstrdup(tlist);
		hl = hostlist_create(tlist);
		hostlist_uniq(hl);
		buf = hostlist_ranged_string_xmalloc(hl);
		num_hosts = hostlist_count(hl);
		hostlist_destroy(hl);
		step_layout->node_list = buf;
	} else {
		step_layout->node_list = xstrdup(tlist);
	}

	step_layout->task_cnt  = num_tasks;
	if (cluster_flags & CLUSTER_FLAG_FE) {
		/* Limited job step support on front-end systems.
		 * All jobs execute through front-end on Blue Gene.
		 * Normally we would not permit execution of job steps,
		 * but can fake it by just allocating all tasks to
		 * one of the allocated nodes. */
		if ((cluster_flags & CLUSTER_FLAG_BG)
		    || (cluster_flags & CLUSTER_FLAG_CRAY_A))
			step_layout->node_cnt  = num_hosts;
		else
			step_layout->node_cnt  = 1;
	} else
		step_layout->node_cnt  = num_hosts;

	if (_init_task_layout(step_layout, arbitrary_nodes,
			      cpus_per_node, cpu_count_reps,
			      cpus_per_task,
			      task_dist, plane_size) != SLURM_SUCCESS) {
		slurm_step_layout_destroy(step_layout);
		step_layout = NULL;
	}
	xfree(arbitrary_nodes);
	return step_layout;
}

/*
 * fake_slurm_step_layout_create - used when you don't allocate a job from the
 *                    controller does not set up anything
 *                    that should really be used with a switch.
 *                    Or to really lay out tasks any any certain fashion.
 * IN tlist - hostlist corresponding to task layout
 * IN cpus_per_node - cpus per node NULL if no allocation
 * IN cpu_count_reps - how many nodes have same cpu count NULL if no allocation
 * IN node_cnt - number of nodes we have
 * IN task_cnt - number of tasks to distribute across these cpus 0
 *               if using cpus_per_node
 * RET a pointer to an slurm_step_layout_t structure
 * NOTE: allocates memory that should be xfreed by caller
 */
slurm_step_layout_t *fake_slurm_step_layout_create(
	const char *tlist,
	uint16_t *cpus_per_node,
	uint32_t *cpu_count_reps,
	uint32_t node_cnt,
	uint32_t task_cnt)
{
	uint32_t cpn = 1;
	int cpu_cnt = 0, cpu_inx = 0, i, j;
	slurm_step_layout_t *step_layout = NULL;

	if ((node_cnt <= 0) || (task_cnt <= 0 && !cpus_per_node) || !tlist) {
		error("there is a problem with your fake_step_layout request\n"
		      "node_cnt = %u, task_cnt = %u, tlist = %s",
		      node_cnt, task_cnt, tlist);
		return NULL;
	}

	step_layout = xmalloc(sizeof(slurm_step_layout_t));
	step_layout->node_list = xstrdup(tlist);
	step_layout->node_cnt = node_cnt;
	step_layout->tasks = xmalloc(sizeof(uint16_t) * node_cnt);
	step_layout->tids  = xmalloc(sizeof(uint32_t *) * node_cnt);

	step_layout->task_cnt = 0;
	for (i = 0; i < step_layout->node_cnt; i++) {
		if (cpus_per_node && cpu_count_reps) {
			step_layout->tasks[i] = cpus_per_node[cpu_inx];
			step_layout->tids[i] = xmalloc(sizeof(uint32_t) *
						       step_layout->tasks[i]);

			for (j = 0; j < step_layout->tasks[i]; j++)
				step_layout->tids[i][j] =
					step_layout->task_cnt++;

			if ((++cpu_cnt) >= cpu_count_reps[cpu_inx]) {
				/* move to next record */
				cpu_inx++;
				cpu_cnt = 0;
			}
		} else {
			cpn = ((task_cnt - step_layout->task_cnt) +
			       (node_cnt - i) - 1) / (node_cnt - i);
			if (step_layout->task_cnt >= task_cnt) {
				step_layout->tasks[i] = 0;
				step_layout->tids[i] = NULL;
			} else {
				step_layout->tasks[i] = cpn;
				step_layout->tids[i] =
					xmalloc(sizeof(uint32_t) * cpn);

				for (j = 0; j < cpn; j++) {
					step_layout->tids[i][j] =
						step_layout->task_cnt++;
					if (step_layout->task_cnt >= task_cnt) {
						step_layout->tasks[i] = j + 1;
						break;
					}
				}
			}
		}
	}

	return step_layout;
}



/* copys structure for step layout */
extern slurm_step_layout_t *slurm_step_layout_copy(
	slurm_step_layout_t *step_layout)
{
	slurm_step_layout_t *layout;
	int i = 0;
	if (!step_layout)
		return NULL;

	layout = xmalloc(sizeof(slurm_step_layout_t));
	layout->node_list = xstrdup(step_layout->node_list);
	layout->node_cnt = step_layout->node_cnt;
	layout->task_cnt = step_layout->task_cnt;
	layout->task_dist = step_layout->task_dist;

	layout->tasks = xmalloc(sizeof(uint16_t) * layout->node_cnt);
	memcpy(layout->tasks, step_layout->tasks,
	       (sizeof(uint16_t) * layout->node_cnt));

	layout->tids  = xmalloc(sizeof(uint32_t *) * layout->node_cnt);
	for (i = 0; i < layout->node_cnt; i++) {
		layout->tids[i] = xmalloc(sizeof(uint32_t) * layout->tasks[i]);
		memcpy(layout->tids[i], step_layout->tids[i],
		       (sizeof(uint32_t) * layout->tasks[i]));
	}

	return layout;
}

extern void pack_slurm_step_layout(slurm_step_layout_t *step_layout,
				   Buf buffer, uint16_t protocol_version)
{
	uint32_t i = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (step_layout)
			i=1;

		pack16(i, buffer);
		if (!i)
			return;
		packstr(step_layout->front_end, buffer);
		packstr(step_layout->node_list, buffer);
		pack32(step_layout->node_cnt, buffer);
		pack32(step_layout->task_cnt, buffer);
		pack16(step_layout->task_dist, buffer);

		for (i=0; i<step_layout->node_cnt; i++) {
			pack32_array(step_layout->tids[i],
				     step_layout->tasks[i],
				     buffer);
		}
	} else {
		error("pack_slurm_step_layout: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

extern int unpack_slurm_step_layout(slurm_step_layout_t **layout, Buf buffer,
				    uint16_t protocol_version)
{
	uint16_t uint16_tmp;
	uint32_t num_tids, uint32_tmp;
	slurm_step_layout_t *step_layout = NULL;
	int i;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&uint16_tmp, buffer);
		if (!uint16_tmp)
			return SLURM_SUCCESS;

		step_layout = xmalloc(sizeof(slurm_step_layout_t));
		*layout = step_layout;

		safe_unpackstr_xmalloc(&step_layout->front_end,
				       &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&step_layout->node_list,
				       &uint32_tmp, buffer);
		safe_unpack32(&step_layout->node_cnt, buffer);
		safe_unpack32(&step_layout->task_cnt, buffer);
		safe_unpack16(&step_layout->task_dist, buffer);

		step_layout->tasks =
			xmalloc(sizeof(uint32_t) * step_layout->node_cnt);
		step_layout->tids = xmalloc(sizeof(uint32_t *)
					    * step_layout->node_cnt);
		for (i = 0; i < step_layout->node_cnt; i++) {
			safe_unpack32_array(&(step_layout->tids[i]),
					    &num_tids,
					    buffer);
			step_layout->tasks[i] = num_tids;
		}
	} else {
		error("unpack_slurm_step_layout: protocol_version "
		      "%hu not supported", protocol_version);
		goto unpack_error;
	}
	return SLURM_SUCCESS;

unpack_error:
	slurm_step_layout_destroy(step_layout);
	*layout = NULL;
	return SLURM_ERROR;
}

/* destroys structure for step layout */
extern int slurm_step_layout_destroy(slurm_step_layout_t *step_layout)
{
	int i=0;
	if (step_layout) {
		xfree(step_layout->front_end);
		xfree(step_layout->node_list);
		xfree(step_layout->tasks);
		for (i = 0; i < step_layout->node_cnt; i++) {
			xfree(step_layout->tids[i]);
		}
		xfree(step_layout->tids);

		xfree(step_layout);
	}

	return SLURM_SUCCESS;
}

int slurm_step_layout_host_id (slurm_step_layout_t *s, int taskid)
{
	int i, j;
	if (!s->tasks || !s->tids || (taskid > s->task_cnt - 1))
		return SLURM_ERROR;
	for (i=0; i < s->node_cnt; i++)
		for (j=0; j<s->tasks[i]; j++)
			if (s->tids[i][j] == taskid)
				return i;

	return SLURM_ERROR;
}

char *slurm_step_layout_host_name (slurm_step_layout_t *s, int taskid)
{
	int hostid = slurm_step_layout_host_id (s, taskid);

	if (hostid < 0)
		return NULL;

	return nodelist_nth_host(s->node_list, hostid);
}

/* build maps for task layout on nodes */
static int _init_task_layout(slurm_step_layout_t *step_layout,
			     const char *arbitrary_nodes,
			     uint16_t *cpus_per_node, uint32_t *cpu_count_reps,
			     uint16_t cpus_per_task,
			     uint16_t task_dist, uint16_t plane_size)
{
	int cpu_cnt = 0, cpu_inx = 0, i;
	uint32_t cluster_flags = slurmdb_setup_cluster_flags();

/* 	char *name = NULL; */
	uint16_t cpus[step_layout->node_cnt];

	if (step_layout->node_cnt == 0)
		return SLURM_ERROR;
	if (step_layout->tasks)	/* layout already completed */
		return SLURM_SUCCESS;

	if ((int)cpus_per_task < 1 || cpus_per_task == (uint16_t)NO_VAL)
		cpus_per_task = 1;

	step_layout->plane_size = plane_size;

	step_layout->tasks = xmalloc(sizeof(uint16_t)
				     * step_layout->node_cnt);
	step_layout->tids  = xmalloc(sizeof(uint32_t *)
				     * step_layout->node_cnt);
	if (!(cluster_flags & CLUSTER_FLAG_BG)) {
		hostlist_t hl = hostlist_create(step_layout->node_list);
		/* make sure the number of nodes we think we have
		 * is the correct number */
		i = hostlist_count(hl);
		if (step_layout->node_cnt > i)
			step_layout->node_cnt = i;
		hostlist_destroy(hl);
	}
	debug("laying out the %u tasks on %u hosts %s dist %u",
	      step_layout->task_cnt, step_layout->node_cnt,
	      step_layout->node_list, task_dist);
	if (step_layout->node_cnt < 1) {
		error("no hostlist given can't layout tasks");
		return SLURM_ERROR;
	}

	for (i=0; i<step_layout->node_cnt; i++) {
/* 		name = hostlist_shift(hl); */
/* 		if (!name) { */
/* 			error("hostlist incomplete for this job request"); */
/* 			hostlist_destroy(hl); */
/* 			return SLURM_ERROR; */
/* 		} */
/* 		debug2("host %d = %s", i, name); */
/* 		free(name); */
		cpus[i] = (cpus_per_node[cpu_inx] / cpus_per_task);
		if (cpus[i] == 0) {
			/* this can be a result of a heterogeneous allocation
			 * (e.g. 4 cpus on one node and 2 on the second with
			 *  cpus_per_task=3)  */
			cpus[i] = 1;
		}

		if ((plane_size != (uint16_t)NO_VAL)
		    && (task_dist != SLURM_DIST_PLANE)) {
			/* plane_size when dist != plane is used to
			   convey ntasks_per_node. Adjust the number
			   of cpus to reflect that.
			*/
			uint16_t cpus_per_node = plane_size * cpus_per_task;
			if (cpus[i] > cpus_per_node)
				cpus[i] = cpus_per_node;
		}

		//info("got %d cpus", cpus[i]);
		if ((++cpu_cnt) >= cpu_count_reps[cpu_inx]) {
			/* move to next record */
			cpu_inx++;
			cpu_cnt = 0;
		}
	}

        if ((task_dist == SLURM_DIST_CYCLIC) ||
            (task_dist == SLURM_DIST_CYCLIC_CYCLIC) ||
            (task_dist == SLURM_DIST_CYCLIC_CFULL) ||
            (task_dist == SLURM_DIST_CYCLIC_BLOCK))
		return _task_layout_cyclic(step_layout, cpus);
	else if (task_dist == SLURM_DIST_ARBITRARY
		&& !(cluster_flags & CLUSTER_FLAG_FE))
		return _task_layout_hostfile(step_layout, arbitrary_nodes);
        else if (task_dist == SLURM_DIST_PLANE)
                return _task_layout_plane(step_layout, cpus);
	else
		return _task_layout_block(step_layout, cpus);
}

/* use specific set run tasks on each host listed in hostfile
 * XXX: Need to handle over-subscribe.
 */
static int _task_layout_hostfile(slurm_step_layout_t *step_layout,
				 const char *arbitrary_nodes)
{
	int i=0, j, taskid = 0, task_cnt=0;
	hostlist_iterator_t itr = NULL, itr_task = NULL;
	char *host = NULL;
	char *host_task = NULL;
	hostlist_t job_alloc_hosts = NULL;
	hostlist_t step_alloc_hosts = NULL;

	debug2("job list is %s", step_layout->node_list);
	job_alloc_hosts = hostlist_create(step_layout->node_list);
	itr = hostlist_iterator_create(job_alloc_hosts);
	if (!arbitrary_nodes) {
		error("no hostlist given for arbitrary dist");
		return SLURM_ERROR;
	}

	debug2("list is %s", arbitrary_nodes);
	step_alloc_hosts = hostlist_create(arbitrary_nodes);
	if (hostlist_count(step_alloc_hosts) != step_layout->task_cnt) {
		error("Asked for %u tasks have %d in the nodelist.  "
		      "Check your nodelist, or set the -n option to be %d",
		      step_layout->task_cnt,
		      hostlist_count(step_alloc_hosts),
		      hostlist_count(step_alloc_hosts));
		return SLURM_ERROR;
	}
	itr_task = hostlist_iterator_create(step_alloc_hosts);
	while((host = hostlist_next(itr))) {
		step_layout->tasks[i] = 0;
		while((host_task = hostlist_next(itr_task))) {
			if (!strcmp(host, host_task)) {
				step_layout->tasks[i]++;
				task_cnt++;
			}
			free(host_task);
			if (task_cnt >= step_layout->task_cnt)
				break;
		}
		debug3("%s got %u tasks", host, step_layout->tasks[i]);
		if (step_layout->tasks[i] == 0)
			goto reset_hosts;
		step_layout->tids[i] = xmalloc(sizeof(uint32_t)
					       * step_layout->tasks[i]);
		taskid = 0;
		j = 0;
		hostlist_iterator_reset(itr_task);
		while((host_task = hostlist_next(itr_task))) {
			if (!strcmp(host, host_task)) {
				step_layout->tids[i][j] = taskid;
				j++;
			}
			taskid++;
			free(host_task);
			if (j >= step_layout->tasks[i])
				break;
		}
		i++;
	reset_hosts:
		hostlist_iterator_reset(itr_task);
		free(host);
		if (i > step_layout->task_cnt)
			break;
	}
	hostlist_iterator_destroy(itr);
	hostlist_iterator_destroy(itr_task);
	hostlist_destroy(job_alloc_hosts);
	hostlist_destroy(step_alloc_hosts);
	if (task_cnt != step_layout->task_cnt) {
		error("Asked for %u tasks but placed %d. Check your nodelist",
		      step_layout->task_cnt, task_cnt);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _task_layout_block(slurm_step_layout_t *step_layout, uint16_t *cpus)
{
	static uint16_t select_params = (uint16_t) NO_VAL;
	int i, j, task_id = 0;

	if (select_params == (uint16_t) NO_VAL)
		select_params = slurm_get_select_type_param();

	if (select_params & CR_PACK_NODES) {
		/* Pass 1: Put one task on each node */
		for (i = 0; ((i < step_layout->node_cnt) &&
			     (task_id < step_layout->task_cnt)); i++) {
			if (step_layout->tasks[i] < cpus[i]) {
				step_layout->tasks[i]++;
				task_id++;
			}
		}

		/* Pass 2: Fill remaining CPUs on a node-by-node basis */
		for (i = 0; ((i < step_layout->node_cnt) &&
			     (task_id < step_layout->task_cnt)); i++) {
			while ((step_layout->tasks[i] < cpus[i]) &&
			       (task_id < step_layout->task_cnt)) {
				step_layout->tasks[i]++;
				task_id++;
			}
		}

		/* Pass 3: Spread remaining tasks across all nodes */
		while (task_id < step_layout->task_cnt) {
			for (i = 0; ((i < step_layout->node_cnt) &&
				     (task_id < step_layout->task_cnt)); i++) {
				step_layout->tasks[i]++;
				task_id++;
			}
		}
	} else {
		/* To effectively deal with heterogeneous nodes, we fake a
		 * cyclic distribution to determine how many tasks go on each
		 * node and then make those assignments in a block fashion. */
		bool over_subscribe = false;
		for (j = 0; task_id < step_layout->task_cnt; j++) {
			bool space_remaining = false;
			for (i = 0; ((i < step_layout->node_cnt) &&
				     (task_id < step_layout->task_cnt)); i++) {
				if ((j < cpus[i]) || over_subscribe) {
					step_layout->tasks[i]++;
					task_id++;
					if ((j + 1) < cpus[i])
						space_remaining = true;
				}
			}
			if (!space_remaining)
				over_subscribe = true;
		}
	}

	/* Now distribute the tasks */
	task_id = 0;
	for (i = 0; i < step_layout->node_cnt; i++) {
		step_layout->tids[i] = xmalloc(sizeof(uint32_t)
					       * step_layout->tasks[i]);
		for (j = 0; j < step_layout->tasks[i]; j++) {
			step_layout->tids[i][j] = task_id;
			task_id++;
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
static int _task_layout_cyclic(slurm_step_layout_t *step_layout,
			       uint16_t *cpus)
{
	int i, j, taskid = 0;
	bool over_subscribe = false;

	for (j=0; taskid<step_layout->task_cnt; j++) {   /* cycle counter */
		bool space_remaining = false;
		for (i=0; ((i<step_layout->node_cnt)
			   && (taskid<step_layout->task_cnt)); i++) {
			if ((j<cpus[i]) || over_subscribe) {
				xrealloc(step_layout->tids[i], sizeof(uint32_t)
					 * (step_layout->tasks[i] + 1));

				step_layout->tids[i][step_layout->tasks[i]] =
					taskid;
				taskid++;
				step_layout->tasks[i]++;
				if ((j+1) < cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}
	return SLURM_SUCCESS;
}


/*
 * The plane distribution results in a block cyclic of block size
 * "plane_size".
 * To effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many tasks go on each node and
 * then make the assignments of task numbers to nodes using the
 * user-specified plane size.
 * For example:
 *	plane_size = 2, #tasks = 6, #nodes = 3
 *
 * Node#:              Node0 Node1 Node2
 *                     ----- ----- -----
 * #of allocated CPUs:   4     1     1
 *
 * task distribution:   0  1   2     3
 *                      4  5
 */
static int _task_layout_plane(slurm_step_layout_t *step_layout,
			      uint16_t *cpus)
{
	int i, j, k, taskid = 0;
	bool over_subscribe = false;
	uint32_t cur_task[step_layout->node_cnt];

	debug3("_task_layout_plane plane_size %u node_cnt %u task_cnt %u",
	       step_layout->plane_size,
	       step_layout->node_cnt, step_layout->task_cnt);

	if (step_layout->plane_size <= 0)
	        return SLURM_ERROR;

	if (step_layout->tasks == NULL)
		return SLURM_ERROR;

	/* figure out how many tasks go to each node */
	for (j=0; taskid<step_layout->task_cnt; j++) {   /* cycle counter */
		bool space_remaining = false;
		for (i=0; ((i<step_layout->node_cnt)
			   && (taskid<step_layout->task_cnt)); i++) {
			if ((j<cpus[i]) || over_subscribe) {
				taskid++;
				step_layout->tasks[i]++;
				if ((j+1) < cpus[i])
					space_remaining = true;
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}

	/* now distribute the tasks */
	taskid = 0;
	for (i=0; i < step_layout->node_cnt; i++) {
	    step_layout->tids[i] = xmalloc(sizeof(uint32_t)
				           * step_layout->tasks[i]);
	    cur_task[i] = 0;
	}
	for (j=0; taskid<step_layout->task_cnt; j++) {   /* cycle counter */
		for (i=0; ((i<step_layout->node_cnt)
			   && (taskid<step_layout->task_cnt)); i++) {
			/* assign a block of 'plane_size' tasks to this node */
			for (k=0; ((k<step_layout->plane_size)
				   && (cur_task[i] < step_layout->tasks[i])
				   && (taskid < step_layout->task_cnt)); k++) {
				step_layout->tids[i][cur_task[i]] = taskid;
				taskid++;
				cur_task[i]++;
			}
		}
	}

	if (taskid != step_layout->task_cnt) {
		error("_task_layout_plane: Mismatch in task count (%d != %d) ",
		      taskid, step_layout->task_cnt);
		return SLURM_ERROR;
	}

#if (0)
	/* debugging only */
	for (i=0; i < step_layout->node_cnt; i++) {
		info("tasks[%d]: %u", i, step_layout->tasks[i]);
	}

	for (i=0; i < step_layout->node_cnt; i++) {
		info ("Host %d _plane_ # of tasks %u", i, step_layout->tasks[i]);
		for (j=0; j<step_layout->tasks[i]; j++) {
			info ("Host %d _plane_ localid %d taskid %u",
			      i, j, step_layout->tids[i][j]);
		}
	}
#endif

	return SLURM_SUCCESS;
}

extern char *slurm_step_layout_type_name(task_dist_states_t task_dist)
{
	switch(task_dist) {
	case SLURM_DIST_CYCLIC:
		return "Cyclic";
		break;
	case SLURM_DIST_BLOCK:	/* distribute tasks filling node by node */
		return "Block";
		break;
	case SLURM_DIST_ARBITRARY:	/* arbitrary task distribution  */
		return "Arbitrary";
		break;
	case SLURM_DIST_PLANE:	/* distribute tasks by filling up
				   planes of lllp first and then by
				   going across the nodes See
				   documentation for more
				   information */
		return "Plane";
		break;
	case SLURM_DIST_CYCLIC_CYCLIC:/* distribute tasks 1 per node:
					 round robin: same for lowest
					 level of logical processor (lllp) */
		return "CCyclic";
		break;
	case SLURM_DIST_CYCLIC_BLOCK: /* cyclic for node and block for lllp  */
		return "CBlock";
		break;
	case SLURM_DIST_BLOCK_CYCLIC: /* block for node and cyclic for lllp  */
		return "BCyclic";
		break;
	case SLURM_DIST_BLOCK_BLOCK:	/* block for node and block for lllp  */
		return "BBlock";
		break;
	case SLURM_DIST_CYCLIC_CFULL:	/* cyclic for node and full
					 * cyclic for lllp  */
		return "CFCyclic";
		break;
	case SLURM_DIST_BLOCK_CFULL:	/* block for node and full
					 * cyclic for lllp  */
		return "BFCyclic";
		break;
	case SLURM_NO_LLLP_DIST:	/* No distribution specified for lllp */
	case SLURM_DIST_UNKNOWN:
	default:
		return "Unknown";

	}
}
