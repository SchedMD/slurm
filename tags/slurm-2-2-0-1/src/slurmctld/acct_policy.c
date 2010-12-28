/*****************************************************************************\
 *  acct_policy.c - Enforce accounting policy
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <slurm/slurm_errno.h>

#include "src/common/assoc_mgr.h"
#include "src/common/slurm_accounting_storage.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/acct_policy.h"

#define _DEBUG 0

enum {
	ACCT_POLICY_ADD_SUBMIT,
	ACCT_POLICY_REM_SUBMIT,
	ACCT_POLICY_JOB_BEGIN,
	ACCT_POLICY_JOB_FINI
};

static void _cancel_job(struct job_record *job_ptr)
{
	time_t now = time(NULL);

	last_job_update = now;
	job_ptr->job_state = JOB_FAILED;
	job_ptr->exit_code = 1;
	job_ptr->state_reason = FAIL_ACCOUNT;
	xfree(job_ptr->state_desc);
	job_ptr->start_time = job_ptr->end_time = now;
	job_completion_logger(job_ptr, false);
	delete_job_details(job_ptr);
}

static bool _valid_job_assoc(struct job_record *job_ptr)
{
	slurmdb_association_rec_t assoc_rec, *assoc_ptr;

	assoc_ptr = (slurmdb_association_rec_t *)job_ptr->assoc_ptr;
	if ((assoc_ptr == NULL) ||
	    (assoc_ptr->id  != job_ptr->assoc_id) ||
	    (assoc_ptr->uid != job_ptr->user_id)) {
		error("Invalid assoc_ptr for jobid=%u", job_ptr->job_id);
		memset(&assoc_rec, 0, sizeof(slurmdb_association_rec_t));
		if(job_ptr->assoc_id)
			assoc_rec.id = job_ptr->assoc_id;
		else {
			assoc_rec.uid       = job_ptr->user_id;
			assoc_rec.partition = job_ptr->partition;
			assoc_rec.acct      = job_ptr->account;
		}
		if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					    accounting_enforce,
					    (slurmdb_association_rec_t **)
					    &job_ptr->assoc_ptr)) {
			info("_validate_job_assoc: invalid account or "
			     "partition for uid=%u jobid=%u",
			     job_ptr->user_id, job_ptr->job_id);
			return false;
		}
		job_ptr->assoc_id = assoc_rec.id;
	}
	return true;
}

static void _adjust_limit_usage(int type, struct job_record *job_ptr)
{
	slurmdb_association_rec_t *assoc_ptr = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK,
				   WRITE_LOCK, NO_LOCK, NO_LOCK };

	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)
	    || !_valid_job_assoc(job_ptr))
		return;

	assoc_mgr_lock(&locks);
	if (job_ptr->qos_ptr && (accounting_enforce & ACCOUNTING_ENFORCE_QOS)) {
		ListIterator itr = NULL;
		slurmdb_qos_rec_t *qos_ptr = NULL;
		slurmdb_used_limits_t *used_limits = NULL;

		qos_ptr = (slurmdb_qos_rec_t *)job_ptr->qos_ptr;
		if(!qos_ptr->usage->user_limit_list)
			qos_ptr->usage->user_limit_list =
				list_create(slurmdb_destroy_used_limits);
		itr = list_iterator_create(qos_ptr->usage->user_limit_list);
		while((used_limits = list_next(itr))) {
			if(used_limits->uid == job_ptr->user_id)
				break;
		}
		list_iterator_destroy(itr);
		if(!used_limits) {
			used_limits = xmalloc(sizeof(slurmdb_used_limits_t));
			used_limits->uid = job_ptr->user_id;
			list_append(qos_ptr->usage->user_limit_list,
				    used_limits);
		}
		switch(type) {
		case ACCT_POLICY_ADD_SUBMIT:
			qos_ptr->usage->grp_used_submit_jobs++;
			used_limits->submit_jobs++;
			break;
		case ACCT_POLICY_REM_SUBMIT:
			if(qos_ptr->usage->grp_used_submit_jobs)
				qos_ptr->usage->grp_used_submit_jobs--;
			else
				debug2("acct_policy_remove_job_submit: "
				       "grp_submit_jobs underflow for qos %s",
				       qos_ptr->name);

			if(used_limits->submit_jobs)
				used_limits->submit_jobs--;
			else
				debug2("acct_policy_remove_job_submit: "
				       "used_submit_jobs underflow for "
				       "qos %s user %d",
				       qos_ptr->name, used_limits->uid);
			break;
		case ACCT_POLICY_JOB_BEGIN:
			qos_ptr->usage->grp_used_jobs++;
			qos_ptr->usage->grp_used_cpus += job_ptr->total_cpus;
			qos_ptr->usage->grp_used_nodes += job_ptr->node_cnt;
			used_limits->jobs++;
			break;
		case ACCT_POLICY_JOB_FINI:
			if(qos_ptr->usage->grp_used_jobs)
				qos_ptr->usage->grp_used_jobs--;
			else
				debug2("acct_policy_job_fini: used_jobs "
				       "underflow for qos %s", qos_ptr->name);

			qos_ptr->usage->grp_used_cpus -= job_ptr->total_cpus;
			if((int32_t)qos_ptr->usage->grp_used_cpus < 0) {
				qos_ptr->usage->grp_used_cpus = 0;
				debug2("acct_policy_job_fini: grp_used_cpus "
				       "underflow for qos %s", qos_ptr->name);
			}

			qos_ptr->usage->grp_used_nodes -= job_ptr->node_cnt;
			if((int32_t)qos_ptr->usage->grp_used_nodes < 0) {
				qos_ptr->usage->grp_used_nodes = 0;
				debug2("acct_policy_job_fini: grp_used_nodes "
				       "underflow for qos %s", qos_ptr->name);
			}

			if(used_limits->jobs)
				used_limits->jobs--;
			else
				debug2("acct_policy_job_fini: used_jobs "
				       "underflow for qos %s user %d",
				       qos_ptr->name, used_limits->uid);
			break;
		default:
			error("acct_policy: qos unknown type %d", type);
			break;
		}
	}

	assoc_ptr = (slurmdb_association_rec_t *)job_ptr->assoc_ptr;
	while(assoc_ptr) {
		switch(type) {
		case ACCT_POLICY_ADD_SUBMIT:
			assoc_ptr->usage->used_submit_jobs++;
			break;
		case ACCT_POLICY_REM_SUBMIT:
			if (assoc_ptr->usage->used_submit_jobs)
				assoc_ptr->usage->used_submit_jobs--;
			else
				debug2("acct_policy_remove_job_submit: "
				       "used_submit_jobs underflow for "
				       "account %s",
				       assoc_ptr->acct);
			break;
		case ACCT_POLICY_JOB_BEGIN:
			assoc_ptr->usage->used_jobs++;
			assoc_ptr->usage->grp_used_cpus += job_ptr->total_cpus;
			assoc_ptr->usage->grp_used_nodes += job_ptr->node_cnt;
			break;
		case ACCT_POLICY_JOB_FINI:
			if (assoc_ptr->usage->used_jobs)
				assoc_ptr->usage->used_jobs--;
			else
				debug2("acct_policy_job_fini: used_jobs "
				       "underflow for account %s",
				       assoc_ptr->acct);

			assoc_ptr->usage->grp_used_cpus -= job_ptr->total_cpus;
			if ((int32_t)assoc_ptr->usage->grp_used_cpus < 0) {
				assoc_ptr->usage->grp_used_cpus = 0;
				debug2("acct_policy_job_fini: grp_used_cpus "
				       "underflow for account %s",
				       assoc_ptr->acct);
			}

			assoc_ptr->usage->grp_used_nodes -= job_ptr->node_cnt;
			if ((int32_t)assoc_ptr->usage->grp_used_nodes < 0) {
				assoc_ptr->usage->grp_used_nodes = 0;
				debug2("acct_policy_job_fini: grp_used_nodes "
				       "underflow for account %s",
				       assoc_ptr->acct);
			}
			break;
		default:
			error("acct_policy: association unknown type %d", type);
			break;
		}
		/* now handle all the group limits of the parents */
		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
	}
	assoc_mgr_unlock(&locks);
}

