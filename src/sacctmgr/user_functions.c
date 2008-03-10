/*****************************************************************************\
 *  user_functions.c - functions dealing with users in the accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
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
		     acct_user_cond_t *user_cond)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(user_cond->user_list, argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(user_cond->user_list, argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "DefaultAccount=", 15) == 0) {
			addto_char_list(user_cond->def_acct_list,
					argv[i]+15);
			set = 1;
		} else if (strncasecmp (argv[i], "DefaultAccounts=", 16) == 0) {
			addto_char_list(user_cond->def_acct_list,
					argv[i]+16);
			set = 1;
		} else if (strncasecmp (argv[i], "Expedite=", 9) == 0) {
			user_cond->expedite =
				str_2_acct_expedite(argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			user_cond->expedite =
				str_2_acct_expedite(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Admin=", 6) == 0) {
			user_cond->admin_level = 
				str_2_acct_admin_level(argv[i]+6);
			set = 1;			
		} else if (strncasecmp (argv[i], "AdminLevel=", 11) == 0) {
			user_cond->admin_level = 
				str_2_acct_admin_level(argv[i]+11);
			set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else {
			addto_char_list(user_cond->user_list, argv[i]);
			set = 1;
		}		
	}	
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    acct_user_rec_t *user)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "DefaultAccount=", 15) == 0) {
			user->default_acct = xstrdup(argv[i]+15);
			set = 1;
		} else if (strncasecmp (argv[i], "Expedite=", 9) == 0) {
			user->expedite =
				str_2_acct_expedite(argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			user->expedite =
				str_2_acct_expedite(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Admin=", 6) == 0) {
			user->admin_level = 
				str_2_acct_admin_level(argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "AdminLevel=", 11) == 0) {
			user->admin_level = 
				str_2_acct_admin_level(argv[i]+11);
			set = 1;
		} else if (strncasecmp (argv[i], "Where", 5) == 0) {
			i--;
			break;
		} else {
			printf(" error: Valid options are 'DefaultAccount=' "
			       "'ExpediteLevel=' and 'AdminLevel='\n");
		}		
	}	
	(*start) = i;

	return set;
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

	if(user_cond->expedite != ACCT_EXPEDITE_NOTSET)
		printf("  Expedite        = %s\n", 
		       acct_expedite_str(user_cond->expedite));

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
		
	if(user->expedite != ACCT_EXPEDITE_NOTSET)
		printf("  Expedite        = %s\n", 
		       acct_expedite_str(user->expedite));

	if(user->admin_level != ACCT_ADMIN_NOTSET)
		printf("  Admin Level     = %s\n", 
		       acct_admin_level_str(user->admin_level));
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
	acct_expedite_level_t expedite = ACCT_EXPEDITE_NOTSET;
	acct_admin_level_t admin_level = ACCT_ADMIN_NOTSET;
	char *name = NULL, *account = NULL, *cluster = NULL, *partition = NULL;
	int partition_set = 0;
	List user_list = NULL;
	List assoc_list = NULL;
	uint32_t fairshare = 1; 
	uint32_t max_jobs = 0; 
	uint32_t max_nodes_per_job = 0;
	uint32_t max_wall_duration_per_job = 0;
	uint32_t max_cpu_secs_per_job = 0;
	char *user_str = NULL;
	int limit_set = 0;

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
		if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(assoc_cond->user_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(assoc_cond->user_list, argv[i]+5);
		} else if (strncasecmp (argv[i], "DefaultAccount=", 15) == 0) {
			default_acct = xstrdup(argv[i]+15);
			addto_char_list(assoc_cond->acct_list,
					argv[i]+15);
		} else if (strncasecmp (argv[i], "Expedite=", 8) == 0) {
			expedite = str_2_acct_expedite(argv[i]+8);
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			expedite = str_2_acct_expedite(argv[i]+14);
		} else if (strncasecmp (argv[i], "Admin=", 5) == 0) {
			admin_level = str_2_acct_admin_level(argv[i]+5);
		} else if (strncasecmp (argv[i], "AdminLevel=", 11) == 0) {
			admin_level = str_2_acct_admin_level(argv[i]+11);
		} else if (strncasecmp (argv[i], "FairShare=", 10) == 0) {
			fairshare = atoi(argv[i]+10);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs=", 8) == 0) {
			max_jobs = atoi(argv[i]+8);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes=", 9) == 0) {
			max_nodes_per_job = atoi(argv[i]+9);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxWall=", 8) == 0) {
			max_wall_duration_per_job = atoi(argv[i]+8);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSecs=", 11) == 0) {
			max_cpu_secs_per_job = atoi(argv[i]+11);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "Account=", 8) == 0) {
			addto_char_list(assoc_cond->acct_list,
					argv[i]+8);
		} else if (strncasecmp (argv[i], "Accounts=", 9) == 0) {
			addto_char_list(assoc_cond->acct_list,
					argv[i]+9);
		} else if (strncasecmp (argv[i], "Cluster=", 8) == 0) {
			addto_char_list(assoc_cond->cluster_list,
					argv[i]+8);
		} else if (strncasecmp (argv[i], "Clusters=", 9) == 0) {
			addto_char_list(assoc_cond->cluster_list,
					argv[i]+9);
		} else if (strncasecmp (argv[i], "Partition=", 10) == 0) {
			addto_char_list(assoc_cond->partition_list,
					argv[i]+10);
		} else if (strncasecmp (argv[i], "Partitions=", 11) == 0) {
			addto_char_list(assoc_cond->partition_list,
					argv[i]+11);
		} else {
			addto_char_list(assoc_cond->user_list, argv[i]);
		}		
	}

	if(!list_count(assoc_cond->user_list)) {
		destroy_acct_association_cond(assoc_cond);
		printf(" Need name of user to add.\n"); 
		return SLURM_SUCCESS;
	} else if(!default_acct) {
		destroy_acct_association_cond(assoc_cond);
		printf(" Need a default account for these users to add.\n"); 
		return SLURM_SUCCESS;
	}

	if(!list_count(assoc_cond->cluster_list)) {
		acct_cluster_rec_t *cluster_rec = NULL;
		itr_c = list_iterator_create(sacctmgr_cluster_list);
		while((cluster_rec = list_next(itr_c))) {
			list_append(assoc_cond->cluster_list, 
				    cluster_rec->name);
		}
		list_iterator_destroy(itr_c);
	}

	/* we are adding these lists to the global lists and will be
	   freed when they are */
	user_list = list_create(NULL);
	assoc_list = list_create(NULL);
	itr = list_iterator_create(assoc_cond->user_list);
	while((name = list_next(itr))) {
		if(!sacctmgr_find_user(name)) {
			user = xmalloc(sizeof(acct_user_rec_t));
			user->name = xstrdup(name);
			user->default_acct = xstrdup(default_acct);
			user->expedite = expedite;
			user->admin_level = admin_level;
			xstrfmtcat(user_str, "  %s\n", name);

			list_append(user_list, user);
			list_append(sacctmgr_user_list, user);
		}

		itr_a = list_iterator_create(assoc_cond->acct_list);
		while((account = list_next(itr_a))) {
			itr_c = list_iterator_create(assoc_cond->cluster_list);
			while((cluster = list_next(itr_c))) {
				itr_p = list_iterator_create(
					assoc_cond->partition_list);
				temp_assoc = sacctmgr_find_account_base_assoc(
					account, cluster);
				if(!temp_assoc) {
					printf(" error: This account '%s' "
					       "doesn't exist on "
					       "cluster %s\n"
					       "        Contact your admin "
					       "to add this account.\n",
					       account, cluster);
					break;
				}/*  else  */
/* 					printf("got %u %s %s %s %s\n", */
/* 					       temp_assoc->id, */
/* 					       temp_assoc->user, */
/* 					       temp_assoc->account, */
/* 					       temp_assoc->cluster, */
/* 					       temp_assoc->parent_account); */
					

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
					assoc->parent = temp_assoc->id;
					assoc->fairshare = fairshare;
					assoc->max_jobs = max_jobs;
					assoc->max_nodes_per_job =
						max_nodes_per_job;
					assoc->max_wall_duration_per_job =
						max_wall_duration_per_job;
					assoc->max_cpu_secs_per_job =
						max_cpu_secs_per_job;
					list_append(assoc_list, assoc);
					list_append(sacctmgr_association_list,
						    assoc);
				}
				list_iterator_destroy(itr_p);
				if(partition_set) 
					continue;

				if(sacctmgr_find_association(name, account,
							     cluster, NULL))
						continue;
					
				assoc = xmalloc(
					sizeof(acct_association_rec_t));
				assoc->user = xstrdup(name);
				assoc->acct = xstrdup(account);
				assoc->cluster = xstrdup(cluster);
				assoc->parent = temp_assoc->id;
				assoc->fairshare = fairshare;
				assoc->max_jobs = max_jobs;
				assoc->max_nodes_per_job = max_nodes_per_job;
				assoc->max_wall_duration_per_job =
					max_wall_duration_per_job;
				assoc->max_cpu_secs_per_job =
					max_cpu_secs_per_job;
				list_append(assoc_list, assoc);
				list_append(sacctmgr_association_list,
					    assoc);
			}
			list_iterator_destroy(itr_c);
		}
		list_iterator_destroy(itr_a);				
	}
	list_iterator_destroy(itr);

	if(user_str) {
		printf(" Adding User(s)\n%s", user_str);
		printf(" Settings =\n");
		printf("  Default Account = %s\n", default_acct);
		if(expedite != ACCT_EXPEDITE_NOTSET)
			printf("  Expedite        = %s\n", 
			       acct_expedite_str(expedite));
		
		if(admin_level != ACCT_ADMIN_NOTSET)
			printf("  Admin Level     = %s\n", 
			       acct_admin_level_str(admin_level));
	}

	if(list_count(assoc_list))
		printf(" Associated With =\n");
	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		if(assoc->partition) 
			printf("  U = %s"
			       "\tA = %s"
			       "\tC = %s"
			       "\tP = %s\n",
			       assoc->user, assoc->acct, assoc->cluster,
			       assoc->partition);
		else 
			printf("  U = %s"
			       "\tA = %s"
			       "\tC = %s\n",
			       assoc->user, assoc->acct, assoc->cluster);
	}
	list_iterator_destroy(itr);

	if(limit_set) {
		printf(" Settings =\n");
		if(fairshare)
			printf("  Fairshare       = %u\n", fairshare);
		if(max_jobs)
			printf("  MaxJobs         = %u\n", max_jobs);
		if(max_nodes_per_job)
			printf("  MaxNodes        = %u\n", max_nodes_per_job);
		if(max_wall_duration_per_job)
			printf("  MaxWall         = %u\n",
			       max_wall_duration_per_job);
		if(max_cpu_secs_per_job)
			printf("  MaxCPUSecs      = %u\n",
			       max_cpu_secs_per_job);
	}


	if(!list_count(user_list) && !list_count(assoc_list))
		printf(" Nothing new added.\n");

	if(execute_flag) {
		if(list_count(user_list))
			rc = acct_storage_g_add_users(db_conn, 
						      user_list);
		list_destroy(user_list);
		if(list_count(assoc_list))
			rc = acct_storage_g_add_associations(db_conn, 
							     assoc_list);
		list_destroy(assoc_list);
	} else {
		sacctmgr_action_t *action = NULL;
		if(list_count(user_list)) {
			action = xmalloc(sizeof(sacctmgr_action_t));
			action->type = SACCTMGR_USER_CREATE;
			action->list = user_list;
			list_append(sacctmgr_action_list, action);
		} else 
			list_destroy(user_list);

		if(list_count(assoc_list)) {
			action = xmalloc(sizeof(sacctmgr_action_t));
			action->type = SACCTMGR_ASSOCIATION_CREATE;
			action->list = assoc_list;
			list_append(sacctmgr_action_list, action);
		} else 
			list_destroy(assoc_list);
	}

	return rc;
}

