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
 *  [<snip>]# squeue
 *   JOBID PARTITION    NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     3        lsf    sleep     root   R       1:58      3 linux[01-03]
 *     4        lsf    sleep     root   R       1:54      1 linux04
 *     5        lsf    sleep     root   R       0:02      3 linux[01-03]
 *  [<snip>]#
 *
 *  Job 3, Job 4, and Job 5 are now running concurrently on the cluster.
 *
 *  [<snip>]#  squeue
 *  JOBID PARTITION     NAME     USER  ST       TIME  NODES NODELIST(REASON)
 *     5        lsf    sleep     root   R       1:52      3 linux[01-03]
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
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#endif

#include "src/common/slurm_xlator.h"
#include "select_cons_res.h"
#include "dist_tasks.h"
#include "job_test.h"


#define NODEINFO_MAGIC 0x82aa

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
slurm_ctl_conf_t slurmctld_conf __attribute__((weak_import));
struct node_record *node_record_table_ptr __attribute__((weak_import));
List part_list __attribute__((weak_import));
List job_list __attribute__((weak_import));
int node_record_count __attribute__((weak_import));
time_t last_node_update __attribute__((weak_import));
struct switch_record *switch_record_table __attribute__((weak_import));
int switch_record_cnt __attribute__((weak_import));
bitstr_t *avail_node_bitmap __attribute__((weak_import));
bitstr_t *idle_node_bitmap __attribute__((weak_import));
uint16_t *cr_node_num_cores __attribute__((weak_import));
uint32_t *cr_node_cores_offset __attribute__((weak_import));
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
bitstr_t *idle_node_bitmap;
uint16_t *cr_node_num_cores;
uint32_t *cr_node_cores_offset;
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
 * minimum version for their plugins as the node selection API matures.
 */
const char plugin_name[] = "Consumable Resources (CR) Node Selection plugin";
const char plugin_type[] = "select/cons_res";
const uint32_t plugin_id      = 101;
const uint32_t plugin_version = 100;
const uint32_t pstate_version = 7;	/* version control on saved state */

uint16_t cr_type = CR_CPU; /* cr_type is overwritten in init() */

uint32_t select_debug_flags;
uint16_t select_fast_schedule;

struct part_res_record *select_part_record = NULL;
struct node_res_record *select_node_record = NULL;
struct node_use_record *select_node_usage  = NULL;
static bool select_state_initializing = true;
static int select_node_cnt = 0;
static bool job_preemption_enabled = false;
static bool job_preemption_killing = false;
static bool job_preemption_tested  = false;

struct select_nodeinfo {
	uint16_t magic;		/* magic number */
	uint16_t alloc_cpus;
};

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void);
extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo);

/* Procedure Declarations */
static int _add_job_to_res(struct job_record *job_ptr, int action);
static int _job_expand(struct job_record *from_job_ptr,
		       struct job_record *to_job_ptr);
static int _rm_job_from_one_node(struct job_record *job_ptr,
				 struct node_record *node_ptr);
static int _rm_job_from_res(struct part_res_record *part_record_ptr,
			    struct node_use_record *node_usage,
			    struct job_record *job_ptr, int action);
static int _run_now(struct job_record *job_ptr, bitstr_t *bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    uint32_t req_nodes, uint16_t job_node_req,
		    List preemptee_candidates, List *preemptee_job_list,
		    bitstr_t *exc_core_bitmap);
static int _sort_usable_nodes_dec(struct job_record *job_a,
				  struct job_record *job_b);
static int _test_only(struct job_record *job_ptr, bitstr_t *bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
 		      uint32_t req_nodes, uint16_t job_node_req);
static int _will_run_test(struct job_record *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes, uint16_t job_node_req,
			  List preemptee_candidates, List *preemptee_job_list,
			  bitstr_t *exc_core_bitmap);

static void _dump_job_res(struct job_resources *job) {
	char str[64];

	if (job->core_bitmap)
		bit_fmt(str, sizeof(str), job->core_bitmap);
	else
		sprintf(str, "[no core_bitmap]");
	info("DEBUG: Dump job_resources: nhosts %u cb %s", job->nhosts, str);
}

static void _dump_nodes(void)
{
	struct node_record *node_ptr;
	List gres_list;
	int i;

	for (i=0; i<select_node_cnt; i++) {
		node_ptr = select_node_record[i].node_ptr;
		info("node:%s cpus:%u c:%u s:%u t:%u mem:%u a_mem:%u state:%d",
		     node_ptr->name,
		     select_node_record[i].cpus,
		     select_node_record[i].cores,
		     select_node_record[i].sockets,
		     select_node_record[i].vpus,
		     select_node_record[i].real_memory,
		     select_node_usage[i].alloc_memory,
		     select_node_usage[i].node_state);

		if (select_node_usage[i].gres_list)
			gres_list = select_node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		if (gres_list)
			gres_plugin_node_state_log(gres_list, node_ptr->name);
	}
}

