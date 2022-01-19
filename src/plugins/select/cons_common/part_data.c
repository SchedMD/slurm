/*****************************************************************************\
 *  part_data.c - Functions for structures dealing with partitions unique
 *                to the select plugin.
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

part_res_record_t *select_part_record = NULL;

typedef struct {
	int jstart;
	struct job_resources *tmpjobs;
} sort_support_t;

/* Sort jobs by first set core, then size (CPU count) */
static int _compare_support(const void *v1, const void *v2)
{
	sort_support_t *s1, *s2;

	s1 = (sort_support_t *) v1;
	s2 = (sort_support_t *) v2;

	if ((s1->jstart > s2->jstart) ||
	    ((s1->jstart == s2->jstart) &&
	     (s1->tmpjobs->ncpus > s2->tmpjobs->ncpus)))
		return 1;

	return 0;
}

static int _sort_part_prio(void *x, void *y)
{
	part_res_record_t *part1 = *(part_res_record_t **) x;
	part_res_record_t *part2 = *(part_res_record_t **) y;

	if (part1->part_ptr->priority_tier > part2->part_ptr->priority_tier)
		return -1;
	if (part1->part_ptr->priority_tier < part2->part_ptr->priority_tier)
		return 1;
	return 0;
}

/* helper script for part_data_sort_rescommon_sort_part_rows() */
static void _swap_rows(part_row_data_t *a, part_row_data_t *b)
{
	part_row_data_t tmprow;

	memcpy(&tmprow, a, sizeof(part_row_data_t));
	memcpy(a, b, sizeof(part_row_data_t));
	memcpy(b, &tmprow, sizeof(part_row_data_t));
}

static void _reset_part_row_bitmap(part_row_data_t *r_ptr)
{
	clear_core_array(r_ptr->row_bitmap);
	r_ptr->row_set_count = 0;
}

/*
 * Add job resource use to the partition data structure
 */
extern void part_data_add_job_to_row(struct job_resources *job,
				     part_row_data_t *r_ptr)
{
	/* add the job to the row_bitmap */
	if (r_ptr->row_bitmap && (r_ptr->num_jobs == 0)) {
		/* if no jobs, clear the existing row_bitmap first */
		_reset_part_row_bitmap(r_ptr);
	}

	job_res_add_cores(job, r_ptr);

	/*  add the job to the job_list */
	if (r_ptr->num_jobs >= r_ptr->job_list_size) {
		r_ptr->job_list_size += 8;
		xrealloc(r_ptr->job_list, r_ptr->job_list_size *
			 sizeof(struct job_resources *));
	}
	r_ptr->job_list[r_ptr->num_jobs++] = job;
}

/*
 * part_data_build_row_bitmaps: A job has been removed from the given partition,
 *                     so the row_bitmap(s) need to be reconstructed.
 *                     Optimize the jobs into the least number of rows,
 *                     and make the lower rows as dense as possible.
 *
 * IN p_ptr - the partition that has jobs to be optimized
 * IN job_ptr - pointer to single job removed, pass NULL to completely rebuild
 */
extern void part_data_build_row_bitmaps(part_res_record_t *p_ptr,
					job_record_t *job_ptr)
{
	uint32_t i, j, num_jobs;
	int x;
	part_row_data_t *this_row, *orig_row;
	sort_support_t *ss;

	if (!p_ptr->row)
		return;

	if (p_ptr->num_rows == 1) {
		this_row = p_ptr->row;
		if (this_row->num_jobs == 0) {
			_reset_part_row_bitmap(this_row);
		} else {
			if (job_ptr) { /* just remove the job */
				xassert(job_ptr->job_resrcs);
				job_res_rm_cores(job_ptr->job_resrcs,
						 this_row);
			} else { /* totally rebuild the bitmap */
				_reset_part_row_bitmap(this_row);
				for (j = 0; j < this_row->num_jobs; j++) {
					job_res_add_cores(
						this_row->job_list[j],
						this_row);
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
			_reset_part_row_bitmap(&p_ptr->row[i]);
		return;
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("DEBUG: (before):");
		part_data_dump_res(p_ptr);
	}
	debug3("reshuffling %u jobs", num_jobs);

	/* make a copy, in case we cannot do better than this */
	orig_row = part_data_dup_row(p_ptr->row, p_ptr->num_rows);
	if (orig_row == NULL)
		return;

	/* create a master job list and clear out ALL row data */
	ss = xcalloc(num_jobs, sizeof(sort_support_t));
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
		_reset_part_row_bitmap(&p_ptr->row[i]);
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
	qsort(ss, num_jobs, sizeof(sort_support_t), _compare_support);
	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
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
			if (job_res_fit_in_row(
				    ss[j].tmpjobs, &(p_ptr->row[i]))) {
				/* job fits in row, so add it */
				part_data_add_job_to_row(
					ss[j].tmpjobs, &(p_ptr->row[i]));
				ss[j].tmpjobs = NULL;
				break;
			}
		}
		/* job should have been added, so shuffle the rows */
		part_data_sort_res(p_ptr);
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
		debug3("dangling job found");

		if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			info("DEBUG: (post-algorithm):");
			part_data_dump_res(p_ptr);
		}

		part_data_destroy_row(p_ptr->row, p_ptr->num_rows);
		p_ptr->row = orig_row;
		orig_row = NULL;

		/* still need to rebuild row_bitmaps */
		for (i = 0; i < p_ptr->num_rows; i++) {
			_reset_part_row_bitmap(&p_ptr->row[i]);
			if (p_ptr->row[i].num_jobs == 0)
				continue;
			for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
				job_res_add_cores(p_ptr->row[i].job_list[j],
						  &p_ptr->row[i]);
			}
		}
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_SELECT_TYPE) {
		info("DEBUG: (after):");
		part_data_dump_res(p_ptr);
	}

	if (orig_row)
		part_data_destroy_row(orig_row, p_ptr->num_rows);
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