/*
 * acct_policy_add_job_submit - Note that a job has been submitted for
 *	accounting policy purposes.
 */
extern void acct_policy_add_job_submit(struct job_record *job_ptr)
{
	_adjust_limit_usage(ACCT_POLICY_ADD_SUBMIT, job_ptr);
}

/*
 * acct_policy_remove_job_submit - Note that a job has finished (might
 *      not had started or been allocated resources) for accounting
 *      policy purposes.
 */
extern void acct_policy_remove_job_submit(struct job_record *job_ptr)
{
	_adjust_limit_usage(ACCT_POLICY_REM_SUBMIT, job_ptr);
}

/*
 * acct_policy_job_begin - Note that a job is starting for accounting
 *	policy purposes.
 */
extern void acct_policy_job_begin(struct job_record *job_ptr)
{
	_adjust_limit_usage(ACCT_POLICY_JOB_BEGIN, job_ptr);
}

/*
 * acct_policy_job_fini - Note that a job is completing for accounting
 *	policy purposes.
 */
extern void acct_policy_job_fini(struct job_record *job_ptr)
{
	_adjust_limit_usage(ACCT_POLICY_JOB_FINI, job_ptr);
}

extern bool acct_policy_validate(job_desc_msg_t *job_desc,
				 struct part_record *part_ptr,
				 slurmdb_association_rec_t *assoc_in,
				 slurmdb_qos_rec_t *qos_ptr,
				 uint16_t *limit_set_max_cpus,
				 uint16_t *limit_set_max_nodes,
				 uint16_t *limit_set_time)
{
	uint32_t time_limit;
	slurmdb_association_rec_t *assoc_ptr = assoc_in;
	int parent = 0;
	char *user_name = NULL;
	bool rc = true;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	xassert(limit_set_max_cpus);
	xassert(limit_set_max_nodes);
	xassert(limit_set_time);

	if (!assoc_ptr) {
		error("_validate_acct_policy: no assoc_ptr given for job.");
		return false;
	}

	user_name = assoc_ptr->user;

	assoc_mgr_lock(&locks);
	if(qos_ptr) {
		/* for validation we don't need to look at
		 * qos_ptr->grp_cpu_mins.
		 */
		if (((*limit_set_max_cpus) != ADMIN_SET_LIMIT)
		    && (qos_ptr->grp_cpus != INFINITE)) {
			if (job_desc->min_cpus > qos_ptr->grp_cpus) {
				info("job submit for user %s(%u): "
				     "min cpu request %u exceeds "
				     "group max cpu limit %u for qos '%s'",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_cpus,
				     qos_ptr->grp_cpus,
				     qos_ptr->name);
				rc = false;
				goto end_it;
			} else if ((job_desc->max_cpus == NO_VAL)
				   || ((*limit_set_max_cpus)
				       && (job_desc->max_cpus
					   > qos_ptr->grp_cpus))) {
				job_desc->max_cpus = qos_ptr->grp_cpus;
				(*limit_set_max_cpus) = 1;
			} else if (job_desc->max_cpus >
				   qos_ptr->grp_cpus) {
				info("job submit for user %s(%u): "
				     "max cpu changed %u -> %u because "
				     "of qos limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_cpus,
				     qos_ptr->grp_cpus);
				if(job_desc->max_cpus == NO_VAL)
					(*limit_set_max_cpus) = 1;
				job_desc->max_cpus = qos_ptr->grp_cpus;
			}
		}

		/* for validation we don't need to look at
		 * qos_ptr->grp_jobs.
		 */

		if (((*limit_set_max_nodes) != ADMIN_SET_LIMIT)
		    && (qos_ptr->grp_nodes != INFINITE)) {
			if (job_desc->min_nodes > qos_ptr->grp_nodes) {
				info("job submit for user %s(%u): "
				     "min node request %u exceeds "
				     "group max node limit %u for qos '%s'",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_nodes,
				     qos_ptr->grp_nodes,
				     qos_ptr->name);
				rc = false;
				goto end_it;
			} else if (job_desc->max_nodes == 0
				   || ((*limit_set_max_nodes)
				       && (job_desc->max_nodes
					   > qos_ptr->grp_nodes))) {
				job_desc->max_nodes = qos_ptr->grp_nodes;
				(*limit_set_max_nodes) = 1;
			} else if (job_desc->max_nodes >
				   qos_ptr->grp_nodes) {
				info("job submit for user %s(%u): "
				     "max node changed %u -> %u because "
				     "of qos limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_nodes,
				     qos_ptr->grp_nodes);
				if(job_desc->max_nodes == NO_VAL)
					(*limit_set_max_nodes) = 1;
				job_desc->max_nodes = qos_ptr->grp_nodes;
			}
		}

		if ((qos_ptr->grp_submit_jobs != INFINITE) &&
		    (qos_ptr->usage->grp_used_submit_jobs
		     >= qos_ptr->grp_submit_jobs)) {
			info("job submit for user %s(%u): "
			     "group max submit job limit exceeded %u "
			     "for qos '%s'",
			     user_name,
			     job_desc->user_id,
			     qos_ptr->grp_submit_jobs,
			     qos_ptr->name);
			rc = false;
			goto end_it;
		}


		/* for validation we don't need to look at
		 * qos_ptr->grp_wall. It is checked while the job is running.
		 */


		/* for validation we don't need to look at
		 * qos_ptr->max_cpu_mins_pj. It is checked while the
		 * job is running.
		 */

		if (((*limit_set_max_cpus) != ADMIN_SET_LIMIT)
		    && (qos_ptr->max_cpus_pj != INFINITE)) {
			if (job_desc->min_cpus > qos_ptr->max_cpus_pj) {
				info("job submit for user %s(%u): "
				     "min cpu limit %u exceeds "
				     "qos max %u",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_cpus,
				     qos_ptr->max_cpus_pj);
				rc = false;
				goto end_it;
			} else if (job_desc->max_cpus == NO_VAL
				   || ((*limit_set_max_cpus)
				       && (job_desc->max_cpus
					   > qos_ptr->max_cpus_pj))) {
				job_desc->max_cpus = qos_ptr->max_cpus_pj;
				(*limit_set_max_cpus) = 1;
			} else if (job_desc->max_cpus >
				   qos_ptr->max_cpus_pj) {
				info("job submit for user %s(%u): "
				     "max cpu changed %u -> %u because "
				     "of qos limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_cpus,
				     qos_ptr->max_cpus_pj);
				if(job_desc->max_cpus == NO_VAL)
					(*limit_set_max_cpus) = 1;
				job_desc->max_cpus = qos_ptr->max_cpus_pj;
			}
		}

		/* for validation we don't need to look at
		 * qos_ptr->max_jobs.
		 */

		if (((*limit_set_max_nodes) != ADMIN_SET_LIMIT)
		    && (qos_ptr->max_nodes_pj != INFINITE)) {
			if (job_desc->min_nodes > qos_ptr->max_nodes_pj) {
				info("job submit for user %s(%u): "
				     "min node limit %u exceeds "
				     "qos max %u",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_nodes,
				     qos_ptr->max_nodes_pj);
				rc = false;
				goto end_it;
			} else if (job_desc->max_nodes == 0
				   || ((*limit_set_max_nodes)
				       && (job_desc->max_nodes
					   > qos_ptr->max_nodes_pj))) {
				job_desc->max_nodes = qos_ptr->max_nodes_pj;
				(*limit_set_max_nodes) = 1;
			} else if (job_desc->max_nodes >
				   qos_ptr->max_nodes_pj) {
				info("job submit for user %s(%u): "
				     "max node changed %u -> %u because "
				     "of qos limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_nodes,
				     qos_ptr->max_nodes_pj);
				if(job_desc->max_nodes == NO_VAL)
					(*limit_set_max_nodes) = 1;
				job_desc->max_nodes = qos_ptr->max_nodes_pj;
			}
		}

		if (qos_ptr->max_submit_jobs_pu != INFINITE) {
			slurmdb_used_limits_t *used_limits = NULL;
			if(qos_ptr->usage->user_limit_list) {
				ListIterator itr = list_iterator_create(
					qos_ptr->usage->user_limit_list);
				while((used_limits = list_next(itr))) {
					if(used_limits->uid
					   == job_desc->user_id)
						break;
				}
				list_iterator_destroy(itr);
			}
			if(used_limits && (used_limits->submit_jobs
					   >= qos_ptr->max_submit_jobs_pu)) {
				info("job submit for user %s(%u): "
				     "qos max submit job limit exceeded %u",
				     user_name,
				     job_desc->user_id,
				     qos_ptr->max_submit_jobs_pu);
				rc = false;
				goto end_it;
			}
		}

		if (((*limit_set_time) != ADMIN_SET_LIMIT)
		    && (qos_ptr->max_wall_pj != INFINITE)) {
			time_limit = qos_ptr->max_wall_pj;
			if (job_desc->time_limit == NO_VAL) {
				if (part_ptr->max_time == INFINITE)
					job_desc->time_limit = time_limit;
				else
					job_desc->time_limit =
						MIN(time_limit,
						    part_ptr->max_time);
				(*limit_set_time) = 1;
			} else if ((*limit_set_time) &&
				   job_desc->time_limit > time_limit) {
				job_desc->time_limit = time_limit;
			} else if (job_desc->time_limit > time_limit) {
				info("job submit for user %s(%u): "
				     "time limit %u exceeds qos max %u",
				     user_name,
				     job_desc->user_id,
				     job_desc->time_limit, time_limit);
				rc = false;
				goto end_it;
			}
		}

	}

	while(assoc_ptr) {
		/* for validation we don't need to look at
		 * assoc_ptr->grp_cpu_mins.
		 */

		if (((*limit_set_max_cpus) != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->grp_cpus == INFINITE))
		    && (assoc_ptr->grp_cpus != INFINITE)) {
			if (job_desc->min_cpus > assoc_ptr->grp_cpus) {
				info("job submit for user %s(%u): "
				     "min cpu request %u exceeds "
				     "group max cpu limit %u for account %s",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_cpus,
				     assoc_ptr->grp_cpus,
				     assoc_ptr->acct);
				rc = false;
				break;
			} else if (job_desc->max_cpus == NO_VAL
				   || ((*limit_set_max_cpus)
				       && (job_desc->max_cpus
					   > assoc_ptr->grp_cpus))) {
				job_desc->max_cpus = assoc_ptr->grp_cpus;
				(*limit_set_max_cpus) = 1;
			} else if (job_desc->max_cpus > assoc_ptr->grp_cpus) {
				info("job submit for user %s(%u): "
				     "max cpu changed %u -> %u because "
				     "of account limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_cpus,
				     assoc_ptr->grp_cpus);
				if(job_desc->max_cpus == NO_VAL)
					(*limit_set_max_cpus) = 1;
				job_desc->max_cpus = assoc_ptr->grp_cpus;
			}
		}

		/* for validation we don't need to look at
		 * assoc_ptr->grp_jobs.
		 */

		if (((*limit_set_max_nodes) != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->grp_nodes == INFINITE))
		    && (assoc_ptr->grp_nodes != INFINITE)) {
			if (job_desc->min_nodes > assoc_ptr->grp_nodes) {
				info("job submit for user %s(%u): "
				     "min node request %u exceeds "
				     "group max node limit %u for account %s",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_nodes,
				     assoc_ptr->grp_nodes,
				     assoc_ptr->acct);
				rc = false;
				break;
			} else if (job_desc->max_nodes == 0
				   || ((*limit_set_max_nodes)
				       && (job_desc->max_nodes
					   > assoc_ptr->grp_nodes))) {
				job_desc->max_nodes = assoc_ptr->grp_nodes;
				(*limit_set_max_nodes) = 1;
			} else if (job_desc->max_nodes >
				   assoc_ptr->grp_nodes) {
				info("job submit for user %s(%u): "
				     "max node changed %u -> %u because "
				     "of account limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_nodes,
				     assoc_ptr->grp_nodes);
				if(job_desc->max_nodes == NO_VAL)
					(*limit_set_max_nodes) = 1;
				job_desc->max_nodes = assoc_ptr->grp_nodes;
			}
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->grp_submit_jobs == INFINITE)) &&
		    (assoc_ptr->grp_submit_jobs != INFINITE) &&
		    (assoc_ptr->usage->used_submit_jobs
		     >= assoc_ptr->grp_submit_jobs)) {
			info("job submit for user %s(%u): "
			     "group max submit job limit exceeded %u "
			     "for account '%s'",
			     user_name,
			     job_desc->user_id,
			     assoc_ptr->grp_submit_jobs,
			     assoc_ptr->acct);
			rc = false;
			break;
		}


		/* for validation we don't need to look at
		 * assoc_ptr->grp_wall. It is checked while the job is running.
		 */

		/* We don't need to look at the regular limits for
		 * parents since we have pre-propogated them, so just
		 * continue with the next parent
		 */
		if (parent) {
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
			continue;
		}

		/* for validation we don't need to look at
		 * assoc_ptr->max_cpu_mins_pj.
		 */

		if (((*limit_set_max_cpus) != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->max_cpus_pj == INFINITE))
		    && (assoc_ptr->max_cpus_pj != INFINITE)) {
			if (job_desc->min_cpus > assoc_ptr->max_cpus_pj) {
				info("job submit for user %s(%u): "
				     "min cpu limit %u exceeds "
				     "account max %u",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_cpus,
				     assoc_ptr->max_cpus_pj);
				rc = false;
				break;
			} else if (job_desc->max_cpus == NO_VAL
				   || ((*limit_set_max_cpus)
				       && (job_desc->max_cpus
					   > assoc_ptr->max_cpus_pj))) {
				job_desc->max_cpus = assoc_ptr->max_cpus_pj;
				(*limit_set_max_cpus) = 1;
			} else if (job_desc->max_cpus >
				   assoc_ptr->max_cpus_pj) {
				info("job submit for user %s(%u): "
				     "max cpu changed %u -> %u because "
				     "of account limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_cpus,
				     assoc_ptr->max_cpus_pj);
				if(job_desc->max_cpus == NO_VAL)
					(*limit_set_max_cpus) = 1;
				job_desc->max_cpus = assoc_ptr->max_cpus_pj;
			}
		}

		/* for validation we don't need to look at
		 * assoc_ptr->max_jobs.
		 */

		if (((*limit_set_max_nodes) != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->max_nodes_pj == INFINITE))
		    && (assoc_ptr->max_nodes_pj != INFINITE)) {
			if (job_desc->min_nodes > assoc_ptr->max_nodes_pj) {
				info("job submit for user %s(%u): "
				     "min node limit %u exceeds "
				     "account max %u",
				     user_name,
				     job_desc->user_id,
				     job_desc->min_nodes,
				     assoc_ptr->max_nodes_pj);
				rc = false;
				break;
			} else if (job_desc->max_nodes == 0
				   || ((*limit_set_max_nodes)
				       && (job_desc->max_nodes
					   > assoc_ptr->max_nodes_pj))) {
				job_desc->max_nodes = assoc_ptr->max_nodes_pj;
				(*limit_set_max_nodes) = 1;
			} else if (job_desc->max_nodes >
				   assoc_ptr->max_nodes_pj) {
				info("job submit for user %s(%u): "
				     "max node changed %u -> %u because "
				     "of account limit",
				     user_name,
				     job_desc->user_id,
				     job_desc->max_nodes,
				     assoc_ptr->max_nodes_pj);
				if(job_desc->max_nodes == NO_VAL)
					(*limit_set_max_nodes) = 1;
				job_desc->max_nodes = assoc_ptr->max_nodes_pj;
			}
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->max_submit_jobs_pu == INFINITE)) &&
		    (assoc_ptr->max_submit_jobs != INFINITE) &&
		    (assoc_ptr->usage->used_submit_jobs
		     >= assoc_ptr->max_submit_jobs)) {
			info("job submit for user %s(%u): "
			     "account max submit job limit exceeded %u",
			     user_name,
			     job_desc->user_id,
			     assoc_ptr->max_submit_jobs);
			rc = false;
			break;
		}

		if (((*limit_set_time) != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->max_wall_pj == INFINITE))
		    && (assoc_ptr->max_wall_pj != INFINITE)) {
			time_limit = assoc_ptr->max_wall_pj;
			if (job_desc->time_limit == NO_VAL) {
				if (part_ptr->max_time == INFINITE)
					job_desc->time_limit = time_limit;
				else
					job_desc->time_limit =
						MIN(time_limit,
						    part_ptr->max_time);
				(*limit_set_time) = 1;
			} else if ((*limit_set_time) &&
				   job_desc->time_limit > time_limit) {
				job_desc->time_limit = time_limit;
			} else if (job_desc->time_limit > time_limit) {
				info("job submit for user %s(%u): "
				     "time limit %u exceeds account max %u",
				     user_name,
				     job_desc->user_id,
				     job_desc->time_limit, time_limit);
				rc = false;
				break;
			}
		}

		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		parent = 1;
	}
