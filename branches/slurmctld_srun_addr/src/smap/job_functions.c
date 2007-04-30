/*****************************************************************************\
 *  job_functions.c - Functions related to job display mode of smap.
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include "src/common/uid.h"
#include "src/common/node_select.h"
#include "src/smap/smap.h"

static int  _get_node_cnt(job_info_t * job);
static int  _max_procs_per_node(void);
static int  _nodes_in_list(char *node_list);
static void _print_header_job(void);
static int  _print_text_job(job_info_t * job_ptr);

extern void get_job()
{
	int error_code = -1, i, j, recs;
	static int printed_jobs = 0;
	static int count = 0;
	static job_info_msg_t *job_info_ptr = NULL, *new_job_ptr = NULL;
	job_info_t job;
	uint16_t show_flags = 0;

	show_flags |= SHOW_ALL;
	if (job_info_ptr) {
		error_code = slurm_load_jobs(job_info_ptr->last_update,
				&new_job_ptr, show_flags);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg(job_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_job_ptr = job_info_ptr;
		}
	} else
		error_code = slurm_load_jobs((time_t) NULL, &new_job_ptr, 
					     show_flags);

	if (error_code) {
		if (quiet_flag != 1) {
			if(!params.commandline) {
				mvwprintw(ba_system_ptr->text_win,
					  ba_system_ptr->ycord, 1,
					  "slurm_load_job: %s", 
					  slurm_strerror(slurm_get_errno()));
				ba_system_ptr->ycord++;
			} else {
				printf("slurm_load_job: %s\n",
				       slurm_strerror(slurm_get_errno()));
			}
		}
	}

	if (!params.no_header)
		_print_header_job();

	if (new_job_ptr)
		recs = new_job_ptr->record_count;
	else
		recs = 0;
	
	if(!params.commandline)
		if((text_line_cnt+printed_jobs) > count) 
			text_line_cnt--;
	printed_jobs = 0;
	count = 0;
	for (i = 0; i < recs; i++) {
		job = new_job_ptr->job_array[i];
		
		if ((job.job_state != JOB_PENDING)
		    &&  (job.job_state != JOB_RUNNING)
		    &&  (job.job_state != JOB_SUSPENDED)
		    &&  ((job.job_state & JOB_COMPLETING) == 0))
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
			
			if(!params.commandline) {
				if((count>=text_line_cnt)
				   && (printed_jobs 
				       < (ba_system_ptr->text_win->_maxy-3))) {
					job.num_procs = (int)letters[count%62];
					wattron(ba_system_ptr->text_win,
						COLOR_PAIR(colors[count%6]));
					_print_text_job(&job);
					wattroff(ba_system_ptr->text_win,
						 COLOR_PAIR(colors[count%6]));
					printed_jobs++;
				} 
			} else {
				job.num_procs = (int)letters[count%62];
				_print_text_job(&job);
			}
			count++;			
		}
		if(count==128)
			count=0;
	}
		
	for (i = 0; i < recs; i++) {
		job = new_job_ptr->job_array[i];
		
		if (job.job_state != JOB_PENDING)
			continue;	/* job has completed */

		if(!params.commandline) {
			if((count>=text_line_cnt)
			   && (printed_jobs 
			       < (ba_system_ptr->text_win->_maxy-3))) {
				job.nodes = "waiting...";
				job.num_procs = (int) letters[count%62];
				wattron(ba_system_ptr->text_win,
					COLOR_PAIR(colors[count%6]));
				_print_text_job(&job);
				wattroff(ba_system_ptr->text_win,
					 COLOR_PAIR(colors[count%6]));
				printed_jobs++;
			} 
		} else {
			job.nodes = "waiting...";
			job.num_procs = (int) letters[count%62];
			_print_text_job(&job);
			printed_jobs++;
		}
		count++;			
		
		if(count==128)
			count=0;
	}

	if (params.commandline && params.iterate)
		printf("\n");

	if(!params.commandline)
		ba_system_ptr->ycord++;
	
	job_info_ptr = new_job_ptr;
	return;
}

