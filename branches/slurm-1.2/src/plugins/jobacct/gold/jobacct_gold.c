/*****************************************************************************\
 *  jobacct_gold.c - jobacct interface to gold.
 *
 *  $Id$
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

#include "src/common/list.h"
#include <src/common/parse_time.h
#include "src/common/slurm_jobacct.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/jobacct/gold/agent.h"
#include "src/plugins/jobacct/gold/gold_interface.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd/slurmd.h"

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
const char plugin_name[] = "Job accounting GOLD plugin";
const char plugin_type[] = "jobacct/gold";
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

/*
 * The following routines are called by slurmctld
 */

/*
 * The following routines are called by slurmd
 */
int jobacct_p_init_struct(struct jobacctinfo *jobacct, 
			  jobacct_id_t *jobacct_id)
{
	return SLURM_SUCCESS;
}

struct jobacctinfo *jobacct_p_alloc(jobacct_id_t *jobacct_id)
{
	return NULL;
}

void jobacct_p_free(struct jobacctinfo *jobacct)
{
	return;
}

int jobacct_p_setinfo(struct jobacctinfo *jobacct, 
		      enum jobacct_data_type type, void *data)
{
	return SLURM_SUCCESS;
	
}

int jobacct_p_getinfo(struct jobacctinfo *jobacct, 
		      enum jobacct_data_type type, void *data)
{
	return SLURM_SUCCESS;
}

void jobacct_p_aggregate(struct jobacctinfo *dest, struct jobacctinfo *from)
{
	return;
}

void jobacct_p_2_sacct(sacct_t *sacct, struct jobacctinfo *jobacct)
{
	return;
}

void jobacct_p_pack(struct jobacctinfo *jobacct, Buf buffer)
{
	return;
}

int jobacct_p_unpack(struct jobacctinfo **jobacct, Buf buffer)
{
	return SLURM_SUCCESS;
}


int jobacct_p_init_slurmctld(char *gold_info)
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

int jobacct_p_fini_slurmctld()
{
	xfree(cluster_name);
	if(gold_account_list) 
		list_destroy(gold_account_list);
	fini_gold();
	return SLURM_SUCCESS;
}

int jobacct_p_job_start_slurmctld(struct job_record *job_ptr)
{
	slurmdbd_msg_t msg;
	dbd_job_start_msg_t req;

	req.assoc_id		= procs;
	req.block_id		= procs;
	req.eligible_time	= procs;
	req.job_id		= procs;
	req.job_state		= procs;
	req.name		= procs;
	req.nodes		= procs;
	req.priority		= procs;
	req.start_time		= procs;
	req.submit_time		= procs;
	req.total_procs		= procs;
	msg.msg_type		= DBD_JOB_START;
	msg.data		= &procs_msg;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;
}

int jobacct_p_job_complete_slurmctld(struct job_record *job_ptr) 
{
	slurmdbd_msg_t msg;
	dbd_job_comp_msg_t req;

	req.assoc_id		= procs;
	req.end_time		= procs;
	req.exit_code		= procs;
	req.job_id		= procs;
	req.job_state		= procs;
	req.name		= procs;
	req.nodes		= procs;
	req.priority		= procs;
	req.start_time		= procs;
	req.total_procs		= procs;
	msg.msg_type		= DBD_JOB_COMPLETE;
	msg.data		= &procs_msg;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;
}

int jobacct_p_step_start_slurmctld(struct step_record *step)
{
	slurmdbd_msg_t msg;
	dbd_step_start_msg_t req;

	req.assoc_id		= procs;
	req.job_id		= procs;
	req.name		= procs;
	req.nodes		= procs;
	req.req_uid		= procs;
	req.start_time		= procs;
	req.step_id		= procs;
	req.total_procs		= procs;
	msg.msg_type		= DBD_STEP_START;
	msg.data		= &procs_msg;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;
}

int jobacct_p_step_complete_slurmctld(struct step_record *step)
{
	return SLURM_SUCCESS;	
}

int jobacct_p_suspend_slurmctld(struct job_record *job_ptr)
{
	return SLURM_SUCCESS;
}

int jobacct_p_startpoll(int frequency)
{
	info("jobacct GOLD plugin loaded");
	debug3("slurmd_jobacct_init() called");
	
	return SLURM_SUCCESS;
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
	slurmdbd_msg_t msg;
	dbd_node_down_msg_t req;
	uint16_t cpus;

	if (slurmctld_conf.fast_schedule)
		cpus = node_ptr->config_ptr->cpus;
	else
		cpus = node_ptr->cpus;
	if (reason == NULL)
		reason = node_ptr->reason;
#if _DEBUG
{
	char tmp_buff[50];
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("jobacct_p_node_down: %s at %s with %u cpus due to %s", 
	     node_ptr->name, tmp_buff, cpus, reason);
}
#endif
	req.cpus         = cpus;
	req.event_time   = event_time;
	req.hostlist     = node_ptr->name;
	req.reason       = reason;
	msg.msg_type     = DBD_NODE_DOWN;
	msg.data         = &req;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int jobacct_p_node_up(struct node_record *node_ptr, time_t event_time)
{
	slurmdbd_msg_t msg;
	dbd_node_up_msg_t req;

#if _DEBUG
{
	char tmp_buff[50];
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("jobacct_p_node_up: %s at %s", node_ptr->name, tmp_buff);
}
#endif

	req.hostlist     = node_ptr->name;
	req.event_time   = event_time;
	msg.msg_type     = DBD_NODE_UP;
	msg.data         = &req;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

extern int jobacct_p_cluster_procs(uint32_t procs, time_t event_time)
{
	static uint32_t last_procs = 0;
	slurmdbd_msg_t msg;
	dbd_node_state_msg_t req;

#if _DEBUG
{
	char tmp_buff[50];
	slurm_make_time_str(&event_time, tmp_buff, sizeof(tmp_buff));
	info("jobacct_p_cluster_procs: %s has %u total CPUs at %s", 
	     cluster_name, procs, tmp_buff);
}
#endif
	if (procs == last_procs) {
		debug3("jobacct_p_cluster_procs: no change in proc count");
		return SLURM_SUCCESS;
	}
	last_procs = procs;

	req.proc_count		= procs;
	req.event_time		= event_time;
	msg.msg_type		= DBD_CLUSTER_PROCS;
	msg.data		= &req;

	if (slurm_send_slurmdbd_msg(&msg) < 0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

