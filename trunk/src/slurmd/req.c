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
#include <signal.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "src/common/credential_utils.h"
#include "src/common/log.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"

#include "src/slurmd/slurmd.h"
#include "src/slurmd/shm.h"
#include "src/slurmd/mgr.h"

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif

static void _rpc_launch_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_batch_job(slurm_msg_t *, slurm_addr *);
static void _rpc_kill_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_reattach_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_revoke_credential(slurm_msg_t *, slurm_addr *);
static void _rpc_update_time(slurm_msg_t *, slurm_addr *);
static void _rpc_shutdown(slurm_msg_t *msg, slurm_addr *cli_addr);
static void _rpc_pid2jid(slurm_msg_t *msg, slurm_addr *);
static int  _rpc_ping(slurm_msg_t *, slurm_addr *);
static int  _launch_tasks(launch_tasks_request_msg_t *, slurm_addr *);
static int  _run_prolog(uint32_t jobid, uid_t uid);
static int  _run_epilog(uint32_t jobid, uid_t uid);
static void _insert_fake_cred(uint32_t jobid, time_t timelimit);

void
slurmd_req(slurm_msg_t *msg, slurm_addr *cli)
{
	switch(msg->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		_rpc_batch_job(msg, cli);
		slurm_free_job_launch_msg(msg->data);
		break;
	case REQUEST_LAUNCH_TASKS:
		_rpc_launch_tasks(msg, cli);
		slurm_free_launch_tasks_request_msg(msg->data);
		break;
	case REQUEST_KILL_TASKS:
		_rpc_kill_tasks(msg, cli);
		slurm_free_kill_tasks_msg(msg->data);
		break;
	case REQUEST_REATTACH_TASKS:
		_rpc_reattach_tasks(msg, cli);
		slurm_free_reattach_tasks_request_msg(msg->data);
		break;
	case REQUEST_REVOKE_JOB_CREDENTIAL:
		_rpc_revoke_credential(msg, cli);
		slurm_free_revoke_credential_msg(msg->data);
		break;
	case REQUEST_UPDATE_JOB_TIME:
		_rpc_update_time(msg, cli);
		slurm_free_update_job_time_msg(msg->data);
		break;
	case REQUEST_SHUTDOWN:
	case REQUEST_SHUTDOWN_IMMEDIATE:
		_rpc_shutdown(msg, cli);
		slurm_free_shutdown_msg(msg->data);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		/* Treat as ping (for slurmctld agent) */
		if (_rpc_ping(msg, cli) == SLURM_SUCCESS) {
			/* Then initiate a separate node registration */
			slurm_free_node_registration_status_msg(msg->data);
			send_registration_msg();
		}
		break;
	case REQUEST_PING:
		_rpc_ping(msg, cli);
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
	slurm_free_msg(msg);
	return;
}

static int
_launch_batch_job(batch_job_launch_msg_t *req, slurm_addr *cli)
{	
	pid_t pid;
	int rc;

	
	switch ((pid = fork())) {
		case -1:
			error("launch_tasks: fork: %m");
			return SLURM_ERROR;
			break;
		case 0: /* child runs job */
			slurm_shutdown_msg_engine(conf->lfd);
			list_destroy(conf->threads);
			destroy_credential_state_list(conf->cred_state_list);
			slurm_destroy_ssl_key_ctx(&conf->vctx);
			slurm_ssl_destroy();
			rc = mgr_launch_batch_job(req, cli);
			exit(rc);
			/* NOTREACHED */
			break;
		default:
			debug("created process %ld for job %d",
					pid, req->job_id);
			break;
	}

	return SLURM_SUCCESS;

}

static int
_launch_tasks(launch_tasks_request_msg_t *req, slurm_addr *cli)
{
	pid_t pid;
	int rc;

	switch ((pid = fork())) {
		case -1:
			error("launch_tasks: fork: %m");
			return SLURM_ERROR;
			break;
		case 0: /* child runs job */
			slurm_shutdown_msg_engine(conf->lfd);
			list_destroy(conf->threads);
			destroy_credential_state_list(conf->cred_state_list);
			slurm_destroy_ssl_key_ctx(&conf->vctx);
			slurm_ssl_destroy();
			rc = mgr_launch_tasks(req, cli);
			exit(rc);
			/* NOTREACHED */
			break;
		default:
			debug("created process %ld for job %d.%d",
					pid, req->job_id, req->job_step_id);
			break;
	}

	return SLURM_SUCCESS;
}
				                                            

static void 
_rpc_launch_tasks(slurm_msg_t *msg, slurm_addr *cli)
{
	int      rc;
	uint16_t port;
	char     host[MAXHOSTNAMELEN];
	uid_t    req_uid;
	bool     super_user = false, run_prolog = false;
	launch_tasks_request_msg_t *req = msg->data;

	req_uid = slurm_auth_uid(msg->cred);
	if ((req_uid == conf->slurm_user_id) || (req_uid == 0))
		super_user = true;
	if ((super_user == false) && (req_uid != req->uid)) {
		error("Security violation, launch task RCP from uid %u",
		      (unsigned int) req_uid);
		rc = ESLURM_USER_ID_MISSING;	/* or invalid user */
		slurm_send_rc_msg(msg, rc);
		return;
	}

	slurm_get_addr(cli, &port, host, sizeof(host));
	info("launch task %u.%u request from %ld@%s", req->job_id, 
	     req->job_step_id, req->uid, host);

	if (!credential_is_cached(conf->cred_state_list, req->job_id)) 
		run_prolog = true;

	rc = verify_credential(&conf->vctx, 
			       req->credential, 
			       conf->cred_state_list);

	if ((rc != SLURM_SUCCESS) && (super_user == false)) {
		error("Invalid credential from %ld@%s", req_uid, host);
		slurm_send_rc_msg(msg, rc);
		return;
	}

	/* Run job prolog if necessary */
	if (run_prolog && (_run_prolog(req->job_id, req->uid) != 0)) {
		error("[job %d] prolog failed", req->job_id);
		slurm_send_rc_msg(msg, ESLURMD_PROLOG_FAILED);
		return;
	}

	rc = _launch_tasks(req, cli);

	slurm_send_rc_msg(msg, rc);
}


static void
_rpc_batch_job(slurm_msg_t *msg, slurm_addr *cli)
{
	batch_job_launch_msg_t *req = (batch_job_launch_msg_t *)msg->data;
	int      rc = SLURM_SUCCESS;
	uid_t    req_uid = slurm_auth_uid(msg->cred);

	if ((req_uid != conf->slurm_user_id) && (req_uid != 0)) {
		error("Security violation, batch launch RPC from uid %u",
		      (unsigned int) req_uid);
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	} else {

		/* 
		 * Run job prolog on this node
		 */
		if (_run_prolog(req->job_id, req->uid) != 0) {
			error("[job %d] prolog failed", req->job_id);
			rc = ESLURMD_PROLOG_FAILED;
		} else {
			_insert_fake_cred(req->job_id, (time_t) -1);

			info("Launching batch job %u for UID %d",
					req->job_id, req->uid);

			if (_launch_batch_job(req, cli) < 0)
				rc = SLURM_FAILURE;
		}
	}

	slurm_send_rc_msg(msg, rc);
}

static void
_rpc_shutdown(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	uid_t req_uid = slurm_auth_uid(msg->cred);

	if ((req_uid != conf->slurm_user_id) && (req_uid != 0)) {
		error("Security violation, shutdown RPC from uid %u",
		      (unsigned int) req_uid);
		slurm_send_rc_msg(msg, ESLURM_USER_ID_MISSING);	/* uid bad */
	} else
		kill(conf->pid, SIGTERM);
}

static int
_rpc_ping(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	int               rc = SLURM_SUCCESS;
	uid_t req_uid = slurm_auth_uid(msg->cred);

	if ((req_uid != conf->slurm_user_id) && (req_uid != 0)) {
		error("Security violation, ping RPC from uid %u",
		      (unsigned int) req_uid);
		rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	}

	/* return result */
	slurm_send_rc_msg(msg, rc);
	return rc;
}

static void
_rpc_kill_tasks(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	int               rc = SLURM_SUCCESS;
	uid_t             req_uid;
	job_step_t       *step;
	kill_tasks_msg_t *req = (kill_tasks_msg_t *) msg->data;

	if (!(step = shm_get_step(req->job_id, req->job_step_id))) {
		debug("kill for nonexistent job %d.%d requested",
				req->job_id, req->job_step_id);
		rc = ESLURM_INVALID_JOB_ID;
		goto done;
	} 

	req_uid = slurm_auth_uid(msg->cred);
	if ((req_uid != step->uid) && (req_uid != 0)) {
	       debug("kill req from uid %ld for job %d.%d owned by uid %ld",
		     req_uid, req->job_id, req->job_step_id, step->uid);       
	       rc = ESLURM_USER_ID_MISSING;	/* or bad in this case */
	       goto done;
	}

	verbose("Successful request to send signal %d to %d", 
		req->signal, req->job_id, req->job_step_id);

	if (killpg(step->sid, req->signal) < 0)
		rc = errno;
	shm_free_step(step);


	/* rc = shm_signal_step(req->job_id, req->job_step_id, req->signal); */

  done:
	slurm_send_rc_msg(msg, rc);
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
		info("_rpc_pid2jid: pid(%u) not found", req->job_pid);
		slurm_send_rc_msg(msg, ESLURM_INVALID_JOB_ID);
	}
}


