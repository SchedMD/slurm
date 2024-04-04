/*****************************************************************************\
 *  job_test.c - Determine if job can be allocated resources.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
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

#include <math.h>
#include <string.h>
#include "select_cons_tres.h"
#include "dist_tasks.h"
#include "job_test.h"
#include "gres_select_filter.h"
#include "gres_select_util.h"
#include "gres_sched.h"

#include "src/slurmctld/licenses.h"

typedef struct avail_res {	/* Per-node resource availability */
	uint16_t avail_cpus;	/* Count of available CPUs for this job
				   limited by options like --ntasks-per-node */
	uint16_t avail_gpus;	/* Count of available GPUs */
	uint16_t avail_res_cnt;	/* Count of available CPUs + GPUs */
	uint16_t *avail_cores_per_sock;	/* Per-socket available core count */
	uint32_t gres_min_cpus; /* Minimum required cpus for gres */
	uint32_t gres_max_tasks; /* Maximum tasks for gres */
	uint16_t max_cpus;	/* Maximum available CPUs on the node */
	uint16_t min_cpus;	/* Minimum allocated CPUs */
	uint16_t sock_cnt;	/* Number of sockets on this node */
	List sock_gres_list;	/* Per-socket GRES availability, sock_gres_t */
	uint16_t spec_threads;	/* Specialized threads to be reserved */
	uint16_t tpc;		/* Threads/cpus per core */
} avail_res_t;

typedef struct node_weight_struct {
	bitstr_t *node_bitmap;	/* bitmap of nodes with this weight */
	uint64_t weight;	/* priority of node for scheduling work on */
} node_weight_type;

typedef struct topo_weight_info {
	bitstr_t *node_bitmap;
	int node_cnt;
	uint64_t weight;
} topo_weight_info_t;

