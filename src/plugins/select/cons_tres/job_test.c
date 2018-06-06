/*****************************************************************************\
 *  job_test.c - Determine if job can be allocated resources.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Derived in large part from select/cons_res plugin
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <string.h>
#include "select_cons_tres.h"
#include "dist_tasks.h"
#include "job_test.h"

#define _DEBUG 1	/* Enables module specific debugging */

/* Local functions */
static void _block_whole_nodes(bitstr_t *node_bitmap,
			       bitstr_t **orig_core_bitmap,
			       bitstr_t **new_core_bitmap);
static void _build_row_bitmaps(struct part_res_record *p_ptr,
			       struct job_record *job_ptr);
static int  _compare_support(const void *v, const void *v1);
static void _cpus_to_use(int *avail_cpus, int rem_cpus, int rem_nodes,
			 struct job_details *details_ptr, uint16_t *cpu_cnt,
			 int node_inx, uint16_t cr_type);
static int _cr_job_list_sort(void *x, void *y);
static struct node_use_record *_dup_node_usage(
					struct node_use_record *orig_ptr);
static struct part_res_record *_dup_part_data(struct part_res_record *orig_ptr);
static struct part_row_data *_dup_row_data(struct part_row_data *orig_row,
					   uint16_t num_rows);
static bool _enough_nodes(int avail_nodes, int rem_nodes,
			  uint32_t min_nodes, uint32_t req_nodes);
static int _eval_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			uint32_t min_nodes, uint32_t max_nodes,
			uint32_t req_nodes, uint16_t *cpu_cnt,
			uint16_t cr_type, bool prefer_alloc_nodes);
static void _get_res_avail(struct job_record *job_ptr, bitstr_t *node_map,
			   bitstr_t **core_map,
			   struct node_use_record *node_usage,
			   uint16_t cr_type, uint16_t **cpu_cnt_ptr,
			   bool test_only, bitstr_t **part_core_map);
static time_t _guess_job_end(struct job_record * job_ptr, time_t now);
static int _is_node_busy(struct part_res_record *p_ptr, uint32_t node_i,
			 int sharing_only, struct part_record *my_part_ptr,
			 bool qos_preemptor);
static int _job_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
		     uint32_t min_nodes, uint32_t max_nodes,
		     uint32_t req_nodes, int mode, uint16_t cr_type,
		     enum node_cr_state job_node_req,
		     struct part_res_record *cr_part_ptr,
		     struct node_use_record *node_usage,
		     bitstr_t **exc_cores, bool prefer_alloc_nodes,
		     bool qos_preemptor, bool preempt_mode);
static inline void _log_select_maps(char *loc, bitstr_t *node_map,
				    bitstr_t **core_map);
static int _rm_job_from_res(struct part_res_record *part_record_ptr,
			    struct node_use_record *node_usage,
			    struct job_record *job_ptr, int action);
static void _rm_job_res(job_resources_t *job_resrcs_ptr,
			bitstr_t ***sys_resrcs_ptr);
static uint16_t *_select_nodes(struct job_record *job_ptr, uint32_t min_nodes,
				uint32_t max_nodes, uint32_t req_nodes,
				bitstr_t *node_map, bitstr_t **avail_cores,
				struct node_use_record *node_usage,
				uint16_t cr_type, bool test_only,
				bitstr_t **part_core_map,
				bool prefer_alloc_nodes);
static int _sort_usable_nodes_dec(void *j1, void *j2);
static int _verify_node_state(struct part_res_record *cr_part_ptr,
			      struct job_record *job_ptr,
			      bitstr_t *node_bitmap,
			      uint16_t cr_type,
			      struct node_use_record *node_usage,
			      enum node_cr_state job_node_req,
			      bitstr_t **exc_cores, bool qos_preemptor);

struct sort_support {
	int jstart;
	struct job_resources *tmpjobs;
};

/*
 * Add job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT sys_resrcs_ptr - bitmap array (one per node) of available cores,
 *			   allocated as needed
 * RET 1 on success, 0 otherwise
 * NOTE: Patterned after add_job_to_cores() in src/common/job_resources.c
 */
extern void add_job_res(job_resources_t *job_resrcs_ptr,
			bitstr_t ***sys_resrcs_ptr)
{
//FIXME: Add other resources than CPUs (e.g. GPUs), lower priority work
//FIXME: Change argument to job pointer? Enhance contents of job_resources_t?, lower priority work
	int i, i_first, i_last;
	int c, c_job, c_off = 0, c_max;
	int rep_inx = 0, rep_offset = -1;
	bitstr_t **local_resrcs_ptr;

	if (!job_resrcs_ptr->core_bitmap)
		return;

	/* add the job to the row_bitmap */
	if (*sys_resrcs_ptr == NULL) {
		local_resrcs_ptr = xmalloc(sizeof(bitstr_t *) * select_node_cnt);
		*sys_resrcs_ptr = local_resrcs_ptr;
		for (i = 0; i < select_node_cnt; i++) {
			local_resrcs_ptr[i] =
				bit_alloc(select_node_record[i].tot_cores);
		}
	} else
		local_resrcs_ptr = *sys_resrcs_ptr;

	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job_resrcs_ptr->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			continue;
		if (job_resrcs_ptr->whole_node) {
			bit_set_all(local_resrcs_ptr[i]);
			continue;
		}
		rep_offset++;
		if (rep_offset > job_resrcs_ptr->sock_core_rep_count[rep_inx]) {
			rep_offset = 0;
			rep_inx++;
		}
		c_job = job_resrcs_ptr->sockets_per_node[rep_inx] *
			job_resrcs_ptr->cores_per_socket[rep_inx];
		c_max = MIN(select_node_record[i].tot_cores, c_job);
		for (c = 0; c < c_max; c++) {
			if (!bit_test(job_resrcs_ptr->core_bitmap, c_off + c))
				continue;
			bit_set(local_resrcs_ptr[i], c);
		}
		c_off += c_job;
	}
}

/*
 * Add job resource use to the partition data structure
 */
extern void add_job_to_row(struct job_resources *job,
			   struct part_row_data *r_ptr)
{
	/* add the job to the row_bitmap */
	if (r_ptr->row_bitmap && (r_ptr->num_jobs == 0)) {
		/* if no jobs, clear the existing row_bitmap first */
		clear_core_array(r_ptr->row_bitmap);
	}
	add_job_res(job, &r_ptr->row_bitmap);

	/*  add the job to the job_list */
	if (r_ptr->num_jobs >= r_ptr->job_list_size) {
		r_ptr->job_list_size += 8;
		xrealloc(r_ptr->job_list, r_ptr->job_list_size *
			 sizeof(struct job_resources *));
	}
	r_ptr->job_list[r_ptr->num_jobs++] = job;
}

/*
 * When any cores on a node are removed from being available for a job,
 * then remove the entire node from being available.
 */
static void _block_whole_nodes(bitstr_t *node_bitmap,
			       bitstr_t **orig_core_bitmap,
			       bitstr_t **new_core_bitmap)
{
	int c, i, i_first, i_last;

	i_first = bit_ffs(node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(node_bitmap);
	else
		i_last = -2;

	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		for (c = 0; c < select_node_record[i].tot_cores; c++) {
			if ( bit_test(orig_core_bitmap[i], c) &&
			    !bit_test(new_core_bitmap[i],  c)) {
				bit_clear(node_bitmap, i);
				break;
			}
		}
	}
}

#if _DEBUG
static inline char *_build_core_str(bitstr_t **row_bitmap)
{
	char *result = NULL, *sep = "", tmp[128];
	int i;

	if (row_bitmap) {
		for (i = 0; i < select_node_cnt; i++) {
			if (!row_bitmap[i] || (bit_ffs(row_bitmap[i]) == -1))
				continue;
			bit_fmt(tmp, sizeof(tmp), row_bitmap[i]);
			xstrfmtcat(result, "%sCores[%d]:%s", sep, i, tmp);
			sep = " ";
		}
	}
	if (!result)
		result = xstrdup("NONE");

	return result;
}

static inline char *_node_state_str(uint16_t node_state)
{
	static char tmp[32];
	if (node_state == NODE_CR_AVAILABLE)
		return "Avail";
	if (node_state == NODE_CR_RESERVED)
		return "Exclusive";
	if (node_state == NODE_CR_ONE_ROW)
		return "Alloc";
	snprintf(tmp, sizeof(tmp), "Shared:%u", node_state);
	return tmp;
}
#endif

extern void log_tres_state(struct node_use_record *node_usage,
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
//FIXME: Add GRES/TRES info, lower priority work
	}

	for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
		info("Part:%s Rows:%u", p_ptr->part_ptr->name, p_ptr->num_rows);
		row = p_ptr->row;
//FIXME: What is wrong here? Appears to be fixed
if (!row) {error("ROW IS NULL"); continue; }
		for (i = 0; i < p_ptr->num_rows; i++) {
			core_str = _build_core_str(row[i].row_bitmap);
			info("  Row:%d Jobs:%u Cores:%s",
			     i, row[i].num_jobs, core_str);
			xfree(core_str);
		}
	}	
