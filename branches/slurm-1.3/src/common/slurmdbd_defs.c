/****************************************************************************\
 *  slurmdbd_defs.c - functions for use with Slurm DBD RPCs
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H 
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#else				/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif				/*  HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/fd.h"
#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/assoc_mgr.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#define DBD_MAGIC		0xDEAD3219
#define MAX_AGENT_QUEUE		10000
#define MAX_DBD_MSG_LEN		16384
#define SLURMDBD_TIMEOUT	300	/* Seconds SlurmDBD for response */

uint16_t running_cache = 0;
pthread_mutex_t assoc_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t assoc_cache_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t agent_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  agent_cond = PTHREAD_COND_INITIALIZER;
static List      agent_list     = (List) NULL;
static pthread_t agent_tid      = 0;
static time_t    agent_shutdown = 0;

static pthread_mutex_t slurmdbd_lock = PTHREAD_MUTEX_INITIALIZER;
static slurm_fd  slurmdbd_fd         = -1;
static char *    slurmdbd_auth_info  = NULL;
static bool      rollback_started    = 0;

static void * _agent(void *x);
static void   _agent_queue_del(void *x);
static void   _close_slurmdbd_fd(void);
static void   _create_agent(void);
static bool   _fd_readable(slurm_fd fd, int read_timeout);
static int    _fd_writeable(slurm_fd fd);
static int    _get_return_code(uint16_t rpc_version, int read_timeout);
static Buf    _load_dbd_rec(int fd);
static void   _load_dbd_state(void);
static void   _open_slurmdbd_fd(void);
static int    _purge_job_start_req(void);
static Buf    _recv_msg(int read_timeout);
static void   _reopen_slurmdbd_fd(void);
static int    _save_dbd_rec(int fd, Buf buffer);
static void   _save_dbd_state(void);
static int    _send_init_msg(void);
static int    _send_fini_msg(void);
static int    _send_msg(Buf buffer);
static void   _sig_handler(int signal);
static void   _shutdown_agent(void);
static void   _slurmdbd_packstr(void *str, uint16_t rpc_version, Buf buffer);
static int    _slurmdbd_unpackstr(void **str, uint16_t rpc_version, Buf buffer);
static int    _tot_wait (struct timeval *start_time);

/****************************************************************************
 * Socket open/close/read/write functions
 ****************************************************************************/

/* Open a socket connection to SlurmDbd
 * auth_info IN - alternate authentication key
 * make_agent IN - make agent to process RPCs if set
 * rollback IN - keep journal and permit rollback if set
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_open_slurmdbd_conn(char *auth_info, bool make_agent, 
				    bool rollback)
{
	/* we need to set this up before we make the agent or we will
	   get a threading issue.
	*/
	slurm_mutex_lock(&slurmdbd_lock);
	xfree(slurmdbd_auth_info);
	if (auth_info)
		slurmdbd_auth_info = xstrdup(auth_info);

	rollback_started = rollback;

	if (slurmdbd_fd < 0)
		_open_slurmdbd_fd();
	slurm_mutex_unlock(&slurmdbd_lock);

	slurm_mutex_lock(&agent_lock);
	if (make_agent && ((agent_tid == 0) || (agent_list == NULL)))
		_create_agent();
	slurm_mutex_unlock(&agent_lock);

	if (slurmdbd_fd < 0)
		return SLURM_ERROR;
	else
		return SLURM_SUCCESS;
}

/* Close the SlurmDBD socket connection */
extern int slurm_close_slurmdbd_conn(void)
{
	/* NOTE: agent_lock not needed for _shutdown_agent() */
	_shutdown_agent();

	if (rollback_started) {
		if (_send_fini_msg() != SLURM_SUCCESS)
			error("slurmdbd: Sending fini msg: %m");
		else
			debug("slurmdbd: Sent fini msg");
	}

	slurm_mutex_lock(&slurmdbd_lock);
	_close_slurmdbd_fd();
	xfree(slurmdbd_auth_info);
	slurm_mutex_unlock(&slurmdbd_lock);

	return SLURM_SUCCESS;
}

/* Send an RPC to the SlurmDBD and wait for the return code reply.
 * The RPC will not be queued if an error occurs.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_slurmdbd_recv_rc_msg(uint16_t rpc_version, 
					   slurmdbd_msg_t *req, int *resp_code)
{
	int rc;
	slurmdbd_msg_t *resp;

	xassert(req);
	xassert(resp_code);

	resp = xmalloc(sizeof(slurmdbd_msg_t));
	rc = slurm_send_recv_slurmdbd_msg(rpc_version, req, resp);
	if (rc != SLURM_SUCCESS) {
		;	/* error message already sent */
	} else if (resp->msg_type != DBD_RC) {
		error("slurmdbd: response is type DBD_RC: %d", resp->msg_type);
		rc = SLURM_ERROR;
	} else {	/* resp->msg_type == DBD_RC */
		dbd_rc_msg_t *msg = resp->data;
		*resp_code = msg->return_code;
		if(msg->return_code != SLURM_SUCCESS)
			error("slurmdbd(%d): from %u: %s", msg->return_code, 
			      msg->sent_type, msg->comment);
		slurmdbd_free_rc_msg(rpc_version, msg);
	}
	xfree(resp);

	return rc;
}

/* Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_recv_slurmdbd_msg(uint16_t rpc_version, 
					slurmdbd_msg_t *req, 
					slurmdbd_msg_t *resp)
{
	int rc = SLURM_SUCCESS, read_timeout;
	Buf buffer;

	xassert(req);
	xassert(resp);

	read_timeout = SLURMDBD_TIMEOUT * 1000;
	slurm_mutex_lock(&slurmdbd_lock);
	if (slurmdbd_fd < 0) {
		/* Either slurm_open_slurmdbd_conn() was not executed or
		 * the connection to Slurm DBD has been closed */
		_open_slurmdbd_fd();
		if (slurmdbd_fd < 0) {
			slurm_mutex_unlock(&slurmdbd_lock);
			return SLURM_ERROR;
		}
	}

	buffer = pack_slurmdbd_msg(rpc_version, req);

	rc = _send_msg(buffer);
	free_buf(buffer);
	if (rc != SLURM_SUCCESS) {
		error("slurmdbd: Sending message type %u", req->msg_type);
		slurm_mutex_unlock(&slurmdbd_lock);
		return SLURM_ERROR;
	}

	buffer = _recv_msg(read_timeout);
	if (buffer == NULL) {
		error("slurmdbd: Getting response to message type %u", 
		      req->msg_type);
		slurm_mutex_unlock(&slurmdbd_lock);
		return SLURM_ERROR;
	}
		
	rc = unpack_slurmdbd_msg(rpc_version, resp, buffer);

	/* check for the rc of the start job message */
	if (rc == SLURM_SUCCESS && resp->msg_type == DBD_JOB_START_RC) 
		rc = ((dbd_job_start_rc_msg_t *)resp->data)->return_code;
	
	free_buf(buffer);
	slurm_mutex_unlock(&slurmdbd_lock);
	
	return rc;
}

/* Send an RPC to the SlurmDBD. Do not wait for the reply. The RPC
 * will be queued and processed later if the SlurmDBD is not responding.
 * NOTE: slurm_open_slurmdbd_conn() must have been called with make_agent set
 * 
 * Returns SLURM_SUCCESS or an error code */
extern int slurm_send_slurmdbd_msg(uint16_t rpc_version, slurmdbd_msg_t *req)
{
	Buf buffer;
	int cnt, rc = SLURM_SUCCESS;
	static time_t syslog_time = 0;

	
	buffer = pack_slurmdbd_msg(rpc_version, req);

	slurm_mutex_lock(&agent_lock);
	if ((agent_tid == 0) || (agent_list == NULL)) {
		_create_agent();
		if ((agent_tid == 0) || (agent_list == NULL)) {
			slurm_mutex_unlock(&agent_lock);
			free_buf(buffer);
			return SLURM_ERROR;
		}
	}
	cnt = list_count(agent_list);
	if ((cnt >= (MAX_AGENT_QUEUE / 2)) &&
	    (difftime(time(NULL), syslog_time) > 120)) {
		/* Record critical error every 120 seconds */
		syslog_time = time(NULL);
		error("slurmdbd: agent queue filling, RESTART SLURMDBD NOW");
		syslog(LOG_CRIT, "*** RESTART SLURMDBD NOW ***");
	}
	if (cnt == (MAX_AGENT_QUEUE - 1))
		cnt -= _purge_job_start_req();
	if (cnt < MAX_AGENT_QUEUE) {
		if (list_enqueue(agent_list, buffer) == NULL)
			fatal("list_enqueue: memory allocation failure");
	} else {
		error("slurmdbd: agent queue is full, discarding request");
		rc = SLURM_ERROR;
	}
	slurm_mutex_unlock(&agent_lock);
	pthread_cond_broadcast(&agent_cond);
	return rc;
}

/* Open a connection to the Slurm DBD and set slurmdbd_fd */
static void _open_slurmdbd_fd(void)
{
	slurm_addr dbd_addr;
	uint16_t slurmdbd_port;
	char *   slurmdbd_host;

	if (slurmdbd_fd >= 0) {
		debug("Attempt to re-open slurmdbd socket");
		return;
	}

	slurmdbd_host = slurm_get_accounting_storage_host();
	slurmdbd_port = slurm_get_accounting_storage_port();
	if (slurmdbd_host == NULL)
		slurmdbd_host = xstrdup(DEFAULT_STORAGE_HOST);
	
	if (slurmdbd_port == 0) {
		slurmdbd_port = SLURMDBD_PORT;
		slurm_set_accounting_storage_port(slurmdbd_port);
	}
	slurm_set_addr(&dbd_addr, slurmdbd_port, slurmdbd_host);
	if (dbd_addr.sin_port == 0)
		error("Unable to locate SlurmDBD host %s:%u", 
		      slurmdbd_host, slurmdbd_port);
	else {
		slurmdbd_fd = slurm_open_msg_conn(&dbd_addr);

		if (slurmdbd_fd < 0)
			error("slurmdbd: slurm_open_msg_conn: %m");
		else {
			fd_set_nonblocking(slurmdbd_fd);
			if (_send_init_msg() != SLURM_SUCCESS)
				error("slurmdbd: Sending DdbInit msg: %m");
			else
				debug("slurmdbd: Sent DbdInit msg");
		}
	}
	xfree(slurmdbd_host);
}

