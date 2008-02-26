/*****************************************************************************\
 *  jobacct_storage_gold.c - jobacct interface to gold.
 *
 *  $Id: jobacct_gold.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
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
#include "src/common/slurm_protocol_api.h"
#include "src/common/jobacct_common.h"

#include "src/database/gold_interface.h"

typedef struct {
	char *user;
	char *project;
	char *machine;
	char *gold_id;
} gold_account_t;

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
const char plugin_name[] = "Job accounting storage GOLD plugin";
const char plugin_type[] = "jobacct_storage/gold";
const uint32_t plugin_version = 100;


static char *cluster_name = NULL;
static List gold_account_list = NULL;

/* _check_for_job 
 * IN jobid - job id to check for 
 * IN submit - timestamp for submit time of job
 * RET 0 for not found 1 for found
 */

static void _destroy_gold_account(void *object)
{
	gold_account_t *gold_account = (gold_account_t *) object;
	if(gold_account) {
		xfree(gold_account->user);
		xfree(gold_account->project);
		xfree(gold_account->machine);
		xfree(gold_account->gold_id);
		xfree(gold_account);
	}
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

static char *_get_account_id(char *user, char *project, char *machine)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *gold_account_id = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	gold_account_t *gold_account = NULL;
	ListIterator itr = list_iterator_create(gold_account_list);

	while((gold_account = list_next(itr))) {
		if(user && strcmp(gold_account->user, user))
			continue;
		if(project && strcmp(gold_account->project, project))
			continue;
		gold_account_id = xstrdup(gold_account->gold_id);
		break;
	}
	list_iterator_destroy(itr);

	if(gold_account_id) 
		return gold_account_id;
	
	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_QUERY);

	gold_request_add_selection(gold_request, "Id");
	gold_request_add_condition(gold_request, "User", user,
				   GOLD_OPERATOR_NONE, 0);
	if(project)
		gold_request_add_condition(gold_request, "Project", project,
					   GOLD_OPERATOR_NONE, 0);
	gold_request_add_condition(gold_request, "Machine", machine,
				   GOLD_OPERATOR_NONE, 0);
		
	gold_response = get_gold_response(gold_request);
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("_get_account_id: no response received");
		return NULL;
	}

	if(gold_response->entry_cnt > 0) {
		resp_entry = list_pop(gold_response->entries);
		name_val = list_pop(resp_entry->name_val);

		gold_account_id = xstrdup(name_val->value);

		destroy_gold_name_value(name_val);
		destroy_gold_response_entry(resp_entry);
		/* no need to keep track of machine since this is
		 * always going to be on the same machine.
		 */
		gold_account = xmalloc(sizeof(gold_account_t));
		gold_account->user = xstrdup(user);
		gold_account->gold_id = xstrdup(gold_account_id);
		if(project)
			gold_account->project = xstrdup(project);
		list_push(gold_account_list, gold_account);
	} else {
		error("no account found returning 0");
		gold_account_id = xstrdup("0");
	}

	destroy_gold_response(gold_response);

	return gold_account_id;
}

static gold_account_t *_get_struct_from_account_id(char *gold_account_id)
{
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char *gold_account_user = NULL;
	gold_response_entry_t *resp_entry = NULL;
	gold_name_value_t *name_val = NULL;
	gold_account_t *gold_account = NULL;
	ListIterator itr = list_iterator_create(gold_account_list);
	
	if(!gold_account_id) {
		error("I need an account id to get a user from it");
		return NULL;
	}
	
	while((gold_account = list_next(itr))) {
		if(!strcmp(gold_account->gold_id, gold_account_id))
			break;
	}
	list_iterator_destroy(itr);

	if(gold_account) 
		return gold_account;
	
	gold_request = create_gold_request(GOLD_OBJECT_ACCT,
					   GOLD_ACTION_QUERY);

	gold_request_add_selection(gold_request, "User");
	gold_request_add_selection(gold_request, "Project");
	
	gold_request_add_condition(gold_request, "Id", gold_account_id,
				   GOLD_OPERATOR_NONE, 0);
		
	gold_response = get_gold_response(gold_request);
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("_get_account_id: no response received");
		return NULL;
	}

	if(gold_response->entry_cnt > 0) {
		gold_account = xmalloc(sizeof(gold_account_t));
		gold_account->gold_id = xstrdup(gold_account_id);
		
		resp_entry = list_pop(gold_response->entries);
		itr = list_iterator_create(resp_entry->name_val);
		while((name_val = list_next(itr))) {
			if(!strcmp(name_val->name, "User")) {
				gold_account->user = xstrdup(name_val->value);
				gold_account_user = xstrdup(name_val->value);
			} else if(!strcmp(name_val->name, "Project")) {
				gold_account->project =
					xstrdup(name_val->value);
			}
		}
		list_iterator_destroy(itr);
		list_push(gold_account_list, gold_account);
		destroy_gold_response_entry(resp_entry);
		
	} else {
		error("no account found returning NULL");
	}

	destroy_gold_response(gold_response);

	return gold_account;
}

