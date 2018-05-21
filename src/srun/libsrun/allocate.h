/*****************************************************************************\
 * src/srun/allocate.h - node allocation functions for srun
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#ifndef _HAVE_ALLOCATE_H
#define _HAVE_ALLOCATE_H

#include "slurm/slurm.h"

#include "src/srun/libsrun/opt.h"
#include "src/srun/libsrun/srun_job.h"

typedef struct slurmctld_communication_addr {
	uint16_t port;
} slurmctld_comm_addr_t;

slurmctld_comm_addr_t slurmctld_comm_addr;

/*
 * Allocate nodes from the slurm controller -- retrying the attempt
 * if the controller appears to be down, and optionally waiting for
 * resources if none are currently available (see opt.immediate)
 *
 * Returns a pointer to a resource_allocation_response_msg which must
 * be freed with slurm_free_resource_allocation_response_msg()
 */
extern resource_allocation_response_msg_t *
	allocate_nodes(bool handle_signals, slurm_opt_t *opt_local);

/*
 * Allocate nodes for heterogeneous/pack job from the slurm controller -- 
 * retrying the attempt if the controller appears to be down, and optionally
 * waiting for resources if none are currently available (see opt.immediate)
 *
 * Returns a pointer to a resource_allocation_response_msg which must
 * be freed with slurm_free_resource_allocation_response_msg()
 */
List allocate_pack_nodes(bool handle_signals);

/* dummy function to handle all signals we want to ignore */
void ignore_signal(int signo);

/* clean up the msg thread polling for information from the controller */
int cleanup_allocation(void);

/*
 * Test if an allocation would occur now given the job request.
 * Do not actually allocate resources
 */
extern int allocate_test(void);

/* Set up port to handle messages from slurmctld */
int slurmctld_msg_init(void);

/*
 * Destroy (free memory from) a job_desc_msg_t object allocated with
 * job_desc_msg_create()
 */
void job_desc_msg_destroy (job_desc_msg_t *j);

/*
 * Check for SLURM_JOB_ID environment variable, and if it is a valid
 * jobid, return a pseudo allocation response pointer.
 *
 * Returns NULL if SLURM_JOB_ID is not present or is invalid.
 */
extern List existing_allocation(void);

/*
 * Create a job step given the job information stored in 'j'
 * After returning, 'j' is filled in with information for job step.
 * IN use_all_cpus - true to use every CPU allocated to the job
 * IN opt_local - options used to create job step
 *
 * Returns -1 if job step creation failure, 0 otherwise
 */
int create_job_step(srun_job_t *j, bool use_all_cpus, slurm_opt_t *opt_local);

/* set the job for debugging purpose */
void set_allocate_job(srun_job_t *job);

#endif /* !_HAVE_ALLOCATE_H */
