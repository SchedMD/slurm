/*****************************************************************************\
 * src/slurmd/req.c - slurmd request handling
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

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/poll.h>
#include <sys/wait.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/util-net.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/mgr.h"

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif


static bool _slurm_authorized_user(uid_t uid);
static bool _job_still_running(uint32_t job_id);
static int  _kill_all_active_steps(uint32_t jobid, int sig);
static int  _launch_tasks(launch_tasks_request_msg_t *, slurm_addr *);
static void _rpc_launch_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_spawn_task(slurm_msg_t *, slurm_addr *);
static void _rpc_batch_job(slurm_msg_t *, slurm_addr *);
static void _rpc_kill_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_timelimit(slurm_msg_t *, slurm_addr *);
static void _rpc_reattach_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_kill_job(slurm_msg_t *, slurm_addr *);
static void _rpc_update_time(slurm_msg_t *, slurm_addr *);
static void _rpc_shutdown(slurm_msg_t *msg, slurm_addr *cli_addr);
static void _rpc_reconfig(slurm_msg_t *msg, slurm_addr *cli_addr);
static void _rpc_pid2jid(slurm_msg_t *msg, slurm_addr *);
static int  _rpc_ping(slurm_msg_t *, slurm_addr *);
static int  _run_prolog(uint32_t jobid, uid_t uid);
static int  _run_epilog(uint32_t jobid, uid_t uid);
static int  _spawn_task(spawn_task_request_msg_t *, slurm_addr *);

static bool _pause_for_job_completion (uint32_t jobid, int maxtime);
static int _waiter_init (uint32_t jobid);
static int _waiter_complete (uint32_t jobid);

/*
 *  List of threads waiting for jobs to complete
 */
static List waiters;

static pthread_mutex_t launch_mutex = PTHREAD_MUTEX_INITIALIZER;

void
slurmd_req(slurm_msg_t *msg, slurm_addr *cli)
{
	int rc;

	switch(msg->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		slurm_mutex_lock(&launch_mutex);
		_rpc_batch_job(msg, cli);
		slurm_free_job_launch_msg(msg->data);
		slurm_mutex_unlock(&launch_mutex);
		break;
	case REQUEST_LAUNCH_TASKS:
		slurm_mutex_lock(&launch_mutex);
		_rpc_launch_tasks(msg, cli);
		slurm_free_launch_tasks_request_msg(msg->data);
		slurm_mutex_unlock(&launch_mutex);
		break;
	case REQUEST_SPAWN_TASK:
		slurm_mutex_lock(&launch_mutex);
		_rpc_spawn_task(msg, cli);
		slurm_free_spawn_task_request_msg(msg->data);
		slurm_mutex_unlock(&launch_mutex);
		break;
	case REQUEST_KILL_TASKS:
		_rpc_kill_tasks(msg, cli);
		slurm_free_kill_tasks_msg(msg->data);
		break;
	case REQUEST_KILL_TIMELIMIT:
		_rpc_timelimit(msg, cli);
		slurm_free_timelimit_msg(msg->data);
		break; 
	case REQUEST_REATTACH_TASKS:
		_rpc_reattach_tasks(msg, cli);
		slurm_free_reattach_tasks_request_msg(msg->data);
		break;
	case REQUEST_KILL_JOB:
		_rpc_kill_job(msg, cli);
		slurm_free_kill_job_msg(msg->data);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		_rpc_update_time(msg, cli);
		slurm_free_update_job_time_msg(msg->data);
		break;
	case REQUEST_SHUTDOWN:
		_rpc_shutdown(msg, cli);
		slurm_free_shutdown_msg(msg->data);
		break;
	case REQUEST_RECONFIGURE:
		_rpc_reconfig(msg, cli);
		/* No body to free */
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		/* Treat as ping (for slurmctld agent, just return SUCCESS) */
		rc = _rpc_ping(msg, cli);
		slurm_free_node_registration_status_msg(msg->data);
		/* Then initiate a separate node registration */
		if (rc == SLURM_SUCCESS)
			send_registration_msg(SLURM_SUCCESS);
		break;
	case REQUEST_PING:
		_rpc_ping(msg, cli);
		/* No body to free */
		break;
	case REQUEST_JOB_ID:
		_rpc_pid2jid(msg, cli);
		slurm_free_job_id_request_msg(msg->data);
		break;
	default:
		error("slurmd_req: invalid request msg type %d\n",
		      msg->msg_type);
		slurm_send_rc_msg(msg, EINVAL);
		break;
	}
	return;
}

