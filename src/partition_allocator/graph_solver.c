/*****************************************************************************\
 *  graph_solver.c
 * 
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "graph_solver.h"
#include "src/common/xstring.h"

#define SUCCESS 0
#define ERROR -1

#define NOT_INDETERMINATE != NO_VAL
#define IS_INDETERMINATE < 0

#define SWAP(a,b,t)	\
_STMT_START {		\
	(t) = (a);	\
	(a) = (b);	\
	(b) = (t);	\
} _STMT_END

// #define PRINT_RESULTS
// #define DEBUG_PRINT_RESULTS
// #define DEBUG
/* this is for debugging, so that we don't run through all
 * permutations each time
 */
// #define PERM_DEBUG
int __permutations;
#define __PERM_LIMIT 1000

// for list utility functions
// extern void * lsd_fatal_error(char *file, int line, char *mesg){exit(0);}
// extern void * lsd_nomem_error(char *file, int line, char *mesg){exit(0);}

enum conn_labels {LABEL_IGNORE, LABEL_FOUND, LABEL_RECYCLE, 
		  LABEL_MERGE};

char* convert_label(int label);

/** internally used helper functions */
void _join_ext_connections(switch_t* switchA, int port_numA, 
			   switch_t* switchB, int port_numB,
			   connection_t** conn);
/* */
int _join_int_connections(switch_t* my_switch, int port_numA, int port_numB,
			  connection_t** conn);
/** */
int _test_ext_connections(node_t* node);
void _init_internal_connections(system_t *sys);
/** */
void _swap(char* a, int i, int j);
/** */
void _enumerate(char* enum_string, int string_size, List results);
/** */
int _find_all_port_permutations(int** plus_ports, List* minus_ports_list);
				
/** */
int _find_all_switch_permutations(node_t* node, int* plus_ports, 
				  List minus_ports_list);
				  
/** */
int _find_all_paths(int* plus_ports, List minus_ports_list,
		    List current_nodes, List current_configs,
		    List part_config_list);
		    
/** */
void _find_all_paths_aux(List* connection_list, List current_port_conf,
			 List part_config_list);
			 
/** */
void _connect_switch(switch_t* my_switch, int* plus_ports, int* minus_ports);
/** */
void _test_list_copy(List A);
/** */
int _convert_string_to_int_array(char* string, int size, int** result_array);
/** */
int _connection_t_cmpf(connection_t* A, connection_t* B);
/** */
int _get_connection_partition(connection_t* conn, List partitions_list);
/** */
int _merge_connection(connection_t* conn, List partitions_list);
/** */
int _label_connection(connection_t* conn);
/* reset the sys structures */
void _reset_sys();
/* reset the partition data */
void _reset_partitions();
/** reset all the indeterminate connections back to their correct values */
void _reset_indeterminate_connections();
/** */
void _init_local_partition(List result_partitions);
/** */
void _print_all_connections();
/** */
void _print_results(List result_partitions, List current_port_conf);
/** */
void _insert_results(List part_config_list, List result_partitions, List current_port_conf);

/** configure up the system to get ready for partitioning*/
// int init_system(system_t* sys)
int init_system(List port_config_list, int num_nodes)
{
	ListIterator itr;
	int i;
	node_t *node;
	switch_t *switch_x; // , *switch_y, *switch_z;
	switch_t *switch_src, *switch_tar;
	connection_t *conn; 
	switch_config_t* switch_config;

#ifdef PERM_DEBUG
	__permutations = 0;
#endif

	/** bad bad programming practice, just a quick fix...for now...*/
	__initialized = false;
	/** */

	new_system();

	/* 1) create all the nodes */
	for (i=0; i<num_nodes; i++){
		new_node(&node, i);
	}

	/* wire the nodes up externally */
	/* FIXME, need to do this intelligently, and dynamically 
 * according to some config file
	 */

	/* 2) create the switches for the nodes */
	itr = list_iterator_create(global_sys->node_list);
	for (i=0; i<num_nodes; i++){	
		node = (node_t*) list_next(itr);
		
		new_switch(&switch_x, node, i, X);
		// new_switch(&switch_y, node, i, Y);
		// new_switch(&switch_z, node, i, Z);
	}
	list_iterator_destroy(itr);

	/* 3) wire up the external connections */
	itr = list_iterator_create(port_config_list);
	while ((switch_config = (switch_config_t*) list_next(itr))){
		new_connection(&conn);
		conn->place = EXTERNAL;
		switch_src = get_switch(switch_config->node_src, switch_config->dim);
		switch_tar = get_switch(switch_config->node_tar, switch_config->dim);

		if (!switch_src || ! switch_tar){
			printf("Error, external switch configuration failed\n");
			return ERROR;
		}
		_join_ext_connections(switch_src, switch_config->port_src,
				      switch_tar, switch_config->port_tar,
				      &conn);
		list_push(global_sys->connection_list, conn);
	}
	list_iterator_destroy(itr);

	// print_system(global_sys);
	return SUCCESS;
}

/** */
void new_conf_result(conf_result_t** conf_result, conf_data_t* conf_data)
{
	*conf_result = (conf_result_t*) xmalloc(sizeof(conf_result_t));
	(*conf_result)->conf_data = conf_data;
	(*conf_result)->port_conf_list = list_create(delete_port_conf);
}

/** delete a conf_result_t */
void delete_conf_result(void* object)
{
	conf_result_t* conf_result = (conf_result_t*) object;

	delete_conf_data(conf_result->conf_data);
	list_destroy(conf_result->port_conf_list);
	xfree(conf_result);
}

/** */
void print_conf_result(conf_result_t* conf_result)
{
	int i, j;
	int num_part;
	if (!conf_result){
		printf("printf_conf_result, error conf_result is NULL\n");		
		return;
	}

	printf("conf_result: \n");
	printf("  port_conf: ");
	print_port_conf_list(conf_result->port_conf_list);

	num_part = conf_result->conf_data->num_partitions;
	printf("  conf_data: ");
	for (i=0; i<num_part; i++){
		printf("%d", conf_result->conf_data->partition_sizes[i]);
		printf("(");
		for (j=0; j<conf_result->conf_data->partition_sizes[i]; j++){
			printf("%d", conf_result->conf_data->node_id[i][j]);
		}
		printf(")");

		printf(" %s", convert_conn_type(conf_result->conf_data->partition_type[i]));
		if (i != (num_part - 1)){
			printf(", ");
		}
	}
	printf("\n");
}