static void 
_rpc_reattach_tasks(slurm_msg_t *msg, slurm_addr *cli)
{
	int         rc   = SLURM_SUCCESS;
	uint16_t    port = 0;
	char        host[MAXHOSTNAMELEN];
	int         i;
	job_step_t *step;
	task_t     *t;
	uid_t       req_uid;
	gid_t       req_gid;
	slurm_addr  ioaddr;
	slurm_msg_t                    resp_msg;
	reattach_tasks_request_msg_t  *req = msg->data;
	reattach_tasks_response_msg_t  resp;

	slurm_get_addr(cli, &port, host, sizeof(host));
	req_uid = slurm_auth_uid(msg->cred);
	req_gid = slurm_auth_gid(msg->cred);

	info("reattach request from %ld@%s for %d.%d", 
	     req_uid, host, req->job_id, req->job_step_id);

	/* 
	 * Set response addr by resp_port and client address
	 */
	memcpy(&resp_msg.address, cli, sizeof(slurm_addr));
	slurm_set_addr(&resp_msg.address, req->resp_port, NULL); 

	if ((step = shm_get_step(req->job_id, req->job_step_id)) < 0) {
		rc = ESRCH;
		goto done;
	}
	
	if ((step->uid != req_uid) && (req_uid != 0)) {
		error("uid %ld attempt to attach to job %d.%d owned by %ld",
				(long) req_uid, req->job_id, req->job_step_id,
				(long) step->uid);
		rc = EPERM;
		goto done;
	}

	/* 
	 * Set IO and response addresses in shared memory
	 */
	memcpy(&ioaddr, cli, sizeof(slurm_addr));
	slurm_set_addr(&ioaddr, req->io_port, NULL);
	slurm_get_addr(&ioaddr, &port, host, sizeof(host));

	debug3("reattach: srun ioaddr: %s:%d", host, port);

	do {
		rc = shm_update_step_addrs( req->job_id, req->job_step_id,
				            &ioaddr, &resp_msg.address,
				            req->key ); 
	} while ((rc < 0) && (errno == EAGAIN));

	resp.local_pids = xmalloc(step->ntasks * sizeof(*resp.local_pids));
	resp.gids       = xmalloc(step->ntasks * sizeof(*resp.local_pids));
	resp.ntasks     = step->ntasks;
	for (t = step->task_list, i = 0; t; t = t->next, i++) {
		resp.gids[t->id] = t->global_id;
		resp.local_pids[t->id] = t->pid;
	}

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


static void
_kill_all_active_steps(uint32_t jobid, int sig)
{
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL;   

	while ((s = list_next(i))) {
		if (s->jobid == jobid) {
			/* Kill entire process group 
			 * (slurmd manager will clean up any stragglers)
			 */
			debug2("sending signal %d to jobid %d (pg:%d)", 
			       sig, jobid, s->sid);
			shm_signal_step(jobid, s->stepid, sig); 
		}
	}
	list_destroy(steps);
}

static void 
_rpc_revoke_credential(slurm_msg_t *msg, slurm_addr *cli)
{
	int   rc      = SLURM_SUCCESS;
	uid_t req_uid = slurm_auth_uid(msg->cred);
	revoke_credential_msg_t *req = (revoke_credential_msg_t *) msg->data;

	if ((req_uid != conf->slurm_user_id) && (req_uid != 0)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, uid %u can't revoke credentials",
		      (unsigned int) req_uid);
	} else {
		rc = revoke_credential(req, conf->cred_state_list);

		/*
		 * Now kill all steps associated with this job, they are
		 * no longer allowed to be running
		 */
		_kill_all_active_steps(req->job_id, SIGKILL);

		if (rc < 0)
			error("revoking credential for job %d: %m", 
			      req->job_id);
		else
			debug("credential for job %d revoked", req->job_id);

		if (_run_epilog(req->job_id, req->job_uid) != 0) {
			error ("[job %d] epilog failed", req->job_id);
			rc = ESLURMD_EPILOG_FAILED;
		}
	}

	slurm_send_rc_msg(msg, rc);
}

