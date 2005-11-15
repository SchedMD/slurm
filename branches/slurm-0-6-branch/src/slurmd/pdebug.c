/*****************************************************************************\
 * src/slurmd/ptrace_debug.c - ptrace functions for slurmd
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
#include "pdebug.h"

/*
 * Prepare task for parallel debugger attach
 * Returns SLURM_SUCCESS or SLURM_ERROR.
 */
int
pdebug_trace_process(slurmd_job_t *job, pid_t pid)
{
	/*  If task to be debugged, wait for it to stop via
	 *  child's ptrace(PTRACE_TRACEME), then SIGSTOP, and 
	 *  ptrace(PTRACE_DETACH). This requires a kernel patch,
	 *  which you may already have in place for TotalView.
	 *  If not, apply the kernel patch in etc/ptrace.patch
	 */

	if (job->task_flags & TASK_PARALLEL_DEBUG) {
		int status;
		waitpid(pid, &status, WUNTRACED);
		if (!WIFSTOPPED(status)) {
			debug("pdebug_trace_process WIFSTOPPED false"
			      " for pid %lu", pid);
			if (WIFEXITED(status)) {
				debug("Process %lu exited \"normally\""
				      " with return code %d",
				      pid, WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				debug("Process %lu kill by signal %d",
				      pid, WTERMSIG(status));
			}
			return SLURM_ERROR;
		}
		if ((pid > (pid_t) 0) && (kill(pid, SIGSTOP) < 0)) {
			error("kill(%lu): %m", (unsigned long) pid);
			return SLURM_ERROR;
		}

#ifdef HAVE_AIX
		if (_PTRACE(PT_DETACH, pid, NULL, 0)) {
#else
		if (_PTRACE(PTRACE_DETACH, pid, NULL, 0)) {
#endif
			error("ptrace(%lu): %m", (unsigned long) pid);
			return SLURM_ERROR;
		}
	}
	return SLURM_SUCCESS;
}

/*
 * Stop current task on exec() for connection from a parallel debugger
 */
void
pdebug_stop_current(slurmd_job_t *job)
{
	/* 
	 * Stop the task on exec for TotalView to connect 
	 */
	if ( (job->task_flags & TASK_PARALLEL_DEBUG)
#ifdef HAVE_AIX
	     && (_PTRACE(PT_TRACE_ME, 0, NULL, 0) < 0) )
#else
	     && (_PTRACE(PTRACE_TRACEME, 0, NULL, 0) < 0) )
#endif
		error("ptrace: %m");
}
