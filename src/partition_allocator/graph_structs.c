/*****************************************************************************\
 *  graph_structs.c
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

#include <unistd.h>
#include <stdlib.h>
#include "graph_structs.h"

// #define DEBUG
/** 
 * user must call delete_system when finished
 */
int new_system() {
	if (!__initialized){
		global_sys = (system_t*) xmalloc(sizeof(system_t));
		if (!global_sys){
			printf("init_sys: error, not enough memory for the system\n");
			exit(1);
		}
		global_sys->node_list = list_create(delete_node);
		global_sys->switch_list = list_create(delete_node);
		global_sys->connection_list = list_create(delete_connection);
		global_sys->partition_list = list_create(delete_partition);
		
		__initialized = true;
		return 0;
	} 
	return 1;
}

/** 
 * create a node and initialize it's internal structures 
 * 
 * each node has 2 special connections that are only connected on one
 * end to the external endpoint of a port.  these special connections
 * 1) already have their labels = id of the node
 * 2) are already assigned to partitions.
 * 
 */
int new_node(node_t** node, Label id)
{
	connection_t* conn;
	partition_t* part;
	int i;

	if (!__initialized){
		new_system();
	}

	(*node) = (node_t*) xmalloc(sizeof(node_t));
	if (!(*node)){
		printf("error, not enough mem for more nodes\n");
		exit(1);
	}
  
	(*node)->id = id;
	(*node)->is_node = true;
	(*node)->dim = NO_VAL;
	for (i=0; i<NUM_PORTS_PER_NODE; i++){
		(*node)->ports[i].node = *node;
		(*node)->ports[i].id = i;
		(*node)->ports[i].conn_int = NULL;
		(*node)->ports[i].conn_ext = NULL;
	}

	/* intialize some port characteristics */
	(*node)->ports[0].type = (*node)->ports[3].type = (*node)->ports[5].type = PLUS;
	(*node)->ports[1].type = (*node)->ports[2].type = (*node)->ports[4].type = MINUS;

	/** 
	 * create the partition that will include the new node/connection connections.
	 */
	new_partition(&part, id);
	
	/** create the special connections that represent inclusion of
	 * the base partitions (nodes) in partitions */
	(*node)->connection_list = list_create(NULL);

	/* CONNECTION for X dimension */
	new_connection(&conn);
	conn->original_id = id;
	conn->id = id;
	conn->place = INTERNAL;
	conn->node = *node;
	/* connect the endoints of the conn to the node ports */
	conn->ep0 = &((*node)->ports[0]);
	conn->ep1 = &((*node)->ports[1]);
	/* connect the node ports to the connection */
	(*node)->ports[0].conn_int = conn;
	(*node)->ports[1].conn_int = conn;
	list_append(global_sys->connection_list, conn);
	list_append((*node)->connection_list, conn);
	add_connection_to_partition(conn, part);

	if (SYSTEM_DIMENSIONS > 1){
		/* CONNECTION for Y dimension */
		new_connection(&conn);
		conn->original_id = id;
		conn->id = id;
		conn->place = INTERNAL;
		conn->node = *node;
		/* connect the endoints of the conn to the node ports */
		conn->ep0 = &((*node)->ports[2]);
		conn->ep1 = &((*node)->ports[3]);
		/* connect the node ports to the connection */
		(*node)->ports[2].conn_int = conn;
		(*node)->ports[3].conn_int = conn;
		list_append(global_sys->connection_list, conn);
		list_append((*node)->connection_list, conn);
		add_connection_to_partition(conn, part);
	}

	if (SYSTEM_DIMENSIONS > 2){
		/* CONNECTION for Z dimension */
		new_connection(&conn);
		conn->original_id = id;
		conn->id = id;
		conn->place = INTERNAL;
		conn->node = *node;
		/* connect the endoints of the conn to the node ports */
		conn->ep0 = &((*node)->ports[4]);
		conn->ep1 = &((*node)->ports[5]);
		/* connect the node ports to the connection */
		(*node)->ports[4].conn_int = conn;
		(*node)->ports[5].conn_int = conn;
		list_append(global_sys->connection_list, conn);
		add_connection_to_partition(conn, part);
		list_append((*node)->connection_list, conn);
	}		

	/** add the partition to the global list of partitions */
	list_append(global_sys->partition_list, part);
	/** add the node to the partition, and vice versa*/
	(*node)->partition = part;
	add_node_to_partition(part, *node);
	list_append(global_sys->node_list, *node);

	return 0;
}

