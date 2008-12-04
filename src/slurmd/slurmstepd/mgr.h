/*****************************************************************************\
 * src/slurmd/slurmstepd/mgr.c - job management functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#ifndef _MGR_H
#define _MGR_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/slurm_protocol_defs.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Send batch exit code to slurmctld
 */
void batch_finish(slurmd_job_t *job, int rc);

/*
 * Initialize a slurmd_job_t structure for a launch tasks
 */
slurmd_job_t *mgr_launch_tasks_setup(launch_tasks_request_msg_t *msg,
				     slurm_addr *client, slurm_addr *self);

/* 
 * Initialize a slurmd_job_t structure for a batch job
 */
slurmd_job_t *mgr_launch_batch_job_setup(batch_job_launch_msg_t *msg,
					 slurm_addr *client);

/*
 * Finalize a batch job.
 */
void mgr_launch_batch_job_cleanup(slurmd_job_t *job, int rc);

/*
 * Launch and manage the tasks in a job step.
 */
int job_manager(slurmd_job_t *job);

/*
 * Register passwd entries so that we do not need to call initgroups(2)
 * frequently.
 */
extern void init_initgroups(int);


#endif
