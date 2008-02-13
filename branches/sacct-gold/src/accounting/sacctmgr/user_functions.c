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

static void _destroy_char(void *object)
{
	char *tmp = (char *)object;
	xfree(tmp);
}

extern int sacctmgr_create_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_list_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	List spec_list = list_create(_destroy_char);
	List user_list;;
	char *tmp_char = NULL;
	char *name = NULL;
	int i, j, start = 0;;
	ListIterator itr = NULL;
	account_user_rec_t *user = NULL;

	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "Name=", 5) == 0) {
			tmp_char = &argv[i][5];
			j = 0;
			if(tmp_char[j] == '\"')
				j++;
			start = j;
			while(tmp_char[j]) {
				if(tmp_char[j] == '\"')
					break;
				else if(tmp_char[j] == ',') {
					if(j-start > 0) {
						name = xmalloc((j-start+1));
						memcpy(name, tmp_char+start,
						       (j-start));
						list_push(spec_list, name);
					}
					j++;
					start = j;
				}
				j++;
			}
			if(j-start > 0) {
				name = xmalloc((j-start)+1);
				memcpy(name, tmp_char+start, (j-start));
				list_push(spec_list, name);
			}
		}
	}

	user_list = account_storage_g_get_users(spec_list, NULL);
	list_destroy(spec_list);
	
	itr = list_iterator_create(user_list);
	printf("%-30s %-30s %-10s\n%-30s %-30s %-10s\n",
	       "Name", "Default Account", "Expedite",
	       "------------------------------",
	       "------------------------------",
	       "----------");
	
	while((user = list_next(itr))) {
		printf("%-30s %-30s %-10d\n",
		       user->name, user->default_account,
		       user->expedite);
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(user_list);
	return rc;
}

extern int sacctmgr_update_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_delete_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}
