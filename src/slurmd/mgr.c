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

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>

#if HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#include "src/common/cbuf.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/safeopen.h"
#include "src/common/slurm_errno.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/setenvpf.h"
#include "src/slurmd/mgr.h"
#include "src/slurmd/io.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/interconnect.h"

static int  _run_job(slurmd_job_t *job);
static int  _run_batch_job(slurmd_job_t *job);
static void _exec_all_tasks(slurmd_job_t *job);
static void _task_exec(slurmd_job_t *job, int i, bool batch);
static int  _drop_privileges(struct passwd *pwd);
static int  _reclaim_privileges(struct passwd *pwd);
static int  _become_user(slurmd_job_t *job);
static int  _unblock_all_signals(void);
static int  _send_exit_msg(int rc, task_info_t *t);
static int  _complete_job(slurmd_job_t *job, int rc, int status);

static void
_setargs(slurmd_job_t *job, char **argv, int argc)
{
	int i;
	size_t len = 0;
	char *name = NULL;

	for (i = 1; i < argc; i++) 
		len += strlen(argv[0]) + 1;

	xstrfmtcat(name, "slurmd [%d.%d]", job->jobid, job->stepid); 

	if (len < strlen(name))
		goto done;

	memset(argv[0], 0, len);
	strncpy(argv[0], name, strlen(name));

    done:
	xfree(name);
	return;
}

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

	_setargs(job, *conf->argv, *conf->argc);

	verbose("running job step %d.%d for %s", 
		job->jobid, job->stepid, job->pwd->pw_name);

	/* Run job's tasks and wait for all tasks to exit.
	 */
	if (_run_job(job) < 0) 
		goto error;

	job_debug2(job, "%ld returned from slurmd_run_job()", getpid());
	shm_fini();
	return(SLURM_SUCCESS);
  error:
	job_error(job, "cannot run job: %m");
	shm_fini();
	return(SLURM_ERROR);
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
	int        envc;
	hostlist_t hl = hostlist_create(nodes);

	if (!hl)
		return SLURM_ERROR;

	envc = (int)job->envc;

	hostlist_ranged_string(hl, 1024, buf);
	setenvpf(&job->env, &envc, "SLURM_JOBID=%u",    job->jobid);
	setenvpf(&job->env, &envc, "SLURM_NNODES=%u",   hostlist_count(hl));
	setenvpf(&job->env, &envc, "SLURM_NODELIST=%s", buf);
	hostlist_destroy(hl);

	job->envc = envc;

	return 0;
}


int
mgr_launch_batch_job(batch_job_launch_msg_t *msg, slurm_addr *cli)
{
	int           rc     = 0;
	int           status = 0;
	slurmd_job_t *job;
	char         *batchdir;

	/* New process, so must reinit shm */
	if ((rc = shm_init()) < 0) 
		goto cleanup1;

	if (!(job = job_batch_job_create(msg))) 
		goto cleanup2;

	job_update_shm(job);

	if ((batchdir = _make_batch_dir(job)) == NULL) 
		goto cleanup2;

	if (job->argv[0])
		xfree(job->argv[0]);

	if ((job->argv[0] = _make_batch_script(msg, batchdir)) == NULL)
		goto cleanup3;

	if ((rc = _setup_batch_env(job, msg->nodes)) < 0)
		goto cleanup;

	status = _run_batch_job(job);
		
   cleanup:
	if (job->argv[0] && (unlink(job->argv[0]) < 0))
		error("unlink(%s): %m", job->argv[0]);
   cleanup3:
	if (batchdir && (rmdir(batchdir) < 0))
		error("rmdir(%s): %m",  batchdir);
	xfree(batchdir);
   cleanup2:
	shm_delete_step(job->jobid, job->stepid);
	shm_fini();
   cleanup1:
	verbose("job %d completed with slurm_rc = %d, job_rc = %d", 
		job->jobid, rc, status);
	_complete_job(job, rc, status);
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
	int            rc   = SLURM_SUCCESS;
	int            i    = 0;
	struct passwd *spwd = getpwuid(geteuid());

	/* Insert job info into shared memory */
	job_update_shm(job);

	if (interconnect_init(job) == SLURM_ERROR) {
		job_error(job, "interconnect_init: %m");
		rc = -2;
		/* shm_init(); */
		goto done;
	}

	/*
	 * Temporarily drop permissions 
	 */
	 if ((rc = _drop_privileges(job->pwd)) < 0)
		goto done;

	/* Option: connect slurmd stderr to srun local task 0: stderr? */
	if (io_spawn_handler(job) == SLURM_ERROR) {
		job_error(job, "unable to spawn io handler");
		rc = -3;
		goto done;
	}

	if (_reclaim_privileges(spwd) < 0) 
		error("sete{u/g}id(%ld/%ld): %m", spwd->pw_uid, spwd->pw_gid);

	_exec_all_tasks(job);
	job_debug2(job, "job complete, waiting on IO");
	io_close_all(job);
	pthread_join(job->ioid, NULL);
	job_debug2(job, "IO complete");

   done:
	interconnect_fini(job); /* ignore errors        */
	job_delete_shm(job);    /* again, ignore errors */
	job_verbose(job, "job complete with rc = %d", rc);
	if (rc < 0) {
		for (i = 0; i < job->ntasks; i++)
			_send_exit_msg(-rc, job->task[i]);
	}
	return rc;
}

