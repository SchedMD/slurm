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
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include <src/common/log.h>
#include <src/common/cbuf.h>
#include <src/common/xsignal.h>
#include <src/common/xstring.h>
#include <src/common/xmalloc.h>
#include <src/common/safeopen.h>
#include <src/common/hostlist.h>

#include <src/slurmd/slurmd.h>
#include <src/slurmd/setenvpf.h>
#include <src/slurmd/mgr.h>
#include <src/slurmd/io.h>
#include <src/slurmd/shm.h>
#include <src/slurmd/interconnect.h>

static int  _run_job(slurmd_job_t *job);
static int  _run_batch_job(slurmd_job_t *job);
static void _exec_all_tasks(slurmd_job_t *job);
static void _task_exec(slurmd_job_t *job, int i, bool batch);
static int  _seteuid_and_chdir(slurmd_job_t *job);
static int  _setuid(slurmd_job_t *job);
static int  _unblock_all_signals(void);
static int  _send_exit_msg(int rc, task_info_t *t);

/* Launch a job step on this node
 */
int
mgr_launch_tasks(launch_tasks_request_msg_t *msg, slurm_addr *cli)
{
	slurmd_job_t *job;

	/* New process, so we must reinit shm */
	if (shm_init() < 0)  
		goto error;

	if (!(job = job_create(msg, cli)))
		goto error;

	verbose("running job step %d.%d for %s", 
		job->jobid, job->stepid, job->pwd->pw_name);

	if (_run_job(job) < 0) 
		goto error;

	job_debug2(job, "%ld returned from slurmd_run_job()", getpid());
	shm_fini();
	return(SLURM_SUCCESS);
  error:
	job_error(job, "cannot run job");
	shm_fini();
	return(SLURM_ERROR);
}

