/*****************************************************************************\
 *  rpc_mgr.c - functions for processing RPCs.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>

#include "src/common/http_switch.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/persist_conn.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurmdbd_pack.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/accounting_storage.h"
#include "src/interfaces/auth.h"
#include "src/interfaces/conn.h"

#include "src/slurmdbd/backup.h"
#include "src/slurmdbd/proc_req.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmdbd/slurmdbd.h"

/* Local functions */
static void _connection_fini_callback(persist_conn_ctx_t *pcon_ctx);

/*
 * Sessions opened while in control, as persist_conn_ctx_t, from _on_init()
 * until _on_fini(). The list only exists while it is not empty.
 */
static list_t *sessions = NULL;
static pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sessions_cond = PTHREAD_COND_INITIALIZER;
/* Requests being processed on sessions. Protected by sessions_mutex. */
static int active_session_msgs = 0;

/* Set by rpc_mgr_register() before any session can use it. */
static conmgr_timeouts_t rpc_timeouts;

/*
 * Track a session unless running as standby. Test have_control under
 * sessions_mutex, so that rpc_mgr_close_sessions() closes every session not
 * refused here.
 * RET true if tracked, false to refuse the session
 */
static bool _add_session(persist_conn_ctx_t *pcon_ctx)
{
	bool added = false;

	slurm_mutex_lock(&sessions_mutex);
	if (have_control) {
		if (!sessions)
			sessions = list_create(NULL);
		list_append(sessions, pcon_ctx);
		added = true;
	}
	slurm_mutex_unlock(&sessions_mutex);

	return added;
}

/* Stop tracking a session after its fini; wake rpc_mgr_close_sessions(). */
static void _remove_session(persist_conn_ctx_t *pcon_ctx)
{
	slurm_mutex_lock(&sessions_mutex);
	if (sessions && list_delete_ptr(sessions, pcon_ctx)) {
		if (!list_count(sessions))
			FREE_NULL_LIST(sessions);
		slurm_cond_broadcast(&sessions_cond);
	}
	slurm_mutex_unlock(&sessions_mutex);
}

/*
 * Process an RPC, excluding reconfig() while it runs. DBD_RECONFIG takes
 * reconfig_lock for writing itself.
 *
 * Lock here, not in proc_req(): _send_mult_msg() calls proc_req()
 * recursively for batched RPCs, which run under this read lock. Recursive
 * read locks can deadlock, and a batched DBD_RECONFIG would deadlock
 * waiting for the write lock.
 */
static int _proc_req(slurmdbd_conn_t *dbd_conn, slurm_msg_t *msg)
{
	int rc;

	if (msg->msg_type == DBD_RECONFIG)
		return proc_req(dbd_conn, msg);

	slurm_rwlock_rdlock(&reconfig_lock);
	rc = proc_req(dbd_conn, msg);
	slurm_rwlock_unlock(&reconfig_lock);

	return rc;
}

/* persist_conn authenticated REQUEST_PERSIST_INIT; authorize and open. */
static int _on_init(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg,
		    uint16_t *reply_flags)
{
	int rc;

	if (!_add_session(pcon_ctx)) {
		/*
		 * Refuse new sessions while running as standby. Relinquishing
		 * control closes the sessions opened before.
		 */
		debug("Refusing new session while running as standby");
		return ESLURM_DB_CONNECTION;
	}

	/* Publish the verified identity for proc_req() and its validators. */
	((slurmdbd_conn_t *) pcon_ctx->arg)->auth = msg->auth;

	slurm_rwlock_rdlock(&reconfig_lock);
	rc = proc_req(pcon_ctx->arg, msg);
	*reply_flags = slurmdbd_conf->persist_conn_rc_flags;
	slurm_rwlock_unlock(&reconfig_lock);

	return rc;
}

static int _on_msg(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg)
{
	return _proc_req(pcon_ctx->arg, msg);
}

static void _on_fini(persist_conn_ctx_t *pcon_ctx)
{
	_connection_fini_callback(pcon_ctx);
	_remove_session(pcon_ctx);
}