void print_conf_result_list(List conf_result_list)
{
	ListIterator itr;
	conf_result_t* conf_result;

	itr = list_iterator_create(conf_result_list);
	while((conf_result = (conf_result_t*) list_next(itr) )){
		print_conf_result(conf_result);
	}
	list_iterator_destroy(itr);
}

/** */
void new_conf_data(conf_data_t** conf_data, int num_partitions)
{
	*conf_data = (conf_data_t*) xmalloc(sizeof(conf_data_t));
	(*conf_data)->num_partitions = num_partitions;
	(*conf_data)->partition_sizes = (int*) xmalloc(sizeof(int) * num_partitions);
	(*conf_data)->partition_type = (conn_type_t*) xmalloc(sizeof(conn_type_t) * num_partitions);

	/* note that before using the node_id, you must again xmalloc for each entry */
	(*conf_data)->node_id = (int**) xmalloc(sizeof(int*) * num_partitions);
}

/** */
void delete_conf_data(void* object)
{
	int i;
	conf_data_t* conf_data = (conf_data_t*) object;
	
	for (i=0; i<conf_data->num_partitions; i++){
		xfree(conf_data->node_id[i]);
	}
	xfree(conf_data->node_id);
	xfree(conf_data->partition_sizes);
	xfree(conf_data->partition_type);
	xfree(conf_data);
}

/** */
void new_port_conf(port_conf_t** port_conf, int* plus_ports, int* minus_ports)
{
	int i;
	(*port_conf) = (port_conf_t*) xmalloc(sizeof(port_conf_t));
	(*port_conf)->plus_ports = (int*) xmalloc(sizeof(int)*NUM_PLUS_PORTS);
	(*port_conf)->minus_ports = (int*) xmalloc(sizeof(int)*NUM_MINUS_PORTS);

	for (i=0; i<NUM_PLUS_PORTS; i++){
		(*port_conf)->plus_ports[i] = plus_ports[i];
	}

	for (i=0; i<NUM_MINUS_PORTS; i++){
		(*port_conf)->minus_ports[i] = minus_ports[i];
	}
}

/** */
void delete_port_conf(void * object)
{
	port_conf_t* port_conf = (port_conf_t*) object;
	xfree(port_conf->plus_ports);
	xfree(port_conf->minus_ports);
	xfree(port_conf);
}

/** 
 * 
 * IN - sys: 
 * 
 * OUT - part_config_list: results of the graph search for
 * the given configuration, as set by init_system();
 */
int find_all_tori(List part_config_list)
{
	List switches_copy = NULL, minus_ports_list = NULL;
	int *plus_ports = NULL;
	List current_configs;
	
	if (_find_all_port_permutations(&plus_ports, &minus_ports_list)){
		printf("had some trouble there with the find all port perms\n");
		return ERROR;
	} 

	list_copy(global_sys->switch_list, &switches_copy);
	/**
	 * find all the toroidal paths in the system, and insert
	 * results into part_config_list
	 */
#ifdef PRINT_RESULTS
	printf("(size/type)\t\t: Y(N) X(N) Y(N-1) X(N-1) ... Y(1) X(1) \n");
#endif
	current_configs = list_create(delete_port_conf);
	_find_all_paths(plus_ports, minus_ports_list,
			switches_copy, current_configs,
			part_config_list);
	
	list_destroy(current_configs);
	list_destroy(switches_copy);

	xfree(plus_ports);
	list_destroy(minus_ports_list);

	/* if the configs and num nodes are too big, then the
	 * algorithm may not finish, so you may want to print this out
	 * to indicate in the output file an actual successful finish.
	 printf("ALL DONE\n");
	 */

	return SUCCESS;
}

/** 
 * recursively iterates through all the possible combinations of
 * wiring configurations and uses _find_all_paths to find all the
 * paths in the given configuration.
 * 
 * NOTE: if we wanted to split each of these permutations up (have
 * _find_all_paths_aux be a pthread), then we need to make copies of
 * the system at each of those, and also keep the results
 * "synchronzied" (in a mutex/semaphore, etc).
 */
int _find_all_paths(int* plus_ports, List minus_ports_list,
		    List current_switches, List current_configs,
		    List part_config_list)
{
	ListIterator list_itr;
	int* minus_ports = NULL;
	switch_t* my_switch;
	port_conf_t* port_conf;
	List connection_list_copy;
	
	/* order doesn't matter here because all switches will 
	 * iterate through all the permutations anyway
	 */
	my_switch = list_pop(current_switches);
	if (!my_switch) {
		printf("ERROR: _find_all_paths: went past the end\n");
		return ERROR;
	}
	list_itr = list_iterator_create(minus_ports_list);
	while((minus_ports = (int*) list_next(list_itr))){
		_connect_switch(my_switch, plus_ports, minus_ports);
		/* print the configured switch:  */
		// print_node(my_switch);
		new_port_conf(&port_conf, plus_ports, minus_ports);
		list_push(current_configs, port_conf);

		/** base case */
		if (list_count(current_switches) == 0){
			// print_port_conf_list(current_configs);
			list_copy(global_sys->connection_list, &connection_list_copy);
			_find_all_paths_aux(&connection_list_copy, current_configs, part_config_list);
			list_destroy(connection_list_copy);
		} else {
			_find_all_paths(plus_ports, minus_ports_list, current_switches,
					current_configs, part_config_list);
		}
		port_conf = list_pop(current_configs);
		delete_port_conf(port_conf);
	}
	list_iterator_destroy(list_itr);

	/** we have to put the guy back in*/
	list_push(current_switches, my_switch);

	return SUCCESS;
}

/** 
 * go through the list of all system connections and search for
 * the connection's partition.
 *
 * This is called for a configuration instance, ie: 
 * node [0] plus port [0 3 5] minus port [1 2 4]
 * node [1] plus port [0 3 5] minus port [1 2 4]
 * node [2] plus port [0 3 5] minus port [2 1 4]
 * 
 */
