/*****************************************************************************\
 *  configure_functions.c - Functions related to configure mode of smap.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  UCRL-CODE-2002-040.
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
	pa_request_t *request; 
} allocated_part_t;

static void	_delete_allocated_parts(List allocated_partitions);
static allocated_part_t *_make_request(pa_request_t *request);
static int	_create_allocation(char *com, List allocated_partitions);
static int	_resolve(char *com);
static int	_down_bps(char *com);
static int	_remove_allocation(char *com, List allocated_partitions);
static int	_alter_allocation(char *com, List allocated_partitions);
static int	_copy_allocation(char *com, List allocated_partitions);
static int	_save_allocation(char *com, List allocated_partitions);
static void	_print_header_command(void);
static void	_print_text_command(allocated_part_t *allocated_part);

char error_string[255];

static void _delete_allocated_parts(List allocated_partitions)
{
	allocated_part_t *allocated_part = NULL;
	
	while ((allocated_part = list_pop(allocated_partitions)) != NULL) {
		remove_part(allocated_part->nodes,0);
		list_destroy(allocated_part->nodes);
		delete_pa_request(allocated_part->request);
		xfree(allocated_part);
	}
	list_destroy(allocated_partitions);
}

static allocated_part_t *_make_request(pa_request_t *request)
{
	List results = list_create(NULL);
	ListIterator results_i;		
	allocated_part_t *allocated_part = NULL;
	pa_node_t *current = NULL;
	
	if (!allocate_part(request, results)){
		memset(error_string,0,255);
		sprintf(error_string,"allocate failure for %d%d%d", 
			  request->geometry[0], request->geometry[1], 
			  request->geometry[2]);
		return NULL;
	} else {
				
		allocated_part = (allocated_part_t *)xmalloc(
			sizeof(allocated_part_t));
		allocated_part->request = request;
		allocated_part->nodes = list_create(NULL);
		results_i = list_iterator_create(results);
		while ((current = list_next(results_i)) != NULL) {
			list_append(allocated_part->nodes,current);
			allocated_part->color = current->color;
			allocated_part->letter = current->letter;
		}
		list_iterator_destroy(results_i);
	}
	list_destroy(results);
	return(allocated_part);

}

static int _create_allocation(char *com, List allocated_partitions)
{
	int i=6, i2=-1, i3=0;
	int len = strlen(com);
	
	allocated_part_t *allocated_part = NULL;
	pa_request_t *request = (pa_request_t*) xmalloc(sizeof(pa_request_t)); 
	
	request->geometry[0] = -1;
	request->conn_type=TORUS;
	request->rotate = false;
	request->elongate = false;
	request->force_contig = false;

	while(i<len) {
		
		while(com[i-1]!=' ' && i<len) {
			i++;
		}
		if(!strncasecmp(com+i, "mesh", 4)) {
			request->conn_type=MESH;
			i+=4;
		} else if(!strncasecmp(com+i, "rotate", 6)) {
			request->rotate=true;
			i+=6;
		} else if(!strncasecmp(com+i, "elongate", 8)) {
			request->elongate=true;
			i+=8;
		} else if(!strncasecmp(com+i, "force", 5)) {
			request->force_contig=true;
			i+=5;
		} else if(i2<0 && (com[i] < 58 && com[i] > 47)) {
			i2=i;
			i++;
		} else {
			i++;
		}
		
	}
	
	if(i2<0) {
		memset(error_string,0,255);
		sprintf(error_string, 
			"No size or dimension specified, please re-enter");
	} else {
		i3=i2;
		while(i3<len) {
			if(com[i3]==' ' || i3==(len-1)) {
				/* for size */
				request->size = atoi(&com[i2]);
				break;
			}
			if(com[i3]=='x') {
				
				/* for geometery */
				request->geometry[0] = atoi(&com[i2]);
				i2++;
				while(com[i2-1]!='x' && i2<len)
					i2++;
				if(i2==len){
					memset(error_string,0,255);
					sprintf(error_string, 
						"Error in dimension "
						"specified, please re-enter");
					break;
				} 
				request->geometry[1] = atoi(&com[i2]);
				i2++;
				while(com[i2-1]!='x' && i2<len)
					i2++;
				if(i2==len){
					memset(error_string,0,255);
					sprintf(error_string, 
						"Error in dimension "
						"specified, please re-enter");
					break;
				} 
				request->geometry[2] = atoi(&com[i2]);
				request->size = -1;
				break;
			}
			i3++;
		}
	
		/*
		  Here is where we do the allocating of the partition. 
		  It will send a request back which we will throw into
		  a list just incase we change something later.		   
		*/
		if(!new_pa_request(request)) {
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
					"Problems with request for %d%d%d\n"
					"Either you put in something "
					"that doesn't work,\n"
					"or we are unable to process "
					"your request.", 
					request->geometry[0], 
					request->geometry[1], 
					request->geometry[2]);
		} else {
			if((allocated_part = _make_request(request)) != NULL)
				list_append(allocated_partitions, 
					    allocated_part);
		}
	}
	
	return 1;
}

