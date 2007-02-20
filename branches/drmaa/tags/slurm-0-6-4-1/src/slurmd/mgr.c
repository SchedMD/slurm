/*****************************************************************************\
 *  src/slurmd/mgr.c - job manager functions for slurmd
 *  $Id$
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
#  include "config.h"
#endif

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <slurm/slurm_errno.h>

#include "src/common/cbuf.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/fd.h"
#include "src/common/safeopen.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/switch.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/util-net.h"

#include "src/slurmd/mgr.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/setproctitle.h"
#include "src/slurmd/task.h"
#include "src/slurmd/io.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/proctrack.h"
#include "src/slurmd/pdebug.h"

/* 
 * Map session manager exit status to slurm errno:
 * Keep in sync with smgr.c exit codes.
 */
static int exit_errno[] = 
{       0, 
	ESLURM_INTERCONNECT_FAILURE, 
	ESLURMD_SET_UID_OR_GID_ERROR,
	ESLURMD_SET_SID_ERROR,
	ESCRIPT_CHDIR_FAILED,
	-1,
	ESLURMD_EXECVE_FAILED
};

#define MAX_SMGR_EXIT_STATUS 6

#define RETRY_DELAY 15		/* retry every 15 seconds */
#define MAX_RETRY   240		/* retry 240 times (one hour max) */

/*
 *  List of signals to block in this process
 */
static int mgr_sigarray[] = {
	SIGINT,  SIGTERM, SIGTSTP,
	SIGQUIT, SIGPIPE, SIGUSR1,
	SIGUSR2, SIGALRM, 0
};


/* 
 * Prototypes
 */

/* 
 * Job manager related prototypes
 */
static void _send_launch_failure(launch_tasks_request_msg_t *, 
                                 slurm_addr *, int);
static int  _job_mgr(slurmd_job_t *job);
static int  _fork_all_tasks(slurmd_job_t *job);
static int  _become_user(slurmd_job_t *job);
static void _set_job_log_prefix(slurmd_job_t *job);
static int  _setup_io(slurmd_job_t *job);
static int  _setup_spawn_io(slurmd_job_t *job);
static int  _drop_privileges(struct passwd *pwd);
static int  _reclaim_privileges(struct passwd *pwd);
static void _send_launch_resp(slurmd_job_t *job, int rc);
static void _slurmd_job_log_init(slurmd_job_t *job);
static void _wait_for_io(slurmd_job_t *job);
static void _handle_attach_req(slurmd_job_t *job);
static int  _send_exit_msg(slurmd_job_t *job, uint32_t *tid, int n, 
		int status);
static void _set_unexited_task_status(slurmd_job_t *job, int status);
static int  _send_pending_exit_msgs(slurmd_job_t *job);
static void _kill_running_tasks(slurmd_job_t *job);
static void _wait_for_all_tasks(slurmd_job_t *job);
static int  _wait_for_any_task(slurmd_job_t *job, bool waitflag);

static void _setargs(slurmd_job_t *job);

static void _random_sleep(slurmd_job_t *job);
static char *_sprint_task_cnt(batch_job_launch_msg_t *msg);
/*
 * Batch job mangement prototypes:
 */
static char * _make_batch_dir(slurmd_job_t *job);
static char * _make_batch_script(batch_job_launch_msg_t *msg, char *path);
static int    _complete_job(uint32_t jobid, uint32_t stepid, 
			    int err, int status);

static slurmd_job_t *reattach_job;

/*
 * SIGHUP signal handler.  A SIGHUP is a message from the main slurmd
 * that a reattach request needs to be processed.
 */
static void _hup_handler(int sig)
{
	if ((sig == SIGHUP)
	    && (reattach_job != NULL))
		_handle_attach_req(reattach_job);
}

/*
 * Launch an job step on the current node
 */
