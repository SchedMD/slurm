/*****************************************************************************\
 *  acct_policy.c - Enforce accounting policy
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/slurm_accounting_storage.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/acct_policy.h"
#include "src/common/node_select.h"
#include "src/common/slurm_priority.h"

#define _DEBUG 0

enum {
	ACCT_POLICY_ADD_SUBMIT,
	ACCT_POLICY_REM_SUBMIT,
	ACCT_POLICY_JOB_BEGIN,
	ACCT_POLICY_JOB_FINI
};

static int _get_tres_state_reason(int tres_pos, int unk_reason)
{
	switch (tres_pos) {
	case TRES_ARRAY_CPU:
		switch (unk_reason) {
		case WAIT_ASSOC_GRP_UNK:
			return WAIT_ASSOC_GRP_CPU;
		case WAIT_ASSOC_GRP_UNK_MIN:
			return WAIT_ASSOC_GRP_CPU_MIN;
		case WAIT_ASSOC_GRP_UNK_RUN_MIN:
			return WAIT_ASSOC_GRP_CPU_RUN_MIN;
		case WAIT_ASSOC_MAX_UNK_PER_JOB:
			return WAIT_ASSOC_MAX_CPU_PER_JOB;
		case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
			return WAIT_ASSOC_MAX_CPU_MINS_PER_JOB;
		case WAIT_QOS_GRP_UNK:
			return WAIT_QOS_GRP_CPU;
		case WAIT_QOS_GRP_UNK_MIN:
			return WAIT_QOS_GRP_CPU_MIN;
		case WAIT_QOS_GRP_UNK_RUN_MIN:
			return WAIT_QOS_GRP_CPU_RUN_MIN;
		case WAIT_QOS_MAX_UNK_PER_JOB:
			return WAIT_QOS_MAX_CPU_PER_JOB;
		case WAIT_QOS_MAX_UNK_PER_NODE:
			return WAIT_QOS_MAX_CPU_PER_NODE;
		case WAIT_QOS_MAX_UNK_PER_ACCT:
			return WAIT_QOS_MAX_CPU_PER_ACCT;
		case WAIT_QOS_MAX_UNK_PER_USER:
			return WAIT_QOS_MAX_CPU_PER_USER;
		case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
			return WAIT_QOS_MAX_CPU_MINS_PER_JOB;
		case WAIT_QOS_MIN_UNK:
			return WAIT_QOS_MIN_CPU;
		default:
			return unk_reason;
			break;
		}
		break;
	case TRES_ARRAY_MEM:
		switch (unk_reason) {
		case WAIT_ASSOC_GRP_UNK:
			return WAIT_ASSOC_GRP_MEM;
		case WAIT_ASSOC_GRP_UNK_MIN:
			return WAIT_ASSOC_GRP_MEM_MIN;
		case WAIT_ASSOC_GRP_UNK_RUN_MIN:
			return WAIT_ASSOC_GRP_MEM_RUN_MIN;
		case WAIT_ASSOC_MAX_UNK_PER_JOB:
			return WAIT_ASSOC_MAX_MEM_PER_JOB;
		case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
			return WAIT_ASSOC_MAX_MEM_MINS_PER_JOB;
		case WAIT_QOS_GRP_UNK:
			return WAIT_QOS_GRP_MEM;
		case WAIT_QOS_GRP_UNK_MIN:
			return WAIT_QOS_GRP_MEM_MIN;
		case WAIT_QOS_GRP_UNK_RUN_MIN:
			return WAIT_QOS_GRP_MEM_RUN_MIN;
		case WAIT_QOS_MAX_UNK_PER_JOB:
			return WAIT_QOS_MAX_MEM_PER_JOB;
		case WAIT_QOS_MAX_UNK_PER_NODE:
			return WAIT_QOS_MAX_MEM_PER_NODE;
		case WAIT_QOS_MAX_UNK_PER_ACCT:
			return WAIT_QOS_MAX_MEM_PER_ACCT;
		case WAIT_QOS_MAX_UNK_PER_USER:
			return WAIT_QOS_MAX_MEM_PER_USER;
		case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
			return WAIT_QOS_MAX_MEM_MINS_PER_JOB;
		case WAIT_QOS_MIN_UNK:
			return WAIT_QOS_MIN_MEM;
		default:
			return unk_reason;
			break;
		}
		break;
	case TRES_ARRAY_ENEGRY:
		switch (unk_reason) {
		case WAIT_ASSOC_GRP_UNK:
			return WAIT_ASSOC_GRP_ENERGY;
		case WAIT_ASSOC_GRP_UNK_MIN:
			return WAIT_ASSOC_GRP_ENERGY_MIN;
		case WAIT_ASSOC_GRP_UNK_RUN_MIN:
			return WAIT_ASSOC_GRP_ENERGY_RUN_MIN;
		case WAIT_ASSOC_MAX_UNK_PER_JOB:
			return WAIT_ASSOC_MAX_ENERGY_PER_JOB;
		case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
			return WAIT_ASSOC_MAX_ENERGY_MINS_PER_JOB;
		case WAIT_QOS_GRP_UNK:
			return WAIT_QOS_GRP_ENERGY;
		case WAIT_QOS_GRP_UNK_MIN:
			return WAIT_QOS_GRP_ENERGY_MIN;
		case WAIT_QOS_GRP_UNK_RUN_MIN:
			return WAIT_QOS_GRP_ENERGY_RUN_MIN;
		case WAIT_QOS_MAX_UNK_PER_JOB:
			return WAIT_QOS_MAX_ENERGY_PER_JOB;
		case WAIT_QOS_MAX_UNK_PER_NODE:
			return WAIT_QOS_MAX_ENERGY_PER_NODE;
		case WAIT_QOS_MAX_UNK_PER_ACCT:
			return WAIT_QOS_MAX_ENERGY_PER_ACCT;
		case WAIT_QOS_MAX_UNK_PER_USER:
			return WAIT_QOS_MAX_ENERGY_PER_USER;
		case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
			return WAIT_QOS_MAX_ENERGY_MINS_PER_JOB;
		case WAIT_QOS_MIN_UNK:
			return WAIT_QOS_MIN_ENERGY;
		default:
			return unk_reason;
			break;
		}
		break;
	case TRES_ARRAY_NODE:
		switch (unk_reason) {
		case WAIT_ASSOC_GRP_UNK:
			return WAIT_ASSOC_GRP_NODE;
		case WAIT_ASSOC_GRP_UNK_MIN:
			return WAIT_ASSOC_GRP_NODE_MIN;
		case WAIT_ASSOC_GRP_UNK_RUN_MIN:
			return WAIT_ASSOC_GRP_NODE_RUN_MIN;
		case WAIT_ASSOC_MAX_UNK_PER_JOB:
			return WAIT_ASSOC_MAX_NODE_PER_JOB;
		case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
			return WAIT_ASSOC_MAX_NODE_MINS_PER_JOB;
		case WAIT_QOS_GRP_UNK:
			return WAIT_QOS_GRP_NODE;
		case WAIT_QOS_GRP_UNK_MIN:
			return WAIT_QOS_GRP_NODE_MIN;
		case WAIT_QOS_GRP_UNK_RUN_MIN:
			return WAIT_QOS_GRP_NODE_RUN_MIN;
		case WAIT_QOS_MAX_UNK_PER_JOB:
			return WAIT_QOS_MAX_NODE_PER_JOB;
		case WAIT_QOS_MAX_UNK_PER_ACCT:
			return WAIT_QOS_MAX_NODE_PER_ACCT;
		case WAIT_QOS_MAX_UNK_PER_USER:
			return WAIT_QOS_MAX_NODE_PER_USER;
		case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
			return WAIT_QOS_MAX_NODE_MINS_PER_JOB;
		case WAIT_QOS_MIN_UNK:
			return WAIT_QOS_MIN_NODE;
		default:
			return unk_reason;
			break;
		}
		break;
	default:
		if (!xstrcmp("gres", assoc_mgr_tres_array[tres_pos]->type))
			switch (unk_reason) {
			case WAIT_ASSOC_GRP_UNK:
				return WAIT_ASSOC_GRP_GRES;
			case WAIT_ASSOC_GRP_UNK_MIN:
				return WAIT_ASSOC_GRP_GRES_MIN;
			case WAIT_ASSOC_GRP_UNK_RUN_MIN:
				return WAIT_ASSOC_GRP_GRES_RUN_MIN;
			case WAIT_ASSOC_MAX_UNK_PER_JOB:
				return WAIT_ASSOC_MAX_GRES_PER_JOB;
			case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
				return WAIT_ASSOC_MAX_GRES_MINS_PER_JOB;
			case WAIT_QOS_GRP_UNK:
				return WAIT_QOS_GRP_GRES;
			case WAIT_QOS_GRP_UNK_MIN:
				return WAIT_QOS_GRP_GRES_MIN;
			case WAIT_QOS_GRP_UNK_RUN_MIN:
				return WAIT_QOS_GRP_GRES_RUN_MIN;
			case WAIT_QOS_MAX_UNK_PER_JOB:
				return WAIT_QOS_MAX_GRES_PER_JOB;
			case WAIT_QOS_MAX_UNK_PER_NODE:
				return WAIT_QOS_MAX_GRES_PER_NODE;
			case WAIT_QOS_MAX_UNK_PER_ACCT:
				return WAIT_QOS_MAX_GRES_PER_ACCT;
			case WAIT_QOS_MAX_UNK_PER_USER:
				return WAIT_QOS_MAX_GRES_PER_USER;
			case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
				return WAIT_QOS_MAX_GRES_MINS_PER_JOB;
			case WAIT_QOS_MIN_UNK:
				return WAIT_QOS_MIN_GRES;
			default:
				return unk_reason;
				break;
			}
		else if (!xstrcmp("license",
				  assoc_mgr_tres_array[tres_pos]->type))
			switch (unk_reason) {
			case WAIT_ASSOC_GRP_UNK:
				return WAIT_ASSOC_GRP_LIC;
			case WAIT_ASSOC_GRP_UNK_MIN:
				return WAIT_ASSOC_GRP_LIC_MIN;
			case WAIT_ASSOC_GRP_UNK_RUN_MIN:
				return WAIT_ASSOC_GRP_LIC_RUN_MIN;
			case WAIT_ASSOC_MAX_UNK_PER_JOB:
				return WAIT_ASSOC_MAX_LIC_PER_JOB;
			case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
				return WAIT_ASSOC_MAX_LIC_MINS_PER_JOB;
			case WAIT_QOS_GRP_UNK:
				return WAIT_QOS_GRP_LIC;
			case WAIT_QOS_GRP_UNK_MIN:
				return WAIT_QOS_GRP_LIC_MIN;
			case WAIT_QOS_GRP_UNK_RUN_MIN:
				return WAIT_QOS_GRP_LIC_RUN_MIN;
			case WAIT_QOS_MAX_UNK_PER_JOB:
				return WAIT_QOS_MAX_LIC_PER_JOB;
			case WAIT_QOS_MAX_UNK_PER_ACCT:
				return WAIT_QOS_MAX_LIC_PER_ACCT;
			case WAIT_QOS_MAX_UNK_PER_USER:
				return WAIT_QOS_MAX_LIC_PER_USER;
			case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
				return WAIT_QOS_MAX_LIC_MINS_PER_JOB;
			case WAIT_QOS_MIN_UNK:
				return WAIT_QOS_MIN_LIC;
			default:
				return unk_reason;
				break;
			}
		else if (!xstrcmp("bb", assoc_mgr_tres_array[tres_pos]->type))
			switch (unk_reason) {
			case WAIT_ASSOC_GRP_UNK:
				return WAIT_ASSOC_GRP_BB;
			case WAIT_ASSOC_GRP_UNK_MIN:
				return WAIT_ASSOC_GRP_BB_MIN;
			case WAIT_ASSOC_GRP_UNK_RUN_MIN:
				return WAIT_ASSOC_GRP_BB_RUN_MIN;
			case WAIT_ASSOC_MAX_UNK_PER_JOB:
				return WAIT_ASSOC_MAX_BB_PER_JOB;
			case WAIT_ASSOC_MAX_UNK_MINS_PER_JOB:
				return WAIT_ASSOC_MAX_BB_MINS_PER_JOB;
			case WAIT_QOS_GRP_UNK:
				return WAIT_QOS_GRP_BB;
			case WAIT_QOS_GRP_UNK_MIN:
				return WAIT_QOS_GRP_BB_MIN;
			case WAIT_QOS_GRP_UNK_RUN_MIN:
				return WAIT_QOS_GRP_BB_RUN_MIN;
			case WAIT_QOS_MAX_UNK_PER_JOB:
				return WAIT_QOS_MAX_BB_PER_JOB;
			case WAIT_QOS_MAX_UNK_PER_NODE:
				return WAIT_QOS_MAX_BB_PER_NODE;
			case WAIT_QOS_MAX_UNK_PER_ACCT:
				return WAIT_QOS_MAX_BB_PER_ACCT;
			case WAIT_QOS_MAX_UNK_PER_USER:
				return WAIT_QOS_MAX_BB_PER_USER;
			case WAIT_QOS_MAX_UNK_MINS_PER_JOB:
				return WAIT_QOS_MAX_BB_MINS_PER_JOB;
			case WAIT_QOS_MIN_UNK:
				return WAIT_QOS_MIN_BB;
			default:
				return unk_reason;
				break;
			}
		break;
	}

	return unk_reason;
}

static void _set_qos_order(struct job_record *job_ptr,
			   slurmdb_qos_rec_t **qos_ptr_1,
			   slurmdb_qos_rec_t **qos_ptr_2)
{
	xassert(job_ptr);
	xassert(qos_ptr_1);
	xassert(qos_ptr_2);

	/* Initialize incoming pointers */
	*qos_ptr_1 = NULL;
	*qos_ptr_2 = NULL;

	if (job_ptr->qos_ptr) {
		if (job_ptr->part_ptr && job_ptr->part_ptr->qos_ptr) {
			/* If the job's QOS has the flag to over ride the
			 * partition then use that otherwise use the
			 * partition's QOS as the king.
			 */
			if (((slurmdb_qos_rec_t *)job_ptr->qos_ptr)->flags
			    & QOS_FLAG_OVER_PART_QOS) {
				*qos_ptr_1 = job_ptr->qos_ptr;
				*qos_ptr_2 = job_ptr->part_ptr->qos_ptr;
			} else {
				*qos_ptr_1 = job_ptr->part_ptr->qos_ptr;
				*qos_ptr_2 = job_ptr->qos_ptr;
			}

			/* No reason to look at the same QOS twice, actually
			 * we never want to do that ;). */
			if (*qos_ptr_1 == *qos_ptr_2)
				*qos_ptr_2 = NULL;
		} else
			*qos_ptr_1 = job_ptr->qos_ptr;
	} else if (job_ptr->part_ptr && job_ptr->part_ptr->qos_ptr)
		*qos_ptr_1 = job_ptr->part_ptr->qos_ptr;

	return;
}

static int _find_used_limits_for_acct(void *x, void *key)
{
	slurmdb_used_limits_t *used_limits = (slurmdb_used_limits_t *)x;
	char *account = (char *)key;

	if (!xstrcmp(account, used_limits->acct))
		return 1;

	return 0;
}

/* Checks for record in *user_limit_list of user_id if
 * *user_limit_list doesn't exist it will create it, if the user_id
 * record doesn't exist it will add it to the list.
 * In all cases the user record is returned.
 */
static slurmdb_used_limits_t *_get_acct_used_limits(
	List *acct_limit_list, char *acct)
{
	slurmdb_used_limits_t *used_limits;

	xassert(acct_limit_list);

	if (!*acct_limit_list)
		*acct_limit_list = list_create(slurmdb_destroy_used_limits);

	if (!(used_limits = list_find_first(*acct_limit_list,
					    _find_used_limits_for_acct,
					    acct))) {
		int i = sizeof(uint64_t) * slurmctld_tres_cnt;

		used_limits = xmalloc(sizeof(slurmdb_used_limits_t));
		used_limits->acct = xstrdup(acct);

		used_limits->tres = xmalloc(i);
		used_limits->tres_run_mins = xmalloc(i);

		list_append(*acct_limit_list, used_limits);
	}

	return used_limits;
}

