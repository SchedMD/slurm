/*****************************************************************************\
 *  cluster_functions.c - functions dealing with clusters in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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
#include "src/common/uid.h"
static bool without_limits = 0;

static int _set_cond(int *start, int argc, char *argv[],
		     List cluster_list,
		     List format_list,
		     uint16_t *classification)
{
	int i;
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

		if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if(!end && !strncasecmp(argv[i], "where",
					       MAX(command_len, 5))) {
			continue;
		} else if (!end && !strncasecmp (argv[i], "WOLimits",
						 MAX(command_len, 3))) {
			without_limits = 1;
		} else if(!end || !strncasecmp (argv[i], "Names",
						MAX(command_len, 1))
			  || !strncasecmp (argv[i], "Clusters",
					   MAX(command_len, 3))) {
			if(cluster_list) {
				if(slurm_addto_char_list(cluster_list,
							 argv[i]+end))
					set = 1;
			}
		} else if (!strncasecmp (argv[i], "Classification",
					 MAX(command_len, 3))) {
			if(classification) {
				*classification =
					str_2_classification(argv[i]+end);
				if(*classification)
					set = 1;
			}
		} else if (!strncasecmp (argv[i], "Format",
					 MAX(command_len, 1))) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
				"Use keyword set to modify value\n", argv[i]);
			break;
		}
	}
	(*start) = i;

	return set;
}

static int _set_rec(int *start, int argc, char *argv[],
		    List name_list,
		    acct_association_rec_t *assoc,
		    uint16_t *classification)
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
			  || !strncasecmp (argv[i], "Names",
					   MAX(command_len, 1))
			  || !strncasecmp (argv[i], "Clusters",
					   MAX(command_len, 3))) {
			if(name_list)
				slurm_addto_char_list(name_list,
						      argv[i]+end);
		} else if (!strncasecmp (argv[i], "Classification",
					 MAX(command_len, 3))) {
			if(classification) {
				*classification =
					str_2_classification(argv[i]+end);
				if(*classification)
					set = 1;
			}
		} else if (!strncasecmp (argv[i], "FairShare",
					 MAX(command_len, 1))
			   || !strncasecmp (argv[i], "Shares",
					 MAX(command_len, 1))) {
			if (get_uint(argv[i]+end, &assoc->shares_raw,
			    "FairShare") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpCPUMins",
					 MAX(command_len, 7))) {
			exit_code=1;
			fprintf(stderr, "GrpCPUMins is not a valid option "
				"for the root association of a cluster.\n");
			break;
		} else if (!strncasecmp (argv[i], "GrpCpus",
					 MAX(command_len, 7))) {
			if (get_uint(argv[i]+end, &assoc->grp_cpus,
			    "GrpCpus") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &assoc->grp_jobs,
			    "GrpJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpNodes",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &assoc->grp_nodes,
			    "GrpNodes") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpSubmitJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &assoc->grp_submit_jobs,
			    "GrpSubmitJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			exit_code=1;
			fprintf(stderr, "GrpWall is not a valid option "
				"for the root association of a cluster.\n");
		} else if (!strncasecmp (argv[i], "MaxCPUMinsPerJob",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end,
				       &assoc->max_cpu_mins_pj,
				       "MaxCPUMins") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxCpusPerJob",
					 MAX(command_len, 7))) {
			if (get_uint(argv[i]+end, &assoc->max_cpus_pj,
			    "MaxCpus") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &assoc->max_jobs,
			    "MaxJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodesPerJob",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end,
			    &assoc->max_nodes_pj,
			    "MaxNodes") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxSubmitJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &assoc->max_submit_jobs,
			    "MaxSubmitJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!strncasecmp (argv[i], "MaxWallDurationPerJob",
					 MAX(command_len, 4))) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				assoc->max_wall_pj = (uint32_t) mins;
				set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad MaxWall time format: %s\n",
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "QOSLevel",
					 MAX(command_len, 1))) {
			if(!assoc->qos_list)
				assoc->qos_list =
					list_create(slurm_destroy_char);

			if(!g_qos_list)
				g_qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);

			if(addto_qos_char_list(assoc->qos_list,
					       g_qos_list, argv[i]+end, option))
				set = 1;
			else
				exit_code = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}
	(*start) = i;

	return set;

}


extern int sacctmgr_add_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i = 0;
	acct_cluster_rec_t *cluster = NULL;
	List name_list = list_create(slurm_destroy_char);
	List cluster_list = NULL;
	acct_association_rec_t start_assoc;

	int limit_set = 0;
	ListIterator itr = NULL, itr_c = NULL;
	char *name = NULL;
	uint16_t class = 0;

	init_acct_association_rec(&start_assoc);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		limit_set += _set_rec(&i, argc, argv,
				     name_list, &start_assoc, &class);
	}
	if(exit_code) {
		list_destroy(name_list);
		return SLURM_ERROR;
	} else if(!list_count(name_list)) {
		list_destroy(name_list);
		exit_code=1;
		fprintf(stderr, " Need name of cluster to add.\n");
		return SLURM_ERROR;
	} else {
		List temp_list = NULL;
		acct_cluster_cond_t cluster_cond;
		char *name = NULL;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		cluster_cond.cluster_list = name_list;
		cluster_cond.classification = class;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		if(!temp_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			return SLURM_ERROR;
		}

		itr_c = list_iterator_create(name_list);
		itr = list_iterator_create(temp_list);
		while((name = list_next(itr_c))) {
			acct_cluster_rec_t *cluster_rec = NULL;

			list_iterator_reset(itr);
			while((cluster_rec = list_next(itr))) {
				if(!strcasecmp(cluster_rec->name, name))
					break;
			}
			if(cluster_rec) {
				printf(" This cluster %s already exists.  "
				       "Not adding.\n", name);
				list_delete_item(itr_c);
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr_c);
		list_destroy(temp_list);
		if(!list_count(name_list)) {
			list_destroy(name_list);
			return SLURM_ERROR;
		}
	}

	printf(" Adding Cluster(s)\n");
	cluster_list = list_create(destroy_acct_cluster_rec);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		cluster = xmalloc(sizeof(acct_cluster_rec_t));

		list_append(cluster_list, cluster);
		cluster->name = xstrdup(name);
		cluster->classification = class;
		cluster->root_assoc = xmalloc(sizeof(acct_association_rec_t));
		init_acct_association_rec(cluster->root_assoc);
		printf("  Name          = %s\n", cluster->name);
		if(cluster->classification)
			printf("  Classification= %s\n",
			       get_classification_str(cluster->classification));

		cluster->root_assoc->shares_raw = start_assoc.shares_raw;

		cluster->root_assoc->grp_cpus = start_assoc.grp_cpus;
		cluster->root_assoc->grp_jobs = start_assoc.grp_jobs;
		cluster->root_assoc->grp_nodes = start_assoc.grp_nodes;
		cluster->root_assoc->grp_submit_jobs =
			start_assoc.grp_submit_jobs;

		cluster->root_assoc->max_cpu_mins_pj =
			start_assoc.max_cpu_mins_pj;
		cluster->root_assoc->max_cpus_pj = start_assoc.max_cpus_pj;
		cluster->root_assoc->max_jobs = start_assoc.max_jobs;
		cluster->root_assoc->max_nodes_pj = start_assoc.max_nodes_pj;
		cluster->root_assoc->max_submit_jobs =
			start_assoc.max_submit_jobs;
		cluster->root_assoc->max_wall_pj = start_assoc.max_wall_pj;

		cluster->root_assoc->qos_list =
			copy_char_list(start_assoc.qos_list);
	}
	list_iterator_destroy(itr);
	list_destroy(name_list);

	if(limit_set) {
		printf(" Default Limits\n");
		sacctmgr_print_assoc_limits(&start_assoc);
		if(start_assoc.qos_list)
			list_destroy(start_assoc.qos_list);
	}

	if(!list_count(cluster_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	}

	notice_thread_init();
	rc = acct_storage_g_add_clusters(db_conn, my_uid, cluster_list);
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
		fprintf(stderr, " Problem adding clusters\n");
	}
end_it:
	list_destroy(cluster_list);

	return rc;
}

extern int sacctmgr_list_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	List cluster_list;
	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	acct_cluster_rec_t *cluster = NULL;
	char *object;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_CLUSTER,
		PRINT_CHOST,
		PRINT_CPORT,
		PRINT_CLASS,
		PRINT_CPUS,
		PRINT_FAIRSHARE,
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
		PRINT_NODECNT,
		PRINT_NODES,
		PRINT_QOS,
		PRINT_QOS_RAW,
		PRINT_RPC_VERSION
	};


	cluster_cond->cluster_list = list_create(slurm_destroy_char);
	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, cluster_cond->cluster_list,
			  format_list,
			  &cluster_cond->classification);
	}

	if(exit_code) {
		destroy_acct_cluster_cond(cluster_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	}

	print_fields_list = list_create(destroy_print_field);

	if(!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				      "Cl,Controlh,Controlp,RPC");
		if(!without_limits)
			slurm_addto_char_list(format_list,
					      "F,GrpJ,GrpN,GrpS,MaxJ,MaxN,"
					      "MaxS,MaxW,QOS");
	}

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
		if(!strncasecmp("Cluster", object, MAX(command_len, 2))
		   || !strncasecmp("Name", object, MAX(command_len, 2))) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("ControlHost", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_CHOST;
			field->name = xstrdup("ControlHost");
			field->len = 15;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("ControlPort", object,
				       MAX(command_len, 8))) {
			field->type = PRINT_CPORT;
			field->name = xstrdup("ControlPort");
			field->len = 12;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("Classification", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_CPUS;
			field->name = xstrdup("Class");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("CPUCount", object,
				       MAX(command_len, 2))) {
			field->type = PRINT_CPUS;
			field->name = xstrdup("CPUCount");
			field->len = 9;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("FairShare", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("FairShare");
			field->len = 9;
			field->print_routine = print_fields_uint;
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
		} else if(!strncasecmp("NodeCount", object,
				       MAX(command_len, 5))) {
			field->type = PRINT_NODECNT;
			field->name = xstrdup("NodeCount");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("NodeNames", object,
				       MAX(command_len, 5))) {
			field->type = PRINT_NODES;
			field->name = xstrdup("NodeNames");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("QOSRAWLevel", object,
				       MAX(command_len, 4))) {
			field->type = PRINT_QOS_RAW;
			field->name = xstrdup("QOS_RAW");
			field->len = 10;
			field->print_routine = print_fields_char_list;
		} else if(!strncasecmp("QOSLevel", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_QOS;
			field->name = xstrdup("QOS");
			field->len = 20;
			field->print_routine = sacctmgr_print_qos_list;
		} else if(!strncasecmp("RPC", object, MAX(command_len, 1))) {
			field->type = PRINT_RPC_VERSION;
			field->name = xstrdup("RPC");
			field->len = 3;
			field->print_routine = print_fields_uint;
		} else if(!strncasecmp("Shares", object,
				       MAX(command_len, 1))) {
			field->type = PRINT_FAIRSHARE;
			field->name = xstrdup("Shares");
			field->len = 9;
			field->print_routine = print_fields_uint;
		} else {
			exit_code=1;
			fprintf(stderr, "Unknown field '%s'\n", object);
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
		destroy_acct_cluster_cond(cluster_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid,
						   cluster_cond);
	destroy_acct_cluster_cond(cluster_cond);

	if(!cluster_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(cluster_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((cluster = list_next(itr))) {
		int curr_inx = 1;
		acct_association_rec_t *assoc = cluster->root_assoc;
		while((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_CLUSTER:
				field->print_routine(field,
						     cluster->name,
						     (curr_inx == field_count));
				break;
			case PRINT_CHOST:
				field->print_routine(field,
						     cluster->control_host,
						     (curr_inx == field_count));
				break;
			case PRINT_CPORT:
				field->print_routine(field,
						     cluster->control_port,
						     (curr_inx == field_count));
				break;
			case PRINT_CLASS:
				field->print_routine(field,
						     get_classification_str(
							     cluster->
							     classification),
						     (curr_inx == field_count));
				break;
			case PRINT_CPUS:
			{
				char tmp_char[9];
				convert_num_unit((float)cluster->cpu_count,
						 tmp_char, sizeof(tmp_char),
						 UNIT_NONE);
				field->print_routine(field,
						     tmp_char,
						     (curr_inx == field_count));
				break;
			}
			case PRINT_FAIRSHARE:
				field->print_routine(
					field,
					assoc->shares_raw,
					(curr_inx == field_count));
				break;
			case PRINT_GRPC:
				field->print_routine(field,
						     assoc->grp_cpus,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPJ:
				field->print_routine(field,
						     assoc->grp_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPN:
				field->print_routine(field,
						     assoc->grp_nodes,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPS:
				field->print_routine(field,
						     assoc->grp_submit_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXCM:
				field->print_routine(
					field,
					assoc->max_cpu_mins_pj,
					(curr_inx == field_count));
				break;
			case PRINT_MAXC:
				field->print_routine(field,
						     assoc->max_cpus_pj,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXJ:
				field->print_routine(field,
						     assoc->max_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXN:
				field->print_routine(field,
						     assoc->max_nodes_pj,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXS:
				field->print_routine(field,
						     assoc->max_submit_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXW:
				field->print_routine(
					field,
					assoc->max_wall_pj,
					(curr_inx == field_count));
				break;

			case PRINT_NODECNT:
			{
				hostlist_t hl = hostlist_create(cluster->nodes);
				int cnt = 0;
				if(hl) {
					cnt = hostlist_count(hl);
					hostlist_destroy(hl);
				}
				field->print_routine(
					field,
					cnt,
					(curr_inx == field_count));
				break;
			}
			case PRINT_NODES:
				field->print_routine(
					field,
					cluster->nodes,
					(curr_inx == field_count));
				break;

			case PRINT_QOS:
				if(!g_qos_list)
					g_qos_list = acct_storage_g_get_qos(
						db_conn, my_uid, NULL);

				field->print_routine(field,
						     g_qos_list,
						     assoc->qos_list,
						     (curr_inx == field_count));
				break;
			case PRINT_QOS_RAW:
				field->print_routine(field,
						     assoc->qos_list,
						     (curr_inx == field_count));
				break;
			case PRINT_RPC_VERSION:
				field->print_routine(
					field,
					cluster->rpc_version,
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
	list_destroy(cluster_list);
	list_destroy(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0;
	acct_association_rec_t *assoc = xmalloc(sizeof(acct_association_rec_t));
	acct_association_cond_t *assoc_cond =
		xmalloc(sizeof(acct_association_cond_t));
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;
	uint16_t class_cond = 0, class_rec = 0;

	init_acct_association_rec(assoc);

	assoc_cond->cluster_list = list_create(slurm_destroy_char);
	assoc_cond->acct_list = list_create(NULL);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))) {
			i++;
			if(_set_cond(&i, argc, argv,
				     assoc_cond->cluster_list,
				     NULL, &class_cond))
				cond_set = 1;
		} else if (!strncasecmp (argv[i], "Set", MAX(command_len, 3))) {
			i++;
			if(_set_rec(&i, argc, argv, NULL, assoc, &class_rec))
				rec_set = 1;
		} else {
			if(_set_cond(&i, argc, argv,
				     assoc_cond->cluster_list,
				     NULL, &class_cond))
				cond_set = 1;
		}
	}

	if(!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		destroy_acct_association_rec(assoc);
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_association_rec(assoc);
			destroy_acct_association_cond(assoc_cond);
			return SLURM_SUCCESS;
		}
	} else if(exit_code) {
		destroy_acct_association_rec(assoc);
		destroy_acct_association_cond(assoc_cond);
		return SLURM_ERROR;
	}

	if(class_cond) {
		List temp_list = NULL;
		acct_cluster_cond_t cluster_cond;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		cluster_cond.cluster_list = assoc_cond->cluster_list;
		cluster_cond.classification = class_cond;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		if(!temp_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			destroy_acct_association_rec(assoc);
			destroy_acct_association_cond(assoc_cond);
			return SLURM_ERROR;
		} else if(!list_count(temp_list)) {
			fprintf(stderr,
				" The class you gave %s didn't "
				"return any clusters.\n",
				get_classification_str(class_cond));
			destroy_acct_association_rec(assoc);
			destroy_acct_association_cond(assoc_cond);
			list_destroy(temp_list);
			return SLURM_ERROR;
		}
		/* we are only looking for the clusters returned from
		   this query, so we free the cluster_list and replace
		   it */
		if(assoc_cond->cluster_list)
			list_destroy(assoc_cond->cluster_list);
		assoc_cond->cluster_list = temp_list;
	}

	printf(" Setting\n");
	if(rec_set) {
		printf(" Default Limits =\n");
		sacctmgr_print_assoc_limits(assoc);
		if(class_rec)
			printf(" Cluster Classification = %s\n",
			       get_classification_str(class_rec));
	}

	list_append(assoc_cond->acct_list, "root");
	notice_thread_init();
	ret_list = acct_storage_g_modify_associations(
		db_conn, my_uid, assoc_cond, assoc);

	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified cluster defaults for associations...\n");
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

	if(class_rec) {
		acct_cluster_cond_t cluster_cond;
		acct_cluster_rec_t cluster_rec;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		memset(&cluster_rec, 0, sizeof(acct_cluster_rec_t));
		/* the class has already returned these clusters so
		   just go with it */
		cluster_cond.cluster_list = assoc_cond->cluster_list;

		cluster_rec.classification = class_rec;

		ret_list = acct_storage_g_modify_clusters(
			db_conn, my_uid, &cluster_cond, &cluster_rec);

		if(ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified cluster classifications...\n");
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
	}

	notice_thread_fini();

	if(set) {
		if(commit_check("Would you like to commit changes?"))
			acct_storage_g_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}
	destroy_acct_association_cond(assoc_cond);
	destroy_acct_association_rec(assoc);

	return rc;
}

