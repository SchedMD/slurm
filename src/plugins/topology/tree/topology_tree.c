/*****************************************************************************\
 *  topology_tree.c - Build configuration information for hierarchical
 *	switch topology
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/slurm_topology.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

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
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as this API matures.
 */
const char plugin_name[]        = "topology tree plugin";
const char plugin_type[]        = "topology/tree";
const uint32_t plugin_version   = 101;

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

static void _destroy_switches(void *ptr);
static void _free_switch_record_table(void);
static int  _get_switch_inx(const char *name);
static void _log_switches(void);
static int  _node_name2bitmap(char *node_names, bitstr_t **bitmap, 
			      hostlist_t *invalid_hostlist);
static int  _parse_switches(void **dest, slurm_parser_enum_t type,
			    const char *key, const char *value,
			    const char *line, char **leftover);
extern int  _read_topo_file(slurm_conf_switches_t **ptr_array[]);
static void _find_child_switches (int sw);
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
	_validate_switches();
	return SLURM_SUCCESS;
}

/*
 * topo_generate_node_ranking  -  this plugin does not set any node_rank fields
 */
extern bool topo_generate_node_ranking(void)
{
	return false;
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
	struct node_record *node_ptr;
	int node_inx;
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
	node_inx = node_ptr - node_record_table_ptr;

	/* look for switches max level */
	for (i=0; i<switch_record_cnt; i++) {
		if ( switch_record_table[i].level > s_max_level )
			s_max_level = switch_record_table[i].level;
	}

	/* initialize output parameters */
	*paddr = xstrdup("");
	*ppattern = xstrdup("");

	/* build node topology address and the associated pattern */
	for (j=s_max_level; j>=0; j--) {
		for (i=0; i<switch_record_cnt; i++) {
			if ( switch_record_table[i].level != j )
				continue;
			if ( !bit_test(switch_record_table[i]. node_bitmap,
				       node_inx) )
				continue;
			if ( sl == NULL ) {
				sl = hostlist_create(switch_record_table[i].
						     name);
			} else {
				hostlist_push_host(sl,
						   switch_record_table[i].
						   name);
			}
		}
		if ( sl ) {
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
 * find_child_switches creates an array of indexes to the
 * immediate descendants of switch sw.
 */
static void _find_child_switches (int sw)
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
		for (i=0; i<switch_record_cnt; i++) {
			if (strcmp(swname, switch_record_table[i].name) == 0) {
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

static void _validate_switches(void)
{
	slurm_conf_switches_t *ptr, **ptr_array;
	int depth, i, j;
	struct switch_record *switch_ptr, *prior_ptr;
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

	switch_record_table = xmalloc(sizeof(struct switch_record) *
				      switch_record_cnt);
	multi_homed_bitmap = bit_alloc(node_record_count);
	switch_ptr = switch_record_table;
	for (i=0; i<switch_record_cnt; i++, switch_ptr++) {
		ptr = ptr_array[i];
		switch_ptr->name = xstrdup(ptr->switch_name);
		/* See if switch name has already been defined. */
		prior_ptr = switch_record_table;
		for (j=0; j<i; j++, prior_ptr++) {
			if (strcmp(switch_ptr->name, prior_ptr->name) == 0) {
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

	for (depth=1; ; depth++) {
		bool resolved = true;
		switch_ptr = switch_record_table;
		for (i=0; i<switch_record_cnt; i++, switch_ptr++) {
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
	}

	switch_levels = 0;
	switch_ptr = switch_record_table;
	for (i=0; i<switch_record_cnt; i++, switch_ptr++) {
		switch_levels = MAX(switch_levels, switch_ptr->level);
		if (switch_ptr->node_bitmap == NULL)
			error("switch %s has no nodes", switch_ptr->name);
	}
	if (switches_bitmap) {
		bit_not(switches_bitmap);
		i = bit_set_count(switches_bitmap);
		if (i > 0) {
			child = bitmap2node_name(switches_bitmap);
			error("WARNING: switches lack access to %d nodes: %s",
			      i, child);
			xfree(child);
		}
		FREE_NULL_BITMAP(switches_bitmap);
	} else
		fatal("switches contain no nodes");

	if (invalid_hl) {
		buf = hostlist_ranged_string_xmalloc(invalid_hl);
		error("WARNING: Invalid hostnames in switch configuration: %s",
		      buf);
		xfree(buf);
		hostlist_destroy(invalid_hl);
	}

	/* Report nodes on multiple leaf switches,
	 * possibly due to bad configuration file */
	i = bit_set_count(multi_homed_bitmap);
	if (i > 0) {
		child = bitmap2node_name(multi_homed_bitmap);
		error("WARNING: Multiple leaf switches contain nodes: %s",
		      child);
		xfree(child);
	}
	FREE_NULL_BITMAP(multi_homed_bitmap);

	/* Create array of indexes of children of each switch,
	 * and see if any switch can reach all nodes */
	for (i = 0; i < switch_record_cnt; i++) {
		if (switch_record_table[i].level != 0) {
			_find_child_switches (i);
		}
		if (node_record_count ==
			bit_set_count(switch_record_table[i].node_bitmap)) {
			have_root = true;
		}
	}
	if (!have_root) {
		info("TOPOLOGY: warning -- no switch can reach all nodes"
				" through its descendants."
				"Do not use route/topology");
	}
	s_p_hashtbl_destroy(conf_hashtbl);
	_log_switches();
}

static void _log_switches(void)
{
	int i;
	struct switch_record *switch_ptr;

	switch_ptr = switch_record_table;
	for (i=0; i<switch_record_cnt; i++, switch_ptr++) {
		if (!switch_ptr->nodes) {
			switch_ptr->nodes = bitmap2node_name(switch_ptr->
							     node_bitmap);
		}
		debug("Switch level:%d name:%s nodes:%s switches:%s",
		      switch_ptr->level, switch_ptr->name,
		      switch_ptr->nodes, switch_ptr->switches);
	}
}

/* Return the index of a given switch name or -1 if not found */
static int _get_switch_inx(const char *name)
{
	int i;
	struct switch_record *switch_ptr;

	switch_ptr = switch_record_table;
	for (i=0; i<switch_record_cnt; i++, switch_ptr++) {
		if (strcmp(switch_ptr->name, name) == 0)
			return i;
	}

	return -1;
}

/* Free all memory associated with switch_record_table structure */
static void _free_switch_record_table(void)
{
	int i;

	if (switch_record_table) {
		for (i=0; i<switch_record_cnt; i++) {
			xfree(switch_record_table[i].name);
			xfree(switch_record_table[i].nodes);
			xfree(switch_record_table[i].switches);
			xfree(switch_record_table[i].switch_index);
			FREE_NULL_BITMAP(switch_record_table[i].node_bitmap);
		}
		xfree(switch_record_table);
		switch_record_cnt = 0;
		switch_levels = 0;
	}
}

/* Return count of switch configuration entries read */
extern int  _read_topo_file(slurm_conf_switches_t **ptr_array[])
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
	if (s_p_parse_file(conf_hashtbl, NULL, topo_conf, false) ==
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

	if ( (host_list = hostlist_create(node_names)) == NULL) {
		/* likely a badly formatted hostlist */
		error("_node_name2bitmap: hostlist_create(%s) error", 
		      node_names);
		return EINVAL;
	}

	while ( (this_node_name = hostlist_shift(host_list)) ) {
		struct node_record *node_ptr;
		node_ptr = find_node_record(this_node_name);
		if (node_ptr) {
			bit_set(my_bitmap, 
				(bitoff_t) (node_ptr - node_record_table_ptr));
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
		free (this_node_name);
	}
	hostlist_destroy(host_list);

	return SLURM_SUCCESS;
}
