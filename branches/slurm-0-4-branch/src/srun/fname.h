/*****************************************************************************\
 * src/srun/fname.c - IO filename type implementation (srun specific)
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

#ifndef _FNAME_H
#define _FNAME_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif 

#include "src/srun/opt.h"

typedef struct io_filename {
	char      *name;
	enum io_t  type;
	int        taskid;  /* taskid for IO if IO_ONE */
} io_filename_t;

/* need to predeclare srun_job to resolve declaration dependencies
 */
typedef struct srun_job * srun_job_t;

/*
 * Create an filename from a (probably user supplied) filename format.
 * fname_create() will expand the format as much as possible for srun,
 * leaving node or task specific format specifiers for the remote 
 * slurmd to handle.
 */
io_filename_t * fname_create(srun_job_t job, char *format);
void fname_destroy(io_filename_t *fname);

char * fname_remote_string (io_filename_t *fname);

#endif /* !_FNAME_H */

