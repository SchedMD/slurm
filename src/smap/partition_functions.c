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
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/smap/smap.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"

static void _print_header_part(void);
static int  _print_text_part(partition_info_t *part_ptr);

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
				_print_text_part(&part);
				wattroff(text_win,
					 COLOR_PAIR(colors[count%6]));
			}
		} else {
			part.flags = (int) letters[count%62];
			_print_text_part(&part);
		}
		count++;

	}
	if (params.commandline && params.iterate)
		printf("\n");

	part_info_ptr = new_part_ptr;
	return;
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

		mvwprintw(text_win, main_ycord,
			  main_xcord, "AVAIL");
		main_xcord += 7;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "TIMELIMIT");
		main_xcord += 11;

		mvwprintw(text_win, main_ycord,
			  main_xcord, "NODES");
		main_xcord += 7;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "NODELIST");
		main_xcord = 1;
		main_ycord++;
	} else {
		printf("PARTITION ");
		printf("AVAIL ");
		printf("TIMELIMIT ");
		printf("NODES ");
		printf("NODELIST\n");
	}
}

static int _print_text_part(partition_info_t *part_ptr)
{
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	char *nodes = NULL, time_buf[20];
	char tmp_cnt[8];

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
			else
				secs2time_str((part_ptr->max_time * 60),
					      time_buf, sizeof(time_buf));

			width = strlen(time_buf);
			mvwprintw(text_win, main_ycord,
				  main_xcord + (9 - width),
				  "%s", time_buf);
			main_xcord += 11;
		} else
			main_xcord += 10;

		mvwprintw(text_win,
			  main_ycord,
			  main_xcord, "%5s", tmp_cnt);

		main_xcord += 7;

		tempxcord = main_xcord;

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

		main_xcord = 1;
		main_ycord++;
	} else {
		if (part_ptr->name) {
			printf("%9.9s ", part_ptr->name);

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
			else
				secs2time_str((part_ptr->max_time * 60),
					      time_buf, sizeof(time_buf));

			printf("%9.9s ", time_buf);
		}

		printf("%5s ", tmp_cnt);

		printf("%s\n", part_ptr->nodes);
	}

	return printed;
}
