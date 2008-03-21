/*****************************************************************************\
 *  account_functions.c - functions dealing with accounts in the
 *                        accounting system.
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
		     acct_account_cond_t *acct_cond,
		     acct_association_cond_t *assoc_cond)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(acct_cond->acct_list, argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(acct_cond->acct_list, argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else if (strncasecmp (argv[i], "Descriptions=", 13) == 0) {
			addto_char_list(acct_cond->description_list,
					argv[i]+13);
			set = 1;
		} else if (strncasecmp (argv[i], "Organizations=", 14) == 0) {
			addto_char_list(acct_cond->organization_list,
					argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "QosLevel=", 14) == 0) {
			acct_cond->qos =
				str_2_acct_qos(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Description=", 12) == 0) {
			addto_char_list(acct_cond->description_list,
					argv[i]+12);
			set = 1;
		} else if (strncasecmp (argv[i], "Organization=", 13) == 0) {
			addto_char_list(acct_cond->organization_list,
					argv[i]+13);
			set = 1;
		} else if (strncasecmp (argv[i], "Qos=", 8) == 0) {
			acct_cond->qos =
				str_2_acct_qos(argv[i]+8);
			set = 1;
		} else {
			addto_char_list(acct_cond->acct_list, argv[i]);
			set = 1;
		}
	}
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    acct_account_rec_t *acct,
		    acct_association_rec_t *assoc)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			acct->name = xstrdup(argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Where", 5) == 0) {
			i--;
			break;
		}else if (strncasecmp (argv[i], "QosLevel=", 14) == 0) {
			acct->qos =
				str_2_acct_qos(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Description=", 12) == 0) {
			acct->description = xstrdup(argv[i]+12);
			set = 1;
		} else if (strncasecmp (argv[i], "Organization=", 13) == 0) {
			acct->organization = xstrdup(argv[i]+13);
			set = 1;
		} else if (strncasecmp (argv[i], "Qos=", 8) == 0) {
			acct->qos =
				str_2_acct_qos(argv[i]+8);
			set = 1;
		} else {
			acct->name = xstrdup(argv[i]+5);
			set = 1;
		}
	}
	(*start) = i;

	return set;
}

static void _print_cond(acct_account_cond_t *acct_cond)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;

	if(!acct_cond) {
		error("no acct_account_cond_t * given");
		return;
	}

	if(acct_cond->acct_list && list_count(acct_cond->acct_list)) {
		itr = list_iterator_create(acct_cond->acct_list);
		printf("  Names       = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("             or %s\n", tmp_char);
		}
	}

	if(acct_cond->description_list
	   && list_count(acct_cond->description_list)) {
		itr = list_iterator_create(acct_cond->description_list);
		printf("  Description = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("             or %s\n", tmp_char);
		}
	}

	if(acct_cond->organization_list
	   && list_count(acct_cond->organization_list)) {
		itr = list_iterator_create(acct_cond->organization_list);
		printf("  Organization = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("             or %s\n", tmp_char);
		}
	}

	if(acct_cond->qos != ACCT_QOS_NOTSET)
		printf("  Qos     = %s\n", 
		       acct_qos_str(acct_cond->qos));
}

/* static void _update_existing(acct_account_cond_t *acct_cond, */
/* 			     acct_account_rec_t *new_acct, */
/* 			     acct_association_rec_t *new_assoc) */
/* { */
/* 	ListIterator itr = NULL; */
/* 	char *tmp_char = NULL; */
/* 	acct_account_rec_t *acct = NULL; */
/* 	acct_association_rec_t *assoc = NULL; */

/* 	if(!acct_cond) { */
/* 		error("no acct_account_cond_t * given"); */
/* 		return; */
/* 	} */

/* 	if(acct_cond->acct_list */
/* 	   && list_count(acct_cond->acct_list)) { */
/* 		itr = list_iterator_create(acct_cond->acct_list); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			if(!(acct = sacctmgr_find_account(tmp_char))) { */
/* 				printf(" Acct '%s' does not exist, " */
/* 				       "not removing.\n", tmp_char); */
/* 				list_remove(itr); */
/* 				continue; */
/* 			} */

/* 			if(!new_acct) { */
/* 				sacctmgr_remove_from_list(sacctmgr_account_list, */
/* 							  acct); */
/* 			} else if(new_acct->interface_node) { */
/* 				xfree(acct->interface_node); */
/* 				acct->interface_node = */
/* 					xstrdup(new_acct->interface_node); */
/* 			} */

