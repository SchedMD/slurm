/*****************************************************************************\
 *  topology_tree.c - Build configuration information for hierarchical
 *	switch topology
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Copyright (C) 2023 NVIDIA CORPORATION.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "src/common/slurm_xlator.h"

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/xstring.h"

#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include "eval_nodes_tree.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern int active_node_record_count __attribute__((weak_import));
#else
node_record_t **node_record_table_ptr;
int node_record_count;
int active_node_record_count;
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
const char plugin_name[]        = "topology tree plugin";
const char plugin_type[]        = "topology/tree";
const uint32_t plugin_id = TOPOLOGY_PLUGIN_TREE;
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

typedef topo_info_t topoinfo_switch_t;

typedef struct topoinfo_tree {
	uint32_t record_count;		/* number of records */
	topoinfo_switch_t *topo_array;	/* the switch topology records */
} topoinfo_tree_t;

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
	switch_record_table_destroy();
	return SLURM_SUCCESS;
}

/*
 * topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topology_p_build_config(void)
{
	if (node_record_count)
		switch_record_validate();
	return SLURM_SUCCESS;
}

extern int topology_p_eval_nodes(topology_eval_t *topo_eval)
{
	topo_eval->eval_nodes = eval_nodes_tree;
	topo_eval->trump_others = false;

	return common_topo_choose_nodes(topo_eval);
}

/*
 * When TopologyParam=SwitchAsNodeRank is set, this plugin assigns a unique
 * node_rank for all nodes belonging to the same leaf switch.
 */
extern bool topology_p_generate_node_ranking(void)
{
	/* By default, node_rank is 0, so start at 1 */
	int switch_rank = 1;

	if (!xstrcasestr(slurm_conf.topology_param, "SwitchAsNodeRank"))
		return false;

	/* Build a temporary topology to be able to find the leaf switches. */
	switch_record_validate();

	if (switch_record_cnt == 0)
		return false;

	for (int sw = 0; sw < switch_record_cnt; sw++) {
		/* skip if not a leaf switch */
		if (switch_record_table[sw].level != 0)
			continue;

		for (int n = 0; n < node_record_count; n++) {
			if (!bit_test(switch_record_table[sw].node_bitmap, n))
				continue;
			node_record_table_ptr[n]->node_rank = switch_rank;
			debug("node=%s rank=%d",
			      node_record_table_ptr[n]->name, switch_rank);
		}

		switch_rank++;
	}

	/* Discard the temporary topology since it is using node bitmaps */
	switch_record_table_destroy();

	return true;
}

/*
 * topo_get_node_addr - build node address and the associated pattern
 *      based on the topology information
 *
 * example of output :
 *      address : s0.s4.s8.tux1
 *      pattern : switch.switch.switch.node
 */
extern int topology_p_get_node_addr(char *node_name, char **paddr,
				    char **ppattern)
{
	node_record_t *node_ptr;
	hostlist_t *sl = NULL;

	int s_max_level = 0;
	int i, j;

	/* no switches found, return */
	if ( switch_record_cnt == 0 ) {
		*paddr = xstrdup(node_name);
		*ppattern = xstrdup("node");
		return SLURM_SUCCESS;
	}

	node_ptr = find_node_record(node_name);
	/* node not found in configuration */
	if ( node_ptr == NULL )
		return SLURM_ERROR;

	/* look for switches max level */
	for (i=0; i<switch_record_cnt; i++) {
		if ( switch_record_table[i].level > s_max_level )
			s_max_level = switch_record_table[i].level;
	}

	/* initialize output parameters */
	*paddr = xstrdup("");
	*ppattern = xstrdup("");

	/* build node topology address and the associated pattern */
	for (j = s_max_level; j >= 0; j--) {
		for (i = 0; i < switch_record_cnt; i++) {
			if (switch_record_table[i].level != j)
				continue;
			if (!bit_test(switch_record_table[i].node_bitmap,
				      node_ptr->index))
				continue;
			if (sl == NULL) {
				sl = hostlist_create(switch_record_table[i].
						     name);
			} else {
				hostlist_push_host(sl,
						   switch_record_table[i].
						   name);
			}
		}
		if (sl) {
			char *buf = hostlist_ranged_string_xmalloc(sl);
			xstrcat(*paddr,buf);
			xfree(buf);
			hostlist_destroy(sl);
			sl = NULL;
		}
		xstrcat(*paddr, ".");
		xstrcat(*ppattern, "switch.");
	}

	/* append node name */
	xstrcat(*paddr, node_name);
	xstrcat(*ppattern, "node");

	return SLURM_SUCCESS;
}

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
				   int *msg_count, hostlist_t ***sp_hl,
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

