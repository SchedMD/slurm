/*****************************************************************************\
 *  block_allocator.c - Assorted functions for layout of bgq blocks,
 *	 wiring, mapping for smap, etc.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Copyright (C) 2011 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "block_allocator.h"
#include "src/common/uid.h"
#include "src/common/timers.h"
#include "src/common/slurmdb_defs.h"
#include "../bg_list_functions.h"

#define DEBUG_PA
#define BEST_COUNT_INIT 20

/* in Q there are always 5 dimensions in a nodecard/board */
typedef struct {
	int start[5];
	int end[5];
} ba_nc_coords_t;

#define mp_strip_unaltered(__mp) (__mp & ~BA_MP_USED_ALTERED_PASS)

/* _ba_system is the "current" system that the structures will work
 *  on */
ba_mp_t ****ba_main_grid = NULL;
ba_geo_system_t *ba_mp_geo_system = NULL;

static ba_geo_system_t *ba_main_geo_system = NULL;
static uint16_t *deny_pass = NULL;
static ba_nc_coords_t g_nc_coords[16];
static ba_mp_t **ba_main_grid_array = NULL;
/* increment Y -> Z -> A -> X -> E
 * used for doing nodecard coords */
static int ba_nc_dim_order[5] = {Y, Z, A, X, E};

/** internal helper functions */

/* */
static char *_copy_from_main(List main_mps, List ret_list);

/* */
static char *_reset_altered_mps(List main_mps, bool get_name);

/* */
static int _check_deny_pass(int dim);

/* */
static int _fill_in_wires(List mps, ba_mp_t *start_mp, int dim,
			  uint16_t geometry, uint16_t conn_type,
			  bool full_check);

/* */
static void _setup_next_mps(int level, uint16_t *coords);

/* */
static void _increment_nc_coords(int dim, int *mp_coords, int *dim_size);

/** */
static bool _mp_used(ba_mp_t* ba_mp, int dim);

/** */
static bool _mp_out_used(ba_mp_t* ba_mp, int dim);

/** */
static uint16_t _find_distance(uint16_t start, uint16_t end, int dim);

static void _find_distance_ba_mp(
	ba_mp_t *curr_mp, ba_mp_t *end_mp, int dim, uint16_t *distance);

static int _ba_set_ionode_str_internal(int level, int *coords,
				       int *start_offset, int *end_offset,
				       hostlist_t hl);

static bitstr_t *_find_sub_block(ba_geo_table_t **geo_table,
				 uint16_t *start_loc, bitstr_t *total_bitmap,
				 uint32_t node_count);

static ba_geo_table_t *_find_geo_table(uint32_t orig_node_count,
				       uint32_t *node_count,
				       uint32_t total_count);

extern void ba_create_system()
{
	int a,x,y,z, i = 0, dim;
	uint16_t coords[SYSTEM_DIMENSIONS];
	int mp_coords[5];

	if (ba_main_grid)
		ba_destroy_system();

	slurm_mutex_lock(&ba_system_mutex);
	/* build all the possible geos for the mid planes */
	ba_main_geo_system =  xmalloc(sizeof(ba_geo_system_t));
	ba_main_geo_system->dim_count = SYSTEM_DIMENSIONS;
	ba_main_geo_system->dim_size =
		xmalloc(sizeof(int) * ba_main_geo_system->dim_count);

	for (dim = 0; dim < SYSTEM_DIMENSIONS; dim++)
		ba_main_geo_system->dim_size[dim] = DIM_SIZE[dim];

	ba_create_geo_table(ba_main_geo_system, 0);
	//ba_print_geo_table(ba_main_geo_system);

	/* build all the possible geos for a sub block inside a mid plane */
	ba_mp_geo_system =  xmalloc(sizeof(ba_geo_system_t));
	ba_mp_geo_system->dim_count = 5;
	ba_mp_geo_system->dim_size =
		xmalloc(sizeof(int) * ba_mp_geo_system->dim_count);
	/* These will never change. */
	ba_mp_geo_system->dim_size[0] = 4;
	ba_mp_geo_system->dim_size[1] = 4;
	ba_mp_geo_system->dim_size[2] = 4;
	ba_mp_geo_system->dim_size[3] = 4;
	ba_mp_geo_system->dim_size[4] = 2;
	/* FIXME: We need to not create and geo with a dimension of 3 in it.
	 * There apparently is a limitation in BGQ where you can't
	 * make a sub-block with a dimension of 3.  If this ever goes
	 * away just remove remove the extra parameter to the
	 * ba_create_geo_table.
	 *
	 * FROM IBM:
	 * We have recently encountered a problematic scenario with
	 * sub-block jobs and how the system (used for I/O) and user
	 * (used for MPI) torus class routes are configured. The
	 * network device hardware has cutoff registers to prevent
	 * packets from flowing outside of the
	 * sub-block. Unfortunately, when the sub-block has a size 3,
	 * the job can attempt to send user packets outside of its
	 * sub-block. This causes it to be terminated by signal 36.
	 */
	ba_create_geo_table(ba_mp_geo_system, 1);
	//ba_print_geo_table(ba_mp_geo_system);

	/* Now set it up to mark the corners of each nodecard.  This
	   is used if running a sub-block job on a small block later.
	*/
	/* This is the basic idea for each small block size origin 00000
	   32  = 2x2x2x2x2
	   64  = 2x2x4x2x2
	   128 = 2x2x4x4x2
	   256 = 4x2x4x4x2
	   512 = 4x4x4x4x2
	*/

	/* 32node boundaries (this is what the following code generates)
	   N00 - 32  = 00000x11111
	   N01 - 64  = 00200x11311
	   N02 - 96  = 00020x11131
	   N03 - 128 = 00220x11331
	   N04 - 160 = 20000x31111
	   N05 - 192 = 20200x31311
	   N06 - 224 = 20020x31131
	   N07 - 256 = 20220x31331
	   N08 - 288 = 02000x13111
	   N09 - 320 = 02200x13311
	   N10 - 352 = 02020x13131
	   N11 - 384 = 02220x13331
	   N12 - 416 = 22000x33111
	   N13 - 448 = 22200x33311
	   N14 - 480 = 22020x33131
	   N15 - 512 = 22220x33331
	*/
	memset(&mp_coords, 0, sizeof(mp_coords));
	for (i=0; i<16; i++) {
		/*
		 * increment Y -> Z -> A -> X
		 * E always goes from 0->1
		 */
		for (dim = 0; dim < 5; dim++) {
			g_nc_coords[i].start[dim] =
				g_nc_coords[i].end[dim] = mp_coords[dim];
			g_nc_coords[i].end[dim]++;
		}
		/* info("%d\tgot %c%c%c%c%cx%c%c%c%c%c", */
		/*      i, */
		/*      alpha_num[g_nc_coords[i].start[A]], */
		/*      alpha_num[g_nc_coords[i].start[X]], */
		/*      alpha_num[g_nc_coords[i].start[Y]], */
		/*      alpha_num[g_nc_coords[i].start[Z]], */
		/*      alpha_num[g_nc_coords[i].start[E]], */
		/*      alpha_num[g_nc_coords[i].end[A]], */
		/*      alpha_num[g_nc_coords[i].end[X]], */
		/*      alpha_num[g_nc_coords[i].end[Y]], */
		/*      alpha_num[g_nc_coords[i].end[Z]], */
		/*      alpha_num[g_nc_coords[i].end[E]]); */
		_increment_nc_coords(0, mp_coords, ba_mp_geo_system->dim_size);
	}

	/* Set up a flat array to be used in conjunction with the
	   ba_geo system.
	*/
	ba_main_grid_array = xmalloc(sizeof(ba_mp_t *) *
				     ba_main_geo_system->total_size);
	i = 0;
	ba_main_grid = (ba_mp_t****)
		xmalloc(sizeof(ba_mp_t***) * DIM_SIZE[A]);
	for (a = 0; a < DIM_SIZE[A]; a++) {
		ba_main_grid[a] = (ba_mp_t***)
			xmalloc(sizeof(ba_mp_t**) * DIM_SIZE[X]);
		for (x = 0; x < DIM_SIZE[X]; x++) {
			ba_main_grid[a][x] = (ba_mp_t**)
				xmalloc(sizeof(ba_mp_t*) * DIM_SIZE[Y]);
			for (y = 0; y < DIM_SIZE[Y]; y++) {
				ba_main_grid[a][x][y] = (ba_mp_t*)
					xmalloc(sizeof(ba_mp_t) * DIM_SIZE[Z]);
				for (z = 0; z < DIM_SIZE[Z]; z++) {
					ba_mp_t *ba_mp = &ba_main_grid
						[a][x][y][z];
					ba_mp->coord[A] = a;
					ba_mp->coord[X] = x;
					ba_mp->coord[Y] = y;
					ba_mp->coord[Z] = z;

					snprintf(ba_mp->coord_str,
						 sizeof(ba_mp->coord_str),
						 "%c%c%c%c",
						 alpha_num[ba_mp->coord[A]],
						 alpha_num[ba_mp->coord[X]],
						 alpha_num[ba_mp->coord[Y]],
						 alpha_num[ba_mp->coord[Z]]);
					ba_setup_mp(ba_mp, true, false);
					ba_mp->state = NODE_STATE_IDLE;
					/* This might get changed
					   later, but just incase set
					   it up here.
					*/
					ba_mp->index = i++;
					ba_mp->ba_geo_index =
						ba_node_xlate_to_1d(
							ba_mp->coord,
							ba_main_geo_system);
					ba_main_grid_array[ba_mp->ba_geo_index]
						= ba_mp;
				}
			}
		}
	}

	_setup_next_mps(A, coords);
	slurm_mutex_unlock(&ba_system_mutex);
}

