/*****************************************************************************\
 *  gres_stepmgr.c - Functions for gres used only in the slurmctld
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Derived in large part from code previously in common/gres.c
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

#include "gres_stepmgr.h"
#include "src/common/assoc_mgr.h"
#include "src/common/xstring.h"

typedef struct {
	bitstr_t *core_bitmap;
	bool decr_job_alloc;
	uint64_t gres_needed;
	gres_key_t *job_search_key;
	uint64_t max_gres;
	list_t *node_gres_list;
	int node_offset;
	int rc;
	list_t *step_gres_list_alloc;
	gres_state_t *gres_state_step;
	uint64_t *step_node_mem_alloc;
	slurm_step_id_t tmp_step_id;
	int total_gres_cpu_cnt;
} foreach_step_alloc_t;

typedef struct {
	uint64_t gres_cnt;
	bool ignore_alloc;
	gres_key_t *job_search_key;
	slurm_step_id_t *step_id;
} foreach_gres_cnt_t;

typedef struct {
	bitstr_t *core_bitmap;
	gres_state_t *gres_state_node;
	uint32_t job_id;
	list_t **job_gres_list;
	bool new_alloc;
	int node_cnt;
	int node_index;
	int node_offset;
	char *node_name;
	int rc;
} foreach_explicit_alloc_t;

/*
 * Determine if specific GRES index on node is available to a job's allocated
 *	cores
 * IN core_bitmap - bitmap of cores allocated to the job on this node
 * IN/OUT alloc_core_bitmap - cores already allocated, NULL if don't care,
 *		updated when the function returns true
 * IN gres_ns - GRES data for this node
 * IN gres_inx - index of GRES being considered for use
 * IN job_gres_ptr - GRES data for this job
 * RET true if available to those core, false otherwise
 */
static bool _cores_on_gres(bitstr_t *core_bitmap, bitstr_t *alloc_core_bitmap,
			   gres_node_state_t *gres_ns, int gres_inx,
			   gres_job_state_t *gres_js)
{
	int i, avail_cores;

	if ((core_bitmap == NULL) || (gres_ns->topo_cnt == 0))
		return true;

	for (i = 0; i < gres_ns->topo_cnt; i++) {
		if (!gres_ns->topo_gres_bitmap[i])
			continue;
		if (bit_size(gres_ns->topo_gres_bitmap[i]) < gres_inx)
			continue;
		if (!bit_test(gres_ns->topo_gres_bitmap[i], gres_inx))
			continue;
		if (gres_js->type_name &&
		    (!gres_ns->topo_type_name[i] ||
		     (gres_js->type_id != gres_ns->topo_type_id[i])))
			continue;
		if (!gres_ns->topo_core_bitmap[i])
			return true;
		if (bit_size(gres_ns->topo_core_bitmap[i]) !=
		    bit_size(core_bitmap))
			break;
		avail_cores = bit_overlap(gres_ns->topo_core_bitmap[i],
					  core_bitmap);
		if (avail_cores && alloc_core_bitmap) {
			avail_cores -= bit_overlap(gres_ns->
						   topo_core_bitmap[i],
						   alloc_core_bitmap);
			if (avail_cores) {
				bit_or(alloc_core_bitmap,
				       gres_ns->topo_core_bitmap[i]);
			}
		}
		if (avail_cores)
			return true;
	}
	return false;
}

static gres_job_state_t *_get_job_alloc_gres_ptr(list_t *job_gres_list_alloc,
						 gres_state_t *gres_state_in,
						 uint32_t type_id,
						 char *type_name,
						 uint32_t node_cnt)
{
	gres_key_t job_search_key;
	gres_job_state_t *gres_js;
	gres_state_t *gres_state_job;

	/* Find in job_gres_list_alloc if it exists */
	job_search_key.config_flags = gres_state_in->config_flags;
	job_search_key.plugin_id = gres_state_in->plugin_id;
	job_search_key.type_id = type_id;

	if (!(gres_state_job = list_find_first(job_gres_list_alloc,
					       gres_find_job_by_key_exact_type,
					       &job_search_key))) {
		gres_js = xmalloc(sizeof(*gres_js));
		gres_js->type_id = type_id;
		gres_js->type_name = xstrdup(type_name);
		gres_js->node_cnt = node_cnt;

		gres_js->gres_bit_alloc = xcalloc(
			node_cnt,
			sizeof(*gres_js->gres_bit_alloc));
		gres_js->gres_cnt_node_alloc = xcalloc(
			node_cnt,
			sizeof(*gres_js->gres_cnt_node_alloc));
		gres_js->gres_bit_step_alloc = xcalloc(
			node_cnt,
			sizeof(*gres_js->gres_bit_step_alloc));
		gres_js->gres_cnt_step_alloc = xcalloc(
			node_cnt,
			sizeof(*gres_js->gres_cnt_step_alloc));

		gres_state_job = xmalloc(sizeof(*gres_state_job));
		gres_state_job->config_flags = gres_state_in->config_flags;
		/* Use gres_state_node here as plugin_id might be NO_VAL */
		gres_state_job->plugin_id = gres_state_in->plugin_id;
		gres_state_job->gres_data = gres_js;
		gres_state_job->gres_name = xstrdup(gres_state_in->gres_name);
		gres_state_job->state_type = GRES_STATE_TYPE_JOB;

		list_append(job_gres_list_alloc, gres_state_job);
	} else
		gres_js = gres_state_job->gres_data;

	return gres_js;
}

static uint64_t _get_sharing_cnt_from_shared_cnt(gres_job_state_t *gres_js,
						 bitstr_t *left_over_bits,
						 int n, int64_t shared_cnt)
{
	uint64_t sharing_cnt = 0;

	if (!gres_js->gres_per_bit_alloc || !gres_js->gres_per_bit_alloc[n]) {
		error("Allocated shared gres with no gres_per_bit_alloc");
		return shared_cnt;
	}

	for (int i = 0; (i = bit_ffs_from_bit(left_over_bits, i)) >= 0; i++) {
		if (shared_cnt <= 0)
			break;
		sharing_cnt++;
		shared_cnt -= gres_js->gres_per_bit_alloc[n][i];
	}

	return sharing_cnt;
}

static uint64_t _cnt_topo_gres(gres_job_state_t *gres_js, int n,
			  bitstr_t *topo_gres_bitmap)
{
	uint64_t gres_cnt = 0;

	if (gres_js->gres_per_bit_alloc && gres_js->gres_per_bit_alloc[n]) {
		for (int i = 0;
		     (i = bit_ffs_from_bit(gres_js->gres_bit_alloc[n], i)) >= 0;
		     i++) {
			if (bit_test(topo_gres_bitmap, i))
				gres_cnt += gres_js->gres_per_bit_alloc[n][i];
		}
	} else {
		gres_cnt = bit_overlap(gres_js->gres_bit_alloc[n],
				       topo_gres_bitmap);
	}

	return gres_cnt;
}

static void _copy_matching_gres_per_bit(gres_job_state_t *gres_js,
					gres_job_state_t *gres_js_alloc, int n)
{
	if (!gres_js_alloc->gres_per_bit_alloc) {
		gres_js_alloc->gres_per_bit_alloc = xcalloc(
			gres_js_alloc->node_cnt, sizeof(uint64_t *));
	}
	gres_js_alloc->gres_per_bit_alloc[n] = xcalloc(
		bit_size(gres_js_alloc->gres_bit_alloc[n]), sizeof(uint64_t));

	for (int i = 0;
	     (i = bit_ffs_from_bit(gres_js_alloc->gres_bit_alloc[n], i)) >= 0;
	     i++) {
		gres_js_alloc->gres_per_bit_alloc[n][i] =
			gres_js->gres_per_bit_alloc[n][i];
	}
}

static void _allocate_gres_bits(gres_node_state_t *gres_ns,
				gres_job_state_t *gres_js,
				int64_t gres_bits,
				int64_t *gres_cnt,
				int node_offset,
				bool shared_gres,
				bitstr_t *core_bitmap,
				bool overlap_all_cores)
{
	bitstr_t *alloc_core_bitmap = NULL;

	if (core_bitmap && overlap_all_cores)
		alloc_core_bitmap = bit_alloc(bit_size(core_bitmap));

	for (int i = 0; i < gres_bits && *gres_cnt > 0; i++) {
		if (bit_test(gres_ns->gres_bit_alloc, i))
			continue;
		if (core_bitmap &&
		    !_cores_on_gres(core_bitmap, alloc_core_bitmap, gres_ns, i,
				    gres_js))
			continue;
		bit_set(gres_ns->gres_bit_alloc, i);
		bit_set(gres_js->gres_bit_alloc[node_offset], i);
		if (shared_gres) { /* Allocate whole sharing gres */
			int n = gres_ns->topo_gres_cnt_avail[i];
			gres_js->gres_per_bit_alloc[node_offset][i] = n;
			gres_ns->gres_cnt_alloc += n;
			(*gres_cnt) -= n;
		} else {
			gres_ns->gres_cnt_alloc++;
			(*gres_cnt)--;
		}
	}
	FREE_NULL_BITMAP(alloc_core_bitmap);
}


