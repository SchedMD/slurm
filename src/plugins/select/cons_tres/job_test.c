/*****************************************************************************\
 *  job_test.c - Determine if job can be allocated resources.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Derived in large part from select/cons_res plugin
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

#include <string.h>
#include "select_cons_tres.h"
#include "dist_tasks.h"
#include "job_test.h"
#include "gres_select_filter.h"
#include "gres_select_util.h"
#include "gres_sock_list.h"

#include "src/slurmctld/licenses.h"
#include "src/common/slurm_time.h"

typedef struct {
	int action;
	list_t *license_list;
	bitstr_t *node_map;
	node_use_record_t *node_usage;
	part_res_record_t *part_record_ptr;
	int rc;
} wrapper_rm_job_args_t;

typedef struct {
	list_t *preemptee_candidates;
	list_t *cr_job_list;
	node_use_record_t *future_usage;
	part_res_record_t *future_part;
	list_t *future_license_list;
	list_t *job_license_list;
	bitstr_t *orig_map;
	bool *qos_preemptor;
	time_t start;
	bitstr_t **tmp_bitmap_pptr;
} cr_job_list_args_t;

typedef struct {
	uint32_t min_nodes;
	uint32_t num_tasks;
	uint32_t *sum_cpus;
} gres_cpus_foreach_args_t;

uint64_t def_cpu_per_gpu = 0;
uint64_t def_mem_per_gpu = 0;
bool preempt_strict_order = false;
bool preempt_for_licenses = false;
int preempt_reorder_cnt	= 1;

/* Local functions */
static avail_res_t *_allocate(job_record_t *job_ptr,
			      bitstr_t *core_map,
			      bitstr_t *part_core_map,
			      const uint32_t node_i,
			      int *cpu_alloc_size,
			      bitstr_t *req_sock_map,
			      uint16_t cr_type);

/* Log avail_res_t information for a given node */
static void _avail_res_log(avail_res_t *avail_res, char *node_name)
{
	int i;
	char *gres_info = "";

	if (!(slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE))
	    return;

	if (!avail_res) {
		log_flag(SELECT_TYPE, "Node:%s No resources", node_name);
		return;
	}

	log_flag(SELECT_TYPE, "Node:%s Sockets:%u SpecThreads:%u CPUs:Min-Max,Avail:%u-%u,%u ThreadsPerCore:%u",
		 node_name, avail_res->sock_cnt, avail_res->spec_threads,
		 avail_res->min_cpus, avail_res->max_cpus,
		 avail_res->avail_cpus, avail_res->tpc);
	gres_info = gres_sock_str(avail_res->sock_gres_list, -1);
	if (gres_info) {
		log_flag(SELECT_TYPE, "  AnySocket %s", gres_info);
		xfree(gres_info);
	}
	for (i = 0; i < avail_res->sock_cnt; i++) {
		gres_info = gres_sock_str(avail_res->sock_gres_list, i);
		if (gres_info) {
			log_flag(SELECT_TYPE, "  Socket[%d] Cores:%u GRES:%s", i,
			     avail_res->avail_cores_per_sock[i], gres_info);
			xfree(gres_info);
		} else {
			log_flag(SELECT_TYPE, "  Socket[%d] Cores:%u", i,
			     avail_res->avail_cores_per_sock[i]);
		}
	}
}

/* When any cores on a node are removed from being available for a job,
 * then remove the entire node from being available. */
static void _block_whole_nodes(bitstr_t *node_bitmap,
			       bitstr_t **orig_core_bitmap,
			       bitstr_t **new_core_bitmap)
{
	int i_node;
	int first_core, last_core, i_core;
	node_record_t *node_ptr;
	bitstr_t *cr_orig_core_bitmap = NULL;
	bitstr_t *cr_new_core_bitmap = NULL;

	for (i_node = 0; (node_ptr = next_node_bitmap(node_bitmap, &i_node));
	     i_node++) {
		first_core = 0;
		last_core = node_ptr->tot_cores;
		cr_orig_core_bitmap = orig_core_bitmap[i_node];
		cr_new_core_bitmap = new_core_bitmap[i_node];

		for (i_core = first_core; i_core < last_core; i_core++) {
			if (bit_test(cr_orig_core_bitmap, i_core) &&
			    !bit_test(cr_new_core_bitmap, i_core)) {
				bit_clear(node_bitmap, i_node);
				break;
			}
		}
	}
}

static void _block_by_topology(job_record_t *job_ptr,
			       part_res_record_t *p_ptr,
			       bitstr_t *node_bitmap)
{
	bitstr_t *tmp_bitmap = NULL;
	static int enable_exclusive_topo = -1;

	if (enable_exclusive_topo == -1) {
		enable_exclusive_topo = 0;
		(void) topology_g_get(TOPO_DATA_EXCLUSIVE_TOPO,
				      &enable_exclusive_topo);
	}

	if (!enable_exclusive_topo)
		return;

	for (; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (int i = 0; i < p_ptr->num_rows; i++) {
			for (int j = 0; j < p_ptr->row[i].num_jobs; j++) {
				struct job_resources *job;
				job = p_ptr->row[i].job_list[j];

				if (!job->node_bitmap)
					continue;
				if (IS_JOB_WHOLE_TOPO(job_ptr) ||
				    (job->whole_node & WHOLE_TOPO) ||
				    (p_ptr->part_ptr->flags &
				     PART_FLAG_EXCLUSIVE_TOPO)) {
					if (tmp_bitmap)
						bit_or(tmp_bitmap,
						       job->node_bitmap);
					else
						tmp_bitmap = bit_copy(
							job->node_bitmap);
				}

			}
		}
	}

	if (tmp_bitmap) {
		topology_g_whole_topo(tmp_bitmap);
		bit_and_not(node_bitmap, tmp_bitmap);
		FREE_NULL_BITMAP(tmp_bitmap);
	}

	return;
}

static uint16_t _valid_uint16(uint16_t arg)
{
	if ((arg == NO_VAL16) || (arg == INFINITE16))
		return 0;
	return arg;
}

static gres_mc_data_t *_build_gres_mc_data(job_record_t *job_ptr)
{
	gres_mc_data_t *tres_mc_ptr;

	tres_mc_ptr = xmalloc(sizeof(gres_mc_data_t));
	tres_mc_ptr->cpus_per_task =
		_valid_uint16(job_ptr->details->cpus_per_task);
	/*
	 * _copy_job_desc_to_job_record() sets job_ptr->details->cpus_per_task
	 * to 1 if unset or NO_VAL16; INFINITE16 is invalid. Therefore,
	 * tres_mc_ptr->cpus_per_task should always be non-zero.
	 */
	xassert(tres_mc_ptr->cpus_per_task);

	tres_mc_ptr->ntasks_per_job = job_ptr->details->num_tasks;
	tres_mc_ptr->ntasks_per_node =
		_valid_uint16(job_ptr->details->ntasks_per_node);
	tres_mc_ptr->overcommit = job_ptr->details->overcommit;
	tres_mc_ptr->task_dist = job_ptr->details->task_dist;
	tres_mc_ptr->whole_node = job_ptr->details->whole_node;
	if (job_ptr->details->mc_ptr) {
		multi_core_data_t *job_mc_ptr = job_ptr->details->mc_ptr;
		tres_mc_ptr->boards_per_node =
			_valid_uint16(job_mc_ptr->boards_per_node);
		tres_mc_ptr->sockets_per_board =
			_valid_uint16(job_mc_ptr->sockets_per_board);
		tres_mc_ptr->sockets_per_node =
			_valid_uint16(job_mc_ptr->sockets_per_node);
		tres_mc_ptr->cores_per_socket =
			_valid_uint16(job_mc_ptr->cores_per_socket);
		tres_mc_ptr->threads_per_core =
			_valid_uint16(job_mc_ptr->threads_per_core);
		tres_mc_ptr->ntasks_per_board =
			_valid_uint16(job_mc_ptr->ntasks_per_board);
		tres_mc_ptr->ntasks_per_socket =
			_valid_uint16(job_mc_ptr->ntasks_per_socket);
		tres_mc_ptr->ntasks_per_core =
			_valid_uint16(job_mc_ptr->ntasks_per_core);
	}
	if ((tres_mc_ptr->ntasks_per_core == 0) &&
	    (slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE))
		tres_mc_ptr->ntasks_per_core = 1;

	return tres_mc_ptr;
}

static struct multi_core_data *_create_default_mc(void)
{
	struct multi_core_data *mc_ptr;

	mc_ptr = xmalloc(sizeof(struct multi_core_data));
	mc_ptr->sockets_per_node = NO_VAL16;
	mc_ptr->cores_per_socket = NO_VAL16;
	mc_ptr->threads_per_core = NO_VAL16;
	/* Other fields initialized to zero by xmalloc */

	return mc_ptr;
}

/* list sort function: sort by the job's expected end time */
static int _cr_job_list_sort(void *x, void *y)
{
	job_record_t *job1_ptr = *(job_record_t **) x;
	job_record_t *job2_ptr = *(job_record_t **) y;

	return slurm_sort_time_list_asc(&job1_ptr->end_time,
					&job2_ptr->end_time);
}

static int _find_job (void *x, void *key)
{
	job_record_t *job_ptr = (job_record_t *) x;
	if (job_ptr == (job_record_t *) key)
		return 1;
	return 0;
}

static int _is_job_sharing(void *x, void *key)
{
	job_record_t *job_ptr = x;
	if ((job_ptr->details->share_res == 1) ||
	    (job_ptr->part_ptr->max_share & SHARED_FORCE)) {
		debug3("%pJ is sharing resources.", job_ptr);
		return 1;
	}
	return 0;
}

static void _free_avail_res(avail_res_t *avail_res)
{
	if (!avail_res)
		return;

	xfree(avail_res->avail_cores_per_sock);
	FREE_NULL_LIST(avail_res->sock_gres_list);
	xfree(avail_res);
}

static void _free_avail_res_array(avail_res_t **avail_res_array)
{
	int n;
	if (!avail_res_array)
		return;

	for (n = 0; next_node(&n); n++)
		_free_avail_res(avail_res_array[n]);
	xfree(avail_res_array);
}

/* Determine the node requirements for the job:
 * - does the job need exclusive nodes? (NODE_CR_RESERVED)
 * - can the job run on shared nodes?   (NODE_CR_ONE_ROW)
 * - can the job run on overcommitted resources? (NODE_CR_AVAILABLE)
 */
static uint16_t _get_job_node_req(job_record_t *job_ptr)
{
	int max_share = job_ptr->part_ptr->max_share;

	if (max_share == 0)	/* Partition OverSubscribe=EXCLUSIVE */
		return NODE_CR_RESERVED;

	/* Partition is OverSubscribe=FORCE */
	if (max_share & SHARED_FORCE)
		return NODE_CR_AVAILABLE;

	if ((max_share > 1) && (job_ptr->details->share_res == 1))
		/* part allows sharing, and the user has requested it */
		return NODE_CR_AVAILABLE;

	return NODE_CR_ONE_ROW;
}

static void _set_gpu_defaults(job_record_t *job_ptr)
{
	static part_record_t *last_part_ptr = NULL;
	static uint64_t last_cpu_per_gpu = NO_VAL64;
	static uint64_t last_mem_per_gpu = NO_VAL64;
	uint64_t cpu_per_gpu, mem_per_gpu;

	if (!job_ptr->gres_list_req)
		return;

	if (job_ptr->part_ptr != last_part_ptr) {
		/* Cache data from last partition referenced */
		last_part_ptr = job_ptr->part_ptr;
		last_cpu_per_gpu = cons_helpers_get_def_cpu_per_gpu(
			last_part_ptr->job_defaults_list);
		last_mem_per_gpu = cons_helpers_get_def_mem_per_gpu(
			last_part_ptr->job_defaults_list);
	}
	if ((last_cpu_per_gpu != NO_VAL64) &&
	    (job_ptr->details->orig_cpus_per_task == NO_VAL16))
		cpu_per_gpu = last_cpu_per_gpu;
	else if ((def_cpu_per_gpu != NO_VAL64) &&
		 (job_ptr->details->orig_cpus_per_task == NO_VAL16))
		cpu_per_gpu = def_cpu_per_gpu;
	else
		cpu_per_gpu = 0;
	if (last_mem_per_gpu != NO_VAL64)
		mem_per_gpu = last_mem_per_gpu;
	else if (def_mem_per_gpu != NO_VAL64)
		mem_per_gpu = def_mem_per_gpu;
	else
		mem_per_gpu = 0;

	gres_select_util_job_set_defs(job_ptr->gres_list_req, "gpu",
				      cpu_per_gpu, mem_per_gpu,
				      &job_ptr->cpus_per_tres,
				      &job_ptr->mem_per_tres,
				      &job_ptr->details->cpus_per_task);
}

/* Calculated the minimum number of gres cpus based on cpus_per_gres */
static int _sum_min_gres_cpus(void *gres_job_state, void *args)
{
	gres_job_state_t *gres_js = ((gres_state_t*)gres_job_state)->gres_data;
	gres_cpus_foreach_args_t *gres_cpus_args = args;
	uint32_t cpus = gres_js->cpus_per_gres;

	if (!cpus)
		return SLURM_SUCCESS;

	if (gres_js->gres_per_node)
		cpus *= gres_js->gres_per_node;
	else if (gres_js->gres_per_task)
		cpus *= gres_js->gres_per_task * gres_cpus_args->num_tasks;
	else if (gres_js->gres_per_socket)
		cpus *= gres_js->gres_per_socket;
	else if (gres_js->gres_per_job)
		cpus *= gres_js->gres_per_job / gres_cpus_args->min_nodes;

	*gres_cpus_args->sum_cpus += cpus;

	return SLURM_SUCCESS;
}

/* Determine how many sockets per node this job requires for GRES */
static uint32_t _socks_per_node(job_record_t *job_ptr)
{
	multi_core_data_t *mc_ptr;
	uint32_t s_p_n = NO_VAL;
	uint32_t cpu_cnt, cpus_per_node, tasks_per_node;
	uint32_t min_nodes;
	uint32_t sum_cpus = 0;

	if (!job_ptr->details)
		return s_p_n;

	cpu_cnt = job_ptr->details->num_tasks * job_ptr->details->cpus_per_task;
	cpu_cnt = MAX(job_ptr->details->min_cpus, cpu_cnt);
	min_nodes = MAX(job_ptr->details->min_nodes, 1);
	cpus_per_node = cpu_cnt / min_nodes;

	/*
	 * Here we need to sum up the cpus per gres so we can tell if we need
	 * more sockets than 1 when enforcing binding.
	 */
	if (job_ptr->gres_list_req) {
		gres_cpus_foreach_args_t gres_args = {
			.min_nodes = min_nodes,
			.num_tasks = job_ptr->details->num_tasks,
			.sum_cpus = &sum_cpus,
		};

		(void) list_for_each(job_ptr->gres_list_req,
				     _sum_min_gres_cpus, &gres_args);
	}

	if ((cpus_per_node <= 1) && (sum_cpus <= 1))
		return (uint32_t) 1;

	mc_ptr = job_ptr->details->mc_ptr;
	if ((mc_ptr->ntasks_per_socket != NO_VAL16) &&
	    (mc_ptr->ntasks_per_socket != INFINITE16)) {
		tasks_per_node = job_ptr->details->num_tasks / min_nodes;
		s_p_n = (tasks_per_node + mc_ptr->ntasks_per_socket - 1) /
			mc_ptr->ntasks_per_socket;
		return s_p_n;
	}

	/*
	 * This logic could be expanded to support additional cases, which may
	 * require information per node information (e.g. threads per core).
	 */

	return s_p_n;
}