void _find_all_paths_aux(List* connection_list, List current_port_conf, 
			 List part_config_list)
{
	connection_t* conn;

	/* number of indeterminate connections */
	int num_ind = 0, num_itr = 0;
	int last_num_ind = NO_VAL;
	connection_t* list_head = NULL;

	int label;

#ifdef PERM_DEBUG
	__permutations++;
	if (__permutations > __PERM_LIMIT){
		printf("test complete\n");
		exit(0);
	}
#endif

	/* need to keep a local copy of all this stuff */
	List result_partitions = list_create(delete_partition);
	_init_local_partition(result_partitions);
#ifdef DEBUG	
	printf("_find_all_paths_aux\n");
#endif

	list_sort (*connection_list, (ListCmpF) _connection_t_cmpf);
	// conn_itr = list_iterator_create(connection_list);
	/** 
	 * this is the engine of the graph solving program.  it
	 * iterates through all connections and labels them according
	 * to the connection charateristics.  if either end of the
	 * connection is connected to a pre-lableled connection (ie,
	 * in a node), then it inherits the label.  otherwise it is
	 * marked as indeterminate.  since all partitions (connected
	 * graphs w/ nodes) originate at nodes and thus the labels
	 * will "spread out" on each cycle.  Thus one important
	 * property of this loop is that the number of indeterminate
	 * connections that participate in a partition will decrease
	 * on each iteration.  If the indeterminate connections do not
	 * decrease, that indicates that the algorithm has found all
	 * partitions, and hence we are done.
	 * 
	 * 
	 * 
	 * NOTE: one way to make each iteration best case: 
	 * (num permutations) * O(indeterminate_conns - 1) faster is if we
	 * keep a separate List indeterminate_conns, and when we remove the 
	 * head of a list, we simply move the head of the list to the next
	 * in line indeterinate conn.
	 */
	while((conn = (connection_t*) list_pop(*connection_list))){
		label = _label_connection(conn);
#ifdef DEBUG
		printf("------------------------------------\n");
		printf(" _label_connection: %s\n", convert_label(label));
		// printf("number of partitions %d\n", list_count(global_sys->partition_list));
		// printf("number of current partitions %d\n", list_count(result_partitions));
		print_connection(conn);
#endif
		switch(label){
		case LABEL_IGNORE: 
			/* drop the connection */
#ifdef DEBUG
			printf("find_all_paths_aux: LABEL_IGNORE, dropping connection\n");
#endif
			break;
		case LABEL_FOUND: 
			/* simply add the connection to the partition*/
			if (conn->partition == NULL){
				// this also adds the connection to the partition
				_get_connection_partition(conn, result_partitions);
			}
			/* otherwise, the connection should already be
			 * in the partition */
			break;
		case LABEL_MERGE:
			if (_merge_connection(conn, result_partitions)){
				printf("_find_all_paths_aux failed for LABEL_MERGE\n");
				exit(0);				
			}
			break;
		case LABEL_RECYCLE:
			num_ind++;
			/** dunno if we're going to have probs with the list */
			list_append(*connection_list, conn);
			if (list_head == NULL){
#ifdef DEBUG
				printf("_find_all_paths_aux: defining head of list\n");
#endif
				list_head = conn;
			}
			/* if the former head is removed, then we have
			 * to redefine the head
			 */
			else if (list_head->id NOT_INDETERMINATE){
#ifdef DEBUG
				printf("_find_all_paths_aux: redefining head of list\n");
#endif
				list_head = conn;
				/* since we don't know "where" we are
				 * in the list, we must reset the
				 * ind_conn counters so that we know
				 * where we are in the list.
				 */
				last_num_ind = NO_VAL;
				num_ind = 0;
			}
			/* */
			else {
				/* list_head = wrap in indeterminate conns list */
				if (conn == list_head){
					num_itr++;
					/* first case = progress being made */
#ifdef DEBUG
					printf("_find_all_paths_aux: back at head\n");
					printf("_find_all_paths_aux: num_ind <%d> last_num_ind <%d>\n",
					       num_ind, last_num_ind);
#endif
					if (last_num_ind == NO_VAL || num_ind < last_num_ind){
						last_num_ind = num_ind;
						num_ind = 0;
					} 
					/* 2nd case = no change,  */
					else if (num_ind == last_num_ind){
#ifdef DEBUG
						printf("no change since last iteration, we're done\n");
#endif
						goto done;
					} 
					/* 3rd case = ...wtf?  */
					else {
						printf("ERROR, finding more work on this iteration???\n");
						printf("_find_all_paths_aux: num_ind <%d> last_num_ind <%d>\n",
						       num_ind, last_num_ind);
						exit(0);
					}
				} 
			}
			break;
		case ERROR: 
		default:
			printf("_label_connection: ERROR, unknown label\n"); /** dunno what's going on... */
			exit(0);
		}
	}
 done:

#ifdef DEBUG_PRINT_RESULTS

	printf("* * * * * * * * * * * * * * * * * * * * * * * * * * * * * \n");
 	// printf("* found number of partitions <%d>\n", list_count(global_sys->partition_list));
	printf("* found number of partitions <%d>\n", list_count(result_partitions));
	printf("* * * * * * * * * * * * * * * * * * * * * * * * * * * * * \n");
	// itr = list_iterator_create(global_sys->partition_list);
	itr = list_iterator_create(result_partitions);
	while(part = (partition_t*) list_next(itr)){
		printf("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - \n");
		print_partition(part);
	}
	list_iterator_destroy(itr);

	printf("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - \n");
	printf("- number of iterations for this set of connections: <%d>\n", num_itr);
	printf("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - \n");
#endif /* DEBUG_PRINT_RESULTS */

#ifdef PRINT_RESULTS
	_print_results(result_partitions, current_port_conf);
#endif
	_insert_results(part_config_list, result_partitions, current_port_conf);

	list_destroy(result_partitions);
	_reset_sys();
}

void _insert_results(List part_config_list, List result_partitions, List current_port_conf)
{
	int part_size;
	bool has_large_part=false, found_torus=false;
	ListIterator itr;
	partition_t* part;

	/* check to see if this set has partitions greater than or
	 * equal to LARGE_PART size partitions, and that we at least
	 * have one toroidal partition (otherwise, it's worthless to
	 * print out.
	 */
	itr = list_iterator_create(result_partitions);
	while((part = (partition_t*) list_next(itr))){
		part_size = list_count(part->node_list);
		if (part_size >= LARGE_PART){
			has_large_part = true;
			if (part->conn_type == TORUS){
				found_torus = true;
			}
		}
	}
	list_iterator_destroy(itr);
	
	if (has_large_part && found_torus){
		conf_data_t* conf_data;
		conf_result_t* conf_result;
		port_conf_t* port_conf, *port_conf_copy;
		ListIterator node_itr;
		int i;
		node_t* node;

		new_conf_data(&conf_data, list_count(result_partitions));

		itr = list_iterator_create(result_partitions);
		i = 0;
		while((part = (partition_t*) list_next(itr))){
			int j;
			conf_data->partition_sizes[i] = list_count(part->node_list);
			conf_data->partition_type[i] = part->conn_type;

			/* this is xfree'd by delete_conf_data */
			j = list_count(part->node_list);
			conf_data->node_id[i] = (int*) xmalloc (sizeof(int) * j);
			node_itr = list_iterator_create(part->node_list);
			while((node = (node_t*) list_next(node_itr))){
				conf_data->node_id[i][--j] = node->id;
			}
			list_iterator_destroy(node_itr);
			
			i++;
		}			
		list_iterator_destroy(itr);

		new_conf_result(&conf_result, conf_data);

		itr = list_iterator_create(current_port_conf);
		while((port_conf = (port_conf_t*) list_next(itr))){
			new_port_conf(&port_conf_copy, port_conf->plus_ports, port_conf->minus_ports);
			list_push(conf_result->port_conf_list, port_conf_copy);
		}			
		list_iterator_destroy(itr);

		list_push(part_config_list, conf_result);
	}
}

