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

#include "src/common/uid.h"
#include "src/common/node_select.h"
#include "src/smap/smap.h"

static void _print_header_job(void);
static int  _print_text_job(job_info_t * job_ptr);

extern void get_job(void)
{
	int error_code = -1, i, j, recs, count = 0;

	static job_info_msg_t *job_info_ptr = NULL, *new_job_ptr = NULL;
	job_info_t job;

	if (job_info_ptr) {
		error_code = slurm_load_jobs(job_info_ptr->last_update,
				&new_job_ptr, 0);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg(job_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_job_ptr = job_info_ptr;
		}
	} else
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr, 0);

	if (error_code) {
		if (quiet_flag != 1) {
			mvwprintw(pa_system_ptr->text_win,
				pa_system_ptr->ycord, 1,
				"slurm_load_job: %s", 
				slurm_strerror(slurm_get_errno()));
			pa_system_ptr->ycord++;
		}
	}

	if (!params.no_header)
		_print_header_job();

	if (new_job_ptr)
		recs = new_job_ptr->record_count;
	else
		recs = 0;
	
	for (i = 0; i < recs; i++) {
		job = new_job_ptr->job_array[i];
		
		if ((job.job_state == JOB_COMPLETE)
		||  (job.job_state == JOB_END))
			continue;	/* job has completed */

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
			    (int) pa_system_ptr->fill_in_value[count].
			    letter;
			wattron(pa_system_ptr->text_win,
				COLOR_PAIR(pa_system_ptr->
					   fill_in_value[count].color));
			_print_text_job(&job);
			wattroff(pa_system_ptr->text_win,
				 COLOR_PAIR(pa_system_ptr->
					    fill_in_value[count].color));
			count++;			
		}
		if(count==128)
			count=0;
	}
	
	for (i = 0; i < recs; i++) {
		job = new_job_ptr->job_array[i];
		
		if (job.job_state != JOB_PENDING)
			continue;	/* job has completed */
		
		job.nodes = "waiting...";
		job.num_procs = (int) pa_system_ptr->fill_in_value[count].
			letter;
		wattron(pa_system_ptr->text_win,
			COLOR_PAIR(pa_system_ptr->
				   fill_in_value[count].color));
		_print_text_job(&job);
		wattroff(pa_system_ptr->text_win,
			 COLOR_PAIR(pa_system_ptr->
				    fill_in_value[count].color));
		count++;			
		
		if(count==128)
			count=0;
	}

	if (params.commandline && params.iterate)
		printf("\n");

	job_info_ptr = new_job_ptr;
	return;
}

static void _print_header_job(void)
{
	if(!params.commandline) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "ID");
		pa_system_ptr->xcord += 3;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "JOBID");
		pa_system_ptr->xcord += 6;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "PARTITION");
		pa_system_ptr->xcord += 10;
#if HAVE_BGL
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "BGL_BLOCK");
		pa_system_ptr->xcord += 10;
#endif
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "USER");
		pa_system_ptr->xcord += 9;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "NAME");
		pa_system_ptr->xcord += 10;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "ST");
		pa_system_ptr->xcord += 6;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "TIME");
		pa_system_ptr->xcord += 5;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "NODES");
		pa_system_ptr->xcord += 6;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "NODELIST");
		pa_system_ptr->xcord = 1;
		pa_system_ptr->ycord++;
	} else {
		printf("ID\t");
		printf("JOBID\t");
		printf("PARTITION\t");
#if HAVE_BGL
		printf("BGL_BLOCK\t");
#endif
		printf("USER\t");
		printf("NAME\t");
		printf("ST\t");
		printf("TIME\t");
		printf("NODES\t");
		printf("NODELIST\n");
	}
}

static int _print_text_job(job_info_t * job_ptr)
{
	time_t time;
	int printed = 0;
	int tempxcord;
	int prefixlen = 0;
	int i = 0;
	int width = 0;
	char time_buf[20];

	if(!params.commandline) {
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%c", job_ptr->num_procs);
		pa_system_ptr->xcord += 3;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%d", job_ptr->job_id);
		pa_system_ptr->xcord += 6;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%.10s", job_ptr->partition);
		pa_system_ptr->xcord += 10;
#if HAVE_BGL
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%.10s", 
			  select_g_sprint_jobinfo(job_ptr->select_jobinfo, 
						  time_buf, 
						  sizeof(time_buf), 
						  SELECT_PRINT_BGL_ID));
		pa_system_ptr->xcord += 10;
#endif
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%.8s", 
			  uid_to_string((uid_t) job_ptr->user_id));
		pa_system_ptr->xcord += 9;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%.9s", job_ptr->name);
		pa_system_ptr->xcord += 10;
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%.2s",
			  job_state_string_compact(job_ptr->job_state));
		pa_system_ptr->xcord += 0;
		if(!strcasecmp(job_ptr->nodes,"waiting...")) {
			sprintf(time_buf,"0:00:00");
		} else {
			time = pa_system_ptr->now_time - job_ptr->start_time;
			snprint_time(time_buf, sizeof(time_buf), time);
		}
		width = strlen(time_buf);
		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord + (10 - width), "%s",
			  time_buf);
		pa_system_ptr->xcord += 11;

		mvwprintw(pa_system_ptr->text_win, pa_system_ptr->ycord,
			  pa_system_ptr->xcord, "%5d", job_ptr->num_nodes);
		pa_system_ptr->xcord += 6;

		tempxcord = pa_system_ptr->xcord;
		//width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;

		while (job_ptr->nodes[i] != '\0') {
			if ((printed = mvwaddch(pa_system_ptr->text_win,
						pa_system_ptr->ycord, pa_system_ptr->xcord,
						job_ptr->nodes[i])) < 0)
				return printed;
			pa_system_ptr->xcord++;
			width = pa_system_ptr->text_win->_maxx - pa_system_ptr->xcord;
			if (job_ptr->nodes[i] == '[')
				prefixlen = i + 1;
			else if (job_ptr->nodes[i] == ',' && (width - 9) <= 0) {
				pa_system_ptr->ycord++;
				pa_system_ptr->xcord = tempxcord + prefixlen;
			}
			i++;
		}
			
		pa_system_ptr->xcord = 1;
		pa_system_ptr->ycord++;
	} else {
		printf("%c\t", job_ptr->num_procs);
		printf("%d\t", job_ptr->job_id);
		printf("%s\t", job_ptr->partition);
#if HAVE_BGL
		printf("%s\t", 
		       select_g_sprint_jobinfo(job_ptr->select_jobinfo, 
					       time_buf, 
					       sizeof(time_buf), 
					       SELECT_PRINT_BGL_ID));
#endif
		printf("%s\t", uid_to_string((uid_t) job_ptr->user_id));
		printf("%s\t", job_ptr->name);
		printf("%s\t",
		       job_state_string_compact(job_ptr->job_state));
		if(!strcasecmp(job_ptr->nodes,"waiting...")) {
			sprintf(time_buf,"0:00:00");
		} else {
			time = pa_system_ptr->now_time - job_ptr->start_time;
			snprint_time(time_buf, sizeof(time_buf), time);
		}
		
		printf("%s\t", time_buf);
		printf("%d\t", job_ptr->num_nodes);
		printf("%s\n", job_ptr->nodes);
	}
	return printed;
}
