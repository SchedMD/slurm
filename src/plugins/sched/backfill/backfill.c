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
 *  Copyright (C) 2003-2008 The Regents of the University of California.
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
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

typedef struct node_space_map {
	time_t begin_time;
	time_t end_time;
	bitstr_t *avail_bitmap;
} node_space_map_t;

/*********************** local variables *********************/
static bool altered_job   = false;
static bool new_work      = false;
static bool stop_backfill = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

static List pend_job_list = NULL;

/* Set __DEBUG to get detailed logging for this thread without 
 * detailed logging for the entire slurmctld daemon */
#define __DEBUG			0
#define MAX_BACKFILL_JOB_CNT	100
#define ONE_DAY			(24 * 60 * 60)
#define SLEEP_TIME		2

/*********************** local functions *********************/
static int  _add_pending_job(struct job_record *job_ptr, 
		struct part_record *part_ptr);
static void _attempt_backfill(struct part_record *part_ptr);
static void _backfill_part(struct part_record *part_ptr);
static void _change_prio(struct job_record *job_ptr, uint32_t prio);
static void _diff_tv_str(struct timeval *tv1,struct timeval *tv2,
		char *tv_str, int len_tv_str);
static bool _has_state_changed(void);
static bool _more_work(void);
static int  _part_prio_sort(void *x, void *y);
static int  _sort_by_prio(void *x, void *y);

/* list processing function, sort jobs by _decreasing_ priority */
static int _sort_by_prio(void *x, void *y)
{
	struct job_record *job_ptr1 = (struct job_record *) x;
	struct job_record *job_ptr2 = (struct job_record *) y;

	if (job_ptr2->priority > job_ptr1->priority)
		return 1;
	else if (job_ptr2->priority > job_ptr1->priority)
		return -1;
	return 0;
}

/* We want in order of decreasing priority, so the ordering here is
 * the reverse of that defined in src/common/list.h.
 * Partition priority is of type uint16_t */
static int _part_prio_sort(void *x, void *y)
{
	struct part_record *part_ptr1 = (struct part_record *) x;
	struct part_record *part_ptr2 = (struct part_record *) y;
	int32_t prio1 = (int32_t) part_ptr1->priority;
	int32_t prio2 = (int32_t) part_ptr2->priority;
	return (int) (prio1 - prio2);
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
extern void *backfill_agent(void *args)
{
	struct timeval tv1, tv2;
	char tv_str[20];
	bool filter_root = false;
	ListIterator part_iterator;
	struct part_record *part_ptr;
	/* Read config, node, and partitions; Write jobs */
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK };

	if (slurm_get_root_filter())
		filter_root = true;
	while (!stop_backfill) {
		sleep(SLEEP_TIME);      /* don't run continuously */

		if ((!_more_work()) || (!_has_state_changed()) || stop_backfill)
			continue;

		gettimeofday(&tv1, NULL);
		lock_slurmctld(all_locks);

		/* make sure partitions are in order of decreasing
		 * priority, backfill highest priority partitions 
		 * first since they may have overlapping nodes */
		list_sort(part_list, _part_prio_sort);

		/* identify partitions eligible for backfill */
		part_iterator = list_iterator_create(part_list);
		while ((part_ptr = (struct part_record *) 
					list_next(part_iterator))) {
			if ((part_ptr->state_up == 0) ||
			    (part_ptr->node_bitmap == NULL))
			 	continue;
			if ((part_ptr->root_only) && filter_root)
				continue;
			_attempt_backfill(part_ptr);
		}
		list_iterator_destroy(part_iterator);

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
extern void run_backfill (void)
{
	pthread_mutex_lock( &thread_flag_mutex );
	new_work = true;
	pthread_mutex_unlock( &thread_flag_mutex );
}

static bool _more_work (void)
{
	static bool rc;
	pthread_mutex_lock( &thread_flag_mutex );
	rc = new_work;
	new_work = false;
	pthread_mutex_unlock( &thread_flag_mutex );
	return rc;
}

/* Report if any changes occurred to job, node or partition information */
static bool _has_state_changed(void)
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
static void _attempt_backfill(struct part_record *part_ptr)
{
	int error_code = 0;
	struct job_record *job_ptr;
	ListIterator job_iterator;

#if __DEBUG
	info("backfill: attempt on partition %s", part_ptr->name);
#endif
		
	/* build lists of pending jobs in this partition */
	pend_job_list = list_create(NULL);
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if ((job_ptr->part_ptr != part_ptr) ||
		    (job_ptr->job_state != JOB_PENDING))
			continue;

		if (_add_pending_job(job_ptr, part_ptr)) {
			error_code = 3;
			break;
		}
	}
	list_iterator_destroy(job_iterator);

	if ((error_code == 0) &&
	    (!list_is_empty(pend_job_list)))
		_backfill_part(part_ptr);

	list_destroy(pend_job_list);
}

/* Add specified pending job to our records 
 * RET 0 on success, otherwise an error code */
static int _add_pending_job(struct job_record *job_ptr, 
		struct part_record *part_ptr)
{
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
		return 0;
	}

	if (detail_ptr->min_nodes > part_ptr->max_nodes) {
#if __DEBUG
		info("backfill: pending job %u exceeds partition node limit", 
				job_ptr->job_id);
#endif
		return 0;	/* Skip this job */
	}

	if (!job_independent(job_ptr)) {
#if __DEBUG
		info("backfill: pending job %u dependent or waiting for "
				"begin time", job_ptr->job_id);
#endif
		return 0;	/* Skip this job */
	}

#if __DEBUG
	info("backfill: job:%u prio:%u pending on %u nodes", 
	     job_ptr->job_id, job_ptr->priority, detail_ptr->min_nodes);
#endif

	list_append(pend_job_list, (void *) job_ptr);
	return 0;
}

