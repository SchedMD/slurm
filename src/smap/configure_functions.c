/*****************************************************************************\
 *  configure_functions.c - Functions related to configure mode of smap.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Copyright (C) 2011 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@schedmd.com>
 *
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "src/smap/smap.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"
#include "src/common/proc_args.h"

///////////////////////////////////////////////////////////////////////

typedef struct {
	int color;
	int color_count;
	char letter;
	List nodes;
	select_ba_request_t *request;
} allocated_block_t;

static int	_add_bg_record(select_ba_request_t *blockreq,
			       List allocated_blocks);
static int	_change_state_all_bps(char *com, int state);
static int	_change_state_bps(char *com, int state);
static int	_copy_allocation(char *com, List allocated_blocks);
static int	_create_allocation(char *com, List allocated_blocks);
static int	_load_configuration(char *com, List allocated_blocks);
static allocated_block_t *_make_request(select_ba_request_t *request);
static void	_print_header_command(void);
static void	_print_text_command(allocated_block_t *allocated_block);
static int	_remove_allocation(char *com, List allocated_blocks);
static int	_resolve(char *com);
static int	_save_allocation(char *com, List allocated_blocks);
static int      _set_layout(char *com);
static int      _set_midplane_cnode_cnt(char *com);
static int      _set_nodecard_cnt(char *com);

int color_count = 0;
char error_string[255];
int midplane_cnode_cnt = 512;
int nodecard_node_cnt = 32;
char *layout_mode = "STATIC";

static void _set_nodes(List nodes, int color, char letter)
{
	ListIterator itr;
	smap_node_t *smap_node;
	ba_mp_t *ba_mp;

	if (!nodes || !smap_system_ptr)
		return;

	itr = list_iterator_create(nodes);
	while ((ba_mp = list_next(itr))) {
		if (!ba_mp->used)
			continue;
		smap_node = smap_system_ptr->grid[ba_mp->index];
		smap_node->color = color;
		smap_node->letter = letter;
	}
	list_iterator_destroy(itr);
	return;
}

static void _destroy_allocated_block(void *object)
{
	allocated_block_t *allocated_block = (allocated_block_t *)object;

	if (allocated_block) {
		bool is_small = (allocated_block->request->conn_type[0] >=
				 SELECT_SMALL);
		if (allocated_block->nodes) {
			_set_nodes(allocated_block->nodes, 0, '.');
			bg_configure_remove_block(
				allocated_block->nodes, is_small);
			list_destroy(allocated_block->nodes);
		}
		destroy_select_ba_request(allocated_block->request);
		xfree(allocated_block);
	}
}

static allocated_block_t *_make_request(select_ba_request_t *request)
{
	List results;
	allocated_block_t *allocated_block = NULL;

#ifdef HAVE_BGQ
	results = list_create(bg_configure_destroy_ba_mp);
#else
	results = list_create(NULL);
#endif

	if (bg_configure_allocate_block(request, results)) {
		char *pass = bg_configure_ba_passthroughs_string(
			request->deny_pass);
		if (pass) {
			sprintf(error_string,"THERE ARE PASSTHROUGHS IN "
				"THIS ALLOCATION DIM %s!!!!!!!", pass);
			xfree(pass);
		}

		allocated_block = xmalloc(sizeof(allocated_block_t));
		allocated_block->request = request;
		allocated_block->nodes = results;
		allocated_block->letter = letters[color_count%62];
		allocated_block->color  = colors[color_count%6];
		allocated_block->color_count = color_count++;
		_set_nodes(allocated_block->nodes,
			   allocated_block->color,
			   allocated_block->letter);
		results = NULL;
	}

	if (results)
		list_destroy(results);
	return allocated_block;

}

static int _full_request(select_ba_request_t *request,
			 bitstr_t *usable_mp_bitmap,
			 List allocated_blocks)
{
	char *tmp_char = NULL, *tmp_char2 = NULL;
	allocated_block_t *allocated_block;
	int rc = 1;

	if (!strcasecmp(layout_mode,"OVERLAP"))
		bg_configure_reset_ba_system(true);

	if (usable_mp_bitmap)
		bg_configure_ba_set_removable_mps(usable_mp_bitmap, 1);

	/*
	 * Here is where we do the allocating of the partition.
	 * It will send a request back which we will throw into
	 * a list just incase we change something later.
	 */
	if (!bg_configure_new_ba_request(request)) {
		memset(error_string, 0, 255);
		if (request->size != -1) {
			sprintf(error_string,
				"Problems with request for %d\n"
				"Either you put in something "
				"that doesn't work,\n"
				"or we are unable to process "
				"your request.",
				request->size);
			rc = 0;
		} else {
			tmp_char = bg_configure_give_geo(request->geometry,
							 params.cluster_dims,
							 1);
			sprintf(error_string,
				"Problems with request of size %s\n"
				"Either you put in something "
				"that doesn't work,\n"
				"or we are unable to process "
				"your request.",
				tmp_char);
			xfree(tmp_char);
			rc = 0;
		}
	} else {
		if ((allocated_block = _make_request(request)) != NULL)
			list_append(allocated_blocks, allocated_block);
		else {
			if (request->geometry[0] != (uint16_t)NO_VAL)
				tmp_char = bg_configure_give_geo(
					request->geometry,
					params.cluster_dims, 1);
			tmp_char2 = bg_configure_give_geo(request->start,
							  params.cluster_dims,
							  1);

			memset(error_string, 0, 255);
			sprintf(error_string,
				"allocate failure\nSize requested "
				"was %d MidPlanes\n",
				request->size);
			if (tmp_char) {
				sprintf(error_string + strlen(error_string),
					"Geo requested was %s\n", tmp_char);
				xfree(tmp_char);
			} else {
				sprintf(error_string + strlen(error_string),
					"No geometry could be laid out "
					"for that size\n");
			}
			sprintf(error_string + strlen(error_string),
				"Start position was %s", tmp_char2);
			xfree(tmp_char2);
			rc = 0;
		}
	}

	if (usable_mp_bitmap)
		bg_configure_ba_reset_all_removed_mps();

	return rc;
}

