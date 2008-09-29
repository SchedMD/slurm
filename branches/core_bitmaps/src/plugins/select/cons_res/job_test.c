/*****************************************************************************\
 *  select_cons_res.c - node selection plugin supporting consumable 
 *  resources policies.
 *****************************************************************************\
 *
 *  The following example below illustrates how four jobs are allocated
 *  across a cluster using when a processor consumable resource approach.
 * 
 *  The example cluster is composed of 4 nodes (10 cpus in total):
 *  linux01 (with 2 processors), 
 *  linux02 (with 2 processors), 
 *  linux03 (with 2 processors), and
 *  linux04 (with 4 processors). 
 *
 *  The four jobs are the following: 
 *  1. srun -n 4 -N 4  sleep 120 &
 *  2. srun -n 3 -N 3 sleep 120 &
 *  3. srun -n 1 sleep 120 &
 *  4. srun -n 3 sleep 120 &
 *  The user launches them in the same order as listed above.
 * 
 *  Using a processor consumable resource approach we get the following
 *  job allocation and scheduling:
 * 
 *  The output of squeue shows that we have 3 out of the 4 jobs allocated
 *  and running. This is a 2 running job increase over the default SLURM
 *  approach.
 * 
 *  Job 2, Job 3, and Job 4 are now running concurrently on the cluster.
 * 
 *  [<snip>]# squeue
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root  PD       0:00      1 (Resources)
 *     2        lsf    sleep     root   R       0:13      4 linux[01-04]
 *     3        lsf    sleep     root   R       0:09      3 linux[01-03]
 *     4        lsf    sleep     root   R       0:05      1 linux04
 *  [<snip>]#
 * 
 *  Once Job 2 finishes, Job 5, which was pending, is allocated
 *  available resources and is then running as illustrated below:
 * 
 *  [<snip>]# squeue4
 *   JOBID PARTITION    NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     3        lsf    sleep     root   R       1:58      3 linux[01-03]
 *     4        lsf    sleep     root   R       1:54      1 linux04
 *     5        lsf    sleep     root   R       0:02      3 linux[01-03]
 *  [<snip>]#
 * 
 *  Job 3, Job 4, and Job 5 are now running concurrently on the cluster.
 * 
 *  [<snip>]#  squeue4
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root   R       1:52      3 xc14n[13-15]
 *  [<snip>]#
 *
 * The advantage of the consumable resource scheduling policy is that
 * the job throughput can increase dramatically.
 *
 *****************************************************************************
 *  Copyright (C) 2005-2008 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle <susanne.balle@hp.com>, who borrowed heavily
 *  from select/linear 
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#endif

#include "dist_tasks.h"
#include "job_test.h"
#include "select_cons_res.h"


/* The following variables are used to test for preemption, which
 * requires a change to the resource selection process */
static bool sched_gang_test = false;
static bool sched_gang      = false;


/* _allocate_sockets - Given the job requirements, determine which sockets
 *                     from the given node can be allocated (if any) to this
 *                     job. Returns the number of cpus that can be used by
 *                     this node AND a core-level bitmap of the selected
 *                     sockets.
 *
 * IN job_ptr      - pointer to job requirements
 * IN/OUT core_map - core_bitmap of available cores
 * IN node_i       - index of node to be evaluated
 */
