/*****************************************************************************\
 *  graph_solver.h
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

#ifndef _GRAPH_SOLVER_H_
#define _GRAPH_SOLVER_H_

#include "graph_structs.h"

#define LARGE_PART 2
#define NUM_NODES 3
#define NUM_INTERNAL_CONNECTIONS 3 /* per node */
#define NUM_EXTERNAL_CONNECTIONS 3
#define WHITE -2
#define NUM_MINUS_PORTS 3
#define NUM_PLUS_PORTS 3

/** 
 * conf_data holds a port configuration's emergent information, namely
 * the number of partitions, the partition sizes, the partition types
 * and the nodes' relative number in the configuration 
 * 
 */
typedef struct conf_data {
	/** number of partitions in this configuration */
	int num_partitions;
	/** partition sizes */
	int* partition_sizes;
	/** partition types corresponding to the partition sizes (by index) */
	conn_type_t* partition_type;
	/* the node id's corresponding to each of the partition sizes.
	 * so if there is a conf_data with partition_sizes: 2 1 1, then
	 * node_id might have something like [ [0 1] [2] [3] ]
	 */
	int** node_id;
} conf_data_t;

/** 
 * a port configuration where the plus ports match up to the minus
 * ports by index.  e.g. for plus_ports = 035 and minus ports 241, the
 * connections will be matched up 02, 34, 51
 */
typedef struct port_conf {
	int* plus_ports;
	int* minus_ports;
} port_conf_t;

/** 
 * conf_result holds the results of the graph solver search for each
 * configuration instance.  So for example, for a 3x1 system, a
 * conf_result will hold in port_conf_list the port configurations
 * (e.g. 421 412 142) and the conf_data that corresponds to that
 * configuration
 */
typedef struct conf_result {
	List port_conf_list;
	conf_data_t* conf_data;
} conf_result_t;

/** create a conf_result_t */
void new_conf_result(conf_result_t** conf_result, conf_data_t* conf_data);
/** delete a conf_result_t */
void delete_conf_result(void* object);
/** print out a conf_result */
void print_conf_result(conf_result_t* conf_result);
/** create a conf_data_t */
void new_conf_data(conf_data_t** conf_data, int num_partitions);
/** delete a conf_data_t */
void delete_conf_data(void* object);
/** create a port_conf_t */
void new_port_conf(port_conf_t** port_conf, int* plus_ports, int* minus_ports);
/** delete a port_conf_t */
void delete_port_conf(void* object);
/* print out a port conf*/
void print_port_conf(port_conf_t* port_conf);
/* print out a port conf list*/
void print_port_conf_list(List current_configs);

/** */
void list_copy(List A, List* B);
/** */
void delete_list(void* object);
/** */
int find_all_tori(List part_config_list);

/** */
void print_system(system_t* sys);
/** */
int init_system(List port_config_list, int num_nodes);

/** 3x3x1 */
void create_config_9_2d(List switch_config_list);
/** 2x2x2 */
void create_config_8_3d(List switch_config_list);
/** 2x2x2 */
void create_config_8_1d(List switch_config_list);
/** 2x2x1 */
void create_config_4_2d(List switch_config_list);
/** 4x1x1 */
void create_config_4_1d(List switch_config_list);
/** 3x1x1 */
void create_config_3_1d(List switch_config_list);

#endif /* _GRAPH_SOLVER_H_ */


