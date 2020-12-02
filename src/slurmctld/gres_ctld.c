/*****************************************************************************\
 *  gres_ctld.c - Functions for gres used only in the slurmctld
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
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

#include "gres_ctld.h"
#include "src/common/xstring.h"

/*
 * Determine if specific GRES index on node is available to a job's allocated
 *	cores
 * IN core_bitmap - bitmap of cores allocated to the job on this node
 * IN/OUT alloc_core_bitmap - cores already allocated, NULL if don't care,
 *		updated when the function returns true
 * IN node_gres_ptr - GRES data for this node
 * IN gres_inx - index of GRES being considered for use
 * IN job_gres_ptr - GRES data for this job
 * RET true if available to those core, false otherwise
 */
static bool _cores_on_gres(bitstr_t *core_bitmap, bitstr_t *alloc_core_bitmap,
			   gres_node_state_t *node_gres_ptr, int gres_inx,
			   gres_job_state_t *job_gres_ptr)
{
	int i, avail_cores;

	if ((core_bitmap == NULL) || (node_gres_ptr->topo_cnt == 0))
		return true;

	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (!node_gres_ptr->topo_gres_bitmap[i])
			continue;
		if (bit_size(node_gres_ptr->topo_gres_bitmap[i]) < gres_inx)
			continue;
		if (!bit_test(node_gres_ptr->topo_gres_bitmap[i], gres_inx))
			continue;
		if (job_gres_ptr->type_name &&
		    (!node_gres_ptr->topo_type_name[i] ||
		     (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i])))
			continue;
		if (!node_gres_ptr->topo_core_bitmap[i])
			return true;
		if (bit_size(node_gres_ptr->topo_core_bitmap[i]) !=
		    bit_size(core_bitmap))
			break;
		avail_cores = bit_overlap(node_gres_ptr->topo_core_bitmap[i],
					  core_bitmap);
		if (avail_cores && alloc_core_bitmap) {
			avail_cores -= bit_overlap(node_gres_ptr->
						   topo_core_bitmap[i],
						   alloc_core_bitmap);
			if (avail_cores) {
				bit_or(alloc_core_bitmap,
				       node_gres_ptr->topo_core_bitmap[i]);
			}
		}
		if (avail_cores)
			return true;
	}
	return false;
}

