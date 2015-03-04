/****************************************************************************\
 *  resource_functions.c - functions dealing with resources in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2013 Bull S. A. S.
 *		Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois.
 *
 *  Written by Bill Brophy <bill.brophy@bull.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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
\****************************************************************************/

#include "src/sacctmgr/sacctmgr.h"

static void _print_overcommit(slurmdb_res_rec_t *res,
			      slurmdb_res_cond_t *res_cond)
{
	List res_list = NULL, cluster_list = NULL;
	ListIterator itr, clus_itr = NULL, found_clus_itr = NULL;
	slurmdb_res_rec_t *found_res;
	slurmdb_clus_res_rec_t *clus_res = NULL;
	char *cluster;

	if (res->percent_used == (uint16_t)NO_VAL)
		return;

	/* Don't use the global g_res_list since we are going to
	 * change the contents of this one.
	 */
	res_cond->with_clusters = 1;

	if (res_cond->cluster_list) {
		cluster_list = res_cond->cluster_list;
		res_cond->cluster_list = NULL;
	}

	res_list = acct_storage_g_get_res(db_conn, my_uid, res_cond);
	if (!res_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting system resources "
			"from database.  Contact your admin.\n");
		return;
	}

	itr = list_iterator_create(res_list);
	while ((found_res = list_next(itr))) {
		int total = 0, percent_allowed;
		fprintf(stderr, "  %s@%s\n",
			found_res->name, found_res->server);
		if (cluster_list)
			clus_itr = list_iterator_create(cluster_list);
		if (found_res->clus_res_list) {
			found_clus_itr = list_iterator_create(
				found_res->clus_res_list);
			while ((clus_res = list_next(found_clus_itr))) {
				cluster = NULL;
				if (clus_itr) {
					while ((cluster = list_next(clus_itr)))
						if (!strcmp(cluster,
							    clus_res->cluster))
						    break;
					list_iterator_reset(clus_itr);
				} else /* This means we didn't specify
					  any clusters (All clusters
					  are overwritten with the
					  requested percentage) so
					  just put something there to
					  get the correct percent_allowed.
				       */
					cluster = "nothing";

				percent_allowed = cluster ? res->percent_used :
					clus_res->percent_allowed;
				total += percent_allowed;

				fprintf(stderr,
					"   Cluster - %s\t %u%%\n",
					clus_res->cluster,
					percent_allowed);
			}
		} else if (clus_itr) {
			while ((cluster = list_next(clus_itr))) {
				total += res->percent_used;
				if (clus_res) {
					fprintf(stderr,
						"   Cluster - %s\t %u%%\n",
						clus_res->cluster,
						res->percent_used);
				} else {
					error("%s: clus_res is NULL", __func__);
				}
			}
		}
		if (clus_itr)
			list_iterator_destroy(clus_itr);
		if (found_clus_itr)
			list_iterator_destroy(found_clus_itr);
		fprintf(stderr, "   total\t\t%u%%\n", total);
	}
	list_iterator_destroy(itr);

	if (cluster_list) {
		res_cond->cluster_list = cluster_list;
		cluster_list = NULL;
	}
}

