/*****************************************************************************\
 * src/srun/io.h - srun I/O routines
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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
#ifndef _HAVE_IO_H
#define _HAVE_IO_H

#include "src/common/io_hdr.h"
#include "src/srun/srun_job.h"

#define STDIO_MAX_FREE_BUF 1024

struct io_buf {
	int ref_count;
	uint32_t length;
	void *data;
	io_hdr_t header;
};

struct io_buf *alloc_io_buf(void);
void free_io_buf(struct io_buf *buf);

int   io_node_fail(char *nodelist, srun_job_t *job);
int   io_thr_create(srun_job_t *job);
eio_obj_t *create_file_write_eio_obj(int fd, srun_job_t *job);
eio_obj_t *create_file_read_eio_obj(int fd, srun_job_t *job,
				    uint16_t type, uint16_t gtaskid);

#endif /* !_HAVE_IO_H */
