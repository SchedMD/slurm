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
#define PA_SYSTEM_DIMENSIONS 3
int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {4,4,4};

typedef struct pa_request {
	int* geometry;
	int size; 
	int conn_type;
	bool rotate;
	bool elongate; 
	bool force_contig;
} pa_request_t; 

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
List pa_system_copies;

/** 
 * pa_node: node within the allocation system.  Note that this node is
 * hard coded for 1d-3d only!  (just have the higher order dims as
 * null if you want lower dimensions).
 */
typedef struct pa_node {
	/* set if using this node in a partition*/
	bool used;

	/* coordinates */
	int coord[PA_SYSTEM_DIMENSIONS];

	/* shallow copy of the conf_results.  initialized and used as
	 * array of Lists accessed by dimension, ie conf_result_list[dim]
	 */
	List* conf_result_list; 
	
} pa_node_t;

/** internal helper functions */
/** */
void _new_pa_node(pa_node_t* pa_node, int* coordinates);
// void _new_pa_node(pa_node_t** pa_node);
/** */
void _print_pa_node();
/** */
void _delete_pa_node(pa_node_t* pa_node);
/** */
bool _is_down_node(pa_node_t* pa_node);
/* */
void _create_pa_system();
/* */
void _print_pa_system();
// void _create_pa_system(pa_node_t**** pa_system, List* conf_result_list);
/* */
void _delete_pa_system();
// void _delete_pa_system(pa_node_t*** pa_system);
/* run the graph solver to get the conf_result(s) */
int _get_part_config(List port_config_list, List conf_result_list);
/* find the first partition match in the system */
int _find_first_match(pa_request_t* pa_request, List* results);
/* check to see if the conf_result matches */
bool _check_pa_node(pa_node_t* pa_node, int geometry, 
 		    int conn_type, bool force_contig,
		    dimension_t dim, int current_node_id);
/* */
void _process_results(List results, pa_request_t* request);
/* */
void _process_result(pa_node_t* result, pa_request_t* request);
/* returns true if node_id array is contiguous (e.g. [53241])*/
bool _is_contiguous(int size, int* node_id);
/* */
static void _print_results(List results);


/** */
void _new_pa_node(pa_node_t* pa_node, int* coord)
{
	int i;
	pa_node->used = false;

	// pa_node = (pa_node_t*) xmalloc(sizeof(pa_node_t));
	pa_node->conf_result_list = (List*) xmalloc(sizeof(List) * PA_SYSTEM_DIMENSIONS);
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		pa_node->coord[i] = coord[i];
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

	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		if (pa_node->conf_result_list[i]){
			list_destroy(pa_node->conf_result_list[i]);
		}
	}

	xfree(pa_node->conf_result_list);
	pa_node->conf_result_list = NULL;	
}

/** return true if the node is a "down" node*/
bool _is_down_node(pa_node_t* pa_node)
{
	if (!pa_node || pa_node->conf_result_list == NULL){
		return true;
	}
	return false;
}

/** */
void _print_pa_node(pa_node_t* pa_node)
{
	int i;
	conf_result_t* conf_result;
	ListIterator itr;

	if (pa_node == NULL){
		printf("_print_pa_node Error, pa_node is NULL\n");
		return;
	}

	printf("pa_node :\t");
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		printf("%d", pa_node->coord[i]);
	}
	printf("\n");
	printf("        used :\t%d\n", pa_node->used);
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		itr = list_iterator_create(pa_node->conf_result_list[i]);
		while((conf_result = (conf_result_t*) list_next(itr))){
			print_conf_result(conf_result);
		}
		list_iterator_destroy(itr);
	}
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
				int coord[PA_SYSTEM_DIMENSIONS] = {x,y,z};
				_new_pa_node(&(_pa_system[x][y][z]), coord);

				for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
					list_copy(_conf_result_list[i], 
						  &(_pa_system[x][y][z].conf_result_list[i]));
				}
			}
		}
	}
}

