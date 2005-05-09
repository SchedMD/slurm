/*****************************************************************************\
 *  partition_allocator.c
 * 
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <math.h>
#include "partition_allocator.h"

#ifdef HAVE_BGL_FILES
# include "src/plugins/select/bluegene/wrap_rm_api.h"
#endif

#define DEBUG_PA
#define BEST_COUNT_INIT 10;

#ifdef HAVE_BGL
int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {0,0,0};
#else
int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {0};
#endif

bool _initialized = false;

/* _pa_system is the "current" system that the structures will work
 *  on */
pa_system_t *pa_system_ptr = NULL;
List path = NULL;
List best_path = NULL;
int best_count;
int color_count = 0;
char letters[36];
char colors[6];

/** internal helper functions */
#ifdef HAVE_BGL_FILES
/** */
static void _bp_map_list_del(void *object);
#endif
#ifdef HAVE_BGL
/* */
static int _check_for_options(pa_request_t* pa_request); 
/* */
static int _append_geo(int *geo, List geos, int rotate);
/* */
static int _create_config_even(pa_node_t ***grid);

#else
/* */
static int _create_config_even(pa_node_t *grid);
#endif

/** */
static void _new_pa_node(pa_node_t *pa_node, 
		int *coord);
/** */
//static void _print_pa_node(pa_node_t* pa_node);
/** */
static void _create_pa_system(void);
/* */
static void _delete_pa_system(void);
/* find the first partition match in the system */
static int _find_match(pa_request_t* pa_request, List results);

static bool _node_used(pa_node_t* pa_node, int *geometry);

/* */
static void _switch_config(pa_node_t* source, pa_node_t* target, int dim, 
			int port_src, int port_tar);
/* */
static void _set_external_wires(int dim, int count, pa_node_t* source, 
			 pa_node_t* target_1, pa_node_t* target_2);
/* */
static char *_set_internal_wires(List nodes, int size, int conn_type);

/* */
static int _find_one_hop(pa_switch_t *curr_switch, int source_port, 
			int *target, int *target2, int dim);
/* */
static int _find_best_path(pa_switch_t *start, int source_port, int *target, 
			int *target2, int dim, int count);
/* */
static int _set_best_path(void);
/* */
static int _configure_dims(int *coord, int *start, int *end, int dim);
/* */
static int _set_one_dim(int *start, int *end, int *coord);

/* Global */
List bp_map_list;
List bgl_info_list;

extern void destroy_bgl_info_record(void* object)
{
	bgl_info_record_t* bgl_info_record = (bgl_info_record_t*) object;

	if (bgl_info_record) {
		if(bgl_info_record->nodes) 
			xfree(bgl_info_record->nodes);
		if(bgl_info_record->owner_name)
			xfree(bgl_info_record->owner_name);
		if(bgl_info_record->bgl_part_id)
			xfree(bgl_info_record->bgl_part_id);
		
		xfree(bgl_info_record);
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
 * IN - conn_type: connection type of request (SELECT_TORUS or SELECT_MESH)
 * 
 * return SUCCESS of operation.
 */
extern int new_pa_request(pa_request_t* pa_request)
{
	int i=0;
#ifdef HAVE_BGL
	float sz=1;
	int geo[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	int i2, i3, picked, total_sz=1 , size2, size3;
	ListIterator itr;
	int checked[8];
	int *geo_ptr;
	
	pa_request->rotate_count= 0;
	pa_request->elongate_count = 0;
	pa_request->elongate_geos = list_create(NULL);
	geo[X] = pa_request->geometry[X];
	geo[Y] = pa_request->geometry[Y];
	geo[Z] = pa_request->geometry[Z];
		
	if(geo[X] != -1) { 
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			if ((geo[i] < 1) 
			    ||  (geo[i] > DIM_SIZE[i])){
				error("new_pa_request Error, request geometry is invalid %d", geo[i]); 
				return 0;
			}
		}
		_append_geo(geo, pa_request->elongate_geos, 0);
		sz=1;
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++)
			sz *= pa_request->geometry[i];
		pa_request->size = sz;
		sz=0;
	}
	
	if(pa_request->elongate || sz) {
		sz=1;
		/* decompose the size into a cubic geometry */
		pa_request->rotate= 1;
		pa_request->elongate = 1;		
		
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++) { 
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}
		
		if(pa_request->size==1) {
			_append_geo(geo, pa_request->elongate_geos, pa_request->rotate);
			goto endit;
		}
		
		if(pa_request->size<=DIM_SIZE[Y]) {
			geo[X] = 1;
			geo[Y] = pa_request->size;
			geo[Z] = 1;
			sz=pa_request->size;
			_append_geo(geo, pa_request->elongate_geos, pa_request->rotate);
		}
		
		if(pa_request->size>total_sz || pa_request->size<1) {
			//error("new_pa_request ERROR, requested size must be\ngreater than 0 and less than %d.",total_sz);
			return 0;			
		}
			
	startagain:		
		picked=0;
		for(i=0;i<8;i++)
			checked[i]=0;
		
		size3=pa_request->size;
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++) {
			total_sz *= DIM_SIZE[i];
			geo[i] = 1;
		}
	
		sz = 1;
		size3=pa_request->size;
		picked=0;
	tryagain:	
		if(size3!=pa_request->size)
			size2=size3;
		else
			size2=pa_request->size;
		//messedup:
		for (i=picked; i<PA_SYSTEM_DIMENSIONS; i++) { 
			if(size2<=1) 
				break;
				
			sz = size2%DIM_SIZE[i];
			if(!sz) {
				geo[i] = DIM_SIZE[i];	
				size2 /= DIM_SIZE[i];
			} else if (size2 > DIM_SIZE[i]){
				for(i2=(DIM_SIZE[i]-1);i2>1;i2--) {
					/* go through each number to see if the size 
					   is divisable by a smaller number that is 
					   good in the other dims. */
					if (!(size2%i2) && !checked[i2]) {
						size2 /= i2;
							
						if(i==0)
							checked[i2]=1;
							
						if(i2<DIM_SIZE[i]) 
							geo[i] = i2;
						else 
							goto tryagain;
						if((i2-1)!=1 && i!=(PA_SYSTEM_DIMENSIONS-1))
							break;
					}		
				}				
				if(i2==1) {
					pa_request->size +=1;
					goto startagain;
				}
						
			} else {
				geo[i] = sz;	
				break;
			}					
		}
		
		if((geo[X]*geo[Y]) <= DIM_SIZE[Y]) {
			pa_request->geometry[X] = 1;
			pa_request->geometry[Y] = geo[X] * geo[Y];
			pa_request->geometry[Z] = geo[Z];	
			_append_geo(pa_request->geometry, pa_request->elongate_geos, pa_request->rotate);

		}
		if((geo[X]*geo[Z]) <= DIM_SIZE[Y]) {
			pa_request->geometry[X] = 1;
			pa_request->geometry[Y] = geo[Y];
			pa_request->geometry[Z] = geo[X] * geo[Z];	
			_append_geo(pa_request->geometry, pa_request->elongate_geos, pa_request->rotate);		
	
		}
		_append_geo(geo, pa_request->elongate_geos, pa_request->rotate);
	
		/* see if We can find a cube or square root of the size to make an easy cube */
		for(i=0;i<PA_SYSTEM_DIMENSIONS-1;i++) {
			sz = powf((float)pa_request->size,(float)1/(PA_SYSTEM_DIMENSIONS-i));
			if(pow(sz,(PA_SYSTEM_DIMENSIONS-i))==pa_request->size)
				break;
		}
	
		if(i<PA_SYSTEM_DIMENSIONS-1) {
			/* we found something that looks like a cube! */
			i3=i;
			for (i=0; i<i3; i++) 
				geo[i] = 1;			
			
			for (i=i3; i<PA_SYSTEM_DIMENSIONS; i++)  
				if(sz<=DIM_SIZE[i]) 
					geo[i] = sz;	
				else
					goto endit;
			
			_append_geo(geo, pa_request->elongate_geos, pa_request->rotate);
		} 
	}
	
