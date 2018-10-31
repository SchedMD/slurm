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
 *  Portions Copyright (C) 2010-2015 SchedMD <https://www.schedmd.com>.
 *  Written by Susanne M. Balle <susanne.balle@hp.com>, who borrowed heavily
 *  from select/linear
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

#include <inttypes.h>
#include <time.h>

#include "dist_tasks.h"
#include "job_test.h"
#include "select_cons_res.h"

/* Enables module specific debugging */
#define _DEBUG 0

static uint16_t _allocate_sc(struct job_record *job_ptr, bitstr_t *core_map,
			     bitstr_t *part_core_map, const uint32_t node_i,
			     int *cpu_alloc_size, bool entire_sockets_only);
static int _choose_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			 uint32_t min_nodes, uint32_t max_nodes,
			 uint32_t req_nodes, uint32_t cr_node_cnt,
			 uint16_t *cpu_cnt, uint16_t cr_type,
			 bool prefer_alloc_nodes);
static int _eval_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt, uint16_t cr_type,
			bool prefer_alloc_nodes);
static int _eval_nodes_busy(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt);
static int _eval_nodes_dfly(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt, uint16_t cr_type);
static int _eval_nodes_lln(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt);
static int _eval_nodes_serial(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt);
static int _eval_nodes_spread(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt);
static int _eval_nodes_topo(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt, uint16_t cr_type);
static uint32_t _gres_sock_job_test(List job_gres_list, List node_gres_list,
				    bool use_total_gres, bitstr_t *cpu_bitmap,
				    int cpu_start_bit, int cpu_end_bit,
				    uint32_t job_id, char *node_name,
				    uint32_t node_i, uint32_t s_p_n);
static uint16_t *_select_nodes(struct job_record *job_ptr, uint32_t min_nodes,
				uint32_t max_nodes, uint32_t req_nodes,
				bitstr_t *node_map, uint32_t cr_node_cnt,
				bitstr_t *core_map,
				struct node_use_record *node_usage,
				uint16_t cr_type, bool test_only,
				bitstr_t *part_core_map,
				bool prefer_alloc_nodes);
static uint32_t _socks_per_node(struct job_record *job_ptr);

/* _allocate_sockets - Given the job requirements, determine which sockets
 *                     from the given node can be allocated (if any) to this
 *                     job. Returns the number of cpus that can be used by
 *                     this node AND a core-level bitmap of the selected
 *                     sockets.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores
 * IN part_core_map - bitmap of cores already allocated from this partition
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 */
uint16_t _allocate_sockets(struct job_record *job_ptr, bitstr_t *core_map,
			   bitstr_t *part_core_map, const uint32_t node_i,
			   int *cpu_alloc_size)
{
	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, true);
}

/* _allocate_cores - Given the job requirements, determine which cores
 *                   from the given node can be allocated (if any) to this
 *                   job. Returns the number of cpus that can be used by
 *                   this node AND a bitmap of the selected cores.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - bitmap of cores available for use/selected for use
 * IN part_core_map - bitmap of cores already allocated from this partition
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN cpu_type      - if true, allocate CPUs rather than cores
 */
uint16_t _allocate_cores(struct job_record *job_ptr, bitstr_t *core_map,
			 bitstr_t *part_core_map, const uint32_t node_i,
			 int *cpu_alloc_size, bool cpu_type)
{
	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, false);
}

/* _allocate_sc - Given the job requirements, determine which cores/sockets
 *                from the given node can be allocated (if any) to this
 *                job. Returns the number of cpus that can be used by
 *                this node AND a bitmap of the selected cores.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - bitmap of cores available for use/selected for use
 * IN part_core_map - bitmap of cores already allocated from this partition
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN entire_sockets_only - if true, allocate cores only on sockets that
 *                        - have no other allocated cores.
 * RET - Count of CPUs that can be used on this node. Equal to the count of CPUs
 *       represented by bits set in core_map, less any specialized threads/CPUS.
 */
static uint16_t _allocate_sc(struct job_record *job_ptr, bitstr_t *core_map,
			     bitstr_t *part_core_map, const uint32_t node_i,
			     int *cpu_alloc_size, bool entire_sockets_only)
{
	uint16_t cpu_count = 0, cpu_cnt = 0;
	uint16_t si, cps, avail_cpus = 0, num_tasks = 0;
	uint32_t core_begin    = cr_get_coremap_offset(node_i);
	uint32_t core_end      = cr_get_coremap_offset(node_i+1);
	uint32_t c;
	uint16_t cpus_per_task = job_ptr->details->cpus_per_task;
	uint16_t free_core_count = 0;
	uint16_t i, j, sockets    = select_node_record[node_i].sockets;
	uint16_t cores_per_socket = select_node_record[node_i].cores;
	uint16_t threads_per_core = select_node_record[node_i].vpus;
	uint16_t min_cores = 1, min_sockets = 1, ntasks_per_socket = 0;
	uint16_t ncpus_per_core = 0xffff;	/* Usable CPUs per core */
	uint16_t ntasks_per_core = 0xffff;
	uint32_t free_cpu_count = 0, used_cpu_count = 0;
	int tmp_cpt = 0; /* cpus_per_task */
	uint16_t free_cores[sockets];
	uint16_t used_cores[sockets];
	uint32_t used_cpu_array[sockets];

	memset(free_cores, 0, sockets * sizeof(uint16_t));
	memset(used_cores, 0, sockets * sizeof(uint16_t));
	memset(used_cpu_array, 0, sockets * sizeof(uint32_t));

	if (entire_sockets_only && job_ptr->details->whole_node &&
	    (job_ptr->details->core_spec != NO_VAL16)) {
		/* Ignore specialized cores when allocating "entire" socket */
		entire_sockets_only = false;
	}
	if (job_ptr->details && job_ptr->details->mc_ptr) {
		uint32_t threads_per_socket;
		multi_core_data_t *mc_ptr = job_ptr->details->mc_ptr;
		if (mc_ptr->cores_per_socket != NO_VAL16) {
			min_cores = mc_ptr->cores_per_socket;
		}
		if (mc_ptr->sockets_per_node != NO_VAL16) {
			min_sockets = mc_ptr->sockets_per_node;
		}
		if ((mc_ptr->ntasks_per_core != INFINITE16) &&
		    (mc_ptr->ntasks_per_core)) {
			ntasks_per_core = mc_ptr->ntasks_per_core;
			ncpus_per_core = MIN(threads_per_core,
					     (ntasks_per_core * cpus_per_task));
		}
		if ((mc_ptr->threads_per_core != NO_VAL16) &&
		    (mc_ptr->threads_per_core <  ncpus_per_core)) {
			ncpus_per_core = mc_ptr->threads_per_core;
		}
		*cpu_alloc_size = MIN(*cpu_alloc_size, ncpus_per_core);
		ntasks_per_socket = mc_ptr->ntasks_per_socket;

		if ((ncpus_per_core != NO_VAL16) &&
		    (ncpus_per_core != INFINITE16) &&
		    (ncpus_per_core > threads_per_core)) {
			goto fini;
		}
		threads_per_socket = threads_per_core * cores_per_socket;
		if ((ntasks_per_socket != NO_VAL16) &&
		    (ntasks_per_socket != INFINITE16) &&
		    (ntasks_per_socket > threads_per_socket)) {
			goto fini;
		}
	}

	/* These are the job parameters that we must respect:
	 *
	 *   job_ptr->details->mc_ptr->cores_per_socket (cr_core|cr_socket)
	 *	- min # of cores per socket to allocate to this job
	 *   job_ptr->details->mc_ptr->sockets_per_node (cr_core|cr_socket)
	 *	- min # of sockets per node to allocate to this job
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
	 * Step 2: For core-level and socket-level: apply sockets_per_node
	 *         and cores_per_socket to the "free" cores.
	 *
	 * Step 3: Compute task-related data: ncpus_per_core,
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

	for (c = core_begin; c < core_end; c++) {
		i = (uint16_t) (c - core_begin) / cores_per_socket;
		if (bit_test(core_map, c)) {
			free_cores[i]++;
			free_core_count++;
		} else if (!part_core_map) {
			used_cores[i]++;
		} else if (bit_test(part_core_map, c)) {
			used_cores[i]++;
			used_cpu_array[i]++;
		}
	}

	for (i = 0; i < sockets; i++) {
		/* if a socket is already in use and entire_sockets_only is
		 * enabled, it cannot be used by this job */
		if (entire_sockets_only && used_cores[i]) {
			free_core_count -= free_cores[i];
			used_cores[i] += free_cores[i];
			free_cores[i] = 0;
		}
		free_cpu_count += free_cores[i] * threads_per_core;
		if (used_cpu_array[i])
			used_cpu_count += used_cores[i] * threads_per_core;
	}

	/*
	 * Ignore resources that would push a job allocation over the
	 * partition CPU limit (if any). Remove cores from consideration by
	 * taking them from the sockets with the lowest free_cores count.
	 * This will tend to satisfy a job's --cores-per-socket specification.
	 */
	if ((job_ptr->part_ptr->max_cpus_per_node != INFINITE) &&
	    (free_cpu_count + used_cpu_count >
	     job_ptr->part_ptr->max_cpus_per_node)) {
		int excess = free_cpu_count + used_cpu_count -
			     job_ptr->part_ptr->max_cpus_per_node;
		int min_excess_cores = min_cores;
		int found_cores;
		excess = (excess + threads_per_core - 1) / threads_per_core;
		while (excess > 0) {
			int min_free_inx = -1;
			for (i = 0; i < sockets; i++) {
				if (free_cores[i] == 0)
					continue;
				if (((min_excess_cores > 1) ||
				     (min_sockets > 1)) &&
				    (free_cores[i] <= min_excess_cores))
					continue;
				if ((min_free_inx == -1) ||
				    (free_cores[i] < free_cores[min_free_inx]))
					min_free_inx = i;
			}
			if (min_free_inx == -1) {
				if (min_excess_cores) {
					min_excess_cores = 0;
					continue;
				}
				break;
			}
			if (free_cores[min_free_inx] < excess)
				found_cores = free_cores[min_free_inx];
			else
				found_cores = excess;
			if (min_excess_cores  > 1 &&
			    ((free_cores[min_free_inx] - found_cores) <
			     min_excess_cores)) {
				found_cores = free_cores[min_free_inx] -
					      min_excess_cores;
			}
			free_core_count -= found_cores;
			free_cpu_count -= (found_cores * threads_per_core);
			free_cores[min_free_inx] -= found_cores;
			excess -= found_cores;
		}
	}

	/* Step 2: check min_cores per socket and min_sockets per node */
	j = 0;
	for (i = 0; i < sockets; i++) {
		if (free_cores[i] < min_cores) {
			/* cannot use this socket */
			free_core_count -= free_cores[i];
			free_cores[i] = 0;
			continue;
		}
		/* count this socket as usable */
		j++;
	}
	if (j < min_sockets) {
		/* cannot use this node */
		num_tasks = 0;
		goto fini;
	}

	if (free_core_count < 1) {
		/* no available resources on this node */
		num_tasks = 0;
		goto fini;
	}

	/* Step 3: Compute task-related data:
	 *         ntasks_per_socket, ntasks_per_node and cpus_per_task
	 *         to determine the number of tasks to run on this node
	 *
	 * Note: cpus_per_task and ncpus_per_core need to play nice
	 *       2 tasks_per_core vs. 2 cpus_per_task
	 */
	avail_cpus = 0;
	num_tasks = 0;
	threads_per_core = cr_cpus_per_core(job_ptr->details, node_i);

	for (i = 0; i < sockets; i++) {
		uint16_t tmp = free_cores[i] * threads_per_core;
		avail_cpus += tmp;
		if (ntasks_per_socket)
			num_tasks += MIN(tmp, ntasks_per_socket);
		else
			num_tasks += tmp;
	}

	/* If job requested exclusive rights to the node don't do the
	 * min here since it will make it so we don't allocate the
	 * entire node. */
	if (job_ptr->details->ntasks_per_node && job_ptr->details->share_res)
		num_tasks = MIN(num_tasks, job_ptr->details->ntasks_per_node);

	if (cpus_per_task < 2) {
		avail_cpus = num_tasks;
	} else if ((ntasks_per_core == 1) &&
		   (cpus_per_task > threads_per_core)) {
		/* find out how many cores a task will use */
		int task_cores = (cpus_per_task + threads_per_core - 1) /
				 threads_per_core;
		int task_cpus  = task_cores * threads_per_core;
		/* find out how many tasks can fit on a node */
		int tasks = avail_cpus / task_cpus;
		/* how many cpus the the job would use on the node */
		avail_cpus = tasks * task_cpus;
		/* subtract out the extra cpus. */
		avail_cpus -= (tasks * (task_cpus - cpus_per_task));
	} else {
		j = avail_cpus / cpus_per_task;
		num_tasks = MIN(num_tasks, j);
		avail_cpus = num_tasks * cpus_per_task;
	}

	if ((job_ptr->details->ntasks_per_node &&
	     (num_tasks < job_ptr->details->ntasks_per_node) &&
	     (job_ptr->details->overcommit == 0)) ||
	    (job_ptr->details->pn_min_cpus &&
	     (avail_cpus < job_ptr->details->pn_min_cpus))) {
		/* insufficient resources on this node */
		num_tasks = 0;
		goto fini;
	}

	/* Step 4 - make sure that ntasks_per_socket is enforced when
	 *          allocating cores
	 */
	cps = num_tasks;
	if (ntasks_per_socket >= 1) {
		cps = ntasks_per_socket;
		if (cpus_per_task > 1)
			cps = ntasks_per_socket * cpus_per_task;
	}
	si = 9999;
	tmp_cpt = cpus_per_task;
	for (c = core_begin; c < core_end && avail_cpus > 0; c++) {
		if (bit_test(core_map, c) == 0)
			continue;
		i = (uint16_t) (c - core_begin) / cores_per_socket;
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
			/* we have to ensure that cpu_count
			 * is not bigger than avail_cpus due to
			 * hyperthreading or this would break
			 * the selection logic providing more
			 * cpus than allowed after task-related data
			 * processing of stage 3
			 */
			if (avail_cpus >= threads_per_core) {
				int used;
				if ((ntasks_per_core == 1) &&
				    (cpus_per_task > threads_per_core)) {
					used = MIN(tmp_cpt, threads_per_core);
				} else
					used = threads_per_core;
				avail_cpus -= used;
				cpu_count  += used;

				if (tmp_cpt <= used)
					tmp_cpt = cpus_per_task;
				else
					tmp_cpt -= used;
			} else {
				cpu_count += avail_cpus;
				avail_cpus = 0;
			}

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

	if ((job_ptr->details->core_spec != NO_VAL16) &&
	    (job_ptr->details->core_spec & CORE_SPEC_THREAD)   &&
	    ((select_node_record[node_i].threads == 1) ||
	     (select_node_record[node_i].threads ==
	      select_node_record[node_i].vpus))) {
		/* NOTE: Currently does not support the situation when Slurm
		 * allocates by core the thread specialization count occupies
		 * a full core */
		c = job_ptr->details->core_spec & (~CORE_SPEC_THREAD);
		if (((cpu_count + c) <= select_node_record[node_i].cpus))
			;
		else if (cpu_count > c)
			cpu_count -= c;
		else
			cpu_count = 0;
	}

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
 * IN node_i        - index of node to be evaluated
 * IN s_p_n         - Expected sockets_per_node (NO_VAL if not known)
 * IN cr_type       - Consumable Resource setting
 * IN test_only     - ignore allocated memory check
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to deselect from the core_map to match the cpu_count.
 */
uint16_t _can_job_run_on_node(struct job_record *job_ptr, bitstr_t *core_map,
			      const uint32_t node_i, uint32_t s_p_n,
			      struct node_use_record *node_usage,
			      uint16_t cr_type,
			      bool test_only, bitstr_t *part_core_map)
{
	uint16_t cpus;
	uint64_t avail_mem, req_mem;
	uint32_t gres_cores, gres_cpus, cpus_per_core;
	int core_start_bit, core_end_bit, cpu_alloc_size, i;
	struct node_record *node_ptr = node_record_table_ptr + node_i;
	List gres_list;

	if (((job_ptr->bit_flags & BACKFILL_TEST) == 0) &&
	    !test_only && IS_NODE_COMPLETING(node_ptr)) {
		/* Do not allocate more jobs to nodes with completing jobs,
		 * backfill scheduler independently handles completing nodes */
		cpus = 0;
		return cpus;
	}

	core_start_bit = cr_get_coremap_offset(node_i);
	core_end_bit   = cr_get_coremap_offset(node_i+1) - 1;
	cpus_per_core  = select_node_record[node_i].cpus /
			 (core_end_bit - core_start_bit + 1);
	node_ptr = select_node_record[node_i].node_ptr;
	if (node_usage[node_i].gres_list)
		gres_list = node_usage[node_i].gres_list;
	else
		gres_list = node_ptr->gres_list;

	if (!(job_ptr->bit_flags & GRES_DISABLE_BIND)) {
		gres_plugin_job_core_filter(job_ptr->gres_list, gres_list,
					    test_only, core_map, core_start_bit,
					    core_end_bit, node_ptr->name);
	}
	if (job_ptr->bit_flags & GRES_DISABLE_BIND) {
		gres_cores = NO_VAL;
	} else if (s_p_n == NO_VAL) {
		gres_cores = gres_plugin_job_test(job_ptr->gres_list,
						  gres_list, test_only,
						  core_map, core_start_bit,
						  core_end_bit, job_ptr->job_id,
						  node_ptr->name);
	} else {
		gres_cores = _gres_sock_job_test(job_ptr->gres_list,
						 gres_list, test_only,
						 core_map, core_start_bit,
						 core_end_bit, job_ptr->job_id,
						 node_ptr->name, node_i, s_p_n);
	}
	if (gres_cores == 0)
		return (uint16_t) 0;

	if (cr_type & CR_CORE) {
		/* cpu_alloc_size = CPUs per core */
		cpu_alloc_size = select_node_record[node_i].vpus;
		cpus = _allocate_cores(job_ptr, core_map, part_core_map,
				       node_i, &cpu_alloc_size, false);

	} else if (cr_type & CR_SOCKET) {
		/* cpu_alloc_size = CPUs per socket */
		cpu_alloc_size = select_node_record[node_i].cores *
				 select_node_record[node_i].vpus;
		cpus = _allocate_sockets(job_ptr, core_map, part_core_map,
					 node_i, &cpu_alloc_size);
	} else {
		cpu_alloc_size = 1;
		cpus = _allocate_cores(job_ptr, core_map, part_core_map,
				       node_i, &cpu_alloc_size, true);
	}

	if (cr_type & CR_MEMORY) {
		/* Memory Check: check pn_min_memory to see if:
		 *          - this node has enough memory (MEM_PER_CPU == 0)
		 *          - there are enough free_cores (MEM_PER_CPU == 1)
		 */
		req_mem   = job_ptr->details->pn_min_memory & ~MEM_PER_CPU;
		avail_mem = select_node_record[node_i].real_memory -
			    select_node_record[node_i].mem_spec_limit;
		if (!test_only)
			avail_mem -= node_usage[node_i].alloc_memory;
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
			/* memory is per-cpu */
			if (!(cr_type & CR_CPU) &&
			    job_ptr->details->mc_ptr &&
			    job_ptr->details->mc_ptr->ntasks_per_core == 1 &&
			    job_ptr->details->cpus_per_task == 1) {
				/* In this scenario, cpus represents cores and
				 * the cpu/core count will be inflated later on
				 * to include all of the threads on a core. So
				 * we need to compare apples to apples and only
				 * remove 1 cpu/core at a time. */
				while ((cpus > 0) &&
				       ((req_mem *
					 ((int) cpus *
					  (int) select_node_record[node_i].vpus))
					 > avail_mem))
					cpus -= 1;
			} else {
				while ((req_mem * cpus) > avail_mem) {
					if (cpus >= cpu_alloc_size) {
						cpus -= cpu_alloc_size;
					} else {
						cpus = 0;
						break;
					}
				}
			}

			if (job_ptr->details->cpus_per_task > 1) {
				i = cpus % job_ptr->details->cpus_per_task;
				cpus -= i;
			}
			if (cpus < job_ptr->details->ntasks_per_node)
				cpus = 0;
			/* FIXME: Need to recheck min_cores, etc. here */
		} else {
			/* memory is per node */
			if (req_mem > avail_mem)
				cpus = 0;
		}
	}

	gres_cpus = gres_cores;
	if (gres_cpus != NO_VAL)
		gres_cpus *= cpus_per_core;
	if ((gres_cpus < job_ptr->details->ntasks_per_node) ||
	    ((job_ptr->details->cpus_per_task > 1) &&
	     (gres_cpus < job_ptr->details->cpus_per_task)))
		gres_cpus = 0;

	while (gres_cpus < cpus) {
		if ((int) cpus < cpu_alloc_size) {
			debug3("cons_res: cpu_alloc_size > cpus, cannot "
			       "continue (node: %s)", node_ptr->name);
			cpus = 0;
			break;
		} else {
			cpus -= cpu_alloc_size;
		}
	}

	if (cpus == 0)
		bit_nclear(core_map, core_start_bit, core_end_bit);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: _can_job_run_on_node: %u cpus on %s(%d), "
		     "mem %"PRIu64"/%"PRIu64"",
		     cpus, select_node_record[node_i].node_ptr->name,
		     node_usage[node_i].node_state,
		     node_usage[node_i].alloc_memory,
		     select_node_record[node_i].real_memory);
	}

	return cpus;
}