static void _dump_part(struct part_res_record *p_ptr)
{
	uint16_t i;
	info("part:%s rows:%u pri:%u ", p_ptr->part_ptr->name, p_ptr->num_rows,
	     p_ptr->part_ptr->priority);
	if (!p_ptr->row)
		return;

	for (i = 0; i < p_ptr->num_rows; i++) {
		char str[64]; /* print first 64 bits of bitmaps */
		if (p_ptr->row[i].row_bitmap) {
			bit_fmt(str, sizeof(str), p_ptr->row[i].row_bitmap);
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
			new_row[i].row_bitmap= bit_copy(orig_row[i].
							row_bitmap);
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

/* delete the given row data */
static void _destroy_row_data(struct part_row_data *row, uint16_t num_rows) {
	uint16_t i;
	for (i = 0; i < num_rows; i++) {
		FREE_NULL_BITMAP(row[i].row_bitmap);
		if (row[i].job_list) {
			uint32_t j;
			for (j = 0; j < row[i].num_jobs; j++)
				row[i].job_list[j] = NULL;
			xfree(row[i].job_list);
		}
	}
	xfree(row);
}

/* delete the given list of partition data */
static void _destroy_part_data(struct part_res_record *this_ptr)
{
	while (this_ptr) {
		struct part_res_record *tmp = this_ptr;
		this_ptr = this_ptr->next;
		tmp->part_ptr = NULL;

		if (tmp->row) {
			_destroy_row_data(tmp->row, tmp->num_rows);
			tmp->row = NULL;
		}
		xfree(tmp);
	}
}


/* (re)create the global select_part_record array */
static void _create_part_data(void)
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
	info("cons_res: preparing for %d partitions", num_parts);

	select_part_record = xmalloc(sizeof(struct part_res_record));
	this_ptr = select_part_record;

	part_iterator = list_iterator_create(part_list);
	if (part_iterator == NULL)
		fatal ("memory allocation failure");

	while ((p_ptr = (struct part_record *) list_next(part_iterator))) {
		this_ptr->part_ptr = p_ptr;
		this_ptr->num_rows = p_ptr->max_share;
		if (this_ptr->num_rows & SHARED_FORCE)
			this_ptr->num_rows &= (~SHARED_FORCE);
		/* SHARED=EXCLUSIVE sets max_share = 0 */
		if (this_ptr->num_rows < 1)
			this_ptr->num_rows = 1;
		/* we'll leave the 'row' array blank for now */
		this_ptr->row = NULL;
		num_parts--;
		if (num_parts) {
			this_ptr->next =xmalloc(sizeof(struct part_res_record));
			this_ptr = this_ptr->next;
		}
	}
	list_iterator_destroy(part_iterator);

	/* should we sort the select_part_record list by priority here? */
}


/* List sort function: sort by the job's expected end time */
static int _cr_job_list_sort(void *x, void *y)
{
	struct job_record *job1_ptr = (struct job_record *) x;
	struct job_record *job2_ptr = (struct job_record *) y;
	return (int) SLURM_DIFFTIME(job1_ptr->end_time, job2_ptr->end_time);
}


/* delete the given select_node_record and select_node_usage arrays */
static void _destroy_node_data(struct node_use_record *node_usage,
			       struct node_res_record *node_data)
{
	int i;

	xfree(node_data);
	if (node_usage) {
		for (i = 0; i < select_node_cnt; i++) {
			if (node_usage[i].gres_list) {
				list_destroy(node_usage[i].gres_list);
			}
		}
		xfree(node_usage);
	}
}


static void _add_job_to_row(struct job_resources *job,
			    struct part_row_data *r_ptr)
{
	/* add the job to the row_bitmap */
	if (r_ptr->row_bitmap && r_ptr->num_jobs == 0) {
		/* if no jobs, clear the existing row_bitmap first */
		uint32_t size = bit_size(r_ptr->row_bitmap);
		bit_nclear(r_ptr->row_bitmap, 0, size-1);
	}
	add_job_to_cores(job, &(r_ptr->row_bitmap), cr_node_num_cores);

	/*  add the job to the job_list */
	if (r_ptr->num_jobs >= r_ptr->job_list_size) {
		r_ptr->job_list_size += 8;
		xrealloc(r_ptr->job_list, r_ptr->job_list_size *
			 sizeof(struct job_resources *));
	}
	r_ptr->job_list[r_ptr->num_jobs++] = job;
}


/* test for conflicting core_bitmap bits */
static int _can_job_fit_in_row(struct job_resources *job,
			       struct part_row_data *r_ptr)
{
	if ((r_ptr->num_jobs == 0) || !r_ptr->row_bitmap)
		return 1;

	return job_fits_into_cores(job, r_ptr->row_bitmap, cr_node_num_cores);
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
	uint32_t i, j, num_jobs, size;
	int x, *jstart;
	struct part_row_data *this_row, *orig_row;
	struct job_resources **tmpjobs, *job;

	if (!p_ptr->row)
		return;

	if (p_ptr->num_rows == 1) {
		this_row = &(p_ptr->row[0]);
		if (this_row->num_jobs == 0) {
			if (this_row->row_bitmap) {
				size = bit_size(this_row->row_bitmap);
				bit_nclear(this_row->row_bitmap, 0, size-1);
			}
		} else {
			xassert(job_ptr);
			xassert(job_ptr->job_resrcs);
			remove_job_from_cores(job_ptr->job_resrcs,
					      &this_row->row_bitmap,
					      cr_node_num_cores);
		}
		return;
	}

	/* gather data */
	num_jobs = 0;
	for (i = 0; i < p_ptr->num_rows; i++) {
		if (p_ptr->row[i].num_jobs) {
			num_jobs += p_ptr->row[i].num_jobs;
		}
	}
	if (num_jobs == 0) {
		size = bit_size(p_ptr->row[0].row_bitmap);
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (p_ptr->row[i].row_bitmap) {
				bit_nclear(p_ptr->row[i].row_bitmap, 0,
					   size-1);
			}
		}
		return;
	}

	if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
		info("DEBUG: _build_row_bitmaps (before):");
		_dump_part(p_ptr);
	}
	debug3("cons_res: build_row_bitmaps reshuffling %u jobs", num_jobs);

	/* make a copy, in case we cannot do better than this */
	orig_row = _dup_row_data(p_ptr->row, p_ptr->num_rows);
	if (orig_row == NULL)
		return;

	/* get row_bitmap size from first row (we can safely assume that the
	 * first row_bitmap exists because there exists at least one job. */
	size = bit_size(p_ptr->row[0].row_bitmap);

	/* create a master job list and clear out ALL row data */
	tmpjobs = xmalloc(num_jobs * sizeof(struct job_resources *));
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
			bit_nclear(p_ptr->row[i].row_bitmap, 0, size-1);
		}
	}

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
			if (jstart[j] < jstart[i]
			    || (jstart[j] == jstart[i] &&
				tmpjobs[j]->ncpus > tmpjobs[i]->ncpus)) {
				x = jstart[i];
				jstart[i] = jstart[j];
				jstart[j] = x;
				job = tmpjobs[i];
				tmpjobs[i] = tmpjobs[j];
				tmpjobs[j] = job;
			}
		}
	}

	if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
		for (i = 0; i < num_jobs; i++) {
			char cstr[64], nstr[64];
			if (tmpjobs[i]->core_bitmap) {
				bit_fmt(cstr, (sizeof(cstr)-1) ,
					tmpjobs[i]->core_bitmap);
			} else
				sprintf(cstr, "[no core_bitmap]");
			if (tmpjobs[i]->node_bitmap) {
				bit_fmt(nstr, (sizeof(nstr)-1),
					tmpjobs[i]->node_bitmap);
			} else
				sprintf(nstr, "[no node_bitmap]");
			info("DEBUG:  jstart %d job nb %s cb %s", jstart[i],
			     nstr, cstr);
		}
	}

	/* add jobs to the rows */
	for (j = 0; j < num_jobs; j++) {
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (_can_job_fit_in_row(tmpjobs[j], &(p_ptr->row[i]))) {
				/* job fits in row, so add it */
				_add_job_to_row(tmpjobs[j], &(p_ptr->row[i]));
				tmpjobs[j] = NULL;
				break;
			}
		}
		/* job should have been added, so shuffle the rows */
		cr_sort_part_rows(p_ptr);
	}

	/* test for dangling jobs */
	for (j = 0; j < num_jobs; j++) {
		if (tmpjobs[j])
			break;
	}
	if (j < num_jobs) {
		/* we found a dangling job, which means our packing
		 * algorithm couldn't improve apon the existing layout.
		 * Thus, we'll restore the original layout here */
		debug3("cons_res: build_row_bitmap: dangling job found");

		if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
			info("DEBUG: _build_row_bitmaps (post-algorithm):");
			_dump_part(p_ptr);
		}

		_destroy_row_data(p_ptr->row, p_ptr->num_rows);
		p_ptr->row = orig_row;
		orig_row = NULL;

		/* still need to rebuild row_bitmaps */
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (p_ptr->row[i].row_bitmap)
				bit_nclear(p_ptr->row[i].row_bitmap, 0,
					   size-1);
			if (p_ptr->row[i].num_jobs == 0)
				continue;
			for (j = 0; j < p_ptr->row[i].num_jobs; j++) {
				add_job_to_cores(p_ptr->row[i].job_list[j],
						 &(p_ptr->row[i].row_bitmap),
						 cr_node_num_cores);
			}
		}
	}

	if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
		info("DEBUG: _build_row_bitmaps (after):");
		_dump_part(p_ptr);
	}

	if (orig_row)
		_destroy_row_data(orig_row, p_ptr->num_rows);
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
 * - add 'struct job_resources' resources to 'struct part_res_record'
 * - add job's memory requirements to 'struct node_res_record'
 *
 * if action = 0 then add cores and memory (starting new job)
 * if action = 1 then only add memory (adding suspended job)
 * if action = 2 then only add cores (suspended job is resumed)
 */
static int _add_job_to_res(struct job_record *job_ptr, int action)
{
	struct job_resources *job = job_ptr->job_resrcs;
	struct node_record *node_ptr;
	struct part_res_record *p_ptr;
	List gres_list;
	int i, n;
	bitstr_t *core_bitmap;

	if (!job || !job->core_bitmap) {
		error("job %u has no select data", job_ptr->job_id);
		return SLURM_ERROR;
	}

	debug3("cons_res: _add_job_to_res: job %u act %d ", job_ptr->job_id,
	       action);

	if (select_debug_flags & DEBUG_FLAG_CPU_BIND)
		_dump_job_res(job);

	for (i = 0, n = -1; i < select_node_cnt; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		n++;

		node_ptr = select_node_record[i].node_ptr;
		if (action != 2) {
			if (select_node_usage[i].gres_list)
				gres_list = select_node_usage[i].gres_list;
			else
				gres_list = node_ptr->gres_list;
			core_bitmap = copy_job_resources_node(job, n);
			gres_plugin_job_alloc(job_ptr->gres_list, gres_list,
					      job->nhosts, n, job->cpus[n],
					      job_ptr->job_id, node_ptr->name,
					      core_bitmap);
			gres_plugin_node_state_log(gres_list, node_ptr->name);
			FREE_NULL_BITMAP(core_bitmap);
		}

		if (action != 2) {
			if (job->memory_allocated[n] == 0)
				continue;	/* node lost by job resizing */
			select_node_usage[i].alloc_memory +=
				job->memory_allocated[n];
			if ((select_node_usage[i].alloc_memory >
			     select_node_record[i].real_memory)) {
				error("cons_res: node %s memory is "
				      "overallocated (%u) for job %u",
				      node_ptr->name,
				      select_node_usage[i].alloc_memory,
				      job_ptr->job_id);
			}
		}
	}

	/* add cores */
	if (action != 1) {
		for (p_ptr = select_part_record; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr == job_ptr->part_ptr)
				break;
		}
		if (!p_ptr) {
			error("cons_res: could not find cr partition %s",
			      job_ptr->part_ptr->name);
			return SLURM_ERROR;
		}
		if (!p_ptr->row) {
			p_ptr->row = xmalloc(p_ptr->num_rows *
					     sizeof(struct part_row_data));
		}

		/* find a row to add this job */
		for (i = 0; i < p_ptr->num_rows; i++) {
			if (!_can_job_fit_in_row(job, &(p_ptr->row[i])))
				continue;
			debug3("cons_res: adding job %u to part %s row %u",
			       job_ptr->job_id, p_ptr->part_ptr->name, i);
			_add_job_to_row(job, &(p_ptr->row[i]));
			break;
		}
		if (i >= p_ptr->num_rows) {
			/* Job started or resumed and it's allocated resources
			 * are already in use by some other job. Typically due
			 * to manually resuming a job. */
			error("cons_res: ERROR: job overflow: "
			      "could not find idle resources for job %u",
			      job_ptr->job_id);
			/* just add the job to the last row for now */
			_add_job_to_row(job, &(p_ptr->row[p_ptr->num_rows-1]));
		}
		/* update the node state */
		for (i = 0; i < select_node_cnt; i++) {
			if (bit_test(job->node_bitmap, i))
				select_node_usage[i].node_state +=
					job->node_req;
		}
		if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
			info("DEBUG: _add_job_to_res (after):");
			_dump_part(p_ptr);
		}
	}

	return SLURM_SUCCESS;
}

static job_resources_t *_create_job_resources(int node_cnt)
{
	job_resources_t *job_resrcs_ptr;

	job_resrcs_ptr = create_job_resources();
	job_resrcs_ptr->cpu_array_reps = xmalloc(sizeof(uint32_t) * node_cnt);
	job_resrcs_ptr->cpu_array_value = xmalloc(sizeof(uint16_t) * node_cnt);
	job_resrcs_ptr->cpus = xmalloc(sizeof(uint16_t) * node_cnt);
	job_resrcs_ptr->cpus_used = xmalloc(sizeof(uint16_t) * node_cnt);
	job_resrcs_ptr->memory_allocated = xmalloc(sizeof(uint32_t) * node_cnt);
	job_resrcs_ptr->memory_used = xmalloc(sizeof(uint32_t) * node_cnt);
	job_resrcs_ptr->nhosts = node_cnt;
	return job_resrcs_ptr;
}

