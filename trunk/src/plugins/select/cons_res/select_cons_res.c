/*****************************************************************************\
 *  select_cons_res.c - node selection plugin supporting consumable 
 *  resources policies.
 *****************************************************************************\
 *
 *  The following example below illustrates how four jobs are allocated
 *  across a cluster using when a processor consumable resource approach.
 * 
 *  The example cluster is composed of 4 nodes (10 cpus in total):
 *  linux01 (with 2 processors), 
 *  linux02 (with 2 processors), 
 *  linux03 (with 2 processors), and
 *  linux04 (with 4 processors). 
 *
 *  The four jobs are the following: 
 *  1. srun -n 4 -N 4  sleep 120 &
 *  2. srun -n 3 -N 3 sleep 120 &
 *  3. srun -n 1 sleep 120 &
 *  4. srun -n 3 sleep 120 &
 *  The user launches them in the same order as listed above.
 * 
 *  Using a processor consumable resource approach we get the following
 *  job allocation and scheduling:
 * 
 *  The output of squeue shows that we have 3 out of the 4 jobs allocated
 *  and running. This is a 2 running job increase over the default SLURM
 *  approach.
 * 
 *  Job 2, Job 3, and Job 4 are now running concurrently on the cluster.
 * 
 *  [<snip>]# squeue
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root  PD       0:00      1 (Resources)
 *     2        lsf    sleep     root   R       0:13      4 linux[01-04]
 *     3        lsf    sleep     root   R       0:09      3 linux[01-03]
 *     4        lsf    sleep     root   R       0:05      1 linux04
 *  [<snip>]#
 * 
 *  Once Job 2 finishes, Job 5, which was pending, is allocated
 *  available resources and is then running as illustrated below:
 * 
 *  [<snip>]# squeue4
 *   JOBID PARTITION    NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     3        lsf    sleep     root   R       1:58      3 linux[01-03]
 *     4        lsf    sleep     root   R       1:54      1 linux04
 *     5        lsf    sleep     root   R       0:02      3 linux[01-03]
 *  [<snip>]#
 * 
 *  Job 3, Job 4, and Job 5 are now running concurrently on the cluster.
 * 
 *  [<snip>]#  squeue4
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root   R       1:52      3 xc14n[13-15]
 *  [<snip>]#
 *
 * The advantage of the consumable resource scheduling policy is that
 * the job throughput can increase dramatically.
 *
 *****************************************************************************
 *  Copyright (C) 2005-2008 Hewlett-Packard Development Company, L.P.
 *  Written by Susanne M. Balle <susanne.balle@hp.com>, who borrowed heavily
 *  from select/linear 
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#endif

#include "select_cons_res.h"
#include "dist_tasks.h"
#include "job_test.h"

#if(0)
#define CR_DEBUG 1
#endif

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for SLURM node selection) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load select plugins if the plugin_type string has a 
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the node selection API matures.
 */
const char plugin_name[] = "Consumable Resources (CR) Node Selection plugin";
const char plugin_type[] = "select/cons_res";
const uint32_t plugin_version = 91;
const uint32_t pstate_version = 7;	/* version control on saved state */

select_type_plugin_info_t cr_type = CR_CPU; /* cr_type is overwritten in init() */

uint16_t select_fast_schedule;

uint16_t *cr_node_num_cores = NULL;
uint32_t *cr_num_core_count = NULL;
struct node_res_record *select_node_record = NULL;
struct part_res_record *select_part_record = NULL;
static int select_node_cnt = 0;


/* Procedure Declarations */
static int _will_run_test(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes, enum node_cr_state job_node_req);

#if (CR_DEBUG)

static void _dump_job_res(select_job_res_t job) {
	char str[64];

	if (job->core_bitmap)
		bit_fmt(str, 63, job->core_bitmap);
	else
		sprintf(str, "[no core_bitmap]");
	info("DEBUG: Dump select_job_res: nhosts %u cb %s", job->nhosts, str);
}
 
static void _dump_nodes()
{
	int i;
	
	for (i=0; i<select_node_cnt; i++) {
		info("node:%s cpus:%u c:%u s:%u t:%u mem:%u a_mem:%u state:%d",
			select_node_record[i].node_ptr->name,
			select_node_record[i].cpus,
			select_node_record[i].cores,
			select_node_record[i].sockets,
			select_node_record[i].vpus,
			select_node_record[i].real_memory,
			select_node_record[i].alloc_memory,
			select_node_record[i].node_state);	
	}
}

static void _dump_part(struct part_res_record *p_ptr)
{
	uint16_t i;
	info("part:%s rows:%u pri:%u ", p_ptr->name, p_ptr->num_rows,
		p_ptr->priority);
	if (!p_ptr->row)
		return;

	for (i = 0; i < p_ptr->num_rows; i++) {
		char str[64]; /* print first 64 bits of bitmaps */
		if (p_ptr->row[i].row_bitmap) {
			bit_fmt(str, 63, p_ptr->row[i].row_bitmap);
		} else {
			sprintf(str, "[no row_bitmap]");
		}
		info("  row%u: num_jobs %u: bitmap: %s", i,
			p_ptr->row[i].num_jobs, str);
	}
}

static void _dump_state(struct part_res_record *p_ptr)
{
	_dump_nodes();

	/* dump partition data */
	for (; p_ptr; p_ptr = p_ptr->next) {
		_dump_part(p_ptr);
	}
	return;
}
#endif


#define CR_NUM_CORE_ARRAY_INCREMENT 8