/* 			if(!(assoc = sacctmgr_find_association( */
/* 				     NULL, "template_acct", */
/* 				     tmp_char, NULL))) { */
/* 				printf(" Can't find template account for '%s' " */
/* 				       "something is messed up.\n", tmp_char); */
/* 				continue; */
/* 			} */
/* 			if(!new_assoc) { */
/* 				sacctmgr_remove_from_list( */
/* 					sacctmgr_association_list, assoc); */
/* 				continue; */
/* 			} */

/* 			if(new_assoc->fairshare) */
/* 				assoc->fairshare = new_assoc->fairshare; */
/* 			if(new_assoc->max_jobs) */
/* 				assoc->max_jobs = new_assoc->max_jobs; */
/* 			if(new_assoc->max_nodes_per_job) */
/* 				assoc->max_nodes_per_job = */
/* 					new_assoc->max_nodes_per_job; */
/* 			if(new_assoc->max_wall_duration_per_job) */
/* 				assoc->max_wall_duration_per_job =  */
/* 					new_assoc->max_wall_duration_per_job; */
/* 			if(new_assoc->max_cpu_secs_per_job) */
/* 				assoc->max_cpu_secs_per_job =  */
/* 					new_assoc->max_cpu_secs_per_job;	 */
/* 		} */
/* 		list_iterator_destroy(itr); */
/* 	} */
/* } */

static void _print_rec(acct_account_rec_t *acct)
{
	if(!acct) {
		error("no acct_account_rec_t * given");
		return;
	}
	
	if(acct->name) 
		printf("  Name         = %s\n", acct->name);	
		
	if(acct->description) 
		printf("  Description  = %s\n", acct->description);

	if(acct->organization) 
		printf("  Organization = %s\n", acct->organization);
		
	if(acct->qos != ACCT_QOS_NOTSET)
		printf("  Qos     = %s\n", 
		       acct_qos_str(acct->qos));

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
	uint32_t fairshare = 1; 
	uint32_t max_jobs = 0; 
	uint32_t max_nodes_per_job = 0;
	uint32_t max_wall_duration_per_job = 0;
	uint32_t max_cpu_secs_per_job = 0;
	char *acct_str = NULL;
	int limit_set = 0;
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(name_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(name_list, argv[i]+5);
		} else if (strncasecmp (argv[i], "Parent=", 7) == 0) {
			parent = xstrdup(argv[i]+7);
		} else if (strncasecmp (argv[i], "Description=", 12) == 0) {
			description = xstrdup(argv[i]+12);
		} else if (strncasecmp (argv[i], "Organization=", 13) == 0) {
			organization = xstrdup(argv[i]+13);
		} else if (strncasecmp (argv[i], "Qos=", 8) == 0) {
			qos = str_2_acct_qos(argv[i]+8);
		} else if (strncasecmp (argv[i], "QosLevel=", 14) == 0) {
			qos = str_2_acct_qos(argv[i]+14);
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
		} else if (strncasecmp (argv[i], "Cluster=", 8) == 0) {
			addto_char_list(cluster_list,
					argv[i]+8);
		} else if (strncasecmp (argv[i], "Clusters=", 9) == 0) {
			addto_char_list(cluster_list,
					argv[i]+9);
		} else {
			addto_char_list(name_list, argv[i]);
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
	acct_list = list_create(NULL);
	assoc_list = list_create(NULL);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		acct = NULL;
		if(!sacctmgr_find_account(name)) {
			if(!description) {
				printf(" Need a description for "
				       "these accounts to add.\n"); 
				rc = SLURM_ERROR;
				goto end_it;
			} else if(!organization) {
				printf(" Need an organization for "
				       "these accounts to add.\n"); 
				rc = SLURM_ERROR;
				goto end_it;
			} 
			acct = xmalloc(sizeof(acct_account_rec_t));
			acct->assoc_list = list_create(NULL);	
			acct->name = xstrdup(name);
			acct->description = xstrdup(description);
			acct->organization = xstrdup(organization);
			acct->qos = qos;
			xstrfmtcat(acct_str, "  %s\n", name);
			list_append(acct_list, acct);
			list_append(sacctmgr_account_list, acct);
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
				break;
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
			list_append(sacctmgr_association_list, assoc);
			
		}
		list_iterator_destroy(itr_c);
	}
end_it:
	list_iterator_destroy(itr);
	list_destroy(name_list);
	list_destroy(cluster_list);

	if(acct_str) {
		printf(" Adding Account(s)\n%s",acct_str);
		printf(" Settings =\n");
		printf("  Description  = %s\n", description);
		printf("  Organization = %s\n", organization);
		
		if(qos != ACCT_QOS_NOTSET)
			printf("  Qos     = %s\n", 
			       acct_qos_str(qos));
		xfree(acct_str);
	}

	if(list_count(assoc_list))
		printf(" Associations =\n");
	if(acct)
		itr = list_iterator_create(acct->assoc_list);
	else
		itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		printf("  A = %s"
		       "\tC = %s\n",
		       assoc->acct, assoc->cluster);
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

	if(!list_count(acct_list) && !list_count(assoc_list))
		printf(" Nothing new added.\n");
	else
		changes_made = 1;

	if(list_count(acct_list)) {
		rc = acct_storage_g_add_accounts(db_conn, my_uid, 
						 acct_list);
		account_changes = 1;
		association_changes = 1;
	}

	list_destroy(acct_list);
	if(list_count(assoc_list)) {
		rc = acct_storage_g_add_associations(db_conn, my_uid, 
						     assoc_list);
		association_changes = 1;
	}
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
	
	acct_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->description_list = list_create(slurm_destroy_char);
	acct_cond->organization_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(acct_cond->acct_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "Descriptions=", 13) == 0) {
			addto_char_list(acct_cond->description_list,
					argv[i]+13);
		} else if (strncasecmp (argv[i], "Organizations=", 14) == 0) {
			addto_char_list(acct_cond->organization_list,
					argv[i]+14);
		} else if (strncasecmp (argv[i], "QosLevel=", 14) == 0) {
			acct_cond->qos =
				str_2_acct_qos(argv[i]+14);
		} else {
			error("Valid options are 'Names=' "
			      "'Descriptions=' 'Oranizations=' "
			      "and 'QosLevel='");
		}		
	}


	acct_list = acct_storage_g_get_accounts(db_conn, acct_cond);
	destroy_acct_account_cond(acct_cond);
	
	if(!acct_list) 
		return SLURM_ERROR;
	

	itr = list_iterator_create(acct_list);
	printf("%-15s %-15s %-15s %-10s\n%-15s %-15s %-15s %-10s\n",
	       "Name", "Description", "Organization", "Qos",
	       "---------------",
	       "---------------",
	       "---------------",
	       "----------");
	
	while((acct = list_next(itr))) {
		printf("%-15.15s %-15.15s %-15.15s %-10.10s\n",
		       acct->name, acct->description,
		       acct->organization,
		       acct_qos_str(acct->qos));
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(acct_list);
	return rc;
}