/** 
 * creates another type of node_t, this time a switch, and connects it
 * to the master node.
 */
int new_switch(switch_t** my_switch, node_t* master, Label id, 
	       dimension_t dim)
{
	connection_t* conn;
	int i, port_numA, port_numB;

	if (!__initialized){
		new_system();
	}

	(*my_switch) = (switch_t*) xmalloc(sizeof(switch_t));
	if (!(*my_switch)){
		printf("error, not enough mem for more switches\n");
		exit(1);
	}
  
	/* initialize the switch innards */
	(*my_switch)->id = id;
	(*my_switch)->is_node = false;
	(*my_switch)->dim = dim;
	for (i=0; i<NUM_PORTS_PER_NODE; i++){
		(*my_switch)->ports[i].node = *my_switch;
		(*my_switch)->ports[i].id = i;
		(*my_switch)->ports[i].conn_int = NULL;
		(*my_switch)->ports[i].conn_ext = NULL;
	}


	/* internal connections for the switch, we make 3 connections
	 * for each switch. 
	 */
	/* add internal connetions to actual switches */
	(*my_switch)->connection_list = list_create(NULL);
	for (i=0; i<INTERNAL_CONNECTIONS_PER_NODE; i++){
		new_connection(&conn);
		conn->place = INTERNAL;
		conn->node = *my_switch;
		/* give the connection to the node */
		list_push((*my_switch)->connection_list, conn); 
		/* also give a pointer to the system */
		list_push(global_sys->connection_list, conn);  
	}
	
	/* create the set of external connections that connect the
	 * switch to the node
	 */

	/*
	 *                      connecting a switch to a node
	 *  ------------|                                             |-----------
	 *    (switch)  |                                             |  (node)
	 *              |                (connection_t)               |
	 * [conn_int]--port-[conn_ext] ==[ep0]---[ep1]== [conn_ext]-port--[conn_int]
	 *              |                                             |
	 * [conn_int]--port-[conn_ext] ==[ep0]---[ep1]== [conn_ext]-port--[conn_int]
	 *              |                                             |
	 *              |                                             |
	 *  ------------|                                             |-----------
	 */
	
	/* FIXME: find some elegant way of setting this */
	/* these are the ports for the nodes, switches always use port 0-1
	 * to connect to the nodes
	 */
	if (dim == X){
		port_numA = 0;
		port_numB = 1;
	} else if (dim == Y) {
		port_numA = 2;
		port_numB = 3;
	} else {/* dim == Z */
		port_numA = 4;
		port_numB = 5;
	}

	/* first of the external connections from switch to node */
	new_connection(&conn);
	conn->original_id = id;
	conn->id = id;
	conn->place = EXTERNAL;
	/* connect the endoints of the conn to the node/switch ports */
	conn->ep0 = &((*my_switch)->ports[0]);
	conn->ep1 = &(master->ports[port_numA]);
	/* connect the node/switch ports to the connection */
	(*my_switch)->ports[0].conn_ext = conn;
	master->ports[port_numA].conn_ext = conn;
	list_append(global_sys->connection_list, conn);
	add_connection_to_partition(conn, master->partition);

	/* second of the external connections from switch to node */
	new_connection(&conn);
	conn->original_id = id;
	conn->id = id;
	conn->place = EXTERNAL;
	/* connect the endoints of the conn to the node/switch ports */
	conn->ep0 = &((*my_switch)->ports[1]);
	conn->ep1 = &(master->ports[port_numB]);
	/* connect the node/switch ports to the connection */
	(*my_switch)->ports[1].conn_ext = conn;
	master->ports[port_numB].conn_ext = conn;
	list_append(global_sys->connection_list, conn);
	add_connection_to_partition(conn, master->partition);

	list_append(global_sys->switch_list, *my_switch);

	return 0;
}

/** 
 * here I could've used the listfindf thing with the list functions
 * (to find the switch).
 */
