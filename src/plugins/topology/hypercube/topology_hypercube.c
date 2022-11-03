/*****************************************************************************\
 *  topology_hypercube.c - Build configuration information for hypercube
 *			   switch topology
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Copyright (C) 2014 Silicon Graphics International Corp. All rights reserved.
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

#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>

#include "slurm/slurm_errno.h"
#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/interfaces/topology.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#include "src/common/node_conf.h"


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
const char plugin_name[]        = "topology hypercube plugin";
const char plugin_type[]        = "topology/hypercube";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

typedef struct slurm_conf_switches {
	char *switch_name;	/* name of this switch */
	char *nodes;	/* names of nodes connected this switch */
	char *switches;	/* names of switches connected to this switch */
	uint32_t link_speed;		/* link speed, arbitrary units */
} slurm_conf_switches_t;

static s_p_hashtbl_t *conf_hashtbl = NULL;
static char* topo_conf = NULL;

typedef struct switch_data_struct switch_data;
struct switch_data_struct {
	char *name;			/* switch name */
	bitstr_t *node_bitmap;	/* bitmap of nodes connectwed to switch */
	int *coordinates; /* coordinates of switch within hypercube topology */
	int *orig_coordinates;/*original switch coordinates in hypercube topology*/
	uint32_t link_speed;		/* link speed, arbitrary units */

	switch_data **sw_conns; /* pointers to connected switches */
	int *sw_conn_speed; /* speed of connection to connected switches */
	int sw_conn_cnt; /* number of switches connected to this switch */
	char *switches;   /* name of direct descendant switches */

	node_record_t **node_conns; /* pointers to connected nodes */
	int *node_index; /* index of connected nodes in node_record_table */
	int node_conn_cnt; /* number of nodes connected to this switch */
	char *nodes;			/* name of direct descendant nodes */

	int rack_number; /* the number of the rack this switch is located in */
	int iru_number; /* the number of the IRU this switch is located in */
	int switch_number; /* the switch number for this switch within its IRU */

	int rank; /* the hilbert rank for this switch */
	int index; /* the index of the switch within the switch record table */
	int distance; /* distance between to start switch in ranked switch table */
};

static switch_data *switch_data_table = NULL;
static int switch_data_cnt = 0; /* size of switch_data_table */


#define switch_time_same_iru 1024
#define switch_time_same_rack 2048
#define switch_time_diff_rack 4096
#define switch_time_unlinked 10000

#define default_link_speed 256


/* Topology functions sorted by group */
//////////////////////////////////////////////////////////////////////////////
//// Data Parsing and Switch Record Table Building Related Functions ////
static void _validate_switches(void);
static int  _read_topo_file(slurm_conf_switches_t **ptr_array[]);
static int  _parse_switches(void **dest, slurm_parser_enum_t type,
				const char *key, const char *value,
				const char *line, char **leftover);
static int  _node_name2bitmap(char *node_names, bitstr_t **bitmap,
				  hostlist_t *invalid_hostlist);
static int _parse_connected_nodes(switch_data *sw_record);
static void _update_switch_connections(void);
static int _parse_connected_switches(switch_data *sw_record);
static int _parse_link_speed(char **sw_name);
static int _char2int(char coord);
static int _get_connection_time(const switch_data *sw_ptr1,
				const switch_data *sw_ptr2);
static void _resize_switch_connections(switch_data *sw_record,
				       int conns_space, int conn_count );
static void _update_location_info(switch_data *switch_ptr);
//////////////////////////////////////////////////////////////////////////////
//// Coordinate Related Functions ////
static int _coordinate_switches(void);
static void _zero_coordinates(void);
static int _find_new_switches(switch_data **switch_table, int record_count);
static int _get_switch_index(switch_data **switch_table,
			     int record_count, const switch_data *switch_ptr);
static void _or_coordinates(const switch_data *src_ptr,switch_data *dest_ptr);
static void _copy_coordinate(const switch_data *src_switch_ptr,
			     switch_data *dest_switch_ptr);
//////////////////////////////////////////////////////////////////////////////
//// Hilbert Curve, Switch Ranking and Distance Related Functions ////
static void _build_hypercube_switch_table( int num_curves);
static void _transform_coordinates( int curve_num );
static void _generate_hilbert_integers(void);
			  // ( position [n], # bits, dimension )
static void _axes_to_transpose(unsigned int* X, int b, int n);
static void _sort_switches_by_rank( int curve_num );
static void _create_sorted_switch_distances(int curve_num,
					    switch_data **ranked_switch_table);
static int _get_switch_distance(const switch_data *sw_ptr1,
				const switch_data *sw_ptr2);
//////////////////////////////////////////////////////////////////////////////
//// String Creation and Printing Related Function ////
static void _print_switch_data_table(void);
static void _print_hypercube_switch_table( int num_curves );
static void _print_sorted_hilbert_curves( int num_curves );
static char *_print_switch_str(switch_data *switch_ptr, int print,char *offset);
static char *_create_coordinate_str(switch_data *switch_ptr);
static char *_create_connection_str(switch_data *switch_ptr);
static char *_create_conn_node_str(switch_data *switch_ptr);
//////////////////////////////////////////////////////////////////////////////
//// Memory Freeing and Allocating Functions ////
static void _destroy_switches(void *ptr);
static void _free_switch_data_table(void);
static void _free_hypercube_switch_table(void);
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////


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
	_free_hypercube_switch_table();
	_free_switch_data_table();

	xfree(topo_conf);
	return SLURM_SUCCESS;
}

