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
#include "src/common/proc_args.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/acct_policy.h"

static bool youngest_order = false;
static uint32_t min_exempt_priority = NO_VAL;

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

static int _is_job_preempt_exempt_internal(void *x, void *key)
{
	job_record_t *preemptee_ptr = (job_record_t *)x;
	job_record_t *preemptor_ptr = (job_record_t *)key;

	if (job_borrow_from_resv_check(preemptee_ptr, preemptor_ptr)) {
		/*
		 * This job is on borrowed time from the reservation!
		 * Automatic preemption.
		 */
	} else if (!(*(ops.preemptable))(preemptee_ptr, preemptor_ptr))
		return 1;

	if (min_exempt_priority < preemptee_ptr->priority)
		return 1;

	if (preemptor_ptr->details &&
	    (preemptor_ptr->details->expanding_jobid == preemptee_ptr->job_id))
		return 1;

	if (acct_policy_is_job_preempt_exempt(preemptee_ptr))
		return 1;

	return 0;
}

static bool _is_job_preempt_exempt(job_record_t *preemptee_ptr,
				  job_record_t *preemptor_ptr)
{
	xassert(preemptee_ptr);
	xassert(preemptor_ptr);

	if (!preemptee_ptr->het_job_list)
		return _is_job_preempt_exempt_internal(
			preemptee_ptr, preemptor_ptr);
	/*
	 * All components of a job must be preemptable otherwise it is
	 * preempt exempt
	 */
        return list_find_first(preemptee_ptr->het_job_list,
			       _is_job_preempt_exempt_internal,
			       preemptor_ptr) ? true : false;
}

/*
 * Return the PreemptMode which should apply to stop this job
 */
static uint16_t _job_preempt_mode_internal(job_record_t *job_ptr)
{
	uint16_t data = (uint16_t)PREEMPT_MODE_OFF;

	xassert(g_context);

	if ((*(ops.get_data))(job_ptr, PREEMPT_DATA_MODE, &data) !=
	    SLURM_SUCCESS)
		return data;

	return data;
}

static int _find_job_by_preempt_mode(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)x;
	uint16_t preempt_mode = *(uint16_t *)arg;

	if (_job_preempt_mode_internal(job_ptr) == preempt_mode)
		return 1;

	return 0;
}

static int _add_preemptable_job(void *x, void *arg)
{
	job_record_t *candidate = (job_record_t *) x;
	preempt_candidates_t *candidates = (preempt_candidates_t *) arg;
	job_record_t *preemptor = candidates->preemptor;

	/*
	 * We only want to look at the master component of a hetjob.  Since all
	 * components have to be preemptable it should be here at some point.
	 */
	if (candidate->het_job_id && !candidate->het_job_list)
		return 0;

	if (_is_job_preempt_exempt(candidate, preemptor))
		return 0;
	/*
	 * We have to check the entire bitmap space here before we can check
	 * each part of a hetjob in _is_job_preempt_exempt()
	 */
	if (!job_overlap_and_running(preemptor->part_ptr->node_bitmap,
				     preemptor->license_list, candidate))
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
	char *plugin_type = "preempt", *temp_str;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	g_context = plugin_context_create(
		plugin_type, slurm_conf.preempt_type,
		(void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type,
		      slurm_conf.preempt_type);
		retval = SLURM_ERROR;
		goto done;
	}

	youngest_order = false;
	if (xstrcasestr(slurm_conf.preempt_params, "youngest_first") ||
	    xstrcasestr(slurm_conf.sched_params, "preempt_youngest_first"))
		youngest_order = true;

	min_exempt_priority = NO_VAL;
	if ((temp_str = xstrcasestr(slurm_conf.preempt_params,
				    "min_exempt_priority=")))
		retval = parse_uint32((temp_str + 20), &min_exempt_priority);

done:
	slurm_mutex_unlock(&g_context_lock);
	return retval;
}

extern int slurm_preempt_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

