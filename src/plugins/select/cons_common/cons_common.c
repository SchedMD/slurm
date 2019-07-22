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
#include "dist_tasks.h"

#include "src/common/assoc_mgr.h"
#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/slurm_topology.h"
#include "src/common/xstring.h"

#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/preempt.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
extern struct node_record *node_record_table_ptr __attribute__((weak_import));
extern List part_list __attribute__((weak_import));
extern List job_list __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern time_t last_node_update __attribute__((weak_import));
extern struct switch_record *switch_record_table __attribute__((weak_import));
extern int switch_record_cnt __attribute__((weak_import));
extern bitstr_t *avail_node_bitmap __attribute__((weak_import));
extern uint16_t *cr_node_num_cores __attribute__((weak_import));
extern uint32_t *cr_node_cores_offset __attribute__((weak_import));
extern int slurmctld_tres_cnt __attribute__((weak_import));
extern slurmctld_config_t slurmctld_config __attribute__((weak_import));
#else
slurm_ctl_conf_t slurmctld_conf;
struct node_record *node_record_table_ptr;
List part_list;
List job_list;
int node_record_count;
time_t last_node_update;
struct switch_record *switch_record_table;
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
uint16_t select_fast_schedule = 0;
int      select_node_cnt      = 0;
bool     spec_cores_first     = false;
bool     topo_optional        = false;

typedef enum {
	HANDLE_JOB_RES_ADD,
	HANDLE_JOB_RES_REM,
	HANDLE_JOB_RES_TEST
} handle_job_res_t;

struct sort_support {
	int jstart;
	struct job_resources *tmpjobs;
};

struct part_res_record *select_part_record = NULL;
struct node_res_record *select_node_record = NULL;
struct node_use_record *select_node_usage  = NULL;

/* Global variables */
static uint64_t   def_cpu_per_gpu	= 0;
static uint64_t   def_mem_per_gpu	= 0;
static int        preempt_reorder_cnt	= 1;
static bool       preempt_strict_order = false;
static bool       select_state_initializing = true;

/* Delete the given select_node_record and select_node_usage arrays */
static void _destroy_node_data(struct node_use_record *node_usage,
			       struct node_res_record *node_data)
{
	int i;

	xfree(node_data);
	if (node_usage) {
		for (i = 0; i < select_node_cnt; i++) {
			FREE_NULL_LIST(node_usage[i].gres_list);
		}
		xfree(node_usage);
	}
}

/* Delete the given list of partition data */
static void _destroy_part_data(struct part_res_record *this_ptr)
{
	while (this_ptr) {
		struct part_res_record *tmp = this_ptr;
		this_ptr = this_ptr->next;
		tmp->part_ptr = NULL;

		if (tmp->row) {
			common_destroy_row_data(tmp->row, tmp->num_rows);
			tmp->row = NULL;
		}
		xfree(tmp);
	}
}

/* Create a duplicate part_res_record list */
static struct part_res_record *_dup_part_data(struct part_res_record *orig_ptr)
{
	struct part_res_record *new_part_ptr, *new_ptr;

	if (orig_ptr == NULL)
		return NULL;

	new_part_ptr = xmalloc(sizeof(struct part_res_record));
	new_ptr = new_part_ptr;

	while (orig_ptr) {
		new_ptr->part_ptr = orig_ptr->part_ptr;
		new_ptr->num_rows = orig_ptr->num_rows;
		new_ptr->row = common_dup_row_data(orig_ptr->row,
						   orig_ptr->num_rows);
		if (orig_ptr->next) {
			new_ptr->next = xmalloc(sizeof(struct part_res_record));
			new_ptr = new_ptr->next;
		}
		orig_ptr = orig_ptr->next;
	}
	return new_part_ptr;
}

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

/* helper script for common_sort_part_rows() */
static void _swap_rows(struct part_row_data *a, struct part_row_data *b)
{
	struct part_row_data tmprow;

	memcpy(&tmprow, a, sizeof(struct part_row_data));
	memcpy(a, b, sizeof(struct part_row_data));
	memcpy(b, &tmprow, sizeof(struct part_row_data));
}

/*
 * Sort the usable_node element to put jobs in the correct
 * preemption order.
 */
static int _sort_usable_nodes_dec(void *j1, void *j2)
{
	struct job_record *job_a = *(struct job_record **)j1;
	struct job_record *job_b = *(struct job_record **)j2;

	if (job_a->details->usable_nodes > job_b->details->usable_nodes)
		return -1;
	else if (job_a->details->usable_nodes < job_b->details->usable_nodes)
		return 1;

	return 0;
}

/* List sort function: sort by the job's expected end time */
static int _cr_job_list_sort(void *x, void *y)
{
	struct job_record *job1_ptr = *(struct job_record **) x;
	struct job_record *job2_ptr = *(struct job_record **) y;

	return (int) SLURM_DIFFTIME(job1_ptr->end_time, job2_ptr->end_time);
}

static struct multi_core_data * _create_default_mc(void)
{
	struct multi_core_data *mc_ptr;

	mc_ptr = xmalloc(sizeof(struct multi_core_data));
	mc_ptr->sockets_per_node = NO_VAL16;
	mc_ptr->cores_per_socket = NO_VAL16;
	mc_ptr->threads_per_core = NO_VAL16;
	/* Other fields initialized to zero by xmalloc */

	return mc_ptr;
}

/* Determine the node requirements for the job:
 * - does the job need exclusive nodes? (NODE_CR_RESERVED)
 * - can the job run on shared nodes?   (NODE_CR_ONE_ROW)
 * - can the job run on overcommitted resources? (NODE_CR_AVAILABLE)
 */
static uint16_t _get_job_node_req(struct job_record *job_ptr)
{
	int max_share = job_ptr->part_ptr->max_share;

	if (max_share == 0)		    /* Partition Shared=EXCLUSIVE */
		return NODE_CR_RESERVED;

	/* Partition is Shared=FORCE */
	if (max_share & SHARED_FORCE)
		return NODE_CR_AVAILABLE;

	if ((max_share > 1) && (job_ptr->details->share_res == 1))
		/* part allows sharing, and the user has requested it */
		return NODE_CR_AVAILABLE;

	return NODE_CR_ONE_ROW;
}

/*
 * Return true if job is in the processing of cleaning up.
 * This is used for Cray systems to indicate the Node Health Check (NHC)
 * is still running. Until NHC completes, the job's resource use persists
 * the select/cons_res plugin data structures.
 */
static bool _job_cleaning(struct job_record *job_ptr)
{
	uint16_t cleaning = 0;

	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_CLEANING,
				    &cleaning);
	if (cleaning)
		return true;
	return false;
}

/* Create a duplicate part_res_record list */
static struct node_use_record *_dup_node_usage(struct node_use_record *orig_ptr)
{
	struct node_use_record *new_use_ptr, *new_ptr;
	List gres_list;
	uint32_t i;

	if (orig_ptr == NULL)
		return NULL;

	new_use_ptr = xcalloc(select_node_cnt, sizeof(struct node_use_record));
	new_ptr = new_use_ptr;

	for (i = 0; i < select_node_cnt; i++) {
		new_ptr[i].node_state   = orig_ptr[i].node_state;
		new_ptr[i].alloc_memory = orig_ptr[i].alloc_memory;
		if (orig_ptr[i].gres_list)
			gres_list = orig_ptr[i].gres_list;
		else
			gres_list = node_record_table_ptr[i].gres_list;
		new_ptr[i].gres_list = gres_plugin_node_state_dup(gres_list);
	}
	return new_use_ptr;
}

static char *_node_state_str(uint16_t node_state)
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

static inline void _dump_nodes(void)
{
	struct node_record *node_ptr;
	List gres_list;
	int i;

	if (!(select_debug_flags & DEBUG_FLAG_SELECT_TYPE))
		return;

	for (i = 0; i < select_node_cnt; i++) {
		node_ptr = select_node_record[i].node_ptr;
		info("Node:%s Boards:%u SocketsPerBoard:%u CoresPerSocket:%u ThreadsPerCore:%u TotalCores:%u CumeCores:%u TotalCPUs:%u PUsPerCore:%u AvailMem:%"PRIu64" AllocMem:%"PRIu64" State:%s(%d)",
		     node_ptr->name,
		     select_node_record[i].boards,
		     select_node_record[i].sockets,
		     select_node_record[i].cores,
		     select_node_record[i].threads,
		     select_node_record[i].tot_cores,
		     select_node_record[i].cume_cores,
		     select_node_record[i].cpus,
		     select_node_record[i].vpus,
		     select_node_record[i].real_memory,
		     select_node_usage[i].alloc_memory,
		     _node_state_str(select_node_usage[i].node_state),
		     select_node_usage[i].node_state);

		if (select_node_usage[i].gres_list)
			gres_list = select_node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		if (gres_list)
			gres_plugin_node_state_log(gres_list, node_ptr->name);
	}
}

static void _log_tres_state(struct node_use_record *node_usage,
			    struct part_res_record *part_record_ptr)
{
#if _DEBUG
	struct part_res_record *p_ptr;
	struct part_row_data *row;
	char *core_str;
	int i;

	for (i = 0; i < select_node_cnt; i++) {
		info("Node:%s State:%s AllocMem:%"PRIu64" of %"PRIu64,
		     node_record_table_ptr[i].name,
		     _node_state_str(node_usage[i].node_state),
		     node_usage[i].alloc_memory,
		     select_node_record[i].real_memory);
	}

	for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
		info("Part:%s Rows:%u", p_ptr->part_ptr->name, p_ptr->num_rows);
		if (!(row = p_ptr->row))	/* initial/unused state */
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			core_str = _build_core_str(row[i].row_bitmap);
			info("  Row:%d Jobs:%u Cores:%s",
			     i, row[i].num_jobs, core_str);
			xfree(core_str);
		}
	}
#endif
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
static avail_res_t *_allocate_sc(struct job_record *job_ptr, bitstr_t *core_map,
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
	}

	return avail_res;
}

static int _sort_part_prio(void *x, void *y)
{
	struct part_res_record *part1 = *(struct part_res_record **) x;
	struct part_res_record *part2 = *(struct part_res_record **) y;

	if (part1->part_ptr->priority_tier > part2->part_ptr->priority_tier)
		return -1;
	if (part1->part_ptr->priority_tier < part2->part_ptr->priority_tier)
		return 1;
	return 0;
}

/* (re)create the global select_part_record array */
static void _create_part_data(void)
{
	List part_rec_list = NULL;
	ListIterator part_iterator;
	struct part_record *p_ptr;
	struct part_res_record *this_ptr, *last_ptr = NULL;
	int num_parts;

	_destroy_part_data(select_part_record);
	select_part_record = NULL;

	num_parts = list_count(part_list);
	if (!num_parts)
		return;
	info("%s: preparing for %d partitions", plugin_type, num_parts);

	part_rec_list = list_create(NULL);
	part_iterator = list_iterator_create(part_list);
	while ((p_ptr = list_next(part_iterator))) {
		this_ptr = xmalloc(sizeof(struct part_res_record));
		this_ptr->part_ptr = p_ptr;
		this_ptr->num_rows = p_ptr->max_share;
		if (this_ptr->num_rows & SHARED_FORCE)
			this_ptr->num_rows &= (~SHARED_FORCE);
		if (preempt_by_qos)	/* Add row for QOS preemption */
			this_ptr->num_rows++;
		/* SHARED=EXCLUSIVE sets max_share = 0 */
		if (this_ptr->num_rows < 1)
			this_ptr->num_rows = 1;
		/* we'll leave the 'row' array blank for now */
		this_ptr->row = NULL;
		list_append(part_rec_list, this_ptr);
	}
	list_iterator_destroy(part_iterator);

	/* Sort the select_part_records by priority */
	list_sort(part_rec_list, _sort_part_prio);
	part_iterator = list_iterator_create(part_rec_list);
	while ((this_ptr = list_next(part_iterator))) {
		if (last_ptr)
			last_ptr->next = this_ptr;
		else
			select_part_record = this_ptr;
		last_ptr = this_ptr;
	}
	list_iterator_destroy(part_iterator);
	list_destroy(part_rec_list);
}

