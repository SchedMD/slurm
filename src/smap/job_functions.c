/*****************************************************************************\
 *  job_functions.c - Functions related to job display mode of smap.
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

#include "src/smap/smap.h"

void print_header_job(void);
int print_text_job(job_info_t * job_ptr);

void get_job()
{
	int error_code = -1, i, j, count = 0;

	static job_info_msg_t *job_info_ptr = NULL, *new_job_ptr;
	job_info_t job;

	if (job_info_ptr) {
		error_code =
		    slurm_load_jobs(job_info_ptr->last_update,
				    &new_job_ptr, 0);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg(job_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_job_ptr = job_info_ptr;
		}
	} else
		error_code =
		    slurm_load_jobs((time_t) NULL, &new_job_ptr, 0);

	if (error_code)
		if (quiet_flag != 1) {
			wclear(smap_info_ptr->text_win);
			smap_info_ptr->ycord =
			    smap_info_ptr->text_win->_maxy / 2;
			smap_info_ptr->xcord =
			    smap_info_ptr->text_win->_maxx;
			mvwprintw(smap_info_ptr->text_win,
				  smap_info_ptr->ycord, 1,
				  "slurm_load_job");

			return;
		}

	if (new_job_ptr->record_count && !params.no_header)
		print_header_job();
	for (i = 0; i < new_job_ptr->record_count; i++) {
		job = new_job_ptr->job_array[i];
		if (job.node_inx[0] != -1) {
			job.num_nodes = 0;
			j = 0;
			while (job.node_inx[j] >= 0) {
				job.num_nodes +=
				    (job.node_inx[j + 1] + 1) -
				    job.node_inx[j];
				set_grid(job.node_inx[j],
					 job.node_inx[j + 1], count);
				j += 2;
			}
			job.num_procs =
			    (int) smap_info_ptr->fill_in_value[count].
			    letter;
			wattron(smap_info_ptr->text_win,
				COLOR_PAIR(smap_info_ptr->
					   fill_in_value[count].color));
			print_text_job(&job);
			wattroff(smap_info_ptr->text_win,
				 COLOR_PAIR(smap_info_ptr->
					    fill_in_value[count].color));
			count++;
		}
	}
	job_info_ptr = new_job_ptr;
	return;
}

void print_header_job(void)
{
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "ID");
	smap_info_ptr->xcord += 3;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, " JOBID");
	smap_info_ptr->xcord += 7;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "PARTITION");
	smap_info_ptr->xcord += 10;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "USER");
	smap_info_ptr->xcord += 9;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "NAME");
	smap_info_ptr->xcord += 10;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "ST");
	smap_info_ptr->xcord += 3;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "      TIME");
	smap_info_ptr->xcord += 11;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "NODES");
	smap_info_ptr->xcord += 6;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "NODELIST");
	smap_info_ptr->xcord = 1;
	smap_info_ptr->ycord++;

}

int print_text_job(job_info_t * job_ptr)
{
	time_t time;
	int printed = 0;
	int tempxcord;
	int prefixlen;
	int i = 0;
	int width = 0;
	struct passwd *user = NULL;
	char time_buf[20];

	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "%c", job_ptr->num_procs);
	smap_info_ptr->xcord += 3;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "%6d", job_ptr->job_id);
	smap_info_ptr->xcord += 7;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "%.10s", job_ptr->partition);
	smap_info_ptr->xcord += 10;
	if ((user = getpwuid((uid_t) job_ptr->user_id)))
		mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
			smap_info_ptr->xcord, "%.8s", user->pw_name);
	else
		mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
			smap_info_ptr->xcord, "%8d", (int) job_ptr->user_id);
	smap_info_ptr->xcord += 9;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "%.9s", job_ptr->name);
	smap_info_ptr->xcord += 10;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "%.2s",
		  job_state_string_compact(job_ptr->job_state));
	smap_info_ptr->xcord += 3;
	time = smap_info_ptr->now_time - job_ptr->start_time;

	time = smap_info_ptr->now_time - job_ptr->start_time;
	snprint_time(time_buf, sizeof(time_buf), time);
	width = strlen(time_buf);
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		smap_info_ptr->xcord + (10 - width), "%s",
		time_buf);
	smap_info_ptr->xcord += 11;

	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "%5d", job_ptr->num_nodes);
        smap_info_ptr->xcord += 6;

	tempxcord = smap_info_ptr->xcord;
	//width = smap_info_ptr->text_win->_maxx - smap_info_ptr->xcord;

	while (job_ptr->nodes[i] != '\0') {
		if ((printed = mvwaddch(smap_info_ptr->text_win,
                                        smap_info_ptr->ycord, smap_info_ptr->xcord,
                                        job_ptr->nodes[i])) < 0)
			return printed;
		smap_info_ptr->xcord++;
		width = smap_info_ptr->text_win->_maxx - smap_info_ptr->xcord;
		if (job_ptr->nodes[i] == '[')
			prefixlen = i + 1;
		else if (job_ptr->nodes[i] == ',' && (width - 9) <= 0) {
			smap_info_ptr->ycord++;
			smap_info_ptr->xcord = tempxcord + prefixlen;
		}
		i++;
	}

	smap_info_ptr->xcord = 1;
	smap_info_ptr->ycord++;
	return printed;
}