extern int
mgr_launch_tasks(launch_tasks_request_msg_t *msg, slurm_addr *cli,
		 slurm_addr *self)
{
	slurmd_job_t *job = NULL;
	
	if (!(job = job_create(msg, cli))) {
		_send_launch_failure (msg, cli, errno);
		return SLURM_ERROR;
	}

	_set_job_log_prefix(job);

	_setargs(job);
	
	job->envtp->cli = cli;
	job->envtp->self = self;
	
	if (_job_mgr(job) < 0) 
		return SLURM_ERROR;
	
		
	job_destroy(job);

	return SLURM_SUCCESS;
}

/*
 * Launch a batch job script on the current node
 */
int
mgr_launch_batch_job(batch_job_launch_msg_t *msg, slurm_addr *cli)
{
	int           rc     = 0;
	int           status = 0;
	uint32_t      jobid  = msg->job_id;
	slurmd_job_t *job = NULL;
	char         *batchdir = NULL;
	char       buf[1024];
	hostlist_t hl = hostlist_create(msg->nodes);
	if (!hl)
		return SLURM_ERROR;
		
	hostlist_ranged_string(hl, 1024, buf);
	
	if (!(job = job_batch_job_create(msg))) {
		/*
		 *  Set "job" status to returned errno and cleanup job.
		 */
		status = errno;
		goto cleanup;
	}

	_set_job_log_prefix(job);

	_setargs(job);

	if ((batchdir = _make_batch_dir(job)) == NULL) 
		goto cleanup1;

	xfree(job->argv[0]);

	if ((job->argv[0] = _make_batch_script(msg, batchdir)) == NULL)
		goto cleanup2;
	
	job->envtp->nprocs = msg->nprocs;
	job->envtp->select_jobinfo = msg->select_jobinfo;
	job->envtp->nhosts = hostlist_count(hl);
	hostlist_destroy(hl);
	job->envtp->nodelist = buf;
	job->envtp->task_count = _sprint_task_cnt(msg);
	
	status = _job_mgr(job);
	
   cleanup2:
	if (job->argv[0] && (unlink(job->argv[0]) < 0))
		error("unlink(%s): %m", job->argv[0]);
   cleanup1:
	if (batchdir && (rmdir(batchdir) < 0))
		error("rmdir(%s): %m",  batchdir);
	xfree(batchdir);
   cleanup :
	if (job->stepid == NO_VAL) 
		verbose("job %u completed with slurm_rc = %d, job_rc = %d", 
			jobid, rc, status);
	else
		verbose("job %u.%u completed with slurm_rc = %d, job_rc = %d", 
			jobid, job->stepid, rc, status);
	_complete_job(jobid, job->stepid, rc, status);
	return 0; 
}

/*
 * Spawn a task / job step on the current node
 */
int
mgr_spawn_task(spawn_task_request_msg_t *msg, slurm_addr *cli,
	       slurm_addr *self)
{
	slurmd_job_t *job = NULL;
	
	if (!(job = job_spawn_create(msg, cli)))
		return SLURM_ERROR;

	job->spawn_task = true;
	_set_job_log_prefix(job);

	_setargs(job);
	
	job->envtp->cli = cli;
	job->envtp->self = self;
	
	if (_job_mgr(job) < 0) 
		return SLURM_ERROR;
	
	job_destroy(job);

	return SLURM_SUCCESS;
}

/*
 * Run a prolog or epilog script. Sets environment variables:
 *   SLURM_JOBID = jobid, SLURM_UID=uid, and
 *   MPIRUN_PARTITION=bgl_part_id (if not NULL)
 * Returns -1 on failure. 
 */
extern int 
run_script(bool prolog, const char *path, uint32_t jobid, uid_t uid, 
		char *bgl_part_id)
{
	int status;
	pid_t cpid;
	char *name = prolog ? "prolog" : "epilog";

	if (path == NULL || path[0] == '\0')
		return 0;

	debug("[job %u] attempting to run %s [%s]", jobid, name, path);

	if (access(path, R_OK | X_OK) < 0) {
		debug("Not running %s [%s]: %m", name, path);
		return 0;
	}

	if ((cpid = fork()) < 0) {
		error ("executing %s: fork: %m", name);
		return -1;
	}
	if (cpid == 0) {
		char *argv[4];
		char **env;

		env = xmalloc(sizeof(char *));

		argv[0] = xstrdup(path);
		argv[1] = NULL;

		env[0]  = NULL;
		setenvf(&env, "SLURM_JOBID", "%u", jobid);
		setenvf(&env, "SLURM_UID",   "%u", uid);
		if (bgl_part_id)
			setenvf(&env, "MPIRUN_PARTITION", "%s", bgl_part_id);

		execve(path, argv, env);
		error("help! %m");
		exit(127);
	}

	do {
		if (waitpid(cpid, &status, 0) < 0) {
			if (errno == EINTR)
				continue;
			error("waidpid: %m");
			return 0;
		} else
			return status;
	} while(1);

	/* NOTREACHED */
}


