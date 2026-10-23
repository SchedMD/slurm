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

/* Test whether cnt is exactly one of the configured chunk sizes */
static bool _is_chunk_size(gres_chunk_domain_t *dom, uint64_t cnt)
{
	for (int i = 0; i < dom->sizes_cnt; i++) {
		if (dom->sizes[i] == cnt)
			return true;
	}

	return false;
}

/*
 * Test whether this node's per device bitmaps are available. Only
 * _build_sock_gres_by_topo() fills them in. A node that has not registered
 * since the controller started falls back to a builder that leaves them NULL,
 * and the chunk layout cannot be checked without them.
 * RET true if the chunk rules can be evaluated on this node
 */
static bool _have_dev_bitmaps(sock_gres_t *sock_gres)
{
	if (sock_gres->bits_any_sock)
		return true;

	if (!sock_gres->bits_by_sock)
		return false;

	for (int s = 0; s < sock_gres->sock_cnt; s++) {
		if (sock_gres->bits_by_sock[s])
			return true;
	}

	return false;
}

/*
 * Build the devices this job could use on this node: those on sockets
 * with usable cores plus those tied to no socket, less what is allocated.
 * IN sock_gres - job/node GRES details
 * IN gres_ns - node state for the GRES
 * IN used_cores_on_sock - cores the job holds per socket, NULL to ignore
 * RET bitmap, must be freed by caller. NULL if none.
 */
static bitstr_t *_build_usable_dev_bitmap(sock_gres_t *sock_gres,
					  gres_node_state_t *gres_ns,
					  uint32_t *used_cores_on_sock)
{
	bitstr_t *usable = NULL;

	if (sock_gres->bits_any_sock)
		usable = bit_copy(sock_gres->bits_any_sock);

	if (sock_gres->bits_by_sock && sock_gres->cnt_by_sock) {
		for (int s = 0; s < sock_gres->sock_cnt; s++) {
			if (!sock_gres->cnt_by_sock[s] ||
			    !sock_gres->bits_by_sock[s])
				continue;
			/*
			 * Under enforce-binding the selection below only picks
			 * from sockets the job holds cores on, so a chunk
			 * chosen elsewhere can never be filled.
			 */
			if (used_cores_on_sock && !used_cores_on_sock[s])
				continue;
			if (!usable)
				usable = bit_copy(sock_gres->bits_by_sock[s]);
			else
				bit_or(usable, sock_gres->bits_by_sock[s]);
		}
	}

	if (!usable)
		return NULL;

	/* Only consider free devices unless testing against all GRES */
	if (!sock_gres->use_total_gres && gres_ns->gres_bit_alloc)
		bit_and_not(usable, gres_ns->gres_bit_alloc);

	return usable;
}

/*
 * Find the largest chunk size that still has a free aligned chunk.
 * RET that size, or 0 if no chunk is free
 */
static uint64_t _largest_free_chunk(sock_gres_t *sock_gres,
				    gres_node_state_t *gres_ns,
				    gres_chunk_domain_t *dom)
{
	bitstr_t *usable = NULL;
	uint64_t best = 0;

	/*
	 * No core binding here: this runs before the job is placed, so the
	 * sockets it will hold cores on are not known yet.
	 */
	if (!(usable = _build_usable_dev_bitmap(sock_gres, gres_ns, NULL)))
		return 0;

	/* Sizes are ascending, so walk them largest first */
	for (int i = (dom->sizes_cnt - 1); i >= 0; i--) {
		int size = dom->sizes[i];

		/* Chunks of this size start at every multiple of size */
		for (int start = 0; (start + size) <= dom->order_len;
		     start += size) {
			int j;

			/* Stop at the first device in this chunk not free */
			for (j = 0; j < size; j++) {
				if (!bit_test(usable, dom->order[start + j]))
					break;
			}
			/* Every device is free, so this is the largest size */
			if (j == size) {
				best = size;
				goto fini;
			}
		}
	}

fini:
	FREE_NULL_BITMAP(usable);

	return best;
}

/*
 * Pick a free aligned chunk. Best fit: among the free chunks, take the one
 * whose next larger chunk has the fewest free devices, so larger free chunks
 * stay whole for later jobs.
 * IN usable - devices that may be picked
 * IN dom - the request's chunk domain
 * IN gres_needed - a configured chunk size, or every device in the domain
 * RET bitmap of the chunk's devices, must be freed by caller. NULL if none.
 */