static int
_complete_job(slurmd_job_t *job, int err, int status)
{
	int                rc;
	size_t             size;
	slurm_fd           sock;
	slurm_msg_t        msg;
	slurm_msg_t        resp_msg;
	complete_job_step_msg_t  req;
	return_code_msg_t *resp;

	req.job_id	= job->jobid;
	req.job_step_id	= NO_VAL; 
	req.job_rc	= status;
	req.slurm_rc	= err; 
	req.node_name	= conf->hostname;
	msg.msg_type	= REQUEST_COMPLETE_JOB_STEP;
	msg.data	= &req;	

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
		error("Unable to receive resp from controller");
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

static void
_handle_attach_req(slurmd_job_t *job)
{
	srun_info_t *srun;

	debug("handling attach request for %d.%d", job->jobid, job->stepid);

	srun       = xmalloc(sizeof(*srun));
	srun->key  = xmalloc(sizeof(*srun->key));

	if (shm_step_addrs( job->jobid, job->stepid, 
			    &srun->ioaddr, &srun->resp_addr,
			    srun->key ) < 0) {
		error("Unable to update client addrs from shm: %m");
		return;
	}

	list_prepend(job->sruns, (void *) srun);

	io_new_clients(job);
}

static int
_run_batch_job(slurmd_job_t *job)
{
	int    status = 0;
	int    rc     = 0;
	task_t t;
	pid_t  sid, pid;
	struct passwd *spwd = getpwuid(getuid());

	/* Temporarily drop permissions to initiate
	 * IO thread. This will ensure that calling user
	 * has appropriate permissions to open output
	 * files, if any.
	 */
	if (_drop_privileges(job->pwd) < 0) {
		error("seteuid(%ld) : %m", job->uid);
		return ESLURMD_SET_UID_OR_GID_ERROR;
	}

	rc = io_spawn_handler(job);

	/* seteuid/gid back to saved uid/gid
	 */
	if (_reclaim_privileges(spwd) < 0) {
		error("seteuid(%ld) : %m", spwd->pw_uid);
		return ESLURMD_SET_UID_OR_GID_ERROR;
	}

	/* Give up if we couldn't spawn IO handler for whatever reason
	 */
	if (rc < 0)
		return ESLURMD_CANNOT_SPAWN_IO_THREAD;

	xsignal(SIGPIPE, SIG_IGN);

	if ((sid = setsid()) < (pid_t) 0) {
		error("job %d: setsid: %m", job->jobid);
		return ESLURMD_SET_SID_ERROR;
	}

	if (shm_update_step_sid(job->jobid, job->stepid, sid) < 0) {
		error("job %d: shm_update_step_sid: %m", job->jobid);
		return ESLURMD_SHARED_MEMORY_ERROR;
	}

	t.id = 0;
	t.global_id = 0;
	t.ppid      = getpid();

	if ((t.pid = fork()) < 0) {
		error("fork: %m");
		return ESLURMD_FORK_FAILED;
	} else if (t.pid == 0)   /* child */
		_task_exec(job, 0, true);

	/* Parent continues: */

	job->task[0]->pid = t.pid;

	if (shm_add_task(job->jobid, job->stepid, &t) < 0) {
		error("job %d: shm_add_task: %m", job->jobid);
		return ESLURMD_SHARED_MEMORY_ERROR;
	}

	while ((pid = waitpid(0, &status, 0)) < 0 && (pid != t.pid)) {
		if ((pid < 0) && (errno == EINTR)) {
			_handle_attach_req(job);
			continue;
		} else if (pid < 0)
			error("waitpid: %m");
	}

	verbose("batch job %d exited with status %d", job->jobid, status);

	/* Wait for io to complete */
	io_close_all(job);
	pthread_join(job->ioid, NULL);

	return status;
}

static void
_hup_handler(int sig) {;}

static void
_wait_for_all_tasks(slurmd_job_t *job)
{
	int waiting = job->ntasks;
	int i;

	xsignal(SIGHUP, _hup_handler);

	while (waiting > 0) {
		int status;
		pid_t pid = waitpid(0, &status, 0);
		if ((pid < (pid_t) 0)) {
			if (errno == EINTR)
				_handle_attach_req(job);
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
_drop_privileges(struct passwd *pwd)
{
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

	if ((rc = _become_user(job)) < 0) 
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

	debug3("All tasks exited");
	return;
}

static int 
_send_exit_msg(int rc, task_info_t *t)
{
	slurm_msg_t     resp;
	task_exit_msg_t msg;
	uint32_t task_id_list[1];
	ListIterator i;
	srun_info_t *srun;

	debug3("sending task exit msg for %d", t->gid);

	/* FIXME:XXX: attempt to combine task IDs in single message */
	task_id_list[0]  = t->gid;
	msg.task_id_list = task_id_list;
	msg.num_tasks    = 1;
       	msg.return_code  = rc;
	resp.data        = &msg;
	resp.msg_type    = MESSAGE_TASK_EXIT;

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
