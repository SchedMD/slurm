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

#include <stdlib.h>
#include <math.h>
#include "partition_allocator.h"
#include "graph_solver.h"

#define EQUAL_ADDRESS(a,b) \
_STMT_START {		\
	(a) = (b);	\
} _STMT_END

int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {8,4,4};
#define DEBUG_PA

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
typedef struct pa_node*** pa_system_t;

/* _pa_system is the "current" system that the structures will work
 *  on */
pa_system_t _pa_system;
List _pa_system_list;

/** internal helper functions */
/** */
void _new_pa_node(pa_node_t* pa_node, int* coordinates);
/** */
void _print_pa_node();
/** */
void _delete_pa_node(pa_node_t* pa_node);
/** */
int _listfindf_pa_node(pa_node_t* A, pa_node_t* B);
/** */
bool _is_down_node(pa_node_t* pa_node);
/* */
void _create_pa_system(pa_system_t* pa_system, List* conf_result_list);
/* */
void _print_pa_system(pa_system_t pa_system);
/* */
void _delete_pa_system(void* object);
/* */
void _delete_pa_system_ptr(void* object);
/* */
void _copy_pa_system(pa_system_t source, pa_system_t* target);
/* */
void _backup_pa_system();
/** load the partition configuration from file */
int _load_part_config(char* filename, List part_config_list);
/* run the graph solver to get the conf_result(s) */
int _get_part_config(int num_nodes, List port_config_list, List conf_result_list);
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
int _parse_conf_result(char *in_line, List config_result_list);
/** */
int _tokenize_int(char* source, int size, char* delimiter, int* int_array);
/** */
int _tokenize_node_ids(char* source, int size, int* partition_sizes, 
		       char *delimiter, char *separator, int** node_id);
int _tokenize_port_conf_list(char* source, char* delimiter, 
			     char* separator, List port_conf_list);
int _tokenize_port_conf(char* source, char* delimiter,
			List port_conf_list);
/** */
void _reset_pa_system();
/** */
void set_ptr(void* A, void* B);

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
void _create_pa_system(pa_system_t* pa_system, List* conf_result_list)
{
	int i, x, y, z;
	*pa_system = (pa_node_t***) xmalloc(sizeof(pa_node_t**) * DIM_SIZE[X]);
	for (x=0; x<DIM_SIZE[X]; x++){
		(*pa_system)[x] = (pa_node_t**) xmalloc(sizeof(pa_node_t*) * DIM_SIZE[Y]);

		for (y=0; y<DIM_SIZE[Y]; y++){
			(*pa_system)[x][y] = (pa_node_t*) xmalloc(sizeof(pa_node_t) * DIM_SIZE[Z]);

			for (z=0; z<DIM_SIZE[Z]; z++){
				int coord[PA_SYSTEM_DIMENSIONS] = {x,y,z};
				_new_pa_node(&((*pa_system)[x][y][z]), coord);

				for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
					list_copy(conf_result_list[i], 
						  &((*pa_system)[x][y][z].conf_result_list[i]));
				}
			}
		}
	}
}

/** */
void _print_pa_system(pa_system_t pa_system)
{
	int x=0,y=0,z=0;
	printf("pa_system: %d%d%d\n", DIM_SIZE[X], DIM_SIZE[Y], DIM_SIZE[Z]);
	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++){
			for (z=0; z<DIM_SIZE[Z]; z++){
				printf(" pa_node %d%d%d 0x%p: \n", x, y, z,
				       &(pa_system[x][y][z]));
				_print_pa_node(&(pa_system[x][y][z]));
			}
		}
	}
}

/** */
void _delete_pa_system(void* object)
{
	int x, y, z;
	pa_system_t pa_system = (pa_system_t) object;
	
	if (!pa_system){
		return;
	}
	
	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++){
			for (z=0; z<DIM_SIZE[Z]; z++){
				_delete_pa_node(&(pa_system[x][y][z]));
			}			
			xfree(pa_system[x][y]);
		}
		xfree(pa_system[x]);
	}
	xfree(pa_system);
}

/** */
void _delete_pa_system_ptr(void* object)
{
	int x, y, z;
	pa_system_t* pa_system = (pa_system_t*) object;
	
	if (!pa_system){
		return;
	}
	
	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++){
			for (z=0; z<DIM_SIZE[Z]; z++){
				// _delete_pa_node(&(pa_system[x][y][z]));
			}			
			// pa_node_t* A = &(*pa_system)[x][y];
			// xfree(pa_system[x][y]);
			// xfree(*A);
		}
		// xfree((*pa_system)[x]);
	}
	// xfree(*pa_system);
}

