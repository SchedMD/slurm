/*****************************************************************************\
 * src/slurmd/mgr.c - job manager functions for slurmd
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <src/common/log.h>

#include <src/slurmd/mgr.h>
#include <src/slurmd/io.h>
#include <src/slurmd/shm.h>
#include <src/slurmd/interconnect.h>

static int _unblock_all_signals(void);
static int _send_exit_msg(int rc, task_info_t *t);

/* Launch a job step on this node
 */
int
mgr_launch_tasks(launch_tasks_request_msg_t *msg)
{
	slurmd_job_t *job;

	log_reinit();

	/* New process, so we must reinit shm */
	if (shm_init() < 0) 
		return SLURM_ERROR;
	if (!(job = job_create(msg)))
		return SLURM_ERROR;
	slurmd_run_job(job); 
	debug2("%ld returned from slurmd_run_job()", getpid());
	shm_fini();
	exit(0); 
	return 0; /* not reached */
}

/* Instance of a slurmd "job" or job step:
 * We run:
 *  interconnect_prepare()       : prepare node for interconnect (if any)
 *  interconnect_init()          : initialize interconnect on node
 *  fork() N tasks --> wait() --> interconnect_fini()
 *   \
 *    `--> interconnect_attach() : attach each proc to interconnect
 *         interconnect_env()    : setup child environment 
 *         exec()
 */
void
slurmd_run_job(slurmd_job_t *job)
{
	int rc;
	/* Insert job info into shared memory */
	job_update_shm(job);

	if (interconnect_init(job) == SLURM_ERROR) {
		error("interconnect_init failed");
		rc = 2;
		goto done;
	}

	/* initialize I/O, connect back to srun, and spawn thread for
	 * forwarding I/O.
	 */
	/* Option: connect slurmd stderr to srun local task 0: stderr? */
	if (io_spawn_handler(job) == SLURM_ERROR) {
		error("unable to spawn io handler");
		rc = 3;
		goto done;
	}

	job_launch_tasks(job);
	verbose("job %d.%d complete, waiting on IO", job->jobid, job->stepid);
	io_close_all(job);
	pthread_join(job->ioid, NULL);
	verbose("job %d.%d IO complete", job->jobid, job->stepid);

  done:
	interconnect_fini(job); /* ignore errors        */
	verbose("removing job %d.%d from system", job->jobid, job->stepid);
	job_delete_shm(job);    /* again, ignore errors */
	return;
}

static void
xsignal(int signo, void (*handler)(int))
{
	struct sigaction sa, old_sa;

	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, signo);
	sa.sa_flags = 0;
	sigaction(signo, &sa, &old_sa);
}

static void
_wait_for_all_tasks(slurmd_job_t *job)
{
	int waiting = job->ntasks;
	int i;

	while (waiting > 0) {
		int status;
		pid_t pid = waitpid(0, &status, 0);
		if (pid < (pid_t) 0) {
			error("waitpid: %m");
			/* job_cleanup() */
		}
		for (i = 0; i < job->ntasks; i++) {
			if (job->task[i]->pid == pid) {
				_send_exit_msg(status, job->task[i]);
				waiting--;
			}
		}
	}
	return;
}

static void
_task_exec(slurmd_job_t *job, int i)
{
	struct passwd *pwd;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	io_prepare_child(job->task[i]);

	/* 
	 * Reinitialize slurm log facility to send errors back to client 
	 */
	log_init("slurmd", opts, 0, NULL);

	if (_unblock_all_signals() == SLURM_ERROR) {
		error("unable to unblock signals");
		exit(1);
	}

	/* attach to interconnect */
	if (interconnect_attach(job, i) < 0) {
		error("interconnect attach failed: %m");
		exit(1);
	}

	if (interconnect_env(job, i) < 0) {
		error("interconnect_env: %m");
	}

	if ((pwd = getpwuid(job->uid)) == NULL) {
		error("User not found on node");
		exit(1);
	}

	if (setgid(pwd->pw_gid) < 0) {
		error("setgid: %m");
		exit(1);
	}

	if (initgroups(pwd->pw_name, pwd->pw_gid) < 0) {
		;
		/* error("initgroups: %m"); */
	}

	if (setuid(job->uid) < 0) {
		error("setuid: %m");
		exit(1);
	}

	if (chdir(job->cwd) < 0) {
		error("couldn't chdir to `%s': %m: going to /tmp instead",
				job->cwd); 
		if (chdir("/tmp") < 0) {
			error("couldn't chdir to /tmp either. dying.");
			exit(1);
		}
	}

	/* exec the cmdline */
	execve(job->argv[0], job->argv, job->env);

	/* error and clean up if execve() returns:
	 */
	error("execve(): %s: %m", job->argv[0]); 
	exit(errno);
}

void 
job_launch_tasks(slurmd_job_t *job)
{
	pid_t sid;
	int i;

	debug3("%ld entered job_launch_tasks", getpid());

	xsignal(SIGPIPE, SIG_IGN);

	if ((sid = setsid()) < (pid_t) 0) 
		error("setsid: %m");

	if (shm_update_step_sid(job->jobid, job->stepid, sid) < 0)
		error("shm_update_step_sid: %m");
	
	debug2("invoking %d tasks for job %d.%d", job->ntasks, job->jobid,
			job->stepid);

	for (i = 0; i < job->ntasks; i++) {
		task_t t;
		verbose("going to fork task %d", i);
		t.id = i;
		t.global_id = job->task[i]->gid;
		t.ppid      = getpid();

		if ((t.pid = fork()) < 0) {
			error("fork: %m");
			exit(1);
			/* job_cleanup() */
		} else if (t.pid == 0)
			break;

		/* Parent continues loop: */

		job->task[i]->pid = t.pid;

		debug2("%ld: forked child process %ld for task %d", 
				getpid(), (long) t.pid, i);  
		debug2("going to add task %d to shm", i);
		if (shm_add_task(job->jobid, job->stepid, &t) < 0)
			error("shm_add_task: %m");
		debug2("task %d added to shm", i);

	}

	if (i == job->ntasks) 
		_wait_for_all_tasks(job);
	else
		_task_exec(job, i);

	return;
}

static int 
_send_exit_msg(int rc, task_info_t *t)
{
	slurm_msg_t resp;
	task_exit_msg_t msg;
	ListIterator i;
	srun_info_t *srun;

       	msg.return_code = rc;
	msg.task_id     = t->gid;
	resp.data       = &msg;
	resp.msg_type   = MESSAGE_TASK_EXIT;

	i = list_iterator_create(t->srun_list);
	while ((srun = list_next(i))) {
		resp.address = srun->resp_addr;
		slurm_send_only_node_msg(&resp);
	}
	list_iterator_destroy(i);

	return SLURM_SUCCESS;
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
