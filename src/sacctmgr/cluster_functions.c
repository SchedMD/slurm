/*****************************************************************************\
 *  cluster_functions.c - functions dealing with clusters in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2011 Lawrence Livermore National Security.
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
#include "src/common/uid.h"

static bool with_deleted = 0;
static bool without_limits = 0;

static int _set_cond(int *start, int argc, char *argv[],
		     slurmdb_cluster_cond_t *cluster_cond,
		     List format_list)
{
	int i;
	int c_set = 0;
	int a_set = 0;
	int end = 0;
	int command_len = 0;

	with_deleted = 0;
	without_limits = 0;

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

		if (!strncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end &&
			   !strncasecmp(argv[i], "WithDeleted",
					 MAX(command_len, 5))) {
			with_deleted = 1;
		} else if (!end && !strncasecmp(argv[i], "WOLimits",
						 MAX(command_len, 3))) {
			without_limits = 1;
		} else if (!end || !strncasecmp(argv[i], "Names",
						MAX(command_len, 1))
			  || !strncasecmp(argv[i], "Clusters",
					   MAX(command_len, 3))) {
			if (!cluster_cond->cluster_list)
				cluster_cond->cluster_list =
					list_create(slurm_destroy_char);
			if (slurm_addto_char_list(cluster_cond->cluster_list,
						 argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp(argv[i], "Classification",
					 MAX(command_len, 3))) {
			cluster_cond->classification =
				str_2_classification(argv[i]+end);
			if (cluster_cond->classification)
				c_set = 1;
		} else if (!strncasecmp(argv[i], "flags",
					 MAX(command_len, 2))) {
			cluster_cond->flags = slurmdb_str_2_cluster_flags(
				argv[i]+end);
			c_set = 1;
		} else if (!strncasecmp(argv[i], "Format",
					 MAX(command_len, 2))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!end || !strncasecmp(argv[i], "PluginIDSelect",
						MAX(command_len, 1))) {
			if (!cluster_cond->plugin_id_select_list)
				cluster_cond->plugin_id_select_list =
					list_create(slurm_destroy_char);
			if (slurm_addto_char_list(
				   cluster_cond->plugin_id_select_list,
				   argv[i]+end))
				c_set = 1;
		} else if (!end || !strncasecmp(argv[i], "RPCVersions",
						MAX(command_len, 1))) {
			if (!cluster_cond->rpc_version_list)
				cluster_cond->rpc_version_list =
					list_create(slurm_destroy_char);
			if (slurm_addto_char_list(cluster_cond->rpc_version_list,
						 argv[i]+end))
				c_set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
			break;
		}
	}
	(*start) = i;

	if (c_set && a_set)
		return 3;
	else if (a_set) {
		return 2;
	} else if (c_set)
		return 1;
	return 0;
}

static int _set_rec(int *start, int argc, char *argv[],
		    List name_list,
		    slurmdb_association_rec_t *assoc,
		    uint16_t *classification)
{
	int i;
	int set = 0;
	int end = 0;
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

		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !strncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !strncasecmp(argv[i], "Names",
					   MAX(command_len, 1))
			  || !strncasecmp(argv[i], "Clusters",
					   MAX(command_len, 3))) {
			if (name_list)
				slurm_addto_char_list(name_list,
						      argv[i]+end);
		} else if (!strncasecmp(argv[i], "Classification",
					 MAX(command_len, 3))) {
			if (classification) {
				*classification =
					str_2_classification(argv[i]+end);
				if (*classification)
					set = 1;
			}
		} else if (!strncasecmp(argv[i], "GrpCPURunMins",
					 MAX(command_len, 7))) {
			exit_code=1;
			fprintf(stderr, "GrpCPURunMins is not a valid option "
				"for the root association of a cluster.\n");
			break;
		} else if (!strncasecmp(argv[i], "GrpCPUMins",
					 MAX(command_len, 7))) {
			exit_code=1;
			fprintf(stderr, "GrpCPUMins is not a valid option "
				"for the root association of a cluster.\n");
			break;
		} else if (!strncasecmp(argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			exit_code=1;
			fprintf(stderr, "GrpWall is not a valid option "
				"for the root association of a cluster.\n");
		} else if (!assoc ||
			  (assoc && !(set = sacctmgr_set_association_rec(
					      assoc, argv[i], argv[i]+end,
					      command_len, option)))) {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}
	(*start) = i;

	return set;

}


extern int sacctmgr_add_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i = 0;
	slurmdb_cluster_rec_t *cluster = NULL;
	List name_list = list_create(slurm_destroy_char);
	List cluster_list = NULL;
	slurmdb_association_rec_t start_assoc;

	int limit_set = 0;
	ListIterator itr = NULL, itr_c = NULL;
	char *name = NULL;
	uint16_t class = 0;

	slurmdb_init_association_rec(&start_assoc, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		limit_set += _set_rec(&i, argc, argv,
				      name_list, &start_assoc, &class);
	}
	if (exit_code) {
		list_destroy(name_list);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		list_destroy(name_list);
		exit_code=1;
		fprintf(stderr, " Need name of cluster to add.\n");
		return SLURM_ERROR;
	} else {
		List temp_list = NULL;
		slurmdb_cluster_cond_t cluster_cond;

		slurmdb_init_cluster_cond(&cluster_cond, 0);
		cluster_cond.cluster_list = name_list;
		cluster_cond.classification = class;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		if (!temp_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			return SLURM_ERROR;
		}

		itr_c = list_iterator_create(name_list);
		itr = list_iterator_create(temp_list);
		while((name = list_next(itr_c))) {
			slurmdb_cluster_rec_t *cluster_rec = NULL;

			list_iterator_reset(itr);
			while((cluster_rec = list_next(itr))) {
				if (!strcasecmp(cluster_rec->name, name))
					break;
			}
			if (cluster_rec) {
				printf(" This cluster %s already exists.  "
				       "Not adding.\n", name);
				list_delete_item(itr_c);
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr_c);
		list_destroy(temp_list);
		if (!list_count(name_list)) {
			list_destroy(name_list);
			return SLURM_ERROR;
		}
	}

	printf(" Adding Cluster(s)\n");
	cluster_list = list_create(slurmdb_destroy_cluster_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		if (!name[0]) {
			exit_code=1;
			fprintf(stderr, " No blank names are "
				"allowed when adding.\n");
			rc = SLURM_ERROR;
			continue;
		}
		cluster = xmalloc(sizeof(slurmdb_cluster_rec_t));
		slurmdb_init_cluster_rec(cluster, 0);

		list_append(cluster_list, cluster);
		cluster->flags = NO_VAL;
		cluster->name = xstrdup(name);
		cluster->classification = class;
		cluster->root_assoc =
			xmalloc(sizeof(slurmdb_association_rec_t));
		slurmdb_init_association_rec(cluster->root_assoc, 0);
		printf("  Name          = %s\n", cluster->name);
		if (cluster->classification)
			printf("  Classification= %s\n",
			       get_classification_str(cluster->classification));

		cluster->root_assoc->def_qos_id = start_assoc.def_qos_id;
		cluster->root_assoc->shares_raw = start_assoc.shares_raw;

		cluster->root_assoc->grp_cpus = start_assoc.grp_cpus;
		cluster->root_assoc->grp_jobs = start_assoc.grp_jobs;
		cluster->root_assoc->grp_mem = start_assoc.grp_mem;
		cluster->root_assoc->grp_nodes = start_assoc.grp_nodes;
		cluster->root_assoc->grp_submit_jobs =
			start_assoc.grp_submit_jobs;

		cluster->root_assoc->max_cpu_mins_pj =
			start_assoc.max_cpu_mins_pj;
		cluster->root_assoc->max_cpus_pj = start_assoc.max_cpus_pj;
		cluster->root_assoc->max_jobs = start_assoc.max_jobs;
		cluster->root_assoc->max_nodes_pj = start_assoc.max_nodes_pj;
		cluster->root_assoc->max_submit_jobs =
			start_assoc.max_submit_jobs;
		cluster->root_assoc->max_wall_pj = start_assoc.max_wall_pj;

		cluster->root_assoc->qos_list =
			copy_char_list(start_assoc.qos_list);
	}
	list_iterator_destroy(itr);
	list_destroy(name_list);

	if (limit_set) {
		printf(" Default Limits\n");
		sacctmgr_print_assoc_limits(&start_assoc);
		if (start_assoc.qos_list)
			list_destroy(start_assoc.qos_list);
	}

	if (!list_count(cluster_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* Since we are creating tables with add cluster that can't be
	   rolled back.  So we ask before hand if they are serious
	   about it so we can rollback if needed.
	*/
	if (commit_check("Would you like to commit changes?")) {
		notice_thread_init();
		rc = acct_storage_g_add_clusters(db_conn, my_uid, cluster_list);
		notice_thread_fini();
		if (rc == SLURM_SUCCESS) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			exit_code=1;
			fprintf(stderr, " Problem adding clusters: %s\n",
				slurm_strerror(rc));
			/* this isn't really needed, but just to be safe */
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		printf(" Changes Discarded\n");
		/* this isn't really needed, but just to be safe */
		acct_storage_g_commit(db_conn, 0);
	}

