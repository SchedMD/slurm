/*****************************************************************************\
 *  partition_allocator.c
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

#include <stdlib.h> // for exit(int);
#include "partition_allocator.h"
#include "graph_solver.h"
#include "math.h"

#define DEBUG_PA
#define X_DIMENSIONS 8
#define Y_DIMENSIONS 4
#define Z_DIMENSIONS 4

/** some internal structures used in the partition allocator alone 
 * 
 * the partition virtual system is a linked list where each node has a
 * link to the other neighbor nodes and holds the list of possible
 * configurations for the X, Y, and Z switches.
 */

/** 
 * pa_node: node within the allocation system.  Note that this node is
 * hard coded for 1d-3d only!  (just have the higher order dims as
 * null if you want lower dimensions).
 */
typedef struct pa_node {
	/* shallow copy of the conf_results */
	List* conf_result_list; 
	List x_conf_result_list, y_conf_result_list, z_conf_result_list;
	
} pa_node_t;

/**
 * These lists hold the partition data and corresponding
 * configurations.  The structs that define the elements in the list
 * are in graph_solver.h
 * 
 * these lists hold the actual conf_results, while the lists in the
 * pa_node only hold shallow copies (addresses) to those elements.
 */
pa_node_t*** _pa_system;
List* _conf_result_list;
bool _initialized = false;

/** internal helper functions */
int _get_part_config(List port_config_list, List conf_result_list);
void _create_pa_system();
void _delete_pa_system();

/** */
void _new_pa_node(pa_node_t* pa_node);
/** */
void _delete_pa_node(pa_node_t* pa_node);

/**
 * Initialize internal structures by either reading previous partition
 * configurations from a file or by running the graph solver.
 * 
 * IN: dunno yet, probably some stuff denoting downed nodes, etc.
 * 
 * return: success or error of the intialization.
 */
void init()
{
	int i, num_nodes;
	List switch_config_list = list_create(delete_gen);

	_conf_result_list = (List*) xmalloc(sizeof(List) * SYSTEM_DIMENSIONS);
	
	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		_conf_result_list[i] = list_create(delete_conf_result);
	}

	num_nodes = 4;
	/** 
	 * hard code in configuration until we can read it from a file 
	 */
	/** yes, I know, y and z configs are the same, but i'm doing this
	 * in case they change
	 */

	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		create_config_4_1d(switch_config_list);
		if (_get_part_config(switch_config_list, _conf_result_list[i])){
			printf("Error getting X configuration\n");
			exit(0);
		}
		list_destroy(switch_config_list);
	}

	_create_pa_system();
	_initialized = true;
}

/** 
 * destroy all the internal (global) data structs.
 */
void fini()
{
	int i;

	for (i=0; i<SYSTEM_DIMENSIONS; i++) {
		list_destroy(_conf_result_list[i]);
	}
	xfree(_conf_result_list);
	_delete_pa_system();
}


/** 
 * set the node in the internal configuration as unusable
 * 
 * IN c: coordinate of the node to put down
 */
void set_node_down(int* c)
{
	if (!_initialized){
		printf("Error, configuration not initialized, call init_configuration first\n");
		return;
	}

#ifdef DEBUG_PA
	printf("set_node_down: node to set down: [%d%d%d]\n", c[0], c[1], c[2]); 
#endif

	/* basically set the node as NULL */
	_delete_pa_node(&(_pa_system[c[0]][c[1]][c[2]]));
}

/** 
 * Try to allocate a partition of the given size.  If elongate is
 * true, the algorithm will try to fit that a partition of cubic shape
 * and then it will try other elongated geometries.  
 * (ie, 2x2x2 -> 4x2x1 -> 8x1x1)
 * 
 * Note that size must be a power of 2, given 3 dimensions.
 * 
 * IN - size: requested size of partition
 * IN - elongate: if true, will try to fit different geometries of
 *      same size requests
 * OUT - bitmap: bitmap of the partition allocated
 * 
 * return: success or error of request
 */
int allocate_part_by_size(int size, bool elongate, bitstr_t** bitmap)
{
	/* decompose the size into a cubic geometry */
	int geometry[SYSTEM_DIMENSIONS];
	int rc, i;

	if (!_initialized){
		printf("Error, configuration not initialized, call init_configuration first\n");
		return 1;
	}

	if ( ((size%2) != 0 || size < 1) && size != 1){
		printf("allocate_part_by_size ERROR, requested size must be greater than "
		       "0 and a power of 2 (of course, size 1 is allowed)\n");
		return 1;
	}

	if (size == 1){
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = 1;
	} else {
		int literal = size / pow(2,(SYSTEM_DIMENSIONS-1));
		for (i=0; i<SYSTEM_DIMENSIONS; i++)
			geometry[i] = literal;
	}

	rc = allocate_part_by_geometry(geometry, false, bitmap);
	// 	if (rc && elongate){
		/* create permutations, try request again for other
		 * geometries. */
		
	// 	}
	return rc;
}


