/*****************************************************************************
 *  Copyright (C) 2014 Silicon Graphics International Corp.
 *  All rights reserved. 
 ****************************************************************************/
#if HAVE_CONFIG_H
#  include "config.h"
#endif

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <sys/param.h>

#include <netloc.h>
#ifdef HAVE_NETLOC_NOSUB
#  include <netloc_map.h>
#else
#  include <netloc/map.h>
#endif

typedef struct node_group {
	char *node_name;
	int node_name_len; 
	int cpus;
	int memory;
	int cores_per_socket;
	int threads_per_core;
} node_group;

typedef struct switch_name {
	const char *sw_name;
	unsigned long physical_id; 
} switch_name;


// Parse the command line arguments and update variables appropriately 
static int parse_args(int argc, char ** argv);

// Check the directory parameters to make sure they are formatted correctly
static int check_directory_parameters();
						
// initialize NetLoc topology to be used to lookup NetLoc information
static netloc_topology_t setup_topology(char *data_uri);

// initialize NetLoc map to be used to lookup HwLoc information
static netloc_map_t setup_map(char *data_uri);

// Generate a topology.conf file based on NetLoc topology and save it to file
static int generate_topology_file(netloc_topology_t *topology, netloc_map_t *map);

// Loop through and parse all of the switches and their connections
static int loop_through_switches(netloc_topology_t *topology, 
					netloc_map_t *map, netloc_dt_lookup_table_t *switches);
						
// Loop through and parse all of the edges for a switch
static int loop_through_edges(netloc_topology_t *topology, netloc_map_t *map, 
					netloc_node_t *node, const char *src_name, FILE *f_temp);


// Add a switch connection and its link speed to the switch list
static int add_switch_connection(netloc_edge_t **edges, int idx, int num_edges,
			const char *src_name, const char *dst_name, char *switch_str);

// calculate the link speed for an edge between two switches
static int calculate_link_speed(netloc_edge_t *edge);
							
// Add a node connection to the node list
static int add_node_connection( netloc_topology_t *topology, netloc_map_t *map, 
						netloc_edge_t *edge, char *node_str );
						
// Find a node group that matches the specifications given
static int find_node_group( int cpus, int cores_per_socket, 
					 int threads_per_core, int memory, const char *dst_name);

// Make a new node group in the table and fill in information
static void make_new_node_group( int cpus, int cores_per_socket, 
					 int threads_per_core, int memory, const char *dst_name);

// Save Topology data of network to topology.conf file
static int save_topology_data_to_file();

// Gets the name and the hw_loc topology for a NetLoc node
static int get_node_name_and_topology(netloc_topology_t *topology, netloc_map_t *map, 
			netloc_node_t *node, const char **name, hwloc_topology_t *hw_topo);
			
// Gets the name of a switch in the network
static int get_switch_name( netloc_topology_t *topology, netloc_map_t *map, 
									netloc_node_t *node, const char **name );

// Find a switch_name that matches the Physical ID given
static int find_switch_name( netloc_node_t *node );

// Compares switch_name with all of the names in the table
static int check_unique_switch_name( char *sw_name);

// Make a new switch_name entry in the table and fill in information
static int make_new_switch_name( netloc_topology_t *topology, netloc_map_t *map,
									netloc_node_t *node, const char **name );

#define NETLOC_DIR "netloc"

const char * ARG_OUTDIR         = "--outdir";
const char * ARG_SHORT_OUTDIR   = "-o";
const char * ARG_DATADIR         = "--datadir";
const char * ARG_SHORT_DATADIR   = "-d";
const char * ARG_VERBOSE         = "--verbose";
const char * ARG_SHORT_VERBOSE   = "-v";
const char * ARG_FABRIC         = "--fabric";
const char * ARG_SHORT_FABRIC   = "-f";
const char * ARG_HELP           = "--help";
const char * ARG_SHORT_HELP     = "-h";

static char * outdir = NULL;
static char * datadir = NULL;
static char * fabric = "fe80:0000:0000:0000";
static int verbose = 0;

static int max_nodes = 0, max_switches = 0;
static node_group *node_group_table = NULL;
static int node_group_cnt = 0;
static int node_groups_max  = 32;
static switch_name **switch_name_table = NULL;
static int switch_name_cnt = 0;
static int switch_name_max  = 256;
static char *file_location = NULL, *file_location_temp= NULL;


