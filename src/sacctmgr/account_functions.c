/*****************************************************************************\
 *  account_functions.c - functions dealing with accounts in the
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
		     acct_account_cond_t *acct_cond,
		     List format_list)
{
	int i;
	int a_set = 0;
	int u_set = 0;
	int end = 0;
	List qos_list = NULL;

	if(!acct_cond) {
		exit_code=1;
		fprintf(stderr, "No acct_cond given");
		return -1;
	}

	if(!acct_cond->assoc_cond) {
		acct_cond->assoc_cond = 
			xmalloc(sizeof(acct_association_cond_t));
		acct_cond->assoc_cond->fairshare = NO_VAL;
		acct_cond->assoc_cond->max_cpu_secs_per_job = NO_VAL;
		acct_cond->assoc_cond->max_jobs = NO_VAL;
		acct_cond->assoc_cond->max_nodes_per_job = NO_VAL;
		acct_cond->assoc_cond->max_wall_duration_per_job = NO_VAL;
	}

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!strncasecmp (argv[i], "Set", 3)) {
			i--;
			break;
		} else if (!strncasecmp (argv[i], "WithAssoc", 5)) {
			acct_cond->with_assocs = 1;
		} else if (!strncasecmp (argv[i], "WithCoordinators", 5)) {
			acct_cond->with_coords = 1;
		} else if(!end && !strncasecmp(argv[i], "where", 5)) {
			continue;
		} else if(!end
			  || !strncasecmp (argv[i], "Names", 1)
			  || !strncasecmp (argv[i], "Accouts", 1)) {
			if(!acct_cond->assoc_cond->acct_list) {
				acct_cond->assoc_cond->acct_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   acct_cond->assoc_cond->acct_list,
				   argv[i]+end)) 
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Clusters", 1)) {
			if(!acct_cond->assoc_cond->cluster_list) {
				acct_cond->assoc_cond->cluster_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(
				   acct_cond->assoc_cond->cluster_list,
				   argv[i]+end))
				a_set = 1;
		} else if (!strncasecmp (argv[i], "Descriptions", 1)) {
			if(!acct_cond->description_list) {
				acct_cond->description_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(acct_cond->description_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Format", 1)) {
			if(format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Organizations", 1)) {
			if(!acct_cond->organization_list) {
				acct_cond->organization_list = 
					list_create(slurm_destroy_char);
			}
			if(slurm_addto_char_list(acct_cond->organization_list,
						 argv[i]+end))
				u_set = 1;
		} else if (!strncasecmp (argv[i], "Parent", 1)) {
			acct_cond->assoc_cond->parent_acct =
				strip_quotes(argv[i]+end, NULL);
			a_set = 1;
		} else if (!strncasecmp (argv[i], "QosLevel", 1)) {
			int option = 0;
			if(!acct_cond->qos_list) {
				acct_cond->qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}
			
			addto_qos_char_list(acct_cond->qos_list, qos_list,
					    argv[i]+end, option);
			u_set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown condition: %s\n"
			       " Use keyword 'set' to modify "
			       "SLURM_PRINT_VALUE\n", argv[i]);
		}
	}

	if(qos_list)
		list_destroy(qos_list);

	(*start) = i;

	if(u_set && a_set)
		return 3;
	else if(a_set)
		return 2;
	else if(u_set)
		return 1;

	return 0;
}

static int _set_rec(int *start, int argc, char *argv[],
		    acct_account_rec_t *acct,
		    acct_association_rec_t *assoc)
{
	int i, mins;
	int u_set = 0;
	int a_set = 0;
	int end = 0;
	List qos_list = NULL;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!strncasecmp (argv[i], "Where", 5)) {
			i--;
			break;
		} else if(!end && !strncasecmp(argv[i], "set", 3)) {
			continue;
		} else if(!end) {
			exit_code=1;
			fprintf(stderr, 
				" Bad format on %s: End your option with "
			       "an '=' sign\n", argv[i]);
		} else if (!strncasecmp (argv[i], "Description", 1)) {
			acct->description =  strip_quotes(argv[i]+end, NULL);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "FairShare", 1)) {
			if (get_uint(argv[i]+end, &assoc->fairshare, 
				     "FairShare") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCPUSec", 4)) {
			if (get_uint(argv[i]+end, &assoc->max_cpu_secs_per_job,
				     "MaxCPUSec") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs", 4)) {
			if (get_uint(argv[i]+end, &assoc->max_jobs,
				     "MaxJobs") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodes", 4)) {
			if (get_uint(argv[i]+end, &assoc->max_nodes_per_job,
				     "MaxNodes") == SLURM_SUCCESS)
				a_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWall", 4)) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				assoc->max_wall_duration_per_job 
					= (uint32_t) mins;
				a_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxWall time format: %s\n", 
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "Organization", 1)) {
			acct->organization = strip_quotes(argv[i]+end, NULL);
			u_set = 1;
		} else if (!strncasecmp (argv[i], "Parent", 1)) {
			assoc->parent_acct = strip_quotes(argv[i]+end, NULL);
			a_set = 1;
		} else if (!strncasecmp (argv[i], "QosLevel=", 1)) {
			int option = 0;
			if(!acct->qos_list) {
				acct->qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}
			if(end > 2 && argv[i][end-1] == '='
			   && (argv[i][end-2] == '+' 
			       || argv[i][end-2] == '-'))
				option = (int)argv[i][end-2];

			addto_qos_char_list(acct->qos_list, qos_list,
					    argv[i]+end, option);
			u_set = 1;
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n"
				" Use keyword 'where' to modify condition\n",
				argv[i]);
		}
	}
	if(qos_list)
		list_destroy(qos_list);

	(*start) = i;

	if(u_set && a_set)
		return 3;
	else if(a_set)
		return 2;
	else if(u_set)
		return 1;
	return 0;
}

static int _isdefault(List acct_list)
{
	int rc = 0;
	acct_user_cond_t user_cond;
	List ret_list = NULL;

	if(!acct_list || !list_count(acct_list))
		return rc;

	memset(&user_cond, 0, sizeof(acct_user_cond_t));
	user_cond.def_acct_list = acct_list;

	ret_list = acct_storage_g_get_users(db_conn, my_uid, &user_cond);
	if(ret_list && list_count(ret_list)) {
		ListIterator itr = list_iterator_create(ret_list);
		acct_user_rec_t *user = NULL;
		fprintf(stderr," Users listed below have these "
			"as their Default Accounts.\n");
		while((user = list_next(itr))) {
			fprintf(stderr, " User - %-10.10s Account - %s\n",
				user->name, user->default_acct);
		}
		list_iterator_destroy(itr);
		rc = 1;		
	}

	if(ret_list)
		list_destroy(ret_list);

	return rc;
}

extern int sacctmgr_add_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	int i=0, mins;
	ListIterator itr = NULL, itr_c = NULL;
	acct_account_rec_t *acct = NULL;
	acct_association_rec_t *assoc = NULL;
	acct_association_cond_t assoc_cond;
	List name_list = list_create(slurm_destroy_char);
	List cluster_list = list_create(slurm_destroy_char);
	char *description = NULL;
	char *organization = NULL;
	char *parent = NULL;
	char *cluster = NULL;
	char *name = NULL;
	List add_qos_list = NULL;
	List qos_list = NULL;
	List acct_list = NULL;
	List assoc_list = NULL;
	List local_assoc_list = NULL;
	List local_account_list = NULL;
	uint32_t fairshare = NO_VAL; 
	uint32_t max_jobs = NO_VAL;
	uint32_t max_nodes_per_job = NO_VAL;
	uint32_t max_wall_duration_per_job = NO_VAL;
	uint32_t max_cpu_secs_per_job = NO_VAL;
	char *acct_str = NULL;
	char *assoc_str = NULL;
	int limit_set = 0;
	
	for (i=0; i<argc; i++) {
		int end = parse_option_end(argv[i]);
		if(!end) {
			slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Cluster", 1)) {
			slurm_addto_char_list(cluster_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Description", 1)) {
			description = strip_quotes(argv[i]+end, NULL);
		} else if (!strncasecmp (argv[i], "FairShare", 1)) {
			if (get_uint(argv[i]+end, &fairshare, 
			    "FairShare") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxCPUSecs", 4)) {
			if (get_uint(argv[i]+end, &max_cpu_secs_per_job, 
			    "MaxCPUSecs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxJobs", 4)) {
			if (get_uint(argv[i]+end, &max_jobs, 
			    "MaxJobs") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxNodes", 4)) {
			if (get_uint(argv[i]+end, &max_nodes_per_job, 
			    "MaxNodes") == SLURM_SUCCESS)
				limit_set = 1;
		} else if (!strncasecmp (argv[i], "MaxWall", 4)) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				max_wall_duration_per_job = (uint32_t) mins;
				limit_set = 1;
			} else {
				exit_code=1;
				fprintf(stderr, 
					" Bad MaxWall time format: %s\n", 
					argv[i]);
			}
		} else if (!strncasecmp (argv[i], "Names", 1)) {
			slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!strncasecmp (argv[i], "Organization", 1)) {
			organization = strip_quotes(argv[i]+end, NULL);
		} else if (!strncasecmp (argv[i], "Parent", 1)) {
			parent = strip_quotes(argv[i]+end, NULL);
		} else if (!strncasecmp (argv[i], "QosLevel", 1)) {
			int option = 0;
			if(!add_qos_list) {
				add_qos_list = 
					list_create(slurm_destroy_char);
			}
			
			if(!qos_list) {
				qos_list = acct_storage_g_get_qos(
					db_conn, my_uid, NULL);
			}
			addto_qos_char_list(add_qos_list, qos_list,
					    argv[i]+end, option);
		} else {
			exit_code=1;
			fprintf(stderr, " Unknown option: %s\n", argv[i]);
		}		
	}

	if(!list_count(name_list)) {
		list_destroy(name_list);
		list_destroy(cluster_list);
		xfree(parent);
		xfree(description);
		xfree(organization);
		exit_code=1;
		fprintf(stderr, " Need name of account to add.\n"); 
		return SLURM_SUCCESS;
	} else {
		acct_account_cond_t account_cond;
		acct_association_cond_t assoc_cond;

		memset(&account_cond, 0, sizeof(acct_account_cond_t));
		memset(&assoc_cond, 0, sizeof(acct_association_cond_t));
		assoc_cond.acct_list = name_list;
		account_cond.assoc_cond = &assoc_cond;

		local_account_list = acct_storage_g_get_accounts(
			db_conn, my_uid, &account_cond);
		
	}
	if(!local_account_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting accounts from database.  "
			"Contact your admin.\n");
		list_destroy(name_list);
		list_destroy(cluster_list);
		xfree(parent);
		xfree(description);
		xfree(organization);
		return SLURM_ERROR;
	}

	if(!parent)
		parent = xstrdup("root");

	if(!list_count(cluster_list)) {
		List temp_list = NULL;
		acct_cluster_rec_t *cluster_rec = NULL;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid, NULL);
		if(!cluster_list) {
			exit_code=1;
			fprintf(stderr, 
				" Problem getting clusters from database.  "
			       "Contact your admin.\n");
			list_destroy(name_list);
			list_destroy(cluster_list);
			list_destroy(local_account_list);
			xfree(parent);
			xfree(description);
			xfree(organization);
			return SLURM_ERROR;
		}

		itr_c = list_iterator_create(temp_list);
		while((cluster_rec = list_next(itr_c))) {
			list_append(cluster_list, xstrdup(cluster_rec->name));
		}
		list_iterator_destroy(itr_c);

		if(!list_count(cluster_list)) {
			exit_code=1;
			fprintf(stderr, 
				"  Can't add accounts, no cluster "
				"defined yet.\n"
				" Please contact your administrator.\n");
			list_destroy(name_list);
			list_destroy(cluster_list);
			list_destroy(local_account_list);
			xfree(parent);
			xfree(description);
			xfree(organization);
			return SLURM_ERROR; 
		}
	} else {
		List temp_list = NULL;
		acct_cluster_cond_t cluster_cond;

		memset(&cluster_cond, 0, sizeof(acct_cluster_cond_t));
		cluster_cond.cluster_list = cluster_list;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
		
		itr_c = list_iterator_create(cluster_list);
		itr = list_iterator_create(temp_list);
		while((cluster = list_next(itr_c))) {
			acct_cluster_rec_t *cluster_rec = NULL;

			list_iterator_reset(itr);
			while((cluster_rec = list_next(itr))) {
				if(!strcasecmp(cluster_rec->name, cluster))
					break;
			}
			if(!cluster_rec) {
				exit_code=1;
				fprintf(stderr, " This cluster '%s' "
				       "doesn't exist.\n"
				       "        Contact your admin "
				       "to add it to accounting.\n",
				       cluster);
				list_delete_item(itr_c);
			}
		}
		list_iterator_destroy(itr);
		list_iterator_destroy(itr_c);
		list_destroy(temp_list);
		if(!list_count(cluster_list)) {
			list_destroy(name_list);
			list_destroy(cluster_list);
			list_destroy(local_account_list);
			return SLURM_ERROR;
		}
	}

		
	acct_list = list_create(destroy_acct_account_rec);
	assoc_list = list_create(destroy_acct_association_rec);
	
	memset(&assoc_cond, 0, sizeof(acct_association_cond_t));

	assoc_cond.acct_list = list_create(NULL);
	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		list_append(assoc_cond.acct_list, name);
	}
	list_iterator_destroy(itr);
	list_append(assoc_cond.acct_list, parent);

	assoc_cond.cluster_list = cluster_list;
	local_assoc_list = acct_storage_g_get_associations(
		db_conn, my_uid, &assoc_cond);	
	list_destroy(assoc_cond.acct_list);
	if(!local_assoc_list) {
		exit_code=1;
		fprintf(stderr, " Problem getting associations from database.  "
		       "Contact your admin.\n");
		list_destroy(name_list);
		list_destroy(cluster_list);
		list_destroy(local_account_list);
		xfree(parent);
		xfree(description);
		xfree(organization);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(name_list);
	while((name = list_next(itr))) {
		acct = NULL;
		if(!sacctmgr_find_account_from_list(local_account_list, name)) {
			acct = xmalloc(sizeof(acct_account_rec_t));
			acct->assoc_list = 
				list_create(destroy_acct_association_rec);	
			acct->name = xstrdup(name);
			if(description) 
				acct->description = xstrdup(description);
			else
				acct->description = xstrdup(name);

			if(organization)
				acct->organization = xstrdup(organization);
			else if(strcmp(parent, "root"))
				acct->organization = xstrdup(parent);
			else
				acct->organization = xstrdup(name);
			if(add_qos_list && list_count(add_qos_list)) {
				char *tmp_qos = NULL;
				ListIterator qos_itr = 
					list_iterator_create(add_qos_list);
				acct->qos_list = 
					list_create(slurm_destroy_char);
				while((tmp_qos = list_next(qos_itr))) {
					list_append(acct->qos_list,
						    xstrdup(tmp_qos));
				}
				list_iterator_destroy(qos_itr);
			}
			xstrfmtcat(acct_str, "  %s\n", name);
			list_append(acct_list, acct);
		}

		itr_c = list_iterator_create(cluster_list);
		while((cluster = list_next(itr_c))) {
			if(sacctmgr_find_account_base_assoc_from_list(
				   local_assoc_list, name, cluster)) {
				//printf(" already have this assoc\n");
				continue;
			}
			if(!sacctmgr_find_account_base_assoc_from_list(
				   local_assoc_list, parent, cluster)) {
				exit_code=1;
				fprintf(stderr, " Parent account '%s' "
				       "doesn't exist on "
				       "cluster %s\n"
				       "        Contact your admin "
				       "to add this account.\n",
				       parent, cluster);
				continue;
			}

			assoc = xmalloc(sizeof(acct_association_rec_t));
			assoc->acct = xstrdup(name);
			assoc->cluster = xstrdup(cluster);
			assoc->parent_acct = xstrdup(parent);
			assoc->fairshare = fairshare;
			assoc->max_jobs = max_jobs;
			assoc->max_nodes_per_job = max_nodes_per_job;
			assoc->max_wall_duration_per_job =
				max_wall_duration_per_job;
			assoc->max_cpu_secs_per_job = 
				max_cpu_secs_per_job;
			if(acct) 
				list_append(acct->assoc_list, assoc);
			else 
				list_append(assoc_list, assoc);
			xstrfmtcat(assoc_str,
				   "  A = %-10.10s"
				   " C = %-10.10s\n",
				   assoc->acct,
				   assoc->cluster);		

		}
		list_iterator_destroy(itr_c);
	}
	list_iterator_destroy(itr);
	list_destroy(local_account_list);
	list_destroy(local_assoc_list);
	list_destroy(name_list);
	list_destroy(cluster_list);

	if(!list_count(acct_list) && !list_count(assoc_list)) {
		printf(" Nothing new added.\n");
		goto end_it;
	} else if(!assoc_str) {
		exit_code=1;
		fprintf(stderr, " No associations created.\n");
		goto end_it;
	}

	if(acct_str) {
		printf(" Adding Account(s)\n%s", acct_str);
		printf(" Settings\n");
		if(description)
			printf("  Description     = %s\n", description);
		else
			printf("  Description     = %s\n", "Account Name");
			
		if(organization)
			printf("  Organization    = %s\n", organization);
		else
			printf("  Organization    = %s\n",
			       "Parent/Account Name");

		if(add_qos_list) {
			char *temp_char = get_qos_complete_str(
				qos_list, add_qos_list);
			if(temp_char) {		
				printf("  Qos             = %s\n", temp_char);
				xfree(temp_char);
			}
		}
		xfree(acct_str);
	}

	if(assoc_str) {
		printf(" Associations\n%s", assoc_str);
		xfree(assoc_str);
	}

	if(limit_set) {
		printf(" Settings\n");
		if(fairshare == INFINITE)
			printf("  Fairshare       = NONE\n");
		else if(fairshare != NO_VAL) 
			printf("  Fairshare       = %u\n", fairshare);
		
		if(max_cpu_secs_per_job == INFINITE)
			printf("  MaxCPUSecs      = NONE\n");
		else if(max_cpu_secs_per_job != NO_VAL) 
			printf("  MaxCPUSecs      = %u\n",
			       max_cpu_secs_per_job);
		
		if(max_jobs == INFINITE) 
			printf("  MaxJobs         = NONE\n");
		else if(max_jobs != NO_VAL) 
			printf("  MaxJobs         = %u\n", max_jobs);
		
		if(max_nodes_per_job == INFINITE)
			printf("  MaxNodes        = NONE\n");
		else if(max_nodes_per_job != NO_VAL)
			printf("  MaxNodes        = %u\n", max_nodes_per_job);
		
		if(max_wall_duration_per_job == INFINITE) 
			printf("  MaxWall         = NONE\n");		
		else if(max_wall_duration_per_job != NO_VAL) {
			char time_buf[32];
			mins2time_str((time_t) max_wall_duration_per_job, 
				      time_buf, sizeof(time_buf));
			printf("  MaxWall         = %s\n", time_buf);
		}
	}
	
	notice_thread_init();
	if(list_count(acct_list)) 
		rc = acct_storage_g_add_accounts(db_conn, my_uid, acct_list);
	

	if(rc == SLURM_SUCCESS) {
		if(list_count(assoc_list)) 
			rc = acct_storage_g_add_associations(db_conn, my_uid, 
							     assoc_list);
	} else {
		exit_code=1;
		fprintf(stderr, " Problem adding accounts\n");
		rc = SLURM_ERROR;
		notice_thread_fini();
		goto end_it;
	}
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
		fprintf(stderr, 
			" error: Problem adding account associations\n");
		rc = SLURM_ERROR;
	}

end_it:
	if(add_qos_list)
		list_destroy(add_qos_list);
	list_destroy(acct_list);
	list_destroy(assoc_list);
		
	xfree(parent);
	xfree(description);
	xfree(organization);

	return rc;
}

extern int sacctmgr_list_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond =
		xmalloc(sizeof(acct_account_cond_t));
 	List acct_list;
	int i=0, set=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	acct_account_rec_t *acct = NULL;
	acct_association_rec_t *assoc = NULL;
	char *object;
	List qos_list = NULL;

	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(slurm_destroy_char);
	List print_fields_list; /* types are of print_field_t */

	enum {
		PRINT_ACCOUNT,
		PRINT_CLUSTER,
		PRINT_COORDS,
		PRINT_DESC,
		PRINT_FAIRSHARE,
		PRINT_ID,
		PRINT_MAXC,
		PRINT_MAXJ,
		PRINT_MAXN,
		PRINT_MAXW,
		PRINT_ORG,
		PRINT_QOS,
		PRINT_QOS_RAW,
		PRINT_PID,
		PRINT_PNAME,
		PRINT_PART,
		PRINT_USER
	};

	acct_cond->with_assocs = with_assoc_flag;

	set = _set_cond(&i, argc, argv, acct_cond, format_list);

	if(exit_code) {
		destroy_acct_account_cond(acct_cond);
		list_destroy(format_list);
		return SLURM_ERROR;
	} else if(!list_count(format_list)) {
		slurm_addto_char_list(format_list, "A,D,O,Q");
		if(acct_cond->with_assocs)
			slurm_addto_char_list(format_list,
					"Cl,ParentN,U,F,MaxC,MaxJ,MaxN,MaxW");
			
		if(acct_cond->with_coords)
			slurm_addto_char_list(format_list, "Coord");
			
	}
	
	if(!acct_cond->with_assocs && set > 1) {
		if(!commit_check("You requested options that are only vaild "
				 "when querying with the withassoc option.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			list_destroy(format_list);
			destroy_acct_account_cond(acct_cond);
			return SLURM_SUCCESS;
		}		
	}

	print_fields_list = list_create(destroy_print_field);

	itr = list_iterator_create(format_list);
	while((object = list_next(itr))) {
		field = xmalloc(sizeof(print_field_t));
		if(!strncasecmp("Account", object, 1)
		   || !strncasecmp("Name", object, 2)) {
			field->type = PRINT_ACCOUNT;
			field->name = xstrdup("Account");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Cluster", object, 2)) {
			field->type = PRINT_CLUSTER;
			field->name = xstrdup("Cluster");
			field->len = 10;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("Coordinators", object, 2)) {
			field->type = PRINT_COORDS;
			field->name = xstrdup("Coordinators");
			field->len = 20;
			field->print_routine = sacctmgr_print_coord_list;
		} else if(!strncasecmp("Description", object, 1)) {
			field->type = PRINT_DESC;
			field->name = xstrdup("Descr");
			field->len = 20;
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
		} else if(!strncasecmp("MaxCPUSecs", object, 4)) {
			field->type = PRINT_MAXC;
			field->name = xstrdup("MaxCPUSecs");
			field->len = 11;
			field->print_routine = print_fields_uint;
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
		} else if(!strncasecmp("Organization", object, 1)) {
			field->type = PRINT_ORG;
			field->name = xstrdup("Org");
			field->len = 20;
			field->print_routine = print_fields_str;
		} else if(!strncasecmp("QOSRAW", object, 4)) {
			field->type = PRINT_QOS_RAW;
			field->name = xstrdup("QOS_RAW");
			field->len = 10;
			field->print_routine = print_fields_char_list;
		} else if(!strncasecmp("QOS", object, 1)) {
			field->type = PRINT_QOS;
			field->name = xstrdup("QOS");
			field->len = 20;
			field->print_routine = sacctmgr_print_qos_list;
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
		} else if(!strncasecmp("User", object, 1)) {
			field->type = PRINT_USER;
			field->name = xstrdup("User");
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
		destroy_acct_account_cond(acct_cond);
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	acct_list = acct_storage_g_get_accounts(db_conn, my_uid, acct_cond);	
	destroy_acct_account_cond(acct_cond);

	if(!acct_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		list_destroy(print_fields_list);
		return SLURM_ERROR;
	}

	itr = list_iterator_create(acct_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while((acct = list_next(itr))) {
		if(acct->assoc_list) {
			ListIterator itr3 =
				list_iterator_create(acct->assoc_list);
			while((assoc = list_next(itr3))) {
				int curr_inx = 1;
				while((field = list_next(itr2))) {
					switch(field->type) {
					case PRINT_ACCOUNT:
						field->print_routine(
							field, acct->name,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_CLUSTER:
						field->print_routine(
							field, assoc->cluster,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_COORDS:
						field->print_routine(
							field,
							acct->coordinators,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_DESC:
						field->print_routine(
							field, 
							acct->description,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_FAIRSHARE:
						field->print_routine(
							field, 
							assoc->fairshare,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_ID:
						field->print_routine(
							field, assoc->id,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_MAXC:
						field->print_routine(
							field, assoc->
							max_cpu_secs_per_job,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_MAXJ:
						field->print_routine(
							field, assoc->max_jobs,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_MAXN:
						field->print_routine(
							field, assoc->
							max_nodes_per_job,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_MAXW:
						field->print_routine(
							field, 
							assoc->
							max_wall_duration_per_job,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_ORG:
						field->print_routine(
							field, 
							acct->organization,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_QOS:
						if(!qos_list) {
							qos_list = 
								acct_storage_g_get_qos(
									db_conn,
									my_uid,
									NULL);
						}
						field->print_routine(
							field, 
							qos_list,
							acct->qos_list,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_QOS_RAW:
						if(!qos_list) {
							qos_list = 
								acct_storage_g_get_qos(
									db_conn,
									my_uid,
									NULL);
						}
						field->print_routine(
							field,
							qos_list,
							acct->qos_list,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_PID:
						field->print_routine(
							field, 
							assoc->parent_id,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_PNAME:
						field->print_routine(
							field, 
							assoc->parent_acct,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_PART:
						field->print_routine(
							field, 
							assoc->partition,
							(curr_inx == 
							 field_count));
						break;
					case PRINT_USER:
						field->print_routine(
							field, assoc->user,
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
			list_iterator_destroy(itr3);		
		} else {
			int curr_inx = 1;
			while((field = list_next(itr2))) {
				switch(field->type) {
				case PRINT_ACCOUNT:
					field->print_routine(
						field, acct->name,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_CLUSTER:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_COORDS:
					field->print_routine(
						field,
						acct->coordinators,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_DESC:
					field->print_routine(
						field, acct->description,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_FAIRSHARE:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_ID:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_MAXC:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_MAXJ:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_MAXN:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_MAXW:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_ORG:
					field->print_routine(
						field, acct->organization,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_QOS:
					if(!qos_list) {
						qos_list = 
							acct_storage_g_get_qos(
								db_conn,
								my_uid,
								NULL);
					}
					field->print_routine(
						field, qos_list,
						acct->qos_list,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_QOS_RAW:
					if(!qos_list) {
						qos_list = 
							acct_storage_g_get_qos(
								db_conn,
								my_uid,
								NULL);
					}
					field->print_routine(
						field, qos_list,
						acct->qos_list,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_PID:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_PNAME:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_PART:
					field->print_routine(
						field, NULL,
							(curr_inx == 
							 field_count));
					break;
				case PRINT_USER:
					field->print_routine(
						field, NULL,
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
	}

	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	list_destroy(acct_list);
	list_destroy(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond = 
		xmalloc(sizeof(acct_account_cond_t));
	acct_account_rec_t *acct = xmalloc(sizeof(acct_account_rec_t));
	acct_association_rec_t *assoc = xmalloc(sizeof(acct_association_rec_t));
	
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	assoc->fairshare = NO_VAL;
	assoc->max_cpu_secs_per_job = NO_VAL;
	assoc->max_jobs = NO_VAL;
	assoc->max_nodes_per_job = NO_VAL;
	assoc->max_wall_duration_per_job = NO_VAL;

	for (i=0; i<argc; i++) {
		if (!strncasecmp (argv[i], "Where", 5)) {
			i++;
			cond_set = _set_cond(&i, argc, argv, acct_cond, NULL);
		} else if (!strncasecmp (argv[i], "Set", 3)) {
			i++;
			rec_set = _set_rec(&i, argc, argv, acct, assoc);
		} else {
			cond_set = _set_cond(&i, argc, argv, acct_cond, NULL);
		}
	}

	if(!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		destroy_acct_account_cond(acct_cond);
		destroy_acct_account_rec(acct);
		destroy_acct_association_rec(assoc);
		return SLURM_ERROR;
	} else if(!cond_set) {
		if(!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			destroy_acct_account_cond(acct_cond);
			destroy_acct_account_rec(acct);
			destroy_acct_association_rec(assoc);
			return SLURM_SUCCESS;
		}		
	}

	notice_thread_init();
	if(rec_set == 3 || rec_set == 1) { // process the account changes
		if(cond_set == 2) {
			exit_code=1;
			fprintf(stderr, 
				" There was a problem with your "
				"'where' options.\n");
			rc = SLURM_ERROR;
			goto assoc_start;
		}
		ret_list = acct_storage_g_modify_accounts(
			db_conn, my_uid, acct_cond, acct);
		if(ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified accounts...\n");
			while((object = list_next(itr))) {
				printf("  %s\n", object);
			}
			list_iterator_destroy(itr);
			set = 1;
		} else if(ret_list) {
			printf(" Nothing modified\n");
			rc = SLURM_ERROR;
		} else {
			exit_code=1;
			fprintf(stderr, " Error with request\n");
			rc = SLURM_ERROR;
		}

		if(ret_list)
			list_destroy(ret_list);
	}

assoc_start:
	if(rec_set == 3 || rec_set == 2) { // process the association changes
		if(cond_set == 1 && !acct_cond->assoc_cond->acct_list) {
			rc = SLURM_ERROR;
			exit_code=1;
			fprintf(stderr, 
				" There was a problem with your "
				"'where' options.\n");
			goto assoc_end;
		}

		ret_list = acct_storage_g_modify_associations(
			db_conn, my_uid, acct_cond->assoc_cond, assoc);

		if(ret_list && list_count(ret_list)) {
			char *object = NULL;
			ListIterator itr = list_iterator_create(ret_list);
			printf(" Modified account associations...\n");
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

assoc_end:

	notice_thread_fini();
	if(set) {
		if(commit_check("Would you like to commit changes?")) 
			acct_storage_g_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			acct_storage_g_commit(db_conn, 0);
		}
	}
	destroy_acct_account_cond(acct_cond);
	destroy_acct_account_rec(acct);
	destroy_acct_association_rec(assoc);	

	return rc;
}

extern int sacctmgr_delete_account(int argc, char *argv[])
{
	int rc = SLURM_SUCCESS;
	acct_account_cond_t *acct_cond =
		xmalloc(sizeof(acct_account_cond_t));
	int i=0;
	List ret_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	
	if(!(set = _set_cond(&i, argc, argv, acct_cond, NULL))) {
		exit_code=1;
		fprintf(stderr, 
			" No conditions given to remove, not executing.\n");
		destroy_acct_account_cond(acct_cond);
		return SLURM_ERROR;
	}

	/* check to see if person is trying to remove root account.  This is
	 * bad, and should not be allowed outside of deleting a cluster.
	 */
	if(acct_cond->assoc_cond
	   && acct_cond->assoc_cond->acct_list
	   && list_count(acct_cond->assoc_cond->acct_list)) {
		char *tmp_char = NULL;
		itr = list_iterator_create(acct_cond->assoc_cond->acct_list);
		while((tmp_char = list_next(itr))) {
			if(!strcasecmp(tmp_char, "root")) 
				break;
		}
		list_iterator_destroy(itr);
		if(tmp_char) {
			exit_code=1;
			fprintf(stderr, " You are not allowed to remove "
			       "the root account.\n"
			       " Use remove cluster instead.\n");
			destroy_acct_account_cond(acct_cond);
			return SLURM_ERROR;
		}
	}

	notice_thread_init();
	if(set == 1) {
		ret_list = acct_storage_g_remove_accounts(
			db_conn, my_uid, acct_cond);		
	} else if(set == 2 || set == 3) {
		ret_list = acct_storage_g_remove_associations(
			db_conn, my_uid, acct_cond->assoc_cond);
	}
	notice_thread_fini();
	destroy_acct_account_cond(acct_cond);
	
	if(ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = NULL;

		/* Check to see if person is trying to remove a default
		 * account of a user.
		 */
		if(_isdefault(ret_list)) {
			exit_code=1;
			fprintf(stderr, " Please either remove accounts listed "
				"above from list and resubmit,\n"
				" or change these users default account to "
				"remove the account(s).\n"
				" Changes Discarded\n");
			list_destroy(ret_list);
			acct_storage_g_commit(db_conn, 0);
			return SLURM_ERROR;	
		}
		itr = list_iterator_create(ret_list);
		if(set == 1) {
			printf(" Deleting accounts...\n");
		} else if(set == 2 || set == 3) {
			printf(" Deleting account associations...\n");
		}
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