#endif
}

static int _find_job (void *x, void *key)
{
	struct job_record *job_ptr = (struct job_record *) x;
	if (job_ptr == (struct job_record *) key)
		return 1;
	return 0;
}

/* Return TRUE if identified job is preemptable */
extern bool is_preemptable(struct job_record *job_ptr,
			   List preemptee_candidates)
{
	if (!preemptee_candidates)
		return false;
	if (list_find_first(preemptee_candidates, _find_job, job_ptr))
		return true;
	return false;
}

/*
 * Return true if job is in the processing of cleaning up.
 * This is used for Cray systems to indicate the Node Health Check (NHC)
 * is still running. Until NHC completes, the job's resource use persists
 * the select/cons_tres plugin data structures.
 */
extern bool job_cleaning(struct job_record *job_ptr)
{
	uint16_t cleaning = 0;

	select_g_select_jobinfo_get(job_ptr->select_jobinfo,
				    SELECT_JOBDATA_CLEANING, &cleaning);
	if (cleaning)
		return true;
	return false;
}

/*
 * deallocate resources previously allocated to the given job
 * - subtract 'struct job_resources' resources from 'struct part_res_record'
 * - subtract job's memory requirements from 'struct node_res_record'
 *
 * if action = 0 then subtract cores, memory + GRES (running job was terminated)
 * if action = 1 then subtract memory + GRES (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 *
 * See also: _add_job_to_res() in select_cons_tres.c
 */
extern int rm_job_res(struct part_res_record *part_record_ptr,
		      struct node_use_record *node_usage,
		      struct job_record *job_ptr, int action)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	int i_first, i_last;
	int i, n;
	List gres_list;

//FIXME: Need to add support for additional resources, lower priority work
//FIXME: Sync with recent changes to cons_res plugin, lower priority work
	if (select_state_initializing) {
		/*
		 * Ignore job removal until select/cons_tres data structures
		 * values are set by select_p_reconfigure()
		 */
		info("cons_tres: %s: plugin still initializing", __func__);
		return SLURM_SUCCESS;
	}
	if (!job || !job->core_bitmap) {
		if (job_ptr->details && (job_ptr->details->min_nodes == 0))
			return SLURM_SUCCESS;
		error("cons_tres: %s: job %u has no job_resrcs info",
		      __func__, job_ptr->job_id);
		return SLURM_ERROR;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_tres: %s: job %u action %d", __func__,
		     job_ptr->job_id, action);
		log_job_resources(job_ptr->job_id, job);
		log_tres_state(node_usage, part_record_ptr);
	}
	debug3("cons_tres: %s: job %u action %d", __func__, job_ptr->job_id,
	       action);

	i_first = bit_ffs(job->node_bitmap);
	if (i_first != -1)
		i_last =  bit_fls(job->node_bitmap);
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
						node_ptr->name);
			gres_plugin_node_state_log(gres_list, node_ptr->name);
		}

		if (action != 2) {
			if (node_usage[i].alloc_memory <
			    job->memory_allocated[n]) {
				error("cons_tres: %s: node %s memory is "
				      "under-allocated (%"PRIu64"-%"PRIu64") "
				      "for job %u",
				      __func__, node_ptr->name,
				      node_usage[i].alloc_memory,
				      job->memory_allocated[n],
				      job_ptr->job_id);
				node_usage[i].alloc_memory = 0;
			} else
				node_usage[i].alloc_memory -=
					job->memory_allocated[n];
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
			error("cons_tres: %s: removed job %u does not have a "
			      "partition assigned",
			      __func__, job_ptr->job_id);
			return SLURM_ERROR;
		}

		for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr == job_ptr->part_ptr)
				break;
		}
		if (!p_ptr) {
			error("cons_tres: %s: removed job %u could not find part %s",
			      __func__, job_ptr->job_id, job_ptr->part_ptr->name);
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
				debug3("cons_tres: %s: removed job %u from "
				       "part %s row %u",
				       __func__, job_ptr->job_id,
				       p_ptr->part_ptr->name, i);
				for (; j < p_ptr->row[i].num_jobs-1; j++) {
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
					error("cons_tres:%s: node_state mis-count "
					      "(job:%u job_cnt:%u node:%s node_cnt:%u)",
					      __func__, job_ptr->job_id,
					      job->node_req, node_ptr->name,
					      node_usage[i].node_state);
					node_usage[i].node_state =
						NODE_CR_AVAILABLE;
				}
			}
		}
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_tres: %s: job %u finished", __func__,
		     job_ptr->job_id);
		log_tres_state(node_usage, part_record_ptr);
	}

	return SLURM_SUCCESS;
}

/*
 * _build_row_bitmaps: A job has been removed from the given partition,
 *                     so the row_bitmap(s) need to be reconstructed.
 *                     Optimize the jobs into the least number of rows,
 *                     and make the lower rows as dense as possible.
 *
 * IN/OUT: p_ptr   - the partition that has jobs to be optimized
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
				_rm_job_res(job_ptr->job_resrcs,
					    &this_row->row_bitmap);
			} else { /* totally rebuild the bitmap */
				clear_core_array(this_row->row_bitmap);
				for (j = 0; j < this_row->num_jobs; j++) {
					add_job_res(this_row->job_list[j],
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
		clear_core_array(p_ptr->row[i].row_bitmap);
		return;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("DEBUG: %s (before):", __func__);
		dump_parts(p_ptr);
	}
	debug3("cons_tres: %s reshuffling %u jobs", __func__, num_jobs);

	/* make a copy, in case we cannot do better than this */
	orig_row = _dup_row_data(p_ptr->row, p_ptr->num_rows);
	if (orig_row == NULL)
		return;

	/* create a master job list and clear out ALL row data */
	ss = xmalloc(num_jobs * sizeof(struct sort_support));
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
	 *     - fixme: JOB SHUFFLING BETWEEN ROWS NEEDS TESTING
	 */
	qsort(ss, num_jobs, sizeof(struct sort_support), _compare_support);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		for (i = 0; i < num_jobs; i++) {
			char cstr[64], nstr[64];
			if (ss[i].tmpjobs->core_bitmap) {
				bit_fmt(cstr, (sizeof(cstr)-1) ,
					ss[i].tmpjobs->core_bitmap);
			} else
				sprintf(cstr, "[no core_bitmap]");
			if (ss[i].tmpjobs->node_bitmap) {
				bit_fmt(nstr, (sizeof(nstr)-1),
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
			if (can_job_fit_in_row(ss[j].tmpjobs,
					       &(p_ptr->row[i]))) {
				/* job fits in row, so add it */
				add_job_to_row(ss[j].tmpjobs, &(p_ptr->row[i]));
				ss[j].tmpjobs = NULL;
				break;
			}
		}
		/* job should have been added, so shuffle the rows */
		cr_sort_part_rows(p_ptr);
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
		debug3("cons_tres: %s: dangling job found", __func__);

		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("DEBUG: %s (post-algorithm):", __func__);
			dump_parts(p_ptr);
		}

		cr_destroy_row_data(p_ptr->row, p_ptr->num_rows);
		p_ptr->row = orig_row;
		orig_row = NULL;

		/* still need to rebuild row_bitmaps */
		for (i = 0; i < p_ptr->num_rows; i++) {
			clear_core_array(p_ptr->row[i].row_bitmap);
			if (p_ptr->row[i].num_jobs == 0)
				continue;
			for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
				add_job_res(p_ptr->row[i].job_list[j],
					     &p_ptr->row[i].row_bitmap);
			}
		}
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("DEBUG: %s (after):", __func__);
		dump_parts(p_ptr);
	}

	if (orig_row)
		cr_destroy_row_data(orig_row, p_ptr->num_rows);
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

/* test for conflicting core bitmap elements */
extern int can_job_fit_in_row(struct job_resources *job,
			      struct part_row_data *r_ptr)
{
	if ((r_ptr->num_jobs == 0) || !r_ptr->row_bitmap)
		return 1;

	return job_fit_test(job, r_ptr->row_bitmap);
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
 * Return the number of usable logical processors by a given job on
 * some specified node. Returns 0xffff if no limit.
 */
extern int vpus_per_core(struct job_details *details, int node_inx)
{
	uint16_t pu_per_core = 0xffff;	/* Usable CPUs per core */
	uint16_t vpus_per_core = select_node_record[node_inx].vpus;

	if (details && details->mc_ptr) {
		multi_core_data_t *mc_ptr = details->mc_ptr;
		if ((mc_ptr->ntasks_per_core != INFINITE16) &&
		    (mc_ptr->ntasks_per_core)) {
			pu_per_core = MIN(vpus_per_core,
					  (mc_ptr->ntasks_per_core *
					   details->cpus_per_task));
		}
		if ((mc_ptr->threads_per_core != NO_VAL16) &&
		    (mc_ptr->threads_per_core <  pu_per_core)) {
			pu_per_core = mc_ptr->threads_per_core;
		}
	}

	vpus_per_core = MIN(vpus_per_core, pu_per_core);
	return vpus_per_core;
}

/* Create a duplicate part_res_record list */
static struct node_use_record *_dup_node_usage(struct node_use_record *orig_ptr)
{
	struct node_use_record *new_use_ptr, *new_ptr;
	List gres_list;
	uint32_t i;

