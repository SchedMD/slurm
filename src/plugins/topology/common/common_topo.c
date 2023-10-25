/*****************************************************************************\
 *  common_topo.c - common functions for accounting storage
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include "common_topo.h"

#include "src/common/hostlist.h"
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

extern int common_topo_split_hostlist_treewidth(hostlist_t *hl,
						hostlist_t ***sp_hl,
						int *count, uint16_t tree_width)
{
	int host_count;
	int *span = NULL;
	char *name = NULL;
	char *buf;
	int nhl = 0;
	int j;

	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	host_count = hostlist_count(hl);
	span = set_span(host_count, tree_width);
	*sp_hl = xcalloc(MIN(tree_width, host_count), sizeof(hostlist_t *));

	while ((name = hostlist_shift(hl))) {
		(*sp_hl)[nhl] = hostlist_create(name);
		free(name);
		for (j = 0; span && (j < span[nhl]); j++) {
			name = hostlist_shift(hl);
			if (!name) {
				break;
			}
			hostlist_push_host((*sp_hl)[nhl], name);
			free(name);
		}
		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			buf = hostlist_ranged_string_xmalloc((*sp_hl)[nhl]);
			debug("ROUTE: ... sublist[%d] %s", nhl, buf);
			xfree(buf);
		}
		nhl++;
	}
	xfree(span);
	*count = nhl;

	return SLURM_SUCCESS;
}

extern int common_topo_get_node_addr(char *node_name, char **addr,
				     char **pattern)
{

#ifndef HAVE_FRONT_END
	if (find_node_record(node_name) == NULL)
		return SLURM_ERROR;
#endif

	*addr = xstrdup(node_name);
	*pattern = xstrdup("node");
	return SLURM_SUCCESS;
}

extern bool common_topo_route_tree(void)
{
	static int route_tree = -1;
	if (route_tree == -1) {
		if (xstrcasestr(slurm_conf.topology_param, "routetree"))
			route_tree = true;
		else
			route_tree = false;
	}

	return route_tree;
}