int main(int argc, char ** argv) {
	int ret;
	netloc_topology_t topology;
	netloc_map_t map;
	
	// Parse the command line arguments and update variables appropriately 
	if( 0 != parse_args(argc, argv) ) {
		printf(
"Usage: %s\n"
"\t%s|%s <directory with hwloc and netloc data directories>\n"
"\t[%s|%s <output directory>]\n"
"\t[%s|%s <IB Fabric ID, eg. fec0:0000:0000:0000>]\n"
"\t[%s|%s] [--help|-h]\n",
			   argv[0],
			   ARG_DATADIR, ARG_SHORT_DATADIR,
			   ARG_OUTDIR, ARG_SHORT_OUTDIR,
			   ARG_FABRIC, ARG_SHORT_FABRIC,
			   ARG_VERBOSE, ARG_SHORT_VERBOSE);
		printf("     Default %-10s = current working directory\n", ARG_OUTDIR);
		return NETLOC_ERROR;
	}
  
	asprintf(&file_location, "%stopology.conf", outdir);
	asprintf(&file_location_temp, "%s.temp", file_location);
	
	// initialize NetLoc topology to be used to lookup NetLoc information
	topology = setup_topology(datadir);
	(verbose) ? printf("Successfully Created Network Topology \n") : 0 ;
	
	// initialize NetLoc map to be used to lookup HwLoc information
	map = setup_map(datadir);
	(verbose) ? printf("Successfully Created Network Map\n") : 0 ;
	
	node_group_table = malloc( sizeof(node_group) * node_groups_max );
	switch_name_table = malloc( sizeof(switch_name *) * switch_name_max );

	// Generate a topology.conf file based on NetLoc topology and save to file
	ret = generate_topology_file(&topology, &map);
	
	if( NETLOC_SUCCESS == ret ) 
		printf("\nDone generating topology.conf file from NetLoc data\n");
	else
		printf("Error: Couldn't Create topology.conf file from NetLoc data\n");

	netloc_detach(topology);
	netloc_map_destroy(map);
	return ret;
}

// Parse the command line arguments and update variables appropriately 
static int parse_args(int argc, char ** argv) {
	int i, ret = NETLOC_SUCCESS;

	for(i = 1; i < argc; ++i ) {
		// --outdir
		if( ( 0 == strncmp(ARG_OUTDIR, argv[i], strlen(ARG_OUTDIR)) ) ||
		(0 == strncmp(ARG_SHORT_OUTDIR, argv[i], strlen(ARG_SHORT_OUTDIR))) ) {
			++i;
			if( i >= argc ) {
				fprintf(stderr, "Error: Must supply an argument to %s\n",
															ARG_OUTDIR );
				return NETLOC_ERROR;
			}
			outdir = strdup(argv[i]);
		}
		// --datadir (directory with hwloc and netloc input data directories)
		else if( 0 ==strncmp(ARG_DATADIR,       argv[i], strlen(ARG_DATADIR)) ||
		0 == strncmp(ARG_SHORT_DATADIR, argv[i], strlen(ARG_SHORT_DATADIR)) ) {
			++i;
			if( i >= argc ) {
				fprintf(stderr, "Error: Must supply an argument to %s "
								"(input data directory)\n", ARG_DATADIR );
				return NETLOC_ERROR;
			}
			datadir = strdup(argv[i]);
		}
		// verbose output
		else if( 0 == strncmp(ARG_VERBOSE, argv[i], strlen(ARG_VERBOSE)) ||
		(0 == strncmp(ARG_SHORT_VERBOSE, argv[i], strlen(ARG_SHORT_VERBOSE)))){
			verbose = 1;
		}
		// Help
		else if( 0 == strncmp(ARG_HELP,       argv[i], strlen(ARG_HELP)) ||
		0 == strncmp(ARG_SHORT_HELP, argv[i], strlen(ARG_SHORT_HELP)) ) {
			return NETLOC_ERROR;
		} else if (0 == strcmp(ARG_FABRIC, argv[i]) ||
			    0 == strcmp(ARG_SHORT_FABRIC, argv[i])) {
			i++;
			if (i >= argc) {
				fprintf(stderr,
"Error: Must supply an argument to %s (fabric ID)\n",
					 ARG_FABRIC);
			}

			fabric = strdup(argv[i]);
		}
		// Unknown options throw warnings
		else {
			fprintf(stderr, "Warning: Unknown argument of <%s>\n", argv[i]);
			return NETLOC_ERROR;
		}
	}
	// Check the directory parameters to make sure they are formatted correctly
	ret = check_directory_parameters();
	return ret;
}


