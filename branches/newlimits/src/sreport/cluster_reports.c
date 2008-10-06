/*****************************************************************************\
 *  cluster_reports.c - functions for generating cluster reports
 *                       from accounting infrastructure.
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

#include "cluster_reports.h"

enum {
	PRINT_CLUSTER_NAME,
	PRINT_CLUSTER_CPUS,
	PRINT_CLUSTER_ACPU,
	PRINT_CLUSTER_DCPU,
	PRINT_CLUSTER_ICPU,
	PRINT_CLUSTER_OCPU,
	PRINT_CLUSTER_RCPU,
	PRINT_CLUSTER_TOTAL,
	PRINT_CLUSTER_ACCT,
	PRINT_CLUSTER_USER_LOGIN,
	PRINT_CLUSTER_USER_PROPER,
	PRINT_CLUSTER_AMOUNT_USED,
};

typedef enum {
	GROUP_BY_ACCOUNT,
	GROUP_BY_ACCOUNT_JOB_SIZE,
	GROUP_BY_ACCOUNT_JOB_SIZE_DURATION,
	GROUP_BY_USER,
	GROUP_BY_USER_JOB_SIZE,
	GROUP_BY_USER_JOB_SIZE_DURATION,
	GROUP_BY_NONE
} report_grouping_t;

static List print_fields_list = NULL; /* types are of print_field_t */

