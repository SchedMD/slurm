/*****************************************************************************\
 *  txn_functions.c - functions dealing with transactions in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/slurmdbd_defs.h"

static int _set_cond(int *start, int argc, char *argv[],
		     acct_txn_cond_t *txn_cond,
		     List format_list)
{
	int i, end = 0;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} if(!end && !strncasecmp(argv[i], "withassocinfo", 5)) {
			txn_cond->with_assoc_info = 1;
			set = 1;
		} else if(!end
			  || (!strncasecmp (argv[i], "Id", 1))
			  || (!strncasecmp (argv[i], "Txn", 1))) {
			if(!txn_cond->id_list)
				txn_cond->id_list = 
					list_create(slurm_destroy_char);
			
			if(slurm_addto_char_list(txn_cond->id_list, 
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Accounts", 3)) {
			if(!txn_cond->acct_list)
				txn_cond->acct_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(txn_cond->acct_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Action", 4)) {
			if(!txn_cond->action_list)
				txn_cond->action_list =
					list_create(slurm_destroy_char);

			if(addto_action_char_list(txn_cond->action_list,
						  argv[i]+end))
				set = 1;
			else
				exit_code=1;
		} else if (!strncasecmp (argv[i], "Actors", 4)) {
			if(!txn_cond->actor_list)
				txn_cond->actor_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(txn_cond->actor_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "Clusters", 3)) {
			if(!txn_cond->cluster_list)
				txn_cond->cluster_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(txn_cond->cluster_list,
						 argv[i]+end))
				set = 1;
		} else if (!strncasecmp (argv[i], "End", 1)) {
			txn_cond->time_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format", 1)) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Start", 1)) {
			txn_cond->time_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "User", 1)) {
			if(!txn_cond->user_list)
				txn_cond->user_list =
					list_create(slurm_destroy_char);
			if(slurm_addto_char_list(txn_cond->user_list,
						 argv[i]+end))
				set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}
	(*start) = i;

	return set;
}


extern int sacctmgr_list_txn(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_txn_cond_t *txn_cond = xmalloc(sizeof(acct_txn_cond_t));
	List txn_list = NULL;
	acct_txn_rec_t *txn = NULL;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *object = NULL;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCT,
		PRINT_ACTION,
		PRINT_ACTOR,
		PRINT_CLUSTER,
		PRINT_ID,
		PRINT_INFO,
		PRINT_TS,
		PRINT_USER,
		PRINT_WHERE
	};

	_set_cond(&i, argc, argv, txn_cond, format_list);

	if(exit_code) {
		destroy_acct_txn_cond(txn_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	}

	print_fields_list = list_create(destroy_print_field);

	if(!list_count(format_list)) {
		slurm_addto_char_list(format_list, "T,Action,Actor,Where,Info");
		if(txn_cond->with_assoc_info) 
			slurm_addto_char_list(format_list, 
					      "User,Account,Cluster");
	}

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Accounts", object, 3)) {
			field->type = PRINT_ACCT;
			field->name = xstrdup("Accounts");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Action", object, 4)) {
			field->type = PRINT_ACTION;
			field->name = xstrdup("Action");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Actor", object, 4)) {
			field->type = PRINT_ACTOR;
			field->name = xstrdup("Actor");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Clusters", object, 4)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Clusters");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("ID", object, 2)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("Info", object, 2)) {
			field->type = PRINT_INFO;
			field->name = xstrdup("Info");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("TimeStamp", object, 1)) {
			field->type = PRINT_TS;
			field->name = xstrdup("Time");
			field->len = 15;
			field->print_routine = print_fields_date;
		} else if(!strncasecmp("Users", object, 4)) {
			field->type = PRINT_USER;
			field->name = xstrdup("Users");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Where", object, 1)) {
			field->type = PRINT_WHERE;
			field->name = xstrdup("Where");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}
		if((tmp_char = strstr(object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if(newlen > 0) 
				field->len = newlen;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if(exit_code) {
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	txn_list = acct_storage_g_get_txn(db_conn, my_uid, txn_cond);
	destroy_acct_txn_cond(txn_cond);

	if(!txn_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}
	itr = list_iterator_create(txn_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	while((txn = list_next(itr))) {
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_ACCT:
				field->print_routine(field, txn->accts);
				break;
			case PRINT_ACTION:
				field->print_routine(
					field, 
					slurmdbd_msg_type_2_str(txn->action,
								0));
				break;
			case PRINT_ACTOR:
				field->print_routine(field,
						     txn->actor_name);
				break;
			case PRINT_CLUSTER:
				field->print_routine(field, txn->clusters);
				break;
			case PRINT_ID:
				field->print_routine(field,
						     txn->id);
				break;
			case PRINT_INFO:
				field->print_routine(field, 
						     txn->set_info);
				break;
			case PRINT_TS:
				field->print_routine(field,
						     txn->timestamp);
				break;
			case PRINT_USER:
				field->print_routine(field, txn->users);
				break;
			case PRINT_WHERE:
				field->print_routine(field, 
						     txn->where_query);
				break;
			default:
				break;
			}
		}
		list_iterator_reset(itr2);
		printf("\n");
	}
			
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(txn_list);
	list_destroy(print_fields_list);
	return rc;
}