extern int topology_p_split_hostlist(hostlist_t *hl, hostlist_t ***sp_hl,
				     int *count, uint16_t tree_width)
{
	int i, j, k, msg_count, switch_count;
	int s_first, s_last;
	char *buf;
	bitstr_t *nodes_bitmap = NULL;		/* nodes in message list */
	bitstr_t *switch_bitmap = NULL;		/* switches  */
	slurmctld_lock_t node_read_lock = { .node = READ_LOCK };
	static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;

	if (!common_topo_route_tree()) {
		return common_topo_split_hostlist_treewidth(
			hl, sp_hl, count, tree_width);
	}

	slurm_mutex_lock(&init_lock);
	if (switch_record_cnt == 0) {
		if (running_in_slurmctld())
			fatal_abort("%s: Somehow we have 0 for switch_record_cnt and we are here in the slurmctld.  This should never happen.", __func__);
		/* configs have not already been processed */
		init_node_conf();
		build_all_nodeline_info(false, 0);
		rehash_node();

		if (topology_g_build_config() != SLURM_SUCCESS) {
			fatal("ROUTE: Failed to build topology config");
		}
	}
	slurm_mutex_unlock(&init_lock);

	/* Only acquire the slurmctld lock if running as the slurmctld. */
	if (running_in_slurmctld())
		lock_slurmctld(node_read_lock);

	/* create bitmap of nodes to send message too */
	if (hostlist2bitmap(hl, false, &nodes_bitmap) != SLURM_SUCCESS) {
		buf = hostlist_ranged_string_xmalloc(hl);
		fatal("ROUTE: Failed to make bitmap from hostlist=%s.", buf);
	}

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
		if (running_in_slurmctld())
			unlock_slurmctld(node_read_lock);
		FREE_NULL_BITMAP(nodes_bitmap);
		FREE_NULL_BITMAP(switch_bitmap);
		return common_topo_split_hostlist_treewidth(hl, sp_hl, count,
							    tree_width);
	}
	*sp_hl = xcalloc(switch_record_cnt, sizeof(hostlist_t *));
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
		node_record_t *node_ptr;

		if (slurm_conf.debug_flags & DEBUG_FLAG_ROUTE) {
			buf = bitmap2node_name(nodes_bitmap);
			debug("ROUTE: didn't find switch containing nodes=%s",
			      buf);
			xfree(buf);
		}
		new_size += msg_count * sizeof(hostlist_t *);
		xrealloc(*sp_hl, new_size);

		for (j = 0; (node_ptr = next_node_bitmap(nodes_bitmap, &j));
		     j++) {
			(*sp_hl)[*count] = hostlist_create(NULL);
			hostlist_push_host((*sp_hl)[*count], node_ptr->name);
			(*count)++;
		}
	}

	if (running_in_slurmctld())
		unlock_slurmctld(node_read_lock);
	FREE_NULL_BITMAP(nodes_bitmap);
	FREE_NULL_BITMAP(switch_bitmap);

	return SLURM_SUCCESS;
}

