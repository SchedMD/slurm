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

#include <stdlib.h>
#include <math.h>
#include "partition_allocator.h"

int DIM_SIZE[PA_SYSTEM_DIMENSIONS] = {0,0,0};
#define DEBUG_PA
#define BEST_COUNT_INIT 10;

bool _initialized = false;


/* _pa_system is the "current" system that the structures will work
 *  on */
pa_system_t *pa_system_ptr;
List path;
List best_path;
int best_count;

/** internal helper functions */
/** */
void _new_pa_node(pa_node_t *pa_node, int coordinates[PA_SYSTEM_DIMENSIONS]);
/** */
void _print_pa_node();
/** */
void _create_pa_system();
/* */
void _delete_pa_system();
/* */
void _backup_pa_system();

int _check_for_options(pa_request_t* pa_request); 
/* find the first partition match in the system */
int _find_match(pa_request_t* pa_request, List results);

int _find_torus(pa_request_t *pa_request, List results);

int _find_mesh(pa_request_t *pa_request, List results);

bool _node_used(pa_node_t* pa_node, int *geometry);
/* */
int _create_config_even(pa_node_t ***grid);
/* */
void _switch_config(pa_node_t* source, pa_node_t* target, int dim, int port_src, int port_tar);
/* */
void _set_external_wires(int dim, int count, pa_node_t* source, 
			 pa_node_t* target_1, pa_node_t* target_2, 
			 pa_node_t* target_first, pa_node_t* target_second);
/* */
int _set_internal_wires(List nodes, int size, int conn_type);
/* */
int _set_internal_port(pa_switch_t *curr_switch, int check_port, int *coord, int dim);
/* */
int _find_one_hop(pa_switch_t *curr_switch, int source_port, int *target, int *target2, int dim);
/* */
int _find_best_path(pa_switch_t *start, int source_port, int *target, int *target2, int dim, int count);
/* */
int _set_best_path();
/* */
int _configure_dims(int *coord, int *end);
/* */
int _set_one_dim(int *start, int *end, int *coord);

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
		   bool force_contig, bool co_proc, int conn_type)
{
	int i, i2, i3, picked, total_sz=1 , size2, size3;
	float sz=1;
	int checked[8];
	
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
		for(i=0;i<8;i++)
			checked[i]=0;
		/* see if We can find a cube or square root of the size to make an easy cube */
		for(i=0;i<PA_SYSTEM_DIMENSIONS-1;i++) {
			sz = powf((float)size,(float)1/(PA_SYSTEM_DIMENSIONS-i));
			if(pow(sz,(PA_SYSTEM_DIMENSIONS-i))==size)
				break;
		}
		size3=size;
		if(i<PA_SYSTEM_DIMENSIONS-1) {
			i3=i;
			/* we found something that looks like a cube! */
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
						/* go through each number to see if the size 
						   is divisable by a smaller number that is 
						   good in the other dims. */
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
	
	//printf("geometry: %d %d %d size = %d\n", pa_request->geometry[0],pa_request->geometry[1],pa_request->geometry[2], pa_request->size);
		
	pa_request->conn_type = conn_type;
	pa_request->rotate_count= 0;
	pa_request->rotate = rotate;
	pa_request->elongate_count = 0;
	pa_request->elongate = elongate;
	pa_request->force_contig = force_contig;
	pa_request->co_proc = co_proc;
	
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
	printf("  pa_request:\n");
	printf("    geometry:\t");
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		printf("%d", pa_request->geometry[i]);
	}
	printf("\n");
	printf("        size:\t%d\n", pa_request->size);
	printf("   conn_type:\t%d\n", pa_request->conn_type);
	printf("      rotate:\t%d\n", pa_request->rotate);
	printf("    elongate:\t%d\n", pa_request->elongate);
	printf("force contig:\t%d\n", pa_request->force_contig);
	printf("     co_proc:\t%d\n", pa_request->co_proc);
}

/**
 * Initialize internal structures by either reading previous partition
 * configurations from a file or by running the graph solver.
 * 
 * IN: dunno yet, probably some stuff denoting downed nodes, etc.
 * 
 * return: success or error of the intialization.
 */
