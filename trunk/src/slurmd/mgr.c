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
#include <sys/ptrace.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <slurm/slurm_errno.h>

#include "src/common/cbuf.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/fd.h"
#include "src/common/safeopen.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/slurmd/mgr.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/setenvpf.h"
#include "src/slurmd/setproctitle.h"
#include "src/slurmd/smgr.h"
#include "src/slurmd/io.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/interconnect.h"


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
static int  _job_mgr(slurmd_job_t *job);
static void _set_job_log_prefix(slurmd_job_t *job);
static int  _setup_io(slurmd_job_t *job);
static int  _drop_privileges(struct passwd *pwd);
static int  _reclaim_privileges(struct passwd *pwd);
static void _send_launch_resp(slurmd_job_t *job, int rc);
static void _slurmd_job_log_init(slurmd_job_t *job);
static int  _update_shm_task_info(slurmd_job_t *job);
static int  _readn(int fd, void *buf, size_t nbytes);
static int  _create_job_session(slurmd_job_t *job);
static int  _wait_for_task_exit(slurmd_job_t *job);
static int  _wait_for_session(slurmd_job_t *job);
static void _wait_for_io(slurmd_job_t *job);
static void _set_unexited_task_status(slurmd_job_t *job, int status);
static void _handle_attach_req(slurmd_job_t *job);
static int  _send_exit_msg(slurmd_job_t *job, int tid[], int n, int status);
static void _set_unexited_task_status(slurmd_job_t *job, int status);
static int  _send_pending_exit_msgs(slurmd_job_t *job);

static void _setargs(slurmd_job_t *job);

static void _random_sleep(slurmd_job_t *job);

/*
 * Batch job mangement prototypes:
 */
static char * _make_batch_dir(slurmd_job_t *job);
static char * _make_batch_script(batch_job_launch_msg_t *msg, char *path);
static int    _setup_batch_env(slurmd_job_t *job, char *nodes);
static int    _complete_job(slurmd_job_t *job, int err, int status);


/* SIGHUP (empty) signal handler
 */
static void _hup_handler(int sig) {;}

/*
 * Launch an job step on the current node
 */
int
mgr_launch_tasks(launch_tasks_request_msg_t *msg, slurm_addr *cli)
{
	slurmd_job_t *job = NULL;

	if (!(job = job_create(msg, cli)))
		return SLURM_ERROR;

	_set_job_log_prefix(job);

	_setargs(job);

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
	slurmd_job_t *job;
	char         *batchdir;

	if (!(job = job_batch_job_create(msg))) 
		goto cleanup;

	_set_job_log_prefix(job);

	_setargs(job);

	if ((batchdir = _make_batch_dir(job)) == NULL) 
		goto cleanup1;

	xfree(job->argv[0]);

	if ((job->argv[0] = _make_batch_script(msg, batchdir)) == NULL)
		goto cleanup2;

	if ((rc = _setup_batch_env(job, msg->nodes)) < 0)
		goto cleanup2;

	status = _job_mgr(job);
		
   cleanup2:
	if (job->argv[0] && (unlink(job->argv[0]) < 0))
		error("unlink(%s): %m", job->argv[0]);
   cleanup1:
	if (batchdir && (rmdir(batchdir) < 0))
		error("rmdir(%s): %m",  batchdir);
	xfree(batchdir);
   cleanup :
	verbose("job %u completed with slurm_rc = %d, job_rc = %d", 
		job->jobid, rc, status);
	_complete_job(job, rc, status);
	return 0; 
}



/*
 * Run a prolog or epilog script.
 * returns -1 on failure. 
 *
 */