typedef struct {
	int action;
	list_t *license_list;
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
	list_t *future_license_list;
	bitstr_t *orig_map;
	bool *qos_preemptor;
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
static List _build_node_weight_list(bitstr_t *node_bitmap);
static void _cpus_to_use(uint16_t *avail_cpus, int64_t rem_max_cpus,
			 int rem_nodes, job_details_t *details_ptr,
			 avail_res_t *avail_res, int node_inx,
			 uint16_t cr_type);
static bool _enough_nodes(int avail_nodes, int rem_nodes,
			  uint32_t min_nodes, uint32_t req_nodes);
static int _eval_nodes(job_record_t *job_ptr, gres_mc_data_t *mc_ptr,
		       bitstr_t *node_map, bitstr_t **avail_core,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, avail_res_t **avail_res_array,
		       uint16_t cr_type, bool prefer_alloc_nodes,
		       bool first_pass);
static int _eval_nodes_busy(job_record_t *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_consec(job_record_t *job_ptr,
			      gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			      bitstr_t **avail_core, uint32_t min_nodes,
			      uint32_t max_nodes, uint32_t req_nodes,
			      avail_res_t **avail_res_array, uint16_t cr_type,
			      bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_dfly(job_record_t *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_block(job_record_t *job_ptr,
			     gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			     bitstr_t **avail_core, uint32_t min_nodes,
			     uint32_t max_nodes, uint32_t req_nodes,
			     avail_res_t **avail_res_array, uint16_t cr_type,
			     bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_lln(job_record_t *job_ptr,
			   gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			   bitstr_t **avail_core, uint32_t min_nodes,
			   uint32_t max_nodes, uint32_t req_nodes,
			   avail_res_t **avail_res_array, uint16_t cr_type,
			   bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_serial(job_record_t *job_ptr,
			      gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			      bitstr_t **avail_core, uint32_t min_nodes,
			      uint32_t max_nodes, uint32_t req_nodes,
			      avail_res_t **avail_res_array, uint16_t cr_type,
			      bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_spread(job_record_t *job_ptr,
			      gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			      bitstr_t **avail_core, uint32_t min_nodes,
			      uint32_t max_nodes, uint32_t req_nodes,
			      avail_res_t **avail_res_array, uint16_t cr_type,
			      bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_topo(job_record_t *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass);
static int64_t _get_rem_max_cpus(job_details_t *details_ptr, int rem_nodes);
static int _node_weight_find(void *x, void *key);
static void _node_weight_free(void *x);
static int _node_weight_sort(void *x, void *y);
static avail_res_t *_allocate(job_record_t *job_ptr,
			      bitstr_t *core_map,
			      bitstr_t *part_core_map,
			      const uint32_t node_i,
			      int *cpu_alloc_size,
			      bitstr_t *req_sock_map,
			      uint16_t cr_type);

/* Find node_weight_type element from list with same weight as node config */
static int _node_weight_find(void *x, void *key)
{
	node_weight_type *nwt = (node_weight_type *) x;
	node_record_t *node_ptr = (node_record_t *) key;
	if (nwt->weight == node_ptr->sched_weight)
		return 1;
	return 0;
}

/* Free node_weight_type element from list */
static void _node_weight_free(void *x)
{
	node_weight_type *nwt = (node_weight_type *) x;
	FREE_NULL_BITMAP(nwt->node_bitmap);
	xfree(nwt);
}

/* Sort list of node_weight_type reords in order of increasing node weight */
static int _node_weight_sort(void *x, void *y)
{
	node_weight_type *nwt1 = *(node_weight_type **) x;
	node_weight_type *nwt2 = *(node_weight_type **) y;
	if (nwt1->weight < nwt2->weight)
		return -1;
	if (nwt1->weight > nwt2->weight)
		return 1;
	return 0;
}

/*
 * Given a bitmap of available nodes, return a list of node_weight_type
 * records in order of increasing "weight" (priority)
 */
static List _build_node_weight_list(bitstr_t *node_bitmap)
{
	List node_list;
	node_record_t *node_ptr;
	node_weight_type *nwt;

	xassert(node_bitmap);
	/* Build list of node_weight_type records, one per node weight */
	node_list = list_create(_node_weight_free);
	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		nwt = list_find_first(node_list, _node_weight_find, node_ptr);
		if (!nwt) {
			nwt = xmalloc(sizeof(node_weight_type));
			nwt->node_bitmap = bit_alloc(node_record_count);
			nwt->weight = node_ptr->sched_weight;
			list_append(node_list, nwt);
		}
		bit_set(nwt->node_bitmap, i);
	}

	/* Sort the list in order of increasing node weight */
	list_sort(node_list, _node_weight_sort);

	return node_list;
}

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

/*
 * Determine how many CPUs on the node can be used based upon the resource
 *	allocation unit (node, socket, core, etc.) and making sure that
 *	resources will be available for nodes considered later in the
 *	scheduling process
 * OUT avail_cpus - Count of CPUs to use on this node
 * IN rem_max_cpus - Maximum count of CPUs remaining to be allocated for job
 * IN rem_nodes - Count of nodes remaining to be allocated for job
 * IN details_ptr - Job details information
 * IN avail_res - Available resources for job on this node, contents updated
 * IN node_inx - Node index
 * IN cr_type - Resource allocation units (CR_CORE, CR_SOCKET, etc).
 */
static void _cpus_to_use(uint16_t *avail_cpus, int64_t rem_max_cpus,
			 int rem_nodes, job_details_t *details_ptr,
			 avail_res_t *avail_res, int node_inx,
			 uint16_t cr_type)
{
	int resv_cpus;	/* CPUs to be allocated on other nodes */

	if (details_ptr->whole_node == 1)	/* Use all resources on node */
		return;

	resv_cpus = MAX((rem_nodes - 1), 0);
	resv_cpus *= cons_helpers_cpus_per_core(details_ptr, node_inx);
	if (cr_type & CR_SOCKET)
		resv_cpus *= node_record_table_ptr[node_inx]->cores;
	rem_max_cpus -= resv_cpus;
	if (*avail_cpus > rem_max_cpus) {
		*avail_cpus = MAX(rem_max_cpus, (int)details_ptr->pn_min_cpus);
		if (avail_res->gres_min_cpus)
			*avail_cpus =
				MAX(*avail_cpus, avail_res->gres_min_cpus);
		else
			*avail_cpus =
				MAX(*avail_cpus, details_ptr->min_gres_cpu);
		/* Round up CPU count to CPU in allocation unit (e.g. core) */
		avail_res->avail_cpus = *avail_cpus;
	}
	avail_res->avail_res_cnt = avail_res->avail_cpus +
				   avail_res->avail_gpus;
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

static int64_t _get_rem_max_cpus(job_details_t *details_ptr, int rem_nodes)
{
	int64_t rem_max_cpus = details_ptr->min_cpus;

	if (details_ptr->max_cpus != NO_VAL)
		rem_max_cpus = details_ptr->max_cpus;
	if (details_ptr->min_gres_cpu)
		rem_max_cpus = MAX(rem_max_cpus,
				   details_ptr->min_gres_cpu * rem_nodes);
	if (details_ptr->min_job_gres_cpu)
		rem_max_cpus = MAX(rem_max_cpus, details_ptr->min_job_gres_cpu);

	return rem_max_cpus;

}

/*
 * Identify the specific cores and GRES available to this job on this node.
 *	The job's requirements for tasks-per-socket, cpus-per-task, etc. are
 *	not considered at this point, but must be considered later.
 * IN job_ptr - job attempting to be scheduled
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN node_inx - zero-origin node index
 * IN max_nodes - maximum additional node count to allocate
 * IN rem_nodes - desired additional node count to allocate
 * IN avail_core - available core bitmap, UPDATED
 * IN avail_res_array - available resources on the node
 * IN first_pass - set if first scheduling attempt for this job, only use
 *		   co-located GRES and cores
 */
static void _select_cores(job_record_t *job_ptr, gres_mc_data_t *mc_ptr,
			  bool enforce_binding, int node_inx,
			  uint16_t *avail_cpus, uint32_t max_nodes,
			  int rem_nodes, bitstr_t **avail_core,
			  avail_res_t **avail_res_array, bool first_pass,
			  uint16_t cr_type)
{
	int alloc_tasks = 0;
	uint32_t min_tasks_this_node = 0, max_tasks_this_node = 0;
	uint32_t min_cores_this_node = 0;
	job_details_t *details_ptr = job_ptr->details;
	node_record_t *node_ptr = node_record_table_ptr[node_inx];

	xassert(mc_ptr->cpus_per_task);

	rem_nodes = MIN(rem_nodes, 1);	/* If range of node counts */
	if (mc_ptr->ntasks_per_node) {
		min_tasks_this_node = mc_ptr->ntasks_per_node;
		max_tasks_this_node = mc_ptr->ntasks_per_node;
	} else if (mc_ptr->ntasks_per_board) {
		min_tasks_this_node = mc_ptr->ntasks_per_board;
		max_tasks_this_node = mc_ptr->ntasks_per_board *
				      node_ptr->boards;
	} else if (mc_ptr->ntasks_per_socket) {
		min_tasks_this_node = mc_ptr->ntasks_per_socket;
		max_tasks_this_node = mc_ptr->ntasks_per_socket *
				      node_ptr->tot_sockets;
	} else if (mc_ptr->ntasks_per_core) {
		min_tasks_this_node = mc_ptr->ntasks_per_core;
		max_tasks_this_node = mc_ptr->ntasks_per_core *
				      (node_ptr->tot_cores -
				       node_ptr->core_spec_cnt);
	} else if (details_ptr && details_ptr->ntasks_per_tres &&
		   (details_ptr->ntasks_per_tres != NO_VAL16)) {
		/* Node ranges not allowed with --ntasks-per-gpu */
		if ((details_ptr->min_nodes != NO_VAL) &&
		    (details_ptr->min_nodes != 0) &&
		    (details_ptr->min_nodes == details_ptr->max_nodes)) {
			min_tasks_this_node = details_ptr->num_tasks /
				details_ptr->min_nodes;
			max_tasks_this_node = min_tasks_this_node;
		} else {
			min_tasks_this_node = details_ptr->ntasks_per_tres;
			max_tasks_this_node = details_ptr->num_tasks;
		}
	} else if (details_ptr && (details_ptr->max_nodes == 1)) {
		if ((details_ptr->num_tasks == NO_VAL) ||
		    (details_ptr->num_tasks == 0)) {
			min_tasks_this_node = 1;
			max_tasks_this_node = NO_VAL;
		} else {
			min_tasks_this_node = details_ptr->num_tasks;
			max_tasks_this_node = details_ptr->num_tasks;
		}
	} else if (details_ptr &&
		   ((details_ptr->num_tasks == 1) ||
		    ((details_ptr->num_tasks == details_ptr->min_nodes) &&
		     (details_ptr->num_tasks == details_ptr->max_nodes)))) {
		min_tasks_this_node = 1;
		max_tasks_this_node = 1;
	} else {
		min_tasks_this_node = 1;
		max_tasks_this_node = NO_VAL;
	}
	/* Determine how many tasks can be started on this node */
	if ((!details_ptr || !details_ptr->overcommit)) {
		alloc_tasks = avail_res_array[node_inx]->avail_cpus /
			      mc_ptr->cpus_per_task;
		if (alloc_tasks < min_tasks_this_node)
			max_tasks_this_node = 0;
		else if ((max_tasks_this_node == NO_VAL) ||
			 (alloc_tasks < max_tasks_this_node))
			max_tasks_this_node = alloc_tasks;
	}

	*avail_cpus = avail_res_array[node_inx]->avail_cpus;
	/*
	 * _allocate_sc() filters available cpus and cores if the job does
	 * not request gres. If the job requests gres, _allocate_sc() defers
	 * filtering cpus and cores so that gres_select_filter_sock_core() can
	 * do it.
	 */
	if (job_ptr->gres_list_req) {
		gres_select_filter_sock_core(
			job_ptr,
			mc_ptr,
			avail_res_array[node_inx]->sock_gres_list,
			avail_res_array[node_inx]->sock_cnt,
			node_ptr->cores, node_ptr->tpc, avail_cpus,
			&min_tasks_this_node, &max_tasks_this_node,
			&min_cores_this_node,
			rem_nodes, enforce_binding, first_pass,
			avail_core[node_inx],
			node_record_table_ptr[node_inx]->name,
			cr_type);
	}
	if (max_tasks_this_node == 0) {
		*avail_cpus = 0;
	} else if ((slurm_conf.select_type_param & CR_ONE_TASK_PER_CORE) &&
		   ((mc_ptr->ntasks_per_core == INFINITE16) ||
		    (mc_ptr->ntasks_per_core == 0)) &&
		   details_ptr && (details_ptr->min_gres_cpu == 0)) {
		*avail_cpus = bit_set_count(avail_core[node_inx]);
	}
	avail_res_array[node_inx]->gres_min_cpus =
		cons_helpers_cpus_per_core(job_ptr->details, node_inx) *
		min_cores_this_node;
	avail_res_array[node_inx]->gres_max_tasks = max_tasks_this_node;
}

/*
 * This is the heart of the selection process
 * IN job_ptr - job attempting to be scheduled
 * IN mc_ptr - job's multi-core specs, NO_VAL and INFINITE mapped to zero
 * IN node_map - bitmap of available/selected nodes, UPDATED
 * IN avail_core - available core bitmap, UPDATED
 * IN min_nodes - minimum node allocation size in nodes
 * IN max_nodes - maximum node allocation size in nodes
 * IN: req_nodes - number of requested nodes
 * IN avail_res_array - available resources on the node
 * IN cr_type - allocation type (sockets, cores, etc.)
 * IN prefer_alloc_nodes - if set, prefer use of already allocated nodes
 * IN first_pass - set if first scheduling attempt for this job, be picky
 * RET SLURM_SUCCESS or an error code
 */
static int _eval_nodes(job_record_t *job_ptr, gres_mc_data_t *mc_ptr,
		       bitstr_t *node_map, bitstr_t **avail_core,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, avail_res_t **avail_res_array,
		       uint16_t cr_type, bool prefer_alloc_nodes,
		       bool first_pass)
{
	job_details_t *details_ptr = job_ptr->details;

	xassert(node_map);
	if (bit_set_count(node_map) < min_nodes)
		return SLURM_ERROR;

	if ((details_ptr->req_node_bitmap) &&
	    (!bit_super_set(details_ptr->req_node_bitmap, node_map)))
		return SLURM_ERROR;

	if (blocks_nodes_bitmap &&
	    bit_overlap_any(blocks_nodes_bitmap, node_map))
		return _eval_nodes_block(job_ptr, mc_ptr, node_map,
					 avail_core, min_nodes,
					 max_nodes, req_nodes,
					 avail_res_array, cr_type,
					 prefer_alloc_nodes, first_pass);

	if (job_ptr->bit_flags & SPREAD_JOB) {
		/* Spread the job out over many nodes */
		return _eval_nodes_spread(job_ptr, mc_ptr, node_map, avail_core,
					  min_nodes, max_nodes, req_nodes,
					  avail_res_array, cr_type,
					  prefer_alloc_nodes, first_pass);
	}

	if (prefer_alloc_nodes && !details_ptr->contiguous) {
		/*
		 * Select resource on busy nodes first in order to leave
		 * idle resources free for as long as possible so that longer
		 * running jobs can get more easily started by the backfill
		 * scheduler plugin
		 */
		return _eval_nodes_busy(job_ptr, mc_ptr, node_map, avail_core,
					min_nodes, max_nodes, req_nodes,
					avail_res_array, cr_type,
					prefer_alloc_nodes, first_pass);
	}


	if ((cr_type & CR_LLN) ||
	    (job_ptr->part_ptr &&
	     (job_ptr->part_ptr->flags & PART_FLAG_LLN))) {
		/* Select resource on the Least Loaded Node */
		return _eval_nodes_lln(job_ptr, mc_ptr, node_map, avail_core,
				       min_nodes, max_nodes, req_nodes,
				       avail_res_array, cr_type,
				       prefer_alloc_nodes, first_pass);
	}

	if (pack_serial_at_end &&
	    (details_ptr->min_cpus == 1) && (req_nodes == 1)) {
		/*
		 * Put serial jobs at the end of the available node list
		 * rather than using a best-fit algorithm, which fragments
		 * resources.
		 */
		return _eval_nodes_serial(job_ptr, mc_ptr, node_map, avail_core,
					  min_nodes, max_nodes, req_nodes,
					  avail_res_array, cr_type,
					  prefer_alloc_nodes, first_pass);
	}

	if (switch_record_cnt && switch_record_table &&
	    !details_ptr->contiguous &&
	    ((topo_optional == false) || job_ptr->req_switch)) {
		/* Perform optimized resource selection based upon topology */
		if (have_dragonfly) {
			return _eval_nodes_dfly(job_ptr, mc_ptr, node_map,
						avail_core, min_nodes,
						max_nodes, req_nodes,
						avail_res_array, cr_type,
						prefer_alloc_nodes, first_pass);
		} else {
			return _eval_nodes_topo(job_ptr, mc_ptr, node_map,
						avail_core, min_nodes,
						max_nodes, req_nodes,
						avail_res_array, cr_type,
						prefer_alloc_nodes, first_pass);
		}
	}

	return _eval_nodes_consec(job_ptr, mc_ptr, node_map, avail_core,
				  min_nodes, max_nodes, req_nodes,
				  avail_res_array, cr_type, prefer_alloc_nodes,
				  first_pass);
}

static int _eval_nodes_consec(job_record_t *job_ptr, gres_mc_data_t *mc_ptr,
			      bitstr_t *node_map, bitstr_t **avail_core,
			      uint32_t min_nodes, uint32_t max_nodes,
			      uint32_t req_nodes, avail_res_t **avail_res_array,
			      uint16_t cr_type, bool prefer_alloc_nodes,
			      bool first_pass)
{
	int i, j, error_code = SLURM_ERROR;
	int *consec_cpus;	/* how many CPUs we can add from this
				 * consecutive set of nodes */
	List *consec_gres;	/* how many GRES we can add from this
				 * consecutive set of nodes */
	int *consec_nodes;	/* how many nodes we can add from this
				 * consecutive set of nodes */
	int *consec_start;	/* where this consecutive set starts (index) */
	int *consec_end;	/* where this consecutive set ends (index) */
	int *consec_req;	/* are nodes from this set required
				 * (in req_bitmap) */
	uint64_t *consec_weight; /* node scheduling weight */
	node_record_t *node_ptr = NULL;
	int consec_index, consec_size, sufficient;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int best_fit_nodes, best_fit_cpus, best_fit_req;
	int best_fit_sufficient, best_fit_index = 0;
	bool new_best;
	uint64_t best_weight = 0;
	uint16_t avail_cpus = 0;
	int64_t rem_max_cpus;
	int total_cpus = 0;	/* #CPUs allocated to job */
	bool gres_per_job, required_node;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bool enforce_binding = false;
	uint16_t *avail_cpu_per_node = NULL;

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;

	/* make allocation for 50 sets of consecutive nodes, expand as needed */
	consec_size = 50;
	consec_cpus   = xcalloc(consec_size, sizeof(int));
	consec_nodes  = xcalloc(consec_size, sizeof(int));
	consec_start  = xcalloc(consec_size, sizeof(int));
	consec_end    = xcalloc(consec_size, sizeof(int));
	consec_req    = xcalloc(consec_size, sizeof(int));
	consec_weight = xcalloc(consec_size, sizeof(uint64_t));

	/* Build table with information about sets of consecutive nodes */
	consec_index = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	consec_weight[consec_index] = NO_VAL64;

	avail_cpu_per_node = xcalloc(node_record_count, sizeof(uint16_t));
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req))) {
		rem_nodes = MIN(min_nodes, req_nodes);
		consec_gres = xcalloc(consec_size, sizeof(List));
	} else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

	/*
	 * If there are required nodes, first determine the resources they
	 * provide, then select additional resources as needed in next loop
	 */
	if (req_map) {
		int count = 0;
		uint16_t *arbitrary_tpn = job_ptr->details->arbitrary_tpn;
		for (i = 0;
		     ((node_ptr = next_node_bitmap(req_map, &i)) &&
		      (max_nodes > 0));
		     i++) {
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			if (arbitrary_tpn) {
				int req_cpus = arbitrary_tpn[count++];
				if ((details_ptr->cpus_per_task != NO_VAL16) &&
				    (details_ptr->cpus_per_task != 0))
					req_cpus *= details_ptr->cpus_per_task;

				req_cpus = MAX(req_cpus,
					       (int) details_ptr->pn_min_cpus);
				req_cpus = MAX(req_cpus,
					       details_ptr->min_gres_cpu);

				if (avail_cpus < req_cpus) {
					debug("%pJ required node %s needed %d cpus but only has %d",
					      job_ptr, node_ptr->name, req_cpus,
					      avail_cpus);
					goto fini;
				}
				avail_cpus = req_cpus;

				avail_res_array[i]->avail_cpus = avail_cpus;
				avail_res_array[i]->avail_res_cnt =
					avail_res_array[i]->avail_cpus +
					avail_res_array[i]->avail_gpus;

			} else
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			if (avail_cpus == 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			avail_cpu_per_node[i] = avail_cpus;
			total_cpus += avail_cpus;
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(node_map, req_map);
			goto fini;
		}
		if (max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
	}

	for (i = 0; next_node(&i); i++) { /* For each node */
		if ((consec_index + 1) >= consec_size) {
			consec_size *= 2;
			xrecalloc(consec_cpus, consec_size, sizeof(int));
			xrecalloc(consec_nodes, consec_size, sizeof(int));
			xrecalloc(consec_start, consec_size, sizeof(int));
			xrecalloc(consec_end, consec_size, sizeof(int));
			xrecalloc(consec_req, consec_size, sizeof(int));
			xrecalloc(consec_weight, consec_size, sizeof(uint64_t));
			if (gres_per_job) {
				xrecalloc(consec_gres,
					  consec_size, sizeof(List));
			}
		}
		if (req_map)
			required_node = bit_test(req_map, i);
		else
			required_node = false;
		if (!bit_test(node_map, i)) {
			node_ptr = NULL;    /* Use as flag, avoid second test */
		} else if (required_node) {
			node_ptr = node_record_table_ptr[i];
		} else {
			node_ptr = node_record_table_ptr[i];
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			if (avail_cpus == 0) {
				bit_clear(node_map, i);
				node_ptr = NULL;
			}
			avail_cpu_per_node[i] = avail_cpus;
		}
		/*
		 * If job requested contiguous nodes,
		 * do not worry about matching node weights
		 */
		if (node_ptr &&
		    !details_ptr->contiguous &&
		    (consec_weight[consec_index] != NO_VAL64) && /* Init value*/
		    (node_ptr->sched_weight != consec_weight[consec_index])) {
			/* End last consecutive set, setup start of next set */
			if (consec_nodes[consec_index] == 0) {
				/* Only required nodes, re-use consec record */
				consec_req[consec_index] = -1;
			} else {
				/* End last set, setup for start of next set */
				consec_end[consec_index]   = i - 1;
				consec_req[++consec_index] = -1;
			}
		}
		if (node_ptr) {
			if (consec_nodes[consec_index] == 0)
				consec_start[consec_index] = i;
			if (required_node) {
				/*
				 * Required node, resources counters updated
				 * in above loop, leave bitmap set
				 */
				if (consec_req[consec_index] == -1) {
					/* first required node in set */
					consec_req[consec_index] = i;
				}
				continue;
			}

			/* node not selected (yet) */
			bit_clear(node_map, i);
			consec_cpus[consec_index] += avail_cpus;
			consec_nodes[consec_index]++;
			if (gres_per_job) {
				gres_sched_consec(
					&consec_gres[consec_index],
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list);
			}
			consec_weight[consec_index] = node_ptr->sched_weight;
		} else if (consec_nodes[consec_index] == 0) {
			/* Only required nodes, re-use consec record */
			consec_req[consec_index] = -1;
			consec_weight[consec_index] = NO_VAL64;
		} else {
			/* End last set, setup for start of next set */
			consec_end[consec_index]   = i - 1;
			consec_req[++consec_index] = -1;
			consec_weight[consec_index] = NO_VAL64;
		}
	}
	if (consec_nodes[consec_index] != 0)
		consec_end[consec_index++] = i - 1;

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (consec_index == 0) {
			info("consec_index is zero");
		}
		for (i = 0; i < consec_index; i++) {
			char *gres_str = NULL, *gres_print = "";
			bitstr_t *host_bitmap;
			char *host_list;
			if (gres_per_job) {
				gres_str = gres_sched_str(consec_gres[i]);
				if (gres_str) {
					xstrcat(gres_str, " ");
					gres_print = gres_str;
				}
			}

			host_bitmap = bit_alloc(node_record_count);
			bit_nset(host_bitmap, consec_start[i], consec_end[i]);
			host_list = bitmap2node_name(host_bitmap);
			info("set:%d consec "
			     "CPUs:%d nodes:%d:%s %sbegin:%d end:%d required:%d weight:%"PRIu64,
			     i, consec_cpus[i], consec_nodes[i],
			     host_list, gres_print, consec_start[i],
			     consec_end[i], consec_req[i], consec_weight[i]);
			FREE_NULL_BITMAP(host_bitmap);
			xfree(gres_str);
			xfree(host_list);
		}
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/*
	 * accumulate nodes from these sets of consecutive nodes until
	 * sufficient resources have been accumulated
	 */
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
			if (sufficient && gres_per_job) {
				sufficient = gres_sched_sufficient(
					job_ptr->gres_list_req, consec_gres[i]);
			}

			/*
			 * if first possibility OR
			 * contains required nodes OR
			 * lowest node weight
			 */
			if ((best_fit_nodes == 0) ||
			    ((best_fit_req == -1) && (consec_req[i] != -1)) ||
			    (consec_weight[i] < best_weight))
				new_best = true;
			else
				new_best = false;
			/*
			 * If equal node weight
			 * first set large enough for request OR
			 * tightest fit (less resource/CPU waste) OR
			 * nothing yet large enough, but this is biggest
			 */
			if (!new_best && (consec_weight[i] == best_weight) &&
			    ((sufficient && (best_fit_sufficient == 0)) ||
			     (sufficient && (consec_cpus[i] < best_fit_cpus)) ||
			     (!sufficient &&
			      (consec_cpus[i] > best_fit_cpus))))
				new_best = true;
			/*
			 * if first continuous node set large enough
			 */
			if (!new_best && !best_fit_sufficient &&
			    details_ptr->contiguous && sufficient)
				new_best = true;
			if (new_best) {
				best_fit_cpus = consec_cpus[i];
				best_fit_nodes = consec_nodes[i];
				best_fit_index = i;
				best_fit_req = consec_req[i];
				best_fit_sufficient = sufficient;
				best_weight = consec_weight[i];
			}

			if (details_ptr->contiguous &&
			    details_ptr->req_node_bitmap) {
				/*
				 * Must wait for all required nodes to be
				 * in a single consecutive block
				 */
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

		if (details_ptr->contiguous && !best_fit_sufficient)
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/*
			 * This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes
			 */
			for (i = best_fit_req;
			     i <= consec_end[best_fit_index]; i++) {
				if ((max_nodes == 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				     (!gres_per_job ||
				      gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))))
					break;
				if (bit_test(node_map, i)) {
					/* required node already in set */
					continue;
				}
				if (avail_cpu_per_node[i] == 0)
					continue;
				avail_cpus = avail_cpu_per_node[i];

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list, &avail_cpus);
				}
				total_cpus += avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_index]; i--) {
				if ((max_nodes == 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				     (!gres_per_job ||
				      gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))))
					break;
				if (bit_test(node_map, i))
					continue;
				if (avail_cpu_per_node[i] == 0)
					continue;
				avail_cpus = avail_cpu_per_node[i];

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list, &avail_cpus);
				}
				total_cpus += avail_cpus;
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
			}
		} else {
			/* No required nodes, try best fit single node */
			int best_fit = -1, best_size = 0;
			int first = consec_start[best_fit_index];
			int last  = consec_end[best_fit_index];
			if (rem_nodes <= 1) {
				for (i = first, j = 0; i <= last; i++, j++) {
					if (bit_test(node_map, i) ||
					    !avail_res_array[i])
						continue;
					if (avail_cpu_per_node[i] < rem_cpus)
						continue;
					if (gres_per_job &&
					    !gres_sched_sufficient(
						    job_ptr->gres_list_req,
						    avail_res_array[i]->
						    sock_gres_list)) {
						continue;
					}
					if ((best_fit == -1) ||
					    (avail_cpu_per_node[i] <best_size)){
						best_fit = i;
						best_size =
							avail_cpu_per_node[i];
						if (best_size == rem_cpus)
							break;
					}
				}
				/*
				 * If we found a single node to use,
				 * clear CPU counts for all other nodes
				 */
				if (best_fit != -1) {
					for (i = first; i <= last; i++) {
						if (i == best_fit)
							continue;
						avail_cpu_per_node[i] = 0;
					}
				}
			}

			for (i = first, j = 0; i <= last; i++, j++) {
				if ((max_nodes == 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				     (!gres_per_job ||
				      gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))))
					break;
				if (bit_test(node_map, i) ||
				    !avail_res_array[i])
					continue;

				avail_cpus = avail_cpu_per_node[i];
				if (avail_cpus <= 0)
					continue;

				if ((max_nodes == 1) &&
				    (avail_cpus < rem_cpus)) {
					/*
					 * Job can only take one more node and
					 * this one has insufficient CPU
					 */
					continue;
				}

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list, &avail_cpus);
				}
				total_cpus += avail_cpus;
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
			}
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_index] = 0;
		consec_nodes[best_fit_index] = 0;
	}

	if (error_code && (rem_cpus <= 0) &&
	    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id) &&
	    _enough_nodes(0, rem_nodes, min_nodes, req_nodes))
		error_code = SLURM_SUCCESS;

