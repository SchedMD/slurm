/*****************************************************************************\
 *  partition_sys.c - component used for wiring up the partitions
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

// #define DEBUG_ALLOCATE
// #define DEBUG_PART
// #define _RM_API_H__

/** need this to have it compile with the BGL header*/
// typedef int MPIR_PROCDESC;

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "partition_sys.h"
#include "bluegene.h"
// #include "rm_api.h"

#ifdef _RM_API_H__
#include "bgl_switch_connections.h"
#endif

/****************************/
/*   for testing purposes   */
uint16_t BGL_PARTITION_NUMBER;
/****************************/

#ifdef _UNIT_TEST_
extern void * lsd_fatal_error(char *file, int line, char *mesg){}
extern void * lsd_nomem_error(char *file, int line, char *mesg){}
#endif

/** */
#ifdef _RM_API_H__
char *BGL_MLOADER_IMAGE = "/bgl/edi/build/bglsys/bin/mmcs-mloader.rts";
char *BGL_BLRTS_IMAGE = "/bgl/edi/build/bglsys/bin//rts_hw.rts";
char *BGL_LINUX_IMAGE = "/bgl/edi/build/bglsys/bin/zImage.elf";
char *BGL_RAMDISK_IMAGE = "/bgl/edi/build/bglsys/bin/ramdisk.elf";
#endif

/** these are used in the dynamic partitioning algorithm */
/* global system = list of free partitions */
List bgl_sys_free = NULL;
/* global system = list of allocated partitions */
List bgl_sys_allocated = NULL;

void _init_sys(partition_t*);
int _is_not_equals_all_coord(uint16_t* rec_a, uint16_t* rec_b);
int _is_not_equals_some_coord(uint16_t* rec_a, uint16_t* rec_b);

#ifdef _RM_API_H__
/** 
 * _get_bp: get the BP at location loc
 *
 * IN - bgl: pointer to preinitialized bgl pointer
 * IN - bp: pointer to preinitailized rm_element_t that will 
 *      hold the BP that we resolve to.
 * IN - loc: location of the desired BP 
 * OUT - bp: will point to BP at location loc
 * OUT - rc: error code (0 = success)
 */
 int _get_bp(rm_element_t *bp, rm_location_t *loc);
 int _check_bp_status(rm_location_t *loc);
 void _pre_allocate(rm_partition_t* my_part, rm_connection_type_t* part_conn);
 int _post_allocate(rm_partition_t *my_part, pm_partition_id_t *part_id);
 int _get_switch_list(partition_t* partition, List* switch_list);
 int _get_bp_by_location(int* cur_coord, rm_BP_t** bp);
 void _rm_switch_t_destroy(void* object);

#else
 int _create_bgl_partitions(List requests);

#endif
 int _break_up_partition(List sys, partition_t* partition_to_break, int index);
 int _fit_request(List sys, List allocated, uint16_t* request);

 void _int_array_destroy(void* object);
 int _int_array_cmpf(uint16_t* rec_a, uint16_t* rec_b);

 int _partition_cmpf_inc(struct partition* rec_a, struct partition* rec_b);
 int _partition_cmpf_dec(struct partition* rec_a, struct partition* rec_b);

#ifdef _RM_API_H__
 void _preallocate(rm_BGL_t* bgl, rm_partition_t* my_part, 
 		 char* username, rm_connection_type_t* part_conn);
 int _postallocate(rm_BGL_t *bgl, rm_partition_t *my_part, 
		 pm_partition_id_t *part_id);
#endif

 List _get_bgl_sys_free();
 List _get_bgl_sys_allocated();
#ifdef _UNIT_TESTS_
extern void debug(const char *fmt, ...);
#endif


