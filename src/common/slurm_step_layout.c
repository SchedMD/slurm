/*****************************************************************************\
 *  slurm_step_layout.c - functions to distribute tasks over nodes.
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes, <cholmes@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  This file is patterned after hostlist.c, written by Mark Grondona and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/interfaces/select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_step_layout.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(pack_slurm_step_layout, slurm_pack_slurm_step_layout);
strong_alias(unpack_slurm_step_layout, slurm_unpack_slurm_step_layout);

/* build maps for task layout on nodes */
static int _init_task_layout(slurm_step_layout_req_t *step_layout_req,
			     slurm_step_layout_t *step_layout,
			     const char *arbitrary_nodes);

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
 * IN step_layout_req - information needed for task distibutionhostlist corresponding to task layout
 * RET a pointer to an slurm_step_layout_t structure
 * NOTE: allocates memory that should be xfreed by caller
 */
slurm_step_layout_t *slurm_step_layout_create(
	slurm_step_layout_req_t *step_layout_req)
{
	char *arbitrary_nodes = NULL;
	slurm_step_layout_t *step_layout =
		xmalloc(sizeof(slurm_step_layout_t));

	step_layout->task_dist = step_layout_req->task_dist;
	if ((step_layout->task_dist & SLURM_DIST_STATE_BASE)
	    == SLURM_DIST_ARBITRARY) {
		hostlist_t *hl = NULL;
		char *buf = NULL;
		/* set the node list for the task layout later if user
		 * supplied could be different that the job allocation */
		arbitrary_nodes = xstrdup(step_layout_req->node_list);
		hl = hostlist_create(step_layout_req->node_list);
		hostlist_uniq(hl);
		buf = hostlist_ranged_string_xmalloc(hl);
		step_layout_req->num_hosts = hostlist_count(hl);
		hostlist_destroy(hl);
		step_layout->node_list = buf;
	} else {
		step_layout->node_list = xstrdup(step_layout_req->node_list);
	}

	step_layout->task_cnt  = step_layout_req->num_tasks;
	step_layout->node_cnt = step_layout_req->num_hosts;

	if (_init_task_layout(step_layout_req, step_layout, arbitrary_nodes)
	    != SLURM_SUCCESS) {
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
extern slurm_step_layout_t *fake_slurm_step_layout_create(
	const char *tlist,
	uint16_t *cpus_per_node,
	uint32_t *cpu_count_reps,
	uint32_t node_cnt,
	uint32_t task_cnt,
	uint16_t protocol_version)
{
	uint32_t cpn = 1;
	int cpu_cnt = 0, cpu_inx = 0, i, j;
	slurm_step_layout_t *step_layout = NULL;

	if (!node_cnt || !tlist ||
	    (!cpus_per_node && (!task_cnt || (task_cnt == NO_VAL)))) {
		error("there is a problem with your fake_step_layout request\n"
		      "node_cnt = %u, task_cnt = %u, tlist = %s",
		      node_cnt, task_cnt, tlist);
		return NULL;
	}

	step_layout = xmalloc(sizeof(slurm_step_layout_t));
	step_layout->node_list = xstrdup(tlist);
	step_layout->node_cnt = node_cnt;
	step_layout->start_protocol_ver = protocol_version;
	step_layout->tasks = xcalloc(node_cnt, sizeof(uint16_t));
	step_layout->tids = xcalloc(node_cnt, sizeof(uint32_t *));

	step_layout->task_cnt = 0;
	for (i = 0; i < step_layout->node_cnt; i++) {
		if (cpus_per_node && cpu_count_reps) {
			step_layout->tasks[i] = cpus_per_node[cpu_inx];
			step_layout->tids[i] = xcalloc(step_layout->tasks[i],
						       sizeof(uint32_t));

			for (j = 0; j < step_layout->tasks[i]; j++)
				step_layout->tids[i][j] =
					step_layout->task_cnt++;

			if ((++cpu_cnt) >= cpu_count_reps[cpu_inx]) {
				/* move to next record */
				cpu_inx++;
				cpu_cnt = 0;
			}
		} else {
			cpn = ROUNDUP((task_cnt - step_layout->task_cnt),
				      (node_cnt - i));
			if (step_layout->task_cnt >= task_cnt) {
				step_layout->tasks[i] = 0;
				step_layout->tids[i] = NULL;
			} else {
				step_layout->tasks[i] = cpn;
				step_layout->tids[i] =
					xcalloc(cpn, sizeof(uint32_t));

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



/* copies structure for step layout */
extern slurm_step_layout_t *slurm_step_layout_copy(
	slurm_step_layout_t *step_layout)
{
	slurm_step_layout_t *layout;
	int i = 0;
	if (!step_layout)
		return NULL;

	layout = xmalloc(sizeof(slurm_step_layout_t));
	if (step_layout->alias_addrs) {
		layout->alias_addrs = xmalloc(sizeof(slurm_node_alias_addrs_t));
		slurm_copy_node_alias_addrs_members(layout->alias_addrs,
						    step_layout->alias_addrs);
	}
	layout->node_list = xstrdup(step_layout->node_list);
	layout->node_cnt = step_layout->node_cnt;
	layout->start_protocol_ver = step_layout->start_protocol_ver;
	layout->task_cnt = step_layout->task_cnt;
	layout->task_dist = step_layout->task_dist;

	layout->tasks = xcalloc(layout->node_cnt, sizeof(uint16_t));
	memcpy(layout->tasks, step_layout->tasks,
	       (sizeof(uint16_t) * layout->node_cnt));
	if (step_layout->cpt_compact_cnt) {
		uint32_t cnt = step_layout->cpt_compact_cnt;

		layout->cpt_compact_cnt = cnt;
		layout->cpt_compact_array =
			xcalloc(cnt, sizeof(*layout->cpt_compact_array));
		memcpy(layout->cpt_compact_array,
		       step_layout->cpt_compact_array,
		       (sizeof(*layout->cpt_compact_array) * cnt));

		layout->cpt_compact_reps =
			xcalloc(cnt, sizeof(*layout->cpt_compact_reps));
		memcpy(layout->cpt_compact_reps,
		       step_layout->cpt_compact_reps,
		       (sizeof(*layout->cpt_compact_reps) * cnt));

	}

	layout->tids = xcalloc(layout->node_cnt, sizeof(uint32_t *));
	for (i = 0; i < layout->node_cnt; i++) {
		layout->tids[i] = xcalloc(layout->tasks[i], sizeof(uint32_t));
		memcpy(layout->tids[i], step_layout->tids[i],
		       (sizeof(uint32_t) * layout->tasks[i]));
	}

	return layout;
}

extern void slurm_step_layout_merge(slurm_step_layout_t *step_layout1,
				    slurm_step_layout_t *step_layout2)
{
	hostlist_t *hl, *hl2;
	hostlist_iterator_t *host_itr;
	int new_pos = 0, node_task_cnt;
	char *host;

	xassert(step_layout1);
	xassert(step_layout2);

	/*
	 * cpt_compact* fields are currently not used by the clients who issue
	 * the RPC that calls this function. So, we currently do not merge
	 * the cpt_compact* fields.
	 */

	hl = hostlist_create(step_layout1->node_list);
	hl2 = hostlist_create(step_layout2->node_list);

	host_itr = hostlist_iterator_create(hl2);
	while ((host = hostlist_next(host_itr))) {
		int pos = hostlist_find(hl, host);

		if (pos == -1) {
			/* If the host doesn't exist push it on the end */
			hostlist_push_host(hl, host);
			pos = step_layout1->node_cnt++;
			xrecalloc(step_layout1->tasks,
				  step_layout1->node_cnt,
				  sizeof(uint16_t));
			xrecalloc(step_layout1->tids,
				  step_layout1->node_cnt,
				  sizeof(uint32_t *));
		}
		free(host);

		/* set the end position of the array */
		node_task_cnt = step_layout1->tasks[pos];
		step_layout1->tasks[pos] +=
			step_layout2->tasks[new_pos];
		xrecalloc(step_layout1->tids[pos],
			  step_layout1->tasks[pos],
			  sizeof(uint32_t));
		for (int i = 0; i < step_layout2->tasks[new_pos]; i++) {
			step_layout1->tids[pos][node_task_cnt++] =
				step_layout2->tids[new_pos][i];
		}
		new_pos++;
	}
	hostlist_iterator_destroy(host_itr);

	/* Don't need to merge alias_addrs it is per-job */
	step_layout1->task_cnt += step_layout2->task_cnt;
	xfree(step_layout1->node_list);
	step_layout1->node_list = hostlist_ranged_string_xmalloc(hl);
	hostlist_destroy(hl);
	hostlist_destroy(hl2);
}

extern void pack_slurm_step_layout(slurm_step_layout_t *step_layout,
				   buf_t *buffer, uint16_t protocol_version)
{
	uint32_t i = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (step_layout)
			i = 1;

		pack16(i, buffer);
		if (!i)
			return;
		packnull(buffer);
		packstr(step_layout->node_list, buffer);
		pack32(step_layout->node_cnt, buffer);
		pack16(step_layout->start_protocol_ver, buffer);
		pack32(step_layout->task_cnt, buffer);
		pack32(step_layout->task_dist, buffer);

		for (i = 0; i < step_layout->node_cnt; i++) {
			pack32_array(step_layout->tids[i],
				     step_layout->tasks[i],
				     buffer);
		}

		pack16_array(step_layout->cpt_compact_array,
			     step_layout->cpt_compact_cnt, buffer);
		pack32_array(step_layout->cpt_compact_reps,
			     step_layout->cpt_compact_cnt, buffer);

		if (step_layout->alias_addrs) {
			char *tmp_str =
				create_net_cred(step_layout->alias_addrs,
						protocol_version);
			packstr(tmp_str, buffer);
			xfree(tmp_str);
		} else {
			packnull(buffer);
		}
	} else {
		error("%s: protocol_version %hu not supported",
		      __func__, protocol_version);
	}
}

extern int unpack_slurm_step_layout(slurm_step_layout_t **layout, buf_t *buffer,
				    uint16_t protocol_version)
{
	uint16_t uint16_tmp;
	uint32_t num_tids, uint32_tmp;
	slurm_step_layout_t *step_layout = NULL;
	int i;
	char *tmp_str = NULL;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&uint16_tmp, buffer);
		if (!uint16_tmp)
			return SLURM_SUCCESS;

		step_layout = xmalloc(sizeof(slurm_step_layout_t));
		*layout = step_layout;

		safe_skipstr(buffer);
		safe_unpackstr(&step_layout->node_list, buffer);
		safe_unpack32(&step_layout->node_cnt, buffer);
		safe_unpack16(&step_layout->start_protocol_ver, buffer);
		safe_unpack32(&step_layout->task_cnt, buffer);
		safe_unpack32(&step_layout->task_dist, buffer);

		safe_xcalloc(step_layout->tasks, step_layout->node_cnt,
			     sizeof(uint32_t));
		safe_xcalloc(step_layout->tids, step_layout->node_cnt,
			     sizeof(uint32_t *));
		for (i = 0; i < step_layout->node_cnt; i++) {
			safe_unpack32_array(&(step_layout->tids[i]),
					    &num_tids,
					    buffer);
			step_layout->tasks[i] = num_tids;
		}
		safe_unpack16_array(&step_layout->cpt_compact_array,
				    &step_layout->cpt_compact_cnt, buffer);
		safe_unpack32_array(&step_layout->cpt_compact_reps,
				    &uint32_tmp, buffer);
		xassert(uint32_tmp == step_layout->cpt_compact_cnt);

		safe_unpackstr(&tmp_str, buffer);
		if (tmp_str) {
			step_layout->alias_addrs =
				extract_net_cred(tmp_str, protocol_version);
			if (!step_layout->alias_addrs) {
				xfree(tmp_str);
				goto unpack_error;
			}
			step_layout->alias_addrs->net_cred = tmp_str;
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
		slurm_free_node_alias_addrs(step_layout->alias_addrs);
		xfree(step_layout->node_list);
		xfree(step_layout->tasks);
		xfree(step_layout->cpt_compact_array);
		xfree(step_layout->cpt_compact_reps);
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
	for (i = 0; i < s->node_cnt; i++)
		for (j = 0; j < s->tasks[i]; j++)
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
static int _init_task_layout(slurm_step_layout_req_t *step_layout_req,
			     slurm_step_layout_t *step_layout,
			     const char *arbitrary_nodes)
{
	int cpu_cnt = 0, cpu_inx = 0, cpu_task_cnt = 0, cpu_task_inx = 0, i;
	hostlist_t *hl;

	uint16_t cpus[step_layout->node_cnt];
	uint16_t cpus_per_task[1];
	uint32_t cpus_task_reps[1];

	if (step_layout->node_cnt == 0)
		return SLURM_ERROR;
	if (step_layout->tasks)	/* layout already completed */
		return SLURM_SUCCESS;

	if (!step_layout_req->cpus_per_task) {
		cpus_per_task[0] = 1;
		cpus_task_reps[0] = step_layout_req->num_hosts;
		step_layout_req->cpus_per_task = cpus_per_task;
		step_layout_req->cpus_task_reps = cpus_task_reps;
	}

	if (((int)step_layout_req->cpus_per_task[0] < 1) ||
	    (step_layout_req->cpus_per_task[0] == NO_VAL16)) {
		step_layout_req->cpus_per_task[0] = 1;
		step_layout_req->cpus_task_reps[0] = step_layout_req->num_hosts;
	}

	step_layout->plane_size = step_layout_req->plane_size;

	step_layout->tasks = xcalloc(step_layout->node_cnt, sizeof(uint16_t));
	step_layout->tids = xcalloc(step_layout->node_cnt, sizeof(uint32_t *));
	hl = hostlist_create(step_layout->node_list);
	/* make sure the number of nodes we think we have
	 * is the correct number */
	i = hostlist_count(hl);
	if (step_layout->node_cnt > i)
		step_layout->node_cnt = i;
	hostlist_destroy(hl);

	debug("laying out the %u tasks on %u hosts %s dist %u",
	      step_layout->task_cnt, step_layout->node_cnt,
	      step_layout->node_list, step_layout->task_dist);
	if (step_layout->node_cnt < 1) {
		error("no hostlist given can't layout tasks");
		return SLURM_ERROR;
	}

	/* hostlist_t *hl = hostlist_create(step_layout->node_list); */
	for (i=0; i<step_layout->node_cnt; i++) {
		/* char *name = hostlist_shift(hl); */
		/* if (!name) { */
		/* 	error("hostlist incomplete for this job request"); */
		/* 	hostlist_destroy(hl); */
		/* 	return SLURM_ERROR; */
		/* } */
		/* debug2("host %d = %s", i, name); */
		/* free(name); */
		cpus[i] = (step_layout_req->cpus_per_node[cpu_inx] /
			   step_layout_req->cpus_per_task[cpu_task_inx]);
		if (cpus[i] == 0) {
			/* this can be a result of a heterogeneous allocation
			 * (e.g. 4 cpus on one node and 2 on the second with
			 *  step_layout_req->cpus_per_task=3)  */
			cpus[i] = 1;
		}

		if (step_layout->plane_size &&
		    (step_layout->plane_size != NO_VAL16) &&
		    ((step_layout->task_dist & SLURM_DIST_STATE_BASE)
		     != SLURM_DIST_PLANE)) {
			/* plane_size when dist != plane is used to
			   convey ntasks_per_node. Adjust the number
			   of cpus to reflect that.
			*/
			uint16_t cpus_per_node =
				step_layout->plane_size *
				step_layout_req->cpus_per_task[cpu_task_inx];
			if (cpus[i] > cpus_per_node)
				cpus[i] = cpus_per_node;
		}

		/* info("got %d cpus", cpus[i]); */
		if ((++cpu_cnt) >=
		    step_layout_req->cpu_count_reps[cpu_inx]) {
			/* move to next record */
			cpu_inx++;
			cpu_cnt = 0;
		}

		if ((++cpu_task_cnt) >=
		    step_layout_req->cpus_task_reps[cpu_task_inx]) {
			/* move to next record */
			cpu_task_inx++;
			cpu_task_cnt = 0;
		}
	}

	if ((step_layout->task_dist & SLURM_DIST_NODEMASK)
	    == SLURM_DIST_NODECYCLIC)
		return _task_layout_cyclic(step_layout, cpus);
	else if ((step_layout->task_dist & SLURM_DIST_STATE_BASE)
		 == SLURM_DIST_ARBITRARY)
		return _task_layout_hostfile(step_layout, arbitrary_nodes);
	else if ((step_layout->task_dist & SLURM_DIST_STATE_BASE)
		 == SLURM_DIST_PLANE)
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
	hostlist_iterator_t *itr = NULL, *itr_task = NULL;
	char *host = NULL;

	hostlist_t *job_alloc_hosts = NULL;
	hostlist_t *step_alloc_hosts = NULL;

	int step_inx = 0, step_hosts_cnt = 0;
	node_record_t **step_hosts_ptrs = NULL;
	node_record_t *host_ptr = NULL;

	debug2("job list is %s", step_layout->node_list);
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
		hostlist_destroy(step_alloc_hosts);
		return SLURM_ERROR;
	}

	job_alloc_hosts = hostlist_create(step_layout->node_list);
	itr             = hostlist_iterator_create(job_alloc_hosts);
	itr_task        = hostlist_iterator_create(step_alloc_hosts);

	/*
	 * Build array of pointers so that we can do pointer comparisons rather
	 * than strcmp's on nodes.
	 */
	step_hosts_cnt  = hostlist_count(step_alloc_hosts);
	step_hosts_ptrs = xcalloc(step_hosts_cnt,
				  sizeof(node_record_t *));

	if (!running_in_daemon()) {
		/* running in salloc - init node records */
		init_node_conf();
		build_all_nodeline_info(false, 0);
		rehash_node();
	}

	step_inx = 0;
	while((host = hostlist_next(itr_task))) {
		step_hosts_ptrs[step_inx++] = find_node_record_no_alias(host);
		free(host);
	}

	while((host = hostlist_next(itr))) {
		host_ptr = find_node_record(host);
		step_layout->tasks[i] = 0;

		for (step_inx = 0; step_inx < step_hosts_cnt; step_inx++) {
			if (host_ptr == step_hosts_ptrs[step_inx]) {
				step_layout->tasks[i]++;
				task_cnt++;
			}
			if (task_cnt >= step_layout->task_cnt)
				break;
		}
		debug3("%s got %u tasks", host, step_layout->tasks[i]);
		if (step_layout->tasks[i] == 0)
			goto reset_hosts;
		step_layout->tids[i] = xcalloc(step_layout->tasks[i],
					       sizeof(uint32_t));
		taskid = 0;
		j = 0;

		for (step_inx = 0; step_inx < step_hosts_cnt; step_inx++) {
			if (host_ptr == step_hosts_ptrs[step_inx]) {
				step_layout->tids[i][j] = taskid;
				j++;
			}
			taskid++;
			if (j >= step_layout->tasks[i])
				break;
		}
		i++;
	reset_hosts:
		free(host);
		if (i > step_layout->task_cnt)
			break;
	}
	hostlist_iterator_destroy(itr);
	hostlist_iterator_destroy(itr_task);
	hostlist_destroy(job_alloc_hosts);
	hostlist_destroy(step_alloc_hosts);
	xfree(step_hosts_ptrs);

	if (task_cnt != step_layout->task_cnt) {
		error("Asked for %u tasks but placed %d. Check your nodelist",
		      step_layout->task_cnt, task_cnt);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _task_layout_block(slurm_step_layout_t *step_layout, uint16_t *cpus)
{
	static uint16_t select_params = NO_VAL16;
	int i, j, task_id = 0;
	bool pack_nodes;

	if (select_params == NO_VAL16)
		select_params = slurm_conf.select_type_param;
	if (step_layout->task_dist & SLURM_DIST_PACK_NODES)
		pack_nodes = true;
	else if (step_layout->task_dist & SLURM_DIST_NO_PACK_NODES)
		pack_nodes = false;
	else if (select_params & SELECT_PACK_NODES)
		pack_nodes = true;
	else
		pack_nodes = false;

	if (pack_nodes) {
		/* Pass 1: Put one task on each node */
		for (i = 0; ((i < step_layout->node_cnt) &&
			     (task_id < step_layout->task_cnt)); i++) {
			/* cpus has already been altered for cpus_per_task */
			if (step_layout->tasks[i] < cpus[i]) {
				step_layout->tasks[i]++;
				task_id++;
			}
		}

		/* Pass 2: Fill remaining CPUs on a node-by-node basis */
		for (i = 0; ((i < step_layout->node_cnt) &&
			     (task_id < step_layout->task_cnt)); i++) {
			/* cpus has already been altered for cpus_per_task */
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
		step_layout->tids[i] = xcalloc(step_layout->tasks[i],
					       sizeof(uint32_t));
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
	int i, j, max_over_subscribe = 0, taskid = 0, total_cpus = 0;
	bool over_subscribe = false;

	for (i = 0; i < step_layout->node_cnt; i++)
		total_cpus += cpus[i];
	if (total_cpus < step_layout->task_cnt) {
		over_subscribe = true;
		i = step_layout->task_cnt - total_cpus;
		max_over_subscribe = ROUNDUP(i, step_layout->node_cnt);
	}

	for (j=0; taskid<step_layout->task_cnt; j++) {   /* cycle counter */
		bool space_remaining = false;
		for (i=0; ((i<step_layout->node_cnt)
			   && (taskid<step_layout->task_cnt)); i++) {
			if ((j < cpus[i]) ||
			    (over_subscribe &&
			     (j < (cpus[i] + max_over_subscribe)))) {
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
	int plane_start = 0;

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
		/* place one task on each node first */
		if (j == 0) {
			for (i = 0; ((i < step_layout->node_cnt) &&
				     (taskid < step_layout->task_cnt)); i++) {
				taskid++;
				step_layout->tasks[i]++;
			}
		}
		for (i = 0; ((i < step_layout->node_cnt) &&
			     (taskid < step_layout->task_cnt)); i++) {
			/* handle placing first task on each node */
			if (j == 0)
				plane_start = 1;
			else
				plane_start = 0;
			for (k = plane_start; (k < step_layout->plane_size) &&
				     (taskid < step_layout->task_cnt); k++) {
				if ((cpus[i] - step_layout->tasks[i]) ||
				    over_subscribe) {
					taskid++;
					step_layout->tasks[i]++;
					if (cpus[i] - (step_layout->tasks[i]
						       + 1) >= 0)
						space_remaining = true;
				}
			}
		}
		if (!space_remaining)
			over_subscribe = true;
	}

	/* now distribute the tasks */
	taskid = 0;
	for (i=0; i < step_layout->node_cnt; i++) {
		step_layout->tids[i] = xcalloc(step_layout->tasks[i],
					       sizeof(uint32_t));
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

typedef struct {
	task_dist_states_t task_dist;
	const char *string;
} layout_type_name_t;

static const layout_type_name_t layout_type_names[] = {
	{ SLURM_DIST_CYCLIC, "Cyclic" },
	 /* distribute tasks filling node by node */
	{ SLURM_DIST_BLOCK, "Block" },
	/* arbitrary task distribution  */
	{ SLURM_DIST_ARBITRARY, "arbitrary task distribution" },
	/*
	 * distribute tasks by filling up planes of lllp first and then by
	 * going across the nodes See documentation for more information
	 */
	{ SLURM_DIST_PLANE, "Plane" },
	/*
	 * distribute tasks 1 per node: round robin: same for lowest
	 * level of logical processor (lllp)
	 */
	{ SLURM_DIST_CYCLIC_CYCLIC, "CCyclic" },
	/* cyclic for node and block for lllp  */
	{ SLURM_DIST_CYCLIC_BLOCK, "CBlock" },
	/* block for node and cyclic for lllp  */
	{ SLURM_DIST_BLOCK_CYCLIC, "BCyclic" },
	/* block for node and block for lllp  */
	{ SLURM_DIST_BLOCK_BLOCK, "BBlock" },
	/* cyclic for node and full cyclic for lllp  */
	{ SLURM_DIST_CYCLIC_CFULL, "CFCyclic" },
	/* block for node and full cyclic for lllp  */
	{ SLURM_DIST_BLOCK_CFULL, "BFCyclic" },
	{ SLURM_DIST_CYCLIC_CYCLIC_CYCLIC, "CCyclicCyclic" },
	{ SLURM_DIST_CYCLIC_CYCLIC_BLOCK, "CCyclicBlock" },
	{ SLURM_DIST_CYCLIC_CYCLIC_CFULL, "CCyclicFCyclic" },
	{ SLURM_DIST_CYCLIC_BLOCK_CYCLIC, "CBlockCyclic" },
	{ SLURM_DIST_CYCLIC_BLOCK_BLOCK, "CBlockBlock" },
	{ SLURM_DIST_CYCLIC_BLOCK_CFULL, "CCyclicFCyclic" },
	{ SLURM_DIST_CYCLIC_CFULL_CYCLIC, "CFCyclicCyclic" },
	{ SLURM_DIST_CYCLIC_CFULL_BLOCK, "CFCyclicBlock" },
	{ SLURM_DIST_CYCLIC_CFULL_CFULL, "CFCyclicFCyclic" },
	{ SLURM_DIST_BLOCK_CYCLIC_CYCLIC, "BCyclicCyclic" },
	{ SLURM_DIST_BLOCK_CYCLIC_BLOCK, "BCyclicBlock" },
	{ SLURM_DIST_BLOCK_CYCLIC_CFULL, "BCyclicFCyclic" },
	{ SLURM_DIST_BLOCK_BLOCK_CYCLIC, "BBlockCyclic" },
	{ SLURM_DIST_BLOCK_BLOCK_BLOCK, "BBlockBlock" },
	{ SLURM_DIST_BLOCK_BLOCK_CFULL, "BBlockFCyclic" },
	{ SLURM_DIST_BLOCK_CFULL_CYCLIC, "BFCyclicCyclic" },
	{ SLURM_DIST_BLOCK_CFULL_BLOCK, "BFCyclicBlock" },
	{ SLURM_DIST_BLOCK_CFULL_CFULL, "BFCyclicFCyclic" },
	{ 0 }
};

extern char *slurm_step_layout_type_name(task_dist_states_t task_dist)
{
	char *name = NULL, *pos = NULL;

	for (int i = 0; layout_type_names[i].task_dist; i++) {
		if (layout_type_names[i].task_dist ==
		    (task_dist & SLURM_DIST_STATE_BASE)) {
			xstrfmtcatat(name, &pos, "%s",
				     layout_type_names[i].string);
			break;
		}
	}

	if (!name) {
		/* SLURM_DIST_UNKNOWN - No distribution specified */
		xstrfmtcatat(name, &pos, "%s", "Unknown");
	}

	if (task_dist & SLURM_DIST_PACK_NODES)
		xstrfmtcatat(name, &pos, ",%s", "Pack");

	if (task_dist & SLURM_DIST_NO_PACK_NODES)
		xstrfmtcatat(name, &pos, ",%s", "NoPack");

	xassert(pos);
	return name;
}