static int _job_alloc(void *job_gres_data, void *node_gres_data, int node_cnt,
		      int node_index, int node_offset, char *gres_name,
		      uint32_t job_id, char *node_name,
		      bitstr_t *core_bitmap, uint32_t plugin_id,
		      uint32_t user_id)
{
	int j, sz1, sz2;
	int64_t gres_cnt, i;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bool type_array_updated = false;
	bitstr_t *alloc_core_bitmap = NULL;
	uint64_t gres_per_bit = 1;
	bool log_cnt_err = true;
	char *log_type;
	bool shared_gres = false, use_busy_dev = false;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_cnt);
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);

	if (node_gres_ptr->no_consume) {
		job_gres_ptr->total_gres = NO_CONSUME_VAL64;
		return SLURM_SUCCESS;
	}

	if (gres_id_shared(plugin_id)) {
		shared_gres = true;
		gres_per_bit = job_gres_ptr->gres_per_node;
	}
	if (gres_id_shared(plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	if (job_gres_ptr->type_name && !job_gres_ptr->type_name[0])
		xfree(job_gres_ptr->type_name);

	xfree(node_gres_ptr->gres_used);	/* Clear cache */
	if (job_gres_ptr->node_cnt == 0) {
		job_gres_ptr->node_cnt = node_cnt;
		if (job_gres_ptr->gres_bit_alloc) {
			error("gres/%s: job %u node_cnt==0 and gres_bit_alloc is set",
			      gres_name, job_id);
			xfree(job_gres_ptr->gres_bit_alloc);
		}
	}
	/*
	 * These next 2 checks were added long before job resizing was allowed.
	 * They are not errors as we need to keep the original size around for
	 * any steps that might still be out there with the larger size.  If the
	 * job was sized up the gres_plugin_job_merge() function handles the
	 * resize so we are set there.
	 */
	else if (job_gres_ptr->node_cnt < node_cnt) {
		debug2("gres/%s: job %u node_cnt is now larger than it was when allocated from %u to %d",
		       gres_name, job_id, job_gres_ptr->node_cnt, node_cnt);
		if (node_offset >= job_gres_ptr->node_cnt)
			return SLURM_ERROR;
	} else if (job_gres_ptr->node_cnt > node_cnt) {
		debug2("gres/%s: job %u node_cnt is now smaller than it was when allocated %u to %d",
		       gres_name, job_id, job_gres_ptr->node_cnt, node_cnt);
	}

	if (!job_gres_ptr->gres_bit_alloc) {
		job_gres_ptr->gres_bit_alloc = xcalloc(node_cnt,
						       sizeof(bitstr_t *));
	}
	if (!job_gres_ptr->gres_cnt_node_alloc) {
		job_gres_ptr->gres_cnt_node_alloc = xcalloc(node_cnt,
							    sizeof(uint64_t));
	}

	/*
	 * select/cons_tres pre-selects the resources and we just need to update
	 * the data structures to reflect the selected GRES.
	 */
	if (job_gres_ptr->total_node_cnt) {
		/* Resuming job */
		if (job_gres_ptr->gres_cnt_node_alloc[node_offset]) {
			gres_cnt = job_gres_ptr->
				gres_cnt_node_alloc[node_offset];
		} else if (job_gres_ptr->gres_bit_alloc[node_offset]) {
			gres_cnt = bit_set_count(
				job_gres_ptr->gres_bit_alloc[node_offset]);
			gres_cnt *= gres_per_bit;
			/* Using pre-selected GRES */
		} else if (job_gres_ptr->gres_cnt_node_select &&
			   job_gres_ptr->gres_cnt_node_select[node_index]) {
			gres_cnt = job_gres_ptr->
				gres_cnt_node_select[node_index];
		} else if (job_gres_ptr->gres_bit_select &&
			   job_gres_ptr->gres_bit_select[node_index]) {
			gres_cnt = bit_set_count(
				job_gres_ptr->gres_bit_select[node_index]);
			gres_cnt *= gres_per_bit;
		} else {
			error("gres/%s: job %u node %s no resources selected",
			      gres_name, job_id, node_name);
			return SLURM_ERROR;
		}
	} else {
		gres_cnt = job_gres_ptr->gres_per_node;
	}

	/*
	 * Check that sufficient resources exist on this node
	 */
	job_gres_ptr->gres_cnt_node_alloc[node_offset] = gres_cnt;
	i = node_gres_ptr->gres_cnt_alloc + gres_cnt;
	if (i > node_gres_ptr->gres_cnt_avail) {
		error("gres/%s: job %u node %s overallocated resources by %"
		      PRIu64", (%"PRIu64" > %"PRIu64")",
		      gres_name, job_id, node_name,
		      i - node_gres_ptr->gres_cnt_avail,
		      i, node_gres_ptr->gres_cnt_avail);
		/* proceed with request, give job what is available */
	}

	if (!node_offset && job_gres_ptr->gres_cnt_step_alloc) {
		uint64_t *tmp = xcalloc(job_gres_ptr->node_cnt,
					sizeof(uint64_t));
		memcpy(tmp, job_gres_ptr->gres_cnt_step_alloc,
		       sizeof(uint64_t) * MIN(node_cnt,
					      job_gres_ptr->node_cnt));
		xfree(job_gres_ptr->gres_cnt_step_alloc);
		job_gres_ptr->gres_cnt_step_alloc = tmp;
	}
	if (job_gres_ptr->gres_cnt_step_alloc == NULL) {
		job_gres_ptr->gres_cnt_step_alloc =
			xcalloc(job_gres_ptr->node_cnt, sizeof(uint64_t));
	}

	/*
	 * Select and/or allocate specific resources for this job.
	 */
	if (job_gres_ptr->gres_bit_alloc[node_offset]) {
		/*
		 * Restarted slurmctld with active job or resuming a suspended
		 * job. In any case, the resources already selected.
		 */
		if (node_gres_ptr->gres_bit_alloc == NULL) {
			node_gres_ptr->gres_bit_alloc =
				bit_copy(job_gres_ptr->
					 gres_bit_alloc[node_offset]);
			node_gres_ptr->gres_cnt_alloc +=
				bit_set_count(node_gres_ptr->gres_bit_alloc);
			node_gres_ptr->gres_cnt_alloc *= gres_per_bit;
		} else if (node_gres_ptr->gres_bit_alloc) {
			gres_cnt = (int64_t)MIN(
				bit_size(node_gres_ptr->gres_bit_alloc),
				bit_size(job_gres_ptr->
					 gres_bit_alloc[node_offset]));
			for (i = 0; i < gres_cnt; i++) {
				if (bit_test(job_gres_ptr->
					     gres_bit_alloc[node_offset], i) &&
				    (shared_gres ||
				     !bit_test(node_gres_ptr->gres_bit_alloc,
					       i))) {
					bit_set(node_gres_ptr->
						gres_bit_alloc,i);
					node_gres_ptr->gres_cnt_alloc +=
						gres_per_bit;
				}
			}
		}
	} else if (job_gres_ptr->total_node_cnt &&
		   job_gres_ptr->gres_bit_select &&
		   job_gres_ptr->gres_bit_select[node_index] &&
		   job_gres_ptr->gres_cnt_node_select) {
		/* Specific GRES already selected, update the node record */
		bool job_mod = false;
		sz1 = bit_size(job_gres_ptr->gres_bit_select[node_index]);
		sz2 = bit_size(node_gres_ptr->gres_bit_alloc);
		if (sz1 > sz2) {
			error("gres/%s: job %u node %s gres bitmap size bad (%d > %d)",
			      gres_name, job_id, node_name, sz1, sz2);
			job_gres_ptr->gres_bit_select[node_index] =
				bit_realloc(job_gres_ptr->
					    gres_bit_select[node_index], sz2);
			job_mod = true;
		} else if (sz1 < sz2) {
			error("gres/%s: job %u node %s gres bitmap size bad (%d < %d)",
			      gres_name, job_id, node_name, sz1, sz2);
			job_gres_ptr->gres_bit_select[node_index] =
				bit_realloc(job_gres_ptr->
					    gres_bit_select[node_index], sz2);
		}

		if (!shared_gres &&
		    bit_overlap_any(job_gres_ptr->gres_bit_select[node_index],
				    node_gres_ptr->gres_bit_alloc)) {
			error("gres/%s: job %u node %s gres bitmap overlap",
			      gres_name, job_id, node_name);
			bit_and_not(job_gres_ptr->gres_bit_select[node_index],
				    node_gres_ptr->gres_bit_alloc);
		}
		job_gres_ptr->gres_bit_alloc[node_offset] =
			bit_copy(job_gres_ptr->gres_bit_select[node_index]);
		job_gres_ptr->gres_cnt_node_alloc[node_offset] =
			job_gres_ptr->gres_cnt_node_select[node_index];
		if (!node_gres_ptr->gres_bit_alloc) {
			node_gres_ptr->gres_bit_alloc =
				bit_copy(job_gres_ptr->
					 gres_bit_alloc[node_offset]);
		} else {
			bit_or(node_gres_ptr->gres_bit_alloc,
			       job_gres_ptr->gres_bit_alloc[node_offset]);
		}
		if (job_mod) {
			node_gres_ptr->gres_cnt_alloc =
				bit_set_count(node_gres_ptr->gres_bit_alloc);
			node_gres_ptr->gres_cnt_alloc *= gres_per_bit;
		} else {
			node_gres_ptr->gres_cnt_alloc += gres_cnt;
		}
	} else if (node_gres_ptr->gres_bit_alloc) {
		int64_t gres_avail = node_gres_ptr->gres_cnt_avail;

		i = bit_size(node_gres_ptr->gres_bit_alloc);
		if (gres_id_shared(plugin_id))
			gres_avail = i;
		else if (i < gres_avail) {
			error("gres/%s: node %s gres bitmap size bad (%"PRIi64" < %"PRIi64")",
			      gres_name, node_name,
			      i, gres_avail);
			node_gres_ptr->gres_bit_alloc =
				bit_realloc(node_gres_ptr->gres_bit_alloc,
					    gres_avail);
		}

		job_gres_ptr->gres_bit_alloc[node_offset] =
			bit_alloc(gres_avail);

		if (core_bitmap)
			alloc_core_bitmap = bit_alloc(bit_size(core_bitmap));
		/* Pass 1: Allocate GRES overlapping all allocated cores */
		for (i=0; i<gres_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			if (!_cores_on_gres(core_bitmap, alloc_core_bitmap,
					    node_gres_ptr, i, job_gres_ptr))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc += gres_per_bit;
			gres_cnt -= gres_per_bit;
		}
		FREE_NULL_BITMAP(alloc_core_bitmap);
		/* Pass 2: Allocate GRES overlapping any allocated cores */
		for (i=0; i<gres_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			if (!_cores_on_gres(core_bitmap, NULL, node_gres_ptr, i,
					    job_gres_ptr))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc += gres_per_bit;
			gres_cnt -= gres_per_bit;
		}
		if (gres_cnt) {
			verbose("gres/%s topology sub-optimal for job %u",
				gres_name, job_id);
		}
		/* Pass 3: Allocate any available GRES */
		for (i=0; i<gres_avail && gres_cnt>0; i++) {
			if (bit_test(node_gres_ptr->gres_bit_alloc, i))
				continue;
			bit_set(node_gres_ptr->gres_bit_alloc, i);
			bit_set(job_gres_ptr->gres_bit_alloc[node_offset], i);
			node_gres_ptr->gres_cnt_alloc += gres_per_bit;
			gres_cnt -= gres_per_bit;
		}
	} else {
		node_gres_ptr->gres_cnt_alloc += gres_cnt;
	}

	if (job_gres_ptr->gres_bit_alloc[node_offset] &&
	    node_gres_ptr->topo_gres_bitmap &&
	    node_gres_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			if (job_gres_ptr->type_name &&
			    (!node_gres_ptr->topo_type_name[i] ||
			     (job_gres_ptr->type_id !=
			      node_gres_ptr->topo_type_id[i])))
				continue;
			if (use_busy_dev &&
			    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
				continue;
			sz1 = bit_size(
				job_gres_ptr->gres_bit_alloc[node_offset]);
			sz2 = bit_size(node_gres_ptr->topo_gres_bitmap[i]);

			if ((sz1 != sz2) && log_cnt_err) {
				if (gres_id_shared(plugin_id))
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
			gres_cnt = bit_overlap(job_gres_ptr->
					       gres_bit_alloc[node_offset],
					       node_gres_ptr->
					       topo_gres_bitmap[i]);
			gres_cnt *= gres_per_bit;
			node_gres_ptr->topo_gres_cnt_alloc[i] += gres_cnt;
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j] += gres_cnt;
				break;
			}
		}
		type_array_updated = true;
	} else if (job_gres_ptr->gres_bit_alloc[node_offset]) {
		int len;	/* length of the gres bitmap on this node */
		len = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
		if (!node_gres_ptr->topo_gres_cnt_alloc) {
			node_gres_ptr->topo_gres_cnt_alloc =
				xcalloc(len, sizeof(uint64_t));
		} else {
			len = MIN(len, node_gres_ptr->gres_cnt_config);
		}

		if ((node_gres_ptr->topo_cnt == 0) && shared_gres) {
			/*
			 * Need to add node topo arrays for slurmctld restart
			 * and job state recovery (with GRES counts per topo)
			 */
			node_gres_ptr->topo_cnt =
				bit_size(job_gres_ptr->
					 gres_bit_alloc[node_offset]);
			node_gres_ptr->topo_core_bitmap =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(bitstr_t *));
			node_gres_ptr->topo_gres_bitmap =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(bitstr_t *));
			node_gres_ptr->topo_gres_cnt_alloc =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(uint64_t));
			node_gres_ptr->topo_gres_cnt_avail =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(uint64_t));
			node_gres_ptr->topo_type_id =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(uint32_t));
			node_gres_ptr->topo_type_name =
				xcalloc(node_gres_ptr->topo_cnt,
					sizeof(char *));
			for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
				node_gres_ptr->topo_gres_bitmap[i] =
					bit_alloc(node_gres_ptr->topo_cnt);
				bit_set(node_gres_ptr->topo_gres_bitmap[i], i);
			}
		}

		for (i = 0; i < len; i++) {
			gres_cnt = 0;
			if (!bit_test(job_gres_ptr->
				      gres_bit_alloc[node_offset], i))
				continue;
			/*
			 * NOTE: Immediately after slurmctld restart and before
			 * the node's registration, the GRES type and topology
			 * information will not be available and we will be
			 * unable to update topo_gres_cnt_alloc or
			 * type_cnt_alloc. This results in some incorrect
			 * internal bookkeeping, but does not cause failures
			 * in terms of allocating GRES to jobs.
			 */
			for (j = 0; j < node_gres_ptr->topo_cnt; j++) {
				if (use_busy_dev &&
				    !node_gres_ptr->topo_gres_cnt_alloc[j])
					continue;
				if (node_gres_ptr->topo_gres_bitmap &&
				    node_gres_ptr->topo_gres_bitmap[j] &&
				    bit_test(node_gres_ptr->topo_gres_bitmap[j],
					     i)) {
					node_gres_ptr->topo_gres_cnt_alloc[i] +=
						gres_per_bit;
					gres_cnt += gres_per_bit;
				}
			}
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				node_gres_ptr->type_cnt_alloc[j] += gres_cnt;
				break;
			}
		}
		type_array_updated = true;
		if (job_gres_ptr->type_name && job_gres_ptr->type_name[0]) {
			/*
			 * We may not know how many GRES of this type will be
			 * available on this node, but need to track how many
			 * are allocated to this job from here to avoid
			 * underflows when this job is deallocated
			 */
			gres_add_type(job_gres_ptr->type_name, node_gres_ptr,
				      0);
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (job_gres_ptr->type_id !=
				    node_gres_ptr->type_id[j])
					continue;
				node_gres_ptr->type_cnt_alloc[j] +=
					job_gres_ptr->gres_per_node;
				break;
			}
		}
	}

	if (!type_array_updated && job_gres_ptr->type_name) {
		gres_cnt = job_gres_ptr->gres_per_node;
		for (j = 0; j < node_gres_ptr->type_cnt; j++) {
			int64_t k;
			if (job_gres_ptr->type_id !=
			    node_gres_ptr->type_id[j])
				continue;
			k = node_gres_ptr->type_cnt_avail[j] -
				node_gres_ptr->type_cnt_alloc[j];
			k = MIN(gres_cnt, k);
			node_gres_ptr->type_cnt_alloc[j] += k;
			gres_cnt -= k;
			if (gres_cnt == 0)
				break;
		}
	}

	return SLURM_SUCCESS;
}