static int _set_layout(char *com)
{
	int i;

	for (i = 0; com[i]; i++) {
		if (!strncasecmp(com+i, "dynamic", 7)) {
			layout_mode = "DYNAMIC";
			break;
		} else if (!strncasecmp(com+i, "static", 6)) {
			layout_mode = "STATIC";
			break;
		} else if (!strncasecmp(com+i, "overlap", 7)) {
			layout_mode = "OVERLAP";
			break;
		}
	}
	if (com[i] == '\0') {
		sprintf(error_string,
			"You didn't put in a mode that I recognized. \n"
			"Please use STATIC, OVERLAP, or DYNAMIC\n");
		return 0;
	}
	sprintf(error_string,
		"LayoutMode set to %s\n", layout_mode);
	return 1;
}

static int _set_midplane_cnode_cnt(char *com)
{
	int i;

	for (i = 0; com[i]; i++) {
		if ((com[i] >= '0') && (com[i] <= '9'))
			break;
	}
	if (com[i] == '\0') {
		sprintf(error_string,
			"I didn't notice the number you typed in\n");
		return 0;
	}

	midplane_cnode_cnt = atoi(&com[i]);
	memset(error_string, 0, 255);
	sprintf(error_string,
		"MidplaneNodeCnt set to %d\n", midplane_cnode_cnt);

	return 1;
}

static int _set_nodecard_cnt(char *com)
{
	int i;

	for (i = 0; com[i]; i++) {
		if ((com[i] >= '0') && (com[i] <= '9'))
			break;
	}
	if (com[i] == '\0') {
		sprintf(error_string,
			"I didn't notice the number you typed in\n");
		return 0;
	}

	nodecard_node_cnt = atoi(&com[i]);
	memset(error_string, 0, 255);
	sprintf(error_string,
		"NodeCardNodeCnt set to %d\n", nodecard_node_cnt);

	return 1;
}

static int _xlate_coord(char *str, int len)
{
	if (len > 1)
		return xstrntol(str, NULL, len, 10);
	else
		return xstrntol(str, NULL, len, params.cluster_base);
}

