/*****************************************************************************\
 *  configure_functions.c - Functions related to configure mode of smap.
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

void _delete_allocated_parts(List allocated_partitions);
allocated_part_t *_make_request(pa_request_t *request);
int _create_allocation(char *com, List allocated_partitions);
int _remove_allocation(char *com, List allocated_partitions);
int _alter_allocation(char *com, List allocated_partitions);
int _copy_allocation(char *com, List allocated_partitions);
int _save_allocation(char *com, List allocated_partitions);
void _print_header_command();
void _print_text_command(allocated_part_t *allocated_part);

char error_string[255];

void _delete_allocated_parts(List allocated_partitions)
{
	allocated_part_t *allocated_part;
	
	while ((allocated_part = list_pop(allocated_partitions)) != NULL) {
		remove_part(allocated_part->nodes,0);
		list_destroy(allocated_part->nodes);
		delete_pa_request(allocated_part->request);
		xfree(allocated_part);
	}
	list_destroy(allocated_partitions);
}

allocated_part_t *_make_request(pa_request_t *request)
{
	List results = list_create(NULL);
	ListIterator results_i;		
	allocated_part_t *allocated_part;
	pa_node_t *current;
	
	if (!allocate_part(request, results)){
		memset(error_string,0,255);
		sprintf(error_string,"allocate failure for %d%d%d", 
			  request->geometry[0], request->geometry[1], request->geometry[2]);
		return NULL;
	} else {
				
		allocated_part = (allocated_part_t *)xmalloc(sizeof(allocated_part_t));
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

int _create_allocation(char *com, List allocated_partitions)
{
	int i=6, i2=-1, i3=0;
	int len = strlen(com);
	
	allocated_part_t *allocated_part;
	pa_request_t *request = (pa_request_t*) xmalloc(sizeof(pa_request_t)); 
	
	request->geometry[0] = -1;
	request->conn_type=MESH;
	request->rotate = false;
	request->elongate = false;
	request->force_contig = false;
	request->co_proc = false;
	
	while(i<len) {
		
		while(com[i-1]!=' ' && i<len) {
			i++;
		}
		if(!strncmp(com+i, "torus", 5) || !strncmp(com+i, "TORUS", 5) || !strncmp(com+i, "Torus", 5)) {
			request->conn_type=TORUS;
			i+=5;
		} else if(!strncmp(com+i, "rotate", 6) || !strncmp(com+i, "ROTATE", 6) || !strncmp(com+i, "Rotate", 6)) {
			request->rotate=true;
			i+=6;
		} else if(!strncmp(com+i, "elongate", 8) || !strncmp(com+i, "ELONGATE", 8) || !strncmp(com+i, "Elongate", 8)) {
			request->elongate=true;
			i+=8;
		} else if(!strncmp(com+i, "force", 5) || !strncmp(com+i, "FORCE", 5) || !strncmp(com+i, "Force", 5)) {
			request->force_contig=true;				
			i+=5;
		} else if(!strncmp(com+i, "proc", 4) || !strncmp(com+i, "PROC", 4) || !strncmp(com+i, "Proc", 4)) {
			request->co_proc=true;				
			i+=4;
		} else if(i2<0 && (com[i] < 58 && com[i] > 47)) {
			i2=i;
			i++;
		} else {
			i++;
		}
		
	}
	
	if(i2<0) {
		memset(error_string,0,255);
		sprintf(error_string, "No size or dimension specified, please re-enter");
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
					sprintf(error_string, "Error in dimension specified, please re-enter");
					break;
				} 
				request->geometry[1] = atoi(&com[i2]);
				i2++;					while(com[i2-1]!='x' && i2<len)
					i2++;
				if(i2==len){
					memset(error_string,0,255);
					sprintf(error_string, "Error in dimension specified, please re-enter");
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
			if(request->size!=-1)
			sprintf(error_string, "Problems with request for %d\nEither you put in something that doesn't work,\nor we are unable to process your request.", 
				  request->size);
			else
				sprintf(error_string, "Problems with request for %d%d%d\nEither you put in something that doesn't work,\nor we are unable to process your request.", 
				  request->geometry[0], request->geometry[1], request->geometry[2]);
		} else {
			if((allocated_part = _make_request(request)) != NULL)
				list_append(allocated_partitions, allocated_part);
		}
	}
	
	return 1;
}

int _remove_allocation(char *com, List allocated_partitions)
{
	ListIterator results_i;
	allocated_part_t *allocated_part;
	int i=6, found=0;
	int len = strlen(com);
	char letter;
	char letter_case = '\0';

	int color_count = 0;		
	while(com[i-1]!=' ' && i<len) {
		i++;
	}
	
	if(i>(len-1)) {
		memset(error_string,0,255);
		sprintf(error_string, "You need to specify which letter to delete.");
		return 0;
	} else {
		letter = com[i];
		if(letter>91) {
			letter_case = letter-32;
		} else {
			letter_case = letter+32;
		}
		results_i = list_iterator_create(allocated_partitions);
		while((allocated_part = list_next(results_i)) != NULL) {
			if(found) {
				redo_part(allocated_part->nodes, allocated_part->request->conn_type, color_count);
				if(allocated_part->request->conn_type==TORUS) 
					allocated_part->letter = 
						pa_system_ptr->fill_in_value[color_count].letter;
				
				else 
					allocated_part->letter =
						pa_system_ptr->fill_in_value[color_count+32].letter;
				allocated_part->color =
						pa_system_ptr->fill_in_value[color_count].color;
				
			} else if((allocated_part->letter == letter) || (allocated_part->letter == letter_case)) {
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

int _alter_allocation(char *com, List allocated_partitions)
{
	int torus=MESH, i=5, i2=0;
	int len = strlen(com);
	bool rotate = false;
	bool elongate = false;
	bool force_contig = false;
	bool co_proc = false;
		
	while(i<len) {
		
		while(com[i-1]!=' ' && i<len) {
			i++;
		}
		if(!strncmp(com+i, "torus", 5)) {
			torus=TORUS;
			i+=5;
		} else if(!strncmp(com+i, "rotate", 6)) {
			rotate=true;
			i+=6;
		} else if(!strncmp(com+i, "elongate", 8)) {
			elongate=true;
			i+=8;
		} else if(!strncmp(com+i, "force", 5)) {
			force_contig=true;				
			i+=5;
		} else if(!strncmp(com+i, "proc", 4)) {
			co_proc=true;				
			i+=4;
		} else if(i2<0 && (com[i] < 58 && com[i] > 47)) {
			i2=i;
			i++;
		} else {
			i++;
		}
		
	}
	return 1;
}

int _copy_allocation(char *com, List allocated_partitions)
{
	ListIterator results_i;
	allocated_part_t *allocated_part;
	pa_request_t *request = (pa_request_t*) xmalloc(sizeof(pa_request_t)); 
	
	int i=4;
	int len = strlen(com);
	char letter = '\0';
	char letter_case = '\0';

	while(com[i-1]!=' ' && i<len) {
		i++;
	}
	
	if(i<len) {
		letter = com[i];
		if(letter>91) {
			letter_case = letter-32;
		} else {
			letter_case = letter+32;
		}
	}
	request->geometry[X] = -1;
		
	results_i = list_iterator_create(allocated_partitions);
	while((allocated_part = list_next(results_i)) != NULL) {
		request->geometry[X] = allocated_part->request->geometry[X];
		request->geometry[Y] = allocated_part->request->geometry[Y];
		request->geometry[Z] = allocated_part->request->geometry[Z];
		request->size = allocated_part->request->size;
		request->conn_type=allocated_part->request->conn_type;
		request->rotate =allocated_part->request->rotate;
		request->elongate = allocated_part->request->elongate;
		request->force_contig = allocated_part->request->force_contig;
		request->co_proc = allocated_part->request->co_proc;
		if((allocated_part->letter == letter) || (allocated_part->letter == letter_case)) {
			break;
		}
	}
	list_iterator_destroy(results_i);			
	
	if(request->geometry[X]==-1) {
		memset(error_string,0,255);
		sprintf(error_string, "Problem with the copy\nAre you sure there is enough room for it?");
		return 0;
	}
	request->rotate_count= 0;
	request->elongate_count = 0;
		
	if((allocated_part = _make_request(request)) != NULL)
		list_append(allocated_partitions, allocated_part);
	return 1;
}

int _save_allocation(char *com, List allocated_partitions)
{
	int len = strlen(com);
	int i=5, j=0;
	allocated_part_t *allocated_part;
	char filename[20];
	char save_string[255];
	FILE *file_ptr;
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
		sprintf(filename,"bluegene.conf.%ld",pa_system_ptr->now_time);
	}
	file_ptr = fopen(filename,"w");
	if (file_ptr!=NULL)
	{
		results_i = list_iterator_create(allocated_partitions);
		while((allocated_part = list_next(results_i)) != NULL) {
			memset(save_string,0,255);
			sprintf(save_string, "Nodes=bgl[%s] Type=%d Use=%d\n", allocated_part->request->save_name, allocated_part->request->conn_type, allocated_part->request->co_proc);
			
			fputs (save_string,file_ptr);
		}
		fclose (file_ptr);
	}
	return 1;
}

void _print_header_command()
{
	pa_system_ptr->ycord=2;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "ID");
	pa_system_ptr->xcord += 4;
	/* mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, */
