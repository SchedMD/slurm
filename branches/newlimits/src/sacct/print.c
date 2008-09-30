/*****************************************************************************\
 *  print.c - print functions for sacct
 *
 *  $Id: print.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  LLNL-CODE-402394.
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

#include "sacct.h"
#include "src/common/parse_time.h"
#include "slurm.h"
#define FORMAT_STRING_SIZE 34

void _elapsed_time(long secs, long usecs, char *str);

void _elapsed_time(long secs, long usecs, char *str)
{
	long	days, hours, minutes, seconds;
	long    subsec = 0;
	
	if(secs < 0) {
		snprintf(str, FORMAT_STRING_SIZE, "'N/A'");
		return;
	}
	
	while (usecs >= 1E6) {
		secs++;
		usecs -= 1E6;
	}
	if(usecs > 0) {
		/* give me 3 significant digits to tack onto the sec */
		subsec = (usecs/1000);
	}
	seconds =  secs % 60;
	minutes = (secs / 60)   % 60;
	hours   = (secs / 3600) % 24;
	days    =  secs / 86400;

	if (days) 
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld-%2.2ld:%2.2ld:%2.2ld",
		         days, hours, minutes, seconds);
	else if (hours)
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld:%2.2ld",
		         hours, minutes, seconds);
	else
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld.%3.3ld",
		         minutes, seconds, subsec);
}

void print_fields(type_t type, void *object)
{
	int f, pf;
	for (f=0; f<nprintfields; f++) {
		pf = printfields[f];
		if (f)
			printf(" ");
		(fields[pf].print_routine)(type, object);
	}
	printf("\n");
}

/* Field-specific print routines */

void print_cpu(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char str[FORMAT_STRING_SIZE];
	
	switch(type) {
	case HEADLINE:
		printf("%-15s", "Cpu");
		break;
	case UNDERSCORE:
		printf("%-15s", "---------------");
		break;
	case JOB:
		_elapsed_time(job->tot_cpu_sec, job->tot_cpu_usec, str);
		printf("%-15s", str);
		break;
	case JOBSTEP:
		_elapsed_time(step->tot_cpu_sec, step->tot_cpu_usec, str);
		printf("%-15s", str);
		break;
	default:
		printf("%-15s", "n/a");
		break;
	} 
}

void print_elapsed(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char str[FORMAT_STRING_SIZE];

	switch(type) {
	case HEADLINE:
		printf("%-15s", "Elapsed");
		break;
	case UNDERSCORE:
		printf("%-15s", "---------------");
		break;
	case JOB:
		_elapsed_time(job->elapsed, 0, str);
		printf("%-15s", str);
		break;
	case JOBSTEP:
		_elapsed_time(step->elapsed, 0, str);
		printf("%-15s", str);
		break;
	default:
		printf("%-15s", "n/a");
		break;
	} 
}

void print_exitcode(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char tmp[9];
	uint16_t term_sig = 0;

	switch(type) {
	case HEADLINE:
		printf("%-8s", "ExitCode");
		break;
	case UNDERSCORE:
		printf("%-8s", "--------");
		break;
	case JOB:
		if (WIFSIGNALED(job->exitcode))
			term_sig = WTERMSIG(job->exitcode);
	
		snprintf(tmp, sizeof(tmp), "%u:%u",
			 WEXITSTATUS(job->exitcode), term_sig);
		printf("%-8s", tmp);
		break;
	case JOBSTEP:
		if (WIFSIGNALED(step->exitcode))
			term_sig = WTERMSIG(step->exitcode);
	
		snprintf(tmp, sizeof(tmp), "%u:%u",
			 WEXITSTATUS(step->exitcode), term_sig);
		printf("%-8s", tmp);
		break;
	default:
		printf("%-8s", "n/a");
		break;
	} 
}

void print_gid(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	int32_t gid = -1;

	switch(type) {
	case HEADLINE:
		printf("%-5s", "Gid");
		break;
	case UNDERSCORE:
		printf("%-5s", "-----");
		break;
	case JOB:
		gid = job->gid;
		break;
	case JOBCOMP:
		printf("%-5u", jobcomp->gid);
		break;
	case JOBSTEP:
		printf("%-5s", " ");
		break;
	default:
		printf("%-5s", "n/a");
		break;
	} 

	if(gid != -1) 
		printf("%-5d", gid);
}