extern Buf pack_slurmdbd_msg(uint16_t rpc_version, slurmdbd_msg_t *req)
{
	Buf buffer = init_buf(MAX_DBD_MSG_LEN);
	pack16(req->msg_type, buffer);

	switch (req->msg_type) {
	case DBD_ADD_ACCOUNTS:
	case DBD_ADD_ASSOCS:
	case DBD_ADD_CLUSTERS:
	case DBD_ADD_USERS:
	case DBD_GOT_ACCOUNTS:
	case DBD_GOT_ASSOCS:
	case DBD_GOT_CLUSTERS:
	case DBD_GOT_JOBS:
	case DBD_GOT_LIST:
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
	case DBD_GOT_TXN:
	case DBD_GOT_USERS:
	case DBD_UPDATE_SHARES_USED:
		slurmdbd_pack_list_msg(
			rpc_version, req->msg_type, 
			(dbd_list_msg_t *)req->data, buffer);
		break;
	case DBD_ADD_ACCOUNT_COORDS:
	case DBD_REMOVE_ACCOUNT_COORDS:
		slurmdbd_pack_acct_coord_msg(rpc_version,
					     (dbd_acct_coord_msg_t *)req->data,
					     buffer);
		break;
	case DBD_CLUSTER_PROCS:
	case DBD_FLUSH_JOBS:
		slurmdbd_pack_cluster_procs_msg(
			rpc_version, 
			(dbd_cluster_procs_msg_t *)req->data, buffer);
		break;
	case DBD_GET_ACCOUNTS:
	case DBD_GET_ASSOCS:
	case DBD_GET_CLUSTERS:
	case DBD_GET_JOBS_COND:
	case DBD_GET_QOS:
	case DBD_GET_WCKEYS:
	case DBD_GET_TXN:
	case DBD_GET_USERS:
	case DBD_REMOVE_ACCOUNTS:
	case DBD_REMOVE_ASSOCS:
	case DBD_REMOVE_CLUSTERS:
	case DBD_REMOVE_QOS:
	case DBD_REMOVE_WCKEYS:
	case DBD_REMOVE_USERS:
		slurmdbd_pack_cond_msg(
			rpc_version, req->msg_type,
			(dbd_cond_msg_t *)req->data, buffer);
		break;
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		slurmdbd_pack_usage_msg(
			rpc_version, req->msg_type,
			(dbd_usage_msg_t *)req->data, buffer);
		break;
	case DBD_GET_JOBS:
		slurmdbd_pack_get_jobs_msg(
			rpc_version, 
			(dbd_get_jobs_msg_t *)req->data, buffer);
		break;
	case DBD_INIT:
		slurmdbd_pack_init_msg(rpc_version,
				       (dbd_init_msg_t *)req->data, buffer, 
				       slurmdbd_auth_info);
		break;
	case DBD_FINI:
		slurmdbd_pack_fini_msg(rpc_version,
				       (dbd_fini_msg_t *)req->data, buffer);
		break;		
	case DBD_JOB_COMPLETE:
		slurmdbd_pack_job_complete_msg(rpc_version,
					       (dbd_job_comp_msg_t *)req->data,
					       buffer);
		break;
	case DBD_JOB_START:
		slurmdbd_pack_job_start_msg(rpc_version,
					    (dbd_job_start_msg_t *)req->data, 
					    buffer);
		break;
	case DBD_JOB_START_RC:
		slurmdbd_pack_job_start_rc_msg(
			rpc_version,
			(dbd_job_start_rc_msg_t *)req->data, buffer);
		break;		
	case DBD_JOB_SUSPEND:
		slurmdbd_pack_job_suspend_msg(
			rpc_version,
			(dbd_job_suspend_msg_t *)req->data, buffer);
		break;
	case DBD_MODIFY_ACCOUNTS:
	case DBD_MODIFY_ASSOCS:
	case DBD_MODIFY_CLUSTERS:
	case DBD_MODIFY_USERS:
		slurmdbd_pack_modify_msg(
			rpc_version, req->msg_type,
			(dbd_modify_msg_t *)req->data, buffer);
		break;
	case DBD_NODE_STATE:
		slurmdbd_pack_node_state_msg(
			rpc_version,
			(dbd_node_state_msg_t *)req->data, buffer);
		break;
	case DBD_RC:
		slurmdbd_pack_rc_msg(rpc_version,
				     (dbd_rc_msg_t *)req->data, buffer);
		break;
	case DBD_STEP_COMPLETE:
		slurmdbd_pack_step_complete_msg(
			rpc_version,
			(dbd_step_comp_msg_t *)req->data, buffer);
		break;
	case DBD_STEP_START:
		slurmdbd_pack_step_start_msg(rpc_version,
					     (dbd_step_start_msg_t *)req->data,
					     buffer);
		break;
	case DBD_REGISTER_CTLD:
		slurmdbd_pack_register_ctld_msg(
			rpc_version,
			(dbd_register_ctld_msg_t *)req->data, buffer);
		break;
	case DBD_ROLL_USAGE:
		slurmdbd_pack_roll_usage_msg(rpc_version,
					     (dbd_roll_usage_msg_t *)
					     req->data, buffer);
		break;
	default:
		error("slurmdbd: Invalid message type pack %u(%s:%u)",
		      req->msg_type,
		      slurmdbd_msg_type_2_str(req->msg_type, 1),
		      req->msg_type);
		free_buf(buffer);
		return NULL;
	}
	return buffer;
}

extern int unpack_slurmdbd_msg(uint16_t rpc_version, 
			       slurmdbd_msg_t *resp, Buf buffer)
{
	int rc = SLURM_SUCCESS;
       
	safe_unpack16(&resp->msg_type, buffer);
	
	switch (resp->msg_type) {
	case DBD_ADD_ACCOUNTS:
	case DBD_ADD_ASSOCS:
	case DBD_ADD_CLUSTERS:
	case DBD_ADD_USERS:
	case DBD_GOT_ACCOUNTS:
	case DBD_GOT_ASSOCS:
	case DBD_GOT_CLUSTERS:
	case DBD_GOT_JOBS:
	case DBD_GOT_LIST:
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
	case DBD_GOT_TXN:
	case DBD_GOT_USERS:
	case DBD_UPDATE_SHARES_USED:
		rc = slurmdbd_unpack_list_msg(
			rpc_version, resp->msg_type,
			(dbd_list_msg_t **)&resp->data, buffer);
		break;
	case DBD_ADD_ACCOUNT_COORDS:
	case DBD_REMOVE_ACCOUNT_COORDS:
		rc = slurmdbd_unpack_acct_coord_msg(
			rpc_version,
			(dbd_acct_coord_msg_t **)&resp->data, buffer);
		break;
	case DBD_CLUSTER_PROCS:
	case DBD_FLUSH_JOBS:
		rc = slurmdbd_unpack_cluster_procs_msg(
			rpc_version,
			(dbd_cluster_procs_msg_t **)&resp->data, buffer);
		break;
	case DBD_GET_ACCOUNTS:
	case DBD_GET_ASSOCS:
	case DBD_GET_CLUSTERS:
	case DBD_GET_JOBS_COND:
	case DBD_GET_USERS:
	case DBD_GET_QOS:
	case DBD_GET_WCKEYS:
	case DBD_GET_TXN:
	case DBD_REMOVE_ACCOUNTS:
	case DBD_REMOVE_ASSOCS:
	case DBD_REMOVE_CLUSTERS:
	case DBD_REMOVE_QOS:
	case DBD_REMOVE_WCKEYS:
	case DBD_REMOVE_USERS:
		rc = slurmdbd_unpack_cond_msg(
			rpc_version, resp->msg_type,
			(dbd_cond_msg_t **)&resp->data, buffer);
		break;
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		rc = slurmdbd_unpack_usage_msg(
			rpc_version,
			resp->msg_type, (dbd_usage_msg_t **)&resp->data, 
			buffer);
		break;
	case DBD_GET_JOBS:
		rc = slurmdbd_unpack_get_jobs_msg(
			rpc_version,
			(dbd_get_jobs_msg_t **)&resp->data, buffer);
		break;
	case DBD_INIT:
		rc = slurmdbd_unpack_init_msg(rpc_version,
					      (dbd_init_msg_t **)&resp->data,
					      buffer, 
					      slurmdbd_auth_info);
		break;
	case DBD_FINI:
		rc = slurmdbd_unpack_fini_msg(rpc_version,
					      (dbd_fini_msg_t **)&resp->data,
					      buffer);
		break;		
	case DBD_JOB_COMPLETE:
		rc = slurmdbd_unpack_job_complete_msg(
			rpc_version,
			(dbd_job_comp_msg_t **)&resp->data, buffer);
		break;
	case DBD_JOB_START:
		rc = slurmdbd_unpack_job_start_msg(
			rpc_version,
			(dbd_job_start_msg_t **)&resp->data, buffer);
		break;
	case DBD_JOB_START_RC:
		rc = slurmdbd_unpack_job_start_rc_msg(
			rpc_version,
			(dbd_job_start_rc_msg_t **)&resp->data, buffer);
		break;		
	case DBD_JOB_SUSPEND:
		rc = slurmdbd_unpack_job_suspend_msg(
			rpc_version,
			(dbd_job_suspend_msg_t **)&resp->data, buffer);
		break;
	case DBD_MODIFY_ACCOUNTS:
	case DBD_MODIFY_ASSOCS:
	case DBD_MODIFY_CLUSTERS:
	case DBD_MODIFY_USERS:
		rc = slurmdbd_unpack_modify_msg(
			rpc_version,
			resp->msg_type, (dbd_modify_msg_t **)&resp->data,
			buffer);
		break;
	case DBD_NODE_STATE:
		rc = slurmdbd_unpack_node_state_msg(
			rpc_version,
			(dbd_node_state_msg_t **)&resp->data, buffer);
		break;
	case DBD_RC:
		rc = slurmdbd_unpack_rc_msg(rpc_version,
					    (dbd_rc_msg_t **)&resp->data,
					    buffer);
		break;
	case DBD_STEP_COMPLETE:
		rc = slurmdbd_unpack_step_complete_msg(
			rpc_version,
			(dbd_step_comp_msg_t **)&resp->data, buffer);
		break;
	case DBD_STEP_START:
		rc = slurmdbd_unpack_step_start_msg(
			rpc_version,
			(dbd_step_start_msg_t **)&resp->data, buffer);
		break;
	case DBD_REGISTER_CTLD:
		rc = slurmdbd_unpack_register_ctld_msg(
			rpc_version,
			(dbd_register_ctld_msg_t **)&resp->data, buffer);
		break;
	case DBD_ROLL_USAGE:
		rc = slurmdbd_unpack_roll_usage_msg(
			rpc_version,
			(dbd_roll_usage_msg_t **)&resp->data, buffer);
		break;
	default:
		error("slurmdbd: Invalid message type unpack %u(%s)",
		      resp->msg_type,
		      slurmdbd_msg_type_2_str(resp->msg_type, 1),
		      resp->msg_type);
		return SLURM_ERROR;
	}
	return rc;

unpack_error:
	return SLURM_ERROR;
}

