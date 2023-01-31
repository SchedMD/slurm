/*****************************************************************************\
 *  cluster_reports.c - functions for generating cluster reports
 *                       from accounting infrastructure.
 *****************************************************************************
 *  Portions Copyright (C) 2010-2017 SchedMD LLC.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "cluster_reports.h"
bool tree_display = 0;

enum {
	PRINT_CLUSTER_NAME,
	PRINT_CLUSTER_TRES_CNT,
	PRINT_CLUSTER_TRES_ALLOC,
	PRINT_CLUSTER_TRES_DOWN,
	PRINT_CLUSTER_TRES_IDLE,
	PRINT_CLUSTER_TRES_PLAN_DOWN,
	PRINT_CLUSTER_TRES_OVER,
	PRINT_CLUSTER_TRES_PLAN,
	PRINT_CLUSTER_TRES_REPORTED,
	PRINT_CLUSTER_ACCT,
	PRINT_CLUSTER_USER_LOGIN,
	PRINT_CLUSTER_USER_PROPER,
	PRINT_CLUSTER_AMOUNT_USED,
	PRINT_CLUSTER_WCKEY,
	PRINT_CLUSTER_ENERGY,
	PRINT_CLUSTER_TRES_NAME,
};

static List print_fields_list = NULL; /* types are of print_field_t */