uint16_t _allocate_sockets(struct job_record *job_ptr, bitstr_t *core_map,
			   const uint32_t node_i)
{
	uint16_t cpu_count = 0, cpu_cnt = 0;
	uint16_t si, cps, avail_cpus = 0, num_tasks = 0;
	uint32_t core_begin    = cr_get_coremap_offset(node_i);
	uint32_t core_end      = cr_get_coremap_offset(node_i+1);
	uint16_t cpus_per_task = job_ptr->details->cpus_per_task;
	uint16_t *used_cores, *free_cores, free_core_count = 0;
	uint16_t i, c, sockets    = select_node_record[node_i].sockets;
	uint16_t cores_per_socket = select_node_record[node_i].cores;
	uint16_t threads_per_core = select_node_record[node_i].vpus;

	uint16_t min_cores = 0, min_sockets = 0, ntasks_per_socket = 0;
	uint16_t max_cores = 0, max_sockets = 0, max_threads = 0;

	if (job_ptr->details && job_ptr->details->mc_ptr) {
		min_cores   = job_ptr->details->mc_ptr->min_cores;
		min_sockets = job_ptr->details->mc_ptr->min_sockets;
		max_cores   = job_ptr->details->mc_ptr->max_cores;
		max_sockets = job_ptr->details->mc_ptr->max_sockets;
		max_threads = job_ptr->details->mc_ptr->max_threads;
		ntasks_per_socket = job_ptr->details->mc_ptr->ntasks_per_socket;
	}
	
	/* These are the job parameters that we must respect:
	 *
	 *   job_ptr->details->mc_ptr->min_cores (cr_core|cr_socket)
	 *	- min # of cores per socket to allocate to this job
	 *   job_ptr->details->mc_ptr->max_cores (cr_core|cr_socket)
	 *	- max # of cores per socket to allocate to this job
	 *   job_ptr->details->mc_ptr->min_sockets (cr_core|cr_socket)
	 *	- min # of sockets per node to allocate to this job
	 *   job_ptr->details->mc_ptr->max_sockets (cr_core|cr_socket)
	 *	- max # of sockets per node to allocate to this job
	 *
	 *   job_ptr->details->mc_ptr->max_threads (cr_core|cr_socket)
	 *	- max_threads per core to allocate to this job
	 *   job_ptr->details->mc_ptr->ntasks_per_core (cr_core|cr_socket)
	 *	- number of tasks to launch per core
	 *   job_ptr->details->mc_ptr->ntasks_per_socket (cr_core|cr_socket)
	 *	- number of tasks to launch per socket
	 *
	 *   job_ptr->details->ntasks_per_node (all cr_types)
	 *	- total number of tasks to launch on this node
	 *   job_ptr->details->cpus_per_task (all cr_types)
	 *	- number of cpus to allocate per task
	 *
	 * These are the hardware constraints:
	 *   cpus = sockets * cores_per_socket * threads_per_core
	 *
	 * These are the cores/sockets that are available: core_map
	 *
	 * NOTE: currently we only allocate at the socket level, the core
	 *       level, or the cpu level. When hyperthreading is enabled
	 *       in the BIOS, then there can be more than one thread/cpu
	 *       per physical core.
	 *
	 * PROCEDURE:
	 *
	 * Step 1: Determine the current usage data: used_cores[],
	 *         used_core_count, free_cores[], free_core_count
	 *
	 * Step 2: For core-level and socket-level: apply min_sockets,
	 *         max_sockets, min_cores, and max_cores to the "free"
	 *         cores.
	 *
	 * Step 3: Compute task-related data: max_threads, ntasks_per_core,
	 *         ntasks_per_socket, ntasks_per_node and cpus_per_task
	 *         and determine the number of tasks to run on this node
	 *
	 * Step 4: Mark the allocated resources in the job_cores bitmap
	 *         and return "num_tasks" from Step 3.
	 *
	 *
	 * For socket and core counts, start by assuming that all available
	 * resources will be given to the job. Check min_* to ensure that
	 * there's enough resources. Reduce the resource count to match max_*
	 * (if necessary). Also reduce resource count (if necessary) to
	 * match ntasks_per_resource.
	 *
	 * NOTE: Memory is not used as a constraint here - should it?
	 *       If not then it needs to be done somewhere else!
	 */


	/* Step 1: create and compute core-count-per-socket
	 * arrays and total core counts */
	free_cores = xmalloc(sockets * sizeof(uint16_t));
	used_cores = xmalloc(sockets * sizeof(uint16_t));
	
	for (c = core_begin; c < core_end; c++) {
		i = (c - core_begin) / cores_per_socket;
		if (bit_test(core_map, c)) {
			free_cores[i]++;
			free_core_count++;
		} else {
			used_cores[i]++;
		}
	}
	/* if a socket is already in use, it cannot be used
	 * by this job */
	for (i = 0; i < sockets; i++) {
		if (used_cores[i]) {
			free_core_count -= free_cores[i];
			used_cores[i] += free_cores[i];
			free_cores[i] = 0;
		}
	}
	xfree(used_cores);
	used_cores = NULL;

	/* Step 2: check min_cores per socket and min_sockets per node */
	c = 0;
	for (i = 0; i < sockets; i++) {
		if (free_cores[i] < min_cores) {
			/* cannot use this socket */
			free_core_count -= free_cores[i];
			free_cores[i] = 0;
			continue;
		}
		/* count this socket as usable */
		c++;
	}
	if (c < min_sockets) {
		/* cannot use this node */
		num_tasks = 0;
		goto fini;
	}
	
	/* check max_cores and max_sockets */
	c = 0;
	for (i = 0; i < sockets; i++) {
		if (max_cores && free_cores[i] > max_cores) {
			/* remove extra cores from this socket */
			uint16_t tmp = free_cores[i] - max_cores;
			free_core_count -= tmp;
			free_cores[i] -= tmp;
		}
		if (free_cores[i] > 0)
			c++;
		if (max_sockets && free_cores[i] && c > max_sockets) {
			/* remove extra sockets from use */
			free_core_count -= free_cores[i];
			free_cores[i] = 0;
		}
	}
	if (free_core_count < 1) {
		/* no available resources on this node */
		num_tasks = 0;
		goto fini;
	}


	/* Step 3: Compute task-related data: use max_threads,
	 *         ntasks_per_socket, ntasks_per_node and cpus_per_task
	 *         to determine the number of tasks to run on this node
	 *
	 * Note: cpus_per_task and ntasks_per_core need to play nice
	 *       2 tasks_per_core vs. 2 cpus_per_task
	 */
	avail_cpus = 0;
	num_tasks = 0;
	threads_per_core = MIN(threads_per_core,max_threads);
	for (i = 0; i < sockets; i++) {
		uint16_t tmp = free_cores[i] * threads_per_core;
		avail_cpus += tmp;
		if (ntasks_per_socket)
			num_tasks += MIN(tmp,ntasks_per_socket);
		else
			num_tasks += tmp;
	}
	if (job_ptr->details->ntasks_per_node)
		num_tasks = MIN(num_tasks, job_ptr->details->ntasks_per_node);
	
	if (cpus_per_task < 2) {
		avail_cpus = num_tasks;
		cps = num_tasks;
	} else {
		c = avail_cpus / cpus_per_task;
		num_tasks = MIN(num_tasks, c);
		avail_cpus = num_tasks * cpus_per_task;
	}
	
	/* Step 4 - make sure that ntasks_per_socket is enforced when
	 *          allocating cores
	 */
	cps = num_tasks;
	if (ntasks_per_socket > 1) {
		cps = ntasks_per_socket;
		if (cpus_per_task > 1)
			cps = ntasks_per_socket * cpus_per_task;
	}
	si = 9999;
	for (c = core_begin; c < core_end && avail_cpus > 0; c++) {
		if (bit_test(core_map, c) == 0)
			continue;
		i = (c - core_begin) / cores_per_socket;
		if (free_cores[i] > 0) {
			/* this socket has free cores, but make sure
			 * we don't use more than are needed for
			 * ntasks_per_socket */
			if (si != i) {
				si = i;
				cpu_cnt = threads_per_core;
			} else {
				if (cpu_cnt >= cps) {
					/* do not allocate this core */
					bit_clear(core_map, c);
					continue;
				}
				cpu_cnt += threads_per_core;
			}
			free_cores[i]--;
			cpu_count += threads_per_core;
			if (avail_cpus >= threads_per_core)
				avail_cpus -= threads_per_core;
			else
				avail_cpus = 0;
			
		} else
			bit_clear(core_map, c);
	}
	/* clear leftovers */
	if (c < core_end)
		bit_nclear(core_map, c, core_end-1);

fini:
	/* if num_tasks == 0 then clear all bits on this node */
	if (!num_tasks) {
		bit_nclear(core_map, core_begin, core_end-1);
		cpu_count = 0;
	}
	xfree(free_cores);
	return cpu_count;
}


/* _allocate_cores - Given the job requirements, determine which cores
 *                   from the given node can be allocated (if any) to this
 *                   job. Returns the number of cpus that can be used by
 *                   this node AND a bitmap of the selected cores.
 *
 * IN job_ptr      - pointer to job requirements
 * IN/OUT core_map - bitmap of cores available for use/selected for use
 * IN node_i       - index of node to be evaluated
 */
