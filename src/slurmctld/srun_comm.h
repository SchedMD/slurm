/*****************************************************************************\
 *  srun_comm.h - definitions srun communications
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov> et. al.
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

#ifndef _HAVE_SRUN_COMM_H
#define _HAVE_SRUN_COMM_H

#include <sys/types.h>
#include <time.h>

#include "src/slurmctld/slurmctld.h"

/*
 * srun_allocate - notify srun of a resource allocation
 * IN job_ptr - job allocated resources
 */
extern void srun_allocate(job_record_t *job_ptr);

/*
 * srun_allocate_abort - notify srun of a resource allocation failure
 * IN job_ptr - job allocated resources
 */
extern void srun_allocate_abort(job_record_t *job_ptr);

/*
 * srun_job_complete - notify srun of a job's termination
 * IN job_ptr - pointer to the slurmctld job record
 */
extern void srun_job_complete(job_record_t *job_ptr);


/*
 * srun_job_suspend - notify salloc of suspend/resume operation
 * IN job_ptr - pointer to the slurmctld job record
 * IN op - SUSPEND_JOB or RESUME_JOB (enum suspend_opts from slurm.h)
 * RET - true if message send, otherwise false
 */
extern bool srun_job_suspend(job_record_t *job_ptr, uint16_t op);

/*
 * srun_step_complete - notify srun of a job step's termination
 * IN step_ptr - pointer to the slurmctld job step record
 */
extern void srun_step_complete(step_record_t *step_ptr);

/*
 * srun_step_missing - notify srun that a job step is missing from
 *		       a node we expect to find it on
 * IN step_ptr  - pointer to the slurmctld job step record
 * IN node_list - name of nodes we did not find the step on
 */
extern void srun_step_missing(step_record_t *step_ptr, char *node_list);

/*
 * srun_step_signal - notify srun that a job step should be signaled
 * NOTE: Needed on BlueGene/Q to signal runjob process
 * IN step_ptr  - pointer to the slurmctld job step record
 * IN signal - signal number
 */
extern void srun_step_signal(step_record_t *step_ptr, uint16_t signal);

/*
 * srun_node_fail - notify srun of a node's failure
 * IN job_ptr - job to notify
 * IN node_name - name of failed node
 */
extern void srun_node_fail(job_record_t *job_ptr, char *node_name);

/*
 * srun_ping - Ping all allocations srun/salloc that have not been heard from
 * recently. This does not ping sruns inside a allocation from sbatch or salloc.
 */
extern void srun_ping (void);

/*
 * srun_response - note that srun has responded
 * IN step_id - id of step responding or NO_VAL if not a step
 */
extern void srun_response(slurm_step_id_t *step_id);

/*
 * srun_timeout - notify srun of a job's timeout
 * IN job_ptr - pointer to the slurmctld job record
 */
extern void srun_timeout(job_record_t *job_ptr);

/*
 * srun_user_message - Send arbitrary message to an srun job (no job steps)
 */
extern int srun_user_message(job_record_t *job_ptr, char *msg);

#endif /* !_HAVE_SRUN_COMM_H */