static int _find_used_limits_for_user(void *x, void *key)
{
	slurmdb_used_limits_t *used_limits = (slurmdb_used_limits_t *)x;
	uint32_t user_id = *(uint32_t *)key;

	if (used_limits->uid == user_id)
		return 1;

	return 0;
}

/* Checks for record in *user_limit_list of user_id if
 * *user_limit_list doesn't exist it will create it, if the user_id
 * record doesn't exist it will add it to the list.
 * In all cases the user record is returned.
 */
static slurmdb_used_limits_t *_get_user_used_limits(
	List *user_limit_list, uint32_t user_id)
{
	slurmdb_used_limits_t *used_limits;

	xassert(user_limit_list);

	if (!*user_limit_list)
		*user_limit_list = list_create(slurmdb_destroy_used_limits);

	if (!(used_limits = list_find_first(*user_limit_list,
					    _find_used_limits_for_user,
					    &user_id))) {
		int i = sizeof(uint64_t) * slurmctld_tres_cnt;

		used_limits = xmalloc(sizeof(slurmdb_used_limits_t));
		used_limits->uid = user_id;

		used_limits->tres = xmalloc(i);
		used_limits->tres_run_mins = xmalloc(i);

		list_append(*user_limit_list, used_limits);
	}

	return used_limits;
}

static bool _valid_job_assoc(struct job_record *job_ptr)
{
	slurmdb_assoc_rec_t assoc_rec, *assoc_ptr;

	assoc_ptr = (slurmdb_assoc_rec_t *)job_ptr->assoc_ptr;
	if ((assoc_ptr == NULL) ||
	    (assoc_ptr->id  != job_ptr->assoc_id) ||
	    (assoc_ptr->uid != job_ptr->user_id)) {
		error("Invalid assoc_ptr for jobid=%u", job_ptr->job_id);
		memset(&assoc_rec, 0, sizeof(slurmdb_assoc_rec_t));

		assoc_rec.acct      = job_ptr->account;
		if (job_ptr->part_ptr)
			assoc_rec.partition = job_ptr->part_ptr->name;
		assoc_rec.uid       = job_ptr->user_id;

		if (assoc_mgr_fill_in_assoc(acct_db_conn, &assoc_rec,
					    accounting_enforce,
					    (slurmdb_assoc_rec_t **)
					    &job_ptr->assoc_ptr, false)) {
			info("_validate_job_assoc: invalid account or "
			     "partition for uid=%u jobid=%u",
			     job_ptr->user_id, job_ptr->job_id);
			return false;
		}
		job_ptr->assoc_id = assoc_rec.id;
	}
	return true;
}

static void _qos_adjust_limit_usage(int type, struct job_record *job_ptr,
				    slurmdb_qos_rec_t *qos_ptr,
				    uint64_t *used_tres_run_secs,
				    uint32_t job_cnt)
{
	slurmdb_used_limits_t *used_limits = NULL, *used_limits_a = NULL;
	slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;
	int i;

	if (!qos_ptr || !assoc_ptr)
		return;

	used_limits_a =	_get_acct_used_limits(&qos_ptr->usage->acct_limit_list,
					      assoc_ptr->acct);

	used_limits = _get_user_used_limits(&qos_ptr->usage->user_limit_list,
					    job_ptr->user_id);

	switch(type) {
	case ACCT_POLICY_ADD_SUBMIT:
		qos_ptr->usage->grp_used_submit_jobs += job_cnt;
		used_limits->submit_jobs += job_cnt;
		used_limits_a->submit_jobs += job_cnt;
		break;
	case ACCT_POLICY_REM_SUBMIT:
		if (qos_ptr->usage->grp_used_submit_jobs >= job_cnt)
			qos_ptr->usage->grp_used_submit_jobs -= job_cnt;
		else {
			qos_ptr->usage->grp_used_submit_jobs = 0;
			debug2("acct_policy_remove_job_submit: "
			       "grp_submit_jobs underflow for qos %s",
			       qos_ptr->name);
		}

		if (used_limits->submit_jobs >= job_cnt)
			used_limits->submit_jobs -= job_cnt;
		else {
			used_limits->submit_jobs = 0;
			debug2("acct_policy_remove_job_submit: "
			       "used_submit_jobs underflow for "
			       "qos %s user %d",
			       qos_ptr->name, used_limits->uid);
		}

		if (used_limits_a->submit_jobs >= job_cnt)
			used_limits_a->submit_jobs -= job_cnt;
		else {
			used_limits_a->submit_jobs = 0;
			debug2("acct_policy_remove_job_submit: "
			       "used_submit_jobs underflow for "
			       "qos %s account %s",
			       qos_ptr->name, used_limits_a->acct);
		}

		break;
	case ACCT_POLICY_JOB_BEGIN:
		qos_ptr->usage->grp_used_jobs++;
		for (i=0; i<slurmctld_tres_cnt; i++) {
			used_limits->tres[i] += job_ptr->tres_alloc_cnt[i];
			used_limits_a->tres[i] += job_ptr->tres_alloc_cnt[i];

			qos_ptr->usage->grp_used_tres[i] +=
				job_ptr->tres_alloc_cnt[i];
			qos_ptr->usage->grp_used_tres_run_secs[i] +=
				used_tres_run_secs[i];
			debug2("acct_policy_job_begin: after "
			       "adding job %u, qos %s "
			       "grp_used_tres_run_secs(%s) "
			       "is %"PRIu64,
			       job_ptr->job_id,
			       qos_ptr->name,
			       assoc_mgr_tres_name_array[i],
			       qos_ptr->usage->grp_used_tres_run_secs[i]);
		}

		used_limits->jobs++;
		used_limits_a->jobs++;
		break;
	case ACCT_POLICY_JOB_FINI:
		/*
		 * If tres_alloc_cnt doesn't exist means ACCT_POLICY_JOB_BEGIN
		 * was never called so no need to clean up that which was never
		 * set up.
		 */
		if (!job_ptr->tres_alloc_cnt)
			break;
		qos_ptr->usage->grp_used_jobs--;
		if ((int32_t)qos_ptr->usage->grp_used_jobs < 0) {
			qos_ptr->usage->grp_used_jobs = 0;
			debug2("acct_policy_job_fini: used_jobs "
			       "underflow for qos %s", qos_ptr->name);
		}

		for (i=0; i<slurmctld_tres_cnt; i++) {
			if (job_ptr->tres_alloc_cnt[i] >
			    qos_ptr->usage->grp_used_tres[i]) {
				qos_ptr->usage->grp_used_tres[i] = 0;
				debug2("acct_policy_job_fini: "
				       "grp_used_tres(%s) "
				       "underflow for QOS %s",
				       assoc_mgr_tres_name_array[i],
				       qos_ptr->name);
			} else
				qos_ptr->usage->grp_used_tres[i] -=
					job_ptr->tres_alloc_cnt[i];

			if (job_ptr->tres_alloc_cnt[i] > used_limits->tres[i]) {
				used_limits->tres[i] = 0;
				debug2("acct_policy_job_fini: "
				       "used_limits->tres(%s) "
				       "underflow for qos %s user %u",
				       assoc_mgr_tres_name_array[i],
				       qos_ptr->name, used_limits->uid);
			} else
				used_limits->tres[i] -=
					job_ptr->tres_alloc_cnt[i];

			if (job_ptr->tres_alloc_cnt[i] >
			    used_limits_a->tres[i]) {
				used_limits_a->tres[i] = 0;
				debug2("acct_policy_job_fini: "
				       "used_limits->tres(%s) "
				       "underflow for qos %s account %s",
				       assoc_mgr_tres_name_array[i],
				       qos_ptr->name, used_limits_a->acct);
			} else
				used_limits_a->tres[i] -=
					job_ptr->tres_alloc_cnt[i];
		}

		if (used_limits->jobs)
			used_limits->jobs--;
		else
			debug2("acct_policy_job_fini: used_jobs "
			       "underflow for qos %s user %d",
			       qos_ptr->name, used_limits->uid);

		if (used_limits_a->jobs)
			used_limits_a->jobs--;
		else
			debug2("acct_policy_job_fini: used_jobs "
			       "underflow for qos %s account %s",
			       qos_ptr->name, used_limits_a->acct);

		break;
	default:
		error("acct_policy: qos unknown type %d", type);
		break;
	}

}

static int _find_qos_part(void *x, void *key)
{
	if ((slurmdb_qos_rec_t *) x == (slurmdb_qos_rec_t *) key)
		return 1;	/* match */

	return 0;
}

