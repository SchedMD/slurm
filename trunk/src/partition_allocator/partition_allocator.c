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
List _conf_result_list[PA_SYSTEM_DIMENSIONS];
bool _initialized = false;


/* _pa_system is the "current" system that the structures will work
 *  on */
pa_system_t * _pa_system_ptr;
List _pa_system_list;
List path;
List best_path;

/** internal helper functions */
/** */
void _new_pa_node(pa_node_t *pa_node, int coordinates[PA_SYSTEM_DIMENSIONS]);
/** */
void _print_pa_node();
/** */
void _delete_pa_node(pa_node_t* pa_node);
/** */
int _listfindf_pa_node(pa_node_t* A, pa_node_t* B);
/** */
bool _is_down_node(pa_node_t* pa_node);
/* */
void _create_pa_system(pa_system_t* pa_system, List conf_result_list[PA_SYSTEM_DIMENSIONS]);
/* */
void _print_pa_system(pa_system_t *pa_system);
/* */
void _delete_pa_system(void* object);
/* */
void _copy_pa_system(pa_system_t *pa_system_target, pa_system_t* pa_system_source);
/* */
void _backup_pa_system();
/** load the partition configuration from file */
int _load_part_config(char* filename, List part_config_list);
/* run the graph solver to get the conf_result(s) */
int _get_part_config(int num_nodes, List port_config_list, List conf_result_list);
/* find the first partition match in the system */
int _check_for_options(pa_request_t* pa_request); 
int _find_match(pa_request_t* pa_request, List* results);
bool _find_first_match_aux(pa_request_t* pa_request, int dim2check, int const_dim, 
			   int dimA, int dimB, List* results);
/* check to see if the conf_result matches */
/* bool _node_used(pa_node_t* pa_node, int geometry,  */
/*  		    int conn_type, bool force_contig, */
/* 		    dimension_t dim, int cur_node_id); */
bool _node_used(pa_node_t* pa_node);
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
void _new_pa_node(pa_node_t *pa_node, int coord[PA_SYSTEM_DIMENSIONS])
{
	int i;
	pa_node->used = false;

	// pa_node = (pa_node_t*) xmalloc(sizeof(pa_node_t));
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
			pa_node->conf_result_list[i] = NULL;	
		}
	}

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
void _create_pa_system(pa_system_t* target_pa_system, List source_list[PA_SYSTEM_DIMENSIONS])
{
	int i, x, y, z;
	
	target_pa_system->grid = (pa_node_t***) xmalloc(sizeof(pa_node_t**) * DIM_SIZE[X]);
	for (x=0; x<DIM_SIZE[X]; x++) {
		target_pa_system->grid[x] = (pa_node_t**) xmalloc(sizeof(pa_node_t*) * DIM_SIZE[Y]);
		for (y=0; y<DIM_SIZE[Y]; y++) {
			target_pa_system->grid[x][y] = (pa_node_t*) xmalloc(sizeof(pa_node_t) * DIM_SIZE[Z]);
			
			for (z=0; z<DIM_SIZE[Z]; z++){
				int coord[PA_SYSTEM_DIMENSIONS] = {x,y,z};
				_new_pa_node(&target_pa_system->grid[x][y][z], coord);

				for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
					list_copy(source_list[i], &target_pa_system->grid[x][y][z].conf_result_list[i]);
				}
			}
		}
	}
}

/** */
void _print_pa_system(pa_system_t *pa_system)
{
	int x=0,y=0,z=0;
	printf("pa_system: %d%d%d\n", DIM_SIZE[X], DIM_SIZE[Y], DIM_SIZE[Z]);
	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++){
			for (z=0; z<DIM_SIZE[Z]; z++){
				printf(" pa_node %d%d%d 0x%p: \n", x, y, z,&pa_system->grid[x][y][z]);
				_print_pa_node(&pa_system->grid[x][y][z]);
			}
		}
	}
}

/** */
void _delete_pa_system(void* object)
{
	int x, y;
	pa_system_t *pa_system = (pa_system_t *) object;
	
	if (!pa_system){
		return;
	}
	
	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++)
			xfree(pa_system->grid[x][y]);
		xfree(pa_system->grid[x]);
	}
	xfree(pa_system->grid);
	xfree(pa_system);
}

