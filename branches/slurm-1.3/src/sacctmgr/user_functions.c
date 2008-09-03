/*****************************************************************************\
 *  user_functions.c - functions dealing with users in the accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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
#include "src/common/uid.h"

static int _set_cond(int *start, int argc, char *argv[],
		     acct_user_cond_t *user_cond,
		     List format_list)
{
	int i;
	int u_set = 0;
	int a_set = 0;
	int end = 0;
	List qos_list = NULL;

	if(!user_cond) {
		error("No user_cond given");
		return -1;
	}

	if(!user_cond->assoc_cond) {
		user_cond->assoc_cond = 
			xmalloc(sizeof(acct_association_cond_t));
		user_cond->assoc_cond->fairshare = NO_VAL;
		user_cond->assoc_cond->max_cpu_secs_per_job = NO_VAL;
		user_cond->assoc_cond->max_jobs = NO_VAL;
		user_cond->assoc_cond->max_nodes_per_job = NO_VAL;
		user_cond->assoc_cond->max_wall_duration_per_job = NO_VAL;
		/* we need this to make sure we only change users, not
		 * accounts if this list didn't exist it would change
		 * accounts.
		 */
		user_cond->assoc_cond->user_list = 
			list_create(slurm_destroy_char);
	}

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!strncasecmp (argv[i], "Set", 3)) {
			i--;
			break;
		} else if (!end && !strncasecmp (argv[i], "WithAssoc", 5)) {
			user_cond->with_assocs = 1;
		} else if (!strncasecmp (argv[i], "WithCoordinators", 5)) {
			user_cond->with_coords = 1;
		} else if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Names", 1)
			  || !strncasecmp (argv[i], "Users", 1)) {
			if(slurm_addto_char_list(
				   user_cond->assoc_cond->user_list,
				   argv[i]+end)) 
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Account", 2)) {
			if(!user_cond->assoc_cond->acct_list) {
				user_cond->assoc_cond->acct_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   user_cond->assoc_cond->acct_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "AdminLevel", 2)) {
			user_cond->admin_level = 
				str_2_acct_admin_level(argv[i]+end);
			u_set = 1;			
		} else if (!strncasecmp (argv[i], "Clusters", 1)) {
			if(!user_cond->assoc_cond->cluster_list) {
				user_cond->assoc_cond->cluster_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   user_cond->assoc_cond->cluster_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "DefaultAccount", 1)) {
			if(!user_cond->def_acct_list) {
				user_cond->def_acct_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(user_cond->def_acct_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Format", 1)) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Partition", 3)) {
			if(!user_cond->assoc_cond->partition_list) {
				user_cond->assoc_cond->partition_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   user_cond->assoc_cond->partition_list, 
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "QosLevel", 1)) {
			int option = 0;
			if(!user_cond->qos_list) {
				user_cond->qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}

			addto_qos_char_list(user_cond->qos_list, qos_list,
					    argv[i]+end, option);
			u_set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify value\n",
				argv[i]);
		}		
	}	

	if(qos_list)
		list_destroy(qos_list);

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
		    acct_association_rec_t *association)
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
		} else if(!end) {
			exit_code=1;
			fprintf(stderr, 
				" Bad format on %s: End your option with "
				"an '=' sign\n", argv[i]);
		} else if (!strncasecmp (argv[i], "AdminLevel", 2)) {
			user->admin_level = 
				str_2_acct_admin_level(argv[i]+end);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "DefaultAccount", 1)) {
			user->default_acct = strip_quotes(argv[i]+end, NULL);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "FairShare", 1)) {
			if(!association)
				continue;
			if (get_uint(argv[i]+end, &association->fairshare, 
				     "FairShare") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCPUSec", 4)) {
			if(!association)
				continue;
			if (get_uint(argv[i]+end, 
				     &association->max_cpu_secs_per_job, 
				     "MaxCPUSec") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs", 4)) {
			if(!association)
				continue;
			if (get_uint(argv[i]+end, &association->max_jobs, 
			    "MaxJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodes", 4)) {
			if(!association)
				continue;
			if (get_uint(argv[i]+end,
			    &association->max_nodes_per_job, 
			    "MaxNodes") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWall", 4)) {
			if(!association)
				continue;
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				association->max_wall_duration_per_job 
					= (uint32_t) mins;
				a_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxWall time format: %s\n", 
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "QosLevel", 1)) {
			int option = 0;
			if(!user->qos_list) {
				user->qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}

			if(end > 2 && argv[i][end-1] == '='
			   && (argv[i][end-2] == '+' 
			       || argv[i][end-2] == '-'))
				option = (int)argv[i][end-2];

			addto_qos_char_list(user->qos_list, qos_list,
					    argv[i]+end, option);
			u_set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}		
	}	
	if(qos_list)
		list_destroy(qos_list);

	(*start) = i;

	if(u_set && a_set)
		return 3;
	else if(u_set)
		return 1;
	else if(a_set)
		return 2;
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
	acct_user_rec_t *user = NULL;
	acct_association_rec_t *assoc = NULL;
	char *default_acct = NULL;
	acct_association_cond_t *assoc_cond = NULL;
	acct_association_cond_t query_assoc_cond;
	List add_qos_list = NULL;
	List qos_list = NULL;
	acct_admin_level_t admin_level = ACCT_ADMIN_NOTSET;
	char *name = NULL, *account = NULL, *cluster = NULL, *partition = NULL;
	int partition_set = 0;
	List user_list = NULL;
	List assoc_list = NULL;
	List local_assoc_list = NULL;
	List local_acct_list = NULL;
	List local_user_list = NULL;
	uint32_t fairshare = NO_VAL; 
	uint32_t max_jobs = NO_VAL; 
	uint32_t max_nodes_per_job = NO_VAL;
	uint32_t max_wall_duration_per_job = NO_VAL;
	uint32_t max_cpu_secs_per_job = NO_VAL;
	char *user_str = NULL;
	char *assoc_str = NULL;
	int limit_set = 0, mins;
	int first = 1;
	int acct_first = 1;
	
/* 	if(!list_count(sacctmgr_cluster_list)) { */
/* 		printf(" Can't add users, no cluster defined yet.\n" */
/* 		       " Please contact your administrator.\n"); */
/* 		return SLURM_ERROR; */
/* 	} */

	assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	assoc_cond->user_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->partition_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if(!end) {
			slurm_addto_char_list(assoc_cond->user_list,
					      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Accounts", 2)) {
			slurm_addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
		} else if (!strncasecmp (argv[i], "AdminLevel", 2)) {
			admin_level = str_2_acct_admin_level(argv[i]+end);
		} else if (!strncasecmp (argv[i], "Clusters", 1)) {
			slurm_addto_char_list(assoc_cond->cluster_list,
					argv[i]+end);
		} else if (!strncasecmp (argv[i], "DefaultAccount", 1)) {
			default_acct = strip_quotes(argv[i]+end, NULL);
			slurm_addto_char_list(assoc_cond->acct_list,
					default_acct);
		} else if (!strncasecmp (argv[i], "FairShare", 1)) {
			if (get_uint(argv[i]+end, &fairshare, 
			    "FairShare") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCPUSecs", 4)) {
			if (get_uint(argv[i]+end, &max_cpu_secs_per_job, 
			    "MaxCPUSecs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs", 4)) {
			if (get_uint(argv[i]+end, &max_jobs, 
			    "MaxJobs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodes", 4)) {
			if (get_uint(argv[i]+end, &max_nodes_per_job, 
			    "MaxNodes") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWall", 4)) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				max_wall_duration_per_job = (uint32_t) mins;
				limit_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxWall time format: %s\n", 
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "Names", 1)) {
			slurm_addto_char_list(assoc_cond->user_list,
					      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Partitions", 1)) {
			slurm_addto_char_list(assoc_cond->partition_list,
					argv[i]+end);
		} else if (!strncasecmp (argv[i], "QosLevel", 1)) {
			int option = 0;
			if(!add_qos_list) {
				add_qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}

			addto_qos_char_list(add_qos_list, qos_list,
					    argv[i]+end, option);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}		
	}

	if(exit_code) {
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	} else if(!list_count(assoc_cond->user_list)) {
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
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	}


	if(!list_count(assoc_cond->acct_list)) {
		destroy_acct_association_cond(assoc_cond);
		exit_code=1;
		fprintf(stderr, " Need name of acct to add user to.\n"); 
		return SLURM_ERROR;
	} else {
 		acct_account_cond_t account_cond;

		memset(&account_cond, 0, sizeof(acct_account_cond_t));
		account_cond.assoc_cond = assoc_cond;

		local_acct_list = acct_storage_g_get_accounts(
			db_conn, my_uid, &account_cond);
		
	}	

	if(!local_acct_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting accounts from database.  "
		       "Contact your admin.\n");
		list_destroy(local_user_list);
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
			destroy_acct_association_cond(assoc_cond);
			list_destroy(local_user_list);
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
			destroy_acct_association_cond(assoc_cond);
			list_destroy(local_user_list);
			list_destroy(local_acct_list);
			return SLURM_ERROR; 
		}
	}

	if(!default_acct) {
		itr_a = list_iterator_create(assoc_cond->acct_list);
		default_acct = xstrdup(list_next(itr_a));
		list_iterator_destroy(itr_a);
	}

	/* we are adding these lists to the global lists and will be
	   freed when they are */
	user_list = list_create(destroy_acct_user_rec);
	assoc_list = list_create(destroy_acct_association_rec);

	memset(&query_assoc_cond, 0, sizeof(acct_association_cond_t));
	query_assoc_cond.acct_list = assoc_cond->acct_list;
	query_assoc_cond.cluster_list = assoc_cond->cluster_list;
	local_assoc_list = acct_storage_g_get_associations(
		db_conn, my_uid, &query_assoc_cond);	
	
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
			pw_uid = uid_from_string(name);
			if(pw_uid == (uid_t) -1) {
				char *warning = xstrdup_printf(
					"There is no uid for user '%s'"
					"\nAre you sure you want to continue?",
					name);

				if(!commit_check(warning)) {
					xfree(warning);
					rc = SLURM_ERROR;
					list_flush(user_list);
					goto no_default;
				}
				xfree(warning);
			}

			user = xmalloc(sizeof(acct_user_rec_t));
			user->assoc_list = list_create(NULL);
			user->name = xstrdup(name);
			user->default_acct = xstrdup(default_acct);

			if(add_qos_list && list_count(add_qos_list)) {
				char *tmp_qos = NULL;
				ListIterator qos_itr = 
					list_iterator_create(add_qos_list);
				user->qos_list = 
					list_create(slurm_destroy_char);
				while((tmp_qos = list_next(qos_itr))) {
					list_append(user->qos_list,
						    xstrdup(tmp_qos));
				}
				list_iterator_destroy(qos_itr);
			}

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
					assoc->user = xstrdup(name);
					assoc->acct = xstrdup(account);
					assoc->cluster = xstrdup(cluster);
					assoc->partition = xstrdup(partition);
					assoc->fairshare = fairshare;
					assoc->max_jobs = max_jobs;
					assoc->max_nodes_per_job =
						max_nodes_per_job;
					assoc->max_wall_duration_per_job =
						max_wall_duration_per_job;
					assoc->max_cpu_secs_per_job =
						max_cpu_secs_per_job;
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
				assoc->user = xstrdup(name);
				assoc->acct = xstrdup(account);
				assoc->cluster = xstrdup(cluster);
				assoc->fairshare = fairshare;
				assoc->max_jobs = max_jobs;
				assoc->max_nodes_per_job = max_nodes_per_job;
				assoc->max_wall_duration_per_job =
					max_wall_duration_per_job;
				assoc->max_cpu_secs_per_job =
					max_cpu_secs_per_job;
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
	}
no_default:
	list_iterator_destroy(itr);
	list_destroy(local_user_list);
	list_destroy(local_acct_list);
	list_destroy(local_assoc_list);
	destroy_acct_association_cond(assoc_cond);

	if(!list_count(user_list) && !list_count(assoc_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	} else if(!assoc_str) {
		exit_code=1;
		fprintf(stderr, " No associations created.\n");
		goto end_it;
	}

	if(user_str) {
		printf(" Adding User(s)\n%s", user_str);
		printf(" Settings =\n");
		printf("  Default Account = %s\n", default_acct);
		if(add_qos_list) {
			char *temp_char = get_qos_complete_str(
				qos_list, add_qos_list);
			if(temp_char) {		
				printf("  Qos             = %s\n", temp_char);
				xfree(temp_char);
			}
		}
		
		if(admin_level != ACCT_ADMIN_NOTSET)
			printf("  Admin Level     = %s\n", 
			       acct_admin_level_str(admin_level));
		xfree(user_str);
	}

	if(assoc_str) {
		printf(" Associations =\n%s", assoc_str);
		xfree(assoc_str);
	}

	if(limit_set) {
		        printf(" Non Default Settings\n");
		if(fairshare == INFINITE)
			printf("  Fairshare       = NONE\n");
		else if(fairshare != NO_VAL) 
			printf("  Fairshare       = %u\n", fairshare);
		
		if(max_cpu_secs_per_job == INFINITE)
			printf("  MaxCPUSecs      = NONE\n");
		else if(max_cpu_secs_per_job != NO_VAL) 
			printf("  MaxCPUSecs      = %u\n",
			       max_cpu_secs_per_job);
		
		if(max_jobs == INFINITE) 
			printf("  MaxJobs         = NONE\n");
		else if(max_jobs != NO_VAL) 
			printf("  MaxJobs         = %u\n", max_jobs);
		
		if(max_nodes_per_job == INFINITE)
			printf("  MaxNodes        = NONE\n");
		else if(max_nodes_per_job != NO_VAL)
			printf("  MaxNodes        = %u\n", max_nodes_per_job);
		
		if(max_wall_duration_per_job == INFINITE) 
			printf("  MaxWall         = NONE\n");		
		else if(max_wall_duration_per_job != NO_VAL) {
			char time_buf[32];
			mins2time_str((time_t) max_wall_duration_per_job, 
				      time_buf, sizeof(time_buf));
			printf("  MaxWall         = %s\n", time_buf);
		}
	}

	notice_thread_init();
	if(list_count(user_list)) {
		rc = acct_storage_g_add_users(db_conn, my_uid, user_list);
	}

	if(rc == SLURM_SUCCESS) {
		if(list_count(assoc_list))
			rc = acct_storage_g_add_associations(db_conn, my_uid, 
							     assoc_list);
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
	if(add_qos_list)
		list_destroy(add_qos_list);
	list_destroy(user_list);
	list_destroy(assoc_list);
	xfree(default_acct);

	return rc;
}

extern int sacctmgr_add_coord(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	int cond_set = 0;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	char *name = NULL;
	char *user_str = NULL;
	char *acct_str = NULL;
	ListIterator itr = NULL;

	for (i=0; i<argc; i++) {
		cond_set = _set_cond(&i, argc, argv, user_cond, NULL);
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

	itr = list_iterator_create(user_cond->assoc_cond->user_list);
	while((name = list_next(itr))) {
		xstrfmtcat(user_str, "  %s\n", name);

	}
	list_iterator_destroy(itr);

	if(!user_str) {
		exit_code=1;
		fprintf(stderr, " You need to specify a user list here.\n"); 
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;		
	}
	itr = list_iterator_create(user_cond->assoc_cond->acct_list);
	while((name = list_next(itr))) {
		xstrfmtcat(acct_str, "  %s\n", name);

	}
	list_iterator_destroy(itr);
	if(!acct_str) {
		exit_code=1;
		fprintf(stderr, " You need to specify a account list here.\n"); 
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;		
	}

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
	int i=0, set=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	acct_user_rec_t *user = NULL;
	acct_association_rec_t *assoc = NULL;
	char *object;
	List qos_list = NULL;

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
		PRINT_FAIRSHARE,
		PRINT_ID,
		PRINT_MAXC,
		PRINT_MAXJ,
		PRINT_MAXN,
		PRINT_MAXW,
		PRINT_QOS,
		PRINT_QOS_RAW,
		PRINT_PID,
		PRINT_PNAME,
		PRINT_PART,
		PRINT_USER
	};

	user_cond->with_assocs = with_assoc_flag;

	set = _set_cond(&i, argc, argv, user_cond, format_list);

	if(exit_code) {
		destroy_acct_user_cond(user_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	}

	if(!list_count(format_list)) {
		slurm_addto_char_list(format_list, "U,D,Q,Ad");
		if(user_cond->with_assocs)
			slurm_addto_char_list(format_list,
					"Cl,Ac,Part,F,MaxC,MaxJ,MaxN,MaxW");
		if(user_cond->with_coords)
			slurm_addto_char_list(format_list, "Coord");
	}

	if(!user_cond->with_assocs && set > 1) {
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
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, 2)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("AdminLevel", object, 2)) {
			field->type = PRINT_ADMIN;
			field->name = xstrdup("Admin");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object, 2)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Coordinators", object, 2)) {
			field->type = PRINT_COORDS;
			field->name = xstrdup("Coord Accounts");
			field->len = 20;
			field->print_routine = sacctmgr_print_coord_list;
		} else if(!strncasecmp("Default", object, 1)) {
			field->type = PRINT_DACCT;
			field->name = xstrdup("Def Acct");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("FairShare", object, 1)) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("FairShare");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxCPUSecs", object, 4)) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUSecs");
			field->len = 11;
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
		} else if(!strncasecmp("MaxWall", object, 4)) {
			field->type = PRINT_MAXW;
			field->name = xstrdup("MaxWall");
			field->len = 11;
			field->print_routine = print_fields_time;
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
		} else if(!strncasecmp("Partition", object, 4)) {
			field->type = PRINT_PART;
			field->name = xstrdup("Partition");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("User", object, 1)
			  || !strncasecmp("Name", object, 2)) {
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
					case PRINT_FAIRSHARE:
						field->print_routine(
							field,
							assoc->fairshare,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_ID:
						field->print_routine(
							field,
							assoc->id,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_MAXC:
						field->print_routine(
							field,
							assoc->
							max_cpu_secs_per_job,
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
							max_nodes_per_job,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_MAXW:
						field->print_routine(
							field,
							assoc->
							max_wall_duration_per_job,
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
							user->qos_list,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_QOS_RAW:
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
							user->qos_list,
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
				case PRINT_ACCOUNT:
					field->print_routine(
						field, 
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
				case PRINT_CLUSTER:
					field->print_routine(
						field,
						NULL,
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
				case PRINT_FAIRSHARE:
					field->print_routine(
						field,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_ID:
					field->print_routine(
						field,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_MAXC:
					field->print_routine(
						field,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_MAXJ:
					field->print_routine(
						field, 
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_MAXN:
					field->print_routine(
						field,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_MAXW:
					field->print_routine(
						field,
						NULL,
						(curr_inx == field_count));
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
						field, qos_list,
						user->qos_list,
						(curr_inx == field_count));
					break;
				case PRINT_QOS_RAW:
					if(!qos_list) {
						qos_list = 
							acct_storage_g_get_qos(
								db_conn,
								my_uid,
								NULL);
					}
					field->print_routine(
						field, qos_list,
						user->qos_list,
						(curr_inx == field_count));
					break;
				case PRINT_PID:
					field->print_routine(
						field,
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_PART:
					field->print_routine(
						field, 
						NULL,
						(curr_inx == field_count));
					break;
				case PRINT_USER:
					field->print_routine(
						field, 
						user->name,
						(curr_inx == field_count));
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
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	assoc->fairshare = NO_VAL;
	assoc->max_cpu_secs_per_job = NO_VAL;
	assoc->max_jobs = NO_VAL;
	assoc->max_nodes_per_job = NO_VAL;
	assoc->max_wall_duration_per_job = NO_VAL;

	for (i=0; i<argc; i++) {
		if (!strncasecmp (argv[i], "Where", 5)) {
			i++;
			cond_set = _set_cond(&i, argc, argv, user_cond, NULL);
			      
		} else if (!strncasecmp (argv[i], "Set", 3)) {
			i++;
			rec_set = _set_rec(&i, argc, argv, user, assoc);
		} else {
			cond_set = _set_cond(&i, argc, argv, user_cond, NULL);
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
				list_transfer(user_cond->def_acct_list,
					      user_cond->assoc_cond->acct_list);
			}
			notice_thread_init();
		}
			
		ret_list = acct_storage_g_modify_users(
			db_conn, my_uid, user_cond, user);
		if(ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified users...\n");
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
	int set = 0;

	if(!(set = _set_cond(&i, argc, argv, user_cond, NULL))) {
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
	if(set == 1) {
		ret_list = acct_storage_g_remove_users(
			db_conn, my_uid, user_cond);		
	} else if(set == 2 || set == 3) {
		ret_list = acct_storage_g_remove_associations(
			db_conn, my_uid, user_cond->assoc_cond);
	}
	notice_thread_fini();

	destroy_acct_user_cond(user_cond);

	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		if(set == 1) {
			printf(" Deleting users...\n");
		} else if(set == 2 || set == 3) {
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
	int cond_set = 0;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	char *name = NULL;
	char *user_str = NULL;
	char *acct_str = NULL;
	ListIterator itr = NULL;
	List ret_list = NULL;


	for (i=0; i<argc; i++) {
		cond_set = _set_cond(&i, argc, argv, user_cond, NULL);
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
