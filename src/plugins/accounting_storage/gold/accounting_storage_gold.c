/*****************************************************************************\
 *  accounting_storage_gold.c - accounting interface to gold.
 *
 *  $Id: accounting_gold.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <pwd.h>


#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/common/uid.h"
#include <src/common/parse_time.h>

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmdbd/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/jobacct_common.h"

#include "src/database/gold_interface.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Accounting storage GOLD plugin";
const char plugin_type[] = "accounting_storage/gold";
const uint32_t plugin_version = 100;

static List local_association_list = NULL;

static int _add_edit_job(struct job_record *job_ptr, gold_object_t action);
static int _check_for_job(uint32_t jobid, time_t submit);
static List _get_association_list_from_response(gold_response_t *gold_response);
/* static int _get_cluster_accounting_list_from_response( */
/* 	gold_response_t *gold_response,  */
/* 	acct_cluster_rec_t *cluster_rec); */
/* static int _get_acct_accounting_list_from_response( */
/* 	gold_response_t *gold_response, */
/* 	acct_association_rec_t *account_rec); */
static List _get_user_list_from_response(gold_response_t *gold_response);
static List _get_acct_list_from_response(gold_response_t *gold_response);
static List _get_cluster_list_from_response(gold_response_t *gold_response);
static int _remove_association_accounting(List id_list);


static int _add_edit_job(struct job_record *job_ptr, gold_object_t action)
{
	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_JOB,
							   action);
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;
	char *jname = NULL;
	char *nodes = "(null)";

	if(!gold_request) 
		return rc;

	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	
	
//info("total procs is  %d", job_ptr->total_procs);
	if(action == GOLD_ACTION_CREATE) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", job_ptr->job_id);
		gold_request_add_assignment(gold_request, "JobId", tmp_buff);
		
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 (int)job_ptr->details->submit_time);
		gold_request_add_assignment(gold_request, "SubmitTime",
					    tmp_buff);
	} else if (action == GOLD_ACTION_MODIFY) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", job_ptr->job_id);
		gold_request_add_condition(gold_request, "JobId", tmp_buff,
					   GOLD_OPERATOR_NONE, 0);
		
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 (int)job_ptr->details->submit_time);
		gold_request_add_condition(gold_request, "SubmitTime",
					   tmp_buff,
					   GOLD_OPERATOR_NONE, 0);
	} else {
		destroy_gold_request(gold_request);
		error("_add_edit_job: bad action given %d", action);		
		return rc;
	}

	if (job_ptr->name && job_ptr->name[0]) {
		int i;
		jname = xmalloc(strlen(job_ptr->name) + 1);
		for (i=0; job_ptr->name[i]; i++) {
			if (isalnum(job_ptr->name[i]))
				jname[i] = job_ptr->name[i];
			else
				jname[i] = '_';
		}
	} else
		jname = xstrdup("allocation");

	gold_request_add_assignment(gold_request, "JobName", jname);
	xfree(jname);
	
	gold_request_add_assignment(gold_request, "Partition",
				    job_ptr->partition);
	
	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 job_ptr->total_procs);
	gold_request_add_assignment(gold_request, "RequestedCPUCount",
				    tmp_buff);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 job_ptr->total_procs);
	gold_request_add_assignment(gold_request, "AllocatedCPUCount",
				    tmp_buff);
	
	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 (int)job_ptr->details->begin_time);
	gold_request_add_assignment(gold_request, "EligibleTime",
				    tmp_buff);

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", job_ptr->assoc_id);
	gold_request_add_assignment(gold_request, "GoldAccountId", tmp_buff);

	gold_request_add_assignment(gold_request, "NodeList", nodes);

	if(job_ptr->job_state >= JOB_COMPLETE) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 (int)job_ptr->end_time);
		gold_request_add_assignment(gold_request, "EndTime",
					    tmp_buff);		
		
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 (int)job_ptr->exit_code);
		gold_request_add_assignment(gold_request, "ExitCode",
					    tmp_buff);
	}

	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 (int)job_ptr->start_time);
	gold_request_add_assignment(gold_request, "StartTime", tmp_buff);
		
	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 job_ptr->job_state & (~JOB_COMPLETING));
	gold_request_add_assignment(gold_request, "State", tmp_buff);	

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("_add_edit_job: no response received");
		return rc;
	}

	if(!gold_response->rc) 
		rc = SLURM_SUCCESS;
	else {
		if(gold_response->rc == 720)
			error("gold_response has non-zero rc(%d): "
			      "NOT PRINTING MESSAGE: this was a parser error",
			      gold_response->rc);
		else
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
		errno = gold_response->rc;
	}
	destroy_gold_response(gold_response);

	return rc;
}

static int _check_for_job(uint32_t jobid, time_t submit) 
{
	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_JOB,
							   GOLD_ACTION_QUERY);
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = 0;

	if(!gold_request) 
		return rc;

	gold_request_add_selection(gold_request, "JobId");

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", jobid);
	gold_request_add_condition(gold_request, "JobId", tmp_buff,
				   GOLD_OPERATOR_NONE, 0);

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)submit);
	gold_request_add_condition(gold_request, "SubmitTime", tmp_buff,
				   GOLD_OPERATOR_NONE, 0);

	gold_response = get_gold_response(gold_request);
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("_check_for_job: no response received");
		return 0;
	}

	if(gold_response->entry_cnt > 0) 
		rc = 1;
	destroy_gold_response(gold_response);
	
	return rc;
}

static List _get_association_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List association_list = NULL;
	acct_association_rec_t *acct_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	association_list = list_create(destroy_acct_association_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		acct_rec = xmalloc(sizeof(acct_association_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Id")) {
				acct_rec->id = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "FairShare")) {
				acct_rec->fairshare = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxJobs")) {
				acct_rec->max_jobs = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxNodesPerJob")) {
				acct_rec->max_nodes_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxWallDurationPerJob")) {
				acct_rec->max_wall_duration_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "MaxProcSecondsPerJob")) {
				acct_rec->max_cpu_secs_per_job = 
					atoi(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "User")) {
				if(strcmp(name_val->name, "NONE"))
					acct_rec->user = 
						xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Project")) {
				acct_rec->acct = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Machine")) {
				acct_rec->cluster = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(association_list, acct_rec);
	}
	list_iterator_destroy(itr);

	return association_list;
}

/* static int _get_cluster_accounting_list_from_response( */
/* 	gold_response_t *gold_response, */
/* 	acct_cluster_rec_t *cluster_rec) */
/* { */
/* 	ListIterator itr = NULL; */
/* 	ListIterator itr2 = NULL; */
/* 	cluster_accounting_rec_t *clusteracct_rec = NULL; */
/* 	gold_response_entry_t *resp_entry = NULL; */
/* 	gold_name_value_t *name_val = NULL; */
	
/* 	if(gold_response->entry_cnt <= 0) { */
/* 		debug2("_get_list_from_response: No entries given"); */
/* 		return SLURM_ERROR; */
/* 	} */
/* 	if(!cluster_rec->accounting_list) */
/* 		cluster_rec->accounting_list =  */
/* 			list_create(destroy_cluster_accounting_rec); */
	
