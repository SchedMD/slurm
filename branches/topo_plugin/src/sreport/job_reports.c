/*****************************************************************************\
 *  job_reports.c - functions for generating job reports
 *                     from accounting infrastructure.
 *****************************************************************************
 *
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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

#include "job_reports.h"

typedef struct {
	List jobs; /* This should be a NULL destroy since we are just
		    * putting a pointer to a jobacct_job_rect_t here
		    * not allocating any new memory */
	uint32_t min_size; /* smallest size of job in cpus here 0 if first */
	uint32_t max_size; /* largest size of job in cpus here INFINITE if
			    * last */
	uint32_t count; /* count of jobs */
	uint64_t cpu_secs; /* how many cpus secs taken up by this
			    * grouping */
} local_grouping_t;

typedef struct {
	char *acct; /*account name */
	uint64_t cpu_secs; /* how many cpus secs taken up by this
			    * acct */
	List groups; /* Names of what are being grouped char *'s*/
	uint32_t lft;
	uint32_t rgt;
} acct_grouping_t;

typedef struct {
	char *cluster; /*cluster name */
	uint64_t cpu_secs; /* how many cpus secs taken up by this
			    * cluster */
	List acct_list; /* containing acct_grouping_t's */
} cluster_grouping_t;

enum {
	PRINT_JOB_ACCOUNT,
	PRINT_JOB_CLUSTER,
	PRINT_JOB_COUNT,
	PRINT_JOB_CPUS,
	PRINT_JOB_DUR,
	PRINT_JOB_NODES,
	PRINT_JOB_SIZE,
	PRINT_JOB_USER,
	PRINT_JOB_WCKEY
};

static List print_fields_list = NULL; /* types are of print_field_t */
static List grouping_print_fields_list = NULL; /* types are of print_field_t */
static int print_job_count = 0;

static void _destroy_local_grouping(void *object)
{
	local_grouping_t *local_grouping = (local_grouping_t *)object;
	if(local_grouping) {
		list_destroy(local_grouping->jobs);
		xfree(local_grouping);
	}
}

static void _destroy_acct_grouping(void *object)
{
	acct_grouping_t *acct_grouping = (acct_grouping_t *)object;
	if(acct_grouping) {
		xfree(acct_grouping->acct);
		if(acct_grouping->groups)
			list_destroy(acct_grouping->groups);
		xfree(acct_grouping);
	}
}

static void _destroy_cluster_grouping(void *object)
{
	cluster_grouping_t *cluster_grouping = (cluster_grouping_t *)object;
	if(cluster_grouping) {
		xfree(cluster_grouping->cluster);
		if(cluster_grouping->acct_list)
			list_destroy(cluster_grouping->acct_list);
		xfree(cluster_grouping);
	}
}

