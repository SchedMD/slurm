/*****************************************************************************\
 *  partition_functions.c - Functions related to partition display 
 *  mode of smap.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
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

#include "src/smap/smap.h"

#define _DEBUG 0
DEF_TIMERS;

typedef struct {
	char *bgl_user_name;
	char *bgl_block_name;
	char *slurm_part_name;
	char *nodes;
	enum connection_type bgl_conn_type;
	enum node_use_type bgl_node_use;
	rm_partition_state_t state;
	hostlist_t hostlist;
	int letter_num;
	int start[PA_SYSTEM_DIMENSIONS];
	int end[PA_SYSTEM_DIMENSIONS];
	bool printed;

} db2_block_info_t;

static List block_list = NULL;

static char* _convert_conn_type(enum connection_type conn_type);
static char* _convert_node_use(enum node_use_type node_use);
static void _print_header_part(void);
static char *_part_state_str(rm_partition_state_t state);
static int  _print_text_part(partition_info_t *part_ptr, 
			     db2_block_info_t *db2_info_ptr);
static void _read_part_db2(void);
#if HAVE_BGL_FILES
static int _set_start_finish(db2_block_info_t *db2_info_ptr);
#endif
static int _in_slurm_partition(db2_block_info_t *db2_info_ptr, int *first, int *last);
static int _print_rest(db2_block_info_t *block_ptr, int *count);

void get_slurm_part(void)
{
	int error_code, i, j, recs, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL, *new_part_ptr;
	partition_info_t part;

	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, 0);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, 0);
	}
	if (error_code) {
		if (quiet_flag != 1) {
			if(!params.commandline) {
				mvwprintw(pa_system_ptr->text_win,
					  pa_system_ptr->ycord, 1,
					  "slurm_load_partitions: %s",
					  slurm_strerror(slurm_get_errno()));
				pa_system_ptr->ycord++;
			} else {
				printf("slurm_load_partitions: %s",
					  slurm_strerror(slurm_get_errno()));
			}
		}
	}

	if (!params.no_header)
		_print_header_part();

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;

	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		
				
		while (part.node_inx[j] >= 0) {
			set_grid(part.node_inx[j],
				 part.node_inx[j + 1], count);
			j += 2;
		}
		part.root_only =
			(int) pa_system_ptr->
			fill_in_value[count].letter;
		wattron(pa_system_ptr->text_win,
			COLOR_PAIR(pa_system_ptr->
				   fill_in_value[count].
				   color));
		_print_text_part(&part, NULL);
		wattroff(pa_system_ptr->text_win,
			 COLOR_PAIR(pa_system_ptr->
				    fill_in_value[count].
				    color));
		count++;
			
	}
	if(count==128)
		count=0;
	if (params.commandline && params.iterate)
		printf("\n");
	
	part_info_ptr = new_part_ptr;
	return;
}

void get_bgl_part(void)
{
	int error_code, i, j, recs=0, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL, *new_part_ptr;
	partition_info_t part;
	int number, start[PA_SYSTEM_DIMENSIONS], end[PA_SYSTEM_DIMENSIONS];
	db2_block_info_t *db2_info_ptr;
	ListIterator itr;
	
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, 0);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, 0);
	}

	if (error_code) {
		if (quiet_flag != 1) {
			if(!params.commandline) {
				mvwprintw(pa_system_ptr->text_win,
					  pa_system_ptr->ycord, 1,
					  "slurm_load_partitions: %s",
					  slurm_strerror(slurm_get_errno()));
				pa_system_ptr->ycord++;
			} else {
				printf("slurm_load_partitions: %s",
				       slurm_strerror(slurm_get_errno()));
			}
		}
	}
	
	
	_read_part_db2();
	

	if (!params.no_header)
		_print_header_part();

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;
	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		while (part.nodes[j] != '\0') {
			if ((part.nodes[j]   == '[')
			    && (part.nodes[j+8] == ']')
			    && ((part.nodes[j+4] == 'x')
				|| (part.nodes[j+4] == '-'))) {
				j++;
				number = atoi(part.nodes + j);
				start[X] = number / 100;
				start[Y] = (number % 100) / 10;
				start[Z] = (number % 10);
				j += 4;

				number = atoi(part.nodes + j);
				end[X] = number / 100;
				end[Y] = (number % 100) / 10;
				end[Z] = (number % 10);
				break;
			}
			j++;
		}
		
		if (block_list) {
			itr = list_iterator_create(block_list);
			while ((db2_info_ptr = (db2_block_info_t*) list_next(itr)) != NULL) {
				if(_in_slurm_partition(db2_info_ptr, start, end))
					db2_info_ptr->slurm_part_name = xstrdup(part.name);
			}
			list_iterator_destroy(itr);
		}
	}

	/* Report any BGL Blocks not in a SLURM partition */
	if (block_list) {
		itr = list_iterator_create(block_list);
		while ((db2_info_ptr = (db2_block_info_t*) list_next(itr)) != NULL)
			_print_rest(db2_info_ptr, &count);
		list_iterator_destroy(itr);
	}

	if (params.commandline && params.iterate)
		printf("\n");

	part_info_ptr = new_part_ptr;
	return;
}