/* (re)set cr_node_num_cores and cr_num_core_count arrays */
static void _init_global_core_data(struct node_record *node_ptr, int node_cnt)
{
	uint32_t i, n, array_size = CR_NUM_CORE_ARRAY_INCREMENT;

	xfree(cr_num_core_count);
	xfree(cr_node_num_cores);
	cr_node_num_cores = xmalloc(array_size * sizeof(uint16_t));
	cr_num_core_count = xmalloc(array_size * sizeof(uint32_t));

	for (i = 0, n = 0; n < node_cnt; n++) {
		uint16_t cores;
		if (select_fast_schedule) {
			cores  = node_ptr[n].config_ptr->cores;
			cores *= node_ptr[n].config_ptr->sockets;
		} else {
			cores  = node_ptr[n].cores;
			cores *= node_ptr[n].sockets;
		}
		if (cr_node_num_cores[i] == cores) {
			cr_num_core_count[i]++;
			continue;
		}
		if (cr_num_core_count[i] > 0) {
			i++;
			if (i > array_size) {
				array_size += CR_NUM_CORE_ARRAY_INCREMENT;
				xrealloc(cr_node_num_cores,
					array_size * sizeof(uint16_t));
				xrealloc(cr_node_num_cores,
					array_size * sizeof(uint16_t));
			}
		}
		cr_node_num_cores[i] = cores;
		cr_num_core_count[i] = 1;
	}
	/* make sure we have '0'-terminate fields at the end */
	i++;
	if (i > array_size) {
		array_size += CR_NUM_CORE_ARRAY_INCREMENT;
		xrealloc(cr_node_num_cores, array_size * sizeof(uint16_t));
		xrealloc(cr_node_num_cores, array_size * sizeof(uint16_t));
	}
}


/* return the coremap index to the first core of the given node */
extern uint32_t cr_get_coremap_offset(uint32_t node_index)
{
	uint32_t i;
	uint32_t cindex = 0;
	uint32_t n = cr_num_core_count[0];
	for (i = 0; cr_num_core_count[i] && node_index > n; i++) {
		cindex += cr_node_num_cores[i] * cr_num_core_count[i];
		n += cr_num_core_count[i+1];
	}
	if (!cr_num_core_count[i])
		return cindex;
	n -= cr_num_core_count[i];

	cindex += cr_node_num_cores[i] * (node_index-n);	
	return cindex;
}


/* return the total number of cores in a given node */
extern uint32_t cr_get_node_num_cores(uint32_t node_index)
{
	uint32_t i = 0;
	uint32_t pos = cr_num_core_count[i++];
	while (node_index >= pos) {
		pos += cr_num_core_count[i++];
	}
	return cr_node_num_cores[i-1];
}


/* Helper function for _dup_part_data: create a duplicate part_row_data array */
static struct part_row_data *_dup_row_data(struct part_row_data *orig_row,
					   uint16_t num_rows)
{
	struct part_row_data *new_row;
	int i, j;

	if (num_rows == 0 || !orig_row)
		return NULL;
	
	new_row = xmalloc(num_rows * sizeof(struct part_row_data));
	for (i = 0; i < num_rows; i++) {
		new_row[i].num_jobs = orig_row[i].num_jobs;
		new_row[i].job_list_size = orig_row[i].job_list_size;
		if (orig_row[i].row_bitmap) 
			new_row[i].row_bitmap= bit_copy(orig_row[i].row_bitmap);
		if (new_row[i].job_list_size == 0)
			continue;
		/* copy the job list */
		new_row[i].job_list = xmalloc(new_row[i].job_list_size *
							sizeof(bitstr_t *));
		for (j = 0; j < new_row[i].num_jobs; j++) {
			new_row[i].job_list[j] = orig_row[i].job_list[j];
		}
	}
	return new_row;
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
		new_ptr->name = xstrdup(orig_ptr->name);
		new_ptr->priority = orig_ptr->priority;
		new_ptr->num_rows = orig_ptr->num_rows;
		new_ptr->row = _dup_row_data(orig_ptr->row, orig_ptr->num_rows);
		if (orig_ptr->next) {
			new_ptr->next = xmalloc(sizeof(struct part_res_record));
			new_ptr = new_ptr->next;
		}
		orig_ptr = orig_ptr->next;
	}
	return new_part_ptr;
}


/* delete the given list of partition data */
static void _destroy_part_data(struct part_res_record *this_ptr)
{
	while (this_ptr) {
		struct part_res_record *tmp = this_ptr;
		this_ptr = this_ptr->next;
		xfree(tmp->name);
		tmp->name = NULL;
		if (tmp->row) {
			int i;
			for (i = 0; i < tmp->num_rows; i++) {
				if (tmp->row[i].row_bitmap) {
					bit_free(tmp->row[i].row_bitmap);
					tmp->row[i].row_bitmap = NULL;
				}
				if (tmp->row[i].job_list) {
					int j;
					for (j = 0; j < tmp->row[i].num_jobs;
						j++)
						tmp->row[i].job_list[j] = NULL;
					xfree(tmp->row[i].job_list);
					tmp->row[i].job_list = NULL;
				}
			}
			xfree(tmp->row);
			tmp->row = NULL;
		}
		xfree(tmp);
	}
}


/* (re)create the global select_part_record array */
static void _create_part_data()
{
	ListIterator part_iterator;
	struct part_record *p_ptr;
	struct part_res_record *this_ptr;
	int num_parts;

	_destroy_part_data(select_part_record);
	select_part_record = NULL;

	num_parts = list_count(part_list);
	if (!num_parts)
		return;

	select_part_record = xmalloc(sizeof(struct part_res_record));
	this_ptr = select_part_record;

	part_iterator = list_iterator_create(part_list);
	if (part_iterator == NULL)
		fatal ("memory allocation failure");

	while ((p_ptr = (struct part_record *) list_next(part_iterator))) {
		this_ptr->name = xstrdup(p_ptr->name);
		this_ptr->num_rows = p_ptr->max_share;
		if (this_ptr->num_rows & SHARED_FORCE)
			this_ptr->num_rows &= (~SHARED_FORCE);
		/* SHARED=EXCLUSIVE sets max_share = 0 */
		if (this_ptr->num_rows < 1)
			this_ptr->num_rows = 1;
		/* we'll leave the 'row' array blank for now */
		this_ptr->row = NULL;
		this_ptr->priority = p_ptr->priority;
		num_parts--;
		if (num_parts) {
			this_ptr->next =xmalloc(sizeof(struct part_res_record));
			this_ptr = this_ptr->next;
		}
	}
	/* should we sort the select_part_record list by priority here? */
}


