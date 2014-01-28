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

static bool with_deleted = 0;
static uint16_t oversubscribed = 1;
static uint16_t duplicate = 2;

static int _check_oversubscription (List new_lic_list, List clus_res_list,
				     uint16_t allocated, bool create)
{
ListIterator itr = NULL;
ListIterator itr1 = NULL;
uint16_t rc = 0;
slurmdb_clus_res_rec_t *new_clus_res = NULL;
slurmdb_clus_res_rec_t *clus_res = NULL;
uint16_t total_allocated = 0;

	itr = list_iterator_create(new_lic_list);
	while ((new_clus_res = list_next(itr))) {
		total_allocated = allocated;
		itr1 = list_iterator_create(clus_res_list);
		while ((clus_res = list_next(itr1))) {
			if (!strcasecmp(new_clus_res->res_ptr->name,
			    clus_res->res_ptr->name) &&
			    strcasecmp(new_clus_res->cluster,
			    clus_res->cluster)) {
				total_allocated += clus_res->percent_allowed;
			}
			if (!strcasecmp(new_clus_res->res_ptr->name,
			    clus_res->res_ptr->name) &&
			    !strcasecmp(new_clus_res->cluster,
			    clus_res->cluster)  && create) {
				list_iterator_destroy(itr1);
				list_iterator_destroy(itr);
				return duplicate;
			}
		}
		list_iterator_destroy(itr1);
		if (total_allocated > 100) {
			rc = oversubscribed;
			break;
		}
	}
	list_iterator_destroy(itr);
	return rc;
}

static int _populate_cluster_name_list(List new_name_list)
{
	List cluster_list = NULL;
	ListIterator itr = NULL;
	char *cluster_name;
	slurmdb_cluster_rec_t *cluster = NULL;

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid,
						   NULL);
	if (!cluster_list) {
		exit_code=1;
		fprintf(stderr,
			" Error obtaining cluster records.\n");
		return SLURM_ERROR;
	}
	itr = list_iterator_create(cluster_list);
	while ((cluster = list_next(itr))) {
		cluster_name = xstrdup(cluster->name);
		list_append(new_name_list, cluster_name);
	}
		list_iterator_destroy(itr);
		list_destroy(cluster_list);
	return SLURM_SUCCESS;
}

static int _get_g_clus_res_list ()
{
	int rc = 0;
	slurmdb_clus_res_cond_t *cluster_lic_cond = NULL;

	if (!g_clus_res_list) {
		cluster_lic_cond = xmalloc(sizeof(slurmdb_clus_res_cond_t));
		slurmdb_init_clus_res_cond(cluster_lic_cond, 0);
		cluster_lic_cond->cluster_list =
			list_create(slurm_destroy_char);
		rc =
		    _populate_cluster_name_list(cluster_lic_cond->cluster_list);
		if (rc != SLURM_SUCCESS)  {
			exit_code = 1;
			fprintf(stderr,
				" Error obtaining cluster names.\n");
			slurmdb_destroy_clus_res_cond(cluster_lic_cond);
			return SLURM_ERROR;
		}
		g_clus_res_list =
			acct_storage_g_get_clus_res(db_conn, my_uid,
						    cluster_lic_cond);
		slurmdb_destroy_clus_res_cond(cluster_lic_cond);
	}
	return SLURM_SUCCESS;
}

