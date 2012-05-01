/*****************************************************************************\
 *  dist_tasks.c - Assign task count to {socket,core,thread} or CPU
 *                 resources
 *****************************************************************************
 *  Copyright (C) 2006-2008 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "select_serial.h"
#include "dist_tasks.h"

/* _compute_task_c_b_task_dist - compute the number of tasks on each
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
static int _compute_c_b_task_dist(struct job_record *job_ptr)
{
	job_resources_t *job_res = job_ptr->job_resrcs;

	if (!job_res || !job_res->cpus) {
		error("cons_res: _compute_c_b_task_dist given NULL job_res");
		return SLURM_ERROR;
	}
	if (job_res->nhosts != 1) {
		error("cons_res: _compute_c_b_task_dist given nhosts==%u",
		      job_res->nhosts);
		return SLURM_ERROR;
	}

	xfree(job_res->cpus);
	job_res->cpus = xmalloc(sizeof(uint16_t));
	job_res->cpus[0] = 1;

	return SLURM_SUCCESS;
}

/* sync up core bitmap with new CPU count using a best-fit approach
 * on the available sockets
 *
 * The CPU array contains the distribution of CPUs, which can include
 * virtual CPUs (hyperthreads)
 */
static void _block_sync_core_bitmap(struct job_record *job_ptr,
				    const uint16_t cr_type)
{
	uint32_t c, s, i, j, n, size, csize, core_cnt;
	uint16_t cpus, num_bits, vpus = 1;
	job_resources_t *job_res = job_ptr->job_resrcs;
	bool alloc_cores = false, alloc_sockets = false;
	uint16_t ntasks_per_core = 0xffff;
	int* sockets_cpu_cnt;
	bool* sockets_used;
	uint16_t sockets_nb;
	uint16_t ncores_nb;
	uint16_t nsockets_nb;
	uint16_t req_cpus,best_fit_cpus = 0;
	uint32_t best_fit_location = 0;
	bool sufficient,best_fit_sufficient;

	if (!job_res)
		return;

	size  = bit_size(job_res->node_bitmap);
	csize = bit_size(job_res->core_bitmap);

	sockets_nb  = select_node_record[0].sockets;
	sockets_cpu_cnt = xmalloc(sockets_nb * sizeof(int));
	sockets_used = xmalloc(sockets_nb * sizeof(bool));

	for (c = 0, i = 0, n = 0; n < size; n++) {

		if (bit_test(job_res->node_bitmap, n) == 0)
			continue;

		core_cnt = 0;
		ncores_nb = select_node_record[n].cores;
		nsockets_nb = select_node_record[n].sockets;
		num_bits =  nsockets_nb * ncores_nb;

		if ((c + num_bits) > csize)
			fatal ("cons_res: _block_sync_core_bitmap index error");

		cpus  = job_res->cpus[i];
		vpus  = MIN(select_node_record[n].vpus, ntasks_per_core);

		if ( nsockets_nb > sockets_nb) {
			sockets_nb = nsockets_nb;
			xrealloc(sockets_cpu_cnt, sockets_nb * sizeof(int));
			xrealloc(sockets_used,sockets_nb * sizeof(bool));
		}

		/* count cores provided by each socket */
		for (s = 0; s < nsockets_nb; s++) {
			sockets_cpu_cnt[s]=0;
			sockets_used[s]=false;
			for ( j = c + (s * ncores_nb) ;
			      j < c + ((s+1) * ncores_nb) ;
			      j++ ) {
				if ( bit_test(job_res->core_bitmap,j) )
					sockets_cpu_cnt[s]++;
			}
		}

		/* select cores in the sockets using a best-fit approach */
		while( cpus > 0 ) {

			best_fit_cpus = 0;
			best_fit_sufficient = false;

			/* compute still required cores on the node */
			req_cpus = cpus / vpus;
			if ( cpus % vpus )
				req_cpus++;

			/* search for the best socket, */
			/* starting from the last one to let more room */
			/* in the first one for system usage */
			for ( s = nsockets_nb - 1 ; (int) s >= (int) 0 ; s-- ) {
				sufficient = sockets_cpu_cnt[s] >= req_cpus ;
				if ( (best_fit_cpus == 0) ||
				     (sufficient && !best_fit_sufficient ) ||
				     (sufficient && (sockets_cpu_cnt[s] <
						     best_fit_cpus)) ||
				     (!sufficient && (sockets_cpu_cnt[s] >
						      best_fit_cpus)) ) {
					best_fit_cpus = sockets_cpu_cnt[s];
					best_fit_location = s;
					best_fit_sufficient = sufficient;
				}
			}

			/* check that we have found a usable socket */
			if ( best_fit_cpus == 0 )
				break;

			debug3("dist_task: best_fit : using node[%u]:"
			       "socket[%u] : %u cores available",
			       n, best_fit_location,
			       sockets_cpu_cnt[best_fit_location]);

			/* select socket cores from last to first */
			/* socket[0]:Core[0] would be the last one */
			sockets_used[best_fit_location] = true;

			for ( j = c + ((best_fit_location+1) * ncores_nb)
				      - 1 ;
			      (int) j >= (int) (c + (best_fit_location *
						     ncores_nb)) ;
			      j-- ) {

				/*
				 * if no more cpus to select
				 * release remaining cores unless
				 * we are allocating whole sockets
				 */
				if ( cpus == 0 && alloc_sockets ) {
					if ( bit_test(job_res->core_bitmap,j) )
						core_cnt++;
					continue;
				}
				else if ( cpus == 0 ) {
					bit_clear(job_res->core_bitmap,j);
					continue;
				}

				/*
				 * remove cores from socket count and
				 * cpus count using hyperthreading requirement
				 */
				if ( bit_test(job_res->core_bitmap,j) ) {
					sockets_cpu_cnt[best_fit_location]--;
					core_cnt++;
					if (cpus < vpus)
						cpus = 0;
					else
						cpus -= vpus;
				}

			}

			/* loop again if more cpus required */
			if ( cpus > 0 )
				continue;

			/* release remaining cores of the unused sockets */
			for (s = 0; s < nsockets_nb; s++) {
				if ( sockets_used[s] )
					continue;
				bit_nclear(job_res->core_bitmap,
					   c+(s*ncores_nb),
					   c+((s+1)*ncores_nb)-1);
			}

		}

		if (cpus > 0) {
			/* cpu count should NEVER be greater than the number
			 * of set bits in the core bitmap for a given node */
			fatal("cons_res: cpus computation error");
		}

		/* adjust cpus count of the current node */
		if ((alloc_cores || alloc_sockets) &&
		    (select_node_record[n].vpus > 1)) {
			job_res->cpus[i] = core_cnt *
				select_node_record[n].vpus;
		}
		i++;

		/* move c to the next node in core_bitmap */
		c += num_bits;

	}

	xfree(sockets_cpu_cnt);
	xfree(sockets_used);
}