static void _print_header_part(void)
{
	if(!params.commandline) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "ID");
		pa_system_ptr->xcord += 4;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "PARTITION");
		pa_system_ptr->xcord += 10;
	
		if (params.display != BGLPART) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "AVAIL");
			pa_system_ptr->xcord += 7;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "TIMELIMIT");
			pa_system_ptr->xcord += 11;
		} else {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "BGL_BLOCK");
			pa_system_ptr->xcord += 12;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "STATE");
			pa_system_ptr->xcord += 8;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "USER");
			pa_system_ptr->xcord += 12;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "CONN");
			pa_system_ptr->xcord += 6;
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "NODE_USE");
			pa_system_ptr->xcord += 10;
		}

		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "NODES");
		pa_system_ptr->xcord += 7;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "NODELIST");
		pa_system_ptr->xcord = 1;
		pa_system_ptr->ycord++;
	} else {
		printf("ID\t");
		printf("PARTITION\t");
		if (params.display != BGLPART) {
			printf("AVAIL\t");
			printf("TIMELIMIT\t");
		} else {
			printf("BGL_BLOCK\t");
			printf("STATE\t");
			printf("USER\t");
			printf("CONN\t");
			printf("NODE_USE\t");
		}

		printf("NODES\t");
		printf("NODELIST\n");	
	}	
}