static void 
_rpc_update_time(slurm_msg_t *msg, slurm_addr *cli)
{
	int   rc      = SLURM_SUCCESS;
	uid_t req_uid = slurm_auth_uid(msg->cred);
	job_time_msg_t *req = (job_time_msg_t *) msg->data;

	if ((req_uid != conf->slurm_user_id) && (req_uid != 0)) {
		rc = ESLURM_USER_ID_MISSING;
		error("Security violation, uid %u can't update time limit",
		      (unsigned int) req_uid);
	} else {
		rc = shm_update_job_timelimit(req->job_id, 
					      req->expiration_time);
		if (rc < 0) {
			error("updating lifetime for job %d: %m", 
			      req->job_id);
			rc = ESLURM_INVALID_JOB_ID;
		} else
			debug("reset job %d lifetime", req->job_id);
	}

	slurm_send_rc_msg(msg, rc);
}

static int 
_run_prolog(uint32_t jobid, uid_t uid)
{
	return run_script(true, conf->prolog, jobid, uid);
}

static int 
_run_epilog(uint32_t jobid, uid_t uid)
{
	return run_script(false, conf->epilog, jobid, uid);
}

/*
 * XXX: Move some of this functionality into credential_utils
 *
 */
static void
_insert_fake_cred(uint32_t jobid, time_t timelimit)
{
	credential_state_t *s = xmalloc(sizeof(*s));

	debug2("inserting fake cred for job %d", jobid);

	s->job_id          = jobid;
	s->revoked         = false;
	s->procs_allocated = 0;
	s->total_procs     = 0;
	s->expiration      = timelimit;
	list_append(conf->cred_state_list, (void *) s);
}
