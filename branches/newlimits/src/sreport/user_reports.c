/*****************************************************************************\
 *  user_reports.c - functions for generating user reports
 *                     from accounting infrastructure.
 *****************************************************************************
 *
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "user_reports.h"

enum {
	PRINT_USER_ACCT,
	PRINT_USER_CLUSTER,
	PRINT_USER_LOGIN,
	PRINT_USER_PROPER,
	PRINT_USER_USED
};


typedef struct {
	List acct_list; /* list of char *'s */
	uint64_t cpu_secs;
	char *name;
	uid_t uid;
} local_user_rec_t;

typedef struct {
	uint64_t cpu_secs;
	char *name;
	List user_list; /* list of local_user_rec_t *'s */
} local_cluster_rec_t;

static List print_fields_list = NULL; /* types are of print_field_t */
static bool group_accts = false;
static int top_limit = 10;

static void _destroy_local_user_rec(void *object)
{
	local_user_rec_t *local_user = (local_user_rec_t *)object;
	if(local_user) {
		if(local_user->acct_list)
			list_destroy(local_user->acct_list);
		xfree(local_user);
	}
}

static void _destroy_local_cluster_rec(void *object)
{
	local_cluster_rec_t *local_cluster = (local_cluster_rec_t *)object;
	if(local_cluster) {
		xfree(local_cluster->name);
		if(local_cluster->user_list)
			list_destroy(local_cluster->user_list);
		xfree(local_cluster);
	}
}

/* 
 * Comparator used for sorting users largest cpu to smallest cpu
 * 
 * returns: -1: user_a > user_b   0: user_a == user_b   1: user_a < user_b
 * 
 */
static int _sort_user_dec(local_user_rec_t *user_a, local_user_rec_t *user_b)
{
	int diff = 0;

	if (user_a->cpu_secs > user_b->cpu_secs)
		return -1;
	else if (user_a->cpu_secs < user_b->cpu_secs)
		return 1;

	if(!user_a->name || !user_b->name)
		return 0;

	diff = strcmp(user_a->name, user_b->name);

	if (diff > 0)
		return -1;
	else if (diff < 0)
		return 1;
	
	return 0;
}


