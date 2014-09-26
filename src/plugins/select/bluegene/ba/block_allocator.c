/*****************************************************************************\
 *  block_allocator.c - Assorted functions for layout of bluegene blocks,
 *	 wiring, mapping for smap, etc.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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
#include "src/common/slurmdb_defs.h"
#include "src/common/timers.h"
#include "src/common/uid.h"

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
	uint16_t geometry[HIGHEST_DIMENSIONS];
	int in;
	int out;
} ba_path_switch_t;


#define DEBUG_PA
#define BEST_COUNT_INIT 20

/* Global */
bool _initialized = false;
bool _wires_initialized = false;
bool _mp_map_initialized = false;

/* _ba_system is the "current" system that the structures will work
 *  on */
List path = NULL;
List best_path = NULL;
int best_count;
uint16_t *deny_pass = NULL;
char *p = '\0';

/* extern Global */
my_bluegene_t *bg = NULL;
ba_mp_t ***ba_main_grid = NULL;

typedef enum {
	BLOCK_ALGO_FIRST,
	BLOCK_ALGO_SECOND
} block_algo_t;

/** internal helper functions */

/* */
static int _check_for_options(select_ba_request_t* ba_request);

/* */
static int _append_geo(uint16_t *geo, List geos, int rotate);

/* */
static int _fill_in_coords(List results, List start_list,
			   uint16_t *geometry, int conn_type);

/* */
static int _copy_the_path(List nodes, ba_switch_t *curr_switch,
			  ba_switch_t *mark_switch,
			  int source, int dim);

/* */
static int _find_yz_path(ba_mp_t *ba_node, uint16_t *first,
			 uint16_t *geometry, int conn_type);

#ifndef HAVE_BG_FILES
/* */
static int _emulate_ext_wiring(ba_mp_t ***grid);
#endif

/** */
static int _reset_the_path(ba_switch_t *curr_switch, int source,
			   int target, int dim);
/* */
static void _delete_path_list(void *object);

/* find the first block match in the system */
static int _find_match(select_ba_request_t* ba_request, List results);

/** */
static bool _node_used(ba_mp_t* ba_node, int x_size);

/* */
static void _switch_config(ba_mp_t* source, ba_mp_t* target, int dim,
			   int port_src, int port_tar);

/* */
static int _set_external_wires(int dim, int count, ba_mp_t* source,
			       ba_mp_t* target);

/* */
static char *_set_internal_wires(List nodes, int size, int conn_type);

/* */
static int _find_x_path(List results, ba_mp_t *ba_node, uint16_t *start,
			int x_size, int found, int conn_type,
			block_algo_t algo);

/* */
static int _remove_node(List results, uint16_t *mp_tar);

/* */
static int _find_next_free_using_port_2(ba_switch_t *curr_switch,
					int source_port,
					List nodes, int dim,
					int count);
/* */
/* static int _find_passthrough(ba_switch_t *curr_switch, int source_port,  */
/* 			     List nodes, int dim,  */
/* 			     int count, int highest_phys_x);  */
/* */
static int _finish_torus(List results,
			 ba_switch_t *curr_switch, int source_port,
			 int dim, int count, uint16_t *start);
/* */
static uint16_t *_set_best_path();

/* */
static int _set_one_dim(uint16_t *start, uint16_t *end, uint16_t *coord);

/* */
static void _destroy_geo(void *object);

