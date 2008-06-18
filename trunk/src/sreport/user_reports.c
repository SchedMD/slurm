/*****************************************************************************\
 *  user_reports.c - functions for generating user reports
 *                     from accounting infrastructure.
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

#include "user_reports.h"

static bool group_accts = false;

typedef struct {
	char *name;
	List accts; /* list of char *'s */
	uint64_t cpu_secs;
} local_user_rec_t;

typedef struct {
	char *name;
	List user_list;
} local_cluster_rec_t;

static int _set_cond(int *start, int argc, char *argv[],
		     acct_user_cond_t *user_cond, List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	acct_association_cond_t *assoc_cond = NULL;
	
	if(!user_cond) {
		error("We need an acct_user_cond to call this");
		return SLURM_ERROR;
	}

	if(!user_cond->user_list)
		user_cond->user_list = list_create(slurm_destroy_char);

	user_cond->with_deleted = 1;
	user_cond->with_deleted = 1;
	if(!user_cond->assoc_cond) {
		user_cond->assoc_cond = 
			xmalloc(sizeof(acct_association_cond_t));
		user_cond->assoc_cond->with_usage = 1;
	}
	assoc_cond = user_cond->assoc_cond;
	if(!assoc_cond->acct_list)
		assoc_cond->acct_list = list_create(slurm_destroy_char);
	if(!assoc_cond->cluster_list)
		assoc_cond->cluster_list = list_create(slurm_destroy_char);

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} else if(!end && !strncasecmp(argv[i], "all_clusters", 1)) {
			local_cluster_flag = 1;
			continue;
		} else if(!end) {
			addto_char_list(user_cond->user_list, argv[i]);
			set = 1;
		} else if (strncasecmp (argv[i], "Accounts", 2) == 0) {
				addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Clusters", 1) == 0) {
			addto_char_list(assoc_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "End", 1) == 0) {
			assoc_cond->usage_end = parse_time(argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Format", 1) == 0) {
			if(format_list)
				addto_char_list(format_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "group", 1) == 0) {
			group_accts = 1;
		} else if (strncasecmp (argv[i], "Start", 1) == 0) {
			assoc_cond->usage_start = parse_time(argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Users", 1) == 0) {
			addto_char_list(user_cond->user_list,
					argv[i]+end);
			set = 1;
		} else {
			printf(" Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if(!local_cluster_flag && !list_count(assoc_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if(temp)
			list_append(assoc_cond->cluster_list, temp);
	}

	set_start_end_time((time_t *)&assoc_cond->usage_start,
			   (time_t *)&assoc_cond->usage_end);

	return set;
}

static int _setup_print_fields_list(List format_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;

	if(!format_list || !list_count(format_list)) {
		printf(" error: we need a format list to set up the print.\n");
		return SLURM_ERROR;
	}

	if(!print_fields_list)
		print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Cluster", object, 2)) {
			field->type = PRINT_USER_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("login", object, 2)) {
			field->type = PRINT_USER_LOGIN;
			field->name = xstrdup("Login");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("Proper", object, 1)) {
			field->type = PRINT_USER_PROPER;
			field->name = xstrdup("Proper Name");
			field->len = 20;
			field->print_routine = sreport_print_str;
		} else if(!strncasecmp("Used", object, 1)) {
			field->type = PRINT_USER_USED;
			field->name = xstrdup("Used");
			if(time_format == SREPORT_TIME_SECS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Accounts", object, 1)) {
			field->type = PRINT_USER_ICPU;
			field->name = xstrdup("Account(s)");
			field->len = 20;
			field->print_routine = sreport_print_str;
		} else {
			printf("Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern int user_top(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

