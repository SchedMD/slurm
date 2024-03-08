/*****************************************************************************\
 *  power_node.c - node power functions for scontrol.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "src/scontrol/scontrol.h"

/*
 * Issue RPC to control node(s) power state
 *
 * IN node_list  - list of nodes to issue command to
 * IN power_up   - flag to indicate power up/down of nodes
 * IN asap       - ASAP option
 * IN force      - FORCE option
 *
 * RET SLURM_SUCCESS or a slurm error code
 */
extern int scontrol_power_nodes(char *node_list, bool power_up, bool asap,
				bool force)
{
	update_node_msg_t node_msg;

	xassert(!asap || !force);

	slurm_init_update_node_msg(&node_msg);

	node_msg.node_names = node_list;

	if (power_up)
		node_msg.node_state = NODE_STATE_POWER_UP;
	else
		node_msg.node_state = NODE_STATE_POWER_DOWN;

	if (!power_up && force)
		node_msg.node_state |= NODE_STATE_POWERED_DOWN;

	if (!power_up && asap)
		node_msg.node_state |= NODE_STATE_POWER_DRAIN;

	return slurm_update_node(&node_msg);
}