extern void destroy_ba_node(void *ptr)
{
	ba_mp_t *ba_node = (ba_mp_t *)ptr;
	if (ba_node) {
		xfree(ba_node->loc);
		xfree(ba_node);
	}
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
 * IN - avail_node_bitmap: bitmap of usable midplanes.
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
	float sz=1;
	int i2, picked, total_sz=1, size2=0;
	uint16_t *geo_ptr;
	int messed_with = 0;
	int checked[DIM_SIZE[X]];
	uint16_t geo[cluster_dims];

	memset(geo, 0, sizeof(geo));
	ba_request->save_name= NULL;
	ba_request->rotate_count= 0;
	ba_request->elongate_count = 0;
	ba_request->elongate_geos = list_create(_destroy_geo);
	memcpy(geo, ba_request->geometry, sizeof(geo));

	if (ba_request->deny_pass == (uint16_t)NO_VAL)
		ba_request->deny_pass = ba_deny_pass;

	if (!(cluster_flags & CLUSTER_FLAG_BG)) {
		if (geo[X] != (uint16_t)NO_VAL) {
			for (i=0; i<cluster_dims; i++) {
				if ((geo[i] < 1) || (geo[i] > DIM_SIZE[i])) {
					error("new_ba_request Error, "
					      "request geometry is invalid %d",
					      geo[i]);
					return 0;
				}
			}
			ba_request->size = ba_request->geometry[X];
		} else if (ba_request->size) {
			ba_request->geometry[X] = ba_request->size;
		} else
			return 0;
		return 1;
	}

	if (geo[X] != (uint16_t)NO_VAL) {
		for (i=0; i<cluster_dims; i++){
			if ((geo[i] < 1) || (geo[i] > DIM_SIZE[i])) {
				error("new_ba_request Error, "
				      "request geometry is invalid dim %d "
				      "can't be %c, largest is %c",
				      i,
				      alpha_num[geo[i]],
				      alpha_num[DIM_SIZE[i]]);
				return 0;
			}
		}
		_append_geo(geo, ba_request->elongate_geos, 0);
		sz=1;
		for (i=0; i<cluster_dims; i++)
			sz *= ba_request->geometry[i];
		ba_request->size = sz;
		sz=0;
	}

	deny_pass = &ba_request->deny_pass;

	if (ba_request->elongate || sz) {
		sz=1;
		/* decompose the size into a cubic geometry */
		ba_request->rotate= 1;
		ba_request->elongate = 1;

		for (i=0; i<cluster_dims; i++) {
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}

		if (ba_request->size==1) {
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
			goto endit;
		}

		if (ba_request->size<=DIM_SIZE[Y]) {
			geo[X] = 1;
			geo[Y] = ba_request->size;
			geo[Z] = 1;
			sz=ba_request->size;
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		}

		i = ba_request->size/4;
		if (!(ba_request->size%2)
		    && i <= DIM_SIZE[Y]
		    && i <= DIM_SIZE[Z]
		    && i*i == ba_request->size) {
			geo[X] = 1;
			geo[Y] = i;
			geo[Z] = i;
			sz=ba_request->size;
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		}

		if (ba_request->size > total_sz || ba_request->size < 1) {
			return 0;
		}
		sz = ba_request->size % (DIM_SIZE[Y] * DIM_SIZE[Z]);
		if (!sz) {
			i = ba_request->size / (DIM_SIZE[Y] * DIM_SIZE[Z]);
			geo[X] = i;
			geo[Y] = DIM_SIZE[Y];
			geo[Z] = DIM_SIZE[Z];
			sz=ba_request->size;
			if ((geo[X]*geo[Y]*geo[Z]) == ba_request->size)
				_append_geo(geo,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			else
				error("%d I was just trying to add a "
				      "geo of %d%d%d "
				      "while I am trying to request "
				      "%d midplanes",
				      __LINE__, geo[X], geo[Y], geo[Z],
				      ba_request->size);
		}
//	startagain:
		picked=0;
		for(i=0; i<DIM_SIZE[X]; i++)
			checked[i]=0;

		for (i=0; i<cluster_dims; i++) {
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}

		sz = 1;
		picked=0;
	tryagain:
		size2 = ba_request->size;
		//messedup:
		for (i=picked; i<cluster_dims; i++) {
			if (size2 <= 1)
				break;

			sz = size2 % DIM_SIZE[i];
			if (!sz) {
				geo[i] = DIM_SIZE[i];
				size2 /= DIM_SIZE[i];
			} else if (size2 > DIM_SIZE[i]) {
				for(i2=(DIM_SIZE[i]-1); i2 > 1; i2--) {
					/* go through each number to see if
					   the size is divisable by a smaller
					   number that is
					   good in the other dims. */
					if (!(size2%i2) && !checked[i2]) {
						size2 /= i2;

						if (i==0)
							checked[i2]=1;

						if (i2<DIM_SIZE[i]) {
							geo[i] = i2;
						} else {
							goto tryagain;
						}
						if ((i2-1)!=1 &&
						    i!=(cluster_dims-1))
							break;
					}
				}
				/* This size can not be made into a
				   block return.  If you want to try
				   until we find the next largest block
				   uncomment the code below and the goto
				   above. If a user specifies a max
				   node count the job will never
				   run.
				*/
				if (i2==1) {
					if (!list_count(
						    ba_request->elongate_geos))
						error("Can't make a block of "
						      "%d into a cube.",
						      ba_request->size);
					goto endit;
/* 					ba_request->size +=1; */
/* 					goto startagain; */
				}
			} else {
				geo[i] = sz;
				break;
			}
		}

		if ((geo[X]*geo[Y]) <= DIM_SIZE[Y]) {
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[X] * geo[Y];
			ba_request->geometry[Z] = geo[Z];
			_append_geo(ba_request->geometry,
				    ba_request->elongate_geos,
				    ba_request->rotate);

		}
		if ((geo[X]*geo[Z]) <= DIM_SIZE[Y]) {
			ba_request->geometry[X] = 1;
			ba_request->geometry[Y] = geo[Y];
			ba_request->geometry[Z] = geo[X] * geo[Z];
			_append_geo(ba_request->geometry,
				    ba_request->elongate_geos,
				    ba_request->rotate);

		}

		/* Make sure geo[X] is even and then see if we can get
		   it into the Y or Z dim. */
		if (!(geo[X]%2) && ((geo[X]/2) <= DIM_SIZE[Y])) {
			if (geo[Y] == 1) {
				ba_request->geometry[Y] = geo[X]/2;
				messed_with = 1;
			} else
				ba_request->geometry[Y] = geo[Y];
			if (!messed_with && geo[Z] == 1) {
				messed_with = 1;
				ba_request->geometry[Z] = geo[X]/2;
			} else
				ba_request->geometry[Z] = geo[Z];
			if (messed_with) {
				messed_with = 0;
				ba_request->geometry[X] = 2;
				_append_geo(ba_request->geometry,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			}
		}
		if (geo[X] == DIM_SIZE[X]
		    && (geo[Y] < DIM_SIZE[Y]
			|| geo[Z] < DIM_SIZE[Z])) {
			if (DIM_SIZE[Y]<DIM_SIZE[Z]) {
				i = DIM_SIZE[Y];
				DIM_SIZE[Y] = DIM_SIZE[Z];
				DIM_SIZE[Z] = i;
			}
			ba_request->geometry[X] = geo[X];
			ba_request->geometry[Y] = geo[Y];
			ba_request->geometry[Z] = geo[Z];
			if (ba_request->geometry[Y] < DIM_SIZE[Y]) {
				i = (DIM_SIZE[Y] - ba_request->geometry[Y]);
				ba_request->geometry[Y] +=i;
			}
			if (ba_request->geometry[Z] < DIM_SIZE[Z]) {
				i = (DIM_SIZE[Z] - ba_request->geometry[Z]);
				ba_request->geometry[Z] +=i;
			}
			for(i = DIM_SIZE[X]; i>0; i--) {
				ba_request->geometry[X]--;
				i2 = (ba_request->geometry[X]
				      * ba_request->geometry[Y]
				      * ba_request->geometry[Z]);
				if (i2 < ba_request->size) {
					ba_request->geometry[X]++;
					messed_with = 1;
					break;
				}
			}
			if (messed_with) {
				messed_with = 0;
				_append_geo(ba_request->geometry,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			}
		}

		if ((geo[X]*geo[Y]*geo[Z]) == ba_request->size)
			_append_geo(geo,
				    ba_request->elongate_geos,
				    ba_request->rotate);
		else
			error("%d I was just trying to add a geo of %d%d%d "
			      "while I am trying to request %d midplanes",
			      __LINE__, geo[X], geo[Y], geo[Z],
			      ba_request->size);

/* Having the functions pow and powf on an aix system doesn't seem to
 * link well, so since this is only for aix and this doesn't really
 * need to be there just don't allow this extra calculation.
 */
#ifndef HAVE_AIX
		/* see if We can find a cube or square root of the
		   size to make an easy cube */
		for(i=0; i<cluster_dims-1; i++) {
			sz = powf((float)ba_request->size,
				  (float)1/(cluster_dims-i));
			if (pow(sz,(cluster_dims-i)) == ba_request->size)
				break;
		}

		if (i < (cluster_dims-1)) {
			/* we found something that looks like a cube! */
			int i3 = i;

			for (i=0; i<i3; i++)
				geo[i] = 1;

			for (i=i3; i<cluster_dims; i++)
				if (sz<=DIM_SIZE[i])
					geo[i] = sz;
				else
					goto endit;

			if ((geo[X]*geo[Y]*geo[Z]) == ba_request->size)
				_append_geo(geo,
					    ba_request->elongate_geos,
					    ba_request->rotate);
			else
				error("%d I was just trying to add "
				      "a geo of %d%d%d "
				      "while I am trying to request "
				      "%d midplanes",
				      __LINE__, geo[X], geo[Y], geo[Z],
				      ba_request->size);
		}
#endif //HAVE_AIX
	}

endit:
	if (!(geo_ptr = list_peek(ba_request->elongate_geos)))
		return 0;

	ba_request->elongate_count++;
	ba_request->geometry[X] = geo_ptr[X];
	ba_request->geometry[Y] = geo_ptr[Y];
	ba_request->geometry[Z] = geo_ptr[Z];
	sz=1;
	for (i=0; i<cluster_dims; i++)
		sz *= ba_request->geometry[i];
	ba_request->size = sz;

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
	debug("        size:\t%d", ba_request->size);
	debug("   conn_type:\t%d", ba_request->conn_type[X]);
	debug("      rotate:\t%d", ba_request->rotate);
	debug("    elongate:\t%d", ba_request->elongate);
}

/* If emulating a system set up a known configuration for wires in a
 * system of the size given.
 * If a real bluegene system, query the system and get all wiring
 * information of the system.
 */
extern void init_wires(void)
{
	int x, y, z, i;
	ba_mp_t *source = NULL;
	if (_wires_initialized)
		return;

	for(x=0;x<DIM_SIZE[X];x++) {
		for(y=0;y<DIM_SIZE[Y];y++) {
			for(z=0;z<DIM_SIZE[Z];z++) {
				source = &ba_main_grid[x][y][z];
				for(i=0; i<NUM_PORTS_PER_NODE; i++) {
					_switch_config(source, source,
						       X, i, i);
					_switch_config(source, source,
						       Y, i, i);
					_switch_config(source, source,
						       Z, i, i);
				}
			}
		}
	}
#ifdef HAVE_BG_FILES
	_set_external_wires(0,0,NULL,NULL);
	if (bridge_setup_system() == -1)
		return;
#endif

	_wires_initialized = true;
	return;
}

/*
 * copy the path of the nodes given
 *
 * IN nodes List of ba_mp_t *'s: nodes to be copied
 * OUT dest_nodes List of ba_mp_t *'s: filled in list of nodes
 * wiring.
 * Return on success SLURM_SUCCESS, on error SLURM_ERROR
 */
extern int copy_node_path(List nodes, List *dest_nodes)
{
	int rc = SLURM_ERROR;

#ifdef HAVE_BG_L_P
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ba_mp_t *ba_node = NULL, *new_ba_node = NULL;
	int dim;
	ba_switch_t *curr_switch = NULL, *new_switch = NULL;

	if (!nodes)
		return SLURM_ERROR;
	if (!*dest_nodes)
		*dest_nodes = list_create(destroy_ba_node);

	itr = list_iterator_create(nodes);
	while ((ba_node = list_next(itr))) {
		itr2 = list_iterator_create(*dest_nodes);
		while ((new_ba_node = list_next(itr2))) {
			if (ba_node->coord[X] == new_ba_node->coord[X] &&
			    ba_node->coord[Y] == new_ba_node->coord[Y] &&
			    ba_node->coord[Z] == new_ba_node->coord[Z])
				break;	/* we found it */
		}
		list_iterator_destroy(itr2);

		if (!new_ba_node) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("adding %c%c%c as a new node",
				     alpha_num[ba_node->coord[X]],
				     alpha_num[ba_node->coord[Y]],
				     alpha_num[ba_node->coord[Z]]);
			new_ba_node = ba_copy_mp(ba_node);
			ba_setup_mp(new_ba_node, false, false);
			list_push(*dest_nodes, new_ba_node);

		}
		new_ba_node->used = true;
		for(dim=0;dim<cluster_dims;dim++) {
			curr_switch = &ba_node->axis_switch[dim];
			new_switch = &new_ba_node->axis_switch[dim];
			if (curr_switch->int_wire[0].used) {
				if (!_copy_the_path(*dest_nodes,
						    curr_switch, new_switch,
						    0, dim)) {
					rc = SLURM_ERROR;
					break;
				}
			}
		}

	}
	list_iterator_destroy(itr);
	rc = SLURM_SUCCESS;
#endif
	return rc;
}

extern ba_mp_t *coord2ba_mp(const uint16_t *coord)
{
	if ((coord[X] >= DIM_SIZE[X]) || (coord[Y] >= DIM_SIZE[Y]) ||
	    (coord[Z] >= DIM_SIZE[Z])) {
		error("Invalid coordinate %d:%d:%d",
		      coord[X], coord[Y], coord[Z]);
		return NULL;
	}
	return &ba_main_grid[coord[X]][coord[Y]][coord[Z]];
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

	// _backup_ba_system();
	if (_find_match(ba_request, results)){
		return 1;
	} else {
		return 0;
	}
}


/*
 * Admin wants to remove a previous allocation.
 * will allow Admin to delete a previous allocation retrival by letter code.
 */
extern int remove_block(List nodes, bool is_small)
{
	int dim;
	ba_mp_t* curr_ba_node = NULL;
	ba_mp_t* ba_node = NULL;
	ba_switch_t *curr_switch = NULL;
	ListIterator itr;

	itr = list_iterator_create(nodes);
	while ((curr_ba_node = (ba_mp_t*) list_next(itr))) {
		/* since the list that comes in might not be pointers
		   to the main list we need to point to that main list */
		ba_node = &ba_main_grid[curr_ba_node->coord[X]]
			[curr_ba_node->coord[Y]]
			[curr_ba_node->coord[Z]];
		if (curr_ba_node->used)
			ba_node->used &= (~BA_MP_USED_TRUE);

		/* Small blocks don't use wires, and only have 1 node,
		   so just break. */
		if (is_small)
			break;
		for(dim=0;dim<cluster_dims;dim++) {
			curr_switch = &ba_node->axis_switch[dim];
			if (curr_switch->int_wire[0].used) {
				_reset_the_path(curr_switch, 0, 1, dim);
			}
		}
	}
	list_iterator_destroy(itr);
	return 1;
}

/*
 * Used to set a block into a virtual system.  The system can be
 * cleared first and this function sets all the wires and midplanes
 * used in the nodelist given.  The nodelist is a list of ba_mp_t's
 * that are already set up.  This is very handly to test if there are
 * any passthroughs used by one block when adding another block that
 * also uses those wires, and neither use any overlapping
 * midplanes. Doing a simple bitmap & will not reveal this.
 *
 * Returns SLURM_SUCCESS if nodelist fits into system without
 * conflict, and SLURM_ERROR if nodelist conflicts with something
 * already in the system.
 */
extern int check_and_set_mp_list(List nodes)
{
	int rc = SLURM_ERROR;

#ifdef HAVE_BG_L_P
	int i, j;
	ba_switch_t *ba_switch = NULL, *curr_ba_switch = NULL;
	ba_mp_t *ba_node = NULL, *curr_ba_node = NULL;
	ListIterator itr = NULL;

	if (!nodes)
		return rc;

	itr = list_iterator_create(nodes);
	while ((ba_node = list_next(itr))) {
		/* info("checking %c%c%c", */
/* 		     ba_node->coord[X],  */
/* 		     ba_node->coord[Y], */
/* 		     ba_node->coord[Z]); */

		curr_ba_node = &ba_main_grid[ba_node->coord[X]]
			[ba_node->coord[Y]]
			[ba_node->coord[Z]];

		if (ba_node->used && curr_ba_node->used) {
			/* Only error if the midplane isn't already
			 * marked down or in a error state outside of
			 * the bluegene block.
			 */
			uint16_t base_state, node_flags;
			base_state = curr_ba_node->state & NODE_STATE_BASE;
			node_flags = curr_ba_node->state & NODE_STATE_FLAGS;
			if (!(node_flags & (NODE_STATE_DRAIN | NODE_STATE_FAIL))
			    && (base_state != NODE_STATE_DOWN)) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("I have already been to "
					     "this node %c%c%c %s",
					     alpha_num[ba_node->coord[X]],
					     alpha_num[ba_node->coord[Y]],
					     alpha_num[ba_node->coord[Z]],
					     node_state_string(
						     curr_ba_node->state));
				rc = SLURM_ERROR;
				goto end_it;
			}
		}

		if (ba_node->used)
			curr_ba_node->used = ba_node->used;
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("check_and_set_mp_list: "
			     "%s is used ?= %d %d",
			     curr_ba_node->coord_str,
			     curr_ba_node->used, ba_node->used);
		for(i=0; i<cluster_dims; i++) {
			ba_switch = &ba_node->axis_switch[i];
			curr_ba_switch = &curr_ba_node->axis_switch[i];
			//info("checking dim %d", i);

			for(j=0; j<NUM_PORTS_PER_NODE; j++) {
				//info("checking port %d", j);

				if (ba_switch->int_wire[j].used
				    && curr_ba_switch->int_wire[j].used
				    && j != curr_ba_switch->
				    int_wire[j].port_tar) {
					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("%c%c%c dim %d port %d "
						     "is already in use to %d",
						     alpha_num[ba_node->
							       coord[X]],
						     alpha_num[ba_node->
							       coord[Y]],
						     alpha_num[ba_node->
							       coord[Z]],
						     i,
						     j,
						     curr_ba_switch->
						     int_wire[j].port_tar);
					rc = SLURM_ERROR;
					goto end_it;
				}
				if (!ba_switch->int_wire[j].used)
					continue;

				/* info("setting %c%c%c dim %d port %d -> %d",*/
/* 				     alpha_num[ba_node->coord[X]],  */
/* 				     alpha_num[ba_node->coord[Y]], */
/* 				     alpha_num[ba_node->coord[Z]],  */
/* 				     i, */
/* 				     j, */
/* 				     ba_switch->int_wire[j].port_tar); */
				curr_ba_switch->int_wire[j].used = 1;
				curr_ba_switch->int_wire[j].port_tar
					= ba_switch->int_wire[j].port_tar;
			}
		}
	}
	rc = SLURM_SUCCESS;
end_it:
	list_iterator_destroy(itr);
#endif
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
	char *name = NULL;
	ba_mp_t* ba_node = NULL;
	int send_results = 0;
	int found = 0;

	xassert(ba_request);

	if (cluster_dims == 1) {
		if (ba_request->start[X]>=DIM_SIZE[X])
			return NULL;
		ba_request->size = ba_request->geometry[X];
		ba_node = &ba_main_grid[ba_request->start[X]][0][0];
	} else {
		int dim;

		ba_request->size = 1;
		for (dim=0; dim<cluster_dims; dim++) {
			if (ba_request->start[dim] >= DIM_SIZE[dim])
				return NULL;
			if ((int16_t)ba_request->geometry[dim] <= 0) {
				error("problem with geometry of %c in dim %d, "
				      "needs to be at least 1",
				      alpha_num[ba_request->geometry[dim]],
				      dim);
				return NULL;
			}
			ba_request->size *= ba_request->geometry[dim];
		}

		ba_node = coord2ba_mp(ba_request->start);
	}

	if (!ba_node)
		return NULL;

	if (!results)
		results = list_create(NULL);
	else
		send_results = 1;

	/* This midplane should have already been checked if it was in
	   use or not */
	list_append(results, ba_node);

	if (ba_request->conn_type[0] >= SELECT_SMALL) {
		/* adding the ba_node and ending */
		ba_node->used |= BA_MP_USED_TRUE;
		name = xstrdup_printf("%s", ba_node->coord_str);
		goto end_it;
	} else if (ba_request->conn_type[0] == SELECT_NAV)
		ba_request->conn_type[0] = bg_conf->default_conn_type[0];

	found = _find_x_path(results, ba_node,
			     ba_node->coord,
			     ba_request->geometry[X],
			     1,
			     ba_request->conn_type[0], BLOCK_ALGO_FIRST);

	if (!found) {
		bool is_small = 0;
		if (ba_request->conn_type[0] == SELECT_SMALL)
			is_small = 1;
		debug2("trying less efficient code");
		remove_block(results, is_small);
		list_flush(results);
		list_append(results, ba_node);
		found = _find_x_path(results, ba_node,
				     ba_node->coord,
				     ba_request->geometry[X],
				     1,
				     ba_request->conn_type[0],
				     BLOCK_ALGO_SECOND);
	}
	if (found) {
		if (cluster_flags & CLUSTER_FLAG_BG) {
			List start_list = NULL;
			ListIterator itr;

			start_list = list_create(NULL);
			itr = list_iterator_create(results);
			while ((ba_node = (ba_mp_t*) list_next(itr))) {
				list_append(start_list, ba_node);
			}
			list_iterator_destroy(itr);

			if (!_fill_in_coords(results,
					     start_list,
					     ba_request->geometry,
					     ba_request->conn_type[0])) {
				list_destroy(start_list);
				goto end_it;
			}
			list_destroy(start_list);
		}
	} else {
		goto end_it;
	}

	name = _set_internal_wires(results,
				   ba_request->size,
				   ba_request->conn_type[0]);
end_it:
	if (!send_results && results) {
		list_destroy(results);
		results = NULL;
	}
	if (name!=NULL) {
		debug2("name = %s", name);
	} else {
		debug2("can't allocate");
		xfree(name);
	}

	return name;
}

/* Rotate a 3-D geometry array through its six permutations */
extern void ba_rotate_geo(uint16_t *req_geometry, int rot_cnt)
{
	uint16_t tmp;

	switch (rot_cnt) {
	case 0:		/* ABC -> ACB */
	case 2:		/* CAB -> CBA */
	case 4:		/* BCA -> BAC */
		SWAP(req_geometry[Y], req_geometry[Z], tmp);
		break;
	case 1:		/* ACB -> CAB */
	case 3:		/* CBA -> BCA */
	case 5:		/* BAC -> ABC */
		SWAP(req_geometry[X], req_geometry[Y], tmp);
		break;
	}
}

/********************* Local Functions *********************/

/*
 * This function is here to check options for rotating and elongating
 * and set up the request based on the count of each option
 */
static int _check_for_options(select_ba_request_t* ba_request)
{
	int temp;
	int set=0;
	uint16_t *geo = NULL;
	ListIterator itr;

	if (ba_request->rotate) {
	rotate_again:
		debug2("Rotating! %d",ba_request->rotate_count);

		if (ba_request->rotate_count==(cluster_dims-1)) {
			temp=ba_request->geometry[X];
			ba_request->geometry[X]=ba_request->geometry[Z];
			ba_request->geometry[Z]=temp;
			ba_request->rotate_count++;
			set=1;

		} else if (ba_request->rotate_count<(cluster_dims*2)) {
			temp=ba_request->geometry[X];
			ba_request->geometry[X]=ba_request->geometry[Y];
			ba_request->geometry[Y]=ba_request->geometry[Z];
			ba_request->geometry[Z]=temp;
			ba_request->rotate_count++;
			set=1;
		} else
			ba_request->rotate = false;
		if (set) {
			if (ba_request->geometry[X]<=DIM_SIZE[X]
			    && ba_request->geometry[Y]<=DIM_SIZE[Y]
			    && ba_request->geometry[Z]<=DIM_SIZE[Z])
				return 1;
			else {
				set = 0;
				goto rotate_again;
			}
		}
	}
	if (ba_request->elongate) {
	elongate_again:
		debug2("Elongating! %d",ba_request->elongate_count);
		ba_request->rotate_count=0;
		ba_request->rotate = true;

		set = 0;
		itr = list_iterator_create(ba_request->elongate_geos);
		for(set=0; set<=ba_request->elongate_count; set++)
			geo = list_next(itr);
		list_iterator_destroy(itr);
		if (geo == NULL)
			return 0;
		ba_request->elongate_count++;
		ba_request->geometry[X] = geo[X];
		ba_request->geometry[Y] = geo[Y];
		ba_request->geometry[Z] = geo[Z];
		if (ba_request->geometry[X]<=DIM_SIZE[X]
		    && ba_request->geometry[Y]<=DIM_SIZE[Y]
		    && ba_request->geometry[Z]<=DIM_SIZE[Z]) {
			return 1;
		} else
			goto elongate_again;

	}
	return 0;
}

/*
 * grab all the geometries that we can get and append them to the list geos
 */
static int _append_geo(uint16_t *geometry, List geos, int rotate)
{
	ListIterator itr;
	uint16_t *geo_ptr = NULL;
	uint16_t *geo = NULL;
	int temp_geo;
	int i, j;

	if (rotate) {
		for (i = (cluster_dims - 1); i >= 0; i--) {
			for (j = 1; j <= i; j++) {
				if ((geometry[j-1] > geometry[j])
				    && (geometry[j] <= DIM_SIZE[j-i])
				    && (geometry[j-1] <= DIM_SIZE[j])) {
					temp_geo = geometry[j-1];
					geometry[j-1] = geometry[j];
					geometry[j] = temp_geo;
				}
			}
		}
	}
	itr = list_iterator_create(geos);
	while ((geo_ptr = list_next(itr)) != NULL) {
		if (geometry[X] == geo_ptr[X]
		    && geometry[Y] == geo_ptr[Y]
		    && geometry[Z] == geo_ptr[Z])
			break;

	}
	list_iterator_destroy(itr);

	if (geo_ptr == NULL) {
		geo = xmalloc(sizeof(int)*cluster_dims);
		geo[X] = geometry[X];
		geo[Y] = geometry[Y];
		geo[Z] = geometry[Z];
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("adding geo %c%c%c",
			     alpha_num[geo[X]], alpha_num[geo[Y]],
			     alpha_num[geo[Z]]);
		list_append(geos, geo);
	}
	return 1;
}

/*
 * Fill in the paths and extra midplanes we need for the block.
 * Basically copy the x path sent in with the start_list in each Y and
 * Z dimension filling in every midplane for the block and then
 * completing the Y and Z wiring, tying the whole block together.
 *
 * IN/OUT results - total list of midplanes after this function
 *        returns successfully.  Should be
 *        an exact copy of the start_list at first.
 * IN start_list - exact copy of results at first, This should only be
 *        a list of midplanes on the X dim.  We will work off this and
 *        the geometry to fill in this wiring for the X dim in all the
 *        Y and Z coords.
 * IN geometry - What the block looks like
 * IN conn_type - Mesh or Torus
 *
 * RET: 0 on failure 1 on success
 */
static int _fill_in_coords(List results, List start_list,
			   uint16_t *geometry, int conn_type)
{
	ba_mp_t *ba_node = NULL;
	ba_mp_t *check_node = NULL;
	int rc = 1;
	ListIterator itr = NULL;
	int y=0, z=0;
	ba_switch_t *curr_switch = NULL;
	ba_switch_t *next_switch = NULL;

	if (!start_list || !results)
		return 0;
	/* go through the start_list and add all the midplanes */
	itr = list_iterator_create(start_list);
	while ((check_node = (ba_mp_t*) list_next(itr))) {
		curr_switch = &check_node->axis_switch[X];

		for(y=0; y<geometry[Y]; y++) {
			if ((check_node->coord[Y]+y) >= DIM_SIZE[Y]) {
				rc = 0;
				goto failed;
			}
			for(z=0; z<geometry[Z]; z++) {
				if ((check_node->coord[Z]+z) >= DIM_SIZE[Z]) {
					rc = 0;
					goto failed;
				}
				ba_node = &ba_main_grid
					[check_node->coord[X]]
					[check_node->coord[Y]+y]
					[check_node->coord[Z]+z];

				if ((ba_node->coord[Y] == check_node->coord[Y])
				    && (ba_node->coord[Z]
					== check_node->coord[Z]))
					continue;

				if (!_node_used(ba_node, geometry[X])) {
					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("here Adding %c%c%c",
						     alpha_num[ba_node->
							       coord[X]],
						     alpha_num[ba_node->
							       coord[Y]],
						     alpha_num[ba_node->
							       coord[Z]]);
					list_append(results, ba_node);
					next_switch = &ba_node->axis_switch[X];

					/* since we are going off the
					 * main system we can send NULL
					 * here
					 */
					_copy_the_path(NULL, curr_switch,
						       next_switch,
						       0, X);
				} else {
					rc = 0;
					goto failed;
				}
			}
		}

	}
	list_iterator_destroy(itr);
	itr = list_iterator_create(start_list);
	check_node = (ba_mp_t*) list_next(itr);
	list_iterator_destroy(itr);

	itr = list_iterator_create(results);
	while ((ba_node = (ba_mp_t*) list_next(itr))) {
		if (!_find_yz_path(ba_node,
				   check_node->coord,
				   geometry,
				   conn_type)){
			rc = 0;
			goto failed;
		}
	}

	if (deny_pass) {
		if ((*deny_pass & PASS_DENY_Y)
		    && (*deny_pass & PASS_FOUND_Y)) {
			debug("We don't allow Y passthoughs");
			rc = 0;
		} else if ((*deny_pass & PASS_DENY_Z)
			   && (*deny_pass & PASS_FOUND_Z)) {
			debug("We don't allow Z passthoughs");
			rc = 0;
		}
	}

failed:
	list_iterator_destroy(itr);

	return rc;
}

/*
 * Copy a path through the wiring of a switch to another switch on a
 * starting port on a dimension.
 *
 * IN/OUT: nodes - Local list of midplanes you are keeping track of.  If
 *         you visit any new midplanes a copy from ba_main_grid
 *         will be added to the list.  If NULL the path will be
 *         set in mark_switch of the main virtual system (ba_main_grid).
 * IN: curr_switch - The switch you want to copy the path of
 * IN/OUT: mark_switch - The switch you want to fill in.  On success
 *         this switch will contain a complete path from the curr_switch
 *         starting from the source port.
 * IN: source - source port number (If calling for the first time
 *         should be 0 since we are looking for 1 at the end)
 * IN: dim - Dimension XYZ
 *
 * RET: on success 1, on error 0
 */
static int _copy_the_path(List nodes, ba_switch_t *curr_switch,
			  ba_switch_t *mark_switch,
			  int source, int dim)
{
	uint16_t *mp_tar;
	uint16_t *mark_mp_tar;
	uint16_t *node_curr;
	uint16_t port_tar, port_tar1;
	ba_switch_t *next_switch = NULL;
	ba_switch_t *next_mark_switch = NULL;

	/* Copy the source used and port_tar */
	mark_switch->int_wire[source].used =
		curr_switch->int_wire[source].used;
	mark_switch->int_wire[source].port_tar =
		curr_switch->int_wire[source].port_tar;

	port_tar = curr_switch->int_wire[source].port_tar;

	/* Now to the same thing from the other end */
	mark_switch->int_wire[port_tar].used =
		curr_switch->int_wire[port_tar].used;
	mark_switch->int_wire[port_tar].port_tar =
		curr_switch->int_wire[port_tar].port_tar;
	port_tar1 = port_tar;

	/* follow the path */
	node_curr = curr_switch->ext_wire[0].mp_tar;
	mp_tar = curr_switch->ext_wire[port_tar].mp_tar;
	if (mark_switch->int_wire[source].used)
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("setting dim %d %c%c%c %d-> %c%c%c %d",
			     dim,
			     alpha_num[node_curr[X]],
			     alpha_num[node_curr[Y]],
			     alpha_num[node_curr[Z]],
			     source,
			     alpha_num[mp_tar[X]],
			     alpha_num[mp_tar[Y]],
			     alpha_num[mp_tar[Z]],
			     port_tar);

	if (port_tar == 1) {
		/* found the end of the line */
		mark_switch->int_wire[1].used =
			curr_switch->int_wire[1].used;
		mark_switch->int_wire[1].port_tar =
			curr_switch->int_wire[1].port_tar;
		return 1;
	}

	mark_mp_tar = mark_switch->ext_wire[port_tar].mp_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;

	if (node_curr[X] == mp_tar[X]
	    && node_curr[Y] == mp_tar[Y]
	    && node_curr[Z] == mp_tar[Z]) {
		/* We are going to the same node! this should never
		   happen */
		debug5("something bad happened!! "
		       "we are on %c%c%c and are going to it "
		       "from port %d - > %d",
		       alpha_num[node_curr[X]],
		       alpha_num[node_curr[Y]],
		       alpha_num[node_curr[Z]],
		       port_tar1, port_tar);
		return 0;
	}

	/* see what the next switch is going to be */
	next_switch = &ba_main_grid[mp_tar[X]][mp_tar[Y]][mp_tar[Z]].
		axis_switch[dim];
	if (!nodes) {
		/* If no nodes then just get the next switch to fill
		   in from the main system */
		next_mark_switch = &ba_main_grid[mark_mp_tar[X]]
			[mark_mp_tar[Y]]
			[mark_mp_tar[Z]]
			.axis_switch[dim];
	} else {
		ba_mp_t *ba_node = NULL;
		ListIterator itr = list_iterator_create(nodes);
		/* see if we have already been to this node */
		while ((ba_node = list_next(itr))) {
			if (ba_node->coord[X] == mark_mp_tar[X] &&
			    ba_node->coord[Y] == mark_mp_tar[Y] &&
			    ba_node->coord[Z] == mark_mp_tar[Z])
				break;	/* we found it */
		}
		list_iterator_destroy(itr);
		if (!ba_node) {
			/* If node grab a copy and add it to the list */
			ba_node = ba_copy_mp(&ba_main_grid[mark_mp_tar[X]]
					     [mark_mp_tar[Y]]
					     [mark_mp_tar[Z]]);
			ba_setup_mp(ba_node, false, false);
			list_push(nodes, ba_node);
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("haven't seen %c%c%c adding it",
				     alpha_num[ba_node->coord[X]],
				     alpha_num[ba_node->coord[Y]],
				     alpha_num[ba_node->coord[Z]]);
		}
		next_mark_switch = &ba_node->axis_switch[dim];

	}

	/* Keep going until we reach the end of the line */
	return _copy_the_path(nodes, next_switch, next_mark_switch,
			      port_tar, dim);
}

static int _find_yz_path(ba_mp_t *ba_node, uint16_t *first,
			 uint16_t *geometry, int conn_type)
{
	ba_mp_t *next_node = NULL;
	uint16_t *mp_tar = NULL;
	ba_switch_t *dim_curr_switch = NULL;
	ba_switch_t *dim_next_switch = NULL;
	int i2;
	int count = 0;

	for(i2=1;i2<=2;i2++) {
		if (geometry[i2] > 1) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("%d node %c%c%c port 2 -> ",
				     i2,
				     alpha_num[ba_node->coord[X]],
				     alpha_num[ba_node->coord[Y]],
				     alpha_num[ba_node->coord[Z]]);

			dim_curr_switch = &ba_node->axis_switch[i2];
			if (dim_curr_switch->int_wire[2].used) {
				debug5("returning here");
				return 0;
			}

			mp_tar = dim_curr_switch->ext_wire[2].mp_tar;

			next_node =
				&ba_main_grid[mp_tar[X]][mp_tar[Y]][mp_tar[Z]];
			dim_next_switch = &next_node->axis_switch[i2];
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("%c%c%c port 5",
				     alpha_num[next_node->coord[X]],
				     alpha_num[next_node->coord[Y]],
				     alpha_num[next_node->coord[Z]]);

			if (dim_next_switch->int_wire[5].used) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("returning here 2");
				return 0;
			}
			debug5("%d %d %d %d",i2, mp_tar[i2],
			       first[i2], geometry[i2]);

			/* Here we need to see where we are in
			 * reference to the geo of this dimension.  If
			 * we have not gotten the number we need in
			 * the direction we just go to the next node
			 * with 5 -> 1.  If we have all the midplanes
			 * we need then we go through and finish the
			 * torus if needed
			 */
			if (mp_tar[i2] < first[i2])
				count = mp_tar[i2]+(DIM_SIZE[i2]-first[i2]);
			else
				count = (mp_tar[i2]-first[i2]);

			if (count == geometry[i2]) {
				debug5("found end of me %c%c%c",
				       alpha_num[mp_tar[X]],
				       alpha_num[mp_tar[Y]],
				       alpha_num[mp_tar[Z]]);
				if (conn_type == SELECT_TORUS) {
					dim_curr_switch->int_wire[0].used = 1;
					dim_curr_switch->int_wire[0].port_tar
						= 2;
					dim_curr_switch->int_wire[2].used = 1;
					dim_curr_switch->int_wire[2].port_tar
						= 0;
					dim_curr_switch = dim_next_switch;

					if (deny_pass
					    && (mp_tar[i2] != first[i2])) {
						if (i2 == 1)
							*deny_pass |=
								PASS_FOUND_Y;
						else
							*deny_pass |=
								PASS_FOUND_Z;
					}
					while (mp_tar[i2] != first[i2]) {
						if (ba_debug_flags
						    & DEBUG_FLAG_BG_ALGO_DEEP)
							info("on dim %d at %d "
							     "looking for %d",
							     i2,
							     mp_tar[i2],
							     first[i2]);

						if (dim_curr_switch->
						    int_wire[2].used) {
							if (ba_debug_flags
							    & DEBUG_FLAG_BG_ALGO_DEEP)
								info("returning"
								     " here 3");
							return 0;
						}

						dim_curr_switch->
							int_wire[2].used = 1;
						dim_curr_switch->
							int_wire[2].port_tar
							= 5;
						dim_curr_switch->
							int_wire[5].used
							= 1;
						dim_curr_switch->
							int_wire[5].
							port_tar = 2;


						mp_tar = dim_curr_switch->
							ext_wire[2].mp_tar;
						next_node = &ba_main_grid
							[mp_tar[X]]
							[mp_tar[Y]]
							[mp_tar[Z]];
						dim_curr_switch =
							&next_node->
							axis_switch[i2];
					}

					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("back to first on dim %d "
						     "at %d looking for %d",
						     i2,
						     mp_tar[i2],
						     first[i2]);

					dim_curr_switch->
						int_wire[5].used = 1;
					dim_curr_switch->
						int_wire[5].port_tar
						= 1;
					dim_curr_switch->
						int_wire[1].used
						= 1;
					dim_curr_switch->
						int_wire[1].
						port_tar = 5;
				}

			} else if (count < geometry[i2]) {
				if (conn_type == SELECT_TORUS ||
				    (conn_type == SELECT_MESH &&
				     (mp_tar[i2] != first[i2]))) {
					dim_curr_switch->
						int_wire[0].used = 1;
					dim_curr_switch->
						int_wire[0].port_tar
						= 2;
					dim_curr_switch->
						int_wire[2].used
						= 1;
					dim_curr_switch->
						int_wire[2].
						port_tar = 0;

					dim_next_switch->int_wire[5].used
						= 1;
					dim_next_switch->
						int_wire[5].port_tar
						= 1;
					dim_next_switch->
						int_wire[1].used = 1;
					dim_next_switch->
						int_wire[1].port_tar
						= 5;
				}
			} else {
				error("We were only looking for %d "
				      "in the %d dim, but now we have %d",
				      geometry[i2], i2, count);
				return 0;
			}
		} else if ((geometry[i2] == 1) && (conn_type == SELECT_TORUS)) {
			/* FIX ME: This is put here because we got
			   into a state where the Y dim was not being
			   processed correctly.  This will set up the
			   0 -> 1 port correctly.  We should probably
			   find out why this was happening in the
			   first place though.  A reproducer was to
			   have
			   MPs=[310x323] Type=TORUS
			   MPs=[200x233] Type=TORUS
			   MPs=[300x303] Type=TORUS
			   MPs=[100x133] Type=TORUS
			   MPs=[000x033] Type=TORUS
			   MPs=[400x433] Type=TORUS
			   and then add
			   MPs=[330x333] Type=TORUS
			*/

			dim_curr_switch = &ba_node->axis_switch[i2];
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("%d node %c%c%c port 0 -> 1",
				     i2,
				     alpha_num[ba_node->coord[X]],
				     alpha_num[ba_node->coord[Y]],
				     alpha_num[ba_node->coord[Z]]);
			dim_curr_switch->int_wire[0].used = 1;
			dim_curr_switch->int_wire[0].port_tar = 1;
			dim_curr_switch->int_wire[1].used = 1;
			dim_curr_switch->int_wire[1].port_tar = 0;
		}
	}
	return 1;
}