/*
 * _can_job_run_on_node - Given the job requirements, determine which
 *                       resources from the given node (if any) can be
 *                       allocated to this job. Returns a structure identifying
 *                       the resources available for allocation to this job.
 *       NOTE: This process does NOT support overcommitting resources
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - per-node bitmap of available cores
 * IN node_i        - index of node to be evaluated
 * IN s_p_n         - Expected sockets_per_node (NO_VAL if not limited)
 * IN cr_type       - Consumable Resource setting
 * IN test_only     - Determine if job could ever run, ignore allocated memory
 *		      check
 * IN will_run      - Determining when a pending job can start
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * resv_exc_ptr IN - gres that can be included (gres_list_inc)
 *                   or excluded (gres_list_exc)
 * RET Available resources. Call _array() to release memory.
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to de-select from the core_map to match the cpu_count.
 */
static avail_res_t *_can_job_run_on_node(job_record_t *job_ptr,
					 bitstr_t **core_map,
					 const uint32_t node_i,
					 uint32_t s_p_n,
					 node_use_record_t *node_usage,
					 uint16_t cr_type,
					 bool test_only, bool will_run,
					 bitstr_t **part_core_map,
					 resv_exc_t *resv_exc_ptr)
{
	uint16_t cpus = 0;
	uint64_t avail_mem = NO_VAL64, req_mem;
	int cpu_alloc_size, i, rc;
	node_record_t *node_ptr = node_record_table_ptr[node_i];
	list_t *node_gres_list;
	bitstr_t *part_core_map_ptr = NULL, *req_sock_map = NULL;
	avail_res_t *avail_res = NULL;
	list_t *sock_gres_list = NULL;
	bool enforce_binding = false;
	uint16_t min_cpus_per_node, ntasks_per_node = 1;

	if (((job_ptr->bit_flags & BACKFILL_TEST) == 0) &&
	    !test_only && !will_run && IS_NODE_COMPLETING(node_ptr)) {
		/*
		 * Do not allocate more jobs to nodes with completing jobs,
		 * backfill scheduler independently handles completing nodes
		 */
		return NULL;
	}

	if (part_core_map)
		part_core_map_ptr = part_core_map[node_i];
	if (node_usage[node_i].gres_list)
		node_gres_list = node_usage[node_i].gres_list;
	else
		node_gres_list = node_ptr->gres_list;

	if (job_ptr->gres_list_req) {
		/* Identify available GRES and adjacent cores */

		if (job_ptr->bit_flags & GRES_ENFORCE_BIND)
			enforce_binding = true;
		if (!core_map[node_i]) {
			core_map[node_i] = bit_alloc(node_ptr->tot_cores);
			bit_set_all(core_map[node_i]);
		}
		sock_gres_list = gres_sock_list_create(
					job_ptr->gres_list_req, node_gres_list,
					resv_exc_ptr,
					test_only, core_map[node_i],
					node_ptr->tot_sockets, node_ptr->cores,
					job_ptr->job_id, node_ptr->name,
					enforce_binding, s_p_n, &req_sock_map,
					job_ptr->user_id, node_i,
					node_ptr->gpu_spec_bitmap,
					node_ptr->res_cores_per_gpu,
					cr_type);
		if (!sock_gres_list) {	/* GRES requirement fail */
			log_flag(SELECT_TYPE, "Test fail on node %s: gres_sock_list_create",
			     node_ptr->name);
			return NULL;
		}
	}

	/* Identify available CPUs */
	avail_res = _allocate(job_ptr, core_map[node_i],
			      part_core_map_ptr, node_i,
			      &cpu_alloc_size, req_sock_map, cr_type);

	FREE_NULL_BITMAP(req_sock_map);
	if (!avail_res || (avail_res->avail_cpus == 0)) {
		_free_avail_res(avail_res);
		log_flag(SELECT_TYPE, "Test fail on node %d: _allocate_cores/sockets",
			 node_i);
		FREE_NULL_LIST(sock_gres_list);
		return NULL;
	}

	/* Check that sufficient CPUs remain to run a task on this node */
	if (job_ptr->details->ntasks_per_node) {
		ntasks_per_node = job_ptr->details->ntasks_per_node;
	} else if (job_ptr->details->overcommit) {
		ntasks_per_node = 1;
	} else if ((job_ptr->details->max_nodes == 1) &&
		   (job_ptr->details->num_tasks != 0)) {
		ntasks_per_node = job_ptr->details->num_tasks;
	}
	min_cpus_per_node = ntasks_per_node * job_ptr->details->cpus_per_task;
	if (avail_res->avail_cpus < min_cpus_per_node) {
		log_flag(SELECT_TYPE, "Test fail on node %d: avail_cpus < min_cpus_per_node (%u < %u)",
			 node_i, avail_res->avail_cpus, min_cpus_per_node);
		FREE_NULL_LIST(sock_gres_list);
		_free_avail_res(avail_res);
		return NULL;
	}

	if (cr_type & CR_MEMORY) {
		avail_mem = node_ptr->real_memory - node_ptr->mem_spec_limit;
		if (!test_only)
			avail_mem -= node_usage[node_i].alloc_memory;
	}

	if (sock_gres_list) {
		uint16_t near_gpu_cnt = 0;
		avail_res->sock_gres_list = sock_gres_list;
		/* Disable GRES that can't be used with remaining cores */
		rc = gres_select_filter_remove_unusable(
			sock_gres_list, avail_mem,
			avail_res->avail_cpus,
			enforce_binding, core_map[node_i],
			node_ptr->tot_sockets, node_ptr->cores, node_ptr->tpc,
			s_p_n,
			job_ptr->details->ntasks_per_node,
			job_ptr->details->cpus_per_task,
			(job_ptr->details->whole_node & WHOLE_NODE_REQUIRED),
			&avail_res->avail_gpus, &near_gpu_cnt);
		if (rc != 0) {
			log_flag(SELECT_TYPE, "Test fail on node %d: gres_select_filter_remove_unusable",
			     node_i);
			_free_avail_res(avail_res);
			return NULL;
		}

		/* Favor nodes with more co-located GPUs */
		node_ptr->sched_weight =
			(node_ptr->sched_weight & 0xffffffffffffff00) |
			(0xff - near_gpu_cnt);
	}

	cpus = avail_res->avail_cpus;

	if (cr_type & CR_MEMORY) {
		/*
		 * Memory Check: check pn_min_memory to see if:
		 *          - this node has enough memory (MEM_PER_CPU == 0)
		 *          - there are enough free_cores (MEM_PER_CPU == 1)
		 */
		req_mem   = job_ptr->details->pn_min_memory & ~MEM_PER_CPU;
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
			/* memory is per-CPU */
			if (!(job_ptr->bit_flags & BF_WHOLE_NODE_TEST) &&
			    ((req_mem * cpus) > avail_mem) &&
			    (job_ptr->details->whole_node &
			     WHOLE_NODE_REQUIRED)) {
				cpus = 0;
			} else if (!(cr_type & CR_CPU) &&
				   job_ptr->details->mc_ptr &&
				   (job_ptr->details->mc_ptr->
				    ntasks_per_core == 1) &&
				   job_ptr->details->cpus_per_task == 1) {
				/*
				 * In this scenario, CPUs represents cores and
				 * the CPU/core count will be inflated later on
				 * to include all of the threads on a core. So
				 * we need to compare apples to apples and only
				 * remove 1 CPU/core at a time.
				 */
				while ((cpus > 0) &&
				       ((req_mem *
					 ((uint64_t)cpus *
					  (uint64_t)node_ptr->tpc))
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

	if (cpus == 0) {
		log_flag(SELECT_TYPE, "Test fail on node %d: cpus == 0",
			 node_i);
		bit_clear_all(core_map[node_i]);
	}

	log_flag(SELECT_TYPE, "%u CPUs on %s(state:%d), mem %"PRIu64"/%"PRIu64,
	         cpus, node_ptr->name,
	         node_usage[node_i].node_state, node_usage[node_i].alloc_memory,
	         node_ptr->real_memory);

	avail_res->avail_cpus = cpus;
	avail_res->avail_res_cnt = cpus + avail_res->avail_gpus;
	_avail_res_log(avail_res, node_ptr->name);

	return avail_res;
}

/*
 * Determine resource availability for pending job
 *
 * IN: job_ptr       - pointer to the job requesting resources
 * IN: node_map      - bitmap of available nodes
 * IN/OUT: core_map  - per-node bitmaps of available cores
 * IN: cr_type       - resource type
 * IN: test_only     - Determine if job could ever run, ignore allocated memory
 *		       check
 * IN: will_run      - Determining when a pending job can start
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * resv_exc_ptr IN   - gres that can be included (gres_list_inc)
 *                     or excluded (gres_list_exc)
 *
 * RET array of avail_res_t pointers, free using _free_avail_res_array()
 */
static avail_res_t **_get_res_avail(job_record_t *job_ptr,
				    bitstr_t *node_map, bitstr_t **core_map,
				    node_use_record_t *node_usage,
				    uint16_t cr_type, bool test_only,
				    bool will_run, bitstr_t **part_core_map,
				    resv_exc_t *resv_exc_ptr)
{
	int i, i_first, i_last;
	avail_res_t **avail_res_array = NULL;
	uint32_t s_p_n = _socks_per_node(job_ptr);

	avail_res_array = xcalloc(node_record_count, sizeof(avail_res_t *));
	i_first = bit_ffs(node_map);
	if (i_first != -1)
		i_last = bit_fls(node_map);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (bit_test(node_map, i))
			avail_res_array[i] =
				_can_job_run_on_node(
					job_ptr, core_map, i,
					s_p_n, node_usage,
					cr_type, test_only, will_run,
					part_core_map, resv_exc_ptr);
	}

	return avail_res_array;
}

/* For a given job already past it's end time, guess when it will actually end.
 * Used for backfill scheduling. */
static time_t _guess_job_end(job_record_t *job_ptr, time_t now)
{
	time_t end_time;
	uint16_t over_time_limit;

	if (job_ptr->part_ptr &&
	    (job_ptr->part_ptr->over_time_limit != NO_VAL16)) {
		over_time_limit = job_ptr->part_ptr->over_time_limit;
	} else {
		over_time_limit = slurm_conf.over_time_limit;
	}
	if (over_time_limit == 0) {
		end_time = job_ptr->end_time + slurm_conf.kill_wait;
	} else if (over_time_limit == INFINITE16) {
		/* No idea when the job might end, this is just a guess */
		if (job_ptr->time_limit && (job_ptr->time_limit != NO_VAL) &&
		    (job_ptr->time_limit != INFINITE)) {
			end_time = now + (job_ptr->time_limit * 60);
		} else {
			end_time = now + (365 * 24 * 60 * 60);	/* one year */
		}
	} else {
		end_time = job_ptr->end_time + slurm_conf.kill_wait +
			(over_time_limit  * 60);
	}
	if (end_time <= now)
		end_time = now + 1;

	return end_time;
}

/*
 * Test to see if a node already has running jobs for _other_ partitions.
 * If (sharing_only) then only check sharing partitions. This is because
 * the job was submitted to a single-row partition which does not share
 * allocated CPUs with multi-row partitions.
 */
static int _is_node_busy(part_res_record_t *p_ptr, uint32_t node_i,
			 bool sharing_only, part_record_t *my_part_ptr,
			 bool use_extra_row, list_t *jobs)
{
	uint32_t r;
	uint16_t num_rows;

	for (; p_ptr; p_ptr = p_ptr->next) {
		num_rows = p_ptr->num_rows;
		if (preempt_by_qos && !use_extra_row)
			num_rows--;	/* Don't use extra row */
		if (sharing_only &&
		    ((num_rows < 2) || (p_ptr->part_ptr == my_part_ptr)))
			continue;
		if (!p_ptr->row)
			continue;

		xassert(!p_ptr->rebuild_rows);
		for (r = 0; r < num_rows; r++) {
			if (!p_ptr->row[r].row_bitmap)
				continue;

			if (!p_ptr->row[r].row_bitmap[node_i])
				continue;

			if (jobs &&
			    list_find_first(jobs, _is_job_sharing, NULL))
				return 1;
		}
	}
	return 0;
}

static bool _is_preemptable(job_record_t *job_ptr, list_t *preemptee_candidates)
{
	if (!preemptee_candidates)
		return false;
	if (list_find_first(preemptee_candidates, _find_job, job_ptr))
		return true;
	return false;
}

/*
 * Select the best set of resources for the given job
 * IN: job_ptr      - pointer to the job requesting resources
 * IN: min_nodes    - minimum number of nodes required
 * IN: max_nodes    - maximum number of nodes requested
 * IN: req_nodes    - number of requested nodes
 * IN/OUT: node_bitmap - bitmap of available nodes / bitmap of selected nodes
 * IN/OUT: avail_core - available/selected cores
 * IN: cr_type      - resource type
 * IN: test_only    - Determine if job could ever run, ignore allocated memory
 *		      check
 * IN: will_run     - Determining when a pending job can start
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * IN: prefer_alloc_nodes - select currently allocated nodes first
 * IN: tres_mc_ptr   - job's multi-core options
 * IN: resv_exc_ptr - gres that can be included (gres_list_inc)
 *                    or excluded (gres_list_exc)
 * IN: select_rc - rc from this function.
 * RET: array of avail_res_t pointers, free using _free_avail_res_array().
 *	NULL on error and select_rc set.
 */
static avail_res_t **_select_nodes(job_record_t *job_ptr, uint32_t min_nodes,
				   uint32_t max_nodes, uint32_t req_nodes,
				   bitstr_t *node_bitmap, bitstr_t **avail_core,
				   node_use_record_t *node_usage,
				   uint16_t cr_type, bool test_only,
				   bool will_run,
				   bitstr_t **part_core_map,
				   bool prefer_alloc_nodes,
				   gres_mc_data_t *tres_mc_ptr,
				   resv_exc_t *resv_exc_ptr,
				   int *select_rc)
{
	int i, rc;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	topology_eval_t topo_eval = {
		.avail_core = avail_core,
		.avail_cpus = 0,
		.avail_res_array = NULL,
		.cr_type = cr_type,
		.enforce_binding = (job_ptr->gres_list_req &&
				    (job_ptr->bit_flags & GRES_ENFORCE_BIND)) ?
		true : false,
		.first_pass = true,
		.job_ptr = job_ptr,
		.max_nodes = max_nodes,
		.mc_ptr = tres_mc_ptr,
		.min_nodes = min_nodes,
		.node_map = node_bitmap,
		.prefer_alloc_nodes = prefer_alloc_nodes,
		.req_nodes = req_nodes,
	};

	if (bit_set_count(topo_eval.node_map) < topo_eval.min_nodes) {
#if _DEBUG
		info("AvailNodes < MinNodes (%u < %u)",
		     bit_set_count(topo_eval.node_map), topo_eval.min_nodes);
#endif
		return NULL;
	}

	core_array_log("_select_nodes/enter",
		       topo_eval.node_map, topo_eval.avail_core);
	/* Determine resource availability on each node for pending job */
	topo_eval.avail_res_array =
		_get_res_avail(topo_eval.job_ptr, topo_eval.node_map,
			       topo_eval.avail_core,
			       node_usage, topo_eval.cr_type, test_only,
			       will_run, part_core_map, resv_exc_ptr);
	if (!topo_eval.avail_res_array)
		return NULL;

	/* Eliminate nodes that don't have sufficient resources for this job */
	for (int n = 0; next_node_bitmap(topo_eval.node_map, &n); n++) {
		if ((!topo_eval.avail_res_array[n] ||
		     !topo_eval.avail_res_array[n]->avail_cpus)) {
			/* insufficient resources available on this node */
			bit_clear(topo_eval.node_map, n);
		}
	}
	if ((bit_set_count(topo_eval.node_map) < topo_eval.min_nodes) ||
	    (req_map && !bit_super_set(req_map, topo_eval.node_map))) {
		rc = SLURM_ERROR;
		goto fini;
	}
	core_array_log("_select_nodes/elim_nodes",
		       topo_eval.node_map, topo_eval.avail_core);

	/* Select the best nodes for this job */
	if (details_ptr->ntasks_per_node && details_ptr->num_tasks) {
		i = ROUNDUP(details_ptr->num_tasks,
			    details_ptr->ntasks_per_node);
		topo_eval.min_nodes = MAX(topo_eval.min_nodes, i);
	}

	rc = topology_g_eval_nodes(&topo_eval);
	if (rc != SLURM_SUCCESS)
		goto fini;

	core_array_log("_select_nodes/choose_nodes",
		       topo_eval.node_map, topo_eval.avail_core);

	/* If successful, sync up the avail_core with the node_map */
	if (rc == SLURM_SUCCESS) {
		int n;
		for (n = 0; n < bit_size(topo_eval.node_map); n++) {
			if (!topo_eval.avail_res_array[n] ||
			    !bit_test(topo_eval.node_map, n))
				FREE_NULL_BITMAP(topo_eval.avail_core[n]);
		}
	}
	core_array_log("_select_nodes/sync_cores",
		       topo_eval.node_map, topo_eval.avail_core);

fini:	if (rc != SLURM_SUCCESS) {
		*select_rc = rc;
		_free_avail_res_array(topo_eval.avail_res_array);
		return NULL;
	}

	return topo_eval.avail_res_array;
}

/*
 * Sort the usable_node element to put jobs in the correct
 * preemption order.
 */
static int _sort_usable_nodes_dec(void *j1, void *j2)
{
	job_record_t *job_a = *(job_record_t **) j1;
	job_record_t *job_b = *(job_record_t **) j2;

	if (job_a->details->usable_nodes > job_b->details->usable_nodes)
		return -1;
	else if (job_a->details->usable_nodes < job_b->details->usable_nodes)
		return 1;

	return 0;
}

/*
 * Determine which of these nodes are usable by this job
 *
 * Remove nodes from node_bitmap that don't have enough memory or other
 * resources to support this job.
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
static int _verify_node_state(part_res_record_t *cr_part_ptr,
			      job_record_t *job_ptr,
			      bitstr_t *node_bitmap,
			      uint16_t cr_type,
			      node_use_record_t *node_usage,
			      enum node_cr_state job_node_req,
			      resv_exc_t *resv_exc_ptr, bool use_extra_row)
{
	node_record_t *node_ptr;
	uint32_t gres_cpus, gres_cores;
	uint64_t free_mem, min_mem, avail_mem;
	list_t *gres_list;

	if (!(job_ptr->bit_flags & JOB_MEM_SET) &&
	    (min_mem = gres_select_util_job_mem_max(job_ptr->gres_list_req))) {
		/*
		 * Clear default partition or system per-node memory limit.
		 * Rely exclusively upon the per-GRES memory limit.
		 */
		job_ptr->details->pn_min_memory = 0;
	} else if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
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

	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		/* node-level memory check */
		if (min_mem && (cr_type & CR_MEMORY)) {
			avail_mem = node_ptr->real_memory -
				    node_ptr->mem_spec_limit;
			if (avail_mem > node_usage[i].alloc_memory) {
				free_mem = avail_mem -
					node_usage[i].alloc_memory;
			} else
				free_mem = 0;
			if (free_mem < min_mem) {
				debug3("Not considering node %s, free_mem < min_mem (%"PRIu64" < %"PRIu64") for %pJ",
				       node_ptr->name,
				       free_mem, min_mem, job_ptr);
				goto clear_bit;
			}
		} else if (cr_type & CR_MEMORY) {   /* --mem=0 for all memory */
			if (node_usage[i].alloc_memory) {
				debug3("Not considering node %s, allocated memory = %"PRIu64" and all memory requested for %pJ",
				       node_ptr->name,
				       node_usage[i].alloc_memory, job_ptr);
				goto clear_bit;
			}
		}

		/* Exclude nodes with reserved cores */
		if ((job_ptr->details->whole_node & WHOLE_NODE_REQUIRED) &&
		    resv_exc_ptr->exc_cores) {
			if (resv_exc_ptr->exc_cores[i] &&
			    (bit_ffs(resv_exc_ptr->exc_cores[i]) != -1)) {
				debug3("node %s exclusive", node_ptr->name);
				goto clear_bit;
			}
		}

		/* node-level GRES check, assumes all cores usable */
		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;

		if ((job_ptr->details->whole_node & WHOLE_NODE_REQUIRED) &&
		    gres_node_state_list_has_alloc_gres(gres_list)) {
			debug3("node %s has GRES in use (whole node requested)",
			       node_ptr->name);
			goto clear_bit;
		}

		gres_cores = gres_job_test(job_ptr->gres_list_req,
					   gres_list, true,
					   0, 0, job_ptr->job_id,
					   node_ptr->name);
		gres_cpus = gres_cores;
		if (gres_cpus != NO_VAL)
			gres_cpus *= node_ptr->tpc;
		if (gres_cpus == 0) {
			debug3("node %s lacks GRES",
			       node_ptr->name);
			goto clear_bit;
		}

		/* exclusive node check */
		if (node_usage[i].node_state >= NODE_CR_RESERVED) {
			debug3("node %s in exclusive use",
			       node_ptr->name);
			goto clear_bit;

			/* non-resource-sharing node check */
		} else if (node_usage[i].node_state >= NODE_CR_ONE_ROW) {
			if ((job_node_req == NODE_CR_RESERVED) ||
			    (job_node_req == NODE_CR_AVAILABLE)) {
				log_flag(SELECT_TYPE, "node %s is running --exclusive job",
					 node_ptr->name);
				goto clear_bit;
			}
			/*
			 * cannot use this node if it is running jobs
			 * in sharing partitions
			 */
			if (_is_node_busy(cr_part_ptr, i, true,
					  job_ptr->part_ptr, use_extra_row,
					  node_usage[i].jobs)) {
				log_flag(SELECT_TYPE, "node %s is running job that shares resouces in other partition",
					 node_ptr->name);
				goto clear_bit;
			}

			/* node is NODE_CR_AVAILABLE - check job request */
		} else {
			if (job_node_req == NODE_CR_RESERVED) {
				if (_is_node_busy(cr_part_ptr, i, false,
						  job_ptr->part_ptr,
						  use_extra_row,
						  node_usage[i].jobs)) {
					log_flag(SELECT_TYPE, "node %s is running other jobs, cannot run --exclusive job here",
						 node_ptr->name);
					goto clear_bit;
				}
			} else if (job_node_req == NODE_CR_ONE_ROW) {
				/*
				 * cannot use this node if it is running jobs
				 * in sharing partitions
				 */
				if (_is_node_busy(cr_part_ptr, i, true,
						  job_ptr->part_ptr,
						  use_extra_row,
						  node_usage[i].jobs)) {
					log_flag(SELECT_TYPE, "node %s is running job that shares resources in other partition",
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
 * _job_test - does most of the real work for select_p_job_test(), which
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
static int _job_test(job_record_t *job_ptr, bitstr_t *node_bitmap,
		     uint32_t min_nodes, uint32_t max_nodes,
		     uint32_t req_nodes, int mode, uint16_t cr_type,
		     enum node_cr_state job_node_req,
		     part_res_record_t *cr_part_ptr,
		     node_use_record_t *node_usage, list_t *license_list,
		     resv_exc_t *resv_exc_ptr, bool prefer_alloc_nodes,
		     bool use_extra_row, bool preempt_mode,
		     list_t *qos_preemptees)
{
	int error_code = SLURM_SUCCESS, select_rc = SLURM_SUCCESS;
	bitstr_t *orig_node_map, **part_core_map = NULL;
	bitstr_t **free_cores_tmp = NULL,  *node_bitmap_tmp = NULL;
	bitstr_t **free_cores_tmp2 = NULL, *node_bitmap_tmp2 = NULL;
	bitstr_t **avail_cores, **free_cores, **avail_cores_tmp = NULL;
	bool test_only = false, will_run = false;
	bool have_gres_max_tasks = false;
	uint32_t sockets_per_node = 1;
	uint32_t c, j, n, c_alloc = 0, c_size, total_cpus;
	uint32_t *gres_min_cpus;
	uint64_t save_mem = 0, avail_mem = 0, needed_mem = 0, lowest_mem = 0;
	int32_t build_cnt;
	job_resources_t *job_res;
	job_details_t *details_ptr = job_ptr->details;
	part_res_record_t *p_ptr, *jp_ptr;
	uint16_t *cpu_count;
	int i;
	avail_res_t **avail_res_array, **avail_res_array_tmp;
	gres_mc_data_t *tres_mc_ptr = NULL;
	list_t **node_gres_list = NULL, **sock_gres_list = NULL;
	uint32_t *gres_task_limit = NULL;
	char *nodename = NULL;
	node_record_t *node_ptr;
	uint32_t orig_min_nodes = min_nodes;
	uint32_t next_job_size = 0;
	uint32_t ntasks_per_node;

	free_job_resources(&job_ptr->job_resrcs);
	part_data_rebuild_rows(cr_part_ptr);

	if (mode == SELECT_MODE_TEST_ONLY)
		test_only = true;
	else if (mode == SELECT_MODE_WILL_RUN)
		will_run = true;
	if (qos_preemptees) {
		use_extra_row = true;
		preempt_mode = false;
	}

	/* check node_state and update the node_bitmap as necessary */
	if (!test_only) {
		error_code = _verify_node_state(
			cr_part_ptr, job_ptr, node_bitmap, cr_type,
			node_usage, job_node_req, resv_exc_ptr, use_extra_row);
		if (error_code != SLURM_SUCCESS) {
			return error_code;
		}
	}
	if (details_ptr->job_size_bitmap) {
		int start;
		n = bit_set_count(node_bitmap);
		if (max_nodes < n)
			n = max_nodes;
		start = bit_fls_from_bit(details_ptr->job_size_bitmap, n);
		if (start < 1 || start < orig_min_nodes)
			return SLURM_ERROR;
		max_nodes = start;
		min_nodes = max_nodes;
		req_nodes = max_nodes;
	}

	if (license_list) {
		/* Ensure job has access to requested licenses */
		int license_rc = license_job_test_with_list(job_ptr, time(NULL),
							    true, license_list,
							    true);
		if (license_rc == SLURM_ERROR) {
			log_flag(SELECT_TYPE,
				 "test 0 fail: insufficient licenses configured");
			return ESLURM_LICENSES_UNAVAILABLE;
		}
		if (!test_only && license_rc == EAGAIN) {
			log_flag(SELECT_TYPE,
				 "test 0 fail: insufficient licenses available");
			return ESLURM_LICENSES_UNAVAILABLE;
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

	ntasks_per_node = MAX(details_ptr->ntasks_per_node, 1);
	if (details_ptr->mc_ptr && details_ptr->mc_ptr->sockets_per_node)
		sockets_per_node = details_ptr->mc_ptr->sockets_per_node;
	_set_gpu_defaults(job_ptr);
	if (!job_ptr->gres_list_req_accum)
		job_ptr->gres_list_req_accum =
			gres_select_util_create_list_req_accum(
				job_ptr->gres_list_req);
	details_ptr->min_gres_cpu = gres_select_util_job_min_cpu_node(
		sockets_per_node,
		details_ptr->ntasks_per_node,
		job_ptr->gres_list_req_accum);
	details_ptr->min_job_gres_cpu = gres_select_util_job_min_cpus(
		details_ptr->min_nodes,
		sockets_per_node,
		ntasks_per_node * details_ptr->min_nodes,
		job_ptr->gres_list_req_accum);

	log_flag(SELECT_TYPE, "evaluating %pJ on %u nodes",
	         job_ptr, bit_set_count(node_bitmap));

	orig_node_map = bit_copy(node_bitmap);
	avail_cores = cons_helpers_mark_avail_cores(node_bitmap, job_ptr);

	/*
	 * test to make sure that this job can succeed with all avail_cores
	 * if 'no' then return FAIL
	 * if 'yes' then we will seek the optimal placement for this job
	 *          within avail_cores
	 */
	free_cores = copy_core_array(avail_cores);
	tres_mc_ptr = _build_gres_mc_data(job_ptr);

try_next_nodes_cnt:
	if (details_ptr->job_size_bitmap) {
		int next = bit_fls_from_bit(details_ptr->job_size_bitmap,
					    max_nodes - 1);
		if (next > 0 && next >= orig_min_nodes)
			next_job_size = next;
		else
			next_job_size = 0;
	}

	if ((gang_mode == 0) &&
	    (job_node_req == NODE_CR_ONE_ROW ||
	     job_node_req == NODE_CR_RESERVED) &&
	    !test_only) {
		log_flag(SELECT_TYPE, "test 0 skipped: goto test 1");
		goto skip_test0;
	}

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr, &select_rc);
	if ((!avail_res_array || !job_ptr->best_switch) && next_job_size) {
		log_flag(SELECT_TYPE, "test 0 fail: try again");
		bit_copybits(node_bitmap, orig_node_map);
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		min_nodes = next_job_size;
		max_nodes = next_job_size;
		req_nodes = next_job_size;
		goto try_next_nodes_cnt;
	} else if (!avail_res_array) {
		/* job can not fit */
		xfree(tres_mc_ptr);
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		log_flag(SELECT_TYPE, "test 0 fail: insufficient resources");
		return select_rc ? select_rc : SLURM_ERROR;
	} else if (test_only) {
		xfree(tres_mc_ptr);
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		_free_avail_res_array(avail_res_array);
		log_flag(SELECT_TYPE, "test 0 pass: test_only");
		return SLURM_SUCCESS;
	} else if (!job_ptr->best_switch) {
		xfree(tres_mc_ptr);
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		_free_avail_res_array(avail_res_array);
		log_flag(SELECT_TYPE, "test 0 fail: waiting for switches");
		return select_rc ? select_rc : SLURM_ERROR;
	}

	log_flag(SELECT_TYPE, "test 0 pass - job fits on given resources");
	_free_avail_res_array(avail_res_array);

	/*
	 * now that we know that this job can run with the given resources,
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
	bit_copybits(node_bitmap, orig_node_map);

	free_core_array(&free_cores);
	free_cores = copy_core_array(avail_cores);

skip_test0:
	if (resv_exc_ptr->exc_cores) {
#if _DEBUG
		core_array_log("exclude reserved cores",
			       NULL, resv_exc_ptr->exc_cores);
#endif
		core_array_and_not(free_cores, resv_exc_ptr->exc_cores);
	}

	/* remove all existing allocations from free_cores */
	for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;

			core_array_and_not(free_cores,
					   p_ptr->row[i].row_bitmap);
			if (p_ptr->part_ptr != job_ptr->part_ptr)
				continue;
			if (part_core_map) {
				core_array_or(part_core_map,
					      p_ptr->row[i].row_bitmap);
			} else {
				part_core_map = copy_core_array(
					p_ptr->row[i].row_bitmap);
			}
		}
	}
	if (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	_block_by_topology(job_ptr, cr_part_ptr, node_bitmap);

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr, &select_rc);
	if (avail_res_array && job_ptr->best_switch) {
		/* job fits! We're done. */
		log_flag(SELECT_TYPE, "test 1 pass - idle resources found");
		goto alloc_job;
	}
	_free_avail_res_array(avail_res_array);
	avail_res_array = NULL;

	if ((gang_mode == 0) && (job_node_req == NODE_CR_ONE_ROW)) {
		/*
		 * This job CANNOT share CPUs regardless of priority,
		 * so we fail here. Note that OverSubscribe=EXCLUSIVE was
		 * already addressed in _verify_node_state() and
		 * job preemption removes jobs from simulated resource
		 * allocation map before this point.
		 */
		log_flag(SELECT_TYPE, "test 1 fail - no idle resources available");
		goto alloc_job;
	}
	log_flag(SELECT_TYPE, "test 1 fail - not enough idle resources");

	/*** Step 2 ***/
	for (jp_ptr = cr_part_ptr; jp_ptr; jp_ptr = jp_ptr->next) {
		if (jp_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!jp_ptr) {
		error("could not find partition for %pJ",
		      job_ptr);
		goto alloc_job;
	}

	bit_copybits(node_bitmap, orig_node_map);
	free_core_array(&free_cores);
	free_cores = copy_core_array(avail_cores);
	if (resv_exc_ptr->exc_cores)
		core_array_and_not(free_cores, resv_exc_ptr->exc_cores);

	if (preempt_by_part) {
		/*
		 * Remove from avail_cores resources allocated to jobs which
		 * this job can not preempt
		 */
		log_flag(SELECT_TYPE, "looking for higher-priority or PREEMPT_MODE_OFF part's to remove from avail_cores");

		for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
			if ((p_ptr->part_ptr->priority_tier <=
			     jp_ptr->part_ptr->priority_tier) &&
			    (p_ptr->part_ptr->preempt_mode !=
			     PREEMPT_MODE_OFF)) {
				log_flag(SELECT_TYPE, "continuing on part: %s",
				         p_ptr->part_ptr->name);
				continue;
			}
			/*
			 * If the partition allows oversubscription we can't
			 * easily determine if the job can start. It
			 * may be that it won't be able to start because of
			 * preemption, but it may be able to start on different
			 * row.
			 */
			if ((p_ptr->part_ptr == jp_ptr->part_ptr) &&
			    (p_ptr->num_rows > 1))
				continue;
			if (!p_ptr->row)
				continue;
			for (i = 0; i < p_ptr->num_rows; i++) {
				if (!p_ptr->row[i].row_bitmap)
					continue;
				core_array_and_not(free_cores,
						   p_ptr->row[i].row_bitmap);
			}
		}
	}

	if (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	_block_by_topology(job_ptr, cr_part_ptr, node_bitmap);

	/* make these changes permanent */
	avail_cores_tmp = avail_cores;
	avail_cores = copy_core_array(free_cores);
	bit_copybits(orig_node_map, node_bitmap);

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr, &select_rc);
	if (!avail_res_array) {
		/*
		 * job needs resources that are currently in use by
		 * higher-priority jobs, so fail for now
		 */
		log_flag(SELECT_TYPE, "test 2 fail - resources busy with higher priority jobs");
		goto alloc_job;
	}
	_free_avail_res_array(avail_res_array);
	log_flag(SELECT_TYPE, "test 2 pass - available resources for this priority");

	/*** Step 3 ***/
	bit_copybits(node_bitmap, orig_node_map);
	free_core_array(&free_cores);
	free_cores = copy_core_array(avail_cores);

	/*
	 * remove existing allocations (jobs) from same-priority partitions
	 * from avail_cores
	 */
	for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->part_ptr->priority_tier !=
		    jp_ptr->part_ptr->priority_tier)
			continue;
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			core_array_and_not(free_cores,
					   p_ptr->row[i].row_bitmap);
		}
	}

	if (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	_block_by_topology(job_ptr, cr_part_ptr, node_bitmap);

	free_cores_tmp  = copy_core_array(free_cores);
	node_bitmap_tmp = bit_copy(node_bitmap);
	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr, &select_rc);
	if (avail_res_array) {
		/*
		 * To the extent possible, remove from consideration resources
		 * which are allocated to jobs in lower priority partitions.
		 */
		log_flag(SELECT_TYPE, "test 3 pass - found resources");
		for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr->priority_tier >=
			    jp_ptr->part_ptr->priority_tier)
				continue;
			if (!p_ptr->row)
				continue;
			for (i = 0; i < p_ptr->num_rows; i++) {
				if (!p_ptr->row[i].row_bitmap)
					continue;
				core_array_and_not(free_cores_tmp,
						   p_ptr->row[i].row_bitmap);
			}
			if (job_ptr->details->whole_node &
			    WHOLE_NODE_REQUIRED) {
				_block_whole_nodes(node_bitmap_tmp, avail_cores,
						   free_cores_tmp);
			}

			_block_by_topology(job_ptr, cr_part_ptr,
					   node_bitmap_tmp);

			free_cores_tmp2  = copy_core_array(free_cores_tmp);
			node_bitmap_tmp2 = bit_copy(node_bitmap_tmp);
			avail_res_array_tmp = _select_nodes(
				job_ptr, min_nodes, max_nodes, req_nodes,
				node_bitmap_tmp, free_cores_tmp, node_usage,
				cr_type, test_only, will_run, part_core_map,
				prefer_alloc_nodes, tres_mc_ptr,
				resv_exc_ptr, &select_rc);
			if (!avail_res_array_tmp) {
				free_core_array(&free_cores_tmp2);
				FREE_NULL_BITMAP(node_bitmap_tmp2);
				break;
			}
			log_flag(SELECT_TYPE, "remove low-priority partition %s",
			         p_ptr->part_ptr->name);
			free_core_array(&free_cores);
			free_cores      = free_cores_tmp;
			free_cores_tmp  = free_cores_tmp2;
			free_cores_tmp2 = NULL;
			bit_copybits(node_bitmap, node_bitmap_tmp);
			FREE_NULL_BITMAP(node_bitmap_tmp);
			node_bitmap_tmp  = node_bitmap_tmp2;
			node_bitmap_tmp2 = NULL;
			_free_avail_res_array(avail_res_array);
			avail_res_array = avail_res_array_tmp;
		}
		goto alloc_job;
	}
	log_flag(SELECT_TYPE, "test 3 fail - not enough idle resources in same priority");

	/*** Step 4 ***/
	/*
	 * try to fit the job into an existing row
	 *
	 * free_cores = core_bitmap to be built
	 * avail_cores = static core_bitmap of all available cores
	 */

	if (!jp_ptr || !jp_ptr->row) {
		/*
		 * there's no existing jobs in this partition, so place
		 * the job in avail_cores. FIXME: still need a good
		 * placement algorithm here that optimizes "job overlap"
		 * between this job (in these idle nodes) and existing
		 * jobs in the other partitions with <= priority to
		 * this partition
		 */
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		bit_copybits(node_bitmap, orig_node_map);
		avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
						req_nodes, node_bitmap,
						free_cores, node_usage, cr_type,
						test_only, will_run,
						part_core_map,
						prefer_alloc_nodes,
						tres_mc_ptr, resv_exc_ptr,
						&select_rc);
		if (avail_res_array)
			log_flag(SELECT_TYPE, "test 4 pass - first row found");
		goto alloc_job;
	}


	if ((jp_ptr->num_rows > 1) && (!preempt_by_qos || qos_preemptees))
		part_data_sort_res(jp_ptr);	/* Preserve row order for QOS */
	c = jp_ptr->num_rows;
	if (preempt_by_qos && !use_extra_row)
		c--;				/* Do not use extra row */
	if (qos_preemptees && use_extra_row) {
		list_itr_t *job_iterator;
		job_record_t *job_ptr;
		/*
		 * We may be putting the job in extra row. We need to make sure
		 * that extra row allows the use of resources of jobs that we
		 * were allowed to preempt and we already know the job should
		 * fit there. If we just leave the row empty, we will select
		 * the nodes not taking currently running jobs into account
		 */
		if (!jp_ptr->row[c - 1].row_bitmap)
			jp_ptr->row[c - 1].row_bitmap = build_core_array();
		for (int i = 0; i < (c - 1); i++) {
			core_array_or(jp_ptr->row[c - 1].row_bitmap,
				      jp_ptr->row[i].row_bitmap);
		}

		job_iterator = list_iterator_create(qos_preemptees);
		while ((job_ptr = list_next(job_iterator))) {
			job_res_rm_cores(job_ptr->job_resrcs,
					 &(jp_ptr->row[c - 1]));
		}
		list_iterator_destroy(job_iterator);
	}


	if (preempt_by_qos && (job_node_req != NODE_CR_AVAILABLE))
		c = 1;
	for (i = 0; i < c; i++) {
		if (!jp_ptr->row[i].row_bitmap)
			break;
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		core_array_and_not(free_cores, jp_ptr->row[i].row_bitmap);
		bit_copybits(node_bitmap, orig_node_map);
		if (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED)
			_block_whole_nodes(node_bitmap, avail_cores,free_cores);

		_block_by_topology(job_ptr, cr_part_ptr, node_bitmap);

		avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
						req_nodes, node_bitmap,
						free_cores, node_usage, cr_type,
						test_only, will_run,
						part_core_map,
						prefer_alloc_nodes,
						tres_mc_ptr, resv_exc_ptr,
						&select_rc);
		if (avail_res_array) {
			log_flag(SELECT_TYPE, "test 4 pass - row %i",
			         i);
			break;
		}
		log_flag(SELECT_TYPE, "test 4 fail - row %i",
		         i);
	}

	if ((i < c) && !jp_ptr->row[i].row_bitmap) {
		/* we've found an empty row, so use it */
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		bit_copybits(node_bitmap, orig_node_map);
		log_flag(SELECT_TYPE, "test 4 trying empty row %i",
		         i);
		avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
						req_nodes, node_bitmap,
						free_cores, node_usage, cr_type,
						test_only, will_run,
						part_core_map,
						prefer_alloc_nodes,
						tres_mc_ptr, resv_exc_ptr,
						&select_rc);
	}

	if (!avail_res_array) {
		/* job can't fit into any row, so exit */
		log_flag(SELECT_TYPE, "test 4 fail - busy partition");
		goto alloc_job;
	}

	/*
       *** CONSTRUCTION ZONE FOR STEPs 5 AND 6 ***
       * Note that while the job may have fit into a row, it should
       * still be run through a good placement algorithm here that
       * optimizes "job overlap" between this job (in these idle nodes)
       * and existing jobs in the other partitions with <= priority to
       * this partition
       */

