/*****************************************************************************\
 *  problem_functions.c - functions dealing with problems in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
static bool tree_display = 0;

static int _set_cond(int *start, int argc, char *argv[],
		     acct_association_cond_t *assoc_cond,
		     List format_list)
{
	int i, end = 0;
	int set = 0;
	List qos_list = NULL;
	int command_len = 0;
	int option = 0;
	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if (!end && !strncasecmp (argv[i], "Tree", 
					  MAX(command_len, 4))) {
			tree_display = 1;
		} else if(!end && !strncasecmp(argv[i], "where", 
					       MAX(command_len, 5))) {
			continue;
		} else if(!end || !strncasecmp (argv[i], "Ids", 
						MAX(command_len, 1))
			  || !strncasecmp (argv[i], "Problems", 
					   MAX(command_len, 2))) {
			if(!assoc_cond->id_list)
				assoc_cond->id_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->id_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Accounts",
					 MAX(command_len, 2))
			   || !strncasecmp (argv[i], "Acct",
					    MAX(command_len, 4))) {
			if(!assoc_cond->acct_list)
				assoc_cond->acct_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Clusters",
					 MAX(command_len, 1))) {
			if(!assoc_cond->cluster_list)
				assoc_cond->cluster_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if(format_list)
				slurm_addto_char_list(format_list,
						      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Partitions", 
					 MAX(command_len, 4))) {
			if(!assoc_cond->partition_list)
				assoc_cond->partition_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->partition_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Parent",
					 MAX(command_len, 4))) {
			if(!assoc_cond->parent_acct_list) {
				assoc_cond->parent_acct_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(assoc_cond->parent_acct_list,
						 argv[i]+end))
			set = 1;
		} else if (!strncasecmp (argv[i], "Users",
					 MAX(command_len, 1))) {
			if(!assoc_cond->user_list)
				assoc_cond->user_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->user_list,
					argv[i]+end);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}
	if(qos_list)
		list_destroy(qos_list);

	(*start) = i;

	return set;
}

extern int sacctmgr_list_problem(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	List assoc_list = NULL;
	acct_association_rec_t *assoc = NULL;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *object = NULL;
	char *print_acct = NULL, *last_cluster = NULL;
	List tree_list = NULL;
	List qos_list = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_CLUSTER,
		PRINT_PROBLEM,
		PRINT_USER,
	};

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3))) 
			i++;		
		_set_cond(&i, argc, argv, assoc_cond, format_list);
	}

	if(exit_code) {
		destroy_acct_association_cond(assoc_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	} else if(!list_count(format_list)) 
		slurm_addto_char_list(format_list, "C,A,U,Problem");

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		int command_len = 0;
		int newlen = 0;
		
		if((tmp_char = strstr(object, "\%"))) {
			newlen = atoi(tmp_char+1);
			tmp_char[0] = '\0';
		} 

		command_len = strlen(object);
	
		field = xmalloc(sizeof(print_field_t));

		if(!strncasecmp("Account", object, MAX(command_len, 1))
		   || !strncasecmp("Acct", object, MAX(command_len, 4))) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			if(tree_display)
				field->len = -20;
			else
				field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Problem", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_PROBLEM;
			field->name = xstrdup("Problem");
			field->len = 40;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("User", object, MAX(command_len, 1))) {
			field->type = PRINT_USER;
			field->name = xstrdup("User");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
			exit(1);
			xfree(field);
			continue;
		}

		if(newlen) 
			field->len = newlen;
		
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if(exit_code) {
		destroy_acct_association_cond(assoc_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	assoc_list = acct_storage_g_get_problems(db_conn, my_uid, assoc_cond);
	destroy_acct_association_cond(assoc_cond);

	if(!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((assoc = list_next(itr))) {
		int curr_inx = 1;
		if(!last_cluster || strcmp(last_cluster, assoc->cluster)) {
			if(tree_list) {
				list_flush(tree_list);
			} else {
				tree_list = 
					list_create(destroy_acct_print_tree);
			}
			last_cluster = assoc->cluster;
		} 
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_ACCOUNT:
				if(tree_display) {
					char *local_acct = NULL;
					char *parent_acct = NULL;
					if(assoc->user) {
						local_acct = xstrdup_printf(
							"|%s", assoc->acct);
						parent_acct = assoc->acct;
					} else {
						local_acct =
							xstrdup(assoc->acct);
						parent_acct = 
							assoc->parent_acct;
					}
					print_acct = get_tree_acct_name(
						local_acct,
						parent_acct, tree_list);
					xfree(local_acct);
				} else {
					print_acct = assoc->acct;
				}
				field->print_routine(
					field, 
					print_acct,
					(curr_inx == field_count));
				break;
			case PRINT_CLUSTER:
				field->print_routine(
					field,
					assoc->cluster,
					(curr_inx == field_count));
				break;
			case PRINT_PROBLEM:
				/* make some sort of string here to
				   print out the problem reported.
				   Maybe make an array or something
				   and just print out a standard error.
				*/
				field->print_routine(
					field,
					NULL,
					(curr_inx == field_count));
				break;
			case PRINT_USER:
				field->print_routine(field, 
						     assoc->user,
						     (curr_inx == field_count));
				break;
			default:
				field->print_routine(
					field, NULL,
					(curr_inx == field_count));
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	if(qos_list)
		list_destroy(qos_list);

	if(tree_list) 
		list_destroy(tree_list);
			
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(assoc_list);
	list_destroy(print_fields_list);
	return rc;
}