extern int sacctmgr_modify_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond = 
		xmalloc(sizeof(acct_account_cond_t));
	acct_account_rec_t *acct = xmalloc(sizeof(acct_account_rec_t));
	acct_association_rec_t *assoc = xmalloc(sizeof(acct_association_rec_t));
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));

	int i=0;
	int cond_set = 0, rec_set = 0;
	List ret_list = NULL;

	acct_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->description_list = list_create(slurm_destroy_char);
	acct_cond->organization_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			if(_set_cond(&i, argc, argv, acct_cond, assoc_cond))
				cond_set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			if(_set_rec(&i, argc, argv, acct, assoc))
				rec_set = 1;
		} else {
			if(_set_cond(&i, argc, argv, acct_cond, assoc_cond))
				cond_set = 1;
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
		destroy_acct_account_cond(acct_cond);
		destroy_acct_account_rec(acct);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			return SLURM_SUCCESS;
		}		
	}

	printf(" Setting\n");
	_print_rec(acct);
	printf("\n Where\n");
	_print_cond(acct_cond);

	
	if((ret_list = acct_storage_g_modify_accounts(db_conn, my_uid, 
						      acct_cond, acct))) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Effected...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		changes_made = 1;
		list_destroy(ret_list);
	} else {
		rc = SLURM_ERROR;
	}
	destroy_acct_account_cond(acct_cond);
	destroy_acct_account_rec(acct);
	

	return rc;
}

extern int sacctmgr_delete_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond =
		xmalloc(sizeof(acct_account_cond_t));
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	int i=0;
	List ret_list = NULL;

	acct_cond->acct_list = list_create(slurm_destroy_char);
	acct_cond->description_list = list_create(slurm_destroy_char);
	acct_cond->organization_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	
	if(!_set_cond(&i, argc, argv, acct_cond, assoc_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		return SLURM_ERROR;
	}

	printf(" Deleting accounts where...");
	_print_cond(acct_cond);

	if((ret_list = acct_storage_g_remove_accounts(db_conn, my_uid, 
						      acct_cond))) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Effected...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		changes_made = 1;
		account_changes = 1;
		list_destroy(ret_list);
	} else {
		rc = SLURM_ERROR;
	}
	destroy_acct_account_cond(acct_cond);

	return rc;
}