static int _resolve(char *com)
{
	int i=0;
#ifdef HAVE_BGL_FILES
	int len=strlen(com);
	char *rack_mid = NULL;
	int *coord = NULL;
#endif
	
	while(com[i-1] != ' ' && com[i] != '\0')
		i++;
	if(com[i] == 'r')
		com[i] = 'R';
		
	memset(error_string,0,255);		
#ifdef HAVE_BGL_FILES
	if (!have_db2) {
		sprintf(error_string, "Must be on BGL SN to resolve\n"); 
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
				"%s resolves to X=%d Y=%d Z=%d or bgl%d%d%d\n",
				com+i,coord[X],coord[Y],coord[Z],
				coord[X],coord[Y],coord[Z]);
		else
			sprintf(error_string, "%s has no resolve.\n", 
				com+i);	
	}
resolve_error:
#else
			sprintf(error_string, 
				"Must be on BGL SN to resolve.\n"); 
#endif
	wnoutrefresh(pa_system_ptr->text_win);
	doupdate();

	return 1;
}
static int _down_bps(char *com)
{
	int i=4,x;
	int len = strlen(com);
	int start[SYSTEM_DIMENSIONS], end[SYSTEM_DIMENSIONS];
#ifdef HAVE_BGL
	int number=0, y=0, z=0;
#endif

	while(com[i-1] != ' ' && i<len)
		i++;
	if(i>(len-1)) {
		memset(error_string,0,255);
		sprintf(error_string, 
			"You didn't specify any nodes to down.");	
		return 0;
	}
		
#ifdef HAVE_BGL
	if ((com[i]   == '[')
	    && (com[i+8] == ']')
	    && ((com[i+4] == 'x')
		|| (com[i+4] == '-'))) {
		i++;
		number = atoi(com + i);
		start[X] = number / 100;
		start[Y] = (number % 100) / 10;
		start[Z] = (number % 10);
		i += 4;
		number = atoi(com + i);
		end[X] = number / 100;
		end[Y] = (number % 100) / 10;
		end[Z] = (number % 10);		
		
	} else if ((com[i] < 58 && com[i] > 47)
		   && (com[i+6] < 58 && com[i+6] > 47)
		   && ((com[i+3] == 'x')
		|| (com[i+3] == '-'))) {
		
		number = atoi(com + i);
		start[X] = number / 100;
		start[Y] = (number % 100) / 10;
		start[Z] = (number % 10);
		i += 4;
		number = atoi(com + i);
		end[X] = number / 100;
		end[Y] = (number % 100) / 10;
		end[Z] = (number % 10);		
		
	} else if((com[i] < 58 && com[i] > 47) 
		  && com[i-1] != '[') {
		number = atoi(com + i);
		start[X] = end[X] = number / 100;
		start[Y] = end[Y] = (number % 100) / 10;
		start[Z] = end[Z] = (number % 10);		
	}

	for(x=start[X];x<=end[X];x++) {
		for(y=start[Y];y<=end[Y];y++) {
			for(z=start[Z];z<=end[Z];z++) {
				pa_system_ptr->grid[x][y][z].color = 0;
				pa_system_ptr->grid[x][y][z].letter = '#';
				pa_system_ptr->grid[x][y][z].used = true;
			}
		}
	}
#else
	if ((com[i]   == '[')
	    && (com[i+8] == ']')
	    && ((com[i+4] == 'x')
		|| (com[i+4] == '-'))) {
		i++;
		start[X] = atoi(com + i);
		i += 4;
		end[X] = atoi(com + i);	
	} else if ((com[i] < 58 && com[i] > 47)
		   && (com[i+6] < 58 && com[i+6] > 47)
		   && ((com[i+3] == 'x')
		|| (com[i+3] == '-'))) {
		
		start[X] = atoi(com + i);
		i += 4;
		end[X] = atoi(com + i);		
	} else if((com[i] < 58 && com[i] > 47) 
		  && com[i-1] != '[') {
		start[X] = end[X] = atoi(com + i);
				
	}

	for(x=start[X];x<=end[X];x++) {
		pa_system_ptr->grid[x].color = 0;
		pa_system_ptr->grid[x].letter = '#';
		pa_system_ptr->grid[x].used = true;
	}	
#endif
	return 1;
}
static int _remove_allocation(char *com, List allocated_partitions)
{
	ListIterator results_i;
	allocated_part_t *allocated_part = NULL;
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
		results_i = list_iterator_create(allocated_partitions);
		while((allocated_part = list_next(results_i)) != NULL) {
			if(found) {
				if(redo_part(allocated_part->nodes, 
					     allocated_part->request->geometry,
					     allocated_part->
					     request->conn_type, 
					     color_count) == SLURM_ERROR) {
					memset(error_string,0,255);
					sprintf(error_string, 
						"problem redoing the part.");
					return 0;
				}
				allocated_part->letter = 
					letters[color_count%62];
				allocated_part->color =
					colors[color_count%6];
				
			} else if(allocated_part->letter == letter) {
				found=1;
				remove_part(allocated_part->nodes,color_count);
				list_destroy(allocated_part->nodes);
				delete_pa_request(allocated_part->request);
				list_remove(results_i);
				color_count--;
			}
			color_count++;
		}
		list_iterator_destroy(results_i);
	}
		
	return 1;
}