/*
 * Get configured DefCpuPerGPU information from a list
 * (either global or per partition list)
 * Returns NO_VAL64 if configuration parameter not set
 */
static uint64_t _get_def_cpu_per_gpu(List job_defaults_list)
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
static uint64_t _get_def_mem_per_gpu(List job_defaults_list)
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

static void _set_gpu_defaults(struct job_record *job_ptr)
{
	static struct part_record *last_part_ptr = NULL;
	static uint64_t last_cpu_per_gpu = NO_VAL64;
	static uint64_t last_mem_per_gpu = NO_VAL64;
	uint64_t cpu_per_gpu, mem_per_gpu;

	if (!is_cons_tres || !job_ptr->gres_list)
		return;

	if (job_ptr->part_ptr != last_part_ptr) {
		/* Cache data from last partition referenced */
		last_part_ptr = job_ptr->part_ptr;
		last_cpu_per_gpu = _get_def_cpu_per_gpu(
			last_part_ptr->job_defaults_list);
		last_mem_per_gpu = _get_def_mem_per_gpu(
			last_part_ptr->job_defaults_list);
	}
	if (last_cpu_per_gpu != NO_VAL64)
		cpu_per_gpu = last_cpu_per_gpu;
	else if (def_cpu_per_gpu != NO_VAL64)
		cpu_per_gpu = def_cpu_per_gpu;
	else
		cpu_per_gpu = 0;
	if (last_mem_per_gpu != NO_VAL64)
		mem_per_gpu = last_mem_per_gpu;
	else if (def_mem_per_gpu != NO_VAL64)
		mem_per_gpu = def_mem_per_gpu;
	else
		mem_per_gpu = 0;

	gres_plugin_job_set_defs(job_ptr->gres_list, "gpu", cpu_per_gpu,
				 mem_per_gpu);
}

/* For a given job already past it's end time, guess when it will actually end.
 * Used for backfill scheduling. */
static time_t _guess_job_end(struct job_record * job_ptr, time_t now)
{
	time_t end_time;
	uint16_t over_time_limit;

	if (job_ptr->part_ptr &&
	    (job_ptr->part_ptr->over_time_limit != NO_VAL16)) {
		over_time_limit = job_ptr->part_ptr->over_time_limit;
	} else {
		over_time_limit = slurmctld_conf.over_time_limit;
	}
	if (over_time_limit == 0) {
		end_time = job_ptr->end_time + slurmctld_conf.kill_wait;
	} else if (over_time_limit == INFINITE16) {
		/* No idea when the job might end, this is just a guess */
		if (job_ptr->time_limit && (job_ptr->time_limit != NO_VAL) &&
		    (job_ptr->time_limit != INFINITE)) {
			end_time = now + (job_ptr->time_limit * 60);
		} else {
			end_time = now + (365 * 24 * 60 * 60);	/* one year */
		}
	} else {
		end_time = job_ptr->end_time + slurmctld_conf.kill_wait +
			   (over_time_limit  * 60);
	}
	if (end_time <= now)
		end_time = now + 1;

	return end_time;
}

/* Determine how many sockets per node this job requires for GRES */
static uint32_t _socks_per_node(struct job_record *job_ptr)
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
	if (!is_cons_tres && ((job_ptr->gres_list == NULL) ||
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
 * IN: test_only     - ignore allocated memory check
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 *
 * RET array of avail_res_t pointers, free using common_free_avail_res_array()
 */
static avail_res_t **_get_res_avail(struct job_record *job_ptr,
				    bitstr_t *node_map, bitstr_t **core_map,
				    struct node_use_record *node_usage,
				    uint16_t cr_type, bool test_only,
				    bitstr_t **part_core_map)
{
	int i, i_first, i_last;
	avail_res_t **avail_res_array = NULL;
	uint32_t s_p_n = _socks_per_node(job_ptr);

	xassert(*cons_common_callbacks.can_job_run_on_node);

	_set_gpu_defaults(job_ptr);
	avail_res_array = xcalloc(select_node_cnt, sizeof(avail_res_t *));
	i_first = bit_ffs(node_map);
	if (i_first != -1)
		i_last = bit_fls(node_map);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_map, i))
			continue;
		avail_res_array[i] =
			(*cons_common_callbacks.can_job_run_on_node)(
				job_ptr, core_map, i,
				s_p_n, node_usage,
				cr_type, test_only,
				part_core_map);
	}

	return avail_res_array;
}

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
			last_core = select_node_record[i_node].tot_cores;
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

static int _find_job (void *x, void *key)
{
	struct job_record *job_ptr = (struct job_record *) x;
	if (job_ptr == (struct job_record *) key)
		return 1;
	return 0;
}

static bool _is_preemptable(struct job_record *job_ptr,
			    List preemptee_candidates)
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
 * IN: test_only    - ignore allocated memory check
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * IN: prefer_alloc_nodes - select currently allocated nodes first
 * IN: tres_mc_ptr   - job's multi-core options
 * RET: array of avail_res_t pointers, free using common_free_avail_res_array().
 *	NULL on error
 */
static avail_res_t **_select_nodes(struct job_record *job_ptr,
				   uint32_t min_nodes, uint32_t max_nodes,
				   uint32_t req_nodes,
				   bitstr_t *node_bitmap, bitstr_t **avail_core,
				   struct node_use_record *node_usage,
				   uint16_t cr_type, bool test_only,
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
		info("%s: AvailNodes < MinNodes (%u < %u)", __func__,
		     bit_set_count(node_bitmap), min_nodes);
#endif
		return NULL;
	}

	core_array_log("_select_nodes/enter", node_bitmap, avail_core);
	/* Determine resource availability on each node for pending job */
	avail_res_array = _get_res_avail(job_ptr, node_bitmap, avail_core,
					 node_usage, cr_type, test_only,
					 part_core_map);
	if (!avail_res_array)
		return avail_res_array;

	/* Eliminate nodes that don't have sufficient resources for this job */
	for (n = 0; n < select_node_cnt; n++) {
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
		if (is_cons_tres) {
			for (n = 0; n < select_node_cnt; n++) {
				if (!avail_res_array[n] ||
				    !bit_test(node_bitmap, n))
					FREE_NULL_BITMAP(avail_core[n]);
			}
		} else {
			uint32_t i_first, i_last, n, start;
			i_first = bit_ffs(node_bitmap);
			if (i_first != -1)
				i_last = bit_fls(node_bitmap);
			else
				i_last = -2;
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
		common_free_avail_res_array(avail_res_array);
		return NULL;
	}

	return avail_res_array;
}

static uint16_t _valid_uint16(uint16_t arg)
{
	if ((arg == NO_VAL16) || (arg == INFINITE16))
		return 0;
	return arg;
}

static gres_mc_data_t *_build_gres_mc_data(struct job_record *job_ptr)
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
	    (slurmctld_conf.select_type_param & CR_ONE_TASK_PER_CORE))
		tres_mc_ptr->ntasks_per_core = 1;

	return tres_mc_ptr;
}

/*
 * Test to see if a node already has running jobs for _other_ partitions.
 * If (sharing_only) then only check sharing partitions. This is because
 * the job was submitted to a single-row partition which does not share
 * allocated CPUs with multi-row partitions.
 */