switch_t* get_switch(Label node_id, dimension_t dim)
{
	ListIterator itr;
	switch_t* my_switch = NULL;;
	if (!global_sys->switch_list){
		printf("get_switch: Error, node_list uninitialized\n");
		return NULL;
	}

	itr = list_iterator_create(global_sys->switch_list);
	while((my_switch = (switch_t*) list_next(itr))){
		if (!my_switch->is_node &&
		    my_switch->id == node_id &&
		    my_switch->dim == dim){
			break;
		}
	}
	list_iterator_destroy(itr);
#ifdef DEBUG
#endif
	if (my_switch == NULL){
		printf("get_switch: Error, no switch with node id <%d> and dim %d\n", node_id, dim);
	}

	return my_switch;
}

/** */
void print_node(node_t* node)
{
	connection_t* conn;
	ListIterator itr = list_iterator_create(node->connection_list);

	if (node->is_node) {
		printf("node_t id:\t%d\n", node->id);
	} else {
		printf("switch_t id:\t%d\n", node->id);
		printf("        dim:\t%s\n", convert_dim(node->dim));
	}
	if (node->partition)
		printf("    part id:\t%d\n", node->partition->id);
	else
		printf("    part id:\tNULL\n");
	if (list_count(node->connection_list) != 0)
		printf("  connections:\n");
	else
		printf("  no connections!\n");
	while((conn = (connection_t*) list_next(itr))){
		print_connection(conn);
	}
	list_iterator_destroy(itr);
}

char* convert_dim(dimension_t dim)
{
	switch(dim){
	case X: return "X";
	case Y: return "Y";
	case Z: return "Z";
	default: return "unknown";
	}
}

/** 
 * delete a node and cleanly remove it's internal structures
 */
void delete_node(void* object)
{
	node_t* node = (node_t*) object;
	list_destroy(node->connection_list);
	xfree (node);
}

/** 
 * create a connection and initialize it's internal structures 
 */
int new_connection(connection_t** connection)
{
	(*connection) = (connection_t*) xmalloc(sizeof(connection_t));
	if (!(*connection)){
		printf("error, not enough mem for more connections\n");
		exit(1);
	}
  
	(*connection)->original_id = NO_VAL;
	(*connection)->id = NO_VAL;
	(*connection)->place = EXTERNAL;
	(*connection)->ep0 = NULL;
	(*connection)->ep1 = NULL;
	(*connection)->partition = NULL;
	(*connection)->node = NULL;

	return 0;
}

/** 
 * delete a connection
 */
void delete_connection(void* object)
{
	connection_t* connection = (connection_t*) object;
	xfree(connection);
}

/** */
void print_connection(connection_t* conn)
{
	if (!conn){
		printf("print_connection error, connection given is NULL\n");
		return;
	}
		
	printf("connection_t old label:\t%d\n", conn->original_id);
	printf("  connection_t label:\t%d\n", conn->id);
	printf("  connection_t place:\t%s\n", convert_place(conn->place));
	if (conn->place == INTERNAL && conn->node != NULL)
		printf("  connection_t has node:\tTRUE\n");
	else
		printf("  connection_t has node:\tFALSE\n");

	if (conn->place == INTERNAL){
		/* print out what's connected to the first endpoint */
		if (!conn->ep0)
			printf("  ep0 is NULL\n");
		else {
			if (!conn->node){
				printf("print_connection error, internal connection has NULL ref to node\n");
			}

			if (conn->node->is_node){
				printf("  ep0 is connected to node ");
			} else {
				printf("  ep0 is connected to switch(%s) ", 
				       convert_dim(conn->node->dim));
			}
			if (conn->ep0->conn_ext) {
				printf("%d port %d ext conn w/ label <%d>\n",
				       conn->node->id, 
				       conn->ep0->id, conn->ep0->conn_ext->id);
			} else {
				printf("%d port %d ext conn is NULL\n",
				       conn->node->id, conn->ep0->id);
			}
		}
		/* print out what's connected to the second endpoint */
		if (!conn->ep1)
			printf("  ep1 is NULL\n");
		else {
			if (!conn->node){
				printf("print_connection error, internal connection has NULL ref to node\n");
			}

			if (conn->node->is_node){
				printf("  ep1 is connected to node ");
			} else {
				printf("  ep1 is connected to switch(%s) ",
				       convert_dim(conn->node->dim));
			}
			if (conn->ep1->conn_ext) {
				printf("%d port %d ext conn w/ label <%d>\n",
				       conn->node->id,
				       conn->ep1->id, conn->ep1->conn_ext->id); 

			} else { 
				printf("%d port %d ext conn is NULL\n",
				       conn->node->id, conn->ep1->id);
			}
		}
	} else {
		/* print out what's connected to the first endpoint */
		if (!conn->ep0)
			printf("  ep0 is NULL\n");
		else {
			if (!conn->ep0->node){
				printf("print_connection error, port has NULL ref to node\n");
			}

			if (conn->ep0->node->is_node){
				printf("  ep0 is connected to node ");
			} else {
				printf("  ep0 is connected to switch(%s) ", 
				       convert_dim(conn->ep0->node->dim));
			}
			if (conn->ep0->conn_int) {
				printf("%d port %d int conn w/ label <%d>\n",
				       conn->ep0->node->id, 
				       conn->ep0->id, conn->ep0->conn_int->id);
			} else {
				printf("%d port %d int conn is NULL\n",
				       conn->ep0->node->id, conn->ep0->id);
			}
		}
		/* print out what's connected to the second endpoint */
		if (!conn->ep1)
			printf("  ep1 is NULL\n");
		else {
			if (!conn->ep1->node){
				printf("print_connection error, port has NULL ref to node\n");
			}

			if (conn->ep1->node->is_node){
				printf("  ep1 is connected to node ");
			} else {
				printf("  ep1 is connected to switch(%s) ",
				       convert_dim(conn->ep0->node->dim));
			}
			if (conn->ep1->conn_int) {
				printf("%d port %d int conn w/ label <%d>\n",
				       conn->ep1->node->id,
				       conn->ep1->id, conn->ep1->conn_int->id); 

			} else { 
				printf("%d port %d int conn is NULL\n",
				       conn->ep1->node->id, conn->ep1->id);
			}
		}
	}

	/* print out connection partition */
	if (conn->partition){
		printf("  connection is a part of partition %d\n", conn->partition->id);
	} else {
		printf("  connection is a not a part of a partition\n");
	}
}