/* Test to see if a node already has running jobs for _other_ partitions.
 * If (sharing_only) then only check sharing partitions. This is because
 * the job was submitted to a single-row partition which does not share
 * allocated CPUs with multi-row partitions.
 */
static int _is_node_busy(struct part_res_record *p_ptr, uint32_t node_i,
			 int sharing_only, struct part_record *my_part_ptr,
			 bool qos_preemptor)
{
	uint32_t r, cpu_begin = cr_get_coremap_offset(node_i);
	uint32_t i, cpu_end   = cr_get_coremap_offset(node_i+1);
	uint16_t num_rows;

	for (; p_ptr; p_ptr = p_ptr->next) {
		num_rows = p_ptr->num_rows;
		if (preempt_by_qos && !qos_preemptor)
			num_rows--;	/* Don't use extra row */
		if (sharing_only &&
		    ((num_rows < 2) || (p_ptr->part_ptr == my_part_ptr)))
			continue;
		if (!p_ptr->row)
			continue;
		for (r = 0; r < num_rows; r++) {
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
 * Remove nodes from node_bitmap that don't have enough memory or gres to
 * support the job.
 *
 * Return SLURM_ERROR if a required node can't be used.
 *
 * if node_state = NODE_CR_RESERVED, clear node_bitmap (if node is required
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
			      struct job_record *job_ptr,
			      bitstr_t *node_bitmap,
			      uint16_t cr_type,
			      struct node_use_record *node_usage,
			      enum node_cr_state job_node_req,
			      bitstr_t *exc_core_bitmap, bool qos_preemptor)
{
	struct node_record *node_ptr;
	uint32_t j, gres_cpus, gres_cores;
	uint64_t free_mem, min_mem;
	int core_start_bit, core_end_bit, cpus_per_core;
	List gres_list;
	int i, i_first, i_last;

	if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
		uint16_t min_cpus;
		min_mem = job_ptr->details->pn_min_memory & (~MEM_PER_CPU);
		min_cpus = MAX(job_ptr->details->ntasks_per_node,
			       job_ptr->details->pn_min_cpus);
		min_cpus = MAX(min_cpus, job_ptr->details->cpus_per_task);
		if (min_cpus > 0)
			min_mem *= min_cpus;
	} else {
		min_mem = job_ptr->details->pn_min_memory;
	}
	i_first = bit_ffs(node_bitmap);
	if (i_first == -1)
		i_last = -2;
	else
		i_last  = bit_fls(node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		node_ptr = select_node_record[i].node_ptr;
		core_start_bit = cr_get_coremap_offset(i);
		core_end_bit   = cr_get_coremap_offset(i+1) - 1;
		cpus_per_core  = select_node_record[i].cpus /
				 (core_end_bit - core_start_bit + 1);
		/* node-level memory check */
		if ((job_ptr->details->pn_min_memory) &&
		    (cr_type & CR_MEMORY)) {
			if (select_node_record[i].real_memory >
			    node_usage[i].alloc_memory)
				free_mem = select_node_record[i].real_memory -
					   node_usage[i].alloc_memory;
			else
				free_mem = 0;
			if (free_mem < min_mem) {
				debug3("cons_res: _vns: node %s no mem %"
					""PRIu64" < %"PRIu64"",
					select_node_record[i].node_ptr->name,
					free_mem, min_mem);
				goto clear_bit;
			}
		} else if (cr_type & CR_MEMORY) {   /* --mem=0 for all memory */
			if (node_usage[i].alloc_memory) {
				debug3("cons_res: _vns: node %s mem in use %"
					""PRIu64"",
					select_node_record[i].node_ptr->name,
					node_usage[i].alloc_memory);
				goto clear_bit;
			}
		}

		/* Exclude nodes with reserved cores */
		if ((job_ptr->details->whole_node == 1) && exc_core_bitmap) {
			for (j = core_start_bit; j <= core_end_bit; j++) {
				if (bit_test(exc_core_bitmap, j))
					continue;
				debug3("cons_res: _vns: node %s exc",
				       select_node_record[i].node_ptr->name);
				goto clear_bit;
			}
		}

		/* node-level gres check */
		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_cores = gres_plugin_job_test(job_ptr->gres_list,
						  gres_list, true,
						  NULL, 0, 0, job_ptr->job_id,
						  node_ptr->name);
		gres_cpus = gres_cores;
		if (gres_cpus != NO_VAL)
			gres_cpus *= cpus_per_core;
		if (gres_cpus == 0) {
			debug3("cons_res: _vns: node %s lacks gres",
			       node_ptr->name);
			goto clear_bit;
		}

		/* exclusive node check */
		if (node_usage[i].node_state >= NODE_CR_RESERVED) {
			debug3("cons_res: _vns: node %s in exclusive use",
			       node_ptr->name);
			goto clear_bit;

		/* non-resource-sharing node check */
		} else if (node_usage[i].node_state >= NODE_CR_ONE_ROW) {
			if ((job_node_req == NODE_CR_RESERVED) ||
			    (job_node_req == NODE_CR_AVAILABLE)) {
				debug3("cons_res: _vns: node %s non-sharing",
				       node_ptr->name);
				goto clear_bit;
			}
			/* cannot use this node if it is running jobs
			 * in sharing partitions */
			if (_is_node_busy(cr_part_ptr, i, 1,
					  job_ptr->part_ptr, qos_preemptor)) {
				debug3("cons_res: _vns: node %s sharing?",
				       node_ptr->name);
				goto clear_bit;
			}

		/* node is NODE_CR_AVAILABLE - check job request */
		} else {
			if (job_node_req == NODE_CR_RESERVED) {
				if (_is_node_busy(cr_part_ptr, i, 0,
						  job_ptr->part_ptr,
						  qos_preemptor)) {
					debug3("cons_res: _vns: node %s busy",
					       node_ptr->name);
					goto clear_bit;
				}
			} else if (job_node_req == NODE_CR_ONE_ROW) {
				/* cannot use this node if it is running jobs
				 * in sharing partitions */
				if (_is_node_busy(cr_part_ptr, i, 1,
						  job_ptr->part_ptr,
						  qos_preemptor)) {
					debug3("cons_res: _vns: node %s vbusy",
					       node_ptr->name);
					goto clear_bit;
				}
			}
		}
		continue;	/* node is usable, test next node */

clear_bit:	/* This node is not usable by this job */
		bit_clear(node_bitmap, i);
		if (job_ptr->details->req_node_bitmap &&
		    bit_test(job_ptr->details->req_node_bitmap, i))
			return SLURM_ERROR;

	}

	return SLURM_SUCCESS;
}

/*
 * Given an available node_bitmap, return a corresponding available core_bitmap,
 *	excluding all specialized cores.
 *
 * node_map IN - Bitmap of available nodes
 * core_spec IN - Count of specialized cores requested by the job or NO_VAL
 * RET bitmap of cores available for use by this job or reservation
 * NOTE: Call bit_free() on return value to avoid memory leak.
 */
extern bitstr_t *make_core_bitmap(bitstr_t *node_map, uint16_t core_spec)
{
	uint32_t c, nodes, size;
	int res_core, res_sock, res_off;
	int n, n_first, n_last;
	uint32_t coff;
	uint16_t spec_cores, i, use_spec_cores;
	struct node_record *node_ptr;
	int from_core, to_core, incr_core, from_sock, to_sock, incr_sock;

	nodes = bit_size(node_map);
	size = cr_get_coremap_offset(nodes);
	bitstr_t *core_map = bit_alloc(size);

	if ((core_spec != NO_VAL16) &&
	    (core_spec & CORE_SPEC_THREAD))	/* Reserving threads */
		core_spec = NO_VAL16;	/* Don't remove cores */

	n_first = bit_ffs(node_map);
	if (n_first == -1)
		n_last = -2;
	else
		n_last = bit_fls(node_map);
	for (n = n_first; n <= n_last; n++) {
		if (!bit_test(node_map, n))
			continue;
		c    = cr_get_coremap_offset(n);
		coff = cr_get_coremap_offset(n+1);
		if ((core_spec != NO_VAL16) &&
		    (core_spec >= (coff - c))) {
			bit_clear(node_map, n);
			continue;
		}
		bit_nset(core_map, c, coff - 1);

		node_ptr = select_node_record[n].node_ptr;
		use_spec_cores =  slurm_get_use_spec_resources();
		if (use_spec_cores && (core_spec == 0))
			continue;

		/* remove node's specialized cores accounting toward the
		 * requested limit if allowed by configuration */
		spec_cores = core_spec;
		if (node_ptr->node_spec_bitmap) {
			for (i = 0; i < (coff - c); i++) {
				if (!bit_test(node_ptr->node_spec_bitmap, i)) {
		 			bit_clear(core_map, c + i);
					if (!use_spec_cores)
						continue;
					if (--spec_cores == 0)
						break;
				}
			}
		}

		/* if enough cores specialized or not necessary to
		 * specialize some of them for the job, continue */
		if (!use_spec_cores || (spec_cores == 0) ||
		    (core_spec == NO_VAL16))
			continue;

		/* if more cores need to be specialized, look for
		 * them in the non-specialized cores */
		if (spec_cores_first) {
			from_core = 0;
			to_core   = select_node_record[n].cores;
			incr_core = 1;
			from_sock = 0;
			to_sock   = select_node_record[n].sockets;
			incr_sock = 1;
		} else {
			from_core = select_node_record[n].cores - 1;
			to_core   = -1;
			incr_core = -1;
			from_sock = select_node_record[n].sockets - 1;
			to_sock   = -1;
			incr_sock = -1;
		}
		for (res_core = from_core;
		     ((spec_cores > 0) && (res_core != to_core));
		     res_core += incr_core) {
			for (res_sock = from_sock;
			     ((spec_cores > 0) && (res_sock != to_sock));
			     res_sock += incr_sock) {
				res_off = (res_sock*select_node_record[n].cores)
					  + res_core;
				if (bit_test(core_map, c + res_off)) {
					bit_clear(core_map, c + res_off);
					spec_cores--;
				}
			}
		}
	}
	return core_map;
}

/*
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN cpu_bitmap     - Identification of available CPUs (NULL if no restriction)
 * IN cpu_start_bit  - index into cpu_bitmap for this node's first CPU
 * IN cpu_end_bit    - index into cpu_bitmap for this node's last CPU
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * IN node_i - Node index
 * IN s_p_n - Sockets per node required by this job or NO_VAL
 * RET: NO_VAL    - All cores on node are available
 *      otherwise - Count of available cores
 */
static uint32_t _gres_sock_job_test(List job_gres_list, List node_gres_list,
				    bool use_total_gres, bitstr_t *core_bitmap,
				    int core_start_bit, int core_end_bit,
				    uint32_t job_id, char *node_name,
				    uint32_t node_i, uint32_t s_p_n)
{
	uint32_t core_cnt, sock_cnt, cores_per_sock;
	uint32_t *avail_cores, result_cores;
	bitstr_t **sock_core_bitmap, *other_node_cores;
	int i, j;
	int core_bit_cnt, core_inx, sock_inx, best_socket;

	if ((s_p_n == NO_VAL) || (core_bitmap == NULL) ||
	    (select_node_record == NULL) ||
	    ((sock_cnt = select_node_record[node_i].sockets) < 2) ||
	    (sock_cnt <= s_p_n)) {
		/* No socket filtering possible, use all sockets */
		return gres_plugin_job_test(job_gres_list, node_gres_list,
					    use_total_gres, core_bitmap,
					    core_start_bit, core_end_bit,
				 	    job_id, node_name);
	}

	/* Build local data structures */
	core_cnt = core_end_bit - core_start_bit + 1;
	cores_per_sock = core_cnt / sock_cnt;
	core_bit_cnt = bit_size(core_bitmap);
	sock_core_bitmap = xmalloc(sizeof(bitstr_t *) * sock_cnt);
	for (i = 0; i < sock_cnt; i++)
		sock_core_bitmap[i] = bit_alloc(core_bit_cnt);
	other_node_cores = bit_copy(core_bitmap);
	for (i = core_start_bit, core_inx = 0, sock_inx = 0;
	     i <= core_end_bit; i++) {
		if (core_inx >= cores_per_sock) {
			core_inx = 0;
			sock_inx++;
		}
		if (bit_test(core_bitmap, i)) {
			bit_set(sock_core_bitmap[sock_inx], i);
			bit_clear(other_node_cores, i);
		}
		core_inx++;
	}

	/* Determine how many cores are available from each socket starting
	 * position and moving forward by s_p_n sockets. In order to keep
	 * the overhead/time and complexity reasonable, we only consider
	 * using consecutive sockets. */
	avail_cores = xmalloc(sizeof(uint32_t) * sock_cnt);
	s_p_n = MAX(s_p_n, 1);
	s_p_n = MIN(s_p_n, sock_cnt);
	for (i = 0; i <= (sock_cnt - s_p_n); i++) {
		for (j = 1; j < s_p_n; j++)
			bit_or(sock_core_bitmap[i], sock_core_bitmap[i+j]);
		avail_cores[i] = gres_plugin_job_test(job_gres_list,
					node_gres_list, use_total_gres,
					sock_core_bitmap[i], core_start_bit,
					core_end_bit, job_id, node_name);
	}

	/* Identify the best sockets */
	best_socket = -1;
	for (i = 0; i <= (sock_cnt - s_p_n); i++) {
		if ((best_socket == -1) ||
		    (avail_cores[i] > avail_cores[best_socket]))
			best_socket = i;
	}
	result_cores = avail_cores[best_socket];
	bit_and(core_bitmap, sock_core_bitmap[best_socket]);
	bit_or(core_bitmap, other_node_cores);

	/* Free local data structures */
	bit_free(other_node_cores);
	for (i = 0; i < sock_cnt; i++)
		bit_free(sock_core_bitmap[i]);
	xfree(sock_core_bitmap);
	xfree(avail_cores);

	return result_cores;
}

/* Determine how many sockets per node this job requires for GRES */
static uint32_t _socks_per_node(struct job_record *job_ptr)
{
	multi_core_data_t *mc_ptr;
	uint32_t s_p_n = NO_VAL;
	uint32_t cpu_cnt, cpus_per_node, tasks_per_node;
	uint32_t min_nodes;

	if ((job_ptr->details == NULL) || (job_ptr->gres_list == NULL) ||
	    ((job_ptr->bit_flags & GRES_ENFORCE_BIND) == 0))
		return s_p_n;

	cpu_cnt = job_ptr->details->num_tasks * job_ptr->details->cpus_per_task;
	cpu_cnt = MAX(job_ptr->details->min_cpus, cpu_cnt);
	min_nodes = MAX(job_ptr->details->min_nodes, 1);
	cpus_per_node = cpu_cnt / min_nodes;
	if (cpus_per_node <= 1)
		return (uint32_t) 1;

	mc_ptr = job_ptr->details->mc_ptr;
	if ((mc_ptr->ntasks_per_socket != NO_VAL16) &&
	    (mc_ptr->ntasks_per_socket != INFINITE16)) {
		tasks_per_node = job_ptr->details->num_tasks / min_nodes;
		s_p_n = (tasks_per_node + mc_ptr->ntasks_per_socket - 1) /
			mc_ptr->ntasks_per_socket;
		return s_p_n;
	}

	/* This logic could be expanded to support additional cases, which may
	 * require information per node information (e.g. threads per core). */

	return s_p_n;
}

/* Compute resource usage for the given job on all available resources
 *
 * IN: job_ptr     - pointer to the job requesting resources
 * IN: node_map    - bitmap of available nodes
 * IN/OUT: core_map - bitmap of available cores
 * IN: cr_node_cnt - total number of nodes in the cluster
 * IN: cr_type     - resource type
 * OUT: cpu_cnt    - number of cpus that can be used by this job
 * IN: test_only   - ignore allocated memory check
 */
static void _get_res_usage(struct job_record *job_ptr, bitstr_t *node_map,
			   bitstr_t *core_map, uint32_t cr_node_cnt,
			   struct node_use_record *node_usage,
			   uint16_t cr_type, uint16_t **cpu_cnt_ptr,
			   bool test_only, bitstr_t *part_core_map)
{
	uint16_t *cpu_cnt;
	uint32_t n;
	uint32_t s_p_n = _socks_per_node(job_ptr);

	cpu_cnt = xmalloc(cr_node_cnt * sizeof(uint16_t));
	for (n = 0; n < cr_node_cnt; n++) {
		if (!bit_test(node_map, n))
			continue;
		cpu_cnt[n] = _can_job_run_on_node(job_ptr, core_map, n, s_p_n,
						  node_usage, cr_type,
						  test_only, part_core_map);
	}
	*cpu_cnt_ptr = cpu_cnt;
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

static void _cpus_to_use(int *avail_cpus, int rem_cpus, int rem_nodes,
			 struct job_details *details_ptr, uint16_t *cpu_cnt,
			 int node_inx, uint16_t cr_type)
{
	int resv_cpus;	/* CPUs to be allocated on other nodes */

	if (details_ptr->whole_node == 1)	/* Use all CPUs on this node */
		return;

	resv_cpus = MAX((rem_nodes - 1), 0);
	resv_cpus *= cr_cpus_per_core(details_ptr, node_inx);
	if (cr_type & CR_SOCKET)
		resv_cpus *= select_node_record[node_inx].cores;
	rem_cpus -= resv_cpus;

	if (*avail_cpus > rem_cpus) {
		*avail_cpus = MAX(rem_cpus, (int)details_ptr->pn_min_cpus);
		/* Round up CPU count to CPU in allocation unit (e.g. core) */
		*cpu_cnt = *avail_cpus;
	}
}

/* this is the heart of the selection process */
static int _eval_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt, uint16_t cr_type,
			bool prefer_alloc_nodes)
{
	int i, j, error_code = SLURM_ERROR;
	int *consec_nodes;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_cpus;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required
				 * (in req_bitmap) */
	int consec_index, consec_size, sufficient;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_sufficient, best_fit_index = 0;
	int avail_cpus;
	bool required_node;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map    = details_ptr->req_node_bitmap;

	xassert(node_map);
	if (cr_node_cnt != node_record_count) {
		error("cons_res: node count inconsistent with slurmctld");
		return error_code;
	}
	if (bit_set_count(node_map) < min_nodes)
		return error_code;

	if ((details_ptr->req_node_bitmap) &&
	    (!bit_super_set(details_ptr->req_node_bitmap, node_map)))
		return error_code;

	if (job_ptr->bit_flags & SPREAD_JOB) {
		/* Spread the job out over many nodes */
		return _eval_nodes_spread(job_ptr, node_map,
					  min_nodes, max_nodes, req_nodes,
					  cr_node_cnt, cpu_cnt);
	}

	if (prefer_alloc_nodes && !details_ptr->contiguous) {
		/* Select resource on busy nodes first in order to leave
		 * idle resources free for as long as possible so that longer
		 * running jobs can get more easily started by the backfill
		 * scheduler plugin */
		return _eval_nodes_busy(job_ptr, node_map,
				       min_nodes, max_nodes, req_nodes,
				       cr_node_cnt, cpu_cnt);
	}

	if ((cr_type & CR_LLN) ||
	    (job_ptr->part_ptr &&
	     (job_ptr->part_ptr->flags & PART_FLAG_LLN))) {
		/* Select resource on the Least Loaded Node */
		return _eval_nodes_lln(job_ptr, node_map,
				       min_nodes, max_nodes, req_nodes,
				       cr_node_cnt, cpu_cnt);
	}

	if (pack_serial_at_end &&
	    (details_ptr->min_cpus == 1) && (req_nodes == 1)) {
		/* Put serial jobs at the end of the available node list
		 * rather than using a best-fit algorithm, which fragments
		 * resources. */
		return _eval_nodes_serial(job_ptr, node_map,
					  min_nodes, max_nodes, req_nodes,
					  cr_node_cnt, cpu_cnt);
	}

	if (switch_record_cnt && switch_record_table &&
	    ((topo_optional == false) || job_ptr->req_switch)) {
		/* Perform optimized resource selection based upon topology */
		if (have_dragonfly) {
			return _eval_nodes_dfly(job_ptr, node_map,
						min_nodes, max_nodes, req_nodes,
						cr_node_cnt, cpu_cnt, cr_type);
		} else {
			return _eval_nodes_topo(job_ptr, node_map,
						min_nodes, max_nodes, req_nodes,
						cr_node_cnt, cpu_cnt, cr_type);
		}
	}

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

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;

	for (i = 0; i < cr_node_cnt; i++) {
		if (req_map)
			required_node = bit_test(req_map, i);
		else
			required_node = false;
		if (bit_test(node_map, i)) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = i;
			avail_cpus = cpu_cnt[i];
			if ((max_nodes > 0) && required_node) {
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = i;
				}
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				/* leaving bitmap set, decrement max limit */
				max_nodes--;
			} else {	/* node not selected (yet) */
				bit_clear(node_map, i);
				consec_cpus[consec_index] += avail_cpus;
				consec_nodes[consec_index]++;
			}
		} else if (consec_nodes[consec_index] == 0) {
			consec_req[consec_index] = -1;
			/* already picked up any required nodes */
			/* re-use this record */
		} else {
			consec_end[consec_index] = i - 1;
			if (++consec_index >= consec_size) {
				consec_size *= 2;
				xrealloc(consec_cpus, sizeof(int)*consec_size);
				xrealloc(consec_nodes,sizeof(int)*consec_size);
				xrealloc(consec_start,sizeof(int)*consec_size);
				xrealloc(consec_end,  sizeof(int)*consec_size);
				xrealloc(consec_req,  sizeof(int)*consec_size);
			}
			consec_cpus[consec_index]  = 0;
			consec_nodes[consec_index] = 0;
			consec_req[consec_index]   = -1;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = i - 1;

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < consec_index; i++) {
			info("cons_res: eval_nodes:%d consec "
			     "c=%d n=%d b=%d e=%d r=%d",
			     i, consec_cpus[i], consec_nodes[i],
			     consec_start[i], consec_end[i], consec_req[i]);
		}
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/* accumulate nodes from these sets of consecutive nodes until */
	/*   sufficient resources have been accumulated */
	while (consec_index && (max_nodes > 0)) {
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		best_fit_req = -1;	/* first required node, -1 if none */
		for (i = 0; i < consec_index; i++) {
			if (consec_nodes[i] == 0)
				continue;	/* no usable nodes here */

			if (details_ptr->contiguous &&
			    details_ptr->req_node_bitmap &&
			    (consec_req[i] == -1))
				continue;  /* not required nodes */

			sufficient = (consec_cpus[i] >= rem_cpus) &&
				     _enough_nodes(consec_nodes[i], rem_nodes,
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

			if (details_ptr->contiguous &&
			    details_ptr->req_node_bitmap) {
				/* Must wait for all required nodes to be
				 * in a single consecutive block */
				int j, other_blocks = 0;
				for (j = (i+1); j < consec_index; j++) {
					if (consec_req[j] != -1) {
						other_blocks = 1;
						break;
					}
				}
				if (other_blocks) {
					best_fit_nodes = 0;
					break;
				}
			}
		}
		if (best_fit_nodes == 0)
			break;

		if (details_ptr->contiguous &&
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
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i)) {
					/* required node already in set */
					continue;
				}
				avail_cpus = cpu_cnt[i];
				if (avail_cpus <= 0)
					continue;

				/* This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out. */
				_cpus_to_use(&avail_cpus, rem_cpus,
					     min_rem_nodes, details_ptr,
					     &cpu_cnt[i], i, cr_type);
				total_cpus += avail_cpus;
				/* enforce the max_cpus limit */
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("1 can't use this node "
					       "since it would put us "
					       "over the limit");
					total_cpus -= avail_cpus;
					continue;
				}
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus -= avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_index]; i--) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i))
					continue;
				avail_cpus = cpu_cnt[i];
				if (avail_cpus <= 0)
					continue;

				/* This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out. */
				_cpus_to_use(&avail_cpus, rem_cpus,
					     min_rem_nodes, details_ptr,
					     &cpu_cnt[i], i, cr_type);
				total_cpus += avail_cpus;
				/* enforce the max_cpus limit */
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("2 can't use this node "
					       "since it would put us "
					       "over the limit");
					total_cpus -= avail_cpus;
					continue;
				}
				rem_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
			}
		} else {
			/* No required nodes, try best fit single node */
			int *cpus_array = NULL, array_len;
			int best_fit = -1, best_size = 0;
			int first = consec_start[best_fit_index];
			int last  = consec_end[best_fit_index];
			if (rem_nodes <= 1) {
				array_len =  last - first + 1;
				cpus_array = xmalloc(sizeof(int) * array_len);
				for (i = first, j = 0; i <= last; i++, j++) {
					if (bit_test(node_map, i))
						continue;
					cpus_array[j] = cpu_cnt[i];
					if (cpus_array[j] < rem_cpus)
						continue;
					if ((best_fit == -1) ||
					    (cpus_array[j] < best_size)) {
						best_fit = j;
						best_size = cpus_array[j];
						if (best_size == rem_cpus)
							break;
					}
				}
				/* If we found a single node to use,
				 * clear cpu counts for all other nodes */
				for (i = first, j = 0;
				     ((i <= last) && (best_fit != -1));
				     i++, j++) {
					if (j != best_fit)
						cpus_array[j] = 0;
				}
			}

			for (i = first, j = 0; i <= last; i++, j++) {
				if ((max_nodes <= 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0)))
					break;
				if (bit_test(node_map, i))
					continue;

				if (cpus_array)
					avail_cpus = cpus_array[j];
				else
					avail_cpus = cpu_cnt[i];
				if (avail_cpus <= 0)
					continue;

				if ((max_nodes == 1) &&
				    (avail_cpus < rem_cpus)) {
					/* Job can only take one more node and
					 * this one has insufficient CPU */
					continue;
				}

				/* This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out. */
				_cpus_to_use(&avail_cpus, rem_cpus,
					     min_rem_nodes, details_ptr,
					     &cpu_cnt[i], i, cr_type);
				total_cpus += avail_cpus;
				/* enforce the max_cpus limit */
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("3 can't use this node "
					       "since it would put us "
					       "over the limit");
					total_cpus -= avail_cpus;
					continue;
				}
				rem_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
			}
			xfree(cpus_array);
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0)) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_index] = 0;
		consec_nodes[best_fit_index] = 0;
	}

	if (error_code && (rem_cpus <= 0) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes))
		error_code = SLURM_SUCCESS;

