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

static int _set_cond(int *start, int argc, char *argv[],
		     account_user_cond_t *user_cond)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(user_cond->user_list, argv[i]+5);
			set = 1;
		} else if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(user_cond->user_list, argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "DefaultAccount=", 15) == 0) {
			addto_char_list(user_cond->def_account_list,
					argv[i]+15);
			set = 1;
		} else if (strncasecmp (argv[i], "DefaultAccounts=", 16) == 0) {
			addto_char_list(user_cond->def_account_list,
					argv[i]+16);
			set = 1;
		} else if (strncasecmp (argv[i], "Expedite=", 9) == 0) {
			user_cond->expedite =
				str_2_account_expedite(argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			user_cond->expedite =
				str_2_account_expedite(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Admin=", 6) == 0) {
			user_cond->admin_level = 
				str_2_account_admin_level(argv[i]+6);
			set = 1;			
		} else if (strncasecmp (argv[i], "AdminLevel=", 11) == 0) {
			user_cond->admin_level = 
				str_2_account_admin_level(argv[i]+11);
			set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i--;
			break;
		} else {
			addto_char_list(user_cond->user_list, argv[i]);
			set = 1;
		}		
	}	
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    account_user_rec_t *user)
{
	int i;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		if (strncasecmp (argv[i], "DefaultAccount=", 15) == 0) {
			user->default_account = xstrdup(argv[i]+15);
			set = 1;
		} else if (strncasecmp (argv[i], "Expedite=", 9) == 0) {
			user->expedite =
				str_2_account_expedite(argv[i]+9);
			set = 1;
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			user->expedite =
				str_2_account_expedite(argv[i]+14);
			set = 1;
		} else if (strncasecmp (argv[i], "Admin=", 6) == 0) {
			user->admin_level = 
				str_2_account_admin_level(argv[i]+6);
			set = 1;
		} else if (strncasecmp (argv[i], "AdminLevel=", 11) == 0) {
			user->admin_level = 
				str_2_account_admin_level(argv[i]+11);
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

static void _print_cond(account_user_cond_t *user_cond)
{
	ListIterator itr = NULL;
	char *tmp_char = NULL;

	if(!user_cond) {
		error("no account_user_cond_t * given");
		return;
	}

	if(user_cond->user_list && list_count(user_cond->user_list)) {
		itr = list_iterator_create(user_cond->user_list);
		printf("  Names           = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("                 or %s\n", tmp_char);
		}
	}

	if(user_cond->def_account_list
	   && list_count(user_cond->def_account_list)) {
		itr = list_iterator_create(user_cond->def_account_list);
		printf("  Default Account = %s\n", (char *)list_next(itr));
		while((tmp_char = list_next(itr))) {
			printf("                 or %s\n", tmp_char);
		}
	}

	if(user_cond->expedite != ACCOUNT_EXPEDITE_NOTSET)
		printf("  Expedite        = %s\n", 
		       account_expedite_str(user_cond->expedite));

	if(user_cond->admin_level != ACCOUNT_ADMIN_NOTSET)
		printf("  Admin Level     = %s\n", 
		       account_admin_level_str(user_cond->admin_level));
}

static void _print_rec(account_user_rec_t *user)
{
	if(!user) {
		error("no account_user_rec_t * given");
		return;
	}
	
	if(user->name) 
		printf("  Name            = %s\n", user->name);	
		
	if(user->default_account) 
		printf("  Default Account = %s\n", user->default_account);
		
	if(user->expedite != ACCOUNT_EXPEDITE_NOTSET)
		printf("  Expedite        = %s\n", 
		       account_expedite_str(user->expedite));

	if(user->admin_level != ACCOUNT_ADMIN_NOTSET)
		printf("  Admin Level     = %s\n", 
		       account_admin_level_str(user->admin_level));
}


extern int sacctmgr_create_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	ListIterator itr = NULL;
	account_user_rec_t *user = NULL;
	List name_list = list_create(destroy_char);
	char *default_acct = NULL;
	account_expedite_level_t expedite = ACCOUNT_EXPEDITE_NOTSET;
	account_admin_level_t admin_level = ACCOUNT_ADMIN_NOTSET;
	char *name = NULL;
	List user_list = NULL;
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Names=", 6) == 0) {
			addto_char_list(name_list, argv[i]+6);
		} else if (strncasecmp (argv[i], "Name=", 5) == 0) {
			addto_char_list(name_list, argv[i]+5);
		} else if (strncasecmp (argv[i], "DefaultAccount=", 15) == 0) {
			default_acct = xstrdup(argv[i]+15);
		} else if (strncasecmp (argv[i], "Expedite=", 8) == 0) {
			expedite = str_2_account_expedite(argv[i]+8);
		} else if (strncasecmp (argv[i], "ExpediteLevel=", 14) == 0) {
			expedite = str_2_account_expedite(argv[i]+14);
		} else if (strncasecmp (argv[i], "Admin=", 5) == 0) {
			admin_level = str_2_account_admin_level(argv[i]+5);
		} else if (strncasecmp (argv[i], "AdminLevel=", 11) == 0) {
			admin_level = str_2_account_admin_level(argv[i]+11);
		} else {
			addto_char_list(name_list, argv[i]);
		}		
	}
	if(!list_count(name_list)) {
		list_destroy(name_list);
		printf(" Need name of user to add.\n"); 
		return SLURM_SUCCESS;
	} else if(!default_acct) {
		list_destroy(name_list);
		printf(" Need a default account for these users to add.\n"); 
		return SLURM_SUCCESS;
	}

	printf(" Adding User(s)\n");
		
	user_list = list_create(destroy_account_user_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		user = xmalloc(sizeof(account_user_rec_t));
		user->name = xstrdup(name);
		user->default_account = xstrdup(default_acct);
		user->expedite = expedite;
		user->admin_level = admin_level;
		printf("\t%s", name);

		list_append(user_list, user);
	}
	list_iterator_destroy(itr);

	printf(" Settings =\n");
	printf("  Default Account = %s\n", default_acct);
	
	if(expedite != ACCOUNT_EXPEDITE_NOTSET)
		printf("  Expedite        = %s\n", 
		       account_expedite_str(expedite));
	
	if(admin_level != ACCOUNT_ADMIN_NOTSET)
		printf("  Admin Level     = %s\n", 
		       account_admin_level_str(admin_level));
	
	if(execute_flag) {
		rc = account_storage_g_add_users(user_list);
		list_destroy(user_list);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_USER_CREATE;
		action->list = list_create(destroy_account_user_rec);
		list_push(action_list, action);
	}

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
	
	_set_cond(&i, argc, argv, user_cond);

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