/* returns number of objects added to list */
extern int _addto_uid_char_list(List char_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	int count = 0;

	if(!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if(names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while(names[i]) {
			//info("got %d - %d = %d", i, start, i-start);
			if(quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if(names[i] == ',') {
				if((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));
					//info("got %s %d", name, i-start);
					if (!isdigit((int) *name)) {
						struct passwd *pwd;
						if (!(pwd=getpwnam(name))) {
							fprintf(stderr, 
								"Invalid user "
								"id: %s\n",
								name);
							exit(1);
						}
						xfree(name);
						name = xstrdup_printf(
							"%d", pwd->pw_uid);
					}
					
					while((tmp_char = list_next(itr))) {
						if(!strcasecmp(tmp_char, name))
							break;
					}

					if(!tmp_char) {
						list_append(char_list, name);
						count++;
					} else 
						xfree(name);
					list_iterator_reset(itr);
				}
				i++;
				start = i;
				if(!names[i]) {
					info("There is a problem with "
					     "your request.  It appears you "
					     "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));
			
			if (!isdigit((int) *name)) {
				struct passwd *pwd;
				if (!(pwd=getpwnam(name))) {
					fprintf(stderr, 
						"Invalid user id: %s\n",
						name);
					exit(1);
				}
				xfree(name);
				name = xstrdup_printf("%d", pwd->pw_uid);
			}
			
			while((tmp_char = list_next(itr))) {
				if(!strcasecmp(tmp_char, name))
					break;
			}
			
			if(!tmp_char) {
				list_append(char_list, name);
				count++;
			} else 
				xfree(name);
		}
	}	
	list_iterator_destroy(itr);
	return count;
} 

static int _set_cond(int *start, int argc, char *argv[],
		     acct_job_cond_t *job_cond,
		     List format_list, List grouping_list)
{
	int i;
	int set = 0;
	int end = 0;
	int local_cluster_flag = all_clusters_flag;
	time_t start_time, end_time;
	int command_len = 0;

	if(!job_cond->cluster_list)
		job_cond->cluster_list = list_create(slurm_destroy_char);

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if(!end)
			command_len=strlen(argv[i]);
		else
			command_len=end-1;

		if(!end && !strncasecmp(argv[i], "all_clusters",
					       MAX(command_len, 1))) {
			local_cluster_flag = 1;
			continue;
		} else if(!end && !strncasecmp(argv[i], "PrintJobCount",
					       MAX(command_len, 2))) {
			print_job_count = 1;
			continue;
		} else if(!end 
			  || !strncasecmp (argv[i], "Clusters",
					   MAX(command_len, 1))) {
			slurm_addto_char_list(job_cond->cluster_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Accounts", 
					 MAX(command_len, 2))) {
			if(!job_cond->acct_list)
				job_cond->acct_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->acct_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Associations",
					 MAX(command_len, 2))) {
			if(!job_cond->associd_list)
				job_cond->associd_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->associd_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "End", MAX(command_len, 1))) {
			job_cond->usage_end = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Gid", MAX(command_len, 2))) {
			if(!job_cond->groupid_list)
				job_cond->groupid_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->groupid_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "grouping",
					 MAX(command_len, 2))) {
			if(grouping_list)
				slurm_addto_char_list(grouping_list, 
						      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Jobs",
					 MAX(command_len, 1))) {
			char *end_char = NULL, *start_char = argv[i]+end;
			jobacct_selected_step_t *selected_step = NULL;
			char *dot = NULL;
			if(!job_cond->step_list)
				job_cond->step_list =
					list_create(slurm_destroy_char);

			while ((end_char = strstr(start_char, ",")) 
			       && start_char) {
				*end_char = 0;
				while (isspace(*start_char))
					start_char++;  /* discard whitespace */
				if(!(int)*start_char)
					continue;
				selected_step = xmalloc(
					sizeof(jobacct_selected_step_t));
				list_append(job_cond->step_list, selected_step);
				
				dot = strstr(start_char, ".");
				if (dot == NULL) {
					debug2("No jobstep requested");
					selected_step->stepid = NO_VAL;
				} else {
					*dot++ = 0;
					selected_step->stepid = atoi(dot);
				}
				selected_step->jobid = atoi(start_char);
				start_char = end_char + 1;
			}
			
			set = 1;
		} else if (!strncasecmp (argv[i], "Partitions",
					 MAX(command_len, 2))) {
			if(!job_cond->partition_list)
				job_cond->partition_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->partition_list,
					      argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Start",
					 MAX(command_len, 1))) {
			job_cond->usage_start = parse_time(argv[i]+end, 1);
			set = 1;
		} else if (!strncasecmp (argv[i], "Users",
					 MAX(command_len, 1))) {
			if(!job_cond->userid_list)
				job_cond->userid_list =
					list_create(slurm_destroy_char);
			_addto_uid_char_list(job_cond->userid_list,
					     argv[i]+end);
			set = 1;
		} else if (!strncasecmp (argv[i], "Wckeys", 
					 MAX(command_len, 2))) {
			if(!job_cond->wckey_list)
				job_cond->wckey_list =
					list_create(slurm_destroy_char);
			slurm_addto_char_list(job_cond->wckey_list,
					      argv[i]+end);
			set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				"Use keyword set to modify value\n", argv[i]);
		}
	}
	(*start) = i;

	if(!local_cluster_flag && !list_count(job_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if(temp)
			list_append(job_cond->cluster_list, temp);
	}

	/* This needs to be done on some systems to make sure
	   cluster_cond isn't messed.  This has happened on some 64
	   bit machines and this is here to be on the safe side.
	*/
	start_time = job_cond->usage_start;
	end_time = job_cond->usage_end;
	set_start_end_time(&start_time, &end_time);
	job_cond->usage_start = start_time;
	job_cond->usage_end = end_time;

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
		char *tmp_char = NULL;
		int command_len = 0;
		int newlen = 0;
		
		if((tmp_char = strstr(object, "\%"))) {
			newlen = atoi(tmp_char+1);
			tmp_char[0] = '\0';
		} 

		command_len = strlen(object);

		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, MAX(command_len, 1))) {
			field->type = PRINT_JOB_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_JOB_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("CpuCount", object, 
				       MAX(command_len, 2))) {
			field->type = PRINT_JOB_CPUS;
			field->name = xstrdup("CPU Count");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("Duration", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_JOB_DUR;
			field->name = xstrdup("Duration");
			field->len = 12;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("JobCount", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_JOB_COUNT;
			field->name = xstrdup("Job Count");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("NodeCount", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_JOB_NODES;
			field->name = xstrdup("Node Count");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("User", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_JOB_USER;
			field->name = xstrdup("User");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Wckey", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_JOB_WCKEY;
			field->name = xstrdup("Wckey");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown field '%s'\n", object);
			xfree(field);
			continue;
		}

		if(newlen) 
			field->len = newlen;
		
		list_append(print_fields_list, field);		
	}
	list_iterator_destroy(itr);

	return SLURM_SUCCESS;
}

static int _setup_grouping_print_fields_list(List grouping_list)
{
	ListIterator itr = NULL;
	print_field_t *field = NULL;
	char *object = NULL;
	char *last_object = NULL;
	uint32_t last_size = 0;
	uint32_t size = 0;
	char *tmp_char = NULL;

	if(!grouping_list || !list_count(grouping_list)) {
		exit_code=1;
		fprintf(stderr, " We need a grouping list to "
			"set up the print.\n");
		return SLURM_ERROR;
	}

	if(!grouping_print_fields_list)
		grouping_print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(grouping_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		size = atoi(object);
		if(print_job_count)
			field->type = PRINT_JOB_COUNT;
		else
			field->type = PRINT_JOB_SIZE;
		field->name = xstrdup_printf("%u-%u cpus", last_size, size-1);
		if(time_format == SREPORT_TIME_SECS_PER
		   || time_format == SREPORT_TIME_MINS_PER
		   || time_format == SREPORT_TIME_HOURS_PER)
			field->len = 20;
		else
			field->len = 13;

		if(print_job_count)
			field->print_routine = print_fields_uint;
		else
			field->print_routine = sreport_print_time;
		last_size = size;
		last_object = object;
		if((tmp_char = strstr(object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if(newlen) 
				field->len = newlen;
		}
		list_append(grouping_print_fields_list, field);		
	}
	list_iterator_destroy(itr);

	if(last_size) {
		field = xmalloc(sizeof(print_field_t));
		if(print_job_count)
			field->type = PRINT_JOB_COUNT;
		else
			field->type = PRINT_JOB_SIZE;
		field->name = xstrdup_printf("> %u cpus", last_size);
		if(time_format == SREPORT_TIME_SECS_PER
		   || time_format == SREPORT_TIME_MINS_PER
		   || time_format == SREPORT_TIME_HOURS_PER)
			field->len = 20;
		else
			field->len = 13;
		if(print_job_count)
			field->print_routine = print_fields_uint;
		else
			field->print_routine = sreport_print_time;
		if((tmp_char = strstr(last_object, "\%"))) {
			int newlen = atoi(tmp_char+1);
			if(newlen) 
				field->len = newlen;
		}
		list_append(grouping_print_fields_list, field);		
	}

	return SLURM_SUCCESS;
}

extern int job_sizes_grouped_by_top_acct(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_job_cond_t *job_cond = xmalloc(sizeof(acct_job_cond_t));
	acct_association_cond_t assoc_cond;
	acct_association_rec_t *assoc = NULL;
	int i=0;

	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator cluster_itr = NULL;
	ListIterator local_itr = NULL;
	ListIterator acct_itr = NULL;
	ListIterator group_itr = NULL;
	

	jobacct_job_rec_t *job = NULL;
	cluster_grouping_t *cluster_group = NULL;
	acct_grouping_t *acct_group = NULL;
	local_grouping_t *local_group = NULL;

	print_field_t *field = NULL;
	print_field_t total_field;
	uint32_t total_time = 0;
	sreport_time_format_t temp_format;
	
	List job_list = NULL;
	List cluster_list = NULL;
	List assoc_list = NULL;

	List tmp_acct_list = NULL;

	List format_list = list_create(slurm_destroy_char);
	List grouping_list = list_create(slurm_destroy_char);

	List header_list = list_create(NULL);

//	sreport_time_format_t temp_time_format = time_format;

	print_fields_list = list_create(destroy_print_field);

	_set_cond(&i, argc, argv, job_cond, format_list, grouping_list);
	
	if(!list_count(format_list))
		slurm_addto_char_list(format_list, "Cl,a");

	if(!list_count(grouping_list)) 
		slurm_addto_char_list(grouping_list, "50,250,500,1000");
	
	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	_setup_grouping_print_fields_list(grouping_list);

	/* we don't want to actually query by accounts in the jobs
	   here since we may be looking for sub accounts of a specific
	   account.
	*/
	tmp_acct_list = job_cond->acct_list;
	job_cond->acct_list = NULL;
	job_list = jobacct_storage_g_get_jobs_cond(db_conn, my_uid, job_cond);
	job_cond->acct_list = tmp_acct_list;
	tmp_acct_list = NULL;

	if(!job_list) {
		exit_code=1;
		fprintf(stderr, " Problem with job query.\n");
		goto end_it;
	}

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	assoc_cond.id_list = job_cond->associd_list;
	assoc_cond.cluster_list = job_cond->cluster_list;
	/* don't limit associations to having the partition_list */
	//assoc_cond.partition_list = job_cond->partition_list;
	if(!job_cond->acct_list || !list_count(job_cond->acct_list)) {
		job_cond->acct_list = list_create(NULL);
		list_append(job_cond->acct_list, "root");
	}
	assoc_cond.parent_acct_list = job_cond->acct_list;	
	

	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     &assoc_cond);
	
	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = job_cond->usage_start;
		time_t my_end = job_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Job Sizes %s - %s (%d secs)\n", 
		       start_char, end_char, 
		       (int)(job_cond->usage_end - job_cond->usage_start));
		if(print_job_count)
			printf("Units are in number of jobs ran\n");
		else
			printf("Time reported in %s\n", time_format_string);
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}
	total_time = job_cond->usage_end - job_cond->usage_start;

	cluster_list = list_create(_destroy_cluster_grouping);

	cluster_itr = list_iterator_create(cluster_list);
	group_itr = list_iterator_create(grouping_list);

	if(!assoc_list) {
		debug2(" No assoc list given.\n");
		goto no_assocs;
	}

	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		while((cluster_group = list_next(cluster_itr))) {
			if(!strcmp(assoc->cluster, cluster_group->cluster)) 
				break;
		}
		if(!cluster_group) {
			cluster_group = 
				xmalloc(sizeof(cluster_grouping_t));
			cluster_group->cluster = xstrdup(assoc->cluster);
			cluster_group->acct_list =
				list_create(_destroy_acct_grouping);
			list_append(cluster_list, cluster_group);
		}

		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			if(!strcmp(assoc->acct, acct_group->acct))
				break;
		}
		list_iterator_destroy(acct_itr);		
			
		if(!acct_group) {
			uint32_t last_size = 0;
			char *group = NULL;
			acct_group = xmalloc(sizeof(acct_grouping_t));
			acct_group->acct = xstrdup(assoc->acct);
			acct_group->lft = assoc->lft;
			acct_group->rgt = assoc->rgt;
			acct_group->groups =
				list_create(_destroy_local_grouping);
			list_append(cluster_group->acct_list, acct_group);
			while((group = list_next(group_itr))) {
				local_group = xmalloc(sizeof(local_grouping_t));
				local_group->jobs = list_create(NULL);
				local_group->min_size = last_size;
				last_size = atoi(group);
				local_group->max_size = last_size-1;
				list_append(acct_group->groups, local_group);
			}
			if(last_size) {
				local_group = xmalloc(sizeof(local_grouping_t));
				local_group->jobs = list_create(NULL);
				local_group->min_size = last_size;
				local_group->max_size = INFINITE;
				list_append(acct_group->groups, local_group);
			}
			list_iterator_reset(group_itr);
		}
		list_iterator_reset(cluster_itr);
	}
	list_iterator_destroy(itr);