	if (orig_ptr == NULL)
		return NULL;

	new_use_ptr = xmalloc(select_node_cnt * sizeof(struct node_use_record));
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
		new_ptr->row = _dup_row_data(orig_ptr->row,
					     orig_ptr->num_rows);
		if (orig_ptr->next) {
			new_ptr->next = xmalloc(sizeof(struct part_res_record));
			new_ptr = new_ptr->next;
		}
		orig_ptr = orig_ptr->next;
	}
	return new_part_ptr;
}

/* Helper function for _dup_part_data: create a duplicate part_row_data array */
static struct part_row_data *_dup_row_data(struct part_row_data *orig_row,
					   uint16_t num_rows)
{
	struct part_row_data *new_row;
	int i, n;

	if (num_rows == 0 || !orig_row)
		return NULL;

	new_row = xmalloc(num_rows * sizeof(struct part_row_data));
	for (i = 0; i < num_rows; i++) {
		new_row[i].num_jobs = orig_row[i].num_jobs;
		new_row[i].job_list_size = orig_row[i].job_list_size;
		if (orig_row[i].row_bitmap) {
			new_row[i].row_bitmap = xmalloc(sizeof(bitstr_t *) *
							select_node_cnt);
			for (n = 0; n < select_node_cnt; n++) {
				if (!orig_row[i].row_bitmap[n])
					continue;
				new_row[i].row_bitmap[n] =
					bit_copy(orig_row[i].row_bitmap[n]);
			}
		}
		if (new_row[i].job_list_size == 0)
			continue;
		/* copy the job list */
		new_row[i].job_list = xmalloc(new_row[i].job_list_size *
					      sizeof(struct job_resources *));
		memcpy(new_row[i].job_list, orig_row[i].job_list,
		       (sizeof(struct job_resources *) * new_row[i].num_jobs));
	}
	return new_row;
}

/*
 * Test if job can fit into the given set of core_bitmaps
 * IN job_resrcs_ptr - resources allocated to a job
 * IN sys_resrcs_ptr - bitmap array (one per node) of available cores
 * RET 1 on success, 0 otherwise
 * NOTE: Patterned after job_fits_into_cores() in src/common/job_resources.c
 */
extern int job_fit_test(job_resources_t *job_resrcs_ptr,
			bitstr_t **sys_resrcs_ptr)
{
	int i, i_first, i_last;
	int c, c_job, c_off = 0, c_max;
	int rep_inx = 0, rep_offset = -1;

	if (!sys_resrcs_ptr)
		return 1;			/* Success */

	i_first = bit_ffs(job_resrcs_ptr->node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(job_resrcs_ptr->node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_resrcs_ptr->node_bitmap, i))
			continue;
		if (job_resrcs_ptr->whole_node) {
			if (!sys_resrcs_ptr[i] ||
			    bit_ffs(sys_resrcs_ptr[i]) == -1)
				return 1;	/* Success */
			return 0;		/* Whole node conflict */
		}
		rep_offset++;
		if (rep_offset > job_resrcs_ptr->sock_core_rep_count[rep_inx]) {
			rep_offset = 0;
			rep_inx++;
		}
		c_job = job_resrcs_ptr->sockets_per_node[rep_inx] *
			job_resrcs_ptr->cores_per_socket[rep_inx];
		c_max = MIN(select_node_record[i].tot_cores, c_job);
		for (c = 0; c < c_max; c++) {
			if (!bit_test(job_resrcs_ptr->core_bitmap, c_off + c))
				continue;
			if (bit_test(sys_resrcs_ptr[i], c))
				return 0;	/* Core conflict on this node */
		}
		c_off += c_job;
	}
	return 1;
}

/*
 * Remove job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT full_bitmap - bitmap of available CPUs, allocate as needed
 * RET 1 on success, 0 otherwise
 */
