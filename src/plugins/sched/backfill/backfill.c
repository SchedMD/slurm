/*****************************************************************************\
 *  backfill.c - simple backfill scheduler plugin. 
 *
 *  If a partition is does not have root only access and nodes are not shared
 *  then raise the priority of pending jobs if doing so does not adversely
 *  effect the expected initiation of any higher priority job. We do not alter
 *  a job's required or excluded node list, so this is a conservative 
 *  algorithm.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

typedef struct part_specs {
	uint32_t idle_node_cnt;
	uint32_t min_cpus;
	uint32_t min_mem;
	uint32_t min_disk;
} part_specs_t;

typedef struct node_space_map {
	uint32_t idle_node_cnt;
	time_t time;
} node_space_map_t;

List pend_job_list = NULL;
List run_job_list  = NULL;

#define MAX_JOB_CNT 100
int node_space_recs;
node_space_map_t node_space[MAX_JOB_CNT + 1];

/* Set __DEBUG to get detailed logging for this thread without 
 * detailed logging for the entire slurmctld daemon */
#define __DEBUG        0
#define SLEEP_TIME     60

static int  _add_pending_job(job_info_t *job_ptr, partition_info_t *part_ptr,
		part_specs_t *part_specs);
static int  _add_running_job(job_info_t *job_ptr, node_info_msg_t *node_info);
static void _attempt_backfill(partition_info_t *part_ptr, 
		node_info_msg_t *node_info, job_info_msg_t *job_info);
static void _backfill_part(part_specs_t *part_specs);
static void _build_node_space_map(part_specs_t *part_specs);
static void _change_prio(uint32_t job_id, uint32_t prio);
static void _dump_node_space_map(uint32_t job_id, uint32_t node_cnt);
static int  _get_avail_node_cnt(job_info_t *job_ptr, 
		node_info_msg_t *node_info);
static void _get_part_specs(partition_info_t *part_ptr, 
		node_info_msg_t *node_info, part_specs_t *part_specs);
static bool _has_state_changed(job_info_msg_t *job_buffer_ptr,
		node_info_msg_t * node_buffer_ptr,
		partition_info_msg_t *part_buffer_ptr);
static int  _load_jobs(job_info_msg_t ** job_buffer_pptr);
static int  _load_nodes(node_info_msg_t ** node_buffer_pptr);
static int  _load_partitions(partition_info_msg_t **part_buffer_pptr);
static int  _loc_restrict(job_info_t *job_ptr, part_specs_t *part_specs);
static int  _sort_by_prio(void *x, void *y);
static int  _sort_by_end(void *x, void *y);

/* list processing function, sort jobs by _decreasing_ priority */
static int _sort_by_prio(void *x, void *y)
{
	job_info_t *job_ptr1 = (job_info_t *) x;
	job_info_t *job_ptr2 = (job_info_t *) y;
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
	job_info_t *job_ptr1 = (job_info_t *) x;
	job_info_t *job_ptr2 = (job_info_t *) y;
	double diff = difftime(job_ptr1->end_time, job_ptr2->end_time);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;
	else
		return 0;
}

/* backfill_agent - detached thread periodically attempts to backfill jobs */
extern void *
backfill_agent(void *args)
{
	int i;
	job_info_msg_t *job_info        = NULL;
	node_info_msg_t *node_info      = NULL;
	partition_info_msg_t *part_info = NULL;
	partition_info_t *part_ptr      = NULL;

	while (1) {
		sleep(SLEEP_TIME);

		/* load all state info */
		if ( _load_jobs(&job_info)         || 
		     _load_nodes(&node_info)       ||
		     _load_partitions(&part_info) )
			continue;

		if ( !_has_state_changed(job_info, node_info, part_info) ) 
			continue;

		/* identify partitions eligible for backfill */
		part_ptr = part_info->partition_array;
		for (i = 0; i < part_info->record_count; i++, part_ptr++) {
			if ( ((part_ptr->root_only)    ||
			      (part_ptr->shared)       ||
			      (part_ptr->state_up == 0)) )
			 	continue; /* not under our control */
			_attempt_backfill(part_ptr, node_info, job_info);
		}
	}
}