no_assocs:
	itr = list_iterator_create(job_list);

	list_append_list(header_list, print_fields_list);
	list_append_list(header_list, grouping_print_fields_list);

	memset(&total_field, 0, sizeof(print_field_t));
	total_field.type = PRINT_JOB_SIZE;
	total_field.name = xstrdup("% of cluster");
	total_field.len = 12;
	total_field.print_routine = sreport_print_time;
	list_append(header_list, &total_field);

	print_fields_header(header_list);
	list_destroy(header_list);

	while((job = list_next(itr))) {
		char *local_cluster = "UNKNOWN";
		char *local_account = "UNKNOWN";

		if(!job->elapsed) {
			/* here we don't care about jobs that didn't
			 * really run here */
			continue;
		}
		if(job->cluster) 
			local_cluster = job->cluster;
		if(job->account) 
			local_account = job->account;

		list_iterator_reset(cluster_itr);
		while((cluster_group = list_next(cluster_itr))) {
			if(!strcmp(local_cluster, cluster_group->cluster)) 
				break;
		}
		if(!cluster_group) {
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			continue;
/* 			cluster_group =  */
/* 				xmalloc(sizeof(cluster_grouping_t)); */
/* 			cluster_group->cluster = xstrdup(local_cluster); */
/* 			cluster_group->acct_list = */
/* 				list_create(_destroy_acct_grouping); */
/* 			list_append(cluster_list, cluster_group); */
		}

		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			if(acct_group->lft != (uint32_t)NO_VAL
			   && job->lft != (uint32_t)NO_VAL) {
				/* keep separate since we don't want
				 * to so a strcmp if we don't have to 
				 */
				if(job->lft > acct_group->lft 
				   && job->lft < acct_group->rgt)
					break;
			} else if(!strcmp(acct_group->acct, local_account))
				break;
		}
		list_iterator_destroy(acct_itr);		
			
		if(!acct_group) {
			//char *group = NULL;
			//uint32_t last_size = 0;
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			continue;
/* 			acct_group = xmalloc(sizeof(acct_grouping_t)); */
/* 			acct_group->acct = xstrdup(local_account); */
/* 			acct_group->groups = */
/* 				list_create(_destroy_local_grouping); */
/* 			list_append(cluster_group->acct_list, acct_group); */

/* 			while((group = list_next(group_itr))) { */
/* 				local_group = xmalloc(sizeof(local_grouping_t)); */
/* 				local_group->jobs = list_create(NULL); */
/* 				local_group->min_size = last_size; */
/* 				last_size = atoi(group); */
/* 				local_group->max_size = last_size-1; */
/* 				list_append(acct_group->groups, local_group); */
/* 			} */
/* 			if(last_size) { */
/* 				local_group = xmalloc(sizeof(local_grouping_t)); */
/* 				local_group->jobs = list_create(NULL); */
/* 				local_group->min_size = last_size; */
/* 				local_group->max_size = INFINITE; */
/* 				list_append(acct_group->groups, local_group); */
/* 			} */
/* 			list_iterator_reset(group_itr); */
		}

		local_itr = list_iterator_create(acct_group->groups);
		while((local_group = list_next(local_itr))) {
			uint64_t total_secs = 0;
			if((job->alloc_cpus < local_group->min_size)
			   || (job->alloc_cpus > local_group->max_size))
				continue;
			list_append(local_group->jobs, job);
			local_group->count++;
			total_secs = (uint64_t)job->elapsed 
				* (uint64_t)job->alloc_cpus;
			local_group->cpu_secs += total_secs;
			acct_group->cpu_secs += total_secs;
			cluster_group->cpu_secs += total_secs;
		}
		list_iterator_destroy(local_itr);		
	}
	list_iterator_destroy(group_itr);
	list_destroy(grouping_list);
	list_iterator_destroy(itr);
	