static void
_set_job_log_prefix(slurmd_job_t *job)
{
	char buf[256];

	if (job->jobid > MAX_NOALLOC_JOBID) 
		return;

	if ((job->jobid >= MIN_NOALLOC_JOBID) || (job->stepid == NO_VAL)) 
		snprintf(buf, sizeof(buf), "[%u]", job->jobid);
	else
		snprintf(buf, sizeof(buf), "[%u.%u]", job->jobid, job->stepid);

	log_set_fpfx(buf);
}

static int
_setup_io(slurmd_job_t *job)
{
	int            rc   = 0;
	struct passwd *spwd = NULL;

	/* 
	 * Save current UID/GID
	 */
	if (!(spwd = getpwuid(geteuid()))) {
		error("getpwuid: %m");
		return ESLURMD_IO_ERROR;
	}

	if (io_spawn_handler(job) < 0)
		return ESLURMD_IO_ERROR;

	/*
	 * Initialize log facility to copy errors back to srun
	 */
	_slurmd_job_log_init(job);

	/*
	 * Temporarily drop permissions, initialize IO clients
	 * (open files/connections for IO, etc), then reclaim privileges.
	 */
	if (_drop_privileges(job->pwd) < 0)
		return ESLURMD_SET_UID_OR_GID_ERROR;

	rc = io_prepare_clients(job);

	if (_reclaim_privileges(spwd) < 0)
		error("sete{u/g}id(%lu/%lu): %m", 
		      (u_long) spwd->pw_uid, (u_long) spwd->pw_gid);

#ifndef NDEBUG
#  ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#  endif /* PR_SET_DUMPABLE */
#endif   /* !NDEBUG         */

	if (rc < 0) 
		return ESLURMD_IO_ERROR;

	return SLURM_SUCCESS;
}


static int
_setup_spawn_io(slurmd_job_t *job)
{
	_slurmd_job_log_init(job);

#ifndef NDEBUG
#  ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#  endif /* PR_SET_DUMPABLE */
#endif   /* !NDEBUG         */

	return SLURM_SUCCESS;
}


static void
_random_sleep(slurmd_job_t *job)
{
	long int delay = 0;
	long int max   = (3 * job->nnodes); 

	srand48((long int) (job->jobid + job->nodeid));

	delay = lrand48() % ( max + 1 );
	debug3("delaying %dms", delay);
	poll(NULL, 0, delay);
}

/*
 * Send task exit message for n tasks. tid is the list of _global_
 * task ids that have exited
 */
static int
_send_exit_msg(slurmd_job_t *job, uint32_t *tid, int n, int status)
{
	slurm_msg_t     resp;
	task_exit_msg_t msg;
	ListIterator    i       = NULL;
	srun_info_t    *srun    = NULL;

	debug3("sending task exit msg for %d tasks", n);

	msg.task_id_list = tid;
	msg.num_tasks    = n;
	msg.return_code  = status;
	resp.data        = &msg;
	resp.msg_type    = MESSAGE_TASK_EXIT;

	/*
	 *  XXX Hack for TCP timeouts on exit of large, synchronized
	 *  jobs. Delay a random amount if job->nnodes > 100
	 */
	if (job->nnodes > 100) 
		_random_sleep(job);

	/*
	 * XXX: Should srun_list be associated with each task?
	 */
	i = list_iterator_create(job->sruns);
	while ((srun = list_next(i))) {
		resp.address = srun->resp_addr;
		if (resp.address.sin_family != 0)
			slurm_send_only_node_msg(&resp);
	}
	list_iterator_destroy(i);

	return SLURM_SUCCESS;
}


