/*****************************************************************************\
 *  job_scheduler.h - data structures and function definitions for scheduling
 *	of pending jobs in priority order
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>, et. al.
 *  Derived from dsh written by Jim Garlick <garlick1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _JOB_SCHEDULER_H
#define _JOB_SCHEDULER_H

#include "src/slurmctld/slurmctld.h"

typedef struct job_queue_rec {
	uint32_t job_id;		/* Job ID */
	struct job_record *job_ptr;	/* Pointer to job record */
	struct part_record *part_ptr;	/* Pointer to partition record. Each
					 * job may have multiple partitions. */
	uint32_t priority;		/* Job priority in THIS partition */
} job_queue_rec_t;

/*
 * build_feature_list - Translate a job's feature string into a feature_list
 * IN  details->features
 * OUT details->feature_list
 * RET error code
 */
extern int build_feature_list(struct job_record *job_ptr);

/*
 * build_job_queue - build (non-priority ordered) list of pending jobs
 * IN clear_start - if set then clear the start_time for pending jobs
 * IN backfill - true if running backfill scheduler, enforce min time limit
 * RET the job queue
 * NOTE: the caller must call list_destroy() on RET value to free memory
 */
extern List build_job_queue(bool clear_start, bool backfill);

/* Given a scheduled job, return a pointer to it batch_job_launch_msg_t data */
extern batch_job_launch_msg_t *build_launch_job_msg(
					struct job_record *job_ptr,
					uint16_t protocol_versin);
/*
 * epilog_slurmctld - execute the prolog_slurmctld for a job that has just
 *	terminated.
 * IN job_ptr - pointer to job that has been terminated
 * RET SLURM_SUCCESS(0) or error code
 */
extern int epilog_slurmctld(struct job_record *job_ptr);

/*
 * job_is_completing - Determine if jobs are in the process of completing.
 * RET - True of any job is in the process of completing AND
 *	 CompleteWait is configured non-zero
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
			       struct job_record *job_ptr,
			       uint16_t protocol_version);

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
 * reboot_job_nodes - Reboot the compute nodes allocated to a job.
 * IN job_ptr - pointer to job that will be initiated
 * RET SLURM_SUCCESS(0) or error code
 */
extern int reboot_job_nodes(struct job_record *job_ptr);

/* If a job can run in multiple partitions, make sure that the one 
 * actually used is first in the string. Needed for job state save/restore */
extern void rebuild_job_part_list(struct job_record *job_ptr);

/*
 * Given that one batch job just completed, attempt to launch a suitable
 * replacement batch job in a response messge as a REQUEST_BATCH_JOB_LAUNCH
 * message type, alternately send a return code fo SLURM_SUCCESS
 * msg IN - The original message from slurmd
 * fini_job_ptr IN - Pointer to job that just completed and needs replacement
 * RET true if there are pending jobs that might use the resources
 */
extern bool replace_batch_job(slurm_msg_t * msg, void *fini_job);

/*
 * schedule - attempt to schedule all pending jobs
 *	pending jobs for each partition will be scheduled in priority
 *	order until a request fails
 * IN job_limit - maximum number of jobs to test now, avoid testing the full
 *		  queue on every job submit (0 means to use the system default,
 *		  SchedulerParameters for default_queue_depth)
 * RET count of jobs scheduled
 * Note: We re-build the queue every time. Jobs can not only be added
 *	or removed from the queue, but have their priority or partition
 *	changed with the update_job RPC. In general nodes will be in priority
 *	order (by submit time), so the sorting should be pretty fast.
 */
extern int schedule(uint32_t job_limit);

/*
 * set_job_elig_time - set the eligible time for pending jobs once their
 *	dependencies are lifted (in job->details->begin_time)
 */
extern void set_job_elig_time(void);

/*
 * sort_job_queue - sort job_queue in decending priority order
 * IN/OUT job_queue - sorted job queue previously made by build_job_queue()
 */
extern void sort_job_queue(List job_queue);

/* Note this differs from the ListCmpF typedef since we want jobs sorted
 *	in order of decreasing priority */
extern int sort_job_queue2(void *x, void *y);

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