/** print out the results of the search */
void _print_results(List result_partitions, List current_port_conf){
	int part_size;
	bool has_large_part=false, found_torus=false;
	ListIterator itr;
	partition_t* part;

	/* check to see if this set has partitions greater than or
	 * equal to LARGE_PART size partitions, and that we at least
	 * have one toroidal partition (otherwise, it's worthless to
	 * print out.
	 */
	itr = list_iterator_create(result_partitions);
	while((part = (partition_t*) list_next(itr))){
		part_size = list_count(part->node_list);
		if (part_size >= LARGE_PART){
			has_large_part = true;
			if (part->conn_type == TORUS){
				found_torus = true;
			}
		}
	}
	list_iterator_destroy(itr);

	if (has_large_part && found_torus){
		itr = list_iterator_create(result_partitions);
		while((part = (partition_t*) list_next(itr))){
			printf("%d/%s ", list_count(part->node_list),
			       convert_conn_type(part->conn_type));
			
		}			
		list_iterator_destroy(itr);
		printf("\t: ");
		print_port_conf_list(current_port_conf);
	}
}

/* reset the sys structures */
void _reset_sys()
{
	ListIterator conn_itr;
	connection_t* conn;

	/* reset all the indeterminate connections back to their
	 * correct values.  set all connections that are not the
	 * special "node" connection and set as back as INDETERMINATE
	 * (NO_VAL);
	 */
	conn_itr = list_iterator_create(global_sys->connection_list);
	while((conn = (connection_t*) list_next(conn_itr))){
		conn->id = conn->original_id;
		if (conn->id == NO_VAL){
			conn->partition = NULL;
		}
	}
	list_iterator_destroy(conn_itr);
}

/** 
 *
 * NOTE: if we switched to using arrays and pointers for INTERNAL/EXTERNAL, 
 * and the endpoints, then this function could be cut in half.
 *
 */
int _label_connection(connection_t* conn)
{
#ifdef DEBUG
	printf("_label_connection\n");
#endif
	if (!conn){
		printf("_label_connection error, connection NULL\n");
		return ERROR;
	} 

	if (conn->id NOT_INDETERMINATE){
#ifdef DEBUG
		printf("_label_connection: found prelabeled node\n");
#endif
		return LABEL_FOUND;
	} 

	if (conn->place == INTERNAL){
#ifdef DEBUG
		printf("internal connection found\n");
#endif
		/* CASE A: both endpoints non-NULL */
		if (conn->ep0->conn_ext != NULL && conn->ep1->conn_ext != NULL){
			/** both endpoints with labels */
			if (conn->ep0->conn_ext->id NOT_INDETERMINATE && 
			    conn->ep1->conn_ext->id NOT_INDETERMINATE){
#ifdef DEBUG
				printf("_label_connection: both endpoints have labels\n");
#endif

				return LABEL_MERGE;
			} 
			/** no endpoints with label */
			else if (conn->ep0->conn_ext->id IS_INDETERMINATE && 
				 conn->ep1->conn_ext->id IS_INDETERMINATE){
#ifdef DEBUG
				printf("_label_connection: no endpoints have labels\n");
#endif
				return LABEL_RECYCLE;
			} 
			/** one endpoint with label */
			else {
#ifdef DEBUG
				printf("_label_connection: one endpoint has label\n");
#endif
				if (conn->ep0->conn_ext->id NOT_INDETERMINATE){
					conn->id = conn->ep0->conn_ext->id;
					return LABEL_FOUND;
				} else {
					conn->id = conn->ep1->conn_ext->id;
					return LABEL_FOUND;
				}
			}
		}

		/* CASE B: both endpoints NULL */
		else if (conn->ep0->conn_ext == NULL && conn->ep1->conn_ext == NULL){
#ifdef DEBUG
			printf("_label_connection: both endpoints NULL\n");
#endif
			return LABEL_IGNORE;
		} 

		/* CASE C: one side of connection NULL */
		else {
#ifdef DEBUG
			printf("_label_connection: one side of connection NULL\n");
#endif
			/** if one side is labeled, place this
			 * connection into that partition.
			 */
			if (conn->ep0->conn_ext != NULL && 
			    conn->ep0->conn_ext->id NOT_INDETERMINATE){
				conn->id = conn->ep0->conn_ext->id;
				return LABEL_FOUND;
			} else if (conn->ep1->conn_ext != NULL && 
				   conn->ep1->conn_ext->id NOT_INDETERMINATE){
				conn->id = conn->ep1->conn_ext->id;
				return LABEL_FOUND;
			}
			/* otherwise,  otherwise, recycle */
			else {
				return LABEL_RECYCLE;
			}
		} 
	} /** for external connections */
	else {
#ifdef DEBUG
		printf("external connection found\n");
#endif
		/* CASE A: both endpoints non-NULL */
		if (conn->ep0->conn_int != NULL && conn->ep1->conn_int != NULL){
			/** both endpoints with labels */
			if (conn->ep0->conn_int->id NOT_INDETERMINATE && 
			    conn->ep1->conn_int->id NOT_INDETERMINATE){
#ifdef DEBUG
				printf("_label_connection: both endpoints have labels\n");
#endif
				return LABEL_MERGE;
			}
			/** no endpoints with label */
			else if (conn->ep0->conn_int->id IS_INDETERMINATE && 
				 conn->ep1->conn_int->id IS_INDETERMINATE){
#ifdef DEBUG
				printf("_label_connection: no endpoints have labels\n");
#endif
				return LABEL_RECYCLE;
			} 
			/** one endpoint with label */
			else {
#ifdef DEBUG
				printf("_label_connection: one endpoint has label\n");
#endif
				if (conn->ep0->conn_int->id NOT_INDETERMINATE){
					conn->id = conn->ep0->conn_int->id;
					return LABEL_FOUND;
				} else {
					conn->id = conn->ep1->conn_int->id;
					return LABEL_FOUND;
				}
			}
		}

		/* CASE B: both endpoints NULL */
		else if (conn->ep0->conn_int == NULL 
			 && conn->ep1->conn_int == NULL){
#ifdef DEBUG
			printf("_label_connection: both endpoints NULL\n");
#endif
			return LABEL_IGNORE;
		}		

		/* CASE C: one side of connection NULL. */
		else {
#ifdef DEBUG
			printf("_label_connection: one side of connection NULL\n");
#endif
			/* if one side
			 * is labeled, place this connection into that
			 * partition.
			 */
			if (conn->ep0->conn_ext != NULL && 
			    conn->ep0->conn_ext->id NOT_INDETERMINATE){
				conn->id = conn->ep0->conn_ext->id;
				return LABEL_FOUND;
			} else if (conn->ep1->conn_ext != NULL && 
				   conn->ep1->conn_ext->id NOT_INDETERMINATE){
				conn->id = conn->ep1->conn_ext->id;
				return LABEL_FOUND;
			}
			/* otherwise,  recycle */
			else {
				return LABEL_RECYCLE;
			}
		}
	}
}