/* List sort function: sort by the job's expected end time */
static int _cr_job_list_sort(void *x, void *y)
{
	struct job_record **job1_pptr = (struct job_record **) x;
	struct job_record **job2_pptr = (struct job_record **) y;
	return (int) difftime(job1_pptr[0]->end_time, job2_pptr[0]->end_time);
}


/* Find a partition record from the global array based
 * upon a pointer to the slurmctld part_record */
struct part_res_record *_get_cr_part_ptr(struct part_record *part_ptr)
{
	struct part_res_record *p_ptr;

	if (part_ptr == NULL)
		return NULL;

	if (!select_part_record)
		_create_part_data();

	for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
		if (strcmp(p_ptr->name, part_ptr->name) == 0)
			return p_ptr;
	}
	error("cons_res: could not find partition %s", part_ptr->name);

	return NULL;
}


/* delete the select_node_record array */
static void _destroy_node_data()
{
	xfree(select_node_record);
	select_node_record = NULL;
}


static void _add_job_to_row(struct select_job_res *job,
			    struct part_row_data *r_ptr, int first_job)
{
	/* add the job to the row_bitmap */
	if (r_ptr->row_bitmap && first_job) {
		uint32_t size = bit_size(r_ptr->row_bitmap);
		bit_nclear(r_ptr->row_bitmap, 0, size-1);
	}
	add_select_job_to_row(job, &(r_ptr->row_bitmap), cr_node_num_cores,
				cr_num_core_count);
	
	/*  add the job to the job_list */
	if (r_ptr->num_jobs >= r_ptr->job_list_size) {
		r_ptr->job_list_size += 8;
		xrealloc(r_ptr->job_list, r_ptr->job_list_size *
					sizeof(struct select_job_res *));
	}
	r_ptr->job_list[r_ptr->num_jobs++] = job;
}


/* test for conflicting core_bitmap bits */
static int _can_job_fit_in_row(struct select_job_res *job,
				struct part_row_data *r_ptr)
{
	if (r_ptr->num_jobs == 0 || !r_ptr->row_bitmap)
		return 1;
	return can_select_job_cores_fit(job, r_ptr->row_bitmap,
					cr_node_num_cores, cr_num_core_count);
}


/* helper script for cr_sort_part_rows() */
static void _swap_rows(struct part_row_data *a, struct part_row_data *b)
{
	struct part_row_data tmprow;

	tmprow.row_bitmap    = a->row_bitmap;
	tmprow.num_jobs      = a->num_jobs;
	tmprow.job_list      = a->job_list;
	tmprow.job_list_size = a->job_list_size;
	
	a->row_bitmap    = b->row_bitmap;
	a->num_jobs      = b->num_jobs;
	a->job_list      = b->job_list;
	a->job_list_size = b->job_list_size;
	
	b->row_bitmap    = tmprow.row_bitmap;
	b->num_jobs      = tmprow.num_jobs;
	b->job_list      = tmprow.job_list;
	b->job_list_size = tmprow.job_list_size;
	
	return;
}


/* sort the rows of a partition from "most allocated" to "least allocated" */
extern void cr_sort_part_rows(struct part_res_record *p_ptr)
{
	uint32_t i, j, a, b;

	if (!p_ptr->row)
		return;
		
	for (i = 0; i < p_ptr->num_rows; i++) {
		if (p_ptr->row[i].row_bitmap)
			a = bit_set_count(p_ptr->row[i].row_bitmap);
		else
			a = 0;
		for (j = i+1; j < p_ptr->num_rows; j++) {
			if (!p_ptr->row[j].row_bitmap)
				continue;
			b = bit_set_count(p_ptr->row[j].row_bitmap);
			if (b > a) {
				_swap_rows(&(p_ptr->row[i]), &(p_ptr->row[j]));
			}
		}
	}
	return;
}


/*
 * _build_row_bitmaps: Optimize the jobs into the least number of rows,
 *                     and make the lower rows as dense as possible.
 *
 * IN/OUT: p_ptr   - the partition that has jobs to be optimized
 */
static void _build_row_bitmaps(struct part_res_record *p_ptr)
{
	uint32_t i, j, num_jobs, size;
	int x, *jstart;
	struct part_row_data *this_row;
	struct select_job_res **tmpjobs, *job;
	
	if (!p_ptr->row)
		return;

	if (p_ptr->num_rows == 1) {
		this_row = &(p_ptr->row[0]);
		if (this_row->num_jobs == 0) {
			if (this_row->row_bitmap) {
				size = bit_size(this_row->row_bitmap);
				bit_nclear(this_row->row_bitmap, 0, size-1);
			}
			return;
		}
		
		/* rebuild the row bitmap */
		num_jobs = this_row->num_jobs;
		tmpjobs = xmalloc(num_jobs * sizeof(struct select_job_res *));	
		for (i = 0; i < num_jobs; i++) {
			tmpjobs[i] = this_row->job_list[i];
			this_row->job_list[i] = NULL;
		}
		this_row->num_jobs = 0;
		_add_job_to_row(tmpjobs[0], this_row, 1);
		for (i = 1; i < num_jobs; i++) {
			_add_job_to_row(tmpjobs[i], this_row, 0);
		}
		xfree(tmpjobs);
		return;
	}

	/* gather data */
	num_jobs = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		if (p_ptr->row[i].num_jobs) {
			num_jobs += p_ptr->row[i].num_jobs;
		}
	}
	debug3("cons_res: build_row_bitmaps reshuffling %u jobs", num_jobs);
	
	/* Question: should we try to avoid disrupting rows that are full? */
	
	/* create a master job list and clear out ALL row data */
	tmpjobs = xmalloc(num_jobs * sizeof(struct select_job_res *));	
	jstart  = xmalloc(num_jobs * sizeof(int));
	x = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
			tmpjobs[x] = p_ptr->row[i].job_list[j];
			p_ptr->row[i].job_list[j] = NULL;
			jstart[x] = bit_ffs(tmpjobs[x]->node_bitmap);
			jstart[x] = cr_get_coremap_offset(jstart[x]);
			jstart[x] += bit_ffs(tmpjobs[x]->core_bitmap);
			x++;
		}
		p_ptr->row[i].num_jobs = 0;
		if (p_ptr->row[i].row_bitmap) {
			size = bit_size(p_ptr->row[i].row_bitmap);
			bit_nclear(p_ptr->row[i].row_bitmap, 0, size-1);
		}
	}
	