/** */
void _print_pa_system()
{
	int x=0,y=0,z=0;
	printf("pa_system: \n");
	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++){
			for (z=0; z<DIM_SIZE[Z]; z++){
				printf(" pa_node %d%d%d 0x%p: \n", x, y, z,
				       &(_pa_system[x][y][z]));
				_print_pa_node(&(_pa_system[x][y][z]));
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
	if(init_system(switch_config_list, num_nodes)){
		printf("error initializing system\n");
		goto cleanup;
	}
	/* first we find the partition configurations for the separate
	 * dimensions
	 */
	if (find_all_tori(part_config_list)){
		printf("error finding all tori\n");
		goto cleanup;
	}
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
// 999
int _find_first_match(pa_request_t* pa_request, List* results)
//int _find_first_match(int* geometry, int conn_type, bool force_contig,
//		      List *results)
{
	int cur_dim=0, k=0, x=0, y=0, z=0;
	int found_count[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	bool match_found, request_filled = false;
	int* geometry = pa_request->geometry;
	int conn_type = pa_request->conn_type;
	bool force_contig = pa_request->force_contig;

	*results = list_create(NULL);

	for (x=0; x<DIM_SIZE[X]; x++){
	for (y=0; y<DIM_SIZE[Y]; y++){ 
	for (z=0; z<DIM_SIZE[Z]; z++){ 

		for (cur_dim=0; cur_dim<PA_SYSTEM_DIMENSIONS; cur_dim++){
			int current_node_id;
			if (cur_dim == X)
				current_node_id = x;
			else if (cur_dim == Y)
				current_node_id = y;
			else 
				current_node_id = z;

			if (found_count[cur_dim] != geometry[cur_dim]){
				pa_node_t* pa_node = &(_pa_system[x][y][z]);
				if (pa_node->conf_result_list == NULL){
					printf("conf_result_list is NULL\n");
				}
				printf("address of pa_node %d%d%d %s 0x%p\n", 
				       x,y,z, convert_dim(cur_dim), &(_pa_system[x][y][z]));
				match_found = _check_pa_node(&(_pa_system[x][y][z]),
							     geometry[cur_dim],
							     conn_type, force_contig,
							     cur_dim, current_node_id);
							     
				if (match_found){
					/* insert the pa_node_t* into the List of results */ 
					list_append(*results, &(_pa_system[x][y][z]));
#ifdef DEBUG_PA
					printf("_find_first_match: found match for %s = %d%d%d\n",
					       convert_dim(cur_dim), x,y,z); 
#endif

					found_count[cur_dim]++;
					if (found_count[cur_dim] == geometry[cur_dim]){
#ifdef DEBUG_PA
						printf("_find_first_match: found full match for %s dimension\n", convert_dim(cur_dim));
#endif
					}
				} 
			}
		}

		/* check whether we're done */
		bool all_found = true;
		for (k=0; k<PA_SYSTEM_DIMENSIONS; k++){
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
		for (k=0; k<PA_SYSTEM_DIMENSIONS; k++){
			found_count[k] = 0;
		}
		list_destroy(*results);
		*results = list_create(NULL);
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
		for (k=0; k<PA_SYSTEM_DIMENSIONS; k++){
			found_count[k] = 0;
		}
		/* this seems fater than popping off all the elements */
		list_destroy(*results);
		*results = list_create(NULL);
	}
	} /* X dimension for loop*/
	
 done:
	/* if the request is filled, we then need to touch the
	 * projections of the allocated partition to reflect the
	 * correct effect on the system.
	 */
	if (request_filled){
		_process_results(*results, pa_request);

		printf("_find_first_match: match found for request %d%d%d\n",
		       geometry[X], geometry[Y], geometry[Z]);
		// 999
		// _print_results(*results);
	} else {
		list_destroy(*results);
		printf("_find_first_match: error, couldn't "
		       "find match for request %d%d%d\n",
		       geometry[X], geometry[Y], geometry[Z]);
	}
	
	return 0;
}

bool _check_pa_node(pa_node_t* pa_node, int geometry, 
 		    int conn_type, bool force_contig,
		    dimension_t dim, int current_node_id)
{
	ListIterator itr;
	conf_result_t* conf_result;
	int i=0, j = 0;

	if (_is_down_node(pa_node)){
		return false;
	}

	/* if we've used this node in another partition already */
	if (pa_node->used)
		return false;

	itr = list_iterator_create(pa_node->conf_result_list[dim]);
	while((conf_result = (conf_result_t*) list_next(itr) )){
		for (i=0; i<conf_result->conf_data->num_partitions; i++){
				
			/* check that the size and connection type match */
			int cur_size = conf_result->conf_data->partition_sizes[i];
			/* check that we're checking on the node specified */
			for (j=0; j<cur_size; j++){
				if (conf_result->conf_data->node_id[i][j] == current_node_id){
					if (cur_size == geometry && 
					    conf_result->conf_data->partition_type[i] == conn_type){
						if (force_contig){
							if (_is_contiguous(cur_size, conf_result->conf_data->node_id[i]))
								return true;
						} else {
							return true;
						}
					}
				}
			}
		}
	}
	list_iterator_destroy(itr);
	
	return false;
}

/**
 * process the results respective of the original request 
 */
void _process_results(List results, pa_request_t* request)
{
	ListIterator itr = list_iterator_create(results);
	pa_node_t* result;
	printf("\n\n*****************************\n");
	printf("****  processing results ****\n");
	printf("*****************************\n");
	while((result = (pa_node_t*) list_next(itr))){
		_process_result(result, request);
	}	
	printf("done processing results\n");
}

/**
 * process the result respective of the original request 
 * 
 * all cur_dim nodes for x = 0, z = 1 must have a request[cur_dim]
 * part for the partition where the node num = coord[cur_dim] in the
 * cur_dim config list.
 */
void _process_result(pa_node_t* result, pa_request_t* pa_request)
{
	ListIterator itr;
	int cur_dim, cur_size;
	int i=0,j=0,k=0, x=0,y=0,z=0;
	int num_part;
	conf_result_t* conf_result;
	bool conf_match;
	result->used = true;
	
	x = result->coord[X];
	y = result->coord[Y];
	z = result->coord[Z];

	// 999
	printf("processing result for %d%d%d\n", x,y,z);
	for (cur_dim=0; cur_dim<PA_SYSTEM_DIMENSIONS; cur_dim++){
		for(i=0; i<DIM_SIZE[cur_dim]; i++){
			if (cur_dim == X){
				printf("touching X %d%d%d\n", i, y, z);
				if (_is_down_node(&(_pa_system[i][y][z]))){
					return;
				}

				itr = list_iterator_create(_pa_system[i][y][z].conf_result_list[cur_dim]);
			} else if (cur_dim == Y){
				printf("touching Y %d%d%d\n", x, i, z);
				if (_is_down_node(&(_pa_system[x][i][z]))){
					return;
				}

				itr = list_iterator_create(_pa_system[x][i][z].conf_result_list[cur_dim]);
			} else {
				printf("touching Z %d%d%d\n", x, y, i);
				if (_is_down_node(&(_pa_system[x][y][i]))){
					return;
				}
				
				itr = list_iterator_create(_pa_system[x][y][i].conf_result_list[cur_dim]);
			}
			
			while((conf_result = (conf_result_t*) list_next(itr))){
				/* all config list entries
				 * must have these matching
				 * data
				 * - request[cur_dim];
				 * - coord[cur_dim];
				 */
				num_part = conf_result->conf_data->num_partitions;
				/* we have to check each of the partions for the correct
				 * node_id that has the correct size */
				conf_match = false;
				for(j=0; j<num_part; j++){
					cur_size = conf_result->conf_data->partition_sizes[j];
					if (cur_size == pa_request->geometry[cur_dim]){
						for (k = 0; k<cur_size; k++){
							if (conf_result->conf_data->node_id[j][k] == 
							    result->coord[cur_dim] &&
							    conf_result->conf_data->partition_type[j] == 
							    pa_request->conn_type){
								if (pa_request->force_contig){
									if (_is_contiguous(cur_size,
											   conf_result->conf_data->node_id[j]))
										conf_match = true;
								} else {
									printf("force contig false\n");
									conf_match = true;
								}
							}
						}
					}
				}
				/* if it doesn't match, remove it */
				if (conf_match == false){
					printf("conf didn't match, removing: ");
					print_conf_result(conf_result);
					list_remove(itr);
				} else {
					printf("conf matched: ");
					print_conf_result(conf_result);
				}
			}
			list_iterator_destroy(itr);
		}
	}
	printf("done processing result\n");
	exit(0);
}

/** 
 * imagine we had 53142, we first find the lowest (eg. 1), 
 * then start filling up the bool array: 
 * 5: [00001]
 * 3: [00101]
 * 1: [10101]
 * 4: [10111]
 * 2: [11111]
 * 
 * then we'll know that the set of nodes is contiguous
 * 
 * returns true if node_ids are contiguous
 */
bool _is_contiguous(int size, int* node_id)
{
	int i;
	bool* cont;
	int node_min = 9999;
	bool result = true;
	
	if (size < 1){
		return false;
	}

	if (size == 1){
		return true;
	}

	cont = (bool*) xmalloc(sizeof(bool)*size);
	/* first we need to find the diff index between the node_id and
	 * the bool* cont array */
	for (i=0; i<size; i++){
		if (node_id[i] < node_min){
			node_min = node_id[i];
		}
	}

	for (i=0; i<size; i++){
		int index = node_id[i] - node_min;
		if (index < 0 || index >= size){
			result = false;
			goto done;
		} else {
			cont[index] = true;
		}
	}

	for (i=0; i<size; i++){
		if (cont[i] == false){
			result = false;
			goto done;
		}
	}

 done: 
	xfree(cont);
	return result;
}

/* print out the list of results */
static void _print_results(List results)
{
	ListIterator itr = list_iterator_create(results);
	pa_node_t* result;
	printf("Results: \n");
	while((result = (pa_node_t*) list_next(itr))){
		_print_pa_node(result);
	}
}

/**
 * create a partition request.  Note that if the geometry is given,
 * then size is ignored.  
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
 * return SUCCESS of operation.
 */
int new_pa_request(pa_request_t** pa_request, 
		   int* geometry, int size, 
		   bool rotate, bool elongate, 
		   bool force_contig, int conn_type)
{
	int i, sz=1;

	if (!_initialized){
		printf("Error, configuration not initialized, call init_configuration first\n");
		return 1;
	}

	*pa_request = (pa_request_t*) xmalloc(sizeof(pa_request_t));
	(*pa_request)->geometry = (int*) xmalloc(sizeof(int)* PA_SYSTEM_DIMENSIONS);
	/* size will be overided by geometry size if given */
	if (geometry){
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			if (geometry[i] < 1 || geometry[i] > DIM_SIZE[i]){
				printf("new_pa_request Error, request geometry is invalid\n"); 
				return 1;
			}
		}

		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			(*pa_request)->geometry[i] = geometry[i];
			sz *= geometry[i];
		}
		(*pa_request)->size = sz;
	} else {
		/* decompose the size into a cubic geometry */
		int i;
		if ( ((size%2) != 0 || size < 1) && size != 1){
			printf("allocate_part_by_size ERROR, requested size must be greater than "
			       "0 and a power of 2 (of course, size 1 is allowed)\n");
			return 1;
		}

		if (size == 1){
			for (i=0; i<PA_SYSTEM_DIMENSIONS; i++)
				(*pa_request)->geometry[i] = 1;
		} else {
			int literal = size / pow(2,(PA_SYSTEM_DIMENSIONS-1));
			for (i=0; i<PA_SYSTEM_DIMENSIONS; i++)
				(*pa_request)->geometry[i] = literal;
		}

		(*pa_request)->size = size;
	}
	(*pa_request)->conn_type = conn_type;
	(*pa_request)->rotate = rotate;
	(*pa_request)->elongate = elongate;
	(*pa_request)->force_contig = force_contig;

	return 0;
}

/**
 * delete a partition request 
 */
void delete_pa_request(pa_request_t *pa_request)
{
	xfree(pa_request);
}

/**
 * print a partition request 
 */
void print_pa_request(struct pa_request* pa_request)
{
	int i;

	if (pa_request == NULL){
		printf("print_pa_request Error, request is NULL\n");
		return;
	}
	printf("pa_request :");
	printf("  geometry :\t");
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		printf("%d", pa_request->geometry[i]);
	}
	printf("\n");
	printf("      size :\t%d\n", pa_request->size);
	printf(" conn_type :\t%s\n", convert_conn_type(pa_request->conn_type));
	printf("    rotate :\t%d\n", pa_request->rotate);
	printf("  elongate :\t%d\n", pa_request->elongate);
	printf("force contig :\t%d\n", pa_request->force_contig);
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
	List switch_config_list;

	_conf_result_list = (List*) xmalloc(sizeof(List) * PA_SYSTEM_DIMENSIONS);
	
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		_conf_result_list[i] = list_create(delete_conf_result);
	}

	num_nodes = 4;
	/** 
	 * hard code in configuration until we can read it from a file 
	 */
	/** yes, I know, y and z configs are the same, but i'm doing this
	 * in case they change
	 */

	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		switch_config_list = list_create(delete_gen);
		create_config_4_1d(switch_config_list);
		if (_get_part_config(switch_config_list, _conf_result_list[i])){
			printf("Error getting configuration\n");
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

	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++) {
		list_destroy(_conf_result_list[i]);
	}
	xfree(_conf_result_list);
	_delete_pa_system();
	printf("pa system destroyed\n");
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
 * Try to allocate a partition.
 * 
 * IN - pa_request: allocation request
 * OUT - bitmap: bitmap of the partition allocated
 * 
 * return: success or error of request
 */
int allocate_part(pa_request_t* pa_request, bitstr_t** bitmap)
{
	List results = NULL;
	if (!_initialized){
		printf("allocate_part Error, configuration not initialized, call init_configuration first\n");
		return 1;
	}

	if (!pa_request){
		printf("allocate_part Error, request not initialized\n");
		return 1;
	}

	print_pa_request(pa_request);
	_find_first_match(pa_request, &results);

	if (!results)
		xfree(results);
	return 0;
}

/** */
int main(int argc, char** argv)
{
	
	init();

	/*
	ListIterator itr;
	conf_result_t* conf_result;

	itr = list_iterator_create(_pa_system[1][2][1].conf_result_list[0]);
	while((conf_result = (conf_result_t*) list_next(itr))){
		print_conf_result(conf_result);
	}
	list_iterator_destroy(itr);
	*/

	int c[3] = {0,0,0};
	set_node_down(c);
	printf("done setting node down\n");

	// _print_pa_system();
	
	int geo[3] = {2,2,2};
	bool rotate = false;
	bool elongate = false;
	bool force_contig = true;
	bitstr_t* result;
	pa_request_t* request; 
	new_pa_request(&request, geo, -1, rotate, elongate, force_contig, TORUS);
	if (!allocate_part(request, &result)){
		printf("allocate success for %d%d%d\n", 
		       geo[0], geo[1], geo[2]);
	}
	
	if (!allocate_part(request, &result)){
		printf("allocate success for %d%d%d\n", 
		       geo[0], geo[1], geo[2]);
	}
	delete_pa_request(request);
	
	fini();
	return 0;
}
