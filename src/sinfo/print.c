/*****************************************************************************\
 *  print.c - sinfo print job functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010-2017 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Joey Ekstrom <ekstrom1@llnl.gov> and
 *  Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <sys/types.h>

#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/sinfo/print.h"
#include "src/sinfo/sinfo.h"

#define MIN_NODE_FIELD_SIZE 9
#define MIN_PART_FIELD_SIZE 9

static int   _build_min_max_16_string(char *buffer, int buf_size,
				uint16_t min, uint16_t max, bool range);
static int   _build_min_max_32_string(char *buffer, int buf_size,
				uint32_t min, uint32_t max,
				bool range, bool use_suffix);
static int   _build_cpu_load_min_max_32(char *buffer, int buf_size,
					uint32_t min, uint32_t max,
					bool range);
static int   _build_free_mem_min_max_64(char *buffer, int buf_size,
					uint64_t min, uint64_t max,
					bool range);
static void  _print_reservation(reserve_info_t *resv_ptr, int width);
static int   _print_secs(long time, int width, bool right, bool cut_output);
static int   _print_str(const char *str, int width, bool right, bool cut_output);
static int   _resv_name_width(reserve_info_t *resv_ptr);
static void  _set_node_field_size(List sinfo_list);
static void  _set_part_field_size(List sinfo_list);
static char *_str_tolower(char *upper_str);

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/
int print_sinfo_list(List sinfo_list)
{
	ListIterator i = list_iterator_create(sinfo_list);
	sinfo_data_t *current;

	if (params.node_field_flag)
		_set_node_field_size(sinfo_list);
	if (params.part_field_flag)
		_set_part_field_size(sinfo_list);

	if (!params.no_header)
		print_sinfo_entry(NULL);

	while ((current = list_next(i)))
		 print_sinfo_entry(current);

	list_iterator_destroy(i);
	return SLURM_SUCCESS;
}

int print_sinfo_entry(sinfo_data_t *sinfo_data)
{
	ListIterator i = list_iterator_create(params.format_list);
	sinfo_format_t *current;

	while ((current = list_next(i))) {
		if (current->function(sinfo_data, current->width,
				      current->right_justify, current->suffix)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
	}
	list_iterator_destroy(i);

	printf("\n");
	return SLURM_SUCCESS;
}

void print_sinfo_reservation(reserve_info_msg_t *resv_ptr)
{
	reserve_info_t *reserve_ptr = NULL;
	char format[64];
	int i, width = 9;

	reserve_ptr = resv_ptr->reservation_array;
	if (!params.no_header) {
		for (i = 0; i < resv_ptr->record_count; i++)
			width = MAX(width, _resv_name_width(&reserve_ptr[i]));
		snprintf(format, sizeof(format),
			 "%%-%ds  %%8s  %%19s  %%19s  %%11s  %%s\n", width);
		printf(format,
		       "RESV_NAME", "STATE", "START_TIME", "END_TIME",
		       "DURATION", "NODELIST");
	}
	for (i = 0; i < resv_ptr->record_count; i++)
		_print_reservation(&reserve_ptr[i], width);
}

/*****************************************************************************
 * Local Print Functions
 *****************************************************************************/
static int _resv_name_width(reserve_info_t *resv_ptr)
{
	if (!resv_ptr->name)
		return 0;
	return strlen(resv_ptr->name);
}

static void _print_reservation(reserve_info_t *resv_ptr, int width)
{
	char format[64], tmp1[32], tmp2[32], tmp3[32];
	char *state = "INACTIVE";
	uint32_t duration;
	time_t now = time(NULL);

	slurm_make_time_str(&resv_ptr->start_time, tmp1, sizeof(tmp1));
	slurm_make_time_str(&resv_ptr->end_time,   tmp2, sizeof(tmp2));
	duration = difftime(resv_ptr->end_time, resv_ptr->start_time);
	secs2time_str(duration, tmp3, sizeof(tmp3));

	if ((resv_ptr->start_time <= now) && (resv_ptr->end_time >= now))
		state = "ACTIVE";
	snprintf(format, sizeof(format),
		 "%%-%ds  %%8s  %%19s  %%19s  %%11s  %%s\n", width);
	printf(format,
	       resv_ptr->name, state, tmp1, tmp2, tmp3, resv_ptr->node_list);

	return;
}

