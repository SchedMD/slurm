/*****************************************************************************\
 *  job_reports.c - functions for generating job reports
 *                  from accounting infrastructure.
 *****************************************************************************
 *  Copyright (C) 2010-2017 SchedMD LLC.
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

#include "src/common/uid.h"
#include "job_reports.h"

enum {
	PRINT_JOB_ACCOUNT,
	PRINT_JOB_CLUSTER,
	PRINT_JOB_COUNT,
	PRINT_JOB_DUR,
	PRINT_JOB_NODES,
	PRINT_JOB_SIZE,
	PRINT_JOB_TRES_COUNT,
	PRINT_JOB_USER,
	PRINT_JOB_WCKEY
};

enum {
	GROUPED_TOP_ACCT,
	GROUPED_WCKEY,
	GROUPED_TOP_ACCT_AND_WCKEY,
};

static List print_fields_list = NULL; /* types are of print_field_t */
static List grouping_print_fields_list = NULL; /* types are of print_field_t */
static int print_job_count = 0;
static bool flat_view = false;
static bool individual_grouping = 0;

/*
 * Comparator used for sorting clusters alphabetically
 *
 * returns: 1: cluster_a > cluster_b
 *           0: cluster_a == cluster_b
 *           -1: cluster_a < cluster_b
 *
 */
static int _sort_cluster_grouping_dec(void *v1, void *v2)
{
	int diff = 0;
	slurmdb_report_cluster_grouping_t *cluster_a;
	slurmdb_report_cluster_grouping_t *cluster_b;

	cluster_a = *(slurmdb_report_cluster_grouping_t **)v1;
	cluster_b = *(slurmdb_report_cluster_grouping_t **)v2;

	if (!cluster_a->cluster || !cluster_b->cluster)
		return 0;

	diff = xstrcmp(cluster_a->cluster, cluster_b->cluster);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	return 0;
}

/*
 * Comparator used for sorting clusters alphabetically
 *
 * returns: 1: acct_a > acct_b
 *           0: acct_a == acct_b
 *           -1: acct_a < acct_b
 *
 */
static int _sort_acct_grouping_dec(void *v1, void *v2)
{
	int diff = 0;
	char tmp_acct_a[200];
	char tmp_acct_b[200];
	char *wckey_a = NULL, *wckey_b = NULL;
	slurmdb_report_acct_grouping_t *acct_a;
	slurmdb_report_acct_grouping_t *acct_b;

	acct_a = *(slurmdb_report_acct_grouping_t **)v1;
	acct_b = *(slurmdb_report_acct_grouping_t **)v2;

	if (!acct_a->acct || !acct_b->acct)
		return 0;

	snprintf(tmp_acct_a, sizeof(tmp_acct_a), "%s", acct_a->acct);
	snprintf(tmp_acct_b, sizeof(tmp_acct_b), "%s", acct_b->acct);
	if ((wckey_a = strstr(tmp_acct_a, ":")))
		*wckey_a++ = 0;

	if ((wckey_b = strstr(tmp_acct_b, ":")))
		*wckey_b++ = 0;

	diff = xstrcmp(tmp_acct_a, tmp_acct_b);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	if (!wckey_a || !wckey_b)
		return 0;

	diff = xstrcmp(wckey_a, wckey_b);

	if (diff > 0)
		return 1;
	else if (diff < 0)
		return -1;

	return 0;
}


static char *_string_to_uid( char *name )
{
	uid_t uid;
	if ( uid_from_string( name, &uid ) != 0 ) {
		fprintf(stderr, "Invalid user id: %s\n", name);
		exit(1);
	}
	xfree(name);
	return xstrdup_printf( "%d", (int) uid );
}


