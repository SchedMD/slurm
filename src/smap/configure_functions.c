/*****************************************************************************\
 *  configure_functions.c - Functions related to configure mode of smap.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/node_select.h"
#include "src/smap/smap.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#if 1
void get_command(void)
{
	fatal("fixme");
}
#else

typedef struct {
	int color;
	char letter;
	List nodes;
	select_ba_request_t *request;
} allocated_block_t;

static int	_add_bg_record(select_ba_request_t *blockreq,
			       List allocated_blocks);
static int	_alter_allocation(char *com, List allocated_blocks);
static int	_change_state_all_bps(char *com, int state);
static int	_change_state_bps(char *com, int state);
static int	_copy_allocation(char *com, List allocated_blocks);
static int	_create_allocation(char *com, List allocated_blocks);
static void	_delete_allocated_blocks(List allocated_blocks);
static int	_load_configuration(char *com, List allocated_blocks);
static allocated_block_t *_make_request(select_ba_request_t *request);
static void	_print_header_command(void);
static void	_print_text_command(allocated_block_t *allocated_block);
static int	_remove_allocation(char *com, List allocated_blocks);
static int	_resolve(char *com);
static int	_save_allocation(char *com, List allocated_blocks);
static int      _set_layout(char *com);
static int      _set_base_part_cnt(char *com);
static int      _set_nodecard_cnt(char *com);

char error_string[255];
int base_part_node_cnt = 512;
int nodecard_node_cnt = 32;
char *layout_mode = "STATIC";

static void _delete_allocated_blocks(List allocated_blocks)
{
	allocated_block_t *allocated_block = NULL;

	while ((allocated_block = list_pop(allocated_blocks)) != NULL) {
		remove_block(allocated_block->nodes, 0,
			     allocated_block->request->conn_type);
		list_destroy(allocated_block->nodes);
		delete_ba_request(allocated_block->request);
		xfree(allocated_block);
	}
	list_destroy(allocated_blocks);
}

static allocated_block_t *_make_request(select_ba_request_t *request)
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
		char *pass = ba_passthroughs_string(request->deny_pass);
		if(pass) {
			sprintf(error_string,"THERE ARE PASSTHROUGHS IN "
				"THIS ALLOCATION DIM %s!!!!!!!", pass);
			xfree(pass);
		}

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
	return 1;
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
	int i=6, geoi=-1, starti=-1, i2=0, small32=-1, small128=-1;
	int len = strlen(com);
	allocated_block_t *allocated_block = NULL;
	select_ba_request_t *request;
	int diff=0;
#ifndef HAVE_BGL
	int small16=-1, small64=-1, small256=-1;
#endif
	request = (select_ba_request_t*) xmalloc(sizeof(select_ba_request_t));
	request->geometry[0] = (uint16_t)NO_VAL;
	request->conn_type=SELECT_TORUS;
	request->rotate = false;
	request->elongate = false;
	request->start_req=0;
	request->size = 0;
	request->small32 = 0;
	request->small128 = 0;
	request->deny_pass = 0;
	request->avail_node_bitmap = NULL;

	while(i<len) {
		if(!strncasecmp(com+i, "mesh", 4)) {
			request->conn_type=SELECT_MESH;
			i+=4;
		} else if(!strncasecmp(com+i, "small", 5)) {
			request->conn_type = SELECT_SMALL;
			i+=5;
		} else if(!strncasecmp(com+i, "deny", 4)) {
			i+=4;
			if(strstr(com+i, "X"))
				request->deny_pass |= PASS_DENY_X;
			if(strstr(com+i, "Y"))
				request->deny_pass |= PASS_DENY_Y;
			if(strstr(com+i, "Z"))
				request->deny_pass |= PASS_DENY_Z;
			if(!strcasecmp(com+i, "ALL"))
				request->deny_pass |= PASS_DENY_ALL;
		} else if(!strncasecmp(com+i, "nodecard", 8)) {
			small32=0;
			i+=8;
		} else if(!strncasecmp(com+i, "quarter", 7)) {
			small128=0;
			i+=7;
		} else if(!strncasecmp(com+i, "32CN", 4)) {
			small32=0;
			i+=4;
		} else if(!strncasecmp(com+i, "128CN", 5)) {
			small128=0;
			i+=5;
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
			  && ((com[i] >= '0' && com[i] <= '9')
			      || (com[i] >= 'A' && com[i] <= 'Z'))) {
			starti=i;
			i++;
		} else if(small32 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small32=i;
			i++;
		} else if(small128 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small128=i;
			i++;
		}
#ifndef HAVE_BGL
		else if(!strncasecmp(com+i, "16CN", 4)) {
			small16=0;
			i+=4;
		} else if(!strncasecmp(com+i, "64CN", 4)) {
			small64=0;
			i+=4;
		} else if(!strncasecmp(com+i, "256CN", 5)) {
			small256=0;
			i+=5;
		} else if(small16 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small16=i;
			i++;
		} else if(small64 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small64=i;
			i++;
		} else if(small256 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small256=i;
			i++;
		}
#endif
		else if(geoi<0 && ((com[i] >= '0' && com[i] <= '9')
				     || (com[i] >= 'A' && com[i] <= 'Z'))) {
			geoi=i;
			i++;
		} else {
			i++;
		}

	}

	if(request->conn_type == SELECT_SMALL) {
		int total = 512;
#ifndef HAVE_BGL
		if(small16 > 0) {
			request->small16 = atoi(&com[small16]);
			total -= request->small16 * 16;
		}

		if(small64 > 0) {
			request->small64 = atoi(&com[small64]);
			total -= request->small64 * 64;
		}

		if(small256 > 0) {
			request->small256 = atoi(&com[small256]);
			total -= request->small256 * 256;
		}
#endif

		if(small32 > 0) {
			request->small32 = atoi(&com[small32]);
			total -= request->small32 * 32;
		}

		if(small128 > 0) {
			request->small128 = atoi(&com[small128]);
			total -= request->small128 * 128;
		}
		if(total < 0) {
			sprintf(error_string,
				"You asked for %d more nodes than "
				"are in a Midplane\n", total * 2);
			geoi = -1;

		}

#ifndef HAVE_BGL
		while(total > 0) {
			if(total >= 256) {
				request->small256++;
				total -= 256;
			} else if(total >= 128) {
				request->small128++;
				total -= 128;
			} else if(total >= 64) {
				request->small64++;
				total -= 64;
			} else if(total >= 32) {
				request->small32++;
				total -= 32;
			} else if(total >= 16) {
				request->small16++;
				total -= 16;
			} else
				break;
		}
#else
		while(total > 0) {
			if(total >= 128) {
				request->small128++;
				total -= 128;
			} else if(total >= 32) {
				request->small32++;
				total -= 32;
			} else
				break;
		}
#endif
		request->size = 1;
/* 		sprintf(error_string, */
/* 			"got %d %d %d %d %d %d", */
/* 			total, request->small16, request->small32, */
/* 			request->small64, request->small128, */
/* 			request->small256); */
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
				diff = i2-geoi;
				/* for geometery */
				if(diff>1) {
					request->geometry[X] =
						xstrntol(&com[geoi],
							 NULL, diff,
							 10);
				} else {
					request->geometry[X] =
						xstrntol(&com[geoi],
							 NULL, diff,
							 params.cluster_base);
				}
				geoi += diff;
				diff = geoi;

				while(com[geoi-1]!='x' && geoi<len)
					geoi++;
				if(geoi==len)
					goto geo_error_message;
				diff = geoi - diff;
				if(diff>1) {
					request->geometry[Y] =
						xstrntol(&com[geoi],
							 NULL, diff,
							 10);
				} else {
					request->geometry[Y] =
						xstrntol(&com[geoi],
							 NULL, diff,
							 params.cluster_base);
				}
				geoi += diff;
				diff = geoi;
				while(com[geoi-1]!='x' && geoi<len)
					geoi++;
				if(geoi==len)
					goto geo_error_message;
				diff = geoi - diff;

				if(diff>1) {
					request->geometry[Z] =
						xstrntol(&com[geoi],
							 NULL, diff,
							 10);
				} else {
					request->geometry[Z] =
						xstrntol(&com[geoi],
							 NULL, diff,
							 params.cluster_base);
				}
				request->size = -1;
				break;
			}
			i2++;
		}

		if(request->start_req) {
			i2 = starti;
			while(com[i2]!='x' && i2<len)
				i2++;
			diff = i2-starti;
			if(diff>1) {
				request->start[X] = xstrntol(&com[starti],
							     NULL, diff,
							     10);
			} else {
				request->start[X] = xstrntol(&com[starti],
							     NULL, diff,
							     params.
							     cluster_base);
			}
			starti += diff;
			if(starti==len)
				goto start_request;

			starti++;
			i2 = starti;
			while(com[i2]!='x' && i2<len)
				i2++;
			diff = i2-starti;

			if(diff>1) {
				request->start[Y] = xstrntol(&com[starti],
							     NULL, diff,
							     10);
			} else {
				request->start[Y] = xstrntol(&com[starti],
							     NULL, diff,
							     params.cluster_base);
			}
			starti += diff;
			if(starti==len)
				goto start_request;

			starti++;
			i2 = starti;
			while(com[i2]!=' ' && i2<len)
				i2++;
			diff = i2-starti;

			if(diff>1) {
				request->start[Z] = xstrntol(&com[starti],
							     NULL, diff,
							     10);
			} else {
				request->start[Z] = xstrntol(&com[starti],
							     NULL, diff,
							     params.
							     cluster_base);
			}
		}
	start_request:
		if(!strcasecmp(layout_mode,"OVERLAP"))
			reset_ba_system(true);

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
	uint16_t *coord = NULL;
