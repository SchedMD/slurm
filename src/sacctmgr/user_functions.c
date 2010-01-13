/*****************************************************************************\
 *  user_functions.c - functions dealing with users in the accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

static int _set_cond(int *start, int argc, char *argv[],
		     acct_user_cond_t *user_cond,
		     List format_list)
{
	int i;
	int u_set = 0;
	int a_set = 0;
	int end = 0;
	acct_association_cond_t *assoc_cond = NULL;
	int command_len = 0;
	int option = 0;

	if(!user_cond) {
		error("No user_cond given");
		return -1;
	}

	if(!user_cond->assoc_cond)
		user_cond->assoc_cond =
			xmalloc(sizeof(acct_association_cond_t));

	assoc_cond = user_cond->assoc_cond;

	/* we need this to make sure we only change users, not
	 * accounts if this list didn't exist it would change
	 * accounts. Having it blank is fine, it just needs to
	 * exist.
	 */
	if(!assoc_cond->user_list)
		assoc_cond->user_list = list_create(slurm_destroy_char);

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !strncasecmp (argv[i], "WithAssoc",
						 MAX(command_len, 5))) {
			user_cond->with_assocs = 1;
		} else if (!end &&
			   !strncasecmp (argv[i], "WithCoordinators",
					 MAX(command_len, 5))) {
			user_cond->with_coords = 1;
		} else if (!end &&
			   !strncasecmp (argv[i], "WithDeleted",
					 MAX(command_len, 5))) {
			user_cond->with_deleted = 1;
			assoc_cond->with_deleted = 1;
		} else if (!end &&
			   !strncasecmp (argv[i], "WithRawQOSLevel",
					 MAX(command_len, 5))) {
			assoc_cond->with_raw_qos = 1;
		} else if (!end && !strncasecmp (argv[i], "WOPLimits",
						 MAX(command_len, 4))) {
			assoc_cond->without_parent_limits = 1;
		} else if(!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Names",
					   MAX(command_len, 1))
			  || !strncasecmp (argv[i], "Users",
					   MAX(command_len, 1))) {
			if(slurm_addto_char_list(assoc_cond->user_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Account",
					 MAX(command_len, 2))
			   || !strncasecmp (argv[i], "Acct",
					    MAX(command_len, 4))) {
			if(!assoc_cond->acct_list) {
				assoc_cond->acct_list =
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(assoc_cond->acct_list,
						 argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "AdminLevel",
					 MAX(command_len, 2))) {
			user_cond->admin_level =
				str_2_acct_admin_level(argv[i]+end);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "Clusters",
					 MAX(command_len, 1))) {
			if(!assoc_cond->cluster_list) {
				assoc_cond->cluster_list =
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(assoc_cond->cluster_list,
						 argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "DefaultAccount",
					 MAX(command_len, 8))) {
			if(!user_cond->def_acct_list) {
				user_cond->def_acct_list =
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(user_cond->def_acct_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "DefaultWCKey",
					 MAX(command_len, 8))) {
			if(!user_cond->def_wckey_list) {
				user_cond->def_wckey_list =
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(user_cond->def_wckey_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "FairShare",
					 MAX(command_len, 1))
			   || !strncasecmp (argv[i], "Shares",
					 MAX(command_len, 1))) {
			if(!assoc_cond->fairshare_list)
				assoc_cond->fairshare_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->fairshare_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCPUMins",
					 MAX(command_len, 7))) {
			if(!assoc_cond->grp_cpu_mins_list)
				assoc_cond->grp_cpu_mins_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_cpu_mins_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCpus",
					 MAX(command_len, 7))) {
			if(!assoc_cond->grp_cpus_list)
				assoc_cond->grp_cpus_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_cpus_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpJobs",
					 MAX(command_len, 4))) {
			if(!assoc_cond->grp_jobs_list)
				assoc_cond->grp_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_jobs_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpNodes",
					 MAX(command_len, 4))) {
			if(!assoc_cond->grp_nodes_list)
				assoc_cond->grp_nodes_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_nodes_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpSubmitJobs",
					 MAX(command_len, 4))) {
			if(!assoc_cond->grp_submit_jobs_list)
				assoc_cond->grp_submit_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->grp_submit_jobs_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			if(!assoc_cond->grp_wall_list)
				assoc_cond->grp_wall_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->grp_wall_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCPUMinsPerJob",
					 MAX(command_len, 7))) {
			if(!assoc_cond->max_cpu_mins_pj_list)
				assoc_cond->max_cpu_mins_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_cpu_mins_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCpusPerJob",
					 MAX(command_len, 7))) {
			if(!assoc_cond->max_cpus_pj_list)
				assoc_cond->max_cpus_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_cpus_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs",
					 MAX(command_len, 4))) {
			if(!assoc_cond->max_jobs_list)
				assoc_cond->max_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_jobs_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodesPerJob",
					 MAX(command_len, 4))) {
			if(!assoc_cond->max_nodes_pj_list)
				assoc_cond->max_nodes_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_nodes_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxSubmitJobs",
					 MAX(command_len, 4))) {
			if(!assoc_cond->max_submit_jobs_list)
				assoc_cond->max_submit_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_submit_jobs_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWallDurationPerJob",
					 MAX(command_len, 4))) {
			if(!assoc_cond->max_wall_pj_list)
				assoc_cond->max_wall_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_wall_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "Partition",
					 MAX(command_len, 3))) {
			if(!assoc_cond->partition_list) {
				assoc_cond->partition_list =
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   assoc_cond->partition_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "QosLevel",
					 MAX(command_len, 1))) {
			if(!assoc_cond->qos_list) {
				assoc_cond->qos_list =
					list_create(slurm_destroy_char);
			}

			if(!g_qos_list) {
				g_qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}

			addto_qos_char_list(assoc_cond->qos_list,
					    g_qos_list, argv[i]+end, option);
			u_set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
		}
	}

	(*start) = i;

	if(u_set && a_set)
		return 3;
	else if(a_set) {
		return 2;
	} else if(u_set)
		return 1;

	return 0;
}