static int _set_assoc_cond(int *start, int argc, char *argv[],
			   acct_association_cond_t *assoc_cond,
			   List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	
	if(!assoc_cond) {
		error("We need an acct_association_cond to call this");
		return SLURM_ERROR;
	}

	assoc_cond->with_usage = 1;
	assoc_cond->with_deleted = 1;

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
		} else if(!end
			  || !strncasecmp (argv[i], "Users", 1)) {
			if(!assoc_cond->user_list)
				assoc_cond->user_list = 
					list_create(slurm_destroy_char);
			slurm_addto_char_list(assoc_cond->user_list,
					      argv[i]+end);
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

static int _set_cluster_cond(int *start, int argc, char *argv[],
			     acct_cluster_cond_t *cluster_cond,
			     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;

	if(!cluster_cond->cluster_list)
		cluster_cond->cluster_list = list_create(slurm_destroy_char);
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
		} else if(!end
			  || !strncasecmp (argv[i], "Clusters", 1)
			  || !strncasecmp (argv[i], "Names", 1)) {
			slurm_addto_char_list(cluster_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "End", 1)) {
			cluster_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format", 1)) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Start", 1)) {
			cluster_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else {
			exit_code=1;
			fprintf(stderr," Unknown condition: %s\n"
			       "Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if(!local_cluster_flag && !list_count(cluster_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if(temp)
			list_append(cluster_cond->cluster_list, temp);
	}

	set_start_end_time((time_t *)&cluster_cond->usage_start,
			   (time_t *)&cluster_cond->usage_end);

	return set;
}

static int _setup_print_fields_list(List format_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;

	if(!format_list || !list_count(format_list)) {
		exit_code=1;
			fprintf(stderr, " we need a format list "
				"to set up the print.\n");
		return SLURM_ERROR;
	}

	if(!print_fields_list)
		print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Accounts", object, 2)) {
			field->type = PRINT_CLUSTER_ACCT;
			field->name = xstrdup("Account");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("allocated", object, 2)) {
			field->type = PRINT_CLUSTER_ACPU;
			field->name = xstrdup("Allocated");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Cluster", object, 2)) {
			field->type = PRINT_CLUSTER_NAME;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("cpu_count", object, 2)) {
			field->type = PRINT_CLUSTER_CPUS;
			field->name = xstrdup("CPU count");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("down", object, 1)) {
			field->type = PRINT_CLUSTER_DCPU;
			field->name = xstrdup("Down");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 10;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("idle", object, 1)) {
			field->type = PRINT_CLUSTER_ICPU;
			field->name = xstrdup("Idle");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Login", object, 1)) {
			field->type = PRINT_CLUSTER_USER_LOGIN;
			field->name = xstrdup("Login");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("overcommited", object, 1)) {
			field->type = PRINT_CLUSTER_OCPU;
			field->name = xstrdup("Over Comm");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 9;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Proper", object, 1)) {
			field->type = PRINT_CLUSTER_USER_PROPER;
			field->name = xstrdup("Proper Name");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("reported", object, 3)) {
			field->type = PRINT_CLUSTER_TOTAL;
			field->name = xstrdup("Reported");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 20;
			else
				field->len = 12;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("reserved", object, 3)) {
			field->type = PRINT_CLUSTER_RCPU;
			field->name = xstrdup("Reserved");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
				field->len = 18;
			else
				field->len = 9;
			field->print_routine = sreport_print_time;
		} else if(!strncasecmp("Used", object, 1)) {
			field->type = PRINT_CLUSTER_AMOUNT_USED;
			field->name = xstrdup("Used");
			if(time_format == SREPORT_TIME_SECS_PER
			   || time_format == SREPORT_TIME_MINS_PER
			   || time_format == SREPORT_TIME_HOURS_PER)
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
		if((tmp_char = strstr(object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if(newlen > 0) 
				field->len = newlen;
		}
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static List _get_cluster_list(int argc, char *argv[], uint32_t *total_time,
			      char *report_name, List format_list)
{
	acct_cluster_cond_t *cluster_cond = 
		xmalloc(sizeof(acct_cluster_cond_t));
	int i=0;
	List cluster_list = NULL;

	cluster_cond->with_deleted = 1;
	cluster_cond->with_usage = 1;

	_set_cluster_cond(&i, argc, argv, cluster_cond, format_list);
	
	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid,
						   cluster_cond);
	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		return NULL;
	}

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_end = cluster_cond->usage_end-1;

		slurm_make_time_str((time_t *)&cluster_cond->usage_start, 
				    start_char, sizeof(start_char));
		slurm_make_time_str(&my_end,
				    end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("%s %s - %s (%d*cpus secs)\n", 
		       report_name, start_char, end_char, 
		       (cluster_cond->usage_end - cluster_cond->usage_start));
		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break; 
		default:
			printf("Time reported in CPU %s\n", time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}
	(*total_time) = cluster_cond->usage_end - cluster_cond->usage_start;

	destroy_acct_cluster_cond(cluster_cond);
	
	return cluster_list;
}

extern int cluster_utilization(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	acct_cluster_rec_t *cluster = NULL;

	print_field_t *field = NULL;
	uint32_t total_time = 0;

	List cluster_list = NULL; 

	List format_list = list_create(slurm_destroy_char);
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);


	if(!(cluster_list = _get_cluster_list(argc, argv, &total_time,
					      "Cluster Utilization",
					      format_list))) 
		goto end_it;

	if(!list_count(format_list)) 
		slurm_addto_char_list(format_list, "Cl,al,d,i,res,rep");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	itr = list_iterator_create(cluster_list);
	itr2 = list_iterator_create(print_fields_list);

	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;
		cluster_accounting_rec_t total_acct;
		uint64_t total_reported = 0;
		uint64_t local_total_time = 0;
		int curr_inx = 1;

		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		memset(&total_acct, 0, sizeof(cluster_accounting_rec_t));
		
		itr3 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr3))) {
			total_acct.alloc_secs += accting->alloc_secs;
			total_acct.down_secs += accting->down_secs;
			total_acct.idle_secs += accting->idle_secs;
			total_acct.resv_secs += accting->resv_secs;
			total_acct.over_secs += accting->over_secs;
			total_acct.cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr3);

		total_acct.cpu_count /= list_count(cluster->accounting_list);
		local_total_time =
			(uint64_t)total_time * (uint64_t)total_acct.cpu_count;
		total_reported = total_acct.alloc_secs + total_acct.down_secs 
			+ total_acct.idle_secs + total_acct.resv_secs;

		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_CLUSTER_NAME:
				field->print_routine(field,
						     cluster->name,
						     (curr_inx == 
						      field_count));		
				break;
			case PRINT_CLUSTER_CPUS:
				field->print_routine(field,
						     total_acct.cpu_count,
						     (curr_inx == 
						      field_count));
				break;
			case PRINT_CLUSTER_ACPU:
				field->print_routine(field,
						     total_acct.alloc_secs,
						     total_reported,
						     (curr_inx == 
						      field_count));
				break;
			case PRINT_CLUSTER_DCPU:
				field->print_routine(field,
						     total_acct.down_secs,
						     total_reported,
						     (curr_inx == 
						      field_count));
				break;
			case PRINT_CLUSTER_ICPU:
				field->print_routine(field,
						     total_acct.idle_secs,
						     total_reported,
						     (curr_inx == 
						      field_count));
				break;
			case PRINT_CLUSTER_RCPU:
				field->print_routine(field,
						     total_acct.resv_secs,
						     total_reported,
						     (curr_inx == 
						      field_count));
				break;
			case PRINT_CLUSTER_OCPU:
					field->print_routine(field,
						     total_acct.over_secs,
						     total_reported,
						     (curr_inx == 
						      field_count));
				break;
			case PRINT_CLUSTER_TOTAL:
				field->print_routine(field,
						     total_reported,
						     local_total_time,
						     (curr_inx == 
						      field_count));
				break;
			default:
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);

