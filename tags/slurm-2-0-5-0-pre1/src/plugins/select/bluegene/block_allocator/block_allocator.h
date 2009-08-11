/*****************************************************************************\
 *  block_allocator.h
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#ifndef _BLOCK_ALLOCATOR_H_
#define _BLOCK_ALLOCATOR_H_

#include "bridge_linker.h"

// #define DEBUG_PA
#define BIG_MAX 9999
#define BUFSIZE 4096

#define NUM_PORTS_PER_NODE 6

#ifdef HAVE_3D
#define BA_SYSTEM_DIMENSIONS 3
#else
#define BA_SYSTEM_DIMENSIONS 1
#endif

#define PASS_DENY_X 0x0001
#define PASS_DENY_Y 0x0002
#define PASS_DENY_Z 0x0004
#define PASS_DENY_ALL 0x00ff

#define PASS_FOUND_X 0x0100
#define PASS_FOUND_Y 0x0200
#define PASS_FOUND_Z 0x0400
#define PASS_FOUND_ANY 0xff00

extern bool _initialized;

enum {X, Y, Z};

/* */

/* 
 * structure that holds switch path information for finding the wiring 
 * path without setting the configuration.
 *
 * - dim      - Which Axis it is on
 * - geometry - node location
 * - in       - ingress port.
 * - out      - egress port.
 * 
 */
typedef struct {
	int dim;
	int geometry[BA_SYSTEM_DIMENSIONS];
	int in; 
	int out;
} ba_path_switch_t; 

/* 
 * structure that holds the configuration settings for each request
 */
typedef struct {
	bitstr_t *avail_node_bitmap;   /* pointer to available nodes */	
#ifdef HAVE_BGL
	char *blrtsimage;              /* BlrtsImage for this block */
#endif
	int conn_type;                 /* mesh, torus, or small */
	bool elongate;                 /* whether allow elongation or not */
	int elongate_count;            /* place in elongate_geos list
					  we are at */
	List elongate_geos;            /* list of possible shapes of
					  blocks. contains int* ptrs */
	int geometry[BA_SYSTEM_DIMENSIONS]; /* size of block in geometry */
	char *linuximage;              /* LinuxImage for this block */
	char *mloaderimage;            /* mloaderImage for this block */
	uint16_t deny_pass;            /* PASSTHROUGH_FOUND is set if there are
					  passthroughs in the block
					  created you can deny
					  passthroughs by setting the
					  appropriate bits*/
	int procs;                     /* Number of Real processors in
					  block */
	char *ramdiskimage;            /* RamDiskImage for this block */
	bool rotate;                   /* whether allow elongation or not */
	int rotate_count;              /* number of times rotated */
	char *save_name;               /* name of blocks in midplanes */
	int size;                      /* count of midplanes in block */
	int small32;                   /* number of blocks using 32 cnodes in
					* block, only used for small
					* block creation */
	int small128;                  /* number of blocks using 128 cnodes in
					* block, only used for small
					* block creation */
#ifndef HAVE_BGL
	int small16;                   /* number of blocks using 16 cnodes in
					* block, only used for small
					* block creation */
	int small64;                   /* number of blocks using 64 cnodes in
					* block, only used for small
					* block creation */
	int small256;                  /* number of blocks using 256 cnodes in
					* block, only used for small
					* block creation */
#endif
	int start[BA_SYSTEM_DIMENSIONS]; /* where to start creation of
					    block */
	int start_req;                 /* state there was a start
					  request */
} ba_request_t; 

/* structure filled in from reading bluegene.conf file for block
 * creation */
typedef struct {
	char *block;                   /* Hostlist of midplanes in the
					  block */
	int conn_type;                 /* mesh, torus, or small */
#ifdef HAVE_BGL
	char *blrtsimage;              /* BlrtsImage for this block */
#endif
	char *linuximage;              /* LinuxImage for this block */
	char *mloaderimage;            /* mloaderImage for this block */
	char *ramdiskimage;            /* RamDiskImage for this block */
	uint16_t small32;                   /* number of blocks using 32 cnodes in
					* block, only used for small
					* block creation */
	uint16_t small128;             /* number of blocks using 128 cnodes in
					* block, only used for small
					* block creation */
#ifndef HAVE_BGL
	uint16_t small16;              /* number of blocks using 16 cnodes in
					* block, only used for small
					* block creation */
	uint16_t small64;                   /* number of blocks using 64 cnodes in
					* block, only used for small
					* block creation */
	uint16_t small256;             /* number of blocks using 256 cnodes in
					* block, only used for small
					* block creation */
#endif
} blockreq_t;

/* structure filled in from reading bluegene.conf file for specifing
 * images */
typedef struct {
	bool def;                      /* Whether image is the default
					  image or not */
	List groups;                   /* list of groups able to use
					* the image contains
					* image_group_t's */
	char *name;                    /* Name of image */
} image_t;