/** */
extern void ba_destroy_system(void)
{
	int a, x, y, z;

	slurm_mutex_lock(&ba_system_mutex);
	xfree(ba_main_grid_array);

	if (ba_main_grid) {
		for (a=0; a<DIM_SIZE[A]; a++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				for (y = 0; y < DIM_SIZE[Y]; y++) {
					for (z=0; z < DIM_SIZE[Z]; z++) {
						free_internal_ba_mp(
							&ba_main_grid
							[a][x][y][z]);
					}
					xfree(ba_main_grid[a][x][y]);
				}
				xfree(ba_main_grid[a][x]);
			}
			xfree(ba_main_grid[a]);
		}
		xfree(ba_main_grid);
		ba_main_grid = NULL;
	}

	if (ba_main_geo_system) {
		ba_free_geo_table(ba_main_geo_system);
		xfree(ba_main_geo_system->dim_size);
		xfree(ba_main_geo_system);
	}

	if (ba_mp_geo_system) {
		ba_free_geo_table(ba_mp_geo_system);
		xfree(ba_mp_geo_system->dim_size);
		xfree(ba_mp_geo_system);
	}

	memset(DIM_SIZE, 0, sizeof(DIM_SIZE));
	slurm_mutex_unlock(&ba_system_mutex);
}

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
 * IN - nodecards: Number of nodecards in each block in request only
 *      used of small block allocations.
 * OUT - passthroughs: if there were passthroughs used in the
 *       generation of the block.
 * IN - procs: Number of real processors requested
 * IN - quarters: Number of midplane quarters in each block in request only
 *      used of small block allocations.
 * IN - RamDiskimage: RamDiskImage for this block if not default
 * IN - rotate: if true, allows rotation of block during fit
 * OUT - save_name: hostlist of midplanes used in block
 * IN/OUT - size: requested/returned count of midplanes in block
 * IN - start: geo location of where to start the allocation
 * IN - start_req: if set use the start variable to start at
 * return success of allocation/validation of params
 */
extern int new_ba_request(select_ba_request_t* ba_request)
{
	int i=0;

	xfree(ba_request->save_name);

	if (ba_request->geometry[0] != (uint16_t)NO_VAL) {
		for (i=0; i<cluster_dims; i++){
			if ((ba_request->geometry[i] < 1)
			    || (ba_request->geometry[i] > DIM_SIZE[i])) {
				error("new_ba_request Error, "
				      "request geometry is invalid dim %d "
				      "can't be %c, largest is %c",
				      i,
				      alpha_num[ba_request->geometry[i]],
				      alpha_num[DIM_SIZE[i]]);
				return 0;
			}
		}
		ba_request->size = 1;
		for (i=0; i<cluster_dims; i++)
			ba_request->size *= ba_request->geometry[i];
	}

	if (!(cluster_flags & CLUSTER_FLAG_BGQ)) {
		if (ba_request->size
		    && (ba_request->geometry[0] == (uint16_t)NO_VAL)) {
			ba_request->geometry[0] = ba_request->size;
		} else {
			error("new_ba_request: "
			      "No size or geometry given");
			return 0;
		}
		return 1;
	}

	if (ba_request->deny_pass == (uint16_t)NO_VAL)
		ba_request->deny_pass = ba_deny_pass;

	deny_pass = &ba_request->deny_pass;
	return 1;
}

/**
 * print a block request
 */
extern void print_ba_request(select_ba_request_t* ba_request)
{
	int i;

	if (ba_request == NULL){
		error("print_ba_request Error, request is NULL");
		return;
	}
	debug("  ba_request:");
	debug("    geometry:\t");
	for (i=0; i<cluster_dims; i++){
		debug("%d", ba_request->geometry[i]);
	}
	debug("   conn_type:\t");
	for (i=0; i<cluster_dims; i++){
		debug("%d", ba_request->conn_type[i]);
	}
	debug("        size:\t%d", ba_request->size);
	debug("      rotate:\t%d", ba_request->rotate);
	debug("    elongate:\t%d", ba_request->elongate);
}

/* ba_system_mutex needs to be locked before calling this. */
extern ba_mp_t *coord2ba_mp(const uint16_t *coord)
{
	if ((coord[A] >= DIM_SIZE[A]) || (coord[X] >= DIM_SIZE[X]) ||
	    (coord[Y] >= DIM_SIZE[Y]) || (coord[Z] >= DIM_SIZE[Z])) {
		error("Invalid coordinate %d:%d:%d:%d",
		      coord[A], coord[X], coord[Y], coord[Z]);
		return NULL;
	}
	return &ba_main_grid[coord[A]][coord[X]][coord[Y]][coord[Z]];
}


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
extern int allocate_block(select_ba_request_t* ba_request, List results)
{
	if (!ba_initialized){
		error("Error, configuration not initialized, "
		      "calling ba_init(NULL, 1)");
		ba_init(NULL, 1);
	}

	if (!ba_request){
		error("allocate_block Error, request not initialized");
		return 0;
	}

	if (!(cluster_flags & CLUSTER_FLAG_BG))
		return 0;

	if ((ba_request->save_name = set_bg_block(results, ba_request)))
		return 1;

	debug2("allocate_block: can't allocate");

	return 0;
}

/*
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int remove_block(List mps, bool is_small)
{
	int dim;
	ba_mp_t* curr_ba_mp = NULL;
	ba_mp_t* ba_mp = NULL;
	ListIterator itr;

	slurm_mutex_lock(&ba_system_mutex);
	itr = list_iterator_create(mps);
	while ((curr_ba_mp = (ba_mp_t*) list_next(itr))) {
		/* since the list that comes in might not be pointers
		   to the main list we need to point to that main list */
		ba_mp = coord2ba_mp(curr_ba_mp->coord);
		if (curr_ba_mp->used) {
			ba_mp->used &= (~BA_MP_USED_TRUE);
			if (ba_mp->used == BA_MP_USED_FALSE)
				bit_clear(ba_main_mp_bitmap,
					  ba_mp->ba_geo_index);
		}
		ba_mp->used &= (~BA_MP_USED_ALTERED_PASS);

		/* Small blocks don't use wires, and only have 1 mp,
		   so just break. */
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("remove_block: midplane %s used state now %d",
			     ba_mp->coord_str, ba_mp->used);

		for (dim=0; dim<cluster_dims; dim++) {
			/* House the altered usage here without any
			   error so we don't take it from the original.
			*/
			uint16_t altered_usage;

			if (curr_ba_mp == ba_mp) {
				altered_usage = ba_mp->alter_switch[dim].usage
					& (~BG_SWITCH_CABLE_ERROR_FULL);
				/* Remove the usage that was altered */
				/* info("remove_block: %s(%d) %s removing %s", */
				/*      ba_mp->coord_str, dim, */
				/*      ba_switch_usage_str( */
				/* 	     ba_mp->axis_switch[dim].usage), */
				/*      ba_switch_usage_str( */
				/* 	     ba_mp->alter_switch[dim].usage)); */
				ba_mp->axis_switch[dim].usage &=
					(~altered_usage);
				/* info("remove_block: %s(%d) is now at %s", */
				/*      ba_mp->coord_str, dim, */
				/*      ba_switch_usage_str( */
				/* 	     ba_mp->axis_switch[dim].usage)); */
				continue;
			}

			/* Set this after we know curr_ba_mp isn't
			   the same as ba_mp so we don't mess up the
			   original.
			*/
			altered_usage = curr_ba_mp->axis_switch[dim].usage
				& (~BG_SWITCH_CABLE_ERROR_FULL);
			if (altered_usage != BG_SWITCH_NONE) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("remove_block: 2 %s(%d) %s %s "
					     "removing %s",
					     ba_mp->coord_str, dim,
					     curr_ba_mp->coord_str,
					     ba_switch_usage_str(
						     ba_mp->axis_switch
						     [dim].usage),
					     ba_switch_usage_str(
						     altered_usage));
				/* Just remove the usage set here */
				ba_mp->axis_switch[dim].usage &=
					(~altered_usage);
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("remove_block: 2 %s(%d) is "
					     "now at %s",
					     ba_mp->coord_str, dim,
					     ba_switch_usage_str(
						     ba_mp->axis_switch[dim].
						     usage));
			}
			//ba_mp->alter_switch[dim].usage = BG_SWITCH_NONE;
		}
	}
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&ba_system_mutex);

	return 1;
}

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
extern int check_and_set_mp_list(List mps)
{
	int rc = SLURM_ERROR;
	int i;
	ba_switch_t *ba_switch = NULL, *curr_ba_switch = NULL;
	ba_mp_t *ba_mp = NULL, *curr_ba_mp = NULL;
	ListIterator itr = NULL;

	if (!mps)
		return rc;

	slurm_mutex_lock(&ba_system_mutex);
	itr = list_iterator_create(mps);
	while ((ba_mp = list_next(itr))) {
		/* info("checking %s", ba_mp->coord_str);  */
		curr_ba_mp = coord2ba_mp(ba_mp->coord);

		if (ba_mp->used && curr_ba_mp->used) {
			/* Only error if the midplane isn't already
			 * marked down or in a error state outside of
			 * the bluegene block.
			 */
			uint16_t base_state, mp_flags;
			base_state = curr_ba_mp->state & NODE_STATE_BASE;
			mp_flags = curr_ba_mp->state & NODE_STATE_FLAGS;
			if (!(mp_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL))
			    && (base_state != NODE_STATE_DOWN)) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("check_and_set_mp_list: "
					     "I have already been to "
					     "this mp %s %s %d %d",
					     ba_mp->coord_str,
					     node_state_string(
						     curr_ba_mp->state),
					     ba_mp->used, curr_ba_mp->used);
				rc = SLURM_ERROR;
				goto end_it;
			}
		}

		if (ba_mp->used) {
			curr_ba_mp->used = ba_mp->used;
			xassert(!bit_test(ba_main_mp_bitmap,
					  ba_mp->ba_geo_index));
			bit_set(ba_main_mp_bitmap, ba_mp->ba_geo_index);
		}

		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("check_and_set_mp_list: "
			     "%s is used ?= %d %d",
			     curr_ba_mp->coord_str,
			     curr_ba_mp->used, ba_mp->used);
		for(i=0; i<cluster_dims; i++) {
			ba_switch = &ba_mp->axis_switch[i];
			curr_ba_switch = &curr_ba_mp->axis_switch[i];
			//info("checking dim %d", i);

			if (ba_switch->usage == BG_SWITCH_NONE)
				continue;
			else if (ba_switch->usage
				 & BG_SWITCH_CABLE_ERROR_FULL) {
				debug2("check_and_set_mp_list: We have "
				       "a switch with an error set in it.  "
				       "This can happen on a system with "
				       "missing cables such as a half rack "
				       "system, or when a nodeboard has "
				       "been set in a service state. %u",
				       ba_switch->usage);
				continue;
			}


			if (ba_switch->usage & curr_ba_switch->usage) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("check_and_set_mp_list: "
					     "%s(%d) is already in "
					     "use the way we want to use it.  "
					     "%s already at %s",
					     ba_mp->coord_str, i,
					     ba_switch_usage_str(
						     ba_switch->usage),
					     ba_switch_usage_str(
						     curr_ba_switch->usage));
				rc = SLURM_ERROR;
				goto end_it;
			}
			/* Since we are only checking to see if this
			   block is creatable we don't need to check
			   hardware issues like bad cables.
			*/
			/* else if ((curr_ba_switch->usage */
			/* 	    & BG_SWITCH_CABLE_ERROR_SET) */
			/* 	   && (ba_switch->usage & BG_SWITCH_OUT_PASS)) { */
			/* 	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) */
			/* 		info("check_and_set_mp_list: " */
			/* 		     "%s(%d)'s cable is not available " */
			/* 		     "can't really make this block.  " */
			/* 		     "We need %s and system is %s", */
			/* 		     ba_mp->coord_str, i, */
			/* 		     ba_switch_usage_str( */
			/* 			     ba_switch->usage), */
			/* 		     ba_switch_usage_str( */
			/* 			     curr_ba_switch->usage)); */
			/* } */

			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("check_and_set_mp_list: "
				     "setting %s(%d) to from %s to %s",
				     ba_mp->coord_str, i,
				     ba_switch_usage_str(curr_ba_switch->usage),
				     ba_switch_usage_str(curr_ba_switch->usage
							 | ba_switch->usage));
			curr_ba_switch->usage |= ba_switch->usage;
		}
	}
	rc = SLURM_SUCCESS;