/* (re)create the global select_part_record array */
extern void part_data_create_array(void)
{
	List part_rec_list = NULL;
	ListIterator part_iterator;
	part_record_t *p_ptr;
	part_res_record_t *this_ptr, *last_ptr = NULL;
	int num_parts;

	part_data_destroy_res(select_part_record);
	select_part_record = NULL;

	num_parts = list_count(part_list);
	if (!num_parts)
		return;
	info("%s: preparing for %d partitions", plugin_type, num_parts);

	part_rec_list = list_create(NULL);
	part_iterator = list_iterator_create(part_list);
	while ((p_ptr = list_next(part_iterator))) {
		this_ptr = xmalloc(sizeof(part_res_record_t));
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

/* Delete the given list of partition data */
extern void part_data_destroy_res(part_res_record_t *this_ptr)
{
	while (this_ptr) {
		part_res_record_t *tmp = this_ptr;
		this_ptr = this_ptr->next;
		tmp->part_ptr = NULL;

		if (tmp->row) {
			part_data_destroy_row(tmp->row, tmp->num_rows);
			tmp->row = NULL;
		}
		xfree(tmp);
	}
}

/* Delete the given partition row data */
extern void part_data_destroy_row(part_row_data_t *row, uint16_t num_rows)
{
	uint32_t r;

	for (r = 0; r < num_rows; r++) {
		free_core_array(&row[r].row_bitmap);
		xfree(row[r].job_list);
	}

	xfree(row);
}

/* Log contents of partition structure */
extern void part_data_dump_res(part_res_record_t *p_ptr)
{
	uint32_t n, r;
	node_record_t *node_ptr;

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
			node_ptr = node_record_table_ptr[n];
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

/* Create a duplicate part_res_record list */
extern part_res_record_t *part_data_dup_res(
	part_res_record_t *orig_ptr, bitstr_t *node_map)
{
	part_res_record_t *new_part_ptr, *new_ptr;

	if (orig_ptr == NULL)
		return NULL;

	new_part_ptr = xmalloc(sizeof(part_res_record_t));
	new_ptr = new_part_ptr;

	while (orig_ptr) {
		new_ptr->part_ptr = orig_ptr->part_ptr;
		if (node_map && orig_ptr->part_ptr->node_bitmap &&
		    bit_overlap_any(node_map,
				    orig_ptr->part_ptr->node_bitmap)) {
			new_ptr->num_rows = orig_ptr->num_rows;
			new_ptr->row = part_data_dup_row(orig_ptr->row,
							 orig_ptr->num_rows);
		}
		if (orig_ptr->next) {
			new_ptr->next = xmalloc(sizeof(part_res_record_t));
			new_ptr = new_ptr->next;
		}
		orig_ptr = orig_ptr->next;
	}
	return new_part_ptr;
}

/* sort the rows of a partition from "most allocated" to "least allocated" */
extern void part_data_sort_res(part_res_record_t *p_ptr)
{
	uint32_t i, j;

	if (!p_ptr->row)
		return;

	for (i = 0; i < p_ptr->num_rows; i++) {
		for (j = i + 1; j < p_ptr->num_rows; j++) {
			if (p_ptr->row[j].row_set_count >
			    p_ptr->row[i].row_set_count) {
				_swap_rows(&(p_ptr->row[i]), &(p_ptr->row[j]));
			}
		}
	}

	return;
}

/* Create a duplicate part_row_data struct */
extern part_row_data_t *part_data_dup_row(part_row_data_t *orig_row,
					       uint16_t num_rows)
{
	part_row_data_t *new_row;
	int i, n;

	if (num_rows == 0 || !orig_row)
		return NULL;

	new_row = xcalloc(num_rows, sizeof(part_row_data_t));
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
			new_row[i].row_set_count = orig_row[i].row_set_count;
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
