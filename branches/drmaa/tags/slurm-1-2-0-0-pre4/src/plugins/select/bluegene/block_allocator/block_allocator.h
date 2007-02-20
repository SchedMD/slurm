/*****************************************************************************\
 *  block_allocator.h
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#ifndef _BLOCK_ALLOCATOR_H_
#define _BLOCK_ALLOCATOR_H_

#include "bridge_linker.h"

// #define DEBUG_PA
#define BIG_MAX 9999
#define BUFSIZE 4096

#define NUM_PORTS_PER_NODE 6

#ifdef HAVE_BG
#define BA_SYSTEM_DIMENSIONS 3
#else
#define BA_SYSTEM_DIMENSIONS 1
#endif

extern bool _initialized;

enum {X, Y, Z};

/* */

/** 
 * structure that holds switch path information for finding the wiring 
 * path without setting the configuration.
 *
 * - geometry - node location
 * - dim      - Which Axis it is on
 * - in       - ingress port.
 * - out      - egress port.
 * 
 */
typedef struct {
	int geometry[BA_SYSTEM_DIMENSIONS];
	int dim;
	int in; 
	int out;
} ba_path_switch_t; 

/** 
 * structure that holds the configuration settings for each request
 * 
 * - letter            - filled in after the request is fulfilled
 * - geometry          - request size
 * - size              - node count for request
 * - conn_type         - MESH or TORUS or SMALL
 * - rotate_count      - when rotating we keep a count so we aren't in an infinate loop.
 * - elongate_count    - when elongating we keep a count so we aren't in an infinate loop.
 * - rotate            - weather to allow rotating or not.
 * - elongate          - weather to allow elongating or not.
 * - force_contig      - weather to allow force contiguous or not.
 * 
 */
typedef struct {
	char *save_name;
	int geometry[BA_SYSTEM_DIMENSIONS];
	int start[BA_SYSTEM_DIMENSIONS];
	int start_req;
	int size; 
	int procs; 
	int conn_type;
	int rotate_count;
	int elongate_count;
	int nodecards;
	int quarters;
	bool passthrough;
	bool rotate;
	bool elongate; 
	List elongate_geos;
} ba_request_t; 

typedef struct blockreq {
	char *block;
	int conn_type;
	uint16_t quarters;
	uint16_t nodecards;
} blockreq_t;

/** 
 * structure that holds the configuration settings for each connection
 * 
 * - port_tar - which port the connection is going to
 *              interanlly - always going to something within the switch.
 *              exteranlly - always going to the next hop outside the switch.
 * - node_tar - coords of where the next hop is externally
 *              interanlly - nothing.
 *              exteranlly - location of next hop.
 * - used     - weather or not the connection is used.
 * 
 */
typedef struct 
{
	/* target port */ 
	int port_tar;

	/* target label */
	int node_tar[BA_SYSTEM_DIMENSIONS];
	bool used;	
} ba_connection_t;
/** 
 * structure that holds the configuration settings for each switch
 * which pretty much means the wiring information 
 * - int_wire - keeps details of where the wires are attached
 *   interanlly.
 * - ext_wire - keeps details of where the wires are attached
 *   exteranlly.
 * 
 */
typedef struct
{
	ba_connection_t int_wire[NUM_PORTS_PER_NODE];
	ba_connection_t ext_wire[NUM_PORTS_PER_NODE];
} ba_switch_t;

/*
 * ba_node_t: node within the allocation system.
 */
typedef struct {
	/* set if using this node in a block*/
	bool used;

	/* coordinates */
	int coord[BA_SYSTEM_DIMENSIONS];
	ba_switch_t axis_switch[BA_SYSTEM_DIMENSIONS];
	char letter;
	int color;
	int indecies;
	int state;
	int conn_type;
	int phys_x;	
} ba_node_t;

typedef struct {
	int xcord;
	int ycord;
	int num_of_proc;
	int resize_screen;

#ifdef HAVE_CURSES_H
 	WINDOW *grid_win;
	WINDOW *text_win;
#endif
	time_t now_time;

	/* made to hold info about a system, which right now is only a grid of ba_nodes*/
#ifdef HAVE_BG
	ba_node_t ***grid;
#else
	ba_node_t *grid;
#endif
} ba_system_t;

