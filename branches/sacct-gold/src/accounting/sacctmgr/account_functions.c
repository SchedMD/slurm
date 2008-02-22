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
		     account_account_cond_t *account_cond)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(account_cond->account_list, argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(account_cond->account_list, argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else if (strncasecmp (argv[i], "Descriptions=", 13) == 0) {
			addto_char_list(account_cond->description_list,
					argv[i]+13);
			set = 1;
		} else if (strncasecmp (argv[i], "Organizations=", 14) == 0) {
			addto_char_list(account_cond->organization_list,
					argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			account_cond->expedite =
				str_2_account_expedite(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Description=", 12) == 0) {
			addto_char_list(account_cond->description_list,
					argv[i]+12);
			set = 1;
		} else if (strncasecmp (argv[i], "Organization=", 13) == 0) {
			addto_char_list(account_cond->organization_list,
					argv[i]+13);
			set = 1;
		} else if (strncasecmp (argv[i], "Expedite=", 8) == 0) {
			account_cond->expedite =
				str_2_account_expedite(argv[i]+8);
			set = 1;
		} else {
			addto_char_list(account_cond->account_list, argv[i]);
			set = 1;
		}
	}
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    account_account_rec_t *account)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			account->name = xstrdup(argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Where", 5) == 0) {
			i--;
			break;
		}else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			account->expedite =
				str_2_account_expedite(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Description=", 12) == 0) {
			account->description = xstrdup(argv[i]+12);
			set = 1;
		} else if (strncasecmp (argv[i], "Organization=", 13) == 0) {
			account->organization = xstrdup(argv[i]+13);
			set = 1;
		} else if (strncasecmp (argv[i], "Expedite=", 8) == 0) {
			account->expedite =
				str_2_account_expedite(argv[i]+8);
			set = 1;
		} else {
			account->name = xstrdup(argv[i]+5);
			set = 1;
		}
	}
	(*start) = i;

	return set;
}

static void _print_cond(account_account_cond_t *account_cond)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;

	if(!account_cond) {
		error("no account_account_cond_t * given");
		return;
	}

	if(account_cond->account_list && list_count(account_cond->account_list)) {
		itr = list_iterator_create(account_cond->account_list);
		printf("  Names       = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("             or %s\n", tmp_char);
		}
	}

	if(account_cond->description_list
	   && list_count(account_cond->description_list)) {
		itr = list_iterator_create(account_cond->description_list);
		printf("  Description = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("             or %s\n", tmp_char);
		}
	}

	if(account_cond->organization_list
	   && list_count(account_cond->organization_list)) {
		itr = list_iterator_create(account_cond->organization_list);
		printf("  Organization = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("             or %s\n", tmp_char);
		}
	}

	if(account_cond->expedite != ACCOUNT_EXPEDITE_NOTSET)
		printf("  Expedite     = %s\n", 
		       account_expedite_str(account_cond->expedite));
}

static void _print_rec(account_account_rec_t *account)
{
	if(!account) {
		error("no account_account_rec_t * given");
		return;
	}
	
	if(account->name) 
		printf("  Name         = %s\n", account->name);	
		
	if(account->description) 
		printf("  Description  = %s\n", account->description);

	if(account->organization) 
		printf("  Organization = %s\n", account->organization);
		
	if(account->expedite != ACCOUNT_EXPEDITE_NOTSET)
		printf("  Expedite     = %s\n", 
		       account_expedite_str(account->expedite));

}


extern int sacctmgr_create_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	ListIterator itr = NULL;
	account_account_rec_t *account = NULL;
	List name_list = list_create(destroy_char);
	char *description = NULL;
	char *organization = NULL;
	account_expedite_level_t expedite = ACCOUNT_EXPEDITE_NOTSET;
	char *name = NULL;
	List account_list = NULL;
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(name_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(name_list, argv[i]+5);
		} else if (strncasecmp (argv[i], "Description=", 12) == 0) {
			description = xstrdup(argv[i]+12);
		} else if (strncasecmp (argv[i], "Organization=", 13) == 0) {
			organization = xstrdup(argv[i]+13);
		} else if (strncasecmp (argv[i], "Expedite=", 8) == 0) {
			expedite = str_2_account_expedite(argv[i]+8);
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			expedite = str_2_account_expedite(argv[i]+14);
		} else {
			addto_char_list(name_list, argv[i]);
		}		
	}

	if(!list_count(name_list)) {
		list_destroy(name_list);
		printf(" Need name of account to add.\n"); 
		return SLURM_SUCCESS;
	} else if(!description) {
		list_destroy(name_list);
		printf(" Need a description for these accounts to add.\n"); 
		return SLURM_SUCCESS;
	} else if(!organization) {
		list_destroy(name_list);
		printf(" Need an organization for these accounts to add.\n"); 
		return SLURM_SUCCESS;
	}

	printf(" Adding Account(s)\n");
		
	account_list = list_create(destroy_account_account_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		account = xmalloc(sizeof(account_account_rec_t));
		account->name = xstrdup(name);
		account->description = xstrdup(description);
		account->organization = xstrdup(organization);
		account->expedite = expedite;
		printf("\t%s", name);

		list_append(account_list, account);
	}
	list_iterator_destroy(itr);

	printf(" Settings =\n");
	printf("  Description = %s\n", description);
	printf("  Organization = %s\n", organization);
	
	if(expedite != ACCOUNT_EXPEDITE_NOTSET)
		printf("  Expedite        = %s\n", 
		       account_expedite_str(expedite));
	
	if(execute_flag) {
		rc = account_storage_g_add_accounts(account_list);
		list_destroy(account_list);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_ACCOUNT_CREATE;
		action->list = list_create(destroy_account_account_rec);
		list_push(action_list, action);
	}
	
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
	account_account_cond_t *account_cond = 
		xmalloc(sizeof(account_account_cond_t));
	account_account_rec_t *account = xmalloc(sizeof(account_account_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0;

	account_cond->account_list = list_create(destroy_char);
	account_cond->description_list = list_create(destroy_char);
	account_cond->organization_list = list_create(destroy_char);
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			if(_set_cond(&i, argc, argv, account_cond))
				cond_set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			if(_set_rec(&i, argc, argv, account))
				rec_set = 1;
		} else {
			if(_set_cond(&i, argc, argv, account_cond))
				cond_set = 1;
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
		destroy_account_account_cond(account_cond);
		destroy_account_account_rec(account);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			return SLURM_SUCCESS;
		}		
	}

	printf(" Setting\n");
	_print_rec(account);
	printf("\n Where\n");
	_print_cond(account_cond);

	if(execute_flag) {
		rc = account_storage_g_modify_accounts(account_cond, account);
		destroy_account_account_cond(account_cond);
		destroy_account_account_rec(account);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_ACCOUNT_MODIFY;
		action->cond = account_cond;
		action->rec = account;
		list_push(action_list, action);
	}

	return rc;
}

extern int sacctmgr_delete_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_account_cond_t *account_cond =
		xmalloc(sizeof(account_account_cond_t));
	int i=0;

	account_cond->account_list = list_create(destroy_char);
	account_cond->description_list = list_create(destroy_char);
	account_cond->organization_list = list_create(destroy_char);
	
	if(!_set_cond(&i, argc, argv, account_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		return SLURM_ERROR;
	}

	printf(" Deleting accounts where...");
	_print_cond(account_cond);

	if(execute_flag) {
		rc = account_storage_g_remove_accounts(account_cond);
		destroy_account_account_cond(account_cond);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_ACCOUNT_DELETE;
		action->cond = account_cond;
		list_push(action_list, action);
	}

	return rc;
}