/* */
void _copy_pa_system(pa_system_t source, pa_system_t* target)
{
	int i, x, y, z;
	*target = (pa_node_t***) xmalloc(sizeof(pa_node_t**) * DIM_SIZE[X]);
	for (x=0; x<DIM_SIZE[X]; x++){
		(*target)[x] = (pa_node_t**) xmalloc(sizeof(pa_node_t*) * DIM_SIZE[Y]);

		for (y=0; y<DIM_SIZE[Y]; y++){
			(*target)[x][y] = (pa_node_t*) xmalloc(sizeof(pa_node_t) * DIM_SIZE[Z]);
			
			for (z=0; z<DIM_SIZE[Z]; z++){
				int coord[PA_SYSTEM_DIMENSIONS] = {x,y,z};
				_new_pa_node(&((*target)[x][y][z]), coord);

				for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
					if (source[x][y][z].conf_result_list[i] != NULL)
						list_copy(source[x][y][z].conf_result_list[i],
							  &((*target)[x][y][z].conf_result_list[i]));
				}
			}
		}
	}
}

void set_ptr(void* A, void* B)
{
	A = B;
}

void _backup_pa_system()
{
	pa_system_t* new_system;
	new_system = (pa_system_t*) xmalloc(sizeof(pa_system_t));
	_copy_pa_system(_pa_system, new_system);
	list_push(_pa_system_list, &_pa_system);
	set_ptr(&_pa_system, new_system);
}

/** load the partition configuration from file */
int _load_part_config(char* filename, List config_result_list)
{

	FILE *conf_file;	/* pointer to input data file */
	int line_num;		/* line number in input file */
	char in_line[BUFSIZE];	/* input line */
	int i, j, error_code;

#ifdef DEBUG_PA
	printf("loading partition configuration from %s\n", filename);
#endif

	/* initialization */
	conf_file = fopen(filename, "r");
	if (conf_file == NULL)
		fatal("_load_part_config error opening file %s, %m",
		      filename);

	/* process the data file */
	line_num = 0;
	while (fgets(in_line, BUFSIZE, conf_file) != NULL) {
		line_num++;
		if (strlen(in_line) >= (BUFSIZE - 1)) {
			error("_load_part_configig line %d, of input file %s "
			      "too long", line_num, filename);
			fclose(conf_file);
			return E2BIG;
			break;
		}

		/* everything after a non-escaped "#" is a comment */
		/* replace comment flag "#" with an end of string (NULL) */
		/* escape sequence "\#" translated to "#" */
		for (i = 0; i < BUFSIZE; i++) {
			if (in_line[i] == (char) NULL)
				break;
			if (in_line[i] != '#')
				continue;
			if ((i > 0) && (in_line[i - 1] == '\\')) {
				for (j = i; j < BUFSIZE; j++) {
					in_line[j - 1] = in_line[j];
				}
				continue;
			}
			in_line[i] = (char) NULL;
			break;
		}
		
		/* parse what is left, non-comments */
		/* partition configuration parameters */
		error_code = _parse_conf_result(in_line, config_result_list);
		
		/* report any leftover strings on input line */
		report_leftover(in_line, line_num);
	}
	fclose(conf_file);
	
	return error_code;
}

/*
 * _parse_conf_result - parse the partition configuration result,
 * build table and set values
 * IN/OUT in_line - line from the configuration file
 * 
 * RET 0 if no error, error code otherwise
 */
int _parse_conf_result(char *in_line, List config_result_list)
{
	int error_code = SLURM_SUCCESS;
	conf_result_t *conf_result;
	conf_data_t *conf_data;

	/* stuff used for tokenizing */
	char *delimiter=",", *separator="/";

	/* elements of the file */
	int num_partitions;
	char *part_sizes = NULL, *part_types = NULL, *node_ids = NULL, *port_confs = NULL;

	error_code = slurm_parser(in_line,
				  "NumPartitions=", 'd', &num_partitions,
				  "PartitionSizes=", 's', &part_sizes,
				  "PartitionTypes=", 's', &part_types,
				  "NodeIDs=", 's', &node_ids,
				  "PortConfig=", 's', &port_confs,
				  "END");

	if (error_code || num_partitions<1 || !part_sizes || !part_types ||
	    !node_ids || !port_confs){
		error_code = SLURM_ERROR;
		goto cleanup;
	}

	new_conf_data(&conf_data, num_partitions);
	
	_tokenize_int(part_sizes, num_partitions,
		      delimiter, conf_data->partition_sizes);
	_tokenize_int(part_types, num_partitions,
		      delimiter, (int*) conf_data->partition_type);
	_tokenize_node_ids(node_ids, num_partitions, conf_data->partition_sizes,
			   delimiter, separator, conf_data->node_id);

	new_conf_result(&conf_result, conf_data);

	_tokenize_port_conf_list(port_confs, delimiter, separator,
				 conf_result->port_conf_list);

	list_push(config_result_list, conf_result);

  cleanup:
	xfree(part_sizes);
	xfree(part_types);
	xfree(node_ids);
	xfree(port_confs);
	return error_code;
}