static int _set_res_cond(int *start, int argc, char *argv[],
			     slurmdb_res_cond_t *res_cond,
			     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;

	if (!res_cond) {
		error("No res_cond given");
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

		if (!strncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !strncasecmp(argv[i], "WithDeleted",
						 MAX(command_len, 5))) {
			res_cond->with_deleted = 1;
		} else if (!end && !strncasecmp(argv[i], "WithClusters",
						 MAX(command_len, 5))) {
			res_cond->with_clusters = 1;
		} else if (!end && !strncasecmp(argv[i], "where",
						MAX(command_len, 5))) {
			continue;
		} else if (!end
			   || !strncasecmp(argv[i], "Names",
					   MAX(command_len, 1))) {
			if (!res_cond->name_list) {
				res_cond->name_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(res_cond->name_list,
						  argv[i]+end))
				set = 1;
		} else if (!end
			   || !strncasecmp(argv[i], "Clusters",
					   MAX(command_len, 1))) {
			if (!res_cond->cluster_list) {
				res_cond->cluster_list =
					list_create(slurm_destroy_char);
			}

			slurm_addto_char_list(res_cond->cluster_list,
					      argv[i]+end);
			if (sacctmgr_validate_cluster_list(
				    res_cond->cluster_list) != SLURM_SUCCESS) {
				exit_code=1;
				fprintf(stderr,
					" Need a valid cluster name to "
					"add a cluster resource.\n");
			} else
				set = 1;
		} else if (!strncasecmp(argv[i], "Descriptions",
				MAX(command_len, 1))) {
		if (!res_cond->description_list) {
			res_cond->description_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(
				    res_cond->description_list,
				    argv[i]+end))
				set = 1;
		} else if (!strncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp(argv[i], "Ids", MAX(command_len, 1))) {
			ListIterator itr = NULL;
			char *temp = NULL;
			uint32_t id = 0;

			if (!res_cond->id_list) {
				res_cond->id_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(res_cond->id_list,
						  argv[i]+end))
				set = 1;

			/* check to make sure user gave ints here */
			itr = list_iterator_create(res_cond->id_list);
			while ((temp = list_next(itr))) {
				if (get_uint(temp, &id, "RES ID")
				    != SLURM_SUCCESS) {
					exit_code = 1;
					list_delete_item(itr);
				}
			}
			list_iterator_destroy(itr);
		} else if (!strncasecmp(argv[i], "PercentAllowed",
					 MAX(command_len, 1))) {
			if (!res_cond->percent_list) {
				res_cond->percent_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(res_cond->percent_list,
						  argv[i]+end))
				set = 1;
		} else if (!strncasecmp(argv[i], "ServerType",
					 MAX(command_len, 7))) {
			if (!res_cond->manager_list) {
				res_cond->manager_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(res_cond->manager_list,
						  argv[i]+end))
				set = 1;
		} else if (!strncasecmp(argv[i], "Server",
					 MAX(command_len, 2))) {
			if (!res_cond->server_list) {
				res_cond->server_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(res_cond->server_list,
						  argv[i]+end))
				set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify "
				"SLURM_PRINT_VALUE\n", argv[i]);
		}
	}

	(*start) = i;
	return set;
}

static int _set_res_rec(int *start, int argc, char *argv[],
			List name_list, List cluster_list,
			slurmdb_res_rec_t *res)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;
	int option = 0;

	xassert(res);

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
			   || !strncasecmp(argv[i], "Resources",
					   MAX(command_len, 1))) {
			if (name_list)
				slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!strncasecmp(argv[i], "Clusters",
					 MAX(command_len, 1))) {
			if (cluster_list) {
				slurm_addto_char_list(cluster_list,
						      argv[i]+end);
				if (sacctmgr_validate_cluster_list(
					    cluster_list) != SLURM_SUCCESS) {
					exit_code=1;
					fprintf(stderr,
						" Need a valid cluster name to "
						"add a cluster resource.\n");
				}
			} else {
				exit_code=1;
				fprintf(stderr,
					" Can't modify the cluster "
					"of an resource\n");
			}
		} else if (!strncasecmp(argv[i], "Count",
					MAX(command_len, 3))) {
			if (get_uint(argv[i]+end, &res->count,
				     "count") == SLURM_SUCCESS) {
				set = 1;
			}
		} else if (!strncasecmp(argv[i], "Description",
					 MAX(command_len, 1))) {
			if (!res->description)
				res->description =
					strip_quotes(argv[i]+end, NULL, 1);
			set = 1;
		} else if (!strncasecmp(argv[i], "Flags",
					 MAX(command_len, 2))) {
			res->flags = str_2_res_flags(argv[i]+end, option);
			if (res->flags == SLURMDB_RES_FLAG_NOTSET) {
				char *tmp_char = slurmdb_res_flags_str(
					SLURMDB_RES_FLAG_BASE);
				printf(" Unknown Server Resource flag used "
				       "in:\n  '%s'\n"
				       " Valid Server Resource flags are\n"
				       " '%s'\n", argv[i]+end, tmp_char);
				xfree(tmp_char);
				exit_code = 1;
			} else
				set = 1;

		} else if (!strncasecmp(argv[i], "Server",
					MAX(command_len, 1))) {
			if (!res->server) {
				res->server=
					strip_quotes(argv[i]+end, NULL, 1);
			}
			set = 1;
		} else if (!strncasecmp(argv[i], "ServerType",
					MAX(command_len, 1))) {
			if (!res->manager)
				res->manager =
					strip_quotes(argv[i]+end, NULL, 1);
			set = 1;
		} else if (!strncasecmp(argv[i], "PercentAllowed",
					MAX(command_len, 1))) {
			/* overload percent_used here */
			if (get_uint16(argv[i]+end, &res->percent_used,
				       "PercentAllowed") == SLURM_SUCCESS) {
				set = 1;
			}
		} else if (!strncasecmp(argv[i], "Type",
					MAX(command_len, 1))) {
			char *temp = strip_quotes(argv[i]+end, NULL, 1);

			if (!strncasecmp("License", temp,
					 MAX(strlen(temp), 1))) {
				res->type = SLURMDB_RESOURCE_LICENSE;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Unknown resource type: '%s'  "
					"Valid resources is License.\n",
					temp);
			}
		} else {
			exit_code = 1;
			printf(" Unknown option: %s\n"
			       " Use keyword 'where' to modify condition\n",
			       argv[i]);
		}
	}

	(*start) = i;

	return set;
}