static int _job_alloc(gres_state_t *gres_state_job, list_t *job_gres_list_alloc,
		      gres_state_t *gres_state_node,
		      int node_cnt, int node_index,
		      int node_offset, uint32_t job_id,
		      char *node_name, bitstr_t *core_bitmap,
		      bool new_alloc)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	char *gres_name = gres_state_job->gres_name;
	uint32_t config_flags = gres_state_job->config_flags;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;
	int j, sz1, sz2, rc = SLURM_SUCCESS;
	int64_t gres_cnt, i;
	gres_job_state_t  *gres_js_alloc;
	bitstr_t *left_over_bits = NULL;
	bool log_cnt_err = true;
	char *log_type;
	bool shared_gres = false;
	bool use_busy_dev = gres_use_busy_dev(gres_state_node, 0);
	uint64_t pre_alloc_gres_cnt;
	uint64_t *pre_alloc_type_cnt = NULL;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_cnt);
	xassert(node_offset >= 0);
	xassert(gres_js);
	xassert(gres_ns);

	if (gres_id_shared(config_flags)) {
		shared_gres = true;
	}

	if (gres_js->type_name && !gres_js->type_name[0])
		xfree(gres_js->type_name);

	xfree(gres_ns->gres_used);	/* Clear cache */

	/*
	 * Check if no nodes, then the next 2 checks were added long before job
	 * resizing was allowed. They are not errors as we need to keep the
	 * original size around for any steps that might still be out there with
	 * the larger size.  If the job was sized up the gres_job_merge()
	 * function handles the resize so we are set there.
	 */
	if (gres_js->node_cnt == 0) {
		gres_js->node_cnt = node_cnt;
		if (gres_js->gres_bit_alloc) {
			error("gres/%s: job %u node_cnt==0 and gres_bit_alloc is set",
			      gres_name, job_id);
			xfree(gres_js->gres_bit_alloc);
		}
	}
	else if (gres_js->node_cnt < node_cnt) {
		debug2("gres/%s: job %u node_cnt is now larger than it was when allocated from %u to %d",
		       gres_name, job_id, gres_js->node_cnt, node_cnt);
		if (node_offset >= gres_js->node_cnt)
			return SLURM_ERROR;
	} else if (gres_js->node_cnt > node_cnt) {
		debug2("gres/%s: job %u node_cnt is now smaller than it was when allocated %u to %d",
		       gres_name, job_id, gres_js->node_cnt, node_cnt);
	}

	if (!gres_js->gres_bit_alloc) {
		gres_js->gres_bit_alloc = xcalloc(node_cnt,
						  sizeof(bitstr_t *));
	}
	if (!gres_js->gres_cnt_node_alloc) {
		gres_js->gres_cnt_node_alloc = xcalloc(node_cnt,
						       sizeof(uint64_t));
	}

	/*
	 * select/cons_tres pre-selects the resources and we just need to update
	 * the data structures to reflect the selected GRES.
	 */
	/* Resuming job */
	if (gres_js->gres_cnt_node_alloc[node_offset]) {
		gres_cnt = gres_js->
			gres_cnt_node_alloc[node_offset];
	} else if (gres_js->gres_bit_alloc[node_offset]) {
		gres_cnt = bit_set_count(
			gres_js->gres_bit_alloc[node_offset]);
		if (gres_js->gres_per_bit_alloc &&
		    gres_js->gres_per_bit_alloc[node_offset]) {
			error("gres_per_bit_alloc and not gres_cnt_node_alloc");
		}
	} else if (gres_js->total_node_cnt) {
		/* Using pre-selected GRES */
		if (gres_js->gres_cnt_node_select &&
		    gres_js->gres_cnt_node_select[node_index]) {
			gres_cnt = gres_js->
				gres_cnt_node_select[node_index];
		/* gres_bit_select should always match gres_cnt_node_select */
		} else {
			error("gres/%s: job %u node %s no resources selected",
			      gres_name, job_id, node_name);
			return SLURM_ERROR;
		}
	} else {
		gres_cnt = gres_js->gres_per_node;
	}

	/*
	 * Check that sufficient resources exist on this node
	 */
	gres_js->gres_cnt_node_alloc[node_offset] = gres_cnt;
	i = gres_ns->gres_cnt_alloc + gres_cnt;
	if (i > gres_ns->gres_cnt_avail) {
		error("gres/%s: job %u node %s overallocated resources by %"
		      PRIu64", (%"PRIu64" > %"PRIu64")",
		      gres_name, job_id, node_name,
		      i - gres_ns->gres_cnt_avail,
		      i, gres_ns->gres_cnt_avail);
		return SLURM_ERROR;
	}

	/*
	 * Grab these here since gres_ns->[gres|type]_cnt_alloc can change
	 * later.
	 */
	pre_alloc_gres_cnt = gres_ns->gres_cnt_alloc;
	pre_alloc_type_cnt = xcalloc(gres_ns->type_cnt,
				     sizeof(*pre_alloc_type_cnt));
	memcpy(pre_alloc_type_cnt, gres_ns->type_cnt_alloc,
	       sizeof(*pre_alloc_type_cnt) * gres_ns->type_cnt);

	if (!node_offset && gres_js->gres_cnt_step_alloc) {
		uint64_t *tmp = xcalloc(gres_js->node_cnt,
					sizeof(uint64_t));
		memcpy(tmp, gres_js->gres_cnt_step_alloc,
		       sizeof(uint64_t) * MIN(node_cnt,
					      gres_js->node_cnt));
		xfree(gres_js->gres_cnt_step_alloc);
		gres_js->gres_cnt_step_alloc = tmp;
	}
	if (gres_js->gres_cnt_step_alloc == NULL) {
		gres_js->gres_cnt_step_alloc =
			xcalloc(gres_js->node_cnt, sizeof(uint64_t));
	}

	/*
	 * Select and/or allocate specific resources for this job.
	 */
	if (gres_js->gres_bit_alloc[node_offset]) {
		/*
		 * Restarted slurmctld with active job or resuming a suspended
		 * job. In any case, the resources already selected.
		 */
		if (gres_ns->gres_bit_alloc == NULL) {
			gres_ns->gres_bit_alloc =
				bit_copy(gres_js->
					 gres_bit_alloc[node_offset]);
			gres_ns->gres_cnt_alloc +=
				gres_js->gres_cnt_node_alloc[node_offset];
		} else if (gres_ns->gres_bit_alloc) {
			gres_cnt = (int64_t)MIN(
				bit_size(gres_ns->gres_bit_alloc),
				bit_size(gres_js->
					 gres_bit_alloc[node_offset]));
			for (i = 0; i < gres_cnt; i++) {
				uint64_t gres_per_bit = 1;
				if (gres_js->gres_per_bit_alloc &&
				    gres_js->gres_per_bit_alloc[node_offset] &&
				    gres_js->gres_per_bit_alloc[node_offset][i])
					gres_per_bit =
						gres_js->gres_per_bit_alloc
							[node_offset][i];
				if (bit_test(gres_js->
					     gres_bit_alloc[node_offset], i) &&
				    (shared_gres ||
				     !bit_test(gres_ns->gres_bit_alloc,
					       i))) {
					bit_set(gres_ns->
						gres_bit_alloc,i);
					gres_ns->gres_cnt_alloc +=
						gres_per_bit;
				}
			}
		}
	} else if (gres_js->total_node_cnt &&
		   gres_js->gres_bit_select &&
		   gres_js->gres_bit_select[node_index] &&
		   gres_js->gres_cnt_node_select) {
		/* Specific GRES already selected, update the node record */
		bool job_mod = false;
		sz1 = bit_size(gres_js->gres_bit_select[node_index]);
		sz2 = bit_size(gres_ns->gres_bit_alloc);
		if (sz1 > sz2) {
			error("gres/%s: job %u node %s gres bitmap size bad (%d > %d)",
			      gres_name, job_id, node_name, sz1, sz2);
			bit_realloc(gres_js->gres_bit_select[node_index], sz2);
			job_mod = true;
		} else if (sz1 < sz2) {
			error("gres/%s: job %u node %s gres bitmap size bad (%d < %d)",
			      gres_name, job_id, node_name, sz1, sz2);
			bit_realloc(gres_js->gres_bit_select[node_index], sz2);
		}

		if (!shared_gres &&
		    bit_overlap_any(gres_js->gres_bit_select[node_index],
				    gres_ns->gres_bit_alloc)) {
			error("gres/%s: job %u node %s gres bitmap overlap",
			      gres_name, job_id, node_name);
			bit_and_not(gres_js->gres_bit_select[node_index],
				    gres_ns->gres_bit_alloc);
		}
		gres_js->gres_bit_alloc[node_offset] =
			bit_copy(gres_js->gres_bit_select[node_index]);
		if (gres_js->gres_per_bit_select &&
		    gres_js->gres_per_bit_select[node_index]){
			if (!gres_js->gres_per_bit_alloc) {
				gres_js->gres_per_bit_alloc = xcalloc(
					gres_js->node_cnt, sizeof(uint64_t *));
			}
			gres_js->gres_per_bit_alloc[node_offset] = xcalloc(
				bit_size(gres_js->gres_bit_alloc[node_offset]),
				sizeof(uint64_t));
			memcpy(gres_js->gres_per_bit_alloc[node_offset],
			       gres_js->gres_per_bit_select[node_index],
			       bit_size(gres_js->gres_bit_select[node_index]) *
				       sizeof(uint64_t));
		}
		gres_js->gres_cnt_node_alloc[node_offset] =
			gres_js->gres_cnt_node_select[node_index];
		if (!gres_ns->gres_bit_alloc) {
			gres_ns->gres_bit_alloc =
				bit_copy(gres_js->
					 gres_bit_alloc[node_offset]);
		} else {
			bit_or(gres_ns->gres_bit_alloc,
			       gres_js->gres_bit_alloc[node_offset]);
		}
		if (job_mod) {
			gres_ns->gres_cnt_alloc =
				bit_set_count(gres_ns->gres_bit_alloc);
			if (shared_gres &&
			    (bit_size(gres_ns->gres_bit_alloc) !=
			     gres_ns->gres_cnt_avail))
				gres_ns->gres_cnt_alloc *=
					(gres_ns->gres_cnt_avail /
					 bit_size(gres_ns->gres_bit_alloc));
		} else {
			gres_ns->gres_cnt_alloc += gres_cnt;
		}
	} else if (gres_ns->gres_bit_alloc) {
		int64_t gres_bits = bit_size(gres_ns->gres_bit_alloc);
		if (!shared_gres && (gres_bits < gres_ns->gres_cnt_avail)) {
			error("gres/%s: node %s gres bitmap size bad (%"PRIi64" < %"PRIi64")",
			      gres_name, node_name,
			      gres_bits, gres_ns->gres_cnt_avail);
			gres_bits = gres_ns->gres_cnt_avail;
			bit_realloc(gres_ns->gres_bit_alloc, gres_bits);
		}

		gres_js->gres_bit_alloc[node_offset] =
			bit_alloc(gres_bits);

		if (shared_gres) {
			if (!gres_js->gres_per_bit_alloc) {
				gres_js->gres_per_bit_alloc = xcalloc(
					gres_js->node_cnt, sizeof(uint64_t *));
			}
			gres_js->gres_per_bit_alloc[node_offset] = xcalloc(
				bit_size(gres_js->gres_bit_alloc[node_offset]),
				sizeof(uint64_t));
		}
		/* Pass 1: Allocate GRES overlapping all allocated cores */
		_allocate_gres_bits(gres_ns, gres_js, gres_bits,
				    &gres_cnt, node_offset, shared_gres,
				    core_bitmap, true);
		/* Pass 2: Allocate GRES overlapping any allocated cores */
		_allocate_gres_bits(gres_ns, gres_js, gres_bits,
				    &gres_cnt, node_offset, shared_gres,
				    core_bitmap, false);
		if (gres_cnt) {
			verbose("gres/%s topology sub-optimal for job %u",
				gres_name, job_id);
		}
		/* Pass 3: Allocate any available GRES */
		_allocate_gres_bits(gres_ns, gres_js, gres_bits,
				    &gres_cnt, node_offset, shared_gres,
				    NULL, false);
	} else {
		gres_ns->gres_cnt_alloc += gres_cnt;
	}

	if (gres_js->gres_bit_alloc[node_offset] &&
	    gres_ns->topo_gres_bitmap &&
	    gres_ns->topo_gres_cnt_alloc) {
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			if (gres_js->type_name &&
			    (!gres_ns->topo_type_name[i] ||
			     (gres_js->type_id !=
			      gres_ns->topo_type_id[i])))
				continue;
			if (use_busy_dev &&
			    (gres_ns->topo_gres_cnt_alloc[i] == 0))
				continue;
			sz1 = bit_size(
				gres_js->gres_bit_alloc[node_offset]);
			sz2 = bit_size(gres_ns->topo_gres_bitmap[i]);

			if ((sz1 != sz2) && log_cnt_err) {
				if (shared_gres)
					log_type = "File";
				else
					log_type = "Count";
				/* Avoid abort on bit_overlap below */
				error("gres/%s %s mismatch for node %s (%d != %d)",
				      gres_name, log_type, node_name, sz1, sz2);
				log_cnt_err = false;
			}
			if (sz1 != sz2)
				continue;	/* See error above */
			gres_cnt = _cnt_topo_gres(gres_js, node_offset,
						  gres_ns->topo_gres_bitmap[i]);
			gres_ns->topo_gres_cnt_alloc[i] += gres_cnt;
			if ((gres_ns->type_cnt == 0) ||
			    (gres_ns->topo_type_name == NULL) ||
			    (gres_ns->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < gres_ns->type_cnt; j++) {
				if (!gres_ns->type_name[j] ||
				    (gres_ns->topo_type_id[i] !=
				     gres_ns->type_id[j]))
					continue;
				gres_ns->type_cnt_alloc[j] += gres_cnt;
				break;
			}
		}
	} else if (gres_js->gres_bit_alloc[node_offset]) {
		int len;	/* length of the gres bitmap on this node */
		len = bit_size(gres_js->gres_bit_alloc[node_offset]);
		if (!gres_ns->topo_gres_cnt_alloc) {
			gres_ns->topo_gres_cnt_alloc =
				xcalloc(len, sizeof(uint64_t));
		} else {
			len = MIN(len, gres_ns->gres_cnt_config);
		}

		for (i = 0; i < len; i++) {
			gres_cnt = 0;
			if (!bit_test(gres_js->
				      gres_bit_alloc[node_offset], i))
				continue;
			uint64_t gres_per_bit = 1;
			if (gres_js->gres_per_bit_alloc &&
			    gres_js->gres_per_bit_alloc[node_offset] &&
			    gres_js->gres_per_bit_alloc[node_offset][i])
				gres_per_bit =
					gres_js->gres_per_bit_alloc
						[node_offset][i];
			/*
			 * NOTE: Immediately after slurmctld restart and before
			 * the node's registration, the GRES type and topology
			 * information will not be available and we will be
			 * unable to update topo_gres_cnt_alloc or
			 * type_cnt_alloc. This results in some incorrect
			 * internal bookkeeping, but does not cause failures
			 * in terms of allocating GRES to jobs.
			 */
			for (j = 0; j < gres_ns->topo_cnt; j++) {
				if (use_busy_dev &&
				    !gres_ns->topo_gres_cnt_alloc[j])
					continue;
				if (gres_ns->topo_gres_bitmap &&
				    gres_ns->topo_gres_bitmap[j] &&
				    bit_test(gres_ns->topo_gres_bitmap[j],
					     i)) {
					gres_ns->topo_gres_cnt_alloc[i] +=
						gres_per_bit;
					gres_cnt += gres_per_bit;
				}
			}
			if ((gres_ns->type_cnt == 0) ||
			    (gres_ns->topo_type_name == NULL) ||
			    (gres_ns->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < gres_ns->type_cnt; j++) {
				if (!gres_ns->type_name[j] ||
				    (gres_ns->topo_type_id[i] !=
				     gres_ns->type_id[j]))
					continue;
				gres_ns->type_cnt_alloc[j] += gres_cnt;
				break;
			}
		}
		if (gres_js->type_name && gres_js->type_name[0]) {
			/*
			 * We may not know how many GRES of this type will be
			 * available on this node, but need to track how many
			 * are allocated to this job from here to avoid
			 * underflows when this job is deallocated
			 */
			gres_add_type(gres_js->type_name, gres_ns,
				      0);
			for (j = 0; j < gres_ns->type_cnt; j++) {
				if (gres_js->type_id !=
				    gres_ns->type_id[j])
					continue;
				gres_ns->type_cnt_alloc[j] +=
					gres_js->gres_per_node;
				break;
			}
		}
	} else {
		gres_cnt = gres_js->gres_per_node;
		for (j = 0; j < gres_ns->type_cnt; j++) {
			int64_t k;
			if (gres_js->type_name &&
			    (gres_js->type_id !=
			     gres_ns->type_id[j]))
				continue;
			k = gres_ns->type_cnt_avail[j] -
				gres_ns->type_cnt_alloc[j];
			k = MIN(gres_cnt, k);
			gres_ns->type_cnt_alloc[j] += k;
			gres_cnt -= k;
			if (gres_cnt == 0)
				break;
		}
	}

	/* If we are already allocated (state restore | reconfig) end now. */
	if (!new_alloc) {
		if (gres_ns->no_consume) {
			gres_ns->gres_cnt_alloc = pre_alloc_gres_cnt;
			for (j = 0; j < gres_ns->type_cnt; j++)
				gres_ns->type_cnt_alloc[j] =
					pre_alloc_type_cnt[j];
		}

		goto cleanup;
	}

	/*
	 * Here we fill job_gres_list_alloc with
	 * one entry for each type of gres separately
	 */
	if (gres_js->gres_bit_alloc && gres_js->gres_bit_alloc[node_offset])
		left_over_bits = bit_copy(gres_js->gres_bit_alloc[node_offset]);
	for (j = 0; j < gres_ns->type_cnt; j++) {
		if (gres_js->type_id &&
		    gres_js->type_id != gres_ns->type_id[j])
			continue;
		gres_js_alloc = _get_job_alloc_gres_ptr(
			job_gres_list_alloc, gres_state_job,
			gres_ns->type_id[j], gres_ns->type_name[j], node_cnt);
		gres_cnt = gres_ns->type_cnt_alloc[j] -
			pre_alloc_type_cnt[j];
		if (gres_ns->no_consume) {
			gres_ns->type_cnt_alloc[j] =
				pre_alloc_type_cnt[j];
			gres_ns->gres_cnt_alloc = pre_alloc_gres_cnt;
			gres_js_alloc->gres_cnt_node_alloc[node_offset] =
				NO_CONSUME_VAL64;
			gres_js_alloc->total_gres = NO_CONSUME_VAL64;
		} else {
			gres_js_alloc->gres_cnt_node_alloc[node_offset] =
				gres_cnt;
			gres_js_alloc->total_gres += gres_cnt;
		}

		if (gres_js->gres_bit_alloc &&
		    gres_js->gres_bit_alloc[node_offset]) {
			if (shared_gres)
				gres_cnt = _get_sharing_cnt_from_shared_cnt(
					gres_js, left_over_bits, node_offset,
					gres_cnt);
			gres_js_alloc->gres_bit_alloc[node_offset] =
				bit_pick_cnt(left_over_bits, gres_cnt);
			bit_and_not(left_over_bits,
				    gres_js_alloc->gres_bit_alloc[node_offset]);
		}

		if (gres_js->gres_per_bit_alloc &&
		    gres_js->gres_per_bit_alloc[node_offset]) {
			_copy_matching_gres_per_bit(gres_js, gres_js_alloc,
						    node_offset);
		}
	}
	FREE_NULL_BITMAP(left_over_bits);
	/* Also track non typed node gres */
	if (gres_ns->type_cnt == 0) {
		gres_js_alloc = _get_job_alloc_gres_ptr(
			job_gres_list_alloc, gres_state_job,
			0, NULL, node_cnt);
		gres_cnt = gres_ns->gres_cnt_alloc - pre_alloc_gres_cnt;
		if (gres_ns->no_consume) {
			gres_ns->gres_cnt_alloc = pre_alloc_gres_cnt;
			gres_js_alloc->gres_cnt_node_alloc[node_offset] =
				NO_CONSUME_VAL64;
			gres_js_alloc->total_gres = NO_CONSUME_VAL64;
		} else {
			gres_js_alloc->gres_cnt_node_alloc[node_offset] =
				gres_cnt;
			gres_js_alloc->total_gres += gres_cnt;
		}

		if (gres_js->gres_bit_alloc &&
		    gres_js->gres_bit_alloc[node_offset])
			gres_js_alloc->gres_bit_alloc[node_offset] = bit_copy(
				gres_js->gres_bit_alloc[node_offset]);

		if (gres_js->gres_per_bit_alloc &&
		    gres_js->gres_per_bit_alloc[node_offset]) {
			_copy_matching_gres_per_bit(gres_js, gres_js_alloc,
						    node_offset);
		}
	}

cleanup:

	xfree(pre_alloc_type_cnt);

	return rc;
}

static int _job_alloc_whole_node_internal(
	gres_key_t *job_search_key, gres_state_t *gres_state_node,
	list_t *job_gres_list, list_t **job_gres_list_alloc, int node_cnt,
	int node_index, int node_offset, int type_index, uint32_t job_id,
	char *node_name, bitstr_t *core_bitmap, bool new_alloc)
{
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	gres_node_state_t *gres_ns = gres_state_node->gres_data;

	if (*job_gres_list_alloc == NULL) {
		*job_gres_list_alloc = list_create(gres_job_list_delete);
	}

	if (!(gres_state_job = list_find_first(job_gres_list,
					       gres_find_job_by_key,
					       job_search_key))) {
		error("%s: This should never happen, we couldn't find the gres %u:%u",
		      __func__,
		      job_search_key->plugin_id,
		      job_search_key->type_id);
		return SLURM_ERROR;
	}

	gres_js = (gres_job_state_t *)gres_state_job->gres_data;

	/*
	 * As the amount of gres on each node could
	 * differ. We need to set the gres_per_node
	 * correctly here to avoid heterogeneous node
	 * issues.
	 */
	if (type_index != -1)
		gres_js->gres_per_node =
			gres_ns->type_cnt_avail[type_index];
	else
		gres_js->gres_per_node = gres_ns->gres_cnt_avail;

	return _job_alloc(gres_state_job, *job_gres_list_alloc, gres_state_node,
			  node_cnt, node_index, node_offset,
			  job_id, node_name, core_bitmap, new_alloc);
}