alloc_job:
	/*
	 * at this point we've found a good set of nodes and cores for the job:
	 * - node_bitmap is the set of nodes to allocate
	 * - free_cores is the set of allocated cores
	 * - avail_res_array identifies cores and GRES
	 *
	 * Next steps are to cleanup the worker variables,
	 * create the job_resources struct,
	 * distribute the job on the bits, and exit
	 */
	if ((!avail_res_array || !job_ptr->best_switch) && next_job_size) {
		log_flag(SELECT_TYPE, "no idle resources, try next job size:%u",
			 next_job_size);
		bit_copybits(node_bitmap, orig_node_map);
		free_core_array(&free_cores);
		if (avail_cores_tmp) {
			free_core_array(&avail_cores);
			avail_cores = avail_cores_tmp;
			avail_cores_tmp = NULL;
		}
		free_cores = copy_core_array(avail_cores);
		min_nodes = next_job_size;
		max_nodes = next_job_size;
		req_nodes = next_job_size;
		goto try_next_nodes_cnt;
	}

	if (avail_cores_tmp)
		free_core_array(&avail_cores_tmp);

	FREE_NULL_BITMAP(orig_node_map);
	free_core_array(&part_core_map);
	free_core_array(&free_cores_tmp);
	FREE_NULL_BITMAP(node_bitmap_tmp);
	if (!avail_res_array || !job_ptr->best_switch) {
		/* we were sent here to cleanup and exit */
		xfree(tres_mc_ptr);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		_free_avail_res_array(avail_res_array);
		log_flag(SELECT_TYPE, "exiting with no allocation");
		return select_rc ? select_rc : SLURM_ERROR;
	}

	if ((mode != SELECT_MODE_WILL_RUN) && (job_ptr->part_ptr == NULL))
		error_code = EINVAL;
	if ((error_code == SLURM_SUCCESS) && (mode == SELECT_MODE_WILL_RUN)) {
		/*
		 * Set a reasonable value for the number of allocated CPUs.
		 * Without computing task distribution this is only a guess
		 */
		job_ptr->total_cpus = MAX(job_ptr->details->min_cpus,
					  job_ptr->details->min_nodes);
	}

	/*
	 * Defer checking select mode until we get a correct CPU count. Then
	 * exit if select mode is not SELECT_MODE_RUN_NOW, making sure to free
	 * job_ptr->job_resrcs.
	 */
	if (error_code != SLURM_SUCCESS) {
		xfree(tres_mc_ptr);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		_free_avail_res_array(avail_res_array);
		return error_code;
	}

	log_flag(SELECT_TYPE, "distributing %pJ",
	         job_ptr);

	/** create the struct_job_res  **/
	n = bit_set_count(node_bitmap);
	cpu_count = xmalloc(sizeof(uint16_t) * n);
	for (i = 0, j = 0; next_node_bitmap(node_bitmap, &i); i++) {
		if (avail_res_array[i])
			cpu_count[j++] = avail_res_array[i]->avail_cpus;
	}
	if (j != n) {
		error("problem building cpu_count array (%d != %d)",
		      j, n);
	}

	job_res                   = create_job_resources();
	job_res->node_bitmap      = bit_copy(node_bitmap);
	job_res->nodes = bitmap2node_name_sortable(node_bitmap, false);
	job_res->nhosts           = n;
	job_res->ncpus            = job_res->nhosts;
	job_res->threads_per_core = job_ptr->details->mc_ptr->threads_per_core;
	job_res->cr_type = cr_type;

	if (job_ptr->details->ntasks_per_node)
		job_res->ncpus   *= details_ptr->ntasks_per_node;
	/* See if # of cpus increases with ntasks_per_tres */
	i = gres_select_util_job_min_tasks(job_res->nhosts, sockets_per_node,
					   details_ptr->ntasks_per_tres, "gpu",
					   job_ptr->gres_list_req);
	job_res->ncpus            = MAX(job_res->ncpus, i);
	job_res->ncpus            = MAX(job_res->ncpus,
					details_ptr->min_cpus);
	job_res->ncpus            = MAX(job_res->ncpus,
					(job_res->nhosts *
					 details_ptr->pn_min_cpus));
	if (job_ptr->details->mc_ptr)
		sockets_per_node = job_ptr->details->mc_ptr->sockets_per_node;
	if (!job_ptr->gres_list_req_accum)
		job_ptr->gres_list_req_accum =
			gres_select_util_create_list_req_accum(
				job_ptr->gres_list_req);
	i = gres_select_util_job_min_cpus(job_res->nhosts, sockets_per_node,
					  job_ptr->details->num_tasks,
					  job_ptr->gres_list_req_accum);
	job_res->ncpus            = MAX(job_res->ncpus, i);
	job_res->node_req         = job_node_req;
	job_res->cpus             = cpu_count;	/* Per node CPU counts */
	job_res->cpus_used        = xmalloc(job_res->nhosts *
					    sizeof(uint16_t));
	job_res->memory_allocated = xmalloc(job_res->nhosts *
					    sizeof(uint64_t));
	job_res->memory_used      = xmalloc(job_res->nhosts *
					    sizeof(uint64_t));
	job_res->whole_node       = job_ptr->details->whole_node;

	/* store the hardware data for the selected nodes */
	error_code = build_job_resources(job_res);
	if (error_code != SLURM_SUCCESS) {
		xfree(tres_mc_ptr);
		_free_avail_res_array(avail_res_array);
		free_job_resources(&job_res);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		return error_code;
	}

	/* total up all CPUs and load the core_bitmap */
	total_cpus = 0;
	c = 0;
	if (job_res->core_bitmap)
		c_size = bit_size(job_res->core_bitmap);
	else
		c_size = 0;
	gres_min_cpus = xcalloc(job_res->nhosts, sizeof(uint32_t));
	for (i = 0, n = 0; (node_ptr = next_node_bitmap(node_bitmap, &i));
	     i++) {
		uint32_t gres_min_node_cpus;
		int first_core = 0, last_core = node_ptr->tot_cores;
		bitstr_t *use_free_cores = free_cores[i];

		for (j = first_core; j < last_core; j++, c++) {
			if (!bit_test(use_free_cores, j))
				continue;
			if (c >= c_size) {
				error("core_bitmap index error on node %s (NODE_INX:%d, C_SIZE:%u)",
				      node_ptr->name, i, c_size);
				drain_nodes(node_ptr->name, "Bad core count",
					    getuid());
				_free_avail_res_array(avail_res_array);
				free_job_resources(&job_res);
				free_core_array(&free_cores);
				xfree(gres_min_cpus);
				return SLURM_ERROR;
			}
			bit_set(job_res->core_bitmap, c);
			c_alloc++;
		}
		if ((gres_min_node_cpus = avail_res_array[i]->gres_min_cpus)) {
			gres_min_cpus[n] = gres_min_node_cpus;
			log_flag(
				SELECT_TYPE,
				"%pJ: Node=%s: job_res->cpus[%d]=%u, gres_min_cpus[%d]=%u",
				job_ptr, node_record_table_ptr[i]->name, i,
				job_res->cpus[n], i, gres_min_cpus[n]);
		}
		if (avail_res_array[i]->gres_max_tasks)
			have_gres_max_tasks = true;
		total_cpus += job_res->cpus[n];
		n++;
	}

	/*
	 * When 'srun --overcommit' is used, ncpus is set to a minimum value
	 * in order to allocate the appropriate number of nodes based on the
	 * job request.
	 * For cons_tres, all available logical processors will be allocated on
	 * each allocated node in order to accommodate the overcommit request.
	 */
	if (details_ptr->overcommit && details_ptr->num_tasks)
		job_res->ncpus = MIN(total_cpus, details_ptr->num_tasks);

	log_flag(SELECT_TYPE, "%pJ ncpus %u cbits %u/%u nbits %u",
	         job_ptr, job_res->ncpus,
	         count_core_array_set(free_cores), c_alloc, job_res->nhosts);
	free_core_array(&free_cores);

	/* distribute the tasks, clear unused cores from job_res->core_bitmap */
	job_ptr->job_resrcs = job_res;
	if (job_ptr->gres_list_req && (error_code == SLURM_SUCCESS)) {
		bool have_gres_per_task, task_limit_set = false;
		/*
		 * Determine if any job gres_per_task specification here
		 * to avoid calling gres_get_task_limit unless needed
		 */
		have_gres_per_task = gres_select_util_job_tres_per_task(
			job_ptr->gres_list_req);
		if (have_gres_per_task || have_gres_max_tasks) {
			gres_task_limit = xcalloc(job_res->nhosts,
						  sizeof(uint32_t));
		}
		node_gres_list = xcalloc(job_res->nhosts, sizeof(list_t *));
		sock_gres_list = xcalloc(job_res->nhosts, sizeof(list_t *));
		for (i = 0, j = 0;
		     (node_ptr = next_node_bitmap(job_res->node_bitmap, &i));
		     i++) {
			if (have_gres_per_task) {
				gres_task_limit[j] =
					gres_select_util_get_task_limit(
						avail_res_array[i]->
						sock_gres_list);
				if (gres_task_limit[j] != NO_VAL)
					task_limit_set = true;
			} else if (have_gres_max_tasks) {
				gres_task_limit[j] =
					avail_res_array[i]->gres_max_tasks;
				task_limit_set = true;
			}
			node_gres_list[j] = node_ptr->gres_list;
			sock_gres_list[j] =
				avail_res_array[i]->sock_gres_list;
			if (task_limit_set)
				log_flag(SELECT_TYPE, "%pJ: Node=%s: gres_task_limit[%d]=%u",
					 job_ptr,
					 node_ptr->name,
					 i, gres_task_limit[j]);
			j++;
		}

		if (!task_limit_set)
			xfree(gres_task_limit);
	}
	error_code = dist_tasks(job_ptr, cr_type, preempt_mode, avail_cores,
				gres_task_limit, gres_min_cpus);
	xfree(gres_min_cpus);
	if (job_ptr->gres_list_req && (error_code == SLURM_SUCCESS)) {
		error_code = gres_select_filter_select_and_set(
			sock_gres_list, job_ptr, tres_mc_ptr);
	}
	xfree(gres_task_limit);
	xfree(node_gres_list);
	xfree(sock_gres_list);
	xfree(tres_mc_ptr);
	_free_avail_res_array(avail_res_array);
	free_core_array(&avail_cores);
	if (error_code != SLURM_SUCCESS) {
		free_job_resources(&job_ptr->job_resrcs);
		return error_code;
	}

	/* translate job_res->cpus array into format with repitition count */
	build_cnt = build_job_resources_cpu_array(job_res);
	if (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED) {
		job_ptr->total_cpus = 0;
		for (i = 0;
		     (node_ptr = next_node_bitmap(job_res->node_bitmap, &i));
		     i++) {
			/*
			 * This could make the job_res->cpus incorrect.
			 * Don't use job_res->cpus when allocating
			 * whole nodes as the job is finishing to
			 * subtract from the total cpu count or you
			 * will get an incorrect count.
			 */
			job_ptr->total_cpus += node_ptr->cpus_efctv;
		}
	} else if (cr_type & CR_SOCKET) {
		int ci = 0;
		int s, last_s, sock_cnt = 0;

		job_ptr->total_cpus = 0;
		for (i = 0;
		     (node_ptr = next_node_bitmap(job_res->node_bitmap, &i));
		     i++) {

			sock_cnt = 0;
			for (s = 0; s < node_ptr->tot_sockets; s++) {
				last_s = -1;
				for (c = 0; c < node_ptr->cores; c++) {
					if (bit_test(job_res->core_bitmap,
						     ci)) {
						if (s != last_s) {
							sock_cnt++;
							last_s = s;
						}
					}
					ci++;
				}
			}
			job_ptr->total_cpus += (sock_cnt * node_ptr->cores *
						node_ptr->tpc);
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
		/*
		 * In the cases where we are evaluating a preemptor job,
		 * we need to save a copy of the assigned node_bitmap so
		 * we have enough information to accurately determine
		 * if we are not breaking accounting policy limits later on.
		 */
		FREE_NULL_BITMAP(job_ptr->node_bitmap_preempt);
		job_ptr->node_bitmap_preempt =
			bit_copy(job_ptr->job_resrcs->node_bitmap);

		/*
		 * If we are a reservation the job_id == 0, we don't want to
		 * free job_resrcs here.
		 */
		if (job_ptr->job_id)
			free_job_resources(&job_ptr->job_resrcs);
		return error_code;
	}

	if (!(cr_type & CR_MEMORY))
		return error_code;

	if (!(job_ptr->bit_flags & JOB_MEM_SET) &&
	    gres_select_util_job_mem_set(job_ptr->gres_list_req, job_res)) {
		debug("%pJ memory set via GRES limit", job_ptr);
	} else {
		/* load memory allocated array */
		save_mem = details_ptr->pn_min_memory;
		for (i = 0, j = 0;
		     (node_ptr = next_node_bitmap(job_res->node_bitmap, &i));
		     i++) {
			nodename = node_ptr->name;
			avail_mem = node_ptr->real_memory -
				    node_ptr->mem_spec_limit;
			if (save_mem & MEM_PER_CPU) {	/* Memory per CPU */
				/*
				 * If the job requested less threads that we
				 * allocated but requested memory based on cpu
				 * count we would need to adjust that to avoid
				 * getting more memory than we are actually
				 * expecting.
				 */
				uint16_t cpu_count =
					job_resources_get_node_cpu_cnt(
						job_res, j, i);
				needed_mem = cpu_count *
					(save_mem & (~MEM_PER_CPU));
			} else if (save_mem) {		/* Memory per node */
				needed_mem = save_mem;
			} else {		/* Allocate all node memory */
				needed_mem = avail_mem;
				if (node_usage[i].alloc_memory > 0) {
					log_flag(SELECT_TYPE, "node %s has already alloc_memory=%"PRIu64". %pJ can't allocate all node memory",
					         nodename,
					         node_usage[i].alloc_memory,
					         job_ptr);
					error_code = SLURM_ERROR;
					break;
				}
				if ((j == 0) || (lowest_mem > avail_mem))
					lowest_mem = avail_mem;
			}
			if (save_mem) {
				if (node_usage[i].alloc_memory > avail_mem) {
					error("node %s memory is already overallocated (%"PRIu64" > %"PRIu64"). %pJ can't allocate any node memory",
					      nodename,
					      node_usage[i].alloc_memory,
					      avail_mem, job_ptr);
					error_code = SLURM_ERROR;
					break;
				}
				avail_mem -= node_usage[i].alloc_memory;
			}
			if (needed_mem > avail_mem) {
				log_flag(SELECT_TYPE, "%pJ would overallocate node %s memory (%"PRIu64" > %"PRIu64")",
				         job_ptr, nodename,
				         needed_mem, avail_mem);
				error_code = SLURM_ERROR;
				break;
			}
			job_res->memory_allocated[j] = needed_mem;
			j++;
		}
	}
	if (error_code == SLURM_ERROR)
		free_job_resources(&job_ptr->job_resrcs);

	return error_code;
}