typedef struct {
	char *name;
	gid_t gid;
} image_group_t;

/* 
 * structure that holds the configuration settings for each connection
 * 
 * - node_tar - coords of where the next hop is externally
 *              interanlly - nothing.
 *              exteranlly - location of next hop.
 * - port_tar - which port the connection is going to
 *              interanlly - always going to something within the switch.
 *              exteranlly - always going to the next hop outside the switch.
 * - used     - weather or not the connection is used.
 * 
 */
typedef struct 
{
	/* target label */
	int node_tar[BA_SYSTEM_DIMENSIONS];
	/* target port */ 
	int port_tar;
	bool used;	
} ba_connection_t;

/* 
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
	/* a switch for each dimensions */
	ba_switch_t axis_switch[BA_SYSTEM_DIMENSIONS]; 
	/* coordinates of midplane */
	int coord[BA_SYSTEM_DIMENSIONS];
	/* color of letter used in smap */
	int color;
	/* midplane index used for easy look up of the miplane */
	int index;
	/* letter used in smap */
	char letter;                    
//	int phys_x;	// no longer needed 
	int state;
	/* set if using this midplane in a block */
	uint16_t used;
} ba_node_t;

typedef struct {
	/* total number of procs on the system */
	int num_of_proc;

	/* made to hold info about a system, which right now is only a
	 * grid of ba_nodes*/
#ifdef HAVE_3D
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
extern my_bluegene_t *bg;
extern List bp_map_list; /* list used for conversion from XYZ to Rack
			  * midplane */
extern char letters[62]; /* complete list of letters used in smap */
extern char colors[6]; /* index into colors used for smap */
extern int DIM_SIZE[BA_SYSTEM_DIMENSIONS]; /* how many midplanes in
					    * each dimension */
extern s_p_options_t bg_conf_file_options[]; /* used to parse the
					      * bluegene.conf file. */
extern uint16_t ba_deny_pass;
extern ba_system_t *ba_system_ptr;

/* Translate a state enum to a readable string */
extern char *bg_block_state_string(rm_partition_state_t state);

/* must xfree return of this */
extern char *ba_passthroughs_string(uint16_t passthrough);

/* Parse a block request from the bluegene.conf file */
extern int parse_blockreq(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value, 
			  const char *line, char **leftover);

extern void destroy_blockreq(void *ptr);

/* Parse imagine information from blugene.conf file */
extern int parse_image(void **dest, slurm_parser_enum_t type,
		       const char *key, const char *value, 
		       const char *line, char **leftover);

extern void destroy_image_group_list(void *ptr);
extern void destroy_image(void *ptr);
extern void destroy_ba_node(void *ptr);

/*
 * create a block request.  Note that if the geometry is given,
 * then size is ignored.  If elongate is true, the algorithm will try
 * to fit that a block of cubic shape and then it will try other
 * elongated geometries.  (ie, 2x2x2 -> 4x2x1 -> 8x1x1). 
 * 
 * IN/OUT - ba_request: structure to allocate and fill in.  
 * 
 * ALL below IN's need to be set within the ba_request before the call
 * if you want them to be used.
 * ALL below OUT's are set and returned within the ba_request.
 * IN - avail_node_bitmap: bitmap of usable midplanes.
 * IN - blrtsimage: BlrtsImage for this block if not default
 * IN - conn_type: connection type of request (TORUS or MESH or SMALL)
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * IN/OUT - geometry: requested/returned geometry of block
 * IN - linuximage: LinuxImage for this block if not default
 * IN - mloaderimage: MLoaderImage for this block if not default
 * OUT - passthroughs: if there were passthroughs used in the
 *       generation of the block.
 * IN - procs: Number of real processors requested
 * IN - RamDiskimage: RamDiskImage for this block if not default
 * IN - rotate: if true, allows rotation of block during fit
 * OUT - save_name: hostlist of midplanes used in block
 * IN/OUT - size: requested/returned count of midplanes in block
 * IN - start: geo location of where to start the allocation
 * IN - start_req: if set use the start variable to start at
 * return success of allocation/validation of params
 */
extern int new_ba_request(ba_request_t* ba_request);

/*
 * delete a block request 
 */
extern void delete_ba_request(void *arg);

/*
 * empty a list that we don't want to destroy the memory of the
 * elements always returns 1
*/
extern int empty_null_destroy_list(void *arg, void *key);

/*
 * print a block request 
 */
extern void print_ba_request(ba_request_t* ba_request);

/*
 * Initialize internal structures by either reading previous block
 * configurations from a file or by running the graph solver.
 * 
 * IN: node_info_msg_t * can be null, 
 *     should be from slurm_load_node().
 * 
 * return: void.
 */
extern void ba_init(node_info_msg_t *node_info_ptr);

/* If emulating a system set up a known configuration for wires in a
 * system of the size given.
 * If a real bluegene system, query the system and get all wiring
 * information of the system.
 */
extern void init_wires();

/* 
 * destroy all the internal (global) data structs.
 */
extern void ba_fini();

/* 
 * set the node in the internal configuration as in, or not in use,
 * along with the current state of the node.
 * 
 * IN ba_node: ba_node_t to update state
 * IN state: new state of ba_node_t
 */
extern void ba_update_node_state(ba_node_t *ba_node, uint16_t state);

/* 
 * copy info from a ba_node, a direct memcpy of the ba_node_t
 * 
 * IN ba_node: node to be copied
 * Returned ba_node_t *: copied info must be freed with destroy_ba_node
 */
extern ba_node_t *ba_copy_node(ba_node_t *ba_node);

/* 
 * copy the path of the nodes given
 * 
 * IN nodes List of ba_node_t *'s: nodes to be copied
 * OUT dest_nodes List of ba_node_t *'s: filled in list of nodes
 * wiring.
 * Return on success SLURM_SUCCESS, on error SLURM_ERROR
 */
extern int copy_node_path(List nodes, List *dest_nodes);

/* 
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

/* 
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int remove_block(List nodes, int new_count);

/* 
 * Admin wants to change something about a previous allocation. 
 * will allow Admin to change previous allocation by giving the 
 * letter code for the allocation and the variable to alter
 * (Not currently used in the system, update this if it is)
 */
extern int alter_block(List nodes, int conn_type);

/* 
 * After a block is deleted or altered following allocations must
 * be redone to make sure correct path will be used in the real system
 * (Not currently used in the system, update this if it is)
 */
extern int redo_block(List nodes, int *geo, int conn_type, int new_count);

/*
 * Used to set a block into a virtual system.  The system can be
 * cleared first and this function sets all the wires and midplanes
 * used in the nodelist given.  The nodelist is a list of ba_node_t's
 * that are already set up.  This is very handly to test if there are
 * any passthroughs used by one block when adding another block that
 * also uses those wires, and neither use any overlapping
 * midplanes. Doing a simple bitmap & will not reveal this.
 *
 * Returns SLURM_SUCCESS if nodelist fits into system without
 * conflict, and SLURM_ERROR if nodelist conflicts with something
 * already in the system.
 */
extern int check_and_set_node_list(List nodes);

/*
 * Used to find, and set up midplanes and the wires in the virtual
 * system and return them in List results 
 * 
 * IN/OUT results - a list with a NULL destroyer filled in with
 *        midplanes and wires set to create the block with the api. If
 *        only interested in the hostlist NULL can be excepted also.
 * IN start - where to start the allocation.
 * IN geometry - the requested geometry of the block.
 * IN conn_type - mesh, torus, or small.
 * RET char * - hostlist of midplanes results represent must be
 *     xfreed.  NULL on failure
 */
extern char *set_bg_block(List results, int *start, 
			  int *geometry, int conn_type);

/*
 * Resets the virtual system to a virgin state.  If track_down_nodes is set
 * then those midplanes are not set to idle, but kept in a down state.
 */
extern int reset_ba_system(bool track_down_nodes);

/*
 * Used to set all midplanes in a special used state except the ones
 * we are able to use in a new allocation.
 *
 * IN: hostlist of midplanes we do not want
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Note: Need to call reset_all_removed_bps before starting another
 * allocation attempt after 
 */
extern int removable_set_bps(char *bps);

/*
 * Resets the virtual system to the pervious state before calling
 * removable_set_bps, or set_all_bps_except.
 */
extern int reset_all_removed_bps();

/*
 * IN: hostlist of midplanes we do not want
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Need to call rest_all_removed_bps before starting another
 * allocation attempt.  If possible use removable_set_bps since it is
 * faster. It does basically the opposite of this function. If you
 * have to come up with this list though it is faster to use this
 * function than if you have to call bitmap2node_name since that is slow.
 */
extern int set_all_bps_except(char *bps);

/*
 * set values of every grid point (used in smap)
 */
extern void init_grid(node_info_msg_t *node_info_ptr);

/*
 * Convert a BG API error code to a string
 * IN inx - error code from any of the BG Bridge APIs
 * RET - string describing the error condition
 */
extern char *bg_err_str(status_t inx);

/*
 * Set up the map for resolving
 */
extern int set_bp_map(void);

/*
 * find a base blocks bg location based on Rack Midplane name R000 not R00-M0
 */
extern int *find_bp_loc(char* bp_id);

/*
 * find a rack/midplace location based on XYZ coords
 */
extern char *find_bp_rack_mid(char* xyz);

/*
 * set the used wires in the virtual system for a block from the real system 
 */
extern int load_block_wiring(char *bg_block_id);

/*
 * get the used wires for a block out of the database and return the
 * node list
 */
extern List get_and_set_block_wiring(char *bg_block_id);

/* make sure a node is in the system return 1 if it is 0 if not */
extern int validate_coord(int *coord);


#endif /* _BLOCK_ALLOCATOR_H_ */
