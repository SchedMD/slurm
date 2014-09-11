/*****************************************************************************\
 *  partition_functions.c - Functions related to partition display
 *  mode of smap.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
#include "src/common/node_select.h"
#include "src/common/parse_time.h"

#define _DEBUG 0

typedef struct {
	char *bg_block_name;
	uint16_t bg_conn_type[HIGHEST_DIMENSIONS];
	uint16_t bg_node_use;
	char *ionode_str;
	List job_list;
	int letter_num;
	List nodelist;
	char *mp_str;
	int cnode_cnt;
	bool printed;
	int size;
	char *slurm_part_name;
	uint16_t state;
} db2_block_info_t;

static List block_list = NULL;

static void _block_list_del(void *object);
static int  _in_slurm_partition(List slurm_nodes, List bg_nodes);
static int  _make_nodelist(char *nodes, List nodelist);
static void _marknodes(db2_block_info_t *block_ptr, int count);
static void _nodelist_del(void *object);
static void _print_header_part(void);
static int  _print_rest(db2_block_info_t *block_ptr);
static int  _print_text_part(partition_info_t *part_ptr,
			     db2_block_info_t *db2_info_ptr);

extern void get_slurm_part(void)
{
	int error_code, i, j, recs, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	partition_info_t part;
	uint16_t show_flags = 0;
	bitstr_t *nodes_req = NULL;
	static uint16_t last_flags = 0;

	if (params.all_flag)
		show_flags |= SHOW_ALL;
	if (part_info_ptr) {
		if (show_flags != last_flags)
			part_info_ptr->last_update = 0;
		error_code = slurm_load_partitions(part_info_ptr->last_update,
						   &new_part_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL,
						   &new_part_ptr, show_flags);
	}

	last_flags = show_flags;
	if (error_code) {
		if (quiet_flag != 1) {
			if (!params.commandline) {
				mvwprintw(text_win,
					  main_ycord, 1,
					  "slurm_load_partitions: %s",
					  slurm_strerror(slurm_get_errno()));
				main_ycord++;
			} else {
				printf("slurm_load_partitions: %s\n",
				       slurm_strerror(slurm_get_errno()));
			}
		}
		return;
	}

	if (!params.no_header)
		_print_header_part();

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;
	if (!params.commandline)
		if ((recs - text_line_cnt) < (getmaxy(text_win) - 4))
			text_line_cnt--;

	if (params.hl)
		nodes_req = get_requested_node_bitmap();
	for (i = 0; i < recs; i++) {
		part = new_part_ptr->partition_array[i];

		if (nodes_req) {
			int overlap = 0;
			bitstr_t *loc_bitmap = bit_alloc(bit_size(nodes_req));
			inx2bitstr(loc_bitmap, part.node_inx);
			overlap = bit_overlap(loc_bitmap, nodes_req);
			FREE_NULL_BITMAP(loc_bitmap);
			if (!overlap)
				continue;
		}
		j = 0;
		while (part.node_inx[j] >= 0) {
			set_grid_inx(part.node_inx[j],
				     part.node_inx[j + 1], count);
			j += 2;
		}

		if (!params.commandline) {
			if (i >= text_line_cnt) {
				part.flags = (int) letters[count%62];
				wattron(text_win,
					COLOR_PAIR(colors[count%6]));
				_print_text_part(&part, NULL);
				wattroff(text_win,
					 COLOR_PAIR(colors[count%6]));
			}
		} else {
			part.flags = (int) letters[count%62];
			_print_text_part(&part, NULL);
		}
		count++;

	}
	if (params.commandline && params.iterate)
		printf("\n");

	part_info_ptr = new_part_ptr;
	return;
}

extern void get_bg_part(void)
{
	int error_code, i, recs=0, count = 0, last_count = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	static block_info_msg_t *bg_info_ptr = NULL;
	static block_info_msg_t *new_bg_ptr = NULL;
	uint16_t show_flags = 0;
	partition_info_t part;
	db2_block_info_t *block_ptr = NULL;
	db2_block_info_t *found_block = NULL;
	ListIterator itr;
	List nodelist = NULL;
	bitstr_t *nodes_req = NULL;

	if (!(params.cluster_flags & CLUSTER_FLAG_BG))
		return;

	if (params.all_flag)
		show_flags |= SHOW_ALL;
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update,
						   &new_part_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL,
						   &new_part_ptr, show_flags);
	}

	if (error_code) {
		if (quiet_flag != 1) {
			if (!params.commandline) {
				mvwprintw(text_win,
					  main_ycord, 1,
					  "slurm_load_partitions: %s",
					  slurm_strerror(slurm_get_errno()));
				main_ycord++;
			} else {
				printf("slurm_load_partitions: %s\n",
				       slurm_strerror(slurm_get_errno()));
			}
		}
		return;
	}
	if (bg_info_ptr) {
		error_code = slurm_load_block_info(bg_info_ptr->last_update,
						   &new_bg_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_block_info_msg(bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_block_info((time_t) NULL,
						   &new_bg_ptr, show_flags);
	}
	if (error_code) {
		if (quiet_flag != 1) {
			if (!params.commandline) {
				mvwprintw(text_win,
					  main_ycord, 1,
					  "slurm_load_block: %s",
					  slurm_strerror(slurm_get_errno()));
				main_ycord++;
			} else {
				printf("slurm_load_block: %s\n",
				       slurm_strerror(slurm_get_errno()));
			}
		}
		return;
	}
	if (block_list) {
		/* clear the old list */
		list_flush(block_list);
	} else {
		block_list = list_create(_block_list_del);
	}
	if (!params.commandline)
		if ((new_bg_ptr->record_count - text_line_cnt)
		   < (getmaxy(text_win) - 4))
			text_line_cnt--;
	if (params.hl)
		nodes_req = get_requested_node_bitmap();
	for (i = 0; i < new_bg_ptr->record_count; i++) {
		if (nodes_req) {
			int overlap = 0;
			bitstr_t *loc_bitmap = bit_alloc(bit_size(nodes_req));
			inx2bitstr(loc_bitmap,
				   new_bg_ptr->block_array[i].mp_inx);
			overlap = bit_overlap(loc_bitmap, nodes_req);
			FREE_NULL_BITMAP(loc_bitmap);
			if (!overlap)
				continue;
		}
		if (params.io_bit && new_bg_ptr->block_array[i].ionode_str) {
			int overlap = 0;
			bitstr_t *loc_bitmap =
				bit_alloc(bit_size(params.io_bit));
			inx2bitstr(loc_bitmap,
				   new_bg_ptr->block_array[i].ionode_inx);
			overlap = bit_overlap(loc_bitmap, params.io_bit);
			FREE_NULL_BITMAP(loc_bitmap);
			if (!overlap)
				continue;
		}

		block_ptr = xmalloc(sizeof(db2_block_info_t));

		block_ptr->bg_block_name
			= xstrdup(new_bg_ptr->block_array[i].bg_block_id);
		block_ptr->mp_str = xstrdup(new_bg_ptr->block_array[i].mp_str);
		block_ptr->nodelist = list_create(_nodelist_del);
		_make_nodelist(block_ptr->mp_str, block_ptr->nodelist);

		block_ptr->state = new_bg_ptr->block_array[i].state;

		memcpy(block_ptr->bg_conn_type,
		       new_bg_ptr->block_array[i].conn_type,
		       sizeof(block_ptr->bg_conn_type));

		if (params.cluster_flags & CLUSTER_FLAG_BGL)
			block_ptr->bg_node_use =
				new_bg_ptr->block_array[i].node_use;

		block_ptr->ionode_str
			= xstrdup(new_bg_ptr->block_array[i].ionode_str);
		block_ptr->cnode_cnt = new_bg_ptr->block_array[i].cnode_cnt;

		itr = list_iterator_create(block_list);
		while ((found_block = (db2_block_info_t*)list_next(itr))) {
			if (!strcmp(block_ptr->mp_str, found_block->mp_str)) {
				block_ptr->letter_num =
					found_block->letter_num;
				break;
			}
		}
		list_iterator_destroy(itr);

		if (!found_block) {
			last_count++;
			_marknodes(block_ptr, last_count);
		}

		block_ptr->job_list = list_create(slurm_free_block_job_info);
		if (new_bg_ptr->block_array[i].job_list) {
			block_job_info_t *found_job;
			ListIterator itr = list_iterator_create(
				new_bg_ptr->block_array[i].job_list);
			while ((found_job = list_next(itr))) {
				block_job_info_t *block_job =
					xmalloc(sizeof(block_job_info_t));
				block_job->job_id = found_job->job_id;
				list_append(block_ptr->job_list, block_job);
			}
			list_iterator_destroy(itr);
		}

		if (block_ptr->bg_conn_type[0] >= SELECT_SMALL)
			block_ptr->size = 0;

		list_append(block_list, block_ptr);
	}

	if (!params.no_header)
		_print_header_part();

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;

	for (i = 0; i < recs; i++) {
		part = new_part_ptr->partition_array[i];

		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		nodelist = list_create(_nodelist_del);
		_make_nodelist(part.nodes, nodelist);

		if (block_list) {
			itr = list_iterator_create(block_list);
			while ((block_ptr = (db2_block_info_t*)
				list_next(itr)) != NULL) {
				if (_in_slurm_partition(nodelist,
						       block_ptr->nodelist)) {
					block_ptr->slurm_part_name
						= xstrdup(part.name);
				}
			}
			list_iterator_destroy(itr);
		}
		list_destroy(nodelist);
	}

	/* Report the BG Blocks */
	if (block_list) {
		itr = list_iterator_create(block_list);
		while ((block_ptr = (db2_block_info_t*)
			list_next(itr)) != NULL) {
			if (params.commandline)
				block_ptr->printed = 1;
			else {
				if (count>=text_line_cnt)
					block_ptr->printed = 1;
			}
			_print_rest(block_ptr);
			count++;
		}
		list_iterator_destroy(itr);
	}


	if (params.commandline && params.iterate)
		printf("\n");

	part_info_ptr = new_part_ptr;
	bg_info_ptr = new_bg_ptr;
	return;
}

