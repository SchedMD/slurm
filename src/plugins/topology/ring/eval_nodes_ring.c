/*****************************************************************************\
 *  eval_nodes_block.c - Determine order of nodes for job using tree algo.
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

#include "eval_nodes_ring.h"

#include "../common/eval_nodes.h"
#include "../common/gres_sched.h"
#include "src/common/xstring.h"

typedef struct {
	uint16_t ring_idx;
	uint16_t start;
	uint16_t size;
	uint64_t weight;
	uint32_t ring_idle_nodes;
} ring_group_t;

bool _test_ring(ring_record_t *ring_ptr, topology_eval_t *topo_eval,
		uint16_t *avail_cpu_per_node, uint32_t min_nodes,
		ring_group_t *best_group)
{
	uint16_t start = 0;
	bool rc = false;
	uint32_t ring_idle_nodes;
	int32_t last_node = -1;
	job_record_t *job_ptr = topo_eval->job_ptr;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;

	if (ring_ptr->ring_size < min_nodes)
		return false;

	ring_idle_nodes =
		bit_overlap(topo_eval->node_map, ring_ptr->nodes_bitmap);

	if (ring_idle_nodes < min_nodes)
		return false;

	while (start < ring_ptr->ring_size) {
		uint16_t step = 1;
		uint64_t group_weight = 0;
		bool group_valid = true;
		list_t *ring_gres = NULL;
		for (int j = 0; j < min_nodes; j++) {
			uint16_t ring_pos = (start + j) % ring_ptr->ring_size;
			uint32_t node_idx =
				ring_ptr->nodes_map[(start + j) %
						    ring_ptr->ring_size];
			node_record_t *node_ptr =
				node_record_table_ptr[node_idx];
			if (!bit_test(topo_eval->node_map, node_idx) ||
			    node_ptr->sched_weight > best_group->weight) {
				group_valid = false;
				step += j;
				break;
			}

			if (ring_pos > last_node) {
				eval_nodes_select_cores(topo_eval, node_idx,
							min_nodes);
				avail_cpu_per_node[node_idx] =
					topo_eval->avail_cpus;
				last_node = ring_pos;
			}

			if (!avail_cpu_per_node[node_idx]) {
				group_valid = false;
				step += j;
				break;
			}
			gres_sched_consec(&ring_gres, job_ptr->gres_list_req,
					  avail_res_array[node_idx]
						  ->sock_gres_list);
			if (node_ptr->sched_weight > group_weight)
				group_weight = node_ptr->sched_weight;
		}
		if (group_valid &&
		    ((group_weight < best_group->weight) ||
		     ((group_weight == best_group->weight) &&
		      (ring_idle_nodes < best_group->ring_idle_nodes))) &&
		    (!topo_eval->gres_per_job ||
		     gres_sched_sufficient(job_ptr->gres_list_req,
					   ring_gres))) {
			best_group->weight = group_weight;
			best_group->start = start;
			best_group->ring_idx = ring_ptr->ring_index;
			best_group->size = min_nodes;
			best_group->ring_idle_nodes = ring_idle_nodes;
			rc = true;
		}
		start += step;
		FREE_NULL_LIST(ring_gres);
	}
	return rc;
}

extern int eval_nodes_ring(topology_eval_t *topo_eval)
{
	int rc = SLURM_ERROR;
	list_t *best_gres = NULL;
	int64_t rem_max_cpus;
	uint64_t maxtasks;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes; /* remaining resources desired */
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	uint16_t *avail_cpu_per_node = NULL;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	ring_context_t *ctx = topo_eval->tctx->plugin_ctx;
	ring_record_t *ring_ptr;
	ring_group_t best_group = { .weight = UINT64_MAX };
	bool group_found = false;

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;

	/* Always use min_nodes */
	topo_eval->gres_per_job = gres_sched_init(job_ptr->gres_list_req);
	rem_nodes = MIN(min_nodes, req_nodes);

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		int req_node_cnt;
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   topo_eval->node_map)) {
			info("%pJ requires nodes which are not currently available",
			     job_ptr);
			rc = ESLURM_BREAK_EVAL;
			goto fini;
		}

		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   ctx->rings_nodes_bitmap)) {
			info("%pJ requires nodes which are not in ringss",
			     job_ptr);
			rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%pJ required node list has no nodes",
			     job_ptr);
			rc = ESLURM_BREAK_EVAL;
			goto fini;
		}
		if (req_node_cnt > topo_eval->max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, req_node_cnt,
			     topo_eval->max_nodes);
			rc = ESLURM_BREAK_EVAL;
			goto fini;
		}
		if (min_nodes > req_node_cnt) {
			info("%pJ requires less nodes than job size with segment are not supported",
			     job_ptr);
			rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
			goto fini;
		}
		bit_and(topo_eval->node_map, job_ptr->details->req_node_bitmap);
	}

	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);
	maxtasks = eval_nodes_set_max_tasks(job_ptr, rem_max_cpus,
					    topo_eval->max_nodes);

	if (!bit_set_count(topo_eval->node_map)) {
		debug("%pJ node_map is empty", job_ptr);
		rc = ESLURM_BREAK_EVAL;
		goto fini;
	}
	avail_cpu_per_node =
		xcalloc(node_record_count, sizeof(*avail_cpu_per_node));
	ring_ptr = ctx->rings;
	for (int i = 0; i < ctx->ring_count; i++, ring_ptr++) {
		if (_test_ring(ring_ptr, topo_eval, avail_cpu_per_node,
			       min_nodes, &best_group))
			group_found = true;
	}

	if (!group_found) {
		log_flag(SELECT_TYPE, "%pJ unable to find any group", job_ptr);
		rc = ESLURM_BREAK_EVAL;
		goto fini;
	}

	ring_ptr = &(ctx->rings[best_group.ring_idx]);
	log_flag(SELECT_TYPE, "%pJ best group: ring_idx:%u start=%u size:%u ring_nodes:%s",
		 job_ptr, best_group.ring_idx, best_group.start,
		 best_group.size, ring_ptr->nodes);
	bit_clear_all(topo_eval->node_map);

	for (int i = 0; i < best_group.size; i++) {
		uint32_t node_idx = ring_ptr->nodes_map[(best_group.start + i) %
							ring_ptr->ring_size];
		topo_eval->avail_cpus = avail_cpu_per_node[node_idx];
		eval_nodes_cpus_to_use(topo_eval, node_idx, rem_max_cpus,
				       min_rem_nodes, &maxtasks, true);
		if (topo_eval->avail_cpus <= 0)
			goto fini;
		rem_cpus -= topo_eval->avail_cpus;
		rem_max_cpus -= topo_eval->avail_cpus;
		min_rem_nodes--;
		bit_set(topo_eval->node_map, node_idx);
	}

	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!topo_eval->gres_per_job ||
	     gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}

	rc = ESLURM_RETRY_EVAL_HINT;

fini:

	if (rc == SLURM_SUCCESS)
		eval_nodes_clip_socket_cores(topo_eval);
	FREE_NULL_LIST(best_gres);
	xfree(avail_cpu_per_node);
	return rc;
}
