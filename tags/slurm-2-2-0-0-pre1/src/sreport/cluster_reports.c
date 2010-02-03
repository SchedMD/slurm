/*****************************************************************************\
 *  cluster_reports.c - functions for generating cluster reports
 *                       from accounting infrastructure.
 *****************************************************************************
 *
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "cluster_reports.h"
bool tree_display = 0;

enum {
	PRINT_CLUSTER_NAME,
	PRINT_CLUSTER_CPUS,
	PRINT_CLUSTER_ACPU,
	PRINT_CLUSTER_DCPU,
	PRINT_CLUSTER_ICPU,
	PRINT_CLUSTER_PDCPU,
	PRINT_CLUSTER_OCPU,
	PRINT_CLUSTER_RCPU,
	PRINT_CLUSTER_TOTAL,
	PRINT_CLUSTER_ACCT,
	PRINT_CLUSTER_USER_LOGIN,
	PRINT_CLUSTER_USER_PROPER,
	PRINT_CLUSTER_AMOUNT_USED,
	PRINT_CLUSTER_WCKEY,
};

typedef enum {
	GROUP_BY_ACCOUNT,
	GROUP_BY_ACCOUNT_JOB_SIZE,
	GROUP_BY_ACCOUNT_JOB_SIZE_DURATION,
	GROUP_BY_USER,
	GROUP_BY_USER_JOB_SIZE,
	GROUP_BY_USER_JOB_SIZE_DURATION,
	GROUP_BY_NONE
} report_grouping_t;

static List print_fields_list = NULL; /* types are of print_field_t */

