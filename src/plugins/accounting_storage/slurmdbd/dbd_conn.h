/****************************************************************************\
 *  dbd_conn.h - functions to manage the connection to the SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2011-2020 SchedMD LLC.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
 *  Written by Morris Jette <jette@schedmd.com>
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

#include "src/common/slurm_persist_conn.h"

/*
 * dbd_conn_open - Get a connection to the dbd.
 * IN/OUT - persist_conn_flags - Flags sent in, returns full set of flags on
 *                               success.
 * IN - cluster_name - Name of cluster you are coming from.
 * IN - rem_host - Host of dbd we are connecting to.
 * IN - rem_port - Port on host of DBD listening for connections.
 * RET - Nonnection to the dbd on SUCCESS, NULL otherwise.
 */
extern slurm_persist_conn_t *dbd_conn_open(uint16_t *persist_conn_flags,
					   char *cluster_name,
					   char *rem_host,
					   uint16_t rem_port);

/* reopen connection if needed */
extern int dbd_conn_check_and_reopen(slurm_persist_conn_t *pc);

/*
 * dbd_conn_close - Close and free memory of connection made from dbd_conn_open.
 */
extern void dbd_conn_close(slurm_persist_conn_t **pc);

/*
 * Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 *
 * No agent code is evaluated here
 *
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code
 */
extern int dbd_conn_send_recv_direct(uint16_t rpc_version,
				     persist_msg_t *req,
				     persist_msg_t *resp);


/*
 * Send an RPC to the SlurmDBD and wait for the return code reply.
 *
 * This handles agent as well as normal connections
 *
 * The RPC will not be queued if an error occurs.
 * Returns SLURM_SUCCESS or an error code
 */
extern int dbd_conn_send_recv_rc_msg(uint16_t rpc_version,
				     persist_msg_t *req,
				     int *resp_code);

/*
 * Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 *
 * This handles agent as well as normal connections
 *
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code
 */
extern int dbd_conn_send_recv(uint16_t rpc_version,
				  persist_msg_t *req,
				  persist_msg_t *resp);
