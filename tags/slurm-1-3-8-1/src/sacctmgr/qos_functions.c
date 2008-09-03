/*****************************************************************************\
 *  qos_functions.c - functions dealing with qoss in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2002-2008 The Regents of the University of California.
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

static int _set_cond(int *start, int argc, char *argv[],
		     acct_qos_cond_t *qos_cond,
		     List format_list)
{
	int i;
	int set = 0;
	int end = 0;

	if(!qos_cond) {
		error("No qos_cond given");
		return -1;
	}

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!strncasecmp (argv[i], "Set", 3)) {
			i--;
		} else if (!strncasecmp (argv[i], "WithDeleted", 5)) {
			qos_cond->with_deleted = 1;
		} else if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Names", 1)
			  || !strncasecmp (argv[i], "QOS", 1)) {
			if(!qos_cond->name_list) {
				qos_cond->name_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(qos_cond->name_list,
						 argv[i]+end))
				set = 1;
		} else if(!strncasecmp (argv[i], "Descriptions", 1)) {
			if(!qos_cond->description_list) {
				qos_cond->description_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(qos_cond->description_list,
						 argv[i]+end))
				set = 1;
		} else if(!strncasecmp (argv[i], "Ids", 1)) {
			if(!qos_cond->id_list) {
				qos_cond->id_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(qos_cond->id_list, 
						 argv[i]+end))
				set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify "
				"SLURM_PRINT_VALUE\n", argv[i]);
		}
	}

	(*start) = i;

	return set;
}

/* static int _set_rec(int *start, int argc, char *argv[], */
/* 		    acct_qos_rec_t *qos) */
/* { */
/* 	int i; */
/* 	int set = 0; */
/* 	int end = 0; */

/* 	for (i=(*start); i<argc; i++) { */
/* 		end = parse_option_end(argv[i]); */
/* 		if (!strncasecmp (argv[i], "Where", 5)) { */
/* 			i--; */
/* 			break; */
/* 		} else if(!end && !strncasecmp(argv[i], "set", 3)) { */
/* 			continue; */
/* 		} else if(!end) { */
/* 			printf(" Bad format on %s: End your option with " */
/* 			       "an '=' sign\n", argv[i]); */
/* 		} else if (!strncasecmp (argv[i], "Description", 1)) { */
/* 			if(!qos->description) */
/* 				qos->description = */
/* 					strip_quotes(argv[i]+end, NULL); */
/* 			set = 1; */
/* 		} else if (!strncasecmp (argv[i], "Name", 1)) { */
/* 			if(!qos->name) */
/* 				qos->name = strip_quotes(argv[i]+end, NULL); */
/* 			set = 1; */
/* 		} else { */
/* 			printf(" Unknown option: %s\n" */
/* 			       " Use keyword 'where' to modify condition\n", */
/* 			       argv[i]); */
/* 		} */
/* 	} */

/* 	(*start) = i; */

/* 	return set; */
/* } */

