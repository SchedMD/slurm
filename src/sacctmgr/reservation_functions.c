/*****************************************************************************\
 *  reservation_functions.c - functions dealing with RESERVATION in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2015 SchedMD LLC.
 *  Written by David Bigagli <david@schedmd.com>
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

#include "src/common/slurm_time.h"
#include "src/sacctmgr/sacctmgr.h"
#include "src/common/assoc_mgr.h"

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_reservation_cond_t *reservation_cond,
		     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;

	if (!reservation_cond) {
		exit_code=1;
		fprintf(stderr, "No reservation_cond given");
		return -1;
	}

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

		if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "where",
						 MAX(command_len, 5))) {
			continue;
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 1))) {
			if (!reservation_cond->cluster_list) {
				reservation_cond->cluster_list =
					list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(reservation_cond->cluster_list,
						  argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "End",
					 MAX(command_len, 1))) {
			reservation_cond->time_end =
				parse_time(argv[i]+end, 1);
			if (errno == ESLURM_INVALID_TIME_VALUE)
				exit_code = 1;
			else
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Ids",
					 MAX(command_len, 1))) {
			if (!reservation_cond->id_list) {
				reservation_cond->id_list =
					list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(reservation_cond->id_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Names",
					 MAX(command_len, 2))) {
			if (!reservation_cond->name_list) {
				reservation_cond->name_list =
					list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(reservation_cond->name_list,
						  argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Nodes",
					 MAX(command_len, 2))) {
			xfree(reservation_cond->nodes);
			reservation_cond->nodes = strip_quotes(
				argv[i]+end, NULL, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 5))) {
			reservation_cond->time_start =
				parse_time(argv[i]+end, 1);
			if (errno == ESLURM_INVALID_TIME_VALUE)
				exit_code = 1;
			else
				set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
		}
	}

	(*start) = i;

	if (set)
		return 1;

	return 0;
}

/* sacctmgr_list_reservation()
 */
int sacctmgr_list_reservation(int argc, char **argv)
{
        List reservation_list;
        ListIterator itr;
	ListIterator itr2;
	List format_list;
	List print_fields_list;
        slurmdb_reservation_cond_t *reservation_cond =
		xmalloc(sizeof(slurmdb_reservation_cond_t));
        slurmdb_reservation_rec_t *reservation;
	int field_count, i;
	print_field_t *field;
	char *tmp_char;

 	/* If we don't have any arguments make sure we set up the
	 * time correctly for just the past day. */
	if (argc == 0) {
                struct tm start_tm;
		reservation_cond->time_start = time(NULL);

                if (!localtime_r(&reservation_cond->time_start, &start_tm)) {
                        fprintf(stderr,
                                " Couldn't get localtime from %ld",
                                (long)reservation_cond->time_start);
			slurmdb_destroy_reservation_cond(reservation_cond);
			exit_code = 1;
                        return 0;
                }
                start_tm.tm_sec = 0;
                start_tm.tm_min = 0;
                start_tm.tm_hour = 0;
                start_tm.tm_mday--;
                reservation_cond->time_start = slurm_mktime(&start_tm);
        }

	format_list = list_create(xfree_ptr);
   	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, reservation_cond, format_list);
	}

	if (reservation_cond->nodes && !reservation_cond->cluster_list) {
		char *warning = xstrdup_printf(
			"If requesting nodes you must also request the cluster.\nWould you like to use the local cluster of '%s'?",
			slurm_conf.cluster_name);

		if (!commit_check(warning)) {
			exit_code = 1;
		} else {
			reservation_cond->cluster_list = list_create(xfree_ptr);
			list_append(reservation_cond->cluster_list,
				    xstrdup(slurm_conf.cluster_name));
		}
		xfree(warning);
	}

	if (exit_code) {
		slurmdb_destroy_reservation_cond(reservation_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}

	if (!list_count(format_list)) {
		/* Append to the format list the fields
		 * we want to print, these are the data structure
		 * members of the type returned by slurmdbd
		 */
		slurm_addto_char_list(format_list,
				      "Cluster,Name%15,TRES%30,"
				      "TimeStart,TimeEnd,Unused");
	}

	reservation_list = slurmdb_reservations_get(
		db_conn, reservation_cond);
	slurmdb_destroy_reservation_cond(reservation_cond);

	if (!reservation_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}


	/* Process the format list creating a list of
	 * print field_t structures
	 */
	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

        itr = list_iterator_create(reservation_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);
	field_count = list_count(print_fields_list);

	/* For each reservation prints the data structure members
	 */
        while ((reservation = list_next(itr))) {
		int curr_inx = 1;
		while ((field = list_next(itr2))) {
			switch (field->type) {
			case PRINT_ASSOC_NAME:
				field->print_routine(
					field,
					reservation->assocs,
					(curr_inx == field_count));
				break;
			case PRINT_CLUSTER:
				field->print_routine(
					field,
					reservation->cluster,
					(curr_inx == field_count));
				break;
			case PRINT_FLAGS:
			{
				reserve_info_t resv_info = {
					.flags = reservation->flags,
				};

				tmp_char = reservation_flags_string(
					&resv_info);
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			}
			case PRINT_ID:
				field->print_routine(field,
						     &reservation->id,
						     (curr_inx == field_count));
				break;
			case PRINT_NAME:
				field->print_routine(field,
						     reservation->name,
						     (curr_inx == field_count));
				break;
			case PRINT_NODENAME:
				field->print_routine(
					field,
					reservation->nodes,
					(curr_inx == field_count));
				break;
			case PRINT_NODEINX:
				field->print_routine(
					field,
					reservation->node_inx,
					(curr_inx == field_count));
				break;
			case PRINT_TIMEEND:
				field->print_routine(
					field,
					&reservation->time_end,
					(curr_inx == field_count));
				break;
			case PRINT_TIMESTART:
				field->print_routine(
					field,
					&reservation->time_start,
					(curr_inx == field_count));
				break;
			case PRINT_TRES:
				sacctmgr_initialize_g_tres_list();

				tmp_char = slurmdb_make_tres_string_from_simple(
					reservation->tres_str, g_tres_list,
					NO_VAL, CONVERT_NUM_UNIT_EXACT,
					0, NULL);
				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				xfree(tmp_char);
				break;
			case PRINT_COMMENT:
				field->print_routine(
					field,
					reservation->comment,
					(curr_inx == field_count));
				break;
			case PRINT_UNUSED:
				field->print_routine(
					field,
					&reservation->unused_wall,
					(curr_inx == field_count));
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
        }
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	FREE_NULL_LIST(reservation_list);
	FREE_NULL_LIST(print_fields_list);

        return 0;
}
