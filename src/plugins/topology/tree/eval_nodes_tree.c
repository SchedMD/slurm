/*****************************************************************************\
 *  eval_nodes_tree.c - Determine order of nodes for job using tree algo.
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

#include "eval_nodes_tree.h"

#include "../common/eval_nodes.h"
#include "../common/gres_sched.h"

#include "src/common/xstring.h"

static void _topo_add_dist(uint32_t *dist, int inx)
{
	for (int i = 0; i < switch_record_cnt; i++) {
		if (switch_record_table[inx].switches_dist[i] == INFINITE ||
		    dist[i] == INFINITE) {
			dist[i] = INFINITE;
		} else {
			dist[i] += switch_record_table[inx].switches_dist[i];
		}
	}
}

/*
 * returns 1 if switch "i" is better fit
 * returns -1 if switch "j" is better fit
 * returns 0 if there is no better fit
 */
static int _topo_compare_switches(int i, int j, int rem_nodes,
				  int *switch_node_cnt, int rem_cpus,
				  uint32_t *switch_cpu_cnt, bool *i_fit_out)
{
	while (1) {
		bool i_fit = ((switch_node_cnt[i] >= rem_nodes) &&
			      ((int) switch_cpu_cnt[i] >= rem_cpus));
		bool j_fit = ((switch_node_cnt[j] >= rem_nodes) &&
			      ((int) switch_cpu_cnt[j] >= rem_cpus));
		*i_fit_out = i_fit;

		if (i_fit && j_fit) {
			if (switch_node_cnt[i] < switch_node_cnt[j])
				return 1;
			if (switch_node_cnt[i] > switch_node_cnt[j])
				return -1;
			break;
		} else if (i_fit) {
			return 1;
		} else if (j_fit) {
			return -1;
		}

		if (((switch_record_table[i].parent != i) ||
		     (switch_record_table[j].parent != j)) &&
		    (switch_record_table[i].parent !=
		     switch_record_table[j].parent)) {
			i = switch_record_table[i].parent;
			j = switch_record_table[j].parent;
			continue;
		}

		break;
	}

	if (switch_node_cnt[i] > switch_node_cnt[j])
		return 1;
	if (switch_node_cnt[i] < switch_node_cnt[j])
		return -1;
	if (switch_record_table[i].level < switch_record_table[j].level)
		return 1;
	if (switch_record_table[i].level > switch_record_table[j].level)
		return -1;
	return 0;

}

static void _topo_choose_best_switch(uint32_t *dist, int *switch_node_cnt,
				     int rem_nodes, uint32_t *switch_cpu_cnt,
				     int rem_cpus, int i, int *best_switch)
{
	int tcs = 0;
	bool i_fit = false;

	if (*best_switch == -1 || dist[i] == INFINITE || !switch_node_cnt[i]) {
		/*
		 * If first possibility
		 */
		if (switch_node_cnt[i] && dist[i] < INFINITE)
			*best_switch = i;
		return;
	}

	tcs = _topo_compare_switches(i, *best_switch, rem_nodes,
				     switch_node_cnt, rem_cpus, switch_cpu_cnt,
				     &i_fit);
	if (((dist[i] < dist[*best_switch]) && i_fit) ||
	    ((dist[i] == dist[*best_switch]) && (tcs > 0))) {
		/*
		 * If closer and fit request OR
		 * same distance and tightest fit (less resource waste)
		 */
		*best_switch = i;
	}
}

/*
 * Allocate resources to the job on one leaf switch if possible,
 * otherwise distribute the job allocation over many leaf switches.
 */