static void _print_header_job(void)
{
	if(!params.commandline) {
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "ID");
		ba_system_ptr->xcord += 3;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "JOBID");
		ba_system_ptr->xcord += 6;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "PARTITION");
		ba_system_ptr->xcord += 10;
#ifdef HAVE_BG
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "BG_BLOCK");
		ba_system_ptr->xcord += 18;
#endif
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "USER");
		ba_system_ptr->xcord += 9;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "NAME");
		ba_system_ptr->xcord += 10;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "ST");
		ba_system_ptr->xcord += 8;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "TIME");
		ba_system_ptr->xcord += 5;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "NODES");
		ba_system_ptr->xcord += 6;
#ifdef HAVE_BG
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "BP_LIST");
#else
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "NODELIST");
#endif
		ba_system_ptr->xcord = 1;
		ba_system_ptr->ycord++;
	} else {
		printf("JOBID ");
		printf("PARTITION ");
#ifdef HAVE_BG
		printf("        BG_BLOCK ");
#endif
		printf("    USER ");
		printf("  NAME ");
		printf("ST ");
		printf("      TIME ");
		printf("NODES ");
#ifdef HAVE_BG
		printf("BP_LIST\n");
#else
		printf("NODELIST\n");
#endif
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
	char tmp_cnt[7];
	uint32_t node_cnt = 0;
	char *ionodes = NULL;
	
#ifdef HAVE_BG
	select_g_get_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_IONODES, 
			     &ionodes);
	select_g_get_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_NODE_CNT, 
			     &node_cnt);
	if(!strcasecmp(job_ptr->nodes,"waiting...")) 
		xfree(ionodes);
#else
	node_cnt = job_ptr->num_nodes;
#endif
	if ((node_cnt  == 0) || (node_cnt == NO_VAL))
		node_cnt = _get_node_cnt(job_ptr);
#ifdef HAVE_BG
	convert_num_unit((float)node_cnt, tmp_cnt, UNIT_NONE);
#else
	sprintf(tmp_cnt, "%d", node_cnt);
#endif
	if(!params.commandline) {
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%c", job_ptr->num_procs);
		ba_system_ptr->xcord += 3;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%d", job_ptr->job_id);
		ba_system_ptr->xcord += 6;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%.10s", job_ptr->partition);
		ba_system_ptr->xcord += 10;
#ifdef HAVE_BG
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%.16s", 
			  select_g_sprint_jobinfo(job_ptr->select_jobinfo, 
						  time_buf, 
						  sizeof(time_buf), 
						  SELECT_PRINT_BG_ID));
		ba_system_ptr->xcord += 18;
