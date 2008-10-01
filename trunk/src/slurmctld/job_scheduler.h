/*****************************************************************************\
 *  job_scheduler.h - data structures and function definitions for scheduling
 *	of pending jobs in priority order
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
 *  Derived from dsh written by Jim Garlick <garlick1@llnl.gov>
 *  LLNL-CODE-402394.
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

#ifndef _JOB_SCHEDULER_H
#define _JOB_SCHEDULER_H

#include "src/slurmctld/slurmctld.h"

struct job_queue {
	struct job_record *job_ptr;
	uint32_t job_priority;
	uint16_t part_priority;
};

/*
 * build_feature_list - Translate a job's feature string into a feature_list
 * IN  details->features
 * OUT details->feature_list
 * RET error code
 */
extern int build_feature_list(struct job_record *job_ptr);

/* 
 * build_job_queue - build (non-priority ordered) list of pending jobs
 * OUT job_queue - pointer to job queue
 * RET number of entries in job_queue
 * NOTE: the buffer at *job_queue must be xfreed by the caller
 */
extern int build_job_queue(struct job_queue **job_queue);

/*
 * job_is_completing - Determine if jobs are in the process of completing.
 * RET - True of any job is in the process of completing
 * NOTE: This function can reduce resource fragmentation, which is a 
 * critical issue on Elan interconnect based systems.
 */

extern bool job_is_completing(void);

/* Determine if a pending job will run using only the specified nodes
 * (in job_desc_msg->req_nodes), build response message and return 
 * SLURM_SUCCESS on success. Otherwise return an error code. Caller 
 * must free response message */
extern int job_start_data(job_desc_msg_t *job_desc_msg, 
			  will_run_response_msg_t **resp);

/*
 * launch_job - send an RPC to a slurmd to initiate a batch job 
 * IN job_ptr - pointer to job that will be initiated
 */
extern void launch_job(struct job_record *job_ptr);

/*
 * make_batch_job_cred - add a job credential to the batch_job_launch_msg
 * IN/OUT launch_msg_ptr - batch_job_launch_msg in which job_id, step_id, 
 *                         uid and nodes have already been set 
 * IN job_ptr - pointer to job record
 * RET 0 or error code
 */
extern int make_batch_job_cred(batch_job_launch_msg_t *launch_msg_ptr,
			       struct job_record *job_ptr);

/* Print a job's dependency information based upon job_ptr->depend_list */
extern void print_job_dependency(struct job_record *job_ptr);

/*
 * prolog_slurmctld - execute the prolog_slurmctld for a job that has just
 *	been allocated resources.
 * IN job_ptr - pointer to job that will be initiated
 * RET SLURM_SUCCESS(0) or error code
 */
extern int prolog_slurmctld(struct job_record *job_ptr);

/* 
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority  
 *	order until a request fails
 * RET count of jobs scheduled
 * Note: We re-build the queue every time. Jobs can not only be added 
 *	or removed from the queue, but have their priority or partition 
 *	changed with the update_job RPC. In general nodes will be in priority 
 *	order (by submit time), so the sorting should be pretty fast.
 */
extern int schedule(void);

/*
 * set_job_elig_time - set the eligible time for pending jobs once their 
 *	dependencies are lifted (in job->details->begin_time)
 */
extern void set_job_elig_time(void);

/* 
 * sort_job_queue - sort job_queue in decending priority order
 * IN job_queue_size - count of elements in the job queue
 * IN/OUT job_queue - pointer to sorted job queue
 */
extern void sort_job_queue(struct job_queue *job_queue, int job_queue_size);

/*
 * Determine if a job's dependencies are met
 * RET: 0 = no dependencies
 *      1 = dependencies remain
 *      2 = failure (job completion code not per dependency), delete the job
 */
extern int test_job_dependency(struct job_record *job_ptr);

/*
 * Parse a job dependency string and use it to establish a "depend_spec" 
 * list of dependencies. We accept both old format (a single job ID) and
 * new format (e.g. "afterok:123:124,after:128").
 * IN job_ptr - job record to have dependency and depend_list updated
 * IN new_depend - new dependency description
 * RET returns an error code from slurm_errno.h
 */
extern int update_job_dependency(struct job_record *job_ptr, char *new_depend);

#endif /* !_JOB_SCHEDULER_H */
