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

#include <string.h>
#include "select_cons_tres.h"
#include "dist_tasks.h"
#include "job_test.h"

#define _DEBUG 0	/* Enables module specific debugging */

/*
 * These symbols are defined here so when we link with something other
 * than the slurmctld we will have these symbols defined. They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern slurmctld_config_t slurmctld_config __attribute__((weak_import));
extern bitstr_t *idle_node_bitmap __attribute__((weak_import));
extern struct node_record *node_record_table_ptr __attribute__((weak_import));
extern List job_list __attribute__((weak_import));
#else
slurmctld_config_t slurmctld_config;
bitstr_t *idle_node_bitmap;
struct node_record *node_record_table_ptr;
List job_list;
#endif

typedef enum {
	HANDLE_JOB_RES_ADD,
	HANDLE_JOB_RES_REM,
	HANDLE_JOB_RES_TEST
} handle_job_res_t;

struct sort_support {
	int jstart;
	struct job_resources *tmpjobs;
};

typedef struct node_weight_struct {
	bitstr_t *node_bitmap;	/* bitmap of nodes with this weight */
	uint32_t weight;	/* priority of node for scheduling work on */
} node_weight_type;

typedef struct topo_weight_info {
	bitstr_t *node_bitmap;
	int node_cnt;
	uint64_t weight;
} topo_weight_info_t;

/* Local functions */
static void _add_job_res(job_resources_t *job_resrcs_ptr,
			 bitstr_t ***sys_resrcs_ptr);
static List _build_node_weight_list(bitstr_t *node_bitmap);
static int  _compare_support(const void *v, const void *v1);
static void _cpus_to_use(uint16_t *avail_cpus, int64_t rem_cpus, int rem_nodes,
			 struct job_details *details_ptr,
			 avail_res_t *avail_res, int node_inx,
			 uint16_t cr_type);
static bool _enough_nodes(int avail_nodes, int rem_nodes,
			  uint32_t min_nodes, uint32_t req_nodes);
static int _eval_nodes(struct job_record *job_ptr, gres_mc_data_t *mc_ptr,
		       bitstr_t *node_map, bitstr_t **avail_core,
		       uint32_t min_nodes, uint32_t max_nodes,
		       uint32_t req_nodes, avail_res_t **avail_res_array,
		       uint16_t cr_type, bool prefer_alloc_nodes,
		       bool first_pass);