/*
 *  Need to close all open fds
 */
static void
_close_fds(void)
{
	int i;
	int maxfd = 1024;
	for (i = 4; i < maxfd; i++) {
		close(i);
	}
}

static int
_fork_new_slurmd(void)
{
	pid_t pid;
	int fds[2] = {-1, -1};
	char c;

	/* Idea taken from ConMan by Chris Dunlap:
	 *  Create pipe for IPC so parent slurmd will wait
	 *  to return until signaled by grandchild process that
	 *  slurmd job manager has been successfully created.
	 */
	if (pipe(fds) < 0)
		error("fork_slurmd: pipe: %m");
	
	if ((pid = fork()) < 0) 
		error("fork_slurmd: fork: %m");
	else if (pid > 0) {
		if ((fds[1] >= 0) && (close(fds[1]) < 0))
			error("Unable to close write-pipe in parent: %m");

		/*  Wait for grandchild */
		if ((fds[0] >= 0) && (read(fds[0], &c, 1) < 0))
			return error("Unable to read EOF from grandchild: %m");
		if ((fds[0] >= 0) && (close(fds[0]) < 0))
			error("Unable to close read-pipe in parent: %m");

		/* Reap child */
		if (waitpid(pid, NULL, 0) < 0)
			error("Unable to reap slurmd child process");

		return ((int) pid);
	}

	if (close(fds[0]) < 0)
		error("Unable to close read-pipe in child: %m");

	if (setsid() < 0)
		error("fork_slurmd: setsid: %m");

	if ((pid = fork()) < 0)
		error("fork_slurmd: Unable to fork grandchild: %m");
	else if (pid > 0)
		exit(0);

	/* Grandchild continues */

	if (close(fds[1]) < 0)
		error("Unable to close write-pipe in grandchild: %m");

	/*
	 *  We could destroy the credential context object here. 
	 *   However, since we have forked from the main slurmd,
	 *   any mutexes protecting this object (and objects it
	 *   contains) will not be in a sane state on some systems
	 *   (e.g. RH73). For now, just let it stay in memory.
	 *
	 *  slurm_cred_ctx_destroy(conf->vctx);
	 */

	slurm_shutdown_msg_engine(conf->lfd);
	_close_fds();

	/*
	 *  Reopen logfile by calling log_alter() without
	 *    changing log options
	 */   
	log_alter(conf->log_opts, 0, conf->logfile);

	/* 
	 * Return 0 to indicate this is a child slurmd
	 */
	return(0);
}

static int
_launch_batch_job(batch_job_launch_msg_t *req, slurm_addr *cli)
{	
	int retval;
	
	if ((retval = _fork_new_slurmd()) == 0) 
		exit (mgr_launch_batch_job(req, cli));

	return (retval <= 0) ? retval : 0;
}

static int
_launch_tasks(launch_tasks_request_msg_t *req, slurm_addr *cli)
{
	int retval;

	if ((retval = _fork_new_slurmd()) == 0)
		exit (mgr_launch_tasks(req, cli));

	return (retval <= 0) ? retval : 0;
}

static int
_spawn_task(spawn_task_request_msg_t *req, slurm_addr *cli)
{
	int retval;

	if ((retval = _fork_new_slurmd()) == 0)
		exit (mgr_spawn_task(req, cli));

	return (retval <= 0) ? retval : 0;
}