static int _set_wckey_cond(int *start, int argc, char *argv[],
			   acct_wckey_cond_t *wckey_cond,
			   List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int option = 0;

	if(!wckey_cond) {
		error("No wckey_cond given");
		return -1;
	}

	wckey_cond->with_usage = 1;
	wckey_cond->with_deleted = 1;

	if(!wckey_cond->cluster_list)
		wckey_cond->cluster_list = list_create(slurm_destroy_char);

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if(!end && !strncasecmp(argv[i], "all_clusters",
					       MAX(command_len, 1))) {
			local_cluster_flag = 1;
		} else if(!end && !strncasecmp(argv[i], "withdeleted",
					  MAX(command_len, 5))) {
			wckey_cond->with_deleted = 1;
			set = 1;
		} else if(!end
			  || !strncasecmp (argv[i], "WCKeys",
					   MAX(command_len, 3))) {
			if(!wckey_cond->name_list)
				wckey_cond->name_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(wckey_cond->name_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Clusters",
					 MAX(command_len, 3))) {
			if(!wckey_cond->cluster_list)
				wckey_cond->cluster_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(wckey_cond->cluster_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "End", MAX(command_len, 1))) {
			wckey_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Start",
					 MAX(command_len, 1))) {
			wckey_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "User",
					 MAX(command_len, 1))) {
			if(!wckey_cond->user_list)
				wckey_cond->user_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(wckey_cond->user_list,
						 argv[i]+end))
				set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}

	(*start) = i;

	if(!local_cluster_flag && !list_count(wckey_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if(temp)
			list_append(wckey_cond->cluster_list, temp);
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = wckey_cond->usage_start;
	end_time = wckey_cond->usage_end;
	set_start_end_time(&start_time, &end_time);
	wckey_cond->usage_start = start_time;
	wckey_cond->usage_end = end_time;

	return set;
}

static int _set_assoc_cond(int *start, int argc, char *argv[],
			   acct_association_cond_t *assoc_cond,
			   List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int command_len = 0;
	int option = 0;

	if(!assoc_cond) {
		error("We need an acct_association_cond to call this");
		return SLURM_ERROR;
	}

	assoc_cond->with_usage = 1;
	assoc_cond->with_deleted = 1;

	if(!assoc_cond->cluster_list)
		assoc_cond->cluster_list = list_create(slurm_destroy_char);
	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if(!end && !strncasecmp(argv[i], "all_clusters",
					       MAX(command_len, 1))) {
			local_cluster_flag = 1;
		} else if (!end && !strncasecmp (argv[i], "Tree",
						 MAX(command_len, 4))) {
			tree_display = 1;
		} else if(!end
			  || !strncasecmp (argv[i], "Users",
					   MAX(command_len, 1))) {
			if(!assoc_cond->user_list)
				assoc_cond->user_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->user_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Accounts",
					 MAX(command_len, 2))) {
			if(!assoc_cond->acct_list)
				assoc_cond->acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Clusters",
					 MAX(command_len, 1))) {
			slurm_addto_char_list(assoc_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "End", MAX(command_len, 1))) {
			assoc_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if(format_list)
				slurm_addto_char_list(format_list,
						      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Start",
					 MAX(command_len, 1))) {
			assoc_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if(!local_cluster_flag && !list_count(assoc_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if(temp)
			list_append(assoc_cond->cluster_list, temp);
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = assoc_cond->usage_start;
	end_time = assoc_cond->usage_end;
	set_start_end_time(&start_time, &end_time);
	assoc_cond->usage_start = start_time;
	assoc_cond->usage_end = end_time;

	return set;
}

static int _set_cluster_cond(int *start, int argc, char *argv[],
			     acct_cluster_cond_t *cluster_cond,
			     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int command_len = 0;
	int option = 0;

	if(!cluster_cond) {
		error("We need an acct_cluster_cond to call this");
		return SLURM_ERROR;
	}

	cluster_cond->with_deleted = 1;
	cluster_cond->with_usage = 1;

	if(!cluster_cond->cluster_list)
		cluster_cond->cluster_list = list_create(slurm_destroy_char);
	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if(!end && !strncasecmp(argv[i], "all_clusters",
					       MAX(command_len, 1))) {
			local_cluster_flag = 1;
		} else if(!end
			  || !strncasecmp (argv[i], "Clusters",
					   MAX(command_len, 1))) {
			slurm_addto_char_list(cluster_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "End", MAX(command_len, 1))) {
			cluster_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if(format_list)
				slurm_addto_char_list(format_list,
						      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Start",
					 MAX(command_len, 1))) {
			cluster_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else {
			exit_code=1;
			fprintf(stderr," Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if(!local_cluster_flag && !list_count(cluster_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if(temp)
			list_append(cluster_cond->cluster_list, temp);
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = cluster_cond->usage_start;
	end_time = cluster_cond->usage_end;
	set_start_end_time(&start_time, &end_time);
	cluster_cond->usage_start = start_time;
	cluster_cond->usage_end = end_time;

	return set;
}

static int _setup_print_fields_list(List format_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;

	if(!format_list || !list_count(format_list)) {
		exit_code=1;
			fprintf(stderr, " we need a format list "
				"to set up the print.\n");
		return SLURM_ERROR;
	}

	if(!print_fields_list)
		print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		int command_len = 0;
		int newlen = 0;

		if((tmp_char = strstr(object, "\%"))) {
			newlen = atoi(tmp_char+1);
			tmp_char[0] = '\0';
		}

		command_len = strlen(object);

		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Accounts", object, MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_ACCT;
			field->name = xstrdup("Account");
			if(tree_display)
				field->len = -20;
			else
				field->len = 15;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("allocated", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_ACPU;
			field->name = xstrdup("Allocated");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Cluster", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_NAME;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("cpucount", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_CPUS;
			field->name = xstrdup("CPU count");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("down", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_DCPU;
			field->name = xstrdup("Down");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("idle", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_ICPU;
			field->name = xstrdup("Idle");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Login", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_USER_LOGIN;
			field->name = xstrdup("Login");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("overcommited", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_OCPU;
			field->name = xstrdup("Over Comm");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 9;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("PlannedDown", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_PDCPU;
			field->name = xstrdup("PLND Down");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Proper", object, MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_USER_PROPER;
			field->name = xstrdup("Proper Name");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("reported", object,
				       MAX(command_len, 3))) {
			field->type = PRINT_CLUSTER_TOTAL;
			field->name = xstrdup("Reported");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("reserved", object,
				       MAX(command_len, 3))) {
			field->type = PRINT_CLUSTER_RCPU;
			field->name = xstrdup("Reserved");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 9;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Used", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_AMOUNT_USED;
			field->name = xstrdup("Used");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("WCKey", object, MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_WCKEY;
			field->name = xstrdup("WCKey");
			if(tree_display)
				field->len = 20;
			else
				field->len = 15;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}

		if(newlen)
			field->len = newlen;

		list_append(print_fields_list, field);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static List _get_cluster_list(int argc, char *argv[], uint32_t *total_time,
			      char *report_name, List format_list)
{
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	int i=0;
	List cluster_list = NULL;

	cluster_cond->with_deleted = 1;
	cluster_cond->with_usage = 1;

	_set_cluster_cond(&i, argc, argv, cluster_cond, format_list);

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid,
						   cluster_cond);
	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		return NULL;
	}

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = cluster_cond->usage_start;
		time_t my_end = cluster_cond->usage_end-1;

		slurm_make_time_str(&my_start,
				    start_char, sizeof(start_char));
		slurm_make_time_str(&my_end,
				    end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("%s %s - %s (%d*cpus secs)\n",
		       report_name, start_char, end_char,
		       (int)(cluster_cond->usage_end
			     - cluster_cond->usage_start));
		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break;
		default:
			printf("Time reported in CPU %s\n", time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}
	(*total_time) = cluster_cond->usage_end - cluster_cond->usage_start;

	destroy_acct_cluster_cond(cluster_cond);

	return cluster_list;
}

extern int cluster_account_by_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	acct_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator assoc_itr = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List assoc_list = NULL;
	List first_list = NULL;
	List cluster_list = NULL;
	List sreport_cluster_list = list_create(destroy_sreport_cluster_rec);
	List tree_list = NULL;
	int i=0;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;
	sreport_assoc_rec_t *sreport_assoc = NULL;
	sreport_cluster_rec_t *sreport_cluster = NULL;
	print_field_t *field = NULL;
	int field_count = 0;
	char *print_acct = NULL;

	print_fields_list = list_create(destroy_print_field);

	memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));

	assoc_cond->with_sub_accts = 1;

	_set_assoc_cond(&i, argc, argv, assoc_cond, format_list);

	if(!list_count(format_list))
		slurm_addto_char_list(format_list,
				      "Cluster,Ac,Login,Proper,Used");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	cluster_cond.with_deleted = 1;
	cluster_cond.with_usage = 1;
	cluster_cond.usage_end = assoc_cond->usage_end;
	cluster_cond.usage_start = assoc_cond->usage_start;
	cluster_cond.cluster_list = assoc_cond->cluster_list;
	cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		goto end_it;
	}
	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     assoc_cond);
	if(!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem with assoc query.\n");
		goto end_it;
	}

	first_list = assoc_list;
	assoc_list = get_hierarchical_sorted_assoc_list(first_list);

	/* set up the structures for easy retrieval later */
	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;

		/* check to see if this cluster is around during the
		   time we are looking at */
		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		sreport_cluster = xmalloc(sizeof(sreport_cluster_rec_t));

		list_append(sreport_cluster_list, sreport_cluster);

		sreport_cluster->name = xstrdup(cluster->name);
		sreport_cluster->assoc_list =
			list_create(destroy_sreport_assoc_rec);

		/* get the amount of time and the average cpu count
		   during the time we are looking at */
		itr2 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr2))) {
			sreport_cluster->cpu_secs += accting->alloc_secs
				+ accting->down_secs + accting->idle_secs
				+ accting->resv_secs;
			sreport_cluster->cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr2);

		sreport_cluster->cpu_count /=
			list_count(cluster->accounting_list);

		/* now add the associations of interest here by user */
		while((assoc = list_next(assoc_itr))) {
			acct_accounting_rec_t *accting2 = NULL;

			if(!assoc->accounting_list
			   || !list_count(assoc->accounting_list)) {
				list_delete_item(assoc_itr);
				continue;
			}

			if(strcmp(cluster->name, assoc->cluster))
				continue;

			sreport_assoc = xmalloc(sizeof(sreport_assoc_rec_t));

			list_append(sreport_cluster->assoc_list,
				    sreport_assoc);

			sreport_assoc->acct = xstrdup(assoc->acct);
			sreport_assoc->parent_acct =
				xstrdup(assoc->parent_acct);
			sreport_assoc->user = xstrdup(assoc->user);

			/* get the amount of time this assoc used
			   during the time we are looking at */
			itr2 = list_iterator_create(assoc->accounting_list);
			while((accting2 = list_next(itr2))) {
				sreport_assoc->cpu_secs +=
					(uint64_t)accting2->alloc_secs;
			}
			list_iterator_destroy(itr2);
			list_delete_item(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(assoc_itr);
	list_iterator_destroy(itr);

	list_destroy(cluster_list);
	cluster_list = NULL;
	list_destroy(assoc_list);
	assoc_list = NULL;
	list_destroy(first_list);
	first_list = NULL;

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = assoc_cond->usage_start;
		time_t my_end = assoc_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/Account/User Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(assoc_cond->usage_end - assoc_cond->usage_start));

		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break;
		default:
			printf("Time reported in CPU %s\n",
			       time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	list_sort(sreport_cluster_list, (ListCmpF)sort_cluster_dec);

	cluster_itr = list_iterator_create(sreport_cluster_list);
	while((sreport_cluster = list_next(cluster_itr))) {
		//list_sort(sreport_cluster->assoc_list,
		//  (ListCmpF)sort_assoc_dec);
		if(tree_list)
			list_flush(tree_list);
		else
			tree_list = list_create(destroy_acct_print_tree);

		itr = list_iterator_create(sreport_cluster->assoc_list);
		while((sreport_assoc = list_next(itr))) {
			int curr_inx = 1;
			if(!sreport_assoc->cpu_secs)
				continue;
			while((field = list_next(itr2))) {
				char *tmp_char = NULL;
				struct passwd *pwd = NULL;
				switch(field->type) {
				case PRINT_CLUSTER_ACCT:
					if(tree_display) {
						char *local_acct = NULL;
						char *parent_acct = NULL;
						if(sreport_assoc->user) {
							local_acct =
								xstrdup_printf(
									"|%s",
									sreport_assoc->acct);
							parent_acct =
								sreport_assoc->acct;
						} else {
							local_acct = xstrdup(
								sreport_assoc->acct);
							parent_acct = sreport_assoc->
								parent_acct;
						}
						print_acct = get_tree_acct_name(
							local_acct,
							parent_acct,
							tree_list);
						xfree(local_acct);
					} else {
						print_acct =
							sreport_assoc->acct;
					}
					field->print_routine(
						field,
						print_acct,
						(curr_inx == field_count));

					break;
				case PRINT_CLUSTER_NAME:
					field->print_routine(
						field,
						sreport_cluster->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_LOGIN:
					field->print_routine(
						field,
						sreport_assoc->user,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_PROPER:
					if(sreport_assoc->user)
						pwd = getpwnam(
							sreport_assoc->user);
					if(pwd) {
						tmp_char =
							strtok(pwd->pw_gecos,
							       ",");
						if(!tmp_char)
							tmp_char =
								pwd->pw_gecos;
					}
					field->print_routine(field,
							     tmp_char,
							     (curr_inx ==
							      field_count));
					break;
				case PRINT_CLUSTER_AMOUNT_USED:
					field->print_routine(
						field,
						sreport_assoc->cpu_secs,
						sreport_cluster->cpu_secs,
						(curr_inx == field_count));
					break;
				default:
					field->print_routine(
						field, NULL,
						(curr_inx == field_count));
					break;
				}
				curr_inx++;
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	destroy_acct_association_cond(assoc_cond);

	if(assoc_list) {
		list_destroy(assoc_list);
		assoc_list = NULL;
	}

	if(first_list) {
		list_destroy(first_list);
		first_list = NULL;
	}

	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}

	if(sreport_cluster_list) {
		list_destroy(sreport_cluster_list);
		sreport_cluster_list = NULL;
	}

	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

extern int cluster_user_by_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	acct_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator assoc_itr = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List assoc_list = NULL;
	List cluster_list = NULL;
	List sreport_cluster_list = list_create(destroy_sreport_cluster_rec);
	int i=0;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;
	sreport_user_rec_t *sreport_user = NULL;
	sreport_cluster_rec_t *sreport_cluster = NULL;
	print_field_t *field = NULL;
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);

	memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));

	_set_assoc_cond(&i, argc, argv, assoc_cond, format_list);

	if(!list_count(format_list))
		slurm_addto_char_list(format_list,
				      "Cluster,Login,Proper,Ac,Used");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	cluster_cond.with_deleted = 1;
	cluster_cond.with_usage = 1;
	cluster_cond.usage_end = assoc_cond->usage_end;
	cluster_cond.usage_start = assoc_cond->usage_start;
	cluster_cond.cluster_list = assoc_cond->cluster_list;
	cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		goto end_it;
	}
	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     assoc_cond);
	if(!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem with assoc query.\n");
		goto end_it;
	}

	/* set up the structures for easy reteval later */
	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;

		/* check to see if this cluster is around during the
		   time we are looking at */
		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		sreport_cluster = xmalloc(sizeof(sreport_cluster_rec_t));

		list_append(sreport_cluster_list, sreport_cluster);

		sreport_cluster->name = xstrdup(cluster->name);
		sreport_cluster->user_list =
			list_create(destroy_sreport_user_rec);

		/* get the amount of time and the average cpu count
		   during the time we are looking at */
		itr2 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr2))) {
			sreport_cluster->cpu_secs += accting->alloc_secs
				+ accting->down_secs + accting->idle_secs
				+ accting->resv_secs;
			sreport_cluster->cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr2);

		sreport_cluster->cpu_count /=
			list_count(cluster->accounting_list);

		/* now add the associations of interest here by user */
		while((assoc = list_next(assoc_itr))) {
			struct passwd *passwd_ptr = NULL;
			uid_t uid = NO_VAL;
			ListIterator user_itr = NULL;
			acct_accounting_rec_t *accting2 = NULL;

			if(!assoc->accounting_list
			   || !list_count(assoc->accounting_list)
			   || !assoc->user) {
				list_delete_item(assoc_itr);
				continue;
			}

			if(strcmp(cluster->name, assoc->cluster))
				continue;

			/* make sure we add all associations to this
			   user rec because we could have some in
			   partitions which would create another
			   record otherwise
			*/
			user_itr = list_iterator_create(
				sreport_cluster->user_list);
			while((sreport_user = list_next(user_itr))) {
				if(!strcmp(sreport_user->name, assoc->user)
				   && !strcmp(sreport_user->acct, assoc->acct))
					break;
			}
			list_iterator_destroy(user_itr);

			if(!sreport_user) {
				passwd_ptr = getpwnam(assoc->user);
				if(passwd_ptr)
					uid = passwd_ptr->pw_uid;
				/* In this report we are using the sreport user
				   structure to store the information we want
				   since it is already available and will do
				   pretty much what we want.
				*/
				sreport_user =
					xmalloc(sizeof(sreport_user_rec_t));
				sreport_user->name = xstrdup(assoc->user);
				sreport_user->uid = uid;
				sreport_user->acct = xstrdup(assoc->acct);

				list_append(sreport_cluster->user_list,
					    sreport_user);
			}

			/* get the amount of time this assoc used
			   during the time we are looking at */
			itr2 = list_iterator_create(assoc->accounting_list);
			while((accting2 = list_next(itr2))) {
				sreport_user->cpu_secs +=
					(uint64_t)accting2->alloc_secs;
			}
			list_iterator_destroy(itr2);
			list_delete_item(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(assoc_itr);
	list_iterator_destroy(itr);

	list_destroy(cluster_list);
	cluster_list = NULL;
	list_destroy(assoc_list);
	assoc_list = NULL;

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = assoc_cond->usage_start;
		time_t my_end = assoc_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/User/Account Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(assoc_cond->usage_end - assoc_cond->usage_start));

		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break;
		default:
			printf("Time reported in CPU %s\n",
			       time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	cluster_itr = list_iterator_create(sreport_cluster_list);
	while((sreport_cluster = list_next(cluster_itr))) {
		list_sort(sreport_cluster->user_list, (ListCmpF)sort_user_dec);

		itr = list_iterator_create(sreport_cluster->user_list);
		while((sreport_user = list_next(itr))) {
			int curr_inx = 1;

			/* we don't care if they didn't use any time */
			if(!sreport_user->cpu_secs)
				continue;

			while((field = list_next(itr2))) {
				char *tmp_char = NULL;
				struct passwd *pwd = NULL;
				switch(field->type) {
				case PRINT_CLUSTER_ACCT:
					field->print_routine(
						field,
						sreport_user->acct,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_NAME:
					field->print_routine(
						field,
						sreport_cluster->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_LOGIN:
					field->print_routine(
						field,
						sreport_user->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_PROPER:
					pwd = getpwnam(sreport_user->name);
					if(pwd) {
						tmp_char =
							strtok(pwd->pw_gecos,
							       ",");
						if(!tmp_char)
							tmp_char =
								pwd->pw_gecos;
					}
					field->print_routine(field,
							     tmp_char,
							     (curr_inx ==
							      field_count));
					break;
				case PRINT_CLUSTER_AMOUNT_USED:
					field->print_routine(
						field,
						sreport_user->cpu_secs,
						sreport_cluster->cpu_secs,
						(curr_inx == field_count));
					break;
				default:
					field->print_routine(
						field, NULL,
						(curr_inx == field_count));
					break;
				}
				curr_inx++;
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	destroy_acct_association_cond(assoc_cond);

	if(assoc_list) {
		list_destroy(assoc_list);
		assoc_list = NULL;
	}

	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}

	if(sreport_cluster_list) {
		list_destroy(sreport_cluster_list);
		sreport_cluster_list = NULL;
	}

	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

extern int cluster_user_by_wckey(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_wckey_cond_t *wckey_cond =
		xmalloc(sizeof(acct_wckey_cond_t));
	acct_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator wckey_itr = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List wckey_list = NULL;
	List cluster_list = NULL;
	List sreport_cluster_list = list_create(destroy_sreport_cluster_rec);
	int i=0;
	acct_cluster_rec_t *cluster = NULL;
	acct_wckey_rec_t *wckey = NULL;
	sreport_user_rec_t *sreport_user = NULL;
	sreport_cluster_rec_t *sreport_cluster = NULL;
	print_field_t *field = NULL;
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);

	memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));

	_set_wckey_cond(&i, argc, argv, wckey_cond, format_list);

	if(!list_count(format_list))
		slurm_addto_char_list(format_list,
				      "Cluster,Login,Proper,WCkey,Used");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	cluster_cond.with_deleted = 1;
	cluster_cond.with_usage = 1;
	cluster_cond.usage_end = wckey_cond->usage_end;
	cluster_cond.usage_start = wckey_cond->usage_start;
	cluster_cond.cluster_list = wckey_cond->cluster_list;
	cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		goto end_it;
	}
	wckey_list = acct_storage_g_get_wckeys(db_conn, my_uid,
						     wckey_cond);
	if(!wckey_list) {
		exit_code=1;
		fprintf(stderr, " Problem with wckey query.\n");
		goto end_it;
	}

	/* set up the structures for easy reteval later */
	itr = list_iterator_create(cluster_list);
	wckey_itr = list_iterator_create(wckey_list);
	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;

		/* check to see if this cluster is around during the
		   time we are looking at */
		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		sreport_cluster = xmalloc(sizeof(sreport_cluster_rec_t));

		list_append(sreport_cluster_list, sreport_cluster);

		sreport_cluster->name = xstrdup(cluster->name);
		sreport_cluster->user_list =
			list_create(destroy_sreport_user_rec);

		/* get the amount of time and the average cpu count
		   during the time we are looking at */
		itr2 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr2))) {
			sreport_cluster->cpu_secs += accting->alloc_secs
				+ accting->down_secs + accting->idle_secs
				+ accting->resv_secs;
			sreport_cluster->cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr2);

		sreport_cluster->cpu_count /=
			list_count(cluster->accounting_list);

		/* now add the wckeys of interest here by user */
		while((wckey = list_next(wckey_itr))) {
			struct passwd *passwd_ptr = NULL;
			uid_t uid = NO_VAL;
			acct_accounting_rec_t *accting2 = NULL;

			if(!wckey->accounting_list
			   || !list_count(wckey->accounting_list)
			   || !wckey->user) {
				list_delete_item(wckey_itr);
				continue;
			}

			if(strcmp(cluster->name, wckey->cluster))
				continue;

			passwd_ptr = getpwnam(wckey->user);
			if(passwd_ptr)
				uid = passwd_ptr->pw_uid;
			/* In this report we are using the sreport user
			   structure to store the information we want
			   since it is already available and will do
			   pretty much what we want.
			*/
			sreport_user =	xmalloc(sizeof(sreport_user_rec_t));
			sreport_user->name = xstrdup(wckey->user);
			sreport_user->uid = uid;
			sreport_user->acct = xstrdup(wckey->name);

			list_append(sreport_cluster->user_list,
				    sreport_user);

			/* get the amount of time this wckey used
			   during the time we are looking at */
			itr2 = list_iterator_create(wckey->accounting_list);
			while((accting2 = list_next(itr2))) {
				sreport_user->cpu_secs +=
					(uint64_t)accting2->alloc_secs;
			}
			list_iterator_destroy(itr2);
			list_delete_item(wckey_itr);
		}
		list_iterator_reset(wckey_itr);
	}
	list_iterator_destroy(wckey_itr);
	list_iterator_destroy(itr);

	list_destroy(cluster_list);
	cluster_list = NULL;
	list_destroy(wckey_list);
	wckey_list = NULL;

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = wckey_cond->usage_start;
		time_t my_end = wckey_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/User/WCKey Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(wckey_cond->usage_end - wckey_cond->usage_start));

		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break;
		default:
			printf("Time reported in CPU %s\n",
			       time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	cluster_itr = list_iterator_create(sreport_cluster_list);
	while((sreport_cluster = list_next(cluster_itr))) {
		list_sort(sreport_cluster->user_list, (ListCmpF)sort_user_dec);

		itr = list_iterator_create(sreport_cluster->user_list);
		while((sreport_user = list_next(itr))) {
			int curr_inx = 1;

			/* we don't care if they didn't use any time */
			if(!sreport_user->cpu_secs)
				continue;

			while((field = list_next(itr2))) {
				char *tmp_char = NULL;
				struct passwd *pwd = NULL;
				switch(field->type) {
				case PRINT_CLUSTER_WCKEY:
					field->print_routine(
						field,
						sreport_user->acct,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_NAME:
					field->print_routine(
						field,
						sreport_cluster->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_LOGIN:
					field->print_routine(
						field,
						sreport_user->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_PROPER:
					pwd = getpwnam(sreport_user->name);
					if(pwd) {
						tmp_char =
							strtok(pwd->pw_gecos,
							       ",");
						if(!tmp_char)
							tmp_char =
								pwd->pw_gecos;
					}
					field->print_routine(field,
							     tmp_char,
							     (curr_inx ==
							      field_count));
					break;
				case PRINT_CLUSTER_AMOUNT_USED:
					field->print_routine(
						field,
						sreport_user->cpu_secs,
						sreport_cluster->cpu_secs,
						(curr_inx == field_count));
					break;
				default:
					field->print_routine(
						field, NULL,
						(curr_inx == field_count));
					break;
				}
				curr_inx++;
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	destroy_acct_wckey_cond(wckey_cond);

	if(wckey_list) {
		list_destroy(wckey_list);
		wckey_list = NULL;
	}

	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}

	if(sreport_cluster_list) {
		list_destroy(sreport_cluster_list);
		sreport_cluster_list = NULL;
	}

	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

extern int cluster_utilization(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	acct_cluster_rec_t *cluster = NULL;

	print_field_t *field = NULL;
	uint32_t total_time = 0;

	List cluster_list = NULL;

	List format_list = list_create(slurm_destroy_char);
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);


	if(!(cluster_list = _get_cluster_list(argc, argv, &total_time,
					      "Cluster Utilization",
					      format_list)))
		goto end_it;

	if(!list_count(format_list))
		slurm_addto_char_list(format_list, "Cl,al,d,planned,i,res,rep");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	itr = list_iterator_create(cluster_list);
	itr2 = list_iterator_create(print_fields_list);

	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;
		cluster_accounting_rec_t total_acct;
		uint64_t total_reported = 0;
		uint64_t local_total_time = 0;
		int curr_inx = 1;

		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		memset(&total_acct, 0, sizeof(cluster_accounting_rec_t));

		itr3 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr3))) {
			total_acct.alloc_secs += accting->alloc_secs;
			total_acct.down_secs += accting->down_secs;
			total_acct.pdown_secs += accting->pdown_secs;
			total_acct.idle_secs += accting->idle_secs;
			total_acct.resv_secs += accting->resv_secs;
			total_acct.over_secs += accting->over_secs;
			total_acct.cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr3);

		total_acct.cpu_count /= list_count(cluster->accounting_list);

		local_total_time =
			(uint64_t)total_time * (uint64_t)total_acct.cpu_count;
		total_reported = total_acct.alloc_secs + total_acct.down_secs
			+ total_acct.pdown_secs + total_acct.idle_secs
			+ total_acct.resv_secs;

		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_CLUSTER_NAME:
				field->print_routine(field,
						     cluster->name,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_CPUS:
				field->print_routine(field,
						     total_acct.cpu_count,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_ACPU:
				field->print_routine(field,
						     total_acct.alloc_secs,
						     total_reported,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_DCPU:
				field->print_routine(field,
						     total_acct.down_secs,
						     total_reported,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_ICPU:
				field->print_routine(field,
						     total_acct.idle_secs,
						     total_reported,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_RCPU:
				field->print_routine(field,
						     total_acct.resv_secs,
						     total_reported,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_OCPU:
					field->print_routine(field,
						     total_acct.over_secs,
						     total_reported,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_PDCPU:
					field->print_routine(field,
						     total_acct.pdown_secs,
						     total_reported,
						     (curr_inx ==
						      field_count));
				break;
			case PRINT_CLUSTER_TOTAL:
				field->print_routine(field,
						     total_reported,
						     local_total_time,
						     (curr_inx ==
						      field_count));
				break;
			default:
				field->print_routine(
					field, NULL,
					(curr_inx == field_count));
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);

end_it:
	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}

	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

extern int cluster_wckey_by_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_wckey_cond_t *wckey_cond =
		xmalloc(sizeof(acct_wckey_cond_t));
	acct_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator wckey_itr = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List wckey_list = NULL;
	List cluster_list = NULL;
	List sreport_cluster_list = list_create(destroy_sreport_cluster_rec);
	List tree_list = NULL;
	int i=0;
	acct_cluster_rec_t *cluster = NULL;
	acct_wckey_rec_t *wckey = NULL;
	sreport_assoc_rec_t *sreport_assoc = NULL;
	sreport_cluster_rec_t *sreport_cluster = NULL;
	print_field_t *field = NULL;
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);

	memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));

	_set_wckey_cond(&i, argc, argv, wckey_cond, format_list);

	if(!list_count(format_list))
		slurm_addto_char_list(format_list,
				      "Cluster,WCKey,Login,Proper,Used");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	cluster_cond.with_deleted = 1;
	cluster_cond.with_usage = 1;
	cluster_cond.usage_end = wckey_cond->usage_end;
	cluster_cond.usage_start = wckey_cond->usage_start;
	cluster_cond.cluster_list = wckey_cond->cluster_list;
	cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		goto end_it;
	}
	wckey_list = acct_storage_g_get_wckeys(db_conn, my_uid, wckey_cond);
	if(!wckey_list) {
		exit_code=1;
		fprintf(stderr, " Problem with wckey query.\n");
		goto end_it;
	}

	/* set up the structures for easy retrieval later */
	itr = list_iterator_create(cluster_list);
	wckey_itr = list_iterator_create(wckey_list);
	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;

		/* check to see if this cluster is around during the
		   time we are looking at */
		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		sreport_cluster = xmalloc(sizeof(sreport_cluster_rec_t));

		list_append(sreport_cluster_list, sreport_cluster);

		sreport_cluster->name = xstrdup(cluster->name);
		sreport_cluster->assoc_list =
			list_create(destroy_sreport_assoc_rec);

		/* get the amount of time and the average cpu count
		   during the time we are looking at */
		itr2 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr2))) {
			sreport_cluster->cpu_secs += accting->alloc_secs
				+ accting->down_secs + accting->idle_secs
				+ accting->resv_secs;
			sreport_cluster->cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr2);

		sreport_cluster->cpu_count /=
			list_count(cluster->accounting_list);

		/* now add the wckeys of interest here by user */
		while((wckey = list_next(wckey_itr))) {
			acct_accounting_rec_t *accting2 = NULL;
			sreport_assoc_rec_t *parent_assoc = NULL;
			ListIterator par_itr = NULL;

			if(!wckey->accounting_list
			   || !list_count(wckey->accounting_list)) {
				list_delete_item(wckey_itr);
				continue;
			}

			if(strcmp(cluster->name, wckey->cluster))
				continue;

			/* find the parent */
			par_itr = list_iterator_create(
				sreport_cluster->assoc_list);
			while((parent_assoc = list_next(par_itr))) {
				if(!parent_assoc->user
				   && !strcmp(parent_assoc->acct, wckey->name))
					break;
			}
			list_iterator_destroy(par_itr);
			if(!parent_assoc) {
				parent_assoc = xmalloc(
					sizeof(sreport_assoc_rec_t));

				list_append(sreport_cluster->assoc_list,
					    parent_assoc);
				parent_assoc->acct = xstrdup(wckey->name);
			}

			sreport_assoc = xmalloc(sizeof(sreport_assoc_rec_t));

			list_append(sreport_cluster->assoc_list,
				    sreport_assoc);

			sreport_assoc->acct = xstrdup(wckey->name);
			sreport_assoc->user = xstrdup(wckey->user);

			/* get the amount of time this wckey used
			   during the time we are looking at */
			itr2 = list_iterator_create(wckey->accounting_list);
			while((accting2 = list_next(itr2))) {
				sreport_assoc->cpu_secs +=
					(uint64_t)accting2->alloc_secs;
				parent_assoc->cpu_secs +=
					(uint64_t)accting2->alloc_secs;
			}
			list_iterator_destroy(itr2);
			list_delete_item(wckey_itr);
		}
		list_iterator_reset(wckey_itr);
	}
	list_iterator_destroy(wckey_itr);
	list_iterator_destroy(itr);

	list_destroy(cluster_list);
	cluster_list = NULL;
	list_destroy(wckey_list);
	wckey_list = NULL;

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = wckey_cond->usage_start;
		time_t my_end = wckey_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/WCKey/User Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(wckey_cond->usage_end - wckey_cond->usage_start));

		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break;
		default:
			printf("Time reported in CPU %s\n",
			       time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	list_sort(sreport_cluster_list, (ListCmpF)sort_cluster_dec);

	cluster_itr = list_iterator_create(sreport_cluster_list);
	while((sreport_cluster = list_next(cluster_itr))) {
		//list_sort(sreport_cluster->wckey_list,
		//  (ListCmpF)sort_wckey_dec);
		if(tree_list)
			list_flush(tree_list);
		else
			tree_list = list_create(destroy_acct_print_tree);

		itr = list_iterator_create(sreport_cluster->assoc_list);
		while((sreport_assoc = list_next(itr))) {
			int curr_inx = 1;
			if(!sreport_assoc->cpu_secs)
				continue;
			while((field = list_next(itr2))) {
				char *tmp_char = NULL;
				struct passwd *pwd = NULL;
				switch(field->type) {
				case PRINT_CLUSTER_WCKEY:
					field->print_routine(
						field,
						sreport_assoc->acct,
						(curr_inx == field_count));

					break;
				case PRINT_CLUSTER_NAME:
					field->print_routine(
						field,
						sreport_cluster->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_LOGIN:
					field->print_routine(
						field,
						sreport_assoc->user,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_PROPER:
					if(sreport_assoc->user)
						pwd = getpwnam(
							sreport_assoc->user);
					if(pwd) {
						tmp_char =
							strtok(pwd->pw_gecos,
							       ",");
						if(!tmp_char)
							tmp_char =
								pwd->pw_gecos;
					}
					field->print_routine(field,
							     tmp_char,
							     (curr_inx ==
							      field_count));
					break;
				case PRINT_CLUSTER_AMOUNT_USED:
					field->print_routine(
						field,
						sreport_assoc->cpu_secs,
						sreport_cluster->cpu_secs,
						(curr_inx == field_count));
					break;
				default:
					field->print_routine(
						field, NULL,
						(curr_inx == field_count));
					break;
				}
				curr_inx++;
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	destroy_acct_wckey_cond(wckey_cond);

	if(wckey_list) {
		list_destroy(wckey_list);
		wckey_list = NULL;
	}

	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}

	if(sreport_cluster_list) {
		list_destroy(sreport_cluster_list);
		sreport_cluster_list = NULL;
	}

	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