static int _is_node_busy(struct part_res_record *p_ptr, uint32_t node_i,
			 int sharing_only, struct part_record *my_part_ptr,
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
			if (is_cons_tres) {
				if (!p_ptr->row[r].row_bitmap ||
				    !p_ptr->row[r].row_bitmap[node_i])
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
static int _verify_node_state(struct part_res_record *cr_part_ptr,
			      struct job_record *job_ptr,
			      bitstr_t *node_bitmap,
			      uint16_t cr_type,
			      struct node_use_record *node_usage,
			      enum node_cr_state job_node_req,
			      bitstr_t **exc_cores, bool qos_preemptor)
{
	struct node_record *node_ptr;
	uint32_t gres_cpus, gres_cores;
	uint64_t free_mem, min_mem, avail_mem;
	List gres_list;
	int i, i_first, i_last;

	if (is_cons_tres && !(job_ptr->bit_flags & JOB_MEM_SET) &&
	    (min_mem = gres_plugin_job_mem_max(job_ptr->gres_list))) {
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

	i_first = bit_ffs(node_bitmap);
	if (i_first == -1)
		i_last = -2;
	else
		i_last  = bit_fls(node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		node_ptr = select_node_record[i].node_ptr;
		/* node-level memory check */
		if ((job_ptr->details->pn_min_memory) &&
		    (cr_type & CR_MEMORY)) {
			avail_mem = select_node_record[i].real_memory -
				    select_node_record[i].mem_spec_limit;
			if (avail_mem > node_usage[i].alloc_memory) {
				free_mem = avail_mem -
					   node_usage[i].alloc_memory;
			} else
				free_mem = 0;
			if (free_mem < min_mem) {
				debug3("%s: %s: node %s no mem (%"PRIu64" < %"PRIu64")",
					plugin_type, __func__,
					node_ptr->name,
					free_mem, min_mem);
				goto clear_bit;
			}
		} else if (cr_type & CR_MEMORY) {   /* --mem=0 for all memory */
			if (node_usage[i].alloc_memory) {
				debug3("%s: %s: node %s mem in use %"PRIu64,
					plugin_type, __func__,
					node_ptr->name,
					node_usage[i].alloc_memory);
				goto clear_bit;
			}
		}

		/* Exclude nodes with reserved cores */
		if ((job_ptr->details->whole_node == 1) && exc_cores) {
			if (is_cons_tres) {
				if (exc_cores[i] &&
				    (bit_ffs(exc_cores[i]) != -1)) {
					debug3("%s: %s: node %s exclusive",
					       plugin_type,
					       __func__,
					       node_ptr->name);
					goto clear_bit;
				}
			} else if (*exc_cores) {
				for (int j = cr_get_coremap_offset(i);
				     j < cr_get_coremap_offset(i+1);
				     j++) {
					if (bit_test(*exc_cores, j))
						continue;
					debug3("%s: %s: _vns: node %s exc",
					       plugin_type, __func__,
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
		gres_cores = gres_plugin_job_test(job_ptr->gres_list,
						  gres_list, true,
						  NULL, 0, 0, job_ptr->job_id,
						  node_ptr->name);
		gres_cpus = gres_cores;
		if (gres_cpus != NO_VAL)
			gres_cpus *= select_node_record[i].vpus;
		if (gres_cpus == 0) {
			debug3("%s: %s: node %s lacks GRES",
			       plugin_type, __func__, node_ptr->name);
			goto clear_bit;
		}

		/* exclusive node check */
		if (node_usage[i].node_state >= NODE_CR_RESERVED) {
			debug3("%s: %s: node %s in exclusive use",
			       plugin_type, __func__, node_ptr->name);
			goto clear_bit;

		/* non-resource-sharing node check */
		} else if (node_usage[i].node_state >= NODE_CR_ONE_ROW) {
			if ((job_node_req == NODE_CR_RESERVED) ||
			    (job_node_req == NODE_CR_AVAILABLE)) {
				debug3("%s: %s: node %s non-sharing",
				       plugin_type, __func__, node_ptr->name);
				goto clear_bit;
			}
			/*
			 * cannot use this node if it is running jobs
			 * in sharing partitions
			 */
			if (_is_node_busy(cr_part_ptr, i, 1,
					  job_ptr->part_ptr, qos_preemptor)) {
				debug3("%s: %s: node %s sharing?",
				       plugin_type, __func__, node_ptr->name);
				goto clear_bit;
			}

		/* node is NODE_CR_AVAILABLE - check job request */
		} else {
			if (job_node_req == NODE_CR_RESERVED) {
				if (_is_node_busy(cr_part_ptr, i, 0,
						  job_ptr->part_ptr,
						  qos_preemptor)) {
					debug3("%s: %s: node %s busy",
					       plugin_type, __func__,
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
					debug3("%s: %s: node %s vbusy",
					       plugin_type, __func__,
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
static int _job_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
		     uint32_t min_nodes, uint32_t max_nodes,
		     uint32_t req_nodes, int mode, uint16_t cr_type,
		     enum node_cr_state job_node_req,
		     struct part_res_record *cr_part_ptr,
		     struct node_use_record *node_usage,
		     bitstr_t **exc_cores, bool prefer_alloc_nodes,
		     bool qos_preemptor, bool preempt_mode)
{
	int error_code = SLURM_SUCCESS;
	bitstr_t *orig_node_map, **part_core_map = NULL;
	bitstr_t **free_cores_tmp = NULL,  *node_bitmap_tmp = NULL;
	bitstr_t **free_cores_tmp2 = NULL, *node_bitmap_tmp2 = NULL;
	bitstr_t **avail_cores, **free_cores;
	bool test_only;
	uint32_t sockets_per_node = 1;
	uint32_t c, j, n, c_alloc = 0, c_size, total_cpus;
	uint64_t save_mem = 0, avail_mem = 0, needed_mem = 0, lowest_mem = 0;
	int32_t build_cnt;
	job_resources_t *job_res;
	struct job_details *details_ptr = job_ptr->details;
	struct part_res_record *p_ptr, *jp_ptr;
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
	else	/* SELECT_MODE_RUN_NOW || SELECT_MODE_WILL_RUN  */
		test_only = false;

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
		if (details_ptr->mc_ptr &&
		    details_ptr->mc_ptr->sockets_per_node)
			sockets_per_node =
				details_ptr->mc_ptr->sockets_per_node;
		details_ptr->min_gres_cpu = gres_plugin_job_min_cpu_node(
			sockets_per_node,
			details_ptr->ntasks_per_node,
			job_ptr->gres_list);
	} else if (exc_cores && *exc_cores)
		exc_core_bitmap = *exc_cores;

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: evaluating %pJ on %u nodes",
		     plugin_type, __func__, job_ptr,
		     bit_set_count(node_bitmap));
	}

	if ((details_ptr->pn_min_memory == 0) &&
	    (select_fast_schedule == 0) &&
	    (!is_cons_tres || !gres_plugin_job_mem_max(job_ptr->gres_list)))
		job_ptr->bit_flags |= NODE_MEM_CALC;	/* To be calculated */

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
					part_core_map, prefer_alloc_nodes,
					tres_mc_ptr);
	if (!avail_res_array) {
		/* job can not fit */
		xfree(tres_mc_ptr);
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 0 fail: insufficient resources",
			     plugin_type, __func__);
		}
		return SLURM_ERROR;
	} else if (test_only) {
		xfree(tres_mc_ptr);
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		common_free_avail_res_array(avail_res_array);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 0 pass: test_only", plugin_type,
			     __func__);
		}
		return SLURM_SUCCESS;
	} else if (!job_ptr->best_switch) {
		xfree(tres_mc_ptr);
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		common_free_avail_res_array(avail_res_array);
		if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
			info("%s: %s: test 0 fail: waiting for switches",
			     plugin_type, __func__);
		}
		return SLURM_ERROR;
	}
	if (cr_type == CR_MEMORY) {
		/*
		 * CR_MEMORY does not care about existing CPU allocations,
		 * so we can jump right to job allocation from here
		 */
		goto alloc_job;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: test 0 pass - job fits on given resources",
		     plugin_type, __func__);
	}
	common_free_avail_res_array(avail_res_array);

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
					part_core_map, prefer_alloc_nodes,
					tres_mc_ptr);
	if (avail_res_array && job_ptr->best_switch) {
		/* job fits! We're done. */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 1 pass - idle resources found",
			     plugin_type, __func__);
		}
		goto alloc_job;
	}
	common_free_avail_res_array(avail_res_array);

	if ((gang_mode == 0) && (job_node_req == NODE_CR_ONE_ROW)) {
		/*
		 * This job CANNOT share CPUs regardless of priority,
		 * so we fail here. Note that Shared=EXCLUSIVE was already
		 * addressed in _verify_node_state() and
		 * job preemption removes jobs from simulated resource
		 * allocation map before this point.
		 */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 1 fail - no idle resources available",
			     plugin_type, __func__);
		}
		goto alloc_job;
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: test 1 fail - not enough idle resources",
		     plugin_type, __func__);
	}

	/*** Step 2 ***/
	for (jp_ptr = cr_part_ptr; jp_ptr; jp_ptr = jp_ptr->next) {
		if (jp_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!jp_ptr) {
		error("%s %s: could not find partition for %pJ",
		      plugin_type, __func__, job_ptr);
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
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: looking for higher-priority or "
			     "PREEMPT_MODE_OFF part's to remove from avail_cores",
			     plugin_type, __func__);
		}

		for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
			if ((p_ptr->part_ptr->priority_tier <=
			     jp_ptr->part_ptr->priority_tier) &&
			    (p_ptr->part_ptr->preempt_mode !=
			     PREEMPT_MODE_OFF)) {
				if (select_debug_flags &
				    DEBUG_FLAG_SELECT_TYPE) {
					info("%s: %s: continuing on part: %s",
					     plugin_type, __func__,
					     p_ptr->part_ptr->name);
				}
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
					part_core_map, prefer_alloc_nodes,
					tres_mc_ptr);
	if (!avail_res_array) {
		/*
		 * job needs resources that are currently in use by
		 * higher-priority jobs, so fail for now
		 */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 2 fail - resources busy with higher priority jobs",
			     plugin_type, __func__);
		}
		goto alloc_job;
	}
	common_free_avail_res_array(avail_res_array);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: test 2 pass - available resources for this priority",
		     plugin_type, __func__);
	}

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
					part_core_map, prefer_alloc_nodes,
					tres_mc_ptr);
	if (avail_res_array) {
		/*
		 * To the extent possible, remove from consideration resources
		 * which are allocated to jobs in lower priority partitions.
		 */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 3 pass - found resources",
			     plugin_type, __func__);
		}
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
				cr_type, test_only, part_core_map,
				prefer_alloc_nodes, tres_mc_ptr);
			if (!avail_res_array_tmp) {
				free_core_array(&free_cores_tmp2);
				FREE_NULL_BITMAP(node_bitmap_tmp2);
				break;
			}
			if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
				info("%s: %s: remove low-priority partition %s",
				     plugin_type, __func__,
				     p_ptr->part_ptr->name);
			}
			free_core_array(&free_cores);
			free_cores      = free_cores_tmp;
			free_cores_tmp  = free_cores_tmp2;
			free_cores_tmp2 = NULL;
			bit_copybits(node_bitmap, node_bitmap_tmp);
			FREE_NULL_BITMAP(node_bitmap_tmp);
			node_bitmap_tmp  = node_bitmap_tmp2;
			node_bitmap_tmp2 = NULL;
			common_free_avail_res_array(avail_res_array);
			avail_res_array = avail_res_array_tmp;
		}
		goto alloc_job;
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: test 3 fail - not enough idle resources in same priority",
		     plugin_type, __func__);
	}

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
						test_only, part_core_map,
						prefer_alloc_nodes,
						tres_mc_ptr);
		if (avail_res_array &&
		    (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)) {
			info("%s: %s: test 4 pass - first row found",
			     plugin_type, __func__);
		}
		goto alloc_job;
	}


	if ((jp_ptr->num_rows > 1) && !preempt_by_qos)
		common_sort_part_rows(jp_ptr);	/* Preserve row order for QOS */
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
						test_only, part_core_map,
						prefer_alloc_nodes,
						tres_mc_ptr);
		if (avail_res_array) {
			if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
				info("%s: %s: test 4 pass - row %i",
				     plugin_type, __func__, i);
			}
			break;
		}
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 4 fail - row %i",
			     plugin_type, __func__, i);
		}
	}

	if ((i < c) && !jp_ptr->row[i].row_bitmap) {
		/* we've found an empty row, so use it */
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		bit_copybits(node_bitmap, orig_node_map);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 4 trying empty row %i",
			     plugin_type, __func__, i);
		}
		avail_res_array = _select_nodes(job_ptr, min_nodes, max_nodes,
						req_nodes, node_bitmap,
						free_cores, node_usage, cr_type,
						test_only, part_core_map,
						prefer_alloc_nodes,
						tres_mc_ptr);
	}

	if (!avail_res_array) {
		/* job can't fit into any row, so exit */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: test 4 fail - busy partition",
			     plugin_type, __func__);
		}
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
		common_free_avail_res_array(avail_res_array);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("%s: %s: exiting with no allocation",
			     plugin_type, __func__);
		}
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
		common_free_avail_res_array(avail_res_array);
		return error_code;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: distributing %pJ", plugin_type, __func__,
		     job_ptr);
	}

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
		error("%s: %s: problem building cpu_count array (%d != %d)",
		      plugin_type, __func__, j, n);
	}

	job_res                   = create_job_resources();
	job_res->node_bitmap      = bit_copy(node_bitmap);
	job_res->nodes            = bitmap2node_name(node_bitmap);
	job_res->nhosts           = n;
	job_res->ncpus            = job_res->nhosts;
	if (job_ptr->details->ntasks_per_node)
		job_res->ncpus   *= details_ptr->ntasks_per_node;
	job_res->ncpus            = MAX(job_res->ncpus,
					details_ptr->min_cpus);
	job_res->ncpus            = MAX(job_res->ncpus,
					(job_res->nhosts *
					 details_ptr->pn_min_cpus));
	if (job_ptr->details->mc_ptr)
		sockets_per_node = job_ptr->details->mc_ptr->sockets_per_node;
	i = gres_plugin_job_min_cpus(job_res->nhosts, sockets_per_node,
				     job_ptr->details->num_tasks,
				     job_ptr->gres_list);
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
	error_code = build_job_resources(job_res, node_record_table_ptr,
					 select_fast_schedule);
	if (error_code != SLURM_SUCCESS) {
		xfree(tres_mc_ptr);
		common_free_avail_res_array(avail_res_array);
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
	for (i = 0, n = i_first; n < select_node_cnt; n++) {
		int first_core, last_core;
		bitstr_t *use_free_cores = NULL;

		if (!bit_test(node_bitmap, n))
			continue;

		if (is_cons_tres) {
			first_core = 0;
			last_core = select_node_record[n].tot_cores;
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
				error("%s: %s core_bitmap index error on node %s (NODE_INX:%d, C_SIZE:%u)",
				      plugin_type, __func__,
				      select_node_record[n].node_ptr->name,
				      n, c_size);
				drain_nodes(select_node_record[n].node_ptr->name,
					    "Bad core count", getuid());
				common_free_avail_res_array(avail_res_array);
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

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: %pJ ncpus %u cbits %u/%u nbits %u",
		     plugin_type, __func__, job_ptr,
		     job_res->ncpus, count_core_array_set(free_cores),
		     c_alloc, job_res->nhosts);
	}
	free_core_array(&free_cores);

	/* distribute the tasks, clear unused cores from job_res->core_bitmap */
	job_ptr->job_resrcs = job_res;
	i_first = bit_ffs(job_res->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job_res->node_bitmap);
	else
		i_last = -2;
	if (is_cons_tres &&
	    job_ptr->gres_list && (error_code == SLURM_SUCCESS)) {
		struct node_record *node_ptr;
		bool have_gres_per_task, task_limit_set = false;

		/*
		 * Determine if any job gres_per_task specification here
		 * to avoid calling gres_plugin_get_task_limit unless needed
		 */
		have_gres_per_task = gres_plugin_job_tres_per_task(
			job_ptr->gres_list);
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
					gres_plugin_get_task_limit(
						avail_res_array[i]->
						sock_gres_list);
				if (gres_task_limit[j] != NO_VAL)
					task_limit_set = true;
			}
			node_ptr = node_record_table_ptr + i;
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
	    job_ptr->gres_list && (error_code == SLURM_SUCCESS)) {
		error_code = gres_plugin_job_core_filter4(
			sock_gres_list,
			job_ptr->job_id, job_res,
			job_ptr->details->overcommit,
			tres_mc_ptr, node_record_table_ptr);
	}
	xfree(gres_task_limit);
	xfree(node_gres_list);
	xfree(sock_gres_list);
	xfree(tres_mc_ptr);
	common_free_avail_res_array(avail_res_array);
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
			job_ptr->total_cpus += select_node_record[i].cpus;
		}
	} else if (cr_type & CR_SOCKET) {
		int ci = 0;
		int s, last_s, sock_cnt = 0;

		job_ptr->total_cpus = 0;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			sock_cnt = 0;
			for (s = 0; s < select_node_record[i].tot_sockets; s++){
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

	if (is_cons_tres && !(job_ptr->bit_flags & JOB_MEM_SET) &&
	    gres_plugin_job_mem_set(job_ptr->gres_list, job_res)) {
		debug("%pJ memory set via GRES limit", job_ptr);
	} else {
		/* load memory allocated array */
		save_mem = details_ptr->pn_min_memory;
		for (i = i_first, j = 0; i <= i_last; i++) {
			if (!bit_test(job_res->node_bitmap, i))
				continue;
			nodename = select_node_record[i].node_ptr->name;
			avail_mem = select_node_record[i].real_memory -
				select_node_record[i].mem_spec_limit;
			if (save_mem & MEM_PER_CPU) {	/* Memory per CPU */
				needed_mem = job_res->cpus[j] *
					(save_mem & (~MEM_PER_CPU));
			} else if (save_mem) {		/* Memory per node */
				needed_mem = save_mem;
			} else {		/* Allocate all node memory */
				needed_mem = avail_mem;
				if (!test_only &&
				    (node_usage[i].alloc_memory > 0)) {
					if (select_debug_flags &
					    DEBUG_FLAG_SELECT_TYPE)
						info("%s: node %s has already alloc_memory=%"PRIu64". %pJ can't allocate all node memory",
						     __func__, nodename,
						     node_usage[i].alloc_memory,
						     job_ptr);
					error_code = SLURM_ERROR;
					break;
				}
				if ((j == 0) || (lowest_mem > avail_mem))
					lowest_mem = avail_mem;
			}
			if (!test_only && save_mem) {
				if (node_usage[i].alloc_memory > avail_mem) {
					error("%s: node %s memory is already overallocated (%"PRIu64" > %"PRIu64"). %pJ can't allocate any node memory",
					      __func__, nodename,
					      node_usage[i].alloc_memory,
					      avail_mem, job_ptr);
					error_code = SLURM_ERROR;
					break;
				}
				avail_mem -= node_usage[i].alloc_memory;
			}
			if (needed_mem > avail_mem) {
				if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
					info("%s: %pJ would overallocate node %s memory (%"PRIu64" > %"PRIu64")",
					     __func__, job_ptr, nodename,
					     needed_mem, avail_mem);
				}
				error_code = SLURM_ERROR;
				break;
			}
			job_res->memory_allocated[j] = needed_mem;
			j++;
		}
		if ((error_code != SLURM_ERROR) && (save_mem == 0))
			details_ptr->pn_min_memory = lowest_mem;
	}
	if (error_code == SLURM_ERROR)
		free_job_resources(&job_ptr->job_resrcs);

	return error_code;
}

