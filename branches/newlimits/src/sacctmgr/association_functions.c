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
bool tree_display = 0;

typedef struct {
	char *name;
	char *print_name;
	char *spaces;
} print_acct_t;

static void _destroy_print_acct(void *object)
{
	print_acct_t *print_acct = (print_acct_t *)object;
	if(print_acct) {
		xfree(print_acct->name);
		xfree(print_acct->print_name);
		xfree(print_acct->spaces);
		xfree(print_acct);
	}
}

static char *_get_print_acct_name(char *name, char *parent, char *cluster, 
				  List tree_list)
{
	ListIterator itr = NULL;
	print_acct_t *print_acct = NULL;
	print_acct_t *par_print_acct = NULL;
	static char *ret_name = NULL;
	static char *last_name = NULL, *last_cluster = NULL;


	if(!tree_list) {
		return NULL;
	}
	
	itr = list_iterator_create(tree_list);
	while((print_acct = list_next(itr))) {
		if(!strcmp(name, print_acct->name)) {
			ret_name = print_acct->print_name;
			break;
		} else if(parent && !strcmp(parent, print_acct->name)) {
			par_print_acct = print_acct;
		}
	}
	list_iterator_destroy(itr);
	
	if(parent && print_acct) {
		return ret_name;
	} 

	print_acct = xmalloc(sizeof(print_acct_t));
	print_acct->name = xstrdup(name);
	if(par_print_acct) {
		print_acct->spaces =
			xstrdup_printf(" %s", par_print_acct->spaces);
	} else {
		print_acct->spaces = xstrdup("");
	}

	/* user account */
	if(name[0] == '|')
		print_acct->print_name = xstrdup_printf("%s%s", 
							print_acct->spaces, 
							parent);	
	else
		print_acct->print_name = xstrdup_printf("%s%s", 
							print_acct->spaces, 
							name);	
	

	list_append(tree_list, print_acct);

	ret_name = print_acct->print_name;
	last_name = name;
	last_cluster = cluster;

	return print_acct->print_name;
}