fini:	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources using as many nodes as
 * possible. Based upon _eval_nodes_busy().
 */
static int _eval_nodes_spread(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus = 0;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = cpu_cnt[i];
				if ((avail_cpus > 0) && (max_nodes > 0)) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (cr_node_cnt - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		return error_code;
	}

	for (i = i_start; i <= i_end; i++) {
		avail_cpus = cpu_cnt[i];
		if ((avail_cpus > 0) && (max_nodes > 0)) {
			bit_set(node_map, i);
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			if (max_nodes <= 0)
				break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, cr_node_cnt-1); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources starting with allocated
 * nodes. Based upon _eval_nodes_lln().
 */
static int _eval_nodes_busy(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus = 0;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = cpu_cnt[i];
				if ((avail_cpus > 0) && (max_nodes > 0)) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (cr_node_cnt - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		return error_code;
	}

	/* Start by using nodes that already have a job running */
	for (i = i_start; i <= i_end; i++) {
		if (bit_test(node_map, i) ||
		    bit_test(idle_node_bitmap, i))
			continue;
		avail_cpus = cpu_cnt[i];
		if ((avail_cpus > 0) && (max_nodes > 0)) {
			bit_set(node_map, i);
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			if ((max_nodes <= 0) ||
			    ((rem_cpus <= 0) && (rem_nodes <= 0)))
				break;
		}
	}

	/* Now try to use idle nodes */
	for (i = i_start; i <= i_end; i++) {
		if (bit_test(node_map, i) ||
		    !bit_test(idle_node_bitmap, i))
			continue;
		avail_cpus = cpu_cnt[i];
		if ((avail_cpus > 0) && (max_nodes > 0)) {
			bit_set(node_map, i);
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			if ((max_nodes <= 0) ||
			    ((rem_cpus <= 0) && (rem_nodes <= 0)))
				break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, cr_node_cnt-1); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources on the least loaded nodes */
static int _eval_nodes_lln(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	int last_max_cpu_cnt = -1;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = cpu_cnt[i];
				if (max_nodes > 0) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (cr_node_cnt - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/* Accumulate nodes from those with highest available CPU count.
	 * Logic is optimized for small node/CPU count allocations.
	 * For larger allocation, use list_sort(). */
	while (((rem_cpus > 0) || (rem_nodes > 0)) && (max_nodes > 0)) {
		int max_cpu_idx = -1;
		for (i = i_start; i <= i_end; i++) {
			if (bit_test(node_map, i))
				continue;
			if ((max_cpu_idx == -1) ||
			    (cpu_cnt[max_cpu_idx] < cpu_cnt[i])) {
				max_cpu_idx = i;
				if (cpu_cnt[max_cpu_idx] == last_max_cpu_cnt)
					break;
			}
		}
		if ((max_cpu_idx == -1) || (cpu_cnt[max_cpu_idx] == 0))
			break;
		last_max_cpu_cnt = cpu_cnt[max_cpu_idx];
		avail_cpus = cpu_cnt[max_cpu_idx];
		if (avail_cpus) {
			rem_cpus -= avail_cpus;
			bit_set(node_map, max_cpu_idx);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
		} else {
			break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, cr_node_cnt-1); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

fini:	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources at the end of the node
 * list to reduce fragmentation */
static int _eval_nodes_serial(struct job_record *job_ptr, bitstr_t *node_map,
			      uint32_t min_nodes, uint32_t max_nodes,
			      uint32_t req_nodes, uint32_t cr_node_cnt,
			      uint16_t *cpu_cnt)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int avail_cpus;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	rem_cpus = details_ptr->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;
	i_start = bit_ffs(node_map);
	if (i_start >= 0)
		i_end = bit_fls(node_map);
	else
		i_end = i_start - 1;
	if (req_map) {
		for (i = i_start; i <= i_end; i++) {
			if (!bit_test(req_map, i)) {
				bit_clear(node_map, i);
				continue;
			}
			if (bit_test(node_map, i)) {
				avail_cpus = cpu_cnt[i];
				if (max_nodes > 0) {
					total_cpus += avail_cpus;
					rem_cpus   -= avail_cpus;
					rem_nodes--;
					min_rem_nodes--;
					/* leaving bitmap set, decr max limit */
					max_nodes--;
				} else {	/* node not selected (yet) */
					bit_clear(node_map, i);
				}
			}
		}
	} else {
		bit_nclear(node_map, 0, (cr_node_cnt - 1));
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	while (((rem_cpus > 0) || (rem_nodes > 0)) && (max_nodes > 0)) {
		int max_cpu_idx = -1;
		for (i = i_end; i >= i_start; i--) {
			if (bit_test(node_map, i))
				continue;
			if (cpu_cnt[i]) {
				max_cpu_idx = i;
				break;
			}
		}
		if ((max_cpu_idx == -1) || (cpu_cnt[max_cpu_idx] == 0))
			break;
		avail_cpus = cpu_cnt[max_cpu_idx];
		if (avail_cpus) {
			rem_cpus -= avail_cpus;
			bit_set(node_map, max_cpu_idx);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
		} else {
			break;
		}
	}

	if ((rem_cpus > 0) || (min_rem_nodes > 0))  {
		bit_nclear(node_map, 0, cr_node_cnt-1); /* Clear Map. */
		error_code = SLURM_ERROR;
	} else
		error_code = SLURM_SUCCESS;

fini:	return error_code;
}

/*
 * A network topology aware version of _eval_nodes().
 * NOTE: The logic here is almost identical to that of _job_test_topo()
 *       in select_linear.c. Any bug found here is probably also there.
 */
static int _eval_nodes_topo(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt, uint16_t cr_type)
{
	bitstr_t **switches_bitmap = NULL;	/* nodes on this switch */
	int       *switches_cpu_cnt = NULL;	/* total CPUs on switch */
	int       *switches_node_cnt = NULL;	/* total nodes on switch */
	int       *switches_required = NULL;	/* set if has required node */
	int        leaf_switch_count = 0;   /* Count of leaf node switches used */

	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int avail_cpus;
	int total_cpus = 0;	/* #CPUs allocated to job */
	int i, j, rc = SLURM_SUCCESS;
	int best_fit_inx, first, last;
	int best_fit_nodes, best_fit_cpus;
	int best_fit_location = 0, best_fit_sufficient;
	bool sufficient;
	long time_waiting = 0;

	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = job_ptr->details->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;

	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		i = bit_set_count(req_nodes_bitmap);
		if (i > max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, i, max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
	}

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switches_bitmap   = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switches_cpu_cnt  = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_cnt = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_required = xmalloc(sizeof(int)        * switch_record_cnt);
	avail_nodes_bitmap = bit_alloc(cr_node_cnt);
	for (i=0; i<switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], bitmap);
		bit_or(avail_nodes_bitmap, switches_bitmap[i]);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
		if (req_nodes_bitmap &&
		    bit_overlap(req_nodes_bitmap, switches_bitmap[i])) {
			switches_required[i] = 1;
		}
	}
	bit_nclear(bitmap, 0, cr_node_cnt - 1);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i=0; i<switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switches_node_cnt[i]) {
				node_names = bitmap2node_name(
						switches_bitmap[i]);
			}
			info("switch=%s level=%d nodes=%u:%s required:%u speed:%u",
			     switch_record_table[i].name,
			     switch_record_table[i].level,
			     switches_node_cnt[i], node_names,
			     switches_required[i],
			     switch_record_table[i].link_speed);
			xfree(node_names);
		}
	}

	if (req_nodes_bitmap &&
	    (!bit_super_set(req_nodes_bitmap, avail_nodes_bitmap))) {
		info("%pJ requires nodes not available on any switch",
		     job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that specific required nodes are linked together */
	if (req_nodes_bitmap) {
		rc = SLURM_ERROR;
		for (i=0; i<switch_record_cnt; i++) {
			if (bit_super_set(req_nodes_bitmap,
					  switches_bitmap[i])) {
				rc = SLURM_SUCCESS;
				break;
			}
		}
		if ( rc == SLURM_ERROR ) {
			info("%pJ requires nodes that are not linked together",
			     job_ptr);
			goto fini;
		}
	}

	if (req_nodes_bitmap) {
		/* Accumulate specific required resources, if any */
		first = bit_ffs(req_nodes_bitmap);
		last  = bit_fls(req_nodes_bitmap);
		for (i=first; ((i<=last) && (first>=0)); i++) {
			if (!bit_test(req_nodes_bitmap, i))
				continue;
			if (max_nodes <= 0) {
				info("%pJ requires nodes than allowed",
				     job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			bit_set(bitmap, i);
			bit_clear(avail_nodes_bitmap, i);
			avail_cpus = cpu_cnt[i];
			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&avail_cpus, rem_cpus, min_rem_nodes,
				     job_ptr->details, &cpu_cnt[i], i, cr_type);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			for (j=0; j<switch_record_cnt; j++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
				/* keep track of the accumulated resources */
				switches_required[j] += avail_cpus;
			}
		}
		/* Compute CPUs already allocated to required nodes */
		if ((job_ptr->details->max_cpus != NO_VAL) &&
		    (total_cpus > job_ptr->details->max_cpus)) {
			info("%pJ can't use required node due to max CPU limit",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0))
			goto fini;

		/* Update bitmaps and node counts for higher-level switches */
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				if (!bit_test(avail_nodes_bitmap, i)) {
					/* cleared from lower level */
					bit_clear(switches_bitmap[j], i);
					switches_node_cnt[j]--;
				} else {
					switches_cpu_cnt[j] += cpu_cnt[i];
				}
			}
		}
	} else {
		/* No specific required nodes, calculate CPU counts */
		for (j=0; j<switch_record_cnt; j++) {
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i=first; i<=last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				switches_cpu_cnt[j] += cpu_cnt[i];
			}
		}
	}

	/* Determine lowest level switch satisfying request with best fit
	 * in respect of the specific required nodes if specified
	 */
	best_fit_inx = -1;
	for (j=0; j<switch_record_cnt; j++) {
		if ((switches_cpu_cnt[j] < rem_cpus) ||
		    (!_enough_nodes(switches_node_cnt[j], rem_nodes,
				    min_nodes, req_nodes)))
			continue;
		if ((best_fit_inx != -1) && (req_nodes > min_nodes) &&
		    (switches_node_cnt[best_fit_inx] < req_nodes) &&
		    (switches_node_cnt[best_fit_inx] < switches_node_cnt[j])) {
			/* Try to get up to the requested node count */
			best_fit_inx = -1;
		}

		/*
		 * If first possibility OR
		 * first required switch OR
		 * lower level switch OR
		 * same level but tighter switch (less resource waste) OR
		 * 2 required switches of same level and nodes count
		 * but the latter accumulated cpus amount is bigger than
		 * the former one
		 */
		if ((best_fit_inx == -1) ||
		    (!switches_required[best_fit_inx] && switches_required[j]) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])) ||
		    ((switches_required[best_fit_inx] && switches_required[j]) &&
		     (switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] == switches_node_cnt[best_fit_inx]) &&
		     switches_required[best_fit_inx] < switches_required[j]) ) {
			/* If first possibility OR */
			/* current best switch not required OR */
			/* current best switch required but this */
			/* better one too */
			if ( best_fit_inx == -1 ||
			     !switches_required[best_fit_inx] ||
			     (switches_required[best_fit_inx] &&
			      switches_required[j]) )
				best_fit_inx = j;
		}
	}
	if (best_fit_inx == -1) {
		debug2("%pJ: best_fit topology failure: no switch currently has sufficient resource to satisfy the request",
		       job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	if (!switches_required[best_fit_inx] && req_nodes_bitmap ) {
		debug("%pJ: best_fit topology failure: no switch including requested nodes and satisfying the request found",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	bit_and(avail_nodes_bitmap, switches_bitmap[best_fit_inx]);

	/* Identify usable leafs (within higher switch having best fit) */
	for (j=0; j<switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from these leafs on a best-fit basis */
	/* Use required switches first to minimize the total amount */
	/* of switches */
	/* compute best-switch nodes available array */
	while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
		int *cpus_array = NULL, array_len;
		best_fit_cpus = best_fit_nodes = best_fit_sufficient = 0;
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			sufficient = (switches_cpu_cnt[j] >= rem_cpus) &&
				     _enough_nodes(switches_node_cnt[j],
						   rem_nodes, min_nodes,
						   req_nodes);
			/* If first possibility OR */
			/* first required switch OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest OR */
			/* 2 required switches of same level and cpus count */
			/* but the latter accumulated cpus amount is bigger */
			/* than the former one */
			if ((best_fit_nodes == 0) ||
			    (!switches_required[best_fit_location] &&
			     switches_required[j] ) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient &&
			     (switches_cpu_cnt[j] < best_fit_cpus)) ||
			    ((sufficient == 0) &&
			     (switches_cpu_cnt[j] > best_fit_cpus)) ||
			    (switches_required[best_fit_location] &&
			     switches_required[j] &&
			     switches_cpu_cnt[best_fit_location] ==
			     switches_cpu_cnt[j] &&
			     switches_required[best_fit_location] <
			     switches_required[j]) ) {
				/* If first possibility OR */
				/* current best switch not required OR */
				/* current best switch required but this */
				/* better one too */
				if ((best_fit_nodes == 0) ||
				    !switches_required[best_fit_location] ||
				    (switches_required[best_fit_location] &&
				     switches_required[j])) {
					best_fit_cpus =  switches_cpu_cnt[j];
					best_fit_nodes = switches_node_cnt[j];
					best_fit_location = j;
					best_fit_sufficient = sufficient;
				}
			}
		}
		if (best_fit_nodes == 0)
			break;

		leaf_switch_count++;
		/* Use select nodes from this leaf */
		first = bit_ffs(switches_bitmap[best_fit_location]);
		last  = bit_fls(switches_bitmap[best_fit_location]);

		/* compute best-switch nodes available cpus array */
		array_len = last - first + 1;
		cpus_array = xmalloc(sizeof(int) * array_len);
		for (i=first, j=0; ((i<=last) && (first>=0)); i++, j++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				cpus_array[j] = 0;
			else
				cpus_array[j] = cpu_cnt[i];
		}

		if (job_ptr->req_switch > 0) {
			if (time_waiting >= job_ptr->wait4switch) {
				job_ptr->best_switch = true;
				debug3("%pJ Waited %ld sec for switches use=%d",
					job_ptr, time_waiting,
					leaf_switch_count);
			} else if (leaf_switch_count>job_ptr->req_switch) {
				/* Allocation is for more than requested number
				 * of switches */
				job_ptr->best_switch = false;
				debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
					job_ptr, time_waiting,
					job_ptr->req_switch,
					leaf_switch_count,
					job_ptr->wait4switch);
			} else {
				job_ptr->best_switch = true;
			}
		}

		/* accumulate resources from this leaf on a best-fit basis */
		while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
			/* pick a node using a best-fit approach */
			/* if rem_cpus < 0, then we will search for nodes
			 * with lower free cpus nb first
			 */
			int suff = 0, bfsuff = 0, bfloc = 0 , bfsize = 0;
			int ca_bfloc = 0;
			for (i=first, j=0; ((i<=last) && (first>=0));
			     i++, j++) {
				if (cpus_array[j] == 0)
					continue;
				suff = cpus_array[j] >= rem_cpus;
				if ( (bfsize == 0) ||
				     (suff && !bfsuff) ||
				     (suff && (cpus_array[j] < bfsize)) ||
				     (!suff && (cpus_array[j] > bfsize)) ) {
					bfsuff = suff;
					bfloc = i;
					bfsize = cpus_array[j];
					ca_bfloc = j;
				}
			}

			/* no node found, break */
			if (bfsize == 0)
				break;

			/* clear resources of this node from the switch */
			bit_clear(switches_bitmap[best_fit_location],bfloc);
			switches_node_cnt[best_fit_location]--;

			switches_cpu_cnt[best_fit_location] -= bfsize;
			cpus_array[ca_bfloc] = 0;

			/* if this node was already selected in an other */
			/* switch, skip it */
			if (bit_test(bitmap, bfloc)) {
				continue;
			}

			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&bfsize, rem_cpus, min_rem_nodes,
				     job_ptr->details, &cpu_cnt[bfloc], bfloc,
				     cr_type);

			/* enforce the max_cpus limit */
			if ((job_ptr->details->max_cpus != NO_VAL) &&
			    (total_cpus+bfsize > job_ptr->details->max_cpus)) {
				debug2("5 can't use this node since it "
				       "would put us over the limit");
				continue;
			}

			/* take the node into account */
			bit_set(bitmap, bfloc);
			total_cpus += bfsize;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus -= bfsize;
		}

		/* free best-switch nodes available cpus array */
		xfree(cpus_array);

		/* mark this switch as processed */
		switches_node_cnt[best_fit_location] = 0;

	}

	if ((rem_cpus <= 0) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes)) {
		rc = SLURM_SUCCESS;
	} else
		rc = SLURM_ERROR;

 fini:	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	if (switches_bitmap) {
		for (i = 0; i < switch_record_cnt; i++) {
			FREE_NULL_BITMAP(switches_bitmap[i]);
		}
	}
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	return rc;
}

