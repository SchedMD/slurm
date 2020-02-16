/*****************************************************************************\
 *  cons_common.c - Common function interface for the select/cons_* plugins
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Derived in large part from select/cons_[res|tres] plugins
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

#define _GNU_SOURCE

#include "src/common/slurm_xlator.h"

#include "cons_common.h"

#include "src/common/assoc_mgr.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/slurm_topology.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
extern node_record_t *node_record_table_ptr __attribute__((weak_import));
extern List part_list __attribute__((weak_import));
extern List job_list __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern time_t last_node_update __attribute__((weak_import));
extern switch_record_t *switch_record_table __attribute__((weak_import));
extern int switch_record_cnt __attribute__((weak_import));
extern bitstr_t *avail_node_bitmap __attribute__((weak_import));
extern uint16_t *cr_node_num_cores __attribute__((weak_import));
extern uint32_t *cr_node_cores_offset __attribute__((weak_import));
extern int slurmctld_tres_cnt __attribute__((weak_import));
extern slurmctld_config_t slurmctld_config __attribute__((weak_import));
#else
slurm_ctl_conf_t slurmctld_conf;
node_record_t *node_record_table_ptr;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
switch_record_t *switch_record_table;
int switch_record_cnt;
bitstr_t *avail_node_bitmap;
uint16_t *cr_node_num_cores;
uint32_t *cr_node_cores_offset;
int slurmctld_tres_cnt = 0;
slurmctld_config_t slurmctld_config;
#endif

/* init common global variables */
bool     backfill_busy_nodes  = false;
int      bf_window_scale      = 0;
cons_common_callbacks_t cons_common_callbacks = {0};
int      core_array_size      = 1;
uint16_t cr_type              = CR_CPU; /* cr_type is overwritten in init() */
bool     gang_mode            = false;
bool     have_dragonfly       = false;
bool     is_cons_tres         = false;
bool     pack_serial_at_end   = false;
bool     preempt_by_part      = false;
bool     preempt_by_qos       = false;
uint16_t priority_flags       = 0;
uint64_t select_debug_flags   = 0;
int      select_node_cnt      = 0;
bool     spec_cores_first     = false;
bool     topo_optional        = false;

/* Global variables */

static job_resources_t *_create_job_resources(int node_cnt)
{
	job_resources_t *job_resrcs_ptr;

	job_resrcs_ptr = create_job_resources();
	job_resrcs_ptr->cpu_array_reps = xcalloc(node_cnt, sizeof(uint32_t));
	job_resrcs_ptr->cpu_array_value = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->cpus = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->cpus_used = xcalloc(node_cnt, sizeof(uint16_t));
	job_resrcs_ptr->memory_allocated = xcalloc(node_cnt, sizeof(uint64_t));
	job_resrcs_ptr->memory_used = xcalloc(node_cnt, sizeof(uint64_t));
	job_resrcs_ptr->nhosts = node_cnt;
	return job_resrcs_ptr;
}

static int _get_avail_cores_on_node(int node_inx, bitstr_t **exc_bitmap)
{
	int exc_cnt = 0, tot_cores;

	xassert(node_inx <= select_node_cnt);

	tot_cores = select_node_record[node_inx].tot_cores;

	if (!exc_bitmap)
		return tot_cores;

	if (is_cons_tres) {
		if (exc_bitmap[node_inx])
			exc_cnt += bit_set_count(exc_bitmap[node_inx]);
	} else if (*exc_bitmap) {
		int coff = cr_get_coremap_offset(node_inx);
		for (int i = 0; i < tot_cores; i++) {
			if (bit_test(*exc_bitmap, coff + i))
				exc_cnt++;
		}
	}
	return tot_cores - exc_cnt;
}

extern char *common_node_state_str(uint16_t node_state)
{
	if (node_state >= NODE_CR_RESERVED)
		return "reserved";	/* Exclusive allocation */
	if (node_state >= NODE_CR_ONE_ROW)
		return "one_row";	/* Dedicated core for this partition */
	return "available";		/* Idle or in-use (shared) */
}

static void _dump_job_res(struct job_resources *job)
{
	char str[64];

	if (job->core_bitmap)
		bit_fmt(str, sizeof(str), job->core_bitmap);
	else
		sprintf(str, "[no core_bitmap]");
	info("DEBUG: Dump job_resources: nhosts %u core_bitmap %s",
	     job->nhosts, str);
}

/*
 * _allocate_sc - Given the job requirements, determine which CPUs/cores
 *                from the given node can be allocated (if any) to this
 *                job. Returns structure identifying the usable resources and
 *                a bitmap of the available cores.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN entire_sockets_only - if true, allocate cores only on sockets that
 *                          have no other allocated cores.
 * IN req_sock_map - OPTIONAL bitmap of required sockets
 * RET resource availability structure, call common_free_avail_res() to free
 */
