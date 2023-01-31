/*****************************************************************************\
 *  federation_functions.c - functions dealing with Federations in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2016 SchedMD LLC.
 *  Written by Brian Christiansen <brian@schedmd.com>
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

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_federation_cond_t *federation_cond,
		     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;

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
			federation_cond->with_deleted = 1;
		} else if (!end && !xstrncasecmp(argv[i], "Tree",
						 MAX(command_len, 4))) {
			tree_display = 1;
		} else if (!end || !xstrncasecmp(argv[i], "Names",
						 MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Federations",
					   MAX(command_len, 3))) {
			if (!federation_cond->federation_list)
				federation_cond->federation_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(
					federation_cond->federation_list,
					argv[i]+end))
				set = 1;
		} else if (!end || !xstrncasecmp(argv[i], "Clusters",
						 MAX(command_len, 3))) {
			if (!federation_cond->cluster_list)
				federation_cond->cluster_list =
					list_create(xfree_ptr);
			if (slurm_addto_char_list(
						  federation_cond->cluster_list,
						  argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 2))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
			break;
		}
	}
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char **argv,
		    List name_list, slurmdb_federation_rec_t *fed)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;
	int option = 0;

	xassert(fed);

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

		if (!xstrncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Name",
					   MAX(command_len, 1))) {
			if (name_list)
				slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 2))) {
			char *name = NULL;
			ListIterator itr;

			if (*(argv[i]+end) == '\0' &&
			    (option == '+' || option == '-')) {
				fprintf(stderr,
					" You didn't specify any clusters to %s\n",
					(option == '-') ? "remove" : "add");
				exit_code = 1;
				break;
			}

			List cluster_names = list_create(xfree_ptr);
			if (slurm_addto_mode_char_list(cluster_names,
						       argv[i]+end, option) < 0)
			{
				FREE_NULL_LIST(cluster_names);
				exit_code = 1;
				break;
			}
			itr = list_iterator_create(cluster_names);
			fed->cluster_list =
				list_create(slurmdb_destroy_cluster_rec);
			while((name = list_next(itr))) {
				if (name[0] == '\0')
					continue;
				slurmdb_cluster_rec_t *cluster =
					xmalloc(sizeof(slurmdb_cluster_rec_t));
				slurmdb_init_cluster_rec(cluster, 0);
				cluster->name = xstrdup(name);
				list_append(fed->cluster_list, cluster);
			}
			list_iterator_destroy(itr);
			FREE_NULL_LIST(cluster_names);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Flags",
					 MAX(command_len, 2))) {
			fed->flags = str_2_federation_flags(argv[i]+end,
								   option);
			if (fed->flags == FEDERATION_FLAG_NOTSET) {
				char *tmp_char = NULL;
				fed->flags = INFINITE;
				fed->flags &= (~FEDERATION_FLAG_NOTSET &
					       ~FEDERATION_FLAG_ADD &
					       ~FEDERATION_FLAG_REMOVE);
				tmp_char =
					slurmdb_federation_flags_str(
							fed->flags);
				fprintf(stderr,
					" Unknown federation flag used in:\n '%s'\n"
					" Valid federation flags are\n  '%s'\n",
					argv[i]+end, tmp_char);
				xfree(tmp_char);
				exit_code = 1;
			} else
				set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr,
				" Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}

	(*start) = i;

	return set;
}


static int _verify_federations(List name_list, bool report_existing)
{
	int          rc        = SLURM_SUCCESS;
	char        *name      = NULL;
	List         temp_list = NULL;
	ListIterator itr       = NULL;
	ListIterator itr_c     = NULL;
	slurmdb_federation_cond_t fed_cond;

	if (!name_list || !list_count(name_list))
		return SLURM_SUCCESS;

	slurmdb_init_federation_cond(&fed_cond, 0);
	fed_cond.federation_list = name_list;

	temp_list = slurmdb_federations_get(db_conn, &fed_cond);
	if (!temp_list) {
		fprintf(stderr,
			" Problem getting federations from database.  "
			"Contact your admin.\n");
		return SLURM_ERROR;
	}

	itr_c = list_iterator_create(name_list);
	itr = list_iterator_create(temp_list);
	while((name = list_next(itr_c))) {
		slurmdb_federation_rec_t *fed_rec = NULL;

		list_iterator_reset(itr);
		while((fed_rec = list_next(itr))) {
			if (!strcasecmp(fed_rec->name, name))
				break;
		}
		if (fed_rec && report_existing) {
			printf(" This federation %s already exists.  "
			       "Not adding.\n", name);
			list_delete_item(itr_c);
		} else if (!fed_rec && !report_existing) {
			fprintf(stderr,
				" The federation %s doesn't exist.\n",
				name);
			rc = SLURM_ERROR;
		}
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr_c);
	FREE_NULL_LIST(temp_list);
	if (!list_count(name_list) || rc != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

static int _remove_existing_feds(List name_list)
{
	return _verify_federations(name_list, 1);
}

extern int verify_federations_exist(List name_list)
{
	return _verify_federations(name_list, 0);
}

/* Verify that clusters exist in the database.
 * Will remove clusters from list if they are already on the federation or if
 * a cluster is being removed and it doesn't exist on the federation.
 *
 * IN cluster_list: list of slurmdb_cluster_rec_t's with cluster names set.
 * IN fed_name: (optional) Name of federation that is being added/modified.
 * OUT existing_fed: Will be set to TRUE if a cluster in cluster_list is
 *                   assigned to a federation that is not fed_name. If fed_name
 *                   is set to NULL and a cluster is assigned to federation then
 *                   existing_fed will be set to TRUE.
 */