extern int sacctmgr_list_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	List user_list;
	int i=0;
	ListIterator itr = NULL;
	acct_user_rec_t *user = NULL;

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);
	
	_set_cond(&i, argc, argv, user_cond);

	user_list = acct_storage_g_get_users(db_conn, 
					     user_cond);
	destroy_acct_user_cond(user_cond);

	if(!user_list) 
		return SLURM_ERROR;

	itr = list_iterator_create(user_list);
	printf("%-15s %-15s %-10s\n%-15s %-15s %-10s\n",
	       "Name", "Default Account", "Expedite",
	       "---------------",
	       "---------------",
	       "----------");
	
	while((user = list_next(itr))) {
		printf("%-15.15s %-15.15s %-10.10s\n",
		       user->name, user->default_acct,
		       acct_expedite_str(user->expedite));
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
	int i=0;
	int cond_set = 0, rec_set = 0;

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			if(_set_cond(&i, argc, argv, user_cond))
				cond_set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			if(_set_rec(&i, argc, argv, user))
				rec_set = 1;
		} else {
			if(_set_cond(&i, argc, argv, user_cond))
				cond_set = 1;
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
		destroy_acct_user_cond(user_cond);
		destroy_acct_user_rec(user);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			return SLURM_SUCCESS;
		}		
	}

	printf(" Setting\n");
	_print_rec(user);
	printf("\n Where\n");
	_print_cond(user_cond);

	if(execute_flag) {
		rc = acct_storage_g_modify_users(db_conn, 
						 user_cond, user);
		destroy_acct_user_cond(user_cond);
		destroy_acct_user_rec(user);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_USER_MODIFY;
		action->cond = user_cond;
		action->rec = user;
		list_push(sacctmgr_action_list, action);
	}

	return rc;
}

extern int sacctmgr_delete_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	int i=0;

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);
	
	if(!_set_cond(&i, argc, argv, user_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		return SLURM_ERROR;
	}

	printf(" Deleting users where...");
	_print_cond(user_cond);

	if(execute_flag) {
		rc = acct_storage_g_remove_users(db_conn, 
						 user_cond);
		destroy_acct_user_cond(user_cond);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_USER_DELETE;
		action->cond = user_cond;
		list_push(sacctmgr_action_list, action);
	}

	return rc;
}
