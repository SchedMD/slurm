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
	int command_len = 0;
	int option = 0;

	if(!qos_cond) {
		error("No qos_cond given");
		return -1;
	}

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

		if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i--;
		} else if (!strncasecmp (argv[i], "WithDeleted", 
					 MAX(command_len, 5))) {
			qos_cond->with_deleted = 1;
		} else if(!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Names",
					   MAX(command_len, 1))
			  || !strncasecmp (argv[i], "QOSLevel",
					   MAX(command_len, 1))) {
			if(!qos_cond->name_list) {
				qos_cond->name_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(qos_cond->name_list,
						 argv[i]+end))
				set = 1;
		} else if(!strncasecmp (argv[i], "Descriptions", 
					MAX(command_len, 1))) {
			if(!qos_cond->description_list) {
				qos_cond->description_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(qos_cond->description_list,
						 argv[i]+end))
				set = 1;
		} else if(!strncasecmp (argv[i], "Ids", MAX(command_len, 1))) {
			ListIterator itr = NULL;
			char *temp = NULL;
			uint32_t id = 0;

			if(!qos_cond->id_list) {
				qos_cond->id_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(qos_cond->id_list, 
						 argv[i]+end))
				set = 1;

			/* check to make sure user gave ints here */
			itr = list_iterator_create(qos_cond->id_list);
			while ((temp = list_next(itr))) {
				if (get_uint(temp, &id, "QOS ID")
				    != SLURM_SUCCESS) {
					exit_code = 1;
					list_delete_item(itr);
				}
			}
			list_iterator_destroy(itr);
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

static int _set_rec(int *start, int argc, char *argv[],
		    List qos_list,
		    acct_qos_rec_t *qos)
{
	int i, mins;
	int set = 0;
	int end = 0;
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

		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if(!end && !strncasecmp(argv[i], "set",
					       MAX(command_len, 3))) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Name",
					   MAX(command_len, 1))) {
			if(qos_list) 
				slurm_addto_char_list(qos_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Description",
					 MAX(command_len, 1))) {
			if(!qos->description)
				qos->description =
					strip_quotes(argv[i]+end, NULL);
			set = 1;
		} else if (!strncasecmp (argv[i], "JobFlags",
					 MAX(command_len, 1))) {
			if(!qos->job_flags)
				qos->job_flags =
					strip_quotes(argv[i]+end, NULL);
			set = 1;			
		} else if (!strncasecmp (argv[i], "GrpCPUMins",
					 MAX(command_len, 7))) {
			if(!qos)
				continue;
			if (get_uint64(argv[i]+end, 
				       &qos->grp_cpu_mins, 
				       "GrpCPUMins") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpCpus",
					 MAX(command_len, 7))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, &qos->grp_cpus,
			    "GrpCpus") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpJobs",
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, &qos->grp_jobs,
			    "GrpJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpNodes",
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, &qos->grp_nodes,
			    "GrpNodes") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpSubmitJobs",
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, &qos->grp_submit_jobs,
			    "GrpSubmitJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				qos->grp_wall	= (uint32_t) mins;
				set = 1;
			} else {
				exit_code=1;
				fprintf(stderr, 
					" Bad GrpWall time format: %s\n", 
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "MaxCPUMinsPerJob", 
					 MAX(command_len, 7))) {
			if(!qos)
				continue;
			if (get_uint64(argv[i]+end, 
				       &qos->max_cpu_mins_pu, 
				       "MaxCPUMins") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxCpusPerJob", 
					 MAX(command_len, 7))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, &qos->max_cpus_pu,
			    "MaxCpus") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobsPerJob",
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, &qos->max_jobs_pu,
			    "MaxJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodesPerJob",
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, 
			    &qos->max_nodes_pu,
			    "MaxNodes") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxSubmitJobs",
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			if (get_uint(argv[i]+end, &qos->max_submit_jobs_pu,
			    "MaxSubmitJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxWallDurationPerJob", 
					 MAX(command_len, 4))) {
			if(!qos)
				continue;
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				qos->max_wall_pu = (uint32_t) mins;
				set = 1;
			} else {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxWall time format: %s\n", 
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "Preemptee", 
					 MAX(command_len, 9))) {
			if(!qos)
				continue;

			if(!qos->preemptee_list) 
				qos->preemptee_list = 
					list_create(slurm_destroy_char);
						
			if(!qos_list) 
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
						
			if(addto_qos_char_list(qos->preemptee_list,
					       qos_list, argv[i]+end, option))
				set = 1;
			else
				exit_code = 1;
		} else if (!strncasecmp (argv[i], "Preemptor",
					 MAX(command_len, 9))) {
			if(!qos)
				continue;

			if(!qos->preemptor_list) 
				qos->preemptor_list = 
					list_create(slurm_destroy_char);
						
			if(!qos_list) 
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
						
			if(addto_qos_char_list(qos->preemptor_list,
					       qos_list, argv[i]+end, option))
				set = 1;
			else
				exit_code = 1;
		} else if (!strncasecmp (argv[i], "Priority", 
					 MAX(command_len, 3))) {
			if(!qos)
				continue;
			
			if (get_uint(argv[i]+end, &qos->priority,
			    "Priority") == SLURM_SUCCESS)
				set = 1;
		} else {
			printf(" Unknown option: %s\n"
			       " Use keyword 'where' to modify condition\n",
			       argv[i]);
		}
	}

	(*start) = i;

	return set;
}