end_it:
	list_iterator_destroy(itr);
	slurm_mutex_unlock(&ba_system_mutex);
	return rc;
}

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
extern char *set_bg_block(List results, select_ba_request_t* ba_request)
{
	List main_mps = NULL;
	char *name = NULL;
	ba_mp_t* ba_mp = NULL;
	int dim;
	uint16_t local_deny_pass = ba_deny_pass;
	ba_geo_table_t *ba_geo_table = NULL;
	bitstr_t *success_bitmap = NULL;
	uint16_t orig_conn_type[HIGHEST_DIMENSIONS];

	xassert(ba_initialized);

	if (!ba_request->size) {
		if (ba_request->geometry[0] == (uint16_t)NO_VAL) {
			error("set_bg_block: No size or geometry given.");
			return NULL;
		}

		ba_request->size = 1;
		for (dim=0; dim<cluster_dims; dim++)
			ba_request->size *= ba_request->geometry[dim];
	}
		/* set up the geo_table */

	xassert(ba_request->size);
	if (!(ba_geo_table =
	      ba_main_geo_system->geo_table_ptr[ba_request->size])) {
		error("set_bg_block: No geometries for %d midplanes",
		      ba_request->size);
		return NULL;
	}
	if (!deny_pass)
		deny_pass = &local_deny_pass;

	memcpy(orig_conn_type, ba_request->conn_type,
	       sizeof(ba_request->conn_type));

	slurm_mutex_lock(&ba_system_mutex);
	while (ba_geo_table) {
		ListIterator itr;
		int scan_offset = 0, cnt = 0, i=0;
		uint16_t start_loc[ba_main_geo_system->dim_count];

		if (ba_request->geometry[0] != (uint16_t)NO_VAL) {
			/* if we are requesting a specific geo, go directly to
			   that geo_table. */
			if (memcmp(ba_request->geometry, ba_geo_table->geometry,
				   sizeof(uint16_t) * cluster_dims)) {
				ba_geo_table = ba_geo_table->next_ptr;
				continue;
			}
		}

	try_again:
		if (success_bitmap)
			FREE_NULL_BITMAP(success_bitmap);
		if (main_mps && list_count(main_mps)) {
			_reset_altered_mps(main_mps, 0);
			list_flush(main_mps);
		}

		if (ba_geo_test_all(ba_main_mp_bitmap,
				    &success_bitmap,
				    ba_geo_table, &cnt,
				    ba_main_geo_system, deny_pass,
				    start_loc, &scan_offset, false)
		    != SLURM_SUCCESS) {
			if (ba_request->geometry[0] != (uint16_t)NO_VAL) {
				ba_geo_table = NULL;
				break;
			}

			ba_geo_table = ba_geo_table->next_ptr;
			continue;
		}

		if (ba_request->start_req) {
			/* if we are requesting a specific start make
			   sure that is what is returned.  Else try
			   again.  Since this only happens with smap
			   or startup this handling it this way
			   shouldn't be that big of a deal. */
			if (memcmp(ba_request->start, start_loc,
				   sizeof(uint16_t) * cluster_dims))
				goto try_again;
		}

		if (!main_mps)
			main_mps = list_create(NULL);
		for (i=0; i<ba_main_geo_system->total_size; i++) {
			if (!bit_test(success_bitmap, i))
				continue;
			ba_mp = ba_main_grid_array[i];
			xassert(ba_mp);

			for (dim=0; dim<cluster_dims; dim++) {
				if (_mp_used(ba_mp, dim))
					goto try_again;

				if (ba_geo_table->geometry[dim] == 1) {
					/* Always check MESH here since we
					 * only care about the IN/OUT ports.
					 * all 1 dimensions need a TORUS */
					ba_mp->alter_switch[dim].usage
						|= BG_SWITCH_WRAPPED;
					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("set_bg_block: "
						     "using mp %s(%d) "
						     "in 1 geo %s added %s",
						     ba_mp->coord_str, dim,
						     ba_switch_usage_str(
							     ba_mp->
							     axis_switch[dim].
							     usage),
						     ba_switch_usage_str(
							     ba_mp->
							     alter_switch[dim].
							     usage));
					continue;
				}
			}
			ba_mp->used = BA_MP_USED_ALTERED;
			list_append(main_mps, ba_mp);
		}
		/* If we are going to take up the entire dimension
		   might as well force it to be TORUS.  Check against
		   MESH here instead of !TORUS so we don't mess up
		   small block allocations.
		*/
		for (dim=0; dim<cluster_dims; dim++) {
			if ((ba_geo_table->geometry[dim] == 1)
			    || ((ba_geo_table->geometry[dim] == DIM_SIZE[dim])
				&& (ba_request->conn_type[dim]
				    == SELECT_NAV))) {
				/* On a Q all single midplane blocks
				 * must be a TORUS.
				 *
				 * Also if we are using all midplanes
				 * in a dimension might as well make
				 * it a torus.
				 */
				ba_request->conn_type[dim] = SELECT_TORUS;
			} else if (ba_request->conn_type[dim] == SELECT_NAV) {
				/* Set everything else to the default */
				ba_request->conn_type[dim] =
					bg_conf->default_conn_type[dim];
			}
		}

		itr = list_iterator_create(main_mps);
		while ((ba_mp = list_next(itr))) {
			if (ba_mp->used & BA_MP_USED_PASS_BIT)
				continue;
			for (dim=0; dim<cluster_dims; dim++) {
				if ((ba_geo_table->geometry[dim] == 1)
				    || (ba_mp->coord[dim] != start_loc[dim]))
					continue;
				if (!_fill_in_wires(
					    main_mps, ba_mp, dim,
					    ba_geo_table->geometry[dim],
					    ba_request->conn_type[dim],
					    ba_request->full_check)) {
					list_iterator_destroy(itr);
					memcpy(ba_request->conn_type,
					       orig_conn_type,
					       sizeof(ba_request->conn_type));
					goto try_again;
				}
			}
		}
		list_iterator_destroy(itr);

		/* fill in the start with the actual start of the
		 * block since it isn't always easy to figure out and
		 * is easily */
		memcpy(ba_request->start, start_loc, sizeof(ba_request->start));

		break;
	}

	if (success_bitmap)
		FREE_NULL_BITMAP(success_bitmap);

	if (ba_geo_table) {
		/* Success */
		if (results)
			name = _copy_from_main(main_mps, results);
		else
			name = _reset_altered_mps(main_mps, 1);
	}

	if (main_mps) {
		/* handle failure */
		if (!name)
			_reset_altered_mps(main_mps, 0);
		list_destroy(main_mps);
		main_mps = NULL;
	}
	slurm_mutex_unlock(&ba_system_mutex);

	if (name)
		debug2("name = %s", name);
	else
		debug2("can't allocate");

	if (deny_pass == &local_deny_pass)
		deny_pass = NULL;

	return name;
}

