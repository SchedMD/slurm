/*****************************************************************************\
 *  pmi.h - Global PMI data as maintained within srun
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _PMI_SERVER_H
#define _PMI_SERVER_H

#include "src/api/slurm_pmi.h"

/* Put the supplied kvs values into the common store */
extern int pmi_kvs_put(struct kvs_comm_set *kvs_set_ptr);

/* Note that a task has reached a barrier,
 * transmit the kvs values to the task */
extern int pmi_kvs_get(kvs_get_msg_t *kvs_get_ptr);

/*
 * Set the maximum number of threads to be used by the PMI server code.
 * The PMI server code is used interally by the slurm_step_launch() function
 * to support MPI libraries that bootstrap themselves using PMI.
 */
extern void pmi_server_max_threads(int max_threads);

#endif