/** 
 * partition_sys: 
 * 
 * partition the system according to the given configuration.  We're
 * assuming that the input config array is only one dimension
 * (eg. only X configurations) and is sorted in decreasing order.
 * 
 * example usage: admin wants to partition system as such: 4x4x4,
 * 2x4x4, 2x4x4 to do this, we would run partition_sys three times with
 * the config as {4,2,2} (X-direction), then {4,4,4}(for Y) and
 * finally {4,4,4}( for Z).
 * 
 * we should really just have all the config stuff in one struct
 * and then have each element in the configs be of type part_config
 * 
 * IN - config: one of the following system configurations
 * IN - BGL_system: pointer to the bgl system functions..or something
 * OUT - partid_list: list of BGL partition id's 
 * OUT - return code of success
 * 
 * SIDE EFFECT: calls BGL CMCS API that changes the DB2 and
 * essentially wires up the system
 */

int partition_sys(List requests)
{

	ListIterator itr;
	partition_t part;
	uint16_t* request;
	int all_success = 0; // 0 = yes, 1 = no

	/* 1) we sort in decreasing order by size */
	sort_int_array_by_dec_size(requests);
	/* initialize the starting system */
	_init_sys(&part);

	if (bgl_sys_allocated == NULL)
		error("list_create failed for bgl_sys_allocated");

	/* 2) for each partition configuration, place them in 
	 * order 
	 */
	itr = list_iterator_create(requests);

#ifdef DEBUG_PART
	debug("REQUESTS: ");
	print_list(requests);
#endif
	while ((request = (uint16_t*) list_next(itr))) {
		if (_fit_request(bgl_sys_free, bgl_sys_allocated, request)){
#ifdef DEBUG_PART
			debug("failure in allocation!!!");
#endif
			all_success = 1; 
		} else {
#ifdef DEBUG_PART
			debug("success in allocation");
#endif
		}
	}
	list_iterator_destroy(itr);

	_create_bgl_partitions(bgl_sys_allocated);

	return all_success;
}

/** 
 * IN - requests: List <partition_t*> to wire up.
 * 
 */
 int _create_bgl_partitions(List requests)
{
	partition_t* cur_partition;	
	ListIterator itr;

	debug("partition_sys::_create_bgl_partitions");
	itr = list_iterator_create(requests);
	while ((cur_partition = (partition_t*) list_next(itr))) {
		configure_switches(cur_partition); 
	}	
	list_iterator_destroy(itr);
	return 0;
}

/** 
 * assign a list of nodes to the configuration
 * 
 * since we *know* that the configuration will 
 * fit in somewhere in a power of two in the system
 * we can always ensure a perfect fit.  Thus if the 
 * size of a given partition is two big, we can 
 * cut it in half.
 * 
 * we assume that the partitioning done before hand
 * 
 */
 int _fit_request(List sys, List allocated, uint16_t* request)
{
	int i, rc = 1;
	uint16_t* new_request = NULL;
	int request_size = int_array_size(request);
	partition_t* cur_partition;
	ListIterator itr;
	partition_t* partition_to_break = NULL;
	int partition_dim_max = -1;
	int max_index = SYSTEM_DIMENSIONS; /* we want the earliest, so we'll 
					    * set a good high point as the farthest 
					    * dimesion */

	if (sys == NULL || allocated == NULL || request == NULL)
		return 1;

	/** print out the request */
#ifdef DEBUG_PART
	debug("\nTrying to fit [ %d", request[0]);
	for (i=1; i<SYSTEM_DIMENSIONS; i++){
		debug(" x %d", request[i]);
	}
	debug(" ]\n");
	debug("current system list");
	print_sys_list(sys);
#endif
	rotate_part(request, &new_request);
	xfree(request);
	request = new_request;

	/** */
	/** this stuff is for knowing which partition we want to select to break */
	itr = list_iterator_create(sys);
	while ((cur_partition = (partition_t*) list_next(itr))) {
		if (!is_not_correct_dimension(cur_partition->dimensions, request)){
#ifdef DEBUG_PART
			debug("\n!!!!!!!!!!!!!!!!!\n!   FOUND FIT   !\n!!!!!!!!!!!!!!!!!\n");
			print_partition(cur_partition);
#endif
      
			list_push(allocated, cur_partition);
			list_remove(itr);
			rc = 0;
			break;
		} 

		/* this partition's too small to break up, so goto next. */
		if (cur_partition->size < request_size) {
			continue;

			/* big enough to break */
		} else {
			/* partition selection policy: 
			 *
			 * the largest dimension that is larger than the request (in
			 * some dimension) that is earliest (dimension wise).
			 */
			for (i=0; i<SYSTEM_DIMENSIONS; i++){
				/* if the current partition's dimension is greater
				 * than that requested
				 */	 
				if (cur_partition->dimensions[i] > request[i] && 
				    cur_partition->dimensions[i] > partition_dim_max &&
				    i < max_index){

					partition_to_break = cur_partition;
					partition_dim_max = cur_partition->dimensions[i];
					max_index = i;
				}
			}
		}
	}
	list_iterator_destroy(itr);
  
	/* well, if we have a partition to break, then we break apart the
	 * partition and then call ourselves again.  otherwise, we've
	 * exhausted all possibilities so we can't fit this request :(
	 */
	if (rc != 0 && partition_to_break != NULL){
		/* break up the partition and do the RECURSIVE CALL! */
		_break_up_partition(sys, partition_to_break, max_index);
		rc = _fit_request(sys, allocated, request);
		/** ??? FIXME, if something is not printed, the program will segfault, looks like 
		 * stdout just needs to be flushed or something */
		/** 999 */
		debug("");
	} 
	return rc;
}

