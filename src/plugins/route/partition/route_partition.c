/*****************************************************************************\
 *  route_partition.c - partition version of route plugin
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Dominik Bartkiewicz <bart@schedmd.com>
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

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/bitstring.h"
#include "src/common/forward.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/route.h"

#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/locks.h"

/*
 * These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined. They will get
 * overwritten when linking with the slurmctld.
 */

#if defined (__APPLE__)
extern int node_record_count __attribute__((weak_import));
extern List part_list __attribute__((weak_import));
#else
int node_record_count;
List part_list = NULL;
#endif


/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "route partition plugin";
const char plugin_type[]        = "route/partition";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

typedef struct {
	int *count;
	bitstr_t *fwd_bitmap;
	int msg_count;
	bitstr_t *nodes_bitmap;
	hostlist_t **sp_hl;
} _foreach_part_split_hostlist_t;

/*****************************************************************************\
 *  Functions required of all plugins
\*****************************************************************************/
/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	return SLURM_SUCCESS;
}

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

/*****************************************************************************\
 *  Plugin API Implementations
\*****************************************************************************/

/*
 * route_p_split_hostlist  - logic to split an input hostlist into
 *                           a set of hostlists to forward to.
 *
 * IN: hl        - hostlist_t *   - list of every node to send message to
 *                                  will be empty on return;
 * OUT: sp_hl    - hostlist_t *** - the array of hostlist that will be malloced
 * OUT: count    - int *          - the count of created hostlist
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_p_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				  int* count, uint16_t tree_width)
{
	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK, .part = READ_LOCK
	};
	bitstr_t *nodes_bitmap = NULL;
	_foreach_part_split_hostlist_t part_split;

	if (!running_in_slurmctld())
		return route_split_hostlist_treewidth(hl, sp_hl, count,
						      tree_width);

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
		size_t new_size = xsize(*sp_hl);
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

/*
 * route_g_reconfigure - reset during reconfigure
 *
 * RET: SLURM_SUCCESS - int
 */
extern int route_p_reconfigure(void)
{
	return SLURM_SUCCESS;
}