// Check the directory parameters to make sure they are formatted correctly
static int check_directory_parameters() {
	int ret = NETLOC_SUCCESS;
	
	// Check Output Directory Parameter
	if( NULL == outdir || strlen(outdir) <= 0 ) {
		if( NULL != outdir )
			free(outdir);
		// Default: current working directory
		outdir = strdup(".");
	}
	if( '/' != outdir[strlen(outdir)-1] ) {
		outdir = (char *)realloc(outdir, sizeof(char) * (strlen(outdir)+1));
		outdir[strlen(outdir)+1] = '\0';
		outdir[strlen(outdir)]   = '/';
	}
	
	// Check Input Data Directory Parameter
	if( NULL == datadir || strlen(datadir) <= 0 ) {
		fprintf(stderr, "Error: Must supply an argument to %s|%s (input data"
						" directory)\n", ARG_DATADIR, ARG_SHORT_DATADIR );
		return NETLOC_ERROR;
	}
	else if( '/' != datadir[strlen(datadir)-1] ) {
		datadir = (char *)realloc(datadir, sizeof(char) * (strlen(datadir)+1));
		datadir[strlen(datadir)+1] = '\0';
		datadir[strlen(datadir)]   = '/';
	}
	
	// Display Parsed Arguments
	(verbose) ? printf("  Input Data Directory: %s\n", datadir) : 0 ;
	(verbose) ? printf("  Output Directory    : %s\n", outdir) : 0 ;
	return ret;
}


// initialize NetLoc topology to be used to lookup NetLoc information
static netloc_topology_t setup_topology(char *data_uri)
{
	int ret;
	netloc_topology_t topology;
	netloc_network_t *tmp_network = NULL;
	char *search_uri = NULL;

	// Setup a Network connection
	tmp_network = netloc_dt_network_t_construct();
	tmp_network->network_type = NETLOC_NETWORK_TYPE_INFINIBAND;
	tmp_network->subnet_id    = strdup(fabric);

	asprintf(&search_uri, "file://%s%s", data_uri, NETLOC_DIR);
	ret = netloc_find_network(search_uri, tmp_network);
	free(search_uri);
	if (NETLOC_SUCCESS != ret) {
		fprintf(stderr,
			 "Error: netloc_find_network return error (%d)\n"
			 "\tConsider passing a different IB fabric ID with -f\n",
			 ret);
		exit(ret);
	}

	// Attach to the topology context
	ret = netloc_attach(&topology, *tmp_network);
	netloc_dt_network_t_destruct(tmp_network);
	if( NETLOC_SUCCESS != ret ) {
		fprintf(stderr, "Error: netloc_attach returned an error (%d)\n", ret);
		exit(ret);
	}
	return topology;
}

// initialize NetLoc map to be used to lookup HwLoc information
static netloc_map_t setup_map(char *data_uri)
{
	int err;
	netloc_map_t map;
	char *path;

	err = netloc_map_create(&map);
	if (err) {
		fprintf(stderr, "Failed to create the map\n");
		exit(EXIT_FAILURE);
	}

	asprintf(&path, "%shwloc", data_uri);

	err = netloc_map_load_hwloc_data(map, path);
	free(path);
	if (err) {
		fprintf(stderr, "Failed to load hwloc data\n");
		exit(EXIT_FAILURE);
	}

	asprintf(&path, "file://%s%s", data_uri, NETLOC_DIR);

	err = netloc_map_load_netloc_data(map, path);
	free(path);
	if (err) {
		fprintf(stderr, "Failed to load netloc data\n");
		exit(EXIT_FAILURE);
	}

	err = netloc_map_build(map, 0);
	if (err) {
		fprintf(stderr, "Failed to build map data\n");
		exit(EXIT_FAILURE);
	}

	return map;
}