/**
 * NOTE: since we place the configs in a stack fashion the list is
 * printed "backwards" 
 * 
 * it's inefficient to print the list out this way, so in production,
 * we should simply print it backwards, or ask moe to add the reverse
 * iterator
 */
void print_port_conf_list(List port_configs)
{
	port_conf_t* port_conf;
	ListIterator itr = list_iterator_create(port_configs);

	while((port_conf = (port_conf_t*) list_next(itr))){
		printf("%d%d%d ",
		       port_conf->minus_ports[0], 
		       port_conf->minus_ports[1], 
		       port_conf->minus_ports[2]);
	}
	printf("\n");
	list_iterator_destroy(itr);
}

/** */
void _print_all_connections()
{
	ListIterator itr;
	connection_t* conn;

	printf("printing all system connections\n");
	itr = list_iterator_create(global_sys->connection_list);
	while((conn = (connection_t*) list_next(itr))){
		printf("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - \n");
		print_connection(conn);
	}
	list_iterator_destroy(itr);
	printf("- - - - - - - - - - - - - - - - - - - - - - - - - - - - - \n");
}

/** */
void print_system(system_t* sys)
{
	ListIterator itr;
	node_t* node;
	switch_t* my_switch;
	connection_t* conn;

	/** print out all the nodes */
	printf("-----------------------------\n");
	printf("number of nodes in system: %d\n", 
	       list_count(sys->node_list));
	itr = list_iterator_create(sys->node_list);
	while ((node = (node_t*) list_next(itr))) {
		print_node(node);
	}
	list_iterator_destroy(itr);

	/** print out all the switches */
	printf("--------------------------------\n");
	printf("number of switches in system: %d\n", 
	       list_count(sys->switch_list));
	itr = list_iterator_create(sys->switch_list);
	while ((my_switch = (switch_t*) list_next(itr))) {
		print_node(my_switch);
	}
	list_iterator_destroy(itr);
	
	printf("\n--------------------------------\n");
	printf("external connections in system:\n");
	itr = list_iterator_create(sys->connection_list);
	while ((conn = (connection_t*) list_next(itr))) {
		if (conn->place == EXTERNAL)
			print_connection(conn);
	}
	list_iterator_destroy(itr);
}

/** */
void _join_ext_connections(switch_t* switchA, int port_numA, 
			   switch_t* switchB, int port_numB, 
			   connection_t** conn)
{
	/* FIXME 2 and 5 should read in from somewhere, and not hardcoded here*/
	int PORT_MIN, PORT_MAX;
	PORT_MIN = 2;
	PORT_MAX = 5;

	if (!switchA || port_numA < PORT_MIN || port_numA > PORT_MAX ||
	    !switchB || port_numB < PORT_MIN || port_numB > PORT_MAX || 
	    !(*conn)){
		printf("error, _join_ext_connection input incomplete\n");
		return;
	}

	/* join the connections to the node ports */
	(*conn)->ep0 = &(switchA->ports[port_numA]);
	(*conn)->ep1 = &(switchB->ports[port_numB]);

	/* join the node ports to the connections */
	switchA->ports[port_numA].conn_ext = (*conn);
	switchB->ports[port_numB].conn_ext = (*conn);
}

/** 
 * the definition of a valid node is in reference to toridal connects:
 * 
 * valid: if both internal connections to the ports with the BP
 * connect to valid external ports (non-NULL) and it has two or more
 * of these types of connections.
 * 
 * RETURN - TRUE (0) if valid, FALSE (1) otherwise
 */
int _test_ext_connections(node_t* node)
{
	ListIterator int_conn_itr;
	connection_t* conn;
	int bp0, bp1, found_bp_conns; // boolean values for having found the two bp conns

	bp0 = bp1 = found_bp_conns = 0;

	int_conn_itr = list_iterator_create(node->connection_list);
	while((conn = (connection_t*) list_next(int_conn_itr))){
		if (conn->ep0->conn_ext != NULL && conn->ep1->conn_ext != NULL){
			if (conn->ep0->conn_ext->id NOT_INDETERMINATE ||
			    conn->ep0->conn_ext->id NOT_INDETERMINATE){
				found_bp_conns++;
				if (!bp0) {
					bp0 = 1;
				} else {
					bp1 = 1;
					printf("test_ext_conn: found both BPs, this node valid");
					break;
				}
			}
		}
		
	}
	list_iterator_destroy(int_conn_itr);

	if (bp0 == 1 && bp1 == 1 && found_bp_conns > 1){
		return SUCCESS;
	} else {
		return ERROR;
	}
}

/** 
 * join the connections to internal ports of the given node.
 * 
 * IN - node: node to wire up
 * IN - port_numA: one side of the connection
 * IN - port_numB: the other side of the connection
 * IN - conn: connection to use
 * RETURN - the success of the connection.
 */
int _join_int_connections(switch_t* my_switch, int port_numA, int port_numB, 
			  connection_t** conn)
{
	/* FIXME 2 and 5 should read in from somewhere, and not hardcoded here */
	int PORT_MIN, PORT_MAX;
	PORT_MIN = 0;
	PORT_MAX = 5;

	/** don't touch the internal connections of a node*/
	if (my_switch->is_node){
#ifdef DEBUG
		printf("_join_int_connections: internal wires of node should not be changed\n");
#endif		
		return SUCCESS;
	}

	if (!my_switch){
		printf("error, _join_int_connection: given node null\n");
		return ERROR;
	}
	if (port_numA < PORT_MIN || port_numA > PORT_MAX ||
	    port_numB < PORT_MIN || port_numB > PORT_MAX){
		printf("error, _join_int_connection: port numbers outside of allowable port"
		       " range: port 1: %d port 2: %d >< min: %d max: %d\n",
		       port_numA, port_numB, PORT_MIN, PORT_MAX);
		return ERROR;
	} 
	
	if (!(*conn)){
		printf("error, _join_int_connection: connection null\n");
		return ERROR;
	}

	/* join the connections to the node ports */
	(*conn)->ep0 = &(my_switch->ports[port_numA]);
	(*conn)->ep1 = &(my_switch->ports[port_numB]);

	/* join the node ports to the connections */
	my_switch->ports[port_numA].conn_int = (*conn);
	my_switch->ports[port_numB].conn_int = (*conn);

	return SUCCESS;
}

