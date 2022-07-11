/*****************************************************************************\
 *  job_test.c - functions to test job on resources
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

#include "cons_common.h"
#include "dist_tasks.h"
#include "gres_select_filter.h"
#include "gres_select_util.h"

#include "src/common/select.h"
#include "src/common/xstring.h"

#include "src/slurmctld/gres_ctld.h"
#include "src/slurmctld/preempt.h"

typedef struct {
	int action;
	bool job_fini;
	bitstr_t *node_map;
	node_use_record_t *node_usage;
	part_res_record_t *part_record_ptr;
	int rc;
} wrapper_rm_job_args_t;

typedef struct {
	List preemptee_candidates;
	List cr_job_list;
	node_use_record_t *future_usage;
	part_res_record_t *future_part;
	bitstr_t *orig_map;
	bool *qos_preemptor;
} cr_job_list_args_t;

uint64_t def_cpu_per_gpu = 0;
uint64_t def_mem_per_gpu = 0;
bool preempt_strict_order = false;
int preempt_reorder_cnt	= 1;

/* When any cores on a node are removed from being available for a job,
 * then remove the entire node from being available. */
static void _block_whole_nodes(bitstr_t *node_bitmap,
			       bitstr_t **orig_core_bitmap,
			       bitstr_t **new_core_bitmap)
{
	int first_node, last_node, i_node;
	int first_core, last_core, i_core;

	bitstr_t *cr_orig_core_bitmap = NULL;
	bitstr_t *cr_new_core_bitmap = NULL;

	first_node = bit_ffs(node_bitmap);
	if (first_node >= 0)
		last_node = bit_fls(node_bitmap);
	else
		last_node = -2;

	if (!is_cons_tres) {
		cr_orig_core_bitmap = *orig_core_bitmap;
		cr_new_core_bitmap = *new_core_bitmap;
	}

	for (i_node = first_node; i_node <= last_node; i_node++) {
		if (!bit_test(node_bitmap, i_node))
			continue;
		if (is_cons_tres) {
			first_core = 0;
			last_core = node_record_table_ptr[i_node]->tot_cores;
			cr_orig_core_bitmap = orig_core_bitmap[i_node];
			cr_new_core_bitmap = new_core_bitmap[i_node];
		} else {
			first_core = cr_get_coremap_offset(i_node);
			last_core  = cr_get_coremap_offset(i_node + 1);
		}

		for (i_core = first_core; i_core < last_core; i_core++) {
			if (bit_test(cr_orig_core_bitmap, i_core) &&
			    !bit_test(cr_new_core_bitmap, i_core)) {
				bit_clear(node_bitmap, i_node);
				break;
			}
		}
	}
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

/* List sort function: sort by the job's expected end time */
static int _cr_job_list_sort(void *x, void *y)
{
	job_record_t *job1_ptr = *(job_record_t **) x;
	job_record_t *job2_ptr = *(job_record_t **) y;

	return (int) SLURM_DIFFTIME(job1_ptr->end_time, job2_ptr->end_time);
}

static int _find_job (void *x, void *key)
{
	job_record_t *job_ptr = (job_record_t *) x;
	if (job_ptr == (job_record_t *) key)
		return 1;
	return 0;
}

extern void _free_avail_res_array(avail_res_t **avail_res_array)
{
	int n;
	if (!avail_res_array)
		return;

	for (n = 0; n < node_record_count; n++)
		common_free_avail_res(avail_res_array[n]);
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

	xassert(is_cons_tres);
	if (!job_ptr->gres_list_req)
		return;

	if (job_ptr->part_ptr != last_part_ptr) {
		/* Cache data from last partition referenced */
		last_part_ptr = job_ptr->part_ptr;
		last_cpu_per_gpu = common_get_def_cpu_per_gpu(
			last_part_ptr->job_defaults_list);
		last_mem_per_gpu = common_get_def_mem_per_gpu(
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

/* Determine how many sockets per node this job requires for GRES */
static uint32_t _socks_per_node(job_record_t *job_ptr)
{
	multi_core_data_t *mc_ptr;
	uint32_t s_p_n = NO_VAL;
	uint32_t cpu_cnt, cpus_per_node, tasks_per_node;
	uint32_t min_nodes;

	if (!job_ptr->details)
		return s_p_n;

	/*
	 * FIXME: This was removed in cons_tres commit e82b9f17a23adf0, I am
	 * wondering if it is actually needed in cons_res.
	 */
	if (!is_cons_tres && ((job_ptr->gres_list_req == NULL) ||
			      ((job_ptr->bit_flags & GRES_ENFORCE_BIND) == 0)))
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

	/*
	 * This logic could be expanded to support additional cases, which may
	 * require information per node information (e.g. threads per core).
	 */

	return s_p_n;
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
 *
 * RET array of avail_res_t pointers, free using _free_avail_res_array()
 */
static avail_res_t **_get_res_avail(job_record_t *job_ptr,
				    bitstr_t *node_map, bitstr_t **core_map,
				    node_use_record_t *node_usage,
				    uint16_t cr_type, bool test_only,
				    bool will_run, bitstr_t **part_core_map)
{
	int i, i_first, i_last;
	avail_res_t **avail_res_array = NULL;
	uint32_t s_p_n = _socks_per_node(job_ptr);

	xassert(*cons_common_callbacks.can_job_run_on_node);

	avail_res_array = xcalloc(node_record_count, sizeof(avail_res_t *));
	i_first = bit_ffs(node_map);
	if (i_first != -1)
		i_last = bit_fls(node_map);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (bit_test(node_map, i))
			avail_res_array[i] =
				(*cons_common_callbacks.can_job_run_on_node)(
					job_ptr, core_map, i,
					s_p_n, node_usage,
					cr_type, test_only, will_run,
					part_core_map);
		/*
		 * FIXME: This is a hack to make cons_res more bullet proof as
		 * there are places that don't always behave correctly with a
		 * sparce array.
		 */
		if (!is_cons_tres && !avail_res_array[i])
			avail_res_array[i] = xmalloc(sizeof(avail_res_t));
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
			 int sharing_only, part_record_t *my_part_ptr,
			 bool qos_preemptor)
{
	uint32_t r, c, core_begin, core_end;
	uint16_t num_rows;
	bitstr_t *use_row_bitmap = NULL;

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

			if (is_cons_tres) {
				if (!p_ptr->row[r].row_bitmap[node_i])
					continue;
				use_row_bitmap =
					p_ptr->row[r].row_bitmap[node_i];
				core_begin = 0;
				core_end = bit_size(
					p_ptr->row[r].row_bitmap[node_i]);
			} else {
				if (!*p_ptr->row[r].row_bitmap)
					continue;
				use_row_bitmap = *p_ptr->row[r].row_bitmap;
				core_begin = cr_get_coremap_offset(node_i);
				core_end = cr_get_coremap_offset(node_i+1);
			}

			for (c = core_begin; c < core_end; c++)
				if (bit_test(use_row_bitmap, c))
					return 1;
		}
	}
	return 0;
}

static bool _is_preemptable(job_record_t *job_ptr, List preemptee_candidates)
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
 * RET: array of avail_res_t pointers, free using _free_avail_res_array().
 *	NULL on error
 */
static avail_res_t **_select_nodes(job_record_t *job_ptr, uint32_t min_nodes,
				   uint32_t max_nodes, uint32_t req_nodes,
				   bitstr_t *node_bitmap, bitstr_t **avail_core,
				   node_use_record_t *node_usage,
				   uint16_t cr_type, bool test_only,
				   bool will_run,
				   bitstr_t **part_core_map,
				   bool prefer_alloc_nodes,
				   gres_mc_data_t *tres_mc_ptr)
{
	int i, rc;
	uint32_t n;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	avail_res_t **avail_res_array;

	xassert(*cons_common_callbacks.choose_nodes);

	if (bit_set_count(node_bitmap) < min_nodes) {
#if _DEBUG
		info("AvailNodes < MinNodes (%u < %u)",
		     bit_set_count(node_bitmap), min_nodes);
#endif
		return NULL;
	}

	core_array_log("_select_nodes/enter", node_bitmap, avail_core);
	/* Determine resource availability on each node for pending job */
	avail_res_array = _get_res_avail(job_ptr, node_bitmap, avail_core,
					 node_usage, cr_type, test_only,
					 will_run, part_core_map);
	if (!avail_res_array)
		return avail_res_array;

	/* Eliminate nodes that don't have sufficient resources for this job */
	for (n = 0; n < node_record_count; n++) {
		if (bit_test(node_bitmap, n) &&
		    (!avail_res_array[n] ||
		     !avail_res_array[n]->avail_cpus)) {
			/* insufficient resources available on this node */
			bit_clear(node_bitmap, n);
		}
	}
	if ((bit_set_count(node_bitmap) < min_nodes) ||
	    (req_map && !bit_super_set(req_map, node_bitmap))) {
		rc = SLURM_ERROR;
		goto fini;
	}
	core_array_log("_select_nodes/elim_nodes", node_bitmap, avail_core);

	/* Select the best nodes for this job */
	if (details_ptr->ntasks_per_node && details_ptr->num_tasks) {
		i  = details_ptr->num_tasks;
		i += (details_ptr->ntasks_per_node - 1);
		i /= details_ptr->ntasks_per_node;
		min_nodes = MAX(min_nodes, i);
	}
	rc = (*cons_common_callbacks.choose_nodes)(
		job_ptr, node_bitmap, avail_core, min_nodes,
		max_nodes, req_nodes, avail_res_array, cr_type,
		prefer_alloc_nodes, tres_mc_ptr);
	if (rc != SLURM_SUCCESS)
		goto fini;

	core_array_log("_select_nodes/choose_nodes", node_bitmap, avail_core);

	/* If successful, sync up the avail_core with the node_map */
	if (rc == SLURM_SUCCESS) {
		int i_first, i_last, n, start;

		i_first = bit_ffs(node_bitmap);
		if (i_first != -1)
			i_last = bit_fls(node_bitmap);
		else
			i_last = -2;

		if (is_cons_tres) {
			for (n = i_first; n < i_last; n++) {
				if (!avail_res_array[n] ||
				    !bit_test(node_bitmap, n))
					FREE_NULL_BITMAP(avail_core[n]);
			}
		} else if (i_last != -2) {
			start = 0;
			for (n = i_first; n < i_last; n++) {
				if (!avail_res_array[n] ||
				    !bit_test(node_bitmap, n))
					continue;
				if (cr_get_coremap_offset(n) != start)
					bit_nclear(
						*avail_core, start,
						(cr_get_coremap_offset(n)) - 1);
				start = cr_get_coremap_offset(n + 1);
			}
			if (cr_get_coremap_offset(n) != start)
				bit_nclear(*avail_core, start,
					   cr_get_coremap_offset(n) - 1);
		}
	}
	core_array_log("_select_nodes/sync_cores", node_bitmap, avail_core);

fini:	if (rc != SLURM_SUCCESS) {
		_free_avail_res_array(avail_res_array);
		return NULL;
	}

	return avail_res_array;
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
			      bitstr_t **exc_cores, bool qos_preemptor)
{
	node_record_t *node_ptr;
	uint32_t gres_cpus, gres_cores;
	uint64_t free_mem, min_mem, avail_mem;
	List gres_list;
	int i, i_first, i_last;
	bool disable_binding = false;

	if (is_cons_tres && !(job_ptr->bit_flags & JOB_MEM_SET) &&
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

	if (!is_cons_tres && (job_ptr->bit_flags & GRES_DISABLE_BIND))
		disable_binding = true;
	i_first = bit_ffs(node_bitmap);
	if (i_first == -1)
		i_last = -2;
	else
		i_last  = bit_fls(node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr[i];
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
		if ((job_ptr->details->whole_node == 1) && exc_cores) {
			if (is_cons_tres) {
				if (exc_cores[i] &&
				    (bit_ffs(exc_cores[i]) != -1)) {
					debug3("node %s exclusive",
					       node_ptr->name);
					goto clear_bit;
				}
			} else if (*exc_cores) {
				for (int j = cr_get_coremap_offset(i);
				     j < cr_get_coremap_offset(i+1);
				     j++) {
					if (bit_test(*exc_cores, j))
						continue;
					debug3("_vns: node %s exc",
					       node_ptr->name);
					goto clear_bit;
				}
			}
		}

		/* node-level GRES check, assumes all cores usable */
		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;

		if ((job_ptr->details->whole_node == WHOLE_NODE_REQUIRED) &&
		    gres_node_state_list_has_alloc_gres(gres_list)) {
			debug3("node %s has GRES in use (whole node requested)",
			       node_ptr->name);
			goto clear_bit;
		}

		gres_cores = gres_job_test(job_ptr->gres_list_req,
					   gres_list, true,
					   NULL, 0, 0, job_ptr->job_id,
					   node_ptr->name,
					   disable_binding);
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
				debug3("node %s non-sharing",
				       node_ptr->name);
				goto clear_bit;
			}
			/*
			 * cannot use this node if it is running jobs
			 * in sharing partitions
			 */
			if (_is_node_busy(cr_part_ptr, i, 1,
					  job_ptr->part_ptr, qos_preemptor)) {
				debug3("node %s sharing?",
				       node_ptr->name);
				goto clear_bit;
			}

			/* node is NODE_CR_AVAILABLE - check job request */
		} else {
			if (job_node_req == NODE_CR_RESERVED) {
				if (_is_node_busy(cr_part_ptr, i, 0,
						  job_ptr->part_ptr,
						  qos_preemptor)) {
					debug3("node %s busy",
					       node_ptr->name);
					goto clear_bit;
				}
			} else if (job_node_req == NODE_CR_ONE_ROW) {
				/*
				 * cannot use this node if it is running jobs
				 * in sharing partitions
				 */
				if (_is_node_busy(cr_part_ptr, i, 1,
						  job_ptr->part_ptr,
						  qos_preemptor)) {
					debug3("node %s vbusy",
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
		     node_use_record_t *node_usage,
		     bitstr_t **exc_cores, bool prefer_alloc_nodes,
		     bool qos_preemptor, bool preempt_mode)
{
	int error_code = SLURM_SUCCESS;
	bitstr_t *orig_node_map, **part_core_map = NULL;
	bitstr_t **free_cores_tmp = NULL,  *node_bitmap_tmp = NULL;
	bitstr_t **free_cores_tmp2 = NULL, *node_bitmap_tmp2 = NULL;
	bitstr_t **avail_cores, **free_cores;
	bool test_only = false, will_run = false;
	uint32_t sockets_per_node = 1;
	uint32_t c, j, n, c_alloc = 0, c_size, total_cpus;
	uint64_t save_mem = 0, avail_mem = 0, needed_mem = 0, lowest_mem = 0;
	int32_t build_cnt;
	job_resources_t *job_res;
	struct job_details *details_ptr = job_ptr->details;
	part_res_record_t *p_ptr, *jp_ptr;
	uint16_t *cpu_count;
	int i, i_first, i_last;
	avail_res_t **avail_res_array, **avail_res_array_tmp;
	gres_mc_data_t *tres_mc_ptr = NULL;
	List *node_gres_list = NULL, *sock_gres_list = NULL;
	uint32_t *gres_task_limit = NULL;
	char *nodename = NULL;
	bitstr_t *exc_core_bitmap = NULL;

	free_job_resources(&job_ptr->job_resrcs);

	if (mode == SELECT_MODE_TEST_ONLY)
		test_only = true;
	else if (mode == SELECT_MODE_WILL_RUN)
		will_run = true;

	/* check node_state and update the node_bitmap as necessary */
	if (!test_only) {
		error_code = _verify_node_state(
			cr_part_ptr, job_ptr, node_bitmap, cr_type,
			node_usage, job_node_req, exc_cores, qos_preemptor);
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

	if (is_cons_tres) {
		uint32_t ntasks_per_node = details_ptr->ntasks_per_node;
		ntasks_per_node = MAX(ntasks_per_node, 1);
		if (details_ptr->mc_ptr &&
		    details_ptr->mc_ptr->sockets_per_node)
			sockets_per_node =
				details_ptr->mc_ptr->sockets_per_node;
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
	} else if (exc_cores && *exc_cores)
		exc_core_bitmap = *exc_cores;

	log_flag(SELECT_TYPE, "evaluating %pJ on %u nodes",
	         job_ptr, bit_set_count(node_bitmap));

	orig_node_map = bit_copy(node_bitmap);
	avail_cores = common_mark_avail_cores(
		node_bitmap, job_ptr->details->core_spec);

	/*
	 * test to make sure that this job can succeed with all avail_cores
	 * if 'no' then return FAIL
	 * if 'yes' then we will seek the optimal placement for this job
	 *          within avail_cores
	 */
	free_cores = copy_core_array(avail_cores);
	if (is_cons_tres)
		tres_mc_ptr = _build_gres_mc_data(job_ptr);

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr);
	if (!avail_res_array) {
		/* job can not fit */
		xfree(tres_mc_ptr);
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		log_flag(SELECT_TYPE, "test 0 fail: insufficient resources");
		return SLURM_ERROR;
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
		return SLURM_ERROR;
	}
	if (cr_type == CR_MEMORY) {
		/*
		 * CR_MEMORY does not care about existing CPU allocations,
		 * so we can jump right to job allocation from here
		 */
		goto alloc_job;
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
	if (exc_core_bitmap && !is_cons_tres) {
		int exc_core_size  = bit_size(exc_core_bitmap);
		int free_core_size = bit_size(*free_cores);
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
			exc_cores = NULL;	/* Clear local value */
		}
	}

	if (exc_cores) {
#if _DEBUG
		core_array_log("exclude reserved cores", NULL, exc_cores);
#endif
		core_array_and_not(free_cores, exc_cores);
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
	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr);
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
	if (exc_cores)
		core_array_and_not(free_cores, exc_cores);

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

	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	/* make these changes permanent */
	free_core_array(&avail_cores);
	avail_cores = copy_core_array(free_cores);

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr);
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

	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	free_cores_tmp  = copy_core_array(free_cores);
	node_bitmap_tmp = bit_copy(node_bitmap);
	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr);
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
			if (job_ptr->details->whole_node == 1) {
				_block_whole_nodes(node_bitmap_tmp, avail_cores,
						   free_cores_tmp);
			}

			free_cores_tmp2  = copy_core_array(free_cores_tmp);
			node_bitmap_tmp2 = bit_copy(node_bitmap_tmp);
			avail_res_array_tmp = _select_nodes(
				job_ptr, min_nodes, max_nodes, req_nodes,
				node_bitmap_tmp, free_cores_tmp, node_usage,
				cr_type, test_only, will_run, part_core_map,
				prefer_alloc_nodes, tres_mc_ptr);
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
						tres_mc_ptr);
		if (avail_res_array)
			log_flag(SELECT_TYPE, "test 4 pass - first row found");
		goto alloc_job;
	}


	if ((jp_ptr->num_rows > 1) && !preempt_by_qos)
		part_data_sort_res(jp_ptr);	/* Preserve row order for QOS */
	c = jp_ptr->num_rows;
	if (preempt_by_qos && !qos_preemptor)
		c--;				/* Do not use extra row */
	if (preempt_by_qos && (job_node_req != NODE_CR_AVAILABLE))
		c = 1;
	for (i = 0; i < c; i++) {
		if (!jp_ptr->row[i].row_bitmap)
			break;
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		core_array_and_not(free_cores, jp_ptr->row[i].row_bitmap);
		bit_copybits(node_bitmap, orig_node_map);
		if (job_ptr->details->whole_node == 1)
			_block_whole_nodes(node_bitmap, avail_cores,free_cores);
		avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
						req_nodes, node_bitmap,
						free_cores, node_usage, cr_type,
						test_only, will_run,
						part_core_map,
						prefer_alloc_nodes,
						tres_mc_ptr);
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
						tres_mc_ptr);
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
		return SLURM_ERROR;
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
	i_first = bit_ffs(node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(node_bitmap);
	else
		i_last = -2;
	for (i = i_first, j = 0; i <= i_last; i++) {
		if (bit_test(node_bitmap, i) && avail_res_array[i])
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
	i_first = bit_ffs(node_bitmap);
	for (i = 0, n = i_first; n < node_record_count; n++) {
		int first_core, last_core;
		bitstr_t *use_free_cores = NULL;

		if (!bit_test(node_bitmap, n))
			continue;

		if (is_cons_tres) {
			first_core = 0;
			last_core = node_record_table_ptr[n]->tot_cores;
			use_free_cores = free_cores[n];
		} else {
			first_core = cr_get_coremap_offset(n);
			last_core = cr_get_coremap_offset(n + 1);
			use_free_cores = *free_cores;
		}
		for (j = first_core; j < last_core; j++, c++) {
			if (!bit_test(use_free_cores, j))
				continue;
			if (c >= c_size) {
				error("core_bitmap index error on node %s (NODE_INX:%d, C_SIZE:%u)",
				      node_record_table_ptr[n]->name,
				      n, c_size);
				drain_nodes(node_record_table_ptr[n]->name,
					    "Bad core count", getuid());
				_free_avail_res_array(avail_res_array);
				free_job_resources(&job_res);
				free_core_array(&free_cores);
				return SLURM_ERROR;
			}
			bit_set(job_res->core_bitmap, c);
			c_alloc++;
		}
		total_cpus += job_res->cpus[i];
		i++;
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
	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	if (is_cons_tres &&
	    job_ptr->gres_list_req && (error_code == SLURM_SUCCESS)) {
		node_record_t *node_ptr;
		bool have_gres_per_task, task_limit_set = false;

		/*
		 * Determine if any job gres_per_task specification here
		 * to avoid calling gres_get_task_limit unless needed
		 */
		have_gres_per_task = gres_select_util_job_tres_per_task(
			job_ptr->gres_list_req);
		if (have_gres_per_task) {
			gres_task_limit = xcalloc(job_res->nhosts,
						  sizeof(uint32_t));
		}
		node_gres_list = xcalloc(job_res->nhosts, sizeof(List));
		sock_gres_list = xcalloc(job_res->nhosts, sizeof(List));
		for (i = i_first, j = 0; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			if (have_gres_per_task) {
				gres_task_limit[j] =
					gres_select_util_get_task_limit(
						avail_res_array[i]->
						sock_gres_list);
				if (gres_task_limit[j] != NO_VAL)
					task_limit_set = true;
			}
			node_ptr = node_record_table_ptr[i];
			node_gres_list[j] = node_ptr->gres_list;
			sock_gres_list[j] =
				avail_res_array[i]->sock_gres_list;
			j++;
		}
		if (!task_limit_set)
			xfree(gres_task_limit);
	}
	error_code = dist_tasks(job_ptr, cr_type, preempt_mode,
				avail_cores, gres_task_limit);
	if (is_cons_tres &&
	    job_ptr->gres_list_req && (error_code == SLURM_SUCCESS)) {
		error_code = gres_select_filter_select_and_set(
			sock_gres_list,
			job_ptr->job_id, job_res,
			job_ptr->details->overcommit,
			tres_mc_ptr);
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
	if (job_ptr->details->whole_node == 1) {
		job_ptr->total_cpus = 0;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			/*
			 * This could make the job_res->cpus incorrect.
			 * Don't use job_res->cpus when allocating
			 * whole nodes as the job is finishing to
			 * subtract from the total cpu count or you
			 * will get an incorrect count.
			 */

			job_ptr->total_cpus +=
				node_record_table_ptr[i]->cpus_efctv;
		}
	} else if (cr_type & CR_SOCKET) {
		int ci = 0;
		int s, last_s, sock_cnt = 0;

		job_ptr->total_cpus = 0;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			sock_cnt = 0;
			for (s = 0; s < node_record_table_ptr[i]->tot_sockets;
			     s++) {
				last_s = -1;
				for (c = 0; c < node_record_table_ptr[i]->cores;
				     c++) {
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
			job_ptr->total_cpus +=
				(sock_cnt * node_record_table_ptr[i]->cores *
				 node_record_table_ptr[i]->tpc);
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

	if (is_cons_tres && !(job_ptr->bit_flags & JOB_MEM_SET) &&
	    gres_select_util_job_mem_set(job_ptr->gres_list_req, job_res)) {
		debug("%pJ memory set via GRES limit", job_ptr);
	} else {
		/* load memory allocated array */
		save_mem = details_ptr->pn_min_memory;
		for (i = i_first, j = 0; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			nodename = node_record_table_ptr[i]->name;
			avail_mem = node_record_table_ptr[i]->real_memory -
				    node_record_table_ptr[i]->mem_spec_limit;
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
		       select_part_record, select_node_usage, NULL, false,
		       false, false);
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
			     job_ptr, wargs->action, wargs->job_fini,
			     wargs->node_map);

	return 0;
}

static int _job_res_rm_job(part_res_record_t *part_record_ptr,
			   node_use_record_t *node_usage,
			   job_record_t *job_ptr, int action, bool job_fini,
			   bitstr_t *node_map)
{
	wrapper_rm_job_args_t wargs = {
		.action = action,
		.job_fini = job_fini,
		.node_usage = node_usage,
		.part_record_ptr = part_record_ptr,
		.node_map = node_map
	};

	if (!job_overlap_and_running(node_map, job_ptr))
		return 1;

	if (!job_ptr->het_job_list)
		(void)_wrapper_job_res_rm_job(job_ptr, &wargs);
	else
		(void)list_for_each(job_ptr->het_job_list,
				    _wrapper_job_res_rm_job,
				    &wargs);
	return 0;
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
	if (!_is_preemptable(job_ptr_preempt, args->preemptee_candidates)) {
		/* Queue job for later removal from data structures */
		list_append(args->cr_job_list, tmp_job_ptr);
	} else if (tmp_job_ptr == job_ptr_preempt) {
		uint16_t mode = slurm_job_preempt_mode(tmp_job_ptr);
		if (mode == PREEMPT_MODE_OFF)
			return 0;
		if (mode == PREEMPT_MODE_SUSPEND) {
			action = 2;	/* remove cores, keep memory */
			if (preempt_by_qos)
				*args->qos_preemptor = true;
		} else
			action = 0;	/* remove cores and memory */
		/* Remove preemptable job now */
		_job_res_rm_job(args->future_part, args->future_usage,
				tmp_job_ptr, action, false,
				args->orig_map);
	}
	return 0;
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
			  List preemptee_candidates,
			  List *preemptee_job_list,
			  bitstr_t **exc_core_bitmap)
{
	part_res_record_t *future_part;
	node_use_record_t *future_usage;
	job_record_t *tmp_job_ptr;
	List cr_job_list;
	ListIterator job_iterator, preemptee_iterator;
	bitstr_t *orig_map;
	int rc = SLURM_ERROR;
	time_t now = time(NULL);
	uint16_t tmp_cr_type = _setup_cr_type(job_ptr);
	bool qos_preemptor = false;
	cr_job_list_args_t args;

	orig_map = bit_copy(node_bitmap);

	/* Try to run with currently available nodes */
	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_WILL_RUN, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage, exc_core_bitmap,
		       false, false, false);
	if (rc == SLURM_SUCCESS) {
		FREE_NULL_BITMAP(orig_map);
		job_ptr->start_time = now;
		return SLURM_SUCCESS;
	}

	if (!preemptee_candidates && (job_ptr->bit_flags & TEST_NOW_ONLY)) {
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}

	/*
	 * Job is still pending. Simulate termination of jobs one at a time
	 * to determine when and where the job can start.
	 */
	future_part = part_data_dup_res(select_part_record, orig_map);
	if (future_part == NULL) {
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}
	future_usage = node_data_dup_use(select_node_usage, orig_map);
	if (future_usage == NULL) {
		part_data_destroy_res(future_part);
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}

	/* Build list of running and suspended jobs */
	cr_job_list = list_create(NULL);
	args = (cr_job_list_args_t) {
		.preemptee_candidates = preemptee_candidates,
		.cr_job_list = cr_job_list,
		.future_usage = future_usage,
		.future_part = future_part,
		.orig_map = orig_map,
		.qos_preemptor = &qos_preemptor,
	};
	list_for_each(job_list, _build_cr_job_list, &args);

	/* Test with all preemptable jobs gone */
	if (preemptee_candidates) {
		bit_or(node_bitmap, orig_map);
		rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes,
			       req_nodes, SELECT_MODE_WILL_RUN, tmp_cr_type,
			       job_node_req, future_part,
			       future_usage, exc_core_bitmap, false,
			       qos_preemptor, true);
		if (rc == SLURM_SUCCESS) {
			/*
			 * Actual start time will actually be later than "now",
			 * but return "now" for backfill scheduler to
			 * initiate preemption.
			 */
			job_ptr->start_time = now;
		}
	}

	/*
	 * Remove the running jobs from exp_node_cr and try scheduling the
	 * pending job after each one (or a few jobs that end close in time).
	 */
	if ((rc != SLURM_SUCCESS) &&
	    ((job_ptr->bit_flags & TEST_NOW_ONLY) == 0)) {
		int time_window = 30;
		time_t end_time = 0;
		bool more_jobs = true;
		DEF_TIMERS;
		list_sort(cr_job_list, _cr_job_list_sort);
		START_TIMER;
		job_iterator = list_iterator_create(cr_job_list);
		while (more_jobs) {
			job_record_t *last_job_ptr = NULL;
			job_record_t *next_job_ptr = NULL;
			int overlap, rm_job_cnt = 0;

			bit_or(node_bitmap, orig_map);
			while (true) {
				tmp_job_ptr = list_next(job_iterator);
				if (!tmp_job_ptr) {
					more_jobs = false;
					break;
				}
				if (slurm_conf.debug_flags &
				    DEBUG_FLAG_SELECT_TYPE) {
					overlap = bit_overlap(node_bitmap,
					                      tmp_job_ptr->
					                      node_bitmap);
					info("%pJ: overlap=%d", tmp_job_ptr,
					      overlap);
				} else
					overlap = bit_overlap_any(node_bitmap,
					                          tmp_job_ptr->
					                          node_bitmap);
				if (overlap == 0)  /* job has no usable nodes */
					continue;  /* skip it */
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
					tmp_job_ptr, 0, false, orig_map);
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
			do {
				if (bf_window_scale)
					time_window += bf_window_scale;
				else
					time_window *= 2;
			} while (next_job_ptr && next_job_ptr->end_time >
				 (end_time + time_window));
			rc = _job_test(job_ptr, node_bitmap, min_nodes,
				       max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN, tmp_cr_type,
				       job_node_req, future_part, future_usage,
				       exc_core_bitmap, backfill_busy_nodes,
				       qos_preemptor, true);
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
timer_check:
			END_TIMER;
			if (DELTA_TIMER >= 2000000)
				break;	/* Quit after 2 seconds wall time */
		}
		list_iterator_destroy(job_iterator);
	}

	if ((rc == SLURM_SUCCESS) && preemptee_job_list &&
	    preemptee_candidates) {
		/*
		 * Build list of preemptee jobs whose resources are
		 * actually used. List returned even if not killed
		 * in selected plugin, but by Moab or something else.
		 */
		if (*preemptee_job_list == NULL) {
			*preemptee_job_list = list_create(NULL);
		}
		preemptee_iterator = list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(preemptee_iterator))) {
			if (!bit_overlap_any(node_bitmap,
					     tmp_job_ptr->node_bitmap))
				continue;
			list_append(*preemptee_job_list, tmp_job_ptr);
		}
		list_iterator_destroy(preemptee_iterator);
	}

	FREE_NULL_LIST(cr_job_list);
	part_data_destroy_res(future_part);
	node_data_destroy(future_usage);
	FREE_NULL_BITMAP(orig_map);

	return rc;
}

/* Allocate resources for a job now, if possible */
static int _run_now(job_record_t *job_ptr, bitstr_t *node_bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    uint32_t req_nodes, uint16_t job_node_req,
		    List preemptee_candidates, List *preemptee_job_list,
		    bitstr_t **exc_cores)
{
	int rc;
	bitstr_t *orig_node_map = NULL, *save_node_map;
	job_record_t *tmp_job_ptr = NULL;
	ListIterator job_iterator, preemptee_iterator;
	part_res_record_t *future_part;
	node_use_record_t *future_usage;
	bool remove_some_jobs = false;
	uint16_t pass_count = 0;
	uint16_t mode = NO_VAL16;
	uint16_t tmp_cr_type = _setup_cr_type(job_ptr);
	bool preempt_mode = false;

	save_node_map = bit_copy(node_bitmap);
top:	orig_node_map = bit_copy(save_node_map);

	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_RUN_NOW, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage, exc_cores, false,
		       false, preempt_mode);

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
		/* Try to schedule job using extra row of core bitmap */
		bit_or(node_bitmap, orig_node_map);
		rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes,
			       req_nodes, SELECT_MODE_RUN_NOW, tmp_cr_type,
			       job_node_req, select_part_record,
			       select_node_usage, exc_cores, false, true,
			       preempt_mode);
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

		job_iterator = list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(job_iterator))) {
			mode = slurm_job_preempt_mode(tmp_job_ptr);
			if ((mode != PREEMPT_MODE_REQUEUE)    &&
			    (mode != PREEMPT_MODE_CANCEL))
				continue;	/* can't remove job */
			/* Remove preemptable job now */
			if(_job_res_rm_job(future_part, future_usage,
					   tmp_job_ptr, 0, false,
					   orig_node_map))
				continue;
			bit_or(node_bitmap, orig_node_map);
			rc = _job_test(job_ptr, node_bitmap, min_nodes,
				       max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       tmp_cr_type, job_node_req,
				       future_part, future_usage, exc_cores,
				       false, false, preempt_mode);
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
					    node_bitmap, tmp_job_ptr))
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
	}
	FREE_NULL_BITMAP(orig_node_map);
	FREE_NULL_BITMAP(save_node_map);

	return rc;
}