static void _adjust_limit_usage(int type, struct job_record *job_ptr)
{
	slurmdb_assoc_rec_t *assoc_ptr = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
	uint64_t used_tres_run_secs[slurmctld_tres_cnt];
	int i;
	uint32_t job_cnt = 1;

	memset(used_tres_run_secs, 0, sizeof(uint64_t) * slurmctld_tres_cnt);

	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)
	    || !_valid_job_assoc(job_ptr))
		return;

	if (type == ACCT_POLICY_JOB_FINI)
		priority_g_job_end(job_ptr);
	else if (type == ACCT_POLICY_JOB_BEGIN) {
		uint64_t time_limit_secs = (uint64_t)job_ptr->time_limit * 60;
		for (i=0; i<slurmctld_tres_cnt; i++)
			used_tres_run_secs[i] =
				job_ptr->tres_alloc_cnt[i] * time_limit_secs;
	} else if (((type == ACCT_POLICY_ADD_SUBMIT) ||
		    (type == ACCT_POLICY_REM_SUBMIT)) &&
		   job_ptr->array_recs && job_ptr->array_recs->task_cnt)
		job_cnt = job_ptr->array_recs->task_cnt;

	assoc_mgr_lock(&locks);

	/*
	 * If we have submitted to multiple partitions we need to handle all of
	 * them on submit and remove if the job was cancelled before it ran
	 * (!job_ptr->tres_alloc_str).
	 */
	if (((type == ACCT_POLICY_ADD_SUBMIT) ||
	    (type == ACCT_POLICY_REM_SUBMIT)) &&
	    job_ptr->part_ptr_list &&
	    (IS_JOB_PENDING(job_ptr) || !job_ptr->tres_alloc_str)) {
		bool job_first = false;
		ListIterator part_itr;
		struct part_record *part_ptr;
		List part_qos_list = NULL;

		if (job_ptr->qos_ptr &&
		    (((slurmdb_qos_rec_t *)job_ptr->qos_ptr)->flags
		     & QOS_FLAG_OVER_PART_QOS))
			job_first = true;

		if (job_first) {
			_qos_adjust_limit_usage(type, job_ptr, job_ptr->qos_ptr,
						used_tres_run_secs, job_cnt);
			part_qos_list = list_create(NULL);
			list_push(part_qos_list, job_ptr->qos_ptr);
		}

		part_itr = list_iterator_create(job_ptr->part_ptr_list);
		while ((part_ptr = list_next(part_itr))) {
			if (!part_ptr->qos_ptr)
				continue;
			if (!part_qos_list)
				part_qos_list = list_create(NULL);
			if (list_find_first(part_qos_list, _find_qos_part,
					    part_ptr->qos_ptr))
				continue;
			list_push(part_qos_list, part_ptr->qos_ptr);
			_qos_adjust_limit_usage(type, job_ptr,
						part_ptr->qos_ptr,
						used_tres_run_secs, job_cnt);
		}
		list_iterator_destroy(part_itr);

		if (!job_first && (!part_qos_list ||
		    !list_find_first(part_qos_list, _find_qos_part,
				     job_ptr->qos_ptr)))
			_qos_adjust_limit_usage(type, job_ptr, job_ptr->qos_ptr,
						used_tres_run_secs, job_cnt);

		FREE_NULL_LIST(part_qos_list);
	} else {
		slurmdb_qos_rec_t *qos_ptr_1, *qos_ptr_2;

		/*
		 * Here if the job is starting and we had a part_ptr_list before
		 * hand we need to remove the submit from all partition qos
		 * outside of the one we actually are going to run on.
		 */
		if ((type == ACCT_POLICY_JOB_BEGIN) &&
		    job_ptr->part_ptr_list) {
			ListIterator part_itr;
			struct part_record *part_ptr;
			List part_qos_list = list_create(NULL);

			if (job_ptr->qos_ptr)
				list_push(part_qos_list, job_ptr->qos_ptr);
			if (job_ptr->part_ptr && job_ptr->part_ptr->qos_ptr &&
			    job_ptr->qos_ptr != job_ptr->part_ptr->qos_ptr)
				list_push(part_qos_list,
					  job_ptr->part_ptr->qos_ptr);

			part_itr = list_iterator_create(job_ptr->part_ptr_list);
			while ((part_ptr = list_next(part_itr))) {
				if (!part_ptr->qos_ptr)
					continue;

				if (list_find_first(part_qos_list,
						    _find_qos_part,
						    part_ptr->qos_ptr))
					continue;
				_qos_adjust_limit_usage(ACCT_POLICY_REM_SUBMIT,
							job_ptr,
							part_ptr->qos_ptr,
							used_tres_run_secs,
							job_cnt);
			}
			list_iterator_destroy(part_itr);
			FREE_NULL_LIST(part_qos_list);
		}

		_set_qos_order(job_ptr, &qos_ptr_1, &qos_ptr_2);

		_qos_adjust_limit_usage(type, job_ptr, qos_ptr_1,
					used_tres_run_secs, job_cnt);
		_qos_adjust_limit_usage(type, job_ptr, qos_ptr_2,
					used_tres_run_secs, job_cnt);
	}

	assoc_ptr = (slurmdb_assoc_rec_t *)job_ptr->assoc_ptr;
	while (assoc_ptr) {
		switch(type) {
		case ACCT_POLICY_ADD_SUBMIT:
			assoc_ptr->usage->used_submit_jobs += job_cnt;
			break;
		case ACCT_POLICY_REM_SUBMIT:
			if (assoc_ptr->usage->used_submit_jobs)
				assoc_ptr->usage->used_submit_jobs -= job_cnt;
			else
				debug2("acct_policy_remove_job_submit: "
				       "used_submit_jobs underflow for "
				       "account %s",
				       assoc_ptr->acct);
			break;
		case ACCT_POLICY_JOB_BEGIN:
			assoc_ptr->usage->used_jobs++;
			for (i=0; i<slurmctld_tres_cnt; i++) {
				assoc_ptr->usage->grp_used_tres[i] +=
					job_ptr->tres_alloc_cnt[i];
				assoc_ptr->usage->grp_used_tres_run_secs[i] +=
					used_tres_run_secs[i];
				debug2("acct_policy_job_begin: after "
				       "adding job %u, assoc %u(%s/%s/%s) "
				       "grp_used_tres_run_secs(%s) "
				       "is %"PRIu64,
				       job_ptr->job_id,
				       assoc_ptr->id, assoc_ptr->acct,
				       assoc_ptr->user, assoc_ptr->partition,
				       assoc_mgr_tres_name_array[i],
				       assoc_ptr->usage->
				       grp_used_tres_run_secs[i]);
			}
			break;
		case ACCT_POLICY_JOB_FINI:
			if (assoc_ptr->usage->used_jobs)
				assoc_ptr->usage->used_jobs--;
			else
				debug2("acct_policy_job_fini: used_jobs "
				       "underflow for account %s",
				       assoc_ptr->acct);

			for (i=0; i<slurmctld_tres_cnt; i++) {
				if (job_ptr->tres_alloc_cnt[i] >
				    assoc_ptr->usage->grp_used_tres[i]) {
					assoc_ptr->usage->grp_used_tres[i] = 0;
					debug2("acct_policy_job_fini: "
					       "grp_used_tres(%s) "
					       "underflow for assoc "
					       "%u(%s/%s/%s)",
					       assoc_mgr_tres_name_array[i],
					       assoc_ptr->id, assoc_ptr->acct,
					       assoc_ptr->user,
					       assoc_ptr->partition);
				} else
					assoc_ptr->usage->grp_used_tres[i] -=
						job_ptr->tres_alloc_cnt[i];
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

static void _set_time_limit(uint32_t *time_limit, uint32_t part_max_time,
			    uint32_t limit_max_time, uint16_t *limit_set_time)
{
	if ((*time_limit) == NO_VAL) {
		if (part_max_time == INFINITE)
			(*time_limit) = limit_max_time;
		else
			(*time_limit) = MIN(limit_max_time, part_max_time);

		if (limit_set_time)
			(*limit_set_time) = 1;
	} else if (limit_set_time && (*limit_set_time) &&
		   ((*time_limit) > limit_max_time))
		(*time_limit) = limit_max_time;
}

static void _qos_alter_job(struct job_record *job_ptr,
			   slurmdb_qos_rec_t *qos_ptr,
			   uint64_t *used_tres_run_secs,
			   uint64_t *new_used_tres_run_secs)
{
	int i;

	if (!qos_ptr || !job_ptr)
		return;

	for (i=0; i<slurmctld_tres_cnt; i++) {
		if (used_tres_run_secs[i] == new_used_tres_run_secs[i])
			continue;
		qos_ptr->usage->grp_used_tres_run_secs[i] -=
			used_tres_run_secs[i];
		qos_ptr->usage->grp_used_tres_run_secs[i] +=
			new_used_tres_run_secs[i];
		debug2("altering job %u QOS %s "
		       "got %"PRIu64" just removed %"PRIu64
		       " and added %"PRIu64"",
		       job_ptr->job_id,
		       qos_ptr->name,
		       qos_ptr->usage->grp_used_tres_run_secs[i],
		       used_tres_run_secs[i],
		       new_used_tres_run_secs[i]);
	}
}

/*
 * _validate_tres_limits_for_assoc - validate the tres requested against limits
 * of an association as well as qos skipping any limit an admin set
 *
 * OUT - tres_pos - if false is returned position in array of failed limit
 * IN - job_tres_array - count of various TRES requested by the job
 * IN - divisor - divide the job_tres_array TRES by this variable, 0 if none
 * IN - assoc_tres_array - TRES limits from an association (Grp, Max, Min)
 * IN - qos_tres_array - TRES limits QOS has imposed already
 * IN - acct_policy_limit_set_array - limits that have been overridden
 *                                    by an admin
 * IN strict_checking - If a limit needs to be enforced now or not.
 * IN update_call - If this is an update or a create call
 * IN max_limit - Limits are for MAX else, the limits are MIN.
 *
 * RET - True if no limit is violated, false otherwise with tres_pos
 * being set to the position of the failed limit.
 */
static bool _validate_tres_limits_for_assoc(
	int *tres_pos,
	uint64_t *job_tres_array,
	uint64_t divisor,
	uint64_t *assoc_tres_array,
	uint64_t *qos_tres_array,
	uint16_t *admin_set_limit_tres_array,
	bool strict_checking,
	bool update_call, bool max_limit)
{
	int i;
	uint64_t job_tres;

	if (!strict_checking)
		return true;

	for (i = 0; i < g_tres_count; i++) {
		(*tres_pos) = i;

		if ((admin_set_limit_tres_array[i] == ADMIN_SET_LIMIT)
		    || (qos_tres_array[i] != INFINITE64)
		    || (assoc_tres_array[i] == INFINITE64)
		    || (!job_tres_array[i] && !update_call))
			continue;

		job_tres = job_tres_array[i];

		if (divisor)
			job_tres /= divisor;

		if (max_limit) {
			if (job_tres > assoc_tres_array[i])
				return false;
		} else if (job_tres < assoc_tres_array[i])
				return false;
	}

	return true;
}

/*
 * _validate_tres_usage_limits_for_assoc - validate the tres requested
 * against limits
 * of an association as well as qos skipping any limit an admin set
 *
 * OUT - tres_pos - if false is returned position in array of failed limit
 * IN - tres_limit_array - TRES limits from an association
 * IN - qos_tres_limit_array - TRES limits QOS has imposed already
 * IN - tres_req_cnt - TRES requested from the job
 * IN - tres_usage - TRES usage in use right now by the assoc (running jobs)
 * IN - curr_usage - TRES usage from the association (in minutes)
 * IN - admin_limit_set - TRES limits that have been overridden by an admin
 * IN - safe_limits - if the safe flag was set on AccountingStorageEnforce
 *
 * RET - True if no limit is violated, false otherwise with tres_pos
 * being set to the position of the failed limit.
 */
static int _validate_tres_usage_limits_for_assoc(
	int *tres_pos,
	uint64_t *tres_limit_array,
	uint64_t *qos_tres_limit_array,
	uint64_t *tres_req_cnt,
	uint64_t *tres_usage,
	uint64_t *curr_usage,
	uint16_t *admin_limit_set,
	bool safe_limits)
{
	int i;
	uint64_t usage = 0;

	xassert(tres_limit_array);
	xassert(qos_tres_limit_array);

	for (i = 0; i < g_tres_count; i++) {
		(*tres_pos) = i;

		if ((admin_limit_set
		     && admin_limit_set[i] == ADMIN_SET_LIMIT) ||
		    (qos_tres_limit_array[i] != INFINITE64) ||
		    (tres_limit_array[i] == INFINITE64))
			continue;

		if (curr_usage && (curr_usage[i] >= tres_limit_array[i]))
			return 1;

		if (safe_limits) {
			xassert(tres_req_cnt);
			if (tres_req_cnt[i] > tres_limit_array[i])
				return 2;

			if (curr_usage)
				usage = curr_usage[i];
			if (tres_usage &&
			    ((tres_req_cnt[i] + tres_usage[i]) >
			     (tres_limit_array[i] - usage)))
				return 3;
		}
	}

	return 0;
}

/*
 * _validate_tres_limits_for_qos - validate the tres requested against limits
 * of a QOS as well as qos skipping any limit an admin set
 *
 * OUT - tres_pos - if false is returned position in array of failed limit
 * IN - job_tres_array - count of various TRES requested by the job
 * IN - divisor - divide the job_tres_array TRES by this variable, 0 if none
 * IN - grp_tres_array - Grp TRES limits from QOS
 * IN - max_tres_array - Max/Min TRES limits from QOS
 * IN/OUT - out_grp_tres_array - Grp TRES limits QOS has imposed already,
 *                               if a new limit is found the limit is filled in.
 * IN/OUT - out_max_tres_array - Max/Min TRES limits QOS has imposed already,
 *                               if a new limit is found the limit is filled in.
 * IN - acct_policy_limit_set_array - limits that have been overridden
 *                                    by an admin
 * IN strict_checking - If a limit needs to be enforced now or not.
 * IN max_limit - Limits are for MAX else, the limits are MIN.
 *
 * RET - True if no limit is violated, false otherwise with tres_pos
 * being set to the position of the failed limit.
 */
static bool _validate_tres_limits_for_qos(
	int *tres_pos,
	uint64_t *job_tres_array,
	uint64_t divisor,
	uint64_t *grp_tres_array,
	uint64_t *max_tres_array,
	uint64_t *out_grp_tres_array,
	uint64_t *out_max_tres_array,
	uint16_t *admin_set_limit_tres_array,
	bool strict_checking, bool max_limit)
{
	uint64_t max_tres_limit, out_max_tres_limit;
	int i;
	uint64_t job_tres;

	if (!strict_checking)
		return true;

	for (i = 0; i < g_tres_count; i++) {
		(*tres_pos) = i;
		if (grp_tres_array) {
			max_tres_limit = MIN(grp_tres_array[i],
					     max_tres_array[i]);
			out_max_tres_limit = MIN(out_grp_tres_array[i],
						 out_max_tres_array[i]);
		} else {
			max_tres_limit = max_tres_array[i];
			out_max_tres_limit = out_max_tres_array[i];
		}

		/* we don't need to look at this limit */
		if ((admin_set_limit_tres_array[i] == ADMIN_SET_LIMIT)
		    || (out_max_tres_limit != INFINITE64)
		    || (max_tres_limit == INFINITE64)
		    || (job_tres_array[i] && (job_tres_array[i] == NO_VAL64)))
			continue;

		out_max_tres_array[i] = max_tres_array[i];

		job_tres = job_tres_array[i];

		if (divisor)
			job_tres /= divisor;

		if (out_grp_tres_array) {
			if (out_grp_tres_array[i] == INFINITE64)
				out_grp_tres_array[i] = grp_tres_array[i];

			if (max_limit) {
				if (job_tres > grp_tres_array[i])
					return false;
			}  else if (job_tres < grp_tres_array[i])
				return false;
		}

		if (max_limit) {
			if (job_tres > max_tres_array[i])
				return false;
		} else if (job_tres < max_tres_array[i])
			return false;
	}

	return true;
}

/* Only check the time_limits if the admin didn't set
 * the timelimit.
 * It is important we look at these even if strict_checking
 * isn't set so we get the correct time_limit from the job.
 */
static bool _validate_time_limit(uint32_t *time_limit_in,
				 uint32_t part_max_time,
				 uint64_t tres_req_cnt,
				 uint64_t max_limit,
				 void *out_max_limit,
				 uint16_t *limit_set_time,
				 bool strict_checking,
				 bool is64)
{
	uint32_t max_time_limit;
	uint64_t out_max_64 = *(uint64_t *)out_max_limit;
	uint32_t out_max_32 = *(uint32_t *)out_max_limit;

	if (!tres_req_cnt ||
	    !strict_checking || (*limit_set_time) == ADMIN_SET_LIMIT)
		return true;

	if (is64) {
		if ((out_max_64 != INFINITE64) ||
		    (max_limit == INFINITE64) ||
		    (tres_req_cnt == NO_VAL64))
			return true;
	} else {
		if ((out_max_32 != INFINITE) ||
		    ((uint32_t)max_limit == INFINITE) ||
		    ((uint32_t)tres_req_cnt == NO_VAL))
			return true;
	}

	max_time_limit = (uint32_t)(max_limit / tres_req_cnt);

	_set_time_limit(time_limit_in, part_max_time, max_time_limit,
			limit_set_time);

	if (is64)
		(*(uint64_t *)out_max_limit) = max_limit;
	else
		(*(uint32_t *)out_max_limit) = (uint32_t)max_limit;

	if ((*time_limit_in) > max_time_limit)
		return false;

	return true;
}



/*
 * _validate_tres_time_limits - validate the tres requested
 * against limits of an association as well as qos skipping any limit
 * an admin set
 *
 * OUT - tres_pos - if false is returned position in array of failed limit
 * IN/OUT - time_limit_in - Job's time limit, set and returned based off limits
 *                          if none is given.
 * IN - part_max_time - Job's partition max time limit
 * IN - job_tres_array - count of various TRES requested by the job
 * IN - max_tres_array - Max TRES limits of association/QOS
 * OUT - out_max_tres_array - Max TRES limits as set by the various TRES
 * OUT - limit_set_time - set if the time_limit was set by a limit QOS/Assoc or
 *                        otherwise.
 * IN strict_checking - If a limit needs to be enforced now or not.
 *
 * RET - True if no limit is violated, false otherwise with tres_pos
 * being set to the position of the failed limit.
 */
static bool _validate_tres_time_limits(
	int *tres_pos,
	uint32_t *time_limit_in,
	uint32_t part_max_time,
	uint64_t *job_tres_array,
	uint64_t *max_tres_array,
	uint64_t *out_max_tres_array,
	uint16_t *limit_set_time,
	bool strict_checking)
{
	int i;
//	uint32_t max_time_limit;

	if (!strict_checking || (*limit_set_time) == ADMIN_SET_LIMIT)
		return true;

	for (i = 0; i < g_tres_count; i++) {
		(*tres_pos) = i;

		if (!_validate_time_limit(time_limit_in, part_max_time,
					  job_tres_array[i],
					  max_tres_array[i],
					  &out_max_tres_array[i],
					  limit_set_time,
					  strict_checking, true))
			return false;
		/* if ((out_max_tres_array[i] != INFINITE64) || */
		/*     (max_tres_array[i] == INFINITE64) || */
		/*     (job_tres_array[i] == NO_VAL64) || */
		/*     (job_tres_array[i] == 0)) */
		/* 	continue; */

		/* max_time_limit = (uint32_t)(max_tres_array[i] / */
		/* 			    job_tres_array[i]); */

		/* _set_time_limit(time_limit_in, */
		/* 		part_max_time, max_time_limit, */
		/* 		limit_set_time); */

		/* out_max_tres_array[i] = max_tres_array[i]; */

		/* if ((*time_limit_in) > max_time_limit) */
		/* 	return false; */
	}

	return true;
}

/*
 * _validate_tres_usage_limits_for_qos - validate the tres requested
 * against limits of an association as well as qos skipping any limit
 * an admin set
 *
 * OUT - tres_pos - if false is returned position in array of failed limit
 * IN - tres_limit_array - TRES limits from an association
 * IN/OUT - out_tres_limit_array - TRES limits QOS has imposed already, if a new
 *                                 limit is found the limit is filled in.
 * IN - tres_req_cnt - TRES requested from the job
 * IN - tres_usage - TRES usage in use right now by the QOS (running jobs)
 * IN - curr_usage - TRES usage from the QOS (in minutes)
 * IN - admin_limit_set - TRES limits that have been overridden by an admin
 * IN - safe_limits - if the safe flag was set on AccountingStorageEnforce
 *
 * RET - True if no limit is violated, false otherwise with tres_pos
 * being set to the position of the failed limit.
 */
static int _validate_tres_usage_limits_for_qos(
	int *tres_pos,
	uint64_t *tres_limit_array,
	uint64_t *out_tres_limit_array,
	uint64_t *tres_req_cnt,
	uint64_t *tres_usage,
	uint64_t *curr_usage,
	uint16_t *admin_limit_set,
	bool safe_limits)
{
	uint64_t usage = 0;
	int i;

	xassert(tres_limit_array);
	xassert(out_tres_limit_array);

	for (i = 0; i < g_tres_count; i++) {
		(*tres_pos) = i;

		if ((admin_limit_set
		     && admin_limit_set[i] == ADMIN_SET_LIMIT) ||
		    (out_tres_limit_array[i] != INFINITE64) ||
		    (tres_limit_array[i] == INFINITE64))
			continue;

		out_tres_limit_array[i] = tres_limit_array[i];

		if (curr_usage && (curr_usage[i] >= tres_limit_array[i]))
			return 1;

		if (safe_limits) {
			xassert(tres_req_cnt);
			if (tres_req_cnt[i] > tres_limit_array[i])
				return 2;

			if (curr_usage)
				usage = curr_usage[i];
			if (tres_usage &&
			    ((tres_req_cnt[i] + tres_usage[i]) >
			     (tres_limit_array[i] - usage)))
				return 3;
		}
	}

	return 0;
}

static int _qos_policy_validate(job_desc_msg_t *job_desc,
				slurmdb_assoc_rec_t *assoc_ptr,
				struct part_record *part_ptr,
				slurmdb_qos_rec_t *qos_ptr,
				slurmdb_qos_rec_t *qos_out_ptr,
				uint32_t *reason,
				acct_policy_limit_set_t *acct_policy_limit_set,
				bool update_call,
				char *user_name,
				int job_cnt,
				bool strict_checking)
{
	int rc = true;
	int tres_pos = 0;

	if (!qos_ptr || !qos_out_ptr)
		return rc;

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   job_desc->tres_req_cnt, 0,
					   NULL,
					   qos_ptr->max_tres_pa_ctld,
					   NULL,
					   qos_out_ptr->max_tres_pa_ctld,
					   acct_policy_limit_set->tres,
					   strict_checking, 1)) {
		if (job_desc->tres_req_cnt[tres_pos] >
		    qos_ptr->max_tres_pa_ctld[tres_pos]) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos, WAIT_QOS_MAX_UNK_PER_ACCT);

			debug2("job submit for user %s(%u): "
			       "min tres(%s) request %"PRIu64" exceeds "
			       "per-acct max tres limit %"PRIu64" for qos '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       job_desc->tres_req_cnt[tres_pos],
			       qos_ptr->max_tres_pa_ctld[tres_pos],
			       qos_ptr->name);
			rc = false;
			goto end_it;
		}
	}

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   job_desc->tres_req_cnt, 0,
					   qos_ptr->grp_tres_ctld,
					   qos_ptr->max_tres_pu_ctld,
					   qos_out_ptr->grp_tres_ctld,
					   qos_out_ptr->max_tres_pu_ctld,
					   acct_policy_limit_set->tres,
					   strict_checking, 1)) {
		if (job_desc->tres_req_cnt[tres_pos] >
		    qos_ptr->max_tres_pu_ctld[tres_pos]) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos, WAIT_QOS_MAX_UNK_PER_USER);

			debug2("job submit for user %s(%u): "
			       "min tres(%s) request %"PRIu64" exceeds "
			       "per-user max tres limit %"PRIu64" for qos '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       job_desc->tres_req_cnt[tres_pos],
			       qos_ptr->max_tres_pu_ctld[tres_pos],
			       qos_ptr->name);
			rc = false;
			goto end_it;
		} else if (job_desc->tres_req_cnt[tres_pos] >
			   qos_ptr->grp_tres_ctld[tres_pos]) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos, WAIT_QOS_GRP_UNK);

			debug2("job submit for user %s(%u): "
			       "min tres(%s) request %"PRIu64" exceeds "
			       "group max tres limit %"PRIu64" for qos '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       job_desc->tres_req_cnt[tres_pos],
			       qos_ptr->grp_tres_ctld[tres_pos],
			       qos_ptr->name);
			rc = false;
			goto end_it;
		}
	}

	/* for validation we don't need to look at
	 * qos_ptr->grp_jobs.
	 */

	if ((qos_out_ptr->grp_submit_jobs == INFINITE) &&
	    (qos_ptr->grp_submit_jobs != INFINITE)) {

		qos_out_ptr->grp_submit_jobs = qos_ptr->grp_submit_jobs;

		if ((qos_ptr->usage->grp_used_submit_jobs + job_cnt)
		    > qos_ptr->grp_submit_jobs) {
			if (reason)
				*reason = WAIT_QOS_GRP_SUB_JOB;
			debug2("job submit for user %s(%u): "
			       "group max submit job limit exceeded %u "
			       "for qos '%s'",
			       user_name,
			       job_desc->user_id,
			       qos_ptr->grp_submit_jobs,
			       qos_ptr->name);
			rc = false;
			goto end_it;
		}
	}

	/* Only check the time_limits if the admin didn't set the timelimit.
	 * It is important we look at these even if strict_checking
	 * isn't set so we get the correct time_limit from the job.
	 */
	if (acct_policy_limit_set->time != ADMIN_SET_LIMIT) {
		if (!_validate_tres_time_limits(
			    &tres_pos,
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    job_desc->tres_req_cnt,
			    qos_ptr->max_tres_mins_pj_ctld,
			    qos_out_ptr->max_tres_mins_pj_ctld,
			    &acct_policy_limit_set->time,
			    strict_checking)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos,
					WAIT_QOS_MAX_UNK_MINS_PER_JOB);
			debug2("job submit for user %s(%u): "
			       "tres(%s) time limit request %"PRIu64" "
			       "exceeds max per-job limit %"PRIu64" "
			       "for qos '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       ((uint64_t)job_desc->time_limit *
				job_desc->tres_req_cnt[tres_pos]),
			       qos_ptr->max_tres_mins_pj_ctld[tres_pos],
			       qos_ptr->name);
			rc = false;
			goto end_it;
		}

		if (!_validate_tres_time_limits(
			    &tres_pos,
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    job_desc->tres_req_cnt,
			    qos_ptr->grp_tres_mins_ctld,
			    qos_out_ptr->grp_tres_mins_ctld,
			    &acct_policy_limit_set->time,
			    strict_checking)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos, WAIT_QOS_GRP_UNK_MIN);
			debug2("job submit for user %s(%u): "
			       "tres(%s) time limit request %"PRIu64" "
			       "exceeds group max limit %"PRIu64" "
			       "for qos '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       ((uint64_t)job_desc->time_limit *
				job_desc->tres_req_cnt[tres_pos]),
			       qos_ptr->grp_tres_mins_ctld[tres_pos],
			       qos_ptr->name);
			rc = false;
			goto end_it;
		}

		if (!_validate_tres_time_limits(
			    &tres_pos,
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    job_desc->tres_req_cnt,
			    qos_ptr->grp_tres_run_mins_ctld,
			    qos_out_ptr->grp_tres_run_mins_ctld,
			    &acct_policy_limit_set->time,
			    strict_checking)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos, WAIT_QOS_GRP_UNK_RUN_MIN);
			debug2("job submit for user %s(%u): "
			       "tres(%s) time limit request %"PRIu64" "
			       "exceeds group max running limit %"PRIu64" "
			       "for qos '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       ((uint64_t)job_desc->time_limit *
				job_desc->tres_req_cnt[tres_pos]),
			       qos_ptr->grp_tres_run_mins_ctld[tres_pos],
			       qos_ptr->name);
			rc = false;
			goto end_it;
		}

		if ((qos_out_ptr->max_wall_pj == INFINITE) &&
		    (qos_ptr->max_wall_pj != INFINITE)) {
			_set_time_limit(&job_desc->time_limit,
					part_ptr->max_time,
					qos_ptr->max_wall_pj,
					&acct_policy_limit_set->time);
			qos_out_ptr->max_wall_pj = qos_ptr->max_wall_pj;

			if (strict_checking
			    && job_desc->time_limit > qos_ptr->max_wall_pj) {
				if (reason)
					*reason = WAIT_QOS_MAX_WALL_PER_JOB;
				debug2("job submit for user %s(%u): "
				       "time limit %u exceeds qos max %u",
				       user_name,
				       job_desc->user_id,
				       job_desc->time_limit,
				       qos_ptr->max_wall_pj);
				rc = false;
				goto end_it;
			}
		}

		if ((qos_out_ptr->grp_wall == INFINITE) &&
		    (qos_ptr->grp_wall != INFINITE)) {
			_set_time_limit(&job_desc->time_limit,
					part_ptr->max_time,
					qos_ptr->grp_wall,
					&acct_policy_limit_set->time);

			qos_out_ptr->grp_wall = qos_ptr->grp_wall;

			if (strict_checking
			    && job_desc->time_limit > qos_ptr->grp_wall) {
				if (reason)
					*reason = WAIT_QOS_GRP_WALL;
				debug2("job submit for user %s(%u): "
				       "time limit %u exceeds qos grp max %u",
				       user_name,
				       job_desc->user_id,
				       job_desc->time_limit,
				       qos_ptr->grp_wall);
				rc = false;
				goto end_it;
			}
		}
	}

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   job_desc->tres_req_cnt, 0,
					   NULL,
					   qos_ptr->max_tres_pj_ctld,
					   NULL,
					   qos_out_ptr->max_tres_pj_ctld,
					   acct_policy_limit_set->tres,
					   strict_checking, 1)) {
		if (reason)
			*reason = _get_tres_state_reason(
				tres_pos, WAIT_QOS_MAX_UNK_PER_JOB);

		debug2("job submit for user %s(%u): "
		       "min tres(%s) request %"PRIu64" exceeds "
		       "per-job max tres limit %"PRIu64" for qos '%s'",
		       user_name,
		       job_desc->user_id,
		       assoc_mgr_tres_name_array[tres_pos],
		       job_desc->tres_req_cnt[tres_pos],
		       qos_ptr->max_tres_pj_ctld[tres_pos],
		       qos_ptr->name);
		rc = false;
		goto end_it;
	}

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   job_desc->tres_req_cnt,
					   job_desc->tres_req_cnt[
						   TRES_ARRAY_NODE],
					   NULL,
					   qos_ptr->max_tres_pn_ctld,
					   NULL,
					   qos_out_ptr->max_tres_pn_ctld,
					   acct_policy_limit_set->tres,
					   strict_checking, 1)) {
		if (reason)
			*reason = _get_tres_state_reason(
				tres_pos, WAIT_QOS_MAX_UNK_PER_NODE);

		debug2("job submit for user %s(%u): "
		       "min tres(%s) request %"PRIu64" exceeds "
		       "per-node max tres limit %"PRIu64" for qos '%s'",
		       user_name,
		       job_desc->user_id,
		       assoc_mgr_tres_name_array[tres_pos],
		       job_desc->tres_req_cnt[tres_pos] /
		       job_desc->tres_req_cnt[TRES_ARRAY_NODE],
		       qos_ptr->max_tres_pn_ctld[tres_pos],
		       qos_ptr->name);
		rc = false;
		goto end_it;
	}

	/* for validation we don't need to look at
	 * qos_ptr->max_jobs.
	 */

	if ((qos_out_ptr->max_submit_jobs_pa == INFINITE) &&
	    (qos_ptr->max_submit_jobs_pa != INFINITE)) {
		slurmdb_used_limits_t *used_limits =
			_get_acct_used_limits(
				&qos_ptr->usage->acct_limit_list,
				assoc_ptr->acct);

		qos_out_ptr->max_submit_jobs_pa = qos_ptr->max_submit_jobs_pa;

		if ((used_limits->submit_jobs + job_cnt) >
		    qos_ptr->max_submit_jobs_pa) {
			if (reason)
				*reason = WAIT_QOS_MAX_SUB_JOB_PER_ACCT;
			debug2("job submit for account %s: "
			       "qos max submit job limit exceeded %u",
			       assoc_ptr->acct,
			       qos_ptr->max_submit_jobs_pa);
			rc = false;
			goto end_it;
		}
	}

	if ((qos_out_ptr->max_submit_jobs_pu == INFINITE) &&
	    (qos_ptr->max_submit_jobs_pu != INFINITE)) {
		slurmdb_used_limits_t *used_limits =
			_get_user_used_limits(
				&qos_ptr->usage->user_limit_list,
				job_desc->user_id);

		qos_out_ptr->max_submit_jobs_pu = qos_ptr->max_submit_jobs_pu;

		if ((used_limits->submit_jobs + job_cnt) >
		     qos_ptr->max_submit_jobs_pu) {
			if (reason)
				*reason = WAIT_QOS_MAX_SUB_JOB;
			debug2("job submit for user %s(%u): "
			       "qos max submit job limit exceeded %u",
			       user_name,
			       job_desc->user_id,
			       qos_ptr->max_submit_jobs_pu);
			rc = false;
			goto end_it;
		}
	}

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   job_desc->tres_req_cnt, 0,
					   NULL,
					   qos_ptr->min_tres_pj_ctld,
					   NULL,
					   qos_out_ptr->min_tres_pj_ctld,
					   acct_policy_limit_set->tres,
					   strict_checking, 0)) {
		if (reason)
			*reason = _get_tres_state_reason(
				tres_pos, WAIT_QOS_MIN_UNK);

		debug2("job submit for user %s(%u): "
		       "min tres(%s) request %"PRIu64" exceeds "
		       "per-job max tres limit %"PRIu64" for qos '%s'",
		       user_name,
		       job_desc->user_id,
		       assoc_mgr_tres_name_array[tres_pos],
		       job_desc->tres_req_cnt[tres_pos],
		       qos_ptr->min_tres_pj_ctld[tres_pos],
		       qos_ptr->name);
		rc = false;
		goto end_it;
	}

