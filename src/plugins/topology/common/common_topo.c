/*****************************************************************************\
 *  common_topo.c - common functions for accounting storage
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/slurm_xlator.h"

#include "common_topo.h"
#include "eval_nodes.h"

#include "src/common/bitstring.h"
#include "src/common/core_array.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

/*
 * These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined. They will get
 * overwritten when linking with the slurmctld.
 */

#if defined (__APPLE__)
extern list_t *part_list __attribute__((weak_import));
extern bitstr_t *idle_node_bitmap __attribute__((weak_import));
#else
list_t *part_list = NULL;
bitstr_t *idle_node_bitmap;
#endif

typedef struct {
	int *count;
	int depth;
	bitstr_t *fwd_bitmap;
	int msg_count;
	bitstr_t *nodes_bitmap;
	hostlist_t ***sp_hl;
	uint16_t tree_width;
} _foreach_part_split_hostlist_t;

static int _split_hostlist_treewidth(hostlist_t *hl, hostlist_t ***sp_hl,
				     int *count, uint16_t tree_width);

static int _part_split_hostlist(void *x, void *y)
{
	part_record_t *part_ptr = x;
	_foreach_part_split_hostlist_t *arg = y;
	int fwd_count, hl_count, hl_depth;
	hostlist_t *hl, **p_hl;
	size_t new_size;

	if (!bit_overlap_any(part_ptr->node_bitmap, arg->nodes_bitmap))
		return 0;

	COPY_BITMAP(arg->fwd_bitmap, part_ptr->node_bitmap);

	/* Extract partition's hostlist and node count */
	bit_and(arg->fwd_bitmap, arg->nodes_bitmap);
	bit_and_not(arg->nodes_bitmap, arg->fwd_bitmap);
	fwd_count = bit_set_count(arg->fwd_bitmap);
	hl = bitmap2hostlist(arg->fwd_bitmap);

	/* Generate FW tree hostlist array from partition's hostlist */
	hl_depth = _split_hostlist_treewidth(hl, &p_hl, &hl_count,
					     arg->tree_width);
	hostlist_destroy(hl);

	/* Make size for FW tree hostlist array in the main hostlist array */
	new_size = xsize(*arg->sp_hl) + hl_count * sizeof(hostlist_t *);
	xrealloc(*arg->sp_hl, new_size);

	/* Append the FW tree hostlist array to the main hostlist array */
	for (int i = 0; i < hl_count; i++)
		(*arg->sp_hl)[*arg->count + i] = p_hl[i];
	xfree(p_hl);
	*arg->count += hl_count;
	arg->depth = MAX(arg->depth, hl_depth);
	arg->msg_count -= fwd_count;

	if (arg->msg_count == 0)
		return -1;

	return 0;
}