// Generate a topology.conf file based on NetLoc topology and save it to file
static int generate_topology_file(netloc_topology_t *topology, 
							netloc_map_t *map)
{
	int ret;
	netloc_dt_lookup_table_t switches = NULL;
	
	// Get all of the switches
	ret = netloc_get_all_switch_nodes(*topology, &switches);
	if( NETLOC_SUCCESS != ret ) {
		fprintf(stderr, "Error: get_all_switch_nodes returned %d\n", ret);
		return ret;
	}
	
	// Loop through and parse all of the switches and their connections
	ret = loop_through_switches(topology, map, &switches);
	if( NETLOC_SUCCESS != ret ) {
		fprintf(stderr, "Error: loop_through_switches returned %d\n", ret);
		return ret;
	}
	
	// Save Topology data of network to topology.conf file
	save_topology_data_to_file();
	
	// Cleanup 
	netloc_lookup_table_destroy(switches);
	free(switches);

	free(file_location);
	free(file_location_temp);
	int i;
	for ( i = 0; i < node_group_cnt; i++)
		free(node_group_table[i].node_name);
	free(node_group_table);
	for ( i = 0; i < switch_name_cnt; i++)
		free(switch_name_table[i]);
	free(switch_name_table);
	return NETLOC_SUCCESS;
}


// Loop through and parse all of the switches and their connections
static int
loop_through_switches(netloc_topology_t *topology, 
			 netloc_map_t *map, netloc_dt_lookup_table_t *switches)
{
	int ret;
	netloc_dt_lookup_table_iterator_t hti = NULL;
	FILE *f_temp = fopen(file_location_temp, "w");

	/* Loop through all of the switches */
	hti = netloc_dt_lookup_table_iterator_t_construct(*switches);
	while (!netloc_lookup_table_iterator_at_end(hti)) {
		const char * key = netloc_lookup_table_iterator_next_key(hti);
		if (NULL == key) {break;}

		netloc_node_t *node = (netloc_node_t *)
			netloc_lookup_table_access(*switches, key);
		if (NETLOC_NODE_TYPE_SWITCH != node->node_type) {
			fprintf(stderr, "Error: Returned unexpected node: %s\n", 
				 netloc_pretty_print_node_t(node));
			return NETLOC_ERROR;
		}

		// Get the Switch Name
		const char *src_name;
		ret = get_switch_name(topology, map, node, &src_name);
		if (NETLOC_SUCCESS != ret) {
			if (verbose) {
				fprintf(stderr,
"Did not find data for any nodes attached to switch %s\n",
					 netloc_pretty_print_node_t(node));
			}
			continue;
		}

		// Loop through and parse all of the edges for a switch
		loop_through_edges(topology, map, node, src_name, f_temp);
	}

	// Cleanup
	fclose(f_temp); 
	netloc_dt_lookup_table_iterator_t_destruct(hti);
	return NETLOC_SUCCESS;
}


