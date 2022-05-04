/****************************************************************************\
 *  dbd_conn.c - functions to manage the connection to the SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2011-2020 SchedMD LLC.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/slurm_xlator.h"
#include "src/common/slurmdbd_pack.h"
#include "src/slurmctld/trigger_mgr.h"
#include "slurmdbd_agent.h"

#define SLURMDBD_TIMEOUT	900	/* Seconds SlurmDBD for response */

static void _acct_full(void)
{
	if (running_in_slurmctld())
		trigger_primary_ctld_acct_full();
}

static void _dbd_fail(void)
{
	if (running_in_slurmctld())
		trigger_primary_dbd_fail();
}

static void _dbd_res_op(void)
{
	if (running_in_slurmctld())
		trigger_primary_dbd_res_op();
}

static void _db_fail(void)
{
	if (running_in_slurmctld())
		trigger_primary_db_fail();
}

static void _db_res_op(void)
{
	if (running_in_slurmctld())
		trigger_primary_db_res_op();
}

static int _connect_dbd_conn(slurm_persist_conn_t *pc)
{

	int rc;
	char *backup_host = NULL;

	xassert(pc);

	/* Only setup a backup host on a non ext_dbd connection */
	if (!(pc->flags & PERSIST_FLAG_EXT_DBD))
		backup_host =
			xstrdup(slurm_conf.accounting_storage_backup_host);
again:
	// A connection failure is only an error if backup dne or also fails
	if (backup_host)
		pc->flags |= PERSIST_FLAG_SUPPRESS_ERR;
	else
		pc->flags &= (~PERSIST_FLAG_SUPPRESS_ERR);

	pc->r_uid = SLURM_AUTH_UID_ANY;

	if (((rc = slurm_persist_conn_open(pc)) != SLURM_SUCCESS) &&
	    backup_host) {
		xfree(pc->rem_host);
		// Force the next error to display
		pc->comm_fail_time = 0;
		pc->rem_host = backup_host;
		backup_host = NULL;
		goto again;
	}

	xfree(backup_host);

	if (rc == SLURM_SUCCESS) {
		/*
		 * Increase SLURMDBD_TIMEOUT to wait as long as we need for a
		 * query to complete.
		 */
		pc->timeout = MAX(pc->timeout, SLURMDBD_TIMEOUT * 1000);
		(pc->trigger_callbacks.dbd_resumed)();
		(pc->trigger_callbacks.db_resumed)();
	}

	if (rc == SLURM_SUCCESS) {
		debug("Sent PersistInit msg");
		/* clear errno (checked after this for errors) */
		errno = 0;
	} else {
		if (rc == ESLURM_DB_CONNECTION)
			(pc->trigger_callbacks.db_fail)();
		slurm_persist_conn_close(pc);

		/* This means errno was already set correctly */
		if (rc != SLURM_ERROR)
			errno = rc;
		error("Sending PersistInit msg: %m");
	}

	return rc;
}

extern slurm_persist_conn_t *dbd_conn_open(uint16_t *persist_conn_flags,
					   char *cluster_name,
					   char *rem_host,
					   uint16_t rem_port)
{
	slurm_persist_conn_t *pc = xmalloc(sizeof(*pc));

	if (persist_conn_flags)
		pc->flags = *persist_conn_flags;
	pc->flags |= (PERSIST_FLAG_DBD | PERSIST_FLAG_RECONNECT);
	pc->persist_type = PERSIST_TYPE_DBD;
	if (cluster_name)
		pc->cluster_name = xstrdup(cluster_name);
	else
		pc->cluster_name = xstrdup(slurm_conf.cluster_name);
	pc->timeout = (slurm_conf.msg_timeout + 35) * 1000;
	if (rem_host)
		pc->rem_host = xstrdup(rem_host);
	else
		pc->rem_host = xstrdup(slurm_conf.accounting_storage_host);
	if (rem_port)
		pc->rem_port = rem_port;
	else
		pc->rem_port = slurm_conf.accounting_storage_port;
	pc->version = SLURM_PROTOCOL_VERSION;

	/* Initialize the callback pointers */
	pc->trigger_callbacks.acct_full = _acct_full;
	pc->trigger_callbacks.dbd_fail = _dbd_fail;
	pc->trigger_callbacks.dbd_resumed = _dbd_res_op;
	pc->trigger_callbacks.db_fail = _db_fail;
	pc->trigger_callbacks.db_resumed = _db_res_op;

	(void)_connect_dbd_conn(pc);

	if (persist_conn_flags)
		*persist_conn_flags = pc->flags;

	return pc;
}

extern int dbd_conn_check_and_reopen(slurm_persist_conn_t *pc)
{
	xassert(pc);

	if (pc && pc->fd >= 0) {
		debug("Attempt to re-open slurmdbd socket");
		/* clear errno (checked after this for errors) */
		errno = 0;
		return SLURM_SUCCESS;
	}

	/*
	 * Reset the rem_host just in case we were connected to the backup
	 * before.
	 */
	xfree(pc->rem_host);
	pc->rem_host = xstrdup(slurm_conf.accounting_storage_host);

	return _connect_dbd_conn(pc);
}

