/*****************************************************************************\
 *  ba_common.h
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#ifndef _BLOCK_ALLOCATOR_COMMON_H_
#define _BLOCK_ALLOCATOR_COMMON_H_

#include "src/common/node_select.h"
#include "bridge_linker.h"

#define BIG_MAX 9999
#define BUFSIZE 4096

#define SWAP(a,b,t)				\
	_STMT_START {				\
		(t) = (a);			\
		(a) = (b);			\
		(b) = (t);			\
	} _STMT_END

/* This is only used on L and P hense the 6 count */
#define NUM_PORTS_PER_NODE 6

extern int DIM_SIZE[HIGHEST_DIMENSIONS]; /* how many midplanes in
					  * each dimension */

#define PASS_DENY_A    0x0001
#define PASS_DENY_X    0x0002
#define PASS_DENY_Y    0x0004
#define PASS_DENY_Z    0x0008
#define PASS_DENY_ALL  0x00ff

#define PASS_FOUND_A   0x0100
#define PASS_FOUND_X   0x0200
#define PASS_FOUND_Y   0x0400
#define PASS_FOUND_Z   0x0800
#define PASS_FOUND_ANY 0xff00

#define BA_MP_USED_FALSE          0x0000
#define BA_MP_USED_TRUE           0x0001
#define BA_MP_USED_TEMP           0x0002
#define BA_MP_USED_ALTERED        0x0100
#define BA_MP_USED_PASS_BIT       0x1000
#define BA_MP_USED_ALTERED_PASS   0x1100 // This should overlap
					 // BA_MP_USED_ALTERED and
					 // BA_MP_USED_PASS_BIT

/* This data structure records all possible combinations of bits which can be
 * set in a bitmap of a specified size. Each bit is equivalent to another and
 * there is no consideration of wiring. Increase LONGEST_BGQ_DIM_LEN as needed
 * to support larger systems. */
#ifndef LONGEST_BGQ_DIM_LEN
#define LONGEST_BGQ_DIM_LEN 8
#endif

typedef struct ba_geo_table {
	uint16_t size;			/* Total object count */
	uint16_t *geometry;		/* Size in each dimension */
	uint16_t full_dim_cnt;		/* Fully occupied dimension count */
	uint16_t passthru_cnt;		/* Count of nodes lost for passthru */
	struct ba_geo_table *next_ptr;	/* Next geometry of this size */
} ba_geo_table_t;

typedef struct {
	uint16_t dim_count;		/* Number of system dimensions */
	int *dim_size;	        	/* System size in each dimension */
	uint32_t total_size;		/* Total number of nodes in system */

	ba_geo_table_t **geo_table_ptr;	/* Pointers to possible geometries.
					 * Index is request size */
	uint16_t geo_table_size;	/* Number of ba_geo_table_t records */
} ba_geo_system_t;

/*
 * structure that holds the configuration settings for each connection
 *
 * - mp_tar - coords of where the next hop is externally
 *              interanlly - nothing.
 *              exteranlly - location of next hop.
 * - port_tar - which port the connection is going to
 *              interanlly - always going to something within the switch.
 *              exteranlly - always going to the next hop outside the switch.
 * - used     - weather or not the connection is used.
 *
 */