static uint16_t _setup_cr_type(job_record_t *job_ptr)
{
	uint16_t tmp_cr_type = slurm_conf.select_type_param;

	if (job_ptr->part_ptr->cr_type) {
		if ((tmp_cr_type & CR_SOCKET) || (tmp_cr_type & CR_CORE)) {
			tmp_cr_type &= ~(CR_SOCKET | CR_CORE | CR_MEMORY);
			tmp_cr_type |= job_ptr->part_ptr->cr_type;
		} else
			info("%s: Can't use Partition SelectType unless using CR_Socket or CR_Core",
			     plugin_type);
	}

	return tmp_cr_type;
}

/* Determine if a job can ever run */
static int _test_only(job_record_t *job_ptr, bitstr_t *node_bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
		      uint32_t req_nodes, uint16_t job_node_req)
{
	int rc;
	uint16_t tmp_cr_type = _setup_cr_type(job_ptr);

	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_TEST_ONLY, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage,
		       cluster_license_list, NULL, false, false, false, NULL);
	return rc;
}

static int _wrapper_get_usable_nodes(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)x;
	wrapper_rm_job_args_t *wargs = (wrapper_rm_job_args_t *)arg;

	if ((!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr)))
		return 0;

	wargs->rc += bit_overlap(wargs->node_map, job_ptr->node_bitmap);
	return 0;
}

