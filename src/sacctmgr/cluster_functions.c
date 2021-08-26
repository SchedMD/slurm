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

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/uid.h"

static bool with_deleted = 0;
static bool with_fed = 0;
static bool without_limits = 0;

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_cluster_cond_t *cluster_cond,
		     List format_list)
{
	int i = 0;
	int cond_set = 0;
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

		if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithDeleted",
					 MAX(command_len, 5))) {
			with_deleted = 1;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithFed",
					 MAX(command_len, 5))) {
			with_fed = 1;
		} else if (!end && !xstrncasecmp(argv[i], "WOLimits",
						 MAX(command_len, 3))) {
			without_limits = 1;
		} else if (!end || !xstrncasecmp(argv[i], "Names",
						MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Clusters",
					   MAX(command_len, 3))) {
			if (!cluster_cond->cluster_list)
				cluster_cond->cluster_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(cluster_cond->cluster_list,
						 argv[i]+end))
				cond_set |= SA_SET_ASSOC;
		} else if (!end || !xstrncasecmp(argv[i], "Federations",
						MAX(command_len, 3))) {
			if (!cluster_cond->federation_list)
				cluster_cond->federation_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(cluster_cond->federation_list,
						  argv[i]+end))
				cond_set |= SA_SET_ASSOC;
		} else if (!xstrncasecmp(argv[i], "Classification",
					 MAX(command_len, 3))) {
			cluster_cond->classification =
				str_2_classification(argv[i]+end);
			if (cluster_cond->classification)
				cond_set |= SA_SET_CLUST;
		} else if (!xstrncasecmp(argv[i], "flags",
					 MAX(command_len, 2))) {
			cluster_cond->flags = slurmdb_str_2_cluster_flags(
				argv[i]+end);
			cond_set |= SA_SET_CLUST;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 2))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!end || !xstrncasecmp(argv[i], "PluginIDSelect",
						MAX(command_len, 1))) {
			if (!cluster_cond->plugin_id_select_list)
				cluster_cond->plugin_id_select_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(
				   cluster_cond->plugin_id_select_list,
				   argv[i]+end))
				cond_set |= SA_SET_CLUST;
		} else if (!end || !xstrncasecmp(argv[i], "RPCVersions",
						MAX(command_len, 1))) {
			if (!cluster_cond->rpc_version_list)
				cluster_cond->rpc_version_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(cluster_cond->rpc_version_list,
						 argv[i]+end))
				cond_set |= SA_SET_CLUST;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
			break;
		}
	}
	(*start) = i;

	return cond_set;
}

static int _set_rec(int *start, int argc, char **argv,
		    List name_list,
		    slurmdb_assoc_rec_t *assoc,
		    slurmdb_cluster_rec_t *cluster)
{
	int i = 0;
	int rec_set = 0;
	int end = 0;
	int command_len = 0;
	int option = 0;

	xassert(assoc);
	xassert(cluster);

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

		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Names",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Clusters",
					   MAX(command_len, 3))) {
			if (name_list)
				slurm_addto_char_list(name_list,
						      argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Classification",
					 MAX(command_len, 3))) {
			cluster->classification =
				str_2_classification(argv[i]+end);
			if (cluster->classification)
				rec_set |= SA_SET_CLUST;
		} else if (!xstrncasecmp(argv[i], "Features",
					MAX(command_len, 2))) {
			if (*(argv[i]+end) == '\0' &&
			    (option == '+' || option == '-')) {
				fprintf(stderr,
					" You didn't specify any features to %s\n",
					(option == '-') ? "remove" : "add");
				exit_code = 1;
				break;
			}

			if (!cluster->fed.feature_list)
				cluster->fed.feature_list =
					list_create(xfree_ptr);
			if ((slurm_addto_mode_char_list(cluster->fed.feature_list,
						   argv[i]+end, option) < 0)) {
				FREE_NULL_LIST(cluster->fed.feature_list);
				exit_code = 1;
				break;
			}
			rec_set |= SA_SET_CLUST;

		} else if (!xstrncasecmp(argv[i], "Federation",
					 MAX(command_len, 3))) {
			cluster->fed.name = xstrdup(argv[i]+end);
			rec_set |= SA_SET_CLUST;
		} else if (!xstrncasecmp(argv[i], "FedState",
					 MAX(command_len, 2))) {
			if (cluster) {
				cluster->fed.state =
					str_2_cluster_fed_states(argv[i]+end);
				if (!cluster->fed.state) {
					exit_code=1;
					fprintf(stderr, "Invalid FedState "
						"%s.\n", argv[i]+end);
					break;
				}

				rec_set |= SA_SET_CLUST;
			}
		} else if (!xstrncasecmp(argv[i], "GrpCPURunMins",
					 MAX(command_len, 7)) ||
			   !xstrncasecmp(argv[i], "GrpTRESRunMins",
					MAX(command_len, 8))) {
			exit_code=1;
			fprintf(stderr, "GrpTRESRunMins is not a valid option "
				"for the root association of a cluster.\n");
			break;
		} else if (!xstrncasecmp(argv[i], "GrpCPUMins",
					 MAX(command_len, 7)) ||
			   !xstrncasecmp(argv[i], "GrpTRESMins",
					MAX(command_len, 8))) {
			exit_code=1;
			fprintf(stderr, "GrpTRESMins is not a valid option "
				"for the root association of a cluster.\n");
			break;
		} else if (!xstrncasecmp(argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			exit_code=1;
			fprintf(stderr, "GrpWall is not a valid option "
				"for the root association of a cluster.\n");
		} else if (sacctmgr_set_assoc_rec(
					      assoc, argv[i], argv[i]+end,
					      command_len, option)) {
			rec_set |= SA_SET_ASSOC;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}
	(*start) = i;

	return rec_set;
}


