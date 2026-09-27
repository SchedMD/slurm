/*****************************************************************************\
 *  gres_chunks.h - GRES device chunk filtering and selection
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

#ifndef _GRES_CHUNKS_H
#define _GRES_CHUNKS_H

#include "src/interfaces/gres.h"

/*
 * Test whether the chunk rules govern a request. An untyped request is
 * governed by any chunk domain on the node.
 * IN gres_js - job state for the request
 * IN gres_ns - node state for the GRES
 * RET true if a chunk domain on this node governs the request
 */
extern bool gres_chunks_apply(gres_job_state_t *gres_js,
			      gres_node_state_t *gres_ns);

/*
 * Reject request forms that cannot describe a per-node chunk.
 * IN sock_gres - job/node GRES details
 * IN whole_node - whether the allocation requires the whole node
 * RET SLURM_SUCCESS for a supported request form, SLURM_ERROR otherwise
 */
extern int gres_chunks_request_validate(sock_gres_t *sock_gres,
					bool whole_node);

/*
 * Validate the per-node chunk count and cap available GRES.
 * IN/OUT sock_gres - per-socket availability, capped to the largest free chunk
 * IN whole_node - whether the allocation requires the whole node
 * IN single_node - the job cannot be spread over more than one node
 * IN task_per_node - fixed tasks per node, or NO_VAL16
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN/OUT min_gres - minimum GRES count required on this node
 * IN/OUT near_gres_cnt - available GRES near usable cores
 * RET SLURM_SUCCESS if feasible, SLURM_ERROR otherwise
 */
extern int gres_chunks_filter(sock_gres_t *sock_gres, bool whole_node,
			      bool single_node, uint16_t task_per_node,
			      bool enforce_binding, uint64_t *min_gres,
			      uint64_t *near_gres_cnt);

/*
 * Recheck the placed job's count and restrict device selection to one chunk.
 * IN/OUT sock_gres - per-socket bitmaps and counts, narrowed to the chunk
 * IN job_ptr - job and its selected nodes
 * IN node_ptr - node being assigned devices
 * IN task_cnt - tasks assigned to this node
 * IN used_cores_on_sock - allocated cores per socket; used for enforce-binding
 * RET SLURM_SUCCESS or ESLURM_INVALID_GRES
 */
extern int gres_chunks_select(sock_gres_t *sock_gres, job_record_t *job_ptr,
			      node_record_t *node_ptr, uint32_t task_cnt,
			      uint32_t *used_cores_on_sock);

#endif /* _GRES_CHUNKS_H */