extern void ba_rotate_geo(uint16_t *req_geo, int rot_cnt)
{
	uint16_t tmp;

	switch (rot_cnt) {
	case 0:		/* ABCD -> ABDC */
	case 3:		/* DABC -> DACB */
	case 6:		/* CDAB -> CDBA */
	case 9:		/* CADB -> CABD */
	case 14:	/* DBAC -> DBCA */
	case 17:	/* ACBD -> ACDB */
	case 20:	/* BDCA -> BCDA */
	case 21:	/* BCDA -> BCAD */
		SWAP(req_geo[Y], req_geo[Z], tmp);
		break;
	case 1:		/* ABDC -> ADBC */
	case 4:		/* DACB -> DCAB */
	case 7:		/* CDBA -> CBDA */
	case 10:	/* CABD -> CBAD */
	case 12:	/* BADC -> BDAC */
	case 15:	/* DBCA -> DCBA */
	case 18:	/* ACDB -> ADCB */
	case 22:	/* BCAD -> BACD */
		SWAP(req_geo[X], req_geo[Y], tmp);
		break;
	case 2:		/* ADBC -> DABC */
	case 5:		/* DCAB -> CDAB */
	case 13:	/* BDAC -> DBAC */
	case 23:	/* BACD -> ABCD */
		SWAP(req_geo[A], req_geo[X], tmp);
		break;
	case 16:	/* DCBA -> ACBD */
	case 19:	/* ADCB -> BDCA */
		SWAP(req_geo[A], req_geo[Z], tmp);
		break;
	case 8:		/* CBDA -> CADB */
		SWAP(req_geo[X], req_geo[Z], tmp);
		break;
	case 11:	/* CBAD -> BCAD -> BACD -> BADC */
		SWAP(req_geo[A], req_geo[X], tmp);
		SWAP(req_geo[X], req_geo[Y], tmp);
		SWAP(req_geo[Y], req_geo[Z], tmp);
		break;

	}

}

extern bool ba_sub_block_in_bitmap(select_jobinfo_t *jobinfo,
				   bitstr_t *usable_bitmap, bool step)
{
	bitstr_t *found_bits = NULL;
	uint32_t node_count;
	ba_geo_table_t *geo_table = NULL;
	int clr_cnt, dim;
	uint16_t start_loc[ba_mp_geo_system->dim_count];

	xassert(jobinfo);
	xassert(usable_bitmap);

	node_count = jobinfo->cnode_cnt;
	clr_cnt = bit_clear_count(usable_bitmap);

	if (clr_cnt < node_count)
		return false;

	jobinfo->dim_cnt = ba_mp_geo_system->dim_count;

try_again:
	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO) {
		bit_not(usable_bitmap);
		char *tmp_char = ba_node_map_ranged_hostlist(
			usable_bitmap, ba_mp_geo_system);
		bit_not(usable_bitmap);
		info("ba_sub_block_in_bitmap: "
		     "looking for %u in a field of %u (%s).",
		     node_count, clr_cnt, tmp_char);
		xfree(tmp_char);
	}

	if (!(geo_table = _find_geo_table(node_count, &node_count, clr_cnt)))
		return false;

	if (!(found_bits = _find_sub_block(
		      &geo_table, start_loc, usable_bitmap, node_count))) {
		/* This is to vet we have a good geo on this request.  So if a
		   person asks for 12 and the only reason they can't get it is
		   because they can't get that geo and if they would of asked
		   for 16 then they could run we do that for them.
		*/
		node_count++;
		if (clr_cnt > node_count) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("trying with a larger size");
			goto try_again;
		}
		return false;
	}

	if (jobinfo->units_avail)
		FREE_NULL_BITMAP(jobinfo->units_avail);
	if (jobinfo->units_used)
		FREE_NULL_BITMAP(jobinfo->units_used);

	jobinfo->units_avail = found_bits;
	found_bits = NULL;
	jobinfo->units_used = bit_copy(jobinfo->units_avail);
	/* ba_sub_block_in_bitmap works for both job and step
	   allocations.  It sets the units_used to the
	   opposite of units_available by default.  If used for a step
	   we want all units used to be that of the avail for easy
	   clearing.
	*/
	if (!step)
		bit_not(jobinfo->units_used);
	xfree(jobinfo->ionode_str);

	jobinfo->cnode_cnt = node_count;

	for (dim = 0; dim < jobinfo->dim_cnt; dim++) {
		jobinfo->geometry[dim] = geo_table->geometry[dim];
		jobinfo->start_loc[dim] = start_loc[dim];
	}

	if (node_count < bg_conf->mp_cnode_cnt) {
		jobinfo->ionode_str = ba_node_map_ranged_hostlist(
			jobinfo->units_avail, ba_mp_geo_system);
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO) {
			char *tmp_char;
			bitstr_t *total_bitmap = bit_copy(usable_bitmap);
			bit_or(total_bitmap, jobinfo->units_avail);
			bit_not(total_bitmap);
			tmp_char = ba_node_map_ranged_hostlist(
				total_bitmap, ba_mp_geo_system);
			FREE_NULL_BITMAP(total_bitmap);
			info("ba_sub_block_in_bitmap: "
			     "can use cnodes %s leaving '%s' usable.",
			     jobinfo->ionode_str, tmp_char);
			xfree(tmp_char);
		}
	} else if (ba_debug_flags & DEBUG_FLAG_BG_ALGO) {
		info("ba_sub_block_in_bitmap: "
		     "can use all cnodes leaving none usable.");
	}

	return true;
}

extern int ba_sub_block_in_bitmap_clear(
	select_jobinfo_t *jobinfo, bitstr_t *usable_bitmap)
{
	char *tmp_char = NULL, *tmp_char2 = NULL;

	if (!jobinfo->units_avail) {
		error("ba_sub_block_in_bitmap_clear: "
		      "no units avail bitmap on the jobinfo");
		return SLURM_ERROR;
	}

	/* use units_avail here instead of units_used so it works for
	   both jobs and steps with no other code.
	*/
	bit_not(jobinfo->units_avail);
	bit_and(usable_bitmap, jobinfo->units_avail);
	bit_not(jobinfo->units_avail);

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_ALGO) {
		tmp_char = ba_node_map_ranged_hostlist(
			jobinfo->units_avail, ba_mp_geo_system);
		bit_not(usable_bitmap);
		tmp_char2 = ba_node_map_ranged_hostlist(
			usable_bitmap, ba_mp_geo_system);
		bit_not(usable_bitmap);
		info("ba_sub_block_in_bitmap_clear: "
		     "cleared cnodes %s making '%s' available.",
		     tmp_char, tmp_char2);
		xfree(tmp_char);
		xfree(tmp_char2);
	}

	return SLURM_SUCCESS;
}

extern ba_mp_t *ba_sub_block_in_record(
	bg_record_t *bg_record, uint32_t *node_count, select_jobinfo_t *jobinfo)
{
	ListIterator itr = NULL;
	ba_mp_t *ba_mp = NULL;
	ba_geo_table_t *geo_table = NULL;
	char *tmp_char = NULL;
	uint32_t orig_node_count = *node_count;
	int dim;
	uint32_t max_clear_cnt = 0, clear_cnt;
	bitstr_t *total_bitmap = NULL;
	uint16_t start_loc[ba_mp_geo_system->dim_count];
	bool passthrough_used = false;

	xassert(ba_mp_geo_system);
	xassert(bg_record->ba_mp_list);
	xassert(jobinfo);
	xassert(!jobinfo->units_used);

	jobinfo->dim_cnt = ba_mp_geo_system->dim_count;

try_again:
	if (!(geo_table = _find_geo_table(
		      orig_node_count, node_count, bg_record->cnode_cnt)))
		return NULL;

	itr = list_iterator_create(bg_record->ba_mp_list);
	while ((ba_mp = list_next(itr))) {
		if (!ba_mp->used) {
			passthrough_used = true;
			continue;
		}

		/* Create the bitmap if it doesn't exist.  Since this
		 * is a copy of the original and the cnode_bitmap is
		 * only used for sub-block jobs we only create it
		 * when needed. */
		if (!ba_mp->cnode_bitmap)
		if (!ba_mp->cnode_bitmap) {
			ba_mp->cnode_bitmap =
				ba_create_ba_mp_cnode_bitmap(bg_record);
			FREE_NULL_BITMAP(ba_mp->cnode_usable_bitmap);
			ba_mp->cnode_usable_bitmap =
				bit_copy(ba_mp->cnode_bitmap);
		}

		if (!ba_mp->cnode_err_bitmap)
			ba_mp->cnode_err_bitmap =
				bit_alloc(bg_conf->mp_cnode_cnt);
		total_bitmap = bit_copy(ba_mp->cnode_bitmap);
		bit_or(total_bitmap, ba_mp->cnode_err_bitmap);

		if ((jobinfo->units_used = _find_sub_block(
			     &geo_table, start_loc, total_bitmap, *node_count)))
			break;

		clear_cnt = bit_clear_count(total_bitmap);

		FREE_NULL_BITMAP(total_bitmap);

		/* Grab the most empty midplane to be used later if we
		   can't find a spot.
		*/
		if (max_clear_cnt < clear_cnt)
			max_clear_cnt = clear_cnt;

		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("couldn't place it on %s", ba_mp->coord_str);
		geo_table = ba_mp_geo_system->geo_table_ptr[*node_count];
	}
	list_iterator_destroy(itr);

	/* This is to vet we have a good geo on this request.  So if a
	   person asks for 12 and the only reason they can't get it is
	   because they can't get that geo and if they would of asked
	   for 16 then they could run we do that for them.
	*/
	if (!ba_mp) {
		if (max_clear_cnt > (*node_count)+1) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("trying with a larger size");
			(*node_count)++;
			goto try_again;
		}
		return NULL;
	}

	/* SUCCESS! */
	if (passthrough_used) {
		/* Since we don't keep track of next mp's in a block
		 * we just recreate it in the virtual system.  This
		 * will only happen on rare occation, so it shouldn't
		 * hurt performance in most cases. (block_state_mutex
		 * should already be locked)
		 */
		reset_ba_system(false);
		if (check_and_set_mp_list(bg_record->ba_mp_list)
		    == SLURM_ERROR) {
			error("ba_sub_block_in_record: "
			      "something happened in the load of %s, "
			      "this should never happen",
			      bg_record->bg_block_id);
			passthrough_used = false;
		}
	}

	/* Since we use conn_type as the relative start point, if the
	   block uses more than 1 midplane we need to give the
	   relative start point a boost when we go to a different midplane.
	*/
	memset(jobinfo->conn_type, 0, sizeof(jobinfo->conn_type));
	for (dim=0; dim<SYSTEM_DIMENSIONS; dim++) {
		if (!passthrough_used)
			jobinfo->conn_type[dim] = _find_distance(
				bg_record->start[dim], ba_mp->coord[dim], dim);
		else
			_find_distance_ba_mp(coord2ba_mp(bg_record->start),
					     ba_mp, dim,
					     &jobinfo->conn_type[dim]);
	}

	bit_or(ba_mp->cnode_bitmap, jobinfo->units_used);
	jobinfo->ionode_str = ba_node_map_ranged_hostlist(
		jobinfo->units_used, ba_mp_geo_system);
	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO) {
		bit_or(total_bitmap, jobinfo->units_used);
		bit_not(total_bitmap);
		tmp_char = ba_node_map_ranged_hostlist(
			total_bitmap, ba_mp_geo_system);
		info("ba_sub_block_in_record: "
		     "using cnodes %s on mp %s "
		     "leaving '%s' on this midplane "
		     "usable in this block (%s)",
		     jobinfo->ionode_str,
		     ba_mp->coord_str, tmp_char,
		     bg_record->bg_block_id);
		xfree(tmp_char);
	}

	for (dim = 0; dim < jobinfo->dim_cnt; dim++) {
		jobinfo->geometry[dim] =
			geo_table->geometry[dim];
		jobinfo->start_loc[dim] = start_loc[dim];
	}
	FREE_NULL_BITMAP(total_bitmap);

	return ba_mp;
}