end_it:
	return rc;
}

static int _qos_job_runnable_pre_select(struct job_record *job_ptr,
					slurmdb_qos_rec_t *qos_ptr,
					slurmdb_qos_rec_t *qos_out_ptr)
{
	uint32_t wall_mins;
	uint32_t time_limit = NO_VAL;
	int rc = true;
	slurmdb_used_limits_t *used_limits = NULL, *used_limits_a = NULL;
	bool safe_limits = false;
	slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;

	if (!qos_ptr || !qos_out_ptr || !assoc_ptr)
		return rc;

	/* check to see if we should be using safe limits, if so we
	 * will only start a job if there are sufficient remaining
	 * cpu-minutes for it to run to completion */
	if (accounting_enforce & ACCOUNTING_ENFORCE_SAFE)
		safe_limits = true;

	wall_mins = qos_ptr->usage->grp_used_wall / 60;

	used_limits_a =	_get_acct_used_limits(&qos_ptr->usage->acct_limit_list,
					      assoc_ptr->acct);

	used_limits = _get_user_used_limits(&qos_ptr->usage->user_limit_list,
					    job_ptr->user_id);


	/* we don't need to check grp_tres_mins here */

	/* we don't need to check grp_tres here */

	/* we don't need to check grp_mem here */
	if ((qos_out_ptr->grp_jobs == INFINITE) &&
	    (qos_ptr->grp_jobs != INFINITE)) {

		qos_out_ptr->grp_jobs = qos_ptr->grp_jobs;

		if (qos_ptr->usage->grp_used_jobs >= qos_ptr->grp_jobs) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = WAIT_QOS_GRP_JOB;
			debug2("job %u being held, "
			       "the job is at or exceeds "
			       "group max jobs limit %u with %u for qos %s",
			       job_ptr->job_id,
			       qos_ptr->grp_jobs,
			       qos_ptr->usage->grp_used_jobs, qos_ptr->name);

			rc = false;
			goto end_it;
		}
	}

	/* we don't need to check grp_tres_run_mins here */

	/* we don't need to check grp_nodes here */

	/* we don't need to check submit_jobs here */

	if ((job_ptr->limit_set.time != ADMIN_SET_LIMIT)
	    && (qos_out_ptr->grp_wall == INFINITE)
	    && (qos_ptr->grp_wall != INFINITE)) {
		if (time_limit == NO_VAL) {
			time_limit = job_ptr->time_limit;
			_set_time_limit(&time_limit,
					job_ptr->part_ptr->max_time,
					MIN(qos_ptr->grp_wall,
					    qos_ptr->max_wall_pj),
					&job_ptr->limit_set.time);
		}

		qos_out_ptr->grp_wall = qos_ptr->grp_wall;

		if (wall_mins >= qos_ptr->grp_wall) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = WAIT_QOS_GRP_WALL;
			debug2("job %u being held, "
			       "the job is at or exceeds "
			       "group wall limit %u "
			       "with %u for qos %s",
			       job_ptr->job_id,
			       qos_ptr->grp_wall,
			       wall_mins, qos_ptr->name);
			rc = false;
			goto end_it;
		} else if (safe_limits &&
			   ((wall_mins + time_limit) > qos_ptr->grp_wall)) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = WAIT_QOS_GRP_WALL;
			debug2("job %u being held, "
			       "the job request will exceed "
			       "group wall limit %u if ran "
			       "with %u for qos %s",
			       job_ptr->job_id,
			       qos_ptr->grp_wall,
			       wall_mins + time_limit, qos_ptr->name);
			rc = false;
			goto end_it;
		}
	}

	/* we don't need to check max_tres_mins_pj here */

	/* we don't need to check max_tres_pj here */

	/* we don't need to check max_tres_pn here */

	/* we don't need to check min_tres_pj here */

	/* we don't need to check max_tres_pa here */

	/* we don't need to check max_tres_pu here */

	if ((qos_out_ptr->max_jobs_pa == INFINITE)
	    && (qos_ptr->max_jobs_pa != INFINITE)) {

		qos_out_ptr->max_jobs_pa = qos_ptr->max_jobs_pa;

		if (used_limits_a->jobs >= qos_ptr->max_jobs_pa) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason =
				WAIT_QOS_MAX_JOB_PER_ACCT;
			debug2("job %u being held, "
			       "the job is at or exceeds "
			       "max jobs per-acct (%s) limit "
			       "%u with %u for QOS %s",
			       job_ptr->job_id,
			       used_limits_a->acct,
			       qos_ptr->max_jobs_pa,
			       used_limits_a->jobs, qos_ptr->name);
			rc = false;
			goto end_it;
		}
	}

	if ((qos_out_ptr->max_jobs_pu == INFINITE)
	    && (qos_ptr->max_jobs_pu != INFINITE)) {

		qos_out_ptr->max_jobs_pu = qos_ptr->max_jobs_pu;

		if (used_limits->jobs >= qos_ptr->max_jobs_pu) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason =
				WAIT_QOS_MAX_JOB_PER_USER;
			debug2("job %u being held, "
			       "the job is at or exceeds "
			       "max jobs per-user limit "
			       "%u with %u for QOS %s",
			       job_ptr->job_id,
			       qos_ptr->max_jobs_pu,
			       used_limits->jobs, qos_ptr->name);
			rc = false;
			goto end_it;
		}
	}

	/* we don't need to check submit_jobs_pa here */

	/* we don't need to check submit_jobs_pu here */

	/* if the qos limits have changed since job
	 * submission and job can not run, then kill it */
	if ((job_ptr->limit_set.time != ADMIN_SET_LIMIT)
	    && (qos_out_ptr->max_wall_pj == INFINITE)
	    && (qos_ptr->max_wall_pj != INFINITE)) {
		if (time_limit == NO_VAL) {
			time_limit = job_ptr->time_limit;
			_set_time_limit(&time_limit,
					job_ptr->part_ptr->max_time,
					qos_ptr->max_wall_pj,
					&job_ptr->limit_set.time);
		}

		qos_out_ptr->max_wall_pj = qos_ptr->max_wall_pj;

		if (time_limit > qos_out_ptr->max_wall_pj) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason =
				WAIT_QOS_MAX_WALL_PER_JOB;
			debug2("job %u being held, "
			       "time limit %u exceeds qos "
			       "max wall pj %u",
			       job_ptr->job_id,
			       job_ptr->time_limit,
			       time_limit);
			rc = false;
			goto end_it;
		}
	}