static char *_set_running_job_str(List job_list, bool compact)
{
	int cnt = list_count(job_list);
	block_job_info_t *block_job;

	if (!cnt) {
		return xstrdup("-");
	} else if (cnt == 1) {
		block_job = list_peek(job_list);
		return xstrdup_printf("%u", block_job->job_id);
	} else if (compact)
		return xstrdup("multiple");
	else {
		char *tmp_char = NULL;
		ListIterator itr = list_iterator_create(job_list);
		while ((block_job = list_next(itr))) {
			if (tmp_char)
				xstrcat(tmp_char, " ");
			xstrfmtcat(tmp_char, "%u", block_job->job_id);
		}
		return tmp_char;
	}

	return NULL;
}

static void _marknodes(db2_block_info_t *block_ptr, int count)
{
	int i, j = 0;
	int start[params.cluster_dims];
	int end[params.cluster_dims];
	char *nodes = block_ptr->mp_str;

	block_ptr->letter_num = count;
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
			if (block_ptr->state != BG_BLOCK_FREE) {
				block_ptr->size += set_grid_bg(
					start, end, count, 1);
			} else {
				block_ptr->size += set_grid_bg(
					start, end, count, 0);
			}
			if (nodes[j] != ',')
				break;
		} else if (((nodes[j] >= '0') && (nodes[j] <= '9')) ||
			   ((nodes[j] >= 'A') && (nodes[j] <= 'Z'))) {
			for (i = 0; i < params.cluster_dims; i++, j++)
				start[i] = select_char2coord(nodes[j]);
			block_ptr->size += set_grid_bg(start, start, count, 1);
			if (nodes[j] != ',')
				break;
		} else
			j++;
	}
}