extern int sacctmgr_add_cluster(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i = 0;
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_cluster_rec_t *start_cluster =
		xmalloc(sizeof(slurmdb_cluster_rec_t));
	List name_list = list_create(xfree_ptr);
	List cluster_list = NULL;
	slurmdb_assoc_rec_t start_assoc;

	int rec_set = 0;
	ListIterator itr = NULL, itr_c = NULL;
	char *name = NULL;

	slurmdb_init_assoc_rec(&start_assoc, 0);
	slurmdb_init_cluster_rec(start_cluster, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		rec_set |= _set_rec(&i, argc, argv,
				    name_list, &start_assoc, start_cluster);
	}
	if (exit_code) {
		FREE_NULL_LIST(name_list);
		slurmdb_destroy_cluster_rec(start_cluster);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		FREE_NULL_LIST(name_list);
		slurmdb_destroy_cluster_rec(start_cluster);
		exit_code=1;
		fprintf(stderr, " Need name of cluster to add.\n");
		return SLURM_ERROR;
	} else {
		List temp_list = NULL;
		slurmdb_cluster_cond_t cluster_cond;

		slurmdb_init_cluster_cond(&cluster_cond, 0);
		cluster_cond.cluster_list = name_list;
		cluster_cond.classification = start_cluster->classification;

		temp_list = slurmdb_clusters_get(db_conn, &cluster_cond);
		if (!temp_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			slurmdb_destroy_cluster_rec(start_cluster);
			return SLURM_ERROR;
		}

		itr_c = list_iterator_create(name_list);
		itr = list_iterator_create(temp_list);
		while((name = list_next(itr_c))) {
			slurmdb_cluster_rec_t *cluster_rec = NULL;

			list_iterator_reset(itr);
			while((cluster_rec = list_next(itr))) {
				if (!xstrcasecmp(cluster_rec->name, name))
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
		FREE_NULL_LIST(temp_list);
		if (!list_count(name_list)) {
			FREE_NULL_LIST(name_list);
			slurmdb_destroy_cluster_rec(start_cluster);
			return SLURM_ERROR;
		}
	}

	if (start_cluster->fed.name) {
		int rc;
		List fed_list = list_create(xfree_ptr);
		list_append(fed_list, xstrdup(start_cluster->fed.name));
		rc = verify_federations_exist(fed_list);
		FREE_NULL_LIST(fed_list);
		if (rc) {
			slurmdb_destroy_cluster_rec(start_cluster);
			FREE_NULL_LIST(name_list);
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
		slurmdb_copy_cluster_rec(cluster, start_cluster);
		cluster->name = xstrdup(name);
		printf("  Name           = %s\n", cluster->name);

		cluster->root_assoc =
			xmalloc(sizeof(slurmdb_assoc_rec_t));
		slurmdb_init_assoc_rec(cluster->root_assoc, 0);
		cluster->root_assoc->def_qos_id = start_assoc.def_qos_id;
		cluster->root_assoc->shares_raw = start_assoc.shares_raw;

		slurmdb_copy_assoc_rec_limits(
			cluster->root_assoc, &start_assoc);
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(name_list);

	if (rec_set)
		printf(" Setting\n");
	if (rec_set & SA_SET_CLUST)
		sacctmgr_print_cluster(start_cluster);
	if (rec_set & SA_SET_ASSOC) {
		printf("  Default Limits:\n");
		sacctmgr_print_assoc_limits(&start_assoc);
		FREE_NULL_LIST(start_assoc.qos_list);
	}
	slurmdb_destroy_cluster_rec(start_cluster);

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
		rc = slurmdb_clusters_add(db_conn, cluster_list);
		notice_thread_fini();
		if (rc == SLURM_SUCCESS) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			exit_code=1;
			fprintf(stderr, " Problem adding clusters: %s\n",
				slurm_strerror(rc));
			/* this isn't really needed, but just to be safe */
			slurmdb_connection_commit(db_conn, 0);
		}
	} else {
		printf(" Changes Discarded\n");
		/* this isn't really needed, but just to be safe */
		slurmdb_connection_commit(db_conn, 0);
	}

end_it:
	FREE_NULL_LIST(cluster_list);

	return rc;
}

extern int sacctmgr_list_cluster(int argc, char **argv)
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

	List format_list = list_create(xfree_ptr);
	List print_fields_list; /* types are of print_field_t */

	slurmdb_init_cluster_cond(cluster_cond, 0);
	cluster_cond->cluster_list = list_create(xfree_ptr);
	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, cluster_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_cluster_cond(cluster_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}

	if (!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				      "Cl,Controlh,Controlp,RPC");
		if (!without_limits)
			slurm_addto_char_list(format_list,
					      "Fa,GrpJ,GrpTRES,GrpS,MaxJ,"
					      "MaxTRES,MaxS,MaxW,QOS,"
					      "DefaultQOS");
		if (with_fed)
			slurm_addto_char_list(
				format_list,
				"Federation,ID,Features,FedState");
	}

	cluster_cond->with_deleted = with_deleted;

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		slurmdb_destroy_cluster_cond(cluster_cond);
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	cluster_list = slurmdb_clusters_get(db_conn, cluster_cond);
	slurmdb_destroy_cluster_cond(cluster_cond);

	if (!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(cluster_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while ((cluster = list_next(itr))) {
		int curr_inx = 1;

		/* set up the working cluster rec so nodecnt's and node names
		 * are handled correctly */
		working_cluster_rec = cluster;
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_CLUSTER:
				field->print_routine(field, cluster->name,
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
						     cluster->classification),
						     (curr_inx == field_count));
				break;
			case PRINT_FEATURES:
				field->print_routine(field,
						     cluster->fed.feature_list,
						     (curr_inx == field_count));
				break;
			case PRINT_FEDERATION:
				field->print_routine(field, cluster->fed.name,
						     (curr_inx == field_count));
				break;
			case PRINT_FEDSTATE:
			{
				char *tmp_str = slurmdb_cluster_fed_states_str(
							cluster->fed.state);
				field->print_routine(field, tmp_str,
						     (curr_inx == field_count));
				break;
			}
			case PRINT_FEDSTATERAW:
				field->print_routine(field, cluster->fed.state,
						     (curr_inx == field_count));
				break;
			case PRINT_ID:
				field->print_routine(field, cluster->fed.id,
						     (curr_inx == field_count));
				break;
			case PRINT_TRES:
				sacctmgr_initialize_g_tres_list();

				tmp_char = slurmdb_make_tres_string_from_simple(
					cluster->tres_str, g_tres_list, NO_VAL,
					CONVERT_NUM_UNIT_EXACT, 0, NULL);
				field->print_routine(field, tmp_char,
						     (curr_inx == field_count));
				xfree(tmp_char);
				break;
			case PRINT_FLAGS:
			{
				char *tmp_char = slurmdb_cluster_flags_2_str(
							     cluster->flags);
				field->print_routine(field, tmp_char,
						     (curr_inx == field_count));
				xfree(tmp_char);
				break;
			}
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
				sacctmgr_print_assoc_rec(cluster->root_assoc,
							 field, NULL,
							 (curr_inx ==
							  field_count));
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
	FREE_NULL_LIST(cluster_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

static int _find_cluster_rec_in_list(void *obj, void *key)
{
	slurmdb_cluster_rec_t *rec = (slurmdb_cluster_rec_t *)obj;
	char *char_key = (char *)key;

	if (!xstrcasecmp(rec->name, char_key))
		return 1;

	return 0;
}

/* Prepare cluster_list to be federation centric that will be passed to
 * verify_fed_clusters in federation_functions.c.
 */
static int _verify_fed_clusters(List cluster_list, const char *fed_name,
				bool *existing_fed)
{
	int   rc         = SLURM_SUCCESS;
	char *tmp_name   = NULL;
	List  tmp_list   = list_create(slurmdb_destroy_cluster_rec);
	ListIterator itr = list_iterator_create(cluster_list);

	while ((tmp_name = list_next(itr))) {
		slurmdb_cluster_rec_t *rec =
			xmalloc(sizeof(slurmdb_cluster_rec_t));
		slurmdb_init_cluster_rec(rec, 0);
		rec->name = xstrdup(tmp_name);
		list_append(tmp_list, rec);
	}

	if ((rc = verify_fed_clusters(tmp_list, fed_name, existing_fed)))
		goto end_it;

	/* have to reconcile lists now, clusters may have been removed from
	 * tmp_list */
	list_iterator_reset(itr);
	while ((tmp_name = list_next(itr))) {
		if (!list_find_first(tmp_list, _find_cluster_rec_in_list,
				     tmp_name))
			list_delete_item(itr);
	}

end_it:
	FREE_NULL_LIST(tmp_list);
	list_iterator_destroy(itr);

	return rc;
}

extern int sacctmgr_modify_cluster(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i=0;
	slurmdb_cluster_rec_t *cluster =
		xmalloc(sizeof(slurmdb_cluster_rec_t));
	slurmdb_assoc_rec_t *assoc =
		xmalloc(sizeof(slurmdb_assoc_rec_t));
	slurmdb_assoc_cond_t *assoc_cond =
		xmalloc(sizeof(slurmdb_assoc_cond_t));
	int cond_set = 0, prev_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	bool existing_fed = false;

	slurmdb_init_assoc_rec(assoc, 0);

	assoc_cond->cluster_list = list_create(xfree_ptr);
	assoc_cond->acct_list = list_create(NULL);

	slurmdb_init_cluster_rec(cluster, 0);
	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = assoc_cond->cluster_list;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			prev_set = _set_cond(&i, argc, argv,
					     &cluster_cond, NULL);
			cond_set |= prev_set;
		} else if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			prev_set = _set_rec(&i, argc, argv,
					    NULL, assoc, cluster);
			rec_set |= prev_set;
		} else {
			prev_set = _set_cond(&i, argc, argv,
					     &cluster_cond, NULL);
			cond_set |= prev_set;
		}
	}

	if (exit_code) {
		rc = SLURM_ERROR;
		goto end_it;
	} else if (!rec_set) {
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
	}

	if (cluster->fed.name && cluster->fed.name[0]) {
		int rc;
		/* Make sure federation exists. */
		List fed_list = list_create(xfree_ptr);
		list_append(fed_list, xstrdup(cluster->fed.name));
		rc = verify_federations_exist(fed_list);
		FREE_NULL_LIST(fed_list);
		if (rc)
			goto end_it;

		/* See if cluster is assigned to another federation already. */
		if (list_count(cluster_cond.cluster_list)) {
			if (_verify_fed_clusters(cluster_cond.cluster_list,
						 cluster->fed.name,
						 &existing_fed))
					goto end_it;
			else if (!list_count(cluster_cond.cluster_list)) {
				/* irrelevant changes have been removed and
				 * nothing to change now. */
				printf("Nothing to change\n");
				rc = SLURM_ERROR;
				(void)rc; /* CLANG false positive */
				goto end_it;
			} else if (existing_fed) {
				char *warning =
					"\nAre you sure you want to continue?";
				if (!commit_check(warning)) {
					rc = SLURM_ERROR;
					(void)rc; /* CLANG false positive */
					goto end_it;
				}
			}
		}
	}

	if (cond_set & SA_SET_USER) {
		List temp_list = NULL;

		temp_list = slurmdb_clusters_get(db_conn, &cluster_cond);
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
		FREE_NULL_LIST(assoc_cond->cluster_list);
		assoc_cond->cluster_list = temp_list;
	}

	printf(" Setting\n");
	if (rec_set & SA_SET_CLUST)
		sacctmgr_print_cluster(cluster);
	if (rec_set & SA_SET_ASSOC) {
		printf("  Default Limits:\n");
		sacctmgr_print_assoc_limits(assoc);
	}

	if (rec_set & SA_SET_CLUST) {
		notice_thread_init();
		ret_list = slurmdb_clusters_modify(
			db_conn, &cluster_cond, cluster);

		if (ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified cluster...\n");
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

		FREE_NULL_LIST(ret_list);
		notice_thread_fini();
	}

	if (rec_set & SA_SET_ASSOC) {
		list_append(assoc_cond->acct_list, "root");
		notice_thread_init();
		ret_list = slurmdb_associations_modify(db_conn,
						       assoc_cond, assoc);

		if (ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified cluster defaults for "
			       "associations...\n");
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
		FREE_NULL_LIST(ret_list);
		notice_thread_fini();
	}


	if (set) {
		if (commit_check("Would you like to commit changes?"))
			slurmdb_connection_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	}
end_it:
	slurmdb_destroy_assoc_cond(assoc_cond);
	slurmdb_destroy_assoc_rec(assoc);
	slurmdb_destroy_cluster_rec(cluster);

	return rc;
}

extern int sacctmgr_delete_cluster(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(slurmdb_cluster_cond_t));
	int i=0;
	List ret_list = NULL;
	int cond_set = 0, prev_set;

	slurmdb_init_cluster_cond(cluster_cond, 0);
	cluster_cond->cluster_list = list_create(xfree_ptr);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
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
	   && !cluster_cond->classification
	   && (!cluster_cond->federation_list ||
	       !list_count(cluster_cond->federation_list))) {
		exit_code=1;
		fprintf(stderr,
			"problem with delete request.  "
			"Nothing given to delete.\n");
		slurmdb_destroy_cluster_cond(cluster_cond);
		return SLURM_SUCCESS;
	}
	notice_thread_init();
	ret_list = slurmdb_clusters_remove(db_conn, cluster_cond);
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
			FREE_NULL_LIST(ret_list);
			slurmdb_connection_commit(db_conn, 0);
			return rc;
		}
		printf(" Deleting clusters...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
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

	FREE_NULL_LIST(ret_list);

	return rc;
}

