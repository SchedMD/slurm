/*****************************************************************************\
 *  configure_functions.c - Functions related to configure mode of smap.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  UCRL-CODE-217948.
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

#include "src/common/uid.h"
#include "src/smap/smap.h"

typedef struct {
	int color;
	char letter;
	List nodes;
	ba_request_t *request; 
} allocated_block_t;

static void	_delete_allocated_blocks(List allocated_blocks);
static allocated_block_t *_make_request(ba_request_t *request);
static int      _set_layout(char *com);
static int      _set_base_part_cnt(char *com);
static int      _set_nodecard_cnt(char *com);
static int	_create_allocation(char *com, List allocated_blocks);
static int	_resolve(char *com);
static int	_change_state_all_bps(char *com, int state);
static int	_change_state_bps(char *com, int state);
static int	_remove_allocation(char *com, List allocated_blocks);
static int	_alter_allocation(char *com, List allocated_blocks);
static int	_copy_allocation(char *com, List allocated_blocks);
static int	_save_allocation(char *com, List allocated_blocks);
static void     _strip_13_10(char *line);
static int	_load_configuration(char *com, List allocated_blocks);
static void	_print_header_command(void);
static void	_print_text_command(allocated_block_t *allocated_block);

char error_string[255];
int base_part_node_cnt = 512;
int nodecard_node_cnt = 32;
char *layout_mode = "STATIC";

static void _delete_allocated_blocks(List allocated_blocks)
{
	allocated_block_t *allocated_block = NULL;
	
	while ((allocated_block = list_pop(allocated_blocks)) != NULL) {
		remove_block(allocated_block->nodes,0);
		list_destroy(allocated_block->nodes);
		delete_ba_request(allocated_block->request);
		xfree(allocated_block);
	}
	list_destroy(allocated_blocks);
}

static allocated_block_t *_make_request(ba_request_t *request)
{
	List results = list_create(NULL);
	ListIterator results_i;		
	allocated_block_t *allocated_block = NULL;
	ba_node_t *current = NULL;
	
	if (!allocate_block(request, results)){
		memset(error_string,0,255);
		sprintf(error_string,"allocate failure for %dx%dx%d", 
			  request->geometry[0], request->geometry[1], 
			  request->geometry[2]);
		return NULL;
	} else {
		if(request->passthrough)
			sprintf(error_string,"THERE ARE PASSTHROUGHS IN "
				"THIS ALLOCATION!!!!!!!");
		
		allocated_block = (allocated_block_t *)xmalloc(
			sizeof(allocated_block_t));
		allocated_block->request = request;
		allocated_block->nodes = list_create(NULL);
		results_i = list_iterator_create(results);
		while ((current = list_next(results_i)) != NULL) {
			list_append(allocated_block->nodes,current);
			allocated_block->color = current->color;
			allocated_block->letter = current->letter;
		}
		list_iterator_destroy(results_i);
	}
	list_destroy(results);
	return(allocated_block);

}

static int _set_layout(char *com)
{
	int i=0;
	int len = strlen(com);
	
	while(i<len) {
		if(!strncasecmp(com+i, "dynamic", 7)) {
			layout_mode = "DYNAMIC";
			break;
		} else if(!strncasecmp(com+i, "static", 6)) {
			layout_mode = "STATIC";
			break;
		} else if(!strncasecmp(com+i, "overlap", 7)) {
			layout_mode = "OVERLAP";
			break;
		} else {
			i++;
		}
	}
	if(i>=len) {
		sprintf(error_string, 
			"You didn't put in a mode that I recognized. \n"
			"Please use (STATIC, OVERLAP, or DYNAMIC)\n");
		return 0;
	}
	sprintf(error_string, 
		"LayoutMode set to %s\n", layout_mode);
	
}

static int _set_base_part_cnt(char *com)
{
	int i=0;
	int len = strlen(com);

	while(i<len) {
		if(com[i] < 58 && com[i] > 47) {
			break;
		} else {
			i++;
		}
	}
	if(i>=len) {		
		sprintf(error_string, 
			"I didn't notice the number you typed in\n");
		return 0;
	}
	base_part_node_cnt = atoi(&com[i]);
	sprintf(error_string, 
		"BasePartitionNodeCnt set to %d\n", base_part_node_cnt);
		
	return 1;
}

static int _set_nodecard_cnt(char *com)
{
	int i=0;
	int len = strlen(com);

	while(i<len) {
		if(com[i] < 58 && com[i] > 47) {
			break;
		} else {
			i++;
		}
	}
	if(i>=len) {		
		sprintf(error_string, 
			"I didn't notice the number you typed in\n");
		return 0;
	}
	nodecard_node_cnt = atoi(&com[i]);
	sprintf(error_string, 
		"NodeCardNodeCnt set to %d\n", nodecard_node_cnt);
		
	return 1;
}

static int _create_allocation(char *com, List allocated_blocks)
{
	int i=6, geoi=-1, starti=-1, i2=0, nodecards=-1, quarters=-1;
	int len = strlen(com);
	char *temp = NULL;
	allocated_block_t *allocated_block = NULL;
	ba_request_t *request = (ba_request_t*) xmalloc(sizeof(ba_request_t)); 
	
	request->geometry[0] = (uint16_t)NO_VAL;
	request->conn_type=SELECT_TORUS;
	request->rotate = false;
	request->elongate = false;
	request->start_req=0;
	request->size = 0;
	request->nodecards = 0;
	request->quarters = 0;
	request->passthrough = false;
	
	while(i<len) {				
		if(!strncasecmp(com+i, "mesh", 4)) {
			request->conn_type=SELECT_MESH;
			i+=4;
		} else if(!strncasecmp(com+i, "small", 5)) {
			request->conn_type = SELECT_SMALL;
			i+=5;
		} else if(!strncasecmp(com+i, "nodecard", 8)) {
			nodecards=0;
			i+=5;
		} else if(!strncasecmp(com+i, "quarter", 7)) {
			quarters=0;
			i+=6;
		} else if(!strncasecmp(com+i, "rotate", 6)) {
			request->rotate=true;
			i+=6;
		} else if(!strncasecmp(com+i, "elongate", 8)) {
			request->elongate=true;
			i+=8;
		} else if(!strncasecmp(com+i, "start", 5)) {
			request->start_req=1;
			i+=5;					
		} else if(request->start_req 
			  && starti<0 
			  && (com[i] < 58 && com[i] > 47)) {
			starti=i;
			i++;
		} else if(nodecards == 0 && (com[i] < 58 && com[i] > 47)) {
			nodecards=i;
			i++;
		} else if(quarters == 0 && (com[i] < 58 && com[i] > 47)) {
			quarters=i;
			i++;
		} else if(geoi<0 && (com[i] < 58 && com[i] > 47)) {
			geoi=i;
			i++;
		} else {
			i++;
		}
		
	}		
	
	if(request->conn_type == SELECT_SMALL) {
		if(nodecards > 0) {
			request->nodecards = atoi(&com[nodecards]);
			nodecards = request->nodecards/4;
			request->nodecards = nodecards*4;
		}

		request->quarters = 4;
		
		if(request->nodecards > 0)
			request->quarters -= nodecards;

		if(request->quarters > 4) {
			request->quarters = 4;
			request->nodecards = 0;
		} else if(request->nodecards > 16) {
			request->quarters = 0;
			request->nodecards = 16;
		}
		
		quarters = request->quarters*4;
		nodecards = request->nodecards;
		if((quarters+nodecards) > 16) {
			sprintf(error_string, 
				"please specify a complete split of a "
				"Base Partion\n"
				"(i.e. nodecards=4)\0");
			geoi = -1;
		}
		request->size = 1;
				
	}

	if(geoi<0 && !request->size) {
		memset(error_string,0,255);
		sprintf(error_string, 
			"No size or dimension specified, please re-enter");
	} else {
		i2=geoi;
		while(i2<len) {
			if(request->size)
				break;
			if(com[i2]==' ' || i2==(len-1)) {
				/* for size */
				request->size = atoi(&com[geoi]);
				break;
			}
			if(com[i2]=='x') {
				
				/* for geometery */
				request->geometry[X] = atoi(&com[geoi]);
				geoi++;
				while(com[geoi-1]!='x' && geoi<len)
					geoi++;
				if(geoi==len)
					goto geo_error_message;
				
				request->geometry[Y] = atoi(&com[geoi]);
				geoi++;
				while(com[geoi-1]!='x' && geoi<len)
					geoi++;
				if(geoi==len)
					goto geo_error_message;
				
				request->geometry[Z] = atoi(&com[geoi]);
				request->size = -1;
				break;
			}
			i2++;
		}

		if(request->start_req) {
			/* for size */
			request->start[X] = atoi(&com[starti]);
			starti++;
			while(com[starti-1]!='x' && starti<len)
				starti++;
			if(starti==len) 
				goto start_request;
			request->start[Y] = atoi(&com[starti]);
			starti++;
			while(com[starti-1]!='x' && starti<len)
				starti++;
			if(starti==len)
				goto start_request;
			request->start[Z] = atoi(&com[starti]);
		}
	start_request:
		if(!strcasecmp(layout_mode,"OVERLAP"))
			reset_ba_system();
	
		/*
		  Here is where we do the allocating of the partition. 
		  It will send a request back which we will throw into
		  a list just incase we change something later.		   
		*/
		if(!new_ba_request(request)) {
			memset(error_string,0,255);
			if(request->size!=-1) {
				sprintf(error_string, 
					"Problems with request for %d\n"
					"Either you put in something "
					"that doesn't work,\n"
					"or we are unable to process "
					"your request.", 
					request->size);
			} else
				sprintf(error_string, 
					"Problems with request for %dx%dx%d\n"
					"Either you put in something "
					"that doesn't work,\n"
					"or we are unable to process "
					"your request.", 
					request->geometry[0], 
					request->geometry[1], 
					request->geometry[2]);
		} else {
			if((allocated_block = _make_request(request)) != NULL)
				list_append(allocated_blocks, 
					    allocated_block);
			else {
				i2 = strlen(error_string);
				sprintf(error_string+i2,
					"\nGeo requested was %d (%dx%dx%d)\n"
					"Start position was %dx%dx%d",
					request->size,
					request->geometry[0], 
					request->geometry[1], 
					request->geometry[2],
					request->start[0], 
					request->start[1], 
					request->start[2]);
			}
		}
	}
	return 1;
	
