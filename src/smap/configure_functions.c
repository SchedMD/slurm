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
	int type;
	char str[255];
} command_info_t;

typedef struct {
	int color;
	char letter;
	List nodes;
	pa_request_t *request; 
} allocated_part_t;

void _delete_allocated_parts(List allocated_partitions);
int _create_allocation(command_info_t *com, List allocated_partitions);
int _remove_allocation(command_info_t *com, List allocated_partitions);
int _alter_allocation(command_info_t *com, List allocated_partitions);
int _copy_allocation(command_info_t *com, List allocated_partitions);
int _save_allocation(command_info_t *com, List allocated_partitions);
void _print_header_command();
void _print_text_command(allocated_part_t *allocated_part);

void _delete_allocated_parts(List allocated_partitions)
{
	allocated_part_t *allocated_part;
	
	while ((allocated_part = list_pop(allocated_partitions)) != NULL) {
		list_destroy(allocated_part->nodes);
		delete_pa_request(allocated_part->request);
		xfree(allocated_part);
	}
	list_destroy(allocated_partitions);
}

int _create_allocation(command_info_t *com, List allocated_partitions)
{
	int i=6, i2=-1, i3=0;
	static int count=0;
	int len = strlen(com->str);
	
	pa_request_t *request = (pa_request_t*) xmalloc(sizeof(pa_request_t)); 
	pa_node_t *current;
	allocated_part_t *allocated_part;
	
	request->geometry[0] = -1;
	request->conn_type=MESH;
	request->rotate = false;
	request->elongate = false;
	request->force_contig = false;
	request->co_proc = false;
	List results;
	ListIterator results_i;		

	while(i<len) {
		
		while(com->str[i-1]!=' ' && i<len) {
			i++;
		}
		if(!strncmp(com->str+i, "torus", 5)) {
			request->conn_type=TORUS;
			i+=5;
		} else if(!strncmp(com->str+i, "rotate", 6)) {
			request->rotate=true;
			i+=6;
		} else if(!strncmp(com->str+i, "elongate", 8)) {
			request->elongate=true;
			i+=8;
		} else if(!strncmp(com->str+i, "force", 5)) {
			request->force_contig=true;				
			i+=5;
		} else if(!strncmp(com->str+i, "proc", 4)) {
			request->co_proc=true;				
			i+=4;
		} else if(i2<0 && (com->str[i] < 58 && com->str[i] > 47)) {
			i2=i;
			i++;
		} else {
			i++;
		}
		
	}
	
	if(i2<0) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "No size or dimension specified, please re-enter");
		pa_system_ptr->ycord++;
	} else {
		i3=i2;
		while(i3<len) {
			if(com->str[i3]==' ' || i3==(len-1)) {
				/* for size */
				request->size = atoi(&com->str[i2]);
				break;
			}
			if(com->str[i3]=='x') {
				
				/* for geometery */
				request->geometry[0] = atoi(&com->str[i2]);
				i2++;						
				while(com->str[i2-1]!='x' && i2<len)
					i2++;
				if(i2==len){
					mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
						  pa_system_ptr->xcord, "Error in dimension specified, please re-enter");
					pa_system_ptr->ycord++;
					break;
				} 
				request->geometry[1] = atoi(&com->str[i2]);
				i2++;	
				while(com->str[i2-1]!='x' && i2<len)
					i2++;
				if(i2==len){
					mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
						  pa_system_ptr->xcord, "Error in dimension specified, please re-enter");
					pa_system_ptr->ycord++;
					break;
				} 
				request->geometry[2] = atoi(&com->str[i2]);
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
		results = list_create(NULL);
		
		if(!new_pa_request(request)) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord,"Problems with request for %d%d%d", 
				  request->geometry[0], request->geometry[1], request->geometry[2]);
			pa_system_ptr->ycord++;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord,"Either you put in something that doesn't work,");
			pa_system_ptr->ycord++;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord,"or we are unable to process your request.");
			list_destroy(results);	
		} else {
			if (!allocate_part(request, results)){
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord,"allocate failure for %d%d%d\n", 
					  request->geometry[0], request->geometry[1], request->geometry[2]);
				pa_system_ptr->ycord++;
				list_destroy(results);
			} else {
				
				allocated_part = (allocated_part_t *)xmalloc(sizeof(allocated_part_t));
				allocated_part->request = request;
				allocated_part->nodes = list_create(NULL);
				allocated_part->color = count;
				if(allocated_part->request->conn_type==TORUS) {
					allocated_part->letter = pa_system_ptr->fill_in_value[count].letter;
				} else {
					allocated_part->letter = pa_system_ptr->fill_in_value[count+32].letter;
				}
				results_i = list_iterator_create(results);
				while ((current = list_next(results_i)) != NULL) {
					list_append(allocated_part->nodes,current);
				}
				list_iterator_destroy(results_i);
				list_append(allocated_partitions, allocated_part);
									
				list_destroy(results);
				count++;

			}
		}
	}
	
	return 1;
}