static void _job_select_whole_node_internal(
	gres_key_t *job_search_key, gres_node_state_t *gres_ns,
	int type_inx, char *gres_name, list_t *job_gres_list)
{
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;

	if (!(gres_state_job = list_find_first(job_gres_list,
					       gres_find_job_by_key,
					       job_search_key))) {
		gres_js = xmalloc(sizeof(gres_job_state_t));
		gres_state_job = gres_create_state(job_search_key,
						   GRES_STATE_SRC_KEY_PTR,
						   GRES_STATE_TYPE_JOB,
						   gres_js);
		gres_state_job->gres_name = xstrdup(gres_name);
		if (type_inx != -1)
			gres_js->type_name =
				xstrdup(gres_ns->type_name[type_inx]);
		gres_js->type_id = job_search_key->type_id;

		list_append(job_gres_list, gres_state_job);
	} else
		gres_js = gres_state_job->gres_data;

	/*
	 * Add the total_gres here but no count, that will be done after
	 * allocation.
	 */
	if (gres_ns->no_consume) {
		gres_js->total_gres = NO_CONSUME_VAL64;
	} else if (type_inx != -1)
		gres_js->total_gres +=
			gres_ns->type_cnt_avail[type_inx];
	else
		gres_js->total_gres += gres_ns->gres_cnt_avail;

}

static void _handle_explicit_alloc(void *x, void *arg)
{
	gres_state_t *gres_state_job = x;
	foreach_explicit_alloc_t *explicit_alloc = arg;
	int rc;

	if (!(gres_state_job->config_flags & GRES_CONF_EXPLICIT) ||
	    !gres_find_id(x, &explicit_alloc->gres_state_node->plugin_id))
		return;

	if (!*explicit_alloc->job_gres_list)
		*explicit_alloc->job_gres_list =
			list_create(gres_job_list_delete);

	rc = _job_alloc(gres_state_job,
			*explicit_alloc->job_gres_list,
			explicit_alloc->gres_state_node,
			explicit_alloc->node_cnt,
			explicit_alloc->node_index,
			explicit_alloc->node_offset,
			explicit_alloc->job_id,
			explicit_alloc->node_name,
			explicit_alloc->core_bitmap,
			explicit_alloc->new_alloc);

	if (rc != SLURM_SUCCESS)
		explicit_alloc->rc = rc;
}

static void _job_alloc_explicit(
	list_t *req_gres_list, foreach_explicit_alloc_t *explicit_alloc)
{
	if (!req_gres_list)
		return;

	(void) list_for_each(req_gres_list,
			     (ListForF) _handle_explicit_alloc,
			     explicit_alloc);
}

static int _foreach_clear_job_gres(void *x, void *arg)
{
	gres_job_clear_alloc(((gres_state_t *)x)->gres_data);

	return 0;
}

/*
 * Fill in job_gres_list with the total amount of GRES on a node.
 * OUT job_gres_list - This list will be destroyed and remade with all GRES on
 *                     node.
 * IN node_gres_list - node's gres_list built by
 *		       gres_node_config_validate()
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_stepmgr_job_select_whole_node(
	list_t **job_gres_list, list_t *node_gres_list,
	uint32_t job_id, char *node_name)
{
	list_itr_t *node_gres_iter;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	if (!*job_gres_list)
		*job_gres_list = list_create(gres_job_list_delete);

	node_gres_iter = list_iterator_create(node_gres_list);
	while ((gres_state_node = list_next(node_gres_iter))) {
		gres_key_t job_search_key;

		gres_ns = (gres_node_state_t *) gres_state_node->gres_data;
		/*
		 * Don't check for no_consume here, we need them added here and
		 * will filter them out in gres_job_alloc_whole_node()
		 */
		if (!gres_ns->gres_cnt_config)
			continue;

		if (gres_state_node->config_flags & GRES_CONF_EXPLICIT)
			continue;

		/* Select shared GRES if requested */
		if (gres_id_shared(gres_state_node->config_flags)) {
			/*
			 * If we find it, delete it and add back to the list as
			 * a whole node selection.
			 * This is because we didn't delete it in
			 * _handle_explicit_req() in node_scheduler.c
			 */
			if (!list_delete_first(*job_gres_list, gres_find_id,
					       &gres_state_node->plugin_id))
				continue;
		}
		/* If we select the shared gres don't select sharing gres */
		if (gres_ns->alt_gres &&
		    gres_id_sharing(gres_state_node->plugin_id)) {
			if (list_find_first(*job_gres_list, gres_find_id,
					    &(gres_ns->alt_gres->plugin_id)))
				continue;
		}

		job_search_key.config_flags = gres_state_node->config_flags;
		job_search_key.plugin_id = gres_state_node->plugin_id;

		/* Add the non-typed one first/always */
		job_search_key.type_id = 0;
		_job_select_whole_node_internal(
			&job_search_key, gres_ns,
			-1, gres_state_node->gres_name, *job_gres_list);

		/* Then add the typed ones if any */
		for (int j = 0; j < gres_ns->type_cnt; j++) {
			job_search_key.type_id = gres_build_id(
				gres_ns->type_name[j]);
			_job_select_whole_node_internal(
				&job_search_key, gres_ns,
				j, gres_state_node->gres_name,
				*job_gres_list);
		}
	}
	list_iterator_destroy(node_gres_iter);

	return SLURM_SUCCESS;
}

/*
 * On a slurmctld restart the type counts are not set on a node, this function
 * fixes this.  At this point it is really just cosmetic though as the parent
 * GRES is already correct on the gres_node_state_t only the types are wrong if
 * only generic GRES was requested by the job.
 */
static int _set_node_type_cnt(gres_state_t *gres_state_job,
			      list_t *node_gres_list)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;

	if (!gres_js->total_gres || !gres_js->type_id)
		return 0;

	if (!(gres_state_node = list_find_first(node_gres_list, gres_find_id,
						&gres_state_job->plugin_id)))
		return 0;

	gres_ns = gres_state_node->gres_data;

	for (int j = 0; j < gres_ns->type_cnt; j++) {
		/*
		 * Already set (typed GRES was requested) ||
		 * Not the right type
		 */
		if (gres_ns->type_cnt_alloc[j] ||
		    (gres_ns->type_id[j] != gres_js->type_id) ||
		    (gres_js->total_gres == NO_CONSUME_VAL64))
			continue;
		gres_ns->type_cnt_alloc[j] = gres_js->total_gres;
		break;
	}
	return 0;
}

/*
 * Select and allocate GRES to a job and update node and job GRES information
 * IN job_gres_list - job's gres_list built by gres_job_state_validate()
 * OUT job_gres_list_alloc - job's list of allocated gres
 * IN node_gres_list - node's gres_list built by
 *		       gres_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_index  - zero-origin global node index
 * IN node_offset - zero-origin index in job allocation to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * IN new_alloc   - If this is a new allocation or not.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_stepmgr_job_alloc(
	list_t *job_gres_list, list_t **job_gres_list_alloc,
	list_t *node_gres_list, int node_cnt,
	int node_index, int node_offset,
	uint32_t job_id, char *node_name,
	bitstr_t *core_bitmap, bool new_alloc)
{
	int rc = SLURM_ERROR, rc2;
	list_itr_t *job_gres_iter;
	gres_state_t *gres_state_job, *gres_state_node;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}
	if (*job_gres_list_alloc == NULL) {
		*job_gres_list_alloc = list_create(gres_job_list_delete);
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		gres_state_node = list_find_first(node_gres_list, gres_find_id,
						  &gres_state_job->plugin_id);
		if (gres_state_node == NULL) {
			error("%s: job %u allocated gres/%s on node %s lacking that gres",
			      __func__, job_id, gres_state_job->gres_name,
			      node_name);
			continue;
		}

		rc2 = _job_alloc(gres_state_job, *job_gres_list_alloc,
				 gres_state_node, node_cnt,
				 node_index,
				 node_offset, job_id, node_name, core_bitmap,
				 new_alloc);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(job_gres_iter);

	/*
	 * On a slurmctld restart the node doesn't know anything about types so
	 * they are not setup, in this situation we can go set them here.  We
	 * can't do it in the req loop above since if the request has typed GRES
	 * in there we could potentially get duplicate counts.
	 */
	if (!new_alloc)
		(void) list_for_each(*job_gres_list_alloc,
				     (ListForF) _set_node_type_cnt,
				     node_gres_list);

	return rc;
}

/*
 * Select and allocate all GRES on a node to a job and update node and job GRES
 * information
 * IN job_gres_list - job's gres_list built by gres_job_whole_node().
 * OUT job_gres_list_alloc - job's list of allocated gres
 * IN node_gres_list - node's gres_list built by
 *		       gres_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_index  - zero-origin global node index
 * IN node_offset - zero-origin index in job allocation to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * IN new_alloc   - If this is a new allocation or not.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_stepmgr_job_alloc_whole_node(
	list_t *job_gres_list, list_t **job_gres_list_alloc, list_t *node_gres_list,
	int node_cnt, int node_index, int node_offset,
	uint32_t job_id, char *node_name,
	bitstr_t *core_bitmap, bool new_alloc)
{
	int rc = SLURM_ERROR, rc2;
	list_itr_t *node_gres_iter;
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	node_gres_iter = list_iterator_create(node_gres_list);
	while ((gres_state_node = list_next(node_gres_iter))) {
		gres_key_t job_search_key;
		gres_ns = (gres_node_state_t *) gres_state_node->gres_data;

		if (!gres_ns->gres_cnt_config)
			continue;

		/* Allocate shared GRES if requested */
		if (gres_id_shared(gres_state_node->config_flags)) {
			if (!list_find_first(job_gres_list, gres_find_id,
					       &gres_state_node->plugin_id))
				continue;
		}
		/* If we allocate the shared gres don't allocate sharing gres */
		if (gres_ns->alt_gres &&
		    gres_id_sharing(gres_state_node->plugin_id)) {
			if (list_find_first(job_gres_list, gres_find_id,
					    &(gres_ns->alt_gres->plugin_id)))
				continue;
		}

		if (gres_state_node->config_flags & GRES_CONF_EXPLICIT) {
			if (job_gres_list) {
				foreach_explicit_alloc_t explicit_alloc = {
					.core_bitmap = core_bitmap,
					.gres_state_node = gres_state_node,
					.job_id = job_id,
					.job_gres_list = job_gres_list_alloc,
					.new_alloc = new_alloc,
					.node_cnt = node_cnt,
					.node_index = node_index,
					.node_offset = node_offset,
					.node_name = node_name,
					.rc = rc,

				};
				_job_alloc_explicit(job_gres_list,
						    &explicit_alloc);

			}
			continue;
		}

		job_search_key.config_flags = gres_state_node->config_flags;
		job_search_key.plugin_id = gres_state_node->plugin_id;

		/*
		 * This check is needed and different from the one in
		 * gres_stepmgr_job_select_whole_node(). _job_alloc() handles
		 * all the heavy lifting later on to make this all correct.
		 */
		if (!gres_ns->type_cnt) {
			job_search_key.type_id = 0;
			rc2 = _job_alloc_whole_node_internal(
				&job_search_key, gres_state_node,
				job_gres_list, job_gres_list_alloc,
				node_cnt, node_index,
				node_offset, -1, job_id, node_name,
				core_bitmap, new_alloc);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
		} else {
			for (int j = 0; j < gres_ns->type_cnt; j++) {
				job_search_key.type_id = gres_build_id(
					gres_ns->type_name[j]);
				rc2 = _job_alloc_whole_node_internal(
					&job_search_key, gres_state_node,
					job_gres_list, job_gres_list_alloc,
					node_cnt, node_index,
					node_offset, j, job_id, node_name,
					core_bitmap, new_alloc);
				if (rc2 != SLURM_SUCCESS)
					rc = rc2;
			}
		}
	}
	list_iterator_destroy(node_gres_iter);

	return rc;
}