/* 	itr = list_iterator_create(gold_response->entries); */
/* 	while((resp_entry = list_next(itr))) { */
/* 		clusteracct_rec = xmalloc(sizeof(cluster_accounting_rec_t)); */
/* 		itr2 = list_iterator_create(resp_entry->name_val); */
/* 		while((name_val = list_next(itr2))) { */
/* 			if(!strcmp(name_val->name, "CPUCount")) { */
/* 				clusteracct_rec->cpu_count =  */
/* 					atoi(name_val->value); */
/* 			} else if(!strcmp(name_val->name,  */
/* 					  "PeriodStart")) { */
/* 				clusteracct_rec->period_start =  */
/* 					atoi(name_val->value); */
/* 			} else if(!strcmp(name_val->name,  */
/* 					  "IdleCPUSeconds")) { */
/* 				clusteracct_rec->idle_secs =  */
/* 					atoi(name_val->value); */
/* 			} else if(!strcmp(name_val->name,  */
/* 					  "DownCPUSeconds")) { */
/* 				clusteracct_rec->down_secs =  */
/* 					atoi(name_val->value); */
/* 			} else if(!strcmp(name_val->name,  */
/* 					  "AllocatedCPUSeconds")) { */
/* 				clusteracct_rec->alloc_secs =  */
/* 					atoi(name_val->value); */
/* 			} else if(!strcmp(name_val->name,  */
/* 					  "ReservedCPUSeconds")) { */
/* 				clusteracct_rec->resv_secs =  */
/* 					atoi(name_val->value); */
/* 			} else { */
/* 				error("Unknown name val of '%s' = '%s'", */
/* 				      name_val->name, name_val->value); */
/* 			} */
/* 		} */
/* 		list_iterator_destroy(itr2); */
/* 		list_append(cluster_rec->accounting_list, clusteracct_rec); */
/* 	} */
/* 	list_iterator_destroy(itr); */

/* 	return SLURM_SUCCESS; */
/* } */

/* static int _get_acct_accounting_list_from_response( */
/* 	gold_response_t *gold_response, */
/* 	acct_association_rec_t *acct_rec) */
/* { */
/* 	ListIterator itr = NULL; */
/* 	ListIterator itr2 = NULL; */
/* 	acct_accounting_rec_t *accounting_rec = NULL; */
/* 	gold_response_entry_t *resp_entry = NULL; */
/* 	gold_name_value_t *name_val = NULL; */
	
/* 	if(!acct_rec->accounting_list) */
/* 		acct_rec->accounting_list = */
/* 			list_create(destroy_acct_accounting_rec); */
	
/* 	itr = list_iterator_create(gold_response->entries); */
/* 	while((resp_entry = list_next(itr))) { */
/* 		accounting_rec = xmalloc(sizeof(acct_accounting_rec_t)); */

/* 		itr2 = list_iterator_create(resp_entry->name_val); */
/* 		while((name_val = list_next(itr2))) { */
/* 			if(!strcmp(name_val->name, "PeriodStart")) { */
/* 				accounting_rec->period_start =  */
/* 					atoi(name_val->value); */
/* 			} else if(!strcmp(name_val->name, */
/* 					  "AllocatedCPUSeconds")) { */
/* 				accounting_rec->alloc_secs =  */
/* 					atoi(name_val->value); */
/* 			} else { */
/* 				error("Unknown name val of '%s' = '%s'", */
/* 				      name_val->name, name_val->value); */
/* 			} */
/* 		} */
/* 		list_iterator_destroy(itr2); */
/* 		list_append(acct_rec->accounting_list, accounting_rec); */
/* 	} */
/* 	list_iterator_destroy(itr); */

/* 	return SLURM_SUCCESS; */
	
/* } */

static List _get_user_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List user_list = NULL;
	acct_user_rec_t *user_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	user_list = list_create(destroy_acct_user_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		user_rec = xmalloc(sizeof(acct_user_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, "Name")) {
				user_rec->name = 
					xstrdup(name_val->value);
			} /* else if(!strcmp(name_val->name, "Expedite")) { */
/* 				if(user_rec->qos_list) */
/* 					continue; */
/* 				user_rec->qos_list =  */
/* 					list_create(slurm_destroy_char);  */
/* 				/\*really needs to have 1 added here */
/* 				  but we shouldn't ever need to use */
/* 				  this. */
/* 				*\/ */
/* 				slurm_addto_char_list(user_rec->qos_list, */
/* 						      name_val->value); */
/* 			}  */else if(!strcmp(name_val->name, "DefaultProject")) {
				user_rec->default_acct = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(user_list, user_rec);
	}
	list_iterator_destroy(itr);

	return user_list;
}

static List _get_acct_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List acct_list = NULL;
	acct_account_rec_t *acct_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	acct_list = list_create(destroy_acct_account_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		acct_rec = xmalloc(sizeof(acct_account_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			/* if(!strcmp(name_val->name, "Expedite")) { */
/* 				acct_rec->qos =  */
/* 					atoi(name_val->value)+1; */
/* 			} else */ if(!strcmp(name_val->name, 
					  "Name")) {
				acct_rec->name = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Organization")) {
				acct_rec->organization = 
					xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, 
					  "Description")) {
				acct_rec->description = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(acct_list, acct_rec);
	}
	list_iterator_destroy(itr);

	return acct_list;
}

static List _get_cluster_list_from_response(gold_response_t *gold_response)
{
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List cluster_list = NULL;
	acct_cluster_rec_t *cluster_rec = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	
	cluster_list = list_create(destroy_acct_cluster_rec);
	
	itr = list_iterator_create(gold_response->entries);
	while((resp_entry = list_next(itr))) {
		cluster_rec = xmalloc(sizeof(acct_cluster_rec_t));

		itr2 = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr2))) {
			if(!strcmp(name_val->name, 
					  "Name")) {
				cluster_rec->name = 
					xstrdup(name_val->value);
			} else {
				error("Unknown name val of '%s' = '%s'",
				      name_val->name, name_val->value);
			}
		}
		list_iterator_destroy(itr2);
		list_append(cluster_list, cluster_rec);
	}
	list_iterator_destroy(itr);

	return cluster_list;
}

static int _remove_association_accounting(List id_list)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	int rc = SLURM_SUCCESS;
	char *object = NULL;
	int set = 0;
	ListIterator itr = NULL;

	gold_request = create_gold_request(GOLD_OBJECT_ACCT_HOUR_USAGE,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("couldn't create gold_request");
		rc = SLURM_ERROR;
		return rc;
	}
	
	if(id_list && list_count(id_list)) {
		itr = list_iterator_create(id_list);
		if(list_count(id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Acct",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
			       
	gold_response = get_gold_response(gold_request);	

	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		destroy_gold_request(gold_request);
		rc = SLURM_ERROR;
		return rc;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		rc = SLURM_ERROR;
		return rc;
	}

	destroy_gold_response(gold_response);

	gold_request->object = GOLD_OBJECT_ACCT_DAY_USAGE;	
	gold_response = get_gold_response(gold_request);	
	
	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		destroy_gold_request(gold_request);
		rc = SLURM_ERROR;
		return rc;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		rc = SLURM_ERROR;
		return rc;
	}
	destroy_gold_response(gold_response);
	
	gold_request->object = GOLD_OBJECT_ACCT_MONTH_USAGE;	
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
		
	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		destroy_gold_request(gold_request);
		rc = SLURM_ERROR;
		return rc;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		rc = SLURM_ERROR;
	}

	destroy_gold_response(gold_response);		


	return rc; 
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	char *keyfile = NULL;
	char *host = NULL;
	uint32_t port = 0;
	struct	stat statbuf;

	if(!(keyfile = slurm_get_accounting_storage_pass()) 
	   || strlen(keyfile) < 1) {
		keyfile = xstrdup("/etc/gold/auth_key");
		debug2("No keyfile specified with AcctStoragePass, "
		       "gold using default %s", keyfile);
	}
	

	if(stat(keyfile, &statbuf)) {
		fatal("Can't stat key file %s. "
		      "To run acct_storage/gold you have to set "
		      "your gold keyfile as "
		      "AcctStoragePass in your slurm.conf", keyfile);
	}


	if(!(host = slurm_get_accounting_storage_host())) {
		host = xstrdup("localhost");
		debug2("No host specified with AcctStorageHost, "
		       "gold using default %s", host);
	}

	if(!(port = slurm_get_accounting_storage_port())) {
		port = 7112;
		debug2("No port specified with AcctStoragePort, "
		       "gold using default %u", port);
	}

	debug2("connecting to gold with keyfile='%s' for %s(%d)",
	       keyfile, host, port);

	init_gold(keyfile, host, port);

	xfree(keyfile);
	xfree(host);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	if(local_association_list)
		list_destroy(local_association_list);
	fini_gold();
	return SLURM_SUCCESS;
}