/** 
 * break up a partition in half according to the index (dimension)
 * given.  since we expect to have only powers of 2 partitions later
 * on, we definetely want to simply split by what's requested.
 * 
 * IMPORTANT!!! I am assuming that we will ALWAYS have a power of 2, so
 * odd number sizes, and dimensions will kill this!!!
 * 
 */
 int _break_up_partition(List sys, partition_t* partition_to_break, 
		int index)
{
	/* the two new partitions to create */
	partition_t *first_part, *second_part;
	double diff; 
	partition_t* next;
	ListIterator itr;

	if (sys == NULL || partition_to_break == NULL)
		return 1;

	/** these get xfree'd when the sys list is destroyed */
	first_part = (partition_t*) xmalloc(sizeof(partition_t));
	second_part = (partition_t*) xmalloc(sizeof(partition_t));

	copy_partition(partition_to_break, first_part);
	copy_partition(partition_to_break, second_part);

	first_part->size /= 2;
	second_part->size /= 2;
	first_part->dimensions[index] /= 2;
	second_part->dimensions[index] /= 2;

	diff = partition_to_break->tr_coord[index] - 
		partition_to_break->bl_coord[index];
	first_part->tr_coord[index] = floor(diff/2);
	second_part->bl_coord[index] = ceil(diff/2);

	itr = list_iterator_create(sys);
	while ((next = (partition_t*) list_next(itr))) {
		if(!is_not_correct_dimension(next->dimensions, 
				partition_to_break->dimensions)){
			/* next we remove the old partition */
			list_remove(itr);

			/* then we insert our new partitions */
			list_append(sys, first_part);
			list_append(sys, second_part);
			break;
		}
	}
	list_iterator_destroy(itr);
	return 0;
}

void print_partition(partition_t* part)
{
	if (part == NULL)
		return;

	debug("\tdimensions: [ %d %d %d ]", part->dimensions[0], 
	      part->dimensions[1], part->dimensions[2]);
	debug("\tbl coord:   [ %d %d %d ]", part->bl_coord[0], 
	      part->bl_coord[1], part->bl_coord[2]);
	debug("\ttr coord:   [ %d %d %d ]", part->tr_coord[0], 
	      part->tr_coord[1], part->tr_coord[2]);
	debug("\tsize: %d", part->size);
	debug("\tbgl_record_ptr addr: %d", part->bgl_record_ptr);
}

void copy_partition(partition_t* src, partition_t* dest)
{
	int i;

	if (src == NULL || dest == NULL)
		return;

	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		dest->bl_coord[i] = src->bl_coord[i];
		dest->tr_coord[i] = src->tr_coord[i];
		dest->dimensions[i] = src->dimensions[i];
	}
	dest->size = src->size;
}

/** 
 * returns 0 for equals, 1 for not equals
 */