static int _job_dealloc(gres_state_t *gres_state_job,
			gres_node_state_t *gres_ns,
			int node_offset, uint32_t job_id,
			char *node_name, bool old_job, bool resize)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	char *gres_name = gres_state_job->gres_name;
	uint32_t config_flags = gres_state_job->config_flags;
	int i, j, len, sz1, sz2, last_node;
	uint64_t gres_cnt = 0, k;
	bool shared_gres = false;

	/*
	 * Validate data structures. Either gres_js->node_cnt and
	 * gres_js->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_offset >= 0);
	xassert(gres_js);
	xassert(gres_ns);

	if (gres_ns->no_consume)
		return SLURM_SUCCESS;

	if (gres_js->node_cnt <= node_offset) {
		error("gres/%s: job %u dealloc of node %s bad node_offset %d "
		      "count is %u", gres_name, job_id, node_name, node_offset,
		      gres_js->node_cnt);
		return SLURM_ERROR;
	}

	if (gres_id_shared(config_flags)) {
		shared_gres = true;
		if (!(gres_js->gres_per_bit_alloc &&
		      gres_js->gres_per_bit_alloc[node_offset]) &&
		    (gres_js->gres_bit_alloc &&
		     gres_js->gres_bit_alloc[node_offset])) {
			error("gres/%s: job %u dealloc node %s where gres shared but there is no gres_per_bit_alloc",
			      gres_name, job_id, node_name);
			return SLURM_ERROR;
		}
	}

	xfree(gres_ns->gres_used);	/* Clear cache */

	/* Clear the node's regular GRES bitmaps based on what the job has */
	if (gres_ns->gres_bit_alloc && gres_js->gres_bit_alloc &&
	    gres_js->gres_bit_alloc[node_offset] &&
	    gres_js->gres_cnt_node_alloc &&
	    gres_js->gres_cnt_node_alloc[node_offset]) {
		len = bit_size(gres_js->gres_bit_alloc[node_offset]);
		i   = bit_size(gres_ns->gres_bit_alloc);
		if (i != len) {
			error("gres/%s: job %u and node %s bitmap sizes differ "
			      "(%d != %d)", gres_name, job_id, node_name, len,
			      i);
			len = MIN(len, i);
			/* proceed with request, make best effort */
		}
		if (gres_ns->gres_cnt_alloc >=
		    gres_js->gres_cnt_node_alloc[node_offset]) {
			gres_ns->gres_cnt_alloc -=
				gres_js->gres_cnt_node_alloc[node_offset];
		} else {
			error("gres/%s: job %u dealloc node %s GRES count underflow (%"PRIu64" < %"PRIu64")",
			      gres_name, job_id, node_name,
			      gres_ns->gres_cnt_alloc,
			      gres_js->gres_cnt_node_alloc[node_offset]);
			gres_ns->gres_cnt_alloc = 0;
		}
		if (!shared_gres) { /* Clear shared later based on topo info */
			for (i = 0; i < len; i++) {
				if (!bit_test(
					    gres_js->gres_bit_alloc[node_offset],
					    i)) {
					continue;
				}
				bit_clear(gres_ns->gres_bit_alloc, i);
			}
		}
	} else if (gres_js->gres_cnt_node_alloc) {
		gres_cnt = gres_js->gres_cnt_node_alloc[node_offset];
	} else {
		error("gres/%s: job %u node %s no gres allocation recorded.",
		      gres_name, job_id, node_name);
	}
	if (gres_cnt && (gres_ns->gres_cnt_alloc >= gres_cnt))
		gres_ns->gres_cnt_alloc -= gres_cnt;
	else if (gres_cnt) {
		error("gres/%s: job %u node %s GRES count underflow (%"PRIu64" < %"PRIu64")",
		      gres_name, job_id, node_name,
		      gres_ns->gres_cnt_alloc, gres_cnt);
		gres_ns->gres_cnt_alloc = 0;
	}

	/* Clear the node's topo GRES bitmaps based on what the job has */
	if (gres_js->gres_bit_alloc &&
	    gres_js->gres_bit_alloc[node_offset] &&
	    gres_ns->topo_gres_bitmap &&
	    gres_ns->topo_gres_cnt_alloc) {
		for (i = 0; i < gres_ns->topo_cnt; i++) {
			sz1 = bit_size(
				gres_js->gres_bit_alloc[node_offset]);
			sz2 = bit_size(gres_ns->topo_gres_bitmap[i]);
			if (sz1 != sz2)
				continue;
			gres_cnt = _cnt_topo_gres(gres_js, node_offset,
						  gres_ns->topo_gres_bitmap[i]);
			if (gres_ns->topo_gres_cnt_alloc[i] >= gres_cnt) {
				gres_ns->topo_gres_cnt_alloc[i] -=
					gres_cnt;
			} else if (old_job) {
				gres_ns->topo_gres_cnt_alloc[i] = 0;
			} else {
				error("gres/%s: job %u dealloc node %s topo gres count underflow "
				      "(%"PRIu64" %"PRIu64")",
				      gres_name, job_id, node_name,
				      gres_ns->topo_gres_cnt_alloc[i],
				      gres_cnt);
				gres_ns->topo_gres_cnt_alloc[i] = 0;
			}
			if (shared_gres && !gres_ns->topo_gres_cnt_alloc[i])
				bit_clear(gres_ns->gres_bit_alloc, i);
			if ((gres_ns->type_cnt == 0) ||
			    (gres_ns->topo_type_name == NULL) ||
			    (gres_ns->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < gres_ns->type_cnt; j++) {
				if (!gres_ns->type_name[j] ||
				    (gres_ns->topo_type_id[i] !=
				     gres_ns->type_id[j]))
					continue;
				if (gres_ns->type_cnt_alloc[j] >=
				    gres_cnt) {
					gres_ns->type_cnt_alloc[j] -=
						gres_cnt;
				} else if (old_job) {
					gres_ns->type_cnt_alloc[j] = 0;
				} else {
					error("gres/%s: job %u dealloc node %s type %s gres count underflow "
					      "(%"PRIu64" %"PRIu64")",
					      gres_name, job_id, node_name,
					      gres_ns->type_name[j],
					      gres_ns->type_cnt_alloc[j],
					      gres_cnt);
					gres_ns->type_cnt_alloc[j] = 0;
				}
			}
		}
	} else if (gres_js->gres_bit_alloc &&
		   gres_js->gres_bit_alloc[node_offset] &&
		   gres_ns->topo_gres_cnt_alloc) {
		/* Avoid crash if configuration inconsistent */
		len = MIN(gres_ns->gres_cnt_config,
			  bit_size(gres_js->
				   gres_bit_alloc[node_offset]));
		for (i = 0; i < len; i++) {
			uint64_t gres_per_bit;
			if (!bit_test(gres_js->
				      gres_bit_alloc[node_offset], i) ||
			    !gres_ns->topo_gres_cnt_alloc[i])
				continue;
			gres_per_bit = shared_gres ?
				gres_js->gres_per_bit_alloc[node_offset][i] : 1;
			if (gres_ns->topo_gres_cnt_alloc[i] >=
			    gres_per_bit) {
				gres_ns->topo_gres_cnt_alloc[i] -=
					gres_per_bit;
			} else {
				error("gres/%s: job %u dealloc node %s "
				      "topo_gres_cnt_alloc[%d] count underflow "
				      "(%"PRIu64" %"PRIu64")",
				      gres_name, job_id, node_name, i,
				      gres_ns->topo_gres_cnt_alloc[i],
				      gres_per_bit);
				gres_ns->topo_gres_cnt_alloc[i] = 0;
			}
			if (shared_gres && !gres_ns->topo_gres_cnt_alloc[i])
				bit_clear(gres_ns->gres_bit_alloc, i);
			if ((gres_ns->type_cnt == 0) ||
			    (gres_ns->topo_type_name == NULL) ||
			    (gres_ns->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < gres_ns->type_cnt; j++) {
				if (!gres_ns->type_name[j] ||
				    (gres_ns->topo_type_id[i] !=
				     gres_ns->type_id[j]))
					continue;
				if (gres_ns->type_cnt_alloc[j] >=
				    gres_per_bit) {
					gres_ns->type_cnt_alloc[j] -=
						gres_per_bit;
				} else {
					error("gres/%s: job %u dealloc node %s "
					      "type %s type_cnt_alloc count underflow "
					      "(%"PRIu64" %"PRIu64")",
					      gres_name, job_id, node_name,
					      gres_ns->type_name[j],
					      gres_ns->type_cnt_alloc[j],
					      gres_per_bit);
					gres_ns->type_cnt_alloc[j] = 0;
				}
 			}
		}
	} else if (gres_js->type_name) {
		for (j = 0; j < gres_ns->type_cnt; j++) {
			if (gres_js->type_id !=
			    gres_ns->type_id[j])
				continue;
			k = MIN(gres_cnt, gres_ns->type_cnt_alloc[j]);
			gres_ns->type_cnt_alloc[j] -= k;
			gres_cnt -= k;
			if (gres_cnt == 0)
				break;
		}
 	}

	if (!resize)
		return SLURM_SUCCESS;

	xassert(gres_js->node_cnt >= 1);

	/*
	 * If resizing, alter the job's GRES bitmaps. Normally, a job's GRES
	 * bitmaps will get automatically freed when the job is destroyed.
	 * However, a job isn't destroyed when it is resized. So we need to
	 * remove this node's GRES from the job's GRES bitmaps.
	 */
	last_node = gres_js->node_cnt - 1;
	if (gres_js->gres_cnt_node_alloc) {
		/*
		 * This GRES is no longer part of the job, remove it from the
		 * alloc list.
		 */
		if (gres_js->gres_cnt_node_alloc[node_offset] >=
		    gres_js->total_gres)
			return ESLURM_UNSUPPORTED_GRES;
		gres_js->total_gres -=
			gres_js->gres_cnt_node_alloc[node_offset];
		/* Shift job GRES counts down, if necessary */
		for (int i = node_offset + 1; i < gres_js->node_cnt; i++) {
			gres_js->gres_cnt_node_alloc[i - 1] =
				gres_js->gres_cnt_node_alloc[i];
		}
		/* Zero this out since we are reducing the node count */
		gres_js->gres_cnt_node_alloc[last_node] = 0;
	}
	/* Downsize job GRES for this node */
	if (gres_js->gres_bit_alloc) {
		/* Free the job's GRES bitmap */
		FREE_NULL_BITMAP(gres_js->gres_bit_alloc[node_offset]);

		/* Shift job GRES bitmaps down, if necessary */
		for (int i = node_offset + 1; i < gres_js->node_cnt; i++) {
			gres_js->gres_bit_alloc[i - 1] =
				gres_js->gres_bit_alloc[i];
		}
		/* NULL the last node since we are reducing the node count. */
		gres_js->gres_bit_alloc[last_node] = NULL;
	}

	/* Downsize job step GRES for this node */
	if (gres_js->gres_bit_step_alloc) {
		/* Free the step's GRES bitmap */
		FREE_NULL_BITMAP(gres_js->gres_bit_step_alloc[node_offset]);

		/* Shift step GRES bitmaps down, if necessary */
		for (int i = node_offset + 1; i < gres_js->node_cnt; i++) {
			gres_js->gres_bit_step_alloc[i - 1] =
				gres_js->gres_bit_step_alloc[i];
		}
		/* NULL the last node since we are reducing the node count. */
		gres_js->gres_bit_step_alloc[last_node] = NULL;
	}

	if (gres_js->gres_cnt_step_alloc) {
		/* Shift step GRES counts down, if necessary */
		for (int i = node_offset + 1; i < gres_js->node_cnt; i++) {
			gres_js->gres_cnt_step_alloc[i - 1] =
				gres_js->gres_cnt_step_alloc[i];
		}
		/* Zero this out since we are reducing the node count */
		gres_js->gres_cnt_step_alloc[last_node] = 0;
	}

	/* Finally, reduce the node count, since this node is deallocated */
	gres_js->node_cnt--;

	return SLURM_SUCCESS;
}

/*
 * Deallocate resource from a job and update node and job gres information
 * IN job_gres_list - job's allocated gres list
 * IN node_gres_list - node's gres_list built by
 *		gres_node_config_validate()
 * IN node_offset - zero-origin index to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN old_job     - true if job started before last slurmctld reboot.
 *		    Immediately after slurmctld restart and before the node's
 *		    registration, the GRES type and topology. This results in
 *		    some incorrect internal bookkeeping, but does not cause
 *		    failures in terms of allocating GRES to jobs.
 * IN: resize     - True if dealloc is due to a node being removed via a job
 * 		    resize; false if dealloc is due to a job test or a real job
 * 		    that is terminating.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_stepmgr_job_dealloc(
	list_t *job_gres_list, list_t *node_gres_list,
	int node_offset, uint32_t job_id,
	char *node_name, bool old_job, bool resize)
{
	int rc = SLURM_SUCCESS, rc2;
	list_itr_t *job_gres_iter;
	gres_state_t *gres_state_job, *gres_state_node;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(job_gres_iter))) {
		gres_state_node = list_find_first(node_gres_list, gres_find_id,
						  &gres_state_job->plugin_id);

		if (gres_state_node == NULL) {
			error("%s: node %s lacks gres/%s for job %u", __func__,
			      node_name, gres_state_job->gres_name, job_id);
			continue;
		}

		rc2 = _job_dealloc(gres_state_job,
				   gres_state_node->gres_data, node_offset,
				   job_id, node_name, old_job, resize);
		if (rc2 == ESLURM_UNSUPPORTED_GRES) {
			list_delete_item(job_gres_iter);
		} else if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(job_gres_iter);

	return rc;
}

/*
 * Merge one job's gres allocation into another job's gres allocation.
 * IN from_job_gres_list - List of gres records for the job being merged
 *			   into another job
 * IN from_job_node_bitmap - bitmap of nodes for the job being merged into
 *			     another job
 * IN/OUT to_job_gres_list - List of gres records for the job being merged
 *			     into job
 * IN to_job_node_bitmap - bitmap of nodes for the job being merged into
 */
extern void gres_stepmgr_job_merge(
	list_t *from_job_gres_list,
	bitstr_t *from_job_node_bitmap,
	list_t *to_job_gres_list,
	bitstr_t *to_job_node_bitmap)
{
	static int select_hetero = -1;
	list_itr_t *gres_iter;
	gres_state_t *gres_state_job, *gres_state_job2;
	gres_job_state_t *gres_js, *gres_js2;
	int new_node_cnt;
	int i_first, i_last, i;
	int from_inx, to_inx, new_inx;
	bitstr_t **new_gres_bit_alloc, **new_gres_bit_step_alloc;
	uint64_t *new_gres_cnt_step_alloc, *new_gres_cnt_node_alloc;
	bool free_to_job_gres_list = false;

	if (select_hetero == -1) {
		/*
		 * Determine if the select plugin supports heterogeneous
		 * GRES allocations (count differ by node): 1=yes, 0=no
		 */
		char *select_type = slurm_get_select_type();
		if (xstrstr(select_type, "cons_tres"))
			select_hetero = 1;
		else
			select_hetero = 0;
		xfree(select_type);
	}

	new_node_cnt = bit_set_count(from_job_node_bitmap) +
		bit_set_count(to_job_node_bitmap) -
		bit_overlap(from_job_node_bitmap, to_job_node_bitmap);
	i_first = MIN(bit_ffs(from_job_node_bitmap),
		      bit_ffs(to_job_node_bitmap));
	i_first = MAX(i_first, 0);
	i_last  = MAX(bit_fls(from_job_node_bitmap),
		      bit_fls(to_job_node_bitmap));
	if (i_last == -1) {
		error("%s: node_bitmaps are empty", __func__);
		return;
	}

	/* Step one - Expand the gres data structures in "to" job */
	if (!to_job_gres_list)
		goto step2;
	gres_iter = list_iterator_create(to_job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(gres_iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		new_gres_bit_alloc = xcalloc(new_node_cnt, sizeof(bitstr_t *));
		new_gres_cnt_node_alloc = xcalloc(new_node_cnt,
						  sizeof(uint64_t));
		new_gres_bit_step_alloc = xcalloc(new_node_cnt,
						  sizeof(bitstr_t *));
		new_gres_cnt_step_alloc = xcalloc(new_node_cnt,
						  sizeof(uint64_t));

		from_inx = to_inx = new_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			bool from_match = false, to_match = false;
			if (bit_test(to_job_node_bitmap, i)) {
				to_match = true;
				to_inx++;
			}
			if (bit_test(from_job_node_bitmap, i)) {
				from_match = true;
				from_inx++;
			}
			if (from_match || to_match)
				new_inx++;
			if (to_match) {
				if (gres_js->gres_bit_alloc) {
					new_gres_bit_alloc[new_inx] =
						gres_js->
						gres_bit_alloc[to_inx];
				}
				if (gres_js->gres_cnt_node_alloc) {
					new_gres_cnt_node_alloc[new_inx] =
						gres_js->
						gres_cnt_node_alloc[to_inx];
				}
				if (gres_js->gres_bit_step_alloc) {
					new_gres_bit_step_alloc[new_inx] =
						gres_js->
						gres_bit_step_alloc[to_inx];
				}
				if (gres_js->gres_cnt_step_alloc) {
					new_gres_cnt_step_alloc[new_inx] =
						gres_js->
						gres_cnt_step_alloc[to_inx];
				}
			}
		}
		gres_js->node_cnt = new_node_cnt;
		xfree(gres_js->gres_bit_alloc);
		gres_js->gres_bit_alloc = new_gres_bit_alloc;
		xfree(gres_js->gres_cnt_node_alloc);
		gres_js->gres_cnt_node_alloc = new_gres_cnt_node_alloc;
		xfree(gres_js->gres_bit_step_alloc);
		gres_js->gres_bit_step_alloc = new_gres_bit_step_alloc;
		xfree(gres_js->gres_cnt_step_alloc);
		gres_js->gres_cnt_step_alloc = new_gres_cnt_step_alloc;
	}
	list_iterator_destroy(gres_iter);

	/*
	 * Step two - Merge the gres information from the "from" job into the
	 * existing gres information for the "to" job
	 */
step2:	if (!from_job_gres_list)
		goto step3;
	if (!to_job_gres_list) {
		to_job_gres_list = list_create(gres_job_list_delete);
		free_to_job_gres_list = true;
	}
	gres_iter = list_iterator_create(from_job_gres_list);
	while ((gres_state_job = (gres_state_t *) list_next(gres_iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		gres_state_job2 = list_find_first(to_job_gres_list,
						  gres_find_id,
						  &gres_state_job->plugin_id);
		if (gres_state_job2) {
			gres_js2 = gres_state_job2->gres_data;
		} else {
			gres_js2 = xmalloc(sizeof(gres_job_state_t));
			gres_js2->cpus_per_gres =
				gres_js->cpus_per_gres;
			gres_js2->gres_per_job =
				gres_js->gres_per_job;
			gres_js2->gres_per_job =
				gres_js->gres_per_job;
			gres_js2->gres_per_socket =
				gres_js->gres_per_socket;
			gres_js2->gres_per_task =
				gres_js->gres_per_task;
			gres_js2->mem_per_gres =
				gres_js->mem_per_gres;
			gres_js2->ntasks_per_gres =
				gres_js->ntasks_per_gres;
			gres_js2->node_cnt = new_node_cnt;
			gres_js2->gres_bit_alloc =
				xcalloc(new_node_cnt, sizeof(bitstr_t *));
			gres_js2->gres_cnt_node_alloc =
				xcalloc(new_node_cnt, sizeof(uint64_t));
			gres_js2->gres_bit_step_alloc =
				xcalloc(new_node_cnt, sizeof(bitstr_t *));
			gres_js2->gres_cnt_step_alloc =
				xcalloc(new_node_cnt, sizeof(uint64_t));

			gres_state_job2 = gres_create_state(
				gres_state_job, GRES_STATE_SRC_STATE_PTR,
				GRES_STATE_TYPE_JOB, gres_js2);

			list_append(to_job_gres_list, gres_state_job2);
		}
		from_inx = to_inx = new_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			bool from_match = false, to_match = false;
			if (bit_test(to_job_node_bitmap, i)) {
				to_match = true;
				to_inx++;
			}
			if (bit_test(from_job_node_bitmap, i)) {
				from_match = true;
				from_inx++;
			}
			if (from_match || to_match)
				new_inx++;
			if (from_match) {
				if (!gres_js->gres_bit_alloc) {
					;
				} else if (select_hetero &&
					   gres_js2->
					   gres_bit_alloc[new_inx] &&
					   gres_js->gres_bit_alloc &&
					   gres_js->
					   gres_bit_alloc[new_inx]) {
					/* Merge job's GRES bitmaps */
					bit_or(gres_js2->
					       gres_bit_alloc[new_inx],
					       gres_js->
					       gres_bit_alloc[from_inx]);
				} else if (gres_js2->
					   gres_bit_alloc[new_inx]) {
					/* Keep original job's GRES bitmap */
				} else {
					gres_js2->gres_bit_alloc[new_inx] =
						gres_js->
						gres_bit_alloc[from_inx];
					gres_js->
						gres_bit_alloc
						[from_inx] = NULL;
				}
				if (!gres_js->gres_cnt_node_alloc) {
					;
				} else if (select_hetero &&
					   gres_js2->
					   gres_cnt_node_alloc[new_inx] &&
					   gres_js->gres_cnt_node_alloc &&
					   gres_js->
					   gres_cnt_node_alloc[new_inx]) {
					gres_js2->
						gres_cnt_node_alloc[new_inx] +=
						gres_js->
						gres_cnt_node_alloc[from_inx];
				} else if (gres_js2->
					   gres_cnt_node_alloc[new_inx]) {
					/* Keep original job's GRES bitmap */
				} else {
					gres_js2->
						gres_cnt_node_alloc[new_inx] =
						gres_js->
						gres_cnt_node_alloc[from_inx];
					gres_js->
						gres_cnt_node_alloc[from_inx]=0;
				}
				if (gres_js->gres_cnt_step_alloc &&
				    gres_js->
				    gres_cnt_step_alloc[from_inx]) {
					error("Attempt to merge gres, from "
					      "job has active steps");
				}
			}
		}
	}
	list_iterator_destroy(gres_iter);

step3:
	if (free_to_job_gres_list)
		FREE_NULL_LIST(to_job_gres_list);
}

/* Clear any vestigial job gres state. This may be needed on job requeue. */
extern void gres_stepmgr_job_clear_alloc(list_t *job_gres_list)
{

	if (job_gres_list == NULL)
		return;

	list_for_each(job_gres_list, _foreach_clear_job_gres, NULL);
}

static char *_build_shared_gres_details(char *nodes, int node_index,
					gres_state_t *gres_state_job,
					gres_job_state_t *gres_js)
{
	int gres_cnt_on_node = 0;
	gres_node_state_t *gres_ns = NULL;
	gres_state_t *gres_state_node;
	hostlist_t *host_list;
	char *node;
	node_record_t *node_ptr = NULL;
	char *pos = NULL;
	char *shared_gres_details_str = NULL;

	/* Use host list so that gres_js node index matches correct gres_ns */
	if (!(host_list = hostlist_create(nodes))) {
		error("Could not create hostlist from nodes '%s'", nodes);
		return NULL;
	}

	/* Find node record based on host list and node index */
	if (!(node = hostlist_nth(host_list, node_index))) {
		hostlist_destroy(host_list);
		return NULL;
	}
	hostlist_destroy(host_list);

	if (!(node_ptr = find_node_record(node))) {
		error("Could not find record for node '%s'", node);
		free(node);
		return NULL;
	}
	free(node);

	/* Find gres_state_node with plugin_id that matches gres_state_job */
	gres_state_node = list_find_first(node_ptr->gres_list, gres_find_id,
					  &gres_state_job->plugin_id);

	if (!gres_state_node)
		return NULL;

	gres_ns = gres_state_node->gres_data;

	if (!gres_ns)
		return NULL;

	/*
	 * Fill shared gres details string with info about allocated shared gres
	 * from gres_js->gres_bit_alloc, and info about available shared gres
	 * from gres_ns->topo_gres_cnt_avail
	 */
	gres_cnt_on_node = bit_size(gres_js->gres_bit_alloc[node_index]);
	for (int i = 0; i < gres_cnt_on_node; i++) {
		xstrfmtcatat(shared_gres_details_str, &pos,
			     "%"PRIu64"/%"PRIu64",",
			     gres_js->gres_per_bit_alloc[node_index][i],
			     gres_ns->topo_gres_cnt_avail[i]);
	}

	if (pos) {
		/* Strip the last comma off. */
		pos--;
		pos[0] = '\0';
	}

	return shared_gres_details_str;
}

/* Given a job's GRES data structure, return the indecies for selected elements
 * IN job_gres_list  - job's allocated GRES data structure
 * IN nodes - list of nodes allocated to job
 * OUT gres_detail_cnt - Number of elements (nodes) in gres_detail_str
 * OUT gres_detail_str - Description of GRES on each node
 * OUT total_gres_str - String containing all gres in the job and counts.
 */
extern void gres_stepmgr_job_build_details(
	list_t *job_gres_list, char *nodes,
	uint32_t *gres_detail_cnt,
	char ***gres_detail_str,
	char **total_gres_str)
{
	int i, j;
	list_itr_t *job_gres_iter;
	gres_state_t *gres_state_job;
	gres_job_state_t *gres_js;
	char *sep1, *sep2, tmp_str[128], *type, **my_gres_details = NULL;
	uint32_t my_gres_cnt = 0;
	char *gres_name, *gres_str = NULL;
	uint64_t gres_cnt;

	/* Release any vestigial data (e.g. from job requeue) */
	for (i = 0; i < *gres_detail_cnt; i++)
		xfree(gres_detail_str[0][i]);
	xfree(*gres_detail_str);
	xfree(*total_gres_str);
	*gres_detail_cnt = 0;

	if (job_gres_list == NULL)	/* No GRES allocated */
		return;

	(void) gres_init();

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = list_next(job_gres_iter))) {
		gres_js = (gres_job_state_t *) gres_state_job->gres_data;
		if (gres_js->gres_bit_alloc == NULL)
			continue;
		if (my_gres_details == NULL) {
			my_gres_cnt = gres_js->node_cnt;
			my_gres_details = xcalloc(my_gres_cnt, sizeof(char *));
		}

		if (gres_js->type_name) {
			sep2 = ":";
			type = gres_js->type_name;
		} else {
			sep2 = "";
			type = "";
		}

		gres_name = xstrdup_printf(
			"%s%s%s",
			gres_state_job->gres_name, sep2, type);
		gres_cnt = 0;

		for (j = 0; j < my_gres_cnt; j++) {
			uint64_t alloc_cnt;

			if (j >= gres_js->node_cnt)
				break;	/* node count mismatch */
			if (my_gres_details[j])
				sep1 = ",";
			else
				sep1 = "";

			if (gres_js->gres_cnt_node_alloc[j] == NO_CONSUME_VAL64)
				alloc_cnt = 0;
			else
				alloc_cnt = gres_js->gres_cnt_node_alloc[j];

			gres_cnt += alloc_cnt;

			if (gres_js->gres_bit_alloc[j] &&
			    (gres_js->gres_per_bit_alloc &&
			     gres_js->gres_per_bit_alloc[j])) {
				char *shared_gres_details =
					_build_shared_gres_details(
						nodes, j, gres_state_job, gres_js);
				xstrfmtcat(my_gres_details[j],
					   "%s%s:%" PRIu64 "(%s)", sep1,
					   gres_name, alloc_cnt,
					   shared_gres_details);
				xfree(shared_gres_details);

			} else if (gres_js->gres_bit_alloc[j]) {
				bit_fmt(tmp_str, sizeof(tmp_str),
					gres_js->gres_bit_alloc[j]);
				xstrfmtcat(my_gres_details[j],
					   "%s%s:%"PRIu64"(IDX:%s)",
					   sep1, gres_name,
					   alloc_cnt,
					   tmp_str);
			} else if (gres_js->gres_cnt_node_alloc[j]) {
				xstrfmtcat(my_gres_details[j],
					   "%s%s(CNT:%"PRIu64")",
					   sep1, gres_name,
					   alloc_cnt);
			}
		}

		xstrfmtcat(gres_str, "%s%s:%"PRIu64,
			   gres_str ? "," : "", gres_name, gres_cnt);
		xfree(gres_name);
	}
	list_iterator_destroy(job_gres_iter);
	*gres_detail_cnt = my_gres_cnt;
	*gres_detail_str = my_gres_details;
	*total_gres_str = gres_str;
}