endit:
	itr = list_iterator_create(pa_request->elongate_geos);
	geo_ptr = list_next(itr);
	list_iterator_destroy(itr);
	
	if(geo_ptr == NULL)
		return 0;

	pa_request->elongate_count++;
	pa_request->geometry[X] = geo_ptr[X];
	pa_request->geometry[Y] = geo_ptr[Y];
	pa_request->geometry[Z] = geo_ptr[Z];
	sz=1;
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++)
		sz *= pa_request->geometry[i];
	pa_request->size = sz;
			
#else
	int geo[PA_SYSTEM_DIMENSIONS] = {0};
	
	pa_request->rotate_count= 0;
	pa_request->elongate_count = 0;
	pa_request->elongate_geos = list_create(NULL);
	geo[X] = pa_request->geometry[X];
		
	if(geo[X] != -1) { 
		for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
			if ((geo[i] < 1) 
			    ||  (geo[i] > DIM_SIZE[i])){
				error("new_pa_request Error, request geometry is invalid %d", 
				      geo[i]); 
				return 0;
			}
		}
		
		pa_request->size = pa_request->geometry[X];

	} else if (pa_request->size) {
		pa_request->geometry[X] = pa_request->size;
	} else
		return 0;
			
#endif
	return 1;
}

/**
 * delete a partition request 
 */
extern void delete_pa_request(pa_request_t *pa_request)
{
	int *geo_ptr;

	if(pa_request->save_name!=NULL)
		xfree(pa_request->save_name);
	
	while((geo_ptr = list_pop(pa_request->elongate_geos)) != NULL)
		xfree(geo_ptr);

	xfree(pa_request);
}

/**
 * print a partition request 
 */
extern void print_pa_request(pa_request_t* pa_request)
{
	int i;

	if (pa_request == NULL){
		error("print_pa_request Error, request is NULL");
		return;
	}
	debug("  pa_request:");
	debug("    geometry:\t");
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		debug("%d", pa_request->geometry[i]);
	}
	debug("");
	debug("        size:\t%d", pa_request->size);
	debug("   conn_type:\t%d", pa_request->conn_type);
	debug("      rotate:\t%d", pa_request->rotate);
	debug("    elongate:\t%d", pa_request->elongate);
	debug("force contig:\t%d", pa_request->force_contig);
	debug("     node_use:\t%d", pa_request->node_use);
}

/**
 * Initialize internal structures by either reading previous partition
 * configurations from a file or by running the graph solver.
 * 
 * IN: node_info_msg_t * can be null, 
 *     should be from slurm_load_node().
 * 
 * return: void.
 */
extern void pa_init(node_info_msg_t *node_info_ptr)
{
	node_info_t *node_ptr = NULL;
	int i;
	int start, temp;
	char *numeric = NULL;
#ifdef HAVE_BGL_FILES
	rm_BGL_t *bgl = NULL;
	rm_size3D_t bp_size;
	int rc = 0;
#endif		
	/* We only need to initialize once, so return if already done so. */

	if (_initialized){
		return;
	}

	best_count=BEST_COUNT_INIT;
						
	pa_system_ptr = (pa_system_t *) xmalloc(sizeof(pa_system_t));
	
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord = 1;
	pa_system_ptr->num_of_proc = 0;
	pa_system_ptr->resize_screen = 0;
	
	if(node_info_ptr!=NULL) {
		for (i = 0; i < node_info_ptr->record_count; i++) {
			node_ptr = &node_info_ptr->node_array[i];
			start = 0;
			numeric = node_ptr->name;
			while (numeric) {
				if ((numeric[0] < '0')
				||  (numeric[0] > '9')) {
					numeric++;
					continue;
				}
				start = atoi(numeric);
				break;
			}
#ifdef HAVE_BGL
			temp = start / 100;
			if (DIM_SIZE[X] < temp)
				DIM_SIZE[X] = temp;
			temp = (start / 10) % 10;
			if (DIM_SIZE[Y] < temp)
				DIM_SIZE[Y] = temp;
			temp = start % 10;
			if (DIM_SIZE[Z] < temp)
				DIM_SIZE[Z] = temp;
#else
			temp = start;
			if (DIM_SIZE[X] < temp)
				DIM_SIZE[X] = temp;
#endif
		}
		DIM_SIZE[X]++;
#ifdef HAVE_BGL
		DIM_SIZE[Y]++;
		DIM_SIZE[Z]++;
#endif
		pa_system_ptr->num_of_proc = node_info_ptr->record_count;
        } 

#ifdef HAVE_BGL_FILES
	if ((DIM_SIZE[X]==0) && (DIM_SIZE[X]==0) && (DIM_SIZE[X]==0)) {
		if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
			error("rm_set_serial(%s): %d", BGL_SERIAL, rc);
			return;
		}
		if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
			error("rm_get_BGL(): %d", rc);
			return;
		}
		
		if ((bgl != NULL)
		&&  ((rc = rm_get_data(bgl, RM_Msize, &bp_size)) == STATUS_OK)) {
			DIM_SIZE[X]=bp_size.X;
			DIM_SIZE[Y]=bp_size.Y;
			DIM_SIZE[Z]=bp_size.Z;
		} else {
			error("rm_get_data(RM_Msize): %d", rc);	
		}
		if ((rc = rm_free_BGL(bgl)) != STATUS_OK)
			error("rm_free_BGL(): %d", rc);
	}
#endif

#ifdef HAVE_BGL
	if ((DIM_SIZE[X]==0) && (DIM_SIZE[X]==0) && (DIM_SIZE[X]==0)) {
		debug("Setting default system dimensions");
		DIM_SIZE[X]=8;
		DIM_SIZE[Y]=4;
		DIM_SIZE[Z]=4;
	}
#else 
	if ((DIM_SIZE[X]==0) && (DIM_SIZE[X]==0) && (DIM_SIZE[X]==0)) {
		debug("Setting default system dimensions");
		DIM_SIZE[X]=100;
	}	
#endif

	if(!pa_system_ptr->num_of_proc)
		pa_system_ptr->num_of_proc = DIM_SIZE[X] * DIM_SIZE[Y] * DIM_SIZE[Z];

		
	_create_pa_system();
	
	init_grid(node_info_ptr);
	
	_create_config_even(pa_system_ptr->grid);
	
	path = list_create(NULL);
	best_path = list_create(NULL);

	_initialized = true;
}

/** 
 * destroy all the internal (global) data structs.
 */
