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

#include "src/slurmd/slurmd.h"
#include "src/slurmd/setenvpf.h"
#include "src/slurmd/mgr.h"
#include "src/slurmd/io.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/interconnect.h"

static int  _run_job(slurmd_job_t *job);
static int  _exec_all_tasks(slurmd_job_t *job);
static void _task_exec(slurmd_job_t *job, int i);
static int  _drop_privileges(struct passwd *pwd);
static int  _reclaim_privileges(struct passwd *pwd);
static int  _become_user(slurmd_job_t *job);
static int  _unblock_all_signals(void);
static int  _block_most_signals(void);
static int  _send_exit_msg(int rc, task_info_t *t);
static int  _complete_job(slurmd_job_t *job, int rc, int status);
static void _send_batch_launch_resp(slurmd_job_t *job);
static void _send_launch_resp(slurmd_job_t *job, int rc);
static void _wait_for_all_tasks(slurmd_job_t *job);
static void _slurmd_job_log_init(slurmd_job_t *job);

static void
_setargs(slurmd_job_t *job, char **argv, int argc)
{
	int i;
	size_t len = 0;
	char *arg  = NULL;

	for (i = 0; i < argc; i++) 
		len += strlen(argv[i]) + 1;

	if (job->stepid == NO_VAL)
		xstrfmtcat(arg, "[%d]", job->jobid);
	else
		xstrfmtcat(arg, "[%d.%d]", job->jobid, job->stepid); 

	if (len < (strlen(arg) + 7))
		goto done;

	memset(argv[0], 0, len);
	strncpy(argv[0], "slurmd", 6);
	strncpy((*argv)+7, arg, strlen(arg));

    done:
	xfree(arg);
	return;
}

/* Launch a job step on this node
 */
