/*****************************************************************************\
 *  reservation_functions.c - Functions related to reservation display mode 
 *  of smap.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "src/common/parse_time.h"
#include "src/smap/smap.h"

static void _print_header_resv(void);
static void _print_text_resv(reserve_info_t * resv_ptr);

extern void get_reservation(void)
{
	int error_code = -1, active, i, recs;
	reserve_info_t resv;
	time_t now = time(NULL);
	static int printed_resv = 0;
	static int count = 0;
	static reserve_info_msg_t *resv_info_ptr = NULL, *new_resv_ptr = NULL;

	if (resv_info_ptr) {
		error_code = slurm_load_reservations(resv_info_ptr->last_update,
						     &new_resv_ptr);
		if (error_code == SLURM_SUCCESS)
			 slurm_free_reservation_info_msg(resv_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_resv_ptr = resv_info_ptr;
		}
	} else
		error_code = slurm_load_reservations((time_t) NULL,
						     &new_resv_ptr);

	if (error_code) {
		if (quiet_flag != 1) {
			if(!params.commandline) {
				mvwprintw(text_win,
					  main_ycord, 1,
					  "slurm_load_reservations: %s", 
					  slurm_strerror(slurm_get_errno()));
				main_ycord++;
			} else {
				printf("slurm_load_reservations: %s\n",
				       slurm_strerror(slurm_get_errno()));
			}
		}
	}

	if (!params.no_header)
		_print_header_resv();

	if (new_resv_ptr)
		recs = new_resv_ptr->record_count;
	else
		recs = 0;

	if (!params.commandline) {
		if((text_line_cnt+printed_resv) > count) 
			text_line_cnt--;
	}
	printed_resv = 0;
	count = 0;
	for (i = 0; i < recs; i++) {
		resv = new_resv_ptr->reservation_array[i];
		if ((resv.start_time <= now) && (resv.end_time >= now))
			active = 1;
		else
			active = 0;

		if (active && (resv.node_inx[0] != -1)) {
#ifdef HAVE_SUN_CONST
			set_grid_name(resv.node_list, count);
#else
			int j = 0;
			resv.node_cnt = 0;
			while (resv.node_inx[j] >= 0) {
				resv.node_cnt +=
				    (resv.node_inx[j + 1] + 1) -
				    resv.node_inx[j];
				set_grid_inx(resv.node_inx[j],
					     resv.node_inx[j + 1], count);
				j += 2;
			}
#endif
		}

		if (resv.node_inx[0] != -1) {
			if (!params.commandline) {
				if ((count >= text_line_cnt) &&
				    (printed_resv  < (text_win->_maxy-3))) {
					resv.flags = (int)letters[count%62];
					wattron(text_win,
						COLOR_PAIR(colors[count%6]));
					_print_text_resv(&resv);
					wattroff(text_win,
						 COLOR_PAIR(colors[count%6]));
					printed_resv++;
				} 
			} else {
				/* put the letter code into "flags" field */
				resv.flags = (int)letters[count%62];
				_print_text_resv(&resv);
			}
			count++;			
		}
		if (count==128)
			count=0;
	}

	if (params.commandline && params.iterate)
		printf("\n");

	if (!params.commandline)
		main_ycord++;
	
	resv_info_ptr = new_resv_ptr;
	return;
}

static void _print_header_resv(void)
{
	if (!params.commandline) {
		mvwprintw(text_win, main_ycord,
			  main_xcord, "ID ");
		main_xcord += 3;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%12.12s  ", "NAME");
		main_xcord += 14;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%19.19s  ", "START_TIME");
		main_xcord += 21;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%19.19s  ", "END_TIME");
		main_xcord += 21;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%5.5s  ", "NODES");
		main_xcord += 7;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%30.30s  ", 
			  "ACCESS_CONTROL(Accounts,Users)");
		main_xcord += 32;
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%s",    "NODELIST");
		main_xcord = 1;
		main_ycord++;
	} else {
		printf("%12.12s  ", "NAME");
		printf("%19.19s  ", "START_TIME");
		printf("%19.19s  ", "END_TIME");
		printf("%5.5s  ",   "NODES");
		printf("%30.30s  ", "ACCESS_CONTROL(Accounts,Users)");
		printf("%s",        "NODELIST\n");
	}
}

static void _print_text_resv(reserve_info_t * resv_ptr)
{
	char start_str[32], end_str[32], acl[32];

	slurm_make_time_str(&resv_ptr->start_time, start_str, 
			    sizeof(start_str));
	slurm_make_time_str(&resv_ptr->end_time, end_str, 
			    sizeof(end_str));

	if (resv_ptr->accounts && resv_ptr->accounts[0] &&
	    resv_ptr->users && resv_ptr->users[0])
		snprintf(acl, sizeof(acl), "A:%s,U:%s", resv_ptr->accounts,
			 resv_ptr->users);
	else if (resv_ptr->accounts && resv_ptr->accounts[0])
		snprintf(acl, sizeof(acl), "A:%s", resv_ptr->accounts);
	else if (resv_ptr->users && resv_ptr->users[0])
		snprintf(acl, sizeof(acl), "U:%s", resv_ptr->users);
	else
		snprintf(acl, sizeof(acl), "NONE");


	if (!params.commandline) {
		mvwprintw(text_win, main_ycord,
			  main_xcord, "%c", resv_ptr->flags);
		main_xcord += 3;

		mvwprintw(text_win, main_ycord,
			  main_xcord, "%12.12s  ", resv_ptr->name);
		main_xcord += 14;

		mvwprintw(text_win, main_ycord,
			  main_xcord, "%19.19s  ", start_str);
		main_xcord += 21;

		mvwprintw(text_win, main_ycord,
			  main_xcord, "%19.19s  ", end_str);
		main_xcord += 21;

		mvwprintw(text_win, main_ycord,
			  main_xcord, "%5.d  ", resv_ptr->node_cnt);
		main_xcord += 7;

		mvwprintw(text_win, main_ycord,
			  main_xcord, "%30.30s  ", acl);
		main_xcord += 33;

		mvwprintw(text_win, main_ycord,
			  main_xcord, "%s", resv_ptr->node_list);

		main_xcord = 1;
		main_ycord++;
	} else {
		printf("%12.12s  ", resv_ptr->name);
		printf("%19.19s  ", start_str);
		printf("%19.19s  ", end_str);
		printf("%5.d  ",    resv_ptr->node_cnt);
		printf("%30.30s  ", acl);
		printf("%s ",       resv_ptr->node_list);

		printf("\n");
		
	}
}


