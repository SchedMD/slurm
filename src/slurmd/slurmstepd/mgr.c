/*****************************************************************************\
 *  src/slurmd/slurmstepd/mgr.c - job manager functions for slurmstepd
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-217948.
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
#include <sys/types.h>

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

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/common/setproctitle.h"
#include "src/slurmd/common/proctrack.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/slurmd/slurmstepd/slurmstepd.h"
#include "src/slurmd/slurmstepd/mgr.h"
#include "src/slurmd/slurmstepd/task.h"
#include "src/slurmd/slurmstepd/io.h"
#include "src/slurmd/slurmstepd/pdebug.h"
#include "src/slurmd/slurmstepd/req.h"

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
	SIGUSR2, SIGALRM, SIGHUP, 0
};


/* 
 * Prototypes
 */

/* 
 * Job manager related prototypes
 */
static void _send_launch_failure(launch_tasks_request_msg_t *, 
                                 slurm_addr *, int);
static int  _fork_all_tasks(slurmd_job_t *job);
static int  _become_user(slurmd_job_t *job);
static void _set_job_log_prefix(slurmd_job_t *job);
static int  _setup_io(slurmd_job_t *job);
static int  _setup_spawn_io(slurmd_job_t *job);
static int  _drop_privileges(slurmd_job_t *job,
			     int *n_old_gids, gid_t **old_gids);
static int  _reclaim_privileges(struct passwd *pwd, int, gid_t *);
static void _send_launch_resp(slurmd_job_t *job, int rc);
static void _slurmd_job_log_init(slurmd_job_t *job);
static void _wait_for_io(slurmd_job_t *job);
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

/*
 * Initialize the group list using the list of gids from the slurmd if
 * available.  Otherwise initialize the groups with initgroups().
 */
static int _initgroups(slurmd_job_t *job);


static slurmd_job_t *reattach_job;

/*
 * Launch an job step on the current node
 */
extern slurmd_job_t *
mgr_launch_tasks_setup(launch_tasks_request_msg_t *msg, slurm_addr *cli,
		       slurm_addr *self)
{
	slurmd_job_t *job = NULL;
	
	if (!(job = job_create(msg, cli))) {
		_send_launch_failure (msg, cli, errno);
		return NULL;
	}

	_set_job_log_prefix(job);

	_setargs(job);
	
	job->envtp->cli = cli;
	job->envtp->self = self;
	
	return job;
}

static void
_batch_cleanup(slurmd_job_t *job, int level, int status)
{
	int rc = 0;

	switch(level) {
	default:
	case 2:
		if (job->argv[0] && (unlink(job->argv[0]) < 0))
			error("unlink(%s): %m", job->argv[0]);
        case 1:
		if (job->batchdir && (rmdir(job->batchdir) < 0))
			error("rmdir(%s): %m",  job->batchdir);
		xfree(job->batchdir);
	case 0:
		if (job->stepid == NO_VAL) 
			verbose("job %u completed with slurm_rc = %d, "
				"job_rc = %d", 
				job->jobid, rc, status);
		else
			verbose("job %u.%u completed with slurm_rc = %d, "
				"job_rc = %d", 
				job->jobid, job->stepid, rc, status);
		_complete_job(job->jobid, job->stepid, rc, status);

	}
}
/*
 * Launch a batch job script on the current node
 */
slurmd_job_t *
mgr_launch_batch_job_setup(batch_job_launch_msg_t *msg, slurm_addr *cli)
{
	int           status = 0;
	uint32_t      jobid  = msg->job_id;
	slurmd_job_t *job = NULL;
	char       buf[1024];
	hostlist_t hl = hostlist_create(msg->nodes);
	if (!hl)
		return NULL;
		
	hostlist_ranged_string(hl, 1024, buf);
	
	if (!(job = job_batch_job_create(msg))) {
		/*
		 *  Set "job" status to returned errno and cleanup job.
		 */
		status = errno;
		_batch_cleanup(job, 0, status);
		return NULL;
	}

	_set_job_log_prefix(job);

	_setargs(job);

	if ((job->batchdir = _make_batch_dir(job)) == NULL) {
		_batch_cleanup(job, 1, status);
		return NULL;
	}

	xfree(job->argv[0]);

	if ((job->argv[0] = _make_batch_script(msg, job->batchdir)) == NULL) {
		_batch_cleanup(job, 2, status);
		return NULL;
	}
	
	job->envtp->nprocs = msg->nprocs;
	job->envtp->select_jobinfo = msg->select_jobinfo;
	job->envtp->nhosts = hostlist_count(hl);
	hostlist_destroy(hl);
	job->envtp->nodelist = xstrdup(buf);
	job->envtp->task_count = _sprint_task_cnt(msg);
	return job;
}