/*
 * topo_build_config - build or rebuild system topology information
 *	after a system startup or reconfiguration.
 */
extern int topo_build_config(void)
{
	return SLURM_SUCCESS;
}

/*
 * topo_generate_node_ranking  - Reads in topology.conf file and the switch
 * connection information for the Hypercube network topology. Use Hilbert Curves
 * to sort switches into multiple 1 dimensional tables which are used in the
 * select plugin to find the best-fit cluster of nodes for a job.
 */
extern bool topo_generate_node_ranking(void)
{
	int i;

	// Reads in topology.conf and parses it into switch_data_table struct
	_validate_switches();

	// Sets coordinates for switches in accordance with the hypercube topology
	_coordinate_switches();

	// Prints out all of the switch information for the network
	_print_switch_data_table();

	int num_curves = hypercube_dimensions;

	// Copy needed data from switch_data_table to hypercube_switch_table
	_build_hypercube_switch_table(num_curves);

	for (i = 0; i < num_curves; i++) {
		/* Apply a linear transformation to the switches coordinates so to
		 * produce a unique mapping from switch data to Hilbert curve */
		_transform_coordinates(i);

		// Creates Hilbert integers for each of the switches in the topology
		_generate_hilbert_integers();

		// Sort switches by their Hilbert integer ranks
		_sort_switches_by_rank(i);
	}

	// Prints out all of the hypercube switch information for the network
	_print_hypercube_switch_table(num_curves);

	// Prints Hypercube switch tables sorted by Hilbert Curve Integers
	_print_sorted_hilbert_curves(num_curves);

	// Free the old switch data table since it is no longer needed
	_free_switch_data_table();

	// Return false to prevent Slurm from doing additional node ordering
	return false;
}

/*
 * topo_get_node_addr - build node address
 */
extern int topo_get_node_addr(char* node_name, char** paddr, char** ppattern)
{
	*paddr = xstrdup(node_name);
	*ppattern = xstrdup("node");
	return SLURM_SUCCESS;
}


//////////////////////////////////////////////////////////////////////////////
//// Data Parsing and Switch Record Table Building Related Functions ////

/* Reads in topology.conf and parses it into switch_data_table struct */
static void _validate_switches(void)
{
	slurm_conf_switches_t *ptr, **ptr_array;
	int i, j;
	switch_data *switch_ptr, *prior_ptr;
	hostlist_t invalid_hl = NULL;

	_free_switch_data_table();

	// Read the data from the topopolgy file into slurm_conf_switches_t struct
	switch_data_cnt = _read_topo_file(&ptr_array);
	if (switch_data_cnt == 0) {
		error("No switches configured");
		s_p_hashtbl_destroy(conf_hashtbl);
		return;
	}

	switch_data_table = xmalloc(sizeof(switch_data) * switch_data_cnt);
	switch_ptr = switch_data_table;

	// loops through all the conf_switches found in config file
	// parses data into switch_data structs to build the record_table
	for (i = 0; i < switch_data_cnt; i++, switch_ptr++) {
		switch_data_table[i].index = i;
		ptr = ptr_array[i];
		switch_ptr->name = xstrdup(ptr->switch_name);

		/* See if switch name has already been defined. */
		prior_ptr = switch_data_table;
		for (j = 0; j < i; j++, prior_ptr++) {
			if (xstrcmp(switch_ptr->name, prior_ptr->name) == 0) {
				fatal("Switch (%s) has already been defined",
				      prior_ptr->name);
			}
		}

		switch_ptr->link_speed = ptr->link_speed;

		if (ptr->nodes) {
			switch_ptr->nodes = xstrdup(ptr->nodes);
			if (_node_name2bitmap(ptr->nodes,
					      &switch_ptr->node_bitmap,
					      &invalid_hl)) {
				fatal("Invalid node name (%s) in switch config (%s)",
				      ptr->nodes, ptr->switch_name);
			}

			switch_ptr->node_conn_cnt =
				_parse_connected_nodes(switch_ptr);
			if (switch_ptr->node_conn_cnt < 1) {
				error("Switch %s does not have any nodes "
				      "connected to it",
				      switch_ptr->name);
			}
		}

		if (ptr->switches) {
			switch_ptr->switches = xstrdup(ptr->switches);
		} else if (!ptr->nodes) {
			fatal("Switch configuration (%s) lacks children",
			      ptr->switch_name);
		}

		_update_location_info(switch_ptr);
	}

	/* Loops through updating and verifying all the switch's connections */
	_update_switch_connections();

	s_p_hashtbl_destroy(conf_hashtbl);
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


/* parses switches found in topology.config and builds conf_switches */
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

	if (!s->nodes && !s->switches) {
		error("switch %s has neither child switches nor nodes",
		      s->switch_name);
		_destroy_switches(s);
		return -1;
	}

	*dest = (void *)s;

	return 1;
}


