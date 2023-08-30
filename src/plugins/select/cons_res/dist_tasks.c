/*****************************************************************************\
 *  dist_tasks - Assign task count to {socket,core,thread} or CPU
 *               resources
 *****************************************************************************
 *  Copyright (C) 2006-2008 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *  Portions copyright (C) 2012 Bull
 *  Written by Martin Perry <martin.perry@bull.com>
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

#include "select_cons_res.h"
#include "dist_tasks.h"

/* dist_tasks_compute_c_b - compute the number of tasks on each
 * of the node for the cyclic and block distribution. We need to do
 * this in the case of consumable resources so that we have an exact
 * count for the needed hardware resources which will be used later to
 * update the different used resources per node structures.
 *
 * The most common case is when we have more resources than needed. In
 * that case we just "take" what we need and "release" the remaining
 * resources for other jobs. In the case where we oversubscribe the
 * CPUs/Logical processors resources we keep the initial set of
 * resources.
 *
 * IN/OUT job_ptr - pointer to job being scheduled. The per-node
 *                  job_res->cpus array is recomputed here.
 *
 */
extern int dist_tasks_compute_c_b(job_record_t *job_ptr,
				  uint32_t *gres_task_limit,
				  uint32_t *gres_min_cpus)
{
	bool over_subscribe = false;
	uint32_t n, i, tid, maxtasks, l;
	uint16_t *avail_cpus;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bool log_over_subscribe = true;

	if (!job_res || !job_res->cpus || !job_res->nhosts) {
		error("cons_res: %s invalid allocation for %pJ",
		      __func__, job_ptr);
		return SLURM_ERROR;
	}

	maxtasks = job_res->ncpus;
	avail_cpus = job_res->cpus;
	job_res->cpus = xmalloc(job_res->nhosts * sizeof(uint16_t));

	/* ncpus is already set the number of tasks if overcommit is used */
	if (!job_ptr->details->overcommit &&
	    (job_ptr->details->cpus_per_task > 1)) {
		if (job_ptr->details->ntasks_per_node == 0) {
			maxtasks = maxtasks / job_ptr->details->cpus_per_task;
		} else {
			maxtasks = job_ptr->details->ntasks_per_node *
				   job_res->nhosts;
		}
	}

	/*
	 * Safe guard if the user didn't specified a lower number of
	 * CPUs than cpus_per_task or didn't specify the number.
	 */
	if (!maxtasks) {
		error("%s: request was for 0 tasks, setting to 1", __func__);
		maxtasks = 1;
	}
	if (job_ptr->details->cpus_per_task == 0)
		job_ptr->details->cpus_per_task = 1;
	if (job_ptr->details->overcommit)
		log_over_subscribe = false;
	for (tid = 0, i = job_ptr->details->cpus_per_task ; (tid < maxtasks);
	     i += job_ptr->details->cpus_per_task ) { /* cycle counter */
		bool space_remaining = false;
		if (over_subscribe && log_over_subscribe) {
			/*
			 * 'over_subscribe' is a relief valve that guards
			 * against an infinite loop, and it *should* never
			 * come into play because maxtasks should never be
			 * greater than the total number of available cpus
			 */
			error("cons_res: %s oversubscribe for %pJ",
			      __func__, job_ptr);
			log_over_subscribe = false	/* Log once per job */;
		}
		for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
			if ((i <= avail_cpus[n]) || over_subscribe) {
				tid++;
				for (l = 0; l < job_ptr->details->cpus_per_task;
				     l++) {
					if (job_res->cpus[n] < avail_cpus[n])
						job_res->cpus[n]++;
				}
				if ((i + 1) <= avail_cpus[n])
					space_remaining = true;
			}
		}
		if (!space_remaining) {
			over_subscribe = true;
		}
	}
	xfree(avail_cpus);
	return SLURM_SUCCESS;
}