uint16_t _allocate_cores(struct job_record *job_ptr, bitstr_t *core_map,
			 const uint32_t node_i, int cpu_type)
{
	uint16_t cpu_count = 0, avail_cpus = 0, num_tasks = 0;
	uint32_t core_begin    = cr_get_coremap_offset(node_i);
	uint32_t core_end      = cr_get_coremap_offset(node_i+1);
	uint16_t cpus_per_task = job_ptr->details->cpus_per_task;
	uint16_t *free_cores, free_core_count = 0;
	uint16_t i, c, sockets    = select_node_record[node_i].sockets;
	uint16_t cores_per_socket = select_node_record[node_i].cores;
	uint16_t threads_per_core = select_node_record[node_i].vpus;

	uint16_t min_cores = 0, min_sockets = 0;
	uint16_t max_cores = 0, max_sockets = 0, max_threads = 0;

	if (!cpu_type && job_ptr->details && job_ptr->details->mc_ptr) {
		min_cores   = job_ptr->details->mc_ptr->min_cores;
		min_sockets = job_ptr->details->mc_ptr->min_sockets;
		max_cores   = job_ptr->details->mc_ptr->max_cores;
		max_sockets = job_ptr->details->mc_ptr->max_sockets;
		max_threads = job_ptr->details->mc_ptr->max_threads;
	}
	
	/* These are the job parameters that we must respect:
	 *
	 *   job_ptr->details->mc_ptr->min_cores (cr_core|cr_socket)
	 *	- min # of cores per socket to allocate to this job
	 *   job_ptr->details->mc_ptr->max_cores (cr_core|cr_socket)
	 *	- max # of cores per socket to allocate to this job
	 *   job_ptr->details->mc_ptr->min_sockets (cr_core|cr_socket)
	 *	- min # of sockets per node to allocate to this job
	 *   job_ptr->details->mc_ptr->max_sockets (cr_core|cr_socket)
	 *	- max # of sockets per node to allocate to this job
	 *
	 *   job_ptr->details->mc_ptr->max_threads (cr_core|cr_socket)
	 *	- max_threads per core to allocate to this job
	 *   job_ptr->details->mc_ptr->ntasks_per_core (cr_core|cr_socket)
	 *	- number of tasks to launch per core
	 *   job_ptr->details->mc_ptr->ntasks_per_socket (cr_core|cr_socket)
	 *	- number of tasks to launch per socket
	 *
	 *   job_ptr->details->ntasks_per_node (all cr_types)
	 *	- total number of tasks to launch on this node
	 *   job_ptr->details->cpus_per_task (all cr_types)
	 *	- number of cpus to allocate per task
	 *
	 * These are the hardware constraints:
	 *   cpus = sockets * cores_per_socket * threads_per_core
	 *
	 * These are the cores that are available for use: core_map
	 *
	 * NOTE: currently we only allocate at the socket level, the core
	 *       level, or the cpu level. When hyperthreading is enabled
	 *       in the BIOS, then there can be more than one thread/cpu
	 *       per physical core.
	 *
	 * PROCEDURE:
	 *
	 * Step 1: Determine the current usage data: free_cores[] and
	 *         free_core_count
	 *
	 * Step 2: Apply min_sockets, max_sockets, min_cores and
	 *         max_cores and to the "free" cores.
	 *
	 * Step 3: Compute task-related data: use max_threads,
	 *         ntasks_per_core, ntasks_per_node and cpus_per_task
	 *         to determine the number of tasks that can run on
	 *         this node
	 *
	 * Step 4: Mark the allocated resources in the job_cores bitmap
	 *         and return "num_tasks" from Step 3.
	 *
	 *
	 * Start by assuming that all "free" cores will be given to the
	 * job. Check min_* to ensure that there's enough resources.
	 * Reduce the core count to match max_* (if necessary). Also,
	 * reduce the core count (if necessary) to match ntasks_per_core.
	 * Note that we're not processing ntasks_per_socket, because the
	 * srun manpage says that ntasks_per_socket is only valid for
	 * CR_SOCKET.
	 */

	/* Step 1: create and compute core-count-per-socket
	 * arrays and total core counts */
	free_cores = xmalloc(sockets * sizeof(uint16_t));
	
	for (c = core_begin; c < core_end; c++) {
		i = (c - core_begin) / cores_per_socket;
		if (bit_test(core_map, c)) {
			free_cores[i]++;
			free_core_count++;
		}
	}
	
	/* Step 2a: check min_cores per socket and min_sockets per node */
	c = 0;
	for (i = 0; i < sockets; i++) {
		if (free_cores[i] < min_cores) {
			/* cannot use this socket */
			free_core_count -= free_cores[i];
			free_cores[i] = 0;
			continue;
		}
		/* count this socket as usable */
		c++;
	}
	if (c < min_sockets) {
		/* cannot use this node */
		num_tasks = 0;
		goto fini;
	}
	
	/* Step 2b: check max_cores per socket and max_sockets per node */
	c = 0;
	for (i = 0; i < sockets; i++) {
		if (max_cores && free_cores[i] > max_cores) {
			/* remove extra cores from this socket */
			uint16_t tmp = free_cores[i] - max_cores;
			free_core_count -= tmp;
			free_cores[i] -= tmp;
		}
		if (free_cores[i] > 0)
			c++;
		if (max_sockets && free_cores[i] && c > max_sockets) {
			/* remove extra sockets from use */
			free_core_count -= free_cores[i];
			free_cores[i] = 0;
		}
	}
	if (free_core_count < 1) {
		/* no available resources on this node */
		num_tasks = 0;
		goto fini;
	}


	/* Step 3: Compute task-related data: use max_threads,
	 *         ntasks_per_core, ntasks_per_node and cpus_per_task
	 *         to determine the number of tasks to run on this node
	 *
	 * Note: cpus_per_task and ntasks_per_core need to play nice
	 *       2 tasks_per_core vs. 2 cpus_per_task
	 */

	if (cpu_type)
		max_threads = threads_per_core;
	threads_per_core = MIN(threads_per_core,max_threads);
	num_tasks = avail_cpus = threads_per_core;
	i = job_ptr->details->mc_ptr->ntasks_per_core;
	if (!cpu_type && i > 0)
		num_tasks = MIN(num_tasks, i);
	
	/* convert from PER_CORE to TOTAL_FOR_NODE */
	avail_cpus *= free_core_count;
	num_tasks *= free_core_count;

	if (job_ptr->details->ntasks_per_node)
		num_tasks = MIN(num_tasks, job_ptr->details->ntasks_per_node);
	
	if (cpus_per_task < 2) {
		avail_cpus = num_tasks;
	} else {
		c = avail_cpus / cpus_per_task;
		num_tasks = MIN(num_tasks, c);
		avail_cpus = num_tasks * cpus_per_task;
	}
	
	/* Step 4 */
	for (c = core_begin; c < core_end && avail_cpus > 0; c++) {
		if (bit_test(core_map, c) == 0)
			continue;
		i = (c - core_begin) / cores_per_socket;
		if (free_cores[i] == 0)
			bit_clear(core_map, c);
		else {
			free_cores[i]--;
			cpu_count += threads_per_core;
			if (avail_cpus >= threads_per_core)
				avail_cpus -= threads_per_core;
			else
				avail_cpus = 0;
		}
	}
	/* clear leftovers */
	if (c < core_end)
		bit_nclear(core_map, c, core_end-1);

fini:
	if (!num_tasks) {
		bit_nclear(core_map, core_begin, core_end-1);
		cpu_count = 0;
	}
	xfree(free_cores);
	return cpu_count;
}


/*
 * _can_job_run_on_node - Given the job requirements, determine which
 *                        resources from the given node (if any) can be
 *                        allocated to this job. Returns the number of
 *                        cpus that can be used by this node and a bitmap
 *                        of available resources for allocation.
 *       NOTE: This process does NOT support overcommitting resources
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores
 * IN n             - index of node to be evaluated
 * IN cr_type       - Consumable Resource setting
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in 
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to deselect from the core_map to match the cpu_count.
 */
uint16_t _can_job_run_on_node(struct job_record *job_ptr, bitstr_t *core_map,
			      const uint32_t node_i,
			      select_type_plugin_info_t cr_type)
{
	uint16_t cpus;
	uint32_t avail_mem, req_mem;

	switch (cr_type) {
	case CR_CORE:
	case CR_CORE_MEMORY:
		cpus = _allocate_cores(job_ptr, core_map, node_i, 0);
		break;
	case CR_SOCKET:
	case CR_SOCKET_MEMORY:
		cpus = _allocate_sockets(job_ptr, core_map, node_i);
		break;
	case SELECT_TYPE_INFO_NONE:
		/* Default for select/linear */
	case CR_CPU:
	case CR_CPU_MEMORY:
	case CR_MEMORY:
	default:
		cpus = _allocate_cores(job_ptr, core_map, node_i, 1);
	}
	
	if (cr_type != CR_CPU_MEMORY && cr_type != CR_CORE_MEMORY &&
	    cr_type != CR_SOCKET_MEMORY && cr_type != CR_MEMORY)
		return cpus;

	/* Memory Check: check job_min_memory to see if:
	 *          - this node has enough memory (MEM_PER_CPU == 0)
	 *          - there are enough free_cores (MEM_PER_CPU = 1)
	 */
	req_mem   = job_ptr->details->job_min_memory & ~MEM_PER_CPU;
	avail_mem = select_node_record[node_i].real_memory - 
			select_node_record[node_i].alloc_memory;
	if (job_ptr->details->job_min_memory & MEM_PER_CPU) {
		/* memory is per-cpu */
		while (cpus > 0 && (req_mem * cpus) > avail_mem)
			cpus--;	
	/* FIXME: do we need to recheck min_cores, etc. here? */	
	} else {
		/* memory is per node */
		if (req_mem > avail_mem) {
			bit_nclear(core_map, cr_get_coremap_offset(node_i), 
					(cr_get_coremap_offset(node_i+1))-1);
			cpus = 0;
		}
	}
	
	debug3("cons_res: _can_job_run_on_node: %u cpus on %s", cpus,
		select_node_record[node_i].node_ptr->name);
	
	return cpus;
}