static void _print_header_part(void)
{
	if (!params.commandline) {
		mvwprintw(text_win, main_ycord,
			  main_xcord, "ID");
		main_xcord += 4;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "PARTITION");
		main_xcord += 10;

		if (params.display != BGPART) {
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "AVAIL");
			main_xcord += 7;
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "TIMELIMIT");
			main_xcord += 11;
		} else {
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "BG_BLOCK");
			main_xcord += 18;
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "STATE");
			main_xcord += 8;
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "JOBID");
			main_xcord += 8;
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "CONN");
			main_xcord += 8;
			if (params.cluster_flags & CLUSTER_FLAG_BGL) {
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "NODE_USE");
				main_xcord += 10;
			}
		}

		mvwprintw(text_win, main_ycord,
			  main_xcord, "NODES");
		main_xcord += 7;
		if (params.cluster_flags & CLUSTER_FLAG_BG)
			mvwprintw(text_win, main_ycord,
				  main_xcord, "MIDPLANELIST");
		else
			mvwprintw(text_win, main_ycord,
				  main_xcord, "NODELIST");
		main_xcord = 1;
		main_ycord++;
	} else {
		printf("PARTITION ");
		if (params.display != BGPART) {
			printf("AVAIL ");
			printf("TIMELIMIT ");
		} else {
			printf("        BG_BLOCK ");
			printf("STATE ");
			printf("    JOBID ");
			printf("     CONN ");
			if (params.cluster_flags & CLUSTER_FLAG_BGL)
				printf(" NODE_USE ");
		}

		printf("NODES ");
		if (params.cluster_flags & CLUSTER_FLAG_BG)
			printf("MIDPLANELIST\n");
		else
			printf("NODELIST\n");
	}
}

