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
#include <math.h>

int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {4,4,4};

struct pa_node*** _pa_system_list;

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
int _listfindf_pa_node(pa_node_t* A, pa_node_t* B);
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
bool _find_first_match_aux(pa_request_t* pa_request, int dim2check, int const_dim, 
			   int dimA, int dimB, List* results);
/* check to see if the conf_result matches */
bool _check_pa_node(pa_node_t* pa_node, int geometry, 
 		    int conn_type, bool force_contig,
		    dimension_t dim, int cur_node_id);
/* */
void _process_results(List results, pa_request_t* request);
/* */
void _process_result(pa_node_t* result, pa_request_t* request, List* result_indices);
/* */
void _get_result_indices(List results, pa_request_t* request, 
			 List** result_indices);
/* */
void _delete_result_indices(List* result_indices);
/** print out the result indices */
void _print_result_indices(List* result_indices);
/* */
int _listfindf_int(int* A, int* B);
/* */
int _cmpf_int(int* A, int* B);
/* returns true if node_id array is contiguous (e.g. [53241])*/
bool _is_contiguous(int size, int* node_id);
/* */
static void _print_results(List results);
/* */
static void _insert_result(List results, pa_node_t* result);


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

	printf("pa_node:\t");
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		printf("%d", pa_node->coord[i]);
	}
	printf("\n");
	printf("        used:\t%d\n", pa_node->used);
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		printf("   conf list:\t%s\n", convert_dim(i));
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
	if (init_system(switch_config_list, num_nodes)){
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
 * greedy algorithm for finding first match
 */
int _find_first_match(pa_request_t* pa_request, List* results)
{
	int cur_dim=0, cur_node_id, k=0, x=0, y=0, z=0;
	int found_count[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	bool match_found, request_filled = false;
	int* geometry = pa_request->geometry;
	int conn_type = pa_request->conn_type;
	bool force_contig = pa_request->force_contig;

	*results = list_create(NULL);

	for (z=0; z<DIM_SIZE[Z]; z++){ 
	for (y=0; y<DIM_SIZE[Y]; y++){ 
	for (x=0; x<DIM_SIZE[X]; x++){
		cur_dim = X;
		cur_node_id = x;

		if (found_count[cur_dim] != geometry[cur_dim]){
			pa_node_t* pa_node = &(_pa_system[x][y][z]);
			// printf("address of pa_node %d%d%d(%s) 0x%p\n",
			// x,y,z, convert_dim(cur_dim), pa_node);
			match_found = _check_pa_node(pa_node,
						     geometry[cur_dim],
						     conn_type, force_contig,
						     cur_dim, cur_node_id);
			if (match_found){
				/* now we recursively snake down the remaining dimensions 
				 * to see if they can also satisfy the request. 
				 */
				/* "remaining_OK": remaining nodes can support the configuration */
				bool remaining_OK = _find_first_match_aux(pa_request, X, Y, x, z, results);
				if (remaining_OK){
					/* insert the pa_node_t* into the List of results */
					_insert_result(*results, pa_node);
#ifdef DEBUG_PA
					// printf("_find_first_match: found match for %s = %d%d%d\n",
					// convert_dim(cur_dim), x,y,z); 
#endif
						
					found_count[cur_dim]++;
					if (found_count[cur_dim] == geometry[cur_dim]){
#ifdef DEBUG_PA
						// printf("_find_first_match: found full match for %s dimension\n", 
						// convert_dim(cur_dim));
#endif
						request_filled = true;
						goto done;
					}
				} else {
#ifdef DEBUG_PA
					printf("_find_first_match: match NOT found for other dimensions,"
					       " resetting previous results\n"); 
#endif
					for (k=0; k<PA_SYSTEM_DIMENSIONS; k++){
						found_count[k] = 0;
					}
					list_destroy(*results);
					*results = list_create(NULL);
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
		
	} /* X dimension for loop for */
	/* if we've gone past a whole row/column/z-thingy..and
	 * still haven't found a match, we need to start over
	 * on the matching.
	 */
	/*
	if (found_count[X] != geometry[X]) {
#ifdef DEBUG_PA
		printf("_find_first_match: match NOT found for X dimension,"
		       " resetting previous results\n"); 
#endif
		for (k=0; k<PA_SYSTEM_DIMENSIONS; k++){
			found_count[k] = 0;
		}
		list_destroy(*results);
		*results = list_create(NULL);
	}
	*/
	} /* Y dimension for loop */
	/* if we've gone past a whole row/column/z-thingy..and
	 * still haven't found a match, we need to start over
	 * on the matching.
	 */
	/*
	if (found_count[Y] != geometry[Y]) {
#ifdef DEBUG_PA
		printf("_find_first_match: match NOT found for Y dimension,"
		       " resetting previous results\n"); 
#endif
		for (k=0; k<PA_SYSTEM_DIMENSIONS; k++){
			found_count[k] = 0;
		}
		list_destroy(*results);
		*results = list_create(NULL);
	}
	*/
	} /* Z dimension for loop*/
	
 done:
	/* if the request is filled, we then need to touch the
	 * projections of the allocated partition to reflect the
	 * correct effect on the system.
	 */
	if (request_filled){
#ifdef DEBUG_PA
		printf("_find_first_match: match found for request %d%d%d\n",
		       geometry[X], geometry[Y], geometry[Z]);
#endif
		_process_results(*results, pa_request);
		// _print_results(*results);

	} else {
		list_destroy(*results);
#ifdef DEBUG_PA
		printf("_find_first_match: error, couldn't "
		       "find match for request %d%d%d\n",
		       geometry[X], geometry[Y], geometry[Z]);
#endif
	}
	
	return 0;
}

/**
 * auxilliary recursive call.
 * 
 * 
 */
bool _find_first_match_aux(pa_request_t* pa_request, int dim2check, int var_dim,
			   int dimA, int dimB, List* results)
{
	int i=0;
	int found_count[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	bool match_found, request_filled = false;
	int* geometry = pa_request->geometry;
	int conn_type = pa_request->conn_type;
	bool force_contig = pa_request->force_contig;
	int *a, *b, *c;

	/** we want to go up the Y dim, but checking for correct X size*/
	for (i=0; i<DIM_SIZE[var_dim]; i++){

		pa_node_t* pa_node;
		if (var_dim == X){
			printf("_find_first_match_aux: aaah, this should never happen\n");
			return false;
			
		} else if (var_dim == Y){
			a = &dimA;
			b = &i;
			c = &dimB;

		} else {
			a = &dimA;
			b = &dimB;
			c = &i;
		}

		// printf("_find_first_match_aux pa_node %d%d%d(%s) dim2check %s\n",
		// pa_node->coord[X], pa_node->coord[Y], pa_node->coord[Z], 
		// convert_dim(var_dim), convert_dim(dim2check));
		pa_node = &(_pa_system[*a][*b][*c]);
		if (found_count[dim2check] != geometry[dim2check]){
			match_found = _check_pa_node(pa_node,
						     geometry[var_dim],
						     conn_type, force_contig,
						     var_dim, i);
			if (match_found){
				bool remaining_OK;
				if (dim2check == X){
					/* index "i" should be the y dir here */
					remaining_OK = _find_first_match_aux(pa_request, Y, Z, dimA, i, results);
				} else {
					remaining_OK = true;
				}

				if (remaining_OK){
					/* insert the pa_node_t* into the List of results */
					_insert_result(*results, pa_node);

#ifdef DEBUG_PA
					//					printf("_find_first_match_aux: found match for %s = %d%d%d\n",
					// convert_dim(dim2check), pa_node->coord[0], pa_node->coord[1], pa_node->coord[2]); 
#endif
					
					found_count[dim2check]++;
					if (found_count[dim2check] == geometry[dim2check]){
#ifdef DEBUG_PA
						; // printf("_find_first_match_aux: found full match for %s dimension\n", convert_dim(dim2check));
#endif
						request_filled = true;
						goto done;
					}
				} else {
					goto done;
				}
			} 
		}
	}

 done:
	/* if the request is filled, we then return our result to the
	 * previous caller */

	return request_filled;
}

bool _check_pa_node(pa_node_t* pa_node, int geometry, 
 		    int conn_type, bool force_contig,
		    dimension_t dim, int cur_node_id)
{
	ListIterator itr;
	conf_result_t* conf_result;
	int i=0, j = 0;

	/* printf("check_pa_node: node to check against %s %d\n", convert_dim(dim), cur_node_id); */
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
				if (conf_result->conf_data->node_id[i][j] == cur_node_id){
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
	ListIterator itr;
	pa_node_t* result;
	List* result_indices;

#ifdef DEBUG_PA
	printf("*****************************\n");
	printf("****  processing results ****\n");
	printf("*****************************\n");
#endif

	/* create a list of REFERENCEs to the indices of the results */
	_get_result_indices(results, request, &result_indices);
#ifdef DEBUG_PA
	_print_result_indices(result_indices);
#endif
	
	itr = list_iterator_create(results);
	while((result = (pa_node_t*) list_next(itr))){
		_process_result(result, request, result_indices);
	}	
	list_iterator_destroy(itr);
	_delete_result_indices(result_indices);
}

/**
 * process the result respective of the original request 
 * 
 * all cur_dim nodes for x = 0, z = 1 must have a request[cur_dim]
 * part for the partition where the node num = coord[cur_dim] in the
 * cur_dim config list.
 */
void _process_result(pa_node_t* result, pa_request_t* pa_request, List* result_indices)
{
	ListIterator itr;
	int cur_dim, cur_size;
	int i=0,j=0,k=0, x=0,y=0,z=0;
	int num_part;
	conf_result_t* conf_result;
	bool conf_match;
	result->used = true;
	pa_node_t* pa_node;
	

#ifdef DEBUG_PA
	printf("processing result for %d%d%d\n", 
	       result->coord[X], result->coord[Y], result->coord[Z]);
#endif
	int *a, *b, *c;
	for (cur_dim=0; cur_dim<PA_SYSTEM_DIMENSIONS; cur_dim++){
		for(i=0; i<DIM_SIZE[cur_dim]; i++){
			if (cur_dim == X){
				a = &i;
				b = &y;
				c = &z;
			} else if (cur_dim == Y) {
				a = &x;
				b = &i;
				c = &z;
			} else {
				a = &x;
				b = &y;
				c = &i;
			}
				
			pa_node = &(_pa_system[*a][*b][*c]);
			// printf("touching Z %d%d%d\n", x, y, i);
			if (_is_down_node(pa_node)){
				// printf("down node X %d%d%d\n", x, y, i);
				continue;
			}
			/* if the node only has one remaining configuration left, then
			 * that's the configuration it's going to have, so we don't
			 * need to narrow it down any more */
			if (list_count(pa_node->conf_result_list[cur_dim]) == 1){
				continue;
			}
			itr = list_iterator_create(pa_node->conf_result_list[cur_dim]);
			while((conf_result = (conf_result_t*) list_next(itr))){
				/*
				printf("node %d%d%d list_count %d for %s\n", 
				       *a,*b,*c,
				       list_count(pa_node->conf_result_list[cur_dim]),
				       convert_dim(cur_dim));
				*/
				/* all config list entries
				 * must have these matching
				 * data
				 * - request[cur_dim];
				 * - coord[cur_dim];
				 */
				num_part = conf_result->conf_data->num_partitions;
				/* we have to check each of the partions for the correct
				 * node_id that has the correct size, conn_type, etc. */
				conf_match = false;
				/* check all the partitions */
				for(j=0; j<num_part; j++){
					cur_size = conf_result->conf_data->partition_sizes[j];
					/* check geometry of the partition */
					if (cur_size == pa_request->geometry[cur_dim]){
						/* now we check to see if the node_id's match. 
						 */
						for (k=0; k<conf_result->conf_data->partition_sizes[j]; k++){
							if (!list_find_first(result_indices[cur_dim],
									     (ListFindF) _listfindf_int,
									     &(conf_result->conf_data->node_id[j][k]))){
								goto next_partition;
							}
						}
						if (conf_result->conf_data->partition_type[j] != pa_request->conn_type){
							goto next_partition;							
						}
						if (pa_request->force_contig){
							if (_is_contiguous(cur_size,
									   conf_result->conf_data->node_id[j]))
								conf_match = true;
						} else {
							conf_match = true;
							/* break back out to the next conf_list */
							break;
						}
					}
				next_partition:
					; // noop target to jump to next partition
				}
				/* if it doesn't match, remove it */
				if (conf_match == false){
					list_remove(itr);
				}
			}
			list_iterator_destroy(itr);
		}
	}
}

/** 
 * get the indices of the results in a sorted int** array 
 * 
 * returned stucture must be freed with _delete_result_indices
 */
// int _get_result_indices(List pa_node_list, pa_request_t* request, 
// 			int*** result_indices)
void _get_result_indices(List pa_node_list, pa_request_t* request, 
			List** result_indices)
{
	int i;
	ListIterator itr;
	pa_node_t* pa_node;

	*result_indices = (List*) xmalloc(sizeof(List)*PA_SYSTEM_DIMENSIONS);
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		(*result_indices)[i] = list_create(NULL);
	}

	itr = list_iterator_create(pa_node_list);
	/* go through one time and get the minimums of each */
	while((pa_node = (pa_node_t*) list_next(itr))){
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			if (!list_find_first((*result_indices)[i], 
					     (ListFindF) _listfindf_int, 
					     &(pa_node->coord[i])))
				list_append((*result_indices)[i], &(pa_node->coord[i]));
		}
	}	

	list_iterator_destroy(itr);

	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		list_sort((*result_indices)[i], (ListCmpF) _cmpf_int);
	}
}

/* */
void _delete_result_indices(List* result_indices)
{
	int i;
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		list_destroy(result_indices[i]);
	}
	xfree(result_indices);
}

/** print out the result indices */
void _print_result_indices(List* result_indices)
{
	int i, *int_ptr;
	ListIterator itr;
	printf("result indices: \n");
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		printf(" (%s)", convert_dim(i));
		itr = list_iterator_create(result_indices[i]);
		while((int_ptr = (int*) list_next(itr))){
			printf("%d", *int_ptr);
		}		
	}
	printf("\n");
}

/** 
 * detect whether the node_id's are continguous, despite their being 
 * sorted or not.
 *
 * imagine we had 53142, we first find the lowest (eg. 1), 
 * then start filling up the bool array: 
 * 5: [00001]
 * 3: [00101]
 * 1: [10101]
 * 4: [10111]
 * 2: [11111]
 * 
 * then we'll know that the set of nodes is contiguous. 
 * 
 * returns true if node_ids are contiguous
 */
bool _is_contiguous(int size, int* node_id)
{
	int i;
	bool* cont;
	int node_min = BIG_MAX;
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

/* */
static void _insert_result(List results, pa_node_t* result)
{
	if (!list_find_first(results, (ListFindF) _listfindf_pa_node, result))
		list_append(results, result);
}

/* */
int _listfindf_pa_node(pa_node_t* A, pa_node_t* B)
{
	int i;
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		if (A->coord[i] != B->coord[i]){
			return false;
		}
	}

	return true;
}

/* */
int _listfindf_int(int* A, int* B)
{
	return (*A == *B);
}

/*  */
int _cmpf_int(int* A, int* B)
{
	if (*A == *B)
		return 0;
	else if (*A < *B)
		return -1;
	else 
		return 1;
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
		   int geometry[PA_SYSTEM_DIMENSIONS], int size, 
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
	if (geometry[0]){
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
	xfree(pa_request->geometry);
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
	printf("pa_request:");
	printf("  geometry:\t");
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		printf("%d", pa_request->geometry[i]);
	}
	printf("\n");
	printf("      size:\t%d\n", pa_request->size);
	printf(" conn_type:\t%s\n", convert_conn_type(pa_request->conn_type));
	printf("    rotate:\t%d\n", pa_request->rotate);
	printf("  elongate:\t%d\n", pa_request->elongate);
	printf("force contig:\t%d\n", pa_request->force_contig);
}

/**
 * Initialize internal structures by either reading previous partition
 * configurations from a file or by running the graph solver.
 * 
 * IN: dunno yet, probably some stuff denoting downed nodes, etc.
 * 
 * return: success or error of the intialization.
 */
void pa_init()
{
	int i;
	List switch_config_list;

	_conf_result_list = (List*) xmalloc(sizeof(List) * PA_SYSTEM_DIMENSIONS);
	
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		_conf_result_list[i] = list_create(delete_conf_result);
	}

	/** 
	 * hard code in configuration until we can read it from a file 
	 */
	/** yes, I know, y and z configs are the same, but i'm doing this
	 * in case they change
	 */

	/* create the X configuration (8 nodes) */
	switch_config_list = list_create(delete_gen);
	create_config_8_1d(switch_config_list);
	if (_get_part_config(switch_config_list, _conf_result_list[X])){
		printf("Error getting configuration\n");
		exit(0);
	}
	list_destroy(switch_config_list);

	/* create the Y configuration (4 nodes) */
	switch_config_list = list_create(delete_gen);
	create_config_4_1d(switch_config_list);
	if (_get_part_config(switch_config_list, _conf_result_list[Y])){
		printf("Error getting configuration\n");
		exit(0);
	}
	list_destroy(switch_config_list);

	/* create the Z configuration (4 nodes) */
	switch_config_list = list_create(delete_gen);
	create_config_4_1d(switch_config_list);
	if (_get_part_config(switch_config_list, _conf_result_list[Z])){
		printf("Error getting configuration\n");
		exit(0);
	}
	list_destroy(switch_config_list);


	_create_pa_system();
	_initialized = true;
}

/** 
 * destroy all the internal (global) data structs.
 */
void pa_fini()
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
void set_node_down(int c[PA_SYSTEM_DIMENSIONS])
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
 * OUT - results: List of results of the allocation request.  Each
 * list entry will be a coordinate.  allocate_part will create the
 * list, but the caller must destroy it.
 * 
 * return: success or error of request
 */
int allocate_part(pa_request_t* pa_request, List* results)
{
	if (!_initialized){
		printf("allocate_part Error, configuration not initialized, call init_configuration first\n");
		return 0;
	}

	if (!pa_request){
		printf("allocate_part Error, request not initialized\n");
		return 0;
	}

#ifdef DEBUG_PA
	print_pa_request(pa_request);
#endif

	_find_first_match(pa_request, results);
	return 1;
}

/** */
int main(int argc, char** argv)
{
	
	pa_init();

	/*
	ListIterator itr;
	conf_result_t* conf_result;

	itr = list_iterator_create(_pa_system[1][2][1].conf_result_list[0]);
	while((conf_result = (conf_result_t*) list_next(itr))){
		print_conf_result(conf_result);
	}
	list_iterator_destroy(itr);
	*/

	/*
	  int dead_node1[3] = {0,0,0};
	  int dead_node2[3] = {1,0,0};
	set_node_down(dead_node1);
	set_node_down(dead_node2);
	printf("done setting node down\n");
	*/
	// _print_pa_system();
	
	int geo[3] = {2,2,2};
	bool rotate = false;
	bool elongate = false;
	bool force_contig = true;
	List results;
	pa_request_t* request; 
	new_pa_request(&request, geo, -1, rotate, elongate, force_contig, TORUS);
	
	// int i;
	// for (i=0; i<8; i++){
	// _print_pa_system();
	if (!allocate_part(request, &results)){
		printf("allocate success for %d%d%d\n", 
		       geo[0], geo[1], geo[2]);
		// _print_results(results);
		list_destroy(results);
	}
	// }

	if (!allocate_part(request, &results)){
		printf("allocate success for %d%d%d\n", 
		       geo[0], geo[1], geo[2]);
		// _print_results(results);
		list_destroy(results);
	}
	_print_pa_system();

	delete_pa_request(request);
	
	pa_fini();
	return 0;
}