extern List slurm_find_preemptable_jobs(job_record_t *job_ptr)
{
	preempt_candidates_t candidates	= { .preemptor = job_ptr };

	/* Validate the preemptor job */
	if (!job_ptr) {
		error("%s: job_ptr is NULL", __func__);
		return NULL;
	}
	if (!IS_JOB_PENDING(job_ptr)) {
		error("%s: %pJ not pending", __func__, job_ptr);
		return NULL;
	}
	if (!job_ptr->part_ptr) {
		error("%s: %pJ has NULL partition ptr", __func__, job_ptr);
		return NULL;
	}
	if (!job_ptr->part_ptr->node_bitmap) {
		error("%s: partition %s node_bitmap=NULL",
		      __func__, job_ptr->part_ptr->name);
		return NULL;
	}

	/* Build an array of pointers to preemption candidates */
	if (slurm_preemption_enabled() ||
	    job_uses_max_start_delay_resv(job_ptr))
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
	uint16_t data;

	if (job_ptr->het_job_list && !job_ptr->job_preempt_comp) {
		/*
		 * Find the component job to use as the template for
		 * setting the preempt mode for all other components.
		 * The first component job found having a preempt mode
		 * in the hierarchy (ordered highest to lowest:
		 * SUSPEND->REQUEUE->CANCEL) will be used as
		 * the template.
		 *
		 * NOTE: CANCEL is not on the list below since it is handled
		 * as the default.
		 */
		static const uint16_t preempt_modes[] = {
			PREEMPT_MODE_SUSPEND,
			PREEMPT_MODE_REQUEUE
		};
		static const int preempt_modes_cnt = sizeof(preempt_modes) /
			sizeof(preempt_modes[0]);

		for (int pm_index = 0; pm_index < preempt_modes_cnt;
		     pm_index++) {
			data = preempt_modes[pm_index];
			if ((job_ptr->job_preempt_comp = list_find_first(
				     job_ptr->het_job_list,
				     _find_job_by_preempt_mode,
				     &data)))
				break;
		}
		/* if not found look up the mode (CANCEL expected) */
		if (!job_ptr->job_preempt_comp)
			data = _job_preempt_mode_internal(job_ptr);
	} else
		data = _job_preempt_mode_internal(job_ptr->job_preempt_comp ?
						  job_ptr->job_preempt_comp :
						  job_ptr);

	return data;
}

/*
 * Return true if any jobs can be preempted, otherwise false
 */
extern bool slurm_preemption_enabled(void)
{
	bool data = false;

	xassert(g_context);

	if ((*(ops.get_data))(NULL, PREEMPT_DATA_ENABLED, &data) !=
	    SLURM_SUCCESS)
		return data;

	return data;
}

/*
 * Return the grace time for job
 */
extern uint32_t slurm_job_get_grace_time(job_record_t *job_ptr)
{
	uint32_t data = 0;

	xassert(g_context);

	if ((*(ops.get_data))(job_ptr, PREEMPT_DATA_GRACE_TIME, &data) !=
	    SLURM_SUCCESS)
		return data;

	return data;
}

/*
 * Check to see if a job is in a grace time.
 * If no grace_time active then return 1.
 * If grace_time is currently active then return -1.
 */
static int _job_check_grace_internal(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)x;
	job_record_t *preemptor_ptr = (job_record_t *)arg;

	int rc = -1;
	uint32_t grace_time = 0;

	if (job_ptr->preempt_time) {
		if (time(NULL) >= job_ptr->end_time) {
			job_ptr->preempt_time = time(NULL);
			rc = 1;
		}
		return rc;
	}

	xassert(preemptor_ptr);

	/*
	 * If this job is running in parts of a reservation
	 */
	if (job_borrow_from_resv_check(job_ptr, preemptor_ptr))
		grace_time = job_ptr->warn_time;
	else
		grace_time = slurm_job_get_grace_time(job_ptr);

	job_ptr->preempt_time = time(NULL);
	job_ptr->end_time = MIN(job_ptr->end_time,
				(job_ptr->preempt_time + (time_t)grace_time));
	if (grace_time) {
		debug("setting %u sec preemption grace time for %pJ to reclaim resources for %pJ",
		      grace_time, job_ptr, preemptor_ptr);
		/* send job warn signal always sends SIGCONT first */
		if (preempt_send_user_signal && job_ptr->warn_signal &&
		    !(job_ptr->warn_flags & WARN_SENT))
			send_job_warn_signal(job_ptr, true);
		else {
			job_signal(job_ptr, SIGCONT, 0, 0, 0);
			job_signal(job_ptr, SIGTERM, 0, 0, 0);
		}
	} else
		rc = 1;

	return rc;
}

