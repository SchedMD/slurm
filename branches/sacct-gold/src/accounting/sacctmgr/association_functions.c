/*****************************************************************************\
 *  association_functions.c - functions dealing with associations in the
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

extern int sacctmgr_create_association(int argc, char *argv[])
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
