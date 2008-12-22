/*****************************************************************************\
 * src/slurmd/slurmstepd_init.h - slurmstepd intialization code
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
	DEFUNCT_SPAWN_TASKS /* DEFUNCT */
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

