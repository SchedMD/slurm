/*****************************************************************************\
 *  eval_nodes_torus3d.c - Determine order of nodes for jobs in 3D torus.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "eval_nodes_torus3d.h"

#include <string.h>

#include "../common/common_topo.h"
#include "../common/eval_nodes.h"
#include "../common/gres_sched.h"
#include "src/common/node_conf.h"

typedef struct {
	bitstr_t *avail_nodes;
	bitstr_t *nodes_bitmap;
	torus3d_placement_t *placement;
	int32_t frag_cost;
	uint32_t torus_idle_nodes;
	uint64_t weight;
	bool valid;
} torus3d_candidate_t;

static void _get_frag_cost(torus3d_candidate_t *cand,
			   torus3d_placement_t *placement)
{
	int32_t frag_cost = 0;

	if (!placement) {
		cand->frag_cost = 0;
		return;
	}

	for (int i = 0; i < placement->anchor_count; i++) {
		if (bit_overlap_any(placement->anchor_bitmaps[i],
				    cand->nodes_bitmap) &&
		    bit_super_set(placement->anchor_bitmaps[i],
				  cand->avail_nodes))
			frag_cost++;
	}
	cand->frag_cost = frag_cost;

	return;
}

static bool _is_better_candidate(torus3d_candidate_t *cand,
				 torus3d_candidate_t *best,
				 torus3d_placement_t *next_placement)
{
	if (!best->valid)
		return true;

	if (cand->weight < best->weight)
		return true;
	else if (cand->weight > best->weight)
		return false;

	if (cand->placement == best->placement) {
		if (best->frag_cost < 0)
			_get_frag_cost(best, next_placement);
		if (cand->frag_cost < 0)
			_get_frag_cost(cand, next_placement);

		if (cand->frag_cost < best->frag_cost)
			return true;
		else if (cand->frag_cost > best->frag_cost)
			return false;
	}

	if (cand->torus_idle_nodes < best->torus_idle_nodes)
		return true;

	return false;
}

static void _check_anchor(torus3d_placement_t *placement, int anchor_idx,
			  torus3d_placement_t *next_placement,
			  topology_eval_t *topo_eval, uint32_t rem_nodes,
			  bool check_gres, int *avail_cpu_per_node,
			  bitstr_t *avail_cpu_set, uint32_t torus_idle_nodes,
			  torus3d_candidate_t *best)
{
	job_record_t *job_ptr = topo_eval->job_ptr;
	uint64_t group_weight = 0;
	bool candidate_valid = true;
	list_t *torus_gres = NULL;
	bitstr_t *placement_bitmap = placement->anchor_bitmaps[anchor_idx];

	/* Skip anchors where not all cells are occupied by valid nodes */
	if (placement->anchor_nodes[anchor_idx] != placement->size)
		return;

	if (job_ptr->details->req_node_bitmap &&
	    !bit_super_set(job_ptr->details->req_node_bitmap, placement_bitmap))
		return;

	for (int node_idx = 0; next_node_bitmap(placement_bitmap, &node_idx);
	     node_idx++) {
		node_record_t *node_ptr = node_record_table_ptr[node_idx];

		if (!bit_test(topo_eval->node_map, node_idx) ||
		    (best->valid && node_ptr->sched_weight > best->weight)) {
			candidate_valid = false;
			break;
		}

		if (!bit_test(avail_cpu_set, node_idx)) {
			eval_nodes_select_cores(topo_eval, node_idx, rem_nodes);
			avail_cpu_per_node[node_idx] = topo_eval->avail_cpus;
			bit_set(avail_cpu_set, node_idx);
		}

		if (!avail_cpu_per_node[node_idx]) {
			candidate_valid = false;
			break;
		}

		if (check_gres) {
			gres_sched_consec(&torus_gres, job_ptr->gres_list_req,
					  topo_eval->avail_res_array[node_idx]
						  ->sock_gres_list);
		}

		if (node_ptr->sched_weight > group_weight)
			group_weight = node_ptr->sched_weight;
	}

	if (candidate_valid &&
	    (!check_gres ||
	     gres_sched_sufficient(job_ptr->gres_list_req, torus_gres))) {
		torus3d_candidate_t cand = {
			.avail_nodes = topo_eval->node_map,
			.nodes_bitmap = placement_bitmap,
			.weight = group_weight,
			.torus_idle_nodes = torus_idle_nodes,
			.valid = true,
			.placement = placement,
			.frag_cost = -1,
		};
		if (_is_better_candidate(&cand, best, next_placement))
			*best = cand;
	}

	FREE_NULL_LIST(torus_gres);
}

