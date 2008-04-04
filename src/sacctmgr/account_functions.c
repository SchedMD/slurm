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

#include "sacctmgr.h"
#include "print.h"

static int _set_cond(int *start, int argc, char *argv[],
		     acct_account_cond_t *acct_cond)
{
	int i;
	int a_set = 0;
	int u_set = 0;
	int end = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else if (strncasecmp (argv[i], "WithAssoc", 4) == 0) {
			acct_cond->with_assocs = 1;
		} else if(!end) {
			addto_char_list(acct_cond->acct_list, argv[i]);
			addto_char_list(acct_cond->assoc_cond->acct_list,
					argv[i]);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Clusters", 1) == 0) {
			addto_char_list(acct_cond->assoc_cond->cluster_list,
					argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "Descriptions", 1) == 0) {
			addto_char_list(acct_cond->description_list,
					argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Names", 1) == 0) {
			addto_char_list(acct_cond->acct_list, argv[i]+end);
			addto_char_list(acct_cond->assoc_cond->acct_list,
					argv[i]);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Organizations", 1) == 0) {
			addto_char_list(acct_cond->organization_list,
					argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Parent", 1) == 0) {
			acct_cond->assoc_cond->parent_acct =
				xstrdup(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "QosLevel", 1) == 0) {
			acct_cond->qos = str_2_acct_qos(argv[i]+end);
			u_set = 1;
		} else {
			printf(" Unknown condition: %s\n"
			       " Use keyword 'set' to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if(a_set) 
		return 2;
	else if(u_set)
		return 1;

	return 0;
}

static int _set_rec(int *start, int argc, char *argv[],
		    acct_account_rec_t *acct,
		    acct_association_rec_t *assoc)
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
		} else if (strncasecmp (argv[i], "Description", 1) == 0) {
			acct->description = xstrdup(argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Fairshare", 1) == 0) {
			assoc->fairshare = atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSec", 4) == 0) {
			assoc->max_cpu_secs_per_job =
				atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs", 4) == 0) {
			assoc->max_jobs = atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes", 4) == 0) {
			assoc->max_nodes_per_job = atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "MaxWall", 4) == 0) {
			assoc->max_wall_duration_per_job = atoi(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "Organization", 1) == 0) {
			acct->organization = xstrdup(argv[i]+end);
			u_set = 1;
		} else if (strncasecmp (argv[i], "Parent", 1) == 0) {
			assoc->parent_acct = xstrdup(argv[i]+end);
			a_set = 1;
		} else if (strncasecmp (argv[i], "QosLevel=", 1) == 0) {
			acct->qos = str_2_acct_qos(argv[i]+end);
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
	else if(a_set)
		return 2;
	else if(u_set)
		return 1;
	return 0;
}

/* static void _print_cond(acct_account_cond_t *acct_cond) */
/* { */
/* 	ListIterator itr = NULL; */
/* 	char *tmp_char = NULL; */

/* 	if(!acct_cond) { */
/* 		error("no acct_account_cond_t * given"); */
/* 		return; */
/* 	} */

/* 	if(acct_cond->acct_list && list_count(acct_cond->acct_list)) { */
/* 		itr = list_iterator_create(acct_cond->acct_list); */
/* 		printf("  Names       = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("             or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(acct_cond->description_list */
/* 	   && list_count(acct_cond->description_list)) { */
/* 		itr = list_iterator_create(acct_cond->description_list); */
/* 		printf("  Description = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("             or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(acct_cond->organization_list */
/* 	   && list_count(acct_cond->organization_list)) { */
/* 		itr = list_iterator_create(acct_cond->organization_list); */
/* 		printf("  Organization = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("             or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(acct_cond->qos != ACCT_QOS_NOTSET) */
/* 		printf("  Qos     = %s\n",  */
/* 		       acct_qos_str(acct_cond->qos)); */
/* } */

/* static void _print_rec(acct_account_rec_t *acct) */
/* { */
/* 	if(!acct) { */
/* 		error("no acct_account_rec_t * given"); */
/* 		return; */
/* 	} */
	
/* 	if(acct->name)  */
/* 		printf("  Name         = %s\n", acct->name);	 */
		
/* 	if(acct->description)  */
/* 		printf("  Description  = %s\n", acct->description); */

/* 	if(acct->organization)  */
/* 		printf("  Organization = %s\n", acct->organization); */
		
/* 	if(acct->qos != ACCT_QOS_NOTSET) */
/* 		printf("  Qos     = %s\n",  */
/* 		       acct_qos_str(acct->qos)); */

/* } */

static void _remove_existing_accounts(List ret_list)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *tmp_char = NULL;
	acct_account_rec_t *acct = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_cluster_rec_t *cluster = NULL;

	if(!ret_list) {
		error("no return list given");
		return;
	}

	itr = list_iterator_create(ret_list);
	itr2 = list_iterator_create(sacctmgr_cluster_list);

	while((tmp_char = list_next(itr))) {
		if((acct = sacctmgr_find_account(tmp_char))) 
			sacctmgr_remove_from_list(sacctmgr_account_list, acct);

		list_iterator_reset(itr2);
		while((cluster = list_next(itr2))) {
			if((assoc = sacctmgr_find_account_base_assoc(
				    tmp_char, cluster->name)))
				sacctmgr_remove_from_list(
					sacctmgr_association_list, assoc);
		}
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
}
	

extern int sacctmgr_add_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	ListIterator itr = NULL, itr_c = NULL;
	acct_account_rec_t *acct = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_association_rec_t *temp_assoc = NULL;
	List name_list = list_create(slurm_destroy_char);
	List cluster_list = list_create(slurm_destroy_char);
	char *description = NULL;
	char *organization = NULL;
	char *parent = NULL;
	char *cluster = NULL;
	char *name = NULL;
	acct_qos_level_t qos = ACCT_QOS_NOTSET;
	List acct_list = NULL;
	List assoc_list = NULL;
	uint32_t fairshare = -2; 
	uint32_t max_jobs = -2; 
	uint32_t max_nodes_per_job = -2;
	uint32_t max_wall_duration_per_job = -2;
	uint32_t max_cpu_secs_per_job = -2;
	char *acct_str = NULL;
	char *assoc_str = NULL;
	int limit_set = 0;
	
	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if(!end) {
			addto_char_list(name_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "Cluster", 1) == 0) {
			addto_char_list(cluster_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "Description", 1) == 0) {
			description = xstrdup(argv[i]+end);
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
			addto_char_list(name_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "Organization", 1) == 0) {
			organization = xstrdup(argv[i]+end);
		} else if (strncasecmp (argv[i], "Parent", 1) == 0) {
			parent = xstrdup(argv[i]+end);
		} else if (strncasecmp (argv[i], "QosLevel", 1) == 0) {
			qos = str_2_acct_qos(argv[i]+end);
		} else {
			printf(" Unknown option: %s\n", argv[i]);
		}		
	}

	if(!list_count(name_list)) {
		list_destroy(name_list);
		list_count(cluster_list);
		xfree(parent);
		xfree(description);
		xfree(organization);
		printf(" Need name of account to add.\n"); 
		return SLURM_SUCCESS;
	}

	if(!parent)
		parent = xstrdup("root");

	if(!list_count(cluster_list)) {
		acct_cluster_rec_t *cluster_rec = NULL;
		
		if(!list_count(sacctmgr_cluster_list)) {
			printf(" error: No cluster added yet.  "
			       "Do this before adding accounts.\n");
			list_destroy(name_list);
			list_count(cluster_list);
			return SLURM_ERROR;			
		}
		itr_c = list_iterator_create(sacctmgr_cluster_list);
		while((cluster_rec = list_next(itr_c))) {
			list_append(cluster_list, 
				    xstrdup(cluster_rec->name));
		}
		list_iterator_destroy(itr_c);
	} else {
		
		itr_c = list_iterator_create(cluster_list);
		while((cluster = list_next(itr_c))) {
			if(!sacctmgr_find_cluster(cluster)) {
				printf(" error: This cluster '%s' "
				       "doesn't exist.\n"
				       "        Contact your admin "
				       "to add it to accounting.\n",
				       cluster);
				list_delete_item(itr_c);
			}
		}
		if(!list_count(cluster_list)) {
			list_destroy(name_list);
			list_count(cluster_list);
			return SLURM_ERROR;
		}
	}

		
	/* we are adding these lists to the global lists and will be
	   freed when they are */
	acct_list = list_create(destroy_acct_account_rec);
	assoc_list = list_create(destroy_acct_association_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		acct = NULL;
		if(!sacctmgr_find_account(name)) {
			acct = xmalloc(sizeof(acct_account_rec_t));
			acct->assoc_list = list_create(NULL);	
			acct->name = xstrdup(name);
			if(description) 
				acct->description = xstrdup(description);
			else
				acct->description = xstrdup(name);

			if(organization)
				acct->organization = xstrdup(organization);
			else if(strcmp(parent, "root"))
				acct->organization = xstrdup(parent);
			else
				acct->organization = xstrdup(name);
				
			acct->qos = qos;
			xstrfmtcat(acct_str, "  %s\n", name);
			list_append(acct_list, acct);
		}

		itr_c = list_iterator_create(cluster_list);
		while((cluster = list_next(itr_c))) {
			if(sacctmgr_find_association(NULL, name,
						     cluster, NULL)) {
				//printf(" already have this assoc\n");
				continue;
			}
			temp_assoc = sacctmgr_find_account_base_assoc(
				parent, cluster);
			if(!temp_assoc) {
				printf(" error: Parent account '%s' "
				       "doesn't exist on "
				       "cluster %s\n"
				       "        Contact your admin "
				       "to add this account.\n",
				       parent, cluster);
				continue;
			} /* else */
/* 					printf("got %u %s %s %s %s\n", */
/* 					       temp_assoc->id, */
/* 					       temp_assoc->user, */
/* 					       temp_assoc->acct, */
/* 					       temp_assoc->cluster, */
/* 					       temp_assoc->parent_acct); */
			
			assoc = xmalloc(sizeof(acct_association_rec_t));
			assoc->acct = xstrdup(name);
			assoc->cluster = xstrdup(cluster);
			assoc->parent_acct = xstrdup(temp_assoc->acct);
			assoc->fairshare = fairshare;
			assoc->max_jobs = max_jobs;
			assoc->max_nodes_per_job = max_nodes_per_job;
			assoc->max_wall_duration_per_job =
				max_wall_duration_per_job;
			assoc->max_cpu_secs_per_job = 
				max_cpu_secs_per_job;
			if(acct) 
				list_append(acct->assoc_list, assoc);
			else 
				list_append(assoc_list, assoc);
			xstrfmtcat(assoc_str,
				   "  A = %-10s"
				   " C = %-10s\n",
				   assoc->acct,
				   assoc->cluster);		

		}
		list_iterator_destroy(itr_c);
	}

	list_iterator_destroy(itr);
	list_destroy(name_list);
	list_destroy(cluster_list);

	if(!list_count(acct_list) && !list_count(assoc_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	} else if(!assoc_str) {
		printf(" Error: no associations created.\n");
		goto end_it;
	}

	if(acct_str) {
		printf(" Adding Account(s)\n%s", acct_str);
		printf(" Settings\n");
		if(description)
			printf("  Description     = %s\n", description);
		else
			printf("  Description     = %s\n", "Account Name");
			
		if(organization)
			printf("  Organization    = %s\n", organization);
		else
			printf("  Organization    = %s\n",
			       "Parent/Account Name");

		if(qos != ACCT_QOS_NOTSET)
		   	printf("  Qos             = %s\n", acct_qos_str(qos));
		xfree(acct_str);
	}

	if(assoc_str) {
		printf(" Associations\n%s", assoc_str);
		xfree(assoc_str);
	}

	if(limit_set) {
		printf(" Settings\n");
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
	if(list_count(acct_list)) 
		rc = acct_storage_g_add_accounts(db_conn, my_uid, acct_list);
	

	if(rc == SLURM_SUCCESS) {
		if(list_count(assoc_list)) 
			rc = acct_storage_g_add_associations(db_conn, my_uid, 
							     assoc_list);
	} else {
		printf(" error: Problem adding accounts\n");
		rc = SLURM_ERROR;
		notice_thread_fini();
		goto end_it;
	}
	notice_thread_fini();
	
	if(rc == SLURM_SUCCESS) {
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
			while((acct = list_pop(acct_list))) {
				list_append(sacctmgr_account_list, acct);
				while((assoc = list_pop(acct->assoc_list))) {
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
		printf(" error: Problem adding account associations");
		rc = SLURM_ERROR;
	}

end_it:
	list_destroy(acct_list);
	list_destroy(assoc_list);
	
	xfree(parent);
	xfree(description);
	xfree(organization);

	return rc;
}

extern int sacctmgr_list_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond =
		xmalloc(sizeof(acct_account_cond_t));
 	List acct_list;
	int i=0;
	ListIterator itr = NULL;
	acct_account_rec_t *acct = NULL;
	print_field_t name_field;
	print_field_t desc_field;
	print_field_t org_field;
	print_field_t qos_field;

	print_field_t cluster_field;
	print_field_t parent_field;
	print_field_t user_field;

	List print_fields_list; /* types are of print_field_t */
	int over= 0;
	
	acct_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->description_list = list_create(slurm_destroy_char);
	acct_cond->organization_list = list_create(slurm_destroy_char);

	acct_cond->assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	acct_cond->assoc_cond->user_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->cluster_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->partition_list = list_create(slurm_destroy_char);

	_set_cond(&i, argc, argv, acct_cond);

	acct_list = acct_storage_g_get_accounts(db_conn, acct_cond);
	destroy_acct_account_cond(acct_cond);
	
	if(!acct_list) 
		return SLURM_ERROR;
	
	print_fields_list = list_create(NULL);

	name_field.name = "Name";
	name_field.len = 10;
	name_field.print_routine = print_str;
	list_append(print_fields_list, &name_field);

	desc_field.name = "Descr";
	desc_field.len = 10;
	desc_field.print_routine = print_str;
	list_append(print_fields_list, &desc_field);

	org_field.name = "Org";
	org_field.len = 10;
	org_field.print_routine = print_str;
	list_append(print_fields_list, &org_field);

	qos_field.name = "QOS";
	qos_field.len = 9;
	qos_field.print_routine = print_str;
	list_append(print_fields_list, &qos_field);

	if(acct_cond->with_assocs) {
		cluster_field.name = "Cluster";
		cluster_field.len = 10;
		cluster_field.print_routine = print_str;
		list_append(print_fields_list, &cluster_field);

		parent_field.name = "Parent";
		parent_field.len = 10;
		parent_field.print_routine = print_str;
		list_append(print_fields_list, &parent_field);

		user_field.name = "User";
		user_field.len = 10;
		user_field.print_routine = print_str;
		list_append(print_fields_list, &user_field);
	}

	itr = list_iterator_create(acct_list);
	print_header(print_fields_list);

	while((acct = list_next(itr))) {
		over = 0;
		print_str(VALUE, &name_field, acct->name);
		over += name_field.len + 1;
		print_str(VALUE, &desc_field, acct->description);
		over += desc_field.len + 1;
		print_str(VALUE, &org_field, acct->organization);
		over += org_field.len + 1;
		print_str(VALUE, &qos_field, acct_qos_str(acct->qos));
		over += qos_field.len + 1;

		if(acct->assoc_list) {
			acct_association_rec_t *assoc = NULL;
			ListIterator itr2 =
				list_iterator_create(acct->assoc_list);
			int first = 1;

			while((assoc = list_next(itr2))) {
				if(!first)
					printf("\n%-*.*s", over, over, " ");
				
				print_str(VALUE, &cluster_field,
					  assoc->cluster);
				print_str(VALUE, &parent_field,
					  assoc->parent_acct);
				print_str(VALUE, &user_field, assoc->user);
				first = 0;
			}
			list_iterator_destroy(itr2);
		}

		printf("\n");
		/*FIX ME: show assocs */
	}

	printf("\n");

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

	acct_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->description_list = list_create(slurm_destroy_char);
	acct_cond->organization_list = list_create(slurm_destroy_char);

	acct_cond->assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	acct_cond->assoc_cond->cluster_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->fairshare = -2; 
	acct_cond->assoc_cond->max_cpu_secs_per_job = -2;
	acct_cond->assoc_cond->max_jobs = -2; 
	acct_cond->assoc_cond->max_nodes_per_job = -2;
	acct_cond->assoc_cond->max_wall_duration_per_job = -2;
	
	assoc->fairshare = -2; 
	assoc->max_cpu_secs_per_job = -2;
	assoc->max_jobs = -2; 
	assoc->max_nodes_per_job = -2;
	assoc->max_wall_duration_per_job = -2;

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			cond_set = _set_cond(&i, argc, argv, acct_cond);
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			rec_set = _set_rec(&i, argc, argv, acct, assoc);
		} else {
			cond_set = _set_cond(&i, argc, argv, acct_cond);
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
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
		} else if(ret_list) {
			printf(" Nothing modified\n");
			rc = SLURM_ERROR;
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
	int set = 0;
	
	acct_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->description_list = list_create(slurm_destroy_char);
	acct_cond->organization_list = list_create(slurm_destroy_char);
	
	acct_cond->assoc_cond = xmalloc(sizeof(acct_association_cond_t));
	acct_cond->assoc_cond->user_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->cluster_list = list_create(slurm_destroy_char);
	acct_cond->assoc_cond->partition_list = list_create(slurm_destroy_char);

	if(!(set = _set_cond(&i, argc, argv, acct_cond))) {
		printf(" No conditions given to remove, not executing.\n");
		destroy_acct_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	notice_thread_init();
	if(set == 1) {
		ret_list = acct_storage_g_remove_accounts(
			db_conn, my_uid, acct_cond);		
	} else if(set == 2) {
		ret_list = acct_storage_g_remove_associations(
			db_conn, my_uid, acct_cond->assoc_cond);
	}
	notice_thread_fini();
	destroy_acct_account_cond(acct_cond);
	
	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		if(set == 1) {
			printf(" Deleting accounts...\n");
		} else if(set == 2) {
			printf(" Deleting account associations...\n");
		}
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
			_remove_existing_accounts(ret_list);
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