static int
_check_job_credential(slurm_cred_t cred, uint32_t jobid, 
		      uint32_t stepid, uid_t uid)
{
	slurm_cred_arg_t arg;
	hostset_t        hset    = NULL;
	bool             user_ok = _slurm_authorized_user(uid); 

	/*
	 * First call slurm_cred_verify() so that all valid
	 * credentials are checked
	 */
	if ((slurm_cred_verify(conf->vctx, cred, &arg) < 0) && !user_ok)
		return SLURM_ERROR;

	/*
	 * If uid is the slurm user id or root, do not bother
	 * performing validity check of the credential
	 */
	if (user_ok)
		return SLURM_SUCCESS;

	if ((arg.jobid != jobid) || (arg.stepid != stepid)) {
		error("job credential for %d.%d, expected %d.%d",
		      arg.jobid, arg.stepid, jobid, stepid); 
		goto fail;
	}

	if (arg.uid != uid) {
		error("job credential created for uid %ld, expected %ld",
		      (long) arg.uid, (long) uid);
		goto fail;
	}

	/*
	 * Check that credential is valid for this host
	 */
	if (!(hset = hostset_create(arg.hostlist))) {
		error("Unable to parse credential hostlist: `%s'", 
		      arg.hostlist);
		goto fail;
	}

	if (!hostset_within(hset, conf->hostname)) {
		error("job credential invald for this host [%d.%d %ld %s]",
		      arg.jobid, arg.stepid, (long) arg.uid, arg.hostlist);
		goto fail;
	}

	hostset_destroy(hset);
	xfree(arg.hostlist);

	return SLURM_SUCCESS;

    fail:
	if (hset) hostset_destroy(hset);
	xfree(arg.hostlist);
	slurm_seterrno_ret(ESLURMD_INVALID_JOB_CREDENTIAL);
}


static void 
_rpc_launch_tasks(slurm_msg_t *msg, slurm_addr *cli)
{
	int      errnum = 0;
	uint16_t port;
	char     host[MAXHOSTNAMELEN];
	uid_t    req_uid;
	launch_tasks_request_msg_t *req = msg->data;
	uint32_t jobid  = req->job_id;
	uint32_t stepid = req->job_step_id;
	bool     super_user = false, run_prolog = false;

	req_uid = g_slurm_auth_get_uid(msg->cred);

	super_user = _slurm_authorized_user(req_uid);

	if ((super_user == false) && (req_uid != req->uid)) {
		error("launch task request from uid %u",
		      (unsigned int) req_uid);
		errnum = ESLURM_USER_ID_MISSING;	/* or invalid user */
		goto done;
	}

	slurmd_get_addr(cli, &port, host, sizeof(host));
	info("launch task %u.%u request from %u@%s", req->job_id, 
	     req->job_step_id, req->uid, host);

	if (!slurm_cred_jobid_cached(conf->vctx, req->job_id)) 
		run_prolog = true;

	if (_check_job_credential(req->cred, jobid, stepid, req_uid) < 0) {
		errnum = errno;
		error("Invalid job credential from %ld@%s: %m", 
		      (long) req_uid, host);
		goto done;
	}

	/* xassert(slurm_cred_jobid_cached(conf->vctx, req->job_id));*/

	/* Run job prolog if necessary */
	if (run_prolog && (_run_prolog(req->job_id, req->uid) != 0)) {
		error("[job %u] prolog failed", req->job_id);
		errnum = ESLURMD_PROLOG_FAILED;
		goto done;
	}

	if (_launch_tasks(req, cli) < 0)
		errnum = errno;

    done:
	if (slurm_send_rc_msg(msg, errnum) < 0) {

		error("launch_tasks: unable to send return code: %m");

		/*
		 * Rewind credential so that srun may perform retry
		 */
		slurm_cred_rewind(conf->vctx, req->cred); /* ignore errors */

	} else if (errnum == SLURM_SUCCESS)
		save_cred_state(conf->vctx);

	/*
	 *  If job prolog failed, indicate failure to slurmctld
	 */
	if (errnum == ESLURMD_PROLOG_FAILED)
		send_registration_msg(errnum);	
}