static char *_part_state_str(rm_partition_state_t state)
{
	static char tmp[16];

#ifdef HAVE_BGL_FILES
	switch (state) {
		case RM_PARTITION_BUSY: 
			return "BUSY";
		case RM_PARTITION_CONFIGURING:
			return "CONFIG";
		case RM_PARTITION_DEALLOCATING:
			return "DEALLOC";
		case RM_PARTITION_ERROR:
			return "ERROR";
		case RM_PARTITION_FREE:
			return "FREE";
		case RM_PARTITION_NAV:
			return "NAV";
		case RM_PARTITION_READY:
			return "READY";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
}

static int _print_text_part(partition_info_t *part_ptr, 
			    db2_block_info_t *db2_info_ptr)
{
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	char *nodes, time_buf[20];

	if(!params.commandline) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%c", part_ptr->root_only);
		pa_system_ptr->xcord += 4;

		if (part_ptr->name) {
			mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
				  pa_system_ptr->xcord, "%.9s", part_ptr->name);
			pa_system_ptr->xcord += 10;
			if (params.display != BGLPART) {
				if (part_ptr->state_up)
					mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
						  pa_system_ptr->xcord, "UP");
				else
					mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
						  pa_system_ptr->xcord, "DOWN");
				pa_system_ptr->xcord += 7;
			
				if (part_ptr->max_time == INFINITE)
					snprintf(time_buf, sizeof(time_buf), "UNLIMITED");
				else {
					snprint_time(time_buf, sizeof(time_buf), 
						     (part_ptr->max_time * 60));
				}
			
				width = strlen(time_buf);
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord + (9 - width), "%s", 
					  time_buf);
				pa_system_ptr->xcord += 11;
			} 
		} else
			pa_system_ptr->xcord += 10;

		if (params.display == BGLPART) {
			if (db2_info_ptr) {
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "%.11s", 
					  db2_info_ptr->bgl_block_name);
				pa_system_ptr->xcord += 12;
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, _part_state_str(db2_info_ptr->state));
				pa_system_ptr->xcord += 8;
				
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "%.11s", 
					  db2_info_ptr->bgl_user_name);
				pa_system_ptr->xcord += 12;
			
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "%.5s", 
					  _convert_conn_type(
						  db2_info_ptr->bgl_conn_type));
				pa_system_ptr->xcord += 6;
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "%.9s",
					  _convert_node_use(
						  db2_info_ptr->bgl_node_use));
				pa_system_ptr->xcord += 10;
			} else {
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "?");
				pa_system_ptr->xcord += 12;
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "?");
				pa_system_ptr->xcord += 8;
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "?");
				pa_system_ptr->xcord += 12;
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "?");
				pa_system_ptr->xcord += 6;
				mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
					  pa_system_ptr->xcord, "?");
				pa_system_ptr->xcord += 10;
			}
		}

		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%5d", part_ptr->total_nodes);
		pa_system_ptr->xcord += 7;

		tempxcord = pa_system_ptr->xcord;
		//width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;
		if (params.display == BGLPART)
			nodes = part_ptr->allow_groups;
		else
			nodes = part_ptr->nodes;
		prefixlen = i;
		while (nodes && nodes[i]) {
			width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;

			if (!prefixlen && nodes[i] == '[' && nodes[i - 1] == ',')
				prefixlen = i + 1;

			if (nodes[i - 1] == ',' && (width - 12) <= 0) {
				pa_system_ptr->ycord++;
				pa_system_ptr->xcord = tempxcord + prefixlen;
			} else if (pa_system_ptr->xcord >
				   pa_system_ptr->text_win->_maxx) {
				pa_system_ptr->ycord++;
				pa_system_ptr->xcord = tempxcord + prefixlen;
			}


			if ((printed = mvwaddch(pa_system_ptr->text_win,
						pa_system_ptr->ycord, pa_system_ptr->xcord,
						nodes[i])) < 0)
				return printed;
			pa_system_ptr->xcord++;

			i++;
		}

		pa_system_ptr->xcord = 1;
		pa_system_ptr->ycord++;
	} else {
		printf("%c\t", part_ptr->root_only);
		
		if (part_ptr->name) {
			printf("%s\t", part_ptr->name);
			pa_system_ptr->xcord += 10;
			if (params.display != BGLPART) {
				if (part_ptr->state_up)
					printf("UP\t");
				else
					printf("DOWN\t");
							
				if (part_ptr->max_time == INFINITE)
					snprintf(time_buf, sizeof(time_buf), "UNLIMITED");
				else {
					snprint_time(time_buf, sizeof(time_buf), 
						     (part_ptr->max_time * 60));
				}
			
				width = strlen(time_buf);
				printf("%s\t", time_buf);
				
			} 
		} else
			pa_system_ptr->xcord += 10;

		if (params.display == BGLPART) {
			if (db2_info_ptr) {
				printf("%s\t", db2_info_ptr->bgl_block_name);
				printf("%s\t", _part_state_str(db2_info_ptr->state));
				
				printf("%s\t", db2_info_ptr->bgl_user_name);
				
				printf("%s\t", _convert_conn_type(
					       db2_info_ptr->bgl_conn_type));
				printf("%s\t",  _convert_node_use(
					       db2_info_ptr->bgl_node_use));
			} else {
				printf("?\t");
				printf("?\t");
				printf("?\t");
				printf("?\t");
				printf("?\t");
			}
		}

		printf("%d\t", part_ptr->total_nodes);
		
		tempxcord = pa_system_ptr->xcord;
		//width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;
		if (params.display == BGLPART)
			nodes = part_ptr->allow_groups;
		else
			nodes = part_ptr->nodes;
		printf("%s\n",nodes);
	}
	return printed;
}

#ifdef HAVE_BGL_FILES
static void _block_list_del(void *object)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *)object;

	if (block_ptr) {
		if(block_ptr->bgl_user_name)
			xfree(block_ptr->bgl_user_name);
		if(block_ptr->bgl_block_name)
			xfree(block_ptr->bgl_block_name);
		if(block_ptr->slurm_part_name)
			xfree(block_ptr->slurm_part_name);
		if(block_ptr->nodes)
			xfree(block_ptr->nodes);
		if (block_ptr->hostlist)
			hostlist_destroy(block_ptr->hostlist);
		
		xfree(block_ptr);
		
	}
}

