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
#include <assert.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include <slurm/slurm_errno.h>

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/setenvpf.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"

#include "src/slurmd/smgr.h"
#include "src/slurmd/ulimits.h"
#include "src/slurmd/io.h"

/*
 * Static list of signals to block in this process
 *  *Must be zero-terminated*
 */
static int smgr_sigarray[] = {
	SIGINT,  SIGTERM, SIGCHLD, SIGUSR1, 
	SIGUSR2, SIGTSTP, SIGXCPU,
	SIGQUIT, SIGPIPE, 
	SIGALRM, 0
};

/*
 * Static prototype definitions.
 */
static void  _session_mgr(slurmd_job_t *job);
static int   _exec_all_tasks(slurmd_job_t *job);
static void  _exec_task(slurmd_job_t *job, int i);
static int   _become_user(slurmd_job_t *job);
static void  _make_tmpdir(slurmd_job_t *job);
static int   _child_exited(void);
static void  _wait_for_all_tasks(slurmd_job_t *job);
static int   _local_taskid(slurmd_job_t *job, pid_t pid);
static int   _send_exit_status(slurmd_job_t *job, pid_t pid, int status);
static char *_signame(int signo);
static void  _cleanup_file_descriptors(slurmd_job_t *job);
static int   _setup_env(slurmd_job_t *job, int taskid);
static void  _setup_spawn_io(slurmd_job_t *job);

/* parallel debugger support */
static void  _pdebug_trace_process(slurmd_job_t *job, pid_t pid);
static void  _pdebug_stop_current(slurmd_job_t *job);
#ifdef HAVE_PTRACE64
#  define _PTRACE(r,p,a,d) ptrace64((r),(long long)(p),(long long)(a),(d),NULL)
#else
#  ifdef PTRACE_FIVE_ARGS
#    define _PTRACE(r,p,a,d) ptrace((r),(p),(a),(d),NULL)
#  else
#    define _PTRACE(r,p,a,d) ptrace((r),(p),(a),(void *)(d))
#  endif
#endif

/*
 *  Dummy handler for SIGCHLD. 
 *
 *  We need this handler to work around what may be a bug in
 *   RedHat 9 based kernel/glibc. If no handler is installed for
 *   any signal that is, by default, ignored, then the signal
 *   will not be delivered even if that signal is currently blocked.
 *
 *  Since we block SIGCHLD, this handler should never actually
 *   get invoked. Assert this fact.
 */
static void _chld_handler(int signo) { assert(signo != SIGCHLD); }


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
	 * Install dummy SIGCHLD handler (see comments above)
	 */
	xsignal(SIGCHLD, &_chld_handler);

	/*
	 * Call interconnect_init() before becoming user
	 */
	if (!job->batch && 
	    (interconnect_init(job->switch_job, job->uid) < 0)) {
		/* error("interconnect_init: %m"); already logged */
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

	if ((!job->spawn_task) && (set_user_limits(job) < 0)) {
		debug("Unable to set user limits");
		exit(5);
	}

	_make_tmpdir(job);

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

	if (!job->batch && 
	    (interconnect_fini(job->switch_job) < 0)) {
		error("interconnect_fini: %m");
		exit(1);
	}

	exit(SLURM_SUCCESS);
}