extern void * acct_storage_p_get_connection(bool make_agent, bool rollback)
{
	return NULL;
}

extern int acct_storage_p_close_connection(void **db_conn)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(void *db_conn, bool commit)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(void *db_conn,
				    List user_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_user_rec_t *object = NULL;
//	char tmp_buff[50];

	itr = list_iterator_create(user_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->default_acct) {
			error("We need a user name and "
			      "default acct to add.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_USER,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		gold_request_add_assignment(gold_request, "Name",
					    object->name);		
		gold_request_add_assignment(gold_request, "DefaultProject",
					    object->default_acct);		

/* 		if(object->qos != ACCT_QOS_NOTSET) { */
/* 			snprintf(tmp_buff, sizeof(tmp_buff), "%u", */
/* 				 object->qos-1); */
/* 			gold_request_add_assignment(gold_request, "Expedite", */
/* 						    tmp_buff); */
/* 		}		 */
		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_users: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			errno = gold_response->rc;
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_add_coord(void *db_conn,
				    char *acct,
				    acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(void *db_conn,
				    List acct_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_account_rec_t *object = NULL;
//	char tmp_buff[50];

	itr = list_iterator_create(acct_list);
	while((object = list_next(itr))) {
		if(!object->name || !object->description
		   || !object->organization) {
			error("We need a acct name, description, and "
			      "organization to add one.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		gold_request_add_assignment(gold_request, "Name",
					    object->name);		
		gold_request_add_assignment(gold_request, "Description",
					    object->description);		
		gold_request_add_assignment(gold_request, "Organization",
					    object->organization);		
/* 		if(object->qos != ACCT_QOS_NOTSET) { */
/* 			snprintf(tmp_buff, sizeof(tmp_buff), "%u", */
/* 				 object->qos-1); */
/* 			gold_request_add_assignment(gold_request, "Expedite", */
/* 						    tmp_buff); */
/* 		}		 */
		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_accts: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			errno = gold_response->rc;
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_add_clusters(void *db_conn,
				       List cluster_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_cluster_rec_t *object = NULL;

	itr = list_iterator_create(cluster_list);
	while((object = list_next(itr))) {
		if(!object->name) {
			error("We need a cluster name to add.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		gold_request_add_assignment(gold_request, "Name",
					    object->name);		

		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_clusters: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			errno = gold_response->rc;
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_add_associations(void *db_conn,
					   List association_list)
{
	ListIterator itr = NULL;
	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	acct_association_rec_t *object = NULL;
	char tmp_buff[50];

	itr = list_iterator_create(association_list);
	while((object = list_next(itr))) {
		if(!object->cluster || !object->acct) {
			error("We need a association cluster and "
			      "acct to add one.");
			rc = SLURM_ERROR;
			continue;
		}
		gold_request = create_gold_request(GOLD_OBJECT_ACCT,
						   GOLD_ACTION_CREATE);
		if(!gold_request) { 
			error("couldn't create gold_request");
			rc = SLURM_ERROR;
			break;
		}
		if(object->user) {
			gold_request_add_assignment(gold_request, "User",
						    object->user);		
			snprintf(tmp_buff, sizeof(tmp_buff), 
				 "%s on %s for %s",
				 object->acct,
				 object->cluster,
				 object->user);
		} else if(object->parent_acct)
			snprintf(tmp_buff, sizeof(tmp_buff), 
				 "%s of %s on %s",
				 object->acct,
				 object->parent_acct,
				 object->cluster);
		else
			snprintf(tmp_buff, sizeof(tmp_buff), 
				 "%s on %s",
				 object->acct,
				 object->cluster);
			
		gold_request_add_assignment(gold_request, "Name", tmp_buff);

		gold_request_add_assignment(gold_request, "Project",
					    object->acct);		
		gold_request_add_assignment(gold_request, "Machine",
					    object->cluster);	
			
		if(object->fairshare) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->fairshare);
			gold_request_add_assignment(gold_request, "FairShare",
						    tmp_buff);		
		}

		if(object->max_jobs) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u", 
				 object->max_jobs);
			gold_request_add_assignment(gold_request, "MaxJobs",
						    tmp_buff);
		}
		
		if(object->max_nodes_per_job) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->max_nodes_per_job);
			gold_request_add_assignment(gold_request,
						    "MaxNodesPerJob",
						    tmp_buff);
		}

		if(object->max_wall_duration_per_job) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->max_wall_duration_per_job);
			gold_request_add_assignment(gold_request,
						    "MaxWallDurationPerJob",
						    tmp_buff);		
		}

		if(object->max_cpu_secs_per_job) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u",
				 object->max_cpu_secs_per_job);
			gold_request_add_assignment(gold_request,
						    "MaxProcSecondsPerJob",
						    tmp_buff);		
		}

		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);

		if(!gold_response) {
			error("acct_storage_p_add_associations: "
			      "no response received");
			rc = SLURM_ERROR;
			break;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			errno = gold_response->rc;
			destroy_gold_response(gold_response);
			rc = SLURM_ERROR;
			break;
		}
		destroy_gold_response(gold_response);		
	}
	list_iterator_destroy(itr);
	
	return rc;
}

extern int acct_storage_p_add_qos(void *db_conn, uint32_t uid, 
				  List qos_list)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_users(void *db_conn,
					acct_user_cond_t *user_q,
					acct_user_rec_t *user)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
//	char tmp_buff[50];
	int set = 0;

	if(!user_q) {
		error("acct_storage_p_modify_users: "
		      "we need conditions to modify");
		return NULL;
	}

	if(!user) {
		error("acct_storage_p_modify_users: "
		      "we need something to change");
		return NULL;
	}

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("acct_storage_p_modify_users: "
		      "couldn't create gold_request");
		return NULL;
	}

	if(user_q->assoc_cond->user_list
	   && list_count(user_q->assoc_cond->user_list)) {
		itr = list_iterator_create(user_q->assoc_cond->user_list);
		if(list_count(user_q->assoc_cond->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user->default_acct) 
		gold_request_add_assignment(gold_request,
					    "DefaultProject",
					    user->default_acct);
	
/* 	if(user->qos != ACCT_QOS_NOTSET) { */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%u", */
/* 			 user->qos-1); */
/* 		gold_request_add_assignment(gold_request, "Expedite", */
/* 					    tmp_buff);		 */
/* 	} */

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_modify_users: "
		      "no response received");
		return NULL;
	}
	
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
	}

	destroy_gold_response(gold_response);		
	
	return NULL;
}

extern List acct_storage_p_modify_user_admin_level(void *db_conn,
						   acct_user_cond_t *user_q)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!user_q || user_q->admin_level == ACCT_ADMIN_NOTSET) {
		error("acct_storage_p_modify_users: "
		      "we need conditions to modify");
		return NULL;
	}

	if(user_q->admin_level == ACCT_ADMIN_NONE) 
		gold_request = create_gold_request(GOLD_OBJECT_ROLEUSER,
						   GOLD_ACTION_DELETE);
	else 
		gold_request = create_gold_request(GOLD_OBJECT_ROLEUSER,
						   GOLD_ACTION_CREATE);
	
	if(!gold_request) { 
		error("couldn't create gold_request");
		return NULL;
	}

	if(user_q->admin_level == ACCT_ADMIN_NONE) {
		gold_request_add_condition(gold_request,
					   "Role",
					   "SystemAdmin",
					   GOLD_OPERATOR_NONE, 2);
		
		gold_request_add_condition(gold_request,
					   "Role",
					   "Operator",
					   GOLD_OPERATOR_NONE, 1);
	} else if(user_q->admin_level == ACCT_ADMIN_SUPER_USER)
		gold_request_add_assignment(gold_request,
					    "Role",
					    "SystemAdmin");
	else if(user_q->admin_level == ACCT_ADMIN_OPERATOR)
		gold_request_add_assignment(gold_request,
					    "Role",
					    "Operator");
	else {
		error("acct_storage_p_modify_user_admin_level: "
		      "unknown admin level %d", user_q->admin_level);
		return NULL;
	}

	if(user_q->assoc_cond->user_list
	   && list_count(user_q->assoc_cond->user_list)) {
		itr = list_iterator_create(user_q->assoc_cond->user_list);
		if(list_count(user_q->assoc_cond->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_modify_users: "
		      "no response received");
		return NULL;
	}
	
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		
	}
	destroy_gold_response(gold_response);	
	
	return NULL;
}