fini:	xfree(avail_cpu_per_node);
	xfree(consec_cpus);
	xfree(consec_nodes);
	xfree(consec_start);
	xfree(consec_end);
	xfree(consec_req);
	xfree(consec_weight);
	if (gres_per_job) {
		for (i = 0; i < consec_size; i++)
			FREE_NULL_LIST(consec_gres[i]);
		xfree(consec_gres);
	}

	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources using as many nodes as
 * possible.
 */
static int _eval_nodes_spread(job_record_t *job_ptr,
			      gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			      bitstr_t **avail_core, uint32_t min_nodes,
			      uint32_t max_nodes, uint32_t req_nodes,
			      avail_res_t **avail_res_array, uint16_t cr_type,
			      bool prefer_alloc_nodes, bool first_pass)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	bool enforce_binding = false;

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

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
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]-> sock_gres_list,
					&avail_cpus);
			}
			if (avail_cpus <= 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(node_map, req_map);
			goto fini;
		}
		if (max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, node_map);
	} else {
		bit_clear_all(node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	if (max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = (node_weight_type *) list_next(iter))) {
		for (i = i_start; i <= i_end; i++) {
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus)
				continue;
			/* Node not available or already selected */
			if (!bit_test(nwt->node_bitmap, i) ||
			    bit_test(node_map, i))
				continue;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			if (avail_cpus == 0)
				continue;
			total_cpus += avail_cpus;
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			bit_set(node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_sched_test(job_ptr->gres_list_req,
					    job_ptr->job_id)) {
				error_code = SLURM_SUCCESS;
				all_done = true;
				break;
			}
			if (max_nodes == 0) {
				all_done = true;
				break;
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources using busy nodes first.
 */
static int _eval_nodes_busy(job_record_t *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int idle_test;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	bool enforce_binding = false;

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

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
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->
					sock_gres_list, &avail_cpus);
			}
			if (avail_cpus <= 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(node_map, req_map);
			goto fini;
		}
		if (max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, node_map);
	} else {
		bit_clear_all(node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/*
	 * Start by using nodes that already have a job running.
	 * Then try to use idle nodes.
	 */
	if (max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = (node_weight_type *) list_next(iter))) {
		for (idle_test = 0; idle_test < 2; idle_test++) {
			for (i = i_start; i <= i_end; i++) {
				if (!avail_res_array[i] ||
				    !avail_res_array[i]->avail_cpus)
					continue;
				/* Node not available or already selected */
				if (!bit_test(nwt->node_bitmap, i) ||
				    bit_test(node_map, i))
					continue;
				if (((idle_test == 0) &&
				     bit_test(idle_node_bitmap, i)) ||
				    ((idle_test == 1) &&
				     !bit_test(idle_node_bitmap, i)))
					continue;
				_select_cores(job_ptr, mc_ptr, enforce_binding,
					      i, &avail_cpus, max_nodes,
					      min_rem_nodes, avail_core,
					      avail_res_array, first_pass,
					      cr_type);
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[i]->
						sock_gres_list,
						&avail_cpus);
				}
				if (avail_cpus == 0)
					continue;
				total_cpus += avail_cpus;
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				bit_set(node_map, i);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    gres_sched_test(job_ptr->gres_list_req,
						    job_ptr->job_id)) {
					error_code = SLURM_SUCCESS;
					all_done = true;
					break;
				}
				if (max_nodes == 0) {
					all_done = true;
					break;
				}
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;
}

static void _topo_add_dist(uint32_t *dist, int inx)
{
	int i;
	for (i = 0; i < switch_record_cnt; i++) {
		if (switch_record_table[inx].switches_dist[i] == INFINITE ||
		    dist[i] == INFINITE) {
			dist[i] = INFINITE;
		} else {
			dist[i] += switch_record_table[inx].switches_dist[i];
		}
	}
}

static int _topo_compare_switches(int i, int j, int rem_nodes,
				  int *switch_node_cnt, int rem_cpus,
				  uint32_t *switch_cpu_cnt)
{
	while (1) {
		bool i_fit = ((switch_node_cnt[i] >= rem_nodes) &&
			      (switch_cpu_cnt[i] >= rem_cpus));
		bool j_fit = ((switch_node_cnt[j] >= rem_nodes) &&
			      (switch_cpu_cnt[j] >= rem_cpus));
		if (i_fit && j_fit) {
			if (switch_node_cnt[i] < switch_node_cnt[j])
				return 1;
			if (switch_node_cnt[i] > switch_node_cnt[j])
				return -1;
			break;
		} else if (i_fit) {
			return 1;
		} else if (j_fit) {
			return -1;
		}

		if (((switch_record_table[i].parent != i) ||
		     (switch_record_table[j].parent != j)) &&
		    (switch_record_table[i].parent !=
		     switch_record_table[j].parent)) {
			i = switch_record_table[i].parent;
			j = switch_record_table[j].parent;
			continue;
		}

		break;
	}

	if (switch_node_cnt[i] > switch_node_cnt[j])
		return 1;
	if (switch_node_cnt[i] < switch_node_cnt[j])
		return -1;
	if (switch_record_table[i].level < switch_record_table[j].level)
		return 1;
	if (switch_record_table[i].level > switch_record_table[j].level)
		return -1;
	return 0;

}
static void _topo_choose_best_switch(uint32_t *dist, int *switch_node_cnt,
				     int rem_nodes, uint32_t *switch_cpu_cnt,
				     int rem_cpus, int i, int *best_switch)
{
	int tcs = 0;

	if (*best_switch == -1 || dist[i] == INFINITE || !switch_node_cnt[i]) {
		/*
		 * If first possibility
		 */
		if (switch_node_cnt[i] && dist[i] < INFINITE)
			*best_switch = i;
		return;
	}

	tcs = _topo_compare_switches(i, *best_switch, rem_nodes,
				     switch_node_cnt, rem_cpus, switch_cpu_cnt);
	if (((dist[i] < dist[*best_switch]) && (tcs >= 0)) ||
	    ((dist[i] == dist[*best_switch]) && (tcs > 0))) {
		/*
		 * If closer and fit request OR
		 * same distance and tightest fit (less resource waste)
		 */
		*best_switch = i;
	}
}
static int _topo_weight_find(void *x, void *key)
{
	topo_weight_info_t *nw = (topo_weight_info_t *) x;
	topo_weight_info_t *nw_key = (topo_weight_info_t *) key;
	if (nw->weight == nw_key->weight)
		return 1;
	return 0;
}

static int _topo_node_find(void *x, void *key)
{
	topo_weight_info_t *nw = (topo_weight_info_t *) x;
	bitstr_t *nw_key = (bitstr_t *) key;
	if (bit_overlap_any(nw->node_bitmap, nw_key))
		return 1;
	return 0;
}

static void _topo_weight_free(void *x)
{
	topo_weight_info_t *nw = (topo_weight_info_t *) x;
	FREE_NULL_BITMAP(nw->node_bitmap);
	xfree(nw);
}

static int _topo_weight_log(void *x, void *arg)
{
	topo_weight_info_t *nw = (topo_weight_info_t *) x;
	char *node_names = bitmap2node_name(nw->node_bitmap);
	info("Topo:%s weight:%"PRIu64, node_names, nw->weight);
	xfree(node_names);
	return 0;
}
static int _topo_weight_sort(void *x, void *y)
{
	topo_weight_info_t *nwt1 = *(topo_weight_info_t **) x;
	topo_weight_info_t *nwt2 = *(topo_weight_info_t **) y;
	if (nwt1->weight < nwt2->weight)
		return -1;
	if (nwt1->weight > nwt2->weight)
		return 1;
	return 0;
}

/*
 * Allocate resources to the job on one leaf switch if possible,
 * otherwise distribute the job allocation over many leaf switches.
 */
static int _eval_nodes_dfly(job_record_t *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass)
{
	List      *switch_gres = NULL;		/* available GRES on switch */
	bitstr_t **switch_node_bitmap = NULL;	/* nodes on this switch */
	int       *switch_node_cnt = NULL;	/* total nodes on switch */
	int       *switch_required = NULL;	/* set if has required node */
	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;	/* required node bitmap */
	bitstr_t  *req2_nodes_bitmap  = NULL;	/* required+lowest prio nodes */
	bitstr_t  *best_nodes_bitmap  = NULL;	/* required+low prio nodes */
	int i, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt = 0, best_node_cnt = 0, req_node_cnt = 0;
	List best_gres = NULL;
	switch_record_t *switch_ptr;
	List node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	ListIterator iter;
	node_record_t *node_ptr;
	uint16_t avail_cpus = 0;
	int64_t rem_max_cpus;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	bool enforce_binding = false;
	job_details_t *details_ptr = job_ptr->details;
	bool gres_per_job, sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	time_t time_waiting = 0;
	int leaf_switch_count = 0;
	int top_switch_inx = -1;
	int prev_rem_nodes;

	if (job_ptr->req_switch > 1) {
		/* Maximum leaf switch count >1 probably makes no sense */
		info("Resetting %pJ leaf switch count from %u to 0",
		     job_ptr, job_ptr->req_switch);
		job_ptr->req_switch = 0;
	}
	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   node_map)) {
			info("%pJ requires nodes which are not currently available",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%pJ required node list has no nodes",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (req_node_cnt > max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			      job_ptr, req_node_cnt,
			      max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
		req_nodes_bitmap = bit_copy(job_ptr->details->req_node_bitmap);
	}

	/*
	 * Add required nodes to job allocation and
	 * build list of node bitmaps, sorted by weight
	 */
	if (!bit_set_count(node_map)) {
		debug("%pJ node_map is empty",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	avail_cpu_per_node = xcalloc(node_record_count, sizeof(uint16_t));
	node_weight_list = list_create(_topo_weight_free);
	for (i = 0; (node_ptr = next_node_bitmap(node_map, &i)); i++) {
		topo_weight_info_t nw_static;
		if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i)) {
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			if (avail_cpus == 0) {
				log_flag(SELECT_TYPE, "%pJ insufficient resources on required node",
				       job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			avail_cpu_per_node[i] = avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
		}

		nw_static.weight = node_ptr->sched_weight;
		nw = list_find_first(node_weight_list, _topo_weight_find,
				     &nw_static);
		if (!nw) {	/* New node weight to add */
			nw = xmalloc(sizeof(topo_weight_info_t));
			nw->node_bitmap = bit_alloc(node_record_count);
			nw->weight = node_ptr->sched_weight;
			list_append(node_weight_list, nw);
		}
		bit_set(nw->node_bitmap, i);
		nw->node_cnt++;
	}

	if (req_nodes_bitmap) {
		bit_and(node_map, req_nodes_bitmap);
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
				 job_ptr);
			goto fini;
		}
	} else {
		bit_clear_all(node_map);
	}

	list_sort(node_weight_list, _topo_weight_sort);
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)
		(void) list_for_each(node_weight_list, _topo_weight_log, NULL);

	/*
	 * Identify the highest level switch to be used.
	 * Note that nodes can be on multiple non-overlapping switches.
	 */
	switch_gres        = xcalloc(switch_record_cnt, sizeof(List));
	switch_node_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switch_node_cnt    = xcalloc(switch_record_cnt, sizeof(int));
	switch_required    = xcalloc(switch_record_cnt, sizeof(int));

	if (!req_nodes_bitmap)
		nw = list_peek(node_weight_list);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		switch_node_bitmap[i] = bit_copy(switch_ptr->node_bitmap);
		if (req_nodes_bitmap &&
		    bit_overlap_any(req_nodes_bitmap, switch_node_bitmap[i])) {
			switch_required[i] = 1;
			if (switch_record_table[i].level == 0) {
				leaf_switch_count++;
			}
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
		if (!req_nodes_bitmap &&
		    (list_find_first(node_weight_list, _topo_node_find,
				    switch_node_bitmap[i]))) {
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
	}

	/*
	 * Top switch is highest level switch containing all required nodes
	 * OR all nodes of the lowest scheduling weight
	 * OR -1 of can not identify top-level switch
	 */
	if (top_switch_inx == -1) {
		error("%pJ unable to identify top level switch",
		       job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that all specificly required nodes are on shared network */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap,
			   switch_node_bitmap[top_switch_inx])) {
		rc = SLURM_ERROR;
		info("%pJ requires nodes that do not have shared network",
		     job_ptr);
		goto fini;
	}

	/*
	 * Remove nodes from consideration that can not be reached from this
	 * top level switch
	 */
	for (i = 0; i < switch_record_cnt; i++) {
		if (top_switch_inx != i) {
			  bit_and(switch_node_bitmap[i],
				  switch_node_bitmap[top_switch_inx]);
		}
	}

	/*
	 * Identify the best set of nodes (i.e. nodes with the lowest weight,
	 * in addition to the required nodes) that can be used to satisfy the
	 * job request. All nodes must be on a common top-level switch. The
	 * logic here adds groups of nodes, all with the same weight, so we
	 * usually identify more nodes than required to satisfy the request.
	 * Later logic selects from those nodes to get the best topology.
	 */
	best_nodes_bitmap = bit_alloc(node_record_count);
	iter = list_iterator_create(node_weight_list);
	while (!sufficient && (nw = list_next(iter))) {
		if (best_node_cnt > 0) {
			/*
			 * All of the lower priority nodes should be included
			 * in the job's allocation. Nodes from the next highest
			 * weight nodes are included only as needed.
			 */
			if (req2_nodes_bitmap)
				bit_or(req2_nodes_bitmap, best_nodes_bitmap);
			else
				req2_nodes_bitmap = bit_copy(best_nodes_bitmap);
		}
		for (i = 0; next_node_bitmap(nw->node_bitmap, &i); i++) {
			if (avail_cpu_per_node[i])
				continue;	/* Required node */
			if (!bit_test(switch_node_bitmap[top_switch_inx], i))
				continue;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			if (avail_cpus == 0) {
				bit_clear(nw->node_bitmap, i);
				continue;
			}
			bit_set(best_nodes_bitmap, i);
			avail_cpu_per_node[i] = avail_cpus;
			best_cpu_cnt += avail_cpus;
			best_node_cnt++;
			if (gres_per_job) {
				gres_sched_consec(
					&best_gres, job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list);
			}
		}

		sufficient = (best_cpu_cnt >= rem_cpus) &&
			     _enough_nodes(best_node_cnt, rem_nodes,
					   min_nodes, req_nodes);
		if (sufficient && gres_per_job) {
			sufficient = gres_sched_sufficient(
				job_ptr->gres_list_req, best_gres);
		}
	}
	list_iterator_destroy(iter);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *gres_str = NULL, *gres_print = "";
		char *node_names;
		if (req_nodes_bitmap) {
			node_names = bitmap2node_name(req_nodes_bitmap);
			info("Required nodes:%s", node_names);
			xfree(node_names);
		}
		node_names = bitmap2node_name(best_nodes_bitmap);
		if (gres_per_job) {
			gres_str = gres_sched_str(best_gres);
			if (gres_str)
				gres_print = gres_str;
		}
		info("Best nodes:%s node_cnt:%d cpu_cnt:%d %s",
		     node_names, best_node_cnt, best_cpu_cnt, gres_print);
		xfree(node_names);
		xfree(gres_str);
	}
	if (!sufficient) {
		log_flag(SELECT_TYPE, "insufficient resources currently available for %pJ",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Add lowest weight nodes. Treat similar to required nodes for the job.
	 * Job will still need to add some higher weight nodes later.
	 */
	if (req2_nodes_bitmap) {
		for (i = 0;
		     next_node_bitmap(req2_nodes_bitmap, &i) && (max_nodes > 0);
		     i++) {
			avail_cpus = avail_cpu_per_node[i];
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
		}

		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_required[i])
				continue;
			if (bit_overlap_any(req2_nodes_bitmap,
					    switch_node_bitmap[i])) {
				switch_required[i] = 1;
				if (switch_record_table[i].level == 0) {
					leaf_switch_count++;
				}
			}
		}
		bit_or(node_map, req2_nodes_bitmap);
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
				 job_ptr);
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    (!gres_per_job || gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))) {
			/* Required nodes completely satisfied the request */
			error("Scheduling anomaly for %pJ",
			      job_ptr);
			rc = SLURM_SUCCESS;
			goto fini;
		}
	}

	/*
	 * Construct a set of switch array entries.
	 * Use the same indexes as switch_record_table in slurmctld.
	 */
	bit_or(best_nodes_bitmap, node_map);
	avail_nodes_bitmap = bit_alloc(node_record_count);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		bit_and(switch_node_bitmap[i], best_nodes_bitmap);
		bit_or(avail_nodes_bitmap, switch_node_bitmap[i]);
		switch_node_cnt[i] = bit_set_count(switch_node_bitmap[i]);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switch_node_cnt[i]) {
				node_names =
					bitmap2node_name(switch_node_bitmap[i]);
			}
			info("switch=%s level=%d nodes=%u:%s required:%u speed:%u",
			     switch_record_table[i].name,
			     switch_record_table[i].level,
			     switch_node_cnt[i], node_names,
			     switch_required[i],
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

	/*
	 * If no resources have yet been  selected,
	 * then pick one leaf switch with the most available nodes.
	 */
	if (leaf_switch_count == 0) {
		int best_switch_inx = -1;
		for (i = 0; i < switch_record_cnt; i++) {
			if (switch_record_table[i].level != 0)
				continue;
			if ((best_switch_inx == -1) ||
			    (switch_node_cnt[i] >
			     switch_node_cnt[best_switch_inx]))
				best_switch_inx = i;
		}
		if (best_switch_inx != -1) {
			leaf_switch_count = 1;
			switch_required[best_switch_inx] = 1;
		}
	}

	/*
	 * All required resources currently on one leaf switch. Determine if
	 * the entire job request can be satisfied using just that one switch.
	 */
	if (leaf_switch_count == 1) {
		best_cpu_cnt = 0;
		best_node_cnt = 0;
		FREE_NULL_LIST(best_gres);
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				best_cpu_cnt += avail_cpus;
				best_node_cnt++;
				if (gres_per_job) {
					gres_sched_consec(
						&best_gres,
						job_ptr->gres_list_req,
						avail_res_array[j]->
						sock_gres_list);
				}
			}
			break;
		}
		sufficient = (best_cpu_cnt >= rem_cpus) &&
			     _enough_nodes(best_node_cnt, rem_nodes,
				   min_nodes, req_nodes);
		if (sufficient && gres_per_job) {
			sufficient = gres_sched_sufficient(
				job_ptr->gres_list_req, best_gres);
		}
		if (sufficient && (i < switch_record_cnt)) {
			/* Complete request using this one leaf switch */
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[j], j, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[j]->
						sock_gres_list,
						&avail_cpus);
				}
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!gres_per_job ||
				     gres_sched_test(job_ptr->gres_list_req,
						     job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
				if (max_nodes <= 0) {
					rc = SLURM_ERROR;
					log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
						 job_ptr);
					goto fini;
				}
			}
		}
	}

	/*
	 * Add additional resources as required from additional leaf switches
	 * on a round-robin basis
	 */
	prev_rem_nodes = rem_nodes + 1;
	while (1) {
		if (prev_rem_nodes == rem_nodes)
			break;	/* Stalled */
		prev_rem_nodes = rem_nodes;
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[j], j, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[j]->
						sock_gres_list,
						&avail_cpus);
				}
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!gres_per_job ||
				     gres_sched_test(job_ptr->gres_list_req,
						     job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
				if (max_nodes <= 0) {
					rc = SLURM_ERROR;
					log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
						 job_ptr);
					goto fini;
				}
				break;	/* Move to next switch */
			}
		}
	}
	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!gres_per_job ||
	     gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:
	if ((job_ptr->req_switch > 0) && (rc == SLURM_SUCCESS) &&
	    switch_node_bitmap) {
		/* req_switch == 1 here; enforced at the top of the function. */
		leaf_switch_count = 0;

		/* count up leaf switches */
		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_record_table[i].level != 0)
				continue;
			if (bit_overlap_any(switch_node_bitmap[i], node_map))
				leaf_switch_count++;
		}
		if (time_waiting >= job_ptr->wait4switch) {
			job_ptr->best_switch = true;
			debug3("%pJ waited %ld sec for switches use=%d",
				job_ptr, time_waiting, leaf_switch_count);
		} else if (leaf_switch_count > job_ptr->req_switch) {
			/*
			 * Allocation is for more than requested number of
			 * switches.
			 */
			job_ptr->best_switch = false;
			debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
				job_ptr, time_waiting, job_ptr->req_switch,
				leaf_switch_count, job_ptr->wait4switch);
		} else {
			job_ptr->best_switch = true;
		}
	}

	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	xfree(avail_cpu_per_node);
	xfree(switch_gres);
	if (switch_node_bitmap) {
		for (i = 0; i < switch_record_cnt; i++)
			FREE_NULL_BITMAP(switch_node_bitmap[i]);
		xfree(switch_node_bitmap);
	}
	xfree(switch_node_cnt);
	xfree(switch_required);
	return rc;
}