geo_error_message:
	memset(error_string,0,255);
	sprintf(error_string, 
		"Error in geo dimension "
		"specified, please re-enter");
	
	return 0;
}

static int _resolve(char *com)
{
	int i=0;
#ifdef HAVE_BG_FILES
	int len=strlen(com);
	char *rack_mid = NULL;
	int *coord = NULL;
#endif
	
	while(com[i-1] != ' ' && com[i] != '\0')
		i++;
	if(com[i] == 'r')
		com[i] = 'R';
		
	memset(error_string,0,255);		
#ifdef HAVE_BG_FILES
	if (!have_db2) {
		sprintf(error_string, "Must be on BG SN to resolve\n"); 
		goto resolve_error;
	}

	if(len-i<3) {
		sprintf(error_string, "Must enter 3 coords to resolve.\n");
		goto resolve_error;
	}
	if(com[i] != 'R') {
		rack_mid = find_bp_rack_mid(com+i);
		
		if(rack_mid)
			sprintf(error_string, 
				"X=%c Y=%c Z=%c resolves to %s\n",
				com[X+i],com[Y+i],com[Z+i], rack_mid);
		else
			sprintf(error_string, 
				"X=%c Y=%c Z=%c has no resolve\n",
				com[X+i],com[Y+i],com[Z+i]);
		
	} else {
		coord = find_bp_loc(com+i);
		
		if(coord)
			sprintf(error_string, 
				"%s resolves to X=%d Y=%d Z=%d or bg%d%d%d\n",
				com+i,coord[X],coord[Y],coord[Z],
				coord[X],coord[Y],coord[Z]);
		else
			sprintf(error_string, "%s has no resolve.\n", 
				com+i);	
	}
resolve_error:
#else
			sprintf(error_string, 
				"Must be on BG SN to resolve.\n"); 
#endif
	wnoutrefresh(ba_system_ptr->text_win);
	doupdate();

	return 1;
}
static int _change_state_all_bps(char *com, int state)
{
	char allnodes[50];
	memset(allnodes,0,50);
		
#ifdef HAVE_BG
	sprintf(allnodes, "000x%d%d%d", 
		DIM_SIZE[X]-1, DIM_SIZE[Y]-1, DIM_SIZE[Z]-1);
#else
	sprintf(allnodes, "0-%d", 
		DIM_SIZE[X]);
#endif
	return _change_state_bps(allnodes, state);
	
}
static int _change_state_bps(char *com, int state)
{
	int i=0, j=0, x;
	int len = strlen(com);
	int start[SYSTEM_DIMENSIONS], end[SYSTEM_DIMENSIONS];
#ifdef HAVE_BG
	int number=0, y=0, z=0;
#endif
	char letter = '.';
	char opposite = '#';
	bool used = false;
	char *c_state = "up";

	if(state == NODE_STATE_DOWN) {
		letter = '#';
		opposite = '.';
		used = true;
		c_state = "down";
	}
	while((com[i] > 57 || com[i] < 48) && i<len) 
		i++;
	if(i>(len-1)) {
		memset(error_string,0,255);
		sprintf(error_string, 
			"You didn't specify any nodes to make %s. "
			"in statement '%s'", 
			c_state, com);
		return 0;
	}
		
#ifdef HAVE_BG
	if ((com[i+3] == 'x')
	    || (com[i+3] == '-')) {
		for(j=0; j<3; j++) 
			if(com[i+j] > 57 || com[i+j] < 48 || (i+j)>len) 
				goto error_message2;
		number = atoi(com + i);
		start[X] = number / 100;
		start[Y] = (number % 100) / 10;
		start[Z] = (number % 10);
		i += 4;
		for(j=0; j<3; j++) 
			if(com[i+j] > 57 || com[i+j] < 48 || (i+j)>len) 
				goto error_message2;
		number = atoi(com + i);		
		end[X] = number / 100;
		end[Y] = (number % 100) / 10;
		end[Z] = (number % 10);		
		
	} else {
		for(j=0; j<3; j++) 
			if(com[i+j] > 57 || com[i+j] < 48 || (i+j)>len) 
				goto error_message2;
		number = atoi(com + i);
		start[X] = end[X] = number / 100;
		start[Y] = end[Y] = (number % 100) / 10;
		start[Z] = end[Z] = (number % 10);		
	}
	if((start[X]>end[X]
	    || start[Y]>end[Y]
	    || start[Z]>end[Z])
	   || (start[X]<0
	       || start[Y]<0
	       || start[Z]<0)
	   || (end[X]>DIM_SIZE[X]-1
	       || end[Y]>DIM_SIZE[Y]-1
	       || end[Z]>DIM_SIZE[Z]-1))
		goto error_message;
	
	for(x=start[X];x<=end[X];x++) {
		for(y=start[Y];y<=end[Y];y++) {
			for(z=start[Z];z<=end[Z];z++) {
				if(ba_system_ptr->grid[x][y][z].letter 
				   != opposite)
					continue;
				ba_system_ptr->grid[x][y][z].color = 0;
				ba_system_ptr->grid[x][y][z].letter = letter;
				ba_system_ptr->grid[x][y][z].used = used;
			}
		}
	}
#else
	if ((com[i+3] == 'x')
	    || (com[i+3] == '-')) {
		start[X] = atoi(com + i);
		i += 4;
		end[X] = atoi(com + i);
	} else {
		start[X] = end[X] = atoi(com + i);		
	}
	
	if((start[X]>end[X])
	   || (start[X]<0)
	   || (end[X]>DIM_SIZE[X]-1))
		goto error_message;

	for(x=start[X];x<=end[X];x++) {
		ba_system_ptr->grid[x].color = 0;
		ba_system_ptr->grid[x].letter = letter;
		ba_system_ptr->grid[x].used = used;
	}	
#endif
	return 1;
error_message:
	memset(error_string,0,255);
#ifdef HAVE_BG
	sprintf(error_string, 
		"Problem with base partitions, "
		"specified range was %d%d%dx%d%d%d",
		start[X],start[Y],start[Z],
		end[X],end[Y],end[Z]);
#else
	sprintf(error_string, 
		"Problem with nodes,  specified range was %d-%d",
		start[X],end[X]);
#endif	
	return 0;
error_message2:
	memset(error_string,0,255);
	sprintf(error_string, 
		"There was a problem with '%s'\nIn your request '%s'"
		"You need to specify XYZ or XYZxXYZ",
		com+i,com);
	return 0;
}
static int _remove_allocation(char *com, List allocated_blocks)
{
	ListIterator results_i;
	allocated_block_t *allocated_block = NULL;
	int i=6, found=0;
	int len = strlen(com);
	char letter;

	int color_count = 0;		
	while(com[i-1]!=' ' && i<len) {
		i++;
	}
	
	if(i>(len-1)) {
		memset(error_string,0,255);
		sprintf(error_string, 
			"You need to specify which letter to delete.");
		return 0;
	} else {
		letter = com[i];
		results_i = list_iterator_create(allocated_blocks);
		while((allocated_block = list_next(results_i)) != NULL) {
			if(found) {
				if(redo_block(allocated_block->nodes, 
					      allocated_block->
					      request->geometry,
					      allocated_block->
					      request->conn_type, 
					      color_count) == SLURM_ERROR) {
					memset(error_string,0,255);
					sprintf(error_string, 
						"problem redoing the part.");
					return 0;
				}
				allocated_block->letter = 
					letters[color_count%62];
				allocated_block->color =
					colors[color_count%6];
				
			} else if(allocated_block->letter == letter) {
				found=1;
				remove_block(allocated_block->nodes,
					     color_count);
				list_destroy(allocated_block->nodes);
				delete_ba_request(allocated_block->request);
				list_remove(results_i);
				color_count--;
			}
			color_count++;
		}
		list_iterator_destroy(results_i);
	}
		
	return 1;
}

