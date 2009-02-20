/*****************************************************************************\
 *  print.c - print functions for sacct
 *
 *  $Id: print.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
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

#include "sstat.h"
#include "src/common/parse_time.h"
#include "slurm.h"
#define FORMAT_STRING_SIZE 34

char *_elapsed_time(long secs, long usecs);

char *_elapsed_time(long secs, long usecs)
{
	long	days, hours, minutes, seconds;
	long    subsec = 0;
	char *str = NULL;

	if(secs < 0 || secs == NO_VAL)
		return NULL;
	
	
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
		str = xstrdup_printf("%ld-%2.2ld:%2.2ld:%2.2ld",
				     days, hours, minutes, seconds);
	else if (hours)
		str = xstrdup_printf("%2.2ld:%2.2ld:%2.2ld",
				     hours, minutes, seconds);
	else
		str = xstrdup_printf("%2.2ld:%2.2ld.%3.3ld",
				     minutes, seconds, subsec);
	return str;
}

void print_fields(jobacct_step_rec_t *step)
{
	print_field_t *field = NULL;
	int curr_inx = 1;
	char outbuf[FORMAT_STRING_SIZE];

	list_iterator_reset(print_fields_itr);
	while((field = list_next(print_fields_itr))) {
		char *tmp_char = NULL;
		
		switch(field->type) {
		case PRINT_AVECPU:
			
			tmp_char = _elapsed_time((int)step->sacct.ave_cpu, 0);
			
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_AVEPAGES:
			convert_num_unit((float)step->sacct.ave_pages,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);
			
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVERSS:
			convert_num_unit((float)step->sacct.ave_rss,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);
			
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEVSIZE:
			convert_num_unit((float)step->sacct.ave_vsize,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);
			
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_JOBID:
			snprintf(outbuf, sizeof(outbuf), "%u.%u",
				 step->job_ptr->jobid,
				 step->stepid);
			
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGES:
			convert_num_unit((float)step->sacct.max_pages,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);
			
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGESNODE:
			tmp_char = find_hostname(
					step->sacct.max_pages_id.nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXPAGESTASK:
			field->print_routine(field,
					     step->sacct.max_pages_id.taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSS:
			convert_num_unit((float)step->sacct.max_rss,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);
			
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSSNODE:
			tmp_char = find_hostname(
					step->sacct.max_rss_id.nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXRSSTASK:
			field->print_routine(field,
					     step->sacct.max_rss_id.taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXVSIZE:
			convert_num_unit((float)step->sacct.max_vsize,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);
			
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXVSIZENODE:
			tmp_char = find_hostname(
					step->sacct.max_vsize_id.nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXVSIZETASK:
			field->print_routine(field,
					     step->sacct.max_vsize_id.taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MINCPU:
			tmp_char = _elapsed_time((int)step->sacct.min_cpu, 0);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUNODE:
			tmp_char = find_hostname(
					step->sacct.min_cpu_id.nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUTASK:
			field->print_routine(field,
					     step->sacct.min_cpu_id.taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_NTASKS:
			field->print_routine(field,
					     step->ntasks,
					     (curr_inx == field_count));
			break;
		case PRINT_SYSTEMCPU:
			tmp_char = _elapsed_time(step->sys_cpu_sec,
						 step->sys_cpu_usec);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_TOTALCPU:
			tmp_char = _elapsed_time(step->tot_cpu_sec, 
						 step->tot_cpu_usec);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		default:
			break;
		}
		curr_inx++;
	}
	printf("\n");
}

