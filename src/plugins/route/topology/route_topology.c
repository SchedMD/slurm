/*****************************************************************************\
 *  route_topology.c - topology version of route plugin (using bitmaps)
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

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/forward.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_route.h"
#include "src/common/slurm_topology.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern switch_record_t *switch_record_table __attribute__((weak_import));
extern int switch_record_cnt __attribute__((weak_import));
extern int switch_levels __attribute__((weak_import));
#else
node_record_t **node_record_table_ptr;
switch_record_t *switch_record_table = NULL;
int switch_record_cnt = 0;
int switch_levels = 0;
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
const char plugin_name[]        = "route topology plugin";
const char plugin_type[]        = "route/topology";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/* Global data */
static pthread_mutex_t route_lock = PTHREAD_MUTEX_INITIALIZER;
static bool run_in_slurmctld = false;

/*****************************************************************************\
 *  Functions required of all plugins
\*****************************************************************************/
/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	if (xstrcmp(slurm_conf.topology_plugin, "topology/tree"))
		fatal("ROUTE: route/topology requires topology/tree");

	run_in_slurmctld = running_in_slurmctld();
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

/*****************************************************************************\
 *  Local functions
\*****************************************************************************/

/*
 * _subtree_split_hostlist() split a hostlist into topology aware subhostlists
 *
 * IN/OUT nodes_bitmap - bitmap of all hosts that need to be sent
 * IN parent - location in switch_record_table
 * IN/OUT msg_count - running count of how many messages we need to send
 * IN/OUT sp_hl - array of subhostlists
 * IN/OUT count - position in sp_hl array
 */
static int _subtree_split_hostlist(bitstr_t *nodes_bitmap, int parent,
				   int *msg_count, hostlist_t **sp_hl,
				   int *count)
{
	int lst_count = 0, sw_count;
	bitstr_t *fwd_bitmap = NULL;		/* nodes in forward list */

	for (int i = 0; i < switch_record_table[parent].num_switches; i++) {
		int k = switch_record_table[parent].switch_index[i];

		if (!fwd_bitmap)
			fwd_bitmap = bit_copy(
				switch_record_table[k].node_bitmap);
		else
			bit_copybits(fwd_bitmap,
				     switch_record_table[k].node_bitmap);
		bit_and(fwd_bitmap, nodes_bitmap);
		sw_count = bit_set_count(fwd_bitmap);
		if (sw_count == 0) {
			continue; /* no nodes on this switch in message list */
		}
		(*sp_hl)[*count] = bitmap2hostlist(fwd_bitmap);
		/* Now remove nodes from this switch from message list */
		bit_and_not(nodes_bitmap, fwd_bitmap);
		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			char *buf;
			buf = hostlist_ranged_string_xmalloc((*sp_hl)[*count]);
			debug("ROUTE: ... sublist[%d] switch=%s :: %s",
			      i, switch_record_table[i].name, buf);
			xfree(buf);
		}
		(*count)++;
		lst_count += sw_count;
		if (lst_count == *msg_count)
			break; /* all nodes in message are in a child list */
	}
	*msg_count -= lst_count;

	FREE_NULL_BITMAP(fwd_bitmap);
	return lst_count;
}

/*****************************************************************************\
 *  API Implementations
\*****************************************************************************/
/*
 * route_p_split_hostlist - logic to split an input hostlist into
 *                           a set of hostlists to forward to.
 *
 * IN: hl        - hostlist_t   - list of every node to send message to
 *                                will be empty on return;
 * OUT: sp_hl    - hostlist_t** - the array of hostlists that will be malloced
 * OUT: count    - int*         - the count of created hostlists
 * RET: SLURM_SUCCESS - int
 *
 * Note: created hostlist will have to be freed independently using
 *       hostlist_destroy by the caller.
 * Note: the hostlist_t array will have to be xfree.
 */
