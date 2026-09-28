/*****************************************************************************\
 *  gres_links.h - link-topology aware GRES selection for the select plugin
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#ifndef _GRES_LINKS_H
#define _GRES_LINKS_H

#include "src/interfaces/gres.h"

/*
 * Shrink the counts to the devices that can be part of a large enough linked
 * set, leaving a node that cannot form one too short for the caller to accept.
 * The counts are capped to the node's largest linked set either way.
 * IN/OUT sock_gres - GRES availability counts, shrunk in place
 * IN gres_ns - node GRES state providing the links_cnt matrix
 * IN min_gres - smallest linked set that satisfies the request
 * IN/OUT near_gres_cnt - GRES near allocated CPUs, shrunk to match
 */
extern void gres_links_filter(sock_gres_t *sock_gres,
			      gres_node_state_t *gres_ns, uint64_t min_gres,
			      uint64_t *near_gres_cnt);

/*
 * Test whether a pick has to be made from a linked set. A job that ends up with
 * a single device on this node is not constrained, since a lone device has
 * nothing to be linked to.
 * IN sock_gres - GRES availability for this node
 * IN node_inx - node index
 * IN gres_needed - number of devices this call is to select
 * RET true if the devices must be picked from a linked set
 */
extern bool gres_links_required(sock_gres_t *sock_gres, int node_inx,
				uint64_t gres_needed);

/*
 * Select up to gres_needed devices for the job on this node, requiring every
 * device to be linked to those already selected and to each other; picks fewer
 * (failing the node) when no large enough linked set is available.
 * IN sock_gres - GRES availability for this node
 * IN node_inx - node index
 * IN gres_needed - number of devices this call is to select
 * IN sock_bits - devices this call may hand over, for a caller bound to one
 *                socket, or NULL when it may hand over any of the node's. The
 *                linked set is looked for over the whole node either way.
 * RET count of devices selected
 */
extern uint64_t gres_links_select(sock_gres_t *sock_gres, int node_inx,
				  uint64_t gres_needed, bitstr_t *sock_bits);

#endif /* _GRES_LINKS_H */