extern int ba_sub_block_in_record_clear(
	bg_record_t *bg_record, struct step_record *step_ptr)
{
	bitoff_t bit;
	ListIterator itr = NULL;
	ba_mp_t *ba_mp = NULL;
	select_jobinfo_t *jobinfo = NULL;
	char *tmp_char = NULL, *tmp_char2 = NULL, *tmp_char3 = NULL;

	xassert(bg_record);
	xassert(step_ptr);

	jobinfo = step_ptr->select_jobinfo->data;
	xassert(jobinfo);

	/* If we are using the entire block and the block is larger
	 * than 1 midplane we don't need to do anything. */
	if ((jobinfo->cnode_cnt == bg_record->cnode_cnt)
	    && (bg_record->mp_count != 1))
		return SLURM_SUCCESS;

	if ((bit = bit_ffs(step_ptr->step_node_bitmap)) == -1) {
		error("ba_sub_block_in_record_clear: "
		      "we couldn't find any bits set");
		return SLURM_ERROR;
	}

	itr = list_iterator_create(bg_record->ba_mp_list);
	while ((ba_mp = list_next(itr))) {
		if (ba_mp->index != bit)
			continue;
		if (!jobinfo->units_used) {
			/* from older version of slurm */
			error("ba_sub_block_in_record_clear: "
			      "didn't have the units_used bitmap "
			      "for some reason?");
			break;
		} else if (!ba_mp->cnode_bitmap) {
			/* If the job allocation has already finished
			   before processing the job step completion
			   this could happen, but it should already be
			   checked before it gets here so this should
			   never happen, this is just for safely sake.
			*/
			error("ba_sub_block_in_record_clear: no cnode_bitmap? "
			      "job %u(%p) is in state %s on block %s %u(%p). "
			      "This should never happen.",
			      step_ptr->job_ptr->job_id, step_ptr->job_ptr,
			      job_state_string(step_ptr->job_ptr->job_state
					       & (~JOB_CONFIGURING)),
			      bg_record->bg_block_id, bg_record->job_running,
			      bg_record->job_ptr);
			break;
		}

		bit_not(jobinfo->units_used);
		bit_and(ba_mp->cnode_bitmap, jobinfo->units_used);
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_ALGO) {
			bitstr_t *total_bitmap = bit_copy(ba_mp->cnode_bitmap);
			if (ba_mp->cnode_err_bitmap) {
				bit_or(total_bitmap, ba_mp->cnode_err_bitmap);
				tmp_char3 = ba_node_map_ranged_hostlist(
					ba_mp->cnode_err_bitmap,
					ba_mp_geo_system);
			}

			bit_not(jobinfo->units_used);
			tmp_char = ba_node_map_ranged_hostlist(
				jobinfo->units_used, ba_mp_geo_system);
			bit_not(total_bitmap);
			tmp_char2 = ba_node_map_ranged_hostlist(
				total_bitmap, ba_mp_geo_system);
			info("ba_sub_block_in_record_clear: "
			     "cleared cnodes %s on mp %s, making '%s' "
			     "on this midplane usable in this block (%s), "
			     "%s are in Software Failure",
			     tmp_char, ba_mp->coord_str, tmp_char2,
			     bg_record->bg_block_id, tmp_char3);
			xfree(tmp_char);
			xfree(tmp_char2);
			xfree(tmp_char3);
			FREE_NULL_BITMAP(total_bitmap);
		}
		break;
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern void ba_sync_job_to_block(bg_record_t *bg_record,
				 struct job_record *job_ptr)
{
	struct step_record *step_ptr;
	ListIterator itr;
	ba_mp_t *ba_mp;
	select_jobinfo_t *jobinfo, *step_jobinfo;

	xassert(bg_record);
	xassert(job_ptr);

	if (bg_record->job_list) {
		if (!find_job_in_bg_record(bg_record, job_ptr->job_id)) {
			ba_mp = list_peek(bg_record->ba_mp_list);
			list_append(bg_record->job_list, job_ptr);
			jobinfo = job_ptr->select_jobinfo->data;
			/* If you were switching from no sub-block
			   allocations to allowing it, the units_avail
			   wouldn't be around for any jobs, but no
			   problem since they were always the size of
			   the block.
			*/
			if (!jobinfo->units_avail) {
				jobinfo->units_avail =
					bit_copy(ba_mp->cnode_bitmap);
				bit_not(jobinfo->units_avail);
			}

			/* Since we are syncing this information lets
			   clear out the old stuff. (You need to use
			   the jobinfo->units_avail here instead of
			   ba_mp->cnode_bitmap because the above trick
			   only works when coming from a system where
			   no sub-block allocation was allowed.)
			*/
			FREE_NULL_BITMAP(jobinfo->units_used);
			jobinfo->units_used = bit_copy(jobinfo->units_avail);
			bit_not(jobinfo->units_used);
			if (bit_overlap(ba_mp->cnode_bitmap,
					jobinfo->units_avail)) {
				error("we have an overlapping job allocation "
				      "(%u) mp %s", job_ptr->job_id,
				      ba_mp->coord_str);
			}
			bit_or(ba_mp->cnode_bitmap, jobinfo->units_avail);
			/* info("%s now has %d left", ba_mp->coord_str, */
			/*      bit_clear_count(ba_mp->cnode_bitmap)); */
			itr = list_iterator_create(job_ptr->step_list);
			while ((step_ptr = list_next(itr))) {
				step_jobinfo = step_ptr->select_jobinfo->data;
				if (bit_overlap(jobinfo->units_used,
						step_jobinfo->units_avail)) {
					error("we have an overlapping step "
					      "(%u.%u) mp %s", job_ptr->job_id,
					      step_ptr->step_id,
					      ba_mp->coord_str);
				}
				bit_or(jobinfo->units_used,
				       step_jobinfo->units_avail);
				/* info("allocation %u now has %d left", */
				/*      job_ptr->job_id, */
				/*      bit_clear_count(jobinfo->units_used));*/
			}
			list_iterator_destroy(itr);
		}
	} else {
		ListIterator ba_itr = NULL;

		bg_record->job_running = job_ptr->job_id;
		bg_record->job_ptr = job_ptr;

		itr = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = list_next(itr))) {
			struct node_record *node_ptr;
			int node_inx;

			jobinfo = step_ptr->select_jobinfo->data;
			if (jobinfo->cnode_cnt == bg_record->cnode_cnt)
				continue;

			if (!ba_itr)
				ba_itr = list_iterator_create(
					bg_record->ba_mp_list);
			else
				list_iterator_reset(ba_itr);

			if (!(node_ptr = find_node_record(
				      step_ptr->step_layout->node_list))) {
				error("can't find midplane %s",
				      step_ptr->step_layout->node_list);
				continue;
			}
			node_inx = node_ptr - node_record_table_ptr;
			while ((ba_mp = list_next(ba_itr))) {
				if (node_inx != ba_mp->index)
					continue;
				if (!ba_mp->cnode_bitmap) {
					ba_mp->cnode_bitmap =
						ba_create_ba_mp_cnode_bitmap(
							bg_record);
					FREE_NULL_BITMAP(
						ba_mp->cnode_usable_bitmap);
					ba_mp->cnode_usable_bitmap =
						bit_copy(ba_mp->cnode_bitmap);
				}
				if (!ba_mp->cnode_err_bitmap)
					ba_mp->cnode_err_bitmap = bit_alloc(
						bg_conf->mp_cnode_cnt);
				if (bit_overlap(ba_mp->cnode_bitmap,
						jobinfo->units_used)) {
					error("we have an overlapping step "
					      "(%u.%u) mp %s", job_ptr->job_id,
					      step_ptr->step_id,
					      ba_mp->coord_str);
				}
				bit_or(ba_mp->cnode_bitmap,
				       jobinfo->units_used);
				break;
			}
		}
		list_iterator_destroy(itr);
		if (ba_itr)
			list_iterator_destroy(ba_itr);
	}
}

