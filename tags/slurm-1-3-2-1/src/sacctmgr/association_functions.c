/*****************************************************************************\
 *  association_functions.c - functions dealing with associations in the
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
#include "src/sacctmgr/print.h"

static int _set_cond(int *start, int argc, char *argv[],
		     acct_association_cond_t *association_cond,
		     List format_list)
{
	int i, end = 0;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end) {
			addto_char_list(association_cond->id_list, argv[i]);
			set = 1;
		} else if (strncasecmp (argv[i], "Id", 1) == 0) {
			addto_char_list(association_cond->id_list, argv[i]+end);
			set = 1;
		}  else if (strncasecmp (argv[i], "Associations", 2) == 0) {
			addto_char_list(association_cond->id_list, argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Users", 1) == 0) {
			addto_char_list(association_cond->user_list,
					argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Accounts", 2) == 0) {
			addto_char_list(association_cond->acct_list,
					argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Clusters", 1) == 0) {
			addto_char_list(association_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Format", 1) == 0) {
			if(format_list)
				addto_char_list(format_list, argv[i]+end);
		} else if (strncasecmp (argv[i], "Partitions", 4) == 0) {
			addto_char_list(association_cond->partition_list,
					argv[i]+end);
			set = 1;
		} else if (strncasecmp (argv[i], "Parent", 4) == 0) {
			association_cond->parent_acct =
				strip_quotes(argv[i]+end, NULL);
			set = 1;
		} else {
			printf(" Unknown condition: %s\n", argv[i]);
		}
	}
	(*start) = i;

	return set;
}

/* static void _print_cond(acct_association_cond_t *association_cond) */
/* { */
/* 	ListIterator itr = NULL; */
/* 	char *tmp_char = NULL; */

/* 	if(!association_cond) { */
/* 		error("no acct_association_cond_t * given"); */
/* 		return; */
/* 	} */

/* 	if(association_cond->id_list && list_count(association_cond->id_list)) { */
/* 		itr = list_iterator_create(association_cond->id_list); */
/* 		printf("  Id        = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("           or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(association_cond->user_list */
/* 	   && list_count(association_cond->user_list)) { */
/* 		itr = list_iterator_create(association_cond->user_list); */
/* 		printf("  User      = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("           or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(association_cond->acct_list */
/* 	   && list_count(association_cond->acct_list)) { */
/* 		itr = list_iterator_create(association_cond->acct_list); */
/* 		printf("  Account   = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("           or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(association_cond->cluster_list */
/* 	   && list_count(association_cond->cluster_list)) { */
/* 		itr = list_iterator_create(association_cond->cluster_list); */
/* 		printf("  Cluster   = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("           or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(association_cond->partition_list */
/* 	   && list_count(association_cond->partition_list)) { */
/* 		itr = list_iterator_create(association_cond->partition_list); */
/* 		printf("  Partition = %s\n", (char *)list_next(itr)); */
/* 		while((tmp_char = list_next(itr))) { */
/* 			printf("           or %s\n", tmp_char); */
/* 		} */
/* 	} */

/* 	if(association_cond->parent_account) */
/* 		printf("  Parent    = %s\n", association_cond->parent_account); */

/* } */

/* static void _print_rec(acct_association_rec_t *association) */
/* { */
/* 	if(!association) { */
/* 		error("no acct_association_rec_t * given"); */
/* 		return; */
/* 	} */
	
/* 	if(association->id)  */
/* 		printf("  Id         = %u\n", association->id);	 */
		
/* 	if(association->user)  */
/* 		printf("  User       = %s\n", association->user); */
/* 	if(association->account)  */
/* 		printf("  Account    = %s\n", association->account); */
/* 	if(association->cluster)  */
/* 		printf("  Cluster    = %s\n", association->cluster); */
/* 	if(association->partition)  */
/* 		printf("  Partition  = %s\n", association->partition); */
/* 	if(association->parent_account)  */
/* 		printf("  Parent     = %s\n", association->parent_account); */
/* 	if(association->fairshare)  */
/* 		printf("  FairShare  = %u\n", association->fairshare); */
/* 	if(association->max_jobs)  */
/* 		printf("  MaxJobs    = %u\n", association->max_jobs); */
/* 	if(association->max_nodes_per_job)  */
/* 		printf("  MaxNodes   = %u\n", association->max_nodes_per_job); */
/* 	if(association->max_wall_duration_per_job) { */
/* 		char time_buf[32]; */
/* 		mins2time_str((time_t) association->max_wall_duration_per_job, */
/* 			      time_buf, sizeof(time_buf)); */
/* 		printf("  MaxWall    = %s\n", time_buf); */
/* 	} */
/* 	if(association->max_cpu_seconds_per_job)  */
/* 		printf("  MaxCPUSecs = %u\n", */
/* 		       association->max_cpu_seconds_per_job); */
/* } */

/* extern int sacctmgr_add_association(int argc, char *argv[]) */
/* { */
/* 	int rc = SLURM_SUCCESS; */

/* 	return rc; */
/* } */