/* 
 * Executes the functions of the slurmd job manager process,
 * which runs as root and performs shared memory and interconnect
 * initialization, etc.
 *
 * Returns 0 if job ran and completed successfully.
 * Returns errno if job startup failed.
 *
 */
static int 
_job_mgr(slurmd_job_t *job)
{
	int rc = 0;

	job->jmgr_pid = getpid();
	debug3("Entered job_mgr for %d.%d pid=%lu",
	       job->jobid, job->stepid, (unsigned long) job->jmgr_pid);
	
	if (shm_init(false) < 0)
		goto fail0;

	if (job_update_shm(job) < 0) {
		if (errno == ENOSPC) 
			rc = ESLURMD_TOOMANYSTEPS;
		else if (errno == EEXIST)
			rc = ESLURMD_STEP_EXISTS;
		goto fail0;
	}

	if (!job->batch && 
	    (interconnect_preinit(job->switch_job) < 0)) {
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto fail1;
	}

/* 	xsignal_block(mgr_sigarray); */
/* 	xsignal(SIGHUP, _hup_handler); */

	if (job->spawn_task)
		rc = _setup_spawn_io(job);
	else
		rc = _setup_io(job);
	if (rc)
		goto fail2;

	g_slurmd_jobacct_jobstep_launched(job);

	/* Call interconnect_init() before becoming user */
	if (!job->batch && 
	    (interconnect_init(job->switch_job, job->uid) < 0)) {
		/* error("interconnect_init: %m"); already logged */
		goto fail2;
	}

	if (_fork_all_tasks(job) < 0) {
		debug("_fork_all_tasks failed");
		goto fail2;
	}

	xsignal_block(mgr_sigarray);
	reattach_job = job;
	xsignal(SIGHUP, _hup_handler);

	if (job_update_state(job, SLURMD_JOB_STARTED) < 0)
		goto fail2;

	/* Send job launch response with list of pids */
	_send_launch_resp(job, 0);

	/* tell the accountants to start counting */
	g_slurmd_jobacct_smgr();

	_wait_for_all_tasks(job);

	if (!job->batch && 
	    (interconnect_fini(job->switch_job) < 0)) {
		error("interconnect_fini: %m");
		exit(1);
	}

	job_update_state(job, SLURMD_JOB_ENDING);

    fail2:
	/*
	 *  First call interconnect_postfini() - In at least one case,
	 *    this will clean up any straggling processes. If this call
	 *    is moved behind wait_for_io(), we may block waiting for IO
	 *    on a hung process.
	 */
	if (!job->batch) {
		_kill_running_tasks(job);
		if (interconnect_postfini(job->switch_job, job->jmgr_pid,
				job->jobid, job->stepid) < 0)
			error("interconnect_postfini: %m");
	}

	/*
	 * Wait for io thread to complete (if there is one)
	 */
	if (!job->spawn_task)
		_wait_for_io(job);

	job_update_state(job, SLURMD_JOB_COMPLETE);
	g_slurmd_jobacct_jobstep_terminated(job);

    fail1:
	job_delete_shm(job);
	shm_fini();
    fail0:
	/* If interactive job startup was abnormal, 
	 * be sure to notify client.
	 */
	if (rc != 0) {
		error("_job_mgr exiting abnormally, rc = %d", rc);
		_send_launch_resp(job, rc);
	}

	return(rc);
}


/* fork and exec N tasks
 */ 