#ifndef HAVE_BG_FILES
/** */
static int _emulate_ext_wiring(ba_mp_t ***grid)
{
	int x;
	ba_mp_t *source = NULL, *target = NULL;
	if (cluster_dims == 1) {
		for(x=0;x<DIM_SIZE[X];x++) {
			source = &grid[x][0][0];
			if (x<(DIM_SIZE[X]-1))
				target = &grid[x+1][0][0];
			else
				target = &grid[0][0][0];
			_set_external_wires(X, x, source, target);
		}
	} else {
		int y,z;
		for(x=0;x<DIM_SIZE[X];x++) {
			for(y=0;y<DIM_SIZE[Y];y++) {
				for(z=0;z<DIM_SIZE[Z];z++) {
					source = &grid[x][y][z];

					if (x<(DIM_SIZE[X]-1)) {
						target = &grid[x+1][y][z];
					} else
						target = &grid[0][y][z];

					_set_external_wires(X, x, source,
							    target);

					if (y<(DIM_SIZE[Y]-1))
						target = &grid[x][y+1][z];
					else
						target = &grid[x][0][z];

					_set_external_wires(Y, y, source,
							    target);
					if (z<(DIM_SIZE[Z]-1))
						target = &grid[x][y][z+1];
					else
						target = &grid[x][y][0];

					_set_external_wires(Z, z, source,
							    target);
				}
			}
		}
	}
	return 1;
}
#endif


