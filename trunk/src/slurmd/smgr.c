/*****************************************************************************\
 * slurmd/smgr.c - session manager functions for slurmd
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark A. Grondona <mgrondona@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/ptrace.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include <slurm/slurm_errno.h>

#include "src/common/log.h"
#include "src/common/fd.h"
#include "src/common/xsignal.h"

#include "src/slurmd/smgr.h"
#include "src/slurmd/ulimits.h"
#include "src/slurmd/interconnect.h"
#include "src/slurmd/setenvpf.h"
#include "src/slurmd/io.h"

/*
 * Static prototype definitions.
 */
static void  _session_mgr(slurmd_job_t *job);
static int   _exec_all_tasks(slurmd_job_t *job);
static void  _exec_task(slurmd_job_t *job, int i);
static int   _become_user(slurmd_job_t *job);
static int   _block_smgr_signals(void);
static int   _child_exited(void);
static void  _wait_for_all_tasks(slurmd_job_t *job);
static int   _local_taskid(slurmd_job_t *job, pid_t pid);
static int   _send_exit_status(slurmd_job_t *job, pid_t pid, int status);
static int   _unblock_all_signals(void);
static void  _cleanup_file_descriptors(slurmd_job_t *job);
static int   _setup_env(slurmd_job_t *job, int taskid);

/* parallel debugger support */
static void  _pdebug_trace_process(slurmd_job_t *job, pid_t pid);
static void  _pdebug_stop_current(slurmd_job_t *job);

/*
 * Create the slurmd session manager process
 */
pid_t 
smgr_create(slurmd_job_t *job)
{
	pid_t pid;
	switch ((pid = fork())) {
	case -1:
		error("smgr_create: fork: %m");
		return pid;
		break;
	case  0: /* child */
		close(job->fdpair[0]);
		_session_mgr(job);
		/* NOTREACHED */
		break;
	}

	/* parent continues here */

	close(job->fdpair[1]);

	return pid;
}

static void
_session_mgr(slurmd_job_t *job)
{
	xassert(job != NULL);

	/*
	 * Call interconnect_init() before becoming user
	 */
	if (!job->batch && (interconnect_init(job) < 0)) {
		error("interconnect_init: %m");
		exit(1);
	}

	if (_become_user(job) < 0) 
		exit(2);
		
	if (setsid() < (pid_t) 0) {
		error("setsid: %m");
		exit(3);
	}

	if (chdir(job->cwd) < 0) {
		error("couldn't chdir to `%s': %m: going to /tmp instead",
		      job->cwd);
		if (chdir("/tmp") < 0) {
			error("couldn't chdir to /tmp either. dying.");
			exit(4);
		}
	}

	if (set_user_limits(job) < 0) {
		debug("Unable to set user limits");
		exit(5);
	}

	if (_exec_all_tasks(job) < 0) {
		debug("exec_all_tasks failed");
		exit(6);
	}

	/*
	 *  Clean up open file descriptors in session manager so that
	 *    IO thread in job manager can tell output is complete,
	 *    and additionally, so that closing stdin will generate
	 *    EOF to tasks
	 */ 
	_cleanup_file_descriptors(job);

        _wait_for_all_tasks(job);

	if (!job->batch && (interconnect_fini(job) < 0)) {
		error("interconnect_fini: %m");
		exit(1);
	}

	exit(SLURM_SUCCESS);
}

/* Close write end of stdin (at the very least)
 */
static void
_cleanup_file_descriptors(slurmd_job_t *j)
{
	int i;
	for (i = 0; i < j->ntasks; i++) {
		task_info_t *t = j->task[i];
		/*
		 * Ignore errors on close()
		 */
		close(t->pin[1]); 
		close(t->pout[0]);
		close(t->perr[0]);
	}
}

static int
_become_user(slurmd_job_t *job)
{
	if (setgid(job->pwd->pw_gid) < 0) {
		error("setgid: %m");
		return -1;
	}

	if (initgroups(job->pwd->pw_name, job->pwd->pw_gid) < 0) {
		;
		/* error("initgroups: %m"); */
	}

	if (setuid(job->pwd->pw_uid) < 0) {
		error("setuid: %m");
		return -1;
	}

	return 0;
}	

/* Execute N tasks and send pids back to job manager process.
 */ 