static int _cmp_bblock(const void *a, const void *b)
{
	int ca = *((int *) a);
	int cb = *((int *) b);

	if (ca < cb)
		return 1;
	else if (ca > cb)
		return -1;

	return 0;
}

static bool _bblocks_in_same_block(int block_inx1, int block_inx2,
				   int block_level)
{
	if ((block_inx1 >> block_level) == (block_inx2 >> block_level))
		return true;
	return false;
}

static void _choose_best_bblock(bitstr_t *bblock_required,
				int llblock_level, int rem_nodes,
				uint32_t *nodes_on_bblock,
				uint32_t *nodes_on_llblock,
				int i, bool *best_same_block,
				bool *best_fit, int *best_bblock_inx)
{
	bool fit = (nodes_on_bblock[i] >= rem_nodes);
	bool same_block = false;
	if (nodes_on_llblock &&
	    !_bblocks_in_same_block(*best_bblock_inx, i, llblock_level)) {
		for (int j = (i & (~0 << llblock_level));
		     ((j < block_record_cnt) &&
		      (j <= (i | ~(~0 << llblock_level))));
		     j++) {
			if (!bit_test(bblock_required, j))
				continue;
			if ((same_block =
			     _bblocks_in_same_block(j, i, llblock_level)))
				break;
		}

		if ((*best_bblock_inx == -1) ||
		    (same_block && !(*best_same_block))) {
			*best_bblock_inx = i;
			*best_fit = fit;
			*best_same_block = same_block;
			return;
		}

		if (!same_block && (*best_same_block))
			return;

		if (nodes_on_llblock[(i >> llblock_level)] >
		    nodes_on_llblock[(*best_bblock_inx >> llblock_level)]) {
			*best_bblock_inx = i;
			*best_fit = fit;
			*best_same_block = same_block;
			return;
		}

		if (nodes_on_llblock[(i >> llblock_level)] <
		    nodes_on_llblock[(*best_bblock_inx >> llblock_level)])
			return;
	}

	if (*best_bblock_inx == -1 || (fit && !(*best_fit)) ||
	    (!fit && !(*best_fit) && (nodes_on_bblock[i] >=
				      nodes_on_bblock[*best_bblock_inx])) ||
	    (fit && (nodes_on_bblock[i] <=
		     nodes_on_bblock[*best_bblock_inx]))) {
		*best_bblock_inx = i;
		*best_fit = fit;
		return;
	}
}

