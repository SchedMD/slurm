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
#include "gold_interface.h"

#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#include "src/common/xmalloc.h"
#include "src/common/list.h"
#include "src/common/xstring.h"
#include "src/common/uid.h"
#include <src/common/parse_time.h>

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/jobacct_common.h"


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

/* for this first draft we are only supporting one cluster per slurm
 * 1.3 will probably do better than this.
 */

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
				   GOLD_OPERATOR_NONE);

	snprintf(tmp_buff, sizeof(tmp_buff), "%u", (int)submit);
	gold_request_add_condition(gold_request, "SubmitTime", tmp_buff,
				   GOLD_OPERATOR_NONE);

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
	
	gold_request = create_gold_request(GOLD_OBJECT_ACCOUNT,
					   GOLD_ACTION_QUERY);

	gold_request_add_selection(gold_request, "Id");
	gold_request_add_condition(gold_request, "User", user,
				   GOLD_OPERATOR_NONE);
	if(project)
		gold_request_add_condition(gold_request, "Project", project,
					   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Machine", machine,
				   GOLD_OPERATOR_NONE);
		
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
	
	
//info("total procs is  %d", job_ptr->details->total_procs);
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
					   GOLD_OPERATOR_NONE);
		
		snprintf(tmp_buff, sizeof(tmp_buff), "%u",
			 (int)job_ptr->details->submit_time);
		gold_request_add_condition(gold_request, "SubmitTime",
					   tmp_buff,
					   GOLD_OPERATOR_NONE);
	} else {
		destroy_gold_request(gold_request);
		error("_add_edit_job: bad action given %d", action);		
		return rc;
	}

	gold_request_add_assignment(gold_request, "Partition",
				    job_ptr->partition);
	
	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 job_ptr->details->total_procs);
	gold_request_add_assignment(gold_request, "RequestedCPUCount",
				    tmp_buff);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u",
		 job_ptr->details->total_procs);
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
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

extern int jobacct_storage_p_init(char *gold_info)
{
	char *total = "localhost:/etc/gold/auth_key:localhost:7112";
	int found = 0;
	int i=0, j=0;
	char *host = NULL;
	char *keyfile = NULL;
	uint16_t port = 0;

	debug2("jobacct_init() called");
	if(cluster_name) {
		info("already called init");
		return SLURM_SUCCESS;
	}
	if(gold_info) 
		total = gold_info;

	if(!gold_account_list) 
		gold_account_list = list_create(_destroy_gold_account);

	
	i = 0;
	while(total[j]) {
		if(total[j] == ':') {
			switch(found) {
			case 0: // cluster_name name
			        cluster_name = xstrndup(total+i, j-i);
				break;
			case 1: // keyfile name
				keyfile = xstrndup(total+i, j-i);
				break;
			case 2: // host name
				host = xstrndup(total+i, j-i);
				break;
			case 3: // port
				port = atoi(total+i);
				break;
			}
			found++;
			i = j+1;	
		}
		j++;
	}
	if(!port) 
		port = atoi(total+i);

	if(!cluster_name)
		fatal("JobAcctLogfile should be in the format of "
		      "cluster_name:gold_auth_key_file_path:"
		      "goldd_host:goldd_port "
		      "bad cluster_name");
	if (!keyfile || *keyfile != '/')
		fatal("JobAcctLogfile should be in the format of "
		      "cluster_name:gold_auth_key_file_path:"
		      "goldd_host:goldd_port "
		      "bad key file");
	if(!host)
		fatal("JobAcctLogfile should be in the format of "
		      "cluster_name:gold_auth_key_file_path:"
		      "goldd_host:goldd_port "
		      "bad host");
	if(!port) 
		fatal("JobAcctLogfile should be in the format of "
		      "cluster_name:gold_auth_key_file_path:"
		      "goldd_host:goldd_port "
		      "bad port");
	
	debug2("connecting from %s to gold with keyfile='%s' for %s(%d)",
	       cluster_name, keyfile, host, port);

	init_gold(cluster_name, keyfile, host, port);
		
	xfree(keyfile);
	xfree(host);

	return SLURM_SUCCESS;
}

int jobacct_storage_p_fini()
{
	xfree(cluster_name);
	if(gold_account_list) 
		list_destroy(gold_account_list);
	fini_gold();
	return SLURM_SUCCESS;
}

int jobacct_storage_p_job_start(struct job_record *job_ptr)
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

int jobacct_storage_p_job_complete(struct job_record *job_ptr) 
{
	gold_object_t action = GOLD_ACTION_MODIFY;
	
	if(!_check_for_job(job_ptr->job_id, job_ptr->details->submit_time)) {
		error("Couldn't find this job entry.  "
		      "This shouldn't happen, we are going to create one.");
		action = GOLD_ACTION_CREATE;
	}

	return _add_edit_job(job_ptr, action);
}

int jobacct_storage_p_step_start(struct step_record *step)
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

int jobacct_storage_p_step_complete(struct step_record *step)
{
	return SLURM_SUCCESS;	
}