end_it:
	assoc_mgr_unlock(&locks);

	return rc;
}

/*
 * acct_policy_job_runnable - Determine of the specified job can execute
 *	right now or not depending upon accounting policy (e.g. running
 *	job limit for this association). If the association limits prevent
 *	the job from ever running (lowered limits since job submission),
 *	then cancel the job.
 */
extern bool acct_policy_job_runnable(struct job_record *job_ptr)
{
	slurmdb_qos_rec_t *qos_ptr;
	slurmdb_association_rec_t *assoc_ptr;
	uint32_t time_limit;
	uint64_t cpu_time_limit;
	uint64_t job_cpu_time_limit;
	bool rc = true;
	uint64_t usage_mins;
	uint32_t wall_mins;
	bool cancel_job = 0;
	int parent = 0; /*flag to tell us if we are looking at the
			 * parent or not
			 */
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	/* check to see if we are enforcing associations */
	if (!accounting_enforce)
		return true;

	if (!_valid_job_assoc(job_ptr)) {
		_cancel_job(job_ptr);
		return false;
	}

	/* now see if we are enforcing limits */
	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return true;

	/* clear old state reason */
        if ((job_ptr->state_reason == WAIT_ASSOC_JOB_LIMIT) ||
	    (job_ptr->state_reason == WAIT_ASSOC_RESOURCE_LIMIT) ||
	    (job_ptr->state_reason == WAIT_ASSOC_TIME_LIMIT))
                job_ptr->state_reason = WAIT_NO_REASON;


	job_cpu_time_limit = (uint64_t)job_ptr->time_limit
		* (uint64_t)job_ptr->details->min_cpus;

	assoc_mgr_lock(&locks);
	qos_ptr = job_ptr->qos_ptr;
	if(qos_ptr) {
		usage_mins = (uint64_t)(qos_ptr->usage->usage_raw / 60.0);
		wall_mins = qos_ptr->usage->grp_used_wall / 60;

		if ((qos_ptr->grp_cpu_mins != (uint64_t)INFINITE)
		    && (usage_mins >= qos_ptr->grp_cpu_mins)) {
			job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
			xfree(job_ptr->state_desc);
			debug2("Job %u being held, "
			       "the job is at or exceeds QOS %s's "
			       "group max cpu minutes of %"PRIu64" "
			       "with %"PRIu64"",
			       job_ptr->job_id,
			       qos_ptr->name,
			       qos_ptr->grp_cpu_mins,
			       usage_mins);
			rc = false;
			goto end_it;
		}

		if ((job_ptr->limit_set_min_cpus != ADMIN_SET_LIMIT)
		    && qos_ptr->grp_cpus != INFINITE) {
			if (job_ptr->details->min_cpus > qos_ptr->grp_cpus) {
				info("job %u is being cancelled, "
				     "min cpu request %u exceeds "
				     "group max cpu limit %u for "
				     "qos '%s'",
				     job_ptr->job_id,
				     job_ptr->details->min_cpus,
				     qos_ptr->grp_cpus,
				     qos_ptr->name);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}

			if ((qos_ptr->usage->grp_used_cpus +
			     job_ptr->details->min_cpus) > qos_ptr->grp_cpus) {
				job_ptr->state_reason =
					WAIT_ASSOC_RESOURCE_LIMIT;
				xfree(job_ptr->state_desc);
				debug2("job %u being held, "
				       "the job is at or exceeds "
				       "group max cpu limit %u "
				       "with already used %u + requested %u "
				       "for qos %s",
				       job_ptr->job_id,
				       qos_ptr->grp_cpus,
				       qos_ptr->usage->grp_used_cpus,
				       job_ptr->details->min_cpus,
				       qos_ptr->name);
				rc = false;
				goto end_it;
			}
		}

		if ((qos_ptr->grp_jobs != INFINITE) &&
		    (qos_ptr->usage->grp_used_jobs >= qos_ptr->grp_jobs)) {
			job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
			xfree(job_ptr->state_desc);
			debug2("job %u being held, "
			       "the job is at or exceeds "
			       "group max jobs limit %u with %u for qos %s",
			       job_ptr->job_id,
			       qos_ptr->grp_jobs,
			       qos_ptr->usage->grp_used_jobs, qos_ptr->name);

			rc = false;
			goto end_it;
		}

		if ((job_ptr->limit_set_min_nodes != ADMIN_SET_LIMIT)
		    && qos_ptr->grp_nodes != INFINITE) {
			if (job_ptr->details->min_nodes > qos_ptr->grp_nodes) {
				info("job %u is being cancelled, "
				     "min node request %u exceeds "
				     "group max node limit %u for "
				     "qos '%s'",
				     job_ptr->job_id,
				     job_ptr->details->min_nodes,
				     qos_ptr->grp_nodes,
				     qos_ptr->name);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}

			if ((qos_ptr->usage->grp_used_nodes +
			     job_ptr->details->min_nodes) >
			    qos_ptr->grp_nodes) {
				job_ptr->state_reason =
					WAIT_ASSOC_RESOURCE_LIMIT;
				xfree(job_ptr->state_desc);
				debug2("job %u being held, "
				       "the job is at or exceeds "
				       "group max node limit %u "
				       "with already used %u + requested %u "
				       "for qos %s",
				       job_ptr->job_id,
				       qos_ptr->grp_nodes,
				       qos_ptr->usage->grp_used_nodes,
				       job_ptr->details->min_nodes,
				       qos_ptr->name);
				rc = false;
				goto end_it;
			}
		}

		/* we don't need to check submit_jobs here */

		if ((qos_ptr->grp_wall != INFINITE)
		    && (wall_mins >= qos_ptr->grp_wall)) {
			job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
			xfree(job_ptr->state_desc);
			debug2("job %u being held, "
			       "the job is at or exceeds "
			       "group wall limit %u "
			       "with %u for qos %s",
			       job_ptr->job_id,
			       qos_ptr->grp_wall,
			       wall_mins, qos_ptr->name);
			rc = false;
			goto end_it;
		}

		if (qos_ptr->max_cpu_mins_pj != INFINITE) {
			cpu_time_limit = qos_ptr->max_cpu_mins_pj;
			if ((job_ptr->time_limit != NO_VAL) &&
			    (job_cpu_time_limit > cpu_time_limit)) {
				info("job %u being cancelled, "
				     "cpu time limit %"PRIu64" exceeds "
				     "qos max per job %"PRIu64"",
				     job_ptr->job_id,
				     job_cpu_time_limit,
				     cpu_time_limit);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}

		if ((job_ptr->limit_set_min_cpus != ADMIN_SET_LIMIT)
		    && qos_ptr->max_cpus_pj != INFINITE) {
			if (job_ptr->details->min_cpus >
			    qos_ptr->max_cpus_pj) {
				info("job %u being cancelled, "
				     "min cpu limit %u exceeds "
				     "qos max %u",
				     job_ptr->job_id,
				     job_ptr->details->min_cpus,
				     qos_ptr->max_cpus_pj);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}

		if (qos_ptr->max_jobs_pu != INFINITE) {
			slurmdb_used_limits_t *used_limits = NULL;
			if(qos_ptr->usage->user_limit_list) {
				ListIterator itr = list_iterator_create(
					qos_ptr->usage->user_limit_list);
				while((used_limits = list_next(itr))) {
					if(used_limits->uid == job_ptr->user_id)
						break;
				}
				list_iterator_destroy(itr);
			}
			if(used_limits && (used_limits->jobs
					   >= qos_ptr->max_jobs_pu)) {
				debug2("job %u being held, "
				       "the job is at or exceeds "
				       "max jobs limit %u with %u for QOS %s",
				       job_ptr->job_id,
				       qos_ptr->max_jobs_pu,
				       used_limits->jobs, qos_ptr->name);
				rc = false;
				goto end_it;
			}
		}

		if ((job_ptr->limit_set_min_nodes != ADMIN_SET_LIMIT)
		    && qos_ptr->max_nodes_pj != INFINITE) {
			if (job_ptr->details->min_nodes >
			    qos_ptr->max_nodes_pj) {
				info("job %u being cancelled, "
				     "min node limit %u exceeds "
				     "qos max %u",
				     job_ptr->job_id,
				     job_ptr->details->min_nodes,
				     qos_ptr->max_nodes_pj);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}

		/* we don't need to check submit_jobs_pu here */

		/* if the qos limits have changed since job
		 * submission and job can not run, then kill it */
		if ((job_ptr->limit_set_time != ADMIN_SET_LIMIT)
		    && qos_ptr->max_wall_pj != INFINITE) {
			time_limit = qos_ptr->max_wall_pj;
			if ((job_ptr->time_limit != NO_VAL) &&
			    (job_ptr->time_limit > time_limit)) {
				info("job %u being cancelled, "
				     "time limit %u exceeds qos "
				     "max wall pj %u",
				     job_ptr->job_id,
				     job_ptr->time_limit,
				     time_limit);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}
	}

	assoc_ptr = job_ptr->assoc_ptr;
	while(assoc_ptr) {
		usage_mins = (uint64_t)(assoc_ptr->usage->usage_raw / 60.0);
		wall_mins = assoc_ptr->usage->grp_used_wall / 60;
#if _DEBUG
		info("acct_job_limits: %u of %u",
		     assoc_ptr->usage->used_jobs, assoc_ptr->max_jobs);
#endif
		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->grp_cpu_mins == (uint64_t)INFINITE))
		    && (assoc_ptr->grp_cpu_mins != (uint64_t)INFINITE)
		    && (usage_mins >= assoc_ptr->grp_cpu_mins)) {
			job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
			xfree(job_ptr->state_desc);
			debug2("job %u being held, "
			       "assoc %u is at or exceeds "
			       "group max cpu minutes limit %"PRIu64" "
			       "with %Lf for account %s",
			       job_ptr->job_id, assoc_ptr->id,
			       assoc_ptr->grp_cpu_mins,
			       assoc_ptr->usage->usage_raw, assoc_ptr->acct);

			rc = false;
			goto end_it;
		}

		if ((job_ptr->limit_set_min_cpus != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->grp_cpus == INFINITE))
		    && (assoc_ptr->grp_cpus != INFINITE)) {
			if (job_ptr->details->min_cpus > assoc_ptr->grp_cpus) {
				info("job %u being cancelled, "
				     "min cpu request %u exceeds "
				     "group max cpu limit %u for "
				     "account %s",
				     job_ptr->job_id,
				     job_ptr->details->min_cpus,
				     assoc_ptr->grp_cpus,
				     assoc_ptr->acct);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}

			if ((assoc_ptr->usage->grp_used_cpus +
				    job_ptr->details->min_cpus) >
				   assoc_ptr->grp_cpus) {
				job_ptr->state_reason =
					WAIT_ASSOC_RESOURCE_LIMIT;
				xfree(job_ptr->state_desc);
				debug2("job %u being held, "
				       "assoc %u is at or exceeds "
				       "group max cpu limit %u "
				       "with already used %u + requested %u "
				       "for account %s",
				       job_ptr->job_id, assoc_ptr->id,
				       assoc_ptr->grp_cpus,
				       assoc_ptr->usage->grp_used_cpus,
				       job_ptr->details->min_cpus,
				       assoc_ptr->acct);
				rc = false;
				goto end_it;
			}
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->grp_jobs == INFINITE)) &&
		    (assoc_ptr->grp_jobs != INFINITE) &&
		    (assoc_ptr->usage->used_jobs >= assoc_ptr->grp_jobs)) {
			job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
			xfree(job_ptr->state_desc);
			debug2("job %u being held, "
			       "assoc %u is at or exceeds "
			       "group max jobs limit %u with %u for account %s",
			       job_ptr->job_id, assoc_ptr->id,
			       assoc_ptr->grp_jobs,
			       assoc_ptr->usage->used_jobs, assoc_ptr->acct);

			rc = false;
			goto end_it;
		}

		if ((job_ptr->limit_set_min_nodes != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->grp_nodes == INFINITE))
		    && (assoc_ptr->grp_nodes != INFINITE)) {
			if (job_ptr->details->min_nodes >
			    assoc_ptr->grp_nodes) {
				info("job %u being cancelled, "
				     "min node request %u exceeds "
				     "group max node limit %u for "
				     "account %s",
				     job_ptr->job_id,
				     job_ptr->details->min_nodes,
				     assoc_ptr->grp_nodes,
				     assoc_ptr->acct);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}

			if ((assoc_ptr->usage->grp_used_nodes +
			     job_ptr->details->min_nodes) >
			    assoc_ptr->grp_nodes) {
				job_ptr->state_reason =
					WAIT_ASSOC_RESOURCE_LIMIT;
				xfree(job_ptr->state_desc);
				debug2("job %u being held, "
				       "assoc %u is at or exceeds "
				       "group max node limit %u "
				       "with already used %u + requested %u "
				       "for account %s",
				       job_ptr->job_id, assoc_ptr->id,
				       assoc_ptr->grp_nodes,
				       assoc_ptr->usage->grp_used_nodes,
				       job_ptr->details->min_nodes,
				       assoc_ptr->acct);
				rc = false;
				goto end_it;
			}
		}

		/* we don't need to check submit_jobs here */

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->grp_wall == INFINITE))
		    && (assoc_ptr->grp_wall != INFINITE)
		    && (wall_mins >= assoc_ptr->grp_wall)) {
			job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
			xfree(job_ptr->state_desc);
			debug2("job %u being held, "
			       "assoc %u is at or exceeds "
			       "group wall limit %u "
			       "with %u for account %s",
			       job_ptr->job_id, assoc_ptr->id,
			       assoc_ptr->grp_wall,
			       wall_mins, assoc_ptr->acct);
			rc = false;
			goto end_it;
		}


		/* We don't need to look at the regular limits for
		 * parents since we have pre-propogated them, so just
		 * continue with the next parent
		 */
		if(parent) {
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
			continue;
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->max_cpu_mins_pj == INFINITE)) &&
		    (assoc_ptr->max_cpu_mins_pj != INFINITE)) {
			cpu_time_limit = assoc_ptr->max_cpu_mins_pj;
			if ((job_ptr->time_limit != NO_VAL) &&
			    (job_cpu_time_limit > cpu_time_limit)) {
				info("job %u being cancelled, "
				     "cpu time limit %"PRIu64" exceeds "
				     "assoc max per job %"PRIu64"",
				     job_ptr->job_id,
				     job_cpu_time_limit,
				     cpu_time_limit);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->max_cpus_pj == INFINITE)) &&
		    (assoc_ptr->max_cpus_pj != INFINITE)) {
			if (job_ptr->details->min_cpus >
			    assoc_ptr->max_cpus_pj) {
				info("job %u being cancelled, "
				     "min cpu limit %u exceeds "
				     "account max %u",
				     job_ptr->job_id,
				     job_ptr->details->min_cpus,
				     assoc_ptr->max_cpus_pj);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->max_jobs_pu == INFINITE)) &&
		    (assoc_ptr->max_jobs != INFINITE) &&
		    (assoc_ptr->usage->used_jobs >= assoc_ptr->max_jobs)) {
			job_ptr->state_reason = WAIT_ASSOC_JOB_LIMIT;
			xfree(job_ptr->state_desc);
			debug2("job %u being held, "
			       "assoc %u is at or exceeds "
			       "max jobs limit %u with %u for account %s",
			       job_ptr->job_id, assoc_ptr->id,
			       assoc_ptr->max_jobs,
			       assoc_ptr->usage->used_jobs, assoc_ptr->acct);
			rc = false;
			goto end_it;
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->max_nodes_pj == INFINITE))
		    && (assoc_ptr->max_nodes_pj != INFINITE)) {
			if (job_ptr->details->min_nodes >
			    assoc_ptr->max_nodes_pj) {
				info("job %u being cancelled, "
				     "min node limit %u exceeds "
				     "account max %u",
				     job_ptr->job_id,
				     job_ptr->details->min_nodes,
				     assoc_ptr->max_nodes_pj);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}

		/* we don't need to check submit_jobs here */

		/* if the association limits have changed since job
		 * submission and job can not run, then kill it */
		if ((job_ptr->limit_set_time != ADMIN_SET_LIMIT)
		    && (!qos_ptr ||
			(qos_ptr && qos_ptr->max_wall_pj == INFINITE))
		    && (assoc_ptr->max_wall_pj != INFINITE)) {
			time_limit = assoc_ptr->max_wall_pj;
			if ((job_ptr->time_limit != NO_VAL) &&
			    (job_ptr->time_limit > time_limit)) {
				info("job %u being cancelled, "
				     "time limit %u exceeds account "
				     "max %u",
				     job_ptr->job_id,
				     job_ptr->time_limit,
				     time_limit);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}

		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		parent = 1;
	}
end_it:
	assoc_mgr_unlock(&locks);

	if(cancel_job)
		_cancel_job(job_ptr);

	return rc;
}