static int _job_alloc_whole_node_internal(
	gres_key_t *job_search_key, gres_node_state_t *node_state_ptr,
	List job_gres_list, int node_cnt, int node_index, int node_offset,
	int type_index, uint32_t job_id, char *node_name,
	bitstr_t *core_bitmap, uint32_t user_id)
{
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_state_ptr;

	if (!(job_gres_ptr = list_find_first(job_gres_list,
					     gres_find_job_by_key,
					     job_search_key))) {
		error("%s: This should never happen, we couldn't find the gres %u:%u",
		      __func__,
		      job_search_key->plugin_id,
		      job_search_key->type_id);
		return SLURM_ERROR;
	}

	job_state_ptr = (gres_job_state_t *)job_gres_ptr->gres_data;

	/*
	 * As the amount of gres on each node could
	 * differ. We need to set the gres_per_node
	 * correctly here to avoid heterogeneous node
	 * issues.
	 */
	if (type_index != -1)
		job_state_ptr->gres_per_node =
			node_state_ptr->type_cnt_avail[type_index];
	else
		job_state_ptr->gres_per_node = node_state_ptr->gres_cnt_avail;

	return _job_alloc(job_state_ptr, node_state_ptr,
			  node_cnt, node_index, node_offset,
			  job_state_ptr->gres_name,
			  job_id, node_name, core_bitmap,
			  job_gres_ptr->plugin_id,
			  user_id);
}

