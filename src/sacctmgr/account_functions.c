/*****************************************************************************\
 *  account_functions.c - functions dealing with accounts in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
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
#include "src/common/assoc_mgr.h"

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_account_cond_t *acct_cond,
		     List format_list)
{
	int i;
	int a_set = 0;
	int u_set = 0;
	int end = 0;
	slurmdb_assoc_cond_t *assoc_cond = NULL;
	int command_len = 0;
	int option = 0;

	if (!acct_cond) {
		exit_code=1;
		fprintf(stderr, "No acct_cond given");
		return -1;
	}

	if (!acct_cond->assoc_cond) {
		acct_cond->assoc_cond =
			xmalloc(sizeof(slurmdb_assoc_cond_t));
	}

	assoc_cond = acct_cond->assoc_cond;

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

		if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithAssoc",
					 MAX(command_len, 5))) {
			acct_cond->with_assocs = 1;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithCoordinators",
					 MAX(command_len, 5))) {
			acct_cond->with_coords = 1;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithDeleted",
					 MAX(command_len, 5))) {
			acct_cond->with_deleted = 1;
			assoc_cond->with_deleted = 1;
		} else if (!end &&
			   !xstrncasecmp(argv[i], "WithRawQOSLevel",
					 MAX(command_len, 5))) {
			assoc_cond->with_raw_qos = 1;
		} else if (!end && !xstrncasecmp(argv[i], "WOPLimits",
						 MAX(command_len, 4))) {
			assoc_cond->without_parent_limits = 1;
		} else if (!end && !xstrncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Names",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Accounts",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Acct",
					   MAX(command_len, 4))) {
			if (!assoc_cond->acct_list) {
				assoc_cond->acct_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(
				   assoc_cond->acct_list,
				   argv[i]+end))
				u_set = 1;
		} else if (!xstrncasecmp(argv[i], "Descriptions",
					 MAX(command_len, 1))) {
			if (!acct_cond->description_list) {
				acct_cond->description_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(acct_cond->description_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Organizations",
					 MAX(command_len, 1))) {
			if (!acct_cond->organization_list) {
				acct_cond->organization_list =
					list_create(slurm_destroy_char);
			}
			if (slurm_addto_char_list(acct_cond->organization_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!(a_set = sacctmgr_set_assoc_cond(
				    assoc_cond, argv[i], argv[i]+end,
				    command_len, option))) {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
		}
	}

	(*start) = i;

	if (u_set && a_set)
		return 3;
	else if (a_set)
		return 2;
	else if (u_set)
		return 1;

	return 0;
}

static int _set_rec(int *start, int argc, char **argv,
		    List acct_list,
		    List cluster_list,
		    slurmdb_account_rec_t *acct,
		    slurmdb_assoc_rec_t *assoc)
{
	int i;
	int u_set = 0;
	int a_set = 0;
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

		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "set",
						 MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Accounts",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Names",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "Acct",
					   MAX(command_len, 4))) {
			if (acct_list)
				slurm_addto_char_list(acct_list, argv[i]+end);
			else {
				exit_code=1;
				fprintf(stderr,
					" Can't modify the name "
					"of an account\n");
			}
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 1))) {
			if (cluster_list)
				slurm_addto_char_list(cluster_list,
						      argv[i]+end);
			else {
				exit_code=1;
				fprintf(stderr,
					" Can't modify the cluster "
					"of an account\n");
			}
		} else if (!xstrncasecmp(argv[i], "Description",
					 MAX(command_len, 1))) {
			acct->description =  strip_quotes(argv[i]+end, NULL, 1);
			u_set = 1;
		} else if (!xstrncasecmp(argv[i], "Organization",
					 MAX(command_len, 1))) {
			acct->organization = strip_quotes(argv[i]+end, NULL, 1);
			u_set = 1;
		} else if (!xstrncasecmp(argv[i], "RawUsage",
					 MAX(command_len, 7))) {
			uint32_t usage;
			if (!assoc)
				continue;
			assoc->usage = xmalloc(sizeof(slurmdb_assoc_usage_t));
			if (get_uint(argv[i]+end, &usage,
				     "RawUsage") == SLURM_SUCCESS) {
				assoc->usage->usage_raw = usage;
				a_set = 1;
			}
		} else if (!assoc ||
			  (assoc && !(a_set = sacctmgr_set_assoc_rec(
					      assoc, argv[i], argv[i]+end,
					      command_len, option)))) {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}

	(*start) = i;

	if (u_set && a_set)
		return 3;
	else if (a_set)
		return 2;
	else if (u_set)
		return 1;

	return 0;
}

static int _isdefault_old(List acct_list)
{
	int rc = 0;
	slurmdb_user_cond_t user_cond;
	List ret_list = NULL;

	if (!acct_list || !list_count(acct_list))
		return rc;

	memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
	user_cond.def_acct_list = acct_list;

	ret_list = slurmdb_users_get(db_conn, &user_cond);
	if (ret_list && list_count(ret_list)) {
		ListIterator itr = list_iterator_create(ret_list);
		slurmdb_user_rec_t *user = NULL;
		fprintf(stderr," Users listed below have these "
			"as their Default Accounts.\n");
		while((user = list_next(itr))) {
			fprintf(stderr, " User - %-10.10s Account - %s\n",
				user->name, user->default_acct);
		}
		list_iterator_destroy(itr);
		rc = 1;
	}

	FREE_NULL_LIST(ret_list);

	return rc;
}

static int _isdefault(int cond_set, List acct_list, List assoc_list)
{
	int rc = 0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *acct;
	char *output = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;

	if (!acct_list || !list_count(acct_list)
	    || !assoc_list || !list_count(assoc_list))
		return rc;

	/* Since not all plugins have been converted to the new style
	   of default accounts we have to handle those that aren't.
	   If the plugin have been converted all the associations here
	   will have is_def set.
	*/
	assoc = list_peek(assoc_list);
	if (!assoc->is_def)
		return _isdefault_old(acct_list);

	itr = list_iterator_create(acct_list);
	itr2 = list_iterator_create(assoc_list);
	while ((acct = list_next(itr))) {
		while ((assoc = list_next(itr2))) {
			char tmp[1000];
			/* The pgsql plugin doesn't have the idea of
			   only_defs, so thre query could return all
			   the associations, even without defaults. */
			if (cond_set == 1) {
				if (xstrcasecmp(acct, assoc->acct))
					continue;
			} else {
				snprintf(tmp, 1000, " A = %s ", assoc->acct);
				if (!strstr(acct, tmp))
					continue;
			}
			snprintf(tmp, 1000, "C = %-10s A = %-20s U = %-9s\n",
				 assoc->cluster, assoc->acct, assoc->user);
			if (output && strstr(output, tmp))
				continue;

			xstrcat(output, tmp);
			rc = 1;
		}
		list_iterator_reset(itr2);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	if (output) {
		fprintf(stderr," Users listed below have these "
			"as their Default Accounts.\n%s", output);
		xfree(output);
	}
	return rc;
}