int _remove_allocation(command_info_t *com, List allocated_partitions)
{
	ListIterator results_i;
	allocated_part_t *allocated_part;
	int i=6, found=0;
	int len = strlen(com->str);
	char letter;
		
	while(com->str[i-1]!=' ' && i<len) {
		i++;
	}
	
	if(i==len) {
		return 0;
	} else {
		letter = com->str[i];
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord,"deleting partition %c\n", 
			  letter);
		pa_system_ptr->ycord++;
		results_i = list_iterator_create(allocated_partitions);
		while((allocated_part = list_next(results_i)) != NULL) {
			if(found) {
				redo_part(allocated_part->nodes, allocated_part->request->conn_type);
			} else if(allocated_part->letter==letter) {
				found=1;
				remove_part(allocated_part->nodes);
				list_destroy(allocated_part->nodes);
				delete_pa_request(allocated_part->request);				
				list_remove(results_i);
			}
		}
		list_iterator_destroy(results_i);
	}
		
	return 1;
}

int _alter_allocation(command_info_t *com, List allocated_partitions)
{
	int torus=MESH, i=5, i2=0;
	int len = strlen(com->str);
	bool rotate = false;
	bool elongate = false;
	bool force_contig = false;
	bool co_proc = false;
		
	while(i<len) {
		
		while(com->str[i-1]!=' ' && i<len) {
			i++;
		}
		if(!strncmp(com->str+i, "torus", 5)) {
			torus=TORUS;
			i+=5;
		} else if(!strncmp(com->str+i, "rotate", 6)) {
			rotate=true;
			i+=6;
		} else if(!strncmp(com->str+i, "elongate", 8)) {
			elongate=true;
			i+=8;
		} else if(!strncmp(com->str+i, "force", 5)) {
			force_contig=true;				
			i+=5;
		} else if(!strncmp(com->str+i, "proc", 4)) {
			co_proc=true;				
			i+=4;
		} else if(i2<0 && (com->str[i] < 58 && com->str[i] > 47)) {
			i2=i;
			i++;
		} else {
			i++;
		}
		
	}
	return 1;
}

int _copy_allocation(command_info_t *com, List allocated_partitions)
{
	ListIterator results_i;
	allocated_part_t *allocated_part;
	
	int i=6, found=0;
	int len = strlen(com->str);
	char letter;

	while(com->str[i-1]!=' ' && i<len) {
		i++;
	}
	
	if(i==len) {
		return 0;
	} else {
		letter = com->str[i];
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord,"deleting partition %c\n", 
			  letter);
		pa_system_ptr->ycord++;
		results_i = list_iterator_create(allocated_partitions);
		while((allocated_part = list_next(results_i)) != NULL) {
			if(found) {
				redo_part(allocated_part->nodes, allocated_part->request->conn_type);
			} else if(allocated_part->letter==letter) {
				found=1;
				remove_part(allocated_part->nodes);
			}
		}
		list_iterator_destroy(results_i);
			
	}

	return 1;
}

int _save_allocation(command_info_t *com, List allocated_partitions)
{
	
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
		COLOR_PAIR(pa_system_ptr->fill_in_value[allocated_part->color].color));
			
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
		 COLOR_PAIR(pa_system_ptr->fill_in_value[allocated_part->color].color));
	return;
}

void get_command(void)
{
	command_info_t *com = xmalloc(sizeof(command_info_t));
	//static node_info_msg_t *node_info_ptr;
	int text_width, text_startx;
	allocated_part_t *allocated_part;
	
	WINDOW *command_win;
	List allocated_partitions;
	ListIterator results_i;
		
	allocated_partitions = list_create(NULL);
				
	text_width = pa_system_ptr->text_win->_maxx;	// - pa_system_ptr->text_win->_begx;
	text_startx = pa_system_ptr->text_win->_begx;
	command_win = newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();

	
	while (strcmp(com->str, "quit")) {
		print_grid();
		wclear(pa_system_ptr->text_win);
		box(pa_system_ptr->text_win, 0, 0);
		box(pa_system_ptr->grid_win, 0, 0);
		
		if (!params.no_header)
			_print_header_command();
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
		wgetstr(command_win, com->str);
		
		if (!strcmp(com->str, "exit")) {
			endwin();
			_delete_allocated_parts(allocated_partitions);
			pa_fini();
			exit(0);
		} else if (!strncmp(com->str, "resume", 6)) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, pa_system_ptr->xcord, "%s", com->str);
		} else if (!strncmp(com->str, "drain", 5)) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, pa_system_ptr->xcord, "%s", com->str);
		} else if (!strncmp(com->str, "remove", 6) || !strncmp(com->str, "delete", 6) || !strncmp(com->str, "drop", 4)) {
			_remove_allocation(com, allocated_partitions);
		} else if (!strncmp(com->str, "alter", 5)) {
			_alter_allocation(com, allocated_partitions);
		} else if (!strncmp(com->str, "create", 6)) {
			_create_allocation(com, allocated_partitions);
		} else if (!strncmp(com->str, "copy", 4)) {
			_copy_allocation(com, allocated_partitions);
		} else if (!strncmp(com->str, "save", 4)) {
			_save_allocation(com, allocated_partitions);
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