int 
run_script(bool prolog, const char *path, uint32_t jobid, uid_t uid)
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
		setenvpf(&env, "SLURM_JOBID=%u", jobid);
		setenvpf(&env, "SLURM_UID=%u",   uid);

		execve(path, argv, env);
		error("help! %m");
		exit(127);
	}

	do {
		if (waitpid(cpid, &status, 0) < 0) {
			if (errno != EINTR)
				return -1;
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
_send_exit_msg(slurmd_job_t *job, int tid[], int n, int status)
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

	debug3("Entered job_mgr pid=%lu", (unsigned long) getpid());

	if (shm_init(false) < 0)
		goto fail0;

	if (job_update_shm(job) < 0) {
		if (errno == ENOSPC) 
			rc = ESLURMD_TOOMANYSTEPS;
		else if (errno == EEXIST)
			rc = ESLURMD_STEP_EXISTS;
		goto fail0;
	}

	if (!job->batch && (interconnect_preinit(job) < 0)) {
		rc = ESLURM_INTERCONNECT_FAILURE;
		goto fail1;
	}

	xsignal_block(mgr_sigarray);
	xsignal(SIGHUP, _hup_handler);

	if ((rc = _setup_io(job))) 
		goto fail2;

	/*
	 * Create slurmd session manager and read task pids from pipe
	 * (waits for session manager process on failure)
	 */
	if ((rc = _create_job_session(job))) 
		goto fail2;

	if (job_update_state(job, SLURMD_JOB_STARTED) < 0)
		goto fail2;

	/*
	 * Send job launch response with list of pids
	 */
	if (!job->batch)
		_send_launch_resp(job, 0);

	/*
	 * Wait for all tasks to exit
	 */
	_wait_for_task_exit(job);

	/* wait for session to terminate, 
	 * then clean up
	 */
	_wait_for_session(job);

	/*
	 * Set status of any unexited tasks to that of 
	 * the session manager. Then send any pending 
	 * exit messages back to clients.
	 */
	_set_unexited_task_status(job, job->smgr_status);
	while (_send_pending_exit_msgs(job)) {;}

	job_update_state(job, SLURMD_JOB_ENDING);
    fail2:

	/*
	 *  First call interconnect_postfini() - In at least one case,
	 *    this will clean up any straggling processes. If this call
	 *    is moved behind wait_for_io(), we may block waiting for IO
	 *    on a hung process.
	 */
	if (!job->batch && (interconnect_postfini(job) < 0))
		error("interconnect_postfini: %m");

	/*
	 * Wait for io thread to complete
	 */
	_wait_for_io(job);


	job_update_state(job, SLURMD_JOB_COMPLETE);

    fail1:
	job_delete_shm(job);
	shm_fini();
    fail0:
	/* If interactive job startup was abnormal, 
	 * be sure to notify client.
	 */
	if ((rc != 0) && !job->batch) 
		_send_launch_resp(job, rc);

	return(rc);
}

/*
 * update task information from "job" into shared memory
 */
static int 
_update_shm_task_info(slurmd_job_t *job)
{
	int retval = SLURM_SUCCESS;
	int i;
	
	for (i = 0; i < job->ntasks; i++) {
		task_t t;

		t.id        = i;
		t.global_id = job->task[i]->gid;
		t.pid       = job->task[i]->pid;
		t.ppid      = job->smgr_pid;

		if (shm_add_task(job->jobid, job->stepid, &t) < 0) {
			error("shm_add_task: %m");
			retval = SLURM_ERROR;
		}
	}

	return retval;
}

static int 
_readn(int fd, void *buf, size_t nbytes)
{
	int    n     = 0;
	char  *pbuf  = (char *) buf;
	size_t nleft = nbytes;

	while (nleft > 0) {
		if ((n = read(fd, (void *) pbuf, nleft)) > 0) {
			pbuf+=n;
			nleft-=n;
		} else if (n == 0)	/* EOF */
			break;
		else if (errno == EINTR)
			break;
		else {
			debug("read: %m");
			break;
		}
	}
	return(n);
}


static int
_create_job_session(slurmd_job_t *job)
{
	int   i;
	int   rc = 0;
	int   fd = job->fdpair[0];
	pid_t spid;   

	if ((spid = smgr_create(job)) < (pid_t) 0) {
		error("Unable to create session manager: %m");
		return ESLURMD_FORK_FAILED;
	}

	/*
	 * If the created job terminates immediately, the shared memory
	 * record can be purged before we canset the mpid and sid below.
	 * This does not truly indicate an error condition, but a rare 
	 * timing anomaly. Thus we log the event using debug()
	 */
	job->jmgr_pid = getpid();
	if (shm_update_step_mpid(job->jobid, job->stepid, getpid()) < 0)
		debug("shm_update_step_mpid: %m");

	job->smgr_pid = spid;
	if (shm_update_step_sid(job->jobid, job->stepid, spid) < 0)
		debug("shm_update_step_sid: %m");

	/*
	 * Read information from session manager slurmd
	 */
	for (i = 0; i < job->ntasks; i++) {
		pid_t *pidptr = &job->task[i]->pid;

		if ((rc = _readn(fd, (void *) pidptr, sizeof(pid_t))) < 0) 
			error("Error obtaining task information: %m");

		if (rc == 0) /* EOF, smgr must've died */
			goto error;
	}

	_update_shm_task_info(job);

	return SLURM_SUCCESS;

    error:
	return _wait_for_session(job);
}

/*
 * Read task exit codes from session pipe.
 * read as many as possible until nonblocking fd returns EAGAIN.
 *
 * Returns number of exit codes read
 */
static int 
_handle_task_exit(slurmd_job_t *job)
{
	exit_status_t e;
	int i       = 0;
	int len     = 0;
	int nexited = 0;

	/*
	 * read at most ntask task exit codes from session manager
	 */
	for (i = 0; i < job->ntasks; i++) {
		task_info_t *t;
		
		if ((len = read(job->fdpair[0], &e, sizeof(e))) < 0) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
				break;
			error("read from session mgr: %m");
			return SLURM_ERROR;
		}

		if (len == 0) /* EOF */
			break;

		t = job->task[e.taskid];

		t->estatus = e.status;
		t->exited  = true;
		t->esent   = false;
		nexited++;
	} 

	return nexited;
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
	int  tid[job->ntasks];

	/* 
	 * Collect all exit codes with the same status into a 
	 * single message. 
	 */
	for (i = 0; i < job->ntasks; i++) {
		task_info_t *t = job->task[i];

		if (!t->exited || t->esent)
			continue;

		if (!set) { 
			status = t->estatus;
			set    = true;
		} else if (status != t->estatus)
			continue;

		tid[nsent++] = t->gid;
		t->esent = true;
	}

	if (nsent) {
		debug2("Aggregated %d task exit messages", nsent);
		_send_exit_msg(job, tid, nsent, status);
	}

	return nsent;
}