static int _reset_the_path(ba_switch_t *curr_switch, int source,
			   int target, int dim)
{
	uint16_t *mp_tar;
	uint16_t *node_curr;
	int port_tar, port_tar1;
	ba_switch_t *next_switch = NULL;

	if (source < 0 || source >= NUM_PORTS_PER_NODE) {
		fatal("source port was %d can only be 0->%d",
		      source, NUM_PORTS_PER_NODE);
	}
	if (target < 0 || target >= NUM_PORTS_PER_NODE) {
		fatal("target port was %d can only be 0->%d",
		      target, NUM_PORTS_PER_NODE);
	}
	/*set the switch to not be used */
	if (!curr_switch->int_wire[source].used) {
		/* This means something overlapping the removing block
		   already cleared this, or the path just never was
		   complete in the first place. */
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("I reached the end, the source isn't used");
		return 1;
	}
	curr_switch->int_wire[source].used = 0;
	port_tar = curr_switch->int_wire[source].port_tar;
	if (port_tar < 0 || port_tar >= NUM_PORTS_PER_NODE) {
		fatal("port_tar port was %d can only be 0->%d",
		      source, NUM_PORTS_PER_NODE);
		return 1;
	}

	port_tar1 = port_tar;
	curr_switch->int_wire[source].port_tar = source;
	curr_switch->int_wire[port_tar].used = 0;
	curr_switch->int_wire[port_tar].port_tar = port_tar;
	if (port_tar == target) {
		return 1;
	}
	/* follow the path */
	node_curr = curr_switch->ext_wire[0].mp_tar;
	mp_tar = curr_switch->ext_wire[port_tar].mp_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	if (source == port_tar1) {
		debug("got this bad one %c%c%c %d %d -> %c%c%c %d",
		      alpha_num[node_curr[X]],
		      alpha_num[node_curr[Y]],
		      alpha_num[node_curr[Z]],
		      source,
		      port_tar1,
		      alpha_num[mp_tar[X]],
		      alpha_num[mp_tar[Y]],
		      alpha_num[mp_tar[Z]],
		      port_tar);
		return 0;
	}
	debug5("from %c%c%c %d %d -> %c%c%c %d",
	       alpha_num[node_curr[X]],
	       alpha_num[node_curr[Y]],
	       alpha_num[node_curr[Z]],
	       source,
	       port_tar1,
	       alpha_num[mp_tar[X]],
	       alpha_num[mp_tar[Y]],
	       alpha_num[mp_tar[Z]],
	       port_tar);
	if (node_curr[X] == mp_tar[X]
	    && node_curr[Y] == mp_tar[Y]
	    && node_curr[Z] == mp_tar[Z]) {
		debug5("%d something bad happened!!", dim);
		return 0;
	}
	next_switch =
		&ba_main_grid[mp_tar[X]][mp_tar[Y]][mp_tar[Z]].axis_switch[dim];

	return _reset_the_path(next_switch, port_tar, target, dim);
//	return 1;
}