void print_group(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	int gid = -1;
	char	*tmp="(unknown)";
	struct	group *gr = NULL;
			
	switch(type) {
	case HEADLINE:
		printf("%-9s", "Group");
		break;
	case UNDERSCORE:
		printf("%-9s", "---------");
		break;
	case JOB:
		gid = job->gid;
		break;
	case JOBCOMP:
		printf("%-9s", jobcomp->gid_name);
		break;
	case JOBSTEP:
		printf("%-9s", " ");
		break;
	default:
		printf("%-9s", "n/a");
		break;
	}
	if(gid != -1) {
		if ((gr=getgrgid(gid)))
			tmp=gr->gr_name;
		printf("%-9s", tmp);
	} 
}

void print_job(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-8s", "Job");
		break;
	case UNDERSCORE:
		printf("%-8s", "--------");
		break;
	case JOB:
		printf("%-8u", job->jobid);
		break;
	case JOBSTEP:
		printf("%-8s", " ");
		break;
	default:
		printf("%-8s", "n/a");
		break;
	} 
}

void print_name(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-18s", "Jobname");
		break;
	case UNDERSCORE:
		printf("%-18s", "------------------");
		break;
	case JOB:
		if(!job->jobname)
			printf("%-18s", "unknown");			     
		else if(strlen(job->jobname)<19)
			printf("%-18s", job->jobname);
		else
			printf("%-15.15s...", job->jobname);
			
		break;
	case JOBCOMP:
		if(!jobcomp->jobname)
			printf("%-18s", "unknown");			     
		else if(strlen(jobcomp->jobname)<19)
			printf("%-18s", jobcomp->jobname);
		else
			printf("%-15.15s...", jobcomp->jobname);
			
		break;
	case JOBSTEP:
		if(!step->stepname)
			printf("%-18s", "unknown");			     
		else if(strlen(step->stepname)<19)
			printf("%-18s", step->stepname);
		else
			printf("%-15.15s...", step->stepname);
		break;
	default:
		printf("%-18s", "n/a");
		break;
	} 
}

void print_jobid(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char outbuf[10];

	switch(type) {
	case HEADLINE:
		printf("%-10s", "JobID");
		break;
	case UNDERSCORE:
		printf("%-10s", "----------");
		break;
	case JOB:
		printf("%-10u", job->jobid);
		break;
	case JOBCOMP:
		printf("%-10u", jobcomp->jobid);
		break;
	case JOBSTEP:
		snprintf(outbuf, sizeof(outbuf), "%u.%u",
			 step->jobid,
			 step->stepid);
		printf("%-10s", outbuf);
		break;
	default:
		printf("%-10s", "n/a");
		break;
	} 

}

void print_ncpus(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-7s", "Ncpus");
		break;
	case UNDERSCORE:
		printf("%-7s", "-------");
		break;
	case JOB:
		printf("%-7u", job->alloc_cpus);
		break;
	case JOBSTEP:
		printf("%-7u", step->ncpus);
		break;
	default:
		printf("%-7s", "n/a");
		break;
	} 
}

void print_nodes(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	
	switch(type) {
	case HEADLINE:
		printf("%-30s", "Nodes");
		break;
	case UNDERSCORE:
		printf("%-30s", "------------------------------");
		break;
	case JOB:
		printf("%-30s", job->nodes);
		break;
	case JOBCOMP:
		printf("%-30s", jobcomp->nodelist);
		break;
	case JOBSTEP:
		printf("%-30s", step->nodes);
		break;
	default:
		printf("%-30s", "n/a");
		break;
	} 
}

void print_nnodes(type_t type, void *object)
{ 
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	char temp[FORMAT_STRING_SIZE];

	switch(type) {
	case HEADLINE:
		printf("%-8s", "Node Cnt");
		break;
	case UNDERSCORE:
		printf("%-8s", "--------");
		break;
	case JOBCOMP:
		convert_num_unit((float)jobcomp->node_cnt, temp, 
				 sizeof(temp), UNIT_NONE);
		printf("%-8s", temp);
		break;
	default:
		printf("%-8s", "n/a");
		break;
	} 
}