static void _job_select_whole_node_internal(
	gres_key_t *job_search_key, gres_node_state_t *node_state_ptr,
	int type_inx, char *gres_name, List job_gres_list)
{
	gres_state_t *job_gres_ptr;
	gres_job_state_t *job_state_ptr;

	if (!(job_gres_ptr = list_find_first(job_gres_list,
					     gres_find_job_by_key,
					     job_search_key))) {
		job_state_ptr = xmalloc(sizeof(gres_job_state_t));

		job_gres_ptr = xmalloc(sizeof(gres_state_t));
		job_gres_ptr->plugin_id = job_search_key->plugin_id;
		job_gres_ptr->gres_data = job_state_ptr;
		job_state_ptr->gres_name = xstrdup(gres_name);
		if (type_inx != -1)
			job_state_ptr->type_name =
				xstrdup(node_state_ptr->type_name[type_inx]);
		job_state_ptr->type_id = job_search_key->type_id;

		list_append(job_gres_list, job_gres_ptr);
	} else
		job_state_ptr = job_gres_ptr->gres_data;

	/*
	 * Add the total_gres here but no count, that will be done after
	 * allocation.
	 */
	if (node_state_ptr->no_consume) {
		job_state_ptr->total_gres = NO_CONSUME_VAL64;
	} else if (type_inx != -1)
		job_state_ptr->total_gres +=
			node_state_ptr->type_cnt_avail[type_inx];
	else
		job_state_ptr->total_gres += node_state_ptr->gres_cnt_avail;
}

/*
 * Fill in job_gres_list with the total amount of GRES on a node.
 * OUT job_gres_list - This list will be destroyed and remade with all GRES on
 *                     node.
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_ctld_job_select_whole_node(
	List *job_gres_list, List node_gres_list,
	uint32_t job_id, char *node_name)
{
	ListIterator node_gres_iter;
	gres_state_t *node_gres_ptr;
	gres_node_state_t *node_state_ptr;

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
	while ((node_gres_ptr = list_next(node_gres_iter))) {
		char *gres_name;
		gres_key_t job_search_key;
		node_state_ptr = (gres_node_state_t *) node_gres_ptr->gres_data;

		/*
		 * Don't check for no_consume here, we need them added here and
		 * will filter them out in gres_plugin_job_alloc_whole_node()
		 */
		if (!node_state_ptr->gres_cnt_config)
			continue;

		if (!(gres_name = gres_get_name_from_id(
			      node_gres_ptr->plugin_id))) {
			error("%s: no plugin configured for data type %u for job %u and node %s",
			      __func__, node_gres_ptr->plugin_id, job_id,
			      node_name);
			/* A likely sign that GresPlugins has changed */
			continue;
		}

		job_search_key.plugin_id = node_gres_ptr->plugin_id;

		if (!node_state_ptr->type_cnt) {
			job_search_key.type_id = 0;
			_job_select_whole_node_internal(
				&job_search_key, node_state_ptr,
				-1, gres_name, *job_gres_list);
		} else {
			for (int j = 0; j < node_state_ptr->type_cnt; j++) {
				job_search_key.type_id = gres_plugin_build_id(
					node_state_ptr->type_name[j]);
				_job_select_whole_node_internal(
					&job_search_key, node_state_ptr,
					j, gres_name, *job_gres_list);
			}
		}
		xfree(gres_name);
	}
	list_iterator_destroy(node_gres_iter);

	return SLURM_SUCCESS;
}

/*
 * Select and allocate GRES to a job and update node and job GRES information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_index  - zero-origin global node index
 * IN node_offset - zero-origin index in job allocation to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * IN user_id     - job's user ID
 * RET SLURM_SUCCESS or error code
 */