/*
 * Convert a BGL API error code to a string
 * IN inx - error code from any of the BGL Bridge APIs
 * RET - string describing the error condition
 */
char *bgl_err_str(status_t inx)
{
	switch (inx) {
	case STATUS_OK:
		return "Status OK";
	case PARTITION_NOT_FOUND:
		return "Partition not found";
	case JOB_NOT_FOUND:
		return "Job not found";
	case BP_NOT_FOUND:
		return "Base partition not found";
	case SWITCH_NOT_FOUND:
		return "Switch not found";
	case JOB_ALREADY_DEFINED:
		return "Job already defined";
	case CONNECTION_ERROR:
		return "Connection error";
	case INTERNAL_ERROR:
		return "Internal error";
	case INVALID_INPUT:
		return "Invalid input";
	case INCOMPATIBLE_STATE:
		return "Incompatible state";
	case INCONSISTENT_DATA:
		return "Inconsistent data";
	}

	return "?";
}



static int _list_match_all(void *object, void *key)
{
	
	return 1;
}


static int _set_start_finish(db2_block_info_t *db2_info_ptr)
{
	int number;
	int j=0;
	
	while (db2_info_ptr->nodes[j] != '\0') {
		if ((db2_info_ptr->nodes[j]   == '[')
		    && (db2_info_ptr->nodes[j+8] == ']')
		    && ((db2_info_ptr->nodes[j+4] == 'x')
			|| (db2_info_ptr->nodes[j+4] == '-'))) {
			j++;
			number = atoi(db2_info_ptr->nodes + j);
			db2_info_ptr->start[X] = number / 100;
			db2_info_ptr->start[Y] = (number % 100) / 10;
			db2_info_ptr->start[Z] = (number % 10);
			j += 4;
			number = atoi(db2_info_ptr->nodes + j);
			db2_info_ptr->end[X] = number / 100;
			db2_info_ptr->end[Y] = (number % 100) / 10;
			db2_info_ptr->end[Z] = (number % 10);
			j += 5;
			if(db2_info_ptr->nodes[j] != ',')
				break;
		} else if((db2_info_ptr->nodes[j] < 58 && db2_info_ptr->nodes[j] > 47) 
			  && db2_info_ptr->nodes[j-1] != '[') {
			number = atoi(db2_info_ptr->nodes + j);
			db2_info_ptr->start[X] = db2_info_ptr->end[X] = number / 100;
			db2_info_ptr->start[Y] = db2_info_ptr->end[Y] = (number % 100) / 10;
			db2_info_ptr->start[Z] = db2_info_ptr->end[Z] = (number % 10);
			j+=3;
			if(db2_info_ptr->nodes[j] != ',')
				break;	
		}
				
		j++;
	}
	/* printf("setting %s start = %d%d%d end = %d%d%d\n", db2_info_ptr->nodes, */
/* 	       db2_info_ptr->start[X],db2_info_ptr->start[Y],db2_info_ptr->start[Z], */
/* 	       db2_info_ptr->end[X],db2_info_ptr->end[Y],db2_info_ptr->end[Z]); */
	return 1;
}
#endif

static int _in_slurm_partition(db2_block_info_t *db2_info_ptr, int *first, int *last)
{
	if((db2_info_ptr->start[X]>=first[X])
	   && (db2_info_ptr->start[Y]>=first[Y])
	   && (db2_info_ptr->start[Z]>=first[Z])
	   && (db2_info_ptr->end[X]<=last[X])
	   && (db2_info_ptr->end[Y]<=last[Y])
	   && (db2_info_ptr->end[Z]<=last[Z]))
		return 1;
	else 
		return 0;
	
}

