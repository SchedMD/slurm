/* 
 * job_scheduler.c - manage the scheduling of pending jobs in priority order
 *
 * author: moe jette, jette@llnl.gov
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "slurmctld.h"

struct job_queue {
	int priority;
	struct job_record *job_ptr;
};

int build_job_queue (struct job_queue **job_queue);
void sort_job_queue (struct job_queue *job_queue, int job_queue_size);

#if DEBUG_MODULE
/* main is used here for module testing purposes only */
int 
main (int argc, char *argv[]) 
{
	printf("No test functions presently available\n");
	exit (0);
}
#endif

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
 * global: job_list - global list of job records
 *	last_job_update - time of last update to job table
 */
void
schedule()
{
	struct job_queue *job_queue;
	int i, j, error_code, failed_part_cnt, job_queue_size;
	struct job_record *job_ptr;
	struct part_record **failed_parts;

	job_queue_size = build_job_queue (&job_queue);
	if (job_queue_size == 0)
		return;
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
		error_code = select_nodes(job_ptr);
		if (error_code == EAGAIN) {
			xrealloc(failed_parts, 
			         (failed_part_cnt+1)*sizeof(struct part_record *));
			failed_parts[failed_part_cnt++] = job_ptr->part_ptr;
		}
		else if (error_code == EINVAL) {
			last_job_update = time (NULL);
			job_ptr->job_state = JOB_FAILED;
			job_ptr->start_time = job_ptr->end_time = time(NULL);
			delete_job_details(job_ptr);
		}
		else {			/* job initiated */
			last_job_update = time (NULL);
			info ("schedule: job_id %u on nodes %s", 
			      job_ptr->job_id, job_ptr->nodes);
		}
	}

	if (failed_parts)
		xfree(failed_parts);
	if (job_queue)
		xfree(job_queue);
}


/* 
 * sort_job_queue - sort a job queue in decending priority order
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
