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
	char letter;
	int conn_type;
	List nodes;
	pa_request_t *request; 
} allocated_part_t;

void print_header_command(void);
int print_text_command(void);
void _delete_allocated_parts(List allocated_partitions);
int _create_allocation(command_info_t *com, List allocated_partitions);
int _remove_allocation(command_info_t *com, List allocated_partitions);
int _alter_allocation(command_info_t *com, List allocated_partitions);
int _save_allocation(command_info_t *com, List allocated_partitions);

void _delete_allocated_parts(List allocated_partitions)
{
	ListIterator results_i;
	allocated_part_t *allocated_part;
	
	results_i = list_iterator_create(allocated_partitions);
	while ((allocated_part = list_next(results_i)) != NULL) {
		list_destroy(allocated_part->nodes);
		delete_pa_request(allocated_part->request);
	}
	list_destroy(allocated_partitions);
}

int _create_allocation(command_info_t *com, List allocated_partitions)
{
	int torus=MESH, i=6, i2=-1, i3=0, geo[PA_SYSTEM_DIMENSIONS] = {-1,-1,-1};
	static int count=0;
	int len = strlen(com->str);
	
	pa_request_t *request; 
	pa_node_t *current;
	allocated_part_t *allocated_part;
	
	bool rotate = false;
	bool elongate = false;
	bool force_contig = false;
	bool co_proc = false;
	List results;
	ListIterator results_i;
	
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
	
	if(i2<0) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "No size or dimension specified, please re-enter");
		pa_system_ptr->ycord++;
	} else {
		i3=i2;
		while(i3<len) {
			if(com->str[i3]==' ' || i3==(len-1)) {
				/* for size */
				i = atoi(&com->str[i2]);
				break;
			}
			if(com->str[i3]=='x') {
				
				/* for geometery */
				geo[0] = atoi(&com->str[i2]);
				i2++;						
				while(com->str[i2-1]!='x' && i2<len)
					i2++;
				if(i2==len){
					mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
						  pa_system_ptr->xcord, "Error in dimension specified, please re-enter");
					pa_system_ptr->ycord++;
					break;
				} 
				geo[1] = atoi(&com->str[i2]);
				i2++;	
				while(com->str[i2-1]!='x' && i2<len)
					i2++;
				if(i2==len){
					mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
						  pa_system_ptr->xcord, "Error in dimension specified, please re-enter");
					pa_system_ptr->ycord++;
					break;
				} 
				geo[2] = atoi(&com->str[i2]);
				i = -1;
				break;
			}
			i3++;
		}
		
		/*
		   Here is where we do the allocating of the partition. 
		   It will send a request back which we will throw into
		   a list just incase we change something later.		   
		*/
		request = (pa_request_t*) xmalloc(sizeof(pa_request_t));
		results = list_create(NULL);
		
		if(!new_pa_request(request, geo, i, rotate, elongate, force_contig, co_proc, torus)) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord,"Problems with request for %d%d%d", geo[0], geo[1], geo[2]);
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
					  geo[0], geo[1], geo[2]);
				pa_system_ptr->ycord++;
				list_destroy(results);
			} else {
				
				allocated_part = (allocated_part_t *)xmalloc(sizeof(allocated_part_t));
				allocated_part->request = request;
				allocated_part->nodes = list_create(NULL);
				allocated_part->conn_type = torus;
				if(torus==TORUS) {
					allocated_part->letter = pa_system_ptr->fill_in_value[count].letter;
				} else {
					allocated_part->letter = pa_system_ptr->fill_in_value[count+32].letter;
				}
				results_i = list_iterator_create(results);
				while ((current = list_next(results_i)) != NULL) {
					list_append(allocated_part->nodes,current);
				}
				
				list_append(allocated_partitions, allocated_part);
									
				list_destroy(results);
				wattron(pa_system_ptr->text_win,
					COLOR_PAIR(pa_system_ptr->fill_in_value[count].color));
			
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "%c",allocated_part->letter);
				pa_system_ptr->xcord += 4;
				/* mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, */
/* 		  pa_system_ptr->xcord, "PARTITION"); */
/* 	pa_system_ptr->xcord += 10; */
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "%d",allocated_part->request->size);
				pa_system_ptr->xcord += 7;
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "bgl[%s]",allocated_part->request->save_name);
				pa_system_ptr->xcord = 1;
				pa_system_ptr->ycord++;
				wattroff(pa_system_ptr->text_win,
					COLOR_PAIR(pa_system_ptr->fill_in_value[count].color));
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
				redo_part(allocated_part->nodes, allocated_part->conn_type);
			} else if(allocated_part->letter==letter) {
				found=1;
				remove_part(allocated_part->nodes);
			}
		}
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

int _save_allocation(command_info_t *com, List allocated_partitions)
{
	
	return 1;
}
void get_command(void)
{
	command_info_t *com = xmalloc(sizeof(command_info_t));
	//static node_info_msg_t *node_info_ptr;
	int text_height, text_width, text_starty, text_startx;
	WINDOW *command_win;
	List allocated_partitions;
		
	allocated_partitions = list_create(NULL);
				
	text_height = pa_system_ptr->text_win->_maxy;	// - pa_system_ptr->text_win->_begy;
	text_width = pa_system_ptr->text_win->_maxx;	// - pa_system_ptr->text_win->_begx;
	text_starty = pa_system_ptr->text_win->_begy;
	text_startx = pa_system_ptr->text_win->_begx;
	command_win = newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();

	if (!params.no_header)
		print_header_command();
	
	while (strcmp(com->str, "quit")) {
		print_grid();
		box(pa_system_ptr->text_win, 0, 0);
		box(pa_system_ptr->grid_win, 0, 0);
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

void print_header_command(void)
{
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "ID");
	pa_system_ptr->xcord += 4;
	/* mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord, */
/* 		  pa_system_ptr->xcord, "PARTITION"); */
/* 	pa_system_ptr->xcord += 10; */
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODES");
	pa_system_ptr->xcord += 7;
	mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
		  pa_system_ptr->xcord, "NODELIST");
	pa_system_ptr->xcord = 1;
	pa_system_ptr->ycord++;
}

int print_text_command()
{
	return 1;
}