/* returns number of objects added to list */
static int _addto_uid_char_list(List char_list, char *names)
{
	int i = 0, start = 0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while (names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if (quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if (names[i] == ',') {
				if ((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					//info("got %s %d", name, i-start);
					name = _string_to_uid( name );

					while ((tmp_char = list_next(itr))) {
						if (!xstrcasecmp(tmp_char,
								 name))
							break;
					}

					if (!tmp_char) {
						list_append(char_list, name);
						count++;
					} else
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
				if (!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if ((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			name = _string_to_uid( name );

			while ((tmp_char = list_next(itr))) {
				if (!xstrcasecmp(tmp_char, name))
					break;
			}

			if (!tmp_char) {
				list_append(char_list, name);
				count++;
			} else
				xfree(name);
		}
	}
	list_iterator_destroy(itr);
	return count;
}

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_job_cond_t *job_cond,
		     List format_list, List grouping_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int command_len = 0;

	if (!job_cond->cluster_list)
		job_cond->cluster_list = list_create(slurm_destroy_char);
	if (cluster_flag)
		slurm_addto_char_list(job_cond->cluster_list, cluster_flag);

	for (i = (*start); i < argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len = strlen(argv[i]);
		else
			command_len = end-1;

		if (!end && !xstrncasecmp(argv[i], "all_clusters",
					  MAX(command_len, 1))) {
			local_cluster_flag = 1;
			continue;
		} else if (!end && !xstrncasecmp(argv[i], "PrintJobCount",
						 MAX(command_len, 2))) {
			print_job_count = 1;
			continue;
		} else if (!end && !xstrncasecmp(argv[i], "FlatView",
						 MAX(command_len, 2))) {
			flat_view = true;
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Clusters",
					   MAX(command_len, 1))) {
			slurm_addto_char_list(job_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Accounts",
					 MAX(command_len, 2))
			   || !xstrncasecmp(argv[i], "Acct",
					   MAX(command_len, 4))) {
			if (!job_cond->acct_list)
				job_cond->acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->acct_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Associations",
					 MAX(command_len, 2))) {
			if (!job_cond->associd_list)
				job_cond->associd_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->associd_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 1))) {
			job_cond->usage_end = parse_time(argv[i]+end, 1);
			job_cond->usage_end = sanity_check_endtime(job_cond->usage_end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 2))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Gid", MAX(command_len, 2))) {
			if (!job_cond->groupid_list)
				job_cond->groupid_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->groupid_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "grouping",
					 MAX(command_len, 2))) {
			if (!xstrncasecmp(argv[i]+end, "individual", 1)) {
				individual_grouping = 1;
			} else if (grouping_list)
					slurm_addto_char_list(grouping_list,
							      argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Jobs",
					 MAX(command_len, 1))) {
			char *end_char = NULL, *start_char = argv[i]+end;
			slurmdb_selected_step_t *selected_step = NULL;
			char *dot = NULL;
			if (!job_cond->step_list)
				job_cond->step_list =
					list_create(slurm_destroy_char);

			while ((end_char = strstr(start_char, ","))) {
				*end_char = 0;
				while (isspace(start_char[0]))
					start_char++;  /* discard whitespace */
				if (start_char[0] == '\0')
					continue;
				selected_step = xmalloc(
					sizeof(slurmdb_selected_step_t));
				list_append(job_cond->step_list, selected_step);

				dot = strstr(start_char, ".");
				if (dot == NULL) {
					debug2("No jobstep requested");
					selected_step->stepid = NO_VAL;
				} else {
					*dot++ = 0;
					selected_step->stepid = atoi(dot);
				}
				selected_step->jobid = atoi(start_char);
				selected_step->array_task_id = NO_VAL;
				selected_step->pack_job_offset = NO_VAL;
				start_char = end_char + 1;
			}

			set = 1;
		} else if (!xstrncasecmp(argv[i], "Nodes",
					 MAX(command_len, 1))) {
			if (job_cond->used_nodes) {
				error("You already specified nodes '%s' "
				      " combine your request into 1 nodes=.",
				      job_cond->used_nodes);
				exit_code = 1;
				break;
			}
			job_cond->used_nodes = xstrdup(argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Partitions",
					 MAX(command_len, 2))) {
			if (!job_cond->partition_list)
				job_cond->partition_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->partition_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 1))) {
			job_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Users",
					 MAX(command_len, 1))) {
			if (!job_cond->userid_list)
				job_cond->userid_list =
					list_create(slurm_destroy_char);
			_addto_uid_char_list(job_cond->userid_list,
					     argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Wckeys",
					 MAX(command_len, 2))) {
			if (!job_cond->wckey_list)
				job_cond->wckey_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->wckey_list,
					      argv[i]+end);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n"
				"Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if (!local_cluster_flag && !list_count(job_cond->cluster_list)) {
		/* Get the default Cluster since no cluster is specified */
		char *temp = slurm_get_cluster_name();
		if (temp)
			list_append(job_cond->cluster_list, temp);
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = job_cond->usage_start;
	end_time = job_cond->usage_end;
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	job_cond->usage_start = start_time;
	job_cond->usage_end = end_time;

	return set;
}


