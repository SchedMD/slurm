/*****************************************************************************\
 * src/slurmd/slurmstepd_init.h - slurmstepd intialization code
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#ifndef _SLURMD_STEP_INIT_H
#define _SLURMD_STEP_INIT_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "src/common/slurm_protocol_defs.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

typedef enum slurmd_step_tupe {
	LAUNCH_BATCH_JOB = 0,
	LAUNCH_TASKS,
	SPAWN_TASKS
} slurmd_step_type_t;

/*
 * Pack information needed for the forked slurmstepd process.
 * Does not pack everything from the slurm_conf_t struct.
 */
void pack_slurmd_conf_lite(slurmd_conf_t *conf, Buf buffer);

/*
 * Unpack information needed for the forked slurmstepd process.
 * Does not unpack everything from the slurm_conf_t struct.
*/
int unpack_slurmd_conf_lite_no_alloc(slurmd_conf_t *conf, Buf buffer);

#endif /* _SLURMD_STEP_INIT_H */