/** 
 * the connection should have both endpoints as non NULL
 * 
 * doesn't matter if both partitions have the same label or different
 * labels.
 */
int _merge_connection(connection_t* conn, List partition_list)
{
	if (conn->place == INTERNAL){
		merge_partitions(conn->ep0->conn_ext->partition,
				 conn->ep1->conn_ext->partition,
				 partition_list);
		conn->id = conn->ep0->conn_ext->id; 
	} else {
		merge_partitions(conn->ep0->conn_int->partition,
				 conn->ep1->conn_int->partition,
				 partition_list);
		conn->id = conn->ep0->conn_int->id;
	}

	_get_connection_partition(conn, partition_list);
	return SUCCESS;
}

/** 
 * find all the possible wiring ups for a given node.  This will not
 * be the same for all nodes.  For each of the precalculated port
 * matches, we see if those ports are connected externally.  if not,
 * then that switch configuration doesn't work.
 *
 * OUT - List* nodes: 
 *
 */
int _find_all_switch_permutations(switch_t* my_switch, int* plus_ports, 
				List minus_ports_list)
{
	int *minus_ports;
	ListIterator list_itr;
	minus_ports = NULL;

	/** for each permutation, create a node of that permutation */
	list_itr = list_iterator_create(minus_ports_list);
	while((minus_ports = (int*) list_next(list_itr))){
		printf("_find_all_node_permutations: plus port %d %d %d minus port %d %d %d\n", 
		       plus_ports[0], plus_ports[1], plus_ports[2],
		       minus_ports[0], minus_ports[1], minus_ports[2]);

		_connect_switch(my_switch, plus_ports, minus_ports);
	}
	list_iterator_destroy(list_itr);

	return SUCCESS;
}

/** 
 * find all the possible port permutations.  So, for example in BGL,
 * we have plus ports: 035 and minus ports: 124, and plus can only the
 * plug into minus.  This function finds all the permutations of that.
 * To find all these, we basically hold one static (the plus ports)
 * and change out the order of the minus ports.
 * 
 * NOTE: this function seems pretty BGL specific
 * 
 * IN - nothing at this point, but the plus/minus ports should be read
 * in from a file
 * 
 * OUT - int* plus_int: the plus ports in an int array
 * OUT - List minus_int_list: the permutations of the minus ports in a
 * list of int arrays
 * 
 * NOTE: the outbound structures will be created by this function and
 * must be freed by the caller.
 */
int _find_all_port_permutations(int** plus_ports, List* minus_int_list)
{
	int rc, string_size;
	int *minus_ports = NULL;
	char *plus_ports_str;
	char *minus_ports_str;
	List minus_str_list;
	rc = SUCCESS;

	*plus_ports = NULL;

	/** FIXME: this should be input from the config file */
	plus_ports_str = xstrdup("035");
	minus_ports_str = xstrdup("124");

	/** we assume that the num of plus ports = minus ports, which
	 * may be incorrect */
	string_size = strlen(plus_ports_str);
	if (string_size != strlen(minus_ports_str)){
		printf("ERROR! # plus ports != # minus ports");
		exit(0); 
	}

	/** 
	 * we keep the plus ports static, and permute the minus
	 * ports.
	 */
	minus_str_list = list_create(delete_gen);
	_enumerate(minus_ports_str, string_size, minus_str_list);

	/** convert the strings to a form we can use */
	*plus_ports = (int*) xmalloc(sizeof(int)*string_size);
	if (!(*plus_ports)){
		printf("error, not enough mem for plus ports\n");
		exit(1);
	}
	
	if (_convert_string_to_int_array(plus_ports_str, string_size, plus_ports)){
		printf("ERROR! can't convert plus_port string to int array\n");
		exit(0);
	}

	*minus_int_list = list_create(delete_gen);

	/* 999 debugging stuff: reduced set of configs */
	/*
	minus_ports = (int*) xmalloc(sizeof(int) * string_size);
	minus_ports[0] = 1;
	minus_ports[1] = 4;
	minus_ports[2] = 2;
	list_append(*minus_int_list, minus_ports);

	minus_ports = (int*) xmalloc(sizeof(int) * string_size);
	minus_ports[0] = 4;
	minus_ports[1] = 1;
	minus_ports[2] = 2;
	list_append(*minus_int_list, minus_ports);
	*/
	ListIterator itr;
	char* minus_string;
	itr = list_iterator_create(minus_str_list);
	while((minus_string = (char*) list_next(itr))){
		minus_ports = (int*) xmalloc(sizeof(int) * string_size);
		if (_convert_string_to_int_array(minus_string, string_size, &minus_ports)){
			printf("ERROR! can't convert string to int array\n");
			exit(0);
		}
		list_append(*minus_int_list, minus_ports);
	}
	list_iterator_destroy(itr);

	xfree(plus_ports_str);
	xfree(minus_ports_str);
	list_destroy(minus_str_list);

	return rc;
}

/** 
 * initialize as:
 *       0  1
 *    /--|--|--\
 *    |  \--/  |
 *  5 --\    /-- 2
 *    |  \  /  |
 *    \__|__|__/
 *       4  3
 */      
void _init_internal_connections(system_t *sys)
{
	ListIterator node_itr, conn_itr;
	node_t* node;
	connection_t* conn;

	node_itr = list_iterator_create(global_sys->node_list);
	while ((node = (node_t*) list_next(node_itr))) {
		
		conn_itr = list_iterator_create(global_sys->connection_list);
		while ((conn = (connection_t*) list_next(conn_itr))) {
			_join_int_connections(node, 0, 1, &conn);
			_join_int_connections(node, 3, 2, &conn);
			_join_int_connections(node, 5, 4, &conn);
		}
		list_iterator_destroy(conn_itr);

	}
	list_iterator_destroy(node_itr);
}

/** 
 * ListDelF for a List of Lists of nodes
 */
