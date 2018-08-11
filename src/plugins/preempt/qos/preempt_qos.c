/*****************************************************************************\
 *  preempt_qos.c - job preemption plugin that selects preemptable
 *  jobs based upon their Quality Of Service (QOS).
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris jette <jette1@llnl.gov>
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

#include <stdio.h>

#include "slurm/slurm_errno.h"

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/plugin.h"
#include "src/common/xstring.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"

const char	plugin_name[]	= "Preempt by Quality Of Service (QOS)";
const char	plugin_type[]	= "preempt/qos";
const uint32_t	plugin_version	= SLURM_VERSION_NUMBER;

static uint32_t _gen_job_prio(struct job_record *job_ptr);
static bool _qos_preemptable(struct job_record *preemptee,
			     struct job_record *preemptor);
static int _sort_by_prio (void *x, void *y);
static int _sort_by_youngest(void *x, void *y);

static bool youngest_order = false;

extern int init(void)
{
	char *sched_params;
	verbose("preempt/qos loaded");
	sched_params = slurm_get_sched_params();
	if (xstrcasestr(sched_params, "preempt_youngest_first"))
		youngest_order = true;
	xfree(sched_params);
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	/* Empty. */
}

extern List find_preemptable_jobs(struct job_record *job_ptr)
{
	ListIterator job_iterator;
	struct job_record *job_p;
	List preemptee_job_list = NULL;

	/* Validate the preemptor job */
	if (job_ptr == NULL) {
		error("find_preemptable_jobs: job_ptr is NULL");
		return preemptee_job_list;
	}
	if (!IS_JOB_PENDING(job_ptr)) {
		error("%s: %pJ not pending", __func__, job_ptr);
		return preemptee_job_list;
	}
	if (job_ptr->part_ptr == NULL) {
		error("%s: %pJ has NULL partition ptr", __func__, job_ptr);
		return preemptee_job_list;
	}
	if (job_ptr->part_ptr->node_bitmap == NULL) {
		error("find_preemptable_jobs: partition %s node_bitmap=NULL",
		      job_ptr->part_ptr->name);
		return preemptee_job_list;
	}

	/* Build an array of pointers to preemption candidates */
	job_iterator = list_iterator_create(job_list);
	while ((job_p = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_p) && !IS_JOB_SUSPENDED(job_p))
			continue;
		if (!_qos_preemptable(job_p, job_ptr))
			continue;
		if ((job_p->node_bitmap == NULL) ||
		    (bit_overlap(job_p->node_bitmap,
				 job_ptr->part_ptr->node_bitmap) == 0))
			continue;
		if (job_ptr->details &&
		    (job_ptr->details->expanding_jobid == job_p->job_id))
			continue;

		/* This job is a preemption candidate */
		if (preemptee_job_list == NULL) {
			preemptee_job_list = list_create(NULL);
		}
		list_append(preemptee_job_list, job_p);
	}
	list_iterator_destroy(job_iterator);

	if (preemptee_job_list && youngest_order)
		list_sort(preemptee_job_list, _sort_by_youngest);
	else if (preemptee_job_list)
		list_sort(preemptee_job_list, _sort_by_prio);

	return preemptee_job_list;
}

static bool _qos_preemptable(struct job_record *preemptee,
			     struct job_record *preemptor)
{
	slurmdb_qos_rec_t *qos_ee = preemptee->qos_ptr;
	slurmdb_qos_rec_t *qos_or = preemptor->qos_ptr;

	if ((qos_ee == NULL) || (qos_or == NULL) ||
	    (qos_or->id == qos_ee->id) ||
	    (qos_or->preempt_bitstr == NULL) ||
	    !bit_test(qos_or->preempt_bitstr, qos_ee->id))
		return false;
	return true;

}

/* Generate the job's priority. It is partly based upon the QOS priority
 * and partly based upon the job size. We want to put smaller jobs at the top
 * of the preemption queue and use a sort algorithm to minimize the number of
 * job's preempted. */
static uint32_t _gen_job_prio(struct job_record *job_ptr)
{
	uint32_t job_prio = 0;
	slurmdb_qos_rec_t *qos_ptr = job_ptr->qos_ptr;

	if (qos_ptr) {
		/* QOS priority is 32-bits, but only use 16-bits so we can
		 * preempt smaller jobs rather than larger jobs. */
		if (qos_ptr->priority >= 0xffff)
			job_prio = 0xffff << 16;
		else
			job_prio = qos_ptr->priority << 16;
	}

	if (job_ptr->node_cnt >= 0xffff)
		job_prio += 0xffff;
	else
		job_prio += job_ptr->node_cnt;

	return job_prio;
}

static int _sort_by_prio(void *x, void *y)
{
	int rc;
	uint32_t job_prio1, job_prio2;
	struct job_record *j1 = *(struct job_record **)x;
	struct job_record *j2 = *(struct job_record **)y;

	job_prio1 = _gen_job_prio(j1);
	job_prio2 = _gen_job_prio(j2);

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
	struct job_record *j1 = *(struct job_record **) x;
	struct job_record *j2 = *(struct job_record **) y;

	if (j1->start_time < j2->start_time)
		rc = 1;
	else if (j1->start_time > j2->start_time)
		rc = -1;
	else
		rc = 0;

	return rc;
}

extern uint16_t job_preempt_mode(struct job_record *job_ptr)
{
	if (job_ptr->qos_ptr && job_ptr->qos_ptr->preempt_mode)
		return job_ptr->qos_ptr->preempt_mode;

	return (slurm_get_preempt_mode() & (~PREEMPT_MODE_GANG));
}

extern bool preemption_enabled(void)
{
	return (slurm_get_preempt_mode() != PREEMPT_MODE_OFF);
}

/* Return true if the preemptor can preempt the preemptee, otherwise false */
extern bool job_preempt_check(job_queue_rec_t *preemptor,
			      job_queue_rec_t *preemptee)
{
	return _qos_preemptable(preemptee->job_ptr, preemptor->job_ptr);
}