extern int sacctmgr_modify_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_user_cond_t *user_cond = xmalloc(sizeof(account_user_cond_t));
	account_user_rec_t *user = xmalloc(sizeof(account_user_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0;

	user_cond->user_list = list_create(destroy_char);
	user_cond->def_account_list = list_create(destroy_char);
	
	for (i=0; i<argc; i++) {
		if (strncasecmp (argv[i], "Where", 5) == 0) {
			i++;
			if(_set_cond(&i, argc, argv, user_cond))
				cond_set = 1;
		} else if (strncasecmp (argv[i], "Set", 3) == 0) {
			i++;
			if(_set_rec(&i, argc, argv, user))
				rec_set = 1;
		} else {
			if(_set_cond(&i, argc, argv, user_cond))
				cond_set = 1;
		}
	}

	if(!rec_set) {
		printf(" You didn't give me anything to set\n");
		destroy_account_user_cond(user_cond);
		destroy_account_user_rec(user);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			return SLURM_SUCCESS;
		}		
	}

	printf(" Setting\n");
	_print_rec(user);
	printf("\n Where\n");
	_print_cond(user_cond);

	if(execute_flag) {
		rc = account_storage_g_modify_users(user_cond, user);
		destroy_account_user_cond(user_cond);
		destroy_account_user_rec(user);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_USER_MODIFY;
		action->cond = user_cond;
		action->rec = user;
		list_push(action_list, action);
	}

	return rc;
}

extern int sacctmgr_delete_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	account_user_cond_t *user_cond = xmalloc(sizeof(account_user_cond_t));
	int i=0;

	user_cond->user_list = list_create(destroy_char);
	user_cond->def_account_list = list_create(destroy_char);
	
	if(!_set_cond(&i, argc, argv, user_cond)) {
		printf(" No conditions given to remove, not executing.\n");
		return SLURM_ERROR;
	}

	printf(" Deleting users where...");
	_print_cond(user_cond);

	if(execute_flag) {
		rc = account_storage_g_remove_users(user_cond);
		destroy_account_user_cond(user_cond);
	} else {
		sacctmgr_action_t *action = xmalloc(sizeof(sacctmgr_action_t));
		action->type = SACCTMGR_USER_DELETE;
		action->cond = user_cond;
		list_push(action_list, action);
	}

	return rc;
}