static int _set_cond(int *start, int argc, char *argv[],
		     acct_association_cond_t *association_cond,
		     List format_list)
{
	int i, end = 0;
	int set = 0;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end && !strncasecmp (argv[i], "Tree", 4)) {
			tree_display = 1;
		} else if (!end && !strncasecmp (argv[i], "WithDeleted", 5)) {
			association_cond->with_deleted = 1;
		} else if (!end && !strncasecmp (argv[i], "WOPInfo", 4)) {
			association_cond->without_parent_info = 1;
		} else if (!end && !strncasecmp (argv[i], "WOPLimits", 4)) {
			association_cond->without_parent_limits = 1;
		} else if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} else if(!end || !strncasecmp (argv[i], "Id", 1)
			  || !strncasecmp (argv[i], "Associations", 2)) {
			if(!association_cond->id_list)
				association_cond->id_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(association_cond->id_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Users", 1)) {
			if(!association_cond->user_list)
				association_cond->user_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(association_cond->user_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Accounts", 2)) {
			if(!association_cond->acct_list)
				association_cond->acct_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(association_cond->acct_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Clusters", 1)) {
			if(!association_cond->cluster_list)
				association_cond->cluster_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(association_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format", 1)) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Partitions", 4)) {
			if(!association_cond->partition_list)
				association_cond->partition_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(association_cond->partition_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Parent", 4)) {
			xfree(association_cond->parent_acct);
			association_cond->parent_acct =
				strip_quotes(argv[i]+end, NULL);
			set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n", argv[i]);
		}
	}
	(*start) = i;

	return set;
}

/* 
 * Comparator used for sorting immediate childern of sacctmgr_assocs
 * 
 * returns: -1: assoc_a > assoc_b   0: assoc_a == assoc_b   1: assoc_a < assoc_b
 * 
 */

static int _sort_childern_list(sacctmgr_assoc_t *assoc_a,
			       sacctmgr_assoc_t *assoc_b)
{
	int diff = 0;

	/* first just check the lfts and rgts if a lft is inside of the
	 * others lft and rgt just return it is less
	 */ 
	if(assoc_a->assoc->lft > assoc_b->assoc->lft 
	   && assoc_a->assoc->lft < assoc_b->assoc->rgt)
		return 1;

	/* check to see if this is a user association or an account.
	 * We want the accounts at the bottom 
	 */
	if(assoc_a->assoc->user && !assoc_b->assoc->user)
		return -1;
	else if(!assoc_a->assoc->user && assoc_b->assoc->user)
		return 1;

	diff = strcmp(assoc_a->sort_name, assoc_b->sort_name);
	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;
	
	return 0;

}

static int _sort_sacctmgr_assoc_list(List sacctmgr_assoc_list)
{
	sacctmgr_assoc_t *sacctmgr_assoc = NULL;
	ListIterator itr;

	if(!list_count(sacctmgr_assoc_list))
		return SLURM_SUCCESS;

	list_sort(sacctmgr_assoc_list, (ListCmpF)_sort_childern_list);

	itr = list_iterator_create(sacctmgr_assoc_list);
	while((sacctmgr_assoc = list_next(itr))) {
		if(list_count(sacctmgr_assoc->childern))
			_sort_sacctmgr_assoc_list(sacctmgr_assoc->childern);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _append_ret_list(List ret_list, List sacctmgr_assoc_list)
{
	sacctmgr_assoc_t *sacctmgr_assoc = NULL;
	ListIterator itr;

	if(!ret_list)
		return SLURM_ERROR;

	if(!list_count(sacctmgr_assoc_list))
		return SLURM_SUCCESS;

	itr = list_iterator_create(sacctmgr_assoc_list);
	while((sacctmgr_assoc = list_next(itr))) {
		list_append(ret_list, sacctmgr_assoc->assoc);

		if(list_count(sacctmgr_assoc->childern)) 
			_append_ret_list(ret_list, sacctmgr_assoc->childern);
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static List _sort_assoc_list(List assoc_list)
{
	List sacctmgr_assoc_list = sacctmgr_get_hierarchical_list(assoc_list);
	List ret_list = list_create(NULL);

	_append_ret_list(ret_list, sacctmgr_assoc_list);
	list_destroy(sacctmgr_assoc_list);
	
	return ret_list;
}

extern List sacctmgr_get_hierarchical_list(List assoc_list)
{
	sacctmgr_assoc_t *par_sacctmgr_assoc = NULL;
	sacctmgr_assoc_t *sacctmgr_assoc = NULL;
	acct_association_rec_t *assoc = NULL;
	List total_assoc_list = list_create(NULL);
	List sacctmgr_assoc_list = list_create(destroy_sacctmgr_assoc);
	ListIterator itr, itr2;

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(total_assoc_list);
	
	while((assoc = list_next(itr))) {
		sacctmgr_assoc = xmalloc(sizeof(sacctmgr_assoc_t));
		sacctmgr_assoc->childern = list_create(destroy_sacctmgr_assoc);
		sacctmgr_assoc->assoc = assoc;
	
		if(!assoc->parent_id) {
			sacctmgr_assoc->sort_name = assoc->cluster;

			list_append(sacctmgr_assoc_list, sacctmgr_assoc);
			list_append(total_assoc_list, sacctmgr_assoc);

			list_iterator_reset(itr2);
			continue;
		}

		while((par_sacctmgr_assoc = list_next(itr2))) {
			if(assoc->parent_id == par_sacctmgr_assoc->assoc->id) 
				break;
		}

		if(assoc->user)
			sacctmgr_assoc->sort_name = assoc->user;
		else
			sacctmgr_assoc->sort_name = assoc->acct;

		if(!par_sacctmgr_assoc) 
			list_append(sacctmgr_assoc_list, sacctmgr_assoc);
		else
			list_append(par_sacctmgr_assoc->childern,
				    sacctmgr_assoc);

		list_append(total_assoc_list, sacctmgr_assoc);
		list_iterator_reset(itr2);
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	list_destroy(total_assoc_list);
//	info("got %d", list_count(sacctmgr_assoc_list));
	_sort_sacctmgr_assoc_list(sacctmgr_assoc_list);

	return sacctmgr_assoc_list;
}

extern int sacctmgr_list_association(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	List assoc_list = NULL;
	List first_list = NULL;
	acct_association_rec_t *assoc = NULL;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	char *object = NULL;
	char *print_acct = NULL, *last_cluster = NULL;
	List tree_list = NULL;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_CLUSTER,
		PRINT_FAIRSHARE,
		PRINT_ID,
		PRINT_LFT,
		PRINT_MAXC,
		PRINT_MAXJ,
		PRINT_MAXN,
		PRINT_MAXW,
		PRINT_PID,
		PRINT_PNAME,
		PRINT_PART,
		PRINT_RGT,
		PRINT_USER
	};

	_set_cond(&i, argc, argv, assoc_cond, format_list);

	if(exit_code) {
		destroy_acct_association_cond(assoc_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	} else if(!list_count(format_list)) 
		slurm_addto_char_list(format_list,
				      "C,A,U,Part,F,MaxC,MaxJ,MaxN,MaxW");

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, 1)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			if(tree_display)
				field->len = 20;
			else
				field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object, 1)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("FairShare", object, 1)) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("FairShare");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("ID", object, 1)) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("LFT", object, 1)) {
			field->type = PRINT_LFT;
			field->name = xstrdup("LFT");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxCPUMins", object, 4)
			  || !strncasecmp("MaxProcSecsPerJob", object, 4)) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("MaxJobs", object, 4)) {
			field->type = PRINT_MAXJ;
			field->name = xstrdup("MaxJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxNodes", object, 4)) {
			field->type = PRINT_MAXN;
			field->name = xstrdup("MaxNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxWall", object, 4)) {
			field->type = PRINT_MAXW;
			field->name = xstrdup("MaxWall");
			field->len = 11;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("ParentID", object, 7)) {
			field->type = PRINT_PID;
			field->name = xstrdup("Par ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("ParentName", object, 7)) {
			field->type = PRINT_PNAME;
			field->name = xstrdup("Par Name");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Partition", object, 4)) {
			field->type = PRINT_PART;
			field->name = xstrdup("Partition");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("RGT", object, 1)) {
			field->type = PRINT_RGT;
			field->name = xstrdup("RGT");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("User", object, 1)) {
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
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);
	list_destroy(format_list);

	if(exit_code) {
		destroy_acct_association_cond(assoc_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     assoc_cond);
	destroy_acct_association_cond(assoc_cond);

	if(!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}
	first_list = assoc_list;
	assoc_list = _sort_assoc_list(first_list);

	itr = list_iterator_create(assoc_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	while((assoc = list_next(itr))) {
		if(!last_cluster || strcmp(last_cluster, assoc->cluster)) {
			if(tree_list) {
				list_flush(tree_list);
			} else {
				tree_list = list_create(_destroy_print_acct);
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
					print_acct = _get_print_acct_name(
						local_acct,
						parent_acct,
						assoc->cluster, tree_list);
					xfree(local_acct);
				} else {
					print_acct = assoc->acct;
				}
				field->print_routine(field, 
						     print_acct);
				break;
			case PRINT_CLUSTER:
				field->print_routine(field,
						     assoc->cluster);
				break;
			case PRINT_FAIRSHARE:
				field->print_routine(field,
						     assoc->fairshare);
				break;
			case PRINT_ID:
				field->print_routine(field, 
						     assoc->id);
				break;
			case PRINT_LFT:
				field->print_routine(field, 
						     assoc->lft);
				break;
			case PRINT_MAXC:
				field->print_routine(
					field,
					assoc->max_cpu_mins_pj);
				break;
			case PRINT_MAXJ:
				field->print_routine(field, 
						     assoc->max_jobs);
				break;
			case PRINT_MAXN:
				field->print_routine(field,
						     assoc->max_nodes_pj);
				break;
			case PRINT_MAXW:
				field->print_routine(
					field,
					assoc->max_wall_pj);
				break;
			case PRINT_PID:
				field->print_routine(field,
						     assoc->parent_id);
				break;
			case PRINT_PNAME:
				field->print_routine(field,
						     assoc->parent_acct);
				break;
			case PRINT_PART:
				field->print_routine(field,
						     assoc->partition);
				break;
			case PRINT_RGT:
				field->print_routine(field, 
						     assoc->rgt);
				break;
			case PRINT_USER:
				field->print_routine(field, 
						     assoc->user);
				break;
			default:
				break;
			}
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	if(tree_list) 
		list_destroy(tree_list);
			
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(first_list);
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