/* Fill in job/node TRES arrays with allocated GRES. */
static void _set_type_tres_cnt(list_t *gres_list,
			       uint64_t *tres_cnt,
			       bool locked)
{
	list_itr_t *itr;
	gres_state_t *gres_state_ptr;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_rec;
	bool typeless_found = false, typeless = false;
	char *col_name = NULL, *prev_gres_name = NULL;
	uint64_t count;
	int tres_pos;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_rec, 0, sizeof(slurmdb_tres_rec_t));
		tres_rec.type = "gres";
	}

	if (!gres_list || !tres_cnt)
		return;

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	gres_clear_tres_cnt(tres_cnt, true);

	itr = list_iterator_create(gres_list);
	while ((gres_state_ptr = list_next(itr))) {
		bool set_total = false;
		tres_rec.name = gres_state_ptr->gres_name;

		/* Get alloc count for main GRES. */
		switch (gres_state_ptr->state_type) {
		case GRES_STATE_TYPE_JOB:
		{
			gres_job_state_t *gres_js = (gres_job_state_t *)
				gres_state_ptr->gres_data;

			/*
			 * If total_gres is set for selected (i.e.
			 * non-allocated) GRES and we had per job request we
			 * shouldn't use total_gres since it may be higher than
			 * actually requested. The way gres_sched_add works is
			 * that it adds as many GRES devices as we can use on
			 * the node. It may be more than requested to allow
			 * further optimization for instance based on nvlink,
			 * e.g. _set_task_bits.
			 */
			if (gres_js->gres_cnt_node_alloc ||
			    !gres_js->gres_per_job)
				count = gres_js->total_gres;
			else
				count = gres_js->gres_per_job;

			/*
			 * Resetting typeless_found to false when GRES name
			 * changes with respect to previous iteration until it
			 * is found again.
			 *
			 * This is needed in situations like i.e.:
			 * "--gres=gpu:1,tmpfs:foo:2,tmpfs:bar:7" where typeless
			 * is found for GRES name "gpu" but then for "tmpfs"
			 * it isn't, and thus the logic later around
			 * typeless_found would not set the count for "tmpfs"
			 * off of the sum of tmpfs:foo and tmpfs:bar counts.
			 */
			if (xstrcmp(prev_gres_name, tres_rec.name)) {
				typeless_found = false;
				xfree(prev_gres_name);
				prev_gres_name = xstrdup(tres_rec.name);
			}

			if (!gres_js->type_name) {
				typeless_found = true;
				typeless = true;
			} else {
				typeless = false;
			}

			break;
		}
		case GRES_STATE_TYPE_NODE:
		{
			gres_node_state_t *gres_ns = (gres_node_state_t *)
				gres_state_ptr->gres_data;
			count = gres_ns->gres_cnt_alloc;
			break;
		}
		default:
			error("%s: unsupported state type %d", __func__,
			      gres_state_ptr->state_type);
			continue;
		}
		/*
		 * Set main TRES's count (i.e. if no GRES "type" is being
		 * accounted for). We need to increment counter since the job
		 * may have been allocated multiple GRES types, but Slurm is
		 * only configured to track the total count. For example, a job
		 * allocated 1 GPU of type "tesla" and 1 GPU of type "volta",
		 * but we want to record that the job was allocated a total of
		 * 2 GPUs.
		 */
		if ((tres_pos = assoc_mgr_find_tres_pos(&tres_rec,true)) != -1){
			if (count == NO_CONSUME_VAL64)
				tres_cnt[tres_pos] = NO_CONSUME_VAL64;
			else if (!typeless_found)
				tres_cnt[tres_pos] += count;
			else if (typeless)
				tres_cnt[tres_pos] = count;
			/*
			 * No need for else statement, as all cases above should
			 * always cover setting main TRES's count.
			 */

			set_total = true;
		}

		/*
		 * Set TRES count for GRES model types. This would be handy for
		 * GRES like "gpu:tesla", where you might want to track both as
		 * TRES.
		 */
		switch (gres_state_ptr->state_type) {
		case GRES_STATE_TYPE_JOB:
		{
			gres_job_state_t *gres_js = (gres_job_state_t *)
				gres_state_ptr->gres_data;

			col_name = gres_js->type_name;
			if (col_name) {
				tres_rec.name = xstrdup_printf(
					"%s:%s",
					gres_state_ptr->gres_name,
					col_name);
				if ((tres_pos = assoc_mgr_find_tres_pos(
					     &tres_rec, true)) != -1)
					tres_cnt[tres_pos] = count;
				xfree(tres_rec.name);
			} else if (!set_total) {
				/*
				 * Job allocated GRES without "type"
				 * specification, but Slurm is only accounting
				 * for this GRES by specific "type", so pick
				 * some valid "type" to get some accounting.
				 * Although the reported "type" may not be
				 * accurate, it is better than nothing...
				 */
				tres_rec.name = gres_state_ptr->gres_name;
				if ((tres_pos = assoc_mgr_find_tres_pos2(
					     &tres_rec, true)) != -1)
					tres_cnt[tres_pos] = count;
			}
			break;
		}
		case GRES_STATE_TYPE_NODE:
		{
			int type;
			gres_node_state_t *gres_ns = (gres_node_state_t *)
				gres_state_ptr->gres_data;

			for (type = 0; type < gres_ns->type_cnt; type++) {
				col_name = gres_ns->type_name[type];
				if (!col_name)
					continue;

				tres_rec.name = xstrdup_printf(
					"%s:%s",
					gres_state_ptr->gres_name,
					col_name);

				count = gres_ns->type_cnt_alloc[type];

				if ((tres_pos = assoc_mgr_find_tres_pos(
					     &tres_rec, true)) != -1)
					tres_cnt[tres_pos] = count;
				xfree(tres_rec.name);
			}
			break;
		}
		default:
			error("%s: unsupported state type %d", __func__,
			      gres_state_ptr->state_type);
			continue;
		}
	}
	list_iterator_destroy(itr);
	xfree(prev_gres_name);

	if (!locked)
		assoc_mgr_unlock(&locks);
}
extern void gres_stepmgr_set_job_tres_cnt(
	list_t *gres_list, uint32_t node_cnt, uint64_t *tres_cnt, bool locked)
{
	if (!node_cnt || (node_cnt == NO_VAL))
		return;

	_set_type_tres_cnt(gres_list, tres_cnt, locked);
}

extern void gres_stepmgr_set_node_tres_cnt(
	list_t *gres_list, uint64_t *tres_cnt, bool locked)
{
	_set_type_tres_cnt(gres_list, tres_cnt, locked);
}