/* */
int _tokenize_int(char* source, int size, char* delimiter, int* int_array)
{
	char *stuff, *cpy, *next_ptr;
	int i;

	cpy = source;
	stuff = strtok_r(cpy, delimiter, &next_ptr);
	for (i = 0; i<size; i++){
		int_array[i] = atoi(stuff);
		cpy = next_ptr;
		stuff = strtok_r(cpy, delimiter, &next_ptr);
	}
	return SLURM_SUCCESS;
}

/* */
int _tokenize_node_ids(char* source, int size, int* partition_sizes, 
		       char *delimiter, char *separator, int** node_id)
{
	char *stuff, *cpy, *next_ptr;
	int i;

	cpy = source;
	stuff = strtok_r(cpy, delimiter, &next_ptr);
	for (i = 0; i<size; i++){
		// int_array[i] = stuff;
		node_id[i] = (int*) xmalloc(sizeof(int)*partition_sizes[i]);
		if (_tokenize_int(stuff, partition_sizes[i], separator, node_id[i])){
			return SLURM_ERROR;
		}
		cpy = next_ptr;
		stuff = strtok_r(cpy, delimiter, &next_ptr);
	}
	return SLURM_SUCCESS;
}

int _tokenize_port_conf_list(char* source, char* delimiter, 
			     char* separator, List port_conf_list)
{
	char *stuff, *cpy, *next_ptr;

	cpy = source;
	while((stuff = strtok_r(cpy, delimiter, &next_ptr))){
		_tokenize_port_conf(stuff, separator, port_conf_list);

		// int_array[i] = atoi(stuff);
		cpy = next_ptr;
		// stuff = strtok_r(cpy, delimiter, &next_ptr);
	}

	return SLURM_SUCCESS;
}

int _tokenize_port_conf(char* source, char* delimiter,
			List port_conf_list)
{
	char *stuff, *cpy, *next_ptr;
	int i;
	int plus_ports[PA_SYSTEM_DIMENSIONS];
	int minus_ports[PA_SYSTEM_DIMENSIONS];
	port_conf_t* port_conf;
	
	cpy = source;
	stuff = strtok_r(cpy, delimiter, &next_ptr);
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		plus_ports[i] = stuff[i]-'0';
	}

	cpy = next_ptr;
	stuff = strtok_r(cpy, delimiter, &next_ptr);
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		minus_ports[i] = stuff[i]-'0';
	}
	new_port_conf(&port_conf, plus_ports, minus_ports);
	list_append(port_conf_list, port_conf);
	return SLURM_SUCCESS;
}

/** 
 * get the partition configuration for the given port configuration
 */
int _get_part_config(int num_nodes, List switch_config_list, List config_result_list)
{
	int rc = 1;
	if (gs_init(switch_config_list, num_nodes)){
		printf("error initializing system\n");
		goto cleanup;
	}
	/* first we find the partition configurations for the separate
	 * dimensions
	 */
	if (find_all_tori(config_result_list)){
		printf("error finding all tori\n");
		goto cleanup;
	}
	rc = 0;

 cleanup:
	gs_fini();
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
	} /* Y dimension for loop */
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

		return 0;
	} else {
		list_destroy(*results);
#ifdef DEBUG_PA
		printf("_find_first_match: error, couldn't "
		       "find match for request %d%d%d\n",
		       geometry[X], geometry[Y], geometry[Z]);
#endif
		return 1;
	}
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