static int _eval_nodes_dfly(topology_eval_t *topo_eval)
{
	list_t **switch_gres = NULL;		/* available GRES on switch */
	bitstr_t **switch_node_bitmap = NULL;	/* nodes on this switch */
	int       *switch_node_cnt = NULL;	/* total nodes on switch */
	int       *switch_required = NULL;	/* set if has required node */
	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;	/* required node bitmap */
	bitstr_t  *req2_nodes_bitmap  = NULL;	/* required+lowest prio nodes */
	bitstr_t  *best_nodes_bitmap  = NULL;	/* required+low prio nodes */
	int i, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt = 0, best_node_cnt = 0, req_node_cnt = 0;
	list_t *best_gres = NULL;
	switch_record_t *switch_ptr;
	list_t *node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	list_itr_t *iter;
	node_record_t *node_ptr;
	int64_t rem_max_cpus;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bool sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	time_t time_waiting = 0;
	int leaf_switch_count = 0;
	int top_switch_inx = -1;
	int prev_rem_nodes;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	uint64_t maxtasks;

	topo_eval->avail_cpus = 0;

	if (job_ptr->req_switch > 1) {
		/* Maximum leaf switch count >1 probably makes no sense */
		info("Resetting %pJ leaf switch count from %u to 0",
		     job_ptr, job_ptr->req_switch);
		job_ptr->req_switch = 0;
	}
	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((topo_eval->gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);
	maxtasks = eval_nodes_set_max_tasks(job_ptr, rem_max_cpus,
					    topo_eval->max_nodes);

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   topo_eval->node_map)) {
			info("%pJ requires nodes which are not currently available",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%pJ required node list has no nodes",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (req_node_cnt > topo_eval->max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			      job_ptr, req_node_cnt,
			      topo_eval->max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
	}

	/*
	 * Add required nodes to job allocation and
	 * build list of node bitmaps, sorted by weight
	 */
	if (!bit_set_count(topo_eval->node_map)) {
		debug("%pJ node_map is empty",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	avail_cpu_per_node = xcalloc(node_record_count, sizeof(uint16_t));
	node_weight_list = list_create(eval_nodes_topo_weight_free);
	for (i = 0;
	     (node_ptr = next_node_bitmap(topo_eval->node_map, &i));
	     i++) {
		topo_weight_info_t nw_static;
		if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i)) {
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			(void) eval_nodes_cpus_to_use(
				topo_eval, i, rem_max_cpus, min_rem_nodes,
				&maxtasks, true);
			if (topo_eval->avail_cpus == 0) {
				log_flag(SELECT_TYPE, "%pJ insufficient resources on required node",
				       job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			avail_cpu_per_node[i] = topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
		}

		nw_static.weight = node_ptr->sched_weight;
		nw = list_find_first(node_weight_list,
				     eval_nodes_topo_weight_find,
				     &nw_static);
		if (!nw) {	/* New node weight to add */
			nw = xmalloc(sizeof(topo_weight_info_t));
			nw->node_bitmap = bit_alloc(node_record_count);
			nw->weight = node_ptr->sched_weight;
			list_append(node_weight_list, nw);
		}
		bit_set(nw->node_bitmap, i);
		nw->node_cnt++;
	}

	if (req_nodes_bitmap) {
		bit_and(topo_eval->node_map, req_nodes_bitmap);
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			rc = SLURM_ERROR;
			log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
				 job_ptr);
			goto fini;
		}
	} else {
		bit_clear_all(topo_eval->node_map);
	}

	list_sort(node_weight_list, eval_nodes_topo_weight_sort);
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)
		(void) list_for_each(node_weight_list,
				     eval_nodes_topo_weight_log, NULL);

	/*
	 * Identify the highest level switch to be used.
	 * Note that nodes can be on multiple non-overlapping switches.
	 */
	switch_gres = xcalloc(switch_record_cnt, sizeof(list_t *));
	switch_node_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switch_node_cnt    = xcalloc(switch_record_cnt, sizeof(int));
	switch_required    = xcalloc(switch_record_cnt, sizeof(int));

	if (!req_nodes_bitmap)
		nw = list_peek(node_weight_list);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		switch_node_bitmap[i] = bit_copy(switch_ptr->node_bitmap);
		if (req_nodes_bitmap &&
		    bit_overlap_any(req_nodes_bitmap, switch_node_bitmap[i])) {
			switch_required[i] = 1;
			if (switch_record_table[i].level == 0) {
				leaf_switch_count++;
			}
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
		if (!req_nodes_bitmap &&
		    (list_find_first(node_weight_list,
				     eval_nodes_topo_node_find,
				     switch_node_bitmap[i]))) {
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
	}

	/*
	 * Top switch is highest level switch containing all required nodes
	 * OR all nodes of the lowest scheduling weight
	 * OR -1 of can not identify top-level switch
	 */
	if (top_switch_inx == -1) {
		error("%pJ unable to identify top level switch",
		       job_ptr);
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		goto fini;
	}

	/* Check that all specificly required nodes are on shared network */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap,
			   switch_node_bitmap[top_switch_inx])) {
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		info("%pJ requires nodes that do not have shared network",
		     job_ptr);
		goto fini;
	}

	/*
	 * Remove nodes from consideration that can not be reached from this
	 * top level switch
	 */
	for (i = 0; i < switch_record_cnt; i++) {
		if (top_switch_inx != i) {
			  bit_and(switch_node_bitmap[i],
				  switch_node_bitmap[top_switch_inx]);
		}
	}

	/*
	 * Identify the best set of nodes (i.e. nodes with the lowest weight,
	 * in addition to the required nodes) that can be used to satisfy the
	 * job request. All nodes must be on a common top-level switch. The
	 * logic here adds groups of nodes, all with the same weight, so we
	 * usually identify more nodes than required to satisfy the request.
	 * Later logic selects from those nodes to get the best topology.
	 */
	best_nodes_bitmap = bit_alloc(node_record_count);
	iter = list_iterator_create(node_weight_list);
	while (!sufficient && (nw = list_next(iter))) {
		if (best_node_cnt > 0) {
			/*
			 * All of the lower priority nodes should be included
			 * in the job's allocation. Nodes from the next highest
			 * weight nodes are included only as needed.
			 */
			if (req2_nodes_bitmap)
				bit_or(req2_nodes_bitmap, best_nodes_bitmap);
			else
				req2_nodes_bitmap = bit_copy(best_nodes_bitmap);
		}
		for (i = 0; next_node_bitmap(nw->node_bitmap, &i); i++) {
			if (avail_cpu_per_node[i])
				continue;	/* Required node */
			if (!bit_test(switch_node_bitmap[top_switch_inx], i))
				continue;
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			if (topo_eval->avail_cpus == 0) {
				bit_clear(nw->node_bitmap, i);
				continue;
			}
			bit_set(best_nodes_bitmap, i);
			avail_cpu_per_node[i] = topo_eval->avail_cpus;
			best_cpu_cnt += topo_eval->avail_cpus;
			best_node_cnt++;
			if (topo_eval->gres_per_job) {
				gres_sched_consec(
					&best_gres, job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list);
			}
		}

		sufficient = (best_cpu_cnt >= rem_cpus) &&
			     eval_nodes_enough_nodes(best_node_cnt, rem_nodes,
						     min_nodes, req_nodes);
		if (sufficient && topo_eval->gres_per_job) {
			sufficient = gres_sched_sufficient(
				job_ptr->gres_list_req, best_gres);
		}
	}
	list_iterator_destroy(iter);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *gres_str = NULL, *gres_print = "";
		char *node_names;
		if (req_nodes_bitmap) {
			node_names = bitmap2node_name(req_nodes_bitmap);
			info("Required nodes:%s", node_names);
			xfree(node_names);
		}
		node_names = bitmap2node_name(best_nodes_bitmap);
		if (topo_eval->gres_per_job) {
			gres_str = gres_sched_str(best_gres);
			if (gres_str)
				gres_print = gres_str;
		}
		info("Best nodes:%s node_cnt:%d cpu_cnt:%d %s",
		     node_names, best_node_cnt, best_cpu_cnt, gres_print);
		xfree(node_names);
		xfree(gres_str);
	}
	if (!sufficient) {
		log_flag(SELECT_TYPE, "insufficient resources currently available for %pJ",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Add lowest weight nodes. Treat similar to required nodes for the job.
	 * Job will still need to add some higher weight nodes later.
	 */
	if (req2_nodes_bitmap) {
		for (i = 0;
		     next_node_bitmap(req2_nodes_bitmap, &i) && (topo_eval->max_nodes > 0);
		     i++) {
			topo_eval->avail_cpus = avail_cpu_per_node[i];
			if (!eval_nodes_cpus_to_use(
				topo_eval, i, rem_max_cpus, min_rem_nodes,
				&maxtasks, true)) {
				/*
				 * Too many restricted cores removed due to
				 * gres layout. Skip node
				 */
				bit_clear(req2_nodes_bitmap, i);
				continue;
			}
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
		}

		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_required[i])
				continue;
			if (bit_overlap_any(req2_nodes_bitmap,
					    switch_node_bitmap[i])) {
				switch_required[i] = 1;
				if (switch_record_table[i].level == 0) {
					leaf_switch_count++;
				}
			}
		}
		bit_or(topo_eval->node_map, req2_nodes_bitmap);
		if (topo_eval->max_nodes <= 0) {
			rc = SLURM_ERROR;
			log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
				 job_ptr);
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    (!topo_eval->gres_per_job ||
		     gres_sched_test(job_ptr->gres_list_req,
				     job_ptr->job_id))) {
			/* Required nodes completely satisfied the request */
			error("Scheduling anomaly for %pJ",
			      job_ptr);
			rc = SLURM_SUCCESS;
			goto fini;
		}
	}

	/*
	 * Construct a set of switch array entries.
	 * Use the same indexes as switch_record_table in slurmctld.
	 */
	bit_or(best_nodes_bitmap, topo_eval->node_map);
	avail_nodes_bitmap = bit_alloc(node_record_count);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		bit_and(switch_node_bitmap[i], best_nodes_bitmap);
		bit_or(avail_nodes_bitmap, switch_node_bitmap[i]);
		switch_node_cnt[i] = bit_set_count(switch_node_bitmap[i]);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switch_node_cnt[i]) {
				node_names =
					bitmap2node_name(switch_node_bitmap[i]);
			}
			info("switch=%s level=%d nodes=%u:%s required:%u speed:%u",
			     switch_record_table[i].name,
			     switch_record_table[i].level,
			     switch_node_cnt[i], node_names,
			     switch_required[i],
			     switch_record_table[i].link_speed);
			xfree(node_names);
		}
	}

	if (req_nodes_bitmap &&
	    (!bit_super_set(req_nodes_bitmap, avail_nodes_bitmap))) {
		info("%pJ requires nodes not available on any switch",
		     job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * If no resources have yet been  selected,
	 * then pick one leaf switch with the most available nodes.
	 */
	if (leaf_switch_count == 0) {
		int best_switch_inx = -1;
		for (i = 0; i < switch_record_cnt; i++) {
			if (switch_record_table[i].level != 0)
				continue;
			if ((best_switch_inx == -1) ||
			    (switch_node_cnt[i] >
			     switch_node_cnt[best_switch_inx]))
				best_switch_inx = i;
		}
		if (best_switch_inx != -1) {
			leaf_switch_count = 1;
			switch_required[best_switch_inx] = 1;
		}
	}

	/*
	 * All required resources currently on one leaf switch. Determine if
	 * the entire job request can be satisfied using just that one switch.
	 */
	if (leaf_switch_count == 1) {
		best_cpu_cnt = 0;
		best_node_cnt = 0;
		FREE_NULL_LIST(best_gres);
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(topo_eval->node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				topo_eval->avail_cpus = avail_cpu_per_node[j];
				best_cpu_cnt += topo_eval->avail_cpus;
				best_node_cnt++;
				if (topo_eval->gres_per_job) {
					gres_sched_consec(
						&best_gres,
						job_ptr->gres_list_req,
						avail_res_array[j]->
						sock_gres_list);
				}
			}
			break;
		}
		sufficient = (best_cpu_cnt >= rem_cpus) &&
			     eval_nodes_enough_nodes(best_node_cnt, rem_nodes,
						     min_nodes, req_nodes);
		if (sufficient && topo_eval->gres_per_job) {
			sufficient = gres_sched_sufficient(
				job_ptr->gres_list_req, best_gres);
		}
		if (sufficient && (i < switch_record_cnt)) {
			/* Complete request using this one leaf switch */
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(topo_eval->node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				topo_eval->avail_cpus = avail_cpu_per_node[j];
				if (!eval_nodes_cpus_to_use(topo_eval, j,
						            rem_max_cpus,
						            min_rem_nodes,
						            &maxtasks, true)) {
					avail_cpu_per_node[j] = 0;
					continue;
				}

				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
				rem_cpus   -= topo_eval->avail_cpus;
				rem_max_cpus -= topo_eval->avail_cpus;
				bit_set(topo_eval->node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!topo_eval->gres_per_job ||
				     gres_sched_test(job_ptr->gres_list_req,
						     job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
				if (topo_eval->max_nodes <= 0) {
					rc = SLURM_ERROR;
					log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
						 job_ptr);
					goto fini;
				}
			}
		}
	}

	/*
	 * Add additional resources as required from additional leaf switches
	 * on a round-robin basis
	 */
	prev_rem_nodes = rem_nodes + 1;
	while (1) {
		if (prev_rem_nodes == rem_nodes)
			break;	/* Stalled */
		prev_rem_nodes = rem_nodes;
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(topo_eval->node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				topo_eval->avail_cpus = avail_cpu_per_node[j];
				if (!eval_nodes_cpus_to_use(topo_eval, j,
						            rem_max_cpus,
						            min_rem_nodes,
						            &maxtasks, true)) {
					avail_cpu_per_node[j] = 0;
					continue;
				}
				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
				rem_cpus   -= topo_eval->avail_cpus;
				rem_max_cpus -= topo_eval->avail_cpus;
				bit_set(topo_eval->node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!topo_eval->gres_per_job ||
				     gres_sched_test(job_ptr->gres_list_req,
						     job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
				if (topo_eval->max_nodes <= 0) {
					rc = SLURM_ERROR;
					log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
						 job_ptr);
					goto fini;
				}
				break;	/* Move to next switch */
			}
		}
	}
	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!topo_eval->gres_per_job ||
	     gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:
	if (rc == SLURM_SUCCESS)
		eval_nodes_clip_socket_cores(topo_eval);

	if ((job_ptr->req_switch > 0) && (rc == SLURM_SUCCESS) &&
	    switch_node_bitmap) {
		/* req_switch == 1 here; enforced at the top of the function. */
		leaf_switch_count = 0;

		/* count up leaf switches */
		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_record_table[i].level != 0)
				continue;
			if (bit_overlap_any(switch_node_bitmap[i], topo_eval->node_map))
				leaf_switch_count++;
		}
		if (time_waiting >= job_ptr->wait4switch) {
			job_ptr->best_switch = true;
			debug3("%pJ waited %ld sec for switches use=%d",
				job_ptr, time_waiting, leaf_switch_count);
		} else if (leaf_switch_count > job_ptr->req_switch) {
			/*
			 * Allocation is for more than requested number of
			 * switches.
			 */
			job_ptr->best_switch = false;
			debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
				job_ptr, time_waiting, job_ptr->req_switch,
				leaf_switch_count, job_ptr->wait4switch);
		} else {
			job_ptr->best_switch = true;
		}
	}

	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	xfree(avail_cpu_per_node);
	xfree(switch_gres);
	if (switch_node_bitmap) {
		for (i = 0; i < switch_record_cnt; i++)
			FREE_NULL_BITMAP(switch_node_bitmap[i]);
		xfree(switch_node_bitmap);
	}
	xfree(switch_node_cnt);
	xfree(switch_required);
	return rc;
}