/*
 * A dragonfly network topology aware version of _eval_nodes().
 * NOTE: The logic here is almost identical to that of _job_test_topo()
 *       in select_linear.c. Any bug found here is probably also there.
 */
static int _eval_nodes_dfly(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint32_t cr_node_cnt,
			uint16_t *cpu_cnt, uint16_t cr_type)
{
	bitstr_t  *switch_use_bitmap = NULL;	/* leaf switches used */
	bitstr_t **switches_bitmap = NULL;	/* nodes on this switch */
	int       *switches_cpu_cnt = NULL;	/* total CPUs on switch */
	int       *switches_node_cnt = NULL;	/* total nodes on switch */
	int       *switches_node_use = NULL;	/* nodes from switch used */
	int        leaf_switch_count = 0;	/* Count of leaf node switches used */

	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;
	int rem_cpus, rem_nodes;	/* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int avail_cpus;
	int total_cpus = 0;	/* #CPUs allocated to job */
	int i, j, rc = SLURM_SUCCESS;
	int best_fit_inx, first, last;
	int best_fit_nodes, best_fit_cpus;
	int best_fit_location = 0;
	long time_waiting = 0;
	int req_switch_cnt = 0;
	int req_switch_id = -1;

	if (job_ptr->req_switch > 1) {
		/* Maximum leaf switch count >1 probably makes no sense */
		info("%s: Resetting %pJ leaf switch count from %u to 0",
		     __func__, job_ptr, job_ptr->req_switch);
		job_ptr->req_switch = 0;
	}
	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	rem_cpus = job_ptr->details->min_cpus;
	rem_nodes = MAX(min_nodes, req_nodes);
	min_rem_nodes = min_nodes;

	if (job_ptr->details->req_node_bitmap) {
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
		i = bit_set_count(req_nodes_bitmap);
		if (i > max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, i, max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
	}

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switch_use_bitmap = bit_alloc(switch_record_cnt);
	switches_bitmap   = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switches_cpu_cnt  = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_cnt = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_use = xmalloc(sizeof(int)        * switch_record_cnt);
	avail_nodes_bitmap = bit_alloc(cr_node_cnt);
	for (i = 0; i < switch_record_cnt; i++) {
		switches_bitmap[i] = bit_copy(switch_record_table[i].
					      node_bitmap);
		bit_and(switches_bitmap[i], bitmap);
		bit_or(avail_nodes_bitmap, switches_bitmap[i]);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
	}
	bit_nclear(bitmap, 0, cr_node_cnt - 1);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switches_node_cnt[i]) {
				node_names = bitmap2node_name(
						switches_bitmap[i]);
			}
			debug("switch=%s nodes=%u:%s speed:%u",
			      switch_record_table[i].name,
			      switches_node_cnt[i], node_names,
			      switch_record_table[i].link_speed);
			xfree(node_names);
		}
	}

	if (req_nodes_bitmap &&
	    (!bit_super_set(req_nodes_bitmap, avail_nodes_bitmap))) {
		info("%pJ requires nodes not available on any switch",
		     job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that specific required nodes are linked together */
	if (req_nodes_bitmap) {
		rc = SLURM_ERROR;
		for (i = 0; i < switch_record_cnt; i++) {
			if (bit_super_set(req_nodes_bitmap,
					  switches_bitmap[i])) {
				rc = SLURM_SUCCESS;
				break;
			}
		}
		if ( rc == SLURM_ERROR ) {
			info("%pJ requires nodes that are not linked together",
			     job_ptr);
			goto fini;
		}
	}

	if (req_nodes_bitmap) {
		/* Accumulate specific required resources, if any */
		first = bit_ffs(req_nodes_bitmap);
		last  = bit_fls(req_nodes_bitmap);
		for (i = first; ((i <= last) && (first >= 0)); i++) {
			if (!bit_test(req_nodes_bitmap, i))
				continue;
			if (max_nodes <= 0) {
				info("%pJ requires nodes than allowed",
				     job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			bit_set(bitmap, i);
			bit_clear(avail_nodes_bitmap, i);
			avail_cpus = cpu_cnt[i];
			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&avail_cpus, rem_cpus, min_rem_nodes,
				     job_ptr->details, &cpu_cnt[i], i, cr_type);
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			for (j = 0; j < switch_record_cnt; j++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				bit_clear(switches_bitmap[j], i);
				switches_node_cnt[j]--;
				switches_node_use[j]++;
				if (switch_record_table[j].level == 0) {
					req_switch_cnt++;
					req_switch_id = j;
				}
			}
		}
		/* Compute CPUs already allocated to required nodes */
		if ((job_ptr->details->max_cpus != NO_VAL) &&
		    (total_cpus > job_ptr->details->max_cpus)) {
			info("%pJ can't use required node due to max CPU limit",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0))
			goto fini;

		/* Update bitmaps and node counts for higher-level switches */
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i = first; i <= last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				if (!bit_test(avail_nodes_bitmap, i)) {
					/* cleared from lower level */
					bit_clear(switches_bitmap[j], i);
					switches_node_cnt[j]--;
				} else {
					switches_cpu_cnt[j] += cpu_cnt[i];
				}
			}
		}
	} else {
		/* No specific required nodes, calculate CPU counts */
		for (j = 0; j < switch_record_cnt; j++) {
			first = bit_ffs(switches_bitmap[j]);
			if (first < 0)
				continue;
			last  = bit_fls(switches_bitmap[j]);
			for (i = first; i <= last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;
				switches_cpu_cnt[j] += cpu_cnt[i];
			}
		}
	}

	/* Determine lowest level switch satisfying request with best fit 
	 * in respect of the specific required nodes if specified
	 */
	best_fit_inx = -1;
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switches_cpu_cnt[j] < rem_cpus) ||
		    (!_enough_nodes(switches_node_cnt[j], rem_nodes,
				    min_nodes, req_nodes)))
			continue;
		if ((best_fit_inx != -1) && (req_nodes > min_nodes) &&
		    (switches_node_cnt[best_fit_inx] < req_nodes) &&
		    (switches_node_cnt[best_fit_inx] < switches_node_cnt[j])) {
			/* Try to get up to the requested node count */
			best_fit_inx = -1;
		}

		if ((req_switch_cnt == 1) && (req_switch_id == j)) {
			best_fit_inx = j;
			break;
		}

		/*
		 * If first possibility OR
		 * lower level switch OR
		 * same level but tighter switch (less resource waste) OR
		 * 2 required switches of same level and nodes count
		 * but the latter accumulated CPUs count is bigger than 
		 * the former one
		 */
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx]))){
			best_fit_inx = j;
		}
	}
	if (best_fit_inx == -1) {
		debug2("%pJ: best_fit topology failure: no switch currently has sufficient resource to satisfy the request",
		       job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	bit_and(avail_nodes_bitmap, switches_bitmap[best_fit_inx]);

	/* Identify usable leafs (within higher switch having best fit) */
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from leafs on a best-fit or round-robin basis */
	while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
		int *cpus_array = NULL, array_len;
		best_fit_cpus = best_fit_nodes = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;

			/* If multiple leaf switches must be used, prefer use
			 * of leaf switches with fewest number of idle CPUs.
			 * This results in more leaf switches being used and
			 * achieves better network bandwidth. */
			if ((best_fit_nodes == 0) ||
			    (switches_node_use[best_fit_location] >
			     switches_node_use[j]) ||
			    ((switches_node_use[best_fit_location] ==
			      switches_node_use[j]) &&
			     (switches_cpu_cnt[j] < best_fit_cpus))) {
				best_fit_cpus =  switches_cpu_cnt[j];
				best_fit_nodes = switches_node_cnt[j];
				best_fit_location = j;
			}
		}

		if (best_fit_nodes == 0)
			break;

		/* Use select nodes from this leaf */
		bit_set(switch_use_bitmap, best_fit_location);
		leaf_switch_count = bit_set_count(switch_use_bitmap);
		first = bit_ffs(switches_bitmap[best_fit_location]);
		last  = bit_fls(switches_bitmap[best_fit_location]);

		/* compute best-switch nodes available CPUs array */
		array_len = last - first + 1;
		cpus_array = xmalloc(sizeof(int) * array_len);
		for (i = first, j = 0; ((i <= last) && (first >= 0)); i++, j++){
			if (!bit_test(switches_bitmap
				      [best_fit_location], i))
				cpus_array[j] = 0;
			else
				cpus_array[j] = cpu_cnt[i];
		}

		if (job_ptr->req_switch > 0) {
			if (time_waiting >= job_ptr->wait4switch) {
				job_ptr->best_switch = true;
				debug3("%pJ Waited %ld sec for switches use=%d",
					job_ptr, time_waiting,
					leaf_switch_count);
			} else if (leaf_switch_count > job_ptr->req_switch) {
				/* Allocation is for more than requested number
				 * of switches */
				job_ptr->best_switch = false;
				debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
					job_ptr, time_waiting,
					job_ptr->req_switch,
					leaf_switch_count,
					job_ptr->wait4switch);
			} else {
				job_ptr->best_switch = true;
			}
		}

		/* accumulate resources from this leaf on a best-fit basis */
		while ((max_nodes > 0) && ((rem_nodes > 0) || (rem_cpus > 0))) {
			/* pick a node using a best-fit approach */
			/* if rem_cpus < 0, then we will search for nodes 
			 * with lower free cpus nb first
			 */
			int suff = 0, bfsuff = 0, bfloc = 0 , bfsize = 0;
			int ca_bfloc = 0;
			for (i = first, j = 0; ((i <= last) && (first >= 0)); 
			     i++, j++) {
				if (cpus_array[j] == 0)
					continue;
				suff = cpus_array[j] >= rem_cpus;
				if ( (bfsize == 0) ||
				     (suff && !bfsuff) ||
				     (suff && (cpus_array[j] < bfsize)) ||
				     (!suff && (cpus_array[j] > bfsize)) ) {
					bfsuff = suff;
					bfloc = i;
					bfsize = cpus_array[j];
					ca_bfloc = j;
				}
			}

			/* no node found, break */
			if (bfsize == 0)
				break;
			
			/* clear resources of this node from the switch */
			bit_clear(switches_bitmap[best_fit_location], bfloc);
			switches_node_cnt[best_fit_location]--;
			switches_node_use[best_fit_location]++;
			switches_cpu_cnt[best_fit_location] -= bfsize;
			cpus_array[ca_bfloc] = 0;

			/* if this node was already selected in an other */
			/* switch, skip it */
			if (bit_test(bitmap, bfloc)) {
				continue;
			}

			/* This could result in 0, but if the user
			 * requested nodes here we will still give
			 * them and then the step layout will sort
			 * things out. */
			_cpus_to_use(&bfsize, rem_cpus, min_rem_nodes,
				     job_ptr->details, &cpu_cnt[bfloc], bfloc,
				     cr_type);

			/* enforce the max_cpus limit */
			if ((job_ptr->details->max_cpus != NO_VAL) &&
			    (total_cpus+bfsize > job_ptr->details->max_cpus)) {
				debug2("5 can't use this node since it "
				       "would put us over the limit");
				continue;
			}

			/* take the node into account */
			bit_set(bitmap, bfloc);
			total_cpus += bfsize;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus -= bfsize;
			if (job_ptr->req_switch != 1)
				break;
		}		

		/* free best-switch nodes available cpus array */
		xfree(cpus_array);
	}

	if ((rem_cpus <= 0) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes)) {
		rc = SLURM_SUCCESS;
	} else
		rc = SLURM_ERROR;

 fini:	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	if (switches_bitmap) {
		for (i = 0; i < switch_record_cnt; i++) {
			FREE_NULL_BITMAP(switches_bitmap[i]);
		}
	}
	FREE_NULL_BITMAP(switch_use_bitmap);
	xfree(switches_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_node_use);

	return rc;
}

