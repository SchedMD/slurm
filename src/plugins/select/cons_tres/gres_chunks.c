/*****************************************************************************\
 *  gres_chunks.c - GRES device chunk filtering and selection
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

#include "src/common/slurm_xlator.h"

#include "gres_chunks.h"

/*
 * Find the chunk domain a request draws from. A typed request draws from its
 * Type's domain. An untyped request draws from every device, so it only has a
 * domain when all of the node's devices are of one chunked Type.
 * IN gres_js - job state for the request
 * IN gres_ns - node state for the GRES
 * RET the domain, or NULL if none
 */
static gres_chunk_domain_t *_find_domain(gres_job_state_t *gres_js,
					 gres_node_state_t *gres_ns)
{
	/* Job did not specify type in request */
	if (!gres_js->type_id) {
		/*
		 * GRES chunks can only be used for untyped job requests on
		 * nodes with exactly one chunk domain.
		 */
		if (gres_ns->chunk_domain_cnt != 1)
			return NULL;
		/*
		 * Make sure all devices on the node are the same type as the
		 * single chunk domain on the node.
		 */
		for (int i = 0; i < gres_ns->topo_cnt; i++) {
			if (gres_ns->topo_type_id[i] !=
			    gres_ns->chunk_domains[0].type_id)
				return NULL;
		}
		return &gres_ns->chunk_domains[0];
	}

	for (int i = 0; i < gres_ns->chunk_domain_cnt; i++) {
		/* Find the node's chunk domain for the job's type */
		if (gres_ns->chunk_domains[i].type_id == gres_js->type_id)
			return &gres_ns->chunk_domains[i];
	}

	return NULL;
}

/*
 * Test whether the chunk rules govern this request. A typed request is
 * governed by its Type's domain, if it has one. An untyped request can draw
 * from any device on the node, so any domain on the node governs it, even when
 * no single domain holds every device and gres_chunks_request_validate()
 * refuses it.
 * IN gres_js - job state for the request
 * IN gres_ns - node state for the GRES
 * RET true if the chunk rules apply
 */
extern bool gres_chunks_apply(gres_job_state_t *gres_js,
			      gres_node_state_t *gres_ns)
{
	if (!gres_ns->chunk_domain_cnt)
		return false;
	if (!gres_js->type_id)
		return true;
	if (!_find_domain(gres_js, gres_ns))
		return false;

	return true;
}

/* Reject request forms that cannot describe a per-node chunk. */
extern int gres_chunks_request_validate(sock_gres_t *sock_gres, bool whole_node)
{
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	gres_node_state_t *gres_ns = sock_gres->gres_state_node->gres_data;

	/*
	 * An untyped request on a node with several Types could take chunked
	 * devices without following their chunks. A whole node job takes every
	 * device on the node, so it never has to fit a chunk.
	 */
	if (!whole_node && !gres_js->type_id && gres_ns->chunk_domain_cnt &&
	    !_find_domain(gres_js, gres_ns)) {
		log_flag(SELECT_TYPE, "%s: %s uses ChunkSizes on a node with several Types, so a request must name a Type",
			 __func__, sock_gres->gres_state_node->gres_name);
		return SLURM_ERROR;
	}

	/*
	 * Chunks need a per-node count. gres_per_job leaves the split to the
	 * scheduler and gres_per_socket asks for a socket shape. gres_per_job
	 * is also derived from a per task request, so only trust it when
	 * nothing else pins the count down.
	 */
	if (!whole_node && gres_chunks_apply(gres_js, gres_ns) &&
	    gres_js->gres_per_socket) {
		log_flag(SELECT_TYPE, "%s: %s uses ChunkSizes, which does not support a per socket request",
			 __func__, sock_gres->gres_state_node->gres_name);
		return SLURM_ERROR;
	}
	if (!whole_node && gres_chunks_apply(gres_js, gres_ns) &&
	    gres_js->gres_per_job && !gres_js->gres_per_node &&
	    !gres_js->gres_per_task) {
		log_flag(SELECT_TYPE, "%s: %s uses ChunkSizes, which does not support a per job request",
			 __func__, sock_gres->gres_state_node->gres_name);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