static int
_fork_all_tasks(slurmd_job_t *job)
{
	int i;
	int *writefds; /* array of write file descriptors */
	int *readfds; /* array of read file descriptors */
	uint32_t cont_id;
	int fdpair[2];

	xassert(job != NULL);

	if (slurm_container_create(job) == SLURM_ERROR) {
		error("slurm_container_create: %m");
		exit(3);
	}

	/*
	 * Pre-allocate a pipe for each of the tasks
	 */
	debug3("num tasks on this node = %d", job->ntasks);
	writefds = (int *) xmalloc (job->ntasks * sizeof(int));
	if (!writefds)
		error("writefds xmalloc failed!");
	readfds = (int *) xmalloc (job->ntasks * sizeof(int));
	if (!readfds)
		error("readfds xmalloc failed!");
	for (i = 0; i < job->ntasks; i++) {
		fdpair[0] = -1; fdpair[1] = -1;
		if (pipe (fdpair) < 0)
			error ("exec_all_tasks: pipe: %m");
		debug("New fdpair[0] = %d, fdpair[1] = %d", fdpair[0], fdpair[1]);
		fd_set_close_on_exec(fdpair[0]);
		fd_set_close_on_exec(fdpair[1]);
		readfds[i] = fdpair[0];
		writefds[i] = fdpair[1];
	}

	/*
	 * Fork all of the task processes.
	 */
	for (i = 0; i < job->ntasks; i++) {
		pid_t pid;
		task_t task;

		if ((pid = fork ()) < 0) {
			error("fork: %m");
			return SLURM_ERROR;
		} else if (pid == 0)  { /* child */
			int j;
#ifdef HAVE_AIX
			(void) mkcrid(0);
#endif
			/* Close file descriptors not needed by the child */
			for (j = 0; j < job->ntasks; j++) {
				close(writefds[j]);
				if (j > i)
					close(readfds[j]);
			}

			if (_become_user(job) < 0) 
				exit(2);

			log_fini();

			exec_task(job, i, readfds[i]);
		}

		/*
		 * Parent continues: 
		 */
		close(readfds[i]);
		verbose ("task %lu (%lu) started %M", 
			(unsigned long) job->task[i]->gtid, 
			(unsigned long) pid); 

		job->task[i]->pid = pid;
		if (i == 0)
			job->pgid = pid;
		/*
		 * Put this task in the step process group
		 */
		if (setpgid (pid, job->pgid) < 0)
			error ("Unable to put task %d (pid %ld) into pgrp %ld",
			       i, pid, job->pgid);

		if (slurm_container_add(job, pid) == SLURM_ERROR) {
			error("slurm_container_create: %m");
			exit(3);
		}
		    
		task.id        = i;
		task.global_id = job->task[i]->gtid;
		task.pid       = job->task[i]->pid;
		task.ppid      = job->jmgr_pid;
		if (shm_add_task(job->jobid, job->stepid, &task) < 0)
			debug("shm_add_task: %m");	/* see comment above */
	}

	/*
	 * All tasks are now forked and running as the user, but
	 * will wait for our signal before calling exec.
	 */
	shm_update_step_pgid(job->jobid, job->stepid, job->pgid);
	shm_update_step_cont_id(job->jobid, job->stepid, job->cont_id);

	/*
	 * Now it's ok to unblock the tasks, so they may call exec.
	 */
	for (i = 0; i < job->ntasks; i++) {
		char c;
		
		debug3("Unblocking %u.%u task %d, writefd = %d",
		       job->jobid, job->stepid, i, writefds[i]);
		if (write (writefds[i], &c, sizeof (c)) != 1)
			error ("write to unblock task %d failed", i); 

		close(writefds[i]);

		/*
		 * Prepare process for attach by parallel debugger 
		 * (if specified and able)
		 */
		pdebug_trace_process(job, job->task[i]->pid);
	}
	xfree(writefds);
	xfree(readfds);

	return SLURM_SUCCESS;
}


/*
 * Loop once through tasks looking for all tasks that have exited with
 * the same exit status (and whose statuses have not been sent back to
 * the client) Aggregrate these tasks into a single task exit message.
 *
 */ 