extern int sacctmgr_delete_cluster(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_cluster_cond_t *cluster_cond =
		xmalloc(sizeof(acct_cluster_cond_t));
	int i=0;
	List ret_list = NULL;
	int cond_set = 0;

	cluster_cond->cluster_list = list_create(slurm_destroy_char);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!strncasecmp (argv[i], "Where", MAX(command_len, 5))
		    || !strncasecmp (argv[i], "Set", MAX(command_len, 3)))
			i++;
		cond_set += _set_cond(&i, argc, argv,
				      cluster_cond->cluster_list,
				      NULL,
				      &cluster_cond->classification);
	}

	if(exit_code) {
		destroy_acct_cluster_cond(cluster_cond);
		return SLURM_ERROR;
	} else if(!cond_set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		destroy_acct_cluster_cond(cluster_cond);
		return SLURM_ERROR;
	}

	if(!list_count(cluster_cond->cluster_list)
	   && !cluster_cond->classification) {
		exit_code=1;
		fprintf(stderr,
			"problem with delete request.  "
			"Nothing given to delete.\n");
		destroy_acct_cluster_cond(cluster_cond);
		return SLURM_SUCCESS;
	}
	notice_thread_init();
	ret_list = acct_storage_g_remove_clusters(
		db_conn, my_uid, cluster_cond);
	notice_thread_fini();

	destroy_acct_cluster_cond(cluster_cond);

	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Deleting clusters...\n");
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