#endif

	while(com[i] != '\0') {
		if((i>0) && (com[i-1] != ' '))
			break;
		i++;
	}
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
				"%s resolves to X=%d Y=%d Z=%d\n",
				com+i,coord[X],coord[Y],coord[Z]);
		else
			sprintf(error_string, "%s has no resolve.\n",
				com+i);
	}
resolve_error:
#else
			sprintf(error_string,
				"Must be on BG SN to resolve.\n");
#endif
	wnoutrefresh(text_win);
	doupdate();

	return 1;
}
static int _change_state_all_bps(char *com, int state)
{
	char allnodes[50];
	memset(allnodes,0,50);

	if (params.cluster_dims == 4)
		sprintf(allnodes, "0000x%c%c%c%d",
			alpha_num[DIM_SIZE[0]-1], alpha_num[DIM_SIZE[1]-1],
			alpha_num[DIM_SIZE[2]-1], alpha_num[DIM_SIZE[3]-1]);
	else if (params.cluster_dims == 3)
		sprintf(allnodes, "000x%c%c%c",
			alpha_num[DIM_SIZE[0]-1], alpha_num[DIM_SIZE[1]-1],
			alpha_num[DIM_SIZE[2]-1]);
	else
		sprintf(allnodes, "0-%d",
			DIM_SIZE[X]);

	return _change_state_bps(allnodes, state);

}
static int _change_state_bps(char *com, int state)
{
	int i=0, x;
	int len = strlen(com);
	int start[params.cluster_dims], end[params.cluster_dims];
	int number=0, y=0, z=0, j=0;
	char letter = '.';
	char opposite = '#';
	bool used = false;
	char *c_state = "up";
	char *p = '\0';

	if(state == NODE_STATE_DOWN) {
		letter = '#';
		opposite = '.';
		used = true;
		c_state = "down";
	}
	while(i<len
	      && (com[i] < '0' || com[i] > 'Z'
		  || (com[i] > '9' && com[i] < 'A')))
		i++;
	if(i>(len-1)) {
		memset(error_string,0,255);
		sprintf(error_string,
			"You didn't specify any nodes to make %s. "
			"in statement '%s'",
			c_state, com);
		return 0;
	}

	if(params.cluster_dims == 1) {
		if ((com[i+3] == 'x')
		    || (com[i+3] == '-')) {
			start[X] =  xstrntol(com + i, NULL,
					     params.cluster_dims,
					     params.cluster_base);
			i += 4;
			end[X] =  xstrntol(com + i, NULL,
					   params.cluster_dims,
					   params.cluster_base);
		} else {
			start[X] = end[X] =  xstrntol(com + i, NULL,
						      params.cluster_dims,
						      params.cluster_base);
		}

		if((start[X]>end[X])
		   || (start[X]<0)
		   || (end[X]>DIM_SIZE[X]-1))
			goto error_message;

		for(x=start[X];x<=end[X];x++) {
			ba_system_ptr->grid[x][0][0].color = 0;
			ba_system_ptr->grid[x][0][0].letter = letter;
			ba_system_ptr->grid[x][0][0].used = used;
		}
		return 1;
	}

	if ((com[i+3] == 'x')
	    || (com[i+3] == '-')) {
		for(j=0; j<3; j++) {
			if (((i+j) <= len) &&
			    (((com[i+j] >= '0') && (com[i+j] <= '9')) ||
			     ((com[i+j] >= 'A') && (com[i+j] <= 'Z'))))
				continue;
			goto error_message2;

		}
		number = xstrntol(com + i, &p, params.cluster_dims,
				  params.cluster_base);
		hostlist_parse_int_to_array(
			number, start, params.cluster_dims,
			params.cluster_base);

		i += 4;
		for(j=0; j<3; j++) {
			if (((i+j) <= len) &&
			    (((com[i+j] >= '0') && (com[i+j] <= '9')) ||
			     ((com[i+j] >= 'A') && (com[i+j] <= 'Z'))))
				continue;
			goto error_message2;
		}
		number = xstrntol(com + i, &p, params.cluster_dims,
				  params.cluster_base);
		hostlist_parse_int_to_array(
			number, end, params.cluster_dims,
			params.cluster_base);
	} else {
		for(j=0; j<3; j++) {
			if (((i+j) <= len) &&
			    (((com[i+j] >= '0') && (com[i+j] <= '9')) ||
			     ((com[i+j] >= 'A') && (com[i+j] <= 'Z'))))
				continue;
			goto error_message2;
		}
		number = xstrntol(com + i, &p, params.cluster_dims,
				  params.cluster_base);
		hostlist_parse_int_to_array(
			number, start, params.cluster_dims,
			params.cluster_base);
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
	return 1;
error_message:
	memset(error_string,0,255);
	if(params.cluster_dims == 1) {
		sprintf(error_string,
			"Problem with nodes,  specified range was %d-%d",
			start[X],end[X]);
	} else {
		sprintf(error_string,
			"Problem with base partitions, "
			"specified range was %d%d%dx%d%d%d",
			alpha_num[start[X]],alpha_num[start[Y]],
			alpha_num[start[Z]],
			alpha_num[end[X]],alpha_num[end[Y]],alpha_num[end[Z]]);
	}
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
	int i=1, found=0;
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
					     color_count,
					     allocated_block->request->
					     conn_type);
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
	/* this doesn't do anything yet. */

	/* int torus=SELECT_TORUS, i=5, i2=0; */
/* 	int len = strlen(com); */
/* 	bool rotate = false; */
/* 	bool elongate = false; */

/* 	while(i<len) { */

/* 		while(com[i-1]!=' ' && i<len) { */
/* 			i++; */
/* 		} */
/* 		if(!strncasecmp(com+i, "mesh", 4)) { */
/* 			torus=SELECT_MESH; */
/* 			i+=4; */
/* 		} else if(!strncasecmp(com+i, "rotate", 6)) { */
/* 			rotate=true; */
/* 			i+=6; */
/* 		} else if(!strncasecmp(com+i, "elongate", 8)) { */
/* 			elongate=true; */
/* 			i+=8; */
/* 		} else if(com[i] < 58 && com[i] > 47) { */
/* 			i2=i; */
/* 			i++; */
/* 		} else { */
/* 			i++; */
/* 		} */

/* 	} */
	return 1;
}