static int _get_usable_nodes(bitstr_t *node_map, job_record_t *job_ptr)
{
	wrapper_rm_job_args_t wargs = {
		.node_map = node_map
	};

	if (!job_ptr->het_job_list)
		(void)_wrapper_get_usable_nodes(job_ptr, &wargs);
	else
		(void)list_for_each_nobreak(job_ptr->het_job_list,
					    _wrapper_get_usable_nodes,
					    &wargs);
	return wargs.rc;
}

static int _wrapper_job_res_rm_job(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)x;
	wrapper_rm_job_args_t *wargs = (wrapper_rm_job_args_t *)arg;

	(void)job_res_rm_job(wargs->part_record_ptr, wargs->node_usage,
			     wargs->license_list, job_ptr, wargs->action,
			     wargs->node_map);

	return 0;
}

static int _job_res_rm_job(part_res_record_t *part_record_ptr,
			   node_use_record_t *node_usage, list_t *license_list,
			   job_record_t *job_ptr, int action,
			   bitstr_t *node_map)
{
	wrapper_rm_job_args_t wargs = {
		.action = action,
		.license_list = license_list,
		.node_usage = node_usage,
		.part_record_ptr = part_record_ptr,
		.node_map = node_map
	};

	if (!job_overlap_and_running(node_map, license_list, job_ptr))
		return 1;

	if (!job_ptr->het_job_list)
		(void)_wrapper_job_res_rm_job(job_ptr, &wargs);
	else
		(void)list_for_each(job_ptr->het_job_list,
				    _wrapper_job_res_rm_job,
				    &wargs);
	return 0;
}

