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

#include <math.h>

#include "eval_nodes_block.h"

#include "../common/eval_nodes.h"
#include "../common/gres_sched.h"

static int _cmp_bblock(const void *a, const void *b)
{
	int ca = *((int *) a);
	int cb = *((int *) b);

	if (ca < cb)
		return 1;
	else if (ca > cb)
		return -1;

	return 0;
}

static bool _bblocks_in_same_block(int block_inx1, int block_inx2,
				   int block_level)
{
	if ((block_inx1 >> block_level) == (block_inx2 >> block_level))
		return true;
	return false;
}

static void _choose_best_bblock(bitstr_t *bblock_required,
				int llblock_level, int rem_nodes,
				uint32_t *nodes_on_bblock,
				uint32_t *nodes_on_llblock,
				int i, bool *best_same_block,
				bool *best_fit, int *best_bblock_inx)
{
	bool fit = (nodes_on_bblock[i] >= rem_nodes);
	bool same_block = false;

	/*
	 * Minimialize number of llblock
	 */
	if (nodes_on_llblock &&
	    !_bblocks_in_same_block(*best_bblock_inx, i, llblock_level)) {
		bool llblock_fit, best_llblock_fit;
		uint32_t best_llblock_node_cnt, llblock_node_cnt;

		for (int j = (i & (~0 << llblock_level));
		     ((j < block_record_cnt) &&
		      (j <= (i | ~(~0 << llblock_level))));
		     j++) {
			if (!bit_test(bblock_required, j))
				continue;
			if ((same_block =
			     _bblocks_in_same_block(j, i, llblock_level)))
				break;
		}

		if ((*best_bblock_inx == -1) ||
		    (same_block && !(*best_same_block))) {
			*best_bblock_inx = i;
			*best_fit = fit;
			*best_same_block = same_block;
			return;
		}

		if (!same_block && (*best_same_block))
			return;

		/*
		 * New llblock needed or both bblocks in alredy used llbock
		 */
		best_llblock_node_cnt =  nodes_on_llblock[(*best_bblock_inx >>
							   llblock_level)];
		llblock_node_cnt = nodes_on_llblock[(i >> llblock_level)];

		llblock_fit = (llblock_node_cnt >= rem_nodes);
		best_llblock_fit = (best_llblock_node_cnt >= rem_nodes);

		/*
		 * Try to use llblock big enough to meet job requaierment
		 */
		if (llblock_fit && !best_llblock_fit) {
			*best_bblock_inx = i;
			*best_fit = fit;
			*best_same_block = same_block;
			return;
		}

		if (!llblock_fit && best_llblock_fit)
			return;

		if (llblock_fit && best_llblock_fit) {
			/*
			 * If both bblock are on llblock which meet requirement
			 * choose llblock with less nodes to avoid fragmentation
			 */
			if (llblock_node_cnt < best_llblock_node_cnt) {
				*best_bblock_inx = i;
				*best_fit = fit;
				*best_same_block = same_block;
				return;
			}

			if (llblock_node_cnt > best_llblock_node_cnt)
				return;

		} else {
			/*
			 * If neither of bblocks are on llblock which meet
			 * requirement choose llblock with more nodes to
			 * minimalize nuber of llblock
			 */
			if (llblock_node_cnt > best_llblock_node_cnt) {
				*best_bblock_inx = i;
				*best_fit = fit;
				*best_same_block = same_block;
				return;
			}

			if (llblock_node_cnt < best_llblock_node_cnt)
				return;
		}
	}

	/*
	 * Minimialize number of bblock
	 */
	if (*best_bblock_inx == -1 || (fit && !(*best_fit)) ||
	    (!fit && !(*best_fit) && (nodes_on_bblock[i] >=
				      nodes_on_bblock[*best_bblock_inx])) ||
	    (fit && (nodes_on_bblock[i] <=
		     nodes_on_bblock[*best_bblock_inx]))) {
		*best_bblock_inx = i;
		*best_fit = fit;
		return;
	}
}

