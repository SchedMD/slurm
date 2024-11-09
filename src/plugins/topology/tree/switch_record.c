/*****************************************************************************\
 *  switch_record.c - Determine order of nodes for job using tree algo.
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

#include "switch_record.h"

#include "src/common/xstring.h"

typedef struct slurm_conf_switches {
	uint32_t link_speed;	/* link speed, arbitrary units */
	char *nodes;		/* names of nodes directly connect to
				 * this switch, if any */
	char *switch_name;	/* name of this switch */
	char *switches;		/* names if child switches directly
				 * connected to this switch, if any */
} slurm_conf_switches_t;

switch_record_t *switch_record_table = NULL;
int switch_record_cnt = 0;
int switch_levels = 0; /* number of switch levels */

static s_p_hashtbl_t *conf_hashtbl = NULL;

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

/* Free all memory associated with switch_record_table structure */
extern void switch_record_table_destroy(void)
{
	if (!switch_record_table)
		return;

	for (int i = 0; i < switch_record_cnt; i++) {
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

static void _destroy_switches(void *ptr)
{
	slurm_conf_switches_t *s = (slurm_conf_switches_t *)ptr;
	xfree(s->nodes);
	xfree(s->switch_name);
	xfree(s->switches);
	xfree(ptr);
}

static int _parse_switches(void **dest, slurm_parser_enum_t type,
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

	if (strlen(s->switch_name) > HOST_NAME_MAX) {
		error("SwitchName (%s) must be shorter than %d chars",
		      s->switch_name, HOST_NAME_MAX);
		_destroy_switches(s);
		return -1;
	}
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

/* Return count of switch configuration entries read */
static int  _read_topo_file(slurm_conf_switches_t **ptr_array[])
{
	static s_p_options_t switch_options[] = {
		{"SwitchName", S_P_ARRAY, _parse_switches, _destroy_switches},
		{NULL}
	};
	int count;
	slurm_conf_switches_t **ptr;

	xassert(topo_conf);
	debug("Reading the %s file", topo_conf);

	conf_hashtbl = s_p_hashtbl_create(switch_options);
	if (s_p_parse_file(conf_hashtbl, NULL, topo_conf, 0, NULL) ==
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

/*
 * _find_child_switches creates an array of indexes to the
 * immediate descendants of switch sw.
 */
static void _find_child_switches(int sw)
{
	int i;
	int cldx; /* index into array of child switches */
	hostlist_iterator_t *hi;
	hostlist_t *swlist;
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

extern void switch_record_validate(void)
{
	slurm_conf_switches_t *ptr, **ptr_array;
	int depth, i, j, node_count;
	switch_record_t *switch_ptr, *prior_ptr;
	hostlist_t *hl, *invalid_hl = NULL;
	char *child, *buf;
	bool  have_root = false;
	bitstr_t *multi_homed_bitmap = NULL;	/* nodes on >1 leaf switch */
	bitstr_t *switches_bitmap = NULL;	/* nodes on any leaf switch */
	bitstr_t *tmp_bitmap = NULL;

	switch_record_table_destroy();

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
		switch_ptr->parent = SWITCH_NO_PARENT;
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
			if (node_name2bitmap(ptr->nodes, true,
					     &switch_ptr->node_bitmap,
					     &invalid_hl)) {
				fatal("Invalid node name (%s) in switch config (%s)",
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
					fatal("Switch configuration %s has invalid child (%s)",
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