extern void ba_create_system()
{
	int x,y,z, i = 0;

	if (ba_main_grid)
		ba_destroy_system();

	best_count=BEST_COUNT_INIT;

	ba_main_grid = (ba_mp_t***)
		xmalloc(sizeof(ba_mp_t**) * DIM_SIZE[X]);
	for (x=0; x<DIM_SIZE[X]; x++) {
		ba_main_grid[x] = (ba_mp_t**)
			xmalloc(sizeof(ba_mp_t*) * DIM_SIZE[Y]);
		for (y=0; y<DIM_SIZE[Y]; y++) {
			ba_main_grid[x][y] = (ba_mp_t*)
				xmalloc(sizeof(ba_mp_t)
					* DIM_SIZE[Z]);
			for (z=0; z<DIM_SIZE[Z]; z++){
				ba_mp_t *ba_mp = &ba_main_grid[x][y][z];
				ba_mp->coord[X] = x;
				ba_mp->coord[Y] = y;
				ba_mp->coord[Z] = z;
				snprintf(ba_mp->coord_str,
					 sizeof(ba_mp->coord_str),
					 "%c%c%c",
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
			}
		}
	}
	if ((cluster_flags & CLUSTER_FLAG_BGL) ||
	    (cluster_flags & CLUSTER_FLAG_BGP)) {
		init_wires();
#ifndef HAVE_BG_FILES
		_emulate_ext_wiring(ba_main_grid);
#endif
	}

	path = list_create(_delete_path_list);
	best_path = list_create(_delete_path_list);
}

/** */
extern void ba_destroy_system(void)
{
	int x, y;

	if (path) {
		list_destroy(path);
		path = NULL;
	}
	if (best_path) {
		list_destroy(best_path);
		best_path = NULL;
	}

#ifdef HAVE_BG_FILES
	if (bg)
		bridge_free_bg(bg);
#endif
	_mp_map_initialized = false;
	_wires_initialized = true;

	if (ba_main_grid) {
		for (x=0; x<DIM_SIZE[X]; x++) {
			for (y=0; y<DIM_SIZE[Y]; y++)
				xfree(ba_main_grid[x][y]);

			xfree(ba_main_grid[x]);
		}
		xfree(ba_main_grid);
		ba_main_grid = NULL;
	}
}

extern bool ba_sub_block_in_bitmap(select_jobinfo_t *jobinfo,
				   bitstr_t *usable_bitmap, bool step)
{
	/* This shouldn't be called. */
	xassert(0);
	return false;
}

extern int ba_sub_block_in_bitmap_clear(select_jobinfo_t *jobinfo,
					bitstr_t *usable_bitmap)
{
	/* this doesn't do anything since above doesn't. */
	return SLURM_SUCCESS;
}

extern ba_mp_t *ba_sub_block_in_record(
	bg_record_t *bg_record, uint32_t *node_count,
	select_jobinfo_t *jobinfo)
{
	/* This shouldn't be called. */
	xassert(0);
	return false;
}

extern int ba_sub_block_in_record_clear(
	bg_record_t *bg_record, struct step_record *step_ptr)
{
	/* this doesn't do anything since above doesn't. */
	return SLURM_SUCCESS;
}

extern void ba_sync_job_to_block(bg_record_t *bg_record,
				 struct job_record *job_ptr)
{
	xassert(bg_record);
	xassert(job_ptr);

	bg_record->job_running = job_ptr->job_id;
	bg_record->job_ptr = job_ptr;
}


extern bitstr_t *ba_create_ba_mp_cnode_bitmap(bg_record_t *bg_record)
{
	return NULL;
}

extern bitstr_t *ba_cnodelist2bitmap(char *cnodelist)
{
	return NULL;
}

extern void ba_set_ionode_str(bg_record_t *bg_record)
{
	char bitstring[BITSIZE];
        if (!bg_record->ionode_bitmap)
		return;

	bit_fmt(bitstring, BITSIZE, bg_record->ionode_bitmap);
	bg_record->ionode_str = xstrdup(bitstring);
}

extern struct job_record *ba_remove_job_in_block_job_list(
	bg_record_t *bg_record, struct job_record *in_job_ptr)
{
	return NULL;
}

static void _delete_path_list(void *object)
{
	ba_path_switch_t *path_switch = (ba_path_switch_t *)object;

	if (path_switch) {
		xfree(path_switch);
	}
	return;
}

/**
 * algorithm for finding match
 */
static int _find_match(select_ba_request_t *ba_request, List results)
{
	int x=0;

	ba_mp_t *ba_node = NULL;
	char *name=NULL;
	int startx = DIM_SIZE[X]-1;
	uint16_t *geo_ptr;

	if (!(cluster_flags & CLUSTER_FLAG_BG))
		return 0;

	/* set up the geo here */
	if (!(geo_ptr = list_peek(ba_request->elongate_geos)))
		return 0;
	ba_request->rotate_count=0;
	ba_request->elongate_count=1;
	ba_request->geometry[X] = geo_ptr[X];
	ba_request->geometry[Y] = geo_ptr[Y];
	ba_request->geometry[Z] = geo_ptr[Z];

	if (ba_request->geometry[X]>DIM_SIZE[X]
	    || ba_request->geometry[Y]>DIM_SIZE[Y]
	    || ba_request->geometry[Z]>DIM_SIZE[Z])
		if (!_check_for_options(ba_request))
			return 0;

start_again:
	x=0;
	if (x == startx)
		x = startx-1;
	while (x!=startx) {
		x++;
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("finding %c%c%c try %d",
			     alpha_num[ba_request->geometry[X]],
			     alpha_num[ba_request->geometry[Y]],
			     alpha_num[ba_request->geometry[Z]],
			     x);
	new_node:
		ba_node = coord2ba_mp(ba_request->start);
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("starting at %s", ba_node->coord_str);

		if (!_node_used(ba_node, ba_request->geometry[X])) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("trying this node %s %c%c%c %d",
				     ba_node->coord_str,
				     alpha_num[ba_request->geometry[X]],
				     alpha_num[ba_request->geometry[Y]],
				     alpha_num[ba_request->geometry[Z]],
				     ba_request->conn_type[X]);
			name = set_bg_block(results, ba_request);

			if (name) {
				ba_request->save_name = xstrdup(name);
				xfree(name);
				return 1;
			}

			if (results) {
				bool is_small = 0;
				if (ba_request->conn_type[0] == SELECT_SMALL)
					is_small = 1;
				remove_block(results, is_small);
				list_flush(results);
			}
			if (ba_request->start_req)
				goto requested_end;
			//exit(0);
			debug2("trying something else");

		}

		if ((DIM_SIZE[Z] - ba_request->start[Z]-1)
		    >= ba_request->geometry[Z])
			ba_request->start[Z]++;
		else {
			ba_request->start[Z] = 0;
			if ((DIM_SIZE[Y] - ba_request->start[Y]-1)
			    >= ba_request->geometry[Y])
				ba_request->start[Y]++;
			else {
				ba_request->start[Y] = 0;
				if ((DIM_SIZE[X] - ba_request->start[X]-1)
				    >= ba_request->geometry[X])
					ba_request->start[X]++;
				else {
					if (ba_request->size == 1)
						goto requested_end;
					if (!_check_for_options(ba_request))
						return 0;
					else {
						memset(ba_request->start, 0,
						       sizeof(ba_request->
							      start));
						goto start_again;
					}
				}
			}
		}
		goto new_node;
	}
requested_end:
	debug2("1 can't allocate");

	return 0;
}

/*
 * Used to check if midplane is usable in the block we are creating
 *
 * IN: ba_node - node to check if is used
 * IN: x_size - How big is the block in the X dim used to see if the
 *     wires are full hence making this midplane unusable.
 */
static bool _node_used(ba_mp_t* ba_node, int x_size)
{
	ba_switch_t* ba_switch = NULL;
	/* if we've used this node in another block already */
	if (!ba_node || ba_node->used) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("node %s used", ba_node->coord_str);
		return true;
	}
	/* Check If we've used this node's switches completely in another
	   block already.  Right now we are only needing to look at
	   the X dim since it is the only one with extra wires.  This
	   can be set up to do all the dim's if in the future if it is
	   needed. We only need to check this if we are planning on
	   using more than 1 midplane in the block creation */
	if (x_size > 1) {
		/* get the switch of the X Dimension */
		ba_switch = &ba_node->axis_switch[X];

		/* If both of these ports are used then the node
		   is in use since there are no more wires we
		   can use since these can not connect to each
		   other they must be connected to the other ports.
		*/
		if (ba_switch->int_wire[3].used
		    && ba_switch->int_wire[5].used) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("switch full in the X dim on node %s!",
				     ba_node->coord_str);
			return true;
		}
	}

	return false;

}


static void _switch_config(ba_mp_t* source, ba_mp_t* target, int dim,
			   int port_src, int port_tar)
{
	ba_switch_t* config = NULL, *config_tar = NULL;
	int i;

	if (!source || !target)
		return;

	config = &source->axis_switch[dim];
	config_tar = &target->axis_switch[dim];
	for(i=0;i<cluster_dims;i++) {
		/* Set the coord of the source target node to the target */
		config->ext_wire[port_src].mp_tar[i] = target->coord[i];

		/* Set the coord of the target back to the source */
		config_tar->ext_wire[port_tar].mp_tar[i] = source->coord[i];
	}

	/* Set the port of the source target node to the target */
	config->ext_wire[port_src].port_tar = port_tar;

	/* Set the port of the target back to the source */
	config_tar->ext_wire[port_tar].port_tar = port_src;
}