/* Test to see if a node already has running jobs.
 * if (sharing_only) then only check sharing partitions. This is because
 * the job was submitted to a single-row partition which does not share
 * allocated CPUs with multi-row partitions.
 */
static int _is_node_busy(struct part_res_record *p_ptr, uint32_t node_i,
			 int sharing_only)
{
	uint32_t r, cpu_begin = cr_get_coremap_offset(node_i);
	uint32_t i, cpu_end   = cr_get_coremap_offset(node_i+1);

	for (; p_ptr; p_ptr = p_ptr->next) {
		if (sharing_only && p_ptr->num_rows < 2)
			continue;
		for (r = 0; r < p_ptr->num_rows; r++) {
			if (!p_ptr->row[r].row_bitmap)
				continue;
			for (i = cpu_begin; i < cpu_end; i++) {
				if (bit_test(p_ptr->row[r].row_bitmap, i))
					return 1;
			}
		}
	}
	return 0;
}


/*
 * Determine which of these nodes are usable by this job
 *
 * Remove nodes from the bitmap that don't have enough memory to
 * support the job. Return SLURM_ERROR if a required node doesn't
 * have enough memory.
 *
 * if node_state = NODE_CR_RESERVED, clear bitmap (if node is required
 *                                   then should we return NODE_BUSY!?!)
 *
 * if node_state = NODE_CR_ONE_ROW, then this node can only be used by
 *                                  another NODE_CR_ONE_ROW job
 *
 * if node_state = NODE_CR_AVAILABLE AND:
 *  - job_node_req = NODE_CR_RESERVED, then we need idle nodes
 *  - job_node_req = NODE_CR_ONE_ROW, then we need idle or non-sharing nodes
 */
static int _verify_node_state(struct part_res_record *cr_part_ptr,
			      struct job_record *job_ptr, bitstr_t * bitmap,
			      select_type_plugin_info_t cr_type,
			      enum node_cr_state job_node_req)
{
	int i;
	uint32_t free_mem, min_mem, size;

	min_mem = job_ptr->details->job_min_memory & (~MEM_PER_CPU);
	size = bit_size(bitmap);
	for (i = 0; i < size; i++) {
		if (!bit_test(bitmap, i))
			continue;

		/* node-level memory check */
		if ((job_ptr->details->job_min_memory) &&
		    ((cr_type == CR_CORE_MEMORY) || (cr_type == CR_CPU_MEMORY) ||
		     (cr_type == CR_MEMORY) || (cr_type == CR_SOCKET_MEMORY))) {
			free_mem  = select_node_record[i].real_memory;
			free_mem -= select_node_record[i].alloc_memory;
			if (free_mem < min_mem)
				goto clear_bit;
		}
		
		/* sched/gang preemption test */
		if (!sched_gang_test) {
			char *sched_type = slurm_get_sched_type();
			if (strcmp(sched_type, "sched/gang") == 0)
				sched_gang = true;
			xfree(sched_type);
			sched_gang_test = true;
		}
		/* if sched/gang is configured, then preemption has been 
		 * enabled and we cannot rule out nodes just because
		 * Shared=NO (NODE_CR_ONE_ROW) or Shared=EXCLUSIVE
		 * (NODE_CR_RESERVED) */
		if (sched_gang)
			continue;

		/* exclusive node check */
		if (select_node_record[i].node_state == NODE_CR_RESERVED) {
			goto clear_bit;
		
		/* non-resource-sharing node check */
		} else if (select_node_record[i].node_state == 
			   NODE_CR_ONE_ROW) {
			if ((job_node_req == NODE_CR_RESERVED) ||
			    (job_node_req == NODE_CR_AVAILABLE))
				goto clear_bit;
			/* cannot use this node if it is running jobs
			 * in sharing partitions */
			if ( _is_node_busy(cr_part_ptr, i, 1) )
				goto clear_bit;
		
		/* node is NODE_CR_AVAILABLE - check job request */
		} else {
			if (job_node_req == NODE_CR_RESERVED) {
				if ( _is_node_busy(cr_part_ptr, i, 0) )
					goto clear_bit;
			} else if (job_node_req == NODE_CR_ONE_ROW) {
				/* cannot use this node if it is running jobs
				 * in sharing partitions */
				if ( _is_node_busy(cr_part_ptr, i, 1) )
					goto clear_bit;
			}
		}
		continue;	/* node is usable, test next node */

 clear_bit:	/* This node is not usable by this job */
		bit_clear(bitmap, i);
		if (job_ptr->details->req_node_bitmap &&
		    bit_test(job_ptr->details->req_node_bitmap, i))
			return SLURM_ERROR;

	}

	return SLURM_SUCCESS;
}


/* given an "avail" node_bitmap, return a corresponding "avail" core_bitmap */
bitstr_t *_make_core_bitmap(bitstr_t *node_map)
{
	uint32_t n, c, nodes, size;

	nodes = bit_size(node_map);
	size = cr_get_coremap_offset(nodes+1);
	bitstr_t *core_map = bit_alloc(size);
	if (!core_map)
		return NULL;

	nodes = bit_size(node_map);
	for (n = 0, c = 0; n < nodes; n++) {
		if (bit_test(node_map, n)) {
			while (c < cr_get_coremap_offset(n+1)) {
				bit_set(core_map, c++);
			}
		}
	}
	return core_map;
}


/* return the number of cpus that the given
 * job can run on the indexed node */
static int _get_cpu_cnt(struct job_record *job_ptr, const int node_index,
			 uint16_t *cpu_cnt, uint32_t *freq, uint32_t size)
{
	int i, pos, cpus;
	uint16_t *layout_ptr = job_ptr->details->req_node_layout;

	pos = 0;
	for (i = 0; i < size; i++) {
		if (pos+freq[i] > node_index)
			break;
		pos += freq[i];
	}
	cpus = cpu_cnt[i];
	if (layout_ptr && bit_test(job_ptr->details->req_node_bitmap, i)) {
		pos = bit_get_pos_num(job_ptr->details->req_node_bitmap, i);
		cpus = MIN(cpus, layout_ptr[pos]);
	} else if (layout_ptr) {
		cpus = 0; /* should not happen? */
	}
	return cpus;
}


#define CR_FREQ_ARRAY_INCREMENT 16

/* Compute resource usage for the given job on all available resources
 *
 * IN: job_ptr     - pointer to the job requesting resources
 * IN: node_map    - bitmap of available nodes
 * IN: core_map    - bitmap of available cores
 * IN: cr_node_cnt - total number of nodes in the cluster
 * IN: cr_type     - resource type
 * OUT: cpu_cnt    - number of cpus that can be used by this job
 * OUT: freq       - number of nodes to which the corresponding cpu_cnt applies
 * OUT:            returns the length of the 2 arrays
 */