static bitstr_t *_pick_chunk(bitstr_t *usable, gres_chunk_domain_t *dom,
			     int gres_needed)
{
	bitstr_t *picked = NULL;
	int best_pos = -1, best_score = -1, parent_size = 0;

	/* The next size up bounds how much free space a pick can break up */
	for (int i = 0; i < dom->sizes_cnt; i++) {
		if ((int) dom->sizes[i] > gres_needed) {
			parent_size = dom->sizes[i];
			break;
		}
	}
	if (!parent_size)
		parent_size = gres_needed;

	/* Chunks of this size start at every multiple of gres_needed */
	for (int start = 0; (start + gres_needed) <= dom->order_len;
	     start += gres_needed) {
		int score = 0, j, parent;

		/* Skip this chunk if any of its devices is not free */
		for (j = 0; j < gres_needed; j++) {
			if (!bit_test(usable, dom->order[start + j]))
				break;
		}
		if (j < gres_needed)
			continue;

		/*
		 * Score this chunk by the free devices in the next larger
		 * chunk holding it. Each size divides the next, so the parent
		 * stays in range.
		 */
		parent = (start / parent_size) * parent_size;
		for (j = 0; j < parent_size; j++) {
			if (bit_test(usable, dom->order[parent + j]))
				score++;
		}

		/* Keep the lowest score, ties go to the first in ChunkOrder */
		if ((best_pos < 0) || (score < best_score)) {
			best_pos = start;
			best_score = score;
		}
	}

	if (best_pos < 0)
		return NULL;

	picked = bit_alloc(bit_size(usable));
	for (int j = 0; j < gres_needed; j++)
		bit_set(picked, dom->order[best_pos + j]);

	return picked;
}

/*
 * Pick a free aligned chunk and narrow selection on this node to it.
 * IN/OUT sock_gres - job/node GRES details, narrowed to the chunk
 * IN gres_ns - node state for the GRES
 * IN dom - the request's chunk domain
 * IN gres_needed - a configured chunk size, or every device in the domain
 * IN used_cores_on_sock - cores the job holds per socket, NULL to ignore
 * RET SLURM_SUCCESS, or ESLURM_INVALID_GRES if no chunk is free
 */
