/*****************************************************************************\
 *  plugstack.h -- plugin stack handling
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

#ifndef _SLURMD_PLUGSTACK_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

int spank_init (slurmd_job_t *job);

int spank_user (slurmd_job_t *job);

int spank_user_task (slurmd_job_t *job, int taskid);

int spank_task_post_fork (slurmd_job_t *job, int taskid);

int spank_task_exit (slurmd_job_t *job, int taskid);

int spank_fini (slurmd_job_t *job);

#endif /* !_SLURMD_PLUGSTACK_H */