extern int eval_nodes_block(topology_eval_t *topo_eval)
{
	bitstr_t **block_node_bitmap = NULL;	/* nodes on this block */
	bitstr_t **bblock_node_bitmap = NULL;	/* nodes on this base block */
	uint32_t block_node_cnt = 0;	/* total nodes on block */
	uint32_t *nodes_on_bblock = NULL;	/* total nodes on bblock */
	bitstr_t *req_nodes_bitmap = NULL;	/* required node bitmap */
	bitstr_t *req2_nodes_bitmap = NULL;	/* required+lowest prio nodes */
	bitstr_t *best_nodes_bitmap = NULL;	/* required+low prio nodes */
	bitstr_t *bblock_bitmap = NULL;
	int *bblock_block_inx = NULL;
	bitstr_t *bblock_required = NULL;
	int i, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt, best_node_cnt, req_node_cnt = 0;
	list_t *best_gres = NULL;
	block_record_t *block_ptr;
	list_t *node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	list_itr_t *iter;
	node_record_t *node_ptr;
	int64_t rem_max_cpus;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	job_record_t *job_ptr = topo_eval->job_ptr;
	job_details_t *details_ptr = job_ptr->details;
	bool requested, sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	int block_inx = -1;
	uint64_t block_lowest_weight = 0;
	int block_cnt = -1, bblock_per_block;
	int prev_rem_nodes;
	avail_res_t **avail_res_array = topo_eval->avail_res_array;
	uint32_t min_nodes = topo_eval->min_nodes;
	uint32_t req_nodes = topo_eval->req_nodes;
	int max_llblock;
	int llblock_level;
	int llblock_size;
	int llblock_cnt = 0;
	uint32_t *nodes_on_llblock = NULL;
	int block_level;
	int bblock_per_llblock;
	uint64_t maxtasks;

	int segment_cnt = 1, rem_segment_cnt = 0;
	bitstr_t *orig_node_map = bit_copy(topo_eval->node_map);
	bitstr_t *alloc_node_map = NULL;
	uint32_t orig_max_nodes = topo_eval->max_nodes;

	topo_eval->avail_cpus = 0;

	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;

	/* Always use min_nodes */
	topo_eval->gres_per_job = gres_sched_init(job_ptr->gres_list_req);
	rem_nodes = MIN(min_nodes, req_nodes);

	if (details_ptr->segment_size > bblock_node_cnt) {
		info("%pJ segment (%u) > bblock_node_cnt (%u) is not supported",
		     job_ptr, details_ptr->segment_size, bblock_node_cnt);
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		goto fini;
	}

	if (details_ptr->segment_size &&
	    (rem_nodes % details_ptr->segment_size)) {
		info("%s: segment_size (%u) does not fit the job size (%d)",
		     __func__, details_ptr->segment_size, rem_nodes);
		rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
		goto fini;
	}

	if (details_ptr->segment_size) {
		segment_cnt = rem_nodes / details_ptr->segment_size;
		rem_segment_cnt = segment_cnt;
		rem_nodes = details_ptr->segment_size;
	}

	bblock_per_block = ((rem_nodes + bblock_node_cnt - 1) /
			    bblock_node_cnt);
	block_level = ceil(log2(bblock_per_block));
	if (block_level > 0)
		llblock_level = bit_fls_from_bit(block_levels, block_level - 1);
	else
		llblock_level = 0;
	block_level = bit_ffs_from_bit(block_levels, block_level);

	xassert(llblock_level >= 0);

	bblock_per_llblock = (1 << llblock_level);
	llblock_size = bblock_per_llblock * bblock_node_cnt;
	max_llblock = (rem_nodes + llblock_size - 1) / llblock_size;

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (segment_cnt > 1) {
			info("%pJ requires nodes with segment are not supported",
			     job_ptr);
			rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
			goto fini;
		}
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   topo_eval->node_map)) {
			info("%pJ requires nodes which are not currently available",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   blocks_nodes_bitmap)) {
			info("%pJ requires nodes which are not in blocks",
			     job_ptr);
			rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
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

next_segment:
	/*
	 * Add required nodes to job allocation and
	 * build list of node bitmaps, sorted by weight
	 */
	if (rem_segment_cnt) {
		rem_nodes = details_ptr->segment_size;
		min_rem_nodes = min_nodes / segment_cnt;
		topo_eval->max_nodes = orig_max_nodes / segment_cnt;
		rem_cpus = details_ptr->min_cpus / segment_cnt;
		if (details_ptr->max_cpus != NO_VAL)
			rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr,
								   rem_nodes);
		else
			rem_max_cpus = details_ptr->max_cpus / segment_cnt;

		max_llblock = (rem_nodes + llblock_size - 1) / llblock_size;
	} else
		rem_max_cpus = eval_nodes_get_rem_max_cpus(details_ptr,
							   rem_nodes);
	maxtasks = eval_nodes_set_max_tasks(job_ptr, rem_max_cpus,
					    topo_eval->max_nodes);

	if (!bit_set_count(topo_eval->node_map)) {
		debug("%pJ node_map is empty",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	if (!avail_cpu_per_node)
		avail_cpu_per_node = xcalloc(node_record_count,
					     sizeof(uint16_t));
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
			rem_cpus -= topo_eval->avail_cpus;
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

	if (block_level < 0) {
		/* Number of base blocks in block */
		bblock_per_block = block_record_cnt;
		block_cnt = 1;
	} else {
		/* Number of base blocks in block */
		bblock_per_block = (1 << block_level);
		block_cnt = (block_record_cnt + bblock_per_block - 1) /
			bblock_per_block;
	}

	if ((bblock_per_block != (bblock_per_llblock * max_llblock)) &&
	    !nodes_on_llblock) {
		llblock_cnt = (block_record_cnt + bblock_per_llblock - 1) /
			      bblock_per_llblock;
		nodes_on_llblock = xcalloc(llblock_cnt, sizeof(uint32_t));
	}

	log_flag(SELECT_TYPE, "%s: bblock_per_block:%u rem_nodes:%u llblock_cnt:%u max_llblock:%d llblock_level:%d",
		 __func__, bblock_per_block, rem_nodes, llblock_cnt,
		 max_llblock, llblock_level);

	if (!bblock_required)
		bblock_required = bit_alloc(block_record_cnt);
	else
		bit_clear_all(bblock_required);

	if (!alloc_node_map) {
		block_node_bitmap = xcalloc(block_cnt, sizeof(bitstr_t *));
		bblock_block_inx = xcalloc(block_record_cnt, sizeof(int));
	}

	for (i = 0, block_ptr = block_record_table; i < block_record_cnt;
	     i++, block_ptr++) {
		int block_inx_tmp = i / bblock_per_block;
		if (alloc_node_map)
			;
		else if (block_node_bitmap[block_inx_tmp])
			bit_or(block_node_bitmap[block_inx_tmp],
			       block_ptr->node_bitmap);
		else
			block_node_bitmap[block_inx_tmp] =
				bit_copy(block_ptr->node_bitmap);
		bblock_block_inx[i] = block_inx_tmp;
		if (nodes_on_llblock) {
			int llblock_inx = i / bblock_per_llblock;
			nodes_on_llblock[llblock_inx] +=
				bit_overlap(block_ptr->node_bitmap,
					    topo_eval->node_map);
		}
	}

	block_inx = -1;
	for (i = 0; i < block_cnt; i++) {
		uint32_t block_cpus = 0;
		uint32_t avail_bnc = 0;
		uint32_t bnc;

		bit_and(block_node_bitmap[i], topo_eval->node_map);

		bnc = bit_set_count(block_node_bitmap[i]);
		if (!nodes_on_llblock) {
			avail_bnc = bnc;
		} else {
			int llblock_per_block = (bblock_per_block /
						 bblock_per_llblock);
			int offset = i * llblock_per_block;
			int tmp_max_llblock;
			llblock_per_block = MIN(llblock_per_block,
						llblock_cnt - offset);
			qsort(&nodes_on_llblock[offset], llblock_per_block,
			      sizeof(int), _cmp_bblock);
			tmp_max_llblock = MIN(max_llblock, llblock_per_block);
			for (j = 0; j < tmp_max_llblock; j++)
				avail_bnc += nodes_on_llblock[offset + j];
		}
		/*
		 * Count total CPUs of the intersection of topo_eval->node_map
		 * and block_node_bitmap.
		 */
		for (j = 0; (node_ptr = next_node_bitmap(block_node_bitmap[i],
							 &j));
		     j++)
			block_cpus += avail_res_array[j]->avail_cpus;
		if (req_nodes_bitmap &&
		    bit_overlap_any(req_nodes_bitmap, block_node_bitmap[i])) {
			if (block_inx == -1) {
				block_inx = i;
				break;
			}
		}
		if (!eval_nodes_enough_nodes(avail_bnc, rem_nodes, min_nodes,
					     req_nodes) ||
		    (rem_cpus > block_cpus))
			continue;
		/*
		 * Select the block:
		 * 	1) with lowest weight nodes
		 * 	2) with lowest sufficient count of nodes -
		 * 	   to minimize fragmentation
		 */
		if (!req_nodes_bitmap &&
		    (nw = list_find_first(node_weight_list,
					  eval_nodes_topo_node_find,
					  block_node_bitmap[i]))) {
			if ((block_inx == -1) ||
			    (nw->weight < block_lowest_weight) ||
			    ((nw->weight == block_lowest_weight) &&
			     (bnc <= block_node_cnt))) {
				block_inx = i;
				block_lowest_weight = nw->weight;
				block_node_cnt = bnc;
			}
		}
	}

	if (!req_nodes_bitmap) {
		bit_clear_all(topo_eval->node_map);
	}

	if (block_inx == -1) {
		log_flag(SELECT_TYPE, "%pJ unable to find block",
			 job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that all specificly required nodes are in one block  */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap, block_node_bitmap[block_inx])) {
		rc = SLURM_ERROR;
		info("%pJ requires nodes that do not have shared block",
		     job_ptr);
		goto fini;
	}

	if (req_nodes_bitmap) {
		int last_llblock = -1;
		bit_and(topo_eval->node_map, req_nodes_bitmap);

		for (i = 0; (i < block_record_cnt) && nodes_on_llblock; i++) {
			if (block_inx != bblock_block_inx[i])
				continue;
			if (bit_overlap_any(
				    req_nodes_bitmap,
				    block_record_table[i].node_bitmap)) {
				bit_set(bblock_required, i);
				if (!_bblocks_in_same_block(last_llblock, i,
							    llblock_level)) {
					max_llblock--;
					last_llblock = i;
				}
			}
		}
		if (max_llblock < 0) {
			rc = ESLURM_REQUESTED_TOPO_CONFIG_UNAVAILABLE;
			info("%pJ requires nodes exceed maximum llblock limit due to required nodes",
			     job_ptr);
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (topo_eval->max_nodes <= 0) {
			rc = SLURM_ERROR;
			info("%pJ requires nodes exceed maximum node limit",
			     job_ptr);
			goto fini;
		}
	}

	requested = false;
	sufficient = false;
	best_node_cnt = 0;
	best_cpu_cnt = 0;

	if (!best_nodes_bitmap)
		best_nodes_bitmap = bit_alloc(node_record_count);
	else
		bit_clear_all(best_nodes_bitmap);

	if (req2_nodes_bitmap)
		bit_clear_all(req2_nodes_bitmap);

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
			if (!bit_test(block_node_bitmap[block_inx], i))
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
		int last_llblock = -1;
		for (i = 0;
		     (next_node_bitmap(req2_nodes_bitmap, &i) &&
		      (topo_eval->max_nodes > 0));
		     i++) {
			topo_eval->avail_cpus = avail_cpu_per_node[i];
			if (!eval_nodes_cpus_to_use(topo_eval, i,
						    rem_max_cpus, min_rem_nodes,
						    &maxtasks, true)) {
				/*
				 * To many restricted gpu cores were removed
				 * due to gres layout.
				 */
				bit_clear(req2_nodes_bitmap, i);
				continue;
			}
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			rem_cpus -= topo_eval->avail_cpus;
			rem_max_cpus -= topo_eval->avail_cpus;
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
			debug("%pJ reached maximum node limit",
			      job_ptr);
			goto fini;
		}
		for (i = 0; i < block_record_cnt; i++) {
			if (block_inx != bblock_block_inx[i])
				continue;
			if (bit_test(bblock_required, i)) {
				last_llblock = i;
				continue;
			}
			if (bit_overlap_any(
				    req2_nodes_bitmap,
				    block_record_table[i].node_bitmap)) {
				bit_set(bblock_required, i);
				if (!_bblocks_in_same_block(last_llblock, i,
							    llblock_level)) {
					max_llblock--;
					last_llblock = i;
				}
			}
		}
	}

	if (max_llblock < 0) {
		rc = SLURM_ERROR;
		info("%pJ requires nodes exceed maximum llblock limit due to node weights",
		     job_ptr);
		goto fini;
	}

	/* Add additional resources for already required base block */
	if (req_nodes_bitmap || req2_nodes_bitmap) {
		for (i = 0; i < block_record_cnt; i++) {
			if (!bit_test(bblock_required, i))
				continue;
			if (!bblock_bitmap)
				bblock_bitmap = bit_copy(
					block_record_table[i].node_bitmap);
			else
				bit_copybits(bblock_bitmap,
					     block_record_table[i].node_bitmap);

			bit_and(bblock_bitmap, block_node_bitmap[block_inx]);
			bit_and(bblock_bitmap, best_nodes_bitmap);
			bit_and_not(bblock_bitmap, topo_eval->node_map);

			for (j = 0; next_node_bitmap(bblock_bitmap, &j); j++) {
				if (!avail_cpu_per_node[j])
					continue;
				topo_eval->avail_cpus = avail_cpu_per_node[j];
				if (!eval_nodes_cpus_to_use(topo_eval, j,
							    rem_max_cpus,
							    min_rem_nodes,
							    &maxtasks, true))
					continue;
				rem_nodes--;
				min_rem_nodes--;
				topo_eval->max_nodes--;
				rem_cpus -= topo_eval->avail_cpus;
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
		}
	}

	if (!nodes_on_bblock)
		nodes_on_bblock = xcalloc(block_record_cnt,
					  sizeof(*nodes_on_bblock));
	if (!bblock_node_bitmap)
		bblock_node_bitmap = xcalloc(block_record_cnt,
					     sizeof(bitstr_t *));
	if (nodes_on_llblock)
		memset(nodes_on_llblock, 0, llblock_cnt * sizeof(uint32_t));

	for (i = 0; i < block_record_cnt; i++) {
		if (block_inx != bblock_block_inx[i])
			continue;
		if (bit_test(bblock_required, i))
			continue;
		if (!bblock_node_bitmap[i])
			bblock_node_bitmap[i] =
				bit_copy(block_record_table[i].node_bitmap);
		else
			bit_copybits(bblock_node_bitmap[i],
				     block_record_table[i].node_bitmap);
		bit_and(bblock_node_bitmap[i], block_node_bitmap[block_inx]);
		bit_and(bblock_node_bitmap[i], best_nodes_bitmap);
		nodes_on_bblock[i] = bit_set_count(bblock_node_bitmap[i]);
		if (nodes_on_llblock) {
			int llblock_inx = i / bblock_per_llblock;
			nodes_on_llblock[llblock_inx] += nodes_on_bblock[i];
		}
	}

	prev_rem_nodes = rem_nodes + 1;
	while (1) {
		int best_bblock_inx = -1;
		bool best_fit = false, best_same_block = true;
		bitstr_t *best_bblock_bitmap = NULL;
		if (prev_rem_nodes == rem_nodes)
			break; 	/* Stalled */
		prev_rem_nodes = rem_nodes;
		for (i = 0; i < block_record_cnt; i++) {
			if (block_inx != bblock_block_inx[i])
				continue;
			if (bit_test(bblock_required, i))
				continue;
			_choose_best_bblock(bblock_required, llblock_level,
					    rem_nodes, nodes_on_bblock,
					    nodes_on_llblock, i,
					    &best_same_block, &best_fit,
					    &best_bblock_inx);
		}
		log_flag(SELECT_TYPE, "%s: rem_nodes:%d  best_bblock_inx:%d",
			 __func__, rem_nodes, best_bblock_inx);
		if (best_bblock_inx == -1)
			break;

		if ((max_llblock <= 0) && !best_same_block) {
			log_flag(SELECT_TYPE, "%s: min_rem_nodes:%d can't add more bblocks due to llblock limit",
				 __func__, min_rem_nodes);
			break;
		}

		best_bblock_bitmap = bblock_node_bitmap[best_bblock_inx];
		bit_and_not(best_bblock_bitmap, topo_eval->node_map);
		bit_set(bblock_required, best_bblock_inx);
		/*
		 * NOTE: Ideally we would add nodes in order of resource
		 * availability rather than in order of bitmap position, but
		 * that would add even more complexity and overhead.
		 */
		for (i = 0; next_node_bitmap(best_bblock_bitmap, &i) &&
			     (topo_eval->max_nodes > 0); i++) {
			if (!avail_cpu_per_node[i])
				continue;
			topo_eval->avail_cpus = avail_cpu_per_node[i];
			if (!eval_nodes_cpus_to_use(topo_eval, i,
					            rem_max_cpus,
					            min_rem_nodes,
					            &maxtasks, true))
				continue;
			rem_nodes--;
			min_rem_nodes--;
			topo_eval->max_nodes--;
			rem_cpus -= topo_eval->avail_cpus;
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
		if (!best_same_block)
			max_llblock--;
	}

	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!topo_eval->gres_per_job ||
	     gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:
	if (rem_segment_cnt && !rc ) {
		if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			char *node_names;
			node_names = bitmap2node_name(topo_eval->node_map);
			info("Segment:%d nodes:%s",
			     segment_cnt - rem_segment_cnt, node_names);
			xfree(node_names);
		}

		if (--rem_segment_cnt > 0) {
			if (alloc_node_map)
				bit_or(alloc_node_map, topo_eval->node_map);
			else
				alloc_node_map = bit_copy(topo_eval->node_map);

			FREE_NULL_LIST(best_gres);
			FREE_NULL_LIST(node_weight_list);
			bit_copybits(topo_eval->node_map, orig_node_map);
			bit_and_not(topo_eval->node_map, alloc_node_map);
			log_flag(SELECT_TYPE, "%s: rem_segment_cnt:%d",
				 __func__, rem_segment_cnt);
			goto next_segment;
		} else if (alloc_node_map) {
			bit_or(topo_eval->node_map, alloc_node_map);
		}
	}


	if (rc == SLURM_SUCCESS)
		eval_nodes_clip_socket_cores(topo_eval);
	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	FREE_NULL_BITMAP(bblock_bitmap);
	FREE_NULL_BITMAP(orig_node_map);
	FREE_NULL_BITMAP(alloc_node_map);
	xfree(avail_cpu_per_node);
	xfree(bblock_block_inx);
	if (block_node_bitmap) {
		for (i = 0; i < block_cnt; i++)
			FREE_NULL_BITMAP(block_node_bitmap[i]);
		xfree(block_node_bitmap);
	}
	if (bblock_node_bitmap) {
		for (i = 0; i < block_record_cnt; i++)
			FREE_NULL_BITMAP(bblock_node_bitmap[i]);
		xfree(bblock_node_bitmap);
	}
	xfree(nodes_on_bblock);
	xfree(nodes_on_llblock);
	FREE_NULL_BITMAP(bblock_required);
	return rc;
}
