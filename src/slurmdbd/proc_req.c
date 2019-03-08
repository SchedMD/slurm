/*****************************************************************************\
 *  proc_req.c - functions for processing incoming RPCs.
 *****************************************************************************
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, Danny Auble <da@llnl.gov>
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

#include "config.h"

#include <signal.h>

#if HAVE_SYS_PRCTL_H
  #include <sys/prctl.h>
#endif

#include "src/common/slurm_auth.h"
#include "src/common/gres.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmdbd/proc_req.h"
#include "src/slurmdbd/slurmdbd.h"
#include "src/slurmctld/slurmctld.h"

/* Local functions */
static bool  _validate_slurm_user(uint32_t uid);
static bool  _validate_super_user(uint32_t uid, slurmdbd_conn_t *slurmdbd_conn);
static bool  _validate_operator(uint32_t uid, slurmdbd_conn_t *slurmdbd_conn);
static int   _unpack_persist_init(slurmdbd_conn_t *slurmdbd_conn,
				  persist_msg_t *msg, Buf *out_buffer,
				  uint32_t *uid);
static int   _add_accounts(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_account_coords(slurmdbd_conn_t *slurmdbd_conn,
				 persist_msg_t *msg, Buf *out_buffer,
				 uint32_t *uid);
static int   _add_tres(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_assocs(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_clusters(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_federations(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg,
			      Buf *out_buffer, uint32_t *uid);
static int   _add_qos(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_res(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_users(slurmdbd_conn_t *slurmdbd_conn,
			persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _add_reservation(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer,
			      uint32_t *uid);
static int   _archive_dump(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _archive_load(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _clear_stats(slurmdbd_conn_t *slurmdbd_conn,
			  persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _cluster_tres(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _fix_runaway_jobs(slurmdbd_conn_t *slurmdbd_conn,
			       persist_msg_t *msg,
			       Buf *out_buffer, uint32_t *uid);
static int   _get_accounts(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_tres(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_assocs(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_clusters(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_federations(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg,
			      Buf *out_buffer, uint32_t *uid);
static int   _get_config(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_events(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_jobs_cond(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _get_probs(slurmdbd_conn_t *slurmdbd_conn,
			persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_qos(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_res(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_stats(slurmdbd_conn_t *slurmdbd_conn,
		        persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_txn(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_usage(slurmdbd_conn_t *slurmdbd_conn,
			persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_users(slurmdbd_conn_t *slurmdbd_conn,
			persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _get_reservations(slurmdbd_conn_t *slurmdbd_conn,
			       persist_msg_t *msg, Buf *out_buffer,
			       uint32_t *uid);
static int   _flush_jobs(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _init_conn(slurmdbd_conn_t *slurmdbd_conn,
			persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _fini_conn(slurmdbd_conn_t *slurmdbd_conn, persist_msg_t *msg,
			Buf *out_buffer);
static int   _job_complete(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _job_start(slurmdbd_conn_t *slurmdbd_conn,
			persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _job_suspend(slurmdbd_conn_t *slurmdbd_conn,
			  persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _modify_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer,
			      uint32_t *uid);
static int   _modify_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _modify_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer,
			      uint32_t *uid);
static int   _modify_federations(slurmdbd_conn_t *slurmdbd_conn,
				 persist_msg_t *msg,
				 Buf *out_buffer, uint32_t *uid);
static int   _modify_job(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _modify_qos(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _modify_res(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _modify_users(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _modify_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _modify_reservation(slurmdbd_conn_t *slurmdbd_conn,
				 persist_msg_t *msg, Buf *out_buffer,
				 uint32_t *uid);
static int   _node_state(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static void  _process_job_start(slurmdbd_conn_t *slurmdbd_conn,
				dbd_job_start_msg_t *job_start_msg,
				dbd_id_rc_msg_t *id_rc_msg);
static int   _reconfig(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _register_ctld(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _remove_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer,
			      uint32_t *uid);
static int   _remove_account_coords(slurmdbd_conn_t *slurmdbd_conn,
				    persist_msg_t *msg, Buf *out_buffer,
				    uint32_t *uid);
static int   _remove_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _remove_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer,
			      uint32_t *uid);
static int   _remove_federations(slurmdbd_conn_t *slurmdbd_conn,
				 persist_msg_t *msg,
				 Buf *out_buffer, uint32_t *uid);
static int   _remove_qos(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _remove_res(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _remove_users(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _remove_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _remove_reservation(slurmdbd_conn_t *slurmdbd_conn,
				 persist_msg_t *msg, Buf *out_buffer,
				 uint32_t *uid);
static int   _roll_usage(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _send_mult_job_start(slurmdbd_conn_t *slurmdbd_conn,
				  persist_msg_t *msg, Buf *out_buffer,
				  uint32_t *uid);
static int   _send_mult_msg(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _shutdown(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);
static int   _step_complete(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid);
static int   _step_start(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid);

#ifndef NDEBUG
/*
 * Used alongside the testsuite to signal that the RPC should be processed
 * as an untrusted user, rather than the "real" account. (Which in a lot of
 * testing is likely SlurmUser, and thus allowed to bypass many security
 * checks.
 *
 * Implemented with a thread-local variable to apply only to the current
 * RPC handling thread. Set by SLURM_DROP_PRIV bit in the slurm_msg_t flags.
 */
__thread bool drop_priv = false;
#endif

/* Process an incoming RPC
 * slurmdbd_conn IN/OUT - in will that the conn.fd set before
 *       calling and db_conn and conn.version will be filled in with the init.
 * msg IN - incoming message
 * msg_size IN - size of msg in bytes
 * first IN - set if first message received on the socket
 * buffer OUT - outgoing response, must be freed by caller
 * uid IN/OUT - user ID who initiated the RPC
 * RET SLURM_SUCCESS or error code */
extern int
proc_req(void *conn, persist_msg_t *msg,
	 Buf *out_buffer, uint32_t *uid)
{
	slurmdbd_conn_t *slurmdbd_conn = conn;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;
	int i, rpc_type_index = -1, rpc_user_index = -1;

	DEF_TIMERS;
	START_TIMER;

	switch (msg->msg_type) {
	case REQUEST_PERSIST_INIT:
		rc = _unpack_persist_init(
			slurmdbd_conn, msg, out_buffer, uid);
		break;
	case DBD_ADD_ACCOUNTS:
		rc = _add_accounts(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_ADD_ACCOUNT_COORDS:
		rc = _add_account_coords(slurmdbd_conn,
					 msg, out_buffer, uid);
		break;
	case DBD_ADD_TRES:
		rc = _add_tres(slurmdbd_conn,
			       msg, out_buffer, uid);
		break;
	case DBD_ADD_ASSOCS:
		rc = _add_assocs(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_ADD_CLUSTERS:
		rc = _add_clusters(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_ADD_FEDERATIONS:
		rc = _add_federations(slurmdbd_conn, msg,
				      out_buffer, uid);
		break;
	case DBD_ADD_QOS:
		rc = _add_qos(slurmdbd_conn,
			      msg, out_buffer, uid);
		break;
	case DBD_ADD_RES:
		rc = _add_res(slurmdbd_conn,
			      msg, out_buffer, uid);
		break;
	case DBD_ADD_USERS:
		rc = _add_users(slurmdbd_conn,
				msg, out_buffer, uid);
		break;
	case DBD_ADD_WCKEYS:
		rc = _add_wckeys(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_ADD_RESV:
		rc = _add_reservation(slurmdbd_conn,
				      msg, out_buffer, uid);
		break;
	case DBD_ARCHIVE_DUMP:
		rc = _archive_dump(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_ARCHIVE_LOAD:
		rc = _archive_load(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_CLUSTER_TRES:
		rc = _cluster_tres(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_GET_ACCOUNTS:
		rc = _get_accounts(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_GET_TRES:
		rc = _get_tres(slurmdbd_conn,
			       msg, out_buffer, uid);
		break;
	case DBD_GET_ASSOCS:
		rc = _get_assocs(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_GET_ASSOC_USAGE:
	case DBD_GET_WCKEY_USAGE:
	case DBD_GET_CLUSTER_USAGE:
		rc = _get_usage(slurmdbd_conn,
				msg, out_buffer, uid);
		break;
	case DBD_GET_CLUSTERS:
		rc = _get_clusters(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_GET_FEDERATIONS:
		rc = _get_federations(slurmdbd_conn, msg,
				      out_buffer, uid);
		break;
	case DBD_GET_CONFIG:
		rc = _get_config(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_GET_EVENTS:
		rc = _get_events(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_GET_JOBS_COND:
		rc = _get_jobs_cond(slurmdbd_conn,
				    msg, out_buffer, uid);
		break;
	case DBD_GET_PROBS:
		rc = _get_probs(slurmdbd_conn,
				msg, out_buffer, uid);
		break;
	case DBD_GET_QOS:
		rc = _get_qos(slurmdbd_conn,
			      msg, out_buffer, uid);
		break;
	case DBD_GET_RES:
		rc = _get_res(slurmdbd_conn,
			      msg, out_buffer, uid);
		break;
	case DBD_GET_TXN:
		rc = _get_txn(slurmdbd_conn,
			      msg, out_buffer, uid);
		break;
	case DBD_GET_WCKEYS:
		rc = _get_wckeys(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_GET_RESVS:
		rc = _get_reservations(slurmdbd_conn,
				       msg, out_buffer, uid);
		break;
	case DBD_GET_USERS:
		rc = _get_users(slurmdbd_conn,
				msg, out_buffer, uid);
		break;
	case DBD_FLUSH_JOBS:
		rc = _flush_jobs(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_INIT:
		rc = _init_conn(slurmdbd_conn, msg, out_buffer, uid);
		break;
	case DBD_FINI:
		rc = _fini_conn(slurmdbd_conn, msg, out_buffer);
		break;
	case DBD_JOB_COMPLETE:
		rc = _job_complete(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_JOB_START:
		rc = _job_start(slurmdbd_conn,
				msg, out_buffer, uid);
		break;
	case DBD_JOB_SUSPEND:
		rc = _job_suspend(slurmdbd_conn,
				  msg, out_buffer, uid);
		break;
	case DBD_MODIFY_ACCOUNTS:
		rc = _modify_accounts(slurmdbd_conn,
				      msg, out_buffer, uid);
		break;
	case DBD_MODIFY_ASSOCS:
		rc = _modify_assocs(slurmdbd_conn,
				    msg, out_buffer, uid);
		break;
	case DBD_MODIFY_CLUSTERS:
		rc = _modify_clusters(slurmdbd_conn,
				      msg, out_buffer, uid);
		break;
	case DBD_MODIFY_FEDERATIONS:
		rc = _modify_federations(slurmdbd_conn, msg,
					 out_buffer, uid);
		break;
	case DBD_MODIFY_JOB:
		rc = _modify_job(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_MODIFY_QOS:
		rc = _modify_qos(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_MODIFY_RES:
		rc = _modify_res(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_MODIFY_USERS:
		rc = _modify_users(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_MODIFY_WCKEYS:
		rc = _modify_wckeys(slurmdbd_conn,
				    msg, out_buffer, uid);
		break;
	case DBD_MODIFY_RESV:
		rc = _modify_reservation(slurmdbd_conn,
					 msg, out_buffer, uid);
		break;
	case DBD_NODE_STATE:
		rc = _node_state(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_RECONFIG:
		/* handle reconfig */
		rc = _reconfig(slurmdbd_conn,
			       msg, out_buffer, uid);
		break;
	case DBD_REGISTER_CTLD:
		rc = _register_ctld(slurmdbd_conn, msg,
				    out_buffer, uid);
		break;
	case DBD_REMOVE_ACCOUNTS:
		rc = _remove_accounts(slurmdbd_conn,
				      msg, out_buffer, uid);
		break;
	case DBD_REMOVE_ACCOUNT_COORDS:
		rc = _remove_account_coords(slurmdbd_conn,
					    msg, out_buffer, uid);
		break;
	case DBD_REMOVE_ASSOCS:
		rc = _remove_assocs(slurmdbd_conn,
				    msg, out_buffer, uid);
		break;
	case DBD_REMOVE_CLUSTERS:
		rc = _remove_clusters(slurmdbd_conn,
				      msg, out_buffer, uid);
		break;
	case DBD_REMOVE_FEDERATIONS:
		rc = _remove_federations(slurmdbd_conn, msg,
					 out_buffer, uid);
		break;
	case DBD_REMOVE_QOS:
		rc = _remove_qos(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_REMOVE_RES:
		rc = _remove_res(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_REMOVE_USERS:
		rc = _remove_users(slurmdbd_conn,
				   msg, out_buffer, uid);
		break;
	case DBD_REMOVE_WCKEYS:
		rc = _remove_wckeys(slurmdbd_conn,
				    msg, out_buffer, uid);
		break;
	case DBD_REMOVE_RESV:
		rc = _remove_reservation(slurmdbd_conn,
					 msg, out_buffer, uid);
		break;
	case DBD_ROLL_USAGE:
		rc = _roll_usage(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_SEND_MULT_JOB_START:
		rc = _send_mult_job_start(slurmdbd_conn,
					  msg, out_buffer, uid);
		break;
	case DBD_SEND_MULT_MSG:
		rc = _send_mult_msg(slurmdbd_conn,
				    msg, out_buffer, uid);
		break;
	case DBD_STEP_COMPLETE:
		rc = _step_complete(slurmdbd_conn,
				    msg, out_buffer, uid);
		break;
	case DBD_STEP_START:
		rc = _step_start(slurmdbd_conn,
				 msg, out_buffer, uid);
		break;
	case DBD_FIX_RUNAWAY_JOB:
		rc = _fix_runaway_jobs(slurmdbd_conn,
				       msg, out_buffer, uid);
		break;
	case DBD_GET_STATS:
		rc = _get_stats(slurmdbd_conn, msg, out_buffer,
				uid);
		break;
	case DBD_CLEAR_STATS:
		rc = _clear_stats(slurmdbd_conn, msg, out_buffer,
				  uid);
		break;
	case DBD_SHUTDOWN:
		rc = _shutdown(slurmdbd_conn, msg, out_buffer,
			       uid);
		break;
	default:
		comment = "Invalid RPC";
		error("CONN:%u %s msg_type=%d",
		      slurmdbd_conn->conn->fd, comment, msg->msg_type);
		rc = EINVAL;
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, 0);
		break;
	}

	if (rc == ESLURM_ACCESS_DENIED)
		error("CONN:%u Security violation, %s",
		      slurmdbd_conn->conn->fd,
		      slurmdbd_msg_type_2_str(msg->msg_type, 1));
	else if (slurmdbd_conn->conn->rem_port
		 && !slurmdbd_conf->commit_delay) {
		/* If we are dealing with the slurmctld do the
		   commit (SUCCESS or NOT) afterwards since we
		   do transactions for performance reasons.
		   (don't ever use autocommit with innodb)
		*/
		acct_storage_g_commit(slurmdbd_conn->db_conn, 1);
	}

	END_TIMER;

	slurm_mutex_lock(&rpc_mutex);
	for (i = 0; i < rpc_stats.type_cnt; i++) {
		if (rpc_stats.rpc_type_id[i] == 0)
			rpc_stats.rpc_type_id[i] = msg->msg_type;
		else if (rpc_stats.rpc_type_id[i] != msg->msg_type)
			continue;
		rpc_type_index = i;
		break;
	}

	for (i = 0; i < rpc_stats.user_cnt; i++) {
		if ((rpc_stats.rpc_user_id[i] == 0) && (i != 0))
			rpc_stats.rpc_user_id[i] = *uid;
		else if (rpc_stats.rpc_user_id[i] != *uid)
			continue;
		rpc_user_index = i;
		break;
	}

	if (rpc_type_index >= 0) {
		rpc_stats.rpc_type_cnt[rpc_type_index]++;
		rpc_stats.rpc_type_time[rpc_type_index] += DELTA_TIMER;
	}
	if (rpc_user_index >= 0) {
		rpc_stats.rpc_user_cnt[rpc_user_index]++;
		rpc_stats.rpc_user_time[rpc_user_index] += DELTA_TIMER;
	}
	slurm_mutex_unlock(&rpc_mutex);

	return rc;
}

/*
 * _validate_slurm_user - validate that the uid is authorized to see
 *      privileged data (either user root or SlurmUser)
 */
static bool _validate_slurm_user(uint32_t uid)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurmdbd_conf->slurm_user_id))
		return true;

	return false;
}

/*
 * _validate_super_user - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_SUPER_USER level
 */
static bool _validate_super_user(uint32_t uid, slurmdbd_conn_t *dbd_conn)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurmdbd_conf->slurm_user_id) ||
	    assoc_mgr_get_admin_level(dbd_conn, uid) >= SLURMDB_ADMIN_SUPER_USER)
		return true;

	return false;
}

/*
 * _validate_operator - validate that the uid is authorized at the
 *      root, SlurmUser, or SLURMDB_ADMIN_OPERATOR level
 */
static bool _validate_operator(uint32_t uid, slurmdbd_conn_t *dbd_conn)
{
#ifndef NDEBUG
	if (drop_priv)
		return false;
#endif
	if ((uid == 0) || (uid == slurmdbd_conf->slurm_user_id) ||
	    assoc_mgr_get_admin_level(dbd_conn, uid) >= SLURMDB_ADMIN_OPERATOR)
		return true;

	return false;
}

static void _add_registered_cluster(slurmdbd_conn_t *db_conn)
{
	ListIterator itr;
	slurmdbd_conn_t *slurmdbd_conn;

	slurm_mutex_lock(&registered_lock);
	itr = list_iterator_create(registered_clusters);
	while ((slurmdbd_conn = list_next(itr))) {
		if (db_conn == slurmdbd_conn)
			break;
	}
	list_iterator_destroy(itr);
	if (!slurmdbd_conn)
		list_append(registered_clusters, db_conn);
	slurm_mutex_unlock(&registered_lock);
}


/* replace \" with \` return is the same as what is given */
static char * _replace_double_quotes(char *option)
{
	int i=0;

	if (!option)
		return NULL;

	while (option[i]) {
		if (option[i] == '\"')
			option[i] = '`';
		i++;
	}
	return option;
}

static int _handle_init_msg(slurmdbd_conn_t *slurmdbd_conn,
			    persist_init_req_msg_t *init_msg,
			    uint32_t *uid)
{
	int rc = SLURM_SUCCESS;

	*uid = init_msg->uid;

#if HAVE_SYS_PRCTL_H
	{
	char *name = xstrdup_printf("p-%s", init_msg->cluster_name);
	if (prctl(PR_SET_NAME, name, NULL, NULL, NULL) < 0)
		error("%s: cannot set my name to %s %m", __func__, name);
	xfree(name);
	}
#endif

	debug("REQUEST_PERSIST_INIT: CLUSTER:%s VERSION:%u UID:%u IP:%s CONN:%u",
	      init_msg->cluster_name, init_msg->version, init_msg->uid,
	      slurmdbd_conn->conn->rem_host, slurmdbd_conn->conn->fd);

	slurmdbd_conn->conn->cluster_name = xstrdup(init_msg->cluster_name);

	/* When dealing with rollbacks it turns out it is much faster
	   to do the commit once or once in a while instead of
	   autocommit.  The SlurmDBD will periodically do a commit to
	   avoid such a slow down.
	*/
	slurmdbd_conn->db_conn = acct_storage_g_get_connection(
		NULL, slurmdbd_conn->conn->fd, NULL, true,
		slurmdbd_conn->conn->cluster_name);
	slurmdbd_conn->conn->version = init_msg->version;
	if (errno)
		rc = errno;

	return rc;
}

static int _unpack_persist_init(slurmdbd_conn_t *slurmdbd_conn,
				persist_msg_t *msg, Buf *out_buffer,
				uint32_t *uid)
{
	int rc;
	slurm_msg_t *smsg = msg->data;
	persist_init_req_msg_t *req_msg = smsg->data;
	char *comment = NULL;

#ifndef NDEBUG
	if ((smsg->flags & SLURM_DROP_PRIV))
		drop_priv = true;
#endif

	req_msg->uid = g_slurm_auth_get_uid(slurmdbd_conn->conn->auth_cred);

	/* If the client happens to be a newer version than we are make it so
	 * they talk language I understand.
	 */
	if (req_msg->version > SLURM_PROTOCOL_VERSION)
		req_msg->version = SLURM_PROTOCOL_VERSION;

	rc = _handle_init_msg(slurmdbd_conn, req_msg, uid);

	if (rc != SLURM_SUCCESS)
		comment = slurm_strerror(rc);

	*out_buffer = slurm_persist_make_rc_msg_flags(
		slurmdbd_conn->conn, rc, comment,
		slurmdbd_conf->persist_conn_rc_flags,
		req_msg->version);

	return rc;
}

static int _add_accounts(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_ACCOUNTS: called");

	rc = acct_storage_g_add_accounts(slurmdbd_conn->db_conn, *uid,
					 get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = "Your user doesn't have privilege to perform this action";
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_ACCOUNTS);
	return rc;
}

static int _fix_runaway_jobs(slurmdbd_conn_t *slurmdbd_conn, persist_msg_t *msg,
			     Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	rc = acct_storage_g_fix_runaway_jobs(slurmdbd_conn->db_conn, *uid,
					     get_msg->my_list);

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment,
						DBD_FIX_RUNAWAY_JOB);

	return rc;
}

static int _add_account_coords(slurmdbd_conn_t *slurmdbd_conn,
			       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_acct_coord_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_ACCOUNT_COORDS: called");

	rc = acct_storage_g_add_coord(slurmdbd_conn->db_conn, *uid,
				      get_msg->acct_list, get_msg->cond);

	if (rc == ESLURM_ACCESS_DENIED) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
	}

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment,
						DBD_ADD_ACCOUNT_COORDS);
	return rc;
}

static int _add_tres(slurmdbd_conn_t *slurmdbd_conn,
		     persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_TRES: called");

	rc = acct_storage_g_add_tres(slurmdbd_conn->db_conn, *uid,
				     get_msg->my_list);

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_TRES);

	/* This happens before the slurmctld registers and only when
	   the slurmctld starts up.  So always commit, success or not.
	   (don't ever use autocommit with innodb)
	*/
	acct_storage_g_commit(slurmdbd_conn->db_conn, 1);

	return rc;
}

static int _add_assocs(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_ASSOCS: called");

	if (!_validate_operator(*uid, slurmdbd_conn)) {
		ListIterator itr = NULL;
		ListIterator itr2 = NULL;
		slurmdb_user_rec_t user;
		slurmdb_coord_rec_t *coord = NULL;
		slurmdb_assoc_rec_t *object = NULL;

		memset(&user, 0, sizeof(slurmdb_user_rec_t));
		user.uid = *uid;
		if (assoc_mgr_fill_in_user(
			    slurmdbd_conn->db_conn, &user, 1, NULL, false)
		    != SLURM_SUCCESS) {
			comment = "Your user has not been added to the accounting system yet.";
			error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
			rc = SLURM_ERROR;
			goto end_it;
		}
		if (!user.coord_accts || !list_count(user.coord_accts)) {
			comment = "Your user doesn't have privilege to perform this action";
			error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;
		}
		itr = list_iterator_create(get_msg->my_list);
		itr2 = list_iterator_create(user.coord_accts);
		while ((object = list_next(itr))) {
			char *account = "root";
			if (object->user)
				account = object->acct;
			else if (object->parent_acct)
				account = object->parent_acct;
			list_iterator_reset(itr2);
			while ((coord = list_next(itr2))) {
				if (!xstrcasecmp(coord->name, account))
					break;
			}
			if (!coord)
				break;
		}
		list_iterator_destroy(itr2);
		list_iterator_destroy(itr);
		if (!coord)  {
			comment = "Your user doesn't have privilege to perform this action";
			error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
			rc = ESLURM_ACCESS_DENIED;
			goto end_it;
		}
	}

	rc = acct_storage_g_add_assocs(slurmdbd_conn->db_conn, *uid,
				       get_msg->my_list);
end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_ASSOCS);
	return rc;
}

static int _add_clusters(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_CLUSTERS: called");

	rc = acct_storage_g_add_clusters(slurmdbd_conn->db_conn, *uid,
					 get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = "Your user doesn't have privilege to perform this action";
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add cluster.";

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_CLUSTERS);
	return rc;
}

static int _add_federations(slurmdbd_conn_t *slurmdbd_conn, persist_msg_t *msg,
			    Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_FEDERATIONS: called");

	rc = acct_storage_g_add_federations(slurmdbd_conn->db_conn, *uid,
					    get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = "Your user doesn't have privilege to perform this "
			"action";
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add cluster.";

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment,
						DBD_ADD_FEDERATIONS);
	return rc;
}

static int _add_qos(slurmdbd_conn_t *slurmdbd_conn,
		    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_QOS: called");

	rc = acct_storage_g_add_qos(slurmdbd_conn->db_conn, *uid,
				    get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = "Your user doesn't have privilege to perform this action";
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add qos.";

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_QOS);
	return rc;
}

static int _add_res(slurmdbd_conn_t *slurmdbd_conn,
		    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_RES: called");

	rc = acct_storage_g_add_res(slurmdbd_conn->db_conn, *uid,
				    get_msg->my_list);
	if (rc == ESLURM_ACCESS_DENIED)
		comment = "Your user doesn't have privilege to perform this action";
	else if (rc != SLURM_SUCCESS)
		comment = "Failed to add system resource.";

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_RES);
	return rc;
}

static int _add_users(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;
	debug2("DBD_ADD_USERS: called");

	rc = acct_storage_g_add_users(slurmdbd_conn->db_conn, *uid,
				      get_msg->my_list);

	if (rc == ESLURM_ACCESS_DENIED)
		comment = "Your user doesn't have privilege to perform this action";

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_USERS);
	return rc;
}

static int _add_wckeys(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_list_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_ADD_WCKEYS: called");

	rc = acct_storage_g_add_wckeys(slurmdbd_conn->db_conn, *uid,
				       get_msg->my_list);

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_WCKEYS);
	return rc;
}

static int _add_reservation(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = msg->data;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_ADD_RESV message from invalid uid";
		error("DBD_ADD_RESV message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_ADD_RESV: called");

	rc = acct_storage_g_add_reservation(slurmdbd_conn->db_conn,
					    rec_msg->rec);

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ADD_RESV);
	return rc;
}

static int _archive_dump(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	char *comment = "SUCCESS";
	slurmdb_archive_cond_t *arch_cond = NULL;

	debug2("DBD_ARCHIVE_DUMP: called");
	if (!_validate_super_user(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	arch_cond = (slurmdb_archive_cond_t *)get_msg->cond;
	/* set up some defaults */
	if (!arch_cond->archive_dir)
		arch_cond->archive_dir = xstrdup(slurmdbd_conf->archive_dir);
	if (!arch_cond->archive_script)
		arch_cond->archive_script =
			xstrdup(slurmdbd_conf->archive_script);

	if (arch_cond->purge_event == NO_VAL)
		arch_cond->purge_event = slurmdbd_conf->purge_event;
	if (arch_cond->purge_job == NO_VAL)
		arch_cond->purge_job = slurmdbd_conf->purge_job;
	if (arch_cond->purge_resv == NO_VAL)
		arch_cond->purge_resv = slurmdbd_conf->purge_resv;
	if (arch_cond->purge_step == NO_VAL)
		arch_cond->purge_step = slurmdbd_conf->purge_step;
	if (arch_cond->purge_suspend == NO_VAL)
		arch_cond->purge_suspend = slurmdbd_conf->purge_suspend;
	if (arch_cond->purge_txn == NO_VAL)
		arch_cond->purge_txn = slurmdbd_conf->purge_txn;
	if (arch_cond->purge_usage == NO_VAL)
		arch_cond->purge_usage = slurmdbd_conf->purge_usage;

	rc = jobacct_storage_g_archive(slurmdbd_conn->db_conn, arch_cond);
	if (rc != SLURM_SUCCESS) {
		if (errno == EACCES)
			comment = "Problem accessing file.";
		else
			comment = "Error with request.";
	}
end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ARCHIVE_DUMP);
	return rc;
}

static int _archive_load(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	slurmdb_archive_rec_t *arch_rec = msg->data;
	char *comment = "SUCCESS";

	debug2("DBD_ARCHIVE_LOAD: called");
	if (!_validate_super_user(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	rc = jobacct_storage_g_archive_load(slurmdbd_conn->db_conn, arch_rec);

	if (rc == ENOENT)
		comment = "No archive file given to recover.";
	else if (rc != SLURM_SUCCESS)
		comment = "Error with request.";

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ARCHIVE_LOAD);
	return rc;
}

static int _cluster_tres(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cluster_tres_msg_t *cluster_tres_msg = msg->data;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_CLUSTER_TRES message from invalid uid";
		error("DBD_CLUSTER_TRES message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_CLUSTER_TRES: called for %s(%s)",
	       slurmdbd_conn->conn->cluster_name,
	       cluster_tres_msg->tres_str);

	rc = clusteracct_storage_g_cluster_tres(
		slurmdbd_conn->db_conn,
		cluster_tres_msg->cluster_nodes,
		cluster_tres_msg->tres_str,
		cluster_tres_msg->event_time,
		slurmdbd_conn->conn->version);
	if (rc == ESLURM_ACCESS_DENIED) {
		comment = "This cluster hasn't been added to accounting yet";
		rc = SLURM_ERROR;
	}
end_it:
	if (rc == SLURM_SUCCESS) {
		xfree(slurmdbd_conn->tres_str);
		slurmdbd_conn->tres_str = cluster_tres_msg->tres_str;
		cluster_tres_msg->tres_str = NULL;
	}
	if (!slurmdbd_conn->conn->rem_port) {
		debug3("DBD_CLUSTER_TRES: cluster not registered");
		slurmdbd_conn->conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				slurmdbd_conn->conn->rem_host);

		_add_registered_cluster(slurmdbd_conn);
	}

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_CLUSTER_TRES);
	return rc;
}

static int _get_accounts(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_ACCOUNTS: called");

	list_msg.my_list = acct_storage_g_get_accounts(slurmdbd_conn->db_conn,
						       *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_ACCOUNTS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_ACCOUNTS,
				       *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_ACCOUNTS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_tres(slurmdbd_conn_t *slurmdbd_conn,
		     persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_TRES: called");

	list_msg.my_list = acct_storage_g_get_tres(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_TRES, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_TRES, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_TRES);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_assocs(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_ASSOCS: called");

	list_msg.my_list = acct_storage_g_get_assocs(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_ASSOCS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_ASSOCS, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_ASSOCS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_clusters(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_CLUSTERS: called");

	list_msg.my_list = acct_storage_g_get_clusters(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_CLUSTERS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_CLUSTERS,
				       *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_CLUSTERS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_federations(slurmdbd_conn_t *slurmdbd_conn, persist_msg_t *msg,
			    Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_FEDERATIONS: called");

	list_msg.my_list = acct_storage_g_get_federations(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_FEDERATIONS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_FEDERATIONS,
				       *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_FEDERATIONS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_config(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	char *config_name = msg->data;
	dbd_list_msg_t list_msg = { NULL };

	debug2("DBD_GET_CONFIG: called");

	if (config_name == NULL ||
	    xstrcmp(config_name, "slurmdbd.conf") == 0)
		list_msg.my_list = dump_config();
	else if ((list_msg.my_list = acct_storage_g_get_config(
			  slurmdbd_conn->db_conn, config_name)) == NULL) {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_CONFIG);
		xfree(config_name);
		return SLURM_ERROR;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_CONFIG, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_CONFIG, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);
	xfree(config_name);

	return SLURM_SUCCESS;
}

static int _get_events(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_EVENTS: called");

	list_msg.my_list = acct_storage_g_get_events(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_EVENTS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_EVENTS,
				       *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_EVENTS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_jobs_cond(slurmdbd_conn_t *slurmdbd_conn,
			  persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *cond_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	slurmdb_job_cond_t *job_cond = cond_msg->cond;
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_JOBS_COND: called");

	/* fail early if too wide a query */
	if (!job_cond->step_list && !_validate_slurm_user(*uid)
	    && (slurmdbd_conf->max_time_range != INFINITE)) {
		time_t start, end;

		start = job_cond->usage_start;

		if (job_cond->usage_end)
			end = job_cond->usage_end;
		else
			end = time(NULL);

		if ((end - start) > slurmdbd_conf->max_time_range) {
			info("Rejecting query > MaxQueryTimeRange from uid %u",
			     *uid);
			*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
								ESLURM_DB_QUERY_TOO_WIDE,
								slurm_strerror(ESLURM_DB_QUERY_TOO_WIDE),
								DBD_GET_JOBS_COND);
			return SLURM_ERROR;
		}
	}

	list_msg.my_list = jobacct_storage_g_get_jobs_cond(
		slurmdbd_conn->db_conn, *uid, job_cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_JOBS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_JOBS, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_JOBS_COND);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_probs(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_PROBS: called");

	list_msg.my_list = acct_storage_g_get_problems(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_PROBS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_PROBS, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_PROBS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_qos(slurmdbd_conn_t *slurmdbd_conn,
		    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *cond_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_QOS: called");

	list_msg.my_list = acct_storage_g_get_qos(slurmdbd_conn->db_conn, *uid,
						  cond_msg->cond);

	if (errno == ESLURM_ACCESS_DENIED && !list_msg.my_list)
		list_msg.my_list = list_create(NULL);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_QOS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_QOS, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_QOS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_res(slurmdbd_conn_t *slurmdbd_conn,
		    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_RES: called");

	list_msg.my_list = acct_storage_g_get_res(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_RES, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_RES,
				       *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_RES);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);
	return rc;
}

static int _get_txn(slurmdbd_conn_t *slurmdbd_conn,
		    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *cond_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_TXN: called");

	list_msg.my_list = acct_storage_g_get_txn(slurmdbd_conn->db_conn, *uid,
						  cond_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_TXN, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_TXN, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_TXN);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_usage(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_usage_msg_t *get_msg = msg->data;
	dbd_usage_msg_t got_msg;
	uint16_t ret_type = 0;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	info("DBD_GET_USAGE: called type is %s",
	     slurmdbd_msg_type_2_str(msg->msg_type, 1));

	switch(msg->msg_type) {
	case DBD_GET_ASSOC_USAGE:
		ret_type = DBD_GOT_ASSOC_USAGE;
		break;
	case DBD_GET_WCKEY_USAGE:
		ret_type = DBD_GOT_WCKEY_USAGE;
		break;
	case DBD_GET_CLUSTER_USAGE:
		ret_type = DBD_GOT_CLUSTER_USAGE;
		break;
	default:
		comment = "Unknown type of usage to get";
		error("%s %u", comment, msg->msg_type);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							SLURM_ERROR, comment,
							msg->msg_type);
		return SLURM_ERROR;
	}

	rc = acct_storage_g_get_usage(slurmdbd_conn->db_conn,
				      *uid, get_msg->rec, msg->msg_type,
				      get_msg->start, get_msg->end);

	if (rc != SLURM_SUCCESS) {
		comment = "Problem getting usage info";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							msg->msg_type);
		return rc;

	}
	memset(&got_msg, 0, sizeof(dbd_usage_msg_t));
	got_msg.rec = get_msg->rec;
	get_msg->rec = NULL;
	*out_buffer = init_buf(1024);
	pack16((uint16_t) ret_type, *out_buffer);
	slurmdbd_pack_usage_msg(&got_msg, slurmdbd_conn->conn->version,
				ret_type, *out_buffer);

	return SLURM_SUCCESS;
}

static int _get_users(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	slurmdb_user_cond_t * user_cond = NULL;

	debug2("DBD_GET_USERS: called");

	user_cond = get_msg->cond;
	if ((!user_cond->with_assocs && !user_cond->with_wckeys)
	    && ((slurmdbd_conn->conn->version < 8)
		|| (user_cond->assoc_cond->only_defs))) {
		List cluster_list = user_cond->assoc_cond->cluster_list;
		/* load up with just this cluster to query against
		 * since befor 2.2 we had only 1 default account so
		 * send the default for this cluster. */
		if (!cluster_list) {
			cluster_list = list_create(NULL);
			list_append(cluster_list,
				    slurmdbd_conn->conn->cluster_name);
			user_cond->assoc_cond->cluster_list = cluster_list;
		}
	}

	list_msg.my_list = acct_storage_g_get_users(slurmdbd_conn->db_conn,
						    *uid, user_cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_USERS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_USERS, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_USERS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_wckeys(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_WCKEYS: called");

	/* We have to check this here, and not in the plugin.  There
	 * are places in the plugin that a non-admin can call this and
	 * it be ok. */
	if (!_validate_operator(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							ESLURM_ACCESS_DENIED,
							comment,
							DBD_GET_WCKEYS);
		return ESLURM_ACCESS_DENIED;
	}

	list_msg.my_list = acct_storage_g_get_wckeys(slurmdbd_conn->db_conn,
						     *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_WCKEYS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_WCKEYS, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_WCKEYS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _get_reservations(slurmdbd_conn_t *slurmdbd_conn,
			     persist_msg_t *msg, Buf *out_buffer,
			     uint32_t *uid)
{
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;

	debug2("DBD_GET_RESVS: called");

	list_msg.my_list = acct_storage_g_get_reservations(
		slurmdbd_conn->db_conn, *uid, get_msg->cond);

	if (!errno) {
		if (!list_msg.my_list)
			list_msg.my_list = list_create(NULL);
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_RESVS, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_RESVS, *out_buffer);
	} else {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							errno,
							slurm_strerror(errno),
							DBD_GET_RESVS);
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _flush_jobs(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_cluster_tres_msg_t *cluster_tres_msg = msg->data;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_FLUSH_JOBS message from invalid uid";
		error("DBD_FLUSH_JOBS message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_FLUSH_JOBS: called for %s",
	       slurmdbd_conn->conn->cluster_name);

	rc = acct_storage_g_flush_jobs_on_cluster(
		slurmdbd_conn->db_conn,
		cluster_tres_msg->event_time);
end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_FLUSH_JOBS);
	return rc;
}

static int _init_conn(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_init_msg_t *init_msg = msg->data;
	persist_init_req_msg_t persist_init;
	char *comment = NULL;
	int rc = SLURM_SUCCESS;

	/* 2 versions After 17.02 this can go away since dbd_init_msg_t is going
	 * away with it.
	 */

	if ((init_msg->version < SLURM_MIN_PROTOCOL_VERSION) ||
	    (init_msg->version > SLURM_PROTOCOL_VERSION)) {
		comment = "Incompatible RPC version";
		error("Incompatible RPC version received "
		      "(%u not between %d and %d)",
		      init_msg->version,
		      SLURM_MIN_PROTOCOL_VERSION, SLURM_PROTOCOL_VERSION);
		rc = SLURM_PROTOCOL_VERSION_ERROR;
		goto end_it;
	}

	memset(&persist_init, 0, sizeof(persist_init_req_msg_t));
	persist_init.cluster_name = init_msg->cluster_name;
	persist_init.version = init_msg->version;
	persist_init.uid = init_msg->uid;

	rc = _handle_init_msg(slurmdbd_conn, &persist_init, uid);

	if (rc != SLURM_SUCCESS)
		comment = slurm_strerror(rc);
end_it:

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_INIT);

	return rc;
}

static int   _fini_conn(slurmdbd_conn_t *slurmdbd_conn, persist_msg_t *msg,
			Buf *out_buffer)
{
	dbd_fini_msg_t *fini_msg = msg->data;
	char *comment = NULL;
	int rc = SLURM_SUCCESS;


	debug2("DBD_FINI: CLOSE:%u COMMIT:%u",
	       fini_msg->close_conn, fini_msg->commit);
	if (fini_msg->close_conn == 1)
		rc = acct_storage_g_close_connection(&slurmdbd_conn->db_conn);
	else
		rc = acct_storage_g_commit(slurmdbd_conn->db_conn,
					   fini_msg->commit);

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_FINI);
	return rc;

}

static int  _job_complete(slurmdbd_conn_t *slurmdbd_conn,
			  persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_comp_msg_t *job_comp_msg = msg->data;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_JOB_COMPLETE message from invalid uid";
		error("CONN:%u %s %u",
		      slurmdbd_conn->conn->fd, comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.admin_comment = job_comp_msg->admin_comment;
	job.assoc_id = job_comp_msg->assoc_id;
	job.comment = job_comp_msg->comment;
	if (job_comp_msg->db_index != NO_VAL64)
		job.db_index = job_comp_msg->db_index;
	job.derived_ec = job_comp_msg->derived_ec;
	job.end_time = job_comp_msg->end_time;
	job.exit_code = job_comp_msg->exit_code;
	job.job_id = job_comp_msg->job_id;
	job.job_state = job_comp_msg->job_state;
	job.requid = job_comp_msg->req_uid;
	job.nodes = job_comp_msg->nodes;
	job.start_time = job_comp_msg->start_time;
	details.submit_time = job_comp_msg->submit_time;
	job.start_protocol_ver = slurmdbd_conn->conn->version;
	job.system_comment = job_comp_msg->system_comment;
	job.tres_alloc_str = job_comp_msg->tres_alloc_str;

	job.details = &details;

	if (job.job_state & JOB_RESIZING) {
		job.resize_time = job_comp_msg->end_time;
		debug2("DBD_JOB_COMPLETE: RESIZE ID:%u", job_comp_msg->job_id);
	} else
		debug2("DBD_JOB_COMPLETE: ID:%u", job_comp_msg->job_id);

	rc = jobacct_storage_g_job_complete(slurmdbd_conn->db_conn, &job);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just in case this gets set we need to clear it */
	xfree(job.wckey);

	if (!slurmdbd_conn->conn->rem_port) {
		debug3("DBD_JOB_COMPLETE: cluster not registered");
		slurmdbd_conn->conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				slurmdbd_conn->conn->rem_host);

		_add_registered_cluster(slurmdbd_conn);
	}

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_JOB_COMPLETE);
	return SLURM_SUCCESS;
}

static int  _job_start(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_start_msg_t *job_start_msg = msg->data;
	dbd_id_rc_msg_t id_rc_msg;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_JOB_START message from invalid uid";
		error("CONN:%u %s %u",
		      slurmdbd_conn->conn->fd, comment, *uid);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							ESLURM_ACCESS_DENIED,
							comment,
							DBD_JOB_START);
		return SLURM_ERROR;
	}

	_process_job_start(slurmdbd_conn, job_start_msg, &id_rc_msg);

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_ID_RC, *out_buffer);
	slurmdbd_pack_id_rc_msg(&id_rc_msg,
				slurmdbd_conn->conn->version, *out_buffer);
	return SLURM_SUCCESS;
}

static int  _job_suspend(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_job_suspend_msg_t *job_suspend_msg = msg->data;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_JOB_SUSPEND message from invalid uid";
		error("CONN:%u %s %u",
		      slurmdbd_conn->conn->fd, comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_JOB_SUSPEND: ID:%u STATE:%s",
	       job_suspend_msg->job_id,
	       job_state_string(job_suspend_msg->job_state));

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = job_suspend_msg->assoc_id;
	if (job_suspend_msg->db_index != NO_VAL64)
		job.db_index = job_suspend_msg->db_index;
	job.job_id = job_suspend_msg->job_id;
	job.job_state = job_suspend_msg->job_state;
	details.submit_time = job_suspend_msg->submit_time;
	job.start_protocol_ver = slurmdbd_conn->conn->version;
	job.suspend_time = job_suspend_msg->suspend_time;

	job.details = &details;
	rc = jobacct_storage_g_job_suspend(slurmdbd_conn->db_conn, &job);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just in case this gets set we need to clear it */
	xfree(job.wckey);
end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment,
						DBD_JOB_SUSPEND);
	return SLURM_SUCCESS;
}

static int   _modify_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_ACCOUNTS: called");

	if (!(list_msg.my_list = acct_storage_g_modify_accounts(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_ACCOUNTS);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _modify_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_ASSOCS: called");

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if (!(list_msg.my_list = acct_storage_g_modify_assocs(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_ASSOCS);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _modify_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer,
			      uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_CLUSTERS: called");

	if (!(list_msg.my_list = acct_storage_g_modify_clusters(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_CLUSTERS);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_federations(slurmdbd_conn_t *slurmdbd_conn,
			       persist_msg_t *msg,
			       Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_FEDERATIONS: called");

	if (!(list_msg.my_list = acct_storage_g_modify_federations(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform "
				"this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_FEDERATIONS);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _modify_job(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_JOB: called");

	if (!(list_msg.my_list = acct_storage_g_modify_job(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_JOB);
		return rc;
	}

	if (get_msg->cond &&
	    (((slurmdb_job_modify_cond_t *)get_msg->cond)->flags &&
	     SLURMDB_MODIFY_NO_WAIT)) {
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_JOB);
	} else {
		*out_buffer = init_buf(1024);
		pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
		slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
				       DBD_GOT_LIST, *out_buffer);
	}

	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _modify_qos(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_QOS: called");

	if (!(list_msg.my_list = acct_storage_g_modify_qos(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_QOS_PREEMPTION_LOOP) {
			comment = "QOS Preemption loop detected";
			rc = ESLURM_QOS_PREEMPTION_LOOP;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_QOS);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _modify_res(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_RES: called");

	if (!(list_msg.my_list = acct_storage_g_modify_res(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform "
				"this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_MODIFY_RES);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);
	return rc;
}

static int   _modify_users(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;
	int same_user = 0;
	slurmdb_user_cond_t *user_cond = NULL;
	slurmdb_user_rec_t *user_rec = NULL;

	debug2("DBD_MODIFY_USERS: called");

	user_cond = (slurmdb_user_cond_t *)get_msg->cond;
	user_rec = (slurmdb_user_rec_t *)get_msg->rec;

	if (!_validate_operator(*uid, slurmdbd_conn)) {
		if (user_cond && user_cond->assoc_cond
		    && user_cond->assoc_cond->user_list
		    && (list_count(user_cond->assoc_cond->user_list) == 1)) {
			uid_t pw_uid;
			char *name;
			name = list_peek (user_cond->assoc_cond->user_list);
		        if ((uid_from_string (name, &pw_uid) >= 0)
			    && pw_uid == *uid) {
				same_user = 1;
				goto is_same_user;
			}
		}
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							ESLURM_ACCESS_DENIED,
							comment,
							DBD_MODIFY_USERS);

		return ESLURM_ACCESS_DENIED;
	}

is_same_user:

	/* same_user can only alter the default account, default wckey
	 * nothing else */
	if (same_user) {
		/* If we add anything else here for the user we will
		 * need to document it
		 */
		if ((user_rec->admin_level != SLURMDB_ADMIN_NOTSET)) {
			comment = "You can only change your own default account, default wckey nothing else";
			error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
			*out_buffer = slurm_persist_make_rc_msg(
				slurmdbd_conn->conn,
				ESLURM_ACCESS_DENIED,
				comment,
				DBD_MODIFY_USERS);

			return ESLURM_ACCESS_DENIED;
		}
	}

	if ((user_rec->admin_level != SLURMDB_ADMIN_NOTSET) &&
	    !_validate_super_user(*uid, slurmdbd_conn)) {
		comment = "You must be a super user to modify a users admin level";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn,
			ESLURM_ACCESS_DENIED,
			comment,
			DBD_MODIFY_USERS);
		return ESLURM_ACCESS_DENIED;
	}

	if (!(list_msg.my_list = acct_storage_g_modify_users(
		      slurmdbd_conn->db_conn, *uid, user_cond, user_rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_MODIFY_USERS);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _modify_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_list_msg_t list_msg = { NULL };
	int rc = SLURM_SUCCESS;
	dbd_modify_msg_t *get_msg = msg->data;
	char *comment = NULL;

	debug2("DBD_MODIFY_WCKEYS: called");

	if (!(list_msg.my_list = acct_storage_g_modify_wckeys(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond,
		      get_msg->rec))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_MODIFY_WCKEYS);
		return rc;
	}

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _modify_reservation(slurmdbd_conn_t *slurmdbd_conn,
			       persist_msg_t *msg, Buf *out_buffer,
			       uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = msg->data;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_MODIFY_RESV message from invalid uid";
		error("CONN:%u %s %u",
		      slurmdbd_conn->conn->fd, comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_MODIFY_RESV: called");

	rc = acct_storage_g_modify_reservation(slurmdbd_conn->db_conn,
					       rec_msg->rec);

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_MODIFY_RESV);
	return rc;
}

static int _node_state(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_node_state_msg_t *node_state_msg = msg->data;
	struct node_record node_ptr;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_NODE_STATE message from invalid uid";
		error("CONN:%u %s %u",
		      slurmdbd_conn->conn->fd, comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	memset(&node_ptr, 0, sizeof(struct node_record));
	node_ptr.name = node_state_msg->hostlist;
	node_ptr.tres_str = node_state_msg->tres_str;
	node_ptr.node_state = node_state_msg->state;
	node_ptr.reason = node_state_msg->reason;
	node_ptr.reason_time = node_state_msg->event_time;
	node_ptr.reason_uid = node_state_msg->reason_uid;

	slurmctld_conf.fast_schedule = 0;

	if (!node_ptr.tres_str)
		node_state_msg->new_state = DBD_NODE_STATE_UP;

	if (node_state_msg->new_state == DBD_NODE_STATE_UP) {
		debug2("DBD_NODE_STATE_UP: NODE:%s REASON:%s TIME:%ld",
		       node_state_msg->hostlist,
		       node_state_msg->reason,
		       (long)node_state_msg->event_time);

		/* clusteracct_storage_g_node_up can change the reason
		 * field so copy it to avoid memory issues.
		 */
		node_ptr.reason = xstrdup(node_state_msg->reason);
		rc = clusteracct_storage_g_node_up(
			slurmdbd_conn->db_conn,
			&node_ptr,
			node_state_msg->event_time);
		xfree(node_ptr.reason);
	} else {
		debug2("DBD_NODE_STATE_DOWN: NODE:%s STATE:%s REASON:%s UID:%u TIME:%ld",
		       node_state_msg->hostlist,
		       node_state_string(node_state_msg->state),
		       node_state_msg->reason,
		       node_ptr.reason_uid,
		       (long)node_state_msg->event_time);
		rc = clusteracct_storage_g_node_down(
			slurmdbd_conn->db_conn,
			&node_ptr,
			node_state_msg->event_time,
			node_state_msg->reason, node_ptr.reason_uid);
	}

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_NODE_STATE);
	return SLURM_SUCCESS;
}

static void _process_job_start(slurmdbd_conn_t *slurmdbd_conn,
			       dbd_job_start_msg_t *job_start_msg,
			       dbd_id_rc_msg_t *id_rc_msg)
{
	struct job_record job, *job_ptr;
	struct job_details details;
	job_array_struct_t array_recs;

	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));
	memset(&array_recs, 0, sizeof(job_array_struct_t));
	memset(id_rc_msg, 0, sizeof(dbd_id_rc_msg_t));

	job.total_nodes = job_start_msg->alloc_nodes;
	job.account = _replace_double_quotes(job_start_msg->account);
	job.array_job_id = job_start_msg->array_job_id;
	job.array_task_id = job_start_msg->array_task_id;
	array_recs.task_id_str = job_start_msg->array_task_str;
	array_recs.max_run_tasks = job_start_msg->array_max_tasks;
	array_recs.task_cnt = job_start_msg->array_task_pending;
	job.assoc_id = job_start_msg->assoc_id;
	if (job_start_msg->db_index != NO_VAL64)
		job.db_index = job_start_msg->db_index;
	details.begin_time = job_start_msg->eligible_time;
	job.user_id = job_start_msg->uid;
	job.group_id = job_start_msg->gid;
	job.job_id = job_start_msg->job_id;
	job.job_state = job_start_msg->job_state;
	job.mcs_label = _replace_double_quotes(job_start_msg->mcs_label);
	job.name = _replace_double_quotes(job_start_msg->name);
	job.nodes = job_start_msg->nodes;
	job.network = job_start_msg->node_inx;
	job.pack_job_id = job_start_msg->pack_job_id;
	job.pack_job_offset = job_start_msg->pack_job_offset;
	job.partition = job_start_msg->partition;
	details.min_cpus = job_start_msg->req_cpus;
	details.pn_min_memory = job_start_msg->req_mem;
	job.qos_id = job_start_msg->qos_id;
	job.resv_id = job_start_msg->resv_id;
	job.priority = job_start_msg->priority;
	job.start_protocol_ver = slurmdbd_conn->conn->version;
	job.start_time = job_start_msg->start_time;
	job.time_limit = job_start_msg->timelimit;
	job.tres_alloc_str = job_start_msg->tres_alloc_str;
	job.tres_req_str = job_start_msg->tres_req_str;
	job.gres_alloc = job_start_msg->gres_alloc;
	job.gres_req = job_start_msg->gres_req;
	job.gres_used = job_start_msg->gres_used;
	job.wckey = _replace_double_quotes(job_start_msg->wckey);
	details.work_dir = _replace_double_quotes(job_start_msg->work_dir);
	details.submit_time = job_start_msg->submit_time;
	job.db_flags = job_start_msg->db_flags;
	details.features = _replace_double_quotes(job_start_msg->constraints);
	job.state_reason_prev_db = job_start_msg->state_reason_prev;

	job.array_recs = &array_recs;
	job.details = &details;
	job_ptr = &job;

	if (job.job_state & JOB_RESIZING) {
		job.resize_time = job_start_msg->eligible_time;
		debug2("DBD_JOB_START: RESIZE CALL ID:%u NAME:%s INX:%"PRIu64,
		       job_start_msg->job_id, job_start_msg->name,
		       job.db_index);
	} else if (job.start_time && !IS_JOB_PENDING(job_ptr)) {
		debug2("DBD_JOB_START: START CALL ID:%u NAME:%s INX:%"PRIu64,
		       job_start_msg->job_id, job_start_msg->name,
		       job.db_index);
	} else {
		debug2("DBD_JOB_START: ELIGIBLE CALL ID:%u NAME:%s",
		       job_start_msg->job_id, job_start_msg->name);
	}
	id_rc_msg->return_code = jobacct_storage_g_job_start(
		slurmdbd_conn->db_conn, &job);
	id_rc_msg->job_id = job.job_id;
	id_rc_msg->db_index = job.db_index;

	/* just in case job.wckey was set because we didn't send one */
	if (!job_start_msg->wckey)
		xfree(job.wckey);

	if (!slurmdbd_conn->conn->rem_port) {
		debug3("DBD_JOB_START: cluster not registered");
		slurmdbd_conn->conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				slurmdbd_conn->conn->rem_host);

		_add_registered_cluster(slurmdbd_conn);
	}
}

static int   _reconfig(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							ESLURM_ACCESS_DENIED,
							comment,
							DBD_MODIFY_WCKEYS);

		return ESLURM_ACCESS_DENIED;
	}

	info("Reconfigure request received");
	reconfig();

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_RECONFIG);
	return rc;

}

static int   _register_ctld(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_register_ctld_msg_t *register_ctld_msg = msg->data;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;
	slurmdb_cluster_cond_t cluster_q;
	slurmdb_cluster_rec_t cluster;
	dbd_list_msg_t list_msg = { NULL };

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_REGISTER_CTLD message from invalid uid";
		error("CONN:%u %s %u",
		      slurmdbd_conn->conn->fd, comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_REGISTER_CTLD: called for %s(%u)",
	       slurmdbd_conn->conn->cluster_name, register_ctld_msg->port);

	/* Just to make sure we don't allow a NULL cluster name to attempt
	   to connect.  This should never happen, but here just for
	   sanity check.
	*/
	if (!slurmdbd_conn->conn->cluster_name) {
		comment = "Must have a cluster name to register it";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		rc = ESLURM_BAD_NAME;
		goto end_it;
	}

	debug2("slurmctld at ip:%s, port:%d",
	       slurmdbd_conn->conn->rem_host, register_ctld_msg->port);

	slurmdb_init_cluster_cond(&cluster_q, 0);
	slurmdb_init_cluster_rec(&cluster, 0);

	cluster_q.cluster_list = list_create(NULL);
	list_append(cluster_q.cluster_list, slurmdbd_conn->conn->cluster_name);
	cluster.control_host = slurmdbd_conn->conn->rem_host;
	cluster.control_port = register_ctld_msg->port;
	cluster.dimensions = register_ctld_msg->dimensions;
	cluster.flags = register_ctld_msg->flags;
	cluster.plugin_id_select = register_ctld_msg->plugin_id_select;
	cluster.rpc_version = slurmdbd_conn->conn->version;

	list_msg.my_list = acct_storage_g_modify_clusters(
		slurmdbd_conn->db_conn, *uid, &cluster_q, &cluster);
	if (errno == EFAULT) {
		comment = "Request to register was incomplete";
		rc = SLURM_ERROR;
	} else if (errno == ESLURM_ACCESS_DENIED) {
		comment = "Your user doesn't have privilege to perform this action";
		rc = ESLURM_ACCESS_DENIED;
	} else if (errno == ESLURM_DB_CONNECTION) {
		comment = slurm_strerror(errno);
		rc = errno;
	} else if (!list_msg.my_list || !list_count(list_msg.my_list)) {
		comment = "This cluster hasn't been added to accounting yet";
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(list_msg.my_list);
	FREE_NULL_LIST(cluster_q.cluster_list);

end_it:

	if (rc == SLURM_SUCCESS) {
		slurmdbd_conn->conn->rem_port = register_ctld_msg->port;

		_add_registered_cluster(slurmdbd_conn);
	}

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_REGISTER_CTLD);
	return rc;
}

static int   _remove_accounts(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_ACCOUNTS: called");

	if (!(list_msg.my_list = acct_storage_g_remove_accounts(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_REMOVE_ACCOUNTS);
		return rc;
	}
	list_msg.return_code = errno;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _remove_account_coords(slurmdbd_conn_t *slurmdbd_conn,
				    persist_msg_t *msg, Buf *out_buffer,
				    uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_acct_coord_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_ACCOUNT_COORDS: called");

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if (!(list_msg.my_list = acct_storage_g_remove_coord(
		      slurmdbd_conn->db_conn, *uid, get_msg->acct_list,
		      get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment,
			DBD_REMOVE_ACCOUNT_COORDS);
		return rc;
	}
	list_msg.return_code = SLURM_SUCCESS;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _remove_assocs(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_ASSOCS: called");

	/* All authentication needs to be done inside the plugin since we are
	 * unable to know what accounts this request is talking about
	 * until we process it through the database.
	 */

	if (!(list_msg.my_list = acct_storage_g_remove_assocs(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_REMOVE_ASSOCS);
		return rc;
	}
	list_msg.return_code = errno;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;

}

static int   _remove_clusters(slurmdbd_conn_t *slurmdbd_conn,
			      persist_msg_t *msg, Buf *out_buffer,
			      uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_CLUSTERS: called");

	if (!(list_msg.my_list = acct_storage_g_remove_clusters(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_REMOVE_CLUSTERS);
		return rc;
	}
	list_msg.return_code = errno;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_federations(slurmdbd_conn_t *slurmdbd_conn,
			       persist_msg_t *msg,
			       Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_FEDERATIONS: called");

	if (!(list_msg.my_list = acct_storage_g_remove_federations(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform "
				"this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							rc, comment,
							DBD_REMOVE_FEDERATIONS);
		return rc;
	}
	list_msg.return_code = errno;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _remove_qos(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_QOS: called");

	if (!(list_msg.my_list = acct_storage_g_remove_qos(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_REMOVE_QOS);
		return rc;
	}
	list_msg.return_code = SLURM_SUCCESS;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_res(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_RES: called");

	if (!(list_msg.my_list = acct_storage_g_remove_res(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform "
				"this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_REMOVE_RES);
		return rc;
	}
	list_msg.return_code = errno;
	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _remove_users(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_USERS: called");

	if (!(list_msg.my_list = acct_storage_g_remove_users(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_REMOVE_USERS);
		return rc;
	}
	list_msg.return_code = errno;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int   _remove_wckeys(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_cond_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;

	debug2("DBD_REMOVE_WCKEYS: called");

	if (!(list_msg.my_list = acct_storage_g_remove_wckeys(
		      slurmdbd_conn->db_conn, *uid, get_msg->cond))) {
		if (errno == ESLURM_ACCESS_DENIED) {
			comment = "Your user doesn't have privilege to perform this action";
			rc = ESLURM_ACCESS_DENIED;
		} else if (errno == SLURM_ERROR) {
			comment = "Something was wrong with your query";
			rc = SLURM_ERROR;
		} else if (errno == SLURM_NO_CHANGE_IN_DATA) {
			comment = "Request didn't affect anything";
			rc = SLURM_SUCCESS;
		} else if (errno == ESLURM_DB_CONNECTION) {
			comment = slurm_strerror(errno);
			rc = errno;
		} else {
			rc = errno;
			if (!(comment = slurm_strerror(errno)))
				comment = "Unknown issue";
		}
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn, rc, comment, DBD_REMOVE_WCKEYS);
		return rc;
	}
	list_msg.return_code = SLURM_SUCCESS;

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_LIST, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_LIST, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return rc;
}

static int _remove_reservation(slurmdbd_conn_t *slurmdbd_conn,
			       persist_msg_t *msg, Buf *out_buffer,
			       uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	dbd_rec_msg_t *rec_msg = msg->data;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_REMOVE_RESV message from invalid uid";
		error("DBD_REMOVE_RESV message from invalid uid %u", *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_REMOVE_RESV: called");

	rc = acct_storage_g_remove_reservation(slurmdbd_conn->db_conn,
					       rec_msg->rec);

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_REMOVE_RESV);
	return rc;
}

static int   _roll_usage(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_roll_usage_msg_t *get_msg = msg->data;
	int i, rc = SLURM_SUCCESS;
	char *comment = NULL;
	rollup_stats_t rollup_stats;

	info("DBD_ROLL_USAGE: called");

	if (!_validate_operator(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	memset(&rollup_stats, 0, sizeof(rollup_stats_t));
	rc = acct_storage_g_roll_usage(slurmdbd_conn->db_conn,
				       get_msg->start, get_msg->end,
				       get_msg->archive_data, &rollup_stats);
	slurm_mutex_lock(&rpc_mutex);
	for (i = 0; i < ROLLUP_COUNT; i++) {
		if (rollup_stats.rollup_time[i] == 0)
			continue;
		rpc_stats.rollup_count[i]++;
		rpc_stats.rollup_time[i] += rollup_stats.rollup_time[i];
		rpc_stats.rollup_max_time[i] =
			MAX(rpc_stats.rollup_max_time[i],
			    rollup_stats.rollup_time[i]);
	}
	slurm_mutex_unlock(&rpc_mutex);

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_ROLL_USAGE);
	return rc;
}

static int   _send_mult_job_start(slurmdbd_conn_t *slurmdbd_conn,
				  persist_msg_t *msg, Buf *out_buffer,
				  uint32_t *uid)
{
	dbd_list_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;
	ListIterator itr = NULL;
	dbd_job_start_msg_t *job_start_msg;
	dbd_id_rc_msg_t *id_rc_msg;
	/* DEF_TIMERS; */

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_SEND_MULT_JOB_START message from invalid uid";
		error("%s %u", comment, *uid);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn,
			ESLURM_ACCESS_DENIED, comment,
			DBD_SEND_MULT_JOB_START);
		return SLURM_ERROR;
	}

	list_msg.my_list = list_create(slurmdbd_free_id_rc_msg);
	/* START_TIMER; */
	itr = list_iterator_create(get_msg->my_list);
	while ((job_start_msg = list_next(itr))) {
	        id_rc_msg = xmalloc(sizeof(dbd_id_rc_msg_t));
		list_append(list_msg.my_list, id_rc_msg);

		_process_job_start(slurmdbd_conn, job_start_msg, id_rc_msg);
	}
	list_iterator_destroy(itr);
	/* END_TIMER; */
	/* info("%d multi job took %s", */
	/*      list_count(get_msg->my_list), TIME_STR); */

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_MULT_JOB_START, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_MULT_JOB_START, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return SLURM_SUCCESS;
}

static int   _send_mult_msg(slurmdbd_conn_t *slurmdbd_conn,
			    persist_msg_t *msg, Buf *out_buffer,
			    uint32_t *uid)
{
	dbd_list_msg_t *get_msg = msg->data;
	dbd_list_msg_t list_msg = { NULL };
	char *comment = NULL;
	ListIterator itr = NULL;
	Buf req_buf = NULL, ret_buf = NULL;
	int rc = SLURM_SUCCESS;
	/* DEF_TIMERS; */

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_SEND_MULT_MSG message from invalid uid";
		error("%s %u", comment, *uid);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							ESLURM_ACCESS_DENIED,
							comment,
							DBD_SEND_MULT_MSG);
		return SLURM_ERROR;
	}

	list_msg.my_list = list_create(slurmdbd_free_buffer);
	/* START_TIMER; */
	itr = list_iterator_create(get_msg->my_list);
	while ((req_buf = list_next(itr))) {
		persist_msg_t sub_msg;

		ret_buf = NULL;

		rc = slurm_persist_conn_process_msg(
			slurmdbd_conn->conn, &sub_msg,
			get_buf_data(req_buf),
			size_buf(req_buf), &ret_buf, 0);

		if (rc == SLURM_SUCCESS) {
			rc = proc_req(slurmdbd_conn, &sub_msg, &ret_buf, uid);
			slurmdbd_free_msg((slurmdbd_msg_t *)&sub_msg);
		}

		if (ret_buf)
			list_append(list_msg.my_list, ret_buf);
		if (rc != SLURM_SUCCESS)
			break;
	}
	list_iterator_destroy(itr);
	/* END_TIMER; */
	/* info("%d multi took %s", list_count(get_msg->my_list), TIME_STR); */

	*out_buffer = init_buf(1024);
	pack16((uint16_t) DBD_GOT_MULT_MSG, *out_buffer);
	slurmdbd_pack_list_msg(&list_msg, slurmdbd_conn->conn->version,
			       DBD_GOT_MULT_MSG, *out_buffer);
	FREE_NULL_LIST(list_msg.my_list);

	return SLURM_SUCCESS;
}

static int  _step_complete(slurmdbd_conn_t *slurmdbd_conn,
			   persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_comp_msg_t *step_comp_msg = msg->data;
	struct step_record step;
	struct job_record job;
	struct job_details details;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_STEP_COMPLETE message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_STEP_COMPLETE: ID:%u.%u SUBMIT:%lu",
	       step_comp_msg->job_id, step_comp_msg->step_id,
	       (unsigned long) step_comp_msg->job_submit_time);

	memset(&step, 0, sizeof(struct step_record));
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));

	job.assoc_id = step_comp_msg->assoc_id;
	if (step_comp_msg->db_index != NO_VAL64)
		job.db_index = step_comp_msg->db_index;
	job.end_time = step_comp_msg->end_time;
	step.exit_code = step_comp_msg->exit_code;
	step.jobacct = step_comp_msg->jobacct;
	job.job_id = step_comp_msg->job_id;
	step.requid = step_comp_msg->req_uid;
	job.start_protocol_ver = slurmdbd_conn->conn->version;
	job.start_time = step_comp_msg->start_time;
	job.tres_alloc_str = step_comp_msg->job_tres_alloc_str;
	step.state = step_comp_msg->state;
	step.step_id = step_comp_msg->step_id;
	details.submit_time = step_comp_msg->job_submit_time;
	details.num_tasks = step_comp_msg->total_tasks;

	job.details = &details;
	step.job_ptr = &job;

	rc = jobacct_storage_g_step_complete(slurmdbd_conn->db_conn, &step);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;
	/* just in case this gets set we need to clear it */
	xfree(job.wckey);

	if (!slurmdbd_conn->conn->rem_port) {
		debug3("DBD_STEP_COMPLETE: cluster not registered");
		slurmdbd_conn->conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				slurmdbd_conn->conn->rem_host);

		_add_registered_cluster(slurmdbd_conn);
	}

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_STEP_COMPLETE);
	return rc;
}

static int  _step_start(slurmdbd_conn_t *slurmdbd_conn,
			persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	dbd_step_start_msg_t *step_start_msg = msg->data;
	struct step_record step;
	struct job_record job;
	struct job_details details;
	slurm_step_layout_t layout;
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_slurm_user(*uid)) {
		comment = "DBD_STEP_START message from invalid uid";
		error("%s %u", comment, *uid);
		rc = ESLURM_ACCESS_DENIED;
		goto end_it;
	}

	debug2("DBD_STEP_START: ID:%u.%u NAME:%s SUBMIT:%lu",
	       step_start_msg->job_id, step_start_msg->step_id,
	       step_start_msg->name,
	       (unsigned long) step_start_msg->job_submit_time);

	memset(&step, 0, sizeof(struct step_record));
	memset(&job, 0, sizeof(struct job_record));
	memset(&details, 0, sizeof(struct job_details));
	memset(&layout, 0, sizeof(slurm_step_layout_t));

	job.assoc_id = step_start_msg->assoc_id;
	if (step_start_msg->db_index != NO_VAL64)
		job.db_index = step_start_msg->db_index;
	job.job_id = step_start_msg->job_id;
	step.name = step_start_msg->name;
	job.nodes = step_start_msg->nodes;
	step.network = step_start_msg->node_inx;
	job.start_protocol_ver = slurmdbd_conn->conn->version;
	step.start_time = step_start_msg->start_time;
	details.submit_time = step_start_msg->job_submit_time;
	step.step_id = step_start_msg->step_id;
	details.num_tasks = step_start_msg->total_tasks;
	step.cpu_freq_min = step_start_msg->req_cpufreq_min;
	step.cpu_freq_max = step_start_msg->req_cpufreq_max;
	step.cpu_freq_gov = step_start_msg->req_cpufreq_gov;
	step.tres_alloc_str = step_start_msg->tres_alloc_str;

	layout.node_cnt = step_start_msg->node_cnt;
	layout.task_dist = step_start_msg->task_dist;

	job.details = &details;
	step.job_ptr = &job;
	step.step_layout = &layout;

	rc = jobacct_storage_g_step_start(slurmdbd_conn->db_conn, &step);

	if (rc && errno == 740) /* meaning data is already there */
		rc = SLURM_SUCCESS;

	/* just in case this gets set we need to clear it */
	xfree(job.wckey);

	if (!slurmdbd_conn->conn->rem_port) {
		debug3("DBD_STEP_START: cluster not registered");
		slurmdbd_conn->conn->rem_port =
			clusteracct_storage_g_register_disconn_ctld(
				slurmdbd_conn->db_conn,
				slurmdbd_conn->conn->rem_host);

		_add_registered_cluster(slurmdbd_conn);
	}

end_it:
	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_STEP_START);
	return rc;
}

static int  _get_stats(slurmdbd_conn_t *slurmdbd_conn,
		       persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							ESLURM_ACCESS_DENIED,
							comment, DBD_GET_STATS);

		return ESLURM_ACCESS_DENIED;
	}

	info("Get stats request received from UID %u", *uid);
	*out_buffer = init_buf(32 * 1024);
	pack16((uint16_t) DBD_GOT_STATS, *out_buffer);
	slurm_mutex_lock(&rpc_mutex);
	slurmdb_pack_stats_msg(&rpc_stats, slurmdbd_conn->conn->version,
			       *out_buffer);
	slurm_mutex_unlock(&rpc_mutex);

	return rc;
}

static int  _clear_stats(slurmdbd_conn_t *slurmdbd_conn,
			 persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int i, rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(
			slurmdbd_conn->conn,
			ESLURM_ACCESS_DENIED,
			comment, DBD_CLEAR_STATS);

		return ESLURM_ACCESS_DENIED;
	}

	info("Clear stats request received from UID %u", *uid);
	slurm_mutex_lock(&rpc_mutex);
	for (i = 0; i < ROLLUP_COUNT; i++) {
		rpc_stats.rollup_count[i] = 0;
		rpc_stats.rollup_time[i] = 0;
		rpc_stats.rollup_max_time[i] = 0;
	}
	for (i = 0; i < rpc_stats.type_cnt; i++) {
		rpc_stats.rpc_type_cnt[i] = 0;
		rpc_stats.rpc_type_time[i] = 0;
	}
	for (i = 0; i < rpc_stats.user_cnt; i++) {
		rpc_stats.rpc_user_cnt[i] = 0;
		rpc_stats.rpc_user_time[i] = 0;
	}
	slurm_mutex_unlock(&rpc_mutex);

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_CLEAR_STATS);
	return rc;
}

static int  _shutdown(slurmdbd_conn_t *slurmdbd_conn,
		      persist_msg_t *msg, Buf *out_buffer, uint32_t *uid)
{
	int rc = SLURM_SUCCESS;
	char *comment = NULL;

	if (!_validate_super_user(*uid, slurmdbd_conn)) {
		comment = "Your user doesn't have privilege to perform this action";
		error("CONN:%u %s", slurmdbd_conn->conn->fd, comment);
		*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
							ESLURM_ACCESS_DENIED,
							comment, DBD_SHUTDOWN);

		return ESLURM_ACCESS_DENIED;
	}

	info("Shutdown request received from UID %u", *uid);
	pthread_kill(signal_handler_thread, SIGTERM);

	*out_buffer = slurm_persist_make_rc_msg(slurmdbd_conn->conn,
						rc, comment, DBD_SHUTDOWN);
	return rc;
}