static void 
_rpc_spawn_task(slurm_msg_t *msg, slurm_addr *cli)
{
	int      errnum = 0;
	uint16_t port;
	char     host[MAXHOSTNAMELEN];
	uid_t    req_uid;
	spawn_task_request_msg_t *req = msg->data;
	uint32_t jobid  = req->job_id;
	uint32_t stepid = req->job_step_id;
	bool     super_user = false, run_prolog = false;

	req_uid = g_slurm_auth_get_uid(msg->cred);

	super_user = _slurm_authorized_user(req_uid);

	if ((super_user == false) && (req_uid != req->uid)) {
		error("spawn task request from uid %u",
		      (unsigned int) req_uid);
		errnum = ESLURM_USER_ID_MISSING;	/* or invalid user */
		goto done;
	}

	slurmd_get_addr(cli, &port, host, sizeof(host));
	info("spawn task %u.%u request from %u@%s", req->job_id, 
	     req->job_step_id, req->uid, host);

	if (!slurm_cred_jobid_cached(conf->vctx, req->job_id)) 
		run_prolog = true;

	if (_check_job_credential(req->cred, jobid, stepid, req_uid) < 0) {
		errnum = errno;
		error("Invalid job credential from %ld@%s: %m", 
		      (long) req_uid, host);
		goto done;
	}

	/* xassert(slurm_cred_jobid_cached(conf->vctx, req->job_id));*/

	/* Run job prolog if necessary */
	if (run_prolog && (_run_prolog(req->job_id, req->uid) != 0)) {
		error("[job %u] prolog failed", req->job_id);
		errnum = ESLURMD_PROLOG_FAILED;
		goto done;
	}

	if (_spawn_task(req, cli) < 0)
		errnum = errno;

    done:
	if (slurm_send_rc_msg(msg, errnum) < 0) {

		error("spawn_task: unable to send return code: %m");

		/*
		 * Rewind credential so that srun may perform retry
		 */
		slurm_cred_rewind(conf->vctx, req->cred); /* ignore errors */

	} else if (errnum == SLURM_SUCCESS)
		save_cred_state(conf->vctx);

	/*
	 *  If job prolog failed, indicate failure to slurmctld
	 */
	if (errnum == ESLURMD_PROLOG_FAILED)
		send_registration_msg(errnum);	
}
static void
_rpc_batch_job(slurm_msg_t *msg, slurm_addr *cli)
{
	batch_job_launch_msg_t *req = (batch_job_launch_msg_t *)msg->data;
	int      rc = SLURM_SUCCESS;
	uid_t    req_uid = g_slurm_auth_get_uid(msg->cred);

	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation, batch launch RPC from uid %u",
		      (unsigned int) req_uid);
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
		goto done;
	} 

	/* 
	 * Run job prolog on this node
	 */
	if (_run_prolog(req->job_id, req->uid) != 0) {
		error("[job %u] prolog failed", req->job_id);
		rc = ESLURMD_PROLOG_FAILED;
		goto done;
	} 
	
	/*
	 * Insert jobid into credential context to denote that
	 * we've now "seen" an instance of the job
	 */
	slurm_cred_insert_jobid(conf->vctx, req->job_id);

	info("Launching batch job %u for UID %d", req->job_id, req->uid);

	rc = _launch_batch_job(req, cli);

    done:
	slurm_send_rc_msg(msg, rc);
}

static void
_rpc_reconfig(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->cred);

	if (!_slurm_authorized_user(req_uid))
		error("Security violation, reconfig RPC from uid %u",
		      (unsigned int) req_uid);
	else
		kill(conf->pid, SIGHUP);

	/* Never return a message, slurmctld does not expect one */
}

static void
_rpc_shutdown(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	uid_t req_uid = g_slurm_auth_get_uid(msg->cred);

	if (!_slurm_authorized_user(req_uid))
		error("Security violation, shutdown RPC from uid %u",
		      (unsigned int) req_uid);
	else
		kill(conf->pid, SIGTERM);

	/* Never return a message, slurmctld does not expect one */
}

static int
_rpc_ping(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	int        rc = SLURM_SUCCESS;
	uid_t req_uid = g_slurm_auth_get_uid(msg->cred);

	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation, ping RPC from uid %u",
		      (unsigned int) req_uid);
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	}

	/* Return result. If the reply can't be sent this indicates that
	 * 1. The network is broken OR
	 * 2. slurmctld has died    OR
	 * 3. slurmd was paged out due to full memory
	 * If the reply request fails, we send an registration message to 
	 * slurmctld in hopes of avoiding having the node set DOWN due to
	 * slurmd paging and not being able to respond in a timely fashion. */
	if (slurm_send_rc_msg(msg, rc) < 0) {
		error("Error responding to ping: %m");
		send_registration_msg(SLURM_SUCCESS);
	}
	return rc;
}