/* _node_name2bitmap - given a node name regular expression, build a bitmap
 *	representation, any invalid hostnames are added to a hostlist
 * IN node_names  - set of node namess
 * OUT bitmap     - set to bitmap, may not have all bits set on error
 * IN/OUT invalid_hostlist - hostlist of invalid host names, initialize to NULL
 * RET 0 if no error, otherwise EINVAL
 * NOTE: call FREE_NULL_BITMAP(bitmap) and hostlist_destroy(invalid_hostlist)
 *       to free memory when variables are no longer required	*/
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
		node_record_t *node_ptr;
		node_ptr = find_node_record(this_node_name);
		if (node_ptr) {
			bit_set(my_bitmap, node_ptr->index);
		} else {
			fatal("Node \"%s\" specified in topology.conf but "
			      "Slurm has no record of node. Verify that node "
			      "\"%s\" is specified in slurm.conf",
			      this_node_name, this_node_name);
		}
		free(this_node_name);
	}
	hostlist_destroy(host_list);

	return SLURM_SUCCESS;
}


/* parses a switch's node list string and adds pointers to the
	connected nodes' data structs */
static int _parse_connected_nodes(switch_data *sw_record)
{
	int max_nodes = 256;
	sw_record->node_conns = xmalloc(max_nodes * sizeof(node_record_t *));
	sw_record->node_index = xmalloc(max_nodes * sizeof(int));
	char * node_name = strtok(sw_record->nodes," ,");
	int i, conn_count = 0;
	node_record_t **tmp_node_conns;
	int *tmp_node_index;

	// loops through all of the node names in the node name string
	while (node_name != NULL) {
		if (conn_count == max_nodes){
			fatal("%s has +%d node connections which is more than expected",
			      sw_record->name, conn_count);
		}

		// look up node struct and add pointer to it in switch's struct
		node_record_t *node_ptr = find_node_record(node_name);
		if (node_ptr) {
			sw_record->node_conns[conn_count] = node_ptr;
			sw_record->node_index[conn_count] = node_ptr->index;
			conn_count++;
		} else {
			fatal("Node \"%s\" connected to switch %s specified in "
			      "topology.conf but Slurm has no record of node. "
			      "Verify that node \"%s\" is specified in "
			      "slurm.conf",
			      node_name, sw_record->name,node_name);
		}

		node_name = strtok (NULL, " ,.-");
	}

	/* Ensure that node_index[] is in sorted order */
	for (i = 0; i < conn_count; i++) {
		int min_val = sw_record->node_index[i];
		int min_idx = i;
		int j;

		for (j = i + 1; j < conn_count; j++) {
			if (min_val > sw_record->node_index[j]) {
				min_val = sw_record->node_index[j];
				min_idx = j;
			}
		}

		if (min_idx != i) {
			node_record_t *trec = sw_record->node_conns[i];
			int tidx = sw_record->node_index[i];

			sw_record->node_conns[i] = sw_record->node_conns[min_idx];
			sw_record->node_conns[min_idx] = trec;

			sw_record->node_index[i] = sw_record->node_index[min_idx];
			sw_record->node_index[min_idx] = tidx;
		}
	}

	tmp_node_conns = xrealloc(sw_record->node_conns,
				  conn_count * sizeof(node_record_t *));
	tmp_node_index = xrealloc(sw_record->node_index,
				  conn_count * sizeof(int));

	if ((tmp_node_conns != NULL) && (tmp_node_index != NULL)) {
		sw_record->node_conns = tmp_node_conns;
		sw_record->node_index = tmp_node_index;
	} else {
		fatal("Error (re)allocating memory for nodes for %s",
		      sw_record->name);
	}

	return conn_count;
}


/* Loops through all the switches and updates and verifies their connections */
static void _update_switch_connections(void)
{
	// after all of the switch structs have been built, loop through
	//again and set all of the switch connections to point to each other
	switch_data * switch_ptr = switch_data_table;
	int i;

	for (i = 0; i < switch_data_cnt; i++, switch_ptr++) {
		switch_ptr->sw_conn_cnt = _parse_connected_switches(switch_ptr);

		if (switch_ptr->sw_conn_cnt > hypercube_dimensions) {
			hypercube_dimensions = switch_ptr->sw_conn_cnt;
		}
	}

	// Malloc space for coordinates
	switch_ptr = switch_data_table;

	for (i = 0; i < switch_data_cnt; i++, switch_ptr++) {
		switch_ptr->coordinates = xmalloc(
			sizeof(int) * hypercube_dimensions);
		switch_ptr->orig_coordinates = xmalloc(
			sizeof(int) * hypercube_dimensions);
#if 0
		if (switch_ptr->sw_conn_cnt < hypercube_dimensions) {
			error(
"Switch %s is only connected to %d switches in %d-dimension hypercube topology",
				switch_ptr->name,
				switch_ptr->sw_conn_cnt, hypercube_dimensions);
		}
#endif
	}
}


/* parses a switch's switch list string and adds pointers to the
	connected switches' data structs */
