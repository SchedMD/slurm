/*****************************************************************************\
 *  common_topo.c - common functions for accounting storage
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

#ifndef _COMMON_TOPO_H
#define _COMMON_TOPO_H

#include <slurm/slurm.h>
#include "src/interfaces/topology.h"

/*
 * common_topo_split_hostlist_treewidth - logic to split an input hostlist into
 *                                  a set of hostlists to forward to.
 *
 * This is the default behavior. It is implemented here as there are cases
 * where the topology version also needs to split the message list based
 * on TreeWidth.
 *
 * IN: hl          - hostlist_t *   - list of every node to send message to
 *                                    will be empty on return which is same
 *                                    behavior as similar code replaced in
 *                                    forward.c
 * OUT: sp_hl      - hostlist_t *** - the array of hostlists that will be
 *                                    malloced
 * OUT: count      - int *          - the count of created hostlists
 * IN:  tree_width - int            - max width of each branch on the tree.
 * RET:              int            - the number of levels opened in the tree,
 *                                    or SLURM_ERROR
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int common_topo_split_hostlist_treewidth(
	hostlist_t *hl,
	hostlist_t ***sp_hl,
	int *count, uint16_t tree_width);

/*
 * common_topo_get_node_addr - Build node address and the associated pattern
 *      based on the topology information in default plugin, only use node name
 *      as the topology address.
 */
extern int common_topo_get_node_addr(char *node_name, char **addr,
				     char **pattern);

/*
 * common_topo_route_tree - Return true if TopologyParam=RouteTree, false
 *                          otherwise.
 */
extern bool common_topo_route_tree(void);

/*
 * common_topo_route_part - Return true if TopologyParam=RoutePart, false
 *                          otherwise.
 */
extern bool common_topo_route_part(void);

/*
 * This is an common step to be called from the select plugin in _select_nodes()
 * and call _eval_nodes() which is based on topology to tackle the knapsack
 * problem. This code incrementally removes nodes with low CPU counts for the
 * job and re-evaluates each result.
 *
 * RET SLURM_SUCCESS or an error code
 */
extern int common_topo_choose_nodes(topology_eval_t *topo_eval);

#endif

