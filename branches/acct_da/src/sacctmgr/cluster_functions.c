/*****************************************************************************\
 *  cluster_functions.c - functions dealing with clusters in the
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
		     acct_cluster_cond_t *cluster_cond)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(cluster_cond->cluster_list, argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(cluster_cond->cluster_list, argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else {
			addto_char_list(cluster_cond->cluster_list, argv[i]);
			set = 1;
		}
	}
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    acct_cluster_rec_t *cluster)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "FairShare=", 10) == 0) {
			cluster->default_fairshare = atoi(argv[i]+10);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs=", 8) == 0) {
			cluster->default_max_jobs = atoi(argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes=", 9) == 0) {
			cluster->default_max_nodes_per_job = atoi(argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxWall=", 8) == 0) {
			cluster->default_max_wall_duration_per_job = atoi(argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSecs=", 11) == 0) {
			cluster->default_max_cpu_secs_per_job = atoi(argv[i]+11);
			set = 1;
		} else if (strncasecmp (argv[i], "Where", 5) == 0) {
			i--;
			break;
		} else {
			printf(" error: Valid options are\n");
		}
	}
	(*start) = i;

	return set;

}

static void _print_cond(acct_cluster_cond_t *cluster_cond)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;

	if(!cluster_cond) {
		error("no acct_cluster_cond_t * given");
		return;
	}

	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		itr = list_iterator_create(cluster_cond->cluster_list);
		tmp_char = list_next(itr);
		printf("  Names         = %s\n", tmp_char);
		while((tmp_char = list_next(itr))) {
			printf("               or %s\n", tmp_char);
		}
	}
}

static void _update_existing(acct_cluster_cond_t *cluster_cond,
			     acct_cluster_rec_t *new_cluster,
			     acct_association_rec_t *new_assoc)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;

	if(!cluster_cond) {
		error("no acct_cluster_cond_t * given");
		return;
	}

	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		itr = list_iterator_create(cluster_cond->cluster_list);
		while((tmp_char = list_next(itr))) {
			if(!(cluster = sacctmgr_find_cluster(tmp_char))) {
				printf(" Cluster '%s' does not exist, "
				       "not removing.\n", tmp_char);
				list_remove(itr);
				continue;
			}

			if(!new_cluster) {
				sacctmgr_remove_from_list(sacctmgr_cluster_list,
							  cluster);
			} else {
								
			}

			if(!(assoc = sacctmgr_find_association(
				     NULL, "template_account",
				     tmp_char, NULL))) {
				printf(" Can't find template account for '%s' "
				       "something is messed up.\n", tmp_char);
				continue;
			}
			if(!new_assoc) {
				sacctmgr_remove_from_list(
					sacctmgr_association_list, assoc);
				continue;
			}

			if(new_assoc->fairshare)
				assoc->fairshare = new_assoc->fairshare;
			if(new_assoc->max_jobs)
				assoc->max_jobs = new_assoc->max_jobs;
			if(new_assoc->max_nodes_per_job)
				assoc->max_nodes_per_job =
					new_assoc->max_nodes_per_job;
			if(new_assoc->max_wall_duration_per_job)
				assoc->max_wall_duration_per_job = 
					new_assoc->max_wall_duration_per_job;
			if(new_assoc->max_cpu_secs_per_job)
				assoc->max_cpu_secs_per_job = 
					new_assoc->max_cpu_secs_per_job;	
		}
		list_iterator_destroy(itr);
	}
}
	
extern int sacctmgr_add_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	acct_cluster_rec_t *cluster = xmalloc(sizeof(acct_cluster_rec_t));
	acct_association_rec_t *assoc = NULL;		  
	List cluster_list = NULL;
	int limit_set = 0;

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			if(cluster->name)
				error("can only add one cluster at a time.\n");
			else {
				cluster->name = xstrdup(argv[i]+5);
			}
		} else if (strncasecmp (argv[i], "FairShare=", 10) == 0) {
			cluster->default_fairshare = atoi(argv[i]+10);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs=", 8) == 0) {
			cluster->default_max_jobs = atoi(argv[i]+8);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes=", 9) == 0) {
			cluster->default_max_nodes_per_job = atoi(argv[i]+9);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxWall=", 8) == 0) {
			cluster->default_max_wall_duration_per_job =
				atoi(argv[i]+8);
			limit_set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSecs=", 11) == 0) {
			cluster->default_max_cpu_secs_per_job = 
				atoi(argv[i]+11);
			limit_set = 1;
		} else {
			if(cluster->name)
				error("can only add one cluster at a time.\n");
			else {
				cluster->name = xstrdup(argv[i]);
			}
		}		
	}

	if(!cluster->name) {
		destroy_acct_cluster_rec(cluster);
		printf(" Need name of cluster to add.\n"); 
		return SLURM_ERROR;
	} else if(sacctmgr_find_cluster(cluster->name)) {
		destroy_acct_cluster_rec(cluster);
		printf(" This cluster already exists.  Not adding.\n");
		return SLURM_ERROR;
	}

	printf(" Adding Cluster(s)\n");
	printf("  Name           = %s\n", cluster->name);

	printf(" User Defaults =\n");

	if(cluster->default_fairshare < 0)
		cluster->default_fairshare = 1;
	printf("  Fairshare     = %u\n", cluster->default_fairshare);

	if(cluster->default_max_jobs)
		printf("  MaxJobs       = %u\n", cluster->default_max_jobs);
	if(cluster->default_max_nodes_per_job)
		printf("  MaxNodes      = %u\n",
		       cluster->default_max_nodes_per_job);
	if(cluster->default_max_wall_duration_per_job)
		printf("  MaxWall       = %u\n",
		       cluster->default_max_wall_duration_per_job);
	if(cluster->default_max_cpu_secs_per_job)
		printf("  MaxCPUSecs    = %u\n",
		       cluster->default_max_cpu_secs_per_job);
	
	cluster_list = list_create(NULL);
	list_append(cluster_list, cluster);

	if(execute_flag) {
		rc = acct_storage_g_add_clusters(db_conn, my_uid, 
						 cluster_list);
		list_destroy(cluster_list);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_CLUSTER_CREATE;
		action->list = cluster_list;
		list_append(sacctmgr_action_list, action);
	}
	list_append(sacctmgr_cluster_list, cluster);
	assoc = xmalloc(sizeof(acct_association_rec_t));
	list_append(sacctmgr_association_list, assoc);
	assoc->acct = xstrdup("root");
	assoc->cluster = xstrdup(cluster->name);
	assoc->fairshare = cluster->default_fairshare;
	assoc->max_jobs = cluster->default_max_jobs;
	assoc->max_nodes_per_job = cluster->default_max_nodes_per_job;
	assoc->max_wall_duration_per_job = 
		cluster->default_max_wall_duration_per_job;
	assoc->max_cpu_secs_per_job = cluster->default_max_cpu_secs_per_job;

	return rc;
}

extern int sacctmgr_list_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	List cluster_list;
	int i=0;
	ListIterator itr = NULL;
	acct_cluster_rec_t *cluster = NULL;

	cluster_cond->cluster_list = list_create(slurm_destroy_char);
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(cluster_cond->cluster_list, argv[i]+5);
		} else if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(cluster_cond->cluster_list, argv[i]+6);
		} else {
			error("Only 'Names=' is supported as an option");
		}		
	}
	
	cluster_list = acct_storage_g_get_clusters(db_conn, cluster_cond);
	destroy_acct_cluster_cond(cluster_cond);
	
	if(!cluster_list) 
		return SLURM_ERROR;
	
	itr = list_iterator_create(cluster_list);
	printf("%-15s %-15s %-5s\n%-15s %-15s %-5s\n",
	       "Name", "Control Host", "Port",
	       "---------------",
	       "---------------",
	       "-----");
	
	while((cluster = list_next(itr))) {
		printf("%-15.15s %-15.15s %-5u\n",
		       cluster->name,
		       cluster->control_host,
		       cluster->control_port);
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(cluster_list);
	return rc;
}

extern int sacctmgr_modify_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
/* 	acct_association_rec_t *assoc = xmalloc(sizeof(acct_association_rec_t)); */
/* 	acct_association_cond_t *assoc_cond = */
/* 		xmalloc(sizeof(acct_association_cond_t)); */
	acct_cluster_rec_t *cluster = xmalloc(sizeof(acct_cluster_rec_t));
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	List cluster_list = NULL;
	int cond_set = 0, rec_set = 0;

	cluster_cond->cluster_list = list_create(slurm_destroy_char);
	//assoc_cond->cluster_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			if(_set_cond(&i, argc, argv, cluster_cond))
				cond_set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			if(_set_rec(&i, argc, argv, cluster))
				rec_set = 1;
		} else {
			if(_set_cond(&i, argc, argv, cluster_cond))
				cond_set = 1;
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
		destroy_acct_cluster_rec(cluster);
		destroy_acct_cluster_cond(cluster_cond);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_cluster_rec(cluster);
			destroy_acct_cluster_cond(cluster_cond);
			return SLURM_SUCCESS;
		}		
	}