static int _alter_allocation(char *com, List allocated_blocks)
{
	int torus=SELECT_TORUS, i=5, i2=0;
	int len = strlen(com);
	bool rotate = false;
	bool elongate = false;
		
	while(i<len) {
		
		while(com[i-1]!=' ' && i<len) {
			i++;
		}
		if(!strncasecmp(com+i, "mesh", 4)) {
			torus=SELECT_MESH;
			i+=4;
		} else if(!strncasecmp(com+i, "rotate", 6)) {
			rotate=true;
			i+=6;
		} else if(!strncasecmp(com+i, "elongate", 8)) {
			elongate=true;
			i+=8;
		} else if(i2<0 && (com[i] < 58 && com[i] > 47)) {
			i2=i;
			i++;
		} else {
			i++;
		}
		
	}
	return 1;
}

static int _copy_allocation(char *com, List allocated_blocks)
{
	ListIterator results_i;
	allocated_block_t *allocated_block = NULL;
	allocated_block_t *temp_block = NULL;
	ba_request_t *request = NULL; 
	
	int i=0;
	int len = strlen(com);
	char letter = '\0';
	int count = 1;
	int *geo = NULL, *geo_ptr = NULL;
			
	while(com[i-1]!=' ' && i<=len) {
		i++;
	}
	
	if(i<=len) {
		if(com[i]>='0' && com[i]<='9')
			count = atoi(com+i);
		else {
			letter = com[i];
			i++;
			if(com[i]!='\n') {
				while(com[i-1]!=' ' && i<len)
					i++;
				
				if(com[i]>='0' && com[i]<='9')
					count = atoi(com+i);
			}
		}
	}

	results_i = list_iterator_create(allocated_blocks);
	while((allocated_block = list_next(results_i)) != NULL) {
		temp_block = allocated_block;
		if(allocated_block->letter != letter)
			continue;
		break;
	}
	list_iterator_destroy(results_i);
	
	if(!letter)
		allocated_block = temp_block;
	
	if(!allocated_block) {
		memset(error_string,0,255);
		sprintf(error_string, 
			"Could not find requested record to copy");
		return 0;
	}
	
	for(i=0;i<count;i++) {
		request = (ba_request_t*) xmalloc(sizeof(ba_request_t)); 
		
		request->geometry[X] = allocated_block->request->geometry[X];
		request->geometry[Y] = allocated_block->request->geometry[Y];
		request->geometry[Z] = allocated_block->request->geometry[Z];
		request->size = allocated_block->request->size;
		request->conn_type=allocated_block->request->conn_type;
		request->rotate =allocated_block->request->rotate;
		request->elongate = allocated_block->request->elongate;
		request->nodecards = allocated_block->request->nodecards;
		request->quarters = allocated_block->request->quarters;
				
		request->rotate_count= 0;
		request->elongate_count = 0;
	       	request->elongate_geos = list_create(NULL);
	
		results_i = list_iterator_create(request->elongate_geos);
		while ((geo_ptr = list_next(results_i)) != NULL) {
			geo = xmalloc(sizeof(int)*3);
			geo[X] = geo_ptr[X];
			geo[Y] = geo_ptr[Y];
			geo[Z] = geo_ptr[Z];
			
			list_append(request->elongate_geos, geo);
		}
		list_iterator_destroy(results_i);
		
		if((allocated_block = _make_request(request)) == NULL) {
			memset(error_string,0,255);
			sprintf(error_string, 
				"Problem with the copy\n"
				"Are you sure there is enough room for it?");
			xfree(request);
			return 0;
		}
		list_append(allocated_blocks, allocated_block);
		
	}
	return 1;
	
}

