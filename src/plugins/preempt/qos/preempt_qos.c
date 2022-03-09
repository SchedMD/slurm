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
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/acct_policy.h"

const char	plugin_name[]	= "Preempt by Quality Of Service (QOS)";
const char	plugin_type[]	= "preempt/qos";
const uint32_t	plugin_version	= SLURM_VERSION_NUMBER;

extern bool preempt_p_preemptable(
	job_record_t *preemptee, job_record_t *preemptor);

static uint16_t _job_preempt_mode(job_record_t *job_ptr)
{
	uint16_t mode;

	if (job_ptr->qos_ptr && job_ptr->qos_ptr->preempt_mode)
		mode = job_ptr->qos_ptr->preempt_mode;
	else
		mode = slurm_conf.preempt_mode;

	mode &= ~PREEMPT_MODE_GANG;
	mode &= ~PREEMPT_MODE_WITHIN;

	return mode;
}

/* Generate the job's priority. It is partly based upon the QOS priority
 * and partly based upon the job size. We want to put smaller jobs at the top
 * of the preemption queue and use a sort algorithm to minimize the number of
 * job's preempted. */
static uint32_t _gen_job_prio(job_record_t *job_ptr)
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

/* Return grace_time for job */
static uint32_t _get_grace_time(job_record_t *job_ptr)
{
	if (!job_ptr->qos_ptr)
		return 0;

        return job_ptr->qos_ptr->grace_time;
}

extern int init(void)
{
	verbose("%s loaded", plugin_type);
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	/* Empty. */
}

/* Return true if the preemptor can preempt the preemptee, otherwise false */
extern bool preempt_p_job_preempt_check(job_queue_rec_t *preemptor,
					job_queue_rec_t *preemptee)
{
	return preempt_p_preemptable(preemptee->job_ptr, preemptor->job_ptr);
}

extern bool preempt_p_preemptable(
	job_record_t *preemptee, job_record_t *preemptor)
{
	slurmdb_qos_rec_t *qos_ee = preemptee->qos_ptr;
	slurmdb_qos_rec_t *qos_or = preemptor->qos_ptr;

	if (!qos_ee || !qos_or) {
		return false;
	} else if (qos_or->id == qos_ee->id) {
		if ((qos_or->preempt_mode & PREEMPT_MODE_WITHIN) ||
		    (slurm_conf.preempt_mode & PREEMPT_MODE_WITHIN))
			return (preemptor->priority > preemptee->priority);

		return false;
	} else if (!qos_or->preempt_bitstr ||
		   !bit_test(qos_or->preempt_bitstr, qos_ee->id)) {
		return false;
	}

	return true;
}

extern int preempt_p_get_data(job_record_t *job_ptr,
			      slurm_preempt_data_type_t data_type,
			      void *data)
{
	int rc = SLURM_SUCCESS;

	switch (data_type) {
	case PREEMPT_DATA_ENABLED:
		(*(bool *)data) = slurm_conf.preempt_mode != PREEMPT_MODE_OFF;
		break;
	case PREEMPT_DATA_MODE:
		(*(uint16_t *)data) = _job_preempt_mode(job_ptr);
		break;
	case PREEMPT_DATA_PRIO:
		(*(uint32_t *)data) = _gen_job_prio(job_ptr);
		break;
	case PREEMPT_DATA_GRACE_TIME:
		(*(uint32_t *)data) = _get_grace_time(job_ptr);
		break;
	default:
		error("%s: unknown enum %d", __func__, data_type);
		rc = SLURM_ERROR;
		break;

	}
	return rc;
}
