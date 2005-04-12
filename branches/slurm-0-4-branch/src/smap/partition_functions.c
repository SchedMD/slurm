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
#include "src/common/node_select.h"
#include "src/api/node_select_info.h"

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
	int letter_num;
	int start[PA_SYSTEM_DIMENSIONS];
	int end[PA_SYSTEM_DIMENSIONS];
	bool printed;

} db2_block_info_t;

#ifdef HAVE_BGL
static List block_list = NULL;
#endif

static char* _convert_conn_type(enum connection_type conn_type);
static char* _convert_node_use(enum node_use_type node_use);
static void _print_header_part(void);
static char *_part_state_str(rm_partition_state_t state);
static int  _print_text_part(partition_info_t *part_ptr, 
			     db2_block_info_t *db2_info_ptr);
#ifdef HAVE_BGL
static int _set_start_finish(db2_block_info_t *db2_info_ptr);
static void _block_list_del(void *object);
static int _list_match_all(void *object, void *key);
static int _in_slurm_partition(db2_block_info_t *db2_info_ptr, int *first, int *last);
static int _print_rest(db2_block_info_t *block_ptr, int *count);
#endif

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
			(int) letters[count%62];
		wattron(pa_system_ptr->text_win,
			COLOR_PAIR(colors[count%6]));
		_print_text_part(&part, NULL);
		wattroff(pa_system_ptr->text_win,
			 COLOR_PAIR(colors[count%6]));
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
#ifdef HAVE_BGL
	int error_code, i, j, recs=0, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL, *new_part_ptr;
	static node_select_info_msg_t *bgl_info_ptr = NULL, *new_bgl_ptr;
	partition_info_t part;
	int number, start[PA_SYSTEM_DIMENSIONS], end[PA_SYSTEM_DIMENSIONS];
	db2_block_info_t *block_ptr;
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
	if (bgl_info_ptr) {
		error_code = slurm_load_node_select(bgl_info_ptr->last_update, 
						   &new_bgl_ptr);
		if (error_code == SLURM_SUCCESS)
			select_g_free_node_info(&bgl_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bgl_ptr = bgl_info_ptr;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, &new_bgl_ptr);
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
	for (i=0; i<new_bgl_ptr->record_count; i++) {
		block_ptr = xmalloc(sizeof(db2_block_info_t));
		list_append(block_list, block_ptr);
		
		block_ptr->bgl_block_name = xstrdup(new_bgl_ptr->bgl_info_array[i].bgl_part_id);
		block_ptr->nodes = xstrdup(new_bgl_ptr->bgl_info_array[i].nodes);
		block_ptr->bgl_user_name = xstrdup(new_bgl_ptr->bgl_info_array[i].owner_name);
		block_ptr->state = new_bgl_ptr->bgl_info_array[i].state;
		block_ptr->bgl_conn_type = new_bgl_ptr->bgl_info_array[i].conn_type;
		block_ptr->bgl_node_use = new_bgl_ptr->bgl_info_array[i].node_use;
		_set_start_finish(block_ptr);
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
			while ((block_ptr = (db2_block_info_t*) list_next(itr)) != NULL) {
				if(_in_slurm_partition(block_ptr, start, end))
					block_ptr->slurm_part_name = xstrdup(part.name);
			}
			list_iterator_destroy(itr);
		}
	}

	/* Report any BGL Blocks not in a SLURM partition */
	if (block_list) {
		itr = list_iterator_create(block_list);
		while ((block_ptr = (db2_block_info_t*) list_next(itr)) != NULL)
			_print_rest(block_ptr, &count);
		list_iterator_destroy(itr);
	}

	if (params.commandline && params.iterate)
		printf("\n");

	part_info_ptr = new_part_ptr;
	bgl_info_ptr = new_bgl_ptr;
#endif /* HAVE_BGL */
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
		printf("PARTITION  ");
		if (params.display != BGLPART) {
			printf("AVAIL  ");
			printf("TIMELIMIT  ");
		} else {
			printf("BGL_BLOCK  ");
			printf("STATE  ");
			printf("  USER  ");
			printf("CONN  ");
			printf(" NODE_USE  ");
		}

		printf("NODES  ");
		printf("NODELIST\n");	
	}	
}

static char *_part_state_str(rm_partition_state_t state)
{
	static char tmp[16];

#ifdef HAVE_BGL
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
			  pa_system_ptr->xcord, "%.5d", part_ptr->total_nodes);
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
		if (part_ptr->name) {
			printf("%9.9s  ", part_ptr->name);
			
			if (params.display != BGLPART) {
				if (part_ptr->state_up)
					printf("   UP  ");
				else
					printf(" DOWN  ");
							
				if (part_ptr->max_time == INFINITE)
					snprintf(time_buf, sizeof(time_buf), "UNLIMITED");
				else {
					snprint_time(time_buf, sizeof(time_buf), 
						     (part_ptr->max_time * 60));
				}
			
				width = strlen(time_buf);
				printf("%9.9s  ", time_buf);
				
			} 
		} else
			pa_system_ptr->xcord += 10;

		if (params.display == BGLPART) {
			if (db2_info_ptr) {
				printf("%9.9s  ", db2_info_ptr->bgl_block_name);
				printf("%5.5s  ", _part_state_str(db2_info_ptr->state));
				
				printf("%6.6s  ", db2_info_ptr->bgl_user_name);
				
				printf("%4.4s  ", _convert_conn_type(
					       db2_info_ptr->bgl_conn_type));
				printf("%9.9s  ",  _convert_node_use(
					       db2_info_ptr->bgl_node_use));
				
			} 
		}

		printf("%5d  ", part_ptr->total_nodes);
		
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

#ifdef HAVE_BGL
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
		
		xfree(block_ptr);
		
	}
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
	part.root_only = (int) letters[block_ptr->letter_num%62];	
	
	wattron(pa_system_ptr->text_win, 
		COLOR_PAIR(colors[block_ptr->letter_num%6]));
	_print_text_part(&part, block_ptr);
	wattroff(pa_system_ptr->text_win,
		 COLOR_PAIR(colors[block_ptr->letter_num%6]));
	return SLURM_SUCCESS;
}
#endif

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