static int _set_external_wires(int dim, int count, ba_mp_t* source,
			       ba_mp_t* target)
{

#ifdef HAVE_BG_FILES
#ifdef HAVE_BGL

#define UNDER_POS  7
#define NODE_LEN 5
#define VAL_NAME_LEN 12

#else

#define UNDER_POS  9
#define NODE_LEN 7
#define VAL_NAME_LEN 16

#endif
	int rc;
	int i;
	rm_wire_t *my_wire = NULL;
	rm_port_t *my_port = NULL;
	char *wire_id = NULL;
	int from_port, to_port;
	int wire_num;
	char from_node[NODE_LEN];
	char to_node[NODE_LEN];

	if (working_cluster_rec) {
		error("Can't do this cross-cluster");
		return -1;
	}
	if (!have_db2) {
		error("Can't access DB2 library, run from service node");
		return -1;
	}

	if (!bg) {
		if ((rc = bridge_get_bg(&bg)) != SLURM_SUCCESS) {
			error("bridge_get_BG(): %d", rc);
			return -1;
		}
	}

	if (bg == NULL)
		return -1;

	if ((rc = bridge_get_data(bg, RM_WireNum, &wire_num))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_BPNum): %d", rc);
		wire_num = 0;
	}
	/* find out system wires on each mp */

	for (i=0; i<wire_num; i++) {

		if (i) {
			if ((rc = bridge_get_data(bg, RM_NextWire, &my_wire))
			    != SLURM_SUCCESS) {
				error("bridge_get_data(RM_NextWire): %d", rc);
				break;
			}
		} else {
			if ((rc = bridge_get_data(bg, RM_FirstWire, &my_wire))
			    != SLURM_SUCCESS) {
				error("bridge_get_data(RM_FirstWire): %d", rc);
				break;
			}
		}
		if ((rc = bridge_get_data(my_wire, RM_WireID, &wire_id))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_FirstWire): %d", rc);
			break;
		}

		if (!wire_id) {
			error("No Wire ID was returned from database");
			continue;
		}

		if (wire_id[UNDER_POS] != '_')
			continue;
		switch(wire_id[0]) {
		case 'X':
			dim = X;
			break;
		case 'Y':
			dim = Y;
			break;
		case 'Z':
			dim = Z;
			break;
		}
		if (strlen(wire_id) < VAL_NAME_LEN) {
			error("Wire_id isn't correct %s",wire_id);
			continue;
		}

                memset(&from_node, 0, sizeof(from_node));
                memset(&to_node, 0, sizeof(to_node));
                strncpy(from_node, wire_id+2, NODE_LEN-1);
                strncpy(to_node, wire_id+UNDER_POS+1, NODE_LEN-1);
		free(wire_id);

		if ((rc = bridge_get_data(my_wire, RM_WireFromPort, &my_port))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_FirstWire): %d", rc);
			break;
		}
		if ((rc = bridge_get_data(my_port, RM_PortID, &from_port))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_PortID): %d", rc);
			break;
		}
		if ((rc = bridge_get_data(my_wire, RM_WireToPort, &my_port))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_WireToPort): %d", rc);
			break;
		}
		if ((rc = bridge_get_data(my_port, RM_PortID, &to_port))
		    != SLURM_SUCCESS) {
			error("bridge_get_data(RM_PortID): %d", rc);
			break;
		}

		source = loc2ba_mp(from_node);
		if (!source) {
			error("1 loc2ba_mp: mpid %s not known", from_node);
			continue;
		}
		if (!validate_coord(source->coord))
			continue;

		target = loc2ba_mp(to_node);
		if (!target) {
			error("2 loc2ba_mp: mpid %s not known", to_node);
			continue;
		}
		if (!validate_coord(target->coord))
			continue;

		_switch_config(source, target, dim, from_port, to_port);

		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("dim %d from %s %d -> %s %d",
			     dim,
			     source->coord_str,
			     from_port,
			     target->coord_str,
			     to_port);
	}
