/*****************************************************************************\
 *  backfill.c - simple backfill scheduler plugin. 
 *
 *  If a partition is does not have root only access and nodes are not shared
 *  then raise the priority of pending jobs if doing so does not adversely
 *  effect the expected initiation of any higher priority job. We do not alter
 *  a job's required or excluded node list, so this is a conservative 
 *  algorithm.
 *
 *  For example, consider a cluster "lx[01-08]" with one job executing on 
 *  nodes "lx[01-04]". The highest priority pending job requires five nodes 
 *  including "lx05". The next highest priority pending job requires any 
 *  three nodes. Without explicitly forcing the second job to use nodes 
 *  "lx[06-08]", we can't start it without possibly delaying the higher 
 *  priority job.
 *****************************************************************************
 *  Copyright (C) 2003-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-226842.
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

typedef struct part_specs {
	uint32_t idle_node_cnt;
	uint32_t max_cpus;
	uint32_t min_cpus;
	uint32_t min_mem;
	uint32_t min_disk;
} part_specs_t;

typedef struct node_space_map {
	uint32_t idle_node_cnt;
	time_t time;
} node_space_map_t;

/*********************** local variables *********************/
static bool altered_job   = false;
static bool new_work      = false;
static bool stop_backfill = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

static List pend_job_list = NULL;
static List run_job_list  = NULL;

#define MAX_JOB_CNT 100
static int node_space_recs;
static node_space_map_t node_space[MAX_JOB_CNT + 1];

/* Set __DEBUG to get detailed logging for this thread without 
 * detailed logging for the entire slurmctld daemon */
#define __DEBUG        0
#define SLEEP_TIME     1

/*********************** local functions *********************/
static int  _add_pending_job(struct job_record *job_ptr, 
		struct part_record *part_ptr, part_specs_t *part_specs);
static int  _add_running_job(struct job_record *job_ptr);
static void _attempt_backfill(struct part_record *part_ptr);
static void _backfill_part(part_specs_t *part_specs);
static void _build_node_space_map(part_specs_t *part_specs);
static void _change_prio(struct job_record *job_ptr, uint32_t prio);
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str);
static void _dump_node_space_map(uint32_t job_id, uint32_t node_cnt);
static int  _get_avail_node_cnt(struct job_record *job_ptr); 
static void _get_part_specs(struct part_record *part_ptr, 
		part_specs_t *part_specs);
static bool _has_state_changed(void);
static bool _loc_restrict(struct job_record *job_ptr, part_specs_t *part_specs);
static bool _more_work(void);
static int  _sort_by_prio(void *x, void *y);
static int  _sort_by_end(void *x, void *y);
static int  _update_node_space_map(struct job_record *job_ptr);

/* list processing function, sort jobs by _decreasing_ priority */
static int _sort_by_prio(void *x, void *y)
{
	struct job_record *job_ptr1 = (struct job_record *) x;
	struct job_record *job_ptr2 = (struct job_record *) y;
	double diff = job_ptr2->priority - job_ptr1->priority;

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;
	else
		return 0;
}

/* list processing function, sort jobs by _increasing_ end time */
static int _sort_by_end(void *x, void *y)
{
	struct job_record *job_ptr1 = (struct job_record *) x;
	struct job_record *job_ptr2 = (struct job_record *) y;
	double diff = difftime(job_ptr1->end_time, job_ptr2->end_time);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;
	else
		return 0;
}

/*
 * _diff_tv_str - build a string showing the time difference between two times
 * IN tv1 - start of event
 * IN tv2 - end of event
 * OUT tv_str - place to put delta time in format "usec=%ld"
 * IN len_tv_str - size of tv_str in bytes
 */
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str)
{
	long delta_t;
	delta_t  = (tv2->tv_sec  - tv1->tv_sec) * 1000000;
	delta_t +=  tv2->tv_usec - tv1->tv_usec;
	snprintf(tv_str, len_tv_str, "usec=%ld", delta_t);
}

/* Terminate backfill_agent */
extern void stop_backfill_agent(void)
{
	stop_backfill = true;
}