/* this is an intermediary step between _select_nodes and _eval_nodes
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low cpu counts for the job and re-evaluates each result */
static int _choose_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			 uint32_t min_nodes, uint32_t max_nodes,
			 uint32_t req_nodes, uint32_t cr_node_cnt,
			 uint16_t *cpu_cnt, uint16_t cr_type,
			 bool prefer_alloc_nodes)
{
	int i, count, ec, most_cpus = 0;
	bitstr_t *origmap, *reqmap = NULL;
	int rem_node_cnt, rem_cpu_cnt = 0;

	if (job_ptr->details->req_node_bitmap)
		reqmap = job_ptr->details->req_node_bitmap;

	/* clear nodes from the bitmap that don't have available resources */
	for (i = 0; i < cr_node_cnt; i++) {
		if (!bit_test(node_map, i))
			continue;
		/* Make sure we don't say we can use a node exclusively
		 * that is bigger than our max cpu count. */
		if (((job_ptr->details->whole_node == 1) &&
		     (job_ptr->details->max_cpus != NO_VAL) &&
		     (job_ptr->details->max_cpus < cpu_cnt[i])) ||
		/* OR node has no CPUs */
		    (cpu_cnt[i] < 1)) {
			if (reqmap && bit_test(reqmap, i)) {
				/* can't clear a required node! */
				return SLURM_ERROR;
			}
			bit_clear(node_map, i);
		}
	}

	if ((job_ptr->details->num_tasks > 1) &&
	    (max_nodes > job_ptr->details->num_tasks))
		max_nodes = MAX(job_ptr->details->num_tasks, min_nodes);

	origmap = bit_copy(node_map);

	ec = _eval_nodes(job_ptr, node_map, min_nodes, max_nodes, req_nodes,
			 cr_node_cnt, cpu_cnt, cr_type, prefer_alloc_nodes);

	if (ec == SLURM_SUCCESS) {
		FREE_NULL_BITMAP(origmap);
		return ec;
	}

	/* This nodeset didn't work. To avoid a possible knapsack problem,
	 * incrementally remove nodes with low cpu counts and retry */
	for (i = 0; i < cr_node_cnt; i++) {
		most_cpus = MAX(most_cpus, cpu_cnt[i]);
		rem_cpu_cnt += cpu_cnt[i];
	}

	for (count = 1; count < most_cpus; count++) {
		bool no_change = true, no_more_remove = false;
		bit_or(node_map, origmap);
		rem_node_cnt = bit_set_count(node_map);
		for (i = 0; i < cr_node_cnt; i++) {
			if ((cpu_cnt[i] > 0) && (cpu_cnt[i] <= count)) {
				if (!bit_test(node_map, i))
					continue;
				if (reqmap && bit_test(reqmap, i))
					continue;
				rem_cpu_cnt -= cpu_cnt[i];
				if (rem_cpu_cnt < job_ptr->details->min_cpus) {
					/* Can not remove this node */
					no_more_remove = true;
					break;
				}
				no_change = false;
				bit_clear(node_map, i);
				bit_clear(origmap, i);
				if ((--rem_node_cnt <= min_nodes) ||
				    (rem_cpu_cnt ==
				     job_ptr->details->min_cpus)) {
					/* Can not remove any more nodes */
					no_more_remove = true;
					break;
				}
			}
		}
		if (no_change)
			continue;
		ec = _eval_nodes(job_ptr, node_map, min_nodes, max_nodes,
				 req_nodes, cr_node_cnt, cpu_cnt, cr_type,
				 prefer_alloc_nodes);
		if ((ec == SLURM_SUCCESS) || no_more_remove) {
			FREE_NULL_BITMAP(origmap);
			return ec;
		}
	}
	FREE_NULL_BITMAP(origmap);
	return ec;
}

