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
#  include <config.h>
#endif

#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/param.h>

#include <src/common/slurm_protocol_api.h>
#include <src/common/credential_utils.h>
#include <src/common/slurm_auth.h>
#include <src/common/log.h>
#include <src/common/xmalloc.h>

#include <src/slurmd/slurmd.h>
#include <src/slurmd/shm.h>
#include <src/slurmd/mgr.h>

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	64
#endif

static void _rpc_launch_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_batch_job(slurm_msg_t *, slurm_addr *);
static void _rpc_kill_tasks(slurm_msg_t *, slurm_addr *);
static void _rpc_revoke_credential(slurm_msg_t *, slurm_addr *);
static void _rpc_ping(slurm_msg_t *, slurm_addr *);
static int  _launch_tasks(launch_tasks_request_msg_t *, slurm_addr *);

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
	case REQUEST_REVOKE_JOB_CREDENTIAL:
		_rpc_revoke_credential(msg, cli);
		slurm_free_revoke_credential_msg(msg->data);
		break;
	case REQUEST_SHUTDOWN:
	case REQUEST_SHUTDOWN_IMMEDIATE:
		kill(conf->pid, SIGTERM);
		slurm_free_shutdown_msg(msg->data);
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		/* Treat as ping (for slurmctld agent) */
		_rpc_ping(msg, cli);
		/* Then initiate a separate node registration */
		slurm_free_node_registration_status_msg(msg->data);
		send_registration_msg();
		break;
	case REQUEST_PING:
		_rpc_ping(msg, cli);
		/* XXX: Is there a slurm_free_blahblah* for this one? */
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
			log_reinit();
			rc = mgr_launch_batch_job(req, cli);
			exit(rc);
			/* NOTREACHED */
			break;
		default:
			verbose("created process %ld for job %d",
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
			log_reinit();
			rc = mgr_launch_tasks(req, cli);
			exit(rc);
			/* NOTREACHED */
			break;
		default:
			verbose("created process %ld for job %d.%d",
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
	gid_t    req_gid;
	slurm_msg_t resp_msg;
	launch_tasks_response_msg_t resp;
	launch_tasks_request_msg_t *req = msg->data;

	slurm_get_addr(cli, &port, host, sizeof(host));
	req_uid = slurm_auth_uid(msg->cred);
	req_gid = slurm_auth_gid(msg->cred);

	verbose("launch tasks request from %ld@%s", req_uid, host, port);

	rc = verify_credential(&conf->vctx, 
			       req->credential, 
			       conf->cred_state_list);

	if ((rc == SLURM_SUCCESS) && (req_uid == req->uid))
		rc = _launch_tasks(req, cli);
	else {
		verbose("Invalid credential from %ld@%s, launching job anyway", 
		        req_uid, host);
		rc = _launch_tasks(req, cli);
	}

	memcpy(&resp_msg.address, cli, sizeof(slurm_addr));
	slurm_set_addr(&resp_msg.address, req->resp_port, NULL); 

	resp_msg.data         = &resp;
	resp_msg.msg_type     = RESPONSE_LAUNCH_TASKS;

	resp.node_name        = conf->hostname;
	resp.srun_node_id     = req->srun_node_id;
	resp.return_code      = rc;

	slurm_send_only_node_msg(&resp_msg);
}

static void
_rpc_batch_job(slurm_msg_t *msg, slurm_addr *cli)
{
	batch_job_launch_msg_t *req = (batch_job_launch_msg_t *)msg->data;
	int      rc = SLURM_SUCCESS;
	uint16_t port;
	char     host[MAXHOSTNAMELEN];
	uid_t    req_uid;
	gid_t    req_gid;

	slurm_get_addr(cli, &port, host, sizeof(host));
	req_uid = slurm_auth_uid(msg->cred);
	req_gid = slurm_auth_gid(msg->cred);

	verbose("req_uid = %ld, req->uid = %ld", req_uid, req->uid);

	if ((req_uid != 0) && (req_uid != (uid_t)req->uid)) {
		rc = EPERM;
		goto done;
	}

	verbose("batch launch request from %ld@%s", req_uid, host, port);

	if (_launch_batch_job(req, cli) < 0)
		rc = SLURM_FAILURE;

  done:
	slurm_send_rc_msg(msg, rc);
}

static void
_rpc_ping(slurm_msg_t *msg, slurm_addr *cli_addr)
{
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
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
		rc = EEXIST;
		goto done;
	} 

	req_uid = slurm_auth_uid(msg->cred);
	if ((req_uid != step->uid) && (req_uid != 0)) {
	       debug("kill req from uid %ld for job %d.%d owned by uid %ld",
		     req_uid, step->jobid, step->stepid, step->uid);	       
	       rc = EPERM;
	       goto done;
	}

	shm_free_step(step);

	rc = shm_signal_step(req->job_id, req->job_step_id, req->signal);

  done:
	slurm_send_rc_msg(msg, SLURM_SUCCESS);
}

static void
_kill_all_active_steps(uint32_t jobid)
{
	List         steps = shm_get_steps();
	ListIterator i     = list_iterator_create(steps);
	job_step_t  *s     = NULL;   

	while ((s = list_next(i))) {
		if (s->jobid == jobid) {
			shm_signal_step(jobid, s->stepid, SIGKILL);
			shm_delete_step(jobid, s->stepid);
		}
	}
	list_iterator_destroy(i);
	list_destroy(steps);
}

static void 
_rpc_revoke_credential(slurm_msg_t *msg, slurm_addr *cli)
{
	int   rc      = SLURM_SUCCESS;
	uid_t req_uid = slurm_auth_uid(msg->cred);
	revoke_credential_msg_t *req = (revoke_credential_msg_t *) msg->data;

	/* XXX Need to check uid for authorization to revoke
	 * credential. 
	 */
	
	rc = revoke_credential(req, conf->cred_state_list);

	/*
	 * Now kill all steps associated with this job, they are
	 * no longer allowed to be running
	 */
	_kill_all_active_steps(req->job_id);

	slurm_send_rc_msg(msg, rc);

	if (rc < 0)
		error("revoking credential for job %d: %m", req->job_id);
	else
		debug("credential for job %d revoked", req->job_id);
}