static int _parse_connected_switches(switch_data *sw_record)
{
	int conns_space = 64;
	char * sw_name = strtok(sw_record->switches, ",-");
	int conn_count = 0;
	int link_speed;

	sw_record->sw_conns = xmalloc(conns_space * sizeof(struct switch_data*));
	sw_record->sw_conn_speed = xmalloc(conns_space * sizeof(int));

	// loops through all of the switch names in the switch name string
	while (sw_name != NULL) {
		switch_data *ptr = switch_data_table;
		int i;

		if (conn_count == conns_space) {
			fatal("%s has +%d connections which is more than "
			      "allocated space for",
			      sw_record->name, conn_count);
		}

		// look up node struct and add pointer to it in switch's struct
		for (i = 0; i < switch_data_cnt; i++, ptr++) {
			if (xstrcmp(ptr->name, sw_name) == 0) {
				sw_record->sw_conns[conn_count] = ptr;
				break;
			}
		}

		if (i == switch_data_cnt) {
			fatal("Could not find switch record for %s in switch "
			      "connection list", sw_name);
		}
		sw_name = strtok (NULL, ",-");

		// parses the link speed for this switch connection
		link_speed = _parse_link_speed(&sw_name);
		if (link_speed < 1) {
			fatal("Invalid switch speed of %s between switches "
			      "%s and %s",
			      sw_name, sw_record->name, ptr->name);
			return 0; /* For CLANG false positive */
		}

		// creates final connection speed by dividing the
		// connection time between the two switches by the link_speed
		sw_record->sw_conn_speed[conn_count] =
			_get_connection_time(sw_record, ptr) / link_speed;
		conn_count++;
	}

	// resize memory allocated for switch connections to right size
	_resize_switch_connections( sw_record, conns_space, conn_count );

	return conn_count;
}


// Parses the link speed for this switch connection
static int _parse_link_speed(char **sw_name)
{
	int link_speed = 0;

	if (_char2int(*sw_name[0]) > -1) {
		//if there is a link speed for this connection
		int counter = 0;

		while (_char2int((*sw_name)[counter]) > -1) {
			link_speed = link_speed * 10 +
				     _char2int((*sw_name)[counter]);
			counter++;
		}

		if (link_speed < 1) {
			return link_speed;
		}

		*sw_name = strtok(NULL, ",-");
	} else {
		link_speed = default_link_speed;
	}

	return link_speed;
}


// returns the integer value for a number character
static int _char2int(char coord)
{
	if ((coord >= '0') && (coord <= '9')) {
		return (coord - '0');
	}

	return -1;
}


// returns the connection time for switches based on their locations
static int _get_connection_time(const switch_data *sw_ptr1,
				const switch_data *sw_ptr2)
{
	if (sw_ptr1->rack_number == sw_ptr2->rack_number){
		if (sw_ptr1->iru_number == sw_ptr2->iru_number) {
			return switch_time_same_iru;
		} else {
			return switch_time_same_rack;
		}
	} else {
		return switch_time_diff_rack;
	}
}


// resize memory allocated for switch connections to right size
static void _resize_switch_connections(switch_data * sw_record,
				       int conns_space, int conn_count)
{

	// resize switch connections if there are less than originally allocated for
	if (conn_count < conns_space) {
		switch_data **tmp_sw_conns = xrealloc(
			sw_record->sw_conns,
			conn_count * sizeof(struct switch_data*));
		int * tmp_sw_conn_speed = xrealloc(
			sw_record->sw_conn_speed,
			conn_count * sizeof(int));

		if ((tmp_sw_conns != NULL) && (tmp_sw_conn_speed != NULL)) {
			sw_record->sw_conns = tmp_sw_conns;
			sw_record->sw_conn_speed = tmp_sw_conn_speed;
		} else {
			fatal("Error (re)allocating memory for connected "
			      "switches for switch %s", sw_record->name);
		}
	}
}


// extracts a switch's location from its name ( Rack, IRU, and Server number)
static void _update_location_info(switch_data * switch_ptr)
{
	char *name = switch_ptr->name;
	int name_len = strlen(name);
	uint32_t sw_num[3] = {0, 0, 0}; // numbers store rack, IRU & switch numbers
	char name_char[3] = {'r', 'i', 's'};
	int i, j = 0;

	// loop through all characters in servers name extracting numbers
	for (i = 0; i < 3; i++) {
		if ((name_char[i] != name[j]) || (_char2int(name[j + 1]) < 0)) {
			fatal("switch %s lacks valid naming syntax", name);
		}

		j++;
		while ((_char2int(name[j]) > -1) && (j < name_len)) {
			sw_num[i] = sw_num[i] * 10 + _char2int(name[j]);
			if (sw_num[i] > 1023) {
				fatal("switch %s has %c value that exceeds "
				      "limit (%d>1023)",
				      name, name_char[i], sw_num[i]);
			}

			j++;
		}
	}

	if (j < name_len) {
		fatal("switch %s lacks valid naming syntax", name);
	}
	switch_ptr->rack_number = sw_num[0];
	switch_ptr->iru_number = sw_num[1];
	switch_ptr->switch_number = sw_num[2];
}


//////////////////////////////////////////////////////////////////////////////
//// Coordinate Related Functions ////

/*
 * Sets coordinates for the switches in accordance with the hypercube topology
 * - First, it picks one switch to be the starting point of the coordinate
 * system and assigns it all zero coordinates
 * - Second, move outwards from starting switch, by assigning coordinates to
 * all of the switches connected to the starting switch. Each of these
 * secondary switches has zeros for coordinates except has a 1 in 1 of its
 * dimensions, with each switch having a 1 in a different dimension.
 * - Lastly, continue to move out from the secondary switches by finding others
 *  switches that they are connected to, giving them coordinates, & repeating
 */
