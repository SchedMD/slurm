/*****************************************************************************\
 *  job_test.h - Determine if job can be allocated resources.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
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

#ifndef _CONS_TRES_JOB_TEST_H
#define _CONS_TRES_JOB_TEST_H

#include "select_cons_tres.h"

/*
 * _can_job_run_on_node - Given the job requirements, determine which
 *                        resources from the given node (if any) can be
 *                        allocated to this job. Returns a structure identifying
 *                        the resources available for allocation to this job.
 *       NOTE: This process does NOT support overcommitting resources
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - per-node bitmap of available cores
 * IN node_i        - index of node to be evaluated
 * IN s_p_n         - Expected sockets_per_node (NO_VAL if not limited)
 * IN cr_type       - Consumable Resource setting
 * IN test_only     - ignore allocated memory check
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * RET Available resources. Call _array() to release memory.
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to de-select from the core_map to match the cpu_count.
 */
extern avail_res_t *can_job_run_on_node(struct job_record *job_ptr,
					bitstr_t **core_map,
					const uint32_t node_i,
					uint32_t s_p_n,
					struct node_use_record *node_usage,
					uint16_t cr_type, bool test_only,
					bitstr_t **part_core_map);


/*
 * build_row_bitmaps: A job has been removed from the given partition,
 *                    so the row_bitmap(s) need to be reconstructed.
 *                    Optimize the jobs into the least number of rows,
 *                    and make the lower rows as dense as possible.
 *
 * IN p_ptr - the partition that has jobs to be optimized
 * IN job_ptr - pointer to single job removed, pass NULL to completely rebuild
 */
extern void build_row_bitmaps(struct part_res_record *p_ptr,
			      struct job_record *job_ptr);

/* test for conflicting core bitmap elements */
extern int can_job_fit_in_row(struct job_resources *job,
			      struct part_row_data *r_ptr);

/*
 * Determine which of these nodes are usable by this job
 *
 * Remove nodes from node_bitmap that don't have enough memory or other
 * resources to support this job.
 *
 * Return SLURM_ERROR if a required node can't be used.
 *
 * if node_state = NODE_CR_RESERVED, clear node_bitmap (if node is required
 *                                   then should we return NODE_BUSY!?!)
 *
 * if node_state = NODE_CR_ONE_ROW, then this node can only be used by
 *                                  another NODE_CR_ONE_ROW job
 *
 * if node_state = NODE_CR_AVAILABLE AND:
 *  - job_node_req = NODE_CR_RESERVED, then we need idle nodes
 *  - job_node_req = NODE_CR_ONE_ROW, then we need idle or non-sharing nodes
 */
extern int verify_node_state(struct part_res_record *cr_part_ptr,
			     struct job_record *job_ptr,
			     bitstr_t *node_bitmap,
			     uint16_t cr_type,
			     struct node_use_record *node_usage,
			     enum node_cr_state job_node_req,
			     bitstr_t **exc_cores, bool qos_preemptor);


extern void add_job_to_res(job_resources_t *job_resrcs_ptr,
			   struct part_row_data *r_ptr,
			   const uint16_t *bits_per_node);

/*
 * Bit a core bitmap array of available cores
 * node_bitmap IN - Nodes available for use
 * core_spec IN - Specialized core specification, NO_VAL16 if none
 * RET core bitmap array, one per node. Use free_core_array() to release memory
 */
extern bitstr_t **mark_avail_cores(bitstr_t *node_bitmap, uint16_t core_spec);

/*
 * This is an intermediary step between _select_nodes() and _eval_nodes()
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low CPU counts for the job and re-evaluates each result.
 *
 * RET SLURM_SUCCESS or an error code
 */
extern int choose_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			bitstr_t **avail_core, uint32_t min_nodes,
			uint32_t max_nodes, uint32_t req_nodes,
			avail_res_t **avail_res_array, uint16_t cr_type,
			bool prefer_alloc_nodes, gres_mc_data_t *tres_mc_ptr);

#endif /* !_CONS_TRES_JOB_TEST_H */