void
mgr_launch_batch_job_cleanup(slurmd_job_t *job, int rc)
{
	_batch_cleanup(job, 2, rc);
}

/*
 * Spawn a task / job step on the current node
 */
slurmd_job_t *
mgr_spawn_task_setup(spawn_task_request_msg_t *msg, slurm_addr *cli,
		     slurm_addr *self)
{
	slurmd_job_t *job = NULL;
	
	if (!(job = job_spawn_create(msg, cli)))
		return NULL;

	job->spawn_task = true;
	_set_job_log_prefix(job);

	_setargs(job);
	
	job->envtp->cli = cli;
	job->envtp->self = self;
	
	return job;
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
	gid_t *old_gids;
	int n_old_gids;

	debug2("Entering _setup_io");

	/* Save current UID/GID */
	if (!(spwd = getpwuid(geteuid()))) {
		error("getpwuid: %m");
		return ESLURMD_IO_ERROR;
	}

	/*
	 * Temporarily drop permissions, initialize task stdio file
	 * decriptors (which may be connected to files), then
	 * reclaim privileges.
	 */
	if (_drop_privileges(job, &n_old_gids, &old_gids) < 0)
		return ESLURMD_SET_UID_OR_GID_ERROR;

	/* FIXME - need to check a return code for failures */
	io_init_tasks_stdio(job);

	if (_reclaim_privileges(spwd, n_old_gids, old_gids) < 0)
		error("sete{u/g}id(%lu/%lu): %m", 
		      (u_long) spwd->pw_uid, (u_long) spwd->pw_gid);

	/*
	 * MUST create the initial client object before starting
	 * the IO thread, or we risk losing stdout/err traffic.
	 */
	if (!job->batch) {
		srun_info_t *srun = list_peek(job->sruns);
		xassert(srun != NULL);
		rc = io_initial_client_connect(srun, job);
		if (rc < 0) 
			return ESLURMD_IO_ERROR;
	}

	if (!job->batch)
		if (io_thread_start(job) < 0)
			return ESLURMD_IO_ERROR;

	/*
	 * Initialize log facility to copy errors back to srun
	 */
	_slurmd_job_log_init(job);


#ifndef NDEBUG
#  ifdef PR_SET_DUMPABLE
	if (prctl(PR_SET_DUMPABLE, 1) < 0)
		debug ("Unable to set dumpable to 1");
#  endif /* PR_SET_DUMPABLE */
#endif   /* !NDEBUG         */

	debug2("Leaving  _setup_io");
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
int 
job_manager(slurmd_job_t *job)
{
	int rc = 0;
	bool io_initialized = false;
	int fd;

	debug3("Entered job_manager for %u.%u pid=%lu",
	       job->jobid, job->stepid, (unsigned long) job->jmgr_pid);
	
	if (!job->batch &&
	    (interconnect_preinit(job->switch_job) < 0)) {
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto fail1;
	}
	
	if (job->spawn_task)
		rc = _setup_spawn_io(job);
	else
		rc = _setup_io(job);
	if (rc) {
		error("IO setup failed: %m");
		goto fail2;
	} else {
		io_initialized = true;
	}

	g_slurmd_jobacct_jobstep_launched(job);

	/* Call interconnect_init() before becoming user */
	if (!job->batch && 
	    (interconnect_init(job->switch_job, job->uid) < 0)) {
		/* error("interconnect_init: %m"); already logged */
		rc = ESLURM_INTERCONNECT_FAILURE;
		io_close_task_fds(job);
		goto fail2;
	}
	
	if (_fork_all_tasks(job) < 0) {
		debug("_fork_all_tasks failed");
		rc = ESLURMD_EXECVE_FAILED;
		io_close_task_fds(job);
		goto fail2;
	}

	io_close_task_fds(job);

	xsignal_block(mgr_sigarray);
	reattach_job = job;

	job->state = SLURMSTEPD_STEP_RUNNING;

	/* Send job launch response with list of pids */
	_send_launch_resp(job, 0);

	/* tell the accountants to start counting */
	g_slurmd_jobacct_smgr();

	_wait_for_all_tasks(job);

	job->state = SLURMSTEPD_STEP_ENDING;

	if (!job->batch && 
	    (interconnect_fini(job->switch_job) < 0)) {
		error("interconnect_fini: %m");
		exit(1);
	}

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
	if (!job->batch && !job->spawn_task && io_initialized) {
		eio_signal_shutdown(job->eio);
		_wait_for_io(job);
	}

	g_slurmd_jobacct_jobstep_terminated(job);

    fail1:
    fail0:
	/* If interactive job startup was abnormal, 
	 * be sure to notify client.
	 */
	if (rc != 0) {
		error("job_manager exiting abnormally, rc = %d", rc);
		_send_launch_resp(job, rc);
	}

	return(rc);
}


/* fork and exec N tasks
 */ 
static int
_fork_all_tasks(slurmd_job_t *job)
{
	int rc = SLURM_SUCCESS;
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
	if (!writefds) {
		error("writefds xmalloc failed!");
		return SLURM_ERROR;
	}
	readfds = (int *) xmalloc (job->ntasks * sizeof(int));
	if (!readfds) {
		error("readfds xmalloc failed!");
		return SLURM_ERROR;
	}
	for (i = 0; i < job->ntasks; i++) {
		fdpair[0] = -1; fdpair[1] = -1;
		if (pipe (fdpair) < 0) {
			error ("exec_all_tasks: pipe: %m");
			return SLURM_ERROR;
		}
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
/* 		task_t task; */

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

			/* log_fini(); */ /* note: moved into exec_task() */

			xsignal_unblock(slurmstepd_blocked_signals);

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
	}

	/*
	 * All tasks are now forked and running as the user, but
	 * will wait for our signal before calling exec.
	 */

	/*
	 * Now it's ok to unblock the tasks, so they may call exec.
	 */
	for (i = 0; i < job->ntasks; i++) {
		char c = '\0';
		
		debug3("Unblocking %u.%u task %d, writefd = %d",
		       job->jobid, job->stepid, i, writefds[i]);
		if (write (writefds[i], &c, sizeof (c)) != 1)
			error ("write to unblock task %d failed", i); 

		close(writefds[i]);

		/*
		 * Prepare process for attach by parallel debugger 
		 * (if specified and able)
		 */
		if (pdebug_trace_process(job, job->task[i]->pid)
				== SLURM_ERROR)
			rc = SLURM_ERROR;
	}
	xfree(writefds);
	xfree(readfds);

	return rc;
}


/*
 * Loop once through tasks looking for all tasks that have exited with
 * the same exit status (and whose statuses have not been sent back to
 * the client) Aggregate these tasks into a single task exit message.
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
 *
 * Returns the number of tasks for which a wait3() was succesfully
 * performed, or -1 if there are no child tasks.
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
		if (pid == -1) {
			if (errno == ECHILD) {
				debug("No child processes");
				if (completed == 0)
					completed = -1;
				goto done;
			} else if (errno == EINTR) {
				debug("wait3 was interrupted");
				continue;
			} else {
				debug("Unknown errno %d", errno);
				continue;
			}
		} else if (pid == 0) { /* WNOHANG and no pids available */
			goto done;
		}

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
			job->envtp->env = job->env;
			job->envtp->procid = job->task[i]->gtid;
			job->envtp->localid = job->task[i]->id;
			setup_env(job->envtp);
			job->env = job->envtp->env;
			if (job->task_epilog) {
				run_script("user task_epilog", job->task_epilog, 
					job->jobid, job->uid, 2, job->env);
			}
			if (conf->task_epilog) {
				char *my_epilog;
				slurm_mutex_lock(&conf->config_mutex);
				my_epilog = xstrdup(conf->task_epilog);
				slurm_mutex_unlock(&conf->config_mutex);
				run_script("slurm task_epilog", my_epilog, 
					job->jobid, job->uid, -1, job->env);
				xfree(my_epilog);
			}
			job->envtp->procid = i;
			post_term(job);
			g_slurmd_jobacct_task_exit(job, pid, status, &rusage);
		}

	} while ((pid > 0) && !waitflag);

