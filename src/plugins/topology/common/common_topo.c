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

#include "src/common/slurm_xlator.h"

#include "common_topo.h"

#include "src/common/bitstring.h"
#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/node_conf.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

/*
 * These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined. They will get
 * overwritten when linking with the slurmctld.
 */

#if defined (__APPLE__)
extern List part_list __attribute__((weak_import));
#else
List part_list = NULL;
#endif

typedef struct {
	int *count;
	bitstr_t *fwd_bitmap;
	int msg_count;
	bitstr_t *nodes_bitmap;
	hostlist_t **sp_hl;
} _foreach_part_split_hostlist_t;

static int _part_split_hostlist(void *x, void *y)
{
	part_record_t *part_ptr = x;
	_foreach_part_split_hostlist_t *arg = y;
	int fwd_count;

	if (!bit_overlap_any(part_ptr->node_bitmap, arg->nodes_bitmap))
		return 0;

	if (!arg->fwd_bitmap)
		arg->fwd_bitmap = bit_copy(part_ptr->node_bitmap);
	else
		bit_copybits(arg->fwd_bitmap, part_ptr->node_bitmap);

	bit_and(arg->fwd_bitmap, arg->nodes_bitmap);
	(arg->sp_hl)[*arg->count] = bitmap2hostlist(arg->fwd_bitmap);
	bit_and_not(arg->nodes_bitmap, arg->fwd_bitmap);
	fwd_count = bit_set_count(arg->fwd_bitmap);
	(*arg->count)++;
	arg->msg_count -= fwd_count;

	if (arg->msg_count == 0)
		return -1;

	return 0;
}

static int _route_part_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				      int *count, uint16_t tree_width)
{
	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK, .part = READ_LOCK
	};
	bitstr_t *nodes_bitmap = NULL;
	_foreach_part_split_hostlist_t part_split;

	xassert(running_in_slurmctld());

	lock_slurmctld(node_read_lock);
	/* create bitmap of nodes to send message too */
	if (hostlist2bitmap(hl, false, &nodes_bitmap) != SLURM_SUCCESS) {
		char *buf = hostlist_ranged_string_xmalloc(hl);
		fatal("ROUTE: Failed to make bitmap from hostlist=%s.", buf);
	}

	*sp_hl = xcalloc(list_count(part_list), sizeof(hostlist_t *));
	*count = 0;

	part_split = (_foreach_part_split_hostlist_t) {
		.count = count,
		.fwd_bitmap = NULL,
		.msg_count = hostlist_count(hl),
		.nodes_bitmap = nodes_bitmap,
		.sp_hl = *sp_hl,
	};

	list_for_each_ro(part_list, _part_split_hostlist, &part_split);

	FREE_NULL_BITMAP(part_split.fwd_bitmap);

	xassert(part_split.msg_count == bit_set_count(nodes_bitmap));
	if (part_split.msg_count) {
		size_t new_size = *count;
		node_record_t *node_ptr;

		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			char *buf = bitmap2node_name(nodes_bitmap);
			log_flag(ROUTE, "didn't find partition containing nodes=%s",
				 buf);
			xfree(buf);
		}
		new_size += part_split.msg_count * sizeof(hostlist_t *);
		xrealloc(*sp_hl, new_size);

		for (int i = 0;
		     (node_ptr = next_node_bitmap(nodes_bitmap, &i));
		     i++) {
			(*sp_hl)[*count] = hostlist_create(NULL);
			hostlist_push_host((*sp_hl)[*count], node_ptr->name);
			(*count)++;
		}
	}

	if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
		char *hl_str = hostlist_ranged_string_xmalloc(hl);
		log_flag(ROUTE, "hl: %s", hl_str);
		xfree(hl_str);
		for (int i = 0; i < *count; i++) {
			char *nodes =
				hostlist_ranged_string_xmalloc((*sp_hl)[i]);
			log_flag(ROUTE, "sp_hl[%d]: %s", i, nodes);
			xfree(nodes);
		}
	}

	unlock_slurmctld(node_read_lock);

	FREE_NULL_BITMAP(nodes_bitmap);
	FREE_NULL_BITMAP(part_split.fwd_bitmap);

	return SLURM_SUCCESS;
}

/* this is used to set how many nodes are going to be on each branch
 * of the tree.
 * IN total       - total number of nodes to send to
 * IN tree_width  - how wide the tree should be on each hop
 * RET int *	  - int array tree_width in length each space
 *		    containing the number of nodes to send to each hop
 *		    on the span.
 */
static int *_set_span(int total,  uint16_t tree_width)
{
	int *span = NULL;
	int left = total;
	int i = 0;

	if (tree_width == 0)
		tree_width = slurm_conf.tree_width;

	//info("span count = %d", tree_width);
	if (total <= tree_width) {
		return span;
	}

	span = xcalloc(tree_width, sizeof(int));

	while (left > 0) {
		for (i = 0; i < tree_width; i++) {
			if ((tree_width-i) >= left) {
				if (span[i] == 0) {
					left = 0;
					break;
				} else {
					span[i] += left;
					left = 0;
					break;
				}
			} else if (left <= tree_width) {
				if (span[i] == 0)
					left--;

				span[i] += left;
				left = 0;
				break;
			}

			if (span[i] == 0)
				left--;

			span[i] += tree_width;
			left -= tree_width;
		}
	}

	return span;
}

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

	if (running_in_slurmctld() && common_topo_route_part())
		return _route_part_split_hostlist(hl, sp_hl,
						  count, tree_width);

	if (!tree_width)
		tree_width = slurm_conf.tree_width;

	host_count = hostlist_count(hl);
	span = _set_span(host_count, tree_width);
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

extern bool common_topo_route_part(void)
{
	static int route_part = -1;
	if (route_part == -1) {
		if (xstrcasestr(slurm_conf.topology_param, "routepart"))
			route_part = true;
		else
			route_part = false;
	}

	return route_part;
}
