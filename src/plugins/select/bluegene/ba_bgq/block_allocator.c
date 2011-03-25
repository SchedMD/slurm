/*****************************************************************************\
 *  block_allocator.c - Assorted functions for layout of bgq blocks,
 *	 wiring, mapping for smap, etc.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#define DEBUG_PA
#define BEST_COUNT_INIT 20

#define mp_strip_unaltered(__mp) (__mp & ~BA_MP_USED_ALTERED_PASS)

/* _ba_system is the "current" system that the structures will work
 *  on */
ba_mp_t ****ba_main_grid = NULL;

static int REAL_DIM_SIZE[HIGHEST_DIMENSIONS] = {0,0,0,0};
static ba_geo_system_t *ba_main_geo_system = NULL;
static ba_geo_system_t *ba_mp_geo_system = NULL;
static uint16_t *deny_pass = NULL;

/** internal helper functions */
/* */
static int _check_for_options(select_ba_request_t* ba_request);

/* */
static int _fill_in_coords(List results, int level, ba_mp_t *start_mp,
			   ba_mp_t **check_mp, int *block_start,
			   int *block_end, int *coords);

static int _finish_torus(List results, int level, uint16_t *block_start,
			 int *block_end, uint16_t *conn_type, int *coords);

/* */
static char *_copy_from_main(List main_mps, List ret_list);

/* */
static char *_reset_altered_mps(List main_mps, bool get_name);

/* */
static int _copy_ba_switch(ba_mp_t *ba_mp, ba_mp_t *orig_mp, int dim);

/* */
static int _check_deny_pass(int dim);

/* */
static int _find_path(List mps, ba_mp_t *start_mp, int dim,
		      uint16_t geometry, uint16_t conn_type,
		      int *block_start, int *block_end);

/* */
static void _setup_next_mps(int level, int *coords);

/** */
static bool _mp_used(ba_mp_t* ba_mp, int dim);

/** */
static bool _mp_out_used(ba_mp_t* ba_mp, int dim);

/* */
/* static int _find_passthrough(ba_switch_t *curr_switch, int source_port,  */
/* 			     List mps, int dim,  */
/* 			     int count, int highest_phys_x);  */
/* */

/* */
/* static int _set_one_dim(uint16_t *start, uint16_t *end, uint16_t *coord); */

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