#endif
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%.8s", 
			  uid_to_string((uid_t) job_ptr->user_id));
		ba_system_ptr->xcord += 9;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%.9s", job_ptr->name);
		ba_system_ptr->xcord += 10;
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%.2s",
			  job_state_string_compact(job_ptr->job_state));
		ba_system_ptr->xcord += 2;
		if(!strcasecmp(job_ptr->nodes,"waiting...")) {
			sprintf(time_buf,"0:00:00");
		} else {
			time = ba_system_ptr->now_time - job_ptr->start_time;
			snprint_time(time_buf, sizeof(time_buf), time);
		}
		width = strlen(time_buf);
		mvwprintw(ba_system_ptr->text_win, ba_system_ptr->ycord,
			  ba_system_ptr->xcord + (10 - width), "%s",
			  time_buf);
		ba_system_ptr->xcord += 11;

		mvwprintw(ba_system_ptr->text_win, 
			  ba_system_ptr->ycord,
			  ba_system_ptr->xcord, "%5s", tmp_cnt);
		
		ba_system_ptr->xcord += 6;

		tempxcord = ba_system_ptr->xcord;
		
		i=0;
		while (job_ptr->nodes[i] != '\0') {
			if ((printed = mvwaddch(ba_system_ptr->text_win,
						ba_system_ptr->ycord, 
						ba_system_ptr->xcord,
						job_ptr->nodes[i])) < 0) {
				xfree(ionodes);
				return printed;
			}
			ba_system_ptr->xcord++;
			width = ba_system_ptr->text_win->_maxx 
				- ba_system_ptr->xcord;
			if (job_ptr->nodes[i] == '[')
				prefixlen = i + 1;
			else if (job_ptr->nodes[i] == ',' 
				 && (width - 9) <= 0) {
				ba_system_ptr->ycord++;
				ba_system_ptr->xcord = tempxcord + prefixlen;
			}
			i++;
		}
		if(ionodes) {
			mvwprintw(ba_system_ptr->text_win, 
				  ba_system_ptr->ycord,
				  ba_system_ptr->xcord, "[%s]", 
				  ionodes);
			ba_system_ptr->xcord += strlen(ionodes)+2;
			xfree(ionodes);
		}

		ba_system_ptr->xcord = 1;
		ba_system_ptr->ycord++;
	} else {
		printf("%5d ", job_ptr->job_id);
		printf("%9.9s ", job_ptr->partition);
#ifdef HAVE_BG
		printf("%16.16s ", 
		       select_g_sprint_jobinfo(job_ptr->select_jobinfo, 
					       time_buf, 
					       sizeof(time_buf), 
					       SELECT_PRINT_BG_ID));
#endif
		printf("%8.8s ", uid_to_string((uid_t) job_ptr->user_id));
		printf("%6.6s ", job_ptr->name);
		printf("%2.2s ",
		       job_state_string_compact(job_ptr->job_state));
		if(!strcasecmp(job_ptr->nodes,"waiting...")) {
			sprintf(time_buf,"0:00:00");
		} else {
			time = ba_system_ptr->now_time - job_ptr->start_time;
			snprint_time(time_buf, sizeof(time_buf), time);
		}
		
		printf("%10.10s ", time_buf);

		printf("%5s ", tmp_cnt);
		
		printf("%s", job_ptr->nodes);
		if(ionodes) {
			printf("[%s]", ionodes);
			xfree(ionodes);
		}

		printf("\n");
		
	}
	return printed;
}

static int _get_node_cnt(job_info_t * job)
{
	int node_cnt = 0, round;
	bool completing = job->job_state & JOB_COMPLETING;
	uint16_t base_job_state = job->job_state & (~JOB_COMPLETING);
	static int max_procs = 0;

	if (base_job_state == JOB_PENDING || completing) {
		if (max_procs == 0)
			max_procs = _max_procs_per_node();

		node_cnt = _nodes_in_list(job->req_nodes);
		node_cnt = MAX(node_cnt, job->num_nodes);
		round  = job->num_procs + max_procs - 1;
		round /= max_procs;      /* round up */
		node_cnt = MAX(node_cnt, round);
	} else
		node_cnt = _nodes_in_list(job->nodes);
	return node_cnt;
}

static int _nodes_in_list(char *node_list)
{
	hostset_t host_set = hostset_create(node_list);
	int count = hostset_count(host_set);
	hostset_destroy(host_set);
	return count;
}

/* Return the maximum number of processors for any node in the cluster */
static int   _max_procs_per_node(void)
{
	int error_code, max_procs = 1;
	node_info_msg_t *node_info_ptr = NULL;

	error_code = slurm_load_node ((time_t) NULL, &node_info_ptr,
				params.all_flag);
	if (error_code == SLURM_SUCCESS) {
		int i;
		node_info_t *node_ptr = node_info_ptr->node_array;
		for (i=0; i<node_info_ptr->record_count; i++) {
			max_procs = MAX(max_procs, node_ptr[i].cpus);
		}
		slurm_free_node_info_msg (node_info_ptr);
	}

	return max_procs;
}