//	_update_existing(cluster_cond, cluster, assoc);

	//assoc_cond->acct_list = list_create(slurm_destroy_char);
	//list_push(assoc_cond->acct_list, "template_account");

	printf(" Setting\n");
	if(rec_set) 
		printf(" User Defaults =\n");
	if(cluster->default_fairshare)
		printf("  Fairshare     = %u\n", cluster->default_fairshare);
	if(cluster->default_max_jobs)
		printf("  MaxJobs       = %u\n", cluster->default_max_jobs);
	if(cluster->default_max_nodes_per_job)
		printf("  MaxNodes      = %u\n", cluster->default_max_nodes_per_job);
	if(cluster->default_max_wall_duration_per_job)
		printf("  MaxWall       = %u\n",
		       cluster->default_max_wall_duration_per_job);
	if(cluster->default_max_cpu_secs_per_job)
		printf("  MaxCPUSecs    = %u\n",
		       cluster->default_max_cpu_secs_per_job);
	printf("\n Where\n");
	_print_cond(cluster_cond);

	cluster_list = list_create(destroy_acct_cluster_rec);
	list_append(cluster_list, cluster);

	if(execute_flag) {
		if(list_count(cluster_cond->cluster_list)) {
			rc = acct_storage_g_modify_clusters(db_conn, my_uid, 
							    cluster_cond,
							    cluster);
		}
		destroy_acct_cluster_cond(cluster_cond);
		destroy_acct_cluster_rec(cluster);

	} else {
		sacctmgr_action_t *action = NULL;
		if(list_count(cluster_cond->cluster_list)) {
			action = xmalloc(sizeof(sacctmgr_action_t));
			action->type = SACCTMGR_CLUSTER_MODIFY;
			action->cond = cluster_cond;
			action->rec = cluster;
			list_append(sacctmgr_action_list, action);
		} else {
			destroy_acct_cluster_cond(cluster_cond);
			destroy_acct_cluster_rec(cluster);
		
		}
	}

	return rc;
}