static int _copy_allocation(char *com, List allocated_blocks)
{
	ListIterator results_i;
	allocated_block_t *allocated_block = NULL;
	allocated_block_t *temp_block = NULL;
	select_ba_request_t *request = NULL;

	int i = 1, j;
	int len = strlen(com);
	char letter = '\0';
	int count = 1;
	int *geo = NULL, *geo_ptr = NULL;

	/* look for the space after copy */
	while ((com[i-1] != ' ') && (i < len))
		i++;

	if (i <= len) {
		/* Here we are looking for a real number for the count
		 * instead of the params.cluster_base so atoi is ok */
		if ((com[i] >= '0') && (com[i] <= '9'))
			count = atoi(com+i);
		else {
			letter = com[i];
			i++;
			if (com[i] != '\n') {
				while ((com[i-1] != ' ') && (i < len))
					i++;

				if ((com[i] >= '0') && (com[i] <= '9'))
					count = atoi(com+i);
			}
		}
	}

	results_i = list_iterator_create(allocated_blocks);
	while((allocated_block = list_next(results_i)) != NULL) {
		temp_block = allocated_block;
		if (allocated_block->letter != letter)
			continue;
		break;
	}
	list_iterator_destroy(results_i);

	if (!letter)
		allocated_block = temp_block;

	if (!allocated_block) {
		memset(error_string, 0, 255);
		sprintf(error_string,
			"Could not find requested record to copy");
		return 0;
	}

	for (i = 0; i < count; i++) {
		request = (select_ba_request_t*)
			  xmalloc(sizeof(select_ba_request_t));
		for (j = 0; j < params.cluster_dims; j++) {
			request->geometry[j] = allocated_block->request->
					       geometry[j];
			request->conn_type[j] = allocated_block->request->
						conn_type[j];
		}
		request->size = allocated_block->request->size;
		request->rotate =allocated_block->request->rotate;
		request->elongate = allocated_block->request->elongate;
		request->deny_pass = allocated_block->request->deny_pass;
#ifndef HAVE_BGL
		request->small16 = allocated_block->request->small16;
		request->small64 = allocated_block->request->small64;
		request->small256 = allocated_block->request->small256;
#endif
		request->small32 = allocated_block->request->small32;
		request->small128 = allocated_block->request->small128;

		request->rotate_count= 0;
		request->elongate_count = 0;
	       	request->elongate_geos = list_create(NULL);
		request->avail_mp_bitmap = NULL;

		results_i = list_iterator_create(request->elongate_geos);
		while ((geo_ptr = list_next(results_i)) != NULL) {
			geo = xmalloc(sizeof(int) * params.cluster_dims);
			for (j = 0; j < params.cluster_dims; j++)
				geo[j] = geo_ptr[j];
			list_append(request->elongate_geos, geo);
		}
		list_iterator_destroy(results_i);

		if ((allocated_block = _make_request(request)) == NULL) {
			memset(error_string, 0, 255);
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
	char filename[50];
	char *save_string = NULL;
	FILE *file_ptr = NULL;
	char *conn_type = NULL;
	char *extra = NULL;

	ListIterator results_i;

	memset(filename,0,50);
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
		time_t now_time = time(NULL);
		sprintf(filename,"bluegene.conf.%ld",
			(long int) now_time);
	}

	file_ptr = fopen(filename,"w");

	if (file_ptr!=NULL) {
		xstrcat(save_string,
			"#\n# bluegene.conf file generated by smap\n");
		xstrcat(save_string,
			"# See the bluegene.conf man page for "
			"more information\n");
		xstrcat(save_string, "#\n");
#ifndef HAVE_BGL
		xstrcat(save_string, "CnloadImage="
			"/bgsys/drivers/ppcfloor/boot/cns,"
			"/bgsys/drivers/ppcfloor/boot/cnk\n");
		xstrcat(save_string, "MloaderImage="
			"/bgsys/drivers/ppcfloor/boot/uloader\n");
		xstrcat(save_string, "IoloadImage="
			"/bgsys/drivers/ppcfloor/boot/cns,"
			"/bgsys/drivers/ppcfloor/boot/linux,"
			"/bgsys/drivers/ppcfloor/boot/ramdisk\n");
#else
		xstrcat(save_string, "BlrtsImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/rts_hw.rts\n");
		xstrcat(save_string, "LinuxImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/zImage.elf\n");
		xstrcat(save_string, "MloaderImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/mmcs-mloader.rts\n");
		xstrcat(save_string, "RamDiskImage="
		       "/bgl/BlueLight/ppcfloor/bglsys/bin/ramdisk.elf\n");
#endif
		xstrcat(save_string, "BridgeAPILogFile="
		       "/var/log/slurm/bridgeapi.log\n");
#ifndef HAVE_BGL
		xstrcat(save_string, "Numpsets=4 # io poor\n");
		xstrcat(save_string, "# Numpsets=32 # io rich\n");
#else
		xstrcat(save_string, "Numpsets=8 # io poor\n");
		xstrcat(save_string, "# Numpsets=64 # io rich\n");
#endif
		xstrcat(save_string, "BridgeAPIVerbose=2\n");

		xstrfmtcat(save_string, "BasePartitionNodeCnt=%d\n",
			   base_part_node_cnt);
		xstrfmtcat(save_string, "NodeCardNodeCnt=%d\n",
			   nodecard_node_cnt);
		if(!list_count(allocated_blocks))
			xstrcat(save_string, "LayoutMode=DYNAMIC\n");
		else {
			xstrfmtcat(save_string, "LayoutMode=%s\n", layout_mode);
			xstrfmtcat(save_string, "#\n# Block Layout\n#\n");
		}
		results_i = list_iterator_create(allocated_blocks);
		while ((allocated_block = list_next(results_i)) != NULL) {
			if (allocated_block->request->conn_type[0] ==
			    SELECT_TORUS)
				conn_type = "TORUS";
			else if(allocated_block->request->conn_type
				== SELECT_MESH)
				conn_type = "MESH";
			else {
				conn_type = "SMALL";
#ifndef HAVE_BGL
				xstrfmtcat(extra,
					   " 16CNBlocks=%d 32CNBlocks=%d "
					   "64CNBlocks=%d 128CNBlocks=%d "
					   "256CNBlocks=%d",
					   allocated_block->request->small16,
					   allocated_block->request->small32,
					   allocated_block->request->small64,
					   allocated_block->request->small128,
					   allocated_block->request->small256);
#else
				xstrfmtcat(extra,
					   " 32CNBlocks=%d 128CNBlocks=%d",
					   allocated_block->request->small32,
					   allocated_block->request->small128);

#endif
			}
			xstrfmtcat(save_string, "BPs=%s Type=%s",
				   allocated_block->request->save_name,
				   conn_type);
			if(extra) {
				xstrfmtcat(save_string, "%s\n", extra);
				xfree(extra);
			} else
				xstrcat(save_string, "\n");

		}
		list_iterator_destroy(results_i);
		fputs(save_string, file_ptr);
		xfree(save_string);
		fclose (file_ptr);
	}

	return 1;
}

static int _coord(char coord)
{
	if ((coord >= '0') && (coord <= '9'))
		return (coord - '0');
	if ((coord >= 'A') && (coord <= 'Z'))
		return (coord - 'A') + 10;
	return -1;
}

/* increment an array, return false if can't be incremented (reached limts) */
static bool _incr_coord(int *start, int *end, int *current)
{
	int i;

	for (i = 0; i < params.cluster_dims; i++) {
		current[i]++;
		if (current[i] <= end[i])
			return true;
		current[i] = start[i];
	}
	return false;
}

static int _add_bg_record(select_ba_request_t *blockreq, List allocated_blocks)
{
#ifdef HAVE_BG
	char *nodes = NULL, *conn_type = NULL;
	int bp_count = 0;
	int diff = 0;
	int largest_diff = -1;
	int my_coord[params.cluster_dims];
	int start[params.cluster_dims];
	int end[params.cluster_dims];
	int start1[params.cluster_dims];
	int end1[params.cluster_dims];
	int geo[params.cluster_dims];
	char com[255];
	int i, j = 0;
	int len = 0;

	for (i = 0; i < params.cluster_dims; i++) {
		geo[i]    = 0;
		start1[i] = -1;
		end1[i]   = -1;
	}

	switch(blockreq->conn_type[0]) {
	case SELECT_MESH:
		conn_type = "mesh";
		break;
	case SELECT_SMALL:
		conn_type = "small";
		break;
	case SELECT_TORUS:
	default:
		conn_type = "torus";
		break;
	}

	nodes = blockreq->save_name;
	if (!nodes)
		return SLURM_SUCCESS;
	len = strlen(nodes);
	while (nodes[j] != '\0') {
		int mid = j   + params.cluster_dims + 1;
		int fin = mid + params.cluster_dims + 1;
		if (((nodes[j] == '[')   || (nodes[j] == ','))   &&
		    ((nodes[mid] == 'x') || (nodes[mid] == '-')) &&
		    ((nodes[fin] == ']') || (nodes[fin] == ','))) {
			j++;	/* Skip leading '[' or ',' */
			for (i = 0; i < params.cluster_dims; i++, j++) {
				start[i] = _coord(nodes[j]);
				my_coord[i] = start[i];
			}
			j++;	/* Skip middle 'x' or '-' */
			for (i = 0; i < params.cluster_dims; i++, j++)
				end[i] = _coord(nodes[j]);
			diff = end[0] - start[0];
			if (diff > largest_diff) {
				for (i = 0; i < params.cluster_dims; i++)
					start1[i] = start[i];
			}
			do {
				for (i = 0; i < params.cluster_dims; i++) {
					if (my_coord[i] > end1[i]) {
						end1[i] = my_coord[i];
						geo[i] = end1[i] -start1[i] +1;
					}
				}
				bp_count++;
			} while(_incr_coord(start, end, my_coord));
			if (nodes[j] != ',')
				break;
			j--;
		} else if (((nodes[j] >= '0') && (nodes[j] <= '9')) ||
			   ((nodes[j] >= 'A') && (nodes[j] <= 'Z'))) {
			for (i = 0; i < params.cluster_dims; i++, j++)
				start[i] = _coord(nodes[j]);
			diff = 0;
			if (diff > largest_diff) {
				for (i = 0; i < params.cluster_dims; i++)
					start1[i] = start[i];
			}
			for (i = 0; i < params.cluster_dims; i++) {
				if (start[i] > end1[i]) {
					end1[i] = start[i];
					geo[i] = end1[i] - start1[i] + 1;
				}
			}
			bp_count++;
			if (nodes[j] != ',')
				break;
			j--;
		}
		j++;
	}
	memset(com, 0, 255);
	if (params.cluster_dims == 4) {
		sprintf(com, "create %dx%dx%dx%d %s start %dx%dx%dx%d "
			"small32=%d small128=%d",
			geo[0], geo[1], geo[2], geo[3], conn_type,
			start1[0], start1[1], start1[2], start1[3],
			blockreq->small32, blockreq->small128);
	} else {
		sprintf(com, "create %dx%dx%d %s start %dx%dx%d "
			"small32=%d small128=%d",
			geo[0], geo[1], geo[2], conn_type,
			start1[0], start1[1], start1[2],
			blockreq->small32, blockreq->small128);
	}
	if (!strcasecmp(layout_mode, "OVERLAP"))
		reset_ba_system(false);

	set_all_bps_except(nodes);
	_create_allocation(com, allocated_blocks);
	reset_all_removed_bps();

#endif
	return SLURM_SUCCESS;
}
static int _load_configuration(char *com, List allocated_blocks)
{
	int len = strlen(com);
	int i=5, j=0;
	char filename[100];
	s_p_hashtbl_t *tbl = NULL;
	char *layout = NULL;
	select_ba_request_t **blockreq_array = NULL;
	int count = 0;

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

	tbl = s_p_hashtbl_create(bg_conf_file_options);
	if(s_p_parse_file(tbl, NULL, filename) == SLURM_ERROR) {
		memset(error_string,0,255);
		sprintf(error_string, "ERROR: couldn't open/read %s",
			filename);
		return 0;
	}

	if (!s_p_get_string(&layout, "LayoutMode", tbl)) {
		memset(error_string,0,255);
		sprintf(error_string,
			"Warning: LayoutMode was not specified in "
			"bluegene.conf defaulting to STATIC partitioning");
	} else {
		_set_layout(layout);
		xfree(layout);
	}

	if(strcasecmp(layout_mode, "DYNAMIC")) {
		if (!s_p_get_array((void ***)&blockreq_array,
				   &count, "BPs", tbl)) {
			memset(error_string,0,255);
			sprintf(error_string,
				"WARNING: no blocks defined in "
				"bluegene.conf");
		}

		for (i = 0; i < count; i++) {
			_add_bg_record(blockreq_array[i], allocated_blocks);
		}
	}

	s_p_hashtbl_destroy(tbl);
	return 1;
}

static void _print_header_command(void)
{
	main_ycord=2;
	mvwprintw(text_win, main_ycord,
		  main_xcord, "ID");
	main_xcord += 4;
	mvwprintw(text_win, main_ycord,
		  main_xcord, "TYPE");
	main_xcord += 7;
	mvwprintw(text_win, main_ycord,
		  main_xcord, "ROTATE");
	main_xcord += 7;
	mvwprintw(text_win, main_ycord,
		  main_xcord, "ELONG");
	main_xcord += 7;
#ifdef HAVE_BG
	mvwprintw(text_win, main_ycord,
		  main_xcord, "BP_COUNT");
#else
	mvwprintw(text_win, main_ycord,
		  main_xcord, "NODES");
#endif
	main_xcord += 10;

#ifndef HAVE_BGL
	mvwprintw(text_win, main_ycord,
		  main_xcord, "16CN");
	main_xcord += 5;
#endif
	mvwprintw(text_win, main_ycord,
		  main_xcord, "32CN");
	main_xcord += 5;
#ifndef HAVE_BGL
	mvwprintw(text_win, main_ycord,
		  main_xcord, "64CN");
	main_xcord += 5;
#endif
	mvwprintw(text_win, main_ycord,
		  main_xcord, "128CN");
	main_xcord += 6;
#ifndef HAVE_BGL
	mvwprintw(text_win, main_ycord,
		  main_xcord, "256CN");
	main_xcord += 6;
#endif
#ifdef HAVE_BG
	mvwprintw(text_win, main_ycord,
		  main_xcord, "BP_LIST");
#else
	mvwprintw(text_win, main_ycord,
		  main_xcord, "NODELIST");
#endif
	main_xcord = 1;
	main_ycord++;
}

static void _print_text_command(allocated_block_t *allocated_block)
{
	wattron(text_win,
		COLOR_PAIR(allocated_block->color));

	mvwprintw(text_win, main_ycord,
		  main_xcord, "%c", allocated_block->letter);
	main_xcord += 4;
	if (allocated_block->request->conn_type[0] == SELECT_TORUS)
		mvwprintw(text_win, main_ycord,
			  main_xcord, "TORUS");
	else if (allocated_block->request->conn_type[0] == SELECT_MESH)
		mvwprintw(text_win, main_ycord,
			  main_xcord, "MESH");
	else
		mvwprintw(text_win, main_ycord,
			  main_xcord, "SMALL");
	main_xcord += 7;

	if(allocated_block->request->rotate)
		mvwprintw(text_win, main_ycord,
			  main_xcord, "Y");
	else
		mvwprintw(text_win, main_ycord,
			  main_xcord, "N");
	main_xcord += 7;

	if(allocated_block->request->elongate)
		mvwprintw(text_win, main_ycord,
			  main_xcord, "Y");
	else
		mvwprintw(text_win, main_ycord,
			  main_xcord, "N");
	main_xcord += 7;

	mvwprintw(text_win, main_ycord,
		  main_xcord, "%d", allocated_block->request->size);
	main_xcord += 10;

	if (allocated_block->request->conn_type[0] == SELECT_SMALL) {
#ifndef HAVE_BGL
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%d",
			  allocated_block->request->small16);
		main_xcord += 5;
#endif
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%d",
			  allocated_block->request->small32);
		main_xcord += 5;
#ifndef HAVE_BGL
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%d",
			  allocated_block->request->small64);
		main_xcord += 5;
