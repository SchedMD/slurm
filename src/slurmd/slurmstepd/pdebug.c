/*****************************************************************************\
 * pdebug.c - ptrace functions for slurmstepd
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include "pdebug.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>


/*
 * Prepare task for parallel debugger attach
 * Returns SLURM_SUCCESS or SLURM_ERROR.
 */
int
pdebug_trace_process(stepd_step_rec_t *job, pid_t pid)
{
	/*  If task to be debugged, wait for it to stop via
	 *  child's ptrace(PTRACE_TRACEME), then SIGSTOP, and
	 *  ptrace(PTRACE_DETACH).
	 */

	if (job->flags & LAUNCH_PARALLEL_DEBUG) {
		int status;
		waitpid(pid, &status, WUNTRACED);
		if (!WIFSTOPPED(status)) {
			int i;
			error("pdebug_trace_process WIFSTOPPED false"
			      " for pid %d", pid);
			if (WIFEXITED(status)) {
				error("Process %d exited \"normally\""
				      " with return code %d",
				      pid,
				      WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				error("Process %d killed by signal %d",
				      pid, WTERMSIG(status));
			}

			/*
			 * Mark this process as complete since it died
			 * prematurely.
			 */
			for (i = 0; i < job->node_tasks; i++) {
				if (job->task[i]->pid == pid) {
					job->task[i]->state =
						STEPD_STEP_TASK_COMPLETE;
				}
			}

			return SLURM_ERROR;
		}
		if ((pid > (pid_t) 0) && (kill(pid, SIGSTOP) < 0)) {
			error("kill(%lu): %m", (unsigned long) pid);
			return SLURM_ERROR;
		}

#ifdef BSD
		if (_PTRACE(PT_DETACH, pid, (caddr_t)1, 0)) {
#elif defined(PT_DETACH)
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
pdebug_stop_current(stepd_step_rec_t *job)
{
	/*
	 * Stop the task on exec for TotalView to connect
	 */
	if ((job->flags & LAUNCH_PARALLEL_DEBUG)
#ifdef BSD
	     && (_PTRACE(PT_TRACE_ME, 0, (caddr_t)0, 0) < 0) )
#elif defined(PT_TRACE_ME)
	     && (_PTRACE(PT_TRACE_ME, 0, NULL, 0) < 0) )
#else
	     && (_PTRACE(PTRACE_TRACEME, 0, NULL, 0) < 0) )
#endif
		error("ptrace: %m");
}

/* Check if this PID should be woken for TotalView partitial attach */
static int _being_traced(pid_t pid)
{
	FILE *fp = NULL;
	size_t n = 0, max_len;
	int tracer_id = 0;
	char *match = NULL;
	char buf[2048] = {0};
	char sp[PATH_MAX] = {0};

	if (snprintf(sp, PATH_MAX, "/proc/%lu/status",(unsigned long)pid) == -1)
		return -1;
	if ((fp = fopen((const char *)sp, "r")) == NULL)
		return -1;

	max_len = sizeof(buf) - 1;
	n = fread(buf, 1, max_len, fp);
	fclose(fp);
	if ((n == 0) || (n == max_len))
		return -1;
	buf[n] = '\0';	/* Ensure string is terminated */
	if ((match = strstr(buf, "TracerPid:")) == NULL)
		return -1;
	if (sscanf(match, "TracerPid:\t%d", &tracer_id) == EOF)
		return -1;
	return tracer_id;
}

static bool _pid_to_wake(pid_t pid)
{
	int rc = 0;

	if ((rc = _being_traced(pid)) == -1) {
		/* If an error occurred (e.g., /proc FS doesn't exist
		 * or TracerPid field doesn't exist, it is better to wake
		 * up the target process -- at the expense of potential
		 * side effects on the debugger. */
		debug("_pid_to_wake(%lu): %m\n", (unsigned long) pid);
		errno = 0;
		rc = 0;
	}
	return (rc == 0) ? true : false;
}

/*
 * Wake tasks currently stopped for parallel debugger attach
 */
void pdebug_wake_process(stepd_step_rec_t *job, pid_t pid)
{
	if ((job->flags & LAUNCH_PARALLEL_DEBUG) && (pid > (pid_t) 0)) {
		if (_pid_to_wake(pid)) {
			if (kill(pid, SIGCONT) < 0)
				error("kill(%lu): %m", (unsigned long) pid);
			else
				debug("woke pid %lu", (unsigned long) pid);
		} else {
			debug("pid %lu not stopped or being traced",
			      (unsigned long) pid);
		}
	}
}
