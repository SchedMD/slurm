/*****************************************************************************\
 * src/slurmd/io.h - slurmd IO routines
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-2002-040.
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

#ifndef _IO_H
#define _IO_H

#include "src/slurmd/slurmd_job.h"
#include "src/common/eio.h"

/*
 * Spawn IO handling thread.
 * Initializes IO pipes, creates IO objects and appends them to job->objs,
 * and opens 2*ntask initial connections for stdout/err, also appending these
 * to job->objs list.
 */
int io_spawn_handler(slurmd_job_t *job);

/*
 * Create a set of new connecting clients for the running job
 * Grabs the latest srun object off the job's list of attached 
 * sruns, and duplicates stdout/err to this new client.
 */
int io_new_clients(slurmd_job_t *job);

/*
 * Frees memory associated with the given IO object
 */
void io_obj_destroy(io_obj_t *obj);

int  io_init_pipes(slurmd_job_t *job);
int  io_prepare_child(slurmd_task_info_t *t);

void io_close_all(slurmd_job_t *job);

/*
 * Connect initial N tasks to their stdio
 */
int io_prepare_clients(slurmd_job_t *job);

/* Notes:
 *
 * slurmd <-+---> client (e.g. srun, file)
 *          `---> client
 * 
 * slurmd can handle multiple client connections. Each task writes
 * stdout and stderr data to the client and reads stdin and signals
 * from the client streams. 
 *
 * I/O objects:
 * task stdout: R/0 pipe created by slurmd
 *  - buffer is null
 *  - readers list has at least one client reader (may be a file obj)
 *  - writers list is empty
 *
 *  task stderr: R/O pipe created by slurmd
 *  - buffer is null
 *  - readers list has at least one client reader (may be a file obj)
 *  - writers list is empty
 *
 *  task stdin: W/O pipe created by slurmd
 *  - circular buffer
 *  - readers list is empty
 *  - writers list contains only one client (may be a file obj)
 *
 *  client stdout/in socket:
 *  - circular buffer for stdout data
 *  - readers list is one task stdin obj or empty
 *  - writers list is one task stdout obj
 *
 *  client stderr/sig socket:
 *  - circular buffer for stderr data
 *  - readers list is null (data read is converted to signal)
 *  - writers list is one task stderr obj
 *
 *  stdout/err file obj:
 *  - circular buffer for stdout/err data
 *  - readers list is empty
 *  - writers list is one task stdout/err obj
 *
 *  stdin file obj
 *  - buffer is null
 *  - readers list is one or more task stdin obj's
 *  - writers list is empty
 */

#endif /* !_IO_H */