/* Enable detailed logging of _select_nodes() node and core bitmaps */
static inline void _log_select_maps(char *loc, bitstr_t *node_map,
				    bitstr_t *core_map)
{
#if _DEBUG
	char str[256];

	if (node_map) {
		bit_fmt(str, (sizeof(str) - 1), node_map);
		info("%s nodemap[0-%d]: %s",
		     loc, (int)bit_size(node_map)-1, str);
	}
	if (core_map) {
		bit_fmt(str, (sizeof(str) - 1), core_map);
		info("%s coremap[0-%d]: %s",
		     loc, (int) bit_size(core_map)-1, str);
	}
#endif
}

/* Select the best set of resources for the given job
 * IN: job_ptr      - pointer to the job requesting resources
 * IN: min_nodes    - minimum number of nodes required
 * IN: max_nodes    - maximum number of nodes requested
 * IN: req_nodes    - number of requested nodes
 * IN/OUT: node_map - bitmap of available nodes / bitmap of selected nodes
 * IN: cr_node_cnt  - total number of nodes in the cluster
 * IN/OUT: core_map - bitmap of available cores / bitmap of selected cores
 * IN: cr_type      - resource type
 * IN: test_only    - ignore allocated memory check
 * IN: part_core_map - bitmap of cores allocated to jobs of this partition
 *                     or NULL if don't care
 * IN: prefer_alloc_nodes - select currently allocated nodes first
 * RET - array with number of CPUs available per node or NULL if not runnable
 */