// Loop through and parse all of the edges for a switch
static int
loop_through_edges(netloc_topology_t *topology, netloc_map_t *map, 
		     netloc_node_t *node, const char *src_name, FILE *f_temp)
{
	int ret, i, num_edges, nodes_cnt = 0, switches_cnt = 0;
	netloc_edge_t **edges = NULL;
	size_t slen = 4096;
   	char *switch_str = malloc(sizeof(char) * slen);
	char *node_str = malloc(sizeof(char) * slen);

	strcpy(switch_str, "");
	strcpy(node_str, "");

	// Get all of the edges
	ret = netloc_get_all_edges(*topology, node, &num_edges, &edges);
	if (NETLOC_SUCCESS != ret) {
		fprintf(stderr,
			 "Error: get_all_edges_by_id returned %d for"
			 " node %s\n", ret, node->description);
		return ret;
	}

	(verbose) ? printf("\nFound Switch: %s - %s which has %d edges \n",
			     src_name, node->physical_id, num_edges) : 0;

	// Loop through all of the edges
	for (i = 0; i < num_edges; i++) {
		(verbose) ? printf("\tEdge %2d - Speed: %s, Width: %s - " , i, 
				     edges[i]->speed, edges[i]->width) : 0;

		if (NETLOC_NODE_TYPE_SWITCH == edges[i]->dest_node->node_type) {
			// get the dest_node name
			const char *dst_name;
			ret = get_switch_name(
				topology, map, edges[i]->dest_node, &dst_name);
			if (NETLOC_SUCCESS != ret) {
				if (verbose) {
					fprintf(stderr,
"Did not find data for any nodes attached to switch %s\n",
						 netloc_pretty_print_node_t(node));
				}
				continue;
			}

			// Add name and link_speed to switch_str
			ret = add_switch_connection(edges, i, num_edges, src_name,
							dst_name, switch_str);
			if (NETLOC_SUCCESS == ret) {switches_cnt++;}
		} else if (NETLOC_NODE_TYPE_HOST == edges[i]->dest_node->node_type) {
			// if edge goes to a node, add name to node_str and put in a group
			ret = add_node_connection(topology, map, edges[i], node_str);
			if (NETLOC_SUCCESS == ret) {nodes_cnt++;}
		} else {
			fprintf(stderr,
				 "Error: Returned unexpected node: %s\n", 
				 netloc_pretty_print_node_t(edges[i]->dest_node));
			return NETLOC_ERROR;
		}
	}

	// update maximum totals needed later
	max_switches = MAX(switches_cnt, max_switches);
	max_nodes = MAX(max_nodes, nodes_cnt);
	
	// Erase any trailing commas
	assert(0 < strlen(switch_str) && slen > strlen(switch_str));
	assert(0 < strlen(node_str) && slen > strlen(node_str));
	switch_str[strlen(switch_str) - 1] = '\0';
	node_str[strlen(node_str) - 1] = '\0';

	// combine strings together and output to tolopogy file 
	fprintf(f_temp, "SwitchName=%s Switches=%s Nodes=%s\n",
		 src_name, switch_str, node_str);			
	
	free(switch_str);
	free(node_str);
	return NETLOC_SUCCESS;
}


// Add a switch connection and its link speed to the switch list
static int
add_switch_connection(netloc_edge_t **edges, int idx, int num_edges,
			 const char *src_name, const char *dst_name, char *switch_str)
{	
	netloc_node_t* dn = edges[idx]->dest_node;
	char * pch = strstr(switch_str, dst_name);
	int i, total_link_speed = 0;
	unsigned long current_ID = dn->physical_id_int;

	// Print out node information
	(verbose) ? printf("Dst:%9s - (%s - %s) [%20s][%18lu]/[%7s] - (%d edges)\n",
			     dst_name, netloc_decode_network_type(dn->network_type),
			     netloc_decode_node_type(dn->node_type), dn->physical_id, 
			     dn->physical_id_int, dn->logical_id, dn->num_edges) : 0;

	// Check to see if this switch is already on the switch connection list	
	if (pch != NULL) {return NETLOC_ERROR;}

	// Total up the link speed for all the connections between the two switches
	for (i = idx; i < num_edges; i++) {
		// If the IDs match then the connections go to the same switch 
		if (edges[i]->dest_node->physical_id_int == current_ID) {
			int link_speed = calculate_link_speed(edges[i]);

			if (0 >= link_speed) {
				fprintf(stderr,
					 "\nError: invalid connection width %s or "
					 "speed %s between %s and %s\n",
					 edges[idx]->width,
					 edges[idx]->speed, src_name, dst_name);
				return NETLOC_ERROR;
			}

			total_link_speed += link_speed;
		}
	}

	// Put the switch and its link_speed on the switch string		
	sprintf(switch_str, "%s%s-%d,", switch_str, dst_name, total_link_speed);
	return NETLOC_SUCCESS;
}


// calculate the link speed for an edge between two switches
static int calculate_link_speed(netloc_edge_t *edge)
{	
	// calculate the link speed between the two switches
	int link_speed = atoi(edge->width);
	if (link_speed < 1 || (link_speed > 24 ) ){
		return -1;
	}
	if ( strcasecmp(edge->speed, "SDR" ) == 0 )
		link_speed *= 2;
	else if ( strcasecmp(edge->speed, "DDR" ) == 0 )
		link_speed *= 4;
	else if ( strcasecmp(edge->speed, "QDR" ) == 0 )
		link_speed *= 8;
	else if ( strcasecmp(edge->speed, "FDR-10" ) == 0 )
		link_speed *= 10;
	else if ( strcasecmp(edge->speed, "FDR" ) == 0 )
		link_speed *= 14;
	else if ( strcasecmp(edge->speed, "EDR" ) == 0 )
		link_speed *= 25;
	else if ( strcasecmp(edge->speed, "HDR" ) == 0 )
		link_speed *= 50;
	else{
		return -1;
	}
	return link_speed;
}


