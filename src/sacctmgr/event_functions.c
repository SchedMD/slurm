/*****************************************************************************\
 *  event_functions.c - functions dealing with events in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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
#include <grp.h>

#include "src/common/slurm_time.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/sacctmgr/sacctmgr.h"

static uint32_t _decode_node_state(char *val)
{
	int vallen;

	xassert(val);

	vallen = strlen(val);

	if (!xstrncasecmp(val, "DRAIN", MAX(vallen, 3)))
		return NODE_STATE_DRAIN;
	else if (!xstrncasecmp(val, "FAIL", MAX(vallen, 3)))
		return NODE_STATE_FAIL;
	else if (!xstrncasecmp(val, "REBOOT^", MAX(vallen, 7)))
		return NODE_STATE_REBOOT_ISSUED;
	else if (!xstrncasecmp(val, "REBOOT", MAX(vallen, 3)))
		return NODE_STATE_REBOOT_REQUESTED;
	else {
		uint32_t j;
		for (j = 0; j < NODE_STATE_END; j++) {
			if (xstrncasecmp(node_state_string(j), val,
					 MAX(vallen, 3)) == 0){
				return j;
			}
		}
		if (j == NODE_STATE_END) {
			exit_code = 1;
			fprintf(stderr, "Invalid state: %s\n", val);
			fprintf (stderr, "Valid node states are: ");
			fprintf (stderr, "DRAIN FAIL ");
			for (j = 0; j < NODE_STATE_END; j++) {
				fprintf (stderr, "%s ",
					 node_state_string(j));
			}
			fprintf (stderr, "\n");
		}
	}

	return NO_VAL;
}

static int _addto_state_char_list_internal(list_t *char_list, char *name, void *x)
{
	uint32_t c;
	char *tmp_name = NULL;

	c = _decode_node_state(name);
	if (c == NO_VAL)
		fatal("unrecognized job state value");
	tmp_name = xstrdup_printf("%u", c);

	if (!list_find_first(char_list, slurm_find_char_in_list, tmp_name)) {
		list_append(char_list, tmp_name);
		return 1;
	} else {
		xfree(tmp_name);
		return 0;
	}
}

static int _addto_state_char_list(list_t *char_list, char *names)
{
	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	return slurm_parse_char_list(char_list, names, NULL,
				     _addto_state_char_list_internal);
}

static uint32_t _parse_cond_flags(const char *flags_str)
{
	uint32_t flags = 0;
	char *tmp_flags, *flag, *save_ptr = NULL;

	tmp_flags = xstrdup(flags_str);

	flag = strtok_r(tmp_flags, ",", &save_ptr);
	while (flag) {
		if (!xstrcasecmp(flag, "OPEN")) {
			flags |= SLURMDB_EVENT_COND_OPEN;
		} else {
			error("Unknown condition flag %s", flag);
			exit_code = 1;
		}

		flag = strtok_r(NULL, ",", &save_ptr);
	}

	xfree(tmp_flags);

	return flags;
}

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_event_cond_t *event_cond,
		     list_t *format_list)
{
	int i, end = 0;
	int set = 0;
	int command_len = 0;
	int local_cluster_flag = 0;
	int all_time_flag = 0;

	if (!event_cond->cluster_list)
		event_cond->cluster_list = list_create(xfree_ptr);
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
					  MAX(command_len, 5))) {
			local_cluster_flag = 1;
		} else if (!end && !xstrncasecmp(argv[i], "all_time",
						 MAX(command_len, 5))) {
			all_time_flag = 1;
		} else if (!end && !xstrncasecmp(argv[i], "where",
						 MAX(command_len, 5))) {
			continue;
		} else if (!end || (!xstrncasecmp(argv[i], "Events",
						  MAX(command_len, 1)))) {
			list_itr_t *itr = NULL;
			list_t *tmp_list = list_create(xfree_ptr);
			char *temp = NULL;

			if (slurm_addto_char_list(tmp_list,
						 argv[i]+end))
				set = 1;

			/* check to make sure user gave ints here */
			itr = list_iterator_create(tmp_list);
			while ((temp = list_next(itr))) {
				if (!xstrncasecmp("Node", temp,
						  MAX(strlen(temp), 1))) {
					if (event_cond->event_type)
						event_cond->event_type =
							SLURMDB_EVENT_ALL;
					else
						event_cond->event_type =
							SLURMDB_EVENT_NODE;
				} else if (!xstrncasecmp("Cluster", temp,
						MAX(strlen(temp), 1))) {
					if (event_cond->event_type)
						event_cond->event_type =
							SLURMDB_EVENT_ALL;
					else
						event_cond->event_type =
							SLURMDB_EVENT_CLUSTER;
				} else {
					exit_code=1;
					fprintf(stderr,
						" Unknown event type: '%s'  "
						"Valid events are Cluster "
						"and Node.\n",
						temp);

				}
			}
			list_iterator_destroy(itr);
			FREE_NULL_LIST(tmp_list);
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 2))) {
			if (!event_cond->cluster_list)
				event_cond->cluster_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(event_cond->cluster_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "CondFlags",
					 MAX(command_len, 2))) {
			event_cond->cond_flags = _parse_cond_flags(argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 1))) {
			event_cond->period_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "MinCpus",
					 MAX(command_len, 2))) {
			if (get_uint(argv[i]+end, &event_cond->cpus_min,
			    "MinCpus") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxCpus",
					 MAX(command_len, 2))) {
			if (get_uint(argv[i]+end, &event_cond->cpus_max,
			    "MaxCpus") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Nodes",
					 MAX(command_len, 1))) {
			xfree(event_cond->node_list);
			event_cond->node_list = xstrdup(argv[i]+end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Reason",
					 MAX(command_len, 1))) {
			if (!event_cond->reason_list)
				event_cond->reason_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(event_cond->reason_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 4))) {
			event_cond->period_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "States",
					 MAX(command_len, 4))) {
			if (!event_cond->state_list)
				event_cond->state_list = list_create(xfree_ptr);
			if (_addto_state_char_list(event_cond->state_list,
						  argv[i]+end) > 0) {
				event_cond->event_type = SLURMDB_EVENT_NODE;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "User",
					 MAX(command_len, 1))) {
			if (!event_cond->reason_uid_list)
				event_cond->reason_uid_list =
					list_create(xfree_ptr);
			if (slurm_addto_id_char_list(
				event_cond->reason_uid_list,
				argv[i]+end, 0) > 0) {
				event_cond->event_type = SLURMDB_EVENT_NODE;
				set = 1;
			} else {
				exit_code=1;
			}
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}
	(*start) = i;

	if (!local_cluster_flag && !list_count(event_cond->cluster_list))
		list_append(event_cond->cluster_list, xstrdup(slurm_conf.cluster_name));

	if (!all_time_flag && !event_cond->period_start) {
		event_cond->period_start = time(NULL);
		if (!event_cond->state_list) {
			struct tm start_tm;

			if (!localtime_r(&event_cond->period_start,
					 &start_tm)) {
				fprintf(stderr,
					" Couldn't get localtime from %ld",
					(long)event_cond->period_start);
				exit_code=1;
				return 0;
			}
			start_tm.tm_sec = 0;
			start_tm.tm_min = 0;
			start_tm.tm_hour = 0;
			start_tm.tm_mday--;
			event_cond->period_start = slurm_mktime(&start_tm);
		}
	}

	return set;
}


