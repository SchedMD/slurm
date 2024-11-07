/*****************************************************************************\
 *  oracle.c - Infrastructure for the bf_topopt/"oracle" subsystem
 *
 *  The oracle() function controls job start delays based on
 *  fragmentation costs, optimizing job placement for efficiency.
 *
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

#include "src/interfaces/topology.h"

#include "backfill.h"
#include "oracle.h"

int bf_topopt_iterations;

static bf_slot_t *slots;
int used_slots;

static int _get_bitmap_from_nspace(node_space_map_t *node_space,
				   uint32_t start_time, bitstr_t *out_bitmap,
				   uint32_t *fragmentation)
{
	int j = 0;
	while (true) {
		if ((node_space[j].end_time > start_time) &&
		    (node_space[j].begin_time <= start_time)) {
			bit_copybits(out_bitmap,
				     node_space[j].avail_bitmap);
			*fragmentation = node_space[j].fragmentation;

			return SLURM_SUCCESS;
		}

		if ((j = node_space[j].next) == 0)
			return SLURM_ERROR;
	}
}

static void _add_slot(job_record_t *job_ptr, bitstr_t *job_bitmap,
		      uint32_t time_limit, uint32_t boot_time,
		      node_space_map_t *node_space)
{
	bf_slot_t *new_slot;
	uint32_t previous_cluster_score;
	int rc;

	if (used_slots >= bf_topopt_iterations)
		return;

	new_slot = &(slots[used_slots]);


	rc = _get_bitmap_from_nspace(node_space, job_ptr->start_time,
				     new_slot->cluster_bitmap,
				     &previous_cluster_score);
	if (rc == SLURM_SUCCESS) {
		bit_and_not(new_slot->cluster_bitmap, new_slot->job_bitmap);
		new_slot->cluster_score =
			topology_g_get_fragmentation(new_slot->cluster_bitmap);

		COPY_BITMAP(new_slot->job_bitmap, job_bitmap);

		COPY_BITMAP(new_slot->job_mask, job_bitmap);

		if (IS_JOB_WHOLE_TOPO(job_ptr))
			topology_g_whole_topo(new_slot->job_mask);

		bit_not(new_slot->job_mask);

		new_slot->job_score =
			topology_g_get_fragmentation(new_slot->job_mask);
		new_slot->start = job_ptr->start_time;
		new_slot->boot_time = boot_time;
		new_slot->time_limit = time_limit;

		log_flag(BACKFILL, "%pJ add slot:%d start_time:%ld previous_cluster_score:%u cluster_score:%u job_score:%u",
			 job_ptr, used_slots, new_slot->start,
			 previous_cluster_score, new_slot->cluster_score,
			 new_slot->job_score);

		(used_slots)++;
	}

	return;
}

void init_oracle(void)
{
	slots = xcalloc(bf_topopt_iterations, sizeof(bf_slot_t));
	for (int i = 0; i < bf_topopt_iterations; i++) {
		slots[i].job_bitmap = bit_alloc(node_record_count);
		slots[i].job_mask = bit_alloc(node_record_count);
		slots[i].cluster_bitmap = bit_alloc(node_record_count);
	}
}

void fini_oracle(void)
{
	for (int i = 0; i < bf_topopt_iterations; i++) {
		FREE_NULL_BITMAP(slots[i].job_bitmap);
		FREE_NULL_BITMAP(slots[i].job_mask);
		FREE_NULL_BITMAP(slots[i].cluster_bitmap);
	}
	xfree(slots);
}

/*
 * Select the "best" slot for given job from those available
 * IN/OUT job_ptr - pointer to job being considered
 * IN/OUT job_bitmap - map of nodes being considered for allocation on input,
 * 		       map of nodes choose to allocation on output
 * IN later_start
 * IN/OUT time_limit - time_limit of job
 * IN/OUT boot_time - boot_time of job
 * IN node_space
 * RET true - When we should check the later start of the job
 * 	false if we want to start/plan.
 */

bool oracle(job_record_t *job_ptr, bitstr_t *job_bitmap, time_t later_start,
	    uint32_t *time_limit, uint32_t *boot_time,
	    node_space_map_t *node_space)
{
	/*
	 * Alwasys if posible add a new slot to slots array
	 */
	if (used_slots < bf_topopt_iterations)
		_add_slot(job_ptr, job_bitmap, *time_limit, *boot_time,
			  node_space);

	/*
	 * The code below is only an example implementation
	 */

	/*
	 * Check later if later_start set and we have space in slots array
	 */
	if (later_start && used_slots < bf_topopt_iterations)
		return true;

	if (used_slots > 0) {
		int best_slot = 0;

		/*
		 * Find slot with minimal job_score
		 */
		for (int i = 1; i < used_slots; i++)
			if (slots[best_slot].job_score > slots[i].job_score)
				best_slot = i;

		/*
		 * Set start_time and job_bitmap acording to the 'best' slot
		 */
		job_ptr->start_time = slots[best_slot].start;
		bit_copybits(job_bitmap, slots[best_slot].job_bitmap);
		*time_limit = slots[best_slot].time_limit;
		*boot_time = slots[best_slot].boot_time;
		log_flag(BACKFILL, "%pJ use:%u start_time: %ld",
			 job_ptr, best_slot, job_ptr->start_time);
	}

	return false;
}