extern bitstr_t *ba_create_ba_mp_cnode_bitmap(bg_record_t *bg_record)
{
	int start, end, ionode_num;
	char *tmp_char = NULL, *tmp_char2;
	bitstr_t *cnode_bitmap = bit_alloc(bg_conf->mp_cnode_cnt);

	if (!bg_record->ionode_bitmap
	    || ((start = bit_ffs(bg_record->ionode_bitmap)) == -1))
		return cnode_bitmap;

	end = bit_fls(bg_record->ionode_bitmap);
	for (ionode_num = start; ionode_num <= end; ionode_num++) {
		int nc_num, nc_start, nc_end;
		if (!bit_test(bg_record->ionode_bitmap, ionode_num))
			continue;

		nc_start = ionode_num * (int)bg_conf->nc_ratio;
		nc_end = nc_start + (int)bg_conf->nc_ratio;
		for (nc_num = nc_start; nc_num < nc_end; nc_num++)
			/* this should always be true */
			(void)ba_node_map_set_range(cnode_bitmap,
						    g_nc_coords[nc_num].start,
						    g_nc_coords[nc_num].end,
						    ba_mp_geo_system);
	}

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		tmp_char = ba_node_map_ranged_hostlist(cnode_bitmap,
						       ba_mp_geo_system);

	bit_not(cnode_bitmap);

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
		tmp_char2 = ba_node_map_ranged_hostlist(cnode_bitmap,
							ba_mp_geo_system);
		info("ba_create_ba_mp_cnode_bitmap: can only use %s cnodes of "
		     "this midplane leaving %s unusable", tmp_char, tmp_char2);
		xfree(tmp_char);
		xfree(tmp_char2);
	}

	return cnode_bitmap;
}

extern bitstr_t *ba_cnodelist2bitmap(char *cnodelist)
{
	char *cnode_name;
	hostlist_t hl;
	bitstr_t *cnode_bitmap = bit_alloc(bg_conf->mp_cnode_cnt);
	int coord[ba_mp_geo_system->dim_count], dim = 0;

	if (!cnodelist)
		return cnode_bitmap;

	if (!(hl = hostlist_create_dims(
		      cnodelist, ba_mp_geo_system->dim_count))) {
		FREE_NULL_BITMAP(cnode_bitmap);
		error("ba_cnodelist2bitmap: couldn't create a hotlist from "
		      "cnodelist given %s", cnodelist);
		return NULL;
	}

	while ((cnode_name = hostlist_shift_dims(
			hl, ba_mp_geo_system->dim_count))) {
		for (dim = 0; dim < ba_mp_geo_system->dim_count; dim++) {
			if (!cnode_name[dim])
				break;
			coord[dim] = select_char2coord(cnode_name[dim]);
		}
		free(cnode_name);

		if (dim != ba_mp_geo_system->dim_count)
			break;

		if (ba_node_map_set_range(cnode_bitmap, coord, coord,
					  ba_mp_geo_system) == -1) {
			/* failure */
			dim = 0;
			break;
		}
	}
	hostlist_destroy(hl);

	if (dim != ba_mp_geo_system->dim_count) {
		FREE_NULL_BITMAP(cnode_bitmap);
		error("ba_cnodelist2bitmap: bad cnodelist given %s", cnodelist);
		return NULL;
	}

	bit_not(cnode_bitmap);

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
		char *tmp_char = ba_node_map_ranged_hostlist(cnode_bitmap,
							     ba_mp_geo_system);
		info("ba_cnodelist2bitmap: %s translates to %s inverted",
		     cnodelist, tmp_char);
		xfree(tmp_char);
	}

	return cnode_bitmap;
}

extern void ba_set_ionode_str(bg_record_t *bg_record)
{
	int ionode_num, coords[5];
	hostlist_t hl;
	bool set_small = 0;

	if (!bg_record->ionode_bitmap
	    || bit_ffs(bg_record->ionode_bitmap) == -1)
		return;

	hl = hostlist_create_dims("", 5);

	for (ionode_num = bit_ffs(bg_record->ionode_bitmap);
	     ionode_num <= bit_fls(bg_record->ionode_bitmap);
	     ionode_num++) {
		int nc_num, nc_start, nc_end;

		if (!bit_test(bg_record->ionode_bitmap, ionode_num))
			continue;

		nc_start = ionode_num * (int)bg_conf->nc_ratio;

		if (!set_small) {
			int dim;
			set_small = 1;
			for (dim = 0; dim<5; dim++)
				bg_record->start_small[dim] =
					g_nc_coords[nc_start].start[dim];
		}

		nc_end = nc_start + (int)bg_conf->nc_ratio;

		for (nc_num = nc_start; nc_num < nc_end; nc_num++) {
			if (_ba_set_ionode_str_internal(
				    0, coords,
				    g_nc_coords[nc_num].start,
				    g_nc_coords[nc_num].end,
				    hl)
			    == -1) {
				hostlist_destroy(hl);
				hl = NULL;
				return;
			}
		}
	}

	bg_record->ionode_str = hostlist_ranged_string_xmalloc_dims(hl, 5, 0);
	//info("iostring is %s", bg_record->ionode_str);
	hostlist_destroy(hl);
	hl = NULL;
}

/* Check to see if a job has been added to the bg_record NO_VAL
 * returns the first one on the list. */
extern struct job_record *ba_remove_job_in_block_job_list(
	bg_record_t *bg_record, struct job_record *in_job_ptr)
{
	ListIterator itr;
	struct job_record *job_ptr = NULL;
	select_jobinfo_t *jobinfo;
	ba_mp_t *ba_mp;
	char *tmp_char = NULL, *tmp_char2 = NULL, *tmp_char3 = NULL;
	bool bad_magic = 0;
	bitstr_t *used_cnodes = NULL;

	xassert(bg_record);

	if (!bg_record->job_list)
		return NULL;

	ba_mp = list_peek(bg_record->ba_mp_list);
	xassert(ba_mp);

	if (in_job_ptr && in_job_ptr->magic != JOB_MAGIC) {
		/* This can happen if the mmcs job hangs out in the system
		 * forever, or at least gets cleared a after the SLURM
		 * job is out of the controller.
		 */
		bad_magic = 1;
		used_cnodes = bit_copy(ba_mp->cnode_bitmap);
		/* Take out the part (if any) of the midplane that
		   isn't part of the block.
		*/
		bit_not(ba_mp->cnode_usable_bitmap);
		bit_and(used_cnodes, ba_mp->cnode_usable_bitmap);
		bit_not(ba_mp->cnode_usable_bitmap);
	}
again:
	itr = list_iterator_create(bg_record->job_list);
	while ((job_ptr = list_next(itr))) {
		if (job_ptr->magic != JOB_MAGIC) {
			error("on block %s we found a job with bad magic",
			      bg_record->bg_block_id);
			list_delete_item(itr);
			continue;
		} else if (bad_magic) {
			jobinfo = job_ptr->select_jobinfo->data;
			if (!jobinfo->units_avail) {
				error("ba_remove_job_in_block_job_list: "
				      "no units avail bitmap on the jobinfo, "
				      "continuing");
				continue;
			}
			bit_not(jobinfo->units_avail);
			bit_and(used_cnodes, jobinfo->units_avail);
			bit_not(jobinfo->units_avail);

			continue;
		}

		if (!in_job_ptr) {
			/* if there is not an in_job_ptr it is because
			   the jobs finished while the slurmctld
			   wasn't running and somehow the state was
			   messed up.  So the cpus were never added to
			   the mix, so don't remove them. This should
			   probably never happen.
			*/
			//num_unused_cpus += job_ptr->total_cpus;
			list_delete_item(itr);
			continue;
		}

		if (job_ptr == in_job_ptr) {
			num_unused_cpus += job_ptr->total_cpus;
			list_delete_item(itr);
			break;
		}
	}
	list_iterator_destroy(itr);

	if (!in_job_ptr) {
		if (ba_mp->cnode_usable_bitmap) {
			FREE_NULL_BITMAP(ba_mp->cnode_bitmap);
			ba_mp->cnode_bitmap =
				bit_copy(ba_mp->cnode_usable_bitmap);
		} else if (ba_mp->cnode_bitmap)
			bit_nclear(ba_mp->cnode_bitmap, 0,
				   bit_size(ba_mp->cnode_bitmap)-1);
		return NULL;
	} else if (!job_ptr && !bad_magic) {
		/* If the job was not found reset the block with the
		   running jobs and go from there.
		*/
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			error("ba_remove_job_in_block_job_list: "
			      "Couldn't remove sub-block job %u from "
			      "block %s",
			      in_job_ptr->job_id, bg_record->bg_block_id);
		}
		bad_magic = 1;
		if ((bg_record->conn_type[0] >= SELECT_SMALL)
		    && ba_mp->cnode_usable_bitmap) {
			bit_not(ba_mp->cnode_usable_bitmap);
			used_cnodes = bit_copy(ba_mp->cnode_usable_bitmap);
			bit_not(ba_mp->cnode_usable_bitmap);
		} else
			used_cnodes = bit_copy(ba_mp->cnode_bitmap);
		goto again;
	}

	if (bad_magic) {
		uint32_t current_cnode_cnt = bit_set_count(used_cnodes);

		num_unused_cpus += current_cnode_cnt * bg_conf->cpu_ratio;

		bit_not(used_cnodes);
		bit_and(ba_mp->cnode_bitmap, used_cnodes);
		bit_not(used_cnodes);
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			debug("ba_remove_job_in_block_job_list: "
			      "Removing old sub-block job using %d cnodes "
			      "from block %s",
			      current_cnode_cnt, bg_record->bg_block_id);
		}
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE) {
			debug("ba_remove_job_in_block_job_list: "
			      "Removing sub-block job %u from block %s",
			      job_ptr->job_id, bg_record->bg_block_id);
		}

		jobinfo = job_ptr->select_jobinfo->data;

		if (!jobinfo->units_avail) {
			error("ba_remove_job_in_block_job_list: "
			      "no units avail bitmap on the jobinfo");
			return job_ptr;
		}
		used_cnodes = jobinfo->units_avail;
	}

	bit_not(used_cnodes);
	bit_and(ba_mp->cnode_bitmap, used_cnodes);
	bit_not(used_cnodes);

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_ALGO) {
		bitstr_t *total_bitmap = bit_copy(ba_mp->cnode_bitmap);
		if (ba_mp->cnode_err_bitmap) {
			bit_or(total_bitmap, ba_mp->cnode_err_bitmap);
			tmp_char3 = ba_node_map_ranged_hostlist(
				ba_mp->cnode_err_bitmap,
				ba_mp_geo_system);
		}

		tmp_char = ba_node_map_ranged_hostlist(
			used_cnodes, ba_mp_geo_system);
		bit_not(total_bitmap);
		tmp_char2 = ba_node_map_ranged_hostlist(
			total_bitmap, ba_mp_geo_system);
		info("ba_remove_job_in_block_job_list: "
		     "cleared cnodes %s on mp %s, making '%s' "
		     "on this midplane usable in this block (%s), "
		     "%s are in Software Failure",
		     tmp_char, ba_mp->coord_str, tmp_char2,
		     bg_record->bg_block_id, tmp_char3);
		xfree(tmp_char);
		xfree(tmp_char2);
		xfree(tmp_char3);
		FREE_NULL_BITMAP(total_bitmap);
	}

	if (bad_magic)
		FREE_NULL_BITMAP(used_cnodes);

	return job_ptr;
}

