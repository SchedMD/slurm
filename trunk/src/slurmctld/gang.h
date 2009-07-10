/*****************************************************************************\
 *  gang.h - Gang scheduler definitions
 *****************************************************************************
 *  Copyright (C) 2008 Hewlett-Packard Development Company, L.P.
 *  Written by Chris Holmes
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __SCHED_GANG_H
#define __SCHED_GANG_H

#include <stdio.h>
#include <slurm/slurm_errno.h>

#include "src/common/plugin.h"
#include "src/common/log.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/slurmctld.h"

/* Initialize data structures and start the gang scheduling thread */
extern int 	gs_init(void);

/* Terminate the gang scheduling thread and free its data structures */
extern int 	gs_fini(void);

/* Notify the gang scheduler that a job has been started */
extern int	gs_job_start(struct job_record *job_ptr);

/* scan the master SLURM job list for any new jobs to add, or for any old jobs 
 *	to remove */
extern int	gs_job_scan(void);

/* Notify the gang scheduler that a job has completed */
extern int	gs_job_fini(struct job_record *job_ptr);

/* Gang scheduling has been disabled by change in configuration, 
 *	resume any suspended jobs */
extern void	gs_wake_jobs(void);

/* Tell gang scheduler that system reconfiguration has been performed
 *	configuration parameters may have changed. Rebuild data structures 
 *	from scratch */
extern int	gs_reconfig(void);

#endif