end_it:

	return rc;
}

static int _qos_job_runnable_post_select(struct job_record *job_ptr,
					 slurmdb_qos_rec_t *qos_ptr,
					 slurmdb_qos_rec_t *qos_out_ptr,
					 uint64_t *tres_req_cnt,
					 uint64_t *job_tres_time_limit)
{
	uint64_t tres_usage_mins[slurmctld_tres_cnt];
	uint64_t tres_run_mins[slurmctld_tres_cnt];
	slurmdb_used_limits_t *used_limits = NULL, *used_limits_a = NULL;
	bool safe_limits = false;
	int rc = true;
	int i, tres_pos = 0;
	slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;

	if (!qos_ptr || !qos_out_ptr || !assoc_ptr)
		return rc;

	/* check to see if we should be using safe limits, if so we
	 * will only start a job if there are sufficient remaining
	 * cpu-minutes for it to run to completion */
	if (accounting_enforce & ACCOUNTING_ENFORCE_SAFE)
		safe_limits = true;

	/* clang needs this memset to avoid a warning */
	memset(tres_run_mins, 0, sizeof(tres_run_mins));
	memset(tres_usage_mins, 0, sizeof(tres_usage_mins));
	for (i=0; i<slurmctld_tres_cnt; i++) {
		tres_run_mins[i] =
			qos_ptr->usage->grp_used_tres_run_secs[i] / 60;
		tres_usage_mins[i] =
			(uint64_t)(qos_ptr->usage->usage_tres_raw[i] / 60.0);
	}

	used_limits_a =	_get_acct_used_limits(&qos_ptr->usage->acct_limit_list,
					      assoc_ptr->acct);

	used_limits = _get_user_used_limits(&qos_ptr->usage->user_limit_list,
					    job_ptr->user_id);

	i = _validate_tres_usage_limits_for_qos(
		&tres_pos, qos_ptr->grp_tres_mins_ctld,
		qos_out_ptr->grp_tres_mins_ctld, job_tres_time_limit,
		tres_run_mins, tres_usage_mins, job_ptr->limit_set.tres,
		safe_limits);
	switch (i) {
	case 1:
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_GRP_UNK_MIN);
		debug2("Job %u being held, "
		       "QOS %s group max tres(%s) minutes limit "
		       "of %"PRIu64" is already at or exceeded with %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       qos_ptr->grp_tres_mins_ctld[tres_pos],
		       tres_usage_mins[tres_pos]);
		rc = false;
		goto end_it;
		break;
	case 2:
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_GRP_UNK_MIN);
		debug2("Job %u being held, "
		       "the job is requesting more than allowed with QOS %s's "
		       "group max tres(%s) minutes of %"PRIu64" "
		       "with %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       qos_ptr->grp_tres_mins_ctld[tres_pos],
		       job_tres_time_limit[tres_pos]);
		rc = false;
		goto end_it;
		break;
	case 3:
		/*
		 * If we're using safe limits start
		 * the job only if there are
		 * sufficient cpu-mins left such that
		 * it will run to completion without
		 * being killed
		 */
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_GRP_UNK_MIN);
		debug2("Job %u being held, "
		       "the job is at or exceeds QOS %s's "
		       "group max tres(%s) minutes of %"PRIu64" "
		       "of which %"PRIu64" are still available "
		       "but request is for %"PRIu64" "
		       "(plus %"PRIu64" already in use) tres "
		       "minutes (request tres count %"PRIu64")",
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       qos_ptr->grp_tres_mins_ctld[tres_pos],
		       qos_ptr->grp_tres_mins_ctld[tres_pos] -
		       tres_usage_mins[tres_pos],
		       job_tres_time_limit[tres_pos],
		       tres_run_mins[tres_pos],
		       tres_req_cnt[tres_pos]);
		rc = false;
		goto end_it;
		break;
	default:
		/* all good */
		break;
	}

	/* If the JOB's cpu limit wasn't administratively set and the
	 * QOS has a GrpCPU limit, cancel the job if its minimum
	 * cpu requirement has exceeded the limit for all CPUs
	 * usable by the QOS
	 */
	i = _validate_tres_usage_limits_for_qos(
		&tres_pos,
		qos_ptr->grp_tres_ctld,	qos_out_ptr->grp_tres_ctld,
		tres_req_cnt, qos_ptr->usage->grp_used_tres,
		NULL, job_ptr->limit_set.tres, 1);
	switch (i) {
	case 1:
		/* not possible because the curr_usage sent in is NULL */
		break;
	case 2:
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_GRP_UNK);
		debug2("job %u is being held, "
		       "QOS %s min tres(%s) request %"PRIu64" exceeds "
		       "group max tres limit %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       tres_req_cnt[tres_pos],
		       qos_ptr->grp_tres_ctld[tres_pos]);
		rc = false;
		goto end_it;
		break;
	case 3:
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_GRP_UNK);
		debug2("job %u being held, "
		       "if allowed the job request will exceed "
		       "QOS %s group max tres(%s) limit "
		       "%"PRIu64" with already used %"PRIu64" + "
		       "requested %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       qos_ptr->grp_tres_ctld[tres_pos],
		       qos_ptr->usage->grp_used_tres[tres_pos],
		       tres_req_cnt[tres_pos]);
		rc = false;
		goto end_it;
	default:
		/* all good */
		break;
	}

	/* we don't need to check grp_jobs here */

	i = _validate_tres_usage_limits_for_qos(
		&tres_pos,
		qos_ptr->grp_tres_run_mins_ctld,
		qos_out_ptr->grp_tres_run_mins_ctld,
		job_tres_time_limit, tres_run_mins, NULL, NULL, 1);
	switch (i) {
	case 1:
		/* not possible because the curr_usage sent in is NULL */
		break;
	case 2:
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_GRP_UNK_RUN_MIN);
		debug2("job %u is being held, "
		       "QOS %s group max running tres(%s) minutes request "
		       "%"PRIu64" exceeds limit %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       job_tres_time_limit[tres_pos],
		       qos_ptr->grp_tres_run_mins_ctld[tres_pos]);
		rc = false;
		goto end_it;
		break;
	case 3:
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_GRP_UNK_RUN_MIN);
		debug2("job %u being held, "
		       "if allowed the job request will exceed "
		       "QOS %s group max running tres(%s) minutes "
		       "limit %"PRIu64" with already "
		       "used %"PRIu64" + requested %"PRIu64,
		       job_ptr->job_id, qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       qos_ptr->grp_tres_run_mins_ctld[tres_pos],
		       tres_run_mins[tres_pos],
		       job_tres_time_limit[tres_pos]);
		rc = false;
		goto end_it;
		break;
	default:
		/* all good */
		break;
	}

	/* we don't need to check submit_jobs here */

	/* we don't need to check grp_wall here */

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   job_tres_time_limit, 0,
					   NULL,
					   qos_ptr->max_tres_mins_pj_ctld,
					   NULL,
					   qos_out_ptr->max_tres_mins_pj_ctld,
					   job_ptr->limit_set.tres,
					   1, 1)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MAX_UNK_MINS_PER_JOB);
		debug2("Job %u being held, "
		       "the job is requesting more than allowed with QOS %s's "
		       "max tres(%s) minutes of %"PRIu64" "
		       "with %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       qos_ptr->max_tres_mins_pj_ctld[tres_pos],
		       job_tres_time_limit[tres_pos]);
		rc = false;
		goto end_it;
	}

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   tres_req_cnt, 0,
					   NULL,
					   qos_ptr->max_tres_pj_ctld,
					   NULL,
					   qos_out_ptr->max_tres_pj_ctld,
					   job_ptr->limit_set.tres,
					   1, 1)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MAX_UNK_PER_JOB);
		debug2("job %u is being held, "
		       "QOS %s min tres(%s) per job "
		       "request %"PRIu64" exceeds "
		       "max tres limit %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       tres_req_cnt[tres_pos],
		       qos_ptr->max_tres_pj_ctld[tres_pos]);
		rc = false;
		goto end_it;
	}

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   tres_req_cnt,
					   tres_req_cnt[TRES_ARRAY_NODE],
					   NULL,
					   qos_ptr->max_tres_pn_ctld,
					   NULL,
					   qos_out_ptr->max_tres_pn_ctld,
					   job_ptr->limit_set.tres,
					   1, 1)) {
		uint64_t req_per_node;
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MAX_UNK_PER_NODE);
		req_per_node = tres_req_cnt[tres_pos];
		if (tres_req_cnt[TRES_ARRAY_NODE] > 1)
			req_per_node /= tres_req_cnt[TRES_ARRAY_NODE];
		debug2("job %u is being held, "
		       "QOS %s min tres(%s) per node "
		       "request %"PRIu64" exceeds "
		       "max tres limit %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       req_per_node,
		       qos_ptr->max_tres_pn_ctld[tres_pos]);
		rc = false;
		goto end_it;
	}

	if (!_validate_tres_limits_for_qos(&tres_pos,
					   tres_req_cnt, 0,
					   NULL,
					   qos_ptr->min_tres_pj_ctld,
					   NULL,
					   qos_out_ptr->min_tres_pj_ctld,
					   job_ptr->limit_set.tres,
					   1, 0)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MIN_UNK);
		debug2("job %u is being held, "
		       "QOS %s min tres(%s) per job "
		       "request %"PRIu64" exceeds "
		       "min tres limit %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       tres_req_cnt[tres_pos],
		       qos_ptr->min_tres_pj_ctld[tres_pos]);
		rc = false;
		goto end_it;
	}

	i = _validate_tres_usage_limits_for_qos(
		&tres_pos,
		qos_ptr->max_tres_pa_ctld, qos_out_ptr->max_tres_pa_ctld,
		tres_req_cnt, used_limits_a->tres,
		NULL, job_ptr->limit_set.tres, 1);
	switch (i) {
	case 1:
		/* not possible because the curr_usage sent in is NULL */
		break;
	case 2:
		/* Hold the job if it exceeds the per-acct
		 * TRES limit for the given QOS
		 */
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MAX_UNK_PER_ACCT);
		debug2("job %u is being held, "
		       "QOS %s min tres(%s) "
		       "request %"PRIu64" exceeds "
		       "max tres per account (%s) limit %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       tres_req_cnt[tres_pos],
		       used_limits_a->acct,
		       qos_ptr->max_tres_pa_ctld[tres_pos]);
		rc = false;
		goto end_it;
		break;
	case 3:
		/* Hold the job if the user has exceeded
		 * the QOS per-user TRES limit with their
		 * current usage */
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MAX_UNK_PER_ACCT);
		debug2("job %u being held, "
		       "if allowed the job request will exceed "
		       "QOS %s max tres(%s) per account (%s) limit "
		       "%"PRIu64" with already used %"PRIu64" + "
		       "requested %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       used_limits_a->acct,
		       qos_ptr->max_tres_pa_ctld[tres_pos],
		       used_limits_a->tres[tres_pos],
		       tres_req_cnt[tres_pos]);
		rc = false;
		goto end_it;
	default:
		/* all good */
		break;
	}

	i = _validate_tres_usage_limits_for_qos(
		&tres_pos,
		qos_ptr->max_tres_pu_ctld, qos_out_ptr->max_tres_pu_ctld,
		tres_req_cnt, used_limits->tres,
		NULL, job_ptr->limit_set.tres, 1);
	switch (i) {
	case 1:
		/* not possible because the curr_usage sent in is NULL */
		break;
	case 2:
		/* Hold the job if it exceeds the per-user
		 * TRES limit for the given QOS
		 */
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MAX_UNK_PER_USER);
		debug2("job %u is being held, "
		       "QOS %s min tres(%s) "
		       "request %"PRIu64" exceeds "
		       "max tres per user limit %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       tres_req_cnt[tres_pos],
		       qos_ptr->max_tres_pu_ctld[tres_pos]);
		rc = false;
		goto end_it;
		break;
	case 3:
		/* Hold the job if the user has exceeded
		 * the QOS per-user TRES limit with their
		 * current usage */
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = _get_tres_state_reason(
			tres_pos, WAIT_QOS_MAX_UNK_PER_USER);
		debug2("job %u being held, "
		       "if allowed the job request will exceed "
		       "QOS %s max tres(%s) per user limit "
		       "%"PRIu64" with already used %"PRIu64" + "
		       "requested %"PRIu64,
		       job_ptr->job_id,
		       qos_ptr->name,
		       assoc_mgr_tres_name_array[tres_pos],
		       qos_ptr->max_tres_pu_ctld[tres_pos],
		       used_limits->tres[tres_pos],
		       tres_req_cnt[tres_pos]);
		rc = false;
		goto end_it;
	default:
		/* all good */
		break;
	}

	/* We do not need to check max_jobs_pa here */

	/* We do not need to check max_jobs_pu here */

	/* we don't need to check submit_jobs_pa here */

	/* we don't need to check submit_jobs_pu here */

	/* we don't need to check max_wall_pj here */

end_it:
	if (!rc)
		job_ptr->qos_blocking_ptr = qos_ptr;

	return rc;
}