end_it:
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

extern int cluster_user_by_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	acct_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator assoc_itr = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List assoc_list = NULL;
	List cluster_list = NULL;
	List sreport_cluster_list = list_create(destroy_sreport_cluster_rec);
	int i=0;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;
	sreport_user_rec_t *sreport_user = NULL;
	sreport_cluster_rec_t *sreport_cluster = NULL;
	print_field_t *field = NULL;
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);

	bzero(&cluster_cond, sizeof(acct_cluster_cond_t));

	_set_assoc_cond(&i, argc, argv, assoc_cond, format_list);

	if(!list_count(format_list)) 
		slurm_addto_char_list(format_list, "Cluster,L,P,Ac,Used");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	cluster_cond.with_deleted = 1;
	cluster_cond.with_usage = 1;
	cluster_cond.usage_end = assoc_cond->usage_end;
	cluster_cond.usage_start = assoc_cond->usage_start;
	cluster_cond.cluster_list = assoc_cond->cluster_list;
	cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		goto end_it;
	}
	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     assoc_cond);
	if(!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem with assoc query.\n");
		goto end_it;
	}

	/* set up the structures for easy reteval later */
	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;

		/* check to see if this cluster is around during the
		   time we are looking at */
		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		sreport_cluster = xmalloc(sizeof(sreport_cluster_rec_t));

		list_append(sreport_cluster_list, sreport_cluster);

		sreport_cluster->name = xstrdup(cluster->name);
		sreport_cluster->user_list = 
			list_create(destroy_sreport_user_rec);

		/* get the amount of time and the average cpu count
		   during the time we are looking at */
		itr2 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr2))) {
			sreport_cluster->cpu_secs += accting->alloc_secs 
				+ accting->down_secs + accting->idle_secs 
				+ accting->resv_secs;
			sreport_cluster->cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr2);

		sreport_cluster->cpu_count /= 
			list_count(cluster->accounting_list);
		
		/* now add the associations of interest here by user */
		while((assoc = list_next(assoc_itr))) {
			struct passwd *passwd_ptr = NULL;
			uid_t uid = NO_VAL;
			ListIterator user_itr = NULL;
			acct_accounting_rec_t *accting2 = NULL;

			if(!assoc->accounting_list
			   || !list_count(assoc->accounting_list)
			   || !assoc->user) {
				list_delete_item(assoc_itr);
				continue;
			}

			if(strcmp(cluster->name, assoc->cluster)) 
				continue;

			/* make sure we add all associations to this
			   user rec because we could have some in
			   partitions which would create another
			   record otherwise
			*/
			user_itr = list_iterator_create(
				sreport_cluster->user_list); 
			while((sreport_user = list_next(user_itr))) {
				if(!strcmp(sreport_user->acct, assoc->acct)) 
					break;				
			}
			list_iterator_destroy(user_itr);

			if(!sreport_user) {
				passwd_ptr = getpwnam(assoc->user);
				if(passwd_ptr) 
					uid = passwd_ptr->pw_uid;
				/* In this report we are using the sreport user
				   structure to store the information we want
				   since it is already avaliable and will do
				   pretty much what we want.
				*/
				sreport_user =
					xmalloc(sizeof(sreport_user_rec_t));
				sreport_user->name = xstrdup(assoc->user);
				sreport_user->uid = uid;
				sreport_user->acct = xstrdup(assoc->acct);
	
				list_append(sreport_cluster->user_list,
					    sreport_user);
			}
			/* get the amount of time this assoc used
			   during the time we are looking at */
			itr2 = list_iterator_create(assoc->accounting_list);
			while((accting2 = list_next(itr2))) {
				sreport_user->cpu_secs += 
					(uint64_t)accting2->alloc_secs;
			}
			list_iterator_destroy(itr2);
			list_delete_item(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(assoc_itr);
	list_iterator_destroy(itr);

	list_destroy(cluster_list);
	cluster_list = NULL;
	list_destroy(assoc_list);
	assoc_list = NULL;

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_end = assoc_cond->usage_end-1;
		
		slurm_make_time_str(
			(time_t *)&assoc_cond->usage_start, 
			start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("User Acct Utilization by Cluster %s - %s (%d secs)\n", 
		       start_char, end_char, 
		       (assoc_cond->usage_end - assoc_cond->usage_start));
		
		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break; 
		default:
			printf("Time reported in CPU %s\n", 
			       time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	cluster_itr = list_iterator_create(sreport_cluster_list);
	while((sreport_cluster = list_next(cluster_itr))) {
		list_sort(sreport_cluster->user_list, (ListCmpF)sort_user_dec);
	
		itr = list_iterator_create(sreport_cluster->user_list);
		while((sreport_user = list_next(itr))) {
			int curr_inx = 1;

			/* we don't care if they didn't use any time */
			if(!sreport_user->cpu_secs)
				continue;

			while((field = list_next(itr2))) {
				char *tmp_char = NULL;
				struct passwd *pwd = NULL;
				switch(field->type) {
				case PRINT_CLUSTER_ACCT:
					field->print_routine(
						field,
						sreport_user->acct,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_NAME:
					field->print_routine(
						field,
						sreport_cluster->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_LOGIN:
					field->print_routine(
						field,
						sreport_user->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_PROPER:
					pwd = getpwnam(sreport_user->name);
					if(pwd) {
						tmp_char = 
							strtok(pwd->pw_gecos,
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
				case PRINT_CLUSTER_AMOUNT_USED:
					field->print_routine(
						field,
						sreport_user->cpu_secs,
						sreport_cluster->cpu_secs,
						(curr_inx == field_count));
					break;
				default:
					break;
				}
				curr_inx++;
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	destroy_acct_association_cond(assoc_cond);
	
	if(assoc_list) {
		list_destroy(assoc_list);
		assoc_list = NULL;
	}
	
	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}
	
	if(sreport_cluster_list) {
		list_destroy(sreport_cluster_list);
		sreport_cluster_list = NULL;
	}
	
	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

extern int cluster_account_by_user(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_association_cond_t *assoc_cond = 
		xmalloc(sizeof(acct_association_cond_t));
	acct_cluster_cond_t cluster_cond;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator assoc_itr = NULL;
	ListIterator cluster_itr = NULL;
	List format_list = list_create(slurm_destroy_char);
	List assoc_list = NULL;
	List cluster_list = NULL;
	List sreport_cluster_list = list_create(destroy_sreport_cluster_rec);
	int i=0;
	acct_cluster_rec_t *cluster = NULL;
	acct_association_rec_t *assoc = NULL;
	sreport_user_rec_t *sreport_user = NULL;
	sreport_cluster_rec_t *sreport_cluster = NULL;
	print_field_t *field = NULL;
	int field_count = 0;

	print_fields_list = list_create(destroy_print_field);

	bzero(&cluster_cond, sizeof(acct_cluster_cond_t));

	_set_assoc_cond(&i, argc, argv, assoc_cond, format_list);

	if(!list_count(format_list)) 
		slurm_addto_char_list(format_list, "Cluster,Ac,L,P,Used");

	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	cluster_cond.with_deleted = 1;
	cluster_cond.with_usage = 1;
	cluster_cond.usage_end = assoc_cond->usage_end;
	cluster_cond.usage_start = assoc_cond->usage_start;
	cluster_cond.cluster_list = assoc_cond->cluster_list;
	cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with cluster query.\n");
		goto end_it;
	}
	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     assoc_cond);
	if(!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem with assoc query.\n");
		goto end_it;
	}

	/* set up the structures for easy retrieval later */
	itr = list_iterator_create(cluster_list);
	assoc_itr = list_iterator_create(assoc_list);
	while((cluster = list_next(itr))) {
		cluster_accounting_rec_t *accting = NULL;

		/* check to see if this cluster is around during the
		   time we are looking at */
		if(!cluster->accounting_list
		   || !list_count(cluster->accounting_list))
			continue;

		sreport_cluster = xmalloc(sizeof(sreport_cluster_rec_t));

		list_append(sreport_cluster_list, sreport_cluster);

		sreport_cluster->name = xstrdup(cluster->name);
		sreport_cluster->user_list = 
			list_create(destroy_sreport_user_rec);

		/* get the amount of time and the average cpu count
		   during the time we are looking at */
		itr2 = list_iterator_create(cluster->accounting_list);
		while((accting = list_next(itr2))) {
			sreport_cluster->cpu_secs += accting->alloc_secs 
				+ accting->down_secs + accting->idle_secs 
				+ accting->resv_secs;
			sreport_cluster->cpu_count += accting->cpu_count;
		}
		list_iterator_destroy(itr2);

		sreport_cluster->cpu_count /= 
			list_count(cluster->accounting_list);
		
		/* now add the associations of interest here by user */
		while((assoc = list_next(assoc_itr))) {
			struct passwd *passwd_ptr = NULL;
			uid_t uid = NO_VAL;
			ListIterator user_itr = NULL;
			acct_accounting_rec_t *accting2 = NULL;

			if(!assoc->accounting_list
			   || !list_count(assoc->accounting_list)
			   || !assoc->user) {
				list_delete_item(assoc_itr);
				continue;
			}

			if(strcmp(cluster->name, assoc->cluster)) 
				continue;

			/* make sure we add all associations to this
			   user rec because we could have some in
			   partitions which would create another
			   record otherwise
			*/
			user_itr = list_iterator_create(
				sreport_cluster->user_list); 
			while((sreport_user = list_next(user_itr))) {
				if(!strcmp(sreport_user->acct, assoc->acct)) 
					break;				
			}
			list_iterator_destroy(user_itr);

			if(!sreport_user) {
				passwd_ptr = getpwnam(assoc->user);
				if(passwd_ptr) 
					uid = passwd_ptr->pw_uid;
				/* In this report we are using the sreport user
				   structure to store the information we want
				   since it is already avaliable and will do
				   pretty much what we want.
				*/
				sreport_user = 
					xmalloc(sizeof(sreport_user_rec_t));
				sreport_user->name = xstrdup(assoc->user);
				sreport_user->uid = uid;
				sreport_user->acct = xstrdup(assoc->acct);
	
				list_append(sreport_cluster->user_list,
					    sreport_user);
			}
			/* get the amount of time this assoc used
			   during the time we are looking at */
			itr2 = list_iterator_create(assoc->accounting_list);
			while((accting2 = list_next(itr2))) {
				sreport_user->cpu_secs += 
					(uint64_t)accting2->alloc_secs;
			}
			list_iterator_destroy(itr2);
			list_delete_item(assoc_itr);
		}
		list_iterator_reset(assoc_itr);
	}
	list_iterator_destroy(assoc_itr);
	list_iterator_destroy(itr);

	list_destroy(cluster_list);
	cluster_list = NULL;
	list_destroy(assoc_list);
	assoc_list = NULL;

	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_end = assoc_cond->usage_end-1;
		
		slurm_make_time_str(
			(time_t *)&assoc_cond->usage_start, 
			start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Acct User Utilization by Cluster %s - %s (%d secs)\n", 
		       start_char, end_char, 
		       (assoc_cond->usage_end - assoc_cond->usage_start));
		
		switch(time_format) {
		case SREPORT_TIME_PERCENT:
			printf("Time reported in %s\n", time_format_string);
			break; 
		default:
			printf("Time reported in CPU %s\n", time_format_string);
			break;
		}
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}

	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);
	cluster_itr = list_iterator_create(sreport_cluster_list);
	while((sreport_cluster = list_next(cluster_itr))) {
		list_sort(sreport_cluster->user_list, (ListCmpF)sort_user_dec);
	
		itr = list_iterator_create(sreport_cluster->user_list);
		while((sreport_user = list_next(itr))) {
			int curr_inx = 1;
			if(!sreport_user->cpu_secs)
				continue;
			while((field = list_next(itr2))) {
				char *tmp_char = NULL;
				struct passwd *pwd = NULL;
				switch(field->type) {
				case PRINT_CLUSTER_ACCT:
					field->print_routine(
						field,
						sreport_user->acct,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_NAME:
					field->print_routine(
						field,
						sreport_cluster->name,
						(curr_inx == field_count));
					break;
				case PRINT_CLUSTER_USER_LOGIN:
					field->print_routine(field,
							     sreport_user->name,
							     (curr_inx == 
							      field_count));
					break;
				case PRINT_CLUSTER_USER_PROPER:
					pwd = getpwnam(sreport_user->name);
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
				case PRINT_CLUSTER_AMOUNT_USED:
					field->print_routine(
						field,
						sreport_user->cpu_secs,
						sreport_cluster->cpu_secs,
						(curr_inx == field_count));
					break;
				default:
					break;
				}
				curr_inx++;
			}
			list_iterator_reset(itr2);
			printf("\n");
		}
		list_iterator_destroy(itr);
	}
	list_iterator_destroy(cluster_itr);
end_it:
	destroy_acct_association_cond(assoc_cond);
	
	if(assoc_list) {
		list_destroy(assoc_list);
		assoc_list = NULL;
	}
	
	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}
	
	if(sreport_cluster_list) {
		list_destroy(sreport_cluster_list);
		sreport_cluster_list = NULL;
	}
	
	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	return rc;
}

