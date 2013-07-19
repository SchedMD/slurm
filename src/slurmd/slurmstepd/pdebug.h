/*****************************************************************************\
 * pdebug.h - ptrace functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#ifndef _PDEBUG_H
#define _PDEBUG_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <unistd.h>
#include <sys/param.h>
#ifdef HAVE_SYS_PTRACE_H
#  include <sys/ptrace.h>
#endif
#include <sys/wait.h>
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * Stop current task on exec() for connection from a parallel debugger
 */
void pdebug_stop_current(stepd_step_rec_t *job);

/*
 * Prepare task for parallel debugger attach
 * Returns SLURM_SUCCESS or SLURM_ERROR.
 */
int pdebug_trace_process(stepd_step_rec_t *job, pid_t pid);

/*
 * Wake tasks currently stopped for parallel debugger attach
 */
void pdebug_wake_process(stepd_step_rec_t *job, pid_t pid);

#ifdef HAVE_PTRACE64
#  define _PTRACE(r,p,a,d) ptrace64((r),(long long)(p),(long long)(a),(d),NULL)
#else
#  ifdef PTRACE_FIVE_ARGS
#    define _PTRACE(r,p,a,d) ptrace((r),(p),(a),(d),NULL)
#  elif defined BSD
#    define _PTRACE(r,p,a,d) ptrace((r),(p),(a),(d))
#  else
#    define _PTRACE(r,p,a,d) ptrace((r),(p),(a),(void *)(d))
#  endif
#endif

#endif