/* Determine if a job can ever run */
static int _test_only(struct job_record *job_ptr, bitstr_t *node_bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
		      uint32_t req_nodes, uint16_t job_node_req)
{
	int rc;
	uint16_t tmp_cr_type = cr_type;

	if (job_ptr->part_ptr->cr_type) {
		if ((cr_type & CR_SOCKET) || (cr_type & CR_CORE)) {
			tmp_cr_type &= ~(CR_SOCKET | CR_CORE | CR_MEMORY);
			tmp_cr_type |= job_ptr->part_ptr->cr_type;
		} else {
			info("%s: Can't use Partition SelectType unless "
			     "using CR_Socket or CR_Core", plugin_type);
		}
	}

	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_TEST_ONLY, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage, NULL, false,
		       false, false);
	return rc;
}

/*
 * Determine where and when the job at job_ptr can begin execution by updating
 * a scratch cr_record structure to reflect each job terminating at the
 * end of its time limit and use this to show where and when the job at job_ptr
 * will begin execution. Used by Slurm's sched/backfill plugin.
 */
static int _will_run_test(struct job_record *job_ptr,
			  bitstr_t *node_bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes, uint16_t job_node_req,
			  List preemptee_candidates,
			  List *preemptee_job_list,
			  bitstr_t **exc_core_bitmap)
{
	struct part_res_record *future_part;
	struct node_use_record *future_usage;
	struct job_record *tmp_job_ptr;
	List cr_job_list;
	ListIterator job_iterator, preemptee_iterator;
	bitstr_t *orig_map;
	int action, rc = SLURM_ERROR;
	time_t now = time(NULL);
	uint16_t tmp_cr_type = cr_type;
	bool qos_preemptor = false;

	orig_map = bit_copy(node_bitmap);

	if (job_ptr->part_ptr->cr_type) {
		if ((cr_type & CR_SOCKET) || (cr_type & CR_CORE)) {
			tmp_cr_type &= ~(CR_SOCKET | CR_CORE | CR_MEMORY);
			tmp_cr_type |= job_ptr->part_ptr->cr_type;
		} else {
			info("%s: Can't use Partition SelectType unless "
			     "using CR_Socket or CR_Core", plugin_type);
		}
	}

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

	/*
	 * Job is still pending. Simulate termination of jobs one at a time
	 * to determine when and where the job can start.
	 */
	future_part = _dup_part_data(select_part_record);
	if (future_part == NULL) {
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}
	future_usage = _dup_node_usage(select_node_usage);
	if (future_usage == NULL) {
		_destroy_part_data(future_part);
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}

	/* Build list of running and suspended jobs */
	cr_job_list = list_create(NULL);
	job_iterator = list_iterator_create(job_list);
	while ((tmp_job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool cleaning = _job_cleaning(tmp_job_ptr);
		if (!cleaning && IS_JOB_COMPLETING(tmp_job_ptr))
			cleaning = true;
		if (!IS_JOB_RUNNING(tmp_job_ptr) &&
		    !IS_JOB_SUSPENDED(tmp_job_ptr) &&
		    !cleaning)
			continue;
		if (tmp_job_ptr->end_time == 0) {
			if (!cleaning) {
				error("%s: %s: Active %pJ has zero end_time",
				      plugin_type, __func__, tmp_job_ptr);
			}
			continue;
		}
		if (tmp_job_ptr->node_bitmap == NULL) {
			/*
			 * This should indicate a requeued job was cancelled
			 * while NHC was running
			 */
			if (!cleaning) {
				error("%s: %s: %pJ has NULL node_bitmap",
				      plugin_type, __func__, tmp_job_ptr);
			}
			continue;
		}
		if (cleaning ||
		    !_is_preemptable(tmp_job_ptr, preemptee_candidates)) {
			/* Queue job for later removal from data structures */
			list_append(cr_job_list, tmp_job_ptr);
		} else {
			uint16_t mode = slurm_job_preempt_mode(tmp_job_ptr);
			if (mode == PREEMPT_MODE_OFF)
				continue;
			if (mode == PREEMPT_MODE_SUSPEND) {
				action = 2;	/* remove cores, keep memory */
				if (preempt_by_qos)
					qos_preemptor = true;
			} else
				action = 0;	/* remove cores and memory */
			/* Remove preemptable job now */
			(void) common_rm_job_res(future_part, future_usage,
						 tmp_job_ptr, action, false);
		}
	}
	list_iterator_destroy(job_iterator);

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
		bool more_jobs = true;
		DEF_TIMERS;
		list_sort(cr_job_list, _cr_job_list_sort);
		START_TIMER;
		job_iterator = list_iterator_create(cr_job_list);
		while (more_jobs) {
			struct job_record *first_job_ptr = NULL;
			struct job_record *last_job_ptr = NULL;
			struct job_record *next_job_ptr = NULL;
			int overlap, rm_job_cnt = 0;

			while (true) {
				tmp_job_ptr = list_next(job_iterator);
				if (!tmp_job_ptr) {
					more_jobs = false;
					break;
				}
				bit_or(node_bitmap, orig_map);
				overlap = bit_overlap(node_bitmap,
						      tmp_job_ptr->node_bitmap);
				if (overlap == 0)  /* job has no usable nodes */
					continue;  /* skip it */
				debug2("%s: %s, %pJ: overlap=%d",
				       plugin_type, __func__,
				       tmp_job_ptr, overlap);
				if (!first_job_ptr)
					first_job_ptr = tmp_job_ptr;
				last_job_ptr = tmp_job_ptr;
				(void) common_rm_job_res(
					future_part, future_usage,
					tmp_job_ptr, 0, false);
				if (rm_job_cnt++ > 200)
					break;
				next_job_ptr = list_peek_next(job_iterator);
				if (!next_job_ptr) {
					more_jobs = false;
					break;
				} else if (next_job_ptr->end_time >
					   (first_job_ptr->end_time +
					    time_window)) {
					break;
				}
			}
			if (!last_job_ptr)	/* Should never happen */
				break;
			if (bf_window_scale)
				time_window += bf_window_scale;
			else
				time_window *= 2;
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
		preemptee_iterator =list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = list_next(preemptee_iterator))) {
			if (!bit_overlap(node_bitmap, tmp_job_ptr->node_bitmap))
				continue;
			list_append(*preemptee_job_list, tmp_job_ptr);
		}
		list_iterator_destroy(preemptee_iterator);
	}

	FREE_NULL_LIST(cr_job_list);
	_destroy_part_data(future_part);
	_destroy_node_data(future_usage, NULL);
	FREE_NULL_BITMAP(orig_map);

	return rc;
}