static int _qos_job_time_out(struct job_record *job_ptr,
			     slurmdb_qos_rec_t *qos_ptr,
			     slurmdb_qos_rec_t *qos_out_ptr,
			     uint64_t *job_tres_usage_mins)
{
	uint64_t tres_usage_mins[slurmctld_tres_cnt];
	uint32_t wall_mins;
	int rc = true, tres_pos = 0, i;
	time_t now = time(NULL);

	if (!qos_ptr || !qos_out_ptr)
		return rc;

	/* The idea here is for qos to trump what an association
	 * has set for a limit, so if an association set of
	 * wall 10 mins and the qos has 20 mins set and the
	 * job has been running for 11 minutes it continues
	 * until 20.
	 */
	/* clang needs this memset to avoid a warning */
	memset(tres_usage_mins, 0, sizeof(tres_usage_mins));
	for (i=0; i<slurmctld_tres_cnt; i++)
		tres_usage_mins[i] =
			(uint64_t)(qos_ptr->usage->usage_tres_raw[i] / 60.0);
	wall_mins = qos_ptr->usage->grp_used_wall / 60;

	i = _validate_tres_usage_limits_for_qos(
		&tres_pos, qos_ptr->grp_tres_mins_ctld,
		qos_out_ptr->grp_tres_mins_ctld, NULL,
		NULL, tres_usage_mins, NULL, 0);
	switch (i) {
	case 1:
		last_job_update = now;
		info("Job %u timed out, "
		     "the job is at or exceeds QOS %s's "
		     "group max tres(%s) minutes of %"PRIu64" "
		     "with %"PRIu64"",
		     job_ptr->job_id,
		     qos_ptr->name,
		     assoc_mgr_tres_name_array[tres_pos],
		     qos_ptr->grp_tres_mins_ctld[tres_pos],
		     tres_usage_mins[tres_pos]);
		job_ptr->state_reason = FAIL_TIMEOUT;
		rc = false;
		goto end_it;
		break;
	case 2:
		/* not possible safe_limits is 0 */
	case 3:
		/* not possible safe_limits is 0 */
	default:
		/* all good */
		break;
	}

	if ((qos_out_ptr->grp_wall == INFINITE)
	    && (qos_ptr->grp_wall != INFINITE)) {

		qos_out_ptr->grp_wall = qos_ptr->grp_wall;

		if (wall_mins >= qos_ptr->grp_wall) {
			last_job_update = now;
			info("Job %u timed out, "
			     "the job is at or exceeds QOS %s's "
			     "group wall limit of %u with %u",
			     job_ptr->job_id,
			     qos_ptr->name, qos_ptr->grp_wall,
			     wall_mins);
			job_ptr->state_reason = FAIL_TIMEOUT;
			rc = false;
			goto end_it;
		}
	}

	i = _validate_tres_usage_limits_for_qos(
		&tres_pos, qos_ptr->max_tres_mins_pj_ctld,
		qos_out_ptr->max_tres_mins_pj_ctld, job_tres_usage_mins,
		NULL, NULL, NULL, 1);
	switch (i) {
	case 1:
		/* not possible curr_usage is NULL */
		break;
	case 2:
		last_job_update = now;
		info("Job %u timed out, "
		     "the job is at or exceeds QOS %s's "
		     "max tres(%s) minutes of %"PRIu64" with %"PRIu64,
		     job_ptr->job_id,
		     qos_ptr->name,
		     assoc_mgr_tres_name_array[tres_pos],
		     qos_ptr->max_tres_mins_pj_ctld[tres_pos],
		     job_tres_usage_mins[tres_pos]);
		job_ptr->state_reason = FAIL_TIMEOUT;
		rc = false;
		goto end_it;
		break;
	case 3:
		/* not possible tres_usage is NULL */
	default:
		/* all good */
		break;
	}

end_it:
	return rc;
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
	/* if end_time_exp == NO_VAL this has already happened */
	if (job_ptr->end_time_exp != (time_t)NO_VAL)
		_adjust_limit_usage(ACCT_POLICY_JOB_FINI, job_ptr);
	else
		debug2("We have already ran the job_fini for job %u",
		       job_ptr->job_id);
}

extern void acct_policy_alter_job(struct job_record *job_ptr,
				  uint32_t new_time_limit)
{
	slurmdb_qos_rec_t *qos_ptr_1, *qos_ptr_2;
	slurmdb_assoc_rec_t *assoc_ptr = NULL;
	assoc_mgr_lock_t locks = { WRITE_LOCK, NO_LOCK, WRITE_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
	uint64_t used_tres_run_secs[slurmctld_tres_cnt];
	uint64_t new_used_tres_run_secs[slurmctld_tres_cnt];
	uint64_t time_limit_secs, new_time_limit_secs;
	int i;

	if (!IS_JOB_RUNNING(job_ptr) || (job_ptr->time_limit == new_time_limit))
		return;

	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)
	    || !_valid_job_assoc(job_ptr))
		return;

	time_limit_secs = (uint64_t)job_ptr->time_limit * 60;
	new_time_limit_secs = (uint64_t)new_time_limit * 60;

	/* clang needs these memset to avoid a warning */
	memset(used_tres_run_secs, 0, sizeof(used_tres_run_secs));
	memset(new_used_tres_run_secs, 0, sizeof(new_used_tres_run_secs));
	for (i=0; i<slurmctld_tres_cnt; i++) {
		used_tres_run_secs[i] =
			job_ptr->tres_alloc_cnt[i] * time_limit_secs;
		new_used_tres_run_secs[i] =
			job_ptr->tres_alloc_cnt[i] * new_time_limit_secs;
	}

	assoc_mgr_lock(&locks);

	_set_qos_order(job_ptr, &qos_ptr_1, &qos_ptr_2);

	_qos_alter_job(job_ptr, qos_ptr_1,
		       used_tres_run_secs, new_used_tres_run_secs);
	_qos_alter_job(job_ptr, qos_ptr_2,
		       used_tres_run_secs, new_used_tres_run_secs);

	assoc_ptr = (slurmdb_assoc_rec_t *)job_ptr->assoc_ptr;
	while (assoc_ptr) {
		for (i=0; i<slurmctld_tres_cnt; i++) {
			if (used_tres_run_secs[i] == new_used_tres_run_secs[i])
				continue;
			assoc_ptr->usage->grp_used_tres_run_secs[i] -=
				used_tres_run_secs[i];
			assoc_ptr->usage->grp_used_tres_run_secs[i] +=
				new_used_tres_run_secs[i];
			debug2("altering job %u assoc %u(%s/%s/%s) "
			       "got %"PRIu64" just removed %"PRIu64
			       " and added %"PRIu64"",
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_ptr->usage->grp_used_tres_run_secs[i],
			       used_tres_run_secs[i],
			       new_used_tres_run_secs[i]);
		}

		/* now handle all the group limits of the parents */
		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
	}
	assoc_mgr_unlock(&locks);
}

extern bool acct_policy_validate(job_desc_msg_t *job_desc,
				 struct part_record *part_ptr,
				 slurmdb_assoc_rec_t *assoc_in,
				 slurmdb_qos_rec_t *qos_ptr,
				 uint32_t *reason,
				 acct_policy_limit_set_t *acct_policy_limit_set,
				 bool update_call)
{
	slurmdb_qos_rec_t *qos_ptr_1, *qos_ptr_2;
	slurmdb_qos_rec_t qos_rec;
	slurmdb_assoc_rec_t *assoc_ptr = assoc_in;
	int parent = 0, job_cnt = 1;
	char *user_name = NULL;
	bool rc = true;
	struct job_record job_rec;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
	bool strict_checking;

	xassert(acct_policy_limit_set);

	if (!assoc_ptr) {
		error("acct_policy_validate: no assoc_ptr given for job.");
		return false;
	}
	user_name = assoc_ptr->user;

	if (job_desc->array_bitmap)
		job_cnt = bit_set_count(job_desc->array_bitmap);

	slurmdb_init_qos_rec(&qos_rec, 0, INFINITE);

	assoc_mgr_lock(&locks);

	assoc_mgr_set_qos_tres_cnt(&qos_rec);

	job_rec.qos_ptr = qos_ptr;
	job_rec.part_ptr = part_ptr;

	_set_qos_order(&job_rec, &qos_ptr_1, &qos_ptr_2);

	if (qos_ptr_1) {
		strict_checking =
			(reason || (qos_ptr_1->flags & QOS_FLAG_DENY_LIMIT));
		if (qos_ptr_2 && !strict_checking)
			strict_checking =
				qos_ptr_2->flags & QOS_FLAG_DENY_LIMIT;

		if (!(rc = _qos_policy_validate(
			      job_desc, assoc_ptr, part_ptr,
			      qos_ptr_1, &qos_rec,
			      reason, acct_policy_limit_set, update_call,
			      user_name, job_cnt, strict_checking)))
			goto end_it;
		if (!(rc = _qos_policy_validate(
			      job_desc, assoc_ptr,
			      part_ptr, qos_ptr_2, &qos_rec,
			      reason, acct_policy_limit_set, update_call,
			      user_name, job_cnt, strict_checking)))
			goto end_it;

	} else
		strict_checking = reason ? true : false;

	while (assoc_ptr) {
		int tres_pos = 0;

		if (!_validate_tres_limits_for_assoc(
			    &tres_pos, job_desc->tres_req_cnt, 0,
			    assoc_ptr->grp_tres_ctld,
			    qos_rec.grp_tres_ctld,
			    acct_policy_limit_set->tres,
			    strict_checking, update_call, 1)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos, WAIT_ASSOC_GRP_UNK);

			debug2("job submit for user %s(%u): "
			       "min tres(%s) request %"PRIu64" exceeds "
			       "group max tres limit %"PRIu64" for account %s",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       job_desc->tres_req_cnt[tres_pos],
			       assoc_ptr->grp_tres_ctld[tres_pos],
			       assoc_ptr->acct);
			rc = false;
			break;
		}

		/* for validation we don't need to look at
		 * assoc_ptr->grp_jobs.
		 */

		if ((qos_rec.grp_submit_jobs == INFINITE) &&
		    (assoc_ptr->grp_submit_jobs != INFINITE) &&
		    ((assoc_ptr->usage->used_submit_jobs + job_cnt)
		     > assoc_ptr->grp_submit_jobs)) {
			if (reason)
				*reason = WAIT_ASSOC_GRP_SUB_JOB;
			debug2("job submit for user %s(%u): "
			       "group max submit job limit exceeded %u "
			       "for account '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_ptr->grp_submit_jobs,
			       assoc_ptr->acct);
			rc = false;
			break;
		}

		tres_pos = 0;
		if (!update_call && !_validate_tres_time_limits(
			    &tres_pos,
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    job_desc->tres_req_cnt,
			    assoc_ptr->grp_tres_mins_ctld,
			    qos_rec.grp_tres_mins_ctld,
			    &acct_policy_limit_set->time,
			    strict_checking)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos,
					WAIT_ASSOC_GRP_UNK_MIN);
			debug2("job submit for user %s(%u): "
			       "tres(%s) time limit request %"PRIu64" "
			       "exceeds group max limit %"PRIu64" "
			       "for account '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       ((uint64_t)job_desc->time_limit *
				job_desc->tres_req_cnt[tres_pos]),
			       assoc_ptr->
			       grp_tres_mins_ctld[tres_pos],
			       assoc_ptr->acct);
			rc = false;
			goto end_it;
		}

		tres_pos = 0;
		if (!update_call && !_validate_tres_time_limits(
			    &tres_pos,
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    job_desc->tres_req_cnt,
			    assoc_ptr->grp_tres_run_mins_ctld,
			    qos_rec.grp_tres_run_mins_ctld,
			    &acct_policy_limit_set->time,
			    strict_checking)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos,
					WAIT_ASSOC_GRP_UNK_RUN_MIN);
			debug2("job submit for user %s(%u): "
			       "tres(%s) time limit request %"PRIu64" "
			       "exceeds group max running "
			       "limit %"PRIu64" for account '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       ((uint64_t)job_desc->time_limit *
				job_desc->tres_req_cnt[tres_pos]),
			       assoc_ptr->
			       grp_tres_run_mins_ctld[tres_pos],
			       assoc_ptr->acct);
			rc = false;
			goto end_it;
		}

		if (!update_call && !_validate_time_limit(
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    1,
			    assoc_ptr->grp_wall,
			    &qos_rec.grp_wall,
			    &acct_policy_limit_set->time,
			    strict_checking, false)) {
			if (reason)
				*reason = WAIT_ASSOC_GRP_WALL;
			debug2("job submit for user %s(%u): "
			       "time limit %u exceeds max group %u for "
			       "account '%s'",
			       user_name,
			       job_desc->user_id,
			       job_desc->time_limit,
			       assoc_ptr->grp_wall,
			       assoc_ptr->acct);
			rc = false;
			break;
		}

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

		tres_pos = 0;
		if (!_validate_tres_limits_for_assoc(
			    &tres_pos, job_desc->tres_req_cnt, 0,
			    assoc_ptr->max_tres_ctld,
			    qos_rec.max_tres_pj_ctld,
			    acct_policy_limit_set->tres,
			    strict_checking, update_call, 1)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos, WAIT_ASSOC_MAX_UNK_PER_JOB);

			debug2("job submit for user %s(%u): "
			       "min tres(%s) request %"PRIu64" exceeds "
			       "max tres limit %"PRIu64" for account %s",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       job_desc->tres_req_cnt[tres_pos],
			       assoc_ptr->max_tres_ctld[tres_pos],
			       assoc_ptr->acct);
			rc = false;
			break;
		}

		tres_pos = 0;
		if (!_validate_tres_limits_for_assoc(
			    &tres_pos, job_desc->tres_req_cnt,
			    job_desc->tres_req_cnt[TRES_ARRAY_NODE],
			    assoc_ptr->max_tres_pn_ctld,
			    qos_rec.max_tres_pn_ctld,
			    acct_policy_limit_set->tres,
			    strict_checking, update_call, 1)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos,
					WAIT_ASSOC_MAX_UNK_PER_NODE);

			debug2("job submit for user %s(%u): "
			       "min tres(%s) request %"PRIu64" exceeds "
			       "max tres limit %"PRIu64" per node "
			       "for account %s",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       job_desc->tres_req_cnt[tres_pos] /
			       job_desc->tres_req_cnt[TRES_ARRAY_NODE],
			       assoc_ptr->max_tres_pn_ctld[tres_pos],
			       assoc_ptr->acct);
			rc = false;
			break;
		}

		/* for validation we don't need to look at
		 * assoc_ptr->max_jobs.
		 */

		if ((qos_rec.max_submit_jobs_pa == INFINITE) &&
		    (qos_rec.max_submit_jobs_pu == INFINITE) &&
		    (assoc_ptr->max_submit_jobs != INFINITE) &&
		    ((assoc_ptr->usage->used_submit_jobs + job_cnt)
		     > assoc_ptr->max_submit_jobs)) {
			if (reason)
				*reason = WAIT_ASSOC_MAX_SUB_JOB;
			debug2("job submit for user %s(%u): "
			       "account max submit job limit exceeded %u",
			       user_name,
			       job_desc->user_id,
			       assoc_ptr->max_submit_jobs);
			rc = false;
			break;
		}

		if (!update_call && !_validate_tres_time_limits(
			    &tres_pos,
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    job_desc->tres_req_cnt,
			    assoc_ptr->max_tres_mins_ctld,
			    qos_rec.max_tres_mins_pj_ctld,
			    &acct_policy_limit_set->time,
			    strict_checking)) {
			if (reason)
				*reason = _get_tres_state_reason(
					tres_pos,
					WAIT_ASSOC_MAX_UNK_MINS_PER_JOB);
			debug2("job submit for user %s(%u): "
			       "tres(%s) time limit request %"PRIu64" "
			       "exceeds max per-job limit %"PRIu64" "
			       "for account '%s'",
			       user_name,
			       job_desc->user_id,
			       assoc_mgr_tres_name_array[tres_pos],
			       ((uint64_t)job_desc->time_limit *
				job_desc->tres_req_cnt[tres_pos]),
			       assoc_ptr->max_tres_mins_ctld[tres_pos],
			       assoc_ptr->acct);
			rc = false;
			break;
		}

		if (!update_call && !_validate_time_limit(
			    &job_desc->time_limit,
			    part_ptr->max_time,
			    1,
			    assoc_ptr->max_wall_pj,
			    &qos_rec.max_wall_pj,
			    &acct_policy_limit_set->time,
			    strict_checking, false)) {
			if (reason)
				*reason = WAIT_ASSOC_MAX_WALL_PER_JOB;
			debug2("job submit for user %s(%u): "
			       "time limit %u exceeds max %u for "
			       "account '%s'",
			       user_name,
			       job_desc->user_id,
			       job_desc->time_limit,
			       assoc_ptr->max_wall_pj,
			       assoc_ptr->acct);
			rc = false;
			break;
		}

		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		parent = 1;
	}
end_it:
	assoc_mgr_unlock(&locks);
	slurmdb_free_qos_rec_members(&qos_rec);

	return rc;
}

/*
 * Determine if the specified job can execute right now or is currently
 * blocked by an association or QOS limit. Does not re-validate job state.
 */
extern bool acct_policy_job_runnable_state(struct job_record *job_ptr)
{
	/* If any more limits are added this will need to be added to */
	if ((job_ptr->state_reason >= WAIT_QOS_GRP_CPU
	     && job_ptr->state_reason <= WAIT_ASSOC_MAX_SUB_JOB) ||
	    (job_ptr->state_reason == WAIT_ASSOC_JOB_LIMIT) ||
	    (job_ptr->state_reason == WAIT_ASSOC_RESOURCE_LIMIT) ||
	    (job_ptr->state_reason == WAIT_ASSOC_TIME_LIMIT) ||
	    (job_ptr->state_reason == WAIT_QOS_JOB_LIMIT) ||
	    (job_ptr->state_reason == WAIT_QOS_TIME_LIMIT)) {
		return false;
	}

	return true;
}