static int _add_edit_job(struct job_record *job_ptr, gold_object_t action)
{
	gold_request_t *gold_request = create_gold_request(GOLD_OBJECT_JOB,
							   action);
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;
	char *gold_account_id = NULL;
	char *user = uid_to_string((uid_t)job_ptr->user_id);
	char *jname = NULL;
	int tmp = 0, i = 0;
	char *account = NULL;
	char *nodes = "(null)";

	if(!gold_request) 
		return rc;

	if ((tmp = strlen(job_ptr->name))) {
		jname = xmalloc(++tmp);
		for (i=0; i<tmp; i++) {
			if (isspace(job_ptr->name[i]))
				jname[i]='_';
			else
				jname[i]=job_ptr->name[i];
		}
	} else
		jname = xstrdup("allocation");
	
	if (job_ptr->account && job_ptr->account[0])
		account = job_ptr->account;
	
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
		
		gold_account_id = _get_account_id(user, account, 
						  cluster_name);
		
		gold_request_add_assignment(gold_request, "GoldAccountId",
					    gold_account_id);
		xfree(gold_account_id);
		
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

	gold_request_add_assignment(gold_request, "NodeList",
				    nodes);

	gold_request_add_assignment(gold_request, "JobName",
				    jname);
	xfree(jname);
	
	if(job_ptr->job_state != JOB_RUNNING) {
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 (int)job_ptr->end_time);
		gold_request_add_assignment(gold_request, "EndTime",
					    tmp_buff);		
		
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 (int)job_ptr->exit_code);
		gold_request_add_assignment(gold_request, "ExitCode",
					    tmp_buff);
	}
/* 	gold_request_add_assignment(gold_request, "ReservedCPUSeconds", */
/* 	     		            ); */


	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 (int)job_ptr->details->begin_time);
	gold_request_add_assignment(gold_request, "EligibleTime",
				    tmp_buff);

	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 (int)job_ptr->start_time);
	gold_request_add_assignment(gold_request, "StartTime",
				    tmp_buff);
		
	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 job_ptr->job_state & (~JOB_COMPLETING));
	gold_request_add_assignment(gold_request, "State",
				    tmp_buff);	

	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("_add_edit_job: no response received");
		return rc;
	}

	if(!gold_response->rc) 
		rc = SLURM_SUCCESS;
	else {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
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

	if(!(cluster_name = slurm_get_cluster_name())) 
		fatal("To run jobacct_storage/gold you have to specify "
		      "ClusterName in your slurm.conf");

	if(!(keyfile = slurm_get_jobacct_storage_pass()) 
	   || strlen(keyfile) < 1) {
		keyfile = xstrdup("/etc/gold/auth_key");
		debug2("No keyfile specified with JobAcctStoragePass, "
		       "gold using default %s", keyfile);
	}
	

	if(stat(keyfile, &statbuf)) {
		fatal("Can't stat key file %s. "
		      "To run jobacct_storage/gold you have to set "
		      "your gold keyfile as "
		      "JobAcctStoragePass in your slurm.conf", keyfile);
	}


	if(!(host = slurm_get_jobacct_storage_host())) {
		host = xstrdup("localhost");
		debug2("No host specified with JobAcctStorageHost, "
		       "gold using default %s", host);
	}

	if(!(port = slurm_get_jobacct_storage_port())) {
		port = 7112;
		debug2("No port specified with JobAcctStoragePort, "
		       "gold using default %u", port);
	}

	debug2("connecting from %s to gold with keyfile='%s' for %s(%d)",
	       cluster_name, keyfile, host, port);

	init_gold(cluster_name, keyfile, host, port);

	if(!gold_account_list) 
		gold_account_list = list_create(_destroy_gold_account);
		
	xfree(keyfile);
	xfree(host);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	xfree(cluster_name);
	if(gold_account_list) 
		list_destroy(gold_account_list);
	fini_gold();
	return SLURM_SUCCESS;
}