static int _route_part_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				      int *count, uint16_t tree_width)
{
	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK, .part = READ_LOCK
	};
	bitstr_t *nodes_bitmap = NULL;
	_foreach_part_split_hostlist_t part_split;

	xassert(running_in_slurmctld());

	lock_slurmctld(node_read_lock);
	/* create bitmap of nodes to send message too */
	if (hostlist2bitmap(hl, false, &nodes_bitmap) != SLURM_SUCCESS) {
		char *buf = hostlist_ranged_string_xmalloc(hl);
		fatal("ROUTE: Failed to make bitmap from hostlist=%s.", buf);
	}

	*sp_hl = xcalloc(list_count(part_list), sizeof(hostlist_t *));
	*count = 0;

	part_split = (_foreach_part_split_hostlist_t) {
		.count = count,
		.depth = 0,
		.fwd_bitmap = NULL,
		.msg_count = hostlist_count(hl),
		.nodes_bitmap = nodes_bitmap,
		.sp_hl = sp_hl,
		.tree_width = tree_width,
	};

	list_for_each_ro(part_list, _part_split_hostlist, &part_split);

	FREE_NULL_BITMAP(part_split.fwd_bitmap);

	xassert(part_split.msg_count == bit_set_count(nodes_bitmap));
	if (part_split.msg_count) {
		size_t new_size = *count * sizeof(hostlist_t *);
		node_record_t *node_ptr;

		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			char *buf = bitmap2node_name(nodes_bitmap);
			log_flag(ROUTE, "didn't find partition containing nodes=%s",
				 buf);
			xfree(buf);
		}
		new_size += part_split.msg_count * sizeof(hostlist_t *);
		xrealloc(*sp_hl, new_size);

		for (int i = 0;
		     (node_ptr = next_node_bitmap(nodes_bitmap, &i));
		     i++) {
			(*sp_hl)[*count] = hostlist_create(NULL);
			hostlist_push_host((*sp_hl)[*count], node_ptr->name);
			(*count)++;
		}
		part_split.depth = MAX(part_split.depth, 1);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
		char *hl_str = hostlist_ranged_string_xmalloc(hl);
		log_flag(ROUTE, "hl: %s", hl_str);
		xfree(hl_str);
		for (int i = 0; i < *count; i++) {
			char *nodes =
				hostlist_ranged_string_xmalloc((*sp_hl)[i]);
			log_flag(ROUTE, "sp_hl[%d]: %s", i, nodes);
			xfree(nodes);
		}
	}

	unlock_slurmctld(node_read_lock);

	FREE_NULL_BITMAP(nodes_bitmap);
	FREE_NULL_BITMAP(part_split.fwd_bitmap);

	return part_split.depth;
}

/* this is used to set how many nodes are going to be on each branch
 * of the tree.
 * IN total       - total number of nodes to send to
 * IN tree_width  - how wide the tree should be on each hop
 * OUT span       - pointer to int array tree_width in length each space
 *		    containing the number of nodes to send to each hop
 *		    on the span.
 * RET int	  - the number of levels opened in the tree, or SLURM_ERROR
 */
static int _set_span(int total, uint16_t tree_width, int **span)
{
	int depth = 0;

	/* This should not happen. This is an error. */
	if (!span || total < 1)
		return SLURM_ERROR;

	/* If default span */
	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	/* Safeguard from leaks */
	if (*span)
		xfree(*span);

	/*
	 * Memory optimization:
	 * Don't allocate if we are in the last step to the leaves, as this is
	 * considered direct communication and we don't really need it.
	 */
	if (total <= tree_width)
		return 1;

	/* Each cell will contain the #nodes below this specific branch */
	*span = xcalloc(tree_width, sizeof(int));

	/*
	 * Try to fill levels until no more nodes are available.
	 *
	 * Each time a new level is created, it is exponentially bigger than the
	 * previous one
	 */
	for (int branch_capacity = 1, level_capacity = tree_width; total;
	     branch_capacity *= tree_width, level_capacity *= tree_width) {
		/* Remaining nodes can fill a whole new level up, or not */
		if (level_capacity <= total) {
			for (int i = 0; i < tree_width; i++)
				(*span)[i] += branch_capacity;
			total -= level_capacity;
		} else {
			/* Evenly distribute remaining nodes */
			branch_capacity = total / tree_width;
			/* But left the division remainder ones */
			level_capacity = branch_capacity * tree_width;
			/* Fill current level up, as much as possible */
			for (int i = 0; i < tree_width; i++)
				(*span)[i] += branch_capacity;
			total -= level_capacity;

			/* Evenly distribute the remainder nodes */
			for (int i = 0; total; i++, total--)
				(*span)[i]++;

			/* total == 0 always at this point */
		}

		/* One level more has been added */
		depth++;

		/* The level needed all the nodes, no more levels are added */
		if (!total)
			break;
	}

	/* Inform the caller about the number of levels below itself */
	return depth;
}