static int _eval_nodes_block(job_record_t *job_ptr,
			     gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			     bitstr_t **avail_core, uint32_t min_nodes,
			     uint32_t max_nodes, uint32_t req_nodes,
			     avail_res_t **avail_res_array, uint16_t cr_type,
			     bool prefer_alloc_nodes, bool first_pass)
{
	uint32_t *block_cpu_cnt = NULL;	/* total CPUs on block */
	List *block_gres = NULL;		/* available GRES on block */
	bitstr_t **block_node_bitmap = NULL;	/* nodes on this block */
	bitstr_t **bblock_node_bitmap = NULL;	/* nodes on this base block */
	uint32_t *block_node_cnt = NULL;	/* total nodes on block */
	uint32_t *nodes_on_bblock = NULL;	/* total nodes on bblock */
	bitstr_t *avail_nodes_bitmap = NULL;	/* nodes on any block */
	bitstr_t *req_nodes_bitmap = NULL;	/* required node bitmap */
	bitstr_t *req2_nodes_bitmap = NULL;	/* required+lowest prio nodes */
	bitstr_t *best_nodes_bitmap = NULL;	/* required+low prio nodes */
	bitstr_t *bblock_bitmap = NULL;
	int *bblock_block_inx = NULL;
	bitstr_t *bblock_required = NULL;
	int i, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt, best_node_cnt, req_node_cnt = 0;
	List best_gres = NULL;
	block_record_t *block_ptr;
	List node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	ListIterator iter;
	node_record_t *node_ptr;
	uint16_t avail_cpus = 0;
	int64_t rem_max_cpus;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	bool enforce_binding = false;
	job_details_t *details_ptr = job_ptr->details;
	bool gres_per_job, requested, sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	int block_inx = -1;
	uint64_t block_lowest_weight = 0;
	int block_cnt = -1, bblock_per_block;
	int prev_rem_nodes;
	int max_llblock;
	int llblock_level;
	int llblock_size;
	int llblock_cnt = 0;
	uint32_t *nodes_on_llblock = NULL;
	int block_level;
	int bblock_per_llblock;

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;

	/* Always use min_nodes */
	gres_per_job = gres_sched_init(job_ptr->gres_list_req);
	rem_nodes = MIN(min_nodes, req_nodes);

	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

	bblock_per_block = ((rem_nodes + bblock_node_cnt - 1) /
			    bblock_node_cnt);
	block_level = ceil(log2(bblock_per_block));
	if (block_level > 0)
		llblock_level = bit_fls_from_bit(block_levels, block_level - 1);
	else
		llblock_level = 0;
	block_level = bit_ffs_from_bit(block_levels, block_level);

	xassert(llblock_level >= 0);

	bblock_per_llblock = (1 << llblock_level);
	llblock_size = bblock_per_llblock * bblock_node_cnt;
	max_llblock = (rem_nodes + llblock_size - 1) / llblock_size;

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   node_map)) {
			info("%pJ requires nodes which are not currently available",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   blocks_nodes_bitmap)) {
			info("%pJ requires nodes which are not in blocks",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%pJ required node list has no nodes",
			     job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (req_node_cnt > max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			     job_ptr, req_node_cnt,
			     max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
		req_nodes_bitmap = job_ptr->details->req_node_bitmap;
	}

	/*
	 * Add required nodes to job allocation and
	 * build list of node bitmaps, sorted by weight
	 */
	if (!bit_set_count(node_map)) {
		debug("%pJ node_map is empty",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	avail_cpu_per_node = xcalloc(node_record_count, sizeof(uint16_t));
	node_weight_list = list_create(_topo_weight_free);
	for (i = 0; (node_ptr = next_node_bitmap(node_map, &i)); i++) {
		topo_weight_info_t nw_static;
		if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i)) {
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			if (avail_cpus == 0) {
				debug2("%pJ insufficient resources on required node",
				       job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			avail_cpu_per_node[i] = avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
		}

		nw_static.weight = node_ptr->sched_weight;
		nw = list_find_first(node_weight_list, _topo_weight_find,
				     &nw_static);
		if (!nw) {	/* New node weight to add */
			nw = xmalloc(sizeof(topo_weight_info_t));
			nw->node_bitmap = bit_alloc(node_record_count);
			nw->weight = node_ptr->sched_weight;
			list_append(node_weight_list, nw);
		}
		bit_set(nw->node_bitmap, i);
		nw->node_cnt++;
	}

	list_sort(node_weight_list, _topo_weight_sort);
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)
		(void) list_for_each(node_weight_list, _topo_weight_log, NULL);

	if (block_level < 0) {
		/* Number of base blocks in block */
		bblock_per_block = block_record_cnt;
		block_cnt = 1;
	} else {
		/* Number of base blocks in block */
		bblock_per_block = (1 << block_level);
		block_cnt = (block_record_cnt + bblock_per_block - 1) /
			bblock_per_block;
	}

	if (bblock_per_block != (bblock_per_llblock * max_llblock)) {
		llblock_cnt = (block_record_cnt + bblock_per_llblock - 1) /
			      bblock_per_llblock;
		nodes_on_llblock = xcalloc(llblock_cnt, sizeof(uint32_t));
	}

	log_flag(SELECT_TYPE, "%s: bblock_per_block:%u rem_nodes:%u llblock_cnt:%u max_llblock:%d llblock_level:%d",
		 __func__, bblock_per_block, rem_nodes, llblock_cnt,
		 max_llblock, llblock_level);

	block_cpu_cnt = xcalloc(block_cnt, sizeof(uint32_t));
	block_gres = xcalloc(block_cnt, sizeof(List));
	block_node_bitmap = xcalloc(block_cnt, sizeof(bitstr_t *));
	block_node_cnt = xcalloc(block_cnt, sizeof(*block_node_cnt));
	bblock_required = bit_alloc(block_record_cnt);
	bblock_block_inx = xcalloc(block_record_cnt, sizeof(int));

	for (i = 0, block_ptr = block_record_table; i < block_record_cnt;
	     i++, block_ptr++) {
		int block_inx = i / bblock_per_block;
		if (block_node_bitmap[block_inx])
			bit_or(block_node_bitmap[block_inx],
			       block_ptr->node_bitmap);
		else
			block_node_bitmap[block_inx] =
				bit_copy(block_ptr->node_bitmap);
		bblock_block_inx[i] = block_inx;
		if (nodes_on_llblock) {
			int llblock_inx = i / bblock_per_llblock;
			nodes_on_llblock[llblock_inx] +=
				bit_overlap(block_ptr->node_bitmap, node_map);
		}
	}

	for (i = 0; i < block_cnt; i++) {
		uint32_t block_cpus = 0;
		bit_and(block_node_bitmap[i], node_map);
		if (!nodes_on_llblock) {
			block_node_cnt[i] = bit_set_count(block_node_bitmap[i]);
		} else {
			int llblock_per_block = (bblock_per_block /
						 bblock_per_llblock);
			int offset = i * llblock_per_block;
			qsort(&nodes_on_llblock[offset], llblock_per_block,
			      sizeof(int), _cmp_bblock);
			for (j = 0; j < max_llblock; j++)
				block_node_cnt[i] +=
					nodes_on_llblock[offset + j];
		}
		/*
		 * Count total CPUs of the intersection of node_map and
		 * block_node_bitmap.
		 */
		for (j = 0; (node_ptr = next_node_bitmap(block_node_bitmap[i],
							 &j));
		     j++)
			block_cpus += avail_res_array[j]->avail_cpus;
		block_cpu_cnt[i] = block_cpus;
		if (req_nodes_bitmap &&
		    bit_overlap_any(req_nodes_bitmap, block_node_bitmap[i])) {
			if (block_inx == -1) {
				block_inx = i;
				break;
			}
		}
		if (!_enough_nodes(block_node_cnt[i], rem_nodes,
				   min_nodes, req_nodes) ||
		    (rem_cpus > block_cpu_cnt[i]))
			continue;
		if (!req_nodes_bitmap &&
		    (nw = list_find_first(node_weight_list, _topo_node_find,
					  block_node_bitmap[i]))) {
			if ((block_inx == -1) ||
			    (nw->weight < block_lowest_weight) ||
			    ((nw->weight == block_lowest_weight) &&
			     (block_node_cnt[i] <=
			      block_node_cnt[block_inx]))) {
				block_inx = i;
				block_lowest_weight = nw->weight;
			}
		}
	}

	if (!req_nodes_bitmap) {
		bit_clear_all(node_map);
	}

	if (block_inx == -1) {
		log_flag(SELECT_TYPE, "%pJ unable to find block",
			 job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that all specificly required nodes are in one block  */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap, block_node_bitmap[block_inx])) {
		rc = SLURM_ERROR;
		info("%pJ requires nodes that do not have shared block",
		     job_ptr);
		goto fini;
	}

	if (req_nodes_bitmap) {
		int last_llblock = -1;
		bit_and(node_map, req_nodes_bitmap);

		for (i = 0; (i < block_record_cnt) && nodes_on_llblock; i++) {
			if (block_inx != bblock_block_inx[i])
				continue;
			if (bit_overlap_any(
				    req_nodes_bitmap,
				    block_record_table[i].node_bitmap)) {
				bit_set(bblock_required, i);
				if (!_bblocks_in_same_block(last_llblock, i,
							    llblock_level)) {
					max_llblock--;
					last_llblock = i;
				}
			}
		}
		if (max_llblock < 0) {
			rc = SLURM_ERROR;
			info("%pJ requires nodes exceed maximum llblock limit due to required nodes",
			     job_ptr);
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			info("%pJ requires nodes exceed maximum node limit",
			     job_ptr);
			goto fini;
		}
	}

	requested = false;
	best_node_cnt = 0;
	best_cpu_cnt = 0;
	best_nodes_bitmap = bit_alloc(node_record_count);
	iter = list_iterator_create(node_weight_list);
	while (!requested && (nw = list_next(iter))) {
		if (best_node_cnt > 0) {
			/*
			 * All of the lower priority nodes should be included
			 * in the job's allocation. Nodes from the next highest
			 * weight nodes are included only as needed.
			 */
			if (req2_nodes_bitmap)
				bit_or(req2_nodes_bitmap, best_nodes_bitmap);
			else
				req2_nodes_bitmap = bit_copy(best_nodes_bitmap);
		}

		if (!bit_set_count(nw->node_bitmap))
			continue;

		for (i = 0; (node_ptr = next_node_bitmap(nw->node_bitmap, &i));
		     i++) {
			if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i))
				continue;	/* Required node */
			if (!bit_test(block_node_bitmap[block_inx], i))
				continue;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			if (avail_cpus == 0) {
				bit_clear(nw->node_bitmap, i);
				continue;
			}
			bit_set(best_nodes_bitmap, i);
			avail_cpu_per_node[i] = avail_cpus;
			best_cpu_cnt += avail_cpus;
			best_node_cnt++;
			if (gres_per_job) {
				gres_sched_consec(
					&best_gres, job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list);
			}
		}

		if (!sufficient) {
			sufficient = (best_cpu_cnt >= rem_cpus) &&
				_enough_nodes(best_node_cnt, rem_nodes,
					      min_nodes, req_nodes);
			if (sufficient && gres_per_job) {
				sufficient = gres_sched_sufficient(
					job_ptr->gres_list_req,
					best_gres);
			}
		}
		requested = ((best_node_cnt >= rem_nodes) &&
			     (best_cpu_cnt >= rem_cpus) &&
			     (!gres_per_job ||
			      gres_sched_sufficient(job_ptr->gres_list_req,
						    best_gres)));

	}
	list_iterator_destroy(iter);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *gres_str = NULL, *gres_print = "";
		char *node_names;
		if (req_nodes_bitmap) {
			node_names = bitmap2node_name(req_nodes_bitmap);
			info("Required nodes:%s", node_names);
			xfree(node_names);
		}
		node_names = bitmap2node_name(best_nodes_bitmap);
		if (gres_per_job) {
			gres_str = gres_sched_str(best_gres);
			if (gres_str)
				gres_print = gres_str;
		}
		info("Best nodes:%s node_cnt:%d cpu_cnt:%d %s",
		     node_names, best_node_cnt, best_cpu_cnt, gres_print);
		xfree(node_names);
		xfree(gres_str);
	}
	if (!sufficient) {
		log_flag(SELECT_TYPE, "insufficient resources currently available for %pJ",
			 job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Add lowest weight nodes. Treat similar to required nodes for the job.
	 * Job will still need to add some higher weight nodes later.
	 */
	if (req2_nodes_bitmap) {
		int last_llblock = -1;
		for (i = 0;
		     next_node_bitmap(req2_nodes_bitmap, &i) && (max_nodes > 0);
		     i++) {
			avail_cpus = avail_cpu_per_node[i];
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
		}

		bit_or(node_map, req2_nodes_bitmap);

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    (!gres_per_job || gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))) {
			/* Required nodes completely satisfied the request */
			error("Scheduling anomaly for %pJ",
			      job_ptr);
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			debug("%pJ reached maximum node limit",
			      job_ptr);
			goto fini;
		}
		for (i = 0; i < block_record_cnt; i++) {
			if (block_inx != bblock_block_inx[i])
				continue;
			if (bit_test(bblock_required, i)) {
				last_llblock = i;
				continue;
			}
			if (bit_overlap_any(
				    req2_nodes_bitmap,
				    block_record_table[i].node_bitmap)) {
				bit_set(bblock_required, i);
				if (!_bblocks_in_same_block(last_llblock, i,
							    llblock_level)) {
					max_llblock--;
					last_llblock = i;
				}
			}

		}
	}

	if (max_llblock < 0) {
		rc = SLURM_ERROR;
		info("%pJ requires nodes exceed maximum llblock limit due to node weights",
		     job_ptr);
		goto fini;
	}
	/* Add additional resources for already required base block */
	if (req_nodes_bitmap || req2_nodes_bitmap) {
		for (i = 0; i < block_record_cnt; i++) {
			if (!bit_test(bblock_required, i))
				continue;
			if (!bblock_bitmap)
				bblock_bitmap = bit_copy(
					block_record_table[i].node_bitmap);
			else
				bit_copybits(bblock_bitmap,
					     block_record_table[i].node_bitmap);

			bit_and(bblock_bitmap, block_node_bitmap[block_inx]);
			bit_and(bblock_bitmap, best_nodes_bitmap);
			bit_and_not(bblock_bitmap, node_map);

			for (j = 0; next_node_bitmap(bblock_bitmap, &j); j++) {
				if (!avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[j], j, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[j]->
						sock_gres_list,
						&avail_cpus);
				}
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!gres_per_job ||
				     gres_sched_test(job_ptr->gres_list_req,
						     job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
			}
		}
	}

	nodes_on_bblock = xcalloc(block_record_cnt, sizeof(*nodes_on_bblock));
	bblock_node_bitmap = xcalloc(block_record_cnt, sizeof(bitstr_t *));

	if (nodes_on_llblock)
		memset(nodes_on_llblock, 0, llblock_cnt * sizeof(uint32_t));

	for (i = 0; i < block_record_cnt; i++) {
		if (block_inx != bblock_block_inx[i])
			continue;
		if (bit_test(bblock_required, i))
			continue;
		bblock_node_bitmap[i] =
			bit_copy(block_record_table[i].node_bitmap);
		bit_and(bblock_node_bitmap[i], block_node_bitmap[block_inx]);
		bit_and(bblock_node_bitmap[i], best_nodes_bitmap);
		nodes_on_bblock[i] = bit_set_count(bblock_node_bitmap[i]);
		if (nodes_on_llblock) {
			int llblock_inx = i / bblock_per_llblock;
			nodes_on_llblock[llblock_inx] += nodes_on_bblock[i];
		}
	}

	prev_rem_nodes = rem_nodes + 1;
	while (1) {
		int best_bblock_inx = -1;
		bool best_fit = false, best_same_block = true;
		bitstr_t *best_bblock_bitmap = NULL;
		if (prev_rem_nodes == rem_nodes)
			break; 	/* Stalled */
		prev_rem_nodes = rem_nodes;

		for (i = 0; i < block_record_cnt; i++) {
			if (block_inx != bblock_block_inx[i])
				continue;
			if (bit_test(bblock_required, i))
				continue;

			_choose_best_bblock(bblock_required, llblock_level,
					    rem_nodes, nodes_on_bblock,
					    nodes_on_llblock, i,
					    &best_same_block, &best_fit,
					    &best_bblock_inx);
		}

		log_flag(SELECT_TYPE, "%s: rem_nodes:%d  best_bblock_inx:%d",
			 __func__, rem_nodes, best_bblock_inx);
		if (best_bblock_inx == -1)
			break;

		if ((max_llblock <= 0) && !best_same_block) {
			log_flag(SELECT_TYPE, "%s: min_rem_nodes:%d can't add more bblocks due to llblock limit",
				 __func__, min_rem_nodes);
			break;
		}

		best_bblock_bitmap = bblock_node_bitmap[best_bblock_inx];
		bit_and_not(best_bblock_bitmap, node_map);
		bit_set(bblock_required, best_bblock_inx);
		/*
		 * NOTE: Ideally we would add nodes in order of resource
		 * availability rather than in order of bitmap position, but
		 * that would add even more complexity and overhead.
		 */
		for (i = 0; next_node_bitmap(best_bblock_bitmap, &i) &&
			     (max_nodes > 0); i++) {
			if (!avail_cpu_per_node[i])
				continue;
			avail_cpus = avail_cpu_per_node[i];
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			bit_set(node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    (!gres_per_job ||
			     gres_sched_test(job_ptr->gres_list_req,
					     job_ptr->job_id))) {
				rc = SLURM_SUCCESS;
				goto fini;
			}
		}
		if (!best_same_block)
			max_llblock--;
	}

	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!gres_per_job ||
	     gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:
	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	FREE_NULL_BITMAP(bblock_bitmap);
	xfree(avail_cpu_per_node);
	xfree(block_cpu_cnt);
	xfree(block_gres);
	xfree(bblock_block_inx);
	if (block_node_bitmap) {
		for (i = 0; i < block_cnt; i++)
			FREE_NULL_BITMAP(block_node_bitmap[i]);
		xfree(block_node_bitmap);
	}
	if (bblock_node_bitmap) {
		for (i = 0; i < block_record_cnt; i++)
			FREE_NULL_BITMAP(bblock_node_bitmap[i]);
		xfree(bblock_node_bitmap);
	}
	xfree(block_node_cnt);
	xfree(nodes_on_bblock);
	xfree(nodes_on_llblock);
	FREE_NULL_BITMAP(bblock_required);
	return rc;
}