static int _set_rec(int *start, int argc, char *argv[],
		    acct_user_rec_t *user,
		    acct_association_rec_t *assoc)
{
	int i, mins;
	int u_set = 0;
	int a_set = 0;
	int end = 0;
	int command_len = 0;
	int option = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if(!end && !strncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		} else if(!end) {
			exit_code=1;
			fprintf(stderr,
				" Bad format on %s: End your option with "
				"an '=' sign\n", argv[i]);
		} else if (!strncasecmp (argv[i], "AdminLevel",
					 MAX(command_len, 2))) {
			user->admin_level =
				str_2_acct_admin_level(argv[i]+end);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "DefaultAccount",
					 MAX(command_len, 8))) {
			if(user->default_acct)
				xfree(user->default_acct);
			user->default_acct = strip_quotes(argv[i]+end, NULL, 1);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "DefaultWCKey",
					 MAX(command_len, 8))) {
			if(user->default_wckey)
				xfree(user->default_wckey);
			user->default_wckey =
				strip_quotes(argv[i]+end, NULL, 1);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "FairShare",
					 MAX(command_len, 1))
			   || !strncasecmp (argv[i], "Shares",
					 MAX(command_len, 1))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->shares_raw,
				     "FairShare") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCPUMins",
					 MAX(command_len, 7))) {
			if(!assoc)
				continue;
			if (get_uint64(argv[i]+end,
				       &assoc->grp_cpu_mins,
				       "GrpCPUMins") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCpus",
					 MAX(command_len, 7))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_cpus,
			    "GrpCpus") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpJobs",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_jobs,
			    "GrpJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpNodes",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_nodes,
			    "GrpNodes") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpSubmitJobs",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_submit_jobs,
			    "GrpSubmitJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				assoc->grp_wall	= (uint32_t) mins;
				a_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad GrpWall time format: %s\n",
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "MaxCPUMinsPerJob",
					 MAX(command_len, 7))) {
			if(!assoc)
				continue;
			if (get_uint64(argv[i]+end,
				       &assoc->max_cpu_mins_pj,
				       "MaxCPUMins") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCpusPerJob",
					 MAX(command_len, 7))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->max_cpus_pj,
			    "MaxCpus") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->max_jobs,
			    "MaxJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodesPerJob",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end,
			    &assoc->max_nodes_pj,
			    "MaxNodes") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxSubmitJobs",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->max_submit_jobs,
			    "MaxSubmitJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWallDurationPerJob",
					 MAX(command_len, 4))) {
			if(!assoc)
				continue;
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				assoc->max_wall_pj = (uint32_t) mins;
				a_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad MaxWall time format: %s\n",
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "QosLevel",
					 MAX(command_len, 1))) {
			if(!assoc)
				continue;
			if(!assoc->qos_list)
				assoc->qos_list =
					list_create(slurm_destroy_char);

			if(!g_qos_list)
				g_qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);

			if(addto_qos_char_list(assoc->qos_list,
					       g_qos_list, argv[i]+end, option))
				a_set = 1;
			else
				exit_code = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}

	(*start) = i;

	if(u_set && a_set)
		return 3;
	else if(u_set)
		return 1;
	else if(a_set)
		return 2;
	return 0;
}

/*
 * IN: user_cond - used for the assoc_cond pointing to the user and
 *     account list
 * IN: check - whether or not to check if the existance of the above lists
 */
static int _check_coord_request(acct_user_cond_t *user_cond, bool check)
{
	ListIterator itr = NULL, itr2 = NULL;
	char *name = NULL;
	acct_user_rec_t *user_rec = NULL;
	acct_account_rec_t *acct_rec = NULL;
	acct_account_cond_t account_cond;
	List local_acct_list = NULL;
	List local_user_list = NULL;
	int rc = SLURM_SUCCESS;

	if(!user_cond) {
		exit_code=1;
		fprintf(stderr, " You need to specify the user_cond here.\n");
		return SLURM_ERROR;
	}

	if(check && (!user_cond->assoc_cond->user_list
		     || !list_count(user_cond->assoc_cond->user_list))) {
		exit_code=1;
		fprintf(stderr, " You need to specify a user list here.\n");
		return SLURM_ERROR;
	}

	if(check && (!user_cond->assoc_cond->acct_list
		     || !list_count(user_cond->assoc_cond->acct_list))) {
		exit_code=1;
		fprintf(stderr, " You need to specify a account list here.\n");
		return SLURM_ERROR;
	}

	memset(&account_cond, 0, sizeof(acct_account_cond_t));
	account_cond.assoc_cond = user_cond->assoc_cond;
	local_acct_list =
		acct_storage_g_get_accounts(db_conn, my_uid, &account_cond);
	if(!local_acct_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting accounts from database.  "
			"Contact your admin.\n");
		return SLURM_ERROR;
	}

	if(user_cond->assoc_cond->acct_list &&
	   (list_count(local_acct_list) !=
	    list_count(user_cond->assoc_cond->acct_list))) {

		itr = list_iterator_create(user_cond->assoc_cond->acct_list);
		itr2 = list_iterator_create(local_acct_list);

		while((name = list_next(itr))) {
			while((acct_rec = list_next(itr2))) {
				if(!strcmp(name, acct_rec->name))
					break;
			}
			list_iterator_reset(itr2);
			if(!acct_rec) {
				fprintf(stderr,
					" You specified a non-existant "
					"account '%s'.\n", name);
				exit_code=1;
				rc = SLURM_ERROR;
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr2);
	}

	local_user_list = acct_storage_g_get_users(db_conn, my_uid, user_cond);
	if(!local_user_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting users from database.  "
			"Contact your admin.\n");
		if(local_acct_list)
			list_destroy(local_acct_list);
		return SLURM_ERROR;
	}

	if(user_cond->assoc_cond->user_list &&
	   (list_count(local_user_list) !=
	    list_count(user_cond->assoc_cond->user_list))) {

		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		itr2 = list_iterator_create(local_user_list);

		while((name = list_next(itr))) {
			while((user_rec = list_next(itr2))) {
				if(!strcmp(name, user_rec->name))
					break;
			}
			list_iterator_reset(itr2);
			if(!user_rec) {
				fprintf(stderr,
					" You specified a non-existant "
					"user '%s'.\n", name);
				exit_code=1;
				rc = SLURM_ERROR;
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr2);
	}

	if(local_acct_list)
		list_destroy(local_acct_list);
	if(local_user_list)
		list_destroy(local_user_list);

	return rc;
}

static int _check_user_has_acct(char *user, char *acct)
{
	acct_association_cond_t assoc_cond;
	List ret_list = NULL;

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_push(assoc_cond.acct_list, acct);
	assoc_cond.user_list = list_create(NULL);
	list_push(assoc_cond.user_list, user);
	ret_list = acct_storage_g_get_associations(db_conn, my_uid,
						   &assoc_cond);

	if(ret_list && (list_count(ret_list)))
		return 1;

	return 0;
}

extern int sacctmgr_add_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr_a = NULL;
	ListIterator itr_c = NULL;
	ListIterator itr_p = NULL;
	ListIterator itr_w = NULL;
	acct_user_rec_t *user = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_association_rec_t start_assoc;
	char *default_acct = NULL;
	char *default_wckey = NULL;
	acct_association_cond_t *assoc_cond = NULL;
	acct_wckey_rec_t *wckey = NULL;
	acct_wckey_cond_t *wckey_cond = NULL;
	acct_admin_level_t admin_level = ACCT_ADMIN_NOTSET;
	char *name = NULL, *account = NULL, *cluster = NULL, *partition = NULL;
	int partition_set = 0;
	List user_list = NULL;
	List assoc_list = NULL;
	List wckey_list = NULL;
	List local_assoc_list = NULL;
	List local_acct_list = NULL;
	List local_user_list = NULL;
	List local_wckey_list = NULL;
	char *user_str = NULL;
	char *assoc_str = NULL;
	char *wckey_str = NULL;
	int limit_set = 0, mins;
	int first = 1;
	int acct_first = 1;
	int command_len = 0;
	int option = 0;
	uint16_t track_wckey = slurm_get_track_wckey();