static int _alter_allocation(char *com, List allocated_partitions)
{
	int torus=TORUS, i=5, i2=0;
	int len = strlen(com);
	bool rotate = false;
	bool elongate = false;
	bool force_contig = false;
		
	while(i<len) {
		
		while(com[i-1]!=' ' && i<len) {
			i++;
		}
		if(!strncasecmp(com+i, "mesh", 4)) {
			torus=MESH;
			i+=4;
		} else if(!strncasecmp(com+i, "rotate", 6)) {
			rotate=true;
			i+=6;
		} else if(!strncasecmp(com+i, "elongate", 8)) {
			elongate=true;
			i+=8;
		} else if(!strncasecmp(com+i, "force", 5)) {
			force_contig=true;				
			i+=5;
		} else if(i2<0 && (com[i] < 58 && com[i] > 47)) {
			i2=i;
			i++;
		} else {
			i++;
		}
		
	}
	return 1;
}

static int _copy_allocation(char *com, List allocated_partitions)
{
	ListIterator results_i;
	allocated_part_t *allocated_part = NULL;
	allocated_part_t *temp_part = NULL;
	pa_request_t *request = NULL; 
	
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

	results_i = list_iterator_create(allocated_partitions);
	while((allocated_part = list_next(results_i)) != NULL) {
		temp_part = allocated_part;
		if(allocated_part->letter != letter)
			continue;
		break;
	}
	list_iterator_destroy(results_i);
	
	if(!letter)
		allocated_part = temp_part;
	
	if(!allocated_part) {
		memset(error_string,0,255);
		sprintf(error_string, 
			"Could not find requested record to copy");
		return 0;
	}
	
	for(i=0;i<count;i++) {
		request = (pa_request_t*) xmalloc(sizeof(pa_request_t)); 
		
		request->geometry[X] = allocated_part->request->geometry[X];
		request->geometry[Y] = allocated_part->request->geometry[Y];
		request->geometry[Z] = allocated_part->request->geometry[Z];
		request->size = allocated_part->request->size;
		request->conn_type=allocated_part->request->conn_type;
		request->rotate =allocated_part->request->rotate;
		request->elongate = allocated_part->request->elongate;
		request->force_contig = allocated_part->request->force_contig;
				
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
		
		if((allocated_part = _make_request(request)) == NULL) {
			memset(error_string,0,255);
			sprintf(error_string, 
				"Problem with the copy\n"
				"Are you sure there is enough room for it?");
			xfree(request);
			return 0;
		}
		list_append(allocated_partitions, allocated_part);
		
	}
	return 1;
	
}