extern int ba_translate_coord2nc(uint16_t *cnode_coords)
{
	int nc_loc, dim, match;
	/* need to figure out which nodeboard this cnode is in */
	for (nc_loc=0; nc_loc<16; nc_loc++) {
		match = 0;
		for (dim = 0; dim < 5; dim++) {
			if ((cnode_coords[dim]
			     >= g_nc_coords[nc_loc].start[dim])
			    && (cnode_coords[dim]
				<= g_nc_coords[nc_loc].end[dim]))
				match++;
		}
		if (match == 5)
			break;
	}
	xassert(nc_loc < 16);
	return nc_loc;
}

/* ba_system_mutex needs to be locked before calling this. */
extern ba_mp_t *ba_inx2ba_mp(int inx)
{
	return ba_main_grid_array[inx];
}

static char *_copy_from_main(List main_mps, List ret_list)
{
	ListIterator itr;
	ba_mp_t *ba_mp;
	ba_mp_t *new_mp;
	int dim;
	char *name = NULL;
	hostlist_t hostlist = NULL;

	if (!main_mps || !ret_list)
		return NULL;

	if (!(itr = list_iterator_create(main_mps)))
		fatal("NULL itr returned");
	while ((ba_mp = list_next(itr))) {
		if (!(ba_mp->used & BA_MP_USED_ALTERED)) {
			error("_copy_from_main: it appears we "
			      "have a mp %s added that wasn't altered %d",
			      ba_mp->coord_str, ba_mp->used);
			continue;
		}

		new_mp = ba_copy_mp(ba_mp);
		list_append(ret_list, new_mp);
		/* copy and reset the path */
		memcpy(new_mp->axis_switch, new_mp->alter_switch,
		       sizeof(ba_mp->axis_switch));
		memset(new_mp->alter_switch, 0, sizeof(new_mp->alter_switch));
		if (new_mp->used & BA_MP_USED_PASS_BIT) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("_copy_from_main: "
				     "mp %s is used for passthrough",
				     new_mp->coord_str);
			new_mp->used = BA_MP_USED_FALSE;
		} else {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("_copy_from_main: "
				     "mp %s is used", new_mp->coord_str);
			new_mp->used = BA_MP_USED_TRUE;
			if (hostlist)
				hostlist_push_host(hostlist, new_mp->coord_str);
			else
				hostlist = hostlist_create(new_mp->coord_str);
		}

		/* reset the main mp */
		ba_mp->used &= (~BA_MP_USED_ALTERED_PASS);
		memset(ba_mp->alter_switch, 0, sizeof(ba_mp->alter_switch));
		/* Take this away if we decide we don't want
		   this to setup the main list.
		*/
		/* info("got usage of %s %d %d", new_mp->coord_str, */
		/*      new_mp->used, ba_mp->used); */
		for (dim=0; dim<cluster_dims; dim++) {
			ba_mp->axis_switch[dim].usage |=
				new_mp->axis_switch[dim].usage;
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				debug("_copy_from_main: dim %d is %s added %s",
				      dim,
				      ba_switch_usage_str(
					      ba_mp->axis_switch[dim].usage),
				      ba_switch_usage_str(
					      new_mp->axis_switch[dim].usage));
		}
	}
	list_iterator_destroy(itr);

	if (hostlist) {
		name = hostlist_ranged_string_xmalloc(hostlist);
		hostlist_destroy(hostlist);
	}

	return name;
}

static char *_reset_altered_mps(List main_mps, bool get_name)
{
	ListIterator itr = NULL;
	ba_mp_t *ba_mp;
	char *name = NULL;
	hostlist_t hostlist = NULL;

	xassert(main_mps);

	if (!(itr = list_iterator_create(main_mps)))
		fatal("got NULL list iterator");
	while ((ba_mp = list_next(itr))) {
		if (!(ba_mp->used & BA_MP_USED_ALTERED)) {
			error("_reset_altered_mps: it appears we "
			      "have a mp %s added that wasn't altered",
			      ba_mp->coord_str);
			continue;
		}

		if (ba_mp->used & BA_MP_USED_PASS_BIT) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_reset_altered_mps: "
				     "mp %s is used for passthrough %d",
				     ba_mp->coord_str, ba_mp->used);
		} else {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_reset_altered_mps: "
				     "mp %s is used %d", ba_mp->coord_str,
				     ba_mp->used);
			if (get_name) {
				if (hostlist)
					hostlist_push_host(hostlist,
						      ba_mp->coord_str);
				else
					hostlist = hostlist_create(
						ba_mp->coord_str);
			}
		}

		ba_mp->used &= (~BA_MP_USED_ALTERED_PASS);
		memset(ba_mp->alter_switch, 0, sizeof(ba_mp->alter_switch));
	}
	list_iterator_destroy(itr);

	if (hostlist) {
		name = hostlist_ranged_string_xmalloc(hostlist);
		hostlist_destroy(hostlist);
	}

	return name;
}

static int _check_deny_pass(int dim)
{
	if (!deny_pass || !*deny_pass)
		return 0;

	switch (dim) {
	case A:
		*deny_pass |= PASS_FOUND_A;
		if (*deny_pass & PASS_DENY_A) {
			debug("We don't allow A passthoughs");
			return 1;
		}
		break;
	case X:
		*deny_pass |= PASS_FOUND_X;
		if (*deny_pass & PASS_DENY_X) {
			debug("We don't allow X passthoughs");
			return 1;
		}
		break;
	case Y:
		*deny_pass |= PASS_FOUND_Y;
		if (*deny_pass & PASS_DENY_Y) {
			debug("We don't allow Y passthoughs");
			return 1;
		}
		break;
	case Z:
		*deny_pass |= PASS_FOUND_Z;
		if (*deny_pass & PASS_DENY_Z) {
			debug("We don't allow Z passthoughs");
			return 1;
		}
		break;
	default:
		error("unknown dim %d", dim);
		return 1;
		break;
	}
	return 0;
}

