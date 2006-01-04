/*****************************************************************************\
 *  task_plugin.h - Define plugin functions for task pre_launch and post_term.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#ifndef _SLURMD_TASK_PLUGIN_H_
#define _SLURMD_TASK_PLUGIN_H_

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Initialize the task plugin.
 *
 * RET - slurm error code
 */
extern int slurmd_task_init( void );

/*
 * Terminate the task plugin, free memory.
 * 
 * RET - slurm error code
 */
extern int slurmd_task_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * Note that a task launch is about to occur.
 *
 * RET - slurm error code
 */
extern int pre_launch(slurmd_job_t *job);

/*
 * Note that a task has terminated.
 *
 * RET - slurm error code
 */
extern int post_term(slurmd_job_t *job);

#endif /* _SLURMD_TASK_PLUGIN_H_ */