extern int gres_ctld_job_alloc(List job_gres_list, List node_gres_list,
			       int node_cnt, int node_index, int node_offset,
			       uint32_t job_id, char *node_name,
			       bitstr_t *core_bitmap, uint32_t user_id)
{
	int rc = SLURM_ERROR, rc2;
	ListIterator job_gres_iter,  node_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		gres_job_state_t *job_state_ptr =
			(gres_job_state_t *) job_gres_ptr->gres_data;
		node_gres_iter = list_iterator_create(node_gres_list);
		while ((node_gres_ptr = list_next(node_gres_iter))) {
			if (job_gres_ptr->plugin_id == node_gres_ptr->plugin_id)
				break;
		}
		list_iterator_destroy(node_gres_iter);
		if (node_gres_ptr == NULL) {
			error("%s: job %u allocated gres/%s on node %s lacking that gres",
			      __func__, job_id, job_state_ptr->gres_name,
			      node_name);
			continue;
		}

		rc2 = _job_alloc(job_gres_ptr->gres_data,
				 node_gres_ptr->gres_data, node_cnt, node_index,
				 node_offset, job_state_ptr->gres_name,
				 job_id, node_name, core_bitmap,
				 job_gres_ptr->plugin_id, user_id);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(job_gres_iter);

	return rc;
}

/*
 * Select and allocate all GRES on a node to a job and update node and job GRES
 * information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_whole_node().
 * IN node_gres_list - node's gres_list built by
 *		       gres_plugin_node_config_validate()
 * IN node_cnt    - total number of nodes originally allocated to the job
 * IN node_index  - zero-origin global node index
 * IN node_offset - zero-origin index in job allocation to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN core_bitmap - cores allocated to this job on this node (NULL if not
 *                  available)
 * IN user_id     - job's user ID
 * RET SLURM_SUCCESS or error code
 */
extern int gres_ctld_job_alloc_whole_node(
	List job_gres_list, List node_gres_list,
	int node_cnt, int node_index, int node_offset,
	uint32_t job_id, char *node_name,
	bitstr_t *core_bitmap, uint32_t user_id)
{
	int rc = SLURM_ERROR, rc2;
	ListIterator node_gres_iter;
	gres_state_t *node_gres_ptr;
	gres_node_state_t *node_state_ptr;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	node_gres_iter = list_iterator_create(node_gres_list);
	while ((node_gres_ptr = list_next(node_gres_iter))) {
		gres_key_t job_search_key;
		node_state_ptr = (gres_node_state_t *) node_gres_ptr->gres_data;

		if (node_state_ptr->no_consume ||
		    !node_state_ptr->gres_cnt_config)
			continue;

		job_search_key.plugin_id = node_gres_ptr->plugin_id;

		if (!node_state_ptr->type_cnt) {
			job_search_key.type_id = 0;
			rc2 = _job_alloc_whole_node_internal(
				&job_search_key, node_state_ptr,
				job_gres_list, node_cnt, node_index,
				node_offset, -1, job_id, node_name,
				core_bitmap, user_id);
			if (rc2 != SLURM_SUCCESS)
				rc = rc2;
		} else {
			for (int j = 0; j < node_state_ptr->type_cnt; j++) {
				job_search_key.type_id = gres_plugin_build_id(
					node_state_ptr->type_name[j]);
				rc2 = _job_alloc_whole_node_internal(
					&job_search_key, node_state_ptr,
					job_gres_list, node_cnt, node_index,
					node_offset, j, job_id, node_name,
					core_bitmap, user_id);
				if (rc2 != SLURM_SUCCESS)
					rc = rc2;
			}
		}
	}
	list_iterator_destroy(node_gres_iter);

	return rc;
}