static bitstr_t *_select_topo_bitmap(job_record_t *job_ptr,
				     bitstr_t *node_bitmap,
				     bitstr_t **efctv_bitmap)
{
	if (IS_JOB_WHOLE_TOPO(job_ptr)) {
		if (!(*efctv_bitmap)) {
			*efctv_bitmap = bit_copy(node_bitmap);
			topology_g_whole_topo(*efctv_bitmap);
		}
		return *efctv_bitmap;
	} else
		return node_bitmap;
}

static int _build_cr_job_list(void *x, void *arg)
{
	int action;
	job_record_t *tmp_job_ptr = (job_record_t *)x;
	job_record_t *job_ptr_preempt = NULL;
	cr_job_list_args_t *args = (cr_job_list_args_t *)arg;

	if (!IS_JOB_RUNNING(tmp_job_ptr) &&
	    !IS_JOB_SUSPENDED(tmp_job_ptr))
		return 0;
	if (tmp_job_ptr->end_time == 0) {
		error("Active %pJ has zero end_time", tmp_job_ptr);
		return 0;
	}
	if (tmp_job_ptr->node_bitmap == NULL) {
		/*
		 * This should indicate a requeued job was cancelled
		 * while NHC was running
		 */
		error("%pJ has NULL node_bitmap", tmp_job_ptr);
		return 0;
	}
	/*
	 * For hetjobs, only the leader component is potentially added
	 * to the preemptee_candidates. If the leader is preemptable,
	 * it will be removed in the else statement alongside all of the
	 * rest of the components. For such case, we don't want to
	 * append non-leaders to cr_job_list, otherwise we would be
	 * double deallocating them (once in this else statement and
	 * twice later in the simulation of jobs removal).
	 */
	job_ptr_preempt = tmp_job_ptr;
	if (tmp_job_ptr->het_job_id) {
		job_ptr_preempt = find_job_record(tmp_job_ptr->het_job_id);
		if (!job_ptr_preempt) {
			error("%pJ HetJob leader not found", tmp_job_ptr);
			return 0;
		}
	}
	if (job_ptr_preempt->end_time < args->start) {
		bitstr_t *efctv_bitmap_ptr;
		efctv_bitmap_ptr = _select_topo_bitmap(tmp_job_ptr,
						       args->orig_map,
						       args->tmp_bitmap_pptr);
		if (bit_overlap_any(efctv_bitmap_ptr,
				    tmp_job_ptr->node_bitmap) ||
		    license_list_overlap(tmp_job_ptr->license_list,
					 args->job_license_list)) {
			job_res_rm_job(args->future_part, args->future_usage,
				       args->future_license_list, tmp_job_ptr,
				       JOB_RES_ACTION_NORMAL, efctv_bitmap_ptr);
		}
	} else if (!_is_preemptable(job_ptr_preempt,
				    args->preemptee_candidates)) {
		/* Queue job for later removal from data structures */
		list_append(args->cr_job_list, tmp_job_ptr);
	} else if (tmp_job_ptr == job_ptr_preempt) {
		uint16_t mode = slurm_job_preempt_mode(tmp_job_ptr);
		if (mode == PREEMPT_MODE_OFF)
			return 0;
		if (mode == PREEMPT_MODE_SUSPEND) {
			/* remove cores, keep memory */
			action = JOB_RES_ACTION_RESUME;
			if (preempt_by_qos)
				*args->qos_preemptor = true;
		} else {
			/* remove cores and memory */
			action = JOB_RES_ACTION_NORMAL;
		}
		/* Remove preemptable job now */
		_job_res_rm_job(args->future_part, args->future_usage,
				args->future_license_list, tmp_job_ptr, action,
				args->orig_map);
	}
	return 0;
}

/*
 * Set scheduling weight for node bitmaps -- pre-nodeset scheduling.
 *
 * Similar to _set_sched_weight() in node_scheduler.c except this function
 * shifts by 16 instead of 8 to give room to accommodate extra weighted states
 * (e.g. completing and rebooting). sched_weight will be rebuilt when scheduling
 * based off of nodesets in _build_node_list().
 *
 * 0x00000000000## - Reserved for cons_tres, favor nodes with co-located CPU/GPU
 * 0x000000000##00 - Reserved for completing and rebooting nodes
 * 0x0########0000 - Node weight
 * 0x#000000000000 - Reserved for powered down nodes
 *
 * 0x0000000000100 - Completing nodes
 * 0x0000000000200 - Rebooting nodes
 * 0x2000000000000 - Node powered down
 */
static void _set_sched_weight(bitstr_t *node_bitmap, bool future)
{
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		node_ptr->sched_weight = node_ptr->weight;
		node_ptr->sched_weight = node_ptr->sched_weight << 16;
		if (!future && IS_NODE_COMPLETING(node_ptr))
			node_ptr->sched_weight |= 0x100;
		if (IS_NODE_REBOOT_REQUESTED(node_ptr) ||
		    IS_NODE_REBOOT_ISSUED(node_ptr))
			node_ptr->sched_weight |= 0x200;
		if (IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_DOWN(node_ptr))
			node_ptr->sched_weight |= 0x2000000000000;
	}
}

static int _future_run_test(job_record_t *job_ptr, bitstr_t *node_bitmap,
			    uint32_t min_nodes, uint32_t max_nodes,
			    uint32_t req_nodes, uint16_t job_node_req,
			    list_t *preemptee_candidates,
			    list_t **preemptee_job_list,
			    resv_exc_t *resv_exc_ptr,
			    will_run_data_t *will_run_ptr,
			    bitstr_t *orig_map)
{
	part_res_record_t *future_part;
	node_use_record_t *future_usage;
	list_t *future_license_list;
	list_t *cr_job_list;
	list_itr_t *job_iterator;
	int rc = SLURM_ERROR;
	time_t now = time(NULL);
	uint16_t tmp_cr_type = _setup_cr_type(job_ptr);
	bool qos_preemptor = false;
	bitstr_t *efctv_bitmap_ptr, *efctv_bitmap = NULL;
	cr_job_list_args_t args;
	int time_window = 30;
	time_t end_time = 0;
	bool more_jobs = true;
	DEF_TIMERS;

	if (will_run_ptr && will_run_ptr->start)
		_set_sched_weight(node_bitmap, true);

	/*
	 * Job is still pending. Simulate termination of jobs one at a time
	 * to determine when and where the job can start.
	 */
	future_part = part_data_dup_res(select_part_record, orig_map);
	if (future_part == NULL) {
		return SLURM_ERROR;
	}
	future_usage = node_data_dup_use(select_node_usage, orig_map);
	if (future_usage == NULL) {
		part_data_destroy_res(future_part);
		return SLURM_ERROR;
	}

	future_license_list = license_copy(cluster_license_list);

	/* Build list of running and suspended jobs */
	cr_job_list = list_create(NULL);
	args = (cr_job_list_args_t) {
		.preemptee_candidates = preemptee_candidates,
		.cr_job_list = cr_job_list,
		.future_usage = future_usage,
		.future_part = future_part,
		.future_license_list = future_license_list,
		.job_license_list = job_ptr->license_list,
		.orig_map = orig_map,
		.qos_preemptor = &qos_preemptor,
		.start = will_run_ptr ? will_run_ptr->start : 0,
		.tmp_bitmap_pptr = &efctv_bitmap,
	};
	list_for_each(job_list, _build_cr_job_list, &args);

	/* Test with all preemptable jobs gone */
	if (preemptee_candidates || args.start) {
		bit_or(node_bitmap, orig_map);
		rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes,
			       req_nodes, SELECT_MODE_WILL_RUN, tmp_cr_type,
			       job_node_req, future_part, future_usage,
			       future_license_list, resv_exc_ptr, false,
			       qos_preemptor, true, NULL);
		if (rc == SLURM_SUCCESS) {
			/*
			 * Actual start time will actually be later than "now",
			 * but return "now" for backfill scheduler to
			 * initiate preemption.
			 */
			job_ptr->start_time = now;
			goto cleanup;
		}
	}

	/*
	 * Remove the running jobs from exp_node_cr and try scheduling the
	 * pending job after each one (or a few jobs that end close in time).
	 */
	list_sort(cr_job_list, _cr_job_list_sort);

	START_TIMER;
	job_iterator = list_iterator_create(cr_job_list);
	while (more_jobs) {
		job_record_t *last_job_ptr = NULL;
		job_record_t *next_job_ptr = NULL;
		int overlap, rm_job_cnt = 0;

		bit_or(node_bitmap, orig_map);
		while (true) {
			job_record_t *tmp_job_ptr = list_next(job_iterator);

			if (!tmp_job_ptr ||
			    (will_run_ptr && will_run_ptr->end &&
			     tmp_job_ptr->end_time > will_run_ptr->end)) {
				more_jobs = false;
				break;
			}
			efctv_bitmap_ptr = _select_topo_bitmap(
						tmp_job_ptr,
						node_bitmap,
						&efctv_bitmap);
			if (slurm_conf.debug_flags &
			    DEBUG_FLAG_SELECT_TYPE) {
				overlap = bit_overlap(efctv_bitmap_ptr,
						      tmp_job_ptr->
						      node_bitmap);
				info("%pJ: overlap=%d", tmp_job_ptr,
				      overlap);
			} else
				overlap = bit_overlap_any(
						efctv_bitmap_ptr,
						tmp_job_ptr->
						node_bitmap);
			if (overlap == 0 && /* job has no usable nodes */
			    !license_list_overlap(tmp_job_ptr->license_list,
						  job_ptr->license_list)) {
				continue;  /* skip it */
			}
			if (!end_time) {
				time_t delta = 0;

				/*
				 * align all time windows on a
				 * time_window barrier from the original
				 * first job evaluated, this prevents
				 * data in the running set from skewing
				 * changing the results between
				 * scheduling evaluations
				 */
				delta = tmp_job_ptr->end_time %
							time_window;
				end_time = tmp_job_ptr->end_time +
						(time_window - delta);
			}
			last_job_ptr = tmp_job_ptr;
			(void) job_res_rm_job(
				future_part, future_usage,
				future_license_list, tmp_job_ptr, 0,
				efctv_bitmap_ptr);
			next_job_ptr = list_peek_next(job_iterator);
			if (!next_job_ptr) {
				more_jobs = false;
				break;
			} else if (next_job_ptr->end_time >
				   (end_time + time_window)) {
				break;
			}
			if (rm_job_cnt++ > 200)
				goto timer_check;
		}
		if (!last_job_ptr)	/* Should never happen */
			break;

		rc = _job_test(job_ptr, node_bitmap, min_nodes,
			       max_nodes, req_nodes,
			       SELECT_MODE_WILL_RUN, tmp_cr_type,
			       job_node_req, future_part, future_usage,
			       future_license_list, resv_exc_ptr,
			       backfill_busy_nodes, qos_preemptor,
			       true, NULL);
		if (rc == SLURM_SUCCESS) {
			if (last_job_ptr->end_time <= now) {
				job_ptr->start_time =
					_guess_job_end(last_job_ptr,
						       now);
			} else {
				job_ptr->start_time =
					last_job_ptr->end_time;
			}
			break;
		}
		do {
			if (bf_window_scale)
				time_window += bf_window_scale;
			else
				time_window *= 2;
		} while (next_job_ptr && next_job_ptr->end_time >
			 (end_time + time_window));
timer_check:
		END_TIMER;
		if (DELTA_TIMER >= 2000000)
			break;	/* Quit after 2 seconds wall time */
	}
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE ||
	    ((job_ptr->bit_flags & BACKFILL_TEST) &&
	     slurm_conf.debug_flags & DEBUG_FLAG_BACKFILL)) {
		char time_str[25];
		/*
		 * When time_window gets large it could result in
		 * delaying jobs regardless of priority. Setting
		 * bf_window_linear could help mitigate this.
		 */
		verbose("%pJ considered resources from running jobs ending within %d seconds of %s",
			job_ptr, time_window, slurm_ctime2_r(&end_time,
			time_str));
	}

	list_iterator_destroy(job_iterator);
cleanup:
	FREE_NULL_BITMAP(efctv_bitmap);
	FREE_NULL_LIST(cr_job_list);
	part_data_destroy_res(future_part);
	node_data_destroy(future_usage);
	FREE_NULL_LIST(future_license_list);

	return rc;
}

/*
 * Determine where and when the job at job_ptr can begin execution by updating
 * a scratch cr_record structure to reflect each job terminating at the
 * end of its time limit and use this to show where and when the job at job_ptr
 * will begin execution. Used by Slurm's sched/backfill plugin.
 */
static int _will_run_test(job_record_t *job_ptr, bitstr_t *node_bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes, uint16_t job_node_req,
			  list_t *preemptee_candidates,
			  list_t **preemptee_job_list,
			  resv_exc_t *resv_exc_ptr,
			  will_run_data_t *will_run_ptr)
{
	list_itr_t *preemptee_iterator;
	int rc = SLURM_ERROR;
	time_t now = time(NULL);
	uint16_t tmp_cr_type = _setup_cr_type(job_ptr);
	bitstr_t *orig_map;

	orig_map = bit_copy(node_bitmap);

	if (will_run_ptr && will_run_ptr->start > now)
		goto test_future;

	_set_sched_weight(node_bitmap, false);

	/* Try to run with currently available nodes */
	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_WILL_RUN, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage,
		       cluster_license_list, resv_exc_ptr, false, false,
		       false, NULL);
	if (rc == SLURM_SUCCESS) {
		job_ptr->start_time = now;
		FREE_NULL_BITMAP(orig_map);
		return SLURM_SUCCESS;
	}

	/* Don't try preempting for licenses if not enabled */
	if ((rc == ESLURM_LICENSES_UNAVAILABLE) && !preempt_for_licenses)
		preemptee_candidates = NULL;

	if (!preemptee_candidates && (job_ptr->bit_flags & TEST_NOW_ONLY)) {
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}

test_future:
	/*
	 * Remove the running jobs from exp_node_cr and try scheduling the
	 * pending job after each one (or a few jobs that end close in time).
	 */
	if ((rc != SLURM_SUCCESS) &&
	    (((job_ptr->bit_flags & TEST_NOW_ONLY) == 0) ||
	     preemptee_candidates)) {
		rc = _future_run_test(job_ptr, node_bitmap,
				      min_nodes, max_nodes,
				      req_nodes, job_node_req,
				      preemptee_candidates,
				      preemptee_job_list,
				      resv_exc_ptr,
				      will_run_ptr,
				      orig_map);
	}

	if ((rc == SLURM_SUCCESS) && preemptee_job_list &&
	    preemptee_candidates) {
		job_record_t *tmp_job_ptr;
		bitstr_t *efctv_bitmap_ptr, *efctv_bitmap = NULL;
		/*
		 * Build list of preemptee jobs whose resources are
		 * actually used. list returned even if not killed
		 * in selected plugin, but by Moab or something else.
		 */
		if (*preemptee_job_list == NULL) {
			*preemptee_job_list = list_create(NULL);
		}
		preemptee_iterator = list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(preemptee_iterator))) {
			efctv_bitmap_ptr = _select_topo_bitmap(tmp_job_ptr,
							       node_bitmap,
							       &efctv_bitmap);
			if (!bit_overlap_any(efctv_bitmap_ptr,
					     tmp_job_ptr->node_bitmap))
				continue;
			list_append(*preemptee_job_list, tmp_job_ptr);
		}
		list_iterator_destroy(preemptee_iterator);
		FREE_NULL_BITMAP(efctv_bitmap);
	}

	FREE_NULL_BITMAP(orig_map);
	return rc;
}