/* To effectively deal with heterogeneous nodes, we fake a cyclic
 * distribution to figure out how many cpus are needed on each node.
 *
 * This routine is a slightly modified "version" of the routine
 * _task_layout_block in src/common/dist_tasks.c. We do not need to
 * assign tasks to job->hostid[] and job->tids[][] at this point so
 * the cpu allocation is the same for cyclic and block.
 *
 * For the consumable resources support we need to determine what
 * "node/CPU/Core/thread"-tuplets will be allocated for a given job.
 * In the past we assumed that we only allocated one task per CPU (at
 * that point the lowest level of logical processor) and didn't allow
 * the use of overcommit. We have changed this philosophy and are now
 * allowing people to overcommit their resources and expect the system
 * administrator to enable the task/affinity plug-in which will then
 * bind all of a job's tasks to its allocated resources thereby
 * avoiding interference between co-allocated running jobs.
 *
 * In the consumable resources environment we need to determine the
 * layout schema within slurmctld.
 *
 * We have a core_bitmap of all available cores. All we're doing here
 * is removing cores that are not needed based on the task count, and
 * the choice of cores to remove is based on the distribution:
 * - "cyclic" removes cores "evenly", starting from the last socket,
 * - "block" removes cores from the "last" socket(s)
 * - "plane" removes cores "in chunks"
 */
extern int cr_dist(struct job_record *job_ptr, const uint16_t cr_type)
{
	int error_code;

	/* perform a cyclic distribution on the 'cpus' array */
	error_code = _compute_c_b_task_dist(job_ptr);
	if (error_code != SLURM_SUCCESS) {
		error("cons_res: cr_dist: Error in "
		      "_compute_c_b_task_dist");
		return error_code;
	}

	/* now sync up the core_bitmap with the allocated 'cpus' array
	 * based on the given distribution AND resource setting */
	_block_sync_core_bitmap(job_ptr, cr_type);
	return SLURM_SUCCESS;
}
