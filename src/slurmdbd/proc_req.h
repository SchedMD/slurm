/*****************************************************************************\
 *  proc_req.h - functions and definitions for processing incoming RPCs.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#ifndef _PROC_REQ_H
#define _PROC_REQ_H

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/persist_conn.h"
#include "src/common/slurm_protocol_defs.h"

typedef struct {
	persist_conn_t *pcon;
	int fd; /* underlying input fd, captured at accept for log/db ids */

	/* Peer identity (filled in from REQUEST_PERSIST_INIT). */
	char *cluster_name;
	char *rem_host;
	uint16_t rem_port;

	/* Negotiated protocol version. */
	uint16_t version;

	/*
	 * Auth state cached from REQUEST_PERSIST_INIT.
	 * auth_cred is borrowed from pcon, do not destroy it.
	 */
	void *auth_cred;
	uid_t auth_uid;
	gid_t auth_gid;
	bool auth_ids_set;

	/* PERSIST_FLAG_* bits applicable to this connection. */
	uint16_t flags;

	persist_conn_t *pcon_send;
	pthread_mutex_t pcon_send_lock;
	void *db_conn; /* database connection */
	char *tres_str;
} slurmdbd_conn_t;

/*
 * Safe defaults for a slurmdbd_conn_t.
 *
 * The connection starts out fail-closed: the peer has no identity until
 * REQUEST_PERSIST_INIT authenticates it, and a zeroed auth_uid would read
 * as root. fd is -1 so an unset descriptor cannot be mistaken for stdin.
 *
 * The caller still fills in fd, rem_host and the pcon members.
 */
#define SLURMDBD_CONN_INITIALIZER \
	((slurmdbd_conn_t) { \
		.fd = -1, \
		.auth_uid = SLURM_AUTH_NOBODY, \
		.auth_gid = SLURM_AUTH_NOBODY, \
		.flags = PERSIST_FLAG_DBD, \
		.version = SLURM_MIN_PROTOCOL_VERSION, \
	})

/*
 * Initialize a slurmdbd_conn_t to SLURMDBD_CONN_INITIALIZER.
 * IN/OUT dbd_conn - connection to initialize
 */
extern void slurmdbd_conn_init(slurmdbd_conn_t *dbd_conn);

/*
 * Free the members a slurmdbd_conn_t owns, not the struct itself.
 *
 * Does not touch pcon or pcon_send, which the persist_conn code owns.
 * auth_cred is borrowed from pcon and so is not destroyed here.
 *
 * IN/OUT dbd_conn - connection whose members are freed
 */
extern void slurmdbd_conn_members_destroy(slurmdbd_conn_t *dbd_conn);

/* Process an incoming RPC
 * slurmdbd_conn IN/OUT - in will that the newsockfd set before
 *       calling and db_conn and rpc_version will be filled in with the init.
 * msg IN - incoming message
 * msg_size IN - size of msg in bytes
 * first IN - set if first message received on the socket
 * buffer OUT - outgoing response, must be freed by caller
 * RET SLURM_SUCCESS or error code */
extern int proc_req(void *conn, persist_msg_t *msg, buf_t **out_buffer);

#endif /* !_PROC_REQ */