void print_ntasks(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-7s", "Ntasks");
		break;
	case UNDERSCORE:
		printf("%-7s", "-------");
		break;
	case JOB:
		printf("%-7u", job->alloc_cpus);
		break;
	case JOBSTEP:
		printf("%-7u", step->ncpus);
		break;
	default:
		printf("%-7s", "n/a");
		break;
	} 
}

void print_partition(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-10s", "Partition");
		break;
	case UNDERSCORE:
		printf("%-10s", "----------");
		break;
	case JOB:
		if(!job->partition)
			printf("%-10s", "unknown");			     
		else if(strlen(job->partition)<11)
			printf("%-10s", job->partition);
		else
			printf("%-7.7s...", job->partition);
		
		break;
	case JOBCOMP:
		if(!jobcomp->partition)
			printf("%-10s", "unknown");			     
		else if(strlen(jobcomp->partition)<11)
			printf("%-10s", jobcomp->partition);
		else
			printf("%-7.7s...", jobcomp->partition);
		
		break;
	case JOBSTEP:
		printf("%-10s", " ");
		break;
	default:
		printf("%-10s", "n/a");
		break;
	} 
}

void print_blockid(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-16s", "BlockID");
		break;
	case UNDERSCORE:
		printf("%-16s", "----------------");
		break;
	case JOB:
		if(!job->blockid)
			printf("%-16s", "unknown");			     
		else if(strlen(job->blockid)<17)
			printf("%-16s", job->blockid);
		else
			printf("%-13.13s...", job->blockid);
		
		break;
	case JOBCOMP:
		if(!jobcomp->blockid)
			printf("%-16s", "unknown");			     
		else if(strlen(jobcomp->blockid)<17)
			printf("%-16s", jobcomp->blockid);
		else
			printf("%-13.13s...", jobcomp->blockid);
		
		break;
	case JOBSTEP:
		printf("%-16s", " ");
		break;
	default:
		printf("%-16s", "n/a");
		break;
	} 
}

void print_pages(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char outbuf[FORMAT_STRING_SIZE];
	char buf1[FORMAT_STRING_SIZE];
	char buf2[FORMAT_STRING_SIZE];
	char buf3[FORMAT_STRING_SIZE];
	sacct_t sacct;
	char *nodes = NULL;
	uint32_t pos;

	switch(type) {
	case HEADLINE:
		printf("%-34s", "MaxPages/Node:Task - Ave");
		break;
	case UNDERSCORE:
		printf("%-34s", "----------------------------------");
		break;
	case JOB:
		sacct = job->sacct;
		nodes = job->nodes;
		pos = sacct.min_cpu_id.nodeid;				 
		convert_num_unit((float)sacct.max_pages, 
				 buf1, sizeof(buf1), UNIT_NONE);

		if(job->track_steps)
			snprintf(outbuf, FORMAT_STRING_SIZE, "%s/- - -", buf1);
		else {
			convert_num_unit((float)sacct.ave_pages,
					 buf2, sizeof(buf2), UNIT_NONE);
			find_hostname(pos, nodes, buf3);
			snprintf(outbuf, FORMAT_STRING_SIZE, "%s/%s:%u - %s", 
				 buf1,
				 buf3,
				 sacct.max_pages_id.taskid, 
				 buf2);
		}
		printf("%-34s", outbuf);
		break;
	case JOBSTEP:
		sacct = step->sacct;
		nodes = step->nodes;
		pos = sacct.min_cpu_id.nodeid;				 
		convert_num_unit((float)sacct.max_pages, buf1, sizeof(buf1),
				 UNIT_NONE);
		convert_num_unit((float)sacct.ave_pages, buf2, sizeof(buf2),
				 UNIT_NONE);
		find_hostname(pos, nodes, buf3);
		snprintf(outbuf, FORMAT_STRING_SIZE, "%s/%s:%u - %s", 
			 buf1,
			 buf3,
			 sacct.max_pages_id.taskid, 
			 buf2);
		printf("%-34s", outbuf);
		break;
	default:
		printf("%-34s", "n/a");
		break;
	} 
}