static int _save_allocation(char *com, List allocated_blocks)
{
	int len = strlen(com);
	int i=5, j=0;
	allocated_block_t *allocated_block = NULL;
	char filename[20];
	char save_string[255];
	FILE *file_ptr = NULL;
	char *conn_type = NULL;
	char *mode_type = NULL;
	char extra[20];

	ListIterator results_i;		
	
	memset(filename,0,20);
	if(len>5)
		while(i<len) {
			
			while(com[i-1]!=' ' && i<len) {
				i++;
			}
			while(i<len && com[i]!=' ') {
				filename[j] = com[i];
				i++;
				j++;
			}
		}
	if(filename[0]=='\0') {
		ba_system_ptr->now_time = time(NULL);		
		sprintf(filename,"bluegene.conf.%ld",
			(long int) ba_system_ptr->now_time);
	}
	file_ptr = fopen(filename,"w");
	if (file_ptr!=NULL) {
		fputs ("#\n# bluegene.conf file generated by smap\n", file_ptr);
		fputs ("# See the bluegene.conf man page for more information\n",
			file_ptr);
		fputs ("#\n", file_ptr);
		fputs ("BlrtsImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/rts_hw.rts\n", 
		       file_ptr);
		fputs ("LinuxImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/zImage.elf\n", 
		       file_ptr);
		fputs ("MloaderImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/mmcs-mloader.rts\n",
		       file_ptr);
		fputs ("RamDiskImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/ramdisk.elf\n", 
		       file_ptr);
		fputs ("BridgeAPILogFile="
		       "/var/log/slurm/bridgeapi.log\n", 
		       file_ptr);
		fputs ("Numpsets=8\n", file_ptr);
		fputs ("BridgeAPIVerbose=0\n", file_ptr);
		sprintf(save_string, "BasePartitionNodeCnt=%d\n\0",
			base_part_node_cnt);
		fputs (save_string,file_ptr);
		sprintf(save_string, "NodeCardNodeCnt=%d\n\0",
			nodecard_node_cnt);
		fputs (save_string,file_ptr);
		sprintf(save_string, "LayoutMode=%s\n\0",
			layout_mode);
		fputs (save_string,file_ptr);

		fputs("#\n# Block Layout\n#\n", file_ptr);
		results_i = list_iterator_create(allocated_blocks);
		while((allocated_block = list_next(results_i)) != NULL) {
			memset(save_string,0,255);
			memset(extra,0,20);
			if(allocated_block->request->conn_type == SELECT_TORUS)
				conn_type = "TORUS";
			else if(allocated_block->request->conn_type 
				== SELECT_MESH)
				conn_type = "MESH";
			else {
				conn_type = "SMALL";
				sprintf(extra, " NodeCards=%d Quarters=%d\0",
					allocated_block->request->nodecards,
					allocated_block->request->quarters);
			}
			sprintf(save_string, "BPs=%s Type=%s%s\n", 
				allocated_block->request->save_name, 
				conn_type, extra);
			fputs (save_string,file_ptr);
		}
		fclose (file_ptr);
	}
	return 1;
}