/* 		  pa_system_ptr->xcord, "PARTITION"); */
/* 	pa_system_ptr->xcord += 10; */
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "TYPE");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "CONF");
	pa_system_ptr->xcord += 9;
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

void _print_text_command(allocated_part_t *allocated_part)
{
	wattron(pa_system_ptr->text_win,
		COLOR_PAIR(allocated_part->color));
			
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "%c",allocated_part->letter);
	pa_system_ptr->xcord += 4;
	/* mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, */
	/* 		  pa_system_ptr->xcord, "PARTITION"); */
	/* 	pa_system_ptr->xcord += 10; */
	if(allocated_part->request->conn_type==TORUS) 
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "TORUS");
	else
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "MESH");	
	pa_system_ptr->xcord += 7;
				
	if(allocated_part->request->co_proc) 
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "coproc");
	else
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "virtual");	
	pa_system_ptr->xcord += 9;
				
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
		  pa_system_ptr->xcord, "bgl[%s]",allocated_part->request->save_name);
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;
	wattroff(pa_system_ptr->text_win,
		 COLOR_PAIR(allocated_part->color));
	return;
}

void get_command(void)
{
	char com[255];
	//static node_info_msg_t *node_info_ptr;
	int text_width, text_startx;
	allocated_part_t *allocated_part;
	int i=0;
	WINDOW *command_win;
	List allocated_partitions;
	ListIterator results_i;
		
	allocated_partitions = list_create(NULL);
				
	text_width = pa_system_ptr->text_win->_maxx;	// - pa_system_ptr->text_win->_begx;
	text_startx = pa_system_ptr->text_win->_begx;
	command_win = newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();

	
	while (strcmp(com, "quit")) {
		print_grid();
		wclear(pa_system_ptr->text_win);
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
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "%c",error_string[i++]);
				pa_system_ptr->xcord++;
			}
			pa_system_ptr->ycord++;
			pa_system_ptr->xcord=1;					
			memset(error_string,0,255);
			
		}
		results_i = list_iterator_create(allocated_partitions);
		while((allocated_part = list_next(results_i)) != NULL) {
			_print_text_command(allocated_part);
		}
		list_iterator_destroy(results_i);
		
		wrefresh(pa_system_ptr->text_win);
		wrefresh(pa_system_ptr->grid_win);
		wclear(command_win);
		box(command_win, 0, 0);
		mvwprintw(command_win, 0, 3,
			  "Input Command: (type quit to change view, exit to exit)");
		wmove(command_win, 1, 1);
		wgetstr(command_win, com);
		
		if (!strcmp(com, "exit")) {
			endwin();
			_delete_allocated_parts(allocated_partitions);
			pa_fini();
			exit(0);
		} else if (!strncmp(com, "resume", 6)) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, pa_system_ptr->xcord, "%s", com);
		} else if (!strncmp(com, "drain", 5)) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, pa_system_ptr->xcord, "%s", com);
		} else if (!strncmp(com, "remove", 6) || !strncmp(com, "delete", 6) || !strncmp(com, "drop", 4)) {
			_remove_allocation(com, allocated_partitions);
		} else if (!strncmp(com, "alter", 5)) {
			_alter_allocation(com, allocated_partitions);
		} else if (!strncmp(com, "create", 6)) {
			_create_allocation(com, allocated_partitions);
		} else if (!strncmp(com, "copy", 4) || !strncmp(com, "c ", 2) || !strncmp(com, "c\0", 2)) {
			_copy_allocation(com, allocated_partitions);
		} else if (!strncmp(com, "save", 4)) {
			_save_allocation(com, allocated_partitions);
		} else if (!strncmp(com, "clear all", 9)) {
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
	
	wclear(pa_system_ptr->text_win);
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord = 1;
	print_date();
	get_job();
	return;
}
