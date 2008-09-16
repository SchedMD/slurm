/*****************************************************************************\
 *  proc_req.h - functions and definitions for processing incoming RPCs.
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

#ifndef _PROC_REQ_H
#define _PROC_REQ_H

#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"

typedef struct {
	void *db_conn; /* database connection */
	slurm_fd newsockfd; /* socket connection descriptor */
	uint16_t rpc_version; /* version of rpc */
} slurmdbd_conn_t;

/* Process an incoming RPC
 * slurmdbd_conn IN/OUT - in will that the newsockfd set before
 *       calling and db_conn and rpc_version will be filled in with the init.
 * msg IN - incoming message
 * msg_size IN - size of msg in bytes
 * first IN - set if first message received on the socket
 * buffer OUT - outgoing response, must be freed by caller
 * uid IN/OUT - user ID who initiated the RPC
 * RET SLURM_SUCCESS or error code */
extern int proc_req(slurmdbd_conn_t *slurmdbd_conn, char *msg, 
		    uint32_t msg_size, bool first, Buf *out_buffer, 
		    uint32_t *uid);

#endif /* !_PROC_REQ */