static void _rm_job_res(job_resources_t *job_resrcs_ptr,
			bitstr_t ***sys_resrcs_ptr)
{
	int i, i_first, i_last;
	int c, c_job, c_off = 0, c_max;
	int rep_inx = 0, rep_offset = -1;
	bitstr_t **core_array;

	if (!job_resrcs_ptr->core_bitmap)
		return;

	/* remove the job from the row_bitmap */
	if (*sys_resrcs_ptr == NULL) {
		core_array = xmalloc(sizeof(bitstr_t *) * select_node_cnt);
		*sys_resrcs_ptr = core_array;
		for (i = 0; i < select_node_cnt; i++) {
			core_array[i] =
				bit_alloc(select_node_record[i].tot_cores);
		}
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
		if (job_resrcs_ptr->whole_node) {
			if (core_array[i]) {
				bit_clear_all(core_array[i]);
			} else {
				error("cons_tres: %s: core_array[%d] is NULL",
				      __func__, i);
			}
			continue;
		}
		rep_offset++;
		if (rep_offset > job_resrcs_ptr->sock_core_rep_count[rep_inx]) {
			rep_offset = 0;
			rep_inx++;
		}
		c_job = job_resrcs_ptr->sockets_per_node[rep_inx] *
			job_resrcs_ptr->cores_per_socket[rep_inx];
		c_max = MIN(select_node_record[i].tot_cores, c_job);
		for (c = 0; c < c_max; c++) {
			if (!bit_test(job_resrcs_ptr->core_bitmap, c_off + c))
				continue;
			if (core_array[i]) {
				bit_clear(core_array[i], c);
			} else {
				error("cons_tres: %s: core_array[%d] is NULL",
				      __func__, i);
			}
		}
		c_off += c_job;
	}
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

/*
 * Bit a core bitmap array of available cores
 * node_bitmap IN - Nodes available for use
 * core_spec IN - Specialized core specification, NO_VAL16 if none
 * RET core bitmap array, one per node. Use free_core_array() to release memory
 */
extern bitstr_t **mark_avail_cores(bitstr_t *node_bitmap, uint16_t core_spec)
{
	bitstr_t **avail_cores;
	int i, i_first, i_last;
	int c, s;
	int core_inx, rem_core_spec, sock_per_node;

	if ((core_spec != NO_VAL16) &&
	    (core_spec & CORE_SPEC_THREAD))	/* Reserving threads */
		core_spec = NO_VAL16;		/* Don't remove cores */

	avail_cores = build_core_array();
	i_first = bit_ffs(node_bitmap);
	if (i_first != -1)
		i_last = bit_fls(node_bitmap);
	else
		i_last = -2;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		avail_cores[i] = bit_alloc(select_node_record[i].tot_cores);
		bit_set_all(avail_cores[i]);

		if (core_spec != NO_VAL16) {
			/*
			 * Clear core bitmap for specified core count
			 * Start with highest socket and core, then
			 * work down to lower sockets
			 */
			rem_core_spec = core_spec;
			sock_per_node = select_node_record[i].boards *
					select_node_record[i].sockets;
			for (s = sock_per_node - 1;
			     (s >= 0) && (rem_core_spec > 0); s--) {
				for (c = select_node_record[i].cores - 1;
				     (c >= 0) && (rem_core_spec > 0); c--) {
					core_inx = c + s *
						   select_node_record[i].cores;
					if (!bit_test(avail_cores[i],
						      core_inx))
						continue;
					bit_clear(avail_cores[i], core_inx);
					rem_core_spec--;
				}
			}
		}
	}

	return avail_cores;
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
	uint32_t c, j, n, csize, total_cpus;
	uint64_t save_mem = 0;
	int32_t build_cnt;
	job_resources_t *job_res;
	struct job_details *details_ptr;
	struct part_res_record *p_ptr, *jp_ptr;
	uint16_t *cpu_count, *cpu_count_tmp;
	int i, i_first, i_last;

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
						exc_cores, qos_preemptor);
		if (error_code != SLURM_SUCCESS) {
			return error_code;
		}
	}

	/* This is the case if -O/--overcommit  is true */
	if (details_ptr->min_cpus == details_ptr->min_nodes) {
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
		info("cons_tres: %s: evaluating job %u on %u nodes",
		     __func__, job_ptr->job_id, bit_set_count(node_bitmap));
	}

	if ((details_ptr->pn_min_memory == 0) &&
	    (select_fast_schedule == 0))
		job_ptr->bit_flags |= NODE_MEM_CALC;	/* To be calculated */

	orig_node_map = bit_copy(node_bitmap);
	avail_cores = mark_avail_cores(node_bitmap,
				       job_ptr->details->core_spec);

	/*
	 * test to make sure that this job can succeed with all avail_cores
	 * if 'no' then return FAIL
	 * if 'yes' then we will seek the optimal placement for this job
	 *          within avail_cores
	 */
	free_cores = copy_core_array(avail_cores);
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);
	if (cpu_count == NULL) {
		/* job can not fit */
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 0 fail: insufficient resources",
			     __func__);
		}
		return SLURM_ERROR;
	} else if (test_only) {
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		xfree(cpu_count);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("cons_tres: %s: test 0 pass: test_only", __func__);
		return SLURM_SUCCESS;
	} else if (!job_ptr->best_switch) {
		FREE_NULL_BITMAP(orig_node_map);
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		xfree(cpu_count);
		if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
			info("cons_tres: %s: test 0 fail: waiting for switches",
			     __func__);
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
		info("cons_tres: %s: test 0 pass - job fits on given resources",
		     __func__);
	}
	xfree(cpu_count);

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
	if (exc_cores) {
#if _DEBUG
		_log_select_maps("exclude reserved cores", NULL, exc_cores);
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
			core_array_and_not(free_cores,p_ptr->row[i].row_bitmap);
			if (p_ptr->part_ptr != job_ptr->part_ptr)
				continue;
			if (part_core_map) {
				core_array_or(part_core_map,
					      p_ptr->row[i].row_bitmap);
			} else {
				part_core_map =
				      copy_core_array(p_ptr->row[i].row_bitmap);
			}
		}
	}
	if (job_ptr->details->whole_node == 1)
		_block_whole_nodes(node_bitmap, avail_cores, free_cores);

	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);

	if ((cpu_count) && (job_ptr->best_switch)) {
		/* job fits! We're done. */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 1 pass - idle resources found",
			     __func__);
		}
		goto alloc_job;
	}
	xfree(cpu_count);

	if ((gang_mode == 0) && (job_node_req == NODE_CR_ONE_ROW)) {
		/*
		 * This job CANNOT share CPUs regardless of priority,
		 * so we fail here. Note that Shared=EXCLUSIVE was already
		 * addressed in _verify_node_state() and job preemption
		 * removes jobs from simulated resource allocation map
		 * before this point.
		 */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 1 fail - no idle resources available",
			     __func__);
		}
		goto alloc_job;
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_tres: %s: test 1 fail - not enough idle resources",
		     __func__);
	}

	/*** Step 2 ***/
	for (jp_ptr = cr_part_ptr; jp_ptr; jp_ptr = jp_ptr->next) {
		if (jp_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!jp_ptr) {
		error("cons_tres %s: could not find partition for job %u",
		      __func__, job_ptr->job_id);
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
			info("cons_tres: %s: looking for higher-priority or "
			     "PREEMPT_MODE_OFF part's to remove from avail_cores",
			     __func__);
		}

		for (p_ptr = cr_part_ptr; p_ptr; p_ptr = p_ptr->next) {
			if ((p_ptr->part_ptr->priority_tier <=
			     jp_ptr->part_ptr->priority_tier) &&
			    (p_ptr->part_ptr->preempt_mode !=
			     PREEMPT_MODE_OFF)) {
				if (select_debug_flags &
				    DEBUG_FLAG_SELECT_TYPE) {
					info("cons_tres: %s: continuing on part: %s",
					     __func__, p_ptr->part_ptr->name);
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

	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);
	if (!cpu_count) {
		/*
		 * job needs resources that are currently in use by
		 * higher-priority jobs, so fail for now
		 */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 2 fail - resources busy with higher priority jobs",
			     __func__);
		}
		goto alloc_job;
	}
	xfree(cpu_count);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_tres: %s: test 2 pass - available resources for this priority",
		     __func__);
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
	cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes, req_nodes,
				  node_bitmap, free_cores,
				  node_usage, cr_type, test_only,
				  part_core_map, prefer_alloc_nodes);
	if (cpu_count) {
		/*
		 * To the extent possible, remove from consideration resources
		 * which are allocated to jobs in lower priority partitions.
		 */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 3 pass - found resources",
			     __func__);
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
			cpu_count_tmp = _select_nodes(job_ptr, min_nodes,
						max_nodes, req_nodes,
						node_bitmap_tmp,
						free_cores_tmp, node_usage,
						cr_type, test_only,
						part_core_map,
						prefer_alloc_nodes);
			if (!cpu_count_tmp) {
				free_core_array(&free_cores_tmp2);
				FREE_NULL_BITMAP(node_bitmap_tmp2);
				break;
			}
			if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
				info("cons_tres: %s: remove low-priority partition %s",
				     __func__, p_ptr->part_ptr->name);
			}
			free_core_array(&free_cores);
			free_cores      = free_cores_tmp;
			free_cores_tmp  = free_cores_tmp2;
			free_cores_tmp2 = NULL;
			bit_copybits(node_bitmap, node_bitmap_tmp);
			FREE_NULL_BITMAP(node_bitmap_tmp);
			node_bitmap_tmp  = node_bitmap_tmp2;
			node_bitmap_tmp2 = NULL;
			xfree(cpu_count);
			cpu_count = cpu_count_tmp;
		}
		goto alloc_job;
	}
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_tres: %s: test 3 fail - not enough idle resources in same priority",
		     __func__);
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
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					  req_nodes, node_bitmap,
					  free_cores, node_usage, cr_type,
					  test_only, part_core_map,
					  prefer_alloc_nodes);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 4 pass - first row found",
			     __func__);
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
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		core_array_and_not(free_cores, jp_ptr->row[i].row_bitmap);
		bit_copybits(node_bitmap, orig_node_map);
		if (job_ptr->details->whole_node == 1)
			_block_whole_nodes(node_bitmap, avail_cores,free_cores);
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					  req_nodes, node_bitmap,
					  free_cores, node_usage, cr_type,
					  test_only, part_core_map,
					  prefer_alloc_nodes);
		if (cpu_count) {
			if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
				info("cons_tres: %s: test 4 pass - row %i",
				     __func__, i);
			}
			break;
		}
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 4 fail - row %i",
			     __func__, i);
		}
	}

	if ((i < c) && !jp_ptr->row[i].row_bitmap) {
		/* we've found an empty row, so use it */
		free_core_array(&free_cores);
		free_cores = copy_core_array(avail_cores);
		bit_copybits(node_bitmap, orig_node_map);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 4 trying empty row %i",
			     __func__, i);
		}
		cpu_count = _select_nodes(job_ptr, min_nodes, max_nodes,
					  req_nodes, node_bitmap,
					  free_cores, node_usage, cr_type,
					  test_only, part_core_map,
					  prefer_alloc_nodes);
	}

	if (!cpu_count) {
		/* job can't fit into any row, so exit */
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: test 4 fail - busy partition",
			     __func__);
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
	 * - cpu_count is the number of CPUs per allocated node
	 *
	 * Next steps are to cleanup the worker variables,
	 * create the job_resources struct,
	 * distribute the job on the bits, and exit
	 */
	FREE_NULL_BITMAP(orig_node_map);
	free_core_array(&part_core_map);
	free_core_array(&free_cores_tmp);
	FREE_NULL_BITMAP(node_bitmap_tmp);
	if (!cpu_count || !job_ptr->best_switch) {
		/* we were sent here to cleanup and exit */
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("cons_tres: %s: exiting with no allocation",
			     __func__);
		}
		return SLURM_ERROR;
	}

	/*
	 * At this point we have:
	 * - a node_bitmap of selected nodes
	 * - a free_cores bitmap of usable cores on each selected node
	 * - a per-alloc-node cpu_count array
	 */
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
	if ((error_code != SLURM_SUCCESS) || (mode != SELECT_MODE_RUN_NOW)) {
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		xfree(cpu_count);
		return error_code;
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_tres: %s: distributing job %u", __func__,
		     job_ptr->job_id);
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
		free_core_array(&avail_cores);
		free_core_array(&free_cores);
		return error_code;
	}

	/* total up all CPUs and load the core_bitmap */
	total_cpus = 0;
	c = 0;
	csize = bit_size(job_res->core_bitmap);
	i_first = bit_ffs(node_bitmap);
	for (i = 0, n = i_first; n < select_node_cnt; n++) {
		if (!bit_test(node_bitmap, n))
			continue;
		for (j = 0; j < select_node_record[n].tot_cores; j++, c++) {
			if (!bit_test(free_cores[n], j))
				continue;
			if (c >= csize)	{
				error("cons_tres: %s core_bitmap index error on node %s",
				      __func__,
				      select_node_record[n].node_ptr->name);
				drain_nodes(select_node_record[n].node_ptr->name,
					    "Bad core count", getuid());
				free_job_resources(&job_res);
				free_core_array(&free_cores);
				return SLURM_ERROR;
			}
			bit_set(job_res->core_bitmap, c);
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
		info("cons_tres: %s: job %u ncpus %u cbits %u/%u nbits %u",
		     __func__, job_ptr->job_id,
		     job_res->ncpus, count_core_array_set(free_cores),
		     bit_set_count(job_res->core_bitmap),
		     job_res->nhosts);
	}
	free_core_array(&free_cores);

	/* distribute the tasks and clear any unused cores */
	job_ptr->job_resrcs = job_res;
	error_code = cr_dist(job_ptr, cr_type, preempt_mode, avail_cores);
	free_core_array(&avail_cores);
	if (error_code != SLURM_SUCCESS) {
		free_job_resources(&job_ptr->job_resrcs);
		return error_code;
	}

	/* translate job_res->cpus array into format with rep count */
	build_cnt = build_job_resources_cpu_array(job_res);
	if (job_ptr->details->whole_node == 1) {
		i_first = bit_ffs(job_res->node_bitmap);
		if (i_first != -1)
			i_last  = bit_fls(job_res->node_bitmap);
		else
			i_last = -2;
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
		i_first = bit_ffs(job_res->node_bitmap);
		if (i_first != -1)
			i_last  = bit_fls(job_res->node_bitmap);
		else
			i_last = -2;
		job_ptr->total_cpus = 0;
		for (i = i_first; i <= i_last; i++) {
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
		i_first = bit_ffs(job_res->node_bitmap);
		if (i_first != -1)
			i_last  = bit_fls(job_res->node_bitmap);
		else
			i_last = -2;
		for (i = i_first, j = 0; i <= i_last; i++) {
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

/*
 * Deallocate resources previously allocated to the given job
 * - subtract 'struct job_resources' resources from 'struct part_res_record'
 * - subtract job's memory requirements from 'struct node_res_record'
 *
 * if action = 0 then subtract cores, memory + TRES (running job was terminated)
 * if action = 1 then subtract memory + TRES (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 */
static int _rm_job_from_res(struct part_res_record *part_record_ptr,
			    struct node_use_record *node_usage,
			    struct job_record *job_ptr, int action)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	int first_bit, last_bit;
	int i, n;
	List gres_list;

	if (select_state_initializing) {
		/*
		 * Ignore job removal until select/cons_tres data structures
		 * values are set by select_p_reconfigure()
		 */
		return SLURM_SUCCESS;
	}
	if (!job || !job->core_bitmap) {
		if (job_ptr->details && (job_ptr->details->min_nodes == 0))
			return SLURM_SUCCESS;
		error("%s: job %u has no job_resrcs info",
		      __func__, job_ptr->job_id);
		return SLURM_ERROR;
	}

	debug3("cons_tres: %s: job %u action %d", __func__, job_ptr->job_id,
	       action);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		log_job_resources(job_ptr->job_id, job);

	first_bit = bit_ffs(job->node_bitmap);
	if (first_bit == -1)
		last_bit = -2;
	else
		last_bit =  bit_fls(job->node_bitmap);
	for (i = first_bit, n = -1; i <= last_bit; i++) {
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
						node_ptr->name);
			gres_plugin_node_state_log(gres_list, node_ptr->name);
		}

		if (action != 2) {
			if (node_usage[i].alloc_memory <
			    job->memory_allocated[n]) {
				error("cons_tres: node %s memory is under-allocated "
				      "(%"PRIu64"-%"PRIu64") for job %u",
				      node_ptr->name,
				      node_usage[i].alloc_memory,
				      job->memory_allocated[n],
				      job_ptr->job_id);
				node_usage[i].alloc_memory = 0;
			} else
				node_usage[i].alloc_memory -=
					job->memory_allocated[n];
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
			error("cons_tres: removed job %u does not have a "
			      "partition assigned",
			      job_ptr->job_id);
			return SLURM_ERROR;
		}

		for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr == job_ptr->part_ptr)
				break;
		}
		if (!p_ptr) {
			error("cons_tres: removed job %u could not find part %s",
			      job_ptr->job_id, job_ptr->part_ptr->name);
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
				debug3("cons_tres: removed job %u from "
				       "part %s row %u",
				       job_ptr->job_id,
				       p_ptr->part_ptr->name, i);
				for (; j < p_ptr->row[i].num_jobs-1; j++) {
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
			for (i = first_bit, n = -1; i <= last_bit; i++) {
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
					error("cons_tres: %s: node_state mis-count "
					      "(job:%u job_cnt:%u node:%s node_cnt:%u)",
					      __func__, job_ptr->job_id,
					      job->node_req, node_ptr->name,
					      node_usage[i].node_state);
					node_usage[i].node_state =
						NODE_CR_AVAILABLE;
				}
			}
		}
	}

	return SLURM_SUCCESS;
}