/*
 * Check to see if a job (or hetjob) is in a grace time.
 * If no grace_time active then return 0.
 * If grace_time is currently active then return 1.
 */
static int _job_check_grace(job_record_t *job_ptr, job_record_t *preemptor_ptr)
{
	if (job_ptr->het_job_list)
		return list_for_each_nobreak(job_ptr->het_job_list,
					     _job_check_grace_internal,
					     preemptor_ptr) <= 0 ? 1 : 0;

	return _job_check_grace_internal(job_ptr, preemptor_ptr) < 0 ? 1 : 0;
}

static int _job_warn_signal_wrapper(void *x, void *arg)
{
	job_record_t *job_ptr = (job_record_t *)x;
	bool ignore_time = *(bool *)arg;

	/* Ignore Time is always true */
	send_job_warn_signal(job_ptr, ignore_time);

	return 0;
}

extern uint32_t slurm_job_preempt(job_record_t *job_ptr,
				  job_record_t *preemptor_ptr,
				  uint16_t mode, bool ignore_time)
{
	int rc = SLURM_ERROR;
	/* If any job is in a grace period continue */
	if (_job_check_grace(job_ptr, preemptor_ptr))
		return SLURM_ERROR;

	if (preempt_send_user_signal) {
		if (job_ptr->het_job_list)
			(void)list_for_each(job_ptr->het_job_list,
					    _job_warn_signal_wrapper,
					    &ignore_time);
		else
			send_job_warn_signal(job_ptr, ignore_time);
	}

	if (mode == PREEMPT_MODE_CANCEL) {
		if (job_ptr->het_job_list)
			rc = het_job_signal(job_ptr, SIGKILL, 0, 0, true);
		else
			rc = job_signal(job_ptr, SIGKILL, 0, 0, true);
		if (rc == SLURM_SUCCESS) {
			info("preempted %pJ has been killed to reclaim resources for %pJ",
			     job_ptr, preemptor_ptr);
		}
	} else if (mode == PREEMPT_MODE_REQUEUE) {
		/* job_requeue already handles het jobs */
		rc = job_requeue(0, job_ptr->job_id,
				 NULL, true, 0);
		if (rc == SLURM_SUCCESS) {
			info("preempted %pJ has been requeued to reclaim resources for %pJ",
			     job_ptr, preemptor_ptr);
		}
	}

	if (rc != SLURM_SUCCESS) {
		if (job_ptr->het_job_list)
			rc = het_job_signal(job_ptr, SIGKILL, 0, 0, true);
		else
			rc = job_signal(job_ptr, SIGKILL, 0, 0, true);
		if (rc == SLURM_SUCCESS) {
			info("%s: preempted %pJ had to be killed",
			     __func__, job_ptr);
		} else {
			info("%s: preempted %pJ kill failure %s",
			     __func__, job_ptr, slurm_strerror(rc));
		}
	}

	return rc;
}
/*
 * Return true if the preemptor can preempt the preemptee, otherwise false
 */
extern bool preempt_g_job_preempt_check(job_queue_rec_t *preemptor,
					job_queue_rec_t *preemptee)
{
	xassert(g_context);

	return (*(ops.job_preempt_check))(preemptor, preemptee);
}

extern bool preempt_g_preemptable(
	job_record_t *preemptee, job_record_t *preemptor)
{
	xassert(g_context);

	return (*(ops.preemptable))(preemptor, preemptee);
}

extern int preempt_g_get_data(job_record_t *job_ptr,
			      slurm_preempt_data_type_t data_type,
			      void *data)
{
	xassert(g_context);

	return (*(ops.get_data))(job_ptr, data_type, data);
}