extern int sacctmgr_list_event(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_event_cond_t *event_cond =
		xmalloc(sizeof(slurmdb_event_cond_t));
	list_t *event_list = NULL;
	slurmdb_event_rec_t *event = NULL;
	int i = 0;
	list_itr_t *itr = NULL;
	list_itr_t *itr2 = NULL;
	int field_count = 0;

	print_field_t *field = NULL;

	list_t *format_list;
	list_t *print_fields_list; /* types are of print_field_t */

	/* If we don't have any arguments make sure we set up the
	   time correctly for just the past day.
	*/
	if (argc == 0) {
		struct tm start_tm;
		event_cond->period_start = time(NULL);

		if (!localtime_r(&event_cond->period_start, &start_tm)) {
			fprintf(stderr,
				" Couldn't get localtime from %ld",
				(long)event_cond->period_start);
			exit_code = 1;
			slurmdb_destroy_event_cond(event_cond);
			return 0;
		}
		start_tm.tm_sec = 0;
		start_tm.tm_min = 0;
		start_tm.tm_hour = 0;
		start_tm.tm_mday--;
		event_cond->period_start = slurm_mktime(&start_tm);
	}

	format_list = list_create(xfree_ptr);
	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, event_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_event_cond(event_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}

	if (!list_count(format_list)) {
		if (event_cond->event_type == SLURMDB_EVENT_CLUSTER)
			slurm_addto_char_list(format_list,
					      "Cluster,TRES,Start,End,"
					      "ClusterNodes");
		else
			slurm_addto_char_list(format_list,
					      "Cluster,NodeName,Start,"
					      "End,State,Reason,User");
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	event_list = slurmdb_events_get(db_conn, event_cond);
	slurmdb_destroy_event_cond(event_cond);

	if (!event_list) {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}
	itr = list_iterator_create(event_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((event = list_next(itr))) {
		int curr_inx = 1;
		char tmp[20], *tmp_char;
		uint64_t tmp_uint64;
		time_t newend = event->period_end;

		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_CLUSTER:
				field->print_routine(field, event->cluster,
						     (curr_inx == field_count));
				break;
			case PRINT_CLUSTER_NODES:
				field->print_routine(
					field,
					event->cluster_nodes,
					(curr_inx == field_count));
				break;
			case PRINT_CPUS:
				convert_num_unit(
					(float)slurmdb_find_tres_count_in_string(
						event->tres_str, TRES_CPU),
					tmp, sizeof(tmp), UNIT_NONE, NO_VAL,
					CONVERT_NUM_UNIT_EXACT);

				field->print_routine(
					field,
					tmp,
					(curr_inx == field_count));
				break;
			case PRINT_DURATION:
				if (!newend)
					newend = time(NULL);
				tmp_uint64 = newend - event->period_start;
				field->print_routine(
					field,
					&tmp_uint64,
					(curr_inx == field_count));
				break;
			case PRINT_TIMEEND:
				field->print_routine(field,
						     &event->period_end,
						     (curr_inx == field_count));
				break;
			case PRINT_EVENTRAW:
				field->print_routine(field, &event->event_type,
						     (curr_inx == field_count));
				break;
			case PRINT_EVENT:
				if (event->event_type == SLURMDB_EVENT_CLUSTER)
					tmp_char = "Cluster";
				else if (event->event_type ==
					 SLURMDB_EVENT_NODE)
					tmp_char = "Node";
				else
					tmp_char = "Unknown";
				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				break;
			case PRINT_NODENAME:
				field->print_routine(field,
						     event->node_name,
						     (curr_inx == field_count));
				break;
			case PRINT_TIMESTART:
				field->print_routine(field,
						     &event->period_start,
						     (curr_inx == field_count));
				break;
			case PRINT_REASON:
				field->print_routine(field, event->reason,
						     (curr_inx == field_count));
				break;
			case PRINT_STATERAW:
				field->print_routine(field, &event->state,
						     (curr_inx == field_count));
				break;
			case PRINT_STATE:
				if (event->event_type == SLURMDB_EVENT_CLUSTER)
					tmp_char = NULL;
				else
					tmp_char = node_state_string_compact(
						event->state);

				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				break;
			case PRINT_TRES:
				sacctmgr_initialize_g_tres_list();

				tmp_char = slurmdb_make_tres_string_from_simple(
					event->tres_str, g_tres_list, NO_VAL,
					CONVERT_NUM_UNIT_EXACT, 0, NULL);

				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			case PRINT_USER:
				if (event->reason_uid != NO_VAL) {
					tmp_char = xstrdup_printf(
						"%s(%u)",
						uid_to_string_cached(
							event->reason_uid),
						event->reason_uid);
				} else
					tmp_char = NULL;
				field->print_routine(field, tmp_char,
						     (curr_inx == field_count));
				xfree(tmp_char);
				break;
			default:
				field->print_routine(field, NULL,
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
	FREE_NULL_LIST(event_list);
	FREE_NULL_LIST(print_fields_list);
	return rc;
}