static int _set_cond(int *start, int argc, char *argv[],
		     acct_user_cond_t *user_cond, List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	acct_association_cond_t *assoc_cond = NULL;
	
	if(!user_cond) {
		error("We need an acct_user_cond to call this");
		return SLURM_ERROR;
	}

	user_cond->with_deleted = 1;
	user_cond->with_assocs = 1;
	if(!user_cond->assoc_cond) {
		user_cond->assoc_cond = 
			xmalloc(sizeof(acct_association_cond_t));
		user_cond->assoc_cond->with_usage = 1;
	}
	assoc_cond = user_cond->assoc_cond;

	if(!assoc_cond->cluster_list)
		assoc_cond->cluster_list = list_create(slurm_destroy_char);

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!strncasecmp (argv[i], "Set", 3)) {
			i--;
			break;
		} else if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} else if(!end && !strncasecmp(argv[i], "all_clusters", 1)) {
			local_cluster_flag = 1;
			continue;
		} else if (!end && !strncasecmp(argv[i], "group", 1)) {
			group_accts = 1;
		} else if(!end
			  || !strncasecmp (argv[i], "Users", 1)) {
			if(!assoc_cond->user_list)
				assoc_cond->user_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->user_list,
					      argv[i]);
			set = 1;
		} else if (!strncasecmp (argv[i], "Accounts", 2)) {
			if(!assoc_cond->acct_list)
				assoc_cond->acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->acct_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Clusters", 1)) {
			slurm_addto_char_list(assoc_cond->cluster_list,
					argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "End", 1)) {
			assoc_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format", 1)) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Start", 1)) {
			assoc_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if(!local_cluster_flag && !list_count(assoc_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if(temp)
			list_append(assoc_cond->cluster_list, temp);
	}

	set_start_end_time((time_t *)&assoc_cond->usage_start,
			   (time_t *)&assoc_cond->usage_end);

	return set;
}

static int _setup_print_fields_list(List format_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;

	if(!format_list || !list_count(format_list)) {
		exit_code=1;
		fprintf(stderr, 
			" We need a format list to set up the print.\n");
		return SLURM_ERROR;
	}

	if(!print_fields_list)
		print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Accounts", object, 1)) {
			field->type = PRINT_USER_ACCT;
			field->name = xstrdup("Account(s)");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object, 1)) {
			field->type = PRINT_USER_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Login", object, 1)) {
			field->type = PRINT_USER_LOGIN;
			field->name = xstrdup("Login");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Proper", object, 1)) {
			field->type = PRINT_USER_PROPER;
			field->name = xstrdup("Proper Name");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Used", object, 1)) {
			field->type = PRINT_USER_USED;
			field->name = xstrdup("Used");
			if(time_format == SREPORT_TIME_SECS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = sreport_print_time;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

extern int user_top(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_user_cond_t *user_cond = xmalloc(sizeof(acct_user_cond_t));
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List user_list = NULL;
	List cluster_list = list_create(_destroy_local_cluster_rec);
	char *object = NULL;

	int i=0;
	uint32_t total_time = 0;
	acct_user_rec_t *user = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_accounting_rec_t *assoc_acct = NULL;
	local_user_rec_t *local_user = NULL;
	local_cluster_rec_t *local_cluster = NULL;
	print_field_t *field = NULL;
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);

	_set_cond(&i, argc, argv, user_cond, format_list);

	if(!list_count(format_list)) 
		slurm_addto_char_list(format_list, "Cl,L,P,A,U");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	user_list = acct_storage_g_get_users(db_conn, my_uid, user_cond);
	if(!user_list) {
		exit_code=1;
		fprintf(stderr, " Problem with user query.\n");
		goto end_it;
	}

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_end = user_cond->assoc_cond->usage_end-1;

		slurm_make_time_str(
			(time_t *)&user_cond->assoc_cond->usage_start, 
			start_char, sizeof(start_char));
		slurm_make_time_str(&my_end,
				    end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Top %u Users %s - %s (%d secs)\n", 
		       top_limit, start_char, end_char, 
		       (user_cond->assoc_cond->usage_end 
			- user_cond->assoc_cond->usage_start));
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}
	total_time = user_cond->assoc_cond->usage_end 
		- user_cond->assoc_cond->usage_start;

	itr = list_iterator_create(user_list);
	cluster_itr = list_iterator_create(cluster_list);
	while((user = list_next(itr))) {
		struct passwd *passwd_ptr = NULL;
		if(!user->assoc_list || !list_count(user->assoc_list))
			continue;
		
		passwd_ptr = getpwnam(user->name);
		if(passwd_ptr) 
			user->uid = passwd_ptr->pw_uid;
		else
			user->uid = (uint32_t)NO_VAL;	

		itr2 = list_iterator_create(user->assoc_list);
		while((assoc = list_next(itr2))) {

			if(!assoc->accounting_list
			   || !list_count(assoc->accounting_list))
				continue;
			
			while((local_cluster = list_next(cluster_itr))) {
				if(!strcmp(local_cluster->name, 
					   assoc->cluster)) {
					ListIterator user_itr = NULL;
					if(!group_accts) {
						local_user = NULL;
						goto new_user;
					}
					user_itr = list_iterator_create
						(local_cluster->user_list); 
					while((local_user 
					       = list_next(user_itr))) {
						if(local_user->uid 
						   == user->uid) {
							break;
						}
					}
					list_iterator_destroy(user_itr);
				new_user:
					if(!local_user) {
						local_user = xmalloc(
							sizeof
							(local_user_rec_t));
						local_user->name =
							xstrdup(assoc->user);
						local_user->uid =
							user->uid;
						local_user->acct_list =
							list_create
							(slurm_destroy_char);
						list_append(local_cluster->
							    user_list, 
							    local_user);
					}
					break;
				}
			}
			if(!local_cluster) {
				local_cluster = 
					xmalloc(sizeof(local_cluster_rec_t));
				list_append(cluster_list, local_cluster);

				local_cluster->name = xstrdup(assoc->cluster);
				local_cluster->user_list = 
					list_create(_destroy_local_user_rec);
				local_user = 
					xmalloc(sizeof(local_user_rec_t));
				local_user->name = xstrdup(assoc->user);
				local_user->uid = user->uid;
				local_user->acct_list = 
					list_create(slurm_destroy_char);
				list_append(local_cluster->user_list, 
					    local_user);
			}
			list_iterator_reset(cluster_itr);

			itr3 = list_iterator_create(local_user->acct_list);
			while((object = list_next(itr3))) {
				if(!strcmp(object, assoc->acct))
					break;
			}
			list_iterator_destroy(itr3);

			if(!object)
				list_append(local_user->acct_list, 
					    xstrdup(assoc->acct));
			itr3 = list_iterator_create(assoc->accounting_list);
			while((assoc_acct = list_next(itr3))) {
				local_user->cpu_secs += assoc_acct->alloc_secs;
				local_cluster->cpu_secs += 
					assoc_acct->alloc_secs;
			}
			list_iterator_destroy(itr3);
		}
		list_iterator_destroy(itr2);
	}	
	list_iterator_destroy(itr);

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	list_iterator_reset(cluster_itr);
	while((local_cluster = list_next(cluster_itr))) {
		list_sort(local_cluster->user_list, (ListCmpF)_sort_user_dec);
	
		itr = list_iterator_create(local_cluster->user_list);
		while((local_user = list_next(itr))) {
			int count = 0;
			int curr_inx = 1;
			while((field = list_next(itr2))) {
				char *tmp_char = NULL;
				struct passwd *pwd = NULL;
				switch(field->type) {
				case PRINT_USER_ACCT:
					itr3 = list_iterator_create(
						local_user->acct_list);
					while((object = list_next(itr3))) {
						if(tmp_char)
							xstrfmtcat(tmp_char,
								   ", %s",
								   object);
						else
							xstrcat(tmp_char,
								object);
					}
					list_iterator_destroy(itr3);
					field->print_routine(
						field,
						tmp_char,
						(curr_inx == field_count));
					xfree(tmp_char);
					break;
				case PRINT_USER_CLUSTER:
					field->print_routine(
						field,
						local_cluster->name,
						(curr_inx == field_count));
					break;
				case PRINT_USER_LOGIN:
					field->print_routine(field,
							     local_user->name,
							     (curr_inx == 
							      field_count));
					break;
				case PRINT_USER_PROPER:
					pwd = getpwnam(local_user->name);
					if(pwd) {
						tmp_char = strtok(pwd->pw_gecos,
								  ",");
						if(!tmp_char)
							tmp_char = 
								pwd->pw_gecos;
					}
					field->print_routine(field,
							     tmp_char,
							     (curr_inx == 
							      field_count));
					break;
				case PRINT_USER_USED:
					field->print_routine(
						field,
						local_user->cpu_secs,
						local_cluster->cpu_secs,
						(curr_inx == field_count));
					break;
				default:
					break;
				}
				curr_inx++;
			}
			list_iterator_reset(itr2);
			printf("\n");
			count++;
			if(count >= top_limit)
				break;
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	/* group_accts could be set in the set_cond function and needs
	 * to be cleared here, or anytime _set_cond is called.
	 */
	group_accts = 0;
	destroy_acct_user_cond(user_cond);
	
	if(user_list) {
		list_destroy(user_list);
		user_list = NULL;
	}
	
	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}
	
	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