extern slurmdbd_msg_type_t str_2_slurmdbd_msg_type(char *msg_type)
{
	if(!msg_type) {
		return NO_VAL;
	} else if(!strcasecmp(msg_type, "Init")) {
		return DBD_INIT;
	} else if(!strcasecmp(msg_type, "Fini")) {
		return DBD_FINI;
	} else if(!strcasecmp(msg_type, "Add Accounts")) {
		return DBD_ADD_ACCOUNTS;
	} else if(!strcasecmp(msg_type, "Add Account Coord")) {
		return DBD_ADD_ACCOUNT_COORDS;
	} else if(!strcasecmp(msg_type, "Add Associations")) {
		return DBD_ADD_ASSOCS;
	} else if(!strcasecmp(msg_type, "Add Clusters")) {
		return DBD_ADD_CLUSTERS;
	} else if(!strcasecmp(msg_type, "Add Users")) {
		return DBD_ADD_USERS;
	} else if(!strcasecmp(msg_type, "Cluster Processors")) {
		return DBD_CLUSTER_PROCS;
	} else if(!strcasecmp(msg_type, "Flush Jobs")) {
		return DBD_FLUSH_JOBS;
	} else if(!strcasecmp(msg_type, "Get Accounts")) {
		return DBD_GET_ACCOUNTS;
	} else if(!strcasecmp(msg_type, "Get Associations")) {
		return DBD_GET_ASSOCS;
	} else if(!strcasecmp(msg_type, "Get Association Usage")) {
		return DBD_GET_ASSOC_USAGE;
	} else if(!strcasecmp(msg_type, "Get Clusters")) {
		return DBD_GET_CLUSTERS;
	} else if(!strcasecmp(msg_type, "Get Cluster Usage")) {
		return DBD_GET_CLUSTER_USAGE;
	} else if(!strcasecmp(msg_type, "Get Jobs")) {
		return DBD_GET_JOBS;
	} else if(!strcasecmp(msg_type, "Get Users")) {
		return DBD_GET_USERS;
	} else if(!strcasecmp(msg_type, "Got Accounts")) {
		return DBD_GOT_ACCOUNTS;
	} else if(!strcasecmp(msg_type, "Got Associations")) {
		return DBD_GOT_ASSOCS;
	} else if(!strcasecmp(msg_type, "Got Association Usage")) {
		return DBD_GOT_ASSOC_USAGE;
	} else if(!strcasecmp(msg_type, "Got Clusters")) {
		return DBD_GOT_CLUSTERS;
	} else if(!strcasecmp(msg_type, "Got Cluster Usage")) {
		return DBD_GOT_CLUSTER_USAGE;
	} else if(!strcasecmp(msg_type, "Got Jobs")) {
		return DBD_GOT_JOBS;
	} else if(!strcasecmp(msg_type, "Got List")) {
		return DBD_GOT_LIST;
	} else if(!strcasecmp(msg_type, "Got Users")) {
		return DBD_GOT_USERS;
	} else if(!strcasecmp(msg_type, "Job Complete")) {
		return DBD_JOB_COMPLETE;
	} else if(!strcasecmp(msg_type, "Job Start")) {
		return DBD_JOB_START;
	} else if(!strcasecmp(msg_type, "Job Start RC")) {
		return DBD_JOB_START_RC;
	} else if(!strcasecmp(msg_type, "Job Suspend")) {
		return DBD_JOB_SUSPEND;
	} else if(!strcasecmp(msg_type, "Modify Accounts")) {
		return DBD_MODIFY_ACCOUNTS;
	} else if(!strcasecmp(msg_type, "Modify Associations")) {
		return DBD_MODIFY_ASSOCS;
	} else if(!strcasecmp(msg_type, "Modify Clusters")) {
		return DBD_MODIFY_CLUSTERS;
	} else if(!strcasecmp(msg_type, "Modify Users")) {
		return DBD_MODIFY_USERS;
	} else if(!strcasecmp(msg_type, "Node State")) {
		return DBD_NODE_STATE;
	} else if(!strcasecmp(msg_type, "RC")) {
		return DBD_RC;
	} else if(!strcasecmp(msg_type, "Register Cluster")) {
		return DBD_REGISTER_CTLD;
	} else if(!strcasecmp(msg_type, "Remove Accounts")) {
		return DBD_REMOVE_ACCOUNTS;
	} else if(!strcasecmp(msg_type, "Remove Account Coords")) {
		return DBD_REMOVE_ACCOUNT_COORDS;
	} else if(!strcasecmp(msg_type, "Remove Associations")) {
		return DBD_REMOVE_ASSOCS;
	} else if(!strcasecmp(msg_type, "Remove Clusters")) {
		return DBD_REMOVE_CLUSTERS;
	} else if(!strcasecmp(msg_type, "Remove Users")) {
		return DBD_REMOVE_USERS;
	} else if(!strcasecmp(msg_type, "Roll Usage")) {
		return DBD_ROLL_USAGE;
	} else if(!strcasecmp(msg_type, "Step Complete")) {
		return DBD_STEP_COMPLETE;
	} else if(!strcasecmp(msg_type, "Step Start")) {
		return DBD_STEP_START;
	} else if(!strcasecmp(msg_type, "Update Shares Used")) {
		return DBD_UPDATE_SHARES_USED;
	} else if(!strcasecmp(msg_type, "Get Jobs Conditional")) {
		return DBD_GET_JOBS_COND;
	} else if(!strcasecmp(msg_type, "Get Transations")) {
		return DBD_GET_TXN;
	} else if(!strcasecmp(msg_type, "Got Transations")) {
		return DBD_GOT_TXN;
	} else if(!strcasecmp(msg_type, "Add QOS")) {
		return DBD_ADD_QOS;
	} else if(!strcasecmp(msg_type, "Get QOS")) {
		return DBD_GET_QOS;
	} else if(!strcasecmp(msg_type, "Got QOS")) {
		return DBD_GOT_QOS;
	} else if(!strcasecmp(msg_type, "Remove QOS")) {
		return DBD_REMOVE_QOS;
	} else if(!strcasecmp(msg_type, "Add WCKeys")) {
		return DBD_ADD_WCKEYS;
	} else if(!strcasecmp(msg_type, "Get WCKeys")) {
		return DBD_GET_WCKEYS;
	} else if(!strcasecmp(msg_type, "Got WCKeys")) {
		return DBD_GOT_WCKEYS;
	} else if(!strcasecmp(msg_type, "Remove WCKeys")) {
		return DBD_REMOVE_WCKEYS;
	} else if(!strcasecmp(msg_type, "Get WCKey Usage")) {
		return DBD_GET_WCKEY_USAGE;
	} else if(!strcasecmp(msg_type, "Got WCKey Usage")) {
		return DBD_GOT_WCKEY_USAGE;
	} else {
		return NO_VAL;		
	}

	return NO_VAL;
}

extern char *slurmdbd_msg_type_2_str(slurmdbd_msg_type_t msg_type, int get_enum)
{
	switch(msg_type) {
	case DBD_INIT:
		if(get_enum) {
			return "DBD_INIT";
		} else
			return "Init";
		break;
	case DBD_FINI:
		if(get_enum) {
			return "DBD_FINI";
		} else
			return "Fini";
		break;
	case DBD_ADD_ACCOUNTS:
		if(get_enum) {
			return "DBD_ADD_ACCOUNTS";
		} else
			return "Add Accounts";
		break;
	case DBD_ADD_ACCOUNT_COORDS:
		if(get_enum) {
			return "DBD_ADD_ACCOUNT_COORDS";
		} else
			return "Add Account Coord";
		break;
	case DBD_ADD_ASSOCS:
		if(get_enum) {
			return "DBD_ADD_ASSOCS";
		} else
			return "Add Associations";
		break;
	case DBD_ADD_CLUSTERS:
		if(get_enum) {
			return "DBD_ADD_CLUSTERS";
		} else
			return "Add Clusters";
		break;
	case DBD_ADD_USERS:
		if(get_enum) {
			return "DBD_ADD_USERS";
		} else
			return "Add Users";
		break;
	case DBD_CLUSTER_PROCS:
		if(get_enum) {
			return "DBD_CLUSTER_PROCS";
		} else
			return "Cluster Processors";
		break;
	case DBD_FLUSH_JOBS:
		if(get_enum) {
			return "DBD_FLUSH_JOBS";
		} else
			return "Flush Jobs";
		break;
	case DBD_GET_ACCOUNTS:
		if(get_enum) {
			return "DBD_GET_ACCOUNTS";
		} else
			return "Get Accounts";
		break;
	case DBD_GET_ASSOCS:
		if(get_enum) {
			return "DBD_GET_ASSOCS";
		} else
			return "Get Associations";
		break;
	case DBD_GET_ASSOC_USAGE:
		if(get_enum) {
			return "DBD_GET_ASSOC_USAGE";
		} else
			return "Get Association Usage";
		break;
	case DBD_GET_CLUSTERS:
		if(get_enum) {
			return "DBD_GET_CLUSTERS";
		} else
			return "Get Clusters";
		break;
	case DBD_GET_CLUSTER_USAGE:
		if(get_enum) {
			return "DBD_GET_CLUSTER_USAGE";
		} else
			return "Get Cluster Usage";
		break;
	case DBD_GET_JOBS:
		if(get_enum) {
			return "DBD_GET_JOBS";
		} else
			return "Get Jobs";
		break;
	case DBD_GET_USERS:
		if(get_enum) {
			return "DBD_GET_USERS";
		} else
			return "Get Users";
		break;
	case DBD_GOT_ACCOUNTS:
		if(get_enum) {
			return "DBD_GOT_ACCOUNTS";
		} else
			return "Got Accounts";
		break;
	case DBD_GOT_ASSOCS:
		if(get_enum) {
			return "DBD_GOT_ASSOCS";
		} else
			return "Got Associations";
		break;
	case DBD_GOT_ASSOC_USAGE:
		if(get_enum) {
			return "DBD_GOT_ASSOC_USAGE";
		} else
			return "Got Association Usage";
		break;
	case DBD_GOT_CLUSTERS:
		if(get_enum) {
			return "DBD_GOT_CLUSTERS";
		} else
			return "Got Clusters";
		break;
	case DBD_GOT_CLUSTER_USAGE:
		if(get_enum) {
			return "DBD_GOT_CLUSTER_USAGE";
		} else
			return "Got Cluster Usage";
		break;
	case DBD_GOT_JOBS:
		if(get_enum) {
			return "DBD_GOT_JOBS";
		} else
			return "Got Jobs";
		break;
	case DBD_GOT_LIST:
		if(get_enum) {
			return "DBD_GOT_LIST";
		} else
			return "Got List";
		break;
	case DBD_GOT_USERS:
		if(get_enum) {
			return "DBD_GOT_USERS";
		} else
			return "Got Users";
		break;
	case DBD_JOB_COMPLETE:
		if(get_enum) {
			return "DBD_JOB_COMPLETE";
		} else
			return "Job Complete";
		break;
	case DBD_JOB_START:
		if(get_enum) {
			return "DBD_JOB_START";
		} else
			return "Job Start";
		break;
	case DBD_JOB_START_RC:
		if(get_enum) {
			return "DBD_JOB_START_RC";
		} else
			return "Job Start RC";
		break;
	case DBD_JOB_SUSPEND:
		if(get_enum) {
			return "DBD_JOB_SUSPEND";
		} else
			return "Job Suspend";
		break;
	case DBD_MODIFY_ACCOUNTS:
		if(get_enum) {
			return "DBD_MODIFY_ACCOUNTS";
		} else
			return "Modify Accounts";
		break;
	case DBD_MODIFY_ASSOCS:
		if(get_enum) {
			return "DBD_MODIFY_ASSOCS";
		} else
			return "Modify Associations";
		break;
	case DBD_MODIFY_CLUSTERS:
		if(get_enum) {
			return "DBD_MODIFY_CLUSTERS";
		} else
			return "Modify Clusters";
		break;
	case DBD_MODIFY_USERS:
		if(get_enum) {
			return "DBD_MODIFY_USERS";
		} else
			return "Modify Users";
		break;
	case DBD_NODE_STATE:
		if(get_enum) {
			return "DBD_NODE_STATE";
		} else
			return "Node State";
		break;
	case DBD_RC:
		if(get_enum) {
			return "DBD_RC";
		} else
			return "Return Code";
		break;
	case DBD_REGISTER_CTLD:
		if(get_enum) {
			return "DBD_REGISTER_CTLD";
		} else
			return "Register Cluster";
		break;
	case DBD_REMOVE_ACCOUNTS:
		if(get_enum) {
			return "DBD_REMOVE_ACCOUNTS";
		} else
			return "Remove Accounts";
		break;
	case DBD_REMOVE_ACCOUNT_COORDS:
		if(get_enum) {
			return "DBD_REMOVE_ACCOUNT_COORDS";
		} else
			return "Remove Account Coords";
		break;
	case DBD_REMOVE_ASSOCS:
		if(get_enum) {
			return "DBD_REMOVE_ASSOCS";
		} else
			return "Remove Associations";
		break;
	case DBD_REMOVE_CLUSTERS:
		if(get_enum) {
			return "DBD_REMOVE_CLUSTERS";
		} else
			return "Remove Clusters";
		break;
	case DBD_REMOVE_USERS:
		if(get_enum) {
			return "DBD_REMOVE_USERS";
		} else
			return "Remove Users";
		break;
	case DBD_ROLL_USAGE:
		if(get_enum) {
			return "DBD_ROLL_USAGE";
		} else
			return "Roll Usage";
		break;
	case DBD_STEP_COMPLETE:
		if(get_enum) {
			return "DBD_STEP_COMPLETE";
		} else
			return "Step Complete";
		break;
	case DBD_STEP_START:
		if(get_enum) {
			return "DBD_STEP_START";
		} else
			return "Step Start";
		break;
	case DBD_UPDATE_SHARES_USED:
		if(get_enum) {
			return "DBD_UPDATE_SHARES_USED";
		} else
			return "Update Shares Used";
		break;
	case DBD_GET_JOBS_COND:
		if(get_enum) {
			return "DBD_GET_JOBS_COND";
		} else
			return "Get Jobs Conditional";
		break;
	case DBD_GET_TXN:
		if(get_enum) {
			return "DBD_GET_TXN";
		} else
			return "Get Transations";
		break;
	case DBD_GOT_TXN:
		if(get_enum) {
			return "DBD_GOT_TXN";
		} else
			return "Got Transations";
		break;
	case DBD_ADD_QOS:
		if(get_enum) {
			return "DBD_ADD_QOS";
		} else
			return "Add QOS";
		break;
	case DBD_GET_QOS:
		if(get_enum) {
			return "DBD_GET_QOS";
		} else
			return "Get QOS";
		break;
	case DBD_GOT_QOS:
		if(get_enum) {
			return "DBD_GOT_QOS";
		} else
			return "Got QOS";
		break;
	case DBD_REMOVE_QOS:
		if(get_enum) {
			return "DBD_REMOVE_QOS";
		} else
			return "Remove QOS";
		break;
	case DBD_ADD_WCKEYS:
		if(get_enum) {
			return "DBD_ADD_WCKEYS";
		} else
			return "Add WCKeys";
		break;
	case DBD_GET_WCKEYS:
		if(get_enum) {
			return "DBD_GET_WCKEYS";
		} else
			return "Get WCKeys";
		break;
	case DBD_GOT_WCKEYS:
		if(get_enum) {
			return "DBD_GOT_WCKEYS";
		} else
			return "Got WCKeys";
		break;
	case DBD_REMOVE_WCKEYS:
		if(get_enum) {
			return "DBD_REMOVE_WCKEYS";
		} else
			return "Remove WCKeys";
		break;
	case DBD_GET_WCKEY_USAGE:
		if(get_enum) {
			return "DBD_GET_WCKEY_USAGE";
		} else
			return "Get WCKey Usage";
		break;
	case DBD_GOT_WCKEY_USAGE:
		if(get_enum) {
			return "DBD_GOT_WCKEY_USAGE";
		} else
			return "Got WCKey Usage";
		break;
	default:
		return "Unknown";
		break;
	}

	return "Unknown";
}

