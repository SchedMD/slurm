/*****************************************************************************\
 *  print.c - sinfo print job functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov> and 
 *  Morris Jette <jette1@llnl.gov>
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

#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#include "src/common/list.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/sinfo/print.h"
#include "src/sinfo/sinfo.h"

#define MIN_NODE_FIELD_SIZE 9

static int   _build_min_max_string(char *buffer, int buf_size, int min, 
                                   int max, bool range);
static int   _print_secs(long time, int width, bool right, bool cut_output);
static int   _print_str(char *str, int width, bool right, bool cut_output);
static void  _set_node_field_size(List sinfo_list);
static char *_str_tolower(char *upper_str);

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/
void print_date(void)
{
	time_t now;

	now = time(NULL);
	printf("%s", ctime(&now));
}

int print_sinfo_list(List sinfo_list)
{
	ListIterator i = list_iterator_create(sinfo_list);
	sinfo_data_t *current;

	if (params.node_field_flag)
		_set_node_field_size(sinfo_list);

	if (!params.no_header)
		print_sinfo_entry(NULL);

	while ((current = list_next(i)) != NULL)
		 print_sinfo_entry(current);

	list_iterator_destroy(i);
	return SLURM_SUCCESS;
}

int print_sinfo_entry(sinfo_data_t *sinfo_data)
{
	ListIterator i = list_iterator_create(params.format_list);
	sinfo_format_t *current;

	while ((current = (sinfo_format_t *) list_next(i)) != NULL) {
		if (current->
		    function(sinfo_data, current->width, 
					current->right_justify, current->suffix)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
	}
	list_iterator_destroy(i);

	printf("\n");
	return SLURM_SUCCESS;
}

/*****************************************************************************
 * Local Print Functions
 *****************************************************************************/

static int _print_str(char *str, int width, bool right, bool cut_output)
{
	char format[64];
	int printed = 0;

	if (right == true && width != 0)
		snprintf(format, 64, "%%%ds", width);
	else if (width != 0)
		snprintf(format, 64, "%%.%ds", width);
	else {
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}

	if ((width == 0) || (cut_output == false)) {
		if ((printed = printf(format, str)) < 0)
			return printed;
	} else {
		char temp[width + 1];
		snprintf(temp, width + 1, format, str);
		if ((printed = printf("%s",temp)) < 0)
			return printed;
	}

	while (printed++ < width)
		printf(" ");

	return printed;
}

static int _print_secs(long time, int width, bool right, bool cut_output)
{
	char str[FORMAT_STRING_SIZE];
	long days, hours, minutes, seconds;

	seconds =  time % 60;
	minutes = (time / 60)   % 60;
	hours   = (time / 3600) % 24;
	days    =  time / 86400;

	if (days) 
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld:%2.2ld:%2.2ld",
		         days, hours, minutes, seconds);
	else if (hours)
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld:%2.2ld",
		         hours, minutes, seconds);
	else
		snprintf(str, FORMAT_STRING_SIZE,
			 "%ld:%2.2ld",
		         minutes, seconds);

	_print_str(str, width, right, cut_output);
	return SLURM_SUCCESS;
}

static int 
_build_min_max_string(char *buffer, int buf_size, int min, int max, bool range)
{
	if (max == min)
		return snprintf(buffer, buf_size, "%d", max);
	else if (range) {
		if (max == INFINITE)
			return snprintf(buffer, buf_size, "%d-infinite", min);
		else
			return snprintf(buffer, buf_size, "%d-%d", min, max);
	} else
		return snprintf(buffer, buf_size, "%d+", min);
}

int
format_add_function(List list, int width, bool right, char *suffix, 
			int (*function) (sinfo_data_t *, int, bool, char*))
{
	sinfo_format_t *tmp = 
		(sinfo_format_t *) xmalloc(sizeof(sinfo_format_t));
	tmp->function = function;
	tmp->width = width;
	tmp->right_justify = right;
	tmp->suffix = suffix;

	if (list_append(list, tmp) == NULL) {
		fprintf(stderr, "Memory exhausted\n");
		exit(1);
	}
	return SLURM_SUCCESS;
}

static void _set_node_field_size(List sinfo_list)
{
	char tmp[1024];
	ListIterator i = list_iterator_create(sinfo_list);
	sinfo_data_t *current;
	int max_width = MIN_NODE_FIELD_SIZE, this_width = 0;

	while ((current = (sinfo_data_t *) list_next(i)) != NULL) {
		this_width = hostlist_ranged_string(current->nodes, 
					sizeof(tmp), tmp);
		max_width = MAX(max_width, this_width);
	}
	list_iterator_destroy(i);
	params.node_field_size = max_width;
}

/*
 * _str_tolower - convert string to all lower case
 * upper_str IN - upper case input string
 * RET - lower case version of upper_str, caller must be xfree
 */ 
static char *_str_tolower(char *upper_str)
{
	int i = strlen(upper_str) + 1;
	char *lower_str = xmalloc(i);

	for (i=0; upper_str[i]; i++)
		lower_str[i] = tolower((int) upper_str[i]);

	return lower_str;
}

/*****************************************************************************
 * Sinfo Print Functions
 *****************************************************************************/