static int _print_rest(db2_block_info_t *block_ptr, int *count)
{
	//static rm_BGL_t *bgl = NULL;
	partition_info_t part;
	db2_block_info_t *db2_info_ptr = NULL;
	ListIterator itr;
	int set = 0;
	if (block_ptr->printed)
		return SLURM_SUCCESS;
		
	part.total_nodes = 0;
	
	if (block_list) {
		itr = list_iterator_create(block_list);
		while ((db2_info_ptr = (db2_block_info_t*) list_next(itr)) != NULL) {
			//printf("looking at %s %s\n", block_ptr->bgl_block_name, db2_info_ptr->bgl_block_name);
			if(!strcmp(block_ptr->bgl_block_name, db2_info_ptr->bgl_block_name)) {
				if(set == 2)
					break;
				set = 0;
				break;
			}
			if((block_ptr->start[X]==db2_info_ptr->start[X] && 
			    block_ptr->start[Y]==db2_info_ptr->start[Y] && 
			    block_ptr->start[Z]==db2_info_ptr->start[Z]) &&
			   (block_ptr->end[X]==db2_info_ptr->end[X] && 
			    block_ptr->end[Y]==db2_info_ptr->end[Y] && 
			    block_ptr->end[Z]==db2_info_ptr->end[Z])) {
				set = 1;
				break;
			} 
			
			if((block_ptr->start[X]<=db2_info_ptr->start[X] && 
			    block_ptr->start[Y]<=db2_info_ptr->start[Y] && 
			    block_ptr->start[Z]<=db2_info_ptr->start[Z]) &&
			   (block_ptr->end[X]>=db2_info_ptr->end[X] && 
			    block_ptr->end[Y]>=db2_info_ptr->end[Y] && 
			    block_ptr->end[Z]>=db2_info_ptr->end[Z])) {
				set = 2;
				continue;
			}		
		}
		list_iterator_destroy(itr);
	}
	
	if (set == 1) {
		block_ptr->letter_num=db2_info_ptr->letter_num;
		part.total_nodes += set_grid_bgl(block_ptr->start, block_ptr->end, block_ptr->letter_num, set);
	} else {
		block_ptr->letter_num=*count;
		part.total_nodes += set_grid_bgl(block_ptr->start, block_ptr->end, block_ptr->letter_num, set);
		(*count)++;
	} 

	if(block_ptr->slurm_part_name)
		part.name = block_ptr->slurm_part_name;
	else
		part.name = "no part";

	part.allow_groups = block_ptr->nodes;
	part.root_only = (int) pa_system_ptr->fill_in_value[block_ptr->letter_num].letter;	
	
	wattron(pa_system_ptr->text_win, 
		COLOR_PAIR(pa_system_ptr->fill_in_value[block_ptr->letter_num].color));
	_print_text_part(&part, block_ptr);
	wattroff(pa_system_ptr->text_win,
		 COLOR_PAIR(pa_system_ptr->fill_in_value[block_ptr->letter_num].color));
	return SLURM_SUCCESS;
}

#ifdef HAVE_BGL_FILES
static int _post_block_read(void *object, void *arg)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *) object;
	int len = 1024;
	
	block_ptr->nodes = xmalloc(len);
	while (hostlist_ranged_string(block_ptr->hostlist, len, 
				      block_ptr->nodes) < 0) {
		len *= 2;
		xrealloc(block_ptr->nodes, len);
	}
	_set_start_finish(block_ptr);
				
	return SLURM_SUCCESS;
} 
#endif

