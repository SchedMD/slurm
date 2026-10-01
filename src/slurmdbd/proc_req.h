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
#include "src/common/slurmdbd_pack.h"

/*
 * slurmdbd's own state for one persistent connection, at pcon_ctx->arg.
 * The peer, identity, version and REQUEST_PERSIST_INIT details are in the
 * persist_conn_ctx_t and in each request's slurm_msg_t.
 */
typedef struct {
	/*
	 * Port the peer slurmctld listens on, set by DBD_REGISTER_CTLD and
	 * zeroed to drop the registration. Zero for other clients.
	 */
	uint16_t rem_port;

	/* Authenticated identity and level, cached at REQUEST_PERSIST_INIT. */
	slurm_msg_auth_t auth;

	/* PERSIST_FLAG_* bits applicable to this connection. */
	uint16_t flags;

	persist_conn_t *pcon_send;
	pthread_mutex_t pcon_send_lock;
	void *db_conn; /* database connection */
	char *tres_str;
} slurmdbd_conn_t;

#define SLURMDBD_CONN_INITIALIZER \
	((slurmdbd_conn_t) { \
		.auth.uid = SLURM_AUTH_NOBODY, \
		.auth.gid = SLURM_AUTH_NOBODY, \
		.flags = PERSIST_FLAG_DBD, \
	})

/*
 * Initialize a slurmdbd_conn_t to SLURMDBD_CONN_INITIALIZER.
 * IN/OUT dbd_conn - connection to initialize
 */
extern void slurmdbd_conn_init(slurmdbd_conn_t *dbd_conn);

/* Free owned strings; teardown releases connections, credentials and struct. */
extern void slurmdbd_conn_members_destroy(slurmdbd_conn_t *dbd_conn);

typedef struct {
	uint16_t msg_type;
	/*
	 * Handle msg, replying through it as slurmctld handlers do.
	 * RET SLURM_SUCCESS or error. Access denied, authentication and
	 * version errors close the connection after the reply.
	 */
	int (*func)(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg);
} slurmdbd_rpc_t;

/* Find the handler for msg_type, or NULL. */
extern const slurmdbd_rpc_t *const find_rpc(uint16_t msg_type);

/*
 * Process a request on a slurmdbd connection, replying through msg.
 * IN slurmdbd_conn - connection; db_conn is opened by REQUEST_PERSIST_INIT
 * IN msg - request with identity, protocol version, msg_type and data
 * RET SLURM_SUCCESS or error code
 */
extern int proc_req(slurmdbd_conn_t *slurmdbd_conn, slurm_msg_t *msg);

#endif /* !_PROC_REQ */
