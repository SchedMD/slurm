/*****************************************************************************\
 * job_scheduler.c - manage the scheduling of pending jobs in priority order
 *	Note there is a global job list (job_list)
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <src/common/list.h>
#include <src/slurmctld/locks.h>
#include <src/slurmctld/slurmctld.h>

struct job_queue {
	int priority;
	struct job_record *job_ptr;
};

int build_job_queue (struct job_queue **job_queue);
void sort_job_queue (struct job_queue *job_queue, int job_queue_size);

/* 
 * build_job_queue - build (non-priority ordered) list of pending jobs
 * input: job_queue - storage location for job queue
 * output: job_queue - pointer to job queue
 *	returns - number of entries in job_queue
 * global: job_list - global list of job records
 * NOTE: the buffer at *job_queue must be xfreed by the caller
 */
int 
build_job_queue (struct job_queue **job_queue) 
{
	ListIterator job_record_iterator;
	struct job_record *job_record_point = NULL;
	int job_buffer_size, job_queue_size;
	struct job_queue *my_job_queue;

	/* build list pending jobs */
	job_buffer_size = job_queue_size = 0;
	job_queue[0] = my_job_queue = NULL;
	job_record_iterator = list_iterator_create (job_list);	
	
	while ((job_record_point = 
		(struct job_record *) list_next (job_record_iterator))) {
		if (job_record_point->job_state != JOB_PENDING) 
			continue;
		if (job_record_point->magic != JOB_MAGIC)
			fatal ("prio_order_job: data integrity is bad");
		if (job_buffer_size <= job_queue_size) {
			job_buffer_size += 50;
			xrealloc(my_job_queue, job_buffer_size * 
				sizeof (struct job_queue));
		}
		my_job_queue[job_queue_size].job_ptr = job_record_point;
		my_job_queue[job_queue_size].priority = job_record_point->priority;
		job_queue_size++;
	}			
	list_iterator_destroy (job_record_iterator);

	job_queue[0] = my_job_queue;
	return job_queue_size;
}


/* 
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority  
 *	order until a request fails
 * output: returns count of jobs scheduled
 * global: job_list - global list of job records
 *	last_job_update - time of last update to job table
 * Note: We re-build the queue every time. Jobs can not only be added 
 *	or removed from the queue, but have their priority or partition 
 *	changed with the update_job RPC. In general nodes will be in priority 
 *	order (by submit time), so the sorting should be pretty fast.
 */
int
schedule (void)
{
	struct job_queue *job_queue;
	int i, j, error_code, failed_part_cnt, job_queue_size, job_cnt = 0;
	struct job_record *job_ptr;
	struct part_record **failed_parts;
	/* Locks: Write job, write node, read partition */
	slurmctld_lock_t job_write_lock = { NO_LOCK, WRITE_LOCK, WRITE_LOCK, READ_LOCK };

	lock_slurmctld (job_write_lock);
	job_queue_size = build_job_queue (&job_queue);
	if (job_queue_size == 0) {
		unlock_slurmctld (job_write_lock);
		return 0;
	}
	sort_job_queue (job_queue, job_queue_size);

	failed_part_cnt = 0;
	failed_parts = NULL;
	for (i=0; i<job_queue_size; i++) {
		job_ptr = job_queue[i].job_ptr;
		for (j=0; j<failed_part_cnt; j++) {
			if (failed_parts[j] == job_ptr->part_ptr)
				break;
		}
		if (j < failed_part_cnt) continue;
		error_code = select_nodes(job_ptr, 0);
		if (error_code == ESLURM_NODES_BUSY) {
			xrealloc(failed_parts, 
			         (failed_part_cnt+1)*sizeof(struct part_record *));
			failed_parts[failed_part_cnt++] = job_ptr->part_ptr;
		}
		else if (error_code == SLURM_SUCCESS) {		/* job initiated */
			last_job_update = time (NULL);
			info ("schedule: job_id %u on nodes %s", 
			      job_ptr->job_id, job_ptr->nodes);
			job_cnt++;
		}
		else {
			info ("schedule: job_id %u non-runnable, errno %d",
				job_ptr->job_id, errno);
			last_job_update = time (NULL);
			job_ptr->job_state = JOB_FAILED;
			job_ptr->start_time = job_ptr->end_time = time(NULL);
			delete_job_details(job_ptr);
		}
	}

	if (failed_parts)
		xfree(failed_parts);
	if (job_queue)
		xfree(job_queue);
	unlock_slurmctld (job_write_lock);
	return job_cnt;
}


/* 
 * sort_job_queue - sort job_queue in decending priority order
 * input: job_queue - pointer to un-sorted job queue
 *	job_queue_size - count of elements in the job queue
 * output: job_queue - pointer to sorted job queue
 */
void
sort_job_queue (struct job_queue *job_queue, int job_queue_size) 
{
	int i, j, top_prio_inx;
	int tmp_prio, top_prio;
	struct job_record *tmp_job_ptr;

	for (i=0; i<job_queue_size; i++) {
		top_prio = job_queue[i].priority;
		top_prio_inx = i;
		for (j=(i+1); j<job_queue_size; j++) {
			if (top_prio >= 
			    job_queue[j].priority)
				continue;
			top_prio = job_queue[j].priority;
			top_prio_inx = j;
		}
		if (top_prio_inx == i)
			continue;
		tmp_prio = job_queue[i].priority;
		tmp_job_ptr = job_queue[i].job_ptr;
		job_queue[i].priority = job_queue[top_prio_inx].priority;
		job_queue[i].job_ptr = job_queue[top_prio_inx].job_ptr;
		job_queue[top_prio_inx].priority = tmp_prio;
		job_queue[top_prio_inx].job_ptr  = tmp_job_ptr;
	}
}