/* Allocate resources to job using a minimal leaf switch count */
static int _eval_nodes_topo(job_record_t *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass)
{
	uint32_t *switch_cpu_cnt = NULL;	/* total CPUs on switch */
	bitstr_t **switch_node_bitmap = NULL;	/* nodes on this switch */
	bitstr_t **start_switch_node_bitmap = NULL;
	int       *switch_node_cnt = NULL;	/* total nodes on switch */
	int       *switch_required = NULL;	/* set if has required node */
	int *req_switch_required = NULL;
	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;	/* required node bitmap */
	bitstr_t  *req2_nodes_bitmap  = NULL;	/* required+lowest prio nodes */
	bitstr_t  *best_nodes_bitmap  = NULL;	/* required+low prio nodes */
	bitstr_t *start_node_map = NULL;
	int i, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt, best_node_cnt, req_node_cnt = 0;
	List best_gres = NULL;
	switch_record_t *switch_ptr;
	List node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	ListIterator iter;
	node_record_t *node_ptr;
	uint16_t avail_cpus = 0;
	int64_t rem_max_cpus, start_rem_max_cpus;
	int rem_cpus, start_rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	bool enforce_binding = false;
	job_details_t *details_ptr = job_ptr->details;
	bool gres_per_job, requested, sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	uint32_t *switches_dist= NULL;
	time_t time_waiting = 0;
	int top_switch_inx = -1;
	uint64_t top_switch_lowest_weight = 0;
	int prev_rem_nodes;
	uint32_t org_max_nodes = max_nodes;

	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   node_map)) {
			info("%pJ requires nodes which are not currently available",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%pJ required node list has no nodes",
			      job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (req_node_cnt > max_nodes) {
			info("%pJ requires more nodes than currently available (%u>%u)",
			      job_ptr, req_node_cnt,
			      max_nodes);
			rc = SLURM_ERROR;
			goto fini;
		}
		req_nodes_bitmap = job_ptr->details->req_node_bitmap;
	}

	/*
	 * Add required nodes to job allocation and
	 * build list of node bitmaps, sorted by weight
	 */
	if (!bit_set_count(node_map)) {
		debug("%pJ node_map is empty",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	avail_cpu_per_node = xcalloc(node_record_count, sizeof(uint16_t));
	node_weight_list = list_create(_topo_weight_free);
	for (i = 0; (node_ptr = next_node_bitmap(node_map, &i)); i++) {
		topo_weight_info_t nw_static;
		if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i)) {
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			if (avail_cpus == 0) {
				debug2("%pJ insufficient resources on required node",
				       job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			avail_cpu_per_node[i] = avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
		}

		nw_static.weight = node_ptr->sched_weight;
		nw = list_find_first(node_weight_list, _topo_weight_find,
				     &nw_static);
		if (!nw) {	/* New node weight to add */
			nw = xmalloc(sizeof(topo_weight_info_t));
			nw->node_bitmap = bit_alloc(node_record_count);
			nw->weight = node_ptr->sched_weight;
			list_append(node_weight_list, nw);
		}
		bit_set(nw->node_bitmap, i);
		nw->node_cnt++;
	}

	list_sort(node_weight_list, _topo_weight_sort);
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE)
		(void) list_for_each(node_weight_list, _topo_weight_log, NULL);

	/*
	 * Identify the highest level switch to be used.
	 * Note that nodes can be on multiple non-overlapping switches.
	 */
	switch_cpu_cnt = xcalloc(switch_record_cnt, sizeof(uint32_t));
	switch_node_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	start_switch_node_bitmap = xcalloc(switch_record_cnt, sizeof(bitstr_t *));
	switch_node_cnt    = xcalloc(switch_record_cnt, sizeof(int));
	switch_required    = xcalloc(switch_record_cnt, sizeof(int));
	req_switch_required = xcalloc(switch_record_cnt, sizeof(int));

	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		uint32_t switch_cpus = 0;
		switch_node_bitmap[i] = bit_copy(switch_ptr->node_bitmap);
		bit_and(switch_node_bitmap[i], node_map);
		switch_node_cnt[i] = bit_set_count(switch_node_bitmap[i]);
		/*
		 * Count total CPUs of the intersection of node_map and
		 * switch_node_bitmap.
		 */
		for (j = 0; (node_ptr = next_node_bitmap(switch_node_bitmap[i],
							 &j));
		     j++)
			switch_cpus += avail_res_array[j]->avail_cpus;
		switch_cpu_cnt[i] = switch_cpus;
		if (req_nodes_bitmap &&
		    bit_overlap_any(req_nodes_bitmap, switch_node_bitmap[i])) {
			switch_required[i] = 1;
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
		if (!_enough_nodes(switch_node_cnt[i], rem_nodes,
				   min_nodes, req_nodes) ||
		    (rem_cpus > switch_cpu_cnt[i]))
			continue;
		if (!req_nodes_bitmap &&
		    (nw = list_find_first(node_weight_list, _topo_node_find,
				    switch_node_bitmap[i]))) {
			if ((top_switch_inx == -1) ||
			    ((switch_record_table[i].level >=
			      switch_record_table[top_switch_inx].level) &&
			     (nw->weight <= top_switch_lowest_weight))) {
				top_switch_inx = i;
				top_switch_lowest_weight = nw->weight;
			}
		}
	}

	if (!req_nodes_bitmap) {
		bit_clear_all(node_map);
	}

	/*
	 * Top switch is highest level switch containing all required nodes
	 * OR all nodes of the lowest scheduling weight
	 * OR -1 if can not identify top-level switch, which may be due to a
	 * disjoint topology and available nodes living on different switches.
	 */
	if (top_switch_inx == -1) {
		log_flag(SELECT_TYPE, "%pJ unable to identify top level switch",
			 job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that all specificly required nodes are on shared network */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap,
			   switch_node_bitmap[top_switch_inx])) {
		rc = SLURM_ERROR;
		info("%pJ requires nodes that do not have shared network",
		     job_ptr);
		goto fini;
	}

	/*
	 * Remove nodes from consideration that can not be reached from this
	 * top level switch.
	 */
	for (i = 0; i < switch_record_cnt; i++) {
		if (top_switch_inx != i) {
			  bit_and(switch_node_bitmap[i],
				  switch_node_bitmap[top_switch_inx]);
		}
	}

	start_rem_cpus = rem_cpus;
	start_rem_max_cpus = rem_max_cpus;
	if (req_nodes_bitmap) {
		bit_and(node_map, req_nodes_bitmap);
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
				 job_ptr);
			goto fini;
		}
	}

	start_node_map = bit_copy(node_map);
	memcpy(req_switch_required, switch_required,
	       switch_record_cnt * sizeof(int));
	for (i = 0; i < switch_record_cnt; i++)
		start_switch_node_bitmap[i] = bit_copy(switch_node_bitmap[i]);

try_again:
	/*
	 * Identify the best set of nodes (i.e. nodes with the lowest weight,
	 * in addition to the required nodes) that can be used to satisfy the
	 * job request. All nodes must be on a common top-level switch. The
	 * logic here adds groups of nodes, all with the same weight, so we
	 * usually identify more nodes than required to satisfy the request.
	 * Later logic selects from those nodes to get the best topology.
	 */
	requested = false;
	best_node_cnt = 0;
	best_cpu_cnt = 0;
	best_nodes_bitmap = bit_alloc(node_record_count);
	iter = list_iterator_create(node_weight_list);
	while (!requested && (nw = list_next(iter))) {
		if (best_node_cnt > 0) {
			/*
			 * All of the lower priority nodes should be included
			 * in the job's allocation. Nodes from the next highest
			 * weight nodes are included only as needed.
			 */
			if (req2_nodes_bitmap)
				bit_or(req2_nodes_bitmap, best_nodes_bitmap);
			else
				req2_nodes_bitmap = bit_copy(best_nodes_bitmap);
		}

		if (!bit_set_count(nw->node_bitmap))
			continue;

		for (i = 0; (node_ptr = next_node_bitmap(nw->node_bitmap, &i));
		     i++) {
			if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i))
				continue;	/* Required node */
			if (!bit_test(switch_node_bitmap[top_switch_inx], i))
				continue;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			if (avail_cpus == 0) {
				bit_clear(nw->node_bitmap, i);
				continue;
			}
			bit_set(best_nodes_bitmap, i);
			avail_cpu_per_node[i] = avail_cpus;
			best_cpu_cnt += avail_cpus;
			best_node_cnt++;
			if (gres_per_job) {
				gres_sched_consec(
					&best_gres, job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list);
			}
		}

		if (!sufficient) {
			sufficient = (best_cpu_cnt >= rem_cpus) &&
				     _enough_nodes(best_node_cnt, rem_nodes,
						   min_nodes, req_nodes);
			if (sufficient && gres_per_job) {
				sufficient = gres_sched_sufficient(
						job_ptr->gres_list_req,
						best_gres);
			}
		}
		requested = ((best_node_cnt >= rem_nodes) &&
			     (best_cpu_cnt >= rem_cpus) &&
			     (!gres_per_job ||
			      gres_sched_sufficient(job_ptr->gres_list_req,
						    best_gres)));
	}
	list_iterator_destroy(iter);

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *gres_str = NULL, *gres_print = "";
		char *node_names;
		if (req_nodes_bitmap) {
			node_names = bitmap2node_name(req_nodes_bitmap);
			info("Required nodes:%s", node_names);
			xfree(node_names);
		}
		node_names = bitmap2node_name(best_nodes_bitmap);
		if (gres_per_job) {
			gres_str = gres_sched_str(best_gres);
			if (gres_str)
				gres_print = gres_str;
		}
		info("Best nodes:%s node_cnt:%d cpu_cnt:%d %s",
		     node_names, best_node_cnt, best_cpu_cnt, gres_print);
		xfree(node_names);
		xfree(gres_str);
	}
	if (!sufficient) {
		log_flag(SELECT_TYPE, "insufficient resources currently available for %pJ",
		      job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Add lowest weight nodes. Treat similar to required nodes for the job.
	 * Job will still need to add some higher weight nodes later.
	 */
	if (req2_nodes_bitmap) {
		for (i = 0;
		     next_node_bitmap(req2_nodes_bitmap, &i) && (max_nodes > 0);
		     i++) {
			avail_cpus = avail_cpu_per_node[i];
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
		}

		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_required[i])
				continue;
			if (bit_overlap_any(req2_nodes_bitmap,
					    switch_node_bitmap[i])) {
				switch_required[i] = 1;
			}
		}
		bit_or(node_map, req2_nodes_bitmap);

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    (!gres_per_job || gres_sched_test(job_ptr->gres_list_req,
						      job_ptr->job_id))) {
			/* Required nodes completely satisfied the request */
			error("Scheduling anomaly for %pJ",
			      job_ptr);
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			log_flag(SELECT_TYPE, "%pJ reached maximum node limit",
				 job_ptr);
			goto fini;
		}
	}

	/*
	 * Construct a set of switch array entries.
	 * Use the same indexes as switch_record_table in slurmctld.
	 */
	bit_or(best_nodes_bitmap, node_map);
	avail_nodes_bitmap = bit_alloc(node_record_count);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		bit_and(switch_node_bitmap[i], best_nodes_bitmap);
		bit_or(avail_nodes_bitmap, switch_node_bitmap[i]);
		switch_node_cnt[i] = bit_set_count(switch_node_bitmap[i]);
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < switch_record_cnt; i++) {
			char *node_names = NULL;
			if (switch_node_cnt[i]) {
				node_names =
					bitmap2node_name(switch_node_bitmap[i]);
			}
			info("switch=%s level=%d nodes=%u:%s required:%u speed:%u",
			     switch_record_table[i].name,
			     switch_record_table[i].level,
			     switch_node_cnt[i], node_names,
			     switch_required[i],
			     switch_record_table[i].link_speed);
			xfree(node_names);
		}
	}

	/* Add additional resources for already required leaf switches */
	if (req_nodes_bitmap || req2_nodes_bitmap) {
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			for (j = 0; next_node_bitmap(switch_node_bitmap[i], &j);
			     j++) {
				if (bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[j], j, cr_type);
				if (gres_per_job) {
					gres_sched_add(
						job_ptr->gres_list_req,
						avail_res_array[j]->
						sock_gres_list,
						&avail_cpus);
				}
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!gres_per_job ||
				     gres_sched_test(job_ptr->gres_list_req,
						     job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
			}
		}
	}

	switches_dist = xcalloc(switch_record_cnt, sizeof(uint32_t));

	for (i = 0; i < switch_record_cnt; i++) {
		if (switch_required[i])
			_topo_add_dist(switches_dist, i);
	}
	/* Add additional resources as required from additional leaf switches */
	prev_rem_nodes = rem_nodes + 1;
	while (1) {
		int best_switch_inx = -1;
		if (prev_rem_nodes == rem_nodes)
			break; 	/* Stalled */
		prev_rem_nodes = rem_nodes;

		for (i = 0; i < switch_record_cnt; i++) {
			if (switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			_topo_choose_best_switch(switches_dist, switch_node_cnt,
						 rem_nodes, switch_cpu_cnt,
						 rem_cpus, i, &best_switch_inx);

		}
		if (best_switch_inx == -1)
			break;

		_topo_add_dist(switches_dist, best_switch_inx);
		/*
		 * NOTE: Ideally we would add nodes in order of resource
		 * availability rather than in order of bitmap position, but
		 * that would add even more complexity and overhead.
		 */
		for (i = 0;
		     next_node_bitmap(
			     switch_node_bitmap[best_switch_inx], &i) &&
		     (max_nodes > 0);
		     i++) {
			if (bit_test(node_map, i) || !avail_cpu_per_node[i])
				continue;
			avail_cpus = avail_cpu_per_node[i];
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			bit_set(node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    (!gres_per_job ||
			     gres_sched_test(job_ptr->gres_list_req,
					     job_ptr->job_id))) {
				rc = SLURM_SUCCESS;
				goto fini;
			}
		}
		switch_node_cnt[best_switch_inx] = 0;	/* Used all */
	}
	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!gres_per_job ||
	     gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:
	if (job_ptr->req_switch > 0 && rc == SLURM_SUCCESS) {
		int leaf_switch_count = 0;

		/* Count up leaf switches. */
		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_record_table[i].level != 0)
				continue;
			if (bit_overlap_any(switch_node_bitmap[i], node_map))
				leaf_switch_count++;
		}
		if (time_waiting >= job_ptr->wait4switch) {
			job_ptr->best_switch = true;
			debug3("%pJ waited %ld sec for switches use=%d",
				job_ptr, time_waiting, leaf_switch_count);
		} else if (leaf_switch_count > job_ptr->req_switch) {
			/*
			 * Allocation is for more than requested number of
			 * switches.
			 */
			if ((req_nodes > min_nodes) && best_nodes_bitmap) {
				/* TRUE only for !gres_per_job */
				req_nodes--;
				rem_nodes = req_nodes;
				rem_nodes -= req_node_cnt;
				min_rem_nodes = min_nodes;
				min_rem_nodes -= req_node_cnt;
				max_nodes = org_max_nodes;
				max_nodes -= req_node_cnt;
				rem_cpus = start_rem_cpus;
				rem_max_cpus = start_rem_max_cpus;
				xfree(switches_dist);
				bit_copybits(node_map, start_node_map);
				memcpy(switch_required, req_switch_required,
				       switch_record_cnt * sizeof(int));
				memset(avail_cpu_per_node, 0,
				       node_record_count * sizeof(uint16_t));
				for (i = 0; i < switch_record_cnt; i++)
					bit_copybits(
						switch_node_bitmap[i],
						start_switch_node_bitmap[i]);
				FREE_NULL_BITMAP(avail_nodes_bitmap);
				FREE_NULL_BITMAP(req2_nodes_bitmap);
				FREE_NULL_BITMAP(best_nodes_bitmap);
				FREE_NULL_LIST(best_gres);
				log_flag(SELECT_TYPE, "%pJ goto try_again req_nodes %d",
					 job_ptr, req_nodes);
				goto try_again;
			}
			job_ptr->best_switch = false;
			debug3("%pJ waited %ld sec for switches=%u found=%d wait %u",
				job_ptr, time_waiting, job_ptr->req_switch,
				leaf_switch_count, job_ptr->wait4switch);
		} else {
			job_ptr->best_switch = true;
		}
	}

	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	FREE_NULL_BITMAP(start_node_map);
	xfree(avail_cpu_per_node);
	xfree(switch_cpu_cnt);
	if (switch_node_bitmap) {
		for (i = 0; i < switch_record_cnt; i++)
			FREE_NULL_BITMAP(switch_node_bitmap[i]);
		xfree(switch_node_bitmap);
	}
	if (start_switch_node_bitmap) {
		for (i = 0; i < switch_record_cnt; i++)
			FREE_NULL_BITMAP(start_switch_node_bitmap[i]);
		xfree(start_switch_node_bitmap);
	}
	xfree(switch_node_cnt);
	xfree(switch_required);
	xfree(req_switch_required);
	xfree(switches_dist);
	return rc;
}

