/*****************************************************************************\
 * src/common/mpi.h - Generic mpi selector for slurm
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondo1@llnl.gov>.
 *  UCRL-CODE-226842.
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

#ifndef _SRUN_MPI_H
#define _SRUN_MPI_H

#if HAVE_CONFIG_H
# include "config.h"
#endif 

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurm_mpi_context *slurm_mpi_context_t;

typedef struct {
	uint32_t jobid;
	uint32_t stepid;
	slurm_step_layout_t *step_layout;
} slurm_mpi_jobstep_info_t;

/**********************************************************************
 * Hooks called by the slurmd and/or slurmstepd.
 **********************************************************************/

/* Load the plugin and call the plugin mpi_p_init() function. */
int slurm_mpi_slurmd_init (slurmd_job_t *job, int rank);

/**********************************************************************
 * Hooks called by client applications.
 * For instance: srun, slaunch, slurm_step_launch().
 **********************************************************************/

/*
 * Just load the requested plugin.  No explicit calls into the plugin
 * once loaded.
 *
 * This function is only called if the user explicitly
 * requested a particular plugin.  Otherwise the system-default mpi plugin
 * is initialized on demand when any of the other slurm_mpi_client_*
 * functions are called.
 */
int slurm_mpi_client_init (char *mpi_type);

/* Call the plugin mpi_p_thr_create() function. */
int slurm_mpi_client_thr_create(slurm_mpi_jobstep_info_t *job, char ***env);

/* Call the plugin mpi_p_single_task() function. */
int slurm_mpi_client_single_task_per_node (void);

/* Call the plugin mpi_p_exit() function. */
int slurm_mpi_client_exit (void);

/**********************************************************************
 * FIXME - Nobody calls the following function.  Perhaps someone should.
 **********************************************************************/
int mpi_fini (void);

#endif /* !_SRUN_MPI_H */
