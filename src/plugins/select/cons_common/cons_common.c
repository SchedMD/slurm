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

#include "src/common/gres.h"
#include "src/common/node_select.h"
#include "src/common/slurm_selecttype_info.h"
#include "src/common/xstring.h"

#include "src/slurmctld/powercapping.h"
#include "src/slurmctld/preempt.h"

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

struct part_res_record *select_part_record = NULL;
struct node_res_record *select_node_record = NULL;
struct node_use_record *select_node_usage  = NULL;

/* Global variables */
static uint64_t   def_cpu_per_gpu	= 0;
static uint64_t   def_mem_per_gpu	= 0;
static int        preempt_reorder_cnt	= 1;
static bool       preempt_strict_order = false;
static bool       select_state_initializing = true;

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

static char *_node_state_str(uint16_t node_state)
{
	if (node_state >= NODE_CR_RESERVED)
		return "reserved";	/* Exclusive allocation */
	if (node_state >= NODE_CR_ONE_ROW)
		return "one_row";	/* Dedicated core for this partition */
	return "available";		/* Idle or in-use (shared) */
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

/* (re)create the global select_part_record array */
static void _create_part_data(void)
{
	List part_rec_list = NULL;
	ListIterator part_iterator;
	struct part_record *p_ptr;
	struct part_res_record *this_ptr, *last_ptr = NULL;
	int num_parts;

	common_destroy_part_data(select_part_record);
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
/* helper script for common_sort_part_rows() */
static void _swap_rows(struct part_row_data *a, struct part_row_data *b)
{
	struct part_row_data tmprow;

	memcpy(&tmprow, a, sizeof(struct part_row_data));
	memcpy(a, b, sizeof(struct part_row_data));
	memcpy(b, &tmprow, sizeof(struct part_row_data));
}

/* Delete the given select_node_record and select_node_usage arrays */
extern void common_destroy_node_data(struct node_use_record *node_usage,
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
extern void common_destroy_part_data(struct part_res_record *this_ptr)
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

/* Delete the given partition row data */
extern void common_destroy_row_data(
	struct part_row_data *row, uint16_t num_rows)
{
	uint32_t r, n;

	for (r = 0; r < num_rows; r++) {
		if (row[r].row_bitmap) {
			xassert(row[r].row_bitmap_size);

			for (n = 0; n < row[r].row_bitmap_size; n++)
				FREE_NULL_BITMAP(row[r].row_bitmap[n]);
			xfree(row[r].row_bitmap);
		}
		xfree(row[r].job_list);
	}

	xfree(row);
}

extern int common_cpus_per_core(struct job_details *details, int node_inx)
{
	uint16_t ncpus_per_core = 0xffff;	/* Usable CPUs per core */
	uint16_t threads_per_core = select_node_record[node_inx].vpus;

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
	xassert(*cons_common_callbacks.add_job_to_res);

	/* add the job to the row_bitmap */
	if (r_ptr->row_bitmap && (r_ptr->num_jobs == 0)) {
		/* if no jobs, clear the existing row_bitmap first */
		common_clear_row_bitmap(r_ptr);
	}

	(*cons_common_callbacks.add_job_to_res)(job, r_ptr, cr_node_num_cores);

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
 * See also: rm_job_res() in job_test.c
 */
extern int common_add_job_to_res(struct job_record *job_ptr, int action)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	struct part_res_record *p_ptr;
	List node_gres_list;
	int i, i_first, i_last, n;
	bitstr_t *core_bitmap;

	xassert(*cons_common_callbacks.can_job_fit_in_row);

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
	if (i_first == -1)
		i_last = -2;
	else
		i_last = bit_fls(job->node_bitmap);
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
			if (!(*cons_common_callbacks.can_job_fit_in_row)(
				    job, &(p_ptr->row[i])))
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

/* Log contents of partition structure */
extern void common_dump_parts(struct part_res_record *p_ptr)
{
	uint32_t n, r;
	struct node_record *node_ptr;

	info("part:%s rows:%u prio:%u ", p_ptr->part_ptr->name, p_ptr->num_rows,
	     p_ptr->part_ptr->priority_tier);

	if (!p_ptr->row)
		return;

	for (r = 0; r < p_ptr->num_rows; r++) {
		char str[64]; /* print first 64 bits of bitmaps */
		char *sep = "", *tmp = NULL;
		int max_nodes_rep = 4;	/* max 4 allocated nodes to report */

		if (!p_ptr->row[r].row_bitmap)
			continue;

		xassert(p_ptr->row[r].row_bitmap_size);

		for (n = 0; n < p_ptr->row[r].row_bitmap_size; n++) {
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

/* Clear all elements the row_bitmap of the row */
extern void common_clear_row_bitmap(struct part_row_data *r_ptr)
{
	int n;

	xassert(r_ptr);

	if (!r_ptr->row_bitmap)
		return;

	xassert(r_ptr->row_bitmap_size);

	for (n = 0; n < r_ptr->row_bitmap_size; n++) {
		if (r_ptr->row_bitmap[n])
			bit_clear_all(r_ptr->row_bitmap[n]);
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

		xassert(p_ptr->row[r].row_bitmap_size);

		for (n = 0; n < p_ptr->row[r].row_bitmap_size; n++) {
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

/* Create a duplicate part_res_record list */
extern struct part_res_record *common_dup_part_data(
	struct part_res_record *orig_ptr)
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
			xassert(orig_row[i].row_bitmap_size);

			new_row[i].row_bitmap =
				xcalloc(orig_row[i].row_bitmap_size,
					sizeof(bitstr_t *));
			new_row[i].row_bitmap_size =
				orig_row[i].row_bitmap_size;
			for (n = 0; n < orig_row[i].row_bitmap_size; n++) {
				if (!orig_row[i].row_bitmap[n])
					continue;
				new_row[i].row_bitmap[n] =
					bit_copy(orig_row[i].row_bitmap[n]);
			}
			new_row[i].first_row_bitmap = new_row[i].row_bitmap[0];
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

	common_destroy_node_data(select_node_usage, select_node_record);
	select_node_record = NULL;
	select_node_usage = NULL;
	common_destroy_part_data(select_part_record);
	select_part_record = NULL;
	cr_fini_global_core_data();
}

extern int common_node_init(struct node_record *node_ptr, int node_cnt)
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

	common_destroy_node_data(select_node_usage, select_node_record);
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

extern int common_reconfig(void)
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

	rc = common_node_init(node_record_table_ptr, node_record_count);
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