static void _decrement_node_cnt(int num_nodes_taken, int switch_index,
				int *switch_node_cnt)
{
	for (int i = switch_index; i >= 0; i = switch_record_table[i].parent) {
		if (switch_node_cnt[i] <= num_nodes_taken) {
			switch_node_cnt[i] = 0;
		} else {
			switch_node_cnt[i] -= num_nodes_taken;
		}

		/* end once we've reached root switch */
		if (switch_record_table[i].parent == SWITCH_NO_PARENT)
			break;
	}
}

/* Allocate resources to job using a minimal leaf switch count */
static int _eval_nodes_topo(topology_eval_t *topo_eval)
{
	uint32_t *switch_cpu_cnt = NULL;	/* total CPUs on switch */
	bitstr_t **switch_node_bitmap = NULL;	/* nodes on this switch */
	bitstr_t **start_switch_node_bitmap = NULL;
	int       *switch_node_cnt = NULL;	/* total nodes on switch */
	int       *switch_required = NULL;	/* set if has required node */
	int *req_switch_required = NULL;
	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;	/* required node bitmap */
	bitstr_t  *req2_nodes_bitmap  = NULL;	/* required+lowest prio nodes */
	bitstr_t  *best_nodes_bitmap  = NULL;	/* required+low prio nodes */
	bitstr_t *start_node_map = NULL;
	int i, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt, best_node_cnt, req_node_cnt = 0;
	list_t *best_gres = NULL;
	switch_record_t *switch_ptr;
	list_t *node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	list_itr_t *iter;
	node_record_t *node_ptr;
	int64_t rem_max_cpus, start_rem_max_cpus = 0;
	int rem_cpus, start_rem_cpus = 0, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bool requested, sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	uint32_t *switches_dist= NULL;
	time_t time_waiting = 0;
	int top_switch_inx = -1;
	uint64_t top_switch_lowest_weight = 0;
	int prev_rem_nodes;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	uint32_t org_max_nodes = topo_eval->max_nodes;
	uint64_t maxtasks;

	topo_eval->avail_cpus = 0;

	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((topo_eval->gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);
	maxtasks = eval_nodes_set_max_tasks(job_ptr, rem_max_cpus,
					    topo_eval->max_nodes);

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   topo_eval->node_map)) {
			info("%pJ requires nodes which are not currently available",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%pJ required node list has no nodes",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (req_node_cnt > topo_eval->max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			      job_ptr, req_node_cnt,
			      topo_eval->max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
		req_nodes_bitmap = job_ptr->details->req_node_bitmap;
	}

	/*
	 * Add required nodes to job allocation and
	 * build list of node bitmaps, sorted by weight
	 */
	if (!bit_set_count(topo_eval->node_map)) {
		debug("%pJ node_map is empty",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	avail_cpu_per_node = xcalloc(node_record_count, sizeof(uint16_t));
	node_weight_list = list_create(eval_nodes_topo_weight_free);
	for (i = 0;
	     (node_ptr = next_node_bitmap(topo_eval->node_map, &i));
	     i++) {
		topo_weight_info_t nw_static;
		if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i)) {
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			(void) eval_nodes_cpus_to_use(topo_eval, i,
						      rem_max_cpus,
						      min_rem_nodes,
						      &maxtasks, true);
			if (topo_eval->avail_cpus == 0) {
				debug2("%pJ insufficient resources on required node",
				       job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			avail_cpu_per_node[i] = topo_eval->avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
		}

		nw_static.weight = node_ptr->sched_weight;
		nw = list_find_first(node_weight_list,
				     eval_nodes_topo_weight_find,
				     &nw_static);
		if (!nw) {	/* New node weight to add */
			nw = xmalloc(sizeof(topo_weight_info_t));
			nw->node_bitmap = bit_alloc(node_record_count);
			nw->weight = node_ptr->sched_weight;
			list_append(node_weight_list, nw);
		}
		bit_set(nw->node_bitmap, i);
		nw->node_cnt++;
	}

	list_sort(node_weight_list, eval_nodes_topo_weight_sort);
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)
		(void) list_for_each(node_weight_list,
				     eval_nodes_topo_weight_log, NULL);

	/*
	 * Identify the highest level switch to be used.
	 * Note that nodes can be on multiple non-overlapping switches.
	 */
	switch_cpu_cnt = xcalloc(switch_record_cnt, sizeof(uint32_t));
	switch_node_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	start_switch_node_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switch_node_cnt    = xcalloc(switch_record_cnt, sizeof(int));
	switch_required    = xcalloc(switch_record_cnt, sizeof(int));
	req_switch_required = xcalloc(switch_record_cnt, sizeof(int));

	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		uint32_t switch_cpus = 0;
		switch_node_bitmap[i] = bit_copy(switch_ptr->node_bitmap);
		bit_and(switch_node_bitmap[i], topo_eval->node_map);
		switch_node_cnt[i] = bit_set_count(switch_node_bitmap[i]);
		/*
		 * Count total CPUs of the intersection of node_map and
		 * switch_node_bitmap.
		 */
		for (j = 0; (node_ptr = next_node_bitmap(switch_node_bitmap[i],
							 &j));
		     j++)
			switch_cpus += avail_res_array[j]->avail_cpus;
		switch_cpu_cnt[i] = switch_cpus;
		if (req_nodes_bitmap &&
		    bit_overlap_any(req_nodes_bitmap, switch_node_bitmap[i])) {
			switch_required[i] = 1;
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
		if (!eval_nodes_enough_nodes(switch_node_cnt[i], rem_nodes,
					     min_nodes, req_nodes) ||
		    (rem_cpus > switch_cpu_cnt[i]))
			continue;
		if (!req_nodes_bitmap &&
		    (nw = list_find_first(node_weight_list,
					  eval_nodes_topo_node_find,
				    switch_node_bitmap[i]))) {
			if ((top_switch_inx == -1) ||
			    ((switch_record_table[i].level >=
			      switch_record_table[top_switch_inx].level) &&
			     (nw->weight <= top_switch_lowest_weight))) {
				top_switch_inx = i;
				top_switch_lowest_weight = nw->weight;
			}
		}
	}

	if (!req_nodes_bitmap) {
		bit_clear_all(topo_eval->node_map);
	}

	/*
	 * Top switch is highest level switch containing all required nodes
	 * OR all nodes of the lowest scheduling weight
	 * OR -1 if can not identify top-level switch, which may be due to a
	 * disjoint topology and available nodes living on different switches.
	 */
	if (top_switch_inx == -1) {
		log_flag(SELECT_TYPE, "%pJ unable to identify top level switch",
			 job_ptr);
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		goto fini;
	}

	/* Check that all specificly required nodes are on shared network */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap,
			   switch_node_bitmap[top_switch_inx])) {
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		info("%pJ requires nodes that do not have shared network",
		     job_ptr);
		goto fini;
	}

	/*
	 * Remove nodes from consideration that can not be reached from this
	 * top level switch.
	 */
	for (i = 0; i < switch_record_cnt; i++) {
		if (top_switch_inx != i) {
			  bit_and(switch_node_bitmap[i],
				  switch_node_bitmap[top_switch_inx]);
		}
	}

	start_rem_cpus = rem_cpus;
	start_rem_max_cpus = rem_max_cpus;
	if (req_nodes_bitmap) {
		bit_and(topo_eval->node_map, req_nodes_bitmap);
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
			log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
				 job_ptr);
			goto fini;
		}
	}

	start_node_map = bit_copy(topo_eval->node_map);
	memcpy(req_switch_required, switch_required,
	       switch_record_cnt * sizeof(int));
	for (i = 0; i < switch_record_cnt; i++)
		start_switch_node_bitmap[i] = bit_copy(switch_node_bitmap[i]);