static int _set_clus_res_cond(int *start, int argc, char *argv[],
		     slurmdb_clus_res_cond_t *clus_res_cond,
		     List format_list)
{
	int i;
	int rc = SLURM_SUCCESS;
	int set = 0;
	int end = 0;
	int command_len = 0;

	if (!clus_res_cond) {
		error("No clus_res_cond given");
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
		if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !strncasecmp (argv[i], "WithDeleted",
						 MAX(command_len, 5))) {
			clus_res_cond->with_deleted = 1;
		} else if (!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end
			  || !strncasecmp (argv[i], "Names",
					   MAX(command_len, 1))) {
			if (!clus_res_cond->name_list) {
				clus_res_cond->name_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(clus_res_cond->name_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Manager",
					   MAX(command_len, 1))) {
			if (!clus_res_cond->manager_list) {
				clus_res_cond->manager_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(clus_res_cond->manager_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Server",
					   MAX(command_len, 1))) {
			if (!clus_res_cond->server_list) {
				clus_res_cond->server_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(clus_res_cond->server_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Descriptions",
					MAX(command_len, 1))) {
			if (!clus_res_cond->description_list) {
				clus_res_cond->description_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(
			    clus_res_cond->description_list, argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Clusters",
					 MAX(command_len, 3))) {
			if (!clus_res_cond->cluster_list) {
				clus_res_cond->cluster_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(clus_res_cond->cluster_list,
						  argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);

		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify "
				"SLURM_PRINT_VALUE\n", argv[i]);
		}
	}
	if (!clus_res_cond->cluster_list) {
		clus_res_cond->cluster_list = list_create(slurm_destroy_char);
		rc = _populate_cluster_name_list(clus_res_cond->cluster_list);
		if (rc != SLURM_SUCCESS)  {
			exit_code = 1;
			fprintf(stderr,
				" Error obtaining cluster names.\n");
			return SLURM_ERROR;
		}
	}
	(*start) = i;
	return set;
}

static int _set_ser_res_cond(int *start, int argc, char *argv[],
		     slurmdb_ser_res_cond_t *ser_res_cond,
		     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;
	with_deleted = 0;

	if (!ser_res_cond) {
		error("No ser_res_cond given");
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

		if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !strncasecmp (argv[i], "WithDeleted",
						 MAX(command_len, 5))) {
			ser_res_cond->with_deleted = 1;
		} else if (!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end
			  || !strncasecmp (argv[i], "Names",
					   MAX(command_len, 1))) {
			if (!ser_res_cond->name_list) {
				ser_res_cond->name_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(ser_res_cond->name_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Descriptions",
					MAX(command_len, 1))) {
			if (!ser_res_cond->description_list) {
				ser_res_cond->description_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(
			    ser_res_cond->description_list, argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Ids", MAX(command_len, 1))) {
			ListIterator itr = NULL;
			char *temp = NULL;
			uint32_t id = 0;

			if (!ser_res_cond->id_list) {
				ser_res_cond->id_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(ser_res_cond->id_list,
						 argv[i]+end))
				set = 1;

			/* check to make sure user gave ints here */
			itr = list_iterator_create(ser_res_cond->id_list);
			while ((temp = list_next(itr))) {
				if (get_uint(temp, &id, "SER_RES ID")
				    != SLURM_SUCCESS) {
					exit_code = 1;
					list_delete_item(itr);
				}
			}
			list_iterator_destroy(itr);
		} else if (!strncasecmp (argv[i], "Manager",
					MAX(command_len, 2))) {
			if (!ser_res_cond->manager_list) {
				ser_res_cond->manager_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(ser_res_cond->manager_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Server",
					MAX(command_len, 2))) {
			if (!ser_res_cond->server_list) {
				ser_res_cond->server_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(ser_res_cond->server_list,
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

static int _set_clus_res_rec(int *start, int argc, char *argv[],
		    List name_list,
		    slurmdb_clus_res_rec_t *clus_res)
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

		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !strncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !strncasecmp (argv[i], "Name",
					   MAX(command_len, 1))) {
			if (name_list)
				slurm_addto_char_list(name_list, argv[i]+end);
		}  else if (!strncasecmp (argv[i], "Cluster",
					 MAX(command_len, 1))) {
			if (!clus_res->cluster) {
				clus_res->cluster =
					strip_quotes(argv[i]+end, NULL, 1);
			}
			set = 1;
		} else if (!strncasecmp (argv[i], "allowed",
					 MAX(command_len, 1))) {
			if (!clus_res)
				continue;
			if (get_uint(argv[i]+end, &clus_res->percent_allowed,
				     "allowed") == SLURM_SUCCESS) {
				set = 1;
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

static int _set_ser_res_rec(int *start, int argc, char *argv[],
		    List name_list,
		    slurmdb_ser_res_rec_t *ser_res)
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

		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !strncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		}else if (! end || !strncasecmp (argv[i], "type",
						MAX(command_len, 3))) {
			ListIterator itr = NULL;
			List tmp_list = list_create(slurm_destroy_char);
			char *temp = NULL;

			if (slurm_addto_char_list(tmp_list,
						 argv[i]+end))
				set = 1;
			itr = list_iterator_create(tmp_list);
			while ((temp = list_next(itr))) {
				if (!strncasecmp("License", temp,
						 MAX(strlen(temp), 1))) {
					ser_res->type =
						    SLURMDB_RESOURCE_LICENSE;
				} else {
					exit_code=1;
					fprintf(stderr,
						" Unknown resource type: '%s'  "
						"Valid resources are License "
						"and Test.\n",
						temp);
				}
			}
			list_iterator_destroy(itr);
			list_destroy(tmp_list);
		} else if (!strncasecmp (argv[i], "Name",
					 MAX(command_len, 1))) {
			if (name_list)
				slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Description",
					 MAX(command_len, 1))) {
			if (!ser_res->description)
				ser_res->description =
					strip_quotes(argv[i]+end, NULL, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Manager",
					 MAX(command_len, 1))) {
			if (!ser_res->manager) {
				ser_res->manager=
					strip_quotes(argv[i]+end, NULL, 1);
			}
			set = 1;
		}  else if (!strncasecmp (argv[i], "Server",
					  MAX(command_len, 1))) {
			if (!ser_res->server) {
				ser_res->server=
					strip_quotes(argv[i]+end, NULL, 1);
			}
			set = 1;
		}else if (!strncasecmp (argv[i], "count",
					MAX(command_len, 3))) {
			if (!ser_res)
				continue;
			if (get_uint(argv[i]+end, &ser_res->count,
				     "count") == SLURM_SUCCESS) {
				set = 1;
			}
		} else if (!strncasecmp (argv[i], "Flags",
					 MAX(command_len, 2))) {
			if (!ser_res)
				continue;
			ser_res->flags = str_2_ser_res_flags(argv[i]+end,
							     option);
			if (ser_res->flags == SER_RES_FLAG_NOTSET) {
				char *tmp_char = NULL;
				ser_res->flags = INFINITE;
				ser_res->flags &= (~SER_RES_FLAG_NOTSET &
					       ~SER_RES_FLAG_ADD &
					       ~SER_RES_FLAG_REMOVE);
				tmp_char =
				     slurmdb_ser_res_flags_str(ser_res->flags);
				printf(" Unknown Server Resource flag used "
				       "in:\n  '%s'\n"
				       " Valid Server Resource flags are\n"
				       " '%s'\n", argv[i]+end, tmp_char);
				xfree(tmp_char);
				exit_code = 1;
			} else
				set = 1;
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

static int _check_cluster_name (List name_list)

{
	List cluster_list = NULL;
	ListIterator itr = NULL;
	ListIterator itr1 = NULL;
	char *name = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;
	bool match = false;

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid, NULL);
	if (!cluster_list) {
		exit_code=1;
		fprintf(stderr,
			" Error obtaining cluster records.\n");
		return SLURM_ERROR;

	}
	if(name_list && list_count(name_list)) {
		itr = list_iterator_create(name_list);
		while ((name = list_next(itr))) {
			itr1 = list_iterator_create(cluster_list);
			while ((cluster = list_next(itr1))) {
				if (!strcasecmp(name, cluster->name)) {
					match = true;
					break;
				}
			}
			list_iterator_destroy(itr1);
			if (match == false) {
				list_iterator_destroy(itr);
				list_destroy(cluster_list);
				return SLURM_ERROR;
			}
		}
		list_iterator_destroy(itr);
		list_destroy(cluster_list);
		return SLURM_SUCCESS;
	} else
		return SLURM_ERROR;
}

extern int sacctmgr_add_ser_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	int i=0, limit_set=0;
	ListIterator itr = NULL;
	slurmdb_ser_res_rec_t *ser_res = NULL;
	slurmdb_ser_res_rec_t *start_ser_res =
		xmalloc(sizeof(slurmdb_ser_res_rec_t));
	List name_list = list_create(slurm_destroy_char);
	char *name = NULL;
	List ser_res_list = NULL;
	char *ser_res_str = NULL;
	slurmdb_ser_res_cond_t *object_ptr =
		xmalloc(sizeof(slurmdb_ser_res_cond_t));

	slurmdb_init_ser_res_cond(object_ptr, 0);

	slurmdb_init_ser_res_rec(start_ser_res, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;

		limit_set +=
			_set_ser_res_rec(&i, argc, argv, name_list,
					 start_ser_res);
	}

	if (exit_code) {
		list_destroy(name_list);
		slurmdb_destroy_ser_res_cond(object_ptr);
		slurmdb_destroy_ser_res_rec(start_ser_res);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		list_destroy(name_list);
		slurmdb_destroy_ser_res_cond(object_ptr);
		slurmdb_destroy_ser_res_rec(start_ser_res);
		exit_code=1;
		fprintf(stderr, " Need name of system resource to add.\n");
		return SLURM_SUCCESS;
	} else if (start_ser_res->count == NO_VAL) {
		list_destroy(name_list);
		slurmdb_destroy_ser_res_cond(object_ptr);
		slurmdb_destroy_ser_res_rec(start_ser_res);
		exit_code=1;
		fprintf(stderr, " Need to designate a resource count.\n");
		return SLURM_SUCCESS;
	} else if (start_ser_res->type == NO_VAL) {
		list_destroy(name_list);
		slurmdb_destroy_ser_res_cond(object_ptr);
		slurmdb_destroy_ser_res_rec(start_ser_res);
		exit_code=1;
		fprintf(stderr, " Need to designate a resource type.\n");
		return SLURM_SUCCESS;
	} else {
		switch (start_ser_res->type) {
		case SLURMDB_RESOURCE_LICENSE :
			break;
		default:
			list_destroy(name_list);
			slurmdb_destroy_ser_res_cond(object_ptr);
			slurmdb_destroy_ser_res_rec(start_ser_res);
			exit_code=1;
			fprintf(stderr, "Need to designate a resource type.\n");
			return SLURM_SUCCESS;
		}
	}

	if (!g_ser_res_list) {
		g_ser_res_list =
			acct_storage_g_get_ser_res(db_conn, my_uid, NULL);
		if (!g_ser_res_list) {
			exit_code=1;
			fprintf(stderr, " Problem getting system resources "
				"from database.  "
				"Contact your admin.\n");
			list_destroy(name_list);
			slurmdb_destroy_ser_res_cond(object_ptr);
			slurmdb_destroy_ser_res_rec(start_ser_res);
			return SLURM_ERROR;
		}
	}

	ser_res_list = list_create(slurmdb_destroy_ser_res_rec);

	itr = list_iterator_create(name_list);
	while ((name = list_next(itr))) {
		ser_res = NULL;
		if (!sacctmgr_find_ser_res_from_list(g_ser_res_list, name)) {
			ser_res =
				xmalloc(sizeof(slurmdb_ser_res_rec_t));
			slurmdb_init_ser_res_rec(ser_res, 0);
			ser_res->name = xstrdup(name);
			if (start_ser_res->description)
				ser_res->description =
					xstrdup(start_ser_res->description);
			else
				ser_res->description = xstrdup(name);
			if (start_ser_res->manager)
				ser_res->manager =
					xstrdup(start_ser_res->manager);
			else
				ser_res->manager = "NULL";
			if (start_ser_res->server)
				ser_res->server =
					xstrdup(start_ser_res->server);
			else
				ser_res->server = "NULL";

			ser_res->count = start_ser_res->count;
			ser_res->flags = start_ser_res->flags;
			ser_res->type = start_ser_res->type;

			xstrfmtcat(ser_res_str, "  %s\n", name);
			list_append(ser_res_list, ser_res);
		}
	}
	list_iterator_destroy(itr);
	list_destroy(name_list);

	if (g_ser_res_list) {
		list_destroy(g_ser_res_list);
		g_ser_res_list = NULL;
	}

	if (!list_count(ser_res_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (ser_res_str) {
		char *tmp_str;
		switch (ser_res->type) {
		case SLURMDB_RESOURCE_LICENSE:
			tmp_str = "License";
			break;
		default:
			tmp_str = "Unknown";
		}
		printf(" Adding system resource(s)\n%s", ser_res_str);
		printf(" Settings\n");
		printf("  Description    = %s\n", ser_res->description);
		printf("  Manager        = %s\n", ser_res->manager);
		printf("  Server         = %s\n", ser_res->server);
		printf("  Count          = %u\n", ser_res->count);
		printf("  Type           = %s\n", tmp_str);

		xfree(ser_res_str);
	}

	if (list_count(ser_res_list)) {
		notice_thread_init();
		rc = acct_storage_g_add_ser_res(db_conn, my_uid,
						 ser_res_list);
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
	slurmdb_destroy_ser_res_cond(object_ptr);
	slurmdb_destroy_ser_res_rec(start_ser_res);
	return rc;
}

extern int sacctmgr_list_ser_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_ser_res_cond_t *ser_res_cond =
		xmalloc(sizeof(slurmdb_ser_res_cond_t));
 	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_ser_res_rec_t *ser_res = NULL;
	List ser_res_list = NULL;
	int field_count = 0;
	print_field_t *field = NULL;
	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_ser_res_cond(&i, argc, argv, ser_res_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_ser_res_cond(ser_res_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	} else if (!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				     "Name,Count,Manager,Server,Type,Flags%10");
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	list_destroy(format_list);

	if (exit_code) {
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}
	ser_res_list =
		acct_storage_g_get_ser_res(db_conn, my_uid, ser_res_cond);
	slurmdb_destroy_ser_res_cond(ser_res_cond);

	if (!ser_res_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}
	itr = list_iterator_create(ser_res_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	while ((ser_res = list_next(itr))) {
		int curr_inx = 1;
		char *tmp_char;
		while ((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_DESC:
				field->print_routine(
					field, ser_res->description,
					(curr_inx == field_count));
				break;
			case PRINT_NAME:
				field->print_routine(
					field, ser_res->name,
					(curr_inx == field_count));
				break;
			case PRINT_ID:
				field->print_routine(
					field, ser_res->id,
					(curr_inx == field_count));
				break;
			case PRINT_COUNT:
				field->print_routine(field,
						     ser_res->count,
						     (curr_inx == field_count));
				break;
			case PRINT_FLAGS:
			{
				char *tmp_char = slurmdb_ser_res_flags_str(
					ser_res->flags);
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			}
				break;
			case PRINT_TYPE:
				if (ser_res->type == SLURMDB_RESOURCE_LICENSE)
					tmp_char = "License";
				else
					tmp_char = "Unknown";
				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				break;
			case PRINT_MANAGER:
				field->print_routine(field,
						     ser_res->manager,
						     (curr_inx == field_count));
				break;
			case PRINT_SERVER:
				field->print_routine(field,
						     ser_res->server,
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
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(ser_res_list);
	list_destroy(print_fields_list);
	return rc;
}

extern int sacctmgr_modify_ser_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_ser_res_cond_t *ser_res_cond =
		xmalloc(sizeof(slurmdb_ser_res_cond_t));
	slurmdb_ser_res_rec_t *ser_res =
		xmalloc(sizeof(slurmdb_ser_res_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;
	slurmdb_init_ser_res_rec(ser_res, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i++;
			cond_set += _set_ser_res_cond(&i, argc, argv,
					      ser_res_cond, NULL);

		} else if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i++;
			rec_set += _set_ser_res_rec(&i, argc, argv, NULL,
						    ser_res);
		} else {
			cond_set += _set_ser_res_cond(&i, argc, argv,
						      ser_res_cond, NULL);
		}
	}

	if (exit_code) {
		slurmdb_destroy_ser_res_cond(ser_res_cond);
		slurmdb_destroy_ser_res_rec(ser_res);
		return SLURM_ERROR;
	} else if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		slurmdb_destroy_ser_res_cond(ser_res_cond);
		slurmdb_destroy_ser_res_rec(ser_res);
		return SLURM_ERROR;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			slurmdb_destroy_ser_res_cond(ser_res_cond);
			slurmdb_destroy_ser_res_rec(ser_res);
			return SLURM_SUCCESS;
		}
	}

	notice_thread_init();
	ret_list = acct_storage_g_modify_ser_res(db_conn, my_uid,
						  ser_res_cond, ser_res);
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
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	if (set) {
		if (commit_check("Would you like to commit changes?")){
			acct_storage_g_commit(db_conn, 1);
		}else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}
	if (ret_list && list_count(ret_list))
		list_destroy(ret_list);
	slurmdb_destroy_ser_res_cond(ser_res_cond);
	slurmdb_destroy_ser_res_rec(ser_res);
	return rc;
}

extern int sacctmgr_delete_ser_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_ser_res_cond_t *ser_res_cond =
		xmalloc(sizeof(slurmdb_ser_res_cond_t));
	int i=0;
	List ret_list = NULL;
	List cluster_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *name = NULL;
	slurmdb_cluster_rec_t *cluster;

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid, NULL);
	if (!cluster_list) {
		exit_code=1;
		fprintf(stderr,
			" Error obtaining cluster records.\n");
		return SLURM_ERROR;

	}
	if (cluster_list && list_count(cluster_list)) {
		itr = list_iterator_create(cluster_list);
		while ((cluster = list_next(itr))) {
	printf(" Checking cluster %s for resource ...\n",
		 cluster->name);
			if ((rc = sacctmgr_delete_clus_res(argc, argv,
			     cluster->name)) != SLURM_SUCCESS) {
				printf(" No cluster resources were deleted \n");
			}
		}
		list_destroy(cluster_list);
	}
	slurmdb_init_ser_res_cond(ser_res_cond, 0);


	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		set += _set_ser_res_cond(&i, argc, argv, ser_res_cond, NULL);
	}

	if (!set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_ser_res_cond(ser_res_cond);
		return SLURM_ERROR;
	} else if (set == -1) {
		slurmdb_destroy_ser_res_cond(ser_res_cond);
		return SLURM_ERROR;
	}

	if (!g_ser_res_list)
		g_ser_res_list = acct_storage_g_get_ser_res(
			db_conn, my_uid, NULL);

	notice_thread_init();
	ret_list =
		acct_storage_g_remove_ser_res(db_conn, my_uid, ser_res_cond);
	notice_thread_fini();
	slurmdb_destroy_ser_res_cond(ser_res_cond);

	if (ret_list && list_count(ret_list)) {
		ListIterator itr = NULL;

		itr = list_iterator_create(ret_list);
		printf(" Deleting system resource(s)...\n");

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

	if (ret_list && list_count(ret_list))
		list_destroy(ret_list);
	xfree(name);
	return rc;
}

extern int sacctmgr_add_clus_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	int i=0, limit_set=0;
	ListIterator itr = NULL;
	ListIterator itr1 = NULL;
	slurmdb_clus_res_rec_t *clus_res = NULL;
	slurmdb_clus_res_rec_t *start_clus_res =
		xmalloc(sizeof(slurmdb_clus_res_rec_t));
	slurmdb_ser_res_rec_t *ser_res;
	List name_list = list_create(slurm_destroy_char);
	char *name = NULL;
	List clus_res_list = NULL;
	List cluster_name_list = NULL;
	char *cluster_name = NULL;
	char *clus_res_str = NULL;
	uint16_t cluster_count = 0;
	uint16_t allocated = 0;
	uint16_t allowed = 0;

	slurmdb_init_clus_res_rec(start_clus_res, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;

		limit_set += _set_clus_res_rec(&i, argc, argv, name_list,
					       start_clus_res);
	}

	if (exit_code) {
		slurmdb_destroy_clus_res_rec(start_clus_res);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		list_destroy(name_list);
		slurmdb_destroy_clus_res_rec(start_clus_res);
		exit_code=1;
		fprintf(stderr, " Need name of clus_res to add.\n");
		return SLURM_SUCCESS;
	}
	cluster_name_list = list_create(slurm_destroy_char);
	if (start_clus_res->cluster) {
		name = xstrdup(start_clus_res->cluster);
		list_append(cluster_name_list,name);
		rc = _check_cluster_name(cluster_name_list);
		if (rc != SLURM_SUCCESS) {
			fprintf(stderr, " Need a valid cluster name to "
					"add a cluster resource.\n");
			list_destroy(cluster_name_list);
			return SLURM_SUCCESS;
		}
		allocated = start_clus_res->percent_allowed;
		if (start_clus_res->percent_allowed == NO_VAL) {
			fprintf(stderr, " Need a valid allowed value.\n");
			return SLURM_ERROR;
		}
	} else {
		rc = _populate_cluster_name_list(cluster_name_list);
		if (rc != SLURM_SUCCESS)  {
			fprintf(stderr,
				" Error obtaining cluster names.\n");
			return SLURM_ERROR;
		}
		cluster_count = list_count(cluster_name_list);
		start_clus_res->percent_allowed = 100 / cluster_count;
		allocated = 100;
	}
	allowed = start_clus_res->percent_allowed;
	if (!g_ser_res_list) {
		g_ser_res_list =
			acct_storage_g_get_ser_res(db_conn, my_uid, NULL);

		if (!g_ser_res_list) {
			slurmdb_destroy_clus_res_rec(start_clus_res);
			exit_code=1;
			fprintf(stderr, " Problem getting system resources "
				"from database.  "
				"Contact your admin.\n");
				list_destroy(cluster_name_list);
			return SLURM_ERROR;
		}
	}
		rc = _get_g_clus_res_list ();
		if ((rc != SLURM_SUCCESS) || !g_clus_res_list) {
			fprintf(stderr, " Problem getting cluster resources "
				"from database.  "
				"Contact your admin.\n");
	 		slurmdb_destroy_clus_res_rec(start_clus_res);
			list_destroy(cluster_name_list);
			return SLURM_ERROR;
		}
	clus_res_list = list_create(slurmdb_destroy_clus_res_rec);
	itr = list_iterator_create(cluster_name_list);
	while ((cluster_name = list_next(itr))) {
		itr1 = list_iterator_create(name_list);
		while ((name = list_next(itr1))) {
			clus_res = NULL;
			if ((ser_res = sacctmgr_find_ser_res_from_list(
			    g_ser_res_list, name))) {
				clus_res =
					xmalloc(sizeof(slurmdb_clus_res_rec_t));
				slurmdb_init_clus_res_rec(clus_res, 0);
				clus_res->res_ptr =
					xmalloc(sizeof(slurmdb_ser_res_rec_t));
				slurmdb_init_ser_res_rec(clus_res->res_ptr, 0);
				clus_res->res_ptr->description =
					xstrdup(ser_res->description);
				clus_res->res_ptr->id = ser_res->id;
				clus_res->res_ptr->name =
					xstrdup(ser_res->name);
				clus_res->res_ptr->count = ser_res->count;
				clus_res->res_ptr->flags =ser_res->flags;
				clus_res->res_ptr->type = ser_res->type;
				clus_res->res_ptr->manager =
					xstrdup(ser_res->manager);
				clus_res->res_ptr->server =
					xstrdup(ser_res->server);
				clus_res->cluster =
					xstrdup(cluster_name);
				clus_res->percent_allowed =
					start_clus_res->percent_allowed;
				list_append(clus_res_list, clus_res);
				xstrfmtcat(clus_res_str, "  %s\n", name);
			}
		}
		list_iterator_destroy(itr1);
	}
	list_iterator_destroy(itr);
	if (clus_res == NULL) {
		printf(" System Resource doesn't exist.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}
	rc = _check_oversubscription(clus_res_list,
			 g_clus_res_list, allocated, true);
	if (rc == oversubscribed) {
		printf(" Oversubscribed clus_res, creation denied. \n");
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (rc == duplicate) {
		printf(" Cluster resource already exists, use modify to "
		       "change it. \n");
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (!list_count(clus_res_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (clus_res_str) {
		printf(" Adding cluster resource(s)\n%s", clus_res_str);
		printf(" Settings\n");
		printf("  Allowed    = %u\n", allowed);
		xfree(clus_res_str);
	}

	if (list_count(clus_res_list)) {
		notice_thread_init();
		rc = acct_storage_g_add_clus_res(db_conn, my_uid,
						 clus_res_list);
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
		fprintf(stderr, " Problem adding clus_res: %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
	}
	notice_thread_fini();

end_it:
	if (g_clus_res_list) {
		list_destroy(g_clus_res_list);
		g_clus_res_list = NULL;
	}

	if (g_ser_res_list) {
		list_destroy(g_ser_res_list);
		g_ser_res_list = NULL;
	}

	if (name_list)
		list_destroy(name_list);
	list_destroy(cluster_name_list);
	list_destroy(clus_res_list);
	slurmdb_destroy_clus_res_rec(start_clus_res);

	return rc;
}

extern int sacctmgr_list_clus_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_clus_res_cond_t *clus_res_cond =
		xmalloc(sizeof(slurmdb_clus_res_cond_t));
	List clus_res_list = NULL;
	int i = 0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_clus_res_rec_t *clus_res = NULL;
	char *tmp_char = NULL;
	int field_count = 0;

	print_field_t *field = NULL;
	List format_list = list_create(slurm_destroy_char);
	List print_fields_list  = NULL;

	slurmdb_init_clus_res_cond(clus_res_cond, 0);
	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_clus_res_cond(&i, argc, argv, clus_res_cond, format_list);
	}

	if (exit_code) {
		rc = SLURM_ERROR;
		goto end_it;
	}
	if (!clus_res_cond->cluster_list) {
		clus_res_cond->cluster_list = list_create(slurm_destroy_char);
		rc = _populate_cluster_name_list(clus_res_cond->cluster_list);
		if (rc != SLURM_SUCCESS)  {
			fprintf(stderr,
				" Error obtaining cluster names.\n");
			rc = SLURM_ERROR;
			goto end_it;
		}
	}else {
		rc = _check_cluster_name(clus_res_cond->cluster_list);
		if (rc != SLURM_SUCCESS) {
			fprintf(stderr, " Need a valid cluster name to "
					"list  cluster resources.\n");
			rc = SLURM_SUCCESS;
			goto end_it;
		}
	}
	if (!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				      "Cluster,Name,Count,Manager,Server,"
				      "Percent,Type,Flags%10");
	}

	clus_res_cond->with_deleted = with_deleted;
	print_fields_list = sacctmgr_process_format_list(format_list);

	if (exit_code) {
		rc =  SLURM_ERROR;
		goto end_it;
	}

	clus_res_list = acct_storage_g_get_clus_res(db_conn, my_uid,
						    clus_res_cond);
	if (!clus_res_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	itr = list_iterator_create(clus_res_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	while ((clus_res = list_next(itr))) {
		int curr_inx = 1;
		slurmdb_ser_res_rec_t *ser_res = clus_res->res_ptr;
		while ((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_CLUSTER:
				field->print_routine(field,
						     clus_res->cluster,
						     (curr_inx == field_count));
				break;
			case PRINT_NAME:
				field->print_routine(field,
						     ser_res->name,
						     (curr_inx == field_count));
				break;
			case PRINT_COUNT:
				field->print_routine(field,
						     ser_res->count,
						     (curr_inx == field_count));
				break;
			case PRINT_FLAGS:
				tmp_char = slurmdb_ser_res_flags_str(
					ser_res->flags);
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			case PRINT_TYPE:
				if (ser_res->type == SLURMDB_RESOURCE_LICENSE)
					tmp_char = "License";
				else
					tmp_char = "Unknown";
				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				break;
			case PRINT_MANAGER:
				field->print_routine(field,
						     ser_res->manager,
						     (curr_inx == field_count));
				break;
			case PRINT_SERVER:
				field->print_routine(field,
						     ser_res->server,
						     (curr_inx == field_count));
				break;
			case PRINT_PERCENT:
				field->print_routine(field,
						     clus_res->percent_allowed,
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
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
end_it:
	if (clus_res_cond)
		slurmdb_destroy_clus_res_cond(clus_res_cond);
	if (format_list)
		list_destroy(format_list);
	if (clus_res_list)
		list_destroy(clus_res_list);
	if (print_fields_list)
		list_destroy(print_fields_list);
	return rc;
}

extern int sacctmgr_modify_clus_res(int argc, char *argv[])

{
	int rc = SLURM_SUCCESS;
	slurmdb_clus_res_cond_t *clus_res_cond =
		xmalloc(sizeof(slurmdb_clus_res_cond_t));
	slurmdb_clus_res_rec_t *clus_res =
		xmalloc(sizeof(slurmdb_clus_res_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;
	List clus_res_list = NULL;
	uint16_t allocated = 0;

	slurmdb_init_clus_res_rec(clus_res, 0);
	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i++;
			cond_set += _set_clus_res_cond(&i, argc, argv,
					      clus_res_cond, NULL);

		} else if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i++;
			rec_set += _set_clus_res_rec(&i, argc, argv, NULL,
						     clus_res);
		} else {
			cond_set += _set_clus_res_cond(&i, argc, argv,
						       clus_res_cond, NULL);
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
	} else if (!clus_res->cluster &&  (!clus_res_cond->cluster_list)) {
		exit_code=1;
		fprintf(stderr, " Need a cluster name to "
				"modify a cluster resource.\n");
		rc = SLURM_SUCCESS;
		goto end_it;
	}
	rc = _check_cluster_name(clus_res_cond->cluster_list);
	if (rc != SLURM_SUCCESS) {
		fprintf(stderr, " Need a valid cluster name to "
				"modify a cluster resource.\n");
		rc = SLURM_SUCCESS;
		goto end_it;
	}
	rc = _get_g_clus_res_list ();
	if ( (rc != SLURM_SUCCESS) || !g_clus_res_list) {
		fprintf(stderr, " Problem getting cluster resources "
			"from database.  "
			"Contact your admin.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}
	clus_res_list = acct_storage_g_get_clus_res(db_conn, my_uid,
						    clus_res_cond);
	if (!clus_res_list) {
		exit_code=1;
		fprintf(stderr, " Cluster resource doesn't exist.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}
	allocated = clus_res->percent_allowed;
	rc = _check_oversubscription(clus_res_list,
			 g_clus_res_list, allocated, false);
	if (rc == oversubscribed) {
		printf(" Oversubscribed clus_res, modification denied. \n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	notice_thread_init();
	ret_list = acct_storage_g_modify_clus_res(db_conn, my_uid,
						  clus_res_cond, clus_res);
	notice_thread_fini();
	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified cluster resource ...\n");
		while ((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if (ret_list) {
		printf(" Nothing modified\n");
		rc = SLURM_ERROR;
	} else {
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	if (set) {
		if (commit_check("Would you like to commit changes?")){
			acct_storage_g_commit(db_conn, 1);
		}else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}
end_it:
	if (clus_res_list)
		list_destroy(clus_res_list);
	if (clus_res)
		slurmdb_destroy_clus_res_rec(clus_res);
	if (g_clus_res_list) {
		list_destroy(g_clus_res_list);
		g_clus_res_list = NULL;
	}
	if (g_ser_res_list) {
		list_destroy(g_ser_res_list);
		g_ser_res_list = NULL;
	}
	if (ret_list && list_count(ret_list))
		list_destroy(ret_list);

	slurmdb_destroy_clus_res_cond(clus_res_cond);
	return rc;
}

extern int sacctmgr_delete_clus_res(int argc, char *argv[], char* cluster_name)

{
	int rc = SLURM_SUCCESS;
	slurmdb_clus_res_cond_t *clus_res_cond =
		xmalloc(sizeof(slurmdb_clus_res_cond_t));
	int i=0;
	List ret_list = NULL;
	int set = 0;
	char *name = NULL;
	slurmdb_init_clus_res_cond(clus_res_cond, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		set += _set_clus_res_cond(&i, argc, argv, clus_res_cond, NULL);
	}

	if (!set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_clus_res_cond(clus_res_cond);
		return SLURM_ERROR;
	} else if (set == -1) {
		slurmdb_destroy_clus_res_cond(clus_res_cond);
		return SLURM_ERROR;
	}

	if (cluster_name){
		if (clus_res_cond->cluster_list) {
			list_destroy(clus_res_cond->cluster_list);
		}
		clus_res_cond->cluster_list = list_create(slurm_destroy_char);
		name = xstrdup(cluster_name);
		list_append(clus_res_cond->cluster_list, name);
	}else if (!clus_res_cond->cluster_list) {
		rc = _populate_cluster_name_list(clus_res_cond->cluster_list);
		if (rc != SLURM_SUCCESS)  {
			fprintf(stderr,
				" Error obtaining cluster names.\n");
			rc = SLURM_ERROR;
			goto end_it;
		}
	}
	rc = _check_cluster_name(clus_res_cond->cluster_list);
	if (rc != SLURM_SUCCESS) {
		fprintf(stderr, " Need a valid cluster name to "
				"delete cluster resource.\n");
		slurmdb_destroy_clus_res_cond(clus_res_cond);
		rc = SLURM_SUCCESS;
	}

	if (!g_ser_res_list)
		g_ser_res_list = acct_storage_g_get_ser_res(
			db_conn, my_uid, NULL);

	rc = _get_g_clus_res_list ();
	if ( (rc != SLURM_SUCCESS) || !g_clus_res_list) {
		fprintf(stderr, " Problem getting cluster resources "
			"from database.  "
			"Contact your admin.\n");
		slurmdb_destroy_clus_res_cond(clus_res_cond);
		rc = SLURM_ERROR;
	}
	notice_thread_init();
	ret_list =
		acct_storage_g_remove_clus_res(db_conn, my_uid, clus_res_cond);
	notice_thread_fini();
	slurmdb_destroy_clus_res_cond(clus_res_cond);

	if (ret_list && list_count(ret_list)) {
		ListIterator itr = NULL;

		itr = list_iterator_create(ret_list);
		printf(" Deleting cluster resource(s)...\n");

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

end_it:
	if (g_clus_res_list) {
		list_destroy(g_clus_res_list);
		g_clus_res_list = NULL;
	}
	if (g_ser_res_list) {
		list_destroy(g_ser_res_list);
		g_ser_res_list = NULL;
	}
	if (ret_list && list_count(ret_list))
		list_destroy(ret_list);
	return rc;
}