extern int sacctmgr_add_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i=0;
	ListIterator itr = NULL, itr_c = NULL;
	slurmdb_account_rec_t *acct = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
	slurmdb_assoc_cond_t assoc_cond;
	List name_list = list_create(slurm_destroy_char);
	List cluster_list = list_create(slurm_destroy_char);
	char *cluster = NULL;
	char *name = NULL;
	List acct_list = NULL;
	List assoc_list = NULL;
	List local_assoc_list = NULL;
	List local_account_list = NULL;
	char *acct_str = NULL;
	char *assoc_str = NULL;
	int limit_set = 0;
	slurmdb_account_rec_t *start_acct =
		xmalloc(sizeof(slurmdb_account_rec_t));
	slurmdb_assoc_rec_t *start_assoc =
		xmalloc(sizeof(slurmdb_assoc_rec_t));

	slurmdb_init_assoc_rec(start_assoc, 0);

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		limit_set += _set_rec(&i, argc, argv, name_list, cluster_list,
				      start_acct, start_assoc);
	}
	if (exit_code) {
		slurmdb_destroy_assoc_rec(start_assoc);
		slurmdb_destroy_account_rec(start_acct);
		return SLURM_ERROR;
	}

	if (!name_list || !list_count(name_list)) {
		FREE_NULL_LIST(name_list);
		FREE_NULL_LIST(cluster_list);
		slurmdb_destroy_assoc_rec(start_assoc);
		slurmdb_destroy_account_rec(start_acct);
		exit_code = 1;
		fprintf(stderr, " Need name of account to add.\n");
		return SLURM_SUCCESS;
	} else {
		slurmdb_account_cond_t account_cond;
		memset(&account_cond, 0, sizeof(slurmdb_account_cond_t));
		memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));

		assoc_cond.acct_list = name_list;
		account_cond.assoc_cond = &assoc_cond;

		local_account_list = slurmdb_accounts_get(
			db_conn, &account_cond);
	}

	if (!local_account_list) {
		exit_code = 1;
		fprintf(stderr, " Problem getting accounts from database.  "
			"Contact your admin.\n");
		FREE_NULL_LIST(name_list);
		FREE_NULL_LIST(cluster_list);
		slurmdb_destroy_assoc_rec(start_assoc);
		slurmdb_destroy_account_rec(start_acct);
		return SLURM_ERROR;
	}

	if (!start_assoc->parent_acct)
		start_assoc->parent_acct = xstrdup("root");

	if (!cluster_list || !list_count(cluster_list)) {
		slurmdb_cluster_rec_t *cluster_rec = NULL;
		List tmp_list =
			slurmdb_clusters_get(db_conn, NULL);
		if (!tmp_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			FREE_NULL_LIST(name_list);
			FREE_NULL_LIST(cluster_list);
			slurmdb_destroy_assoc_rec(start_assoc);
			slurmdb_destroy_account_rec(start_acct);
			FREE_NULL_LIST(local_account_list);
			return SLURM_ERROR;
		}

		if (!list_count(tmp_list)) {
			exit_code=1;
			fprintf(stderr,
				"  Can't add accounts, no cluster "
				"defined yet.\n"
				" Please contact your administrator.\n");
			FREE_NULL_LIST(name_list);
			FREE_NULL_LIST(cluster_list);
			slurmdb_destroy_assoc_rec(start_assoc);
			slurmdb_destroy_account_rec(start_acct);
			FREE_NULL_LIST(local_account_list);
			return SLURM_ERROR;
		}
		if (!cluster_list)
			list_create(slurm_destroy_char);
		else
			list_flush(cluster_list);

		itr_c = list_iterator_create(tmp_list);
		while((cluster_rec = list_next(itr_c))) {
			list_append(cluster_list, xstrdup(cluster_rec->name));
		}
		list_iterator_destroy(itr_c);
		FREE_NULL_LIST(tmp_list);
	} else if (sacctmgr_validate_cluster_list(cluster_list)
		   != SLURM_SUCCESS) {
		slurmdb_destroy_assoc_rec(start_assoc);
		slurmdb_destroy_account_rec(start_acct);
		FREE_NULL_LIST(local_account_list);

		return SLURM_ERROR;
	}


	acct_list = list_create(slurmdb_destroy_account_rec);
	assoc_list = list_create(slurmdb_destroy_assoc_rec);

	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));

	assoc_cond.acct_list = list_create(NULL);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr)))
		list_append(assoc_cond.acct_list, name);
	list_iterator_destroy(itr);
	list_append(assoc_cond.acct_list, start_assoc->parent_acct);

	assoc_cond.cluster_list = cluster_list;
	local_assoc_list = slurmdb_associations_get(
		db_conn, &assoc_cond);
	FREE_NULL_LIST(assoc_cond.acct_list);
	if (!local_assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting associations from database.  "
			"Contact your admin.\n");
		FREE_NULL_LIST(name_list);
		FREE_NULL_LIST(cluster_list);
		slurmdb_destroy_assoc_rec(start_assoc);
		slurmdb_destroy_account_rec(start_acct);
		FREE_NULL_LIST(local_account_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		if (!name[0]) {
			exit_code=1;
			fprintf(stderr, " No blank names are "
				"allowed when adding.\n");
			rc = SLURM_ERROR;
			continue;
		}

		acct = NULL;
		if (!sacctmgr_find_account_from_list(local_account_list, name)) {
			acct = xmalloc(sizeof(slurmdb_account_rec_t));
			acct->assoc_list =
				list_create(slurmdb_destroy_assoc_rec);
			acct->name = xstrdup(name);
			if (start_acct->description)
				acct->description =
					xstrdup(start_acct->description);
			else
				acct->description = xstrdup(name);

			if (start_acct->organization)
				acct->organization =
					xstrdup(start_acct->organization);
			else if (xstrcmp(start_assoc->parent_acct, "root"))
				acct->organization =
					xstrdup(start_assoc->parent_acct);
			else
				acct->organization = xstrdup(name);

			xstrfmtcat(acct_str, "  %s\n", name);
			list_append(acct_list, acct);
		}

		itr_c = list_iterator_create(cluster_list);
		while((cluster = list_next(itr_c))) {
			if (sacctmgr_find_account_base_assoc_from_list(
				   local_assoc_list, name, cluster)) {
				//printf(" already have this assoc\n");
				continue;
			}
			if (!sacctmgr_find_account_base_assoc_from_list(
				   local_assoc_list, start_assoc->parent_acct,
				   cluster)) {
				exit_code=1;
				fprintf(stderr, " Parent account '%s' "
					"doesn't exist on "
					"cluster %s\n"
					"        Contact your admin "
					"to add this account.\n",
					start_assoc->parent_acct, cluster);
				continue;
			}

			assoc = xmalloc(sizeof(slurmdb_assoc_rec_t));
			slurmdb_init_assoc_rec(assoc, 0);
			assoc->acct = xstrdup(name);
			assoc->cluster = xstrdup(cluster);
			assoc->def_qos_id = start_assoc->def_qos_id;

			assoc->parent_acct = xstrdup(start_assoc->parent_acct);
			assoc->shares_raw = start_assoc->shares_raw;

			slurmdb_copy_assoc_rec_limits(assoc, start_assoc);

			if (acct)
				list_append(acct->assoc_list, assoc);
			else
				list_append(assoc_list, assoc);
			xstrfmtcat(assoc_str,
				   "  A = %-10.10s"
				   " C = %-10.10s\n",
				   assoc->acct,
				   assoc->cluster);

		}
		list_iterator_destroy(itr_c);
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(local_account_list);
	FREE_NULL_LIST(local_assoc_list);


	if (!list_count(acct_list) && !list_count(assoc_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	} else if (!assoc_str) {
		exit_code=1;
		fprintf(stderr, " No associations created.\n");
		goto end_it;
	}

	if (acct_str) {
		printf(" Adding Account(s)\n%s", acct_str);
		printf(" Settings\n");
		if (start_acct->description)
			printf("  Description     = %s\n",
			       start_acct->description);
		else
			printf("  Description     = %s\n", "Account Name");

		if (start_acct->organization)
			printf("  Organization    = %s\n",
			       start_acct->organization);
		else
			printf("  Organization    = %s\n",
			       "Parent/Account Name");

		xfree(acct_str);
	}

	if (assoc_str) {
		printf(" Associations\n%s", assoc_str);
		xfree(assoc_str);
	}

	if (limit_set) {
		printf(" Settings\n");
		sacctmgr_print_assoc_limits(start_assoc);
	}

	notice_thread_init();
	if (list_count(acct_list))
		rc = slurmdb_accounts_add(db_conn, acct_list);


	if (rc == SLURM_SUCCESS) {
		if (list_count(assoc_list))
			rc = slurmdb_associations_add(db_conn, assoc_list);
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding accounts: %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
		notice_thread_fini();
		goto end_it;
	}
	notice_thread_fini();

	if (rc == SLURM_SUCCESS) {
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else {
		exit_code=1;
		fprintf(stderr,
			" error: Problem adding account associations: %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
	}

end_it:
	FREE_NULL_LIST(name_list);
	FREE_NULL_LIST(cluster_list);
	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(assoc_list);

	slurmdb_destroy_assoc_rec(start_assoc);
	slurmdb_destroy_account_rec(start_acct);
	return rc;
}

extern int sacctmgr_list_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_account_cond_t *acct_cond =
		xmalloc(sizeof(slurmdb_account_cond_t));
 	List acct_list;
	int i=0, cond_set=0, prev_set=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_account_rec_t *acct = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	acct_cond->with_assocs = with_assoc_flag;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, acct_cond, format_list);
		cond_set |=  prev_set;
	}

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	} else if (!list_count(format_list)) {
		slurm_addto_char_list(format_list, "Acc,Des,O");
		if (acct_cond->with_assocs)
			slurm_addto_char_list(format_list,
					      "Cl,ParentN,U,Share,Priority,"
					      "GrpJ,GrpN,"
					      "GrpCPUs,GrpMEM,GrpS,GrpWall,GrpCPUMins,"
					      "MaxJ,MaxN,MaxCPUs,MaxS,MaxW,"
					      "MaxCPUMins,QOS,DefaultQOS");

		if (acct_cond->with_coords)
			slurm_addto_char_list(format_list, "Coord");

	}

	if (!acct_cond->with_assocs && cond_set > 1) {
		if (!commit_check("You requested options that are only valid "
				 "when querying with the withassoc option.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			FREE_NULL_LIST(format_list);
			slurmdb_destroy_account_cond(acct_cond);
			return SLURM_SUCCESS;
		}
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	acct_list = slurmdb_accounts_get(db_conn, acct_cond);
	slurmdb_destroy_account_cond(acct_cond);

	if (!acct_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(acct_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((acct = list_next(itr))) {
		if (acct->assoc_list) {
			ListIterator itr3 =
				list_iterator_create(acct->assoc_list);
			while((assoc = list_next(itr3))) {
				int curr_inx = 1;
				while((field = list_next(itr2))) {
					switch(field->type) {
					case PRINT_ACCT:
						field->print_routine(
							field, acct->name,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_COORDS:
						field->print_routine(
							field,
							acct->coordinators,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_DESC:
						field->print_routine(
							field,
							acct->description,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_ORG:
						field->print_routine(
							field,
							acct->organization,
							(curr_inx ==
							 field_count));
						break;
					default:
						sacctmgr_print_assoc_rec(
							assoc, field, NULL,
							(curr_inx ==
							 field_count));
						break;
					}
					curr_inx++;
				}
				list_iterator_reset(itr2);
				printf("\n");
			}
			list_iterator_destroy(itr3);
		} else {
			int curr_inx = 1;
			while((field = list_next(itr2))) {
				switch(field->type) {
				case PRINT_QOS:
					field->print_routine(
						field, NULL,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_ACCT:
					field->print_routine(
						field, acct->name,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_COORDS:
					field->print_routine(
						field,
						acct->coordinators,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_DESC:
					field->print_routine(
						field, acct->description,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_ORG:
					field->print_routine(
						field, acct->organization,
						(curr_inx ==
						 field_count));
					break;
				case PRINT_PRIO:
					field->print_routine(
						field,
						INFINITE,
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
	}

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(acct_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_account_cond_t *acct_cond =
		xmalloc(sizeof(slurmdb_account_cond_t));
	slurmdb_account_rec_t *acct = xmalloc(sizeof(slurmdb_account_rec_t));
	slurmdb_assoc_rec_t *assoc =
		xmalloc(sizeof(slurmdb_assoc_rec_t));

	int i=0;
	int cond_set = 0, prev_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	slurmdb_init_assoc_rec(assoc, 0);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			prev_set = _set_cond(&i, argc, argv, acct_cond, NULL);
			cond_set |= prev_set;
		} else if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			prev_set = _set_rec(&i, argc, argv, NULL, NULL,
					    acct, assoc);
			rec_set |= prev_set;
		} else {
			prev_set = _set_cond(&i, argc, argv, acct_cond, NULL);
			cond_set |= prev_set;
		}
	}

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		slurmdb_destroy_account_rec(acct);
		slurmdb_destroy_assoc_rec(assoc);
		return SLURM_ERROR;
	} else if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		slurmdb_destroy_account_cond(acct_cond);
		slurmdb_destroy_account_rec(acct);
		slurmdb_destroy_assoc_rec(assoc);
		return SLURM_ERROR;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			slurmdb_destroy_account_cond(acct_cond);
			slurmdb_destroy_account_rec(acct);
			slurmdb_destroy_assoc_rec(assoc);
			return SLURM_SUCCESS;
		}
	}

	// Special case:  reset raw usage only
	if (assoc->usage) {
		rc = SLURM_ERROR;
		if (assoc->usage->usage_raw == 0.0)
			rc = sacctmgr_remove_assoc_usage(acct_cond->assoc_cond);
		else
			error("Raw usage can only be set to 0 (zero)");

		slurmdb_destroy_account_cond(acct_cond);
		slurmdb_destroy_account_rec(acct);
		slurmdb_destroy_assoc_rec(assoc);
		return rc;
	}

	notice_thread_init();
	if (rec_set & 1) { // process the account changes
		if (cond_set == 2) {
			exit_code=1;
			fprintf(stderr,
				" There was a problem with your "
				"'where' options.\n");
			rc = SLURM_ERROR;
			goto assoc_start;
		}
		ret_list = slurmdb_accounts_modify(
			db_conn, acct_cond, acct);
		if (ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified accounts...\n");
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
	}

assoc_start:
	if (rec_set == 3 || rec_set == 2) { // process the association changes
		if (cond_set == 1 && !acct_cond->assoc_cond->acct_list) {
			rc = SLURM_ERROR;
			exit_code=1;
			fprintf(stderr,
				" There was a problem with your "
				"'where' options.\n");
			goto assoc_end;
		}

		if (assoc->parent_acct) {
			slurmdb_account_rec_t *acct_rec =
				sacctmgr_find_account(assoc->parent_acct);
			if (!acct_rec) {
				exit_code=1;
				fprintf(stderr,
					" Parent Account %s doesn't exist.\n",
					assoc->parent_acct);
				rc = SLURM_ERROR;
				goto assoc_end;
			}
		}

		ret_list = slurmdb_associations_modify(
			db_conn, acct_cond->assoc_cond, assoc);

		if (ret_list && list_count(ret_list)) {
			set = 1;
			if (assoc->def_qos_id != NO_VAL)
				set = sacctmgr_check_default_qos(
					     assoc->def_qos_id,
					     acct_cond->assoc_cond);
			else if (assoc->qos_list)
				set = sacctmgr_check_default_qos(
					     -1, acct_cond->assoc_cond);

			if (set) {
				char *object = NULL;
				ListIterator itr = list_iterator_create(
					ret_list);
				printf(" Modified account associations...\n");
				while((object = list_next(itr))) {
					printf("  %s\n", object);
				}
				list_iterator_destroy(itr);
				set = 1;
			}
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
	}

assoc_end:

	notice_thread_fini();
	if (set) {
		if (commit_check("Would you like to commit changes?"))
			slurmdb_connection_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	}
	slurmdb_destroy_account_cond(acct_cond);
	slurmdb_destroy_account_rec(acct);
	slurmdb_destroy_assoc_rec(assoc);

	return rc;
}

extern int sacctmgr_delete_account(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_account_cond_t *acct_cond =
		xmalloc(sizeof(slurmdb_account_cond_t));
	int i = 0;
	List ret_list = NULL, local_assoc_list = NULL;
	ListIterator itr = NULL;
	int cond_set = 0, prev_set = 0;

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, acct_cond, NULL);
		cond_set |= prev_set;
	}

	if (!cond_set) {
		exit_code = 1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	if (exit_code) {
		slurmdb_destroy_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	if (!acct_cond->assoc_cond) {
		error("%s: Association condition is NULL", __func__);
		slurmdb_destroy_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	/* check to see if person is trying to remove root account.  This is
	 * bad, and should not be allowed outside of deleting a cluster.
	 */
	if (acct_cond->assoc_cond
	   && acct_cond->assoc_cond->acct_list
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		char *tmp_char = NULL;
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while ((tmp_char = list_next(itr))) {
			if (!xstrcasecmp(tmp_char, "root"))
				break;
		}
		list_iterator_destroy(itr);
		if (tmp_char) {
			exit_code=1;
			fprintf(stderr, " You are not allowed to remove "
				"the root account.\n"
				" Use remove cluster instead.\n");
			slurmdb_destroy_account_cond(acct_cond);
			return SLURM_ERROR;
		}
	}

	acct_cond->assoc_cond->only_defs = 1;
	local_assoc_list = slurmdb_associations_get(
		db_conn, acct_cond->assoc_cond);
	acct_cond->assoc_cond->only_defs = 0;

	notice_thread_init();
	if (cond_set == 1) {
		ret_list = slurmdb_accounts_remove(
			db_conn, acct_cond);
	} else if (cond_set & 2) {
		ret_list = slurmdb_associations_remove(
			db_conn, acct_cond->assoc_cond);
	}
	rc = errno;
	notice_thread_fini();
	slurmdb_destroy_account_cond(acct_cond);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = NULL;

		/* Check to see if person is trying to remove a default
		 * account of a user.  _isdefault only works with the
		 * output from slurmdb_accounts_remove, and
		 * with a previously got assoc_list.
		 */
		if (_isdefault(cond_set, ret_list, local_assoc_list)) {
			exit_code=1;
			fprintf(stderr, " Please either remove the "
				"accounts listed "
				"above from list and resubmit,\n"
				" or change these users default account to "
				"remove the account(s).\n"
				" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
			goto end_it;
		}
		itr = list_iterator_create(ret_list);
		/* If there were jobs running with an association to
		   be deleted, don't.
		*/
		if (rc == ESLURM_JOBS_RUNNING_ON_ASSOC) {
			fprintf(stderr, " Error with request: %s\n",
				slurm_strerror(rc));
			while((object = list_next(itr))) {
				fprintf(stderr,"  %s\n", object);
			}
			slurmdb_connection_commit(db_conn, 0);
			goto end_it;
		}

		if (cond_set == 1) {
			printf(" Deleting accounts...\n");
		} else if (cond_set & 2) {
			printf(" Deleting account associations...\n");
		}
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

end_it:

	FREE_NULL_LIST(ret_list);
	FREE_NULL_LIST(local_assoc_list);

	return rc;
}