static int _coordinate_switches(void)
{
	int counter, j;

	// create a temp record_table that will store all switches that
	// have been assigned coordinates
	switch_data ** coordinated_switch_data_table =
		xmalloc(sizeof(struct switch_data*) * switch_data_cnt);
	int coordinated_switch_data_count = 0;
	switch_data *switch_ptr = NULL;

	_zero_coordinates();

	// Find origin node and add to coordinated_switch_data_table
	counter = 0;
	switch_ptr = &switch_data_table[counter];
	while (switch_ptr->sw_conn_cnt < hypercube_dimensions) {
		switch_ptr = &switch_data_table[++counter];
	}

	coordinated_switch_data_table[coordinated_switch_data_count] = switch_ptr;
	coordinated_switch_data_count++;

	/* Add 1st round of switches to coordinate system and assign coordinates */
	for (j = 0; j < switch_ptr->sw_conn_cnt; j++) {
		switch_ptr->sw_conns[j]->coordinates[j] = 1;
		coordinated_switch_data_table[coordinated_switch_data_count] =
			switch_ptr->sw_conns[j];
		coordinated_switch_data_count++;
	}

	// while there are still switches without coordinates continue to loop
	while (coordinated_switch_data_count < switch_data_cnt) {
		coordinated_switch_data_count = _find_new_switches(
				coordinated_switch_data_table,
				coordinated_switch_data_count);
	}

	debug("Finished calculating coordinates for switches");
	xfree(coordinated_switch_data_table);

	return 1;
}


/* Sets all of the coordinates in the switches equal to zero */
static void _zero_coordinates(void)
{
	int i, j;

	for (i = 0; i < switch_data_cnt; i++) {
		for (j = 0; j < hypercube_dimensions; j++) {
			switch_data_table[i].coordinates[j] = 0;
		}
	}
}


/*
 * Finds & adds neighboring switch to coordinated table & gives them coordinates
 * - In order for a switch to be given coordinates, it has to be connected
 * to two switches that already have coordinates. When a neighboring switch is
 * found without coordinates it is added to a temp list. Then once that switch
 * is found by another neighboring switch, the new switch is added to the
 * coordinated switch list and given coordinates equal to the OR of the
 * coordinates of the two neighboring switches that found it.
 * - If the program cannot find any more uncoordinated switches with two
 * coordinated neighbors, but there are still switches that need coordinates,
 * then the program resorts to coordinating switches based on only 1 neighbor
 */
static int _find_new_switches(switch_data **switch_table, int record_count)
{
	switch_data **temp_record_table = xmalloc(
		sizeof(struct switch_data*) * switch_data_cnt);
	int i, j, temp_record_count = 0, old_record_count = record_count;
	switch_data *switch_ptr;

	// loop through all of the switches with coordinates
	for (i = 0; i < record_count; i++) {
		switch_ptr = switch_table[i];

		// loop through all of the switches that a switch is connected to
		for (j = 0; j < switch_ptr->sw_conn_cnt; j++) {
			int index = _get_switch_index(
				temp_record_table,
				temp_record_count, switch_ptr->sw_conns[j]);

			/*
			 * If this is an uncoordinated switch and it was on the
			 * temp_record_table, meaning that it was already found by
			 * one neighboring switch, then give it coordinates and
			 * add it to the switch_table
			 */
			if (index > -1) {
				_or_coordinates(switch_ptr, switch_ptr->sw_conns[j]);
				switch_table[record_count] =
					switch_ptr->sw_conns[j];
				record_count++;
				temp_record_table[index] = NULL;
			}

			/*
			 * If the switch was not already on the temp_record_table,
			 * but it doesn't have coordinates, then add it to the
			 * temp_record_table
			 */
			else if (_get_switch_index(switch_table, record_count,
						   switch_ptr->sw_conns[j]) < 0) {
				_copy_coordinate(switch_ptr, switch_ptr->sw_conns[j]);
				temp_record_table[temp_record_count] =
					switch_ptr->sw_conns[j];
				temp_record_count++;
			}
		}
	}

	// if there are no more uncoordinated switches with 2 coordinated neighbors
	if (record_count == old_record_count) {
		if (temp_record_count == 0) {
			fatal("Could not coordinate all switches listed."
			      "Please recheck switch connections in "
			      "topology.conf file");
		}

		// Add switches that only have 1 coordinated neighbor to switch_table
		for (i = 0; i < temp_record_count; i++) {
			switch_ptr = temp_record_table[i];
			if (switch_ptr != NULL) {
				switch_table[record_count] = temp_record_table[i];
				switch_table[record_count]->coordinates[j] = 1;
				record_count++;
				temp_record_table[i] = NULL;
			}
		}
	}

	xfree(temp_record_table);
	return record_count;
}


/* Return index of a given switch name or -1 if not found */
static int _get_switch_index(switch_data ** switch_table,
			     int record_count, const switch_data * switch_ptr)
{
	int i;

	for (i = 0; i < record_count; i++) {
		const switch_data * ptr = switch_table[i];

		if (ptr != NULL) {
			if (xstrcmp(ptr->name, switch_ptr->name) == 0) {
				return i;
			}
		}
	}

	return -1;
}