/** 
 * Try to allocate a partition of the given geometery.  This function
 * is more flexible than allocate_part_by_size by allowing
 * configurations that are not restricted by the power of 2
 * restriction.
 * 
 * IN - size: requested size of partition
 * IN - rotate: if true, allows rotation of partition during fit
 * OUT - bitmap: bitmap of the partition allocated
 * 
 * return: success or error of request
 */
int allocate_part_by_geometry(int* geometry, bool rotate, bitstr_t** bitmap)
{
	if (!_initialized){
		printf("Error, configuration not initialized, call init_configuration first\n");
		return 1;
	}


	/** first we do a search for the wanted geometry */
	_find_first_match(geometry);

	/* if found, we then need to touch the projections of the
	 * allocated partition to reflect the correct effect on the
	 * system.
	 */
	return 0;
}

/** 
 * get the partition configuration for the given port configuration
 */
int _get_part_config(List switch_config_list, List part_config_list)
{
	int num_nodes = 4;
	int rc = 1;
	printf("initializing internal structures\n");
	if(init_system(switch_config_list, num_nodes)){
		printf("error initializing system\n");
		goto cleanup;
	}
	/* first we find the partition configurations for the separate
	 * dimensions
	 */
	printf("find all tori\n");
	if (find_all_tori(part_config_list)){
		printf("error finding all tori\n");
		goto cleanup;
	}
	printf("done\n");
	rc = 0;

 cleanup:
	delete_system();
	return rc;
}

/** */
void _new_pa_node(pa_node_t* pa_node)
{
	int i;
	// pa_node = (pa_node_t*) xmalloc(sizeof(pa_node_t));
	pa_node->conf_result_list = (List*) xmalloc(sizeof(List) * SYSTEM_DIMENSIONS);
	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		pa_node->conf_result_list[i] = NULL;
	}
}

/** destroy the shallow copies of the list, and then the pa_node */
void _delete_pa_node(pa_node_t* pa_node)
{
	int i;
	if (!pa_node || !pa_node->conf_result_list){
		return;
	}

	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		if (pa_node->conf_result_list[i]){
			list_destroy(pa_node->conf_result_list[i]);
		}
	}

	xfree(pa_node->conf_result_list);
}

/** */
void _create_pa_system()
{
	int i, x, y, z;
	_pa_system = (pa_node_t***) xmalloc(sizeof(pa_node_t**) * X_DIMENSIONS);
	for (x=0; x<X_DIMENSIONS; x++){
		_pa_system[x] = (pa_node_t**) xmalloc(sizeof(pa_node_t*) * Y_DIMENSIONS);

		for (y=0; y<Y_DIMENSIONS; y++){

			_pa_system[x][y] = (pa_node_t*) xmalloc(sizeof(pa_node_t) * Z_DIMENSIONS);
			for (z=0; z<Z_DIMENSIONS; z++){

				_new_pa_node(&(_pa_system[x][y][z]));
				for (i=0; i<SYSTEM_DIMENSIONS; i++){

					list_copy(_conf_result_list[i], 
						  &(_pa_system[x][y][z].conf_result_list[i]));
				}
			}
		}
	}
}

/** */
void _delete_pa_system()
{
	int x, y, z;

	if (!_initialized){
		return;
	}

	for (x=0; x<X_DIMENSIONS; x++){
		for (y=0; y<Y_DIMENSIONS; y++){
			for (z=0; z<Z_DIMENSIONS; z++){
				_delete_pa_node(&(_pa_system[x][y][z]));
			}
			xfree(_pa_system[x][y]);
		}
		xfree(_pa_system[x]);
	}
	xfree(_pa_system);
}

/** */
int main(int argc, char** argv)
{
	ListIterator itr;
	conf_result_t* conf_result;
	init();
	/* now we sit and wait for requests */
	
	/*
	itr = list_iterator_create(_pa_system[1][2][1].conf_result_list[0]);
	while((conf_result = (conf_result_t*) list_next(itr))){
		print_conf_result(conf_result);
	}
	list_iterator_destroy(itr);
	*/

	int c[3] = {1,2,1};
	set_node_down(c);
	printf("done setting node down\n");

	fini();
	return 0;
}