static uint64_t _step_get_gres_needed(gres_step_state_t *gres_ss,
				      bool first_step_node,
				      uint16_t tasks_on_node,
				      uint32_t rem_nodes, uint64_t *max_gres)
{
	uint64_t gres_needed;
	*max_gres = 0;
	if (first_step_node)
		gres_ss->total_gres = 0;

	if (gres_ss->gres_per_node) {
		gres_needed = gres_ss->gres_per_node;
	} else if (gres_ss->gres_per_task) {
		gres_needed = gres_ss->gres_per_task * tasks_on_node;
	} else if (gres_ss->ntasks_per_gres) {
		gres_needed = tasks_on_node / gres_ss->ntasks_per_gres;
	} else if (gres_ss->gres_per_step && (rem_nodes == 1)) {
		gres_needed = gres_ss->gres_per_step -
			gres_ss->total_gres;
	} else if (gres_ss->gres_per_step) {
		uint64_t tmp = gres_ss->total_gres + (rem_nodes - 1);

		/* Note: total_gres is the number of accumulated gres. */

		if (gres_ss->total_gres >= gres_ss->gres_per_step) {
			/* If we already have the gres required, get no more. */
			gres_needed = 0;
			*max_gres = 0;
		} else if (gres_ss->gres_per_step > tmp) {
			/* Leave at least one GRES per remaining node. */
			*max_gres = gres_ss->gres_per_step - tmp;
			gres_needed = 1;
		} else {
			/*
			 * We don't need enough gres to have one on every
			 * remaining node. Get all possible gres on each
			 * remaining node instead of trying to spread them out
			 * over the nodes.
			 */
			gres_needed = 1;
			*max_gres = gres_ss->gres_per_step -
					gres_ss->total_gres;
		}
	} else {
		/*
		 * No explicit step GRES specification.
		 * Note that gres_per_socket is not supported for steps
		 */
		gres_needed = INFINITE64; /* All allocated to job on Node */
	}

	return gres_needed;
}

static void _init_step_gres_per_bit(gres_job_state_t *gres_js,
				    gres_step_state_t *gres_ss, int n,
				    bool decr_job_alloc)
{
	if (!gres_js->gres_per_bit_alloc || !gres_js->gres_per_bit_alloc[n])
		error("Job has shared gres but there is no job gres_per_bit_alloc");

	if (decr_job_alloc && !gres_js->gres_per_bit_step_alloc)
		gres_js->gres_per_bit_step_alloc = xcalloc(gres_js->node_cnt,
							   sizeof(uint64_t *));
	if (decr_job_alloc && !gres_js->gres_per_bit_step_alloc[n])
		gres_js->gres_per_bit_step_alloc[n] = xcalloc(
			bit_size(gres_js->gres_bit_alloc[n]), sizeof(uint64_t));

	if (!gres_ss->gres_per_bit_alloc)
		gres_ss->gres_per_bit_alloc = xcalloc(gres_ss->node_cnt,
						      sizeof(uint64_t *));
	if (!gres_ss->gres_per_bit_alloc[n])
		gres_ss->gres_per_bit_alloc[n] = xcalloc(
			bit_size(gres_js->gres_bit_alloc[n]), sizeof(uint64_t));
}

static bool _shared_step_gres_avail(gres_job_state_t *gres_js,
				    gres_step_state_t *gres_ss,
				    uint64_t *gres_alloc, bool decr_job_alloc,
				    int n, int i)
{
	uint64_t cnt = MIN(*gres_alloc, gres_js->gres_per_bit_alloc[n][i]);

	if (decr_job_alloc)
		cnt = MIN(cnt,
			  (gres_js->gres_per_bit_alloc[n][i] -
			   gres_js->gres_per_bit_step_alloc[n][i]));

	if (!cnt)
		return false;

	if (decr_job_alloc)
		gres_js->gres_per_bit_step_alloc[n][i] += cnt;

	gres_ss->gres_per_bit_alloc[n][i] = cnt;

	*gres_alloc -= cnt;

	return true;
}

static int _set_step_gres_bit_alloc(gres_step_state_t *gres_ss,
				    gres_state_t *gres_state_job,
				    int node_offset,
				    slurm_step_id_t *step_id,
				    uint64_t gres_alloc,
				    bool decr_job_alloc,
				    list_t *node_gres_list,
				    bitstr_t *core_bitmap)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	int len = bit_size(gres_js->gres_bit_alloc[node_offset]);
	bitstr_t *gres_bit_alloc = bit_alloc(len);
	bitstr_t *gres_bit_avail = bit_copy(
		gres_js->gres_bit_alloc[node_offset]);
	gres_state_t *gres_state_node;
	gres_node_state_t *gres_ns;

	if (!(gres_state_node = list_find_first(node_gres_list,
						gres_find_id,
						&gres_state_job->plugin_id))) {
		error("No node gres when step gres is allocated. This should never happen.");
		return 0;
	}
	gres_ns = gres_state_node->gres_data;

	if (gres_id_shared(gres_state_job->config_flags)) {
		_init_step_gres_per_bit(gres_js, gres_ss, node_offset,
					decr_job_alloc);
	}

	if (decr_job_alloc &&
	    gres_js->gres_bit_step_alloc &&
	    gres_js->gres_bit_step_alloc[node_offset] &&
	    !gres_id_shared(gres_state_job->config_flags)) {
		bit_and_not(gres_bit_avail,
			    gres_js->gres_bit_step_alloc[node_offset]);
	}

	for (int i = 0; i < len && gres_alloc; i++) {
		if (!bit_test(gres_bit_avail, i) ||
		    bit_test(gres_bit_alloc, i) ||
		    !_cores_on_gres(core_bitmap, NULL, gres_ns, i, gres_js))
			continue;

		if (gres_id_shared(gres_state_job->config_flags)) {
			if (_shared_step_gres_avail(gres_js, gres_ss,
						    &gres_alloc, decr_job_alloc,
						    node_offset, i))
				bit_set(gres_bit_alloc, i);
		} else {
			bit_set(gres_bit_alloc, i);
			gres_alloc--;
		}
	}
	FREE_NULL_BITMAP(gres_bit_avail);

	if (decr_job_alloc) {
		if (!gres_js->gres_bit_step_alloc) {
			gres_js->gres_bit_step_alloc =
				xcalloc(gres_js->node_cnt,
					sizeof(bitstr_t *));
		}
		if (gres_js->gres_bit_step_alloc[node_offset]) {
			bit_or(gres_js->gres_bit_step_alloc[node_offset],
			       gres_bit_alloc);
		} else {
			gres_js->gres_bit_step_alloc[node_offset] =
				bit_copy(gres_bit_alloc);
		}
	}
	if (!gres_ss->gres_bit_alloc) {
		gres_ss->gres_bit_alloc =
			xcalloc(gres_js->node_cnt, sizeof(bitstr_t *));
	}
	if (gres_ss->gres_bit_alloc[node_offset]) {
		bit_or(gres_ss->gres_bit_alloc[node_offset],
		       gres_bit_alloc);
		FREE_NULL_BITMAP(gres_bit_alloc);
	} else {
		gres_ss->gres_bit_alloc[node_offset] = gres_bit_alloc;
	}

	return gres_alloc;
}

static int _step_alloc(gres_step_state_t *gres_ss,
		       gres_state_t *gres_state_step_req,
		       gres_state_t *gres_state_job,
		       int node_offset,
		       slurm_step_id_t *step_id,
		       uint64_t *gres_needed, uint64_t *max_gres,
		       bool decr_job_alloc,
		       uint64_t *step_node_mem_alloc,
		       list_t *node_gres_list,
		       bitstr_t *core_bitmap,
		       int *total_gres_cpu_cnt)
{
	gres_job_state_t *gres_js = gres_state_job->gres_data;
	gres_step_state_t *gres_ss_req = gres_state_step_req->gres_data;
	uint64_t gres_alloc, gres_left;

	xassert(gres_js);
	xassert(gres_ss);
	xassert(gres_ss_req);

	if (!gres_js->gres_cnt_node_alloc) {
		error("gres/%s: %s gres_cnt_node_alloc is not allocated",
		      gres_state_job->gres_name, __func__);
		return SLURM_ERROR;
	}
	if ((gres_js->gres_cnt_node_alloc[node_offset] == NO_CONSUME_VAL64) ||
	    (gres_js->total_gres == NO_CONSUME_VAL64)) {
		if (*gres_needed != INFINITE64)
			*gres_needed = 0;
		gres_ss->total_gres = NO_CONSUME_VAL64;
		return SLURM_SUCCESS;
	}

	if (node_offset >= gres_js->node_cnt) {
		error("gres/%s: %s for %ps, node offset invalid (%d >= %u)",
		      gres_state_job->gres_name, __func__, step_id, node_offset,
		      gres_js->node_cnt);
		return SLURM_ERROR;
	}

	if (gres_ss->node_cnt == 0)
		gres_ss->node_cnt = gres_js->node_cnt;
	if (!gres_ss->gres_cnt_node_alloc) {
		gres_ss->gres_cnt_node_alloc =
			xcalloc(gres_ss->node_cnt, sizeof(uint64_t));
	}

	if (!gres_js->gres_cnt_step_alloc) {
		gres_js->gres_cnt_step_alloc = xcalloc(
			gres_js->node_cnt, sizeof(uint64_t));
	}

	gres_alloc = gres_js->gres_cnt_node_alloc[node_offset];

	if (decr_job_alloc)
		gres_alloc -= gres_js->gres_cnt_step_alloc[node_offset];

	if (*gres_needed != INFINITE64) {
		if (*max_gres && decr_job_alloc) {
			gres_alloc = MIN(gres_alloc, *max_gres);
		} else
			gres_alloc = MIN(gres_alloc,*gres_needed);
	}

	if (gres_js->gres_bit_alloc && gres_js->gres_bit_alloc[node_offset]) {
		gres_left = _set_step_gres_bit_alloc(gres_ss, gres_state_job,
						     node_offset, step_id,
						     gres_alloc,
						     decr_job_alloc,
						     node_gres_list,
						     core_bitmap);
		if (gres_left && !core_bitmap) /* only on Pass 2 */
			error("gres/%s: %s %ps oversubscribed resources on node %d",
			      gres_state_job->gres_name, __func__, step_id,
			      node_offset);
		else
			gres_alloc -= gres_left;
	} else
		debug3("gres/%s: %s gres_bit_alloc for %ps is NULL",
		       gres_state_job->gres_name, __func__, step_id);

	if (*gres_needed != INFINITE64) {
		if (*max_gres && decr_job_alloc)
			*max_gres -= gres_alloc;
		if (gres_alloc < *gres_needed)
			*gres_needed -= gres_alloc;
		else
			*gres_needed = 0;
	}

	if (gres_ss->gres_cnt_node_alloc &&
	    (node_offset < gres_ss->node_cnt)) {
		gres_ss->gres_cnt_node_alloc[node_offset] += gres_alloc;
		/*
		 * Calculate memory allocated to the step based on the
		 * mem_per_gres limit.
		 * FIXME: Currently the only option that sets mem_per_gres is
		 * --mem-per-gpu. Adding another option will require a change
		 * here - perhaps we should take the MAX of all mem_per_gres.
		 * Similar logic is in gres_select_util_job_mem_set(),
		 * which would also need to be changed if another
		 * mem_per_gres option was added.
		 */
		if (gres_ss_req->mem_per_gres &&
		    (gres_ss_req->mem_per_gres != NO_VAL64))
			*step_node_mem_alloc +=
				gres_ss_req->mem_per_gres * gres_alloc;
	}
	gres_ss_req->total_gres += gres_alloc;
	gres_ss->total_gres += gres_alloc;

	if (gres_ss->node_in_use == NULL) {
		gres_ss->node_in_use = bit_alloc(gres_js->node_cnt);
	}
	bit_set(gres_ss->node_in_use, node_offset);
	if (decr_job_alloc)
		gres_js->gres_cnt_step_alloc[node_offset] += gres_alloc;
	if (gres_ss_req->cpus_per_gres != NO_VAL16)
		*total_gres_cpu_cnt += gres_alloc * gres_ss_req->cpus_per_gres;

	return SLURM_SUCCESS;
}

static gres_step_state_t *_step_get_alloc_gres_ptr(list_t *step_gres_list_alloc,
						   gres_state_t *gres_state_job)
{
	gres_key_t step_search_key;
	gres_step_state_t *gres_ss;
	gres_state_t *gres_state_step;
	gres_job_state_t *gres_js = gres_state_job->gres_data;

	/* Find in job_gres_list_alloc if it exists */
	step_search_key.config_flags = gres_state_job->config_flags;
	step_search_key.plugin_id = gres_state_job->plugin_id;
	step_search_key.type_id = gres_js->type_id;

	if (!(gres_state_step = list_find_first(step_gres_list_alloc,
						gres_find_step_by_key,
						&step_search_key))) {
		gres_ss = xmalloc(sizeof(*gres_ss));
		gres_ss->type_id = gres_js->type_id;
		gres_ss->type_name = xstrdup(gres_js->type_name);

		gres_state_step = xmalloc(sizeof(*gres_state_step));
		gres_state_step->config_flags = step_search_key.config_flags;
		gres_state_step->plugin_id = step_search_key.plugin_id;
		gres_state_step->gres_data = gres_ss;
		gres_state_step->gres_name = xstrdup(gres_state_job->gres_name);
		gres_state_step->state_type = GRES_STATE_TYPE_STEP;

		list_append(step_gres_list_alloc, gres_state_step);
	} else
		gres_ss = gres_state_step->gres_data;

	return gres_ss;
}

static int _step_alloc_type(gres_state_t *gres_state_job,
			    foreach_step_alloc_t *args)
{
	gres_job_state_t *gres_js = (gres_job_state_t *)
		gres_state_job->gres_data;
	gres_step_state_t *gres_ss = (gres_step_state_t *)
		args->gres_state_step->gres_data;
	gres_step_state_t *gres_ss_alloc;

	/*
	 * This isn't the gres we are looking for, or we already have allocated
	 * all of this GRES to other steps. If decr_job_alloc is false, then
	 * this step can share GRES. So, only do the last check if the step
	 * cannot share GRES (decr_job_alloc is true).
	 */
	if ((!args->gres_needed && !args->max_gres) ||
	    !gres_find_job_by_key_with_cnt(gres_state_job,
					   args->job_search_key) ||
	    (args->decr_job_alloc &&
	     (gres_js->gres_cnt_step_alloc[args->node_offset] ==
	      gres_js->gres_cnt_node_alloc[args->node_offset])))
		return 0;

	gres_ss_alloc = _step_get_alloc_gres_ptr(
		args->step_gres_list_alloc, gres_state_job);

	args->rc = _step_alloc(gres_ss_alloc, args->gres_state_step,
			       gres_state_job,
			       args->node_offset, &args->tmp_step_id,
			       &args->gres_needed, &args->max_gres,
			       args->decr_job_alloc,
			       args->step_node_mem_alloc,
			       args->node_gres_list,
			       args->core_bitmap,
			       &args->total_gres_cpu_cnt);

	if (args->rc != SLURM_SUCCESS) {
		return -1;
	}

	if (gres_ss->node_cnt == 0)
		gres_ss->node_cnt = gres_js->node_cnt;

	return 0;
}

