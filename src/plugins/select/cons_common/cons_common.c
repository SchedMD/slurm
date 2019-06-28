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

#include "cons_common.h"

#include "src/common/xstring.h"
#include "src/common/gres.h"

#include "src/slurmctld/powercapping.h"

/* init common global variables */
bool     backfill_busy_nodes  = false;
int      bf_window_scale      = 0;
cons_common_callbacks_t cons_common_callbacks = {0};
uint16_t cr_type              = CR_CPU; /* cr_type is overwritten in init() */
bool     gang_mode            = false;
bool     have_dragonfly       = false;
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
}
