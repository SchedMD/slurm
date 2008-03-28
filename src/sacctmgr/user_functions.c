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

#include "sacctmgr.h"

static int _set_cond(int *start, int argc, char *argv[],
		     acct_user_cond_t *user_cond,
		     acct_association_cond_t *assoc_cond)
{
	int i;
	int u_set = 0;
	int a_set = 0;
	int end = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else if(!end) {
			addto_char_list(user_cond->user_list, argv[i]);
			addto_char_list(assoc_cond->user_list, argv[i]);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Account", 2) == 0) {
			addto_char_list(assoc_cond->acct_list, argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "AdminLevel", 2) == 0) {
			user_cond->admin_level = 
				str_2_acct_admin_level(argv[i]+end);
			u_set = 1;			
		} else if (strncasecmp (argv[i], "Clusters", 1) == 0) {
			addto_char_list(assoc_cond->cluster_list, argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "DefaultAccount", 1) == 0) {
			addto_char_list(user_cond->def_acct_list,
					argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Names", 1) == 0) {
			addto_char_list(user_cond->user_list, argv[i]+end);
			addto_char_list(assoc_cond->user_list, argv[i]);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Partition", 3) == 0) {
			addto_char_list(assoc_cond->partition_list, 
					argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "QosLevel", 1) == 0) {
			user_cond->qos =
				str_2_acct_qos(argv[i]+end);
			u_set = 1;
		} else {
			printf(" Unknown condition: %s", argv[i]);
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

static int _set_rec(int *start, int argc, char *argv[],
		    acct_user_rec_t *user,
		    acct_association_rec_t *association)
{
	int i;
	int u_set = 0;
	int a_set = 0;
	int end = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i--;
			break;
		} else if(!end) {
			printf(" Bad format on %s: End your option with "
			       "an '=' sign\n", argv[i]);
		} else if (strncasecmp (argv[i], "AdminLevel", 2) == 0) {
			user->admin_level = 
				str_2_acct_admin_level(argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "DefaultAccount", 1) == 0) {
			user->default_acct = xstrdup(argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Fairshare", 1) == 0) {
			association->fairshare = atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSec", 4) == 0) {
			association->max_cpu_secs_per_job =
				atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs", 4) == 0) {
			association->max_jobs = atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes", 4) == 0) {
			association->max_nodes_per_job = atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxWall", 4) == 0) {
			association->max_wall_duration_per_job =
				atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "QosLevel", 1) == 0) {
			user->qos = str_2_acct_qos(argv[i]+end);
			u_set = 1;
		} else {
			printf(" Unknown option: %s", argv[i]);
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

static void _print_cond(acct_user_cond_t *user_cond)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;

	if(!user_cond) {
		error("no acct_user_cond_t * given");
		return;
	}

	if(user_cond->user_list && list_count(user_cond->user_list)) {
		itr = list_iterator_create(user_cond->user_list);
		printf("  Names           = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("                 or %s\n", tmp_char);
		}
	}

	if(user_cond->def_acct_list
	   && list_count(user_cond->def_acct_list)) {
		itr = list_iterator_create(user_cond->def_acct_list);
		printf("  Default Account = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("                 or %s\n", tmp_char);
		}
	}

	if(user_cond->qos != ACCT_QOS_NOTSET)
		printf("  Qos        = %s\n", 
		       acct_qos_str(user_cond->qos));

	if(user_cond->admin_level != ACCT_ADMIN_NOTSET)
		printf("  Admin Level     = %s\n", 
		       acct_admin_level_str(user_cond->admin_level));
}

static void _print_rec(acct_user_rec_t *user)
{
	if(!user) {
		error("no acct_user_rec_t * given");
		return;
	}
	
	if(user->name) 
		printf("  Name            = %s\n", user->name);	
		
	if(user->default_acct) 
		printf("  Default Account = %s\n", user->default_acct);
		
	if(user->qos != ACCT_QOS_NOTSET)
		printf("  Qos        = %s\n", 
		       acct_qos_str(user->qos));

	if(user->admin_level != ACCT_ADMIN_NOTSET)
		printf("  Admin Level     = %s\n", 
		       acct_admin_level_str(user->admin_level));
}

static void _remove_existing_users(List ret_list)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;
	acct_user_rec_t *user = NULL;

	if(!ret_list) {
		error("no return list given");
		return;
	}

	itr = list_iterator_create(ret_list);
	while((tmp_char = list_next(itr))) {
		if((user = sacctmgr_find_user(tmp_char))) 
			sacctmgr_remove_from_list(sacctmgr_cluster_list, user);
	}
	list_iterator_destroy(itr);
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
	acct_association_rec_t *temp_assoc = NULL;
	char *default_acct = NULL;
	acct_association_cond_t *assoc_cond = NULL;
	acct_qos_level_t qos = ACCT_QOS_NOTSET;
	acct_admin_level_t admin_level = ACCT_ADMIN_NOTSET;
	char *name = NULL, *account = NULL, *cluster = NULL, *partition = NULL;
	int partition_set = 0;
	List user_list = NULL;
	List assoc_list = NULL;
	uint32_t fairshare = -1; 
	uint32_t max_jobs = -1; 
	uint32_t max_nodes_per_job = -1;
	uint32_t max_wall_duration_per_job = -1;
	uint32_t max_cpu_secs_per_job = -1;
	uint32_t use_fairshare = -1; 
	uint32_t use_max_jobs = -1; 
	uint32_t use_max_nodes_per_job = -1;
	uint32_t use_max_wall_duration_per_job = -1;
	uint32_t use_max_cpu_secs_per_job = -1;
	char *user_str = NULL;
	char *assoc_str = NULL;
	int limit_set = 0;
	int first = 1;
	int acct_first = 1;

	if(!list_count(sacctmgr_cluster_list)) {
		printf(" Can't add users, no cluster defined yet.\n"
		       " Please contact your administrator.\n");
		return SLURM_ERROR;
	}

	assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	assoc_cond->user_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->partition_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if(!end) {
			addto_char_list(assoc_cond->user_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "Accounts", 2) == 0) {
			addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
		} else if (strncasecmp (argv[i], "AdminLevel", 2) == 0) {
			admin_level = str_2_acct_admin_level(argv[i]+end);
		} else if (strncasecmp (argv[i], "Clusters", 1) == 0) {
			addto_char_list(assoc_cond->cluster_list,
					argv[i]+end);
		} else if (strncasecmp (argv[i], "DefaultAccount", 1) == 0) {
			default_acct = xstrdup(argv[i]+end);
			addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
		} else if (strncasecmp (argv[i], "FairShare", 1) == 0) {
			fairshare = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSecs", 4) == 0) {
			max_cpu_secs_per_job = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs", 4) == 0) {
			max_jobs = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes", 4) == 0) {
			max_nodes_per_job = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxWall", 4) == 0) {
			max_wall_duration_per_job = atoi(argv[i]+end);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "Names", 1) == 0) {
			addto_char_list(assoc_cond->user_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "Partitions", 1) == 0) {
			addto_char_list(assoc_cond->partition_list,
					argv[i]+end);
		} else if (strncasecmp (argv[i], "QosLevel", 1) == 0) {
			qos = str_2_acct_qos(argv[i]+end);
		} else {
			printf(" Unknown option: %s", argv[i]);
		}		
	}

	if(!list_count(assoc_cond->user_list)) {
		destroy_acct_association_cond(assoc_cond);
		printf(" Need name of user to add.\n"); 
		return SLURM_SUCCESS;
	}

	
	
	if(!list_count(assoc_cond->cluster_list)) {
		acct_cluster_rec_t *cluster_rec = NULL;
		itr_c = list_iterator_create(sacctmgr_cluster_list);
		while((cluster_rec = list_next(itr_c))) {
			list_append(assoc_cond->cluster_list, 
				    xstrdup(cluster_rec->name));
		}
		list_iterator_destroy(itr_c);
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
	itr = list_iterator_create(assoc_cond->user_list);
	while((name = list_next(itr))) {
		user = NULL;
		if(!sacctmgr_find_user(name)) {
			if(!default_acct) {
				printf(" Need a default account for "
				       "these users to add.\n"); 
				rc = SLURM_ERROR;
				goto no_default;
			}
			if(first) {
				if(!sacctmgr_find_account(default_acct)) {
					printf(" error: This account '%s' "
					       "doesn't exist.\n"
					       "        Contact your admin "
					       "to add this account.\n",
					       default_acct);
					continue;
				}
				first = 0;				
			}
			user = xmalloc(sizeof(acct_user_rec_t));
			user->assoc_list = list_create(NULL);
			user->name = xstrdup(name);
			user->default_acct = xstrdup(default_acct);
			user->qos = qos;
			user->admin_level = admin_level;
			xstrfmtcat(user_str, "  %s\n", name);

			list_append(user_list, user);
		}

		itr_a = list_iterator_create(assoc_cond->acct_list);
		while((account = list_next(itr_a))) {
			if(acct_first) {
				if(!sacctmgr_find_account(default_acct)) {
					printf(" error: This account '%s' "
					       "doesn't exist.\n"
					       "        Contact your admin "
					       "to add this account.\n",
					       account);
					continue;
				}
			}
			itr_c = list_iterator_create(assoc_cond->cluster_list);
			while((cluster = list_next(itr_c))) {
				acct_association_rec_t *root_assoc = 
					sacctmgr_find_root_assoc(cluster);
				if(!root_assoc) {
					printf(" error: This cluster '%s' "
					       "doesn't have a root account\n"
					       "        Something bad has "
					       "happend.  "
					       "Contact your admin.\n",
					       cluster);
					continue;
				}
				
				temp_assoc = sacctmgr_find_account_base_assoc(
					account, cluster);
				if(!temp_assoc) {
					if(acct_first)
						printf(" error: This "
						       "account '%s' "
						       "doesn't exist on "
						       "cluster %s\n"
						       "        Contact your "
						       "admin "
						       "to add this account.\n",
						       account, cluster);
					
					continue;
				}/*  else  */
/* 					printf("got %u %s %s %s %s\n", */
/* 					       temp_assoc->id, */
/* 					       temp_assoc->user, */
/* 					       temp_assoc->account, */
/* 					       temp_assoc->cluster, */
/* 					       temp_assoc->parent_account); */
				use_fairshare = -1; 
				use_max_jobs = -1; 
				use_max_nodes_per_job = -1;
				use_max_wall_duration_per_job = -1;
				use_max_cpu_secs_per_job = -1;

				if(fairshare >= 0)
					use_fairshare = fairshare;
				else if((int)root_assoc->fairshare >= 0)
					use_fairshare = root_assoc->fairshare;
				
				if(max_jobs >= 0)
					use_max_jobs = max_jobs;
				else if((int)root_assoc->max_jobs >= 0)
					use_max_jobs =root_assoc->max_jobs;
				
				if(max_nodes_per_job >= 0)
					use_max_nodes_per_job =
						max_nodes_per_job;
				else if((int)root_assoc->max_nodes_per_job >= 0)
					use_max_nodes_per_job =
						root_assoc->max_nodes_per_job;
				
				if(max_wall_duration_per_job >= 0)
					use_max_wall_duration_per_job =
						max_wall_duration_per_job;
				else if((int)root_assoc->
					max_wall_duration_per_job >= 0)
					use_max_wall_duration_per_job =
						root_assoc->
						max_wall_duration_per_job;

				if(max_cpu_secs_per_job >= 0)
					use_max_cpu_secs_per_job =
						max_cpu_secs_per_job;
				else if((int)root_assoc->
					max_cpu_secs_per_job >= 0)
					use_max_cpu_secs_per_job =
						root_assoc->
						max_cpu_secs_per_job;

				itr_p = list_iterator_create(
					assoc_cond->partition_list);
				while((partition = list_next(itr_p))) {
					partition_set = 1;
					if(sacctmgr_find_association(
						   name, account,
						   cluster, partition))
						continue;
					assoc = xmalloc(
						sizeof(acct_association_rec_t));
					assoc->user = xstrdup(name);
					assoc->acct = xstrdup(account);
					assoc->cluster = xstrdup(cluster);
					assoc->partition = xstrdup(partition);
					//assoc->parent_acct = xstrdup(account);
					assoc->fairshare = use_fairshare;
					assoc->max_jobs = use_max_jobs;
					assoc->max_nodes_per_job =
						use_max_nodes_per_job;
					assoc->max_wall_duration_per_job =
						use_max_wall_duration_per_job;
					assoc->max_cpu_secs_per_job =
						use_max_cpu_secs_per_job;
					if(user) 
						list_append(user->assoc_list,
							    assoc);
					else 
						list_append(assoc_list, assoc);
					xstrfmtcat(assoc_str,
						   "  U = %s"
						   "\tA = %s"
						   "\tC = %s"
						   "\tP = %s\n",
						   assoc->user, assoc->acct,
						   assoc->cluster,
						   assoc->partition);
				}
				list_iterator_destroy(itr_p);
				if(partition_set) 
					continue;

				if(sacctmgr_find_association(name, account,
							     cluster, NULL))
						continue;
					
				assoc = xmalloc(sizeof(acct_association_rec_t));
				assoc->user = xstrdup(name);
				assoc->acct = xstrdup(account);
				assoc->cluster = xstrdup(cluster);
				assoc->fairshare = use_fairshare;
				assoc->max_jobs = use_max_jobs;
				assoc->max_nodes_per_job = 
					use_max_nodes_per_job;
				assoc->max_wall_duration_per_job =
					use_max_wall_duration_per_job;
				assoc->max_cpu_secs_per_job =
					use_max_cpu_secs_per_job;
				if(user) 
					list_append(user->assoc_list, assoc);
				else 
					list_append(assoc_list, assoc);
				list_append(sacctmgr_association_list, assoc);
				xstrfmtcat(assoc_str,
					   "  U = %s"
					   "\tA = %s"
					   "\tC = %s\n",
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
	destroy_acct_association_cond(assoc_cond);

	if(user_str) {
		printf(" Adding User(s)\n%s", user_str);
		printf(" Settings =\n");
		printf("  Default Account = %s\n", default_acct);
		if(qos != ACCT_QOS_NOTSET)
			printf("  Qos        = %s\n", 
			       acct_qos_str(qos));
		
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
		printf(" Non Default Settings =\n");
		if((int)fairshare >= 0)
			printf("  Fairshare       = %u\n", fairshare);
		if((int)max_jobs >= 0)
			printf("  MaxJobs         = %u\n", max_jobs);
		if((int)max_nodes_per_job >= 0)
			printf("  MaxNodes        = %u\n", max_nodes_per_job);
		if((int)max_wall_duration_per_job >= 0)
			printf("  MaxWall         = %u\n",
			       max_wall_duration_per_job);
		if((int)max_cpu_secs_per_job >= 0)
			printf("  MaxCPUSecs      = %u\n",
			       max_cpu_secs_per_job);
	}

	if(!list_count(user_list) && !list_count(assoc_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	}

	if(list_count(user_list)) {
		rc = acct_storage_g_add_users(db_conn, my_uid, 
					      user_list);
	}

	if(rc == SLURM_SUCCESS) {
		if(list_count(assoc_list))
			rc = acct_storage_g_add_associations(db_conn, my_uid, 
							     assoc_list);
	} else {
		printf(" error: Problem adding users\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	if(rc == SLURM_SUCCESS) {
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
			while((user = list_pop(user_list))) {
				list_append(sacctmgr_user_list, user);
				while((assoc = list_pop(user->assoc_list))) {
					list_append(sacctmgr_association_list,
						    assoc);
				}
			}
			while((assoc = list_pop(assoc_list))) {
				list_append(sacctmgr_association_list, assoc);
			}			
		} else
			acct_storage_g_commit(db_conn, 0);
	} else {
		printf(" error: Problem adding user associations");
		rc = SLURM_ERROR;
	}

end_it:
	list_destroy(user_list);
	list_destroy(assoc_list);
	xfree(default_acct);

	return rc;
}

extern int sacctmgr_list_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	List user_list;
	int i=0;
	ListIterator itr = NULL;
	acct_user_rec_t *user = NULL;

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);

	assoc_cond->user_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->partition_list = list_create(slurm_destroy_char);
	
	_set_cond(&i, argc, argv, user_cond, assoc_cond);

	user_list = acct_storage_g_get_users(db_conn, user_cond);
	destroy_acct_user_cond(user_cond);
	destroy_acct_association_cond(assoc_cond);

	if(!user_list) 
		return SLURM_ERROR;

	itr = list_iterator_create(user_list);
	printf("%-15s %-15s %-10s\n%-15s %-15s %-10s\n",
	       "Name", "Default Account", "Qos",
	       "---------------",
	       "---------------",
	       "----------");
	
	while((user = list_next(itr))) {
		printf("%-15.15s %-15.15s %-10.10s\n",
		       user->name, user->default_acct,
		       acct_qos_str(user->qos));
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(user_list);
	return rc;
}

extern int sacctmgr_modify_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	acct_user_rec_t *user = xmalloc(sizeof(acct_user_rec_t));
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	acct_association_rec_t *assoc = xmalloc(sizeof(acct_association_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0;
	List ret_list = NULL;

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);
	
	assoc_cond->user_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->partition_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			cond_set = _set_cond(&i, argc, argv,
					     user_cond, assoc_cond);
			      
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			rec_set = _set_rec(&i, argc, argv, user, assoc);
		} else {
			cond_set = _set_cond(&i, argc, argv,
					     user_cond, assoc_cond);
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
		destroy_acct_user_cond(user_cond);
		destroy_acct_user_rec(user);
		destroy_acct_association_cond(assoc_cond);
		destroy_acct_association_rec(assoc);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_user_cond(user_cond);
			destroy_acct_user_rec(user);
			destroy_acct_association_cond(assoc_cond);
			destroy_acct_association_rec(assoc);
			return SLURM_SUCCESS;
		}		
	}

	printf(" Setting\n");
	_print_rec(user);
	printf("\n Where\n");
	_print_cond(user_cond);

	if((ret_list = acct_storage_g_modify_users(db_conn, my_uid,
						   user_cond, user))) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified users...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) 
			acct_storage_g_commit(db_conn, 1);
		else
			acct_storage_g_commit(db_conn, 0);
		list_destroy(ret_list);
	} else {
		rc = SLURM_ERROR;
	}
	destroy_acct_user_cond(user_cond);
	destroy_acct_user_rec(user);	
	destroy_acct_association_cond(assoc_cond);
	destroy_acct_association_rec(assoc);

	return rc;
}

extern int sacctmgr_delete_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	int i=0;
	List ret_list = NULL;

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);
	
	assoc_cond->user_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->partition_list = list_create(slurm_destroy_char);

	if(!_set_cond(&i, argc, argv, user_cond, assoc_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		destroy_acct_user_cond(user_cond);
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	}

	if((ret_list = acct_storage_g_remove_users(db_conn, my_uid,
						   user_cond))) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Deleting users...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
			_remove_existing_users(ret_list);
		} else
			acct_storage_g_commit(db_conn, 0);
		list_destroy(ret_list);
	} else {
		rc = SLURM_ERROR;
	}

	destroy_acct_user_cond(user_cond);
	destroy_acct_association_cond(assoc_cond);

	return rc;
}
