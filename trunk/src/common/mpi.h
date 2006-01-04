/*****************************************************************************\
 * src/common/mpi.h - Generic mpi selector for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondo1@llnl.gov>.
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

#ifndef _SRUN_MPI_H
#define _SRUN_MPI_H

#if HAVE_CONFIG_H
# include "config.h"
#endif 

#include "src/srun/opt.h"
#include "src/srun/srun_job.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurm_mpi_context *slurm_mpi_context_t;

int srun_mpi_init (char *mpi_type);
int slurmd_mpi_init (slurmd_job_t *job, int rank);
int mpi_fini (void);
int slurm_mpi_thr_create(srun_job_t *job);
int slurm_mpi_single_task_per_node (void);
int slurm_mpi_exit (void);


#endif /* !_SRUN_MPI_H */