/** */
int new_port(port_t** port, int id)
{
	(*port) = (port_t*) xmalloc(sizeof(port_t));
	if (!(*port)){
		printf("error, not enough mem for more ports\n");
		exit(1);
	}
  
	(*port)->node = NULL;
	(*port)->id = id;
	(*port)->type = PLUS;
	(*port)->conn_int = NULL;
	(*port)->conn_ext = NULL;

	return 0;
}

/** */
void delete_port(void* object)
{
	port_t* port = (port_t*) object;
	xfree (port);
}

/** */
int new_partition(partition_t** partition, Label label)
{
	(*partition) = (partition_t*) xmalloc(sizeof(partition_t));
	if (!(*partition)){
		printf("error, not enough mem for more partitions\n");
		exit(1);
	}
  
#ifdef DEBUG
	printf("new_partition %d\n", label);
#endif

	(*partition)->id = label;
	(*partition)->num_connections = 0;
	(*partition)->node_list = list_create(NULL);
	(*partition)->connection_list = list_create(NULL);
	// (*partition)->is_torus = true;
	(*partition)->conn_type = TORUS;
	return 0;
}

/** add a node to the partition */
int add_node_to_partition(partition_t* part, node_t* node)
{
	if (!node || !part){
		printf("add_node_to_partition: error, given node or part is NULL\n");
		return 1;
	}
	if (!node->is_node){
		printf("add_node_to_partition: error, given structure is not a node\n");
		return 1;
	}

	/* first we see if the node is already a part of the partition */
	// if (list_find_first(part->node_list, (ListFindF) listfindf_node, node)){
	if (!node_not_in_list(part->node_list, node)){
#ifdef DEBUG
		printf("add_node_to_partition: node <%d> is already in list\n", node->id);
#endif		
		return 0;
	}
#ifdef DEBUG
	printf("add_node_to_partition: node <%d> added to list\n", node->id);
#endif		
	/* if not, then we go ahead and insert it */
	list_append(part->node_list, node);
	return 0;
}

