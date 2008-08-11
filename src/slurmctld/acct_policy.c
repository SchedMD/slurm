/*****************************************************************************\
 *  acct_policy.c - Enforce accounting policy
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <slurm/slurm_errno.h>

#include "src/common/assoc_mgr.h"
#include "src/common/slurm_accounting_storage.h"

#include "src/slurmctld/slurmctld.h"

#define _DEBUG 0


static void _cancel_job(struct job_record *job_ptr)
{
	time_t now = time(NULL);

	last_job_update = now;
	job_ptr->job_state = JOB_FAILED;
	job_ptr->exit_code = 1;
	job_ptr->state_reason = FAIL_BANK_ACCOUNT;
	xfree(job_ptr->state_desc);
	job_ptr->start_time = job_ptr->end_time = now;
	job_completion_logger(job_ptr);
	delete_job_details(job_ptr);
}

static bool _valid_job_assoc(struct job_record *job_ptr)
{
	acct_association_rec_t assoc_rec, *assoc_ptr;

	assoc_ptr = job_ptr->assoc_ptr;
	if ((assoc_ptr == NULL) ||
	    (assoc_ptr-> id != job_ptr->assoc_id) ||
	    (assoc_ptr->uid != job_ptr->user_id)) {
		error("Invalid assoc_ptr for jobid=%u", job_ptr->job_id);
		bzero(&assoc_rec, sizeof(acct_association_rec_t));
		assoc_rec.uid       = job_ptr->user_id;
		assoc_rec.partition = job_ptr->partition;
		assoc_rec.acct      = job_ptr->account;
		if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					    accounting_enforce, &assoc_ptr)) {
			info("_validate_job_assoc: invalid account or "
			     "partition for uid=%u jobid=%u",
			     job_ptr->user_id, job_ptr->job_id);
			return false;
		}
		job_ptr->assoc_id = assoc_rec.id;
		job_ptr->assoc_ptr = (void *) assoc_ptr;
	}
	return true;
}

/*
 * acct_policy_job_begin - Note that a job is starting for accounting
 *	policy purposes.
 */
extern void acct_policy_job_begin(struct job_record *job_ptr)
{
	acct_association_rec_t *assoc_ptr;

	if (!accounting_enforce || !_valid_job_assoc(job_ptr))
		return;

	assoc_ptr = job_ptr->assoc_ptr;
	assoc_ptr->used_jobs++;
}

/*
 * acct_policy_job_fini - Note that a job is completing for accounting
 *	policy purposes.
 */
extern void acct_policy_job_fini(struct job_record *job_ptr)
{
	acct_association_rec_t *assoc_ptr;

	if (!accounting_enforce || !_valid_job_assoc(job_ptr))
		return;

	assoc_ptr = job_ptr->assoc_ptr;
	if (assoc_ptr->used_jobs)
		assoc_ptr->used_jobs--;
	else
		error("acct_policy_job_fini: used_jobs underflow");
}

/*
 * acct_policy_job_runnable - Determine of the specified job can execute
 *	right now or not depending upon accounting policy (e.g. running
 *	job limit for this association). If the association limits prevent
 *	the job from ever running (lowered limits since job submissin), 
 *	then cancel the job.
 */
extern bool acct_policy_job_runnable(struct job_record *job_ptr)
{
	acct_association_rec_t *assoc_ptr;
	uint32_t time_limit;

	if (!accounting_enforce)
		return true;

	if (!_valid_job_assoc(job_ptr)) {
		_cancel_job(job_ptr);
		return false;
	}

	assoc_ptr = job_ptr->assoc_ptr;
#if _DEBUG
	info("acct_job_limits: %u of %u", 
	     assoc_ptr->used_jobs, assoc_ptr->max_jobs);
#endif

	if ((assoc_ptr->max_jobs != NO_VAL) &&
	    (assoc_ptr->max_jobs != INFINITE) &&
	    (assoc_ptr->used_jobs >= assoc_ptr->max_jobs)) {
		job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
		xfree(job_ptr->state_desc);
		return false;
	}

	/* if the association limits have changed since job
	 * submission and job can not run, then kill it */
	if ((assoc_ptr->max_wall_duration_per_job != NO_VAL) &&
	    (assoc_ptr->max_wall_duration_per_job != INFINITE)) {
		time_limit = assoc_ptr->max_wall_duration_per_job;
		if ((job_ptr->time_limit != NO_VAL) &&
		    (job_ptr->time_limit > time_limit)) {
			info("job %u being cancelled, time limit exceeds "
			     "account max (%u > %u)",
			     job_ptr->job_id, job_ptr->time_limit, time_limit);
			_cancel_job(job_ptr);
			return false;
		}
	}

	if ((assoc_ptr->max_nodes_per_job != NO_VAL) &&
	    (assoc_ptr->max_nodes_per_job != INFINITE)) {
		if (job_ptr->details->min_nodes > 
		    assoc_ptr->max_nodes_per_job) {
			info("job %u being cancelled,  min node limit exceeds "
			     "account max (%u > %u)",
			     job_ptr->job_id, job_ptr->details->min_nodes, 
			     assoc_ptr->max_nodes_per_job);
			_cancel_job(job_ptr);
			return false;
		}
	}

	/* NOTE: We can't enforce assoc_ptr->max_cpu_secs_per_job at this
	 * time because we don't have access to a CPU count for the job
	 * due to how all of the job's specifications interact */

	return true;
}