/* Explicitly strip out  new-line and carriage-return */
static void _strip_13_10(char *line)
{
	int len = strlen(line);
	int i;

	for(i=0;i<len;i++) {
		if(line[i]==13 || line[i]==10) {
			line[i] = '\0';
			return;
		}
	}
}

static int _parse_bg_spec(char *in_line, List allocated_blocks)
{
#ifdef HAVE_BG
	int error_code = SLURM_SUCCESS;
	char *nodes = NULL, *conn_type = NULL;
	int bp_count = 0;
	int start[BA_SYSTEM_DIMENSIONS];
	int end[BA_SYSTEM_DIMENSIONS];
	int start1[BA_SYSTEM_DIMENSIONS];
	int end1[BA_SYSTEM_DIMENSIONS];
	int geo[BA_SYSTEM_DIMENSIONS];
	char *layout = NULL;
	int pset_num=-1, api_verb=-1, num_nodecard=0, num_quarter=0;
	char com[255];
	int j = 0, number;
	int len = 0;
	int x,y,z;
	
	geo[X] = 0;
	geo[Y] = 0;
	geo[Z] = 0;
	
	end1[X] = -1;
	end1[Y] = -1;
	end1[Z] = -1;
	
	error_code = slurm_parser(in_line,
				  "Numpsets=", 'd', &pset_num,
				  "BasePartitionNodeCnt=", 'd', 
				  &base_part_node_cnt,
				  "NodeCardNodeCnt=", 'd', &nodecard_node_cnt,
				  "LayoutMode=", 's', &layout,
				  "BPs=", 's', &nodes,
				  "Nodes=", 's', &nodes,
				  "Type=", 's', &conn_type,
				  "NodeCards=", 'd', &num_nodecard,
				  "Quarters=", 'd', &num_quarter,
				  "END");
	if(layout)
		_set_layout(layout);
	if(!nodes)
		return SLURM_SUCCESS;
	len = strlen(nodes);
	while (nodes[j] != '\0') {
		if(j > len)
			break;
		else if ((nodes[j] == '[' || nodes[j] == ',')
		    && (nodes[j+8] == ']' || nodes[j+8] == ',')
		    && (nodes[j+4] == 'x' || nodes[j+4] == '-')) {
			j++;
			number = atoi(nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j += 4;
			number = atoi(nodes + j);
			end[X] = number / 100;
			end[Y] = (number % 100) / 10;
			end[Z] = (number % 10);
			j += 3;
			if(!bp_count) {
				start1[X] = start[X];
				start1[Y] = start[Y];
				start1[Z] = start[Z];
			}
			for (x = start[X]; x <= end[X]; x++) 
				for (y = start[Y]; y <= end[Y]; y++) 
					for (z = start[Z]; z <= end[Z]; z++) {
						if(x>end1[X]) {
						        geo[X]++;
							end1[X] = x;
						}
						if(y>end1[Y]) {
							geo[Y]++;
							end1[Y] = y;
						}
						if(z>end1[Z]) {
							geo[Z]++;
							end1[Z] = z;
						}
						bp_count++;
					}
			if(nodes[j] != ',')
				break;
			j--;
		} else if((nodes[j] < 58 && nodes[j] > 47)) {
			number = atoi(nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j+=3;
			if(!bp_count) {
				start1[X] = start[X];
				start1[Y] = start[Y];
				start1[Z] = start[Z];
			}
			if(start[X]>end1[X]) {
				geo[X]++;
				end1[X] = start[X];
			}
			if(start[Y]>end1[Y]) {
				geo[Y]++;
				end1[Y] = start[Y];
			}
			if(start[Z]>end1[Z]) {
				geo[Z]++;
				end1[Z] = start[Z];
			}
			bp_count++;
			if(nodes[j] != ',')
				break;
		}
		j++;
	}
	memset(com,0,255);
	sprintf(com,"create %dx%dx%d %s start %dx%dx%d "
		"nodecards=%d quarters=%d",
		geo[X], geo[Y], geo[Z], conn_type, 
		start1[X], start1[Y], start1[Z],
		num_nodecard, num_quarter);
	_create_allocation(com, allocated_blocks);
#endif
	return SLURM_SUCCESS;
}
static int _load_configuration(char *com, List allocated_blocks)
{
	int len = strlen(com);
	int i=5, j=0;
	char filename[100];
	FILE *file_ptr = NULL;
	char in_line[BUFSIZE];	/* input line */
	int line_num = 0;	/* line number in input file */
	
	ListIterator results_i;		
	_delete_allocated_blocks(allocated_blocks);
	allocated_blocks = list_create(NULL);

	memset(filename,0,100);
	if(len>5)
		while(i<len) {			
			while(com[i-1]!=' ' && i<len) {
				i++;
			}
			while(i<len && com[i]!=' ') {
				filename[j] = com[i];
				i++;
				j++;
				if(j>100) {
					memset(error_string,0,255);
					sprintf(error_string, 
						"filename is too long needs "
						"to be under 100 chars");
					return 0;
				}
			}
		}
		
	if(filename[0]=='\0') {
		sprintf(filename,"bluegene.conf");
	}
	file_ptr = fopen(filename,"r");
	if (file_ptr==NULL) {
		memset(error_string,0,255);
		sprintf(error_string, "problem reading file %s", filename);
		return 0;
	}

	while (fgets(in_line, BUFSIZE, file_ptr) != NULL) {
		line_num++;
		_strip_13_10(in_line);
		if (strlen(in_line) >= (BUFSIZE - 1)) {
			memset(error_string,0,255);
			sprintf(error_string, 
				"_read_bg_config line %d, of input file %s "
				"too long", line_num, filename);
			fclose(file_ptr);
			return 0;
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
		/* block configuration parameters */
		_parse_bg_spec(in_line, allocated_blocks);
	}
	fclose(file_ptr);
	
	return 1;
}

static void _print_header_command(void)
{
	ba_system_ptr->ycord=2;
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "ID");
	ba_system_ptr->xcord += 4;
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "TYPE");
	ba_system_ptr->xcord += 7;
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "ROTATE");
	ba_system_ptr->xcord += 7;
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "ELONG");
	ba_system_ptr->xcord += 7;