/*
 * Serve requests only while in control. Once control is relinquished, close
 * without a reply. slurmctld sends its queued messages again after it
 * reconnects; other requests fail.
 */
static int _on_session_msg(persist_conn_ctx_t *pcon_ctx, slurm_msg_t *msg)
{
	int rc;

	/*
	 * Test under sessions_mutex with the increment: rpc_mgr_close_sessions()
	 * clears have_control under the same mutex, so a request is either
	 * counted here before its close pass or refused after it. Never both
	 * missed.
	 */
	slurm_mutex_lock(&sessions_mutex);
	if (!have_control) {
		slurm_mutex_unlock(&sessions_mutex);
		debug("[%s] Dropping %s after relinquishing control",
		      conmgr_con_get_name(pcon_ctx->ref),
		      slurmdbd_msg_type_2_str(msg->msg_type, true));
		conmgr_con_queue_close(pcon_ctx->ref);
		return ESLURM_DB_CONNECTION;
	}
	active_session_msgs++;
	slurm_mutex_unlock(&sessions_mutex);

	rc = _on_msg(pcon_ctx, msg);

	slurm_mutex_lock(&sessions_mutex);
	xassert(active_session_msgs > 0);
	active_session_msgs--;
	slurm_cond_broadcast(&sessions_cond);
	slurm_mutex_unlock(&sessions_mutex);

	return rc;
}

static const persist_conn_ops_t persist_ops = {
	.flags = PERSIST_FLAG_DBD,
	.on_init = _on_init,
	.on_msg = _on_session_msg,
	.on_fini = _on_fini,
};

/*
 * Make a connection a persistent slurmdbd session. persist_conn reads the
 * framed REQUEST_PERSIST_INIT itself.
 */
static int _assign(conmgr_fd_ref_t *ref)
{
	slurmdbd_conn_t *dbd_conn = xmalloc(sizeof(*dbd_conn));

	slurmdbd_conn_init(dbd_conn);

	/* dbd_conn lives until _on_fini(), even if this fails. */
	return persist_conn_assign(ref, &persist_ops, &rpc_timeouts, dbd_conn,
				   NULL);
}

/*
 * DBD clients frame REQUEST_PERSIST_INIT as
 * [length][REQUEST_PERSIST_INIT][message] instead of a standard RPC.
 * Assign those connections before http_switch sees them; persist_conn
 * then reads that first frame itself.
 */
static bool _is_framed_persist_init(conmgr_fd_ref_t *ref)
{
	const void *data = NULL;
	size_t bytes = 0;
	uint16_t type;

	if (conmgr_con_get_input_buffer(ref, &data, &bytes) ||
	    (bytes < (sizeof(uint32_t) + sizeof(type))))
		return false;
	memcpy(&type, (const char *) data + sizeof(uint32_t), sizeof(type));
	return (ntohs(type) == REQUEST_PERSIST_INIT);
}

/* HTTP is not served yet. */
static int _on_http(conmgr_fd_t *con)
{
	log_flag(NET, "%s: [%s] rejecting non-RPC connection",
		 __func__, conmgr_fd_get_name(con));
	return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
}

static void *_on_connection(conmgr_callback_args_t conmgr_args, void *arg)
{
	log_flag(NET, "%s: [%s] accepted connection",
		 __func__, conmgr_fd_get_name(conmgr_args.con));

	/* Non-NULL; persistent connections get their own arg on assign. */
	return (void *) &persist_ops;
}

static int _on_data(conmgr_callback_args_t conmgr_args, void *arg)
{
	if (_is_framed_persist_init(conmgr_args.ref))
		return _assign(conmgr_args.ref);

	return http_switch_on_data(conmgr_args, _on_http);
}

/*
 * DBD clients open every connection with a framed REQUEST_PERSIST_INIT, which
 * _on_data() assigns before an RPC is unpacked. Reject anything else.
 */