// Add a node connection to the node list
static int
add_node_connection(netloc_topology_t *topology, netloc_map_t *map, 
		      netloc_edge_t *edge, char *node_str)
{
	int ret;
	hwloc_topology_t dst_hw_topo;
	const char *dst_name;

	ret = get_node_name_and_topology(topology, map, edge->dest_node, 
					     &dst_name, &dst_hw_topo);
	if (NETLOC_SUCCESS != ret) {return NETLOC_ERROR;}

	(verbose) ? printf( "Dst:%9s - ", dst_name) : 0;

	sprintf(node_str, "%s%s,",node_str, dst_name);
	
	// get and calculate needed node information
	hwloc_obj_t hw_obj = hwloc_get_root_obj(dst_hw_topo);
	int cpus = hwloc_get_nbobjs_by_type(dst_hw_topo, HWLOC_OBJ_PU);
	int sockets = hwloc_get_nbobjs_by_type(dst_hw_topo, HWLOC_OBJ_SOCKET);
	int cores = hwloc_get_nbobjs_by_type(dst_hw_topo, HWLOC_OBJ_CORE);
	int cores_per_socket = cores / sockets;
	int threads_per_core = cpus / cores;
	int memory = hw_obj->memory.total_memory/1024/1024;
	
	// Find a node group that matches the specifications given
	ret = find_node_group(cpus, cores_per_socket, threads_per_core,
				 memory, dst_name);
	
	// if couldn't find a matching node group, create a new one
	if (ret == node_group_cnt) {
		// Make a new node group in the table and fill in information
		make_new_node_group(cpus, cores_per_socket, threads_per_core,
				      memory, dst_name);
	}

	netloc_node_t* dn = edge->dest_node;
	( verbose ) ? printf("(%s - %s) [%20s][%18lu]/[%7s] - (%d edges)\n",
		netloc_decode_network_type(dn->network_type),
			netloc_decode_node_type(dn->node_type), dn->physical_id, 
				dn->physical_id_int, dn->logical_id, dn->num_edges) : 0;

	return NETLOC_SUCCESS;
}


// Find a node group that matches the specifications given
static int find_node_group( int cpus, int cores_per_socket, 
					 int threads_per_core, int memory, const char *dst_name)
{
	int j;
	for ( j=0; j < node_group_cnt; j++){
		// Check to make sure all of the numbers are the same
		if ((node_group_table[j].cpus == cpus) && 
			(node_group_table[j].memory == memory) && 
			(node_group_table[j].cores_per_socket == cores_per_socket) && 
			(node_group_table[j].threads_per_core == threads_per_core)){
			// Make node_name string bigger if there isn't enough space 
			if ((strlen(node_group_table[j].node_name) + strlen(dst_name) + 3)
								>= node_group_table[j].node_name_len ){
				node_group_table[j].node_name_len *= 2;
				char *temp_node_name = 
						(char *) realloc( node_group_table[j].node_name, 
							sizeof(char) * node_group_table[j].node_name_len);
				if (temp_node_name == NULL) {
					printf("Error (re)allocating memory - node_name string\n");
					exit(-1);
				}
				node_group_table[j].node_name = temp_node_name;		
			}
			sprintf(node_group_table[j].node_name, "%s,%s",
					node_group_table[j].node_name, dst_name);
			return j;
		}
	}
	return j;
}


// Make a new node group in the table and fill in information
static void make_new_node_group( int cpus, int cores_per_socket, 
					 int threads_per_core, int memory, const char *dst_name)
{
	node_group_table[node_group_cnt].node_name = malloc( sizeof(char) * 2048);
	node_group_table[node_group_cnt].node_name_len = 2048;
	strcpy(node_group_table[node_group_cnt].node_name, dst_name);
	node_group_table[node_group_cnt].cpus = cpus;
	node_group_table[node_group_cnt].memory = memory;
	node_group_table[node_group_cnt].cores_per_socket = cores_per_socket;
	node_group_table[node_group_cnt].threads_per_core = threads_per_core;
	node_group_cnt++;
	// if there aren't any more empty groups, make new ones
	if ( node_group_cnt >= node_groups_max){
		node_groups_max *= 2;
		node_group *temp_node_group = realloc(node_group_table, 
									sizeof(node_group) * node_groups_max);
		if ( temp_node_group == NULL){
			printf("Error (re)allocating memory for more node groups");
			exit(-1);
		}
		node_group_table = temp_node_group;
	}
}