extern int gres_stepmgr_step_alloc(
	list_t *step_gres_list,
	list_t **step_gres_list_alloc,
	list_t *job_gres_list,
	int node_offset, bool first_step_node,
	uint16_t tasks_on_node, uint32_t rem_nodes,
	uint32_t job_id, uint32_t step_id,
	bool decr_job_alloc,
	uint64_t *step_node_mem_alloc,
	list_t *node_gres_list,
	bitstr_t *core_bitmap,
	int *total_gres_cpu_cnt)
{
	int rc = SLURM_SUCCESS;
	list_itr_t *step_gres_iter;
	gres_state_t *gres_state_step;
	slurm_step_id_t tmp_step_id = { 0 };

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("%s: step allocates GRES, but job %u has none",
		      __func__, job_id);
		return ESLURM_INSUFFICIENT_GRES;
	}

	if (!*step_gres_list_alloc)
		*step_gres_list_alloc = list_create(gres_step_list_delete);

	xassert(step_node_mem_alloc);
	*step_node_mem_alloc = 0;

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	step_gres_iter = list_iterator_create(step_gres_list);
	while ((gres_state_step = list_next(step_gres_iter))) {
		gres_step_state_t *gres_ss =
			(gres_step_state_t *) gres_state_step->gres_data;
		gres_key_t job_search_key;
		foreach_step_alloc_t args;
		job_search_key.config_flags = gres_state_step->config_flags;
		job_search_key.plugin_id = gres_state_step->plugin_id;
		if (gres_ss->type_name)
			job_search_key.type_id = gres_ss->type_id;
		else
			job_search_key.type_id = NO_VAL;

		job_search_key.node_offset = node_offset;
		args.core_bitmap = core_bitmap;
		args.decr_job_alloc = decr_job_alloc;
		args.gres_needed = _step_get_gres_needed(
			gres_ss, first_step_node, tasks_on_node,
			rem_nodes, &args.max_gres);

		args.job_search_key = &job_search_key;
		args.node_gres_list = node_gres_list;
		args.node_offset = node_offset;
		args.rc = SLURM_SUCCESS;
		args.step_gres_list_alloc = *step_gres_list_alloc;
		args.gres_state_step = gres_state_step;
		args.step_node_mem_alloc = step_node_mem_alloc;
		args.tmp_step_id = tmp_step_id;
		args.total_gres_cpu_cnt = 0;

		/* Pass 1: Allocate GRES overlapping available cores */
		(void) list_for_each(job_gres_list, (ListForF) _step_alloc_type,
				     &args);
		if (args.gres_needed) {
			log_flag(STEPS, "cpus for optimal gres/%s topology unavailable for %ps allocating anyway.",
				 gres_state_step->gres_name, &tmp_step_id);
		}
		/* Pass 2: Allocate any available GRES */
		args.core_bitmap = NULL;
		(void) list_for_each(job_gres_list, (ListForF) _step_alloc_type,
				     &args);
		*total_gres_cpu_cnt += args.total_gres_cpu_cnt;

		if (args.rc != SLURM_SUCCESS)
			rc = args.rc;

		if (args.gres_needed && args.gres_needed != INFINITE64 &&
		    rc == SLURM_SUCCESS) {
			error("gres/%s: %s for %ps, step's > job's for node %d (gres still needed: %"PRIu64")",
			      gres_state_step->gres_name, __func__, &tmp_step_id,
			      node_offset, args.gres_needed);
			rc = ESLURM_INSUFFICIENT_GRES;
		}
	}
	list_iterator_destroy(step_gres_iter);

	return rc;
}

static int _step_dealloc(gres_state_t *gres_state_step, list_t *job_gres_list,
			 slurm_step_id_t *step_id, int node_offset,
			 bool decr_job_alloc)
{
	gres_state_t *gres_state_job;
	gres_step_state_t *gres_ss =
		(gres_step_state_t *)gres_state_step->gres_data;
	gres_job_state_t *gres_js;
	uint32_t j;
	uint64_t gres_cnt;
	int len_j, len_s;
	gres_key_t job_search_key;

	xassert(job_gres_list);
	xassert(gres_ss);

	job_search_key.config_flags = gres_state_step->config_flags;
	job_search_key.plugin_id = gres_state_step->plugin_id;
	if (gres_ss->type_name)
		job_search_key.type_id = gres_ss->type_id;
	else
		job_search_key.type_id = NO_VAL;
	job_search_key.node_offset = node_offset;
	if (!(gres_state_job = list_find_first(
		      job_gres_list,
		      gres_find_job_by_key_with_cnt,
		      &job_search_key)))
		return SLURM_SUCCESS;

	gres_js = (gres_job_state_t *)gres_state_job->gres_data;
	if (gres_js->total_gres == NO_CONSUME_VAL64) {
		xassert(!gres_ss->node_in_use);
		xassert(!gres_ss->gres_bit_alloc);
		return SLURM_SUCCESS;
	} else if (gres_js->node_cnt < node_offset) {
		/*
		 * gres_find_job_by_key_with_cnt() already does this
		 * check so we should never get here, but here as a
		 * sanity check.
		 */
		return SLURM_SUCCESS;
	}

	if (!gres_ss->node_in_use) {
		error("gres/%s: %s %ps dealloc, node_in_use is NULL",
		      gres_state_job->gres_name, __func__, step_id);
		return SLURM_ERROR;
	}

	if (!bit_test(gres_ss->node_in_use, node_offset))
		return SLURM_SUCCESS;

	if (!decr_job_alloc) {
		/* This step was not counted against job allocation */
		if (gres_ss->gres_bit_alloc)
			FREE_NULL_BITMAP(gres_ss->gres_bit_alloc[node_offset]);
		return SLURM_SUCCESS;
	}

	if (gres_ss->gres_cnt_node_alloc)
		gres_cnt = gres_ss->gres_cnt_node_alloc[node_offset];
	else {
		error("gres/%s: %s %ps dealloc, gres_cnt_node_alloc is NULL",
		      gres_state_job->gres_name, __func__, step_id);
		return SLURM_ERROR;
	}

	if (gres_js->gres_cnt_step_alloc) {
		if (gres_js->gres_cnt_step_alloc[node_offset] >= gres_cnt) {
			gres_js->gres_cnt_step_alloc[node_offset] -= gres_cnt;
		} else {
			error("gres/%s: %s %ps dealloc count underflow",
			      gres_state_job->gres_name, __func__,
			      step_id);
			gres_js->gres_cnt_step_alloc[node_offset] = 0;
		}
	}
	if ((gres_ss->gres_bit_alloc == NULL) ||
	    (gres_ss->gres_bit_alloc[node_offset] == NULL))
		return SLURM_SUCCESS;
	if (gres_js->gres_bit_alloc[node_offset] == NULL) {
		error("gres/%s: %s job %u gres_bit_alloc[%d] is NULL",
		      gres_state_job->gres_name, __func__,
		      step_id->job_id, node_offset);
		return SLURM_SUCCESS;
	}
	len_j = bit_size(gres_js->gres_bit_alloc[node_offset]);
	len_s = bit_size(gres_ss->gres_bit_alloc[node_offset]);
	if (len_j != len_s) {
		error("gres/%s: %s %ps dealloc, bit_alloc[%d] size mis-match (%d != %d)",
		      gres_state_job->gres_name, __func__,
		      step_id, node_offset, len_j, len_s);
		len_j = MIN(len_j, len_s);
	}
	for (j = 0; j < len_j; j++) {
		if (!bit_test(gres_ss->gres_bit_alloc[node_offset], j))
			continue;
		if (gres_js->gres_bit_step_alloc &&
		    gres_js->gres_bit_step_alloc[node_offset]) {
			bit_clear(gres_js->gres_bit_step_alloc[node_offset],
				  j);
			if (gres_id_shared(gres_state_job->config_flags) &&
			    gres_js->gres_per_bit_step_alloc[node_offset] &&
			    gres_ss->gres_per_bit_alloc[node_offset])
				gres_js->gres_per_bit_step_alloc[node_offset]
								[j] -=
					gres_ss->gres_per_bit_alloc[node_offset]
								   [j];
		}
	}
	FREE_NULL_BITMAP(gres_ss->gres_bit_alloc[node_offset]);
	if (gres_ss->gres_per_bit_alloc)
		xfree(gres_ss->gres_per_bit_alloc[node_offset]);

	return SLURM_SUCCESS;
}

extern int gres_stepmgr_step_dealloc(
	list_t *step_gres_list, list_t *job_gres_list,
	uint32_t job_id, uint32_t step_id,
	int node_offset,
	bool decr_job_alloc)
{
	int rc = SLURM_SUCCESS, rc2;
	list_itr_t *step_gres_iter;
	gres_state_t *gres_state_step;
	slurm_step_id_t tmp_step_id;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("%s: step deallocates gres, but job %u has none",
		      __func__, job_id);
		return SLURM_ERROR;
	}

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	step_gres_iter = list_iterator_create(step_gres_list);
	while ((gres_state_step = list_next(step_gres_iter))) {
		rc2 = _step_dealloc(gres_state_step,
				    job_gres_list,
				    &tmp_step_id,
				    node_offset,
				    decr_job_alloc);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(step_gres_iter);

	return rc;
}

/*
 * A job allocation size has changed. Update the job step gres information
 * bitmaps and other data structures.
 * IN gres_list - List of Gres records for this step to track usage
 * IN orig_job_node_bitmap - bitmap of nodes in the original job allocation
 * IN new_job_node_bitmap  - bitmap of nodes in the new job allocation
 */
void gres_stepmgr_step_state_rebase(
	list_t *gres_list,
	bitstr_t *orig_job_node_bitmap,
	bitstr_t *new_job_node_bitmap)
{
	list_itr_t *gres_iter;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss;
	int new_node_cnt;
	int i_first, i_last, i;
	int old_inx, new_inx;
	bitstr_t *new_node_in_use;
	bitstr_t **new_gres_bit_alloc = NULL;

	if (gres_list == NULL)
		return;

	gres_iter = list_iterator_create(gres_list);
	while ((gres_state_step = list_next(gres_iter))) {
		gres_ss = (gres_step_state_t *) gres_state_step->gres_data;
		if (!gres_ss)
			continue;
		if (!gres_ss->node_in_use) {
			error("gres_step_state_rebase: node_in_use is NULL");
			continue;
		}
		new_node_cnt = bit_set_count(new_job_node_bitmap);
		i_first = MIN(bit_ffs(orig_job_node_bitmap),
			      bit_ffs(new_job_node_bitmap));
		i_first = MAX(i_first, 0);
		i_last  = MAX(bit_fls(orig_job_node_bitmap),
			      bit_fls(new_job_node_bitmap));
		if (i_last == -1) {
			error("gres_step_state_rebase: node_bitmaps "
			      "are empty");
			continue;
		}
		new_node_in_use = bit_alloc(new_node_cnt);

		old_inx = new_inx = -1;
		for (i = i_first; i <= i_last; i++) {
			bool old_match = false, new_match = false;
			if (bit_test(orig_job_node_bitmap, i)) {
				old_match = true;
				old_inx++;
			}
			if (bit_test(new_job_node_bitmap, i)) {
				new_match = true;
				new_inx++;
			}
			if (old_match && new_match) {
				bit_set(new_node_in_use, new_inx);
				if (gres_ss->gres_bit_alloc) {
					if (!new_gres_bit_alloc) {
						new_gres_bit_alloc = xcalloc(
							new_node_cnt,
							sizeof(bitstr_t *));
					}
					new_gres_bit_alloc[new_inx] =
						gres_ss->
						gres_bit_alloc[old_inx];
				}
			} else if (old_match &&
				   gres_ss->gres_bit_alloc &&
				   gres_ss->gres_bit_alloc[old_inx]) {
				/*
				 * Node removed from job allocation,
				 * release step's resources
				 */
				FREE_NULL_BITMAP(gres_ss->
					 gres_bit_alloc[old_inx]);
			}
		}

		gres_ss->node_cnt = new_node_cnt;
		FREE_NULL_BITMAP(gres_ss->node_in_use);
		gres_ss->node_in_use = new_node_in_use;
		xfree(gres_ss->gres_bit_alloc);
		gres_ss->gres_bit_alloc = new_gres_bit_alloc;
	}
	list_iterator_destroy(gres_iter);
}

static void _gres_add_2_tres_str(char **tres_str, slurmdb_tres_rec_t *tres_rec,
				 uint64_t count)
{
	uint64_t old_count;

	old_count = slurmdb_find_tres_count_in_string(*tres_str, tres_rec->id);
	if (old_count == INFINITE64) {
		/* New gres */
		xstrfmtcat(*tres_str, "%s%u=%"PRIu64, *tres_str ? "," : "",
			   tres_rec->id, count);
	} else {
		/* Add gres counts together */
		char *tmp_str = xstrdup_printf("%u=", tres_rec->id);
		char *cut = xstrstr(*tres_str, tmp_str) + strlen(tmp_str);
		xfree(tmp_str);

		cut[0] = 0;
		xstrfmtcat(*tres_str, "%"PRIu64"%s", old_count + count,
			   xstrstr(cut + 1, ","));
	}
}

static void _gres_2_tres_str_internal(char **tres_str,
				      char *gres_name, char *gres_type,
				      uint64_t count)
{
	slurmdb_tres_rec_t *tres_rec;
	static bool first_run = 1;
	static slurmdb_tres_rec_t tres_req;

	/* we only need to init this once */
	if (first_run) {
		first_run = 0;
		memset(&tres_req, 0, sizeof(slurmdb_tres_rec_t));
		tres_req.type = "gres";
	}

	xassert(verify_assoc_lock(TRES_LOCK, READ_LOCK));
	xassert(gres_name);
	xassert(tres_str);

	tres_req.name = gres_name;
	tres_rec = assoc_mgr_find_tres_rec(&tres_req);

	if (tres_rec)
		_gres_add_2_tres_str(tres_str, tres_rec, count);

	if (gres_type) {
		/*
		 * Now let's put of the : name TRES if we are
		 * tracking it as well.  This would be handy
		 * for GRES like "gpu:tesla", where you might
		 * want to track both as TRES.
		 */
		tres_req.name = xstrdup_printf("%s:%s", gres_name, gres_type);
		tres_rec = assoc_mgr_find_tres_rec(&tres_req);
		xfree(tres_req.name);

		if (tres_rec)
			_gres_add_2_tres_str(tres_str, tres_rec, count);
	}
}

/*
 * Given a job's GRES data structure, return a simple tres string of gres
 * allocated on the node_inx requested
 * IN job_gres_list  - job's alllocated GRES data structure
 * IN node_inx - position of node in gres_js->gres_cnt_node_alloc
 * IN locked - if the assoc_mgr tres read locked is locked or not
 *
 * RET - simple string containing gres this job is allocated on the node
 * requested.
 */
extern char *gres_stepmgr_gres_on_node_as_tres(
	list_t *job_gres_list, int node_inx, bool locked)
{
	list_itr_t *job_gres_iter;
	gres_state_t *gres_state_job;
	char *tres_str = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!job_gres_list)	/* No GRES allocated */
		return NULL;

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((gres_state_job = list_next(job_gres_iter))) {
		uint64_t count;
		gres_job_state_t *gres_js =
			(gres_job_state_t *)gres_state_job->gres_data;
		if (!gres_js->gres_bit_alloc)
			continue;

		if (node_inx > gres_js->node_cnt)
			break;

		if (!gres_state_job->gres_name) {
			debug("%s: couldn't find name", __func__);
			continue;
		}

		/* If we are no_consume, print a 0 */
		if (gres_js->total_gres == NO_CONSUME_VAL64)
			count = 0;
		else if (gres_js->gres_cnt_node_alloc[node_inx])
			count = gres_js->gres_cnt_node_alloc[node_inx];
		else /* If this gres isn't on the node skip it */
			continue;
		_gres_2_tres_str_internal(&tres_str,
					  gres_state_job->gres_name,
					  gres_js->type_name,
					  count);
	}
	list_iterator_destroy(job_gres_iter);

	if (!locked)
		assoc_mgr_unlock(&locks);

	return tres_str;
}