/** */
int add_connection_to_partition(connection_t* conn, partition_t* part)
{
	if (!conn || !part){
		printf("add_connection_to_partition: Error, partition or connection NULL\n");
		return 1;
	}

	if (part->id != conn->id){
		printf("add_connection_to_partition: Error, partition and connection have different labels\n");
		return 1;
	}

	list_append(part->connection_list, conn);
	part->num_connections++;
	conn->partition = part;
	
	/* check if this connection violates the toroidal 
	 * property of the partition.
	 * 
	 * this partition is a torus if the number of
	 * non-toroidal connections == 0
	 */
	if (!is_node_connection(conn)){
		if (conn->place == INTERNAL){
			if (conn->ep0->conn_ext == NULL || 
			    conn->ep1->conn_ext == NULL) {
				part->conn_type = MESH;
			}
		} else {
			if (conn->ep0->conn_int == NULL || 
			    conn->ep1->conn_int == NULL) {
				part->conn_type = MESH;
			}
		}
	} 
	/* if this connection is part of a node (as opposed to switch)
	 * add the node to the partition node_list
	 */
	else {
		add_node_to_partition(part, conn->node);
	}

#ifdef DEBUG
	printf("add_connection_to_partition: added connection %d to partition %d\n", 
	       conn->id, part->id);
#endif

	return 0;
}

/**
 * copy the partition info and make a shallow copy (only copy the mem
 * addresses) of the connections
 */
void copy_partition(partition_t* old_part, partition_t** new_part)
{
	ListIterator itr;
	connection_t* conn;
	node_t* node;

	new_partition(new_part, old_part->id);
	(*new_part)->conn_type = old_part->conn_type;

	/* copy the references from the connection list over*/
	itr = list_iterator_create(old_part->connection_list);
	while((conn = (connection_t*) list_next(itr))){
		/* make sure that connection is pointing to new partition*/
		conn->partition = *new_part;
		list_append((*new_part)->connection_list, conn);
	}
	list_iterator_destroy(itr);

	/* copy the references from the node list over*/
	itr = list_iterator_create(old_part->node_list);
	while((node = (node_t*) list_next(itr))){
		list_append((*new_part)->node_list, node);
	}
	list_iterator_destroy(itr);
}

/** 
 * 
 * merge the two partitions, where the smaller one is merged into the
 * larger.  we merge from the smaller one because hopefully that will
 * take less time.  if partition labels are the same, they both get
 * merged into one.  if both partitions addresses are the same, then
 * they are the same partition and nothing happens.  if both partition
 * labels are different, then we merge the smallest into the largest.
 * 
 * also removes the smaller partition from the global partition list
 * 
 * 
 */
int merge_partitions(partition_t* A, partition_t* B, List partition_list)
{
	connection_t* conn;
	ListIterator itr;
	partition_t* smaller_part;
	partition_t* larger_part;
	int sizeA, sizeB;

	if (!A || !B){
		printf("_merge_partitions: error one of the partitions NULL\n");
		return 1;
	}

	/** if both addresses the same */
	if (A == B){
#ifdef DEBUG
		printf("_merge_partitions: both end partitions the same, no need to do anything\n");
#endif
		return 0;
	}

	sizeA = list_count(A->node_list);
	sizeB = list_count(B->node_list);
	if (sizeA < 1 || sizeB < 1){
		printf("_merge_partitions: error one of the partitions has no size\n");
		return 1;
	}

#ifdef DEBUG
	printf("graph_structs: merging partition %d to %d\n", A->id, B->id);
#endif
	/* sort out who's bigger */
	if (sizeA < sizeB){
		smaller_part = A;
		larger_part = B;
	} else {
		smaller_part = B;
		larger_part = A;
	}

	/* copy all the elements out from the smaller list to the
	 * larger one
	 */
	itr = list_iterator_create(smaller_part->connection_list);
	while ((conn = (connection_t*) list_next(itr))) {
		conn->id = larger_part->id;
		conn->partition = larger_part;
		add_connection_to_partition(conn, larger_part);
		list_remove(itr); 
	}
	list_iterator_destroy(itr);

	/** remove the old partition from the list */
	remove_partition(partition_list, smaller_part);
#ifdef DEBUG
	printf("graph_structs: merging partitions done, new size of %d is %d\n", 
	       larger_part->id, list_count(larger_part->node_list));
	

	printf("done merging, new global partition list size: %d\n", 
	       list_count(partition_list));
#endif
	return 0;
}

/**
 * remove the partition from the list.  this function removes the
 * partition with the same memory location as the one given.
 */