/*
 * common_job_test - Given a specification of scheduling requirements,
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
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN exc_cores - Cores to be excluded for use (in advanced reservation)
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
extern int common_job_test(job_record_t *job_ptr, bitstr_t *node_bitmap,
			   uint32_t min_nodes, uint32_t max_nodes,
			   uint32_t req_nodes, uint16_t mode,
			   List preemptee_candidates,
			   List *preemptee_job_list,
			   bitstr_t **exc_cores)
{
	int rc = EINVAL;
	uint16_t job_node_req;

	if (!(slurm_conf.conf_flags & CTL_CONF_ASRU))
		job_ptr->details->core_spec = NO_VAL16;
	if ((job_ptr->details->core_spec != NO_VAL16) &&
	    (job_ptr->details->whole_node != 1)) {
		info("Setting Exclusive mode for %pJ with CoreSpec=%u",
		     job_ptr,
		     job_ptr->details->core_spec);
		job_ptr->details->whole_node = 1;
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

		core_array_log("node_list & exc_cores", node_bitmap, exc_cores);

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
				    exc_cores);
	} else if (mode == SELECT_MODE_TEST_ONLY) {
		rc = _test_only(job_ptr, node_bitmap, min_nodes,
				max_nodes, req_nodes, job_node_req);
	} else if (mode == SELECT_MODE_RUN_NOW) {
		rc = _run_now(job_ptr, node_bitmap, min_nodes, max_nodes,
			      req_nodes, job_node_req,
			      preemptee_candidates,
			      preemptee_job_list, exc_cores);
	} else {
		/* Should never get here */
		error("Mode %d is invalid",
		      mode);
		return EINVAL;
	}

	if ((slurm_conf.debug_flags & DEBUG_FLAG_CPU_BIND) ||
	    (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)) {
		if (job_ptr->job_resrcs) {
			verbose("Test returned:%s", slurm_strerror(rc));
			log_job_resources(job_ptr);
			if (is_cons_tres)
				gres_job_state_log(job_ptr->gres_list_req,
						   job_ptr->job_id);
		} else {
			verbose("no job_resources info for %pJ rc=%d",
				job_ptr, rc);
		}
	}

	return rc;
}