extern int verify_fed_clusters(List cluster_list, const char *fed_name,
			       bool *existing_fed)
{
	char        *missing_str  = NULL;
	char        *existing_str = NULL;
	List         temp_list    = NULL;
	ListIterator itr_db       = NULL;
	ListIterator itr_c        = NULL;
	slurmdb_cluster_rec_t *cluster_rec  = NULL;
	slurmdb_cluster_cond_t cluster_cond;

	/* Get existing clusters from database */
	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = list_create(xfree_ptr);
	itr_c = list_iterator_create(cluster_list);
	while ((cluster_rec = list_next(itr_c))) {
		char *tmp_name = cluster_rec->name;
		if (!tmp_name)
			continue;

		if (tmp_name && (tmp_name[0] == '+' || tmp_name[0] == '-'))
			tmp_name++;
		list_append(cluster_cond.cluster_list, xstrdup(tmp_name));
	}
	temp_list = slurmdb_clusters_get(db_conn, &cluster_cond);
	FREE_NULL_LIST(cluster_cond.cluster_list);
	if (!temp_list) {
		fprintf(stderr,
			" Problem getting clusters from database.  "
			"Contact your admin.\n");
		list_iterator_destroy(itr_c);
		return SLURM_ERROR;
	}

	/* See if the clusters we are looking to add are in the cluster list
	 * from the db. */
	list_iterator_reset(itr_c);
	itr_db = list_iterator_create(temp_list);
	while((cluster_rec = list_next(itr_c))) {
		slurmdb_cluster_rec_t *db_rec = NULL;
		char *tmp_name = cluster_rec->name;
		if (!tmp_name)
			continue;

		if (tmp_name[0] == '+' || tmp_name[0] == '-')
			tmp_name++;

		list_iterator_reset(itr_db);
		while((db_rec = list_next(itr_db))) {
			if (!strcasecmp(db_rec->name, tmp_name))
				break;
		}
		if (!db_rec) {
			xstrfmtcat(missing_str, " The cluster %s doesn't exist."
				   " Please add first.\n", tmp_name);
		} else if (*cluster_rec->name != '-' &&
			   db_rec->fed.name && *db_rec->fed.name) {

			if (fed_name && !xstrcmp(fed_name, db_rec->fed.name)) {
				fprintf(stderr, " The cluster %s is already "
						"assigned to federation %s\n",
					db_rec->name, db_rec->fed.name);
				list_delete_item(itr_c);
			} else {
				xstrfmtcat(existing_str, " The cluster %s is "
					   "assigned to federation %s\n",
					   db_rec->name, db_rec->fed.name);
			}
		} else if (*cluster_rec->name == '-' &&
			   fed_name && xstrcmp(fed_name, db_rec->fed.name)) {
			fprintf(stderr, " The cluster %s isn't assigned to "
					"federation %s\n",
				db_rec->name, fed_name);
			list_delete_item(itr_c);
		} else if (db_rec->flags & CLUSTER_FLAG_EXT) {
			xstrfmtcat(missing_str, " The cluster %s is an external cluster and can't be added to a federation.\n",
				   db_rec->name);
		}
	}

	list_iterator_destroy(itr_db);
	list_iterator_destroy(itr_c);
	FREE_NULL_LIST(temp_list);
	if (missing_str) {
		fprintf(stderr, "%s", missing_str);
		xfree(missing_str);
		return SLURM_ERROR;
	} else if (existing_str) {
		*existing_fed = true;
		fprintf(stderr, "%s", existing_str);
	}
	xfree(existing_str);

	return SLURM_SUCCESS;
}