static int
_exec_all_tasks(slurmd_job_t *job)
{
	int i;
	int fd = job->fdpair[1];

	xassert(job != NULL);
	xassert(fd >= 0);

	_block_smgr_signals();

	for (i = 0; i < job->ntasks; i++) {
		pid_t pid = fork();

		if (pid < 0) {
			error("fork: %m");
			return SLURM_ERROR;
		} else if (pid == 0)  /* child */
			_exec_task(job, i);

		/* Parent continues: 
		 */

		debug2("pid %ld forked child process %ld for local task %d",
		       (long) getpid(), (long) pid, i);

		/* 
		 * Send pid to job manager
		 */
		if (fd_write_n(fd, (char *)&pid, sizeof(pid_t)) < 0) {
			error("unable to update task pid!: %m");
			return SLURM_ERROR;
		}

		job->task[i]->pid = pid;

		/*
		 * Prepare process for attach by parallel debugger 
		 * (if specified and able)
		 */
		_pdebug_trace_process(job, pid);
	}

	return SLURM_SUCCESS;
}


static void
_exec_task(slurmd_job_t *job, int i)
{
	if (_unblock_all_signals() < 0) {
		error("unable to unblock signals");
		exit(1);
	}

	if (!job->batch) {
		if (interconnect_attach(job, i) < 0) {
			error("Unable to attach to interconnect: %m");
			exit(1);
		}

		if (_setup_env(job, i) < 0)
			error("error establishing SLURM env vars: %m");

		_pdebug_stop_current(job);
	}

	/* 
	 * If io_prepare_child() is moved above interconnect_attach()
	 * this causes EBADF from qsw_attach(). Why?
	 */
	io_prepare_child(job->task[i]);

	execve(job->argv[0], job->argv, job->env);

	/* 
	 * error() and clean up if execve() returns:
	 */
	error("execve(): %s: %m", job->argv[0]); 
	exit(errno);
}


/*
 *  Block a set of signals so that session manager process
 *   is not killed.
 */
static int
_block_smgr_signals(void)
{
	int      e;
	sigset_t set;

	if (sigemptyset(&set) < 0)
		error("sigemptyset: %m");
	if (sigaddset(&set, SIGCHLD) < 0)
		error("sigaddset(SIGCHLD): %m");
	if (sigaddset(&set, SIGTERM) < 0)
		error("sigaddset(SIGTERM): %m");
	if (sigaddset(&set, SIGINT) < 0)
		error("sigaddset(SIGINT): %m");
	if (sigaddset(&set, SIGXCPU) < 0)
		error("sigaddset(SIGXCPU): %m");

	if ((e = pthread_sigmask(SIG_BLOCK, &set, NULL)) < 0)
		error("pthread_sigmask: %s", slurm_strerror(e));

	return e ? SLURM_ERROR : SLURM_SUCCESS;
}


/*
 *  Call sigwait on the set of signals already blocked in this
 *   process, and only return (true) on reciept of SIGCHLD;
 */
static int
_child_exited(void)
{
	int      sig;
	sigset_t set;

	/*
	 *  Get current mask of blocked signals
	 */
	xsignal_save_mask(&set);

	do {
		sigwait(&set, &sig);
		switch (sig) {
		case SIGXCPU: 
			error("job exceeded timelimit"); 
		case SIGCHLD: 
			break;
		default: 
			debug("slurmd got signal %d", sig); break;
			break;
		}
	} while (sig != SIGCHLD);

	return 1;
}

/*
 *  Collect a single task's exit status and send it up to the
 *   slurmd job manager. 
 *
 *  Returns the number of tasks actually reaped
 *   (i.e. 1 for success, 0 for failure)
 *
 */
static int
_reap_task(slurmd_job_t *job)
{
	pid_t pid;
	int   status = 0;

	if ((pid = waitpid(-1, &status, WNOHANG)) > (pid_t) 0) 
		return _send_exit_status(job, pid, status);

	if (pid == (pid_t) 0)
		return 0;

	/* 
	 *  Else waitpid returned error:
	 */
	switch (errno) {
	case ECHILD: 
		/* 
		 *  waitpid() may return "No child processes." if
		 *   a debugger has attached and is tracing all tasks.
		 *   (gnats:217)
		 *
		 *   Note: This should be a non-issue due to _child_exited.
		 */
		break;
	default:
		error("waitpid: %m");
		break;
	}

	return 0; 
}


/* wait for N tasks to exit, reporting exit status back to slurmd mgr
 * process over file descriptor fd.
 *
 */