static int _eval_nodes_busy(struct job_record *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_dfly(struct job_record *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_lln(struct job_record *job_ptr,
			   gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			   bitstr_t **avail_core, uint32_t min_nodes,
			   uint32_t max_nodes, uint32_t req_nodes,
			   avail_res_t **avail_res_array, uint16_t cr_type,
			   bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_serial(struct job_record *job_ptr,
			      gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			      bitstr_t **avail_core, uint32_t min_nodes,
			      uint32_t max_nodes, uint32_t req_nodes,
			      avail_res_t **avail_res_array, uint16_t cr_type,
			      bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_spread(struct job_record *job_ptr,
			      gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			      bitstr_t **avail_core, uint32_t min_nodes,
			      uint32_t max_nodes, uint32_t req_nodes,
			      avail_res_t **avail_res_array, uint16_t cr_type,
			      bool prefer_alloc_nodes, bool first_pass);
static int _eval_nodes_topo(struct job_record *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass);
static int _handle_job_res(job_resources_t *job_resrcs_ptr,
			   bitstr_t ***sys_resrcs_ptr,
			   handle_job_res_t type);
static int _is_node_busy(struct part_res_record *p_ptr, uint32_t node_i,
			 int sharing_only, struct part_record *my_part_ptr,
			 bool qos_preemptor);
static int _job_fit_test(job_resources_t *job_resrcs_ptr,
			 bitstr_t **sys_resrcs_ptr);
static int _node_weight_find(void *x, void *key);
static void _node_weight_free(void *x);
static int _node_weight_sort(void *x, void *y);
static void _rm_job_res(job_resources_t *job_resrcs_ptr,
		bitstr_t ***sys_resrcs_ptr);

/* Find node_weight_type element from list with same weight as node config */
static int _node_weight_find(void *x, void *key)
{
	node_weight_type *nwt = (node_weight_type *) x;
	struct config_record *config_ptr = (struct config_record *) key;
	if (nwt->weight == config_ptr->weight)
		return 1;
	return 0;
}

/* Free node_weight_type element from list */
static void _node_weight_free(void *x)
{
	node_weight_type *nwt = (node_weight_type *) x;
	bit_free(nwt->node_bitmap);
	xfree(nwt);
}

/* Sort list of node_weight_type reords in order of increasing node weight */
static int _node_weight_sort(void *x, void *y)
{
	node_weight_type *nwt1 = *(node_weight_type **) x;
	node_weight_type *nwt2 = *(node_weight_type **) y;
	return (int) (nwt1->weight - nwt2->weight);
}

/*
 * Given a bitmap of available nodes, return a list of node_weight_type
 * records in order of increasing "weight" (priority)
 */
static List _build_node_weight_list(bitstr_t *node_bitmap)
{
	int i, i_first, i_last;
	List node_list;
	struct node_record *node_ptr;
	node_weight_type *nwt;

	xassert(node_bitmap);
	/* Build list of node_weight_type records, one per node weight */
	node_list = list_create(_node_weight_free);
	i_first = bit_ffs(node_bitmap);
	if (i_first == -1)
		return node_list;
	i_last = bit_fls(node_bitmap);
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		nwt = list_find_first(node_list, _node_weight_find,
				      node_ptr->config_ptr);
		if (!nwt) {
			nwt = xmalloc(sizeof(node_weight_type));
			nwt->node_bitmap = bit_alloc(select_node_cnt);
			nwt->weight = node_ptr->config_ptr->weight;
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
#if _DEBUG
	int i;
	char *gres_info = "";

	if (!avail_res) {
		info("Node:%s No resources", node_name);
		return;
	}

	info("Node:%s Sockets:%u SpecThreads:%u CPUs:Min-Max,Avail:%u-%u,%u VPUs:%u",
	     node_name, avail_res->sock_cnt, avail_res->spec_threads,
	     avail_res->min_cpus, avail_res->max_cpus, avail_res->avail_cpus,
	     avail_res->vpus);
	gres_info = gres_plugin_sock_str(avail_res->sock_gres_list, -1);
	if (gres_info) {
		info("  AnySocket %s", gres_info);
		xfree(gres_info);
	}
	for (i = 0; i < avail_res->sock_cnt; i++) {
		gres_info = gres_plugin_sock_str(avail_res->sock_gres_list, i);
		if (gres_info) {
			info("  Socket[%d] Cores:%u GRES:%s", i,
			     avail_res->avail_cores_per_sock[i], gres_info);
			xfree(gres_info);
		} else {
			info("  Socket[%d] Cores:%u", i,
			     avail_res->avail_cores_per_sock[i]);
		}
	}
#endif
}

/*
 * build_row_bitmaps: A job has been removed from the given partition,
 *                    so the row_bitmap(s) need to be reconstructed.
 *                    Optimize the jobs into the least number of rows,
 *                    and make the lower rows as dense as possible.
 *
 * IN p_ptr - the partition that has jobs to be optimized
 * IN job_ptr - pointer to single job removed, pass NULL to completely rebuild
 */
extern void build_row_bitmaps(struct part_res_record *p_ptr,
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
					_add_job_res(this_row->job_list[j],
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
				_add_job_res(p_ptr->row[i].job_list[j],
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

/* test for conflicting core bitmap elements */
extern int can_job_fit_in_row(struct job_resources *job,
			      struct part_row_data *r_ptr)
{
	if ((r_ptr->num_jobs == 0) || !r_ptr->row_bitmap)
		return 1;

	if (!r_ptr->row_bitmap_size)
		r_ptr->row_bitmap_size = select_node_cnt;

	return _job_fit_test(job, r_ptr->row_bitmap);
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
 * Test if job can fit into the given set of core_bitmaps
 * IN job_resrcs_ptr - resources allocated to a job
 * IN sys_resrcs_ptr - bitmap array (one per node) of available cores
 * RET 1 on success, 0 otherwise
 * NOTE: Patterned after job_fits_into_cores() in src/common/job_resources.c
 */
static int _job_fit_test(job_resources_t *job_resrcs_ptr,
			 bitstr_t **sys_resrcs_ptr)
{
	if (!sys_resrcs_ptr)
		return 1;			/* Success */

	return _handle_job_res(job_resrcs_ptr, &sys_resrcs_ptr,
			       HANDLE_JOB_RES_TEST);
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
	int c, c_job, c_off = 0, c_max;
	int rep_inx = 0, rep_offset = 0;
	bitstr_t **core_array;

	if (!job_resrcs_ptr->core_bitmap)
		return 1;

	/* create row_bitmap data structure as needed */
	if (*sys_resrcs_ptr == NULL) {
		if (type == HANDLE_JOB_RES_TEST)
			return 1;
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
			if (!core_array[i]) {
				if (type != HANDLE_JOB_RES_TEST)
					error("%s: %s: core_array[%d] is NULL %d",
					      plugin_type, __func__, i, type);
				continue;	/* Move to next node */
			}

			switch (type) {
			case HANDLE_JOB_RES_ADD:
				bit_set_all(core_array[i]);
				break;
			case HANDLE_JOB_RES_REM:
				bit_clear_all(core_array[i]);
				break;
			case HANDLE_JOB_RES_TEST:
				if (bit_ffs(core_array[i]) != -1)
					return 0; /* Whole node conflict */
				break;
			}
			continue;	/* Move to next node */
		}
		if (rep_offset >= job_resrcs_ptr->sock_core_rep_count[rep_inx]){
			rep_offset = 1;
			rep_inx++;
		} else {
			rep_offset++;
		}
		c_job = job_resrcs_ptr->sockets_per_node[rep_inx] *
			job_resrcs_ptr->cores_per_socket[rep_inx];
		c_max = MIN(select_node_record[i].tot_cores, c_job);
		for (c = 0; c < c_max; c++) {
			if (!bit_test(job_resrcs_ptr->core_bitmap, c_off + c))
				continue;
			if (!core_array[i]) {
				if (type != HANDLE_JOB_RES_TEST)
					error("%s: %s: core_array[%d] is NULL %d",
					      plugin_type, __func__, i, type);
				continue;	/* Move to next node */
			}
			switch (type) {
			case HANDLE_JOB_RES_ADD:
				bit_set(core_array[i], c);
				break;
			case HANDLE_JOB_RES_REM:
				bit_clear(core_array[i], c);
				break;
			case HANDLE_JOB_RES_TEST:
				if (bit_test(core_array[i], c))
					return 0;    /* Core conflict on node */
				break;
			}
		}
		c_off += c_job;
	}
	return 1;
}

/*
 * Add job resource allocation to record of resources allocated to all nodes
 * IN job_resrcs_ptr - resources allocated to a job
 * IN/OUT sys_resrcs_ptr - bitmap array (one per node) of available cores,
 *			   allocated as needed
 * NOTE: Patterned after add_job_to_cores() in src/common/job_resources.c
 */
static void _add_job_res(job_resources_t *job_resrcs_ptr,
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
static void _rm_job_res(job_resources_t *job_resrcs_ptr,
			bitstr_t ***sys_resrcs_ptr)
{
	(void)_handle_job_res(job_resrcs_ptr, sys_resrcs_ptr,
			      HANDLE_JOB_RES_REM);
}

extern void add_job_to_res(job_resources_t *job_resrcs_ptr,
			   struct part_row_data *r_ptr,
			   const uint16_t *bits_per_node)
{

	if (!r_ptr->row_bitmap_size)
		r_ptr->row_bitmap_size = select_node_cnt;

	_add_job_res(job_resrcs_ptr, &r_ptr->row_bitmap);
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
	int node_core_spec, thread_spec = 0;
	struct node_record *node_ptr;

	if ((core_spec != NO_VAL16) &&
	    (core_spec & CORE_SPEC_THREAD)) {	/* Reserving threads */
		thread_spec = core_spec & (~CORE_SPEC_THREAD);
		core_spec = NO_VAL16;		/* Don't remove cores */
	}

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
		if (core_spec == 0)    /* Job can't over-ride system defaults */
			continue;

		if (thread_spec &&
		    (select_node_record[i].cpus ==
		     select_node_record[i].tot_cores)) {
			/* Each core has one thead, reserve cores here */
			node_core_spec = thread_spec;
		} else
			node_core_spec = core_spec;

		/*
		 * remove node's specialized cores accounting toward the
		 * requested limit if allowed by configuration
		 */
		node_ptr = select_node_record[i].node_ptr;
		if (node_ptr->node_spec_bitmap) {
			int node_spec_core_cnt = 0;
			for (c = 0; c < select_node_record[i].tot_cores; c++) {
				if (!bit_test(node_ptr->node_spec_bitmap, c)) {
					bit_clear(avail_cores[i], c);
					node_spec_core_cnt++;
					if ((node_core_spec != NO_VAL16) ||
					    (node_spec_core_cnt >=
					     node_core_spec))
						break;
				}
			}
			if ((node_core_spec == NO_VAL16) ||
			    (node_spec_core_cnt >= node_core_spec))
				continue;
			rem_core_spec = node_core_spec - node_spec_core_cnt;
		} else {
			if (node_core_spec == NO_VAL16)
				continue;
			rem_core_spec = node_core_spec;
		}

		/*
		 * Clear core bitmap for specified core count. Start with
		 * highest socket and core, then work down to lower sockets
		 */
		sock_per_node = select_node_record[i].tot_sockets;
		for (c = select_node_record[i].cores - 1;
		     (c >= 0) && (rem_core_spec > 0); c--) {
			for (s = sock_per_node - 1;
			     (s >= 0) && (rem_core_spec > 0); s--) {

				core_inx = c + s * select_node_record[i].cores;
				if (!bit_test(avail_cores[i], core_inx))
					continue;
				bit_clear(avail_cores[i], core_inx);
				rem_core_spec--;
			}
		}
	}

	return avail_cores;
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
			 int rem_nodes, struct job_details *details_ptr,
			 avail_res_t *avail_res, int node_inx,
			 uint16_t cr_type)
{
	int resv_cpus;	/* CPUs to be allocated on other nodes */

	if (details_ptr->whole_node == 1)	/* Use all resources on node */
		return;

	resv_cpus = MAX((rem_nodes - 1), 0);
	resv_cpus *= common_cpus_per_core(details_ptr, node_inx);
	if (cr_type & CR_SOCKET)
		resv_cpus *= select_node_record[node_inx].cores;
	rem_max_cpus -= resv_cpus;
	if (*avail_cpus > rem_max_cpus) {
		*avail_cpus = MAX(rem_max_cpus, (int)details_ptr->pn_min_cpus);
		*avail_cpus = MAX(*avail_cpus, details_ptr->min_gres_cpu);
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
static void _select_cores(struct job_record *job_ptr, gres_mc_data_t *mc_ptr,
			  bool enforce_binding, int node_inx,
			  uint16_t *avail_cpus, uint32_t max_nodes,
			  int rem_nodes, bitstr_t **avail_core,
			  avail_res_t **avail_res_array, bool first_pass)
{
	int alloc_tasks = 0;
	uint32_t min_tasks_this_node = 0, max_tasks_this_node = 0;
	struct job_details *details_ptr = job_ptr->details;

	rem_nodes = MIN(rem_nodes, 1);	/* If range of node counts */
	if (mc_ptr->ntasks_per_node) {
		min_tasks_this_node = mc_ptr->ntasks_per_node;
		max_tasks_this_node = mc_ptr->ntasks_per_node;
	} else if (mc_ptr->ntasks_per_board) {
		min_tasks_this_node = mc_ptr->ntasks_per_board;
		max_tasks_this_node = mc_ptr->ntasks_per_board *
				      select_node_record[node_inx].boards;
	} else if (mc_ptr->ntasks_per_socket) {
		min_tasks_this_node = mc_ptr->ntasks_per_socket;
		max_tasks_this_node = mc_ptr->ntasks_per_socket *
				      select_node_record[node_inx].tot_sockets;
	} else if (mc_ptr->ntasks_per_core) {
		min_tasks_this_node = mc_ptr->ntasks_per_core;
		max_tasks_this_node = mc_ptr->ntasks_per_core *
				      select_node_record[node_inx].tot_cores;
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
	if (mc_ptr->cpus_per_task &&
	    (!details_ptr || !details_ptr->overcommit)) {
		alloc_tasks = avail_res_array[node_inx]->avail_cpus /
			      mc_ptr->cpus_per_task;
		if (alloc_tasks < min_tasks_this_node)
			max_tasks_this_node = 0;
	}

	*avail_cpus = avail_res_array[node_inx]->avail_cpus;
	if (job_ptr->gres_list) {
		gres_plugin_job_core_filter3(mc_ptr,
				avail_res_array[node_inx]->sock_gres_list,
				avail_res_array[node_inx]->sock_cnt,
				select_node_record[node_inx].cores,
				select_node_record[node_inx].vpus, avail_cpus,
				&min_tasks_this_node, &max_tasks_this_node,
				rem_nodes, enforce_binding, first_pass,
				avail_core[node_inx]);
	}
	if (max_tasks_this_node == 0) {
		*avail_cpus = 0;
	} else if ((slurmctld_conf.select_type_param & CR_ONE_TASK_PER_CORE) &&
		   ((mc_ptr->ntasks_per_core == INFINITE16) ||
		    (mc_ptr->ntasks_per_core == 0)) &&
		   details_ptr && (details_ptr->min_gres_cpu == 0)) {
		*avail_cpus = bit_set_count(avail_core[node_inx]);
	}
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
static int _eval_nodes(struct job_record *job_ptr, gres_mc_data_t *mc_ptr,
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
	struct node_record *node_ptr = NULL;
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
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bool enforce_binding = false;
	uint16_t *avail_cpu_per_node = NULL;

	xassert(node_map);
	if (select_node_cnt != node_record_count) {
		error("%s: node count inconsistent with slurmctld (%u != %u)",
		      plugin_type, select_node_cnt, node_record_count);
		return error_code;
	}
	if (bit_set_count(node_map) < min_nodes)
		return error_code;

	if ((details_ptr->req_node_bitmap) &&
	    (!bit_super_set(details_ptr->req_node_bitmap, node_map)))
		return error_code;

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

	if (job_ptr->gres_list && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;

	/* make allocation for 50 sets of consecutive nodes, expand as needed */
	consec_size = 50;
	consec_cpus   = xmalloc(sizeof(int) * consec_size);
	consec_nodes  = xmalloc(sizeof(int) * consec_size);
	consec_start  = xmalloc(sizeof(int) * consec_size);
	consec_end    = xmalloc(sizeof(int) * consec_size);
	consec_req    = xmalloc(sizeof(int) * consec_size);
	consec_weight = xmalloc(sizeof(uint64_t) * consec_size);

	/* Build table with information about sets of consecutive nodes */
	consec_index = 0;
	consec_req[consec_index] = -1;	/* no required nodes here by default */
	consec_weight[consec_index] = NO_VAL64;

	avail_cpu_per_node = xmalloc(sizeof(uint16_t) * select_node_cnt);
	rem_cpus = details_ptr->min_cpus;
	rem_max_cpus = details_ptr->max_cpus;
	min_rem_nodes = min_nodes;
	if ((gres_per_job = gres_plugin_job_sched_init(job_ptr->gres_list))) {
		rem_nodes = MIN(min_nodes, req_nodes);
		consec_gres = xmalloc(sizeof(List) * consec_size);
	} else
		rem_nodes = MAX(min_nodes, req_nodes);

	/*
	 * If there are required nodes, first determine the resources they
	 * provide, then select additional resources as needed in next loop
	 */
	if (req_map) {
		int i_first, i_last;
		i_first = bit_ffs(req_map);
		if (i_first >= 0) {
			i_last = bit_fls(req_map);
			if (((i_last - i_first + 1) > max_nodes) &&
			    (bit_set_count(req_map) > max_nodes))
				goto fini;
		} else
			i_last = i_first - 1;
		for (i = i_first; ((i <= i_last) && (max_nodes > 0)); i++) {
			if (!bit_test(req_map, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
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
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			bit_and(node_map, req_map);
			goto fini;
		}
		if (max_nodes <= 0) {
			error_code = SLURM_ERROR;
			goto fini;
		}
	}

	for (i = 0; i < select_node_cnt; i++) {		/* For each node */
		if ((consec_index + 1) >= consec_size) {
			consec_size *= 2;
			xrealloc(consec_cpus,  sizeof(int) * consec_size);
			xrealloc(consec_nodes, sizeof(int) * consec_size);
			xrealloc(consec_start, sizeof(int) * consec_size);
			xrealloc(consec_end,   sizeof(int) * consec_size);
			xrealloc(consec_req,   sizeof(int) * consec_size);
			xrealloc(consec_weight,
			         sizeof(uint64_t) * consec_size);
			if (gres_per_job) {
				xrealloc(consec_gres,
					 sizeof(List) * consec_size);
			}
		}
		if (req_map)
			required_node = bit_test(req_map, i);
		else
			required_node = false;
		if (!bit_test(node_map, i)) {
			node_ptr = NULL;    /* Use as flag, avoid second test */
		} else if (required_node) {
			node_ptr = node_record_table_ptr + i;
		} else {
			node_ptr = node_record_table_ptr + i;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			if (avail_cpus == 0) {
				bit_clear(node_map, i);
				node_ptr = NULL;
				/*
				 * If first_pass == true then _select_cores()
				 * enforces GRES binding to cores, even if
				 * enforce_binding == false. Set avail_res_cnt
				 * to something we can use to eliminate nodes
				 * with low resource availability and retry.
				 */
				if (first_pass && !enforce_binding &&
				    job_ptr->gres_list &&
				    (avail_res_array[i]->avail_res_cnt == 0)) {
					avail_res_array[i]->avail_res_cnt =
						avail_res_array[i]->avail_cpus;
				}
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
				gres_plugin_job_sched_consec(
					&consec_gres[consec_index],
					job_ptr->gres_list,
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

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		if (consec_index == 0) {
			info("%s: %s: consec_index is zero", plugin_type,
			     __func__);
		}
		for (i = 0; i < consec_index; i++) {
			char *gres_str = NULL, *gres_print = "";
			bitstr_t *host_bitmap;
			char *host_list;
			if (gres_per_job) {
				gres_str = gres_plugin_job_sched_str(
						consec_gres[i],
						job_ptr->gres_list);
				if (gres_str) {
					xstrcat(gres_str, " ");
					gres_print = gres_str;
				}
			}

			host_bitmap = bit_alloc(select_node_cnt);
			bit_nset(host_bitmap, consec_start[i], consec_end[i]);
			host_list = bitmap2node_name(host_bitmap);
			info("%s: eval_nodes: set:%d consec "
			     "CPUs:%d nodes:%d:%s %sbegin:%d end:%d required:%d weight:%"PRIu64,
			     plugin_type, i, consec_cpus[i], consec_nodes[i],
			     host_list, gres_print, consec_start[i],
			     consec_end[i], consec_req[i], consec_weight[i]);
			bit_free(host_bitmap);
			xfree(gres_str);
			xfree(host_list);
		}
	}

	/* Compute CPUs already allocated to required nodes */
	if ((details_ptr->max_cpus != NO_VAL) &&
	    (total_cpus > details_ptr->max_cpus)) {
		info("%s: %s: %pJ can't use required nodes due to max CPU limit",
		     plugin_type, __func__, job_ptr);
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
				sufficient = gres_plugin_job_sched_sufficient(
						job_ptr->gres_list,
						consec_gres[i]);
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
				      gres_plugin_job_sched_test(
						job_ptr->gres_list,
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
				/* enforce the max_cpus limit */
				total_cpus += avail_cpus;
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("%s: %s: %pJ can't use node %d without exceeding job limit",
					       plugin_type, __func__,
					       job_ptr, i);
					total_cpus -= avail_cpus;
					continue;
				}
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list, avail_cpus);
				}
			}
			for (i = (best_fit_req - 1);
			     i >= consec_start[best_fit_index]; i--) {
				if ((max_nodes == 0) ||
				    ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				     (!gres_per_job ||
				      gres_plugin_job_sched_test(
						job_ptr->gres_list,
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
				total_cpus += avail_cpus;
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("%s: %s: %pJ can't use node %d without exceeding job limit",
					       plugin_type, __func__,
					       job_ptr, i);
					total_cpus -= avail_cpus;
					continue;
				}
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list, avail_cpus);
				}
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
					    !gres_plugin_job_sched_test2(
							job_ptr->gres_list,
							avail_res_array[i]->
							sock_gres_list,
							job_ptr->job_id)) {
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
				      gres_plugin_job_sched_test(
						job_ptr->gres_list,
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
				total_cpus += avail_cpus;
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("%s: %s: %pJ can't use node %d without exceeding job limit",
					       plugin_type, __func__,
					       job_ptr, i);
					total_cpus -= avail_cpus;
					continue;
				}
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				bit_set(node_map, i);
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list, avail_cpus);
				}
			}
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
			error_code = SLURM_SUCCESS;
			break;
		}
		consec_cpus[best_fit_index] = 0;
		consec_nodes[best_fit_index] = 0;
	}

	if (error_code && (rem_cpus <= 0) &&
	    gres_plugin_job_sched_test(job_ptr->gres_list, job_ptr->job_id) &&
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
static int _eval_nodes_spread(struct job_record *job_ptr,
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
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	struct node_record *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	bool enforce_binding = false;

	if (job_ptr->gres_list && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	rem_max_cpus = details_ptr->max_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_plugin_job_sched_init(job_ptr->gres_list)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

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
			node_ptr = node_record_table_ptr + i;
			if (!bit_test(node_map, i)) {
				debug("%pJ required node %s not available",
				      job_ptr, node_ptr->name);
				continue;
			}
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if ((avail_cpus > 0) && (max_nodes > 0)) {
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				/* leaving bitmap set, decr max limit */
				max_nodes--;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list, avail_cpus);
				}
			} else {	/* node not selected (yet) */
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
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
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (avail_cpus == 0)
				continue;
			total_cpus += avail_cpus;
			if ((details_ptr->max_cpus != NO_VAL) &&
			    (total_cpus > details_ptr->max_cpus)) {
				debug2("%s: %s: %pJ can't use node %d without exceeding job limit",
				       plugin_type, __func__, job_ptr, i);
				total_cpus -= avail_cpus;
				continue;
			}
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			bit_set(node_map, i);
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_plugin_job_sched_test(job_ptr->gres_list,
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
		   !gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	bit_free(orig_node_map);
	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources using busy nodes first.
 */
static int _eval_nodes_busy(struct job_record *job_ptr,
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
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	struct node_record *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	bool enforce_binding = false;

	if (job_ptr->gres_list && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	rem_max_cpus = details_ptr->max_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_plugin_job_sched_init(job_ptr->gres_list)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

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
			node_ptr = node_record_table_ptr + i;
			if (!bit_test(node_map, i)) {
				debug("%pJ required node %s not available",
				      job_ptr, node_ptr->name);
				continue;
			}
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if ((avail_cpus > 0) && (max_nodes > 0)) {
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				/* leaving bitmap set, decr max limit */
				if (max_nodes)
					max_nodes--;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list, avail_cpus);
				}
			} else {	/* node not selected (yet) */
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
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
					      avail_res_array, first_pass);
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
				if (avail_cpus == 0)
					continue;
				total_cpus += avail_cpus;
				if ((details_ptr->max_cpus != NO_VAL) &&
				    (total_cpus > details_ptr->max_cpus)) {
					debug2("%s: %s: %pJ can't use node %d without exceeding job limit",
					       plugin_type, __func__, job_ptr,
					       i);
					total_cpus -= avail_cpus;
					continue;
				}
				rem_cpus -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				bit_set(node_map, i);
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list,
						avail_cpus);
				}
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    gres_plugin_job_sched_test(
							job_ptr->gres_list,
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
		   !gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	bit_free(orig_node_map);
	return error_code;
}

static int _topo_weight_find(void *x, void *key)
{
	topo_weight_info_t *nw = (topo_weight_info_t *) x;
	topo_weight_info_t *nw_key = (topo_weight_info_t *) key;
	if (nw->weight == nw_key->weight)
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
	info("%s: Topo:%s weight:%"PRIu64, __func__, node_names, nw->weight);
	xfree(node_names);
	return 0;
}
static int _topo_weight_sort(void *x, void *y)
{
	topo_weight_info_t *nwt1 = *(topo_weight_info_t **) x;
	topo_weight_info_t *nwt2 = *(topo_weight_info_t **) y;
	return (int) (nwt1->weight - nwt2->weight);
}

/*
 * Allocate resources to the job on one leaf switch if possible,
 * otherwise distribute the job allocation over many leaf switches.
 */
static int _eval_nodes_dfly(struct job_record *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass)
{
	int       *switch_cpu_cnt = NULL;	/* total CPUs on switch */
	List      *switch_gres = NULL;		/* available GRES on switch */
	bitstr_t **switch_node_bitmap = NULL;	/* nodes on this switch */
	int       *switch_node_cnt = NULL;	/* total nodes on switch */
	int       *switch_required = NULL;	/* set if has required node */
	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;	/* required node bitmap */
	bitstr_t  *req2_nodes_bitmap  = NULL;	/* required+lowest prio nodes */
	bitstr_t  *best_nodes_bitmap  = NULL;	/* required+low prio nodes */
	int i, i_first, i_last, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt = 0, best_node_cnt = 0, req_node_cnt = 0;
	List best_gres = NULL;
	struct switch_record *switch_ptr;
	List node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	ListIterator iter;
	struct node_record *node_ptr;
	uint16_t avail_cpus = 0;
	int64_t rem_max_cpus;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	bool enforce_binding = false;
	struct job_details *details_ptr = job_ptr->details;
	bool gres_per_job, sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	int64_t time_waiting = 0;
	int leaf_switch_count = 0, req_leaf_switch_count = 0;
	int top_switch_inx = -1;
	int prev_rem_nodes;

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

	if (job_ptr->gres_list && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	rem_max_cpus = details_ptr->max_cpus;
	min_rem_nodes = min_nodes;
	if ((gres_per_job = gres_plugin_job_sched_init(job_ptr->gres_list)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   node_map)) {
			info("%s: %s: %pJ requires nodes which are not currently available",
			      plugin_type, __func__, job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%s: %s: %pJ required node list has no nodes",
			      plugin_type, __func__, job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (req_node_cnt > max_nodes) {
			info("%s: %s: %pJ requires more nodes than currently available (%u>%u)",
			      plugin_type, __func__, job_ptr, req_node_cnt,
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
	i_first = bit_ffs(node_map);
	if (i_first == -1) {
		debug("%s: %s: %pJ node_map is empty",
		      plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	i_last = bit_fls(node_map);
	avail_cpu_per_node = xmalloc(sizeof(uint16_t) * select_node_cnt);
	node_weight_list = list_create(_topo_weight_free);
	for (i = i_first; i <= i_last; i++) {
		topo_weight_info_t nw_static;
		if (!bit_test(node_map, i))
			continue;
		if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i)) {
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (avail_cpus == 0) {
				debug2("%s: %s: %pJ insufficient resources on required node",
				       plugin_type, __func__, job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			avail_cpu_per_node[i] = avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
		}

		node_ptr = node_record_table_ptr + i;
		nw_static.weight = node_ptr->sched_weight;
		nw = list_find_first(node_weight_list, _topo_weight_find,
				     &nw_static);
		if (!nw) {	/* New node weight to add */
			nw = xmalloc(sizeof(topo_weight_info_t));
			nw->node_bitmap = bit_alloc(select_node_cnt);
			nw->weight = node_ptr->sched_weight;
			list_append(node_weight_list, nw);
		}
		bit_set(nw->node_bitmap, i);
		nw->node_cnt++;
	}

	if (req_nodes_bitmap) {
		bit_and(node_map, req_nodes_bitmap);
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			info("%s: %s: %pJ requires nodes exceed maximum node limit",
			     plugin_type, __func__, job_ptr);
			goto fini;
		}
	} else {
		bit_clear_all(node_map);
	}

	list_sort(node_weight_list, _topo_weight_sort);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		(void) list_for_each(node_weight_list, _topo_weight_log, NULL);

	/*
	 * Identify the highest level switch to be used.
	 * Note that nodes can be on multiple non-overlapping switches.
	 */
	switch_cpu_cnt     = xmalloc(sizeof(int)        * switch_record_cnt);
	switch_gres        = xmalloc(sizeof(List)       * switch_record_cnt);
	switch_node_bitmap = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switch_node_cnt    = xmalloc(sizeof(int)        * switch_record_cnt);
	switch_required    = xmalloc(sizeof(int)        * switch_record_cnt);

	if (!req_nodes_bitmap)
		nw = list_peek(node_weight_list);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		switch_node_bitmap[i] = bit_copy(switch_ptr->node_bitmap);
		if (req_nodes_bitmap &&
		    bit_overlap(req_nodes_bitmap, switch_node_bitmap[i])) {
			switch_required[i] = 1;
			if (switch_record_table[i].level == 0) {
				leaf_switch_count++;
				req_leaf_switch_count++;
			}
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
		if (!req_nodes_bitmap &&
		    bit_overlap(nw->node_bitmap, switch_node_bitmap[i])) {
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
		error("%s: %s: %pJ unable to identify top level switch",
		       plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that all specificly required nodes are on shared network */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap,
			   switch_node_bitmap[top_switch_inx])) {
		rc = SLURM_ERROR;
		info("%s: %s: %pJ requires nodes that do not have shared network",
		     plugin_type, __func__, job_ptr);
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
	best_nodes_bitmap = bit_alloc(select_node_cnt);
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
		i_first = bit_ffs(nw->node_bitmap);
		if (i_first == -1)
			continue;
		i_last = bit_fls(nw->node_bitmap);
		for (i = i_first; i <= i_last; i++) {
			if (avail_cpu_per_node[i])
				continue;	/* Required node */
			if (!bit_test(nw->node_bitmap, i) ||
			    !bit_test(switch_node_bitmap[top_switch_inx], i))
				continue;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			if (avail_cpus == 0) {
				bit_clear(nw->node_bitmap, i);
				continue;
			}
			bit_set(best_nodes_bitmap, i);
			avail_cpu_per_node[i] = avail_cpus;
			best_cpu_cnt += avail_cpus;
			best_node_cnt++;
			if (gres_per_job) {
				gres_plugin_job_sched_consec(
					&best_gres, job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list);
			}
		}

		sufficient = (best_cpu_cnt >= rem_cpus) &&
			     _enough_nodes(best_node_cnt, rem_nodes,
					   min_nodes, req_nodes);
		if (sufficient && gres_per_job) {
			sufficient = gres_plugin_job_sched_sufficient(
					job_ptr->gres_list, best_gres);
		}
	}
	list_iterator_destroy(iter);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *gres_str = NULL, *gres_print = "";
		char *node_names;
		if (req_nodes_bitmap) {
			node_names = bitmap2node_name(req_nodes_bitmap);
			info("%s: Required nodes:%s", __func__, node_names);
			xfree(node_names);
		}
		node_names = bitmap2node_name(best_nodes_bitmap);
		if (gres_per_job) {
			gres_str = gres_plugin_job_sched_str(best_gres,
							job_ptr->gres_list);
			if (gres_str)
				gres_print = gres_str;
		}
		info("%s: Best nodes:%s node_cnt:%d cpu_cnt:%d %s", __func__,
		     node_names, best_node_cnt, best_cpu_cnt, gres_print);
		xfree(node_names);
		xfree(gres_str);
	}
	if (!sufficient) {
		info("%s: %s: insufficient resources currently available for %pJ",
		      plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Add lowest weight nodes. Treat similar to required nodes for the job.
	 * Job will still need to add some higher weight nodes later.
	 */
	if (req2_nodes_bitmap) {
		i_first = bit_ffs(req2_nodes_bitmap);
		if (i_first >= 0)
			i_last = bit_fls(req2_nodes_bitmap);
		else
			i_last = -2;
		for (i = i_first; ((i <= i_last) && (max_nodes > 0)); i++) {
			if (!bit_test(req2_nodes_bitmap, i))
				continue;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			avail_cpus = avail_cpu_per_node[i];
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
		}

		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_required[i])
				continue;
			if (bit_overlap(req2_nodes_bitmap,
					switch_node_bitmap[i])) {
				switch_required[i] = 1;
				if (switch_record_table[i].level == 0) {
					leaf_switch_count++;
					req_leaf_switch_count++;
				}
			}
		}
		bit_or(node_map, req2_nodes_bitmap);
		if ((job_ptr->req_switch > 0) &&
		    (req_leaf_switch_count > job_ptr->req_switch) &&
		    (time_waiting < job_ptr->wait4switch)) {
			info("%s: %s: %pJ requires %d leaf switches but is limited to %d",
			     plugin_type, __func__, job_ptr, leaf_switch_count,
			     job_ptr->req_switch);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			info("%s: %s: %pJ reached maximum node limit",
			     plugin_type, __func__, job_ptr);
			goto fini;
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    (!gres_per_job ||
		     gres_plugin_job_sched_test(job_ptr->gres_list,
					        job_ptr->job_id))) {
			/* Required nodes completely satisfied the request */
			error("%s: Scheduling anomaly for %pJ",
			      __func__, job_ptr);
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

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
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
		info("%s: %s: %pJ requires nodes not available on any switch",
		     plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	if ((req_nodes_bitmap || req2_nodes_bitmap) &&
	    (job_ptr->req_switch > 0) &&
	    (req_leaf_switch_count > job_ptr->req_switch) &&
	    (time_waiting < job_ptr->wait4switch)) {
		info("%s: %s: %pJ requires %d leaf switches but is limited to %d",
		     plugin_type, __func__, job_ptr, leaf_switch_count,
		     job_ptr->req_switch);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * If no resources have yet been  selected,
	 * then pick one leaf switch with the most available nodes.
	 */
	if (req_leaf_switch_count == 0) {
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
			req_leaf_switch_count = 1;
			switch_required[best_switch_inx] = 1;
		}
	}

	/*
	 * All required resources currently on one leaf switch. Determine if
	 * the entire job request can be satisfied using just that one switch.
	 */
	if (req_leaf_switch_count == 1) {
		best_cpu_cnt = 0;
		best_node_cnt = 0;
		FREE_NULL_LIST(best_gres);
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			i_first = bit_ffs(switch_node_bitmap[i]);
			if (i_first >= 0)
				i_last = bit_fls(switch_node_bitmap[i]);
			else
				i_last = -2;
			for (j = i_first; j <= i_last; j++) {
				if (!bit_test(switch_node_bitmap[i], j) ||
				    bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				best_cpu_cnt += avail_cpus;
				best_node_cnt++;
				if (gres_per_job) {
					gres_plugin_job_sched_consec(
						&best_gres, job_ptr->gres_list,
						avail_res_array[j]->sock_gres_list);
				}
			}
			break;
		}
		sufficient = (best_cpu_cnt >= rem_cpus) &&
			     _enough_nodes(best_node_cnt, rem_nodes,
				   min_nodes, req_nodes);
		if (sufficient && gres_per_job) {
			sufficient = gres_plugin_job_sched_sufficient(
						job_ptr->gres_list, best_gres);
		}
		if (sufficient && (i < switch_record_cnt)) {
			/* Complete request using this one leaf switch */
			for (j = i_first; j <= i_last; j++) {
				if (!bit_test(switch_node_bitmap[i], j) ||
				    bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[j]->
						sock_gres_list,
						avail_cpus);
				}
				bit_set(node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!gres_per_job ||
				     gres_plugin_job_sched_test(
							job_ptr->gres_list,
							job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
				if (max_nodes <= 0) {
					rc = SLURM_ERROR;
					info("%s: %s: %pJ reached maximum node limit",
					     plugin_type, __func__, job_ptr);
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
		if ((job_ptr->req_switch > 0) &&
		    (req_leaf_switch_count >= job_ptr->req_switch) &&
		    (time_waiting < job_ptr->wait4switch)) {
			info("%s: %s: %pJ requires %d leaf switches but is limited to %d",
			     plugin_type, __func__, job_ptr, leaf_switch_count,
			     job_ptr->req_switch);
			rc = SLURM_ERROR;
			goto fini;
		}
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			i_first = bit_ffs(switch_node_bitmap[i]);
			if (i_first >= 0)
				i_last = bit_fls(switch_node_bitmap[i]);
			else
				i_last = -2;
			for (j = i_first; j <= i_last; j++) {
				if (!bit_test(switch_node_bitmap[i], j) ||
				    bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[j]->
						sock_gres_list,
						avail_cpus);
				}
				bit_set(node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!gres_per_job ||
				     gres_plugin_job_sched_test(
							job_ptr->gres_list,
							job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
				if (max_nodes <= 0) {
					rc = SLURM_ERROR;
					info("%s: %s: %pJ reached maximum node limit",
					     plugin_type, __func__, job_ptr);
					goto fini;
				}
				break;	/* Move to next switch */
			}
		}
	}
	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!gres_per_job ||
	     gres_plugin_job_sched_test(job_ptr->gres_list, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	xfree(avail_cpu_per_node);
	xfree(switch_cpu_cnt);
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

/* Allocate resources to job using a minimal leaf switch count */
static int _eval_nodes_topo(struct job_record *job_ptr,
			    gres_mc_data_t *mc_ptr, bitstr_t *node_map,
			    bitstr_t **avail_core, uint32_t min_nodes,
			    uint32_t max_nodes, uint32_t req_nodes,
			    avail_res_t **avail_res_array, uint16_t cr_type,
			    bool prefer_alloc_nodes, bool first_pass)
{
	int       *switch_cpu_cnt = NULL;	/* total CPUs on switch */
	List      *switch_gres = NULL;		/* available GRES on switch */
	bitstr_t **switch_node_bitmap = NULL;	/* nodes on this switch */
	int       *switch_node_cnt = NULL;	/* total nodes on switch */
	int       *switch_required = NULL;	/* set if has required node */
	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t  *req_nodes_bitmap   = NULL;	/* required node bitmap */
	bitstr_t  *req2_nodes_bitmap  = NULL;	/* required+lowest prio nodes */
	bitstr_t  *best_nodes_bitmap  = NULL;	/* required+low prio nodes */
	int i, i_first, i_last, j, rc = SLURM_SUCCESS;
	int best_cpu_cnt = 0, best_node_cnt = 0, req_node_cnt = 0;
	List best_gres = NULL;
	struct switch_record *switch_ptr;
	List node_weight_list = NULL;
	topo_weight_info_t *nw = NULL;
	ListIterator iter;
	struct node_record *node_ptr;
	uint16_t avail_cpus = 0;
	int64_t rem_max_cpus;
	int rem_cpus, rem_nodes; /* remaining resources desired */
	int min_rem_nodes;	/* remaining resources desired */
	int total_cpus = 0;	/* #CPUs allocated to job */
	bool enforce_binding = false;
	struct job_details *details_ptr = job_ptr->details;
	bool gres_per_job, sufficient = false;
	uint16_t *avail_cpu_per_node = NULL;
	int64_t time_waiting = 0;
	int leaf_switch_count = 0, req_leaf_switch_count = 0;
	int top_switch_inx = -1;
	int prev_rem_nodes;

	if (job_ptr->req_switch) {
		time_t     time_now;
		time_now = time(NULL);
		if (job_ptr->wait4switch_start == 0)
			job_ptr->wait4switch_start = time_now;
		time_waiting = time_now - job_ptr->wait4switch_start;
	}

	if (job_ptr->gres_list && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	rem_max_cpus = details_ptr->max_cpus;
	min_rem_nodes = min_nodes;
	if ((gres_per_job = gres_plugin_job_sched_init(job_ptr->gres_list)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

	/* Validate availability of required nodes */
	if (job_ptr->details->req_node_bitmap) {
		if (!bit_super_set(job_ptr->details->req_node_bitmap,
				   node_map)) {
			info("%s: %s: %pJ requires nodes which are not currently available",
			      plugin_type, __func__, job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}

		req_node_cnt = bit_set_count(job_ptr->details->req_node_bitmap);
		if (req_node_cnt == 0) {
			info("%s: %s: %pJ required node list has no nodes",
			      plugin_type, __func__, job_ptr);
			rc = SLURM_ERROR;
			goto fini;
		}
		if (req_node_cnt > max_nodes) {
			info("%s: %s: %pJ requires more nodes than currently available (%u>%u)",
			      plugin_type, __func__, job_ptr, req_node_cnt,
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
	i_first = bit_ffs(node_map);
	if (i_first == -1) {
		debug("%s: %s: %pJ node_map is empty",
		      plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	i_last = bit_fls(node_map);
	avail_cpu_per_node = xmalloc(sizeof(uint16_t) * select_node_cnt);
	node_weight_list = list_create(_topo_weight_free);
	for (i = i_first; i <= i_last; i++) {
		topo_weight_info_t nw_static;
		if (!bit_test(node_map, i))
			continue;
		if (req_nodes_bitmap && bit_test(req_nodes_bitmap, i)) {
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if (avail_cpus == 0) {
				debug2("%s: %s: %pJ insufficient resources on required node",
				       plugin_type, __func__, job_ptr);
				rc = SLURM_ERROR;
				goto fini;
			}
			avail_cpu_per_node[i] = avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
		}

		node_ptr = node_record_table_ptr + i;
		nw_static.weight = node_ptr->sched_weight;
		nw = list_find_first(node_weight_list, _topo_weight_find,
				     &nw_static);
		if (!nw) {	/* New node weight to add */
			nw = xmalloc(sizeof(topo_weight_info_t));
			nw->node_bitmap = bit_alloc(select_node_cnt);
			nw->weight = node_ptr->sched_weight;
			list_append(node_weight_list, nw);
		}
		bit_set(nw->node_bitmap, i);
		nw->node_cnt++;
	}

	if (req_nodes_bitmap) {
		bit_and(node_map, req_nodes_bitmap);
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
			/* Required nodes completely satisfied the request */
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			info("%s: %s: %pJ requires nodes exceed maximum node limit",
			     plugin_type, __func__, job_ptr);
			goto fini;
		}
	} else {
		bit_clear_all(node_map);
	}

	list_sort(node_weight_list, _topo_weight_sort);
	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		(void) list_for_each(node_weight_list, _topo_weight_log, NULL);

	/*
	 * Identify the highest level switch to be used.
	 * Note that nodes can be on multiple non-overlapping switches.
	 */
	switch_cpu_cnt     = xmalloc(sizeof(int)        * switch_record_cnt);
	switch_gres        = xmalloc(sizeof(List)       * switch_record_cnt);
	switch_node_bitmap = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switch_node_cnt    = xmalloc(sizeof(int)        * switch_record_cnt);
	switch_required    = xmalloc(sizeof(int)        * switch_record_cnt);

	if (!req_nodes_bitmap)
		nw = list_peek(node_weight_list);
	for (i = 0, switch_ptr = switch_record_table; i < switch_record_cnt;
	     i++, switch_ptr++) {
		switch_node_bitmap[i] = bit_copy(switch_ptr->node_bitmap);
		if (req_nodes_bitmap &&
		    bit_overlap(req_nodes_bitmap, switch_node_bitmap[i])) {
			switch_required[i] = 1;
			if (switch_record_table[i].level == 0) {
				leaf_switch_count++;
				req_leaf_switch_count++;
			}
			if ((top_switch_inx == -1) ||
			    (switch_record_table[i].level >
			     switch_record_table[top_switch_inx].level)) {
				top_switch_inx = i;
			}
		}
		if (!req_nodes_bitmap &&
		    bit_overlap(nw->node_bitmap, switch_node_bitmap[i])) {
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
		error("%s: %s: %pJ unable to identify top level switch",
		       plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Check that all specificly required nodes are on shared network */
	if (req_nodes_bitmap &&
	    !bit_super_set(req_nodes_bitmap,
			   switch_node_bitmap[top_switch_inx])) {
		rc = SLURM_ERROR;
		info("%s: %s: %pJ requires nodes that do not have shared network",
		     plugin_type, __func__, job_ptr);
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

	/*
	 * Identify the best set of nodes (i.e. nodes with the lowest weight,
	 * in addition to the required nodes) that can be used to satisfy the
	 * job request. All nodes must be on a common top-level switch. The
	 * logic here adds groups of nodes, all with the same weight, so we
	 * usually identify more nodes than required to satisfy the request.
	 * Later logic selects from those nodes to get the best topology.
	 */
	best_nodes_bitmap = bit_alloc(select_node_cnt);
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
		i_first = bit_ffs(nw->node_bitmap);
		if (i_first == -1)
			continue;
		i_last = bit_fls(nw->node_bitmap);
		for (i = i_first; i <= i_last; i++) {
			if (avail_cpu_per_node[i])
				continue;	/* Required node */
			if (!bit_test(nw->node_bitmap, i) ||
			    !bit_test(switch_node_bitmap[top_switch_inx], i))
				continue;
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			if (avail_cpus == 0) {
				bit_clear(nw->node_bitmap, i);
				continue;
			}
			bit_set(best_nodes_bitmap, i);
			avail_cpu_per_node[i] = avail_cpus;
			best_cpu_cnt += avail_cpus;
			best_node_cnt++;
			if (gres_per_job) {
				gres_plugin_job_sched_consec(
					&best_gres, job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list);
			}
		}

		sufficient = (best_cpu_cnt >= rem_cpus) &&
			     _enough_nodes(best_node_cnt, rem_nodes,
					   min_nodes, req_nodes);
		if (sufficient && gres_per_job) {
			sufficient = gres_plugin_job_sched_sufficient(
					job_ptr->gres_list, best_gres);
		}
	}
	list_iterator_destroy(iter);

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		char *gres_str = NULL, *gres_print = "";
		char *node_names;
		if (req_nodes_bitmap) {
			node_names = bitmap2node_name(req_nodes_bitmap);
			info("%s: Required nodes:%s", __func__, node_names);
			xfree(node_names);
		}
		node_names = bitmap2node_name(best_nodes_bitmap);
		if (gres_per_job) {
			gres_str = gres_plugin_job_sched_str(best_gres,
							job_ptr->gres_list);
			if (gres_str)
				gres_print = gres_str;
		}
		info("%s: Best nodes:%s node_cnt:%d cpu_cnt:%d %s", __func__,
		     node_names, best_node_cnt, best_cpu_cnt, gres_print);
		xfree(node_names);
		xfree(gres_str);
	}
	if (!sufficient) {
		info("%s: %s: insufficient resources currently available for %pJ",
		      plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}

	/*
	 * Add lowest weight nodes. Treat similar to required nodes for the job.
	 * Job will still need to add some higher weight nodes later.
	 */
	if (req2_nodes_bitmap) {
		i_first = bit_ffs(req2_nodes_bitmap);
		if (i_first >= 0)
			i_last = bit_fls(req2_nodes_bitmap);
		else
			i_last = -2;
		for (i = i_first; ((i <= i_last) && (max_nodes > 0)); i++) {
			if (!bit_test(req2_nodes_bitmap, i))
				continue;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			avail_cpus = avail_cpu_per_node[i];
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
		}

		for (i = 0, switch_ptr = switch_record_table;
		     i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_required[i])
				continue;
			if (bit_overlap(req2_nodes_bitmap,
					switch_node_bitmap[i])) {
				switch_required[i] = 1;
				if (switch_record_table[i].level == 0) {
					leaf_switch_count++;
					req_leaf_switch_count++;
				}
			}
		}
		bit_or(node_map, req2_nodes_bitmap);
		if ((job_ptr->req_switch > 0) &&
		    (req_leaf_switch_count > job_ptr->req_switch) &&
		    (time_waiting < job_ptr->wait4switch)) {
			info("%s: %s: %pJ requires %d leaf switches but is limited to %d",
			     plugin_type, __func__, job_ptr, leaf_switch_count,
			     job_ptr->req_switch);
			rc = SLURM_ERROR;
			goto fini;
		}

		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    (!gres_per_job ||
		     gres_plugin_job_sched_test(job_ptr->gres_list,
					        job_ptr->job_id))) {
			/* Required nodes completely satisfied the request */
			error("%s: Scheduling anomaly for %pJ",
			      __func__, job_ptr);
			rc = SLURM_SUCCESS;
			goto fini;
		}
		if (max_nodes <= 0) {
			rc = SLURM_ERROR;
			info("%s: %s: %pJ reached maximum node limit",
			     plugin_type, __func__, job_ptr);
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

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
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
		info("%s: %s: %pJ requires nodes not available on any switch",
		     plugin_type, __func__, job_ptr);
		rc = SLURM_ERROR;
		goto fini;
	}
	if ((req_nodes_bitmap || req2_nodes_bitmap) &&
	    (job_ptr->req_switch > 0) &&
	    (req_leaf_switch_count > job_ptr->req_switch) &&
	    (time_waiting < job_ptr->wait4switch)) {
		info("%s: %s: %pJ requires %d leaf switches but is limited to %d",
		     plugin_type, __func__, job_ptr, leaf_switch_count,
		     job_ptr->req_switch);
		rc = SLURM_ERROR;
		goto fini;
	}

	/* Add additional resources for already required leaf switches */
	if (req_leaf_switch_count) {
		for (i = 0; i < switch_record_cnt; i++) {
			if (!switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			i_first = bit_ffs(switch_node_bitmap[i]);
			if (i_first >= 0)
				i_last = bit_fls(switch_node_bitmap[i]);
			else
				i_last = -2;
			for (j = i_first; j <= i_last; j++) {
				if (!bit_test(switch_node_bitmap[i], j) ||
				    bit_test(node_map, j) ||
				    !avail_cpu_per_node[j])
					continue;
				avail_cpus = avail_cpu_per_node[j];
				rem_nodes--;
				min_rem_nodes--;
				max_nodes--;
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[j]->
						sock_gres_list,
						avail_cpus);
				}
				bit_set(node_map, j);
				if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
				    (!gres_per_job ||
				     gres_plugin_job_sched_test(
							job_ptr->gres_list,
							job_ptr->job_id))) {
					rc = SLURM_SUCCESS;
					goto fini;
				}
			}
		}
	}

	/* Add additional resources as required from additional leaf switches */
	prev_rem_nodes = rem_nodes + 1;
	while (1) {
		if (prev_rem_nodes == rem_nodes)
			break; 	/* Stalled */
		prev_rem_nodes = rem_nodes;

		if ((job_ptr->req_switch > 0) &&
		    (req_leaf_switch_count >= job_ptr->req_switch) &&
		    (time_waiting < job_ptr->wait4switch)) {
			info("%s: %s: %pJ requires %d leaf switches but is limited to %d",
			     plugin_type, __func__, job_ptr, leaf_switch_count,
			     job_ptr->req_switch);
			rc = SLURM_ERROR;
			goto fini;
		}
		top_switch_inx = -1;
		for (i = 0; i < switch_record_cnt; i++) {
			if (switch_required[i] || !switch_node_bitmap[i] ||
			    (switch_record_table[i].level != 0))
				continue;
			if (switch_node_cnt[i] &&
			    ((top_switch_inx == -1) ||
			     (switch_node_cnt[i] >
			      switch_node_cnt[top_switch_inx])))
				top_switch_inx = i;
		}
		if (top_switch_inx == -1)
			break;

		/*
		 * NOTE: Ideally we would add nodes in order of resource
		 * availability rather than in order of bitmap position, but
		 * that would add even more complexity and overhead.
		 */
		i_first = bit_ffs(switch_node_bitmap[top_switch_inx]);
		if (i_first >= 0)
			i_last = bit_fls(switch_node_bitmap[top_switch_inx]);
		else
			i_last = -2;
		for (i = i_first; ((i <= i_last) && (max_nodes > 0)); i++) {
			if (!bit_test(switch_node_bitmap[top_switch_inx], i) ||
			    bit_test(node_map, i) ||
			    !avail_cpu_per_node[i])
				continue;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			avail_cpus = avail_cpu_per_node[i];
			total_cpus += avail_cpus;
			rem_cpus   -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
			bit_set(node_map, i);
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    (!gres_per_job ||
			     gres_plugin_job_sched_test(job_ptr->gres_list,
							job_ptr->job_id))) {
				rc = SLURM_SUCCESS;
				goto fini;
			}
		}
		switch_node_cnt[top_switch_inx] = 0;	/* Used all */
	}
	if ((min_rem_nodes <= 0) && (rem_cpus <= 0) &&
	    (!gres_per_job ||
	     gres_plugin_job_sched_test(job_ptr->gres_list, job_ptr->job_id))) {
		rc = SLURM_SUCCESS;
		goto fini;
	}
	rc = SLURM_ERROR;

fini:	FREE_NULL_LIST(best_gres);
	FREE_NULL_LIST(node_weight_list);
	FREE_NULL_BITMAP(avail_nodes_bitmap);
	FREE_NULL_BITMAP(req_nodes_bitmap);
	FREE_NULL_BITMAP(req2_nodes_bitmap);
	FREE_NULL_BITMAP(best_nodes_bitmap);
	xfree(avail_cpu_per_node);
	xfree(switch_cpu_cnt);
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

static int _eval_nodes_lln(struct job_record *job_ptr,
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
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	struct node_record *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	uint16_t *avail_cpu_per_node = NULL;
	bool enforce_binding = false;

	if (job_ptr->gres_list && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	rem_max_cpus = details_ptr->max_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_plugin_job_sched_init(job_ptr->gres_list)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

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
			node_ptr = node_record_table_ptr + i;
			if (!bit_test(node_map, i)) {
				debug("%pJ required node %s not available",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if ((avail_cpus > 0) && (max_nodes > 0)) {
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				/* leaving bitmap set, decr max limit */
				max_nodes--;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list, avail_cpus);
				}
			} else {	/* node not selected (yet) */
				debug("%pJ required node %s not available",
				      job_ptr, node_ptr->name);
				goto fini;
			}
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
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
	avail_cpu_per_node = xmalloc(sizeof(uint16_t) * select_node_cnt);
	node_weight_list = _build_node_weight_list(orig_node_map);
	iter = list_iterator_create(node_weight_list);
	while (!all_done && (nwt = (node_weight_type *) list_next(iter))) {
		int last_max_cpu_cnt = -1;
		while (!all_done) {
			int max_cpu_idx = -1;
			for (i = i_start; i <= i_end; i++) {
				/* Node not available or already selected */
				if (!bit_test(nwt->node_bitmap, i) ||
				    bit_test(node_map, i))
					continue;
				_select_cores(job_ptr, mc_ptr, enforce_binding,
					      i, &avail_cpus, max_nodes,
					      min_rem_nodes, avail_core,
					      avail_res_array, first_pass);
				_cpus_to_use(&avail_cpus, rem_max_cpus,
					     min_rem_nodes, details_ptr,
					     avail_res_array[i], i, cr_type);
				if (avail_cpus == 0)
					continue;
				avail_cpu_per_node[i] = avail_cpus;
				if ((max_cpu_idx == -1) ||
				    (avail_cpu_per_node[max_cpu_idx] <
				     avail_cpu_per_node[i])) {
					max_cpu_idx = i;
					if (avail_cpu_per_node[max_cpu_idx] ==
					    last_max_cpu_cnt)
						break;
				}
			}
			if ((max_cpu_idx == -1) ||
			    (avail_cpu_per_node[max_cpu_idx] == 0)) {
				/* No more usable nodes left, get next weight */
				break;
			}
			i = max_cpu_idx;
			avail_cpus = avail_cpu_per_node[i];
			last_max_cpu_cnt = avail_cpus;
			total_cpus += avail_cpus;
			if ((details_ptr->max_cpus != NO_VAL) &&
			    (total_cpus > details_ptr->max_cpus)) {
				debug2("%s: %s: %pJ can't use node %d without exceeding job limit",
				       plugin_type, __func__, job_ptr, i);
				bit_clear(nwt->node_bitmap, i);
				total_cpus -= avail_cpus;
				continue;
			}
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			bit_set(node_map, i);
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_plugin_job_sched_test(job_ptr->gres_list,
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
		   !gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	bit_free(orig_node_map);
	xfree(avail_cpu_per_node);
	return error_code;
}

/*
 * A variation of _eval_nodes() to select resources at the end of the node
 * list to reduce fragmentation
 */
static int _eval_nodes_serial(struct job_record *job_ptr,
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
	struct job_details *details_ptr = job_ptr->details;
	bitstr_t *req_map = details_ptr->req_node_bitmap;
	bitstr_t *orig_node_map = bit_copy(node_map);
	bool all_done = false, gres_per_job;
	uint16_t avail_cpus = 0;
	struct node_record *node_ptr;
	List node_weight_list = NULL;
	node_weight_type *nwt;
	ListIterator iter;
	bool enforce_binding = false;

	if (job_ptr->gres_list && (job_ptr->bit_flags & GRES_ENFORCE_BIND))
		enforce_binding = true;
	rem_cpus = details_ptr->min_cpus;
	rem_max_cpus = details_ptr->max_cpus;
	min_rem_nodes = min_nodes;
	if ((details_ptr->num_tasks != NO_VAL) &&
	    (details_ptr->num_tasks != 0))
		max_nodes = MIN(max_nodes, details_ptr->num_tasks);
	if ((gres_per_job = gres_plugin_job_sched_init(job_ptr->gres_list)))
		rem_nodes = MIN(min_nodes, req_nodes);
	else
		rem_nodes = MAX(min_nodes, req_nodes);

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
			node_ptr = node_record_table_ptr + i;
			if (!bit_test(node_map, i)) {
				debug("%pJ required node %s not available",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			if (!avail_res_array[i] ||
			    !avail_res_array[i]->avail_cpus) {
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
			_select_cores(job_ptr, mc_ptr, enforce_binding, i,
				      &avail_cpus, max_nodes, min_rem_nodes,
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus, min_rem_nodes,
				     details_ptr, avail_res_array[i], i,
				     cr_type);
			if ((avail_cpus > 0) && (max_nodes > 0)) {
				total_cpus += avail_cpus;
				rem_cpus   -= avail_cpus;
				rem_max_cpus -= avail_cpus;
				rem_nodes--;
				min_rem_nodes--;
				/* leaving bitmap set, decr max limit */
				max_nodes--;
				if (gres_per_job) {
					gres_plugin_job_sched_add(
						job_ptr->gres_list,
						avail_res_array[i]->
						sock_gres_list, avail_cpus);
				}
			} else {	/* node not selected (yet) */
				debug("%pJ required node %s lacks available resources",
				      job_ptr, node_ptr->name);
				goto fini;
			}
		}
		if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
		    gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
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
				      avail_core, avail_res_array, first_pass);
			_cpus_to_use(&avail_cpus, rem_max_cpus,
				     min_rem_nodes, details_ptr,
				     avail_res_array[i], i, cr_type);
			if (avail_cpus == 0)
				continue;
			total_cpus += avail_cpus;
			if ((details_ptr->max_cpus != NO_VAL) &&
			    (total_cpus > details_ptr->max_cpus)) {
				debug2("%s: %s: %pJ can't use node %d without exceeding job limit",
				       plugin_type, __func__, job_ptr, i);
				total_cpus -= avail_cpus;
				continue;
			}
			rem_cpus -= avail_cpus;
			rem_max_cpus -= avail_cpus;
			rem_nodes--;
			min_rem_nodes--;
			max_nodes--;
			bit_set(node_map, i);
			if (gres_per_job) {
				gres_plugin_job_sched_add(job_ptr->gres_list,
					avail_res_array[i]->sock_gres_list,
					avail_cpus);
			}
			if ((rem_nodes <= 0) && (rem_cpus <= 0) &&
			    gres_plugin_job_sched_test(job_ptr->gres_list,
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
		   !gres_plugin_job_sched_test(job_ptr->gres_list,
					       job_ptr->job_id)) {
		bit_clear_all(node_map);
		error_code = SLURM_ERROR;
	} else {
		error_code = SLURM_SUCCESS;
	}

fini:	FREE_NULL_LIST(node_weight_list);
	bit_free(orig_node_map);
	return error_code;

}

/*
 * This is an intermediary step between _select_nodes() and _eval_nodes()
 * to tackle the knapsack problem. This code incrementally removes nodes
 * with low CPU counts for the job and re-evaluates each result.
 *
 * RET SLURM_SUCCESS or an error code
 */
extern int choose_nodes(struct job_record *job_ptr, bitstr_t *node_map,
			bitstr_t **avail_core, uint32_t min_nodes,
			uint32_t max_nodes, uint32_t req_nodes,
			avail_res_t **avail_res_array, uint16_t cr_type,
			bool prefer_alloc_nodes, gres_mc_data_t *tres_mc_ptr)
{
	int i, i_first, i_last;
	int count, ec, most_res = 0, rem_nodes, node_cnt = 0;
	bitstr_t *orig_node_map, *req_node_map = NULL;
	bitstr_t **orig_core_array;

	if (job_ptr->details->req_node_bitmap)
		req_node_map = job_ptr->details->req_node_bitmap;

	/* clear nodes from the bitmap that don't have available resources */
	i_first = bit_ffs(node_map);
	if (i_first >= 0)
		i_last = bit_fls(node_map);
	else
		i_last = i_first - 1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(node_map, i))
			continue;
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
		} else {
			node_cnt++;
		}
	}

	if ((job_ptr->details->num_tasks > 1) &&
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
	for (i = 0; i < select_node_cnt; i++) {
		if (avail_res_array[i]) {
			most_res = MAX(most_res,
				       avail_res_array[i]->avail_res_cnt);
		}
	}

	for (count = 1; count < most_res; count++) {
		int nochange = 1;
		bit_or(node_map, orig_node_map);
		core_array_or(avail_core, orig_core_array);
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(node_map, i))
				continue;
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

fini:	if ((ec == SLURM_SUCCESS) && job_ptr->gres_list && orig_core_array) {
		/*
		 * Update available CPU count for any removed cores.
		 * Cores are only removed for jobs with GRES to enforce binding.
		 */
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(node_map, i)||
			    !orig_core_array[i] || !avail_core[i])
				continue;
			count = bit_set_count(orig_core_array[i]) -
				bit_set_count(avail_core[i]);
			count *= select_node_record[i].vpus;
			if (count > avail_res_array[i]->avail_cpus) {
				error("%s: %s: avail_cpus underflow for %pJ",
				      plugin_type, __func__, job_ptr);
				avail_res_array[i]->avail_cpus = 0;
			} else {
				avail_res_array[i]->avail_cpus -= count;
			}
		}
	}
	FREE_NULL_BITMAP(orig_node_map);
	free_core_array(&orig_core_array);
	return ec;
}

/*
 * can_job_run_on_node - Given the job requirements, determine which
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
 * IN test_only     - ignore allocated memory check
 * IN: part_core_map - per-node bitmap of cores allocated to jobs of this
 *                     partition or NULL if don't care
 * RET Available resources. Call _array() to release memory.
 *
 * NOTE: The returned cpu_count may be less than the number of set bits in
 *       core_map for the given node. The cr_dist functions will determine
 *       which bits to de-select from the core_map to match the cpu_count.
 */
extern avail_res_t *can_job_run_on_node(struct job_record *job_ptr,
					bitstr_t **core_map,
					const uint32_t node_i,
					uint32_t s_p_n,
					struct node_use_record *node_usage,
					uint16_t cr_type, bool test_only,
					bitstr_t **part_core_map)
{
	uint16_t cpus = 0;
	uint64_t avail_mem = NO_VAL64, req_mem;
	int cpu_alloc_size, i, rc;
	struct node_record *node_ptr = node_record_table_ptr + node_i;
	List node_gres_list;
	bitstr_t *part_core_map_ptr = NULL, *req_sock_map = NULL;
	avail_res_t *avail_res = NULL;
	List sock_gres_list = NULL;
	bool enforce_binding = false;
	uint16_t min_cpus_per_node, ntasks_per_node = 1;

	if (((job_ptr->bit_flags & BACKFILL_TEST) == 0) &&
	    !test_only && IS_NODE_COMPLETING(node_ptr)) {
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

	if (job_ptr->gres_list) {
		/* Identify available GRES and adjacent cores */
		if (job_ptr->bit_flags & GRES_ENFORCE_BIND)
			enforce_binding = true;
		if (!core_map[node_i]) {
			core_map[node_i] = bit_alloc(
					select_node_record[node_i].tot_cores);
			bit_set_all(core_map[node_i]);
		}
		sock_gres_list = gres_plugin_job_test2(
					job_ptr->gres_list, node_gres_list,
					test_only, core_map[node_i],
					select_node_record[node_i].tot_sockets,
					select_node_record[node_i].cores,
					job_ptr->job_id, node_ptr->name,
					enforce_binding, s_p_n, &req_sock_map,
					job_ptr->user_id, node_i);
		if (!sock_gres_list) {	/* GRES requirement fail */
#if _DEBUG
			info("Test fail on node %d: gres_plugin_job_test2",
			     node_i);
#endif
			return NULL;
		}
	}

	/* Identify available CPUs */
	if (cr_type & CR_CORE) {
		/* cpu_alloc_size = # of CPUs per core */
		cpu_alloc_size = select_node_record[node_i].vpus;
		avail_res = common_allocate_cores(job_ptr, core_map[node_i],
						  part_core_map_ptr, node_i,
						  &cpu_alloc_size, false,
						  req_sock_map);

	} else if (cr_type & CR_SOCKET) {
		/* cpu_alloc_size = # of CPUs per socket */
		cpu_alloc_size = select_node_record[node_i].cores *
				 select_node_record[node_i].vpus;
		avail_res = common_allocate_sockets(job_ptr, core_map[node_i],
						    part_core_map_ptr, node_i,
						    &cpu_alloc_size,
						    req_sock_map);
	} else {
		/* cpu_alloc_size = 1 individual CPU */
		cpu_alloc_size = 1;
		avail_res = common_allocate_cores(job_ptr, core_map[node_i],
						  part_core_map_ptr, node_i,
						  &cpu_alloc_size, true,
						  req_sock_map);
	}
	FREE_NULL_BITMAP(req_sock_map);
	if (!avail_res || (avail_res->max_cpus == 0)) {
		common_free_avail_res(avail_res);
#if _DEBUG
		info("Test fail on node %d: _allocate_cores/sockets",
		     node_i);
#endif
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
	} else if (job_ptr->details->max_nodes) {
		ntasks_per_node = (job_ptr->details->num_tasks +
				   job_ptr->details->max_nodes - 1) /
				  job_ptr->details->max_nodes;
	}
	min_cpus_per_node = ntasks_per_node * job_ptr->details->cpus_per_task;
	if (avail_res->max_cpus < min_cpus_per_node) {
#if _DEBUG
		info("Test fail on node %d: max_cpus < min_cpus_per_node (%u < %u)",
		     node_i, avail_res->max_cpus, min_cpus_per_node);
#endif
		FREE_NULL_LIST(sock_gres_list);
		common_free_avail_res(avail_res);
		return NULL;
	}

	if (cr_type & CR_MEMORY) {
		avail_mem = select_node_record[node_i].real_memory -
			    select_node_record[node_i].mem_spec_limit;
		if (!test_only)
			avail_mem -= node_usage[node_i].alloc_memory;
	}

	if (sock_gres_list) {
		uint16_t near_gpu_cnt = 0;
		avail_res->sock_gres_list = sock_gres_list;
		/* Disable GRES that can't be used with remaining cores */
		rc = gres_plugin_job_core_filter2(
					sock_gres_list, avail_mem,
					avail_res->max_cpus,
					enforce_binding, core_map[node_i],
					select_node_record[node_i].tot_sockets,
					select_node_record[node_i].cores,
					select_node_record[node_i].vpus,
					s_p_n,
					job_ptr->details->ntasks_per_node,
					&avail_res->avail_gpus, &near_gpu_cnt);
		if (rc != 0) {
#if _DEBUG
			info("Test fail on node %d: gres_plugin_job_core_filter2",
			     node_i);
#endif
			common_free_avail_res(avail_res);
			return NULL;
		}

		/* Favor nodes with more co-located GPUs */
		node_ptr->sched_weight =
			(node_ptr->sched_weight & 0xffffffffffffff00) |
			(0xff - near_gpu_cnt);
	}

	for (i = 0; i < avail_res->sock_cnt; i++)
		cpus += avail_res->avail_cores_per_sock[i];
	cpus *= avail_res->vpus;
	cpus -= avail_res->spec_threads;

	if (cr_type & CR_MEMORY) {
		/*
		 * Memory Check: check pn_min_memory to see if:
		 *          - this node has enough memory (MEM_PER_CPU == 0)
		 *          - there are enough free_cores (MEM_PER_CPU == 1)
		 */
		req_mem   = job_ptr->details->pn_min_memory & ~MEM_PER_CPU;
		if (job_ptr->details->pn_min_memory & MEM_PER_CPU) {
			/* memory is per-CPU */
			if (!(cr_type & CR_CPU) && job_ptr->details->mc_ptr &&
			    (job_ptr->details->mc_ptr->ntasks_per_core == 1) &&
			    (job_ptr->details->cpus_per_task == 1)) {
				/*
				 * In this scenario, CPUs represents cores and
				 * the CPU/core count will be inflated later on
				 * to include all of the threads on a core. So
				 * we need to compare apples to apples and only
				 * remove 1 CPU/core at a time.
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

	if (cpus == 0) {
#if _DEBUG
		info("Test fail on node %d: cpus == 0", node_i);
#endif
		bit_clear_all(core_map[node_i]);
	}

	if (select_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("%s: %s: %u CPUs on %s(state:%d), mem %"PRIu64"/%"PRIu64,
		     plugin_type, __func__, cpus,
		     select_node_record[node_i].node_ptr->name,
		     node_usage[node_i].node_state,
		     node_usage[node_i].alloc_memory,
		     select_node_record[node_i].real_memory);
	}

	avail_res->avail_cpus = cpus;
	_avail_res_log(avail_res, node_ptr->name);

	return avail_res;
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
extern int verify_node_state(struct part_res_record *cr_part_ptr,
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

	if (!(job_ptr->bit_flags & JOB_MEM_SET) &&
	    ((min_mem = gres_plugin_job_mem_max(job_ptr->gres_list)))) {
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
					select_node_record[i].node_ptr->name,
					free_mem, min_mem);
				goto clear_bit;
			}
		} else if (cr_type & CR_MEMORY) {   /* --mem=0 for all memory */
			if (node_usage[i].alloc_memory) {
				debug3("%s: %s: node %s mem in use %"PRIu64,
					plugin_type, __func__,
					select_node_record[i].node_ptr->name,
					node_usage[i].alloc_memory);
				goto clear_bit;
			}
		}

		/* Exclude nodes with reserved cores */
		if ((job_ptr->details->whole_node == 1) && exc_cores &&
		    exc_cores[i] && (bit_ffs(exc_cores[i]) != -1)) {
			debug3("%s: %s: node %s exclusive", plugin_type,
			       __func__, select_node_record[i].node_ptr->name);
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
