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
#include "print.h"

static int _set_cond(int *start, int argc, char *argv[],
		     acct_user_cond_t *user_cond)
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
		} else if (strncasecmp (argv[i], "WithAssoc", 4) == 0) {
			user_cond->with_assocs = 1;
		} else if(!end) {
			addto_char_list(user_cond->user_list, argv[i]);
			addto_char_list(user_cond->assoc_cond->user_list,
					argv[i]);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Account", 2) == 0) {
			addto_char_list(user_cond->assoc_cond->acct_list,
					argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "AdminLevel", 2) == 0) {
			user_cond->admin_level = 
				str_2_acct_admin_level(argv[i]+end);
			u_set = 1;			
		} else if (strncasecmp (argv[i], "Clusters", 1) == 0) {
			addto_char_list(user_cond->assoc_cond->cluster_list,
					argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "DefaultAccount", 1) == 0) {
			addto_char_list(user_cond->def_acct_list,
					argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Names", 1) == 0) {
			addto_char_list(user_cond->user_list, argv[i]+end);
			addto_char_list(user_cond->assoc_cond->user_list,
					argv[i]);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Partition", 3) == 0) {
			addto_char_list(user_cond->assoc_cond->partition_list, 
					argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "QosLevel", 1) == 0) {
			user_cond->qos = str_2_acct_qos(argv[i]+end);
			u_set = 1;
		} else {
			printf(" Unknown condition: %s\n"
			       " Use keyword 'set' to modify value\n", argv[i]);
		}		
	}	
	(*start) = i;

	if(a_set) {
		return 2;
	} else if(u_set)
		return 1;

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
			printf(" Unknown option: %s\n"
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

/* static void _print_cond(acct_user_cond_t *user_cond) */
/* { */
/* 	ListIterator itr = NULL; */
/* 	char *tmp_char = NULL; */

/* 	if(!user_cond) { */
/* 		error("no acct_user_cond_t * given"); */
/* 		return; */
/* 	} */

/* 	if(user_cond->user_list && list_count(user_cond->user_list)) { */
/* 		itr = list_iterator_create(user_cond->user_list); */
/* 		printf("  Names           = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("                 or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(user_cond->def_acct_list */
/* 	   && list_count(user_cond->def_acct_list)) { */
/* 		itr = list_iterator_create(user_cond->def_acct_list); */
/* 		printf("  Default Account = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("                 or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(user_cond->qos != ACCT_QOS_NOTSET) */
/* 		printf("  Qos        = %s\n",  */
/* 		       acct_qos_str(user_cond->qos)); */

/* 	if(user_cond->admin_level != ACCT_ADMIN_NOTSET) */
/* 		printf("  Admin Level     = %s\n",  */
/* 		       acct_admin_level_str(user_cond->admin_level)); */
/* } */

/* static void _print_rec(acct_user_rec_t *user) */
/* { */
/* 	if(!user) { */
/* 		error("no acct_user_rec_t * given"); */
/* 		return; */
/* 	} */
	
/* 	if(user->name)  */
/* 		printf("  Name            = %s\n", user->name);	 */
		
/* 	if(user->default_acct)  */
/* 		printf("  Default Account = %s\n", user->default_acct); */
		
/* 	if(user->qos != ACCT_QOS_NOTSET) */
/* 		printf("  Qos        = %s\n",  */
/* 		       acct_qos_str(user->qos)); */

/* 	if(user->admin_level != ACCT_ADMIN_NOTSET) */
/* 		printf("  Admin Level     = %s\n",  */
/* 		       acct_admin_level_str(user->admin_level)); */
/* } */

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
	acct_association_rec_t *acct_assoc = NULL;
	char *default_acct = NULL;
	acct_association_cond_t *assoc_cond = NULL;
	acct_qos_level_t qos = ACCT_QOS_NOTSET;
	acct_admin_level_t admin_level = ACCT_ADMIN_NOTSET;
	char *name = NULL, *account = NULL, *cluster = NULL, *partition = NULL;
	int partition_set = 0;
	List user_list = NULL;
	List assoc_list = NULL;
	uint32_t fairshare = -2; 
	uint32_t max_jobs = -2; 
	uint32_t max_nodes_per_job = -2;
	uint32_t max_wall_duration_per_job = -2;
	uint32_t max_cpu_secs_per_job = -2;
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
			printf(" Unknown option: %s\n", argv[i]);
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
				acct_assoc = sacctmgr_find_account_base_assoc(
					account, cluster);
				if(!acct_assoc) {
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
/* 					       acct_assoc->id, */
/* 					       acct_assoc->user, */
/* 					       acct_assoc->account, */
/* 					       acct_assoc->cluster, */
/* 					       acct_assoc->parent_account); */
				
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
						   "  U = %-9s"
						   " A = %-10s"
						   " C = %-10s"
						   " P = %-10s\n",
						   assoc->user, assoc->acct,
						   assoc->cluster,
						   assoc->partition);
				}
				list_iterator_destroy(itr_p);
				if(partition_set) 
					continue;

				if(sacctmgr_find_association(
					   name, account, cluster, NULL))
						continue;
					
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
				list_append(sacctmgr_association_list, assoc);
				xstrfmtcat(assoc_str,
					   "  U = %-9s"
					   " A = %-10s"
					   " C = %-10s\n",
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

	if(!list_count(user_list) && !list_count(assoc_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	} else if(!assoc_str) {
		printf(" Error: no associations created.\n");
		goto end_it;
	}

	if(user_str) {
		printf(" Adding User(s)\n%s", user_str);
		printf(" Settings =\n");
		printf("  Default Account = %s\n", default_acct);
		if(qos != ACCT_QOS_NOTSET)
			printf("  Qos        = %s\n", acct_qos_str(qos));
		
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
		if((int)fairshare != -2)
			printf("  Fairshare       = %u\n", fairshare);
		if((int)max_cpu_secs_per_job != -2)
			printf("  MaxCPUSecs      = %u\n",
			       max_cpu_secs_per_job);
		if((int)max_jobs != -2)
			printf("  MaxJobs         = %u\n", max_jobs);
		if((int)max_nodes_per_job != -2)
			printf("  MaxNodes        = %u\n", max_nodes_per_job);
		if((int)max_wall_duration_per_job != -2)
			printf("  MaxWall         = %u\n",
			       max_wall_duration_per_job);
	}

	notice_thread_init();
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
		notice_thread_fini();
		goto end_it;
	}
	notice_thread_fini();

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
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
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
	List user_list;
	int i=0;
	ListIterator itr = NULL;
	acct_user_rec_t *user = NULL;
	print_field_t name_field;
	print_field_t dacct_field;
	print_field_t qos_field;
	print_field_t admin_field;

	print_field_t cluster_field;
	print_field_t acct_field;
	print_field_t part_field;

	List print_fields_list; /* types are of print_field_t */
	int over= 0;

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);

	user_cond->assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	user_cond->assoc_cond->user_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->acct_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->cluster_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->partition_list = list_create(slurm_destroy_char);
	
	_set_cond(&i, argc, argv, user_cond);

	user_list = acct_storage_g_get_users(db_conn, user_cond);
	destroy_acct_user_cond(user_cond);

	if(!user_list) 
		return SLURM_ERROR;

	print_fields_list = list_create(NULL);

	name_field.name = "Name";
	name_field.len = 9;
	name_field.print_routine = print_str;
	list_append(print_fields_list, &name_field);

	dacct_field.name = "Def Acct";
	dacct_field.len = 10;
	dacct_field.print_routine = print_str;
	list_append(print_fields_list, &dacct_field);

	qos_field.name = "QOS";
	qos_field.len = 9;
	qos_field.print_routine = print_str;
	list_append(print_fields_list, &qos_field);

	admin_field.name = "Admin";
	admin_field.len = 9;
	admin_field.print_routine = print_str;
	list_append(print_fields_list, &admin_field);

	if(user_cond->with_assocs) {
		cluster_field.name = "Cluster";
		cluster_field.len = 10;
		cluster_field.print_routine = print_str;
		list_append(print_fields_list, &cluster_field);

		acct_field.name = "Account";
		acct_field.len = 10;
		acct_field.print_routine = print_str;
		list_append(print_fields_list, &acct_field);

		part_field.name = "Partition";
		part_field.len = 10;
		part_field.print_routine = print_str;
		list_append(print_fields_list, &part_field);
	}

	itr = list_iterator_create(user_list);
	print_header(print_fields_list);

	while((user = list_next(itr))) {
		over = 0;
		print_str(VALUE, &name_field, user->name);
		over += name_field.len + 1;
		print_str(VALUE, &dacct_field, user->default_acct);
		over += dacct_field.len + 1;
		print_str(VALUE, &qos_field, acct_qos_str(user->qos));
		over += qos_field.len + 1;
		print_str(VALUE, &admin_field,
			  acct_admin_level_str(user->admin_level));
		over += admin_field.len + 1;

		if(user->assoc_list) {
			acct_association_rec_t *assoc = NULL;
			ListIterator itr2 =
				list_iterator_create(user->assoc_list);
			int first = 1;

			while((assoc = list_next(itr2))) {
				if(!first)
					printf("\n%-*.*s", over, over, " ");
				
				print_str(VALUE, &cluster_field,
					  assoc->cluster);
				print_str(VALUE, &acct_field, assoc->acct);
				print_str(VALUE, &part_field, assoc->partition);
				first = 0;
			}
			list_iterator_destroy(itr2);
		}
		printf("\n");
	}

	printf("\n");

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

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);
	
	user_cond->assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	user_cond->assoc_cond->user_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->acct_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->cluster_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->partition_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->fairshare = -2; 
	user_cond->assoc_cond->max_cpu_secs_per_job = -2;
	user_cond->assoc_cond->max_jobs = -2; 
	user_cond->assoc_cond->max_nodes_per_job = -2;
	user_cond->assoc_cond->max_wall_duration_per_job = -2;
	
	assoc->fairshare = -2; 
	assoc->max_cpu_secs_per_job = -2;
	assoc->max_jobs = -2; 
	assoc->max_nodes_per_job = -2;
	assoc->max_wall_duration_per_job = -2;

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			cond_set = _set_cond(&i, argc, argv, user_cond);
			      
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			rec_set = _set_rec(&i, argc, argv, user, assoc);
		} else {
			cond_set = _set_cond(&i, argc, argv, user_cond);
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
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
			goto assoc_start;
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
			list_destroy(ret_list);
			set = 1;
		} else if(ret_list) {
			printf(" Nothing modified\n");
		} else {
			printf(" Error with request\n");
			rc = SLURM_ERROR;
		}

		if(ret_list)
			list_destroy(ret_list);
	}