extern void dbd_conn_close(slurm_persist_conn_t **pc)
{
	int rc;
	Buf buffer;
	dbd_fini_msg_t req;

	if (!pc)
		return;

	/*
	 * Only send the FINI message if we haven't shutdown
	 * (i.e. not slurmctld)
	 */
	if (*(*pc)->shutdown) {
		log_flag(NET, "We are shutdown, not sending DB_FINI to %s:%u",
			 (*pc)->rem_host,
			 (*pc)->rem_port);
		return;
	}

	/* If the connection is already gone, we don't need to send a fini. */
	if (slurm_persist_conn_writeable(*pc) == -1) {
		log_flag(NET, "unable to send DB_FINI msg to %s:%u",
			 (*pc)->rem_host,
			 (*pc)->rem_port);
		return;
	}

	buffer = init_buf(1024);
	pack16((uint16_t) DBD_FINI, buffer);
	req.commit = 0;
	req.close_conn = 1;
	slurmdbd_pack_fini_msg(&req, SLURM_PROTOCOL_VERSION, buffer);

	rc = slurm_persist_send_msg(*pc, buffer);
	free_buf(buffer);

	log_flag(NET, "sent DB_FINI msg to %s:%u rc(%d):%s",
		 (*pc)->rem_host, (*pc)->rem_port,
		 rc, slurm_strerror(rc));

	slurm_persist_conn_destroy(*pc);
	*pc = NULL;
}

/*
 * Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code
 */
extern int dbd_conn_send_recv_direct(uint16_t rpc_version,
				     persist_msg_t *req,
				     persist_msg_t *resp)
{
	int rc = SLURM_SUCCESS;
	Buf buffer;
	slurm_persist_conn_t *use_conn = req->conn;

	xassert(req);
	xassert(resp);
	xassert(use_conn);

	if (use_conn->fd < 0) {
		/* The connection has been closed, reopen */
		rc = dbd_conn_check_and_reopen(use_conn);

		if (rc != SLURM_SUCCESS || (use_conn->fd < 0)) {
			rc = SLURM_ERROR;
			goto end_it;
		}
	}

	if (!(buffer = pack_slurmdbd_msg(req, rpc_version))) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	rc = slurm_persist_send_msg(use_conn, buffer);
	free_buf(buffer);
	if (rc != SLURM_SUCCESS) {
		error("Sending message type %s: %d: %s",
		      slurmdbd_msg_type_2_str(req->msg_type, 1), rc,
		      slurm_strerror(rc));
		goto end_it;
	}

	buffer = slurm_persist_recv_msg(use_conn);
	if (buffer == NULL) {
		error("Getting response to message type: %s",
		      slurmdbd_msg_type_2_str(req->msg_type, 1));
		rc = SLURM_ERROR;
		goto end_it;
	}

	rc = unpack_slurmdbd_msg(resp, rpc_version, buffer);
	/* check for the rc of the start job message */
	if (rc == SLURM_SUCCESS && resp->msg_type == DBD_ID_RC)
		rc = ((dbd_id_rc_msg_t *)resp->data)->return_code;

	free_buf(buffer);
end_it:

	log_flag(PROTOCOL, "msg_type:%s protocol_version:%hu return_code:%d response_msg_type:%s",
		 slurmdbd_msg_type_2_str(req->msg_type, 1),
		 rpc_version, rc, slurmdbd_msg_type_2_str(resp->msg_type, 1));

	return rc;
}

extern int dbd_conn_send_recv_rc_msg(uint16_t rpc_version,
				     persist_msg_t *req,
				     int *resp_code)
{
	int rc;
	persist_msg_t resp;

	xassert(req);
	xassert(resp_code);

	memset(&resp, 0, sizeof(persist_msg_t));
	rc = dbd_conn_send_recv(rpc_version, req, &resp);
	if (rc != SLURM_SUCCESS) {
		;	/* error message already sent */
	} else if (resp.msg_type != PERSIST_RC) {
		error("response is not type PERSIST_RC: %s(%u)",
		      slurmdbd_msg_type_2_str(resp.msg_type, 1),
		      resp.msg_type);
		rc = SLURM_ERROR;
	} else {	/* resp.msg_type == PERSIST_RC */
		persist_rc_msg_t *msg = resp.data;
		*resp_code = msg->rc;
		if (msg->rc != SLURM_SUCCESS &&
		    msg->rc != ACCOUNTING_FIRST_REG &&
		    msg->rc != ACCOUNTING_TRES_CHANGE_DB &&
		    msg->rc != ACCOUNTING_NODES_CHANGE_DB) {
			char *comment = msg->comment;
			if (!comment)
				comment = slurm_strerror(msg->rc);
			if (!req->conn &&
			    (msg->ret_info == DBD_REGISTER_CTLD) &&
			    slurm_conf.accounting_storage_enforce) {
				error("Issue with call "
				      "%s(%u): %u(%s)",
				      slurmdbd_msg_type_2_str(
					      msg->ret_info, 1),
				      msg->ret_info, msg->rc,
				      comment);
				fatal("You need to add this cluster "
				      "to accounting if you want to "
				      "enforce associations, or no "
				      "jobs will ever run.");
			} else
				debug("Issue with call "
				      "%s(%u): %u(%s)",
				      slurmdbd_msg_type_2_str(
					      msg->ret_info, 1),
				      msg->ret_info, msg->rc,
				      comment);
		}
		slurm_persist_free_rc_msg(msg);
	}

	log_flag(PROTOCOL, "msg_type:%s protocol_version:%hu return_code:%d",
		 slurmdbd_msg_type_2_str(req->msg_type, 1),
		 rpc_version, rc);

	return rc;
}

extern int dbd_conn_send_recv(uint16_t rpc_version,
			      persist_msg_t *req,
			      persist_msg_t *resp)
{
	if (running_in_slurmctld() &&
	    (!req->conn || (req->conn == slurmdbd_conn)))
		return slurmdbd_agent_send_recv(rpc_version, req, resp);
	else
		return dbd_conn_send_recv_direct(rpc_version, req, resp);
}
