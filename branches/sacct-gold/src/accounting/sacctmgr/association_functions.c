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
	List id_list = list_create(destroy_char);
	List user_list = list_create(destroy_char);
	List account_list = list_create(destroy_char);
	List cluster_list = list_create(destroy_char);
	List assoc_list = NULL;
	account_association_rec_t *assoc = NULL;

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Id=", 3) == 0) {
			addto_char_list(id_list, argv[i]+3);
			break;
		} else if (strncasecmp (argv[i], "User=", 4) == 0) {
			addto_char_list(user_list, argv[i]+3);
			break;
		} else if (strncasecmp (argv[i], "Account=", 7) == 0) {
			addto_char_list(account_list, argv[i]+3);
			break;
		} else if (strncasecmp (argv[i], "Cluster=", 7) == 0) {
			addto_char_list(cluster_list, argv[i]+3);
			break;
		}		
	}

	assoc_list = account_storage_g_get_clusters(
		id_list, user_list, account_list, cluster_list, NULL);
	list_destroy(spec_list);
	
	itr = list_iterator_create(assoc_list);
	printf("%-6s %-10s %-10s %-10s %-10s %-9s %-7s %-8s %-7 %-10\n"
	       "%-6s %-10s %-10s %-10s %-10s %-9s %-7s %-8s %-7 %-10\n", 
	       "Id", "User", "Account", "Cluster", "Partition", "FairShare", 
	       "MaxJobs", "MaxNodes", "MaxWall", "MaxCPUSecs",
	       "------", "----------", "----------", "----------", 
	       "----------", "---------", "-------", "--------", "-------",
	       "----------");
	
	while((assoc = list_next(itr))) {
		printf("%-6.6u\n", assoc->id);
		printf("%-10.10s\n", assoc->user);
		printf("%-10.10s\n", assoc->account);
		printf("%-10.10s\n", assoc->cluster);
		printf("%-10.10s\n", assoc->partition);
		printf("%-9.9u\n", assoc->fairshare);
		printf("%-7.7u\n", assoc->max_jobs);
		printf("%-8.8u\n", assoc->max_nodes_per_job);
		printf("%-7.7u\n", assoc->max_wall_duration_per_job);
		printf("%-10.10u\n", assoc->max_cpu_seconds_per_job);
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(assoc_list);

	return rc;
}

extern int sacctmgr_update_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_delete_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}