/* */
void _copy_pa_system(pa_system_t* target_pa_system, pa_system_t *source_pa_system)
{
	int i, x, y, z;
	
	target_pa_system = xmalloc(sizeof(pa_system_t));

	target_pa_system->grid = (pa_node_t***) xmalloc(sizeof(pa_node_t**) * DIM_SIZE[X]);
	for (x=0; x<DIM_SIZE[X]; x++) {
		target_pa_system->grid[x] = (pa_node_t**) xmalloc(sizeof(pa_node_t*) * DIM_SIZE[Y]);
		for (y=0; y<DIM_SIZE[Y]; y++) {
			target_pa_system->grid[x][y] = (pa_node_t*) xmalloc(sizeof(pa_node_t) * DIM_SIZE[Z]);
			
			for (z=0; z<DIM_SIZE[Z]; z++){
				int coord[PA_SYSTEM_DIMENSIONS] = {x,y,z};
				_new_pa_node(&target_pa_system->grid[x][y][z], coord);

       				for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
					if (source_pa_system->grid[x][y][z].conf_result_list[i] != NULL)
						list_copy(source_pa_system->grid[x][y][z].conf_result_list[i], &target_pa_system->grid[x][y][z].conf_result_list[i]);
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
	_copy_pa_system(new_system, _pa_system_ptr);
	list_push(_pa_system_list, &_pa_system_ptr);
	set_ptr(_pa_system_ptr, new_system);
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
	/* if (find_all_tori(config_result_list)){ */
/* 		printf("error finding all tori\n"); */
/* 		goto cleanup; */
/* 	} */
	rc = 0;

 cleanup:
	//gs_fini();
	return rc;
}

int _check_for_options(pa_request_t* pa_request) 
{
	int temp;
	if(pa_request->rotate) {
		//printf("Rotating! %d%d%d \n",pa_request->geometry[X],pa_request->geometry[Y],pa_request->geometry[Z]);
		if (pa_request->rotate_count==(PA_SYSTEM_DIMENSIONS-1)) {
			//printf("Special!\n");
			temp=pa_request->geometry[X];
			pa_request->geometry[X]=pa_request->geometry[Z];
			pa_request->geometry[Z]=temp;
			pa_request->rotate_count++;
			return 1;
		
		} else if(pa_request->rotate_count<(PA_SYSTEM_DIMENSIONS*2)) {
			temp=pa_request->geometry[X];
			pa_request->geometry[X]=pa_request->geometry[Y];
			pa_request->geometry[Y]=pa_request->geometry[Z];
			pa_request->geometry[Z]=temp;
			pa_request->rotate_count++;
			return 1;
		} else 
			pa_request->rotate = false;
		       
	}
	if(pa_request->elongate && pa_request->elongate_count<PA_SYSTEM_DIMENSIONS) {
		pa_request->elongate_count++;
		printf("Elongating! not working yet\n");
		return 0;
	}
	return 0;
}

/** 
 * greedy algorithm for finding first match
 */
int _find_match(pa_request_t* pa_request, List* results)
{
	int x=0, y=0, z=0;
	int *geometry = pa_request->geometry;
	int start_x=0, start_y=0, start_z=0;
	int find_x=0, find_y=0, find_z=0;
	pa_node_t* pa_node;
	int found_one=0;

start_again:
	//printf("starting looking for a grid of %d%d%d\n",pa_request->geometry[X],pa_request->geometry[Y],pa_request->geometry[Z]);
	for (z=0; z<geometry[Z]; z++) {
		for (y=0; y<geometry[Y]; y++) {			
			for (x=0; x<geometry[X]; x++) {
				pa_node = &_pa_system_ptr->grid[find_x][find_y][find_z];
				if (!_node_used(pa_node)) {
					//	cont_x++;			
					
					//printf("Yeap, I found one at %d%d%d\n", find_x, find_y, find_z);
					_insert_result(*results, pa_node);
					find_x++;
					found_one=1;
				} else {
					//printf("hey I am used! %d%d%d\n", find_x, find_y, find_z);
					if(found_one) {
						list_destroy(*results);
						*results = list_create(NULL);
						found_one=0;
					}
					if((DIM_SIZE[X]-find_x-1)>=geometry[X]) {
						find_x++;
						start_x=find_x;
					} else {
						find_x=0;
						start_x=find_x;
						if(find_z<(DIM_SIZE[Z]-1)) {
							//find_y=0;
							//printf("incrementing find_Z from %d to %d\n",find_z,find_z+1);
							find_z++;
							start_z=find_z;
						} else if (find_y<(DIM_SIZE[Y]-1)) {
							find_z=0;
							start_z=find_z;
							//printf("incrementing find_y from %d to %d\n",find_y,find_y+1);
							find_y++;
							//printf("setting start_y = %d\n",find_y);
							start_y=find_y;
						} else {
							//printf("couldn't find it\n");
							if(!_check_for_options(pa_request))
								return 0;
							else {
								find_x=0;
								find_y=0;
								find_z=0;
								start_x=0;
								start_y=0;
								start_z=0;
								goto start_again;
							}
						}
						//printf("x= %d, y= %d, z= %d\n", x, y, z);
					}
					goto start_again;
				}		
			}
			//printf("looking at %d%d%d\n",x,y,z);
			find_x=start_x;
			if(y<(geometry[Y]-1)) {
				if(find_y<(DIM_SIZE[Y]-1)) {
					find_y++;
				} else {
					if(!_check_for_options(pa_request))
						return 0;
					else {
						find_x=0;
						find_y=0;
						find_z=0;
						start_x=0;
						start_y=0;
						start_z=0;
						goto start_again;
					}
				}
			}
		//start_y=find_y;
		}
		find_y=start_y;
		if(z<(geometry[Z]-1)) {
			if(find_z<(DIM_SIZE[Z]-1)) {
				find_z++;
			} else {
				if(!_check_for_options(pa_request))
					return 0;
				else {
					find_x=0;
					find_y=0;
					find_z=0;
					start_x=0;
					start_y=0;
					start_z=0;
					goto start_again;
				}
			}
		}
		//start_z=find_z;
	}

	if(found_one) {
		/** THIS IS where we might should call the graph
		 * solver to see if the allocation can be wired,
		 * before returning a definitive TRUE */
		_set_internal_wires(results, pa_request->size);
	} else {
		printf("couldn't find it 2\n");
		return 0;
	}
	return 1;
}

/**
 * auxilliary recursive call.
 * 
 * 
 */
bool _find_first_match_aux(pa_request_t* pa_request, 
			   int dim2check, int var_dim,
			   int dimA, int dimB, List* results)
{
	int i=0;
	int found_count[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	bool match_found, request_filled = false;
	int* geometry = pa_request->geometry;
	int a, b, c;

	/** we want to go up the Y dim, but checking for correct X size*/
	for (i=0; i<DIM_SIZE[var_dim]; i++){

		pa_node_t* pa_node;
		if (var_dim == X){
			printf("_find_first_match_aux: aaah, this should never happen\n");
			return false;
			
		} else if (var_dim == Y){
			a = dimA;
			b = i;
			c = dimB;
		} else {
			a = dimA;
			b = dimB;
			c = i;
		}

		// printf("_find_first_match_aux pa_node %d%d%d(%s) dim2check %s\n",
		// pa_node->coord[X], pa_node->coord[Y], pa_node->coord[Z], 
		// convert_dim(var_dim), convert_dim(dim2check));
		pa_node = &_pa_system_ptr->grid[a][b][c];
		if (found_count[dim2check] != geometry[dim2check]){
			match_found = _node_used(pa_node);
			if (!match_found){
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
					
					// found_count[dim2check]++;
					found_count[var_dim]++;
#ifdef DEBUG_PA
					if (var_dim == Z && match_found){
						printf("var_dim %d dim2check %d\n", var_dim, dim2check);
						printf("match found for Z\n");
						printf("found_count %d\n", found_count[var_dim]);
					}
					printf("dim %d <found %d geometry %d>\n", var_dim, found_count[var_dim], geometry[var_dim]);
#endif
					if (found_count[var_dim] == geometry[var_dim]){
					// if (found_count[dim2check] == geometry[dim2check]){
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

/* bool _node_used(pa_node_t* pa_node, int geometry,  */
/*  		    int conn_type, bool force_contig, */
/* 		    dimension_t dim, int cur_node_id) */
bool _node_used(pa_node_t* pa_node)
{
/* 	ListIterator itr; */
/* 	conf_result_t* conf_result; */
/* 	int i=0, j = 0; */

	/* printf("_node_used: node to check against %s %d\n", convert_dim(dim), cur_node_id); */
	/* if we've used this node in another partition already */
	if (_is_down_node(pa_node) || pa_node->used)
		return true;
	else
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
	int a, b, c;
	for (cur_dim=0; cur_dim<PA_SYSTEM_DIMENSIONS; cur_dim++){
		for(i=0; i<DIM_SIZE[cur_dim]; i++){
			if (cur_dim == X){
				a = i;
				b = y;
				c = z;
			} else if (cur_dim == Y) {
				a = x;
				b = i;
				c = z;
			} else {
				a = x;
				b = y;
				c = i;
			}
				
			pa_node = &_pa_system_ptr->grid[a][b][c];
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
	// _pa_system = *pa_system;
	set_ptr(_pa_system_ptr, pa_system);
}

int _find_smallest_dim(int size, int dim) 
{
	int smaller=0;
	if(size%2) {
		
	}
	return smaller;
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
 * IN - conn_type: connection type of request (SELECT_TORUS or SELECT_MESH)
 * 
 * return SUCCESS of operation.
 */
int new_pa_request(pa_request_t* pa_request, 
		   int geometry[PA_SYSTEM_DIMENSIONS], int size, 
		   bool rotate, bool elongate, 
		   bool force_contig, int conn_type)
{
	int i, i2, i3, picked, total_sz=1 , size2, size3;
	float sz=1;
	int checked[7];
	
	/* size will be overided by geometry size if given */
	if (geometry[0] != -1){
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			if (geometry[i] < 1 || geometry[i] > DIM_SIZE[i]){
				printf("new_pa_request Error, request geometry is invalid\n"); 
				return 0;
			}
		}

		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			pa_request->geometry[i] = geometry[i];
		}

	} else {
		/* decompose the size into a cubic geometry */
		
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++) { 
			total_sz *= DIM_SIZE[i];
			pa_request->geometry[i] = 1;
		}
	
		if(size==1)
			goto endit;
		
		if(size>total_sz || size<1) {
			printf("new_pa_request ERROR, requested size must be\ngreater than 0 and less than %d.\n",total_sz);
			return 0;			
		}
 		
	startagain:		
		picked=0;
		for(i=0;i<7;i++)
			checked[i]=0;
		
		for(i=0;i<PA_SYSTEM_DIMENSIONS-1;i++) {
			sz = powf((float)size,(float)1/(PA_SYSTEM_DIMENSIONS-i));
			if(pow(sz,(PA_SYSTEM_DIMENSIONS-i))==size)
				break;
		}
		size3=size;
		if(i<PA_SYSTEM_DIMENSIONS-1) {
			i3=i;
			/* 			we found something that looks like a cube! */
			for (i=0; i<PA_SYSTEM_DIMENSIONS-i3; i++) {  
				if(sz<=DIM_SIZE[i]) {
					pa_request->geometry[i] = sz;
					size3 /= sz;
					picked++;
				} else 
					goto tryagain;
			}		
		} else {
			picked=0;
		tryagain:	
			if(size3!=size)
				size2=size3;
			else
				size2=size;
			//messedup:
			for (i=picked; i<PA_SYSTEM_DIMENSIONS; i++) { 
				if(size2<=1) 
					break;
				
				sz = size2%DIM_SIZE[i];
				if(!sz) {
					pa_request->geometry[i] = DIM_SIZE[i];	
					size2 /= DIM_SIZE[i];
				} else if (size2 > DIM_SIZE[i]){
					for(i2=(DIM_SIZE[i]-1);i2>1;i2--) {
						if (!(size2%i2) && !checked[i2]) {
							size2 /= i2;
							
							if(i==0)
								checked[i2]=1;
							
							if(i2<DIM_SIZE[i]) 
								pa_request->geometry[i] = i2;
							else 
								goto tryagain;
							if((i2-1)!=1 && i!=(PA_SYSTEM_DIMENSIONS-1))
							   break;
						}		
					}				
					if(i2==1) {
						size +=1;
						goto startagain;
					}
						
				} else {
					pa_request->geometry[i] = sz;	
					break;
				}					
			}
		}
	}
			
	sz=1;
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++)
		sz *= pa_request->geometry[i];
endit:
	pa_request->size = sz;
	
	printf("geometry: %d %d %d size = %d\n", pa_request->geometry[0],pa_request->geometry[1],pa_request->geometry[2], pa_request->size);
		
	pa_request->conn_type = conn_type;
	pa_request->rotate_count= 0;
	pa_request->rotate = rotate;
	pa_request->elongate_count = 0;
	pa_request->elongate = elongate;
	pa_request->force_contig = force_contig;
	
	return 1;
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
void print_pa_request(pa_request_t* pa_request)
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
//	List switch_config_list;
#ifdef DEBUG_PA
	printf("pa_init()\n");
#endif
	/* if we've initialized, just pop off all the old crusty
	 * pa_systems */
	if (_initialized){
		_reset_pa_system();
		return;
	}
	
//	char* filenames[3];
	
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		_conf_result_list[i] = list_create(delete_conf_result);
	}

	/* see if we can load in the filenames from the env */

/* 	filenames[X] = "X_alt_dim_torus.conf"; */
/* 	filenames[Y] = "Y_dim_torus.conf"; */
/* 	filenames[Z] = "Z_dim_torus.conf"; */

	// filenames[X] = getenv("X_DIM_CONF");
	// filenames[Y] = getenv("Y_DIM_CONF");
	// filenames[Z] = getenv("Z_DIM_CONF");

	/* create the X configuration (8 nodes) */
/* 	if (filenames[X]){ */
/* 		time_t start, end; */
/* 		time(&start); */
/* 		_load_part_config(filenames[X], _conf_result_list[X]); */
/* 		time(&end); */
/* 		printf("loading file time: %ld\n", (end-start)); */
/* 	} else { */
	
/* 		switch_config_list = list_create(delete_gen); */
/* 		create_config_8_1d(switch_config_list); */
/* 		if (_get_part_config(8, switch_config_list, _conf_result_list[X])){ */
/* 			printf("Error getting configuration\n"); */
/* 			exit(0); */
/* 		} */
/* 		list_destroy(switch_config_list); */
/* 	} */

/* 	/\* create the Y configuration (4 nodes) *\/ */
/* 	if (filenames[Y]){ */
/* 		_load_part_config(filenames[Y], _conf_result_list[Y]); */
/* 	} else { */
/* 		switch_config_list = list_create(delete_gen); */
/* 		create_config_4_1d(switch_config_list); */
/* 		if (_get_part_config(4, switch_config_list, _conf_result_list[Y])){ */
/* 			printf("Error getting configuration\n"); */
/* 			exit(0); */
/* 		} */
/* 		list_destroy(switch_config_list); */
/* 	} */

/* 	/\* create the Z configuration (4 nodes) *\/ */
/* 	if (filenames[Z]){ */
/* 		_load_part_config(filenames[Z], _conf_result_list[Z]); */
/* 	} else { */
/* 		switch_config_list = list_create(delete_gen); */
/* 		create_config_4_1d(switch_config_list); */
/* 		if (_get_part_config(4, switch_config_list, _conf_result_list[Z])){ */
/* 			printf("Error getting configuration\n"); */
/* 			exit(0); */
/* 		} */
/* 		list_destroy(switch_config_list); */
/* 	} */

	_pa_system_ptr = xmalloc(sizeof(pa_system_t));
		
	_create_pa_system(_pa_system_ptr, _conf_result_list);
	_create_config_even(_pa_system_ptr->grid);
	path = list_create(NULL);
	best_path = list_create(NULL);
	_pa_system_list = list_create(_delete_pa_system);
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

	list_destroy(path);
	list_destroy(best_path);
	list_destroy(_pa_system_list);
	_delete_pa_system(_pa_system_ptr);

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
	// _backup_pa_system();

	/* basically set the node as NULL */
	_pa_system_ptr->grid[c[0]][c[1]][c[2]].used = true;
	// _delete_pa_node(&_pa_system_ptr->grid[c[0]][c[1]][c[2]]);
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
int allocate_part(pa_request_t* pa_request)
{
	List results;

	if (!_initialized){
		printf("allocate_part Error, configuration not initialized, call init_configuration first\n");
		return 0;
	}

	if (!pa_request){
		printf("allocate_part Error, request not initialized\n");
		return 0;
	}
	results = list_create(NULL);
	
#ifdef DEBUG_PA
	print_pa_request(pa_request);
#endif

	// _backup_pa_system();
	if (_find_match(pa_request, &results)){
		//printf("hey I am returning 1\n");
		list_destroy(results);
		return 1;
	} else {
		//printf("hey I am returning 0\n");
		list_destroy(results);
		return 0;
	}
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
		_delete_pa_system(_pa_system_ptr);
		// _pa_system = *pa_system;
		set_ptr(_pa_system_ptr, pa_system);
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
	printf("Str result = %s",result_str);
	return result_str;
}

/** */
int _create_config_even(pa_node_t ***grid)
{
	int x, y ,z;
	pa_node_t *source, *target_1, *target_2, *target_first, *target_second;
	for(x=0;x<DIM_SIZE[X];x++) {
		for(y=0;y<DIM_SIZE[Y];y++) {
			for(z=0;z<DIM_SIZE[Z];z++) {
				source = &grid[x][y][z];
				
				if(x<(DIM_SIZE[X]-1))
					target_1 = &grid[x+1][y][z];
				else
					target_1 = NULL;
				if(x<(DIM_SIZE[X]-2))
					target_2 = &grid[x+2][y][z];
				else
					target_2 = NULL;
				target_first = &grid[0][y][z];
				target_second = &grid[1][y][z];
				_set_external_wires(X, x, source, target_1, target_2, target_first, target_second);
				
				if(y<(DIM_SIZE[Y]-1))
					target_1 = &grid[x][y+1][z];
				else
					target_1 = NULL;
				if(y<(DIM_SIZE[Y]-2))
					target_2 = &grid[x][y+2][z];
				else
					target_2 = NULL;
				target_first = &grid[x][0][z];
				target_second = &grid[x][1][z];
				_set_external_wires(Y, y, source, target_1, target_2, target_first, target_second);

				if(z<(DIM_SIZE[Z]-1))
					target_1 = &grid[x][y][z+1];
				else
					target_1 = NULL;
				if(z<(DIM_SIZE[Z]-2))
					target_2 = &grid[x][y][z+2];
				else
					target_2 = NULL;
				target_first = &grid[x][y][0];
				target_second = &grid[x][y][1];
				_set_external_wires(Z, z, source, target_1, target_2, target_first, target_second);
			}
		}
	}
	return 1;
}
void _switch_config(pa_node_t* source, pa_node_t* target, int dim, int port_src, int port_tar)
{
	pa_switch_t* config = &source->axis_switch[dim];
	pa_switch_t* config_tar = &target->axis_switch[dim];
	int i;
	for(i=0;i<PA_SYSTEM_DIMENSIONS;i++) {
		/* Set the coord of the source target node on the internal to itself, */
		/* and the extrenal to the target */
		config->int_wire[port_src].node_tar[i] = source->coord[i];
		config->ext_wire[port_src].node_tar[i] = target->coord[i];
	
		/* Set the coord of the target back to the source */
		config_tar->ext_wire[port_tar].node_tar[i] = source->coord[i];
	}

	/* Set the port of the source target node on the internal to itself, */
	/* and the extrenal to the target */
	config->int_wire[port_src].port_tar = port_src;
	config->ext_wire[port_src].port_tar = port_tar;
	
	/* Set the port of the target back to the source */
	config_tar->ext_wire[port_tar].port_tar = port_src;
}

void _set_external_wires(int dim, int count, pa_node_t* source, pa_node_t* target_1, pa_node_t* target_2, pa_node_t* target_first, pa_node_t* target_second)
{
	_switch_config(source, source, dim, 0, 0);
	_switch_config(source, source, dim, 1, 1);
	if(count==0) {
		/* First Node */
		/* 4->3 of next */
		_switch_config(source, target_1, dim, 4, 3);
		/* 2->5 of next */
		_switch_config(source, target_1, dim, 2, 5);
		/* 3->4 of next even */
		_switch_config(source, target_2, dim, 3, 4);
	} else if(!(count%2)) {
		if(count<DIM_SIZE[dim]-2) {
			/* Not Last Even Node */
			/* 3->4 of next even */
			_switch_config(source, target_2, dim, 3, 4);
			/* 2->5 of next */
			_switch_config(source, target_1, dim, 2, 5);
			/* 5->2 of next */
			_switch_config(source, target_1, dim, 5, 2);
		} else {
			/* Last Even Node */
			/* 3->4 of next */
			_switch_config(source, target_1, dim, 3, 4);
			/* 5->2 of next */
			_switch_config(source, target_1, dim, 5, 2);
			/* 2->5 of first */
			_switch_config(source, target_first, dim, 2, 5);
		}
	} else {
		if(count<DIM_SIZE[dim]-2) {
			/* Not Last Odd Node */
			/* 4->3 of next odd */
			_switch_config(source, target_2, dim, 4, 3);
		} else {
			/* Last Odd Node */
			/* 5->2 of second */
			_switch_config(source, target_second, dim, 5, 2);
		}	
	}	
}

int _set_internal_port(pa_switch_t *curr_switch, int check_port, int *coord, int dim) 
{
	pa_switch_t *next_switch; 
	int port_tar;
	int source_port=1;
	int target_port=0;
	int *node_tar;
	
	if(!(check_port%2)) {
		source_port=0;
		target_port=1;
	}
	
	curr_switch->int_wire[source_port].used = 1;
	curr_switch->int_wire[source_port].port_tar = check_port;
	curr_switch->int_wire[check_port].used = 1;
	curr_switch->int_wire[check_port].port_tar = source_port;
	
	node_tar = curr_switch->ext_wire[check_port].node_tar;
	port_tar = curr_switch->ext_wire[check_port].port_tar;
	next_switch = &_pa_system_ptr->grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
	
	next_switch->int_wire[port_tar].used = 1;
	next_switch->int_wire[port_tar].port_tar = port_tar;
	next_switch->int_wire[target_port].used = 1;
	next_switch->int_wire[target_port].port_tar = port_tar;
	
	return 1;
}

int _find_best_path(pa_switch_t *start, int source_port, int *target, int dim, int count) 
{
	pa_switch_t *next_switch; 
	pa_path_switch_t *path_add = (pa_path_switch_t *)xmalloc(sizeof(pa_path_switch_t));
	pa_path_switch_t *path_switch;
	int port_tar;
	int target_port=0;
	int ports_to_try[2] = {3,5};
	int *node_tar= start->ext_wire[0].node_tar;
	int *node_src = start->ext_wire[0].node_tar;
	int i;
	int used=0;
	static bool found = false;
	ListIterator itr;

	path_add->geometry[X] = node_src[X];
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
	path_add->dim = dim;
	path_add->in = source_port;
	
	if((node_tar[X]==target[X] && node_tar[Y]==target[Y] && node_tar[Z]==target[Z])) {
		found = true;
		if((ports_to_try[0]%2))
			target_port=1;
		
		path_add->out = target_port;
		list_push(path, path_add);
		
		printf("count = %d\n",count);
		list_copy(path, &best_path);
		return 1;
	} 
	
	if(source_port==0 || source_port==3 || source_port==5) {
		ports_to_try[0] = 2;
		ports_to_try[1] = 4;		
	}
	
	for(i=0;i<2;i++) {
		used=0;
		itr = list_iterator_create(path);
		while((path_switch = (pa_path_switch_t*) list_next(itr))){
//			printf("%d%d%d %d%d%d %d %d - %d\n", path_switch->geometry[X], path_switch->geometry[Y], path_switch->geometry[Z], node_src[X], node_src[Y], node_src[Z], ports_to_try[i], path_switch->in,  path_switch->out);
			if(((path_switch->geometry[X] == node_src[X]) && (path_switch->geometry[Y] == node_src[Y]) && (path_switch->geometry[Z] == node_tar[Z])) && (path_switch->in==ports_to_try[i] || path_switch->out==ports_to_try[i])) {
				used = 1;
				printf("found\n");
				break;
			}
//			printf("%d %d\n",(path_switch->geometry[X] == node_src[X] && path_switch->geometry[Y] == node_src[Y]) && path_switch->geometry[Z] == node_tar[Z]),(path_switch->in==ports_to_try[i] || path_switch->out==ports_to_try[i]));
		}
		list_iterator_destroy(itr);
		
//		if(!start->int_wire[ports_to_try[i]].used) {
		if(!used) {
			start->int_wire[source_port].used = 1;
			start->int_wire[source_port].port_tar = ports_to_try[i];
			start->int_wire[ports_to_try[i]].used = 1;
			start->int_wire[ports_to_try[i]].port_tar = source_port;
		
			port_tar = start->ext_wire[ports_to_try[i]].port_tar;
			node_tar = start->ext_wire[ports_to_try[i]].node_tar;
			//printf("%d%d%d - %d-%d -> %d%d%d - %d\n",node_src[X],node_src[Y],node_src[Z], source_port, ports_to_try[i], node_tar[X],node_tar[Y],node_tar[Z], port_tar);
			
			next_switch = &_pa_system_ptr->grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
			
			count++;
			path_add->out = ports_to_try[i];
			list_push(path, path_add);
			//printf("%d%d%d %d - %d\n", path_add->geometry[X], path_add->geometry[Y], path_add->geometry[Z], path_add->in,  path_add->out);
			_find_best_path(next_switch, port_tar, target, dim, count);
			
			if(found) {
				break;
			}
		}
		//list_pop(path);
	
	}
	return 0;
}


int _set_internal_wires(List *nodes, int size)
{
	pa_node_t* pa_node[size];
	pa_switch_t *curr_switch; 
	pa_path_switch_t *path_switch;
	//pa_switch_t *next_switch; 
	int check_port;
	int count=0;
	int coord[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	int target[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	int *start;
	int *end;
	ListIterator itr;
	
	itr = list_iterator_create(*nodes);
	
	while((pa_node[count] = (pa_node_t*) list_next(itr))){
		count++;
	}
	list_iterator_destroy(itr);
		
	start = pa_node[0]->coord;
	end = pa_node[count-1]->coord;
	printf("grid = [%d%d%d - %d%d%d]\n",start[X], start[Y], start[Z], end[X], end[Y], end[Z]);
	for(coord[X]=start[X];coord[X]<=end[X];coord[X]++) {
		for(coord[Y]=start[Y];coord[Y]<=end[Y];coord[Y]++) {
			for(coord[Z]=start[Z];coord[Z]<=end[Z];coord[Z]++) {
				if(!_pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].used) {
					
					/* set it up for the X axis */
					curr_switch = &_pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].axis_switch[X];
					if(coord[X]<(end[X]-1)) {
						/* set it up for 0 */
						if(!curr_switch->ext_wire[4].used) 
							check_port=4;
						else
							check_port=2;
						_set_internal_port(curr_switch, check_port, coord, X);
												
						/* set it up for 1 */
						if(!curr_switch->ext_wire[3].used) 
							check_port=3;
						else
							check_port=5;
						_set_internal_port(curr_switch, check_port, coord, X);
						/*****************************/

					} else if(coord[X]==(end[X]-1)) {
						//next_switch = &_pa_system_ptr->grid[coord[X]+1][coord[Y]][coord[Z]].axis_switch[X];	
						target[X]=coord[X]+1;
						target[Y]=coord[Y];
						target[Z]=coord[Z];
						if(!curr_switch->int_wire[0].used) 
							_find_best_path(curr_switch, 0, target, X, 0);
						else
							_find_best_path(curr_switch, 1, target, X, 0);	
					} 
					
					_pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].used=1;
		
				}
			}
		}
	}
	itr = list_iterator_create(best_path);
	while((path_switch = (pa_path_switch_t*) list_next(itr))){
		printf("%d%d%d %d - %d\n", path_switch->geometry[X], path_switch->geometry[Y], path_switch->geometry[Z], path_switch->in,  path_switch->out);
		curr_switch = &_pa_system_ptr->grid[path_switch->geometry[X]][path_switch->geometry[Y]][path_switch->geometry[Z]].axis_switch[path_switch->dim];
		curr_switch->int_wire[path_switch->in].used = 1;
		curr_switch->int_wire[path_switch->in].port_tar = path_switch->out;
		curr_switch->int_wire[path_switch->out].used = 1;
		curr_switch->int_wire[path_switch->out].port_tar = path_switch->in;			
	}
	list_iterator_destroy(itr);
	for(count=0;count<size;count++) {
		
		printf("Using node %d%d%d\n",pa_node[count]->coord[X], pa_node[count]->coord[Y], pa_node[count]->coord[Z]);
		printf("O set %d -> %d - %d%d%d\n",pa_node[count]->axis_switch[X].int_wire[0].port_tar, pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[0].port_tar].port_tar, pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[0].port_tar].node_tar[X],pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[0].port_tar].node_tar[Y],pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[0].port_tar].node_tar[Z]);
		printf("1 set %d -> %d - %d%d%d\n",pa_node[count]->axis_switch[X].int_wire[1].port_tar, pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[1].port_tar].port_tar, pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[1].port_tar].node_tar[X],pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[1].port_tar].node_tar[Y],pa_node[count]->axis_switch[X].ext_wire[pa_node[count]->axis_switch[X].int_wire[1].port_tar].node_tar[Z]);

	}
return 1;
}				

/** */
int main(int argc, char** argv)
{
	int geo[3] = {-1,-1,-1};
	bool rotate = true;
	bool elongate = true;
	bool force_contig = true;
	pa_request_t *request; 
	time_t start, end;
	
	request = (pa_request_t*) xmalloc(sizeof(pa_request_t));
		
	//printf("geometry: %d %d %d size = %d\n", request->geometry[0], request->geometry[1], request->geometry[2], request->size);
	time(&start);
	pa_init();
	time(&end);
	printf("init: %ld\n", (end-start));

	
	geo[0] = 3;
	geo[1] = 1;
	geo[2] = 1;	
	new_pa_request(request, geo, -1, rotate, elongate, force_contig, SELECT_TORUS);
	allocate_part(request);

	// time(&start);
	pa_fini();
	// time(&end);
	// printf("fini: %ld\n", (end-start));

	delete_pa_request(request);
	gs_fini();
	
	return 0;
}