/* backfill_agent - detached thread periodically attempts to backfill jobs */
extern void *
backfill_agent(void *args)
{
	struct timeval tv1, tv2;
	char tv_str[20];
	bool filter_root = false;
	/* Read config, node, and partitions; Write jobs */
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	if (slurm_get_root_filter())
		filter_root = true;
	while (!stop_backfill) {
		sleep(SLEEP_TIME);      /* don't run continuously */
		if ((!_more_work()) || stop_backfill)
			continue;

		gettimeofday(&tv1, NULL);
		lock_slurmctld(all_locks);
		if ( _has_state_changed() ) {
			ListIterator part_iterator;
			struct part_record *part_ptr;

			/* identify partitions eligible for backfill */
			part_iterator = list_iterator_create(part_list);
			while ((part_ptr = (struct part_record *) 
						list_next(part_iterator))) {
				if ( ((part_ptr->shared)       ||
				      (part_ptr->state_up == 0)) )
				 	continue; /* not under our control */
				if ((part_ptr->root_only) && filter_root)
					continue;
				_attempt_backfill(part_ptr);
			}
			list_iterator_destroy(part_iterator);
		}
		unlock_slurmctld(all_locks);
		gettimeofday(&tv2, NULL);
		_diff_tv_str(&tv1, &tv2, tv_str, 20);
#if __DEBUG
		info("backfill: completed, %s", tv_str);
#endif
		if (altered_job) {
			altered_job = false;
			schedule();	/* has own locks */
		}
	}
	return NULL;
}

/* trigger the attempt of a backfill */
extern void
run_backfill (void)
{
	pthread_mutex_lock( &thread_flag_mutex );
	new_work = true;
	pthread_mutex_unlock( &thread_flag_mutex );
}

static bool
_more_work (void)
{
	static bool rc;
	pthread_mutex_lock( &thread_flag_mutex );
	rc = new_work;
	new_work = false;
	pthread_mutex_unlock( &thread_flag_mutex );
	return rc;
}

/* Report if any changes occurred to job, node or partition information */
static bool
_has_state_changed(void)
{
	static time_t backfill_job_time  = (time_t) 0;
        static time_t backfill_node_time = (time_t) 0;
	static time_t backfill_part_time = (time_t) 0;

	if ( (backfill_job_time  == last_job_update ) &&
	     (backfill_node_time == last_node_update) &&
	     (backfill_part_time == last_part_update) )
		return false;

	backfill_job_time  = last_job_update;
	backfill_node_time = last_node_update;
	backfill_part_time = last_part_update;
	return true;
}

/* Attempt to perform backfill scheduling on the specified partition */
static void 
_attempt_backfill(struct part_record *part_ptr)
{
	int i, cg_hung = 0, error_code = 0;
	uint32_t max_pending_prio = 0;
	uint32_t min_pend_job_size = INFINITE;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	part_specs_t part_specs;
	time_t now = time(NULL);

#if __DEBUG
	info("backfill: attempt on partition %s", part_ptr->name);
#endif

	_get_part_specs(part_ptr, &part_specs);
	if (part_specs.idle_node_cnt == 0)
		return;		/* no idle nodes */

	pend_job_list = list_create(NULL);
	run_job_list  = list_create(NULL);
		
	/* build lists of pending and running jobs in this partition */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (job_ptr->part_ptr != part_ptr)
			continue;	/* job in different partition */

		if (job_ptr->job_state & JOB_COMPLETING) {
			long wait_time = (long) difftime(now, job_ptr->end_time);
			if (wait_time > 600) {
				/* Job has been in completing state for 
				 * >10 minutes, try to schedule around it */
				cg_hung++;
				continue;
			}
#if __DEBUG
			info("backfill: Job %u completing, skip partition", 
					job_ptr->job_id);
#endif
			error_code = 1;
			break;
		} else if (job_ptr->job_state == JOB_RUNNING) {
			if (_add_running_job(job_ptr)) {
				error_code = 2;
				break;
			}
		} else if (job_ptr->job_state == JOB_PENDING) {
			max_pending_prio = MAX(max_pending_prio, 
					job_ptr->priority);
			if (_add_pending_job(job_ptr, part_ptr, &part_specs)) {
				error_code = 3;
				break;
			}
			min_pend_job_size = MIN(min_pend_job_size, 
					job_ptr->node_cnt);
		}
	}
	list_iterator_destroy(job_iterator);
	if (error_code) 
		goto cleanup;

	i = list_count(run_job_list) + cg_hung;
	/* Do not try to backfill if
	 * we already have many running jobs,
	 * there are no pending jobs, OR
	 * there are insufficient idle nodes to start any pending jobs */
	if ((i > MAX_JOB_CNT)
	|| list_is_empty(pend_job_list)
	|| (min_pend_job_size > part_specs.idle_node_cnt))
		goto cleanup;

	list_sort(pend_job_list, _sort_by_prio);
	list_sort(run_job_list, _sort_by_end);
	_build_node_space_map(&part_specs);
	_backfill_part(&part_specs);

      cleanup:
	list_destroy(pend_job_list);
	list_destroy(run_job_list);
}

