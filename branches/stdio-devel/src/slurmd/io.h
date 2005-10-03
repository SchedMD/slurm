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

struct io_buf {
	int ref_count;
	uint32_t length;
	void *data;
};

struct io_buf *alloc_io_buf(void);
void free_io_buf(struct io_buf *buf);

/*
 * Start IO handling thread.
 * Initializes IO pipes, creates IO objects and appends them to job->objs,
 * and opens 2*ntask initial connections for stdout/err, also appending these
 * to job->objs list.
 */
int io_thread_start(slurmd_job_t *job);

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

int io_dup_stdio(slurmd_task_info_t *t);

/*
 *  Close the tasks' ends of the stdio pipes.
 *  Presumably the tasks have already been started, and
 *  have their copies of these file descriptors.
 */
void io_close_task_fds(slurmd_job_t *job);

void io_close_all(slurmd_job_t *job);

/*
 * Connect initial N tasks to their stdio
 */
int io_prepare_clients(slurmd_job_t *job);

#endif /* !_IO_H */