/*
 * acct_policy_job_runnable_pre_select - Determine if the specified
 *	job can execute right now or not depending upon accounting
 *	policy (e.g. running job limit for this association). If the
 *	association limits prevent the job from ever running (lowered
 *	limits since job submission), then cancel the job.
 */
extern bool acct_policy_job_runnable_pre_select(struct job_record *job_ptr)
{
	slurmdb_qos_rec_t *qos_ptr_1, *qos_ptr_2;
	slurmdb_qos_rec_t qos_rec;
	slurmdb_assoc_rec_t *assoc_ptr;
	uint32_t time_limit = NO_VAL;
	bool rc = true;
	uint32_t wall_mins;
	bool safe_limits = false;
	int parent = 0; /* flag to tell us if we are looking at the
			 * parent or not
			 */
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	/* check to see if we are enforcing associations */
	if (!accounting_enforce)
		return true;

	if (!_valid_job_assoc(job_ptr)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = FAIL_ACCOUNT;
		return false;
	}

	/* now see if we are enforcing limits */
	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return true;

	/* clear old state reason */
	if (!acct_policy_job_runnable_state(job_ptr)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = WAIT_NO_REASON;
	}

	slurmdb_init_qos_rec(&qos_rec, 0, INFINITE);

	assoc_mgr_lock(&locks);

	assoc_mgr_set_qos_tres_cnt(&qos_rec);

	_set_qos_order(job_ptr, &qos_ptr_1, &qos_ptr_2);

	/* check the first QOS setting it's values in the qos_rec */
	if (qos_ptr_1 &&
	    !(rc = _qos_job_runnable_pre_select(job_ptr, qos_ptr_1, &qos_rec)))
		goto end_it;

	/* If qos_ptr_1 didn't set the value use the 2nd QOS to set
	   the limit.
	*/
	if (qos_ptr_2 &&
	    !(rc = _qos_job_runnable_pre_select(job_ptr, qos_ptr_2, &qos_rec)))
		goto end_it;

	/* check to see if we should be using safe limits, if so we
	 * will only start a job if there are sufficient remaining
	 * cpu-minutes for it to run to completion */
	if (accounting_enforce & ACCOUNTING_ENFORCE_SAFE)
		safe_limits = true;

	assoc_ptr = job_ptr->assoc_ptr;
	while (assoc_ptr) {
		/* This only trips when the grp_used_wall is divisible
		 * by 60, i.e if a limit is 1 min and you have only
		 * accumulated 59 seconds you will still be able to
		 * get another job in as 59/60 = 0 int wise.
		 */
		wall_mins = assoc_ptr->usage->grp_used_wall / 60;

#if _DEBUG
		info("acct_job_limits: %u of %u",
		     assoc_ptr->usage->used_jobs, assoc_ptr->max_jobs);
#endif
		/* we don't need to check grp_cpu_mins here */

		/* we don't need to check grp_cpus here */

		/* we don't need to check grp_mem here */

		if ((qos_rec.grp_jobs == INFINITE) &&
		    (assoc_ptr->grp_jobs != INFINITE) &&
		    (assoc_ptr->usage->used_jobs >= assoc_ptr->grp_jobs)) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = WAIT_ASSOC_GRP_JOB;
			debug2("job %u being held, "
			       "assoc %u is at or exceeds "
			       "group max jobs limit %u with %u for account %s",
			       job_ptr->job_id, assoc_ptr->id,
			       assoc_ptr->grp_jobs,
			       assoc_ptr->usage->used_jobs, assoc_ptr->acct);

			rc = false;
			goto end_it;
		}

		/* we don't need to check grp_cpu_run_mins here */

		/* we don't need to check grp_nodes here */

		/* we don't need to check submit_jobs here */

		if ((job_ptr->limit_set.time != ADMIN_SET_LIMIT)
		    && (qos_rec.grp_wall == INFINITE)
		    && (assoc_ptr->grp_wall != INFINITE)) {
			if (time_limit == NO_VAL) {
				time_limit = job_ptr->time_limit;
				_set_time_limit(&time_limit,
						job_ptr->part_ptr->max_time,
						MIN(assoc_ptr->grp_wall,
						    assoc_ptr->max_wall_pj),
						&job_ptr->limit_set.time);
			}

			if (wall_mins >= assoc_ptr->grp_wall) {
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = WAIT_ASSOC_GRP_WALL;
				debug2("job %u being held, "
				       "assoc %u is at or exceeds "
				       "group wall limit %u "
				       "with %u for account %s",
				       job_ptr->job_id, assoc_ptr->id,
				       assoc_ptr->grp_wall,
				       wall_mins, assoc_ptr->acct);
				rc = false;
				goto end_it;
			} else if (safe_limits &&
				   ((wall_mins + time_limit) >
				    assoc_ptr->grp_wall)) {
				xfree(job_ptr->state_desc);
				job_ptr->state_reason = WAIT_ASSOC_GRP_WALL;
				debug2("job %u being held, "
				       "the job request with assoc %u "
				       "will exceed group wall limit %u if ran "
				       "with %u for account %s",
				       job_ptr->job_id, assoc_ptr->id,
				       assoc_ptr->grp_wall,
				       wall_mins + time_limit, assoc_ptr->acct);
				rc = false;
				goto end_it;
			}
		}

		/* We don't need to look at the regular limits for
		 * parents since we have pre-propogated them, so just
		 * continue with the next parent
		 */
		if (parent) {
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
			continue;
		}

		/* we don't need to check max_cpu_mins_pj here */

		/* we don't need to check max_cpus_pj here */

		if ((qos_rec.max_jobs_pa == INFINITE) &&
		    (qos_rec.max_jobs_pu == INFINITE) &&
		    (assoc_ptr->max_jobs != INFINITE) &&
		    (assoc_ptr->usage->used_jobs >= assoc_ptr->max_jobs)) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = WAIT_ASSOC_MAX_JOBS;
			debug2("job %u being held, "
			       "assoc %u is at or exceeds "
			       "max jobs limit %u with %u for account %s",
			       job_ptr->job_id, assoc_ptr->id,
			       assoc_ptr->max_jobs,
			       assoc_ptr->usage->used_jobs, assoc_ptr->acct);
			rc = false;
			goto end_it;
		}

		/* we don't need to check submit_jobs here */

		/* if the association limits have changed since job
		 * submission and job can not run, then kill it */
		if ((job_ptr->limit_set.time != ADMIN_SET_LIMIT)
		    && (qos_rec.max_wall_pj == INFINITE)
		    && (assoc_ptr->max_wall_pj != INFINITE)) {
			if (time_limit == NO_VAL) {
				time_limit = job_ptr->time_limit;
				_set_time_limit(&time_limit,
						job_ptr->part_ptr->max_time,
						assoc_ptr->max_wall_pj,
						&job_ptr->limit_set.time);
			}

			if (time_limit > assoc_ptr->max_wall_pj) {
				xfree(job_ptr->state_desc);
				job_ptr->state_reason =
					WAIT_ASSOC_MAX_WALL_PER_JOB;
				debug2("job %u being held, "
				       "time limit %u exceeds account max %u",
				       job_ptr->job_id,
				       job_ptr->time_limit,
				       time_limit);
				rc = false;
				goto end_it;
			}
		}

		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		parent = 1;
	}
end_it:
	assoc_mgr_unlock(&locks);
	slurmdb_free_qos_rec_members(&qos_rec);

	return rc;
}

/*
 * acct_policy_job_runnable_post_select - After nodes have been
 *	selected for the job verify the counts don't exceed aggregated limits.
 */
extern bool acct_policy_job_runnable_post_select(
	struct job_record *job_ptr, uint64_t *tres_req_cnt)
{
	slurmdb_qos_rec_t *qos_ptr_1, *qos_ptr_2;
	slurmdb_qos_rec_t qos_rec;
	slurmdb_assoc_rec_t *assoc_ptr;
	uint64_t tres_usage_mins[slurmctld_tres_cnt];
	uint64_t tres_run_mins[slurmctld_tres_cnt];
	uint64_t job_tres_time_limit[slurmctld_tres_cnt];
	uint32_t time_limit;
	bool rc = true;
	bool safe_limits = false;
	int i, tres_pos;
	int parent = 0; /* flag to tell us if we are looking at the
			 * parent or not
			 */
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };

	xassert(job_ptr);
	xassert(job_ptr->part_ptr);
	xassert(tres_req_cnt);

	/* check to see if we are enforcing associations */
	if (!accounting_enforce)
		return true;

	/* probably don't need to check this here */
	/* if (!_valid_job_assoc(job_ptr)) { */
	/* 	job_ptr->state_reason = FAIL_ACCOUNT; */
	/* 	return false; */
	/* } */

	/* now see if we are enforcing limits */
	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return true;

	/* check to see if we should be using safe limits, if so we
	 * will only start a job if there are sufficient remaining
	 * cpu-minutes for it to run to completion */
	if (accounting_enforce & ACCOUNTING_ENFORCE_SAFE)
		safe_limits = true;

	/* clear old state reason */
	if (!acct_policy_job_runnable_state(job_ptr)) {
		xfree(job_ptr->state_desc);
		job_ptr->state_reason = WAIT_NO_REASON;
	}

	job_ptr->qos_blocking_ptr = NULL;

	/* clang needs this memset to avoid a warning */
	memset(tres_run_mins, 0, sizeof(tres_run_mins));
	memset(tres_usage_mins, 0, sizeof(tres_usage_mins));
	memset(job_tres_time_limit, 0, sizeof(job_tres_time_limit));

	/* time_limit may be NO_VAL if the partition does not have
	 * a DefaultTime, in which case the partition max_time should
	 * be used instead */
	time_limit = job_ptr->time_limit;
	_set_time_limit(&time_limit, job_ptr->part_ptr->max_time,
			job_ptr->part_ptr->default_time, NULL);

	for (i=0; i<slurmctld_tres_cnt; i++)
		job_tres_time_limit[i] = (uint64_t)time_limit * tres_req_cnt[i];

	slurmdb_init_qos_rec(&qos_rec, 0, INFINITE);

	assoc_mgr_lock(&locks);

	assoc_mgr_set_qos_tres_cnt(&qos_rec);

	_set_qos_order(job_ptr, &qos_ptr_1, &qos_ptr_2);

	/* check the first QOS setting it's values in the qos_rec */
	if (qos_ptr_1 &&
	    !(rc = _qos_job_runnable_post_select(job_ptr, qos_ptr_1,
						 &qos_rec, tres_req_cnt,
						 job_tres_time_limit)))
		goto end_it;

	/* If qos_ptr_1 didn't set the value use the 2nd QOS to set
	   the limit.
	*/
	if (qos_ptr_2 &&
	    !(rc = _qos_job_runnable_post_select(job_ptr, qos_ptr_2,
						 &qos_rec, tres_req_cnt,
						 job_tres_time_limit)))
		goto end_it;

	assoc_ptr = job_ptr->assoc_ptr;
	while (assoc_ptr) {
		for (i=0; i<slurmctld_tres_cnt; i++) {
			tres_usage_mins[i] =
				(uint64_t)(assoc_ptr->usage->usage_tres_raw[i]
					   / 60);
			tres_run_mins[i] =
				assoc_ptr->usage->grp_used_tres_run_secs[i] /
				60;
		}

#if _DEBUG
		info("acct_job_limits: %u of %u",
		     assoc_ptr->usage->used_jobs, assoc_ptr->max_jobs);
#endif
		/*
		 * If the association has a GrpCPUMins limit set (and there
		 * is no QOS with GrpCPUMins set) we may hold the job
		 */
		i = _validate_tres_usage_limits_for_assoc(
			&tres_pos, assoc_ptr->grp_tres_mins_ctld,
			qos_rec.grp_tres_mins_ctld,
			job_tres_time_limit, tres_run_mins,
			tres_usage_mins, job_ptr->limit_set.tres,
			safe_limits);
		switch (i) {
		case 1:
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_GRP_UNK_MIN);
			debug2("Job %u being held, "
			       "assoc %u(%s/%s/%s) group max tres(%s) "
			       "minutes limit of %"PRIu64" is already at or "
			       "exceeded with %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->grp_tres_mins_ctld[tres_pos],
			       tres_usage_mins[tres_pos]);
			rc = false;
			goto end_it;
			break;
		case 2:
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_GRP_UNK_MIN);
			debug2("Job %u being held, "
			       "the job is requesting more than allowed "
			       "with assoc %u(%s/%s/%s) "
			       "group max tres(%s) minutes of %"PRIu64" "
			       "with %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->grp_tres_mins_ctld[tres_pos],
			       job_tres_time_limit[tres_pos]);
			rc = false;
			goto end_it;
			break;
		case 3:
			/*
			 * If we're using safe limits start
			 * the job only if there are
			 * sufficient cpu-mins left such that
			 * it will run to completion without
			 * being killed
			 */
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_GRP_UNK_MIN);
			debug2("Job %u being held, "
			       "the job is at or exceeds assoc %u(%s/%s/%s) "
			       "group max tres(%s) minutes of %"PRIu64" "
			       "of which %"PRIu64" are still available "
			       "but request is for %"PRIu64" "
			       "(plus %"PRIu64" already in use) tres "
			       "minutes (request tres count %"PRIu64")",
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->grp_tres_mins_ctld[tres_pos],
			       assoc_ptr->grp_tres_mins_ctld[tres_pos] -
			       tres_usage_mins[tres_pos],
			       job_tres_time_limit[tres_pos],
			       tres_run_mins[tres_pos],
			       tres_req_cnt[tres_pos]);
			rc = false;
			goto end_it;
			break;
		default:
			/* all good */
			break;
		}


		i = _validate_tres_usage_limits_for_assoc(
			&tres_pos,
			assoc_ptr->grp_tres_ctld, qos_rec.grp_tres_ctld,
			tres_req_cnt, assoc_ptr->usage->grp_used_tres,
			NULL, job_ptr->limit_set.tres, 1);
		switch (i) {
		case 1:
			/* not possible because the curr_usage sent in is NULL*/
			break;
		case 2:
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_GRP_UNK);
			debug2("job %u is being held, "
			       "assoc %u(%s/%s/%s) min tres(%s) "
			       "request %"PRIu64" exceeds "
			       "group max tres limit %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       tres_req_cnt[tres_pos],
			       assoc_ptr->grp_tres_ctld[tres_pos]);
			rc = false;
			goto end_it;
			break;
		case 3:
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_GRP_UNK);
			debug2("job %u being held, "
			       "if allowed the job request will exceed "
			       "assoc %u(%s/%s/%s) group max "
			       "tres(%s) limit "
			       "%"PRIu64" with already used %"PRIu64" + "
			       "requested %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->grp_tres_ctld[tres_pos],
			       assoc_ptr->usage->grp_used_tres[tres_pos],
			       tres_req_cnt[tres_pos]);
			rc = false;
			goto end_it;
		default:
			/* all good */
			break;
		}

		/* we don't need to check grp_jobs here */

		i = _validate_tres_usage_limits_for_assoc(
			&tres_pos,
			assoc_ptr->grp_tres_run_mins_ctld,
			qos_rec.grp_tres_run_mins_ctld,
			job_tres_time_limit, tres_run_mins, NULL, NULL, 1);
		switch (i) {
		case 1:
			/* not possible because the curr_usage sent in is NULL*/
			break;
		case 2:
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_GRP_UNK_RUN_MIN);
			debug2("job %u is being held, "
			       "assoc %u(%s/%s/%s) group max running "
			       "tres(%s) minutes request limit %"PRIu64" "
			       "exceeds limit %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       tres_run_mins[tres_pos],
			       assoc_ptr->grp_tres_run_mins_ctld[tres_pos]);
			rc = false;
			goto end_it;
			break;
		case 3:
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_GRP_UNK_RUN_MIN);
			debug2("job %u being held, "
			       "if allowed the job request will exceed "
			       "assoc %u(%s/%s/%s) group max running "
			       "tres(%s) minutes limit %"PRIu64
			       " with already used %"PRIu64
			       " + requested %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->grp_tres_run_mins_ctld[tres_pos],
			       tres_run_mins[tres_pos],
			       job_tres_time_limit[tres_pos]);
			rc = false;
			goto end_it;
			break;
		default:
			/* all good */
			break;
		}

		/* we don't need to check submit_jobs here */

		/* we don't need to check grp_wall here */


		/* We don't need to look at the regular limits for
		 * parents since we have pre-propogated them, so just
		 * continue with the next parent
		 */
		if (parent) {
			assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
			continue;
		}

		if (!_validate_tres_limits_for_assoc(
			    &tres_pos, job_tres_time_limit, 0,
			    assoc_ptr->max_tres_mins_ctld,
			    qos_rec.max_tres_mins_pj_ctld,
			    job_ptr->limit_set.tres,
			    1, 0, 1)) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_MAX_UNK_MINS_PER_JOB);
			debug2("Job %u being held, "
			       "the job is requesting more than allowed "
			       "with assoc %u(%s/%s/%s) max tres(%s) "
			       "minutes of %"PRIu64" with %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->max_tres_mins_ctld[tres_pos],
			       job_tres_time_limit[tres_pos]);
			rc = false;
			goto end_it;
		}

		if (!_validate_tres_limits_for_assoc(
			    &tres_pos, tres_req_cnt, 0,
			    assoc_ptr->max_tres_ctld,
			    qos_rec.max_tres_pj_ctld,
			    job_ptr->limit_set.tres,
			    1, 0, 1)) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_MAX_UNK_PER_JOB);
			debug2("job %u is being held, "
			       "the job is requesting more than allowed "
			       "with assoc %u(%s/%s/%s) max tres(%s) "
			       "limit of %"PRIu64" with %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->max_tres_ctld[tres_pos],
			       tres_req_cnt[tres_pos]);
			rc = false;
			break;
		}

		if (!_validate_tres_limits_for_assoc(
			    &tres_pos, tres_req_cnt,
			    tres_req_cnt[TRES_ARRAY_NODE],
			    assoc_ptr->max_tres_pn_ctld,
			    qos_rec.max_tres_pn_ctld,
			    job_ptr->limit_set.tres,
			    1, 0, 1)) {
			xfree(job_ptr->state_desc);
			job_ptr->state_reason = _get_tres_state_reason(
				tres_pos, WAIT_ASSOC_MAX_UNK_PER_NODE);
			debug2("job %u is being held, "
			       "the job is requesting more than allowed "
			       "with assoc %u(%s/%s/%s) max tres(%s) "
			       "per node limit of %"PRIu64" with %"PRIu64,
			       job_ptr->job_id,
			       assoc_ptr->id, assoc_ptr->acct,
			       assoc_ptr->user, assoc_ptr->partition,
			       assoc_mgr_tres_name_array[tres_pos],
			       assoc_ptr->max_tres_pn_ctld[tres_pos],
			       tres_req_cnt[tres_pos]);
			rc = false;
			break;
		}

		/* we do not need to check max_jobs here */

		/* we don't need to check submit_jobs here */

		/* we don't need to check max_wall_pj here */

		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		parent = 1;
	}