#if (CR_DEBUG)
	for (i = 0; i < x; i++) {
		char cstr[64], nstr[64];
		if (tmpjobs[i]->core_bitmap)
			bit_fmt(cstr, 63, tmpjobs[i]->core_bitmap);
		else
			sprintf(cstr, "[no core_bitmap]");
		if (tmpjobs[i]->node_bitmap)
			bit_fmt(nstr, 63, tmpjobs[i]->node_bitmap);
		else
			sprintf(nstr, "[no node_bitmap]");
		info ("DEBUG:  jstart %d job nb %s cb %s", jstart[i], nstr,
			cstr);
	}
#endif

	/* VERY difficult: Optimal placement of jobs in the matrix
	 * - how to order jobs to be added to the matrix?
	 *   - "by size" does not guarantee optimal placement
	 *
	 *   - for now, try sorting jobs by first bit set
	 *     - if job allocations stay "in blocks", then this should work OK
	 *     - may still get scenarios where jobs should switch rows
	 *     - fixme: JOB SHUFFLING BETWEEN ROWS NEEDS TESTING
	 */
	for (i = 0; i < num_jobs; i++) {
		for (j = i+1; j < num_jobs; j++) {
			if (jstart[j] < jstart[i]) {
				x = jstart[i];
				jstart[i] = jstart[j];
				jstart[j] = x;
				job = tmpjobs[i];
				tmpjobs[i] = tmpjobs[j];
				tmpjobs[j] = job;
			}
		}
	}

	/* add jobs to the rows */
	for (j = 0; j < num_jobs; j++) {
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (p_ptr->row[i].num_jobs == 0) {
				/* add the job to this row */
				_add_job_to_row(tmpjobs[j], &(p_ptr->row[i]),1);
				tmpjobs[j] = NULL;
				break;
			}
			/* ELSE: test if the job can fit in this row */
			if (_can_job_fit_in_row(tmpjobs[j], &(p_ptr->row[i]))) {
				/* job fits in row, so add it */
				_add_job_to_row(tmpjobs[j], &(p_ptr->row[i]),0);
				tmpjobs[j] = NULL;
				break;
			}
		}
		/* job should have been added, so shuffle the rows */
		cr_sort_part_rows(p_ptr);
	}
	
	/* test for dangling jobs */
	for (j = 0; j < num_jobs; j++) {
		if (tmpjobs[j]) {
			error("cons_res: ERROR: job overflow "
			      "during build_row_bitmap");
			/* just merge this job into the last row for now */
			_add_job_to_row(tmpjobs[j],
					&(p_ptr->row[p_ptr->num_rows-1]),0);
		}
	}

#if (CR_DEBUG)
	info("DEBUG: _build_row_bitmaps:");
	_dump_part(p_ptr);
#endif

	xfree(tmpjobs);
	xfree(jstart);
	return;

	/* LEFTOVER DESIGN THOUGHTS, PRESERVED HERE */
	
	/* 1. sort jobs by size
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
	
	/* WORK IN PROGRESS - more optimization should go here, such as:
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


/* allocate resources to the given job
 * - add 'struct select_job_res' resources to 'struct part_res_record'
 * - add job's memory requirements to 'struct node_res_record'
 *
 * if action = 0 then add cores and memory
 * if action = 1 then only add memory (job is suspended)
 * if action = 2 then only add cores (job is resumed)
 */
static int _add_job_to_res(struct job_record *job_ptr, int action)
{
	struct select_job_res *job = job_ptr->select_job;
	struct part_res_record *p_ptr;
	int i, n;

	if (!job || !job->core_bitmap) {
		error("job %u has no select data", job_ptr->job_id);
		return SLURM_ERROR;
	}
	
	debug3("cons_res: _add_job_to_res: job %u act %d ", job_ptr->job_id,
		action);

#if (CR_DEBUG)
	_dump_job_res(job);
#endif

	/* add memory */
	if (action != 2) {
		for (i = 0, n = 0; i < select_node_cnt; i++) {
			if (!bit_test(job->node_bitmap, i))
				continue;
			select_node_record[i].alloc_memory +=
						job->memory_allocated[n];
			if (select_node_record[i].alloc_memory >
				select_node_record[i].real_memory) {
				error("error: node %s mem is overallocated(%d)",
					select_node_record[i].node_ptr->name,
					select_node_record[i].alloc_memory);
				
			}
			n++;
		}
	}
	
	/* add cores */
	if (action != 1) {

		p_ptr = _get_cr_part_ptr(job_ptr->part_ptr);
		if (!p_ptr)
			return SLURM_ERROR;
			
		if (!p_ptr->row) {
			p_ptr->row = xmalloc(p_ptr->num_rows *
						sizeof(struct part_row_data));
			debug3("cons_res: adding job %u to part %s row 0",
				job_ptr->job_id, p_ptr->name);
			_add_job_to_row(job, &(p_ptr->row[0]), 1);
			goto node_st;
		}
		
		/* find a row to add this job */
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (p_ptr->row[i].num_jobs == 0) {
				debug3("cons_res: adding job %u to part %s row %u",
				job_ptr->job_id, p_ptr->name, i);
				_add_job_to_row(job, &(p_ptr->row[i]), 1);
				break;
			}
			if (_can_job_fit_in_row(job, &(p_ptr->row[i]))) {
				debug3("cons_res: adding job %u to part %s row %u",
				job_ptr->job_id, p_ptr->name, i);
				_add_job_to_row(job, &(p_ptr->row[i]), 0);
				break;
			}
		}
		if (i >= p_ptr->num_rows) {
			/* ERROR: could not find a row for this job */
			error("cons_res: ERROR: job overflow: "
			      "could not find row for job");
			/* just add the job to the last row for now */
			_add_job_to_row(job, &(p_ptr->row[p_ptr->num_rows-1]), 0);
		}
		/* update the node state */
node_st:	for (i = 0; i < select_node_cnt; i++) {
			if (bit_test(job->node_bitmap, i))
				select_node_record[i].node_state =job->node_req;
		}
#if (CR_DEBUG)
		info("DEBUG: _add_job_to_res:");
		_dump_part(p_ptr);
#endif
	}

	return SLURM_SUCCESS;
}


