/*****************************************************************************\
 *  gmpi.h - srun support for MPICH-GM (GMPI)
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Takao Hatazaki <takao.hatazaki@hp.com>
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

#ifndef _HAVE_GMPI_H
#define _HAVE_GMPI_H

#include "src/srun/job.h"

typedef struct {
	int defined;
	unsigned int port_board_id;
	unsigned int unique_high_id;
	unsigned int unique_low_id;
	unsigned int numanode;
	unsigned int remote_pid;
	unsigned int remote_port;
} gm_slave_t;

#define GMPI_RECV_BUF_LEN 65536

extern int gmpi_thr_create(job_t *job, char **port);

#endif	/* _HAVE_GMPI_H */