static int _save_allocation(char *com, List allocated_partitions)
{
	int len = strlen(com);
	int i=5, j=0;
	allocated_part_t *allocated_part = NULL;
	char filename[20];
	char save_string[255];
	FILE *file_ptr = NULL;
	char *conn_type = NULL;
	char *mode_type = NULL;

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
		pa_system_ptr->now_time = time(NULL);		
		sprintf(filename,"bluegene.conf.%ld",
			(long int) pa_system_ptr->now_time);
	}
	file_ptr = fopen(filename,"w");
	if (file_ptr!=NULL) {
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

		results_i = list_iterator_create(allocated_partitions);
		while((allocated_part = list_next(results_i)) != NULL) {
			memset(save_string,0,255);
			if(allocated_part->request->conn_type == TORUS)
				conn_type = "TORUS";
			else
				conn_type = "MESH";
			
			sprintf(save_string, "Nodes=%s Type=%s\n", 
				allocated_part->request->save_name, 
				conn_type);
			fputs (save_string,file_ptr);
		}
		fclose (file_ptr);
	}
	return 1;
}

static void _print_header_command(void)
{
	pa_system_ptr->ycord=2;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "ID");
	pa_system_ptr->xcord += 4;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "TYPE");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "CONTIG");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "ROTATE");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "ELONG");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODES");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODELIST");
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;
}

static void _print_text_command(allocated_part_t *allocated_part)
{
	wattron(pa_system_ptr->text_win,
		COLOR_PAIR(allocated_part->color));
			
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%c",allocated_part->letter);
	pa_system_ptr->xcord += 4;
	if(allocated_part->request->conn_type==TORUS) 
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "TORUS");
	else
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "MESH");	
	pa_system_ptr->xcord += 7;
				
	if(allocated_part->request->force_contig)
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "Y");
	else
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "N");
	pa_system_ptr->xcord += 7;
				
	if(allocated_part->request->rotate)
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "Y");
	else
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "N");
	pa_system_ptr->xcord += 7;
				
	if(allocated_part->request->elongate)
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "Y");
	else
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "N");
	pa_system_ptr->xcord += 7;

	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%d",allocated_part->request->size);
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%s",
		  allocated_part->request->save_name);
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;
	wattroff(pa_system_ptr->text_win,
		 COLOR_PAIR(allocated_part->color));
	return;
}

