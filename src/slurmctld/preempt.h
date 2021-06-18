/*****************************************************************************\
 *  preempt.h - Define job preemption plugin functions.
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

#ifndef __SLURM_CONTROLLER_PREEMPT_H__
#define __SLURM_CONTROLLER_PREEMPT_H__

#include "slurm/slurm.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"

typedef enum {
	PREEMPT_DATA_ENABLED,
	PREEMPT_DATA_MODE,
	PREEMPT_DATA_PRIO,
	PREEMPT_DATA_GRACE_TIME,
} slurm_preempt_data_type_t;

/*
 * Initialize the preemption plugin.
 *
 * Returns a Slurm errno.
 */
int slurm_preempt_init(void);

/*
 * Terminate the preemption plugin.
 *
 * Returns a Slurm errno.
 */
extern int slurm_preempt_fini(void);

/*
 * slurm_find_preemptable_jobs - Given a pointer to a pending job, return list
 *	of pointers to preemptable jobs. The jobs should be sorted in order
 *	from most desirable to least desirable to preempt.
 * NOTE: Returns NULL if no preemptable jobs are found.
 * NOTE: Caller must list_destroy() any list returned.
 */
extern List slurm_find_preemptable_jobs(job_record_t *job_ptr);

/*
 * Return the PreemptMode which should apply to stop this job
 */
extern uint16_t slurm_job_preempt_mode(job_record_t *job_ptr);

/*
 * Return true if any jobs can be preempted, otherwise false
 */
extern bool slurm_preemption_enabled(void);

/* Returns 0 if grace time isn't found */
extern uint32_t slurm_job_get_grace_time(job_record_t *job_ptr);

extern uint32_t slurm_job_preempt(job_record_t *job_ptr,
				  job_record_t *preemptor_ptr,
				  uint16_t mode, bool ignore_time);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Return true if the preemptor can preempt the preemptee, otherwise false
 * This requires the part_ptr of both job_queue_rec_t to be set correctly.
 * It does not require the job_record_t to have the correct part_ptr set.
 */
extern bool preempt_g_job_preempt_check(job_queue_rec_t *preemptor,
					job_queue_rec_t *preemptee);

/*
 * Return true if the preemptor can preempt the preemptee, otherwise false
 * This requires the part_ptr of both job_record_t to be set correctly.
 */
extern bool preempt_g_preemptable(
	job_record_t *preemptee, job_record_t *preemptor);

/*
 * Get various preemption variables from the plugin.
 * job_ptr - Pointer to job to use.
 * PREEMPT_DATA_ENABLED - Set 'data' to a boolean if preemption is enabled or
 *                        not.
 * PREEMPT_DATA_MODE - Set 'data' to uint16_t on which PreemptMode which should
 *                     apply to stop this job.
 * PREEMPT_DATA_PRIO - Set 'data' to a uint32_t of what the priority should be
 *                     for the given job.
 * PREEMPT_DATA_GRACE_TIME - Set 'data' to a uint32_t of what grace_time should
 *                           be for the given job.
 */
extern int preempt_g_get_data(job_record_t *job_ptr,
			      slurm_preempt_data_type_t data_type,
			      void *data);

#endif /*__SLURM_CONTROLLER_PREEMPT_H__*/