done:
	return completed;
}
	

static void
_wait_for_all_tasks(slurmd_job_t *job)
{
	int tasks_left = 0;
	int i;

	for (i = 0; i < job->ntasks; i++) {
		if (job->task[i]->state < SLURMD_TASK_COMPLETE) {
			tasks_left++;
		}
	}
	if (tasks_left < job->ntasks)
		verbose("Only %d of %d requested tasks successfully launched",
			tasks_left, job->ntasks);

	for (i = 0; i < tasks_left; ) {
		int rc;
		rc = _wait_for_any_task(job, true);
		if (rc != -1) {
			i += rc;
			if (i < job->ntasks) {
				rc = _wait_for_any_task(job, false);
				if (rc != -1) {
					i += rc;
				}
			}
		}

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
	int          delay = 1;

	if (job->batch)
		return;

	if (job->cont_id) {
		slurm_container_signal(job->cont_id, SIGKILL);

		/* Spin until the container is successfully destroyed */
		while (slurm_container_destroy(job->cont_id) != SLURM_SUCCESS) {
			slurm_container_signal(job->cont_id, SIGKILL);
			sleep(delay);
			if (delay < 120) {
				delay *= 2;
			} else {
				error("Unable to destroy container, job %u.%u",
				      job->jobid, job->stepid);
			}
		}
	}

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
	if (job->ioid)
		pthread_join(job->ioid, NULL);
	else
		info("_wait_for_io: ioid==0");

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

	info("sending REQUEST_COMPLETE_JOB_STEP");
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


static int
_drop_privileges(slurmd_job_t *job, int *n_old_gids, gid_t **old_gids)
{
	/*
	 * No need to drop privileges if we're not running as root
	 */
	if (getuid() != (uid_t) 0)
		return SLURM_SUCCESS;

	*n_old_gids = getgroups(0, NULL);
	*old_gids = (gid_t *)xmalloc(*n_old_gids * sizeof(gid_t));
	getgroups(*n_old_gids, *old_gids);

	if (setegid(job->pwd->pw_gid) < 0) {
		error("setegid: %m");
		return -1;
	}

	if (_initgroups(job) < 0) {
		error("_initgroups: %m"); 
	}

	if (seteuid(job->pwd->pw_uid) < 0) {
		error("seteuid: %m");
		return -1;
	}

	return SLURM_SUCCESS;
}

static int
_reclaim_privileges(struct passwd *pwd, int n_old_gids, gid_t *old_gids)
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

	setgroups(n_old_gids, old_gids);
	xfree(old_gids);

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
	 *
	 * The maximum stderr log level is LOG_LEVEL_DEBUG3 because
	 * some higher level debug messages are generated in the
	 * stdio code, which would otherwise create more stderr traffic
	 * to srun and therefore more debug messages in an endless loop.
	 */
	conf->log_opts.stderr_level = LOG_LEVEL_ERROR + job->debug;
	if (conf->log_opts.stderr_level > LOG_LEVEL_DEBUG3)
		conf->log_opts.stderr_level = LOG_LEVEL_DEBUG3;


	snprintf(argv0, sizeof(argv0), "slurmd[%s]", conf->hostname);

	/* 
	 * reinitialize log 
	 */
	log_alter(conf->log_opts, 0, NULL);
	log_set_argv0(argv0);

	/* Connect slurmd stderr to job's stderr */
	if ((!job->spawn_task) && 
	    (dup2(job->task[0]->stderr_fd, STDERR_FILENO) < 0)) {
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

	if (_initgroups(job) < 0) {
		;
		/* error("_initgroups: %m"); */
	}

	if (setuid(job->pwd->pw_uid) < 0) {
		error("setuid: %m");
		return -1;
	}

	return 0;
}	


static int
_initgroups(slurmd_job_t *job)
{
	int rc;
	char *username;
	gid_t gid;

	if (job->ngids > 0) {
		xassert(job->gids);
		debug2("Using gid list sent by slurmd");
		return setgroups(job->ngids, job->gids);
	}

	username = job->pwd->pw_name;
	gid = job->pwd->pw_gid;
	debug2("Uncached user/gid: %s/%ld", username, (long)gid);
	if (rc = initgroups(username, gid)) {
		if ((errno == EPERM) && (getuid != (uid_t) 0)) {
			debug("Error in initgroups(%s, %ld): %m",
				username, (long)gid);
		} else {
			error("Error in initgroups(%s, %ld): %m",
				username, (long)gid);
		}
		return -1;
	}
	return 0;
}
