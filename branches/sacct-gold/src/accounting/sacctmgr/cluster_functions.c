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
		     account_cluster_cond_t *cluster_cond)
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
		    account_cluster_rec_t *cluster)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "InterfaceNode=", 14) == 0) {
			cluster->interface_node = xstrdup(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Interface=", 10) == 0) {
			cluster->interface_node = xstrdup(argv[i]+10);
			set = 1;
		} else if (strncasecmp (argv[i], "Where", 5) == 0) {
			i--;
			break;
		} else {
			printf(" error: Valid options are 'InterfaceNode='\n");
		}
	}
	(*start) = i;

	return set;

}

static void _print_cond(account_cluster_cond_t *cluster_cond)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;

	if(!cluster_cond) {
		error("no account_cluster_cond_t * given");
		return;
	}

	if(cluster_cond->cluster_list
	   && list_count(cluster_cond->cluster_list)) {
		itr = list_iterator_create(cluster_cond->cluster_list);
		printf("  Names           = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("                 or %s\n", tmp_char);
		}
	}
}


extern int sacctmgr_add_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	account_cluster_rec_t *cluster = xmalloc(sizeof(account_cluster_rec_t));
	List cluster_list = NULL;

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			cluster->name = xstrdup(argv[i]+5);
		} else if (strncasecmp (argv[i], "Interface=", 10) == 0) {
			cluster->interface_node = xstrdup(argv[i]+10);
		} else if (strncasecmp (argv[i], "InterfaceNode=", 14) == 0) {
			cluster->interface_node = xstrdup(argv[i]+14);
		} else {
			cluster->name = xstrdup(argv[i]);
		}		
	}
	if(!cluster->name) {
		destroy_account_cluster_rec(cluster);
		printf(" Need name of cluster to add.\n"); 
		return SLURM_SUCCESS;
	}
	
	printf(" Adding Cluster(s)\n");
	printf("  Name           = %s", cluster->name);
	if(cluster->interface_node)
		printf("  Interface Node = %s", cluster->interface_node);
	cluster_list = list_create(destroy_account_cluster_rec);
	list_push(cluster_list, cluster);

	if(execute_flag) {
		rc = account_storage_g_add_clusters(cluster_list);
		list_destroy(cluster_list);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_CLUSTER_CREATE;
		action->list = list_create(destroy_account_cluster_rec);
		list_push(sacctmgr_action_list, action);
	}

	return rc;
}

extern int sacctmgr_list_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(account_cluster_cond_t));
	List cluster_list;
	int i=0;
	ListIterator itr = NULL;
	account_cluster_rec_t *cluster = NULL;

	cluster_cond->cluster_list = list_create(destroy_char);
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(cluster_cond->cluster_list, argv[i]+5);
		} else if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(cluster_cond->cluster_list, argv[i]+6);
		} else {
			error("Only 'Names=' is supported as an option");
		}		
	}

	cluster_list = account_storage_g_get_clusters(cluster_cond);
	destroy_account_cluster_cond(cluster_cond);
	
	if(!cluster_list) 
		return SLURM_ERROR;
	
	itr = list_iterator_create(cluster_list);
	printf("%-15s %-15s\n%-15s %-15s\n", "Name", "Interface Node",
	       "---------------",
	       "---------------");
	
	while((cluster = list_next(itr))) {
		printf("%-15.15s %-15.15s\n",
		       cluster->name,
		       cluster->interface_node);
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
	account_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(account_cluster_cond_t));
	account_cluster_rec_t *cluster = xmalloc(sizeof(account_cluster_rec_t));
	List cluster_list = NULL;
	int cond_set = 0, rec_set = 0;

	cluster_cond->cluster_list = list_create(destroy_char);

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
		destroy_account_cluster_cond(cluster_cond);
		destroy_account_cluster_rec(cluster);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			return SLURM_SUCCESS;
		}		
	}

	printf(" Setting\n");
	printf("  InterfaceNode = %s", cluster->interface_node);
	printf("\n Where\n");
	_print_cond(cluster_cond);

	cluster_list = list_create(destroy_account_cluster_rec);
	list_push(cluster_list, cluster);

	if(execute_flag) {
		rc = account_storage_g_modify_clusters(cluster_cond, cluster);
		destroy_account_cluster_cond(cluster_cond);
		destroy_account_cluster_rec(cluster);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_CLUSTER_MODIFY;
		action->cond = cluster_cond;
		action->rec = cluster;
		list_push(sacctmgr_action_list, action);
	}

	return rc;
}

extern int sacctmgr_delete_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_cluster_cond_t *cluster_cond = xmalloc(sizeof(account_cluster_cond_t));
	int i=0;

	cluster_cond->cluster_list = list_create(destroy_char);
	
	if(!_set_cond(&i, argc, argv, cluster_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		return SLURM_ERROR;
	}

	printf(" Deleting clusters where...");
	_print_cond(cluster_cond);

	if(execute_flag) {
		rc = account_storage_g_remove_clusters(cluster_cond);
		destroy_account_cluster_cond(cluster_cond);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_CLUSTER_DELETE;
		action->cond = cluster_cond;
		list_push(sacctmgr_action_list, action);
	}

	return rc;
}