static int _send_init_msg()
{
	int rc, read_timeout;
	Buf buffer;
	dbd_init_msg_t req;

	buffer = init_buf(1024);
	pack16((uint16_t) DBD_INIT, buffer);
	req.rollback = rollback_started;
	req.version  = SLURMDBD_VERSION;
	slurmdbd_pack_init_msg(SLURMDBD_VERSION, &req, buffer,
			       slurmdbd_auth_info);

	rc = _send_msg(buffer);
	free_buf(buffer);
	if (rc != SLURM_SUCCESS) {
		error("slurmdbd: Sending DBD_INIT message");
		return rc;
	}

	read_timeout = slurm_get_msg_timeout() * 1000;
	rc = _get_return_code(SLURMDBD_VERSION, read_timeout);
	
	return rc;
}

static int _send_fini_msg(void)
{
	Buf buffer;
	dbd_fini_msg_t req;

	buffer = init_buf(1024);
	pack16((uint16_t) DBD_FINI, buffer);
	req.commit  = 0;
	req.close_conn   = 1;
	slurmdbd_pack_fini_msg(SLURMDBD_VERSION, &req, buffer);

	_send_msg(buffer);
	free_buf(buffer);
	
	return SLURM_SUCCESS;
}

/* Close the SlurmDbd connection */
static void _close_slurmdbd_fd(void)
{
	if (slurmdbd_fd >= 0) {
		close(slurmdbd_fd);
		slurmdbd_fd = -1;
	}
}

/* Reopen the Slurm DBD connection due to some error */
static void _reopen_slurmdbd_fd(void)
{
	info("slurmdbd: reopening connection");
	_close_slurmdbd_fd();
	_open_slurmdbd_fd();
}

static int _send_msg(Buf buffer)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_wrote;
	int rc, retry_cnt = 0;

	if (slurmdbd_fd < 0)
		return EAGAIN;

	rc =_fd_writeable(slurmdbd_fd);
	if (rc == -1) {
	re_open:	/* SlurmDBD shutdown, try to reopen a connection now */
		if (retry_cnt++ > 3)
			return EAGAIN;
		_reopen_slurmdbd_fd();
		rc = _fd_writeable(slurmdbd_fd);
	}
	if (rc < 1)
		return EAGAIN;

	msg_size = get_buf_offset(buffer);
	nw_size = htonl(msg_size);
	msg_wrote = write(slurmdbd_fd, &nw_size, sizeof(nw_size));
	if (msg_wrote != sizeof(nw_size))
		return EAGAIN;

	msg = get_buf_data(buffer);
	while (msg_size > 0) {
		rc = _fd_writeable(slurmdbd_fd);
		if (rc == -1)
			goto re_open;
		if (rc < 1)
			return EAGAIN;
		msg_wrote = write(slurmdbd_fd, msg, msg_size);
		if (msg_wrote <= 0)
			return EAGAIN;
		msg += msg_wrote;
		msg_size -= msg_wrote;
	}

	return SLURM_SUCCESS;
}

static int _get_return_code(uint16_t rpc_version, int read_timeout)
{
	Buf buffer;
	uint16_t msg_type;
	dbd_rc_msg_t *msg;
	dbd_job_start_rc_msg_t *js_msg;
	int rc = SLURM_ERROR;

	buffer = _recv_msg(read_timeout);
	if (buffer == NULL)
		return rc;

	safe_unpack16(&msg_type, buffer);
	switch(msg_type) {
	case DBD_JOB_START_RC:
		if (slurmdbd_unpack_job_start_rc_msg(rpc_version, 
						     &js_msg, buffer)
		    == SLURM_SUCCESS) {
			rc = js_msg->return_code;
			slurmdbd_free_job_start_rc_msg(rpc_version, js_msg);
			if (rc != SLURM_SUCCESS)
				error("slurmdbd: DBD_JOB_START_RC is %d", rc);
		} else
			error("slurmdbd: unpack message error");
		break;
	case DBD_RC:
		if (slurmdbd_unpack_rc_msg(rpc_version, 
					   &msg, buffer) == SLURM_SUCCESS) {
			rc = msg->return_code;
			if (rc != SLURM_SUCCESS) {
				if(msg->sent_type == DBD_REGISTER_CTLD &&
				   slurm_get_accounting_storage_enforce()) {
					error("slurmdbd: DBD_RC is %d from "
					      "%s(%u): %s",
					      rc,
					      slurmdbd_msg_type_2_str(
						      msg->sent_type, 1),
					      msg->sent_type,
					      msg->comment);
					fatal("You need to add this cluster "
					      "to accounting if you want to "
					      "enforce associations, or no "
					      "jobs will ever run.");
				} else
					error("slurmdbd: DBD_RC is %d from "
					      "%s(%u): %s",
					      rc,
					      slurmdbd_msg_type_2_str(
						      msg->sent_type, 1),
					      msg->sent_type,
					      msg->comment);
				
			}
			slurmdbd_free_rc_msg(rpc_version, msg);
		} else
			error("slurmdbd: unpack message error");
		break;
	default:
		error("slurmdbd: bad message type %d != DBD_RC", msg_type);
	}

unpack_error:
	free_buf(buffer);
	return rc;
}

static Buf _recv_msg(int read_timeout)
{
	uint32_t msg_size, nw_size;
	char *msg;
	ssize_t msg_read, offset;
	Buf buffer;

	if (slurmdbd_fd < 0)
		return NULL;

	if (!_fd_readable(slurmdbd_fd, read_timeout))
		return NULL;
	msg_read = read(slurmdbd_fd, &nw_size, sizeof(nw_size));
	if (msg_read != sizeof(nw_size))
		return NULL;
	msg_size = ntohl(nw_size);
	/* We don't error check for an upper limit here
  	 * since size could possibly be massive */
	if (msg_size < 2) {
		error("slurmdbd: Invalid msg_size (%u)", msg_size);
		return NULL;
	}

	msg = xmalloc(msg_size);
	offset = 0;
	while (msg_size > offset) {
		if (!_fd_readable(slurmdbd_fd, read_timeout))
			break;		/* problem with this socket */
		msg_read = read(slurmdbd_fd, (msg + offset), 
				(msg_size - offset));
		if (msg_read <= 0) {
			error("slurmdbd: read: %m");
			break;
		}
		offset += msg_read;
	}
	if (msg_size != offset) {
		if (agent_shutdown == 0) {
			error("slurmdbd: only read %d of %d bytes", 
			      offset, msg_size);
		}	/* else in shutdown mode */
		xfree(msg);
		return NULL;
	}

	buffer = create_buf(msg, msg_size);
	if (buffer == NULL)
		fatal("create_buf: malloc failure");
	return buffer;
}

/* Return time in msec since "start time" */
static int _tot_wait (struct timeval *start_time)
{
	struct timeval end_time;
	int msec_delay;

	gettimeofday(&end_time, NULL);
	msec_delay =   (end_time.tv_sec  - start_time->tv_sec ) * 1000;
	msec_delay += ((end_time.tv_usec - start_time->tv_usec + 500) / 1000);
	return msec_delay;
}

/* Wait until a file is readable, 
 * RET false if can not be read */
static bool _fd_readable(slurm_fd fd, int read_timeout)
{
	struct pollfd ufds;
	int rc, time_left;
	struct timeval tstart;

	ufds.fd     = fd;
	ufds.events = POLLIN;
	gettimeofday(&tstart, NULL);
	while (agent_shutdown == 0) {
		time_left = read_timeout - _tot_wait(&tstart);
		rc = poll(&ufds, 1, time_left);
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("poll: %m");
			return false;
		}
		if (rc == 0)
			return false;
		if (ufds.revents & POLLHUP) {
			debug2("SlurmDBD connection closed");
			return false;
		}
		if (ufds.revents & POLLNVAL) {
			error("SlurmDBD connection is invalid");
			return false;
		}
		if (ufds.revents & POLLERR) {
			error("SlurmDBD connection experienced an error");
			return false;
		}
		if ((ufds.revents & POLLIN) == 0) {
			error("SlurmDBD connection %d events %d", 
			      fd, ufds.revents);
			return false;
		}
		/* revents == POLLIN */
		return true;
	}
	return false;
}

/* Wait until a file is writable, 
 * RET 1 if file can be written now,
 *     0 if can not be written to within 5 seconds
 *     -1 if file has been closed POLLHUP
 */
static int _fd_writeable(slurm_fd fd)
{
	struct pollfd ufds;
	int write_timeout = 5000;
	int rc, time_left;
	struct timeval tstart;
	char temp[2];

	ufds.fd     = fd;
	ufds.events = POLLOUT;
	gettimeofday(&tstart, NULL);
	while (agent_shutdown == 0) {
		time_left = write_timeout - _tot_wait(&tstart);
		rc = poll(&ufds, 1, time_left);
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("poll: %m");
			return -1;
		}
		if (rc == 0)
			return 0;
		/*
		 * Check here to make sure the socket really is there.
		 * If not then exit out and notify the sender.  This
 		 * is here since a write doesn't always tell you the
		 * socket is gone, but getting 0 back from a
		 * nonblocking read means just that. 
		 */
		if (ufds.revents & POLLHUP || (recv(fd, &temp, 1, 0) == 0)) {
			debug2("SlurmDBD connection is closed");
			return -1;
		}
		if (ufds.revents & POLLNVAL) {
			error("SlurmDBD connection is invalid");
			return 0;
		}
		if (ufds.revents & POLLERR) {
			error("SlurmDBD connection experienced an error: %m");
			return 0;
		}
		if ((ufds.revents & POLLOUT) == 0) {
			error("SlurmDBD connection %d events %d", 
			      fd, ufds.revents);
			return 0;
		}
		/* revents == POLLOUT */
		return 1;
	}
	return 0;
}

/****************************************************************************
 * Functions for agent to manage queue of pending message for the Slurm DBD
 ****************************************************************************/
static void _create_agent(void)
{
	if (agent_list == NULL) {
		agent_list = list_create(_agent_queue_del);
		if (agent_list == NULL)
			fatal("list_create: malloc failure");
		_load_dbd_state();
	}

	if (agent_tid == 0) {
		pthread_attr_t agent_attr;
		slurm_attr_init(&agent_attr);
		if (pthread_create(&agent_tid, &agent_attr, _agent, NULL) ||
		    (agent_tid == 0))
			fatal("pthread_create: %m");
	}
}

static void _agent_queue_del(void *x)
{
	Buf buffer = (Buf) x;
	free_buf(buffer);
}