static int _job_dealloc(void *job_gres_data, void *node_gres_data,
			int node_offset, char *gres_name, uint32_t job_id,
			char *node_name, bool old_job, uint32_t plugin_id,
			uint32_t user_id, bool job_fini)
{
	int i, j, len, sz1, sz2;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bool type_array_updated = false;
	uint64_t gres_cnt = 0, k;
	uint64_t gres_per_bit = 1;

	/*
	 * Validate data structures. Either job_gres_data->node_cnt and
	 * job_gres_data->gres_bit_alloc are both set or both zero/NULL.
	 */
	xassert(node_offset >= 0);
	xassert(job_gres_ptr);
	xassert(node_gres_ptr);

	if (node_gres_ptr->no_consume)
		return SLURM_SUCCESS;

	if (job_gres_ptr->node_cnt <= node_offset) {
		error("gres/%s: job %u dealloc of node %s bad node_offset %d "
		      "count is %u", gres_name, job_id, node_name, node_offset,
		      job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}

	if (gres_id_shared(plugin_id))
		gres_per_bit = job_gres_ptr->gres_per_node;

	xfree(node_gres_ptr->gres_used);	/* Clear cache */
	if (node_gres_ptr->gres_bit_alloc && job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset]) {
		len = bit_size(job_gres_ptr->gres_bit_alloc[node_offset]);
		i   = bit_size(node_gres_ptr->gres_bit_alloc);
		if (i != len) {
			error("gres/%s: job %u and node %s bitmap sizes differ "
			      "(%d != %d)", gres_name, job_id, node_name, len,
			       i);
			len = MIN(len, i);
			/* proceed with request, make best effort */
		}
		for (i = 0; i < len; i++) {
			if (!bit_test(job_gres_ptr->gres_bit_alloc[node_offset],
				      i)) {
				continue;
			}
			bit_clear(node_gres_ptr->gres_bit_alloc, i);

			/*
			 * NOTE: Do not clear bit from
			 * job_gres_ptr->gres_bit_alloc[node_offset]
			 * since this may only be an emulated deallocate
			 */
			if (node_gres_ptr->gres_cnt_alloc >= gres_per_bit) {
				node_gres_ptr->gres_cnt_alloc -= gres_per_bit;
			} else {
				error("gres/%s: job %u dealloc node %s GRES count underflow (%"PRIu64" < %"PRIu64")",
				      gres_name, job_id, node_name,
				      node_gres_ptr->gres_cnt_alloc,
				      gres_per_bit);
				node_gres_ptr->gres_cnt_alloc = 0;
			}
		}
	} else if (job_gres_ptr->gres_cnt_node_alloc) {
		gres_cnt = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	} else {
		gres_cnt = job_gres_ptr->gres_per_node;
	}
	if (gres_cnt && (node_gres_ptr->gres_cnt_alloc >= gres_cnt))
		node_gres_ptr->gres_cnt_alloc -= gres_cnt;
	else if (gres_cnt) {
		error("gres/%s: job %u node %s GRES count underflow (%"PRIu64" < %"PRIu64")",
		      gres_name, job_id, node_name,
		      node_gres_ptr->gres_cnt_alloc, gres_cnt);
		node_gres_ptr->gres_cnt_alloc = 0;
	}

	if (job_gres_ptr->gres_bit_alloc &&
	    job_gres_ptr->gres_bit_alloc[node_offset] &&
	    node_gres_ptr->topo_gres_bitmap &&
	    node_gres_ptr->topo_gres_cnt_alloc) {
		for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
			sz1 = bit_size(
				job_gres_ptr->gres_bit_alloc[node_offset]);
			sz2 = bit_size(node_gres_ptr->topo_gres_bitmap[i]);
			if (sz1 != sz2)
				continue;
			gres_cnt = (uint64_t)bit_overlap(
				job_gres_ptr->gres_bit_alloc[node_offset],
				node_gres_ptr->topo_gres_bitmap[i]);
			gres_cnt *= gres_per_bit;
			if (node_gres_ptr->topo_gres_cnt_alloc[i] >= gres_cnt) {
				node_gres_ptr->topo_gres_cnt_alloc[i] -=
					gres_cnt;
			} else if (old_job) {
				node_gres_ptr->topo_gres_cnt_alloc[i] = 0;
			} else {
				error("gres/%s: job %u dealloc node %s topo gres count underflow "
				      "(%"PRIu64" %"PRIu64")",
				      gres_name, job_id, node_name,
				      node_gres_ptr->topo_gres_cnt_alloc[i],
				      gres_cnt);
				node_gres_ptr->topo_gres_cnt_alloc[i] = 0;
			}
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				if (node_gres_ptr->type_cnt_alloc[j] >=
				    gres_cnt) {
					node_gres_ptr->type_cnt_alloc[j] -=
						gres_cnt;
				} else if (old_job) {
					node_gres_ptr->type_cnt_alloc[j] = 0;
				} else {
					error("gres/%s: job %u dealloc node %s type %s gres count underflow "
					      "(%"PRIu64" %"PRIu64")",
					      gres_name, job_id, node_name,
					      node_gres_ptr->type_name[j],
					      node_gres_ptr->type_cnt_alloc[j],
					      gres_cnt);
					node_gres_ptr->type_cnt_alloc[j] = 0;
				}
			}
		}
		type_array_updated = true;
	} else if (job_gres_ptr->gres_bit_alloc &&
		   job_gres_ptr->gres_bit_alloc[node_offset] &&
		   node_gres_ptr->topo_gres_cnt_alloc) {
		/* Avoid crash if configuration inconsistent */
		len = MIN(node_gres_ptr->gres_cnt_config,
			  bit_size(job_gres_ptr->
				   gres_bit_alloc[node_offset]));
		for (i = 0; i < len; i++) {
			if (!bit_test(job_gres_ptr->
				      gres_bit_alloc[node_offset], i) ||
			    !node_gres_ptr->topo_gres_cnt_alloc[i])
				continue;
			if (node_gres_ptr->topo_gres_cnt_alloc[i] >=
			    gres_per_bit) {
				node_gres_ptr->topo_gres_cnt_alloc[i] -=
								gres_per_bit;
			} else {
				error("gres/%s: job %u dealloc node %s "
				      "topo_gres_cnt_alloc[%d] count underflow "
				      "(%"PRIu64" %"PRIu64")",
				      gres_name, job_id, node_name, i,
				      node_gres_ptr->topo_gres_cnt_alloc[i],
				      gres_per_bit);
				node_gres_ptr->topo_gres_cnt_alloc[i] = 0;
			}
			if ((node_gres_ptr->type_cnt == 0) ||
			    (node_gres_ptr->topo_type_name == NULL) ||
			    (node_gres_ptr->topo_type_name[i] == NULL))
				continue;
			for (j = 0; j < node_gres_ptr->type_cnt; j++) {
				if (!node_gres_ptr->type_name[j] ||
				    (node_gres_ptr->topo_type_id[i] !=
				     node_gres_ptr->type_id[j]))
					continue;
				if (node_gres_ptr->type_cnt_alloc[j] >=
				    gres_per_bit) {
					node_gres_ptr->type_cnt_alloc[j] -=
								gres_per_bit;
				} else {
					error("gres/%s: job %u dealloc node %s "
					      "type %s type_cnt_alloc count underflow "
					      "(%"PRIu64" %"PRIu64")",
					      gres_name, job_id, node_name,
					      node_gres_ptr->type_name[j],
					      node_gres_ptr->type_cnt_alloc[j],
					      gres_per_bit);
					node_gres_ptr->type_cnt_alloc[j] = 0;
				}
 			}
		}
		type_array_updated = true;
	}

	if (!type_array_updated && job_gres_ptr->type_name) {
		gres_cnt = job_gres_ptr->gres_per_node;
		for (j = 0; j < node_gres_ptr->type_cnt; j++) {
			if (job_gres_ptr->type_id !=
			    node_gres_ptr->type_id[j])
				continue;
			k = MIN(gres_cnt, node_gres_ptr->type_cnt_alloc[j]);
			node_gres_ptr->type_cnt_alloc[j] -= k;
			gres_cnt -= k;
			if (gres_cnt == 0)
				break;
		}
 	}

	return SLURM_SUCCESS;
}