extern int sacctmgr_add_qos(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0, limit_set=0;
	ListIterator itr = NULL;
	acct_qos_rec_t *qos = NULL;
	acct_qos_rec_t *start_qos = xmalloc(sizeof(acct_qos_rec_t));
	List name_list = list_create(slurm_destroy_char);
	char *description = NULL;
	char *name = NULL;
	List qos_list = NULL;
	List local_qos_list = NULL;
	char *qos_str = NULL;

	init_acct_qos_rec(start_qos);

	for (i=0; i<argc; i++) 
		limit_set = _set_rec(&i, argc, argv, name_list, start_qos);

	if(exit_code) {
		list_destroy(name_list);
		xfree(description);
		return SLURM_ERROR;
	} else if(!list_count(name_list)) {
		list_destroy(name_list);
		destroy_acct_qos_rec(start_qos);
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
			if(start_qos->description) 
				qos->description =
					xstrdup(start_qos->description);
			else
				qos->description = xstrdup(name);

			qos->grp_cpu_mins = start_qos->grp_cpu_mins;
			qos->grp_cpus = start_qos->grp_cpus;
			qos->grp_jobs = start_qos->grp_jobs;
			qos->grp_nodes = start_qos->grp_nodes;
			qos->grp_submit_jobs = start_qos->grp_submit_jobs;
			qos->grp_wall = start_qos->grp_wall;

			qos->max_cpu_mins_pu = start_qos->max_cpu_mins_pu;
			qos->max_cpus_pu = start_qos->max_cpus_pu;
			qos->max_jobs_pu = start_qos->max_jobs_pu;
			qos->max_nodes_pu = start_qos->max_nodes_pu;
			qos->max_submit_jobs_pu = start_qos->max_submit_jobs_pu;
			qos->max_wall_pu = start_qos->max_wall_pu;

			if(start_qos->job_flags)
				qos->job_flags = start_qos->job_flags;

			qos->priority = start_qos->priority;

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
			printf("  Description    = %s\n", description);
		else
			printf("  Description    = %s\n", "QOS Name");

		sacctmgr_print_qos_limits(start_qos);

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
	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_DESC,
		PRINT_ID,
		PRINT_NAME,
		PRINT_JOBF,
		PRINT_PRIO,
		PRINT_GRPCM,
		PRINT_GRPC,
		PRINT_GRPJ,
		PRINT_GRPN,
		PRINT_GRPS,
		PRINT_GRPW,
		PRINT_MAXC,
		PRINT_MAXCM,
		PRINT_MAXJ,
		PRINT_MAXN,
		PRINT_MAXS,
		PRINT_MAXW,
	};

	_set_cond(&i, argc, argv, qos_cond, format_list);

	if(exit_code) {
		destroy_acct_qos_cond(qos_cond);
		list_destroy(format_list);		
		return SLURM_ERROR;
	} else if(!list_count(format_list)) {
		slurm_addto_char_list(format_list, "N,Prio,JobF,"
				      "GrpJ,GrpN,GrpS,MaxJ,MaxN,MaxS,MaxW");
	}

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		char *tmp_char = NULL;
		int command_len = strlen(object);

		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Description", object, MAX(command_len, 1))) {
			field->type = PRINT_DESC;
			field->name = xstrdup("Descr");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("GrpCPUMins", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_GRPCM;
			field->name = xstrdup("GrpCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("GrpCPUs", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_GRPC;
			field->name = xstrdup("GrpCPUs");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpJobs", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_GRPJ;
			field->name = xstrdup("GrpJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpNodes", object, 
				       MAX(command_len, 4))) {
			field->type = PRINT_GRPN;
			field->name = xstrdup("GrpNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("GrpSubmitJobs", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_GRPS;
			field->name = xstrdup("GrpSubmit");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("ID", object, MAX(command_len, 1))) {
			field->type = PRINT_ID;
			field->name = xstrdup("ID");
			field->len = 6;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("JobFlags", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_JOBF;
			field->name = xstrdup("JobFlags");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("MaxCPUMinsPerJob", object,
				       MAX(command_len, 7))) {
			field->type = PRINT_MAXCM;
			field->name = xstrdup("MaxCPUMins");
			field->len = 11;
			field->print_routine = print_fields_uint64;
		} else if(!strncasecmp("MaxCPUsPerJob", object,
				       MAX(command_len, 7))) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUs");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxJobs", object, 
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXJ;
			field->name = xstrdup("MaxJobs");
			field->len = 7;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxNodesPerJob", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXN;
			field->name = xstrdup("MaxNodes");
			field->len = 8;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxSubmitJobs", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXS;
			field->name = xstrdup("MaxSubmit");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("MaxWallDurationPerJob", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_MAXW;
			field->name = xstrdup("MaxWall");
			field->len = 11;
			field->print_routine = print_fields_time;
		} else if(!strncasecmp("Name", object, MAX(command_len, 1))) {
			field->type = PRINT_NAME;
			field->name = xstrdup("NAME");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Priority", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_PRIO;
			field->name = xstrdup("Priority");
			field->len = 10;
			field->print_routine = print_fields_int;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
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

	field_count = list_count(print_fields_list);

	while((qos = list_next(itr))) {
		int curr_inx = 1;
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_DESC:
				field->print_routine(
					field, qos->description,
					(curr_inx == field_count));
				break;
			case PRINT_GRPCM:
				field->print_routine(
					field,
					qos->grp_cpu_mins,
					(curr_inx == field_count));
				break;
			case PRINT_GRPC:
				field->print_routine(field,
						     qos->grp_cpus,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPJ:
				field->print_routine(field, 
						     qos->grp_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPN:
				field->print_routine(field,
						     qos->grp_nodes,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPS:
				field->print_routine(field, 
						     qos->grp_submit_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPW:
				field->print_routine(
					field,
					qos->grp_wall,
					(curr_inx == field_count));
				break;
			case PRINT_ID:
				field->print_routine(
					field, qos->id,
					(curr_inx == field_count));
				break;
			case PRINT_JOBF:
				field->print_routine(
					field, qos->job_flags,
					(curr_inx == field_count));
				break;
			case PRINT_MAXCM:
				field->print_routine(
					field,
					qos->max_cpu_mins_pu,
					(curr_inx == field_count));
				break;
			case PRINT_MAXC:
				field->print_routine(field,
						     qos->max_cpus_pu,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXJ:
				field->print_routine(field, 
						     qos->max_jobs_pu,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXN:
				field->print_routine(field,
						     qos->max_nodes_pu,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXS:
				field->print_routine(field, 
						     qos->max_submit_jobs_pu,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXW:
				field->print_routine(
					field,
					qos->max_wall_pu,
					(curr_inx == field_count));
				break;
			case PRINT_NAME:
				field->print_routine(
					field, qos->name,
					(curr_inx == field_count));
				break;
			case PRINT_PRIO:
				field->print_routine(
					field, qos->priority,
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
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(qos_list);
	list_destroy(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_qos(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_qos_cond_t *qos_cond = xmalloc(sizeof(acct_qos_cond_t));
	acct_qos_rec_t *qos = xmalloc(sizeof(acct_qos_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	init_acct_qos_rec(qos);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i++;
			cond_set = _set_cond(&i, argc, argv, qos_cond, NULL);
			      
		} else if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i++;
			rec_set = _set_rec(&i, argc, argv, NULL, qos);
		} else {
			cond_set = _set_cond(&i, argc, argv, qos_cond, NULL);
		}
	}

	if(exit_code) {
		destroy_acct_qos_cond(qos_cond);
		destroy_acct_qos_rec(qos);
		return SLURM_ERROR;
	} else if(!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		destroy_acct_qos_cond(qos_cond);
		destroy_acct_qos_rec(qos);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_qos_cond(qos_cond);
			destroy_acct_qos_rec(qos);
			return SLURM_SUCCESS;
		}		
	}

	notice_thread_init();		
	
	ret_list = acct_storage_g_modify_qos(db_conn, my_uid, qos_cond, qos);
	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified qos...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if(ret_list) {
		printf(" Nothing modified\n");
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request\n");
		rc = SLURM_ERROR;
	}
	
	if(ret_list)
		list_destroy(ret_list);

	notice_thread_fini();

	if(set) {
		if(commit_check("Would you like to commit changes?")) 
			acct_storage_g_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}

	destroy_acct_qos_cond(qos_cond);
	destroy_acct_qos_rec(qos);	

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
