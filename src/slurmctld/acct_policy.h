/*****************************************************************************\
 *  acct_policy.h - definitions of functions in acct_policy.c
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov> et. al.
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

#ifndef _HAVE_ACCT_POLICY_H
#define _HAVE_ACCT_POLICY_H

#define ADMIN_SET_LIMIT 0xffff

typedef struct {
	uint16_t max_cpus;
	uint16_t max_nodes;
	uint16_t min_cpus;
	uint16_t min_nodes;
	uint16_t pn_min_memory;
	uint16_t qos;
	uint16_t time;
} acct_policy_limit_set_t;

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

extern bool acct_policy_validate(job_desc_msg_t *job_desc,
				 struct part_record *part_ptr,
				 slurmdb_association_rec_t *assoc_in,
				 slurmdb_qos_rec_t *qos_ptr,
				 uint32_t *state_reason,
				 acct_policy_limit_set_t *acct_policy_limit_set,
				 bool update_call);

/*
 * acct_policy_job_runnable_pre_select - Determine of the specified
 *	job can execute right now or not depending upon accounting
 *	policy (e.g. running job limit for this association). If the
 *	association limits prevent the job from ever running (lowered
 *	limits since job submission), then cancel the job.
 */
extern bool acct_policy_job_runnable_pre_select(struct job_record *job_ptr);

/*
 * acct_policy_job_runnable_post_select - After nodes have been
 *	selected for the job verify the counts don't exceed aggregated limits.
 */
extern bool acct_policy_job_runnable_post_select(
	struct job_record *job_ptr, uint32_t cpu_cnt,
	uint32_t node_cnt, uint32_t pn_min_memory);

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

#endif /* !_HAVE_ACCT_POLICY_H */