static void
_rpc_kill_tasks(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	int               rc = SLURM_SUCCESS;
	uid_t             req_uid;
	job_step_t       *step = NULL;
	kill_tasks_msg_t *req = (kill_tasks_msg_t *) msg->data;

	if (!(step = shm_get_step(req->job_id, req->job_step_id))) {
		debug("kill for nonexistent job %u.%u requested",
		      req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	} 

	req_uid = g_slurm_auth_get_uid(msg->cred);
	if ((req_uid != step->uid) && (!_slurm_authorized_user(req_uid))) {
	       debug("kill req from uid %ld for job %u.%u owned by uid %ld",
		     (long) req_uid, req->job_id, req->job_step_id, 
		     (long) step->uid);       
	       rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	       goto done;
	}

	if (step->state != SLURMD_JOB_STARTED) {
		debug ("kill req for starting job step %d.%d", 
		       step->jobid, step->stepid);
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	if (step->sid <= (pid_t) 0) {
		debug ("step %ld.%d invalid in shm [mpid:%d sid:%d]", 
			req->job_id, req->job_step_id, 
			step->mpid, step->sid);
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	if ((step->sid > (pid_t) 0)
	 && (kill(-step->sid, req->signal) < 0))
		rc = errno; 

	if ((step->task_list->pid > (pid_t) 0)
	&&  (kill (-step->task_list->pid, req->signal) < 0))
		rc = errno;

	if (rc == SLURM_SUCCESS)
		verbose("Sent signal %d to %u.%u", 
			req->signal, req->job_id, req->job_step_id);
	else
		verbose("Error sending signal %d to %u.%u: %s", 
			req->signal, req->job_id, req->job_step_id, 
			slurm_strerror(rc));

  done:
	if (step)
		shm_free_step(step);
	slurm_send_rc_msg(msg, rc);
}

static void
_kill_running_session_mgrs(uint32_t jobid, int signum, char *signame)
{
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL;
	int          cnt   = 0;	

	while ((s = list_next(i))) {
		if ((s->jobid == jobid) && (s->sid > (pid_t) 0)) {
			kill(s->sid, signum);
			cnt++;
		}
	}
	list_destroy(steps);
	if (cnt)
		verbose("Job %u: sent %s to %d active steps",
			jobid, signame, cnt);

	return;
}

/* 
 *  For the specified job_id: Send SIGXCPU to the smgr, reply to slurmctld, 
 *   sleep(configured kill_wait), then send SIGKILL 
 */
static void
_rpc_timelimit(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	uid_t           uid = g_slurm_auth_get_uid(msg->cred);
	kill_job_msg_t *req = msg->data;
	int             nsteps;

	if (!_slurm_authorized_user(uid)) {
		error ("Security violation: rpc_timelimit req from uid %ld", 
		       (long) uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	/*
	 *  Indicate to slurmctld that we've recieved the message
	 */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
	slurm_close_accepted_conn(msg->conn_fd);
	msg->conn_fd = -1;

	/*
	 *  Send SIGXCPU to warn session managers of job steps for this
	 *   job that the job is about to be terminated
	 */
	_kill_running_session_mgrs(req->job_id, SIGXCPU, "SIGXCPU");

	nsteps = _kill_all_active_steps(req->job_id, SIGTERM);

	verbose( "Job %u: timeout: sent SIGTERM to %d active steps", 
	         req->job_id, nsteps );

	sleep(1);

	/*
	 * Check to see if any processes are still around
	 */
	if ((nsteps > 0) && _job_still_running(req->job_id)) {
		verbose( "Job %u: waiting %d secs for SIGKILL", 
			 req->job_id, conf->cf.kill_wait       );
		sleep (conf->cf.kill_wait - 1);
	}

	/* SIGKILL and send response */
	_rpc_kill_job(msg, cli_addr); 
}

static void  _rpc_pid2jid(slurm_msg_t *msg, slurm_addr *cli)
{
	job_id_request_msg_t *req = (job_id_request_msg_t *) msg->data;
	slurm_msg_t           resp_msg;
	job_id_response_msg_t resp;
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL;  
	bool         found = false; 
	pid_t	     mysid = getsid(req->job_pid);

	if (mysid == -1)
		error("getsid: %m");
	while ((mysid != -1) && (s = list_next(i))) {
		if (s->sid == mysid) {
			resp.job_id = s->jobid;
			found = true;
			break;
		}
	}
	list_destroy(steps);

	if (found) {
		resp_msg.address      = msg->address;
		resp_msg.msg_type     = RESPONSE_JOB_ID;
		resp_msg.data         = &resp;
		slurm_send_node_msg(msg->conn_fd, &resp_msg);
	} else {
		/* We could possibly scan the proc table and figure 
		 * out which job this pid belongs to, but for now 
		 * we only handle the job's top level pid */
		debug3("_rpc_pid2jid: pid(%u) not found", req->job_pid);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
	}
}


static void 
_rpc_reattach_tasks(slurm_msg_t *msg, slurm_addr *cli)
{
	int          rc   = SLURM_SUCCESS;
	uint16_t     port = 0;
	char         host[MAXHOSTNAMELEN];
	int          i;
	job_step_t  *step;
	job_state_t *state;
	task_t      *t;
	uid_t        req_uid;
	gid_t        req_gid;
	slurm_addr   ioaddr;
	char        *key;
	int          len;
	slurm_msg_t                    resp_msg;
	reattach_tasks_request_msg_t  *req = msg->data;
	reattach_tasks_response_msg_t  resp;

	memset(&resp, 0, sizeof(reattach_tasks_response_msg_t));
	slurmd_get_addr(cli, &port, host, sizeof(host));
	req_uid = g_slurm_auth_get_uid(msg->cred);
	req_gid = g_slurm_auth_get_gid(msg->cred);

	info("reattach request from %ld@%s for %u.%u", 
	     (long) req_uid, host, req->job_id, req->job_step_id);

	/* 
	 * Set response addr by resp_port and client address
	 */
	memcpy(&resp_msg.address, cli, sizeof(slurm_addr));
	slurm_set_addr(&resp_msg.address, req->resp_port, NULL); 

	if (!(step = shm_get_step(req->job_id, req->job_step_id))) {
		rc = ESRCH;
		goto done;
	}
	
	if ((step->uid != req_uid) && (req_uid != 0)) {
		error("uid %ld attempt to attach to job %u.%u owned by %ld",
				(long) req_uid, req->job_id, req->job_step_id,
				(long) step->uid);
		rc = EPERM;
		goto done;
	}

	state = shm_lock_step_state(req->job_id, req->job_step_id);
	if (  (*state != SLURMD_JOB_STARTING) 
	   && (*state != SLURMD_JOB_STARTED)  ) {
		shm_unlock_step_state(req->job_id, req->job_step_id);
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}
	shm_unlock_step_state(req->job_id, req->job_step_id);

	/* 
	 * Set IO and response addresses in shared memory
	 */
	memcpy(&ioaddr, cli, sizeof(slurm_addr));
	slurm_set_addr(&ioaddr, req->io_port, NULL);
	slurmd_get_addr(&ioaddr, &port, host, sizeof(host));

	debug3("reattach: srun ioaddr: %s:%d", host, port);


	slurm_cred_get_signature(req->cred, &key, &len);

	while (1) {
		rc = shm_update_step_addrs( req->job_id, req->job_step_id,
				            &ioaddr, &resp_msg.address,
				            key ); 
		if ((rc == 0) || (errno != EAGAIN))
			break;
		sched_yield();	/* relinquish processor */
	}

	resp.local_pids = xmalloc(step->ntasks * sizeof(*resp.local_pids));
	resp.gids       = xmalloc(step->ntasks * sizeof(*resp.local_pids));
	resp.ntasks     = step->ntasks;
	for (t = step->task_list, i = 0; t; t = t->next, i++) {
		resp.gids[t->id] = t->global_id;
		resp.local_pids[t->id] = t->pid;
	}
	resp.executable_name  = xstrdup(step->exec_name);

	shm_free_step(step);

    done:
	debug2("update step addrs rc = %d", rc);
	resp_msg.data         = &resp;
	resp_msg.msg_type     = RESPONSE_REATTACH_TASKS;
	resp.node_name        = conf->hostname;
	resp.srun_node_id     = req->srun_node_id;
	resp.return_code      = rc;

	slurm_send_only_node_msg(&resp_msg);

	xfree(resp.gids);
	xfree(resp.local_pids);

}


static int
_kill_all_active_steps(uint32_t jobid, int sig)
{
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL; 
	int step_cnt       = 0;  

	while ((s = list_next(i))) {
		if (s->jobid != jobid)		/* wrong job */
			continue;

		if (s->sid <= 0) {
			debug ("bad sid value in shm for %d!", jobid);
			continue;
		}

		/* XXX?
		 * We don't send anything but SIGKILL to batch jobs
		 */
		if ((s->stepid == NO_VAL) && (sig != SIGKILL))
			continue;

		step_cnt++;
		debug2("signal %d to job %u (pg:%d)", sig, jobid, s->sid);

		if ((s->sid > (pid_t) 0)
		&&  (kill(-s->sid, sig) < 0))
			error("kill jid %d sid %d: %m", s->jobid, s->sid);
		if ((s->task_list->pid  > (pid_t) 0)
		&&  (kill(-s->task_list->pid, sig) < 0))
			error("kill jid %d pgrp %d: %m", s->jobid, 
				s->task_list->pid);
	}
	list_destroy(steps);
	if (step_cnt == 0)
		debug2("No steps in jobid %d to send signal %d", jobid, sig);
	return step_cnt;
}

static bool
_job_still_running(uint32_t job_id)
{
	bool        retval = false;
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL;   

	while ((s = list_next(i))) {
		if ((s->jobid == job_id) &&
		    (shm_step_still_running(job_id, s->stepid))) {
			retval = true;
			break;
		}
	}
	list_destroy(steps);

	return retval;
}

/*
 *  Send epilog complete message to currently active comtroller.
 *   Returns SLURM_SUCCESS if message sent successfully,
 *           SLURM_FAILURE if epilog complete message fails to be sent.
 */
static int
_epilog_complete(uint32_t jobid, int rc)
{
	slurm_msg_t            msg;
	epilog_complete_msg_t  req;

	req.job_id      = jobid;
	req.return_code = rc;
	req.node_name   = conf->hostname;

	msg.msg_type    = MESSAGE_EPILOG_COMPLETE;
	msg.data        = &req;

	if (slurm_send_only_controller_msg(&msg) < 0) {
		error("Unable to send epilog complete message: %m");
		return SLURM_ERROR;
	}
	debug ("Job %u: sent epilog complete msg: rc = %d", jobid, rc);

	return SLURM_SUCCESS;
}

static void 
_rpc_kill_job(slurm_msg_t *msg, slurm_addr *cli)
{
	int             rc     = SLURM_SUCCESS;
	kill_job_msg_t *req    = msg->data;
	uid_t           uid    = g_slurm_auth_get_uid(msg->cred);
	int             nsteps = 0;

	/* 
	 * check that requesting user ID is the SLURM UID
	 */
	if (!_slurm_authorized_user(uid)) {
		error("Security violation: kill_job(%ld) from uid %ld",
		      req->job_id, (long) uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	} 

	/*
	 *  Initialize a "waiter" thread for this jobid. If another
	 *   thread is already waiting on termination of this job, 
	 *   _waiter_init() will return < 0. In this case, just 
	 *   notify slurmctld that we recvd the message successfully,
	 *   then exit this thread.
	 */
	if (_waiter_init (req->job_id) < 0) {
		slurm_send_rc_msg (msg, SLURM_SUCCESS);
		return;
	}


	/*
	 * "revoke" all future credentials for this jobid
	 */
	if (slurm_cred_revoke(conf->vctx, req->job_id) < 0) {
		debug("revoking cred for job %u: %m", req->job_id);
	} else {
		save_cred_state(conf->vctx);
		debug("credential for job %u revoked", req->job_id);
	}

	nsteps = _kill_all_active_steps(req->job_id, SIGTERM);

	/*
	 *  If there are currently no active job steps, and no
	 *    configured epilog to run, bypass asynchronous reply and
	 *    notify slurmctld that we have already completed this
	 *    request.
	 */
	if ((nsteps == 0) && !conf->epilog && (msg->conn_fd >= 0)) {
		slurm_send_rc_msg(msg, ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		slurm_cred_begin_expiration(conf->vctx, req->job_id);
		_waiter_complete(req->job_id);
		return;
	}

	/*
	 *  At this point, if connection still open, we send controller
	 *   a "success" reply to indicate that we've recvd the msg.
	 */
	if (msg->conn_fd >= 0) {
		slurm_send_rc_msg(msg, SLURM_SUCCESS);
		if (slurm_close_accepted_conn(msg->conn_fd) < 0)
			error ("rpc_kill_job: close(%d): %m", msg->conn_fd);
		msg->conn_fd = -1;
		
	}

	/*
	 *  Check for corpses
	 */
	if ( !_pause_for_job_completion (req->job_id, 5)
	   && _kill_all_active_steps(req->job_id, SIGKILL) ) {
		/*
		 *  Block until all user processes are complete.
		 */
		_pause_for_job_completion (req->job_id, 0);
	}

	/*
	 *  Begin expiration period for cached information about job.
	 *   If expiration period has already begun, then do not run
	 *   the epilog again, as that script has already been executed 
	 *   for this job.
	 */
	if (slurm_cred_begin_expiration(conf->vctx, req->job_id) < 0) {
		debug("Not running epilog for jobid %d: %m", req->job_id);
		goto done;
	}

	save_cred_state(conf->vctx);

	if (_run_epilog(req->job_id, req->job_uid) != 0) {
		error ("[job %u] epilog failed", req->job_id);
		rc = ESLURMD_EPILOG_FAILED;
	} else
		debug("completed epilog for jobid %u", req->job_id);
	
    done:
	_epilog_complete(req->job_id, rc);
	_waiter_complete(req->job_id);
}

/*
 *  Returns true if "uid" is a "slurm authorized user" - i.e. uid == 0
 *   or uid == slurm user id at this time.
 */
static bool
_slurm_authorized_user(uid_t uid)
{
	return ((uid == (uid_t) 0) || (uid == conf->slurm_user_id));
}


struct waiter {
	uint32_t jobid;
	pthread_t thd;
};


static struct waiter *
_waiter_create(uint32_t jobid)
{
	struct waiter *wp = xmalloc(sizeof(struct waiter));

	wp->jobid = jobid;
	wp->thd   = pthread_self();

	return wp;
}

static int _find_waiter(struct waiter *w, uint32_t *jp)
{
	return (w->jobid == *jp);
}

static void _waiter_destroy(struct waiter *wp)
{
	xfree(wp);
}

static int _waiter_init (uint32_t jobid)
{
	if (!waiters)
		waiters = list_create((ListDelF) _waiter_destroy);
	/* 
	 *  Exit this thread if another thread is waiting on job
	 */
	if (list_find_first (waiters, (ListFindF) _find_waiter, &jobid))
		return SLURM_ERROR;
	else 
		list_append(waiters, _waiter_create(jobid));

	return (SLURM_SUCCESS);
}

static int _waiter_complete (uint32_t jobid)
{
	return (list_delete_all (waiters, (ListFindF) _find_waiter, &jobid));
}

/*
 *  Like _wait_for_procs(), but only wait for up to maxtime seconds
 *    
 *  Returns true if all job 
 */
static bool
_pause_for_job_completion (uint32_t jobid, int maxtime)
{
	int sec = 0, rc = 0;

	while ( ((sec++ < maxtime) || (maxtime == 0))
	      && (rc = _job_still_running (jobid)))
		sleep (1);
	/* 
	 * Return true if job is NOT running
	 */
	return (!rc);
}

static void 
_rpc_update_time(slurm_msg_t *msg, slurm_addr *cli)
{
	int   rc      = SLURM_SUCCESS;
	uid_t req_uid = g_slurm_auth_get_uid(msg->cred);
	job_time_msg_t *req = (job_time_msg_t *) msg->data;

	if ((req_uid != conf->slurm_user_id) && (req_uid != 0)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, uid %u can't update time limit",
		      (unsigned int) req_uid);
		goto done;
	} 

	if (shm_update_job_timelimit(req->job_id, req->expiration_time) < 0) {
		error("updating lifetime for job %u: %m", req->job_id);
		rc = ESLURM_INVALID_JOB_ID;
	} else
		debug("reset job %u lifetime", req->job_id);

    done:
	slurm_send_rc_msg(msg, rc);
}

static int 
_run_prolog(uint32_t jobid, uid_t uid)
{
	int error_code;

	slurm_mutex_lock(&conf->config_mutex);
	error_code = run_script(true, conf->prolog, jobid, uid);
	slurm_mutex_unlock(&conf->config_mutex);
	return error_code;
}

static int 
_run_epilog(uint32_t jobid, uid_t uid)
{
	int error_code;

	slurm_mutex_lock(&conf->config_mutex);
	error_code = run_script(false, conf->epilog, jobid, uid);
	slurm_mutex_unlock(&conf->config_mutex);
	return error_code;
}