void remove_partition(List part_list, partition_t* rm_part)
{
	ListIterator itr;
	partition_t* next_part;
	int found = 0;
	itr = list_iterator_create(part_list);
	while ((next_part = (partition_t*) list_next(itr))) {
		/* if the addresses match */
		if (next_part == rm_part){
			/* simply remove it from the list */
			list_remove(itr);
			delete_partition(next_part);
			found = 1;
			break;
		}
	}	

#ifdef DEBUG
	if (found){
		printf("remove_partition: partition found and removed from list\n");
	} else {
		printf("remove_partition: partition NOT found in list\n");
	}
#endif
}

/** 
 * note that we do _not_ clean up the node_list and connection_list,
 * b/c those should get deleted when nodes and the system get
 * destroyed.
 */
void delete_partition(void* object)
{
	partition_t* part = (partition_t*) object;
	list_destroy(part->node_list);
	list_destroy(part->connection_list);
	xfree(part);
	part = NULL;
}

/** */
void print_partition(partition_t* part)
{
	ListIterator itr;
	connection_t* conn;
	if (!part){
		printf("print_partition error, partition is NULL\n");
	}
	
	printf("partition label:\t%d\n", part->id);
	printf("partition size :\t%d\n", list_count(part->node_list));
	printf("partition num conn:\t%d\n", part->num_connections);
	if (part->conn_type == TORUS)
		printf("partition conn type:\ttoroidal\n");
	else 
		printf("partition conn type:\tnon-toroidal\n");

	itr = list_iterator_create(part->connection_list);
	while((conn = (connection_t*) list_next(itr))){
		print_connection(conn);
	}
	list_iterator_destroy(itr);
}

/** */
int partition_size(partition_t* partition)
{
	if (!partition){
		printf("partition_size: Error, partition is NULL\n");
		return -1;
	}
	return list_count(partition->node_list);
}

/** */
partition_t* find_partition(List l, Label id)
{
	return (partition_t*) list_find_first(l, (ListFindF) listfindf_partition, &id);
}

/** 
 * compares labels and tries to find the correct partition
 */
int listfindf_partition(partition_t* part, Label *id)
{
	return (part->id == *id);
}

/** 
 *
 */
int listfindf_node(node_t* A, node_t *B)
{
	if (A->id == B->id){
		return 0;
	}
	return 1;
}


/** 
 *
 */
int node_not_in_list(List node_list, node_t *node)
{
	node_t *n;
	ListIterator itr = list_iterator_create(node_list);
	while((n = (node_t*)list_next(itr))){
		if (n->id == node->id){
			return 0;
		}
	}
	list_iterator_destroy(itr);
	return 1;
}

/** 
 */
char* convert_place(int place)
{
	if (place == INTERNAL)
		return "INTERNAL";
	else 
		return "EXTERNAL";
}


/** 
 */
char* convert_conn_type(conn_type_t conn_type)
{
	return (conn_type == TORUS) ? "T" : "M";
}

/** */
void delete_system()
{
	list_destroy(global_sys->node_list);
	list_destroy(global_sys->switch_list);
	list_destroy(global_sys->connection_list);
	list_destroy(global_sys->partition_list);
	xfree(global_sys);
	__initialized = false;
}

/** returns true if this is a "node" connection */
bool is_node_connection(connection_t* conn)
{
	/* only if the connection is internal does it have a
	 * pointer to its node
	 */
	if (conn->place == INTERNAL &&
	    conn->node != NULL &&
	    conn->node->is_node)
		return true;
	else 
		return false;
}

/** */
void delete_gen(void* object)
{
	xfree(object);
}

void new_switch_config(switch_config_t** config, Label id, dimension_t dim, 
		       Label node_src, int port_src, 
		       Label node_tar, int port_tar)
{
	(*config) = (switch_config_t*) xmalloc(sizeof(switch_config_t));

	(*config)->id = id;
	(*config)->dim = dim;
	(*config)->node_src = node_src;
	(*config)->port_src = port_src;
	(*config)->node_tar = node_tar;
	(*config)->port_tar = port_tar;
}

void delete_switch_config(void* object)
{
	
	switch_config_t* conf = (switch_config_t*) object;
	xfree(conf);
}

/** 
 */
void print_switch_config(switch_config_t* config)
{
	printf("switch_config id:\t%d\n", config->id);
	printf("      dim:\t%d\n", config->dim);
	printf("   source:\t%d: %d \n", config->node_src, config->port_src);
	printf("   target:\t%d: %d \n", config->node_tar, config->port_tar);
}