/* get the specs on nodes within a partition */
static void 
_get_part_specs(struct part_record *part_ptr, part_specs_t *part_specs)
{
	int i, j;

	part_specs->idle_node_cnt = 0;
	part_specs->max_cpus      = 0;
	part_specs->min_cpus      = INFINITE;
	part_specs->min_mem       = INFINITE;
	part_specs->min_disk      = INFINITE;

	for (i=0; i<node_record_count; i++) {
		struct node_record *node_ptr = &node_record_table_ptr[i];
		bool found_part = false;

		for (j=0; j<node_ptr->part_cnt; j++) {
			if (node_ptr->part_pptr[j] != part_ptr)
				continue;
			found_part = true;
			break;
		}
		if (found_part == false)
			continue;	/* different partition */
		if (node_ptr->node_state == NODE_STATE_IDLE)
			part_specs->idle_node_cnt++;
		if (slurmctld_conf.fast_schedule) {
			part_specs->max_cpus = MAX(part_specs->max_cpus,
					node_ptr->config_ptr->cpus);
			part_specs->min_cpus = MIN(part_specs->min_cpus,
					node_ptr->config_ptr->cpus);
			part_specs->min_mem  = MIN(part_specs->min_mem,
					node_ptr->config_ptr->real_memory);
			part_specs->min_disk = MIN(part_specs->min_disk,
					node_ptr->config_ptr->tmp_disk);
		} else {
			part_specs->max_cpus = MAX(part_specs->max_cpus,
					node_ptr->cpus);
			part_specs->min_cpus = MIN(part_specs->min_cpus,
					node_ptr->cpus);
			part_specs->min_mem  = MIN(part_specs->min_mem,
					node_ptr->real_memory);
			part_specs->min_disk = MIN(part_specs->min_disk,
					 node_ptr->tmp_disk);
		}
	}

#if __DEBUG
	info("backfill: partition %s cpus=%u:%u mem=%u+ disk=%u+",
		part_ptr->name, part_specs->min_cpus, part_specs->max_cpus, 
		part_specs->min_mem, part_specs->min_disk);
#endif
}

/* Add specified pending job to our records */
static int
_add_pending_job(struct job_record *job_ptr, struct part_record *part_ptr, 
		part_specs_t *part_specs)
{
	int min_node_cnt;
	struct job_details *detail_ptr = job_ptr->details;

	if (job_ptr->priority == 0) {
#if __DEBUG
		info("backfill: pending job %u is held", job_ptr->job_id);
#endif
		return 0;	/* Skip this job */
	}

	if ((job_ptr->time_limit != NO_VAL) && 
	    (job_ptr->time_limit > part_ptr->max_time)) {
#if __DEBUG
		info("backfill: pending job %u exceeds partition time limit", 
			job_ptr->job_id);
#endif
		return 0;	/* Skip this job */
	}

	if (detail_ptr == NULL) {
		error("backfill: pending job %u lacks details", 
			job_ptr->job_id);
		return 1;
	}

	/* figure out how many nodes this job needs */
	min_node_cnt = (job_ptr->num_procs + part_specs->max_cpus - 1) /
			part_specs->max_cpus;	/* round up */
	detail_ptr->min_nodes = MAX(min_node_cnt, detail_ptr->min_nodes);
	if (detail_ptr->min_nodes > part_ptr->max_nodes) {
#if __DEBUG
		info("backfill: pending job %u exceeds partition node limit", 
				job_ptr->job_id);
#endif
		return 0;	/* Skip this job */
	}

#if __DEBUG
	info("backfill: job %u pending on %d nodes", job_ptr->job_id, 
		detail_ptr->min_nodes);
#endif

	list_append(pend_job_list, (void *) job_ptr);
	return 0;
}