int is_partition_not_equals(partition_t* A, partition_t* B)
{
	if (A == NULL || B == NULL)
		return 1;

	if (A->bl_coord == B->bl_coord &&
	    A->tr_coord == B->tr_coord)
		return 0; 
	else 
		return 1; 
}

/** 
 * return - the int array's size
 */
int int_array_size(uint16_t* part_geometry)
{
	int size = 1;
	int i;
  
	if (part_geometry == NULL)
		return 0;

	for(i=0; i<SYSTEM_DIMENSIONS; i++){
		size *= part_geometry[i];
	}

	return size;
}

/** 
 * print out a list
 */
void print_list(List list)
{
	int* stuff = NULL, i = 0;
	ListIterator itr;

	if (list == NULL)
		return;

	debug("trying to get the list iterator");	
	itr = list_iterator_create(list);
	debug("done");

	debug("printing list");
	while ((stuff = (int*) list_next(itr))) {
		debug("stuff %d", stuff);
		if (stuff == NULL){
			break; 
		}

		debug("[ %d", stuff[0]);
		for (i=1; i<SYSTEM_DIMENSIONS; i++){
			debug(" x %d", stuff[i]);
		}
		debug(" ]");
	}
	list_iterator_destroy(itr);
}

/** 
 * print out list of the system partitions
 */
void print_sys_list(List list)
{
	ListIterator itr;
	int i, part_count=0;
	partition_t* stuff;

	if (list == NULL){
		debug("List is empty (NULL)");
		return;
	}

	itr = list_iterator_create(list);
	while ((stuff = (partition_t*) list_next(itr))) {
		if (stuff == NULL){
			break; 
		}

		debug("part %d: dimensions [ %d", part_count++, stuff->dimensions[0]);
		for (i=1; i<SYSTEM_DIMENSIONS; i++){
			debug(" x %d", stuff->dimensions[i]);
		}
		debug(" ]");

		debug("bl coord [ %d", stuff->bl_coord[0]);
		for (i=1; i<SYSTEM_DIMENSIONS; i++){
			debug(" x %d", stuff->bl_coord[i]);
		}
		debug(" ]");

		debug("tr coord [ %d", stuff->tr_coord[0]);
		for (i=1; i<SYSTEM_DIMENSIONS; i++){
			debug(" x %d", stuff->tr_coord[i]);
		}
		debug(" ]");

	}
	list_iterator_destroy(itr);
}

/** 
 * sort the configurations by decreasing size
 */
void sort_int_array_by_dec_size(List configs)
{
	if (configs == NULL)
		return;
	list_sort(configs, (ListCmpF) _int_array_cmpf);
}

/** 
 * Comparator used for sorting int arrays
 * 
 * returns: -1: rec_a > rec_b   0: rec_a == rec_b   1: rec_a < rec_b
 * 
 * Note: return values are "reversed" so that we can have the list
 * sorted in decreasing order (largest to smallest)
 */
 int _int_array_cmpf(uint16_t* rec_a, uint16_t* rec_b)
{
	int vol_a, vol_b;

	if (rec_a == NULL || rec_b == NULL)
		return -9;

	vol_a = int_array_size(rec_a);
	vol_b = int_array_size(rec_b);
	if (vol_a > vol_b)
		return -1;
	else if (vol_a < vol_b)
		return 1;
	else 
		return 0;
}

/** 
 * configure_switches = wire the partition as a mesh (pre_0_1 implementation)
 * 
 * returns 0 for success, 1 for failure
 */