/* 	if(!list_count(sacctmgr_cluster_list)) { */
/* 		printf(" Can't add users, no cluster defined yet.\n" */
/* 		       " Please contact your administrator.\n"); */
/* 		return SLURM_ERROR; */
/* 	} */
	init_acct_association_rec(&start_assoc);

	assoc_cond = xmalloc(sizeof(acct_association_cond_t));

	assoc_cond->user_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->partition_list = list_create(slurm_destroy_char);

	wckey_cond = xmalloc(sizeof(acct_wckey_cond_t));

	wckey_cond->name_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if(!end
		   || !strncasecmp (argv[i], "Names", MAX(command_len, 1))
		   || !strncasecmp (argv[i], "Users", MAX(command_len, 1))) {
			slurm_addto_char_list(assoc_cond->user_list,
					      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Accounts",
					 MAX(command_len, 2))
			   || !strncasecmp (argv[i], "Acct",
					    MAX(command_len, 4))) {
			slurm_addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
		} else if (!strncasecmp (argv[i], "AdminLevel",
					 MAX(command_len, 2))) {
			admin_level = str_2_acct_admin_level(argv[i]+end);
		} else if (!strncasecmp (argv[i], "Clusters",
					 MAX(command_len, 1))) {
			slurm_addto_char_list(assoc_cond->cluster_list,
					      argv[i]+end);
		} else if (!strncasecmp (argv[i], "DefaultAccount",
					 MAX(command_len, 8))) {
			if(default_acct) {
				fprintf(stderr,
					" Already listed DefaultAccount %s\n",
					default_acct);
				exit_code = 1;
				continue;
			}
			default_acct = strip_quotes(argv[i]+end, NULL, 1);
			slurm_addto_char_list(assoc_cond->acct_list,
					      default_acct);
		} else if (!strncasecmp (argv[i], "DefaultWCKey",
					 MAX(command_len, 8))) {
			if(default_wckey) {
				fprintf(stderr,
					" Already listed DefaultWCKey %s\n",
					default_wckey);
				exit_code = 1;
				continue;
			}
			default_wckey = strip_quotes(argv[i]+end, NULL, 1);
			slurm_addto_char_list(wckey_cond->name_list,
					      default_wckey);
		} else if (!strncasecmp (argv[i], "FairShare",
					 MAX(command_len, 1))
			   || !strncasecmp (argv[i], "Shares",
					 MAX(command_len, 1))) {
			if (get_uint(argv[i]+end, &start_assoc.shares_raw,
			    "FairShare") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCPUMins",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end,
				       &start_assoc.grp_cpu_mins,
				       "GrpCPUMins") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCpus",
					 MAX(command_len, 7))) {
			if (get_uint(argv[i]+end, &start_assoc.grp_cpus,
			    "GrpCpus") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "GrpJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &start_assoc.grp_jobs,
			    "GrpJobs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "GrpNodes",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &start_assoc.grp_nodes,
			    "GrpNodes") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "GrpSubmitJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &start_assoc.grp_submit_jobs,
			    "GrpSubmitJobs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				start_assoc.grp_wall = (uint32_t) mins;
				limit_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad GrpWall time format: %s\n",
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "MaxCPUMinsPerJob",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end,
				       &start_assoc.max_cpu_mins_pj,
				       "MaxCPUMins") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCpusPerJob",
					 MAX(command_len, 7))) {
			if (get_uint(argv[i]+end, &start_assoc.max_cpus_pj,
			    "MaxCpus") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &start_assoc.max_jobs,
			    "MaxJobs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodesPerJob",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end,
			    &start_assoc.max_nodes_pj,
			    "MaxNodes") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxSubmitJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &start_assoc.max_submit_jobs,
			    "MaxSubmitJobs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWallDurationPerJob",
					 MAX(command_len, 4))) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				start_assoc.max_wall_pj = (uint32_t) mins;
				limit_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad MaxWall time format: %s\n",
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "Partitions",
					 MAX(command_len, 1))) {
			slurm_addto_char_list(assoc_cond->partition_list,
					argv[i]+end);
		} else if (!strncasecmp (argv[i], "QosLevel",
					 MAX(command_len, 1))) {
			if(!start_assoc.qos_list)
				start_assoc.qos_list =
					list_create(slurm_destroy_char);

			if(!g_qos_list)
				g_qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);

			if(addto_qos_char_list(start_assoc.qos_list, g_qos_list,
					       argv[i]+end, option))
				limit_set = 1;
			else {
				exit_code = 1;
				fprintf(stderr,
					" Bad QosLevel value: %s\n",
					argv[i]+end);
			}
		} else if (!strncasecmp (argv[i], "WCKeys",
					 MAX(command_len, 1))) {
			slurm_addto_char_list(wckey_cond->name_list,
					      argv[i]+end);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}
	}

	if(exit_code) {
		destroy_acct_wckey_cond(wckey_cond);
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	} else if(!list_count(assoc_cond->user_list)) {
		destroy_acct_wckey_cond(wckey_cond);
		destroy_acct_association_cond(assoc_cond);
		exit_code=1;
		fprintf(stderr, " Need name of user to add.\n");
		return SLURM_ERROR;
	} else {
 		acct_user_cond_t user_cond;

		memset(&user_cond, 0, sizeof(acct_user_cond_t));
		user_cond.assoc_cond = assoc_cond;

		local_user_list = acct_storage_g_get_users(
			db_conn, my_uid, &user_cond);

	}

	if(!local_user_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting users from database.  "
			"Contact your admin.\n");
		destroy_acct_wckey_cond(wckey_cond);
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	}


	if(!list_count(assoc_cond->cluster_list)) {
		List cluster_list = NULL;
		acct_cluster_rec_t *cluster_rec = NULL;

		cluster_list = acct_storage_g_get_clusters(db_conn,
							   my_uid, NULL);
		if(!cluster_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
			       "Contact your admin.\n");
			destroy_acct_wckey_cond(wckey_cond);
			destroy_acct_association_cond(assoc_cond);
			list_destroy(local_user_list);
			if(local_acct_list)
				list_destroy(local_acct_list);
			return SLURM_ERROR;
		}

		itr_c = list_iterator_create(cluster_list);
		while((cluster_rec = list_next(itr_c))) {
			list_append(assoc_cond->cluster_list,
				    xstrdup(cluster_rec->name));
		}
		list_iterator_destroy(itr_c);

		if(!list_count(assoc_cond->cluster_list)) {
			exit_code=1;
			fprintf(stderr,
				"  Can't add users, no cluster defined yet.\n"
				" Please contact your administrator.\n");
			destroy_acct_wckey_cond(wckey_cond);
			destroy_acct_association_cond(assoc_cond);
			list_destroy(local_user_list);
			if(local_acct_list)
				list_destroy(local_acct_list);
			return SLURM_ERROR;
		}
	} else {
		List temp_list = NULL;
		acct_cluster_cond_t cluster_cond;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		cluster_cond.cluster_list = assoc_cond->cluster_list;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);

		itr_c = list_iterator_create(assoc_cond->cluster_list);
		itr = list_iterator_create(temp_list);
		while((cluster = list_next(itr_c))) {
			acct_cluster_rec_t *cluster_rec = NULL;

			list_iterator_reset(itr);
			while((cluster_rec = list_next(itr))) {
				if(!strcasecmp(cluster_rec->name, cluster))
					break;
			}
			if(!cluster_rec) {
				exit_code=1;
				fprintf(stderr, " This cluster '%s' "
				       "doesn't exist.\n"
				       "        Contact your admin "
				       "to add it to accounting.\n",
				       cluster);
				list_delete_item(itr_c);
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr_c);
		list_destroy(temp_list);

		if(!list_count(assoc_cond->cluster_list)) {
			destroy_acct_wckey_cond(wckey_cond);
			destroy_acct_association_cond(assoc_cond);
			list_destroy(local_user_list);
			if(local_acct_list)
				list_destroy(local_acct_list);
			return SLURM_ERROR;
		}
	}

	if(!list_count(assoc_cond->acct_list)) {
		if(!list_count(wckey_cond->name_list)) {
			destroy_acct_wckey_cond(wckey_cond);
			destroy_acct_association_cond(assoc_cond);
			exit_code=1;
			fprintf(stderr, " Need name of account to "
				"add user to.\n");
			return SLURM_ERROR;
		}
	} else {
 		acct_account_cond_t account_cond;
		acct_association_cond_t query_assoc_cond;

		memset(&account_cond, 0, sizeof(acct_account_cond_t));
		account_cond.assoc_cond = assoc_cond;

		local_acct_list = acct_storage_g_get_accounts(
			db_conn, my_uid, &account_cond);

		if(!local_acct_list) {
			exit_code=1;
			fprintf(stderr, " Problem getting accounts "
				"from database.  Contact your admin.\n");
			list_destroy(local_user_list);
			destroy_acct_wckey_cond(wckey_cond);
			destroy_acct_association_cond(assoc_cond);
			return SLURM_ERROR;
		}

		if(!default_acct)
			default_acct =
				xstrdup(list_peek(assoc_cond->acct_list));

		memset(&query_assoc_cond, 0, sizeof(acct_association_cond_t));
		query_assoc_cond.acct_list = assoc_cond->acct_list;
		query_assoc_cond.cluster_list = assoc_cond->cluster_list;
		local_assoc_list = acct_storage_g_get_associations(
			db_conn, my_uid, &query_assoc_cond);

		if(!local_assoc_list) {
			exit_code=1;
			fprintf(stderr, " Problem getting associations "
				"from database.  Contact your admin.\n");
			list_destroy(local_user_list);
			list_destroy(local_acct_list);
			destroy_acct_wckey_cond(wckey_cond);
			destroy_acct_association_cond(assoc_cond);
			return SLURM_ERROR;
		}
	}

	if(track_wckey) {
		if(!default_wckey)
			default_wckey =
				xstrdup(list_peek(wckey_cond->name_list));
		wckey_cond->cluster_list = assoc_cond->cluster_list;
		wckey_cond->user_list = assoc_cond->user_list;
		local_wckey_list = acct_storage_g_get_wckeys(
			db_conn, my_uid, wckey_cond);
		wckey_cond->cluster_list = NULL;
		wckey_cond->user_list = NULL;

	}

	/* we are adding these lists to the global lists and will be
	   freed when they are */
	user_list = list_create(destroy_acct_user_rec);
	assoc_list = list_create(destroy_acct_association_rec);
	wckey_list = list_create(destroy_acct_wckey_rec);

	itr = list_iterator_create(assoc_cond->user_list);
	while((name = list_next(itr))) {
		user = NULL;
		if(!sacctmgr_find_user_from_list(local_user_list, name)) {
			uid_t pw_uid;
			if(!default_acct) {
				exit_code=1;
				fprintf(stderr, " Need a default account for "
				       "these users to add.\n");
				rc = SLURM_ERROR;
				goto no_default;
			}
			if(first) {
				if(!sacctmgr_find_account_from_list(
					   local_acct_list, default_acct)) {
					exit_code=1;
					fprintf(stderr, " This account '%s' "
						"doesn't exist.\n"
						"        Contact your admin "
						"to add this account.\n",
					       default_acct);
					continue;
				}
				first = 0;
			}
			if (uid_from_string (name, &pw_uid) < 0) {
				char *warning = xstrdup_printf(
					"There is no uid for user '%s'"
					"\nAre you sure you want to continue?",
					name);

				if(!commit_check(warning)) {
					xfree(warning);
					rc = SLURM_ERROR;
					list_flush(user_list);
					goto end_it;
				}
				xfree(warning);
			}

			user = xmalloc(sizeof(acct_user_rec_t));
			user->assoc_list =
				list_create(destroy_acct_association_rec);
			user->wckey_list =
				list_create(destroy_acct_wckey_rec);
			user->name = xstrdup(name);
			user->default_acct = xstrdup(default_acct);
			user->default_wckey = xstrdup(default_wckey);

			user->admin_level = admin_level;

			xstrfmtcat(user_str, "  %s\n", name);

			list_append(user_list, user);
		}

		itr_a = list_iterator_create(assoc_cond->acct_list);
		while((account = list_next(itr_a))) {
			if(acct_first) {
				if(!sacctmgr_find_account_from_list(
					   local_acct_list, default_acct)) {
					exit_code=1;
					fprintf(stderr, " This account '%s' "
						"doesn't exist.\n"
						"        Contact your admin "
						"to add this account.\n",
					       account);
					continue;
				}
			}
			itr_c = list_iterator_create(assoc_cond->cluster_list);
			while((cluster = list_next(itr_c))) {
				if(!sacctmgr_find_account_base_assoc_from_list(
					   local_assoc_list, account,
					   cluster)) {
					if(acct_first) {
						exit_code=1;
						fprintf(stderr, " This "
							"account '%s' "
							"doesn't exist on "
							"cluster %s\n"
							"        Contact your "
							"admin to add "
							"this account.\n",
							account, cluster);
					}
					continue;
				}

				itr_p = list_iterator_create(
					assoc_cond->partition_list);
				while((partition = list_next(itr_p))) {
					partition_set = 1;
					if(sacctmgr_find_association_from_list(
						   local_assoc_list,
						   name, account,
						   cluster, partition))
						continue;
					assoc = xmalloc(
						sizeof(acct_association_rec_t));
					init_acct_association_rec(assoc);
					assoc->user = xstrdup(name);
					assoc->acct = xstrdup(account);
					assoc->cluster = xstrdup(cluster);
					assoc->partition = xstrdup(partition);

					assoc->shares_raw =
						start_assoc.shares_raw;

					assoc->grp_cpu_mins =
						start_assoc.grp_cpu_mins;
					assoc->grp_cpus = start_assoc.grp_cpus;
					assoc->grp_jobs = start_assoc.grp_jobs;
					assoc->grp_nodes =
						start_assoc.grp_nodes;
					assoc->grp_submit_jobs =
						start_assoc.grp_submit_jobs;
					assoc->grp_wall = start_assoc.grp_wall;

					assoc->max_cpu_mins_pj =
						start_assoc.max_cpu_mins_pj;
					assoc->max_cpus_pj =
						start_assoc.max_cpus_pj;
					assoc->max_jobs = start_assoc.max_jobs;
					assoc->max_nodes_pj =
						start_assoc.max_nodes_pj;
					assoc->max_submit_jobs =
						start_assoc.max_submit_jobs;
					assoc->max_wall_pj =
						start_assoc.max_wall_pj;

					assoc->qos_list = copy_char_list(
						start_assoc.qos_list);

					if(user)
						list_append(user->assoc_list,
							    assoc);
					else
						list_append(assoc_list, assoc);
					xstrfmtcat(assoc_str,
						   "  U = %-9.9s"
						   " A = %-10.10s"
						   " C = %-10.10s"
						   " P = %-10.10s\n",
						   assoc->user, assoc->acct,
						   assoc->cluster,
						   assoc->partition);
				}
				list_iterator_destroy(itr_p);
				if(partition_set)
					continue;

				if(sacctmgr_find_association_from_list(
					   local_assoc_list,
					   name, account, cluster, NULL)) {
					continue;
				}

				assoc = xmalloc(sizeof(acct_association_rec_t));
				init_acct_association_rec(assoc);
				assoc->user = xstrdup(name);
				assoc->acct = xstrdup(account);
				assoc->cluster = xstrdup(cluster);

				assoc->shares_raw = start_assoc.shares_raw;

				assoc->grp_cpu_mins =
					start_assoc.grp_cpu_mins;
				assoc->grp_cpus = start_assoc.grp_cpus;
				assoc->grp_jobs = start_assoc.grp_jobs;
				assoc->grp_nodes = start_assoc.grp_nodes;
				assoc->grp_submit_jobs =
					start_assoc.grp_submit_jobs;
				assoc->grp_wall = start_assoc.grp_wall;

				assoc->max_cpu_mins_pj =
					start_assoc.max_cpu_mins_pj;
				assoc->max_cpus_pj = start_assoc.max_cpus_pj;
				assoc->max_jobs = start_assoc.max_jobs;
				assoc->max_nodes_pj = start_assoc.max_nodes_pj;
				assoc->max_submit_jobs =
					start_assoc.max_submit_jobs;
				assoc->max_wall_pj = start_assoc.max_wall_pj;

				assoc->qos_list =
					copy_char_list(start_assoc.qos_list);

				if(user)
					list_append(user->assoc_list, assoc);
				else
					list_append(assoc_list, assoc);
				xstrfmtcat(assoc_str,
					   "  U = %-9.9s"
					   " A = %-10.10s"
					   " C = %-10.10s\n",
					   assoc->user, assoc->acct,
					   assoc->cluster);
			}
			list_iterator_destroy(itr_c);
		}
		list_iterator_destroy(itr_a);
		acct_first = 0;

		/* continue here if not doing wckeys */
		if(!track_wckey)
			continue;

		itr_w = list_iterator_create(wckey_cond->name_list);
		while((account = list_next(itr_w))) {
			itr_c = list_iterator_create(assoc_cond->cluster_list);
			while((cluster = list_next(itr_c))) {
				if(sacctmgr_find_wckey_from_list(
					   local_wckey_list, name, account,
					   cluster)) {
					continue;
				}
				wckey = xmalloc(sizeof(acct_wckey_rec_t));
				wckey->user = xstrdup(name);
				wckey->name = xstrdup(account);
				wckey->cluster = xstrdup(cluster);
				if(user)
					list_append(user->wckey_list, wckey);
				else
					list_append(wckey_list, wckey);
				xstrfmtcat(wckey_str,
					   "  U = %-9.9s"
					   " W = %-10.10s"
					   " C = %-10.10s\n",
					   wckey->user, wckey->name,
					   wckey->cluster);

			}
			list_iterator_destroy(itr_c);
		}
		list_iterator_destroy(itr_w);
	}