static int _print_text_part(partition_info_t *part_ptr,
			    db2_block_info_t *db2_info_ptr)
{
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	char *nodes = NULL, time_buf[20], *conn_str = NULL;
	char tmp_cnt[8];
	char tmp_char[8];

	if (params.cluster_flags & CLUSTER_FLAG_BG)
		convert_num_unit((float)part_ptr->total_nodes, tmp_cnt,
				 sizeof(tmp_cnt), UNIT_NONE);
	else
		snprintf(tmp_cnt, sizeof(tmp_cnt), "%u", part_ptr->total_nodes);

	if (!params.commandline) {
		mvwprintw(text_win,
			  main_ycord,
			  main_xcord, "%c",
			  part_ptr->flags);
		main_xcord += 4;

		if (part_ptr->name) {
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "%.9s",
				  part_ptr->name);
			main_xcord += 10;
			if (params.display != BGPART) {
				char *tmp_state;
				if (part_ptr->state_up == PARTITION_INACTIVE)
					tmp_state = "inact";
				else if (part_ptr->state_up == PARTITION_UP)
					tmp_state = "up";
				else if (part_ptr->state_up == PARTITION_DOWN)
					tmp_state = "down";
				else if (part_ptr->state_up == PARTITION_DRAIN)
					tmp_state = "drain";
				else
					tmp_state = "unk";
				mvwprintw(text_win, main_ycord, main_xcord,
					  tmp_state);
				main_xcord += 7;

				if (part_ptr->max_time == INFINITE)
					snprintf(time_buf, sizeof(time_buf),
						 "infinite");
				else {
					secs2time_str((part_ptr->max_time
						       * 60),
						      time_buf,
						      sizeof(time_buf));
				}

				width = strlen(time_buf);
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord + (9 - width),
					  "%s",
					  time_buf);
				main_xcord += 11;
			}
		} else
			main_xcord += 10;

		if (params.display == BGPART) {
			if (db2_info_ptr) {
				char *job_running = _set_running_job_str(
					db2_info_ptr->job_list, 1);
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "%.16s",
					  db2_info_ptr->bg_block_name);
				main_xcord += 18;
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "%.7s",
					  bg_block_state_string(
						  db2_info_ptr->state));
				main_xcord += 8;

				snprintf(tmp_char, sizeof(tmp_char),
					 "%s", job_running);
				xfree(job_running);

				mvwprintw(text_win,
					  main_ycord,
					  main_xcord,
					  "%.8s", tmp_char);
				main_xcord += 8;

				conn_str = conn_type_string_full(
					db2_info_ptr->bg_conn_type);
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "%.7s",
					  conn_str);
				xfree(conn_str);
				main_xcord += 8;

				if (params.cluster_flags & CLUSTER_FLAG_BGL) {
					mvwprintw(text_win,
						  main_ycord,
						  main_xcord, "%.9s",
						  node_use_string(
							  db2_info_ptr->
							  bg_node_use));
					main_xcord += 10;
				}
			} else {
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "?");
				main_xcord += 18;
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "?");
				main_xcord += 8;
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "?");
				main_xcord += 8;
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "?");
				main_xcord += 9;
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "?");
				main_xcord += 7;
				mvwprintw(text_win,
					  main_ycord,
					  main_xcord, "?");
				main_xcord += 10;
			}
		}
		mvwprintw(text_win,
			  main_ycord,
			  main_xcord, "%5s", tmp_cnt);

		main_xcord += 7;

		tempxcord = main_xcord;

		if (params.display == BGPART)
			nodes = part_ptr->allow_groups;
		else
			nodes = part_ptr->nodes;
		i = 0;
		prefixlen = i;
		while (nodes && nodes[i]) {
			width = getmaxx(text_win) - 1 - main_xcord;

			if (!prefixlen && (nodes[i] == '[') &&
			    (nodes[i - 1] == ','))
				prefixlen = i + 1;

			if (nodes[i - 1] == ',' && (width - 12) <= 0) {
				main_ycord++;
				main_xcord = tempxcord + prefixlen;
			} else if (main_xcord >= getmaxx(text_win)) {
				main_ycord++;
				main_xcord = tempxcord + prefixlen;
			}

			if ((printed = mvwaddch(text_win,
						main_ycord,
						main_xcord,
						nodes[i])) < 0)
				return printed;
			main_xcord++;

			i++;
		}
		if ((params.display == BGPART) && db2_info_ptr &&
		    (db2_info_ptr->ionode_str)) {
			mvwprintw(text_win,
				  main_ycord,
				  main_xcord, "[%s]",
				  db2_info_ptr->ionode_str);
		}

		main_xcord = 1;
		main_ycord++;
	} else {
		if (part_ptr->name) {
			printf("%9.9s ", part_ptr->name);

			if (params.display != BGPART) {
				if (part_ptr->state_up == PARTITION_INACTIVE)
					printf(" inact ");
				else if (part_ptr->state_up == PARTITION_UP)
					printf("   up ");
				else if (part_ptr->state_up == PARTITION_DOWN)
					printf(" down ");
				else if (part_ptr->state_up == PARTITION_DRAIN)
					printf(" drain ");
				else
					printf(" unk ");

				if (part_ptr->max_time == INFINITE)
					snprintf(time_buf, sizeof(time_buf),
						 "infinite");
				else {
					secs2time_str((part_ptr->max_time
						       * 60),
						      time_buf,
						      sizeof(time_buf));
				}

				printf("%9.9s ", time_buf);
			}
		}

		if (params.display == BGPART) {
			if (db2_info_ptr) {
				char *job_running = _set_running_job_str(
					db2_info_ptr->job_list, 1);
				printf("%16.16s ",
				       db2_info_ptr->bg_block_name);
				printf("%-7.7s ",
				       bg_block_state_string(
					       db2_info_ptr->state));

				printf("%8.8s ", job_running);
				xfree(job_running);

				conn_str = conn_type_string_full(
					db2_info_ptr->bg_conn_type);
				printf("%8.8s ", conn_str);
				xfree(conn_str);

				if (params.cluster_flags & CLUSTER_FLAG_BGL)
					printf("%9.9s ", node_use_string(
						       db2_info_ptr->
						       bg_node_use));
			}
		}

		printf("%5s ", tmp_cnt);

		if (params.display == BGPART)
			nodes = part_ptr->allow_groups;
		else
			nodes = part_ptr->nodes;

		if ((params.display == BGPART) && db2_info_ptr &&
		    (db2_info_ptr->ionode_str)) {
			printf("%s[%s]\n", nodes, db2_info_ptr->ionode_str);
		} else
			printf("%s\n",nodes);
	}
	return printed;
}