extern void pa_fini()
{
	if (!_initialized){
		return;
	}

	if (path)
		list_destroy(path);
	if (best_path)
		list_destroy(best_path);
#ifdef HAVE_BGL_FILES
	if (bp_map_list)
		list_destroy(bp_map_list);
#endif
	_delete_pa_system();

//	debug2("pa system destroyed");
}


/** 
 * set the node in the internal configuration as unusable
 * 
 * IN c: coordinate of the node to put down
 */
extern void pa_set_node_down(pa_node_t *pa_node)
{
	if (!_initialized){
		error("Error, configuration not initialized, "
			"call init_configuration first");
		return;
	}

#ifdef DEBUG_PA
#ifdef HAVE_BGL
	debug("pa_set_node_down: node to set down: [%d%d%d]", pa_node->coord[X], pa_node->coord[Y], pa_node->coord[Z]); 
#else
	debug("pa_set_node_down: node to set down: [%d]", pa_node->coord[X]); 
#endif
#endif

	/* basically set the node as used */
	pa_node->used = true;
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
extern int allocate_part(pa_request_t* pa_request, List results)
{

	if (!_initialized){
		error("allocate_part Error, configuration not initialized, call init_configuration first");
		return 0;
	}

	if (!pa_request){
		error("allocate_part Error, request not initialized");
		return 0;
	}
	
	// _backup_pa_system();
	if (_find_match(pa_request, results)){
		return 1;
	} else {
		return 0;
	}
}

extern int _reset_the_path(pa_switch_t *curr_switch, int source, int target, int dim)
{
	int *node_tar;
	int port_tar;
	pa_switch_t *next_switch = NULL; 
	/*set the switch to not be used */
	curr_switch->int_wire[source].used = 0;
	port_tar = curr_switch->int_wire[source].port_tar;
	curr_switch->int_wire[source].port_tar = source;
	curr_switch->int_wire[port_tar].used = 0;
	curr_switch->int_wire[port_tar].port_tar = port_tar;
	if(port_tar==target)
		return 1;
	/* follow the path */
	node_tar = curr_switch->ext_wire[port_tar].node_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	
#ifdef HAVE_BGL
	next_switch = &pa_system_ptr->
		grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
#else
	next_switch = &pa_system_ptr->
		grid[node_tar[X]].axis_switch[dim];
#endif

	_reset_the_path(next_switch, port_tar, target, dim);
	return 1;
}

/** 
 * Doh!  Admin made a boo boo.  Note: Undo only has one history
 * element, so two consecutive undo's will fail.
 *
 * returns SLURM_SUCCESS if undo was successful.
 */
extern int remove_part(List nodes, int new_count)
{
	int dim;
	pa_node_t* pa_node = NULL;
	pa_switch_t *curr_switch = NULL; 
		
	while((pa_node = (pa_node_t*) list_pop(nodes)) != NULL) {
		pa_node->used = false;
		pa_node->color = 7;
		pa_node->letter = '.';
		
		for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) {
			curr_switch = &pa_node->axis_switch[dim];
			if(curr_switch->int_wire[0].used) {
				_reset_the_path(curr_switch, 0, 1, dim);
			}
			
			if(curr_switch->int_wire[1].used) {
				_reset_the_path(curr_switch, 1, 0, dim);
			}
		}
	}
	color_count=new_count;			
	return 1;
}

/** 
 * Doh!  Admin made a boo boo.  Note: Undo only has one history
 * element, so two consecutive undo's will fail.
 *
 * returns SLURM_SUCCESS if undo was successful.
 */
extern int alter_part(List nodes, int conn_type)
{
	int dim;
	pa_node_t* pa_node = NULL;
	pa_switch_t *curr_switch = NULL; 
	int size=0;
	char *name = NULL;
	ListIterator results_i;		

	results_i = list_iterator_create(nodes);
	while ((pa_node = list_next(results_i)) != NULL) {
		pa_node->used = false;
		
		for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) {
			curr_switch = &pa_node->axis_switch[dim];
			if(curr_switch->int_wire[0].used) {
				_reset_the_path(curr_switch, 0, 1, dim);
			}
			
			if(curr_switch->int_wire[1].used) {
				_reset_the_path(curr_switch, 1, 0, dim);
			}
		}
		size++;
	}
	list_iterator_destroy(results_i);
	if((name = _set_internal_wires(nodes, size, conn_type)) == NULL)
		return SLURM_ERROR;
	else {
		xfree(name);
		return SLURM_SUCCESS;
	}
}

/** 
 * After a partition is deleted or altered following allocations must
 * be redone to make sure correct path will be used in the real system
 *
 */
extern int redo_part(List nodes, int conn_type, int new_count)
{
	int dim;
	pa_node_t* pa_node;
	pa_switch_t *curr_switch; 
	int size=0;
	char *name = NULL;
	ListIterator results_i;		

	results_i = list_iterator_create(nodes);
	while ((pa_node = list_next(results_i)) != NULL) {
		pa_node->used = false;

		pa_node->letter = letters[new_count%62];			
		pa_node->color = colors[new_count%6];
			
		for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) {
			curr_switch = &pa_node->axis_switch[dim];
			if(curr_switch->int_wire[0].used) {
				_reset_the_path(curr_switch, 0, 1, dim);
			}
			
			if(curr_switch->int_wire[1].used) {
				_reset_the_path(curr_switch, 1, 0, dim);
			}
		}
		size++;
	}
	color_count++;
	list_iterator_destroy(results_i);
	if((name = _set_internal_wires(nodes, size, conn_type)) == NULL)
		return SLURM_ERROR;
	else {
		xfree(name);
		return SLURM_SUCCESS;
	}
}

extern int set_bgl_part(List nodes, int size, int conn_type)
{
	char *name;
	if((name = _set_internal_wires(nodes, size, conn_type)) == NULL)
		return SLURM_ERROR;
	else {
		xfree(name);
		return SLURM_SUCCESS;
	}
}