/* Allocate resources for a job now, if possible */
static int _run_now(struct job_record *job_ptr, bitstr_t *node_bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    uint32_t req_nodes, uint16_t job_node_req,
		    List preemptee_candidates, List *preemptee_job_list,
		    bitstr_t **exc_cores)
{
	int rc;
	bitstr_t *orig_node_map = NULL, *save_node_map;
	struct job_record *tmp_job_ptr = NULL;
	ListIterator job_iterator, preemptee_iterator;
	struct part_res_record *future_part;
	struct node_use_record *future_usage;
	bool remove_some_jobs = false;
	uint16_t pass_count = 0;
	uint16_t mode = NO_VAL16;
	uint16_t tmp_cr_type = cr_type;
	bool preempt_mode = false;

	save_node_map = bit_copy(node_bitmap);
top:	orig_node_map = bit_copy(save_node_map);

	if (job_ptr->part_ptr->cr_type) {
		if ((cr_type & CR_SOCKET) || (cr_type & CR_CORE)) {
			tmp_cr_type &= ~(CR_SOCKET | CR_CORE | CR_MEMORY);
			tmp_cr_type |= job_ptr->part_ptr->cr_type;
		} else {
			info("%s: Can't use Partition SelectType unless "
			     "using CR_Socket or CR_Core", plugin_type);
		}
	}

	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_RUN_NOW, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage, exc_cores, false,
		       false, preempt_mode);

	if ((rc != SLURM_SUCCESS) && preemptee_candidates && preempt_by_qos) {
		/* Determine QOS preempt mode of first job */
		job_iterator = list_iterator_create(preemptee_candidates);
		if ((tmp_job_ptr = (struct job_record *)
		    list_next(job_iterator))) {
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
		future_part = _dup_part_data(select_part_record);
		if (future_part == NULL) {
			FREE_NULL_BITMAP(orig_node_map);
			FREE_NULL_BITMAP(save_node_map);
			return SLURM_ERROR;
		}
		future_usage = _dup_node_usage(select_node_usage);
		if (future_usage == NULL) {
			_destroy_part_data(future_part);
			FREE_NULL_BITMAP(orig_node_map);
			FREE_NULL_BITMAP(save_node_map);
			return SLURM_ERROR;
		}

		job_iterator = list_iterator_create(preemptee_candidates);
		while ((tmp_job_ptr = (struct job_record *)
			list_next(job_iterator))) {
			if (!IS_JOB_RUNNING(tmp_job_ptr) &&
			    !IS_JOB_SUSPENDED(tmp_job_ptr))
				continue;
			mode = slurm_job_preempt_mode(tmp_job_ptr);
			if ((mode != PREEMPT_MODE_REQUEUE)    &&
			    (mode != PREEMPT_MODE_CHECKPOINT) &&
			    (mode != PREEMPT_MODE_CANCEL))
				continue;	/* can't remove job */
			/* Remove preemptable job now */
			(void) common_rm_job_res(future_part, future_usage,
						 tmp_job_ptr, 0, false);
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
				/* Remove remaining jobs from preempt list */
				while ((tmp_job_ptr = (struct job_record *)
					list_next(job_iterator))) {
					(void) list_remove(job_iterator);
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
				tmp_job_ptr = (struct job_record *)
					      list_remove(job_iterator);
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
				while ((tmp_job_ptr = (struct job_record *)
					list_next(job_iterator))) {
					if (tmp_job_ptr->details->usable_nodes
					    == 99999)
						break;
					tmp_job_ptr->details->usable_nodes =
						bit_overlap(node_bitmap,
							    tmp_job_ptr->
							    node_bitmap);
				}
				while ((tmp_job_ptr = (struct job_record *)
					list_next(job_iterator))) {
					tmp_job_ptr->details->usable_nodes = 0;
				}
				list_sort(preemptee_candidates,
					  (ListCmpF)_sort_usable_nodes_dec);
			}
			FREE_NULL_BITMAP(orig_node_map);
			list_iterator_destroy(job_iterator);
			_destroy_part_data(future_part);
			_destroy_node_data(future_usage, NULL);
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
			while ((tmp_job_ptr = (struct job_record *)
				list_next(preemptee_iterator))) {
				mode = slurm_job_preempt_mode(tmp_job_ptr);
				if ((mode != PREEMPT_MODE_REQUEUE)    &&
				    (mode != PREEMPT_MODE_CHECKPOINT) &&
				    (mode != PREEMPT_MODE_CANCEL))
					continue;
				if (bit_overlap(node_bitmap,
						tmp_job_ptr->node_bitmap) == 0)
					continue;
				list_append(*preemptee_job_list,
					    tmp_job_ptr);
				remove_some_jobs = true;
			}
			list_iterator_destroy(preemptee_iterator);
			if (!remove_some_jobs) {
				FREE_NULL_LIST(*preemptee_job_list);
			}
		}

		_destroy_part_data(future_part);
		_destroy_node_data(future_usage, NULL);
	}
	FREE_NULL_BITMAP(orig_node_map);
	FREE_NULL_BITMAP(save_node_map);

	return rc;
}

static bitstr_t *_create_core_bitmap(int node_inx)
{
	xassert(node_inx < select_node_cnt);

	if (is_cons_tres)
		return bit_alloc(select_node_record[node_inx].tot_cores);
	else {
		/*
		 * For cons_res we need the whole system size instead of per
		 * node.
		 */
		static uint32_t sys_core_size = NO_VAL;

		xassert(!node_inx);

		if (sys_core_size == NO_VAL) {
			sys_core_size = 0;
			for (int i = 0; i < select_node_cnt; i++)
				sys_core_size +=
					select_node_record[i].tot_cores;
		}
		return bit_alloc(sys_core_size);
	}
}

/*
 * Handle job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT sys_resrcs_ptr - bitmap of available CPUs, allocate as needed
 * IN type - add/rem/test
 * RET 1 on success, 0 otherwise
 */
static int _handle_job_res(job_resources_t *job_resrcs_ptr,
			   bitstr_t ***sys_resrcs_ptr,
			   handle_job_res_t type)
{
	int i, i_first, i_last;
	int c, c_off = 0, full_offset;
	bitstr_t **core_array;
	bitstr_t *use_core_array;
	uint32_t core_begin;
	uint32_t core_end;
	uint16_t cores_per_node;

	if (!job_resrcs_ptr->core_bitmap)
		return 1;

	/* create row_bitmap data structure as needed */
	if (*sys_resrcs_ptr == NULL) {
		if (type == HANDLE_JOB_RES_TEST)
			return 1;
		core_array = build_core_array();
		*sys_resrcs_ptr = core_array;
		for (int i = 0; i < core_array_size; i++)
			core_array[i] = _create_core_bitmap(i);
	} else
		core_array = *sys_resrcs_ptr;

	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job_resrcs_ptr->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			continue;

		cores_per_node = select_node_record[i].tot_cores;

		if (is_cons_tres) {
			core_begin = 0;
			core_end = select_node_record[i].tot_cores;
			use_core_array = core_array[i];
			full_offset = 0;
		} else {
			core_begin = cr_get_coremap_offset(i);
			core_end = cr_get_coremap_offset(i+1);
			use_core_array = core_array[0];
			full_offset = cr_node_cores_offset[i];
		}

		if (job_resrcs_ptr->whole_node) {
			if (!use_core_array) {
				if (type != HANDLE_JOB_RES_TEST)
					error("%s: %s: core_array for node %d is NULL %d",
					      plugin_type, __func__, i, type);
				continue;	/* Move to next node */
			}

			switch (type) {
			case HANDLE_JOB_RES_ADD:
				bit_nset(use_core_array,
					 core_begin, core_end-1);
				break;
			case HANDLE_JOB_RES_REM:
				bit_nclear(use_core_array,
					   core_begin, core_end-1);
				break;
			case HANDLE_JOB_RES_TEST:
				if (is_cons_tres) {
					if (bit_ffs(use_core_array) != -1)
						return 0;
				} else {
					for (c = 0; c < cores_per_node; c++)
						if (bit_test(job_resrcs_ptr->core_bitmap,
							     c_off + c))
							return 0;
				}
				break;
			}
			continue;	/* Move to next node */
		}

		for (c = 0; c < cores_per_node; c++) {
			if (!bit_test(job_resrcs_ptr->core_bitmap, c_off + c))
				continue;
			if (!use_core_array) {
				if (type != HANDLE_JOB_RES_TEST)
					error("%s: %s: core_array for node %d is NULL %d",
					      plugin_type, __func__, i, type);
				continue;	/* Move to next node */
			}
			switch (type) {
			case HANDLE_JOB_RES_ADD:
				bit_set(use_core_array, full_offset + c);
				break;
			case HANDLE_JOB_RES_REM:
				bit_clear(use_core_array, full_offset + c);
				break;
			case HANDLE_JOB_RES_TEST:
				if (bit_test(use_core_array, full_offset + c))
					return 0;    /* Core conflict on node */
				break;
			}
		}
		c_off += cores_per_node;
	}

	return 1;
}

/* Sort jobs by start time, then size (CPU count) */
static int _compare_support(const void *v1, const void *v2)
{
	struct sort_support *s1, *s2;

	s1 = (struct sort_support *) v1;
	s2 = (struct sort_support *) v2;

	if ((s1->jstart > s2->jstart) ||
	    ((s1->jstart == s2->jstart) &&
	     (s1->tmpjobs->ncpus > s2->tmpjobs->ncpus)))
		return 1;

	return 0;
}

/*
 * _build_row_bitmaps: A job has been removed from the given partition,
 *                     so the row_bitmap(s) need to be reconstructed.
 *                     Optimize the jobs into the least number of rows,
 *                     and make the lower rows as dense as possible.
 *
 * IN p_ptr - the partition that has jobs to be optimized
 * IN job_ptr - pointer to single job removed, pass NULL to completely rebuild
 */
static void _build_row_bitmaps(struct part_res_record *p_ptr,
			       struct job_record *job_ptr)
{
	uint32_t i, j, num_jobs;
	int x;
	struct part_row_data *this_row, *orig_row;
	struct sort_support *ss;

	if (!p_ptr->row)
		return;

	if (p_ptr->num_rows == 1) {
		this_row = p_ptr->row;
		if (this_row->num_jobs == 0) {
			clear_core_array(this_row->row_bitmap);
		} else {
			if (job_ptr) { /* just remove the job */
				xassert(job_ptr->job_resrcs);
				common_rm_job_cores(job_ptr->job_resrcs,
						    &this_row->row_bitmap);
			} else { /* totally rebuild the bitmap */
				clear_core_array(this_row->row_bitmap);
				for (j = 0; j < this_row->num_jobs; j++) {
					common_add_job_cores(
						this_row->job_list[j],
						&this_row->row_bitmap);
				}
			}
		}
		return;
	}