/* Bitwise OR on coordiantes of 1st & 2nd switch saves result in 2nd switch */
static void _or_coordinates(const switch_data *src_ptr, switch_data *dest_ptr)
{
	int i;

	for (i = 0; i < hypercube_dimensions; i++) {
		dest_ptr->coordinates[i] = src_ptr->coordinates[i] |
			dest_ptr->coordinates[i];
	}
}


/* Copies the coordiantes of the first switch to the second switch */
static void _copy_coordinate(const switch_data *src_switch_ptr,
				 switch_data *dest_switch_ptr)
{
	int i;

	for (i = 0; i < hypercube_dimensions; i++) {
		dest_switch_ptr->coordinates[i] =
			src_switch_ptr->coordinates[i];
	}
}


//////////////////////////////////////////////////////////////////////////////
//// Hilbert Curve, Switch Ranking and Distance Related Functions ////


/*
 * Allocates memory for hypercube_switch_table and hypercube_switches and
 * copy important data from switch_data_table to hypercube_switch_table
 */
static void _build_hypercube_switch_table(int num_curves)
{
	int i, j;

	_free_hypercube_switch_table();
	hypercube_switch_cnt = switch_data_cnt;
	hypercube_switch_table =
		xmalloc(sizeof(struct hypercube_switch) * switch_data_cnt);

	// copy important data from switch_data_table to hypercube_switch_table
	for (i = 0; i < switch_data_cnt; i++ ) {
		hypercube_switch_table[i].switch_index =
			switch_data_table[i].index;
		hypercube_switch_table[i].switch_name = xmalloc(
			strlen(switch_data_table[i].name) + 1);

		strcpy(hypercube_switch_table[i].switch_name,
			switch_data_table[i].name);
		hypercube_switch_table[i].node_bitmap =
			bit_copy(switch_data_table[i].node_bitmap);
		hypercube_switch_table[i].node_cnt =
			switch_data_table[i].node_conn_cnt;
		hypercube_switch_table[i].avail_cnt = 0;
		hypercube_switch_table[i].node_index = xmalloc(
			sizeof(int) * hypercube_switch_table[i].node_cnt);

		for (j = 0; j < hypercube_switch_table[i].node_cnt; j++) {
			hypercube_switch_table[i].node_index[j] =
				switch_data_table[i].node_index[j];
		}

		hypercube_switch_table[i].distance = xmalloc(
			sizeof(int32_t) * num_curves);
		xassert(num_curves >= hypercube_dimensions);
		for (j = 0; j < hypercube_dimensions; j++) {
			hypercube_switch_table[i].distance[j] = 0;
		}
	}

	// allocated space for the pointers to each of the different curves
	hypercube_switches =
		xmalloc(sizeof(struct hypercube_switch **) * num_curves);
}


/* apply a linear transformation to the switches coordinates so to produce
	a unique mapping from switch data to Hilbert curve */
static void _transform_coordinates(int curve_num)
{
	int i, j, dim;

	// if it is the first curve, set up orig_coordinates struct
	// and copy coordinates to orig_coordinates to be stored
	if (curve_num == 0) {
		for (i = 0; i < switch_data_cnt; i++) {
			for (j = 0; j < hypercube_dimensions; j++) {
				switch_data_table[i].orig_coordinates[j] =
					switch_data_table[i].coordinates[j];
			}
		}
		return;
	}

	// copy the original coordinates to the temp coordinates of the switch
	// and center the coordinates around the origin of coordinate system
	for (i = 0; i < switch_data_cnt; i++) {
		for (j = 0; j < hypercube_dimensions; j++) {
			switch_data_table[i].coordinates[j] =
				2 * switch_data_table[i].orig_coordinates[j] - 1;
		}
	}

	// apply a linear transformation to centered coordinates
	dim = (curve_num + 1 ) % hypercube_dimensions;
	for (i = 0; i < switch_data_cnt; i++) {
		int temp = switch_data_table[i].coordinates[curve_num];

		switch_data_table[i].coordinates[curve_num] =
			switch_data_table[i].coordinates[dim];
		switch_data_table[i].coordinates[dim] = -1 * temp;
	}

	// uncenter the coordinates back to the range [0,1]
	for (i = 0; i < switch_data_cnt; i++) {
		for (j = 0; j < hypercube_dimensions; j++) {
			switch_data_table[i].coordinates[j] =
				(switch_data_table[i].coordinates[j] + 1 ) / 2;
		}
	}
}


/*
 * Creates Hilbert integers for each of the switches in the topology.
 * Hilbert Curve algorithm and AxestoTranspose function taken from torus
 * topology plugin and modified slightly to account for hypercube topology.
 */
static void _generate_hilbert_integers(void)
{
	switch_data * switch_ptr = switch_data_table;
	int counter, switch_rank;
	int i, j;
	unsigned int *hilbert;

	if (hypercube_dimensions <= 0)
		return;

	hilbert = xmalloc(sizeof(unsigned int) * hypercube_dimensions);
	for (i = 0; i < switch_data_cnt; i++, switch_ptr++) {
		for (j = 0; j < hypercube_dimensions; j++) {
			hilbert[j] = switch_ptr->coordinates[j];
		}

		/*
		 * Gray encode switch coordinates and then use the output to
		 * create switch's rank
		 */
		_axes_to_transpose(hilbert, 1, hypercube_dimensions);

		for (j = hypercube_dimensions - 1, counter = 0, switch_rank = 0;
		     j >= 0; j--, counter++) {
			switch_rank += (hilbert[j] & 1) << counter;
		}
		switch_ptr->rank = switch_rank;
	}
	xfree(hilbert);
}