/* Helper function for _rm_job_from_res:
 * - return 1 if node is available for general use, else return 0.
 *   if 'free' then the node can be NODE_CR_AVAILABLE. Otherwise,
 *   there's an existing job that may make the node either
 *   NODE_CR_RESERVED or NODE_CR_ONE_ROW.
 */
static int _is_node_free(struct part_res_record *p_ptr, uint32_t node_i)
{
	uint32_t c, cpu_begin = cr_get_coremap_offset(node_i);
	uint32_t i, cpu_end   = cr_get_coremap_offset(node_i+1);

	for (; p_ptr; p_ptr = p_ptr->next) {
		if (!p_ptr->row)
			continue;
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!p_ptr->row[i].row_bitmap)
				continue;
			for (c = cpu_begin; c < cpu_end; c++) {
				if (bit_test(p_ptr->row[i].row_bitmap, c))
					return 0;
			}
		}
	}
	return 1;
}

/* deallocate resources to the given job
 * - subtract 'struct select_job_res' resources from 'struct part_res_record'
 * - subtract job's memory requirements from 'struct node_res_record'
 *
 * if action = 0 then subtract cores and memory
 * if action = 1 then only subtract memory (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 *
 */
static int _rm_job_from_res(struct job_record *job_ptr, int action)
{
	struct select_job_res *job = job_ptr->select_job;
	int i, n;

	if (!job || !job->core_bitmap) {
		error("job %u has no select data", job_ptr->job_id);
		return SLURM_ERROR;
	}
	
	debug3("cons_res: _rm_job_from_res: job %u act %d", job_ptr->job_id,
		action);
#if (CR_DEBUG)
	_dump_job_res(job);
#endif
	
	/* subtract memory */
	if (action != 2) {
		for (i = 0, n = 0; i < select_node_cnt; i++) {
			if (!bit_test(job->node_bitmap, i))
				continue;
			if (select_node_record[i].alloc_memory <
				job->memory_allocated[n]) {
				error("error: %s mem is underalloc'd(%u-%u)",
					select_node_record[i].node_ptr->name,
					select_node_record[i].alloc_memory,
					job->memory_allocated[n]);
				select_node_record[i].alloc_memory = 0;
			} else {
				select_node_record[i].alloc_memory -=
						job->memory_allocated[n];
			}
			n++;
		}
	}
	
	/* subtract cores */
	if (action != 1) {
		/* reconstruct rows with remaining jobs */
		struct part_res_record *p_ptr;
		
		p_ptr = _get_cr_part_ptr(job_ptr->part_ptr);
		if (!p_ptr) {
			error("error: 'rm' could not find part %s",
				job_ptr->part_ptr);
			return SLURM_ERROR;
		}
		
		if (!p_ptr->row) {
			return SLURM_SUCCESS;
		}
		
		/* remove the job from the job_list */
		n = 0;
		for (i = 0; i < p_ptr->num_rows; i++) {
			uint32_t j;
			for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
				if (p_ptr->row[i].job_list[j] != job)
					continue;
					debug3("cons_res: removing job %u from "
					       "part %s row %u",
					       job_ptr->job_id, p_ptr->name, i);
				for (; j < p_ptr->row[i].num_jobs-1; j++) {
					p_ptr->row[i].job_list[j] =
						p_ptr->row[i].job_list[j+1];
				}
				p_ptr->row[i].job_list[j] = NULL;
				p_ptr->row[i].num_jobs -= 1;
				/* found job - we're done */
				n = 1;
				i = p_ptr->num_rows;
				break;
			}
		}
		
		if (n) {
			/* job was found and removed, so refresh the bitmaps */
			_build_row_bitmaps(p_ptr);

			/* Adjust the node_state of all nodes affected by
			 * the removal of this job. If all cores are now
			 * available, set node_state = NODE_CR_AVAILABLE
			 */
			for (n = 0; n < select_node_cnt; n++) {
				if (bit_test(job->node_bitmap, n) == 0)
					continue;
				if (select_node_record[n].node_state ==
				    NODE_CR_AVAILABLE)
					continue;
				if (_is_node_free(select_part_record, n))
					select_node_record[n].node_state = 
						NODE_CR_AVAILABLE;
			}
		}
#if (CR_DEBUG)
		_dump_part(p_ptr);
#endif
	}

	return SLURM_SUCCESS;
}


/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
#ifdef HAVE_XCPU
	error("%s is incompatible with XCPU use", plugin_name);
	fatal("Use SelectType=select/linear");
#endif
#ifdef HAVE_BG
	error("%s is incompatable with BlueGene", plugin_name);
	fatal("Use SelectType=select/bluegene");