extern ba_mp_t *coord2ba_mp(const int *coord)
{
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
	uint16_t start[cluster_dims];
	char *name=NULL;
	int i, dim, startx;
	ba_geo_table_t *ba_geo_table;
	bool found = false;

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

	memset(start, 0, sizeof(start));
	startx = (start[X]-1);

	if (startx == -1)
		startx = DIM_SIZE[X]-1;
	if (ba_request->start_req) {
		for (dim = 0; dim < cluster_dims; dim++) {
			if (ba_request->start[dim] >= DIM_SIZE[dim])
				return 0;
			start[dim] = ba_request->start[dim];
		}
	}

	/* set up the geo_table */
	if (ba_request->geometry[0] == (uint16_t)NO_VAL) {
		if (!(ba_request->geo_table =
		      ba_main_geo_system->geo_table_ptr[ba_request->size])) {
			error("allocate_block: "
			      "No geometries for %d midplanes",
			      ba_request->size);
			return 0;
		}
		ba_geo_table = (ba_geo_table_t *)ba_request->geo_table;
		if (!ba_geo_table || !ba_geo_table->geometry) {
			error("allocate_block: no geo table");
			return 0;
		}

		memcpy(ba_request->geometry, ba_geo_table->geometry,
		       sizeof(ba_geo_table->geometry));
	} else
		ba_request->geo_table = NULL;

start_again:
	i = 0;
	if (i == startx)
		i = startx-1;
	while (i != startx) {
		i++;
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("allocate_block: finding %c%c%c%c try %d",
			     alpha_num[ba_request->geometry[A]],
			     alpha_num[ba_request->geometry[X]],
			     alpha_num[ba_request->geometry[Y]],
			     alpha_num[ba_request->geometry[Z]],
			     i);
	new_mp:
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("allocate_block: starting at %c%c%c%c",
			     alpha_num[start[A]],
			     alpha_num[start[X]],
			     alpha_num[start[Y]],
			     alpha_num[start[Z]]);

		if ((name = set_bg_block(results, start,
					 ba_request->geometry,
					 ba_request->conn_type))) {
			ba_request->save_name = name;
			name = NULL;
			return 1;
		}

		/* If there was an error set_bg_block resets the
		   results list */
		/* if (results && list_count(results)) { */
		/* 	bool is_small = 0; */
		/* 	if (ba_request->conn_type[0] == SELECT_SMALL) */
		/* 		is_small = 1; */
		/* 	remove_block(results, is_small); */
		/* 	list_flush(results); */
		/* } */

		if (ba_request->start_req) {
			info("start asked for ");
			goto requested_end;
		}
		//exit(0);
		debug2("allocate_block: trying something else");

		found = false;
		for (dim = cluster_dims-1; dim >= 0; dim--) {
			start[dim]++;
			if (start[dim] < DIM_SIZE[dim]) {
				found = true;
				break;
			}
			start[dim] = 0;
		}
		if (!found) {
			if (ba_request->size == 1)
				goto requested_end;
			if (!_check_for_options(ba_request))
				return 0;
			else {
				memset(start, 0, sizeof(start));
				goto start_again;
			}
		}
		goto new_mp;
	}

requested_end:
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

	itr = list_iterator_create(mps);
	while ((curr_ba_mp = (ba_mp_t*) list_next(itr))) {
		/* since the list that comes in might not be pointers
		   to the main list we need to point to that main list */
		ba_mp = &ba_main_grid
			[curr_ba_mp->coord[A]]
			[curr_ba_mp->coord[X]]
			[curr_ba_mp->coord[Y]]
			[curr_ba_mp->coord[Z]];
		if (curr_ba_mp->used)
			ba_mp->used &= (~BA_MP_USED_TRUE);
		ba_mp->used &= (~BA_MP_USED_ALTERED_PASS);

		/* Small blocks don't use wires, and only have 1 mp,
		   so just break. */
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("remove_block: %s state now %d",
			     ba_mp->coord_str, ba_mp->used);

		for (dim=0; dim<cluster_dims; dim++) {
			if (curr_ba_mp == ba_mp) {
				/* Remove the usage that was altered */
				/* info("remove_block: %s(%d) %s removing %s", */
				/*      ba_mp->coord_str, dim, */
				/*      ba_switch_usage_str( */
				/* 	     ba_mp->axis_switch[dim].usage), */
				/*      ba_switch_usage_str( */
				/* 	     ba_mp->alter_switch[dim].usage)); */
				ba_mp->axis_switch[dim].usage &=
					(~ba_mp->alter_switch[dim].usage);
				/* info("remove_block: %s(%d) is now at %s", */
				/*      ba_mp->coord_str, dim, */
				/*      ba_switch_usage_str( */
				/* 	     ba_mp->axis_switch[dim].usage)); */
			} else if (curr_ba_mp->axis_switch[dim].usage
				   != BG_SWITCH_NONE) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("remove_block: 2 %s(%d) %s %s "
					     "removing %s",
					     ba_mp->coord_str, dim,
					     curr_ba_mp->coord_str,
					     ba_switch_usage_str(
						     ba_mp->axis_switch
						     [dim].usage),
					     ba_switch_usage_str(
						     curr_ba_mp->axis_switch
						     [dim].usage));
				/* Just remove the usage set here */
				ba_mp->axis_switch[dim].usage &=
					(~curr_ba_mp->axis_switch[dim].usage);
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
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

	itr = list_iterator_create(mps);
	while ((ba_mp = list_next(itr))) {
		/* info("checking %c%c%c", */
/* 		     ba_mp->coord[X],  */
/* 		     ba_mp->coord[Y], */
/* 		     ba_mp->coord[Z]); */

		curr_ba_mp = &ba_main_grid
			[ba_mp->coord[A]]
			[ba_mp->coord[X]]
			[ba_mp->coord[Y]]
			[ba_mp->coord[Z]];

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

		if (ba_mp->used)
			curr_ba_mp->used = ba_mp->used;
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
	return rc;
}

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
 *
 * RET char * - hostlist of midplanes results represent must be
 *     xfreed.  NULL on failure
 */
extern char *set_bg_block(List results, uint16_t *start,
			  uint16_t *geometry, uint16_t *conn_type)
{
	List main_mps = NULL;
	char *name = NULL;
	ba_mp_t* ba_mp = NULL;
	ba_mp_t *check_mp[cluster_dims];
	int size = 1, dim;
	int block_start[cluster_dims];
	int block_end[cluster_dims];
	int coords[cluster_dims];
	uint16_t local_deny_pass = ba_deny_pass;

	if (!ba_initialized){
		error("Error, configuration not initialized, "
		      "calling ba_init(NULL, 1)");
		ba_init(NULL, 1);
	}

	if (cluster_dims == 1) {
		if (start[A] >= DIM_SIZE[A])
			return NULL;
		size = geometry[X];
		ba_mp = &ba_main_grid[start[A]][0][0][0];
	} else {
		for (dim=0; dim<cluster_dims; dim++) {
			if (start[dim] >= DIM_SIZE[dim])
				return NULL;
			if (geometry[dim] <= 0) {
				error("problem with geometry of %c in dim %d, "
				      "needs to be at least 1",
				      alpha_num[geometry[dim]], dim);
				return NULL;
			}
			size *= geometry[dim];
		}

		ba_mp = &ba_main_grid[start[A]][start[X]][start[Y]][start[Z]];
		/* info("looking at %s", ba_mp->coord_str); */
	}

	if (!ba_mp)
		goto end_it;

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		info("trying mp %s %c%c%c%c %d",
		     ba_mp->coord_str,
		     alpha_num[geometry[A]],
		     alpha_num[geometry[X]],
		     alpha_num[geometry[Y]],
		     alpha_num[geometry[Z]],
		     conn_type[A]);

	/* check just the first dim to see if this node is used for
	   anything just yet. */
	if (_mp_used(ba_mp, 0))
		goto end_it;

	if (conn_type[A] >= SELECT_SMALL) {
		/* adding the ba_mp and end, we could go through the
		 * regular logic here, but this is just faster. */
		if (results) {
			ba_mp = ba_copy_mp(ba_mp);
			/* We need to have this node wrapped in Q to handle
			   wires correctly when creating around the midplane.
			*/
			ba_setup_mp(ba_mp, false, true);
			ba_mp->used = BA_MP_USED_TRUE;
			list_append(results, ba_mp);
		}
		name = xstrdup(ba_mp->coord_str);
		goto end_it;
	}

	main_mps = list_create(NULL);

	ba_mp->used |= BA_MP_USED_ALTERED;
	list_append(main_mps, ba_mp);

	if (!deny_pass)
		deny_pass = &local_deny_pass;

	/* set the end to the start and the _find_path will increase each dim.*/
	for (dim=0; dim<cluster_dims; dim++) {
		block_start[dim] = start[dim];
		block_end[dim] = start[dim];
		if (!_find_path(main_mps, ba_mp, dim, geometry[dim],
				conn_type[dim], &block_start[dim],
				&block_end[dim])) {
			goto end_it;
		}
	}

	//ba_mp = coord2ba_mp(block_start);

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
		info("complete box is %c%c%c%c x %c%c%c%c",
		     alpha_num[block_start[A]],
		     alpha_num[block_start[X]],
		     alpha_num[block_start[Y]],
		     alpha_num[block_start[Z]],
		     alpha_num[block_end[A]],
		     alpha_num[block_end[X]],
		     alpha_num[block_end[Y]],
		     alpha_num[block_end[Z]]);

	if (_fill_in_coords(main_mps, A, ba_mp, check_mp,
			    block_start, block_end, coords) == -1)
		goto end_it;

	if (_finish_torus(main_mps, A, start,
			  block_end, conn_type, coords) == -1)
		goto end_it;

	/* Success */
	if (results)
		name = _copy_from_main(main_mps, results);
	else
		name = _reset_altered_mps(main_mps, 1);

end_it:

	if (main_mps) {
		/* handle failure */
		if (!name)
			_reset_altered_mps(main_mps, 0);
		list_destroy(main_mps);
		main_mps = NULL;
	}

	if (name)
		debug2("name = %s", name);
	else
		debug2("can't allocate");

	if (deny_pass == &local_deny_pass)
		deny_pass = NULL;

	return name;
}

/*
 * Resets the virtual system to a virgin state.  If track_down_mps is set
 * then those midplanes are not set to idle, but kept in a down state.
 */
extern void reset_ba_system(bool track_down_mps)
{
	int a, x, y, z;

	for (a = 0; a < DIM_SIZE[A]; a++)
		for (x = 0; x < DIM_SIZE[X]; x++)
			for (y = 0; y < DIM_SIZE[Y]; y++)
				for (z = 0; z < DIM_SIZE[Z]; z++) {
					ba_mp_t *ba_mp = &ba_main_grid
						[a][x][y][z];
					ba_setup_mp(ba_mp, track_down_mps,
						    false);
				}
}

/*
 * set values of every grid point (used in smap)
 */
extern void init_grid(node_info_msg_t * node_info_ptr)
{
	int i = 0, j, a, x, y, z;
	ba_mp_t *ba_mp = NULL;

	if (!node_info_ptr) {
		for (a = 0; a < DIM_SIZE[A]; a++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				for (y = 0; y < DIM_SIZE[Y]; y++) {
					for (z = 0; z < DIM_SIZE[Z]; z++) {
						ba_mp = &ba_main_grid
							[a][x][y][z];
						ba_mp->state = NODE_STATE_IDLE;
						ba_mp->index = i++;
					}
				}
			}
		}
		return;
	}

	for (j = 0; j < (int)node_info_ptr->record_count; j++) {
		int coord[cluster_dims];
		node_info_t *node_ptr = &node_info_ptr->node_array[j];
		if (!node_ptr->name)
			continue;

		memset(coord, 0, sizeof(coord));
		if (cluster_dims == 1) {
			coord[0] = j;
		} else {
			if ((i = strlen(node_ptr->name)) < cluster_dims)
				continue;
			for (x=0; x<cluster_dims; x++)
				coord[x] = select_char2coord(
					node_ptr->name[i-(cluster_dims+x)]);
		}

		for (x=0; x<cluster_dims; x++)
			if (coord[x] < 0)
				break;
		if (x < cluster_dims)
			continue;

		ba_mp = &ba_main_grid[coord[A]][coord[X]][coord[Y]][coord[Z]];
		ba_mp->index = j;
		if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr)) {
			if (ba_initialized)
				ba_update_mp_state(
					ba_mp, node_ptr->node_state);
		}
		ba_mp->state = node_ptr->node_state;
	}
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

/*
 * This function is here to check options for rotating and elongating
 * and set up the request based on the count of each option
 */
static int _check_for_options(select_ba_request_t* ba_request)
{
	ba_geo_table_t *ba_geo_table;

	if (ba_request->geo_table) {
		ba_geo_table = ba_request->geo_table;
		ba_request->geo_table =	ba_geo_table->next_ptr;
	}

	if (ba_request->geo_table) {
		ba_geo_table = ba_request->geo_table;
		memcpy(ba_request->geometry, ba_geo_table->geometry,
		       sizeof(ba_geo_table->geometry));
		/* info("now trying %c%c%c%c", */
		/*      alpha_num[ba_request->geometry[A]], */
		/*      alpha_num[ba_request->geometry[X]], */
		/*      alpha_num[ba_request->geometry[Y]], */
		/*      alpha_num[ba_request->geometry[Z]]); */
		return 1;
	}
	return 0;
}

/*
 * Fill in the paths and extra midplanes we need for the block.
 * Basically copy the starting coords sent in starting at block_start
 * ending with block_end in every midplane for the block.  This
 * function does not finish torus' (use _finish_torus for that).
 *
 * IN/OUT results - total list of midplanes after this function
 *        returns successfully.
 * IN level - which dimension we are on.  Since this is a recursive
 *        function calls to this function should always be 'A' when
 *        starting.
 * IN start_mp - starting location of the block, should be the ba_mp
 *        from the block_start.
 * IN block_start - starting point of the block.
 * IN block_end - ending point of the block.
 * IN coords - Where we are recursively. So this should just be an
 *        uninitialized int [SYSTEM_DIMENSIONS]
 *
 * RET: -1 on failure 1 on success
 */
static int _fill_in_coords(List results, int level, ba_mp_t *start_mp,
			   ba_mp_t **check_mp, int *block_start,
			   int *block_end, int *coords)
{
	int dim;
	int count_outside = 0;
	uint16_t used = 0;
	ba_mp_t *curr_mp;

	if (level > cluster_dims)
		return -1;

	if (level < cluster_dims) {
		check_mp[level] = start_mp;
		coords[level] = start_mp->coord[level];
		do {
			/* handle the outter dims here */
			if (_fill_in_coords(
				    results, level+1, start_mp,
				    check_mp, block_start, block_end,
				    coords) == -1)
				return -1;
			if (check_mp[level]->alter_switch[level].usage
			    & BG_SWITCH_OUT_PASS)
				check_mp[level] =
					check_mp[level]->next_mp[level];
			else {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("mp %s(%d) isn't connected "
					     "anymore, we found the end.",
					     check_mp[level]->coord_str, level);
				return 0;
			}
			if (coords[level] < (DIM_SIZE[level]-1))
				coords[level]++;
			else
				coords[level] = 0;
		} while (coords[level] != start_mp->coord[level]);
		return 1;
	}

	curr_mp = &ba_main_grid[coords[A]][coords[X]][coords[Y]][coords[Z]];

	/* info("looking at %s", curr_mp->coord_str); */
	for (dim=0; dim<cluster_dims; dim++) {
		/* If we get over 2 in any dim that we are
		   greater here we are pass anything we need to
		   passthrough, so break.
		*/
		if (check_mp[dim]->used & BA_MP_USED_PASS_BIT)
			used = check_mp[dim]->used;

		/* info("inside at %s %d %d %d", check_mp[dim]->coord_str, */
		/*      dim, check_mp[dim]->used, used); */

		/* info("passthrough %d used %d %d %d %d", dim, used, */
		/*      curr_mp->coord[dim], block_start[dim], */
		/*      block_end[dim]); */
		if ((curr_mp->coord[dim] < block_start[dim])
		    || (curr_mp->coord[dim] > block_end[dim])) {
			count_outside++;
			/* info("yes under %d", count_outside); */
			if (count_outside > 1)
				break;
		}
	}

	/* info("got used of %d", used); */
	if (count_outside > 1) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("skipping non-used %s", curr_mp->coord_str);
		return 1;
	}

	for (dim=0; dim<cluster_dims; dim++) {
		int rc;

		/* If we are passing though skip all except the
		   actual passthrough dim.
		*/
		if ((used & BA_MP_USED_PASS_BIT)
		    && (check_mp[dim]->used != used)) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("skipping here %s(%d)",
				     curr_mp->coord_str, dim);
			continue;
		}

		/* ba_mp_t *orig_mp = check_mp[dim]; */
		/* ba_mp_t *ba_mp = curr_mp; */
		/* info("looking to put " */
		/*      "mp %s(%d) %s onto mp %s(%d) %s", */
		/*      orig_mp->coord_str, dim, */
		/*      ba_switch_usage_str(orig_mp->alter_switch[dim].usage),*/
		/*      ba_mp->coord_str, dim, */
		/*      ba_switch_usage_str(ba_mp->alter_switch[dim].usage)); */

		/* if 1 is returned we haven't visited this mp yet,
		   and need to add it to the list
		*/
		if ((rc = _copy_ba_switch(curr_mp, check_mp[dim], dim)) == -1)
			return rc;
		else if (rc == 1)
			list_append(results, curr_mp);
	}
	return 1;
}