end_it:
	assoc_mgr_unlock(&locks);
	slurmdb_free_qos_rec_members(&qos_rec);

	return rc;
}

extern uint32_t acct_policy_get_max_nodes(struct job_record *job_ptr,
					  uint32_t *wait_reason)
{
	uint64_t max_nodes_limit = INFINITE64, qos_max_p_limit = INFINITE64,
		grp_nodes = INFINITE64;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   NO_LOCK, NO_LOCK, NO_LOCK };
	slurmdb_qos_rec_t *qos_ptr_1, *qos_ptr_2;
	slurmdb_assoc_rec_t *assoc_ptr = job_ptr->assoc_ptr;
	bool parent = 0; /* flag to tell us if we are looking at the
			  * parent or not
			  */
	bool grp_set = 0;

	/* check to see if we are enforcing associations */
	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS))
		return max_nodes_limit;

	xassert(wait_reason);

	assoc_mgr_lock(&locks);

	_set_qos_order(job_ptr, &qos_ptr_1, &qos_ptr_2);

	if (qos_ptr_1) {
		uint64_t max_nodes_pj =
			qos_ptr_1->max_tres_pj_ctld[TRES_ARRAY_NODE];
		uint64_t max_nodes_pu =
			qos_ptr_1->max_tres_pu_ctld[TRES_ARRAY_NODE];
		uint64_t max_nodes_pa =
			qos_ptr_1->max_tres_pa_ctld[TRES_ARRAY_NODE];

		grp_nodes = qos_ptr_1->grp_tres_ctld[TRES_ARRAY_NODE];

		if (qos_ptr_2) {
			if (max_nodes_pa == INFINITE64)
				max_nodes_pa = qos_ptr_2->max_tres_pa_ctld[
					TRES_ARRAY_NODE];
			if (max_nodes_pj == INFINITE64)
				max_nodes_pj = qos_ptr_2->max_tres_pj_ctld[
					TRES_ARRAY_NODE];
			if (max_nodes_pu == INFINITE64)
				max_nodes_pu = qos_ptr_2->max_tres_pu_ctld[
					TRES_ARRAY_NODE];
			if (grp_nodes == INFINITE64)
				grp_nodes = qos_ptr_2->grp_tres_ctld[
					TRES_ARRAY_NODE];
		}

		if (max_nodes_pa < max_nodes_limit) {
			max_nodes_limit = max_nodes_pa;
			*wait_reason = WAIT_QOS_MAX_NODE_PER_ACCT;
		}

		if (max_nodes_pj < max_nodes_limit) {
			max_nodes_limit = max_nodes_pj;
			*wait_reason = WAIT_QOS_MAX_NODE_PER_JOB;
		}

		if (max_nodes_pu < max_nodes_limit) {
			max_nodes_limit = max_nodes_pu;
			*wait_reason = WAIT_QOS_MAX_NODE_PER_USER;
		}

		qos_max_p_limit = max_nodes_limit;

		if (grp_nodes < max_nodes_limit) {
			max_nodes_limit = grp_nodes;
			*wait_reason = WAIT_QOS_GRP_NODE;
		}
	}

	/* We have to traverse all the associations because QOS might
	   not override a particular limit.
	*/
	while (assoc_ptr) {
		if ((!qos_ptr_1 || (grp_nodes == INFINITE64))
		    && (assoc_ptr->grp_tres_ctld[TRES_ARRAY_NODE] != INFINITE64)
		    && (assoc_ptr->grp_tres_ctld[TRES_ARRAY_NODE] <
			max_nodes_limit)) {
			max_nodes_limit =
				assoc_ptr->grp_tres_ctld[TRES_ARRAY_NODE];
			*wait_reason = WAIT_ASSOC_GRP_NODE;
			grp_set = 1;
		}

		if (!parent
		    && (qos_max_p_limit == INFINITE64)
		    && (assoc_ptr->max_tres_ctld[TRES_ARRAY_NODE] != INFINITE64)
		    && (assoc_ptr->max_tres_ctld[TRES_ARRAY_NODE] <
			max_nodes_limit)) {
			max_nodes_limit =
				assoc_ptr->max_tres_ctld[TRES_ARRAY_NODE];
			*wait_reason = WAIT_ASSOC_MAX_NODE_PER_JOB;
		}

		/* only check the first grp set */
		if (grp_set)
			break;

		assoc_ptr = assoc_ptr->usage->parent_assoc_ptr;
		parent = 1;
		continue;
	}

	assoc_mgr_unlock(&locks);
	return max_nodes_limit;
}

/*
 * acct_policy_update_pending_job - Make sure the limits imposed on a job on
 *	submission are correct after an update to a qos or association.  If
 *	the association/qos limits prevent the job from running (lowered
 *	limits since job submission), then reset its reason field.
 */
extern int acct_policy_update_pending_job(struct job_record *job_ptr)
{
	job_desc_msg_t job_desc;
	acct_policy_limit_set_t acct_policy_limit_set;
	bool update_accounting = false;
	struct job_details *details_ptr;
	int rc = SLURM_SUCCESS;
	uint64_t tres_req_cnt[slurmctld_tres_cnt];

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

	/* copy the limits set from the job the only one that
	 * acct_policy_validate changes is the time limit so we
	 * should be ok with the memcpy here */
	memcpy(&acct_policy_limit_set, &job_ptr->limit_set,
	       sizeof(acct_policy_limit_set_t));
	job_desc.tres_req_cnt = tres_req_cnt;
	/* copy all the tres requests over */
	memcpy(job_desc.tres_req_cnt, job_ptr->tres_req_cnt,
	       sizeof(uint64_t) * slurmctld_tres_cnt);

	/* Only set this value if not set from a limit */
	if (job_ptr->limit_set.time == ADMIN_SET_LIMIT)
		acct_policy_limit_set.time = job_ptr->limit_set.time;
	else if ((job_ptr->time_limit != NO_VAL) && !job_ptr->limit_set.time)
		job_desc.time_limit = job_ptr->time_limit;

	if (!acct_policy_validate(&job_desc, job_ptr->part_ptr,
				  job_ptr->assoc_ptr, job_ptr->qos_ptr,
				  &job_ptr->state_reason,
				  &acct_policy_limit_set, 0)) {
		info("acct_policy_update_pending_job: exceeded "
		     "association/qos's cpu, node, memory or "
		     "time limit for job %d", job_ptr->job_id);
		return SLURM_ERROR;
	}

	/* The only variable in acct_policy_limit_set that is changed
	 * in acct_policy_validate is the time limit so only worry
	 * about that one.
	 */

	/* If it isn't an admin set limit replace it. */
	if (!acct_policy_limit_set.time && (job_ptr->limit_set.time == 1)) {
		job_ptr->time_limit = NO_VAL;
		job_ptr->limit_set.time = 0;
		update_accounting = true;
	} else if (acct_policy_limit_set.time != ADMIN_SET_LIMIT) {
		if (job_ptr->time_limit != job_desc.time_limit) {
			job_ptr->time_limit = job_desc.time_limit;
			update_accounting = true;
		}
		job_ptr->limit_set.time = acct_policy_limit_set.time;
	}

	if (update_accounting) {
		last_job_update = time(NULL);
		debug("limits changed for job %u: updating accounting",
		      job_ptr->job_id);
		/* Update job record in accounting to reflect changes */
		jobacct_storage_job_start_direct(acct_db_conn, job_ptr);
	}

	return rc;
}

/*
 * acct_policy_job_runnable - Determine if the specified job has timed
 *	out based on it's QOS or association.
 */
extern bool acct_policy_job_time_out(struct job_record *job_ptr)
{
	uint64_t job_tres_usage_mins[slurmctld_tres_cnt];
	uint64_t time_delta;
	uint64_t tres_usage_mins[slurmctld_tres_cnt];
	uint32_t wall_mins;
	slurmdb_qos_rec_t *qos_ptr_1, *qos_ptr_2;
	slurmdb_qos_rec_t qos_rec;
	slurmdb_assoc_rec_t *assoc = NULL;
	assoc_mgr_lock_t locks = { READ_LOCK, NO_LOCK, READ_LOCK, NO_LOCK,
				   READ_LOCK, NO_LOCK, NO_LOCK };
	time_t now;
	int i, tres_pos;

	/* Now see if we are enforcing limits.  If Safe is set then
	 * return false as well since we are being safe if the limit
	 * was changed after the job was already deemed safe to start.
	 */
	if (!(accounting_enforce & ACCOUNTING_ENFORCE_LIMITS)
	    || (accounting_enforce & ACCOUNTING_ENFORCE_SAFE))
		return false;

	slurmdb_init_qos_rec(&qos_rec, 0, INFINITE);
	assoc_mgr_lock(&locks);

	assoc_mgr_set_qos_tres_cnt(&qos_rec);

	_set_qos_order(job_ptr, &qos_ptr_1, &qos_ptr_2);

	assoc =	(slurmdb_assoc_rec_t *)job_ptr->assoc_ptr;

	now = time(NULL);

	time_delta = (uint64_t)(((now - job_ptr->start_time) -
				 job_ptr->tot_sus_time) / 60);

	/* clang needs this memset to avoid a warning */
	memset(job_tres_usage_mins, 0, sizeof(tres_usage_mins));
	memset(tres_usage_mins, 0, sizeof(tres_usage_mins));

	/* find out how many cpu minutes this job has been
	 * running for. We add 1 here to make it so we can check for
	 * just > instead of >= in our checks */
	for (i=0; i<slurmctld_tres_cnt; i++)
		if (job_ptr->tres_alloc_cnt[i])
			job_tres_usage_mins[i] =
				(time_delta * job_ptr->tres_alloc_cnt[i]) + 1;

	/* check the first QOS setting it's values in the qos_rec */
	if (qos_ptr_1 && !_qos_job_time_out(job_ptr, qos_ptr_1,
					    &qos_rec, job_tres_usage_mins))
		goto job_failed;

	/* If qos_ptr_1 didn't set the value use the 2nd QOS to set
	   the limit.
	*/
	if (qos_ptr_2 && !_qos_job_time_out(job_ptr, qos_ptr_2,
					    &qos_rec, job_tres_usage_mins))
		goto job_failed;

	/* handle any association stuff here */
	while (assoc) {
		for (i=0; i<slurmctld_tres_cnt; i++)
			tres_usage_mins[i] =
				(uint64_t)(assoc->usage->usage_tres_raw[i]
					   / 60.0);
		wall_mins = assoc->usage->grp_used_wall / 60;

		i = _validate_tres_usage_limits_for_assoc(
			&tres_pos, assoc->grp_tres_mins_ctld,
			qos_rec.grp_tres_mins_ctld, NULL,
			NULL, tres_usage_mins, NULL, 0);
		switch (i) {
		case 1:
			last_job_update = now;
			info("Job %u timed out, "
			     "the job is at or exceeds assoc %u(%s/%s/%s) "
			     "group max tres(%s) minutes of %"PRIu64
			     " with %"PRIu64"",
			     job_ptr->job_id,
			     assoc->id, assoc->acct,
			     assoc->user, assoc->partition,
			     assoc_mgr_tres_name_array[tres_pos],
			     assoc->grp_tres_mins_ctld[tres_pos],
			     tres_usage_mins[tres_pos]);
			job_ptr->state_reason = FAIL_TIMEOUT;
			goto job_failed;
			break;
		case 2:
			/* not possible safe_limits is 0 */
		case 3:
			/* not possible safe_limits is 0 */
		default:
			/* all good */
			break;
		}

		if ((qos_rec.grp_wall == INFINITE)
		    && (assoc->grp_wall != INFINITE)
		    && (wall_mins >= assoc->grp_wall)) {
			info("Job %u timed out, "
			     "assoc %u is at or exceeds "
			     "group wall limit %u "
			     "with %u for account %s",
			     job_ptr->job_id, assoc->id,
			     assoc->grp_wall,
			     wall_mins, assoc->acct);
			job_ptr->state_reason = FAIL_TIMEOUT;
			break;
		}

		i = _validate_tres_usage_limits_for_assoc(
			&tres_pos, assoc->max_tres_mins_ctld,
			qos_rec.max_tres_mins_pj_ctld, job_tres_usage_mins,
			NULL, NULL, NULL, 1);
		switch (i) {
		case 1:
			/* not possible curr_usage is NULL */
			break;
		case 2:
			last_job_update = now;
			info("Job %u timed out, "
			     "the job is at or exceeds assoc %u(%s/%s/%s) "
			     "max tres(%s) minutes of %"PRIu64
			     " with %"PRIu64,
			     job_ptr->job_id,
			     assoc->id, assoc->acct,
			     assoc->user, assoc->partition,
			     assoc_mgr_tres_name_array[tres_pos],
			     assoc->max_tres_mins_ctld[tres_pos],
			     job_tres_usage_mins[tres_pos]);
			job_ptr->state_reason = FAIL_TIMEOUT;
			goto job_failed;
			break;
		case 3:
			/* not possible tres_usage is NULL */
		default:
			/* all good */
			break;
		}

		assoc = assoc->usage->parent_assoc_ptr;
		/* these limits don't apply to the root assoc */
		if (assoc == assoc_mgr_root_assoc)
			break;
	}
job_failed:
	assoc_mgr_unlock(&locks);
	slurmdb_free_qos_rec_members(&qos_rec);

	if (job_ptr->state_reason == FAIL_TIMEOUT)
		return true;

	return false;
}