extern int sacctmgr_dump_cluster (int argc, char *argv[])
{
	acct_user_cond_t user_cond;
	acct_user_rec_t *user = NULL;
	acct_hierarchical_rec_t *acct_hierarchical_rec = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_association_cond_t assoc_cond;
	List assoc_list = NULL;
	List acct_list = NULL;
	List user_list = NULL;
	List acct_hierarchical_rec_list = NULL;
	char *cluster_name = NULL;
	char *file_name = NULL;
	char *user_name = NULL;
	char *line = NULL;
	int i, command_len = 0;
	FILE *fd = NULL;
	char *class_str = NULL;

	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		int option = 0;

		if(!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if(argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}
		if(!end || !strncasecmp (argv[i], "Cluster",
					 MAX(command_len, 1))) {
			if(cluster_name) {
				exit_code=1;
				fprintf(stderr,
					" Can only do one cluster at a time.  "
				       "Already doing %s\n", cluster_name);
				continue;
			}
			cluster_name = xstrdup(argv[i]+end);
		} else if (!strncasecmp (argv[i], "File",
					 MAX(command_len, 1))) {
			if(file_name) {
				exit_code=1;
				fprintf(stderr,
					" File name already set to %s\n",
					file_name);
				continue;
			}
			file_name = xstrdup(argv[i]+end);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}
	}

	if(!cluster_name) {
		exit_code=1;
		fprintf(stderr, " We need a cluster to dump.\n");
		return SLURM_ERROR;
	} else {
		List temp_list = NULL;
		acct_cluster_cond_t cluster_cond;
		acct_cluster_rec_t *cluster_rec = NULL;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		cluster_cond.cluster_list = list_create(NULL);
		list_push(cluster_cond.cluster_list, cluster_name);

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		list_destroy(cluster_cond.cluster_list);
		if(!temp_list) {
			exit_code=1;
			fprintf(stderr,
				" Problem getting clusters from database.  "
				"Contact your admin.\n");
			xfree(cluster_name);
			return SLURM_ERROR;
		}

		cluster_rec = list_peek(temp_list);
		if(!cluster_rec) {
			exit_code=1;
			fprintf(stderr, " Cluster %s doesn't exist.\n",
				cluster_name);
			xfree(cluster_name);
			list_destroy(temp_list);
			return SLURM_ERROR;
		}
		class_str = get_classification_str(cluster_rec->classification);
		list_destroy(temp_list);
	}

	if(!file_name) {
		file_name = xstrdup_printf("./%s.cfg", cluster_name);
		printf(" No filename given, using %s.\n", file_name);
	}

	memset(&user_cond, 0, sizeof(acct_user_cond_t));
	user_cond.with_coords = 1;
	user_cond.with_wckeys = 1;

	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
	assoc_cond.without_parent_limits = 1;
	assoc_cond.with_raw_qos = 1;
	assoc_cond.cluster_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, cluster_name);
	/* this is needed for getting the correct wckeys */
	user_cond.assoc_cond = &assoc_cond;

	user_list = acct_storage_g_get_users(db_conn, my_uid, &user_cond);
	/* make sure this person running is an admin */
	user_name = uid_to_string(my_uid);
	if(!(user = sacctmgr_find_user_from_list(user_list, user_name))) {
		exit_code=1;
		fprintf(stderr, " Your uid (%u) is not in the "
			"accounting system, can't dump cluster.\n", my_uid);
		xfree(cluster_name);
		xfree(user_name);
		if(user_list)
			list_destroy(user_list);
		list_destroy(assoc_cond.cluster_list);
		return SLURM_ERROR;

	} else {
		if(my_uid != slurm_get_slurm_user_id() && my_uid != 0
		    && user->admin_level < ACCT_ADMIN_SUPER_USER) {
			exit_code=1;
			fprintf(stderr, " Your user does not have sufficient "
				"privileges to dump clusters.\n");
			xfree(cluster_name);
			if(user_list)
				list_destroy(user_list);
			xfree(user_name);
			list_destroy(assoc_cond.cluster_list);
			return SLURM_ERROR;
		}
	}
	xfree(user_name);

	/* assoc_cond is set up above */
	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     &assoc_cond);

	list_destroy(assoc_cond.cluster_list);
	if(!assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		xfree(cluster_name);
		return SLURM_ERROR;
	} else if(!list_count(assoc_list)) {
		exit_code=1;
		fprintf(stderr, " Cluster %s returned nothing.\n",
			cluster_name);
		list_destroy(assoc_list);
		xfree(cluster_name);
		return SLURM_ERROR;
	}

	acct_hierarchical_rec_list = get_acct_hierarchical_rec_list(assoc_list);

	acct_list = acct_storage_g_get_accounts(db_conn, my_uid, NULL);

	fd = fopen(file_name, "w");
	/* Add header */
	if(fprintf(fd,
		   "# To edit this file start with a cluster line "
		   "for the new cluster\n"
		   "# Cluster - cluster_name:MaxNodesPerJob=50\n"
		   "# Followed by Accounts you want in this fashion "
		   "(root is created by default)...\n"
		   "# Parent - root\n"
		   "# Account - cs:MaxNodesPerJob=5:MaxJobs=4:"
		   "MaxProcSecondsPerJob=20:FairShare=399:"
		   "MaxWallDurationPerJob=40:Description='Computer Science':"
		   "Organization='LC'\n"
		   "# Any of the options after a ':' can be left out and "
		   "they can be in any order.\n"
		   "# If you want to add any sub accounts just list the "
		   "Parent THAT HAS ALREADY \n"
		   "# BEEN CREATED before the account line in this fashion...\n"
		   "# Parent - cs\n"
		   "# Account - test:MaxNodesPerJob=1:MaxJobs=1:"
		   "MaxProcSecondsPerJob=1:FairShare=1:MaxWallDurationPerJob=1:"
		   "Description='Test Account':Organization='Test'\n"
		   "# To add users to a account add a line like this after a "
		   "Parent - line\n"
		   "# User - lipari:MaxNodesPerJob=2:MaxJobs=3:"
		   "MaxProcSecondsPerJob=4:FairShare=1:"
		   "MaxWallDurationPerJob=1\n") < 0) {
		exit_code=1;
		fprintf(stderr, "Can't write to file");
		xfree(cluster_name);
		return SLURM_ERROR;
	}

	line = xstrdup_printf("Cluster - %s", cluster_name);

	if(class_str)
		xstrfmtcat(line, ":Classification=%s", class_str);

	acct_hierarchical_rec = list_peek(acct_hierarchical_rec_list);
	assoc = acct_hierarchical_rec->assoc;
	if(strcmp(assoc->acct, "root"))
		fprintf(stderr, "Root association not on the top it was %s\n",
			assoc->acct);
	else
		print_file_add_limits_to_line(&line, assoc);

	if(fprintf(fd, "%s\n", line) < 0) {
		exit_code=1;
		fprintf(stderr, " Can't write to file");
		return SLURM_ERROR;
	}
	info("%s", line);

	print_file_acct_hierarchical_rec_list(
		fd, acct_hierarchical_rec_list, user_list, acct_list);

	xfree(cluster_name);
	xfree(file_name);
	list_destroy(acct_hierarchical_rec_list);
	list_destroy(assoc_list);
	fclose(fd);

	return SLURM_SUCCESS;
}
