/*****************************************************************************\
 *  rpc_mgr.h - functions for processing RPCs.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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

#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/slurmdbd/proc_req.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmdbd/rpc_mgr.h"
#include "src/slurmdbd/slurmdbd.h"

#define MAX_THREAD_COUNT 100

/*
 *  Maximum message size. Messages larger than this value (in bytes)
 *  will not be received.
 */
#define MAX_MSG_SIZE     (16*1024*1024)

/* Local functions */
static void _connection_fini_callback(void *arg);

/* Local variables */
static pthread_t       master_thread_id = 0;

/* Process incoming RPCs. Meant to execute as a pthread */
extern void *rpc_mgr(void *no_data)
{
	int sockfd, newsockfd;
	int i;
	uint16_t port;
	slurm_addr_t cli_addr;
	slurmdbd_conn_t *conn_arg = NULL;

	master_thread_id = pthread_self();

	/* initialize port for RPCs */
	if ((sockfd = slurm_init_msg_engine_port(get_dbd_port()))
	    == SLURM_SOCKET_ERROR)
		fatal("slurm_init_msg_engine_port error %m");

	slurm_persist_conn_recv_server_init();

	/*
	 * Process incoming RPCs until told to shutdown
	 */
	while (!shutdown_time &&
	       (i = slurm_persist_conn_wait_for_thread_loc()) >= 0) {
		/*
		 * accept needed for stream implementation is a no-op in
		 * message implementation that just passes sockfd to newsockfd
		 */
		if ((newsockfd = slurm_accept_msg_conn(sockfd,
						       &cli_addr)) ==
		    SLURM_SOCKET_ERROR) {
			slurm_persist_conn_free_thread_loc(i);
			if (errno != EINTR)
				error("slurm_accept_msg_conn: %m");
			continue;
		}
		fd_set_nonblocking(newsockfd);

		conn_arg = xmalloc(sizeof(slurmdbd_conn_t));
		conn_arg->conn = xmalloc(sizeof(slurm_persist_conn_t));
		conn_arg->conn->fd = newsockfd;
		conn_arg->conn->flags = PERSIST_FLAG_DBD;
		conn_arg->conn->callback_proc = proc_req;
		conn_arg->conn->callback_fini = _connection_fini_callback;
		conn_arg->conn->shutdown = &shutdown_time;
		conn_arg->conn->version = SLURM_MIN_PROTOCOL_VERSION;
		conn_arg->conn->rem_host = xmalloc_nz(sizeof(char) * 16);
		/* Don't fill in the rem_port here.  It will be filled in
		 * later if it is a slurmctld connection. */
		slurm_get_ip_str(&cli_addr, &port,
				 conn_arg->conn->rem_host, sizeof(char) * 16);

		slurm_persist_conn_recv_thread_init(
			conn_arg->conn, i, conn_arg);
	}

	debug("rpc_mgr shutting down");
	(void) slurm_shutdown_msg_engine(sockfd);
	pthread_exit((void *) 0);
	return NULL;
}

/* Wake up the RPC manager and all spawned threads so they can exit */
extern void rpc_mgr_wake(void)
{
	if (master_thread_id)
		pthread_kill(master_thread_id, SIGUSR1);
	slurm_persist_conn_recv_server_fini();
}

static void _connection_fini_callback(void *arg)
{
	slurmdbd_conn_t *conn = (slurmdbd_conn_t *) arg;

	if (conn->conn->rem_port) {
		if (!shutdown_time) {
			slurmdb_cluster_rec_t cluster_rec;
			ListIterator itr;
			slurmdbd_conn_t *slurmdbd_conn;
			memset(&cluster_rec, 0, sizeof(slurmdb_cluster_rec_t));
			cluster_rec.name = conn->conn->cluster_name;
			cluster_rec.control_host = conn->conn->rem_host;
			cluster_rec.control_port = conn->conn->rem_port;
			cluster_rec.tres_str = conn->tres_str;
			debug("cluster %s has disconnected",
			      conn->conn->cluster_name);

			clusteracct_storage_g_fini_ctld(
				conn->db_conn, &cluster_rec);

			slurm_mutex_lock(&registered_lock);
			itr = list_iterator_create(registered_clusters);
			while ((slurmdbd_conn = list_next(itr))) {
				if (conn == slurmdbd_conn) {
					list_delete_item(itr);
					break;
				}
			}
			list_iterator_destroy(itr);
			slurm_mutex_unlock(&registered_lock);
		}
		/* needs to be the last thing done */
		acct_storage_g_commit(conn->db_conn, 1);
	}

	acct_storage_g_close_connection(&conn->db_conn);
	/* handled directly in the internal persist_conn code */
	//slurm_persist_conn_members_destroy(&conn->conn);
	xfree(conn->tres_str);
	xfree(conn);
}
