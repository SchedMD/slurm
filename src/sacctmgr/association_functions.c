/*****************************************************************************\
 *  association_functions.c - functions dealing with associations in the
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

static int _set_cond(int *start, int argc, char *argv[],
		     account_association_cond_t *association_cond)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Id=", 3) == 0) {
			addto_char_list(association_cond->id_list, argv[i]+3);
			set = 1;
		} else if (strncasecmp (argv[i], "Ids=", 4) == 0) {
			addto_char_list(association_cond->id_list, argv[i]+4);
			set = 1;
		} else if (strncasecmp (argv[i], "User=", 5) == 0) {
			addto_char_list(association_cond->user_list, argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Users=", 6) == 0) {
			addto_char_list(association_cond->user_list, argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "Account=", 8) == 0) {
			addto_char_list(association_cond->account_list,
					argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "Accounts=", 9) == 0) {
			addto_char_list(association_cond->account_list,
					argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "Cluster=", 8) == 0) {
			addto_char_list(association_cond->cluster_list,
					argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "Clusters=", 9) == 0) {
			addto_char_list(association_cond->cluster_list,
					argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "Partition=", 10) == 0) {
			addto_char_list(association_cond->partition_list,
					argv[i]+10);
			set = 1;
		} else if (strncasecmp (argv[i], "Partitions=", 11) == 0) {
			addto_char_list(association_cond->partition_list,
					argv[i]+11);
			set = 1;
		} else if (strncasecmp (argv[i], "Parent=", 7) == 0) {
			association_cond->parent_account = xstrdup(argv[i]+7);
			set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else {
			addto_char_list(association_cond->id_list, argv[i]);
			set = 1;
		}
	}
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    account_association_rec_t *association)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Id=", 3) == 0) {
			association->id = atoi(argv[i]+3);
			set = 1;
		} else if (strncasecmp (argv[i], "User=", 5) == 0) {
			association->user = xstrdup(argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Account=", 8) == 0) {
			association->account = xstrdup(argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "Cluster=", 8) == 0) {
			association->cluster = xstrdup(argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "Partition=", 10) == 0) {
			association->partition = xstrdup(argv[i]+10);
			set = 1;
		} else if (strncasecmp (argv[i], "Parent=", 7) == 0) {
			association->parent_account = xstrdup(argv[i]+7);
			set = 1;
		} else if (strncasecmp (argv[i], "FairShare=", 10) == 0) {
			association->fairshare = atoi(argv[i]+10);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxJobs=", 8) == 0) {
			association->max_jobs = atoi(argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxNodes=", 9) == 0) {
			association->max_nodes_per_job = atoi(argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxWall=", 8) == 0) {
			association->max_wall_duration_per_job =
				atoi(argv[i]+8);
			set = 1;
		} else if (strncasecmp (argv[i], "MaxCPUSecs=", 11) == 0) {
			association->max_cpu_seconds_per_job = atoi(argv[i]+11);
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

static void _print_cond(account_association_cond_t *association_cond)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;

	if(!association_cond) {
		error("no account_association_cond_t * given");
		return;
	}

	if(association_cond->id_list && list_count(association_cond->id_list)) {
		itr = list_iterator_create(association_cond->id_list);
		printf("  Id        = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("           or %s\n", tmp_char);
		}
	}

	if(association_cond->user_list
	   && list_count(association_cond->user_list)) {
		itr = list_iterator_create(association_cond->user_list);
		printf("  User      = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("           or %s\n", tmp_char);
		}
	}

	if(association_cond->account_list
	   && list_count(association_cond->account_list)) {
		itr = list_iterator_create(association_cond->account_list);
		printf("  Account   = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("           or %s\n", tmp_char);
		}
	}

	if(association_cond->cluster_list
	   && list_count(association_cond->cluster_list)) {
		itr = list_iterator_create(association_cond->cluster_list);
		printf("  Cluster   = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("           or %s\n", tmp_char);
		}
	}

	if(association_cond->partition_list
	   && list_count(association_cond->partition_list)) {
		itr = list_iterator_create(association_cond->partition_list);
		printf("  Partition = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("           or %s\n", tmp_char);
		}
	}

	if(association_cond->parent_account)
		printf("  Parent    = %s\n", association_cond->parent_account);

}

static void _print_rec(account_association_rec_t *association)
{
	if(!association) {
		error("no account_association_rec_t * given");
		return;
	}
	
	if(association->id) 
		printf("  Id         = %u\n", association->id);	
		
	if(association->user) 
		printf("  User       = %s\n", association->user);
	if(association->account) 
		printf("  Account    = %s\n", association->account);
	if(association->cluster) 
		printf("  Cluster    = %s\n", association->cluster);
	if(association->partition) 
		printf("  Partition  = %s\n", association->partition);
	if(association->parent_account) 
		printf("  Parent     = %s\n", association->parent_account);
	if(association->fairshare) 
		printf("  FairShare  = %u\n", association->fairshare);
	if(association->max_jobs) 
		printf("  MaxJobs    = %u\n", association->max_jobs);
	if(association->max_nodes_per_job) 
		printf("  MaxNodes   = %u\n", association->max_nodes_per_job);
	if(association->max_wall_duration_per_job) 
		printf("  MaxWall    = %u\n",
		       association->max_wall_duration_per_job);
	if(association->max_cpu_seconds_per_job) 
		printf("  MaxCPUSecs = %u\n",
		       association->max_cpu_seconds_per_job);
}

extern int sacctmgr_add_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int sacctmgr_list_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_association_cond_t *assoc_cond =
		xmalloc(sizeof(account_association_cond_t));
	List assoc_list = NULL;
	account_association_rec_t *assoc = NULL;
	int i=0;
	ListIterator itr = NULL;

	assoc_cond->id_list = list_create(destroy_char);
	assoc_cond->user_list = list_create(destroy_char);
	assoc_cond->account_list = list_create(destroy_char);
	assoc_cond->cluster_list = list_create(destroy_char);

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Ids=", 4) == 0) {
			addto_char_list(assoc_cond->id_list, argv[i]+3);
		} else if (strncasecmp (argv[i], "Users=", 6) == 0) {
			addto_char_list(assoc_cond->user_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "Accounts=", 9) == 0) {
			addto_char_list(assoc_cond->account_list, argv[i]+9);
		} else if (strncasecmp (argv[i], "Clusters=", 9) == 0) {
			addto_char_list(assoc_cond->cluster_list, argv[i]+9);
		} else {
			error("Valid options are 'Ids=' 'Users=' 'Accounts=' "
			      "and 'Clusters='");
		}		
	}

	assoc_list = account_storage_g_get_associations(assoc_cond);
	destroy_account_association_cond(assoc_cond);
	
	if(!assoc_list) 
		return SLURM_ERROR;

	itr = list_iterator_create(assoc_list);
	printf("%-6s %-10s %-10s %-10s %-10s %-9s %-7s %-8s %-7s %-10s\n"
	       "%-6s %-10s %-10s %-10s %-10s %-9s %-7s %-8s %-7s %-10s\n", 
	       "Id", "User", "Account", "Cluster", "Partition", "FairShare", 
	       "MaxJobs", "MaxNodes", "MaxWall", "MaxCPUSecs",
	       "------", "----------", "----------", "----------", 
	       "----------", "---------", "-------", "--------", "-------",
	       "----------");
	
	while((assoc = list_next(itr))) {
		printf("%-6u ", assoc->id);
		printf("%-10.10s ", assoc->user);
		printf("%-10.10s ", assoc->account);
		printf("%-10.10s ", assoc->cluster);
		printf("%-10.10s ", assoc->partition);
		printf("%-9u ", assoc->fairshare);
		printf("%-7u ", assoc->max_jobs);
		printf("%-8u ", assoc->max_nodes_per_job);
		printf("%-7u ", assoc->max_wall_duration_per_job);
		printf("%-10u\n", assoc->max_cpu_seconds_per_job);
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(assoc_list);

	return rc;
}

extern int sacctmgr_modify_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_delete_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}