int jobacct_storage_p_suspend(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

/* 
 * get info from the storage 
 * returns List of job_rec_t *
 * note List needs to be freed when called
 */
int jobacct_storage_p_get_jobs(List job_list,
			       List selected_steps,
			       List selected_parts,
			       void *params)
{
	info("not implemented");
	
	return SLURM_SUCCESS;
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

int jobacct_p_endpoll()
{
	return SLURM_SUCCESS;
}

int jobacct_p_set_proctrack_container_id(uint32_t id)
{
	return SLURM_SUCCESS;
}

int jobacct_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	return SLURM_SUCCESS;
}

struct jobacctinfo *jobacct_p_stat_task(pid_t pid)
{
	return NULL;
}

struct jobacctinfo *jobacct_p_remove_task(pid_t pid)
{
	return NULL;
}

void jobacct_p_suspend_poll()
{
	return;
}

void jobacct_p_resume_poll()
{
	return;
}

#define _DEBUG 0

extern int jobacct_p_node_down(struct node_record *node_ptr, time_t event_time,
			       char *reason)
{
	uint16_t cpus;
	int rc = SLURM_ERROR;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];

	if (slurmctld_conf.fast_schedule)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;

#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("Node_acct_down: %s at %s with %u cpus due to %s", 
	     node_ptr->name, tmp_buff, cpus, node_ptr->reason);
#endif
	/* If the node was already down end that record since the
	 * reason will most likely be different
	 */

	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
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
		destroy_gold_response(gold_response);
		return rc;
	}
	destroy_gold_response(gold_response);

	/* now add the new one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_CREATE);
	if(!gold_request) 
		return rc;
	
	gold_request_add_assignment(gold_request, "Machine", cluster_name);
	snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)event_time);
	gold_request_add_assignment(gold_request, "StartTime", tmp_buff);
	gold_request_add_assignment(gold_request, "Name", node_ptr->name);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u", node_ptr->cpus);
	gold_request_add_assignment(gold_request, "CPUCount", tmp_buff);
	if(reason)
		gold_request_add_assignment(gold_request, "Reason", reason);
	else	
		gold_request_add_assignment(gold_request, "Reason", 
					    node_ptr->reason);
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
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

extern int jobacct_p_node_up(struct node_record *node_ptr, time_t event_time)
{
	int rc = SLURM_ERROR;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];

#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("Node_acct_up: %s at %s", node_ptr->name, tmp_buff);
#endif
	/* FIXME: WRITE TO DATABASE HERE */

	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", node_ptr->name,
				   GOLD_OPERATOR_NONE);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
	gold_request_add_assignment(gold_request, "EndTime", tmp_buff);		
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_node_up: no response received");
		return rc;
	}

	if(gold_response->rc) {
		error("gold_response has non-zero rc(%d): %s",
		      gold_response->rc,
		      gold_response->message);
		destroy_gold_response(gold_response);
		return rc;
	}
	destroy_gold_response(gold_response);


	return rc;
}

extern int jobacct_p_cluster_procs(uint32_t procs, time_t event_time)
{
	static uint32_t last_procs = -1;
	gold_request_t *gold_request = NULL;
	gold_response_t *gold_response = NULL;
	char tmp_buff[50];
	int rc = SLURM_ERROR;

	if (procs == last_procs) {
		debug3("we have the same procs as before no need to "
		       "query the database.");
		return SLURM_SUCCESS;
	}
	last_procs = procs;

	/* Record the processor count */
#if _DEBUG
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("Node_acct_procs: %s has %u total CPUs at %s", 
	     cluster_name, procs, tmp_buff);
#endif
	
	/* get the last known one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_QUERY);
	if(!gold_request) 
		return rc;
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", "NULL",
				   GOLD_OPERATOR_NONE);

	gold_request_add_selection(gold_request, "CPUCount");
		
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
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
	}

	destroy_gold_response(gold_response);
	


	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_MODIFY);
	if(!gold_request) 
		return rc;
	
	gold_request_add_condition(gold_request, "Machine", cluster_name,
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "EndTime", "0",
				   GOLD_OPERATOR_NONE);
	gold_request_add_condition(gold_request, "Name", "NULL",
				   GOLD_OPERATOR_NONE);

	snprintf(tmp_buff, sizeof(tmp_buff), "%d", ((int)event_time - 1));
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
		destroy_gold_response(gold_response);
		return rc;
	}
	destroy_gold_response(gold_response);

	/* now add the new one */
	gold_request = create_gold_request(GOLD_OBJECT_EVENT,
					   GOLD_ACTION_CREATE);
	if(!gold_request) 
		return rc;
	
	gold_request_add_assignment(gold_request, "Machine", cluster_name);
	snprintf(tmp_buff, sizeof(tmp_buff), "%d", (int)event_time);
	gold_request_add_assignment(gold_request, "StartTime", tmp_buff);
	snprintf(tmp_buff, sizeof(tmp_buff), "%u", procs);
	gold_request_add_assignment(gold_request, "CPUCount", tmp_buff);
			
	gold_response = get_gold_response(gold_request);	
	destroy_gold_request(gold_request);

	if(!gold_response) {
		error("jobacct_p_cluster_procs: no response received");
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
