/*****************************************************************************\
 *  association_functions.c - functions dealing with associations in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "src/sacctmgr/sacctmgr.h"

static int _set_cond(int *start, int argc, char *argv[],
		     slurmdb_association_cond_t *assoc_cond,
		     List format_list)
{
	int i, end = 0;
	int set = 0;
	int command_len = 0;
	int option = 0;
	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if (!end && !strncasecmp (argv[i], "OnlyDefaults",
					  MAX(command_len, 2))) {
			assoc_cond->only_defs = 1;
			set = 1;
		} else if (!end && !strncasecmp (argv[i], "Tree",
					  MAX(command_len, 4))) {
			tree_display = 1;
		} else if (!end && !strncasecmp (argv[i], "WithDeleted",
						 MAX(command_len, 5))) {
			assoc_cond->with_deleted = 1;
		} else if (!end &&
			   !strncasecmp (argv[i], "WithRawQOSLevel",
					 MAX(command_len, 5))) {
			assoc_cond->with_raw_qos = 1;
		} else if (!end &&
			   !strncasecmp (argv[i], "WithSubAccounts",
					 MAX(command_len, 5))) {
			assoc_cond->with_sub_accts = 1;
		} else if (!end && !strncasecmp (argv[i], "WOPInfo",
						 MAX(command_len, 4))) {
			assoc_cond->without_parent_info = 1;
		} else if (!end && !strncasecmp (argv[i], "WOPLimits",
						 MAX(command_len, 4))) {
			assoc_cond->without_parent_limits = 1;
		} else if (!end && !strncasecmp (argv[i], "WOLimits",
						 MAX(command_len, 3))) {
			assoc_cond->without_parent_limits = 1;
		} else if (!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end || !strncasecmp (argv[i], "Ids",
						MAX(command_len, 1))
			  || !strncasecmp (argv[i], "Associations",
					   MAX(command_len, 2))) {
			ListIterator itr = NULL;
			char *temp = NULL;
			uint32_t id = 0;

			if (!assoc_cond->id_list)
				assoc_cond->id_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->id_list,
					      argv[i]+end);
			/* check to make sure user gave ints here */
			itr = list_iterator_create(assoc_cond->id_list);
			while ((temp = list_next(itr))) {
				if (get_uint(temp, &id, "AssocId")
				    != SLURM_SUCCESS) {
					exit_code = 1;
					list_delete_item(itr);
				}
			}
			list_iterator_destroy(itr);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list,
						      argv[i]+end);
		} else if (!(set = sacctmgr_set_association_cond(
				    assoc_cond, argv[i], argv[i]+end,
				    command_len, option)) || exit_code) {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}

	(*start) = i;

	return set;
}

