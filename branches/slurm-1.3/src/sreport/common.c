/*****************************************************************************\
 *  common.c - common functions for generating reports
 *             from accounting infrastructure.
 *****************************************************************************
 *
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "sreport.h"

extern void sreport_print_time(print_field_t *field,
			       uint64_t value, uint64_t total_time, int last)
{
	if(!total_time)
		total_time = 1;

	/* (value == unset)  || (value == cleared) */
	if((value == NO_VAL) || (value == INFINITE)) {
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if(print_fields_parsable_print)
			printf("|");	
		else				
			printf("%-*s ", field->len, " ");
	} else {
		char *output = NULL;
		double percent = (double)value;
		
		switch(time_format) {
		case SREPORT_TIME_SECS:
			output = xstrdup_printf("%llu", value);
			break;
		case SREPORT_TIME_PERCENT:
			percent /= total_time;
			percent *= 100;
			output = xstrdup_printf("%.2lf%%", percent);
			break; 
		case SREPORT_TIME_SECS_PER:
			percent /= total_time;
			percent *= 100;
			output = xstrdup_printf("%llu(%.2lf%%)",
						value, percent);
			break;
		default:
			output = xstrdup_printf("%llu", value);
			break;
		}
		
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%s", output);
		else if(print_fields_parsable_print)
			printf("%s|", output);	
		else
			printf("%*s ", field->len, output);
		xfree(output);
	}
}

extern int parse_option_end(char *option)
{
	int end = 0;
	
	if(!option)
		return 0;

	while(option[end] && option[end] != '=')
		end++;
	if(!option[end])
		return 0;
	end++;
	return end;
}

/* you need to xfree whatever is sent from here */
extern char *strip_quotes(char *option, int *increased)
{
	int end = 0;
	int i=0, start=0;
	char *meat = NULL;

	if(!option)
		return NULL;

	/* first strip off the ("|')'s */
	if (option[i] == '\"' || option[i] == '\'')
		i++;
	start = i;

	while(option[i]) {
		if(option[i] == '\"' || option[i] == '\'') {
			end++;
			break;
		}
		i++;
	}
	end += i;

	meat = xmalloc((i-start)+1);
	memcpy(meat, option+start, (i-start));

	if(increased)
		(*increased) += end;

	return meat;
}

extern void addto_char_list(List char_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;

	if(!char_list) {
		error("No list was given to fill in");
		return;
	}

	itr = list_iterator_create(char_list);
	if(names) {
		if (names[i] == '\"' || names[i] == '\'')
			i++;
		start = i;
		while(names[i]) {
			if(names[i] == '\"' || names[i] == '\'')
				break;
			else if(names[i] == ',') {
				if((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));

					while((tmp_char = list_next(itr))) {
						if(!strcasecmp(tmp_char, name))
							break;
					}

					if(!tmp_char)
						list_append(char_list, name);
					else 
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
			}
			i++;
		}
		if((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			while((tmp_char = list_next(itr))) {
				if(!strcasecmp(tmp_char, name))
					break;
			}
			
			if(!tmp_char)
				list_append(char_list, name);
			else 
				xfree(name);
		}
	}	
	list_iterator_destroy(itr);
} 

extern int set_start_end_time(time_t *start, time_t *end)
{
	time_t my_time = time(NULL);
	struct tm start_tm;
	struct tm end_tm;

	/* Default is going to be the last day */
	if(!(*end)) {
		if(!localtime_r(&my_time, &end_tm)) {
			error("Couldn't get localtime from end %d",
			      my_time);
			return SLURM_ERROR;
		}
		end_tm.tm_hour = 0;
		(*end) = mktime(&end_tm);		
	} else {
		if(!localtime_r(end, &end_tm)) {
			error("Couldn't get localtime from user end %d",
			      my_time);
			return SLURM_ERROR;
		}
	}
	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_isdst = -1;
	(*end) = mktime(&end_tm);		

	if(!(*start)) {
		if(!localtime_r(&my_time, &start_tm)) {
			error("Couldn't get localtime from start %d",
			      my_time);
			return SLURM_ERROR;
		}
		start_tm.tm_hour = 0;
		start_tm.tm_mday--;
		(*start) = mktime(&start_tm);		
	} else {
		if(!localtime_r(start, &start_tm)) {
			error("Couldn't get localtime from user start %d",
			      my_time);
			return SLURM_ERROR;
		}
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_isdst = -1;
	(*start) = mktime(&start_tm);		

	if((*end)-(*start) < 3600) 
		(*end) = (*start) + 3600;

	return SLURM_SUCCESS;
}