static int _set_wckey_cond(int *start, int argc, char **argv,
			   slurmdb_wckey_cond_t *wckey_cond,
			   List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;

	if (!wckey_cond) {
		error("No wckey_cond given");
		return -1;
	}

	wckey_cond->with_usage = 1;
	wckey_cond->with_deleted = 1;

	if (!wckey_cond->cluster_list)
		wckey_cond->cluster_list = list_create(xfree_ptr);
	if (cluster_flag)
		slurm_addto_char_list(wckey_cond->cluster_list, cluster_flag);

	for (i = (*start); i < argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len = end - 1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!end && !xstrncasecmp(argv[i], "all_clusters",
					  MAX(command_len, 1))) {
			local_cluster_flag = 1;
		} else if (!end && !xstrncasecmp(argv[i], "withdeleted",
						 MAX(command_len, 5))) {
			wckey_cond->with_deleted = 1;
			set = 1;
		} else if (!end
			  || !xstrncasecmp(argv[i], "WCKeys",
					   MAX(command_len, 3))) {
			if (!wckey_cond->name_list)
				wckey_cond->name_list = list_create(xfree_ptr);
			if (slurm_addto_char_list(wckey_cond->name_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 3))) {
			if (!wckey_cond->cluster_list)
				wckey_cond->cluster_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(wckey_cond->cluster_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 1))) {
			wckey_cond->usage_end = parse_time(argv[i]+end, 1);
			wckey_cond->usage_end = sanity_check_endtime(
				wckey_cond->usage_end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 1))) {
			wckey_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "User",
					 MAX(command_len, 1))) {
			if (!wckey_cond->user_list)
				wckey_cond->user_list = list_create(xfree_ptr);
			if (slurm_addto_char_list_with_case(
				    wckey_cond->user_list,
				    argv[i]+end, user_case_norm))
				set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}

	(*start) = i;

	if (!local_cluster_flag && !list_count(wckey_cond->cluster_list)) {
		/* Get the default Cluster since no cluster is specified */
		list_append(wckey_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	/* This needs to be done on some systems to make sure
	   wckey_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = wckey_cond->usage_start;
	end_time = wckey_cond->usage_end;
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	wckey_cond->usage_start = start_time;
	wckey_cond->usage_end = end_time;

	return set;
}

static int _set_assoc_cond(int *start, int argc, char **argv,
			   slurmdb_assoc_cond_t *assoc_cond,
			   List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int command_len = 0;

	if (!assoc_cond) {
		error("We need an slurmdb_assoc_cond to call this");
		return SLURM_ERROR;
	}

	assoc_cond->with_usage = 1;
	assoc_cond->with_deleted = 1;

	if (!assoc_cond->cluster_list)
		assoc_cond->cluster_list = list_create(xfree_ptr);
	if (cluster_flag)
		slurm_addto_char_list(assoc_cond->cluster_list, cluster_flag);

	for (i = (*start); i < argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len = strlen(argv[i]);
		else {
			command_len = end - 1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!end && !xstrncasecmp(argv[i], "all_clusters",
					       MAX(command_len, 1))) {
			local_cluster_flag = 1;
		} else if (!end && !xstrncasecmp(argv[i], "Tree",
						 MAX(command_len, 4))) {
			tree_display = 1;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Users",
					   MAX(command_len, 1))) {
			if (!assoc_cond->user_list)
				assoc_cond->user_list = list_create(xfree_ptr);
			slurm_addto_char_list_with_case(assoc_cond->user_list,
							argv[i]+end,
							user_case_norm);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Accounts",
					 MAX(command_len, 2))
			   || !xstrncasecmp(argv[i], "Acct",
					   MAX(command_len, 4))) {
			if (!assoc_cond->acct_list)
				assoc_cond->acct_list = list_create(xfree_ptr);
			slurm_addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 1))) {
			slurm_addto_char_list(assoc_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 1))) {
			assoc_cond->usage_end = parse_time(argv[i]+end, 1);
			assoc_cond->usage_end = sanity_check_endtime(assoc_cond->usage_end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list,
						      argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 1))) {
			assoc_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if (!local_cluster_flag && !list_count(assoc_cond->cluster_list)) {
		/* Get the default Cluster since no cluster is specified */
		list_append(assoc_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	/* This needs to be done on some systems to make sure
	   assoc_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = assoc_cond->usage_start;
	end_time = assoc_cond->usage_end;
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	assoc_cond->usage_start = start_time;
	assoc_cond->usage_end = end_time;
	return set;
}

static int _set_cluster_cond(int *start, int argc, char **argv,
			     slurmdb_cluster_cond_t *cluster_cond,
			     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int command_len = 0;

	if (!cluster_cond) {
		error("We need an slurmdb_cluster_cond to call this");
		return SLURM_ERROR;
	}

	cluster_cond->with_deleted = 1;
	cluster_cond->with_usage = 1;

	if (!cluster_cond->cluster_list)
		cluster_cond->cluster_list = list_create(xfree_ptr);
	if (cluster_flag)
		slurm_addto_char_list(cluster_cond->cluster_list, cluster_flag);

	for (i = (*start); i < argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len = end - 1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!end && !xstrncasecmp(argv[i], "all_clusters",
					       MAX(command_len, 1))) {
			local_cluster_flag = 1;
		} else if (!end
			   || !xstrncasecmp(argv[i], "Clusters",
					    MAX(command_len, 1))) {
			slurm_addto_char_list(cluster_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 1))) {
			cluster_cond->usage_end = parse_time(argv[i]+end, 1);
			cluster_cond->usage_end = sanity_check_endtime(cluster_cond->usage_end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list,
						      argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 1))) {
			cluster_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr," Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if (!local_cluster_flag && !list_count(cluster_cond->cluster_list)) {
		/* Get the default Cluster since no cluster is specified */
		list_append(cluster_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = cluster_cond->usage_start;
	end_time = cluster_cond->usage_end;
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	cluster_cond->usage_start = start_time;
	cluster_cond->usage_end = end_time;

	return set;
}

static int _setup_print_fields_list(List format_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;

	if (!format_list || !list_count(format_list)) {
		exit_code = 1;
			fprintf(stderr, " we need a format list "
				"to set up the print.\n");
		return SLURM_ERROR;
	}

	if (!print_fields_list)
		print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while ((object = list_next(itr))) {
		char *tmp_char = NULL;
		int command_len = 0;
		int newlen = 0;

		if ((tmp_char = strstr(object, "\%"))) {
			newlen = atoi(tmp_char+1);
			tmp_char[0] = '\0';
		}

		command_len = strlen(object);

		field = xmalloc(sizeof(print_field_t));
		if (!xstrncasecmp("Accounts", object, MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_ACCT;
			field->name = xstrdup("Account");
			if (tree_display)
				field->len = -20;
			else
				field->len = 15;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("allocated", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_TRES_ALLOC;
			field->name = xstrdup("Allocated");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Cluster", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_NAME;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("down", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_TRES_DOWN;
			field->name = xstrdup("Down");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("idle", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_TRES_IDLE;
			field->name = xstrdup("Idle");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Login", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_USER_LOGIN;
			field->name = xstrdup("Login");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("overcommitted", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_TRES_OVER;
			field->name = xstrdup("Over Comm");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("PlannedDown", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_TRES_PLAN_DOWN;
			field->name = xstrdup("PLND Down");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Proper", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_USER_PROPER;
			field->name = xstrdup("Proper Name");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("reported", object,
					 MAX(command_len, 3))) {
			field->type = PRINT_CLUSTER_TRES_REPORTED;
			field->name = xstrdup("Reported");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("reserved", object,
					 MAX(command_len, 3)) ||
			   !xstrncasecmp("planned", object,
					 MAX(command_len, 4))) {
			field->type = PRINT_CLUSTER_TRES_PLAN;
			field->name = xstrdup("Planned");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("TresCount", object,
					 MAX(command_len, 5)) ||
			   !xstrncasecmp("cpucount", object,
					 MAX(command_len, 2)) ||
			   !xstrncasecmp("count", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_TRES_CNT;
			field->name = xstrdup("TRES Count");
			field->len = 10;
			field->print_routine = print_fields_uint;
		} else if (!xstrncasecmp("TresName", object,
					 MAX(command_len, 5))) {
			field->type = PRINT_CLUSTER_TRES_NAME;
			field->name = xstrdup("TRES Name");
			field->len = 14;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Used", object, MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_AMOUNT_USED;
			field->name = xstrdup("Used");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("WCKey", object, MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER_WCKEY;
			field->name = xstrdup("WCKey");
			if (tree_display)
				field->len = 20;
			else
				field->len = 15;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Energy", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER_ENERGY;
			field->name = xstrdup("Energy");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = print_fields_str;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}

		if (newlen)
			field->len = newlen;

		list_append(print_fields_list, field);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static void _set_usage_column_width(List print_fields_list,
				    List slurmdb_report_cluster_list)
{
	print_field_t *field, *usage_field = NULL, *energy_field = NULL;
	ListIterator itr;

	xassert(print_fields_list);
	xassert(slurmdb_report_cluster_list);

	itr = list_iterator_create(print_fields_list);
	while ((field = list_next(itr))) {
		switch (field->type) {
		case PRINT_CLUSTER_AMOUNT_USED:
			usage_field = field;
			break;
		case PRINT_CLUSTER_ENERGY:
			energy_field = field;
			break;
		}
	}
	list_iterator_destroy(itr);

	sreport_set_usage_column_width(usage_field, energy_field,
				       slurmdb_report_cluster_list);
}

static void _merge_cluster_recs(List cluster_list)
{
	slurmdb_cluster_rec_t *cluster = NULL, *first_cluster = NULL;
	ListIterator iter = NULL;

	if (list_count(cluster_list) < 2)
		return;

	iter = list_iterator_create(cluster_list);
	while ((cluster = list_next(iter))) {
		if (!first_cluster) {
			first_cluster = cluster;
			xfree(cluster->name);
			if (fed_name)
				xstrfmtcat(cluster->name, "FED:%s", fed_name);
			else
				cluster->name = xstrdup("FEDERATION");
		} else if (!first_cluster->accounting_list) {
			first_cluster->accounting_list =
				cluster->accounting_list;
			cluster->accounting_list = NULL;
			list_delete_item(iter);
		} else {
			list_transfer(first_cluster->accounting_list,
				      cluster->accounting_list);
			list_delete_item(iter);
		}
	}
	list_iterator_destroy(iter);
}

static List _get_cluster_list(int argc, char **argv, uint32_t *total_time,
			      char *report_name, List format_list)
{
	slurmdb_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(slurmdb_cluster_cond_t));
	int i = 0, fed_cluster_count = 1;
	List cluster_list = NULL;

	slurmdb_init_cluster_cond(cluster_cond, 0);
	cluster_cond->with_deleted = 1;
	cluster_cond->with_usage = 1;

	_set_cluster_cond(&i, argc, argv, cluster_cond, format_list);

	cluster_list = slurmdb_clusters_get(db_conn, cluster_cond);
	if (!cluster_list) {
		exit_code = 1;
		fprintf(stderr, " Problem with cluster query.\n");
		return NULL;
	}
	if (fed_name) {
		fed_cluster_count = list_count(cluster_list);
		_merge_cluster_recs(cluster_list);
	}

	if (print_fields_have_header) {
		char start_char[256];
		char end_char[256];
		time_t my_start = cluster_cond->usage_start;
		time_t my_end = cluster_cond->usage_end-1;

		slurm_make_time_str(&my_start,
				    start_char, sizeof(start_char));
		slurm_make_time_str(&my_end,
				    end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("%s %s - %s\n",
		       report_name, start_char, end_char);
		switch(time_format) {
		case SLURMDB_REPORT_TIME_PERCENT:
			printf("Usage reported in %s\n", time_format_string);
			break;
		default:
			printf("Usage reported in %s %s\n",
			       tres_usage_str, time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	/* Mutliply the time range by fed_cluster_count since the federation
	 * represents time for all clusters in the federation and not just one
	 * cluster. This gives correct reported time for a federated utilization
	 * report. */
	(*total_time) = (cluster_cond->usage_end - cluster_cond->usage_start) *
			fed_cluster_count;

	slurmdb_destroy_cluster_cond(cluster_cond);

	return cluster_list;
}

static void _cluster_account_by_user_tres_report(
	slurmdb_tres_rec_t *tres,
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster,
	slurmdb_report_assoc_rec_t *slurmdb_report_assoc,
	List tree_list)
{
	slurmdb_tres_rec_t *cluster_tres_rec, *tres_rec, *total_energy;
	char *tmp_char = NULL;
	int curr_inx = 1, field_count;
	ListIterator iter = NULL;
	print_field_t *field;
	uint64_t cluster_energy_cnt = 0, assoc_energy_cnt = 0;
	uint32_t tres_energy;
	char *tres_tmp = NULL;
	char *print_acct = NULL;

	sreport_set_tres_recs(&cluster_tres_rec, &tres_rec,
			      slurmdb_report_cluster->tres_list,
			      slurmdb_report_assoc->tres_list,
			      tres);

	field_count = list_count(print_fields_list);
	iter = list_iterator_create(print_fields_list);
	while ((field = list_next(iter))) {
		struct passwd *pwd = NULL;
		switch (field->type) {
		case PRINT_CLUSTER_ACCT:
			if (tree_display) {
				char *local_acct = NULL;
				char *parent_acct = NULL;
				if (slurmdb_report_assoc->user) {
					local_acct = xstrdup_printf(
						"|%s",
						slurmdb_report_assoc->acct);
					parent_acct =
						slurmdb_report_assoc->acct;
				} else {
					local_acct = xstrdup(
						slurmdb_report_assoc->acct);
					parent_acct = slurmdb_report_assoc->
						parent_acct;
				}

				print_acct = slurmdb_tree_name_get(local_acct,
								   parent_acct,
								   tree_list);
				xfree(local_acct);
			} else {
				print_acct = slurmdb_report_assoc->acct;
			}
			field->print_routine(field, print_acct,
					     (curr_inx == field_count));

			break;
		case PRINT_CLUSTER_NAME:
			field->print_routine(field,
					     slurmdb_report_cluster->name,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_LOGIN:
			field->print_routine(field,
					     slurmdb_report_assoc->user,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_PROPER:
			if (slurmdb_report_assoc->user)
				pwd = getpwnam(slurmdb_report_assoc->user);
			if (pwd) {
				tmp_char = strtok(pwd->pw_gecos, ",");
				if (!tmp_char)
					tmp_char = pwd->pw_gecos;
			}
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_AMOUNT_USED:
			tmp_char = sreport_get_time_str(
					tres_rec ? tres_rec->alloc_secs : 0,
					cluster_tres_rec ?
					cluster_tres_rec->alloc_secs : 0);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_ENERGY:
			/* For backward compatibility with pre-TRES logic,
			 * get energy_cnt here */
			tres_energy = TRES_ENERGY;
			if ((total_energy = list_find_first(
				     slurmdb_report_cluster->tres_list,
				     slurmdb_find_tres_in_list,
				     &tres_energy)))
				cluster_energy_cnt = total_energy->alloc_secs;
			if ((total_energy = list_find_first(
					slurmdb_report_assoc->tres_list,
					slurmdb_find_tres_in_list,
					&tres_energy)))
				assoc_energy_cnt = total_energy->alloc_secs;
			tmp_char = sreport_get_time_str(assoc_energy_cnt,
							cluster_energy_cnt);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_NAME:
			xstrfmtcat(tres_tmp, "%s%s%s",
				   tres->type,
				   tres->name ? "/" : "",
				   tres->name ? tres->name : "");

			field->print_routine(field, tres_tmp,
					     (curr_inx == field_count));
			xfree(tres_tmp);
			break;
		default:
			field->print_routine(field, NULL,
					     (curr_inx == field_count));
			break;
		}
		curr_inx++;
	}
	list_iterator_destroy(iter);
	printf("\n");
}

static void _merge_cluster_reps(List cluster_list)
{
	slurmdb_report_cluster_rec_t *cluster = NULL, *first_cluster = NULL;
	ListIterator iter = NULL;

	if (list_count(cluster_list) < 2)
		return;

	iter = list_iterator_create(cluster_list);
	while ((cluster = list_next(iter))) {
		if (!first_cluster) {
			first_cluster = cluster;
			xfree(cluster->name);
			if (fed_name)
				xstrfmtcat(cluster->name, "FED:%s", fed_name);
			else
				cluster->name = xstrdup("FEDERATION");
			continue;
		}
		combine_tres_list(first_cluster->tres_list, cluster->tres_list);
		if (!first_cluster->assoc_list) {
			first_cluster->assoc_list = cluster->assoc_list;
			cluster->assoc_list = NULL;
		} else {
			combine_assoc_tres(first_cluster->assoc_list,
					   cluster->assoc_list);
		}
		if (!first_cluster->user_list) {
			first_cluster->user_list = cluster->user_list;
			cluster->user_list = NULL;
		} else {
			combine_user_tres(first_cluster->user_list,
					  cluster->user_list);
		}
		list_delete_item(iter);
	}
	list_iterator_destroy(iter);
}

extern int cluster_account_by_user(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_assoc_cond_t *assoc_cond =
		xmalloc(sizeof(slurmdb_assoc_cond_t));
	slurmdb_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator tres_itr = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(xfree_ptr);
	List slurmdb_report_cluster_list = NULL;
	int i = 0;
	slurmdb_report_assoc_rec_t *slurmdb_report_assoc = NULL;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;
	List tree_list = NULL;

	print_fields_list = list_create(destroy_print_field);

	slurmdb_init_cluster_cond(&cluster_cond, 0);

	assoc_cond->with_sub_accts = 1;

	_set_assoc_cond(&i, argc, argv, assoc_cond, format_list);
	if (!list_count(format_list)) {
		if (tres_str) {
			slurm_addto_char_list(format_list,
				"Cluster,Ac,Login,Proper,TresName,Used");
		} else {
			slurm_addto_char_list(format_list,
				"Cluster,Ac,Login,Proper,Used,Energy");
		}
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	if (!(slurmdb_report_cluster_list =
	     slurmdb_report_cluster_account_by_user(db_conn, assoc_cond))) {
		exit_code = 1;
		goto end_it;
	}
	if (fed_name)
		_merge_cluster_reps(slurmdb_report_cluster_list);

	if (print_fields_have_header) {
		char start_char[256];
		char end_char[256];
		time_t my_start = assoc_cond->usage_start;
		time_t my_end = assoc_cond->usage_end - 1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/Account/User Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(assoc_cond->usage_end - assoc_cond->usage_start));

		switch (time_format) {
		case SLURMDB_REPORT_TIME_PERCENT:
			printf("Usage reported in %s\n", time_format_string);
			break;
		default:
			printf("Usage reported in %s %s\n",
			       tres_usage_str, time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	_set_usage_column_width(print_fields_list, slurmdb_report_cluster_list);

	print_fields_header(print_fields_list);

	list_sort(slurmdb_report_cluster_list, (ListCmpF)sort_cluster_dec);

	tres_itr = list_iterator_create(tres_list);
	cluster_itr = list_iterator_create(slurmdb_report_cluster_list);
	while ((slurmdb_report_cluster = list_next(cluster_itr))) {
		//list_sort(slurmdb_report_cluster->assoc_list,
		//  (ListCmpF)sort_assoc_dec);

		if (tree_list)
			list_flush(tree_list);
		else
			tree_list = list_create(slurmdb_destroy_print_tree);

		itr = list_iterator_create(slurmdb_report_cluster->assoc_list);
		while ((slurmdb_report_assoc = list_next(itr))) {
			slurmdb_tres_rec_t *tres;
			list_iterator_reset(tres_itr);
			while ((tres = list_next(tres_itr))) {
				if (tres->id == NO_VAL)
					continue;
				_cluster_account_by_user_tres_report(
					tres,
					slurmdb_report_cluster,
					slurmdb_report_assoc,
					tree_list);
			}
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
	list_iterator_destroy(tres_itr);

end_it:
	slurmdb_destroy_assoc_cond(assoc_cond);
	FREE_NULL_LIST(slurmdb_report_cluster_list);
	FREE_NULL_LIST(print_fields_list);
	FREE_NULL_LIST(tree_list);

	return rc;
}

static void _cluster_user_by_account_tres_report(slurmdb_tres_rec_t *tres,
		slurmdb_report_cluster_rec_t *slurmdb_report_cluster,
		slurmdb_report_user_rec_t *slurmdb_report_user)
{
	slurmdb_tres_rec_t *cluster_tres_rec, *tres_rec, *total_energy;
	char *tmp_char = NULL;
	struct passwd *pwd = NULL;
	int curr_inx = 1, field_count;
	ListIterator iter = NULL;
	print_field_t *field;
	uint64_t cluster_energy_cnt = 0, user_energy_cnt = 0;
	uint32_t tres_energy;
	char *tres_tmp = NULL;

	sreport_set_tres_recs(&cluster_tres_rec, &tres_rec,
			      slurmdb_report_cluster->tres_list,
			      slurmdb_report_user->tres_list,
			      tres);

	field_count = list_count(print_fields_list);
	iter = list_iterator_create(print_fields_list);
	while ((field = list_next(iter))) {
		switch (field->type) {
		case PRINT_CLUSTER_ACCT:
			field->print_routine(field,
					     slurmdb_report_user->acct,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_NAME:
			field->print_routine(field,
					     slurmdb_report_cluster->name,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_LOGIN:
			field->print_routine(field,
					     slurmdb_report_user->name,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_PROPER:
			pwd = getpwnam(slurmdb_report_user->name);
			if (pwd) {
				tmp_char = strtok(pwd->pw_gecos, ",");
				if (!tmp_char)
					tmp_char = pwd->pw_gecos;
			}
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_AMOUNT_USED:
			tmp_char = sreport_get_time_str(
					tres_rec ? tres_rec->alloc_secs : 0,
					cluster_tres_rec ?
					cluster_tres_rec->alloc_secs : 0);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_ENERGY:
			/* For backward compatibility with pre-TRES logic,
			 * get energy_cnt here */
			tres_energy = TRES_ENERGY;
			if ((total_energy = list_find_first(
				     slurmdb_report_cluster->tres_list,
				     slurmdb_find_tres_in_list,
				     &tres_energy)))
				cluster_energy_cnt = total_energy->alloc_secs;
			if ((total_energy = list_find_first(
					slurmdb_report_user->tres_list,
					slurmdb_find_tres_in_list,
					&tres_energy)))
				user_energy_cnt = total_energy->alloc_secs;
			tmp_char = sreport_get_time_str(user_energy_cnt,
							cluster_energy_cnt);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_NAME:
			xstrfmtcat(tres_tmp, "%s%s%s",
				   tres->type,
				   tres->name ? "/" : "",
				   tres->name ? tres->name : "");

			field->print_routine(field, tres_tmp,
					     (curr_inx == field_count));
			xfree(tres_tmp);
			break;
		default:
			field->print_routine(field, NULL,
					     (curr_inx == field_count));
			break;
		}
		curr_inx++;
	}
	list_iterator_destroy(iter);
	printf("\n");
}

extern int cluster_user_by_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_assoc_cond_t *assoc_cond =
		xmalloc(sizeof(slurmdb_assoc_cond_t));
	slurmdb_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(xfree_ptr);
	List slurmdb_report_cluster_list = NULL;
	int i = 0;
	slurmdb_report_user_rec_t *slurmdb_report_user = NULL;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;

	print_fields_list = list_create(destroy_print_field);

	slurmdb_init_cluster_cond(&cluster_cond, 0);

	_set_assoc_cond(&i, argc, argv, assoc_cond, format_list);

	if (!list_count(format_list)) {
		if (tres_str) {
			slurm_addto_char_list(format_list,
				"Cluster,Login,Proper,Ac,TresName,Used");
		} else {
			slurm_addto_char_list(format_list,
				"Cluster,Login,Proper,Ac,Used,Energy");
		}
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	if (!(slurmdb_report_cluster_list =
	     slurmdb_report_cluster_user_by_account(db_conn, assoc_cond))) {
		exit_code = 1;
		goto end_it;
	}
	if (fed_name)
		_merge_cluster_reps(slurmdb_report_cluster_list);

	if (print_fields_have_header) {
		char start_char[256];
		char end_char[256];
		time_t my_start = assoc_cond->usage_start;
		time_t my_end = assoc_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/User/Account Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(assoc_cond->usage_end - assoc_cond->usage_start));

		switch (time_format) {
		case SLURMDB_REPORT_TIME_PERCENT:
			printf("Usage reported in %s\n", time_format_string);
			break;
		default:
			printf("Usage reported in %s %s\n",
			       tres_usage_str, time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	_set_usage_column_width(print_fields_list, slurmdb_report_cluster_list);

	print_fields_header(print_fields_list);

	cluster_itr = list_iterator_create(slurmdb_report_cluster_list);
	while ((slurmdb_report_cluster = list_next(cluster_itr))) {
		itr = list_iterator_create(slurmdb_report_cluster->user_list);
		while ((slurmdb_report_user = list_next(itr))) {
			slurmdb_tres_rec_t *tres;
			itr2 = list_iterator_create(tres_list);
			while ((tres = list_next(itr2))) {
				if (tres->id == NO_VAL)
					continue;
				_cluster_user_by_account_tres_report(tres,
					slurmdb_report_cluster,
					slurmdb_report_user);
			}
			list_iterator_destroy(itr2);

		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	slurmdb_destroy_assoc_cond(assoc_cond);
	FREE_NULL_LIST(slurmdb_report_cluster_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

static void _cluster_user_by_wckey_tres_report(slurmdb_tres_rec_t *tres,
		slurmdb_report_cluster_rec_t *slurmdb_report_cluster,
		slurmdb_report_user_rec_t *slurmdb_report_user)
{
	slurmdb_tres_rec_t *cluster_tres_rec, *tres_rec, *total_energy;
	char *tmp_char = NULL;
	struct passwd *pwd = NULL;
	int curr_inx = 1, field_count;
	ListIterator iter = NULL;
	print_field_t *field;
	uint64_t cluster_energy_cnt = 0, user_energy_cnt = 0;
	uint32_t tres_energy;
	char *tres_tmp = NULL;

	sreport_set_tres_recs(&cluster_tres_rec, &tres_rec,
			      slurmdb_report_cluster->tres_list,
			      slurmdb_report_user->tres_list,
			      tres);

	field_count = list_count(print_fields_list);
	iter = list_iterator_create(print_fields_list);
	while ((field = list_next(iter))) {
		switch (field->type) {
		case PRINT_CLUSTER_WCKEY:
			field->print_routine(field,
					     slurmdb_report_user->acct,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_NAME:
			field->print_routine(field,
					     slurmdb_report_cluster->name,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_LOGIN:
			field->print_routine(field,
					     slurmdb_report_user->name,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_PROPER:
			pwd = getpwnam(slurmdb_report_user->name);
			if (pwd) {
				tmp_char = strtok(pwd->pw_gecos, ",");
				if (!tmp_char)
					tmp_char = pwd->pw_gecos;
			}
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_AMOUNT_USED:
			tmp_char = sreport_get_time_str(
					tres_rec ? tres_rec->alloc_secs : 0,
					cluster_tres_rec ?
					cluster_tres_rec->alloc_secs : 0);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_ENERGY:
			/* For backward compatibility with pre-TRES logic,
			 * get energy_cnt here */
			tres_energy = TRES_ENERGY;
			if ((total_energy = list_find_first(
				     slurmdb_report_cluster->tres_list,
				     slurmdb_find_tres_in_list,
				     &tres_energy)))
				cluster_energy_cnt = total_energy->alloc_secs;
			if ((total_energy = list_find_first(
					slurmdb_report_user->tres_list,
					slurmdb_find_tres_in_list,
					&tres_energy)))
				user_energy_cnt = total_energy->alloc_secs;
			tmp_char = sreport_get_time_str(user_energy_cnt,
							cluster_energy_cnt);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_NAME:
			xstrfmtcat(tres_tmp, "%s%s%s",
				   tres->type,
				   tres->name ? "/" : "",
				   tres->name ? tres->name : "");

			field->print_routine(field, tres_tmp,
					     (curr_inx == field_count));
			xfree(tres_tmp);
			break;
		default:
			field->print_routine(field, NULL,
					     (curr_inx == field_count));
			break;
		}
		curr_inx++;
	}
	list_iterator_destroy(iter);
	printf("\n");
}

extern int cluster_user_by_wckey(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_wckey_cond_t *wckey_cond =
		xmalloc(sizeof(slurmdb_wckey_cond_t));
	slurmdb_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(xfree_ptr);
	List slurmdb_report_cluster_list = NULL;
	int i = 0;
	slurmdb_report_user_rec_t *slurmdb_report_user = NULL;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;

	print_fields_list = list_create(destroy_print_field);

	slurmdb_init_cluster_cond(&cluster_cond, 0);

	_set_wckey_cond(&i, argc, argv, wckey_cond, format_list);

	if (!list_count(format_list)) {
		if (tres_str) {
			slurm_addto_char_list(format_list,
				"Cluster,Login,Proper,WCkey,TresName,Used");
		} else {
			slurm_addto_char_list(format_list,
				"Cluster,Login,Proper,WCkey,Used");
		}
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	if (!(slurmdb_report_cluster_list =
	     slurmdb_report_cluster_user_by_wckey(db_conn, wckey_cond))) {
		exit_code = 1;
		goto end_it;
	}
	if (fed_name)
		_merge_cluster_reps(slurmdb_report_cluster_list);

	if (print_fields_have_header) {
		char start_char[256];
		char end_char[256];
		time_t my_start = wckey_cond->usage_start;
		time_t my_end = wckey_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/User/WCKey Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(wckey_cond->usage_end - wckey_cond->usage_start));

		switch (time_format) {
		case SLURMDB_REPORT_TIME_PERCENT:
			printf("Usage reported in %s\n", time_format_string);
			break;
		default:
			printf("Usage reported in %s %s\n",
			       tres_usage_str, time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	_set_usage_column_width(print_fields_list, slurmdb_report_cluster_list);

	print_fields_header(print_fields_list);

	cluster_itr = list_iterator_create(slurmdb_report_cluster_list);
	while ((slurmdb_report_cluster = list_next(cluster_itr))) {
		itr = list_iterator_create(slurmdb_report_cluster->user_list);
		while ((slurmdb_report_user = list_next(itr))) {
			slurmdb_tres_rec_t *tres;
			itr2 = list_iterator_create(tres_list);
			while ((tres = list_next(itr2))) {
				if (tres->id == NO_VAL)
					continue;
				_cluster_user_by_wckey_tres_report(tres,
					slurmdb_report_cluster,
					slurmdb_report_user);
			}
			list_iterator_destroy(itr2);
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	slurmdb_destroy_wckey_cond(wckey_cond);
	FREE_NULL_LIST(slurmdb_report_cluster_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

/* Note the accounting_list in the cluster variable must already be
 * processed/summed before calling this function.
 */
static void _cluster_util_tres_report(slurmdb_tres_rec_t *tres,
				      slurmdb_cluster_rec_t *cluster,
				      uint32_t total_time)
{
	slurmdb_cluster_accounting_rec_t *total_acct;
	slurmdb_cluster_accounting_rec_t *total_energy;
	uint64_t total_reported = 0;
	uint64_t local_total_time = 0;
	int curr_inx = 1, field_count;
	ListIterator iter;
	char *tmp_char, *tres_tmp = NULL;
	print_field_t *field;
	uint32_t tres_energy;
	uint64_t energy_cnt = 0;

	if (!(total_acct = list_find_first(
		      cluster->accounting_list,
		      slurmdb_find_cluster_accting_tres_in_list,
		      &tres->id))) {
		debug2("error, no %s%s%s(%d) TRES!",
		       tres->type,
		       tres->name ? "/" : "",
		       tres->name ? tres->name : "",
		       tres->id);
		return;
	}

	total_reported = total_acct->tres_rec.alloc_secs;

	/* ENERGY could be 0 if there is no power cap set, so just say
	 * we reported the whole thing in that case.
	 */
	if (!total_acct->tres_rec.count && (tres->id == TRES_ENERGY))
		local_total_time = total_reported;
	else
		local_total_time = (uint64_t)total_time *
			(uint64_t)total_acct->tres_rec.count;

	field_count = list_count(print_fields_list);
	iter = list_iterator_create(print_fields_list);
	while ((field = list_next(iter))) {
		switch (field->type) {
		case PRINT_CLUSTER_NAME:
			field->print_routine(field, cluster->name,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_TRES_CNT:
			field->print_routine(field,
					     &total_acct->tres_rec.count,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_TRES_ALLOC:
			tmp_char = sreport_get_time_str(total_acct->alloc_secs,
							total_reported);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_DOWN:
			tmp_char = sreport_get_time_str(total_acct->down_secs,
							total_reported);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_IDLE:
			tmp_char = sreport_get_time_str(total_acct->idle_secs,
							total_reported);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_PLAN:
			tmp_char = sreport_get_time_str(total_acct->plan_secs,
							total_reported);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_OVER:
			tmp_char = sreport_get_time_str(total_acct->over_secs,
							total_reported);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_PLAN_DOWN:
			tmp_char = sreport_get_time_str(total_acct->pdown_secs,
							total_reported);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_REPORTED:
			tmp_char = sreport_get_time_str(total_reported,
							local_total_time);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_ENERGY:
			/* For backward compatibility with pre-TRES logic,
			 * get energy_cnt here */
			tres_energy = TRES_ENERGY;
			if ((total_energy = list_find_first(
				     cluster->accounting_list,
				     slurmdb_find_cluster_accting_tres_in_list,
				     &tres_energy)))
				energy_cnt = total_energy->tres_rec.count;
			tmp_char = sreport_get_time_str(energy_cnt, energy_cnt);
			field->print_routine(field, tmp_char,
			                     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_NAME:
			xstrfmtcat(tres_tmp, "%s%s%s",
				   tres->type,
				   tres->name ? "/" : "",
				   tres->name ? tres->name : "");

			field->print_routine(field, tres_tmp,
					     (curr_inx == field_count));
			xfree(tres_tmp);
			break;
		default:
			field->print_routine(field, NULL,
					     (curr_inx == field_count));
			break;
		}
		curr_inx++;
	}
	list_iterator_destroy(iter);
	printf("\n");
}

extern int cluster_utilization(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	uint32_t total_time = 0;
	List cluster_list = NULL;
	List format_list = list_create(xfree_ptr);
	slurmdb_cluster_accounting_rec_t total_acct;
	print_field_t *field;
	slurmdb_tres_rec_t *tres;

	print_fields_list = list_create(destroy_print_field);

	if (!(cluster_list = _get_cluster_list(argc, argv, &total_time,
					       "Cluster Utilization",
					       format_list)))
		goto end_it;

	if (!list_count(format_list)) {
		if (tres_str) {
			slurm_addto_char_list(format_list,
					      "Cl,TresName,al,d,planned,i,res,rep");
		} else {
			slurm_addto_char_list(format_list,
					      "Cl,al,d,planned,i,res,rep");
		}
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	memset(&total_acct, 0, sizeof(slurmdb_cluster_accounting_rec_t));
	itr = list_iterator_create(cluster_list);
	while ((cluster = list_next(itr))) {
		slurmdb_cluster_accounting_rec_t *accting = NULL;
		List total_tres_acct = NULL;

		if (!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		itr3 = list_iterator_create(cluster->accounting_list);
		while ((accting = list_next(itr3))) {
			slurmdb_sum_accounting_list(
				accting, &total_tres_acct);
		}
		list_iterator_destroy(itr3);

		/* Swap out the accounting list for the total tres
		 * acct list.  This way we can figure out what the
		 * largest number is before we have to print the
		 * columns.
		 */
		FREE_NULL_LIST(cluster->accounting_list);
		cluster->accounting_list = total_tres_acct;
		total_tres_acct = NULL;

		itr2 = list_iterator_create(tres_list);
		while ((tres = list_next(itr2))) {
			if (tres->id == NO_VAL)
				continue;

			if (!(accting = list_find_first(
				      cluster->accounting_list,
				      slurmdb_find_cluster_accting_tres_in_list,
				      &tres->id))) {
				continue;
			}

			accting->tres_rec.count /=
				accting->tres_rec.rec_count;

			total_acct.alloc_secs = MAX(total_acct.alloc_secs,
						    accting->alloc_secs);
			total_acct.down_secs = MAX(total_acct.down_secs,
						   accting->down_secs);
			total_acct.idle_secs = MAX(total_acct.idle_secs,
						   accting->idle_secs);
			total_acct.plan_secs = MAX(total_acct.plan_secs,
						   accting->plan_secs);
			total_acct.over_secs = MAX(total_acct.over_secs,
						   accting->over_secs);
			total_acct.pdown_secs = MAX(total_acct.pdown_secs,
						    accting->pdown_secs);

			accting->tres_rec.alloc_secs =
				accting->alloc_secs +
				accting->down_secs +
				accting->pdown_secs +
				accting->idle_secs +
				accting->plan_secs;

			total_acct.tres_rec.alloc_secs = MAX(
				total_acct.tres_rec.alloc_secs,
				accting->tres_rec.alloc_secs);
		}
		list_iterator_destroy(itr2);
	}

	itr = list_iterator_create(print_fields_list);
	while ((field = list_next(itr))) {
		switch (field->type) {
		case PRINT_CLUSTER_TRES_ALLOC:
			sreport_set_usage_col_width(
				field, total_acct.alloc_secs);
			break;
		case PRINT_CLUSTER_TRES_DOWN:
			sreport_set_usage_col_width(
				field, total_acct.down_secs);
			break;
		case PRINT_CLUSTER_TRES_IDLE:
			sreport_set_usage_col_width(
				field, total_acct.idle_secs);
			break;
		case PRINT_CLUSTER_TRES_PLAN:
			sreport_set_usage_col_width(
				field, total_acct.plan_secs);
			break;
		case PRINT_CLUSTER_TRES_OVER:
			sreport_set_usage_col_width(
				field, total_acct.over_secs);
			break;
		case PRINT_CLUSTER_TRES_PLAN_DOWN:
			sreport_set_usage_col_width(
				field, total_acct.pdown_secs);
			break;
		case PRINT_CLUSTER_TRES_REPORTED:
			sreport_set_usage_col_width(
				field, total_acct.tres_rec.alloc_secs);
			break;
		case PRINT_CLUSTER_ENERGY:
			sreport_set_usage_col_width(
				field, total_acct.alloc_secs);
			break;
		}
	}
	list_iterator_destroy(itr);

	print_fields_header(print_fields_list);

	itr = list_iterator_create(cluster_list);
	while ((cluster = list_next(itr))) {
		if (!cluster->accounting_list ||
		    !list_count(cluster->accounting_list))
			continue;

		itr2 = list_iterator_create(tres_list);
		while ((tres = list_next(itr2))) {
			if (tres->id == NO_VAL)
				continue;
			_cluster_util_tres_report(tres, cluster, total_time);
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);

end_it:
	FREE_NULL_LIST(cluster_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

static void _cluster_wckey_by_user_tres_report(slurmdb_tres_rec_t *tres,
		slurmdb_report_cluster_rec_t *slurmdb_report_cluster,
		slurmdb_report_assoc_rec_t *slurmdb_report_assoc)
{
	slurmdb_tres_rec_t *cluster_tres_rec, *tres_rec;
	int curr_inx = 1, field_count;
	ListIterator iter = NULL;
	print_field_t *field;
	char *tres_tmp = NULL;

	sreport_set_tres_recs(&cluster_tres_rec, &tres_rec,
			      slurmdb_report_cluster->tres_list,
			      slurmdb_report_assoc->tres_list,
			      tres);

	field_count = list_count(print_fields_list);
	iter = list_iterator_create(print_fields_list);
	while ((field = list_next(iter))) {
		char *tmp_char = NULL;
		struct passwd *pwd = NULL;
		switch (field->type) {
		case PRINT_CLUSTER_WCKEY:
			field->print_routine(field,
					     slurmdb_report_assoc->acct,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_NAME:
			field->print_routine(field,
					     slurmdb_report_cluster->name,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_LOGIN:
			field->print_routine(field, slurmdb_report_assoc->user,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_USER_PROPER:
			if (slurmdb_report_assoc->user)
				pwd = getpwnam(slurmdb_report_assoc->user);
			if (pwd) {
				tmp_char = strtok(pwd->pw_gecos, ",");
				if (!tmp_char)
					tmp_char = pwd->pw_gecos;
			}
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER_AMOUNT_USED:
			tmp_char = sreport_get_time_str(
					tres_rec ? tres_rec->alloc_secs : 0,
					cluster_tres_rec ?
					cluster_tres_rec->alloc_secs : 0);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_CLUSTER_TRES_NAME:
			xstrfmtcat(tres_tmp, "%s%s%s",
				   tres->type,
				   tres->name ? "/" : "",
				   tres->name ? tres->name : "");

			field->print_routine(field, tres_tmp,
					     (curr_inx == field_count));
			xfree(tres_tmp);
			break;
		default:
			field->print_routine(field, NULL,
					     (curr_inx == field_count));
			break;
		}
		curr_inx++;
	}
	list_iterator_destroy(iter);
	printf("\n");
}

extern int cluster_wckey_by_user(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_wckey_cond_t *wckey_cond =
		xmalloc(sizeof(slurmdb_wckey_cond_t));
	slurmdb_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(xfree_ptr);
	List slurmdb_report_cluster_list = NULL;
	int i = 0;
	slurmdb_report_assoc_rec_t *slurmdb_report_assoc = NULL;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;

	print_fields_list = list_create(destroy_print_field);

	slurmdb_init_cluster_cond(&cluster_cond, 0);

	_set_wckey_cond(&i, argc, argv, wckey_cond, format_list);

	if (!list_count(format_list)) {
		if (tres_str) {
			slurm_addto_char_list(format_list,
				"Cluster,WCKey,Login,Proper,TresName,Used");
		} else {
			slurm_addto_char_list(format_list,
				"Cluster,WCKey,Login,Proper,Used");
		}
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	if (!(slurmdb_report_cluster_list =
	     slurmdb_report_cluster_wckey_by_user(db_conn, wckey_cond))) {
		exit_code = 1;
		goto end_it;
	}
	if (fed_name)
		_merge_cluster_reps(slurmdb_report_cluster_list);

	if (print_fields_have_header) {
		char start_char[256];
		char end_char[256];
		time_t my_start = wckey_cond->usage_start;
		time_t my_end = wckey_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Cluster/WCKey/User Utilization %s - %s (%d secs)\n",
		       start_char, end_char,
		       (int)(wckey_cond->usage_end - wckey_cond->usage_start));

		switch (time_format) {
		case SLURMDB_REPORT_TIME_PERCENT:
			printf("Usage reported in %s\n", time_format_string);
			break;
		default:
			printf("Usage reported in %s %s\n",
			       tres_usage_str, time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	_set_usage_column_width(print_fields_list, slurmdb_report_cluster_list);

	print_fields_header(print_fields_list);

	list_sort(slurmdb_report_cluster_list, (ListCmpF)sort_cluster_dec);

	cluster_itr = list_iterator_create(slurmdb_report_cluster_list);
	while ((slurmdb_report_cluster = list_next(cluster_itr))) {
		slurmdb_tres_rec_t *tres;

		if (!slurmdb_report_cluster->tres_list ||
		    !list_count(slurmdb_report_cluster->tres_list)) {
			error("No TRES given for cluster %s",
			      slurmdb_report_cluster->name);
			continue;
		}

		itr = list_iterator_create(slurmdb_report_cluster->assoc_list);
		while ((slurmdb_report_assoc = list_next(itr))) {
			itr2 = list_iterator_create(tres_list);
			while ((tres = list_next(itr2))) {
				if (tres->id == NO_VAL)
					continue;
				_cluster_wckey_by_user_tres_report(tres,
					slurmdb_report_cluster,
					slurmdb_report_assoc);
			}
			list_iterator_destroy(itr2);
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);

end_it:
	slurmdb_destroy_wckey_cond(wckey_cond);
	FREE_NULL_LIST(slurmdb_report_cluster_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}
