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
#include "src/interfaces/topology.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/* These are defined here so when we link with something other than
 * the slurmctld we will have these symbols defined.  They will get
 * overwritten when linking with the slurmctld.
 */
#if defined (__APPLE__)
extern node_record_t **node_record_table_ptr __attribute__((weak_import));
extern int node_record_count __attribute__((weak_import));
extern switch_record_t *switch_record_table __attribute__((weak_import));
extern int switch_record_cnt __attribute__((weak_import));
extern int switch_levels __attribute__((weak_import));
extern int active_node_record_count __attribute__((weak_import));
#else
node_record_t **node_record_table_ptr;
int node_record_count;
switch_record_t *switch_record_table;
int switch_record_cnt;
int switch_levels;
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
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

typedef struct slurm_conf_switches {
	uint32_t link_speed;	/* link speed, arbitrary units */
	char *nodes;		/* names of nodes directly connect to
				 * this switch, if any */
	char *switch_name;	/* name of this switch */
	char *switches;		/* names if child switches directly
				 * connected to this switch, if any */
} slurm_conf_switches_t;
static s_p_hashtbl_t *conf_hashtbl = NULL;
static char* topo_conf = NULL;

static void _check_better_path(int i, int j ,int k);
static void _destroy_switches(void *ptr);
static void _free_switch_record_table(void);
static int  _get_switch_inx(const char *name);
static void _log_switches(void);
static int  _node_name2bitmap(char *node_names, bitstr_t **bitmap,
			      hostlist_t *invalid_hostlist);
static int  _parse_switches(void **dest, slurm_parser_enum_t type,
			    const char *key, const char *value,
			    const char *line, char **leftover);
static int  _read_topo_file(slurm_conf_switches_t **ptr_array[]);
static void _find_child_switches(int sw);
static void _validate_switches(void);


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
	_free_switch_record_table();
	xfree(topo_conf);
	return SLURM_SUCCESS;
}

/*
 * topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topo_build_config(void)
{
	if (node_record_count)
		_validate_switches();
	return SLURM_SUCCESS;
}

/*
 * When TopologyParam=SwitchAsNodeRank is set, this plugin assigns a unique
 * node_rank for all nodes belonging to the same leaf switch.
 */