typedef struct {
	/* target label */
	uint16_t mp_tar[HIGHEST_DIMENSIONS];
	/* target port */
	uint16_t port_tar;
	uint16_t used;
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
typedef struct {
	ba_connection_t int_wire[NUM_PORTS_PER_NODE];
	ba_connection_t ext_wire[NUM_PORTS_PER_NODE];
	uint16_t usage;
} ba_switch_t;

/*
 * ba_mp_t: mp within the allocation system.
 */
typedef struct block_allocator_mp {
	/* altered wires in the switch */
	ba_switch_t alter_switch[HIGHEST_DIMENSIONS];
	/* a switch for each dimensions */
	ba_switch_t axis_switch[HIGHEST_DIMENSIONS];
	/* index into the ba_main_grid_array (BGQ) used for easy look
	 * up of the miplane in that system */
	uint32_t ba_geo_index;
	/* Bitmap of available cnodes */
	bitstr_t *cnode_bitmap;
	/* Bitmap of available cnodes in error (usually software) */
	bitstr_t *cnode_err_bitmap;
	/* Bitmap of available cnodes in the containing block */
	bitstr_t *cnode_usable_bitmap;
	/* coordinates of midplane */
	uint16_t coord[HIGHEST_DIMENSIONS];
	/* coordinates of midplane in str format */
	char coord_str[HIGHEST_DIMENSIONS+1];
	/* index into the node_record_table_ptr used for easy look up
	 * of the miplane in that system */
	uint32_t index;
	/* rack-midplane location. */
	char *loc;
	struct block_allocator_mp *next_mp[HIGHEST_DIMENSIONS];
	char **nodecard_loc;
	struct block_allocator_mp *prev_mp[HIGHEST_DIMENSIONS];
	int state;
	/* set if using this midplane in a block */
	uint16_t used;
} ba_mp_t;

typedef struct {
	int elem_count;			/* length of arrays set_count_array
					 * and set_bits_array */
	int *gap_count;			/* number of gaps in this array */
	bool *has_wrap;			/* true if uses torus to wrap alloc,
					 * implies gap_count <= 1 */
	int *set_count_array;		/* number of set bits in this array */
	bitstr_t **set_bits_array;	/* bitmap rows to use */
	uint16_t *start_coord;		/* array of lowest coord in block */
	uint16_t *block_size;		/* dimension size in block */
} ba_geo_combos_t;

extern ba_geo_combos_t geo_combos[LONGEST_BGQ_DIM_LEN];

extern uint16_t ba_deny_pass;
extern int cluster_dims;
extern uint32_t cluster_flags;
extern int cluster_base;
extern bool ba_initialized;
extern uint64_t ba_debug_flags;
extern bitstr_t *ba_main_mp_bitmap;
extern pthread_mutex_t ba_system_mutex;

/*
 * Initialize internal structures by either reading previous block
 * configurations from a file or by running the graph solver.
 *
 * IN: node_info_msg_t * can be null,
 *     should be from slurm_load_node().
 * IN: load_bridge: whiether or not to get bridge information
 *
 * return: void.
 */
extern void ba_init(node_info_msg_t *node_info_ptr, bool load_bridge);

/*
 * destroy all the internal (global) data structs.
 */
extern void ba_fini(void);

/* setup the wires for the system */
extern void ba_setup_wires(void);

extern void free_internal_ba_mp(ba_mp_t *ba_mp);
extern void destroy_ba_mp(void *ptr);
extern void pack_ba_mp(ba_mp_t *ba_mp, Buf buffer, uint16_t protocol_version);
extern int unpack_ba_mp(ba_mp_t **ba_mp_pptr, Buf buffer,
			uint16_t protocol_version);

/* translate a string of at least AXYZ into a ba_mp_t ptr */
extern ba_mp_t *str2ba_mp(const char *coords);
/*
 * find a base blocks bg location (rack/midplane)
 */
extern ba_mp_t *loc2ba_mp(const char* mp_id);
extern ba_mp_t *coord2ba_mp(const uint16_t *coord);

/*
 * setup the ports and what not for a midplane.
 */
extern void ba_setup_mp(ba_mp_t *ba_mp, bool track_down_mps, bool wrap_it);

/*
 * copy info from a ba_mp, a direct memcpy of the ba_mp_t
 *
 * IN ba_mp: mp to be copied
 * Returned ba_mp_t *: copied info must be freed with destroy_ba_mp
 */
extern ba_mp_t *ba_copy_mp(ba_mp_t *ba_mp);

/*
 * Print a linked list of ba_geo_table_t entries.
 * IN geo_ptr - first geo_table entry to print
 * IN header - message header
 * IN my_geo_system - system geometry specification
 */
extern int ba_geo_list_print(ba_geo_table_t *geo_ptr, char *header,
			     ba_geo_system_t *my_geo_system);

/*
 * Print the contents of all ba_geo_table_t entries.
 */
extern void ba_print_geo_table(ba_geo_system_t *my_geo_system);

/*
 * Create a geo_table of possible unique geometries
 * IN/OUT my_geo_system - system geometry specification.
 *		Set dim_count and dim_size. Other fields should be NULL.
 *		This function will set total_size, geo_table_ptr, and
 *		geo_table_size.
 * IN     avoid_three - used to get around a limitation in the IBM IO
 *              system where a sub-block allocation can't reliably
 *              have a dimension of 3 in in.
 * Release memory using ba_free_geo_table().
 */
extern void ba_create_geo_table(ba_geo_system_t *my_geo_system,
				bool avoid_three);

/*
 * Free memory allocated by ba_create_geo_table().
 * IN my_geo_system - System geometry specification.
 */
extern void ba_free_geo_table(ba_geo_system_t *my_geo_system);

/*
 * Allocate a multi-dimensional node bitmap. Use ba_node_map_free() to free
 * IN my_geo_system - system geometry specification
 */
extern bitstr_t *ba_node_map_alloc(ba_geo_system_t *my_geo_system);

/*
 * Free a node map created by ba_node_map_alloc()
 * IN node_bitmap - bitmap of currently allocated nodes
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_free(bitstr_t *node_bitmap,
			     ba_geo_system_t *my_geo_system);

/*
 * Set the contents of the specified position in the bitmap
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN full_offset - N-dimension zero-origin offset to set
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_set(bitstr_t *node_bitmap, uint16_t *full_offset,
			    ba_geo_system_t *my_geo_system);

/*
 * Set the contents of the specified position in the bitmap
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN start_offset - N-dimension zero-origin offset to start setting at
 * IN end_offset - N-dimension zero-origin offset to start setting at
 * IN my_geo_system - system geometry specification
 */
extern int ba_node_map_set_range(bitstr_t *node_bitmap,
				 int *start_offset, int *end_offset,
				 ba_geo_system_t *my_geo_system);

/*
 * Return the contents of the specified position in the bitmap
 * IN node_bitmap - bitmap of currently allocated nodes
 * IN full_offset - N-dimension zero-origin offset to test
 * IN my_geo_system - system geometry specification
 */
extern int ba_node_map_test(bitstr_t *node_bitmap, uint16_t *full_offset,
			    ba_geo_system_t *my_geo_system);

/*
 * Add a new allocation's node bitmap to that of the currently
 *	allocated bitmap
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN alloc_bitmap - bitmap of nodes to be added fromtonode_bitmap
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_add(bitstr_t *node_bitmap, bitstr_t *alloc_bitmap,
			    ba_geo_system_t *my_geo_system);

/*
 * Remove a terminating allocation's node bitmap from that of the currently
 *	allocated bitmap
 * IN/OUT node_bitmap - bitmap of currently allocated nodes
 * IN alloc_bitmap - bitmap of nodes to be removed from node_bitmap
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_rm(bitstr_t *node_bitmap, bitstr_t *alloc_bitmap,
			   ba_geo_system_t *my_geo_system);

/*
 * Print the contents of a node map created by ba_node_map_alloc() or
 *	ba_geo_test_all(). Output may be in one-dimension or more depending
 *	upon configuration.
 * IN node_bitmap - bitmap representing current system state, bits are set
 *                  for currently allocated nodes
 * IN my_geo_system - system geometry specification
 */
extern void ba_node_map_print(bitstr_t *node_bitmap,
			      ba_geo_system_t *my_geo_system);

/*
 * give a hostlist version of the contents of a node map created by
 *	ba_node_map_alloc() or
 *	ba_geo_test_all(). Output may be in one-dimension or more depending
 *	upon configuration.
 * IN node_bitmap - bitmap representing current system state, bits are set
 *                  for currently allocated nodes
 * IN my_geo_system - system geometry specification
 * OUT char * - needs to be xfreed from caller.
 */
extern char *ba_node_map_ranged_hostlist(bitstr_t *node_bitmap,
					 ba_geo_system_t *my_geo_system);

/*
 * Attempt to place a new allocation into an existing node state.
 * Do not rotate or change the requested geometry, but do attempt to place
 * it using all possible starting locations.
 *
 * IN node_bitmap - bitmap representing current system state, bits are set
 *		for currently allocated nodes
 * OUT alloc_node_bitmap - bitmap representing where to place the allocation
 *		set only if RET == SLURM_SUCCESS
 * IN geo_req - geometry required for the new allocation
 * OUT attempt_cnt - number of job placements attempted
 * IN my_geo_system - system geometry specification
 * IN deny_pass - if set, then do not allow gaps in a specific dimension, any
 *		gap applies to all elements at that position in that dimension,
 *		one value per dimension, default value prevents gaps in any
 *		dimension
 * IN/OUT start_pos - input is pointer to array having same size as
 *		dimension count or NULL. Set to starting coordinates of
 *		the allocation in each dimension.
 * IN/OUT scan_offset - Location in search table from which to continue
 *		searching for resources. Initial value should be zero. If the
 *		allocation selected by the algorithm is not acceptable, call
 *		the function repeatedly with the previous output value of
 *		scan_offset
 * IN deny_wrap - If set then do not permit the allocation to wrap (i.e. do
 *		not treat as having a torus interconnect)
 * RET - SLURM_SUCCESS if allocation can be made, otherwise SLURM_ERROR
 */
extern int ba_geo_test_all(bitstr_t *node_bitmap,
			   bitstr_t **alloc_node_bitmap,
			   ba_geo_table_t *geo_req, int *attempt_cnt,
			   ba_geo_system_t *my_geo_system, uint16_t *deny_pass,
			   uint16_t *start_pos, int *scan_offset,
			   bool deny_wrap);

/* Translate a multi-dimension coordinate (3-D, 4-D, 5-D, etc.) into a 1-D
 * offset in the ba_geo_system_t bitmap
 *
 * IN full_offset - N-dimension zero-origin offset to test
 * IN my_geo_system - system geometry specification
 * RET - 1-D offset
 */
extern int ba_node_xlate_to_1d(uint16_t *full_offset,
			       ba_geo_system_t *my_geo_system);

/*
 * Used to set all midplanes in a special used state except the ones
 * we are able to use in a new allocation.
 *
 * IN: bitmap of midplanes we do or do not want
 * IN: except - If true set all midplanes not set in the bitmap else
 *              set all midplanes that are set in the bitmap.
 * RET: SLURM_SUCCESS on success, or SLURM_ERROR on error
 *
 * Note: Need to call ba_reset_all_removed_mps before starting another
 * allocation attempt after
 */
extern int ba_set_removable_mps(bitstr_t *bitmap, bool except);

/*
 * Resets the virtual system to the pervious state before calling
 * ba_set_removable_mps.
 */
extern int ba_reset_all_removed_mps(void);

/*
 * set the mp in the internal configuration as in, or not in use,
 * along with the current state of the mp.
 *
 * IN ba_mp: ba_mp_t to update state
 * IN state: new state of ba_mp_t
 */
extern void ba_update_mp_state(ba_mp_t *ba_mp, uint16_t state);

/* make sure a node is in the system return 1 if it is 0 if not */
extern int validate_coord(uint16_t *coord);

extern char *ba_switch_usage_str(uint16_t usage);

extern void set_ba_debug_flags(uint64_t debug_flags);
/*
 * Resets the virtual system to a virgin state.  If track_down_mps is set
 * then those midplanes are not set to idle, but kept in a down state.
 */
extern void reset_ba_system(bool track_down_mps);

/* in the respective block_allocator.c */
extern void ba_create_system(void);
extern void ba_destroy_system(void);

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
 * IN - avail_mp_bitmap: bitmap of usable midplanes.
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
extern int new_ba_request(select_ba_request_t* ba_request);

/*
 * print a block request
 */
extern void print_ba_request(select_ba_request_t* ba_request);

#ifdef HAVE_BG_L_P
/*
 * copy the path of the nodes given
 *
 * IN nodes List of ba_mp_t *'s: nodes to be copied
 * OUT dest_nodes List of ba_mp_t *'s: filled in list of nodes
 * wiring.
 * Return on success SLURM_SUCCESS, on error SLURM_ERROR
 */
extern int copy_node_path(List nodes, List *dest_nodes);
#endif

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
extern int allocate_block(select_ba_request_t* ba_request, List results);

/*
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int remove_block(List mps, bool is_small);

/*
 * Used to set a block into a virtual system.  The system can be
 * cleared first and this function sets all the wires and midplanes
 * used in the mplist given.  The mplist is a list of ba_mp_t's
 * that are already set up.  This is very handly to test if there are
 * any passthroughs used by one block when adding another block that
 * also uses those wires, and neither use any overlapping
 * midplanes. Doing a simple bitmap & will not reveal this.
 *
 * Returns SLURM_SUCCESS if mplist fits into system without
 * conflict, and SLURM_ERROR if mplist conflicts with something
 * already in the system.
 */
extern int check_and_set_mp_list(List mps);

/*
 * Used to find, and set up midplanes and the wires in the virtual
 * system and return them in List results
 *
 * IN/OUT results - a list with a NULL destroyer filled in with
 *        midplanes and wires set to create the block with the api. If
 *        only interested in the hostlist NULL can be excepted also.
 * IN ba_request - request for the block
 *
 * To be set in the ba_request
 *    start - where to start the allocation. (optional)
 *    geometry or size - the requested geometry of the block. (required)
 *    conn_type - mesh, torus, or small. (required)
 *
 * RET char * - hostlist of midplanes results represent must be
 *     xfreed.  NULL on failure
 */
extern char *set_bg_block(List results, select_ba_request_t* ba_request);

/*
 * Set up the map for resolving
 */
extern int set_mp_locations(void);

/*
 * set the used wires in the virtual system for a block from the real system
 */
extern int load_block_wiring(char *bg_block_id);

extern void ba_rotate_geo(uint16_t *req_geo, int rot_cnt);

extern bool ba_sub_block_in_bitmap(select_jobinfo_t *jobinfo,
				   bitstr_t *usable_bitmap, bool step);

extern int ba_sub_block_in_bitmap_clear(select_jobinfo_t *jobinfo,
					bitstr_t *usable_bitmap);

extern ba_mp_t *ba_sub_block_in_record(
	bg_record_t *bg_record, uint32_t *node_count,
	select_jobinfo_t *jobinfo);

extern int ba_sub_block_in_record_clear(
	bg_record_t *bg_record, struct step_record *step_ptr);

extern void ba_sync_job_to_block(bg_record_t *bg_record,
				 struct job_record *job_ptr);

extern bitstr_t *ba_create_ba_mp_cnode_bitmap(bg_record_t *bg_record);

/* returns a bitmap with the cnodelist bits in a midplane not set */
extern bitstr_t *ba_cnodelist2bitmap(char *cnodelist);

/* set the ionode str based off the block allocator, either ionodes
 * or cnode coords */
extern void ba_set_ionode_str(bg_record_t *bg_record);

/* Convert PASS_FOUND_* into equivalent string
 * Caller MUST xfree() the returned value */
extern char *ba_passthroughs_string(uint16_t passthrough);

extern char *give_geo(uint16_t *int_geo, int dims, bool with_sep);

extern struct job_record *ba_remove_job_in_block_job_list(
	bg_record_t *bg_record, struct job_record *job_ptr);
#endif