static void _check_torus(torus3d_record_t *torus, topology_eval_t *topo_eval,
			 uint32_t rem_nodes, bool check_gres,
			 int *avail_cpu_per_node, bitstr_t *avail_cpu_set,
			 torus3d_candidate_t *best)
{
	uint32_t torus_idle_nodes =
		bit_overlap(topo_eval->node_map, torus->nodes_bitmap);

	for (int i = 0; i < torus->placement_count; i++) {
		torus3d_placement_t *placement = &torus->placements[i];
		torus3d_placement_t *next_placement = NULL;
		if (placement->size != rem_nodes)
			continue;

		for (int j = i + 1;
		     j < torus->placement_count && !next_placement; j++) {
			if (torus->placements[j].size > rem_nodes)
				next_placement = &torus->placements[j];
		}

		for (int j = 0; j < placement->anchor_count; j++)
			_check_anchor(placement, j, next_placement, topo_eval,
				      rem_nodes, check_gres, avail_cpu_per_node,
				      avail_cpu_set, torus_idle_nodes, best);
	}
}

extern int eval_nodes_torus3d(topology_eval_t *topo_eval)
{
	int rc = SLURM_ERROR;
	int64_t rem_max_cpus;
	uint64_t maxtasks;
	int rem_cpus, rem_nodes;
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	int *avail_cpu_per_node = NULL;
	bitstr_t *avail_cpu_set = NULL;
	bitstr_t *selected_bitmap = NULL;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	torus3d_context_t *ctx = topo_eval->tctx->plugin_ctx;
	torus3d_candidate_t best = { 0 };
	bool size_supported = false;
	bool check_gres = false;
	uint16_t segment_size = details_ptr->segment_size;
	int segment_cnt = 0, rem_segment_cnt = 0;
	bitstr_t *req_nodes_bitmap = NULL;

	if (details_ptr->arbitrary_tpn) {
		info("topology torus3d does not support arbitrary tasks distribution");
		rc = ESLURM_NOT_SUPPORTED;
		goto fini;
	}

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;

	topo_eval->gres_per_job = gres_sched_init(job_ptr->gres_list_req);
	rem_nodes = MIN(min_nodes, req_nodes);

	if (segment_size > rem_nodes) {
		info("Ignoring segment_size (%u): larger than job size (%u)",
		     segment_size, rem_nodes);
		segment_size = 0;
	}

	if (segment_size) {
		if (rem_nodes % segment_size) {
			info("%pJ segment_size (%u) does not divide job size (%u)",
			     job_ptr, details_ptr->segment_size, min_nodes);
			return ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		}

		segment_cnt = rem_nodes / details_ptr->segment_size;
		rem_nodes = details_ptr->segment_size;
		rem_cpus /= segment_cnt;
		rem_segment_cnt = segment_cnt;
	}

	for (int i = 0; i < ctx->record_count; i++) {
		torus3d_record_t *torus = &ctx->records[i];
		for (int j = 0; j < torus->placement_count; j++) {
			if (torus->placements[j].size == rem_nodes) {
				size_supported = true;
				break;
			}
		}
		if (size_supported)
			break;
	}

	if (!size_supported)
		return ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		int req_node_cnt;
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   topo_eval->node_map)) {
			info("%pJ requires nodes which are not currently available",
		     job_ptr);
			rc = ESLURM_TOPO_REQ_NODES_NOT_AVAIL;
			topo_eval->eval_action = EVAL_ACTION_BREAK;
			goto fini;
		}

		if (ctx->placement_nodes_bitmap &&
		    !bit_super_set(job_ptr->details->req_node_bitmap,
				   ctx->placement_nodes_bitmap)) {
			info("%pJ requires nodes which are not in torus3d placements",
		     job_ptr);
			rc = ESLURM_TOPO_REQ_NODES_NO_MATCH_TOPO;
			topo_eval->eval_action = EVAL_ACTION_BREAK;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%pJ required node list has no nodes",
		     job_ptr);
			rc = ESLURM_TOPO_REQ_NODES_NOT_AVAIL;
			topo_eval->eval_action = EVAL_ACTION_BREAK;
			goto fini;
		}
		if (req_node_cnt > topo_eval->max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
		     job_ptr, req_node_cnt,
		     topo_eval->max_nodes);
			rc = ESLURM_TOPO_MAX_NODE_LIMIT;
			topo_eval->eval_action = EVAL_ACTION_BREAK;
			goto fini;
		}
		if (segment_cnt > 1) {
			if (min_nodes > req_node_cnt) {
				info("%pJ requires less nodes than job size with segment are not supported",
				     job_ptr);
				rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
				goto fini;
			}
			bit_and(topo_eval->node_map,
				job_ptr->details->req_node_bitmap);
			req_nodes_bitmap = job_ptr->details->req_node_bitmap;
			job_ptr->details->req_node_bitmap = NULL;
		}
	}

	if (!bit_set_count(topo_eval->node_map)) {
		debug("%pJ node_map is empty", job_ptr);
		rc = ESLURM_TOPO_EMPTY_NODE_MAP;
		topo_eval->eval_action = EVAL_ACTION_BREAK;
		goto fini;
	}

	if (topo_eval->gres_per_job)
		check_gres = true;

	avail_cpu_per_node =
		xcalloc(node_record_count, sizeof(*avail_cpu_per_node));
	avail_cpu_set = bit_alloc(node_record_count);
	selected_bitmap = bit_alloc(node_record_count);