#endif
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%d",
			  allocated_block->request->small128);
		main_xcord += 6;
#ifndef HAVE_BGL
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%d",
			  allocated_block->request->small256);
		main_xcord += 6;
#endif

	} else
#ifndef HAVE_BGL
		main_xcord += 27;
#else
		main_xcord += 11;
#endif

	mvwprintw(text_win, main_ycord,
		  main_xcord, "%s",
		  allocated_block->request->save_name);
	main_xcord = 1;
	main_ycord++;
	wattroff(text_win,
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
		select_g_ba_fini();
		exit(0);
	}
	init_wires();
	allocated_blocks = list_create(NULL);

	text_width = text_win->_maxx;
	text_startx = text_win->_begx;
	command_win = newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();

	while (strcmp(com, "quit")) {
		clear_window(grid_win);
		print_grid(0);
		clear_window(text_win);
		box(text_win, 0, 0);
		box(grid_win, 0, 0);

		if (!params.no_header)
			_print_header_command();

		if(error_string!=NULL) {
			i=0;
			while(error_string[i]!='\0') {
				if(error_string[i]=='\n') {
					main_ycord++;
					main_xcord=1;
					i++;
				}
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord,
					  "%c",
					  error_string[i++]);
				main_xcord++;
			}
			main_ycord++;
			main_xcord=1;
			memset(error_string,0,255);
		}
		results_i = list_iterator_create(allocated_blocks);

		count = list_count(allocated_blocks)
			- (LINES-(main_ycord+5));

		if(count<0)
			count=0;
		i=0;
		while((allocated_block = list_next(results_i)) != NULL) {
			if(i>=count)
				_print_text_command(allocated_block);
			i++;
		}
		list_iterator_destroy(results_i);

		wnoutrefresh(text_win);
		wnoutrefresh(grid_win);
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
			select_g_ba_fini();
			exit(0);
		}

		if (!strcmp(com, "quit") || !strcmp(com, "\\q")) {
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
			mvwprintw(text_win,
				main_ycord,
				main_xcord, "%s", com);
		} else if (!strncasecmp(com, "drain", 5)) {
			mvwprintw(text_win,
				main_ycord,
				main_xcord, "%s", com);
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

	clear_window(text_win);
	main_xcord = 1;
	main_ycord = 1;
	print_date();
	get_job();
	return;
}
#endif