static int _print_str(const char *str, int width, bool right, bool cut_output)
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
			 "%ld-%2.2ld:%2.2ld:%2.2ld",
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
_build_min_max_16_string(char *buffer, int buf_size, uint16_t min, uint16_t max,
			 bool range)
{
	char tmp_min[8];
	char tmp_max[8];
	convert_num_unit((float)min, tmp_min, sizeof(tmp_min), UNIT_NONE,
			 NO_VAL, params.convert_flags);
	convert_num_unit((float)max, tmp_max, sizeof(tmp_max), UNIT_NONE,
			 NO_VAL, params.convert_flags);

	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range) {
		if (max == INFINITE16)
			return snprintf(buffer, buf_size, "%s-infinite",
					tmp_min);
		else
			return snprintf(buffer, buf_size, "%s-%s",
					tmp_min, tmp_max);
	} else
		return snprintf(buffer, buf_size, "%s+", tmp_min);
}

static int
_build_min_max_32_string(char *buffer, int buf_size,
			 uint32_t min, uint32_t max,
			 bool range, bool use_suffix)
{
	char tmp_min[8];
	char tmp_max[8];

	if (use_suffix) {
		convert_num_unit((float)min, tmp_min, sizeof(tmp_min),
				 UNIT_NONE, NO_VAL, params.convert_flags);
		convert_num_unit((float)max, tmp_max, sizeof(tmp_max),
				 UNIT_NONE, NO_VAL, params.convert_flags);
	} else {
		snprintf(tmp_min, sizeof(tmp_min), "%u", min);
		snprintf(tmp_max, sizeof(tmp_max), "%u", max);
	}

	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range) {
		if (max == INFINITE)
			return snprintf(buffer, buf_size, "%s-infinite",
					tmp_min);
		else
			return snprintf(buffer, buf_size, "%s-%s",
					tmp_min, tmp_max);
	} else
		return snprintf(buffer, buf_size, "%s+", tmp_min);


}

static int
_build_cpu_load_min_max_32(char *buffer, int buf_size,
			    uint32_t min, uint32_t max,
			    bool range)
{

	char tmp_min[8];
	char tmp_max[8];

	if (min == NO_VAL) {
		strcpy(tmp_min, "N/A");
	} else {
		snprintf(tmp_min, sizeof(tmp_min), "%.2f", (min/100.0));
	}

	if (max == NO_VAL) {
		strcpy(tmp_max, "N/A");
	} else {
		snprintf(tmp_max, sizeof(tmp_max), "%.2f", (max/100.0));
	}

	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range)
		return snprintf(buffer, buf_size, "%s-%s", tmp_min, tmp_max);
	else
		return snprintf(buffer, buf_size, "%s+", tmp_min);
}

static int
_build_free_mem_min_max_64(char *buffer, int buf_size,
			    uint64_t min, uint64_t max,
			    bool range)
{

	char tmp_min[16];
	char tmp_max[16];

	if (min == NO_VAL64) {
		strcpy(tmp_min, "N/A");
	} else {
		snprintf(tmp_min, sizeof(tmp_min), "%"PRIu64"", min);
	}

	if (max == NO_VAL64) {
		strcpy(tmp_max, "N/A");
	} else {
		snprintf(tmp_max, sizeof(tmp_max), "%"PRIu64"", max);
	}

	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range)
		return snprintf(buffer, buf_size, "%s-%s", tmp_min, tmp_max);
	else
		return snprintf(buffer, buf_size, "%s+", tmp_min);
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
	list_append(list, tmp);

	return SLURM_SUCCESS;
}

int
format_prepend_function(List list, int width, bool right, char *suffix,
			int (*function) (sinfo_data_t *, int, bool, char*))
{
	sinfo_format_t *tmp =
		(sinfo_format_t *) xmalloc(sizeof(sinfo_format_t));
	tmp->function = function;
	tmp->width = width;
	tmp->right_justify = right;
	tmp->suffix = suffix;
	list_prepend(list, tmp);

	return SLURM_SUCCESS;
}

