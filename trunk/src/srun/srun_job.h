/*****************************************************************************\
 *  src/srun/srun_job.h - specification of an srun "job"
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#ifndef _HAVE_JOB_H
#define _HAVE_JOB_H

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif

#include <netinet/in.h>

#include <slurm/slurm.h>

#include "src/common/eio.h"
#include "src/common/cbuf.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/dist_tasks.h"
#include "src/common/global_srun.h"
#include "src/api/step_io.h"

#include "src/srun/fname.h"

extern int message_thread;

void    update_job_state(srun_job_t *job, srun_job_state_t newstate);
void    job_force_termination(srun_job_t *job);

srun_job_state_t job_state(srun_job_t *job);

extern srun_job_t * job_create_noalloc(void);
extern srun_job_t * job_create_allocation(
	resource_allocation_response_msg_t *resp);
extern srun_job_t * job_create_structure(
	resource_allocation_response_msg_t *resp);

/*
 *  Update job filenames and modes for stderr, stdout, and stdin.
 */
void    job_update_io_fnames(srun_job_t *j);

/* 
 * Issue a fatal error message and terminate running job
 */
void    job_fatal(srun_job_t *job, const char *msg);

/* 
 * Deallocates job and or job step via slurm API
 */
void    srun_job_destroy(srun_job_t *job, int error);

/* 
 * Send SIGKILL to running job via slurm controller
 */
void    srun_job_kill(srun_job_t *job);

/*
 * report current task status
 */
void    report_task_status(srun_job_t *job);

/*
 * report current node status
 */
void    report_job_status(srun_job_t *job);

/*
 * Returns job return code (for srun exit status)
 */
int    job_rc(srun_job_t *job);

/*
 * To run a job step on existing allocation, modify the 
 * existing_allocation() response to remove nodes as needed 
 * for the job step request (for --excluded nodes or reduced 
 * --nodes count). This is a temporary fix for slurm 0.2.
 * resp IN/OUT - existing_allocation() response message
 * RET - zero or fatal error code
 */

int    job_resp_hack_for_step(resource_allocation_response_msg_t *resp);

#endif /* !_HAVE_JOB_H */
