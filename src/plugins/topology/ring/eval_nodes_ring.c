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

#define HEAP_PARENT(i) ((i - 1) / 2)
#define HEAP_LEFT(i) (2 * i + 1)
#define HEAP_RIGHT(i) (2 * i + 2)

typedef struct {
	uint16_t ring_idx;
	uint16_t start;
	uint16_t size;
	uint64_t weight;
	uint32_t ring_idle_nodes;
} ring_segment_t;

typedef struct {
	ring_segment_t *segments;
	uint16_t count;
	uint16_t size;
} ring_segment_set_t;

static bool _is_worse(ring_segment_t *a, ring_segment_t *b)
{
	if (a->weight > b->weight)
		return true;

	/* If weights are equal, higher idle node count is worse */
	if ((a->weight == b->weight) &&
	    (a->ring_idle_nodes > b->ring_idle_nodes))
		return true;

	return false;
}

/*
static void _swap_segments(ring_segment_set_t *set, int i, int j)
{
	ring_segment_t temp = set->segments[i];
	set->segments[i] = set->segments[j];
	set->segments[j] = temp;
}
*/

static void _sift_up(ring_segment_set_t *set)
{
	int idx = set->count - 1;
	ring_segment_t tmp = set->segments[idx];
	int parent = HEAP_PARENT(idx);

	while (idx > 0 && _is_worse(&tmp, &set->segments[parent])) {
		set->segments[idx] = set->segments[parent];
		idx = parent;
		parent = HEAP_PARENT(idx);
	}

	set->segments[idx] = tmp;
}

static void _shift_down(ring_segment_set_t *set)
{
	int idx = 0;
	ring_segment_t tmp = set->segments[idx];

	while (1) {
		int max_idx = idx;
		int l, r;
		l = HEAP_LEFT(idx);
		r = HEAP_RIGHT(idx);

		if (l < set->count &&
		    _is_worse(&set->segments[l], &set->segments[max_idx]))
			max_idx = l;

		if (r < set->count &&
		    _is_worse(&set->segments[r], &set->segments[max_idx]))
			max_idx = r;

		if (idx == max_idx)
			break;

		set->segments[idx] = set->segments[max_idx];
		idx = max_idx;
	}

	set->segments[idx] = tmp;
}

static void _heap_push(ring_segment_set_t *set, ring_segment_t *cand)
{
	ring_segment_t *worst;

	if (set->count < set->size) {
		int idx = set->count++;
		set->segments[idx] = *cand;
		_sift_up(set);
		return;
	}

	worst = &set->segments[0];
	if (_is_worse(cand, worst))
		return;

	/* or equal */
	if (cand->weight == worst->weight &&
	    cand->ring_idle_nodes == worst->ring_idle_nodes)
		return;

	/* Replace worst with new candidate and shift down */
	*worst = *cand;
	_shift_down(set);
	return;
}