#ifdef HAVE_BG
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "BP_COUNT");
#else
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "NODES");
#endif
	ba_system_ptr->xcord += 10;
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "NODECARDS");
	ba_system_ptr->xcord += 11;
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "QUARTERS");
	ba_system_ptr->xcord += 10;
#ifdef HAVE_BG
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "BP_LIST");
#else
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "NODELIST");
#endif
	ba_system_ptr->xcord = 1;
	ba_system_ptr->ycord++;
}

static void _print_text_command(allocated_block_t *allocated_block)
{
	wattron(ba_system_ptr->text_win,
		COLOR_PAIR(allocated_block->color));
			
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "%c",allocated_block->letter);
	ba_system_ptr->xcord += 4;
	if(allocated_block->request->conn_type==SELECT_TORUS) 
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "TORUS");
	else if (allocated_block->request->conn_type==SELECT_MESH)
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "MESH");
	else 
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "SMALL");
	ba_system_ptr->xcord += 7;
				
	if(allocated_block->request->rotate)
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "Y");
	else
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "N");
	ba_system_ptr->xcord += 7;
				
	if(allocated_block->request->elongate)
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "Y");
	else
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "N");
	ba_system_ptr->xcord += 7;

	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "%d",allocated_block->request->size);
	ba_system_ptr->xcord += 10;
	
	if(allocated_block->request->conn_type == SELECT_SMALL) {
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%d", 
			  allocated_block->request->nodecards);
		ba_system_ptr->xcord += 11;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%d", 
			  allocated_block->request->quarters);
		ba_system_ptr->xcord += 10;
		
	} else
		ba_system_ptr->xcord += 21;
	
	mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
		  ba_system_ptr->xcord, "%s",
		  allocated_block->request->save_name);
	ba_system_ptr->xcord = 1;
	ba_system_ptr->ycord++;
	wattroff(ba_system_ptr->text_win,
		 COLOR_PAIR(allocated_block->color));
	return;
}