extern int reset_pa_system()
{
	int x, y, z;
	int coord[PA_SYSTEM_DIMENSIONS];

	for (x = 0; x < DIM_SIZE[X]; x++)
		for (y = 0; y < DIM_SIZE[Y]; y++)
			for (z = 0; z < DIM_SIZE[Z]; z++) {
#ifdef HAVE_BGL
				coord[X] = x;
				coord[Y] = y;
				coord[Z] = z;
				_new_pa_node(&pa_system_ptr->grid[x][y][z], coord);
#else
				coord[X] = x;
				_new_pa_node(&pa_system_ptr->grid[x], coord);

#endif
			}
				
	return 1;
}
/* init_grid - set values of every grid point */
extern void init_grid(node_info_msg_t * node_info_ptr)
{
	node_info_t *node_ptr;
	int x, i = 0;
	uint16_t node_base_state;
	/* For systems with more than 62 active jobs or BGL blocks, 
	 * we just repeat letters */

#ifdef HAVE_BGL
	int y,z;
	for (x = 0; x < DIM_SIZE[X]; x++)
		for (y = 0; y < DIM_SIZE[Y]; y++)
			for (z = 0; z < DIM_SIZE[Z]; z++) {
				if(node_info_ptr!=NULL) {
					node_ptr = &node_info_ptr->node_array[i];
					node_base_state = 
						(node_ptr->node_state) 
						& (~NODE_STATE_NO_RESPOND);
					pa_system_ptr->grid[x][y][z].color = 7;
					if ((node_base_state == NODE_STATE_DOWN) || 
					    (node_base_state == NODE_STATE_DRAINED) || 
					    (node_base_state == NODE_STATE_DRAINING)) {
						pa_system_ptr->grid[x][y][z].color = 0;
						pa_system_ptr->grid[x][y][z].letter = '#';
						if(_initialized) {
							pa_set_node_down(&pa_system_ptr->grid[x][y][z]);
						}
					} else {
						pa_system_ptr->grid[x][y][z].color = 7;
						pa_system_ptr->grid[x][y][z].letter = '.';
					}
					pa_system_ptr->grid[x][y][z].state = node_ptr->node_state;
				} else {
					pa_system_ptr->grid[x][y][z].color = 7;
					pa_system_ptr->grid[x][y][z].letter = '.';
					pa_system_ptr->grid[x][y][z].state = 
						NODE_STATE_IDLE;
				}
				pa_system_ptr->grid[x][y][z].indecies = i++;
			}
#else
	for (x = 0; x < DIM_SIZE[X]; x++) {
		if(node_info_ptr!=NULL) {
			node_ptr = &node_info_ptr->node_array[i];
			node_base_state = 
				(node_ptr->node_state) 
				& (~NODE_STATE_NO_RESPOND);
			pa_system_ptr->grid[x].color = 7;
			if ((node_base_state == NODE_STATE_DOWN) || 
			    (node_base_state == NODE_STATE_DRAINED) || 
			    (node_base_state == NODE_STATE_DRAINING)) {
				pa_system_ptr->grid[x].color = 0;
				pa_system_ptr->grid[x].letter = '#';
				if(_initialized) {
					pa_set_node_down(&pa_system_ptr->grid[x]);
				}
			} else {
				pa_system_ptr->grid[x].color = 7;
				pa_system_ptr->grid[x].letter = '.';
			}
			pa_system_ptr->grid[x].state = node_ptr->node_state;
		} else {
			pa_system_ptr->grid[x].color = 7;
			pa_system_ptr->grid[x].letter = '.';
			pa_system_ptr->grid[x].state = 
				NODE_STATE_IDLE;
		}
		pa_system_ptr->grid[x].indecies = i++;
	}
#endif
	return;
}

extern int *find_bp_loc(char* bp_id)
{
#ifdef HAVE_BGL_FILES
	pa_bp_map_t *bp_map = NULL;
	ListIterator itr;
	
	if(!bp_map_list)
		set_bp_map();
		
	itr = list_iterator_create(bp_map_list);
	while ((bp_map = list_next(itr)) != NULL)
		if (!strcmp(bp_map->bp_id, bp_id)) 
			break;	/* we found it */
	
	list_iterator_destroy(itr);
	if(bp_map != NULL)
		return bp_map->coord;
	else
		return NULL;

#else
	return NULL;
#endif
}

extern char *find_bp_rack_mid(char* xyz)
{
#ifdef HAVE_BGL_FILES
	pa_bp_map_t *bp_map = NULL;
	ListIterator itr;
	int number;
	int coord[PA_SYSTEM_DIMENSIONS];

	number = atoi(&xyz[X]);
	coord[X] = number / 100;
	coord[Y] = (number % 100) / 10;
	coord[Z] = (number % 10);
	if(!bp_map_list)
		set_bp_map();
	
	itr = list_iterator_create(bp_map_list);
	while ((bp_map = list_next(itr)) != NULL)
		if (bp_map->coord[X] == coord[X] &&
		    bp_map->coord[Y] == coord[Y] &&
		    bp_map->coord[Z] == coord[Z]) 
			break;	/* we found it */
	
	list_iterator_destroy(itr);
	if(bp_map != NULL)
		return bp_map->bp_id;
	else
		return NULL;

#else
	return NULL;
#endif
}

/********************* Local Functions *********************/

#ifdef HAVE_BGL_FILES
static void _bp_map_list_del(void *object)
{
	pa_bp_map_t *bp_map = (pa_bp_map_t *)object;
	
	if (bp_map) {
		xfree(bp_map);		
	}
}
#endif
#ifdef HAVE_BGL
static int _check_for_options(pa_request_t* pa_request) 
{

	int temp;
	int set=0;
	int *geo = NULL;
	ListIterator itr;

	if(pa_request->rotate) {
	rotate_again:
		//debug("Rotating! %d",pa_request->rotate_count);
		
		if (pa_request->rotate_count==(PA_SYSTEM_DIMENSIONS-1)) {
			temp=pa_request->geometry[X];
			pa_request->geometry[X]=pa_request->geometry[Z];
			pa_request->geometry[Z]=temp;
			pa_request->rotate_count++;
			set=1;
		
		} else if(pa_request->rotate_count<(PA_SYSTEM_DIMENSIONS*2)) {
			temp=pa_request->geometry[X];
			pa_request->geometry[X]=pa_request->geometry[Y];
			pa_request->geometry[Y]=pa_request->geometry[Z];
			pa_request->geometry[Z]=temp;
			pa_request->rotate_count++;
			set=1;
		} else 
			pa_request->rotate = false;
		if(set) {
			if(pa_request->geometry[X]<=DIM_SIZE[X] 
			   && pa_request->geometry[Y]<=DIM_SIZE[Y]
			   && pa_request->geometry[Z]<=DIM_SIZE[Z])			   
				return 1;
			else {
				set = 0;
				goto rotate_again;
			}
		}
	}
	if(pa_request->elongate) {
	elongate_again:
		//debug("Elongating! %d",pa_request->elongate_count);
		pa_request->rotate_count=0;
		pa_request->rotate = true;
		
		set = 0;
		itr = list_iterator_create(pa_request->elongate_geos);
		for(set=0; set<=pa_request->elongate_count; set++)
			geo = list_next(itr);
		list_iterator_destroy(itr);
		if(geo == NULL)
			return 0;
		pa_request->elongate_count++;
		pa_request->geometry[X] = geo[X];
		pa_request->geometry[Y] = geo[Y];
		pa_request->geometry[Z] = geo[Z];
		if(pa_request->geometry[X]<=DIM_SIZE[X] 
		   && pa_request->geometry[Y]<=DIM_SIZE[Y]
		   && pa_request->geometry[Z]<=DIM_SIZE[Z]) {
			return 1;
		} else 				
			goto elongate_again;
		
	}
	return 0;
}

static int _append_geo(int *geometry, List geos, int rotate) 
{
	ListIterator itr;
	int *geo_ptr = NULL;
	int *geo = NULL;
	int temp_geo;
	int i, j;
	geo = xmalloc(sizeof(int)*3);
	
	if(rotate) {
		for (i = (PA_SYSTEM_DIMENSIONS - 1); i >= 0; i--) {
			for (j = 1; j <= i; j++) {
				if (geometry[j-1] > geometry[j]) {
					temp_geo = geometry[j-1];
					geometry[j-1] = geometry[j];
					geometry[j] = temp_geo;
				}
			}
		}
	}
	itr = list_iterator_create(geos);
	while ((geo_ptr = list_next(itr)) != NULL) {
		if(geometry[X] == geo_ptr[X]
		   && geometry[Y] == geo_ptr[Y]
		   && geometry[Z] == geo_ptr[Z])
			break;
		
	}
	list_iterator_destroy(itr);
	
	if(geo_ptr == NULL) { 
		geo[X] = geometry[X];
		geo[Y] = geometry[Y];
		geo[Z] = geometry[Z];

		list_append(geos, geo);
	}
	return 1;
}
#endif

