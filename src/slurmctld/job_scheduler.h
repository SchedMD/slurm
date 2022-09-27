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

#ifndef _JOB_SCHEDULER_H
#define _JOB_SCHEDULER_H

#include "src/slurmctld/slurmctld.h"

typedef struct job_queue_rec {
	uint32_t array_task_id;		/* Job array, task ID */
	uint32_t job_id;		/* Job ID */
	job_record_t *job_ptr;		/* Pointer to job record */
	part_record_t *part_ptr;	/* Pointer to partition record. Each
					 * job may have multiple partitions. */
	uint32_t priority;		/* Job priority in THIS partition */
	slurmctld_resv_t *resv_ptr;     /* If job didn't ask for a reservation,
					 * this reservation is one it can run
					 * in without requesting */
	bool use_prefer; /* This is a separate queue record to evaluate the
			    job's prefer constraint. */
} job_queue_rec_t;

/* Use as return values for test_job_dependency. */
enum {
	NO_DEPEND = 0,
	LOCAL_DEPEND,
	FAIL_DEPEND,
	REMOTE_DEPEND
};

extern void main_sched_init(void);

extern void main_sched_fini(void);

/*
 * build_feature_list - Translate a job's feature string into a feature_list
 * IN  details->features|prefer
 * OUT details->feature_list|prefer_list
 * IN  prefer - if prefer or feature is being processed
 * RET error code
 */
extern int build_feature_list(job_record_t *job_ptr, bool prefer);

/*
 * Set up job_queue_rec->job_ptr to use a magnetic reservation if the
 * job_queue_rec has resv_name filled in.
 */
extern void job_queue_rec_magnetic_resv(job_queue_rec_t *job_queue_rec);

/*
 * If a job requested multiple reservations to potentially run in queue
 * them now.
 */
extern void job_queue_rec_resv_list(job_queue_rec_t *job_queue_rec);

/*
 * build_job_queue - build (non-priority ordered) list of pending jobs
 * IN clear_start - if set then clear the start_time for pending jobs
 * IN backfill - true if running backfill scheduler, enforce min time limit
 * RET the job queue
 * NOTE: the caller must call list_destroy() on RET value to free memory
 */
extern List build_job_queue(bool clear_start, bool backfill);

/* Given a scheduled job, return a pointer to it batch_job_launch_msg_t data */
extern batch_job_launch_msg_t *build_launch_job_msg(job_record_t *job_ptr,
						    uint16_t protocol_version);

/* Determine if job's deadline specification is still valid, kill job if not
 * job_ptr IN - Job to test
 * func IN - function named used for logging, "sched" or "backfill"
 * RET - true of valid, false if invalid and job cancelled
 */
extern bool deadline_ok(job_record_t *job_ptr, char *func);

/*
 * epilog_slurmctld - execute the prolog_slurmctld for a job that has just
 *	terminated.
 * IN job_ptr - pointer to job that has been terminated
 */
extern void epilog_slurmctld(job_record_t *job_ptr);

/*
 * Delete a record from a job's feature_list
 */
extern void feature_list_delete(void *x);

/*
 * Return a pointer to the dependency in job_ptr's dependency list that
 * matches dep_ptr, or NULL if none is found.
 *
 * A dependency "matches" when the job_id and depend_type are the same.
 */
extern depend_spec_t *find_dependency(job_record_t *job_ptr,
				      depend_spec_t *dep_ptr);

/*
 * Update a job's state_reason, state_desc, and dependency string based on the
 * states of its dependencies.
 *
 * This is called by list_for_each() and thus has 2 void* parameters:
 * object is a pointer to job_record_t.
 * arg is unused.
 */
extern int handle_job_dependency_updates(void *object, void *arg);

/*
 * job_is_completing - Determine if jobs are in the process of completing.
 * IN/OUT  eff_cg_bitmap - optional bitmap of all relevent completing nodes,
 *                         relevenace determined by filtering via CompleteWait
 *                         if NULL, function will terminate at first completing
 *                         job
 * RET - True of any job is in the process of completing AND
 *	 CompleteWait is configured non-zero
 * NOTE: This function can reduce resource fragmentation, which is a
 * critical issue on Elan interconnect based systems.
 */
extern bool job_is_completing(bitstr_t *eff_cg_bitmap);