static char *
_make_batch_dir(batch_job_launch_msg_t *msg)
{
	char path[MAXPATHLEN]; 

	snprintf(path, 1024, "%s/job%u", conf->spooldir, msg->job_id);

	if ((mkdir(path, 0700) < 0) && (errno != EEXIST)) {
		error("mkdir(%s): %m", path);
		goto error;
	}

	if (chown(path, (uid_t) msg->uid, (gid_t) -1) < 0) {
		error("chown(%s): %m", path);
		goto error;
	}

	if (chmod(path, 0500) < 0) {
		error("chmod(%s): %m", path);
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
		if ((errno != EEXIST) || (unlink(script) < 0)) 
			goto error;
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

static void
_setup_batch_env(slurmd_job_t *job, char *nodes)
{
	char       buf[1024];
	int        envc;
	hostlist_t hl = hostlist_create(nodes);


	envc = (int)job->envc;

	setenvpf(&job->env, &envc, "SLURM_JOBID=%u",    job->jobid);
	if (hl) {
		hostlist_ranged_string(hl, 1024, buf);
		setenvpf(&job->env, &envc, "SLURM_NNODES=%u",   
				           hostlist_count(hl));
		setenvpf(&job->env, &envc, "SLURM_NODELIST=%s", buf);
		hostlist_destroy(hl);
	}

	job->envc = envc;
}


int
mgr_launch_batch_job(batch_job_launch_msg_t *msg, slurm_addr *cli)
{
	slurmd_job_t *job;
	char         *batchdir;

	/* New process, so must reinit shm */
	if (shm_init() < 0)
		goto cleanup;

	if (!(job = job_batch_job_create(msg))) 
		goto cleanup;

	if ((batchdir = _make_batch_dir(msg)) == NULL)
		goto cleanup;

	if (job->argv[0])
		xfree(job->argv[0]);

	if ((job->argv[0] = _make_batch_script(msg, batchdir)) == NULL)
		goto cleanup;

	_setup_batch_env(job, msg->nodes);

	_run_batch_job(job);
		
   cleanup:
	shm_fini();
	if (unlink(job->argv[0]) < 0)
		error("unlink(%s): %m", job->argv[0]);
	if (rmdir(batchdir) < 0)
		error("rmdir(%s): %m",  batchdir);
	xfree(batchdir);
	return 0; 
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
static int
_run_job(slurmd_job_t *job)
{
	int   rc = SLURM_SUCCESS;
	int   i;
	uid_t suid = getuid();
	gid_t sgid = getgid();

	/* Insert job info into shared memory */
	job_update_shm(job);

	/* 
	 * Need to detach from shared memory
	 * We don't know what will happen in interconnect_init()
	 */
	shm_fini();

	if (interconnect_init(job) == SLURM_ERROR) {
		job_error(job, "interconnect_init: %m");
		rc = -2;
		shm_init();
		goto done;
	}

	/* Reattach to shared memory after interconnect is initialized
	 */
	job_debug(job, "%ld reattaching to shm", getpid());
	if (shm_init() < 0) {
		job_error(job, "unable to reattach to shm: %m");
		rc = -1;
		goto done;
	}
	
	/* initialize I/O, connect back to srun, and spawn thread for
	 * forwarding I/O.
	 */

	/* Temporarily drop permissions and attempt to chdir()
	 *
	 */
	if ((rc = _seteuid_and_chdir(job)) < 0) 
		goto done;

	/* Option: connect slurmd stderr to srun local task 0: stderr? */
	if (io_spawn_handler(job) == SLURM_ERROR) {
		job_error(job, "unable to spawn io handler");
		rc = -3;
		goto done;
	}

	if ((seteuid(suid) < 0) || (setegid(sgid) < 0)) 
		error("seteuid(0): %m");

	_exec_all_tasks(job);
	job_debug(job, "job complete, waiting on IO");
	io_close_all(job);
	pthread_join(job->ioid, NULL);
	job_debug(job, "IO complete");

done:
	interconnect_fini(job); /* ignore errors        */
	job_delete_shm(job);    /* again, ignore errors */
	job_verbose(job, "completed");
	if (rc < 0) {
		for (i = 0; i < job->ntasks; i++)
			_send_exit_msg(-rc, job->task[i]);
	}
	return rc;
}

static int
_complete_job(slurmd_job_t *job)
{
	int                rc;
	size_t             size;
	slurm_fd           sock;
	slurm_msg_t        msg;
	slurm_msg_t        resp_msg;
	job_step_id_msg_t  req;
	return_code_msg_t *resp;

	req.job_id   = job->jobid;
	req.job_step_id  = NO_VAL; 
	msg.msg_type = REQUEST_COMPLETE_JOB_STEP;
	msg.data     = &req;	

	if ((sock = slurm_open_controller_conn()) < 0) {
		error("unable to open connection to controller");
		return SLURM_ERROR;
	}
	
	if ((rc = slurm_send_controller_msg(sock, &msg)) < 0) {
		error("sending message to controller");	
		return SLURM_ERROR;
	}

	size = slurm_receive_msg(sock, &resp_msg);

	if ((rc = slurm_shutdown_msg_conn(sock)) < 0) {
		error("shutting down controller connection");
		return SLURM_ERROR;
	}

	if (size < 0) {
		error("Unable to recieve resp from controller");
		return SLURM_ERROR;
	}

	switch (resp_msg.msg_type) {
	case RESPONSE_SLURM_RC:
		resp = resp_msg.data;
		rc = resp->return_code;
		slurm_free_return_code_msg(resp);
		slurm_seterrno_ret(rc);
		break;
	default:
		slurm_seterrno_ret(SLURM_UNEXPECTED_MSG_ERROR);
		break;
	}

	return SLURM_SUCCESS;
}

static int
_run_batch_job(slurmd_job_t *job)
{
	int    rc;
	task_t t;
	pid_t  sid, pid;
	int status;
	gid_t sgid = getgid();
	uid_t suid = getuid();

	job_update_shm(job);

	/* Temporarily drop permissions to initiate
	 * IO thread. This will ensure that calling user
	 * has appropriate permissions to open output
	 * files, if any.
	 */
	_seteuid_and_chdir(job);

	if (io_spawn_handler(job) == SLURM_ERROR) {
		job_error(job, "unable to spawn io handler");
		rc = SLURM_ERROR;
		goto done;
	}

	/* seteuid/gid back to saved uid/gid
	 */
	if ((seteuid(suid) < 0) || (setegid(sgid) < 0)) {
		fatal("set{e/g}uid(%ld/%ld) : %m", suid, sgid);
	}

	xsignal(SIGPIPE, SIG_IGN);

	if ((sid = setsid()) < (pid_t) 0) {
		error("job %d: setsid: %m", job->jobid);
	}

	if (shm_update_step_sid(job->jobid, job->stepid, sid) < 0)
		error("job %d: shm_update_step_sid: %m", job->jobid);

	t.id = 0;
	t.global_id = 0;
	t.ppid      = getpid();

	if ((t.pid = fork()) < 0) {
		error("fork: %m");
		exit(1);
		/* job_cleanup() */
	} else if (t.pid == 0)   /* child */
		_task_exec(job, 0, true);

	/* Parent continues: */

	job->task[0]->pid = t.pid;

	if (shm_add_task(job->jobid, job->stepid, &t) < 0)
		job_error(job, "shm_add_task: %m");

	while ((pid = waitpid(0, &status, 0)) < 0 && (pid != t.pid)) {
		if (pid > 0)
			continue;
		if (errno == EINTR)
			continue;
		else
			error("waitpid: %m");
	}

	verbose("batch job %d exited with status %d", job->jobid, status);

	/* Wait for io to complete */
	io_close_all(job);
	pthread_join(job->ioid, NULL);

	_complete_job(job);

done:
	shm_delete_step(job->jobid, job->stepid);
	return rc;

}

static void
_wait_for_all_tasks(slurmd_job_t *job)
{
	int waiting = job->ntasks;
	int i;

	while (waiting > 0) {
		int status;
		pid_t pid = waitpid(0, &status, 0);
		if ((pid < (pid_t) 0)) {
			if (errno == EINTR)
				continue;
			job_error(job, "waitpid: %m");
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

static int
_seteuid_and_chdir(slurmd_job_t *job)
{
	if (setegid(job->pwd->pw_gid) < 0) {
		error("setegid: %m");
		return -1;
	}

	if (initgroups(job->pwd->pw_name, job->pwd->pw_gid) < 0) {
		;
		/* error("initgroups: %m"); */
	}

	if (seteuid(job->pwd->pw_uid) < 0) {
		error("seteuid: %m");
		return -1;
	}

	if (chdir(job->cwd) < 0) {
		error("couldn't chdir to `%s': %m: going to /tmp instead",
				job->cwd); 
		if (chdir("/tmp") < 0) {
			error("couldn't chdir to /tmp either. dying.");
			return -1;
		}
	}

	return SLURM_SUCCESS;
}

static int
_setuid(slurmd_job_t *job)
{
	if (setgid(job->pwd->pw_gid) < 0) {
		error("setgid: %m");
		return -1;
	}

	if (initgroups(job->pwd->pw_name, job->pwd->pw_gid) < 0) {
		;
		/* error("initgroups: %m"); */
	}

	if (setuid(job->uid) < 0) {
		error("setuid: %m");
		return -1;
	}

	return 0;
}

static void
_task_exec(slurmd_job_t *job, int i, bool batch)
{
	int rc;
	log_options_t opts = LOG_OPTS_STDERR_ONLY;

	io_prepare_child(job->task[i]);

	/* 
	 * Reinitialize slurm log facility to send errors back to client 
	 */
	log_init("slurmd", opts, 0, NULL); 

	if ((rc = _setuid(job)) < 0) 
		exit(rc);

	if (_unblock_all_signals() == SLURM_ERROR) {
		error("unable to unblock signals");
		exit(1);
	}

	/* attach to interconnect */
	if (!batch && (interconnect_attach(job, i) < 0)) {
		error("interconnect attach failed: %m");
		exit(1);
	}

	if (!batch && (interconnect_env(job, i) < 0)) {
		error("interconnect_env: %m");
	}

	/* exec the cmdline */
	execve(job->argv[0], job->argv, job->env);

	/* error and clean up if execve() returns:
	 */
	error("execve(): %s: %m", job->argv[0]); 
	exit(errno);
}

static void 
_exec_all_tasks(slurmd_job_t *job)
{
	pid_t sid;
	int i;

	job_debug3(job, "%ld entered _launch_tasks", getpid());

	xsignal(SIGPIPE, SIG_IGN);

	if ((sid = setsid()) < (pid_t) 0) {
		job_error(job, "setsid: %m");
	}

	if (shm_update_step_sid(job->jobid, job->stepid, sid) < 0)
		job_error(job, "shm_update_step_sid: %m");
	
	job_debug2(job, "invoking %d tasks", job->ntasks);

	for (i = 0; i < job->ntasks; i++) {
		task_t t;
		job_debug2(job, "going to fork task %d", i);
		t.id = i;
		t.global_id = job->task[i]->gid;
		t.ppid      = getpid();

		if ((t.pid = fork()) < 0) {
			error("fork: %m");
			exit(1);
			/* job_cleanup() */
		} else if (t.pid == 0)   /* child */
			break;

		/* Parent continues loop: */

		job->task[i]->pid = t.pid;

		job_debug2(job, "%ld: forked child process %ld for task %d", 
				getpid(), (long) t.pid, i);  
		job_debug2(job, "going to add task %d to shm", i);
		if (shm_add_task(job->jobid, job->stepid, &t) < 0)
			job_error(job, "shm_add_task: %m");
		job_debug2(job, "task %d added to shm", i);

	}

	if (i == job->ntasks) 
		_wait_for_all_tasks(job);
	else
		_task_exec(job, i, false);

	return;
}

static int 
_send_exit_msg(int rc, task_info_t *t)
{
	slurm_msg_t     resp;
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