static void _print_res_format(slurmdb_res_rec_t *res,
			      slurmdb_clus_res_rec_t *clus_res,
			      ListIterator itr,
			      int field_count)
{
	int curr_inx = 1;
	char *tmp_char;
	print_field_t *field = NULL;
	uint32_t count;

	xassert(itr);
	xassert(res);

	while ((field = list_next(itr))) {
		switch(field->type) {
		case PRINT_ALLOWED:
			field->print_routine(
				field, clus_res ? clus_res->percent_allowed : 0,
				(curr_inx == field_count));
			break;
		case PRINT_CLUSTER:
			field->print_routine(
				field, clus_res ? clus_res->cluster : NULL,
				(curr_inx == field_count));
			break;
		case PRINT_CALLOWED:
			if (clus_res)
				count = (res->count *
					 clus_res->percent_allowed) / 100;
			else
				count = 0;
			field->print_routine(field, count,
					     (curr_inx == field_count));
			break;
		case PRINT_COUNT:
			field->print_routine(field,
					     res->count,
					     (curr_inx == field_count));
			break;
		case PRINT_DESC:
			field->print_routine(
				field, res->description,
				(curr_inx == field_count));
			break;
		case PRINT_ID:
			field->print_routine(
				field, res->id,
				(curr_inx == field_count));
			break;
		case PRINT_FLAGS:
			tmp_char = slurmdb_res_flags_str(res->flags);
			field->print_routine(
				field,
				tmp_char,
				(curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_SERVERTYPE:
			field->print_routine(field,
					     res->manager,
					     (curr_inx == field_count));
			break;
		case PRINT_NAME:
			field->print_routine(
				field, res->name,
				(curr_inx == field_count));
			break;
		case PRINT_SERVER:
			field->print_routine(field,
					     res->server,
					     (curr_inx == field_count));
			break;
		case PRINT_TYPE:
			field->print_routine(field,
					     slurmdb_res_type_str(
						     res->type),
					     (curr_inx == field_count));
			break;
		case PRINT_ALLOCATED:
			field->print_routine(
				field, res->percent_used,
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
	list_iterator_reset(itr);
	printf("\n");
}

extern int sacctmgr_add_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	int i=0, limit_set=0;
	ListIterator itr = NULL;
	ListIterator clus_itr = NULL;
	slurmdb_res_rec_t *res = NULL;
	slurmdb_res_rec_t *found_res = NULL;
	slurmdb_res_rec_t *start_res = xmalloc(sizeof(slurmdb_res_rec_t));
	List cluster_list = list_create(slurm_destroy_char);
	List name_list = list_create(slurm_destroy_char);
	char *name = NULL;
	List res_list = NULL;
	char *res_str = NULL;

	slurmdb_init_res_rec(start_res, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;

		limit_set += _set_res_rec(&i, argc, argv, name_list,
					  cluster_list, start_res);
	}

	if (exit_code) {
		FREE_NULL_LIST(name_list);
		FREE_NULL_LIST(cluster_list);
		slurmdb_destroy_res_rec(start_res);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		FREE_NULL_LIST(name_list);
		FREE_NULL_LIST(cluster_list);
		slurmdb_destroy_res_rec(start_res);
		exit_code=1;
		fprintf(stderr, " Need name of resource to add.\n");
		return SLURM_SUCCESS;
	}

	if (!start_res->server) {
		/* assign some server name */
		start_res->server = xstrdup("slurmdb");
	}

	if (!g_res_list) {
		slurmdb_res_cond_t res_cond;
		slurmdb_init_res_cond(&res_cond, 0);
		/* 2 means return all resources even if they don't
		   have clusters attached to them.
		*/
		res_cond.with_clusters = 2;
		g_res_list = acct_storage_g_get_res(db_conn, my_uid, &res_cond);
		if (!g_res_list) {
			exit_code=1;
			fprintf(stderr, " Problem getting system resources "
				"from database.  "
				"Contact your admin.\n");
			FREE_NULL_LIST(name_list);
			FREE_NULL_LIST(cluster_list);
			slurmdb_destroy_res_rec(start_res);
			return SLURM_ERROR;
		}
	}

	res_list = list_create(slurmdb_destroy_res_rec);

	itr = list_iterator_create(name_list);
	if (cluster_list)
		clus_itr = list_iterator_create(cluster_list);
	while ((name = list_next(itr))) {
		bool added = 0;
		found_res = sacctmgr_find_res_from_list(
			g_res_list, NO_VAL, name, start_res->server);
		if (!found_res) {
			if (start_res->type == SLURMDB_RESOURCE_NOTSET) {
				exit_code=1;
				fprintf(stderr,
					" Need to designate a resource "
					"type to initially add '%s'.\n", name);
				break;

			} else if (start_res->count == NO_VAL) {
				exit_code=1;
				fprintf(stderr,
					" Need to designate a resource "
					"count to initially add '%s'.\n", name);
				break;
			}
			added = 1;
			res = xmalloc(sizeof(slurmdb_res_rec_t));
			slurmdb_init_res_rec(res, 0);
			res->name = xstrdup(name);
			res->description =
				xstrdup(start_res->description ?
					start_res->description : name);
			res->manager = xstrdup(start_res->manager);
			res->server = xstrdup(start_res->server);
			res->count = start_res->count;
			res->flags = start_res->flags;
			res->type = start_res->type;
			res->percent_used = 0;

			xstrfmtcat(res_str, "  %s@%s\n",
				   res->name, res->server);
			list_append(res_list, res);
		}

		if (cluster_list && list_count(cluster_list)) {
			ListIterator found_itr = NULL;
			slurmdb_clus_res_rec_t *clus_res;
			char *cluster;
			uint16_t start_used = 0;

			if (found_res) {
				if (found_res->clus_res_list)
					found_itr = list_iterator_create(
						found_res->clus_res_list);
				res = xmalloc(sizeof(slurmdb_res_rec_t));
				slurmdb_init_res_rec(res, 0);
				res->id = found_res->id;
				res->type = found_res->type;
				res->server = xstrdup(found_res->server);
				start_used = res->percent_used =
					found_res->percent_used;
			}

			res->clus_res_list = list_create(
				slurmdb_destroy_clus_res_rec);

			while ((cluster = list_next(clus_itr))) {
				clus_res = NULL;
				if (found_itr) {
					while ((clus_res =
						list_next(found_itr))) {
						if (!strcmp(clus_res->cluster,
							    cluster))
							break;
					}
					list_iterator_reset(found_itr);
				}

				if (!clus_res) {
					if (!added) {
						added = 1;
						xstrfmtcat(res_str,
							   "  %s@%s\n", name,
							   res->server);
						list_append(res_list, res);
					}
					/* make sure we don't overcommit */
					res->percent_used +=
						start_res->percent_used;
					if (res->percent_used > 100) {
						exit_code=1;
						fprintf(stderr,
							" Adding this %d "
							"clusters to resource "
							"%s@%s at %u%% each "
							", with %u%% already "
							"used,  would go over "
							"100%%.  Please redo "
							"your math and "
							"resubmit.\n",
							list_count(
								cluster_list),
							res->name, res->server,
							start_res->percent_used,
							start_used);
						break;
					}
					clus_res = xmalloc(
						sizeof(slurmdb_clus_res_rec_t));
					list_append(res->clus_res_list,
						    clus_res);
					clus_res->cluster = xstrdup(cluster);
					clus_res->percent_allowed =
						start_res->percent_used;
					xstrfmtcat(res_str,
						   "   Cluster - %s\t%u%%\n",
						   cluster,
						   clus_res->percent_allowed);
					/* FIXME: make sure we don't
					   overcommit */
				}
			}
			if (res->percent_used > 100)
				break;

			if (found_itr)
				list_iterator_destroy(found_itr);

			if (!added)
				slurmdb_destroy_res_rec(res);

			list_iterator_reset(clus_itr);
		}
	}

	if (cluster_list)
		list_iterator_destroy(clus_itr);

	list_iterator_destroy(itr);

	FREE_NULL_LIST(name_list);
	FREE_NULL_LIST(cluster_list);

	if (exit_code) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (!list_count(res_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (res_str) {
		char *tmp_str;
		switch (res->type) {
		case SLURMDB_RESOURCE_LICENSE:
			tmp_str = "License";
			break;
		default:
			tmp_str = "Unknown";
		}
		printf(" Adding Resource(s)\n%s", res_str);
		printf(" Settings\n");
		if (res->name)
			printf("  Name           = %s\n", res->name);
		if (res->server)
			printf("  Server         = %s\n", res->server);
		if (res->description)
			printf("  Description    = %s\n", res->description);
		if (res->manager)
			printf("  ServerType     = %s\n", res->manager);
		if (res->count != NO_VAL)
			printf("  Count          = %u\n", res->count);
		printf("  Type           = %s\n", tmp_str);

		xfree(res_str);
	}

	if (list_count(res_list)) {
		notice_thread_init();
		rc = acct_storage_g_add_res(db_conn, my_uid, res_list);
		notice_thread_fini();
	} else
		goto end_it;
	if (rc == SLURM_SUCCESS) {
		if (commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding system resource: %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
	}

end_it:
	FREE_NULL_LIST(res_list);
	slurmdb_destroy_res_rec(start_res);
	return rc;
}

extern int sacctmgr_list_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_res_cond_t *res_cond = xmalloc(sizeof(slurmdb_res_cond_t));
 	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_res_rec_t *res = NULL;
	slurmdb_clus_res_rec_t *clus_res = NULL;
	List res_list = NULL;
	int field_count = 0;
	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	slurmdb_init_res_cond(res_cond, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_res_cond(&i, argc, argv, res_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_res_cond(res_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	} else if (!list_count(format_list)) {
		slurm_addto_char_list(
			format_list,
			"Name,Server,Type,Count,Allocated,ServerType");
		if (res_cond->with_clusters)
			slurm_addto_char_list(
				format_list, "Cluster,Allowed");
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}
	res_list = acct_storage_g_get_res(db_conn, my_uid, res_cond);
	slurmdb_destroy_res_cond(res_cond);

	if (!res_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}
	itr = list_iterator_create(res_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	while ((res = list_next(itr))) {
		if (res_cond->with_clusters && res->clus_res_list
		    && list_count(res->clus_res_list)) {
			ListIterator clus_itr = list_iterator_create(
				res->clus_res_list);
			while ((clus_res = list_next(clus_itr))) {
				_print_res_format(res, clus_res,
						  itr2, field_count);
			}
			list_iterator_destroy(clus_itr);
		} else
			_print_res_format(res, NULL, itr2, field_count);

	}
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(res_list);
	FREE_NULL_LIST(print_fields_list);
	return rc;
}

extern int sacctmgr_modify_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_res_cond_t *res_cond =
		xmalloc(sizeof(slurmdb_res_cond_t));
	slurmdb_res_rec_t *res =
		xmalloc(sizeof(slurmdb_res_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	slurmdb_init_res_cond(res_cond, 0);
	slurmdb_init_res_rec(res, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			cond_set += _set_res_cond(&i, argc, argv,
						  res_cond, NULL);

		} else if (!strncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			rec_set += _set_res_rec(&i, argc, argv,
						NULL, NULL, res);
		} else {
			cond_set += _set_res_cond(&i, argc, argv,
						  res_cond, NULL);
		}
	}

	if (exit_code) {
		slurmdb_destroy_res_cond(res_cond);
		slurmdb_destroy_res_rec(res);
		return SLURM_ERROR;
	} else if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		slurmdb_destroy_res_cond(res_cond);
		slurmdb_destroy_res_rec(res);
		return SLURM_ERROR;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions with "
				  "'WHERE'.\n"
				  "Are you sure you want to continue?")) {
			printf("Aborted\n");
			slurmdb_destroy_res_cond(res_cond);
			slurmdb_destroy_res_rec(res);
			return SLURM_SUCCESS;
		}
	}

	if (res->count != NO_VAL && res_cond->cluster_list &&
			list_count(res_cond->cluster_list)) {
		fprintf(stderr, "Can't change \"count\" on a cluster-based "
			"resource. Remove cluster selection.\n");
		return SLURM_ERROR;
	} else if (res->percent_used != (uint16_t)NO_VAL &&
			!res_cond->cluster_list) {
		fprintf(stderr, "Can't change \"percentallowed\" without "
			"specifying a cluster.\n");
		return SLURM_ERROR;
	}

	notice_thread_init();
	ret_list = acct_storage_g_modify_res(db_conn, my_uid, res_cond, res);
	notice_thread_fini();
	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified server resource ...\n");
		while ((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if (ret_list) {
		printf(" Nothing modified\n");
		rc = SLURM_ERROR;
	} else if (errno == ESLURM_OVER_ALLOCATE) {
		exit_code=1;
		rc = SLURM_ERROR;
		fprintf(stderr,
			" If change was accepted it would look like this...\n");
		_print_overcommit(res, res_cond);

	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	if (set) {
		if (commit_check("Would you like to commit changes?")){
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}

	FREE_NULL_LIST(ret_list);

	slurmdb_destroy_res_cond(res_cond);
	slurmdb_destroy_res_rec(res);
	return rc;
}

extern int sacctmgr_delete_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_res_cond_t *res_cond = xmalloc(sizeof(slurmdb_res_cond_t));
	int i=0;
	List ret_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *name = NULL;

	slurmdb_init_res_cond(res_cond, 0);


	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		set += _set_res_cond(&i, argc, argv, res_cond, NULL);
	}

	if (!set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_res_cond(res_cond);
		return SLURM_ERROR;
	} else if (set == -1) {
		slurmdb_destroy_res_cond(res_cond);
		return SLURM_ERROR;
	}

	notice_thread_init();
	ret_list = acct_storage_g_remove_res(db_conn, my_uid, res_cond);
	notice_thread_fini();
	slurmdb_destroy_res_cond(res_cond);

	if (ret_list && list_count(ret_list)) {
		itr = list_iterator_create(ret_list);
		printf(" Deleting resource(s)...\n");

		while ((name = list_next(itr))) {
			printf("  %s\n", name);
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

	FREE_NULL_LIST(ret_list);

	xfree(name);
	return rc;
}