uint32_t _get_res_usage(struct job_record *job_ptr, bitstr_t *node_map,
			bitstr_t *core_map, uint32_t cr_node_cnt,
			select_type_plugin_info_t cr_type,
			uint16_t **cpu_cnt_ptr, uint32_t **freq_ptr)
{
	uint16_t *cpu_cnt, cpu_count;
	uint32_t *freq;
	uint32_t n, size = 0, array_size = CR_FREQ_ARRAY_INCREMENT;

	cpu_cnt = xmalloc(array_size * sizeof(uint16_t));
	freq    = xmalloc(array_size * sizeof(uint32_t));
	
	for (n = 0; n < cr_node_cnt; n++) {
		if (bit_test(node_map, n)) {
			cpu_count = _can_job_run_on_node(job_ptr, core_map,
							 n, cr_type);
			if (cpu_count == cpu_cnt[size]) {
				freq[size]++;
				continue;
			}
			if (freq[size] == 0) {
				cpu_cnt[size] = cpu_count;
				freq[size]++;
				continue;
			}
			size++;
			if (size >= array_size) {
				array_size += CR_FREQ_ARRAY_INCREMENT;
				xrealloc(cpu_cnt, array_size *sizeof(uint16_t));
				xrealloc(freq, array_size * sizeof(uint32_t));
			}
			cpu_cnt[size] = cpu_count;
			freq[size]++;
		} else {
			if (cpu_cnt[size] == 0) {
				freq[size]++;
				continue;
			}
			size++;
			if (size >= array_size) {
				array_size += CR_FREQ_ARRAY_INCREMENT;
				xrealloc(cpu_cnt, array_size *sizeof(uint16_t));
				xrealloc(freq, array_size * sizeof(uint32_t));
			}
			freq[size]++;
		}
	}
	*cpu_cnt_ptr = cpu_cnt;
	*freq_ptr = freq;
	return size+1;
}			


static bool _enough_nodes(int avail_nodes, int rem_nodes, 
			  uint32_t min_nodes, uint32_t req_nodes)
{
	int needed_nodes;

	if (req_nodes > min_nodes)
		needed_nodes = rem_nodes + min_nodes - req_nodes;
	else
		needed_nodes = rem_nodes;

	return (avail_nodes >= needed_nodes);
}


/* this is the heart of the selection process */
static int _eval_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt, uint32_t *freq, uint32_t size)
{
	int i, f, index, error_code = SLURM_ERROR;
	int *consec_nodes;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this 
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required 
				 * (in req_bitmap) */
	int consec_index, consec_size, sufficient;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_sufficient, best_fit_index = 0;
	int avail_cpus, ll;	/* ll = layout array index */
	bool required_node;
	bitstr_t *req_map      = job_ptr->details->req_node_bitmap;
	uint16_t *layout_ptr = job_ptr->details->req_node_layout;

	xassert(node_map);
	
	if (bit_set_count(node_map) < min_nodes)
		return error_code;

	consec_size = 50;	/* start allocation for 50 sets of 
				 * consecutive nodes */
	consec_cpus  = xmalloc(sizeof(int) * consec_size);
	consec_nodes = xmalloc(sizeof(int) * consec_size);
	consec_start = xmalloc(sizeof(int) * consec_size);
	consec_end   = xmalloc(sizeof(int) * consec_size);
	consec_req   = xmalloc(sizeof(int) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_index = 0;
	consec_cpus[consec_index] = consec_nodes[consec_index] = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */

	rem_cpus = job_ptr->num_procs;
	rem_nodes = MAX(min_nodes, req_nodes);

	i = 0;
	f = 0;
	for (index = 0, ll = -1; index < cr_node_cnt; index++, f++) {
		if (f >= freq[i]) {
			f = 0;
			i++;
		}
		if (req_map) {
			required_node = bit_test(req_map, index);
		} else
			required_node = false;
		if (layout_ptr && required_node)
			ll++;
		if (bit_test(node_map, index)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = index;
			avail_cpus = cpu_cnt[i];
			if (layout_ptr && required_node){
				avail_cpus = MIN(avail_cpus, layout_ptr[ll]);
			} else if (layout_ptr) {
				avail_cpus = 0; /* should not happen? */
			}
			if ((max_nodes > 0) && required_node) {
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = index;
				}
				rem_cpus -= avail_cpus;
				rem_nodes--;
				/* leaving bitmap set, decrement max limit */
				max_nodes--;
			} else {	/* node not selected (yet) */
				bit_clear(node_map, index);
				consec_cpus[consec_index] += avail_cpus;
				consec_nodes[consec_index]++;
			}
		} else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;
			/* already picked up any required nodes */
			/* re-use this record */
		} else {
			consec_end[consec_index] = index - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				xrealloc(consec_cpus, sizeof(int)*consec_size);
				xrealloc(consec_nodes, sizeof(int)*consec_size);
				xrealloc(consec_start, sizeof(int)*consec_size);
				xrealloc(consec_end, sizeof(int)*consec_size);
				xrealloc(consec_req, sizeof(int)*consec_size);
			}
			consec_cpus[consec_index] = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index] = -1;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = index - 1;
	
	for (i = 0; i < consec_index; i++) {
		debug3("cons_res: eval_nodes:%d consec c=%d n=%d b=%d e=%d r=%d",
			i, consec_cpus[i], consec_nodes[i], consec_start[i],
			consec_end[i], consec_req[i]);
	}
	
	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index && (max_nodes > 0)) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;
			sufficient =  (consec_cpus[i] >= rem_cpus)
				&& _enough_nodes(consec_nodes[i], rem_nodes,
						 min_nodes, req_nodes);
			
			/* if first possibility OR */
			/* contains required nodes OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
			if ((best_fit_nodes == 0) ||
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient && (consec_cpus[i] < best_fit_cpus)) ||
			    (!sufficient && (consec_cpus[i] > best_fit_cpus))) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_index = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
			}
		}
		if (best_fit_nodes == 0)
			break;
		if (job_ptr->details->contiguous &&
		    ((best_fit_cpus < rem_cpus) ||
		     (!_enough_nodes(best_fit_nodes, rem_nodes,
				     min_nodes, req_nodes))))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/* This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_index]; i++) {
				if ((max_nodes <= 0)
				    ||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i))
					continue;
				bit_set(node_map, i);
				rem_nodes--;
				max_nodes--;
				avail_cpus = _get_cpu_cnt(job_ptr, i, cpu_cnt,
							  freq, size);
				rem_cpus -= avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_index]; i--) {
				if ((max_nodes <= 0)
				    ||  ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i)) 
					continue;
				avail_cpus = _get_cpu_cnt(job_ptr, i, cpu_cnt,
							  freq, size);
				if (avail_cpus <= 0)
					continue;
				rem_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				max_nodes--;
			}
		} else {
			for (i = consec_start[best_fit_index];
			     i <= consec_end[best_fit_index]; i++) {
				if ((max_nodes <= 0)
				    || ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i))
					continue;
				avail_cpus = _get_cpu_cnt(job_ptr, i, cpu_cnt,
							  freq, size);
				if (avail_cpus <= 0)
					continue;
				if ((max_nodes == 1) && 
				    (avail_cpus < rem_cpus)) {
					/* Job can only take one more node and
					 * this one has insufficient CPU */
					continue;
				}
				rem_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				max_nodes--;
			}
		}

		if (job_ptr->details->contiguous ||
		    ((rem_nodes <= 0) && (rem_cpus <= 0))) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_index] = 0;
		consec_nodes[best_fit_index] = 0;
	}
	
	if (error_code && (rem_cpus <= 0)
	    && _enough_nodes(0, rem_nodes, min_nodes, req_nodes))
		error_code = SLURM_SUCCESS;

	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}