static int 
_send_pending_exit_msgs(slurmd_job_t *job)
{
	int  i;
	int  nsent  = 0;
	int  status = 0;
	bool set    = false;
	uint32_t  tid[job->ntasks];

	/* 
	 * Collect all exit codes with the same status into a 
	 * single message. 
	 */
	for (i = 0; i < job->ntasks; i++) {
		slurmd_task_info_t *t = job->task[i];

		if (!t->exited || t->esent)
			continue;

		if (!set) { 
			status = t->estatus;
			set    = true;
		} else if (status != t->estatus)
			continue;

		tid[nsent++] = t->gtid;
		t->esent = true;
	}

	if (nsent) {
		debug2("Aggregated %d task exit messages", nsent);
		_send_exit_msg(job, tid, nsent, status);
	}

	return nsent;
}

/*
 * If waitflag is true, perform a blocking wait for a single process
 * and then return.
 *
 * If waitflag is false, do repeated non-blocking waits until
 * there are no more processes to reap (waitpid returns 0).
 */
static int
_wait_for_any_task(slurmd_job_t *job, bool waitflag)
{
	slurmd_task_info_t *t = NULL;
	int i;
	int status;
	pid_t pid;
	struct rusage rusage;
	int completed = 0;

	do {
		pid = wait3(&status, waitflag ? 0 : WNOHANG, &rusage);
		if (pid <= 0)
			continue;

		/* See if the pid matches that of one of the tasks */
		for (i = 0; i < job->ntasks; i++) {
			if (job->task[i]->pid == pid) {
				t = job->task[i];
				completed++;
				break;
			}
		}
		if (t != NULL) {
			debug3("Process %d, task %d finished", (int)pid, i);
			t->exited  = true;
			t->estatus = status;
			g_slurmd_jobacct_task_exit(job, pid, status, &rusage);
		}

	} while ((pid > 0) && !waitflag);

	return completed;
}
	

static void
_wait_for_all_tasks(slurmd_job_t *job)
{
	int i;

	for (i = 0; i < job->ntasks; ) {
		i += _wait_for_any_task(job, true);
		if (i < job->ntasks)
			i += _wait_for_any_task(job, false);

		while (_send_pending_exit_msgs(job)) {;}
	}
}


static void
_set_unexited_task_status(slurmd_job_t *job, int status)
{
	int i;
	for (i = 0; i < job->ntasks; i++) {
		slurmd_task_info_t *t = job->task[i];

		if (t->exited) continue;

		t->exited  = true;
		t->estatus = status;
	}
}



/*
 * Make sure all processes in session are dead for interactive jobs.  On 
 * systems with an IBM Federation switch, all processes must be terminated 
 * before the switch window can be released by interconnect_postfini().
 *  For batch jobs, we let spawned processes continue by convention
 * (although this could go either way). The Epilog program could be used 
 * to terminate any "orphan" processes.
 */
static void
_kill_running_tasks(slurmd_job_t *job)
{
	List         steps;
	ListIterator i;
	job_step_t  *s     = NULL;
	int          delay = 1;

	if (job->batch)
		return;

	steps = shm_get_steps();
	i = list_iterator_create(steps);

	/* find the job_step_t for this job step in the list from shared mem */
	while ((s = list_next(i)))
		if ((s->jobid == job->jobid) && (s->stepid == job->stepid))
			break;
	if (!s)
		return;

	if (s->cont_id) {
		slurm_container_signal(s->cont_id, SIGKILL);

		/* Try destroying the container up to 30 times */
		while (slurm_container_destroy(s->cont_id) != SLURM_SUCCESS) {
			slurm_container_signal(s->cont_id, SIGKILL);
			sleep(delay);
			if (delay < 120) {
				delay *= 2;
			} else {
				error("Unable to destroy container, job %u.%u",
				      job->jobid, job->stepid);
			}
		}
	}

	list_iterator_destroy(i);
	list_destroy(steps);
	return;
}

/*
 * Wait for IO
 */