void print_rss(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char outbuf[FORMAT_STRING_SIZE];
	char buf1[FORMAT_STRING_SIZE];
	char buf2[FORMAT_STRING_SIZE];
	char buf3[FORMAT_STRING_SIZE];
	sacct_t sacct;
	char *nodes = NULL;
	uint32_t pos;

	switch(type) {
	case HEADLINE:
		printf("%-34s", "MaxRSS/Node:Task - Ave");
		break;
	case UNDERSCORE:
		printf("%-34s", "----------------------------------");
		break;
	case JOB:
		sacct = job->sacct;
		nodes = job->nodes;
		pos = sacct.min_cpu_id.nodeid;				 
		convert_num_unit((float)sacct.max_rss, buf1, sizeof(buf1),
				 UNIT_KILO);

		if(job->track_steps)
			snprintf(outbuf, FORMAT_STRING_SIZE, "%s/- - -", buf1);
		else {
			convert_num_unit((float)sacct.ave_rss, 
					 buf2, sizeof(buf2), UNIT_KILO);
			find_hostname(pos, nodes, buf3);
			snprintf(outbuf, FORMAT_STRING_SIZE, "%s/%s:%u - %s", 
				 buf1,
				 buf3, 
				 sacct.max_rss_id.taskid, 
				 buf2);
		}
		printf("%-34s", outbuf);
		break;
	case JOBSTEP:
		sacct = step->sacct;
		nodes = step->nodes;
		pos = sacct.min_cpu_id.nodeid;				 
		convert_num_unit((float)sacct.max_rss, buf1, sizeof(buf1),
				 UNIT_KILO);
		convert_num_unit((float)sacct.ave_rss, buf2, sizeof(buf2),
				 UNIT_KILO);
		find_hostname(pos, nodes, buf3);
		snprintf(outbuf, FORMAT_STRING_SIZE, "%s/%s:%u - %s", 
			 buf1,
			 buf3, 
			 sacct.max_rss_id.taskid, 
			 buf2);
		printf("%-34s", outbuf);
		break;
	default:
		printf("%-34s", "n/a");
		break;
	} 
}

void print_state(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-20s", "State");
		break;
	case UNDERSCORE:
		printf("%-20s", "--------------------");
		break;
	case JOB:
		if ( job->state == JOB_CANCELLED) {
			printf ("%-10s by %6d",
				job_state_string(job->state), job->requid);
		}
		else {
			printf("%-20s", job_state_string(job->state));
		}
		break;
	case JOBCOMP:
		printf("%-20s", jobcomp->state);
		break;
	case JOBSTEP:
		if ( step->state == JOB_CANCELLED) {
			printf ("%-10s by %6d",
				job_state_string(step->state), step->requid);
		}
		else {
			printf("%-20s", job_state_string(step->state));
		}
		break;
	default:
		printf("%-20s", "n/a");
		break;
	} 
}

void print_submit(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char time_str[32];
		
	switch(type) {
	case HEADLINE:
		printf("%-14s", "Submit Time");
		break;
	case UNDERSCORE:
		printf("%-14s", "--------------");
		break;
	case JOB:
		slurm_make_time_str(&job->submit, 
				    time_str, 
				    sizeof(time_str));
		printf("%-14s", time_str);
		break;
	case JOBSTEP:
		slurm_make_time_str(&step->start, 
				    time_str, 
				    sizeof(time_str));
		printf("%-14s", time_str);
		break;
	default:
		printf("%-14s", "n/a");
		break;
	} 
}

void print_start(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char time_str[32];
	
	switch(type) {
	case HEADLINE:
		printf("%-19s", "Start Time");
		break;
	case UNDERSCORE:
		printf("%-19s", "--------------------");
		break;
	case JOB:
		slurm_make_time_str(&job->start, 
				    time_str, 
				    sizeof(time_str));
		printf("%-19s", time_str);
		break;
	case JOBCOMP:
		printf("%-19s", jobcomp->start_time);
		break;
	case JOBSTEP:
		slurm_make_time_str(&step->start, 
				    time_str, 
				    sizeof(time_str));
		printf("%-19s", time_str);
		break;
	default:
		printf("%-19s", "n/a");
		break;
	} 
}