/* this is an intermediary step between _select_nodes and _eval_nodes
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low cpu counts for the job and re-evaluates each result */
static int _choose_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			 uint32_t min_nodes, uint32_t max_nodes, 
			 uint32_t req_nodes, uint32_t cr_node_cnt,
			 uint16_t *cpu_cnt, uint32_t *freq, uint32_t size)
{
	int i, node_boundary, count, ec, most_cpus = 0;
	bitstr_t *origmap, *reqmap = NULL;

	/* allocated node count should never exceed num_procs, right? 
	 * if so, then this should be done earlier and max_nodes
	 * could be used to make this process more efficient (truncate
	 * # of available nodes when (# of idle nodes == max_nodes)*/
	if (max_nodes > job_ptr->num_procs)
		max_nodes = job_ptr->num_procs;

	origmap = bit_copy(node_map);
	if (origmap == NULL)
		fatal("bit_copy malloc failure");

	ec = _eval_nodes(job_ptr, node_map, min_nodes, max_nodes,
			 req_nodes, cr_node_cnt, cpu_cnt, freq, size);

	if (ec == SLURM_SUCCESS) {
		bit_free(origmap);
		return ec;
	}

	/* This nodeset didn't work. To avoid a possible knapsack problem, 
	 * incrementally remove nodes with low cpu counts and retry */

	/* find the higest number of cpus per node */
	for (i = 0; i < size; i++) {
		if (cpu_cnt[i] > most_cpus)
			most_cpus = cpu_cnt[i];
	}

	if (job_ptr->details->req_node_bitmap)
		reqmap = job_ptr->details->req_node_bitmap;
	
	for (count = 1; count < most_cpus; count++) {
		int nochange = 1;
		bit_or(node_map, origmap);
		for (i = 0, node_boundary = 0; i < size; i++) {
			if (cpu_cnt[i] > 0 && cpu_cnt[i] <= count) {
				int j, n = node_boundary;
				for (j = 0; j < freq[i]; j++, n++) {
					if (!bit_test(node_map, n))
						continue;
					if (reqmap && bit_test(reqmap, n)) {
						continue;
					}
					nochange = 0;
					bit_clear(node_map, n);
					bit_clear(origmap, n);
				}
			}
			node_boundary += freq[i];
		}
		if (nochange)
			continue;
		ec = _eval_nodes(job_ptr, node_map, min_nodes, max_nodes,
				 req_nodes, cr_node_cnt, cpu_cnt, freq, size);
		if (ec == SLURM_SUCCESS) {
			bit_free(origmap);
			return ec;
		}
	}
	bit_free(origmap);
	return ec;
}


/* Select the best set of resources for the given job
 * IN: job_ptr      - pointer to the job requesting resources
 * IN: min_nodes    - minimum number of nodes reuired
 * IN: max_nodes    - maximum number of nodes requested
 * IN: req_nodes    - number of required nodes
 * IN/OUT: node_map - bitmap of available nodes / bitmap of selected nodes
 * IN: cr_node_cnt  - total number of nodes in the cluster
 * IN/OUT: core_map - bitmap of available cores / bitmap of selected cores
 * IN: cr_type      - resource type
 * OUT:             return SLURM_SUCCESS if an allocation was found
 */
static uint16_t *_select_nodes(struct job_record *job_ptr, uint32_t min_nodes,
				uint32_t max_nodes, uint32_t req_nodes,
				bitstr_t *node_map, uint32_t cr_node_cnt,
				bitstr_t *core_map,
				select_type_plugin_info_t cr_type)
{
	int rc;
	uint16_t *cpu_cnt, *cpus = NULL;
	uint32_t start, n, a, i, f, size, *freq;
	
	if (bit_set_count(node_map) < min_nodes)
		return NULL;

	/* get resource usage for this job from each available node */
	size = _get_res_usage(job_ptr, node_map, core_map, cr_node_cnt,
				cr_type, &cpu_cnt, &freq);
	
	/* choose the best nodes for the job */
	rc = _choose_nodes(job_ptr, node_map, min_nodes, max_nodes, req_nodes,
				cr_node_cnt, cpu_cnt, freq, size);
	
	/* if successful, sync up the core_map with the node_map, and
	 * create a cpus array */
	if (rc == SLURM_SUCCESS) {
		cpus = xmalloc(bit_set_count(node_map) * sizeof(uint16_t));
		start = 0;
		a = i = f = 0;
		for (n = 0; n < cr_node_cnt; n++) {
			if (bit_test(node_map, n)) {
				cpus[a++] = cpu_cnt[i];
				if (cr_get_coremap_offset(n) != start) {
					bit_nclear(core_map, start, 
						(cr_get_coremap_offset(n))-1);
				}
				start = cr_get_coremap_offset(n+1);
			}
			f++;
			if (f >= freq[i]) {
				f = 0;
				i++;
			}
		}
		if (cr_get_coremap_offset(n) != start) {
			bit_nclear(core_map, start,
						(cr_get_coremap_offset(n))-1);
		}
	}

	xfree(cpu_cnt);
	xfree(freq);
	return cpus;
}


/* cr_job_test - does most of the real work for select_p_job_test(), which 
 *	includes contiguous selection, load-leveling and max_share logic
 *
 * PROCEDURE:
 *
 * Step 1: compare nodes in "avail" bitmap with current node state data
 *         to find available nodes that match the job request
 *
 * Step 2: check resources in "avail" bitmap with allocated resources from
 *         higher priority partitions (busy resources are UNavailable)
 *
 * Step 3: select resource usage on remaining resources in "avail" bitmap
 *         for this job, with the placement influenced by existing
 *         allocations
 */
extern int cr_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes, int mode,
			select_type_plugin_info_t cr_type,
			enum node_cr_state job_node_req, uint32_t cr_node_cnt,
			struct part_res_record *cr_part_ptr)
{
	int error_code = SLURM_SUCCESS, ll; /* ll = layout array index */
	uint16_t *layout_ptr = NULL;
	bitstr_t *orig_map, *avail_cores, *free_cores;
	bitstr_t *tmpcore = NULL, *reqmap = NULL;
	bool test_only;
	uint32_t c, i, n, csize, total_cpus, save_mem = 0;
	select_job_res_t job_res;
	struct part_res_record *p_ptr, *jp_ptr;
	uint16_t *cpu_count;

	layout_ptr = job_ptr->details->req_node_layout;
	reqmap = job_ptr->details->req_node_bitmap;
	
	free_select_job_res(&job_ptr->select_job);

	if (mode == SELECT_MODE_TEST_ONLY) {
		/* testing doesn't care about the current amount of available
		 * memory, so we'll "zero out" the job request for now */
		test_only = true;
		save_mem = job_ptr->details->job_min_memory;
		job_ptr->details->job_min_memory = 0;
	} else	/* SELECT_MODE_RUN_NOW || SELECT_MODE_WILL_RUN  */ 
		test_only = false;
	debug3("DEBUG: cr_job_test called with %u nodes",
		bit_set_count(bitmap));
	/* check node_state and update the node bitmap as necessary */
	if (!test_only) {
		error_code = _verify_node_state(cr_part_ptr, job_ptr, 
						bitmap, cr_type, job_node_req);
		if (error_code != SLURM_SUCCESS) {
			if (save_mem)
				job_ptr->details->job_min_memory = save_mem;
			return error_code;
		}
	}