extern int jobacct_storage_p_init(char *gold_info)
{
	return SLURM_SUCCESS;
}

extern int jobacct_storage_p_fini()
{
	return SLURM_SUCCESS;
}

extern int jobacct_storage_p_job_start(struct job_record *job_ptr)
{
	gold_object_t action = GOLD_ACTION_CREATE;
	
	if(_check_for_job(job_ptr->job_id, job_ptr->details->submit_time)) {
		error("It looks like this job is already in GOLD.  "
		      "This shouldn't happen, we are going to overwrite "
		      "old info.");
		action = GOLD_ACTION_MODIFY;
	}

	return _add_edit_job(job_ptr, action);
}

extern int jobacct_storage_p_job_complete(struct job_record *job_ptr) 
{
	gold_object_t action = GOLD_ACTION_MODIFY;
	
	if(!_check_for_job(job_ptr->job_id, job_ptr->details->submit_time)) {
		error("Couldn't find this job entry.  "
		      "This shouldn't happen, we are going to create one.");
		action = GOLD_ACTION_CREATE;
	}

	return _add_edit_job(job_ptr, action);
}

extern int jobacct_storage_p_step_start(struct step_record *step)
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

extern int jobacct_storage_p_step_complete(struct step_record *step)
{
	return SLURM_SUCCESS;	
}

extern int jobacct_storage_p_suspend(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs(List selected_steps,
				       List selected_parts,
				       void *params)
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
	jobacct_header_t header;
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
			int req_cpus = 0;
			int alloc_cpus = 0;
			char *nodelist = NULL;
			char *job_name = NULL;
			int eligible = 0;
			int end = 0;
			int suspended = 0;
			int state = 0;
			int exitcode = 0;
			int qos = 0;
			gold_account_t *gold_account = NULL;
	
			itr2 = list_iterator_create(resp_entry->name_val);
			while((name_val = list_next(itr2))) {
				if(!strcmp(name_val->name, "JobId")) {
					header.jobnum = atoi(name_val->value);
				} else if(!strcmp(name_val->name, 
						  "GoldAccountId")) {
					gold_account =
						_get_struct_from_account_id(
							name_val->value);
					if(gold_account) {
						struct passwd *passwd_ptr =
							getpwnam(gold_account->
								 user);
						if(passwd_ptr) {
							header.uid =
								passwd_ptr->
								pw_uid;
							header.gid = 
								passwd_ptr->
								pw_gid;
						}
					}					
				} else if(!strcmp(name_val->name,
						  "Partition")) {
					header.partition =
						xstrdup(name_val->value);
				} else if(!strcmp(name_val->name,
						  "RequestedCPUCount")) {
					req_cpus = atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "AllocatedCPUCount")) {
					alloc_cpus = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "NodeList")) {
					nodelist = xstrdup(name_val->value);
				} else if(!strcmp(name_val->name, "JobName")) {
					job_name = xstrdup(name_val->value);
				} else if(!strcmp(name_val->name,
						  "SubmitTime")) {
					header.job_submit = 
						atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "EligibleTime")) {
					eligible = atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "StartTime")) {
					header.timestamp = 
						atoi(name_val->value);
				} else if(!strcmp(name_val->name, "EndTime")) {
					end = atoi(name_val->value);
				} else if(!strcmp(name_val->name,
						  "Suspended")) {
					suspended = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "State")) {
					state = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "ExitCode")) {
					exitcode = atoi(name_val->value);
				} else if(!strcmp(name_val->name, "QoS")) {
					qos = atoi(name_val->value);
				}
			}
			list_iterator_destroy(itr2);
			job = create_jobacct_job_rec(header);
			job->show_full = 1;
			job->status = state;
			job->jobname = job_name;
			job->track_steps = 0;
			job->priority = 0;
			job->ncpus = alloc_cpus;
			job->end = end;

			if (!nodelist) 
				job->nodes = xstrdup("(unknown)");
			  else
				job->nodes = nodelist;
			
			if(gold_account) 
				job->account = xstrdup(gold_account->project);
			job->exitcode = exitcode;
			list_append(job_list, job);
		}
		list_iterator_destroy(itr);		
	}
	destroy_gold_response(gold_response);
	
	return job_list;
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_p_archive(List selected_parts,
				      void *params)
{
	info("not implemented");
	
	return;
}