#else
	_switch_config(source, source, dim, 0, 0);
	_switch_config(source, source, dim, 1, 1);
	if (dim!=X) {
		_switch_config(source, target, dim, 2, 5);
		_switch_config(source, source, dim, 3, 3);
		_switch_config(source, source, dim, 4, 4);
		return 1;
	}

	if (cluster_dims == 1) {
		if (count == 0)
			_switch_config(source, source, dim, 5, 5);
		else if (count < DIM_SIZE[X]-1)
			_switch_config(source, target, dim, 2, 5);
		else
			_switch_config(source, source, dim, 2, 2);
		_switch_config(source, source, dim, 3, 3);
		_switch_config(source, source, dim, 4, 4);
		return 1;
	}

	/* set up x */
	/* always 2->5 of next. If it is the last it will go to the first.*/
	_switch_config(source, target, dim, 2, 5);

	/* set up split x */
	if (DIM_SIZE[X] == 1) {
	} else if (DIM_SIZE[X] == 4) {
		switch(count) {
		case 0:
		case 3:
			/* 0 and 3rd Node */
			/* nothing */
			break;
		case 1:
			/* 1st Node */
			target = &ba_main_grid[0]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 0th */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 2:
			/* 2nd Node */
			target = &ba_main_grid[3]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 3rd and back */
			_switch_config(source, target, dim, 4, 3);
			_switch_config(source, target, dim, 3, 4);
			break;
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else if (DIM_SIZE[X] == 5) {
		/* 4 X dim fixes for wires */
		switch(count) {
		case 0:
		case 2:
			/* 0th and 2nd node */
			/* Only the 2-5 is used here
			   so nothing else */
			break;
		case 1:
			/* 1st node */
			/* change target to 4th node */
			target = &ba_main_grid[4]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 4th */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 3:
			/* 3rd node */
			/* change target to 2th node */
			target = &ba_main_grid[2]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 2nd */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 4:
			/* 4th node */
			/* change target to 1st node */
			target = &ba_main_grid[1]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 1st */
			_switch_config(source, target, dim, 4, 3);

			break;
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else if (DIM_SIZE[X] == 8) {
		switch(count) {
		case 0:
		case 4:
			/* 0 and 4th Node */
			/* nothing */
			break;
		case 1:
		case 5:
			/* 1st Node */
			target = &ba_main_grid[count-1]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of previous */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 2:
			/* 2nd Node */
			target = &ba_main_grid[7]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of last */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 3:
			/* 3rd Node */
			target = &ba_main_grid[6]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 6th */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 6:
			/* 6th Node */
			target = &ba_main_grid[3]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 3rd */
			_switch_config(source, target, dim, 4, 3);
			break;
		case 7:
			/* 7th Node */
			target = &ba_main_grid[2]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 2nd */
			_switch_config(source, target, dim, 4, 3);
			break;
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else if (DIM_SIZE[X] == 9) {
		switch(count) {
		case 0:
		case 4:
			/* 0 and 4th Node */
			/* nothing */
		case 5:
		case 6:
		case 7:
			/*already handled below */
			break;
		case 1:
			/* 1st Node */
			target = &ba_main_grid[7]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 7th and back */
			_switch_config(source, target, dim, 4, 3);
			_switch_config(target, source, dim, 4, 3);
			break;
		case 2:
			/* 2nd Node */
			target = &ba_main_grid[6]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 6th and back */
			_switch_config(source, target, dim, 4, 3);
			_switch_config(target, source, dim, 4, 3);
			break;
		case 3:
			/* 3rd Node */
			target = &ba_main_grid[5]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 5th and back */
			_switch_config(source, target, dim, 4, 3);
			_switch_config(target, source, dim, 4, 3);
			break;
		case 8:
			/* 8th Node */
			target = &ba_main_grid[0]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of 0th */
			_switch_config(source, target, dim, 4, 3);
			break;
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else if (DIM_SIZE[X] == 13) {
		int temp_num = 0;

		switch(count) {
		case 0:
		case 6:
			/* 0 and 6th Node no split */
		case 8:
		case 9:
		case 10:
		case 11:
		case 12:
			/* already taken care of in the next case so
			 * do nothing
			 */
			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
			/* get the node count - 1 then subtract it
			 * from 12 to get the new target and then go
			 * from 4->3 and back again
			 */
			temp_num = 12 - (count - 1);
			if (temp_num < 5)
				fatal("node %d shouldn't go to %d",
				      count, temp_num);

			target = &ba_main_grid[temp_num]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 */
			_switch_config(source, target, dim, 4, 3);
			/* and back 4->3 */
			_switch_config(target, source, dim, 4, 3);
			break;
		case 7:
			/* 7th Node */
			target = &ba_main_grid[count-1]
				[source->coord[Y]]
				[source->coord[Z]];
			/* 4->3 of previous */
			_switch_config(source, target, dim, 4, 3);
			break;
		default:
			fatal("got %d for a count on a %d X-dim system",
			      count, DIM_SIZE[X]);
			break;
		}
	} else {
		fatal("We don't have a config to do a BG system with %d "
		      "in the X-dim.", DIM_SIZE[X]);
	}
#endif
	return 1;
}

static char *_set_internal_wires(List nodes, int size, int conn_type)
{
	ba_mp_t* ba_node[size+1];
	int count=0, i;
	uint16_t *start = NULL;
	uint16_t *end = NULL;
	char *name = NULL;
	ListIterator itr;
	hostlist_t hostlist;

	if (!nodes)
		return NULL;

	hostlist = hostlist_create(NULL);
	itr = list_iterator_create(nodes);
	while ((ba_node[count] = list_next(itr))) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("name = %s", ba_node[count]->coord_str);
		hostlist_push_host(hostlist, ba_node[count]->coord_str);
		count++;
	}
	list_iterator_destroy(itr);

	start = ba_node[0]->coord;
	end = ba_node[count-1]->coord;
	name = hostlist_ranged_string_xmalloc(hostlist);
	hostlist_destroy(hostlist);

	for (i=0;i<count;i++) {
		if (!ba_node[i]->used) {
			ba_node[i]->used |= BA_MP_USED_TRUE;
		} else {
			debug("No network connection to create "
			      "bgblock containing %s", name);
			debug("Use smap to define bgblocks in "
			      "bluegene.conf");
			xfree(name);
			return NULL;
		}
	}

	if (conn_type == SELECT_TORUS)
		for (i=0;i<count;i++) {
			_set_one_dim(start, end, ba_node[i]->coord);
		}

	return name;
}

/*
 * Used to find a complete path based on the conn_type for an x dim.
 * When starting to wire a block together this should be called first.
 *
 * IN/OUT: results - contains the number of midplanes we are
 *     potentially going to use in the X dim.
 * IN: ba_node - current node we are looking at and have already added
 *     to results.
 * IN: start - coordinates of the first midplane (so we know when when
 *     to end with a torus)
 * IN: x_size - How many midplanes are we looking for in the X dim
 * IN: found - count of how many midplanes we have found in the x dim
 * IN: conn_type - MESH or TORUS
 * IN: algo - algorythm to try an allocation by
 *
 * RET: 0 on failure, 1 on success
 */
static int _find_x_path(List results, ba_mp_t *ba_node,
			uint16_t *start, int x_size,
			int found, int conn_type, block_algo_t algo)
{
	ba_switch_t *curr_switch = NULL;
	ba_switch_t *next_switch = NULL;

	int port_tar = 0;
	int source_port=0;
	int target_port=1;
	int broke = 0, not_first = 0;
	int ports_to_try[2] = {4, 2};
	uint16_t *mp_tar = NULL;
	int i = 0;
	ba_mp_t *next_node = NULL;
	ba_mp_t *check_node = NULL;
/* 	int highest_phys_x = x_size - start[X]; */
/* 	info("highest_phys_x is %d", highest_phys_x); */

	ListIterator itr = NULL;

	if (!ba_node || !results || !start)
		return 0;

	curr_switch = &ba_node->axis_switch[X];

	/* we don't need to go any further */
	if (x_size == 1) {
		/* Only set this if Torus since mesh doesn't have any
		 * connections in this path */
		if (conn_type == SELECT_TORUS) {
			curr_switch->int_wire[source_port].used = 1;
			curr_switch->int_wire[source_port].port_tar =
				target_port;
			curr_switch->int_wire[target_port].used = 1;
			curr_switch->int_wire[target_port].port_tar =
				source_port;
		}
		return 1;
	}

	if (algo == BLOCK_ALGO_FIRST) {
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;
	} else if (algo == BLOCK_ALGO_SECOND) {
		ports_to_try[0] = 2;
		ports_to_try[1] = 4;
	} else {
		error("Unknown algo %d", algo);
		return 0;
	}

	if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
		info("Algo(%d) found - %d", algo, found);

	/* Check the 2 ports we can leave though in ports_to_try */
	for(i=0;i<2;i++) {
/* 		info("trying port %d", ports_to_try[i]); */
		/* check to make sure it isn't used */
		if (!curr_switch->int_wire[ports_to_try[i]].used) {
			/* looking at the next node on the switch
			   and it's port we are going to */
			mp_tar = curr_switch->
				ext_wire[ports_to_try[i]].mp_tar;
			port_tar = curr_switch->
				ext_wire[ports_to_try[i]].port_tar;
/* 			info("%c%c%c port %d goes to %c%c%c port %d", */
/* 			     alpha_num[ba_node->coord[X]], */
/* 			     alpha_num[ba_node->coord[Y]], */
/* 			     alpha_num[ba_node->coord[Z]], */
/* 			     ports_to_try[i], */
/* 			     alpha_num[mp_tar[X]], */
/* 			     alpha_num[mp_tar[Y]], */
/* 			     alpha_num[mp_tar[Z]], */
/* 			     port_tar); */
			/* check to see if we are back at the start of the
			   block */
			if ((mp_tar[X] == start[X]
			     && mp_tar[Y] == start[Y]
			     && mp_tar[Z] == start[Z])) {
				broke = 1;
				goto broke_it;
			}
			/* check to see if the port points to itself */
			if ((mp_tar[X] == ba_node->coord[X]
			     && mp_tar[Y] == ba_node->coord[Y]
			     && mp_tar[Z] == ba_node->coord[Z])) {
				continue;
			}
			/* check to see if I am going to a place I have
			   already been before */
			itr = list_iterator_create(results);
			while ((next_node = list_next(itr))) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("Algo(%d) looking at %c%c%c "
					     "and %c%c%c",
					     algo,
					     alpha_num[next_node->coord[X]],
					     alpha_num[next_node->coord[Y]],
					     alpha_num[next_node->coord[Z]],
					     alpha_num[mp_tar[X]],
					     alpha_num[mp_tar[Y]],
					     alpha_num[mp_tar[Z]]);
				if ((mp_tar[X] == next_node->coord[X]
				     && mp_tar[Y] == next_node->coord[Y]
				     && mp_tar[Z] == next_node->coord[Z])) {
					not_first = 1;
					break;
				}
			}
			list_iterator_destroy(itr);
			if (not_first && found < DIM_SIZE[X]) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("Algo(%d) already been there "
					     "before", algo);
				not_first = 0;
				continue;
			}
			not_first = 0;

		broke_it:
			next_node = &ba_main_grid[mp_tar[X]]
				[mp_tar[Y]]
				[mp_tar[Z]];
			next_switch = &next_node->axis_switch[X];

 			if ((conn_type == SELECT_MESH) && (found == (x_size))) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("Algo(%d) we found the end of "
					     "the mesh", algo);
				return 1;
			}
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
				info("Algo(%d) Broke = %d Found = %d "
				     "x_size = %d",
				     algo, broke, found, x_size);

			if (broke && (found == x_size)) {
				goto found_path;
			} else if (found == x_size) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("Algo(%d) finishing the torus!",
					     algo);

				if (deny_pass && (*deny_pass & PASS_DENY_X)) {
					info("we don't allow passthroughs 1");
					return 0;
				}

				if (best_path)
					list_flush(best_path);
				else
					best_path =
						list_create(_delete_path_list);

				if (path)
					list_flush(path);
				else
					path = list_create(_delete_path_list);

				_finish_torus(results,
					      curr_switch, 0, X, 0, start);

				if (best_count < BEST_COUNT_INIT) {
					if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
						info("Algo(%d) Found a best "
						     "path with %d steps.",
						     algo, best_count);
					_set_best_path();
					return 1;
				} else {
					return 0;
				}
			} else if (broke) {
				broke = 0;
				continue;
			}

			if (!_node_used(next_node, x_size)) {
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("Algo(%d) found %d looking at "
					     "%c%c%c %d going to %c%c%c %d",
					     algo,
					     found,
					     alpha_num[ba_node->coord[X]],
					     alpha_num[ba_node->coord[Y]],
					     alpha_num[ba_node->coord[Z]],
					     ports_to_try[i],
					     alpha_num[mp_tar[X]],
					     alpha_num[mp_tar[Y]],
					     alpha_num[mp_tar[Z]],
					     port_tar);
				itr = list_iterator_create(results);
				while ((check_node = list_next(itr))) {
					if ((mp_tar[X] == check_node->coord[X]
					     && mp_tar[Y] ==
					     check_node->coord[Y]
					     && mp_tar[Z] ==
					     check_node->coord[Z])) {
						break;
					}
				}
				list_iterator_destroy(itr);
				if (!check_node) {
					if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
						info("Algo(%d) add %c%c%c",
						     algo,
						     alpha_num[next_node->
							       coord[X]],
						     alpha_num[next_node->
							       coord[Y]],
						     alpha_num[next_node->
							       coord[Z]]);
					list_append(results, next_node);
				} else {
					if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
						info("Algo(%d) Hey this is "
						     "already added %c%c%c",
						     algo,
						     alpha_num[mp_tar[X]],
						     alpha_num[mp_tar[Y]],
						     alpha_num[mp_tar[Z]]);
					continue;
				}
				found++;

				/* look for the next closest midplane */
				if (!_find_x_path(results, next_node,
						  start, x_size,
						  found, conn_type, algo)) {
					_remove_node(results, next_node->coord);
					found--;
					continue;
				} else {
				found_path:
					if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
						info("Algo(%d) added node "
						     "%c%c%c %d %d -> "
						     "%c%c%c %d %d",
						     algo,
						     alpha_num[ba_node->
							       coord[X]],
						     alpha_num[ba_node->
							       coord[Y]],
						     alpha_num[ba_node->
							       coord[Z]],
						     source_port,
						     ports_to_try[i],
						     alpha_num[mp_tar[X]],
						     alpha_num[mp_tar[Y]],
						     alpha_num[mp_tar[Z]],
						     port_tar,
						     target_port);
					curr_switch->int_wire[source_port].used
						= 1;
					curr_switch->int_wire
						[source_port].port_tar
						= ports_to_try[i];
					curr_switch->int_wire
						[ports_to_try[i]].used = 1;
					curr_switch->int_wire
						[ports_to_try[i]].port_tar
						= source_port;

					next_switch->int_wire[port_tar].used
						= 1;
					next_switch->int_wire[port_tar].port_tar
						= target_port;
					next_switch->int_wire[target_port].used
						= 1;
					next_switch->int_wire
						[target_port].port_tar
						= port_tar;
					return 1;
				}
			}
		}
	}

	if (algo == BLOCK_ALGO_FIRST) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("Algo(%d) couldn't find path", algo);
		return 0;
	} else if (algo == BLOCK_ALGO_SECOND) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("Algo(%d) looking for the next free node "
			     "starting at %c%c%c",
			     algo,
			     alpha_num[ba_node->coord[X]],
			     alpha_num[ba_node->coord[Y]],
			     alpha_num[ba_node->coord[Z]]);

		if (best_path)
			list_flush(best_path);
		else
			best_path = list_create(_delete_path_list);

		if (path)
			list_flush(path);
		else
			path = list_create(_delete_path_list);

		_find_next_free_using_port_2(curr_switch, 0, results, X, 0);

		if (best_count < BEST_COUNT_INIT) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("Algo(%d) yes found next free %d", algo,
				     best_count);
			mp_tar = _set_best_path();

			if (deny_pass && (*deny_pass & PASS_DENY_X)
			    && (*deny_pass & PASS_FOUND_X)) {
				debug("We don't allow X passthoughs.");
				return 0;
			}
			/* info("got here with %c%c%c", */
			/*      alpha_num[mp_tar[X]], */
			/*      alpha_num[mp_tar[Y]], */
			/*      alpha_num[mp_tar[Z]]); */
			next_node = &ba_main_grid[mp_tar[X]]
				[mp_tar[Y]]
				[mp_tar[Z]];

			next_switch = &next_node->axis_switch[X];

			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("Algo(%d) found %d looking at %c%c%c "
				     "going to %c%c%c %d",
				     algo, found,
				     alpha_num[ba_node->coord[X]],
				     alpha_num[ba_node->coord[Y]],
				     alpha_num[ba_node->coord[Z]],
				     alpha_num[mp_tar[X]],
				     alpha_num[mp_tar[Y]],
				     alpha_num[mp_tar[Z]],
				     port_tar);

			list_append(results, next_node);
			found++;
			if (_find_x_path(results, next_node,
					 start, x_size, found,
					 conn_type, algo)) {
				return 1;
			} else {
				found--;
				_reset_the_path(curr_switch, 0, 1, X);
				_remove_node(results, next_node->coord);
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
					info("Algo(%d) couldn't finish "
					     "the path off this one", algo);
			}
		}

		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("Algo(%d) couldn't find path", algo);
		return 0;
	}

	error("We got here meaning there is a bad algo, "
	      "but this should never happen algo(%d)", algo);
	return 0;
}

static int _remove_node(List results, uint16_t *mp_tar)
{
	ListIterator itr;
	ba_mp_t *ba_node = NULL;

	itr = list_iterator_create(results);
	while ((ba_node = (ba_mp_t*) list_next(itr))) {

#ifdef HAVE_BG_L_P
		if (mp_tar[X] == ba_node->coord[X]
		    && mp_tar[Y] == ba_node->coord[Y]
		    && mp_tar[Z] == ba_node->coord[Z]) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("removing %c%c%c from list",
				     alpha_num[mp_tar[X]],
				     alpha_num[mp_tar[Y]],
				     alpha_num[mp_tar[Z]]);
			list_remove (itr);
			break;
		}
#else
		if (mp_tar[X] == ba_node->coord[X]) {
			if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
				info("removing %d from list",
				     mp_tar[X]);
			list_remove (itr);
			break;
		}
#endif
	}
	list_iterator_destroy(itr);
	return 1;
}