//	time_format = SREPORT_TIME_PERCENT;
	
	itr = list_iterator_create(print_fields_list);
	itr2 = list_iterator_create(grouping_print_fields_list);
	list_iterator_reset(cluster_itr);
	while((cluster_group = list_next(cluster_itr))) {
		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			
			while((field = list_next(itr))) {
				switch(field->type) {
				case PRINT_JOB_CLUSTER:
					field->print_routine(
						field,
						cluster_group->cluster, 0);
					break;
				case PRINT_JOB_ACCOUNT:
					field->print_routine(field,
							     acct_group->acct,
							     0);
					break;
				default:
					field->print_routine(field,
							     NULL,
							     0);
					break;
				}
			}
			list_iterator_reset(itr);
			local_itr = list_iterator_create(acct_group->groups);
			while((local_group = list_next(local_itr))) {
				field = list_next(itr2);
				switch(field->type) {
				case PRINT_JOB_SIZE:
					field->print_routine(
						field,
						local_group->cpu_secs,
						acct_group->cpu_secs,
						0);
					break;
				case PRINT_JOB_COUNT:
					field->print_routine(
						field,
						local_group->count,
						0);
					break;
				default:
					field->print_routine(field,
							     NULL,
							     0);
					break;
				}
			}
			list_iterator_reset(itr2);
			list_iterator_destroy(local_itr);
			
			temp_format = time_format;
			time_format = SREPORT_TIME_PERCENT;
			total_field.print_routine(&total_field,
						  acct_group->cpu_secs,
						  cluster_group->cpu_secs, 1);
			time_format = temp_format;
			printf("\n");
		}
		list_iterator_destroy(acct_itr);
	}
	list_iterator_destroy(itr);