static void _shutdown_agent(void)
{
	int i;

	if (agent_tid) {
		agent_shutdown = time(NULL);
		for (i=0; i<50; i++) {	/* up to 5 secs total */
			pthread_cond_broadcast(&agent_cond);
			usleep(100000);	/* 0.1 sec per try */
			if (pthread_kill(agent_tid, SIGUSR1))
				break;

		}
		/* On rare occasions agent thread may not end quickly, 
		 * perhaps due to communication problems with slurmdbd. 
		 * Cancel it and join before returning or we could remove 
		 * and leave the agent without valid data */
		if (pthread_kill(agent_tid, 0) == 0) {
			error("slurmdbd: agent failed to shutdown gracefully");
			error("slurmdbd: unable to save pending requests");
			pthread_cancel(agent_tid);
		}
		pthread_join(agent_tid,  NULL);
		agent_tid = 0;
	}
}

static void _slurmdbd_packstr(void *str, uint16_t rpc_version, Buf buffer)
{
	packstr((char *)str, buffer);
}

static int _slurmdbd_unpackstr(void **str, uint16_t rpc_version, Buf buffer)
{
	uint32_t uint32_tmp;
	safe_unpackstr_xmalloc((char **)str, &uint32_tmp, buffer);
	return SLURM_SUCCESS;
unpack_error:
	return SLURM_ERROR;
}

static void *_agent(void *x)
{
	int cnt, rc;
	Buf buffer;
	struct timespec abs_time;
	static time_t fail_time = 0;
	int sigarray[] = {SIGUSR1, 0};
	int read_timeout = SLURMDBD_TIMEOUT * 1000;
 
	/* Prepare to catch SIGUSR1 to interrupt pending
	 * I/O and terminate in a timely fashion. */
	xsignal(SIGUSR1, _sig_handler);
	xsignal_unblock(sigarray);

	while (agent_shutdown == 0) {

		slurm_mutex_lock(&slurmdbd_lock);
		if ((slurmdbd_fd < 0) && 
		    (difftime(time(NULL), fail_time) >= 10)) {			
			/* The connection to Slurm DBD is not open */
			_open_slurmdbd_fd();
			if (slurmdbd_fd < 0)
				fail_time = time(NULL);
		}

		slurm_mutex_lock(&agent_lock);
		if (agent_list && slurmdbd_fd)
			cnt = list_count(agent_list);
		else
			cnt = 0;
		if ((cnt == 0) || (slurmdbd_fd < 0) ||
		    (fail_time && (difftime(time(NULL), fail_time) < 10))) {
			slurm_mutex_unlock(&slurmdbd_lock);
			abs_time.tv_sec  = time(NULL) + 10;
			abs_time.tv_nsec = 0;
			rc = pthread_cond_timedwait(&agent_cond, &agent_lock,
						    &abs_time);
			slurm_mutex_unlock(&agent_lock);
			continue;
		} else if ((cnt > 0) && ((cnt % 50) == 0))
			info("slurmdbd: agent queue size %u", cnt);
		/* Leave item on the queue until processing complete */
		if (agent_list)
			buffer = (Buf) list_peek(agent_list);
		else
			buffer = NULL;
		slurm_mutex_unlock(&agent_lock);
		if (buffer == NULL) {
			slurm_mutex_unlock(&slurmdbd_lock);

			slurm_mutex_lock(&assoc_cache_mutex);
			if(slurmdbd_fd >= 0 && running_cache)
				pthread_cond_signal(&assoc_cache_cond);
			slurm_mutex_unlock(&assoc_cache_mutex);

			continue;
		}

		/* NOTE: agent_lock is clear here, so we can add more
		 * requests to the queue while waiting for this RPC to 
		 * complete. */
		rc = _send_msg(buffer);
		if (rc != SLURM_SUCCESS) {
			if (agent_shutdown) {
				slurm_mutex_unlock(&slurmdbd_lock);
				break;
			}
			error("slurmdbd: Failure sending message");
		} else {
			rc = _get_return_code(SLURMDBD_VERSION, read_timeout);
			if (rc == EAGAIN) {
				if (agent_shutdown) {
					slurm_mutex_unlock(&slurmdbd_lock);
					break;
				}
				error("slurmdbd: Failure with "
				      "message need to resend");
			}
		}
		slurm_mutex_unlock(&slurmdbd_lock);
		
		slurm_mutex_lock(&assoc_cache_mutex);
		if(slurmdbd_fd >= 0 && running_cache)
			pthread_cond_signal(&assoc_cache_cond);
		slurm_mutex_unlock(&assoc_cache_mutex);

		slurm_mutex_lock(&agent_lock);
		if (agent_list && (rc == SLURM_SUCCESS)) {
			buffer = (Buf) list_dequeue(agent_list);
			free_buf(buffer);
			fail_time = 0;
		} else {
			fail_time = time(NULL);
		}
		slurm_mutex_unlock(&agent_lock);
	}

	slurm_mutex_lock(&agent_lock);
	_save_dbd_state();
	if (agent_list) {
		list_destroy(agent_list);
		agent_list = NULL;
	}
	slurm_mutex_unlock(&agent_lock);
	return NULL;
}

static void _save_dbd_state(void)
{
	char *dbd_fname;
	Buf buffer;
	int fd, rc, wrote = 0;
	uint16_t msg_type;
	uint32_t offset;

	dbd_fname = slurm_get_state_save_location();
	xstrcat(dbd_fname, "/dbd.messages");
	fd = open(dbd_fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error("slurmdbd: Creating state save file %s", dbd_fname);
	} else if (agent_list) {
		while ((buffer = list_dequeue(agent_list))) {
			/* We do not want to store registration
			   messages.  If an admin puts in an incorrect
			   cluster name we can get a deadlock unless
			   they add the bogus cluster name to the
			   accounting system.
			*/
			offset = get_buf_offset(buffer);
			if (offset < 2) {
				free_buf(buffer);
				continue;
			}
			set_buf_offset(buffer, 0);
			unpack16(&msg_type, buffer);
			set_buf_offset(buffer, offset);
			if(msg_type == DBD_REGISTER_CTLD) {
				free_buf(buffer);
				continue;
			}
			rc = _save_dbd_rec(fd, buffer);
			free_buf(buffer);
			if (rc != SLURM_SUCCESS)
				break;
			wrote++;
		}
	}
	if (fd >= 0) {
		verbose("slurmdbd: saved %d pending RPCs", wrote);
		(void) close(fd);
	}
	xfree(dbd_fname);
}

static void _load_dbd_state(void)
{
	char *dbd_fname;
	Buf buffer;
	int fd, recovered = 0;

	dbd_fname = slurm_get_state_save_location();
	xstrcat(dbd_fname, "/dbd.messages");
	fd = open(dbd_fname, O_RDONLY);
	if (fd < 0) {
		error("slurmdbd: Opening state save file %s", dbd_fname);
	} else {
		while (1) {
			buffer = _load_dbd_rec(fd);
			if (buffer == NULL)
				break;
			if (list_enqueue(agent_list, buffer) == NULL)
				fatal("slurmdbd: list_enqueue, no memory");
			recovered++;
		}
	}
	if (fd >= 0) {
		verbose("slurmdbd: recovered %d pending RPCs", recovered);
		(void) close(fd);
		(void) unlink(dbd_fname);	/* clear save state */
	}
	xfree(dbd_fname);
}

static int _save_dbd_rec(int fd, Buf buffer)
{
	ssize_t size, wrote;
	uint32_t msg_size = get_buf_offset(buffer);
	uint32_t magic = DBD_MAGIC;
	char *msg = get_buf_data(buffer);

	size = sizeof(msg_size);
	wrote = write(fd, &msg_size, size);
	if (wrote != size) {
		error("slurmdbd: state save error: %m");
		return SLURM_ERROR;
	}

	wrote = 0;
	while (wrote < msg_size) {
		wrote = write(fd, msg, msg_size);
		if (wrote > 0) {
			msg += wrote;
			msg_size -= wrote;
		} else if ((wrote == -1) && (errno == EINTR))
			continue;
		else {
			error("slurmdbd: state save error: %m");
			return SLURM_ERROR;
		}
	}	

	size = sizeof(magic);
	wrote = write(fd, &magic, size);
	if (wrote != size) {
		error("slurmdbd: state save error: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static Buf _load_dbd_rec(int fd)
{
	ssize_t size, rd_size;
	uint32_t msg_size, magic;
	char *msg;
	Buf buffer;

	size = sizeof(msg_size);
	rd_size = read(fd, &msg_size, size);
	if (rd_size == 0)
		return (Buf) NULL;
	if (rd_size != size) {
		error("slurmdbd: state recover error: %m");
		return (Buf) NULL;
	}
	if (msg_size > MAX_DBD_MSG_LEN) {
		error("slurmdbd: state recover error, msg_size=%u", msg_size);
		return (Buf) NULL;
	}

	buffer = init_buf((int) msg_size);
	if (buffer == NULL)
		fatal("slurmdbd: create_buf malloc failure");
	set_buf_offset(buffer, msg_size);
	msg = get_buf_data(buffer);
	size = msg_size;
	while (size) {
		rd_size = read(fd, msg, size);
		if (rd_size > 0) {
			msg += rd_size;
			size -= rd_size;
		} else if ((rd_size == -1) && (errno == EINTR))
			continue;
		else {
			error("slurmdbd: state recover error: %m");
			free_buf(buffer);
			return (Buf) NULL;
		}
	}

	size = sizeof(magic);
	rd_size = read(fd, &magic, size);
	if ((rd_size != size) || (magic != DBD_MAGIC)) {
		error("slurmdbd: state recover error");
		free_buf(buffer);
		return (Buf) NULL;
	}

	return buffer;
}

static void _sig_handler(int signal)
{
}

/* Purge queued job/step start records from the agent queue
 * RET number of records purged */
static int _purge_job_start_req(void)
{
	int purged = 0;
	ListIterator iter;
	uint16_t msg_type;
	uint32_t offset;
	Buf buffer;

	iter = list_iterator_create(agent_list);
	while ((buffer = list_next(iter))) {
		offset = get_buf_offset(buffer);
		if (offset < 2)
			continue;
		set_buf_offset(buffer, 0);
		unpack16(&msg_type, buffer);
		set_buf_offset(buffer, offset);
		if ((msg_type == DBD_JOB_START) ||
		    (msg_type == DBD_STEP_START)) {
			list_remove(iter);
			purged++;
		}
	}
	list_iterator_destroy(iter);
	info("slurmdbd: purge %d job/step start records", purged);
	return purged;
}

/****************************************************************************\
 * Free data structures
\****************************************************************************/
void inline slurmdbd_free_acct_coord_msg(uint16_t rpc_version, 
					 dbd_acct_coord_msg_t *msg)
{
	if(msg) {
		if(msg->acct_list) {
			list_destroy(msg->acct_list);
			msg->acct_list = NULL;
		}
		destroy_acct_user_cond(msg->cond);
		xfree(msg);
	}
}
void inline slurmdbd_free_cluster_procs_msg(uint16_t rpc_version, 
					    dbd_cluster_procs_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		xfree(msg);
	}
}

void inline slurmdbd_free_cond_msg(uint16_t rpc_version, 
				   slurmdbd_msg_type_t type,
				   dbd_cond_msg_t *msg)
{
	void (*my_destroy) (void *object);

	if (msg) {
		switch(type) {
		case DBD_GET_ACCOUNTS:
		case DBD_REMOVE_ACCOUNTS:
			my_destroy = destroy_acct_account_cond;
			break;
		case DBD_GET_ASSOCS:
		case DBD_REMOVE_ASSOCS:
			my_destroy = destroy_acct_association_cond;
			break;
		case DBD_GET_CLUSTERS:
		case DBD_REMOVE_CLUSTERS:
			my_destroy = destroy_acct_cluster_cond;
			break;
		case DBD_GET_JOBS_COND:
			my_destroy = destroy_acct_job_cond;
			break;
		case DBD_GET_QOS:
		case DBD_REMOVE_QOS:
			my_destroy = destroy_acct_qos_cond;
			break;
		case DBD_GET_WCKEYS:
		case DBD_REMOVE_WCKEYS:
			my_destroy = destroy_acct_wckey_cond;
			break;
		case DBD_GET_TXN:
			my_destroy = destroy_acct_txn_cond;
			break;
		case DBD_GET_USERS:
		case DBD_REMOVE_USERS:
			my_destroy = destroy_acct_user_cond;
			break;
		default:
			fatal("Unknown cond type");
			return;
		}
		if(msg->cond)
			(*(my_destroy))(msg->cond);
		xfree(msg);
	}
}

void inline slurmdbd_free_get_jobs_msg(uint16_t rpc_version, 
				       dbd_get_jobs_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		if(msg->selected_steps)
			list_destroy(msg->selected_steps);
		if(msg->selected_parts)
			list_destroy(msg->selected_parts);
		xfree(msg->user);
		xfree(msg);
	}
}

void inline slurmdbd_free_init_msg(uint16_t rpc_version, 
				   dbd_init_msg_t *msg)
{
	xfree(msg);
}

void inline slurmdbd_free_fini_msg(uint16_t rpc_version, 
				   dbd_fini_msg_t *msg)
{
	xfree(msg);
}

void inline slurmdbd_free_job_complete_msg(uint16_t rpc_version, 
					   dbd_job_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->nodes);
		xfree(msg);
	}
}