static void _test_ring(ring_record_t *ring_ptr, topology_eval_t *topo_eval,
		       uint16_t *avail_cpu_per_node, uint32_t min_nodes,
		       ring_segment_set_t *set)
{
	uint16_t start = 0;
	bool found_segment = false;
	uint32_t ring_idle_nodes;
	int32_t last_node = -1;
	job_record_t *job_ptr = topo_eval->job_ptr;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	ring_segment_t best_segment;
	bool check_gres = false;
	uint16_t ring_size = ring_ptr->ring_size;
	ring_idx_map_t *nodes_map = &(ring_ptr->nodes_map);

	if (ring_size < min_nodes)
		return;

	ring_idle_nodes =
		bit_overlap(topo_eval->node_map, ring_ptr->nodes_bitmap);

	if (ring_idle_nodes < min_nodes)
		return;

	if (set->count < set->size) {
		best_segment.weight = UINT64_MAX;
		best_segment.ring_idle_nodes = UINT32_MAX;
	} else {
		best_segment = set->segments[0];
	}

	if ((set->size == 1) && topo_eval->gres_per_job)
		check_gres = true;

	while (start < ring_size) {
		uint16_t step = 1;
		uint64_t group_weight = 0;
		bool group_valid = true;
		list_t *ring_gres = NULL;
		for (int j = 0; j < min_nodes; j++) {
			uint16_t ring_pos = (start + j) % ring_size;
			uint32_t node_idx =
				(*nodes_map)[(start + j) % ring_size];
			node_record_t *node_ptr =
				node_record_table_ptr[node_idx];
			if (!bit_test(topo_eval->node_map, node_idx) ||
			    node_ptr->sched_weight > best_segment.weight) {
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
			if (check_gres) {
				gres_sched_consec(&ring_gres,
						  job_ptr->gres_list_req,
						  avail_res_array[node_idx]
							  ->sock_gres_list);
			}
			if (node_ptr->sched_weight > group_weight)
				group_weight = node_ptr->sched_weight;
		}
		if (group_valid &&
		    ((group_weight < best_segment.weight) ||
		     ((group_weight == best_segment.weight) &&
		      (ring_idle_nodes < best_segment.ring_idle_nodes))) &&
		    (!check_gres ||
		     gres_sched_sufficient(job_ptr->gres_list_req,
					   ring_gres))) {
			best_segment.weight = group_weight;
			best_segment.start = start;
			best_segment.ring_idx = ring_ptr->ring_index;
			best_segment.size = min_nodes;
			best_segment.ring_idle_nodes = ring_idle_nodes;
			found_segment = true;
		}
		start += step;
		FREE_NULL_LIST(ring_gres);
	}

	if (found_segment)
		_heap_push(set, &best_segment);

	return;
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
	ring_segment_set_t best_segments = { 0 };
	uint16_t segment_cnt = 1;

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;

	/* Always use min_nodes */
	topo_eval->gres_per_job = gres_sched_init(job_ptr->gres_list_req);
	rem_nodes = MIN(min_nodes, req_nodes);

	if (details_ptr->segment_size &&
	    (rem_nodes % details_ptr->segment_size)) {
		info("%s: segment_size (%u) does not fit the job size (%d)",
		     __func__, details_ptr->segment_size, rem_nodes);
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		goto fini;
	}

	if (details_ptr->segment_size) {
		segment_cnt = rem_nodes / details_ptr->segment_size;
		rem_nodes = details_ptr->segment_size;
	}

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
			info("%pJ requires nodes which are not in rings",
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

	best_segments.size = segment_cnt;
	best_segments.count = 0;
	best_segments.segments =
		xcalloc(best_segments.size, sizeof(*(best_segments.segments)));
	avail_cpu_per_node =
		xcalloc(node_record_count, sizeof(*avail_cpu_per_node));
	ring_ptr = ctx->rings;

	for (int i = 0; i < ctx->ring_count; i++, ring_ptr++) {
		_test_ring(ring_ptr, topo_eval, avail_cpu_per_node, rem_nodes,
			   &best_segments);
	}

	if (best_segments.count < best_segments.size) {
		log_flag(SELECT_TYPE, "%pJ unable to find all segments",
			 job_ptr);
		rc = ESLURM_BREAK_EVAL;
		goto fini;
	}

	bit_clear_all(topo_eval->node_map);

	for (int j = 0; j < best_segments.count; j++) {
		ring_segment_t *segment = &(best_segments.segments[j]);
		ring_ptr = &(ctx->rings[segment->ring_idx]);
		log_flag(SELECT_TYPE, "%pJ add segment: ring_idx:%u start=%u size:%u ring_nodes:%s",
			 job_ptr, segment->ring_idx, segment->start,
			 segment->size, ring_ptr->nodes);
		for (int i = 0; i < segment->size; i++) {
			uint32_t node_idx =
				ring_ptr->nodes_map[(segment->start + i) %
						    ring_ptr->ring_size];
			topo_eval->avail_cpus = avail_cpu_per_node[node_idx];
			eval_nodes_cpus_to_use(topo_eval, node_idx,
					       rem_max_cpus, min_rem_nodes,
					       &maxtasks, true);
			if (topo_eval->avail_cpus <= 0)
				goto fini;
			rem_cpus -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
			min_rem_nodes--;
			bit_set(topo_eval->node_map, node_idx);
		}
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
	xfree(best_segments.segments);
	return rc;
}
