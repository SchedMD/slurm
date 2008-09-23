/*****************************************************************************\
 *  dist_tasks - Assign task count to {socket,core,thread} or CPU
 *               resources
 ***************************************************************************** 
 *  Copyright (C) 2006-2008 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle, <susanne.balle@hp.com>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include "select_cons_res.h"
#include "dist_tasks.h"

#if(1)
#define CR_DEBUG 1
#endif

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
int _compute_c_b_task_dist(struct job_record *job_ptr)
{
	bool over_subscribe = false;
	uint32_t n, i, tid, maxtasks;
	uint16_t *avail_cpus;
	select_job_res_t job_res = job_ptr->select_job;
	if (!job_res || !job_res->cpus) {
		error("cons_res: _compute_c_b_task_dist given NULL job_ptr");
		return SLURM_ERROR;
	}

	maxtasks = job_res->nprocs;
	avail_cpus = job_res->cpus;
	job_res->cpus = xmalloc(job_res->nhosts * sizeof(uint16_t));

	for (tid = 0, i = 0; (tid < maxtasks); i++) { /* cycle counter */
		bool space_remaining = false;
		if (over_subscribe) {
			/* 'over_subscribe' is a relief valve that guards
			 * against an infinite loop, and it *should* never
			 * come into play because maxtasks should never be
			 * greater than the total number of available cpus
			 */
			error("cons_res: _compute_c_b_task_dist oversubscribe");
		}
		for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
			if ((i < avail_cpus[n]) || over_subscribe) {
				tid++;
				if (job_res->cpus[n] < avail_cpus[n])
					job_res->cpus[n]++;
				if ((i + 1) < avail_cpus[n])
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

/* sync up core bitmap with new CPU count
 *
 * The CPU array contains the distribution of CPUs, which can include
 * virtual CPUs (hyperthreads)
 */
static void _block_sync_core_bitmap(struct job_record *job_ptr)
{
	uint32_t c, i, n, size, csize;
	uint16_t cpus, num_bits, vpus = 1;
	select_job_res_t job_res = job_ptr->select_job;
	if (!job_res)
		return;
	
	size  = bit_size(job_res->node_bitmap);
	csize = bit_size(job_res->core_bitmap);
	for (c = 0, i = 0, n = 0; n < size; n++) {
		
		if (bit_test(job_res->node_bitmap, n) == 0)
			continue;
		num_bits = select_node_record[n].sockets *
				select_node_record[n].cores;
		if ((c + num_bits) > csize)
			fatal ("cons_res: _block_sync_core_bitmap index error");
		
		cpus  = job_res->cpus[i++];
		if (job_ptr->details && job_ptr->details->mc_ptr) {
			vpus  = MIN(job_ptr->details->mc_ptr->max_threads,
				    select_node_record[n].vpus);
		}
		
		while (cpus > 0 && num_bits > 0) {
			if (bit_test(job_res->core_bitmap, c++)) {
				if (cpus < vpus)
					cpus = 0;
				else
					cpus -= vpus;
			}
			num_bits--;
		}
		if (cpus > 0)
			/* cpu count should NEVER be greater than the number
			 * of set bits in the core bitmap for a given node */
			fatal("cons_res: cpus computation error");

		while (num_bits > 0) {
			bit_clear(job_res->core_bitmap, c++);
			num_bits--;
		}
		
	}
}


/* Sync up the core_bitmap with the CPU array using cyclic distribution
 *
 * The CPU array contains the distribution of CPUs, which can include
 * virtual CPUs (hyperthreads)
 */
static void _cyclic_sync_core_bitmap(struct job_record *job_ptr)
{
	uint32_t c, i, s, n, *sock_start, *sock_end, size, csize;
	uint16_t cps = 0, cpus, vpus, sockets, sock_size;
	select_job_res_t job_res = job_ptr->select_job;
	bitstr_t *core_map;
	if (!job_res)
		return;
	core_map = job_res->core_bitmap;
	if (!core_map)
		return;
	
	sock_size  = select_node_record[0].sockets;
	sock_start = xmalloc(sock_size * sizeof(uint32_t));
	sock_end   = xmalloc(sock_size * sizeof(uint32_t));
	
	size  = bit_size(job_res->node_bitmap);
	csize = bit_size(core_map);
	for (c = 0, i = 0, n = 0; n < size; n++) {
		
		if (bit_test(job_res->node_bitmap, n) == 0)
			continue;
		sockets = select_node_record[n].sockets;
		cps     = select_node_record[n].cores;
		vpus    = MIN(job_ptr->details->mc_ptr->max_threads,
				select_node_record[n].vpus);
#ifdef CR_DEBUG
		info("DEBUG: job %u node %s max_threads %u, vpus %u cpus %u",
			job_ptr->job_id, select_node_record[n].node_ptr->name,
			job_ptr->details->mc_ptr->max_threads, vpus,
			job_res->cpus[i]);
#endif
		if ((c + (sockets * cps)) > csize)
			fatal ("cons_res: _cyclic_sync_core_bitmap index error");

		if (sockets > sock_size) {
			sock_size = sockets;
			xrealloc(sock_start, sock_size * sizeof(uint32_t));
			xrealloc(sock_end, sock_size * sizeof(uint32_t));
		}
		
		for (s = 0; s < sockets; s++) {
			sock_start[s] = c + (s * cps);
			sock_end[s]   = sock_start[s] + cps;
		}
		cpus  = job_res->cpus[i++];
		while (cpus > 0) {
			uint16_t prev_cpus = cpus;
			for (s = 0; s < sockets; s++) {

				while (sock_start[s] < sock_end[s] &&
					bit_test(core_map, sock_start[s]) == 0)
					sock_start[s]++;

				if (sock_start[s] == sock_end[s])
					/* this socket is unusable*/
					continue;
				if (cpus < vpus)
					cpus = 0;
				else
					cpus -= vpus;
				sock_start[s]++;
			}
			if (prev_cpus == cpus) {
				/* we're stuck!*/
				fatal("cons_res: sync loop not progressing");
			}
		}
		/* clear the rest of the cores in each socket
		 * FIXME: do we need min_core/min_socket checks here? */
		for (s = 0; s < sockets; s++) {
			if (sock_start[s] == sock_end[s])
				continue;
			bit_nclear(core_map, sock_start[s], sock_end[s]-1);
		}
		/* advance 'c' to the beginning of the next node */
		c += sockets * cps;
	}
	xfree(sock_start);
	xfree(sock_end);
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
 * the use of overcommit. We have change this philosophy and are now
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
extern int cr_dist(struct job_record *job_ptr,
		   const select_type_plugin_info_t cr_type)
{
	int error_code, cr_cpu = 1; 
	
	/* perform a cyclic distribution of tasks on the 'cpus' array */
	error_code = _compute_c_b_task_dist(job_ptr);
	if (error_code != SLURM_SUCCESS) {
		error("cons_res: Error in _compute_c_b_task_dist from cr_dist");
		return error_code;
	}

	/* now sync up the core_bitmap with the allocated 'cpus' array
	 * based on the given distribution AND resource setting */
	if ((cr_type == CR_CORE)   || (cr_type == CR_CORE_MEMORY) ||
	    (cr_type == CR_SOCKET) || (cr_type == CR_SOCKET_MEMORY))
		cr_cpu = 0;

	if (cr_cpu) {
		_block_sync_core_bitmap(job_ptr);
		return SLURM_SUCCESS;
	}

	/* Determine the number of logical processors per node needed
	 * for this job. Make sure below matches the layouts in
	 * lllp_distribution in plugins/task/affinity/dist_task.c (FIXME) */
	switch(job_ptr->details->task_dist) {
	case SLURM_DIST_BLOCK_BLOCK:
	case SLURM_DIST_CYCLIC_BLOCK:
		_block_sync_core_bitmap(job_ptr);
		break;
	case SLURM_DIST_ARBITRARY:
	case SLURM_DIST_BLOCK:
	case SLURM_DIST_CYCLIC:				
	case SLURM_DIST_BLOCK_CYCLIC:
	case SLURM_DIST_CYCLIC_CYCLIC:
	case SLURM_DIST_UNKNOWN:
		_cyclic_sync_core_bitmap(job_ptr); 
		break;
	case SLURM_DIST_PLANE:
		fatal("cons_res: cr_dist coding error");
		break;
	default:
		error("select/cons_res: invalid task_dist entry");
		error_code = SLURM_ERROR;
		break;
	}
	return SLURM_SUCCESS;
}

/* User has specified the '--exclusive' flag on the srun command line
 * which means that the job should use only dedicated nodes. In this
 * case we just need to confirm that all core bits have been set for
 * each allocated node.
 */
extern int cr_exclusive_dist(struct job_record *job_ptr)
{
	uint32_t n, c, size, csize;
	uint16_t num_bits;
	bitstr_t *node_map, *core_map;

	if (!job_ptr->select_job || !job_ptr->select_job->core_bitmap ||
	    !job_ptr->select_job->node_bitmap) {
	    	error("cons_res: cr_exclusive_dist given NULL job_ptr");
		return SLURM_ERROR;
	}
	node_map = job_ptr->select_job->node_bitmap;
	core_map = job_ptr->select_job->core_bitmap;
	
	size  = bit_size(node_map);
	csize = bit_size(core_map);
	for (c = 0, n = 0; n < size; n++) {
		if (bit_test(node_map, n) == 0)
			continue;
		num_bits = select_node_record[n].sockets *
				select_node_record[n].cores;
		if ((c + num_bits) > csize)
			fatal ("cons_res: cr_exclusive_dist index error");

		for (;num_bits > 0; num_bits--) {
			if (bit_test(core_map, c++) == 0) {
				error("cons_res: cr_exclusive_dist clear bit");
				return SLURM_ERROR;
			}
		}
	}
	return SLURM_SUCCESS;
}

/* FIXME! need thread support, which will depend on cr_type? */
extern int cr_plane_dist(struct job_record *job_ptr,
			 const select_type_plugin_info_t cr_type)
{
	bool over_subscribe = false;
	uint32_t n, i, p, tid, maxtasks;
	uint16_t *avail_cpus, plane_size = 1;
	select_job_res_t job_res = job_ptr->select_job;
	if (!job_res || !job_res->cpus) {
		error("cons_res: _cr_plane_dist given NULL job_res");
		return SLURM_ERROR;
	}

	maxtasks = job_res->nprocs;
	avail_cpus = job_res->cpus;
	
	if (job_ptr->details && job_ptr->details->mc_ptr)
		plane_size = job_ptr->details->mc_ptr->plane_size;

	if (plane_size <= 0) {
		error("cons_res: _cr_plane_dist received invalid plane_size");
		return SLURM_ERROR;
	}
	job_res->cpus = xmalloc(job_res->nhosts * sizeof(uint16_t));

	for (tid = 0, i = 0; (tid < maxtasks); i++) { /* cycle counter */
		bool space_remaining = false;
		if (over_subscribe) {
			/* 'over_subscribe' is a relief valve that guards
			 * against an infinite loop, and it *should* never
			 * come into play because maxtasks should never be
			 * greater than the total number of available cpus
			 */
			error("cons_res: _compute_c_b_task_dist oversubscribe");
		}
		for (n = 0; ((n < job_res->nhosts) && (tid < maxtasks)); n++) {
			for (p = 0; p < plane_size && (tid < maxtasks); p++) {
				if ((job_res->cpus[n] < avail_cpus[n]) ||
				    over_subscribe) {
					tid++;
					if (job_res->cpus[n] < avail_cpus[n])
						job_res->cpus[n]++;
				}
			}
			if (job_res->cpus[n] < avail_cpus[n])
				space_remaining = true;
		}
		if (!space_remaining) {
			over_subscribe = true;
		}
	}
	xfree(avail_cpus);

	/* now sync up core_bitmap with 'cpus' array */
	_block_sync_core_bitmap(job_ptr);

	return SLURM_SUCCESS;
}
