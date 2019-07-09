/*****************************************************************************\
 *  acct_policy.h - definitions of functions in acct_policy.c
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov> et. al.
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

#ifndef _HAVE_ACCT_POLICY_H
#define _HAVE_ACCT_POLICY_H

#include "src/common/list.h"

/*
 * acct_policy_add_job_submit - Note that a job has been submitted for
 *	accounting policy purposes.
 */
extern void acct_policy_add_job_submit(struct job_record *job_ptr);

/*
 * acct_policy_remove_job_submit - Note that a job has finished (might
 *      not had started or been allocated resources) for accounting
 *      policy purposes.
 */
extern void acct_policy_remove_job_submit(struct job_record *job_ptr);

/*
 * acct_policy_job_begin - Note that a job is starting for accounting
 *	policy purposes.
 */
extern void acct_policy_job_begin(struct job_record *job_ptr);

/*
 * acct_policy_job_fini - Note that a job is completing for accounting
 *	policy purposes.
 */
extern void acct_policy_job_fini(struct job_record *job_ptr);

/*
 * acct_policy_alter_job - if resources change on a job this needs to
 * be called after they have been validated, but before they actually
 * do.  Each of the resources can be changed one at a time.  If no
 * change happens on a resouce just put old values in for the new.
 * At the time of writing this function any node or cpu size change
 * while running was already handled in the job_pre|post_resize_acctg functions.
 */
extern void acct_policy_alter_job(struct job_record *job_ptr,
				  uint32_t new_time_limit);

/*
 * acct_policy_validate - validate that a job request can be satisfied without
 * exceeding any association or QOS limit.
 * job_desc IN - job descriptor being submitted
 * part_ptr IN - pointer to (one) partition to which the job is being submitted
 * assoc_in IN - pointer to association to which the job is being submitted
 * qos_ptr IN - pointer to QOS to which the job is being submitted
 * state_reason OUT - if non-NULL, set to reason for rejecting the job
 * acct_policy_limit_set IN/OUT - limits set for the job, pre-allocated storage
 *		is filled in by acct_policy_validate
 * update_call IN - true if request to update existing job request
 * RET true if valid
 */
extern bool acct_policy_validate(job_desc_msg_t *job_desc,
				 struct part_record *part_ptr,
				 slurmdb_assoc_rec_t *assoc_in,
				 slurmdb_qos_rec_t *qos_ptr,
				 uint32_t *state_reason,
				 acct_policy_limit_set_t *acct_policy_limit_set,
				 bool update_call);

/*
 * acct_policy_validate_pack - validate that a pack job as a whole (all
 * components at once) can be satisfied without exceeding any association or
 * QOS limit.
 * submit_job_list IN - list of "struct job_record" entries (already created)
 * RET true if valid
 */
extern bool acct_policy_validate_pack(List submit_job_list);

/*
 * acct_policy_job_runnable_pre_select - Determine of the specified
 *	job can execute right now or not depending upon accounting
 *	policy (e.g. running job limit for this association). If the
 *	association limits prevent the job from ever running (lowered
 *	limits since job submission), then cancel the job.
 */
extern bool acct_policy_job_runnable_pre_select(struct job_record *job_ptr,
						bool assoc_mgr_locked);

/*
 * acct_policy_job_runnable_post_select - After nodes have been
 *	selected for the job verify the counts don't exceed aggregated limits.
 */
extern bool acct_policy_job_runnable_post_select(
	struct job_record *job_ptr, uint64_t *tres_req_cnt,
	bool assoc_mgr_locked);

/*
 * Determine of the specified job can execute right now or is currently
 * blocked by an association or QOS limit. Does not re-validate job state.
 */
extern bool acct_policy_job_runnable_state(struct job_record *job_ptr);

/*
 * Using the limits on the job get the max nodes possible.
 */
extern uint32_t acct_policy_get_max_nodes(struct job_record *job_ptr,
					  uint32_t *wait_reason);

/*
 * acct_policy_update_pending_job - Make sure the limits imposed on a
 *	job on submission are correct after an update to a qos or
 *	association.  If the association/qos limits prevent
 *	the job from ever running (lowered limits since job submission),
 *	then cancel the job.
 */
extern int acct_policy_update_pending_job(struct job_record *job_ptr);

/*
 * acct_policy_job_runnable - Determine of the specified job has timed
 *	out based on it's QOS or association. Returns True if job is
 *	timed out and sets job_ptr->state_reason = FAIL_TIMEOUT;
 */
extern bool acct_policy_job_time_out(struct job_record *job_ptr);

/*
 * acct_policy_handle_accrue_time - Set accrue time if we are under a limit.  If
 * we are a task array we will also split off things to handle them
 * individually.
 */
extern int acct_policy_handle_accrue_time(struct job_record *job_ptr,
					  bool assoc_mgr_locked);

/*
 * acct_policy_add_accrue_time - Implicitly add job to the accrue_cnt of the
 * assoc and QOS of the job/part.
 */
extern void acct_policy_add_accrue_time(struct job_record *job_ptr,
					bool assoc_mgr_locked);

extern void acct_policy_remove_accrue_time(struct job_record *job_ptr,
					   bool assoc_mgr_locked);

extern uint32_t acct_policy_get_prio_thresh(struct job_record *job_ptr,
					    bool assoc_mgr_locked);

/*
 * acct_policy_get_preemptable_time - get the time the job becomes preemptable
 * 	based on conf and qos PreemptExemptTime
 */
extern time_t acct_policy_get_preemptable_time(struct job_record *job_ptr);

/*
 * acct_policy_is_job_preemptable - Check if job is preemptable checking
 * 	global conf and qos options PreemptExemptTime
 * 	returns true if job is *exempt* from preemption
 */
extern bool acct_policy_is_job_preempt_exempt(struct job_record *job_ptr);

extern void acct_policy_set_qos_order(struct job_record *job_ptr,
				      slurmdb_qos_rec_t **qos_ptr_1,
				      slurmdb_qos_rec_t **qos_ptr_2);

extern slurmdb_used_limits_t *acct_policy_get_acct_used_limits(
	List *acct_limit_list, char *acct);

extern slurmdb_used_limits_t *acct_policy_get_user_used_limits(
	 List *user_limit_list, uint32_t user_id);

#endif /* !_HAVE_ACCT_POLICY_H */