static void _block_list_del(void *object)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *)object;

	if (block_ptr) {
		xfree(block_ptr->bg_block_name);
		xfree(block_ptr->slurm_part_name);
		xfree(block_ptr->mp_str);
		xfree(block_ptr->ionode_str);
		if (block_ptr->nodelist)
			list_destroy(block_ptr->nodelist);
		if (block_ptr->job_list) {
			list_destroy(block_ptr->job_list);
			block_ptr->job_list = NULL;
		}
		xfree(block_ptr);

	}
}

static void _nodelist_del(void *object)
{
	int *coord = (int *)object;
	xfree(coord);
	return;
}

static int _in_slurm_partition(List slurm_nodes, List bg_nodes)
{
	ListIterator slurm_itr;
	ListIterator bg_itr;
	int *coord = NULL;
	int *slurm_coord = NULL;
	int found = 0, i;

	bg_itr = list_iterator_create(bg_nodes);
	slurm_itr = list_iterator_create(slurm_nodes);
	while ((coord = list_next(bg_itr)) != NULL) {
		list_iterator_reset(slurm_itr);
		found = 0;
		while ((slurm_coord = list_next(slurm_itr)) != NULL) {
			for (i = 0; i < params.cluster_dims; i++) {
				if (coord[i] != slurm_coord[i])
					break;
			}
			if (i >= params.cluster_dims) {
				found = 1;
				break;
			}
		}
		if (!found)
			break;
	}
	list_iterator_destroy(slurm_itr);
	list_iterator_destroy(bg_itr);

	if (found)
		return 1;
	else
		return 0;

}