/* Enable detailed logging of cr_dist() node and per-node core bitmaps */
static inline void _log_select_maps(char *loc, bitstr_t *node_map,
				    bitstr_t **core_map)
{
#if _DEBUG
	char tmp[100];
	int i;

	if (node_map) {
		bit_fmt(tmp, sizeof(tmp), node_map);
		info("%s nodemap:%s", loc, tmp);
	}
	if (core_map) {
		for (i = 0; i < select_node_cnt; i++) {
			if (!core_map[i] || (bit_ffs(core_map[i]) == -1))
				continue;
			bit_fmt(tmp, sizeof(tmp), core_map[i]);
			info("%s coremap[%d]:%s", loc, i, tmp);
		}
	}
#endif
}

/*
 * Determine how many CPUs on the node can be used
 */
static void _cpus_to_use(int *avail_cpus, int rem_cpus, int rem_nodes,
			 struct job_details *details_ptr, uint16_t *cpu_cnt,
			 int node_inx, uint16_t cr_type)
{
	int resv_cpus;	/* CPUs to be allocated on other nodes */

	if (details_ptr->whole_node == 1)	/* Use all resources on node */
		return;

	resv_cpus = MAX((rem_nodes - 1), 0);
	resv_cpus *= vpus_per_core(details_ptr, node_inx);
	if (cr_type & CR_SOCKET)
		resv_cpus *= select_node_record[node_inx].cores;
	rem_cpus -= resv_cpus;

	if (*avail_cpus > rem_cpus) {
		*avail_cpus = MAX(rem_cpus, (int)details_ptr->pn_min_cpus);
		/* Round up CPU count to CPU in allocation unit (e.g. core) */
		*cpu_cnt = *avail_cpus;
	}
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
			uint32_t req_nodes, uint16_t *cpu_cnt, uint16_t cr_type,
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
	if (select_node_cnt != node_record_count) {
		error("cons_tres: node count inconsistent with slurmctld");
		return error_code;
	}
	if (bit_set_count(node_map) < min_nodes)
		return error_code;

	if ((details_ptr->req_node_bitmap) &&
	    (!bit_super_set(details_ptr->req_node_bitmap, node_map)))
		return error_code;
#if 0
//FIXME: Can we better handle nodes of different weights?, lower priority work
//FIXME: Weights job-specific due to FLEX reservations and node_features reboot needs, lower priority work
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
#endif
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

	for (i = 0; i < select_node_cnt; i++) {
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
			/* already acquired required nodes re-use record */
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
			info("cons_tres: eval_nodes:%d consec "
			     "CPUs:%d nodes:%d begin:%d end:%d required:%d",
			     i, consec_cpus[i], consec_nodes[i],
			     consec_start[i], consec_end[i], consec_req[i]);
		}
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("Job %u can't use required nodes due to max CPU limit",
		     job_ptr->job_id);
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

			/*
			 * if first possibility OR
			 * contains required nodes OR
			 * first set large enough for request OR
			 * tightest fit (less resource waste) OR
			 * nothing yet large enough, but this is biggest
			 */
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

		if (details_ptr->contiguous &&
		    ((best_fit_cpus < rem_cpus) ||
		     (!_enough_nodes(best_fit_nodes, rem_nodes,
				     min_nodes, req_nodes))))
			break;	/* no hole large enough */
		if (best_fit_req != -1) {
			/*
			 * This collection of nodes includes required ones
			 * select nodes from this set, first working up
			 * then down from the required nodes
			 */
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

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
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

				/*
				 * This could result in 0, but if the user
				 * requested nodes here we will still give
				 * them and then the step layout will sort
				 * things out.
				 */
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
				/*
				 * If we found a single node to use,
				 * clear cpu counts for all other nodes
				 */
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
 * this is an intermediary step between _select_nodes and _eval_nodes
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low cpu counts for the job and re-evaluates each result
 */
static int _choose_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			 uint32_t min_nodes, uint32_t max_nodes,
			 uint32_t req_nodes, uint16_t *cpu_cnt,
			 uint16_t cr_type, bool prefer_alloc_nodes)
{
	int i, count, ec, most_cpus = 0;
	bitstr_t *origmap, *reqmap = NULL;

	if (job_ptr->details->req_node_bitmap)
		reqmap = job_ptr->details->req_node_bitmap;

	/* clear nodes from the bitmap that don't have available resources */
	for (i = 0; i < select_node_cnt; i++) {
		if (!bit_test(node_map, i))
			continue;
		/*
		 * Make sure we don't say we can use a node exclusively
		 * that is bigger than our max cpu count.
		 */
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
			 cpu_cnt, cr_type, prefer_alloc_nodes);

	if (ec == SLURM_SUCCESS) {
		FREE_NULL_BITMAP(origmap);
		return ec;
	}

	/*
	 * This nodeset didn't work. To avoid a possible knapsack problem,
	 * incrementally remove nodes with low CPU counts and retry
	 */
	for (i = 0; i < select_node_cnt; i++) {
		most_cpus = MAX(most_cpus, cpu_cnt[i]);
	}

	for (count = 1; count < most_cpus; count++) {
		int nochange = 1;
		bit_or(node_map, origmap);
		for (i = 0; i < select_node_cnt; i++) {
			if ((cpu_cnt[i] > 0) && (cpu_cnt[i] <= count)) {
				if (!bit_test(node_map, i))
					continue;
				if (reqmap && bit_test(reqmap, i))
					continue;
				nochange = 0;
				bit_clear(node_map, i);
				bit_clear(origmap, i);
			}
		}
		if (nochange)
			continue;
		ec = _eval_nodes(job_ptr, node_map, min_nodes, max_nodes,
				 req_nodes,  cpu_cnt, cr_type,
				 prefer_alloc_nodes);
		if (ec == SLURM_SUCCESS) {
			FREE_NULL_BITMAP(origmap);
			return ec;
		}
	}
	FREE_NULL_BITMAP(origmap);
	return ec;
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

	/*
	 * This logic could be expanded to support additional cases, which may
	 * require information per node information (e.g. threads per core).
	 */

	return s_p_n;
}

/*
 * Determine how many CPUs on the node can be used by this job
 * IN job_gres_list  - job's gres_list built by gres_plugin_job_state_validate()
 * IN node_gres_list - node's gres_list built by gres_plugin_node_config_validate()
 * IN use_total_gres - if set then consider all gres resources as available,
 *		       and none are commited to running jobs
 * IN cpu_bitmap     - Identification of available CPUs (NULL if no restriction)
 * IN job_id         - job's ID (for logging)
 * IN node_name      - name of the node (for logging)
 * IN node_i         - Node index
 * IN s_p_n          - Sockets per node required by this job or NO_VAL
 * RET: NO_VAL       - All cores on node are available
 *      otherwise    - Count of available cores
 */
static uint32_t _gres_sock_job_test(List job_gres_list, List node_gres_list,
				    bool use_total_gres, bitstr_t *core_bitmap,
				    uint32_t job_id, char *node_name,
				    uint32_t node_i, uint32_t s_p_n)
{
//FIXME: Add code here
return NO_VAL;
}

/*
 * _allocate_sc - Given the job requirements, determine which cores/sockets
 *                from the given node can be allocated (if any) to this
 *                job. Returns the number of cpus that can be used by
 *                this node AND a bitmap of the selected cores.
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - core_bitmap of available cores on this node
 * IN part_core_map - bitmap of cores already allocated on this partition/node
 * IN node_i        - index of node to be evaluated
 * IN/OUT cpu_alloc_size - minimum allocation size, in CPUs
 * IN entire_sockets_only - if true, allocate cores only on sockets that
 *                        - have no other allocated cores.
 */
static uint16_t _allocate_sc(struct job_record *job_ptr, bitstr_t *core_map,
			     bitstr_t *part_core_map, const uint32_t node_i,
			     int *cpu_alloc_size, bool entire_sockets_only)
{
	uint16_t cpu_count = 0, cpu_cnt = 0;
	uint16_t si, cps, avail_cpus = 0, num_tasks = 0;
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

	/*
	 * These are the job parameters that we must respect:
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

	/*
	 * Step 1: create and compute core-count-per-socket
	 * arrays and total core counts
	 */
	for (c = 0; c < select_node_record[node_i].tot_cores; c++) {
		i = (uint16_t) (c / cores_per_socket);
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

	/*
	 * Ignore resources that would push a job allocation over the
	 * partition CPU limit (if any)
	 */
	if ((job_ptr->part_ptr->max_cpus_per_node != INFINITE) &&
	    (free_cpu_count + used_cpu_count >
	     job_ptr->part_ptr->max_cpus_per_node)) {
		int excess = free_cpu_count + used_cpu_count -
			     job_ptr->part_ptr->max_cpus_per_node;
		for (c = 0; c < select_node_record[node_i].tot_cores; c++) {
			i = (uint16_t) (c / cores_per_socket);
			if (free_cores[i] > 0) {
				free_core_count--;
				free_cores[i]--;
				excess -= threads_per_core;
				if (excess <= 0)
					break;
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
	threads_per_core = vpus_per_core(job_ptr->details, node_i);

	for (i = 0; i < sockets; i++) {
		uint16_t tmp = free_cores[i] * threads_per_core;
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
		if (job_ptr->details->ntasks_per_node)
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

	/*
	 * Step 4 - make sure that ntasks_per_socket is enforced when
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
	for (c = 0;
	     c < select_node_record[node_i].tot_cores && (avail_cpus > 0);
	     c++) {
		if (bit_test(core_map, c) == 0)
			continue;
		i = (uint16_t) (c / cores_per_socket);
		if (free_cores[i] > 0) {
			/*
			 * this socket has free cores, but make sure we don't
			 * use more than are needed for ntasks_per_socket
			 */
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
			/*
			 * we have to ensure that cpu_count is not bigger than
			 * avail_cpus due tp hyperthreading or this would break
			 * the selection logic providing more cpus than allowed
			 * after task-related data processing of stage 3
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
	if (c < select_node_record[node_i].tot_cores)
		bit_nclear(core_map, c, select_node_record[node_i].tot_cores-1);

fini:
	/* if num_tasks == 0 then clear all bits on this node */
	if (!num_tasks) {
		bit_clear_all(core_map);
		cpu_count = 0;
	}

	if ((job_ptr->details->core_spec != NO_VAL16) &&
	    (job_ptr->details->core_spec & CORE_SPEC_THREAD)   &&
	    ((select_node_record[node_i].threads == 1) ||
	     (select_node_record[node_i].threads ==
	      select_node_record[node_i].vpus))) {
		/*
		 * NOTE: Currently does not support the situation when Slurm
		 * allocates by core the thread specialization count occupies
		 * a full core
		 */
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
 * _allocate_cores - Given the job requirements, determine which cores
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
 */
uint16_t _allocate_cores(struct job_record *job_ptr, bitstr_t *core_map,
			 bitstr_t *part_core_map, const uint32_t node_i,
			 int *cpu_alloc_size, bool cpu_type)
{
	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, false);
}

/*
 * _allocate_sockets - Given the job requirements, determine which sockets
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
 */
uint16_t _allocate_sockets(struct job_record *job_ptr, bitstr_t *core_map,
			   bitstr_t *part_core_map, const uint32_t node_i,
			   int *cpu_alloc_size)
{
	return _allocate_sc(job_ptr, core_map, part_core_map, node_i,
			    cpu_alloc_size, true);
}

/*
 * _can_job_run_on_node - Given the job requirements, determine which
 *                        resources from the given node (if any) can be
 *                        allocated to this job. Returns the number of
 *                        CPUs that can be used by this node and a bitmap
 *                        of available resources for allocation.
 *       NOTE: This process does NOT support overcommitting resources
 *
 * IN job_ptr       - pointer to job requirements
 * IN/OUT core_map  - per-node bitmap of available cores
 * IN node_i        - index of node to be evaluated
 * IN s_p_n         - Expected sockets_per_node (NO_VAL if not known)
 * IN cr_type       - Consumable Resource setting
 * IN test_only     - ignore allocated memory check
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to deselect from the core_map to match the cpu_count.
 */
uint16_t _can_job_run_on_node(struct job_record *job_ptr, bitstr_t **core_map,
			      const uint32_t node_i, uint32_t s_p_n,
			      struct node_use_record *node_usage,
			      uint16_t cr_type,
			      bool test_only, bitstr_t **part_core_map)
{
	uint16_t cpus;
	uint64_t avail_mem, req_mem;
	uint32_t gres_cores, gres_cpus, cpus_per_core;
	int core_cnt, cpu_alloc_size, i;
	struct node_record *node_ptr = node_record_table_ptr + node_i;
	List gres_list;
	bitstr_t *part_core_map_ptr = NULL;

	if (((job_ptr->bit_flags & BACKFILL_TEST) == 0) &&
	    !test_only && IS_NODE_COMPLETING(node_ptr)) {
		/*
		 * Do not allocate more jobs to nodes with completing jobs,
		 * backfill scheduler independently handles completing nodes
		 */
		cpus = 0;
		return cpus;
	}

	if (part_core_map)
		part_core_map_ptr = part_core_map[node_i];
	cpus_per_core = select_node_record[node_i].vpus;
	node_ptr = select_node_record[node_i].node_ptr;
	if (node_usage[node_i].gres_list)
		gres_list = node_usage[node_i].gres_list;
	else
		gres_list = node_ptr->gres_list;
	core_cnt = select_node_record[node_i].tot_cores;

	gres_plugin_job_core_filter(job_ptr->gres_list, gres_list, test_only,
				    core_map[node_i], 0, core_cnt - 1,
				    node_ptr->name);
	if (s_p_n == NO_VAL) {
		gres_cores = gres_plugin_job_test(job_ptr->gres_list,
						  gres_list, test_only,
						  core_map[node_i], 0,
						  core_cnt - 1, job_ptr->job_id,
						  node_ptr->name);
	} else {
		gres_cores = _gres_sock_job_test(job_ptr->gres_list,
						 gres_list, test_only,
						 core_map[node_i],
						 job_ptr->job_id,
						 node_ptr->name, node_i, s_p_n);
	}
	if (gres_cores == 0)
		return (uint16_t) 0;

	if (cr_type & CR_CORE) {
		/* cpu_alloc_size = CPUs per core */
		cpu_alloc_size = select_node_record[node_i].vpus;
		cpus = _allocate_cores(job_ptr, core_map[node_i],
				       part_core_map_ptr, node_i,
				       &cpu_alloc_size, false);

	} else if (cr_type & CR_SOCKET) {
		/* cpu_alloc_size = CPUs per socket */
		cpu_alloc_size = select_node_record[node_i].cores *
				 select_node_record[node_i].vpus;
		cpus = _allocate_sockets(job_ptr, core_map[node_i],
					 part_core_map_ptr, node_i,
					 &cpu_alloc_size);
	} else {
		cpu_alloc_size = 1;
		cpus = _allocate_cores(job_ptr, core_map[node_i],
				       part_core_map_ptr, node_i,
				       &cpu_alloc_size, true);
	}

	if (cr_type & CR_MEMORY) {
		/*
		 * Memory Check: check pn_min_memory to see if:
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
				/*
				 * In this scenario, cpus represents cores and
				 * the cpu/core count will be inflated later on
				 * to include all of the threads on a core. So
				 * we need to compare apples to apples and only
				 * remove 1 cpu/core at a time.
				 */
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
			debug3("cons_tres: %s: cpu_alloc_size > cpus, cannot "
			       "continue (node: %s)", __func__, node_ptr->name);
			cpus = 0;
			break;
		} else {
			cpus -= cpu_alloc_size;
		}
	}

	if (cpus == 0)
		bit_clear_all(core_map[node_i]);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("cons_tres: %s: %u cpus on %s(%d), mem %"PRIu64"/%"PRIu64,
		     __func__, cpus, select_node_record[node_i].node_ptr->name,
		     node_usage[node_i].node_state,
		     node_usage[node_i].alloc_memory,
		     select_node_record[node_i].real_memory);
	}

	return cpus;
}

/*
 * Determine CPU/core availability for pending job
 *
 * IN: job_ptr       - pointer to the job requesting resources
 * IN: node_map      - bitmap of available nodes
 * IN/OUT: core_map  - per-node bitmaps of available cores
 * IN: cr_node_cnt   - total number of nodes in the cluster
 * IN: cr_type       - resource type
 * OUT: cpu_cnt      - number of CPUs that can be used by this job on each node
 * IN: test_only     - ignore allocated memory check
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 */
static void _get_res_avail(struct job_record *job_ptr, bitstr_t *node_map,
			   bitstr_t **core_map,
			   struct node_use_record *node_usage,
			   uint16_t cr_type, uint16_t **cpu_cnt_ptr,
			   bool test_only, bitstr_t **part_core_map)
{
	uint16_t *cpu_cnt;
	uint32_t n;
	uint32_t s_p_n = _socks_per_node(job_ptr);

	cpu_cnt = xmalloc(sizeof(uint16_t) * select_node_cnt);
	for (n = 0; n < select_node_cnt; n++) {
		if (!bit_test(node_map, n))
			continue;
		cpu_cnt[n] = _can_job_run_on_node(job_ptr, core_map, n, s_p_n,
						  node_usage, cr_type,
						  test_only, part_core_map);
	}
	*cpu_cnt_ptr = cpu_cnt;
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
 * RET - array with number of CPUs that can be run on each node in node_map
 */
static uint16_t *_select_nodes(struct job_record *job_ptr, uint32_t min_nodes,
				uint32_t max_nodes, uint32_t req_nodes,
				bitstr_t *node_bitmap, bitstr_t **avail_core,
				struct node_use_record *node_usage,
				uint16_t cr_type, bool test_only,
				bitstr_t **part_core_map,
				bool prefer_alloc_nodes)
{
	int i, rc;
	uint16_t *cpu_cnt, *cpus = NULL;
	uint32_t n, a;
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;

	if (bit_set_count(node_bitmap) < min_nodes)
		return NULL;

	_log_select_maps("_select_nodes/enter", node_bitmap, avail_core);
	/* get resource usage for this job from each available node */
	_get_res_avail(job_ptr, node_bitmap, avail_core, node_usage, cr_type,
		       &cpu_cnt, test_only, part_core_map);

	/* clear all nodes that do not have sufficient resources for this job */
	for (n = 0; n < select_node_cnt; n++) {
		if (bit_test(node_bitmap, n) && (cpu_cnt[n] == 0)) {
			/* insufficient resources available on this node */
			if (req_map && bit_test(req_map, n)) {
				/* cannot clear a required node! */
				xfree(cpu_cnt);
				return NULL;
			}
			bit_clear(node_bitmap, n);
		}
	}
	if (bit_set_count(node_bitmap) < min_nodes) {
		xfree(cpu_cnt);
		return NULL;
	}
	_log_select_maps("_select_nodes/elim_nodes", node_bitmap, avail_core);

	if (details_ptr->ntasks_per_node && details_ptr->num_tasks) {
		i  = details_ptr->num_tasks;
		i += (details_ptr->ntasks_per_node - 1);
		i /= details_ptr->ntasks_per_node;
		min_nodes = MAX(min_nodes, i);
	}

	/* choose the best nodes for the job */
	rc = _choose_nodes(job_ptr, node_bitmap, min_nodes, max_nodes,
			   req_nodes, cpu_cnt, cr_type, prefer_alloc_nodes);
	_log_select_maps("_select_nodes/choose_nodes", node_bitmap, avail_core);

	/*
	 * if successful, sync up the avail_core with the node_map, and
	 * create a cpus array
	 */
	if (rc == SLURM_SUCCESS) {
		cpus = xmalloc(bit_set_count(node_bitmap) * sizeof(uint16_t));
		for (a = 0, n = 0; n < select_node_cnt; n++) {
			if (bit_test(node_bitmap, n))
				cpus[a++] = cpu_cnt[n];
			else
				FREE_NULL_BITMAP(avail_core[n]);
		}
	}
	_log_select_maps("_select_nodes/sync_cores", node_bitmap, avail_core);
	xfree(cpu_cnt);
	return cpus;
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
	uint32_t r, c, cores;
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
			if (!p_ptr->row[r].row_bitmap ||
			    !p_ptr->row[r].row_bitmap[node_i])
				continue;
			cores = bit_size(p_ptr->row[r].row_bitmap[node_i]);
			for (c = 0; c < cores; c++) {
				if (bit_test(p_ptr->row[r].row_bitmap[node_i],
					     c))
					return 1;
			}
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
	uint64_t free_mem, min_mem;
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
				debug3("cons_tres: %s: node %s no mem (%"PRIu64" < %"PRIu64")",
					__func__,
					select_node_record[i].node_ptr->name,
					free_mem, min_mem);
				goto clear_bit;
			}
		} else if (cr_type & CR_MEMORY) {   /* --mem=0 for all memory */
			if (node_usage[i].alloc_memory) {
				debug3("cons_tres: %s: node %s mem in use %"PRIu64,
					__func__,
					select_node_record[i].node_ptr->name,
					node_usage[i].alloc_memory);
				goto clear_bit;
			}
		}

		/* Exclude nodes with reserved cores */
		if ((job_ptr->details->whole_node == 1) && exc_cores &&
		    exc_cores[i] && (bit_ffs(exc_cores[i]) != -1)) {
			debug3("cons_tres: %s: node %s exclusive", __func__,
			       select_node_record[i].node_ptr->name);
			goto clear_bit;
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
			debug3("cons_tres: %s: node %s lacks GRES",
			       __func__, node_ptr->name);
			goto clear_bit;
		}

		/* exclusive node check */
		if (node_usage[i].node_state >= NODE_CR_RESERVED) {
			debug3("cons_tres: %s: node %s in exclusive use",
			       __func__, node_ptr->name);
			goto clear_bit;

		/* non-resource-sharing node check */
		} else if (node_usage[i].node_state >= NODE_CR_ONE_ROW) {
			if ((job_node_req == NODE_CR_RESERVED) ||
			    (job_node_req == NODE_CR_AVAILABLE)) {
				debug3("cons_tres: %s: node %s non-sharing",
				       __func__, node_ptr->name);
				goto clear_bit;
			}
			/*
			 * cannot use this node if it is running jobs
			 * in sharing partitions
			 */
			if (_is_node_busy(cr_part_ptr, i, 1,
					  job_ptr->part_ptr, qos_preemptor)) {
				debug3("cons_tres: %s: node %s sharing?",
				       __func__, node_ptr->name);
				goto clear_bit;
			}

		/* node is NODE_CR_AVAILABLE - check job request */
		} else {
			if (job_node_req == NODE_CR_RESERVED) {
				if (_is_node_busy(cr_part_ptr, i, 0,
						  job_ptr->part_ptr,
						  qos_preemptor)) {
					debug3("cons_tres: %s: node %s busy",
					       __func__, node_ptr->name);
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
					debug3("cons_tres: %s: node %s vbusy",
					       __func__, node_ptr->name);
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

/* Allocate resources for a job now, if possible */
extern int run_now(struct job_record *job_ptr, bitstr_t *node_bitmap,
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
			info("cons_tres: Can't use Partition SelectType unless "
			     "using CR_Socket or CR_Core");
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
			cr_destroy_part_data(future_part);
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
			_rm_job_from_res(future_part, future_usage,
					 tmp_job_ptr, 0);
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
			cr_destroy_part_data(future_part);
			cr_destroy_node_data(future_usage, NULL);
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

		cr_destroy_part_data(future_part);
		cr_destroy_node_data(future_usage, NULL);
	}
	FREE_NULL_BITMAP(orig_node_map);
	FREE_NULL_BITMAP(save_node_map);

	return rc;
}

/* Determine if a job can ever run */
extern int test_only(struct job_record *job_ptr, bitstr_t *node_bitmap,
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
			info("cons_tres: Can't use Partition SelectType unless "
			     "using CR_Socket or CR_Core");
		}
	}

	rc = _job_test(job_ptr, node_bitmap, min_nodes, max_nodes, req_nodes,
		       SELECT_MODE_TEST_ONLY, tmp_cr_type, job_node_req,
		       select_part_record, select_node_usage, NULL, false,
		       false, false);
	return rc;
}

/* List sort function: sort by the job's expected end time */
static int _cr_job_list_sort(void *x, void *y)
{
	struct job_record *job1_ptr = *(struct job_record **) x;
	struct job_record *job2_ptr = *(struct job_record **) y;

	return (int) SLURM_DIFFTIME(job1_ptr->end_time, job2_ptr->end_time);
}

/*
 * For a given job already past it's end time, guess when it will actually end.
 * Used for backfill scheduling.
 */
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
		end_time = now + (365 * 24 * 60 * 60);	/* one year */
	} else {
		end_time = job_ptr->end_time + slurmctld_conf.kill_wait +
			   (over_time_limit  * 60);
	}
	if (end_time <= now)
		end_time = now + 1;

	return end_time;
}

/*
 * Determine where and when the job at job_ptr can begin execution by updating
 * a scratch cr_record structure to reflect each job terminating at the
 * end of its time limit and use this to show where and when the job at job_ptr
 * will begin execution. Used by Slurm's sched/backfill plugin.
 */
extern int will_run_test(struct job_record *job_ptr, bitstr_t *node_bitmap,
			 uint32_t min_nodes, uint32_t max_nodes,
			 uint32_t req_nodes, uint16_t job_node_req,
			 List preemptee_candidates, List *preemptee_job_list,
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
			info("cons_tres: Can't use Partition SelectType unless "
			     "using CR_Socket or CR_Core");
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
		cr_destroy_part_data(future_part);
		FREE_NULL_BITMAP(orig_map);
		return SLURM_ERROR;
	}

	/* Build list of running and suspended jobs */
	cr_job_list = list_create(NULL);
	job_iterator = list_iterator_create(job_list);
	while ((tmp_job_ptr = (struct job_record *) list_next(job_iterator))) {
		bool cleaning = job_cleaning(tmp_job_ptr);
		if (!cleaning && IS_JOB_COMPLETING(tmp_job_ptr))
			cleaning = true;
		if (!IS_JOB_RUNNING(tmp_job_ptr) &&
		    !IS_JOB_SUSPENDED(tmp_job_ptr) &&
		    !cleaning)
			continue;
		if (tmp_job_ptr->end_time == 0) {
			if (!cleaning) {
				error("%s: Active job %u has zero end_time",
				      __func__, tmp_job_ptr->job_id);
			}
			continue;
		}
		if (tmp_job_ptr->node_bitmap == NULL) {
			/*
			 * This should indicate a requeued job was cancelled
			 * while NHC was running
			 */
			if (!cleaning) {
				error("%s: Job %u has NULL node_bitmap",
				      __func__, tmp_job_ptr->job_id);
			}
			continue;
		}
		if (cleaning ||
		    !is_preemptable(tmp_job_ptr, preemptee_candidates)) {
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
			_rm_job_from_res(future_part, future_usage,
					 tmp_job_ptr, action);
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
				debug2("cons_tres: %s, job %u: overlap=%d",
				       __func__, tmp_job_ptr->job_id, overlap);
				if (!first_job_ptr)
					first_job_ptr = tmp_job_ptr;
				last_job_ptr = tmp_job_ptr;
				_rm_job_from_res(future_part, future_usage,
						 tmp_job_ptr, 0);
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
		while ((tmp_job_ptr = (struct job_record *)
			list_next(preemptee_iterator))) {
			if (bit_overlap(node_bitmap,
					tmp_job_ptr->node_bitmap) == 0)
				continue;
			list_append(*preemptee_job_list, tmp_job_ptr);
		}
		list_iterator_destroy(preemptee_iterator);
	}

	FREE_NULL_LIST(cr_job_list);
	cr_destroy_part_data(future_part);
	cr_destroy_node_data(future_usage, NULL);
	FREE_NULL_BITMAP(orig_map);

	return rc;
}

/*
 * Build an empty array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **build_core_array(void)
{
	bitstr_t **row_bitmap;
	row_bitmap = xmalloc(sizeof(bitstr_t *) * select_node_cnt);
	return row_bitmap;
}

/* Clear all elements of an array of bitmaps, one per node */
extern void clear_core_array(bitstr_t **core_array)
{
	int n;

	if (!core_array)
		return;
	for (n = 0; n < select_node_cnt; n++) {
		if (core_array[n])
			bit_clear_all(core_array[n]);
	}
}

/*
 * Copy an array of bitmaps, one per node
 * Use free_core_array() to release returned memory
 */
extern bitstr_t **copy_core_array(bitstr_t **core_array)
{
	bitstr_t **core_array2 = NULL;
	int n;

	if (core_array) {
		core_array2 = xmalloc(sizeof(bitstr_t *) * select_node_cnt);
		for (n = 0; n < select_node_cnt; n++) {
			if (core_array[n])
				core_array2[n] = bit_copy(core_array[n]);
		}
	}
	return core_array2;
}

/*
 * Return count of set bits in array of bitmaps, one per node
 */
extern int count_core_array_set(bitstr_t **core_array)
{
	int count = 0, n;

	if (!core_array)
		return count;
	for (n = 0; n < select_node_cnt; n++) {
		if (core_array[n])
			count += bit_set_count(core_array[n]);
	}
	return count;
}

/*
 * Set row_bitmap1 to core_array1 & core_array2
 */
extern void core_array_and(bitstr_t **core_array1, bitstr_t **core_array2)
{
	int n, s1, s2;
	for (n = 0; n < select_node_cnt; n++) {
		if (core_array1[n] && core_array2[n]) {
			s1 = bit_size(core_array1[n]);
			s2 = bit_size(core_array2[n]);
			if (s1 > s2)
				core_array2[n] = bit_realloc(core_array1[n],s1);
			else if (s1 < s2)
				core_array1[n] = bit_realloc(core_array1[n],s2);
			bit_and(core_array1[n], core_array2[n]);
		} else if (core_array1[n])
			bit_free(core_array1[n]);
	}
}

/*
 * Set row_bitmap1 to row_bitmap1 & !row_bitmap2
 * In other words, any bit set in row_bitmap2 is cleared from row_bitmap1
 */
extern void core_array_and_not(bitstr_t **core_array1, bitstr_t **core_array2)
{
	int n, s1, s2;
	for (n = 0; n < select_node_cnt; n++) {
		if (core_array1[n] && core_array2[n]) {
			s1 = bit_size(core_array1[n]);
			s2 = bit_size(core_array2[n]);
			if (s1 > s2)
				core_array2[n] = bit_realloc(core_array1[n],s1);
			else if (s1 < s2)
				core_array1[n] = bit_realloc(core_array1[n],s2);
			bit_and_not(core_array1[n], core_array2[n]);
		}
	}
}

/*
 * Set row_bitmap1 to core_array1 | core_array2
 */
extern void core_array_or(bitstr_t **core_array1, bitstr_t **core_array2)
{
	int n, s1, s2;
	for (n = 0; n < select_node_cnt; n++) {
		if (core_array1[n] && core_array2[n]) {
			s1 = bit_size(core_array1[n]);
			s2 = bit_size(core_array2[n]);
			if (s1 > s2)
				core_array2[n] = bit_realloc(core_array1[n],s1);
			else if (s1 < s2)
				core_array1[n] = bit_realloc(core_array1[n],s2);
			bit_or(core_array1[n], core_array2[n]);
		} else if (core_array2[n])
			core_array1[n] = bit_copy(core_array2[n]);
	}
}

/* Free an array of bitmaps, one per node */
extern void free_core_array(bitstr_t ***core_array)
{
	bitstr_t **core_array2 = *core_array;
	int n;

	if (core_array2) {
		for (n = 0; n < select_node_cnt; n++)
			FREE_NULL_BITMAP(core_array2[n]);
		xfree(core_array2);
		*core_array = NULL;
	}
}
