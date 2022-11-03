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
#include "src/common/pack.h"
#include "src/interfaces/select.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/slurm_resource_info.h"
#include "src/slurmctld/slurmctld.h"

#include "../cons_common/cons_common.h"

/*
 * can_job_run_on_node - Given the job requirements, determine which
 *                       resources from the given node (if any) can be
 *                       allocated to this job. Returns the number of
 *                       cpus that can be used by this node and a bitmap
 *                       of available resources for allocation.
 *       NOTE: This process does NOT support overcommitting resources
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores
 * IN node_i        - index of node to be evaluated
 * IN s_p_n         - Expected sockets_per_node (NO_VAL if not known)
 * IN cr_type       - Consumable Resource setting
 * IN test_only     - Determine if job could ever run, ignore allocated memory
 *		      check
 * IN will_run      - Determining when a pending job can start
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * RET Available resources. Call _array() to release memory.
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to deselect from the core_map to match the cpu_count.
 */
extern avail_res_t *can_job_run_on_node(job_record_t *job_ptr,
					bitstr_t **core_map,
					const uint32_t node_i,
					uint32_t s_p_n,
					node_use_record_t *node_usage,
					uint16_t cr_type,
					bool test_only, bool will_run,
					bitstr_t **part_core_map);

/* this is an intermediary step between _select_nodes and _eval_nodes
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low cpu counts for the job and re-evaluates each result */
extern int choose_nodes(job_record_t *job_ptr, bitstr_t *node_map,
			bitstr_t **avail_core, uint32_t min_nodes,
			uint32_t max_nodes, uint32_t req_nodes,
			avail_res_t **avail_res_array, uint16_t cr_type,
			bool prefer_alloc_nodes, gres_mc_data_t *tres_mc_ptr);

#endif /* !_CR_JOB_TEST_H */