no_default:
	list_iterator_destroy(itr);
	list_destroy(local_user_list);
	if(local_acct_list)
		list_destroy(local_acct_list);
	if(local_assoc_list)
		list_destroy(local_assoc_list);
	if(local_wckey_list)
		list_destroy(local_wckey_list);
	destroy_acct_wckey_cond(wckey_cond);
	destroy_acct_association_cond(assoc_cond);

	if(!list_count(user_list) && !list_count(assoc_list)
	   && !list_count(wckey_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	} else if(!assoc_str && !wckey_str) {
		exit_code=1;
		fprintf(stderr, " No associations or wckeys created.\n");
		goto end_it;
	}

	if(user_str) {
		printf(" Adding User(s)\n%s", user_str);
		printf(" Settings =\n");
		printf("  Default Account = %s\n", default_acct);
		if(track_wckey)
			printf("  Default WCKey   = %s\n", default_wckey);

		if(admin_level != ACCT_ADMIN_NOTSET)
			printf("  Admin Level     = %s\n",
			       acct_admin_level_str(admin_level));
		xfree(user_str);
	}

	if(assoc_str) {
		printf(" Associations =\n%s", assoc_str);
		xfree(assoc_str);
	}

	if(wckey_str) {
		printf(" WCKeys =\n%s", wckey_str);
		xfree(wckey_str);
	}

	if(limit_set) {
		printf(" Non Default Settings\n");
		sacctmgr_print_assoc_limits(&start_assoc);
		if(start_assoc.qos_list)
			list_destroy(start_assoc.qos_list);
	}

	notice_thread_init();
	if(list_count(user_list)) {
		rc = acct_storage_g_add_users(db_conn, my_uid, user_list);
	}

	if(rc == SLURM_SUCCESS) {
		if(list_count(assoc_list))
			rc = acct_storage_g_add_associations(db_conn, my_uid,
							     assoc_list);
	}

	if(rc == SLURM_SUCCESS) {
		if(list_count(wckey_list))
			rc = acct_storage_g_add_wckeys(db_conn, my_uid,
						       wckey_list);
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding users\n");
		rc = SLURM_ERROR;
		notice_thread_fini();
		goto end_it;
	}

	notice_thread_fini();

	if(rc == SLURM_SUCCESS) {
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding user associations\n");
		rc = SLURM_ERROR;
	}

end_it:
	list_destroy(user_list);
	list_destroy(assoc_list);
	list_destroy(wckey_list);
	xfree(default_acct);
	xfree(default_wckey);

	return rc;
}