/* Move all resources from one job to another */
static int _job_expand(struct job_record *from_job_ptr,
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
	xassert(to_job_ptr);
	if (from_job_ptr->job_id == to_job_ptr->job_id) {
		error("select/cons_res: attempt to merge job %u with self",
		      from_job_ptr->job_id);
		return SLURM_ERROR;
	}

	from_job_resrcs_ptr = from_job_ptr->job_resrcs;
	if ((from_job_resrcs_ptr == NULL) ||
	    (from_job_resrcs_ptr->cpus == NULL) ||
	    (from_job_resrcs_ptr->core_bitmap == NULL) ||
	    (from_job_resrcs_ptr->node_bitmap == NULL)) {
		error("select/cons_res: job %u lacks a job_resources struct",
		      from_job_ptr->job_id);
		return SLURM_ERROR;
	}
	to_job_resrcs_ptr = to_job_ptr->job_resrcs;
	if ((to_job_resrcs_ptr == NULL) ||
	    (to_job_resrcs_ptr->cpus == NULL) ||
	    (to_job_resrcs_ptr->core_bitmap == NULL) ||
	    (to_job_resrcs_ptr->node_bitmap == NULL)) {
		error("select/cons_res: job %u lacks a job_resources struct",
		      to_job_ptr->job_id);
		return SLURM_ERROR;
	}

	(void) _rm_job_from_res(select_part_record, select_node_usage,
				from_job_ptr, 0);
	(void) _rm_job_from_res(select_part_record, select_node_usage,
				to_job_ptr, 0);

	if (to_job_resrcs_ptr->core_bitmap_used) {
		i = bit_size(to_job_resrcs_ptr->core_bitmap_used);
		bit_nclear(to_job_resrcs_ptr->core_bitmap_used, 0, i-1);
	}

	tmp_bitmap = bit_copy(to_job_resrcs_ptr->node_bitmap);
	if (!tmp_bitmap)
		fatal("bit_copy: malloc failure");
	bit_or(tmp_bitmap, from_job_resrcs_ptr->node_bitmap);
	tmp_bitmap2 = bit_copy(to_job_ptr->node_bitmap);
	if (!tmp_bitmap)
		fatal("bit_copy: malloc failure");
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
	build_job_resources(new_job_resrcs_ptr, node_record_table_ptr,
			    select_fast_schedule);
	xfree(to_job_ptr->node_addr);
	to_job_ptr->node_addr = xmalloc(sizeof(slurm_addr_t) * node_cnt);
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
			/* Merge alloc info from both "from" and "to" jobs,
			 * leave "from" job with no allocated CPUs or memory */
			new_job_resrcs_ptr->cpus[new_node_offset] =
				from_job_resrcs_ptr->cpus[from_node_offset];
			from_job_resrcs_ptr->cpus[from_node_offset] = 0;
			/* new_job_resrcs_ptr->cpus_used[new_node_offset] =
				from_job_resrcs_ptr->
				cpus_used[from_node_offset]; Should be 0 */
			new_job_resrcs_ptr->memory_allocated[new_node_offset] =
				from_job_resrcs_ptr->
				memory_allocated[from_node_offset];
			/* new_job_resrcs_ptr->memory_used[new_node_offset] =
				from_job_resrcs_ptr->
				memory_used[from_node_offset]; Should be 0 */
			job_resources_bits_copy(new_job_resrcs_ptr,
						new_node_offset,
						from_job_resrcs_ptr,
						from_node_offset);
		}
		if (to_node_used) {
			/* Merge alloc info from both "from" and "to" jobs */

			/* DO NOT double count the allocated CPUs in partition
			 * with Shared nodes */
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
				/* Adust cpu count for shared CPUs */
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

		to_job_ptr->total_cpus += new_job_resrcs_ptr->
					  cpus[new_node_offset];
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
	if (to_job_ptr->details) {
		to_job_ptr->details->min_cpus = to_job_ptr->total_cpus;
		to_job_ptr->details->max_cpus = to_job_ptr->total_cpus;
	}
	from_job_ptr->total_cpus   = 0;
	from_job_resrcs_ptr->ncpus = 0;
	if (from_job_ptr->details) {
		from_job_ptr->details->min_cpus = 0;
		from_job_ptr->details->max_cpus = 0;
	}

	from_job_ptr->total_nodes   = 0;
	from_job_resrcs_ptr->nhosts = 0;
	from_job_ptr->node_cnt      = 0;
	if (from_job_ptr->details)
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

	(void) _add_job_to_res(to_job_ptr, 0);

	return SLURM_SUCCESS;
}

/* deallocate resources previously allocated to the given job
 * - subtract 'struct job_resources' resources from 'struct part_res_record'
 * - subtract job's memory requirements from 'struct node_res_record'
 *
 * if action = 0 then subtract cores and memory (running job was terminated)
 * if action = 1 then only subtract memory (suspended job was terminated)
 * if action = 2 then only subtract cores (job is suspended)
 *
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
		/* Ignore job removal until select/cons_res data structures
		 * values are set by select_p_reconfigure() */
		return SLURM_SUCCESS;
	}
	if (!job || !job->core_bitmap) {
		error("job %u has no select data", job_ptr->job_id);
		return SLURM_ERROR;
	}

	debug3("cons_res: _rm_job_from_res: job %u action %d", job_ptr->job_id,
	       action);
	if (select_debug_flags & DEBUG_FLAG_CPU_BIND)
		_dump_job_res(job);

	first_bit = bit_ffs(job->node_bitmap);
	if (first_bit == -1)
		last_bit = -2;
	else
		last_bit =  bit_fls(job->node_bitmap);
	for (i = first_bit, n = -1; i <= last_bit; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		n++;

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
			if (job->memory_allocated[n] == 0)
				continue;	/* no memory allocated */
			if (node_usage[i].alloc_memory <
			    job->memory_allocated[n]) {
				error("cons_res: node %s memory is "
				      "under-allocated (%u-%u) for job %u",
				      node_ptr->name,
				      node_usage[i].alloc_memory,
				      job->memory_allocated[n],
				      job_ptr->job_id);
				node_usage[i].alloc_memory = 0;
			} else {
				node_usage[i].alloc_memory -=
					job->memory_allocated[n];
			}
		}
	}

	/* subtract cores */
	if (action != 1) {
		/* reconstruct rows with remaining jobs */
		struct part_res_record *p_ptr;

		if (!job_ptr->part_ptr) {
			error("cons_res: removed job %u does not have a "
			      "partition assigned",
			      job_ptr->job_id);
			return SLURM_ERROR;
		}

		for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
			if (p_ptr->part_ptr == job_ptr->part_ptr)
				break;
		}
		if (!p_ptr) {
			error("cons_res: removed job %u could not find part %s",
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
				debug3("cons_res: removed job %u from "
				       "part %s row %u",
				       job_ptr->job_id,
				       p_ptr->part_ptr->name, i);
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
			_build_row_bitmaps(p_ptr, job_ptr);

			/* Adjust the node_state of all nodes affected by
			 * the removal of this job. If all cores are now
			 * available, set node_state = NODE_CR_AVAILABLE
			 */
			for (i = 0, n = -1; i < select_node_cnt; i++) {
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
					error("cons_res:_rm_job_from_res: "
					      "node_state mis-count");
					node_usage[i].node_state =
						NODE_CR_AVAILABLE;
				}
			}
		}
	}

	return SLURM_SUCCESS;
}

static int _rm_job_from_one_node(struct job_record *job_ptr,
				 struct node_record *node_ptr)
{
	struct part_res_record *part_record_ptr = select_part_record;
	struct node_use_record *node_usage = select_node_usage;
	struct job_resources *job = job_ptr->job_resrcs;
	struct part_res_record *p_ptr;
	int first_bit, last_bit;
	int i, node_inx, n;
	List gres_list;

	if (!job || !job->core_bitmap) {
		error("select/cons_res: job %u has no select data",
		      job_ptr->job_id);
		return SLURM_ERROR;
	}

	debug3("cons_res: _rm_job_from_one_node: job %u node %s",
	       job_ptr->job_id, node_ptr->name);
	if (select_debug_flags & DEBUG_FLAG_CPU_BIND)
		_dump_job_res(job);

	/* subtract memory */
	node_inx  = node_ptr - node_record_table_ptr;
	first_bit = bit_ffs(job->node_bitmap);
	last_bit  = bit_fls(job->node_bitmap);
	for (i = first_bit, n = -1; i <= last_bit; i++) {
		if (!bit_test(job->node_bitmap, i))
			continue;
		n++;
		if (i != node_inx)
			continue;

		if (job->cpus[n] == 0) {
			info("attempt to remove node %s from job %u again",
			     node_ptr->name, job_ptr->job_id);
			return SLURM_SUCCESS;
		}

		if (node_usage[i].gres_list)
			gres_list = node_usage[i].gres_list;
		else
			gres_list = node_ptr->gres_list;
		gres_plugin_job_dealloc(job_ptr->gres_list, gres_list, n,
					job_ptr->job_id, node_ptr->name);
		gres_plugin_node_state_log(gres_list, node_ptr->name);

		job->cpus[n] = 0;
		job->ncpus = build_job_resources_cpu_array(job);
		clear_job_resources_node(job, n);
		if (node_usage[i].alloc_memory < job->memory_allocated[n]) {
			error("cons_res: node %s memory is underallocated "
			      "(%u-%u) for job %u",
			      node_ptr->name, node_usage[i].alloc_memory,
			      job->memory_allocated[n], job_ptr->job_id);
			node_usage[i].alloc_memory = 0;
		} else
			node_usage[i].alloc_memory -= job->memory_allocated[n];
		job->memory_allocated[n] = 0;
		break;
	}