void get_command(void)
{
	char com[255];
	
	int text_width, text_startx;
	allocated_part_t *allocated_part = NULL;
	int i=0;
	int count=0;
	
	WINDOW *command_win;
        List allocated_partitions;
	ListIterator results_i;
		
	if(params.commandline) {
		printf("Configure won't work with commandline mode.\n");
		printf("Please remove the -c from the commandline.\n");
		pa_fini();
		exit(0);
	}
	init_wires();
	allocated_partitions = list_create(NULL);
				
	text_width = pa_system_ptr->text_win->_maxx;	
	text_startx = pa_system_ptr->text_win->_begx;
	command_win = newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();
	
	while (strcmp(com, "quit")) {
		clear_window(pa_system_ptr->grid_win);
		print_grid(0);
		clear_window(pa_system_ptr->text_win);
		box(pa_system_ptr->text_win, 0, 0);
		box(pa_system_ptr->grid_win, 0, 0);
		
		if (!params.no_header)
			_print_header_command();

		if(error_string!=NULL) {
			i=0;
			while(error_string[i]!='\0') {
				if(error_string[i]=='\n') {
					pa_system_ptr->ycord++;
					pa_system_ptr->xcord=1;
					i++;
				}
				mvwprintw(pa_system_ptr->text_win, 
					  pa_system_ptr->ycord,
					  pa_system_ptr->xcord, 
					  "%c",
					  error_string[i++]);
				pa_system_ptr->xcord++;
			}
			pa_system_ptr->ycord++;
			pa_system_ptr->xcord=1;	
			memset(error_string,0,255);			
		}
		results_i = list_iterator_create(allocated_partitions);
		
		count = list_count(allocated_partitions) 
			- (LINES-(pa_system_ptr->ycord+5)); 
		
		if(count<0)
			count=0;
		i=0;
		while((allocated_part = list_next(results_i)) != NULL) {
			if(i>=count)
				_print_text_command(allocated_part);
			i++;
		}
		list_iterator_destroy(results_i);		
		
		wnoutrefresh(pa_system_ptr->text_win);
		wnoutrefresh(pa_system_ptr->grid_win);
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
			_delete_allocated_parts(allocated_partitions);
			pa_fini();
			exit(0);
		} if (!strcmp(com, "quit")) {
			break;
		} else if (!strncasecmp(com, "resolve", 7) ||
			   !strncasecmp(com, "r ", 2)) {
			_resolve(com);
		} else if (!strncasecmp(com, "resume", 6)) {
			mvwprintw(pa_system_ptr->text_win,
				pa_system_ptr->ycord,
				pa_system_ptr->xcord, "%s", com);
		} else if (!strncasecmp(com, "drain", 5)) {
			mvwprintw(pa_system_ptr->text_win, 
				pa_system_ptr->ycord, 
				pa_system_ptr->xcord, "%s", com);
		} else if (!strncasecmp(com, "down", 4)) {
			_down_bps(com);
		} else if (!strncasecmp(com, "remove", 6)
			|| !strncasecmp(com, "delete", 6) 
			|| !strncasecmp(com, "drop", 4)) {
			_remove_allocation(com, allocated_partitions);
		} else if (!strncasecmp(com, "alter", 5)) {
			_alter_allocation(com, allocated_partitions);
		} else if (!strncasecmp(com, "create", 6)) {
			_create_allocation(com, allocated_partitions);
		} else if (!strncasecmp(com, "copy", 4)
			|| !strncasecmp(com, "c ", 2) 
			|| !strncasecmp(com, "c\0", 2)) {
			_copy_allocation(com, allocated_partitions);
		} else if (!strncasecmp(com, "save", 4)) {
			_save_allocation(com, allocated_partitions);
		} else if (!strncasecmp(com, "clear all", 9)
			|| !strncasecmp(com, "clear", 5)) {
			_delete_allocated_parts(allocated_partitions);
			allocated_partitions = list_create(NULL);
		} else {
			memset(error_string,0,255);
			sprintf(error_string, "Unknown command '%s'",com);
		}
	}
	_delete_allocated_parts(allocated_partitions);
	params.display = 0;
	noecho();
	
	clear_window(pa_system_ptr->text_win);
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord = 1;
	print_date();
	get_job(0);
	return;
}