/* Add specified running job to our records */
static int
_add_running_job(struct job_record *job_ptr)
{
#if __DEBUG
	info("backfill: job %u running on %d nodes: %s", job_ptr->job_id, 
			job_ptr->node_cnt, job_ptr->nodes);
#endif

	list_append(run_job_list, (void *) job_ptr);
	return 0;
}

/* build a map of how many nodes are free at any point in time 
 * based upon currently running jobs. pending jobs are added to 
 * the map as we execute the backfill algorithm */
static void 
_build_node_space_map(part_specs_t *part_specs)
{
	ListIterator run_job_iterate;
	struct job_record *run_job_ptr;
	int base_size = 0;

	node_space_recs = 0;

	if (part_specs->idle_node_cnt) {
		base_size = part_specs->idle_node_cnt;
		node_space[node_space_recs].idle_node_cnt = base_size; 
		node_space[node_space_recs++].time = time(NULL);
	}

	run_job_iterate = list_iterator_create(run_job_list);
	while ( (run_job_ptr = list_next(run_job_iterate)) ) {
		uint32_t nodes2free = _get_avail_node_cnt(run_job_ptr);
		if (nodes2free == 0)
			continue;	/* no nodes returning to service */
		base_size += nodes2free;
		node_space[node_space_recs].idle_node_cnt = base_size;
		node_space[node_space_recs++].time = run_job_ptr->end_time;
	}
	list_iterator_destroy(run_job_iterate);

	_dump_node_space_map(0, 0);
}

static void
_dump_node_space_map(uint32_t job_id, uint32_t node_cnt)
{
#if __DEBUG
	int i;
	time_t now;

	if (job_id == 0)
		info("backfill: initial node_space_map");
	else
		info("backfill: node_space_map after job %u allocated %u nodes", 
				job_id, node_cnt);

	now = time(NULL);
	for (i=0; i<node_space_recs; i++) {
		info("backfill: %3d nodes at time %4d (seconds in future)",
			node_space[i].idle_node_cnt,	
			(int) difftime(node_space[i].time, now));
	}
#endif
}

/* return 1 if the job could be started now, 0 otherwise and add job into
 * node_space_map
 */
static int
_update_node_space_map(struct job_record *job_ptr)
{
	int i, j, min_nodes, nodes_needed;
	time_t fini_time;

	if (node_space_recs == 0)	/* no nodes now or in future */
		return 0;
	if (job_ptr->details == NULL)	/* pending job lacks details */
		return 0;

	if (job_ptr->time_limit == NO_VAL)
		fini_time = time(NULL) + job_ptr->part_ptr->max_time;
	else
		fini_time = time(NULL) + job_ptr->time_limit;
	min_nodes = node_space[0].idle_node_cnt;
	for (i=1; i<node_space_recs; i++) {
		if (node_space[i].time > fini_time)
			break;
		if (min_nodes > node_space[i].idle_node_cnt)
			min_nodes = node_space[i].idle_node_cnt;
	}
	
	nodes_needed = job_ptr->details->min_nodes;
	if (nodes_needed <= min_nodes)
		return 1;

	for (i=0; i<node_space_recs; i++) {
		int fits = 0;
		if (node_space[i].idle_node_cnt < nodes_needed)
			continue;	/* can't start yet... */
		fits = 1;
		for (j=i; j<node_space_recs; j++) {
			if (node_space[j].idle_node_cnt < nodes_needed) {
				fits = 0;
				break;
			}
		}
		if (fits == 0)
			continue;
		for (j=i; j<node_space_recs; j++) {
			node_space[j].idle_node_cnt -= nodes_needed;
		}
		break;
	}

	_dump_node_space_map(job_ptr->job_id, nodes_needed);
	return 0;
}