extern int sacctmgr_delete_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	int i=0;

	cluster_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	
	if(!_set_cond(&i, argc, argv, cluster_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		destroy_acct_cluster_cond(cluster_cond);
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	}

	_update_existing(cluster_cond, NULL, NULL);

	if(!list_count(cluster_cond->cluster_list)) {
		destroy_acct_cluster_cond(cluster_cond);
		destroy_acct_association_cond(assoc_cond);
		return SLURM_SUCCESS;
	}
	printf(" Deleting clusters where...\n");
	_print_cond(cluster_cond);

	if(execute_flag) {
		if(list_count(cluster_cond->cluster_list)) {
			rc = acct_storage_g_remove_clusters(db_conn, my_uid, 
							    cluster_cond);
			rc = acct_storage_g_remove_associations(db_conn, 
								my_uid, 
								assoc_cond);
		}
		destroy_acct_cluster_cond(cluster_cond);
		destroy_acct_association_cond(assoc_cond);
	} else {
		sacctmgr_action_t *action = NULL;
		
		if(list_count(cluster_cond->cluster_list)) {
			action = xmalloc(sizeof(sacctmgr_action_t));
			action->type = SACCTMGR_CLUSTER_DELETE;
			action->cond = cluster_cond;
			list_append(sacctmgr_action_list, action);

			action = xmalloc(sizeof(sacctmgr_action_t));
			action->type = SACCTMGR_ASSOCIATION_DELETE;
			action->cond = assoc_cond;
			list_append(sacctmgr_action_list, action);
		} else {
			destroy_acct_cluster_cond(cluster_cond);
			destroy_acct_association_cond(assoc_cond);
		}
	}

	return rc;
}