extern bool sacctmgr_check_default_qos(uint32_t qos_id,
				       slurmdb_association_cond_t *assoc_cond)
{
	char *object = NULL;
	ListIterator itr;
	slurmdb_association_rec_t *assoc;
	List no_access_list = NULL;
	List assoc_list = NULL;

	if (qos_id == NO_VAL)
		return true;

	assoc_list = acct_storage_g_get_associations(
		db_conn, my_uid, assoc_cond);
	if (!assoc_list) {
		fprintf(stderr, "Couldn't get a list back for checking qos.\n");
		return false;
	}

	if (!g_qos_list)
		g_qos_list = acct_storage_g_get_qos(db_conn, my_uid, NULL);

	itr = list_iterator_create(assoc_list);
	while ((assoc = list_next(itr))) {
		char *qos = NULL;

		if (assoc->qos_list) {
			int check_qos = qos_id;
			if (check_qos == -1)
				check_qos = assoc->def_qos_id;
			if ((int)check_qos > 0) {
				ListIterator qos_itr =
					list_iterator_create(assoc->qos_list);
				while ((qos = list_next(qos_itr))) {
					if (qos[0] == '-')
						continue;
					else if (qos[0] == '+')
						qos++;
					/* info("looking for %u ?= %u", */
					/*      check_qos, slurm_atoul(qos)); */
					if (check_qos == slurm_atoul(qos))
						break;
				}
				list_iterator_destroy(qos_itr);
			} else
				qos = "";
		}

		if (!qos) {
			char *name = slurmdb_qos_str(g_qos_list,
						     assoc->def_qos_id);
			if (!assoc->user) {
				// see if this isn't a user
				object = xstrdup_printf(
					"  DefQOS = %-10s C = %-10s A = %-20s ",
					name, assoc->cluster, assoc->acct);
			} else if (assoc->partition) {
				// see if there is a partition name
				object = xstrdup_printf(
					"  DefQOS = %-10s C = %-10s A = %-20s "
					"U = %-9s P = %s",
					name, assoc->cluster, assoc->acct,
					assoc->user, assoc->partition);
			} else {
				object = xstrdup_printf(
					"  DefQOS = %-10s C = %-10s A = %-20s "
					"U = %-9s",
					name, assoc->cluster,
					assoc->acct, assoc->user);
			}

			if (!no_access_list)
				no_access_list =
					list_create(slurm_destroy_char);
			list_append(no_access_list, object);
		}
	}
	list_iterator_destroy(itr);
	list_destroy(assoc_list);

	if (!no_access_list)
		return true;
	fprintf(stderr,
		" These associations don't have access to their "
		"default qos.\n");
	fprintf(stderr,
		" Please give them access before they the default "
		"can be set to this.\n");
	itr = list_iterator_create(no_access_list);
	while ((object = list_next(itr)))
		fprintf(stderr, "%s\n", object);
	list_iterator_destroy(itr);
	list_destroy(no_access_list);

	return 0;
}