static int _print_rest(db2_block_info_t *block_ptr)
{
	partition_info_t part;

	if (block_ptr->cnode_cnt == 0)
		block_ptr->cnode_cnt = block_ptr->size;
	part.total_nodes = block_ptr->cnode_cnt;
	if (block_ptr->slurm_part_name)
		part.name = block_ptr->slurm_part_name;
	else
		part.name = "no part";

	if (!block_ptr->printed)
		return SLURM_SUCCESS;
	part.allow_groups = block_ptr->mp_str;
	part.flags = (int) letters[block_ptr->letter_num%62];
	if (!params.commandline) {
		wattron(text_win,
			COLOR_PAIR(colors[block_ptr->letter_num%6]));
		_print_text_part(&part, block_ptr);
		wattroff(text_win,
			 COLOR_PAIR(colors[block_ptr->letter_num%6]));
	} else {
		_print_text_part(&part, block_ptr);
	}
	return SLURM_SUCCESS;
}

static int *_build_coord(int *current)
{
	int i;
	int *coord = NULL;

	coord = xmalloc(sizeof(int) * params.cluster_dims);
	for (i = 0; i < params.cluster_dims; i++)
		coord[i] = current[i];
	return coord;
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

static void _addto_nodelist(List nodelist, int *start, int *end)
{
	int *coord = NULL;
	int i;

	coord = xmalloc(sizeof(int) * params.cluster_dims);
	for (i = 0; i < params.cluster_dims; i++) {
		xassert(start[i] >= 0);
		coord[i] = start[i];
		if (end[i] < dim_size[i])
			continue;
		fatal("It appears the slurm.conf file has changed since "
		      "the last restart.\nThings are in an incompatible "
		      "state, please restart the slurmctld.");
	}

	do {
		list_append(nodelist, _build_coord(coord));
	} while (_incr_coord(start, end, coord));

	xfree(coord);
}

static int _make_nodelist(char *nodes, List nodelist)
{
	int i, j = 0;
	int start[params.cluster_dims];
	int end[params.cluster_dims];

	if (!nodelist)
		nodelist = list_create(_nodelist_del);

	memset(start, params.cluster_dims, sizeof(int));
	memset(end,   params.cluster_dims, sizeof(int));

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
			_addto_nodelist(nodelist, start, end);
			if (nodes[j] != ',')
				break;
			j--;
		} else if (((nodes[j] >= '0') && (nodes[j] <= '9')) ||
			   ((nodes[j] >= 'A') && (nodes[j] <= 'Z'))) {
			for (i = 0; i < params.cluster_dims; i++, j++)
				start[i] = select_char2coord(nodes[j]);
			_addto_nodelist(nodelist, start, start);
			if (nodes[j] != ',')
				break;
			j--;
		}
		j++;
	}

	return 1;
}
