/*****************************************************************************\
 *  src/slurmd/interconnect.h - general interconnect routines for slurmd
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> 
 *          modified by Mark Grondona <mgrondona@llnl.gov>
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

#ifndef _INTERCONNECT_H_
#define _INTERCONNECT_H_

#include "src/slurmd/job.h"

/*
 * Setup node for interconnect use.
 *
 * This function is run from the top level slurmd only once per
 * slurmd run. It may be used, for instance, to perform some one-time
 * interconnect setup or spawn an error handling thread.
 *
 */
int interconnect_node_init(void);

/*
 * Finalize interconnect on node. 
 *
 * This function is called once as slurmd exits (slurmd will wait for
 * this function to return before continuing the exit process)
 */
int interconnect_node_fini(void);


/*
 * Notes on job related interconnect functions:
 *
 * Interconnect functions are run within slurmd in the following way:
 * (Diagram courtesy of Jim Garlick [see qsw.c] )
 *
 *  Process 1 (root)        Process 2 (root, user)  |  Process 3 (user task)
 *                                                  |
 *  interconnect_preinit                            |
 *  fork ------------------ interconnect_init       |
 *  waitpid                 setuid, chdir, etc.     |
 *                          fork N procs -----------+--- interconnect_attach
 *                          wait all                |    exec mpi process
 *                          interconnect_fini*      |
 *  interconnect_postfini                           |    
 *                                                  |
 *
 * [ *Note: interconnect_fini() is run as the uid of the job owner, not root ]
 */
/*
 * Prepare node for job. 
 *
 * pre is run as root in the first slurmd process, the so called job
 * manager. This function can be used to perform any initialization
 * that needs to be performed in the same process as interconnect_fini()
 * 
 */
int interconnect_preinit(slurmd_job_t *job);

/* 
 * initialize interconnect on node for job. This function is run from the 
 * 2nd slurmd process (some interconnect implementations may require
 * interconnect init functions to be executed from a separate process
 * than the process executing initerconnect_fini() [e.g. QsNet])
 *
 */
int interconnect_init(slurmd_job_t *job);

/*
 * This function is run from the same process as interconnect_init()
 * after all job tasks have exited. It is *not* run as root, because
 * the process in question has already setuid to the job owner.
 *
 */
int interconnect_fini(slurmd_job_t *job);

/*
 * Finalize interconnect on node.
 *
 * This function is run from the initial slurmd process (same process
 * as interconnect_preinit()), and is run as root. Any cleanup routines
 * that need to be run with root privileges should be run from this
 * function.
 */
int interconnect_postfini(slurmd_job_t *job);

/* 
 * attach process to interconnect
 * (Called from within the process, so it is appropriate to set 
 * interconnect specific environment variables here)
 */
int interconnect_attach(slurmd_job_t *job, int taskid);

#endif /* _INTERCONNECT_H */