static int _on_rpc(conmgr_callback_args_t conmgr_args, slurm_msg_t *msg,
		   int unpack_rc, void *arg)
{
	if (unpack_rc || !msg->auth.ids_set) {
		error("[%s] rejecting %s RPC: %s",
		      conmgr_con_get_name(conmgr_args.ref),
		      (unpack_rc ? "malformed" : "unauthenticated"),
		      slurm_strerror(unpack_rc ? unpack_rc :
				     SLURM_PROTOCOL_AUTHENTICATION_ERROR));
		FREE_NULL_MSG(msg);
		return (unpack_rc ? unpack_rc :
				    SLURM_PROTOCOL_AUTHENTICATION_ERROR);
	}

	error("[%s] rejecting %s RPC: expected framed REQUEST_PERSIST_INIT",
	      conmgr_con_get_name(conmgr_args.ref),
	      rpc_num2string(msg->msg_type));
	FREE_NULL_MSG(msg);
	return SLURM_PROTOCOL_AUTHENTICATION_ERROR;
}

extern void rpc_mgr_register(void)
{
	static const conmgr_events_t events = {
		.on_connection = _on_connection,
		.on_data = _on_data,
		.on_msg = _on_rpc,
	};
	static bool registered = false;
	/* Avoid Nagle delays on request/reply traffic, as persist_conn did. */
	conmgr_con_flags_t flags = CON_FLAG_TCP_NODELAY;
	int sockfd, rc;

	if (registered)
		return;
	registered = true;

	/*
	 * Inheirt the conmgr timeouts except allow the connection wait forever
	 * for new incoming RPCs
	 */
	rpc_timeouts = *conmgr_timeouts();
	rpc_timeouts.read = TIMESPEC_INFINITE;

	if (conn_tls_enabled())
		flags |= CON_FLAG_TLS_SERVER;

	if ((sockfd = slurm_init_msg_engine_port(slurmdbd_conf->dbd_port)) ==
	    SLURM_ERROR) {
		fatal("slurm_init_msg_engine_port error %m");
	}

	if ((rc = conmgr_process_fd_listen(sockfd, CON_TYPE_RAW, NULL, &events,
					   flags, NULL))) {
		fatal("conmgr_process_fd_listen() failed: %s",
		      slurm_strerror(rc));
	}

	log_flag(NET, "%s: listening on port %u%s",
		 __func__, slurmdbd_conf->dbd_port,
		 (flags & CON_FLAG_TLS_SERVER) ? " (TLS required)" : "");
}

/*
 * Close a session from its own work. While a connection's work is active,
 * conmgr_con_queue_close() closes it at once, as if called by that work:
 * from another thread, that races with a request in progress and can rewind
 * input that the request already consumed.
 */
static void _close_work(conmgr_callback_args_t conmgr_args, void *arg)
{
	conmgr_con_queue_close(conmgr_args.ref);
}

static int _close_session(void *x, void *arg)
{
	persist_conn_ctx_t *pcon_ctx = x;

	xassert(pcon_ctx->magic == PERSIST_CONN_CTX_MAGIC);

	conmgr_con_add_work_fifo(pcon_ctx->ref, _close_work, NULL);
	return SLURM_SUCCESS;
}

extern void rpc_mgr_close_sessions(void)
{
	int count;

	slurm_mutex_lock(&sessions_mutex);
	/*
	 * Refuse new requests from now on, under sessions_mutex so _on_session_msg()
	 * cannot slip a request between the close below and the wait.
	 */
	have_control = false;
	if ((count = list_count(sessions))) {
		debug("Closing %d sessions opened while in control", count);
		(void) list_for_each_ro(sessions, _close_session, NULL);
	}

	/*
	 * Wait until each has run its fini, but not for sessions processing a
	 * request, which includes the caller's own: a DBD_SHUTDOWN request that
	 * got here cannot finish while it waits.
	 */
	while (list_count(sessions) > active_session_msgs)
		slurm_cond_wait(&sessions_cond, &sessions_mutex);
	slurm_mutex_unlock(&sessions_mutex);
}