/*
 * Wait for tasks to exit by reading task exit codes from session manger.
 *
 * Send exit messages to client(s), aggregating where possible.
 *
 */
static int
_wait_for_task_exit(slurmd_job_t *job)
{
	int           rc      = 0;
	int           timeout = -1;
	int           waiting = job->ntasks;
	int           rfd     = job->fdpair[0];
	struct pollfd pfd[1]; 

	pfd[0].fd     = rfd;
	pfd[0].events = POLLIN;

	fd_set_nonblocking(rfd);

	do {
		int revents = 0;
		int nsent   = 0;

		if ((rc = poll(pfd, 1, timeout)) < 0) {
			if (errno == EINTR)
				_handle_attach_req(job);
			else
				error("wait_for_task_exit: poll: %m");
			continue;
		}

		revents = pfd[0].revents;

		xassert (!(revents & POLLNVAL));

		if ((revents & (POLLIN|POLLHUP|POLLERR))) {

		     if ((rc = _handle_task_exit(job)) <= 0) {
			     if (rc < 0) 
				     error("Unable to read task exit codes");
			     goto done;
		     } 

		     if (rc < (job->ntasks - nsent)) {
			     timeout = 50;
			     continue;
		     }
		}

		/* 
		 * send all pending task exit messages
		 */
		while ((nsent = _send_pending_exit_msgs(job))) 
			waiting -= rc;

		timeout = -1;

	} while (waiting);

	close(rfd);
	return SLURM_SUCCESS;

    done:
	close(rfd);
	return SLURM_FAILURE;
}

static void
_set_unexited_task_status(slurmd_job_t *job, int status)
{
	int i;
	for (i = 0; i < job->ntasks; i++) {
		task_info_t *t = job->task[i];

		if (t->exited) continue;

		t->exited  = true;
		t->estatus = status;
	}
}

/*
 * read task exit status from slurmd session manager process,
 * then wait for session manager to terminate
 */
static int
_wait_for_session(slurmd_job_t *job)
{
	int   status = job->smgr_status;
	int   rc     = 0;
	pid_t pid;

	if (status != -1) 
		goto done;

	while ((pid = waitpid(job->smgr_pid, &status, 0)) < (pid_t) 0) {
		if (errno == EINTR) 
			_handle_attach_req(job);
		else {
			error("waitpid: %m");
			break;
		}
	}

	job->smgr_status = status;

    done:
	if (WIFSIGNALED(status)) {

		int signo = WTERMSIG(status);

		if (signo != 9)
			error ("slurmd session manager killed by signal %d", signo);

		/*
		 * Make sure all processes in session are dead
		 */
		if (job->smgr_pid > (pid_t) 0)
			killpg(job->smgr_pid, SIGKILL);
		return ESLURMD_SESSION_KILLED;
	}

	if (!WIFEXITED(status))
		rc = WEXITSTATUS(status);

	return (rc <= MAX_SMGR_EXIT_STATUS) ? exit_errno[rc] : rc;
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

	snprintf(path, 1024, "%s/job%05u", conf->spooldir, job->jobid);

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

static int
_setup_batch_env(slurmd_job_t *job, char *nodes)
{
	char       buf[1024];
	hostlist_t hl = hostlist_create(nodes);

	if (!hl)
		return SLURM_ERROR;

	hostlist_ranged_string(hl, 1024, buf);
	setenvpf(&job->env, "SLURM_JOBID=%u",    job->jobid);
	setenvpf(&job->env, "SLURM_NNODES=%u",   hostlist_count(hl));
	setenvpf(&job->env, "SLURM_NODELIST=%s", buf);
	hostlist_destroy(hl);

	return 0;
}


static void
_send_launch_resp(slurmd_job_t *job, int rc)
{	
	int i;
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;
	srun_info_t *srun = list_peek(job->sruns);

	debug("Sending launch resp rc=%d", rc);

        resp_msg.address      = srun->resp_addr;
	resp_msg.data         = &resp;
	resp_msg.msg_type     = RESPONSE_LAUNCH_TASKS;

	resp.node_name        = conf->hostname;
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
_complete_job(slurmd_job_t *job, int err, int status)
{
	int                      rc;
	slurm_msg_t              req_msg;
	complete_job_step_msg_t  req;

	req.job_id	= job->jobid;
	req.job_step_id	= NO_VAL; 
	req.job_rc	= status;
	req.slurm_rc	= err; 
	req.node_name	= conf->hostname;
	req_msg.msg_type= REQUEST_COMPLETE_JOB_STEP;
	req_msg.data	= &req;	

	if (slurm_send_recv_controller_rc_msg(&req_msg, &rc) < 0) {
		error("Unable to send job complete message: %m");
		return SLURM_ERROR;
	}

	if (rc) slurm_seterrno_ret(rc);

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
	if (dup2(job->task[0]->perr[1], STDERR_FILENO) < 0) {
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