static int _fill_in_wires(List mps, ba_mp_t *start_mp, int dim,
			  uint16_t geometry, uint16_t conn_type,
			  bool full_check)
{
	ba_mp_t *curr_mp = start_mp->next_mp[dim];
	ba_switch_t *axis_switch = NULL;
	ba_switch_t *alter_switch = NULL;
	int count = 1;
	int add = 0;

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		info("_fill_in_wires: at mp %s(%d) geo %d switches "
		     "at %s and %s",
		     start_mp->coord_str, dim, geometry,
		     ba_switch_usage_str(start_mp->axis_switch[dim].usage),
		     ba_switch_usage_str(start_mp->alter_switch[dim].usage));

	alter_switch = &start_mp->alter_switch[dim];

	if (_mp_out_used(start_mp, dim))
		return 0;

	alter_switch->usage |= BG_SWITCH_OUT;
	alter_switch->usage |= BG_SWITCH_OUT_PASS;

	while (curr_mp != start_mp) {
		add = 0;
		xassert(curr_mp);
		axis_switch = &curr_mp->axis_switch[dim];
		alter_switch = &curr_mp->alter_switch[dim];

		/* This should never happen since we got here
		   from an unused mp */
		if (axis_switch->usage & BG_SWITCH_IN_PASS) {
			info("_fill_in_wires: got a bad axis_switch "
			     "at %s %d %s %s",
			     curr_mp->coord_str, dim,
			     ba_switch_usage_str(axis_switch->usage),
			     ba_switch_usage_str(alter_switch->usage));
			xassert(0);
		}

		if ((count < geometry)
		    && (curr_mp->used & BA_MP_USED_ALTERED)) {
			/* if (curr_mp->coord[dim] < start_mp->coord[dim]) { */
			/* 	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) */
			/* 		info("Available mp %s(%d) is less " */
			/* 		     "than our starting point " */
			/* 		     "of %s(%d) since we already " */
			/* 		     "looked at this return.", */
			/* 		     curr_mp->coord_str, dim, */
			/* 		     start_mp->coord_str, dim); */
			/* 	return 0; */
			/* } */
			count++;
			alter_switch->usage |= BG_SWITCH_IN_PASS;
			alter_switch->usage |= BG_SWITCH_IN;
			if ((count < geometry) || (conn_type == SELECT_TORUS)) {
				if (_mp_out_used(curr_mp, dim))
					return 0;
				alter_switch->usage |= BG_SWITCH_OUT;
				alter_switch->usage |= BG_SWITCH_OUT_PASS;
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("_fill_in_wires: using mp %s(%d) "
					     "%d(%d) %s added %s",
					     curr_mp->coord_str, dim,
					     count, geometry,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
			} else if (conn_type == SELECT_MESH) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("_fill_in_wires: using mp %s(%d) "
					     "%d(%d) %s added %s",
					     curr_mp->coord_str, dim,
					     count, geometry,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				return 1;
			}
		} else if (!_mp_out_used(curr_mp, dim)
			   && !_check_deny_pass(dim)) {

			if (!full_check
			    && bridge_check_nodeboards(curr_mp->loc)) {
				if (ba_debug_flags
				    & DEBUG_FLAG_BG_ALGO_DEEP) {
					info("_fill_in_wires: can't "
					     "use mp %s(%d) "
					     "as passthrough it has "
					     "nodeboards not available",
					     curr_mp->coord_str, dim);
				}
				return 0;
			}
			if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
				add = 1;
				curr_mp->used |= BA_MP_USED_ALTERED_PASS;
			} else {
				error("WHAT? %s", curr_mp->coord_str);
			}
			alter_switch->usage |= BG_SWITCH_PASS;
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
				if (count == geometry) {
					info("_fill_in_wires: using mp %s(%d) "
					     "to finish torus %s added %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				} else {
					info("_fill_in_wires: using mp %s(%d) "
					     "as passthrough %s added %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				}
			}
		} else {
			/* we can't use this so return with a nice 0 */
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_fill_in_wires: we can't use this "
				     "so return");
			return 0;
		}

		if (add)
			list_append(mps, curr_mp);
		curr_mp = curr_mp->next_mp[dim];
	}

	if (count != geometry)
		return 0;

	if (curr_mp == start_mp) {
		axis_switch = &curr_mp->axis_switch[dim];
		alter_switch = &curr_mp->alter_switch[dim];
		/* This should never happen since we got here
		   from an unused mp */
		if (axis_switch->usage & BG_SWITCH_IN_PASS) {
			info("_fill_in_wires: 2 got a bad axis_switch "
			     "at %s %d %s",
			     curr_mp->coord_str, dim,
			     ba_switch_usage_str(axis_switch->usage));
			xassert(0);
		}

		alter_switch->usage |= BG_SWITCH_IN_PASS;
		alter_switch->usage |= BG_SWITCH_IN;
	}

	return 1;
}

static void _setup_next_mps(int level, uint16_t *coords)
{
	ba_mp_t *curr_mp;
	uint16_t next_coords[SYSTEM_DIMENSIONS];
	uint16_t prev_coords[SYSTEM_DIMENSIONS];
	int dim;

	if (level > cluster_dims)
		return;

	if (level < cluster_dims) {
		for (coords[level] = 0;
		     coords[level] < DIM_SIZE[level];
		     coords[level]++) {
			/* handle the outer dims here */
			_setup_next_mps(level+1, coords);
		}
		return;
	}
	curr_mp = coord2ba_mp(coords);
	if (!curr_mp)
		return;
	for (dim = 0; dim < cluster_dims; dim++) {
		memcpy(next_coords, coords, sizeof(next_coords));
		memcpy(prev_coords, coords, sizeof(prev_coords));
		if (next_coords[dim] < (DIM_SIZE[dim]-1))
			next_coords[dim]++;
		else
			next_coords[dim] = 0;

		if (prev_coords[dim] > 0)
			prev_coords[dim]--;
		else
			prev_coords[dim] = DIM_SIZE[dim]-1;
		curr_mp->next_mp[dim] = coord2ba_mp(next_coords);
		curr_mp->prev_mp[dim] = coord2ba_mp(prev_coords);
	}
}

/* Used to set up the next nodecard we are going to look at.  Setting
 * mp_coords to 00000 each time this is called will increment
 * mp_coords to the next starting point of the next nodecard.
 */
static void _increment_nc_coords(int dim, int *mp_coords, int *dim_size)
{
	if (dim >= 5)
		return;

	mp_coords[ba_nc_dim_order[dim]]+=2;
	if (mp_coords[ba_nc_dim_order[dim]] >= dim_size[ba_nc_dim_order[dim]]) {
		mp_coords[ba_nc_dim_order[dim]] = 0;
		_increment_nc_coords(dim+1, mp_coords, dim_size);
	}
}

/*
 * Used to check if midplane is usable in the block we are creating
 *
 * IN: ba_mp - mp to check if is used
 * IN: dim - dimension we are checking.
  */
static bool _mp_used(ba_mp_t* ba_mp, int dim)
{
	xassert(ba_mp);

	/* if we've used this mp in another block already */
	if (mp_strip_unaltered(ba_mp->used)
	    || (ba_mp->axis_switch[dim].usage & BG_SWITCH_WRAPPED)
	    || (ba_mp->alter_switch[dim].usage & BG_SWITCH_WRAPPED)) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("mp %s(%d) used (%d, %s/%s)",
			     ba_mp->coord_str, dim,
			     mp_strip_unaltered(ba_mp->used),
			     ba_switch_usage_str(
				     ba_mp->axis_switch[dim].usage),
			     ba_switch_usage_str(
				     ba_mp->alter_switch[dim].usage));
		return true;
	}
	return false;
}

/*
 * Used to check if we can leave a midplane
 *
 * IN: ba_mp - mp to check if is used
 * IN: dim - dimension we are checking.
 */
static bool _mp_out_used(ba_mp_t* ba_mp, int dim)
{
	xassert(ba_mp);

	/* If the mp is already used just check the PASS_USED. */
	if ((ba_mp->axis_switch[dim].usage & BG_SWITCH_CABLE_ERROR_SET)
	    || (ba_mp->axis_switch[dim].usage & BG_SWITCH_OUT_PASS)
	    || (ba_mp->alter_switch[dim].usage & BG_SWITCH_OUT_PASS)) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("mp %s(%d) has passthroughs used (%s)",
			     ba_mp->coord_str, dim, ba_switch_usage_str(
				     ba_mp->axis_switch[dim].usage));
		return true;
	}

	return false;
}

static uint16_t _find_distance(uint16_t start, uint16_t end, int dim)
{
	/* If we started at a position that requires us to wrap around
	 * make sure we add the 1 to end to get the correct relative
	 * position.
	 */
	if (end < start) {
		return (((DIM_SIZE[dim]-1) - start) + (end+1)) * 4;
	} else
		return (end - start) * 4;
}

static void _find_distance_ba_mp(
	ba_mp_t *curr_mp, ba_mp_t *end_mp, int dim, uint16_t *distance)
{
	xassert(curr_mp);

	if ((*distance) > DIM_SIZE[dim]) {
		error("Whoa, we are higher than we can possibly go, this "
		      "should never happen.  If it does you will get an "
		      "error with your srun.");
		(*distance) = 0;
		return;
	}

	if (curr_mp->coord[dim] == end_mp->coord[dim]) {
		(*distance) *= 4;
		return;
	}

	if (curr_mp->used)
		(*distance)++;

	_find_distance_ba_mp(curr_mp->next_mp[dim], end_mp, dim, distance);
}

static int _ba_set_ionode_str_internal(int level, int *coords,
				       int *start_offset, int *end_offset,
				       hostlist_t hl)
{
	char tmp_char[6];

	xassert(hl);

	if (level > 5)
		return -1;

	if (level < 5) {
		for (coords[level] = start_offset[level];
		     coords[level] <= end_offset[level];
		     coords[level]++) {
			/* handle the outter dims here */
			if (_ba_set_ionode_str_internal(
				    level+1, coords,
				    start_offset, end_offset,
				    hl) == -1)
				return -1;
		}
		return 1;
	}
	snprintf(tmp_char, sizeof(tmp_char), "%c%c%c%c%c",
		 alpha_num[coords[0]],
		 alpha_num[coords[1]],
		 alpha_num[coords[2]],
		 alpha_num[coords[3]],
		 alpha_num[coords[4]]);
	hostlist_push_host_dims(hl, tmp_char, 5);
	return 1;
}

static bitstr_t *_find_sub_block(ba_geo_table_t **in_geo_table,
				 uint16_t *start_loc, bitstr_t *total_bitmap,
				 uint32_t node_count)
{
	int cnt = 0;
	bitstr_t *found_bits = NULL;
	uint32_t clear_cnt = bit_clear_count(total_bitmap);
	ba_geo_table_t *geo_table = *in_geo_table;

	if (clear_cnt < node_count) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("ba_pick_sub_block: only have %d avail need %d",
			     clear_cnt, node_count);
		return NULL;
	}

	while (geo_table) {
		int scan_offset = 0;

		/* FIXME: In the current IBM API it doesn't
		   allow wrapping inside the midplane.  In the
		   future this will change.  When that happens
		   there will need to be a flag that is sent
		   here instead of always true.
		*/
		if (ba_geo_test_all(total_bitmap,
				    &found_bits,
				    geo_table, &cnt,
				    ba_mp_geo_system, NULL,
				    start_loc, &scan_offset, true)
		    == SLURM_SUCCESS) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
				int dim;
				info("scan_offset=%d", scan_offset);
				for (dim = 0;
				     dim < ba_mp_geo_system->dim_count;
				     dim++) {
					info("start_loc[%d]=%u geometry[%d]=%u",
					     dim, start_loc[dim], dim,
					     geo_table->geometry[dim]);
				}
			}
			break;
		}
		geo_table = geo_table->next_ptr;
	}

	*in_geo_table = geo_table;

	return found_bits;
}

static ba_geo_table_t *_find_geo_table(uint32_t orig_node_count,
				       uint32_t *node_count,
				       uint32_t total_count)
{
	ba_geo_table_t *geo_table = NULL;

	while (!(geo_table = ba_mp_geo_system->geo_table_ptr[*node_count])) {
		debug2("_find_geo_table: No geometries of size %u ",
		       *node_count);
		(*node_count)++;
		if (*node_count > total_count)
			break;
	}
	if (*node_count > total_count) {
		debug("_find_geo_table: requested sub-block larger "
		      "than block");
		return NULL;
	}

	if (orig_node_count != *node_count)
		debug("_find_geo_table: user requested %u nodes, "
		      "but that can't make a block, giving them %d",
		      orig_node_count, *node_count);

	if (!geo_table) {
		/* This should never happen */
		error("_find_geo_table: "
		      "Couldn't place this job size %u tried up to "
		      "the full size of the block (%u)",
		      orig_node_count, total_count);
		return NULL;
	}

	return geo_table;
}

