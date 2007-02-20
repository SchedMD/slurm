/*****************************************************************************\
 *  common.c - Common functions for use by SLURM's LoadLeveler APIs.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory.
 *  Written by Morris Jette <jette1@llnl.gov>
 * 
 *  This file is part of slurm_ll_api, a collection of LoadLeveler-compatable
 *  interfaces to Simple Linux Utility for Resource Managment (SLURM).  These 
 *  interfaces are used by POE (IBM's Parallel Operating Environment) to 
 *  initiated SLURM jobs. For details, see <http://www.llnl.gov/linux/slurm/>.
 *
 *  This notice is required to be provided under our contract with the U.S.
 *  Department of Energy (DOE).  This work was produced at the University
 *  of California, Lawrence Livermore National Laboratory under Contract
 *  No. W-7405-ENG-48 with the DOE.
 * 
 *  Neither the United States Government nor the University of California
 *  nor any of their employees, makes any warranty, express or implied, or
 *  assumes any liability or responsibility for the accuracy, completeness,
 *  or usefulness of any information, apparatus, product, or process
 *  disclosed, or represents that its use would not infringe
 *  privately-owned rights.
 *
 *  Also, reference herein to any specific commercial products, process, or
 *  services by trade name, trademark, manufacturer or otherwise does not
 *  necessarily constitute or imply its endorsement, recommendation, or
 *  favoring by the United States Government or the University of
 *  California.  The views and opinions of authors expressed herein do not
 *  necessarily state or reflect those of the United States Government or
 *  the University of California, and shall not be used for advertising or
 *  product endorsement purposes.
 * 
 *  The precise terms and conditions for copying, distribution and
 *  modification are specified in the file "COPYING".
\*****************************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

char *totalview_jobid;

enum SessionType poe_session;

static job_step_create_request_msg_t *_step_req_create(
                slurm_job_init_t *slurm_job_init_ptr);

/* make dummy functions to agree with LL_API definitions */
extern int ckpt_api(void);
extern int ll_preempt_api(int a, void *b, char *c, enum LL_preempt_op d);

/* Define where to find all of the SLURM API functions */
/* LL_API slurm_funcs = { */
/* 	llsubmit, */
/* 	llfree_job_info, */
/* 	GetHistory, */
/* 	ll_get_hostlist, */
/* 	ll_start_host, */
/* 	llinit, */
/* 	llinitiate, */
/* 	llwait, */
/* 	ll_get_jobs, */
/* 	ll_free_jobs, */
/* 	ll_get_nodes, */
/* 	ll_free_nodes, */
/* 	ll_start_job, */
/* 	ll_terminate_job, */
/* 	llpd_allocate, */
/* 	ll_update, */
/* 	ll_fetch, */
/* 	ll_deallocate, */
/* 	ll_query, */
/* 	ll_set_request, */
/* 	ll_reset_request, */
/* 	ll_get_objs, */
/* 	ll_next_obj, */
/* 	ll_free_objs, */
/* 	ll_init_job, */
/* 	ll_deallocate_job, */
/* 	ll_parse_string, */
/* 	ll_parse_file, */
/* 	ll_parse_verify, */
/* 	ll_request, */
/* 	ll_spawn, */
/* 	ll_event, */
/* 	ll_get_job, */
/* 	ll_close, */
/* 	ll_get_data, */
/* 	ll_set_data, */
/* 	ll_version, */
/* 	ll_control, */
/* 	ll_spawn_task, */
/* 	ll_ckpt, */
/* 	ll_modify, */
/* 	ll_preempt_api, */
/* 	ll_local_ckpt_start, */
/* 	ll_local_ckpt_complete, */
/* 	ckpt_api, */
/* 	ll_init_ckpt, */
/* 	ll_ckpt_complete, */
/* 	ll_set_ckpt_callbacks, */
/* 	ll_unset_ckpt_callbacks, */
/* 	ll_error */
/* }; */

/* Given a LL_element, return a string indicating its type */
extern char *elem_name(enum slurm_elem_types type)
{
	char *rc;

	switch (type) {
		case ADAPTER_ELEM:
			rc = "ADAPTER_ELEM";
			break;
		case CLUSTER_ELEM:
			rc = "CLUSTER_ELEM";
			break;
		case CLUSTER_QUERY:
			rc = "CLUSTER_QUERY";
			break;
		case JOB_INIT:
			rc = "JOB_INIT";
			break;
		case JOB_QUERY:
			rc = "JOB_QUERY";
			break;
		case NODE_ELEM:
			rc = "NODE_ELEM";
			break;
		case STEP_ELEM:
			rc = "STEP_ELEM";
			break;
		case SWITCH_ELEM:
			rc = "SWITCH_ELEM";
			break;
		case TASK_ELEM:
			rc = "TASK_ELEM";
			break;
		case TASK_INST_ELEM:
			rc = "TASK_INST_ELEM";
			break;
		default:
			rc = "INVALID";
	}
	return rc;
}

