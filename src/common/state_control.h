/*****************************************************************************\
 *  state_control.h - state control common functions
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
 *  Written by Alejandro Sanchez <alex@schedmd.com>
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

#ifndef _STATE_CONTROL_H
#define _STATE_CONTROL_H

#include "slurm/slurm.h"

/*
 * Parse and process reservation request option NodeCnt= or TRES=node=
 *
 * IN/OUT resv_msg_ptr - msg where node_cnt member is modified
 * IN val - NodeCnt value to parse
 * IN from_tres - used to discern if the count comes from TRES= or NodeCnt=
 * OUT err_msg - set to an explanation of failure, if any. Don't set if NULL
 */
extern int _parse_resv_node_cnt(resv_desc_msg_t *resv_msg_ptr, char *val,
				bool from_tres, char **err_msg);

#endif /* !_STATE_CONTROL_H */