extern int sacctmgr_set_association_cond(slurmdb_association_cond_t *assoc_cond,
					 char *type, char *value,
					 int command_len, int option)
{
	int set =0;

	xassert(assoc_cond);
	xassert(type);

	if (!strncasecmp (type, "Accounts", MAX(command_len, 2))
		   || !strncasecmp (type, "Acct", MAX(command_len, 4))) {
		if (!assoc_cond->acct_list)
			assoc_cond->acct_list = list_create(slurm_destroy_char);
		slurm_addto_char_list(assoc_cond->acct_list, value);

		if (list_count(assoc_cond->acct_list))
			set = 1;
	} else if (!strncasecmp(type, "Ids", MAX(command_len, 1))
		   || !strncasecmp(type, "Associations", MAX(command_len, 2))) {
		ListIterator itr = NULL;
		char *temp = NULL;
		uint32_t id = 0;

		if (!assoc_cond->id_list)
			assoc_cond->id_list =list_create(slurm_destroy_char);
		slurm_addto_char_list(assoc_cond->id_list, value);
		/* check to make sure user gave ints here */
		itr = list_iterator_create(assoc_cond->id_list);
		while ((temp = list_next(itr))) {
			if (get_uint(temp, &id, "AssocId") != SLURM_SUCCESS) {
				exit_code = 1;
				list_delete_item(itr);
			}
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if (!strncasecmp (type, "Clusters", MAX(command_len, 1))) {
		if (!assoc_cond->cluster_list)
			assoc_cond->cluster_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->cluster_list, value))
			set = 1;
	} else if (!strncasecmp (type, "DefaultQOS", MAX(command_len, 8))) {
		if (!assoc_cond->def_qos_id_list)
			assoc_cond->def_qos_id_list =
				list_create(slurm_destroy_char);

		if (!g_qos_list)
			g_qos_list = acct_storage_g_get_qos(
				db_conn, my_uid, NULL);

		if (slurmdb_addto_qos_char_list(assoc_cond->def_qos_id_list,
						g_qos_list, value, 0))
			set = 1;
		else
			exit_code = 1;
	} else if (!strncasecmp (type, "FairShare", MAX(command_len, 1))
		   || !strncasecmp (type, "Shares", MAX(command_len, 1))) {
		if (!assoc_cond->fairshare_list)
			assoc_cond->fairshare_list =
				list_create(slurm_destroy_char);

		if (slurm_addto_char_list(assoc_cond->fairshare_list, value))
			set = 1;
	} else if (!strncasecmp (type, "GrpCPUMins", MAX(command_len, 7))) {
		if (!assoc_cond->grp_cpu_mins_list)
			assoc_cond->grp_cpu_mins_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_cpu_mins_list, value))
			set = 1;
	} else if (!strncasecmp (type, "GrpCPURunMins", MAX(command_len, 7))) {
		if (!assoc_cond->grp_cpu_run_mins_list)
			assoc_cond->grp_cpu_run_mins_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_cpu_run_mins_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "GrpCpus", MAX(command_len, 7))) {
		if (!assoc_cond->grp_cpus_list)
			assoc_cond->grp_cpus_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_cpus_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "GrpJobs", MAX(command_len, 4))) {
		if (!assoc_cond->grp_jobs_list)
			assoc_cond->grp_jobs_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_jobs_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "GrpMemory", MAX(command_len, 4))) {
		if (!assoc_cond->grp_mem_list)
			assoc_cond->grp_mem_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_mem_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "GrpNodes", MAX(command_len, 4))) {
		if (!assoc_cond->grp_nodes_list)
			assoc_cond->grp_nodes_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_nodes_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "GrpSubmitJobs", MAX(command_len, 4))) {
		if (!assoc_cond->grp_submit_jobs_list)
			assoc_cond->grp_submit_jobs_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_submit_jobs_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "GrpWall", MAX(command_len, 4))) {
		if (!assoc_cond->grp_wall_list)
			assoc_cond->grp_wall_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->grp_wall_list, value))
			set = 1;
	} else if (!strncasecmp (type, "MaxCPUMinsPerJob",
				 MAX(command_len, 7))) {
		if (!assoc_cond->max_cpu_mins_pj_list)
			assoc_cond->max_cpu_mins_pj_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->max_cpu_mins_pj_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "MaxCPURunMins", MAX(command_len, 7))) {
		if (!assoc_cond->max_cpu_run_mins_list)
			assoc_cond->max_cpu_run_mins_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->max_cpu_run_mins_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "MaxCpusPerJob", MAX(command_len, 7))) {
		if (!assoc_cond->max_cpus_pj_list)
			assoc_cond->max_cpus_pj_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->max_cpus_pj_list, value))
			set = 1;
	} else if (!strncasecmp (type, "MaxJobs", MAX(command_len, 4))) {
		if (!assoc_cond->max_jobs_list)
			assoc_cond->max_jobs_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->max_jobs_list, value))
			set = 1;
	} else if (!strncasecmp (type, "MaxNodesPerJob", MAX(command_len, 4))) {
		if (!assoc_cond->max_nodes_pj_list)
			assoc_cond->max_nodes_pj_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->max_nodes_pj_list, value))
			set = 1;
	} else if (!strncasecmp (type, "MaxSubmitJobs", MAX(command_len, 4))) {
		if (!assoc_cond->max_submit_jobs_list)
			assoc_cond->max_submit_jobs_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->max_submit_jobs_list,
					 value))
			set = 1;
	} else if (!strncasecmp (type, "MaxWallDurationPerJob",
				 MAX(command_len, 4))) {
		if (!assoc_cond->max_wall_pj_list)
			assoc_cond->max_wall_pj_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->max_wall_pj_list, value))
			set = 1;
	} else if (!strncasecmp (type, "Partitions", MAX(command_len, 3))) {
		if (!assoc_cond->partition_list)
			assoc_cond->partition_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->partition_list, value))
			set = 1;
	} else if (!strncasecmp(type, "Parents", MAX(command_len, 4))) {
		if (!assoc_cond->parent_acct_list)
			assoc_cond->parent_acct_list =
				list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->parent_acct_list, value))
			set = 1;
	} else if (!strncasecmp (type, "QosLevel", MAX(command_len, 1))) {
		if (!assoc_cond->qos_list)
			assoc_cond->qos_list = list_create(slurm_destroy_char);

		if (!g_qos_list)
			g_qos_list = acct_storage_g_get_qos(
				db_conn, my_uid, NULL);

		if (slurmdb_addto_qos_char_list(assoc_cond->qos_list, g_qos_list,
					       value, option))
			set = 1;
	} else if (!strncasecmp (type, "Users", MAX(command_len, 1))) {
		if (!assoc_cond->user_list)
			assoc_cond->user_list = list_create(slurm_destroy_char);
		if (slurm_addto_char_list(assoc_cond->user_list, value))
			set = 1;
	}

	return set;
}