#endif
	cr_type = (select_type_plugin_info_t)
			slurmctld_conf.select_type_param;
	info("%s loaded with argument %d ", plugin_name, cr_type);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	_destroy_node_data();
	_destroy_part_data(select_part_record);
	select_part_record = NULL;
	xfree(cr_node_num_cores);
	xfree(cr_num_core_count);
	cr_node_num_cores = NULL;
	cr_num_core_count = NULL;

	verbose("%s shutting down ...", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM 
 * node selection API.
 */


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


/* This is Part 1 of a 4-part procedure which can be found in
 * src/slurmctld/read_config.c. The whole story goes like this:
 *
 * Step 1: select_g_node_init       : initializes the 'select_node_record'
 *                                    global array
 * Step 2: select_g_state_restore   : NO-OP - nothing to restore
 * Step 3: select_g_job_init        : NO-OP - nothing to initialize
 * Step 4: select_g_update_nodeinfo : called from reset_job_bitmaps() with
 *                                    each valid recovered job_ptr AND from
 *                                    select_nodes(), this procedure adds job
 *                                    data to the 'select_part_record' global
 *                                    array
 */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	int i;

	info("cons_res: select_p_node_init");
	if (node_ptr == NULL) {
		error("select_g_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}
	if (node_cnt < 0) {
		error("select_g_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	/* initial global core data structures */
	_init_global_core_data(node_ptr, node_cnt);
	
	_destroy_node_data();
	select_node_cnt  = node_cnt;
	select_node_record = xmalloc(node_cnt * sizeof(struct node_res_record));
	select_fast_schedule = slurm_get_fast_schedule();

	for (i = 0; i < select_node_cnt; i++) {
		select_node_record[i].node_ptr = &node_ptr[i];
		if (select_fast_schedule) {
			struct config_record *config_ptr;
			config_ptr = node_ptr[i].config_ptr;
			select_node_record[i].cpus    = config_ptr->cpus;
			select_node_record[i].sockets = config_ptr->sockets;
			select_node_record[i].cores   = config_ptr->cores;
			select_node_record[i].vpus    = config_ptr->threads;
			select_node_record[i].real_memory =
							config_ptr->real_memory;
		} else {
			select_node_record[i].cpus    = node_ptr[i].cpus;
			select_node_record[i].sockets = node_ptr[i].sockets;
			select_node_record[i].cores   = node_ptr[i].cores;
			select_node_record[i].vpus    = node_ptr[i].threads;
			select_node_record[i].real_memory =
							node_ptr[i].real_memory;
		}
		select_node_record[i].node_state = NODE_CR_AVAILABLE;
	}

	return SLURM_SUCCESS;
}

extern int select_p_block_init(List part_list)
{
	return SLURM_SUCCESS;
}

static struct multi_core_data * _create_default_mc(void)
{
	struct multi_core_data *mc_ptr;
	mc_ptr = xmalloc(sizeof(struct multi_core_data));
	mc_ptr->min_sockets = 1;
	mc_ptr->max_sockets = 0xffff;
	mc_ptr->min_cores   = 1;
	mc_ptr->max_cores   = 0xffff;
	mc_ptr->min_threads = 1;
	mc_ptr->max_threads = 0xffff;
/*	mc_ptr is initialized to zero by xmalloc*/
/*	mc_ptr->ntasks_per_socket = 0; */
/*	mc_ptr->ntasks_per_core   = 0; */
/*	mc_ptr->plane_size        = 0; */
	return mc_ptr;
}

/* Determine the node requirements for the job:
 * - does the job need exclusive nodes? (NODE_CR_RESERVED)
 * - can the job run on shared nodes?   (NODE_CR_ONE_ROW)
 * - can the job run on overcommitted resources? (NODE_CR_AVAILABLE)
 */
static enum node_cr_state _get_job_node_req(struct job_record *job_ptr)
{
	int max_share = job_ptr->part_ptr->max_share;
	
	if (max_share == 0)
		return NODE_CR_RESERVED;
	
	if (max_share & SHARED_FORCE)
		return NODE_CR_AVAILABLE;

	/* Shared=NO or Shared=YES */
	if (job_ptr->details->shared == 0)
		/* user has requested exclusive nodes */
		return NODE_CR_RESERVED;
	if ((max_share > 1) && (job_ptr->details->shared == 2))
		/* part allows sharing, and
		 * the user has requested it */
		return NODE_CR_AVAILABLE;
	return NODE_CR_ONE_ROW;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satisfy the request.
 * 	"best" is defined as either a minimal number of consecutive nodes
 *	or if sharing resources then sharing them with a job of similar size.
 * IN/OUT job_ptr - pointer to job being considered for initiation,
 *                  set's start_time when job expected to start
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN req_nodes - requested (or desired) count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * IN mode - SELECT_MODE_RUN_NOW: try to schedule job now
 *           SELECT_MODE_TEST_ONLY: test if job can ever run
 *           SELECT_MODE_WILL_RUN: determine when and where job can run
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init): 
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: the job information that is considered for scheduling includes:
 *	req_node_bitmap: bitmap of specific nodes required by the job
 *	contiguous: allocated nodes must be sequentially located
 *	num_procs: minimum number of processors required by the job
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t * bitmap,
			     uint32_t min_nodes, uint32_t max_nodes, 
			     uint32_t req_nodes, int mode)
{
	int rc;
	enum node_cr_state job_node_req;

	xassert(bitmap);

	if (!job_ptr->details)
		return EINVAL;

	if (!job_ptr->details->mc_ptr)
		job_ptr->details->mc_ptr = _create_default_mc();
	job_node_req = _get_job_node_req(job_ptr);

	debug3("cons_res: select_p_job_test: job %d node_req %d, mode %d",
	       job_ptr->job_id, job_node_req, mode);
	debug3("cons_res: select_p_job_test: min_n %u max_n %u req_n %u nb %u",
	       min_nodes, max_nodes, req_nodes, bit_set_count(bitmap));

#if (CR_DEBUG)
	_dump_state(select_part_record);
#endif
	if (mode == SELECT_MODE_WILL_RUN) {
		rc = _will_run_test(job_ptr, bitmap, min_nodes, max_nodes,
				    req_nodes, job_node_req);
	} else {
		rc = cr_job_test(job_ptr, bitmap, min_nodes, max_nodes,
				 req_nodes, mode, cr_type, job_node_req,
				 select_node_cnt, select_part_record);
	}

#if (CR_DEBUG)
	if (job_ptr->select_job)
		log_select_job_res(job_ptr->select_job);
	else
		info("no select_job_res info for job %u", 
		     job_ptr->job_id);
#endif

	return rc;
}

/*
 * select_p_job_list_test - Given a list of select_will_run_t's in
 *	accending priority order we will see if we can start and
 *	finish all the jobs without increasing the start times of the
 *	jobs specified and fill in the est_start of requests with no
 *	est_start.  If you are looking to see if one job will ever run
 *	then use select_p_job_test instead.
 * IN/OUT req_list - list of select_will_run_t's in asscending
 *	             priority order on success of placement fill in
 *	             est_start of request with time.
 * RET zero on success, EINVAL otherwise
 */
extern int select_p_job_list_test(List req_list)
{
	/* not currently supported */
	return EINVAL;
}


/* _will_run_test - determine when and where a pending job can start, removes 
 *	jobs from node table at termination time and run _test_job() after 
 *	each one. */
static int _will_run_test(struct job_record *job_ptr, bitstr_t *bitmap,
			uint32_t min_nodes, uint32_t max_nodes, 
			uint32_t req_nodes, enum node_cr_state job_node_req)
{
	struct part_res_record *future_part;
	struct job_record *tmp_job_ptr, **tmp_job_pptr;
	List cr_job_list;
	ListIterator job_iterator;
	bitstr_t *orig_map;
	int rc = SLURM_ERROR;
	uint32_t i, *orig_alloc_mem;
	uint8_t *orig_node_state;
	time_t now = time(NULL);

	orig_map = bit_copy(bitmap);

	/* Try to run with currently available nodes */
	rc = cr_job_test(job_ptr, bitmap, min_nodes, max_nodes, req_nodes, 
			 SELECT_MODE_WILL_RUN, cr_type, job_node_req,
			 select_node_cnt, select_part_record);
	if (rc == SLURM_SUCCESS) {
		bit_free(orig_map);
		job_ptr->start_time = time(NULL);
		return SLURM_SUCCESS;
	}

	/* Job is still pending. Simulate termination of jobs one at a time 
	 * to determine when and where the job can start. */

	future_part = _dup_part_data(select_part_record);
	if (future_part == NULL) {
		bit_free(orig_map);
		return SLURM_ERROR;
	}
	/* Need to preserve node_res_record->alloc_memory and
	 * node_res_record->node_state, which will both be
	 * altered when jobs are removed */
	orig_alloc_mem  = xmalloc(select_node_cnt * sizeof(uint32_t));
	orig_node_state = xmalloc(select_node_cnt * sizeof(uint8_t)); 
	for (i = 0; i < select_node_cnt; i++) {
		orig_alloc_mem[i]  = select_node_record[i].alloc_memory;
		orig_node_state[i] = select_node_record[i].node_state;
	}

	/* Build list of running jobs */
	cr_job_list = list_create(NULL);
	job_iterator = list_iterator_create(job_list);
	while ((tmp_job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (tmp_job_ptr->job_state != JOB_RUNNING)
			continue;
		if (tmp_job_ptr->end_time == 0) {
			error("Job %u has zero end_time", tmp_job_ptr->job_id);
			continue;
		}
		tmp_job_pptr = xmalloc(sizeof(struct job_record *));
		*tmp_job_pptr = tmp_job_ptr;
		list_append(cr_job_list, tmp_job_pptr);
	}
	list_iterator_destroy(job_iterator);
	list_sort(cr_job_list, _cr_job_list_sort);

	/* Remove the running jobs one at a time from exp_node_cr and try
	 * scheduling the pending job after each one */
	job_iterator = list_iterator_create(cr_job_list);
	while ((tmp_job_pptr = (struct job_record **) list_next(job_iterator))) {
		tmp_job_ptr = *tmp_job_pptr;
		_rm_job_from_res(tmp_job_ptr, 0);
		bit_or(bitmap, orig_map);
		rc = cr_job_test(job_ptr, bitmap, min_nodes, max_nodes, 
				 req_nodes, SELECT_MODE_WILL_RUN, cr_type,
				 job_node_req, select_node_cnt, future_part);
		if (rc == SLURM_SUCCESS) {
			if (tmp_job_ptr->end_time <= now)
				 job_ptr->start_time = now + 1;
			else
				job_ptr->start_time = tmp_job_ptr->end_time;
			break;
		}
	}
	list_iterator_destroy(job_iterator);
	list_destroy(cr_job_list);
	_destroy_part_data(future_part);
	for(i = 0; i < select_node_cnt; i++) {
		select_node_record[i].alloc_memory = orig_alloc_mem[i];
		select_node_record[i].node_state   = orig_node_state[i];
	}
	xfree(orig_alloc_mem);
	xfree(orig_node_state);
	bit_free(orig_map);
	return rc;
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_ready(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);
	
	_rm_job_from_res(job_ptr, 0);

	return SLURM_SUCCESS;
}

/* NOTE: This function is not called with sched/gang because it needs
 * to track how many jobs are running or suspended on each node.
 * This sum is compared with the partition's Shared parameter */
extern int select_p_job_suspend(struct job_record *job_ptr)
{
	xassert(job_ptr);

	return _rm_job_from_res(job_ptr, 2);
}

/* See NOTE with select_p_job_suspend above */
extern int select_p_job_resume(struct job_record *job_ptr)
{
	xassert(job_ptr);
	
	return _add_job_to_res(job_ptr, 2);
}


extern int select_p_pack_node_info(time_t last_query_time,
				   Buf * buffer_ptr)
{
	/* This function is always invalid on normal Linux clusters */
	return SLURM_ERROR;
}


extern int select_p_get_select_nodeinfo(struct node_record *node_ptr,
					enum select_data_info dinfo,
					void *data)
{
	uint32_t n, i, c, start, end;
	struct part_res_record *p_ptr;
	uint16_t tmp, *tmp_16;

	xassert(node_ptr);

	switch (dinfo) {
	case SELECT_ALLOC_CPUS:
		tmp_16 = (uint16_t *) data;
		*tmp_16 = 0;

		/* determine the highest number of allocated cores from */
		/* all rows of all partitions */
		for (n = 0; n < node_record_count; n++) {
			if (&(node_record_table_ptr[n]) == node_ptr)
				break;
		}
		if (n >= node_record_count) {
			/* did not find the node */
			return SLURM_ERROR;
		}
		start = cr_get_coremap_offset(n);
		end = cr_get_coremap_offset(n+1);
		for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
			if (!p_ptr->row)
				continue;
			for (i = 0; i < p_ptr->num_rows; i++) {
				if (!p_ptr->row[i].row_bitmap)
					continue;
				tmp = 0;
				for (c = start; c < end; c++) {
					if (bit_test(p_ptr->row[i].row_bitmap,
						     c))
						tmp++;
				}
				if (tmp > *tmp_16)
					*tmp_16 = tmp;
			}
		}
		break;
	default:
		error("select_g_get_select_nodeinfo info %d invalid", dinfo);
		return SLURM_ERROR;
		break;
	}
	return SLURM_SUCCESS;
}


extern int select_p_update_nodeinfo(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if ((job_ptr->job_state != JOB_RUNNING)
	&&  (job_ptr->job_state != JOB_SUSPENDED))
		return SLURM_SUCCESS;
	
	return _add_job_to_res(job_ptr, 0);
}

extern int select_p_update_block (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}


/* Helper function for _synchronize_bitmap().  Check
 * if the given node has at least one available CPU */
static uint16_t _is_node_avail(uint32_t node_i)
{
	struct part_res_record *p_ptr;
	uint32_t i, r, cpu_begin, cpu_end;
	
	/* check the node state */
	if (select_node_record[node_i].node_state == NODE_CR_RESERVED)
		return (uint16_t) 0;

	cpu_begin = cr_get_coremap_offset(node_i);
	cpu_end   = cr_get_coremap_offset(node_i+1);
	if (select_node_record[node_i].node_state == NODE_CR_ONE_ROW) {
		/* check the core_bitmaps in "single-row" partitions */
		for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->num_rows > 1)
				continue;
			if (!p_ptr->row || !p_ptr->row[0].row_bitmap)
				return (uint16_t) 1;
			for (i = cpu_begin; i < cpu_end; i++) {
				if (!bit_test(p_ptr->row[0].row_bitmap, i))
					return (uint16_t) 1;
			}
		}
	} else {
		/* check the core_bitmaps in all partitions */
		for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
			if (!p_ptr->row)
				return (uint16_t) 1;
			for (r = 0; r < p_ptr->num_rows; r++) {
				if (!p_ptr->row[r].row_bitmap)
					return (uint16_t) 1;
				for (i = cpu_begin; i < cpu_end; i++) {
					if (!bit_test(p_ptr->row[r].row_bitmap,
							i))
						return (uint16_t) 1;
				}
			}
		}
		
	}
	return (uint16_t) 0;
}