static void
_wait_for_io(slurmd_job_t *job)
{
	debug("Waiting for IO");
	io_close_all(job);

	/*
	 * Wait until IO thread exits
	 */
	pthread_join(job->ioid, NULL);

	return;
}

	
static char *
_make_batch_dir(slurmd_job_t *job)
{
	char path[MAXPATHLEN]; 

	if (job->stepid == NO_VAL)
	  snprintf(path, 1024, "%s/job%05u", conf->spooldir, job->jobid);
	else
	  snprintf(path, 1024, "%s/job%05u.%05u", conf->spooldir, job->jobid,
		   job->stepid);

	if ((mkdir(path, 0750) < 0) && (errno != EEXIST)) {
		error("mkdir(%s): %m", path);
		goto error;
	}

	if (chown(path, (uid_t) -1, (gid_t) job->pwd->pw_gid) < 0) {
		error("chown(%s): %m", path);
		goto error;
	}

	if (chmod(path, 0750) < 0) {
		error("chmod(%s, 750): %m");
		goto error;
	}

	return xstrdup(path);

   error:
	return NULL;
}

static char *
_make_batch_script(batch_job_launch_msg_t *msg, char *path)
{
	FILE *fp = NULL;
	char  script[MAXPATHLEN];

	snprintf(script, 1024, "%s/%s", path, "script"); 

  again:
	if ((fp = safeopen(script, "w", SAFEOPEN_CREATE_ONLY)) == NULL) {
		if ((errno != EEXIST) || (unlink(script) < 0))  {
			error("couldn't open `%s': %m", script);
			goto error;
		}
		goto again;
	}

	if (fputs(msg->script, fp) < 0) {
		error("fputs: %m");
		goto error;
	}

	if (fclose(fp) < 0) {
		error("fclose: %m");
	}
	
	if (chown(script, (uid_t) msg->uid, (gid_t) -1) < 0) {
		error("chown(%s): %m", path);
		goto error;
	}

	if (chmod(script, 0500) < 0) {
		error("chmod: %m");
	}

	return xstrdup(script);

  error:
	return NULL;

}

static char *
_sprint_task_cnt(batch_job_launch_msg_t *msg)
{
        int i;
        char *task_str = xstrdup("");
        char tmp[16], *comma = "";

	for (i=0; i<msg->num_cpu_groups; i++) {
		if (i == 1)
			comma = ",";
		if (msg->cpu_count_reps[i] > 1)
			sprintf(tmp, "%s%d(x%d)", comma, msg->cpus_per_node[i],
				msg->cpu_count_reps[i]);
		else
			sprintf(tmp, "%s%d", comma, msg->cpus_per_node[i]);
		xstrcat(task_str, tmp);
	}

        return task_str;
}

static void
_send_launch_failure (launch_tasks_request_msg_t *msg, slurm_addr *cli, int rc)
{
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;

	debug ("sending launch failure message: %s", slurm_strerror (rc));

	memcpy(&resp_msg.address, cli, sizeof(slurm_addr));
	slurm_set_addr(&resp_msg.address, msg->resp_port, NULL); 
	resp_msg.data = &resp;
	resp_msg.msg_type = RESPONSE_LAUNCH_TASKS;

	resp.node_name     = conf->node_name;
	resp.srun_node_id  = msg->srun_node_id;
	resp.return_code   = rc ? rc : -1;
	resp.count_of_pids = 0;

	slurm_send_only_node_msg(&resp_msg);

	return;
}

static void
_send_launch_resp(slurmd_job_t *job, int rc)
{	
	int i;
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;
	srun_info_t *srun = list_peek(job->sruns);

	if (job->batch || job->spawn_task)
		return;

	debug("Sending launch resp rc=%d", rc);

        resp_msg.address      = srun->resp_addr;
	resp_msg.data         = &resp;
	resp_msg.msg_type     = RESPONSE_LAUNCH_TASKS;

	resp.node_name        = conf->node_name;
	resp.srun_node_id     = job->nodeid;
	resp.return_code      = rc;
	resp.count_of_pids    = job->ntasks;

	resp.local_pids = xmalloc(job->ntasks * sizeof(*resp.local_pids));
	for (i = 0; i < job->ntasks; i++) 
		resp.local_pids[i] = job->task[i]->pid;  

	slurm_send_only_node_msg(&resp_msg);

	xfree(resp.local_pids);
}