	if (IS_JOB_SUSPENDED(job_ptr))
		return SLURM_SUCCESS;	/* No cores allocated to the job now */

	/* subtract cores, reconstruct rows with remaining jobs */
	if (!job_ptr->part_ptr) {
		error("cons_res: removed job %u does not have a partition "
		      "assigned", job_ptr->job_id);
		return SLURM_ERROR;
	}

	for (p_ptr = part_record_ptr; p_ptr; p_ptr = p_ptr->next) {
		if (p_ptr->part_ptr == job_ptr->part_ptr)
			break;
	}
	if (!p_ptr) {
		error("cons_res: removed job %u could not find part %s",
		      job_ptr->job_id, job_ptr->part_ptr->name);
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
			debug3("cons_res: found job %u in part %s row %u",
			       job_ptr->job_id, p_ptr->part_ptr->name, i);
			/* found job - we're done, don't actually remove */
			n = 1;
			i = p_ptr->num_rows;
			break;
		}
	}
	if (n == 0) {
		error("cons_res: could not find job %u in partition %s",
		      job_ptr->job_id, p_ptr->part_ptr->name);
		return SLURM_ERROR;
	}


	/* job was found and removed from core-bitmap, so refresh CR bitmaps */
	_build_row_bitmaps(p_ptr, job_ptr);

	/* Adjust the node_state of the node removed from this job.
	 * If all cores are now available, set node_state = NODE_CR_AVAILABLE */
	if (node_usage[node_inx].node_state >= job->node_req) {
		node_usage[node_inx].node_state -= job->node_req;
	} else {
		error("cons_res:_rm_job_from_one_node: node_state miscount");
		node_usage[node_inx].node_state = NODE_CR_AVAILABLE;
	}

	return SLURM_SUCCESS;
}

