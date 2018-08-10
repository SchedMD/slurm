/*****************************************************************************\
 *  user_reports.c - functions for generating user reports
 *                     from accounting infrastructure.
 *****************************************************************************
 *  Copyright (C) 2010-2015 SchedMD LLC.
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

#include "user_reports.h"

enum {
	PRINT_USER_ACCT,
	PRINT_USER_CLUSTER,
	PRINT_USER_LOGIN,
	PRINT_USER_PROPER,
	PRINT_USER_USED,
	PRINT_USER_ENERGY,
	PRINT_USER_TRES_NAME,
};

static List print_fields_list = NULL; /* types are of print_field_t */
static bool group_accts = false;
static uint32_t top_limit = 10;

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_user_cond_t *user_cond, List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	slurmdb_assoc_cond_t *assoc_cond = NULL;
	time_t start_time, end_time;
	int command_len = 0;

	if (!user_cond) {
		error("We need an slurmdb_user_cond to call this");
		return SLURM_ERROR;
	}

	user_cond->with_deleted = 1;
	user_cond->with_assocs = 1;
	if (!user_cond->assoc_cond) {
		user_cond->assoc_cond =
			xmalloc(sizeof(slurmdb_assoc_cond_t));
		user_cond->assoc_cond->with_usage = 1;
	}
	assoc_cond = user_cond->assoc_cond;

	if (!assoc_cond->cluster_list)
		assoc_cond->cluster_list = list_create(slurm_destroy_char);
	if (cluster_flag)
		slurm_addto_char_list(assoc_cond->cluster_list, cluster_flag);

	for (i = (*start); i < argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len = strlen(argv[i]);
		else
			command_len = end - 1;

		if (!end && !xstrncasecmp(argv[i], "all_clusters",
					  MAX(command_len, 1))) {
			local_cluster_flag = 1;
			continue;
		} else if (!end && !xstrncasecmp(argv[i], "group",
						 MAX(command_len, 1))) {
			group_accts = 1;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Users",
					   MAX(command_len, 1))) {
			if (!assoc_cond->user_list)
				assoc_cond->user_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list_with_case(assoc_cond->user_list,
							argv[i]+end,
							user_case_norm);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Accounts",
					 MAX(command_len, 2))
			   || !xstrncasecmp(argv[i], "Acct",
					    MAX(command_len, 4))) {
			if (!assoc_cond->acct_list)
				assoc_cond->acct_list =
					list_create(slurm_destroy_char);
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
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 1))) {
			assoc_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "TopCount",
					 MAX(command_len, 1))) {
			if (get_uint(argv[i]+end, &top_limit, "TopCount")
			    != SLURM_SUCCESS)
				exit_code = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				"Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if (!local_cluster_flag && !list_count(assoc_cond->cluster_list)) {
		/* Get the default Cluster since no cluster is specified */
		char *temp = slurm_get_cluster_name();
		if (temp)
			list_append(assoc_cond->cluster_list, temp);
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = assoc_cond->usage_start;
	end_time = assoc_cond->usage_end;
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	assoc_cond->usage_start = start_time;
	assoc_cond->usage_end = end_time;

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
		if (!xstrncasecmp("Accounts", object, MAX(command_len, 1))) {
			field->type = PRINT_USER_ACCT;
			field->name = xstrdup("Account");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Cluster", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_USER_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Energy", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_USER_ENERGY;
			field->name = xstrdup("Energy");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = slurmdb_report_print_time;
		} else if (!xstrncasecmp("Login", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_USER_LOGIN;
			field->name = xstrdup("Login");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Proper", object,
					 MAX(command_len, 1))) {
			field->type = PRINT_USER_PROPER;
			field->name = xstrdup("Proper Name");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("TresName", object,
					 MAX(command_len, 5))) {
			field->type = PRINT_USER_TRES_NAME;
			field->name = xstrdup("TRES Name");
			field->len = 14;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Used", object, MAX(command_len, 1))) {
			field->type = PRINT_USER_USED;
			field->name = xstrdup("Used");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = slurmdb_report_print_time;
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

static int _set_user_acct(void *x, void *arg)
{
	slurmdb_report_user_rec_t *orig_report_user;
	ListIterator iter = NULL;
	char *acct;

	orig_report_user = (slurmdb_report_user_rec_t *) x;
	if (orig_report_user->acct)
		return 0;

	iter = list_iterator_create(orig_report_user->acct_list);
	while ((acct = list_next(iter))) {
		if (orig_report_user->acct)
			xstrfmtcat(orig_report_user->acct, ", %s", acct);
		else
			xstrcat(orig_report_user->acct, acct);
	}
	list_iterator_destroy(iter);

	return 0;
}

static void _user_top_tres_report(slurmdb_tres_rec_t *tres,
			slurmdb_report_cluster_rec_t *slurmdb_report_cluster,
			slurmdb_report_user_rec_t *slurmdb_report_user)
{
	slurmdb_tres_rec_t *cluster_tres_rec, *tres_rec, *total_energy;
	ListIterator iter = NULL;
	print_field_t *field;
	char *tres_tmp = NULL, *tmp_char = NULL;
	struct passwd *pwd = NULL;
	int curr_inx = 1, field_count;
	uint32_t tres_energy;
	uint64_t cluster_energy_cnt = 0, user_energy_cnt = 0;

	sreport_set_tres_recs(&cluster_tres_rec, &tres_rec,
			      slurmdb_report_cluster->tres_list,
			      slurmdb_report_user->tres_list,
			      tres);

	field_count = list_count(print_fields_list);
	iter = list_iterator_create(print_fields_list);
	while ((field = list_next(iter))) {
		switch (field->type) {
		case PRINT_USER_ACCT:
			if (!slurmdb_report_user->acct)
				_set_user_acct(slurmdb_report_user, NULL);
			field->print_routine(field, slurmdb_report_user->acct,
					     (curr_inx == field_count));
			break;
		case PRINT_USER_CLUSTER:
			field->print_routine(field,
					     slurmdb_report_cluster->name,
					     (curr_inx == field_count));
			break;
		case PRINT_USER_LOGIN:
			field->print_routine(field, slurmdb_report_user->name,
					     (curr_inx == field_count));
			break;
		case PRINT_USER_PROPER:
			pwd = getpwnam(slurmdb_report_user->name);
			if (pwd) {
				tmp_char = strtok(pwd->pw_gecos, ",");
				if (!tmp_char)
					tmp_char = pwd->pw_gecos;
			}
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			tmp_char = NULL;	/* Not xmalloced */
			break;
		case PRINT_USER_USED:
			field->print_routine(field,
					     tres_rec ?
					     tres_rec->alloc_secs : 0,
					     cluster_tres_rec ?
					     cluster_tres_rec->alloc_secs : 0,
					     (curr_inx == field_count));
			break;
		case PRINT_USER_ENERGY:
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
			field->print_routine(field, user_energy_cnt,
					     cluster_energy_cnt,
					     (curr_inx == field_count));
			break;
		case PRINT_USER_TRES_NAME:
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
		case PRINT_USER_USED:
			usage_field = field;
			break;
		case PRINT_USER_ENERGY:
			energy_field = field;
			break;
		}
	}
	list_iterator_destroy(itr);

	sreport_set_usage_column_width(usage_field, energy_field,
				       slurmdb_report_cluster_list);
}

/* Merge line user/account record List into a single list of unique records */
static void _merge_user_report(List slurmdb_report_cluster_list)
{
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;
	slurmdb_report_cluster_rec_t *first_report_cluster = NULL;
	ListIterator iter = NULL;

	if (list_count(slurmdb_report_cluster_list) < 2)
		return;

	/* Put user records from all clusters into one list,
	 * removing duplicates. */
	iter = list_iterator_create(slurmdb_report_cluster_list);
	while ((slurmdb_report_cluster = list_next(iter))) {
		(void) list_for_each(slurmdb_report_cluster->user_list,
				     _set_user_acct, NULL);
		if (!first_report_cluster) {
			first_report_cluster = slurmdb_report_cluster;
			xfree(first_report_cluster->name);
			if (fed_name) {
				xstrfmtcat(first_report_cluster->name, "FED:%s",
					   fed_name);
			} else {
				first_report_cluster->name =
					xstrdup("FEDERATION");
			}
			continue;
		}
		if (!first_report_cluster->user_list) {
			first_report_cluster->user_list =
				slurmdb_report_cluster->user_list;
		} else {
			combine_user_tres(first_report_cluster->user_list,
					  slurmdb_report_cluster->user_list);
			combine_tres_list(first_report_cluster->tres_list,
					  slurmdb_report_cluster->tres_list);
		}
		list_delete_item(iter);
	}
	list_iterator_destroy(iter);
}

extern int user_top(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_user_cond_t *user_cond = xmalloc(sizeof(slurmdb_user_cond_t));
	ListIterator itr = NULL, itr2 = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List slurmdb_report_cluster_list = NULL;
	int i = 0;
	slurmdb_report_user_rec_t *slurmdb_report_user = NULL;
	slurmdb_report_cluster_rec_t *slurmdb_report_cluster = NULL;

	print_fields_list = list_create(destroy_print_field);

	_set_cond(&i, argc, argv, user_cond, format_list);

	if (!list_count(format_list)) {
		if (tres_str) {
			slurm_addto_char_list(format_list,
					      "Cl,L,P,A,TresName,Used");
		} else {
			slurm_addto_char_list(format_list, "Cl,L,P,A,U,Energy");
		}
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	if (!(slurmdb_report_cluster_list =
	     slurmdb_report_user_top_usage(db_conn, user_cond, group_accts))) {
		exit_code = 1;
		goto end_it;
	}
	if (fed_name)
		_merge_user_report(slurmdb_report_cluster_list);

	if (print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = user_cond->assoc_cond->usage_start;
		time_t my_end = user_cond->assoc_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Top %u Users %s - %s (%d secs)\n",
		       top_limit, start_char, end_char,
		       (int)(user_cond->assoc_cond->usage_end
			- user_cond->assoc_cond->usage_start));

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
		int count = 0;

		list_sort(slurmdb_report_cluster->user_list,
			  (ListCmpF)sort_user_dec);

		itr = list_iterator_create(slurmdb_report_cluster->user_list);
		while ((slurmdb_report_user = list_next(itr))) {
			slurmdb_tres_rec_t *tres;
			itr2 = list_iterator_create(tres_list);
			while ((tres = list_next(itr2))) {
				if (tres->id == NO_VAL)
					continue;
				_user_top_tres_report(tres,
					slurmdb_report_cluster,
					slurmdb_report_user);
			}
			list_iterator_destroy(itr2);
			count++;
			if (count >= top_limit)
				break;
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);

end_it:
	/* group_accts could be set in the set_cond function and needs
	 * to be cleared here, or anytime _set_cond is called.
	 */
	group_accts = 0;
	slurmdb_destroy_user_cond(user_cond);
	FREE_NULL_LIST(slurmdb_report_cluster_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}
