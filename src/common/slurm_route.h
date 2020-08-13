/*****************************************************************************\
 *  slurm_route.h - Define route plugin functions.
 *****************************************************************************
 *  Copyright (C) 2014 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Rod Schultz <rod.schultz@bull.com>
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

#ifndef __SLURM_ROUTE_PLUGIN_API_H__
#define __SLURM_ROUTE_PLUGIN_API_H__

/*****************************************************************************\
 *  Functions required of all plugins
\*****************************************************************************/

/*
 * Initialize the route plugin.
 *
 * Returns a Slurm errno.
 */
extern int route_init(void);

/*
 * Terminate the route plugin.
 *
 * Returns a Slurm errno.
 */
extern int route_fini(void);

/*****************************************************************************\
 *  Plugin API Declarations
\*****************************************************************************/

/*
 * route_g_split_hostlist - logic to split an input hostlist into
 *                           a set of hostlists to forward to.
 *
 * IN: hl        - hostlist_t   - list of every node to send message to
 *                                will be empty on return which is same behavior
 *                                as similar code replaced in forward.c
 * OUT: sp_hl    - hostlist_t** - the array of hostlists that will be malloced
 * OUT: count    - int*         - the count of created hostlists
 * IN: tree_width- int          - Max width of each branch on the tree.
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_g_split_hostlist(hostlist_t hl,
				  hostlist_t** sp_hl,
				  int* count, uint16_t tree_width);

/*
 * route_g_reconfigure - reset during reconfigure
 *
 * RET: SLURM_SUCCESS - int
 */
extern int route_g_reconfigure(void);

/*****************************************************************************\
 *  Plugin Common Functions
\*****************************************************************************/

/*
 * route_split_hostlist_treewidth - logic to split an input hostlist into
 *                                   a set of hostlists to forward to.
 *
 * This is the default behavior. It is implemented here as there are cases
 * where the topology version also needs to split the message list based
 * on TreeWidth.
 *
 * IN: hl        - hostlist_t   - list of every node to send message to
 *                                will be empty on return which is same behavior
 *                                as similar code replaced in forward.c
 * OUT: sp_hl    - hostlist_t** - the array of hostlists that will be malloced
 * OUT: count    - int*         - the count of created hostlists
 * IN: tree_width- int          - Max width of each branch on the tree.
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_split_hostlist_treewidth(hostlist_t hl,
					  hostlist_t** sp_hl,
					  int* count, uint16_t tree_width);

#endif /*___SLURM_ROUTE_PLUGIN_API_H__*/
