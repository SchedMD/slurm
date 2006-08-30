/*****************************************************************************\
 * src/srun/msg.h - message traffic between srun and slurmd routines
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

#include "src/srun/srun_job.h"

#ifndef _HAVE_MSG_H
#define _HAVE_MSG_H

void     *msg_thr(void *arg);
int       msg_thr_create(srun_job_t *job);
slurm_fd  slurmctld_msg_init(void);
void      timeout_handler(time_t timeout);

typedef struct slurmctld_communication_addr {
	char *hostname;
	uint16_t port;
} slurmctld_comm_addr_t;

slurmctld_comm_addr_t slurmctld_comm_addr;

#endif /* !_HAVE_MSG_H */