/*
 * Finish wiring a block together given start and end points.  All
 * used nodes should be marked inside those points before this
 * function is called.
 *
 * IN/OUT results - total list of midplanes after this function
 *        returns successfully.
 * IN level - which dimension we are on.  Since this is a recursive
 *        function calls to this function should always be 'A' when
 *        starting.
 * IN block_start - starting point of the block.
 * IN block_end - ending point of the block.
 * IN conn_type - Mesh or Torus for each Dim.
 * IN coords - Where we are recursively. So this should just be an
 *        uninitialized int [SYSTEM_DIMENSIONS]
 *
 * RET: -1 on failure 1 on success
 */
static int _finish_torus(List results, int level, uint16_t *block_start,
			 int *block_end, uint16_t *conn_type, int *coords)
{
	int dim;
	ba_mp_t *curr_mp, *start_mp;

	if (level > cluster_dims)
		return -1;

	if (level < cluster_dims) {
		for (coords[level] = block_start[level];
		     coords[level] <= block_end[level];
		     coords[level]++) {
			/* handle the outter dims here */
			if (_finish_torus(
				    results, level+1,
				    block_start, block_end,
				    conn_type, coords) == -1)
				return -1;
		}
		return 1;
	}

	curr_mp = &ba_main_grid[coords[A]][coords[X]][coords[Y]][coords[Z]];
	if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("_finish_torus: skipping non-used %s",
			     curr_mp->coord_str);
		return 1;
	}
	start_mp = curr_mp;

	/* info("_finish_torus: starting with %s", */
	/*      curr_mp->coord_str); */

	for (dim=0; dim<cluster_dims; dim++) {
		if (conn_type[dim] != SELECT_TORUS)
			continue;
		if (!(start_mp->alter_switch[dim].usage & BG_SWITCH_OUT_PASS)) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("finish_torus: mp %s(%d) already "
				     "terminated",
				     curr_mp->coord_str, dim);
			continue;
		}
		curr_mp = start_mp->next_mp[dim];
		while (curr_mp != start_mp) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_finish_torus: looking at %s(%d)",
				     curr_mp->coord_str, dim);
			if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
				ba_switch_t *axis_switch =
					&curr_mp->axis_switch[dim];
				ba_switch_t *alter_switch =
					&curr_mp->alter_switch[dim];
				if (axis_switch->usage & BG_SWITCH_PASS_USED) {
					info("_finish_torus: got a bad "
					     "axis_switch at "
					     "%s(%d) %s %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
					xassert(0);
				}
				alter_switch->usage |= BG_SWITCH_PASS;
				curr_mp->used |= BA_MP_USED_ALTERED_PASS;
				list_append(results, curr_mp);

				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("_finish_torus: using mp %s(%d) "
					     "to finish torus %s added %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
			} else if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_finish_torus: skipping already "
				     "set %s(%d) %s",
				     curr_mp->coord_str, dim,
				     ba_switch_usage_str(
					     curr_mp->alter_switch[dim].usage));
			curr_mp = curr_mp->next_mp[dim];
		}
		/* info("_finish_torus: ended with %s(%d)", */
		/*      curr_mp->coord_str, dim); */
	}

	return 1;
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
				hostlist_push(hostlist, new_mp->coord_str);
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
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_copy_from_main: dim %d is %s", dim,
				     ba_switch_usage_str(
					     ba_mp->axis_switch[dim].usage));
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
					hostlist_push(hostlist,
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

static int _copy_ba_switch(ba_mp_t *ba_mp, ba_mp_t *orig_mp, int dim)
{
	int rc = 0;
	if (ba_mp->alter_switch[dim].usage != BG_SWITCH_NONE) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("_copy_ba_switch: "
			     "switch already set %s(%d)",
			     ba_mp->coord_str, dim);
		return 0;
	}

	if (orig_mp->alter_switch[dim].usage == BG_SWITCH_NONE) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("_copy_ba_switch: "
			     "switch not needed %s(%d)",
			     ba_mp->coord_str, dim);
		return 0;
	}

	if ((orig_mp->used & BA_MP_USED_PASS_BIT)
	    || (ba_mp->used & BA_MP_USED_PASS_BIT)) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("_copy_ba_switch: "
			     "pass bit set %d %d",
			     orig_mp->alter_switch[dim].usage
			     & BG_SWITCH_PASS_FLAG,
			     ba_mp->alter_switch[dim].usage
			     & BG_SWITCH_PASS_FLAG);
		if (!(orig_mp->alter_switch[dim].usage & BG_SWITCH_PASS_FLAG)) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("_copy_ba_switch: "
				     "skipping %s(%d)", ba_mp->coord_str, dim);
			return 0;
		}
	} else if (_mp_used(ba_mp, dim)) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("_copy_ba_switch: "
			     "%s is already used", ba_mp->coord_str);
		return -1;
	}

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		info("_copy_ba_switch: "
		     "mapping %s(%d) %s to %s(%d) %s",
		     orig_mp->coord_str, dim,
		     ba_switch_usage_str(orig_mp->alter_switch[dim].usage),
		     ba_mp->coord_str, dim,
		     ba_switch_usage_str(ba_mp->alter_switch[dim].usage));

	if (ba_mp->axis_switch[dim].usage & orig_mp->alter_switch[dim].usage) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("_copy_ba_switch: "
			     "can't use %s(%d) switch %s "
			     "overlapped with request %s",
			     ba_mp->coord_str, dim,
			     ba_switch_usage_str(
				     ba_mp->axis_switch[dim].usage),
			     ba_switch_usage_str(
				     orig_mp->alter_switch[dim].usage));
		return -1;
	}

	/* If we return 1 it means we haven't yet looked at this
	 * midplane so add it to the list */
	if (!(ba_mp->used & BA_MP_USED_ALTERED))
		rc = 1;

	/* set up the usage of the midplane */
	if (orig_mp->used & BA_MP_USED_PASS_BIT)
		ba_mp->used |= BA_MP_USED_ALTERED_PASS;
	else
		ba_mp->used |= BA_MP_USED_ALTERED;

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		info("_copy_ba_switch: "
		     "mp %s(%d) adds %s to mp %s(%d) %s %d",
		     orig_mp->coord_str, dim,
		     ba_switch_usage_str(orig_mp->alter_switch[dim].usage),
		     ba_mp->coord_str, dim,
		     ba_switch_usage_str(ba_mp->alter_switch[dim].usage),
		     ba_mp->used);
	ba_mp->alter_switch[dim].usage |= orig_mp->alter_switch[dim].usage;

	return rc;
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