//	time_format = temp_time_format;

end_it:
	if(print_job_count)
		print_job_count = 0;

	destroy_acct_job_cond(job_cond);
	
	if(job_list) {
		list_destroy(job_list);
		job_list = NULL;
	}
	
	if(assoc_list) {
		list_destroy(assoc_list);
		assoc_list = NULL;
	}
	
	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}
	
	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	if(grouping_print_fields_list) {
		list_destroy(grouping_print_fields_list);
		grouping_print_fields_list = NULL;
	}

	return rc;
}

extern int job_sizes_grouped_by_wckey(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_job_cond_t *job_cond = xmalloc(sizeof(acct_job_cond_t));
	acct_wckey_cond_t wckey_cond;
	acct_wckey_rec_t *wckey = NULL;
	int i=0;

	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator cluster_itr = NULL;
	ListIterator local_itr = NULL;
	ListIterator acct_itr = NULL;
	ListIterator group_itr = NULL;	

	jobacct_job_rec_t *job = NULL;
	cluster_grouping_t *cluster_group = NULL;
	acct_grouping_t *acct_group = NULL;
	local_grouping_t *local_group = NULL;

	print_field_t *field = NULL;
	print_field_t total_field;
	uint32_t total_time = 0;
	sreport_time_format_t temp_format;
	
	List job_list = NULL;
	List cluster_list = NULL;
	List wckey_list = NULL;

	List format_list = list_create(slurm_destroy_char);
	List grouping_list = list_create(slurm_destroy_char);

	List header_list = list_create(NULL);

//	sreport_time_format_t temp_time_format = time_format;

	print_fields_list = list_create(destroy_print_field);

	_set_cond(&i, argc, argv, job_cond, format_list, grouping_list);
	
	if(!list_count(format_list))
		slurm_addto_char_list(format_list, "Cl,wc");

	if(!list_count(grouping_list)) 
		slurm_addto_char_list(grouping_list, "50,250,500,1000");
	
	_setup_print_fields_list(format_list);
	list_destroy(format_list);

	_setup_grouping_print_fields_list(grouping_list);

	/* we don't want to actually query by wckeys in the jobs
	   here since we may be looking for sub accounts of a specific
	   account.
	*/
	job_list = jobacct_storage_g_get_jobs_cond(db_conn, my_uid, job_cond);

	if(!job_list) {
		exit_code=1;
		fprintf(stderr, " Problem with job query.\n");
		goto end_it;
	}

	memset(&wckey_cond, 0, sizeof(acct_wckey_cond_t));
	wckey_cond.name_list = job_cond->wckey_list;
	wckey_cond.cluster_list = job_cond->cluster_list;

	wckey_list = acct_storage_g_get_wckeys(db_conn, my_uid, &wckey_cond);
	
	if(print_fields_have_header) {
		char start_char[20];
		char end_char[20];
		time_t my_start = job_cond->usage_start;
		time_t my_end = job_cond->usage_end-1;

		slurm_make_time_str(&my_start, start_char, sizeof(start_char));
		slurm_make_time_str(&my_end, end_char, sizeof(end_char));
		printf("----------------------------------------"
		       "----------------------------------------\n");
		printf("Job Sizes by Wckey %s - %s (%d secs)\n", 
		       start_char, end_char, 
		       (int)(job_cond->usage_end - job_cond->usage_start));
		if(print_job_count)
			printf("Units are in number of jobs ran\n");
		else
			printf("Time reported in %s\n", time_format_string);
		printf("----------------------------------------"
		       "----------------------------------------\n");
	}
	total_time = job_cond->usage_end - job_cond->usage_start;

	cluster_list = list_create(_destroy_cluster_grouping);

	cluster_itr = list_iterator_create(cluster_list);
	group_itr = list_iterator_create(grouping_list);

	if(!wckey_list) {
		debug2(" No wckey list given.\n");
		goto no_assocs;
	}

	itr = list_iterator_create(wckey_list);
	while((wckey = list_next(itr))) {
		while((cluster_group = list_next(cluster_itr))) {
			if(!strcmp(wckey->cluster, cluster_group->cluster)) 
				break;
		}
		if(!cluster_group) {
			cluster_group = 
				xmalloc(sizeof(cluster_grouping_t));
			cluster_group->cluster = xstrdup(wckey->cluster);
			cluster_group->acct_list =
				list_create(_destroy_acct_grouping);
			list_append(cluster_list, cluster_group);
		}

		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			if(!strcmp(wckey->name, acct_group->acct))
				break;
		}
		list_iterator_destroy(acct_itr);		
			
		if(!acct_group) {
			uint32_t last_size = 0;
			char *group = NULL;
			acct_group = xmalloc(sizeof(acct_grouping_t));
			acct_group->acct = xstrdup(wckey->name);
			acct_group->lft = wckey->id;
			acct_group->groups =
				list_create(_destroy_local_grouping);
			list_append(cluster_group->acct_list, acct_group);
			while((group = list_next(group_itr))) {
				local_group = xmalloc(sizeof(local_grouping_t));
				local_group->jobs = list_create(NULL);
				local_group->min_size = last_size;
				last_size = atoi(group);
				local_group->max_size = last_size-1;
				list_append(acct_group->groups, local_group);
			}
			if(last_size) {
				local_group = xmalloc(sizeof(local_grouping_t));
				local_group->jobs = list_create(NULL);
				local_group->min_size = last_size;
				local_group->max_size = INFINITE;
				list_append(acct_group->groups, local_group);
			}
			list_iterator_reset(group_itr);
		}
		list_iterator_reset(cluster_itr);
	}
	list_iterator_destroy(itr);