	/* This is the case if -O/--overcommit  is true */ 
	if (job_ptr->num_procs == job_ptr->details->min_nodes) {
		struct multi_core_data *mc_ptr = job_ptr->details->mc_ptr;
		job_ptr->num_procs *= MAX(1, mc_ptr->min_threads);
		job_ptr->num_procs *= MAX(1, mc_ptr->min_cores);
		job_ptr->num_procs *= MAX(1, mc_ptr->min_sockets);
	}

	debug3("cons_res: cr_job_test: evaluating job %u on %u nodes",
		job_ptr->job_id, bit_set_count(bitmap));

	orig_map = bit_copy(bitmap);
	avail_cores = _make_core_bitmap(bitmap);

	/* test to make sure that this job can succeed with all avail_cores
	 * if 'no' then return FAIL
	 * if 'yes' then we will seek the optimal placement for this job
	 *          within avail_cores
	 */
	free_cores = bit_copy(avail_cores);
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				   bitmap, cr_node_cnt, free_cores, cr_type);
	if (cpu_count == NULL) {
		/* job cannot fit */
		bit_free(orig_map);
		bit_free(free_cores);
		bit_free(avail_cores);
		if (save_mem)
			job_ptr->details->job_min_memory = save_mem;
		debug3("cons_res: cr_job_test: test 0 fail: "
		       "insufficient resources");
		return SLURM_ERROR;
	} else if (test_only) {
		/* FIXME: does "test_only" expect struct_job_res
		 * to be filled out? For now we assume NO */
		bit_free(orig_map);
		bit_free(free_cores);
		bit_free(avail_cores);
		xfree(cpu_count);
		if (save_mem)
			job_ptr->details->job_min_memory = save_mem;
		debug3("cons_res: cr_job_test: test 0 pass: test_only"); 
		return SLURM_SUCCESS;
	}
	if (cr_type == CR_MEMORY) {
		/* CR_MEMORY does not care about existing CPU allocations,
		 * so we can jump right to job allocation from here */
		goto alloc_job;
	}
	xfree(cpu_count);
	debug3("cons_res: cr_job_test: test 0 pass - "
	       "job fits on given resources");

	/* now that we know that this job can run with the given resources,
	 * let's factor in the existing allocations and seek the optimal set
	 * of resources for this job. Here is the procedure:
	 *
	 * Step 1: Seek idle nodes across all partitions. If successful then
	 *         place job and exit. If not successful, then continue:
	 *
	 * Step 2: Remove resources that are in use by higher-pri partitions,
	 *         and test that job can still succeed. If not then exit.
	 *
	 * Step 3: Seek idle nodes among the partitions with the same
	 *         priority as the job's partition. If successful then
	 *         goto Step 6. If not then continue:
	 *
	 * Step 4: Seek placement within the job's partition. Search
	 *         row-by-row. If no placement if found, then exit. If a row
	 *         is found, then continue:
	 *
	 * Step 5: Place job and exit. FIXME! Here is where we need a
	 *         placement algorithm that recognizes existing job
	 *         boundaries and tries to "overlap jobs" as efficiently
	 *         as possible.
	 *
	 * Step 6: Place job and exit. FIXME! here is we use a placement
	 *         algorithm similar to Step 5 on jobs from lower-priority
	 *         partitions.
	 */


	/*** Step 1 ***/
	bit_copybits(bitmap, orig_map);
	bit_copybits(free_cores, avail_cores);

	/* remove all existing allocations from free_cores */
	tmpcore = bit_copy(free_cores);
	for(p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			bit_copybits(tmpcore, p_ptr->row[i].row_bitmap);
			bit_not(tmpcore); /* set bits now "free" resources */
			bit_and(free_cores, tmpcore);
		}
	}
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				   bitmap, cr_node_cnt, free_cores, cr_type);
	if (cpu_count) {
		/* job fits! We're done. */
		debug3("cons_res: cr_job_test: test 1 pass - "
		       "idle resources found");
		goto alloc_job;
	}
	debug3("cons_res: cr_job_test: test 1 fail - "
	       "not enough idle resources");

	/*** Step 2 ***/
	bit_copybits(bitmap, orig_map);
	bit_copybits(free_cores, avail_cores);
	
	for (jp_ptr = cr_part_ptr; jp_ptr; jp_ptr = jp_ptr->next) {
		if (strcmp(jp_ptr->name, job_ptr->part_ptr->name) == 0)
			break;
	}
	if (!jp_ptr)
		fatal("cons_res error: could not find partition for job %u",
			job_ptr->job_id);

	/* remove hi-pri existing allocations from avail_cores */
	for(p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->priority <= jp_ptr->priority)
			continue;
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			bit_copybits(tmpcore, p_ptr->row[i].row_bitmap);
			bit_not(tmpcore); /* set bits now "free" resources */
			bit_and(free_cores, tmpcore);
		}
	}
	/* make these changes permanent */
	bit_copybits(avail_cores, free_cores);
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				   bitmap, cr_node_cnt, free_cores, cr_type);
	if (!cpu_count) {
		/* job needs resources that are currently in use by
		 * higher-priority jobs, so fail for now */
		debug3("cons_res: cr_job_test: test 2 fail - "
			"resources busy with higher priority jobs");
		goto alloc_job;
	}
	xfree(cpu_count);
	debug3("cons_res: cr_job_test: test 2 pass - "
	       "available resources for this priority");

	/*** Step 3 ***/
	bit_copybits(bitmap, orig_map);
	bit_copybits(free_cores, avail_cores);
	
	/* remove same-priority existing allocations from free_cores */
	for(p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->priority != jp_ptr->priority)
			continue;
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			bit_copybits(tmpcore, p_ptr->row[i].row_bitmap);
			bit_not(tmpcore); /* set bits now "free" resources */
			bit_and(free_cores, tmpcore);
		}
	}
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				   bitmap, cr_node_cnt, free_cores, cr_type);
	if (cpu_count) {
		/* lo-pri jobs are the only thing left in our way.
		 * for now we'll ignore them, but FIXME: we need
		 * a good placement algorithm here that optimizes
		 * "job overlap" between this job (in these idle
		 * nodes) and the lo-pri jobs */
		debug3("cons_res: cr_job_test: test 3 pass - found resources");
		goto alloc_job;
	}
	debug3("cons_res: cr_job_test: test 3 fail - "
	       "not enough idle resources in same priority");
	
	
	/*** Step 4 ***/	
	/* try to fit the job into an existing row */
	/*
	 * tmpcore = worker core_bitmap
	 * free_cores = core_bitmap to be built
	 * avail_cores = static core_bitmap of all available cores
	 */
	
	if (jp_ptr->row == NULL) {
		/* there's no existing jobs in this partition, so place
		 * the job in avail_cores. FIXME: still need a good
		 * placement algorithm here that optimizes "job overlap"
		 * between this job (in these idle nodes) and existing
		 * jobs in the other partitions with <= priority to
		 * this partition */
		bit_copybits(bitmap, orig_map);
		bit_copybits(free_cores, avail_cores);
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					   req_nodes, bitmap, cr_node_cnt,
					   free_cores, cr_type);
		debug3("cons_res: cr_job_test: test 4 pass - first row found");
		goto alloc_job;
	}

	cr_sort_part_rows(jp_ptr);
	for (i = 0; i < jp_ptr->num_rows; i++) {
		if (!jp_ptr->row[i].row_bitmap)
			break;
		bit_copybits(bitmap, orig_map);
		bit_copybits(free_cores, avail_cores);
		bit_copybits(tmpcore, jp_ptr->row[i].row_bitmap);
		bit_not(tmpcore);
		bit_and(free_cores, tmpcore);
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					   req_nodes, bitmap, cr_node_cnt,
					   free_cores, cr_type);
		if (cpu_count) {
			debug3("cons_res: cr_job_test: test 4 pass - row %i",i);
			break;
		}
		debug3("cons_res: cr_job_test: test 4 fail - row %i", i);
	}

	if (i < jp_ptr->num_rows && !jp_ptr->row[i].row_bitmap) {
		/* we've found an empty row, so use it */
		bit_copybits(bitmap, orig_map);
		bit_copybits(free_cores, avail_cores);
		debug3("cons_res: cr_job_test: test 4 trying empty row %i",i);
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					   req_nodes, bitmap, cr_node_cnt,
					   free_cores, cr_type);
	}

	if (!cpu_count) {
		/* job can't fit into any row, so exit */
		debug3("cons_res: cr_job_test: test 4 fail - busy partition");
		goto alloc_job;
		
	}

	/*** CONSTRUCTION ZONE FOR STEPs 5 AND 6 ***
	 *  Note that while the job may have fit into a row, it should
	 *  still be run through a good placement algorithm here that
	 * optimizes "job overlap" between this job (in these idle nodes)
	 * and existing jobs in the other partitions with <= priority to
	 * this partition */