static int _find_path(List mps, ba_mp_t *start_mp, int dim,
		      uint16_t geometry, uint16_t conn_type,
		      int *block_start, int *block_end)
{
	ba_mp_t *curr_mp = start_mp->next_mp[dim];
	ba_switch_t *axis_switch = NULL;
	ba_switch_t *alter_switch = NULL;
	int count = 1;
	int add = 0;

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		info("_find_path: at mp %s(%d) geo %d switches at %s and %s",
		     start_mp->coord_str, dim, geometry,
		     ba_switch_usage_str(start_mp->axis_switch[dim].usage),
		     ba_switch_usage_str(start_mp->alter_switch[dim].usage));

	if (_mp_used(start_mp, dim))
		return 0;

	axis_switch = &start_mp->axis_switch[dim];
	alter_switch = &start_mp->alter_switch[dim];
	if (geometry == 1) {
		/* Always check MESH here since we only care about the
		   IN/OUT ports.
		*/
		start_mp->used |= BA_MP_USED_ALTERED;
		/* all 1 dimensions need a TORUS */
		alter_switch->usage |= BG_SWITCH_WRAPPED;
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("_find_path: using mp %s(%d) in 1 geo %s added %s",
			     start_mp->coord_str, dim,
			     ba_switch_usage_str(axis_switch->usage),
			     ba_switch_usage_str(alter_switch->usage));
		return 1;
	}
	if (_mp_out_used(start_mp, dim))
		return 0;
	start_mp->used |= BA_MP_USED_ALTERED;
	alter_switch->usage |= BG_SWITCH_OUT;
	alter_switch->usage |= BG_SWITCH_OUT_PASS;

	while (curr_mp != start_mp) {
		xassert(curr_mp);
		axis_switch = &curr_mp->axis_switch[dim];
		alter_switch = &curr_mp->alter_switch[dim];

		/* This should never happen since we got here
		   from an unused mp */
		if (axis_switch->usage & BG_SWITCH_IN_PASS) {
			info("_find_path: got a bad axis_switch at %s %d %s %s",
			     curr_mp->coord_str, dim,
			     ba_switch_usage_str(axis_switch->usage),
			     ba_switch_usage_str(alter_switch->usage));
			xassert(0);
		}

		if ((count < geometry) && !_mp_used(curr_mp, dim)) {
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
			if (curr_mp->coord[dim] < *block_start)
				*block_start = curr_mp->coord[dim];

			if (curr_mp->coord[dim] > *block_end)
				*block_end = curr_mp->coord[dim];
			count++;
			if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
				add = 1;
				curr_mp->used |= BA_MP_USED_ALTERED;
			}
			alter_switch->usage |= BG_SWITCH_IN_PASS;
			alter_switch->usage |= BG_SWITCH_IN;
			if ((count < geometry) || (conn_type == SELECT_TORUS)) {
				alter_switch->usage |= BG_SWITCH_OUT;
				alter_switch->usage |= BG_SWITCH_OUT_PASS;
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("_find_path: using mp %s(%d) "
					     "%d(%d) %s added %s",
					     curr_mp->coord_str, dim,
					     count, geometry,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
			} else if (conn_type == SELECT_MESH) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("_find_path: using mp %s(%d) "
					     "%d(%d) %s added %s",
					     curr_mp->coord_str, dim,
					     count, geometry,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				if (add)
					list_append(mps, curr_mp);
				return 1;
			}
		} else if (!_mp_out_used(curr_mp, dim)
			   && !_check_deny_pass(dim)) {
			if (!(curr_mp->used & BA_MP_USED_ALTERED)) {
				add = 1;
				curr_mp->used |= BA_MP_USED_ALTERED_PASS;
			}
			alter_switch->usage |= BG_SWITCH_PASS;
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP) {
				if (count == geometry) {
					info("_find_path: using mp %s(%d) to "
					     "finish torus %s added %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				} else {
					info("_find_path: using mp %s(%d) as "
					     "passthrough %s added %s",
					     curr_mp->coord_str, dim,
					     ba_switch_usage_str(
						     axis_switch->usage),
					     ba_switch_usage_str(
						     alter_switch->usage));
				}
			}
		} else {
			/* we can't use this so return with a nice 0 */
			info("_find_path: we can't use this so return");
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
			info("_find_path: 2 got a bad axis_switch at %s %d %s",
			     curr_mp->coord_str, dim,
			     ba_switch_usage_str(axis_switch->usage));
			xassert(0);
		}

		alter_switch->usage |= BG_SWITCH_IN_PASS;
		alter_switch->usage |= BG_SWITCH_IN;
	}

	return 1;
}