static void _connection_fini_callback(persist_conn_ctx_t *pcon_ctx)
{
	slurmdbd_conn_t *dbd_conn = pcon_ctx->arg;
	bool stay_locked = false;
	int tries = 0;

	/*
	 * If we are sending updates to this ctld, it is holding
	 * pcon_send_lock and is in slurm_persist_conn_open() -- either
	 * still inside connect() (pcon_send->last_fd not yet published)
	 * or already past it and sitting in poll() on the pcon_send
	 * socket. The ctld has disconnected (that is why we are here)
	 * so the socket will never get a reply. Wake the push by
	 * shutting down the published fd. Clearing PERSIST_FLAG_RECONNECT
	 * first prevents _slurm_persist_recv_msg() from reopening the
	 * connection and re-wedging on a fresh fd. The push errors out
	 * of slurm_persist_conn_open(), releases pcon_send_lock, and we
	 * proceed with destroy.
	 *
	 * Reading pcon_send->last_fd lock-free is safe because it is a
	 * scalar published by _open_persist_conn() and cleared to -1 by
	 * _conn_destroy() before the conn struct is freed -- so unlike
	 * dereferencing pcon_send->conn, this cannot land on a freed
	 * TLS conn struct.
	 *
	 * Loop while we cannot acquire the lock: last_fd is only valid
	 * once the socket has been created, so during connect() it may
	 * be -1 for up to a TCP SYN timeout. Re-attempt the shutdown
	 * each pass so we wake the push as soon as the fd is published.
	 */
	if (dbd_conn->pcon_send)
		dbd_conn->pcon_send->flags &= ~PERSIST_FLAG_RECONNECT;
	while (pthread_mutex_trylock(&dbd_conn->pcon_send_lock) == EBUSY) {
		if (dbd_conn->pcon_send) {
			int fd = dbd_conn->pcon_send->last_fd;
			if (fd > 0) {
				debug("Terminating send to the slurmctld on cluster %s. It is restarting or shutting down.",
				      pcon_ctx->cluster_name);
				shutdown(fd, SHUT_RDWR);
			}
		}
		if (tries++ >= 100) {
			slurm_mutex_lock(&dbd_conn->pcon_send_lock);
			break;
		}
		usleep(10000); /* 10ms */
	}

	slurm_persist_conn_destroy(dbd_conn->pcon_send);
	dbd_conn->pcon_send = NULL;
	slurm_mutex_unlock(&dbd_conn->pcon_send_lock);

	if (dbd_conn->rem_port) {
		if (!shutdown_time) {
			slurmdb_cluster_rec_t cluster_rec;
			memset(&cluster_rec, 0, sizeof(slurmdb_cluster_rec_t));
			cluster_rec.name = pcon_ctx->cluster_name;
			cluster_rec.control_host = pcon_ctx->rem_host;
			cluster_rec.control_port = dbd_conn->rem_port;
			cluster_rec.rpc_version = pcon_ctx->version;
			cluster_rec.tres_str = dbd_conn->tres_str;
			if (dbd_conn->flags & PERSIST_FLAG_EXT_DBD)
				cluster_rec.flags = CLUSTER_FLAG_EXT;
			debug("cluster %s has disconnected",
			      pcon_ctx->cluster_name);

			clusteracct_storage_g_fini_ctld(
				dbd_conn->db_conn, &cluster_rec);
		} else if (slurmdbd_conf->commit_delay)
			stay_locked = true;

		/*
		 * On connection close, remove from the list of registered
		 * clusters. The List ensures acct_storage_g_commit() is run
		 * every CommitDelay interval, but the final commit is handled
		 * below.
		 */
		slurm_mutex_lock(&registered_lock);
		if (registered_clusters)
			list_delete_ptr(registered_clusters, pcon_ctx);
		if (!stay_locked)
			slurm_mutex_unlock(&registered_lock);

		/* needs to be the last thing done */
		acct_storage_g_commit(dbd_conn->db_conn, 1);
	}

	acct_storage_g_close_connection(&dbd_conn->db_conn);

	if (stay_locked)
		slurm_mutex_unlock(&registered_lock);

	slurm_mutex_destroy(&dbd_conn->pcon_send_lock);
	slurmdbd_conn_members_destroy(dbd_conn);
	xfree(dbd_conn);
}