/* Convert QueryType object into equivalent string */
extern char *query_type_str(enum QueryType query_type)
{
	char *rc;

	switch (query_type) {
		case CLUSTERS:
			rc = "CLUSTERS";
			break;
		case JOBS:
			rc = "JOBS";
			break;
		case MACHINES:
			rc = "MACHINES";
			break;
		default:
			rc = "INVALID";
	}
	return rc;
}

/* Convert SLURM job states into equivalent LoadLeveler step states */
extern enum StepState remap_slurm_state(enum job_states slurm_job_state)
{
	if (slurm_job_state & JOB_COMPLETING)
		return STATE_COMPLETE_PENDING;

	switch (slurm_job_state) {
		case JOB_PENDING:
			return STATE_PENDING;
		case JOB_RUNNING:
			return STATE_RUNNING;
		case JOB_COMPLETE:
			return STATE_COMPLETED;
		case JOB_FAILED:
		case JOB_TIMEOUT:
		case JOB_NODE_FAIL:
			return STATE_TERMINATED;
		default:
			ERROR("remap_slurm_state(%d) unsupported\n",
					slurm_job_state);
			return STATE_COMPLETED;
	}
}

/* Convert LL state to a string */
extern char *ll_state_str(enum StepState state)
{
	switch (state) {
		case STATE_COMPLETE_PENDING:
			return "complete pending";
		case STATE_PENDING:
			return "pending";
		case STATE_RUNNING:
			return "running";
		case STATE_COMPLETED:
			return "completed";
		case STATE_TERMINATED:
			return "terminated";
		default:
			return "unknown";
	}
}

/*
 * Build a slurm job step context for a given job and step element
 * RET -1 on error, 0 otherwise
 */
extern int build_step_ctx(slurm_elem_t *job_elem, slurm_elem_t *step_elem)
{
	slurm_job_init_t *job_init_data = (slurm_job_init_t *) job_elem->data;
	slurm_step_elem_t *step_data = (slurm_step_elem_t *) step_elem->data;
	job_step_create_request_msg_t  *req  = NULL;
	uint32_t step_id;

	if (step_data->ctx)
		return 0;	/* already done */

	if (!(req = _step_req_create(job_init_data))) {
		ERROR("Unable to allocate step request message\n");
		return -1;
	}

	step_data->ctx = slurm_step_ctx_create(req);
	if (step_data->ctx == NULL) {
		ERROR("slurm_step_ctx_create: %s\n", 
			slurm_strerror(slurm_get_errno()));
		return -1;
	}

	if (slurm_step_ctx_get(step_data->ctx, SLURM_STEP_CTX_STEPID, 
			&step_id) == SLURM_SUCCESS) {
		char *step_str = malloc(32);
		if (step_str == NULL) {
			ERROR("malloc failure\n");
			return -1;
		}
		snprintf(step_str, 16, "%u", step_id);
		step_data->step_id = step_str;
		if (setenv("SLURM_STEPID", step_str, 1) != 0)
			ERROR("slurm_step_ctx_create: SLURM_STEPID set error");
	}

	return 0;
}


static job_step_create_request_msg_t *_step_req_create(
		slurm_job_init_t *slurm_job_init_ptr)
{
	job_step_create_request_msg_t *r;
	job_desc_msg_t *job_req;
	resource_allocation_response_msg_t *job_resp;
	char *slurm_network;

	if (slurm_job_init_ptr->slurm_job_desc == NULL) {
		ERROR("slurm_job_desc is NULL");
		return NULL;
	}
	job_req = slurm_job_init_ptr->slurm_job_desc;
	if (slurm_job_init_ptr->job_alloc_resp == NULL) {
		ERROR("job_alloc_resp is NULL");
		return NULL;
	}

	job_resp = slurm_job_init_ptr->job_alloc_resp;
	if (job_resp->job_id) {
		char job_id[16];
		snprintf(job_id, 16, "%u", job_resp->job_id);
		if (setenv("SLURM_JOBID", job_id, 1) != 0)
			ERROR("slurm_step_ctx_create: SLURM_JOBID set error");
		totalview_jobid = strndup(job_id, 15);
	}

	r = calloc(1, sizeof(job_step_create_request_msg_t));
	if (r == NULL) {
		ERROR("calloc error");
		return NULL;
	}

	r->job_id     = job_resp->job_id;
	r->user_id    = job_req->user_id;
	r->node_count = job_resp->node_cnt;
	/* Processor count not relevant to poe */
	r->cpu_count  = job_resp->node_cnt;
	r->num_tasks  = job_req->num_tasks;
	r->node_list  = job_resp->node_list;
	r->task_dist  = slurm_job_init_ptr->task_dist;
	r->name       = "poe";
	slurm_network = getenv("SLURM_NETWORK");
	if (slurm_network)
		r->network = slurm_network;
	else
		r->network = job_req->network; 

	if (slurmctld_comm_addr.port) {
		r->host = strdup(slurmctld_comm_addr.hostname);
		r->port = slurmctld_comm_addr.port;
	}
	return(r);
}