void print_timelimit(type_t type, void *object)
{ 
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	
	switch(type) {
	case HEADLINE:
		printf("%-10s", "Time Limit");
		break;
	case UNDERSCORE:
		printf("%-10s", "----------");
		break;
	case JOBCOMP:
		printf("%-10s", jobcomp->timelimit);
		break;
	default:
		printf("%-10s", "n/a");
		break;
	} 
}

void print_end(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char time_str[32];
	
	switch(type) {
	case HEADLINE:
		printf("%-19s", "End Time");
		break;
	case UNDERSCORE:
		printf("%-19s", "--------------------");
		break;
	case JOB:
		slurm_make_time_str(&job->end, 
				    time_str, 
				    sizeof(time_str));
		printf("%-19s", time_str);
		break;
	case JOBCOMP:
		printf("%-19s", jobcomp->end_time);
		break;
	case JOBSTEP:
		slurm_make_time_str(&step->end, 
				    time_str, 
				    sizeof(time_str));
		printf("%-19s", time_str);
		break;
	default:
		printf("%-19s", "n/a");
		break;
	} 
}

void print_systemcpu(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char str[FORMAT_STRING_SIZE];

	switch(type) {
	case HEADLINE:
		printf("%-15s", "SystemCpu");
		break;
	case UNDERSCORE:
		printf("%-15s", "---------------");
		break;
	case JOB:
		_elapsed_time(job->sys_cpu_sec,
			      job->sys_cpu_usec, str);
		printf("%-15s", str);
		break;
	case JOBSTEP:
		_elapsed_time(step->sys_cpu_sec,
			      step->sys_cpu_usec, str);
		printf("%-15s", str);
		break;
	default:
		printf("%-15s", "n/a");
		break;
	} 
}

void print_uid(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	int32_t uid = -1;
	struct passwd *pw = NULL;
	
	switch(type) {
	case HEADLINE:
		printf("%-5s", "Uid");
		break;
	case UNDERSCORE:
		printf("%-5s", "-----");
		break;
	case JOB:
		if(job->user) {
			if ((pw=getpwnam(job->user)))
				uid = pw->pw_uid;
		} else 
			uid = job->uid;
		break;
	case JOBCOMP:
		printf("%-5u", jobcomp->uid);
		break;
	case JOBSTEP:
		printf("%-5s", " ");
		break;
	} 

	if(uid != -1) 
		printf("%-5d", uid);
}

void print_user(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobcomp_job_rec_t *jobcomp = (jobcomp_job_rec_t *)object;
	int uid = -1;
	char	*tmp="(unknown)";
	struct	passwd *pw = NULL;		 

	switch(type) {
	case HEADLINE:
		printf("%-9s", "User");
		break;
	case UNDERSCORE:
		printf("%-9s", "---------");
		break;
	case JOB:
		if(job->user) 
			printf("%-9s", job->user);
		else
			uid = job->uid;
		break;
	case JOBCOMP:
		printf("%-9s", jobcomp->uid_name);
		break;
	case JOBSTEP:
		printf("%-9s", " ");
		break;
	default:
		printf("%-9s", "n/a");
		break;
	} 
	if(uid != -1) {
		if ((pw=getpwuid(uid)))
			tmp=pw->pw_name;
		printf("%-9s", tmp);
	}
}

void print_usercpu(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char str[FORMAT_STRING_SIZE];
	
	switch(type) {
	case HEADLINE:
		printf("%-15s", "UserCpu");
		break;
	case UNDERSCORE:
		printf("%-15s", "---------------");
		break;
	case JOB:
		_elapsed_time(job->user_cpu_sec,
			      job->user_cpu_usec, str);
		printf("%-15s", str);
		break;
	case JOBSTEP:
		_elapsed_time(step->user_cpu_sec,
			      step->user_cpu_usec, str);
		printf("%-15s", str);
		break;
	default:
		printf("%-15s", "n/a");
		break;
	} 

}