void pa_init(node_info_msg_t *node_info_ptr)
{
	node_info_t *node_ptr;
	int i;
	int start, temp;
	
	/* if we've initialized, just pop off all the old crusty
	 * pa_systems */
	if (_initialized){
		return;
	}

	if(node_info_ptr==NULL) {
		printf("You need to run slurm_load_node to init the node pointer\nbefore calling pa_init.\n");
		return;
	}
	
	best_count=BEST_COUNT_INIT;
					
	pa_system_ptr = (pa_system_t *) xmalloc(sizeof(pa_system_t));
		
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord = 1;
	pa_system_ptr->num_of_proc = 0;
	pa_system_ptr->resize_screen = 0;

	for (i = 0; i < node_info_ptr->record_count; i++) {
		node_ptr = &node_info_ptr->node_array[i];
		start = atoi(node_ptr->name + 3);
		temp = start / 100;

		if (DIM_SIZE[X] < temp)
			DIM_SIZE[X] = temp;
		temp = (start % 100) / 10;
		if (DIM_SIZE[Y] < temp)
			DIM_SIZE[Y] = temp;
		temp = start % 10;
		if (DIM_SIZE[Z] < temp)
			DIM_SIZE[Z] = temp;
	}
	DIM_SIZE[X]++;
	DIM_SIZE[Y]++;
	DIM_SIZE[Z]++;
	_create_pa_system();

	pa_system_ptr->num_of_proc = node_info_ptr->record_count;
                
	pa_system_ptr->fill_in_value = (pa_node_t *) 
		xmalloc(sizeof(pa_node_t) * pa_system_ptr->num_of_proc);
                
	init_grid(node_info_ptr);

	_create_config_even(pa_system_ptr->grid);
	
	path = list_create(NULL);
	best_path = list_create(NULL);

	_initialized = true;
}

/** 
 * destroy all the internal (global) data structs.
 */