extern int sacctmgr_set_association_rec(slurmdb_association_rec_t *assoc,
					char *type, char *value,
					int command_len, int option)
{
	int set = 0;
	uint32_t mins = NO_VAL;

	if (!assoc)
		return set;

	if (!strncasecmp (type, "DefaultQOS", MAX(command_len, 8))) {
		if (!g_qos_list)
			g_qos_list = acct_storage_g_get_qos(
				db_conn, my_uid, NULL);

		if (atoi(value) == -1)
			assoc->def_qos_id = -1;
		else
			assoc->def_qos_id = str_2_slurmdb_qos(
				g_qos_list, value);

		if (assoc->def_qos_id == NO_VAL) {
			fprintf(stderr,
				"You gave a bad default qos '%s'.  "
				"Use 'list qos' to get "
				"complete list.\n",
				value);
			exit_code = 1;
		}
		set = 1;
	} else if (!strncasecmp(type, "FairShare", MAX(command_len, 1))
		   || !strncasecmp(type, "Shares", MAX(command_len, 1))) {
		if (!strncasecmp(value, "parent", 6)) {
			assoc->shares_raw = SLURMDB_FS_USE_PARENT;
			set = 1;
		} else if (get_uint(value, &assoc->shares_raw,
				    "FairShare") == SLURM_SUCCESS) {
			set = 1;
		}
	} else if (!strncasecmp(type, "GrpCPUMins", MAX(command_len, 7))) {
		if (get_uint64(value, &assoc->grp_cpu_mins,
			       "GrpCPUMins") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "GrpCPURunMins", MAX(command_len, 7))) {
		if (get_uint64(value, &assoc->grp_cpu_run_mins,
			       "GrpCPURunMins") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "GrpCpus", MAX(command_len, 7))) {
		if (get_uint(value, &assoc->grp_cpus,
			     "GrpCpus") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "GrpJobs", MAX(command_len, 4))) {
		if (get_uint(value, &assoc->grp_jobs,
			     "GrpJobs") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "GrpMemory", MAX(command_len, 4))) {
		if (get_uint(value, &assoc->grp_mem,
			     "GrpMemory") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "GrpNodes", MAX(command_len, 4))) {
		if (get_uint(value, &assoc->grp_nodes,
			     "GrpNodes") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "GrpSubmitJobs",
				MAX(command_len, 4))) {
		if (get_uint(value, &assoc->grp_submit_jobs,
			     "GrpSubmitJobs") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "GrpWall", MAX(command_len, 4))) {
		mins = time_str2mins(value);
		if (mins != NO_VAL) {
			assoc->grp_wall	= mins;
			set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Bad GrpWall time format: %s\n", type);
		}
	} else if (!strncasecmp(type, "MaxCPUMinsPerJob",
				MAX(command_len, 7))) {
		if (get_uint64(value, &assoc->max_cpu_mins_pj,
			       "MaxCPUMins") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "MaxCPURunMins", MAX(command_len, 7))) {
		if (get_uint64(value, &assoc->max_cpu_run_mins,
			       "MaxCPURunMins") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "MaxCpusPerJob", MAX(command_len, 7))) {
		if (get_uint(value, &assoc->max_cpus_pj,
			     "MaxCpus") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "MaxJobs", MAX(command_len, 4))) {
		if (get_uint(value, &assoc->max_jobs,
			     "MaxJobs") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "MaxNodesPerJob", MAX(command_len, 4))) {
		if (get_uint(value, &assoc->max_nodes_pj,
			     "MaxNodes") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "MaxSubmitJobs", MAX(command_len, 4))) {
		if (get_uint(value, &assoc->max_submit_jobs,
			     "MaxSubmitJobs") == SLURM_SUCCESS)
			set = 1;
	} else if (!strncasecmp(type, "MaxWallDurationPerJob",
				MAX(command_len, 4))) {
		mins = time_str2mins(value);
		if (mins != NO_VAL) {
			assoc->max_wall_pj = mins;
			set = 1;
		} else {
			exit_code=1;
			fprintf(stderr,
				" Bad MaxWall time format: %s\n",
				type);
		}
	} else if (!strncasecmp(type, "Parent", MAX(command_len, 1))) {
		assoc->parent_acct = strip_quotes(value, NULL, 1);
		set = 1;
	} else if (!strncasecmp(type, "QosLevel", MAX(command_len, 1))) {
		if (!assoc->qos_list)
			assoc->qos_list = list_create(slurm_destroy_char);

		if (!g_qos_list)
			g_qos_list = acct_storage_g_get_qos(
				db_conn, my_uid, NULL);

		if (slurmdb_addto_qos_char_list(assoc->qos_list,
						g_qos_list, value,
						option))
			set = 1;
	}

	return set;
}