static void _setup_next_mps(int level, int *coords)
{
	ba_mp_t *curr_mp;
	int next_coords[SYSTEM_DIMENSIONS];
	int prev_coords[SYSTEM_DIMENSIONS];
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

extern void ba_create_system(int num_cpus, int *real_dims)
{
	int a,x,y,z;
	int coords[SYSTEM_DIMENSIONS];

	if (ba_main_grid)
		ba_destroy_system();

	memcpy(REAL_DIM_SIZE, real_dims, sizeof(REAL_DIM_SIZE));

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
				}
			}
		}
	}

	/* build all the possible geos for the mid planes */
	ba_main_geo_system =  xmalloc(sizeof(ba_geo_system_t));
	ba_main_geo_system->dim_count = SYSTEM_DIMENSIONS;
	ba_main_geo_system->dim_size = xmalloc(sizeof(int) *
					  ba_main_geo_system->dim_count);
	memcpy(ba_main_geo_system->dim_size, DIM_SIZE, sizeof(DIM_SIZE));
	ba_create_geo_table(ba_main_geo_system);
	//ba_print_geo_table(ba_main_geo_system);

	/* build all the possible geos for a sub block inside a mid plane */
	ba_mp_geo_system =  xmalloc(sizeof(ba_geo_system_t));
	ba_mp_geo_system->dim_count = 5;
	ba_mp_geo_system->dim_size = xmalloc(sizeof(int) *
					  ba_mp_geo_system->dim_count);
	/* These will never change. */
	ba_mp_geo_system->dim_size[0] = 4;
	ba_mp_geo_system->dim_size[1] = 4;
	ba_mp_geo_system->dim_size[2] = 4;
	ba_mp_geo_system->dim_size[3] = 4;
	ba_mp_geo_system->dim_size[4] = 2;
	ba_create_geo_table(ba_mp_geo_system);
	//ba_print_geo_table(ba_mp_geo_system);

	_setup_next_mps(A, coords);
}

/** */
extern void ba_destroy_system(void)
{
	int a, x, y;

	if (ba_main_grid) {
		for (a=0; a<DIM_SIZE[A]; a++) {
			for (x = 0; x < DIM_SIZE[X]; x++) {
				for (y = 0; y < DIM_SIZE[Y]; y++)
					xfree(ba_main_grid[a][x][y]);
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
	if ((ba_mp->axis_switch[dim].usage & BG_SWITCH_PASS_USED)
	    || (ba_mp->alter_switch[dim].usage & BG_SWITCH_PASS_USED)) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("mp %s(%d) has passthroughs used (%s)",
			     ba_mp->coord_str, dim, ba_switch_usage_str(
				     ba_mp->axis_switch[dim].usage));
		return true;
	}

	return false;
}