/* Allocate resources for a job now, if possible */
static int _run_now(job_record_t *job_ptr, bitstr_t *node_bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    uint32_t req_nodes, uint16_t job_node_req,
		    list_t *preemptee_candidates, list_t **preemptee_job_list,
		    resv_exc_t *resv_exc_ptr)
{
	int rc;
	bitstr_t *orig_node_map = NULL, *save_node_map;
	job_record_t *tmp_job_ptr = NULL;
	list_itr_t *job_iterator, *preemptee_iterator;
	part_res_record_t *future_part;
	node_use_record_t *future_usage;
	list_t *future_license_list;
	bool remove_some_jobs = false;
	uint16_t pass_count = 0;
	uint16_t mode = NO_VAL16;
	uint16_t tmp_cr_type = _setup_cr_type(job_ptr);
	bool preempt_mode = false;

	save_node_map = bit_copy(node_bitmap);
top:	orig_node_map = bit_copy(save_node_map);

	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_RUN_NOW, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage,
		       cluster_license_list, resv_exc_ptr, false, false,
		       false, NULL);

	/* Don't try preempting for licenses if not enabled */
	if ((rc == ESLURM_LICENSES_UNAVAILABLE) && !preempt_for_licenses)
		preemptee_candidates = NULL;

	if ((rc != SLURM_SUCCESS) && preemptee_candidates && preempt_by_qos) {
		/* Determine QOS preempt mode of first job */
		job_iterator = list_iterator_create(preemptee_candidates);
		if ((tmp_job_ptr = list_next(job_iterator))) {
			mode = slurm_job_preempt_mode(tmp_job_ptr);
		}
		list_iterator_destroy(job_iterator);
	}
	if ((rc != SLURM_SUCCESS) && preemptee_candidates && preempt_by_qos &&
	    (mode == PREEMPT_MODE_SUSPEND) &&
	    (job_ptr->priority != 0)) {	/* Job can be held by bad allocate */
		list_t *preemptees_to_suspend_by_qos = list_create(NULL);

		future_part = part_data_dup_res(select_part_record,
						orig_node_map);
		if (future_part == NULL) {
			FREE_NULL_BITMAP(orig_node_map);
			FREE_NULL_BITMAP(save_node_map);
			return SLURM_ERROR;
		}
		future_usage = node_data_dup_use(select_node_usage,
						 orig_node_map);
		if (future_usage == NULL) {
			part_data_destroy_res(future_part);
			FREE_NULL_BITMAP(orig_node_map);
			FREE_NULL_BITMAP(save_node_map);
			return SLURM_ERROR;
		}
		future_license_list = license_copy(cluster_license_list);

		job_iterator = list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(job_iterator))) {
			int mode = slurm_job_preempt_mode(tmp_job_ptr);
			if (mode != PREEMPT_MODE_SUSPEND)
				continue;
			/*
			 * Remove resources used by tmp_job_ptr and check if
			 * the preemptor job can run.
			 */
			if (_job_res_rm_job(future_part, future_usage,
					    NULL, tmp_job_ptr,
					    JOB_RES_ACTION_RESUME,
					    orig_node_map))
				continue;
			list_append(preemptees_to_suspend_by_qos, tmp_job_ptr);
			bit_or(node_bitmap, orig_node_map);
			rc = _job_test(job_ptr, node_bitmap, min_nodes,
				       max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       tmp_cr_type, job_node_req,
				       future_part, future_usage,
				       future_license_list, resv_exc_ptr,
				       false, false, preempt_mode, NULL);

			if (rc != SLURM_SUCCESS)
				continue;

			/*
			 * We have identified the preemptee jobs that we need
			 * to suspend to run the preemptor job. Try to
			 * schedule it using extra row of core bitmap.
			 */
			bit_or(node_bitmap, orig_node_map);
			rc = _job_test(job_ptr, node_bitmap, min_nodes,
				       max_nodes, req_nodes,
				       SELECT_MODE_RUN_NOW, tmp_cr_type,
				       job_node_req, select_part_record,
				       select_node_usage, cluster_license_list,
				       resv_exc_ptr, false, true, preempt_mode,
				       preemptees_to_suspend_by_qos);
			FREE_NULL_LIST(preemptees_to_suspend_by_qos);

			FREE_NULL_BITMAP(orig_node_map);
			FREE_NULL_BITMAP(save_node_map);
			list_iterator_destroy(job_iterator);
			part_data_destroy_res(future_part);
			node_data_destroy(future_usage);
			FREE_NULL_LIST(future_license_list);

			return rc;
		}
		FREE_NULL_LIST(preemptees_to_suspend_by_qos);
	} else if ((rc != SLURM_SUCCESS) && preemptee_candidates) {
		int preemptee_cand_cnt = list_count(preemptee_candidates);
		/* Remove preemptable jobs from simulated environment */
		preempt_mode = true;
		future_part = part_data_dup_res(select_part_record,
						orig_node_map);
		if (future_part == NULL) {
			FREE_NULL_BITMAP(orig_node_map);
			FREE_NULL_BITMAP(save_node_map);
			return SLURM_ERROR;
		}
		future_usage = node_data_dup_use(select_node_usage,
						 orig_node_map);
		if (future_usage == NULL) {
			part_data_destroy_res(future_part);
			FREE_NULL_BITMAP(orig_node_map);
			FREE_NULL_BITMAP(save_node_map);
			return SLURM_ERROR;
		}

		future_license_list = license_copy(cluster_license_list);

		job_iterator = list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(job_iterator))) {
			mode = slurm_job_preempt_mode(tmp_job_ptr);
			if ((mode != PREEMPT_MODE_REQUEUE)    &&
			    (mode != PREEMPT_MODE_CANCEL))
				continue;	/* can't remove job */
			/* Remove preemptable job now */
			if(_job_res_rm_job(future_part, future_usage,
					   future_license_list, tmp_job_ptr, 0,
					   orig_node_map))
				continue;
			bit_or(node_bitmap, orig_node_map);
			rc = _job_test(job_ptr, node_bitmap, min_nodes,
				       max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       tmp_cr_type, job_node_req,
				       future_part, future_usage,
				       future_license_list, resv_exc_ptr,
				       false, false, preempt_mode, NULL);
			tmp_job_ptr->details->usable_nodes = 0;
			if (rc != SLURM_SUCCESS)
				continue;

			if ((pass_count++ > preempt_reorder_cnt) ||
			    (preemptee_cand_cnt <= pass_count)) {
				/*
				 * Ignore remaining jobs, but keep in the list
				 * since the code can get called multiple times
				 * for different node/feature sets --
				 * _get_req_features().
				 */
				while ((tmp_job_ptr = list_next(job_iterator))) {
					tmp_job_ptr->details->usable_nodes = 1;
				}
				break;
			}

			/*
			 * Reorder preemption candidates to minimize number
			 * of preempted jobs and their priorities.
			 */
			if (preempt_strict_order) {
				/*
				 * Move last preempted job to top of preemption
				 * candidate list, preserving order of other
				 * jobs.
				 */
				tmp_job_ptr = list_remove(job_iterator);
				list_prepend(preemptee_candidates, tmp_job_ptr);
			} else {
				/*
				 * Set the last job's usable count to a large
				 * value and re-sort preempted jobs. usable_nodes
				 * count set to zero above to eliminate values
				 * previously set to 99999. Note: usable_count
				 * is only used for sorting purposes.
				 */
				tmp_job_ptr->details->usable_nodes = 99999;
				list_iterator_reset(job_iterator);
				while ((tmp_job_ptr = list_next(job_iterator))) {
					if (tmp_job_ptr->details->usable_nodes
					    == 99999)
						break;
					tmp_job_ptr->details->usable_nodes =
						_get_usable_nodes(node_bitmap,
								  tmp_job_ptr);
				}
				while ((tmp_job_ptr = list_next(job_iterator))) {
					tmp_job_ptr->details->usable_nodes = 0;
				}
				list_sort(preemptee_candidates,
					  (ListCmpF)_sort_usable_nodes_dec);
			}
			FREE_NULL_BITMAP(orig_node_map);
			list_iterator_destroy(job_iterator);
			part_data_destroy_res(future_part);
			node_data_destroy(future_usage);
			FREE_NULL_LIST(future_license_list);
			goto top;
		}
		list_iterator_destroy(job_iterator);

		if ((rc == SLURM_SUCCESS) && preemptee_job_list &&
		    preemptee_candidates) {
			/*
			 * Build list of preemptee jobs whose resources are
			 * actually used
			 */
			if (*preemptee_job_list == NULL) {
				*preemptee_job_list = list_create(NULL);
			}
			preemptee_iterator = list_iterator_create(
				preemptee_candidates);
			while ((tmp_job_ptr = list_next(preemptee_iterator))) {
				mode = slurm_job_preempt_mode(tmp_job_ptr);
				if ((mode != PREEMPT_MODE_REQUEUE)    &&
				    (mode != PREEMPT_MODE_CANCEL))
					continue;
				if (!job_overlap_and_running(
					    node_bitmap,
					    job_ptr->licenses_to_preempt,
					    tmp_job_ptr))
					continue;
				if (tmp_job_ptr->details->usable_nodes)
					break;
				list_append(*preemptee_job_list,
					    tmp_job_ptr);
				remove_some_jobs = true;
			}
			list_iterator_destroy(preemptee_iterator);
			if (!remove_some_jobs) {
				FREE_NULL_LIST(*preemptee_job_list);
			}
		}

		part_data_destroy_res(future_part);
		node_data_destroy(future_usage);
		FREE_NULL_LIST(future_license_list);
	}
	FREE_NULL_BITMAP(orig_node_map);
	FREE_NULL_BITMAP(save_node_map);

	return rc;
}

static bool _check_ntasks_per_sock(uint16_t core, uint16_t socket,
				   uint16_t threads_per_core, uint16_t cps,
				   uint16_t *cpu_cnt, bitstr_t * core_map)
{
	if (!cpu_cnt[socket]) {	/* Start use of next socket */
		cpu_cnt[socket] = threads_per_core;
	} else {	/* Continued use of same socket */
		if (cpu_cnt[socket] >= cps) {
			/* do not allocate this core */
			bit_clear(core_map, core);
			return true;
		}
		cpu_cnt[socket] += threads_per_core;
	}
	return false;
}

