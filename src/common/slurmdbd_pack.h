/****************************************************************************\
 *  slurmdbd_pack.h - functions to pack SlurmDBD RPCs
 *****************************************************************************
 *  Copyright (C) 2011-2018 SchedMD LLC
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

#ifndef _SLURMDBD_PACK_H
#define _SLURMDBD_PACK_H

#include "src/common/pack.h"
#include "src/common/slurmdbd_defs.h"

#define MAX_DBD_MSG_LEN         16384

/*****************************************************************************\
 * Pack various SlurmDBD message structures into a buffer
\*****************************************************************************/

extern void slurmdbd_pack_id_rc_msg(void *in, uint16_t rpc_version,
				    buf_t *buffer);

extern int slurmdbd_unpack_id_rc_msg(void **msg, uint16_t rpc_version,
				     buf_t *buffer);

extern void slurmdbd_pack_usage_msg(dbd_usage_msg_t *msg,
				    uint16_t rpc_version,
				    slurmdbd_msg_type_t type,
				    buf_t *buffer);
extern int slurmdbd_unpack_usage_msg(dbd_usage_msg_t **msg,
				     uint16_t rpc_version,
				     slurmdbd_msg_type_t type,
				     buf_t *buffer);

extern void slurmdbd_pack_fini_msg(dbd_fini_msg_t *msg, uint16_t rpc_version,
				   buf_t *buffer);
extern int slurmdbd_unpack_fini_msg(dbd_fini_msg_t **msg, uint16_t rpc_version,
				    buf_t *buffer);

extern void slurmdbd_pack_list_msg(dbd_list_msg_t *msg, uint16_t rpc_version,
				   slurmdbd_msg_type_t type, buf_t *buffer);
extern int slurmdbd_unpack_list_msg(dbd_list_msg_t **msg, uint16_t rpc_version,
				    slurmdbd_msg_type_t type, buf_t *buffer);

extern buf_t *pack_slurmdbd_msg(persist_msg_t *req, uint16_t rpc_version);
extern int unpack_slurmdbd_msg(persist_msg_t *resp, uint16_t rpc_version,
			       buf_t *buffer);

#endif	/* !_SLURMDBD_PACK_H */
