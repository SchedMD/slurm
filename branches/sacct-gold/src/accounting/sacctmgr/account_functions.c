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

extern int sacctmgr_create_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_list_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_account_cond_t *account_cond =
		xmalloc(sizeof(account_account_cond_t));
	List account_list;
	int i=0;
	ListIterator itr = NULL;
	account_account_rec_t *account = NULL;
	
	account_cond->account_list = list_create(destroy_char);
	account_cond->description_list = list_create(destroy_char);
	account_cond->organization_list = list_create(destroy_char);

	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(account_cond->account_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "Descriptions=", 13) == 0) {
			addto_char_list(account_cond->description_list,
					argv[i]+13);
		} else if (strncasecmp (argv[i], "Organizations=", 14) == 0) {
			addto_char_list(account_cond->organization_list,
					argv[i]+14);
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			account_cond->expedite =
				str_2_account_expedite(argv[i]+14);
		} else {
			error("Valid options are 'Names=' "
			      "'Descriptions=' 'Oranizations=' "
			      "and 'ExpediteLevel='");
		}		
	}


	account_list = account_storage_g_get_accounts(account_cond);
	destroy_account_account_cond(account_cond);
	
	if(!account_list) 
		return SLURM_ERROR;
	

	itr = list_iterator_create(account_list);
	printf("%-15s %-15s %-15s %-10s\n%-15s %-15s %-15s %-10s\n",
	       "Name", "Description", "Organization", "Expedite",
	       "---------------",
	       "---------------",
	       "---------------",
	       "----------");
	
	while((account = list_next(itr))) {
		printf("%-15.15s %-15.15s %-15.15s %-10.10s\n",
		       account->name, account->description,
		       account->organization,
		       account_expedite_str(account->expedite));
	}

	printf("\n");

	list_iterator_destroy(itr);
	list_destroy(account_list);
	return rc;
}

extern int sacctmgr_modify_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}

extern int sacctmgr_delete_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	return rc;
}