static int _find_next_free_using_port_2(ba_switch_t *curr_switch,
					int source_port,
					List nodes,
					int dim,
					int count)
{
	ba_switch_t *next_switch = NULL;
	ba_path_switch_t *path_add =
		(ba_path_switch_t *) xmalloc(sizeof(ba_path_switch_t));
	ba_path_switch_t *path_switch = NULL;
	ba_path_switch_t *temp_switch = NULL;
	uint16_t port_tar;
	int target_port = 0;
	int port_to_try = 2;
	uint16_t *mp_tar= curr_switch->ext_wire[0].mp_tar;
	uint16_t *node_src = curr_switch->ext_wire[0].mp_tar;
	int used = 0;
	int broke = 0;
	ba_mp_t *ba_node = NULL;

	ListIterator itr;

	path_add->geometry[X] = node_src[X];
#ifdef HAVE_3D
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
#endif
	path_add->dim = dim;
	path_add->in = source_port;

	if (count>=best_count)
		goto return_0;

	itr = list_iterator_create(nodes);
	while ((ba_node = (ba_mp_t*) list_next(itr))) {
		if (mp_tar[X] == ba_node->coord[X]
		    && mp_tar[Y] == ba_node->coord[Y]
		    && mp_tar[Z] == ba_node->coord[Z]) {
			broke = 1;
			break;
		}
	}
	list_iterator_destroy(itr);

	if (!broke && count>0 &&
	    !ba_main_grid[mp_tar[X]][mp_tar[Y]][mp_tar[Z]].used) {
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
			info("this one not found %c%c%c",
			     alpha_num[mp_tar[X]],
			     alpha_num[mp_tar[Y]],
			     alpha_num[mp_tar[Z]]);
		broke = 0;

		if ((source_port%2))
			target_port=1;

		list_flush(best_path);

		path_add->out = target_port;
		list_push(path, path_add);

		itr = list_iterator_create(path);
		while ((path_switch = (ba_path_switch_t*) list_next(itr))){

			temp_switch = (ba_path_switch_t *)
				xmalloc(sizeof(ba_path_switch_t));

			temp_switch->geometry[X] = path_switch->geometry[X];
			temp_switch->geometry[Y] = path_switch->geometry[Y];
			temp_switch->geometry[Z] = path_switch->geometry[Z];
			temp_switch->dim = path_switch->dim;
			temp_switch->in = path_switch->in;
			temp_switch->out = path_switch->out;
			list_append(best_path, temp_switch);
		}
		list_iterator_destroy(itr);
		best_count = count;
		return 1;
	}

	used=0;
	if (!curr_switch->int_wire[port_to_try].used) {
		itr = list_iterator_create(path);
		while ((path_switch =
			(ba_path_switch_t*) list_next(itr))){

			if (((path_switch->geometry[X] == node_src[X])
			     && (path_switch->geometry[Y] == node_src[Y])
			     && (path_switch->geometry[Z] == mp_tar[Z]))) {
				if ( path_switch->out
				     == port_to_try) {
					used = 1;
					break;
				}
			}
		}
		list_iterator_destroy(itr);

		/* check to see if wire 0 is used with this port */
		if (curr_switch->
		    ext_wire[port_to_try].mp_tar[X]
		    == curr_switch->ext_wire[0].mp_tar[X]
		    && curr_switch->ext_wire[port_to_try].mp_tar[Y]
		    == curr_switch->ext_wire[0].mp_tar[Y]
		    && curr_switch->ext_wire[port_to_try].mp_tar[Z]
		    == curr_switch->ext_wire[0].mp_tar[Z]) {
			used = 1;
		}

		if (!used) {
			port_tar = curr_switch->
				ext_wire[port_to_try].port_tar;
			mp_tar = curr_switch->
				ext_wire[port_to_try].mp_tar;

			next_switch = &ba_main_grid
				[mp_tar[X]][mp_tar[Y]][mp_tar[Z]]
				.axis_switch[X];

			count++;
			path_add->out = port_to_try;
			list_push(path, path_add);
			_find_next_free_using_port_2(next_switch,
						     port_tar, nodes,
						     dim, count);
			while ((temp_switch = list_pop(path)) != path_add){
				xfree(temp_switch);
				if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
					info("something here 1");
			}
		}
	}
return_0:
	xfree(path_add);
	return 0;
}

/*
 * Used to tie the end of the block to the start. best_path and path
 * should both be set up before calling this function.
 *
 * IN: curr_switch -
 * IN: source_port -
 * IN: dim -
 * IN: count -
 * IN: start -
 *
 * RET: 0 on failure, 1 on success
 *
 * Sets up global variable best_path, and best_count.  On success
 * best_count will be >= BEST_COUNT_INIT you can call _set_best_path
 * to apply this path to the main system (ba_main_grid)
 */

static int _finish_torus(List results,
			 ba_switch_t *curr_switch, int source_port,
			 int dim, int count, uint16_t *start)
{
	ba_switch_t *next_switch = NULL;
	ba_path_switch_t *path_add = xmalloc(sizeof(ba_path_switch_t));
	ba_path_switch_t *path_switch = NULL;
	ba_path_switch_t *temp_switch = NULL;
	uint16_t port_tar;
	int target_port=0;
	int ports_to_try[2] = {3,5};
	uint16_t *mp_tar= curr_switch->ext_wire[0].mp_tar;
	uint16_t *node_src = curr_switch->ext_wire[0].mp_tar;
	int i;
	int used=0;
	ListIterator itr;

	path_add->geometry[X] = node_src[X];
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];

	path_add->dim = dim;
	path_add->in = source_port;

	if (count>=best_count) {
		xfree(path_add);
		return 0;
	}
	if (mp_tar[X] == start[X]
	    && mp_tar[Y] == start[Y]
	    && mp_tar[Z] == start[Z]) {

		if ((source_port%2))
			target_port=1;
		if (!curr_switch->int_wire[target_port].used) {

			list_flush(best_path);

			path_add->out = target_port;
			list_push(path, path_add);

			itr = list_iterator_create(path);
			while ((path_switch = list_next(itr))) {
				temp_switch = xmalloc(sizeof(ba_path_switch_t));

				temp_switch->geometry[X] =
					path_switch->geometry[X];
				temp_switch->geometry[Y] =
					path_switch->geometry[Y];
				temp_switch->geometry[Z] =
					path_switch->geometry[Z];

				temp_switch->dim = path_switch->dim;
				temp_switch->in = path_switch->in;
				temp_switch->out = path_switch->out;
				list_append(best_path,temp_switch);
			}
			list_iterator_destroy(itr);
			best_count = count;
			return 1;
		}
	}

	if (source_port==0 || source_port==3 || source_port==5) {
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;
	}

	for(i=0;i<2;i++) {
		used=0;
		if (!curr_switch->int_wire[ports_to_try[i]].used) {
			itr = list_iterator_create(path);
			while ((path_switch = list_next(itr))){

				if (((path_switch->geometry[X] == node_src[X])
				     && (path_switch->geometry[Y]
					 == node_src[Y])
				     && (path_switch->geometry[Z]
					 == mp_tar[Z]))) {
					if ( path_switch->out
					     == ports_to_try[i]) {
						used = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr);

			/* check to see if wire 0 is used with this port */
			if ((curr_switch->
			     ext_wire[ports_to_try[i]].mp_tar[X] ==
			     curr_switch->ext_wire[0].mp_tar[X] &&
			     curr_switch->
			     ext_wire[ports_to_try[i]].mp_tar[Y] ==
			     curr_switch->ext_wire[0].mp_tar[Y] &&
			     curr_switch->
			     ext_wire[ports_to_try[i]].mp_tar[Z] ==
			     curr_switch->ext_wire[0].mp_tar[Z])) {
				continue;
			}


			if (!used) {
				ba_mp_t *next_node = NULL;
				port_tar = curr_switch->
					ext_wire[ports_to_try[i]].port_tar;
				mp_tar = curr_switch->
					ext_wire[ports_to_try[i]].mp_tar;

				/* Check to see if I am going to a place I have
				   already been before, because even
				   though we may be able to do this
				   electrically this doesn't mean the
				   under lying infrastructure will
				   allow it. */
				itr = list_iterator_create(results);
				while ((next_node = list_next(itr))) {
					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("finishing_torus: "
						     "looking at %c%c%c "
						     "and %c%c%c",
						     alpha_num[next_node->
							       coord[X]],
						     alpha_num[next_node->
							       coord[Y]],
						     alpha_num[next_node->
							       coord[Z]],
						     alpha_num[mp_tar[X]],
						     alpha_num[mp_tar[Y]],
						     alpha_num[mp_tar[Z]]);
					if ((mp_tar[X] == next_node->coord[X])
					    && (mp_tar[Y]
						== next_node->coord[Y])
					    && (mp_tar[Z]
						== next_node->coord[Z])) {
						break;
					}
				}
				list_iterator_destroy(itr);
				if (next_node) {
					if (ba_debug_flags & DEBUG_FLAG_BG_ALGO)
						info("finishing_torus: "
						     "Can't finish torus with "
						     "%c%c%c we already were "
						     "there.",
						     alpha_num[next_node->
							       coord[X]],
						     alpha_num[next_node->
							       coord[Y]],
						     alpha_num[next_node->
							       coord[Z]]);
					continue;
				}

				next_switch = &ba_main_grid
					[mp_tar[X]][mp_tar[Y]][mp_tar[Z]]
					.axis_switch[dim];


				count++;
				path_add->out = ports_to_try[i];
				list_push(path, path_add);
				_finish_torus(results, next_switch, port_tar,
					      dim, count, start);
				while ((temp_switch = list_pop(path))
				       != path_add){
					xfree(temp_switch);
					if (ba_debug_flags
					    & DEBUG_FLAG_BG_ALGO_DEEP)
						info("something here 3");
				}
			}
		}
	}
	xfree(path_add);
	return 0;
}

/*
 * using best_path set up previously from _finish_torus or
 * _find_next_free_using_port_2.  Will set up the path contained there
 * into the main virtual system.  With will also set the passthrough
 * flag if there was a passthrough used.
 */
static uint16_t *_set_best_path()
{
	ListIterator itr;
	ba_path_switch_t *path_switch = NULL;
	ba_switch_t *curr_switch = NULL;
	uint16_t *geo = NULL;

	if (!best_path)
		return NULL;

	itr = list_iterator_create(best_path);
	while ((path_switch = (ba_path_switch_t*) list_next(itr))) {
		if (deny_pass && path_switch->in > 1 && path_switch->out > 1) {
			*deny_pass |= PASS_FOUND_X;
			debug2("got a passthrough in X");
		}
		if (ba_debug_flags & DEBUG_FLAG_BG_ALGO_DEEP)
			info("mapping %c%c%c %d->%d",
			     alpha_num[path_switch->geometry[X]],
			     alpha_num[path_switch->geometry[Y]],
			     alpha_num[path_switch->geometry[Z]],
			     path_switch->in, path_switch->out);
		if (!geo)
			geo = path_switch->geometry;
		curr_switch = &ba_main_grid
			[path_switch->geometry[X]]
			[path_switch->geometry[Y]]
			[path_switch->geometry[Z]].
			axis_switch[path_switch->dim];

		curr_switch->int_wire[path_switch->in].used = 1;
		curr_switch->int_wire[path_switch->in].port_tar =
			path_switch->out;
		curr_switch->int_wire[path_switch->out].used = 1;
		curr_switch->int_wire[path_switch->out].port_tar =
			path_switch->in;
	}
	list_iterator_destroy(itr);

	best_count=BEST_COUNT_INIT;
	return geo;
}

static int _set_one_dim(uint16_t *start, uint16_t *end, uint16_t *coord)
{
	int dim;
	ba_switch_t *curr_switch = NULL;

	for(dim=0;dim<cluster_dims;dim++) {
		if (start[dim]==end[dim]) {
			curr_switch = &ba_main_grid
				[coord[X]][coord[Y]][coord[Z]].axis_switch[dim];

			if (!curr_switch->int_wire[0].used
			    && !curr_switch->int_wire[1].used) {
				curr_switch->int_wire[0].used = 1;
				curr_switch->int_wire[0].port_tar = 1;
				curr_switch->int_wire[1].used = 1;
				curr_switch->int_wire[1].port_tar = 0;
			}
		}
	}
	return 1;
}

static void _destroy_geo(void *object)
{
	uint16_t *geo_ptr = (uint16_t *)object;
	xfree(geo_ptr);
}

