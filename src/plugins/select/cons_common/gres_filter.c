/*****************************************************************************\
 *  gres_filter.c - filters used in the select plugin
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

#include "src/common/slurm_xlator.h"

#include "gres_filter.h"

static uint32_t mps_plugin_id = NO_VAL;

static void _job_core_filter(void *job_gres_data, void *node_gres_data,
			     bool use_total_gres, bitstr_t *core_bitmap,
			     int core_start_bit, int core_end_bit,
			     char *node_name,
			     uint32_t plugin_id)
{
	int i, j, core_ctld;
	gres_job_state_t  *job_gres_ptr  = (gres_job_state_t *) job_gres_data;
	gres_node_state_t *node_gres_ptr = (gres_node_state_t *) node_gres_data;
	bitstr_t *avail_core_bitmap = NULL;
	bool use_busy_dev = false;

	if (!node_gres_ptr->topo_cnt || !core_bitmap ||	/* No topology info */
	    !job_gres_ptr->gres_per_node)		/* No job GRES */
		return;

	if (mps_plugin_id == NO_VAL)
		mps_plugin_id = gres_plugin_build_id("mps");

	if (!use_total_gres &&
	    (plugin_id == mps_plugin_id) &&
	    (node_gres_ptr->gres_cnt_alloc != 0)) {
		/* We must use the ONE already active GRES of this type */
		use_busy_dev = true;
	}

	/* Determine which specific cores can be used */
	avail_core_bitmap = bit_copy(core_bitmap);
	bit_nclear(avail_core_bitmap, core_start_bit, core_end_bit);
	for (i = 0; i < node_gres_ptr->topo_cnt; i++) {
		if (node_gres_ptr->topo_gres_cnt_avail[i] == 0)
			continue;
		if (!use_total_gres &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] >=
		     node_gres_ptr->topo_gres_cnt_avail[i]))
			continue;
		if (use_busy_dev &&
		    (node_gres_ptr->topo_gres_cnt_alloc[i] == 0))
			continue;
		if (job_gres_ptr->type_name &&
		    (!node_gres_ptr->topo_type_name[i] ||
		     (job_gres_ptr->type_id != node_gres_ptr->topo_type_id[i])))
			continue;
		if (!node_gres_ptr->topo_core_bitmap[i]) {
			FREE_NULL_BITMAP(avail_core_bitmap);	/* No filter */
			return;
		}
		core_ctld = core_end_bit - core_start_bit + 1;
		gres_validate_node_cores(node_gres_ptr, core_ctld, node_name);
		core_ctld = bit_size(node_gres_ptr->topo_core_bitmap[i]);
		for (j = 0; j < core_ctld; j++) {
			if (bit_test(node_gres_ptr->topo_core_bitmap[i], j)) {
				bit_set(avail_core_bitmap, core_start_bit + j);
			}
		}
	}
	bit_and(core_bitmap, avail_core_bitmap);
	FREE_NULL_BITMAP(avail_core_bitmap);
}

/*
 * Clear the core_bitmap for cores which are not usable by this job (i.e. for
 *	cores which are already bound to other jobs or lack GRES)
 * IN job_gres_list - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by
 *                     gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all GRES resources as available,
 *		       and none are commited to running jobs
 * IN/OUT core_bitmap - Identification of available cores
 *                      (NULL if no restriction)
 * IN core_start_bit - index into core_bitmap for this node's first cores
 * IN core_end_bit - index into core_bitmap for this node's last cores
 */
extern void gres_filter_cons_res(List job_gres_list, List node_gres_list,
				 bool use_total_gres,
				 bitstr_t *core_bitmap,
				 int core_start_bit, int core_end_bit,
				 char *node_name)
{
	ListIterator  job_gres_iter;
	gres_state_t *job_gres_ptr, *node_gres_ptr;

	if ((job_gres_list == NULL) || (core_bitmap == NULL))
		return;
	if (node_gres_list == NULL) {
		bit_nclear(core_bitmap, core_start_bit, core_end_bit);
		return;
	}

	job_gres_iter = list_iterator_create(job_gres_list);
	while ((job_gres_ptr = (gres_state_t *) list_next(job_gres_iter))) {
		node_gres_ptr = list_find_first(node_gres_list, gres_find_id,
		                                &job_gres_ptr->plugin_id);
		if (node_gres_ptr == NULL) {
			/* node lack resources required by the job */
			bit_nclear(core_bitmap, core_start_bit, core_end_bit);
			break;
		}

		_job_core_filter(job_gres_ptr->gres_data,
				 node_gres_ptr->gres_data,
				 use_total_gres, core_bitmap,
				 core_start_bit, core_end_bit,
				 node_name,
				 job_gres_ptr->plugin_id);
	}
	list_iterator_destroy(job_gres_iter);

	return;
}