void print_vsize(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char outbuf[FORMAT_STRING_SIZE];
	char buf1[FORMAT_STRING_SIZE];
	char buf2[FORMAT_STRING_SIZE];
	char buf3[FORMAT_STRING_SIZE];
	sacct_t sacct;
	char *nodes = NULL;
	uint32_t pos;

	switch(type) {
	case HEADLINE:
		printf("%-34s", "MaxVSIZE/Node:Task - Ave");
		break;
	case UNDERSCORE:
		printf("%-34s", "----------------------------------");
		break;
	case JOB:
		sacct = job->sacct;
		nodes = job->nodes;
		pos = sacct.min_cpu_id.nodeid;
		convert_num_unit((float)sacct.max_vsize, 
				 buf1, sizeof(buf1), UNIT_KILO);
		if(job->track_steps)
			snprintf(outbuf, FORMAT_STRING_SIZE, "%s/- - -", buf1);
		else {
			convert_num_unit((float)sacct.ave_vsize,
					 buf2, sizeof(buf2), UNIT_KILO);
			find_hostname(pos, nodes, buf3);
			snprintf(outbuf, FORMAT_STRING_SIZE, "%s/%s:%u - %s", 
				 buf1,
				 buf3, 
				 sacct.max_vsize_id.taskid, 
				 buf2);
		}
		printf("%-34s", outbuf);
		break;
	case JOBSTEP:
		sacct = step->sacct;
		nodes = step->nodes;
		pos = sacct.min_cpu_id.nodeid;				 
		convert_num_unit((float)sacct.max_vsize, buf1, sizeof(buf1), 
				 UNIT_KILO);
		convert_num_unit((float)sacct.ave_vsize, buf2, sizeof(buf2),
				 UNIT_KILO);
		find_hostname(pos, nodes, buf3);
		snprintf(outbuf, FORMAT_STRING_SIZE, "%s/%s:%u - %s", 
			 buf1,
			 buf3, 
			 sacct.max_vsize_id.taskid, 
			 buf2);
		printf("%-34s", outbuf);
		break;
	default:
		printf("%-34s", "n/a");
		break;
	} 
}

void print_cputime(type_t type, void *object)
{ 
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;
	char outbuf[FORMAT_STRING_SIZE];
	char buf1[FORMAT_STRING_SIZE];
	char buf2[FORMAT_STRING_SIZE];
	char buf3[FORMAT_STRING_SIZE];
	sacct_t sacct;
	char *nodes = NULL;
	uint32_t pos;

	switch(type) {
	case HEADLINE:
		printf("%-37s", "MinCPUtime/Node:Task - Ave");
		break;
	case UNDERSCORE:
		printf("%-37s", "-------------------------------------");
		break;
	case JOB:
		sacct = job->sacct;
		nodes = job->nodes;
		pos = sacct.min_cpu_id.nodeid;				 
		_elapsed_time((int)sacct.min_cpu, 0, buf1);
		if(job->track_steps)
			snprintf(outbuf, FORMAT_STRING_SIZE, 
				 "%s/- - -", buf1);
		else {
			_elapsed_time((int)sacct.ave_cpu, 0, buf2);
			find_hostname(pos, nodes, buf3);
			snprintf(outbuf, FORMAT_STRING_SIZE, 
				 "%s/%s:%u - %s", 
				 buf1,
				 buf3, 
				 sacct.min_cpu_id.taskid, 
				 buf2);
		}
		printf("%-37s", outbuf);
		break;
	case JOBSTEP:
		sacct = step->sacct;
		nodes = step->nodes;
		pos = sacct.min_cpu_id.nodeid;				 
		_elapsed_time((int)sacct.min_cpu, 0, buf1);
		_elapsed_time((int)sacct.ave_cpu, 0, buf2);
		find_hostname(pos, nodes, buf3);
		snprintf(outbuf, FORMAT_STRING_SIZE, 
			 "%s/%s:%u - %s", 
			 buf1,
			 buf3, 
			 sacct.min_cpu_id.taskid, 
			 buf2);
		printf("%-37s", outbuf);
		break;
	default:
		printf("%-37s", "n/a");
		break;
	} 
}