static int _setup_print_fields_list(List format_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;

	if (!format_list || !list_count(format_list)) {
		exit_code = 1;
		fprintf(stderr,
			" We need a format list to set up the print.\n");
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
		if (!xstrncasecmp("Account", object, MAX(command_len, 1))
		   || !xstrncasecmp("Acct", object, MAX(command_len, 4))) {
			field->type = PRINT_JOB_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Cluster", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_JOB_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Duration", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_JOB_DUR;
			field->name = xstrdup("Duration");
			field->len = 12;
			field->print_routine = print_fields_time;
		} else if (!xstrncasecmp("JobCount", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_JOB_COUNT;
			field->name = xstrdup("Job Count");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if (!xstrncasecmp("NodeCount", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_JOB_NODES;
			field->name = xstrdup("Node Count");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if (!xstrncasecmp("TresCount", object,
					 MAX(command_len, 5)) ||
			   !xstrncasecmp("CpuCount", object,
					 MAX(command_len, 2)) ||
			   !xstrncasecmp("count", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_JOB_TRES_COUNT;
			field->name = xstrdup("TRES Count");
			field->len = 10;
			field->print_routine = print_fields_uint;
		} else if (!xstrncasecmp("User", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_JOB_USER;
			field->name = xstrdup("User");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Wckey", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_JOB_WCKEY;
			field->name = xstrdup("Wckey");
			field->len = 9;
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

static int _setup_grouping_print_fields_list(List grouping_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;
	char *last_object = NULL;
	uint32_t last_size = 0;
	uint32_t size = 0;
	char *tmp_char = NULL, *tres_type;

	if (!tres_str || !xstrcasecmp(tres_str, "cpu"))
		tres_type = "CPUs";
	else
		tres_type = "TRES";

	if (!grouping_list || !list_count(grouping_list)) {
		exit_code = 1;
		fprintf(stderr,
			" We need a grouping list to set up the print.\n");
		return SLURM_ERROR;
	}

	if (!grouping_print_fields_list)
		grouping_print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(grouping_list);
	while ((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		size = atoi(object);
		if (print_job_count)
			field->type = PRINT_JOB_COUNT;
		else
			field->type = PRINT_JOB_SIZE;
		if (individual_grouping)
			field->name = xstrdup_printf("%u %s", size, tres_type);
		else
			field->name = xstrdup_printf("%u-%u %s", last_size,
						     size-1, tres_type);
		if (time_format == SLURMDB_REPORT_TIME_SECS_PER
		   || time_format == SLURMDB_REPORT_TIME_MINS_PER
		   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
			field->len = 20;
		else
			field->len = 13;

		if (print_job_count)
			field->print_routine = print_fields_uint;
		else
			field->print_routine = slurmdb_report_print_time;
		last_size = size;
		last_object = object;
		if ((tmp_char = strstr(object, "\%"))) {
			int newlen = atoi(tmp_char + 1);
			if (newlen)
				field->len = newlen;
		}
		list_append(grouping_print_fields_list, field);
	}
	list_iterator_destroy(itr);

	if (last_size && !individual_grouping) {
		field = xmalloc(sizeof(print_field_t));
		if (print_job_count)
			field->type = PRINT_JOB_COUNT;
		else
			field->type = PRINT_JOB_SIZE;

		field->name = xstrdup_printf(">= %u %s", last_size, tres_type);
		if (time_format == SLURMDB_REPORT_TIME_SECS_PER
		   || time_format == SLURMDB_REPORT_TIME_MINS_PER
		   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
			field->len = 20;
		else
			field->len = 13;
		if (print_job_count)
			field->print_routine = print_fields_uint;
		else
			field->print_routine = slurmdb_report_print_time;
		if ((tmp_char = strstr(last_object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if (newlen)
				field->len = newlen;
		}
		list_append(grouping_print_fields_list, field);
	}

	return SLURM_SUCCESS;
}

/* Return 1 if a slurmdb job grouping record has NULL tres_list */
static int _find_empty_job_tres(void *x, void *key)
{
	slurmdb_report_job_grouping_t *dup_job_group;

	dup_job_group = (slurmdb_report_job_grouping_t *) x;
	if (dup_job_group->tres_list == NULL)
		return 1;
	return 0;
}

/* Return 1 if account name for two records match */
static int _match_job_group(void *x, void *key)
{
	slurmdb_report_job_grouping_t *orig_job_group;
	slurmdb_report_job_grouping_t *dup_job_group;

	orig_job_group = (slurmdb_report_job_grouping_t *) key;
	dup_job_group  = (slurmdb_report_job_grouping_t *) x;
	if ((orig_job_group->min_size == dup_job_group->min_size) &&
	    (orig_job_group->max_size == dup_job_group->max_size))
		return 1;
	return 0;
}

/* For duplicate account records, combine TRES records into the original
 * list and purge the duplicate records */
static void _combine_job_groups(List first_job_list, List new_job_list)
{
	slurmdb_report_job_grouping_t *orig_job_group = NULL;
	slurmdb_report_job_grouping_t *dup_job_group = NULL;
	ListIterator iter = NULL;

	if (!first_job_list || !new_job_list)
		return;

	iter = list_iterator_create(first_job_list);
	while ((orig_job_group = list_next(iter))) {
		dup_job_group = list_find_first(new_job_list,
						_match_job_group,
						orig_job_group);
		if (!dup_job_group)
			continue;
		orig_job_group->count += dup_job_group->count;
		combine_tres_list(orig_job_group->tres_list,
				  dup_job_group->tres_list);
		FREE_NULL_LIST(dup_job_group->tres_list);
	}
	list_iterator_destroy(iter);

	(void) list_delete_all(new_job_list, _find_empty_job_tres, NULL);
	list_transfer(first_job_list, new_job_list);
}

/* Return 1 if a slurmdb account record has NULL tres_list */
static int _find_empty_acct_tres(void *x, void *key)
{
	slurmdb_report_acct_grouping_t *dup_report_acct;

	dup_report_acct = (slurmdb_report_acct_grouping_t *) x;
	if (dup_report_acct->tres_list == NULL)
		return 1;
	return 0;
}

/* Return 1 if account name for two records match */
static int _match_acct_name(void *x, void *key)
{
	slurmdb_report_acct_grouping_t *orig_report_acct;
	slurmdb_report_acct_grouping_t *dup_report_acct;

	orig_report_acct = (slurmdb_report_acct_grouping_t *) key;
	dup_report_acct  = (slurmdb_report_acct_grouping_t *) x;
	if (!xstrcmp(orig_report_acct->acct, dup_report_acct->acct))
		return 1;
	return 0;
}

/* For duplicate account records, combine TRES records into the original
 * list and purge the duplicate records */
static void _combine_acct_groups(List first_acct_list, List new_acct_list)
{
	slurmdb_report_acct_grouping_t *orig_report_acct = NULL;
	slurmdb_report_acct_grouping_t *dup_report_acct = NULL;
	ListIterator iter = NULL;

	if (!first_acct_list || !new_acct_list)
		return;

	iter = list_iterator_create(first_acct_list);
	while ((orig_report_acct = list_next(iter))) {
		dup_report_acct = list_find_first(new_acct_list,
						  _match_acct_name,
						  orig_report_acct);
		if (!dup_report_acct)
			continue;
		orig_report_acct->count += dup_report_acct->count;
		_combine_job_groups(orig_report_acct->groups,
				    dup_report_acct->groups);
		combine_tres_list(orig_report_acct->tres_list,
				  dup_report_acct->tres_list);
		FREE_NULL_LIST(dup_report_acct->tres_list);
	}
	list_iterator_destroy(iter);

	(void) list_delete_all(new_acct_list, _find_empty_acct_tres, NULL);
	list_transfer(first_acct_list, new_acct_list);
}

static void _merge_cluster_groups(List slurmdb_report_cluster_grouping_list)
{
	slurmdb_report_cluster_grouping_t *group = NULL, *first_group = NULL;
	ListIterator iter = NULL;

	if (list_count(slurmdb_report_cluster_grouping_list) < 2)
		return;

	iter = list_iterator_create(slurmdb_report_cluster_grouping_list);
	while ((group = list_next(iter))) {
		if (!first_group) {
			first_group = group;
			xfree(group->cluster);
			if (fed_name)
				xstrfmtcat(group->cluster, "FED:%s", fed_name);
			else
				group->cluster = xstrdup("FEDERATION");
			continue;
		}
		first_group->count += group->count;
		combine_tres_list(first_group->tres_list, group->tres_list);
		if (!first_group->acct_list) {
			first_group->acct_list = group->acct_list;
			group->acct_list = NULL;
		} else {
			_combine_acct_groups(first_group->acct_list,
					      group->acct_list);
		}
		list_delete_item(iter);
	}
	list_iterator_destroy(iter);
}

static int _run_report(int type, int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_job_cond_t *job_cond = xmalloc(sizeof(slurmdb_job_cond_t));
	uint32_t tres_id = TRES_CPU;
	int i = 0, tres_cnt = 0;
	slurmdb_tres_rec_t *tres;
	uint64_t count1, count2;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator cluster_itr = NULL;
	ListIterator local_itr = NULL;
	ListIterator acct_itr = NULL;
	slurmdb_report_cluster_grouping_t *cluster_group = NULL;
	slurmdb_report_acct_grouping_t *acct_group = NULL;
	slurmdb_report_job_grouping_t *job_group = NULL;
	print_field_t *field = NULL;
	print_field_t total_field;
	slurmdb_report_time_format_t temp_format;
	List slurmdb_report_cluster_grouping_list = NULL;
	List assoc_list = NULL;
	List format_list = list_create(slurm_destroy_char);
	List grouping_list = list_create(slurm_destroy_char);
	List header_list = NULL;
	char *object_str = "";

	/* init memory before chance of going to end_it before being init'ed. */
	memset(&total_field, 0, sizeof(print_field_t));
	print_fields_list = list_create(destroy_print_field);

	_set_cond(&i, argc, argv, job_cond, format_list, grouping_list);

	if (!individual_grouping && !list_count(grouping_list))
		slurm_addto_char_list(grouping_list, "50,250,500,1000");

	switch (type) {
	case GROUPED_TOP_ACCT:
		if (!(slurmdb_report_cluster_grouping_list =
		      slurmdb_report_job_sizes_grouped_by_top_account(
			      db_conn, job_cond, grouping_list, flat_view))) {
			exit_code = 1;
			goto end_it;
		}
		if (!list_count(format_list))
			slurm_addto_char_list(format_list, "Cl,a");
		break;
	case GROUPED_WCKEY:
		if (!(slurmdb_report_cluster_grouping_list =
		      slurmdb_report_job_sizes_grouped_by_wckey(
			      db_conn, job_cond, grouping_list))) {
			exit_code = 1;
			goto end_it;
		}
		if (!list_count(format_list))
			slurm_addto_char_list(format_list, "Cl,wc");
		object_str = "by Wckey ";
		break;
	case GROUPED_TOP_ACCT_AND_WCKEY:
		if (!(slurmdb_report_cluster_grouping_list =
		      slurmdb_report_job_sizes_grouped_by_top_account_then_wckey(
			      db_conn, job_cond, grouping_list, flat_view))) {
			exit_code = 1;
			goto end_it;
		}
		if (!list_count(format_list))
			slurm_addto_char_list(format_list, "Cl,a%-20");
		break;
	default:
		exit_code = 1;
		goto end_it;
		break;
	}
	if (fed_name)
		_merge_cluster_groups(slurmdb_report_cluster_grouping_list);

	itr2 = list_iterator_create(tres_list);
	while ((tres = list_next(itr2))) {
		if (tres->id == NO_VAL)
			continue;
		tres_id = tres->id;
		tres_cnt++;
	}
	list_iterator_destroy(itr2);
	if (tres_cnt > 1) {
		fprintf(stderr,
		        " Job report only support a single --tres type.\n"
			" Generate a separate report for each TRES type.\n");
		exit_code = 1;
		goto end_it;
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	if (_setup_grouping_print_fields_list(grouping_list) != SLURM_SUCCESS)
		goto end_it;

	if (print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = job_cond->usage_start;
		time_t my_end = job_cond->usage_end - 1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Job Sizes %s%s - %s (%d secs)\n",
		       object_str, start_char, end_char,
		       (int)(job_cond->usage_end - job_cond->usage_start));
		if (tres_str)
			printf("TRES type is %s\n", tres_str);
		if (print_job_count)
			printf("Units are in number of jobs ran\n");
		else
			printf("Time reported in %s\n", time_format_string);
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	header_list = list_create(NULL);
	list_append_list(header_list, print_fields_list);
	list_append_list(header_list, grouping_print_fields_list);

	total_field.type = PRINT_JOB_SIZE;
	total_field.name = xstrdup("% of cluster");
	total_field.len = 12;
	total_field.print_routine = slurmdb_report_print_time;
	list_append(header_list, &total_field);

	print_fields_header(header_list);
	FREE_NULL_LIST(header_list);

//	time_format = SLURMDB_REPORT_TIME_PERCENT;

	itr = list_iterator_create(print_fields_list);
	itr2 = list_iterator_create(grouping_print_fields_list);
	list_sort(slurmdb_report_cluster_grouping_list,
	          (ListCmpF)_sort_cluster_grouping_dec);
	cluster_itr = list_iterator_create(
		slurmdb_report_cluster_grouping_list);
	while ((cluster_group = list_next(cluster_itr))) {
		slurmdb_tres_rec_t *tres_rec;
		uint64_t cluster_tres_alloc_secs = 0;

		if (cluster_group->tres_list &&
		    (tres_rec = list_find_first(
			    cluster_group->tres_list,
			    slurmdb_find_tres_in_list,
			    &tres_id)))
			cluster_tres_alloc_secs = tres_rec->alloc_secs;

		list_sort(cluster_group->acct_list,
		          (ListCmpF)_sort_acct_grouping_dec);
		acct_itr = list_iterator_create(cluster_group->acct_list);
		while ((acct_group = list_next(acct_itr))) {
			uint64_t acct_tres_alloc_secs = 0;

			if (acct_group->tres_list &&
			    (tres_rec = list_find_first(
				    acct_group->tres_list,
				    slurmdb_find_tres_in_list,
				    &tres_id)))
				acct_tres_alloc_secs = tres_rec->alloc_secs;

			while ((field = list_next(itr))) {
				switch (field->type) {
				case PRINT_JOB_CLUSTER:
					field->print_routine(
						field,
						cluster_group->cluster, 0);
					break;
				case PRINT_JOB_WCKEY:
				case PRINT_JOB_ACCOUNT:
					field->print_routine(field,
							     acct_group->acct,
							     0);
					break;
				default:
					field->print_routine(field,
							     NULL,
							     0);
					break;
				}
			}
			list_iterator_reset(itr);
			local_itr = list_iterator_create(acct_group->groups);
			while ((job_group = list_next(local_itr))) {
				uint64_t job_cpu_alloc_secs = 0;

				if (job_group->tres_list &&
				    (tres_rec = list_find_first(
					    job_group->tres_list,
					    slurmdb_find_tres_in_list,
					    &tres_id)))
					job_cpu_alloc_secs =
						tres_rec->alloc_secs;

				field = list_next(itr2);
				switch (field->type) {
				case PRINT_JOB_SIZE:
					field->print_routine(
						field,
						job_cpu_alloc_secs,
						acct_tres_alloc_secs,
						0);
					break;
				case PRINT_JOB_COUNT:
					field->print_routine(
						field,
						job_group->count,
						0);
					break;
				default:
					field->print_routine(field,
							     NULL,
							     0);
					break;
				}
			}
			list_iterator_reset(itr2);
			list_iterator_destroy(local_itr);

			temp_format = time_format;
			time_format = SLURMDB_REPORT_TIME_PERCENT;
			if (!print_job_count) {
				count1 = acct_tres_alloc_secs;
				count2 = cluster_tres_alloc_secs;
			} else {
				count1 = acct_group->count;
				count2 = cluster_group->count;
			}
			total_field.print_routine(&total_field,
						  count1, count2, 1);
			time_format = temp_format;
			printf("\n");
		}
		list_iterator_destroy(acct_itr);
	}
	list_iterator_destroy(itr);

//	time_format = temp_time_format;

end_it:
	xfree(total_field.name);
	if (print_job_count)
		print_job_count = 0;

	if (individual_grouping)
		individual_grouping = 0;

	slurmdb_destroy_job_cond(job_cond);

	FREE_NULL_LIST(grouping_list);
	FREE_NULL_LIST(assoc_list);
	FREE_NULL_LIST(slurmdb_report_cluster_grouping_list);
	FREE_NULL_LIST(print_fields_list);
	FREE_NULL_LIST(grouping_print_fields_list);

	return rc;
}

extern int job_sizes_grouped_by_top_acct(int argc, char **argv)
{
	return _run_report(GROUPED_TOP_ACCT, argc, argv);
}

extern int job_sizes_grouped_by_wckey(int argc, char **argv)
{
	return _run_report(GROUPED_WCKEY, argc, argv);
}

extern int job_sizes_grouped_by_top_acct_and_wckey(int argc, char **argv)
{
	return _run_report(GROUPED_TOP_ACCT_AND_WCKEY, argc, argv);
}
