/*****************************************************************************\
 *  partition_allocator.h
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef _PARTITION_ALLOCATOR_H_
#define _PARTITION_ALLOCATOR_H_
/* This must be included first for AIX systems */
#include "src/common/macros.h"

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_CURSES_H
#  include <curses.h>
#endif
#if HAVE_NCURSES_H
#  include <ncurses.h>
#endif

#include "src/api/node_select_info.h"
#include "src/common/read_config.h"
#include "src/common/parse_spec.h"
#include "src/slurmctld/proc_req.h"
#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/bitstring.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include <dlfcn.h>

// #define DEBUG_PA
#define BIG_MAX 9999
#define BUFSIZE 4096

#define NUM_PORTS_PER_NODE 6

#ifdef HAVE_BGL
#define PA_SYSTEM_DIMENSIONS 3
#else
#define PA_SYSTEM_DIMENSIONS 1
#endif

extern bool _initialized;
extern bool have_db2;

enum {X, Y, Z};

/* */
enum {MESH, TORUS};
enum {COPROCESSOR, VIRTUAL};

/* NOTE: Definition of bgl_info_record_t moved to src/api/node_select_info.h */

extern List bgl_info_list;			/* List of BGL blocks */

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
	int geometry[PA_SYSTEM_DIMENSIONS];
	int dim;
	int in; 
	int out;
} pa_path_switch_t; 

/** 
 * structure that holds the configuration settings for each request
 * 
 * - letter            - filled in after the request is fulfilled
 * - geometry          - request size
 * - size              - node count for request
 * - conn_type         - MESH or TORUS
 * - rotate_count      - when rotating we keep a count so we aren't in an infinate loop.
 * - elongate_count    - when elongating we keep a count so we aren't in an infinate loop.
 * - rotate            - weather to allow rotating or not.
 * - elongate          - weather to allow elongating or not.
 * - force_contig      - weather to allow force contiguous or not.
 * 
 */
typedef struct {
	char *save_name;
	int geometry[PA_SYSTEM_DIMENSIONS];
	int size; 
	int conn_type;
	int rotate_count;
	int elongate_count;
	bool rotate;
	bool elongate; 
	bool force_contig;
	List elongate_geos;
} pa_request_t; 

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
	int node_tar[PA_SYSTEM_DIMENSIONS];
	bool used;	
} pa_connection_t;
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
	pa_connection_t int_wire[NUM_PORTS_PER_NODE];
	pa_connection_t ext_wire[NUM_PORTS_PER_NODE];

} pa_switch_t;

/*
 * pa_node_t: node within the allocation system.
 */
typedef struct {
	/* set if using this node in a partition*/
	bool used;

	/* coordinates */
	int coord[PA_SYSTEM_DIMENSIONS];
	pa_switch_t axis_switch[PA_SYSTEM_DIMENSIONS];
	char letter;
	int color;
	int indecies;
	int state;
	int conn_type;
	int phys_x;
	
} pa_node_t;

typedef struct {
	int xcord;
	int ycord;
	int num_of_proc;
	int resize_screen;

	WINDOW *grid_win;
	WINDOW *text_win;

	time_t now_time;

	/* made to hold info about a system, which right now is only a grid of pa_nodes*/
#ifdef HAVE_BGL
	pa_node_t ***grid;
#else
	pa_node_t *grid;
#endif
} pa_system_t;

/* Used to Keep track of where the Base Partitions are at all times
   Rack and Midplane is the bp_id and XYZ is the coords.
*/

typedef struct {
	char *bp_id;
	int coord[PA_SYSTEM_DIMENSIONS];	
} pa_bp_map_t;

/* Global */
extern List bp_map_list;
extern char letters[62];
extern char colors[6];
extern int DIM_SIZE[PA_SYSTEM_DIMENSIONS];

/* destroy a bgl_info_record_t */
extern void destroy_bgl_info_record(void* object);

/**
 * create a partition request.  Note that if the geometry is given,
 * then size is ignored.  If elongate is true, the algorithm will try
 * to fit that a partition of cubic shape and then it will try other
 * elongated geometries.  (ie, 2x2x2 -> 4x2x1 -> 8x1x1). Note that
 * size must be a power of 2, given 3 dimensions.
 * 
 * OUT - pa_request: structure to allocate and fill in.  
 * IN - geometry: requested geometry of partition
 * IN - size: requested size of partition
 * IN - rotate: if true, allows rotation of partition during fit
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * IN - contig: enforce contiguous regions constraint
 * IN - conn_type: connection type of request (TORUS or MESH)
 * 
 * return success of allocation/validation of params
 */
extern int new_pa_request(pa_request_t* pa_request);

/**
 * delete a partition request 
 */
extern void delete_pa_request(pa_request_t* pa_request);

/**
 * print a partition request 
 */
extern void print_pa_request(pa_request_t* pa_request);

/**
 * Initialize internal structures by either reading previous partition
 * configurations from a file or by running the graph solver.
 * 
 * IN: dunno yet, probably some stuff denoting downed nodes, etc.
 * 
 * return: success or error of the intialization.
 */
extern void pa_init();
/**
 */
extern void init_wires();
/** 
 * destroy all the internal (global) data structs.
 */
extern void pa_fini();

/** 
 * set the node in the internal configuration as unusable
 * 
 * IN c: coordinate of the node to put down
 */
extern void pa_set_node_down(pa_node_t *pa_node);

/** 
 * Try to allocate a partition.
 * 
 * IN - pa_request: allocation request
 * OUT - results: List of results of the allocation request.  Each
 * list entry will be a coordinate.  allocate_part will create the
 * list, but the caller must destroy it.
 * 
 * return: success or error of request
 */
extern int allocate_part(pa_request_t* pa_request, List results);

/** 
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int remove_part(List nodes, int new_count);

/** 
 * Admin wants to change something about a previous allocation. 
 * will allow Admin to change previous allocation by giving the 
 * letter code for the allocation and the variable to alter
 *
 */
extern int alter_part(List nodes, int conn_type);

/** 
 * After a partition is deleted or altered following allocations must
 * be redone to make sure correct path will be used in the real system
 *
 */
extern int redo_part(List nodes, int *geo, int conn_type, int new_count);

extern char *set_bgl_part(List results, int *start, 
			  int *geometry, int conn_type);

extern int reset_pa_system();

extern void init_grid(node_info_msg_t *node_info_ptr);

/**
 * Set up the map for resolving
 */
extern int set_bp_map(void);

/**
 * find a base partitions bgl location 
 */
extern int *find_bp_loc(char* bp_id);

/**
 * find a rack/midplace location 
 */
extern char *find_bp_rack_mid(char* xyz);

#endif /* _PARTITION_ALLOCATOR_H_ */