no_assocs:
	itr = list_iterator_create(job_list);

	list_append_list(header_list, print_fields_list);
	list_append_list(header_list, grouping_print_fields_list);

	memset(&total_field, 0, sizeof(print_field_t));
	total_field.type = PRINT_JOB_SIZE;
	total_field.name = xstrdup("% of cluster");
	total_field.len = 12;
	total_field.print_routine = sreport_print_time;
	list_append(header_list, &total_field);

	print_fields_header(header_list);
	list_destroy(header_list);

	while((job = list_next(itr))) {
		char *local_cluster = "UNKNOWN";
		char *local_account = "UNKNOWN";

		if(!job->elapsed) {
			/* here we don't care about jobs that didn't
			 * really run here */
			continue;
		}
		if(job->cluster) 
			local_cluster = job->cluster;
		if(job->account) 
			local_account = job->account;

		list_iterator_reset(cluster_itr);
		while((cluster_group = list_next(cluster_itr))) {
			if(!strcmp(local_cluster, cluster_group->cluster)) 
				break;
		}
		if(!cluster_group) {
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			continue;
		}

		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			if(!strcmp(job->wckey, acct_group->acct))
				break;
		}
		list_iterator_destroy(acct_itr);		
			
		if(!acct_group) {
			/* here we are only looking for groups that
			 * were added with the associations above
			 */
			continue;
		}

		local_itr = list_iterator_create(acct_group->groups);
		while((local_group = list_next(local_itr))) {
			uint64_t total_secs = 0;
			if((job->alloc_cpus < local_group->min_size)
			   || (job->alloc_cpus > local_group->max_size))
				continue;
			list_append(local_group->jobs, job);
			local_group->count++;
			total_secs = (uint64_t)job->elapsed 
				* (uint64_t)job->alloc_cpus;
			local_group->cpu_secs += total_secs;
			acct_group->cpu_secs += total_secs;
			cluster_group->cpu_secs += total_secs;
		}
		list_iterator_destroy(local_itr);		
	}
	list_iterator_destroy(group_itr);
	list_destroy(grouping_list);
	list_iterator_destroy(itr);
	