extern int topology_p_topology_free(void *topoinfo_ptr)
{
	int i = 0;
	topoinfo_tree_t *topoinfo = topoinfo_ptr;
	if (topoinfo) {
		if (topoinfo->topo_array) {
			for (i = 0; i < topoinfo->record_count; i++) {
				xfree(topoinfo->topo_array[i].name);
				xfree(topoinfo->topo_array[i].nodes);
				xfree(topoinfo->topo_array[i].switches);
			}
			xfree(topoinfo->topo_array);
		}
		xfree(topoinfo);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_get(topology_data_t type, void *data)
{
	int rc = SLURM_SUCCESS;

	switch (type) {
	case TOPO_DATA_TOPOLOGY_PTR:
	{
		dynamic_plugin_data_t **topoinfo_pptr = data;
		topoinfo_tree_t *topoinfo_ptr =
			xmalloc(sizeof(topoinfo_tree_t));

		*topoinfo_pptr = xmalloc(sizeof(dynamic_plugin_data_t));
		(*topoinfo_pptr)->data = topoinfo_ptr;
		(*topoinfo_pptr)->plugin_id = plugin_id;

		topoinfo_ptr->record_count = switch_record_cnt;
		topoinfo_ptr->topo_array = xcalloc(topoinfo_ptr->record_count,
						   sizeof(topoinfo_switch_t));

		for (int i = 0; i < topoinfo_ptr->record_count; i++) {
			topoinfo_ptr->topo_array[i].level =
				switch_record_table[i].level;
			topoinfo_ptr->topo_array[i].link_speed =
				switch_record_table[i].link_speed;
			topoinfo_ptr->topo_array[i].name =
				xstrdup(switch_record_table[i].name);
			topoinfo_ptr->topo_array[i].nodes =
				xstrdup(switch_record_table[i].nodes);
			topoinfo_ptr->topo_array[i].switches =
				xstrdup(switch_record_table[i].switches);
		}
		break;
	}
	case TOPO_DATA_REC_CNT:
	{
		int *rec_cnt = data;
		*rec_cnt = switch_record_cnt;
		break;
	}
	default:
		error("Unsupported option %d", type);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

extern int topology_p_topology_pack(void *topoinfo_ptr, buf_t *buffer,
				    uint16_t protocol_version)
{
	int i;
	topoinfo_tree_t *topoinfo = topoinfo_ptr;

	pack32(topoinfo->record_count, buffer);
	for (i = 0; i < topoinfo->record_count; i++) {
		pack16(topoinfo->topo_array[i].level, buffer);
		pack32(topoinfo->topo_array[i].link_speed, buffer);
		packstr(topoinfo->topo_array[i].name, buffer);
		packstr(topoinfo->topo_array[i].nodes, buffer);
		packstr(topoinfo->topo_array[i].switches, buffer);
	}
	return SLURM_SUCCESS;
}
void _print_topo_record(topoinfo_switch_t * topo_ptr, char **out)
{
	char *env, *line = NULL, *pos = NULL;

	/****** Line 1 ******/
	xstrfmtcatat(line, &pos, "SwitchName=%s Level=%u LinkSpeed=%u",
		     topo_ptr->name, topo_ptr->level, topo_ptr->link_speed);

	if (topo_ptr->nodes)
		xstrfmtcatat(line, &pos, " Nodes=%s", topo_ptr->nodes);

	if (topo_ptr->switches)
		xstrfmtcatat(line, &pos, " Switches=%s", topo_ptr->switches);

	if ((env = getenv("SLURM_TOPO_LEN")))
		xstrfmtcat(*out, "%.*s\n", atoi(env), line);
	else
		xstrfmtcat(*out, "%s\n", line);

	xfree(line);

}

extern int topology_p_topology_print(void *topoinfo_ptr, char *nodes_list,
				     char **out)
{
	int i, match, match_cnt = 0;;
	topoinfo_tree_t *topoinfo = topoinfo_ptr;

	*out = NULL;

	if ((nodes_list == NULL) || (nodes_list[0] == '\0')) {
		if (topoinfo->record_count == 0) {
			error("No topology information available");
			return SLURM_SUCCESS;
		}

		for (i = 0; i < topoinfo->record_count; i++)
			_print_topo_record(&topoinfo->topo_array[i], out);

		return SLURM_SUCCESS;
	}

	/* Search for matching switch name */
	for (i = 0; i < topoinfo->record_count; i++) {
		if (xstrcmp(topoinfo->topo_array[i].name, nodes_list))
			continue;
		_print_topo_record(&topoinfo->topo_array[i], out);
		return SLURM_SUCCESS;
	}

	/* Search for matching node name */
	for (i = 0; i < topoinfo->record_count; i++) {
		hostset_t *hs;

		if ((topoinfo->topo_array[i].nodes == NULL) ||
		    (topoinfo->topo_array[i].nodes[0] == '\0'))
			continue;
		hs = hostset_create(topoinfo->topo_array[i].nodes);
		if (hs == NULL)
			fatal("hostset_create: memory allocation failure");
		match = hostset_within(hs, nodes_list);
		hostset_destroy(hs);
		if (!match)
			continue;
		match_cnt++;
		_print_topo_record(&topoinfo->topo_array[i], out);
	}

	if (match_cnt == 0) {
		error("Topology information contains no switch or "
		      "node named %s", nodes_list);
	}
	return SLURM_SUCCESS;
}

extern int topology_p_topology_unpack(void **topoinfo_pptr, buf_t *buffer,
				      uint16_t protocol_version)
{
	int i = 0;
	topoinfo_tree_t *topoinfo_ptr =
		xmalloc(sizeof(topoinfo_tree_t));

	*topoinfo_pptr = topoinfo_ptr;
	safe_unpack32(&topoinfo_ptr->record_count, buffer);
	safe_xcalloc(topoinfo_ptr->topo_array, topoinfo_ptr->record_count,
		     sizeof(topoinfo_switch_t));
	for (i = 0; i < topoinfo_ptr->record_count; i++) {
		safe_unpack16(&topoinfo_ptr->topo_array[i].level, buffer);
		safe_unpack32(&topoinfo_ptr->topo_array[i].link_speed, buffer);
		safe_unpackstr(&topoinfo_ptr->topo_array[i].name, buffer);
		safe_unpackstr(&topoinfo_ptr->topo_array[i].nodes, buffer);
		safe_unpackstr(&topoinfo_ptr->topo_array[i].switches, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	topology_p_topology_free(topoinfo_ptr);
	*topoinfo_pptr = NULL;
	return SLURM_ERROR;
}