extern int route_p_split_hostlist(hostlist_t hl,
				  hostlist_t** sp_hl,
				  int* count, uint16_t tree_width)
{
	int i, j, k, msg_count, switch_count;
	int s_first, s_last;
	char *buf;
	bitstr_t *nodes_bitmap = NULL;		/* nodes in message list */
	bitstr_t *switch_bitmap = NULL;		/* switches  */
	slurmctld_lock_t node_read_lock = { .node = READ_LOCK };

	slurm_mutex_lock(&route_lock);
	if (switch_record_cnt == 0) {
		if (run_in_slurmctld)
			fatal_abort("%s: Somehow we have 0 for switch_record_cnt and we are here in the slurmctld.  This should never happen.", __func__);
		/* configs have not already been processed */
		slurm_conf_init(NULL);
		init_node_conf();
		build_all_nodeline_info(false, 0);
		rehash_node();

		if (slurm_topo_build_config() != SLURM_SUCCESS) {
			fatal("ROUTE: Failed to build topology config");
		}
	}
	slurm_mutex_unlock(&route_lock);
	/* Only acquire the slurmctld lock if running as the slurmctld. */
	if (run_in_slurmctld)
		lock_slurmctld(node_read_lock);
	/* create bitmap of nodes to send message too */
	if (hostlist2bitmap(hl, false, &nodes_bitmap) != SLURM_SUCCESS) {
		buf = hostlist_ranged_string_xmalloc(hl);
		fatal("ROUTE: Failed to make bitmap from hostlist=%s.", buf);
	}
	if (run_in_slurmctld)
		unlock_slurmctld(node_read_lock);

	/* Find lowest level switches containing all the nodes in the list */
	switch_bitmap = bit_alloc(switch_record_cnt);
	for (j = 0; j < switch_record_cnt; j++) {
		if ((switch_record_table[j].level == 0 ) &&
		    bit_overlap_any(switch_record_table[j].node_bitmap,
				    nodes_bitmap)) {
				bit_set(switch_bitmap, j);
		}
	}

	switch_count = bit_set_count(switch_bitmap);

	for (i = 1; i <= switch_levels; i++) {
		/* All nodes in message list are in one switch */
		if (switch_count < 2)
			break;
		for (j = 0; j < switch_record_cnt; j++) {
			if (switch_count < 2)
				break;
			if (switch_record_table[j].level == i) {
				int first_child = -1, child_cnt = 0, num_desc;
				num_desc = switch_record_table[j].
						num_desc_switches;
				for (k = 0; k < num_desc; k++) {
					int index = switch_record_table[j].
						switch_desc_index[k];
					if (bit_test(switch_bitmap, index)) {
						child_cnt++;
						if (child_cnt > 1) {
							bit_clear(switch_bitmap,
								  index);
						} else {
							first_child = index;
						}
					}
				}
				if (child_cnt > 1) {
					bit_clear(switch_bitmap, first_child);
					bit_set(switch_bitmap, j);
					switch_count -= (child_cnt - 1);
				}
			}
		}
	}

	s_first = bit_ffs(switch_bitmap);
	if (s_first != -1)
		s_last = bit_fls(switch_bitmap);
	else
		s_last = -2;

	if (switch_count == 1 && switch_record_table[s_first].level == 0 &&
	    bit_super_set(nodes_bitmap,
			  switch_record_table[s_first].node_bitmap)) {
		/* This is a leaf switch. Construct list based on TreeWidth */
		FREE_NULL_BITMAP(nodes_bitmap);
		FREE_NULL_BITMAP(switch_bitmap);
		return route_split_hostlist_treewidth(hl, sp_hl, count,
						      tree_width);
	}
	*sp_hl = xcalloc(switch_record_cnt, sizeof(hostlist_t));
	msg_count = hostlist_count(hl);
	*count = 0;
	for (j = s_first; j <= s_last; j++) {
		xassert(msg_count);

		if (!bit_test(switch_bitmap, j))
			continue;
		_subtree_split_hostlist(nodes_bitmap, j, &msg_count, sp_hl,
					count);
	}
	xassert(msg_count == bit_set_count(nodes_bitmap));
	if (msg_count) {
		size_t new_size = xsize(*sp_hl);
		int n_first, n_last;

		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			buf = bitmap2node_name(nodes_bitmap);
			debug("ROUTE: didn't find switch containing nodes=%s",
			      buf);
			xfree(buf);
		}
		new_size += msg_count * sizeof(hostlist_t);
		xrealloc(*sp_hl, new_size);

		n_first = bit_ffs(nodes_bitmap);
		if (n_first != -1)
			n_last = bit_fls(nodes_bitmap);
		else
			n_last = -2;
		for (j = n_first; j <= n_last; j++) {
			if (!bit_test(nodes_bitmap, j))
				continue;
			(*sp_hl)[*count] = hostlist_create(NULL);
			hostlist_push_host((*sp_hl)[*count],
					   node_record_table_ptr[j]->name);
			(*count)++;
		}
	}

	FREE_NULL_BITMAP(nodes_bitmap);
	FREE_NULL_BITMAP(switch_bitmap);

	return SLURM_SUCCESS;

}

/*
 * route_g_reconfigure - reset during reconfigure
 *
 * RET: SLURM_SUCCESS - int
 */
extern int route_p_reconfigure (void)
{
	return SLURM_SUCCESS;
}
