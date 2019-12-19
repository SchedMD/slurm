/*****************************************************************************\
 *  preempt.c - Job preemption plugin function setup.
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include <pthread.h>
#include <signal.h>

#include "preempt.h"
#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/acct_policy.h"

static bool youngest_order = false;

typedef struct slurm_preempt_ops {
	bool		(*job_preempt_check)  (job_queue_rec_t *preemptor,
					       job_queue_rec_t *preemptee);
	bool		(*preemptable) (job_record_t *preemptor,
					job_record_t *preemptee);
	int		(*get_data) (job_record_t *job_ptr,
				     slurm_preempt_data_type_t data_type,
				     void *data);
} slurm_preempt_ops_t;

typedef struct {
	job_record_t *preemptor;
	List preemptee_job_list;
} preempt_candidates_t;

/*
 * Must be synchronized with slurm_preempt_ops_t above.
 */
static const char *syms[] = {
	"preempt_p_job_preempt_check",
	"preempt_p_preemptable",
	"preempt_p_get_data",
};

static slurm_preempt_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t	    g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

static int _is_job_preempt_exempt(job_record_t *preemptee_ptr,
				  job_record_t *preemptor_ptr)
{
	if (!IS_JOB_RUNNING(preemptee_ptr) && !IS_JOB_SUSPENDED(preemptee_ptr))
		return 1;

	if (job_borrow_from_resv_check(preemptee_ptr, preemptor_ptr)) {
		/*
		 * This job is on borrowed time from the reservation!
		 * Automatic preemption.
		 */
	} else if (!(*(ops.preemptable))(preemptee_ptr, preemptor_ptr))
		return 0;

	if (!preemptee_ptr->node_bitmap ||
	    !bit_overlap(preemptee_ptr->node_bitmap,
			 preemptor_ptr->part_ptr->node_bitmap))
		return 1;

	if (preemptor_ptr->details &&
	    (preemptor_ptr->details->expanding_jobid == preemptee_ptr->job_id))
		return 1;

	if (acct_policy_is_job_preempt_exempt(preemptee_ptr))
		return 1;

	return 0;
}

static int _add_preemptable_job(void *x, void *arg)
{
	job_record_t *candidate = (job_record_t *) x;
	preempt_candidates_t *candidates = (preempt_candidates_t *) arg;
	job_record_t *preemptor = candidates->preemptor;

	if (_is_job_preempt_exempt(candidate, preemptor))
		return 0;

	/* This job is a preemption candidate */
	if (!candidates->preemptee_job_list)
		candidates->preemptee_job_list = list_create(NULL);
	list_append(candidates->preemptee_job_list, candidate);

	return 0;
}

static int _sort_by_prio(void *x, void *y)
{
	int rc;
	uint32_t job_prio1, job_prio2;
	job_record_t *j1 = *(job_record_t **)x;
	job_record_t *j2 = *(job_record_t **)y;

	(void)(*(ops.get_data))(j1, PREEMPT_DATA_PRIO, &job_prio1);
	(void)(*(ops.get_data))(j2, PREEMPT_DATA_PRIO, &job_prio2);

	if (job_prio1 > job_prio2)
		rc = 1;
	else if (job_prio1 < job_prio2)
		rc = -1;
	else
		rc = 0;

	return rc;
}

static int _sort_by_youngest(void *x, void *y)
{
	int rc;
	job_record_t *j1 = *(job_record_t **) x;
	job_record_t *j2 = *(job_record_t **) y;

	if (j1->start_time < j2->start_time)
		rc = 1;
	else if (j1->start_time > j2->start_time)
		rc = -1;
	else
		rc = 0;

	return rc;
}

extern int slurm_preempt_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "preempt";
	char *type = NULL;
	char *sched_params;

	/* This function is called frequently, so it should be as fast as
	 * possible. The test below will be true almost all of the time and
	 * is as fast as possible. */
	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_preempt_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

	sched_params = slurm_get_sched_params();
	if (xstrcasestr(sched_params, "preempt_youngest_first"))
		youngest_order = true;
	xfree(sched_params);

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);
	return retval;
}

extern int slurm_preempt_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

extern List slurm_find_preemptable_jobs(job_record_t *job_ptr)
{
	preempt_candidates_t candidates
		= { .preemptor = job_ptr, .preemptee_job_list = NULL };

	/* Validate the preemptor job */
	if (job_ptr == NULL) {
		error("%s: job_ptr is NULL", __func__);
		return NULL;
	}
	if (!IS_JOB_PENDING(job_ptr)) {
		error("%s: %pJ not pending", __func__, job_ptr);
		return NULL;
	}
	if (job_ptr->part_ptr == NULL) {
		error("%s: %pJ has NULL partition ptr", __func__, job_ptr);
		return NULL;
	}
	if (job_ptr->part_ptr->node_bitmap == NULL) {
		error("%s: partition %s node_bitmap=NULL",
		      __func__, job_ptr->part_ptr->name);
		return NULL;
	}

	/* Build an array of pointers to preemption candidates */
	list_for_each(job_list, _add_preemptable_job, &candidates);

	if (candidates.preemptee_job_list && youngest_order)
		list_sort(candidates.preemptee_job_list, _sort_by_youngest);
	else if (candidates.preemptee_job_list)
		list_sort(candidates.preemptee_job_list, _sort_by_prio);

	return candidates.preemptee_job_list;
}

/*
 * Return the PreemptMode which should apply to stop this job
 */
extern uint16_t slurm_job_preempt_mode(job_record_t *job_ptr)
{
	uint16_t data = (uint16_t)PREEMPT_MODE_OFF;

	if ((slurm_preempt_init() < 0) ||
	    ((*(ops.get_data))(job_ptr, PREEMPT_DATA_MODE, &data) !=
	     SLURM_SUCCESS))
		return data;

	return data;
}

/*
 * Return true if any jobs can be preempted, otherwise false
 */
extern bool slurm_preemption_enabled(void)
{
	bool data = false;

	if ((slurm_preempt_init() < 0) ||
	    ((*(ops.get_data))(NULL, PREEMPT_DATA_ENABLED, &data) !=
	     SLURM_SUCCESS))
		return data;

	return data;
}

/*
 * Return the grace time for job
 */
extern uint32_t slurm_job_get_grace_time(job_record_t *job_ptr)
{
	uint32_t data = 0;

	if ((slurm_preempt_init() < 0) ||
	    ((*(ops.get_data))(job_ptr, PREEMPT_DATA_GRACE_TIME, &data) !=
	     SLURM_SUCCESS))
		return data;

	return data;
}

/*
 * Return true if the preemptor can preempt the preemptee, otherwise false
 */
extern bool preempt_g_job_preempt_check(job_queue_rec_t *preemptor,
					job_queue_rec_t *preemptee)
{
	if (slurm_preempt_init() < 0)
		return false;

	return (*(ops.job_preempt_check))(preemptor, preemptee);
}

extern bool preempt_g_preemptable(
	job_record_t *preemptee, job_record_t *preemptor)
{
	if (slurm_preempt_init() < 0)
		return false;

	return (*(ops.preemptable))(preemptor, preemptee);
}

extern int preempt_g_get_data(job_record_t *job_ptr,
			      slurm_preempt_data_type_t data_type,
			      void *data)
{
	if (slurm_preempt_init() < 0)
		return SLURM_ERROR;

	return (*(ops.get_data))(job_ptr, data_type, data);
}
