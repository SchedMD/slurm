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
#include "src/common/slurm_topology.h"
#include "src/slurmctld/locks.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
struct switch_record *switch_record_table __attribute__((weak_import)) = NULL;
int switch_record_cnt __attribute__((weak_import)) = 0;
int switch_levels __attribute__((weak_import)) = 0;
#else
struct switch_record *switch_record_table = NULL;
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
static uint64_t debug_flags = 0;
static pthread_mutex_t route_lock = PTHREAD_MUTEX_INITIALIZER;

/*****************************************************************************\
 *  Functions required of all plugins
\*****************************************************************************/
/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	char *topotype;
	topotype = slurm_get_topology_plugin();
	if (xstrcasecmp(topotype,"topology/tree") != 0) {
		fatal("ROUTE: route/topology requires topology/tree");
	}
	xfree(topotype);
	debug_flags = slurm_get_debug_flags();
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

/* Only run when in the slurmctld */
static bool _run_in_daemon(void)
{
	static bool set = false;
	static bool run = false;

	if (!set) {
		set = 1;
		run = run_in_daemon("slurmctld");
	}

	return run;
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
	int i, j, k, hl_ndx, msg_count, sw_count, lst_count;
	char  *buf;
	bitstr_t *nodes_bitmap = NULL;		/* nodes in message list */
	bitstr_t *fwd_bitmap = NULL;		/* nodes in forward list */
	slurmctld_lock_t node_read_lock = { .node = READ_LOCK };

	msg_count = hostlist_count(hl);
	slurm_mutex_lock(&route_lock);
	if (switch_record_cnt == 0) {
		if (_run_in_daemon())
			fatal_abort("%s: Somehow we have 0 for switch_record_cnt and we are here in the slurmctld.  This should never happen.", __func__);
		/* configs have not already been processed */
		slurm_conf_init(NULL);
		if (init_node_conf()) {
			fatal("ROUTE: Failed to init slurm config");
		}
		if (build_all_nodeline_info(false, 0)) {
			fatal("ROUTE: Failed to build node config");
		}
		rehash_node();

		if (slurm_topo_build_config() != SLURM_SUCCESS) {
			fatal("ROUTE: Failed to build topology config");
		}
	}
	slurm_mutex_unlock(&route_lock);
	*sp_hl = (hostlist_t*) xmalloc(switch_record_cnt * sizeof(hostlist_t));
	/* Only acquire the slurmctld lock if running as the slurmctld. */
	if (_run_in_daemon())
		lock_slurmctld(node_read_lock);
	/* create bitmap of nodes to send message too */
	if (hostlist2bitmap (hl, false, &nodes_bitmap) != SLURM_SUCCESS) {
		buf = hostlist_ranged_string_xmalloc(hl);
		fatal("ROUTE: Failed to make bitmap from hostlist=%s.", buf);
	}
	if (_run_in_daemon())
		unlock_slurmctld(node_read_lock);

	/* Find lowest level switch containing all the nodes in the list */
	j = 0;
	for (i = 0; i <= switch_levels; i++) {
		for (j=0; j<switch_record_cnt; j++) {
			if (switch_record_table[j].level == i) {
				if (bit_super_set(nodes_bitmap,
						  switch_record_table[j].
						  node_bitmap)) {
					/* All nodes in message list are in
					 * this switch */
					break;
				}
			}
		}
		if (j < switch_record_cnt) {
			/* Got here via break after bit_super_set */
			break; // 'j' is our switch
		} /* else, no switches at this level reach all nodes */
	}
	if (i > switch_levels) {
		/* This can only happen if trying to schedule multiple physical
		 * clusters as a single logical cluster under the control of a
		 * single slurmctld daemon, and sending something like a
		 * node_registation request to all nodes.
		 * Revert to default behavior*/
		if (debug_flags & DEBUG_FLAG_ROUTE) {
			buf = hostlist_ranged_string_xmalloc(hl);
			debug("ROUTE: didn't find switch containing nodes=%s",
			      buf);
			xfree(buf);
		}
		FREE_NULL_BITMAP(nodes_bitmap);
		xfree(*sp_hl);
		return route_split_hostlist_treewidth(
			hl, sp_hl, count, tree_width);
	}
	if (switch_record_table[j].level == 0) {
		/* This is a leaf switch. Construct list based on TreeWidth */
		FREE_NULL_BITMAP(nodes_bitmap);
		xfree(*sp_hl);
		return route_split_hostlist_treewidth(
			hl, sp_hl, count, tree_width);
	}
	/* loop through children, construction a hostlist for each child switch
	 * with nodes in the message list */
	hl_ndx = 0;
	lst_count = 0;
	for (i=0; i < switch_record_table[j].num_switches; i++) {
		k = switch_record_table[j].switch_index[i];
		fwd_bitmap = bit_copy(switch_record_table[k].node_bitmap);
		bit_and(fwd_bitmap, nodes_bitmap);
		sw_count = bit_set_count(fwd_bitmap);
		if (sw_count == 0) {
			continue; /* no nodes on this switch in message list */
		}
		(*sp_hl)[hl_ndx] = bitmap2hostlist(fwd_bitmap);
		/* Now remove nodes from this switch from message list */
		bit_and_not(nodes_bitmap, fwd_bitmap);
		FREE_NULL_BITMAP(fwd_bitmap);
		if (debug_flags & DEBUG_FLAG_ROUTE) {
			buf = hostlist_ranged_string_xmalloc((*sp_hl)[hl_ndx]);
			debug("ROUTE: ... sublist[%d] switch=%s :: %s",
			      i, switch_record_table[i].name, buf);
			xfree(buf);
		}
		hl_ndx++;
		lst_count += sw_count;
		if (lst_count == msg_count)
			break; /* all nodes in message are in a child list */
	}
	FREE_NULL_BITMAP(nodes_bitmap);

	*count = hl_ndx;
	return SLURM_SUCCESS;

}

/*
 * route_g_reconfigure - reset during reconfigure
 *
 * RET: SLURM_SUCCESS - int
 */
extern int route_p_reconfigure (void)
{
	debug_flags = slurm_get_debug_flags();
	return SLURM_SUCCESS;
}


/*
 * route_p_next_collector - return address of next collector
 *
 * IN: is_collector - bool* - flag indication if this node is a collector
 *
 * RET: slurm_addr_t* - address of node to send messages to be aggregated.
 */
extern slurm_addr_t* route_p_next_collector ( bool *is_collector )
{
	return route_next_collector(is_collector);
}

/*
 * route_g_next_collector_backup
 *
 * RET: slurm_addr_t* - address of backup node to send messages to be aggregated.
 */
extern slurm_addr_t* route_p_next_collector_backup ( void )
{
	/* return NULL until we have a clearly defined backup.
	 * Otherwise we could get into a sending loop if the primary
	 * fails with us sending to a sibling that may have me as a
	 * parent.
	 */
	return NULL;
}