extern int sacctmgr_add_coord(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	int cond_set = 0, prev_set = 0;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	char *name = NULL;
	char *user_str = NULL;
	char *acct_str = NULL;
	ListIterator itr = NULL;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, user_cond, NULL);
		cond_set = MAX(cond_set, prev_set);
	}

	if(exit_code) {
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	} else if(!cond_set) {
		exit_code=1;
		fprintf(stderr, " You need to specify conditions to "
		       "to add the coordinator.\n");
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}

	if((_check_coord_request(user_cond, true) == SLURM_ERROR)
	   || exit_code) {
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(user_cond->assoc_cond->user_list);
	while((name = list_next(itr))) {
		xstrfmtcat(user_str, "  %s\n", name);
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(user_cond->assoc_cond->acct_list);
	while((name = list_next(itr))) {
		xstrfmtcat(acct_str, "  %s\n", name);
	}
	list_iterator_destroy(itr);

	printf(" Adding Coordinator User(s)\n%s", user_str);
	printf(" To Account(s) and all sub-accounts\n%s", acct_str);

	notice_thread_init();
	rc = acct_storage_g_add_coord(db_conn, my_uid,
				      user_cond->assoc_cond->acct_list,
				      user_cond);
	notice_thread_fini();
	destroy_acct_user_cond(user_cond);

	if(rc == SLURM_SUCCESS) {
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding coordinator\n");
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int sacctmgr_list_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	List user_list;
	int i=0, cond_set=0, prev_set=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	acct_user_rec_t *user = NULL;
	acct_association_rec_t *assoc = NULL;
	char *object;

	print_field_t *field = NULL;
	int field_count = 0;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_ADMIN,
		PRINT_CLUSTER,
		PRINT_COORDS,
		PRINT_DACCT,
		PRINT_DWCKEY,
		PRINT_FAIRSHARE,
		PRINT_GRPCM,
		PRINT_GRPC,
		PRINT_GRPJ,
		PRINT_GRPN,
		PRINT_GRPS,
		PRINT_GRPW,
		PRINT_ID,
		PRINT_MAXC,
		PRINT_MAXCM,
		PRINT_MAXJ,
		PRINT_MAXN,
		PRINT_MAXS,
		PRINT_MAXW,
		PRINT_QOS,
		PRINT_QOS_RAW,
		PRINT_PID,
		PRINT_PNAME,
		PRINT_PART,
		PRINT_USER
	};

	user_cond->with_assocs = with_assoc_flag;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, user_cond, format_list);
		cond_set = MAX(cond_set, prev_set);
	}

	if(exit_code) {
		destroy_acct_user_cond(user_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	}

	if(!list_count(format_list)) {
		if(slurm_get_track_wckey())
			slurm_addto_char_list(format_list,
					      "U,DefaultA,DefaultW,Ad");
		else
			slurm_addto_char_list(format_list, "U,DefaultA,Ad");
		if(user_cond->with_assocs)
			slurm_addto_char_list(format_list,
					      "Cl,Ac,Part,F,"
					      "MaxJ,MaxN,MaxS,MaxW,QOS");
		if(user_cond->with_coords)
			slurm_addto_char_list(format_list, "Coord");
	}

	if(!user_cond->with_assocs && cond_set > 1) {
		if(!commit_check("You requested options that are only vaild "
				 "when querying with the withassoc option.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			list_destroy(format_list);
			destroy_acct_user_cond(user_cond);
			return SLURM_SUCCESS;
		}
	}

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		int command_len = 0;
		int newlen = 0;

		if((tmp_char = strstr(object, "\%"))) {
			newlen = atoi(tmp_char+1);
			tmp_char[0] = '\0';
		}

		command_len = strlen(object);

		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, MAX(command_len, 2))
		   || !strncasecmp ("Acct", object, MAX(command_len, 4))) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("AdminLevel", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_ADMIN;
			field->name = xstrdup("Admin");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Coordinators", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_COORDS;
			field->name = xstrdup("Coord Accounts");
			field->len = 20;
			field->print_routine = sacctmgr_print_coord_list;
		} else if(!strncasecmp("DefaultAccount", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_DACCT;
			field->name = xstrdup("Def Acct");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("DefaultWCKey", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_DWCKEY;
			field->name = xstrdup("Def WCKey");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("FairShare", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("FairShare");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpCPUMins", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_GRPCM;
			field->name = xstrdup("GrpCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("GrpCPUs", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_GRPC;
			field->name = xstrdup("GrpCPUs");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpJobs", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_GRPJ;
			field->name = xstrdup("GrpJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpNodes", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_GRPN;
			field->name = xstrdup("GrpNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpSubmitJobs", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_GRPS;
			field->name = xstrdup("GrpSubmit");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpWall", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_GRPW;
			field->name = xstrdup("GrpWall");
			field->len = 11;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("ID", object, MAX(command_len, 1))) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxCPUMinsPerJob", object,
				       MAX(command_len, 7))) {
			field->type = PRINT_MAXCM;
			field->name = xstrdup("MaxCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("MaxCPUsPerJob", object,
				       MAX(command_len, 7))) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUs");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxJobs", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXJ;
			field->name = xstrdup("MaxJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxNodesPerJob", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXN;
			field->name = xstrdup("MaxNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxSubmitJobs", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXS;
			field->name = xstrdup("MaxSubmit");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxWallDurationPerJob", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXW;
			field->name = xstrdup("MaxWall");
			field->len = 11;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("QOSRAWLevel", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_QOS_RAW;
			field->name = xstrdup("QOS_RAW");
			field->len = 10;
			field->print_routine = print_fields_char_list;
		} else if(!strncasecmp("QOSLevel", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_QOS;
			field->name = xstrdup("QOS");
			field->len = 20;
			field->print_routine = sacctmgr_print_qos_list;
		} else if(!strncasecmp("ParentID", object,
				       MAX(command_len, 7))) {
			field->type = PRINT_PID;
			field->name = xstrdup("Par ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("Partition", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_PART;
			field->name = xstrdup("Partition");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Shares", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("Shares");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("User", object, MAX(command_len, 1))
			  || !strncasecmp("Name", object,
					  MAX(command_len, 2))) {
			field->type = PRINT_USER;
			field->name = xstrdup("User");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}

		if(newlen)
			field->len = newlen;

		list_append(print_fields_list, field);
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if(exit_code) {
		destroy_acct_user_cond(user_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	user_list = acct_storage_g_get_users(db_conn, my_uid, user_cond);
	destroy_acct_user_cond(user_cond);

	if(!user_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(user_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((user = list_next(itr))) {
		if(user->assoc_list) {
			ListIterator itr3 =
				list_iterator_create(user->assoc_list);

			while((assoc = list_next(itr3))) {
				int curr_inx = 1;
				while((field = list_next(itr2))) {
					switch(field->type) {
					case PRINT_ACCOUNT:
						field->print_routine(
							field,
							assoc->acct,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_ADMIN:
						field->print_routine(
							field,
							acct_admin_level_str(
								user->
								admin_level),
							(curr_inx ==
							 field_count));
						break;
					case PRINT_CLUSTER:
						field->print_routine(
							field,
							assoc->cluster,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_COORDS:
						field->print_routine(
							field,
							user->coord_accts,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_DACCT:
						field->print_routine(
							field,
							user->default_acct,
							(curr_inx ==
							 field_count),
							(curr_inx ==
							 field_count));
						break;
					case PRINT_DWCKEY:
						field->print_routine(
							field,
							user->default_wckey,
							(curr_inx ==
							 field_count),
							(curr_inx ==
							 field_count));
						break;
					case PRINT_FAIRSHARE:
						field->print_routine(
							field,
							assoc->shares_raw,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_GRPCM:
						field->print_routine(
							field,
							assoc->grp_cpu_mins,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_GRPC:
						field->print_routine(
							field,
							assoc->grp_cpus,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_GRPJ:
						field->print_routine(
							field,
							assoc->grp_jobs,
							(curr_inx
							 == field_count));
						break;
					case PRINT_GRPN:
						field->print_routine(
							field,
							assoc->grp_nodes,
							(curr_inx
							 == field_count));
						break;
					case PRINT_GRPS:
						field->print_routine(
							field,
						assoc->grp_submit_jobs,
							(curr_inx
							 == field_count));
						break;
					case PRINT_GRPW:
						field->print_routine(
							field,
							assoc->grp_wall,
							(curr_inx
							 == field_count));
						break;
					case PRINT_ID:
						field->print_routine(
							field,
							assoc->id,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_MAXCM:
						field->print_routine(
							field,
							assoc->
							max_cpu_mins_pj,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_MAXC:
						field->print_routine(
							field,
							assoc->max_cpus_pj,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_MAXJ:
						field->print_routine(
							field,
							assoc->max_jobs,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_MAXN:
						field->print_routine(
							field,
							assoc->
							max_nodes_pj,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_MAXS:
						field->print_routine(
							field,
							assoc->max_submit_jobs,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_MAXW:
						field->print_routine(
							field,
							assoc->
							max_wall_pj,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_QOS:
						if(!g_qos_list) {
							g_qos_list =
								acct_storage_g_get_qos(
									db_conn,
									my_uid,
									NULL);
						}
						field->print_routine(
							field,
							g_qos_list,
							assoc->qos_list,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_QOS_RAW:
						field->print_routine(
							field,
							NULL,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_PID:
						field->print_routine(
							field,
							assoc->parent_id,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_PNAME:
						field->print_routine(
							field,
							assoc->parent_acct,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_PART:
						field->print_routine(
							field,
							assoc->partition,
							(curr_inx ==
							 field_count));
						break;
					case PRINT_USER:
						field->print_routine(
							field,
							user->name,
							(curr_inx ==
							 field_count));
						break;
					default:
						field->print_routine(
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
			list_iterator_destroy(itr3);
		} else {
			int curr_inx = 1;
			while((field = list_next(itr2))) {
				switch(field->type) {
					/* All the association stuff */
				case PRINT_ACCOUNT:
				case PRINT_CLUSTER:
				case PRINT_FAIRSHARE:
				case PRINT_GRPCM:
				case PRINT_GRPC:
				case PRINT_GRPJ:
				case PRINT_GRPN:
				case PRINT_GRPS:
				case PRINT_GRPW:
				case PRINT_ID:
				case PRINT_MAXCM:
				case PRINT_MAXC:
				case PRINT_MAXJ:
				case PRINT_MAXN:
				case PRINT_MAXS:
				case PRINT_MAXW:
				case PRINT_QOS_RAW:
				case PRINT_PID:
				case PRINT_PNAME:
				case PRINT_PART:
					field->print_routine(
						field,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_QOS:
					field->print_routine(
						field, NULL,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_ADMIN:
					field->print_routine(
						field,
						acct_admin_level_str(
							user->admin_level),
						(curr_inx == field_count));
					break;
				case PRINT_COORDS:
					field->print_routine(
						field,
						user->coord_accts,
						(curr_inx == field_count));
					break;
				case PRINT_DACCT:
					field->print_routine(
						field,
						user->default_acct,
						(curr_inx == field_count));
					break;
				case PRINT_DWCKEY:
					field->print_routine(
						field,
						user->default_wckey,
						(curr_inx == field_count));
					break;
				case PRINT_USER:
					field->print_routine(
						field,
						user->name,
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
	list_destroy(user_list);
	list_destroy(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	acct_user_rec_t *user = xmalloc(sizeof(acct_user_rec_t));
	acct_association_rec_t *assoc = xmalloc(sizeof(acct_association_rec_t));
	int i=0;
	int cond_set = 0, prev_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	init_acct_association_rec(assoc);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i++;
			prev_set = _set_cond(&i, argc, argv, user_cond, NULL);
			cond_set = MAX(cond_set, prev_set);
		} else if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i++;
			prev_set = _set_rec(&i, argc, argv, user, assoc);
			rec_set = MAX(rec_set, prev_set);
		} else {
			prev_set = _set_cond(&i, argc, argv, user_cond, NULL);
			cond_set = MAX(cond_set, prev_set);
		}
	}

	if(exit_code) {
		destroy_acct_user_cond(user_cond);
		destroy_acct_user_rec(user);
		destroy_acct_association_rec(assoc);
		return SLURM_ERROR;
	} else if(!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		destroy_acct_user_cond(user_cond);
		destroy_acct_user_rec(user);
		destroy_acct_association_rec(assoc);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_user_cond(user_cond);
			destroy_acct_user_rec(user);
			destroy_acct_association_rec(assoc);
			return SLURM_SUCCESS;
		}
	}

	notice_thread_init();
	if(rec_set == 3 || rec_set == 1) { // process the account changes
		if(cond_set == 2) {
			rc = SLURM_ERROR;
			exit_code=1;
			fprintf(stderr,
				" There was a problem with your "
				"'where' options.\n");
			goto assoc_start;
		}

		if(user_cond->assoc_cond
		   && user_cond->assoc_cond->acct_list
		   && list_count(user_cond->assoc_cond->acct_list)) {
			notice_thread_fini();
			if(commit_check(
				   " You specified Accounts in your "
				   "request.  Did you mean "
				   "DefaultAccounts?\n")) {
				if(!user_cond->def_acct_list)
					user_cond->def_acct_list =
						list_create(slurm_destroy_char);
				list_transfer(user_cond->def_acct_list,
					      user_cond->assoc_cond->acct_list);
			}
			notice_thread_init();
		}

		ret_list = acct_storage_g_modify_users(
			db_conn, my_uid, user_cond, user);
		if(ret_list && list_count(ret_list)) {
			char *object = NULL;
			List regret_list = NULL;
			ListIterator itr = list_iterator_create(ret_list);

			while((object = list_next(itr))) {
				/* We have to check here for the user names to
				 * make sure the user has an association with
				 * the new default account.  We have to wait
				 * until we get the ret_list of names since
				 * names are required to change a user since
				 * you can specfy a user by something else
				 * like default_account or something.  If the
				 * user doesn't have the account make
				 * note of it.
				 */
				if(user->default_acct &&
				   !_check_user_has_acct(
					   object, user->default_acct)) {
					if(!regret_list)
						regret_list = list_create(NULL);
					list_append(regret_list, object);
					continue;
				}
			}
			if(regret_list) {
				list_iterator_destroy(itr);
				itr = list_iterator_create(regret_list);
				printf(" Can't modify because these users "
				       "aren't associated with new "
				       "default account '%s'...\n",
				       user->default_acct);
				while((object = list_next(itr))) {
					printf("  %s\n", object);
				}
				list_iterator_destroy(itr);
				exit_code=1;
				rc = SLURM_ERROR;
				list_destroy(regret_list);
			} else {
				list_iterator_reset(itr);
				printf(" Modified users...\n");
				while((object = list_next(itr))) {
					printf("  %s\n", object);
				}
				list_iterator_destroy(itr);
				set = 1;
			}
		} else if(ret_list) {
			printf(" Nothing modified\n");
		} else {
			exit_code=1;
			fprintf(stderr, " Error with request\n");
			rc = SLURM_ERROR;
		}

		if(ret_list)
			list_destroy(ret_list);
	}

assoc_start:
	if(rec_set == 3 || rec_set == 2) { // process the association changes
		if(cond_set == 1
		   && !list_count(user_cond->assoc_cond->user_list)) {
			rc = SLURM_ERROR;
			exit_code=1;
			fprintf(stderr,
				" There was a problem with your "
				"'where' options.\n");
			goto assoc_end;
		}

		ret_list = acct_storage_g_modify_associations(
			db_conn, my_uid, user_cond->assoc_cond, assoc);

		if(ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified account associations...\n");
			while((object = list_next(itr))) {
				printf("  %s\n", object);
			}
			list_iterator_destroy(itr);
			set = 1;
		} else if(ret_list) {
			printf(" Nothing modified\n");
		} else {
			exit_code=1;
			fprintf(stderr, " Error with request\n");
			rc = SLURM_ERROR;
		}

		if(ret_list)
			list_destroy(ret_list);
	}
assoc_end:

	notice_thread_fini();
	if(set) {
		if(commit_check("Would you like to commit changes?"))
			acct_storage_g_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}

	destroy_acct_user_cond(user_cond);
	destroy_acct_user_rec(user);
	destroy_acct_association_rec(assoc);

	return rc;
}

extern int sacctmgr_delete_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	int i=0;
	List ret_list = NULL;
	int cond_set = 0, prev_set = 0;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, user_cond, NULL);
		cond_set = MAX(cond_set, prev_set);
	}

	if(!cond_set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}

	if(exit_code) {
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}

	notice_thread_init();
	if(cond_set == 1) {
		ret_list = acct_storage_g_remove_users(
			db_conn, my_uid, user_cond);
	} else if(cond_set == 2 || cond_set == 3) {
		ret_list = acct_storage_g_remove_associations(
			db_conn, my_uid, user_cond->assoc_cond);
	}
	notice_thread_fini();

	destroy_acct_user_cond(user_cond);

	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		if(cond_set == 1) {
			printf(" Deleting users...\n");
		} else if(cond_set == 2 || cond_set == 3) {
			printf(" Deleting user associations...\n");
		}
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else if(ret_list) {
		printf(" Nothing deleted\n");
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request\n");
		rc = SLURM_ERROR;
	}


	if(ret_list)
		list_destroy(ret_list);

	return rc;
}

extern int sacctmgr_delete_coord(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0, set=0;
	int cond_set = 0, prev_set = 0;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	char *name = NULL;
	char *user_str = NULL;
	char *acct_str = NULL;
	ListIterator itr = NULL;
	List ret_list = NULL;


	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		prev_set = _set_cond(&i, argc, argv, user_cond, NULL);
		cond_set = MAX(cond_set, prev_set);
	}

	if(exit_code) {
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	} else if(!cond_set) {
		exit_code=1;
		fprintf(stderr, " You need to specify a user list "
		       "or account list here.\n");
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}
	if((_check_coord_request(user_cond, false) == SLURM_ERROR)
	   || exit_code) {
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}

	if(user_cond->assoc_cond->user_list) {
		itr = list_iterator_create(user_cond->assoc_cond->user_list);
		while((name = list_next(itr))) {
			xstrfmtcat(user_str, "  %s\n", name);

		}
		list_iterator_destroy(itr);
	}

	if(user_cond->assoc_cond->acct_list) {
		itr = list_iterator_create(user_cond->assoc_cond->acct_list);
		while((name = list_next(itr))) {
			xstrfmtcat(acct_str, "  %s\n", name);

		}
		list_iterator_destroy(itr);
	}

	if(!user_str && !acct_str) {
		exit_code=1;
		fprintf(stderr, " You need to specify a user list "
		       "or an account list here.\n");
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}
	/* FIX ME: This list should be recieved from the slurmdbd not
	 * just assumed.  Right now it doesn't do it correctly though.
	 * This is why we are doing it this way.
	 */
	if(user_str) {
		printf(" Removing Coordinators with user name\n%s", user_str);
		if(acct_str)
			printf(" From Account(s)\n%s", acct_str);
		else
			printf(" From all accounts\n");
	} else
		printf(" Removing all users from Accounts\n%s", acct_str);

	notice_thread_init();
        ret_list = acct_storage_g_remove_coord(db_conn, my_uid,
					       user_cond->assoc_cond->acct_list,
					       user_cond);
	destroy_acct_user_cond(user_cond);


	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Removed Coordinators (sub accounts not listed)...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if(ret_list) {
		printf(" Nothing removed\n");
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request\n");
		rc = SLURM_ERROR;
	}

	if(ret_list)
		list_destroy(ret_list);
	notice_thread_fini();
	if(set) {
		if(commit_check("Would you like to commit changes?"))
			acct_storage_g_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}

	return rc;
}