static struct multi_core_data * _create_default_mc(void)
{
	struct multi_core_data *mc_ptr;
	mc_ptr = xmalloc(sizeof(struct multi_core_data));
	mc_ptr->sockets_per_node = (uint16_t) NO_VAL;
	mc_ptr->cores_per_socket = (uint16_t) NO_VAL;
	mc_ptr->threads_per_core = (uint16_t) NO_VAL;
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
static uint16_t _get_job_node_req(struct job_record *job_ptr)
{
	int max_share = job_ptr->part_ptr->max_share;

	if (max_share == 0)		    /* Partition Shared=EXCLUSIVE */
		return NODE_CR_RESERVED;

	/* Partition is Shared=FORCE */
	if (max_share & SHARED_FORCE)
		return NODE_CR_AVAILABLE;

	/* Partition is Shared=NO or Shared=YES */
	if (job_ptr->details->shared == 0)
		/* user has requested exclusive nodes */
		return NODE_CR_RESERVED;

	if ((max_share > 1) && (job_ptr->details->shared == 1))
		/* part allows sharing, and the user has requested it */
		return NODE_CR_AVAILABLE;

	return NODE_CR_ONE_ROW;
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

/* Determine if a job can ever run */
static int _test_only(struct job_record *job_ptr, bitstr_t *bitmap,
		      uint32_t min_nodes, uint32_t max_nodes,
		      uint32_t req_nodes, uint16_t job_node_req)
{
	int rc;

	rc = cr_job_test(job_ptr, bitmap, min_nodes, max_nodes, req_nodes,
			 SELECT_MODE_TEST_ONLY, cr_type, job_node_req,
			 select_node_cnt, select_part_record,
			 select_node_usage, NULL);
	return rc;
}

/*
 * Sort the usable_node element to put jobs in the correct
 * preemption order.
 */
static int _sort_usable_nodes_dec(struct job_record *job_a,
				  struct job_record *job_b)
{
	if (job_a->details->usable_nodes > job_b->details->usable_nodes)
		return -1;
	else if (job_a->details->usable_nodes < job_b->details->usable_nodes)
		return 1;

	return 0;
}

/* Allocate resources for a job now, if possible */
static int _run_now(struct job_record *job_ptr, bitstr_t *bitmap,
		    uint32_t min_nodes, uint32_t max_nodes,
		    uint32_t req_nodes, uint16_t job_node_req,
		    List preemptee_candidates, List *preemptee_job_list,
		    bitstr_t *exc_core_bitmap)
{
	int rc;
	bitstr_t *orig_map = NULL, *save_bitmap;
	struct job_record *tmp_job_ptr;
	ListIterator job_iterator, preemptee_iterator;
	struct part_res_record *future_part;
	struct node_use_record *future_usage;
	bool remove_some_jobs = false;
	uint16_t pass_count = 0;
	uint16_t mode;

	save_bitmap = bit_copy(bitmap);
top:	orig_map = bit_copy(save_bitmap);
	if (!orig_map)
		fatal("bit_copy: malloc failure");

	rc = cr_job_test(job_ptr, bitmap, min_nodes, max_nodes, req_nodes,
			 SELECT_MODE_RUN_NOW, cr_type, job_node_req,
			 select_node_cnt, select_part_record,
			 select_node_usage, exc_core_bitmap);

	if ((rc != SLURM_SUCCESS) && preemptee_candidates) {
		/* Remove preemptable jobs from simulated environment */
		future_part = _dup_part_data(select_part_record);
		if (future_part == NULL) {
			FREE_NULL_BITMAP(orig_map);
			FREE_NULL_BITMAP(save_bitmap);
			return SLURM_ERROR;
		}
		future_usage = _dup_node_usage(select_node_usage);
		if (future_usage == NULL) {
			_destroy_part_data(future_part);
			FREE_NULL_BITMAP(orig_map);
			FREE_NULL_BITMAP(save_bitmap);
			return SLURM_ERROR;
		}

		job_iterator = list_iterator_create(preemptee_candidates);
		if (job_iterator == NULL)
			fatal ("memory allocation failure");
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
			bit_or(bitmap, orig_map);
			rc = cr_job_test(job_ptr, bitmap, min_nodes,
					 max_nodes, req_nodes,
					 SELECT_MODE_WILL_RUN,
					 cr_type, job_node_req,
					 select_node_cnt,
					 future_part, future_usage,
					 exc_core_bitmap);
			tmp_job_ptr->details->usable_nodes = 0;
			/*
			 * If successful, set the last job's usable count to a
			 * large value so that it will be first after sorting.
			 * usable_nodes count set to zero above to eliminate
			 * values previously set to 9999.
			 * Note: usable_count is only used for sorting purposes
			 */
			if (rc == SLURM_SUCCESS) {
				tmp_job_ptr->details->usable_nodes = 9999;
				list_iterator_reset(job_iterator);
				while ((tmp_job_ptr = (struct job_record *)
					list_next(job_iterator))) {
					if (tmp_job_ptr->details->usable_nodes
					    == 9999)
						break;
					tmp_job_ptr->details->usable_nodes =
						bit_overlap(bitmap,
							    tmp_job_ptr->
							    node_bitmap);
				}
				while ((tmp_job_ptr = (struct job_record *)
					list_next(job_iterator))) {
					tmp_job_ptr->details->usable_nodes = 0;
				}
				if (pass_count++ ||
				    (list_count(preemptee_candidates) == 1))
					break;
				list_sort(preemptee_candidates,
					  (ListCmpF)_sort_usable_nodes_dec);
				FREE_NULL_BITMAP(orig_map);
				list_iterator_destroy(job_iterator);
				goto top;
			}
		}
		list_iterator_destroy(job_iterator);

		if ((rc == SLURM_SUCCESS) && preemptee_job_list &&
		    preemptee_candidates) {
			/* Build list of preemptee jobs whose resources are
			 * actually used */
			if (*preemptee_job_list == NULL) {
				*preemptee_job_list = list_create(NULL);
				if (*preemptee_job_list == NULL)
					fatal("list_create malloc failure");
			}
			preemptee_iterator = list_iterator_create(
				preemptee_candidates);
			if (preemptee_iterator == NULL)
				fatal ("memory allocation failure");
			while ((tmp_job_ptr = (struct job_record *)
				list_next(preemptee_iterator))) {
				mode = slurm_job_preempt_mode(tmp_job_ptr);
				if ((mode != PREEMPT_MODE_REQUEUE)    &&
				    (mode != PREEMPT_MODE_CHECKPOINT) &&
				    (mode != PREEMPT_MODE_CANCEL))
					continue;
				if (tmp_job_ptr->details->usable_nodes == 0)
					continue;
				list_append(*preemptee_job_list,
					    tmp_job_ptr);
				remove_some_jobs = true;
			}
			list_iterator_destroy(preemptee_iterator);
			if (!remove_some_jobs) {
				list_destroy(*preemptee_job_list);
				*preemptee_job_list = NULL;
			}
		}

		_destroy_part_data(future_part);
		_destroy_node_data(future_usage, NULL);
	}
	FREE_NULL_BITMAP(orig_map);
	FREE_NULL_BITMAP(save_bitmap);

	return rc;
}

/* _will_run_test - determine when and where a pending job can start, removes
 *	jobs from node table at termination time and run _test_job() after
 *	each one. Used by SLURM's sched/backfill plugin and Moab. */
static int _will_run_test(struct job_record *job_ptr, bitstr_t *bitmap,
			  uint32_t min_nodes, uint32_t max_nodes,
			  uint32_t req_nodes, uint16_t job_node_req,
			  List preemptee_candidates, List *preemptee_job_list,
			  bitstr_t *exc_core_bitmap)
{
	struct part_res_record *future_part;
	struct node_use_record *future_usage;
	struct job_record *tmp_job_ptr;
	List cr_job_list;
	ListIterator job_iterator, preemptee_iterator;
	bitstr_t *orig_map;
	int action, rc = SLURM_ERROR;
	time_t now = time(NULL);

	orig_map = bit_copy(bitmap);
	if (!orig_map)
		fatal("bit_copy: malloc failure");

	/* Try to run with currently available nodes */
	rc = cr_job_test(job_ptr, bitmap, min_nodes, max_nodes, req_nodes,
			 SELECT_MODE_WILL_RUN, cr_type, job_node_req,
			 select_node_cnt, select_part_record,
			 select_node_usage, exc_core_bitmap);
	if (rc == SLURM_SUCCESS) {
		FREE_NULL_BITMAP(orig_map);
		job_ptr->start_time = time(NULL);
		return SLURM_SUCCESS;
	}

	/* Job is still pending. Simulate termination of jobs one at a time
	 * to determine when and where the job can start. */
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
	if (!cr_job_list)
		fatal("list_create: memory allocation error");
	job_iterator = list_iterator_create(job_list);
	if (job_iterator == NULL)
		fatal ("memory allocation failure");
	while ((tmp_job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(tmp_job_ptr) &&
		    !IS_JOB_SUSPENDED(tmp_job_ptr))
			continue;
		if (tmp_job_ptr->end_time == 0) {
			error("Job %u has zero end_time", tmp_job_ptr->job_id);
			continue;
		}
		if (_is_preemptable(tmp_job_ptr, preemptee_candidates)) {
			uint16_t mode = slurm_job_preempt_mode(tmp_job_ptr);
			if (mode == PREEMPT_MODE_OFF)
				continue;
			if (mode == PREEMPT_MODE_SUSPEND)
				action = 2;	/* remove cores, keep memory */
			else
				action = 0;	/* remove cores and memory */
			/* Remove preemptable job now */
			_rm_job_from_res(future_part, future_usage,
					 tmp_job_ptr, action);
		} else
			list_append(cr_job_list, tmp_job_ptr);
	}
	list_iterator_destroy(job_iterator);

	/* Test with all preemptable jobs gone */
	if (preemptee_candidates) {
		bit_or(bitmap, orig_map);
		rc = cr_job_test(job_ptr, bitmap, min_nodes, max_nodes,
				 req_nodes, SELECT_MODE_WILL_RUN, cr_type,
				 job_node_req, select_node_cnt, future_part,
				 future_usage, exc_core_bitmap);
		if (rc == SLURM_SUCCESS)
			job_ptr->start_time = now + 1;
	}

	/* Remove the running jobs one at a time from exp_node_cr and try
	 * scheduling the pending job after each one. */
	if (rc != SLURM_SUCCESS) {
		list_sort(cr_job_list, _cr_job_list_sort);
		job_iterator = list_iterator_create(cr_job_list);
		if (job_iterator == NULL)
			fatal ("memory allocation failure");
		while ((tmp_job_ptr = list_next(job_iterator))) {
		        int ovrlap;
			bit_or(bitmap, orig_map);
			ovrlap = bit_overlap(bitmap, tmp_job_ptr->node_bitmap);
			if (ovrlap == 0)	/* job has no usable nodes */
				continue;	/* skip it */
			debug2("cons_res: _will_run_test, job %u: overlap=%d",
			       tmp_job_ptr->job_id, ovrlap);
			_rm_job_from_res(future_part, future_usage,
					 tmp_job_ptr, 0);
			rc = cr_job_test(job_ptr, bitmap, min_nodes,
					 max_nodes, req_nodes,
					 SELECT_MODE_WILL_RUN, cr_type,
					 job_node_req, select_node_cnt,
					 future_part, future_usage,
					 exc_core_bitmap);
			if (rc == SLURM_SUCCESS) {
				if (tmp_job_ptr->end_time <= now)
					job_ptr->start_time = now + 1;
				else
					job_ptr->start_time = tmp_job_ptr->
						end_time;
				break;
			}
		}
		list_iterator_destroy(job_iterator);
	}

	if ((rc == SLURM_SUCCESS) && preemptee_job_list &&
	    preemptee_candidates) {
		/* Build list of preemptee jobs whose resources are
		 * actually used. List returned even if not killed
		 * in selected plugin, but by Moab or something else. */
		if (*preemptee_job_list == NULL) {
			*preemptee_job_list = list_create(NULL);
			if (*preemptee_job_list == NULL)
				fatal("list_create malloc failure");
		}
		preemptee_iterator =list_iterator_create(preemptee_candidates);
		if (preemptee_iterator == NULL)
			fatal ("memory allocation failure");
		while ((tmp_job_ptr = (struct job_record *)
			list_next(preemptee_iterator))) {
			if (bit_overlap(bitmap,
					tmp_job_ptr->node_bitmap) == 0)
				continue;
			list_append(*preemptee_job_list, tmp_job_ptr);
		}
		list_iterator_destroy(preemptee_iterator);
	}

	list_destroy(cr_job_list);
	_destroy_part_data(future_part);
	_destroy_node_data(future_usage, NULL);
	FREE_NULL_BITMAP(orig_map);
	return rc;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init(void)
{
	cr_type = slurmctld_conf.select_type_param;
	if (cr_type)
		verbose("%s loaded with argument %u", plugin_name, cr_type);
	select_debug_flags = slurm_get_debug_flags();

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	_destroy_node_data(select_node_usage, select_node_record);
	select_node_record = NULL;
	select_node_usage = NULL;
	_destroy_part_data(select_part_record);
	select_part_record = NULL;
	cr_fini_global_core_data();

	if (cr_type)
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
	int i, tot_core;

	info("cons_res: select_p_node_init");
	if ((cr_type & (CR_CPU | CR_SOCKET | CR_CORE)) == 0) {
		fatal("Invalid SelectTypeParameter: %s",
		      sched_param_type_string(cr_type));
	}
	if (node_ptr == NULL) {
		error("select_p_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}
	if (node_cnt < 0) {
		error("select_p_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	/* initial global core data structures */
	select_state_initializing = true;
	select_fast_schedule = slurm_get_fast_schedule();
	cr_init_global_core_data(node_ptr, node_cnt, select_fast_schedule);

	_destroy_node_data(select_node_usage, select_node_record);
	select_node_cnt  = node_cnt;
	select_node_record = xmalloc(node_cnt *
				     sizeof(struct node_res_record));
	select_node_usage  = xmalloc(node_cnt *
				     sizeof(struct node_use_record));

	for (i = 0; i < select_node_cnt; i++) {
		select_node_record[i].node_ptr = &node_ptr[i];
		if (select_fast_schedule) {
			struct config_record *config_ptr;
			config_ptr = node_ptr[i].config_ptr;
			select_node_record[i].cpus    = config_ptr->cpus;
			select_node_record[i].boards  = config_ptr->boards;
			select_node_record[i].sockets = config_ptr->sockets;
			select_node_record[i].cores   = config_ptr->cores;
			select_node_record[i].vpus    = config_ptr->threads;
			select_node_record[i].real_memory = config_ptr->
				real_memory;
		} else {
			select_node_record[i].cpus    = node_ptr[i].cpus;
			select_node_record[i].boards  = node_ptr[i].boards;
			select_node_record[i].sockets = node_ptr[i].sockets;
			select_node_record[i].cores   = node_ptr[i].cores;
			select_node_record[i].vpus    = node_ptr[i].threads;
			select_node_record[i].real_memory = node_ptr[i].
				real_memory;
		}
		tot_core = select_node_record[i].sockets *
			   select_node_record[i].cores;
		if (tot_core >= select_node_record[i].cpus)
			select_node_record[i].vpus = 1;
		select_node_usage[i].node_state = NODE_CR_AVAILABLE;
		gres_plugin_node_state_dealloc_all(select_node_record[i].
						   node_ptr->gres_list);
	}
	_create_part_data();

	return SLURM_SUCCESS;
}

extern int select_p_block_init(List part_list)
{
	return SLURM_SUCCESS;
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
 * IN preemptee_candidates - List of pointers to jobs which can be preempted.
 * IN/OUT preemptee_job_list - Pointer to list of job pointers. These are the
 *		jobs to be preempted to initiate the pending job. Not set
 *		if mode=SELECT_MODE_TEST_ONLY or input pointer is NULL.
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
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t * bitmap,
			     uint32_t min_nodes, uint32_t max_nodes,
			     uint32_t req_nodes, uint16_t mode,
			     List preemptee_candidates,
			     List *preemptee_job_list,
			     bitstr_t *exc_core_bitmap)
{
	int rc = EINVAL;
	uint16_t job_node_req;
	bool debug_cpu_bind = false, debug_check = false;

	xassert(bitmap);

	debug2("select_p_job_test for job %u", job_ptr->job_id);
	if (!debug_check) {
		debug_check = true;
		if (slurm_get_debug_flags() & DEBUG_FLAG_CPU_BIND)
			debug_cpu_bind = true;
	}

	if (!job_ptr->details)
		return EINVAL;

	if (!job_ptr->details->mc_ptr)
		job_ptr->details->mc_ptr = _create_default_mc();
	job_node_req = _get_job_node_req(job_ptr);

	if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
		info("cons_res: select_p_job_test: job %u node_req %u mode %d",
		     job_ptr->job_id, job_node_req, mode);
		info("cons_res: select_p_job_test: min_n %u max_n %u req_n %u "
		     "avail_n %u",
	 	     min_nodes, max_nodes, req_nodes, bit_set_count(bitmap));
		_dump_state(select_part_record);
	}
	if (mode == SELECT_MODE_WILL_RUN) {
		rc = _will_run_test(job_ptr, bitmap, min_nodes, max_nodes,
				    req_nodes, job_node_req,
				    preemptee_candidates, preemptee_job_list,
				    exc_core_bitmap);
	} else if (mode == SELECT_MODE_TEST_ONLY) {
		rc = _test_only(job_ptr, bitmap, min_nodes, max_nodes,
				req_nodes, job_node_req);
	} else if (mode == SELECT_MODE_RUN_NOW) {
		rc = _run_now(job_ptr, bitmap, min_nodes, max_nodes,
			      req_nodes, job_node_req,
			      preemptee_candidates, preemptee_job_list,
			      exc_core_bitmap);
	} else
		fatal("select_p_job_test: Mode %d is invalid", mode);

	if (select_debug_flags & DEBUG_FLAG_CPU_BIND) {
		if (job_ptr->job_resrcs)
			log_job_resources(job_ptr->job_id, job_ptr->job_resrcs);
		else {
			info("no job_resources info for job %u",
			     job_ptr->job_id);
		}
	} else if (debug_cpu_bind && job_ptr->job_resrcs) {
		log_job_resources(job_ptr->job_id, job_ptr->job_resrcs);
	}

	return rc;
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/* Determine if allocated nodes are usable (powered up) */
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

	for (i=i_first; i<=i_last; i++) {
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		node_ptr = node_record_table_ptr + i;
		if (IS_NODE_POWER_SAVE(node_ptr) || IS_NODE_POWER_UP(node_ptr))
			return 0;
	}

	return READY_NODE_STATE;
}

extern int select_p_job_resized(struct job_record *job_ptr,
				struct node_record *node_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	_rm_job_from_one_node(job_ptr, node_ptr);
	return SLURM_SUCCESS;
}

extern bool select_p_job_expand_allow(void)
{
	return true;
}

extern int select_p_job_expand(struct job_record *from_job_ptr,
			       struct job_record *to_job_ptr)
{
	xassert(from_job_ptr);
	xassert(from_job_ptr->magic == JOB_MAGIC);
	xassert(to_job_ptr);
	xassert(to_job_ptr->magic == JOB_MAGIC);

	return _job_expand(from_job_ptr, to_job_ptr);
}

extern int select_p_job_signal(struct job_record *job_ptr, int signal)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	_rm_job_from_res(select_part_record, select_node_usage, job_ptr, 0);

	return SLURM_SUCCESS;
}

/* NOTE: This function is not called with gang scheduling because it
 * needs to track how many jobs are running or suspended on each node.
 * This sum is compared with the partition's Shared parameter */
extern int select_p_job_suspend(struct job_record *job_ptr, bool indf_susp)
{
	xassert(job_ptr);

	if (!indf_susp)
		return SLURM_SUCCESS;

	return _rm_job_from_res(select_part_record, select_node_usage,
				job_ptr, 2);
}

/* See NOTE with select_p_job_suspend above */
extern int select_p_job_resume(struct job_record *job_ptr, bool indf_susp)
{
	xassert(job_ptr);

	if (!indf_susp)
		return SLURM_SUCCESS;

	return _add_job_to_res(job_ptr, 2);
}


extern bitstr_t *select_p_step_pick_nodes(struct job_record *job_ptr,
					  select_jobinfo_t *jobinfo,
					  uint32_t node_count)
{
	return NULL;
}

extern int select_p_step_finish(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_pack_select_info(time_t last_query_time,
				     uint16_t show_flags, Buf *buffer_ptr,
				     uint16_t protocol_version)
{
	/* This function is always invalid on normal Linux clusters */
	return SLURM_ERROR;
}

extern int select_p_select_nodeinfo_pack(select_nodeinfo_t *nodeinfo,
					 Buf buffer,
					 uint16_t protocol_version)
{
	pack16(nodeinfo->alloc_cpus, buffer);

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_unpack(select_nodeinfo_t **nodeinfo,
					   Buf buffer,
					   uint16_t protocol_version)
{
	select_nodeinfo_t *nodeinfo_ptr = NULL;

	nodeinfo_ptr = select_p_select_nodeinfo_alloc();
	*nodeinfo = nodeinfo_ptr;

	safe_unpack16(&nodeinfo_ptr->alloc_cpus, buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("select_nodeinfo_unpack: error unpacking here");
	select_p_select_nodeinfo_free(nodeinfo_ptr);
	*nodeinfo = NULL;

	return SLURM_ERROR;
}

extern select_nodeinfo_t *select_p_select_nodeinfo_alloc(void)
{
	select_nodeinfo_t *nodeinfo = xmalloc(sizeof(struct select_nodeinfo));

	nodeinfo->magic = NODEINFO_MAGIC;

	return nodeinfo;
}

extern int select_p_select_nodeinfo_free(select_nodeinfo_t *nodeinfo)
{
	if (nodeinfo) {
		if (nodeinfo->magic != NODEINFO_MAGIC) {
			error("select_p_select_nodeinfo_free: "
			      "nodeinfo magic bad");
			return EINVAL;
		}
		nodeinfo->magic = 0;
		xfree(nodeinfo);
	}
	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set_all(void)
{
	struct part_res_record *p_ptr;
	struct node_record *node_ptr = NULL;
	int i=0, n=0, c, start, end;
	uint16_t tmp, tmp_16 = 0;
	static time_t last_set_all = 0;
	uint32_t node_threads, node_cpus;
	select_nodeinfo_t *nodeinfo = NULL;

	/* only set this once when the last_node_update is newer than
	 * the last time we set things up. */
	if (last_set_all && (last_node_update < last_set_all)) {
		debug2("Node select info for set all hasn't "
		       "changed since %ld",
		       (long)last_set_all);
		return SLURM_NO_CHANGE_IN_DATA;
	}
	last_set_all = last_node_update;

	for (n=0; n < select_node_cnt; n++) {
		node_ptr = &(node_record_table_ptr[n]);

		/* We have to use the '_g_' here to make sure we get
		   the correct data to work on.  i.e. cray calls this
		   plugin from within select/cray which has it's own
		   struct.
		*/
		select_g_select_nodeinfo_get(node_ptr->select_nodeinfo,
					     SELECT_NODEDATA_PTR, 0,
					     (void *)&nodeinfo);
		if(!nodeinfo) {
			error("no nodeinfo returned from structure");
			continue;
		}

		if (slurmctld_conf.fast_schedule) {
			node_cpus    = node_ptr->config_ptr->cpus;
			node_threads = node_ptr->config_ptr->threads;
		} else {
			node_cpus    = node_ptr->cpus;
			node_threads = node_ptr->threads;
		}

		start = cr_get_coremap_offset(n);
		end = cr_get_coremap_offset(n+1);
		tmp_16 = 0;
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
				/* get the row with the largest cpu
				   count on it. */
				if (tmp > tmp_16)
					tmp_16 = tmp;
			}
		}

		/* The minimum allocatable unit may a core, so scale
		 * threads up to the proper CPU count */
		if ((end - start) < node_cpus)
			tmp_16 *= node_threads;

		nodeinfo->alloc_cpus = tmp_16;
	}

	return SLURM_SUCCESS;
}

extern int select_p_select_nodeinfo_set(struct job_record *job_ptr)
{
	int rc;
	xassert(job_ptr);
	xassert(job_ptr->magic == JOB_MAGIC);

	if (!IS_JOB_RUNNING(job_ptr) && !IS_JOB_SUSPENDED(job_ptr))
		return SLURM_SUCCESS;

	rc = _add_job_to_res(job_ptr, 0);
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
	char **tmp_char = (char **) data;
	select_nodeinfo_t **select_nodeinfo = (select_nodeinfo_t **) data;

	if (nodeinfo == NULL) {
		error("get_nodeinfo: nodeinfo not set");
		return SLURM_ERROR;
	}

	if (nodeinfo->magic != NODEINFO_MAGIC) {
		error("get_nodeinfo: jobinfo magic bad");
		return SLURM_ERROR;
	}

	switch (dinfo) {
	case SELECT_NODEDATA_SUBGRP_SIZE:
		*uint16 = 0;
		break;
	case SELECT_NODEDATA_SUBCNT:
		if (state == NODE_STATE_ALLOCATED)
			*uint16 = nodeinfo->alloc_cpus;
		else
			*uint16 = 0;
		break;
	case SELECT_NODEDATA_PTR:
		*select_nodeinfo = nodeinfo;
		break;
	case SELECT_NODEDATA_RACK_MP:
	case SELECT_NODEDATA_EXTRA_INFO:
		*tmp_char = NULL;
		break;
	default:
		error("Unsupported option %d for get_nodeinfo.", dinfo);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

extern int select_p_select_jobinfo_alloc(void)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_free(select_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_set(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_get(select_jobinfo_t *jobinfo,
				       enum select_jobdata_type data_type,
				       void *data)
{
	return SLURM_ERROR;
}

extern select_jobinfo_t *select_p_select_jobinfo_copy(
	select_jobinfo_t *jobinfo)
{
	return NULL;
}

extern int select_p_select_jobinfo_pack(select_jobinfo_t *jobinfo, Buf buffer,
					uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern int select_p_select_jobinfo_unpack(select_jobinfo_t *jobinfo,
					  Buf buffer,
					  uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern char *select_p_select_jobinfo_sprint(select_jobinfo_t *jobinfo,
					    char *buf, size_t size, int mode)
{
	if (buf && size) {
		buf[0] = '\0';
		return buf;
	} else
		return NULL;
}

extern char *select_p_select_jobinfo_xstrdup(
	select_jobinfo_t *jobinfo, int mode)
{
	return NULL;
}

extern int select_p_update_block (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_update_sub_node (update_part_msg_t *part_desc_ptr)
{
	return SLURM_SUCCESS;
}

extern int select_p_fail_cnode(struct step_record *step_ptr)
{
	return SLURM_SUCCESS;
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
		*tmp_32 = 1;
		break;
	case SELECT_CONFIG_INFO:
		*tmp_list = NULL;
		break;
	default:
		error("select_p_get_info_from_plugin info %d invalid",
		      info);
		rc = SLURM_ERROR;
		break;
	}
	return rc;
}

/* For right now, we just update the node's memory size. In order to update
 * socket, core, thread or cpu count, we would need to rebuild many bitmaps. */
extern int select_p_update_node_config (int index)
{
	if (index >= select_node_cnt) {
		error("select_p_update_node_config: index too large %d>%d",
		      index, select_node_cnt);
		return SLURM_ERROR;
	}

	if (select_fast_schedule)
		return SLURM_SUCCESS;

	select_node_record[index].real_memory = select_node_record[index].
		node_ptr->real_memory;
	return SLURM_SUCCESS;
}

extern int select_p_update_node_state(struct node_record *node_ptr)
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
	select_debug_flags = slurm_get_debug_flags();

	/* Rebuild the global data structures */
	job_preemption_enabled = false;
	job_preemption_killing = false;
	job_preemption_tested  = false;
	rc = select_p_node_init(node_record_table_ptr, node_record_count);
	if (rc != SLURM_SUCCESS)
		return rc;

	/* reload job data */
	job_iterator = list_iterator_create(job_list);
	if (job_iterator == NULL)
		fatal ("memory allocation failure");
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (IS_JOB_RUNNING(job_ptr)) {
			/* add the job */
			_add_job_to_res(job_ptr, 0);
		} else if (IS_JOB_SUSPENDED(job_ptr)) {
			/* add the job in a suspended state */
			_add_job_to_res(job_ptr, 2);
		}
	}
	list_iterator_destroy(job_iterator);
	select_state_initializing = false;

	return SLURM_SUCCESS;
}

/* given an "avail" node_bitmap, return a corresponding "avail" core_bitmap */
/* DUPLICATE CODE: see job_test.c */
/* Adding a filter for setting cores based on avail bitmap */
bitstr_t *_make_core_bitmap_filtered(bitstr_t *node_map, int filter)
{
	uint32_t n, c, nodes, size;
	uint32_t coff;

	nodes = bit_size(node_map);
	size = cr_get_coremap_offset(nodes);
	bitstr_t *core_map = bit_alloc(size);
	if (!core_map)
		return NULL;

	if (!filter)
		return core_map;

	nodes = bit_size(node_map);
	for (n = 0; n < nodes; n++) {
		if (bit_test(node_map, n)) {
			c = cr_get_coremap_offset(n);
			coff = cr_get_coremap_offset(n+1);
			while (c < coff) {
				bit_set(core_map, c++);
			}
		}
	}
	return core_map;
}

/* Once here, if core_cnt is NULL, avail_bitmap has nodes not used by any job or
 * reservation */
bitstr_t *sequential_pick(bitstr_t *avail_bitmap, uint32_t node_cnt,
			  uint32_t *core_cnt, bitstr_t **core_bitmap)
{
	bitstr_t *sp_avail_bitmap;
	char str[300];
	uint32_t cores_per_node;
	bitstr_t *tmpcore;
	int total_core_cnt = 0;

	/* We have these cases here:
	 *	1) Reservation requests using just number of nodes
	 *		- core_cnt is null
	 *	2) Reservations request using  number of nodes + number of cores
	 *	3) Reservations request using node list
	 *		- node_cnt is 0
	 *		- core_cnt is null
	 *	4) Reservation request using node list + number of cores list
	 *		- node_cnt is 0
	 */

	if ((node_cnt) && (core_cnt)) {
		debug2("reserving %u cores per node in %d nodes",
			cores_per_node, node_cnt);
		total_core_cnt = core_cnt[0];
		cores_per_node = core_cnt[0] / MAX(node_cnt, 1);
	}
	if ((!node_cnt) && (core_cnt)) {
		int num_nodes = bit_set_count(avail_bitmap);
		int i;
		bit_fmt(str, (sizeof(str) - 1), avail_bitmap);
		debug2("Reserving cores from nodes: %s", str);
		for (i=0; i < num_nodes; i++)
			total_core_cnt += core_cnt[i];
	}

	debug2("Reservations requires %d cores", total_core_cnt);

	sp_avail_bitmap = bit_alloc(bit_size(avail_bitmap));
	if (sp_avail_bitmap == NULL) {
		fatal ("memory allocation failure");
	}

	bit_fmt(str, (sizeof(str) - 1), avail_bitmap);
	bit_fmt(str, (sizeof(str) - 1), sp_avail_bitmap);

	if (core_cnt) { /* Reservation is using partial nodes */
		debug2("Reservation is using partial nodes");

		/* if not NULL = Cores used by other core based reservations
		 * overlapping in time with this one */
		if (*core_bitmap == NULL) {
			*core_bitmap = _make_core_bitmap_filtered(avail_bitmap,
								  0);
		}
		tmpcore = bit_copy(*core_bitmap);

		bit_not(tmpcore); /* tmpcore contains now current free cores */
		bit_fmt(str, (sizeof(str) - 1), tmpcore);
		debug2("tmpcore contains just current free cores: %s", str);
		bit_and(*core_bitmap, tmpcore);	/* clear core_bitmap */

		while (total_core_cnt) {
			int inx, coff, coff2;
			int i;
			int cores_in_node;
			int local_cores;
			int node_list_inx;

			if (node_cnt == 0)
				cores_per_node = core_cnt[node_list_inx];

			inx = bit_ffs(avail_bitmap);
			if (inx < 0) {
				info("reservation request can not be satisfied");
				FREE_NULL_BITMAP(sp_avail_bitmap);
				FREE_NULL_BITMAP(tmpcore);
				return NULL;
			}
			debug2("Using node %d", inx);

			coff = cr_get_coremap_offset(inx);
			coff2 = cr_get_coremap_offset(inx + 1);
			local_cores = coff2 - coff;

			bit_clear(avail_bitmap, inx);

			if (local_cores < cores_per_node)
				continue;

			cores_in_node = 0;

			/* First let's see in there are enough cores in 
			 * this node */
			for (i = 0; i < local_cores; i++) {
				if (bit_test(tmpcore, coff + i))
					cores_in_node++;
			}
			if (cores_in_node < cores_per_node)
				continue;

			debug2("Using node %d (avail: %d, needed: %d)", 
				inx, cores_in_node, cores_per_node);

			cores_in_node = 0;
			for (i = 0; i < local_cores; i++) {
				if (bit_test(tmpcore, coff + i)) {
					bit_set(*core_bitmap, coff + i);
					total_core_cnt--;
					cores_in_node++;
					if ((cores_in_node == cores_per_node) ||
					    (total_core_cnt == 0))
						break;
				}
			}

			if (cores_in_node) {
				/* Add this node to the final node bitmap */
				debug2("Reservation using %d cores in node %d",
				       cores_in_node, inx);
				bit_set(sp_avail_bitmap, inx);
			} else {
				debug2("Reservation NOT using node %d", inx);
			}
			node_list_inx++;

		}
		FREE_NULL_BITMAP(tmpcore);

		bit_fmt(str, (sizeof(str) - 1), *core_bitmap);
		info("sequential pick using coremap: %s", str);

		if (total_core_cnt) {
			info("reservation request can not be satisfied");
			FREE_NULL_BITMAP(sp_avail_bitmap);
			return NULL;
		}

	} else { /* Reservation is using full nodes */

		while (node_cnt) {
			int inx;

			inx = bit_ffs(avail_bitmap);
			if (inx < 0)
				break;

			/* Add this node to the final node bitmap */
			bit_set(sp_avail_bitmap, inx);
			node_cnt--;

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_bitmap, inx);
		}

		if (node_cnt) {
			info("reservation request can not be satisfied");
			FREE_NULL_BITMAP(sp_avail_bitmap);
			return NULL;
		}

		bit_fmt(str, (sizeof(str) - 1), sp_avail_bitmap);
		info("sequential pick using nodemap: %s", str);
	}

	return sp_avail_bitmap;
}

static int _get_avail_core_in_node(bitstr_t *core_bitmap, int node)
{
	int coff;
	int total_cores;
	int i;
	int avail = 0;

	coff = cr_get_coremap_offset(node);
	total_cores = cr_node_num_cores[node];

	if (!core_bitmap)
		return total_cores;

	for (i = 0; i < total_cores; i++) {
		if (!bit_test(core_bitmap, coff + i))
			avail++;
	}

	return avail;
}

/*
 * select_p_resv_test - Identify the nodes which "best" satisfy a reservation
 *	request. "best" is defined as either single set of consecutive nodes
 *	satisfying the request and leaving the minimum number of unused nodes
 *	OR the fewest number of consecutive node sets
 * IN avail_bitmap - nodes available for the reservation
 * IN node_cnt - count of required nodes
 * IN core_bitmap - cores which can not be used for this reservation
 * OUT avail_bitmap - nodes allocated for the reservation
 * OUT core_bitmap - cores which allocated to this reservation
 * RET - nodes selected for use by the reservation
 */
extern bitstr_t * select_p_resv_test(bitstr_t *avail_bitmap, uint32_t node_cnt,
				     uint32_t *core_cnt, bitstr_t **core_bitmap)
{
	bitstr_t **switches_bitmap;		/* nodes on this switch */
	bitstr_t **switches_core_bitmap;	/* cores on this switch */
	int       *switches_cpu_cnt;		/* total CPUs on switch */
	int       *switches_node_cnt;		/* total nodes on switch */
	int       *switches_required;		/* set if has required node */

	bitstr_t  *avail_nodes_bitmap = NULL;	/* nodes on any switch */
	bitstr_t *sp_avail_bitmap;
	int rem_nodes, rem_cores;		/* remaining resources desired */
	int i, j;
	int best_fit_inx, first, last;
	int best_fit_nodes;
	int best_fit_location = 0, best_fit_sufficient;
	bool sufficient;
	int cores_per_node;

	xassert(avail_bitmap);

	/* When reservation includes a nodelist we use sequential_pick code */
	if (!switch_record_cnt || !switch_record_table || !node_cnt)  {
		return sequential_pick(avail_bitmap, node_cnt, core_cnt,
				       core_bitmap);
	}

	/* Use topology state information */
	if (bit_set_count(avail_bitmap) < node_cnt)
		return avail_nodes_bitmap;

	if (core_cnt && (*core_bitmap == NULL))
		*core_bitmap = _make_core_bitmap_filtered(avail_bitmap, 0);
	
	rem_nodes = node_cnt;
	rem_cores = core_cnt[0];

	/* Assuming symmetric cluster */
	if(core_cnt)
		cores_per_node = core_cnt[0] / MAX(node_cnt, 1);

	/* Construct a set of switch array entries,
	 * use the same indexes as switch_record_table in slurmctld */
	switches_bitmap   = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switches_core_bitmap = xmalloc(sizeof(bitstr_t *) * switch_record_cnt);
	switches_cpu_cnt  = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_node_cnt = xmalloc(sizeof(int)        * switch_record_cnt);
	switches_required = xmalloc(sizeof(int)        * switch_record_cnt);

	for (i=0; i<switch_record_cnt; i++) {
		char str[100];
		switches_bitmap[i] = bit_copy(switch_record_table[i].
						  node_bitmap);
		bit_and(switches_bitmap[i], avail_bitmap);
		switches_node_cnt[i] = bit_set_count(switches_bitmap[i]);

		switches_core_bitmap[i] =
			_make_core_bitmap_filtered(switches_bitmap[i], 1);

		if (*core_bitmap) {
			bit_not(*core_bitmap);
			bit_and(switches_core_bitmap[i], *core_bitmap);
			bit_not(*core_bitmap);
		}
		bit_fmt(str, sizeof(str), switches_core_bitmap[i]);
		debug2("Switch %d can use cores: %s", i, str);

		switches_cpu_cnt[i] = bit_set_count(switches_core_bitmap[i]);

		debug2("switch %d looking cores in %d nodes (%d)",
		       i, switches_node_cnt[i], switches_cpu_cnt[i]);
	}

	/* Let's check nodes with less avail cores than needed */

	for (j=0; j<switch_record_cnt; j++) {
		first = bit_ffs(switches_bitmap[j]);
		last  = bit_fls(switches_bitmap[j]);
		for (i=first; ((i<=last) && (first>=0)); i++) {
			int c;
			if (!bit_test(switches_bitmap[j], i))
				continue;

			c = _get_avail_core_in_node(*core_bitmap, i);
			if (c < cores_per_node)
				switches_node_cnt[j] -= c;
		}
	}

#if SELECT_DEBUG
	/* Don't compile this, it slows things down too much */
	for (i=0; i<switch_record_cnt; i++) {
		char *node_names = NULL;
		if (switches_node_cnt[i])
			node_names = bitmap2node_name(switches_bitmap[i]);
		debug("switch=%s nodes=%u:%s required:%u speed=%u",
		      switch_record_table[i].name,
		      switches_node_cnt[i], node_names,
		      switches_required[i],
		      switch_record_table[i].link_speed);
		xfree(node_names);
	}
#endif

	/* Determine lowest level switch satifying request with best fit */
	best_fit_inx = -1;
	for (j=0; j<switch_record_cnt; j++) {
		if ((switches_node_cnt[j] < rem_nodes) ||
		    (core_cnt && (switches_cpu_cnt[j] < core_cnt[0])))
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
		debug("select_p_resv_test: could not find resources for "
		      "reservation");
		goto fini;
	}

	/* Identify usable leafs (within higher switch having best fit) */
	for (j=0; j<switch_record_cnt; j++) {
		if ((switch_record_table[j].level != 0) ||
		    (!bit_super_set(switches_bitmap[j],
				    switches_bitmap[best_fit_inx]))) {
			switches_node_cnt[j] = 0;
		}
	}

	/* Select resources from these leafs on a best-fit basis */
	avail_nodes_bitmap = bit_alloc(node_record_count);
	while (rem_nodes > 0) {
		int avail_cores_in_node;
		best_fit_nodes = best_fit_sufficient = 0;
		for (j=0; j<switch_record_cnt; j++) {
			if (switches_node_cnt[j] == 0)
				continue;
			if(core_cnt)
				sufficient = 
					(switches_node_cnt[j] >= rem_nodes) &&
					(switches_cpu_cnt[j] >= core_cnt[0]);
			else
				sufficient = switches_node_cnt[j] >= rem_nodes;
			/* If first possibility OR */
			/* first set large enough for request OR */
			/* tightest fit (less resource waste) OR */
			/* nothing yet large enough, but this is biggest */
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
		first = bit_ffs(switches_bitmap[best_fit_location]);
		last  = bit_fls(switches_bitmap[best_fit_location]);
		for (i=first; ((i<=last) && (first>=0)); i++) {
			if (!bit_test(switches_bitmap[best_fit_location], i)){
				continue;
			}

			bit_clear(switches_bitmap[best_fit_location], i);
			switches_node_cnt[best_fit_location]--;

			if (bit_test(avail_nodes_bitmap, i)) {
				/* node on multiple leaf switches
				 * and already selected */
				continue;
			}

			if (*core_bitmap) {
				int coff;
				avail_cores_in_node = 0;
				coff = cr_get_coremap_offset(i);
				debug2("Testing node %d, core offset %d",
				       i, coff);
				for (j=0; j<cr_node_num_cores[i]; j++){
					if (!bit_test(*core_bitmap, coff + j))
						avail_cores_in_node++;
				}
				if (avail_cores_in_node < cores_per_node)
					continue;
				
				debug2("Using node %d with %d cores available",
				       i, avail_cores_in_node);
			}

			bit_set(avail_nodes_bitmap, i);
			if (core_cnt)
			    rem_cores -= cores_per_node;
			if (--rem_nodes <= 0)
				break;
		}
		switches_node_cnt[best_fit_location] = 0;
	}
	if (rem_nodes > 0)	/* insufficient resources */
		FREE_NULL_BITMAP(avail_nodes_bitmap);

fini:	for (i=0; i<switch_record_cnt; i++) {
		FREE_NULL_BITMAP(switches_bitmap[i]);
		FREE_NULL_BITMAP(switches_core_bitmap[i]);
	}

	xfree(switches_bitmap);
	xfree(switches_core_bitmap);
	xfree(switches_cpu_cnt);
	xfree(switches_node_cnt);
	xfree(switches_required);

	if (avail_nodes_bitmap && core_cnt) { 
		/* Reservation is using partial nodes */
		//char str[100];
		bitstr_t *exc_core_bitmap = NULL;

		sp_avail_bitmap = bit_alloc(bit_size(avail_bitmap));
		if (sp_avail_bitmap == NULL)
			fatal ("memory allocation failure");

		if (*core_bitmap) {
			exc_core_bitmap = *core_bitmap;
			*core_bitmap = bit_alloc(bit_size(exc_core_bitmap));
		}

		cores_per_node = core_cnt[0] / MAX(node_cnt, 1);

		while (core_cnt[0]) {
			uint32_t inx, coff;
			int i;
			int avail_cores_in_node;

			inx = bit_ffs(avail_nodes_bitmap);
			if ((inx < 0) || (inx > bit_size(avail_bitmap)))
				break;

			debug2("Using node inx %d cores_per_node %d "
			       "core_cnt %d", inx, cores_per_node, core_cnt[0]);
			coff = cr_get_coremap_offset(inx);

			/* Clear this node from the initial available bitmap */
			bit_clear(avail_nodes_bitmap, inx);

			if (cr_node_num_cores[inx] < cores_per_node)
				continue;

			avail_cores_in_node = 0;
			for (i = 0; i < cr_node_num_cores[inx]; i++) {
				if (!bit_test(exc_core_bitmap, coff + i)) {
					avail_cores_in_node++;
				}
			}

			debug2("Node %d has %d available cores", inx,
			       avail_cores_in_node);

			if (avail_cores_in_node < cores_per_node)
				continue;

			avail_cores_in_node = 0;
			for (i = 0; i < cr_node_num_cores[inx]; i++) {
				if (!bit_test(exc_core_bitmap, coff + i)) {
					bit_set(*core_bitmap, coff + i);
					core_cnt[0]--;
					avail_cores_in_node++;
				}

				if ((avail_cores_in_node == cores_per_node) ||
				    (core_cnt[0] == 0))
					break;
			}

			/* Add this node to the final node bitmap */
			bit_set(sp_avail_bitmap, inx);

		}
		FREE_NULL_BITMAP(avail_nodes_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);

		//bit_fmt(str, (sizeof(str) - 1), *core_bitmap);
		//info("sequential pick using coremap: %s", str);

		if (core_cnt[0]) {
			info("reservation request can not be satisfied");
			FREE_NULL_BITMAP(sp_avail_bitmap);
			return NULL;
		}
		return sp_avail_bitmap;
	}

	return avail_nodes_bitmap;
}

extern void select_p_ba_init(node_info_msg_t *node_info_ptr, bool sanity_check)
{
	return;
}
extern void select_p_ba_fini(void)
{
	return;
}

extern int *select_p_ba_get_dims(void)
{
	return NULL;
}