#ifdef _RM_API_H__
int configure_switches(rm_partition_t* partition, partition_t* partition)
#else
int configure_switches(partition_t* partition)
#endif
{
	bgl_record_t* bgl_rec;
	int cur_coord[SYSTEM_DIMENSIONS]; 
	pm_partition_id_t* bgl_part_id;
#ifdef _RM_API_H__
	rm_switch_t* my_switch;
	rm_partition_t *bgl_part;

	pre_allocate(bgl_part, cur_partition->part_type);
#endif

	if (partition == NULL){
		return 1;
	}

	/** FIXME 
	 * right now the loop is (for example): 
	 * for bl: 102 to tr 323 (dim = 3x3x2 volume = 18)
	 * 102, 103
	 * 112, 113
	 * 122, 123
	 * 
	 * 202, 203
	 * 212, 213
	 * 222, 223
	 * 
	 * 302, 303
	 * 312, 313
	 * 322, 323
	 */

	/* for each of the dimensions */
	// int first = 1;
	for (cur_coord[0] = partition->bl_coord[0];
	     cur_coord[0] <= partition->tr_coord[0]; 
	     cur_coord[0]++){

		for (cur_coord[1] = partition->bl_coord[1];
		     cur_coord[1] <= partition->tr_coord[1]; 
		     cur_coord[1]++){
      
			for (cur_coord[2] = partition->bl_coord[2];
			     cur_coord[2] <= partition->tr_coord[2]; 
			     cur_coord[2]++){

#ifdef _RM_API_H__
				/***** BGL SPECIFIC ******/ 
				/** below, we wire up each all three switches of each BP **/
				/* SPECIAL CASE FIRST BP */
				if (!_is_not_equals_some_coord(cur_coord, partition->bl_coord)){
					List switch_list;
					ListIterator itr;

					if (!_get_switch_list(cur_coord, &switch_list)){
						error("configure_switches, error in getting bgl switch");
					}
					
					itr = list_iterator_create(switch_list);
					while ((cur_switch = (rm_switch_t*) list_next(itr))) {
						rm_dimension_t dim;
						rm_get_data(cur_switch,RM_SwitchDim,&dim);
						/** why is the X dim such a bizach */
						if (dim == RM_DIM_X){
							/** 
							 * FIXME, well see, here
							 * we should be hooking up 
							 * BP's 0 and 1 as in the "middle",
							 * that is, having both the
							 * "next and prev" connections
							 * and of course, this depends
							 * on the size of the BP.  if size=2,
							 * we're all right, but if size is
							 * greater, that it's the one's 
							 * in the physical middle that
							 * must be wired so....
							 */
							if (first == 1){
								connect_next(bgl_part, cur_switch, first);
								first = 0;
							}
						} else {
							if (first == 1){
								connect_next(bgl_part, cur_switch, first);
								first = 0;
							}
						}
					}

					/* now we have a valide BGL
					 * partition ID with which to
					 * submit jobs */

					/** FIXME now go get the BGL record and insert the new
					 * bgl_part_id */
					
					/* SPECIAL CASE END BP */
				} else if (!_is_not_equals_some_coord(cur_coord, partition->tr_coord)){
					; 
					
				/* NORMAL CASE, IN BETWEEN */
				} else {
					if (_get_switch_list(cur_coord, switch_list)){
						error("configure_switches, error in getting bgl switch");
					}
				}
`				
#else /* FOR DEBUGGING PURPOSES */
#ifdef DEBUG_ALLOCATE
				/***** DEBUG SPECIFIC (PRINT OUT RESULTS) ******/ 
				/* SPECIAL CASE FIRST BP */
				if (!_is_not_equals_some_coord(cur_coord, partition->bl_coord)){
					debug("allocate: connecting 1-3 of BP %d", cur_coord[0]);
					for (i=1; i<SYSTEM_DIMENSIONS; i++){
						debug(" x %d", cur_coord[i]);
					}
					debug("");


				/* SPECIAL CASE END BP */
				} else if (!_is_not_equals_some_coord(cur_coord, partition->tr_coord)){
					debug("allocate: connecting 0-4 of BP %d", cur_coord[0]);
					for (i=1; i<SYSTEM_DIMENSIONS; i++){
						debug(" x %d", cur_coord[i]);
					}
					debug("");
	  
				/* NORMAL CASE, IN BETWEEN */
				} else {
					debug("allocate: connecting 0-4,1-3 of BP %d", cur_coord[0]);
					for (i=1; i<SYSTEM_DIMENSIONS; i++){
						debug(" x %d", cur_coord[i]);
					}
					debug("");
				}
#endif
#endif
				
			} /* end of cur_coord[2]*/
		} /* end of cur_coord[1]*/
	} /* end of cur_coord[1]*/

	bgl_part_id = (pm_partition_id_t*) xmalloc(sizeof(pm_partition_id_t));

#ifdef _RM_API_H__
	post_allocate(bgl_part, bgl_part_id);
	bgl_rec = (bgl_record_t*) partition->bgl_record_ptr;
	bgl_rec->bgl_part_id = bgl_part_id;
	partition->bgl_part_id = bgl_part_id;

#else 

	// *bgl_part_id = (int)(rand()%100);
	*bgl_part_id = BGL_PARTITION_NUMBER++;
	bgl_rec = (bgl_record_t*) partition->bgl_record_ptr;
	bgl_rec->bgl_part_id = bgl_part_id;
	partition->bgl_part_id = bgl_part_id;

#endif
	return SLURM_SUCCESS;
}

/** 
 * find if the cur_part fits the same dimensions as the given request
 * return 0 for affirmative (correct dimension), and 1 for negative (not correct dimension)
 */
int is_not_correct_dimension(uint16_t* cur_part, uint16_t* request)
{
	int i, j;
	int cur_part_tmp[SYSTEM_DIMENSIONS];
	int end_of_array = SYSTEM_DIMENSIONS;
	int tmp, found_match;

	if (cur_part == NULL || request == NULL)
		return 1;

	/* copy over arrays into temporary arrays */
	for(i=0; i<SYSTEM_DIMENSIONS; i++){
		cur_part_tmp[i] = cur_part[i];
	}

	for(i=0; i<SYSTEM_DIMENSIONS; i++){
		found_match = 0;
		for(j=0; j<end_of_array; j++){
			if (request[i] == cur_part_tmp[j]){
				/* swap out end of array */
				tmp = cur_part_tmp[end_of_array-1];
				cur_part_tmp[end_of_array-1] = cur_part_tmp[j];
				cur_part_tmp[j] = tmp;	
				--end_of_array;
				found_match = 1;
				break;
			}
		}
		if (!found_match){
			break;
		}
	}

	/* if we've found all the elements, then the "end of array"
	 * should be 0
	 */
	if (!end_of_array){
		return 0;
	} else {
		return 1;
	}
}

int factorial(int numb)
{
	int i, fact = 1;
	for (i=numb; i > 0; i++)
		fact *= i;
	return fact;
}

/** 
 * return the index
 */
int max_dim_index(int* array)
{
	int i, max = -1, max_index = 0;
	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		if (array[i] > max){
			max = array[i];
			max_index = i;
		}
	}
  
	return max_index;
}

/** 
 * rotate the given partition configuration into decreasing
 * order. (ie, 2,1,4 -> 4,2,1).  
 * 
 * note: this is for 3d only!
 */
void rotate_part(const uint16_t* config, uint16_t** new_config)
{
	if (config == NULL)
		return;

	xfree(*new_config);
	(*new_config) = (uint16_t*) xmalloc(SYSTEM_DIMENSIONS * 
		sizeof(uint16_t));

	if (config[0] > config[1]){
		if (config[1] > config[2]){
			; // array already sorted
		} else {
			if (config[0] > config[2]){
				(*new_config)[0] = config[0];
				(*new_config)[1] = config[2];
				(*new_config)[2] = config[1];
				return;
			} else {
				(*new_config)[0] = config[2];
				(*new_config)[1] = config[0];
				(*new_config)[2] = config[1];
				return;
			}
		}
	/* config[0] <= config[1] */
	} else {
		if (config[1] > config[2]){
			if (config[0] > config [2]){
				(*new_config)[0] = config[1];
				(*new_config)[1] = config[0];
				(*new_config)[2] = config[2];
				return;
			} else {
				(*new_config)[0] = config[1];
				(*new_config)[1] = config[2];
				(*new_config)[2] = config[0];
				return;
			}
		} else {
			(*new_config)[0] = config[2];
			(*new_config)[1] = config[1];
			(*new_config)[2] = config[0];
			return;
		}
	}
}

/** 
 * get the initial configuration of the BGL system (or clean it up so
 * that we know what we're dealing with).
 * 
 * this should really go out and get BGL specific information
 * 
 */
 void _init_sys(partition_t *part)
{
	/* initialize the system wide partition */
	bgl_sys_free = list_create((ListDelF) _int_array_destroy);

	part->bl_coord[0] = 0;
	part->bl_coord[1] = 0;
	part->bl_coord[2] = 0;
	part->tr_coord[0] = (X_DIMENSION - 1);
	part->tr_coord[1] = (Y_DIMENSION - 1);
	part->tr_coord[2] = (Z_DIMENSION - 1);
	part->dimensions[0] = X_DIMENSION;
	part->dimensions[1] = Y_DIMENSION;
	part->dimensions[2] = Z_DIMENSION;
	part->size =  X_DIMENSION * Y_DIMENSION * Z_DIMENSION;

	/** ??? FIXME, I get a segfault if I have the list_create before the use of bgl_sys_free (swapping
	 * the following two statements.  WHY???
	 */
	list_push(bgl_sys_free, part);
	bgl_sys_allocated = list_create((ListDelF) _int_array_destroy);
}

/** 
 * to be used by list object to destroy the array elements
 */
 void _int_array_destroy(void* object)
{
	xfree(object);
}

#ifdef _RM_API_H__
/** 
 * initialize the BGL partition in the resource manager 
 */
void pre_allocate(rm_partition_t *my_part, rm_connection_type_t *part_conn)
{
	rm_new_partition(&my_part); //here we go... new partition to be added
	rm_set_data(my_part,RM_PartitionMloaderImg, BGL_MLOADER_IMAGE);
	rm_set_data(my_part,RM_PartitionBlrtsImg,  BGL_BLRTS_IMAGE);
	rm_set_data(my_part,RM_PartitionLinuxImg, BGL_LINUX_IMAGE);
	rm_set_data(my_part,RM_PartitionRamdiskImg, BGL_RAMDISK_IMAGE);
	rm_set_data(my_part,RM_PartitionConnection, part_conn);
}

/** 
 * add the partition record to the DB and boot it up!
 */
int post_allocate(rm_partition_t *my_part, pm_partition_id_t *part_id)
{
	int rc;
	rm_partition_state_t state;

	//Add partition record to the DB
	rc = rm_add_partition(my_part);
	if (rc != STATUS_OK){
		error("Error adding partition");
		return(-1);
	}

	// Get back the new partition id
	rm_get_data(my_part, RM_PartitionID, &part_id);

	//Initiate boot of the partition
	debug("Booting Partition %s", part_id);
	rc = pm_create_partition(part_id);
	if (rc != STATUS_OK){
		error("Error booting_partition partition");
		return(-1);
	}

	//Wait for Partition to be booted
	rc = rm_get_partition(part_id, &my_part);
	if (rc != STATUS_OK){
		error("Error in GetPartition");
		return(-1);
	}

	rm_get_data(my_part, RM_PartitionState, &state);
	error("Partition %s state = %s. Waiting...", part_id, convert_partition_state(state));
	fflush(stdout);
}

/** 
 * get switch of the BP of these coordinates
 */
int _get_switch_list(int* cur_coord, List* switch_list)
{
	int switch_num, i;
	rm_BP_t * bp;
	rm_BP_id_t *cur_bpid, *bpid;
	rm_switch_t* bgl_switch;
	int found_bpid;

	*switch_list = list_create(rm_switch_t_destroy);
	
	if (_get_bp_by_location(cur_coord, &bp)){
		return SLURM_ERROR;
	}
	
	rm_get_data(bp, RM_BPID, &bpid);
	
	rm_get_data(bgl, RM_SwitchNum, &switch_num);
	rm_get_data(my_bgl,RM_FirstSwitch,&my_switch);
	found_bpid = 0;
	for (i=0; i<SwitchNum; i++){
		rm_get_data(my_switch, RM_SwitchBPID, &cur_bpid);
		/** FIXME is there an equality function for BPID? */
		if (*bpid == *cur_bpid){
			found_bpid = 1;
			break;
		}

		rm_get_data(my_bgl,RM_NextSwitch,&my_switch);
	}

	if (found_bpid){
		/** 
		 * according to the mpirun example, I should be able to simply
		 * get these three switches in a row, and they should be XYZ
		 */

		list_push(switch_list, my_switch);
		for (i=0; i<2; i++){
			rm_get_data(my_bgl,RM_NextSwitch,&my_switch);
			/* i'm just going to check here again for my sanity*/
			if (*bpid != *cur_bpid)
				break;
			
			list_push(switch_list, my_switch);
		}
		
		return SLURM_SUCCESS;
	} 

	return SLURM_ERROR;
}

/** 
 * to be used by list object to destroy the array elements
 */
void rm_switch_t_destroy(void* object)
{
	/** FIXME, find out how to destroy one of these */
	xfree(object);
}


/** 
 * this is just stupid.  there are some implicit rules for where
 * "NextBP" goes to, but we don't know, so we have to do this.
 */
int _get_bp_by_location(rm_BGL_t* my_bgl, int* cur_coord, rm_BP_t** bp)
{
	int i, bp_num;
	rm_location_t loc;

	rm_get_data(my_bgl, RM_BPNum, &bp_num);
	rm_get_data(my_bgl, RM_FirstBP, bp);

	for (i=0; i<bp_num; i++){
		rm_get_data(*bp, RM_BPLoc, &loc);
		if (loc.X == cur_coord[0] && loc.Y == cur_coord[1] && loc.Z == cur_coord[2]){
			return SLURM_SUCCESS;
		}
		rm_get_data(my_bgl, RM_NextBP, bp);
	}

	// error("_get_bp_by_location: could not find specified bp.");
	return SLURM_ERROR;
}

#endif

/** 
 * non-equality for at least one coordinate
 * 
 * returns 0 if equals, 1 if not equals
 */
 int _is_not_equals_some_coord(uint16_t* rec_a, uint16_t* rec_b)
{
	int i;
	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		if (rec_a[i] == rec_b[i])
			return 0;
	}
	return 1;
}

/** 
 * non-equality for all coordinates
 * 
 * returns 0 if equals, 1 if not equals
 */
 int _is_not_equals_all_coord(uint16_t* rec_a, uint16_t* rec_b)
{
	int i;
	for (i=0; i<SYSTEM_DIMENSIONS; i++){
		if (rec_a[i] != rec_b[i])
			return 1;
	}
	return 0;
}

/** 
 * sort the partitions by increasing size
 */
 void _sort_partitions_by_inc_size(List parts){
	if (parts == NULL)
		return;
	list_sort(parts, (ListCmpF) _partition_cmpf_inc);
}

/** 
 * sort the partitions by decreasing size
 */
 void _sort_partitions_by_dec_size(List parts){
	if (parts == NULL)
		return;
	list_sort(parts, (ListCmpF) _partition_cmpf_dec);
}


/** 
 * Comparator used for sorting partitions smallest to largest
 * 
 * returns: -1: rec_a  < rec_b    0: rec_a == rec_b   1: rec_a > rec_b
 * 
 */
 int _partition_cmpf_inc(struct partition* rec_a, struct partition* rec_b)
{
	if (rec_a->size < rec_b->size)
		return -1;
	else if (rec_a->size > rec_b->size)
		return 1;
	else 
		return 0;
}

/** 
 * Comparator used for sorting partitions largest to smallest
 * 
 * returns: -1: rec_a > rec_b    0: rec_a == rec_b   1: rec_a < rec_b
 * 
 */
 int _partition_cmpf_dec(struct partition* rec_a, struct partition* rec_b)
{
	if (rec_a->size > rec_b->size)
		return -1;
	else if (rec_a->size < rec_b->size)
		return 1;
	else 
		return 0;
}

/** */
 List _get_bgl_sys_allocated()
{
	return bgl_sys_allocated;	
}

/** */
 List _get_bgl_sys_free()
{
	return bgl_sys_free;
}

#ifdef _UNIT_TESTS_
extern void debug(const char *fmt, ...)
{
	printf(fmt, ...);
}
#endif

void init_bgl_partition_num()
{
	BGL_PARTITION_NUMBER = 0;
}