static int _eval_nodes_lln(job_record_t *job_ptr,
			   gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			   bitstr_t **avail_core, uint32_t min_nodes,
			   uint32_t max_nodes, uint32_t req_nodes,
			   avail_res_t **avail_res_array, uint16_t cr_type,
			   bool prefer_alloc_nodes, bool first_pass)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	bool enforce_binding = false;

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

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
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->
					sock_gres_list, &avail_cpus);
			}
			if (avail_cpus <= 0) {
				debug("%pJ required node %s not available",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(node_map, req_map);
			goto fini;
		}
		if (max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, node_map);
	} else {
		bit_clear_all(node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	/*
	 * Accumulate nodes from those with highest available CPU count.
	 * Logic is optimized for small node/CPU count allocations.
	 * For larger allocation, use list_sort().
	 */
	if (max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = (node_weight_type *) list_next(iter))) {
		int last_max_cpu_cnt = -1;
		while (!all_done) {
			int max_cpu_idx = -1;
			uint16_t max_cpu_avail_cpus = 0;
			for (i = i_start; i <= i_end; i++) {
				/* Node not available or already selected */
				if (!bit_test(nwt->node_bitmap, i) ||
				    bit_test(node_map, i))
					continue;
				_select_cores(job_ptr, mc_ptr, enforce_binding,
					      i, &avail_cpus, max_nodes,
					      min_rem_nodes, avail_core,
					      avail_res_array, first_pass,
					      cr_type);
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
				if (avail_cpus == 0)
					continue;
				/*
				 * Find the "least-loaded" node at the current
				 * node-weight level. This is defined as the
				 * node with the greatest ratio of available to
				 * total cpus. (But shift the divisors around
				 * to avoid any floating-point math.)
				 */
				if ((max_cpu_idx == -1) ||
				    ((avail_res_array[max_cpu_idx]->max_cpus *
				      node_record_table_ptr[i]->cpus) <
				     (avail_res_array[i]->max_cpus *
				      node_record_table_ptr[max_cpu_idx]->cpus))) {
					max_cpu_idx = i;
					max_cpu_avail_cpus = avail_cpus;
					if (avail_res_array[max_cpu_idx]->
					    max_cpus == last_max_cpu_cnt)
						break;
				}
			}
			if ((max_cpu_idx == -1) ||
			    (max_cpu_avail_cpus == 0)) {
				/* No more usable nodes left, get next weight */
				break;
			}
			i = max_cpu_idx;
			avail_cpus = max_cpu_avail_cpus;
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			last_max_cpu_cnt = avail_res_array[i]->max_cpus;
			total_cpus += avail_cpus;
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			bit_set(node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_sched_test(job_ptr->gres_list_req,
					    job_ptr->job_id)) {
				error_code = SLURM_SUCCESS;
				all_done = true;
				break;
			}
			if (max_nodes == 0) {
				all_done = true;
				break;
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources at the end of the node
 * list to reduce fragmentation
 */
static int _eval_nodes_serial(job_record_t *job_ptr,
			      gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			      bitstr_t **avail_core, uint32_t min_nodes,
			      uint32_t max_nodes, uint32_t req_nodes,
			      avail_res_t **avail_res_array, uint16_t cr_type,
			      bool prefer_alloc_nodes, bool first_pass)
{
	int i, i_start, i_end, error_code = SLURM_ERROR;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	int64_t rem_max_cpus;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	node_record_t *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	bool enforce_binding = false;

	if (job_ptr->gres_list_req && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_sched_init(job_ptr->gres_list_req)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);
	rem_max_cpus = _get_rem_max_cpus(details_ptr, rem_nodes);

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
			node_ptr = node_record_table_ptr[i];
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (max_nodes <= 0) {
				log_flag(SELECT_TYPE, "%pJ requires nodes exceed maximum node limit",
					 job_ptr);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->
					sock_gres_list, &avail_cpus);
			}
			if (avail_cpus <= 0) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			/* leaving bitmap set, decr max limit */
			max_nodes--;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(node_map, req_map);
			goto fini;
		}
		if (max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
		bit_and_not(orig_node_map, node_map);
	} else {
		bit_clear_all(node_map);
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%pJ can't use required nodes due to max CPU limit",
		     job_ptr);
		goto fini;
	}

	if (max_nodes == 0)
		all_done = true;
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = (node_weight_type *) list_next(iter))) {
		for (i = i_end; ((i >= i_start) && (max_nodes > 0)); i--) {
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus)
				continue;
			/* Node not available or already selected */
			if (!bit_test(nwt->node_bitmap, i) ||
			    bit_test(node_map, i))
				continue;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass,
				      cr_type);
			_cpus_to_use(&avail_cpus, rem_max_cpus,
				     min_rem_nodes, details_ptr,
				     avail_res_array[i], i, cr_type);
			if (avail_cpus == 0)
				continue;
			total_cpus += avail_cpus;
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			bit_set(node_map, i);
			if (gres_per_job) {
				gres_sched_add(
					job_ptr->gres_list_req,
					avail_res_array[i]->sock_gres_list,
					&avail_cpus);
			}
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_sched_test(job_ptr->gres_list_req,
					    job_ptr->job_id)) {
				error_code = SLURM_SUCCESS;
				all_done = true;
				break;
			}
			if (max_nodes == 0) {
				all_done = true;
				break;
			}
		}
	}
	list_iterator_destroy(iter);

	if (error_code == SLURM_SUCCESS) {
		/* Already succeeded */
	} else if ((rem_cpus > 0) || (min_rem_nodes > 0) ||
		   !gres_sched_test(job_ptr->gres_list_req, job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(orig_node_map);
	return error_code;

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

/* List sort function: sort by the job's expected end time */
static int _cr_job_list_sort(void *x, void *y)
{
	job_record_t *job1_ptr = *(job_record_t **) x;
	job_record_t *job2_ptr = *(job_record_t **) y;

	return (int) (job1_ptr->end_time - job2_ptr->end_time);
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
	List node_gres_list;
	bitstr_t *part_core_map_ptr = NULL, *req_sock_map = NULL;
	avail_res_t *avail_res = NULL;
	List sock_gres_list = NULL;
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
		sock_gres_list = gres_sched_create_sock_gres_list(
					job_ptr->gres_list_req, node_gres_list,
					resv_exc_ptr,
					test_only, core_map[node_i],
					node_ptr->tot_sockets, node_ptr->cores,
					job_ptr->job_id, node_ptr->name,
					enforce_binding, s_p_n, &req_sock_map,
					job_ptr->user_id, node_i);
		if (!sock_gres_list) {	/* GRES requirement fail */
			log_flag(SELECT_TYPE, "Test fail on node %d: gres_sched_create_sock_gres_list",
			     node_i);
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
			(job_ptr->details->whole_node == 1),
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
			    (job_ptr->details->whole_node == 1)) {
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
			 int sharing_only, part_record_t *my_part_ptr,
			 bool qos_preemptor, List jobs)
{
	uint32_t r;
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

			if (!p_ptr->row[r].row_bitmap[node_i])
				continue;

			if (jobs &&
			    list_find_first(jobs, _is_job_sharing, NULL))
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
 * This is an intermediary step between _select_nodes() and _eval_nodes()
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low CPU counts for the job and re-evaluates each result.
 *
 * RET SLURM_SUCCESS or an error code
 */
static int _choose_nodes(job_record_t *job_ptr, bitstr_t *node_map,
			 bitstr_t **avail_core, uint32_t min_nodes,
			 uint32_t max_nodes, uint32_t req_nodes,
			 avail_res_t **avail_res_array, uint16_t cr_type,
			 bool prefer_alloc_nodes, gres_mc_data_t *tres_mc_ptr)
{
	int i, count, ec, most_res = 0, rem_nodes;
	bitstr_t *orig_node_map, *req_node_map = NULL;
	bitstr_t **orig_core_array;

	if (job_ptr->details->req_node_bitmap)
		req_node_map = job_ptr->details->req_node_bitmap;

	/* clear nodes from the bitmap that don't have available resources */
	for (i = 0; next_node_bitmap(node_map, &i); i++) {
		/*
		 * Make sure we don't say we can use a node exclusively
		 * that is bigger than our whole-job maximum CPU count.
		 */
		if (((job_ptr->details->whole_node == 1) &&
		     (job_ptr->details->max_cpus != NO_VAL) &&
		     (job_ptr->details->max_cpus <
		      avail_res_array[i]->avail_cpus)) ||
		/* OR node has no CPUs */
		    (avail_res_array[i]->avail_cpus < 1)) {

			if (req_node_map && bit_test(req_node_map, i)) {
				/* can't clear a required node! */
				return SLURM_ERROR;
			}
			bit_clear(node_map, i);
		}
	}

	if (job_ptr->details->num_tasks &&
	    !(job_ptr->details->ntasks_per_node) &&
	    (max_nodes > job_ptr->details->num_tasks))
		max_nodes = MAX(job_ptr->details->num_tasks, min_nodes);

	/*
	 * _eval_nodes() might need to be called more than once and is
	 * destructive of node_map and avail_core. Copy those bitmaps.
	 */
	orig_node_map = bit_copy(node_map);
	orig_core_array = copy_core_array(avail_core);

	ec = _eval_nodes(job_ptr, tres_mc_ptr, node_map, avail_core, min_nodes,
			 max_nodes, req_nodes, avail_res_array, cr_type,
			 prefer_alloc_nodes, true);
	if (ec == SLURM_SUCCESS)
		goto fini;
	bit_or(node_map, orig_node_map);
	core_array_or(avail_core, orig_core_array);

	rem_nodes = bit_set_count(node_map);
	if (rem_nodes <= min_nodes) {
		/* Can not remove any nodes, enable use of non-local GRES */
		ec = _eval_nodes(job_ptr, tres_mc_ptr, node_map, avail_core,
				 min_nodes, max_nodes, req_nodes,
				 avail_res_array, cr_type, prefer_alloc_nodes,
				 false);
		goto fini;
	}

	/*
	 * This nodeset didn't work. To avoid a possible knapsack problem,
	 * incrementally remove nodes with low resource counts (sum of CPU and
	 * GPU count if using GPUs, otherwise the CPU count) and retry
	 */
	for (i = 0; next_node(&i); i++) {
		if (avail_res_array[i]) {
			most_res = MAX(most_res,
				       avail_res_array[i]->avail_res_cnt);
		}
	}

	for (count = 1; count < most_res; count++) {
		int nochange = 1;
		bit_or(node_map, orig_node_map);
		core_array_or(avail_core, orig_core_array);
		for (i = 0; next_node_bitmap(node_map, &i); i++) {
			if ((avail_res_array[i]->avail_res_cnt > 0) &&
			    (avail_res_array[i]->avail_res_cnt <= count)) {
				if (req_node_map && bit_test(req_node_map, i))
					continue;
				nochange = 0;
				bit_clear(node_map, i);
				bit_clear(orig_node_map, i);
				if (--rem_nodes <= min_nodes)
					break;
			}
		}
		if (nochange && (count != 1))
			continue;
		ec = _eval_nodes(job_ptr, tres_mc_ptr, node_map, avail_core,
				 min_nodes, max_nodes, req_nodes,
				 avail_res_array, cr_type, prefer_alloc_nodes,
				 false);
		if (ec == SLURM_SUCCESS)
			break;
		if (rem_nodes <= min_nodes)
			break;
	}

fini:	if ((ec == SLURM_SUCCESS) && job_ptr->gres_list_req &&
	     orig_core_array) {
		/*
		 * Update available CPU count for any removed cores.
		 * Cores are only removed for jobs with GRES to enforce binding.
		 */
		for (i = 0; next_node_bitmap(node_map, &i); i++) {
			if (!orig_core_array[i] || !avail_core[i])
				continue;
			count = bit_set_count(avail_core[i]);
			count *= node_record_table_ptr[i]->tpc;
			avail_res_array[i]->avail_cpus =
				MIN(count, avail_res_array[i]->avail_cpus);
			if (avail_res_array[i]->avail_cpus == 0) {
				error("avail_cpus underflow for %pJ",
				      job_ptr);
				if (req_node_map && bit_test(req_node_map, i)) {
					/* can't clear a required node! */
					ec = SLURM_ERROR;
				}
				bit_clear(node_map, i);
			}
		}
	}
	FREE_NULL_BITMAP(orig_node_map);
	free_core_array(&orig_core_array);
	return ec;
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
				   gres_mc_data_t *tres_mc_ptr,
				   resv_exc_t *resv_exc_ptr)
{
	int i, rc;
	job_details_t *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	avail_res_t **avail_res_array;

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
					 will_run, part_core_map, resv_exc_ptr);
	if (!avail_res_array)
		return avail_res_array;

	/* Eliminate nodes that don't have sufficient resources for this job */
	for (int n = 0; next_node_bitmap(node_bitmap, &n); n++) {
		if ((!avail_res_array[n] ||
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
		i = ROUNDUP(details_ptr->num_tasks,
			    details_ptr->ntasks_per_node);
		min_nodes = MAX(min_nodes, i);
	}
	rc = _choose_nodes(job_ptr, node_bitmap, avail_core, min_nodes,
			   max_nodes, req_nodes, avail_res_array, cr_type,
			   prefer_alloc_nodes, tres_mc_ptr);
	if (rc != SLURM_SUCCESS)
		goto fini;

	core_array_log("_select_nodes/choose_nodes", node_bitmap, avail_core);

	/* If successful, sync up the avail_core with the node_map */
	if (rc == SLURM_SUCCESS) {
		int n;
		for (n = 0; n < bit_size(node_bitmap); n++) {
			if (!avail_res_array[n] ||
			    !bit_test(node_bitmap, n))
				FREE_NULL_BITMAP(avail_core[n]);
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
			      resv_exc_t *resv_exc_ptr, bool qos_preemptor)
{
	node_record_t *node_ptr;
	uint32_t gres_cpus, gres_cores;
	uint64_t free_mem, min_mem, avail_mem;
	List gres_list;
	bool disable_binding = false;

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
		if ((job_ptr->details->whole_node == 1) &&
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
					  job_ptr->part_ptr, qos_preemptor,
					  node_usage[i].jobs)) {
				debug3("node %s sharing?",
				       node_ptr->name);
				goto clear_bit;
			}

			/* node is NODE_CR_AVAILABLE - check job request */
		} else {
			if (job_node_req == NODE_CR_RESERVED) {
				if (_is_node_busy(cr_part_ptr, i, 0,
						  job_ptr->part_ptr,
						  qos_preemptor,
						  node_usage[i].jobs)) {
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
						  qos_preemptor,
						  node_usage[i].jobs)) {
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
		     node_use_record_t *node_usage, list_t *license_list,
		     resv_exc_t *resv_exc_ptr, bool prefer_alloc_nodes,
		     bool qos_preemptor, bool preempt_mode)
{
	int error_code = SLURM_SUCCESS;
	bitstr_t *orig_node_map, **part_core_map = NULL;
	bitstr_t **free_cores_tmp = NULL,  *node_bitmap_tmp = NULL;
	bitstr_t **free_cores_tmp2 = NULL, *node_bitmap_tmp2 = NULL;
	bitstr_t **avail_cores, **free_cores, **avail_cores_tmp = NULL;
	bool test_only = false, will_run = false;
	bool have_gres_max_tasks = false;
	uint32_t sockets_per_node = 1;
	uint32_t c, j, n, c_alloc = 0, c_size, total_cpus;
	uint64_t save_mem = 0, avail_mem = 0, needed_mem = 0, lowest_mem = 0;
	int32_t build_cnt;
	job_resources_t *job_res;
	job_details_t *details_ptr = job_ptr->details;
	part_res_record_t *p_ptr, *jp_ptr;
	uint16_t *cpu_count;
	int i;
	avail_res_t **avail_res_array, **avail_res_array_tmp;
	gres_mc_data_t *tres_mc_ptr = NULL;
	List *node_gres_list = NULL, *sock_gres_list = NULL;
	uint32_t *gres_task_limit = NULL;
	char *nodename = NULL;
	node_record_t *node_ptr;
	uint32_t orig_min_nodes = min_nodes;
	uint32_t next_job_size = 0;
	uint32_t ntasks_per_node;

	free_job_resources(&job_ptr->job_resrcs);

	if (mode == SELECT_MODE_TEST_ONLY)
		test_only = true;
	else if (mode == SELECT_MODE_WILL_RUN)
		will_run = true;

	/* check node_state and update the node_bitmap as necessary */
	if (!test_only) {
		error_code = _verify_node_state(
			cr_part_ptr, job_ptr, node_bitmap, cr_type,
			node_usage, job_node_req, resv_exc_ptr, qos_preemptor);
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
		if (start < 0 || start < orig_min_nodes)
			return SLURM_ERROR;
		max_nodes = start;
		min_nodes = max_nodes;
		req_nodes = max_nodes;
	}

	if (license_list) {
		/* Ensure job has access to requested licenses */
		int license_rc = license_job_test_with_list(job_ptr, time(NULL),
							    true, license_list);
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
	avail_cores = cons_helpers_mark_avail_cores(
		node_bitmap, job_ptr->details->core_spec);

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
	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr);
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
	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr);
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

	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	/* make these changes permanent */
	avail_cores_tmp = avail_cores;
	avail_cores = copy_core_array(free_cores);
	bit_copybits(orig_node_map, node_bitmap);

	avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
					req_nodes, node_bitmap, free_cores,
					node_usage, cr_type, test_only,
					will_run, part_core_map,
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr);
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
					prefer_alloc_nodes, tres_mc_ptr,
					resv_exc_ptr);
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
				prefer_alloc_nodes, tres_mc_ptr,
				resv_exc_ptr);
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
						tres_mc_ptr, resv_exc_ptr);
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
						tres_mc_ptr, resv_exc_ptr);
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
						tres_mc_ptr, resv_exc_ptr);
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
	for (i = 0, n = 0; (node_ptr = next_node_bitmap(node_bitmap, &i));
	     i++) {
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
				return SLURM_ERROR;
			}
			bit_set(job_res->core_bitmap, c);
			c_alloc++;
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
		node_gres_list = xcalloc(job_res->nhosts, sizeof(List));
		sock_gres_list = xcalloc(job_res->nhosts, sizeof(List));
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
	error_code = dist_tasks(job_ptr, cr_type, preempt_mode,
				avail_cores, gres_task_limit);
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
	if (job_ptr->details->whole_node == 1) {
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
		       cluster_license_list, NULL, false, false, false);
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
static void _set_sched_weight(bitstr_t *node_bitmap)
{
	node_record_t *node_ptr;

	for (int i = 0; (node_ptr = next_node_bitmap(node_bitmap, &i)); i++) {
		node_ptr->sched_weight = node_ptr->weight;
		node_ptr->sched_weight = node_ptr->sched_weight << 16;
		if (IS_NODE_COMPLETING(node_ptr))
			node_ptr->sched_weight |= 0x100;
		if (IS_NODE_REBOOT_REQUESTED(node_ptr) ||
		    IS_NODE_REBOOT_ISSUED(node_ptr))
			node_ptr->sched_weight |= 0x200;
		if (IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_DOWN(node_ptr))
			node_ptr->sched_weight |= 0x2000000000000;
	}
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
			  resv_exc_t *resv_exc_ptr)
{
	part_res_record_t *future_part;
	node_use_record_t *future_usage;
	list_t *future_license_list;
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

	_set_sched_weight(node_bitmap);

	/* Try to run with currently available nodes */
	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_WILL_RUN, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage,
		       cluster_license_list, resv_exc_ptr, false, false,
		       false);
	if (rc == SLURM_SUCCESS) {
		FREE_NULL_BITMAP(orig_map);
		job_ptr->start_time = now;
		return SLURM_SUCCESS;
	}

	/* Don't try preempting for licenses if not enabled */
	if ((rc == ESLURM_LICENSES_UNAVAILABLE) && !preempt_for_licenses)
		preemptee_candidates = NULL;

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

	future_license_list = license_copy(cluster_license_list);

	/* Build list of running and suspended jobs */
	cr_job_list = list_create(NULL);
	args = (cr_job_list_args_t) {
		.preemptee_candidates = preemptee_candidates,
		.cr_job_list = cr_job_list,
		.future_usage = future_usage,
		.future_part = future_part,
		.future_license_list = future_license_list,
		.orig_map = orig_map,
		.qos_preemptor = &qos_preemptor,
	};
	list_for_each(job_list, _build_cr_job_list, &args);

	/* Test with all preemptable jobs gone */
	if (preemptee_candidates) {
		bit_or(node_bitmap, orig_map);
		rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes,
			       req_nodes, SELECT_MODE_WILL_RUN, tmp_cr_type,
			       job_node_req, future_part, future_usage,
			       future_license_list, resv_exc_ptr, false,
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
					future_license_list, tmp_job_ptr, 0,
					orig_map);
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
				       future_license_list, resv_exc_ptr,
				       backfill_busy_nodes, qos_preemptor,
				       true);
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
	FREE_NULL_LIST(future_license_list);

	return rc;
}