static void
_wait_for_all_tasks(slurmd_job_t *job)
{
	int active = job->ntasks;

	/*
	 *  While there are still active tasks, block waiting
	 *   for SIGCHLD, then reap as many children as possible.
	 */

	while ((active > 0) && _child_exited()) 
		while (_reap_task(job) && --active) {;}

	return;
}

static int
_wid(int n)
{
	int width = 1;
	n--;
	while (n /= 10) width++;
	return width;
}

/*
 *  Send exit status for local pid `pid' to slurmd manager process.
 *    Returns 1 if pid corresponds to a local taskid, 0 otherwise.
 *
 */ 
static int 
_send_exit_status(slurmd_job_t *job, pid_t pid, int status)
{
	exit_status_t e;
	int           retry = 1;
	int           rc  = 0;
	int           len = sizeof(e);
	int           fd  = job->fdpair[1];

	if ((e.taskid = _local_taskid(job, pid)) < 0)
		return 0;
	e.status = status;

	verbose("task %*d (%ld) exited status 0x%04x %M", 
	        _wid(job->ntasks), e.taskid, pid, status);

	while (((rc = fd_write_n(fd, &e, len)) <= 0) && retry--) {;}

	if (rc < len) 
		error("failed to send task %d exit msg: rc=%d: %s",
		      e.taskid, rc, (rc < 0 ? slurm_strerror(errno) : "")); 
	/* 
	 * Return 1 on failure to notify slurm mgr -- this will
	 *  allow current process to be aware that the task exited anyway
	 */
	return 1;
}

/*
 *  Returns local taskid corresponding to `pid' or SLURM_ERROR
 *    if no local task has pid.
 */
static int
_local_taskid(slurmd_job_t *job, pid_t pid)
{
	int i;
	for (i = 0; i < job->ntasks; i++) {
		if (job->task[i]->pid == pid) 
			return i;
	}
	return SLURM_ERROR;
}

static int
_setup_env(slurmd_job_t *job, int taskid)
{
	int cnt = (int) job->envc;
	task_info_t *t = job->task[taskid];

	if (setenvpf(&job->env, &cnt, "SLURM_JOBID=%d",  job->jobid)  < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "SLURM_NODEID=%d", job->nodeid) < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "SLURM_PROCID=%d", t->gid     ) < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "SLURM_NNODES=%d", job->nnodes) < 0)
		return -1;
	if (setenvpf(&job->env, &cnt, "SLURM_NPROCS=%d", job->nprocs) < 0)
		return -1;

	job->envc = (uint16_t) cnt;

	return SLURM_SUCCESS;
}

/*
 * Prepare task for parallel debugger attach
 */
static void 
_pdebug_trace_process(slurmd_job_t *job, pid_t pid)
{
#if HAVE_TOTALVIEW
	/*  If task to be debugged, wait for it to stop via
	 *  child's ptrace(PTRACE_TRACEME), then SIGSTOP, and 
	 *  ptrace(PTRACE_DETACH). This requires a kernel patch,
	 *  which you probably already have in place for TotalView:
	 *  http://hypermail.idiosynkrasia.net
	 *        /linux-kernel/archived/2001/week51/1193.html 
	 */

	if (job->task_flags & TASK_TOTALVIEW_DEBUG) {
		int status;
		waitpid(pid, &status, WUNTRACED);
		if (kill(pid, SIGSTOP) < 0)
			error("kill(%ld): %m", (long) pid);
		if (ptrace(PTRACE_DETACH, (long) pid, NULL, NULL))
			error("ptrace(%ld): %m", (long) pid);
	}
#endif /* HAVE_TOTALVIEW */
}

/*
 * Stop current task on exec() for connection from a parallel debugger
 */
static void
_pdebug_stop_current(slurmd_job_t *job)
{
#if HAVE_TOTALVIEW
	/* 
	 * Stop the task on exec for TotalView to connect 
	 */
	if ( (job->task_flags & TASK_TOTALVIEW_DEBUG)
	     && (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) )
		error("ptrace: %m");
#endif
}


static int
_unblock_all_signals(void)
{
	sigset_t set;
	if (sigfillset(&set)) {
		error("sigfillset: %m");
		return SLURM_ERROR;
	}
	if (sigprocmask(SIG_UNBLOCK, &set, NULL)) {
		error("sigprocmask: %m");
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}


