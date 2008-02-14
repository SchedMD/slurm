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

extern int sacctmgr_create_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_list_cluster(char *names)
{
	int rc = SLURM_SUCCESS;
	List spec_list = list_create(destroy_char);
	List cluster_list;
	char *name = NULL;
	int i=0, start = 0;;
	ListIterator itr = NULL;
	account_cluster_rec_t *cluster = NULL;

	if(names) {
		if (names[i] == '\"' || names[i] == '\'')
			i++;
		start = i;
		while(names[i]) {
			if(names[i] == '\"' || names[i] == '\'')
				break;
			else if(names[i] == ',') {
				if(i-start > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					list_push(spec_list, name);
				}
				i++;
				start = i;
			}
			i++;
		}
		if(i-start > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			list_push(spec_list, name);
		}
	}

	cluster_list = account_storage_g_get_clusters(spec_list, NULL);
	list_destroy(spec_list);
	
	itr = list_iterator_create(cluster_list);
	printf("%-15s\n%-15s\n", "Name", "---------------");
	
	while((cluster = list_next(itr))) {
		printf("%-15.15s\n", cluster->name);
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(cluster_list);
	return rc;
}

extern int sacctmgr_update_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_delete_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}
