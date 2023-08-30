/*****************************************************************************\
 *  state_control.h - state control common functions
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
 *  Written by Alejandro Sanchez <alex@schedmd.com>
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

#ifndef _STATE_CONTROL_H
#define _STATE_CONTROL_H

#include "slurm/slurm.h"

/*
 * IN watts - resv_watts to convert to string format
 */
extern char *state_control_watts_to_str(uint32_t watts);

/*
 * Parse reservation request option Watts
 * IN watts_str - value to parse
 * IN/OUT resv_msg_ptr - msg where resv_watts member is modified
 * OUT err_msg - set to an explanation of failure, if any. Don't set if NULL
 */
extern uint32_t state_control_parse_resv_watts(char * watts_str,
					       resv_desc_msg_t *resv_msg_ptr,
					       char **err_msg);

/*
 * RET SLURM_SUCCESS if 'type' is a configured TRES.
 */
extern int state_control_configured_tres(char *type);

/*
 * RET SLURM_SUCCESS if SelectType includes select/cons_res or if
 * SelectTypeParameters includes OTHER_CONS_RES on a Cray.
 */
extern int state_control_corecnt_supported(void);

/*
 * Parse and process reservation request option CoreCnt= or TRES=cpu=
 *
 * IN/OUT resv_msg_ptr - msg where core_cnt member is modified
 * IN val - CoreCnt value to parse
 * OUT res_free_flags - uint32_t of flags to set various bits to free strings
 *                      afterwards if needed.
 *                      See RESV_FREE_STR_* in src/common/slurm_protocol_defs.h.
 * IN from_tres - used to discern if the count comes from TRES= or CoreCnt=
 * OUT err_msg - set to an explanation of failure, if any. Don't set if NULL
 */
extern int state_control_parse_resv_corecnt(resv_desc_msg_t *resv_msg_ptr,
					    char *val, uint32_t *res_free_flags,
					    bool from_tres, char **err_msg);

/*
 * Parse and process reservation request option NodeCnt= or TRES=node=
 *
 * IN/OUT resv_msg_ptr - msg where node_cnt member is modified
 * IN val - NodeCnt value to parse
 * OUT res_free_flags - uint32_t of flags to set various bits to free strings
 *                      afterwards if needed.
 *                      See RESV_FREE_STR_* in src/common/slurm_protocol_defs.h.
 * IN from_tres - used to discern if the count comes from TRES= or NodeCnt=
 * OUT err_msg - set to an explanation of failure, if any. Don't set if NULL
 */
extern int parse_resv_nodecnt(resv_desc_msg_t *resv_msg_ptr, char *val,
			      uint32_t *res_free_flags, bool from_tres,
			      char **err_msg);


/*
 * Parse and process reservation request option TRES=
 * IN val - TRES value to parse
 * IN/OUT resv_msg_ptr - msg whose members might be modified
 * OUT res_free_flags - uint32_t of flags to set various bits to free strings
 *                      afterwards if needed.
 *                      See RESV_FREE_STR_* in src/common/slurm_protocol_defs.h.
 * OUT err_msg - set to an explanation of failure, if any. Don't set if NULL
 */
extern int state_control_parse_resv_tres(char *val,
					 resv_desc_msg_t *resv_msg_ptr,
					 uint32_t *res_free_flags,
					 char **err_msg);

#endif /* !_STATE_CONTROL_H */