alloc_job:
	/* at this point we've found a good set of
	 * bits to allocate to this job:
	 * - bitmap is the set of nodes to allocate
	 * - free_cores is the set of allocated cores
	 * - cpu_count is the number of cpus per allocated node
	 *
	 * Next steps are to cleanup the worker variables,
	 * create the select_job_res struct,
	 * distribute the job on the bits, and exit
	 */
	bit_free(orig_map);
	bit_free(avail_cores);
	if (tmpcore)
		bit_free(tmpcore);
	if (!cpu_count) {
		/* we were sent here to cleanup and exit */
		bit_free(free_cores);
		debug3("cons_res: exiting cr_job_test with no allocation");
		return SLURM_ERROR;
	}

	/* At this point we have:
	 * - a bitmap of selected nodes
	 * - a free_cores bitmap of usable cores on each selected node
	 * - a per-alloc-node cpu_count array
	 */

	if ((mode != SELECT_MODE_WILL_RUN) && (job_ptr->part_ptr == NULL))
		error_code = EINVAL;
	if ((error_code == SLURM_SUCCESS) && (mode == SELECT_MODE_WILL_RUN)) {
		if (job_ptr->details->shared == 0) {
			job_ptr->total_procs = 0;
			for (i = 0; i < cr_node_cnt; i++) {
				if (!bit_test(bitmap, i))
					continue;
				job_ptr->total_procs +=
					select_node_record[i].cpus;
			}
		} else {
			job_ptr->total_procs = job_ptr->num_procs;
			if (job_ptr->details->cpus_per_task &&
			    (job_ptr->details->cpus_per_task != 
			     (uint16_t) NO_VAL)) {
				job_ptr->total_procs *= job_ptr->details->
							cpus_per_task;
			}
		}
	}
	if ((error_code != SLURM_SUCCESS) || (mode != SELECT_MODE_RUN_NOW)) {
		bit_free(free_cores);
		return error_code;
	}

	debug3("cons_res: cr_job_test: distributing job %u", job_ptr->job_id);
	/** create the struct_job_res  **/
	job_res                   = create_select_job_res();
	job_res->node_bitmap      = bit_copy(bitmap);
	if (job_res->node_bitmap == NULL)
		fatal("bit_copy malloc failure");
	job_res->nhosts           = bit_set_count(bitmap);
	job_res->nprocs           = MAX(job_ptr->num_procs, job_res->nhosts);
	job_res->node_req         = job_node_req;
	job_res->cpus             = cpu_count;
	job_res->cpus_used        = xmalloc(job_res->nhosts * sizeof(uint16_t));
	job_res->memory_allocated = xmalloc(job_res->nhosts * sizeof(uint32_t));
	job_res->memory_used      = xmalloc(job_res->nhosts * sizeof(uint32_t));

	/* store the hardware data for the selected nodes */
	error_code = build_select_job_res(job_res, node_record_table_ptr,
					  select_fast_schedule);
	if (error_code != SLURM_SUCCESS) {
		free_select_job_res(&job_res);		
		bit_free(free_cores);
		return error_code;
	}

	/* sync up cpus with layout_ptr, total up
	 * all cpus, and load the core_bitmap */
	ll = -1;
	total_cpus = 0;
	c = 0;
	csize = bit_size(job_res->core_bitmap);
	for (i = 0, n = 0; n < cr_node_cnt; n++) {
		uint32_t j;
		if (layout_ptr && reqmap && bit_test(reqmap,n))
			ll++;
		if (bit_test(bitmap, n) == 0)
			continue;
		j = cr_get_coremap_offset(n);
		for (; j < cr_get_coremap_offset(n+1); j++, c++) {
			if (bit_test(free_cores, j)) {
				if (c >= csize)	{
					fatal("cons_res: cr_job_test "
					      "core_bitmap index error");
				}
				bit_set(job_res->core_bitmap, c);
			}
		}
		
		if (layout_ptr && reqmap && bit_test(reqmap, n)) {
			job_res->cpus[i] = MIN(job_res->cpus[i],layout_ptr[ll]);
		} else if (layout_ptr) {
			job_res->cpus[i] = 0;
		}
		total_cpus += job_res->cpus[i];
		i++;
	}

	/* translate job_res->cpus array into format with rep count */
	build_select_job_res_cpu_array(job_res);

	/* When 'srun --overcommit' is used, nprocs is set to a minimum value
	 * in order to allocate the appropriate number of nodes based on the
	 * job request.
	 * For cons_res, all available logical processors will be allocated on
	 * each allocated node in order to accommodate the overcommit request.
	 */
	if (job_ptr->details->overcommit)
		job_res->nprocs = MIN(total_cpus, job_ptr->details->num_tasks);

	debug3("cons_res: cr_job_test: job %u nprocs %u cbits %u/%u nbits %u",
		job_ptr->job_id, job_res->nprocs, bit_set_count(free_cores),
		bit_set_count(job_res->core_bitmap), job_res->nhosts);
	bit_free(free_cores);

	/* distribute the tasks and clear any unused cores */
	job_ptr->select_job = job_res;
	error_code = cr_dist(job_ptr, cr_type);
	if (error_code != SLURM_SUCCESS) {
		free_select_job_res(&job_ptr->select_job);
		return error_code;
	}

	if ((cr_type != CR_CPU_MEMORY) && (cr_type != CR_CORE_MEMORY) &&
	    (cr_type != CR_SOCKET_MEMORY) && (cr_type != CR_MEMORY))
		return error_code;

	/* load memory allocated array */
	save_mem =job_ptr->details->job_min_memory;
	if (save_mem & MEM_PER_CPU) {
		/* memory is per-cpu */
		save_mem &= (~MEM_PER_CPU);
		for (i = 0; i < job_res->nhosts; i++) {
			job_res->memory_allocated[i] = job_res->cpus[i] *
						       save_mem;
		}
	} else {
		/* memory is per-node */
		for (i = 0; i < job_res->nhosts; i++) {
			job_res->memory_allocated[i] = save_mem;
		}
	}
	return error_code;
}