	/* gather data */
	num_jobs = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		num_jobs += p_ptr->row[i].num_jobs;
	}
	if (num_jobs == 0) {
		for (i = 0; i < p_ptr->num_rows; i++)
			clear_core_array(p_ptr->row[i].row_bitmap);
		return;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("DEBUG: %s (before):", __func__);
		common_dump_parts(p_ptr);
	}
	debug3("%s: %s reshuffling %u jobs", plugin_type, __func__, num_jobs);

	/* make a copy, in case we cannot do better than this */
	orig_row = common_dup_row_data(p_ptr->row, p_ptr->num_rows);
	if (orig_row == NULL)
		return;

	/* create a master job list and clear out ALL row data */
	ss = xcalloc(num_jobs, sizeof(struct sort_support));
	x = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
			ss[x].tmpjobs = p_ptr->row[i].job_list[j];
			p_ptr->row[i].job_list[j] = NULL;
			ss[x].jstart = bit_ffs(ss[x].tmpjobs->node_bitmap);
			ss[x].jstart = cr_get_coremap_offset(ss[x].jstart);
			ss[x].jstart += bit_ffs(ss[x].tmpjobs->core_bitmap);
			x++;
		}
		p_ptr->row[i].num_jobs = 0;
		clear_core_array(p_ptr->row[i].row_bitmap);
	}

	/*
	 * VERY difficult: Optimal placement of jobs in the matrix
	 * - how to order jobs to be added to the matrix?
	 *   - "by size" does not guarantee optimal placement
	 *
	 *   - for now, try sorting jobs by first bit set
	 *     - if job allocations stay "in blocks", then this should work OK
	 *     - may still get scenarios where jobs should switch rows
	 */
	qsort(ss, num_jobs, sizeof(struct sort_support), _compare_support);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < num_jobs; i++) {
			char cstr[64], nstr[64];
			if (ss[i].tmpjobs->core_bitmap) {
				bit_fmt(cstr, (sizeof(cstr) - 1) ,
					ss[i].tmpjobs->core_bitmap);
			} else
				sprintf(cstr, "[no core_bitmap]");
			if (ss[i].tmpjobs->node_bitmap) {
				bit_fmt(nstr, (sizeof(nstr) - 1),
					ss[i].tmpjobs->node_bitmap);
			} else
				sprintf(nstr, "[no node_bitmap]");
			info("DEBUG:  jstart %d job nb %s cb %s",
			     ss[i].jstart, nstr, cstr);
		}
	}

	/* add jobs to the rows */
	for (j = 0; j < num_jobs; j++) {
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (common_job_fit_in_row(ss[j].tmpjobs,
						  &(p_ptr->row[i]))) {
				/* job fits in row, so add it */
				common_add_job_to_row(
					ss[j].tmpjobs, &(p_ptr->row[i]));
				ss[j].tmpjobs = NULL;
				break;
			}
		}
		/* job should have been added, so shuffle the rows */
		common_sort_part_rows(p_ptr);
	}

	/* test for dangling jobs */
	for (j = 0; j < num_jobs; j++) {
		if (ss[j].tmpjobs)
			break;
	}
	if (j < num_jobs) {
		/*
		 * we found a dangling job, which means our packing
		 * algorithm couldn't improve apon the existing layout.
		 * Thus, we'll restore the original layout here
		 */
		debug3("%s: %s: dangling job found", plugin_type, __func__);

		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("DEBUG: %s (post-algorithm):", __func__);
			common_dump_parts(p_ptr);
		}

		common_destroy_row_data(p_ptr->row, p_ptr->num_rows);
		p_ptr->row = orig_row;
		orig_row = NULL;

		/* still need to rebuild row_bitmaps */
		for (i = 0; i < p_ptr->num_rows; i++) {
			clear_core_array(p_ptr->row[i].row_bitmap);
			if (p_ptr->row[i].num_jobs == 0)
				continue;
			for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
				common_add_job_cores(p_ptr->row[i].job_list[j],
						     &p_ptr->row[i].row_bitmap);
			}
		}
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("DEBUG: %s (after):", __func__);
		common_dump_parts(p_ptr);
	}

	if (orig_row)
		common_destroy_row_data(orig_row, p_ptr->num_rows);
	xfree(ss);

	return;

	/* LEFTOVER DESIGN THOUGHTS, PRESERVED HERE */

	/*
	 * 1. sort jobs by size
	 * 2. only load core bitmaps with largest jobs that conflict
	 * 3. sort rows by set count
	 * 4. add remaining jobs, starting with fullest rows
	 * 5. compute  set count: if disparity between rows got closer, then
	 *    switch non-conflicting jobs that were added
	 */

	/*
	 *  Step 1: remove empty rows between non-empty rows
	 *  Step 2: try to collapse rows
	 *  Step 3: sort rows by size
	 *  Step 4: try to swap jobs from different rows to pack rows
	 */

	/*
	 * WORK IN PROGRESS - more optimization should go here, such as:
	 *
	 * - try collapsing jobs from higher rows to lower rows
	 *
	 * - produce a load array to identify cores with less load. Test
	 * to see if those cores are in the lower row. If not, try to swap
	 * those jobs with jobs in the lower row. If the job can be swapped
	 * AND the lower row set_count increases, then SUCCESS! else swap
	 * back. The goal is to pack the lower rows and "bubble up" clear
	 * bits to the higher rows.
	 */
}

/* Delete the given partition row data */
extern void common_destroy_row_data(
	struct part_row_data *row, uint16_t num_rows)
{
	uint32_t r;

	for (r = 0; r < num_rows; r++) {
		free_core_array(&row[r].row_bitmap);
		xfree(row[r].job_list);
	}

	xfree(row);
}

extern void common_free_avail_res(avail_res_t *avail_res)
{
	if (!avail_res)
		return;

	xfree(avail_res->avail_cores_per_sock);
	FREE_NULL_LIST(avail_res->sock_gres_list);
	xfree(avail_res);
}

extern void common_free_avail_res_array(avail_res_t **avail_res_array)
{
	int n;
	if (!avail_res_array)
		return;

	for (n = 0; n < select_node_cnt; n++)
		common_free_avail_res(avail_res_array[n]);
	xfree(avail_res_array);
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

/*
 * Add job resource use to the partition data structure
 */
extern void common_add_job_to_row(struct job_resources *job,
				  struct part_row_data *r_ptr)
{
	/* add the job to the row_bitmap */
	if (r_ptr->row_bitmap && (r_ptr->num_jobs == 0)) {
		/* if no jobs, clear the existing row_bitmap first */
		clear_core_array(r_ptr->row_bitmap);
	}

	common_add_job_cores(job, &r_ptr->row_bitmap);

	/*  add the job to the job_list */
	if (r_ptr->num_jobs >= r_ptr->job_list_size) {
		r_ptr->job_list_size += 8;
		xrealloc(r_ptr->job_list, r_ptr->job_list_size *
			 sizeof(struct job_resources *));
	}
	r_ptr->job_list[r_ptr->num_jobs++] = job;
}


/*
 * allocate resources to the given job
 * - add 'struct job_resources' resources to 'struct part_res_record'
 * - add job's memory requirements to 'struct node_res_record'
 *
 * if action = 0 then add cores, memory + GRES (starting new job)
 * if action = 1 then add memory + GRES (adding suspended job at restart)
 * if action = 2 then only add cores (suspended job is resumed)
 *
 * See also: common_rm_job_res()
 */
extern int common_add_job_to_res(struct job_record *job_ptr, int action)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	struct part_res_record *p_ptr;
	List node_gres_list;
	int i, i_first, i_last, n;
	bitstr_t *core_bitmap;

	if (!job || !job->core_bitmap) {
		error("%s: %s: %pJ has no job_resrcs info",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	debug3("%s: %s: %pJ action:%d ", plugin_type, __func__, job_ptr,
	       action);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		log_job_resources(job_ptr);

	i_first = bit_ffs(job->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job->node_bitmap);
	else
		i_last = -2;
	for (i = i_first, n = -1; i <= i_last; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		n++;
		if (job->cpus[n] == 0)
			continue;  /* node removed by job resize */

		node_ptr = select_node_record[i].node_ptr;
		if (action != 2) {
			if (select_node_usage[i].gres_list)
				node_gres_list = select_node_usage[i].gres_list;
			else
				node_gres_list = node_ptr->gres_list;
			core_bitmap = copy_job_resources_node(job, n);
			gres_plugin_job_alloc(job_ptr->gres_list,
					      node_gres_list, job->nhosts,
					      i, n, job_ptr->job_id,
					      node_ptr->name, core_bitmap,
					      job_ptr->user_id);
			gres_plugin_node_state_log(node_gres_list,
						   node_ptr->name);
			FREE_NULL_BITMAP(core_bitmap);
		}

		if (action != 2) {
			if (job->memory_allocated[n] == 0)
				continue;	/* node lost by job resizing */
			select_node_usage[i].alloc_memory +=
				job->memory_allocated[n];
			if ((select_node_usage[i].alloc_memory >
			     select_node_record[i].real_memory)) {
				error("%s: %s: node %s memory is "
				      "overallocated (%"PRIu64") for %pJ",
				      plugin_type, __func__, node_ptr->name,
				      select_node_usage[i].alloc_memory,
				      job_ptr);
			}
		}
		if ((powercap_get_cluster_current_cap() != 0) &&
		    (which_power_layout() == 2)) {
			adapt_layouts(job, job_ptr->details->cpu_freq_max, n,
				      node_ptr->name, true);
		}
	}

	if (action != 2) {
		gres_build_job_details(job_ptr->gres_list,
				       &job_ptr->gres_detail_cnt,
				       &job_ptr->gres_detail_str);
	}

	/* add cores */
	if (action != 1) {
		for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr == job_ptr->part_ptr)
				break;
		}
		if (!p_ptr) {
			char *part_name;
			if (job_ptr->part_ptr)
				part_name = job_ptr->part_ptr->name;
			else
				part_name = job_ptr->partition;
			error("%s: %s: could not find partition %s",
			      plugin_type, __func__, part_name);
			return SLURM_ERROR;
		}
		if (!p_ptr->row) {
			p_ptr->row = xcalloc(p_ptr->num_rows,
					     sizeof(struct part_row_data));
		}

		/* find a row to add this job */
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!common_job_fit_in_row(job, &(p_ptr->row[i])))
				continue;
			debug3("%s: %s: adding %pJ to part %s row %u",
			       plugin_type, __func__, job_ptr,
			       p_ptr->part_ptr->name, i);
			common_add_job_to_row(job, &(p_ptr->row[i]));
			break;
		}
		if (i >= p_ptr->num_rows) {
			/*
			 * Job started or resumed and it's allocated resources
			 * are already in use by some other job. Typically due
			 * to manually resuming a job.
			 */
			error("%s: %s: job overflow: "
			      "could not find idle resources for %pJ",
			      plugin_type, __func__, job_ptr);
			/* No row available to record this job */
		}
		/* update the node state */
		for (i = i_first, n = -1; i <= i_last; i++) {
			if (bit_test(job->node_bitmap, i)) {
				n++;
				if (job->cpus[n] == 0)
					continue;  /* node lost by job resize */
				select_node_usage[i].node_state +=
					job->node_req;
			}
		}
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("DEBUG: %s (after):", __func__);
			common_dump_parts(p_ptr);
		}
	}
	return SLURM_SUCCESS;
}