void delete_list(void* object)
{
	ListIterator itr;
	node_t* node;
	List list = (List) object;
	itr = list_iterator_create(list);
	while ((node = (node_t*) list_next(itr))) {
		delete_node(node);
	}
	list_iterator_destroy(itr);
}

/**
 * resursive implementation of solutiion to enumerate all the
 * permutations of a string
 * 
 * IN - string to enumerate
 * IN - size of string
 * OUT - results: List of strings.  This list must already be
 * initialized
 */
void _enumerate(char* enum_string, int string_size, List results)
{
	int i; 
	char *full_string = NULL;
	/* base case of recursion */
	if (string_size == 0) {
		full_string = xstrdup(enum_string);
		list_push(results, full_string);
		// printf("%s\n", enum_string);
		
	} else {
		for (i=0; i<string_size; i++){
			char tmp;
			/* swap (1, n-1) */
			SWAP(enum_string[i], enum_string[string_size-1], tmp);
			 // _swap(enum_string, i, string_size-1);
			/* recursive call on n-1 elements */
			_enumerate(enum_string, string_size-1, results);
			/* put it all back together */
			SWAP(enum_string[string_size-1], enum_string[i], tmp);
			// _swap(enum_string, string_size-1, i);
		}
	}
}

/**
 * helper swap function used by enumerate
 */
void _swap(char* a, int i, int j) 
{
	char tmp;
	tmp = a[i];
	a[i] = a[j];
	a[j] = tmp;
}

/* result_array must be initializd before passing into this function
 */
int _convert_string_to_int_array(char* string, int size, int** result_array)
{
	char zero = '0';
	int i;

	/* printf("converting string %s to array\n", string); */
	if (!result_array){
		printf("Error, result array not initialized\n");
		return ERROR;
	}

	for (i=0; i<size; i++){
		(*result_array)[i] = (int)(string[i]-zero);
	}
 	/* printf("array %d %d %d\n", (*result_array)[0], (*result_array)[1], (*result_array)[2]); */
	
	return SUCCESS;
}
 
/**
 * 
 */
void _connect_switch(switch_t* my_switch, int* plus_ports, int* minus_ports)
{
	connection_t* conn;
	ListIterator itr;
	int i;
	
	itr = list_iterator_create(my_switch->connection_list);
	for (i=0; i<INTERNAL_CONNECTIONS_PER_NODE; i++){
		conn = (connection_t*) list_next(itr);
		_join_int_connections(my_switch, plus_ports[i], minus_ports[i], &conn);
	}
	list_iterator_destroy(itr);
}

/** 
 * Shallow copy list A to List B.  List A should be preinitialized
 * (obviously) but List B will be created here and freed by caller.
 */
void list_copy(List A, List* B)
{
	ListIterator itr;
	void* object;
	
	*B = list_create(NULL);
	itr = list_iterator_create(A);
	while ((object = list_next(itr))) {	
		list_append(*B, object);
	}
	list_iterator_destroy(itr);
}

/** 
 * tests that lists point to the same object, but each hold separate
 * address references.
 */
void _test_list_copy(List A)
{
	List B;
	List C;
	list_copy(A, &B);
	list_copy(A, &C);
	node_t* node;
	ListIterator itr;

	/** DEBUGING */
	printf("copy1 nodes: (before) [ ");
	itr = list_iterator_create(B);
	while ((node = (node_t*) list_next(itr))) {
		printf("%d ", node->id);
	}
	list_iterator_destroy(itr);
	printf("]\n");

	/** do some stuff to it..*/
	list_pop(B);
	list_pop(B);

	printf("copy1 nodes: (after) [ ");
	itr = list_iterator_create(B);
	while ((node = (node_t*) list_next(itr))) {
		printf("%d ", node->id);
	}
	list_iterator_destroy(itr);
	printf("]\n");

	/** see if that affects the orig */
	printf("orig nodes: [ ");
	itr = list_iterator_create(A);
	while ((node = (node_t*) list_next(itr))) {
		printf("%d ", node->id);
	}
	list_iterator_destroy(itr);
	printf("]\n");

	/** now we try changing one, and that should change the other */
	node = (node_t*)list_pop(C);
	node->id = 5;
	list_push(C, node);

	/** DEBUGING */
	printf("copy2 nodes: [ ");
	itr = list_iterator_create(C);
	while ((node = (node_t*) list_next(itr))) {
		printf("%d ", node->id);
	}
	list_iterator_destroy(itr);
	printf("]\n");

	/** see if that affects the orig */
	printf("orig nodes: [ ");
	itr = list_iterator_create(A);
	while ((node = (node_t*) list_next(itr))) {
		printf("%d ", node->id);
	}
	list_iterator_destroy(itr);
	printf("]\n");

	list_destroy(B);
	list_destroy(C);
}

/** 
 * Comparator used for sorting connections by label
 * 
 * returns: -1: A internal && B external 0: A equal to B 1: A ext && B int
 * 
 */
int _connection_t_cmpf(connection_t* A, connection_t* B)
{
	if (A->place == B->place)
		return 0;
	if (A->place == INTERNAL)
		return -1;
	else 
		return 1;
}


/* note that we initialize all the partitions as starting
 * off as toridal connections */
void _init_local_partition(List result_partitions)
{
	partition_t *new_part, *old_part;
	ListIterator part_itr;

	if (!result_partitions){
		printf("_init_local_partition ERROR, list is unintialized\n");
		exit(0);
	}

	part_itr = list_iterator_create(global_sys->partition_list);
	while ((old_part = (partition_t*) list_next(part_itr))){
		copy_partition(old_part, &new_part);
		new_part->conn_type = TORUS;
		list_append(result_partitions, new_part);
	}
	list_iterator_destroy(part_itr);
}

char* convert_label(int label)
{
	switch (label){
	case LABEL_IGNORE:	return "LABEL_IGNORE";
	case LABEL_FOUND:	return "LABEL_FOUND";
	case LABEL_RECYCLE:	return "LABEL_RECYCLE";
	case LABEL_MERGE:	return "LABEL_MERGE";
	default:		 return "UNKNOWN LABEL???";
	}
}

int _get_connection_partition(connection_t* conn, List partition_list)
{
	partition_t* part = find_partition(partition_list, conn->id);

#ifdef DEBUG
	printf("_get_connection_partition for partition label %d\n",  conn->id);
#endif

	if (!part){
		printf("_get_connection_partition error, partition <%d> not found\n", conn->id);
		exit(0);
	}

	add_connection_to_partition(conn, part);
	return SUCCESS;
}