// Save Topology data of network to topology.conf file
int save_topology_data_to_file()
{
	int j;
	// open up files to save data to topology.conf
	FILE *f = fopen(file_location, "w");
	FILE *f_temp = fopen(file_location_temp, "r");
	if ( (f == NULL) || (f_temp == NULL) ){
		printf("Error opening file!\n");
		exit(1);
	}
	
	// print hypercube topology configuration information for reference
	fprintf(f,"#############################################################"
		"#####\n# Slurm's network topology configuration file for use with the"
		" topology/hypercube plugin\n#########################################"
		"#########################\n# Hypcube topology information:\n# Maximum "
		"Number of Dimensions: %d \n# Maximum Number of Nodes per Switch: %d\n"
		"\n##################################################################\n"
		,max_switches, max_nodes); 

	/*
	 * Print out compute nodes info and partitions nodes list for slurm.conf
	 * in case the user wants to use this tool to fill in their node list for
	 * that config file.
	 */
	fprintf(f, "# Compute Nodes information for slurm.conf:\n");
	for ( j=0; j < node_group_cnt; j++){
		fprintf(f,"# NodeName=%s CPUs=%d RealMemory=%d CoresPerSocket=%d " 
			"ThreadsPerCore=%d State=UNKNOWN\n", node_group_table[j].node_name,
				node_group_table[j].cpus, node_group_table[j].memory,
					node_group_table[j].cores_per_socket, 
						node_group_table[j].threads_per_core);
	}
	fprintf(f,"\n###########################################################"
			"#######\n# Partition nodes list for slurm.conf: \n" "# Nodes=" );
	for ( j=0; j < node_group_cnt-1; j++){
		fprintf(f, "%s,", node_group_table[j].node_name );
	}
	fprintf(f, "%s \n", node_group_table[j].node_name );
	// copy switch information from temp file to topology.conf
	fprintf(f,	"\n#########################################################"
				"#########\n# Switch Hypercube Topology Information: \n");
	char ch;
	while ( ( ch = fgetc(f_temp) ) != EOF )
		fputc(ch, f);
	
	// Cleanup 
	fclose(f);
	fclose(f_temp);
	remove(file_location_temp);
	return NETLOC_SUCCESS;
}


// Gets the name and the hw_loc topology for a NetLoc node
static int
get_node_name_and_topology(
	netloc_topology_t *topology, netloc_map_t *map, 
	netloc_node_t *node, const char **name, hwloc_topology_t *hw_topo)
{
	netloc_map_port_t port = NULL;
	hwloc_obj_t hw_obj = NULL;
	netloc_map_server_t server = NULL;
	int ret;

	ret = netloc_map_netloc2port(*map, *topology, node, NULL, &port);
	if( NETLOC_SUCCESS != ret ) {
		if (verbose) {
			printf( "\n    Error: netloc_map_netloc2port could not find"
				 " port info for %s\n", netloc_pretty_print_node_t(node) );
		}
		return ret;
	}
	ret = netloc_map_port2hwloc(port, hw_topo, &hw_obj);
	if( NETLOC_SUCCESS != ret ) {
		fprintf(stderr, "Error: netloc_map_port2hwloc returned an error");
		return ret;
	}
	ret = netloc_map_hwloc2server(*map, *hw_topo, &server);
	if( NETLOC_SUCCESS != ret ) {
		fprintf(stderr, "Error: netloc_map_hwloc2server returned an error");
		return ret;
	}
	ret = netloc_map_server2name(server, name);
	if( NETLOC_SUCCESS != ret ) {
		fprintf(stderr, "Error: netloc_map_server2name returned an error");
		return ret;
	}

	return NETLOC_SUCCESS;
}