next_segment:

	rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr, rem_nodes);
	maxtasks = eval_nodes_set_max_tasks(job_ptr, rem_max_cpus, rem_nodes);

	memset(&best, 0, sizeof(best));

	for (int i = 0; i < ctx->record_count; i++) {
		_check_torus(&ctx->records[i], topo_eval, rem_nodes, check_gres,
			     avail_cpu_per_node, avail_cpu_set, &best);
	}

	if (!best.valid) {
		log_flag(SELECT_TYPE, "%pJ unable to find torus3d placement",
			 job_ptr);
		if (bit_ffs(selected_bitmap) >= 0) {
			bit_copybits(topo_eval->node_map, selected_bitmap);
			rc = ESLURM_TOPO_SEGMENT_NO_FIT;
			topo_eval->eval_action = EVAL_ACTION_RETRY_HINT;
		} else {
			rc = ESLURM_TOPO_NO_FIT;
			topo_eval->eval_action = EVAL_ACTION_BREAK;
		}
		goto fini;
	}

	for (int node_idx = 0; next_node_bitmap(best.nodes_bitmap, &node_idx);
	     node_idx++) {
		topo_eval->avail_cpus = avail_cpu_per_node[node_idx];
		eval_nodes_cpus_to_use(topo_eval, node_idx, rem_max_cpus,
				       rem_nodes, &maxtasks, true);
		if (topo_eval->avail_cpus <= 0) {
			rc = ESLURM_TOPO_INSUFFICIENT_RESOURCES;
			topo_eval->eval_action = EVAL_ACTION_RETRY_HINT;
			goto fini;
		}
		rem_cpus -= topo_eval->avail_cpus;
		rem_max_cpus -= topo_eval->avail_cpus;
		rem_nodes--;
	}

	if ((rem_nodes > 0) || (rem_cpus > 0) ||
	    (topo_eval->gres_per_job &&
	     !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = ESLURM_TOPO_INSUFFICIENT_RESOURCES;
		topo_eval->eval_action = EVAL_ACTION_RETRY_HINT;
		goto fini;
	}

	bit_or(selected_bitmap, best.nodes_bitmap);

	if (--rem_segment_cnt > 0) {
		bit_and_not(topo_eval->node_map, best.nodes_bitmap);
		rem_nodes = details_ptr->segment_size;
		rem_cpus = details_ptr->min_cpus / segment_cnt;
		goto next_segment;
	}

	rc = SLURM_SUCCESS;
	bit_copybits(topo_eval->node_map, selected_bitmap);

fini:
	if (req_nodes_bitmap)
		job_ptr->details->req_node_bitmap = req_nodes_bitmap;
	if (rc == SLURM_SUCCESS)
		eval_nodes_clip_socket_cores(topo_eval);
	xfree(avail_cpu_per_node);
	FREE_NULL_BITMAP(avail_cpu_set);
	FREE_NULL_BITMAP(selected_bitmap);
	return rc;
}
