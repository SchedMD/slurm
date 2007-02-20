/*****************************************************************************\
 *  src/slurmd/req.c - slurmd request handling
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

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/param.h>		/* MAXPATHLEN */
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_select.c"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/util-net.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/mgr.h"
#include "src/slurmd/proctrack.h"

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif


static int  _abort_job(uint32_t job_id);
static bool _slurm_authorized_user(uid_t uid);
static bool _job_still_running(uint32_t job_id);
static int  _kill_all_active_steps(uint32_t jobid, int sig, bool batch);
static int  _launch_tasks(launch_tasks_request_msg_t *, slurm_addr *,
			slurm_addr *);
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
static int  _run_prolog(uint32_t jobid, uid_t uid, char *bgl_part_id);
static int  _run_epilog(uint32_t jobid, uid_t uid, char *bgl_part_id);
static int  _spawn_task(spawn_task_request_msg_t *, slurm_addr *,
			slurm_addr *);

static bool _pause_for_job_completion (uint32_t jobid, int maxtime);
static int _waiter_init (uint32_t jobid);
static int _waiter_complete (uint32_t jobid);

static bool _steps_completed_now(uint32_t jobid);
static void _wait_state_completed(uint32_t jobid, int max_delay);

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
		/* Mutex locking moved into _rpc_batch_job() due to 
		 * very slow prolog on Blue Gene system. Only batch 
		 * jobs are supported on Blue Gene (no job steps). */
		_rpc_batch_job(msg, cli);
		slurm_free_job_launch_msg(msg->data);
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
		debug2("RPC: REQUEST_KILL_JOB");
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
		/* No body to free */
		/* Then initiate a separate node registration */
		if (rc == SLURM_SUCCESS)
			send_registration_msg(SLURM_SUCCESS, true);
		break;
	case REQUEST_PING:
		_rpc_ping(msg, cli);
		/* No body to free */
		break;
	case REQUEST_JOB_ID:
		_rpc_pid2jid(msg, cli);
		slurm_free_job_id_request_msg(msg->data);
		break;
	case MESSAGE_JOBACCT_DATA:
		{
			int rc=SLURM_SUCCESS;
			debug3("jobacct(%i) received jobacct message",
					getpid());
			slurm_send_rc_msg(msg,rc); /* ACK the message */
			debug3("jobacct(%i) sent jobacct rc=%d message",
					getpid(), rc);
			rc=g_slurm_jobacct_process_message(msg);
			debug3("jobacct(%i) slurm_jobacct_process_message "
					"rc=%d",
					getpid(), rc);
			slurm_free_jobacct_msg(msg->data);
		}
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
	if (pipe(fds) < 0) {
		error("fork_slurmd: pipe: %m");
		return -1;
	}
	
	if ((pid = fork()) < 0) { 
		error("fork_slurmd: fork: %m");
		close(fds[0]);
		close(fds[1]);
		return -1;
	} else if (pid > 0) {
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

#ifdef DISABLE_LOCALTIME
	disable_localtime();
#endif
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
_launch_tasks(launch_tasks_request_msg_t *req, slurm_addr *cli,
	      slurm_addr *self)
{
	int retval;

	if ((retval = _fork_new_slurmd()) == 0)
		exit (mgr_launch_tasks(req, cli, self));

	return (retval <= 0) ? retval : 0;
}

static int
_spawn_task(spawn_task_request_msg_t *req, slurm_addr *cli, slurm_addr *self)
{
	int retval;

	if ((retval = _fork_new_slurmd()) == 0)
		exit (mgr_spawn_task(req, cli, self));

	return (retval <= 0) ? retval : 0;
}

static int
_check_job_credential(slurm_cred_t cred, uint32_t jobid, 
		      uint32_t stepid, uid_t uid, int tasks_to_launch)
{
	slurm_cred_arg_t arg;
	hostset_t        hset    = NULL;
	bool             user_ok = _slurm_authorized_user(uid); 
	int              host_index = -1;
	int              rc;

	/*
	 * First call slurm_cred_verify() so that all valid
	 * credentials are checked
	 */
	if (((rc = slurm_cred_verify(conf->vctx, cred, &arg)) < 0) && !user_ok)
		return SLURM_ERROR;

	/*
	 * If uid is the slurm user id or root, do not bother
	 * performing validity check of the credential
	 */
	if (user_ok) {
		if (rc >= 0)
			xfree(arg.hostlist);
		return SLURM_SUCCESS;
	}

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

	if (!hostset_within(hset, conf->node_name)) {
		error("job credential invald for this host [%d.%d %ld %s]",
		      arg.jobid, arg.stepid, (long) arg.uid, arg.hostlist);
		goto fail;
	}

        if ((arg.ntask_cnt > 0) && (tasks_to_launch > 0)) {

                host_index = hostset_index(hset, conf->node_name, jobid);

                if(host_index >= 0)
                  debug3(" cons_res %u ntask_cnt %d task[%d] = %d = task_to_launch %d host %s ", 
                         arg.jobid, arg.ntask_cnt, host_index, arg.ntask[host_index], 
                         tasks_to_launch, conf->node_name);

                if (host_index < 0) { 
                        error("job cr credential invalid host_index %d for job %d",
                              host_index, arg.jobid);
                        goto fail; 
                }
                
                if (!(arg.ntask[host_index] == tasks_to_launch)) {
                        error("job cr credential (%d != %d) invalid for this host [%d.%d %ld %s]",
                              arg.ntask[host_index], tasks_to_launch, arg.jobid, arg.stepid, 
                              (long) arg.uid, arg.hostlist);
                        goto fail;
                }
        }

	hostset_destroy(hset);
	xfree(arg.hostlist);
        arg.ntask_cnt = 0;
        if (arg.ntask) xfree(arg.ntask);
        arg.ntask = NULL;

	return SLURM_SUCCESS;

    fail:
	if (hset) hostset_destroy(hset);
	xfree(arg.hostlist);
        arg.ntask_cnt = 0;
        if (arg.ntask) xfree(arg.ntask);
        arg.ntask = NULL;
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
	slurm_addr self;
	socklen_t adlen;

	req_uid = g_slurm_auth_get_uid(msg->cred);

	super_user = _slurm_authorized_user(req_uid);

	if ((super_user == false) && (req_uid != req->uid)) {
		error("launch task request from uid %u",
		      (unsigned int) req_uid);
		errnum = ESLURM_USER_ID_MISSING;	/* or invalid user */
		goto done;
	}

	slurmd_get_addr(cli, &port, host, sizeof(host));
	info("launch task %u.%u request from %u.%u@%s", req->job_id, 
	     req->job_step_id, req->uid, req->gid, host);

#ifndef HAVE_FRONT_END
	if (!slurm_cred_jobid_cached(conf->vctx, req->job_id)) 
		run_prolog = true;
#endif

	if (_check_job_credential(req->cred, jobid, stepid, req_uid,
			req->tasks_to_launch) < 0) {
		errnum = errno;
		error("Invalid job credential from %ld@%s: %m", 
		      (long) req_uid, host);
		goto done;
	}

	/* Make an effort to not overflow shm records */
	if (shm_free_steps() < 2) {
		errnum = ESLURMD_TOOMANYSTEPS;
		error("reject task %u.%u, too many steps", req->job_id,
			req->job_step_id);
		goto done;
	}

	/* xassert(slurm_cred_jobid_cached(conf->vctx, req->job_id));*/

	/* Run job prolog if necessary */
	if (run_prolog && (_run_prolog(req->job_id, req->uid, NULL) != 0)) {
		error("[job %u] prolog failed", req->job_id);
		errnum = ESLURMD_PROLOG_FAILED;
		goto done;
	}

	adlen = sizeof(self);
	_slurm_getsockname(msg->conn_fd, (struct sockaddr *)&self, &adlen);
	if (_launch_tasks(req, cli, &self) < 0)
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
		send_registration_msg(errnum, false);	
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
	slurm_addr self;
	socklen_t adlen;
        int spawn_tasks_to_launch = -1;

	req_uid = g_slurm_auth_get_uid(msg->cred);

	super_user = _slurm_authorized_user(req_uid);

	if ((super_user == false) && (req_uid != req->uid)) {
		error("spawn task request from uid %u",
		      (unsigned int) req_uid);
		errnum = ESLURM_USER_ID_MISSING;	/* or invalid user */
		goto done;
	}

	/* Make an effort to not overflow shm records */
	if (shm_free_steps() < 2) {
		errnum = ESLURMD_TOOMANYSTEPS;
		error("reject task %u.%u, too many steps", req->job_id,
			req->job_step_id);
		goto done;
	}

	slurmd_get_addr(cli, &port, host, sizeof(host));
	info("spawn task %u.%u request from %u@%s", req->job_id, 
	     req->job_step_id, req->uid, host);

#ifndef HAVE_FRONT_END
	if (!slurm_cred_jobid_cached(conf->vctx, req->job_id)) 
		run_prolog = true;
#endif

	if (_check_job_credential(req->cred, jobid, stepid, req_uid, 
			spawn_tasks_to_launch) < 0) {
		errnum = errno;
		error("Invalid job credential from %ld@%s: %m", 
		      (long) req_uid, host);
		goto done;
	}

	/* xassert(slurm_cred_jobid_cached(conf->vctx, req->job_id));*/

	/* Run job prolog if necessary */
	if (run_prolog && (_run_prolog(req->job_id, req->uid, NULL) != 0)) {
		error("[job %u] prolog failed", req->job_id);
		errnum = ESLURMD_PROLOG_FAILED;
		goto done;
	}

	adlen = sizeof(self);
	_slurm_getsockname(msg->conn_fd, (struct sockaddr *)&self, &adlen);
	if (_spawn_task(req, cli, &self) < 0)
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
		send_registration_msg(errnum, false);	
}

static void
_prolog_error(batch_job_launch_msg_t *req, int rc)
{
	char *err_name_ptr, err_name[128], path_name[MAXPATHLEN];
	int fd;

	if (req->err)
		err_name_ptr = req->err;
	else {
		snprintf(err_name, sizeof(err_name), "slurm-%u.err", req->job_id);
		err_name_ptr = err_name;
	}
	if (err_name_ptr[0] == '/')
		snprintf(path_name, MAXPATHLEN, "%s", err_name_ptr);
	else if (req->work_dir)
		snprintf(path_name, MAXPATHLEN, "%s/%s", req->work_dir, err_name_ptr);
	else
		snprintf(path_name, MAXPATHLEN, "/%s", err_name_ptr);

	if ((fd = open(path_name, (O_CREAT|O_APPEND|O_WRONLY), 0644)) == -1) {
		error("Unable to open %s: %s", path_name, slurm_strerror(errno));
		return;
	}
	snprintf(err_name, 128, "Error running slurm prolog: %d\n", WEXITSTATUS(rc));
	write(fd, err_name, strlen(err_name));
	fchown(fd, (uid_t) req->uid, (gid_t) req->gid);
	close(fd);
}

static void
_rpc_batch_job(slurm_msg_t *msg, slurm_addr *cli)
{
	batch_job_launch_msg_t *req = (batch_job_launch_msg_t *)msg->data;
	bool     first_job_run = true;
	int      rc = SLURM_SUCCESS;
	uid_t    req_uid = g_slurm_auth_get_uid(msg->cred);
	char    *bgl_part_id = NULL;
	bool	replied = false;

	if (!_slurm_authorized_user(req_uid)) {
		error("Security violation, batch launch RPC from uid %u",
		      (unsigned int) req_uid);
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
		goto done;
	} 

	/* Make an effort to not overflow shm records */
	if (shm_free_steps() < 2) {
		rc = ESLURMD_TOOMANYSTEPS;
		error("reject job %u, too many steps", req->job_id);
		_prolog_error(req, rc);
		goto done;
	}

	if (req->step_id != NO_VAL && req->step_id != 0)
		first_job_run = false;

	/*
	 * Insert jobid into credential context to denote that
	 * we've now "seen" an instance of the job
	 */
	if (first_job_run) {
		slurm_cred_insert_jobid(conf->vctx, req->job_id);

		/* 
	 	 * Run job prolog on this node
	 	 */
		select_g_get_jobinfo(req->select_jobinfo, SELECT_DATA_PART_ID, 
			&bgl_part_id);

#ifdef HAVE_BGL
		/* BlueGene prolog waits for partition boot and is very slow.
		 * Just reply now and send a separate kill job request if the 
		 * prolog or launch fail. */
		slurm_send_rc_msg(msg, rc);
		replied = true;
#endif

		rc = _run_prolog(req->job_id, req->uid, bgl_part_id);
		xfree(bgl_part_id);
		if (rc != 0) {
			error("[job %u] prolog failed", req->job_id);
			_prolog_error(req, rc);
			rc = ESLURMD_PROLOG_FAILED;
			goto done;
		}
	}

	/* Since job could have been killed while the prolog was 
	 * running (especially on BlueGene, which can wait  minutes
	 * for partition booting). Test if the credential has since
	 * been revoked and exit as needed. */
	if (slurm_cred_revoked(conf->vctx, req->job_id)) {
		info("Job %u already killed, do not launch tasks",  
			req->job_id);
		goto done;
	}
	
	slurm_mutex_lock(&launch_mutex);
	if (req->step_id == NO_VAL)
		info("Launching batch job %u for UID %d",
			req->job_id, req->uid);
	else
		info("Launching batch job %u.%u for UID %d",
			req->job_id, req->step_id, req->uid);
	rc = _launch_batch_job(req, cli);
	slurm_mutex_unlock(&launch_mutex);

    done:
	if (!replied)
		slurm_send_rc_msg(msg, rc);
	else if (rc != 0) {
		/* prolog or job launch failure, 
		 * tell slurmctld that the job failed */
		(void) _abort_job(req->job_id);
	}
}

static int
_abort_job(uint32_t job_id)
{
	complete_job_step_msg_t  resp;
	slurm_msg_t resp_msg;

	resp.job_id       = job_id;
	resp.job_step_id  = NO_VAL;
	resp.job_rc       = 1;
	resp.slurm_rc     = 0;
	resp.node_name    = NULL;	/* unused */
	resp_msg.msg_type = REQUEST_COMPLETE_JOB_STEP;
	resp_msg.data     = &resp;
	return slurm_send_only_controller_msg(&resp_msg);
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
	else {
		if (kill(conf->pid, SIGTERM) != 0)
			error("kill(%u,SIGTERM): %m", conf->pid);
	}

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
		send_registration_msg(SLURM_SUCCESS, false);
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

	if (step->state == SLURMD_JOB_STARTING) {
		debug ("kill req for starting job step %u.%u",
			req->job_id, req->job_step_id); 
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

	if (step->cont_id == 0) {
		debug ("step %u.%u invalid in shm [mpid:%d cont_id:%u]", 
			req->job_id, req->job_step_id, 
			step->mpid, step->cont_id);
		rc = ESLURMD_JOB_NOTRUNNING;
		goto done;
	}

#if 0
	/* This code was used in an investigation of hung TotalView proceses */
	if ((req->signal == SIGKILL)
	    || (req->signal == SIGINT)) { /* for proctrack/linuxproc */
		/*
		 * Assume step termination request.
		 * Send SIGCONT just in case the processes are stopped.
		 */
		slurm_container_signal(step->cont_id, SIGCONT);
		if (slurm_container_signal(step->cont_id, req->signal) < 0)
			rc = errno;
	} else 
#endif
	if (req->signal == 0) {
		if (slurm_container_signal(step->cont_id, req->signal) < 0)
			rc = errno;
/* SIGMIGRATE and SIGSOUND are used to initiate job checkpoint on AIX.
 * These signals are not sent to the entire process group, but just a
 * single process, namely the PMD. */
#ifdef SIGMIGRATE
#ifdef SIGSOUND
	} else if ((req->signal == SIGMIGRATE) || 
		   (req->signal == SIGSOUND)) {
		if (step->task_list
		    && (step->task_list->pid > (pid_t) 0)
		    && (kill(step->task_list->pid, req->signal) < 0))
			rc = errno;
#endif
#endif
	} else {
		if ((step->pgid > (pid_t) 0)
		    &&  (killpg(step->pgid, req->signal) < 0))
			rc = errno;
	} 
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

/* 
 *  For the specified job_id: reply to slurmctld, 
 *   sleep(configured kill_wait), then send SIGKILL 
 *  FIXME! - Perhaps we should send SIGXCPU first?
 */
static void
_rpc_timelimit(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	uid_t           uid = g_slurm_auth_get_uid(msg->cred);
	kill_job_msg_t *req = msg->data;
	int             nsteps;

	debug2("Processing RPC: REQUEST_KILL_TIMELIMIT");
	if (!_slurm_authorized_user(uid)) {
		error ("Security violation: rpc_timelimit req from uid %ld", 
		       (long) uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	}

	/*
	 *  Indicate to slurmctld that we've received the message
	 */
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
	slurm_close_accepted_conn(msg->conn_fd);
	msg->conn_fd = -1;

	nsteps = _kill_all_active_steps(req->job_id, SIGTERM, false);
	verbose( "Job %u: timeout: sent SIGTERM to %d active steps", 
	         req->job_id, nsteps );

	/* Revoke credential, send SIGKILL, run epilog, etc. */
	_rpc_kill_job(msg, cli_addr); 
}

static void  _rpc_pid2jid(slurm_msg_t *msg, slurm_addr *cli)
{
	job_id_request_msg_t *req = (job_id_request_msg_t *) msg->data;
	slurm_msg_t           resp_msg;
	job_id_response_msg_t resp;
	bool         found = false; 
	uint32_t     my_cont = slurm_container_find(req->job_pid);
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL;

	if (my_cont == 0) {
		debug("slurm_container_find(%u): process not found",
		      (uint32_t) req->job_pid);
		/*
		 * Check if the job_pid matches the pid of a job step slurmd.
		 * LCRM gets confused if a session leader process
		 * (the job step slurmd) is not labelled as a process in the
		 * job step.
		 */
		while ((s = list_next(i))) {
			if (s->mpid == req->job_pid) {
				resp.job_id = s->jobid;
				found = true;
				break;
			}
		}
	} else {
		while ((s = list_next(i))) {
			if (s->cont_id == my_cont) {
				resp.job_id = s->jobid;
				found = true;
				break;
			}
		}
	}
	list_iterator_destroy(i);
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
	slurmd_job_state_t *state;
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
	resp.gtids      = xmalloc(step->ntasks * sizeof(*resp.local_pids));
	resp.ntasks     = step->ntasks;
	for (t = step->task_list, i = 0; t; t = t->next, i++) {
		resp.gtids[t->id] = t->global_id;
		resp.local_pids[t->id] = t->pid;
	}
	resp.executable_name  = xstrdup(step->exec_name);

	shm_free_step(step);

    done:
	debug2("update step addrs rc = %d", rc);
	resp_msg.data         = &resp;
	resp_msg.msg_type     = RESPONSE_REATTACH_TASKS;
	resp.node_name        = conf->node_name;
	resp.srun_node_id     = req->srun_node_id;
	resp.return_code      = rc;

	slurm_send_only_node_msg(&resp_msg);

	xfree(resp.gtids);
	xfree(resp.local_pids);

}

/*
 * _kill_all_active_steps - signals all steps of a job
 * jobid IN - id of job to signal
 * sig   IN - signal to send
 * batch IN - if true signal batch script, otherwise skip it
 * RET count of signaled job steps (plus batch script, if applicable)
 */
static int
_kill_all_active_steps(uint32_t jobid, int sig, bool batch)
{
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL; 
	int step_cnt       = 0;  

	while ((s = list_next(i))) {
		if (s->jobid != jobid) {
			/* multiple jobs expected on shared nodes */
			debug3("Step from other job: s->jobid=%d, jobid=%d",
			      s->jobid, jobid);
			continue;
		}

		if (s->cont_id == 0) {
			debug ("bad cont_id value in shm for %d!", jobid);
			continue;
		}

		if ((s->stepid == NO_VAL) && (!batch))
			continue;

		step_cnt++;

		debug2("signal %d to job %u (cont_id:%u)",
		       sig, jobid, s->cont_id);
		if (slurm_container_signal(s->cont_id, sig) < 0)
			error("kill jid %d cont_id %u: %m",
			      s->jobid, s->cont_id);
	}
	list_iterator_destroy(i);
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
	list_iterator_destroy(i);
	list_destroy(steps);

	return retval;
}

/*
 * Wait until all job steps are in SLURMD_JOB_COMPLETE state.
 * This indicates that interconnect_postfini has completed and 
 * freed the switch windows (as needed only for Federation switch).
 */
static void
_wait_state_completed(uint32_t jobid, int max_delay)
{
	char *switch_type = slurm_get_switch_type();
	int i;

	if (strcmp(switch_type, "switch/federation")) {
		xfree(switch_type);
		return;
	}
	xfree(switch_type);

	for (i=0; i<max_delay; i++) {
		if (_steps_completed_now(jobid))
			break;
		sleep(1);
	}
	if (i >= max_delay)
		error("timed out waiting for job %u to complete", jobid);
}

static bool
_steps_completed_now(uint32_t jobid)
{
	List   steps = shm_get_steps();
	ListIterator i = list_iterator_create(steps);
	job_step_t *s = NULL;
	bool rc = true;

	while ((s = list_next(i))) {
		if (s->jobid != jobid)
			continue;
		if (s->state != SLURMD_JOB_COMPLETE) {
			rc = false;
			break;
		}
	}
	list_iterator_destroy(i);
	list_destroy(steps);

	return rc;
}

/*
 *  Send epilog complete message to currently active comtroller.
 *   Returns SLURM_SUCCESS if message sent successfully,
 *           SLURM_FAILURE if epilog complete message fails to be sent.
 */
static int
_epilog_complete(uint32_t jobid, int rc)
{
	int                    ret = SLURM_SUCCESS;
	slurm_msg_t            msg;
	epilog_complete_msg_t  req;

	_wait_state_completed(jobid, 5);

	req.job_id      = jobid;
	req.return_code = rc;
	req.node_name   = conf->node_name;
	if (switch_g_alloc_node_info(&req.switch_nodeinfo))
		error("switch_g_alloc_node_info: %m");
	if (switch_g_build_node_info(req.switch_nodeinfo))
		error("switch_g_build_node_info: %m");

	msg.msg_type    = MESSAGE_EPILOG_COMPLETE;
	msg.data        = &req;

	if (slurm_send_only_controller_msg(&msg) < 0) {
		error("Unable to send epilog complete message: %m");
		ret = SLURM_ERROR;
	} else
		debug ("Job %u: sent epilog complete msg: rc = %d", jobid, rc);

	switch_g_free_node_info(&req.switch_nodeinfo);
	return ret;
}

static void 
_rpc_kill_job(slurm_msg_t *msg, slurm_addr *cli)
{
	int             rc     = SLURM_SUCCESS;
	kill_job_msg_t *req    = msg->data;
	uid_t           uid    = g_slurm_auth_get_uid(msg->cred);
	int             nsteps = 0;
	int		delay;
	char           *bgl_part_id = NULL;

	debug2("Processing RPC: REQUEST_KILL_JOB");
	/* 
	 * check that requesting user ID is the SLURM UID
	 */
	if (!_slurm_authorized_user(uid)) {
		error("Security violation: kill_job(%ld) from uid %ld",
		      req->job_id, (long) uid);
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);
		return;
	} 

	/*
	 *  Initialize a "waiter" thread for this jobid. If another
	 *   thread is already waiting on termination of this job, 
	 *   _waiter_init() will return SLURM_ERROR. In this case, just 
	 *   notify slurmctld that we recvd the message successfully,
	 *   then exit this thread.
	 */
	if (_waiter_init(req->job_id) == SLURM_ERROR) {
		if (msg->conn_fd >= 0)
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

	/*
	 * Tasks might be stopped (possibly by a debugger)
	 * so send SIGCONT first.
	 */
	_kill_all_active_steps(req->job_id, SIGCONT, true);

	nsteps = _kill_all_active_steps(req->job_id, SIGTERM, true);

	/*
	 *  If there are currently no active job steps and no
	 *    configured epilog to run, bypass asynchronous reply and
	 *    notify slurmctld that we have already completed this
	 *    request. We need to send current switch state on AIX
	 *    systems, so this bypass can not be used.
	 */
#ifndef HAVE_AIX
	if ((nsteps == 0) && !conf->epilog) {
		if (msg->conn_fd >= 0)
			slurm_send_rc_msg(msg, 
				ESLURMD_KILL_JOB_ALREADY_COMPLETE);
		slurm_cred_begin_expiration(conf->vctx, req->job_id);
		_waiter_complete(req->job_id);
		return;
	}
#endif

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
	delay = MAX(conf->cf.kill_wait, 5);
	if ( !_pause_for_job_completion (req->job_id, delay)
	   && _kill_all_active_steps(req->job_id, SIGKILL, true) ) {
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
	select_g_get_jobinfo(req->select_jobinfo, SELECT_DATA_PART_ID,
		&bgl_part_id);
	rc = _run_epilog(req->job_id, req->job_uid, bgl_part_id);
	xfree(bgl_part_id);
	if (rc != 0) {
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
_run_prolog(uint32_t jobid, uid_t uid, char *bgl_part_id)
{
	int error_code;
	char *my_prolog;

	slurm_mutex_lock(&conf->config_mutex);
	my_prolog = xstrdup(conf->prolog);
	slurm_mutex_unlock(&conf->config_mutex);

	error_code = run_script(true, my_prolog, jobid, uid, bgl_part_id);
	xfree(my_prolog);

	return error_code;
}

static int 
_run_epilog(uint32_t jobid, uid_t uid, char *bgl_part_id)
{
	int error_code;
	char *my_epilog;

	slurm_mutex_lock(&conf->config_mutex);
	my_epilog = xstrdup(conf->epilog);
	slurm_mutex_unlock(&conf->config_mutex);

	error_code = run_script(false, my_epilog, jobid, uid, bgl_part_id);
	xfree(my_epilog);

	return error_code;
}