static avail_res_t *_allocate_sc(job_record_t *job_ptr, bitstr_t *core_map,
				 bitstr_t *part_core_map, const uint32_t node_i,
				 int *cpu_alloc_size, bool entire_sockets_only,
				 bitstr_t *req_sock_map)
{
	uint16_t cpu_count = 0, cpu_cnt = 0, part_cpu_limit = 0xffff;
	uint16_t si, cps, avail_cpus = 0, num_tasks = 0;
	uint32_t c;
	uint32_t core_begin;
	uint32_t core_end;
	struct job_details *details_ptr = job_ptr->details;
	uint16_t cpus_per_task = details_ptr->cpus_per_task;
	uint16_t free_core_count = 0, spec_threads = 0;
	uint16_t i, j;
	uint16_t sockets = select_node_record[node_i].tot_sockets;
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
	avail_res_t *avail_res;


	if (is_cons_tres) {
		core_begin = 0;
		core_end = select_node_record[node_i].tot_cores;
	} else {
		core_begin = cr_get_coremap_offset(node_i);
		core_end = cr_get_coremap_offset(node_i+1);
	}

	memset(free_cores, 0, sockets * sizeof(uint16_t));
	memset(used_cores, 0, sockets * sizeof(uint16_t));
	memset(used_cpu_array, 0, sockets * sizeof(uint32_t));

	if (entire_sockets_only && details_ptr->whole_node &&
	    (details_ptr->core_spec != NO_VAL16)) {
		/* Ignore specialized cores when allocating "entire" socket */
		entire_sockets_only = false;
	}
	if (details_ptr->mc_ptr) {
		uint32_t threads_per_socket;
		multi_core_data_t *mc_ptr = details_ptr->mc_ptr;
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

	/*
	 * These are the job parameters that we must respect:
	 *
	 *   details_ptr->mc_ptr->cores_per_socket (cr_core|cr_socket)
	 *	- min # of cores per socket to allocate to this job
	 *   details_ptr->mc_ptr->sockets_per_node (cr_core|cr_socket)
	 *	- min # of sockets per node to allocate to this job
	 *   details_ptr->mc_ptr->ntasks_per_core (cr_core|cr_socket)
	 *	- number of tasks to launch per core
	 *   details_ptr->mc_ptr->ntasks_per_socket (cr_core|cr_socket)
	 *	- number of tasks to launch per socket
	 *
	 *   details_ptr->ntasks_per_node (all cr_types)
	 *	- total number of tasks to launch on this node
	 *   details_ptr->cpus_per_task (all cr_types)
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
	 */

	/*
	 * Step 1: create and compute core-count-per-socket
	 * arrays and total core counts
	 */
	for (c = core_begin; c < core_end; c++) {
		i = (uint16_t) ((c - core_begin) / cores_per_socket);
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
		/*
		 * if a socket is already in use and entire_sockets_only is
		 * enabled, it cannot be used by this job
		 */
		if (entire_sockets_only && used_cores[i]) {
			free_core_count -= free_cores[i];
			used_cores[i] += free_cores[i];
			free_cores[i] = 0;
		}
		free_cpu_count += free_cores[i] * threads_per_core;
		if (used_cpu_array[i])
			used_cpu_count += used_cores[i] * threads_per_core;
	}

	/* Enforce partition CPU limit, but do not pick specific cores yet */
	if ((job_ptr->part_ptr->max_cpus_per_node != INFINITE) &&
	    (free_cpu_count + used_cpu_count >
	     job_ptr->part_ptr->max_cpus_per_node)) {

		if (is_cons_tres) {
			if (used_cpu_count >=
			    job_ptr->part_ptr->max_cpus_per_node) {
				/* no available CPUs on this node */
				num_tasks = 0;
				goto fini;
			}
			part_cpu_limit = job_ptr->part_ptr->max_cpus_per_node -
				used_cpu_count;
			if ((part_cpu_limit == 1) &&
			    (((ntasks_per_core != 0xffff) &&
			      (ntasks_per_core > part_cpu_limit)) ||
			     (ntasks_per_socket > part_cpu_limit) ||
			     ((ncpus_per_core != 0xffff) &&
			      (ncpus_per_core > part_cpu_limit)) ||
			     (cpus_per_task > part_cpu_limit))) {
				/* insufficient available CPUs on this node */
				num_tasks = 0;
				goto fini;
			}
		} else {
			int excess = free_cpu_count + used_cpu_count -
				job_ptr->part_ptr->max_cpus_per_node;
			int min_excess_cores = min_cores;
			int found_cores;
			excess = (excess + threads_per_core - 1) /
				threads_per_core;
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
					    (free_cores[i] <
					     free_cores[min_free_inx]))
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
				free_cpu_count -= (found_cores *
						   threads_per_core);
				free_cores[min_free_inx] -= found_cores;
				excess -= found_cores;
			}
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

	/*
	 * Step 3: Compute task-related data:
	 *         ntasks_per_socket, ntasks_per_node and cpus_per_task
	 *         to determine the number of tasks to run on this node
	 *
	 * Note: cpus_per_task and ncpus_per_core need to play nice
	 *       2 tasks_per_core vs. 2 cpus_per_task
	 */
	avail_cpus = 0;
	num_tasks = 0;
	threads_per_core = common_cpus_per_core(details_ptr, node_i);

	for (i = 0; i < sockets; i++) {
		uint16_t tmp = free_cores[i] * threads_per_core;
		if ((tmp == 0) && req_sock_map && bit_test(req_sock_map, i)) {
			/* no available resources on required socket */
			num_tasks = 0;
			goto fini;
		}
		avail_cpus += tmp;
		if (ntasks_per_socket)
			num_tasks += MIN(tmp, ntasks_per_socket);
		else
			num_tasks += tmp;
	}

	/*
	 * If job requested exclusive rights to the node don't do the min
	 * here since it will make it so we don't allocate the entire node.
	 */
	if (details_ptr->ntasks_per_node && details_ptr->share_res)
		num_tasks = MIN(num_tasks, details_ptr->ntasks_per_node);

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
		/* how many cpus the job would use on the node */
		avail_cpus = tasks * task_cpus;
		/* subtract out the extra cpus. */
		avail_cpus -= (tasks * (task_cpus - cpus_per_task));
	} else {
		j = avail_cpus / cpus_per_task;
		num_tasks = MIN(num_tasks, j);
		avail_cpus = num_tasks * cpus_per_task;
	}

	if ((details_ptr->ntasks_per_node &&
	     (num_tasks < details_ptr->ntasks_per_node) &&
	     (details_ptr->overcommit == 0)) ||
	    (details_ptr->pn_min_cpus &&
	     (avail_cpus < details_ptr->pn_min_cpus))) {
		/* insufficient resources on this node */
		num_tasks = 0;
		goto fini;
	}

	/*
	 * Step 4 - make sure that ntasks_per_socket is enforced when
	 *          allocating cores
	 */
	if ((ntasks_per_socket != NO_VAL16) &&
	    (ntasks_per_socket != INFINITE16) &&
	    (ntasks_per_socket >= 1)) {
		cps = ntasks_per_socket;
		if (cpus_per_task > 1)
			cps *= cpus_per_task;
	} else
		cps = cores_per_socket * threads_per_core;

	si = 9999;
	tmp_cpt = cpus_per_task;
	for (c = core_begin; c < core_end && (avail_cpus > 0); c++) {
		if (!bit_test(core_map, c))
			continue;

		/* Socket index */
		i = (uint16_t) ((c - core_begin) / cores_per_socket);
		if (free_cores[i] > 0) {
			/*
			 * this socket has free cores, but make sure we don't
			 * use more than are needed for ntasks_per_socket
			 */
			if (si != i) {	/* Start use of next socket */
				si = i;
				cpu_cnt = threads_per_core;
			} else {	/* Continued use of same socket */
				if (cpu_cnt >= cps) {
					/* do not allocate this core */
					bit_clear(core_map, c);
					continue;
				}
				cpu_cnt += threads_per_core;
			}
			free_cores[i]--;
			/*
			 * we have to ensure that cpu_count is not bigger than
			 * avail_cpus due to hyperthreading or this would break
			 * the selection logic providing more CPUs than allowed
			 * after task-related data processing of stage 3
			 */
			if (avail_cpus >= threads_per_core) {
				int used;
				if (is_cons_tres &&
				    (slurmctld_conf.select_type_param &
				     CR_ONE_TASK_PER_CORE) &&
				    (details_ptr->min_gres_cpu > 0)) {
					used = threads_per_core;
				} else if ((ntasks_per_core == 1) &&
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
		bit_nclear(core_map, c, core_end - 1);

fini:
	/* if num_tasks == 0 then clear all bits on this node */
	if (num_tasks == 0) {
		bit_nclear(core_map, core_begin, core_end-1);
		cpu_count = 0;
	}

	if ((details_ptr->core_spec != NO_VAL16) &&
	    (details_ptr->core_spec & CORE_SPEC_THREAD) &&
	    ((select_node_record[node_i].threads == 1) ||
	     (select_node_record[node_i].threads ==
	      select_node_record[node_i].vpus))) {
		/*
		 * NOTE: Currently does not support the situation when Slurm
		 * allocates by core, the thread specialization count occupies
		 * a full core
		 */
		c = details_ptr->core_spec & (~CORE_SPEC_THREAD);
		if (((cpu_count + c) <= select_node_record[node_i].cpus))
			;
		else if (cpu_count > c)
			spec_threads = c;
		else
			spec_threads = cpu_count;
	}
	cpu_count -= spec_threads;

	avail_res = xmalloc(sizeof(avail_res_t));
	avail_res->max_cpus = MIN(cpu_count, part_cpu_limit);

	if (is_cons_tres) {
		avail_res->min_cpus = *cpu_alloc_size;
		avail_res->avail_cores_per_sock =
			xcalloc(sockets, sizeof(uint16_t));
		for (c = core_begin; c < core_end; c++) {
			i = (uint16_t) ((c - core_begin) / cores_per_socket);
			if (bit_test(core_map, c))
				avail_res->avail_cores_per_sock[i]++;
		}
		avail_res->sock_cnt = sockets;
		avail_res->spec_threads = spec_threads;
		avail_res->vpus = select_node_record[node_i].vpus;
		avail_res->avail_cpus = free_cpu_count;
	}

	return avail_res;
}

/*
 * Get configured DefCpuPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t common_get_def_cpu_per_gpu(List job_defaults_list)
{
	uint64_t cpu_per_gpu = NO_VAL64;
	ListIterator iter;
	job_defaults_t *job_defaults;

	if (!job_defaults_list)
		return cpu_per_gpu;

	iter = list_iterator_create(job_defaults_list);
	while ((job_defaults = (job_defaults_t *) list_next(iter))) {
		if (job_defaults->type == JOB_DEF_CPU_PER_GPU) {
			cpu_per_gpu = job_defaults->value;
			break;
		}
	}
	list_iterator_destroy(iter);

	return cpu_per_gpu;
}

/*
 * Get configured DefMemPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
extern uint64_t common_get_def_mem_per_gpu(List job_defaults_list)
{
	uint64_t mem_per_gpu = NO_VAL64;
	ListIterator iter;
	job_defaults_t *job_defaults;

	if (!job_defaults_list)
		return mem_per_gpu;

	iter = list_iterator_create(job_defaults_list);
	while ((job_defaults = (job_defaults_t *) list_next(iter))) {
		if (job_defaults->type == JOB_DEF_MEM_PER_GPU) {
			mem_per_gpu = job_defaults->value;
			break;
		}
	}
	list_iterator_destroy(iter);

	return mem_per_gpu;
}

extern void common_free_avail_res(avail_res_t *avail_res)
{
	if (!avail_res)
		return;

	xfree(avail_res->avail_cores_per_sock);
	FREE_NULL_LIST(avail_res->sock_gres_list);
	xfree(avail_res);
}

/*
 * Return the number of usable logical processors by a given job on
 * some specified node. Returns 0xffff if no limit.
 */
extern int common_cpus_per_core(struct job_details *details, int node_inx)
{
	uint16_t ncpus_per_core = 0xffff;	/* Usable CPUs per core */
	uint16_t threads_per_core = select_node_record[node_inx].vpus;

	if (is_cons_tres &&
	    (slurmctld_conf.select_type_param & CR_ONE_TASK_PER_CORE) &&
	    (details->min_gres_cpu > 0)) {
		/* May override default of 1 CPU per core */
		uint16_t pu_per_core = 0xffff;	/* Usable CPUs per core */
		uint16_t vpus_per_core = select_node_record[node_inx].vpus;
		return MIN(vpus_per_core, pu_per_core);
	}

	if (details && details->mc_ptr) {
		multi_core_data_t *mc_ptr = details->mc_ptr;
		if ((mc_ptr->ntasks_per_core != INFINITE16) &&
		    (mc_ptr->ntasks_per_core)) {
			ncpus_per_core = MIN(threads_per_core,
					     (mc_ptr->ntasks_per_core *
					      details->cpus_per_task));
		}
		if ((mc_ptr->threads_per_core != NO_VAL16) &&
		    (mc_ptr->threads_per_core <  ncpus_per_core)) {
			ncpus_per_core = mc_ptr->threads_per_core;
		}
	}

	threads_per_core = MIN(threads_per_core, ncpus_per_core);

	return threads_per_core;
}

extern void common_init(void)
{
	char *topo_param;

	cr_type = slurmctld_conf.select_type_param;
	if (cr_type)
		verbose("%s loaded with argument %u", plugin_type, cr_type);

	select_debug_flags = slurm_get_debug_flags();

	topo_param = slurm_get_topology_param();
	if (topo_param) {
		if (xstrcasestr(topo_param, "dragonfly"))
			have_dragonfly = true;
		if (xstrcasestr(topo_param, "TopoOptional"))
			topo_optional = true;
		xfree(topo_param);
	}

	priority_flags = slurm_get_priority_flags();

	if (slurm_get_preempt_mode() & PREEMPT_MODE_GANG)
		gang_mode = true;
	else
		gang_mode = false;

	if (plugin_id == SELECT_PLUGIN_CONS_TRES)
		is_cons_tres = true;
}

extern void common_fini(void)
{
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("%s shutting down ...", plugin_type);
	else
		verbose("%s shutting down ...", plugin_type);

	node_data_destroy(select_node_usage, select_node_record);
	select_node_record = NULL;
	select_node_usage = NULL;
	part_data_destroy_res(select_part_record);
	select_part_record = NULL;
	cr_fini_global_core_data();
}

/*
 * Bit a core bitmap array of available cores
 * node_bitmap IN - Nodes available for use
 * core_spec IN - Specialized core specification, NO_VAL16 if none
 * RET core bitmap array, one per node. Use free_core_array() to release memory
 */
extern bitstr_t **common_mark_avail_cores(
	bitstr_t *node_bitmap, uint16_t core_spec)
{
	bitstr_t **avail_cores;
	int from_core, to_core, incr_core, from_sock, to_sock, incr_sock;
	int res_core, res_sock, res_off;
	int n, n_first, n_last;
	int c;
	int rem_core_spec, node_core_spec, thread_spec = 0;
	node_record_t *node_ptr;
	bitstr_t *core_map = NULL;
	uint16_t use_spec_cores = slurmctld_conf.conf_flags & CTL_CONF_ASRU;
	node_res_record_t *node_res_ptr = NULL;
	uint32_t coff;

	if (is_cons_tres) {
		avail_cores = build_core_array();
	} else {
		core_map = bit_alloc(
			cr_get_coremap_offset(bit_size(node_bitmap)));
		avail_cores = build_core_array();
		*avail_cores = core_map;
	}

	if ((core_spec != NO_VAL16) &&
	    (core_spec & CORE_SPEC_THREAD)) {	/* Reserving threads */
		thread_spec = core_spec & (~CORE_SPEC_THREAD);
		core_spec = NO_VAL16;		/* Don't remove cores */
	}

	n_first = bit_ffs(node_bitmap);
	if (n_first != -1)
		n_last = bit_fls(node_bitmap);
	else
		n_last = -2;
	for (n = n_first; n <= n_last; n++) {
		if (!bit_test(node_bitmap, n))
			continue;

		node_res_ptr = &select_node_record[n];
		node_ptr = node_res_ptr->node_ptr;

		if (is_cons_tres) {
			c    = 0;
			coff = node_res_ptr->tot_cores;
			avail_cores[n] = bit_alloc(node_res_ptr->tot_cores);
			core_map = avail_cores[n];
		} else {
			c    = cr_get_coremap_offset(n);
			coff = cr_get_coremap_offset(n+1);
		}

		if ((core_spec != NO_VAL16) &&
		    (core_spec >= node_res_ptr->tot_cores)) {
			bit_clear(node_bitmap, n);
			continue;
		}

		bit_nset(core_map, c, coff - 1);

		/* Job can't over-ride system defaults */
		if (use_spec_cores && (core_spec == 0))
			continue;

		if (thread_spec &&
		    (node_res_ptr->cpus == node_res_ptr->tot_cores))
			/* Each core has one thead, reserve cores here */
			node_core_spec = thread_spec;
		else
			node_core_spec = core_spec;

		/*
		 * remove node's specialized cores accounting toward the
		 * requested limit if allowed by configuration
		 */
		rem_core_spec = node_core_spec;
		if (node_ptr->node_spec_bitmap) {
			for (int i = 0; i < node_res_ptr->tot_cores; i++) {
				if (!bit_test(node_ptr->node_spec_bitmap, i)) {
					bit_clear(core_map, c + i);
					if (!use_spec_cores)
						continue;
					rem_core_spec--;
					if (!rem_core_spec)
						break;
				}
			}
		}

		if (!use_spec_cores || !rem_core_spec ||
		    (node_core_spec == NO_VAL16))
			continue;

		/* if more cores need to be specialized, look for
		 * them in the non-specialized cores */
		if (spec_cores_first) {
			from_core = 0;
			to_core   = node_res_ptr->cores;
			incr_core = 1;
			from_sock = 0;
			to_sock   = node_res_ptr->tot_sockets;
			incr_sock = 1;
		} else {
			from_core = node_res_ptr->cores - 1;
			to_core   = -1;
			incr_core = -1;
			from_sock = node_res_ptr->tot_sockets - 1;
			to_sock   = -1;
			incr_sock = -1;
		}
		for (res_core = from_core;
		     ((rem_core_spec > 0) && (res_core != to_core));
		     res_core += incr_core) {
			for (res_sock = from_sock;
			     ((rem_core_spec > 0) && (res_sock != to_sock));
			     res_sock += incr_sock) {
				res_off = c + res_core +
					(res_sock * node_res_ptr->cores);
				if (!bit_test(core_map, res_off))
					continue;
				bit_clear(core_map, res_off);
				rem_core_spec--;
			}
		}
	}

	return avail_cores;
}

/*
 * common_allocate_cores - Given the job requirements, determine which cores
 *                   from the given node can be allocated (if any) to this
 *                   job. Returns the number of cpus that can be used by
 *                   this node AND a bitmap of the selected cores.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN cpu_type      - if true, allocate CPUs rather than cores
 * IN req_sock_map - OPTIONAL bitmap of required sockets
 * RET resource availability structure, call common_free_avail_res() to free
 */
extern avail_res_t *common_allocate_cores(job_record_t *job_ptr,
					  bitstr_t *core_map,
					  bitstr_t *part_core_map,
					  const uint32_t node_i,
					  int *cpu_alloc_size,
					  bool cpu_type,
					  bitstr_t *req_sock_map)
{
	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, false, req_sock_map);
}

/*
 * common_allocate_sockets - Given the job requirements, determine which sockets
 *                     from the given node can be allocated (if any) to this
 *                     job. Returns the number of cpus that can be used by
 *                     this node AND a core-level bitmap of the selected
 *                     sockets.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN req_sock_map - OPTIONAL bitmap of required sockets
 * RET resource availability structure, call common_free_avail_res() to free
 */
extern avail_res_t *common_allocate_sockets(job_record_t *job_ptr,
					    bitstr_t *core_map,
					    bitstr_t *part_core_map,
					    const uint32_t node_i,
					    int *cpu_alloc_size,
					    bitstr_t *req_sock_map)
{
	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, true, req_sock_map);
}

extern int select_p_state_save(char *dir_name)
{
	/* nothing to save */
	return SLURM_SUCCESS;
}

/* This is Part 2 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. See select_p_node_init for the
 * whole story.
 */
extern int select_p_state_restore(char *dir_name)
{
	/* nothing to restore */
	return SLURM_SUCCESS;
}

/* This is Part 3 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. See select_p_node_init for the
 * whole story.
 */
extern int select_p_job_init(List job_list)
{
	/* nothing to initialize for jobs */
	return SLURM_SUCCESS;
}

/* This plugin does not generate a node ranking. */
extern bool select_p_node_ranking(node_record_t *node_ptr, int node_cnt)
{
	return false;
}

/* This is Part 1 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. The whole story goes like this:
 *
 * Step 1: select_g_node_init          : initializes the global node arrays
 * Step 2: select_g_state_restore      : NO-OP - nothing to restore
 * Step 3: select_g_job_init           : NO-OP - nothing to initialize
 * Step 4: select_g_select_nodeinfo_set: called from reset_job_bitmaps() with
 *                                       each valid recovered job_ptr AND from
 *                                       select_nodes(), this procedure adds
 *                                       job data to the 'select_part_record'
 *                                       global array
 */
extern int select_p_node_init(node_record_t *node_ptr, int node_cnt)
{
	char *preempt_type, *sched_params, *tmp_ptr;
	uint32_t cume_cores = 0;
	int i;

	info("%s: %s", plugin_type, __func__);
	if ((cr_type & (CR_CPU | CR_CORE | CR_SOCKET)) == 0) {
		fatal("Invalid SelectTypeParameters: %s (%u), "
		      "You need at least CR_(CPU|CORE|SOCKET)*",
		      select_type_param_string(cr_type), cr_type);
	}
	if (node_ptr == NULL) {
		error("select_p_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}
	if (node_cnt < 0) {
		error("select_p_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	sched_params = slurm_get_sched_params();
	if (xstrcasestr(sched_params, "preempt_strict_order"))
		preempt_strict_order = true;
	else
		preempt_strict_order = false;
	if ((tmp_ptr = xstrcasestr(sched_params, "preempt_reorder_count="))) {
		preempt_reorder_cnt = atoi(tmp_ptr + 22);
		if (preempt_reorder_cnt < 0) {
			error("Invalid SchedulerParameters preempt_reorder_count: %d",
			      preempt_reorder_cnt);
			preempt_reorder_cnt = 1;	/* Use default value */
		}
	}
	if ((tmp_ptr = xstrcasestr(sched_params, "bf_window_linear="))) {
		bf_window_scale = atoi(tmp_ptr + 17);
		if (bf_window_scale <= 0) {
			error("Invalid SchedulerParameters bf_window_linear: %d",
			      bf_window_scale);
			bf_window_scale = 0;		/* Use default value */
		}
	} else
		bf_window_scale = 0;

	if (xstrcasestr(sched_params, "pack_serial_at_end"))
		pack_serial_at_end = true;
	else
		pack_serial_at_end = false;
	if (xstrcasestr(sched_params, "spec_cores_first"))
		spec_cores_first = true;
	else
		spec_cores_first = false;
	if (xstrcasestr(sched_params, "bf_busy_nodes"))
		backfill_busy_nodes = true;
	else
		backfill_busy_nodes = false;
	xfree(sched_params);

	preempt_type = slurm_get_preempt_type();
	preempt_by_part = false;
	preempt_by_qos = false;
	if (preempt_type) {
		if (xstrcasestr(preempt_type, "partition"))
			preempt_by_part = true;
		if (xstrcasestr(preempt_type, "qos"))
			preempt_by_qos = true;
		xfree(preempt_type);
	}

	/* initial global core data structures */
	select_state_initializing = true;
	cr_init_global_core_data(node_ptr, node_cnt);

	node_data_destroy(select_node_usage, select_node_record);
	select_node_cnt = node_cnt;

	if (is_cons_tres)
		core_array_size = select_node_cnt;

	select_node_record = xcalloc(select_node_cnt,
				     sizeof(node_res_record_t));
	select_node_usage  = xcalloc(select_node_cnt,
				     sizeof(node_use_record_t));

	for (i = 0; i < select_node_cnt; i++) {
		config_record_t *config_ptr;
		select_node_record[i].node_ptr = &node_ptr[i];
		select_node_record[i].mem_spec_limit =
			node_ptr[i].mem_spec_limit;

		config_ptr = node_ptr[i].config_ptr;
		select_node_record[i].cpus    = config_ptr->cpus;
		select_node_record[i].boards  = config_ptr->boards;
		select_node_record[i].sockets = config_ptr->sockets;
		select_node_record[i].cores   = config_ptr->cores;
		select_node_record[i].threads = config_ptr->threads;
		select_node_record[i].vpus    = config_ptr->threads;
		select_node_record[i].real_memory = config_ptr->real_memory;

		select_node_record[i].tot_sockets =
			select_node_record[i].boards *
			select_node_record[i].sockets;
		select_node_record[i].tot_cores =
			select_node_record[i].tot_sockets *
			select_node_record[i].cores;
		cume_cores += select_node_record[i].tot_cores;
		select_node_record[i].cume_cores = cume_cores;
		if (select_node_record[i].tot_cores >=
		    select_node_record[i].cpus)
			select_node_record[i].vpus = 1;

		if ((select_node_record[i].cpus !=
		     select_node_record[i].tot_cores) &&
		    (select_node_record[i].cpus !=
		     select_node_record[i].tot_cores *
		     select_node_record[i].threads))
			fatal("NodeName=%s CPUs=%u doesn't match neither Sockets(%u)*CoresPerSocket(%u)=(%u) nor Sockets(%u)*CoresPerSocket(%u)*ThreadsPerCore(%u)=(%u).  Please fix your slurm.conf.",
			      node_ptr[i].name,
			      select_node_record[i].cpus,
			      select_node_record[i].tot_sockets,
			      select_node_record[i].cores,
			      select_node_record[i].tot_cores,
			      select_node_record[i].tot_sockets,
			      select_node_record[i].cores,
			      select_node_record[i].threads,
			      select_node_record[i].tot_cores *
			      select_node_record[i].threads);

		select_node_usage[i].node_state = NODE_CR_AVAILABLE;
		gres_plugin_node_state_dealloc_all(
			select_node_record[i].node_ptr->gres_list);
	}
	part_data_create_array();
	node_data_dump();

	return SLURM_SUCCESS;
}

extern int select_p_job_begin(job_record_t *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_ready(job_record_t *job_ptr)
{
	int i, i_first, i_last;
	node_record_t *node_ptr;

	if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr)) {
		/* Gang scheduling might suspend job immediately */
		return 0;
	}

	if ((job_ptr->node_bitmap == NULL) ||
	    ((i_first = bit_ffs(job_ptr->node_bitmap)) == -1))
		return READY_NODE_STATE;
	i_last  = bit_fls(job_ptr->node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		node_ptr = node_record_table_ptr + i;
		if (IS_NODE_POWER_SAVE(node_ptr) || IS_NODE_POWER_UP(node_ptr))
			return 0;
	}

	return READY_NODE_STATE;
}

extern int select_p_job_expand(job_record_t *from_job_ptr,
			       job_record_t *to_job_ptr)
{
	job_resources_t *from_job_resrcs_ptr, *to_job_resrcs_ptr,
		*new_job_resrcs_ptr;
	node_record_t *node_ptr;
	int first_bit, last_bit, i, node_cnt;
	bool from_node_used, to_node_used;
	int from_node_offset, to_node_offset, new_node_offset;
	bitstr_t *tmp_bitmap, *tmp_bitmap2;

	xassert(from_job_ptr);
	xassert(from_job_ptr->details);
	xassert(to_job_ptr);
	xassert(to_job_ptr->details);

	if (from_job_ptr->job_id == to_job_ptr->job_id) {
		error("%s: %s: attempt to merge %pJ with self",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}

	from_job_resrcs_ptr = from_job_ptr->job_resrcs;
	if ((from_job_resrcs_ptr == NULL) ||
	    (from_job_resrcs_ptr->cpus == NULL) ||
	    (from_job_resrcs_ptr->core_bitmap == NULL) ||
	    (from_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %s: %pJ lacks a job_resources struct",
		      plugin_type, __func__, from_job_ptr);
		return SLURM_ERROR;
	}
	to_job_resrcs_ptr = to_job_ptr->job_resrcs;
	if ((to_job_resrcs_ptr == NULL) ||
	    (to_job_resrcs_ptr->cpus == NULL) ||
	    (to_job_resrcs_ptr->core_bitmap == NULL) ||
	    (to_job_resrcs_ptr->node_bitmap == NULL)) {
		error("%s: %s: %pJ lacks a job_resources struct",
		      plugin_type, __func__, to_job_ptr);
		return SLURM_ERROR;
	}

	if (is_cons_tres) {
		if (to_job_ptr->gres_list) {
			/* Can't reset gres/mps fields today */
			error("%s: %s: %pJ has allocated GRES",
			      plugin_type, __func__, to_job_ptr);
			return SLURM_ERROR;
		}
		if (from_job_ptr->gres_list) {
			/* Can't reset gres/mps fields today */
			error("%s: %s: %pJ has allocated GRES",
			      plugin_type, __func__, from_job_ptr);
			return SLURM_ERROR;
		}
	}

	(void) job_res_rm_job(select_part_record, select_node_usage,
			      from_job_ptr, 0, true, NULL);
	(void) job_res_rm_job(select_part_record, select_node_usage,
			      to_job_ptr, 0, true, NULL);

	if (to_job_resrcs_ptr->core_bitmap_used) {
		i = bit_size(to_job_resrcs_ptr->core_bitmap_used);
		bit_nclear(to_job_resrcs_ptr->core_bitmap_used, 0, i-1);
	}

	tmp_bitmap = bit_copy(to_job_resrcs_ptr->node_bitmap);
	bit_or(tmp_bitmap, from_job_resrcs_ptr->node_bitmap);
	tmp_bitmap2 = bit_copy(to_job_ptr->node_bitmap);
	bit_or(tmp_bitmap2, from_job_ptr->node_bitmap);
	bit_and(tmp_bitmap, tmp_bitmap2);
	bit_free(tmp_bitmap2);
	node_cnt = bit_set_count(tmp_bitmap);

	new_job_resrcs_ptr = _create_job_resources(node_cnt);
	new_job_resrcs_ptr->ncpus = from_job_resrcs_ptr->ncpus +
		to_job_resrcs_ptr->ncpus;
	new_job_resrcs_ptr->node_req = to_job_resrcs_ptr->node_req;
	new_job_resrcs_ptr->node_bitmap = tmp_bitmap;
	new_job_resrcs_ptr->nodes = bitmap2node_name(new_job_resrcs_ptr->
						     node_bitmap);
	new_job_resrcs_ptr->whole_node = to_job_resrcs_ptr->whole_node;
	build_job_resources(new_job_resrcs_ptr, node_record_table_ptr);
	xfree(to_job_ptr->node_addr);
	to_job_ptr->node_addr = xcalloc(node_cnt, sizeof(slurm_addr_t));
	to_job_ptr->total_cpus = 0;

	first_bit = MIN(bit_ffs(from_job_resrcs_ptr->node_bitmap),
			bit_ffs(to_job_resrcs_ptr->node_bitmap));
	last_bit =  MAX(bit_fls(from_job_resrcs_ptr->node_bitmap),
			bit_fls(to_job_resrcs_ptr->node_bitmap));
	from_node_offset = to_node_offset = new_node_offset = -1;
	for (i = first_bit; i <= last_bit; i++) {
		from_node_used = to_node_used = false;
		if (bit_test(from_job_resrcs_ptr->node_bitmap, i)) {
			from_node_used = bit_test(from_job_ptr->node_bitmap,i);
			from_node_offset++;
		}
		if (bit_test(to_job_resrcs_ptr->node_bitmap, i)) {
			to_node_used = bit_test(to_job_ptr->node_bitmap, i);
			to_node_offset++;
		}
		if (!from_node_used && !to_node_used)
			continue;
		new_node_offset++;
		node_ptr = node_record_table_ptr + i;
		memcpy(&to_job_ptr->node_addr[new_node_offset],
		       &node_ptr->slurm_addr, sizeof(slurm_addr_t));
		if (from_node_used) {
			/*
			 * Merge alloc info from both "from" and "to" jobs,
			 * leave "from" job with no allocated CPUs or memory
			 *
			 * The following fields should be zero:
			 * from_job_resrcs_ptr->cpus_used[from_node_offset]
			 * from_job_resrcs_ptr->memory_used[from_node_offset];
			 */
			new_job_resrcs_ptr->cpus[new_node_offset] =
				from_job_resrcs_ptr->cpus[from_node_offset];
			from_job_resrcs_ptr->cpus[from_node_offset] = 0;
			new_job_resrcs_ptr->memory_allocated[new_node_offset] =
				from_job_resrcs_ptr->
				memory_allocated[from_node_offset];
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						from_job_resrcs_ptr,
						from_node_offset);
		}
		if (to_node_used) {
			/*
			 * Merge alloc info from both "from" and "to" jobs
			 *
			 * DO NOT double count the allocated CPUs in partition
			 * with Shared nodes
			 */
			new_job_resrcs_ptr->cpus[new_node_offset] +=
				to_job_resrcs_ptr->cpus[to_node_offset];
			new_job_resrcs_ptr->cpus_used[new_node_offset] +=
				to_job_resrcs_ptr->cpus_used[to_node_offset];
			new_job_resrcs_ptr->memory_allocated[new_node_offset]+=
				to_job_resrcs_ptr->
				memory_allocated[to_node_offset];
			new_job_resrcs_ptr->memory_used[new_node_offset] +=
				to_job_resrcs_ptr->memory_used[to_node_offset];
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						to_job_resrcs_ptr,
						to_node_offset);
			if (from_node_used) {
				/* Adjust CPU count for shared CPUs */
				int from_core_cnt, to_core_cnt, new_core_cnt;
				from_core_cnt = count_job_resources_node(
					from_job_resrcs_ptr,
					from_node_offset);
				to_core_cnt = count_job_resources_node(
					to_job_resrcs_ptr,
					to_node_offset);
				new_core_cnt = count_job_resources_node(
					new_job_resrcs_ptr,
					new_node_offset);
				if ((from_core_cnt + to_core_cnt) !=
				    new_core_cnt) {
					new_job_resrcs_ptr->
						cpus[new_node_offset] *=
						new_core_cnt;
					new_job_resrcs_ptr->
						cpus[new_node_offset] /=
						(from_core_cnt + to_core_cnt);
				}
			}
		}
		if (to_job_ptr->details->whole_node == 1) {
			to_job_ptr->total_cpus += select_node_record[i].cpus;
		} else {
			to_job_ptr->total_cpus += new_job_resrcs_ptr->
				cpus[new_node_offset];
		}
	}
	build_job_resources_cpu_array(new_job_resrcs_ptr);
	gres_plugin_job_merge(from_job_ptr->gres_list,
			      from_job_resrcs_ptr->node_bitmap,
			      to_job_ptr->gres_list,
			      to_job_resrcs_ptr->node_bitmap);

	/* Now swap data: "new" -> "to" and clear "from" */
	free_job_resources(&to_job_ptr->job_resrcs);
	to_job_ptr->job_resrcs = new_job_resrcs_ptr;

	to_job_ptr->cpu_cnt = to_job_ptr->total_cpus;
	to_job_ptr->details->min_cpus = to_job_ptr->total_cpus;
	to_job_ptr->details->max_cpus = to_job_ptr->total_cpus;
	from_job_ptr->total_cpus   = 0;
	from_job_resrcs_ptr->ncpus = 0;
	from_job_ptr->details->min_cpus = 0;
	from_job_ptr->details->max_cpus = 0;

	from_job_ptr->total_nodes   = 0;
	from_job_resrcs_ptr->nhosts = 0;
	from_job_ptr->node_cnt      = 0;
	from_job_ptr->details->min_nodes = 0;
	to_job_ptr->total_nodes     = new_job_resrcs_ptr->nhosts;
	to_job_ptr->node_cnt        = new_job_resrcs_ptr->nhosts;

	bit_or(to_job_ptr->node_bitmap, from_job_ptr->node_bitmap);
	bit_nclear(from_job_ptr->node_bitmap, 0, (node_record_count - 1));
	bit_nclear(from_job_resrcs_ptr->node_bitmap, 0,
		   (node_record_count - 1));

	xfree(to_job_ptr->nodes);
	to_job_ptr->nodes = xstrdup(new_job_resrcs_ptr->nodes);
	xfree(from_job_ptr->nodes);
	from_job_ptr->nodes = xstrdup("");
	xfree(from_job_resrcs_ptr->nodes);
	from_job_resrcs_ptr->nodes = xstrdup("");

	(void) job_res_add_job(to_job_ptr, 0);

	return SLURM_SUCCESS;
}

extern int select_p_job_resized(job_record_t *job_ptr, node_record_t *node_ptr)
{
	part_res_record_t *part_record_ptr = select_part_record;
	node_use_record_t *node_usage = select_node_usage;
	struct job_resources *job = job_ptr->job_resrcs;
	part_res_record_t *p_ptr;
	int i, i_first, i_last, node_inx, n;
	List gres_list;
	bool old_job = false;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (!job || !job->core_bitmap) {
		error("%s: %s: %pJ has no job_resrcs info",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	debug3("%s: %s: %pJ node %s",
	       plugin_type, __func__, job_ptr, node_ptr->name);
	if (job_ptr->start_time < slurmctld_config.boot_time)
		old_job = true;
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		_dump_job_res(job);

	/* subtract memory */
	node_inx  = node_ptr - node_record_table_ptr;
	i_first = bit_ffs(job->node_bitmap);
	if (i_first != -1)
		i_last  = bit_fls(job->node_bitmap);
	else
		i_last = -2;
	for (i = i_first, n = 0; i <= i_last; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		if (i != node_inx) {
			n++;
			continue;
		}

		if (job->cpus[n] == 0) {
			info("%s: %s: attempt to remove node %s from %pJ again",
			     plugin_type, __func__, node_ptr->name, job_ptr);
			return SLURM_SUCCESS;
		}

		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_plugin_job_dealloc(job_ptr->gres_list, gres_list, n,
					job_ptr->job_id, node_ptr->name,
					old_job, job_ptr->user_id, true);
		gres_plugin_node_state_log(gres_list, node_ptr->name);

		if (node_usage[i].alloc_memory < job->memory_allocated[n]) {
			error("%s: %s: node %s memory is underallocated (%"PRIu64"-%"PRIu64") for %pJ",
			      plugin_type,
			      __func__, node_ptr->name,
			      node_usage[i].alloc_memory,
			      job->memory_allocated[n], job_ptr);
			node_usage[i].alloc_memory = 0;
		} else
			node_usage[i].alloc_memory -= job->memory_allocated[n];

		extract_job_resources_node(job, n);

		break;
	}

	if (IS_JOB_SUSPENDED(job_ptr))
		return SLURM_SUCCESS;	/* No cores allocated to the job now */

	/* subtract cores, reconstruct rows with remaining jobs */
	if (!job_ptr->part_ptr) {
		error("%s: %s: removed %pJ does not have a partition assigned",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!p_ptr) {
		error("%s: %s: removed %pJ could not find part %s",
		      plugin_type, __func__, job_ptr, job_ptr->part_ptr->name);
		return SLURM_ERROR;
	}

	if (!p_ptr->row)
		return SLURM_SUCCESS;

	/* look for the job in the partition's job_list */
	n = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		uint32_t j;
		for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
			if (p_ptr->row[i].job_list[j] != job)
				continue;
			debug3("%s: %s: found %pJ in part %s row %u",
			       plugin_type, __func__, job_ptr,
			       p_ptr->part_ptr->name, i);
			/* found job - we're done, don't actually remove */
			n = 1;
			i = p_ptr->num_rows;
			break;
		}
	}
	if (n == 0) {
		error("%s: %s: could not find %pJ in partition %s",
		      plugin_type, __func__, job_ptr, p_ptr->part_ptr->name);
		return SLURM_ERROR;
	}


	/* some node of job removed from core-bitmap, so rebuild core bitmaps */
	part_data_build_row_bitmaps(p_ptr, NULL);

	/*
	 * Adjust the node_state of the node removed from this job.
	 * If all cores are now available, set node_state = NODE_CR_AVAILABLE
	 */
	if (node_usage[node_inx].node_state >= job->node_req) {
		node_usage[node_inx].node_state -= job->node_req;
	} else {
		error("%s: %s: node_state miscount", plugin_type, __func__);
		node_usage[node_inx].node_state = NODE_CR_AVAILABLE;
	}

	return SLURM_SUCCESS;
}

extern int select_p_job_signal(job_record_t *job_ptr, int signal)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	return SLURM_SUCCESS;
}

extern int select_p_job_mem_confirm(job_record_t *job_ptr)
{
	int i_first, i_last, i, offset;
	uint64_t avail_mem, lowest_mem = 0;

	xassert(job_ptr);

	if (!(job_ptr->bit_flags & NODE_MEM_CALC))
		return SLURM_SUCCESS;
	if ((job_ptr->details == NULL) ||
	    (job_ptr->job_resrcs == NULL) ||
	    (job_ptr->job_resrcs->node_bitmap == NULL) ||
	    (job_ptr->job_resrcs->memory_allocated == NULL))
		return SLURM_ERROR;
	i_first = bit_ffs(job_ptr->job_resrcs->node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(job_ptr->job_resrcs->node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first, offset = 0; i <= i_last; i++) {
		if (!bit_test(job_ptr->job_resrcs->node_bitmap, i))
			continue;
		avail_mem = select_node_record[i].real_memory -
			select_node_record[i].mem_spec_limit;
		job_ptr->job_resrcs->memory_allocated[offset] = avail_mem;
		select_node_usage[i].alloc_memory = avail_mem;
		if ((offset == 0) || (lowest_mem > avail_mem))
			lowest_mem = avail_mem;
		offset++;
	}
	job_ptr->details->pn_min_memory = lowest_mem;

	return SLURM_SUCCESS;
}

extern int select_p_job_fini(job_record_t *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("%s: %s: %pJ", plugin_type, __func__, job_ptr);

	job_res_rm_job(select_part_record, select_node_usage,
		       job_ptr, 0, true, NULL);

	return SLURM_SUCCESS;
}

/* NOTE: This function is not called with gang scheduling because it
 * needs to track how many jobs are running or suspended on each node.
 * This sum is compared with the partition's Shared parameter */
extern int select_p_job_suspend(job_record_t *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (indf_susp)
			info("%s: %s: %pJ indf_susp", plugin_type, __func__,
			     job_ptr);
		else
			info("%s: %s: %pJ", plugin_type, __func__, job_ptr);
	}

	if (!indf_susp)
		return SLURM_SUCCESS;

	return job_res_rm_job(select_part_record, select_node_usage,
			      job_ptr, 2, false, NULL);
}

/* See NOTE with select_p_job_suspend() above */
extern int select_p_job_resume(job_record_t *job_ptr, bool indf_susp)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (indf_susp)
			info("%s: %s: %pJ indf_susp", plugin_type, __func__,
			     job_ptr);
		else
			info("%s: %s: %pJ", plugin_type, __func__, job_ptr);
	}
	if (!indf_susp)
		return SLURM_SUCCESS;

	return job_res_add_job(job_ptr, 2);
}

extern bitstr_t *select_p_step_pick_nodes(job_record_t *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_step_start(step_record_t *step_ptr)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_step_finish(step_record_t *step_ptr, bool killing_step)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	select_nodeinfo_t *nodeinfo_empty = NULL;

	if (!nodeinfo) {
		/*
		 * We should never get here,
		 * but avoid abort with bad data structures
		 */
		error("%s: nodeinfo is NULL", __func__);
		nodeinfo_empty = xmalloc(sizeof(select_nodeinfo_t));
		nodeinfo = nodeinfo_empty;
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		pack16(nodeinfo->alloc_cpus, buffer);
		pack64(nodeinfo->alloc_memory, buffer);
		packstr(nodeinfo->tres_alloc_fmt_str, buffer);
		packdouble(nodeinfo->tres_alloc_weighted, buffer);
	}
	xfree(nodeinfo_empty);

	return SLURM_SUCCESS;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(select_nodeinfo_t));

	nodeinfo->magic = nodeinfo_magic;

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		if (nodeinfo->magic != nodeinfo_magic) {
			error("%s: nodeinfo magic bad", __func__);
			return EINVAL;
		}
		xfree(nodeinfo->tres_alloc_cnt);
		xfree(nodeinfo->tres_alloc_fmt_str);
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	uint32_t uint32_tmp;
	select_nodeinfo_t *nodeinfo_ptr = NULL;

	nodeinfo_ptr = select_p_select_nodeinfo_alloc();
	*nodeinfo = nodeinfo_ptr;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack16(&nodeinfo_ptr->alloc_cpus, buffer);
		safe_unpack64(&nodeinfo_ptr->alloc_memory, buffer);
		safe_unpackstr_xmalloc(&nodeinfo_ptr->tres_alloc_fmt_str,
				       &uint32_tmp, buffer);
		safe_unpackdouble(&nodeinfo_ptr->tres_alloc_weighted, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	error("%s: error unpacking here", __func__);
	select_p_select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;

	return SLURM_ERROR;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	static time_t last_set_all = 0;
	part_res_record_t *p_ptr;
	node_record_t *node_ptr = NULL;
	int i, n;
	uint32_t alloc_cpus, alloc_cores, node_cores, node_cpus, node_threads;
	uint32_t node_boards, node_sockets, total_node_cores;
	bitstr_t **alloc_core_bitmap = NULL;
	List gres_list;

	/*
	 * only set this once when the last_node_update is newer than
	 * the last time we set things up.
	 */
	if (last_set_all && (last_node_update < last_set_all)) {
		debug2("%s: Node data hasn't changed since %ld", __func__,
		       (long)last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;
	}
	last_set_all = last_node_update;

	/*
	 * Build core bitmap array representing all cores allocated to all
	 * active jobs (running or preempted jobs)
	 */
	for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			if (!alloc_core_bitmap) {
				alloc_core_bitmap =
					copy_core_array(
						p_ptr->row[i].row_bitmap);
			} else {
				core_array_or(alloc_core_bitmap,
					      p_ptr->row[i].row_bitmap);
			}
		}
	}

	for (n = 0, node_ptr = node_record_table_ptr;
	     n < select_node_cnt; n++, node_ptr++) {
		select_nodeinfo_t *nodeinfo = NULL;
		/*
		 * We have to use the '_g_' here to make sure we get the
		 * correct data to work on.  i.e. select/cray calls this plugin
		 * and has it's own struct.
		 */
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
					     SELECT_NODEDATA_PTR, 0,
					     (void *)&nodeinfo);
		if (!nodeinfo) {
			error("%s: no nodeinfo returned from structure",
			      __func__);
			continue;
		}

		node_boards  = node_ptr->config_ptr->boards;
		node_sockets = node_ptr->config_ptr->sockets;
		node_cores   = node_ptr->config_ptr->cores;
		node_cpus    = node_ptr->config_ptr->cpus;
		node_threads = node_ptr->config_ptr->threads;

		if (is_cons_tres) {
			if (alloc_core_bitmap && alloc_core_bitmap[n])
				alloc_cores = bit_set_count(
					alloc_core_bitmap[n]);
			else
				alloc_cores = 0;

			total_node_cores =
				node_boards * node_sockets * node_cores;
		} else {
			int start = cr_get_coremap_offset(n);
			int end = cr_get_coremap_offset(n + 1);
			if (alloc_core_bitmap)
				alloc_cores = bit_set_count_range(
					*alloc_core_bitmap,
					start, end);
			else
				alloc_cores = 0;

			total_node_cores = end - start;
		}

		/*
		 * Administrator could resume suspended jobs and oversubscribe
		 * cores, avoid reporting more cores in use than configured
		 */
		if (alloc_cores > total_node_cores)
			alloc_cpus = total_node_cores;
		else
			alloc_cpus = alloc_cores;

		/*
		 * The minimum allocatable unit may a core, so scale by thread
		 * count up to the proper CPU count as needed
		 */
		if (total_node_cores < node_cpus)
			alloc_cpus *= node_threads;
		nodeinfo->alloc_cpus = alloc_cpus;

		if (select_node_record) {
			nodeinfo->alloc_memory =
				select_node_usage[n].alloc_memory;
		} else {
			nodeinfo->alloc_memory = 0;
		}

		/* Build allocated TRES info */
		if (!nodeinfo->tres_alloc_cnt)
			nodeinfo->tres_alloc_cnt = xcalloc(slurmctld_tres_cnt,
							   sizeof(uint64_t));
		nodeinfo->tres_alloc_cnt[TRES_ARRAY_CPU] = alloc_cpus;
		nodeinfo->tres_alloc_cnt[TRES_ARRAY_MEM] =
			nodeinfo->alloc_memory;
		if (select_node_usage[n].gres_list)
			gres_list = select_node_usage[n].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_set_node_tres_cnt(gres_list, nodeinfo->tres_alloc_cnt,
				       false);

		xfree(nodeinfo->tres_alloc_fmt_str);
		nodeinfo->tres_alloc_fmt_str =
			assoc_mgr_make_tres_str_from_array(
				nodeinfo->tres_alloc_cnt,
				TRES_STR_CONVERT_UNITS, false);
		nodeinfo->tres_alloc_weighted =
			assoc_mgr_tres_weighted(nodeinfo->tres_alloc_cnt,
						node_ptr->config_ptr->tres_weights,
						priority_flags, false);
	}
	free_core_array(&alloc_core_bitmap);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(job_record_t *job_ptr)
{
	int rc;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (IS_JOB_RUNNING(job_ptr))
		rc = job_res_add_job(job_ptr, 0);
	else if (IS_JOB_SUSPENDED(job_ptr)) {
		if (job_ptr->priority == 0)
			rc = job_res_add_job(job_ptr, 1);
		else	/* Gang schedule suspend */
			rc = job_res_add_job(job_ptr, 0);
	} else
		return SLURM_SUCCESS;

	gres_plugin_job_state_log(job_ptr->gres_list, job_ptr->job_id);

	return rc;
}

extern int select_p_select_nodeinfo_get(select_nodeinfo_t *nodeinfo,
					enum select_nodedata_type dinfo,
					enum node_states state,
					void *data)
{
	int rc = SLURM_SUCCESS;
	uint16_t *uint16 = (uint16_t *) data;
	uint64_t *uint64 = (uint64_t *) data;
	char **tmp_char = (char **) data;
	double *tmp_double = (double *) data;
	select_nodeinfo_t **select_nodeinfo = (select_nodeinfo_t **) data;

	if (nodeinfo == NULL) {
		error("%s: nodeinfo not set", __func__);
		return SLURM_ERROR;
	}

	if (nodeinfo->magic != nodeinfo_magic) {
		error("%s: jobinfo magic bad", __func__);
		return SLURM_ERROR;
	}

	switch (dinfo) {
	case SELECT_NODEDATA_SUBCNT:
		if (state == NODE_STATE_ALLOCATED)
			*uint16 = nodeinfo->alloc_cpus;
		else
			*uint16 = 0;
		break;
	case SELECT_NODEDATA_PTR:
		*select_nodeinfo = nodeinfo;
		break;
	case SELECT_NODEDATA_MEM_ALLOC:
		*uint64 = nodeinfo->alloc_memory;
		break;
	case SELECT_NODEDATA_TRES_ALLOC_FMT_STR:
		*tmp_char = xstrdup(nodeinfo->tres_alloc_fmt_str);
		break;
	case SELECT_NODEDATA_TRES_ALLOC_WEIGHTED:
		*tmp_double = nodeinfo->tres_alloc_weighted;
		break;
	default:
		error("%s: Unsupported option %d", __func__, dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_alloc(void)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_get(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_ERROR;
}

/* Unused for this plugin */
extern select_jobinfo_t *select_p_select_jobinfo_copy(select_jobinfo_t *jobinfo)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_select_jobinfo_unpack(select_jobinfo_t *jobinfo,
					  Buf buffer,
					  uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	}
	return NULL;
}

/* Unused for this plugin */
extern char *select_p_select_jobinfo_xstrdup(select_jobinfo_t *jobinfo,
					     int mode)
{
	return NULL;
}

extern int select_p_get_info_from_plugin(enum select_plugindata_info info,
					 job_record_t *job_ptr,
					 void *data)
{
	int rc = SLURM_SUCCESS;
	uint32_t *tmp_32 = (uint32_t *) data;
	List *tmp_list = (List *) data;

	switch (info) {
	case SELECT_CR_PLUGIN:
		*tmp_32 = is_cons_tres ?
			SELECT_TYPE_CONS_TRES : SELECT_TYPE_CONS_RES;
		break;
	case SELECT_CONFIG_INFO:
		*tmp_list = NULL;
		break;
	case SELECT_SINGLE_JOB_TEST:
		*tmp_32 = is_cons_tres ? 1 : 0;
		break;
	default:
		error("%s: info type %d invalid", __func__, info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_update_node_config(int index)
{
	if (index >= select_node_cnt) {
		error("%s: index too large (%d > %d)", __func__, index,
		      select_node_cnt);
		return SLURM_ERROR;
	}

	/*
	 * Socket and core count can be changed when KNL node reboots in a
	 * different NUMA configuration
	 */
	if (!(slurmctld_conf.conf_flags & CTL_CONF_OR) &&
	    (select_node_record[index].sockets !=
	     select_node_record[index].node_ptr->config_ptr->sockets) &&
	    (select_node_record[index].cores !=
	     select_node_record[index].node_ptr->config_ptr->cores) &&
	    ((select_node_record[index].sockets *
	      select_node_record[index].cores) ==
	     (select_node_record[index].node_ptr->sockets *
	      select_node_record[index].node_ptr->cores))) {
		select_node_record[index].cores =
			select_node_record[index].node_ptr->config_ptr->cores;
		select_node_record[index].sockets =
			select_node_record[index].node_ptr->config_ptr->sockets;
		/* tot_sockets should be the same */
		/* tot_cores should be the same */
	}

	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	ListIterator job_iterator;
	job_record_t *job_ptr;
	int rc = SLURM_SUCCESS;

	info("%s: reconfigure", plugin_type);
	select_debug_flags = slurm_get_debug_flags();

	if (is_cons_tres) {
		def_cpu_per_gpu = 0;
		def_mem_per_gpu = 0;
		if (slurmctld_conf.job_defaults_list) {
			def_cpu_per_gpu = common_get_def_cpu_per_gpu(
				slurmctld_conf.job_defaults_list);
			def_mem_per_gpu = common_get_def_mem_per_gpu(
				slurmctld_conf.job_defaults_list);
		}
	}

	rc = select_p_node_init(node_record_table_ptr, node_record_count);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* reload job data */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (IS_JOB_RUNNING(job_ptr)) {
			/* add the job */
			job_res_add_job(job_ptr, 0);
		} else if (IS_JOB_SUSPENDED(job_ptr)) {
			/* add the job in a suspended state */
			if (job_ptr->priority == 0)
				(void) job_res_add_job(job_ptr, 1);
			else	/* Gang schedule suspend */
				(void) job_res_add_job(job_ptr, 0);
		}
	}
	list_iterator_destroy(job_iterator);
	select_state_initializing = false;

	return SLURM_SUCCESS;
}

extern bitstr_t *select_p_resv_test(resv_desc_msg_t *resv_desc_ptr,
				    uint32_t node_cnt,
				    bitstr_t *avail_node_bitmap,
				    bitstr_t **core_bitmap)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	bitstr_t ***switches_core_bitmap;	/* cores on this switch */
	int       *switches_core_cnt;		/* total cores on switch */
	int       *switches_node_cnt;		/* total nodes on switch */
	int       *switches_required;		/* set if has required node */

	bitstr_t *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t *picked_node_bitmap;
	uint32_t *core_cnt;
	bitstr_t **exc_core_bitmap = NULL, **picked_core_bitmap;
	int32_t prev_rem_cores, rem_cores = 0, rem_cores_save, rem_nodes;
	uint32_t cores_per_node = 1;	/* Minimum cores per node to consider */
	bool aggr_core_cnt = false, clear_core, sufficient;
	int c, i, i_first, i_last, j, k, n;
	int best_fit_inx, best_fit_nodes;
	int best_fit_location = 0, best_fit_sufficient;

	xassert(avail_node_bitmap);
	xassert(resv_desc_ptr);

	/*
	 * FIXME: core_bitmap is a full-system core bitmap to be
	 * replaced with a set of per-node bitmaps in a future release
	 * of Slurm.
	 */
	if (core_bitmap)
		exc_core_bitmap = core_bitmap_to_array(*core_bitmap);

	core_cnt = resv_desc_ptr->core_cnt;

	if (core_cnt) {
		/*
		 * Run this now to set up exc_core_bitmap if needed for
		 * pick_first_cores and sequential_pick.
		 */
		if (!exc_core_bitmap)
			exc_core_bitmap = build_core_array();
		(*cons_common_callbacks.spec_core_filter)(
			avail_node_bitmap, exc_core_bitmap);
	}

	if ((resv_desc_ptr->flags & RESERVE_FLAG_FIRST_CORES) && core_cnt) {
		/* Reservation request with "Flags=first_cores CoreCnt=#" */
		avail_nodes_bitmap = (*cons_common_callbacks.pick_first_cores)(
			avail_node_bitmap,
			node_cnt, core_cnt,
			&exc_core_bitmap);
		if (avail_nodes_bitmap && core_bitmap && exc_core_bitmap) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = core_array_to_bitmap(exc_core_bitmap);
		}
		free_core_array(&exc_core_bitmap);
		return avail_nodes_bitmap;
	}

	/* When reservation includes a nodelist we use _sequential_pick code */
	if (!switch_record_cnt || !switch_record_table || !node_cnt)  {
		/* Reservation request with "Nodes=* [CoreCnt=#]" */
		avail_nodes_bitmap = (*cons_common_callbacks.sequential_pick)(
			avail_node_bitmap,
			node_cnt, core_cnt,
			&exc_core_bitmap);
		if (avail_nodes_bitmap && core_bitmap && exc_core_bitmap) {
			FREE_NULL_BITMAP(*core_bitmap);
			*core_bitmap = core_array_to_bitmap(exc_core_bitmap);
		}
		free_core_array(&exc_core_bitmap);
		return avail_nodes_bitmap;
	}

	/* Use topology state information */
	if (bit_set_count(avail_node_bitmap) < node_cnt) {
		free_core_array(&exc_core_bitmap);
		return NULL;
	}

	rem_nodes = node_cnt;
	if (core_cnt && core_cnt[1]) {	/* Array of core counts */
		for (j = 0; core_cnt[j]; j++) {
			rem_cores += core_cnt[j];
			if (j == 0)
				cores_per_node = core_cnt[j];
			else if (cores_per_node > core_cnt[j])
				cores_per_node = core_cnt[j];
		}
	} else if (core_cnt) {		/* Aggregate core count */
		rem_cores = core_cnt[0];
		cores_per_node = core_cnt[0] / MAX(node_cnt, 1);
		aggr_core_cnt = true;
	} else if (cr_node_num_cores)
		cores_per_node = cr_node_num_cores[0];
	else
		cores_per_node = 1;
	rem_cores_save = rem_cores;

	/*
	 * Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld
	 */
	switches_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switches_core_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t **));
	switches_core_cnt = xcalloc(switch_record_cnt, sizeof(int));
	switches_node_cnt = xcalloc(switch_record_cnt, sizeof(int));
	switches_required = xcalloc(switch_record_cnt, sizeof(int));

	for (i = 0; i < switch_record_cnt; i++) {
		switches_bitmap[i] =
			bit_copy(switch_record_table[i].node_bitmap);
		bit_and(switches_bitmap[i], avail_node_bitmap);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);
		switches_core_bitmap[i] = common_mark_avail_cores(
			switches_bitmap[i], NO_VAL16);
		if (exc_core_bitmap) {
			core_array_and_not(switches_core_bitmap[i],
					   exc_core_bitmap);
		}
		switches_core_cnt[i] =
			count_core_array_set(switches_core_bitmap[i]);
		debug2("switch:%d nodes:%d cores:%d",
		       i, switches_node_cnt[i], switches_core_cnt[i]);
	}

	/* Remove nodes with fewer available cores than needed */
	if (core_cnt) {
		n = 0;

		for (j = 0; j < switch_record_cnt; j++) {
			i_first = bit_ffs(switches_bitmap[j]);
			if (i_first >= 0)
				i_last = bit_fls(switches_bitmap[j]);
			else
				i_last = i_first - 1;
			for (i = i_first; i <= i_last; i++) {
				if (!bit_test(switches_bitmap[j], i))
					continue;

				c = _get_avail_cores_on_node(
					i, exc_core_bitmap);

				clear_core = false;
				if (aggr_core_cnt && (c < cores_per_node)) {
					clear_core = true;
				} else if (aggr_core_cnt) {
					;
				} else if (c < core_cnt[n]) {
					clear_core = true;
				} else if (core_cnt[n]) {
					n++;
				}
				if (!clear_core)
					continue;
				for (k = 0; k < switch_record_cnt; k++) {
					if (!switches_bitmap[k] ||
					    !bit_test(switches_bitmap[k], i))
						continue;
					bit_clear(switches_bitmap[k], i);
					switches_node_cnt[k]--;
					switches_core_cnt[k] -= c;
				}
			}
		}
	}

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i = 0; i < switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		info("switch=%s nodes=%u:%s cores:%d required:%u speed=%u",
		     switch_record_table[i].name,
		     switches_node_cnt[i], node_names,
		     switches_core_cnt[i], switches_required[i],
		     switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	/* Determine lowest level switch satisfying request with best fit */
	best_fit_inx = -1;
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switches_node_cnt[j] < rem_nodes) ||
		    (core_cnt && (switches_core_cnt[j] < rem_cores)))
			continue;
		if ((best_fit_inx == -1) ||
		    (switch_record_table[j].level <
		     switch_record_table[best_fit_inx].level) ||
		    ((switch_record_table[j].level ==
		      switch_record_table[best_fit_inx].level) &&
		     (switches_node_cnt[j] < switches_node_cnt[best_fit_inx])))
			/* We should use core count by switch here as well */
			best_fit_inx = j;
	}
	if (best_fit_inx == -1) {
		debug("%s: could not find resources for reservation", __func__);
		goto fini;
	}

	/* Identify usable leafs (within higher switch having best fit) */
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from these leafs on a best-fit basis */
	avail_nodes_bitmap = bit_alloc(node_record_count);
	while (rem_nodes > 0) {
		best_fit_nodes = best_fit_sufficient = 0;
		for (j = 0; j < switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			if (core_cnt) {
				sufficient =
					(switches_node_cnt[j] >= rem_nodes) &&
					(switches_core_cnt[j] >= rem_cores);
			} else
				sufficient = switches_node_cnt[j] >= rem_nodes;
			/*
			 * If first possibility OR
			 * first set large enough for request OR
			 * tightest fit (less resource waste) OR
			 * nothing yet large enough, but this is biggest
			 */
			if ((best_fit_nodes == 0) ||
			    (sufficient && (best_fit_sufficient == 0)) ||
			    (sufficient &&
			     (switches_node_cnt[j] < best_fit_nodes)) ||
			    ((sufficient == 0) &&
			     (switches_node_cnt[j] > best_fit_nodes))) {
				best_fit_nodes = switches_node_cnt[j];
				best_fit_location = j;
				best_fit_sufficient = sufficient;
			}
		}
		if (best_fit_nodes == 0)
			break;
		/* Use select nodes from this leaf */
		i_first = bit_ffs(switches_bitmap[best_fit_location]);
		if (i_first >= 0)
			i_last = bit_fls(switches_bitmap[best_fit_location]);
		else
			i_last = i_first - 1;

		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(switches_bitmap[best_fit_location], i))
				continue;
			bit_clear(switches_bitmap[best_fit_location], i);
			switches_node_cnt[best_fit_location]--;

			if (bit_test(avail_nodes_bitmap, i)) {
				/*
				 * node on multiple leaf switches
				 * and already selected
				 */
				continue;
			}

			c = _get_avail_cores_on_node(i, exc_core_bitmap);

			if (c < cores_per_node)
				continue;
			debug2("Using node %d with %d cores available", i, c);
			bit_set(avail_nodes_bitmap, i);
			rem_cores -= c;
			if (--rem_nodes <= 0)
				break;
		}
		switches_node_cnt[best_fit_location] = 0;
	}

	if ((rem_nodes > 0) || (rem_cores > 0))	/* insufficient resources */
		FREE_NULL_BITMAP(avail_nodes_bitmap);