void inline slurmdbd_free_job_start_msg(uint16_t rpc_version, 
					dbd_job_start_msg_t *msg)
{
	if (msg) {
		xfree(msg->account);
		xfree(msg->block_id);
		xfree(msg->cluster);
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg->partition);
		xfree(msg);
	}
}

void inline slurmdbd_free_job_start_rc_msg(uint16_t rpc_version, 
					   dbd_job_start_rc_msg_t *msg)
{
	xfree(msg);
}

void inline slurmdbd_free_job_suspend_msg(uint16_t rpc_version, 
					  dbd_job_suspend_msg_t *msg)
{
	xfree(msg);
}

void inline slurmdbd_free_list_msg(uint16_t rpc_version, 
				   dbd_list_msg_t *msg)
{
	if (msg) {
		if(msg->my_list)
			list_destroy(msg->my_list);
		xfree(msg);
	}
}

void inline slurmdbd_free_modify_msg(uint16_t rpc_version, 
				     slurmdbd_msg_type_t type,
				     dbd_modify_msg_t *msg)
{
	void (*destroy_cond) (void *object);
	void (*destroy_rec) (void *object);
	
	if (msg) {
		switch(type) {
		case DBD_MODIFY_ACCOUNTS:
			destroy_cond = destroy_acct_account_cond;
			destroy_rec = destroy_acct_account_rec;
			break;
		case DBD_MODIFY_ASSOCS:
			destroy_cond = destroy_acct_association_cond;
			destroy_rec = destroy_acct_association_rec;
			break;
		case DBD_MODIFY_CLUSTERS:
			destroy_cond = destroy_acct_cluster_cond;
			destroy_rec = destroy_acct_cluster_rec;
			break;
		case DBD_MODIFY_USERS:
			destroy_cond = destroy_acct_user_cond;
			destroy_rec = destroy_acct_user_rec;
			break;
		default:
			fatal("Unknown modify type");
			return;
		}
		
		if(msg->cond)
			(*(destroy_cond))(msg->cond);
		if(msg->rec)
			(*(destroy_rec))(msg->rec);
		xfree(msg);
	}
}

void inline slurmdbd_free_node_state_msg(uint16_t rpc_version, 
					 dbd_node_state_msg_t *msg)
{
	if (msg) {
		xfree(msg->cluster_name);
		xfree(msg->hostlist);
		xfree(msg->reason);
		xfree(msg);
	}
}

void inline slurmdbd_free_rc_msg(uint16_t rpc_version, 
				 dbd_rc_msg_t *msg)
{
	if(msg) {
		xfree(msg->comment);
		xfree(msg);
	}
}

void inline slurmdbd_free_register_ctld_msg(uint16_t rpc_version, 
					    dbd_register_ctld_msg_t *msg)
{
	if(msg) {
		xfree(msg->cluster_name);
		xfree(msg);
	}
}

void inline slurmdbd_free_roll_usage_msg(uint16_t rpc_version, 
					 dbd_roll_usage_msg_t *msg)
{
	xfree(msg);
}

void inline slurmdbd_free_step_complete_msg(uint16_t rpc_version, 
					    dbd_step_comp_msg_t *msg)
{
	if (msg) {
		xfree(msg->jobacct);
		xfree(msg);
	}
}

void inline slurmdbd_free_step_start_msg(uint16_t rpc_version, 
					 dbd_step_start_msg_t *msg)
{
	if (msg) {
		xfree(msg->name);
		xfree(msg->nodes);
		xfree(msg);
	}
}

void inline slurmdbd_free_usage_msg(uint16_t rpc_version, 
				    slurmdbd_msg_type_t type,
				    dbd_usage_msg_t *msg)
{
	void (*destroy_rec) (void *object);
	if (msg) {
		switch(type) {
		case DBD_GET_ASSOC_USAGE:
		case DBD_GOT_ASSOC_USAGE:
			destroy_rec = destroy_acct_association_rec;
			break;
		case DBD_GET_CLUSTER_USAGE:
		case DBD_GOT_CLUSTER_USAGE:
			destroy_rec = destroy_acct_cluster_rec;
			break;
		case DBD_GET_WCKEY_USAGE:
		case DBD_GOT_WCKEY_USAGE:
			destroy_rec = destroy_acct_wckey_rec;
			break;
		default:
			fatal("Unknown usuage type");
			return;
		}

		if(msg->rec)
			(*(destroy_rec))(msg->rec);
		xfree(msg);
	}
}

/****************************************************************************\
 * Pack and unpack data structures
\****************************************************************************/
void inline
slurmdbd_pack_acct_coord_msg(uint16_t rpc_version,
			     dbd_acct_coord_msg_t *msg, Buf buffer)
{
	char *acct = NULL;
	ListIterator itr = NULL;
	uint32_t count = 0;

	if(msg->acct_list)
		count = list_count(msg->acct_list);
	
	pack32(count, buffer);
	if(count) {
		itr = list_iterator_create(msg->acct_list);
		while((acct = list_next(itr))) {
			packstr(acct, buffer);
		}
		list_iterator_destroy(itr);
	}
	count = 0;

	pack_acct_user_cond(msg->cond, rpc_version, buffer);
}

int inline
slurmdbd_unpack_acct_coord_msg(uint16_t rpc_version, 
			       dbd_acct_coord_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	int i;
	char *acct = NULL;
	uint32_t count = 0;
	dbd_acct_coord_msg_t *msg_ptr = xmalloc(sizeof(dbd_acct_coord_msg_t));
	*msg = msg_ptr;

	safe_unpack32(&count, buffer);
	if(count) {
		msg_ptr->acct_list = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&acct, &uint32_tmp, buffer);
			list_append(msg_ptr->acct_list, acct);
		}
	}

	if(unpack_acct_user_cond((void *)&msg_ptr->cond, rpc_version, buffer) 
	   == SLURM_ERROR)
		goto unpack_error;
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_acct_coord_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline
slurmdbd_pack_cluster_procs_msg(uint16_t rpc_version, 
				dbd_cluster_procs_msg_t *msg, Buf buffer)
{
	packstr(msg->cluster_name, buffer);
	pack32(msg->proc_count,    buffer);
	pack_time(msg->event_time, buffer);
}

