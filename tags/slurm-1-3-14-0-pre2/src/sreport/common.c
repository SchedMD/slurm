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
		double temp_d = (double)value;
		
		switch(time_format) {
		case SREPORT_TIME_SECS:
			output = xstrdup_printf("%llu", value);
			break;
		case SREPORT_TIME_MINS:
			temp_d /= 60;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		case SREPORT_TIME_HOURS:
			temp_d /= 3600;
			output = xstrdup_printf("%.0lf", temp_d);
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
		case SREPORT_TIME_MINS_PER:
			percent /= total_time;
			percent *= 100;
			temp_d /= 60;
			output = xstrdup_printf("%.0lf(%.2lf%%)",
						temp_d, percent);
			break;
		case SREPORT_TIME_HOURS_PER:
			percent /= total_time;
			percent *= 100;
			temp_d /= 3600;
			output = xstrdup_printf("%.0lf(%.2lf%%)",
						temp_d, percent);
			break;
		default:
			temp_d /= 60;
			output = xstrdup_printf("%.0lf", temp_d);
			break;
		}
		
		if(print_fields_parsable_print 
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%s", output);
		else if(print_fields_parsable_print)
			printf("%s|", output);	
		else
			printf("%*.*s ", field->len, field->len, output);
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
	time_t temp_time;
	struct tm start_tm;
	struct tm end_tm;
	int sent_start = (*start), sent_end = (*end);

//	info("now got %d and %d sent", (*start), (*end));
	/* Default is going to be the last day */
	if(!sent_end) {
		if(!localtime_r(&my_time, &end_tm)) {
			error("Couldn't get localtime from end %d",
			      my_time);
			return SLURM_ERROR;
		}
		end_tm.tm_hour = 0;
		//(*end) = mktime(&end_tm);		
	} else {
		temp_time = sent_end;
		if(!localtime_r(&temp_time, &end_tm)) {
			error("Couldn't get localtime from user end %d",
			      my_time);
			return SLURM_ERROR;
		}
		if(end_tm.tm_sec >= 30)
			end_tm.tm_min++;
		if(end_tm.tm_min >= 30)
			end_tm.tm_hour++;
	}
	
	end_tm.tm_sec = 0;
	end_tm.tm_min = 0;
	end_tm.tm_isdst = -1;
	(*end) = mktime(&end_tm);		

	if(!sent_start) {
		if(!localtime_r(&my_time, &start_tm)) {
			error("Couldn't get localtime from start %d",
			      my_time);
			return SLURM_ERROR;
		}
		start_tm.tm_hour = 0;
		start_tm.tm_mday--;
		//(*start) = mktime(&start_tm);		
	} else {
		temp_time = sent_start;
		if(!localtime_r(&temp_time, &start_tm)) {
			error("Couldn't get localtime from user start %d",
			      my_time);
			return SLURM_ERROR;
		}
		if(start_tm.tm_sec >= 30)
			start_tm.tm_min++;
		if(start_tm.tm_min >= 30)
			start_tm.tm_hour++;
	}
	start_tm.tm_sec = 0;
	start_tm.tm_min = 0;
	start_tm.tm_isdst = -1;
	(*start) = mktime(&start_tm);		

	if((*end)-(*start) < 3600) 
		(*end) = (*start) + 3600;
/* 	info("now got %d and %d sent", (*start), (*end)); */
/* 	char start_char[20]; */
/* 	char end_char[20]; */
/* 	time_t my_start = (*start); */
/* 	time_t my_end = (*end); */
	
/* 	slurm_make_time_str(&my_start,  */
/* 			    start_char, sizeof(start_char)); */
/* 	slurm_make_time_str(&my_end, */
/* 			    end_char, sizeof(end_char)); */
/* 	info("which is %s - %s", start_char, end_char); */
	return SLURM_SUCCESS;
}

extern void destroy_sreport_assoc_rec(void *object)
{
	sreport_assoc_rec_t *sreport_assoc = (sreport_assoc_rec_t *)object;
	if(sreport_assoc) {
		xfree(sreport_assoc->acct);
		xfree(sreport_assoc->cluster);
		xfree(sreport_assoc->parent_acct);
		xfree(sreport_assoc->user);
		xfree(sreport_assoc);
	}
}

extern void destroy_sreport_user_rec(void *object)
{
	sreport_user_rec_t *sreport_user = (sreport_user_rec_t *)object;
	if(sreport_user) {
		xfree(sreport_user->acct);
		if(sreport_user->acct_list)
			list_destroy(sreport_user->acct_list);
		xfree(sreport_user->name);
		xfree(sreport_user);
	}
}

extern void destroy_sreport_cluster_rec(void *object)
{
	sreport_cluster_rec_t *sreport_cluster = 
		(sreport_cluster_rec_t *)object;
	if(sreport_cluster) {
		if(sreport_cluster->assoc_list)
			list_destroy(sreport_cluster->assoc_list);
		xfree(sreport_cluster->name);
		if(sreport_cluster->user_list)
			list_destroy(sreport_cluster->user_list);
		xfree(sreport_cluster);
	}
}

/* 
 * Comparator used for sorting users largest cpu to smallest cpu
 * 
 * returns: 1: user_a > user_b   0: user_a == user_b   -1: user_a < user_b
 * 
 */
extern int sort_user_dec(sreport_user_rec_t *user_a, sreport_user_rec_t *user_b)
{
	int diff = 0;

	if(sort_flag == SREPORT_SORT_TIME) {
		if (user_a->cpu_secs > user_b->cpu_secs)
			return -1;
		else if (user_a->cpu_secs < user_b->cpu_secs)
			return 1;
	}

	if(!user_a->name || !user_b->name)
		return 0;

	diff = strcmp(user_a->name, user_b->name);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;
	
	return 0;
}

/* 
 * Comparator used for sorting clusters alphabetically
 * 
 * returns: 1: cluster_a > cluster_b   
 *           0: cluster_a == cluster_b
 *           -1: cluster_a < cluster_b
 * 
 */
extern int sort_cluster_dec(sreport_cluster_rec_t *cluster_a,
			    sreport_cluster_rec_t *cluster_b)
{
	int diff = 0;

	if(!cluster_a->name || !cluster_b->name)
		return 0;

	diff = strcmp(cluster_a->name, cluster_b->name);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;
	
	return 0;
}

/* 
 * Comparator used for sorting assocs alphabetically by acct and then
 * by user.  The association with a total count of time is at the top
 * of the accts.
 * 
 * returns: -1: assoc_a > assoc_b   
 *           0: assoc_a == assoc_b
 *           1: assoc_a < assoc_b
 * 
 */
extern int sort_assoc_dec(sreport_assoc_rec_t *assoc_a,
			  sreport_assoc_rec_t *assoc_b)
{
	int diff = 0;

	if(!assoc_a->acct || !assoc_b->acct)
		return 0;

	diff = strcmp(assoc_a->acct, assoc_b->acct);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;
	
	if(!assoc_a->user && assoc_b->user)
		return 1;
	else if(!assoc_b->user)
		return -1;

	diff = strcmp(assoc_a->user, assoc_b->user);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;
	

	return 0;
}

extern int get_uint(char *in_value, uint32_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;
	
	if(!(meat = strip_quotes(in_value, NULL))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);
	
	if (num < 0)
		*out_value = INFINITE;		/* flag to clear */
	else
		*out_value = (uint32_t) num;
	return SLURM_SUCCESS;
}

