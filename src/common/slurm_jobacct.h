/*****************************************************************************\
 *  slurm_jobacct.h - implementation-independent job completion logging 
 *  API definitions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.com> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
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

/*****************************************************************************\
 *  Modification history
 *
 *  19 Jan 2005 by Andy Riebs <andy.riebs@hp.com>
 *       This file is derived from the file slurm_JOBACCT.c, written by
 *       Moe Jette, et al.
\*****************************************************************************/


#ifndef __SLURM_JOBACCT_H__
#define __SLURM_JOBACCT_H__

#if HAVE_STDINT_H
#  include <stdint.h>           /* for uint16_t, uint32_t definitions */
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>         /* for uint16_t, uint32_t definitions */
#endif
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd_job.h"

typedef struct slurm_jobacct_context * slurm_jobacct_context_t;


extern int g_slurm_jobacct_process_message(struct slurm_msg *msg);

extern int g_slurmctld_jobacct_init(char *job_acct_loc, 
		char *job_acct_parameters);

extern int g_slurmctld_jobacct_fini(void);

extern int g_slurmctld_jobacct_job_complete(struct job_record *job_ptr);

extern int g_slurmctld_jobacct_job_start(struct job_record *job_ptr);

extern int g_slurmd_jobacct_init(char *job_acct_parameters);

extern int g_slurmd_jobacct_jobstep_launched(slurmd_job_t *job);

extern int g_slurmd_jobacct_jobstep_terminated(slurmd_job_t *job);

extern int g_slurmd_jobacct_smgr(void);

extern int g_slurmd_jobacct_task_exit(slurmd_job_t *job, pid_t pid,
		int status, struct rusage *rusage);

#endif /*__SLURM_JOBACCT_H__*/