extern int sacctmgr_list_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	List assoc_list = NULL;
	acct_association_rec_t *assoc = NULL;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *object;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_CLUSTER,
		PRINT_FAIRSHARE,
		PRINT_ID,
		PRINT_MAXC,
		PRINT_MAXJ,
		PRINT_MAXN,
		PRINT_MAXW,
		PRINT_PID,
		PRINT_PNAME,
		PRINT_PART,
		PRINT_USER
	};

	assoc_cond->id_list = list_create(slurm_destroy_char);
	assoc_cond->user_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(slurm_destroy_char);
	assoc_cond->cluster_list = list_create(slurm_destroy_char);

	_set_cond(&i, argc, argv, assoc_cond, format_list);

	assoc_list = acct_storage_g_get_associations(db_conn, assoc_cond);
	destroy_acct_association_cond(assoc_cond);
	
	if(!assoc_list) {
		list_destroy(format_list);
		return SLURM_ERROR;
	}
	print_fields_list = list_create(destroy_print_field);

	if(!list_count(format_list)) 
		addto_char_list(format_list, "C,A,U,F,MaxC,MaxJ,MaxN,MaxW");
	
	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, 1)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 10;
			field->print_routine = print_str;
		} else if(!strncasecmp("Cluster", object, 1)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_str;
		} else if(!strncasecmp("FairShare", object, 1)) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("FairShare");
			field->len = 9;
			field->print_routine = print_uint;
		} else if(!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_uint;
		} else if(!strncasecmp("MaxCPUSecs", object, 4)) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUSecs");
			field->len = 11;
			field->print_routine = print_uint;
		} else if(!strncasecmp("MaxJobs", object, 4)) {
			field->type = PRINT_MAXJ;
			field->name = xstrdup("MaxJobs");
			field->len = 7;
			field->print_routine = print_uint;
		} else if(!strncasecmp("MaxNodes", object, 4)) {
			field->type = PRINT_MAXN;
			field->name = xstrdup("MaxNodes");
			field->len = 8;
			field->print_routine = print_uint;
		} else if(!strncasecmp("MaxWall", object, 4)) {
			field->type = PRINT_MAXW;
			field->name = xstrdup("MaxWall");
			field->len = 11;
			field->print_routine = print_time;
		} else if(!strncasecmp("ParentID", object, 7)) {
			field->type = PRINT_PID;
			field->name = xstrdup("Par ID");
			field->len = 6;
			field->print_routine = print_uint;
		} else if(!strncasecmp("ParentName", object, 7)) {
			field->type = PRINT_PNAME;
			field->name = xstrdup("Par Name");
			field->len = 10;
			field->print_routine = print_str;
		} else if(!strncasecmp("Partition", object, 4)) {
			field->type = PRINT_PART;
			field->name = xstrdup("Partition");
			field->len = 10;
			field->print_routine = print_str;
		} else if(!strncasecmp("User", object, 1)) {
			field->type = PRINT_USER;
			field->name = xstrdup("User");
			field->len = 10;
			field->print_routine = print_str;
		} else {
			printf("Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(print_fields_list);
	print_header(print_fields_list);

	while((assoc = list_next(itr))) {
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_ACCOUNT:
				field->print_routine(SLURM_PRINT_VALUE, field, 
						     assoc->acct);
				break;
			case PRINT_CLUSTER:
				field->print_routine(SLURM_PRINT_VALUE, field,
						     assoc->cluster);
				break;
			case PRINT_FAIRSHARE:
				field->print_routine(SLURM_PRINT_VALUE, field,
						     assoc->fairshare);
				break;
			case PRINT_ID:
				field->print_routine(SLURM_PRINT_VALUE, field, 
						     assoc->id);
				break;
			case PRINT_MAXC:
				field->print_routine(
					SLURM_PRINT_VALUE, field,
					assoc->max_cpu_secs_per_job);
				break;
			case PRINT_MAXJ:
				field->print_routine(SLURM_PRINT_VALUE, field, 
						     assoc->max_jobs);
				break;
			case PRINT_MAXN:
				field->print_routine(SLURM_PRINT_VALUE, field,
						     assoc->max_nodes_per_job);
				break;
			case PRINT_MAXW:
				field->print_routine(
					SLURM_PRINT_VALUE, field,
					assoc->max_wall_duration_per_job);
				break;
			case PRINT_PID:
				field->print_routine(SLURM_PRINT_VALUE, field,
						     assoc->parent_id);
				break;
			case PRINT_PNAME:
				field->print_routine(SLURM_PRINT_VALUE, field,
						     assoc->parent_acct);
				break;
			case PRINT_PART:
				field->print_routine(SLURM_PRINT_VALUE, field,
						     assoc->partition);
				break;
			case PRINT_USER:
				field->print_routine(SLURM_PRINT_VALUE, field, 
						     assoc->user);
				break;
			default:
				break;
			}
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	printf("\n");

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(assoc_list);
	list_destroy(print_fields_list);
	return rc;
}

/* extern int sacctmgr_modify_association(int argc, char *argv[]) */
/* { */
/* 	int rc = SLURM_SUCCESS; */
/* 	return rc; */
/* } */

/* extern int sacctmgr_delete_association(int argc, char *argv[]) */
/* { */
/* 	int rc = SLURM_SUCCESS; */
/* 	return rc; */
/* } */
