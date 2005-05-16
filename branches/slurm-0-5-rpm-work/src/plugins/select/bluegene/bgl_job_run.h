/*****************************************************************************\
 *  bgl_job_run.h - header for blue gene job execution (e.g. initiation and
 *  termination) functions. 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifndef _BGL_JOB_RUN_H_
#define _BGL_JOB_RUN_H_

#include "src/slurmctld/slurmctld.h"

/*
 * Perform any setup required to initiate a job
 * job_ptr IN - pointer to the job being initiated
 * RET - SLURM_SUCCESS or an error code 
 *
 * NOTE: This happens in parallel with srun and slurmd spawning 
 * the job. A prolog script is expected to defer initiation of 
 * the job script until the BGL block is available for use.
 */
extern int start_job(struct job_record *job_ptr);

/* 
 * Perform any work required to terminate a job
 * job_ptr IN - pointer to the job being terminated
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens in parallel with srun and slurmd terminating
 * the job. Insure that this function, mpirun and the epilog can 
 * all deal with termination race conditions.
 */
extern int term_job(struct job_record *job_ptr);

/*
 * Perform any work required to terminate a jobs on a partition
 * bgl_part_id IN - partition name
 * RET - SLURM_SUCCESS or an error code
 *
 * NOTE: This happens when new partitions are created and we
 * need to clean up jobs on them.
 */
extern int term_jobs_on_part(pm_partition_id_t bgl_part_id);

/*
 * Synchronize BGL block state to that of currently active jobs.
 * This can recover from slurmctld crashes when partition ownership 
 * changes were queued
 */
extern int sync_jobs(List job_list);

/*
 * Boot a partition. Partition state expected to be FREE upon entry. 
 * NOTE: This function does not wait for the boot to complete.
 * the slurm prolog script needs to perform the waiting.
 */
extern int boot_part(bgl_record_t *bgl_record, rm_partition_mode_t node_use);
#endif /* _BGL_JOB_RUN_H_ */
