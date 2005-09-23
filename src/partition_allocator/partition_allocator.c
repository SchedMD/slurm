/*****************************************************************************\
 *  partition_allocator.c - Assorted functions for layout of bglblocks, 
 *	 wiring, mapping for smap, etc.
 *  $Id$
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "src/partition_allocator/partition_allocator.h"

#ifdef HAVE_BGL_FILES
# include "src/plugins/select/bluegene/wrap_rm_api.h"
#endif

#define DEBUG_PA
#define BEST_COUNT_INIT 20

#ifdef HAVE_BGL
int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {0,0,0};
#else
int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {0};
#endif

bool _initialized = false;
bool _wires_initialized = false;
bool _bp_map_initialized = false;
bool have_db2 = false;

/* _pa_system is the "current" system that the structures will work
 *  on */
pa_system_t *pa_system_ptr = NULL;
List path = NULL;
List best_path = NULL;
int best_count;
int color_count = 0;
char letters[62];
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
static int _fill_in_coords(List results, List start_list, 
			   int *geometry, int conn_type);
/* */
static int _copy_the_path(pa_switch_t *curr_switch, pa_switch_t *mark_switch, 
			  int source, int dim);
/* */
static int _find_yz_path(pa_node_t *pa_node, int *first, 
			 int *geometry, int conn_type);
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
static int _reset_the_path(pa_switch_t *curr_switch, int source, 
			   int target, int dim);
/** */
static void _create_pa_system(void);
/* */
static void _delete_pa_system(void);
/* */
static void _delete_path_list(void *object);

/* find the first partition match in the system */
static int _find_match(pa_request_t* pa_request, List results);

static bool _node_used(pa_node_t* pa_node, int *geometry);

/* */
static void _switch_config(pa_node_t* source, pa_node_t* target, int dim, 
			   int port_src, int port_tar);
/* */
static int _set_external_wires(int dim, int count, pa_node_t* source, 
				pa_node_t* target);
/* */
static char *_set_internal_wires(List nodes, int size, int conn_type);
/* */
static int _find_x_path(List results, pa_node_t *pa_node, 
			int *start, int *first, 
			int *geometry, int found, int conn_type);
/* */
static int _find_x_path2(List results, pa_node_t *pa_node, 
			 int *start, int *first, 
			 int *geometry, int found, int conn_type);
/* */
static int _remove_node(List results, int *node_tar);
/* */
static int _find_next_free(pa_switch_t *curr_switch, int source_port, 
			   List nodes, int dim, int count);
/* */
static int _find_passthrough(pa_switch_t *curr_switch, int source_port, 
			     List nodes, int dim, 
			     int count, int highest_phys_x); 
/* */
static int _finish_torus(pa_switch_t *curr_switch, int source_port, 
			   List nodes, int dim, int count, int *start);