/* return the number of nodes to be returned to this partition when
 * the specified job terminates. Don't count DRAIN or DOWN nodes */
static int
_get_avail_node_cnt(struct job_record *job_ptr)
{
	int cnt = 0, i;
	struct node_record *node_ptr;
	uint16_t base_state;

	for (i=0; i<node_record_count; i++) { 
		if (bit_test(job_ptr->node_bitmap, i) == 0)
			continue;
		node_ptr = node_record_table_ptr + i;
		if (node_ptr->node_state & NODE_STATE_DRAIN)
			continue;
		base_state = node_ptr->node_state & NODE_STATE_BASE;
		if (base_state == NODE_STATE_DOWN)
			continue;	
		cnt++;
	}

	return cnt;
}


/* scan pending job queue and change the priority of any that 
 * can run now without delaying the expected initiation time 
 * of any higher priority job */
static void 
_backfill_part(part_specs_t *part_specs)
{
	struct job_record *pend_job_ptr;
	ListIterator pend_job_iterate;
	struct job_record *first_job = NULL;	/* just used as flag */

	/* find job to possibly backfill */
	pend_job_iterate = list_iterator_create(pend_job_list);
	while ( (pend_job_ptr = list_next(pend_job_iterate)) ) {
		if (first_job == NULL)
			first_job = pend_job_ptr;

		if (_loc_restrict(pend_job_ptr, part_specs)) {
#if __DEBUG
			info("Job %u has locality restrictions",
				pend_job_ptr->job_id);
#endif
			continue;
		}

		if (first_job == pend_job_ptr) {
			if (pend_job_ptr->details == NULL)
				break;
			if (pend_job_ptr->details->min_nodes <= 
					part_specs->idle_node_cnt) {
#if __DEBUG
				info("Job %u should start via FIFO",
					pend_job_ptr->job_id);
#endif
				break;
			}
		}

		if (_update_node_space_map(pend_job_ptr)) {
			_change_prio(pend_job_ptr, 
				(first_job->priority + 1));
			break;
		}
	}
	list_iterator_destroy(pend_job_iterate);
}

/* Return true if job has locality restrictions, false otherwise */
static bool
_loc_restrict(struct job_record *job_ptr, part_specs_t *part_specs)
{
	struct job_details *detail_ptr = job_ptr->details;
	
	if (detail_ptr == NULL)
		return false;

	if ( (detail_ptr->contiguous) || (detail_ptr->features)  ||
	     (detail_ptr->req_nodes && detail_ptr->req_nodes[0]) ||
	     (detail_ptr->exc_nodes && detail_ptr->exc_nodes[0]) )
		return true;

	if ( (detail_ptr->job_min_procs    > part_specs->min_cpus) ||
	     (detail_ptr->job_min_memory   > part_specs->min_mem)  ||
	     (detail_ptr->job_min_tmp_disk > part_specs->min_disk) )
		return true;

	if (part_specs->max_cpus != part_specs->min_cpus) {
		int max_node_cnt;
		max_node_cnt = (job_ptr->num_procs + part_specs->min_cpus 
				- 1) / part_specs->min_cpus;
		if (max_node_cnt > detail_ptr->min_nodes)
			return true;
	}

	return false;
}

/* Change the priority of a pending job to get it running now */
static void
_change_prio(struct job_record *job_ptr, uint32_t prio)
{
	info("backfill: set job %u to priority %u", job_ptr->job_id, prio);
	job_ptr->priority = prio;
	altered_job = true;
	run_backfill();
	last_job_update = time(NULL);
}