/*
 * Deallocate resource from a job and update node and job gres information
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *		gres_plugin_node_config_validate()
 * IN node_offset - zero-origin index to the node of interest
 * IN job_id      - job's ID (for logging)
 * IN node_name   - name of the node (for logging)
 * IN old_job     - true if job started before last slurmctld reboot.
 *		    Immediately after slurmctld restart and before the node's
 *		    registration, the GRES type and topology. This results in
 *		    some incorrect internal bookkeeping, but does not cause
 *		    failures in terms of allocating GRES to jobs.
 * IN user_id     - job's user ID
 * IN: job_fini   - job fully terminating on this node (not just a test)
 * RET SLURM_SUCCESS or error code
 */
extern int gres_ctld_job_dealloc(List job_gres_list, List node_gres_list,
				 int node_offset, uint32_t job_id,
				 char *node_name, bool old_job,
				 uint32_t user_id, bool job_fini)
{
	int rc = SLURM_SUCCESS, rc2;
	ListIterator job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;
	char *gres_name = NULL;

	if (job_gres_list == NULL)
		return SLURM_SUCCESS;
	if (node_gres_list == NULL) {
		error("%s: job %u has gres specification while node %s has none",
		      __func__, job_id, node_name);
		return SLURM_ERROR;
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		if (!(gres_name = gres_get_name_from_id(
			      job_gres_ptr->plugin_id))) {
			error("%s: no plugin configured for data type %u for job %u and node %s",
			      __func__, job_gres_ptr->plugin_id, job_id,
			      node_name);
			/* A likely sign that GresPlugins has changed */
			gres_name = "UNKNOWN";
		}

		node_gres_ptr = list_find_first(node_gres_list, gres_find_id,
						&job_gres_ptr->plugin_id);

		if (node_gres_ptr == NULL) {
			error("%s: node %s lacks gres/%s for job %u", __func__,
			      node_name, gres_name , job_id);
			continue;
		}

		rc2 = _job_dealloc(job_gres_ptr->gres_data,
				   node_gres_ptr->gres_data, node_offset,
				   gres_name, job_id, node_name, old_job,
				   job_gres_ptr->plugin_id, user_id, job_fini);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(job_gres_iter);

	return rc;
}
static int _step_alloc(void *step_gres_data, void *job_gres_data,
		       uint32_t plugin_id, int node_offset,
		       bool first_step_node,
		       slurm_step_id_t *step_id,
		       uint16_t tasks_on_node, uint32_t rem_nodes)
{
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *)  job_gres_data;
	gres_step_state_t *step_gres_ptr = (gres_step_state_t *) step_gres_data;
	uint64_t gres_needed, gres_avail, max_gres = 0;
	bitstr_t *gres_bit_alloc;
	int i, len;

	xassert(job_gres_ptr);
	xassert(step_gres_ptr);

	if (job_gres_ptr->node_cnt == 0)	/* no_consume */
		return SLURM_SUCCESS;

	if (node_offset >= job_gres_ptr->node_cnt) {
		error("gres/%s: %s for %ps, node offset invalid (%d >= %u)",
		      job_gres_ptr->gres_name, __func__, step_id, node_offset,
		      job_gres_ptr->node_cnt);
		return SLURM_ERROR;
	}

	if (first_step_node)
		step_gres_ptr->total_gres = 0;
	if (step_gres_ptr->gres_per_node) {
		gres_needed = step_gres_ptr->gres_per_node;
	} else if (step_gres_ptr->gres_per_task) {
		gres_needed = step_gres_ptr->gres_per_task * tasks_on_node;
	} else if (step_gres_ptr->gres_per_step && (rem_nodes == 1)) {
		gres_needed = step_gres_ptr->gres_per_step -
			      step_gres_ptr->total_gres;
	} else if (step_gres_ptr->gres_per_step) {
		/* Leave at least one GRES per remaining node */
		max_gres = step_gres_ptr->gres_per_step -
			   step_gres_ptr->total_gres - (rem_nodes - 1);
		gres_needed = 1;
	} else {
		/*
		 * No explicit step GRES specification.
		 * Note that gres_per_socket is not supported for steps
		 */
		gres_needed = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	}
	if (step_gres_ptr->node_cnt == 0)
		step_gres_ptr->node_cnt = job_gres_ptr->node_cnt;
	if (!step_gres_ptr->gres_cnt_node_alloc) {
		step_gres_ptr->gres_cnt_node_alloc =
			xcalloc(step_gres_ptr->node_cnt, sizeof(uint64_t));
	}

	if (job_gres_ptr->gres_cnt_node_alloc &&
	    job_gres_ptr->gres_cnt_node_alloc[node_offset])
		gres_avail = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	else if (job_gres_ptr->gres_bit_select &&
		 job_gres_ptr->gres_bit_select[node_offset])
		gres_avail = bit_set_count(
				job_gres_ptr->gres_bit_select[node_offset]);
	else if (job_gres_ptr->gres_cnt_node_alloc)
		gres_avail = job_gres_ptr->gres_cnt_node_alloc[node_offset];
	else
		gres_avail = job_gres_ptr->gres_per_node;
	if (gres_needed > gres_avail) {
		error("gres/%s: %s for %ps, step's > job's "
		      "for node %d (%"PRIu64" > %"PRIu64")",
		      job_gres_ptr->gres_name, __func__,
		      step_id, node_offset, gres_needed, gres_avail);
		return SLURM_ERROR;
	}

	if (!job_gres_ptr->gres_cnt_step_alloc) {
		job_gres_ptr->gres_cnt_step_alloc =
			xcalloc(job_gres_ptr->node_cnt, sizeof(uint64_t));
	}

	if (gres_needed >
	    (gres_avail - job_gres_ptr->gres_cnt_step_alloc[node_offset])) {
		error("gres/%s: %s for %ps, step's > job's "
		      "remaining for node %d (%"PRIu64" > "
		      "(%"PRIu64" - %"PRIu64"))",
		      job_gres_ptr->gres_name, __func__,
		      step_id, node_offset, gres_needed, gres_avail,
		      job_gres_ptr->gres_cnt_step_alloc[node_offset]);
		return SLURM_ERROR;
	}
	gres_avail -= job_gres_ptr->gres_cnt_step_alloc[node_offset];
	if (max_gres)
		gres_needed = MIN(gres_avail, max_gres);

	if (step_gres_ptr->gres_cnt_node_alloc &&
	    (node_offset < step_gres_ptr->node_cnt))
		step_gres_ptr->gres_cnt_node_alloc[node_offset] = gres_needed;
	step_gres_ptr->total_gres += gres_needed;

	if (step_gres_ptr->node_in_use == NULL) {
		step_gres_ptr->node_in_use = bit_alloc(job_gres_ptr->node_cnt);
	}
	bit_set(step_gres_ptr->node_in_use, node_offset);
	job_gres_ptr->gres_cnt_step_alloc[node_offset] += gres_needed;

	if ((job_gres_ptr->gres_bit_alloc == NULL) ||
	    (job_gres_ptr->gres_bit_alloc[node_offset] == NULL)) {
		debug3("gres/%s: %s gres_bit_alloc for %ps is NULL",
		       job_gres_ptr->gres_name, __func__, step_id);
		return SLURM_SUCCESS;
	}

	gres_bit_alloc = bit_copy(job_gres_ptr->gres_bit_alloc[node_offset]);
	len = bit_size(gres_bit_alloc);
	if (gres_id_shared(plugin_id)) {
		for (i = 0; i < len; i++) {
			if (gres_needed > 0) {
				if (bit_test(gres_bit_alloc, i))
					gres_needed = 0;
			} else {
				bit_clear(gres_bit_alloc, i);
			}
		}
	} else {
		if (job_gres_ptr->gres_bit_step_alloc &&
		    job_gres_ptr->gres_bit_step_alloc[node_offset]) {
			bit_and_not(gres_bit_alloc,
				job_gres_ptr->gres_bit_step_alloc[node_offset]);
		}
		for (i = 0; i < len; i++) {
			if (gres_needed > 0) {
				if (bit_test(gres_bit_alloc, i))
					gres_needed--;
			} else {
				bit_clear(gres_bit_alloc, i);
			}
		}
	}
	if (gres_needed) {
		error("gres/%s: %s %ps oversubscribed resources on node %d",
		      job_gres_ptr->gres_name, __func__, step_id, node_offset);
	}

	if (job_gres_ptr->gres_bit_step_alloc == NULL) {
		job_gres_ptr->gres_bit_step_alloc =
			xcalloc(job_gres_ptr->node_cnt, sizeof(bitstr_t *));
	}
	if (job_gres_ptr->gres_bit_step_alloc[node_offset]) {
		bit_or(job_gres_ptr->gres_bit_step_alloc[node_offset],
		       gres_bit_alloc);
	} else {
		job_gres_ptr->gres_bit_step_alloc[node_offset] =
			bit_copy(gres_bit_alloc);
	}
	if (step_gres_ptr->gres_bit_alloc == NULL) {
		step_gres_ptr->gres_bit_alloc = xcalloc(job_gres_ptr->node_cnt,
							sizeof(bitstr_t *));
	}
	if (step_gres_ptr->gres_bit_alloc[node_offset]) {
		error("gres/%s: %s %ps bit_alloc already exists",
		      job_gres_ptr->gres_name, __func__, step_id);
		bit_or(step_gres_ptr->gres_bit_alloc[node_offset],
		       gres_bit_alloc);
		FREE_NULL_BITMAP(gres_bit_alloc);
	} else {
		step_gres_ptr->gres_bit_alloc[node_offset] = gres_bit_alloc;
	}

	return SLURM_SUCCESS;
}

