/*****************************************************************************\
 *  graph_structs.h
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _GRAPH_STRUCTS_H_
#define _GRAPH_STRUCTS_H_

/* for use of lists */
#include "src/common/list.h"
/* for bool */
#include "src/common/macros.h"
/* malloc and free, but safer */
#include "src/common/xmalloc.h"

#ifndef NO_VAL
#define NO_VAL -2
#endif
#define NUM_PORTS_PER_NODE 6
#define NUM_ENDPOINTS_PER_CONNECTION 2
#define NUM_CONNECTIONS_PER_PORT 2
#define INTERNAL_CONNECTIONS_PER_NODE NUM_PORTS_PER_NODE/2

// #ifdef SYSTEM_DIMENSIONS
// #undef SYSTEM_DIMENSIONS
#define SYSTEM_DIMENSIONS 1
// #endif

/* a connection could be an internal or external wire */
typedef enum placement {INTERNAL, EXTERNAL} placement_t;
/* a port could be of type PLUS or MINUS */
enum type_t {PLUS, MINUS};
/* dimensionality */
typedef enum dimension_type {X, Y, Z} dimension_t;

/* */
typedef enum conn_type {MESH, TORUS} conn_type_t;

 typedef int Label;
// typedef int dimension_t;

bool __initialized;
/** 
 * 
 * A system is composed of a set of nodes connected by a set of
 * external connections.
 * 
 */
typedef struct system
{
	List node_list;
	List switch_list;
	List connection_list;
	List partition_list;
} system_t;
system_t* global_sys;

/** 
 * A connection can either be internal or external to a node and has
 * two endpoints and a label.  The label informs what partition it is
 * a part of.
 */
typedef struct connection 
{
	Label original_id;
	Label id;
	int place;		/* internal or external, see enum placement */
	struct port* ep0;
	struct port* ep1;
	struct partition* partition; /** a link to the corresponding partition */
	struct node* node;	     /* pointer to the node we belong to, if INTERNAL */
} connection_t;

/** 
 * A port is the junction for connections that allows one connection
 * to connect to another.
 * 
 * if the port is connected to the base partition compute nodes,
 * meaning ports 0,1, then the external connections will be special
 * ones such that the other ends (of the connection) are NULL && and
 * the label of that connection is set to BPXXX, where XXX = the
 * coordinates of the partition.
 */
typedef struct port
{
	struct node* node;
	int id;
	int type;
	connection_t* conn_int;
	connection_t* conn_ext;
} port_t;

/** 
 * a "node" both a BGL base partition (BP) or a switch, depending on
 * whether is_node is set.
 * 
 * 
 * NODE schematic: currently hard wired for 3 dims
 * 
 * /---|---|---\
 * |   5   4   |
 * |   \---/   |
 * |        /-3-
 * |        |  |
 * |        \-2-
 * |   /---\   |
 * |   0   1   |
 * \---|---|---/
 * 
 * SWITCH schematic
 * 
 *       0  1
 *    /--|--|--\
 *    |        |
 *  5 -        - 2
 *    |        |
 *    \__|__|__/
 *       4  3
 
 */
typedef struct node
{
	/* unique id for this node */
	Label id;
	/** 
	 * list of internal connections belonging to this node
	 */
	List connection_list;

	/** 
	 * list of ports of this node
	 */
	// List port_list;
	port_t ports[NUM_PORTS_PER_NODE];

	/** true if this is node, else this represents a switch */
	bool is_node;

	/** partition that this node belongs to */
	struct partition* partition;

	/* if this node is !is_node => switch_t, then dimension applies*/
	dimension_t dim;
	
} node_t;
typedef node_t switch_t;

/** 
 * structure that holds the configuration settings for each switch
 * 
 * - dimension
 * - from node, to node
 * - from port, to port
 * 
 */
typedef struct switch_config
{
	/* might get used...*/
	Label id;

	/* dimension */
	dimension_t dim;

	/* node labels */
	Label node_src; // source
	Label node_tar; // target

	/* ports */ 
	int port_src;
	int port_tar;
	
} switch_config_t;

/** 
 * 
 */
typedef struct partition
{
	/* */
	Label id;
	/* for debugging */
	int num_connections;
	/* */
	List node_list; 
	/* */
	List connection_list; 
	/* need to figure out how to fill in */
	int dimensions[SYSTEM_DIMENSIONS]; 
	/* true if no non-toridal connections included in partition */
	// bool is_torus;
	conn_type_t conn_type;

} partition_t;

/** creator/destructor fxns */

/** */
// int new_node(node_t** node, char* id);
int new_node(node_t** node, Label id);
/** */
int new_switch(switch_t** node, node_t* master, Label id,
	       dimension_t dim);
/** */
switch_t* get_switch(Label node_id, dimension_t dim);
/** */
void delete_node(void* object);
/** */
void print_node(node_t* node);
/** returns true if this is a "node" connection */
bool is_node_connection(connection_t* conn);
/** 
 * compares memory address of the nodes
 */
int listfindf_node(node_t* A, node_t *B);
int node_not_in_list(List node_list, node_t *node);

/** */
int new_connection(connection_t** connection);
/** */
void delete_connection(void* object);
/** */
void print_connection(connection_t* conn);

/** */
int new_port(port_t** port, int id);
/** */
void delete_port(void* object);

/** */
int new_partition(partition_t** partition, Label id);
/** */
int add_node_to_partition(partition_t* part, node_t* node);
/** */
int add_connection_to_partition(connection_t* connection, partition_t* partition);
/** */
void copy_partition(partition_t* old_part, partition_t** new_part);
/** */
int merge_partitions(partition_t* A, partition_t* B, List partition_list);
/** */
void delete_partition(void* object);
/** */
void print_partition(partition_t* partition);
/** */
partition_t* find_partition(List l, Label id);
/** */
int partition_size(partition_t* partition);
/** 
 * compares labels and tries to find the correct partition
 */
int listfindf_partition(partition_t* part, Label *id);

/** */
int new_system();
/** */
void delete_system();

/** */
char* convert_conn_type(conn_type_t conn_type);

char* convert_place(int place);
/** remove the given partition address from the list of partitions */
void remove_partition(List part_list, partition_t* part);

/** free a generic object (ie, char*, int*, config_t*) */
void delete_gen(void* object);

char* convert_dim(dimension_t dim);

/**  */
void new_switch_config(switch_config_t** config, Label id, dimension_t dim, 
	       Label node_src, int port_src, 
	       Label node_tar, int port_tar);
/** */
void delete_switch_config(void* object);
/** */
void print_switch_config(switch_config_t* config);

#endif /* _GRAPH_STRUCTS_H_ */