static void _setup_spawn_io(slurmd_job_t *job)
{
	srun_info_t *srun;
	int fd = -1;

	srun = list_peek(job->sruns);
	xassert(srun);
	if ((fd = (int) slurm_open_stream(&srun->ioaddr)) < 0) {
		error("connect io: %m");
		exit(1);
	}
	(void) close(STDIN_FILENO);
	(void) close(STDOUT_FILENO);
	(void) close(STDERR_FILENO);
	if ((dup(fd) != 0) || (dup(fd) != 1) || (dup(fd) != 2)) {
		error("dup: %m");
		exit(1);
	}
	(void) close(fd);
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
	if (setgid(job->gid) < 0) {
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

	/*
	 *  Block signals for this process before exec-ing
	 *   user tasks. Esp. important to block SIGCHLD until
	 *   we're ready to handle it.
	 */
	if (xsignal_block(smgr_sigarray) < 0)
		return error ("Unable to block signals");

	for (i = 0; i < job->ntasks; i++) {
		pid_t pid = fork();

		if (pid < 0) {
			error("fork: %m");
			return SLURM_ERROR;
		} else if (pid == 0)  /* child */
			_exec_task(job, i);

		/* Parent continues: 
		 */
		verbose ("task %lu (%lu) started %M", 
			(unsigned long) job->task[i]->gid, 
			(unsigned long) pid); 

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
	if (xsignal_unblock(smgr_sigarray) < 0) {
		error("unable to unblock signals");
		exit(1);
	}

	/*
	 * Move this process into new pgrp within this session
	 */
	if (setpgid (0, i ? job->task[0]->pid : 0) < 0) 
		error ("Unable to put task %d into pgrp %ld: %m", 
		       i, job->task[0]->pid);

	if (!job->batch) {
		if (interconnect_attach(job->switch_job, &job->env,
				job->nodeid, (uint32_t) i, job->nnodes,
				job->nprocs, job->task[i]->gid) < 0) {
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
	if (job->spawn_task)
		_setup_spawn_io(job);
	else
		io_prepare_child(job->task[i]);

	execve(job->argv[0], job->argv, job->env);

	/* 
	 * error() and clean up if execve() returns:
	 */
	error("execve(): %s: %m", job->argv[0]); 
	exit(errno);
}

/*
 *  Translate a signal number to recognizable signal name.
 *    Returns signal name or "signal <num>" 
 */
static char *
_signame(int signo)
{
	int i;
	static char str[10];
	static struct {
		int s_num;
		char * s_name;
	} sigtbl[] = {   
		{ 1, "SIGHUP" }, { 2, "SIGINT" }, { 3, "SIGQUIT"},
		{ 6, "SIGABRT"}, { 10,"SIGUSR1"}, { 12,"SIGUSR2"},
		{ 13,"SIGPIPE"}, { 14,"SIGALRM"}, { 15,"SIGTERM"},
		{ 17,"SIGCHLD"}, { 18,"SIGCONT"}, { 19,"SIGSTOP"},
		{ 20,"SIGTSTP"}, { 21,"SIGTTIN"}, { 22,"SIGTTOU"},
		{ 23,"SIGURG" }, { 24,"SIGXCPU"}, { 25,"SIGXFSZ"},
		{ 0, NULL}
	};

	for (i = 0; ; i++) {
		if ( sigtbl[i].s_num == signo )
			return sigtbl[i].s_name;
		if ( sigtbl[i].s_num == 0 )
			break;
	}

	snprintf(str, 9, "signal %d", signo);
	return str;
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

	for (;;) {
		xsignal_sigset_create(smgr_sigarray, &set);
		sigwait(&set, &sig);

		debug2 ("slurmd caught %s", _signame(sig));

		switch (sig) {
		 case SIGCHLD: return 1;
		 case SIGXCPU: error ("job exceeded timelimit"); break;
		 default:      break;      
		}
	}
	/* NOTREACHED */
	return 0;
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

	if ((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > (pid_t) 0) 
		return _send_exit_status(job, pid, status);

	if ((pid < 0) && (errno != ECHILD))
		error  ("waitpid: %m");
	else
		debug2 ("waitpid(-1, WNOHANG) returned 0");

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
 *   Returns 1 if pid corresponding to a local taskid has exited.
 *   Returns 0 if pid is not a tracked task, or if task has not
 *    exited (e.g. task has stopped).
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

	/*
	 *  Report tasks that are stopped via debug log,
	 *   but return 0 since the task has not exited.
	 */
	if ( WIFSTOPPED(status) ) {
		verbose ( "task %*d (%ld) stopped by %s %M",
		          _wid(job->ntasks), 
			  job->task[e.taskid]->gid,
			  pid, 
			  _signame(WSTOPSIG(status))
			);
		return 0;
	}

	verbose ( "task %*d (%ld) exited status 0x%04x %M", 
	          _wid(job->ntasks), 
	          job->task[e.taskid]->gid, 
	          pid, 
	          status
	        );

	while (((rc = fd_write_n(fd, &e, len)) <= 0) && retry--) {;}

	if (rc < len) 
		error("failed to send task %lu exit msg: rc=%d: %s",
			(unsigned long) e.taskid, rc, 
			(rc < 0 ? slurm_strerror(errno) : "")); 
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
	debug("unknown pid %ld exited status 0x%04x %M", (long) pid);
	return SLURM_ERROR;
}

/*
 *  Set task-specific environment variables
 */
static int
_setup_env(slurmd_job_t *job, int taskid)
{
	task_info_t *t = job->task[taskid];

	if (setenvpf(&job->env, "SLURM_NODEID",       "%d", job->nodeid) < 0)
		return -1;
	if (setenvpf(&job->env, "SLURM_CPUS_ON_NODE", "%d", job->cpus) < 0)
		return -1;
	if (setenvpf(&job->env, "SLURM_PROCID",       "%d", t->gid     ) < 0)
		return -1;

	return SLURM_SUCCESS;
}

static void
_make_tmpdir(slurmd_job_t *job)
{
	char *tmpdir;

	if (!(tmpdir = getenvp(job->env, "TMPDIR")))
		return;

	if ((mkdir(tmpdir, 0700) < 0) && (errno != EEXIST))
		error ("Unable to create TMPDIR [%s]: %m", tmpdir);

	return;
}


/*
 * Prepare task for parallel debugger attach
 */
static void 
_pdebug_trace_process(slurmd_job_t *job, pid_t pid)
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
		if (kill(pid, SIGSTOP) < 0)
			error("kill(%lu): %m", (unsigned long) pid);
		if (_PTRACE(PTRACE_DETACH, pid, NULL, 0))
			error("ptrace(%lu): %m", (unsigned long) pid);
	}
}

/*
 * Stop current task on exec() for connection from a parallel debugger
 */
static void
_pdebug_stop_current(slurmd_job_t *job)
{
	/* 
	 * Stop the task on exec for TotalView to connect 
	 */
	if ( (job->task_flags & TASK_PARALLEL_DEBUG)
	     && (_PTRACE(PTRACE_TRACEME, 0, NULL, 0) < 0) )
		error("ptrace: %m");
}