static int _split_hostlist_treewidth(hostlist_t *hl, hostlist_t ***sp_hl,
				     int *count, uint16_t tree_width)
{
	int host_count = hostlist_count(hl), depth, *span = NULL;
	char *name;

	/* If default span */
	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	/* This should not happen. This is an error. */
	if ((depth = _set_span(host_count, tree_width, &span)) < 0)
		return SLURM_ERROR;

	/*
	 * Memory optimization:
	 * _set_span doesn't return array for direct communication
	 * (if depth == 1 then span == NULL), so we just fill the hostlist array
	 * directly.
	 */
	if (depth == 1)
		tree_width = host_count;

	/* Each cell will contain the hostlist below this specific branch */
	*sp_hl = xcalloc(tree_width, sizeof(hostlist_t *));

	/*
	 * Fill the hostlists for each branch according to the distribution in
	 * set_span.
	 *
	 * Additionally, try to preserve network locality (based on distance)
	 * for subtrees, by assuming consecutive nodes are placed one next to
	 * each other
	 */
	for (*count = 0; (*count < tree_width) && (name = hostlist_shift(hl));
	     (*count)++) {
		/* Open the new branch, and add the 1st one */
		(*sp_hl)[*count] = hostlist_create(name);
		free(name);

		/* Consecutively add the rest of nodes for this branch */
		for (int i = 1; span && (i < span[*count]); i++) {
			name = hostlist_shift(hl);
			hostlist_push_host((*sp_hl)[*count], name);
			free(name);
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			char *buf =
			hostlist_ranged_string_xmalloc((*sp_hl)[*count]);
			debug("ROUTE: ... sublist[%d] %s", *count, buf);
			xfree(buf);
		}
	}

	xfree(span);
	return depth;
}

extern int common_topo_split_hostlist_treewidth(hostlist_t *hl,
						hostlist_t ***sp_hl,
						int *count, uint16_t tree_width)
{
	if (running_in_slurmctld() && common_topo_route_part())
		return _route_part_split_hostlist(hl, sp_hl, count, tree_width);

	return _split_hostlist_treewidth(hl, sp_hl, count, tree_width);
}

extern int common_topo_get_node_addr(char *node_name, char **addr,
				     char **pattern)
{

#ifndef HAVE_FRONT_END
	if (find_node_record(node_name) == NULL)
		return SLURM_ERROR;
#endif

	*addr = xstrdup(node_name);
	*pattern = xstrdup("node");
	return SLURM_SUCCESS;
}

extern bool common_topo_route_tree(void)
{
	static int route_tree = -1;
	if (route_tree == -1) {
		if (xstrcasestr(slurm_conf.topology_param, "routetree"))
			route_tree = true;
		else
			route_tree = false;
	}

	return route_tree;
}

extern bool common_topo_route_part(void)
{
	static int route_part = -1;
	if (route_part == -1) {
		if (xstrcasestr(slurm_conf.topology_param, "routepart"))
			route_part = true;
		else
			route_part = false;
	}

	return route_part;
}