/* Load current job table information into *job_buffer_pptr */
static int 
_load_jobs (job_info_msg_t ** job_buffer_pptr) 
{
	int error_code;
	static job_info_msg_t *old_job_buffer_ptr = NULL;
	job_info_msg_t * job_buffer_ptr = NULL;

	if (old_job_buffer_ptr) {
		error_code = slurm_load_jobs (old_job_buffer_ptr->last_update, 
					&job_buffer_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg (old_job_buffer_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_buffer_ptr = old_job_buffer_ptr;
			error_code = SLURM_SUCCESS;
		}
	}
	else
		error_code = slurm_load_jobs ((time_t) NULL, &job_buffer_ptr);

	if (error_code == SLURM_SUCCESS) {
		old_job_buffer_ptr = job_buffer_ptr;
		*job_buffer_pptr = job_buffer_ptr;
	}  else
		error("backfill/slurm_load_jobs: %s", 
			slurm_strerror(slurm_get_errno()) );

	return error_code;
}

/* Load current job table information into *node_buffer_pptr */
static int 
_load_nodes (node_info_msg_t ** node_buffer_pptr) 
{
	int error_code;
	static node_info_msg_t *old_node_info_ptr = NULL;
	node_info_msg_t *node_info_ptr = NULL;

	if (old_node_info_ptr) {
		error_code = slurm_load_node (old_node_info_ptr->last_update, 
			&node_info_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_node_info_msg (old_node_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			node_info_ptr = old_node_info_ptr;
			error_code = SLURM_SUCCESS;
		}
	}
	else
		error_code = slurm_load_node ((time_t) NULL, &node_info_ptr);

	if (error_code == SLURM_SUCCESS) {
		old_node_info_ptr = node_info_ptr;
		*node_buffer_pptr = node_info_ptr;
	}  else
		error("backfill/slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()) );

	return error_code;
}

/* Load current partiton table information into *part_buffer_pptr */
static int 
_load_partitions (partition_info_msg_t **part_buffer_pptr)
{
	int error_code;
	static partition_info_msg_t *old_part_info_ptr = NULL;
	partition_info_msg_t *part_info_ptr = NULL;

	if (old_part_info_ptr) {
		error_code = slurm_load_partitions (
						old_part_info_ptr->last_update,
						&part_info_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg (old_part_info_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			part_info_ptr = old_part_info_ptr;
			error_code = SLURM_SUCCESS;
		}
	}
	else
		error_code = slurm_load_partitions ((time_t) NULL, 
						    &part_info_ptr);

	if (error_code == SLURM_SUCCESS) {
		old_part_info_ptr = part_info_ptr;
		*part_buffer_pptr = part_info_ptr;
	}  else
		error("backfill/slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()) );

	return error_code;
}

/* Report if any changes occured to job, node or partition information */
static bool
_has_state_changed(job_info_msg_t *job_buffer_ptr,
		 node_info_msg_t * node_buffer_ptr,
		 partition_info_msg_t *part_buffer_ptr)
{
	static time_t last_job_time  = (time_t) 0;
        static time_t last_node_time = (time_t) 0;
	static time_t last_part_time = (time_t) 0;

	if ( (last_job_time  == job_buffer_ptr->last_update ) &&
	     (last_node_time == node_buffer_ptr->last_update) &&
	     (last_part_time == part_buffer_ptr->last_update) )
		return false;

	last_job_time  = job_buffer_ptr->last_update;
	last_node_time = node_buffer_ptr->last_update;
	last_part_time = part_buffer_ptr->last_update;
	return true;
}

/* Attempt to perform backfill scheduling on the specified partition */
static void 
_attempt_backfill(partition_info_t *part_ptr, node_info_msg_t *node_info, 
		job_info_msg_t *job_info)
{
	int i;
	uint32_t max_pending_prio = 0;
	uint32_t min_pend_job_size = INFINITE;
	job_info_t *job_ptr;
	part_specs_t part_specs;

#if __DEBUG
	info("backfill attempt on partition %s", part_ptr->name);
#endif

	_get_part_specs(part_ptr, node_info, &part_specs);
	if (part_specs.idle_node_cnt == 0)
		return;		/* no idle nodes */

	if (pend_job_list)
		list_destroy(pend_job_list);
	pend_job_list = list_create(NULL);
	if (run_job_list)
		list_destroy(run_job_list);
	run_job_list = list_create(NULL);
		
	/* build lists of pending and running jobs in this partition */
	job_ptr = job_info->job_array;
	for (i = 0; i < job_info->record_count; i++, job_ptr++) {
		if ( strcmp(part_ptr->name, job_ptr->partition) )
			continue;	/* job in different partition */

		if (job_ptr->job_state & JOB_COMPLETING) {
#if __DEBUG
			info("backfill: Job %u completing, skip partition", 
					job_ptr->job_id);
#endif
			return;
		} else if (job_ptr->job_state == JOB_RUNNING) {
			if (_add_running_job(job_ptr, node_info))
				return;		/* error */
		} else if (job_ptr->job_state == JOB_PENDING) {
			if (max_pending_prio < job_ptr->priority)
				max_pending_prio = job_ptr->priority;
			if (_add_pending_job(job_ptr, part_ptr, &part_specs))
				return;		/* error */
			if (min_pend_job_size > job_ptr->num_nodes)
				min_pend_job_size = job_ptr->num_nodes;
		}
	}
	i = list_count(run_job_list);
	if ( (i == 0) || (i > MAX_JOB_CNT) )
		return;		/* no running jobs or already have many */
	if (list_is_empty(pend_job_list))
		return;		/* no pending jobs */
	if (min_pend_job_size > part_specs.idle_node_cnt)
		return;		/* not enough free nodes for any pending job */

	list_sort(pend_job_list, _sort_by_prio);
	list_sort(run_job_list, _sort_by_end);
	_build_node_space_map(&part_specs);
	_backfill_part(&part_specs);
}

/* get the specs on nodes within a partition */
static void 
_get_part_specs(partition_info_t *part_ptr, node_info_msg_t *node_info, 
		part_specs_t *part_specs)
{
	node_info_t *node_ptr = node_info->node_array;
	int i, j;

	part_specs->idle_node_cnt = 0;
	part_specs->min_cpus      = INFINITE;
	part_specs->min_mem       = INFINITE;
	part_specs->min_disk      = INFINITE;

	for (i=0; ; i+=2) {
		if (part_ptr->node_inx[i] == -1)
			break;
		for (j=part_ptr->node_inx[i]; j<=part_ptr->node_inx[i+1]; 
				j++) {
			if (part_specs->min_cpus > node_ptr[j].cpus)
				part_specs->min_cpus = node_ptr[j].cpus;
			if (part_specs->min_mem  > node_ptr[j].real_memory)
				part_specs->min_mem  = node_ptr[j].real_memory;
			if (part_specs->min_disk > node_ptr[j].tmp_disk)
				part_specs->min_disk = node_ptr[j].tmp_disk;
			if (node_ptr[j].node_state == NODE_STATE_IDLE)
				part_specs->idle_node_cnt++;
		}
	}

#if __DEBUG
	debug("backfill: partition %s minimum cpus=%u mem=%u disk=%u",
		part_ptr->name, part_specs->min_cpus, 
		part_specs->min_mem, part_specs->min_disk);
#endif
}

/* Add specified pending job to our records */
static int
_add_pending_job(job_info_t *job_ptr, partition_info_t *part_ptr, 
		part_specs_t *part_specs)
{
	int min_node_cnt;

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

	/* figure out how many nodes this job needs */
	min_node_cnt = (job_ptr->num_procs + part_specs->min_cpus - 1) /
			part_specs->min_cpus;	/* round up */
	if (job_ptr->num_nodes < min_node_cnt)
		job_ptr->num_nodes = min_node_cnt;

	if (job_ptr->num_nodes > part_ptr->max_nodes) {
#if __DEBUG
		info("backfill: pending job %u exceeds partition node limit", 
				job_ptr->job_id);
#endif
		return 0;	/* Skip this job */
	}

#if __DEBUG
	info("backfill: job %u pending on %d nodes", job_ptr->job_id, 
		job_ptr->num_nodes);
#endif

	list_append(pend_job_list, (void *) job_ptr);
	return 0;
}

/* Add specified running job to our records */
static int
_add_running_job(job_info_t *job_ptr, node_info_msg_t *node_info)
{
	job_ptr->num_nodes = _get_avail_node_cnt(job_ptr, node_info);
#if __DEBUG
	info("backfill: job %u running on %d nodes: %s", job_ptr->job_id, 
			job_ptr->num_nodes, job_ptr->nodes);
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
	job_info_t *run_job_ptr;
	int base_size = 0;

	node_space_recs = 0;

	if (part_specs->idle_node_cnt) {
		base_size = part_specs->idle_node_cnt;
		node_space[node_space_recs].idle_node_cnt = base_size; 
		node_space[node_space_recs++].time = time(NULL);
	}

	run_job_iterate = list_iterator_create(run_job_list);
	while ( (run_job_ptr = list_next(run_job_iterate)) ) {
		if (run_job_ptr->num_nodes == 0)
			continue;	/* no nodes returning to service */
		base_size += run_job_ptr->num_nodes;
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
		info("initial node_space_map");
	else
		info("node_space_map after job %u allocated %u nodes", 
				job_id, node_cnt);

	now = time(NULL);
	for (i=0; i<node_space_recs; i++) {
		info("%3d nodes at time %4d (seconds in future)",
			node_space[i].idle_node_cnt,	
			(int) difftime(node_space[i].time, now));
	}
#endif
}

/* return 1 if the job could be started now, 0 otherwise and add job into
 * node_space_map
 */
static int
_update_node_space_map(job_info_t *job_ptr)
{
	int i, j, min_nodes;

	if (node_space_recs == 0)	/* no nodes now or in future */
		return 0;

	min_nodes = node_space[0].idle_node_cnt;
	for (i=1; i<node_space_recs; i++) {
		if (min_nodes > node_space[i].idle_node_cnt)
			min_nodes = node_space[i].idle_node_cnt;
	}
	if (job_ptr->num_nodes <= min_nodes)
		return 1;

	for (i=0; i<node_space_recs; i++) {
		int fits = 0;
		if (node_space[i].idle_node_cnt < job_ptr->num_nodes)
			continue;	/* can't start yet... */
		fits = 1;
		for (j=i; j<node_space_recs; j++) {
			if (node_space[j].idle_node_cnt < job_ptr->num_nodes) {
				fits = 0;
				break;
			}
		}
		if (fits == 0)
			continue;
		for (j=i; j<node_space_recs; j++) {
			node_space[j].idle_node_cnt -= job_ptr->num_nodes;
		}
		break;
	}

	_dump_node_space_map(job_ptr->job_id, job_ptr->num_nodes);
	return 0;
}

/* return the number of nodes to be returned to this partition when
 * the specified job terminates */
static int
_get_avail_node_cnt(job_info_t *job_ptr, node_info_msg_t *node_info)
{
	int i, j, cnt = 0;

	for (i=0; ; i+=2) {
		if (job_ptr->node_inx[i] == -1)
			break;
		for (j=job_ptr->node_inx[i]; j<=job_ptr->node_inx[i+1]; j++) {
			node_info_t *node_ptr = &node_info->node_array[j];
			if ((node_ptr->node_state != NODE_STATE_ALLOCATED) ||
			    (strcmp(node_ptr->partition, job_ptr->partition)))
				continue;
			cnt++;
		}
	}
	return cnt;
}


/* scan pending job queue and change the priority of any that 
 * can run now without delaying the expected initiation time 
 * of any higher priority job */
static void 
_backfill_part(part_specs_t *part_specs)
{
	job_info_t *pend_job_ptr;
	ListIterator pend_job_iterate;
	job_info_t *first_job = NULL;	/* just used as flag */

	/* find job to possibly backfill */
	pend_job_iterate = list_iterator_create(pend_job_list);
	while ( (pend_job_ptr = list_next(pend_job_iterate)) ) {
		if (_loc_restrict(pend_job_ptr, part_specs)) {
#if __DEBUG
			info("Job %u has locality restrictions",
				pend_job_ptr->job_id);
#endif
			break;
		}

		if (first_job == NULL) {
			if (pend_job_ptr->num_nodes <= 
					part_specs->idle_node_cnt) {
#if __DEBUG
				info("Job %u should start via FIFO",
					pend_job_ptr->job_id);
#endif
				break;
			}
			first_job = pend_job_ptr;
		}

		if (_update_node_space_map(pend_job_ptr)) {
			_change_prio(pend_job_ptr->job_id, 
				(first_job->priority + 1));
			break;
		}
	}
	list_iterator_destroy(pend_job_iterate);
}

/* Return 1 if job has locality restrictions, 0 otherwise */
static int
_loc_restrict(job_info_t *job_ptr, part_specs_t *part_specs)
{
	if ( (job_ptr->contiguous) ||
	     (job_ptr->req_nodes && job_ptr->req_nodes[0]) ||
	     (job_ptr->exc_nodes && job_ptr->exc_nodes[0]) )
		return 1;

	if ( (job_ptr->min_procs    > part_specs->min_cpus) ||
	     (job_ptr->min_memory   > part_specs->min_mem) ||
	     (job_ptr->min_tmp_disk > part_specs->min_disk) )
		return 1;

	return 0;
}

/* Change the priority of a pending job to get it running now */
static void
_change_prio(uint32_t job_id, uint32_t prio)
{
	job_desc_msg_t update_job_msg;

	slurm_init_job_desc_msg(&update_job_msg);
	update_job_msg.job_id   = job_id;
	update_job_msg.priority = prio;
	info("set job %u to priority %u\n", job_id, prio);
	if (slurm_update_job(&update_job_msg))
		error("backfill/slurm_update_job: %s",
			slurm_strerror(slurm_get_errno()) );
}