static int _create_allocation(char *com, List allocated_blocks)
{
	int i=6, j, geoi=-1, starti=-1, i2=0, small32=-1, small128=-1;
	int len = strlen(com);
	select_ba_request_t *request;
	char fini_char;
	int diff=0;
#ifndef HAVE_BGL
#ifdef HAVE_BGP
	int small16=-1;
#endif
	int small64=-1, small256=-1;
#endif
	request = (select_ba_request_t*) xmalloc(sizeof(select_ba_request_t));
	request->rotate = false;
	request->elongate = false;
	request->start_req = 0;
	request->size = 0;
	request->small32 = 0;
	request->small128 = 0;
	request->deny_pass = 0;
	request->avail_mp_bitmap = NULL;
	for (j = 0; j < params.cluster_dims; j++) {
		request->geometry[j]  = (uint16_t) NO_VAL;
		request->conn_type[j] = SELECT_TORUS;
	}

	while (i < len) {
		if (!strncasecmp(com+i, "mesh", 4)
		    || !strncasecmp(com+i, "small", 5)
		    || !strncasecmp(com+i, "torus", 5)) {
			char conn_type[200];
			j = i;
			while (j < len) {
				if (com[j] == ' ')
					break;
				conn_type[j-i] = com[j];
				j++;
				if (j >= 200)
					break;
			}
			conn_type[(j-i)+1] = '\0';
			verify_conn_type(conn_type, request->conn_type);
			i += j;
		} else if (!strncasecmp(com+i, "deny", 4)) {
			i += 4;
			if (strstr(com+i, "A"))
				request->deny_pass |= PASS_DENY_A;
			if (strstr(com+i, "X"))
				request->deny_pass |= PASS_DENY_X;
			if (strstr(com+i, "Y"))
				request->deny_pass |= PASS_DENY_Y;
			if (strstr(com+i, "Z"))
				request->deny_pass |= PASS_DENY_Z;
			if (!strcasecmp(com+i, "ALL"))
				request->deny_pass |= PASS_DENY_ALL;
		} else if (!strncasecmp(com+i, "nodecard", 8)) {
			small32 = 0;
			i += 8;
		} else if (!strncasecmp(com+i, "quarter", 7)) {
			small128 = 0;
			i += 7;
		} else if (!strncasecmp(com+i, "32CN", 4)) {
			small32 = 0;
			i += 4;
		} else if (!strncasecmp(com+i, "128CN", 5)) {
			small128 = 0;
			i += 5;
		} else if (!strncasecmp(com+i, "rotate", 6)) {
			request->rotate = true;
			i += 6;
		} else if (!strncasecmp(com+i, "elongate", 8)) {
			request->elongate = true;
			i += 8;
		} else if (!strncasecmp(com+i, "start", 5)) {
			request->start_req = 1;
			i += 5;
		} else if (request->start_req && (starti < 0) &&
			   (((com[i] >= '0') && (com[i] <= '9')) ||
			    ((com[i] >= 'A') && (com[i] <= 'Z')))) {
			starti = i;
			i++;
		} else if (small32 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small32 = i;
			i++;
		} else if (small128 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small128 = i;
			i++;
		}
#ifdef HAVE_BGP
		else if (!strncasecmp(com+i, "16CN", 4)) {
			small16 = 0;
			i += 4;
		} else if (small16 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small16 = i;
			i++;
		}
#endif
#ifndef HAVE_BGL
		else if (!strncasecmp(com+i, "64CN", 4)) {
			small64 = 0;
			i += 4;
		} else if (!strncasecmp(com+i, "256CN", 5)) {
			small256 = 0;
			i += 5;
		} else if (small64 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small64 = i;
			i++;
		} else if (small256 == 0 && (com[i] >= '0' && com[i] <= '9')) {
			small256 = i;
			i++;
		}
#endif
		else if ((geoi < 0) &&
			 (((com[i] >= '0') && (com[i] <= '9')) ||
			  ((com[i] >= 'A') && (com[i] <= 'Z')))) {
			geoi = i;
			i++;
		} else {
			i++;
		}

	}

	if (request->conn_type[0] >= SELECT_SMALL) {
		int total = 512;
#ifdef HAVE_BGP
		if (small16 > 0) {
			request->small16 = atoi(&com[small16]);
			total -= request->small16 * 16;
		}
#endif
#ifndef HAVE_BGL
		if (small64 > 0) {
			request->small64 = atoi(&com[small64]);
			total -= request->small64 * 64;
		}

		if (small256 > 0) {
			request->small256 = atoi(&com[small256]);
			total -= request->small256 * 256;
		}
#endif

		if (small32 > 0) {
			request->small32 = atoi(&com[small32]);
			total -= request->small32 * 32;
		}

		if (small128 > 0) {
			request->small128 = atoi(&com[small128]);
			total -= request->small128 * 128;
		}
		if (total < 0) {
			sprintf(error_string,
				"You asked for %d more nodes than "
				"are in a Midplane\n", total * 2);
			geoi = -1;

		}

#ifndef HAVE_BGL
		while (total > 0) {
			if (total >= 256) {
				request->small256++;
				total -= 256;
			} else if (total >= 128) {
				request->small128++;
				total -= 128;
			} else if (total >= 64) {
				request->small64++;
				total -= 64;
			} else if (total >= 32) {
				request->small32++;
				total -= 32;
			}
#ifdef HAVE_BGP
			else if (total >= 16) {
				request->small16++;
				total -= 16;
			}
#endif
			else
				break;
		}
#else
		while (total > 0) {
			if (total >= 128) {
				request->small128++;
				total -= 128;
			} else if (total >= 32) {
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

	if ((geoi < 0) && !request->size) {
		memset(error_string, 0, 255);
		sprintf(error_string,
			"No size or dimension specified, please re-enter");
	} else {
		i2 = geoi;
		while (i2 < len) {
			if (request->size)
				break;
			if ((com[i2] == ' ') || (i2 == (len-1))) {
				char *p;
				/* for size */
				request->size = strtol(&com[geoi], &p, 10);
				if (*p == 'k' || *p == 'K') {
					request->size *= 2; /* (1024 / 512) */
					p++;
				}
				break;
			}
			if (com[i2]=='x') {
				request->size = -1;
				diff = i2 - geoi;
				/* for geometery */
				request->geometry[0] = _xlate_coord(&com[geoi],
								    diff);
				for (j = 1; j < params.cluster_dims; j++) {
					geoi += diff;
					diff = geoi;
					while ((com[geoi-1]!='x') && com[geoi])
						geoi++;
					if (com[geoi] == '\0')
						goto geo_error_message;
					diff = geoi - diff;
					request->geometry[j] =
						_xlate_coord(&com[geoi], diff);
				}
				break;
			}
			i2++;
		}

		if (request->start_req) {
			i2 = starti;
			while (com[i2]!='x' && i2<len)
				i2++;
			diff = i2-starti;
			request->start[0] = _xlate_coord(&com[starti], diff);

			for (j = 1; j < params.cluster_dims; j++) {
				starti += diff;
				if (starti == len)
					goto start_request;
				starti++;
				i2 = starti;
				if (j == (params.cluster_dims - 1))
					fini_char = ' ';
				else
					fini_char = 'x';
				while ((com[i2] != fini_char) && com[i2])
					i2++;
				diff = i2 - starti;
				request->start[j] = _xlate_coord(&com[starti],
								 diff);
			}
		}
	start_request:
		if (!_full_request(request, NULL, allocated_blocks))
			destroy_select_ba_request(request);

	}
	return 1;

geo_error_message:
	destroy_select_ba_request(request);
	memset(error_string, 0, 255);
	sprintf(error_string,
		"Error in geo dimension specified, please re-enter");

	return 0;
}

static int _resolve(char *com)
{
	int i=0;
	char *ret_str;

	while (com[i] != '\0') {
		if ((i>0) && (com[i-1] != ' '))
			break;
		i++;
	}
	if (com[i] == 'r')
		com[i] = 'R';
	ret_str = resolve_mp(com+i, NULL);
	if (ret_str) {
		snprintf(error_string, sizeof(error_string), "%s", ret_str);
		xfree(ret_str);
	}

	if (params.commandline)
		printf("%s", error_string);
	else {
		wnoutrefresh(text_win);
		doupdate();
	}

	return 1;
}

static int _change_state_all_bps(char *com, int state)
{
	char start_loc[32], end_loc[32];
	char allnodes[50];
	int i;

	xassert(params.cluster_dims < 31);
	for (i = 0; i < params.cluster_dims; i++) {
		start_loc[i] = '0';
		end_loc[i]   = alpha_num[dim_size[i] - 1];
	}
	start_loc[i] = '\0';
	end_loc[i]   = '\0';

	memset(allnodes, 0, 50);
	sprintf(allnodes, "%sx%s", start_loc, end_loc);

	return _change_state_bps(allnodes, state);

}
static int _change_state_bps(char *com, int state)
{
	char *host;
	int i = 0;
	uint16_t pos[params.cluster_dims];
	char letter = '.';
	bool used = false;
	char *c_state = "up";
	hostlist_t hl = NULL;
	int rc = 1;

	if (state == NODE_STATE_DOWN) {
		letter = '#';
		used = true;
		c_state = "down";
	}

	while (com[i] && (com[i] != '[') &&
	       ((com[i] < '0') || (com[i] > '9')) &&
	       ((com[i] < 'A') || (com[i] > 'Z')))
		i++;
	if (com[i] == '\0') {
		memset(error_string, 0, 255);
		sprintf(error_string,
			"You didn't specify any nodes to make %s. "
			"in statement '%s'",
			c_state, com);
		return 0;
	}

	if (!(hl = hostlist_create(com+i))) {
		memset(error_string, 0, 255);
		sprintf(error_string, "Bad hostlist given '%s'", com+i);
		return 0;

	}

	while ((host = hostlist_shift(hl))) {
		ba_mp_t *ba_mp;
		smap_node_t *smap_node;

		for (i = 0; i < params.cluster_dims; i++)
			pos[i] = select_char2coord(host[i]);
		if (!(ba_mp = bg_configure_coord2ba_mp(pos))) {
			memset(error_string, 0, 255);
			sprintf(error_string, "Bad host given '%s'", host);
			rc = 0;
			break;
		}
		bg_configure_ba_update_mp_state(ba_mp, state);
		smap_node = smap_system_ptr->grid[ba_mp->index];
		smap_node->color = 0;
		smap_node->letter = letter;
		smap_node->used = used;
		free(host);
	}
	hostlist_destroy(hl);

	return rc;
}

static int _remove_allocation(char *com, List allocated_blocks)
{
	ListIterator results_i;
	allocated_block_t *allocated_block = NULL;
	int i=1, found=0;
	int len = strlen(com);
	char letter;

	while (com[i-1]!=' ' && i<len) {
		i++;
	}

	if (i>(len-1)) {
		memset(error_string, 0, 255);
		sprintf(error_string,
			"You need to specify which letter to delete.");
		return 0;
	} else {
		letter = com[i];
		results_i = list_iterator_create(allocated_blocks);
		while ((allocated_block = list_next(results_i)) != NULL) {
			if (found) {
				allocated_block->letter =
					letters[color_count%62];
				allocated_block->color =
					colors[color_count%6];
				allocated_block->color_count = color_count++;
				_set_nodes(allocated_block->nodes,
					   allocated_block->color,
					   allocated_block->letter);
			} else if (allocated_block->letter == letter) {
				found = 1;
				color_count = allocated_block->color_count;
				list_delete_item(results_i);
			}
		}
		list_iterator_destroy(results_i);
	}

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
	while ((allocated_block = list_next(results_i)) != NULL) {
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
	char *extra = NULL;

	ListIterator results_i;

	memset(filename, 0, 50);
	if (len > 5)
		while (i<len) {

			while (com[i-1]!=' ' && i<len) {
				i++;
			}
			while (i<len && com[i]!=' ') {
				filename[j] = com[i];
				i++;
				j++;
			}
		}
	if (filename[0]=='\0') {
		time_t now_time = time(NULL);
		sprintf(filename,"bluegene.conf.%ld",
			(long int) now_time);
	}

	file_ptr = fopen(filename,"w");

	if (file_ptr!=NULL) {
		char *image_dir = NULL;

		xstrcat(save_string,
			"#\n# bluegene.conf file generated by smap\n");
		xstrcat(save_string,
			"# See the bluegene.conf man page for "
			"more information\n");
		xstrcat(save_string, "#\n");
#ifdef HAVE_BGL
		image_dir = "/bgl/BlueLight/ppcfloor/bglsys/bin";
		xstrfmtcat(save_string, "BlrtsImage=%s/rts_hw.rts\n",
			   image_dir);
		xstrfmtcat(save_string, "LinuxImage=%s/zImage.elf\n",
			   image_dir);
		xstrfmtcat(save_string,
			   "MloaderImage=%s/mmcs-mloader.rts\n",
			   image_dir);
		xstrfmtcat(save_string,
			   "RamDiskImage=%s/ramdisk.elf\n",
			   image_dir);

		xstrcat(save_string, "IONodesPerMP=8 # io poor\n");
		xstrcat(save_string, "# IONodesPerMP=64 # io rich\n");
#elif defined HAVE_BGP
		image_dir = "/bgsys/drivers/ppcfloor/boot";
		xstrfmtcat(save_string, "CnloadImage=%s/cns,%s/cnk\n",
			   image_dir, image_dir);
		xstrfmtcat(save_string, "MloaderImage=%s/uloader\n",
			   image_dir);
		xstrfmtcat(save_string,
			   "IoloadImage=%s/cns,%s/linux,%s/ramdisk\n",
			   image_dir, image_dir, image_dir);
		xstrcat(save_string, "IONodesPerMP=4 # io poor\n");
		xstrcat(save_string, "# IONodesPerMP=32 # io rich\n");
#else
		image_dir = "/bgsys/drivers/ppcfloor/boot";
		xstrfmtcat(save_string, "MloaderImage=%s/firmware\n",
			   image_dir);
		xstrcat(save_string, "IONodesPerMP=4 # io semi-poor\n");
		xstrcat(save_string, "# IONodesPerMP=16 # io rich\n");
#endif

		xstrcat(save_string, "BridgeAPILogFile="
		       "/var/log/slurm/bridgeapi.log\n");

		xstrcat(save_string, "BridgeAPIVerbose=2\n");

		xstrfmtcat(save_string, "MidPlaneNodeCnt=%d\n",
			   midplane_cnode_cnt);
		xstrfmtcat(save_string, "NodeCardNodeCnt=%d\n",
			   nodecard_node_cnt);
		if (!list_count(allocated_blocks))
			xstrcat(save_string, "LayoutMode=DYNAMIC\n");
		else {
			xstrfmtcat(save_string, "LayoutMode=%s\n", layout_mode);
			xstrfmtcat(save_string, "#\n# Block Layout\n#\n");
		}
		results_i = list_iterator_create(allocated_blocks);
		while ((allocated_block = list_next(results_i)) != NULL) {
			select_ba_request_t *request = allocated_block->request;

			if (request->small16 || request->small32
			    || request->small64 || request->small128
			    || request->small256) {
#ifdef HAVE_BGL
				xstrfmtcat(extra,
					   " 32CNBlocks=%d "
					   "128CNBlocks=%d",
					   request->small32,
					   request->small128);
#elif defined HAVE_BGP
				xstrfmtcat(extra,
					   " 16CNBlocks=%d "
					   "32CNBlocks=%d "
					   "64CNBlocks=%d "
					   "128CNBlocks=%d "
					   "256CNBlocks=%d",
					   request->small16,
					   request->small32,
					   request->small64,
					   request->small128,
					   request->small256);
#else
				xstrfmtcat(extra,
					   " 32CNBlocks=%d "
					   "64CNBlocks=%d "
					   "128CNBlocks=%d "
					   "256CNBlocks=%d",
					   request->small32,
					   request->small64,
					   request->small128,
					   request->small256);
#endif
			}

			xstrfmtcat(save_string, "MPs=%s", request->save_name);

			for (i=0; i<SYSTEM_DIMENSIONS; i++) {
				if (request->conn_type[i] == (uint16_t)NO_VAL)
					break;
				if (i)
					xstrcat(save_string, ",");
				else
					xstrcat(save_string, " Type=");
				xstrfmtcat(save_string, "%s", conn_type_string(
						   request->conn_type[i]));
#ifdef HAVE_BG_L_P
				break;
#endif
			}

			if (extra) {
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

static int _add_bg_record(select_ba_request_t *blockreq, List allocated_blocks)
{
	int rc = 1;
#ifdef HAVE_BG
	char *nodes = NULL, *host;
	int diff = 0;
	int largest_diff = -1;
	uint16_t start[params.cluster_dims];
	uint16_t end[params.cluster_dims];
	uint16_t best_start[params.cluster_dims];
	int i, j = 0;
	hostlist_t hl = NULL;
	bitstr_t *mark_bitmap = NULL;
	char tmp_char[params.cluster_dims+1],
		tmp_char2[params.cluster_dims+1];

	memset(tmp_char, 0, sizeof(tmp_char));
	memset(tmp_char2, 0, sizeof(tmp_char2));

	start[0] = end[0] = (int16_t)-1; /* Set this here just so Clang won't
					  * report false postive.
					  */
	for (i = 0; i < params.cluster_dims; i++) {
		best_start[0] = 0;
		blockreq->geometry[i] = 0;
		end[i] = (int16_t)-1;
	}

	nodes = blockreq->save_name;
	if (!nodes)
		return SLURM_SUCCESS;

	while (nodes[j] && (nodes[j] != '[') &&
	       ((nodes[j] < '0') || (nodes[j] > '9')) &&
	       ((nodes[j] < 'A') || (nodes[j] > 'Z')))
		j++;
	if (nodes[j] == '\0') {
		snprintf(error_string, sizeof(error_string),
			 "This block '%s' for some reason didn't contain "
			 "any midplanes.",
			 nodes);
		rc = 0;
		goto fini;
	}

	if (!(hl = hostlist_create(nodes+j))) {
		snprintf(error_string, sizeof(error_string),
			 "Bad hostlist given '%s'", nodes+j);
		rc = 0;
		goto fini;
	}
	/* figure out the geo and the size */
	mark_bitmap = bit_alloc(smap_system_ptr->node_cnt);
	while ((host = hostlist_shift(hl))) {
		ba_mp_t *ba_mp;
		uint16_t pos[params.cluster_dims];
		for (i = 0; i < params.cluster_dims; i++)
			pos[i] = select_char2coord(host[i]);
		free(host);
		if (!(ba_mp = bg_configure_coord2ba_mp(pos))) {
			memset(error_string, 0, 255);
			sprintf(error_string, "Bad host given '%s'", host);
			rc = 0;
			break;
		}
		bit_set(mark_bitmap, ba_mp->index);
		for (i = 0; i < params.cluster_dims; i++) {
			if (ba_mp->coord[i] > (int16_t)end[i]) {
				blockreq->geometry[i]++;
				end[i] = ba_mp->coord[i];
			}
		}
	}
	hostlist_destroy(hl);

	if (!rc)
		goto fini;

	/* figure out the start pos */
	while (nodes[j] != '\0') {
		int mid = j   + params.cluster_dims + 1;
		int fin = mid + params.cluster_dims + 1;
		if (((nodes[j] == '[')   || (nodes[j] == ','))   &&
		    ((nodes[mid] == 'x') || (nodes[mid] == '-')) &&
		    ((nodes[fin] == ']') || (nodes[fin] == ','))) {
			j++;	/* Skip leading '[' or ',' */
			for (i = 0; i < params.cluster_dims; i++, j++)
				start[i] = select_char2coord(nodes[j]);
			j++;	/* Skip middle 'x' or '-' */
			for (i = 0; i < params.cluster_dims; i++, j++)
				end[i] = select_char2coord(nodes[j]);
			diff = end[0] - start[0];
		} else if (((nodes[j] >= '0') && (nodes[j] <= '9')) ||
			   ((nodes[j] >= 'A') && (nodes[j] <= 'Z'))) {
			for (i = 0; i < params.cluster_dims; i++, j++)
				start[i] = select_char2coord(nodes[j]);
			diff = 0;
		} else {
			j++;
			continue;
		}

		if (diff > largest_diff) {
			largest_diff = diff;
			memcpy(best_start, start, sizeof(best_start));
		}
		if (nodes[j] != ',')
			break;
	}

	if (largest_diff == -1) {
		snprintf(error_string, sizeof(error_string),
			 "No hostnames given here");
		goto fini;
	}

	memcpy(blockreq->start, best_start, sizeof(blockreq->start));


	if (!_full_request(blockreq, mark_bitmap, allocated_blocks))
		destroy_select_ba_request(blockreq);
fini:
	FREE_NULL_BITMAP(mark_bitmap);

#endif
	return rc;
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

	if (allocated_blocks)
		list_flush(allocated_blocks);
	else
		allocated_blocks = list_create(_destroy_allocated_block);

	memset(filename, 0, 100);
	if (len>5)
		while (i<len) {
			while (com[i-1]!=' ' && i<len) {
				i++;
			}
			while (i<len && com[i]!=' ') {
				filename[j] = com[i];
				i++;
				j++;
				if (j>100) {
					memset(error_string, 0, 255);
					sprintf(error_string,
						"filename is too long needs "
						"to be under 100 chars");
					return 0;
				}
			}
		}

	if (filename[0]=='\0') {
		sprintf(filename,"bluegene.conf");
	}

	if (!(tbl = bg_configure_config_make_tbl(filename))) {
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

	if (strcasecmp(layout_mode, "DYNAMIC")) {
		if (!s_p_get_array((void ***)&blockreq_array,
				   &count, "MPs", tbl)) {
			if (!s_p_get_array((void ***)&blockreq_array,
					   &count, "BPs", tbl)) {
				memset(error_string, 0, 255);
				sprintf(error_string,
					"WARNING: no blocks defined in "
					"bluegene.conf");
			}
		}

		for (i = 0; i < count; i++) {
			_add_bg_record(blockreq_array[i], allocated_blocks);
			/* The freeing of this will happen when
			   allocated_blocks gets freed.
			*/
			blockreq_array[i] = NULL;
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
	main_xcord += 8;
	mvwprintw(text_win, main_ycord,
		  main_xcord, "ROTATE");
	main_xcord += 7;
	mvwprintw(text_win, main_ycord,
		  main_xcord, "ELONG");
	main_xcord += 7;
#ifdef HAVE_BG
	mvwprintw(text_win, main_ycord,
		  main_xcord, "MIDPLANES");
#else
	mvwprintw(text_win, main_ycord,
		  main_xcord, "NODES");
#endif
	main_xcord += 10;

#ifdef HAVE_BGP
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
		  main_xcord, "MIDPLANELIST");
#else
	mvwprintw(text_win, main_ycord,
		  main_xcord, "NODELIST");
#endif
	main_xcord = 1;
	main_ycord++;
}

static void _print_text_command(allocated_block_t *allocated_block)
{
	char *tmp_char = NULL;

	wattron(text_win,
		COLOR_PAIR(allocated_block->color));

	mvwprintw(text_win, main_ycord,
		  main_xcord, "%c", allocated_block->letter);
	main_xcord += 4;

	tmp_char = conn_type_string_full(allocated_block->request->conn_type);
	mvwprintw(text_win, main_ycord, main_xcord, tmp_char);
	xfree(tmp_char);
	main_xcord += 8;

	if (allocated_block->request->rotate)
		mvwprintw(text_win, main_ycord,
			  main_xcord, "Y");
	else
		mvwprintw(text_win, main_ycord,
			  main_xcord, "N");
	main_xcord += 7;

	if (allocated_block->request->elongate)
		mvwprintw(text_win, main_ycord,
			  main_xcord, "Y");
	else
		mvwprintw(text_win, main_ycord,
			  main_xcord, "N");
	main_xcord += 7;

	mvwprintw(text_win, main_ycord,
		  main_xcord, "%d", allocated_block->request->size);
	main_xcord += 10;

	if (allocated_block->request->conn_type[0] >= SELECT_SMALL) {
#ifdef HAVE_BGP
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
	} else {
#ifdef HAVE_BGL
		main_xcord += 11;
#elif defined HAVE_BGP
		main_xcord += 27;
#else
		main_xcord += 22;
#endif
	}
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
	int i = 0;
	int count = 0;

	WINDOW *command_win = NULL;
        List allocated_blocks = NULL;
	ListIterator results_i;

	if (params.commandline && !params.command) {
		printf("Configure won't work with commandline mode.\n");
		printf("Please remove the -c from the commandline.\n");
		bg_configure_ba_fini();
		exit(0);
	}

	if (working_cluster_rec) {
		char *cluster_name = slurm_get_cluster_name();
		if (strcmp(working_cluster_rec->name, cluster_name)) {
			xfree(cluster_name);
			endwin();
			printf("To use the configure option you must be on the "
			       "cluster the configure is for.\nCross cluster "
			       "support doesn't exist today.\nSorry for the "
			       "inconvenince.\n");
			bg_configure_ba_fini();
			exit(0);
		}
		xfree(cluster_name);
	}

	/* make sure we don't get any noisy debug */
	ba_configure_set_ba_debug_flags(0);

	bg_configure_ba_setup_wires();

	color_count = 0;

	allocated_blocks = list_create(_destroy_allocated_block);

	if (params.commandline) {
		snprintf(com, sizeof(com), "%s", params.command);
		goto run_command;
	} else {
		text_width = text_win->_maxx;
		text_startx = text_win->_begx;
		command_win = newwin(3, text_width - 1, LINES - 4,
				     text_startx + 1);
		curs_set(1);
		echo();
	}

	while (strcmp(com, "quit")) {
		clear_window(grid_win);
		print_grid();
		clear_window(text_win);
		box(text_win, 0, 0);
		box(grid_win, 0, 0);

		if (!params.no_header)
			_print_header_command();

		if (error_string != NULL) {
			i = 0;
			while (error_string[i] != '\0') {
				if (error_string[i] == '\n') {
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
			main_xcord = 1;
			memset(error_string, 0, 255);
		}
		results_i = list_iterator_create(allocated_blocks);

		count = list_count(allocated_blocks)
			- (LINES-(main_ycord+5));

		if (count<0)
			count=0;
		i=0;
		while ((allocated_block = list_next(results_i)) != NULL) {
			if (i >= count)
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
			if (allocated_blocks)
				list_destroy(allocated_blocks);
			bg_configure_ba_fini();
			exit(0);
		}
	run_command:

		if (!strcmp(com, "quit") || !strcmp(com, "\\q")) {
			break;
		} else if (!strncasecmp(com, "layout", 6)) {
			_set_layout(com);
		} else if (!strncasecmp(com, "midplane", 8) ||
			   !strncasecmp(com, "basepartition", 13)) {
			_set_midplane_cnode_cnt(com);
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
			list_flush(allocated_blocks);
		} else {
			memset(error_string, 0, 255);
			sprintf(error_string, "Unknown command '%s'",com);
		}

		if (params.commandline) {
			bg_configure_ba_fini();
			exit(1);
		}
	}
	if (allocated_blocks)
		list_destroy(allocated_blocks);
	params.display = 0;
	noecho();

	clear_window(text_win);
	main_xcord = 1;
	main_ycord = 1;
	curs_set(0);
	print_date();
	get_job();
	return;
}

