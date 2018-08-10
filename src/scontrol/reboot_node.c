/*****************************************************************************\
 *  reboot_node.c - scontrol reboot functionality
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

#include "slurm.h"

#include "src/scontrol/scontrol.h"

extern int scontrol_cancel_reboot(char *nodes)
{
	int rc = SLURM_SUCCESS;
	update_node_msg_t node_msg;

	slurm_init_update_node_msg(&node_msg);

	node_msg.node_names = nodes;
	node_msg.node_state = NODE_STATE_CANCEL_REBOOT;

	if (slurm_update_node(&node_msg)) {
		exit_code = 1;
		rc = slurm_get_errno();
		slurm_perror ("slurm_update error");
	}

	return rc;
}