/*
 * Deallocate resources previously allocated to the given job
 * - subtract 'struct job_resources' resources from 'struct part_res_record'
 * - subtract job's memory requirements from 'struct node_res_record'
 *
 * if action = 0 then subtract cores, memory + GRES (running job was terminated)
 * if action = 1 then subtract memory + GRES (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 * IN: job_fini - job fully terminating on this node (not just a test)
 *
 * RET SLURM_SUCCESS or error code
 *
 * See also: common_add_job_to_res()
 */
extern int common_rm_job_res(struct part_res_record *part_record_ptr,
			     struct node_use_record *node_usage,
			     struct job_record *job_ptr, int action,
			     bool job_fini)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	int i_first, i_last;
	int i, n;
	List gres_list;
	bool old_job = false;

	if (select_state_initializing) {
		/*
		 * Ignore job removal until select/cons_tres data structures
		 * values are set by select_p_reconfigure()
		 */
		info("%s: %s: plugin still initializing",
		     plugin_type, __func__);
		return SLURM_SUCCESS;
	}
	if (!job || !job->core_bitmap) {
		if (job_ptr->details && (job_ptr->details->min_nodes == 0))
			return SLURM_SUCCESS;
		error("%s: %s: %pJ has no job_resrcs info",
		      plugin_type, __func__, job_ptr);
		return SLURM_ERROR;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: %pJ action %d", plugin_type, __func__,
		     job_ptr, action);
		log_job_resources(job_ptr);
		_log_tres_state(node_usage, part_record_ptr);
	} else {
		debug3("%s: %s: %pJ action %d", plugin_type, __func__,
		       job_ptr, action);
	}
	if (job_ptr->start_time < slurmctld_config.boot_time)
		old_job = true;
	i_first = bit_ffs(job->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job->node_bitmap);
	else
		i_last = -2;
	for (i = i_first, n = -1; i <= i_last; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		n++;
		if (job->cpus[n] == 0)
			continue;  /* node lost by job resize */

		node_ptr = node_record_table_ptr + i;
		if (action != 2) {
			if (node_usage[i].gres_list)
				gres_list = node_usage[i].gres_list;
			else
				gres_list = node_ptr->gres_list;
			gres_plugin_job_dealloc(job_ptr->gres_list, gres_list,
						n, job_ptr->job_id,
						node_ptr->name, old_job,
						job_ptr->user_id, job_fini);
			gres_plugin_node_state_log(gres_list, node_ptr->name);
		}

		if (action != 2) {
			if (node_usage[i].alloc_memory <
			    job->memory_allocated[n]) {
				error("%s: %s: node %s memory is "
				      "under-allocated (%"PRIu64"-%"PRIu64") "
				      "for %pJ",
				      plugin_type, __func__, node_ptr->name,
				      node_usage[i].alloc_memory,
				      job->memory_allocated[n],
				      job_ptr);
				node_usage[i].alloc_memory = 0;
			} else {
				node_usage[i].alloc_memory -=
					job->memory_allocated[n];
			}
		}
		if ((powercap_get_cluster_current_cap() != 0) &&
		    (which_power_layout() == 2)) {
			adapt_layouts(job, job_ptr->details->cpu_freq_max, n,
				      node_ptr->name, false);
		}
	}

	/* subtract cores */
	if (action != 1) {
		/* reconstruct rows with remaining jobs */
		struct part_res_record *p_ptr;

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
			      plugin_type, __func__, job_ptr,
			      job_ptr->part_ptr->name);
			return SLURM_ERROR;
		}

		if (!p_ptr->row)
			return SLURM_SUCCESS;

		/* remove the job from the job_list */
		n = 0;
		for (i = 0; i < p_ptr->num_rows; i++) {
			uint32_t j;
			for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
				if (p_ptr->row[i].job_list[j] != job)
					continue;
				debug3("%s: %s: removed %pJ from part %s row %u",
				       plugin_type, __func__, job_ptr,
				       p_ptr->part_ptr->name, i);
				for ( ; j < p_ptr->row[i].num_jobs-1; j++) {
					p_ptr->row[i].job_list[j] =
						p_ptr->row[i].job_list[j+1];
				}
				p_ptr->row[i].job_list[j] = NULL;
				p_ptr->row[i].num_jobs--;
				/* found job - we're done */
				n = 1;
				i = p_ptr->num_rows;
				break;
			}
		}
		if (n) {
			/* job was found and removed, so refresh the bitmaps */
			_build_row_bitmaps(p_ptr, job_ptr);
			/*
			 * Adjust the node_state of all nodes affected by
			 * the removal of this job. If all cores are now
			 * available, set node_state = NODE_CR_AVAILABLE
			 */
			for (i = i_first, n = -1; i <= i_last; i++) {
				if (bit_test(job->node_bitmap, i) == 0)
					continue;
				n++;
				if (job->cpus[n] == 0)
					continue;  /* node lost by job resize */
				if (node_usage[i].node_state >=
				    job->node_req) {
					node_usage[i].node_state -=
						job->node_req;
				} else {
					node_ptr = node_record_table_ptr + i;
					error("%s: %s: node_state mis-count "
					      "(%pJ job_cnt:%u node:%s node_cnt:%u)",
					      plugin_type, __func__, job_ptr,
					      job->node_req, node_ptr->name,
					      node_usage[i].node_state);
					node_usage[i].node_state =
						NODE_CR_AVAILABLE;
				}
			}
		}
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: %pJ finished", plugin_type, __func__, job_ptr);
		_log_tres_state(node_usage, part_record_ptr);
	}

	return SLURM_SUCCESS;
}

/*
 * Add job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT sys_resrcs_ptr - bitmap array (one per node) of available cores,
 *			   allocated as needed
 * NOTE: Patterned after add_job_to_cores() in src/common/job_resources.c
 */
extern void common_add_job_cores(job_resources_t *job_resrcs_ptr,
				 bitstr_t ***sys_resrcs_ptr)
{
	(void)_handle_job_res(job_resrcs_ptr, sys_resrcs_ptr,
			      HANDLE_JOB_RES_ADD);
}


/*
 * Remove job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT full_bitmap - bitmap of available CPUs, allocate as needed
 */
extern void common_rm_job_cores(job_resources_t *job_resrcs_ptr,
				bitstr_t ***sys_resrcs_ptr)
{
	(void)_handle_job_res(job_resrcs_ptr, sys_resrcs_ptr,
			      HANDLE_JOB_RES_REM);
}

/*
 * Test if job can fit into the given set of core_bitmaps
 * IN job_resrcs_ptr - resources allocated to a job
 * IN r_ptr - row we are trying to fit
 * RET 1 on success, 0 otherwise
 * NOTE: Patterned after job_fits_into_cores() in src/common/job_resources.c
 */
extern int common_job_fit_in_row(job_resources_t *job_resrcs_ptr,
				 struct part_row_data *r_ptr)
{
	if ((r_ptr->num_jobs == 0) || !r_ptr->row_bitmap)
		return 1;

	return _handle_job_res(job_resrcs_ptr, &r_ptr->row_bitmap,
			       HANDLE_JOB_RES_TEST);
}

/* Log contents of partition structure */
extern void common_dump_parts(struct part_res_record *p_ptr)
{
	uint32_t n, r;
	struct node_record *node_ptr;

	info("part:%s rows:%u prio:%u ", p_ptr->part_ptr->name, p_ptr->num_rows,
	     p_ptr->part_ptr->priority_tier);

	xassert(core_array_size);

	if (!p_ptr->row)
		return;

	for (r = 0; r < p_ptr->num_rows; r++) {
		char str[64]; /* print first 64 bits of bitmaps */
		char *sep = "", *tmp = NULL;
		int max_nodes_rep = 4;	/* max 4 allocated nodes to report */

		if (!p_ptr->row[r].row_bitmap)
			continue;

		for (n = 0; n < core_array_size; n++) {
			if (!p_ptr->row[r].row_bitmap[n] ||
			    !bit_set_count(p_ptr->row[r].row_bitmap[n]))
				continue;
			node_ptr = node_record_table_ptr + n;
			bit_fmt(str, sizeof(str), p_ptr->row[r].row_bitmap[n]);
			xstrfmtcat(tmp, "%salloc_cores[%s]:%s",
				   sep, node_ptr->name, str);
			sep = ",";
			if (--max_nodes_rep == 0)
				break;
		}
		info(" row:%u num_jobs:%u: %s", r, p_ptr->row[r].num_jobs, tmp);
		xfree(tmp);
	}
}

/* sort the rows of a partition from "most allocated" to "least allocated" */
extern void common_sort_part_rows(struct part_res_record *p_ptr)
{
	uint32_t i, j, b, n, r;
	uint32_t *a;

	if (!p_ptr->row)
		return;

	a = xcalloc(p_ptr->num_rows, sizeof(uint32_t));
	for (r = 0; r < p_ptr->num_rows; r++) {
		if (!p_ptr->row[r].row_bitmap)
			continue;

		for (n = 0; n < core_array_size; n++) {
			if (!p_ptr->row[r].row_bitmap[n])
				continue;
			a[r] += bit_set_count(p_ptr->row[r].row_bitmap[n]);
		}
	}
	for (i = 0; i < p_ptr->num_rows; i++) {
		for (j = i + 1; j < p_ptr->num_rows; j++) {
			if (a[j] > a[i]) {
				b = a[j];
				a[j] = a[i];
				a[i] = b;
				_swap_rows(&(p_ptr->row[i]), &(p_ptr->row[j]));
			}
		}
	}
	xfree(a);

	return;
}