assoc_start:
	if(rec_set == 3 || rec_set == 2) { // process the association changes
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
			printf(" Error with request\n");
			rc = SLURM_ERROR;
		}

		if(ret_list)
			list_destroy(ret_list);
	}

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

	user_cond->user_list = list_create(slurm_destroy_char);
	user_cond->def_acct_list = list_create(slurm_destroy_char);
	
	user_cond->assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	user_cond->assoc_cond->user_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->acct_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->cluster_list = list_create(slurm_destroy_char);
	user_cond->assoc_cond->partition_list = list_create(slurm_destroy_char);

	if(!(set = _set_cond(&i, argc, argv, user_cond))) {
		printf(" No conditions given to remove, not executing.\n");
		destroy_acct_user_cond(user_cond);
		return SLURM_ERROR;
	}

	notice_thread_init();
	if(set == 1) {
		ret_list = acct_storage_g_remove_users(
			db_conn, my_uid, user_cond);		
	} else if(set == 2) {
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
		} else if(set == 2) {
			printf(" Deleting user associations...\n");
		}
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
			_remove_existing_users(ret_list);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else if(ret_list) {
		printf(" Nothing deleted\n");
	} else {
		printf(" Error with request\n");
		rc = SLURM_ERROR;
	} 
	

	if(ret_list)
		list_destroy(ret_list);

	return rc;
}