/*
 * Allocate resource to a step and update job and step gres information
 * IN step_gres_list - step's gres_list built by
 *		gres_plugin_step_state_validate()
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_offset - job's zero-origin index to the node of interest
 * IN first_step_node - true if this is the first node in the step's allocation
 * IN tasks_on_node - number of tasks to be launched on this node
 * IN rem_nodes - desired additional node count to allocate, including this node
 * IN job_id, step_id - ID of the step being allocated.
 * RET SLURM_SUCCESS or error code
 */
extern int gres_ctld_step_alloc(List step_gres_list, List job_gres_list,
				int node_offset, bool first_step_node,
				uint16_t tasks_on_node, uint32_t rem_nodes,
				uint32_t job_id, uint32_t step_id)
{
	int rc = SLURM_SUCCESS, rc2;
	ListIterator step_gres_iter;
	gres_state_t *step_gres_ptr, *job_gres_ptr;
	slurm_step_id_t tmp_step_id;

	if (step_gres_list == NULL)
		return SLURM_SUCCESS;
	if (job_gres_list == NULL) {
		error("%s: step allocates GRES, but job %u has none",
		      __func__, job_id);
		return SLURM_ERROR;
	}

	tmp_step_id.job_id = job_id;
	tmp_step_id.step_het_comp = NO_VAL;
	tmp_step_id.step_id = step_id;

	step_gres_iter = list_iterator_create(step_gres_list);
	while ((step_gres_ptr = list_next(step_gres_iter))) {
		gres_step_state_t *step_data_ptr =
			(gres_step_state_t *) step_gres_ptr->gres_data;
		gres_key_t job_search_key;
		job_search_key.plugin_id = step_gres_ptr->plugin_id;
		if (step_data_ptr->type_name)
			job_search_key.type_id = step_data_ptr->type_id;
		else
			job_search_key.type_id = NO_VAL;

		job_search_key.node_offset = node_offset;
		if (!(job_gres_ptr = list_find_first(
			      job_gres_list,
			      gres_find_job_by_key_with_cnt,
			      &job_search_key))) {
			/* job lack resources required by the step */
			rc = ESLURM_INVALID_GRES;
			break;
		}

		rc2 = _step_alloc(step_data_ptr,
				  job_gres_ptr->gres_data,
				  step_gres_ptr->plugin_id, node_offset,
				  first_step_node, &tmp_step_id, tasks_on_node,
				  rem_nodes);
		if (rc2 != SLURM_SUCCESS)
			rc = rc2;
	}
	list_iterator_destroy(step_gres_iter);

	return rc;
}