fini:	for (i = 0; i < switch_record_cnt; i++) {
		FREE_NULL_BITMAP(switches_bitmap[i]);
		free_core_array(&switches_core_bitmap[i]);
	}
	xfree(switches_bitmap);
	xfree(switches_core_bitmap);
	xfree(switches_core_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	if (avail_nodes_bitmap && core_cnt) {
		/* Reservation is using partial nodes */
		picked_node_bitmap = bit_alloc(bit_size(avail_node_bitmap));
		picked_core_bitmap = build_core_array();

		rem_cores = rem_cores_save;
		n = 0;
		prev_rem_cores = -1;

		while (rem_cores) {
			int avail_cores_in_node, inx, coff;
			bitstr_t *use_exc_bitmap = NULL,
				*use_picked_bitmap = NULL;

			inx = bit_ffs(avail_nodes_bitmap);
			if ((inx < 0) && aggr_core_cnt && (rem_cores > 0) &&
			    (rem_cores != prev_rem_cores)) {
				/*
				 * Make another pass over nodes to reach
				 * requested aggregate core count
				 */
				bit_or(avail_nodes_bitmap, picked_node_bitmap);
				inx = bit_ffs(avail_nodes_bitmap);
				prev_rem_cores = rem_cores;
				cores_per_node = 1;
			}
			if (inx < 0)
				break;

			debug2("Using node inx:%d cores_per_node:%d rem_cores:%u",
			       inx, cores_per_node, rem_cores);

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_nodes_bitmap, inx);

			if (select_node_record[inx].tot_cores < cores_per_node)
				continue;
			avail_cores_in_node =
				_get_avail_cores_on_node(inx, exc_core_bitmap);

			debug2("Node inx:%d has %d available cores", inx,
			       avail_cores_in_node);
			if (avail_cores_in_node < cores_per_node)
				continue;

			xassert(exc_core_bitmap);

			avail_cores_in_node = 0;

			if (!is_cons_tres) {
				use_exc_bitmap = *exc_core_bitmap;
				coff = cr_get_coremap_offset(inx);
				if (!*picked_core_bitmap)
					*picked_core_bitmap = bit_alloc(
						bit_size(use_exc_bitmap));
				use_picked_bitmap = *picked_core_bitmap;
			} else {
				use_exc_bitmap = exc_core_bitmap[inx];
				coff = 0;
				if (!picked_core_bitmap[inx]) {
					picked_core_bitmap[inx] = bit_alloc(
						select_node_record[inx].
						tot_cores);
				}
				use_picked_bitmap = picked_core_bitmap[inx];
			}

			for (int i = 0;
			     i < select_node_record[inx].tot_cores;
			     i++) {
				int set = coff + i;
				if ((!use_exc_bitmap ||
				     !bit_test(use_exc_bitmap, set)) &&
				    !bit_test(use_picked_bitmap, set)) {
					bit_set(use_picked_bitmap, set);
					rem_cores--;
					avail_cores_in_node++;
				}
				if (rem_cores == 0)
					break;
				if (aggr_core_cnt &&
				    (avail_cores_in_node >= cores_per_node))
					break;
				if (!aggr_core_cnt &&
				    (avail_cores_in_node >= core_cnt[n]))
					break;
			}

			/* Add this node to the final node bitmap */
			if (avail_cores_in_node)
				bit_set(picked_node_bitmap, inx);
			n++;
		}
		FREE_NULL_BITMAP(avail_nodes_bitmap);
		free_core_array(&exc_core_bitmap);

		if (rem_cores) {
			info("reservation request can not be satisfied");
			FREE_NULL_BITMAP(picked_node_bitmap);
			picked_node_bitmap = NULL;
		} else {
			*core_bitmap = core_array_to_bitmap(picked_core_bitmap);
		}
		free_core_array(&picked_core_bitmap);
		return picked_node_bitmap;
	}
	free_core_array(&exc_core_bitmap);

	return avail_nodes_bitmap;
}
