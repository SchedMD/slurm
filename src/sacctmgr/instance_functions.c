/*****************************************************************************\
 *  instance_functions.c - functions dealing with instances in the
 *			   accounting system.
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Ben Glines <ben.glines@schedmd.com>
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
#include "src/interfaces/data_parser.h"

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_instance_cond_t *instance_cond,
		     List format_list)
{
	int i, end = 0;
	int set = 0;
	int command_len = 0;

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

		if (!end &&
		    !xstrncasecmp(argv[i], "where", MAX(command_len, 5))) {
			continue;
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 2))) {
			if (slurm_addto_char_list(instance_cond->cluster_list,
						  argv[i] + end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "End", MAX(command_len, 2))) {
			instance_cond->time_end = parse_time(argv[i] + end, 1);
			set = 1;
		} else if (!strncasecmp(argv[i], "Extra",
					MAX(command_len, 2))) {
			if (!instance_cond->extra_list)
				instance_cond->extra_list = list_create(
					xfree_ptr);
			if (slurm_addto_char_list(instance_cond->extra_list,
						  argv[i] + end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list,
						      argv[i] + end);
		} else if (!strncasecmp(argv[i], "InstanceId",
					MAX(command_len, 9))) {
			if (!instance_cond->instance_id_list)
				instance_cond->instance_id_list = list_create(
					xfree_ptr);
			if (slurm_addto_char_list(
				    instance_cond->instance_id_list,
				    argv[i] + end))
				set = 1;
		} else if (!strncasecmp(argv[i], "InstanceType",
					MAX(command_len, 9))) {
			if (!instance_cond->instance_type_list)
				instance_cond->instance_type_list = list_create(
					xfree_ptr);
			if (slurm_addto_char_list(
				    instance_cond->instance_type_list,
				    argv[i] + end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Nodes",
					 MAX(command_len, 1))) {
			xfree(instance_cond->node_list);
			instance_cond->node_list = xstrdup(argv[i] + end);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Start",
					 MAX(command_len, 4))) {
			instance_cond->time_start = parse_time(argv[i] + end, 1);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}
	(*start) = i;


	return set;
}

extern int sacctmgr_list_instance(int argc, char **argv)
{
	int field_count = 0;
	int rc = SLURM_SUCCESS;
	int i = 0;
	slurmdb_instance_cond_t *instance_cond = xmalloc(
		sizeof(slurmdb_instance_cond_t));
	slurmdb_instance_rec_t *instance = NULL;
	List format_list; /* list of char * */
	List instance_list = NULL; /* list of slurmdb_instance_rec_t */
	List print_fields_list; /* list of print_field_t */
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	print_field_t *field = NULL;

	instance_cond->cluster_list = list_create(xfree_ptr);
	format_list = list_create(xfree_ptr);

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5)) ||
		    !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, instance_cond, format_list);
	}

	/* Set default time_start to the start of previous day */
	if (!instance_cond->time_start) {
		instance_cond->time_start = time(NULL);
		struct tm start_tm;

		if (!localtime_r(&instance_cond->time_start, &start_tm)) {
			fprintf(stderr, " Couldn't get localtime from %ld",
				(long) instance_cond->time_start);
			exit_code = 1;
			slurmdb_destroy_instance_cond(instance_cond);
			return SLURM_ERROR;
		}
		start_tm.tm_sec = 0;
		start_tm.tm_min = 0;
		start_tm.tm_hour = 0;
		start_tm.tm_mday--;
		instance_cond->time_start = slurm_mktime(&start_tm);
	}

	/*
	 * Set default cluster to the local cluster defined in slurm.conf
	 */
	if (!list_count(instance_cond->cluster_list)) {
		list_append(instance_cond->cluster_list,
			    xstrdup(slurm_conf.cluster_name));
	}

	/* Set default format */
	if (!list_count(format_list)) {
		slurm_addto_char_list(format_list, "Cluster,NodeName,Start,"
						   "End,InstanceId,"
						   "InstanceType,Extra");
	}

	if (exit_code) {
		slurmdb_destroy_instance_cond(instance_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	instance_list = slurmdb_instances_get(db_conn, instance_cond);
	slurmdb_destroy_instance_cond(instance_cond);

	if (mime_type) {
		DATA_DUMP_CLI_SINGLE(OPENAPI_INSTANCES_RESP, instance_list,
				     argc, argv, db_conn, mime_type,
				     data_parser, rc);
		FREE_NULL_LIST(print_fields_list);
		FREE_NULL_LIST(instance_list);
		return rc;
	}

	if (!instance_list) {
		exit_code = 1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(instance_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while ((instance = list_next(itr))) {
		int curr_inx = 1;
		uint64_t tmp_uint64;
		time_t newend = instance->time_end;

		while ((field = list_next(itr2))) {
			switch (field->type) {
			case PRINT_CLUSTER:
				field->print_routine(field, instance->cluster,
						     (curr_inx == field_count));
				break;
			case PRINT_EXTRA:
				field->print_routine(field,
						     instance->extra,
						     (curr_inx == field_count));
				break;
			case PRINT_DURATION:
				if (!newend)
					newend = time(NULL);
				tmp_uint64 = newend - instance->time_start;
				field->print_routine(field, &tmp_uint64,
						     (curr_inx == field_count));
				break;
			case PRINT_INSTANCE_ID:
				field->print_routine(field,
						     instance->instance_id,
						     (curr_inx == field_count));
				break;
			case PRINT_INSTANCE_TYPE:
				field->print_routine(field,
						     instance->instance_type,
						     (curr_inx == field_count));
				break;
			case PRINT_NODENAME:
				field->print_routine(field, instance->node_name,
						     (curr_inx == field_count));
				break;
			case PRINT_TIMEEND:
				field->print_routine(field, &instance->time_end,
						     (curr_inx == field_count));
				break;
			case PRINT_TIMESTART:
				field->print_routine(field,
						     &instance->time_start,
						     (curr_inx == field_count));
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
	FREE_NULL_LIST(instance_list);
	FREE_NULL_LIST(print_fields_list);
	return rc;
}
