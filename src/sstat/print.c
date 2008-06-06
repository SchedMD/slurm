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

#include "sstat.h"
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

extern void print_fields(type_t type, void *object)
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

extern void print_cputime(type_t type, void *object)
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

extern void print_jobid(type_t type, void *object)
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

extern void print_ntasks(type_t type, void *object)
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

extern void print_pages(type_t type, void *object)
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

extern void print_rss(type_t type, void *object)
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

extern void print_state(type_t type, void *object)
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

extern void print_vsize(type_t type, void *object)
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
				 buf1, sizeof(buf1),UNIT_KILO);
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
