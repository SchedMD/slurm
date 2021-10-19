/****************************************************************************\
 *  slurmdbd_agent.h - functions to manage the connection to the SlurmDBD
 *****************************************************************************
 *  Copyright (C) 2011-2018 SchedMD LLC.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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

#ifndef _SLURMDBD_AGENT_H
#define _SLURMDBD_AGENT_H

#include "dbd_conn.h"
#include "src/common/assoc_mgr.h"

extern slurm_persist_conn_t *slurmdbd_conn;

/* Set the slurmdbd_conn we will use for the agent */
extern void slurmdbd_agent_set_conn(slurm_persist_conn_t *pc);
/* Shut down the agent */
extern void slurmdbd_agent_rem_conn(void);

/*
 * Send an RPC to the SlurmDBD and wait for an arbitrary reply message.
 * This stops the agent and sends the message.
 * The RPC will not be queued if an error occurs.
 * The "resp" message must be freed by the caller.
 * Returns SLURM_SUCCESS or an error code
 */
extern int slurmdbd_agent_send_recv(uint16_t rpc_version,
				    persist_msg_t *req,
				    persist_msg_t *resp);

/* Send an RPC to the SlurmDBD. Do not wait for the reply. The RPC
 * will be queued and processed later if the SlurmDBD is not responding.
 * NOTE: slurm_open_slurmdbd_conn() must have been called with make_agent set
 *
 * Returns SLURM_SUCCESS or an error code */
extern int slurmdbd_agent_send(uint16_t rpc_version, persist_msg_t *req);

/* Return true if connection to slurmdbd is active, false otherwise. */
extern bool slurmdbd_conn_active(void);

/* Return the number of messages waiting to be sent to the DBD */
extern int slurmdbd_agent_queue_count(void);

/* set up local variables based on slurm.conf params */
extern void slurmdbd_agent_config_setup(void);

#endif
