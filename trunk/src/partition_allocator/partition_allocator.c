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
#define MY_SYSTEM_DIMENSIONS 3
int DIM_SIZE[MY_SYSTEM_DIMENSIONS] = {4,4,4};

/**
 * These lists hold the partition data and corresponding
 * configurations.  The structs that define the elements in the list
 * are in graph_solver.h
 * 
 * these lists hold the actual conf_results, while the lists in the
 * pa_node only hold shallow copies (addresses) to those elements.
 */
List* _conf_result_list;
bool _initialized = false;


/** some internal structures used in the partition allocator alone 
 * 
 * the partition virtual system is a linked list where each node has a
 * link to the other neighbor nodes and holds the list of possible
 * configurations for the X, Y, and Z switches.
 */
struct pa_node*** _pa_system;
/** 
 * pa_node: node within the allocation system.  Note that this node is
 * hard coded for 1d-3d only!  (just have the higher order dims as
 * null if you want lower dimensions).
 */
typedef struct pa_node {
	/* shallow copy of the conf_results */
	List* conf_result_list; 
	
} pa_node_t;

/** internal helper functions */
/* */
void _create_pa_system();
// void _create_pa_system(pa_node_t**** pa_system, List* conf_result_list);
/* */
void _delete_pa_system();
// void _delete_pa_system(pa_node_t*** pa_system);
/** */
void _new_pa_node(pa_node_t* pa_node);
// void _new_pa_node(pa_node_t** pa_node);
/** */
void _delete_pa_node(pa_node_t* pa_node);
/* run the graph solver to get the conf_result(s) */
int _get_part_config(List port_config_list, List conf_result_list);
/* find the first partition match in the system */
int _find_first_match(int* geometry, int conn_type);
/* check to see if the conf_result matches */
bool _check_pa_node(pa_node_t* pa_node, int geometry, 
		    int conn_type, dimension_t cur_dim, 
		    int current_node_id);

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
	_pa_system = (pa_node_t***) xmalloc(sizeof(pa_node_t**) * DIM_SIZE[X]);
	for (x=0; x<DIM_SIZE[X]; x++){
		_pa_system[x] = (pa_node_t**) xmalloc(sizeof(pa_node_t*) * DIM_SIZE[Y]);

		for (y=0; y<DIM_SIZE[Y]; y++){

			_pa_system[x][y] = (pa_node_t*) xmalloc(sizeof(pa_node_t) * DIM_SIZE[Z]);
			for (z=0; z<DIM_SIZE[Z]; z++){

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

	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++){
			for (z=0; z<DIM_SIZE[Z]; z++){
				_delete_pa_node(&(_pa_system[x][y][z]));
			}
			xfree(_pa_system[x][y]);
		}
		xfree(_pa_system[x]);
	}
	xfree(_pa_system);
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

/**
 * find the first partition match in the system 
 *
 * note that inherent in this function, because of the way we loop
 * through the elements, we are fitting in a bottom-left first fashion.
 */
int _find_first_match(int* geometry, int conn_type)
{
	int dim, i, j, k, x, y, z;
	int found_count[MY_SYSTEM_DIMENSIONS] = {0,0,0};
	ListIterator itr;
	conf_result_t* conf_result;
	bool match_found, request_filled = false;

	for (x=0; x<DIM_SIZE[X]; x++){

		/* if we've already found a match for this dimension
		 * note that this can be reset if any of the dimensions fail.
		 */
		if (found_count[X] == geometry[X]){
			printf("skipping X dim\n");
		} else {
			pa_node_t* pa_node = &(_pa_system[x][y][z]);
			if (pa_node->conf_result_list == NULL){
				printf("conf_result_list is nULL\n");
			}
			printf("address of pa_node %d\n", _pa_system[x][y][z]);
			exit(0);
			match_found = _check_pa_node(&(_pa_system[x][y][z]), 
						     geometry[X], conn_type, X,
						     x);
		}

	for (y=0; y<DIM_SIZE[Y]; y++){ 
		if (found_count[Y] == geometry[Y]){
			printf("skipping Y dim\n");
		} else {
			match_found = _check_pa_node(&(_pa_system[x][y][z]), 
						     geometry[Y], conn_type, Y,
						     y);
		}

	for (z=0; z<DIM_SIZE[Z]; z++){ 

		if (found_count[Z] == geometry[Z]){
			printf("skipping Z dim\n");
		} else {
			match_found = _check_pa_node(&(_pa_system[x][y][z]), 
						     geometry[Z], conn_type, Z, 
						     z);

			if (match_found){
				found_count[Z]++;
				if (found_count[Z] == geometry[Z]){
#ifdef DEBUG_PA
					printf("_find_first_match: match found for Z dimension\n"); 
#endif
				}
			} 
		}

		/* check whether we're done */
		bool all_found = true;
		for (k=0; k<MY_SYSTEM_DIMENSIONS; k++){
			if (found_count[k] != geometry[k]) {
				all_found = false;
				break;
			}
		}
		if (all_found){
			request_filled = true;
			goto done;
		}

	} /* Z dimension for loop for */
	/* if we've gone past a whole row/column/z-thingy..and
	 * still haven't found a match, we need to start over
	 * on the matching.
	 */
	if (found_count[Z] != geometry[Z]) {
#ifdef DEBUG_PA
		printf("_find_first_match: match NOT found for Z dimension,"
		       " resetting previous results\n"); 
#endif
		for (k=0; k<MY_SYSTEM_DIMENSIONS; k++){
			found_count[k] = 0;
		}
	}

	} /* Y dimension for loop */
		/* if we've gone past a whole row/column/z-thingy..and
		 * still haven't found a match, we need to start over
		 * on the matching.
		 */
		if (found_count[Y] != geometry[Y]) {
#ifdef DEBUG_PA
			printf("_find_first_match: match NOT found for Y dimension,"
			       " resetting previous results\n"); 
#endif
			for (k=0; k<MY_SYSTEM_DIMENSIONS; k++){
				found_count[k] = 0;
			}
		}
	} /* X dimension for loop*/
	
 done:
	if (request_filled){
		printf("_find_first_match: match found for request %d%d%d\n",
		       geometry[X], geometry[Y], geometry[Z]);
	} else {
		printf("_find_first_match: error, couldn't "
		       "find match for request %d%d%d\n",
		       geometry[X], geometry[Y], geometry[Z]);
	}
	
	return 0;
}

bool _check_pa_node(pa_node_t* pa_node, int geometry, 
		    int conn_type, dimension_t dim, 
		    int current_node_id)
{
	ListIterator itr;
	conf_result_t* conf_result;
	int i, j;

	/** this is the designation of a DOWN_NODE_*/
	if (pa_node->conf_result_list == NULL)
		return false;

	itr = list_iterator_create(pa_node->conf_result_list[dim]);
	while((conf_result = (conf_result_t*) list_next(itr) )){
		
		for (i=0; i<conf_result->conf_data->num_partitions; i++){
				
			/* check that the size and connection type match */
			int curr_size = conf_result->conf_data->partition_sizes[i];
			/* check that we're checking on the node specified */
			for (j=0; j<curr_size; j++){
				if (conf_result->conf_data->node_id[i][j] == current_node_id){
					if (curr_size == geometry && 
					    conf_result->conf_data->partition_type[i] == conn_type){
						return true;
						/* we could explore and see if the two nodes are consecutive
						 * but we'd have to check where we are, and the plus one or
						 * minus one to the other node (if another node exists),
						 * which is a big hassle compared to simply iterating
						 * one more time.
						 */
					}
				}
			}
		}
	}
	list_iterator_destroy(itr);
	
	return false;
}


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
 * IN - connection type of request (TORUS or MESH)
 * OUT - bitmap: bitmap of the partition allocated
 * 
 * return: success or error of request
 */
int allocate_part_by_size(int size, bool elongate, int conn_type, 
			  bitstr_t** bitmap)
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

	rc = allocate_part_by_geometry(geometry, false, conn_type, bitmap);
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
 * IN - connection type of request (TORUS or MESH)
 * OUT - bitmap: bitmap of the partition allocated
 * 
 * return: success or error of request
 */
int allocate_part_by_geometry(int* geometry, bool rotate, int conn_type,
			      bitstr_t** bitmap)
{
	if (!_initialized){
		printf("Error, configuration not initialized, call init_configuration first\n");
		return 1;
	}


#ifdef DEBUG_PA	
	printf("allocate_part_by_geometry: request <%d%d%d> %s\n",
	       geometry[X], geometry[Y], geometry[Z], 
	       (rotate) ? "rotate" : "no rotate");
#endif
	/** first we do a search for the wanted geometry */
	_find_first_match(geometry, conn_type);

	/* if found, we then need to touch the projections of the
	 * allocated partition to reflect the correct effect on the
	 * system.
	 */
	return 0;
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

	/*
	int c[3] = {1,2,1};
	set_node_down(c);
	printf("done setting node down\n");
	*/

	int request[3] = {2,2,2};
	bool rotate = false;
	bitstr_t* result;
	allocate_part_by_geometry(request, false, TORUS, &result);

	fini();
	return 0;
}
