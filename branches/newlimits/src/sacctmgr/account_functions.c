/*****************************************************************************\
 *  account_functions.c - functions dealing with accounts in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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
		     acct_account_cond_t *acct_cond,
		     List format_list)
{
	int i;
	int a_set = 0;
	int u_set = 0;
	int end = 0;
	List qos_list = NULL;
	acct_association_cond_t *assoc_cond = NULL;
		     
	if(!acct_cond) {
		exit_code=1;
		fprintf(stderr, "No acct_cond given");
		return -1;
	}

	if(!acct_cond->assoc_cond) {
		acct_cond->assoc_cond = 
			xmalloc(sizeof(acct_association_cond_t));
	}

	assoc_cond = acct_cond->assoc_cond;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!strncasecmp (argv[i], "Set", 3)) {
			i--;
			break;
		} else if (!end && 
			   !strncasecmp (argv[i], "WithAssoc", 5)) {
			acct_cond->with_assocs = 1;
		} else if (!end && 
			   !strncasecmp (argv[i], "WithCoordinators", 5)) {
			acct_cond->with_coords = 1;
		} else if (!end && 
			   !strncasecmp (argv[i], "WithSubAccounts", 5)) {
			assoc_cond->with_sub_accts = 1;
		} else if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Names", 1)
			  || !strncasecmp (argv[i], "Accouts", 1)) {
			if(!assoc_cond->acct_list) {
				assoc_cond->acct_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   assoc_cond->acct_list,
				   argv[i]+end)) 
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Clusters", 1)) {
			if(!assoc_cond->cluster_list) {
				assoc_cond->cluster_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   assoc_cond->cluster_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "Descriptions", 1)) {
			if(!acct_cond->description_list) {
				acct_cond->description_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(acct_cond->description_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Format", 1)) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "FairShare", 1)) {
			if(!assoc_cond->fairshare_list)
				assoc_cond->fairshare_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->fairshare_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCPUMins", 7)) {
			if(!assoc_cond->grp_cpu_mins_list)
				assoc_cond->grp_cpu_mins_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_cpu_mins_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCpus", 7)) {
			if(!assoc_cond->grp_cpus_list)
				assoc_cond->grp_cpus_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_cpus_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpJobs", 4)) {
			if(!assoc_cond->grp_jobs_list)
				assoc_cond->grp_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_jobs_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpNodes", 4)) {
			if(!assoc_cond->grp_nodes_list)
				assoc_cond->grp_nodes_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(assoc_cond->grp_nodes_list,
					argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpSubmitJobs", 4)) {
			if(!assoc_cond->grp_submit_jobs_list)
				assoc_cond->grp_submit_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->grp_submit_jobs_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpWall", 4)) {
			if(!assoc_cond->grp_wall_list)
				assoc_cond->grp_wall_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->grp_wall_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCPUMins", 7)) {
			if(!assoc_cond->max_cpu_mins_pj_list)
				assoc_cond->max_cpu_mins_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_cpu_mins_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCpus", 7)) {
			if(!assoc_cond->max_cpus_pj_list)
				assoc_cond->max_cpus_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_cpus_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs", 4)) {
			if(!assoc_cond->max_jobs_list)
				assoc_cond->max_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_jobs_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodes", 4)) {
			if(!assoc_cond->max_nodes_pj_list)
				assoc_cond->max_nodes_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_nodes_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxSubmitJobs", 4)) {
			if(!assoc_cond->max_submit_jobs_list)
				assoc_cond->max_submit_jobs_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_submit_jobs_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWall", 4)) {
			if(!assoc_cond->max_wall_pj_list)
				assoc_cond->max_wall_pj_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(
				   assoc_cond->max_wall_pj_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "Organizations", 1)) {
			if(!acct_cond->organization_list) {
				acct_cond->organization_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(acct_cond->organization_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Parent", 1)) {
			if(!assoc_cond->parent_acct_list) {
				assoc_cond->parent_acct_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(assoc_cond->parent_acct_list,
						 argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "QosLevel", 1)) {
			int option = 0;
			if(!assoc_cond->qos_list) {
				assoc_cond->qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}
			
			if(addto_qos_char_list(assoc_cond->qos_list, qos_list,
					       argv[i]+end, option))
				a_set = 1;
			else
				exit_code = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
			       " Use keyword 'set' to modify "
			       "SLURM_PRINT_VALUE\n", argv[i]);
		}
	}

	if(qos_list)
		list_destroy(qos_list);

	(*start) = i;

	if(u_set && a_set)
		return 3;
	else if(a_set)
		return 2;
	else if(u_set)
		return 1;

	return 0;
}

static int _set_rec(int *start, int argc, char *argv[],
		    List acct_list,
		    List cluster_list,
		    acct_account_rec_t *acct,
		    acct_association_rec_t *assoc)
{
	int i, mins;
	int u_set = 0;
	int a_set = 0;
	int end = 0;
	List qos_list = NULL;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!strncasecmp (argv[i], "Where", 5)) {
			i--;
			break;
		} else if(!end && !strncasecmp(argv[i], "set", 3)) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Account", 1)
			  || !strncasecmp (argv[i], "Names", 1)) {
			if(acct_list) 
				slurm_addto_char_list(acct_list, argv[i]+end);
				
		} else if (!strncasecmp (argv[i], "Cluster", 1)) {
			if(cluster_list)
				slurm_addto_char_list(cluster_list,
						      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Description", 1)) {
			acct->description =  strip_quotes(argv[i]+end, NULL);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "FairShare", 1)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->fairshare, 
				     "FairShare") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCPUMins", 7)) {
			if(!assoc)
				continue;
			if (get_uint64(argv[i]+end, 
				       &assoc->grp_cpu_mins, 
				       "GrpCPUMins") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpCpus", 7)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_cpus,
			    "GrpCpus") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpJobs", 4)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_jobs,
			    "GrpJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpNodes", 4)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_nodes,
			    "GrpNodes") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpSubmitJobs", 4)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->grp_submit_jobs,
			    "GrpSubmitJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "GrpWall", 4)) {
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
		} else if (!strncasecmp (argv[i], "MaxCPUMins", 7)) {
			if(!assoc)
				continue;
			if (get_uint64(argv[i]+end, 
				       &assoc->max_cpu_mins_pj, 
				       "MaxCPUMins") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCpus", 7)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->max_cpus_pj,
			    "MaxCpus") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs", 4)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->max_jobs,
			    "MaxJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodes", 4)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, 
			    &assoc->max_nodes_pj,
			    "MaxNodes") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxSubmitJobs", 4)) {
			if(!assoc)
				continue;
			if (get_uint(argv[i]+end, &assoc->max_submit_jobs,
			    "MaxSubmitJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWall", 4)) {
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
		} else if (!strncasecmp (argv[i], "Organization", 1)) {
			acct->organization = strip_quotes(argv[i]+end, NULL);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "Parent", 1)) {
			if(!assoc)
				continue;
			assoc->parent_acct = strip_quotes(argv[i]+end, NULL);
			a_set = 1;
		} else if (!strncasecmp (argv[i], "QosLevel", 1)) {
			int option = 0;
			if(!assoc)
				continue;
			if(!assoc->qos_list) 
				assoc->qos_list = 
					list_create(slurm_destroy_char);
						
			if(!qos_list) 
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
						
			if(end > 2 && argv[i][end-1] == '='
			   && (argv[i][end-2] == '+' 
			       || argv[i][end-2] == '-'))
				option = (int)argv[i][end-2];

			if(addto_qos_char_list(assoc->qos_list,
					       qos_list, argv[i]+end, option))
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

	if(qos_list)
		list_destroy(qos_list);

	if(u_set && a_set)
		return 3;
	else if(a_set)
		return 2;
	else if(u_set)
		return 1;

	return 0;
}

static int _isdefault(List acct_list)
{
	int rc = 0;
	acct_user_cond_t user_cond;
	List ret_list = NULL;

	if(!acct_list || !list_count(acct_list))
		return rc;

	memset(&user_cond, 0, sizeof(acct_user_cond_t));
	user_cond.def_acct_list = acct_list;

	ret_list = acct_storage_g_get_users(db_conn, my_uid, &user_cond);
	if(ret_list && list_count(ret_list)) {
		ListIterator itr = list_iterator_create(ret_list);
		acct_user_rec_t *user = NULL;
		fprintf(stderr," Users listed below have these "
			"as their Default Accounts.\n");
		while((user = list_next(itr))) {
			fprintf(stderr, " User - %-10.10s Account - %s\n",
				user->name, user->default_acct);
		}
		list_iterator_destroy(itr);
		rc = 1;		
	}

	if(ret_list)
		list_destroy(ret_list);

	return rc;
}

extern int sacctmgr_add_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	ListIterator itr = NULL, itr_c = NULL;
	acct_account_rec_t *acct = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_association_cond_t assoc_cond;
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
	acct_account_rec_t *start_acct = xmalloc(sizeof(acct_account_rec_t));
	acct_association_rec_t *start_assoc =
		xmalloc(sizeof(acct_association_rec_t));
	
	init_acct_association_rec(start_assoc);

	for (i=0; i<argc; i++) 
		limit_set = _set_rec(&i, argc, argv, name_list, cluster_list,
				     start_acct, start_assoc);

	if(exit_code) 
		return SLURM_ERROR;

	if(!name_list || !list_count(name_list)) {
		list_destroy(name_list);
		list_destroy(cluster_list);
		destroy_acct_association_rec(start_assoc);
		destroy_acct_account_rec(start_acct);
		exit_code=1;
		fprintf(stderr, " Need name of account to add.\n"); 
		return SLURM_SUCCESS;
	} else {
		acct_account_cond_t account_cond;
		memset(&account_cond, 0, sizeof(acct_account_cond_t));
		memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

		assoc_cond.acct_list = name_list;
		account_cond.assoc_cond = &assoc_cond;

		local_account_list = acct_storage_g_get_accounts(
			db_conn, my_uid, &account_cond);		
	}

	if(!local_account_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting accounts from database.  "
			"Contact your admin.\n");
		list_destroy(name_list);
		list_destroy(cluster_list);
		destroy_acct_association_rec(start_assoc);
		destroy_acct_account_rec(start_acct);
		return SLURM_ERROR;
	}

	if(!start_assoc->parent_acct)
		start_assoc->parent_acct = xstrdup("root");

	if(!cluster_list || !list_count(cluster_list)) {
		List tmp_list =
			acct_storage_g_get_clusters(db_conn, my_uid, NULL);
		if(!tmp_list) {
			exit_code=1;
			fprintf(stderr, 
				" Problem getting clusters from database.  "
			       "Contact your admin.\n");
			list_destroy(name_list);
			list_destroy(cluster_list);
			destroy_acct_association_rec(start_assoc);
			destroy_acct_account_rec(start_acct);
			list_destroy(local_account_list);
			return SLURM_ERROR;
		}
		
		if(!list_count(tmp_list)) {
			exit_code=1;
			fprintf(stderr, 
				"  Can't add accounts, no cluster "
				"defined yet.\n"
				" Please contact your administrator.\n");
			list_destroy(name_list);
			list_destroy(cluster_list);
			destroy_acct_association_rec(start_assoc);
			destroy_acct_account_rec(start_acct);
			list_destroy(local_account_list);
			return SLURM_ERROR; 
		}
		list_destroy(cluster_list);
		cluster_list = tmp_list;
	} else {
		List temp_list = NULL;
		acct_cluster_cond_t cluster_cond;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		cluster_cond.cluster_list = cluster_list;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		
		itr_c = list_iterator_create(cluster_list);
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

		if(!list_count(cluster_list)) {
			destroy_acct_association_rec(start_assoc);
			destroy_acct_account_rec(start_acct);
			list_destroy(local_account_list);
			return SLURM_ERROR;
		}
	}

		
	acct_list = list_create(destroy_acct_account_rec);
	assoc_list = list_create(destroy_acct_association_rec);
	
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

	assoc_cond.acct_list = list_create(NULL);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) 
		list_append(assoc_cond.acct_list, name);
	list_iterator_destroy(itr);
	list_append(assoc_cond.acct_list, start_assoc->parent_acct);

	assoc_cond.cluster_list = cluster_list;
	local_assoc_list = acct_storage_g_get_associations(
		db_conn, my_uid, &assoc_cond);	
	list_destroy(assoc_cond.acct_list);
	if(!local_assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting associations from database.  "
		       "Contact your admin.\n");
		list_destroy(name_list);
		list_destroy(cluster_list);
		destroy_acct_association_rec(start_assoc);
		destroy_acct_account_rec(start_acct);
		list_destroy(local_account_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		acct = NULL;
		if(!sacctmgr_find_account_from_list(local_account_list, name)) {
			acct = xmalloc(sizeof(acct_account_rec_t));
			acct->assoc_list = list_create(NULL);	
			acct->name = xstrdup(name);
			if(start_acct->description) 
				acct->description =
					xstrdup(start_acct->description);
			else
				acct->description = xstrdup(name);

			if(start_acct->organization)
				acct->organization = 
					xstrdup(start_acct->organization);
			else if(strcmp(start_assoc->parent_acct, "root"))
				acct->organization =
					xstrdup(start_assoc->parent_acct);
			else
				acct->organization = xstrdup(name);

			xstrfmtcat(acct_str, "  %s\n", name);
			list_append(acct_list, acct);
		}

		itr_c = list_iterator_create(cluster_list);
		while((cluster = list_next(itr_c))) {
			if(sacctmgr_find_account_base_assoc_from_list(
				   local_assoc_list, name, cluster)) {
				//printf(" already have this assoc\n");
				continue;
			}
			if(!sacctmgr_find_account_base_assoc_from_list(
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

			assoc = xmalloc(sizeof(acct_association_rec_t));
			init_acct_association_rec(assoc);
			assoc->acct = xstrdup(name);
			assoc->cluster = xstrdup(cluster);
			assoc->parent_acct = xstrdup(start_assoc->parent_acct);
			assoc->fairshare = start_assoc->fairshare;

			assoc->grp_cpu_mins = start_assoc->grp_cpu_mins;
			assoc->grp_cpus = start_assoc->grp_cpus;
			assoc->grp_jobs = start_assoc->grp_jobs;
			assoc->grp_nodes = start_assoc->grp_nodes;
			assoc->grp_submit_jobs = start_assoc->grp_submit_jobs;
			assoc->grp_wall = start_assoc->grp_wall;

			assoc->max_cpu_mins_pj = start_assoc->max_cpu_mins_pj;
			assoc->max_cpus_pj = start_assoc->max_cpus_pj;
			assoc->max_jobs = start_assoc->max_jobs;
			assoc->max_nodes_pj = start_assoc->max_nodes_pj;
			assoc->max_submit_jobs = start_assoc->max_submit_jobs;
			assoc->max_wall_pj = start_assoc->max_wall_pj;

			assoc->qos_list = copy_char_list(start_assoc->qos_list);

			if(acct) 
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
	list_destroy(local_account_list);
	list_destroy(local_assoc_list);


	if(!list_count(acct_list) && !list_count(assoc_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	} else if(!assoc_str) {
		exit_code=1;
		fprintf(stderr, " No associations created.\n");
		goto end_it;
	}

	if(acct_str) {
		printf(" Adding Account(s)\n%s", acct_str);
		printf(" Settings\n");
		if(start_acct->description)
			printf("  Description     = %s\n", 
			       start_acct->description);
		else
			printf("  Description     = %s\n", "Account Name");
			
		if(start_acct->organization)
			printf("  Organization    = %s\n",
			       start_acct->organization);
		else
			printf("  Organization    = %s\n",
			       "Parent/Account Name");

		xfree(acct_str);
	}

	if(assoc_str) {
		printf(" Associations\n%s", assoc_str);
		xfree(assoc_str);
	}

	if(limit_set) {
		printf(" Settings\n");
		sacctmgr_print_assoc_limits(start_assoc);
	}
	
	notice_thread_init();
	if(list_count(acct_list)) 
		rc = acct_storage_g_add_accounts(db_conn, my_uid, acct_list);
	

	if(rc == SLURM_SUCCESS) {
		if(list_count(assoc_list)) 
			rc = acct_storage_g_add_associations(db_conn, my_uid, 
							     assoc_list);
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding accounts\n");
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
		fprintf(stderr, 
			" error: Problem adding account associations\n");
		rc = SLURM_ERROR;
	}

end_it:
	list_destroy(name_list);
	list_destroy(cluster_list);
	list_destroy(acct_list);
	list_destroy(assoc_list);		
	
	destroy_acct_association_rec(start_assoc);
	destroy_acct_account_rec(start_acct);
	
	return rc;
}

extern int sacctmgr_list_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond =
		xmalloc(sizeof(acct_account_cond_t));
 	List acct_list;
	int i=0, set=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	acct_account_rec_t *acct = NULL;
	acct_association_rec_t *assoc = NULL;
	char *object;
	List qos_list = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_CLUSTER,
		PRINT_COORDS,
		PRINT_DESC,
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
		PRINT_ORG,
		PRINT_QOS,
		PRINT_QOS_RAW,
		PRINT_PID,
		PRINT_PNAME,
		PRINT_PART,
		PRINT_USER
	};

	acct_cond->with_assocs = with_assoc_flag;

	set = _set_cond(&i, argc, argv, acct_cond, format_list);

	if(exit_code) {
		destroy_acct_account_cond(acct_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	} else if(!list_count(format_list)) {
		slurm_addto_char_list(format_list, "A,D,O");
		if(acct_cond->with_assocs)
			slurm_addto_char_list(format_list,
					      "Cl,ParentN,U,F,GrpJ,GrpN,GrpS,"
					      "MaxJ,MaxN,MaxS,MaxW,QOS");
			
		if(acct_cond->with_coords)
			slurm_addto_char_list(format_list, "Coord");
			
	}
	
	if(!acct_cond->with_assocs && set > 1) {
		if(!commit_check("You requested options that are only vaild "
				 "when querying with the withassoc option.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			list_destroy(format_list);
			destroy_acct_account_cond(acct_cond);
			return SLURM_SUCCESS;
		}		
	}

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, 1)
		   || !strncasecmp("Name", object, 2)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object, 2)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Coordinators", object, 2)) {
			field->type = PRINT_COORDS;
			field->name = xstrdup("Coordinators");
			field->len = 20;
			field->print_routine = sacctmgr_print_coord_list;
		} else if(!strncasecmp("Description", object, 1)) {
			field->type = PRINT_DESC;
			field->name = xstrdup("Descr");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("FairShare", object, 1)) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("FairShare");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpCPUMins", object, 8)) {
			field->type = PRINT_GRPCM;
			field->name = xstrdup("GrpCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("GrpCPUs", object, 8)) {
			field->type = PRINT_GRPC;
			field->name = xstrdup("GrpCPUs");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpJobs", object, 4)) {
			field->type = PRINT_GRPJ;
			field->name = xstrdup("GrpJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpNodes", object, 4)) {
			field->type = PRINT_GRPN;
			field->name = xstrdup("GrpNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpSubmitJobs", object, 4)) {
			field->type = PRINT_GRPS;
			field->name = xstrdup("GrpSubmit");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpWall", object, 4)) {
			field->type = PRINT_GRPW;
			field->name = xstrdup("GrpWall");
			field->len = 11;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxCPUMins", object, 7)) {
			field->type = PRINT_MAXCM;
			field->name = xstrdup("MaxCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("MaxCPUs", object, 7)) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUs");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxJobs", object, 4)) {
			field->type = PRINT_MAXJ;
			field->name = xstrdup("MaxJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxNodes", object, 4)) {
			field->type = PRINT_MAXN;
			field->name = xstrdup("MaxNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxSubmitJobs", object, 4)) {
			field->type = PRINT_MAXS;
			field->name = xstrdup("MaxSubmit");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxWall", object, 4)) {
			field->type = PRINT_MAXW;
			field->name = xstrdup("MaxWall");
			field->len = 11;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("Organization", object, 1)) {
			field->type = PRINT_ORG;
			field->name = xstrdup("Org");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("QOSRAW", object, 4)) {
			field->type = PRINT_QOS_RAW;
			field->name = xstrdup("QOS_RAW");
			field->len = 10;
			field->print_routine = print_fields_char_list;
		} else if(!strncasecmp("QOS", object, 1)) {
			field->type = PRINT_QOS;
			field->name = xstrdup("QOS");
			field->len = 20;
			field->print_routine = sacctmgr_print_qos_list;
		} else if(!strncasecmp("ParentID", object, 7)) {
			field->type = PRINT_PID;
			field->name = xstrdup("Par ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("ParentName", object, 7)) {
			field->type = PRINT_PNAME;
			field->name = xstrdup("Par Name");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("User", object, 1)) {
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
		if((tmp_char = strstr(object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if(newlen > 0) 
				field->len = newlen;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if(exit_code) {
		destroy_acct_account_cond(acct_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	acct_list = acct_storage_g_get_accounts(db_conn, my_uid, acct_cond);	
	destroy_acct_account_cond(acct_cond);

	if(!acct_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(acct_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((acct = list_next(itr))) {
		if(acct->assoc_list) {
			ListIterator itr3 =
				list_iterator_create(acct->assoc_list);
			while((assoc = list_next(itr3))) {
				int curr_inx = 1;
				while((field = list_next(itr2))) {
					switch(field->type) {
					case PRINT_ACCOUNT:
						field->print_routine(
							field, acct->name,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_CLUSTER:
						field->print_routine(
							field, assoc->cluster,
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
					case PRINT_FAIRSHARE:
						field->print_routine(
							field, 
							assoc->fairshare,
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
							field, assoc->id,
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
							field, assoc->max_jobs,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_MAXN:
						field->print_routine(
							field, assoc->
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
					case PRINT_ORG:
						field->print_routine(
							field, 
							acct->organization,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_QOS:
						if(!qos_list) {
							qos_list = 
								acct_storage_g_get_qos(
									db_conn,
									my_uid,
									NULL);
						}
						field->print_routine(
							field,
							qos_list,
							assoc->qos_list,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_QOS_RAW:
						field->print_routine(
							field,
							assoc->qos_list,
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
							field, assoc->user,
							(curr_inx == 
							 field_count));
						break;
					default:
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
				case PRINT_USER:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_QOS:
					field->print_routine(
						field, NULL,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_ACCOUNT:
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
				default:
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
	list_destroy(acct_list);
	list_destroy(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond = 
		xmalloc(sizeof(acct_account_cond_t));
	acct_account_rec_t *acct = xmalloc(sizeof(acct_account_rec_t));
	acct_association_rec_t *assoc = xmalloc(sizeof(acct_association_rec_t));
	
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	init_acct_association_rec(assoc);

	for (i=0; i<argc; i++) {
		if (!strncasecmp (argv[i], "Where", 5)) {
			i++;
			cond_set = _set_cond(&i, argc, argv, acct_cond, NULL);
		} else if (!strncasecmp (argv[i], "Set", 3)) {
			i++;
			rec_set = _set_rec(&i, argc, argv, NULL, NULL, 
					   acct, assoc);
		} else {
			cond_set = _set_cond(&i, argc, argv, acct_cond, NULL);
		}
	}

	if(exit_code) {
		destroy_acct_account_cond(acct_cond);
		destroy_acct_account_rec(acct);
		destroy_acct_association_rec(assoc);
		return SLURM_ERROR;
	} else if(!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		destroy_acct_account_cond(acct_cond);
		destroy_acct_account_rec(acct);
		destroy_acct_association_rec(assoc);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_account_cond(acct_cond);
			destroy_acct_account_rec(acct);
			destroy_acct_association_rec(assoc);
			return SLURM_SUCCESS;
		}		
	}

	notice_thread_init();
	if(rec_set == 3 || rec_set == 1) { // process the account changes
		if(cond_set == 2) {
			exit_code=1;
			fprintf(stderr, 
				" There was a problem with your "
				"'where' options.\n");
			rc = SLURM_ERROR;
			goto assoc_start;
		}
		ret_list = acct_storage_g_modify_accounts(
			db_conn, my_uid, acct_cond, acct);
		if(ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified accounts...\n");
			while((object = list_next(itr))) {
				printf("  %s\n", object);
			}
			list_iterator_destroy(itr);
			set = 1;
		} else if(ret_list) {
			printf(" Nothing modified\n");
			rc = SLURM_ERROR;
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
		if(cond_set == 1 && !acct_cond->assoc_cond->acct_list) {
			rc = SLURM_ERROR;
			exit_code=1;
			fprintf(stderr, 
				" There was a problem with your "
				"'where' options.\n");
			goto assoc_end;
		}

		ret_list = acct_storage_g_modify_associations(
			db_conn, my_uid, acct_cond->assoc_cond, assoc);

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
	destroy_acct_account_cond(acct_cond);
	destroy_acct_account_rec(acct);
	destroy_acct_association_rec(assoc);	

	return rc;
}

extern int sacctmgr_delete_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond =
		xmalloc(sizeof(acct_account_cond_t));
	int i=0;
	List ret_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	
	if(!(set = _set_cond(&i, argc, argv, acct_cond, NULL))) {
		exit_code=1;
		fprintf(stderr, 
			" No conditions given to remove, not executing.\n");
		destroy_acct_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	if(exit_code) {
		destroy_acct_account_cond(acct_cond);
		return SLURM_ERROR;
	}
	/* check to see if person is trying to remove root account.  This is
	 * bad, and should not be allowed outside of deleting a cluster.
	 */
	if(acct_cond->assoc_cond
	   && acct_cond->assoc_cond->acct_list
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		char *tmp_char = NULL;
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while((tmp_char = list_next(itr))) {
			if(!strcasecmp(tmp_char, "root")) 
				break;
		}
		list_iterator_destroy(itr);
		if(tmp_char) {
			exit_code=1;
			fprintf(stderr, " You are not allowed to remove "
			       "the root account.\n"
			       " Use remove cluster instead.\n");
			destroy_acct_account_cond(acct_cond);
			return SLURM_ERROR;
		}
	}

	notice_thread_init();
	if(set == 1) {
		ret_list = acct_storage_g_remove_accounts(
			db_conn, my_uid, acct_cond);		
	} else if(set == 2 || set == 3) {
		ret_list = acct_storage_g_remove_associations(
			db_conn, my_uid, acct_cond->assoc_cond);
	}
	notice_thread_fini();
	destroy_acct_account_cond(acct_cond);
	
	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = NULL;

		/* Check to see if person is trying to remove a default
		 * account of a user.
		 */
		if(_isdefault(ret_list)) {
			exit_code=1;
			fprintf(stderr, " Please either remove accounts listed "
				"above from list and resubmit,\n"
				" or change these users default account to "
				"remove the account(s).\n"
				" Changes Discarded\n");
			list_destroy(ret_list);
			acct_storage_g_commit(db_conn, 0);
			return SLURM_ERROR;	
		}
		itr = list_iterator_create(ret_list);
		if(set == 1) {
			printf(" Deleting accounts...\n");
		} else if(set == 2 || set == 3) {
			printf(" Deleting account associations...\n");
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