#if __DEBUG
static void _dump_node_space_table(node_space_map_t *node_space_ptr,
		int node_space_recs)
{
	int i;
	char begin_buf[32], end_buf[32], *node_list;

	info("=========================================");
	for (i=0; i<node_space_recs; i++) {
		slurm_make_time_str(&node_space_ptr[i].begin_time,
				    begin_buf, sizeof(begin_buf));
		slurm_make_time_str(&node_space_ptr[i].end_time,
				    end_buf, sizeof(end_buf));
		node_list = bitmap2node_name(node_space_ptr[i].avail_bitmap);
		info("Begin:%s End:%s Nodes:%s", begin_buf, end_buf, node_list);
		xfree(node_list);
	}
	info("=========================================");
}
#endif

/* scan pending job queue and change the priority of any that 
 * can run now without delaying the expected initiation time 
 * of any higher priority job */
static void _backfill_part(struct part_record *part_ptr)
{
	struct job_record *job_ptr;
	ListIterator pend_job_iterate;
	struct job_record *first_job = NULL;	/* just used as flag */
	uint32_t min_nodes, max_nodes, req_nodes, end_time, time_limit;
	bitstr_t *avail_bitmap;
	time_t now = time(NULL), end_reserve;
	int i, j, rc, node_space_recs;
	node_space_map_t node_space[MAX_BACKFILL_JOB_CNT + 1];

	node_space_recs = 1;
	node_space[0].begin_time = now;
	node_space[0].end_time = now + ONE_DAY;
	node_space[0].avail_bitmap = bit_copy(part_ptr->node_bitmap);
	bit_and(node_space[0].avail_bitmap, avail_node_bitmap);

	node_space[1].begin_time = node_space[0].end_time;
	node_space[1].end_time = node_space[1].begin_time + ONE_DAY;
	node_space[1].avail_bitmap =  bit_alloc(node_record_count);
	node_space_recs = 2;

#if __DEBUG
	_dump_node_space_table(node_space, node_space_recs);
#endif

	/* find job to possibly backfill */
	list_sort(pend_job_list, _sort_by_prio);
	pend_job_iterate = list_iterator_create(pend_job_list);
	while ( (job_ptr = list_next(pend_job_iterate)) ) {
		if (first_job == NULL)
			first_job = job_ptr;

		/* Determine minimum and maximum node counts */
		min_nodes = MAX(job_ptr->details->min_nodes, 
				part_ptr->min_nodes);
		if (job_ptr->details->max_nodes == 0)
			max_nodes = part_ptr->max_nodes;
		else
			max_nodes = MIN(job_ptr->details->max_nodes, 
					part_ptr->max_nodes);
		max_nodes = MIN(max_nodes, 500000);	/* prevent overflows */
		if (job_ptr->details->max_nodes)
			req_nodes = max_nodes;
		else
			req_nodes = min_nodes;
		if (min_nodes > max_nodes) {
			/* job's min_nodes exceeds partition's max_nodes */
			continue;
		}

		/* Determine job's expected completion time */
		if (job_ptr->time_limit == NO_VAL)
			time_limit = part_ptr->max_time;
		else
			time_limit = job_ptr->time_limit;
		if (time_limit > (ONE_DAY/60))
			continue;
		end_time = (time_limit * 60) + now;

		for (i=0; i<node_space_recs; i++) {
			if (end_time <= node_space[i].end_time)
				break;
		}
		if (i >= node_space_recs)	/* job runs too long */
			continue;
		avail_bitmap = bit_copy(node_space[i].avail_bitmap);
		if (job_ptr->details->exc_node_bitmap) {
			bit_not(job_ptr->details->exc_node_bitmap);
			bit_and(avail_bitmap, 
				job_ptr->details->exc_node_bitmap);
			bit_not(job_ptr->details->exc_node_bitmap);
		}
		if ((job_ptr->details->req_node_bitmap) &&
		    (!bit_super_set(job_ptr->details->req_node_bitmap,
				    avail_bitmap))) {
			bit_free(avail_bitmap);
			continue;
		}

		/* try to schedule the job */
		rc = select_g_job_test(job_ptr, avail_bitmap,
				min_nodes, max_nodes, req_nodes, 
				SELECT_MODE_WILL_RUN);
		if (rc != SLURM_SUCCESS) {
			bit_free(avail_bitmap);
			continue;
		}
		if (job_ptr->start_time <= (now + 1)) {
			/* start the job now */
			_change_prio(job_ptr, (first_job->priority + 1));
			bit_free(avail_bitmap);
			break;
		}
		if (job_ptr->start_time > (now + ONE_DAY)) {
			/* too far in the future to worry about */
			bit_free(avail_bitmap);
			continue;
		}

		if (node_space_recs == MAX_BACKFILL_JOB_CNT)
			break;

		/*
		 * Add reservation to scheduling table
		 */
		end_reserve = job_ptr->start_time + (time_limit * 60);
		bit_not(avail_bitmap);
		for (i=0; i<node_space_recs; i++) {
			if (node_space[i].begin_time >= job_ptr->start_time)
				break;
		}
		if (node_space[i].begin_time > job_ptr->start_time) {
			/* Need to shift existing records and add a new one */
			for (j=node_space_recs; j>i; j--) {
				node_space[j].begin_time = 
						node_space[j-1].begin_time;
				node_space[j].end_time = 
						node_space[j-1].end_time;
				node_space[j].avail_bitmap = 
						node_space[j-1].avail_bitmap;
			}
			node_space[i].begin_time = job_ptr->start_time;
			node_space[i].end_time = node_space[i-1].end_time;
			node_space[i].avail_bitmap = bit_copy(
					node_space[i-1].avail_bitmap);
			node_space[i-1].end_time = job_ptr->start_time;
			node_space_recs++;
			if (node_space[i].end_time > end_reserve) {
				node_space[i].end_time = end_reserve;
				node_space[i+1].begin_time = end_reserve;
			}
		}

		for ( ; i<node_space_recs; i++) {
			bit_and(node_space[i].avail_bitmap, avail_bitmap);
			if (node_space[i].end_time >= end_reserve)
				break;
		}
		if (node_space[i].end_time > end_reserve) {
			/* Need to shift existing records and add a new one */
			for (j=node_space_recs; j>i; j--) {
				node_space[j].begin_time = 
						node_space[j-1].begin_time;
				node_space[j].end_time = 
						node_space[j-1].end_time;
				node_space[j].avail_bitmap = 
						node_space[j-1].avail_bitmap;
			}
			node_space[i].end_time = end_reserve;
			node_space[i+1].begin_time = end_reserve;
			node_space[i].avail_bitmap = bit_copy(
					node_space[i+1].avail_bitmap);
			node_space_recs++;
		}
		bit_free(avail_bitmap);
#if __DEBUG
		_dump_node_space_table(node_space, node_space_recs);
#endif
	}
	list_iterator_destroy(pend_job_iterate);

	for (i=0; i<node_space_recs; i++)
		bit_free(node_space[i].avail_bitmap);
}

/* Change the priority of a pending job to get it running now */
static void _change_prio(struct job_record *job_ptr, uint32_t prio)
{
	info("backfill: set job %u to priority %u", job_ptr->job_id, prio);
	job_ptr->priority = prio;
	altered_job = true;
	run_backfill();
	last_job_update = time(NULL);
}