int
mgr_launch_tasks(launch_tasks_request_msg_t *msg, slurm_addr *cli)
{
	slurmd_job_t *job;
	char buf[256];

	snprintf(buf, sizeof(buf), "[%d.%d]", msg->job_id, msg->job_step_id);
	log_set_fpfx(buf);

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

	debug2("%ld returned from slurmd_run_job()", getpid());
	shm_fini();
	return(SLURM_SUCCESS);
  error:
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
	char         buf[256];

	snprintf(buf, sizeof(buf), "[%d]", msg->job_id);
	log_set_fpfx(buf);

	/* New process, so must reinit shm */
	if ((rc = shm_init()) < 0) 
		goto cleanup1;

	if (!(job = job_batch_job_create(msg))) 
		goto cleanup2;

	/*
	 * This is now done in _run_job() 
	 */
	/* job_update_shm(job); */

	_setargs(job, *conf->argv, *conf->argc);

	if ((batchdir = _make_batch_dir(job)) == NULL) 
		goto cleanup2;

	xfree(job->argv[0]);

	if ((job->argv[0] = _make_batch_script(msg, batchdir)) == NULL)
		goto cleanup3;

	if ((rc = _setup_batch_env(job, msg->nodes)) < 0)
		goto cleanup;

	status = _run_job(job);
		
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
	struct passwd *spwd = getpwuid(geteuid());

	_block_most_signals();

	/* Insert job info into shared memory */
	job_update_shm(job);

	if (!job->batch && interconnect_init(job) == SLURM_ERROR) {
		error("interconnect_init: %m");
		rc = errno;
		/* shm_init(); */
		goto fail;
	}

	if ((rc = io_spawn_handler(job)) < 0) {
		rc = ESLURMD_IO_ERROR;
		goto fail1;
	}

	/* connect job stderr to this node's task 0 stderr so
	 * user recieves error messages on stderr
	 */
	_slurmd_job_log_init(job);

	/*
	 * Temporarily drop permissions 
	 */
	if ((rc = _drop_privileges(job->pwd)) < 0) {
		rc = ESLURMD_SET_UID_OR_GID_ERROR;
		goto fail2;
	}

	/* Open input/output files and/or connections back to client
	 */
	rc = io_prepare_clients(job);

	if (_reclaim_privileges(spwd) < 0) 
		error("sete{u/g}id(%ld/%ld): %m", spwd->pw_uid, spwd->pw_gid);


	if (rc < 0) {
		rc = ESLURMD_IO_ERROR;
		goto fail2;
	}

	rc = _exec_all_tasks(job);
	if (job->batch)
		_send_batch_launch_resp(job);
	else
		_send_launch_resp(job, rc);
	_wait_for_all_tasks(job);

	debug2("all tasks exited, waiting on IO");
	io_close_all(job);
	pthread_join(job->ioid, NULL);
	debug2("IO complete");

	if (!job->batch)
		interconnect_fini(job); /* ignore errors        */
	job_delete_shm(job);            /* again, ignore errors */
	verbose("job completed, rc = %d", rc);
	return rc;

fail2:
	io_close_all(job);
	pthread_join(job->ioid, NULL);
fail1:
	if (!job->batch)
		interconnect_fini(job);
fail:
	job_delete_shm(job);
	if (!job->batch)
		_send_launch_resp(job, rc);
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
			if (errno == EINTR) {
				_handle_attach_req(job);
				continue;
			}
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
	if (getuid() == pwd->pw_uid)
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
_task_exec(slurmd_job_t *job, int i)
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
	if (!job->batch && (interconnect_attach(job, i) < 0)) {
		error("interconnect attach failed: %m");
		exit(1);
	}

	if (!job->batch && (interconnect_env(job, i) < 0)) {
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

#ifdef HAVE_TOTALVIEW
	/* Stop the tasks on exec for TotalView to connect */
	if ((job->task_flags & TASK_TOTALVIEW_DEBUG) &&
	    (ptrace(PTRACE_TRACEME, 0, NULL, NULL) == -1))
		error("ptrace: %m");
#endif

	/* exec the cmdline */
	execve(job->argv[0], job->argv, job->env);

	/* error and clean up if execve() returns:
	 */
	error("execve(): %s: %m", job->argv[0]); 
	exit(errno);
}

static int
_exec_all_tasks(slurmd_job_t *job)
{
	pid_t sid;
	int i;

	debug3("%ld entered _launch_tasks", getpid());

	xsignal(SIGPIPE, SIG_IGN);

	if ((sid = setsid()) < (pid_t) 0) {
		error("setsid: %m");
	}

	_block_most_signals();

	if (shm_update_step_sid(job->jobid, job->stepid, sid) < 0)
		error("shm_update_step_sid: %m");
	
	debug2("invoking %d tasks", job->ntasks);

	for (i = 0; i < job->ntasks; i++) {
		task_t t;
		debug2("going to fork task %d", i);
		t.id = i;
		t.global_id = job->task[i]->gid;
		t.ppid      = getpid();

		if ((t.pid = fork()) < 0) {
			error("fork: %m");
			return 1;
			/* job_cleanup() */
		} else if (t.pid == 0)   /* child */
			break;

		/* Parent continues loop: */

		job->task[i]->pid = t.pid;

		debug2("%ld: forked child process %ld for task %d", 
				getpid(), (long) t.pid, i);  
		debug2("going to add task %d to shm", i);
		if (shm_add_task(job->jobid, job->stepid, &t) < 0)
			error("shm_add_task: %m");
		debug2("task %d added to shm", i);
#ifdef HAVE_TOTALVIEW
		/* If task to be debugged, wait for it to stop via
		 * child's ptrace(PTRACE_TRACEME), then SIGSTOP, and 
		 * ptrace(PTRACE_DETACH). This requires a kernel patch,
 		 * which you probably already have in place for TotalView:
 		 * http://hypermail.idiosynkrasia.net/linux-kernel/
		 *	archived/2001/week51/1193.html */
		if (job->task_flags & TASK_TOTALVIEW_DEBUG) {
			int status;
			waitpid(t.pid, &status, WUNTRACED);
			if (kill(t.pid, SIGSTOP))
				error("kill %ld: %m", (long) t.pid);
			if (ptrace(PTRACE_DETACH, (long) t.pid, NULL, NULL))
				error("ptrace %ld: %m", (long) t.pid);
		}
#endif

	}

	if (i == job->ntasks) 
		return 0; /* _wait_for_all_tasks(job); */
	else
		_task_exec(job, i);

	debug3("All tasks exited");
	return 0;
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
		if (resp.address.sin_family != 0)
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

static int
_block_most_signals(void)
{
	sigset_t set;
	if (sigemptyset(&set) < 0) {
		error("sigemptyset: %m");
		return SLURM_ERROR;
	}
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	/* sigaddset(&set, SIGSTOP); */
	sigaddset(&set, SIGTSTP);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGPIPE);
	if (sigprocmask(SIG_BLOCK, &set, NULL) < 0) {
		error("sigprocmask: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static void
_send_batch_launch_resp(slurmd_job_t *job)
{	
	slurm_msg_t resp_msg;
	batch_launch_response_msg_t resp;
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL;
	bool        found  = false;

	while ((s = list_next(i))) {
		if (s->jobid == job->jobid) {
			resp.sid = s->sid;
			found = true;
			break;
		}
	}
	list_destroy(steps);
	if (!found)
		error("failed to find jobid %u in shared memory", job->jobid);
	else {
		debug("Sending batch launch resp");
		resp_msg.data         = &resp;
		resp_msg.msg_type     = RESPONSE_BATCH_JOB_LAUNCH;

		/* resp.sid           = s->sid; set above */
		resp.job_id           = job->jobid;
		slurm_send_only_controller_msg(&resp_msg);
	}
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

	/* Connect slurmd stderr to job's stderr */
	if (dup2(job->task[0]->perr[1], STDERR_FILENO) < 0) {
		error("job_log_init: dup2(stderr): %m");
		return;
	}

	fd_set_nonblocking(STDERR_FILENO);

	snprintf(argv0, sizeof(argv0), "slurmd[%s]", conf->hostname);

	/* 
	 * reinitialize log 
	 */
	log_init(argv0, conf->log_opts, 0, NULL);
}

int 
run_script(bool prolog, const char *path, uint32_t jobid, uid_t uid)
{
	int status;
	pid_t cpid;
	char *name = prolog ? "prolog" : "epilog";

	if (path == NULL || path[0] == '\0')
		return 0;

	debug("[job %d] attempting to run %s [%s]", jobid, name, path);

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
		int envc = 0;


		env = xmalloc(sizeof(char *));

		argv[0] = xstrdup(path);
		argv[1] = NULL;

		env[0]  = NULL;
		setenvpf(&env, &envc, "SLURM_JOBID=%u", jobid);
		setenvpf(&env, &envc, "SLURM_UID=%u",   uid);

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