/* Allocate resources for a job now, if possible */
static int _run_now(job_record_t *job_ptr, bitstr_t *node_bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    uint32_t req_nodes, uint16_t job_node_req,
		    List preemptee_candidates, List *preemptee_job_list,
		    resv_exc_t *resv_exc_ptr)
{
	int rc;
	bitstr_t *orig_node_map = NULL, *save_node_map;
	job_record_t *tmp_job_ptr = NULL;
	ListIterator job_iterator, preemptee_iterator;
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
		       preempt_mode);

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
		/* Try to schedule job using extra row of core bitmap */
		bit_or(node_bitmap, orig_node_map);
		rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes,
			       req_nodes, SELECT_MODE_RUN_NOW, tmp_cr_type,
			       job_node_req, select_part_record,
			       select_node_usage, cluster_license_list,
			       resv_exc_ptr, false, true, preempt_mode);
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
					    node_bitmap, job_ptr->license_list,
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
	uint32_t core_begin;
	uint32_t core_end;
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
	uint32_t used_cpu_array[sockets];
	uint16_t cpu_cnt[sockets];
	uint16_t max_cpu_per_req_sock = INFINITE16;
	avail_res_t *avail_res = xmalloc(sizeof(avail_res_t));
	bitstr_t *tmp_core = NULL;
	bool use_tpc = false;
	uint32_t socket_begin;
	uint32_t socket_end;


	core_begin = 0;
	core_end = node_ptr->tot_cores;

	memset(free_cores, 0, sockets * sizeof(uint16_t));
	memset(used_cores, 0, sockets * sizeof(uint16_t));
	memset(used_cpu_array, 0, sockets * sizeof(uint32_t));
	memset(cpu_cnt, 0, sockets * sizeof(uint16_t));

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

	socket_begin = core_begin;
	socket_end = core_begin + cores_per_socket;
	for (i = 0; i < sockets; i++) {
		free_cores[i] = bit_set_count_range(core_map, socket_begin,
						    socket_end);
		free_core_count += free_cores[i];
		if (!tmp_core) {
			used_cores[i] += (cores_per_socket - free_cores[i]);
		} else {
			used_cores[i] = bit_set_count_range(tmp_core,
							    socket_begin,
							    socket_end);
			used_cpu_array[i] = used_cores[i];
		}

		socket_begin = socket_end;
		socket_end += cores_per_socket;
		/*
		 * if a socket is already in use and entire_sockets_only is
		 * enabled or used_cpus reached MaxCPUsPerSocket partition limit
		 * the socket cannot be used by this job
		 */
		if ((entire_sockets_only && used_cores[i]) ||
		    ((used_cores[i] * threads_per_core) >=
		     job_ptr->part_ptr->max_cpus_per_socket)) {
			log_flag(SELECT_TYPE, "MaxCpusPerSocket: %u, CPUs already used on socket[%d]: %u - won't use the socket.",
				 job_ptr->part_ptr->max_cpus_per_socket,
				 i,
				 used_cpu_array[i]);
			free_core_count -= free_cores[i];
			used_cores[i] += free_cores[i];
			free_cores[i] = 0;
		}
		free_cpu_count += free_cores[i] * threads_per_core;
		if (used_cpu_array[i])
			used_cpu_count += used_cores[i] * threads_per_core;
	}
	avail_res->max_cpus = free_cpu_count;
	FREE_NULL_BITMAP(tmp_core);

	/* Enforce partition CPU limit, but do not pick specific cores yet */
	if ((job_ptr->part_ptr->max_cpus_per_node != INFINITE) &&
	    (free_cpu_count + used_cpu_count >
	     job_ptr->part_ptr->max_cpus_per_node)) {

		if (job_ptr->details->whole_node) {
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
	threads_per_core = cons_helpers_cpus_per_core(details_ptr, node_i);

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
	for (c = core_begin; c < core_end ; c++) {
		if (!bit_test(core_map, c) || (tmp_core &&
					       bit_test(tmp_core, c)))
			continue;

		/* Socket index */
		i = (uint16_t) ((c - core_begin) / cores_per_socket);
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
		bit_nclear(core_map, core_begin, core_end-1);
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
	socket_begin = core_begin;
	socket_end = core_begin + cores_per_socket;
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
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
 * IN resv_exc_ptr - Various TRES which the job can NOT use.
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
		    List preemptee_candidates,
		    List *preemptee_job_list,
		    resv_exc_t *resv_exc_ptr)
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
				    resv_exc_ptr);
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