extern List acct_storage_p_modify_accts(void *db_conn,
				       acct_account_cond_t *acct_q,
				       acct_account_rec_t *acct)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
//	char tmp_buff[50];
	int set = 0;
	char *object = NULL;

	if(!acct_q) {
		error("acct_storage_p_modify_accts: "
		      "we need conditions to modify");
		return NULL;
	}

	if(!acct) {
		error("acct_storage_p_modify_accts: "
		      "we need something to change");
		return NULL;
	}
	
	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("couldn't create gold_request");
		return NULL;
	}

	if(acct_q->assoc_cond->acct_list
	   && list_count(acct_q->assoc_cond->acct_list)) {
		itr = list_iterator_create(acct_q->assoc_cond->acct_list);
		if(list_count(acct_q->assoc_cond->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->description_list 
	   && list_count(acct_q->description_list)) {
		itr = list_iterator_create(acct_q->description_list);
		if(list_count(acct_q->description_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Description",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->organization_list 
	   && list_count(acct_q->organization_list)) {
		itr = list_iterator_create(acct_q->organization_list);
		if(list_count(acct_q->organization_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Organization",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct->description) 
		gold_request_add_assignment(gold_request,
					    "Description",
					    acct->description);
	if(acct->organization) 
		gold_request_add_assignment(gold_request,
					    "Organization",
					    acct->organization);
	
/* 	if(acct->qos != ACCT_QOS_NOTSET) { */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%u", */
/* 			 acct->qos-1); */
/* 		gold_request_add_assignment(gold_request, "Expedite", */
/* 					    tmp_buff);		 */
/* 	} */
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_modify_accts: "
		      "no response received");
		return NULL;
	}
	
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		
	}

	destroy_gold_response(gold_response);		
	
	return NULL;
}

extern List acct_storage_p_modify_clusters(void *db_conn,
					  acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_associations(void *db_conn,
					      acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	char *object = NULL;
	int set = 0;

	if(!assoc_q) {
		error("acct_storage_p_modify_associations: "
		      "we need conditions to modify");
		return NULL;
	}

	if(!assoc) {
		error("acct_storage_p_modify_associations: "
		      "we need something to change");
		return NULL;
	}

	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) { 
		error("couldn't create gold_request");
		return NULL;
	}

	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		itr = list_iterator_create(assoc_q->id_list);
		if(list_count(assoc_q->id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Id",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		itr = list_iterator_create(assoc_q->user_list);
		if(list_count(assoc_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "User",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		itr = list_iterator_create(assoc_q->acct_list);
		if(list_count(assoc_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Project",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		itr = list_iterator_create(assoc_q->cluster_list);
		if(list_count(assoc_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc->fairshare) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->fairshare);
		gold_request_add_assignment(gold_request, "Fairshare",
					    tmp_buff);		
	}

	if(assoc->max_jobs) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u", 
			 assoc->max_jobs);
		gold_request_add_assignment(gold_request, "MaxJobs",
					    tmp_buff);
	}
		
	if(assoc->max_nodes_per_job) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->max_nodes_per_job);
		gold_request_add_assignment(gold_request,
					    "MaxNodesPerJob",
					    tmp_buff);
	}

	if(assoc->max_wall_duration_per_job) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->max_wall_duration_per_job);
		gold_request_add_assignment(gold_request,
					    "MaxWallDurationPerJob",
					    tmp_buff);		
	}

	if(assoc->max_cpu_secs_per_job) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 assoc->max_cpu_secs_per_job);
		gold_request_add_assignment(gold_request,
					    "MaxProcSecondsPerJob",
					    tmp_buff);		
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		return NULL;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		
	}
	destroy_gold_response(gold_response);		
	
	return NULL;
}

extern List acct_storage_p_remove_users(void *db_conn,
				       acct_user_cond_t *user_q)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!user_q) {
		error("acct_storage_p_remove_users: "
		      "we need conditions to remove");
		return NULL;
	}

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_users: "
		      "couldn't create gold_request");
		return NULL;
	}
	
	if(user_q->assoc_cond->user_list 
	   && list_count(user_q->assoc_cond->user_list)) {
		itr = list_iterator_create(user_q->assoc_cond->user_list);
		if(list_count(user_q->assoc_cond->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
	
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_remove_users: "
		      "no response received");
		return NULL;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_users: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		
	}
	destroy_gold_response(gold_response);		
		
	return NULL;
}