/** */
void _reset_pa_system()
{
	pa_system_t* pa_system = NULL;
	while((pa_system = (pa_system_t*) list_pop(_pa_system_list))){
		;
	}
	
	/* after that's done, we should be left with the top
	 * of the stack which was the original pa_system */
	// 999
	// _pa_system = *pa_system;
	set_ptr(&_pa_system, pa_system);
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
 * IN - conn_type: connection type of request (RM_TORUS or RM_MESH)
 * 
 * return SUCCESS of operation.
 */
int new_pa_request(pa_request_t** pa_request, 
		   int geometry[PA_SYSTEM_DIMENSIONS], int size, 
		   bool rotate, bool elongate, 
		   bool force_contig, int conn_type)
{
	int i, sz=1;

	*pa_request = (pa_request_t*) xmalloc(sizeof(pa_request_t));
	(*pa_request)->geometry = (int*) xmalloc(sizeof(int)* PA_SYSTEM_DIMENSIONS);
	/* size will be overided by geometry size if given */
	if (geometry[0] != -1){
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
			printf("new_pa_request ERROR, requested size must be greater than "
			       "0 and a power of 2 (of course, size 1 is allowed)\n");
			return 1;
		}

		if (size == 1){
			for (i=0; i<PA_SYSTEM_DIMENSIONS; i++)
				(*pa_request)->geometry[i] = 1;
		} else {
			int literal = size / pow(2,(PA_SYSTEM_DIMENSIONS-1));
			for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
				(*pa_request)->geometry[i] = literal;
			printf("parsed geometry: %d\n", (*pa_request)->geometry[i]);}
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
	
#ifdef DEBUG_PA
	printf("pa_init()\n");
#endif
	/* if we've initialized, just pop off all the old crusty
	 * pa_systems */
	if (_initialized){
		_reset_pa_system();
		return;
	}
	
	char** filenames = (char**)xmalloc(sizeof(char*) * PA_SYSTEM_DIMENSIONS);
	_conf_result_list = (List*) xmalloc(sizeof(List) * PA_SYSTEM_DIMENSIONS);
	
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		_conf_result_list[i] = list_create(delete_conf_result);
	}

	/* see if we can load in the filenames from the env */

	filenames[X] = "Y_dim_torus.conf";
	filenames[Y] = "Y_dim_torus.conf";
	filenames[Z] = "Z_dim_torus.conf";

	// 999
	// filenames[X] = getenv("X_DIM_CONF");
	// filenames[Y] = getenv("Y_DIM_CONF");
	// filenames[Z] = getenv("Z_DIM_CONF");

	/* create the X configuration (8 nodes) */
	if (filenames[X]){
		time_t start, end;
		time(&start);
		_load_part_config(filenames[X], _conf_result_list[X]);
		time(&end);
		printf("loading file time: %ld\n", (end-start));
	} else {
		switch_config_list = list_create(delete_gen);
		create_config_8_1d(switch_config_list);
		if (_get_part_config(8, switch_config_list, _conf_result_list[X])){
			printf("Error getting configuration\n");
			exit(0);
		}
		list_destroy(switch_config_list);
	}

	/* create the Y configuration (4 nodes) */
	if (filenames[Y]){
		_load_part_config(filenames[Y], _conf_result_list[Y]);
	} else {
		switch_config_list = list_create(delete_gen);
		create_config_4_1d(switch_config_list);
		if (_get_part_config(4, switch_config_list, _conf_result_list[Y])){
			printf("Error getting configuration\n");
			exit(0);
		}
		list_destroy(switch_config_list);
	}

	/* create the Z configuration (4 nodes) */
	if (filenames[Z]){
		_load_part_config(filenames[Z], _conf_result_list[Z]);
	} else {
		switch_config_list = list_create(delete_gen);
		create_config_4_1d(switch_config_list);
		if (_get_part_config(4, switch_config_list, _conf_result_list[Z])){
			printf("Error getting configuration\n");
			exit(0);
		}
		list_destroy(switch_config_list);
	}

	xfree(filenames);

	_create_pa_system(&_pa_system, _conf_result_list);
	_pa_system_list = list_create(_delete_pa_system);
	// _pa_system_list = list_create(_delete_pa_system_ptr);
	_initialized = true;
	
	// whenever we make a change, we do this: 
}

/** 
 * destroy all the internal (global) data structs.
 */
void pa_fini()
{
	int i;

#ifdef DEBUG_PA
	printf("pa_fini()\n");
#endif

	if (!_initialized){
		return;
	}

	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++) {
		list_destroy(_conf_result_list[i]);
	}
	xfree(_conf_result_list);

	list_destroy(_pa_system_list);
	_delete_pa_system(_pa_system);

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

	/* first we make a copy of the current system */
	// 999
	// _backup_pa_system();

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

	// 999
	// _backup_pa_system();
	if (!_find_first_match(pa_request, results))
		return 1;
	else 
		return 0;
}

/** 
 * Doh!  Admin made a boo boo.  Note: Undo only has one history
 * element, so two consecutive undo's will fail.
 *
 * returns SLURM_SUCCESS if undo was successful.
 */
int undo_last_allocatation()
{
	pa_system_t* pa_system;
	pa_system = (pa_system_t*) list_pop(_pa_system_list);
	if (pa_system == NULL){
		return SLURM_ERROR;
	} else {
		_delete_pa_system(_pa_system);
		// 999 
		// _pa_system = *pa_system;
		set_ptr(&_pa_system, pa_system);
	}
	return SLURM_SUCCESS;
}