/* Used to Keep track of where the Base Blocks are at all times
   Rack and Midplane is the bp_id and XYZ is the coords.
*/

typedef struct {
	char *bp_id;
	int coord[BA_SYSTEM_DIMENSIONS];	
} ba_bp_map_t;

/* Global */
extern List bp_map_list;
extern char letters[62];
extern char colors[6];
extern int DIM_SIZE[BA_SYSTEM_DIMENSIONS];
extern s_p_options_t bg_conf_file_options[];

extern int parse_blockreq(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value, 
			  const char *line, char **leftover);

extern void destroy_blockreq(void *ptr);
extern void destroy_ba_node(void *ptr);

/**
 * create a block request.  Note that if the geometry is given,
 * then size is ignored.  If elongate is true, the algorithm will try
 * to fit that a block of cubic shape and then it will try other
 * elongated geometries.  (ie, 2x2x2 -> 4x2x1 -> 8x1x1). Note that
 * size must be a power of 2, given 3 dimensions.
 * 
 * OUT - ba_request: structure to allocate and fill in.  
 * IN - geometry: requested geometry of block
 * IN - size: requested size of block
 * IN - rotate: if true, allows rotation of block during fit
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * IN - contig: enforce contiguous regions constraint
 * IN - conn_type: connection type of request (TORUS or MESH or SMALL)
 * 
 * return success of allocation/validation of params
 */
extern int new_ba_request(ba_request_t* ba_request);

/**
 * delete a block request 
 */
extern void delete_ba_request(void *arg);

/**
 * print a block request 
 */
extern void print_ba_request(ba_request_t* ba_request);

/**
 * Initialize internal structures by either reading previous block
 * configurations from a file or by running the graph solver.
 * 
 * IN: dunno yet, probably some stuff denoting downed nodes, etc.
 * 
 * return: success or error of the intialization.
 */
extern void ba_init();
/**
 */
extern void init_wires();
/** 
 * destroy all the internal (global) data structs.
 */
extern void ba_fini();

/** 
 * set the node in the internal configuration as unusable
 * 
 * IN c: coordinate of the node to put down
 */
extern void ba_set_node_down(ba_node_t *ba_node);

/** 
 * copy info from a ba_node
 * 
 * IN ba_node: node to be copied
 * OUT ba_node_t *: copied info must be freed with destroy_ba_node
 */
extern ba_node_t *ba_copy_node(ba_node_t *ba_node);

/** 
 * Try to allocate a block.
 * 
 * IN - ba_request: allocation request
 * OUT - results: List of results of the allocation request.  Each
 * list entry will be a coordinate.  allocate_block will create the
 * list, but the caller must destroy it.
 * 
 * return: success or error of request
 */
extern int allocate_block(ba_request_t* ba_request, List results);

/** 
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int remove_block(List nodes, int new_count);

/** 
 * Admin wants to change something about a previous allocation. 
 * will allow Admin to change previous allocation by giving the 
 * letter code for the allocation and the variable to alter
 *
 */
extern int alter_block(List nodes, int conn_type);

/** 
 * After a block is deleted or altered following allocations must
 * be redone to make sure correct path will be used in the real system
 *
 */
extern int redo_block(List nodes, int *geo, int conn_type, int new_count);

extern void set_node_list(List nodes);

extern int check_and_set_node_list(List nodes);

extern char *set_bg_block(List results, int *start, 
			  int *geometry, int conn_type);

extern int reset_ba_system();

extern void init_grid(node_info_msg_t *node_info_ptr);
/*
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
extern char *bg_err_str(status_t inx);

/**
 * Set up the map for resolving
 */
extern int set_bp_map(void);

/**
 * find a base blocks bg location 
 */
extern int *find_bp_loc(char* bp_id);

/**
 * find a rack/midplace location 
 */
extern char *find_bp_rack_mid(char* xyz);

/**
 * set the used wires for a block out of the database 
 */
extern int load_block_wiring(char *bg_block_id);

#endif /* _BLOCK_ALLOCATOR_H_ */