static void _count_used_cpus(uint16_t threads_per_core, uint16_t cpus_per_task,
			     uint16_t ntasks_per_core, bool use_tpc,
			     int *remain_cpt, uint16_t *avail_cpus,
			     uint16_t *cpu_count)
{
	if (*avail_cpus >= threads_per_core) {
		int used;
		if (use_tpc) {
			used = threads_per_core;
		} else if ((ntasks_per_core == 1) &&
			   (cpus_per_task > threads_per_core)) {
			used = MIN(*remain_cpt, threads_per_core);
		} else
			used = threads_per_core;
		*avail_cpus -= used;
		*cpu_count  += used;
		if (*remain_cpt <= used)
			*remain_cpt = cpus_per_task;
		else
			*remain_cpt -= used;
	} else {
		*cpu_count += *avail_cpus;
		*avail_cpus = 0;
	}
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
 * RET resource availability structure, call _free_avail_res() to free
 */
static avail_res_t *_allocate_sc(job_record_t *job_ptr, bitstr_t *core_map,
				 bitstr_t *part_core_map, const uint32_t node_i,
				 int *cpu_alloc_size, bool entire_sockets_only,
				 bitstr_t *req_sock_map)
{
	uint16_t cpu_count = 0, part_cpu_limit = INFINITE16;
	uint16_t cps, avail_cpus = 0, num_tasks = 0;
	uint16_t req_sock_cpus = 0;
	uint32_t c;
	job_details_t *details_ptr = job_ptr->details;
	uint16_t cpus_per_task = details_ptr->cpus_per_task;
	uint16_t free_core_count = 0, spec_threads = 0;
	uint16_t i, j;
	node_record_t *node_ptr = node_record_table_ptr[node_i];
	uint16_t sockets = node_ptr->tot_sockets;
	uint16_t cores_per_socket = node_ptr->cores;
	uint16_t threads_per_core = node_ptr->tpc;
	uint16_t min_cores = 1, min_sockets = 1, ntasks_per_socket = 0;
	uint16_t ncpus_per_core = INFINITE16;	/* Usable CPUs per core */
	uint16_t ntasks_per_core = INFINITE16;
	uint32_t free_cpu_count = 0, used_cpu_count = 0;
	int tmp_cpt = 0; /* cpus_per_task */
	uint16_t free_cores[sockets];
	uint16_t used_cores[sockets];
	uint16_t cpu_cnt[sockets];
	uint16_t max_cpu_per_req_sock = INFINITE16;
	avail_res_t *avail_res = xmalloc(sizeof(avail_res_t));
	bitstr_t *tmp_core = NULL;
	bool use_tpc = false;
	uint32_t socket_begin;
	uint32_t socket_end;

	memset(free_cores, 0, sockets * sizeof(uint16_t));
	memset(used_cores, 0, sockets * sizeof(uint16_t));
	memset(cpu_cnt, 0, sockets * sizeof(uint16_t));

	if ((details_ptr->whole_node & WHOLE_NODE_REQUIRED) &&
	    entire_sockets_only && (details_ptr->core_spec != NO_VAL16)) {
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

		if ((ncpus_per_core != INFINITE16) &&
		    (ncpus_per_core > threads_per_core)) {
			goto fini;
		}
		threads_per_socket = threads_per_core * cores_per_socket;
		if ((ntasks_per_socket != INFINITE16) &&
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
	if (part_core_map) {
		tmp_core = bit_copy(part_core_map);
		bit_and_not(tmp_core, core_map);
	}

	socket_begin = 0;
	socket_end = cores_per_socket;
	for (i = 0; i < sockets; i++) {
		uint16_t used_cpus;

		free_cores[i] = bit_set_count_range(core_map, socket_begin,
						    socket_end);
		if (!tmp_core)
			used_cores[i] = (cores_per_socket - free_cores[i]);
		else
			used_cores[i] = bit_set_count_range(tmp_core,
							    socket_begin,
							    socket_end);
		used_cpus = used_cores[i] * threads_per_core;

		socket_begin = socket_end;
		socket_end += cores_per_socket;
		/*
		 * Socket CPUs restrictions:
		 * 1. Partially allocated socket, but entire_sockets_only:
		 *    Enabled when CR_SOCKET. This mode counts unusable CPUs as
		 *    allocated, so it also counts them against MaxCpusPerNode.
		 * 2. Partially allocated socket, up/beyond MaxCPUsPerSocket:
		 *    This mode does not count unusable CPUs as allocated, nor
		 *    against MaxCpusPerNode.
		 * 3. Free/partially allocated socket, MaxCPUsPerSocket enabled:
		 *    We can still use CPUs, but up to MaxCPUsPerSocket.
		 *    This mode does not count unusable CPUs as allocated, nor
		 *    against MaxCpusPerNode.
		 */
		if (entire_sockets_only && used_cpus) {
			used_cores[i] += free_cores[i];
			used_cpus = used_cores[i] * threads_per_core;
			free_cores[i] = 0;
		} else if (used_cpus >=
			   job_ptr->part_ptr->max_cpus_per_socket) {
			log_flag(SELECT_TYPE, "MaxCpusPerSocket: %u, CPUs already used on socket[%d]: %u - won't use the socket.",
				 job_ptr->part_ptr->max_cpus_per_socket,
				 i,
				 used_cpus);
			free_cores[i] = 0;
		} else if (job_ptr->part_ptr->max_cpus_per_socket != INFINITE) {
			free_cores[i] =
				MIN(free_cores[i],
				    (job_ptr->part_ptr->max_cpus_per_socket /
				     threads_per_core));
		}
		free_core_count += free_cores[i];
		used_cpu_count += used_cpus;
	}
	free_cpu_count = free_core_count * threads_per_core;
	avail_res->max_cpus = free_cpu_count;
	FREE_NULL_BITMAP(tmp_core);

	/* Enforce partition CPU limit, but do not pick specific cores yet */
	if ((job_ptr->part_ptr->max_cpus_per_node != INFINITE) &&
	    (free_cpu_count + used_cpu_count >
	     job_ptr->part_ptr->max_cpus_per_node)) {

		if (job_ptr->details->whole_node & WHOLE_NODE_REQUIRED) {
			log_flag(SELECT_TYPE, "Total cpu count greater than max_cpus_per_node on exclusive job. (%d > %d)",
				 free_cpu_count + used_cpu_count,
				 job_ptr->part_ptr->max_cpus_per_node);
			num_tasks = 0;
			goto fini;
		}

		if (used_cpu_count >= job_ptr->part_ptr->max_cpus_per_node) {
			/* no available CPUs on this node */
			num_tasks = 0;
			goto fini;
		}
		part_cpu_limit = job_ptr->part_ptr->max_cpus_per_node -
			used_cpu_count;
		if ((part_cpu_limit == 1) &&
		    (((ntasks_per_core != INFINITE16) &&
		      (ntasks_per_core > part_cpu_limit)) ||
		     ((ntasks_per_socket != INFINITE16) &&
		      (ntasks_per_socket > part_cpu_limit)) ||
		     ((ncpus_per_core != INFINITE16) &&
		      (ncpus_per_core > part_cpu_limit)) ||
		     (cpus_per_task > part_cpu_limit))) {
			/* insufficient available CPUs on this node */
			num_tasks = 0;
			goto fini;
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
	threads_per_core = job_mgr_determine_cpus_per_core(details_ptr, node_i);

	/* Are enough CPUs available compared with required ones? */
	if ((free_core_count * threads_per_core) < details_ptr->pn_min_cpus) {
		num_tasks = 0;
		goto fini;
	}

	for (i = 0; i < sockets; i++) {
		uint16_t tmp = free_cores[i] * threads_per_core;
		if (req_sock_map && bit_test(req_sock_map, i)) {
			if (tmp == 0) {
				/* no available resources on required socket */
				num_tasks = 0;
				goto fini;
			}
			req_sock_cpus += tmp;
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
	 * Don't min num_tasks if cpus_per_tres given, since number of
	 * CPUs in that case is not determined by tasks.
	 */
	if (details_ptr->ntasks_per_node && details_ptr->share_res &&
	    !job_ptr->cpus_per_tres)
		num_tasks = MIN(num_tasks, details_ptr->ntasks_per_node);

	/*
	 * If the job requests gres, then do not limit avail_cpus here.
	 * avail_cpus will be limited later by gres_select_filter_sock_core.
	 */
	if (!job_ptr->gres_list_req) {
		if (cpus_per_task < 2) {
			avail_cpus = num_tasks;
		} else if ((ntasks_per_core == 1) &&
			   (cpus_per_task > threads_per_core)) {
			/* find out how many cores a task will use */
			int task_cores =
				(cpus_per_task + threads_per_core - 1) /
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
	}

	/*
	 * If there's an auto adjustment then use the max between the required
	 * CPUs according to required task number, or to the autoadjustment.
	 */
	if (details_ptr->pn_min_cpus > details_ptr->orig_pn_min_cpus) {
		avail_cpus = MAX(details_ptr->pn_min_cpus, avail_cpus);
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
	if ((ntasks_per_socket != INFINITE16) &&
	    (ntasks_per_socket >= 1)) {
		cps = ntasks_per_socket;
		if (cpus_per_task > 1)
			cps *= cpus_per_task;
	} else
		cps = cores_per_socket * threads_per_core;

	tmp_cpt = cpus_per_task;
	if (req_sock_map && (i = bit_set_count(req_sock_map))) {
		tmp_core = bit_alloc(bit_size(core_map));
		if (req_sock_cpus > avail_cpus) {
			max_cpu_per_req_sock = avail_cpus / i;
		}
		i = 0;
	} else {
		i = sockets;
	}

	if ((slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE) &&
	    (details_ptr->min_gres_cpu > 0)) {
		use_tpc = true;
	}
	for ( ; ((i < sockets) && (avail_cpus > 0)); i++) {
		if (bit_test(req_sock_map, i)) {
			for (j = 0; j < cores_per_socket &&
				    free_cores[i]; j++) {
				c = (i * cores_per_socket) + j;
				if (!bit_test(core_map, c))
					continue;
				/*
				 * this socket has free cores, but make sure we don't
				 * use more than are needed for ntasks_per_socket
				 */
				if (_check_ntasks_per_sock(c, i,
							   threads_per_core,
							   cps, cpu_cnt,
							   core_map)) {
					continue;
				}
				free_cores[i]--;
				/*
				 * we have to ensure that cpu_count is not bigger than
				 * avail_cpus due to hyperthreading or this would break
				 * the selection logic providing more CPUs than allowed
				 * after task-related data processing of stage 3
				 */
				_count_used_cpus(threads_per_core,
						 cpus_per_task, ntasks_per_core,
						 use_tpc, &tmp_cpt, &avail_cpus,
						 &cpu_count);

				bit_set(tmp_core, c);
				if (cpu_cnt[i] > max_cpu_per_req_sock)
					break;
			}
		}
	}
	for (c = 0; c < node_ptr->tot_cores; c++) {
		if (!bit_test(core_map, c) || (tmp_core &&
					       bit_test(tmp_core, c)))
			continue;

		/* Socket index */
		i = (uint16_t) (c / cores_per_socket);
		if (free_cores[i] > 0 && (avail_cpus > 0)) {
			/*
			 * this socket has free cores, but make sure we don't
			 * use more than are needed for ntasks_per_socket
			 */
			if (_check_ntasks_per_sock(c, i, threads_per_core, cps,
						   cpu_cnt, core_map))
				continue;

			free_cores[i]--;
			/*
			 * we have to ensure that cpu_count is not bigger than
			 * avail_cpus due to hyperthreading or this would break
			 * the selection logic providing more CPUs than allowed
			 * after task-related data processing of stage 3
			 */
			_count_used_cpus(threads_per_core, cpus_per_task,
				         ntasks_per_core, use_tpc, &tmp_cpt,
					 &avail_cpus, &cpu_count);
		} else
			bit_clear(core_map, c);
	}

fini:
	/* if num_tasks == 0 then clear all bits on this node */
	if (num_tasks == 0) {
		bit_clear_all(core_map);
		cpu_count = 0;
	}

	if ((details_ptr->core_spec != NO_VAL16) &&
	    (details_ptr->core_spec & CORE_SPEC_THREAD) &&
	    ((node_ptr->threads == 1) ||
	     (node_ptr->threads == node_ptr->tpc))) {
		/*
		 * NOTE: Currently does not support the situation when Slurm
		 * allocates by core, the thread specialization count occupies
		 * a full core
		 */
		c = details_ptr->core_spec & (~CORE_SPEC_THREAD);
		if (((cpu_count + c) <= node_ptr->cpus))
			;
		else if (cpu_count > c)
			spec_threads = c;
		else
			spec_threads = cpu_count;
	}
	cpu_count -= spec_threads;

	avail_res->avail_cpus = MIN(cpu_count, part_cpu_limit);

	avail_res->min_cpus = *cpu_alloc_size;
	avail_res->avail_cores_per_sock = xcalloc(sockets, sizeof(uint16_t));
	socket_begin = 0;
	socket_end = cores_per_socket;
	for (i = 0; i < sockets; i++) {
		avail_res->avail_cores_per_sock[i] =
			bit_set_count_range(core_map, socket_begin, socket_end);
		socket_begin = socket_end;
		socket_end += cores_per_socket;
	}
	avail_res->sock_cnt = sockets;
	avail_res->spec_threads = spec_threads;
	avail_res->tpc = node_ptr->tpc;

	FREE_NULL_BITMAP(tmp_core);

	return avail_res;
}

/*
 * _allocate - Given the job requirements, determine which resources
 *             from the given node can be allocated (if any) to this
 *             job. Returns the number of cpus that can be used by
 *             this node AND a bitmap of the selected cores|sockets.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN req_sock_map - OPTIONAL bitmap of required sockets
 * IN cr_type - Consumable Resource setting
 * RET resource availability structure, call _free_avail_res() to free
 */
static avail_res_t *_allocate(job_record_t *job_ptr,
			      bitstr_t *core_map,
			      bitstr_t *part_core_map,
			      const uint32_t node_i,
			      int *cpu_alloc_size,
			      bitstr_t *req_sock_map,
			      uint16_t cr_type)
{
	bool alloc_sockets;

	if (cr_type & CR_SOCKET) {
		/* cpu_alloc_size = CPUs per socket */
		alloc_sockets = true;
		*cpu_alloc_size = node_record_table_ptr[node_i]->cores *
			node_record_table_ptr[node_i]->tpc;
	} else {
		/* cpu_alloc_size = # of CPUs per core || 1 individual CPU */
		alloc_sockets = false;
		*cpu_alloc_size = (cr_type & CR_CORE) ?
			node_record_table_ptr[node_i]->tpc : 1;
	}

	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, alloc_sockets, req_sock_map);
}

/*
 * job_test - Given a specification of scheduling requirements,
 *	identify the nodes which "best" satisfy the request.
 *	"best" is defined as either a minimal number of consecutive nodes
 *	or if sharing resources then sharing them with a job of similar size.
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN mode - SELECT_MODE_RUN_NOW   (0): try to schedule job now
 *           SELECT_MODE_TEST_ONLY (1): test if job can ever run
 *           SELECT_MODE_WILL_RUN  (2): determine when and where job can run
 * IN preemptee_candidates - list of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN resv_exc_ptr - Various TRES which the job can NOT use.
 * IN will_run_ptr - Pointer to data specific to WILL_RUN mode
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init):
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_cpus: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of req_nodes at the time that
 *	select_p_job_test is called
 */
extern int job_test(job_record_t *job_ptr, bitstr_t *node_bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    uint32_t req_nodes, uint16_t mode,
		    list_t *preemptee_candidates,
		    list_t **preemptee_job_list,
		    resv_exc_t *resv_exc_ptr,
		    will_run_data_t *will_run_ptr)
{
	int rc = EINVAL;
	uint16_t job_node_req;

	if (!(slurm_conf.conf_flags & CONF_FLAG_ASRU))
		job_ptr->details->core_spec = NO_VAL16;
	if ((job_ptr->details->core_spec != NO_VAL16) &&
	    !(job_ptr->details->whole_node & WHOLE_NODE_REQUIRED)) {
		info("Setting Exclusive mode for %pJ with CoreSpec=%u",
		     job_ptr,
		     job_ptr->details->core_spec);
		job_ptr->details->whole_node |= WHOLE_NODE_REQUIRED;
	}

	if (!job_ptr->details->mc_ptr)
		job_ptr->details->mc_ptr = _create_default_mc();
	job_node_req = _get_job_node_req(job_ptr);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *node_mode = "Unknown", *alloc_mode = "Unknown";
		if (job_node_req == NODE_CR_RESERVED)
			node_mode = "Exclusive";
		else if (job_node_req == NODE_CR_AVAILABLE)
			node_mode = "OverCommit";
		else if (job_node_req == NODE_CR_ONE_ROW)
			node_mode = "Normal";
		if (mode == SELECT_MODE_WILL_RUN)
			alloc_mode = "Will_Run";
		else if (mode == SELECT_MODE_TEST_ONLY)
			alloc_mode = "Test_Only";
		else if (mode == SELECT_MODE_RUN_NOW)
			alloc_mode = "Run_Now";
		verbose("%pJ node_mode:%s alloc_mode:%s",
			job_ptr, node_mode, alloc_mode);

		core_array_log("node_list & exc_cores",
			       node_bitmap, resv_exc_ptr->exc_cores);

		verbose("nodes: min:%u max:%u requested:%u avail:%u",
			min_nodes, max_nodes, req_nodes,
			bit_set_count(node_bitmap));
		node_data_dump();
	}

	if (mode == SELECT_MODE_WILL_RUN) {
		rc = _will_run_test(job_ptr, node_bitmap, min_nodes,
				    max_nodes,
				    req_nodes, job_node_req,
				    preemptee_candidates,
				    preemptee_job_list,
				    resv_exc_ptr,
				    will_run_ptr);
	} else if (mode == SELECT_MODE_TEST_ONLY) {
		rc = _test_only(job_ptr, node_bitmap, min_nodes,
				max_nodes, req_nodes, job_node_req);
	} else if (mode == SELECT_MODE_RUN_NOW) {
		rc = _run_now(job_ptr, node_bitmap, min_nodes, max_nodes,
			      req_nodes, job_node_req,
			      preemptee_candidates,
			      preemptee_job_list, resv_exc_ptr);
	} else {
		/* Should never get here */
		error("Mode %d is invalid",
		      mode);
		return EINVAL;
	}

	FREE_NULL_LIST(job_ptr->licenses_to_preempt);

	if ((slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND) ||
	    (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)) {
		if (job_ptr->job_resrcs) {
			verbose("Test returned:%s", slurm_strerror(rc));
			log_job_resources(job_ptr);
			gres_job_state_log(job_ptr->gres_list_req,
					   job_ptr->job_id);
		} else {
			verbose("no job_resources info for %pJ rc=%d",
				job_ptr, rc);
		}
	}

	return rc;
}