int inline
slurmdbd_unpack_cluster_procs_msg(uint16_t rpc_version,
				  dbd_cluster_procs_msg_t **msg, Buf buffer)
{
	dbd_cluster_procs_msg_t *msg_ptr;
	uint32_t uint32_tmp;

	msg_ptr = xmalloc(sizeof(dbd_cluster_procs_msg_t));
	*msg = msg_ptr;
	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->proc_count, buffer);
	safe_unpack_time(&msg_ptr->event_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_cluster_procs_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline slurmdbd_pack_cond_msg(uint16_t rpc_version, 
				   slurmdbd_msg_type_t type,
				   dbd_cond_msg_t *msg, Buf buffer)
{
	void (*my_function) (void *object, uint16_t rpc_version, Buf buffer);

	switch(type) {
	case DBD_GET_ACCOUNTS:
	case DBD_REMOVE_ACCOUNTS:
		my_function = pack_acct_account_cond;
		break;
	case DBD_GET_ASSOCS:
	case DBD_REMOVE_ASSOCS:
		my_function = pack_acct_association_cond;
		break;
	case DBD_GET_CLUSTERS:
	case DBD_REMOVE_CLUSTERS:
		my_function = pack_acct_cluster_cond;
		break;
	case DBD_GET_JOBS_COND:
		my_function = pack_acct_job_cond;
		break;
	case DBD_GET_QOS:
	case DBD_REMOVE_QOS:
		my_function = pack_acct_qos_cond;
		break;
	case DBD_GET_WCKEYS:
	case DBD_REMOVE_WCKEYS:
		my_function = pack_acct_wckey_cond;
		break;
	case DBD_GET_USERS:
	case DBD_REMOVE_USERS:
		my_function = pack_acct_user_cond;
		break;
	case DBD_GET_TXN:
		my_function = pack_acct_txn_cond;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}

	(*(my_function))(msg->cond, rpc_version, buffer);
}

int inline slurmdbd_unpack_cond_msg(uint16_t rpc_version, 
				    slurmdbd_msg_type_t type,
				    dbd_cond_msg_t **msg, Buf buffer)
{
	dbd_cond_msg_t *msg_ptr = NULL;
	int (*my_function) (void **object, uint16_t rpc_version, Buf buffer);

	switch(type) {
	case DBD_GET_ACCOUNTS:
	case DBD_REMOVE_ACCOUNTS:
		my_function = unpack_acct_account_cond;
		break;
	case DBD_GET_ASSOCS:
	case DBD_REMOVE_ASSOCS:
		my_function = unpack_acct_association_cond;
		break;
	case DBD_GET_CLUSTERS:
	case DBD_REMOVE_CLUSTERS:
		my_function = unpack_acct_cluster_cond;
		break;
	case DBD_GET_JOBS_COND:
		my_function = unpack_acct_job_cond;
		break;
	case DBD_GET_QOS:
	case DBD_REMOVE_QOS:
		my_function = unpack_acct_qos_cond;
		break;
	case DBD_GET_WCKEYS:
	case DBD_REMOVE_WCKEYS:
		my_function = unpack_acct_wckey_cond;
		break;
	case DBD_GET_USERS:
	case DBD_REMOVE_USERS:
		my_function = unpack_acct_user_cond;
		break;
	case DBD_GET_TXN:
		my_function = unpack_acct_txn_cond;
		break;
	default:
		fatal("Unknown unpack type");
		return SLURM_ERROR;
	}

	msg_ptr = xmalloc(sizeof(dbd_cond_msg_t));
	*msg = msg_ptr;

	if((*(my_function))(&msg_ptr->cond, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;
	
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_cond_msg(rpc_version, type, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline slurmdbd_pack_get_jobs_msg(uint16_t rpc_version,
				       dbd_get_jobs_msg_t *msg, Buf buffer)
{
	uint32_t i = 0;
	ListIterator itr = NULL;
	jobacct_selected_step_t *job = NULL;
	char *part = NULL;

	packstr(msg->cluster_name, buffer);

	pack16(msg->completion, buffer);

	pack32(msg->gid, buffer);

	pack_time(msg->last_update, buffer);

	if(msg->selected_steps) 
		i = list_count(msg->selected_steps);
			
	pack32(i, buffer);
	if(i) {
		itr = list_iterator_create(msg->selected_steps);
		while((job = list_next(itr))) {
			pack_jobacct_selected_step(job, rpc_version, buffer);
		}
		list_iterator_destroy(itr);
	}

	i = 0;
	if(msg->selected_parts) 
		i = list_count(msg->selected_parts);
			
	pack32(i, buffer);
	if(i) {
		itr = list_iterator_create(msg->selected_parts);
		while((part = list_next(itr))) {
			packstr(part, buffer);
		}
		list_iterator_destroy(itr);
	}
	packstr(msg->user, buffer);
}

int inline slurmdbd_unpack_get_jobs_msg(uint16_t rpc_version,
					dbd_get_jobs_msg_t **msg, Buf buffer)
{
	int i;
	uint32_t count = 0;
	uint32_t uint32_tmp;
	dbd_get_jobs_msg_t *msg_ptr;
	jobacct_selected_step_t *job = NULL;
	char *part = NULL;

	msg_ptr = xmalloc(sizeof(dbd_get_jobs_msg_t));
	*msg = msg_ptr;

	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &uint32_tmp, buffer);

	safe_unpack16(&msg_ptr->completion, buffer);

	safe_unpack32(&msg_ptr->gid, buffer);

	safe_unpack_time(&msg_ptr->last_update, buffer);

	safe_unpack32(&count, buffer);
	if(count) {
		msg_ptr->selected_steps =
			list_create(destroy_jobacct_selected_step);
		for(i=0; i<count; i++) {
			unpack_jobacct_selected_step(&job, rpc_version, buffer);
			list_append(msg_ptr->selected_steps, job);
		}
	}
	safe_unpack32(&count, buffer);
	if(count) {
		msg_ptr->selected_parts = list_create(slurm_destroy_char);
		for(i=0; i<count; i++) {
			safe_unpackstr_xmalloc(&part, &uint32_tmp, buffer);
			list_append(msg_ptr->selected_parts, part);
		}
	}

	safe_unpackstr_xmalloc(&msg_ptr->user, &uint32_tmp, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_get_jobs_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_init_msg(uint16_t rpc_version, dbd_init_msg_t *msg, 
		       Buf buffer, char *auth_info)
{
	int rc;
	void *auth_cred;

	pack16(msg->rollback, buffer);
	pack16(msg->version, buffer);
	auth_cred = g_slurm_auth_create(NULL, 2, auth_info);
	if (auth_cred == NULL) {
		error("Creating authentication credential: %s",
		      g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
	} else {
		rc = g_slurm_auth_pack(auth_cred, buffer);
		(void) g_slurm_auth_destroy(auth_cred);
		if (rc) {
			error("Packing authentication credential: %s",
			      g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		}
	}
}

int inline 
slurmdbd_unpack_init_msg(uint16_t rpc_version, dbd_init_msg_t **msg,
			 Buf buffer, char *auth_info)
{
	void *auth_cred;

	dbd_init_msg_t *msg_ptr = xmalloc(sizeof(dbd_init_msg_t));
	*msg = msg_ptr;
	int rc = SLURM_SUCCESS;
		
	safe_unpack16(&msg_ptr->rollback, buffer);
	safe_unpack16(&msg_ptr->version, buffer);
	auth_cred = g_slurm_auth_unpack(buffer);
	if (auth_cred == NULL) {
		error("Unpacking authentication credential: %s",
		      g_slurm_auth_errstr(g_slurm_auth_errno(NULL)));
		rc = ESLURM_ACCESS_DENIED;
		goto unpack_error;
	}
	msg_ptr->uid = g_slurm_auth_get_uid(auth_cred, auth_info);
	if(g_slurm_auth_errno(auth_cred) != SLURM_SUCCESS) {
		error("Bad authentication: %s",
		      g_slurm_auth_errstr(g_slurm_auth_errno(auth_cred)));
		rc = ESLURM_ACCESS_DENIED;
		goto unpack_error;
	}

	g_slurm_auth_destroy(auth_cred);
	return rc;

unpack_error:
	slurmdbd_free_init_msg(rpc_version, msg_ptr);
	*msg = NULL;
	if(rc == SLURM_SUCCESS)
		rc = SLURM_ERROR;
	return rc;
}

void inline 
slurmdbd_pack_fini_msg(uint16_t rpc_version, dbd_fini_msg_t *msg, Buf buffer)
{
	pack16(msg->close_conn, buffer);
	pack16(msg->commit, buffer);
}

int inline 
slurmdbd_unpack_fini_msg(uint16_t rpc_version, dbd_fini_msg_t **msg, Buf buffer)
{
	dbd_fini_msg_t *msg_ptr = xmalloc(sizeof(dbd_fini_msg_t));
	*msg = msg_ptr;

	safe_unpack16(&msg_ptr->close_conn, buffer);
	safe_unpack16(&msg_ptr->commit, buffer);

	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_fini_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_job_complete_msg(uint16_t rpc_version, 
			       dbd_job_comp_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack_time(msg->end_time, buffer);
	pack32(msg->exit_code, buffer);
	pack32(msg->job_id, buffer);
	pack16(msg->job_state, buffer);
	packstr(msg->nodes, buffer);
	pack_time(msg->start_time, buffer);
	pack_time(msg->submit_time, buffer);
}

int inline 
slurmdbd_unpack_job_complete_msg(uint16_t rpc_version,
				 dbd_job_comp_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_job_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_comp_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack_time(&msg_ptr->end_time, buffer);
	safe_unpack32(&msg_ptr->exit_code, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack16(&msg_ptr->job_state, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack_time(&msg_ptr->submit_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_job_complete_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_job_start_msg(uint16_t rpc_version, 
			    dbd_job_start_msg_t *msg, Buf buffer)
{
	if(rpc_version < 3) {
		packstr(msg->account, buffer);
		pack32(msg->alloc_cpus, buffer);
		pack32(msg->assoc_id, buffer);
		packstr(msg->block_id, buffer);
		pack32(msg->db_index, buffer);
		pack_time(msg->eligible_time, buffer);
		pack32(msg->gid, buffer);
		pack32(msg->job_id, buffer);
		pack16(msg->job_state, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		pack32(msg->req_cpus, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->submit_time, buffer);
		pack32(msg->uid, buffer);
	} else if(rpc_version >=3) {
		packstr(msg->account, buffer);
		pack32(msg->alloc_cpus, buffer);
		pack32(msg->assoc_id, buffer);
		packstr(msg->block_id, buffer);
		packstr(msg->cluster, buffer);
		pack32(msg->db_index, buffer);
		pack_time(msg->eligible_time, buffer);
		pack32(msg->gid, buffer);
		pack32(msg->job_id, buffer);
		pack16(msg->job_state, buffer);
		packstr(msg->name, buffer);
		packstr(msg->nodes, buffer);
		packstr(msg->partition, buffer);
		pack32(msg->priority, buffer);
		pack32(msg->req_cpus, buffer);
		pack_time(msg->start_time, buffer);
		pack_time(msg->submit_time, buffer);
		pack32(msg->uid, buffer);		
	}
}

int inline 
slurmdbd_unpack_job_start_msg(uint16_t rpc_version,
			      dbd_job_start_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_job_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_start_msg_t));
	*msg = msg_ptr;

	if(rpc_version < 3) {
		safe_unpackstr_xmalloc(&msg_ptr->account, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->alloc_cpus, buffer);
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->block_id, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->db_index, buffer);
		safe_unpack_time(&msg_ptr->eligible_time, buffer);
		safe_unpack32(&msg_ptr->gid, buffer);
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack16(&msg_ptr->job_state, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->priority, buffer);
		safe_unpack32(&msg_ptr->req_cpus, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->submit_time, buffer);
		safe_unpack32(&msg_ptr->uid, buffer);
	} else if(rpc_version >= 3) {
		safe_unpackstr_xmalloc(&msg_ptr->account, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->alloc_cpus, buffer);
		safe_unpack32(&msg_ptr->assoc_id, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->block_id, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->cluster, &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->db_index, buffer);
		safe_unpack_time(&msg_ptr->eligible_time, buffer);
		safe_unpack32(&msg_ptr->gid, buffer);
		safe_unpack32(&msg_ptr->job_id, buffer);
		safe_unpack16(&msg_ptr->job_state, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
		safe_unpackstr_xmalloc(&msg_ptr->partition,
				       &uint32_tmp, buffer);
		safe_unpack32(&msg_ptr->priority, buffer);
		safe_unpack32(&msg_ptr->req_cpus, buffer);
		safe_unpack_time(&msg_ptr->start_time, buffer);
		safe_unpack_time(&msg_ptr->submit_time, buffer);
		safe_unpack32(&msg_ptr->uid, buffer);	
	}
	
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_job_start_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_job_start_rc_msg(uint16_t rpc_version, 
			       dbd_job_start_rc_msg_t *msg, Buf buffer)
{
	pack32(msg->db_index, buffer);
	pack32(msg->return_code, buffer);
}

int inline 
slurmdbd_unpack_job_start_rc_msg(uint16_t rpc_version, 
				 dbd_job_start_rc_msg_t **msg, Buf buffer)
{
	dbd_job_start_rc_msg_t *msg_ptr = 
		xmalloc(sizeof(dbd_job_start_rc_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack32(&msg_ptr->return_code, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_job_start_rc_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_job_suspend_msg(uint16_t rpc_version,
			      dbd_job_suspend_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack32(msg->job_id, buffer);
	pack16(msg->job_state, buffer);
	pack_time(msg->submit_time, buffer);
	pack_time(msg->suspend_time, buffer);
}

int inline 
slurmdbd_unpack_job_suspend_msg(uint16_t rpc_version,
				dbd_job_suspend_msg_t **msg, Buf buffer)
{
	dbd_job_suspend_msg_t *msg_ptr = xmalloc(sizeof(dbd_job_suspend_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack16(&msg_ptr->job_state, buffer);
	safe_unpack_time(&msg_ptr->submit_time, buffer);
	safe_unpack_time(&msg_ptr->suspend_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_job_suspend_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline slurmdbd_pack_list_msg(uint16_t rpc_version,
				   slurmdbd_msg_type_t type,
				   dbd_list_msg_t *msg, Buf buffer)
{
	uint32_t count = 0;
	ListIterator itr = NULL;
	void *object = NULL;
	void (*my_function) (void *object, uint16_t rpc_version, Buf buffer);

	switch(type) {
	case DBD_ADD_ACCOUNTS:
	case DBD_GOT_ACCOUNTS:
		my_function = pack_acct_account_rec;
		break;
	case DBD_ADD_ASSOCS:
	case DBD_GOT_ASSOCS:
		my_function = pack_acct_association_rec;
		break;
	case DBD_ADD_CLUSTERS:
	case DBD_GOT_CLUSTERS:
		my_function = pack_acct_cluster_rec;
		break;
	case DBD_GOT_JOBS:
		my_function = pack_jobacct_job_rec;
		break;
	case DBD_GOT_LIST:
		my_function = _slurmdbd_packstr;
		break;
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
		my_function = pack_acct_qos_rec;
		break;
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
		my_function = pack_acct_wckey_rec;
		break;
	case DBD_ADD_USERS:
	case DBD_GOT_USERS:
		my_function = pack_acct_user_rec;
		break;
	case DBD_GOT_TXN:
		my_function = pack_acct_txn_rec;
		break;
	case DBD_UPDATE_SHARES_USED:
		my_function = pack_update_shares_used;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}
	if(msg->my_list) {
		count = list_count(msg->my_list);
		pack32(count, buffer); 
	} else {
		// to let user know there wasn't a list (error)
		pack32((uint32_t)-1, buffer); 
	}
	if(count) {
		itr = list_iterator_create(msg->my_list);
		while((object = list_next(itr))) {
			(*(my_function))(object, rpc_version, buffer);
		}
		list_iterator_destroy(itr);
	}
}

int inline slurmdbd_unpack_list_msg(uint16_t rpc_version, 
				    slurmdbd_msg_type_t type,
				    dbd_list_msg_t **msg, Buf buffer)
{
	int i;
	uint32_t count;
	dbd_list_msg_t *msg_ptr = NULL;
	void *object = NULL;
	int (*my_function) (void **object, uint16_t rpc_version, Buf buffer);
	void (*my_destroy) (void *object);

	switch(type) {
	case DBD_ADD_ACCOUNTS:
	case DBD_GOT_ACCOUNTS:
		my_function = unpack_acct_account_rec;
		my_destroy = destroy_acct_account_rec;
		break;
	case DBD_ADD_ASSOCS:
	case DBD_GOT_ASSOCS:
		my_function = unpack_acct_association_rec;
		my_destroy = destroy_acct_association_rec;
		break;
	case DBD_ADD_CLUSTERS:
	case DBD_GOT_CLUSTERS:
		my_function = unpack_acct_cluster_rec;
		my_destroy = destroy_acct_cluster_rec;
		break;
	case DBD_GOT_JOBS:
		my_function = unpack_jobacct_job_rec;
		my_destroy = destroy_jobacct_job_rec;
		break;
	case DBD_GOT_LIST:
		my_function = _slurmdbd_unpackstr;
		my_destroy = slurm_destroy_char;
		break;
	case DBD_ADD_QOS:
	case DBD_GOT_QOS:
		my_function = unpack_acct_qos_rec;
		my_destroy = destroy_acct_qos_rec;
		break;
	case DBD_ADD_WCKEYS:
	case DBD_GOT_WCKEYS:
		my_function = unpack_acct_wckey_rec;
		my_destroy = destroy_acct_wckey_rec;
		break;
	case DBD_ADD_USERS:
	case DBD_GOT_USERS:
		my_function = unpack_acct_user_rec;
		my_destroy = destroy_acct_user_rec;
		break;
	case DBD_GOT_TXN:
		my_function = unpack_acct_txn_rec;
		my_destroy = destroy_acct_txn_rec;
		break;
	case DBD_UPDATE_SHARES_USED:
		my_function = unpack_update_shares_used;
		my_destroy = destroy_update_shares_rec;
		break;
	default:
		fatal("Unknown unpack type");
		return SLURM_ERROR;
	}

	msg_ptr = xmalloc(sizeof(dbd_list_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&count, buffer);
	if((int)count > -1) {
		/* here we are looking to make the list if -1 or
		   higher than 0.  If -1 we don't want to have the
		   list be NULL meaning an error occured.
		*/
		msg_ptr->my_list = list_create((*(my_destroy)));
		for(i=0; i<count; i++) {
			if(((*(my_function))(&object, rpc_version, buffer))
			   == SLURM_ERROR)
				goto unpack_error;
			list_append(msg_ptr->my_list, object);
		}
	}
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_list_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline slurmdbd_pack_modify_msg(uint16_t rpc_version,
				     slurmdbd_msg_type_t type,
				     dbd_modify_msg_t *msg, Buf buffer)
{
	void (*my_cond) (void *object, uint16_t rpc_version, Buf buffer);
	void (*my_rec) (void *object, uint16_t rpc_version, Buf buffer);

	switch(type) {
	case DBD_MODIFY_ACCOUNTS:
		my_cond = pack_acct_account_cond;
		my_rec = pack_acct_account_rec;
		break;
	case DBD_MODIFY_ASSOCS:
		my_cond = pack_acct_association_cond;
		my_rec = pack_acct_association_rec;
		break;
	case DBD_MODIFY_CLUSTERS:
		my_cond = pack_acct_cluster_cond;
		my_rec = pack_acct_cluster_rec;
		break;
	case DBD_MODIFY_USERS:
		my_cond = pack_acct_user_cond;
		my_rec = pack_acct_user_rec;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}
	(*(my_cond))(msg->cond, rpc_version, buffer);
	(*(my_rec))(msg->rec, rpc_version, buffer);
}

int inline slurmdbd_unpack_modify_msg(uint16_t rpc_version, 
				      slurmdbd_msg_type_t type,
				      dbd_modify_msg_t **msg, Buf buffer)
{
	dbd_modify_msg_t *msg_ptr = NULL;
	int (*my_cond) (void **object, uint16_t rpc_version, Buf buffer);
	int (*my_rec) (void **object, uint16_t rpc_version, Buf buffer);

	msg_ptr = xmalloc(sizeof(dbd_modify_msg_t));
	*msg = msg_ptr;

	switch(type) {
	case DBD_MODIFY_ACCOUNTS:
		my_cond = unpack_acct_account_cond;
		my_rec = unpack_acct_account_rec;
		break;
	case DBD_MODIFY_ASSOCS:
		my_cond = unpack_acct_association_cond;
		my_rec = unpack_acct_association_rec;
		break;
	case DBD_MODIFY_CLUSTERS:
		my_cond = unpack_acct_cluster_cond;
		my_rec = unpack_acct_cluster_rec;
		break;
	case DBD_MODIFY_USERS:
		my_cond = unpack_acct_user_cond;
		my_rec = unpack_acct_user_rec;
		break;
	default:
		fatal("Unknown unpack type");
		return SLURM_ERROR;
	}

	if((*(my_cond))(&msg_ptr->cond, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;
	if((*(my_rec))(&msg_ptr->rec, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;
	
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_modify_msg(rpc_version, type, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_node_state_msg(uint16_t rpc_version,
			     dbd_node_state_msg_t *msg, Buf buffer)
{
	packstr(msg->cluster_name, buffer);
	pack32(msg->cpu_count, buffer);
	packstr(msg->hostlist, buffer);
	packstr(msg->reason, buffer);
	pack16(msg->new_state, buffer);
	pack_time(msg->event_time, buffer);
}

int inline
slurmdbd_unpack_node_state_msg(uint16_t rpc_version,
			       dbd_node_state_msg_t **msg, Buf buffer)
{
	dbd_node_state_msg_t *msg_ptr;
	uint32_t uint32_tmp;

	msg_ptr = xmalloc(sizeof(dbd_node_state_msg_t));
	*msg = msg_ptr;
	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->cpu_count, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->hostlist, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->reason,   &uint32_tmp, buffer);
	safe_unpack16(&msg_ptr->new_state, buffer);
	safe_unpack_time(&msg_ptr->event_time, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_node_state_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_rc_msg(uint16_t rpc_version,
		     dbd_rc_msg_t *msg, Buf buffer)
{
	packstr(msg->comment, buffer);
	pack32(msg->return_code, buffer);
	pack16(msg->sent_type, buffer);
}

int inline 
slurmdbd_unpack_rc_msg(uint16_t rpc_version,
		       dbd_rc_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_rc_msg_t *msg_ptr = xmalloc(sizeof(dbd_rc_msg_t));
	*msg = msg_ptr;
	safe_unpackstr_xmalloc(&msg_ptr->comment, &uint32_tmp, buffer);
	safe_unpack32(&msg_ptr->return_code, buffer);
	safe_unpack16(&msg_ptr->sent_type, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_rc_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_register_ctld_msg(uint16_t rpc_version,
				dbd_register_ctld_msg_t *msg, Buf buffer)
{
	packstr(msg->cluster_name, buffer);
	pack16(msg->port, buffer);
}

int inline 
slurmdbd_unpack_register_ctld_msg(uint16_t rpc_version,
				  dbd_register_ctld_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_register_ctld_msg_t *msg_ptr = xmalloc(
		sizeof(dbd_register_ctld_msg_t));
	*msg = msg_ptr;
	safe_unpackstr_xmalloc(&msg_ptr->cluster_name, &uint32_tmp, buffer);
	safe_unpack16(&msg_ptr->port, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_register_ctld_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_roll_usage_msg(uint16_t rpc_version,
			     dbd_roll_usage_msg_t *msg, Buf buffer)
{
	pack_time(msg->start, buffer);
}

int inline 
slurmdbd_unpack_roll_usage_msg(uint16_t rpc_version,
			       dbd_roll_usage_msg_t **msg, Buf buffer)
{
	dbd_roll_usage_msg_t *msg_ptr = xmalloc(sizeof(dbd_roll_usage_msg_t));

	*msg = msg_ptr;
	safe_unpack_time(&msg_ptr->start, buffer);
	return SLURM_SUCCESS;
	
unpack_error:
	slurmdbd_free_roll_usage_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_step_complete_msg(uint16_t rpc_version,
				dbd_step_comp_msg_t *msg, Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack_time(msg->end_time, buffer);
	pack32(msg->exit_code, buffer);
	jobacct_common_pack((struct jobacctinfo *)msg->jobacct, buffer);
	pack32(msg->job_id, buffer);
	pack32(msg->req_uid, buffer);
	pack_time(msg->start_time, buffer);
	pack_time(msg->job_submit_time, buffer);
	pack32(msg->step_id, buffer);
	pack32(msg->total_procs, buffer);
}

int inline 
slurmdbd_unpack_step_complete_msg(uint16_t rpc_version,
				  dbd_step_comp_msg_t **msg, Buf buffer)
{
	dbd_step_comp_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_comp_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack_time(&msg_ptr->end_time, buffer);
	safe_unpack32(&msg_ptr->exit_code, buffer);
	jobacct_common_unpack((struct jobacctinfo **)&msg_ptr->jobacct, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpack32(&msg_ptr->req_uid, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack_time(&msg_ptr->job_submit_time, buffer);
	safe_unpack32(&msg_ptr->step_id, buffer);
	safe_unpack32(&msg_ptr->total_procs, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_step_complete_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline 
slurmdbd_pack_step_start_msg(uint16_t rpc_version, dbd_step_start_msg_t *msg,
			     Buf buffer)
{
	pack32(msg->assoc_id, buffer);
	pack32(msg->db_index, buffer);
	pack32(msg->job_id, buffer);
	packstr(msg->name, buffer);
	packstr(msg->nodes, buffer);
	pack_time(msg->start_time, buffer);
	pack_time(msg->job_submit_time, buffer);
	pack32(msg->step_id, buffer);
	pack32(msg->total_procs, buffer);
}

int inline 
slurmdbd_unpack_step_start_msg(uint16_t rpc_version,
			       dbd_step_start_msg_t **msg, Buf buffer)
{
	uint32_t uint32_tmp;
	dbd_step_start_msg_t *msg_ptr = xmalloc(sizeof(dbd_step_start_msg_t));
	*msg = msg_ptr;
	safe_unpack32(&msg_ptr->assoc_id, buffer);
	safe_unpack32(&msg_ptr->db_index, buffer);
	safe_unpack32(&msg_ptr->job_id, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->name, &uint32_tmp, buffer);
	safe_unpackstr_xmalloc(&msg_ptr->nodes, &uint32_tmp, buffer);
	safe_unpack_time(&msg_ptr->start_time, buffer);
	safe_unpack_time(&msg_ptr->job_submit_time, buffer);
	safe_unpack32(&msg_ptr->step_id, buffer);
	safe_unpack32(&msg_ptr->total_procs, buffer);
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_step_start_msg(rpc_version, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

void inline slurmdbd_pack_usage_msg(uint16_t rpc_version,
				    slurmdbd_msg_type_t type,
				    dbd_usage_msg_t *msg, Buf buffer)
{
	void (*my_rec) (void *object, uint16_t rpc_version, Buf buffer);

	switch(type) {
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
		my_rec = pack_acct_association_rec;
		break;
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
		my_rec = pack_acct_cluster_rec;
		break;
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		my_rec = pack_acct_wckey_rec;
		break;
	default:
		fatal("Unknown pack type");
		return;
	}
	
	(*(my_rec))(msg->rec, rpc_version, buffer);
	pack_time(msg->start, buffer);
	pack_time(msg->end, buffer);
}

int inline slurmdbd_unpack_usage_msg(uint16_t rpc_version,
				     slurmdbd_msg_type_t type,
				     dbd_usage_msg_t **msg, Buf buffer)
{
	dbd_usage_msg_t *msg_ptr = NULL;
	int (*my_rec) (void **object, uint16_t rpc_version, Buf buffer);

	msg_ptr = xmalloc(sizeof(dbd_usage_msg_t));
	*msg = msg_ptr;

	switch(type) {
	case DBD_GET_ASSOC_USAGE:
	case DBD_GOT_ASSOC_USAGE:
		my_rec = unpack_acct_association_rec;
		break;
	case DBD_GET_CLUSTER_USAGE:
	case DBD_GOT_CLUSTER_USAGE:
		my_rec = unpack_acct_cluster_rec;
		break;
	case DBD_GET_WCKEY_USAGE:
	case DBD_GOT_WCKEY_USAGE:
		my_rec = unpack_acct_wckey_rec;
		break;
	default:
		fatal("Unknown pack type");
		return SLURM_ERROR;
	}

	if((*(my_rec))(&msg_ptr->rec, rpc_version, buffer) == SLURM_ERROR)
		goto unpack_error;

	unpack_time(&msg_ptr->start, buffer);
	unpack_time(&msg_ptr->end, buffer);

	
	return SLURM_SUCCESS;

unpack_error:
	slurmdbd_free_usage_msg(rpc_version, type, msg_ptr);
	*msg = NULL;
	return SLURM_ERROR;
}