/* Worker function for select_p_get_info_from_plugin() */
static int _synchronize_bitmaps(bitstr_t ** partially_idle_bitmap)
{
	int size, i, idlecpus = bit_set_count(avail_node_bitmap);
	size = bit_size(avail_node_bitmap);
	bitstr_t *bitmap = bit_alloc(size);
	if (bitmap == NULL)
		return SLURM_ERROR;

	debug3("cons_res: synch_bm: size avail %d (%d set) size idle %d ",
	       size, idlecpus, bit_size(idle_node_bitmap));

	for (i = 0; i < select_node_cnt; i++) {
		if (bit_test(avail_node_bitmap, i) != 1)
			continue;

		if (bit_test(idle_node_bitmap, i) == 1) {
			bit_set(bitmap, i);
			continue;
		}
		
		if(_is_node_avail(i))
			bit_set(bitmap, i);
	}
	idlecpus = bit_set_count(bitmap);
	debug3("cons_res: synch found %d partially idle nodes", idlecpus);

	*partially_idle_bitmap = bitmap;
	return SLURM_SUCCESS;
}


extern int select_p_get_info_from_plugin(enum select_data_info info,
					 void *data)
{
	int rc = SLURM_SUCCESS;

	switch (info) {
	case SELECT_BITMAP:
	{
		bitstr_t **bitmap = (bitstr_t **) data;
		bitstr_t *tmp_bitmap = NULL;
		
		rc = _synchronize_bitmaps(&tmp_bitmap);
		if (rc != SLURM_SUCCESS) {
			FREE_NULL_BITMAP(tmp_bitmap);
			return rc;
		}
		*bitmap = tmp_bitmap;	/* Ownership transfer, 
					 * Remember to free bitmap 
					 * using FREE_NULL_BITMAP(bitmap);*/
		tmp_bitmap = 0;
		break;
	}
	case SELECT_CR_PLUGIN:
	{
		uint32_t *tmp_32 = (uint32_t *) data;
		*tmp_32 = 1;
		break;
	}
	default:
		error("select_g_get_info_from_plugin info %d invalid",
		      info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_update_node_state (int index, uint16_t state)
{
	return SLURM_SUCCESS;
}

extern int select_p_alter_node_cnt(enum select_node_cnt type, void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_reconfigure(void)
{
	ListIterator job_iterator;
	struct job_record *job_ptr;
	int rc = SLURM_SUCCESS;

	info("cons_res: select_p_reconfigure");
	select_fast_schedule = slurm_get_fast_schedule();

	/* Refresh the select_node_record global array in case nodes
	 * have been added, removed, or modified.
	 */
	rc = select_p_node_init(node_record_table_ptr, node_record_count);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* recreate the partition data structures (delete existing data) */
	_create_part_data();
	
	/* reload job data */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->job_state == JOB_RUNNING) {
			/* add the job */
			_add_job_to_res(job_ptr, 0);
		} else if (job_ptr->job_state == JOB_SUSPENDED) {
			/* add the job in a suspended state */
			_add_job_to_res(job_ptr, 2);
		}
	}
	list_iterator_destroy(job_iterator);

	return SLURM_SUCCESS;
}