/** */
#ifdef HAVE_BGL
static int _create_config_even(pa_node_t ***grid)
#else
static int _create_config_even(pa_node_t *grid)
#endif
{
	int x;
	pa_node_t *source = NULL, *target_1 = NULL;

#ifdef HAVE_BGL
	int y,z;
	pa_node_t *target_2 = NULL;
	for(x=0;x<DIM_SIZE[X];x++) {
		for(y=0;y<DIM_SIZE[Y];y++) {
			for(z=0;z<DIM_SIZE[Z];z++) {
				source = &grid[x][y][z];
				
				if(x<(DIM_SIZE[X]-1)) {
					target_1 = &grid[x+1][y][z];
				} else
					target_1 = NULL;
				if(x<(DIM_SIZE[X]-2))
					target_2 = &grid[x+2][y][z];
				else
					target_2 = target_1;
				_set_external_wires(X, x, source, 
						target_1, target_2);
				
				if(y<(DIM_SIZE[Y]-1)) 
					target_1 = &grid[x][y+1][z];
				else 
					target_1 = &grid[x][0][z];
				
				_set_external_wires(Y, y, source, 
						    target_1, NULL);
				if(z<(DIM_SIZE[Z]-1)) 
					target_1 = &grid[x][y][z+1];
				else 
					target_1 = &grid[x][y][0];
				
				_set_external_wires(Z, z, source, 
						    target_1, NULL);
			}
		}
	}
#else
	for(x=0;x<DIM_SIZE[X];x++) {
		source = &grid[x];
				
		/* if(x<(DIM_SIZE[X]-1)) { */
/* 			target_1 = &grid[x+1]; */
/* 		} else */
/* 			target_1 = NULL; */
/* 		if(x<(DIM_SIZE[X]-2)) */
/* 			target_2 = &grid[x+2]; */
/* 		else */
/* 			target_2 = target_1; */
/* 		target_first = &grid[0]; */
/* 		if (DIM_SIZE[X] > 1) */
/* 			target_second = &grid[1]; */
/* 		else */
/* 			target_second = target_first; */
	/* 	_set_external_wires(X, x, source,  */
/* 				    target_1, target_2,  */
/* 				    target_first, target_second); */
				
		target_1 = &grid[x+1];
		
		_set_external_wires(X, x, source, 
				    target_1, NULL);
	}
#endif
	return 1;
}

/** */
extern int set_bp_map(void)
{
#ifdef HAVE_BGL_FILES
	static rm_BGL_t *bgl = NULL;
	int rc;
	rm_BP_t *my_bp = NULL;
	pa_bp_map_t *bp_map = NULL;
	int bp_num, i;
	char *bp_id = NULL;
	rm_location_t bp_loc;

	bp_map_list = list_create(_bp_map_list_del);

	if (!getenv("DB2INSTANCE") || !getenv("VWSPATH")) {
		error("Missing DB2INSTANCE or VWSPATH env var."
			"Execute 'db2profile'");
		return -1;
	}
	
	if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
		error("rm_set_serial(): %d", rc);
		return -1;
	}
	
	if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
		error("rm_get_BGL(): %d", rc);
		return -1;
	}
	if ((rc = rm_get_data(bgl, RM_BPNum, &bp_num)) != STATUS_OK) {
		error("rm_get_data(RM_BPNum): %d", rc);
		bp_num = 0;
	}

	for (i=0; i<bp_num; i++) {

		if (i) {
			if ((rc = rm_get_data(bgl, RM_NextBP, &my_bp))
			    != STATUS_OK) {
				error("rm_get_data(RM_NextBP): %d", rc);
				break;
			}
		} else {
			if ((rc = rm_get_data(bgl, RM_FirstBP, &my_bp))
			    != STATUS_OK) {
				error("rm_get_data(RM_FirstBP): %d", rc);
				break;
			}
		}
		
		bp_map = (pa_bp_map_t *) xmalloc(sizeof(pa_bp_map_t));
		
		if ((rc = rm_get_data(my_bp, RM_BPID, &bp_id))
		    != STATUS_OK) {
			xfree(bp_map);
			error("rm_get_data(RM_BPID): %d", rc);
			continue;
		}
		
		if ((rc = rm_get_data(my_bp, RM_BPLoc, &bp_loc))
		    != STATUS_OK) {
			xfree(bp_map);
			error("rm_get_data(RM_BPLoc): %d", rc);
			continue;
		}
		
		bp_map->bp_id = strdup(bp_id);
		bp_map->coord[X] = bp_loc.X;
		bp_map->coord[Y] = bp_loc.Y;
		bp_map->coord[Z] = bp_loc.Z;
		
		list_push(bp_map_list, bp_map);
		
	}
	if ((rc = rm_free_BGL(bgl)) != STATUS_OK)
		error("rm_free_BGL(): %s", rc);

#endif
	return 1;
	
}

static void _new_pa_node(pa_node_t *pa_node, int *coord)
{
	int i,j;
	pa_node->used = false;
	
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		pa_node->coord[i] = coord[i];
		
		for(j=0;j<NUM_PORTS_PER_NODE;j++) {
			pa_node->axis_switch[i].int_wire[j].used = 0;	
			if(i!=X) {
				if(j==3 || j==4) 
					pa_node->axis_switch[i].int_wire[j].used = 1;	
			}
			pa_node->axis_switch[i].int_wire[j].port_tar = j;	
		}
	}
}

static void _create_pa_system(void)
{
	int x;
	int coord[PA_SYSTEM_DIMENSIONS];
				
#ifdef HAVE_BGL
	int y,z;
	pa_system_ptr->grid = (pa_node_t***) 
		xmalloc(sizeof(pa_node_t**) * DIM_SIZE[X]);
#else
	pa_system_ptr->grid = (pa_node_t*) 
		xmalloc(sizeof(pa_node_t) * DIM_SIZE[X]);
#endif
	for (x=0; x<DIM_SIZE[X]; x++) {
#ifdef HAVE_BGL
		pa_system_ptr->grid[x] = (pa_node_t**) 
			xmalloc(sizeof(pa_node_t*) * DIM_SIZE[Y]);
		for (y=0; y<DIM_SIZE[Y]; y++) {
			pa_system_ptr->grid[x][y] = (pa_node_t*) 
				xmalloc(sizeof(pa_node_t) * DIM_SIZE[Z]);
			for (z=0; z<DIM_SIZE[Z]; z++){
				coord[X] = x;
				coord[Y] = y;
				coord[Z] = z;
				_new_pa_node(&pa_system_ptr->grid[x][y][z], coord);
			}
		}
#else
		coord[X] = x;
		_new_pa_node(&pa_system_ptr->grid[x], coord);
#endif
	}
}

/** */
static void _delete_pa_system(void)
{
#ifdef HAVE_BGL
	int x=0;
	int y;
#endif
	if (!pa_system_ptr){
		return;
	}
	
	if(pa_system_ptr->grid) {
#ifdef HAVE_BGL
		for (x=0; x<DIM_SIZE[X]; x++) {
			for (y=0; y<DIM_SIZE[Y]; y++)
				xfree(pa_system_ptr->grid[x][y]);
			
			xfree(pa_system_ptr->grid[x]);
		}
#endif
		
		
		xfree(pa_system_ptr->grid);
	}
	xfree(pa_system_ptr);
}