end_it:
	list_destroy(cluster_list);

	return rc;
}

extern int sacctmgr_list_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	slurmdb_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(slurmdb_cluster_cond_t));
	List cluster_list;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	char *tmp_char = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	slurmdb_init_cluster_cond(cluster_cond, 0);
	cluster_cond->cluster_list = list_create(slurm_destroy_char);
	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, cluster_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_cluster_cond(cluster_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	}

	if (!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				      "Cl,Controlh,Controlp,RPC");
		if (!without_limits)
			slurm_addto_char_list(format_list,
					      "Fa,GrpJ,GrpN,GrpS,MaxJ,MaxN,"
					      "MaxS,MaxW,QOS,DefaultQOS");
	}

	cluster_cond->with_deleted = with_deleted;

	print_fields_list = sacctmgr_process_format_list(format_list);
	list_destroy(format_list);

	if (exit_code) {
		slurmdb_destroy_cluster_cond(cluster_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid,
						   cluster_cond);
	slurmdb_destroy_cluster_cond(cluster_cond);

	if (!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(cluster_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((cluster = list_next(itr))) {
		int curr_inx = 1;
		slurmdb_association_rec_t *assoc = cluster->root_assoc;
		/* set up the working cluster rec so nodecnt's and node names
		 * are handled correctly */
		working_cluster_rec = cluster;
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_CLUSTER:
				field->print_routine(field,
						     cluster->name,
						     (curr_inx == field_count));
				break;
			case PRINT_CHOST:
				field->print_routine(field,
						     cluster->control_host,
						     (curr_inx == field_count));
				break;
			case PRINT_CPORT:
				field->print_routine(field,
						     cluster->control_port,
						     (curr_inx == field_count));
				break;
			case PRINT_CLASS:
				field->print_routine(field,
						     get_classification_str(
							     cluster->
							     classification),
						     (curr_inx == field_count));
				break;
			case PRINT_CPUS:
			{
				char tmp_char[9];
				convert_num_unit((float)cluster->cpu_count,
						 tmp_char, sizeof(tmp_char),
						 UNIT_NONE);
				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				break;
			}
			case PRINT_DQOS:
				if (!g_qos_list) {
					g_qos_list = acct_storage_g_get_qos(
						db_conn,
						my_uid,
						NULL);
				}
				tmp_char = slurmdb_qos_str(g_qos_list,
							   assoc->def_qos_id);
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				break;
			case PRINT_FAIRSHARE:
				field->print_routine(
					field,
					assoc->shares_raw,
					(curr_inx == field_count));
				break;
			case PRINT_FLAGS:
			{
				char *tmp_char = slurmdb_cluster_flags_2_str(
					cluster->flags);
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			}
			case PRINT_GRPC:
				field->print_routine(field,
						     assoc->grp_cpus,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPJ:
				field->print_routine(field,
						     assoc->grp_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPMEM:
				field->print_routine(field,
						     assoc->grp_mem,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPN:
				field->print_routine(field,
						     assoc->grp_nodes,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPS:
				field->print_routine(field,
						     assoc->grp_submit_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXCM:
				field->print_routine(
					field,
					assoc->max_cpu_mins_pj,
					(curr_inx == field_count));
				break;
			case PRINT_MAXC:
				field->print_routine(field,
						     assoc->max_cpus_pj,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXJ:
				field->print_routine(field,
						     assoc->max_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXN:
				field->print_routine(field,
						     assoc->max_nodes_pj,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXS:
				field->print_routine(field,
						     assoc->max_submit_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXW:
				field->print_routine(
					field,
					assoc->max_wall_pj,
					(curr_inx == field_count));
				break;

			case PRINT_NODECNT:
			{
				hostlist_t hl = hostlist_create(cluster->nodes);
				int cnt = 0;
				if (hl) {
					cnt = hostlist_count(hl);
					hostlist_destroy(hl);
				}
				field->print_routine(
					field,
					cnt,
					(curr_inx == field_count));
				break;
			}
			case PRINT_CLUSTER_NODES:
				field->print_routine(
					field,
					cluster->nodes,
					(curr_inx == field_count));
				break;
			case PRINT_QOS:
				if (!g_qos_list)
					g_qos_list = acct_storage_g_get_qos(
						db_conn, my_uid, NULL);

				field->print_routine(field,
						     g_qos_list,
						     assoc->qos_list,
						     (curr_inx == field_count));
				break;
			case PRINT_QOS_RAW:
				field->print_routine(field,
						     assoc->qos_list,
						     (curr_inx == field_count));
				break;
			case PRINT_RPC_VERSION:
				field->print_routine(
					field,
					cluster->rpc_version,
					(curr_inx == field_count));
				break;
			case PRINT_SELECT:
				field->print_routine(
					field,
					cluster->plugin_id_select,
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
	/* clear the working cluster rec */
	working_cluster_rec = NULL;

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(cluster_list);
	list_destroy(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	slurmdb_association_rec_t *assoc =
		xmalloc(sizeof(slurmdb_association_rec_t));
	slurmdb_association_cond_t *assoc_cond =
		xmalloc(sizeof(slurmdb_association_cond_t));
	int cond_set = 0, prev_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;
	uint16_t class_rec = 0;
	slurmdb_cluster_cond_t cluster_cond;

	slurmdb_init_association_rec(assoc, 0);

	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(NULL);

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = assoc_cond->cluster_list;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			prev_set = _set_cond(&i, argc, argv,
					     &cluster_cond, NULL);
			cond_set |= prev_set;
		} else if (!strncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			prev_set = _set_rec(&i, argc, argv,
					    NULL, assoc, &class_rec);
			rec_set |= prev_set;
		} else {
			prev_set = _set_cond(&i, argc, argv,
					     &cluster_cond, NULL);
			cond_set |= prev_set;
		}
	}

	if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		rc = SLURM_ERROR;
		goto end_it;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			rc = SLURM_SUCCESS;
			goto end_it;
		}
	} else if (exit_code) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (cond_set & 1) {
		List temp_list = NULL;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		if (!temp_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			rc = SLURM_ERROR;
			goto end_it;
		} else if (!list_count(temp_list)) {
			fprintf(stderr,
				" Query didn't return any clusters.\n");
			rc = SLURM_ERROR;
			goto end_it;
		}
		/* we are only looking for the clusters returned from
		   this query, so we free the cluster_list and replace
		   it */
		if (assoc_cond->cluster_list)
			list_destroy(assoc_cond->cluster_list);
		assoc_cond->cluster_list = temp_list;
	}

	printf(" Setting\n");
	if (rec_set) {
		printf(" Default Limits =\n");
		sacctmgr_print_assoc_limits(assoc);
		if (class_rec)
			printf(" Cluster Classification = %s\n",
			       get_classification_str(class_rec));
	}

	list_append(assoc_cond->acct_list, "root");
	notice_thread_init();
	ret_list = acct_storage_g_modify_associations(
		db_conn, my_uid, assoc_cond, assoc);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified cluster defaults for associations...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if (ret_list) {
		printf(" Nothing modified\n");
		rc = SLURM_ERROR;
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	if (ret_list)
		list_destroy(ret_list);

	if (class_rec) {
		slurmdb_cluster_rec_t cluster_rec;

		slurmdb_init_cluster_rec(&cluster_rec, 0);
		/* the class has already returned these clusters so
		   just go with it */
		cluster_rec.classification = class_rec;

		ret_list = acct_storage_g_modify_clusters(
			db_conn, my_uid, &cluster_cond, &cluster_rec);

		if (ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified cluster classifications...\n");
			while((object = list_next(itr))) {
				printf("  %s\n", object);
			}
			list_iterator_destroy(itr);
			set = 1;
		} else if (ret_list) {
			printf(" Nothing modified\n");
			rc = SLURM_ERROR;
		} else {
			exit_code=1;
			fprintf(stderr, " Error with request: %s\n",
				slurm_strerror(errno));
			rc = SLURM_ERROR;
		}

		if (ret_list)
			list_destroy(ret_list);
	}

	notice_thread_fini();

	if (set) {
		if (commit_check("Would you like to commit changes?"))
			acct_storage_g_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}
end_it:
	slurmdb_destroy_association_cond(assoc_cond);
	slurmdb_destroy_association_rec(assoc);

	return rc;
}

extern int sacctmgr_delete_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	slurmdb_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(slurmdb_cluster_cond_t));
	int i=0;
	List ret_list = NULL;
	int cond_set = 0, prev_set;

	slurmdb_init_cluster_cond(cluster_cond, 0);
	cluster_cond->cluster_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, cluster_cond, NULL);
		cond_set |= prev_set;
	}

	if (exit_code) {
		slurmdb_destroy_cluster_cond(cluster_cond);
		return SLURM_ERROR;
	} else if (!cond_set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_cluster_cond(cluster_cond);
		return SLURM_ERROR;
	}

	if (!list_count(cluster_cond->cluster_list)
	   && !cluster_cond->classification) {
		exit_code=1;
		fprintf(stderr,
			"problem with delete request.  "
			"Nothing given to delete.\n");
		slurmdb_destroy_cluster_cond(cluster_cond);
		return SLURM_SUCCESS;
	}
	notice_thread_init();
	ret_list = acct_storage_g_remove_clusters(
		db_conn, my_uid, cluster_cond);
	rc = errno;
	notice_thread_fini();

	slurmdb_destroy_cluster_cond(cluster_cond);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		/* If there were jobs running with an association to
		   be deleted, don't.
		*/
		if (rc == ESLURM_JOBS_RUNNING_ON_ASSOC) {
			fprintf(stderr, " Error with request: %s\n",
				slurm_strerror(rc));
			while((object = list_next(itr))) {
				fprintf(stderr,"  %s\n", object);
			}
			list_destroy(ret_list);
			acct_storage_g_commit(db_conn, 0);
			return rc;
		}
		printf(" Deleting clusters...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if (commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else if (ret_list) {
		printf(" Nothing deleted\n");
		rc = SLURM_ERROR;
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	if (ret_list)
		list_destroy(ret_list);

	return rc;
}

extern int sacctmgr_dump_cluster (int argc, char *argv[])
{
	slurmdb_user_cond_t user_cond;
	slurmdb_user_rec_t *user = NULL;
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	slurmdb_association_cond_t assoc_cond;
	List assoc_list = NULL;
	List acct_list = NULL;
	List user_list = NULL;
	List slurmdb_hierarchical_rec_list = NULL;
	char *cluster_name = NULL;
	char *file_name = NULL;
	char *user_name = NULL;
	char *line = NULL;
	int i, command_len = 0;
	FILE *fd = NULL;
	char *class_str = NULL;

	for (i = 0; i < argc; i++) {
		int end = parse_option_end(argv[i]);

		if (!end)
			command_len = strlen(argv[i]);
		else {
			command_len = end - 1;
			if (argv[i][end] == '=') {
				end++;
			}
		}
		if (!end || !strncasecmp(argv[i], "Cluster",
					 MAX(command_len, 1))) {
			if (cluster_name) {
				exit_code = 1;
				fprintf(stderr,
					" Can only do one cluster at a time.  "
					"Already doing %s\n", cluster_name);
				continue;
			}
			cluster_name = xstrdup(argv[i]+end);
		} else if (!strncasecmp(argv[i], "File",
					MAX(command_len, 1))) {
			if (file_name) {
				exit_code = 1;
				fprintf(stderr,
					" File name already set to %s\n",
					file_name);
				continue;
			}
			file_name = xstrdup(argv[i]+end);
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}
	}

	if (!cluster_name) {
		exit_code = 1;
		fprintf(stderr, " We need a cluster to dump.\n");
		xfree(file_name);
		return SLURM_ERROR;
	} else {
		List temp_list = NULL;
		slurmdb_cluster_cond_t cluster_cond;
		slurmdb_cluster_rec_t *cluster_rec = NULL;

		slurmdb_init_cluster_cond(&cluster_cond, 0);
		cluster_cond.cluster_list = list_create(NULL);
		list_push(cluster_cond.cluster_list, cluster_name);

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		FREE_NULL_LIST(cluster_cond.cluster_list);
		if (!temp_list) {
			exit_code = 1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			xfree(cluster_name);
			xfree(file_name);
			return SLURM_ERROR;
		}

		cluster_rec = list_peek(temp_list);
		if (!cluster_rec) {
			exit_code = 1;
			fprintf(stderr, " Cluster %s doesn't exist.\n",
				cluster_name);
			xfree(cluster_name);
			xfree(file_name);
			FREE_NULL_LIST(temp_list);
			return SLURM_ERROR;
		}
		class_str = get_classification_str(cluster_rec->classification);
		FREE_NULL_LIST(temp_list);
	}

	if (!file_name) {
		file_name = xstrdup_printf("./%s.cfg", cluster_name);
		printf(" No filename given, using %s.\n", file_name);
	}

	memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
	user_cond.with_coords = 1;
	user_cond.with_wckeys = 1;
	user_cond.with_assocs = 1;

	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	assoc_cond.without_parent_limits = 1;
	assoc_cond.with_raw_qos = 1;
	assoc_cond.cluster_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, cluster_name);
	/* this is needed for getting the correct wckeys */
	user_cond.assoc_cond = &assoc_cond;

	user_list = acct_storage_g_get_users(db_conn, my_uid, &user_cond);
	/* If not running with the DBD assoc_cond.user_list can be set,
	 * which will mess other things up.
	 */
	if (assoc_cond.user_list) {
		FREE_NULL_LIST(assoc_cond.user_list);
		assoc_cond.user_list = NULL;
	}

	/* make sure this person running is an admin */
	user_name = uid_to_string_cached(my_uid);
	if (!(user = sacctmgr_find_user_from_list(user_list, user_name))) {
		exit_code = 1;
		fprintf(stderr, " Your uid (%u) is not in the "
			"accounting system, can't dump cluster.\n", my_uid);
		FREE_NULL_LIST(assoc_cond.cluster_list);
		xfree(cluster_name);
		xfree(file_name);
		FREE_NULL_LIST(user_list);
		return SLURM_ERROR;

	} else {
		if (my_uid != slurm_get_slurm_user_id() && my_uid != 0
		    && user->admin_level < SLURMDB_ADMIN_SUPER_USER) {
			exit_code = 1;
			fprintf(stderr, " Your user does not have sufficient "
				"privileges to dump clusters.\n");
			FREE_NULL_LIST(assoc_cond.cluster_list);
			xfree(cluster_name);
			xfree(file_name);
			FREE_NULL_LIST(user_list);
			return SLURM_ERROR;
		}
	}
	xfree(user_name);

	/* assoc_cond is set up above */
	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     &assoc_cond);
	FREE_NULL_LIST(assoc_cond.cluster_list);
	if (!assoc_list) {
		exit_code = 1;
		fprintf(stderr, " Problem with query.\n");
		xfree(cluster_name);
		xfree(file_name);
		return SLURM_ERROR;
	} else if (!list_count(assoc_list)) {
		exit_code = 1;
		fprintf(stderr, " Cluster %s returned nothing.\n",
			cluster_name);
		FREE_NULL_LIST(assoc_list);
		xfree(cluster_name);
		xfree(file_name);
		return SLURM_ERROR;
	}

	slurmdb_hierarchical_rec_list = slurmdb_get_acct_hierarchical_rec_list(
		assoc_list);

	acct_list = acct_storage_g_get_accounts(db_conn, my_uid, NULL);

	if ((fd = fopen(file_name,"w")) == NULL) {
		fprintf(stderr, "Can't open file %s, %s\n", file_name,
			slurm_strerror(errno));
		FREE_NULL_LIST(acct_list);
		FREE_NULL_LIST(assoc_list);
		xfree(cluster_name);
		xfree(file_name);
		FREE_NULL_LIST(slurmdb_hierarchical_rec_list);
		return SLURM_ERROR;
	}

	/* Add header */
	if (fprintf(fd,
		    "# To edit this file start with a cluster line "
		    "for the new cluster\n"
		    "# Cluster - 'cluster_name':MaxNodesPerJob=50\n"
		    "# Followed by Accounts you want in this fashion "
		    "(root is created by default)...\n"
		    "# Parent - 'root'\n"
		    "# Account - 'cs':MaxNodesPerJob=5:MaxJobs=4:"
		    "MaxCPUMins=20:FairShare=399:"
		    "MaxWallDuration=40:Description='Computer Science':"
		    "Organization='LC'\n"
		    "# Any of the options after a ':' can be left out and "
		    "they can be in any order.\n"
		    "# If you want to add any sub accounts just list the "
		    "Parent THAT HAS ALREADY \n"
		    "# BEEN CREATED before the account line in this "
		    "fashion...\n"
		    "# Parent - 'cs'\n"
		    "# Account - 'test':MaxNodesPerJob=1:MaxJobs=1:"
		    "MaxCPUMins=1:FairShare=1:"
		    "MaxWallDuration=1:"
		    "Description='Test Account':Organization='Test'\n"
		    "# To add users to a account add a line like this after a "
		    "Parent - 'line'\n"
		    "# User - 'lipari':MaxNodesPerJob=2:MaxJobs=3:"
		    "MaxCPUMins=4:FairShare=1:"
		    "MaxWallDurationPerJob=1\n") < 0) {
		exit_code = 1;
		fprintf(stderr, "Can't write to file");
		FREE_NULL_LIST(acct_list);
		FREE_NULL_LIST(assoc_list);
		xfree(cluster_name);
		xfree(file_name);
		FREE_NULL_LIST(slurmdb_hierarchical_rec_list);
		return SLURM_ERROR;
	}

	line = xstrdup_printf("Cluster - '%s'", cluster_name);

	if (class_str)
		xstrfmtcat(line, ":Classification='%s'", class_str);

	slurmdb_hierarchical_rec = list_peek(slurmdb_hierarchical_rec_list);
	assoc = slurmdb_hierarchical_rec->assoc;
	if (strcmp(assoc->acct, "root")) {
		fprintf(stderr, "Root association not on the top it was %s\n",
			assoc->acct);
	} else
		print_file_add_limits_to_line(&line, assoc);

	if (fprintf(fd, "%s\n", line) < 0) {
		exit_code = 1;
		fprintf(stderr, " Can't write to file");
		FREE_NULL_LIST(acct_list);
		FREE_NULL_LIST(assoc_list);
		xfree(cluster_name);
		xfree(file_name);
		xfree(line);
		FREE_NULL_LIST(slurmdb_hierarchical_rec_list);
		return SLURM_ERROR;
	}
	info("%s", line);
	xfree(line);

	print_file_slurmdb_hierarchical_rec_list(
		fd, slurmdb_hierarchical_rec_list, user_list, acct_list);

	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(assoc_list);
	xfree(cluster_name);
	xfree(file_name);
	FREE_NULL_LIST(slurmdb_hierarchical_rec_list);
	fclose(fd);

	return SLURM_SUCCESS;
}