static void _set_node_field_size(List sinfo_list)
{
	char *tmp = NULL;
	ListIterator i = list_iterator_create(sinfo_list);
	sinfo_data_t *current;
	int max_width = MIN_NODE_FIELD_SIZE, this_width = 0;

	while ((current = list_next(i))) {
		tmp = hostlist_ranged_string_xmalloc(current->nodes);
		this_width = strlen(tmp);
		xfree(tmp);
		max_width = MAX(max_width, this_width);
	}
	list_iterator_destroy(i);
	params.node_field_size = max_width;
}

static void _set_part_field_size(List sinfo_list)
{
	ListIterator i = list_iterator_create(sinfo_list);
	sinfo_data_t *current;
	int max_width = MIN_PART_FIELD_SIZE, this_width = 0;

	while ((current = list_next(i))) {
		if (!current->part_info || !current->part_info->name)
			continue;
		this_width = strlen(current->part_info->name);
		if (current->part_info->flags & PART_FLAG_DEFAULT)
			this_width++;
		max_width = MAX(max_width, this_width);
	}
	list_iterator_destroy(i);
	params.part_field_size = max_width;
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
		else if (sinfo_data->part_info->state_up == PARTITION_UP)
			_print_str("up", width, right_justify, true);
		else if (sinfo_data->part_info->state_up == PARTITION_DOWN)
			_print_str("down", width, right_justify, true);
		else if (sinfo_data->part_info->state_up == PARTITION_DRAIN)
			_print_str("drain", width, right_justify, true);
		else if (sinfo_data->part_info->state_up == PARTITION_INACTIVE)
			_print_str("inactive", width, right_justify, true);
		else
			_print_str("unknown", width, right_justify, true);
	} else
		_print_str("AVAIL", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_comment(sinfo_data_t *sinfo_data, int width,
		   bool right_justify, char *suffix)
{
	if (sinfo_data)
		_print_str(sinfo_data->comment, width, right_justify, true);
	else
		_print_str("COMMENT", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_cpus(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_32_string(id, FORMAT_STRING_SIZE,
				      sinfo_data->min_cpus,
				      sinfo_data->max_cpus,
				      false, true);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("CPUS", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

/* Cpus, allocated/idle/other/total */
int _print_cpus_aiot(sinfo_data_t * sinfo_data, int width,
		     bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		snprintf(id, FORMAT_STRING_SIZE, "%u/%u/%u/%u",
			 sinfo_data->cpus_alloc, sinfo_data->cpus_idle,
			 sinfo_data->cpus_other, sinfo_data->cpus_total);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("CPUS(A/I/O/T)", width, right_justify, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_sct(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char sockets[FORMAT_STRING_SIZE];
	char cores[FORMAT_STRING_SIZE];
	char threads[FORMAT_STRING_SIZE];
	char sct[(FORMAT_STRING_SIZE+1)*3];
	if (sinfo_data) {
		_build_min_max_16_string(sockets, FORMAT_STRING_SIZE,
				      sinfo_data->min_sockets,
				      sinfo_data->max_sockets, false);
		_build_min_max_16_string(cores, FORMAT_STRING_SIZE,
				      sinfo_data->min_cores,
				      sinfo_data->max_cores, false);
		_build_min_max_16_string(threads, FORMAT_STRING_SIZE,
				      sinfo_data->min_threads,
				      sinfo_data->max_threads, false);
		sct[0] = '\0';
		strcat(sct, sockets);
		strcat(sct, ":");
		strcat(sct, cores);
		strcat(sct, ":");
		strcat(sct, threads);
		_print_str(sct, width, right_justify, true);
	} else {
		_print_str("S:C:T", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_sockets(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_16_string(id, FORMAT_STRING_SIZE,
				      sinfo_data->min_sockets,
				      sinfo_data->max_sockets, false);
		_print_str(id, width, right_justify, true);
	} else {
		_print_str("SOCKETS", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_cores(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_16_string(id, FORMAT_STRING_SIZE,
				      sinfo_data->min_cores,
				      sinfo_data->max_cores, false);
		_print_str(id, width, right_justify, true);
	} else {
		_print_str("CORES", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_threads(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_16_string(id, FORMAT_STRING_SIZE,
				      sinfo_data->min_threads,
				      sinfo_data->max_threads, false);
		_print_str(id, width, right_justify, true);
	} else {
		_print_str("THREADS", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_disk(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_32_string(id, FORMAT_STRING_SIZE,
				      sinfo_data->min_disk,
				      sinfo_data->max_disk,
				      false, false);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("TMP_DISK", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_extra(sinfo_data_t *sinfo_data, int width, bool right_justify,
		 char *suffix)
{
	if (sinfo_data)
		_print_str(sinfo_data->extra, width, right_justify, true);
	else
		_print_str("EXTRA", width, right_justify, true);

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
		_print_str("AVAIL_FEATURES", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_features_act(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data)
		_print_str(sinfo_data->features_act, width, right_justify, true);
	else
		_print_str("ACTIVE_FEATURES", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_gres(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data)
		_print_str(sinfo_data->gres, width, right_justify, true);
	else
		_print_str("GRES", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_gres_used(sinfo_data_t * sinfo_data, int width,
		     bool right_justify, char *suffix)
{
	if (sinfo_data)
		_print_str(sinfo_data->gres_used, width, right_justify, true);
	else
		_print_str("GRES_USED", width, right_justify, true);

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

int _print_alloc_nodes(sinfo_data_t * sinfo_data, int width,
		       bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else if (sinfo_data->part_info->allow_alloc_nodes)
			_print_str(sinfo_data->part_info->allow_alloc_nodes,
				   width, right_justify, true);
		else
			_print_str("all", width, right_justify, true);
	} else
		_print_str("ALLOCNODES", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_memory(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_free_mem_min_max_64(id, FORMAT_STRING_SIZE,
					   sinfo_data->min_mem,
					   sinfo_data->max_mem,
					   false);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("MEMORY", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_node_address(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		char *tmp = NULL;
		tmp = hostlist_ranged_string_xmalloc(
				sinfo_data->node_addr);
		_print_str(tmp, width, right_justify, true);
		xfree(tmp);
	} else {
		char *title = "NODE_ADDR";
		_print_str(title, width, right_justify, false);
	}

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
		char *tmp = NULL;
		tmp = hostlist_ranged_string_xmalloc(
				sinfo_data->nodes);
		_print_str(tmp, width, right_justify, true);
		xfree(tmp);
	} else {
		char *title = "NODELIST";
		_print_str(title, width, right_justify, false);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_node_hostnames(sinfo_data_t * sinfo_data, int width,
			  bool right_justify, char *suffix)
{
	if (params.node_field_flag)
		width = params.node_field_size;

	if (sinfo_data) {
		char *tmp = NULL;
		tmp = hostlist_ranged_string_xmalloc(
				sinfo_data->hostnames);
		_print_str(tmp, width, right_justify, true);
		xfree(tmp);
	} else {
		char *title = "HOSTNAMES";
		_print_str(title, width, right_justify, false);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_nodes_t(sinfo_data_t * sinfo_data, int width,
		   bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		snprintf(id, FORMAT_STRING_SIZE, "%d",
			 sinfo_data->nodes_total);
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
		snprintf(id, FORMAT_STRING_SIZE, "%d/%d",
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
			 sinfo_data->nodes_other, sinfo_data->nodes_total);
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
	if (params.part_field_flag)
		width = params.part_field_size;
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else {
			char *tmp;
			tmp = xstrdup(sinfo_data->part_info->name);
			if (sinfo_data->part_info->flags & PART_FLAG_DEFAULT) {
				if ( (strlen(tmp) < width) || (width == 0) )
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

int _print_partition_name(sinfo_data_t * sinfo_data, int width,
			  bool right_justify, char *suffix)
{
	if (params.part_field_flag)
		width = params.part_field_size;
	if (sinfo_data) {
		if (sinfo_data->part_info == NULL)
			_print_str("n/a", width, right_justify, true);
		else {
			_print_str(sinfo_data->part_info->name, width,
				   right_justify, true);
		}
	} else
		_print_str("PARTITION", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_port(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_16_string(id, FORMAT_STRING_SIZE,
				      sinfo_data->port,
				      sinfo_data->port, false);
		_print_str(id, width, right_justify, true);
	} else {
		_print_str("PORT", width, right_justify, true);
	}

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

int _print_preempt_mode(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		uint16_t preempt_mode = sinfo_data->part_info->preempt_mode;
		if (preempt_mode == NO_VAL16)
			preempt_mode = slurm_conf.preempt_mode;
		_print_str(preempt_mode_string(preempt_mode),
			   width, right_justify, true);
	} else
		_print_str("PREEMPT_MODE", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_priority_job_factor(sinfo_data_t * sinfo_data, int width,
			       bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (sinfo_data) {
		_build_min_max_16_string(id, FORMAT_STRING_SIZE,
				sinfo_data->part_info->priority_job_factor,
				sinfo_data->part_info->priority_job_factor,
				true);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("PRIO_JOB_FACTOR", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_priority_tier(sinfo_data_t * sinfo_data, int width,
			 bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (sinfo_data) {
		_build_min_max_16_string(id, FORMAT_STRING_SIZE,
				sinfo_data->part_info->priority_tier,
				sinfo_data->part_info->priority_tier,
				true);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("PRIO_TIER", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_reason(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		char * reason = sinfo_data->reason ? sinfo_data->reason:"none";
		if (xstrncmp(reason, "(null)", 6) == 0)
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
		else if (sinfo_data->part_info->flags & PART_FLAG_ROOT_ONLY)
			_print_str("yes", width, right_justify, true);
		else
			_print_str("no", width, right_justify, true);
	} else
		_print_str("ROOT", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_oversubscribe(sinfo_data_t * sinfo_data, int width,
			 bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (sinfo_data) {
		bool force = sinfo_data->part_info->max_share & SHARED_FORCE;
		uint16_t val = sinfo_data->part_info->max_share & (~SHARED_FORCE);
		if (val == 0)
			snprintf(id, sizeof(id), "EXCLUSIVE");
		else if (force)
			snprintf(id, sizeof(id), "FORCE:%u", val);
		else if (val == 1)
			snprintf(id, sizeof(id), "NO");
		else
			snprintf(id, sizeof(id), "YES:%u", val);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("OVERSUBSCRIBE", width, right_justify, true);

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
			_build_min_max_32_string(id, FORMAT_STRING_SIZE,
					      sinfo_data->part_info->min_nodes,
					      sinfo_data->part_info->max_nodes,
					      true, true);
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
	char *upper_state, *lower_state;
	uint32_t my_state;

	if (sinfo_data && sinfo_data->nodes_total) {
		my_state = sinfo_data->node_state;
		upper_state = node_state_string_compact(my_state);
		lower_state = _str_tolower(upper_state);
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

int _print_state_complete(sinfo_data_t * sinfo_data, int width,
			  bool right_justify, char *suffix)
{
	if (sinfo_data && sinfo_data->nodes_total) {
		char *state;
		uint32_t my_state;

		my_state = sinfo_data->node_state;
		state = xstrtolower(node_state_string_complete(my_state));
		_print_str(state, width, right_justify, true);
		xfree(state);
	} else if (sinfo_data)
		_print_str("n/a", width, right_justify, true);
	else
		_print_str("STATECOMPLETE", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_state_long(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	char *upper_state, *lower_state;
	uint32_t my_state;

	if (sinfo_data && sinfo_data->nodes_total) {
		my_state = sinfo_data->node_state;
		upper_state = node_state_string(my_state);
		lower_state = _str_tolower(upper_state);
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

int _print_timestamp(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data && sinfo_data->reason_time) {
		char time_str[32];
		slurm_make_time_str(&sinfo_data->reason_time,
				    time_str, sizeof(time_str));
		_print_str(time_str, width, right_justify, true);
	} else if (sinfo_data)
		_print_str("Unknown", width, right_justify, true);
	else
		_print_str("TIMESTAMP", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_user(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data && (sinfo_data->reason_uid != NO_VAL)) {
		char user[FORMAT_STRING_SIZE];
		struct passwd *pw = NULL;

		if ((pw=getpwuid(sinfo_data->reason_uid)))
			snprintf(user, sizeof(user), "%s", pw->pw_name);
		else
			snprintf(user, sizeof(user), "Unk(%u)",
				 sinfo_data->reason_uid);
		_print_str(user, width, right_justify, true);
	} else if (sinfo_data)
		_print_str("Unknown", width, right_justify, true);
	else
		_print_str("USER", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_user_long(sinfo_data_t * sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data && (sinfo_data->reason_uid != NO_VAL)) {
		char user[FORMAT_STRING_SIZE];
		struct passwd *pw = NULL;

		if ((pw=getpwuid(sinfo_data->reason_uid)))
			snprintf(user, sizeof(user), "%s(%u)", pw->pw_name,
				 sinfo_data->reason_uid);
		else
			snprintf(user, sizeof(user), "Unk(%u)",
				 sinfo_data->reason_uid);
		_print_str(user, width, right_justify, true);
	} else if (sinfo_data)
		_print_str("Unknown", width, right_justify, true);
	else
		_print_str("USER", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_default_time(sinfo_data_t * sinfo_data, int width,
				bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if ((sinfo_data->part_info == NULL) ||
		    (sinfo_data->part_info->default_time == NO_VAL))
			_print_str("n/a", width, right_justify, true);
		else if (sinfo_data->part_info->default_time == INFINITE)
			_print_str("infinite", width, right_justify, true);
		else
			_print_secs((sinfo_data->part_info->default_time * 60L),
				    width, right_justify, true);
	} else
		_print_str("DEFAULTTIME", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_weight(sinfo_data_t * sinfo_data, int width,
		  bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];
	if (sinfo_data) {
		_build_min_max_32_string(id, FORMAT_STRING_SIZE,
					 sinfo_data->min_weight,
					 sinfo_data->max_weight,
					 false, false);
		_print_str(id, width, right_justify, true);
	} else
		_print_str("WEIGHT", width, right_justify, true);

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_com_invalid(sinfo_data_t * sinfo_data, int width,
		       bool right_justify, char *suffix)
{
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_cpu_load(sinfo_data_t * sinfo_data, int width,
		    bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (sinfo_data) {
		_build_cpu_load_min_max_32(id, FORMAT_STRING_SIZE,
					 sinfo_data->min_cpu_load,
					 sinfo_data->max_cpu_load,
					 true);
		_print_str(id, width, right_justify, true);
	} else {
		_print_str("CPU_LOAD", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_free_mem(sinfo_data_t * sinfo_data, int width,
		    bool right_justify, char *suffix)
{
	char id[FORMAT_STRING_SIZE];

	if (sinfo_data) {
		_build_free_mem_min_max_64(id, FORMAT_STRING_SIZE,
					   sinfo_data->min_free_mem,
					   sinfo_data->max_free_mem,
					   true);
		_print_str(id, width, right_justify, true);
	} else {
		_print_str("FREE_MEM", width, right_justify, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_max_cpus_per_node(sinfo_data_t * sinfo_data, int width,
			     bool right_justify, char *suffix)
{
	char tmp_line[32];
	if (sinfo_data) {
		if (sinfo_data->part_info->max_cpus_per_node == INFINITE)
			sprintf(tmp_line, "UNLIMITED");
		else
			sprintf(tmp_line, "%u", sinfo_data->max_cpus_per_node);
		_print_str(tmp_line, width, right_justify, true);

	} else {
		_print_str("MAX_CPUS_PER_NODE", width, right_justify, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_version(sinfo_data_t * sinfo_data, int width,
		   bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->version == NULL) {
			_print_str("N/A", width, right_justify, true);
		} else {
			_print_str(sinfo_data->version, width,
				   right_justify, true);
		}
	} else {
		_print_str("VERSION", width, right_justify, true);
	}
	if (suffix) {
		printf ("%s", suffix);
	}
	return SLURM_SUCCESS;

}

int _print_alloc_mem(sinfo_data_t * sinfo_data, int width,
		     bool right_justify, char *suffix)
{
	char tmp_line[32];
	if (sinfo_data) {
		sprintf(tmp_line, "%"PRIu64"", sinfo_data->alloc_memory);
		_print_str(tmp_line, width, right_justify, true);
	} else {
		_print_str("ALLOCMEM", width, right_justify, true);
	}
	if (suffix) {
		printf ("%s", suffix);
	}
	return SLURM_SUCCESS;
}


int _print_cluster_name(sinfo_data_t *sinfo_data, int width,
			bool right_justify, char *suffix)
{
	if (sinfo_data) {
		if (sinfo_data->cluster_name == NULL) {
			_print_str("N/A", width, right_justify, true);
		} else {
			_print_str(sinfo_data->cluster_name, width,
				   right_justify, true);
		}
	} else {
		_print_str("CLUSTER", width, right_justify, true);
	}
	if (suffix) {
		printf ("%s", suffix);
	}
	return SLURM_SUCCESS;

}