static uint16_t *_select_nodes(struct job_record *job_ptr, uint32_t min_nodes,
				uint32_t max_nodes, uint32_t req_nodes,
				bitstr_t *node_map, uint32_t cr_node_cnt,
				bitstr_t *core_map,
				struct node_use_record *node_usage,
				uint16_t cr_type, bool test_only,
				bitstr_t *part_core_map,
				bool prefer_alloc_nodes)
{
	int i, rc;
	uint16_t *cpu_cnt, *cpus = NULL;
	uint32_t start, n, a;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	if (bit_set_count(node_map) < min_nodes)
		return NULL;

	_log_select_maps("_select_nodes/enter", node_map, core_map);
	/* get resource usage for this job from each available node */
	_get_res_usage(job_ptr, node_map, core_map, cr_node_cnt,
		       node_usage, cr_type, &cpu_cnt, test_only, part_core_map);

	/* clear all nodes that do not have sufficient resources for this job */
	for (n = 0; n < cr_node_cnt; n++) {
		if (bit_test(node_map, n) && (cpu_cnt[n] == 0)) {
			/* insufficient resources available on this node */
			if (req_map && bit_test(req_map, n)) {
				/* cannot clear a required node! */
				xfree(cpu_cnt);
				return NULL;
			}
			bit_clear(node_map, n);
		}
	}
	if (bit_set_count(node_map) < min_nodes) {
		xfree(cpu_cnt);
		return NULL;
	}
	_log_select_maps("_select_nodes/elim_nodes", node_map, core_map);

	if (details_ptr->ntasks_per_node && details_ptr->num_tasks) {
		i  = details_ptr->num_tasks;
		i += (details_ptr->ntasks_per_node - 1);
		i /= details_ptr->ntasks_per_node;
		min_nodes = MAX(min_nodes, i);
	}

	/* choose the best nodes for the job */
	rc = _choose_nodes(job_ptr, node_map, min_nodes, max_nodes, req_nodes,
			   cr_node_cnt, cpu_cnt, cr_type, prefer_alloc_nodes);
	_log_select_maps("_select_nodes/choose_nodes", node_map, core_map);

	/* if successful, sync up the core_map with the node_map, and
	 * create a cpus array */
	if (rc == SLURM_SUCCESS) {
		cpus = xmalloc(bit_set_count(node_map) * sizeof(uint16_t));
		start = 0;
		a = 0;
		for (n = 0; n < cr_node_cnt; n++) {
			if (bit_test(node_map, n)) {
				cpus[a++] = cpu_cnt[n];
				if (cr_get_coremap_offset(n) != start) {
					bit_nclear(core_map, start,
						   (cr_get_coremap_offset(n))-1);
				}
				start = cr_get_coremap_offset(n + 1);
			}
		}
		if (cr_get_coremap_offset(n) != start) {
			bit_nclear(core_map, start, cr_get_coremap_offset(n)-1);
		}
	}
	_log_select_maps("_select_nodes/sync_cores", node_map, core_map);
	xfree(cpu_cnt);
	return cpus;
}

/* When any cores on a node are removed from being available for a job,
 * then remove the entire node from being available. */
static void _block_whole_nodes(bitstr_t *node_bitmap,
			       bitstr_t *orig_core_bitmap,
			       bitstr_t *new_core_bitmap)
{
	int first_node, last_node, i_node;
	int first_core, last_core, i_core;

	first_node = bit_ffs(node_bitmap);
	if (first_node >= 0)
		last_node = bit_fls(node_bitmap);
	else
		last_node = first_node - 1;

	for (i_node = first_node; i_node <= last_node; i_node++) {
		if (!bit_test(node_bitmap, i_node))
			continue;
		first_core = cr_get_coremap_offset(i_node);
		last_core  = cr_get_coremap_offset(i_node + 1) - 1;
		for (i_core = first_core; i_core <= last_core; i_core++) {
			if ( bit_test(orig_core_bitmap, i_core) &&
			    !bit_test(new_core_bitmap,  i_core)) {
				bit_clear(node_bitmap, i_node);
				break;
			}
		}
	}
}

/* cr_job_test - does most of the real work for select_p_job_test(), which
 *	includes contiguous selection, load-leveling and max_share logic
 *
 * PROCEDURE:
 *
 * Step 1: compare nodes in "avail" node_bitmap with current node state data
 *         to find available nodes that match the job request
 *
 * Step 2: check resources in "avail" node_bitmap with allocated resources from
 *         higher priority partitions (busy resources are UNavailable)
 *
 * Step 3: select resource usage on remaining resources in "avail" node_bitmap
 *         for this job, with the placement influenced by existing
 *         allocations
 */
extern int cr_job_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, int mode,
			uint16_t cr_type, enum node_cr_state job_node_req,
			uint32_t cr_node_cnt,
			struct part_res_record *cr_part_ptr,
			struct node_use_record *node_usage,
			bitstr_t *exc_core_bitmap, bool prefer_alloc_nodes,
			bool qos_preemptor, bool preempt_mode)
{
	static int gang_mode = -1;
	int error_code = SLURM_SUCCESS;
	bitstr_t *orig_map, *avail_cores, *free_cores, *part_core_map = NULL;
	bitstr_t *free_cores_tmp = NULL,  *node_bitmap_tmp = NULL;
	bitstr_t *free_cores_tmp2 = NULL, *node_bitmap_tmp2 = NULL;
	bool test_only;
	uint32_t c, j, k, n, csize, total_cpus;
	uint64_t save_mem = 0;
	int32_t build_cnt;
	job_resources_t *job_res;
	struct job_details *details_ptr;
	struct part_res_record *p_ptr, *jp_ptr;
	uint16_t *cpu_count;
	uint16_t *cpu_count_tmp;
	int i, first, last;

	if (gang_mode == -1) {
		if (slurm_get_preempt_mode() & PREEMPT_MODE_GANG)
			gang_mode = 1;
		else
			gang_mode = 0;
	}

	details_ptr = job_ptr->details;

	free_job_resources(&job_ptr->job_resrcs);

	if (mode == SELECT_MODE_TEST_ONLY)
		test_only = true;
	else	/* SELECT_MODE_RUN_NOW || SELECT_MODE_WILL_RUN  */
		test_only = false;

	/* check node_state and update the node_bitmap as necessary */
	if (!test_only) {
		error_code = _verify_node_state(cr_part_ptr, job_ptr,
						node_bitmap, cr_type,
						node_usage, job_node_req,
						exc_core_bitmap, qos_preemptor);
		if (error_code != SLURM_SUCCESS) {
			return error_code;
		}
	}

	/*
	 * Ensure sufficient resources to satisfy thread/core/socket
	 * specifications with -O/--overcommit option.
	 */
	if (details_ptr->overcommit &&
	    (details_ptr->min_cpus == details_ptr->min_nodes)) {
		struct multi_core_data *mc_ptr = details_ptr->mc_ptr;

		if ((mc_ptr->threads_per_core != NO_VAL16) &&
		    (mc_ptr->threads_per_core > 1))
			details_ptr->min_cpus *= mc_ptr->threads_per_core;
		if ((mc_ptr->cores_per_socket != NO_VAL16) &&
		    (mc_ptr->cores_per_socket > 1))
			details_ptr->min_cpus *= mc_ptr->cores_per_socket;
		if ((mc_ptr->sockets_per_node != NO_VAL16) &&
		    (mc_ptr->sockets_per_node > 1))
			details_ptr->min_cpus *= mc_ptr->sockets_per_node;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: cr_job_test: evaluating %pJ on %u nodes",
		     job_ptr, bit_set_count(node_bitmap));
	}

	if ((details_ptr->pn_min_memory == 0) &&
	    (select_fast_schedule == 0))
		job_ptr->bit_flags |= NODE_MEM_CALC;	/* To be calculated */

	orig_map = bit_copy(node_bitmap);
	avail_cores = make_core_bitmap(node_bitmap,
				       job_ptr->details->core_spec);

	/* test to make sure that this job can succeed with all avail_cores
	 * if 'no' then return FAIL
	 * if 'yes' then we will seek the optimal placement for this job
	 *          within avail_cores
	 */
	free_cores = bit_copy(avail_cores);
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, cr_node_cnt, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);
	if (cpu_count == NULL) {
		/* job cannot fit */
		FREE_NULL_BITMAP(orig_map);
		FREE_NULL_BITMAP(free_cores);
		FREE_NULL_BITMAP(avail_cores);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: test 0 fail: "
			     "insufficient resources");
		}
		return SLURM_ERROR;
	} else if (test_only) {
		FREE_NULL_BITMAP(orig_map);
		FREE_NULL_BITMAP(free_cores);
		FREE_NULL_BITMAP(avail_cores);
		xfree(cpu_count);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("cons_res: cr_job_test: test 0 pass: test_only");
		return SLURM_SUCCESS;
	} else if (!job_ptr->best_switch) {
		FREE_NULL_BITMAP(orig_map);
		FREE_NULL_BITMAP(free_cores);
		FREE_NULL_BITMAP(avail_cores);
		xfree(cpu_count);
		if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
			info("cons_res: cr_job_test: test 0 fail: "
			     "waiting for switches");
		}
		return SLURM_ERROR;
	}
	if (cr_type == CR_MEMORY) {
		/* CR_MEMORY does not care about existing CPU allocations,
		 * so we can jump right to job allocation from here */
		goto alloc_job;
	}
	xfree(cpu_count);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: cr_job_test: test 0 pass - "
		     "job fits on given resources");
	}

	/* now that we know that this job can run with the given resources,
	 * let's factor in the existing allocations and seek the optimal set
	 * of resources for this job. Here is the procedure:
	 *
	 * Step 1: Seek idle CPUs across all partitions. If successful then
	 *         place job and exit. If not successful, then continue. Two
	 *         related items to note:
	 *          1. Jobs that don't share CPUs finish with step 1.
	 *          2. The remaining steps assume sharing or preemption.
	 *
	 * Step 2: Remove resources that are in use by higher-priority
	 *         partitions, and test that job can still succeed. If not
	 *         then exit.
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
	bit_copybits(node_bitmap, orig_map);
	bit_copybits(free_cores, avail_cores);

	if (exc_core_bitmap) {
		int exc_core_size  = bit_size(exc_core_bitmap);
		int free_core_size = bit_size(free_cores);
		if (exc_core_size != free_core_size) {
			/* This would indicate that cores were added to or
			 * removed from nodes in this reservation when the
			 * slurmctld daemon restarted with a new slurm.conf
			 * file. This can result in cores being lost from a
			 * reservation. */
			error("Bad core_bitmap size for reservation %s "
			      "(%d != %d), ignoring core reservation",
			      job_ptr->resv_name,
			      exc_core_size, free_core_size);
			exc_core_bitmap = NULL;	/* Clear local value */
		}
	}
	if (exc_core_bitmap) {
#if _DEBUG
		char str[100];
		bit_fmt(str, (sizeof(str) - 1), exc_core_bitmap);
		debug2("excluding cores reserved: %s", str);
#endif
		bit_and_not(free_cores, exc_core_bitmap);
	}

	/* remove all existing allocations from free_cores */
	for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			bit_and_not(free_cores, p_ptr->row[i].row_bitmap);
			if (p_ptr->part_ptr != job_ptr->part_ptr)
				continue;
			if (part_core_map) {
				bit_or(part_core_map, p_ptr->row[i].row_bitmap);
			} else {
				part_core_map = bit_copy(p_ptr->row[i].
							 row_bitmap);
			}
		}
	}
	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, cr_node_cnt, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);

	if ((cpu_count) && (job_ptr->best_switch)) {
		/* job fits! We're done. */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: test 1 pass - "
			     "idle resources found");
		}
		goto alloc_job;
	}
	xfree(cpu_count);

	if ((gang_mode == 0) && (job_node_req == NODE_CR_ONE_ROW)) {
		/* This job CANNOT share CPUs regardless of priority,
		 * so we fail here. Note that Shared=EXCLUSIVE was already
		 * addressed in _verify_node_state() and job preemption
		 * removes jobs from simulated resource allocation map
		 * before this point. */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: test 1 fail - no idle resources available");
		}
		goto alloc_job;
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: cr_job_test: test 1 fail - "
		     "not enough idle resources");
	}

	/*** Step 2 ***/
	bit_copybits(node_bitmap, orig_map);
	bit_copybits(free_cores, avail_cores);

	if (exc_core_bitmap) {
		bit_and_not(free_cores, exc_core_bitmap);
	}

	for (jp_ptr = cr_part_ptr; jp_ptr; jp_ptr = jp_ptr->next) {
		if (jp_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!jp_ptr) {
		fatal("cons_res error: could not find partition for %pJ",
		      job_ptr);
		return SLURM_ERROR;	/* CLANG false positive */
	}

	if (preempt_by_part) {
		/* Remove from avail_cores resources allocated to jobs which
		 * this job can not preempt */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: looking for "
			     "higher-priority or PREEMPT_MODE_OFF part's to "
			     "remove from avail_cores");
		}

		for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
			if ((p_ptr->part_ptr->priority_tier <=
			     jp_ptr->part_ptr->priority_tier) &&
			    (p_ptr->part_ptr->preempt_mode !=
			     PREEMPT_MODE_OFF)) {
				if (select_debug_flags &
				    DEBUG_FLAG_SELECT_TYPE) {
					info("cons_res: cr_job_test: "
					     "continuing on part: %s",
					     p_ptr->part_ptr->name);
				}
				continue;
			}
			if (!p_ptr->row)
				continue;
			for (i = 0; i < p_ptr->num_rows; i++) {
				if (!p_ptr->row[i].row_bitmap)
					continue;
				bit_and_not(free_cores, p_ptr->row[i].row_bitmap);
			}
		}
	}
	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);
	/* make these changes permanent */
	bit_copybits(avail_cores, free_cores);

	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, cr_node_cnt, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);
	if (!cpu_count) {
		/* job needs resources that are currently in use by
		 * higher-priority jobs, so fail for now */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: test 2 fail - "
			     "resources busy with higher priority jobs");
		}
		goto alloc_job;
	}
	xfree(cpu_count);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: cr_job_test: test 2 pass - "
		     "available resources for this priority");
	}

	/*** Step 3 ***/
	bit_copybits(node_bitmap, orig_map);
	bit_copybits(free_cores, avail_cores);

	/* remove existing allocations (jobs) from same-priority partitions
	 * from avail_cores */
	for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->part_ptr->priority_tier !=
		    jp_ptr->part_ptr->priority_tier)
			continue;
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			bit_and_not(free_cores, p_ptr->row[i].row_bitmap);
		}
	}

	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	free_cores_tmp  = bit_copy(free_cores);
	node_bitmap_tmp = bit_copy(node_bitmap);
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, cr_node_cnt, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);

	if (cpu_count) {
		/*
		 * To the extent possible, remove from consideration resources
		 * which are allocated to jobs in lower priority partitions.
		 */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("cons_res: cr_job_test: test 3 pass - found resources");
		for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr->priority_tier >=
			    jp_ptr->part_ptr->priority_tier)
				continue;
			if (!p_ptr->row)
				continue;
			for (i = 0; i < p_ptr->num_rows; i++) {
				if (!p_ptr->row[i].row_bitmap)
					continue;
				bit_and_not(free_cores_tmp,
					    p_ptr->row[i].row_bitmap);
			}
			if (job_ptr->details->whole_node == 1) {
				_block_whole_nodes(node_bitmap_tmp, avail_cores,
						   free_cores_tmp);
			}

			free_cores_tmp2  = bit_copy(free_cores_tmp);
			node_bitmap_tmp2 = bit_copy(node_bitmap_tmp);
			cpu_count_tmp = _select_nodes(job_ptr, min_nodes,
						max_nodes, req_nodes,
						node_bitmap_tmp, cr_node_cnt,
						free_cores_tmp, node_usage,
						cr_type, test_only,
						part_core_map,
						prefer_alloc_nodes);
			if (!cpu_count_tmp) {
				FREE_NULL_BITMAP(free_cores_tmp2);
				FREE_NULL_BITMAP(node_bitmap_tmp2);
				break;
			}
			if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
				info("cons_res: cr_job_test: remove low-priority partition %s",
				     p_ptr->part_ptr->name);
			}
			bit_copybits(free_cores, free_cores_tmp);
			bit_copybits(node_bitmap, node_bitmap_tmp);
			bit_copybits(free_cores_tmp, free_cores_tmp2);
			bit_copybits(node_bitmap_tmp, node_bitmap_tmp2);
			FREE_NULL_BITMAP(free_cores_tmp2);
			FREE_NULL_BITMAP(node_bitmap_tmp2);
			xfree(cpu_count);
			cpu_count = cpu_count_tmp;
		}
		goto alloc_job;
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: cr_job_test: test 3 fail - "
		     "not enough idle resources in same priority");
	}


	/*** Step 4 ***/
	/* try to fit the job into an existing row
	 *
	 * free_cores = core_bitmap to be built
	 * avail_cores = static core_bitmap of all available cores
	 */

	if (!jp_ptr || !jp_ptr->row) {
		/* there's no existing jobs in this partition, so place
		 * the job in avail_cores. FIXME: still need a good
		 * placement algorithm here that optimizes "job overlap"
		 * between this job (in these idle nodes) and existing
		 * jobs in the other partitions with <= priority to
		 * this partition */
		bit_copybits(node_bitmap, orig_map);
		bit_copybits(free_cores, avail_cores);
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					  req_nodes, node_bitmap, cr_node_cnt,
					  free_cores, node_usage, cr_type,
					  test_only, part_core_map,
					  prefer_alloc_nodes);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: test 4 pass - "
			     "first row found");
		}
		goto alloc_job;
	}

	if ((jp_ptr->num_rows > 1) && !preempt_by_qos)
		cr_sort_part_rows(jp_ptr);	/* Preserve row order for QOS */
	c = jp_ptr->num_rows;
	if (preempt_by_qos && !qos_preemptor)
		c--;				/* Do not use extra row */
	if (preempt_by_qos && (job_node_req != NODE_CR_AVAILABLE))
		c = 1;
	for (i = 0; i < c; i++) {
		if (!jp_ptr->row[i].row_bitmap)
			break;
		bit_copybits(node_bitmap, orig_map);
		bit_copybits(free_cores, avail_cores);
		bit_and_not(free_cores, jp_ptr->row[i].row_bitmap);

		if (job_ptr->details->whole_node == 1)
			_block_whole_nodes(node_bitmap, avail_cores,
					   free_cores);

		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					  req_nodes, node_bitmap, cr_node_cnt,
					  free_cores, node_usage, cr_type,
					  test_only, part_core_map,
					  prefer_alloc_nodes);
		if (cpu_count) {
			if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
				info("cons_res: cr_job_test: test 4 pass - "
				     "row %i", i);
			}
			break;
		}
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("cons_res: cr_job_test: test 4 fail - row %i", i);
	}

	if ((i < c) && !jp_ptr->row[i].row_bitmap) {
		/* we've found an empty row, so use it */
		bit_copybits(node_bitmap, orig_map);
		bit_copybits(free_cores, avail_cores);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: "
			     "test 4 trying empty row %i",i);
		}
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					  req_nodes, node_bitmap, cr_node_cnt,
					  free_cores, node_usage, cr_type,
					  test_only, part_core_map,
					  prefer_alloc_nodes);
	}

	if (!cpu_count) {
		/* job can't fit into any row, so exit */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_res: cr_job_test: test 4 fail - "
			     "busy partition");
		}
		goto alloc_job;

	}

	/*** CONSTRUCTION ZONE FOR STEPs 5 AND 6 ***
	 * Note that while the job may have fit into a row, it should
	 * still be run through a good placement algorithm here that
	 * optimizes "job overlap" between this job (in these idle nodes)
	 * and existing jobs in the other partitions with <= priority to
	 * this partition */