extern int common_topo_choose_nodes(topology_eval_t *topo_eval)
{
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	job_record_t *job_ptr = topo_eval->job_ptr;

	int i, count, ec, most_res = 0;
	bitstr_t *orig_node_map, *req_node_map = NULL;
	bitstr_t **orig_core_array;
	int rem_nodes;
	uint32_t orig_max_nodes = topo_eval->max_nodes;

	if (job_ptr->details->req_node_bitmap)
		req_node_map = job_ptr->details->req_node_bitmap;

	/* clear nodes from the bitmap that don't have available resources */
	for (i = 0; next_node_bitmap(topo_eval->node_map, &i); i++) {
		/*
		 * Make sure we don't say we can use a node exclusively
		 * that is bigger than our whole-job maximum CPU count.
		 */
		if (((job_ptr->details->whole_node & WHOLE_NODE_REQUIRED) &&
		     (job_ptr->details->max_cpus != NO_VAL) &&
		     (job_ptr->details->max_cpus <
		      avail_res_array[i]->avail_cpus)) ||
		/* OR node has no CPUs */
		    (avail_res_array[i]->avail_cpus < 1)) {

			if (req_node_map && bit_test(req_node_map, i)) {
				/* can't clear a required node! */
				return SLURM_ERROR;
			}
			bit_clear(topo_eval->node_map, i);
		}
	}

	if (job_ptr->details->num_tasks &&
	    !(job_ptr->details->ntasks_per_node) &&
	    (topo_eval->max_nodes > job_ptr->details->num_tasks))
		topo_eval->max_nodes =
			MAX(job_ptr->details->num_tasks, topo_eval->min_nodes);

	/*
	 * common_topo_eval_nodes() might need to be called more than once and
	 * is destructive of node_map and avail_core. Copy those bitmaps.
	 */
	orig_node_map = bit_copy(topo_eval->node_map);
	orig_core_array = copy_core_array(topo_eval->avail_core);

	topo_eval->first_pass = true;

	ec = eval_nodes(topo_eval);
	if (ec == SLURM_SUCCESS)
		goto fini;

	topo_eval->first_pass = false;
	topo_eval->max_nodes = orig_max_nodes;

	bit_or(topo_eval->node_map, orig_node_map);
	core_array_or(topo_eval->avail_core, orig_core_array);

	rem_nodes = bit_set_count(topo_eval->node_map);
	if (rem_nodes <= topo_eval->min_nodes) {
		/* Can not remove any nodes, enable use of non-local GRES */
		ec = eval_nodes(topo_eval);
		goto fini;
	}

	/*
	 * This nodeset didn't work. To avoid a possible knapsack problem,
	 * incrementally remove nodes with low resource counts (sum of CPU and
	 * GPU count if using GPUs, otherwise the CPU count) and retry
	 */
	for (i = 0; next_node(&i); i++) {
		if (avail_res_array[i]) {
			most_res = MAX(most_res,
				       avail_res_array[i]->avail_res_cnt);
		}
	}

	for (count = 1; count < most_res; count++) {
		int nochange = 1;
		topo_eval->max_nodes = orig_max_nodes;
		bit_or(topo_eval->node_map, orig_node_map);
		core_array_or(topo_eval->avail_core, orig_core_array);
		for (i = 0; next_node_bitmap(topo_eval->node_map, &i); i++) {
			if ((avail_res_array[i]->avail_res_cnt > 0) &&
			    (avail_res_array[i]->avail_res_cnt <= count)) {
				if (req_node_map && bit_test(req_node_map, i))
					continue;
				/*
				 * We adjust avail_res_cnt to the minimum needed
				 * for the evaluated nodes on every
				 * eval_nodes().
				 * So, we need to check again if some more nodes
				 * can be removed in the updated nodeset before
				 * increasing the count, or we could end up
				 * removing more (possibly valid) nodes than
				 * needed.
				 */
				if (nochange)
					count--;
				nochange = 0;
				bit_clear(topo_eval->node_map, i);
				bit_clear(orig_node_map, i);
				if (--rem_nodes <= topo_eval->min_nodes)
					break;
			}
		}
		if (nochange && (count != 1))
			continue;
		ec = eval_nodes(topo_eval);
		if (ec == SLURM_SUCCESS)
			break;
		if (rem_nodes <= topo_eval->min_nodes)
			break;
	}

fini:	if ((ec == SLURM_SUCCESS) && job_ptr->gres_list_req &&
	     orig_core_array) {
		/*
		 * Update available CPU count for any removed cores.
		 * Cores are only removed for jobs with GRES to enforce binding.
		 */
		for (i = 0; next_node_bitmap(topo_eval->node_map, &i); i++) {
			if (!orig_core_array[i] || !topo_eval->avail_core[i])
				continue;
			count = bit_set_count(topo_eval->avail_core[i]);
			count *= node_record_table_ptr[i]->tpc;
			avail_res_array[i]->avail_cpus =
				MIN(count, avail_res_array[i]->avail_cpus);
			if (avail_res_array[i]->avail_cpus == 0) {
				error("avail_cpus underflow for %pJ",
				      job_ptr);
				if (req_node_map && bit_test(req_node_map, i)) {
					/* can't clear a required node! */
					ec = SLURM_ERROR;
				}
				bit_clear(topo_eval->node_map, i);
			}
		}
	}
	FREE_NULL_BITMAP(orig_node_map);
	free_core_array(&orig_core_array);
	return ec;
}