// Gets the name of a switch in the network
static int get_switch_name(netloc_topology_t *topology, netloc_map_t *map, 
			      netloc_node_t *node, const char **name)
{
	// Find a switch_name that matches the Physical ID given
	int ret = find_switch_name(node);
	
	// If there already a switch_name assigned to the physical ID
	if  (ret != switch_name_cnt) {
		*name = switch_name_table[ret]->sw_name; 
	}
	// Else if couldn't find a matching switch_name create a new one
	else{
		// Make a switch_name entry in the table and fill in information
		ret = make_new_switch_name(topology, map, node, name);
		if (NETLOC_SUCCESS != ret) {return ret;}

		switch_name *sw_name_entry = malloc(sizeof(switch_name));
		sw_name_entry->sw_name = *name;
		sw_name_entry->physical_id = node->physical_id_int;
		switch_name_table[switch_name_cnt] = sw_name_entry;
		switch_name_cnt++;
		
		// If no more room for more switch_names, then make more space
		if (switch_name_cnt == switch_name_max) {
			switch_name_max *= 2;
			switch_name **temp_switch_name_table = realloc(
				switch_name_table, 
				sizeof(switch_name) * switch_name_max);
			if (temp_switch_name_table == NULL){
				printf("Error (re)allocating memory for more switch_names");
				exit(-1);
			}
			switch_name_table = temp_switch_name_table;
		}
	}
	return NETLOC_SUCCESS;
}


// Find a switch_name that matches the Physical ID given
static int find_switch_name( netloc_node_t *node )
{
	int j;
	for ( j=0; j < switch_name_cnt; j++){
		// Check to see if the numbers are the same
		if ( switch_name_table[j]->physical_id == node->physical_id_int ) {
			return j;
		}
	}
	return j;
}


// Compares switch_name with all of the names in the table
static int check_unique_switch_name( char *sw_name)
{
	int j;
	for ( j=0; j < switch_name_cnt; j++){
		// Check to see if the names are the same
		if ( strcmp( switch_name_table[j]->sw_name, sw_name ) == 0 ) {
			break;
		}
	}
	// if the name already exists return 0, else return 1
	if ( j < switch_name_cnt )
		return NETLOC_ERROR;
	else 
		return NETLOC_SUCCESS;
}


// Make a new switch_name entry in the table and fill in information
static int
make_new_switch_name(netloc_topology_t *topology, netloc_map_t *map, 
			netloc_node_t *node, const char **name )
{	
	int ret, i, num_edges;
	netloc_edge_t **edges = NULL;
	const char *node_name;

	//Get all of the edges
	ret = netloc_get_all_edges(*topology, node, &num_edges, &edges);
	if (NETLOC_SUCCESS != ret) {
		fprintf(stderr,
			 "Error: netloc_get_all_edges returned %d for"
			 " node %s\n", ret, netloc_pretty_print_node_t(node));
		return ret;
	}

	// get the node name of the first host connected to the switch
	for (i = 0; i < num_edges; i++) {
		if (NETLOC_NODE_TYPE_HOST == edges[i]->dest_node->node_type) {
			hwloc_topology_t dst_hw_topo;

			ret = get_node_name_and_topology(
				topology, map, 
				edges[i]->dest_node, &node_name, &dst_hw_topo);
			if (NETLOC_SUCCESS == ret) {break;}
		}	
	}

	/*
	 * If we couldn't find hwloc data for any host attached to the switch,
	 * let's issue a warning but otherwise assume that the switch won't be
	 * used
	 */
	if (num_edges == i) {
		if (verbose) {
			fprintf(stderr,
				 "Skipping switch because no data was available for attached nodes:\n"
				 "\t%s\n",
				 netloc_pretty_print_node_t(node));
		}
		return NETLOC_ERROR_EMPTY;
	}

	// Use the node name to create the switch name
	char * temp_node_name = strdup(node_name);
	char * temp_name = strtok (temp_node_name,"n");
	char * sw_name;
	int switch_cnt = 0;
	asprintf( &sw_name, "%ss%d", temp_name, switch_cnt);

	// Check to see if the switch name is unique, change it if it isn't
	while (check_unique_switch_name(sw_name) == NETLOC_ERROR) {
		free(sw_name);
		switch_cnt++;
		asprintf( &sw_name, "%ss%d", temp_name, switch_cnt);
	}

	free(temp_node_name);
	*name = sw_name;
	return NETLOC_SUCCESS;
}