extern int sacctmgr_dump_cluster (int argc, char **argv)
{
	slurmdb_user_cond_t user_cond;
	slurmdb_user_rec_t *user = NULL;
	slurmdb_hierarchical_rec_t *slurmdb_hierarchical_rec = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_assoc_cond_t assoc_cond;
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
		if (!end || !xstrncasecmp(argv[i], "Cluster",
					 MAX(command_len, 1))) {
			if (cluster_name) {
				exit_code = 1;
				fprintf(stderr,
					" Can only do one cluster at a time.  "
					"Already doing %s\n", cluster_name);
				continue;
			}
			cluster_name = xstrdup(argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "File",
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

		temp_list = slurmdb_clusters_get(db_conn, &cluster_cond);
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

	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.without_parent_limits = 1;
	assoc_cond.with_raw_qos = 1;
	assoc_cond.cluster_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, cluster_name);
	/* this is needed for getting the correct wckeys */
	user_cond.assoc_cond = &assoc_cond;

	user_list = slurmdb_users_get(db_conn, &user_cond);
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
		if ((my_uid != slurm_conf.slurm_user_id) && (my_uid != 0)
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
	assoc_list = slurmdb_associations_get(db_conn, &assoc_cond);
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

	acct_list = slurmdb_accounts_get(db_conn, NULL);

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
		    "# Cluster - 'cluster_name':MaxTRESPerJob=node=50\n"
		    "# Followed by Accounts you want in this fashion "
		    "(root is created by default)...\n"
		    "# Parent - 'root'\n"
		    "# Account - 'cs':MaxTRESPerJob=node=5:MaxJobs=4:"
		    "MaxTRESMinsPerJob=cpu=20:FairShare=399:"
		    "MaxWallDurationPerJob=40:Description='Computer Science':"
		    "Organization='LC'\n"
		    "# Any of the options after a ':' can be left out and "
		    "they can be in any order.\n"
		    "# If you want to add any sub accounts just list the "
		    "Parent THAT HAS ALREADY\n"
		    "# BEEN CREATED before the account line in this "
		    "fashion...\n"
		    "# Parent - 'cs'\n"
		    "# Account - 'test':MaxTRESPerJob=node=1:MaxJobs=1:"
		    "MaxTRESMinsPerJob=cpu=1:FairShare=1:"
		    "MaxWallDurationPerJob=1:"
		    "Description='Test Account':Organization='Test'\n"
		    "# To add users to a account add a line like this after a "
		    "Parent - 'line'\n"
		    "# User - 'lipari':MaxTRESPerJob=node=2:MaxJobs=3:"
		    "MaxTRESMinsPerJob=cpu=4:FairShare=1:"
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
	if (xstrcmp(assoc->acct, "root")) {
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