extern void sacctmgr_print_association_rec(slurmdb_association_rec_t *assoc,
					   print_field_t *field, List tree_list,
					   bool last)
{
	char *print_acct = NULL;
	char *tmp_char = NULL;

	xassert(field);

	if (!assoc) {
		field->print_routine(field, NULL, last);
		return;
	}

	switch(field->type) {
	case PRINT_ACCT:
		if (tree_display) {
			char *local_acct = NULL;
			char *parent_acct = NULL;
			if (assoc->user) {
				local_acct = xstrdup_printf("|%s", assoc->acct);
				parent_acct = assoc->acct;
			} else {
				local_acct =
					xstrdup(assoc->acct);
				parent_acct =
					assoc->parent_acct;
			}
			print_acct = slurmdb_tree_name_get(
				local_acct, parent_acct, tree_list);
			xfree(local_acct);
		} else {
			print_acct = assoc->acct;
		}
		field->print_routine(field, print_acct, last);
		break;
	case PRINT_CLUSTER:
		field->print_routine(field, assoc->cluster, last);
		break;
	case PRINT_DQOS:
		if (!g_qos_list)
			g_qos_list = acct_storage_g_get_qos(
				db_conn, my_uid, NULL);
		tmp_char = slurmdb_qos_str(g_qos_list, assoc->def_qos_id);
		field->print_routine(field, tmp_char, last);
		break;
	case PRINT_FAIRSHARE:
		if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
			print_fields_str(field, "parent", last);
		else
			field->print_routine(field, assoc->shares_raw, last);
		break;
	case PRINT_GRPCM:
		field->print_routine(field, assoc->grp_cpu_mins, last);
		break;
	case PRINT_GRPCRM:
		field->print_routine(field, assoc->grp_cpu_run_mins, last);
		break;
	case PRINT_GRPC:
		field->print_routine(field, assoc->grp_cpus, last);
		break;
	case PRINT_GRPJ:
		field->print_routine(field, assoc->grp_jobs, last);
		break;
	case PRINT_GRPMEM:
		field->print_routine(field, assoc->grp_mem, last);
		break;
	case PRINT_GRPN:
		field->print_routine(field, assoc->grp_nodes, last);
		break;
	case PRINT_GRPS:
		field->print_routine(field, assoc->grp_submit_jobs, last);
		break;
	case PRINT_GRPW:
		field->print_routine(field, assoc->grp_wall, last);
		break;
	case PRINT_ID:
		field->print_routine(field, assoc->id, last);
		break;
	case PRINT_LFT:
		field->print_routine(field, assoc->lft, last);
		break;
	case PRINT_MAXCM:
		field->print_routine(field, assoc->max_cpu_mins_pj, last);
		break;
	case PRINT_MAXCRM:
		field->print_routine(field, assoc->max_cpu_run_mins, last);
		break;
	case PRINT_MAXC:
		field->print_routine(field, assoc->max_cpus_pj, last);
		break;
	case PRINT_MAXJ:
		field->print_routine(field, assoc->max_jobs, last);
		break;
	case PRINT_MAXN:
		field->print_routine(field, assoc->max_nodes_pj, last);
		break;
	case PRINT_MAXS:
		field->print_routine(field, assoc->max_submit_jobs, last);
		break;
	case PRINT_MAXW:
		field->print_routine(field, assoc->max_wall_pj,	last);
		break;
	case PRINT_PID:
		field->print_routine(field, assoc->parent_id, last);
		break;
	case PRINT_PNAME:
		field->print_routine(field, assoc->parent_acct, last);
		break;
	case PRINT_PART:
		field->print_routine(field, assoc->partition, last);
		break;
	case PRINT_QOS:
		if (!g_qos_list)
			g_qos_list = acct_storage_g_get_qos(
				db_conn, my_uid, NULL);

		field->print_routine(field, g_qos_list, assoc->qos_list, last);
		break;
	case PRINT_QOS_RAW:
		field->print_routine(field, assoc->qos_list, last);
		break;
	case PRINT_RGT:
		field->print_routine(field, assoc->rgt, last);
		break;
	case PRINT_USER:
		field->print_routine(field, assoc->user, last);
		break;
	default:
		field->print_routine(field, NULL, last);
		break;
	}
}