extern int sacctmgr_add_qos(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	ListIterator itr = NULL;
	acct_qos_rec_t *qos = NULL;
	List name_list = list_create(slurm_destroy_char);
	char *description = NULL;
	char *name = NULL;
	List qos_list = NULL;
	List local_qos_list = NULL;
	char *qos_str = NULL;
	
	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if(!end || !strncasecmp (argv[i], "Names", 1)) {
			slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Description", 1)) {
			description = strip_quotes(argv[i]+end, NULL);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}		
	}

	if(exit_code) {
		list_destroy(name_list);
		xfree(description);
		return SLURM_ERROR;
	} else if(!list_count(name_list)) {
		list_destroy(name_list);
		xfree(description);
		exit_code=1;
		fprintf(stderr, " Need name of qos to add.\n"); 
		return SLURM_SUCCESS;
	} 


	local_qos_list = acct_storage_g_get_qos(db_conn, my_uid, NULL);

	if(!local_qos_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting qos's from database.  "
		       "Contact your admin.\n");
		list_destroy(name_list);
		xfree(description);
		return SLURM_ERROR;
	}

	qos_list = list_create(destroy_acct_qos_rec);

	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		qos = NULL;
		if(!sacctmgr_find_qos_from_list(local_qos_list, name)) {
			qos = xmalloc(sizeof(acct_qos_rec_t));
			qos->name = xstrdup(name);
			if(description) 
				qos->description = xstrdup(description);
			else
				qos->description = xstrdup(name);

			xstrfmtcat(qos_str, "  %s\n", name);
			list_append(qos_list, qos);
		}
	}
	list_iterator_destroy(itr);
	list_destroy(local_qos_list);
	list_destroy(name_list);
	
	if(!list_count(qos_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	} 

	if(qos_str) {
		printf(" Adding QOS(s)\n%s", qos_str);
		printf(" Settings\n");
		if(description)
			printf("  Description     = %s\n", description);
		else
			printf("  Description     = %s\n", "QOS Name");
		xfree(qos_str);
	}
	
	notice_thread_init();
	if(list_count(qos_list)) 
		rc = acct_storage_g_add_qos(db_conn, my_uid, qos_list);
	else 
		goto end_it;

	notice_thread_fini();
	
	if(rc == SLURM_SUCCESS) {
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding QOS.\n");
		rc = SLURM_ERROR;
	}

end_it:
	list_destroy(qos_list);
	xfree(description);

	return rc;
}

extern int sacctmgr_list_qos(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_qos_cond_t *qos_cond = xmalloc(sizeof(acct_qos_cond_t));
 	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	acct_qos_rec_t *qos = NULL;
	char *object;
	List qos_list = NULL;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_DESC,
		PRINT_ID,
		PRINT_NAME
	};

	_set_cond(&i, argc, argv, qos_cond, format_list);

	if(exit_code) {
		destroy_acct_txn_cond(qos_cond);
		list_destroy(format_list);		
		return SLURM_ERROR;
	} else if(!list_count(format_list)) {
		slurm_addto_char_list(format_list, "N");
	}

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Description", object, 1)) {
			field->type = PRINT_DESC;
			field->name = xstrdup("Descr");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("Name", object, 1)) {
			field->type = PRINT_NAME;
			field->name = xstrdup("NAME");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if(exit_code) {
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}
	qos_list = acct_storage_g_get_qos(db_conn, my_uid, qos_cond);	
	destroy_acct_qos_cond(qos_cond);

	if(!qos_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}
	itr = list_iterator_create(qos_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	while((qos = list_next(itr))) {
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_DESC:
				field->print_routine(
					field, qos->description);
				break;
			case PRINT_ID:
				field->print_routine(
					field, qos->id);
				break;
			case PRINT_NAME:
				field->print_routine(
					field, qos->name);
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
	list_destroy(qos_list);
	list_destroy(print_fields_list);

	return rc;
}

extern int sacctmgr_delete_qos(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_qos_cond_t *qos_cond =
		xmalloc(sizeof(acct_qos_cond_t));
	int i=0;
	List ret_list = NULL;
	int set = 0;
	
	if(!(set = _set_cond(&i, argc, argv, qos_cond, NULL))) {
		exit_code=1;
		fprintf(stderr, 
			" No conditions given to remove, not executing.\n");
		destroy_acct_qos_cond(qos_cond);
		return SLURM_ERROR;
	} else if(set == -1) {
		destroy_acct_qos_cond(qos_cond);
		return SLURM_ERROR;
	}

	notice_thread_init();
	ret_list = acct_storage_g_remove_qos(db_conn, my_uid, qos_cond);
	notice_thread_fini();
	destroy_acct_qos_cond(qos_cond);
	
	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Deleting QOS(s)...\n");
		
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if(commit_check("Would you like to commit changes?")) {
			acct_storage_g_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	} else if(ret_list) {
		printf(" Nothing deleted\n");
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request\n");
		rc = SLURM_ERROR;
	} 

	if(ret_list)
		list_destroy(ret_list);

	return rc;
}