//	time_format = SREPORT_TIME_PERCENT;
	
	itr = list_iterator_create(print_fields_list);
	itr2 = list_iterator_create(grouping_print_fields_list);
	list_iterator_reset(cluster_itr);
	while((cluster_group = list_next(cluster_itr))) {
		acct_itr = list_iterator_create(cluster_group->acct_list);
		while((acct_group = list_next(acct_itr))) {
			
			while((field = list_next(itr))) {
				switch(field->type) {
				case PRINT_JOB_CLUSTER:
					field->print_routine(
						field,
						cluster_group->cluster, 0);
					break;
				case PRINT_JOB_WCKEY:
					field->print_routine(field,
							     acct_group->acct,
							     0);
					break;
				default:
					field->print_routine(field,
							     NULL,
							     0);
					break;
				}
			}
			list_iterator_reset(itr);
			local_itr = list_iterator_create(acct_group->groups);
			while((local_group = list_next(local_itr))) {
				field = list_next(itr2);
				switch(field->type) {
				case PRINT_JOB_SIZE:
					field->print_routine(
						field,
						local_group->cpu_secs,
						acct_group->cpu_secs,
						0);
					break;
				case PRINT_JOB_COUNT:
					field->print_routine(
						field,
						local_group->count,
						0);
					break;
				default:
					field->print_routine(field,
							     NULL,
							     0);
					break;
				}
			}
			list_iterator_reset(itr2);
			list_iterator_destroy(local_itr);
			
			temp_format = time_format;
			time_format = SREPORT_TIME_PERCENT;
			total_field.print_routine(&total_field,
						  acct_group->cpu_secs,
						  cluster_group->cpu_secs, 1);
			time_format = temp_format;
			printf("\n");
		}
		list_iterator_destroy(acct_itr);
	}
	list_iterator_destroy(itr);

//	time_format = temp_time_format;

end_it:
	if(print_job_count)
		print_job_count = 0;

	destroy_acct_job_cond(job_cond);
	
	if(job_list) {
		list_destroy(job_list);
		job_list = NULL;
	}
	
	if(wckey_list) {
		list_destroy(wckey_list);
		wckey_list = NULL;
	}
	
	if(cluster_list) {
		list_destroy(cluster_list);
		cluster_list = NULL;
	}
	
	if(print_fields_list) {
		list_destroy(print_fields_list);
		print_fields_list = NULL;
	}

	if(grouping_print_fields_list) {
		list_destroy(grouping_print_fields_list);
		grouping_print_fields_list = NULL;
	}

	return rc;
}