/* */
static int *_set_best_path();

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
	int messed_with = 0;
	
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
				error("new_pa_request Error, "
				      "request geometry is invalid %d", 
				      geo[i]); 
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
			_append_geo(geo, 
				    pa_request->elongate_geos,
				    pa_request->rotate);
			goto endit;
		}
		
		if(pa_request->size<=DIM_SIZE[Y]) {
			geo[X] = 1;
			geo[Y] = pa_request->size;
			geo[Z] = 1;
			sz=pa_request->size;
			_append_geo(geo, 
				    pa_request->elongate_geos, 
				    pa_request->rotate);
		}
		
		i = pa_request->size/4;
		if(!(pa_request->size%2)
		   && i <= DIM_SIZE[Y]
		   && i <= DIM_SIZE[Z]
		   && i*i == pa_request->size) {
			geo[X] = 1;
			geo[Y] = i;
			geo[Z] = i;
			sz=pa_request->size;
			_append_geo(geo,
				    pa_request->elongate_geos,
				    pa_request->rotate);
		}
	
		if(pa_request->size>total_sz || pa_request->size<1) {
			return 0;			
		}
		sz = pa_request->size % (DIM_SIZE[Y] * DIM_SIZE[Z]);
		if(!sz) {
		      i = pa_request->size / (DIM_SIZE[Y] * DIM_SIZE[Z]);
		      geo[X] = i;
		      geo[Y] = DIM_SIZE[Y];
		      geo[Z] = DIM_SIZE[Z];
		      sz=pa_request->size;
		      _append_geo(geo,
				  pa_request->elongate_geos,
				  pa_request->rotate);
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
					/* go through each number to see if 
					   the size is divisable by a smaller 
					   number that is 
					   good in the other dims. */
					if (!(size2%i2) && !checked[i2]) {
						size2 /= i2;
									
						if(i==0)
							checked[i2]=1;
							
						if(i2<DIM_SIZE[i]) 
							geo[i] = i2;
						else {
							goto tryagain;
						}
						if((i2-1)!=1 && 
						   i!=(PA_SYSTEM_DIMENSIONS-1))
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
			_append_geo(pa_request->geometry, 
				    pa_request->elongate_geos, 
				    pa_request->rotate);

		}
		if((geo[X]*geo[Z]) <= DIM_SIZE[Y]) {
			pa_request->geometry[X] = 1;
			pa_request->geometry[Y] = geo[Y];
			pa_request->geometry[Z] = geo[X] * geo[Z];	
			_append_geo(pa_request->geometry, 
				    pa_request->elongate_geos, 
				    pa_request->rotate);		
	
		}
		if((geo[X]/2) <= DIM_SIZE[Y]) {
			if(geo[Y] == 1) {
				pa_request->geometry[Y] = geo[X]/2;
				messed_with = 1;
			} else  
				pa_request->geometry[Y] = geo[Y];
			if(!messed_with && geo[Z] == 1) {
				messed_with = 1;
				pa_request->geometry[Z] = geo[X]/2;
			} else
				pa_request->geometry[Z] = geo[Z];
			if(messed_with) {
				messed_with = 0;
				pa_request->geometry[X] = 2;
				_append_geo(pa_request->geometry, 
					    pa_request->elongate_geos, 
					    pa_request->rotate);
			}
		}
		if(geo[X] == DIM_SIZE[X]
		   && (geo[Y] < DIM_SIZE[Y] 
		       || geo[Z] < DIM_SIZE[Z])) {
			if(DIM_SIZE[Y]<DIM_SIZE[Z]) {
				i = DIM_SIZE[Y];
				DIM_SIZE[Y] = DIM_SIZE[Z];
				DIM_SIZE[Z] = i;
			}
			pa_request->geometry[X] = geo[X];
			pa_request->geometry[Y] = geo[Y];
			pa_request->geometry[Z] = geo[Z];
			if(pa_request->geometry[Y] < DIM_SIZE[Y]) {
				i = (DIM_SIZE[Y] - pa_request->geometry[Y]);
				pa_request->geometry[Y] +=i;
			}
			if(pa_request->geometry[Z] < DIM_SIZE[Z]) {
				i = (DIM_SIZE[Z] - pa_request->geometry[Z]);
				pa_request->geometry[Z] +=i;
			}
			for(i = DIM_SIZE[X]; i>0; i--) {
				pa_request->geometry[X]--;
				i2 = (pa_request->geometry[X]
				      * pa_request->geometry[Y]
				      * pa_request->geometry[Z]);
				if(i2 < pa_request->size) {
					pa_request->geometry[X]++;
					messed_with = 1;
					break;
				}					
			}			
			if(messed_with) {
				messed_with = 0;
				_append_geo(pa_request->geometry, 
					    pa_request->elongate_geos, 
					    pa_request->rotate);
			}
		}
		
		_append_geo(geo, 
			    pa_request->elongate_geos, 
			    pa_request->rotate);
	
		/* see if We can find a cube or square root of the 
		   size to make an easy cube */
		for(i=0;i<PA_SYSTEM_DIMENSIONS-1;i++) {
			sz = powf((float)pa_request->size,
				  (float)1/(PA_SYSTEM_DIMENSIONS-i));
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
				
			_append_geo(geo, 
				    pa_request->elongate_geos, 
				    pa_request->rotate);
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
				error("new_pa_request Error, "
				      "request geometry is invalid %d", 
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
	debug("        size:\t%d", pa_request->size);
	debug("   conn_type:\t%d", pa_request->conn_type);
	debug("      rotate:\t%d", pa_request->rotate);
	debug("    elongate:\t%d", pa_request->elongate);
	debug("force contig:\t%d", pa_request->force_contig);
}

/**
 * Search for local DB2 library
 */
static void _db2_check(void)
{
	void *handle;

	handle = dlopen("libdb2.so", RTLD_LAZY);
	if (!handle) {
		debug("can not open libdb2.so");
		return;
	}

	if (dlsym(handle, "SQLAllocHandle") == NULL)
		debug("SQLAllocHandle not found in libdb2.so");
	else
		have_db2 = true;

	dlclose(handle);
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
	int start, temp;
	char *numeric = NULL;
	int x,y,z;

#ifdef HAVE_BGL
	int i;
#endif

#ifdef HAVE_BGL_FILES
	rm_BGL_t *bgl = NULL;
	rm_size3D_t bp_size;
	int rc = 0;
#endif		
	/* We only need to initialize once, so return if already done so. */
	if (_initialized){
		return;
	}
	y = 65;
	for (x = 0; x < 62; x++) {
		if (y == 91)
			y = 97;
		else if(y == 123)
			y = 48;
		else if(y == 58)
			y = 65;
		letters[x] = y;
		y++;
	}

	z=1;
	for (x = 0; x < 6; x++) {
		if(z == 4)
			z++;
		colors[x] = z;				
		z++;
	}
	_db2_check();
		
	best_count=BEST_COUNT_INIT;
						
	pa_system_ptr = (pa_system_t *) xmalloc(sizeof(pa_system_t));
	
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord = 1;
	pa_system_ptr->num_of_proc = 0;
	pa_system_ptr->resize_screen = 0;
	
	if(node_info_ptr!=NULL) {
#ifdef HAVE_BGL
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
			temp = start / 100;
			if (DIM_SIZE[X] < temp)
				DIM_SIZE[X] = temp;
			temp = (start / 10) % 10;
			if (DIM_SIZE[Y] < temp)
				DIM_SIZE[Y] = temp;
			temp = start % 10;
			if (DIM_SIZE[Z] < temp)
				DIM_SIZE[Z] = temp;
		}
		DIM_SIZE[X]++;
		DIM_SIZE[Y]++;
		DIM_SIZE[Z]++;
#else
		DIM_SIZE[X] = node_info_ptr->record_count;
#endif
		pa_system_ptr->num_of_proc = node_info_ptr->record_count;
        } 

#ifdef HAVE_BGL_FILES
	if (have_db2
	&&  (DIM_SIZE[X]==0) && (DIM_SIZE[X]==0) && (DIM_SIZE[X]==0)) {
		if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
			error("rm_set_serial(%s): %d", BGL_SERIAL, rc);
			return;
		}
		if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
			error("rm_get_BGL(): %d", rc);
			return;
		}
		
		if ((bgl != NULL)
		&&  ((rc = rm_get_data(bgl, RM_Msize, &bp_size)) 
		     == STATUS_OK)) {
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
	if (DIM_SIZE[X]==0) {
		debug("Setting default system dimensions");
		DIM_SIZE[X]=100;
	}	
#endif
	
	if(!pa_system_ptr->num_of_proc)
		pa_system_ptr->num_of_proc = 
			DIM_SIZE[X] 
#ifdef HAVE_BGL
			* DIM_SIZE[Y] 
			* DIM_SIZE[Z]
#endif 
			;

	_create_pa_system();
	
	init_grid(node_info_ptr);
	
#ifndef HAVE_BGL_FILES
	_create_config_even(pa_system_ptr->grid);
#endif
	path = list_create(_delete_path_list);
	best_path = list_create(_delete_path_list);

	_initialized = true;
}

extern void init_wires()
{
	int x, y, z, i;
	pa_node_t *source = NULL;
	if(_wires_initialized)
		return;

	for(x=0;x<DIM_SIZE[X];x++) {
		for(y=0;y<DIM_SIZE[Y];y++) {
			for(z=0;z<DIM_SIZE[Z];z++) {
#ifdef HAVE_BGL
				source = &pa_system_ptr->grid[x][y][z];
#else
				source = &pa_system_ptr->grid[x];
#endif
				for(i=0; i<6; i++) {
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
#ifdef HAVE_BGL_FILES	
	   _set_external_wires(0,0,NULL,NULL);
	   if(!bp_map_list) {
		if(set_bp_map() == -1)
			return;
	   }
#endif
	   _wires_initialized = true;
	return;
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
	debug("pa_set_node_down: node to set down: [%d%d%d]", 
	      pa_node->coord[X], pa_node->coord[Y], pa_node->coord[Z]); 
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
		error("allocate_part Error, configuration not initialized, "
		      "call init_configuration first");
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
	ListIterator itr;
		
	itr = list_iterator_create(nodes);
	while((pa_node = (pa_node_t*) list_next(itr)) != NULL) {
		pa_node->used = false;
		pa_node->color = 7;
		pa_node->letter = '.';
		for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) {		
			curr_switch = &pa_node->axis_switch[dim];
			if(curr_switch->int_wire[0].used) {
				_reset_the_path(curr_switch, 0, 1, dim);
			}
		}
	}
	list_iterator_destroy(itr);
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
	/* int dim; */
/* 	pa_node_t* pa_node = NULL; */
/* 	pa_switch_t *curr_switch = NULL;  */
/* 	int size=0; */
/* 	char *name = NULL; */
/* 	ListIterator results_i;	 */	

	return SLURM_ERROR;
	/* results_i = list_iterator_create(nodes); */
/* 	while ((pa_node = list_next(results_i)) != NULL) { */
/* 		pa_node->used = false; */
		
/* 		for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) { */
/* 			curr_switch = &pa_node->axis_switch[dim]; */
/* 			if(curr_switch->int_wire[0].used) { */
/* 				_reset_the_path(curr_switch, 0, 1, dim); */
/* 			} */
/* 		} */
/* 		size++; */
/* 	} */
/* 	list_iterator_destroy(results_i); */
/* 	if((name = _set_internal_wires(nodes, size, conn_type)) == NULL) */
/* 		return SLURM_ERROR; */
/* 	else { */
/* 		xfree(name); */
/* 		return SLURM_SUCCESS; */
/* 	} */
}

/** 
 * After a partition is deleted or altered following allocations must
 * be redone to make sure correct path will be used in the real system
 *
 */
extern int redo_part(List nodes, int *geo, int conn_type, int new_count)
{
       	pa_node_t* pa_node;
	char *name = NULL;
	ListIterator itr;		

	itr = list_iterator_create(nodes);
	pa_node = list_next(itr);
	list_iterator_destroy(itr);

	remove_part(nodes, new_count);
	list_destroy(nodes);
	nodes = list_create(NULL);
	
	name = set_bgl_part(nodes, pa_node->coord, geo, conn_type);
	if(!name)
		return SLURM_ERROR;
	else {
		xfree(name);
		return SLURM_SUCCESS;
	}
}

extern char *set_bgl_part(List results, int *start, 
			  int *geometry, int conn_type)
{
	char *name = NULL;
	pa_node_t* pa_node = NULL;
       	int size = 0;
	int send_results = 0;
	List start_list = NULL;
	ListIterator itr;
	int found = 0;

	if(!results)
		results = list_create(NULL);
	else
		send_results = 1;
	start_list = list_create(NULL);
#ifdef HAVE_BGL
	if(start[X]>=DIM_SIZE[X] 
	   || start[Y]>=DIM_SIZE[Y]
	   || start[Z]>=DIM_SIZE[Z])
		return NULL;
	size = geometry[X] * geometry[Y] * geometry[Z];
	pa_node = &pa_system_ptr->
		grid[start[X]][start[Y]][start[Z]];
#else
	if(start[X]>=DIM_SIZE[X])
		return NULL;
	size = geometry[X];
	pa_node = &pa_system_ptr->
			grid[start[X]];	
#endif

	if(!pa_node)
		return NULL;
	
#ifdef HAVE_BGL
	debug2("starting at %d%d%d",pa_node->coord[X],
	       pa_node->coord[Y],pa_node->coord[Z]);
#else
	debug2("starting at %d",pa_node->coord[X]);
#endif
	
	list_append(results, pa_node);
	found = _find_x_path(results, pa_node,
			     pa_node->coord, 
			     pa_node->coord, 
			     geometry, 
			     1,
			     conn_type);

	if(!found) {
		debug("trying less efficient code");
		remove_part(results, color_count);
		list_destroy(results);
		results = list_create(NULL);
		list_append(results, pa_node);
		found = _find_x_path2(results, pa_node,
				      pa_node->coord,
				      pa_node->coord,
				      geometry,
				      1,
				      conn_type);
	}
	if(found) {
#ifdef HAVE_BGL
		itr = list_iterator_create(results);
		while((pa_node = (pa_node_t*) list_next(itr))) {
			list_append(start_list, pa_node);
		}
		list_iterator_destroy(itr);
		
		if(!_fill_in_coords(results, 
				    start_list, 
				    geometry, 
				    conn_type))			
			return NULL;
#endif		
	} else {
		return NULL;
	}

	name = _set_internal_wires(results,
				   size,
				   conn_type);
	if(!send_results)
		list_destroy(results);
	if(name!=NULL) {
		debug2("name = %s", name);
	} else {
		debug2("can't allocte");
		xfree(name);
		return NULL;
	}

	return name;
	
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
				_new_pa_node(&pa_system_ptr->grid[x][y][z], 
					     coord);
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
					node_ptr = 
						&node_info_ptr->node_array[i];
					node_base_state = 
						(node_ptr->node_state) 
						& (~NODE_STATE_NO_RESPOND);
					pa_system_ptr->grid[x][y][z].color = 7;
					if ((node_base_state 
					     == NODE_STATE_DOWN) || 
					    (node_base_state 
					     == NODE_STATE_DRAINED) || 
					    (node_base_state 
					     == NODE_STATE_DRAINING)) {
						pa_system_ptr->
							grid[x][y][z].color 
							= 0;
						pa_system_ptr->
							grid[x][y][z].letter 
							= '#';
						if(_initialized) {
							pa_set_node_down(
							&pa_system_ptr->
							grid[x][y][z]);
						}
					} else {
						pa_system_ptr->grid[x][y][z].
							color = 7;
						pa_system_ptr->grid[x][y][z].
							letter = '.';
					}
					pa_system_ptr->grid[x][y][z].state 
						= node_ptr->node_state;
				} else {
					pa_system_ptr->grid[x][y][z].color = 7;
					pa_system_ptr->grid[x][y][z].letter 
						= '.';
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
					pa_set_node_down(
						&pa_system_ptr->grid[x]);
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
	
	if(!bp_map_list) {
		if(set_bp_map() == -1)
			return NULL;
	}
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
	int len = strlen(xyz);
	len -= 3;
	if(len<0)
		return NULL;
	number = atoi(&xyz[X]+len);
	coord[X] = number / 100;
	coord[Y] = (number % 100) / 10;
	coord[Z] = (number % 10);
	if(!bp_map_list) {
		if(set_bp_map() == -1)
			return NULL;
	}
	
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
		xfree(bp_map->bp_id);
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
		debug2("Rotating! %d",pa_request->rotate_count);
		
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
		debug2("Elongating! %d",pa_request->elongate_count);
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
		debug3("adding geo %d%d%d",geo[X],geo[Y],geo[Z]);
		list_append(geos, geo);
	}
	return 1;
}

static int _fill_in_coords(List results, List start_list,
			    int *geometry, int conn_type)
{
	pa_node_t *pa_node = NULL;
	pa_node_t *check_node = NULL;
	int rc = 1;
	ListIterator itr = NULL;
	int y=0, z=0, j;
	pa_switch_t *curr_switch = NULL; 
	pa_switch_t *next_switch = NULL; 
	
	if(!start_list)
		return 0;
	itr = list_iterator_create(start_list);
	while(check_node = (pa_node_t*) list_next(itr)) {		
		curr_switch = &check_node->axis_switch[X];
	
		for(y=0; y<geometry[Y]; y++) {
			if((check_node->coord[Y]+y) 
			   >= DIM_SIZE[Y]) {
				rc = 0;
				goto failed;
			}
			for(z=0; z<geometry[Z]; z++) {
				if((check_node->coord[Z]+z) 
				   >= DIM_SIZE[Z]) {
					rc = 0;
					goto failed;
				}
				pa_node = &pa_system_ptr->grid
					[check_node->coord[X]]
					[check_node->coord[Y]+y]
					[check_node->coord[Z]+z];
				if(pa_node->coord[Y] 
				   == check_node->coord[Y]
				   && pa_node->coord[Z] 
				   == check_node->coord[Z])
					continue;
				if (!_node_used(pa_node,geometry)) {
					debug2("here Adding %d%d%d",
					       pa_node->coord[X],
					       pa_node->coord[Y],
					       pa_node->coord[Z]);
					list_append(results, pa_node);
					next_switch = &pa_node->axis_switch[X];
					_copy_the_path(curr_switch, 
						       next_switch, 
						       pa_node->coord[X],
						       0);
				} else {
					rc = 0;
					goto failed;
				}
			}
		}
		
	}
	list_iterator_destroy(itr);
	itr = list_iterator_create(start_list);
	check_node = (pa_node_t*) list_next(itr);
	list_iterator_destroy(itr);
	
	itr = list_iterator_create(results);
	while(pa_node = (pa_node_t*) list_next(itr)) {	
		if(!_find_yz_path(pa_node, 
				  check_node->coord, 
				  geometry, 
				  conn_type)){
			rc = 0;
			goto failed;
		}
	}
failed:
	list_iterator_destroy(itr);				
				
	return rc;
}

static int _copy_the_path(pa_switch_t *curr_switch, pa_switch_t *mark_switch, 
			  int start, int source)
{
	int *node_tar;
	int *mark_node_tar;
	int *node_curr;
	int port_tar, port_tar1;
	pa_switch_t *next_switch = NULL; 
	pa_switch_t *next_mark_switch = NULL; 
	/*set the switch to not be used */
		
	mark_switch->int_wire[source].used = 
		curr_switch->int_wire[source].used;
	mark_switch->int_wire[source].port_tar = 
		curr_switch->int_wire[source].port_tar;

	port_tar = curr_switch->int_wire[source].port_tar;
	
	mark_switch->int_wire[port_tar].used = 
		curr_switch->int_wire[port_tar].used;
	mark_switch->int_wire[port_tar].port_tar = 
		curr_switch->int_wire[port_tar].port_tar;
	port_tar1 = port_tar;
	
	/* follow the path */
	node_curr = curr_switch->ext_wire[0].node_tar;
	if(port_tar == 1) {
		mark_switch->int_wire[1].used = 
			curr_switch->int_wire[1].used;
		mark_switch->int_wire[1].port_tar = 
			curr_switch->int_wire[1].port_tar;
		return 1;
	}

	node_tar = curr_switch->ext_wire[port_tar].node_tar;
	mark_node_tar = mark_switch->ext_wire[port_tar].node_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	
	if(node_curr[X] == node_tar[X]
	   && node_curr[Y] == node_tar[Y]
	   && node_curr[Z] == node_tar[Z]) {
		debug2("something bad happened!!");
		return 0;
	}
	next_switch = &pa_system_ptr->
		grid[node_tar[X]]
#ifdef HAVE_BGL
		[node_tar[Y]]
		[node_tar[Z]]
#endif
		.axis_switch[X];
	next_mark_switch = &pa_system_ptr->
		grid[mark_node_tar[X]]
#ifdef HAVE_BGL
		[mark_node_tar[Y]]
		[mark_node_tar[Z]]
#endif
		.axis_switch[X];

	_copy_the_path(next_switch, next_mark_switch, start, port_tar);
	return 1;
}

static int _find_yz_path(pa_node_t *pa_node, int *first, 
			 int *geometry, int conn_type)
{
	pa_node_t *next_node = NULL;
	int *node_tar = NULL;
	pa_switch_t *dim_curr_switch = NULL; 
	pa_switch_t *dim_next_switch = NULL; 
	int i2;
	int count = 0;

	for(i2=1;i2<=2;i2++) {
		if(geometry[i2] > 1) {
			debug2("%d node %d%d%d"
			       " port 2 -> ",
			       i2,
			       pa_node->coord[X],
			       pa_node->coord[Y],
			       pa_node->coord[Z]);
							       
			dim_curr_switch = 
				&pa_node->
				axis_switch[i2];
			if(dim_curr_switch->int_wire[2].used) {
				debug2("returning here");
				return 0;
			}
							
			node_tar = dim_curr_switch->
				ext_wire[2].node_tar;
							
			next_node = &pa_system_ptr->
				grid[node_tar[X]][node_tar[Y]][node_tar[Z]];
			dim_next_switch = &next_node->axis_switch[i2];
			debug2("%d%d%d port 5",
			       next_node->coord[X],
			       next_node->coord[Y],
			       next_node->coord[Z]);
							  
			if(dim_next_switch->int_wire[5].used) {
				debug2("returning here 2");
				return 0;
			}
			debug3("%d %d %d %d",i2, node_tar[i2],
			       first[i2], geometry[i2]);
			if(node_tar[i2] < first[i2])
				count = DIM_SIZE[i2]-first[i2]+node_tar[i2];
			else
				count = node_tar[i2]+first[i2];
			if((count) == (geometry[i2])) {
				debug3("found end of me %d%d%d",
				       node_tar[X],
				       node_tar[Y],
				       node_tar[Z]);
				if(conn_type == TORUS) {
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
					dim_curr_switch = dim_next_switch;
									
					while(node_tar[i2] != first[i2]) {
						debug2("on dim %d at %d "
						       "looking for %d",
						       i2,
						       node_tar[i2],
						       first[i2]);
						
						if(dim_curr_switch->
						   int_wire[2].used) {
							debug2("returning "
							       "here 3");
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
						
						
						node_tar = dim_curr_switch->
							ext_wire[2].node_tar;
						next_node = &pa_system_ptr->
							grid
							[node_tar[X]]
							[node_tar[Y]]
							[node_tar[Z]];
						dim_curr_switch = 
							&next_node->
							axis_switch[i2];
					}
									
					debug2("back to first on dim %d "
					       "at %d looking for %d",
					       i2,
					       node_tar[i2],
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
								
			} else {
				if(conn_type == TORUS || 
				   (conn_type == MESH && 
				    (node_tar[i2] != first[i2]))) {
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
			}
		}
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
	pa_node_t *source = NULL, *target = NULL;

#ifdef HAVE_BGL
	int y,z;
	init_wires();

	for(x=0;x<DIM_SIZE[X];x++) {
		for(y=0;y<DIM_SIZE[Y];y++) {
			for(z=0;z<DIM_SIZE[Z];z++) {
				source = &grid[x][y][z];
				
				if(x<(DIM_SIZE[X]-1)) {
					target = &grid[x+1][y][z];
				} else
					target = &grid[0][y][z];

				_set_external_wires(X, x, source, 
						    target);
				
				if(y<(DIM_SIZE[Y]-1)) 
					target = &grid[x][y+1][z];
				else 
					target = &grid[x][0][z];
				
				_set_external_wires(Y, y, source, 
						    target);
				if(z<(DIM_SIZE[Z]-1)) 
					target = &grid[x][y][z+1];
				else 
					target = &grid[x][y][0];
				
				_set_external_wires(Z, z, source, 
						    target);
			}
		}
	}
#else
	for(x=0;x<DIM_SIZE[X];x++) {
		source = &grid[x];
				
		target = &grid[x+1];
		
		_set_external_wires(X, x, source, 
				    target);
	}
#endif
	return 1;
}

static int _reset_the_path(pa_switch_t *curr_switch, int source, 
			   int target, int dim)
{
	int *node_tar;
	int *node_curr;
	int port_tar, port_tar1;
	pa_switch_t *next_switch = NULL; 
	/*set the switch to not be used */
		
	curr_switch->int_wire[source].used = 0;
	port_tar = curr_switch->int_wire[source].port_tar;
	port_tar1 = port_tar;
	curr_switch->int_wire[source].port_tar = source;
	curr_switch->int_wire[port_tar].used = 0;
	curr_switch->int_wire[port_tar].port_tar = port_tar;
	if(port_tar==target) {
		return 1;
	}
	/* follow the path */
	node_curr = curr_switch->ext_wire[0].node_tar;
	node_tar = curr_switch->ext_wire[port_tar].node_tar;
	port_tar = curr_switch->ext_wire[port_tar].port_tar;
	debug2("from %d%d%d %d %d -> %d%d%d %d",
	       node_curr[X],
	       node_curr[Y],
	       node_curr[Z],
	       source,
	       port_tar1,
	       node_tar[X],
	       node_tar[Y],
	       node_tar[Z],
	       port_tar);
	if(node_curr[X] == node_tar[X]
	   && node_curr[Y] == node_tar[Y]
	   && node_curr[Z] == node_tar[Z]) {
		debug2("%d something bad happened!!", dim);
		return 0;
	}
	next_switch = &pa_system_ptr->
		grid[node_tar[X]]
#ifdef HAVE_BGL
		[node_tar[Y]]
		[node_tar[Z]]
#endif
		.axis_switch[dim];

	_reset_the_path(next_switch, port_tar, target, dim);
	return 1;
}

int _port_enum(int port)
{
	switch(port) {
	case 6:
		return 0;
		break;
	case 7:
		return 1;
		break;
	case 8:
		return 2;
		break;
	case 9:
		return 3;
		break;
	case 10:
		return 4;
		break;
	case 11:
		return 5;
		break;
	default:
		return -1;
	}
}

/** */
extern int set_bp_map(void)
{
#ifdef HAVE_BGL_FILES
	static rm_BGL_t *bgl = NULL;
	int rc;
	rm_BP_t *my_bp = NULL;
	pa_bp_map_t *bp_map = NULL;
	int bp_num, i, j;
	char *bp_id = NULL;
	rm_location_t bp_loc;
	rm_wire_t *my_wire = NULL;
	rm_port_t *my_port = NULL;
	int number = 0;

	if(_bp_map_initialized)
		return 1;

	bp_map_list = list_create(_bp_map_list_del);

	if (!have_db2) {
		error("Can't access DB2 library, run from service node");
		return -1;
	}

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

		if(!bp_id) {
			error("No BP ID was returned from database");
			continue;
		}
			
		if ((rc = rm_get_data(my_bp, RM_BPLoc, &bp_loc))
		    != STATUS_OK) {
			xfree(bp_map);
			error("rm_get_data(RM_BPLoc): %d", rc);
			continue;
		}
		
		bp_map->bp_id = xstrdup(bp_id);
		bp_map->coord[X] = bp_loc.X;
		bp_map->coord[Y] = bp_loc.Y;
		bp_map->coord[Z] = bp_loc.Z;
		number = atoi(bp_id+1);		
		if(DIM_SIZE[X] > bp_loc.X
		   && DIM_SIZE[Y] > bp_loc.Y
		   && DIM_SIZE[Z] > bp_loc.Z)
			pa_system_ptr->grid
				[bp_loc.X]
				[bp_loc.Y]
				[bp_loc.Z].phys_x = number / 100;
		
		list_push(bp_map_list, bp_map);
		
		free(bp_id);		
	}

	if ((rc = rm_free_BGL(bgl)) != STATUS_OK)
		error("rm_free_BGL(): %s", rc);	
	
#endif
	_bp_map_initialized = true;
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
					pa_node->axis_switch[i].int_wire[j].
						used = 1;	
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
				_new_pa_node(&pa_system_ptr->grid[x][y][z], 
					     coord);
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

static void _delete_path_list(void *object)
{
	pa_path_switch_t *path_switch = (pa_path_switch_t *)object;

	if (path_switch) {
		xfree(path_switch);
	}
	return;
}

/** 
 * algorithm for finding match
 */
static int _find_match(pa_request_t *pa_request, List results)
{
	int x=0;
#ifdef HAVE_BGL
	int start[PA_SYSTEM_DIMENSIONS] = {0,0,0};
#else
	int start[PA_SYSTEM_DIMENSIONS] = {0};
#endif
	pa_node_t *pa_node = NULL;
	char *name=NULL;

#ifdef HAVE_BGL
	if(pa_request->geometry[X]>DIM_SIZE[X] 
	   || pa_request->geometry[Y]>DIM_SIZE[Y]
	   || pa_request->geometry[Z]>DIM_SIZE[Z])
		if(!_check_for_options(pa_request))
			return 0;
#endif

start_again:
	for (x=0; x<DIM_SIZE[X]; x++) {	
		debug3("finding %d%d%d try %d",
		       pa_request->geometry[X],
		       pa_request->geometry[Y],
		       pa_request->geometry[Z],
		       x);
	new_node:
		debug("starting at %d%d%d",
		     start[X],
		     start[Y],
		     start[Z]);
					
		pa_node = &pa_system_ptr->
			grid[start[X]]
#ifdef HAVE_BGL
			[start[Y]]
			[start[Z]]
#endif
			;

		if (!_node_used(pa_node, pa_request->geometry)) {
			name = set_bgl_part(results,
					    start, 
					    pa_request->geometry, 
					    pa_request->conn_type);
			if(name) {
				pa_request->save_name = xstrdup(name);
				xfree(name);
				return 1;
			}
			//exit(0);
			debug("trying something else");
			remove_part(results, color_count);
			list_destroy(results);
			results = list_create(NULL);
		}
#ifdef HAVE_BGL
		
		if((DIM_SIZE[Z]-start[Z]-1)
		   >= pa_request->geometry[Z])
			start[Z]++;
		else {
			start[Z] = 0;
			if((DIM_SIZE[Y]-start[Y]-1)
			   >= pa_request->geometry[Y])
				start[Y]++;
			else {
				start[Y] = 0;
				if ((DIM_SIZE[X]-start[X]-1)
				    >= pa_request->geometry[X])
					start[X]++;
				else {
					if(!_check_for_options(pa_request))
						return 0;
					else {
						start[X]=0;
						start[Y]=0;
						start[Z]=0;
						goto start_again;
					}
				}
			}
		}
		goto new_node;
#endif
	}							
	
	error("can't allocate");
	
	return 0;
}

/* bool _node_used(pa_node_t* pa_node, int geometry,  */
static bool _node_used(pa_node_t* pa_node, int *geometry)
{
	int i=0;
	pa_switch_t* pa_switch = NULL;
	
	/* if we've used this node in another partition already */
	if (!pa_node || pa_node->used) {
		debug3("node used");
		return true;
	}
	/* if we've used this nodes switches completely in another 
	   partition already */
	for(i=0;i<1;i++) {
		if(geometry[i]>1) {
			pa_switch = &pa_node->axis_switch[i];
			
			if(pa_switch->int_wire[3].used 
			   && pa_switch->int_wire[5].used) {
				debug3("switch in use dim %d!",i);
				return true;
			}
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

static int _set_external_wires(int dim, int count, pa_node_t* source, 
			       pa_node_t* target)
{
#ifdef HAVE_BGL_FILES
	rm_BGL_t *bgl = NULL;
	int rc;
	int i;
	rm_wire_t *my_wire = NULL;
	rm_port_t *my_port = NULL;
	char *wire_id = NULL;
	char from_node[5];
	char to_node[5];
	int from_port, to_port;
	int wire_num;
	int *coord;

	if (!have_db2) {
		error("Can't access DB2 library, run from service node");
		return -1;
	}
	if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
		error("rm_set_serial(%s): %d", BGL_SERIAL, rc);
		return -1;
	}
	if ((rc = rm_get_BGL(&bgl)) != STATUS_OK) {
		error("rm_get_BGL(): %d", rc);
		return -1;
	}
	
	if (bgl == NULL) 
		return -1;
	
	if ((rc = rm_get_data(bgl, RM_WireNum, &wire_num)) != STATUS_OK) {
		error("rm_get_data(RM_BPNum): %d", rc);
		wire_num = 0;
	}
	/* find out system wires on each bp */
	
	for (i=0; i<wire_num; i++) {
		
		if (i) {
			if ((rc = rm_get_data(bgl, RM_NextWire, &my_wire))
			    != STATUS_OK) {
				error("rm_get_data(RM_NextWire): %d", rc);
				break;
			}
		} else {
			if ((rc = rm_get_data(bgl, RM_FirstWire, &my_wire))
			    != STATUS_OK) {
				error("rm_get_data(RM_FirstWire): %d", rc);
				break;
			}
		}
		if ((rc = rm_get_data(my_wire, RM_WireID, &wire_id))
		    != STATUS_OK) {
			error("rm_get_data(RM_FirstWire): %d", rc);
			break;
		}
		
		if(!wire_id) {
			error("No Wire ID was returned from database");
			continue;
		}

		if(wire_id[7] != '_') 
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
		if(strlen(wire_id)<12) {
			error("Wire_id isn't correct %s",wire_id);
			continue;
		}
		strncpy(from_node, wire_id+2, 4);
		strncpy(to_node, wire_id+8, 4);
		
		free(wire_id);
		
		from_node[4] = '\0';
		to_node[4] = '\0';
		if ((rc = rm_get_data(my_wire, RM_WireFromPort, &my_port))
		    != STATUS_OK) {
			error("rm_get_data(RM_FirstWire): %d", rc);
			break;
		}
		if ((rc = rm_get_data(my_port, RM_PortID, &from_port))
		    != STATUS_OK) {
			error("rm_get_data(RM_PortID): %d", rc);
			break;
		}
		if ((rc = rm_get_data(my_wire, RM_WireToPort, &my_port))
		    != STATUS_OK) {
			error("rm_get_data(RM_WireToPort): %d", rc);
			break;
		}
		if ((rc = rm_get_data(my_port, RM_PortID, &to_port))
		    != STATUS_OK) {
			error("rm_get_data(RM_PortID): %d", rc);
			break;
		}

		coord = find_bp_loc(from_node);
		if(coord[X]>=DIM_SIZE[X] 
		   || coord[Y]>=DIM_SIZE[Y]
		   || coord[Z]>=DIM_SIZE[Z]) {
			error("got coord %d%d%d greater than system dims "
			      "%d%d%d",
			      coord[X],
			      coord[Y],
			      coord[Z],
			      DIM_SIZE[X],
			      DIM_SIZE[Y],
			      DIM_SIZE[Z]);
			continue;
		}
		source = &pa_system_ptr->
			grid[coord[X]][coord[Y]][coord[Z]];
		coord = find_bp_loc(to_node);
		if(coord[X]>=DIM_SIZE[X] 
		   || coord[Y]>=DIM_SIZE[Y]
		   || coord[Z]>=DIM_SIZE[Z]) {
			error("got coord %d%d%d greater than system dims "
			      "%d%d%d",
			      coord[X],
			      coord[Y],
			      coord[Z],
			      DIM_SIZE[X],
			      DIM_SIZE[Y],
			      DIM_SIZE[Z]);
			continue;
		}
		target = &pa_system_ptr->
			grid[coord[X]][coord[Y]][coord[Z]];
		_switch_config(source, 
			       target, 
			       dim, 
			       _port_enum(from_port),
			       _port_enum(to_port));	
		
		debug3("dim %d from %d%d%d %d -> %d%d%d %d",
		     dim,
		     source->coord[X], source->coord[Y], source->coord[Z],
		     _port_enum(from_port),
		     target->coord[X], target->coord[Y], target->coord[Z],
		     _port_enum(to_port));
		
	}
	if ((rc = rm_free_BGL(bgl)) != STATUS_OK)
		error("rm_free_BGL(): %s", rc);
	
#else

	_switch_config(source, source, dim, 0, 0);
	_switch_config(source, source, dim, 1, 1);
	if(dim!=X) {
		_switch_config(source, target, dim, 2, 5);
		_switch_config(source, source, dim, 3, 3);
		_switch_config(source, source, dim, 4, 4);
		return 1;
	}
	/* always 2->5 of next. If it is the last
		   it will go to the first.*/
		
	
#ifdef HAVE_BGL
#if 0
	/* this is here for the second half of bgl system.
	   if used it should be changed to #if 1
	*/
	if(count == 0) {
		/* 3->4 of next */
		_switch_config(source, target, dim, 3, 4);
		/* 4->3 of next */
		_switch_config(source, target, dim, 4, 3);
		/* 2 not in use */
		_switch_config(source, source, dim, 2, 2);
		target = &pa_system_ptr->grid[DIM_SIZE[X]-1]
			[source->coord[Y]]
			[source->coord[Z]];
		
		/* 5->2 of last */
		_switch_config(source, target, dim, 5, 2);
		
	} else if (count == 1) {
		/* 2->5 of next */
		_switch_config(source, target, dim, 2, 5);
		/* 5 not in use */
		_switch_config(source, source, dim, 5, 5);
	} else if (count == 2) {
		/* 2->5 of next */
		_switch_config(source, target, dim, 2, 5);
		/* 3->4 of next */
		_switch_config(source, target, dim, 3, 4);
		/* 4 not in use */
		_switch_config(source, source, dim, 4, 4);
	} else if(count == 3) {
		/* 2->5 of first */
		_switch_config(source, target, dim, 2, 5);
		/* 3 not in use */
		_switch_config(source, source, dim, 3, 3);
	}
      
	return 1;
#endif
	_switch_config(source, target, dim, 2, 5);
	if(count == 0 || count==4) {
		/* 0 and 4th Node */
		/* 3->4 of next */
		_switch_config(source, target, dim, 3, 4);
		/* 4 is not in use */
		_switch_config(source, source, dim, 4, 4);
	} else if( count == 1 || count == 5) {
		/* 1st and 5th Node */
		/* 3 is not in use */
		_switch_config(source, source, dim, 3, 3);
	} else if(count == 2) {
		/* 2nd Node */
		/* make sure target is the last node */
		target = &pa_system_ptr->grid[DIM_SIZE[X]-1]
			[source->coord[Y]]
			[source->coord[Z]];
		/* 3->4 of last */
		_switch_config(source, target, dim, 3, 4);
		/* 4->3 of last */
		_switch_config(source, target, dim, 4, 3);
	} else if(count == 3) {
		/* 3rd Node */
		/* make sure target is the next to last node */
		target = &pa_system_ptr->grid[DIM_SIZE[X]-2]
			[source->coord[Y]]
			[source->coord[Z]];
		/* 3->4 of next to last */
		_switch_config(source, target, dim, 3, 4);
		/* 4->3 of next to last */
		_switch_config(source, target, dim, 4, 3);
	}

	if(DIM_SIZE[X] <= 4) {
		/* 4 X dim fixes for wires */
		
		if(count == 2) {
			/* 2 not in use */
			_switch_config(source, source, dim, 2, 2);
		} else if(count == 3) {
			/* 5 not in use */
			_switch_config(source, source, dim, 5, 5);
		}
	} else if(DIM_SIZE[X] != 8) {
		fatal("Do don't have a config to do this BGL system.");
	}
#else
	if(count == 0)
		_switch_config(source, source, dim, 5, 5);
	else if(count < DIM_SIZE[X]-1)
		_switch_config(source, target, dim, 2, 5);
	else
		_switch_config(source, source, dim, 2, 2);
	_switch_config(source, source, dim, 3, 3);
	_switch_config(source, source, dim, 4, 4);
#endif /* HAVE_BGL */
#endif /* HAVE_BGL_FILES */
	return 1;
}
				
static char *_set_internal_wires(List nodes, int size, int conn_type)
{
	pa_node_t* pa_node[size+1];
	int count=0, i, set=0;
	int *start = NULL;
	int *end = NULL;
	char *name = xmalloc(sizeof(char)*BUFSIZE);
	ListIterator itr;
	hostlist_t hostlist = hostlist_create(NULL);
	
	if(!nodes)
		return NULL;
	itr = list_iterator_create(nodes);
	while((pa_node[count] = (pa_node_t*) list_next(itr))) {
		sprintf(name, "bgl%d%d%d\0", 
			pa_node[count]->coord[X],
			pa_node[count]->coord[Y],
			pa_node[count]->coord[Z]);
		debug3("name = %s",name);
		count++;
		hostlist_push(hostlist, name);
				
	}
	list_iterator_destroy(itr);
		
	start = pa_node[0]->coord;
	end = pa_node[count-1]->coord;	
	hostlist_ranged_string(hostlist, BUFSIZE, name);
	hostlist_destroy(hostlist);

	for(i=0;i<count;i++) {
		if(!pa_node[i]->used) {
			pa_node[i]->used=1;
			pa_node[i]->conn_type=conn_type;
			if(pa_node[i]->letter == '.') {
				pa_node[i]->letter = letters[color_count%62];
				pa_node[i]->color = colors[color_count%6];
				debug3("count %d setting letter = %c "
				       "color = %d",
				       color_count,
				       pa_node[i]->letter,
				       pa_node[i]->color);
				set=1;
			}
		} else {
			error("No network connection to create bglblock "
			      "containing %s", name);
			error("Use smap to define bglblocks in bluegene.conf");
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

static int _find_x_path(List results, pa_node_t *pa_node, 
	int *start, int *first, int *geometry, 
	int found, int conn_type) 
{
	pa_switch_t *curr_switch = NULL; 
	pa_switch_t *next_switch = NULL; 
	
	int port_tar;
	int source_port=0;
	int target_port=0;
	int num_visited=0;
	int broke = 0, not_first = 0;
	int ports_to_try[2] = {3,5};
	int *node_tar = NULL;
	int i, i2;
	pa_node_t *next_node = NULL;
	pa_node_t *check_node = NULL;
	int highest_phys_x = geometry[X] - start[X];
	
	ListIterator itr;
	List path = NULL;

	if(!pa_node)
		return 0;

	if(!source_port) {
		target_port=1;
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;
			
	}
	curr_switch = &pa_node->axis_switch[X];
	if(geometry[X] == 1) {
		goto found_one;
	}
	debug2("found - %d",found);
	for(i=0;i<2;i++) {
		/* check to make sure it isn't used */
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			/* looking at the next node on the switch 
			   and it's port we are going to */
			node_tar = curr_switch->
				ext_wire[ports_to_try[i]].node_tar;
			port_tar = curr_switch->
				ext_wire[ports_to_try[i]].port_tar;

			/* check to see if we are back at the start of the
			   partition */
			if((node_tar[X] == 
			    start[X] && 
			    node_tar[Y] == 
			    start[Y] && 
			    node_tar[Z] == 
			    start[Z])) {
				broke = 1;
				goto broke_it;
			}
			/* check to see if the port points to itself */
			if((node_tar[X] == 
			    pa_node->coord[X] && 
			    node_tar[Y] == 
			    pa_node->coord[Y] && 
			    node_tar[Z] == 
			    pa_node->coord[Z])) {
				continue;
			}
			/* check to see if I am going to a place I have
			   already been before */
			itr = list_iterator_create(results);
			while((next_node = (pa_node_t*) list_next(itr))) {
				debug3("looking at %d%d%d and %d%d%d",
				       next_node->coord[X],
				       next_node->coord[Y],
				       next_node->coord[Z],
				       node_tar[X],
				       node_tar[Y],
				       node_tar[Z]);
				if((node_tar[X] == 
				    next_node->coord[X] && 
				    node_tar[Y] == 
				    next_node->coord[Y] && 
				    node_tar[Z] == 
				    next_node->coord[Z])) {
					not_first = 1;
					break;
				}				
			}
			list_iterator_destroy(itr);
			if(not_first && found<DIM_SIZE[X]) {
				debug2("already been there before");
				not_first = 0;
				continue;
			} 
			not_first = 0;
				
		broke_it:
			next_node = &pa_system_ptr->
				grid[node_tar[X]]
#ifdef HAVE_BGL
				[node_tar[Y]]
				[node_tar[Z]]
#endif
				;
			next_switch = &next_node->axis_switch[X];

 			if((conn_type == MESH) && (found == (geometry[X]))) {
				debug2("we found the end of the mesh");
				return 1;
			}
			debug3("Broke = %d Found = %d geometry[X] = %d",
			       broke, found, geometry[X]);
			debug2("Next Phys X %d Highest X %d",
			       next_node->phys_x, highest_phys_x);
			if(next_node->phys_x >= highest_phys_x) {
				debug2("looking for a passthrough");
				list_destroy(best_path);
				best_path = list_create(_delete_path_list);
				_find_passthrough(curr_switch,
						  0,
						  results,
						  X,
						  0,
						  highest_phys_x);
				if(best_count < BEST_COUNT_INIT) {
					debug2("yes found next free %d", best_count);
					node_tar = _set_best_path();
					next_node = &pa_system_ptr->
						grid[node_tar[X]]
#ifdef HAVE_BGL
						[node_tar[Y]]
						[node_tar[Z]]
#endif
						;
					next_switch = 
						&next_node->axis_switch[X];
					
#ifdef HAVE_BGL
					debug2("found %d looking at %d%d%d going to %d%d%d %d",
					       found,
					       pa_node->coord[X],
					       pa_node->coord[Y],
					       pa_node->coord[Z],
					       node_tar[X],
					       node_tar[Y],
					       node_tar[Z],
					       port_tar);
#endif		
					list_append(results, next_node);
					found++;
					if(_find_x_path(results, next_node, 
							start, first, geometry, found, conn_type)) {
						return 1;
					} else {
						found--;
						_reset_the_path(curr_switch, 0, 1, X);
						_remove_node(results, next_node->coord);
						return 0;
					}
				}
			}			

			if(broke && (found == geometry[X])) {
				goto found_path;
			} else if(found == geometry[X]) {
				debug2("finishing the torus!");
				list_destroy(best_path);
				best_path = list_create(_delete_path_list);
				_finish_torus(curr_switch, 
					      0, 
					      results, 
					      X, 
					      0, 
					      start);
				if(best_count < BEST_COUNT_INIT) {
					debug2("Found a best path with %d "
					       "steps.", best_count);
					_set_best_path();
					return 1;
				} else {
					return 0;
				}
			} else if(broke) {
				broke = 0;
				continue;
			}

			if (!_node_used(next_node, geometry)) {
#ifdef HAVE_BGL
				debug2("found %d looking at %d%d%d "
				       "%d going to %d%d%d %d",
				       found,
				       pa_node->coord[X],
				       pa_node->coord[Y],
				       pa_node->coord[Z],
				       ports_to_try[i],
				       node_tar[X],
				       node_tar[Y],
				       node_tar[Z],
				       port_tar);
#endif
				itr = list_iterator_create(results);
				while((check_node = 
				       (pa_node_t*) list_next(itr))) {
					if((node_tar[X] == 
					    check_node->coord[X] && 
					    node_tar[Y] == 
					    check_node->coord[Y] && 
					    node_tar[Z] == 
					    check_node->coord[Z])) {
						break;
					}
				}
				list_iterator_destroy(itr);
				if(!check_node) {
#ifdef HAVE_BGL
					debug2("add %d%d%d",
					       next_node->coord[X],
					       next_node->coord[Y],
					       next_node->coord[Z]);
#endif					       
					list_append(results, next_node);
				} else {
#ifdef HAVE_BGL
					debug2("Hey this is already added "
					       "%d%d%d",
					       node_tar[X],
					       node_tar[Y],
					       node_tar[Z]);
#endif
					continue;
				}
				found++;
				
				if(!_find_x_path(results, next_node, 
						 start, first, geometry, 
						 found, conn_type)) {
					_remove_node(results,
						     next_node->coord);
					found--;
					continue;
				} else {
				found_path:
#ifdef HAVE_BGL
					debug2("added node %d%d%d %d %d -> "
					       "%d%d%d %d %d",
					       pa_node->coord[X],
					       pa_node->coord[Y],
					       pa_node->coord[Z],
					       source_port,
					       ports_to_try[i],
					       node_tar[X],
					       node_tar[Y],
					       node_tar[Z],
					       port_tar,
					       target_port);
#endif					
				found_one:			
					if(geometry[X] != 1) {
						curr_switch->
							int_wire
							[source_port].used = 1;
						curr_switch->
							int_wire
							[source_port].port_tar
							= ports_to_try[i];
						curr_switch->
							int_wire
							[ports_to_try[i]].used
							= 1;
						curr_switch->
							int_wire
							[ports_to_try[i]].
							port_tar = source_port;
					
						next_switch->
							int_wire[port_tar].used
							= 1;
						next_switch->
							int_wire
							[port_tar].port_tar
							= target_port;
						next_switch->
							int_wire
							[target_port].used = 1;
						next_switch->
							int_wire
							[target_port].port_tar
							= port_tar;
					}
					return 1;

				}
			} 			
		}
	}

	debug2("couldn't find path");
	return 0;
}

static int _find_x_path2(List results, pa_node_t *pa_node, 
	int *start, int *first, int *geometry, 
	int found, int conn_type) 
{
pa_switch_t *curr_switch = NULL; 
pa_switch_t *next_switch = NULL; 
	
int port_tar;
	int source_port=0;
	int target_port=0;
	int num_visited=0;
	int broke = 0, not_first = 0;
	int ports_to_try[2] = {3,5};
	int *node_tar = NULL;
	int i, i2;
	pa_node_t *next_node = NULL;
	pa_node_t *check_node = NULL;
	
	ListIterator itr;
	List path = NULL;

	if(!pa_node)
		return 0;

	if(!source_port) {
		target_port=1;
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;
			
	}
	curr_switch = &pa_node->axis_switch[X];
	if(geometry[X] == 1) {
		goto found_one;
	}
	debug2("found - %d",found);
	for(i=0;i<2;i++) {
		/* check to make sure it isn't used */
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			node_tar = curr_switch->
				ext_wire[ports_to_try[i]].node_tar;
			port_tar = curr_switch->
				ext_wire[ports_to_try[i]].port_tar;
			if((node_tar[X] == 
			    start[X] && 
			    node_tar[Y] == 
			    start[Y] && 
			    node_tar[Z] == 
			    start[Z])) {
				broke = 1;
				goto broke_it;
			}
			if((node_tar[X] == 
			    pa_node->coord[X] && 
			    node_tar[Y] == 
			    pa_node->coord[Y] && 
			    node_tar[Z] == 
			    pa_node->coord[Z])) {
				continue;
			}
			itr = list_iterator_create(results);
			while((next_node = (pa_node_t*) list_next(itr))) {
				if((node_tar[X] == 
				    next_node->coord[X] && 
				    node_tar[Y] == 
				    next_node->coord[Y] && 
				    node_tar[Z] == 
				    next_node->coord[Z])) {
					not_first = 1;
					break;
				}
				
			}
			list_iterator_destroy(itr);
			if(not_first && found<DIM_SIZE[X]) {
				not_first = 0;
				continue;
			} 
			not_first = 0;
				
		broke_it:
#ifdef HAVE_BGL
			next_node = &pa_system_ptr->
				grid[node_tar[X]][node_tar[Y]][node_tar[Z]];
#else
			next_node = &pa_system_ptr->
				grid[node_tar[X]];
#endif
			next_switch = &next_node->axis_switch[X];
		
			
 			if((conn_type == MESH) && (found == (geometry[X]))) {
				debug2("we found the end of the mesh");
				return 1;
			}
			debug3("Broke = %d Found = %d geometry[X] = %d",
			       broke, found, geometry[X]);
			if(broke && (found == geometry[X])) {
				goto found_path;
			} else if(found == geometry[X]) {
				debug2("finishing the torus!");
				list_destroy(best_path);
				best_path = list_create(_delete_path_list);
				_finish_torus(curr_switch, 
					      0, 
					      results, 
					      X, 
					      0, 
					      start);
				if(best_count < BEST_COUNT_INIT) {
					debug2("Found a best path with %d "
					       "steps.", best_count);
					_set_best_path();
					return 1;
				} else {
					return 0;
				}
			} else if(broke) {
				broke = 0;
				continue;
			}

			if (!_node_used(next_node, geometry)) {
#ifdef HAVE_BGL
				debug2("found %d looking at %d%d%d "
				       "%d going to %d%d%d %d",
				       found,
				       pa_node->coord[X],
				       pa_node->coord[Y],
				       pa_node->coord[Z],
				       ports_to_try[i],
				       node_tar[X],
				       node_tar[Y],
				       node_tar[Z],
				       port_tar);
#endif
				itr = list_iterator_create(results);
				while((check_node = 
				       (pa_node_t*) list_next(itr))) {
					if((node_tar[X] == 
					    check_node->coord[X] && 
					    node_tar[Y] == 
					    check_node->coord[Y] && 
					    node_tar[Z] == 
					    check_node->coord[Z])) {
						break;
					}
				}
				list_iterator_destroy(itr);
				if(!check_node) {
#ifdef HAVE_BGL
					debug2("add %d%d%d",
					       next_node->coord[X],
					       next_node->coord[Y],
					       next_node->coord[Z]);
#endif					       
					list_append(results, next_node);
				} else {
#ifdef HAVE_BGL
					debug2("Hey this is already added "
					       "%d%d%d",
					       node_tar[X],
					       node_tar[Y],
					       node_tar[Z]);
#endif
					continue;
				}
				found++;
				
				if(!_find_x_path2(results, next_node, 
						 start, first, geometry, 
						 found, conn_type)) {
					_remove_node(results,
						     next_node->coord);
					found--;
					continue;
				} else {
				found_path:
#ifdef HAVE_BGL
					debug2("added node %d%d%d %d %d -> "
					       "%d%d%d %d %d",
					       pa_node->coord[X],
					       pa_node->coord[Y],
					       pa_node->coord[Z],
					       source_port,
					       ports_to_try[i],
					       node_tar[X],
					       node_tar[Y],
					       node_tar[Z],
					       port_tar,
					       target_port);
#endif					
				found_one:			
					if(geometry[X] != 1) {
						curr_switch->
							int_wire
							[source_port].used = 1;
						curr_switch->
							int_wire
							[source_port].port_tar
							= ports_to_try[i];
						curr_switch->
							int_wire
							[ports_to_try[i]].used
							= 1;
						curr_switch->
							int_wire
							[ports_to_try[i]].
							port_tar = source_port;
					
						next_switch->
							int_wire[port_tar].used
							= 1;
						next_switch->
							int_wire
							[port_tar].port_tar
							= target_port;
						next_switch->
							int_wire
							[target_port].used = 1;
						next_switch->
							int_wire
							[target_port].port_tar
							= port_tar;
					}
					return 1;

				}
			} 			
		}
	}
#ifdef HAVE_BGL
	debug2("looking for the next free node starting at %d%d%d",
	       pa_node->coord[X],
	       pa_node->coord[Y],
	       pa_node->coord[Z]);
#endif
	list_destroy(best_path);
	best_path = list_create(_delete_path_list);
	_find_next_free(curr_switch, 
			0, 
			results, 
			X, 
			0);
	if(best_count < BEST_COUNT_INIT) {
		debug2("yes found next free %d", best_count);
		node_tar = _set_best_path();
#ifdef HAVE_BGL
		next_node = &pa_system_ptr->
			grid[node_tar[X]][node_tar[Y]][node_tar[Z]];
#else
		next_node = &pa_system_ptr->
			grid[node_tar[X]];
#endif
		next_switch = &next_node->axis_switch[X];
		
#ifdef HAVE_BGL
		debug2("found %d looking at %d%d%d going to %d%d%d %d",
		       found,
		       pa_node->coord[X],
		       pa_node->coord[Y],
		       pa_node->coord[Z],
		       node_tar[X],
		       node_tar[Y],
		       node_tar[Z],
		       port_tar);
#endif		
		list_append(results, next_node);
		found++;
		if(_find_x_path2(results, next_node, 
				start, first, geometry, found, conn_type)) {
			return 1;
		} else {
			found--;
			_reset_the_path(curr_switch, 0, 1, X);
			_remove_node(results, next_node->coord);
			return 0;
		}
	} 
	
	debug2("couldn't find path");
	return 0;
}

static int _remove_node(List results, int *node_tar)
{
	ListIterator itr;
	pa_node_t *pa_node = NULL;
	
	itr = list_iterator_create(results);
	while((pa_node = (pa_node_t*) list_next(itr))) {
		
#ifdef HAVE_BGL
		if(node_tar[X] == pa_node->coord[X] 
		   && node_tar[Y] == pa_node->coord[Y] 
		   && node_tar[Z] == pa_node->coord[Z]) {
			debug2("removing %d%d%d from list",
			       node_tar[X],
			       node_tar[Y],
			       node_tar[Z]);
			list_remove (itr);
			break;
		}
#else
		if(node_tar[X] == pa_node->coord[X]) {
			debug2("removing %d from list",
			       node_tar[X]);
			list_remove (itr);
			break;
		}
#endif
	}
	list_iterator_destroy(itr);
	return 1;
}

static int _find_next_free(pa_switch_t *curr_switch, int source_port, 
			   List nodes, int dim, int count) 
{
	pa_switch_t *next_switch = NULL; 
	pa_path_switch_t *path_add = 
		(pa_path_switch_t *) xmalloc(sizeof(pa_path_switch_t));
	pa_path_switch_t *path_switch = NULL;
	pa_path_switch_t *temp_switch = NULL;
	int port_tar;
	int target_port = 0;
	int ports_to_try[2] = {3,5};
	int *node_tar= curr_switch->ext_wire[0].node_tar;
	int *node_src = curr_switch->ext_wire[0].node_tar;
	int i;
	int used=0;
	int broke = 0;
	pa_node_t *pa_node = NULL;
	
	ListIterator itr;
	static bool found = false;

	path_add->geometry[X] = node_src[X];
#ifdef HAVE_BGL
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
#endif
	path_add->dim = dim;
	path_add->in = source_port;

	if(count>=best_count)
		return 0;
	
	itr = list_iterator_create(nodes);
	while((pa_node = (pa_node_t*) list_next(itr))) {
		
#ifdef HAVE_BGL
		if(node_tar[X] == pa_node->coord[X] 
		   && node_tar[Y] == pa_node->coord[Y] 
		   && node_tar[Z] == pa_node->coord[Z]) {
			broke = 1;
			break;
		}
#else
		if(node_tar[X] == pa_node->coord[X]) {
			broke = 1;
			break;
		}
#endif
		
	}
	list_iterator_destroy(itr);

	if(!broke && count>0 &&
	   !pa_system_ptr->grid[node_tar[X]]
#ifdef HAVE_BGL
	   [node_tar[Y]]
	   [node_tar[Z]]
#endif
	   .used) {
		
		debug3("this one not found %d%d%d",
		       node_tar[X],
		       node_tar[Y],
		       node_tar[Z]);
		
		broke = 0;
				
		if((source_port%2))
			target_port=1;
		
		list_destroy(best_path);
		best_path = list_create(_delete_path_list);
		found = true;
		path_add->out = target_port;
		list_push(path, path_add);
		
		itr = list_iterator_create(path);
		while((path_switch = (pa_path_switch_t*) list_next(itr))){
		
			temp_switch = (pa_path_switch_t *) 
				xmalloc(sizeof(pa_path_switch_t));
			 
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
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;	
	}
			
	for(i=0;i<2;i++) {
		used=0;
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			itr = list_iterator_create(path);
			while((path_switch = 
			       (pa_path_switch_t*) list_next(itr))){
				
				if(((path_switch->geometry[X] == node_src[X]) 
#ifdef HAVE_BGL
				    && (path_switch->geometry[Y] 
					== node_src[Y])
				    && (path_switch->geometry[Z] 
					== node_tar[Z])
#endif
					   )) {
					
					if( path_switch->out
					    == ports_to_try[i]) {
						used = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr);
			
			if(curr_switch->
			   ext_wire[ports_to_try[i]].node_tar[X]
			   == curr_switch->ext_wire[0].node_tar[X]  
#ifdef HAVE_BGL
			   && curr_switch->
			   ext_wire[ports_to_try[i]].node_tar[Y] 
			   == curr_switch->ext_wire[0].node_tar[Y] 
			   && curr_switch->
			   ext_wire[ports_to_try[i]].node_tar[Z] 
			   == curr_switch->ext_wire[0].node_tar[Z]
#endif
				) {
				continue;
			}
						
			if(!used) {
				port_tar = curr_switch->
					ext_wire[ports_to_try[i]].port_tar;
				node_tar = curr_switch->
					ext_wire[ports_to_try[i]].node_tar;
				
				next_switch = &pa_system_ptr->
					grid[node_tar[X]]
#ifdef HAVE_BGL
					[node_tar[Y]]
					[node_tar[Z]]
#endif
					.axis_switch[X];
				
				count++;
				path_add->out = ports_to_try[i];
				list_push(path, path_add);
				_find_next_free(next_switch, port_tar, nodes,
						dim, count);
				while(list_pop(path) != path_add){
				} 
			}
		}
	}
	xfree(path_add);
	return 0;
}

static int _find_passthrough(pa_switch_t *curr_switch, int source_port, 
			   List nodes, int dim, int count, int highest_phys_x) 
{
	pa_switch_t *next_switch = NULL; 
	pa_path_switch_t *path_add = 
		(pa_path_switch_t *) xmalloc(sizeof(pa_path_switch_t));
	pa_path_switch_t *path_switch = NULL;
	pa_path_switch_t *temp_switch = NULL;
	int port_tar;
	int target_port = 0;
	int ports_to_try[2] = {3,5};
	int *node_tar= curr_switch->ext_wire[0].node_tar;
	int *node_src = curr_switch->ext_wire[0].node_tar;
	int i;
	int used=0;
	int broke = 0;
	pa_node_t *pa_node = NULL;
	
	ListIterator itr;
	static bool found = false;

	path_add->geometry[X] = node_src[X];
#ifdef HAVE_BGL
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
#endif
	path_add->dim = dim;
	path_add->in = source_port;
	
	if(count>=best_count) {
		return 0;
	}

	itr = list_iterator_create(nodes);
	while((pa_node = (pa_node_t*) list_next(itr))) {
		
#ifdef HAVE_BGL
		if(node_tar[X] == pa_node->coord[X] 
		   && node_tar[Y] == pa_node->coord[Y] 
		   && node_tar[Z] == pa_node->coord[Z]) {
			broke = 1;
			break;
		}
#else
		if(node_tar[X] == pa_node->coord[X]) {
			broke = 1;
			break;
		}
#endif
		
	}
	list_iterator_destroy(itr);
	pa_node = &pa_system_ptr->
		grid[node_tar[X]]
#ifdef HAVE_BGL
		[node_tar[Y]]
		[node_tar[Z]]
#endif
		;
	if(!broke && count>0
	   && !pa_node->used 
	   && (pa_node->phys_x < highest_phys_x)) {
		
		debug3("this one not found %d%d%d",
		       node_tar[X],
		       node_tar[Y],
		       node_tar[Z]);
		
		broke = 0;
				
		if((source_port%2))
			target_port=1;
		
		list_destroy(best_path);
		best_path = list_create(_delete_path_list);
		found = true;
		path_add->out = target_port;
		list_push(path, path_add);
		
		itr = list_iterator_create(path);
		while((path_switch = (pa_path_switch_t*) list_next(itr))){
		
			temp_switch = (pa_path_switch_t *) 
				xmalloc(sizeof(pa_path_switch_t));
			 
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
		if(count==0) {
			ports_to_try[0] = 2;
			ports_to_try[1] = 4;	
		} else {
			ports_to_try[0] = 4;
			ports_to_try[1] = 2;	
		}
	}
			
	for(i=0;i<2;i++) {
		used=0;
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			itr = list_iterator_create(path);
			while((path_switch = 
			       (pa_path_switch_t*) list_next(itr))){
				
				if(((path_switch->geometry[X] == node_src[X]) 
#ifdef HAVE_BGL
				    && (path_switch->geometry[Y] 
					== node_src[Y])
				    && (path_switch->geometry[Z] 
					== node_tar[Z])
#endif
					   )) {
					
					if( path_switch->out
					    == ports_to_try[i]) {
						used = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr);
			
			if(curr_switch->
			   ext_wire[ports_to_try[i]].node_tar[X]
			   == curr_switch->ext_wire[0].node_tar[X]  
#ifdef HAVE_BGL
			   && curr_switch->
			   ext_wire[ports_to_try[i]].node_tar[Y] 
			   == curr_switch->ext_wire[0].node_tar[Y] 
			   && curr_switch->
			   ext_wire[ports_to_try[i]].node_tar[Z] 
			   == curr_switch->ext_wire[0].node_tar[Z]
#endif
				) {
				continue;
			}
						
			if(!used) {
				port_tar = curr_switch->
					ext_wire[ports_to_try[i]].port_tar;
				node_tar = curr_switch->
					ext_wire[ports_to_try[i]].node_tar;
				
				next_switch = &pa_system_ptr->
					grid[node_tar[X]]
#ifdef HAVE_BGL
					[node_tar[Y]]
					[node_tar[Z]]
#endif
					.axis_switch[X];
				
				count++;
				path_add->out = ports_to_try[i];
				list_push(path, path_add);
				debug3("looking at this one "
				       "%d%d%d %d -> %d%d%d %d",
				       pa_node->coord[X],
				       pa_node->coord[Y],
				       pa_node->coord[Z],
				       ports_to_try[i],
				       node_tar[X],
				       node_tar[Y],
				       node_tar[Z],
				       port_tar);
		
				_find_passthrough(next_switch, port_tar, nodes,
						dim, count, highest_phys_x);
				while(list_pop(path) != path_add){
				} 
			}
		}
	}
	xfree(path_add);
	return 0;
}

static int _finish_torus(pa_switch_t *curr_switch, int source_port, 
			 List nodes, int dim, int count, int *start) 
{
	pa_switch_t *next_switch = NULL; 
	pa_path_switch_t *path_add = 
		(pa_path_switch_t *) xmalloc(sizeof(pa_path_switch_t));
	pa_path_switch_t *path_switch = NULL;
	pa_path_switch_t *temp_switch = NULL;
	int port_tar;
	int target_port=0;
	int ports_to_try[2] = {3,5};
	int *node_tar= curr_switch->ext_wire[0].node_tar;
	int *node_src = curr_switch->ext_wire[0].node_tar;
	int i;
	int used=0;
	pa_node_t *pa_node = NULL;
	ListIterator itr;
	static bool found = false;

	path_add->geometry[X] = node_src[X];
#ifdef HAVE_BGL
	path_add->geometry[Y] = node_src[Y];
	path_add->geometry[Z] = node_src[Z];
#endif
	path_add->dim = dim;
	path_add->in = source_port;

	if(count>=best_count)
		return 0;
		
	if(node_tar[X] == start[X] 
#ifdef HAVE_BGL
	    && node_tar[Y] == start[Y] 
	    && node_tar[Z] == start[Z]
#endif
		) {
		
		if((source_port%2))
			target_port=1;
		if(!curr_switch->int_wire[target_port].used) {
			
			list_destroy(best_path);
			best_path = list_create(_delete_path_list);
			found = true;
			path_add->out = target_port;
			list_push(path, path_add);
			
			itr = list_iterator_create(path);
			while((path_switch = 
			       (pa_path_switch_t*) list_next(itr))){
				
				temp_switch = (pa_path_switch_t *) 
					xmalloc(sizeof(pa_path_switch_t));
				
				temp_switch->geometry[X] = 
					path_switch->geometry[X];
#ifdef HAVE_BGL
				temp_switch->geometry[Y] = 
					path_switch->geometry[Y];
				temp_switch->geometry[Z] = 
					path_switch->geometry[Z];
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
	}
	
	if(source_port==0 || source_port==3 || source_port==5) {
		ports_to_try[0] = 4;
		ports_to_try[1] = 2;		
	}
	
	for(i=0;i<2;i++) {
		used=0;
		if(!curr_switch->int_wire[ports_to_try[i]].used) {
			itr = list_iterator_create(path);
			while((path_switch = 
			       (pa_path_switch_t*) list_next(itr))){
				
				if(((path_switch->geometry[X] == node_src[X]) 
#ifdef HAVE_BGL
				    && (path_switch->geometry[Y] 
					== node_src[Y])
				    && (path_switch->geometry[Z] 
					== node_tar[Z])
#endif
					)) {
					if( path_switch->out
					    == ports_to_try[i]) {
						used = 1;
						break;
					}
				}
			}
			list_iterator_destroy(itr);
			if((curr_switch->
			    ext_wire[ports_to_try[i]].node_tar[X] == 
			    curr_switch->ext_wire[0].node_tar[X] && 
			    curr_switch->
			    ext_wire[ports_to_try[i]].node_tar[Y] == 
			    curr_switch->ext_wire[0].node_tar[Y] && 
			    curr_switch->
			    ext_wire[ports_to_try[i]].node_tar[Z] == 
			    curr_switch->ext_wire[0].node_tar[Z])) {
				continue;
			}
			if(!used) {
				port_tar = curr_switch->
					ext_wire[ports_to_try[i]].port_tar;
				node_tar = curr_switch->
					ext_wire[ports_to_try[i]].node_tar;
				
				next_switch = &pa_system_ptr->
					grid[node_tar[X]]
#ifdef HAVE_BGL
					[node_tar[Y]]
					[node_tar[Z]]
#endif
					.axis_switch[dim];
			
				
				count++;
				path_add->out = ports_to_try[i];
				list_push(path, path_add);
				_finish_torus(next_switch, port_tar, nodes,
						dim, count, start);
				while(list_pop(path) != path_add){
				} 
			}
		}
       }
       xfree(path_add);
       return 0;
}

static int *_set_best_path()
{
	ListIterator itr;
	pa_path_switch_t *path_switch = NULL;
	pa_switch_t *curr_switch = NULL; 
	int *geo = NULL;
	if(!best_path)
		return NULL;
	itr = list_iterator_create(best_path);
	while((path_switch = (pa_path_switch_t*) list_next(itr))) {
#ifdef HAVE_BGL
		debug3("mapping %d%d%d",path_switch->geometry[X],
		       path_switch->geometry[Y],
		       path_switch->geometry[Z]);
		if(!geo)
			geo = path_switch->geometry;
		curr_switch = &pa_system_ptr->
			grid
			[path_switch->geometry[X]]
			[path_switch->geometry[Y]]
			[path_switch->geometry[Z]].  
			axis_switch[path_switch->dim];
#else
		curr_switch = &pa_system_ptr->
			grid[path_switch->geometry[X]].
			axis_switch[path_switch->dim];
#endif
	
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

static int _set_one_dim(int *start, int *end, int *coord)
{
	int dim;
	pa_switch_t *curr_switch = NULL; 
	
	for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) {
		if(start[dim]==end[dim]) {
			curr_switch = &pa_system_ptr->grid[coord[X]]
#ifdef HAVE_BGL
				[coord[Y]]
				[coord[Z]]
#endif
				.axis_switch[dim];

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
	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	int debug_level = 5;

	List results;
//	List results2;
//	int i,j;
	DIM_SIZE[X]=8;
	DIM_SIZE[Y]=4;
	DIM_SIZE[Z]=4;
	pa_init(NULL);
	init_wires(NULL);
	log_opts.stderr_level  = debug_level;
	log_opts.logfile_level = debug_level;
	log_opts.syslog_level  = debug_level;
	
	log_alter(log_opts, LOG_DAEMON, 
		  "/dev/null");
						
	results = list_create(NULL);
	request->geometry[0] = 2;
	request->geometry[1] = 4;
	request->geometry[2] = 4;
	request->size = 32;
	request->rotate = 0;
	request->elongate = 0;
	request->conn_type = TORUS;
	new_pa_request(request);
	print_pa_request(request);
	if(!allocate_part(request, results)) {
       		debug("couldn't allocate %d%d%d",
		       request->geometry[0],
		       request->geometry[1],
		       request->geometry[2]);	
	}
	list_destroy(results);
	
	results = list_create(NULL);
	request->geometry[0] = 5;
	request->geometry[1] = 4;
	request->geometry[2] = 4;
	request->size = 4;
	request->conn_type = TORUS;
	new_pa_request(request);
	print_pa_request(request);
	if(!allocate_part(request, results)) {
       		debug("couldn't allocate %d%d%d",
		       request->geometry[0],
		       request->geometry[1],
		       request->geometry[2]);
	}
	list_destroy(results);
	
/* 	results = list_create(NULL); */
/* 	request->geometry[0] = 4; */
/* 	request->geometry[1] = 4; */
/* 	request->geometry[2] = 4; */
/* 	//request->size = 2; */
/* 	request->conn_type = TORUS; */
/* 	new_pa_request(request); */
/* 	print_pa_request(request); */
/* 	if(!allocate_part(request, results)) { */
/*        		printf("couldn't allocate %d%d%d\n", */
/* 		       request->geometry[0], */
/* 		       request->geometry[1], */
/* 		       request->geometry[2]); */
/* 	} */

/* 	results = list_create(NULL); */
/* 	request->geometry[0] = 1; */
/* 	request->geometry[1] = 4; */
/* 	request->geometry[2] = 4; */
/* 	//request->size = 2; */
/* 	request->conn_type = TORUS; */
/* 	new_pa_request(request); */
/* 	print_pa_request(request); */
/* 	if(!allocate_part(request, results)) { */
/*        		printf("couldn't allocate %d%d%d\n", */
/* 		       request->geometry[0], */
/* 		       request->geometry[1], */
/* 		       request->geometry[2]); */
/* 	} */

	
	int dim,j;
	int x,y,z;
	int startx=0;
	int starty=0;
	int startz=0;
	int endx=DIM_SIZE[X];
	int endy=1;//DIM_SIZE[Y];
	int endz=1;//DIM_SIZE[Z];
	for(x=startx;x<endx;x++) {
		for(y=starty;y<endy;y++) {
			for(z=startz;z<endz;z++) {
				info("Node %d%d%d Used = %d Letter = %c",
				       x,y,z,pa_system_ptr->grid[x][y][z].used,
				       pa_system_ptr->grid[x][y][z].letter);
				for(dim=0;dim<1;dim++) {
					info("Dim %d",dim);
					pa_switch_t *wire =
						&pa_system_ptr->
						grid[x][y][z].axis_switch[dim];
					for(j=0;j<6;j++)
						info("\t%d -> %d -> %d%d%d %d "
						     "Used = %d",
						     j, wire->int_wire[j].
						     port_tar,
						     wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     node_tar[X],
						     wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     node_tar[Y],
						     wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     node_tar[Z],
						     wire->ext_wire[
							     wire->int_wire[j].
							     port_tar].
						     port_tar,
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
