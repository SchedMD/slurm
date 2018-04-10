/*****************************************************************************\
 *  src/slurmd/slurmstepd/multi_prog.h - Task specific argv arrays
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>.
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

#ifndef _SLURMD_MULTI_PROG_H
#define _SLURMD_MULTI_PROG_H

#include "slurmstepd_job.h"

/*
 * Parse an MPMD file and determine count and layout of each task for use
 * with Cray systems. Builds the mpmd_set structure in the job record.
 *
 * IN/OUT job - job step details, builds mpmd_set structure
 * IN gtid - Array of global task IDs, indexed by node_id and task
 */
extern void multi_prog_parse(stepd_step_rec_t *job, uint32_t **gtid);

/* Free memory associated with a job's MPMD data structure built by
 * multi_prog_parse() and used for Cray system. */
extern void mpmd_free(stepd_step_rec_t *job);

/* Execute a single task based upon the config_data (contents of config_file)
 * and the environment variables supplied.
 *
 * "task_rank" is the task's GLOBAL rank within the job step.
 */
extern int multi_prog_get_argv(char *config_data, char **prog_env,
			       int task_rank, uint32_t *argc, char ***argv,
			       int global_argc, char **global_argv);
#endif /* !_SLURMD_MULTI_PROG_H */