/*
 * acct_policy_update_pending_job - Make sure the limits imposed on a
 *	job on submission are correct after an update to a qos or
 *	association.  If the association/qos limits prevent
 *	the job from ever running (lowered limits since job submission),
 *	then cancel the job.
 */
extern int acct_policy_update_pending_job(struct job_record *job_ptr)
{
	job_desc_msg_t job_desc;
	uint16_t limit_set_max_cpus = 0;
	uint16_t limit_set_max_nodes = 0;
	uint16_t limit_set_time = 0;
	bool update_accounting = false;
	struct job_details *details_ptr;
	int rc = SLURM_SUCCESS;

	/* check to see if we are enforcing associations and the job
	 * is pending or if we are even enforcing limits. */
	if (!accounting_enforce || !IS_JOB_PENDING(job_ptr)
	    || !(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return SLURM_SUCCESS;

	details_ptr = job_ptr->details;

	if (!details_ptr) {
		error("acct_policy_update_pending_job: no details");
		return SLURM_ERROR;
	}

	/* set up the job desc to make sure things are the way we
	 * need.
	 */
	slurm_init_job_desc_msg(&job_desc);

	job_desc.min_cpus = details_ptr->min_cpus;
	/* Only set this value if not set from a limit */
	if (job_ptr->limit_set_max_cpus == ADMIN_SET_LIMIT)
		limit_set_max_cpus = job_ptr->limit_set_max_cpus;
	else if ((details_ptr->max_cpus != NO_VAL)
		 && !job_ptr->limit_set_max_cpus)
		job_desc.max_cpus = details_ptr->max_cpus;

	job_desc.min_nodes = details_ptr->min_nodes;
	/* Only set this value if not set from a limit */
	if (job_ptr->limit_set_max_nodes == ADMIN_SET_LIMIT)
		limit_set_max_nodes = job_ptr->limit_set_max_nodes;
	else if ((details_ptr->max_nodes != NO_VAL)
		 && !job_ptr->limit_set_max_nodes)
		job_desc.max_nodes = details_ptr->max_nodes;
	else
		job_desc.max_nodes = 0;

	/* Only set this value if not set from a limit */
	if (job_ptr->limit_set_time == ADMIN_SET_LIMIT)
		limit_set_time = job_ptr->limit_set_time;
	else if ((job_ptr->time_limit != NO_VAL) && !job_ptr->limit_set_time)
		job_desc.time_limit = job_ptr->time_limit;

	if (!acct_policy_validate(&job_desc, job_ptr->part_ptr,
				  job_ptr->assoc_ptr, job_ptr->qos_ptr,
				  &limit_set_max_cpus,
				  &limit_set_max_nodes,
				  &limit_set_time)) {
		info("acct_policy_update_pending_job: exceeded "
		     "association/qos's cpu, node or "
		     "time limit for job %d", job_ptr->job_id);
		_cancel_job(job_ptr);
		return SLURM_ERROR;
	}

	/* If it isn't an admin set limit replace it. */
	if (!limit_set_max_cpus && (job_ptr->limit_set_max_cpus == 1)) {
		details_ptr->max_cpus = NO_VAL;
		job_ptr->limit_set_max_cpus = 0;
		update_accounting = true;
	} else if (limit_set_max_cpus != ADMIN_SET_LIMIT) {
		if (details_ptr->max_cpus != job_desc.max_cpus) {
			details_ptr->max_cpus = job_desc.max_cpus;
			update_accounting = true;
		}
		job_ptr->limit_set_max_cpus = limit_set_max_cpus;
	}

	if (!limit_set_max_nodes && (job_ptr->limit_set_max_nodes == 1)) {
		details_ptr->max_nodes = 0;
		job_ptr->limit_set_max_nodes = 0;
		update_accounting = true;
	} else if (limit_set_max_nodes != ADMIN_SET_LIMIT) {
		if (details_ptr->max_nodes != job_desc.max_nodes) {
			details_ptr->max_nodes = job_desc.max_nodes;
			update_accounting = true;
		}
		job_ptr->limit_set_max_nodes = limit_set_max_nodes;
	}

	if (!limit_set_time && (job_ptr->limit_set_time == 1)) {
		job_ptr->time_limit = NO_VAL;
		job_ptr->limit_set_time = 0;
		update_accounting = true;
	} else if (limit_set_time != ADMIN_SET_LIMIT) {
		if (job_ptr->time_limit != job_desc.time_limit) {
			job_ptr->time_limit = job_desc.time_limit;
			update_accounting = true;
		}
		job_ptr->limit_set_time = limit_set_time;
	}

	if (update_accounting) {
		last_job_update = time(NULL);
		debug("limits changed for job %u: updating accounting",
		      job_ptr->job_id);
		if (details_ptr->begin_time) {
			/* Update job record in accounting to reflect changes */
			jobacct_storage_g_job_start(acct_db_conn, job_ptr);
		}
	}

	return rc;
}

extern bool acct_policy_node_usable(struct job_record *job_ptr,
				    uint32_t used_cpus,
				    char *node_name, uint32_t node_cpus)
{
	slurmdb_qos_rec_t *qos_ptr;
	slurmdb_association_rec_t *assoc_ptr;
	bool rc = true;
	uint32_t total_cpus = used_cpus + node_cpus;
	bool cancel_job = 0;
	int parent = 0; /* flag to tell us if we are looking at the
			 * parent or not
			 */
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	/* check to see if we are enforcing associations */
	if (!accounting_enforce)
		return true;

	if (!_valid_job_assoc(job_ptr)) {
		_cancel_job(job_ptr);
		return false;
	}

	/* now see if we are enforcing limits */
	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return true;

	/* clear old state reason */
        if ((job_ptr->state_reason == WAIT_ASSOC_JOB_LIMIT) ||
	    (job_ptr->state_reason == WAIT_ASSOC_RESOURCE_LIMIT) ||
	    (job_ptr->state_reason == WAIT_ASSOC_TIME_LIMIT))
                job_ptr->state_reason = WAIT_NO_REASON;


	assoc_mgr_lock(&locks);
	qos_ptr = job_ptr->qos_ptr;
	if(qos_ptr) {
		if (qos_ptr->grp_cpus != INFINITE) {
			if ((total_cpus+qos_ptr->usage->grp_used_cpus)
			    > qos_ptr->grp_cpus) {
				debug("Can't use %s, adding it's %u cpus "
				      "exceeds "
				      "group max cpu limit %u for qos '%s'",
				     node_name,
				     node_cpus,
				     qos_ptr->grp_cpus,
				     qos_ptr->name);
				rc = false;
				goto end_it;
			}
		}

		if (qos_ptr->max_cpus_pj != INFINITE) {
			if (total_cpus > qos_ptr->max_cpus_pj) {
				debug("Can't use %s, adding it's %u cpus "
				      "exceeds "
				      "max cpu limit %u for qos '%s'",
				      node_name,
				      node_cpus,
				      qos_ptr->max_cpus_pj,
				      qos_ptr->name);
				cancel_job = 1;
				rc = false;
				goto end_it;
			}
		}
	}

	assoc_ptr = job_ptr->assoc_ptr;
	while(assoc_ptr) {
		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->grp_cpus == INFINITE))
		    && (assoc_ptr->grp_cpus != INFINITE)) {
			if ((total_cpus+assoc_ptr->usage->grp_used_cpus)
			    > assoc_ptr->grp_cpus) {
				debug("Can't use %s, adding it's %u cpus "
				      "exceeds "
				      "group max cpu limit %u for account '%s'",
				      node_name,
				      node_cpus,
				      assoc_ptr->grp_cpus,
				      assoc_ptr->acct);
				rc = false;
				goto end_it;
			}
		}

		/* We don't need to look at the regular limits for
		 * parents since we have pre-propogated them, so just
		 * continue with the next parent
		 */
		if(parent) {
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
			continue;
		}

		if ((!qos_ptr ||
		     (qos_ptr && qos_ptr->max_cpus_pj == INFINITE)) &&
		    (assoc_ptr->max_cpus_pj != INFINITE)) {
			if (job_ptr->details->min_cpus >
			    assoc_ptr->max_cpus_pj) {
				debug("Can't use %s, adding it's %u cpus "
				      "exceeds "
				      "max cpu limit %u for account '%s'",
				      node_name,
				      node_cpus,
				      assoc_ptr->max_cpus_pj,
				      assoc_ptr->acct);
				rc = false;
				goto end_it;
			}
		}
		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		parent = 1;
	}
end_it:
	assoc_mgr_unlock(&locks);

	if(cancel_job)
		_cancel_job(job_ptr);

	return rc;
}