extern List acct_storage_p_remove_coord(void *db_conn,
				       char *acct,
				       acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_accts(void *db_conn,
				       acct_account_cond_t *acct_q)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!acct_q) {
		error("acct_storage_p_remove_accts: "
		      "we need conditions to remove");
		return NULL;
	}

	gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_accts: "
		      "couldn't create gold_request");
		return NULL;
	}
	
	if(acct_q->assoc_cond->acct_list
	   && list_count(acct_q->assoc_cond->acct_list)) {
		itr = list_iterator_create(acct_q->assoc_cond->acct_list);
		if(list_count(acct_q->assoc_cond->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->description_list 
	   && list_count(acct_q->description_list)) {
		itr = list_iterator_create(acct_q->description_list);
		if(list_count(acct_q->description_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Description",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->organization_list 
	   && list_count(acct_q->organization_list)) {
		itr = list_iterator_create(acct_q->organization_list);
		if(list_count(acct_q->organization_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Organization",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_remove_accts: "
		      "no response received");
		return NULL;
	}
	
	if(gold_response->rc) {
		error("acct_storage_p_remove_accts: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		
	}
	destroy_gold_response(gold_response);		
		
	return NULL;
}

extern List acct_storage_p_remove_clusters(void *db_conn,
					  acct_cluster_cond_t *cluster_q)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!cluster_q) {
		error("acct_storage_p_modify_clusters: "
		      "we need conditions to modify");
		return NULL;
	}

	gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_clusters: "
		      "couldn't create gold_request");
		return NULL;
	}
	
	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		itr = list_iterator_create(cluster_q->cluster_list);
		if(list_count(cluster_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}


	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
		
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		return NULL;
	}
	
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		destroy_gold_response(gold_response);
		return NULL;
	}
	destroy_gold_response(gold_response);

	gold_request = create_gold_request(GOLD_OBJECT_MACHINE_HOUR_USAGE,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("acct_storage_p_remove_clusters: "
		      "couldn't create gold_request");
		return NULL;
	}
	
	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		itr = list_iterator_create(cluster_q->cluster_list);
		if(list_count(cluster_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return NULL;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		return NULL;
	}
	destroy_gold_response(gold_response);

	gold_request->object = GOLD_OBJECT_MACHINE_DAY_USAGE;
	gold_response = get_gold_response(gold_request);	
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return NULL;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		destroy_gold_request(gold_request);
		destroy_gold_response(gold_response);
		return NULL;
	}
	
	destroy_gold_response(gold_response);

	gold_request->object = GOLD_OBJECT_MACHINE_MONTH_USAGE;
	gold_response = get_gold_response(gold_request);	
	if(!gold_response) {
		error("acct_storage_p_remove_clusters: "
		      "no response received");
		destroy_gold_request(gold_request);
		return NULL;
	}
		
	if(gold_response->rc) {
		error("acct_storage_p_remove_clusters: "
		      "gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		
	}
	
	destroy_gold_request(gold_request);
	destroy_gold_response(gold_response);
	
	return NULL;
}

extern List acct_storage_p_remove_associations(void *db_conn,
					      acct_association_cond_t *assoc_q)
{
	ListIterator itr = NULL;
//	int rc = SLURM_SUCCESS;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *object = NULL;
	int set = 0;

	if(!assoc_q) {
		error("acct_storage_p_remove_associations: "
		      "we need conditions to remove");
		return NULL;
	}

	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_DELETE);
	if(!gold_request) { 
		error("couldn't create gold_request");
		return NULL;
	}

	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		itr = list_iterator_create(assoc_q->id_list);
		if(list_count(assoc_q->id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Id",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		itr = list_iterator_create(assoc_q->user_list);
		if(list_count(assoc_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "User",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		itr = list_iterator_create(assoc_q->acct_list);
		if(list_count(assoc_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Project",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		itr = list_iterator_create(assoc_q->cluster_list);
		if(list_count(assoc_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);
	
	if(!gold_response) {
		error("acct_storage_p_modify_associations: "
		      "no response received");
		return NULL;
	}
		
	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		
	}

	if(gold_response->entry_cnt > 0) {
		ListIterator itr = NULL;
		ListIterator itr2 = NULL;
		gold_response_entry_t *resp_entry = NULL;
		gold_name_value_t *name_val = NULL;
		List id_list = list_create(slurm_destroy_char);

		itr = list_iterator_create(gold_response->entries);
		while((resp_entry = list_next(itr))) {
			itr2 = list_iterator_create(
				resp_entry->name_val);
			while((name_val = list_next(itr2))) {
				if(!strcmp(name_val->name, "Id")) {
					list_push(id_list, name_val->value);
					break;
				}
			}
			list_iterator_destroy(itr2);			
		}
		list_iterator_destroy(itr);
		_remove_association_accounting(id_list);
		list_destroy(id_list);
	} else {
		debug3("no associations found");
	}
	destroy_gold_response(gold_response);		

	return NULL;
}

extern List acct_storage_p_remove_qos(void *db_conn, uint32_t uid, 
				      acct_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_get_users(void *db_conn, uid_t uid,
				     acct_user_cond_t *user_q)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List user_list = NULL;
	ListIterator itr = NULL;
	char *object = NULL;
	int set = 0;
//	char tmp_buff[50];

	gold_request = create_gold_request(GOLD_OBJECT_USER,
					   GOLD_ACTION_QUERY);

	if(!gold_request) 
		return NULL;

	if(!user_q) 
		goto empty;

	if(user_q->assoc_cond->user_list 
	   && list_count(user_q->assoc_cond->user_list)) {
		itr = list_iterator_create(user_q->assoc_cond->user_list);
		if(list_count(user_q->assoc_cond->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(user_q->def_acct_list && list_count(user_q->def_acct_list)) {
		itr = list_iterator_create(user_q->def_acct_list);
		if(list_count(user_q->def_acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request,
						   "DefaultProject",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}
	
/* 	if(user_q->qos != ACCT_QOS_NOTSET) { */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%u", */
/* 			 user_q->qos-1); */
/* 		gold_request_add_condition(gold_request, "Expedite", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_NONE, 0);		 */
/* 	} */

empty:
	gold_request_add_condition(gold_request, "Active",
				   "True",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_condition(gold_request, "Special",
				   "False",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_selection(gold_request, "Name");
	gold_request_add_selection(gold_request, "DefaultProject");
	gold_request_add_selection(gold_request, "Expedite");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_users: no response received");
		return NULL;
	}

	user_list = _get_user_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return user_list;
}

extern List acct_storage_p_get_accts(void *db_conn, uid_t uid,
				     acct_account_cond_t *acct_q)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List acct_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;
//	char tmp_buff[50];


	gold_request = create_gold_request(GOLD_OBJECT_PROJECT,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return NULL;

	if(!acct_q) 
		goto empty;

	if(acct_q->assoc_cond->acct_list 
	   && list_count(acct_q->assoc_cond->acct_list)) {
		itr = list_iterator_create(acct_q->assoc_cond->acct_list);
		if(list_count(acct_q->assoc_cond->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->description_list 
	   && list_count(acct_q->description_list)) {
		itr = list_iterator_create(acct_q->description_list);
		if(list_count(acct_q->description_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Description",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(acct_q->organization_list 
	   && list_count(acct_q->organization_list)) {
		itr = list_iterator_create(acct_q->organization_list);
		if(list_count(acct_q->organization_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Organization",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

/* 	if(acct_q->qos != ACCT_QOS_NOTSET) { */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%u", */
/* 			 acct_q->qos-1); */
/* 		gold_request_add_condition(gold_request, "Expedite", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_NONE, 0);		 */
/* 	} */
empty:
	gold_request_add_condition(gold_request, "Active",
				   "True",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_condition(gold_request, "Special",
				   "False",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_selection(gold_request, "Name");
	gold_request_add_selection(gold_request, "Organization");
	gold_request_add_selection(gold_request, "Description");
	gold_request_add_selection(gold_request, "Expedite");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_accts: no response received");
		return NULL;
	}

	acct_list = _get_acct_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return acct_list;
}

extern List acct_storage_p_get_clusters(void *db_conn, uid_t uid,
					acct_cluster_cond_t *cluster_q)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List cluster_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;


	gold_request = create_gold_request(GOLD_OBJECT_MACHINE,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return NULL;

	if(!cluster_q) 
		goto empty;

	if(cluster_q->cluster_list && list_count(cluster_q->cluster_list)) {
		itr = list_iterator_create(cluster_q->cluster_list);
		if(list_count(cluster_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Name",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

empty:
	gold_request_add_condition(gold_request, "Active",
				   "True",
				   GOLD_OPERATOR_NONE,
				   0);

	gold_request_add_condition(gold_request, "Special",
				   "False",
				   GOLD_OPERATOR_NONE,
				   0);
	
	gold_request_add_selection(gold_request, "Name");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_clusters: no response received");
		return NULL;
	}

	cluster_list = _get_cluster_list_from_response(gold_response);
	
	destroy_gold_response(gold_response);

	return cluster_list;
}

extern List acct_storage_p_get_associations(void *db_conn, uid_t uid,
					    acct_association_cond_t *assoc_q)
{

	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	List association_list = NULL;
	ListIterator itr = NULL;
	int set = 0;
	char *object = NULL;

	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_QUERY);
	
	if(!gold_request) 
		return NULL;

	if(!assoc_q) 
		goto empty;
	
	if(assoc_q->id_list && list_count(assoc_q->id_list)) {
		itr = list_iterator_create(assoc_q->id_list);
		if(list_count(assoc_q->id_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Id",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->user_list && list_count(assoc_q->user_list)) {
		itr = list_iterator_create(assoc_q->user_list);
		if(list_count(assoc_q->user_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "User",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->acct_list && list_count(assoc_q->acct_list)) {
		itr = list_iterator_create(assoc_q->acct_list);
		if(list_count(assoc_q->acct_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Project",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(assoc_q->cluster_list && list_count(assoc_q->cluster_list)) {
		itr = list_iterator_create(assoc_q->cluster_list);
		if(list_count(assoc_q->cluster_list) > 1)
			set = 2;
		else
			set = 0;
		
		while((object = list_next(itr))) {
			gold_request_add_condition(gold_request, "Machine",
						   object,
						   GOLD_OPERATOR_NONE, set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

empty:
	gold_request_add_selection(gold_request, "Id");
	gold_request_add_selection(gold_request, "User");
	gold_request_add_selection(gold_request, "Project");
	gold_request_add_selection(gold_request, "Machine");
	gold_request_add_selection(gold_request, "Parent");
	gold_request_add_selection(gold_request, "FairShare");
	gold_request_add_selection(gold_request, "MaxJobs");
	gold_request_add_selection(gold_request, "MaxNodesPerJob");
	gold_request_add_selection(gold_request, "MaxWallDurationPerJob");
	gold_request_add_selection(gold_request, "MaxProcSecondsPerJob");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("acct_storage_p_get_associations: "
		      "no response received");
		return NULL;
	}

	association_list = _get_association_list_from_response(gold_response);

	destroy_gold_response(gold_response);

	return association_list;
}

extern List acct_storage_p_get_qos(void *db_conn, uid_t uid,
				   acct_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_get_txn(void *db_conn, uid_t uid,
				   acct_txn_cond_t *txn_cond)
{
	return NULL;
}

extern int acct_storage_p_get_usage(void *db_conn, uid_t uid,
				    acct_association_rec_t *acct_assoc,
				    time_t start, time_t end)
{
	int rc = SLURM_ERROR;
/* 	gold_request_t *gold_request = NULL; */
/* 	gold_response_t *gold_response = NULL; */
/* 	char tmp_buff[50]; */
/* 	gold_object_t g_object; */
/* 	char *req_cpu_type = NULL; */

/* 	if(!acct_assoc || acct_assoc->id) { */
/* 		error("acct_storage_p_get_usage: " */
/* 		      "We need an id to go off to query off of"); */
/* 		return rc; */
/* 	} */

/* 	switch(type) { */
/* 	case ACCT_USAGE_HOUR: */
/* 		g_object = GOLD_OBJECT_ACCT_HOUR_USAGE; */
/* 		req_cpu_type = "AllocatedCPUSeconds"; */
/* 		break; */
/* 	case ACCT_USAGE_DAY: */
/* 		g_object = GOLD_OBJECT_ACCT_DAY_USAGE; */
/* 		req_cpu_type = "AllocatedCPUSeconds"; */
/* 		break; */
/* 	case ACCT_USAGE_MONTH: */
/* 		g_object = GOLD_OBJECT_ACCT_MONTH_USAGE; */
/* 		req_cpu_type = "AllocatedCPUHours"; */
/* 		break; */
/* 	default: */
/* 		error("Unknown usage type"); */
/* 		return rc; */
/* 	} */
/* 	gold_request = create_gold_request( */
/* 		g_object, GOLD_ACTION_QUERY); */

/* 	if(!gold_request)  */
/* 		return rc; */

/* 	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id); */
/* 	gold_request_add_condition(gold_request, "Acct", tmp_buff, */
/* 				   GOLD_OPERATOR_NONE, 0); */

/* 	if(start) { */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start); */
/* 		gold_request_add_condition(gold_request, "PeriodStart", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_GE, 0); */
/* 	} */
/* 	if(end) {	 */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end); */
/* 		gold_request_add_condition(gold_request, "PeriodStart", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_L, 0); */
/* 	} */

/* 	gold_request_add_selection(gold_request, "PeriodStart"); */
/* 	gold_request_add_selection(gold_request, req_cpu_type); */

/* 	gold_response = get_gold_response(gold_request);	 */
/* 	destroy_gold_request(gold_request); */

/* 	if(!gold_response) { */
/* 		error("acct_storage_p_get_usage: " */
/* 		      "no response received"); */
/* 		return rc; */
/* 	} */

/* 	rc = _get_acct_accounting_list_from_response( */
/* 		gold_response, acct_assoc); */

/* 	destroy_gold_response(gold_response); */

	return rc;
}

extern int acct_storage_p_roll_usage(void *db_conn, 
				     time_t sent_start)
{
	int rc = SLURM_ERROR;
	/* FIX ME: This doesn't do anything now */
/* 	gold_request_t *gold_request = NULL; */
/* 	gold_response_t *gold_response = NULL; */
/* 	char tmp_buff[50]; */

/* 	if(!acct_assoc || acct_assoc->id) { */
/* 		error("acct_storage_p_roll_usage: " */
/* 		      "We need an id to go off to query off of"); */
/* 		return rc; */
/* 	} */

/* 	switch(type) { */
/* 	case ACCT_USAGE_HOUR: */
/* 		g_object = GOLD_OBJECT_ACCT_HOUR_USAGE; */
/* 		req_cpu_type = "AllocatedCPUSecs"; */
/* 		break; */
/* 	case ACCT_USAGE_DAY: */
/* 		g_object = GOLD_OBJECT_ACCT_DAY_USAGE; */
/* 		req_cpu_type = "AllocatedCPUSecs"; */
/* 		break; */
/* 	case ACCT_USAGE_MONTH: */
/* 		g_object = GOLD_OBJECT_ACCT_MONTH_USAGE; */
/* 		req_cpu_type = "AllocatedCPUHours"; */
/* 		break; */
/* 	default: */
/* 		error("Unknown usage type"); */
/* 		return rc; */
/* 	} */
/* 	gold_request = create_gold_request( */
/* 		GOLD_OBJECT_ACCT_DAY_USAGE, GOLD_ACTION_QUERY); */

/* 	if(!gold_request)  */
/* 		return rc; */

/* 	snprintf(tmp_buff, sizeof(tmp_buff), "%u", acct_assoc->id); */
/* 	gold_request_add_condition(gold_request, "Acct", tmp_buff, */
/* 				   GOLD_OPERATOR_NONE, 0); */

/* 	if(start) { */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start); */
/* 		gold_request_add_condition(gold_request, "PeriodStart", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_GE, 0); */
/* 	} */
/* 	if(end) {	 */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end); */
/* 		gold_request_add_condition(gold_request, "PeriodStart", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_L, 0); */
/* 	} */

/* 	gold_request_add_selection(gold_request, "PeriodStart"); */
/* 	gold_request_add_selection(gold_request, "AllocatedCPUSecs"); */

/* 	gold_response = get_gold_response(gold_request);	 */
/* 	destroy_gold_request(gold_request); */

/* 	if(!gold_response) { */
/* 		error("acct_storage_p_get_daily_usage: " */
/* 		      "no response received"); */
/* 		return rc; */
/* 	} */

/* 	rc = _get_acct_accounting_list_from_response( */
/* 		gold_response, acct_assoc); */

/* 	destroy_gold_response(gold_response); */

	return rc;
}

extern int clusteracct_storage_p_node_down(void *db_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time,
					   char *reason)
{
	uint16_t cpus;
	int rc = SLURM_ERROR;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	char *my_reason;

	if (slurmctld_conf.fast_schedule && !slurmdbd_conf)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

	if (reason)
		my_reason = reason;
	else
		my_reason = node_ptr->reason;

#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("cluster_acct_down: %s at %s with %u cpus due to %s", 
	     node_ptr->name, tmp_buff, cpus, reason);
#endif
	/* If the node was already down end that record since the
	 * reason will most likely be different
	 */

	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster,
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE, 0);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_storage_p_node_down: no response received");
		return rc;
	}

	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		destroy_gold_response(gold_response);
		return rc;
	}
	destroy_gold_response(gold_response);

	/* now add the new one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_CREATE);
	if(!gold_request) 
		return rc;
	
	gold_request_add_assignment(gold_request, "Machine", cluster);
	snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)event_time);
	gold_request_add_assignment(gold_request, "StartTime", tmp_buff);
	gold_request_add_assignment(gold_request, "Name", node_ptr->name);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u", cpus);
	gold_request_add_assignment(gold_request, "CPUCount", tmp_buff);
	gold_request_add_assignment(gold_request, "Reason", my_reason);
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_node_down: no response received");
		return rc;
	}

	if(!gold_response->rc) 
		rc = SLURM_SUCCESS;
	else {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
	}
	destroy_gold_response(gold_response);

	return rc;
}

extern int clusteracct_storage_p_node_up(void *db_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	int rc = SLURM_ERROR;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];

#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("cluster_acct_up: %s at %s", node_ptr->name, tmp_buff);
#endif

	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster,
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE, 0);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_node_up: no response received");
		return rc;
	}

	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
		destroy_gold_response(gold_response);
		return rc;
	}
	rc = SLURM_SUCCESS;
	destroy_gold_response(gold_response);


	return rc;
}

extern int clusteracct_storage_p_register_ctld(char *cluster,
					       uint16_t port)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_procs(void *db_conn,
					       char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
	static uint32_t last_procs = -1;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;
	bool no_modify = 0;

	if (procs == last_procs) {
		debug3("we have the same procs as before no need to "
		       "query the database.");
		return SLURM_SUCCESS;
	}
	last_procs = procs;

	/* Record the processor count */
#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("cluster_acct_procs: %s has %u total CPUs at %s", 
	     cluster, procs, tmp_buff);
#endif
	
	/* get the last known one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return rc;
	gold_request_add_condition(gold_request, "Machine", cluster,
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "Name", "NULL",
				   GOLD_OPERATOR_NONE, 0);

	gold_request_add_selection(gold_request, "CPUCount");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_cluster_procs: no response received");
		return rc;
	}

	if(gold_response->entry_cnt > 0) {
		gold_response_entry_t *resp_entry = 
			list_pop(gold_response->entries);
		gold_name_value_t *name_val = list_pop(resp_entry->name_val);

		if(procs == atoi(name_val->value)) {
			debug("System hasn't changed since last entry");
			destroy_gold_name_value(name_val);
			destroy_gold_response_entry(resp_entry);
			destroy_gold_response(gold_response);
			return SLURM_SUCCESS;
		} else {
			debug("System has changed from %s cpus to %d",
			      name_val->value, procs);   
		}

		destroy_gold_name_value(name_val);
		destroy_gold_response_entry(resp_entry);
	} else {
		debug("We don't have an entry for this machine "
		      "most likely a first time running.");
		no_modify = 1;
	}

	destroy_gold_response(gold_response);
	
	if(no_modify) {
		gold_request = create_gold_request(GOLD_OBJECT_EVENT,
						   GOLD_ACTION_MODIFY);
		if(!gold_request) 
			return rc;
		
		gold_request_add_condition(gold_request, "Machine",
					   cluster,
					   GOLD_OPERATOR_NONE, 0);
		gold_request_add_condition(gold_request, "EndTime", "0",
					   GOLD_OPERATOR_NONE, 0);
		gold_request_add_condition(gold_request, "Name", "NULL",
					   GOLD_OPERATOR_NONE, 0);
		
		snprintf(tmp_buff, sizeof(tmp_buff), "%d", 
			 ((int)event_time - 1));
		gold_request_add_assignment(gold_request, "EndTime", tmp_buff);	
		
		gold_response = get_gold_response(gold_request);	
		destroy_gold_request(gold_request);
		
		if(!gold_response) {
			error("jobacct_p_cluster_procs: no response received");
			return rc;
		}
		
		if(gold_response->rc) {
			error("gold_response has non-zero rc(%d): %s",
			      gold_response->rc,
			      gold_response->message);
			errno = gold_response->rc;
			destroy_gold_response(gold_response);
			return rc;
		}
		destroy_gold_response(gold_response);
	}

	/* now add the new one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_CREATE);
	if(!gold_request) 
		return rc;
	
	gold_request_add_assignment(gold_request, "Machine", cluster);
	snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)event_time);
	gold_request_add_assignment(gold_request, "StartTime", tmp_buff);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u", procs);
	gold_request_add_assignment(gold_request, "CPUCount", tmp_buff);
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("clusteracct_p_cluster_procs: no response received");
		return rc;
	}

	if(!gold_response->rc) 
		rc = SLURM_SUCCESS;
	else {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		errno = gold_response->rc;
	}
	destroy_gold_response(gold_response);

	return rc;
}

extern int clusteracct_storage_p_get_usage(
	void *db_conn, uid_t uid, 
	acct_cluster_rec_t *cluster_rec, time_t start, 
	time_t end)
{
	int rc = SLURM_ERROR;
/* 	gold_request_t *gold_request = NULL; */
/* 	gold_response_t *gold_response = NULL; */
/* 	char tmp_buff[50]; */
/* 	gold_object_t g_object; */
/* 	char *alloc_cpu = NULL; */
/* 	char *idle_cpu = NULL; */
/* 	char *down_cpu = NULL; */
/* 	char *resv_cpu = NULL; */

/* 	if(!cluster_rec || !cluster_rec->name) { */
/* 		error("clusteracct_storage_p_get_hourly_usage:" */
/* 		      "no cluster name given to query."); */
/* 		return rc; */
/* 	} */
/* 	switch(type) { */
/* 	case ACCT_USAGE_HOUR: */
/* 		g_object = GOLD_OBJECT_MACHINE_HOUR_USAGE; */
/* 		alloc_cpu = "AllocatedCPUSeconds"; */
/* 		idle_cpu = "IdleCPUSeconds"; */
/* 		down_cpu = "DownCPUSeconds"; */
/* 		resv_cpu = "ReservedCPUSeconds"; */
/* 		break; */
/* 	case ACCT_USAGE_DAY: */
/* 		g_object = GOLD_OBJECT_MACHINE_DAY_USAGE; */
/* 		alloc_cpu = "AllocatedCPUSeconds"; */
/* 		idle_cpu = "IdleCPUSeconds"; */
/* 		down_cpu = "DownCPUSeconds"; */
/* 		resv_cpu = "ReservedCPUSeconds"; */
/* 		break; */
/* 	case ACCT_USAGE_MONTH: */
/* 		g_object = GOLD_OBJECT_MACHINE_MONTH_USAGE; */
/* 		alloc_cpu = "AllocatedCPUHours"; */
/* 		idle_cpu = "IdleCPUHours"; */
/* 		down_cpu = "DownCPUHours"; */
/* 		resv_cpu = "ReservedCPUHours"; */
/* 		break; */
/* 	default: */
/* 		error("Unknown usage type"); */
/* 		return rc; */
/* 	} */
/* 	/\* get the last known one *\/ */
/* 	gold_request = create_gold_request(GOLD_OBJECT_MACHINE_HOUR_USAGE, */
/* 					   GOLD_ACTION_QUERY); */
/* 	if(!gold_request)  */
/* 		return rc; */

/* 	gold_request_add_condition(gold_request, "Machine", cluster_rec->name, */
/* 				   GOLD_OPERATOR_NONE, 0); */
/* 	if(start) { */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)start); */
/* 		gold_request_add_condition(gold_request, "PeriodStart", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_GE, 0); */
/* 	} */
/* 	if(end) {	 */
/* 		snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)end); */
/* 		gold_request_add_condition(gold_request, "PeriodStart", */
/* 					   tmp_buff, */
/* 					   GOLD_OPERATOR_L, 0); */
/* 	} */

/* 	gold_request_add_selection(gold_request, "CPUCount"); */
/* 	gold_request_add_selection(gold_request, "PeriodStart"); */
/* 	gold_request_add_selection(gold_request, idle_cpu); */
/* 	gold_request_add_selection(gold_request, down_cpu); */
/* 	gold_request_add_selection(gold_request, alloc_cpu); */
/* 	gold_request_add_selection(gold_request, resv_cpu); */
		
/* 	gold_response = get_gold_response(gold_request);	 */
/* 	destroy_gold_request(gold_request); */

/* 	if(!gold_response) { */
/* 		error("clusteracct_p_get_hourly_usage: no response received"); */
/* 		return rc; */
/* 	} */

/* 	if(gold_response->entry_cnt > 0) { */
/* 		rc = _get_cluster_accounting_list_from_response( */
/* 			gold_response, cluster_rec); */
/* 	} else { */
/* 		debug("We don't have an entry for this machine for this time"); */
/* 	} */
/* 	destroy_gold_response(gold_response); */

	return rc;
}

extern int jobacct_storage_p_job_start(void *db_conn,
				       struct job_record *job_ptr)
{
	gold_object_t action = GOLD_ACTION_CREATE;
	
	if(_check_for_job(job_ptr->job_id, job_ptr->details->submit_time)) {
		debug3("It looks like this job is already in GOLD.");
		action = GOLD_ACTION_MODIFY;
	}

	return _add_edit_job(job_ptr, action);
}

extern int jobacct_storage_p_job_complete(void *db_conn,
					  struct job_record *job_ptr) 
{
	gold_object_t action = GOLD_ACTION_MODIFY;
	
	if(!_check_for_job(job_ptr->job_id, job_ptr->details->submit_time)) {
		error("Couldn't find this job entry.  "
		      "This shouldn't happen, we are going to create one.");
		action = GOLD_ACTION_CREATE;
	}

	return _add_edit_job(job_ptr, action);
}

extern int jobacct_storage_p_step_start(void *db_conn,
					struct step_record *step)
{
	gold_object_t action = GOLD_ACTION_MODIFY;
	
	if(!_check_for_job(step->job_ptr->job_id,
			   step->job_ptr->details->submit_time)) {
		error("Couldn't find this job entry.  "
		      "This shouldn't happen, we are going to create one.");
		action = GOLD_ACTION_CREATE;
	}

	return _add_edit_job(step->job_ptr, action);

}

extern int jobacct_storage_p_step_complete(void *db_conn,
					   struct step_record *step)
{
	return SLURM_SUCCESS;	
}

extern int jobacct_storage_p_suspend(void *db_conn,
				     struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/* 
 * get info from the storage 
 * returns List of jobacct_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs(void *db_conn, uid_t uid,
				       List selected_steps,
				       List selected_parts,
				       sacct_parameters_t *params)
{
	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_JOB,
							   GOLD_ACTION_QUERY);
	gold_response_t *gold_response = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	char tmp_buff[50];
	int set = 0;
	char *selected_part = NULL;
	jobacct_selected_step_t *selected_step = NULL;
	jobacct_job_rec_t *job = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	List job_list = NULL;

	if(!gold_request) 
		return NULL;


	if(selected_steps && list_count(selected_steps)) {
		itr = list_iterator_create(selected_steps);
		if(list_count(selected_steps) > 1)
			set = 2;
		else
			set = 0;
		while((selected_step = list_next(itr))) {
			snprintf(tmp_buff, sizeof(tmp_buff), "%u", 
				 selected_step->jobid);
			gold_request_add_condition(gold_request, "JobId",
						   tmp_buff,
						   GOLD_OPERATOR_NONE,
						   set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	if(selected_parts && list_count(selected_parts)) {
		if(list_count(selected_parts) > 1)
			set = 2;
		else
			set = 0;
		itr = list_iterator_create(selected_parts);
		while((selected_part = list_next(itr))) {
			gold_request_add_condition(gold_request, "Partition",
						   selected_part,
						   GOLD_OPERATOR_NONE,
						   set);
			set = 1;
		}
		list_iterator_destroy(itr);
	}

	gold_request_add_selection(gold_request, "JobId");
	gold_request_add_selection(gold_request, "GoldAccountId");
	gold_request_add_selection(gold_request, "Partition");
	gold_request_add_selection(gold_request, "RequestedCPUCount");
	gold_request_add_selection(gold_request, "AllocatedCPUCount");
	gold_request_add_selection(gold_request, "NodeList");
	gold_request_add_selection(gold_request, "JobName");
	gold_request_add_selection(gold_request, "SubmitTime");
	gold_request_add_selection(gold_request, "EligibleTime");
	gold_request_add_selection(gold_request, "StartTime");
	gold_request_add_selection(gold_request, "EndTime");
	gold_request_add_selection(gold_request, "Suspended");
	gold_request_add_selection(gold_request, "State");
	gold_request_add_selection(gold_request, "ExitCode");
	gold_request_add_selection(gold_request, "QoS");

	gold_response = get_gold_response(gold_request);
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("_check_for_job: no response received");
		return NULL;
	}
	
	job_list = list_create(destroy_jobacct_job_rec);
	if(gold_response->entry_cnt > 0) {
		itr = list_iterator_create(gold_response->entries);
		while((resp_entry = list_next(itr))) {
			job = create_jobacct_job_rec();
			itr2 = list_iterator_create(resp_entry->name_val);
			while((name_val = list_next(itr2))) {
				if(!strcmp(name_val->name, "JobId")) {
					job->jobid = atoi(name_val->value);
				} else if(!strcmp(name_val->name, 
						  "GoldAccountId")) {
					acct_association_rec_t account_rec;
					memset(&account_rec, 0,
					       sizeof(acct_association_rec_t));
					account_rec.id = atoi(name_val->value);
					/* FIX ME: We need to get the
					 * parts of the association from
					 * gold here
					 */
	/* 				if(acct_storage_p_get_assoc_id( */
/* 						   db_conn, */
/* 						   &account_rec) == SLURM_ERROR) */
/* 						error("no assoc found for " */
/* 						      "id %u", */
/* 						      account_rec.id); */
					
/* 					if(account_rec.cluster) { */
/* 						if(params->opt_cluster && */
/* 						   strcmp(params->opt_cluster, */
/* 							  account_rec. */
/* 							  cluster)) { */
/* 							destroy_jobacct_job_rec( */
/* 								job); */
/* 							job = NULL; */
/* 							break; */
/* 						} */
/* 						job->cluster = */
/* 							xstrdup(account_rec. */
/* 								cluster); */
/* 					} */

					if(account_rec.user) {
						struct passwd pwd, *result;
						size_t bufsize;
						char *buffer;
						int rc;
						bufsize = sysconf(
							_SC_GETPW_R_SIZE_MAX);
						buffer = xmalloc(bufsize);
						rc = getpwnam_r(account_rec.
								user,
								&pwd, buffer,
								bufsize, 
								&result);
						if (rc != 0)
							result = NULL;
						job->user = xstrdup(account_rec.
								    user);
						if(result) {
							job->uid =
								result->
								pw_uid;
							job->gid = 
								result->
								pw_gid;
						}
						xfree(buffer);
					}
					if(account_rec.acct) 
						job->account =
							xstrdup(account_rec.
								acct);
				} else if(!strcmp(name_val->name,
						  "Partition")) {
					job->partition =
						xstrdup(name_val->value);
				} else if(!strcmp(name_val->name,
						  "RequestedCPUCount")) {
					job->req_cpus = atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "AllocatedCPUCount")) {
					job->alloc_cpus = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "NodeList")) {
					job->nodes = xstrdup(name_val->value);
				} else if(!strcmp(name_val->name, "JobName")) {
					job->jobname = xstrdup(name_val->value);
				} else if(!strcmp(name_val->name,
						  "SubmitTime")) {
					job->submit = atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "EligibleTime")) {
					job->eligible = atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "StartTime")) {
					job->start = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "EndTime")) {
					job->end = atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "Suspended")) {
					job->suspended = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "State")) {
					job->state = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "ExitCode")) {
					job->exitcode = atoi(name_val->value);
				} /* else if(!strcmp(name_val->name, "QoS")) { */
/* 					job->qos = atoi(name_val->value); */
/* 				} */
			}
			list_iterator_destroy(itr2);

			if(!job) 
				continue;

			job->show_full = 1;
			job->track_steps = 0;
			job->priority = 0;

			if (!job->nodes) 
				job->nodes = xstrdup("(unknown)");
			
			list_append(job_list, job);
		}
		list_iterator_destroy(itr);		
	}
	destroy_gold_response(gold_response);
	
	return job_list;
}

/* 
 * get info from the storage 
 * returns List of jobacct_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(void *db_conn, uid_t uid,
					    void *job_cond)
{
	info("not implemented");
	return NULL;
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_p_archive(void *db_conn,
				      List selected_parts,
				      void *params)
{
	info("not implemented");
	
	return;
}

extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used)
{
	return SLURM_SUCCESS;
}
