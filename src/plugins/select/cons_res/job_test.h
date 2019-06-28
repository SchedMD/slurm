/*****************************************************************************\
 *  select_cons_res.h
 *****************************************************************************
 *  Copyright (C) 2006 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
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

#ifndef _CR_JOB_TEST_H
#define _CR_JOB_TEST_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_resource_info.h"
#include "src/slurmctld/slurmctld.h"

#include "../cons_common/cons_common.h"

/*
 * _can_job_run_on_node - Given the job requirements, determine which
 *                        resources from the given node (if any) can be
 *                        allocated to this job. Returns the number of
 *                        cpus that can be used by this node and a bitmap
 *                        of available resources for allocation.
 *       NOTE: This process does NOT support overcommitting resources
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores
 * IN node_i        - index of node to be evaluated
 * IN s_p_n         - Expected sockets_per_node (NO_VAL if not known)
 * IN cr_type       - Consumable Resource setting
 * IN test_only     - ignore allocated memory check
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to deselect from the core_map to match the cpu_count.
 */
extern avail_res_t *can_job_run_on_node(struct job_record *job_ptr,
					bitstr_t **core_map,
					const uint32_t node_i,
					uint32_t s_p_n,
					struct node_use_record *node_usage,
					uint16_t cr_type,
					bool test_only,
					bitstr_t **part_core_map);

/*
 * Determine which of these nodes are usable by this job
 *
 * Remove nodes from node_bitmap that don't have enough memory or gres to
 * support the job.
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

/* this is an intermediary step between _select_nodes and _eval_nodes
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low cpu counts for the job and re-evaluates each result */
extern int choose_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			bitstr_t **avail_core, uint32_t min_nodes,
			uint32_t max_nodes, uint32_t req_nodes,
			avail_res_t **avail_res_array, uint16_t cr_type,
			bool prefer_alloc_nodes, gres_mc_data_t *tres_mc_ptr);

/*
 * Given an available node_bitmap, return a corresponding available core_bitmap,
 *	excluding all specialized cores.
 *
 * node_map IN - Bitmap of available nodes
 * core_spec IN - Count of specialized cores requested by the job or NO_VAL
 * RET bitmap of cores available for use by this job or reservation
 * NOTE: Call bit_free() on return value to avoid memory leak.
 */
extern bitstr_t **make_core_bitmap(bitstr_t *node_map, uint16_t core_spec);

#endif /* !_CR_JOB_TEST_H */