static void _read_part_db2(void)
{
#ifdef HAVE_BGL_FILES
	status_t rc;
/* 	static rm_BGL_t *bgl = NULL; */
	int bp_num, i;
	char *bp_id;
	rm_partition_t *part_ptr;
	db2_block_info_t *block_ptr;
	//block_t *block_ptr;
	char *user_name;
	int part_number, part_count;
	char *part_name;
	char node_name_tmp[7];
	rm_element_t *bp_ptr;
	int *coord;
	rm_connection_type_t conn_type;
	rm_partition_mode_t node_use;
	rm_partition_list_t *part_list;
	rm_partition_state_flag_t state = 7;
	
	if ((rc = rm_set_serial(BGL_SERIAL)) != STATUS_OK) {
		error("rm_set_serial(): %d\n", rc);
		return;
	}			
	
	if (block_list) {
		/* clear the old list */
		list_delete_all(block_list, _list_match_all, NULL);
	} else {
		block_list = list_create(_block_list_del);
		if (!block_list) {
			fprintf(stderr, "malloc error\n");
			return;
		}
	}	
	
	if ((rc = rm_get_partitions_info(state, &part_list))
	    != STATUS_OK) {
		error("rm_get_partitions(): %s",
		      bgl_err_str(rc));
		return;
		
	}
	
	rm_get_data(part_list, RM_PartListSize, &part_count);
	
	rm_get_data(part_list, RM_PartListFirstPart, &part_ptr);
	
	for(part_number=0; part_number<part_count; part_number++) {
		rm_get_data(part_ptr, RM_PartitionID, &part_name);
		if(strncmp("RMP",part_name,3))
			goto next_partition;
		
		//printf("Checking if Partition %s is free",part_name);
		if ((rc = rm_get_partition(part_name, &part_ptr))
			    != STATUS_OK) {
				break;
			}		
			
		if ((rc = rm_get_data(part_ptr, RM_PartitionBPNum, &bp_num)) != STATUS_OK) {
			error("rm_get_data(RM_BPNum): %s", bgl_err_str(rc));
			bp_num = 0;
		}
		/* if(bp_num==0) */
/* 			continue; */
		if ((rc = rm_get_data(part_ptr, RM_PartitionFirstBP, &bp_ptr))
		    != STATUS_OK) {
			error("rm_get_data(RM_FirstBP): %s",
			      bgl_err_str(rc));
			rc = SLURM_ERROR;
			return;
		}
		block_ptr = xmalloc(sizeof(db2_block_info_t));
		list_push(block_list, block_ptr);
		block_ptr->bgl_block_name = xstrdup(part_name);
			
		block_ptr->hostlist = hostlist_create(NULL);
			
		for (i=0; i<bp_num; i++) {
			if ((rc = rm_get_data(bp_ptr, RM_BPID, &bp_id))
			    != STATUS_OK) {
				error("rm_get_data(RM_BPLoc): %s",
				      bgl_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}
			coord = find_bp_loc(bp_id);
				
			sprintf(node_name_tmp, "bgl%d%d%d", 
				coord[X], coord[Y], coord[Z]);
				
			hostlist_push(block_ptr->hostlist, node_name_tmp);
			if ((rc = rm_get_data(part_ptr, RM_PartitionNextBP, &bp_ptr))
			    != STATUS_OK) {
				error("rm_get_data(RM_NextBP): %s",
				      bgl_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}
		}	
		if ((rc = rm_get_data(part_ptr, 
				      RM_PartitionState, 
				      &block_ptr->state)) != STATUS_OK) {
		}
			
		if ((rc = rm_get_data(part_ptr, 
				      RM_PartitionUserName, 
				      &user_name)) != STATUS_OK) {
		} else
			block_ptr->bgl_user_name = xstrdup(user_name);
			
		if ((rc = rm_get_data(part_ptr,
				      RM_PartitionConnection,
				      &conn_type)) != STATUS_OK) {
			block_ptr->bgl_conn_type = SELECT_NAV;
		} else
			block_ptr->bgl_conn_type = conn_type;
		if ((rc = rm_get_data(part_ptr, RM_PartitionMode,
				      &node_use))
		    != STATUS_OK) {
			block_ptr->bgl_node_use = SELECT_NAV_MODE;
		} else
			block_ptr->bgl_node_use = node_use;
			
	next_partition:
		/* if ((rc = rm_free_partition(part_ptr)) != STATUS_OK) { */
/* 		} */
		rm_get_data(part_list, RM_PartListNextPart, &part_ptr);
	}
	rm_free_partition_list(part_list);
		
	/* perform post-processing for each bluegene partition */
	list_for_each(block_list, _post_block_read, NULL);

#endif
}


static char* _convert_conn_type(enum connection_type conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH):
		return "MESH";
	case (SELECT_TORUS):
		return "TORUS";
	case (SELECT_NAV):
		return "NAV";
	}
	return "?";
}

static char* _convert_node_use(enum node_use_type node_use)
{
	switch (node_use) {
	case (SELECT_COPROCESSOR_MODE):
		return "COPROCESSOR";
	case (SELECT_VIRTUAL_NODE_MODE):
		return "VIRTUAL";
	case (SELECT_NAV_MODE):
		return "NAV";
	}
	return "?";
}

