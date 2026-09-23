/*****************************************************************************\
 *  job_resilience.h - adaptive job resilience on node failure
 *****************************************************************************
 *  Copyright (C) 2026 Gustavo Lima <gustcol@gmail.com>
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

#ifndef _SLURMCTLD_JOB_RESILIENCE_H
#define _SLURMCTLD_JOB_RESILIENCE_H

#include "src/common/job_record.h"
#include "src/common/node_conf.h"

/*
 * Percentage (0-100) of configured nodes that are currently up.
 * Caller must hold the node read lock.
 */
extern int job_resilience_cluster_health(void);

/*
 * True if job_ptr should be shrunk rather than killed or requeued when one
 * of its nodes fails: the ADAPTIVE_RESILIENCE flag is set, the job is a
 * running (not configuring) multi-node batch job that is not part of a
 * heterogeneous job, and the cluster health is at or above the threshold
 * from SchedulerParameters=resilience_min_health (default 70).
 * Caller must hold the job and node read locks.
 */
extern bool job_resilience_eligible(job_record_t *job_ptr);

/*
 * Remove node_ptr from the running job_ptr and keep the job running on the
 * remaining nodes. Records the original allocation the first time the job
 * shrinks and sets the ResilienceRecovery state reason.
 * Caller must hold the job and node write locks.
 * Returns SLURM_SUCCESS if the job was shrunk, SLURM_ERROR otherwise (the
 * caller should then fall back to the regular node failure handling).
 */
extern int job_resilience_shrink(job_record_t *job_ptr,
				 node_record_t *node_ptr);

/*
 * Called when a node returns to service. Updates the state description of
 * any resilience-shrunk job that lost this node.
 * Caller must hold the job and node write locks.
 */
extern void job_resilience_node_recovered(node_record_t *node_ptr);

/* Clear the resilience state of a job that is being requeued. */
extern void job_resilience_reset(job_record_t *job_ptr);

#endif