extern int sacctmgr_add_federation(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i = 0, limit_set = 0;
	slurmdb_federation_rec_t *start_fed =
		xmalloc(sizeof(slurmdb_federation_rec_t));
	List name_list = list_create(xfree_ptr);
	List federation_list;
	ListIterator itr = NULL;
	char *name = NULL;

	slurmdb_init_federation_rec(start_fed, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		limit_set += _set_rec(&i, argc, argv, name_list, start_fed);
	}
	if (exit_code) {
		FREE_NULL_LIST(name_list);
		slurmdb_destroy_federation_rec(start_fed);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		slurmdb_destroy_federation_rec(start_fed);
		FREE_NULL_LIST(name_list);
		fprintf(stderr, " Need name of federation to add.\n");
		return SLURM_ERROR;
	} else if (_remove_existing_feds(name_list)) {
		FREE_NULL_LIST(name_list);
		slurmdb_destroy_federation_rec(start_fed);
		return SLURM_ERROR;
	}

	if ((list_count(name_list) > 1) &&
	    start_fed && start_fed->cluster_list &&
	    list_count(start_fed->cluster_list)) {
		slurmdb_destroy_federation_rec(start_fed);
		FREE_NULL_LIST(name_list);
		fprintf(stderr, " Can't assign clusters to multiple "
				"federations.\n");
		return SLURM_ERROR;
	}
	if (start_fed && start_fed->cluster_list &&
	    list_count(start_fed->cluster_list)) {
		bool existing_feds = false;

		if (list_count(name_list) > 1){
			slurmdb_destroy_federation_rec(start_fed);
			FREE_NULL_LIST(name_list);
			fprintf(stderr, " Can't assign clusters to "
				"multiple federations.\n");
			return SLURM_ERROR;
		}

		/* ensure that clusters exist in db */
		/* and if the clusters are already assigned to another fed. */
		if (verify_fed_clusters(start_fed->cluster_list, NULL,
					&existing_feds)) {
			FREE_NULL_LIST(name_list);
			slurmdb_destroy_federation_rec(start_fed);
			return SLURM_ERROR;
		} else if (existing_feds) {
			char *warning = "\nAre you sure you want to continue?";
			if (!commit_check(warning)) {
				FREE_NULL_LIST(name_list);
				slurmdb_destroy_federation_rec(start_fed);
				return SLURM_ERROR;
			}
		}
	}

	printf(" Adding Federation(s)\n");
	federation_list = list_create(slurmdb_destroy_federation_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		slurmdb_federation_rec_t *fed = NULL;
		if (!name[0]) {
			fprintf(stderr, " Skipping blank fed name.\n");
			continue;
		}
		fed = xmalloc(sizeof(slurmdb_federation_rec_t));
		slurmdb_init_federation_rec(fed, 0);
		list_append(federation_list, fed);
		slurmdb_copy_federation_rec(fed, start_fed);
		fed->name = xstrdup(name);
		printf("  %s\n", fed->name);
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(name_list);

	if (limit_set) {
		printf(" Settings\n");
		sacctmgr_print_federation(start_fed);
	}
	slurmdb_destroy_federation_rec(start_fed);

	if (!list_count(federation_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	notice_thread_init();
	rc = slurmdb_federations_add(db_conn, federation_list);
	notice_thread_fini();

	if (rc == SLURM_SUCCESS) {
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else {
		exit_code = 1;
		fprintf(stderr, " Problem adding federation(s): %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
	}

end_it:
	FREE_NULL_LIST(federation_list);

	return rc;
}

extern int sacctmgr_list_federation(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_federation_cond_t *federation_cond =
		xmalloc(sizeof(slurmdb_federation_cond_t));
	List federation_list;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_federation_rec_t *fed = NULL;
	bool print_clusters = false;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(xfree_ptr);
	List print_fields_list; /* types are of print_field_t */

	slurmdb_init_federation_cond(federation_cond, 0);
	federation_cond->federation_list = list_create(xfree_ptr);
	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, federation_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_federation_cond(federation_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	}

	if (!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				      "Federation,Cluster,ID%2,"
				      "Features,FedState");
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		slurmdb_destroy_federation_cond(federation_cond);
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	federation_list = slurmdb_federations_get(db_conn, federation_cond);
	slurmdb_destroy_federation_cond(federation_cond);

	if (!federation_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(federation_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	/* only print clusters if a cluster field is requested */
	while((field = list_next(itr2))) {
		switch (field->type) {
		case PRINT_CLUSTER:
		case PRINT_FEDSTATE:
		case PRINT_FEDSTATERAW:
		case PRINT_ID:
			print_clusters = true;
			break;
		}
	}
	list_iterator_reset(itr2);

	while ((fed = list_next(itr))) {
		int      curr_inx   = 1;
		char    *tmp_str    = NULL;
		uint32_t tmp_uint32 = 0;
		slurmdb_cluster_rec_t *tmp_cluster = NULL;
		ListIterator itr3 =
			list_iterator_create(fed->cluster_list);

		if (!tree_display && print_clusters)
			tmp_cluster = list_next(itr3);

		do {
			while((field = list_next(itr2))) {
				switch(field->type) {
				/* Federation Specific Fields */
				case PRINT_FEDERATION:
					if (tree_display && tmp_cluster)
						tmp_str = NULL;
					else
						tmp_str = fed->name;
					field->print_routine(
						field, tmp_str,
						(curr_inx == field_count));
					break;
				case PRINT_FLAGS:
					if (tree_display && tmp_cluster)
						tmp_str = NULL;
					else {
						tmp_str =
						slurmdb_federation_flags_str(
								fed->flags);
					}
					field->print_routine(
						field, tmp_str,
						(curr_inx == field_count));

					if (tmp_str)
						xfree(tmp_str);
					break;

				/* Cluster Specific Fields */
				case PRINT_CLUSTER:
					if (!tmp_cluster)
						tmp_str = NULL;
					else
						tmp_str = tmp_cluster->name;
					field->print_routine(
						field, tmp_str,
						(curr_inx == field_count));
					break;
				case PRINT_FEATURES:
				{
					List tmp_list = NULL;
					if (tmp_cluster)
						tmp_list = tmp_cluster->
							fed.feature_list;
					field->print_routine(
						field, &tmp_list,
						(curr_inx == field_count));
					break;
				}
				case PRINT_FEDSTATE:
					if (!tmp_cluster)
						tmp_str = NULL;
					else {
						tmp_str =
						slurmdb_cluster_fed_states_str(
							tmp_cluster->fed.state);
					}
					field->print_routine(
						field, tmp_str,
						(curr_inx == field_count));
					break;
				case PRINT_FEDSTATERAW:
					if (!tmp_cluster)
						tmp_uint32 = NO_VAL;
					else
						tmp_uint32 =
							tmp_cluster->fed.state;
					field->print_routine(
						field, &tmp_uint32,
						(curr_inx == field_count));
					break;
				case PRINT_ID:
					if (!tmp_cluster)
						tmp_uint32 = NO_VAL;
					else
						tmp_uint32 =
							tmp_cluster->fed.id;
					field->print_routine(
						field, &tmp_uint32,
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
		} while(print_clusters && (tmp_cluster = list_next(itr3)));
		list_iterator_destroy(itr3);
	}

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(federation_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

/* Add clusters to be removed if "setting" a federation to a specific set of
 * clusters or clearing all clusters.
 *
 * IN cluster_list: list of slurmdb_cluster_rec_t's with cluster names set that
 *                  are to be "set" on the federation the federation.
 * IN federation: name of the federation that is being added/modified.
 */
static int _add_clusters_to_remove(List cluster_list, const char *federation)
{
	List        db_list = NULL;
	ListIterator db_itr = NULL;
	slurmdb_federation_cond_t db_cond;
	slurmdb_federation_rec_t *db_rec = NULL;
	slurmdb_cluster_rec_t    *db_cluster = NULL;

	slurmdb_init_federation_cond(&db_cond, 0);
	db_cond.federation_list = list_create(xfree_ptr);
	list_append(db_cond.federation_list, xstrdup(federation));

	db_list = slurmdb_federations_get(db_conn, &db_cond);
	if (!db_list || !list_count(db_list)) {
		fprintf(stderr, " Problem getting federations "
			"from database. Contact your admin.\n");
		return SLURM_ERROR;
	}
	FREE_NULL_LIST(db_cond.federation_list);
	db_rec = list_peek(db_list);
	db_itr = list_iterator_create(db_rec->cluster_list);
	while ((db_cluster = list_next(db_itr))) {
		bool found_cluster = false;
		slurmdb_cluster_rec_t *orig_cluster = NULL;
		ListIterator orig_itr = list_iterator_create(cluster_list);

		/* Figure out if cluster in cluster_list is already on the
		 * federation. If it is, don't add to list to remove */
		while ((orig_cluster = list_next(orig_itr))) {
			char *db_name = db_cluster->name;
			if (*db_name == '+' || *db_name == '-')
				++db_name;
			if (!xstrcmp(orig_cluster->name, db_name)) {
				found_cluster = true;
				break;
			}
		}
		list_iterator_destroy(orig_itr);
		if (found_cluster)
			continue;

		slurmdb_cluster_rec_t *cluster =
			xmalloc(sizeof(slurmdb_cluster_rec_t));
		slurmdb_init_cluster_rec(cluster, 0);
		cluster->name = xstrdup_printf("-%s", db_cluster->name);
		list_append(cluster_list, cluster);
	}
	list_iterator_destroy(db_itr);
	FREE_NULL_LIST(db_list);

	return SLURM_SUCCESS;
}

/* Change add mode of clusters to be added to federation to += mode.
 * A cluster that is already part of a federation will be removed from the list
 * to set the federation clusters to, so all assigns need to be changed to '+'
 * plus modes. Clusters that are to be removed from the federation clustesr will
 * have already been added to the list in '-' mode.
 */
static int _change_assigns_to_adds(List cluster_list)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = list_iterator_create(cluster_list);
	slurmdb_cluster_rec_t *cluster = NULL;

	while ((cluster = list_next(itr))) {
		if (cluster->name && *cluster->name &&
		    (*cluster->name != '-' && *cluster->name != '+')) {
			char *tmp_name = xstrdup_printf("+%s", cluster->name);
			xfree(cluster->name);
			cluster->name = tmp_name;
		}
	}

	return rc;
}

extern int sacctmgr_modify_federation(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i=0;
	int cond_set = 0, prev_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;
	slurmdb_federation_cond_t *federation_cond =
		xmalloc(sizeof(slurmdb_federation_cond_t));
	slurmdb_federation_rec_t *federation =
		xmalloc(sizeof(slurmdb_federation_rec_t));

	slurmdb_init_federation_cond(federation_cond, 0);
	slurmdb_init_federation_rec(federation, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			prev_set = _set_cond(&i, argc, argv,
					     federation_cond, NULL);
			cond_set |= prev_set;
		} else if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			prev_set = _set_rec(&i, argc, argv, NULL, federation);
			rec_set |= prev_set;
		} else {
			prev_set = _set_cond(&i, argc, argv,
					     federation_cond, NULL);
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
		if (!commit_check("You didn't set any conditions "
				  "with 'WHERE'.\n"
				  "Are you sure you want to continue?")) {
			printf("Aborted\n");
			rc = SLURM_SUCCESS;
			goto end_it;
		}
	} else if (verify_federations_exist(
					federation_cond->federation_list)) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (federation->cluster_list) {
		bool existing_feds = false;
		char *mod_fed = NULL;
		slurmdb_cluster_rec_t *tmp_c = NULL;
		List cluster_list = federation->cluster_list;

		if (!federation_cond->federation_list ||
		    (list_count(federation_cond->federation_list) != 1)) {
			fprintf(stderr, " Can't assign clusters to multiple federations.\n");
			rc = SLURM_ERROR;
			goto end_it;
		}

		/* Add all clusters that need to be removed if clearing all
		 * clusters or add clusters that will be removed if setting
		 * clusters to specific set. */
		mod_fed = list_peek(federation_cond->federation_list);
		if ((!list_count(cluster_list) ||
		     ((tmp_c = list_peek(cluster_list)) &&
		      *tmp_c->name != '-' && *tmp_c->name != '+')) &&
		    ((rc = _add_clusters_to_remove(cluster_list, mod_fed)) ||
		     (rc = _change_assigns_to_adds(cluster_list)))) {
			goto end_it;
		} else if ((rc = verify_fed_clusters(cluster_list, mod_fed,
						     &existing_feds))) {
			goto end_it;
		} else if (!list_count(cluster_list)) {
			printf("Nothing to change\n");
			rc = SLURM_ERROR;
			goto end_it;
		} else if (existing_feds) {
			char *warning = "\nAre you sure you want to continue?";
			if (!commit_check(warning)) {
				rc = SLURM_ERROR;
				goto end_it;
			}
		}
	}

	printf(" Setting\n");
	sacctmgr_print_federation(federation);

	notice_thread_init();
	ret_list = slurmdb_federations_modify(db_conn,
					      federation_cond,
					      federation);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified federation...\n");
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

	if (set) {
		if (commit_check("Would you like to commit changes?"))
			slurmdb_connection_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	}
end_it:
	slurmdb_destroy_federation_cond(federation_cond);
	slurmdb_destroy_federation_rec(federation);

	return rc;
}

extern int sacctmgr_delete_federation(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_federation_cond_t *fed_cond =
		xmalloc(sizeof(slurmdb_federation_cond_t));
	int i=0;
	List ret_list = NULL;
	int cond_set = 0, prev_set;

	slurmdb_init_federation_cond(fed_cond, 0);
	fed_cond->federation_list = list_create(xfree_ptr);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, fed_cond, NULL);
		cond_set |= prev_set;
	}

	if (exit_code) {
		slurmdb_destroy_federation_cond(fed_cond);
		return SLURM_ERROR;
	} else if (!cond_set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_federation_cond(fed_cond);
		return SLURM_ERROR;
	}

	if (!list_count(fed_cond->federation_list)) {
		exit_code=1;
		fprintf(stderr,
			"problem with delete request.  "
			"Nothing given to delete.\n");
		slurmdb_destroy_federation_cond(fed_cond);
		return SLURM_SUCCESS;
	}
	notice_thread_init();
	ret_list = slurmdb_federations_remove(db_conn, fed_cond);
	rc = errno;
	notice_thread_fini();

	slurmdb_destroy_federation_cond(fed_cond);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);

		printf(" Deleting federations...\n");
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