static int
_complete_job(uint32_t jobid, uint32_t stepid, int err, int status)
{
	int                      rc, i;
	slurm_msg_t              req_msg;
	complete_job_step_msg_t  req;

	req.job_id	= jobid;
	req.job_step_id	= stepid; 
	req.job_rc      = status;
	req.slurm_rc	= err; 
	req.node_name	= conf->node_name;
	req_msg.msg_type= REQUEST_COMPLETE_JOB_STEP;
	req_msg.data	= &req;	

	/* Note: these log messages don't go to slurmd.log from here */
	for (i=0; i<=MAX_RETRY; i++) {
		if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) >= 0)
			break;
		info("Retrying job complete RPC for %u.%u", jobid, stepid);
		sleep(RETRY_DELAY);
	}
	if (i > MAX_RETRY) {
		error("Unable to send job complete message: %m");
		return SLURM_ERROR;
	}

	if ((rc == ESLURM_ALREADY_DONE) || (rc == ESLURM_INVALID_JOB_ID))
		rc = SLURM_SUCCESS;
	if (rc)
		slurm_seterrno_ret(rc);

	return SLURM_SUCCESS;
}


static void
_handle_attach_req(slurmd_job_t *job)
{
	srun_info_t *srun;

	debug("handling attach request for %u.%u", job->jobid, job->stepid);

	srun       = xmalloc(sizeof(*srun));
	srun->key  = xmalloc(sizeof(*srun->key));

	if (shm_step_addrs( job->jobid, job->stepid, 
			    &srun->ioaddr, &srun->resp_addr,
			    srun->key ) < 0) {
		if (errno)
			error("Unable to update client addrs from shm: %m");
		return;
	}

	list_prepend(job->sruns, (void *) srun);

	io_new_clients(job);
}


static int
_drop_privileges(struct passwd *pwd)
{
	/*
	 * No need to drop privileges if we're not running as root
	 */
	if (getuid() != (uid_t) 0)
		return SLURM_SUCCESS;

	if (setegid(pwd->pw_gid) < 0) {
		error("setegid: %m");
		return -1;
	}

	if (initgroups(pwd->pw_name, pwd->pw_gid) < 0) {
		error("initgroups: %m"); 
	}

	if (seteuid(pwd->pw_uid) < 0) {
		error("seteuid: %m");
		return -1;
	}

	return SLURM_SUCCESS;
}

static int
_reclaim_privileges(struct passwd *pwd)
{
	/* 
	 * No need to reclaim privileges if our uid == pwd->pw_uid
	 */
	if (geteuid() == pwd->pw_uid)
		return SLURM_SUCCESS;

	if (seteuid(pwd->pw_uid) < 0) {
		error("seteuid: %m");
		return -1;
	}

	if (setegid(pwd->pw_gid) < 0) {
		error("setegid: %m");
		return -1;
	}

	if (initgroups(pwd->pw_name, pwd->pw_gid) < 0) {
		error("initgroups: %m"); 
		return -1;
	}

	return SLURM_SUCCESS;
}


static void
_slurmd_job_log_init(slurmd_job_t *job) 
{
	char argv0[64];

	if (!job->spawn_task)
		conf->log_opts.buffered = 1;

	/*
	 * Reset stderr logging to user requested level
	 * (Logfile and syslog levels remain the same)
	 */
	conf->log_opts.stderr_level = LOG_LEVEL_ERROR + job->debug;

	snprintf(argv0, sizeof(argv0), "slurmd[%s]", conf->hostname);

	/* 
	 * reinitialize log 
	 */
	log_alter(conf->log_opts, 0, NULL);
	log_set_argv0(argv0);

	/* Connect slurmd stderr to job's stderr */
	if ((!job->spawn_task) && 
	    (dup2(job->task[0]->perr[1], STDERR_FILENO) < 0)) {
		error("job_log_init: dup2(stderr): %m");
		return;
	}
}


static void
_setargs(slurmd_job_t *job)
{
	if (job->jobid > MAX_NOALLOC_JOBID)
		return;

	if ((job->jobid >= MIN_NOALLOC_JOBID) || (job->stepid == NO_VAL))
		setproctitle("[%u]",    job->jobid);
	else
		setproctitle("[%u.%u]", job->jobid, job->stepid); 

	return;
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