void print_account(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-16s", "Account");
		break;
	case UNDERSCORE:
		printf("%-16s", "----------------");
		break;
	case JOB:
		if(!job->account)
			printf("%-16s", "unknown");
		else if(strlen(job->account)<17)
			printf("%-16s", job->account);
		else
			printf("%-13.13s...", job->account);
		break;
	case JOBSTEP:
		if(!step->account)
			printf("%-16s", "unknown");
		else if(strlen(step->account)<17)
			printf("%-16s", step->account);
		else
			printf("%-13.13s...", step->account);
		break;
	default:
		printf("%-16s", "n/a");
		break;
		break;
	}
}

void print_assoc(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-16s", "AssociationID");
		break;
	case UNDERSCORE:
		printf("%-16s", "----------------");
		break;
	case JOB:
		if(!job->associd)
			printf("%-16s", "unknown");
		else 
			printf("%-16u", job->associd);
		break;
	case JOBSTEP:
		if(!step->associd)
			printf("%-16s", "unknown");
		else 
			printf("%-16u", step->associd);
		break;
	default:
		printf("%-16s", "n/a");
		break;
		break;
	}
}

void print_cluster(type_t type, void *object)
{
	jobacct_job_rec_t *job = (jobacct_job_rec_t *)object;
	jobacct_step_rec_t *step = (jobacct_step_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-16s", "Cluster");
		break;
	case UNDERSCORE:
		printf("%-16s", "----------------");
		break;
	case JOB:
		if(!job->cluster)
			printf("%-16s", "unknown");
		else if(strlen(job->cluster)<17)
			printf("%-16s", job->cluster);
		else
			printf("%-13.13s...", job->cluster);
		break;
	case JOBSTEP:
		if(!step->cluster)
			printf("%-16s", "unknown");
		else if(strlen(step->cluster)<17)
			printf("%-16s", step->cluster);
		else
			printf("%-13.13s...", step->cluster);
		break;
	default:
		printf("%-16s", "n/a");
		break;
		break;
	}
}

void print_connection(type_t type, void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-10s", "Connection");
		break;
	case UNDERSCORE:
		printf("%-10s", "----------");
		break;
	case JOBCOMP:
		printf("%-10s", job->connection);
		break;
	default:
		printf("%-10s", "n/a");
		break;
	}
}
void print_geo(type_t type, void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-8s", "Geometry");
		break;
	case UNDERSCORE:
		printf("%-8s", "--------");
		break;
	case JOBCOMP:
		printf("%-8s", job->geo);
		break;
	default:
		printf("%-8s", "n/a");
		break;
	}
}
void print_max_procs(type_t type, void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-9s", "Max Procs");
		break;
	case UNDERSCORE:
		printf("%-9s", "---------");
		break;
	case JOBCOMP:
		printf("%-9d", job->max_procs);
		break;
	default:
		printf("%-9s", "n/a");
		break;
	}
}
void print_reboot(type_t type, void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-6s", "Reboot");
		break;
	case UNDERSCORE:
		printf("%-6s", "------");
		break;
	case JOBCOMP:
		printf("%-6s", job->reboot);
		break;
	default:
		printf("%-6s", "n/a");
		break;
	}
}
void print_rotate(type_t type, void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-6s", "Rotate");
		break;
	case UNDERSCORE:
		printf("%-6s", "------");
		break;
	case JOBCOMP:
		printf("%-6s", job->rotate);
		break;
	default:
		printf("%-6s", "n/a");
		break;
	}
}
void print_bg_start_point(type_t type, void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;

	switch(type) {
	case HEADLINE:
		printf("%-14s", "BG Start Point");
		break;
	case UNDERSCORE:
		printf("%-14s", "--------------");
		break;
	case JOBCOMP:
		printf("%-14s", job->bg_start_point);
		break;
	default:
		printf("%-14s", "n/a");
		break;
	}
}

