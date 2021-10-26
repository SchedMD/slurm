/****************************************************************************\
 *  slurm_protocol_pack.h - definitions for all pack and unpack functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>.
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

#ifndef _SLURM_PROTOCOL_PACK_H
#define _SLURM_PROTOCOL_PACK_H

#include <inttypes.h>

#include "src/common/pack.h"
#include "src/common/slurm_protocol_defs.h"

typedef void (*pack_function_t) (void *object,
				 uint16_t protocol_version,
				 buf_t *buffer);

/****************************/
/* Message header functions */
/****************************/

/*
 * packs a slurm protocol header that precedes every slurm message
 * IN header - the header structure to pack
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 */
extern void pack_header(header_t *header, buf_t *buffer);

/*
 * unpacks a slurm protocol header that precedes every slurm message
 * OUT header - the header structure to unpack
 * IN/OUT buffer - source of the unpack data, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
extern int unpack_header(header_t *header, buf_t *buffer);


/**************************************************************************/
/* generic case statement Pack / Unpack methods for slurm protocol bodies */
/**************************************************************************/

/*
 * packs a generic slurm protocol message body
 * IN msg - the body structure to pack (note: includes message type)
 * IN/OUT buffer - destination of the pack, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
extern int pack_msg(slurm_msg_t const *msg, buf_t *buffer);

/*
 * unpacks a generic slurm protocol message body
 * OUT msg - the body structure to unpack (note: includes message type)
 * IN/OUT buffer - source of the unpack, contains pointers that are
 *			automatically updated
 * RET 0 or error code
 */
extern int unpack_msg(slurm_msg_t *msg, buf_t *buffer);

extern int slurm_pack_list(List send_list,
			   void (*pack_function) (void *object,
						  uint16_t rpc_version,
						  buf_t *buffer),
			   buf_t *buffer, uint16_t protocol_version);
extern int slurm_pack_list_until(List send_list, pack_function_t pack_function,
				 buf_t *buffer, uint32_t max_buf_size,
				 uint16_t protocol_version);
extern int slurm_unpack_list(List *recv_list,
			     int (*unpack_function) (void **object,
						     uint16_t protocol_version,
						     buf_t *buffer),
			     void (*destroy_function) (void *object),
			     buf_t *buffer, uint16_t protocol_version);

extern void pack_dep_list(List dep_list, buf_t *buffer,
			  uint16_t protocol_version);
extern int unpack_dep_list(List *dep_list, buf_t *buffer,
			   uint16_t protocol_version);

extern void pack_multi_core_data(multi_core_data_t *multi_core, buf_t *buffer,
				 uint16_t protocol_version);
extern int unpack_multi_core_data(multi_core_data_t **multi_core, buf_t *buffer,
				  uint16_t protocol_version);

extern void pack_config_response_msg(config_response_msg_t *msg,
				     buf_t *buffer, uint16_t protocol_version);
extern int unpack_config_response_msg(config_response_msg_t **msg_ptr,
				      buf_t *buffer, uint16_t protocol_version);

extern void pack_step_id(slurm_step_id_t *msg, buf_t *buffer,
			 uint16_t protocol_version);
extern int unpack_step_id_members(slurm_step_id_t *msg, buf_t *buffer,
				  uint16_t protocol_version);
extern int unpack_step_id(slurm_step_id_t **msg_ptr, buf_t *buffer,
			  uint16_t protocol_version);

/*
 * Remove these 2 functions pack_old_step_id and convert_old_step_id 2 versions
 * after 20.11.  They are only here for convenience and will no longer be needed
 * after 2 versions after 20.11.
 */
extern void pack_old_step_id(uint32_t step_id, buf_t *buffer);
extern void convert_old_step_id(uint32_t *step_id);

extern void slurm_pack_selected_step(void *in, uint16_t protocol_version,
				     buf_t *buffer);
extern int slurm_unpack_selected_step(slurm_selected_step_t **step,
				      uint16_t protocol_version, buf_t *buffer);

#endif