/* Runs Hilbert Curve Algorithm on switch coordinates to create Gray code
 * that can be used to make the Hilbert Integer for the switch */
// 			      ( position [n], # bits, dimension )
static void _axes_to_transpose(unsigned int * x, int b, int n)
{
	unsigned int p, q, t;
	int i;

	// Inverse undo
	for (q = 1 << (b - 1); q > 1; q >>= 1) {
		p = q - 1;
		if (x[0] & q) {
			x[0] ^= p; // invert
		}

		for (i = 1; i < n; i++) {
			if (x[i] & q) {
				x[0] ^= p; // invert
			} else { // exchange
				t = (x[0] ^ x[i]) & p;
				x[0] ^= t;
				x[i] ^= t;
			}
		}
	}

	// Gray encode (inverse of decode)
	for (i = 1; i < n; i++) {
		x[i] ^= x[i-1];
	}
	t = x[n-1];
	for (i = 1; i < b; i <<= 1) {
		x[n-1] ^= x[n-1] >> i;
	}
	t ^= x[n-1];
	for (i = n - 2; i >= 0; i--) {
		x[i] ^= t;
	}
}


/*
 * Sort switches by their Hilbert integer ranks
 */
static void _sort_switches_by_rank(int curve_num)
{
	int i, j, min_inx;
	uint32_t min_val;
	switch_data ** ranked_switch_table = xmalloc(
		sizeof(struct switch_data*) * switch_data_cnt);

	for (i = 0; i < switch_data_cnt; i++) {
		ranked_switch_table[i] = &switch_data_table[i];
	}

	/* Now we need to sort the switch records */
	for (i = 0; i < switch_data_cnt; i++) {
		min_val = ranked_switch_table[i]->rank;
		min_inx = i;
		for (j = i + 1; j < switch_data_cnt; j++) {
			if (ranked_switch_table[j]->rank < min_val) {
				min_val = ranked_switch_table[j]->rank;
				min_inx = j;
			}
		}

		if (min_inx != i) {	// swap records
			switch_data * sw_record_tmp = ranked_switch_table[i];

			ranked_switch_table[i] = ranked_switch_table[min_inx];
			ranked_switch_table[min_inx] = sw_record_tmp;
		}
	}

	for (i = 0; i < switch_data_cnt; i++) {
		ranked_switch_table[i]->rank = i;
	}

	_create_sorted_switch_distances(curve_num, ranked_switch_table);

	xfree(ranked_switch_table);
}


// Calculate and update distances for sorted switches
static void _create_sorted_switch_distances(
	int curve_num, switch_data **ranked_switch_table)
{
	int i, total_distance = 0;

	/* Create distance from switches to first switch in ranked table */
	total_distance += _get_switch_distance(
		ranked_switch_table[0],
		ranked_switch_table[switch_data_cnt - 1]);
	ranked_switch_table[0]->distance = total_distance;

	/* Keep adding up so we have distance back to [0] */
	for (i = 1; i < switch_data_cnt; i++) {
		total_distance += _get_switch_distance(
			ranked_switch_table[i],
			ranked_switch_table[i - 1]);
		ranked_switch_table[i]->distance = total_distance;
	}

	/* Copy distances to hypercube_switch_table and add sorted pointers */
	hypercube_switches[curve_num] =
		xmalloc(sizeof(struct hypercube_switch *) * switch_data_cnt);

	for (i = 0; i < switch_data_cnt; i++ ) {
		int index = ranked_switch_table[i]->index;

		hypercube_switch_table[index].distance[curve_num] =
			ranked_switch_table[i]->distance;
		hypercube_switches[curve_num][i] =
			&hypercube_switch_table[index];
	}
}


/* returns the connection distance for two neighbor switches in ranked table */
static int _get_switch_distance(const switch_data *sw_ptr1,
				const switch_data *sw_ptr2)
{
	int i;

	for (i = 0; i < sw_ptr1->sw_conn_cnt; i++) {
		if (sw_ptr1->sw_conns[i] == sw_ptr2) {
			return sw_ptr1->sw_conn_speed[i];
		}
	}

	/*
	 * The switches are not linked in the Hilbert path of this machine.
	 * We return a really big number to indicate this.
	 */
	return switch_time_unlinked;
}


//////////////////////////////////////////////////////////////////////////////
//// String Creation and Printing Related Function ////

/* prints switch_strings for all switches in the switch record table */
static void _print_switch_data_table(void)
{
	switch_data *switch_ptr = switch_data_table;
	int i;

	debug("Switch record table has %d switch records in it",
	      switch_data_cnt);
	for (i = 0; i < switch_data_cnt; i++, switch_ptr++) {
		_print_switch_str(switch_ptr, 1, "    ");
	}
}