static uint64_t _step_test(gres_step_state_t *gres_ss, bool first_step_node,
			   uint16_t cpus_per_task, int max_rem_nodes,
			   bool ignore_alloc, uint64_t gres_cnt, bool test_mem,
			   int node_offset, slurm_step_id_t *step_id,
			   job_resources_t *job_resrcs_ptr, int *err_code)
{
	uint64_t cpu_cnt, min_gres = 1, task_cnt;

	xassert(gres_ss);

	if (!gres_cnt)
		return 0;

	if (first_step_node) {
		gres_ss->gross_gres = 0;
		gres_ss->total_gres = 0;
	}
	if (gres_ss->gres_per_node)
		min_gres = gres_ss-> gres_per_node;
	if (gres_ss->gres_per_socket)
		min_gres = MAX(min_gres, gres_ss->gres_per_socket);
	if (gres_ss->gres_per_task)
		min_gres = MAX(min_gres, gres_ss->gres_per_task);
	if (gres_ss->gres_per_step &&
	    (gres_ss->gres_per_step > gres_ss->total_gres) &&
	    (max_rem_nodes == 1)) {
		uint64_t gres_per_step = gres_ss->gres_per_step;
		if (ignore_alloc)
			gres_per_step -= gres_ss->gross_gres;
		else
			gres_per_step -= gres_ss->total_gres;
		min_gres = MAX(min_gres, gres_per_step);
	}

	if (gres_cnt != NO_VAL64) {
		uint16_t cpus_per_gres = gres_ss->cpus_per_gres;

		if (min_gres > gres_cnt) {
			cpu_cnt = 0;
		} else if (cpus_per_gres && (cpus_per_gres != NO_VAL16)) {
			cpu_cnt = cpus_per_gres * gres_cnt;
		} else if (gres_ss->gres_per_task) {
			task_cnt = (gres_cnt + gres_ss->gres_per_task - 1)
				/ gres_ss->gres_per_task;
			cpu_cnt = task_cnt * cpus_per_task;
		} else
			cpu_cnt = NO_VAL64;
	} else {
		gres_cnt = 0;
		cpu_cnt = NO_VAL64;
	}

	/* Test if there is enough memory available to run the step. */
	if (test_mem && cpu_cnt && gres_cnt && gres_ss->mem_per_gres &&
	    (gres_ss->mem_per_gres != NO_VAL64)) {
		uint64_t mem_per_gres, mem_req, mem_avail;

		mem_per_gres = gres_ss->mem_per_gres;
		mem_req = min_gres * mem_per_gres;
		mem_avail = job_resrcs_ptr->memory_allocated[node_offset];
		if (!ignore_alloc)
			mem_avail -= job_resrcs_ptr->memory_used[node_offset];

		if (mem_avail < mem_req) {
			log_flag(STEPS, "%s: JobId=%u: Usable memory on node: %"PRIu64" is less than requested %"PRIu64", skipping the node",
				 __func__, step_id->job_id, mem_avail,
				 mem_req);
			cpu_cnt = 0;
			*err_code = ESLURM_INVALID_TASK_MEMORY;
		}
	}

	if (cpu_cnt != 0) {
		if (ignore_alloc)
			gres_ss->gross_gres += gres_cnt;
		else
			gres_ss->total_gres += gres_cnt;
	}

	return cpu_cnt;
}

static int _step_get_gres_cnt(void *x, void *arg)
{
	gres_state_t *gres_state_job = (gres_state_t *)x;
	foreach_gres_cnt_t *foreach_gres_cnt = (foreach_gres_cnt_t *)arg;
	gres_job_state_t *gres_js;
	gres_key_t *job_search_key = foreach_gres_cnt->job_search_key;
	bool ignore_alloc = foreach_gres_cnt->ignore_alloc;
	slurm_step_id_t *step_id = foreach_gres_cnt->step_id;
	int node_offset = job_search_key->node_offset;

	/* This isn't the gres we are looking for */
	if (!gres_find_job_by_key_with_cnt(gres_state_job, job_search_key))
		return 0;

	/* This is the first time we have found a matching GRES. */
	if (foreach_gres_cnt->gres_cnt == INFINITE64)
		foreach_gres_cnt->gres_cnt = 0;

	gres_js = gres_state_job->gres_data;

	if (gres_js->total_gres == NO_CONSUME_VAL64) {
		foreach_gres_cnt->gres_cnt = NO_CONSUME_VAL64;
		return -1;
	}

	if ((node_offset >= gres_js->node_cnt)) {
		error("gres/%s: %s %ps node offset invalid (%d >= %u)",
		      gres_state_job->gres_name, __func__, step_id,
		      node_offset, gres_js->node_cnt);
		foreach_gres_cnt->gres_cnt = 0;
		return -1;
	}
	if (!gres_id_shared(job_search_key->config_flags) &&
	    gres_js->gres_bit_alloc &&
	    gres_js->gres_bit_alloc[node_offset]) {
		foreach_gres_cnt->gres_cnt += bit_set_count(
			gres_js->gres_bit_alloc[node_offset]);
		if (!ignore_alloc &&
		    gres_js->gres_bit_step_alloc &&
		    gres_js->gres_bit_step_alloc[node_offset]) {
			foreach_gres_cnt->gres_cnt -=
				bit_set_count(gres_js->
					      gres_bit_step_alloc[node_offset]);
		}
	} else if (gres_js->gres_cnt_node_alloc &&
		   gres_js->gres_cnt_step_alloc) {
		foreach_gres_cnt->gres_cnt +=
			gres_js->gres_cnt_node_alloc[node_offset];
		if (!ignore_alloc) {
			foreach_gres_cnt->gres_cnt -= gres_js->
				gres_cnt_step_alloc[node_offset];
		}
	} else {
		debug3("gres/%s:%s: %s %ps gres_bit_alloc and gres_cnt_node_alloc are NULL",
		       gres_state_job->gres_name, gres_js->type_name,
		       __func__, step_id);
		foreach_gres_cnt->gres_cnt = NO_VAL64;
		return -1;
	}
	return 0;
}

extern uint64_t gres_stepmgr_step_test(gres_stepmgr_step_test_args_t *args)
{
	uint64_t cpu_cnt, tmp_cnt;
	uint16_t cpus_per_task = args->cpus_per_task;
	list_itr_t *step_gres_iter;
	gres_state_t *gres_state_step;
	gres_step_state_t *gres_ss = NULL;
	slurm_step_id_t tmp_step_id;
	foreach_gres_cnt_t foreach_gres_cnt;

	if (args->step_gres_list == NULL)
		return NO_VAL64;
	if (args->job_gres_list == NULL)
		return 0;

	if (cpus_per_task == 0)
		cpus_per_task = 1;
	cpu_cnt = NO_VAL64;
	(void) gres_init();
	*(args->err_code) = SLURM_SUCCESS;

	tmp_step_id.job_id = args->job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = args->step_id;

	memset(&foreach_gres_cnt, 0, sizeof(foreach_gres_cnt));
	foreach_gres_cnt.ignore_alloc = args->ignore_alloc;
	foreach_gres_cnt.step_id = &tmp_step_id;

	step_gres_iter = list_iterator_create(args->step_gres_list);
	while ((gres_state_step = (gres_state_t *) list_next(step_gres_iter))) {
		gres_key_t job_search_key;

		gres_ss = (gres_step_state_t *)gres_state_step->gres_data;
		job_search_key.config_flags = gres_state_step->config_flags;
		job_search_key.plugin_id = gres_state_step->plugin_id;
		if (gres_ss->type_name)
			job_search_key.type_id = gres_ss->type_id;
		else
			job_search_key.type_id = NO_VAL;

		job_search_key.node_offset = args->node_offset;

		foreach_gres_cnt.job_search_key = &job_search_key;
		foreach_gres_cnt.gres_cnt = INFINITE64;

		(void)list_for_each(args->job_gres_list, _step_get_gres_cnt,
				    &foreach_gres_cnt);

		if (foreach_gres_cnt.gres_cnt == INFINITE64) {
			log_flag(STEPS, "%s: Job lacks GRES (%s:%s) required by the step",
				 __func__, gres_state_step->gres_name,
				 gres_ss->type_name);
			cpu_cnt = 0;
			break;
		}

		if (foreach_gres_cnt.gres_cnt == NO_CONSUME_VAL64) {
			cpu_cnt = NO_VAL64;
			break;
		}

		tmp_cnt = _step_test(gres_ss, args->first_step_node,
				     cpus_per_task, args->max_rem_nodes,
				     args->ignore_alloc,
				     foreach_gres_cnt.gres_cnt,
				     args->test_mem, args->node_offset,
				     &tmp_step_id,
				     args->job_resrcs_ptr, args->err_code);
		if ((tmp_cnt != NO_VAL64) && (tmp_cnt < cpu_cnt))
			cpu_cnt = tmp_cnt;

		if (cpu_cnt == 0)
			break;
	}
	list_iterator_destroy(step_gres_iter);

	return cpu_cnt;
}

extern char *gres_stepmgr_gres_2_tres_str(list_t *gres_list, bool locked)
{
	list_itr_t *itr;
	gres_state_t *gres_state_ptr;
	uint64_t count;
	char *col_name = NULL;
	char *tres_str = NULL;
	assoc_mgr_lock_t locks = { .tres = READ_LOCK };

	if (!gres_list)
		return NULL;

	/* must be locked first before gres_contrex_lock!!! */
	if (!locked)
		assoc_mgr_lock(&locks);

	itr = list_iterator_create(gres_list);
	while ((gres_state_ptr = list_next(itr))) {
		switch (gres_state_ptr->state_type) {
		case GRES_STATE_TYPE_JOB:
		{
			gres_job_state_t *gres_js = (gres_job_state_t *)
				gres_state_ptr->gres_data;
			col_name = gres_js->type_name;
			count = gres_js->total_gres;
			break;
		}
		case GRES_STATE_TYPE_STEP:
		{
			gres_step_state_t *gres_ss = (gres_step_state_t *)
				gres_state_ptr->gres_data;
			col_name = gres_ss->type_name;
			count = gres_ss->total_gres;
			break;
		}
		default:
			error("%s: unsupported state type %d", __func__,
			      gres_state_ptr->state_type);
			continue;
		}

		/* If we are no_consume, print a 0 */
		if (count == NO_CONSUME_VAL64)
			count = 0;

		_gres_2_tres_str_internal(&tres_str,
					  gres_state_ptr->gres_name,
					  col_name, count);
	}
	list_iterator_destroy(itr);

	if (!locked)
		assoc_mgr_unlock(&locks);

	return tres_str;
}

/*
 * Increment indexes to next round-robin index
 * IN/OUT cur_inx - bitmap index
 * IN/OUT node_inx - job node index
 */
static int _gres_next_node_inx(int *cur_inx, int *node_inx, int len,
			       int node_cnt, bitstr_t *nodes_bitmap,
			       int start_inx)
{
	bool wrapped = false;
	xassert(cur_inx);
	xassert(node_inx);
	xassert(nodes_bitmap);

	if (!len)
		return SLURM_ERROR;

	if (*node_inx == -1) {
		if (start_inx)
			*node_inx += bit_set_count_range(nodes_bitmap, 0,
							 start_inx);
		*cur_inx = start_inx;

	} else {
		*cur_inx = (*cur_inx + 1) % len;
		wrapped = *cur_inx <= start_inx;
		if (*cur_inx == start_inx)
			return SLURM_ERROR; /* Normal break case */
	}

	*cur_inx = bit_ffs_from_bit(nodes_bitmap, *cur_inx);

	if (wrapped && (*cur_inx >= start_inx))
		return SLURM_ERROR; /* Normal break case */

	if (*cur_inx < 0) {
		xassert(false);
		return SLURM_ERROR; /* This should never happen */
	}

	*node_inx = (*node_inx + 1) % node_cnt;
	return SLURM_SUCCESS;
}

/*
 * If a step gres request used gres_per_step it must be tested more than just in
 * gres_stepmgr_step_test. This function only acts when gres_per_step is used
 * IN step_gres_list  - step's requested GRES data structure
 * IN job_ptr - Job data
 * IN/OUT nodes_avail - Bitstring of nodes available for this step to use
 * IN min_nodes - minimum nodes required for this step
 */
extern void gres_stepmgr_step_test_per_step(
	list_t *step_gres_list,
	job_record_t *job_ptr,
	bitstr_t *nodes_avail,
	int min_nodes)
{
	list_itr_t *step_gres_iter;
	gres_state_t *gres_state_step;
	slurm_step_id_t tmp_step_id;
	foreach_gres_cnt_t foreach_gres_cnt;
	bitstr_t *node_bitmap = job_ptr->job_resrcs->node_bitmap;
	int i_first, bit_len;

	if (!step_gres_list)
		return;
	if (!job_ptr->gres_list_alloc)
		return;

	(void) gres_init();
	i_first = job_ptr->job_resrcs->next_step_node_inx;
	bit_len = bit_fls(node_bitmap) + 1;
	if (i_first >= bit_len)
		i_first = 0;

	tmp_step_id.job_id = job_ptr->job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = NO_VAL;

	memset(&foreach_gres_cnt, 0, sizeof(foreach_gres_cnt));
	foreach_gres_cnt.ignore_alloc = false;
	foreach_gres_cnt.step_id = &tmp_step_id;

	step_gres_iter = list_iterator_create(step_gres_list);
	while ((gres_state_step = list_next(step_gres_iter))) {
		gres_key_t job_search_key;
		int32_t *gres_cnts;
		int gres_req, limit;
		bitstr_t *nodes_picked;
		gres_step_state_t *gres_ss = gres_state_step->gres_data;

		if (!gres_ss->gres_per_step)
			continue;

		gres_req = gres_ss->gres_per_step;
		limit = (gres_req + min_nodes - 1) / min_nodes;

		job_search_key.config_flags = gres_state_step->config_flags;
		job_search_key.plugin_id = gres_state_step->plugin_id;
		if (gres_ss->type_name)
			job_search_key.type_id = gres_ss->type_id;
		else
			job_search_key.type_id = NO_VAL;

		foreach_gres_cnt.job_search_key = &job_search_key;

		nodes_picked = bit_alloc(bit_size(nodes_avail));
		gres_cnts = xcalloc(job_ptr->node_cnt, sizeof(uint64_t));
		for (int node_inx = 0; node_inx < job_ptr->node_cnt; node_inx++)
			gres_cnts[node_inx] = NO_VAL;

		/*
		 * Select nodes until enough gres has been allocated.
		 * Starting with nodes that have an equal share available each,
		 */
		while (limit >= 0) {
			int next_smallest = -1;
			int i, node_inx = -1;
			while (_gres_next_node_inx(&i, &node_inx, bit_len,
						   job_ptr->job_resrcs->nhosts,
						   node_bitmap, i_first) ==
			       SLURM_SUCCESS) {
				if (!bit_test(nodes_avail, i) ||
				    bit_test(nodes_picked, i))
					continue;

				/* Only calculate gres cnt once */
				if (gres_cnts[node_inx] == NO_VAL) {
					job_search_key.node_offset = node_inx;
					foreach_gres_cnt.gres_cnt = INFINITE64;
					(void) list_for_each(
						job_ptr->gres_list_alloc,
						_step_get_gres_cnt,
						&foreach_gres_cnt);
					gres_cnts[node_inx] =
						foreach_gres_cnt.gres_cnt;
				}

				if (gres_cnts[node_inx] >= limit) {
					bit_set(nodes_picked, i);
					gres_req -= gres_cnts[node_inx];
				} else if (gres_cnts[node_inx] >
					   next_smallest) {
					next_smallest = gres_cnts[node_inx];
				}

				if ((gres_req <= 0) &&
				    (bit_set_count(nodes_picked) >=
				     min_nodes)) {
					bit_and(nodes_avail, nodes_picked);
					next_smallest = -1; /* exit loop */
					break;
				}
			}
			limit = next_smallest;
		}
		FREE_NULL_BITMAP(nodes_picked);
		xfree(gres_cnts);
	}
	list_iterator_destroy(step_gres_iter);
}