/** 
 * algorithm for finding match
 */
static int _find_match(pa_request_t *pa_request, List results)
{
	int x=0;
	int *geometry = pa_request->geometry;
#ifdef HAVE_BGL
	int y=0, z=0;
	int start[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	int find[PA_SYSTEM_DIMENSIONS] = {0,0,0};
#else
	int find[PA_SYSTEM_DIMENSIONS] = {0};
#endif
	pa_node_t* pa_node = NULL;
	int found_one=0;
	char *name=NULL;

#ifdef HAVE_BGL
	if(geometry[X]>DIM_SIZE[X] 
	   || geometry[Y]>DIM_SIZE[Y]
	   || geometry[Z]>DIM_SIZE[Z])
		if(!_check_for_options(pa_request))
			return 0;
start_again:
	
	for (x=0; x<geometry[X]; x++) {
		for (y=0; y<geometry[Y]; y++) {			
			for (z=0; z<geometry[Z]; z++) {
				
				pa_node = &pa_system_ptr->grid[find[X]][find[Y]][find[Z]];
				if (!_node_used(pa_node,geometry)) {
					list_append(results, pa_node);
					find[Z]++;
					found_one=1;
				} else {
					if(found_one) {
						list_destroy(results);
						results = list_create(NULL);
						found_one=0;
					}
					if((DIM_SIZE[Z]-find[Z]-1)>=geometry[Z]) {
						find[Z]++;
						start[Z]=find[Z];
					} else {
						find[Z]=0;
						start[Z]=find[Z];
						if((DIM_SIZE[Y]-find[Y]-1)>=geometry[Y]) {
							find[Y]++;
							start[Y]=find[Y];
						} else {
							find[Y]=0;
							start[Y]=find[Y];						
							if ((DIM_SIZE[X]-find[X]-1)>=geometry[X]) {
								find[X]++;
								start[X]=find[X];
							} else {
								if(!_check_for_options(pa_request))
									return 0;
								else {
									find[X]=0;
									find[Y]=0;
									find[Z]=0;
									start[X]=0;
									start[Y]=0;
									start[Z]=0;
									goto start_again;
								}
							}
						}
					}
					goto start_again;
				}		
			}
			find[Z]=start[Z];
			if(y<(geometry[Y]-1)) {
				if(find[Y]<(DIM_SIZE[Y]-1)) {
					find[Y]++;
				} else {
					if(!_check_for_options(pa_request))
						return 0;
					else {
						find[X]=0;
						find[Y]=0;
						find[Z]=0;
						start[X]=0;
						start[Y]=0;
						start[Z]=0;
						goto start_again;
					}
				}
			}
		}
		find[Y]=start[Y];
		if(x<(geometry[X]-1)) {
			if(find[X]<(DIM_SIZE[X]-1)) {
				find[X]++;
			} else {
				if(!_check_for_options(pa_request))
					return 0;
				else {
					find[X]=0;
					find[Y]=0;
					find[Z]=0;
					start[X]=0;
					start[Y]=0;
					start[Z]=0;
					goto start_again;
				}
			}
		}
	}

	if(found_one) {
		if(pa_request->conn_type==TORUS)
			name = _set_internal_wires(results, pa_request->size, TORUS);
		else
			name = _set_internal_wires(results, pa_request->size, MESH);
		
		if(name!=NULL)
			pa_request->save_name = name;
		else
			return 0;
	} else {
		debug("couldn't find it 2");
		return 0;
	}

#else
start_again:
	
	for (x=0; x<geometry[X]; x++) {
		pa_node = &pa_system_ptr->grid[find[X]];
		if (!_node_used(pa_node,geometry)) {
			list_append(results, pa_node);
			find[X]++;
			found_one=1;
		} else {
			if(found_one) {
				list_destroy(results);
				results = list_create(NULL);
				found_one=0;
			}
			
			if ((DIM_SIZE[X]-find[X]-1)>=geometry[X]) {
				find[X]++;
			} else
				break;
			goto start_again;
		}		
	}
	
	if(found_one) {
		if(pa_request->conn_type==TORUS)
			name = _set_internal_wires(results, pa_request->size, TORUS);
		else
			name = _set_internal_wires(results, pa_request->size, MESH);
		
		if(name!=NULL)
			pa_request->save_name = name;
		else
			return 0;
	} else {
		debug("couldn't find it 2");
		return 0;
	}

#endif
	return 1;
}

/* bool _node_used(pa_node_t* pa_node, int geometry,  */
/*  		    int conn_type, bool force_contig, */
/* 		    dimension_t dim, int cur_node_id) */
static bool _node_used(pa_node_t* pa_node, int *geometry)
{
	int i=0;
	pa_switch_t* pa_switch = NULL;
	
	/* if we've used this node in another partition already */
	if (!pa_node || pa_node->used)
		return true;
	
	/* if we've used this nodes switches completely in another partition already */
	for(i=0;i<PA_SYSTEM_DIMENSIONS;i++) {
		if(geometry[i]>1) {
			pa_switch = &pa_node->axis_switch[i];
			if(pa_switch->int_wire[3].used && pa_switch->int_wire[5].used)
				return true;
		}
	}
		
	return false;

}


static void _switch_config(pa_node_t* source, pa_node_t* target, int dim, 
		int port_src, int port_tar)
{
	pa_switch_t* config = NULL, *config_tar = NULL;
	int i;

	if (!source || !target)
		return;
	
	config = &source->axis_switch[dim];
	config_tar = &target->axis_switch[dim];
	for(i=0;i<PA_SYSTEM_DIMENSIONS;i++) {
		/* Set the coord of the source target node to the target */
		config->ext_wire[port_src].node_tar[i] = target->coord[i];
	
		/* Set the coord of the target back to the source */
		config_tar->ext_wire[port_tar].node_tar[i] = source->coord[i];
	}

	/* Set the port of the source target node to the target */
	config->ext_wire[port_src].port_tar = port_tar;
	
	/* Set the port of the target back to the source */
	config_tar->ext_wire[port_tar].port_tar = port_src;
}

static void _set_external_wires(int dim, int count, pa_node_t* source, 
		pa_node_t* target_1, pa_node_t* target_2)
{
	_switch_config(source, source, dim, 0, 0);
	_switch_config(source, source, dim, 1, 1);
	if(dim!=X) {
		_switch_config(source, target_1, dim, 2, 5);			
		_switch_config(source, source, dim, 3, 3);			
		_switch_config(source, source, dim, 4, 4);			
		return;
	}

	if(count==0) {
		/* First Even Node */
		/* 4->3 of next */
		_switch_config(source, target_1, dim, 4, 3);
		/* 5->2 of next */
		_switch_config(source, target_1, dim, 5, 2);
		/* 2->5 of next even */
		_switch_config(source, target_2, dim, 2, 5);	

	} else if(!(count%2)) {
		if(count<DIM_SIZE[dim]-2) {
			/* Not Last Even Node */
			/* 3->4 of next */
			_switch_config(source, target_1, dim, 3, 4);
			/* 4->3 of next */
			_switch_config(source, target_1, dim, 4, 3);
			/* 2->5 of next even */
			_switch_config(source, target_2, dim, 2, 5);
			/* 5->2 of next even */
			_switch_config(source, target_2, dim, 5, 2);
			
		} else {
			/* Last Even Node */
			/* 3->4 of next */
			_switch_config(source, target_1, dim, 3, 4);
			/* 2->5 of previous */
			/********** fix me: on the full system this is needed ******/ 
			//_switch_config(source, target_1, dim, 2, 5);	
			/********** fix me: not this ******/ 
			_switch_config(source, target_1, dim, 4, 3);
		}
	} else {
		if(count<DIM_SIZE[dim]-2) {
			/* Not Last Odd Node */
			/* 5->2 of next odd */
			_switch_config(source, target_2, dim, 5, 2);
		} else {
			/* Last Odd Node */
			/* nothing */
		}	
	}	
}

static char *_set_internal_wires(List nodes, int size, int conn_type)
{
	pa_node_t* pa_node[size+1];
	int count=0, i, set=0;
	int *start = NULL;
	int *end = NULL;
	char *name = (char *) xmalloc(sizeof(char)*8);
	ListIterator itr;

	memset(name,0,8);		
	itr = list_iterator_create(nodes);
	while((pa_node[count] = (pa_node_t*) list_next(itr))) {
		count++;
	}
	list_iterator_destroy(itr);
		
	start = pa_node[0]->coord;
	end = pa_node[count-1]->coord;	
#ifdef HAVE_BGL
	sprintf(name, "%d%d%dx%d%d%d",start[X],start[Y],start[Z],end[X],end[Y],end[Z]);
#else
	sprintf(name, "%d-%d", start[X], end[X]);
#endif
	for(i=0;i<count;i++) {
		if(!pa_node[i]->used) {
			if(size!=1) 
				_configure_dims(pa_node[i]->coord, start, end, conn_type);
				
			pa_node[i]->used=1;
			pa_node[i]->conn_type=conn_type;
			if(pa_node[i]->letter == '.') {
				pa_node[i]->letter = letters[color_count%62];
				pa_node[i]->color = colors[color_count%6];
				
				set=1;
			}
		} else {
			error("AHHHHHHH I can't do it in _set_internal_wires");
			xfree(name);
			return NULL;
		}
	}

	if(conn_type == TORUS)
		for(i=0;i<count;i++) {
			_set_one_dim(start, end, pa_node[i]->coord);
		}

	if(set)
		color_count++;		

	return name;
}				

static int _find_one_hop(pa_switch_t *curr_switch, int source_port, 
		int *target, int *target2, int dim) 
{
	pa_switch_t *next_switch = NULL; 
	int port_tar;
	int target_port=0;
	int ports_to_try[2] = {3,5};
	int *node_tar = NULL;
	int i;

	if(!source_port) {
		target_port=1;
		ports_to_try[0] = 2;
		ports_to_try[1] = 4;
			
	}

	for(i=0;i<2;i++) {
		/* check to make sure it isn't used */
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			node_tar = curr_switch->ext_wire[ports_to_try[i]].node_tar;
#ifdef HAVE_BGL
			if((node_tar[X]==target[X] && 
			    node_tar[Y]==target[Y] && 
			    node_tar[Z]==target[Z]) ||
			   (node_tar[X]==target2[X] && 
			    node_tar[Y]==target2[Y] && 
			    node_tar[Z]==target2[Z])) {
				port_tar = curr_switch->ext_wire[ports_to_try[i]].port_tar;
				next_switch = &pa_system_ptr->
					grid[node_tar[X]][node_tar[Y]][node_tar[Z]].
					axis_switch[dim];
#else
			if((node_tar[X]==target[X]) || 
			   (node_tar[X]==target2[X])) {
				port_tar = curr_switch->ext_wire[ports_to_try[i]].port_tar;
				next_switch = &pa_system_ptr->
					grid[node_tar[X]].
					axis_switch[dim];
#endif
				if(!next_switch->int_wire[target_port].used) {
					curr_switch->int_wire[source_port].used = 1;
					curr_switch->int_wire[source_port].port_tar = 
						ports_to_try[i];
					curr_switch->int_wire[ports_to_try[i]].used = 1;
					curr_switch->int_wire[ports_to_try[i]].port_tar = 
						source_port;
					
					next_switch->int_wire[port_tar].used = 1;
					next_switch->int_wire[port_tar].port_tar = target_port;
					next_switch->int_wire[target_port].used = 1;
					next_switch->int_wire[target_port].port_tar = port_tar;
					return 1;
				}
			}		
		}
	}

	return 0;
}

static int _find_best_path(pa_switch_t *start, int source_port, int *target, 
		int *target2, int dim, int count) 
{
	pa_switch_t *next_switch = NULL; 
	pa_path_switch_t *path_add = 
		(pa_path_switch_t *) xmalloc(sizeof(pa_path_switch_t));
	pa_path_switch_t *path_switch = NULL;
	pa_path_switch_t *temp_switch = NULL;
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
#ifdef HAVE_BGL
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
#endif
	path_add->dim = dim;
	path_add->in = source_port;

	if(count>=best_count)
		return 0;

#ifdef HAVE_BGL
	if((node_tar[X]==target[X] && 
	    node_tar[Y]==target[Y] && 
	    node_tar[Z]==target[Z]) ||
	   (node_tar[X]==target2[X] && 
	    node_tar[Y]==target2[Y] && 
	    node_tar[Z]==target2[Z])) {
#else
	if(node_tar[X]==target[X] ||
	   node_tar[X]==target2[X]) {
#endif
		list_destroy(best_path);
		best_path = list_create(NULL);
		found = true;
		if((source_port%2))
			target_port=1;
		
		path_add->out = target_port;
		list_push(path, path_add);
		
		itr = list_iterator_create(path);
		while((path_switch = (pa_path_switch_t*) list_next(itr))){
		
			temp_switch = (pa_path_switch_t *) xmalloc(sizeof(pa_path_switch_t));
			 
			temp_switch->geometry[X] = path_switch->geometry[X];
#ifdef HAVE_BGL
			temp_switch->geometry[Y] = path_switch->geometry[Y];
			temp_switch->geometry[Z] = path_switch->geometry[Z];
#endif
			temp_switch->dim = path_switch->dim;
			temp_switch->in = path_switch->in;
			temp_switch->out = path_switch->out;
			list_append(best_path,temp_switch);
		}
		list_iterator_destroy(itr);
		best_count = count;
		return 1;
	} 
	
	if(source_port==0 || source_port==3 || source_port==5) {
		ports_to_try[0] = 2;
		ports_to_try[1] = 4;		
	}
	
	for(i=0;i<2;i++) {
		used=0;
		if(!start->int_wire[ports_to_try[i]].used) {
			itr = list_iterator_create(path);
			while((path_switch = (pa_path_switch_t*) list_next(itr))){
				
#ifdef HAVE_BGL
				if(((path_switch->geometry[X] == node_src[X]) && 
				    (path_switch->geometry[Y] == node_src[Y]) && 
				    (path_switch->geometry[Z] == node_tar[Z]))) {
#else
				if(path_switch->geometry[X] == node_src[X]) { 
#endif
					if( path_switch->out==ports_to_try[i]) {
						used = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr);
			
			if(!used) {
				port_tar = start->ext_wire[ports_to_try[i]].port_tar;
				node_tar = start->ext_wire[ports_to_try[i]].node_tar;
				
#ifdef HAVE_BGL
				next_switch = &pa_system_ptr->
					grid[node_tar[X]][node_tar[Y]][node_tar[Z]].
					axis_switch[dim];
#else
				next_switch = &pa_system_ptr->
					grid[node_tar[X]].
					axis_switch[dim];
#endif				
				
				count++;
				path_add->out = ports_to_try[i];
				list_push(path, path_add);
				_find_best_path(next_switch, port_tar, target, 
						target2, dim, count);
				while(list_pop(path) != path_add){
				} 
			}
		}
	}
	xfree(path_add);
	return 0;
}

static int _set_best_path(void)
{
	ListIterator itr;
	pa_path_switch_t *path_switch = NULL;
	pa_switch_t *curr_switch = NULL; 
	
	itr = list_iterator_create(best_path);
	while((path_switch = (pa_path_switch_t*) list_next(itr))) {
#ifdef HAVE_BGL
		curr_switch = &pa_system_ptr->
			grid
			[path_switch->geometry[X]]
			[path_switch->geometry[Y]]
			[path_switch->geometry[Z]].  
			axis_switch[path_switch->dim];
#else
		curr_switch = &pa_system_ptr->
			grid
			[path_switch->geometry[X]].
			axis_switch[path_switch->dim];
		
#endif
	
		curr_switch->int_wire[path_switch->in].used = 1;
		curr_switch->int_wire[path_switch->in].port_tar = path_switch->out;
		curr_switch->int_wire[path_switch->out].used = 1;
		curr_switch->int_wire[path_switch->out].port_tar = path_switch->in;
	}	
	best_count=BEST_COUNT_INIT;
	return 1;
}

static int _configure_dims(int *coord, int *start, int *end, int conn_type)
{
	pa_switch_t *curr_switch = NULL; 
	int dim;
#ifdef HAVE_BGL
	int target[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	int target2[PA_SYSTEM_DIMENSIONS] = {0,0,0};
#else
	int target[PA_SYSTEM_DIMENSIONS] = {0};
	int target2[PA_SYSTEM_DIMENSIONS] = {0};
#endif	
	/* set it up for the X axis */
	for(dim=0; dim<PA_SYSTEM_DIMENSIONS; dim++) {
		if(start[dim] == end[dim])
			continue;
#ifdef HAVE_BGL
		curr_switch = &pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].axis_switch[dim];

		target[X]=coord[X];	
		target2[X]=coord[X];
		target[Y]=coord[Y];
		target2[Y]=coord[Y];
		target[Z]=coord[Z];
		target2[Z]=coord[Z];
#else
		curr_switch = &pa_system_ptr->grid[coord[X]].axis_switch[dim];

		target[X]=coord[X];	
		target2[X]=coord[X];		
#endif
		
		if(dim==X) {
			if((coord[dim]+1)>end[dim]) {
				target[dim]=start[dim];
			} else {
				target[dim]=coord[dim]+1;
			}
			if((coord[dim]+2)>end[dim])
				target2[dim]=end[dim];
			else
				target2[dim]=coord[dim]+2;
			
		} else {
			if((coord[dim]+1)>end[dim])
				target[dim]=start[dim];
			else
				target[dim]=coord[dim]+1;
			if((coord[dim]-1)<start[dim])
				target2[dim]=end[dim];
			else
				target2[dim]=coord[dim]-1;
			
		}
		
		if(coord[dim]<(end[dim]-1)) {
			/* set it up for 0 */
			if(!curr_switch->int_wire[0].used) {
					if(!_find_one_hop(curr_switch, 0, target, target2, dim)) {
					_find_best_path(curr_switch, 0, target, target2, dim, 0);
					_set_best_path();
				}
			} 
			if((dim == X) || (conn_type == TORUS))
				if(!curr_switch->int_wire[1].used) { 
						if(!_find_one_hop(curr_switch, 1, target, target2, dim)) {
						_find_best_path(curr_switch, 1, target, target2, dim, 0);
						_set_best_path();
					} 
				}
			
			/*****************************/
			
		} else if((coord[dim]==(end[dim]-1))) {
			
			if((dim != X) || (conn_type == TORUS))
				if(!curr_switch->int_wire[0].used) {
					_find_best_path(curr_switch, 0, target, target, dim, 0);
					_set_best_path();
				}

			if(conn_type == TORUS)
				if(!curr_switch->int_wire[1].used) {
					_find_best_path(curr_switch, 1, target, target, dim, 0);	
					_set_best_path();
				} 
		} 
	}
	return 1;
	
}

static int _set_one_dim(int *start, int *end, int *coord)
{
	int dim;
	pa_switch_t *curr_switch = NULL; 
	
	for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) {
		if(start[dim]==end[dim]) {
#ifdef HAVE_BGL
			curr_switch = &pa_system_ptr->grid[coord[X]][coord[Y]]
				[coord[Z]].axis_switch[dim];
#else
			curr_switch = &pa_system_ptr->grid[coord[X]]
				.axis_switch[dim];
#endif
			if(!curr_switch->int_wire[0].used 
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

//#define BUILD_EXE
#ifdef BUILD_EXE
/** */
int main(int argc, char** argv)
{
	pa_request_t *request = (pa_request_t*) xmalloc(sizeof(pa_request_t)); 
	int *loc = NULL;
	List results;
//	List results2;
//	int i,j;
	DIM_SIZE[X]=4;
	DIM_SIZE[Y]=1;
	DIM_SIZE[Z]=1;
	pa_init(NULL);
	
	

	results = list_create(NULL);
	request->geometry[0] = 4;
	request->geometry[1] = 1;
	request->geometry[2] = 1;
	request->size = 4;
	request->conn_type = TORUS;
	new_pa_request(request);
	print_pa_request(request);
	allocate_part(request, results);

	/* results = list_create(NULL); */
/* 	request->geometry[0] = 5; */
/* 	request->geometry[1] = 1; */
/* 	request->geometry[2] = 1; */
/* 	request->size = -1; //atoi(argv[1]); */
/* 	request->conn_type = MESH; */
/* 	new_pa_request(request); */
/* 	print_pa_request(request); */
/* 	allocate_part(request, results); */

	int dim,j;
	int x,y,z;
	int startx=0;
	int starty=0;
	int startz=0;
	int endx=7;
	int endy=0;
	int endz=0;
	for(x=startx;x<=endx;x++) {
		for(y=starty;y<=endy;y++) {
			for(z=startz;z<=endz;z++) {
				info("Node %d%d%d Used = %d Letter = %c",
				       x,y,z,pa_system_ptr->grid[x][y][z].used,
				       pa_system_ptr->grid[x][y][z].letter);
				for(dim=0;dim<1;dim++) {
					info("Dim %d",dim);
					pa_switch_t *wire =
						&pa_system_ptr->
						grid[x][y][z].axis_switch[dim];
					for(j=0;j<6;j++)
						info("\t%d -> %d -> %d Used = %d",
						       j, wire->int_wire[j].port_tar,
						       wire->ext_wire[wire->int_wire[j].port_tar].port_tar,
						       wire->int_wire[j].used);
				}
			}
		}
	}
	/* list_destroy(results); */

/* 	pa_fini(); */

/* 	delete_pa_request(request); */
	
	return 0;
}


#endif