void pa_fini()
{
	if (!_initialized){
		return;
	}

	list_destroy(path);
	list_destroy(best_path);
	_delete_pa_system();

//	printf("pa system destroyed\n");
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
	pa_system_ptr->grid[c[0]][c[1]][c[2]].used = true;
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
int allocate_part(pa_request_t* pa_request, List results)
{

	if (!_initialized){
		printf("allocate_part Error, configuration not initialized, call init_configuration first\n");
		return 0;
	}

	if (!pa_request){
		printf("allocate_part Error, request not initialized\n");
		return 0;
	}
	
	// _backup_pa_system();
	if (_find_match(pa_request, results)){
		//printf("hey I am returning 1\n");
		return 1;
	} else {
		printf("hey I am returning 0\n");
		return 0;
	}
}

/** 
 * Doh!  Admin made a boo boo.  Note: Undo only has one history
 * element, so two consecutive undo's will fail.
 *
 * returns SLURM_SUCCESS if undo was successful.
 */
int remove_part(List nodes)
{
	pa_node_t* pa_node;
	while((pa_node = (pa_node_t*) list_pop(nodes)) != NULL) {
		
	}
	
	return 1;
}

/** 
 * Doh!  Admin made a boo boo.  Note: Undo only has one history
 * element, so two consecutive undo's will fail.
 *
 * returns SLURM_SUCCESS if undo was successful.
 */
int alter_part(List nodes)
{
	pa_node_t* pa_node;
	while((pa_node = (pa_node_t*) list_pop(nodes)) != NULL) {
		
	}
	
	return 1;
}

/** 
 * After a partition is deleted or altered following allocations must
 * be redone to make sure correct path will be used in the real system
 *
 */
int redo_part(List nodes)
{
	pa_node_t* pa_node;
	while((pa_node = (pa_node_t*) list_pop(nodes)) != NULL) {
		
	}

	return 1;
}

/* _init_grid - set values of every grid point */
void init_grid(node_info_msg_t * node_info_ptr)
{
	node_info_t *node_ptr;
	int x, y, z, i = 0;
	int c[PA_SYSTEM_DIMENSIONS];
	uint16_t node_base_state;

	for (x = 0; x < DIM_SIZE[X]; x++)
		for (y = 0; y < DIM_SIZE[Y]; y++)
			for (z = 0; z < DIM_SIZE[Z]; z++) {
				node_ptr = &node_info_ptr->node_array[i];
				node_base_state = (node_ptr->node_state) & (~NODE_STATE_NO_RESPOND);
				pa_system_ptr->grid[x][y][z].color = 7;
				if ((node_base_state == NODE_STATE_DOWN) || 
				    (node_base_state == NODE_STATE_DRAINED) || 
				    (node_base_state == NODE_STATE_DRAINING)) {
					pa_system_ptr->grid[x][y][z].color = 0;
					pa_system_ptr->grid[x][y][z].letter = '#';
					if(_initialized) {
						c[0] = x;
						c[1] = y;
						c[2] = z;
						set_node_down(c);
					}
				} else {
					pa_system_ptr->grid[x][y][z].color = 7;
					pa_system_ptr->grid[x][y][z].letter = '.';
				}
				pa_system_ptr->grid[x][y][z].state = node_ptr->node_state;
				pa_system_ptr->grid[x][y][z].indecies = i++;
			}
	y = 65;
	z = 0;
	for (x = 0; x < pa_system_ptr->num_of_proc; x++) {
		y = y % 128;
		if (y == 0)
			y = 65;
		pa_system_ptr->fill_in_value[x].letter = y;
		z = z % 7;
		if (z == 0)
			z = 1;
		pa_system_ptr->fill_in_value[x].color = z;
		z++;
		y++;
	}
	return;
}

/** */
void _new_pa_node(pa_node_t *pa_node, int coord[PA_SYSTEM_DIMENSIONS])
{
	int i,j;
	pa_node->used = false;

	// pa_node = (pa_node_t*) xmalloc(sizeof(pa_node_t));
	for (i=0; i<PA_SYSTEM_DIMENSIONS; i++){
		pa_node->coord[i] = coord[i];
		
		for(j=0;j<NUM_PORTS_PER_NODE;j++) {
			pa_node->axis_switch[i].int_wire[j].used = 0;	
			pa_node->axis_switch[i].int_wire[j].port_tar = -1;	
		}
	}
}

/** */
void _print_pa_node(pa_node_t* pa_node)
{
	int i;

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
}

/** */
void _create_pa_system()
{
	int x, y, z;
	
	pa_system_ptr->grid = (pa_node_t***) 
		xmalloc(sizeof(pa_node_t**) * DIM_SIZE[X]);
	for (x=0; x<DIM_SIZE[X]; x++) {
		pa_system_ptr->grid[x] = (pa_node_t**) 
			xmalloc(sizeof(pa_node_t*) * DIM_SIZE[Y]);
		for (y=0; y<DIM_SIZE[Y]; y++) {
			pa_system_ptr->grid[x][y] = (pa_node_t*) 
				xmalloc(sizeof(pa_node_t) * DIM_SIZE[Z]);
			for (z=0; z<DIM_SIZE[Z]; z++){
				int coord[PA_SYSTEM_DIMENSIONS] = {x,y,z};
				_new_pa_node(&pa_system_ptr->grid[x][y][z], coord);
			}
		}
	}
	pa_system_ptr->fill_in_value = (pa_node_t *) 
		xmalloc(sizeof(pa_node_t) * pa_system_ptr->num_of_proc);
                
}

/** */
void _delete_pa_system()
{
	int x, y;
	
	if (!pa_system_ptr){
		return;
	}
	
	for (x=0; x<DIM_SIZE[X]; x++){
		for (y=0; y<DIM_SIZE[Y]; y++)
			xfree(pa_system_ptr->grid[x][y]);
		xfree(pa_system_ptr->grid[x]);
	}
	xfree(pa_system_ptr->grid);
	xfree(pa_system_ptr->fill_in_value);
	xfree(pa_system_ptr);
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
 * algorithm for finding match
 */
int _find_match(pa_request_t *pa_request, List results)
{
	int x=0, y=0, z=0;
	int *geometry = pa_request->geometry;
	int start_x=0, start_y=0, start_z=0;
	int find_x=0, find_y=0, find_z=0;
	pa_node_t* pa_node;
	int found_one=0;

start_again:
	//printf("starting looking for a grid of %d%d%d\n",pa_request->geometry[X],pa_request->geometry[Y],pa_request->geometry[Z]);
	//printf("\n");
	for (z=0; z<geometry[Z]; z++) {
		for (y=0; y<geometry[Y]; y++) {			
			for (x=0; x<geometry[X]; x++) {
				pa_node = &pa_system_ptr->grid[find_x][find_y][find_z];
				if (!_node_used(pa_node,geometry)) {
					
					//printf("Yeap, I found one at %d%d%d\n", find_x, find_y, find_z);
					//_insert_result(results, pa_node);
					list_append(results, pa_node);
					find_x++;
					found_one=1;
				} else {
					//printf("hey I am used! %d%d%d\n", find_x, find_y, find_z);
					if(found_one) {
						list_destroy(results);
						results = list_create(NULL);
						found_one=0;
					}
					if((DIM_SIZE[X]-find_x-1)>=geometry[X]) {
						find_x++;
						start_x=find_x;
					} else {
						find_x=0;
						start_x=find_x;
						if((DIM_SIZE[Y]-find_y-1)>=geometry[Y]) {
						/* if(find_y<(DIM_SIZE[Y]-1)) { */
							//find_y=0;
							//printf("incrementing find_y from %d to %d\n",find_z,find_z+1);
							find_y++;
							start_y=find_y;
						} else {
							find_y=0;
							start_y=find_y;						
							if ((DIM_SIZE[Z]-find_z-1)>=geometry[Z]) {
							/* if (find_z<(DIM_SIZE[Z]-1)) { */
								find_z++;
								start_z=find_z;
							//printf("incrementing find_Z from %d to %d\n",find_y,find_y+1);
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
	if(pa_request->conn_type==TORUS)
		_set_internal_wires(results, pa_request->size, TORUS);
	else
		_set_internal_wires(results, pa_request->size, MESH);
	} else {
		printf("couldn't find it 2\n");
		return 0;
	}
	return 1;
}

/** 
 * algorithm for finding match
 */
int _find_torus(pa_request_t *pa_request, List results)
{
	return 1;
}

/** 
 * algorithm for finding match
 */
int _find_mesh(pa_request_t *pa_request, List results)
{
	return 1;
}

/* bool _node_used(pa_node_t* pa_node, int geometry,  */
/*  		    int conn_type, bool force_contig, */
/* 		    dimension_t dim, int cur_node_id) */
bool _node_used(pa_node_t* pa_node, int *geometry)
{
	int i=0;
	pa_switch_t* pa_switch;
	

	/* printf("_node_used: node to check against %s %d\n", convert_dim(dim), cur_node_id); */
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

				_set_external_wires(X, x, source, target_1, target_2, 
						    target_first, target_second);
				
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
				_set_external_wires(Y, y, source, target_1, target_2, 
						    target_first, target_second);

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
				_set_external_wires(Z, z, source, target_1, target_2, 
						    target_first, target_second);
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

int _set_internal_wires(List nodes, int size, int conn_type)
{
	pa_node_t* pa_node[size];
	//pa_switch_t *next_switch; 
	int count=0, i;
	int *coord;
	int *start;
	int *end;
	static int part_count=0;
	ListIterator itr;
	
	itr = list_iterator_create(nodes);
	while((pa_node[count] = (pa_node_t*) list_next(itr))) {
		count++;
	}
	list_iterator_destroy(itr);
		
	start = pa_node[0]->coord;
	end = pa_node[count-1]->coord;	

	itr = list_iterator_create(nodes);
	
	for(i=0;i<count;i++) {
		coord = pa_node[i]->coord;		
		if(!pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].used) {	
			if(size!=1) 
				_configure_dims(coord, end);
			pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].used=1;
			if(conn_type==TORUS) 
				pa_system_ptr->
					grid[coord[X]][coord[Y]][coord[Z]].letter = 
					pa_system_ptr->fill_in_value[part_count].letter;
			else 
				pa_system_ptr->
					grid[coord[X]][coord[Y]][coord[Z]].letter = 
					pa_system_ptr->fill_in_value[part_count+32].letter;
			
			pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].color = 
				pa_system_ptr->fill_in_value[part_count].color;
			
		} else {
			printf("AHHHHHHH I can't do it in _set_internal_wires\n");
			return 0;	
		}
	}

	//printf("Start = %d%d%d, End = %d%d%d\n",start[X],start[Y],start[Z],end[X],end[Y],end[Z]);
	for(i=0;i<count;i++) {
		_set_one_dim(start, end, pa_node[i]->coord);
	}
	part_count++;		
/* 	int i; */
/* 	itr = list_iterator_create(nodes); */
/* 	while((pa_node = (pa_node_t*) list_next(itr))){ */
/* 		for(i=0;i<PA_SYSTEM_DIMENSIONS;i++) { */
/* 			printf("dim %d O set %d -> %d - %d%d%d\n",i,pa_node->axis_switch[i].int_wire[0].port_tar, pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[0].port_tar].port_tar, pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[0].port_tar].node_tar[X],pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[0].port_tar].node_tar[Y],pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[0].port_tar].node_tar[Z]); */
/* 			printf("dim %d 1 set %d -> %d - %d%d%d\n",i,pa_node->axis_switch[i].int_wire[1].port_tar, pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[1].port_tar].port_tar, pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[1].port_tar].node_tar[X],pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[1].port_tar].node_tar[Y],pa_node->axis_switch[i].ext_wire[pa_node->axis_switch[i].int_wire[1].port_tar].node_tar[Z]); */
/* 		} */
/* 		for(i=0;i<NUM_PORTS_PER_NODE;i++) { */
/* 			printf("Port %d -> %d Used = %d\n",i,pa_node->axis_switch[X].int_wire[i].port_tar,pa_node->axis_switch[X].int_wire[i].used); */
/* 		} */

/* 	} */
/* 	list_iterator_destroy(itr); */

	return 1;
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
	
	/* if(coord[X]==0 && coord[Y]==2 && coord[Z]==0) { */
/* 		printf("1 hey I am messing with it right here! dim %d %d -> %d\n", dim,source_port,check_port); */
/* 	} */
	curr_switch->int_wire[source_port].used = 1;
	curr_switch->int_wire[source_port].port_tar = check_port;
	curr_switch->int_wire[check_port].used = 1;
	curr_switch->int_wire[check_port].port_tar = source_port;
	
	node_tar = curr_switch->ext_wire[check_port].node_tar;
	port_tar = curr_switch->ext_wire[check_port].port_tar;
	next_switch = &pa_system_ptr->
		grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
	/* if(node_tar[X]==0 && node_tar[Y]==2 && node_tar[Z]==0) { */
/* 		printf("1 %d%d%d hey I am messing with it right here! dim %d %d -> %d\n",coord[X], coord[Y], coord[Z], dim,source_port,check_port); */
/* 		printf("2 hey I am messing with it right here! dim %d %d -> %d\n", dim,port_tar,target_port); */
/* 	} */
	next_switch->int_wire[port_tar].used = 1;
	next_switch->int_wire[port_tar].port_tar = port_tar;
	next_switch->int_wire[target_port].used = 1;
	next_switch->int_wire[target_port].port_tar = port_tar;
	
	return 1;
}

int _find_one_hop(pa_switch_t *curr_switch, int source_port, int *target, int *target2, int dim) 
{
	pa_switch_t *next_switch; 
	int port_tar;
	int target_port=0;
	int ports_to_try[2] = {3,5};
	int *node_tar;
	int i;

	if(!source_port) {
		target_port=1;
		ports_to_try[0] = 2;
		ports_to_try[1] = 4;		
	}
	
	for(i=0;i<2;i++) {
		node_tar = curr_switch->ext_wire[ports_to_try[i]].node_tar;
		if((node_tar[X]==target[X] && node_tar[Y]==target[Y] && node_tar[Z]==target[Z]) ||
		   (node_tar[X]==target2[X] && node_tar[Y]==target2[Y] && node_tar[Z]==target2[Z])) {
			curr_switch->int_wire[source_port].used = 1;
			curr_switch->int_wire[source_port].port_tar = ports_to_try[i];
			curr_switch->int_wire[ports_to_try[i]].used = 1;
			curr_switch->int_wire[ports_to_try[i]].port_tar = source_port;
	
			port_tar = curr_switch->ext_wire[ports_to_try[i]].port_tar;
			next_switch = &pa_system_ptr->
				grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
			next_switch->int_wire[port_tar].used = 1;
			next_switch->int_wire[port_tar].port_tar = target_port;
			next_switch->int_wire[target_port].used = 1;
			next_switch->int_wire[target_port].port_tar = port_tar;
			
			return 1;
		}		
	}
	printf("suck, I didn't find it in one hop dim = %d from port %d\n",dim,source_port);
	printf("targets from %d%d%d are %d%d%d and %d%d%d\n",node_tar[X],node_tar[Y],node_tar[Z],target[X],target[Y],target[Z],target2[X],target2[Y],target2[Z]);
	return 0;
}

int _find_best_path(pa_switch_t *start, int source_port, int *target, int *target2, int dim, int count) 
{
	pa_switch_t *next_switch; 
	pa_path_switch_t *path_add = (pa_path_switch_t *) xmalloc(sizeof(pa_path_switch_t));
	pa_path_switch_t *path_switch;
	pa_path_switch_t *temp_switch;
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

	if(count>=best_count)
		return 0;

	if((node_tar[X]==target[X] && node_tar[Y]==target[Y] && node_tar[Z]==target[Z]) ||
	   (node_tar[X]==target2[X] && node_tar[Y]==target2[Y] && node_tar[Z]==target2[Z])) {
		list_destroy(best_path);
		best_path = list_create(NULL);
		found = true;
		if((source_port%2))
			target_port=1;
		
		path_add->out = target_port;
		list_push(path, path_add);
		
		//printf("count = %d\n",count);
		itr = list_iterator_create(path);
		while((path_switch = (pa_path_switch_t*) list_next(itr))){
			//printf("%d%d%d %d - %d\n", path_switch->geometry[X], path_switch->geometry[Y], path_switch->geometry[Z], path_switch->in,  path_switch->out);
			temp_switch = (pa_path_switch_t *) xmalloc(sizeof(pa_path_switch_t));
			 
			temp_switch->geometry[X] = path_switch->geometry[X];
			temp_switch->geometry[Y] = path_switch->geometry[Y];
			temp_switch->geometry[Z] = path_switch->geometry[Z];
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
				//printf("%d%d%d %d%d%d %d %d - %d\n", path_switch->geometry[X], path_switch->geometry[Y], path_switch->geometry[Z], node_src[X], node_src[Y], node_src[Z], ports_to_try[i], path_switch->in,  path_switch->out);
				if(((path_switch->geometry[X] == node_src[X]) && 
				    (path_switch->geometry[Y] == node_src[Y]) && 
				    (path_switch->geometry[Z] == node_tar[Z]))) {
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
				
				next_switch = &pa_system_ptr->
					grid[node_tar[X]][node_tar[Y]][node_tar[Z]].axis_switch[dim];
				
				count++;
				path_add->out = ports_to_try[i];
				list_push(path, path_add);
				//printf("%d%d%d %d - %d\n", path_add->geometry[X], path_add->geometry[Y], path_add->geometry[Z], path_add->in,  path_add->out);
				_find_best_path(next_switch, port_tar, target, target2, dim, count);
				//printf("popping %d%d%d %d - %d\n", path_add->geometry[X], path_add->geometry[Y], path_add->geometry[Z], path_add->in,  path_add->out);
				while(list_pop(path) != path_add){
				} 
			}
		}
	}
	xfree(path_add);
	return 0;
}

int _set_best_path()
{
	ListIterator itr;
	pa_path_switch_t *path_switch;
	pa_switch_t *curr_switch; 
	
	itr = list_iterator_create(best_path);
	while((path_switch = (pa_path_switch_t*) list_next(itr))){
		//printf("final %d%d%d %d - %d\n", path_switch->geometry[X], path_switch->geometry[Y], path_switch->geometry[Z], path_switch->in,  path_switch->out);
		curr_switch = &pa_system_ptr->
			grid
			[path_switch->geometry[X]]
			[path_switch->geometry[Y]]
			[path_switch->geometry[Z]].  
			axis_switch[path_switch->dim];
		/* if(path_switch->geometry[X]==0 && path_switch->geometry[Y]==2 && path_switch->geometry[Z]==0) { */
/* 			printf("hey I am messing with it right here! dim %d %d -> %d\n",path_switch->dim,path_switch->in,path_switch->out); */
/* 		} */
	
		curr_switch->int_wire[path_switch->in].used = 1;
		curr_switch->int_wire[path_switch->in].port_tar = path_switch->out;
		curr_switch->int_wire[path_switch->out].used = 1;
		curr_switch->int_wire[path_switch->out].port_tar = path_switch->in;			
	}	
	best_count=BEST_COUNT_INIT;
	//printf("\n");
	return 1;
}

int _configure_dims(int *coord, int *end)
{
	pa_switch_t *curr_switch; 
	int dim;
	int j;
			
	int target[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	int target2[PA_SYSTEM_DIMENSIONS] = {0,0,0};
	
	/* set it up for the X axis */
	//printf("working on node %d%d%d\n",coord[X],coord[Y],coord[Z]); 
	for(dim=0; dim<PA_SYSTEM_DIMENSIONS; dim++) {
		//printf("Dim %d\n",dim); 
		curr_switch = &pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].axis_switch[dim];
		if(dim==X) {
			target[X]=coord[X]+1;
			target2[X]=coord[X]+2;
		} else {
			target[X]=coord[X];	
			target2[X]=coord[X];
		}
		if(dim==Y) {
			target[Y]=coord[Y]+1;
			target2[Y]=coord[Y]+2;
		} else {
			target[Y]=coord[Y];
			target2[Y]=coord[Y];
		}
		if(dim==Z) {
			target[Z]=coord[Z]+1;
			target2[Z]=coord[Z]+2;
		} else {
			target[Z]=coord[Z];
			target2[Z]=coord[Z];
		}
		if(coord[dim]<(end[dim]-1)) {
			/* set it up for 0 */
			if(!curr_switch->int_wire[0].used) {
				if(!_find_one_hop(curr_switch, 0, target, target2, dim)) {
					_find_best_path(curr_switch, 0, target, target2, dim, 0);
					_set_best_path();
				} /* else {/\* the switch is full on this level, we can't use it *\/ */
/* 					printf("Oh my gosh I can't do it in configure dims 0\n"); */
/* 					printf("Switch %d%d%d dim %d\n",coord[X],coord[Y],coord[Z],dim); */
/* 					for(j=0;j<6;j++) */
/* 						printf("\t%d -> %d Used = %d\n", j, curr_switch->int_wire[j].port_tar, curr_switch->int_wire[j].used); */
/* 					return 0; */
/* 				} */
			} 

			if(!curr_switch->int_wire[1].used) { 
				if(!_find_one_hop(curr_switch, 1, target, target2, dim)) {
					_find_best_path(curr_switch, 1, target, target2, dim, 0);
					_set_best_path();
				} /* else {/\* the switch is full on this level, we can't use it *\/ */
/* 					printf("Oh my gosh I can't do it in configure dims 1\n"); */
/* 					printf("Switch %d%d%d dim %d\n",coord[X],coord[Y],coord[Z],dim); */
/* 					for(j=0;j<6;j++) */
/* 						printf("\t%d -> %d Used = %d\n", j, curr_switch->int_wire[j].port_tar, curr_switch->int_wire[j].used); */
/* 					//return 0; */
/* 				} */
			}
			
			/*****************************/
			
		} else if(coord[dim]==(end[dim]-1)) {
			//next_switch = &pa_system_ptr->grid[coord[X]+1][coord[Y]][coord[Z]].axis_switch[X];	
			if(!curr_switch->int_wire[0].used) {
				_find_best_path(curr_switch, 0, target, target, dim, 0);
				_set_best_path();
			}
			if(!curr_switch->int_wire[1].used) {
				_find_best_path(curr_switch, 1, target, target, dim, 0);	
				_set_best_path();
			} 
		} /* else  */
/* 			printf("I don't think we should get here\n"); */
	}
	return 1;
	
}

int _set_one_dim(int *start, int *end, int *coord)
{
	int dim;
	pa_switch_t *curr_switch; 
	
	for(dim=0;dim<PA_SYSTEM_DIMENSIONS;dim++) {
		if(start[dim]==end[dim]) {
			curr_switch = &pa_system_ptr->grid[coord[X]][coord[Y]][coord[Z]].axis_switch[dim];
			if(!curr_switch->int_wire[0].used && !curr_switch->int_wire[1].used) {
				curr_switch->int_wire[0].used = 1;
				curr_switch->int_wire[0].port_tar = 1;
				curr_switch->int_wire[1].used = 1;
				curr_switch->int_wire[1].port_tar = 0;
			}
		}
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
	bool co_proc = true;
	pa_request_t *request; 
	int error_code;
	time_t start, end;
	node_info_msg_t * node_info_ptr;
	List results;
//	int i,j;
	request = (pa_request_t*) xmalloc(sizeof(pa_request_t));
		
	//printf("geometry: %d %d %d size = %d\n", request->geometry[0], request->geometry[1], request->geometry[2], request->size);
#ifdef HAVE_BGL
	error_code = slurm_load_node((time_t) NULL, &node_info_ptr, 0);
	if (error_code) {
		slurm_perror("slurm_load_node");
		exit(0);
	} else {
		pa_init(node_info_ptr);
        }
#else
	printf("This will only run on a BGL system right now.\n");
	exit(0);
#endif
	
	results = list_create(NULL);
	geo[0] = -1;
	geo[1] = 3;
	geo[2] = 1;	
	int size = atoi(argv[1]);
	new_pa_request(request, geo, size, rotate, elongate, force_contig, co_proc, SELECT_TORUS);
	time(&start);
	print_pa_request(request);
	allocate_part(request, results);
	time(&end);
	//printf("allocate_part: %ld\n", (end-start));
	list_destroy(results);
			
/* 	results = list_create(NULL); */
/* 	geo[0] = 3; */
/* 	geo[1] = 3; */
/* 	geo[2] = 3; */
/* 	new_pa_request(request, geo, -1, rotate, elongate, force_contig, co_proc, SELECT_TORUS); */
/* 	time(&start); */
/* 	print_pa_request(request); */
/* 	allocate_part(request, results); */
/* 	time(&end); */
/* 	//printf("allocate_part: %ld\n", (end-start)); */
/* 	list_destroy(results); */
	
/* 	results = list_create(NULL); */
/* 	geo[0] = 2; */
/* 	geo[1] = 2; */
/* 	geo[2] = 2; */
/* 	new_pa_request(request, geo, -1, rotate, elongate, force_contig, co_proc, SELECT_TORUS); */
/* 	print_pa_request(request); */
/* 	allocate_part(request, results); */
/* 	list_destroy(results); */

/* 	results = list_create(NULL); */
/* 	geo[0] = 2; */
/* 	geo[1] = 2; */
/* 	geo[2] = 2; */
/* 	new_pa_request(request, geo, -1, rotate, elongate, force_contig, co_proc, SELECT_TORUS); */
/* 	print_pa_request(request); */
/* 	allocate_part(request, results); */
/* 	list_destroy(results); */

/* 	results = list_create(NULL); */
/* 	geo[0] = 2; */
/* 	geo[1] = 2; */
/* 	geo[2] = 2; */
/* 	new_pa_request(request, geo, -1, rotate, elongate, force_contig, co_proc, SELECT_TORUS); */
/* 	print_pa_request(request); */
/* 	allocate_part(request, results); */
/* 	list_destroy(results); */

/* 	results = list_create(NULL); */
/* 	geo[0] = 2; */
/* 	geo[1] = 2; */
/* 	geo[2] = 2; */
/* 	new_pa_request(request, geo, -1, rotate, elongate, force_contig, co_proc, SELECT_TORUS); */
/* 	print_pa_request(request); */
/* 	allocate_part(request, results); */
/* 	list_destroy(results); */

/* 	int dim,j; */
/* 	int x,y,z; */
/* 	int startx=0; */
/* 	int starty=0; */
/* 	int startz=0; */
/* 	int endx=7; */
/* 	int endy=0; */
/* 	int endz=0; */
/* 	for(x=startx;x<=endx;x++) { */
/* 		for(y=starty;y<=endy;y++) { */
/* 			for(z=startz;z<=endz;z++) { */
/* 				printf("Node %d%d%d Used = %d\n",x,y,z,pa_system_ptr->grid[x][y][z].used); */
/* 				for(dim=0;dim<1;dim++) { */
/* 					printf("Dim %d\n",dim); */
/* 					pa_switch_t *wire = &pa_system_ptr->grid[x][y][z].axis_switch[dim]; */
/* 					for(j=0;j<6;j++) */
/* 						printf("\t%d -> %d Used = %d\n", j, wire->int_wire[j].port_tar, wire->int_wire[j].used); */
/* 				} */
/* 			} */
/* 		} */
/* 	} */
/* 	pa_switch_t *wire = &pa_system_ptr->grid[7][3][3].axis_switch[0]; */
/* 	for(j=0;j<6;j++) */
/* 		printf("\t%d -> %d Used = %d\n", j, wire->int_wire[j].port_tar, wire->int_wire[j].used); */

// time(&start);
	pa_fini();
	// time(&end);
	// printf("fini: %ld\n", (end-start));

	delete_pa_request(request);
	
	return 0;
}