/*
 * Determine if a pending job will run using only the specified nodes, build
 * response message and return SLURM_SUCCESS on success. Otherwise return an
 * error code. Caller must free response message.
 */
extern int job_start_data(job_record_t *job_ptr,
			  will_run_response_msg_t **resp);

/*
 * launch_job - send an RPC to a slurmd to initiate a batch job
 * IN job_ptr - pointer to job that will be initiated
 */
extern void launch_job(job_record_t *job_ptr);

/*
 * make_batch_job_cred - add a job credential to the batch_job_launch_msg
 * IN/OUT launch_msg_ptr - batch_job_launch_msg in which job_id, step_id,
 *                         uid and nodes have already been set
 * IN job_ptr - pointer to job record
 * RET 0 or error code
 */
extern int make_batch_job_cred(batch_job_launch_msg_t *launch_msg_ptr,
			       job_record_t *job_ptr,
			       uint16_t protocol_version);

/*
 * Determine which nodes must be rebooted for a job
 * IN job_ptr - pointer to job that will be initiated
 * RET bitmap of nodes requiring a reboot for NodeFeaturesPlugin or NULL if none
 */
extern bitstr_t *node_features_reboot(job_record_t *job_ptr);

/* Print a job's dependency information based upon job_ptr->depend_list */
extern void print_job_dependency(job_record_t *job_ptr, const char *func);

/* Decrement a job's prolog_running counter and launch the job if zero */
extern void prolog_running_decr(job_record_t *job_ptr);

/*
 * prolog_slurmctld - execute the prolog_slurmctld for a job that has just
 *	been allocated resources.
 * IN job_ptr - pointer to job that will be initiated
 */
extern void prolog_slurmctld(job_record_t *job_ptr);

/*
 * reboot_job_nodes - Reboot the compute nodes allocated to a job.
 * IN job_ptr - pointer to job that will be initiated
 * RET SLURM_SUCCESS(0) or error code
 */
extern void reboot_job_nodes(job_record_t *job_ptr);

/* If a job can run in multiple partitions, make sure that the one
 * actually used is first in the string. Needed for job state save/restore */
extern void rebuild_job_part_list(job_record_t *job_ptr);

/*
 * Queue requests of job scheduler
 */
extern void schedule(bool full_queue);

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
 * Inputs: job_ptr
 * Outputs: was_changed (optional) -
 *          If it exists, set it to true if at least 1 dependency changed
 *          state, otherwise false.
 * RET: NO_DEPEND = no dependencies
 *      LOCAL_DEPEND = dependencies remain
 *      FAIL_DEPEND = failure (job completion code not per dependency),
 *                    delete the job
 */
extern int test_job_dependency(job_record_t *job_ptr, bool *was_changed);

/*
 * Parse a job dependency string and use it to establish a "depend_spec"
 * list of dependencies. We accept both old format (a single job ID) and
 * new format (e.g. "afterok:123:124,after:128").
 * IN job_ptr - job record to have dependency and depend_list updated
 * IN new_depend - new dependency description
 * RET returns an error code from slurm_errno.h
 */
extern int update_job_dependency(job_record_t *job_ptr, char *new_depend);

/*
 * new_depend_list is a dependency list that came from a sibling cluster. It
 * has updates to the job dependencies on that cluster. Use those changes to
 * update the dependency list of job_ptr.
 * Return true if a dependency was updated, false if not.
 */
extern bool update_job_dependency_list(job_record_t *job_ptr,
				       List new_depend_list);

/*
 * When an array job is rejected for some reason, the remaining array tasks will
 * get skipped by both the main scheduler and the backfill scheduler (it's an
 * optimization). Hence, their reasons should match the reason of the first job.
 * This function sets those reasons.
 *
 * job_ptr		(IN) The current job being evaluated, after it has gone
 * 			through the scheduling loop.
 * reject_array_job	(IN) A pointer to the first job (array task) in the most
 * 			recently rejected array job. If job_ptr belongs to the
 * 			same array as reject_array_job, then set job_ptr's
 * 			reason to match reject_array_job.
 */
extern void fill_array_reasons(job_record_t *job_ptr,
			       job_record_t *reject_arr_job);


/* Add a job_queue_rec_t to job_queue */
extern void job_queue_append_internal(job_queue_req_t *job_queue_req);

#endif /* !_JOB_SCHEDULER_H */
