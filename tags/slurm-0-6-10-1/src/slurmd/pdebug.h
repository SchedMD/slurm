/*****************************************************************************\
 * src/slurmd/ptrace_debug.h - ptrace functions for slurmd
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
#ifndef _PDEBUG_H
#define _PDEBUG_H

#include <sys/ptrace.h>
#include <sys/wait.h>
#include "src/slurmd/slurmd_job.h"

/*
 * Stop current task on exec() for connection from a parallel debugger
 */
void pdebug_stop_current(slurmd_job_t *job);
/*
 * Prepare task for parallel debugger attach
 * Returns SLURM_SUCCESS or SLURM_ERROR.
 */
int pdebug_trace_process(slurmd_job_t *job, pid_t pid);

#ifdef HAVE_PTRACE64
#  define _PTRACE(r,p,a,d) ptrace64((r),(long long)(p),(long long)(a),(d),NULL)
#else
#  ifdef PTRACE_FIVE_ARGS
#    define _PTRACE(r,p,a,d) ptrace((r),(p),(a),(d),NULL)
#  else
#    define _PTRACE(r,p,a,d) ptrace((r),(p),(a),(void *)(d))
#  endif
#endif

#endif
