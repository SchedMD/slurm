/*****************************************************************************\
 *  resv_reports.c - functions for generating reservation reports
 *                       from accounting infrastructure.
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

#include "cluster_reports.h"
#include "resv_reports.h"
enum {
	PRINT_RESV_ASSOCS,
	PRINT_RESV_CLUSTER,
	PRINT_RESV_END,
	PRINT_RESV_FLAGS,
	PRINT_RESV_ID,
	PRINT_RESV_NAME,
	PRINT_RESV_NODES,
	PRINT_RESV_START,
	PRINT_RESV_TIME,
	PRINT_RESV_TRES_ALLOC,
	PRINT_RESV_TRES_CNT,
	PRINT_RESV_TRES_IDLE,
	PRINT_RESV_TRES_NAME,
	PRINT_RESV_TRES_USAGE,
};


static List print_fields_list = NULL; /* types are of print_field_t */

static int _set_resv_cond(int *start, int argc, char **argv,
			  slurmdb_reservation_cond_t *resv_cond,
			  List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int command_len = 0;

	if (!resv_cond) {
		error("We need an slurmdb_reservation_cond to call this");
		return SLURM_ERROR;
	}

	resv_cond->with_usage = 1;

	if (!resv_cond->cluster_list)
		resv_cond->cluster_list = list_create(xfree_ptr);
	if (cluster_flag)
		slurm_addto_char_list(resv_cond->cluster_list, cluster_flag);

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!end && !xstrncasecmp(argv[i], "all_clusters",
					  MAX(command_len, 1))) {
			local_cluster_flag = 1;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Names",
					   MAX(command_len, 1))) {
			if (!resv_cond->name_list)
				resv_cond->name_list = list_create(xfree_ptr);
			slurm_addto_char_list(resv_cond->name_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 1))) {
			slurm_addto_char_list(resv_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 1))) {
			resv_cond->time_end = parse_time(argv[i]+end, 1);
			resv_cond->time_end = sanity_check_endtime(resv_cond->time_end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Flags",
					 MAX(command_len, 2))) {
			resv_cond->flags = parse_resv_flags(argv[i]+end,
							    __func__, NULL);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 2))) {
			if (format_list)
				slurm_addto_char_list(format_list,
						      argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Ids",
					 MAX(command_len, 1))) {
			if (!resv_cond->id_list)
				resv_cond->id_list = list_create(xfree_ptr);
			slurm_addto_char_list(resv_cond->id_list, argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Nodes",
					 MAX(command_len, 1))) {
			if (resv_cond->nodes) {
				error("You already specified nodes '%s' "
				      " combine your request into 1 nodes=.",
				      resv_cond->nodes);
				exit_code = 1;
				break;
			}
			resv_cond->nodes = xstrdup(argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 1))) {
			resv_cond->time_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr," Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if (!local_cluster_flag && !list_count(resv_cond->cluster_list)) {
		/* Get the default Cluster since no cluster is specified */
		list_append(resv_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = resv_cond->time_start;
	end_time = resv_cond->time_end;
	slurmdb_report_set_start_end_time(&start_time, &end_time);
	resv_cond->time_start = start_time;
	resv_cond->time_end = end_time;

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
			" we need a format list to set up the print.\n");
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
			newlen = atoi(tmp_char + 1);
			tmp_char[0] = '\0';
		}

		command_len = strlen(object);

		field = xmalloc(sizeof(print_field_t));
		if (!xstrncasecmp("allocated", object,
				  MAX(command_len, 2))) {
			field->type = PRINT_RESV_TRES_ALLOC;
			field->name = xstrdup("Allocated");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 29;
			else
				field->len = 20;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Associations",
					 object, MAX(command_len, 2))) {
			field->type = PRINT_RESV_ASSOCS;
			field->name = xstrdup("Associations");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Cluster", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("End", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_END;
			field->name = xstrdup("End");
			field->len = 19;
			field->print_routine = print_fields_date;
		} else if (!xstrncasecmp("Flags", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_FLAGS;
			field->name = xstrdup("Flags");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Idle", object, MAX(command_len, 1))) {
			field->type = PRINT_RESV_TRES_IDLE;
			field->name = xstrdup("Idle");
			if (time_format == SLURMDB_REPORT_TIME_SECS_PER
			   || time_format == SLURMDB_REPORT_TIME_MINS_PER
			   || time_format == SLURMDB_REPORT_TIME_HOURS_PER)
				field->len = 29;
			else
				field->len = 20;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Name", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_NAME;
			field->name = xstrdup("Name");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("Nodes", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_NODES;
			field->name = xstrdup("Nodes");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("ReservationId", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_ID;
			field->name = xstrdup("Id");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if (!xstrncasecmp("Start", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_START;
			field->name = xstrdup("Start");
			field->len = 19;
			field->print_routine = print_fields_date;
		} else if (!xstrncasecmp("TotalTime", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_TIME;
			field->name = xstrdup("TotalTime");
			field->len = 9;
			field->print_routine = print_fields_time_from_secs;
		} else if (!xstrncasecmp("TresCount", object,
					 MAX(command_len, 5)) ||
			   !xstrncasecmp("CpuCount", object,
					 MAX(command_len, 2)) ||
			   !xstrncasecmp("count", object,
					 MAX(command_len, 2))) {
			field->type = PRINT_RESV_TRES_CNT;
			field->name = xstrdup("TRES count");
			field->len = 10;
			field->print_routine = print_fields_uint;
		} else if (!xstrncasecmp("TresName", object,
					 MAX(command_len, 5))) {
			field->type = PRINT_RESV_TRES_NAME;
			field->name = xstrdup("TRES Name");
			field->len = 14;
			field->print_routine = print_fields_str;
		} else if (!xstrncasecmp("TresTime", object,
					 MAX(command_len, 2)) ||
			   !xstrncasecmp("CpuTime", object,
					 MAX(command_len, 5))) {
			field->type = PRINT_RESV_TRES_USAGE;
			field->name = xstrdup("TRES Time");
			field->len = 9;
			field->print_routine = print_fields_time_from_secs;
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

static List _get_resv_list(int argc, char **argv,
			   char *report_name, List format_list)
{
	slurmdb_reservation_cond_t *resv_cond =
		xmalloc(sizeof(slurmdb_reservation_cond_t));
	int i = 0;
	List resv_list = NULL;

	resv_cond->with_usage = 1;

	_set_resv_cond(&i, argc, argv, resv_cond, format_list);

	resv_list = slurmdb_reservations_get(db_conn, resv_cond);
	if (!resv_list) {
		exit_code = 1;
		fprintf(stderr, " Problem with reservation query.\n");
		return NULL;
	}

	if (print_fields_have_header) {
		char start_char[256];
		char end_char[256];
		time_t my_start = resv_cond->time_start;
		time_t my_end = resv_cond->time_end-1;

		slurm_make_time_str(&my_start,
				    start_char, sizeof(start_char));
		slurm_make_time_str(&my_end,
				    end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("%s %s - %s\n",
		       report_name, start_char, end_char);
		switch (time_format) {
		case SLURMDB_REPORT_TIME_PERCENT:
			printf("Usage reported in %s\n", time_format_string);
			break;
		default:
			printf("Usage reported in TRES %s\n",
			       time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	slurmdb_destroy_reservation_cond(resv_cond);

	return resv_list;
}

static void _resv_tres_report(slurmdb_reservation_rec_t *resv_ptr,
			      slurmdb_tres_rec_t *resv_tres)
{
	uint64_t idle_secs = 0, total_reported = 0;
	uint64_t tres_alloc_cnt = 0, tres_alloc_secs = 0;
	int curr_inx = 1;
	char *tmp_char = NULL, *tres_tmp = NULL;
	slurmdb_tres_rec_t *tres_rec;
	print_field_t *field;
	int field_count = 0;
	ListIterator iter = NULL;
	uint64_t total_time = 0;

	if (resv_ptr->time_end <= resv_ptr->time_start)
		return;
	total_time = resv_ptr->time_end - resv_ptr->time_start;

	/*
	 * Need to get allocated from reservation which is in
	 * tres_str/resv_tres. resv_ptr->tres_list contains the accumulated
	 * tres seconds that were used by jobs that ran in the reservation. The
	 * tres_list will have more tres types than exist in the reservations
	 * tres because only cpu, licenses and bb are supported tres types than
	 * can be reserved.
	 */
	if (resv_ptr->tres_list &&
	    (tres_rec = list_find_first(resv_ptr->tres_list,
					slurmdb_find_tres_in_list,
					&resv_tres->id))) {
		tres_alloc_secs = tres_rec->alloc_secs;
	}

	tres_alloc_cnt  = resv_tres->count;
	total_reported  = (uint64_t)(total_time * tres_alloc_cnt);

	/*
	 * With Flex reservations you can have more time that possible in the
	 * allocation.
	 */
	if (total_reported > tres_alloc_secs)
		idle_secs = total_reported - tres_alloc_secs;

	field_count = list_count(print_fields_list);
	iter = list_iterator_create(print_fields_list);
	while ((field = list_next(iter))) {
		switch (field->type) {
		case PRINT_RESV_NAME:
			field->print_routine(field, resv_ptr->name,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_CLUSTER:
			field->print_routine(field, resv_ptr->cluster,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_TRES_CNT:
			field->print_routine(field, &tres_alloc_cnt,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_ID:
			field->print_routine(field, &resv_ptr->id,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_TRES_ALLOC:
			tmp_char = sreport_get_time_str(tres_alloc_secs,
							total_reported);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_RESV_TRES_IDLE:
			tmp_char = sreport_get_time_str(idle_secs,
							total_reported);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_RESV_NODES:
			field->print_routine(field, resv_ptr->nodes,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_ASSOCS:
			field->print_routine(field, resv_ptr->assocs,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_START:
			field->print_routine(field, &resv_ptr->time_start,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_END:
			field->print_routine(field, &resv_ptr->time_end,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_FLAGS:
		{
			reserve_info_t resv_info = {
				.flags = resv_ptr->flags,
			};

			tmp_char = reservation_flags_string(&resv_info);
			field->print_routine(field, tmp_char,
					     (curr_inx == field_count));
			tmp_char = NULL;
			break;
		}
		case PRINT_RESV_TIME:
			field->print_routine(field, &total_time,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_TRES_NAME:
			xstrfmtcat(tres_tmp, "%s%s%s",
				   resv_tres->type,
				   resv_tres->name ? "/" : "",
				   resv_tres->name ? resv_tres->name : "");

			field->print_routine(field, tres_tmp,
					     (curr_inx == field_count));
			xfree(tres_tmp);
			break;
		case PRINT_RESV_TRES_USAGE:
			field->print_routine(field, &total_reported,
					     (curr_inx == field_count));
			break;
		default:
			field->print_routine(field, NULL,
					     (curr_inx == field_count));
			break;
		}
		curr_inx++;
		xfree(tmp_char);
	}
	list_iterator_reset(iter);
	printf("\n");
}

extern int resv_utilization(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	ListIterator resv_itr = NULL;
	slurmdb_reservation_rec_t *resv_ptr = NULL;
	List resv_list = NULL;
	List req_tres_list = tres_list;

	List format_list = list_create(xfree_ptr);

	print_fields_list = list_create(destroy_print_field);

	if (!(resv_list = _get_resv_list(argc, argv,
					 "Reservation Utilization",
					 format_list)))
		goto end_it;

	if (!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				      "Cl,name,start,end,TresName,al,i");
	}

	_setup_print_fields_list(format_list);
	FREE_NULL_LIST(format_list);

	print_fields_header(print_fields_list);

	/*
	 * Don't compress duplicate reservations into a single record.
	 * If a reservation is updated, it needs to be shown as its own record.
	 */

	if (!tres_str) {
		/*
		 * If the user didn't request specific TRES types then display
		 * the all TRES types that are on the reservation. Use the
		 * g_tres_list as it is the unaltered list from the database.
		 */
		req_tres_list = g_tres_list;
	}

	resv_itr = list_iterator_create(resv_list);
	while ((resv_ptr = list_next(resv_itr))) {
		List resv_tres_list = NULL;
		ListIterator tres_itr;
		slurmdb_tres_rec_t *resv_tres, *req_tres;

		slurmdb_tres_list_from_string(&resv_tres_list,
					      resv_ptr->tres_str,
					      TRES_STR_FLAG_NONE);
		if (!resv_tres_list)
			continue;

		tres_itr = list_iterator_create(resv_tres_list);
		while ((resv_tres = list_next(tres_itr))) {
			/* see if it is in the requested tres list */
			if (!(req_tres = list_find_first(
						req_tres_list,
						slurmdb_find_tres_in_list,
						&resv_tres->id))) {
				debug2("TRES id %d is not in the requested TRES list",
				       resv_tres->id);
				continue;
			}

			/*
			 * The resveration's tres doesn't have the name or type
			 * on it. The req_tres tres came from the database.
			 */
			xfree(resv_tres->type);
			xfree(resv_tres->name);
			resv_tres->type = xstrdup(req_tres->type);
			resv_tres->name = xstrdup(req_tres->name);

			_resv_tres_report(resv_ptr, resv_tres);
		}
		list_iterator_destroy(tres_itr);
		FREE_NULL_LIST(resv_tres_list);
	}
	list_iterator_destroy(resv_itr);

end_it:
	FREE_NULL_LIST(resv_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}