alloc_job:
	/* at this point we've found a good set of
	 * bits to allocate to this job:
	 * - node_bitmap is the set of nodes to allocate
	 * - free_cores is the set of allocated cores
	 * - cpu_count is the number of cpus per allocated node
	 *
	 * Next steps are to cleanup the worker variables,
	 * create the job_resources struct,
	 * distribute the job on the bits, and exit
	 */
	FREE_NULL_BITMAP(orig_map);
	FREE_NULL_BITMAP(part_core_map);
	FREE_NULL_BITMAP(free_cores_tmp);
	FREE_NULL_BITMAP(node_bitmap_tmp);
	if ((!cpu_count) || (!job_ptr->best_switch)) {
		/* we were sent here to cleanup and exit */
		FREE_NULL_BITMAP(avail_cores);
		FREE_NULL_BITMAP(free_cores);
		xfree(cpu_count);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("cons_res: exiting cr_job_test with no allocation");
		return SLURM_ERROR;
	}

	/* At this point we have:
	 * - a node_bitmap of selected nodes
	 * - a free_cores bitmap of usable cores on each selected node
	 * - a per-alloc-node cpu_count array
	 */

	if ((mode != SELECT_MODE_WILL_RUN) && (job_ptr->part_ptr == NULL))
		error_code = EINVAL;
	if ((error_code == SLURM_SUCCESS) && (mode == SELECT_MODE_WILL_RUN)) {
		/* Set a reasonable value for the number of allocated CPUs.
		 * Without computing task distribution this is only a guess */
		job_ptr->total_cpus = MAX(job_ptr->details->min_cpus,
					  job_ptr->details->min_nodes);
	}
	/*
	 * Defer checking select mode until we get a correct CPU count. Then
	 * exit if select mode is not SELECT_MODE_RUN_NOW, making sure to free
	 * job_ptr->job_resrcs.
	 */
	if (error_code != SLURM_SUCCESS) {
		FREE_NULL_BITMAP(avail_cores);
		FREE_NULL_BITMAP(free_cores);
		xfree(cpu_count);
		return error_code;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: cr_job_test: distributing %pJ",
		     job_ptr);
	}

	/** create the struct_job_res  **/
	job_res                   = create_job_resources();
	job_res->node_bitmap      = bit_copy(node_bitmap);
	job_res->nodes            = bitmap2node_name(node_bitmap);
	job_res->nhosts           = bit_set_count(node_bitmap);
	job_res->ncpus            = job_res->nhosts;
	if (job_ptr->details->ntasks_per_node)
		job_res->ncpus   *= details_ptr->ntasks_per_node;
	job_res->ncpus            = MAX(job_res->ncpus,
					details_ptr->min_cpus);
	job_res->ncpus            = MAX(job_res->ncpus,
					(job_res->nhosts *
					 details_ptr->pn_min_cpus));
	job_res->node_req         = job_node_req;
	job_res->cpus             = cpu_count;
	job_res->cpus_used        = xmalloc(job_res->nhosts *
					    sizeof(uint16_t));
	job_res->memory_allocated = xmalloc(job_res->nhosts *
					    sizeof(uint64_t));
	job_res->memory_used      = xmalloc(job_res->nhosts *
					    sizeof(uint64_t));
	job_res->whole_node       = job_ptr->details->whole_node;

	/* store the hardware data for the selected nodes */
	error_code = build_job_resources(job_res, node_record_table_ptr,
					  select_fast_schedule);
	if (error_code != SLURM_SUCCESS) {
		free_job_resources(&job_res);
		FREE_NULL_BITMAP(avail_cores);
		FREE_NULL_BITMAP(free_cores);
		return error_code;
	}

	/* total up all cpus and load the core_bitmap */
	total_cpus = 0;
	c = 0;
	csize = bit_size(job_res->core_bitmap);

	for (i = 0, n = 0; n < cr_node_cnt; n++) {
		if (bit_test(node_bitmap, n) == 0)
			continue;
		j = cr_get_coremap_offset(n);
		k = cr_get_coremap_offset(n + 1);
		for (; j < k; j++, c++) {
			if (bit_test(free_cores, j)) {
				if (c >= csize)	{
					error("cons_res: cr_job_test "
					      "core_bitmap index error on "
					      "node %s",
					      select_node_record[n].node_ptr->
					      name);
					drain_nodes(select_node_record[n].
						    node_ptr->name,
						    "Bad core count",
						    getuid());
					free_job_resources(&job_res);
					FREE_NULL_BITMAP(free_cores);
					return SLURM_ERROR;
				}
				bit_set(job_res->core_bitmap, c);
			}
		}

		total_cpus += job_res->cpus[i];
		i++;
	}

	/* When 'srun --overcommit' is used, ncpus is set to a minimum value
	 * in order to allocate the appropriate number of nodes based on the
	 * job request.
	 * For cons_res, all available logical processors will be allocated on
	 * each allocated node in order to accommodate the overcommit request.
	 */
	if (details_ptr->overcommit && details_ptr->num_tasks)
		job_res->ncpus = MIN(total_cpus, details_ptr->num_tasks);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_res: cr_job_test: %pJ ncpus %u cbits %u/%u nbits %u",
		     job_ptr, job_res->ncpus, bit_set_count(free_cores),
		     bit_set_count(job_res->core_bitmap), job_res->nhosts);
	}
	FREE_NULL_BITMAP(free_cores);

	/* distribute the tasks and clear any unused cores */
	job_ptr->job_resrcs = job_res;
	error_code = cr_dist(job_ptr, cr_type, preempt_mode, avail_cores);
	FREE_NULL_BITMAP(avail_cores);
	if (error_code != SLURM_SUCCESS) {
		free_job_resources(&job_ptr->job_resrcs);
		return error_code;
	}

	/* translate job_res->cpus array into format with rep count */
	build_cnt = build_job_resources_cpu_array(job_res);
	if (job_ptr->details->whole_node == 1) {
		first = bit_ffs(job_res->node_bitmap);
		if (first != -1)
			last  = bit_fls(job_res->node_bitmap);
		else
			last = first - 1;
		job_ptr->total_cpus = 0;
		for (i = first; i <= last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			/* This could make the job_res->cpus incorrect.
			 * Don't use job_res->cpus when allocating
			 * whole nodes as the job is finishing to
			 * subtract from the total cpu count or you
			 * will get an incorrect count.
			 */
			job_ptr->total_cpus += select_node_record[i].cpus;
		}
	} else if (cr_type & CR_SOCKET) {
		int ci = 0;
		int s, last_s, sock_cnt = 0;
		first = bit_ffs(job_res->node_bitmap);
		if (first != -1)
			last  = bit_fls(job_res->node_bitmap);
		else
			last = first - 1;
		job_ptr->total_cpus = 0;
		for (i = first; i <= last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			sock_cnt = 0;
			for (s = 0; s < select_node_record[i].sockets; s++) {
				last_s = -1;
				for (c = 0; c<select_node_record[i].cores; c++){
					if (bit_test(job_res->core_bitmap, ci)){
						if (s != last_s) {
							sock_cnt++;
							last_s = s;
						}
					}
					ci++;
				}
			}
			job_ptr->total_cpus += (sock_cnt *
						select_node_record[i].cores *
						select_node_record[i].vpus);
		}
	} else if (build_cnt >= 0)
		job_ptr->total_cpus = build_cnt;
	else
		job_ptr->total_cpus = total_cpus;	/* best guess */

	/*
	 * Stop if we aren't trying to start the job right now. We needed to
	 * get to here to have an accurate total_cpus so that accounting limits
	 * checks are accurate later on.
	 */
	if (mode != SELECT_MODE_RUN_NOW) {
		free_job_resources(&job_ptr->job_resrcs);
		return error_code;
	}

	if (!(cr_type & CR_MEMORY))
		return error_code;

	/* load memory allocated array */
	save_mem = details_ptr->pn_min_memory;
	if (save_mem & MEM_PER_CPU) {
		/* memory is per-cpu */
		save_mem &= (~MEM_PER_CPU);
		for (i = 0; i < job_res->nhosts; i++) {
			job_res->memory_allocated[i] = job_res->cpus[i] *
						       save_mem;
		}
	} else if (save_mem) {
		/* memory is per-node */
		for (i = 0; i < job_res->nhosts; i++) {
			job_res->memory_allocated[i] = save_mem;
		}
	} else {	/* --mem=0, allocate job all memory on node */
		uint64_t avail_mem, lowest_mem = 0;
		first = bit_ffs(job_res->node_bitmap);
		if (first != -1)
			last  = bit_fls(job_res->node_bitmap);
		else
			last = first - 1;
		for (i = first, j = 0; i <= last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			avail_mem = select_node_record[i].real_memory -
				    select_node_record[i].mem_spec_limit;
			if ((j == 0) || (lowest_mem > avail_mem))
				lowest_mem = avail_mem;
			job_res->memory_allocated[j++] = avail_mem;
		}
		details_ptr->pn_min_memory = lowest_mem;
	}
	return error_code;
}