extern int sacctmgr_list_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	slurmdb_association_cond_t *assoc_cond =
		xmalloc(sizeof(slurmdb_association_cond_t));
	List assoc_list = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *last_cluster = NULL;
	List tree_list = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, assoc_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_association_cond(assoc_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	} else if (!list_count(format_list)) {
		slurm_addto_char_list(format_list, "Cluster,Account,User,Part");
		if (!assoc_cond->without_parent_limits)
			slurm_addto_char_list(format_list,
					      "Share,GrpJ,GrpN,GrpCPUs,GrpMEM,"
					      "GrpS,GrpWall,GrpCPUMins,MaxJ,"
					      "MaxN,MaxCPUs,MaxS,MaxW,"
					      "MaxCPUMins,QOS,DefaultQOS,GrpCPURunMins");
	}
	print_fields_list = sacctmgr_process_format_list(format_list);
	list_destroy(format_list);

	if (exit_code) {
		slurmdb_destroy_association_cond(assoc_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     assoc_cond);
	slurmdb_destroy_association_cond(assoc_cond);

	if (!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	slurmdb_sort_hierarchical_assoc_list(assoc_list);

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((assoc = list_next(itr))) {
		int curr_inx = 1;
		if (!last_cluster || strcmp(last_cluster, assoc->cluster)) {
			if (tree_list) {
				list_flush(tree_list);
			} else {
				tree_list =
					list_create(slurmdb_destroy_print_tree);
			}
			last_cluster = assoc->cluster;
		}
		while((field = list_next(itr2))) {
			sacctmgr_print_association_rec(
				assoc, field, tree_list,
				(curr_inx == field_count));
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	if (tree_list)
		list_destroy(tree_list);

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(assoc_list);
	list_destroy(print_fields_list);
	tree_display = 0;
	return rc;
}

/* extern int sacctmgr_modify_association(int argc, char *argv[]) */
/* { */
/* 	int rc = SLURM_SUCCESS; */
/* 	return rc; */
/* } */

/* extern int sacctmgr_delete_association(int argc, char *argv[]) */
/* { */
/* 	int rc = SLURM_SUCCESS; */
/* 	return rc; */
/* } */