/** 
 * get the port configuration for the nodes in the partition
 * allocation result
 *
 *
 * IN: pa_node list from result of allocate_part
 * OUT/return: char* to be appended to output of each partition in the
 * bluegene.conf file
 * 
 * NOTE, memory for returned string must be xfree'd by caller
 */
char* get_conf_result_str(List pa_node_list)
{
	char* result_str;
	ListIterator pan_itr, cf_itr, pc_itr;
	pa_node_t* pa_node;
	conf_result_t* conf_result;
	port_conf_t* port_conf;

	result_str = xstrdup("NodeIDs=");

	pan_itr = list_iterator_create(pa_node_list);
	pa_node = (pa_node_t*) list_next(pan_itr);
	while(pa_node != NULL){
		_xstrfmtcat(&result_str, "%d%d%d",
			    pa_node->coord[0], 
			    pa_node->coord[1], 
			    pa_node->coord[2]);

		pa_node = (pa_node_t*) list_next(pan_itr);
		if(pa_node != NULL){
			_xstrcat(&result_str, ",");			
		}
	}


	_xstrcat(&result_str, " PortConfigs=");
	list_iterator_reset(pan_itr);
	while((pa_node = (pa_node_t*) list_next(pan_itr))){
		int i;
		
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			cf_itr = list_iterator_create(pa_node->conf_result_list[i]);
			// while((conf_result = (conf_result_t*) list_next(cf_itr))){
			conf_result = (conf_result_t*) list_next(cf_itr);
			pc_itr = list_iterator_create(conf_result->port_conf_list);
			port_conf = (port_conf_t*) list_next(pc_itr);
			while(port_conf != NULL){
				
				_xstrfmtcat(&result_str, "%d%d%d/%d%d%d",
					    port_conf->plus_ports[0],
					    port_conf->plus_ports[1],
					    port_conf->plus_ports[2],
					    port_conf->minus_ports[0],
					    port_conf->minus_ports[1],
					    port_conf->minus_ports[2]);
				
				port_conf = (port_conf_t*) list_next(pc_itr);
				if(port_conf != NULL){
						_xstrcat(&result_str, ",");
				}
			}
			list_iterator_destroy(cf_itr);
		}
	}
	list_iterator_destroy(pan_itr);
	
	return result_str;
}

/** */
int main(int argc, char** argv)
{
	int geo[3];
	bool rotate = false;
	bool elongate = false;
	bool force_contig = true;
	List results;
	pa_request_t* request; 
	time_t start, end;

	if (argc == 4){
		int i;
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			geo[i] = atoi(argv[i+1]);
		}
		printf("allocating by geometry: %d %d %d\n", geo[0], geo[1], geo[2]);
		new_pa_request(&request, geo, -1, rotate, elongate, force_contig, RM_TORUS);
	} else if (argc == 2) {
		int size;
		size = atoi(argv[1]);
		geo[0] = -1;
		printf("allocating by size: %d\n", size);
		new_pa_request(&request, geo, size, rotate, elongate, force_contig, RM_TORUS);
	} else {
		printf(" usage: partition_allocator dimX dimY dimZ\n");
		printf("    or: partition_allocator size\n");
		printf(" tries to allocate the given geometry request \n");
		exit(0);
	}

	
	time(&start);
	pa_init();
	time(&end);
	printf("init: %ld\n", (end-start));
	/*
	  int dead_node1[3] = {0,0,0};
	  int dead_node2[3] = {1,0,0};
	  set_node_down(dead_node1);
	  set_node_down(dead_node2);
	  printf("done setting node down\n");
	*/
	
	time(&start);
	if (allocate_part(request, &results)){
		ListIterator itr;

		printf("allocation succeeded\n");
		       
		itr = list_iterator_create(results);
		printf("results: \n");
		/*
		pa_node_t* result;
		while((result = (pa_node_t*) list_next(itr))){
			printf("%d%d%d ", 
			       result->coord[0], 
			       result->coord[1],
			       result->coord[2]);
		}
		printf("\n");
		*/
		char* result = get_conf_result_str(results);
		printf("results: %s\n", result);
		xfree(result);
		list_destroy(results);
		// _print_results(results);
	} else {
		printf("request failed\n");
	}
	time(&end);
	printf("allocate: %ld\n", (end-start));


	time(&start);
	pa_fini();
	time(&end);
	printf("fini: %ld\n", (end-start));

	delete_pa_request(request);

	return 0;
}