void get_command(void)
{
	char com[255];
	
	int text_width, text_startx;
	allocated_block_t *allocated_block = NULL;
	int i=0;
	int count=0;
	
	WINDOW *command_win;
        List allocated_blocks;
	ListIterator results_i;
		
	if(params.commandline) {
		printf("Configure won't work with commandline mode.\n");
		printf("Please remove the -c from the commandline.\n");
		ba_fini();
		exit(0);
	}
	init_wires();
	allocated_blocks = list_create(NULL);
				
	text_width = ba_system_ptr->text_win->_maxx;	
	text_startx = ba_system_ptr->text_win->_begx;
	command_win = newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();
	
	while (strcmp(com, "quit")) {
		clear_window(ba_system_ptr->grid_win);
		print_grid(0);
		clear_window(ba_system_ptr->text_win);
		box(ba_system_ptr->text_win, 0, 0);
		box(ba_system_ptr->grid_win, 0, 0);
		
		if (!params.no_header)
			_print_header_command();

		if(error_string!=NULL) {
			i=0;
			while(error_string[i]!='\0') {
				if(error_string[i]=='\n') {
					ba_system_ptr->ycord++;
					ba_system_ptr->xcord=1;
					i++;
				}
				mvwprintw(ba_system_ptr->text_win, 
					  ba_system_ptr->ycord,
					  ba_system_ptr->xcord, 
					  "%c",
					  error_string[i++]);
				ba_system_ptr->xcord++;
			}
			ba_system_ptr->ycord++;
			ba_system_ptr->xcord=1;	
			memset(error_string,0,255);			
		}
		results_i = list_iterator_create(allocated_blocks);
		
		count = list_count(allocated_blocks) 
			- (LINES-(ba_system_ptr->ycord+5)); 
		
		if(count<0)
			count=0;
		i=0;
		while((allocated_block = list_next(results_i)) != NULL) {
			if(i>=count)
				_print_text_command(allocated_block);
			i++;
		}
		list_iterator_destroy(results_i);		
		
		wnoutrefresh(ba_system_ptr->text_win);
		wnoutrefresh(ba_system_ptr->grid_win);
		doupdate();
		clear_window(command_win);
		
		box(command_win, 0, 0);
		mvwprintw(command_win, 0, 3,
			  "Input Command: (type quit to change view, "
			  "exit to exit)");
		wmove(command_win, 1, 1);
		wgetstr(command_win, com);
		
		if (!strcmp(com, "exit")) {
			endwin();
			_delete_allocated_blocks(allocated_blocks);
			ba_fini();
			exit(0);
		} if (!strcmp(com, "quit")) {
			break;
		} else if (!strncasecmp(com, "layout", 6)) {
			_set_layout(com);
		} else if (!strncasecmp(com, "basepartition", 13)) {
			_set_base_part_cnt(com);
		} else if (!strncasecmp(com, "nodecard", 8)) {
			_set_nodecard_cnt(com);
		} else if (!strncasecmp(com, "resolve", 7) ||
			   !strncasecmp(com, "r ", 2)) {
			_resolve(com);
		} else if (!strncasecmp(com, "resume", 6)) {
			mvwprintw(ba_system_ptr->text_win,
				ba_system_ptr->ycord,
				ba_system_ptr->xcord, "%s", com);
		} else if (!strncasecmp(com, "drain", 5)) {
			mvwprintw(ba_system_ptr->text_win, 
				ba_system_ptr->ycord, 
				ba_system_ptr->xcord, "%s", com);
		} else if (!strncasecmp(com, "alldown", 7)) {
			_change_state_all_bps(com, NODE_STATE_DOWN);
		} else if (!strncasecmp(com, "down", 4)) {
			_change_state_bps(com, NODE_STATE_DOWN);
		} else if (!strncasecmp(com, "allup", 5)) {
			_change_state_all_bps(com, NODE_STATE_IDLE);
		} else if (!strncasecmp(com, "up", 2)) {
			_change_state_bps(com, NODE_STATE_IDLE);
		} else if (!strncasecmp(com, "remove", 6)
			|| !strncasecmp(com, "delete", 6) 
			|| !strncasecmp(com, "drop", 4)) {
			_remove_allocation(com, allocated_blocks);
		} else if (!strncasecmp(com, "alter", 5)) {
			_alter_allocation(com, allocated_blocks);
		} else if (!strncasecmp(com, "create", 6)) {
			_create_allocation(com, allocated_blocks);
		} else if (!strncasecmp(com, "copy", 4)
			|| !strncasecmp(com, "c ", 2) 
			|| !strncasecmp(com, "c\0", 2)) {
			_copy_allocation(com, allocated_blocks);
		} else if (!strncasecmp(com, "save", 4)) {
			_save_allocation(com, allocated_blocks);
		} else if (!strncasecmp(com, "load", 4)) {
			_load_configuration(com, allocated_blocks);
		} else if (!strncasecmp(com, "clear all", 9)
			|| !strncasecmp(com, "clear", 5)) {
			_delete_allocated_blocks(allocated_blocks);
			allocated_blocks = list_create(NULL);
		} else {
			memset(error_string,0,255);
			sprintf(error_string, "Unknown command '%s'",com);
		}
	}
	_delete_allocated_blocks(allocated_blocks);
	params.display = 0;
	noecho();
	
	clear_window(ba_system_ptr->text_win);
	ba_system_ptr->xcord = 1;
	ba_system_ptr->ycord = 1;
	print_date();
	get_job(0);
	return;
}