/* Create a duplicate part_row_data struct */
extern struct part_row_data *common_dup_row_data(struct part_row_data *orig_row,
						 uint16_t num_rows)
{
	struct part_row_data *new_row;
	int i, n;

	if (num_rows == 0 || !orig_row)
		return NULL;

	new_row = xcalloc(num_rows, sizeof(struct part_row_data));
	for (i = 0; i < num_rows; i++) {
		new_row[i].num_jobs = orig_row[i].num_jobs;
		new_row[i].job_list_size = orig_row[i].job_list_size;
		if (orig_row[i].row_bitmap) {
			new_row[i].row_bitmap = build_core_array();
			for (n = 0; n < core_array_size; n++) {
				if (!orig_row[i].row_bitmap[n])
					continue;
				new_row[i].row_bitmap[n] =
					bit_copy(orig_row[i].row_bitmap[n]);
			}
		}
		if (new_row[i].job_list_size == 0)
			continue;
		/* copy the job list */
		new_row[i].job_list = xcalloc(new_row[i].job_list_size,
					      sizeof(struct job_resources *));
		memcpy(new_row[i].job_list, orig_row[i].job_list,
		       (sizeof(struct job_resources *) * new_row[i].num_jobs));
	}
	return new_row;
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

	_destroy_node_data(select_node_usage, select_node_record);
	select_node_record = NULL;
	select_node_usage = NULL;
	_destroy_part_data(select_part_record);
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
	struct node_record *node_ptr;
	bitstr_t *core_map = NULL;
	uint16_t use_spec_cores = slurm_get_use_spec_resources();
	struct node_res_record *node_res_ptr = NULL;
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
				if (!bit_test(node_ptr->node_spec_bitmap, 1)) {
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
extern avail_res_t *common_allocate_cores(struct job_record *job_ptr,
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
extern avail_res_t *common_allocate_sockets(struct job_record *job_ptr,
					    bitstr_t *core_map,
					    bitstr_t *part_core_map,
					    const uint32_t node_i,
					    int *cpu_alloc_size,
					    bitstr_t *req_sock_map)
{
	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, true, req_sock_map);
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
extern int common_job_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
			   uint32_t min_nodes, uint32_t max_nodes,
			   uint32_t req_nodes, uint16_t mode,
			   List preemptee_candidates,
			   List *preemptee_job_list,
			   bitstr_t **exc_cores)
{
	int i, rc = EINVAL;
	uint16_t job_node_req;
	char tmp[128];

	if (slurm_get_use_spec_resources() == 0)
		job_ptr->details->core_spec = NO_VAL16;
	if ((job_ptr->details->core_spec != NO_VAL16) &&
	    (job_ptr->details->whole_node != 1)) {
		info("%s: %s: Setting Exclusive mode for %pJ with CoreSpec=%u",
		      plugin_type, __func__, job_ptr,
		      job_ptr->details->core_spec);
		job_ptr->details->whole_node = 1;
	}

	if (!job_ptr->details->mc_ptr)
		job_ptr->details->mc_ptr = _create_default_mc();
	job_node_req = _get_job_node_req(job_ptr);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *node_mode = "Unknown", *alloc_mode = "Unknown";
		char *core_list = NULL, *node_list, *sep = "";
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
		info("%s: %s: %pJ node_mode:%s alloc_mode:%s",
		     plugin_type, __func__, job_ptr, node_mode, alloc_mode);
		if (exc_cores) {
			for (i = 0; i < core_array_size; i++) {
				if (!exc_cores[i])
					continue;
				bit_fmt(tmp, sizeof(tmp), exc_cores[i]);
				xstrfmtcat(core_list, "%snode[%d]:%s", sep, i,
					   tmp);
				sep = ",";
			}
		} else {
			core_list = xstrdup("NONE");
		}
		node_list = bitmap2node_name(node_bitmap);
		info("%s: %s: node_list:%s exc_cores:%s", plugin_type, __func__,
		     node_list, core_list);
		xfree(node_list);
		xfree(core_list);
		info("%s: %s: nodes: min:%u max:%u requested:%u avail:%u",
		     plugin_type, __func__, min_nodes, max_nodes, req_nodes,
		     bit_set_count(node_bitmap));
		_dump_nodes();
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
		error("%s: %s: Mode %d is invalid",
		      plugin_type, __func__, mode);
		return EINVAL;
	}

	if ((select_debug_flags & DEBUG_FLAG_CPU_BIND) ||
	    (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)) {
		if (job_ptr->job_resrcs) {
			if (rc != SLURM_SUCCESS) {
				info("%s: %s: error:%s", plugin_type, __func__,
				     slurm_strerror(rc));
			}
			log_job_resources(job_ptr);
			if (is_cons_tres)
				gres_plugin_job_state_log(job_ptr->gres_list,
							  job_ptr->job_id);
		} else {
			info("%s: %s: no job_resources info for %pJ rc=%d",
			     plugin_type, __func__, job_ptr, rc);
		}
	}

	return rc;
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
extern bool select_p_node_ranking(struct node_record *node_ptr, int node_cnt)
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
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
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
	select_fast_schedule = slurm_get_fast_schedule();
	cr_init_global_core_data(node_ptr, node_cnt, select_fast_schedule);

	_destroy_node_data(select_node_usage, select_node_record);
	select_node_cnt = node_cnt;

	if (is_cons_tres)
		core_array_size = select_node_cnt;

	select_node_record = xcalloc(select_node_cnt,
				     sizeof(struct node_res_record));
	select_node_usage  = xcalloc(select_node_cnt,
				     sizeof(struct node_use_record));

	for (i = 0; i < select_node_cnt; i++) {
		select_node_record[i].node_ptr = &node_ptr[i];
		select_node_record[i].mem_spec_limit =
			node_ptr[i].mem_spec_limit;
		if (select_fast_schedule) {
			struct config_record *config_ptr;
			config_ptr = node_ptr[i].config_ptr;
			select_node_record[i].cpus    = config_ptr->cpus;
			select_node_record[i].boards  = config_ptr->boards;
			select_node_record[i].sockets = config_ptr->sockets;
			select_node_record[i].cores   = config_ptr->cores;
			select_node_record[i].threads = config_ptr->threads;
			select_node_record[i].vpus    = config_ptr->threads;
			select_node_record[i].real_memory =
				config_ptr->real_memory;
		} else {
			select_node_record[i].cpus    = node_ptr[i].cpus;
			select_node_record[i].boards  = node_ptr[i].boards;
			select_node_record[i].sockets = node_ptr[i].sockets;
			select_node_record[i].cores   = node_ptr[i].cores;
			select_node_record[i].threads = node_ptr[i].threads;
			select_node_record[i].vpus    = node_ptr[i].threads;
			select_node_record[i].real_memory =
				node_ptr[i].real_memory;
		}
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
		select_node_usage[i].node_state = NODE_CR_AVAILABLE;
		gres_plugin_node_state_dealloc_all(
			select_node_record[i].node_ptr->gres_list);
	}
	_create_part_data();
	_dump_nodes();

	return SLURM_SUCCESS;
}

extern int select_p_block_init(List part_list)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	int i, i_first, i_last;
	struct node_record *node_ptr;

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

extern int select_p_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
	job_resources_t *from_job_resrcs_ptr, *to_job_resrcs_ptr,
			*new_job_resrcs_ptr;
	struct node_record *node_ptr;
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

	(void) common_rm_job_res(select_part_record, select_node_usage,
				 from_job_ptr, 0, true);
	(void) common_rm_job_res(select_part_record, select_node_usage,
				 to_job_ptr, 0, true);

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
	build_job_resources(new_job_resrcs_ptr, node_record_table_ptr,
			    select_fast_schedule);
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

	(void) common_add_job_to_res(to_job_ptr, 0);

	return SLURM_SUCCESS;
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	struct part_res_record *part_record_ptr = select_part_record;
	struct node_use_record *node_usage = select_node_usage;
	struct job_resources *job = job_ptr->job_resrcs;
	struct part_res_record *p_ptr;
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
	common_build_row_bitmaps(p_ptr, NULL);

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

extern int select_p_job_signal(struct job_record *job_ptr, int signal)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	return SLURM_SUCCESS;
}

extern int select_p_job_mem_confirm(struct job_record *job_ptr)
{
	int i_first, i_last, i, offset;
	uint64_t avail_mem, lowest_mem = 0;

	xassert(job_ptr);

	if (((job_ptr->bit_flags & NODE_MEM_CALC) == 0) ||
	    (select_fast_schedule != 0))
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

extern int select_p_job_fini(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("%s: %s: %pJ", plugin_type, __func__, job_ptr);

	common_rm_job_res(select_part_record, select_node_usage,
			  job_ptr, 0, true);

	return SLURM_SUCCESS;
}

/* NOTE: This function is not called with gang scheduling because it
 * needs to track how many jobs are running or suspended on each node.
 * This sum is compared with the partition's Shared parameter */
extern int select_p_job_suspend(struct job_record *job_ptr, bool indf_susp)
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

	return common_rm_job_res(select_part_record, select_node_usage,
				 job_ptr, 2, false);
}

/* See NOTE with select_p_job_suspend() above */
extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
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

	return common_add_job_to_res(job_ptr, 2);
}

extern bitstr_t *select_p_step_pick_nodes(struct job_record *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count,
					  bitstr_t **avail_nodes)
{
	return NULL;
}

/* Unused for this plugin */
extern int select_p_step_start(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_step_finish(struct step_record *step_ptr, bool killing_step)
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
	struct part_res_record *p_ptr;
	struct node_record *node_ptr = NULL;
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

		if (slurmctld_conf.fast_schedule) {
			node_boards  = node_ptr->config_ptr->boards;
			node_sockets = node_ptr->config_ptr->sockets;
			node_cores   = node_ptr->config_ptr->cores;
			node_cpus    = node_ptr->config_ptr->cpus;
			node_threads = node_ptr->config_ptr->threads;
		} else {
			node_boards  = node_ptr->boards;
			node_sockets = node_ptr->sockets;
			node_cores   = node_ptr->cores;
			node_cpus    = node_ptr->cpus;
			node_threads = node_ptr->threads;
		}

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

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	int rc;

	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (IS_JOB_RUNNING(job_ptr))
		rc = common_add_job_to_res(job_ptr, 0);
	else if (IS_JOB_SUSPENDED(job_ptr)) {
		if (job_ptr->priority == 0)
			rc = common_add_job_to_res(job_ptr, 1);
		else	/* Gang schedule suspend */
			rc = common_add_job_to_res(job_ptr, 0);
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
					 struct job_record *job_ptr,
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
	if ((select_fast_schedule == 1) &&
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

	if (select_fast_schedule)
		return SLURM_SUCCESS;

	select_node_record[index].real_memory =
		select_node_record[index].node_ptr->real_memory;
	select_node_record[index].mem_spec_limit =
		select_node_record[index].node_ptr->mem_spec_limit;

	return SLURM_SUCCESS;
}

/* Unused for this plugin */
extern int select_p_update_node_state(struct node_record *node_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	int cleaning_job_cnt = 0, rc = SLURM_SUCCESS, run_time;
	time_t now = time(NULL);

	info("%s: reconfigure", plugin_type);
	select_debug_flags = slurm_get_debug_flags();

	if (is_cons_tres) {
		def_cpu_per_gpu = 0;
		def_mem_per_gpu = 0;
		if (slurmctld_conf.job_defaults_list) {
			def_cpu_per_gpu = _get_def_cpu_per_gpu(
				slurmctld_conf.job_defaults_list);
			def_mem_per_gpu = _get_def_mem_per_gpu(
				slurmctld_conf.job_defaults_list);
		}
	}

	rc = select_p_node_init(node_record_table_ptr, node_record_count);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* reload job data */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_RUNNING(job_ptr)) {
			/* add the job */
			common_add_job_to_res(job_ptr, 0);
		} else if (IS_JOB_SUSPENDED(job_ptr)) {
			/* add the job in a suspended state */
			if (job_ptr->priority == 0)
				(void) common_add_job_to_res(job_ptr, 1);
			else	/* Gang schedule suspend */
				(void) common_add_job_to_res(job_ptr, 0);
		} else if (_job_cleaning(job_ptr)) {
			cleaning_job_cnt++;
			run_time = (int) difftime(now, job_ptr->end_time);
			if (run_time >= 300) {
				info("%pJ NHC hung for %d secs, releasing resources now, may underflow later",
				     job_ptr, run_time);
				/* If/when NHC completes, it will release
				 * resources that are not marked as allocated
				 * to this job without line below. */
				//common_add_job_to_res(job_ptr, 0);
				uint16_t released = 1;
				select_g_select_jobinfo_set(
						       job_ptr->select_jobinfo,
						       SELECT_JOBDATA_RELEASED,
						       &released);
			} else {
				common_add_job_to_res(job_ptr, 0);
			}
		}
	}
	list_iterator_destroy(job_iterator);
	select_state_initializing = false;

	if (cleaning_job_cnt) {
		info("%d jobs are in cleaning state (running Node Health Check)",
		     cleaning_job_cnt);
	}

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
	uint32_t *core_cnt, flags;
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
	flags = resv_desc_ptr->flags;

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

	if ((flags & RESERVE_FLAG_FIRST_CORES) && core_cnt) {
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
				if (!bit_test(use_exc_bitmap, set) &&
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