int _print_avail(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else if (sinfo_data->part_info->state_up)
			_print_str("up", width, right_justify, true);
		else
			_print_str("down", width, right_justify, true);
	} else
		_print_str("AVAIL", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_cpus(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_string(id, FORMAT_STRING_SIZE, 
		                      sinfo_data->min_cpus, 
		                      sinfo_data->max_cpus, false);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("CPUS", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_disk(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_string(id, FORMAT_STRING_SIZE, 
		                      sinfo_data->min_disk, 
		                      sinfo_data->max_disk, false);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("TMP_DISK", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_features(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data)
		_print_str(sinfo_data->features, width, right_justify, true);
	else
		_print_str("FEATURES", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_groups(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else if (sinfo_data->part_info->allow_groups)
			_print_str(sinfo_data->part_info->allow_groups, 
					width, right_justify, true);
		else
			_print_str("all", width, right_justify, true);
	} else
		_print_str("GROUPS", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_memory(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_string(id, FORMAT_STRING_SIZE, 
		                      sinfo_data->min_mem, 
		                      sinfo_data->max_mem, false);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("MEMORY", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_node_list(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (params.node_field_flag)
		width = params.node_field_size;

	if (sinfo_data) {
		char tmp[1024];
		hostlist_ranged_string(sinfo_data->nodes, 
					sizeof(tmp), tmp);
		_print_str(tmp, width, right_justify, true);
	} else
		_print_str("NODELIST", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_nodes_t(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		snprintf(id, FORMAT_STRING_SIZE, "%u", sinfo_data->nodes_tot);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("NODES", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_nodes_ai(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		snprintf(id, FORMAT_STRING_SIZE, "%u/%u", 
		         sinfo_data->nodes_alloc, sinfo_data->nodes_idle);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("NODES(A/I)", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_nodes_aiot(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		snprintf(id, FORMAT_STRING_SIZE, "%u/%u/%u/%u", 
		         sinfo_data->nodes_alloc, sinfo_data->nodes_idle,
		         sinfo_data->nodes_other, sinfo_data->nodes_tot);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("NODES(A/I/O/T)", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_partition(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else {
			char *tmp;
			tmp = xstrdup(sinfo_data->part_info->name);
			if (sinfo_data->part_info->default_part) {
				if (strlen(tmp) < width)
					xstrcat(tmp, "*");
				else if (width > 0)
					tmp[width-1] = '*';
			}
			_print_str(tmp, width, right_justify, true);
			xfree(tmp);
		}
	} else
		_print_str("PARTITION", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_prefix(sinfo_data_t * job, int width, bool right_justify, 
		char* suffix)
{
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_reason(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		char * reason = sinfo_data->reason ? sinfo_data->reason:"none";
		if (strncmp(reason, "(null)", 6) == 0) 
			reason = "none";
		_print_str(reason, width, right_justify, true);
	} else
		_print_str("REASON", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_root(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else if (sinfo_data->part_info->root_only)
			_print_str("yes", width, right_justify, true);
		else
			_print_str("no", width, right_justify, true);
	} else
		_print_str("ROOT", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_share(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else if (sinfo_data->part_info->shared > 1)
			_print_str("force", width, right_justify, true);
		else if (sinfo_data->part_info->shared)
			_print_str("yes", width, right_justify, true);
		else
			_print_str("no", width, right_justify, true);
	} else
		_print_str("SHARE", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_size(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else {
			if ((sinfo_data->part_info->min_nodes < 1) &&
			    (sinfo_data->part_info->max_nodes > 0))
				sinfo_data->part_info->min_nodes = 1;
			_build_min_max_string(id, FORMAT_STRING_SIZE, 
		                      sinfo_data->part_info->min_nodes, 
		                      sinfo_data->part_info->max_nodes,
		                      true);
			_print_str(id, width, right_justify, true);
		}
	} else
		_print_str("JOB_SIZE", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_state_compact(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data && sinfo_data->nodes_tot) {
		char *upper_state = node_state_string_compact(
				sinfo_data->node_state);
		char *lower_state = _str_tolower(upper_state);
		_print_str(lower_state, width, right_justify, true);
		xfree(lower_state);
	} else if (sinfo_data)
		_print_str("n/a", width, right_justify, true);
	else
		_print_str("STATE", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_state_long(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data && sinfo_data->nodes_tot) {
		char *upper_state = node_state_string(sinfo_data->node_state);
		char *lower_state = _str_tolower(upper_state);
		_print_str(lower_state, width, right_justify, true);
		xfree(lower_state);
	} else if (sinfo_data)
		_print_str("n/a", width, right_justify, true);
	else
		_print_str("STATE", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}


int _print_time(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else if (sinfo_data->part_info->max_time == INFINITE)
			_print_str("infinite", width, right_justify, true);
		else
			_print_secs((sinfo_data->part_info->max_time * 60L),
					width, right_justify, true);
	} else
		_print_str("TIMELIMIT", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_weight(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_string(id, FORMAT_STRING_SIZE, 
		                      sinfo_data->min_weight, 
		                      sinfo_data->max_weight, false);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("WEIGHT", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}
