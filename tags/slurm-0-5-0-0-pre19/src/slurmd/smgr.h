/*****************************************************************************\
 * src/slurmd/smgr.h - session manager functions for slurmd
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

#ifndef _SMGR_H
#define _SMGR_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <slurm/slurm_errno.h>

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif /* HAVE_SYS_TYPES_H */

#include "src/slurmd/job.h"

/*
 * Task exit code information
 */
typedef struct exit_status {
	int taskid;
	int status;
} exit_status_t;


/*
 * Create the session manager process, which starts a new session
 * and runs as the UID of the job owner. The session manager process
 * will wait for all tasks in the job to exit (sending task exit messages
 * as appropriate), and then exit itself.
 *
 * If the smgr process is successfully created, the pid of the new 
 * process is returned. On error, (pid_t) -1 is returned.
 *
 */
pid_t smgr_create(slurmd_job_t *job);

#endif /* !_SMGR_H */