try_again:
	/*
	 * Identify the best set of nodes (i.e. nodes with the lowest weight,
	 * in addition to the required nodes) that can be used to satisfy the
	 * job request. All nodes must be on a common top-level switch. The
	 * logic here adds groups of nodes, all with the same weight, so we
	 * usually identify more nodes than required to satisfy the request.
	 * Later logic selects from those nodes to get the best topology.
	 */
	requested = false;
	best_node_cnt = 0;
	best_cpu_cnt = 0;
	best_nodes_bitmap = bit_alloc(node_record_count);
	iter = list_iterator_create(node_weight_list);
	while (!requested && (nw = list_next(iter))) {
		if (best_node_cnt > 0) {
			/*
			 * All of the lower priority nodes should be included
			 * in the job's allocation. Nodes from the next highest
			 * weight nodes are included only as needed.
			 */
			if (req2_nodes_bitmap)
				bit_or(req2_nodes_bitmap, best_nodes_bitmap);
			else
				req2_nodes_bitmap = bit_copy(best_nodes_bitmap);
		}

		if (!bit_set_count(nw->node_bitmap))
			continue;

		for (i = 0; (node_ptr = next_node_bitmap(nw->node_bitmap, &i));
		     i++) {
			if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i))
				continue;	/* Required node */
			if (!bit_test(switch_node_bitmap[top_switch_inx], i))
				continue;
			eval_nodes_select_cores(topo_eval, i, min_rem_nodes);
			if (topo_eval->avail_cpus == 0) {
				bit_clear(nw->node_bitmap, i);
				continue;
			}
			bit_set(best_nodes_bitmap, i);
			avail_cpu_per_node[i] = topo_eval->avail_cpus;
			best_cpu_cnt += topo_eval->avail_cpus;
			best_node_cnt++;
			if (topo_eval->gres_per_job) {
				gres_sched_consec(
					&best_gres, job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list);
			}
		}

		if (!sufficient) {
			sufficient = (best_cpu_cnt >= rem_cpus) &&
				     eval_nodes_enough_nodes(
					     best_node_cnt, rem_nodes,
					     min_nodes, req_nodes);
			if (sufficient && topo_eval->gres_per_job) {
				sufficient = gres_sched_sufficient(
						job_ptr->gres_list_req,
						best_gres);
			}
		}
		requested = ((best_node_cnt >= rem_nodes) &&
			     (best_cpu_cnt >= rem_cpus) &&
			     (!topo_eval->gres_per_job ||
			      gres_sched_sufficient(job_ptr->gres_list_req,
						    best_gres)));
	}
	list_iterator_destroy(iter);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *gres_str = NULL, *gres_print = "";
		char *node_names;
		if (req_nodes_bitmap) {
			node_names = bitmap2node_name(req_nodes_bitmap);
			info("Required nodes:%s", node_names);
			xfree(node_names);
		}
		node_names = bitmap2node_name(best_nodes_bitmap);
		if (topo_eval->gres_per_job) {
			gres_str = gres_sched_str(best_gres);
			if (gres_str)
				gres_print = gres_str;
		}
		info("Best nodes:%s node_cnt:%d cpu_cnt:%d %s",
		     node_names, best_node_cnt, best_cpu_cnt, gres_print);
		xfree(node_names);
		xfree(gres_str);
	}
	if (!sufficient) {
		log_flag(SELECT_TYPE, "insufficient resources currently available for %pJ",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Add lowest weight nodes. Treat similar to required nodes for the job.
	 * Job will still need to add some higher weight nodes later.
	 */
	if (req2_nodes_bitmap) {
		for (i = 0;
		     next_node_bitmap(req2_nodes_bitmap, &i) && (topo_eval->max_nodes > 0);
		     i++) {
			topo_eval->avail_cpus = avail_cpu_per_node[i];
			if (!eval_nodes_cpus_to_use(topo_eval, i, rem_max_cpus,
						    min_rem_nodes, &maxtasks,
						    true)) {
				/*
				 * Too many restricted gpu cores removed due to
				 * gres layout. Skip node
				 */
				bit_clear(req2_nodes_bitmap, i);
				continue;
			}
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
		}

		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_required[i])
				continue;
			if (bit_overlap_any(req2_nodes_bitmap,
					    switch_node_bitmap[i])) {
				switch_required[i] = 1;
			}
		}
		bit_or(topo_eval->node_map, req2_nodes_bitmap);

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    (!topo_eval->gres_per_job ||
		     gres_sched_test(job_ptr->gres_list_req,
				     job_ptr->job_id))) {
			/* Required nodes completely satisfied the request */
			error("Scheduling anomaly for %pJ",
			      job_ptr);
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			rc = SLURM_ERROR;
			log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
				 job_ptr);
			goto fini;
		}
	}

	/*
	 * Construct a set of switch array entries.
	 * Use the same indexes as switch_record_table in slurmctld.
	 */
	bit_or(best_nodes_bitmap, topo_eval->node_map);
	avail_nodes_bitmap = bit_alloc(node_record_count);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		bit_and(switch_node_bitmap[i], best_nodes_bitmap);
		bit_or(avail_nodes_bitmap, switch_node_bitmap[i]);
		switch_node_cnt[i] = bit_set_count(switch_node_bitmap[i]);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switch_node_cnt[i]) {
				node_names =
					bitmap2node_name(switch_node_bitmap[i]);
			}
			info("switch=%s level=%d nodes=%u:%s required:%u speed:%u",
			     switch_record_table[i].name,
			     switch_record_table[i].level,
			     switch_node_cnt[i], node_names,
			     switch_required[i],
			     switch_record_table[i].link_speed);
			xfree(node_names);
		}
	}

	/* Add additional resources for already required leaf switches */
	if (req_nodes_bitmap || req2_nodes_bitmap) {
		int num_nodes_taken = 0;
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(topo_eval->node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				topo_eval->avail_cpus = avail_cpu_per_node[j];
				if (!eval_nodes_cpus_to_use(topo_eval, j,
							    rem_max_cpus,
							    min_rem_nodes,
							    &maxtasks, true)) {
					avail_cpu_per_node[j] = 0;
					continue;
				}
				num_nodes_taken++;
				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
				rem_cpus   -= topo_eval->avail_cpus;
				rem_max_cpus -= topo_eval->avail_cpus;
				bit_set(topo_eval->node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!topo_eval->gres_per_job ||
				     gres_sched_test(job_ptr->gres_list_req,
						     job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
			}

			_decrement_node_cnt(num_nodes_taken, i,
					    switch_node_cnt);
		}
	}

	switches_dist = xcalloc(switch_record_cnt, sizeof(uint32_t));

	for (i = 0; i < switch_record_cnt; i++) {
		if (switch_required[i])
			_topo_add_dist(switches_dist, i);
	}
	/* Add additional resources as required from additional leaf switches */
	prev_rem_nodes = rem_nodes + 1;
	while (1) {
		int best_switch_inx = -1;

		if (prev_rem_nodes == rem_nodes)
			break; 	/* Stalled */
		prev_rem_nodes = rem_nodes;

		for (i = 0; i < switch_record_cnt; i++) {
			if (switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			_topo_choose_best_switch(switches_dist, switch_node_cnt,
						 rem_nodes, switch_cpu_cnt,
						 rem_cpus, i, &best_switch_inx);

		}
		if (best_switch_inx == -1)
			break;

		_topo_add_dist(switches_dist, best_switch_inx);
		/*
		 * NOTE: Ideally we would add nodes in order of resource
		 * availability rather than in order of bitmap position, but
		 * that would add even more complexity and overhead.
		 */
		for (i = 0;
		     next_node_bitmap(
			     switch_node_bitmap[best_switch_inx], &i) &&
		     (topo_eval->max_nodes > 0);
		     i++) {
			if (bit_test(topo_eval->node_map, i) ||
			    !avail_cpu_per_node[i])
				continue;
			topo_eval->avail_cpus = avail_cpu_per_node[i];
			if (!eval_nodes_cpus_to_use(topo_eval, i,
						    rem_max_cpus, min_rem_nodes,
						    &maxtasks, true)) {
				avail_cpu_per_node[i] = 0;
				continue;
			}
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			rem_cpus   -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			bit_set(topo_eval->node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    (!topo_eval->gres_per_job ||
			     gres_sched_test(job_ptr->gres_list_req,
					     job_ptr->job_id))) {
				rc = SLURM_SUCCESS;
				goto fini;
			}
		}
		_decrement_node_cnt(switch_node_cnt[best_switch_inx],
				    best_switch_inx, switch_node_cnt);
		switch_node_cnt[best_switch_inx] = 0;	/* Used all */
	}
	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!topo_eval->gres_per_job ||
	     gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:
	if (rc == SLURM_SUCCESS)
		eval_nodes_clip_socket_cores(topo_eval);

	if (job_ptr->req_switch > 0 && rc == SLURM_SUCCESS) {
		int leaf_switch_count = 0;

		/* Count up leaf switches. */
		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_record_table[i].level != 0)
				continue;
			if (bit_overlap_any(switch_node_bitmap[i], topo_eval->node_map))
				leaf_switch_count++;
		}
		if (time_waiting >= job_ptr->wait4switch) {
			job_ptr->best_switch = true;
			debug3("%pJ waited %ld sec for switches use=%d",
				job_ptr, time_waiting, leaf_switch_count);
		} else if (leaf_switch_count > job_ptr->req_switch) {
			/*
			 * Allocation is for more than requested number of
			 * switches.
			 */
			if ((req_nodes > min_nodes) && best_nodes_bitmap) {
				/* TRUE only for !topo_eval->gres_per_job */
				req_nodes--;
				rem_nodes = req_nodes;
				rem_nodes -= req_node_cnt;
				min_rem_nodes = min_nodes;
				min_rem_nodes -= req_node_cnt;
				topo_eval->max_nodes = org_max_nodes;
				topo_eval->max_nodes -= req_node_cnt;
				rem_cpus = start_rem_cpus;
				rem_max_cpus = start_rem_max_cpus;
				xfree(switches_dist);
				bit_copybits(topo_eval->node_map, start_node_map);
				memcpy(switch_required, req_switch_required,
				       switch_record_cnt * sizeof(int));
				memset(avail_cpu_per_node, 0,
				       node_record_count * sizeof(uint16_t));
				for (i = 0; i < switch_record_cnt; i++)
					bit_copybits(
						switch_node_bitmap[i],
						start_switch_node_bitmap[i]);
				FREE_NULL_BITMAP(avail_nodes_bitmap);
				FREE_NULL_BITMAP(req2_nodes_bitmap);
				FREE_NULL_BITMAP(best_nodes_bitmap);
				FREE_NULL_LIST(best_gres);
				log_flag(SELECT_TYPE, "%pJ goto try_again req_nodes %d",
					 job_ptr, req_nodes);
				goto try_again;
			}
			job_ptr->best_switch = false;
			debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
				job_ptr, time_waiting, job_ptr->req_switch,
				leaf_switch_count, job_ptr->wait4switch);
		} else {
			job_ptr->best_switch = true;
		}
	}

	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	FREE_NULL_BITMAP(start_node_map);
	xfree(avail_cpu_per_node);
	xfree(switch_cpu_cnt);
	if (switch_node_bitmap) {
		for (i = 0; i < switch_record_cnt; i++)
			FREE_NULL_BITMAP(switch_node_bitmap[i]);
		xfree(switch_node_bitmap);
	}
	if (start_switch_node_bitmap) {
		for (i = 0; i < switch_record_cnt; i++)
			FREE_NULL_BITMAP(start_switch_node_bitmap[i]);
		xfree(start_switch_node_bitmap);
	}
	xfree(switch_node_cnt);
	xfree(switch_required);
	xfree(req_switch_required);
	xfree(switches_dist);
	return rc;
}

extern int eval_nodes_tree(topology_eval_t *topo_eval)
{
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;

	static bool have_dragonfly = false;
	static bool topo_optional = false;

	static bool set = false;

	if (!set) {
		if (xstrcasestr(slurm_conf.topology_param, "dragonfly"))
			have_dragonfly = true;
		if (xstrcasestr(slurm_conf.topology_param, "TopoOptional"))
			topo_optional = true;
		set = true;
	}

	xassert(switch_record_cnt);
	xassert(switch_record_table);

	if (!details_ptr->contiguous &&
	    ((topo_optional == false) || topo_eval->job_ptr->req_switch)) {
		/* Perform optimized resource selection based upon topology */
		if (have_dragonfly) {
			return _eval_nodes_dfly(topo_eval);
		} else {
			return _eval_nodes_topo(topo_eval);
		}
	}

	return ESLURM_NOT_SUPPORTED;
}