/** */
void create_config_9_2d(List configs)
{
	switch_config_t* conf;
	
	/** 
	 * remember that connections are bidirectional, so we only
	 * have a connection between nodes once
	 */
	/* first X row */
	new_switch_config(&conf, NO_VAL, X, 0, 3, 1, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 0, 4, 2, 3);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 1, 3, 2, 4);
	list_append(configs, conf);

	/* second X row */
	new_switch_config(&conf, NO_VAL, X, 3, 3, 4, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 3, 4, 5, 3);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 4, 3, 5, 4);
	list_append(configs, conf);

	/* third X row */
	new_switch_config(&conf, NO_VAL, X, 6, 3, 7, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 6, 4, 8, 3);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 7, 3, 8, 4);
	list_append(configs, conf);

	/* first Y column */

	new_switch_config(&conf, NO_VAL, Y, 0, 3, 3, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 0, 4, 6, 3);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 3, 3, 6, 4);
	list_append(configs, conf);

	/* second Y column */
	new_switch_config(&conf, NO_VAL, Y, 1, 3, 4, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 1, 4, 7, 3);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 4, 3, 7, 4);
	list_append(configs, conf);

	/* third Y column */
	new_switch_config(&conf, NO_VAL, Y, 2, 3, 5, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 2, 4, 8, 3);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 5, 3, 8, 4);
	list_append(configs, conf);
}

/** tested, working */
void create_config_3_1d(List configs)
{
	switch_config_t* conf;

	/* first X row */
	new_switch_config(&conf, NO_VAL, X, 0, 3, 1, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 0, 4, 2, 3);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 1, 3, 2, 4);
	list_append(configs, conf);

}

/* */
void create_config_4_2d(List configs)
{
	switch_config_t* conf;
	
	/** 
	 * remember that connections are bidirectional, so we only
	 * have a connection between nodes once
	 */
	/* first X row */
	new_switch_config(&conf, NO_VAL, X, 0, 3, 1, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 0, 4, 1, 3);
	list_append(configs, conf);

	/* second X row */
	new_switch_config(&conf, NO_VAL, X, 2, 3, 3, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 2, 4, 3, 3);
	list_append(configs, conf);


	/* first Y column */
	new_switch_config(&conf, NO_VAL, Y, 0, 3, 2, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 0, 4, 2, 3);
	list_append(configs, conf);

	/* second Y column */
	new_switch_config(&conf, NO_VAL, Y, 1, 3, 3, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 1, 4, 3, 3);
	list_append(configs, conf);
}

/* */
void create_config_4_1d(List configs)
{
	switch_config_t* conf;
	
	/** 
	 * remember that connections are bidirectional, so we only
	 * have a connection between nodes once
	 */
	/* first X row */
	new_switch_config(&conf, NO_VAL, X, 0, 3, 1, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 1, 3, 2, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 2, 3, 3, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 3, 3, 0, 4);
	list_append(configs, conf);
	printf("done create_conf\n");
}

/* */
void create_config_8_1d(List configs)
{
	switch_config_t* conf;
	
	/** 
	 * remember that connections are bidirectional, so we only
	 * have a connection between nodes once
	 */
	/* top row, horizontal connections */
	new_switch_config(&conf, NO_VAL, X, 0, 3, 2, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 2, 3, 4, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 4, 3, 6, 4);
	list_append(configs, conf);

	/* bottom row, horizontal connections */
	new_switch_config(&conf, NO_VAL, X, 1, 3, 3, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 3, 3, 5, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 5, 3, 7, 4);
	list_append(configs, conf);

	/* 1st column, vertical connections */
	new_switch_config(&conf, NO_VAL, X, 0, 2, 1, 5);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 1, 2, 0, 5);
	list_append(configs, conf);

	/* 2nd column, vertical connections */
	new_switch_config(&conf, NO_VAL, X, 2, 2, 3, 5);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 3, 2, 2, 5);
	list_append(configs, conf);

	/* 3rd column, vertical connections */
	new_switch_config(&conf, NO_VAL, X, 4, 2, 5, 5);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 5, 2, 4, 5);
	list_append(configs, conf);

	/* 4th column, vertical connections */
	new_switch_config(&conf, NO_VAL, X, 6, 2, 7, 5);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 7, 2, 6, 5);
	list_append(configs, conf);
}


/* */
void create_config_8_3d(List configs)
{
	switch_config_t* conf;
	
	/** 
	 * remember that connections are bidirectional, so we only
	 * have a connection between nodes once
	 */
	/****************************************/
	/* first X row Z0 */
	new_switch_config(&conf, NO_VAL, X, 0, 3, 1, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 0, 4, 1, 3);
	list_append(configs, conf);

	/* second X row Z0*/
	new_switch_config(&conf, NO_VAL, X, 2, 3, 3, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 2, 4, 3, 3);
	list_append(configs, conf);


	/* first X row Z1 */
	new_switch_config(&conf, NO_VAL, X, 4, 3, 5, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 4, 4, 5, 3);
	list_append(configs, conf);

	/* second X row Z1 */
	new_switch_config(&conf, NO_VAL, X, 6, 3, 7, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, X, 6, 4, 7, 3);
	list_append(configs, conf);

	/****************************************/
	/* first Y column Z0 */
	new_switch_config(&conf, NO_VAL, Y, 0, 3, 2, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 0, 4, 2, 3);
	list_append(configs, conf);

	/* second Y column Z0 */
	new_switch_config(&conf, NO_VAL, Y, 1, 3, 3, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 1, 4, 3, 3);
	list_append(configs, conf);

	/* first Y column Z1 */
	new_switch_config(&conf, NO_VAL, Y, 4, 3, 6, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 4, 4, 6, 3);
	list_append(configs, conf);

	/* second Y column Z1 */
	new_switch_config(&conf, NO_VAL, Y, 5, 3, 7, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Y, 5, 4, 7, 3);
	list_append(configs, conf);

	/****************************************/
	/*  */
	new_switch_config(&conf, NO_VAL, Z, 0, 3, 4, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Z, 0, 4, 4, 3);
	list_append(configs, conf);

	/*  */
	new_switch_config(&conf, NO_VAL, Z, 1, 3, 5, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Z, 1, 4, 5, 3);
	list_append(configs, conf);

	/*  */
	new_switch_config(&conf, NO_VAL, Z, 2, 3, 6, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Z, 2, 4, 6, 3);
	list_append(configs, conf);

	/*  */
	new_switch_config(&conf, NO_VAL, Z, 3, 3, 7, 4);
	list_append(configs, conf);
	new_switch_config(&conf, NO_VAL, Z, 3, 4, 7, 3);
	list_append(configs, conf);
}