static int _select_chunk(sock_gres_t *sock_gres, gres_node_state_t *gres_ns,
			 gres_chunk_domain_t *dom, int gres_needed,
			 uint32_t *used_cores_on_sock)
{
	bitstr_t *usable = NULL, *picked = NULL;

	if (!(usable = _build_usable_dev_bitmap(sock_gres, gres_ns,
						used_cores_on_sock)))
		return ESLURM_INVALID_GRES;

	picked = _pick_chunk(usable, dom, gres_needed);
	FREE_NULL_BITMAP(usable);
	if (!picked)
		return ESLURM_INVALID_GRES;

	/*
	 * Narrow to the chunk so the selection below cannot pick outside it.
	 * Counts are only lowered, so a socket remove_unusable zeroed for core
	 * binding stays zeroed.
	 */
	if (sock_gres->bits_any_sock) {
		bit_and(sock_gres->bits_any_sock, picked);
		sock_gres->cnt_any_sock =
			MIN(sock_gres->cnt_any_sock,
			    bit_set_count(sock_gres->bits_any_sock));
	}
	if (sock_gres->bits_by_sock) {
		for (int s = 0; s < sock_gres->sock_cnt; s++) {
			if (!sock_gres->bits_by_sock[s])
				continue;
			bit_and(sock_gres->bits_by_sock[s], picked);
			if (sock_gres->cnt_by_sock) {
				int cnt = 0;

				cnt = bit_set_count(sock_gres->bits_by_sock[s]);
				sock_gres->cnt_by_sock[s] =
					MIN(sock_gres->cnt_by_sock[s], cnt);
			}
		}
	}
	sock_gres->total_cnt = MIN(sock_gres->total_cnt, gres_needed);
	FREE_NULL_BITMAP(picked);

	return SLURM_SUCCESS;
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

/* Count the devices in bits that are part of picked, 0 if either is NULL */
static uint64_t _picked_cnt(bitstr_t *bits, bitstr_t *picked)
{
	if (!bits || !picked)
		return 0;

	return bit_overlap(bits, picked);
}

/*
 * Only one chunk is handed out, so the largest free one bounds this node.
 * IN/OUT sock_gres - availability, capped to the largest free chunk
 * IN gres_ns - node state for the GRES
 * IN dom - the request's chunk domain
 * IN chunk_gres - devices the job needs on this node, 0 if not fixed
 * IN enforce_binding - GRES must be co-allocated with cores
 * IN/OUT near_gres_cnt - available GRES near usable cores
 */
static void _cap_at_free_chunk(sock_gres_t *sock_gres,
			       gres_node_state_t *gres_ns,
			       gres_chunk_domain_t *dom, uint64_t chunk_gres,
			       bool enforce_binding, uint64_t *near_gres_cnt)
{
	uint64_t chunk_max = _largest_free_chunk(sock_gres, gres_ns, dom);
	bitstr_t *picked = NULL, *usable = NULL;
	bool by_pick = enforce_binding && chunk_gres;

	/*
	 * total_cnt can end up below the sum of the per-socket counts, so
	 * gres_filter_sock_core() must recompute it from the cap when it drops
	 * a socket, rather than subtract the socket's count from it.
	 */
	sock_gres->total_cnt = MIN(sock_gres->total_cnt, chunk_max);
	sock_gres->links_cap = chunk_max;

	/*
	 * Under enforce-binding, devices only come from sockets the job holds
	 * cores on, and the counts below decide where its cores land. Count
	 * only the devices of one free chunk of the size needed, so its cores
	 * land on every socket that chunk needs. Capping each socket on its
	 * own would let a socket with too few free devices draw the cores, or
	 * spread them over sockets that no single chunk fills.
	 */
	if (by_pick &&
	    (usable = _build_usable_dev_bitmap(sock_gres, gres_ns, NULL))) {
		picked = _pick_chunk(usable, dom, chunk_gres);
		FREE_NULL_BITMAP(usable);
	}

	/*
	 * gres_filter_sock_core() runs before selection and counts per socket,
	 * so leaving these uncapped lets it commit cores on sockets the chosen
	 * chunk does not reach.
	 */
	if (by_pick)
		sock_gres->cnt_any_sock =
			MIN(sock_gres->cnt_any_sock,
			    _picked_cnt(sock_gres->bits_any_sock, picked));
	else
		sock_gres->cnt_any_sock =
			MIN(sock_gres->cnt_any_sock, chunk_max);
	if (sock_gres->cnt_by_sock) {
		for (int s = 0; s < sock_gres->sock_cnt; s++) {
			bitstr_t *sock_bits = NULL;
			uint64_t sock_max = chunk_max;

			if (sock_gres->bits_by_sock)
				sock_bits = sock_gres->bits_by_sock[s];
			if (by_pick)
				sock_max = _picked_cnt(sock_bits, picked);
			sock_gres->cnt_by_sock[s] =
				MIN(sock_gres->cnt_by_sock[s], sock_max);
		}
	}
	FREE_NULL_BITMAP(picked);

	/*
	 * Taken from total_cnt above, so without this the node reports more
	 * near GPUs than available ones.
	 */
	*near_gres_cnt = MIN(*near_gres_cnt, sock_gres->total_cnt);
}

/*
 * Apply chunk size rules and cap availability at a free aligned chunk, so an
 * impossible node fails here, not late in gres_select_filter_select_and_set().
 */
extern int gres_chunks_filter(sock_gres_t *sock_gres, bool whole_node,
			      bool single_node, uint16_t task_per_node,
			      bool enforce_binding, uint64_t *min_gres,
			      uint64_t *near_gres_cnt)
{
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	gres_node_state_t *gres_ns = sock_gres->gres_state_node->gres_data;
	gres_chunk_domain_t *dom = _find_domain(gres_js, gres_ns);
	uint64_t dev_cnt, chunk_gres = 0;

	/* No ChunkSizes apply to this request */
	if (!dom)
		return SLURM_SUCCESS;

	/*
	 * A whole node job takes every device on the node, so it already holds
	 * the whole chunk domain and the size rules do not apply to it.
	 */
	if (whole_node)
		return SLURM_SUCCESS;

	dev_cnt = dom->order_len;

	/*
	 * A chunk needs a definite per node count. gres_per_node is one, and a
	 * per task request becomes one once the tasks per node are fixed, by
	 * ntasks-per-node or a one node job.
	 */
	if (gres_js->gres_per_node) {
		chunk_gres = gres_js->gres_per_node;
	} else if (gres_js->gres_per_task && task_per_node &&
		   (task_per_node != NO_VAL16)) {
		chunk_gres = gres_js->gres_per_task * task_per_node;
	} else if (gres_js->gres_per_task && single_node) {
		chunk_gres = gres_js->gres_per_job;
	}

	/*
	 * Guessing it from the total instead only works when the job happens
	 * to land the way the guess assumed, which fails late in
	 * gres_select_filter_select_and_set() and repeats forever.
	 */
	if (!chunk_gres && gres_js->gres_per_task) {
		log_flag(SELECT_TYPE, "%s: %s uses ChunkSizes, so a per task request must fix the count per node, with --ntasks-per-node or a one node allocation",
			 __func__, sock_gres->gres_state_node->gres_name);
		return SLURM_ERROR;
	}

	if (chunk_gres) {
		/* A job spanning nodes must take each whole domain */
		if (!single_node && (chunk_gres != dev_cnt)) {
			log_flag(SELECT_TYPE, "%s: %s may span nodes, so it must request all %"PRIu64" devices per node rather than %"PRIu64,
				 __func__,
				 sock_gres->gres_state_node->gres_name,
				 dev_cnt, chunk_gres);
			return SLURM_ERROR;
		}

		/* Only a partial request has to fit one chunk */
		if ((chunk_gres != dev_cnt) &&
		    !_is_chunk_size(dom, chunk_gres)) {
			log_flag(SELECT_TYPE, "%s: %s needs %"PRIu64" devices, which is not one of the configured ChunkSizes",
				 __func__,
				 sock_gres->gres_state_node->gres_name,
				 chunk_gres);
			return SLURM_ERROR;
		}

		*min_gres = MAX(*min_gres, chunk_gres);
	}

	/*
	 * Skip the cap when the device bitmaps are missing, or a node that has
	 * not registered yet reads as having no free chunk and is rejected as
	 * never runnable.
	 */
	if ((chunk_gres != dev_cnt) && _have_dev_bitmaps(sock_gres))
		_cap_at_free_chunk(sock_gres, gres_ns, dom, chunk_gres,
				   enforce_binding, near_gres_cnt);

	return SLURM_SUCCESS;
}

/* Narrow final selection to one chunk before per-request device assignment. */
extern int gres_chunks_select(sock_gres_t *sock_gres, job_record_t *job_ptr,
			      node_record_t *node_ptr, uint32_t task_cnt,
			      uint32_t *used_cores_on_sock)
{
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	gres_node_state_t *gres_ns = sock_gres->gres_state_node->gres_data;
	gres_chunk_domain_t *dom = _find_domain(gres_js, gres_ns);
	int gres_needed;
	int rc = ESLURM_INVALID_GRES;

	/* No ChunkSizes apply to this request */
	if (!dom)
		return SLURM_SUCCESS;

	/*
	 * Skip nodes without the device bitmaps, matching the cap in
	 * _foreach_remove_unusable(). A node that has not registered since the
	 * controller started has none, and would fail here every cycle.
	 */
	if (!_have_dev_bitmaps(sock_gres))
		return SLURM_SUCCESS;

	/* Shared GRES are slices of one device, not chunks of devices */
	if (gres_id_shared(sock_gres->gres_state_job->config_flags))
		return SLURM_SUCCESS;

	/*
	 * A whole node job takes every device on the node, which is the whole
	 * chunk domain and more, so there is no chunk to pick for it.
	 */
	if (job_ptr->details &&
	    (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED))
		return SLURM_SUCCESS;

	gres_needed = dom->order_len;
	if (gres_js->gres_per_node) {
		gres_needed = gres_js->gres_per_node;
	} else if (gres_js->gres_per_task) {
		gres_needed = gres_js->gres_per_task * task_cnt;
	}

	/*
	 * Recheck the chunk rules at final selection using the actual node and
	 * task counts. A job spanning nodes must take every device on each.
	 */
	if ((bit_set_count(job_ptr->job_resrcs->node_bitmap) > 1) &&
	    (gres_needed != dom->order_len)) {
		log_flag(SELECT_TYPE, "%s: %pJ spans nodes, so it must request all %d %s devices on node %s rather than %d",
			 __func__, job_ptr, dom->order_len,
			 sock_gres->gres_state_node->gres_name, node_ptr->name,
			 gres_needed);
		return ESLURM_INVALID_GRES;
	}

	/* Otherwise it must take every device or exactly one chunk size */
	if ((gres_needed != dom->order_len) &&
	    !_is_chunk_size(dom, gres_needed)) {
		log_flag(SELECT_TYPE, "%s: %pJ needs %d %s devices on node %s, which is not one of the configured ChunkSizes",
			 __func__, job_ptr, gres_needed,
			 sock_gres->gres_state_node->gres_name, node_ptr->name);
		return ESLURM_INVALID_GRES;
	}

	/*
	 * Commit to one chunk and narrow the socket bitmaps to it, so the
	 * selection that follows works unchanged and cannot reach outside it.
	 * Under enforce-binding _set_node_bits() only fills from sockets the
	 * job holds cores on, so a chunk chosen anywhere else fails every
	 * cycle. Keep the pick inside those sockets.
	 */
	rc = _select_chunk(sock_gres, gres_ns, dom, gres_needed,
			   (job_ptr->bit_flags & GRES_ENFORCE_BIND) ?
				   used_cores_on_sock :
				   NULL);
	if (rc != SLURM_SUCCESS) {
		log_flag(SELECT_TYPE, "%s: %pJ found no free chunk of %d %s devices on node %s",
			 __func__, job_ptr, gres_needed,
			 sock_gres->gres_state_node->gres_name, node_ptr->name);
		return rc;
	}

	return SLURM_SUCCESS;
}