/* prints name and coordinates of all switches in hypercube switch table*/
static void _print_hypercube_switch_table( int num_curves )
{
	int i, j;

	debug("Hypercube table has %d switch records in it",
	      hypercube_switch_cnt);
	for (i = 0; i < hypercube_switch_cnt; i++ ) {
		char *distances = xstrdup("Distances: ");
		char *nodes = xstrdup("Node Index: ");
		for ( j = 0; j < num_curves; j++ ){
			if (hypercube_switch_table[i].distance[j]) {
				xstrfmtcat(distances, "%d, ",
					   hypercube_switch_table[i].distance[j]);
			} else
				break;
		}
		for ( j = 0; j < hypercube_switch_table[i].node_cnt; j++ ) {
			xstrfmtcat(nodes, "%d, ",
				   hypercube_switch_table[i].node_index[j]);
		}
		debug("    %s: %d - %s %s",
		      switch_data_table[i].name, i, distances,nodes);
		xfree(distances);
		xfree(nodes);
	}
}


/* Prints Hypercube switch tables sorted by Hilbert Curve Integers */
static void _print_sorted_hilbert_curves( int num_curves )
{
	int i, j;

	debug("Hilbert Curves Ranking Created for %d Hilbert Curves",
	      num_curves);
	for ( i = 0 ; i < hypercube_switch_cnt ; i++ ) {
		char *s = xstrdup("-- ");
		for ( j = 0 ; j < num_curves ; j++ ) {
			xstrfmtcat(s, "%7s -%4d,  ",
				   hypercube_switches[j][i]->switch_name,
				   hypercube_switches[j][i]->switch_index);
		}
		debug("%s", s);
		xfree(s);
	}
}


/* returns a string of a switch's name coordinates and connections */
static char *_print_switch_str(switch_data *switch_ptr, int print, char *offset)
{
	char *str = NULL;
	char *coordinates = _create_coordinate_str(switch_ptr);
	char *connections = _create_connection_str(switch_ptr);
	char *conn_nodes = _create_conn_node_str(switch_ptr);

	xstrfmtcat(str, "%s%s -- coordinates: %s -- connections:%s -- nodes:%s",
		   offset, switch_ptr->name, coordinates, connections,
		   conn_nodes);
	xfree(coordinates);
	xfree(connections);
	xfree(conn_nodes);

	if (print == 1) {
		debug("%s", str);
		xfree(str);
		return NULL;
	}
	return str;
}


/* returns a string of the coordinates for a switch */
static char *_create_coordinate_str(switch_data *switch_ptr)
{
	int i;
	char *str = xstrdup("(");
	for (i = 0; i < hypercube_dimensions; i++) {
		xstrfmtcat(str, "%d,", switch_ptr->coordinates[i]);
	}
	str[strlen(str)-1] = ')';
	return str;
}


/* returns a string of the connections for a switch */
static char *_create_connection_str(switch_data *switch_ptr)
{
	int i;
	char *str = NULL;
	for (i = 0; i < switch_ptr->sw_conn_cnt; i++) {
		xstrfmtcat(str, "%s-%d,", switch_ptr->sw_conns[i]->name,
			   switch_ptr->sw_conn_speed[i]);
	}
	if (str)
		str[strlen(str)-1] = '\0';
	return str;
}


/* returns a string of the names of the connected nodes for a switch */
static char *_create_conn_node_str(switch_data *switch_ptr)
{
	int i;
	char *str = NULL;
	for (i = 0; i < switch_ptr->node_conn_cnt; i++) {
		xstrfmtcat(str, "%s,", switch_ptr->node_conns[i]->name);
	}
	if (str)
		str[strlen(str)-1] = '\0';
	return str;
}


//////////////////////////////////////////////////////////////////////////////
//// Memory Freeing and Allocating Functions ////

/* Free all memory associated with slurm_conf_switches_t structure */
static void _destroy_switches(void *ptr)
{
	slurm_conf_switches_t *s = (slurm_conf_switches_t *)ptr;
	xfree(s->nodes);
	xfree(s->switch_name);
	xfree(s->switches);
	xfree(ptr);
}


/* Free all memory associated with switch_data_table structure */
static void _free_switch_data_table(void)
{
	int i;

	if (switch_data_table) {
		for (i = 0; i < switch_data_cnt; i++) {
			xfree(switch_data_table[i].name);
			xfree(switch_data_table[i].nodes);
			xfree(switch_data_table[i].switches);
			xfree(switch_data_table[i].coordinates);
			xfree(switch_data_table[i].orig_coordinates);
			xfree(switch_data_table[i].sw_conns);
			xfree(switch_data_table[i].sw_conn_speed);
			xfree(switch_data_table[i].node_conns);
			xfree(switch_data_table[i].node_index);
			FREE_NULL_BITMAP(switch_data_table[i].node_bitmap);
		}
		xfree(switch_data_table);
	}
}

/* Free all memory associated with hypercube_switch_table structure */
static void _free_hypercube_switch_table(void)
{
	int i;

	if (hypercube_switch_table) {
		for (i = 0; i < hypercube_switch_cnt ; i++) {
			xfree(hypercube_switch_table[i].switch_name);
			xfree(hypercube_switch_table[i].node_index);
			xfree(hypercube_switch_table[i].distance);
			FREE_NULL_BITMAP(hypercube_switch_table[i].node_bitmap);
		}
		xfree(hypercube_switch_table);
	}
}
