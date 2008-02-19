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

extern int sacctmgr_create_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_list_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_user_cond_t *user_cond = xmalloc(sizeof(account_user_cond_t));
	List user_list;
	int i=0;
	ListIterator itr = NULL;
	account_user_rec_t *user = NULL;

	user_cond->user_list = list_create(destroy_char);
	user_cond->def_account_list = list_create(destroy_char);
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(user_cond->user_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "DefaultAccounts=", 16) == 0) {
			addto_char_list(user_cond->def_account_list,
					argv[i]+16);
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			user_cond->expedite =
				str_2_account_expedite(argv[i]+14);
		} else if (strncasecmp (argv[i], "AdminLevel=", 11) == 0) {
			user_cond->admin_level = 
				str_2_account_admin_level(argv[i]+11);
		} else {
			error("Valid options are 'Names=' 'DefaultAccounts=' "
			      "'ExpediteLevel=' and 'AdminLevel='");
		}		
	}

	user_list = account_storage_g_get_users(user_cond);
	destroy_account_user_cond(user_cond);

	if(!user_list) 
		return SLURM_ERROR;

	itr = list_iterator_create(user_list);
	printf("%-15s %-15s %-10s\n%-15s %-15s %-10s\n",
	       "Name", "Default Account", "Expedite",
	       "---------------",
	       "---------------",
	       "----------");
	
	while((user = list_next(itr))) {
		printf("%-15.15s %-15.15s %-10.10s\n",
		       user->name, user->default_account,
		       account_expedite_str(user->expedite));
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