extern bool topo_generate_node_ranking(void)
{
	/* By default, node_rank is 0, so start at 1 */
	int switch_rank = 1;

	if (!xstrcasestr(slurm_conf.topology_param, "SwitchAsNodeRank"))
		return false;

	/* Build a temporary topology to be able to find the leaf switches. */
	_validate_switches();

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
	_free_switch_record_table();

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
extern int topo_get_node_addr(char* node_name, char** paddr, char** ppattern)
{
	node_record_t *node_ptr;
	hostlist_t sl = NULL;

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
 * _find_child_switches creates an array of indexes to the
 * immediate descendants of switch sw.
 */
static void _find_child_switches(int sw)
{
	int i;
	int cldx; /* index into array of child switches */
	hostlist_iterator_t hi;
	hostlist_t swlist;
	char *swname;

	swlist = hostlist_create(switch_record_table[sw].switches);
	switch_record_table[sw].num_switches = hostlist_count(swlist);
	switch_record_table[sw].switch_index =
			xmalloc(switch_record_table[sw].num_switches
				* sizeof(uint16_t));

	hi = hostlist_iterator_create(swlist);
	cldx = 0;
	while ((swname = hostlist_next(hi))) {
		/* Find switch whose name is the name of this child.
		 * and add its index to child index array */
		for (i = 0; i < switch_record_cnt; i++) {
			if (xstrcmp(swname, switch_record_table[i].name) == 0) {
				switch_record_table[sw].switch_index[cldx] = i;
				switch_record_table[i].parent = sw;
				cldx++;
				break;
			}
		}
		free(swname);
	}
	hostlist_iterator_destroy(hi);
	hostlist_destroy(swlist);
}

static void _merge_switches_array(uint16_t *switch_index1, uint16_t *cnt1,
				  uint16_t *switch_index2, uint16_t cnt2)
{
	int i, j;
	uint16_t init_cnt1 = *cnt1;

	for (i = 0; i < cnt2; i++) {
		for (j = 0; j < init_cnt1; j++) {
			if (switch_index1[j] == switch_index2[i])
				break;
		}
		if (j < init_cnt1)
			continue;
		switch_index1[*cnt1] = switch_index2[i];
		(*cnt1)++;
	}
}

/*
 * _find_desc_switches creates an array of indexes to the
 * all descendants of switch sw.
 */
static void _find_desc_switches(int sw)
{
	int k;
	_merge_switches_array(switch_record_table[sw].switch_desc_index,
			      &(switch_record_table[sw].num_desc_switches),
			      switch_record_table[sw].switch_index,
			      switch_record_table[sw].num_switches);

	for (k = 0; k < switch_record_table[sw].num_switches; k++) {
		int child_index = switch_record_table[sw].switch_index[k];
		_merge_switches_array(
			switch_record_table[sw].switch_desc_index,
			&(switch_record_table[sw].num_desc_switches),
			switch_record_table[child_index].switch_desc_index,
			switch_record_table[child_index].num_desc_switches);
	}

}
static void _validate_switches(void)
{
	slurm_conf_switches_t *ptr, **ptr_array;
	int depth, i, j, node_count;
	switch_record_t *switch_ptr, *prior_ptr;
	hostlist_t hl, invalid_hl = NULL;
	char *child, *buf;
	bool  have_root = false;
	bitstr_t *multi_homed_bitmap = NULL;	/* nodes on >1 leaf switch */
	bitstr_t *switches_bitmap = NULL;	/* nodes on any leaf switch */
	bitstr_t *tmp_bitmap = NULL;

	_free_switch_record_table();

	switch_record_cnt = _read_topo_file(&ptr_array);
	if (switch_record_cnt == 0) {
		error("No switches configured");
		s_p_hashtbl_destroy(conf_hashtbl);
		return;
	}

	switch_record_table = xcalloc(switch_record_cnt,
				      sizeof(switch_record_t));
	multi_homed_bitmap = bit_alloc(node_record_count);
	switch_ptr = switch_record_table;
	for (i = 0; i < switch_record_cnt; i++, switch_ptr++) {
		ptr = ptr_array[i];
		switch_ptr->name = xstrdup(ptr->switch_name);
		/* See if switch name has already been defined. */
		prior_ptr = switch_record_table;
		for (j = 0; j < i; j++, prior_ptr++) {
			if (xstrcmp(switch_ptr->name, prior_ptr->name) == 0) {
				fatal("Switch (%s) has already been defined",
				      prior_ptr->name);
			}
		}
		switch_ptr->link_speed = ptr->link_speed;
		if (ptr->nodes) {
			switch_ptr->level = 0;	/* leaf switch */
			switch_ptr->nodes = xstrdup(ptr->nodes);
			if (_node_name2bitmap(ptr->nodes,
					      &switch_ptr->node_bitmap,
					      &invalid_hl)) {
				fatal("Invalid node name (%s) in switch "
				      "config (%s)",
				      ptr->nodes, ptr->switch_name);
			}
			if (switches_bitmap) {
				tmp_bitmap = bit_copy(switch_ptr->node_bitmap);
				bit_and(tmp_bitmap, switches_bitmap);
				bit_or(multi_homed_bitmap, tmp_bitmap);
				FREE_NULL_BITMAP(tmp_bitmap);
				bit_or(switches_bitmap,
				       switch_ptr->node_bitmap);
			} else {
				switches_bitmap = bit_copy(switch_ptr->
							   node_bitmap);
			}
		} else if (ptr->switches) {
			switch_ptr->level = -1;	/* determine later */
			switch_ptr->switches = xstrdup(ptr->switches);
		} else {
			fatal("Switch configuration (%s) lacks children",
			      ptr->switch_name);
		}
	}

	for (depth = 1; ; depth++) {
		bool resolved = true;
		switch_ptr = switch_record_table;
		for (i = 0; i < switch_record_cnt; i++, switch_ptr++) {
			if (switch_ptr->level != -1)
				continue;
			hl = hostlist_create(switch_ptr->switches);
			if (!hl) {
				fatal("Invalid switches: %s",
				      switch_ptr->switches);
			}
			while ((child = hostlist_pop(hl))) {
				j = _get_switch_inx(child);
				if ((j < 0) || (j == i)) {
					fatal("Switch configuration %s has "
					      "invalid child (%s)",
					      switch_ptr->name, child);
				}
				if (switch_record_table[j].level == -1) {
					/* Children not resolved */
					resolved = false;
					switch_ptr->level = -1;
					FREE_NULL_BITMAP(switch_ptr->
							 node_bitmap);
					free(child);
					break;
				}
				if (switch_ptr->level == -1) {
					switch_ptr->level = 1 +
						switch_record_table[j].level;
					switch_ptr->node_bitmap =
						bit_copy(switch_record_table[j].
							 node_bitmap);
				} else {
					switch_ptr->level =
						MAX(switch_ptr->level,
						     (switch_record_table[j].
						      level + 1));
					bit_or(switch_ptr->node_bitmap,
					       switch_record_table[j].
					       node_bitmap);
				}
				free(child);
			}
			hostlist_destroy(hl);
		}
		if (resolved)
			break;
		if (depth > 20)	/* Prevent infinite loop */
			fatal("Switch configuration is not a tree");
	}

	switch_levels = 0;
	switch_ptr = switch_record_table;
	for (i = 0; i < switch_record_cnt; i++, switch_ptr++) {
		switch_levels = MAX(switch_levels, switch_ptr->level);
		if (switch_ptr->node_bitmap == NULL)
			error("switch %s has no nodes", switch_ptr->name);
	}
	if (switches_bitmap) {
		bit_not(switches_bitmap);
		i = bit_set_count(switches_bitmap);
		if (i > 0) {
			child = bitmap2node_name(switches_bitmap);
			warning("switches lack access to %d nodes: %s",
				i, child);
			xfree(child);
		}
		FREE_NULL_BITMAP(switches_bitmap);
	} else
		fatal("switches contain no nodes");

	if (invalid_hl) {
		buf = hostlist_ranged_string_xmalloc(invalid_hl);
		warning("Invalid hostnames in switch configuration: %s", buf);
		xfree(buf);
		hostlist_destroy(invalid_hl);
	}

	/* Report nodes on multiple leaf switches,
	 * possibly due to bad configuration file */
	i = bit_set_count(multi_homed_bitmap);
	if (i > 0) {
		child = bitmap2node_name(multi_homed_bitmap);
		warning("Multiple leaf switches contain nodes: %s", child);
		xfree(child);
	}
	FREE_NULL_BITMAP(multi_homed_bitmap);

	node_count = active_node_record_count;
	/* Create array of indexes of children of each switch,
	 * and see if any switch can reach all nodes */
	for (i = 0; i < switch_record_cnt; i++) {
		if (switch_record_table[i].level != 0) {
			_find_child_switches(i);
		}
		if (node_count ==
			bit_set_count(switch_record_table[i].node_bitmap)) {
			have_root = true;
		}
	}

	for (i = 0; i < switch_record_cnt; i++) {
		switch_record_table[i].switches_dist = xcalloc(
			switch_record_cnt, sizeof(uint32_t));
		switch_record_table[i].switch_desc_index = xcalloc(
			switch_record_cnt, sizeof(uint16_t));
		switch_record_table[i].num_desc_switches = 0;
	}
	for (i = 0; i < switch_record_cnt; i++) {
		for (j = i + 1; j < switch_record_cnt; j++) {
			switch_record_table[i].switches_dist[j] = INFINITE;
			switch_record_table[j].switches_dist[i] = INFINITE;
		}
		for (j = 0; j < switch_record_table[i].num_switches; j++) {
			uint16_t child = switch_record_table[i].switch_index[j];

			switch_record_table[i].switches_dist[child] = 1;
			switch_record_table[child].switches_dist[i] = 1;
		}
	}
	for (i = 0; i < switch_record_cnt; i++) {
		for (j = 0; j < switch_record_cnt; j++) {
			int k;
			for (k = 0; k < switch_record_cnt; k++) {
				_check_better_path(i, j ,k);
			}
		}
	}
	for (i = 1; i <= switch_levels; i++) {
		for (j = 0; j < switch_record_cnt; j++) {
			if (switch_record_table[j].level != i)
				continue;
			_find_desc_switches(j);
		}
	}
	if (!have_root && running_in_daemon())
		warning("TOPOLOGY: no switch can reach all nodes through its descendants. If this is not intentional, fix the topology.conf file.");

	s_p_hashtbl_destroy(conf_hashtbl);
	_log_switches();
}

static void _log_switches(void)
{
	int i, j;
	char *tmp_str = NULL, *sep;
	switch_record_t *switch_ptr;

	switch_ptr = switch_record_table;
	for (i = 0; i < switch_record_cnt; i++, switch_ptr++) {
		if (!switch_ptr->nodes) {
			switch_ptr->nodes = bitmap2node_name(switch_ptr->
							     node_bitmap);
		}
		debug("Switch level:%d name:%s nodes:%s switches:%s",
		      switch_ptr->level, switch_ptr->name,
		      switch_ptr->nodes, switch_ptr->switches);
	}
	for (i = 0; i < switch_record_cnt; i++) {
		sep = "";
		for (j = 0; j < switch_record_cnt; j++) {
			xstrfmtcat(tmp_str, "%s%u", sep,
				   switch_record_table[i].switches_dist[j]);
			sep = ", ";
		}
		debug("\tswitches_dist[%d]:\t%s", i, tmp_str);
		xfree(tmp_str);
	}
	for (i = 0; i < switch_record_cnt; i++) {
		sep = "";
		for (j = 0; j < switch_record_table[i].num_desc_switches; j++) {
			xstrfmtcat(tmp_str, "%s%u", sep,
				   switch_record_table[i].switch_desc_index[j]);
			sep = ", ";
		}
		debug("\tswitch_desc_index[%d]:\t%s", i, tmp_str);
		xfree(tmp_str);
	}
}

/* Return the index of a given switch name or -1 if not found */
static int _get_switch_inx(const char *name)
{
	int i;
	switch_record_t *switch_ptr;

	switch_ptr = switch_record_table;
	for (i = 0; i < switch_record_cnt; i++, switch_ptr++) {
		if (xstrcmp(switch_ptr->name, name) == 0)
			return i;
	}

	return -1;
}

/* Free all memory associated with switch_record_table structure */
static void _free_switch_record_table(void)
{
	int i;

	if (switch_record_table) {
		for (i = 0; i < switch_record_cnt; i++) {
			xfree(switch_record_table[i].name);
			xfree(switch_record_table[i].nodes);
			xfree(switch_record_table[i].switches);
			xfree(switch_record_table[i].switches_dist);
			xfree(switch_record_table[i].switch_desc_index);
			xfree(switch_record_table[i].switch_index);
			FREE_NULL_BITMAP(switch_record_table[i].node_bitmap);
		}
		xfree(switch_record_table);
		switch_record_cnt = 0;
		switch_levels = 0;
	}
}

/* Return count of switch configuration entries read */
static int  _read_topo_file(slurm_conf_switches_t **ptr_array[])
{
	static s_p_options_t switch_options[] = {
		{"SwitchName", S_P_ARRAY, _parse_switches, _destroy_switches},
		{NULL}
	};
	int count;
	slurm_conf_switches_t **ptr;

	debug("Reading the topology.conf file");
	if (!topo_conf)
		topo_conf = get_extra_conf_path("topology.conf");

	conf_hashtbl = s_p_hashtbl_create(switch_options);
	if (s_p_parse_file(conf_hashtbl, NULL, topo_conf, false, NULL) ==
	    SLURM_ERROR) {
		s_p_hashtbl_destroy(conf_hashtbl);
		fatal("something wrong with opening/reading %s: %m",
		      topo_conf);
	}

	if (s_p_get_array((void ***)&ptr, &count, "SwitchName", conf_hashtbl))
		*ptr_array = ptr;
	else {
		*ptr_array = NULL;
		count = 0;
	}
	return count;
}

static int  _parse_switches(void **dest, slurm_parser_enum_t type,
			    const char *key, const char *value,
			    const char *line, char **leftover)
{
	s_p_hashtbl_t *tbl;
	slurm_conf_switches_t *s;
	static s_p_options_t _switch_options[] = {
		{"LinkSpeed", S_P_UINT32},
		{"Nodes", S_P_STRING},
		{"Switches", S_P_STRING},
		{NULL}
	};

	tbl = s_p_hashtbl_create(_switch_options);
	s_p_parse_line(tbl, *leftover, leftover);

	s = xmalloc(sizeof(slurm_conf_switches_t));
	s->switch_name = xstrdup(value);
	if (!s_p_get_uint32(&s->link_speed, "LinkSpeed", tbl))
		s->link_speed = 1;
	s_p_get_string(&s->nodes, "Nodes", tbl);
	s_p_get_string(&s->switches, "Switches", tbl);
	s_p_hashtbl_destroy(tbl);

	if (s->nodes && s->switches) {
		error("switch %s has both child switches and nodes",
		      s->switch_name);
		_destroy_switches(s);
		return -1;
	}
	if (!s->nodes && !s->switches) {
		error("switch %s has neither child switches nor nodes",
		      s->switch_name);
		_destroy_switches(s);
		return -1;
	}

	*dest = (void *)s;

	return 1;
}

static void _destroy_switches(void *ptr)
{
	slurm_conf_switches_t *s = (slurm_conf_switches_t *)ptr;
	xfree(s->nodes);
	xfree(s->switch_name);
	xfree(s->switches);
	xfree(ptr);
}

/*
 * _node_name2bitmap - given a node name regular expression, build a bitmap
 *	representation, any invalid hostnames are added to a hostlist
 * IN node_names  - set of node namess
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * IN/OUT invalid_hostlist - hostlist of invalid host names, initialize to NULL
 * RET 0 if no error, otherwise EINVAL
 * NOTE: call FREE_NULL_BITMAP(bitmap) and hostlist_destroy(invalid_hostlist)
 *       to free memory when variables are no longer required
 */
static int _node_name2bitmap(char *node_names, bitstr_t **bitmap,
			     hostlist_t *invalid_hostlist)
{
	char *this_node_name;
	bitstr_t *my_bitmap;
	hostlist_t host_list;

	my_bitmap = (bitstr_t *) bit_alloc(node_record_count);
	*bitmap = my_bitmap;

	if (node_names == NULL) {
		error("_node_name2bitmap: node_names is NULL");
		return EINVAL;
	}

	if ((host_list = hostlist_create(node_names)) == NULL) {
		/* likely a badly formatted hostlist */
		error("_node_name2bitmap: hostlist_create(%s) error",
		      node_names);
		return EINVAL;
	}

	while ((this_node_name = hostlist_shift(host_list)) ) {
		node_record_t *node_ptr;
		node_ptr = find_node_record(this_node_name);
		if (node_ptr) {
			bit_set(my_bitmap, node_ptr->index);
		} else {
			debug2("_node_name2bitmap: invalid node specified %s",
			       this_node_name);
			if (*invalid_hostlist) {
				hostlist_push_host(*invalid_hostlist,
						   this_node_name);
			} else {
				*invalid_hostlist =
					hostlist_create(this_node_name);
			}
		}
		free(this_node_name);
	}
	hostlist_destroy(host_list);

	return SLURM_SUCCESS;
}

static void _check_better_path(int i, int j ,int k)
{
	int tmp;

	if ((switch_record_table[j].switches_dist[i] == INFINITE) ||
	    (switch_record_table[i].switches_dist[k] == INFINITE)) {
		tmp = INFINITE;
	} else {
		tmp = switch_record_table[j].switches_dist[i] +
		      switch_record_table[i].switches_dist[k];
	}

	if (switch_record_table[j].switches_dist[k] > tmp)
		switch_record_table[j].switches_dist[k] = tmp;
}
