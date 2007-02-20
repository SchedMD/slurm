/*****************************************************************************\
 *  Function: ll_get_data
 *
 *  Description: Return data from a valid LL_element (return a specific 
 *  type of data from a LoadLeveler structure that has already been filled 
 *  by ll_get_objs).
 *
 *  Arguments:
 *    IN element : data element from which to get information, this data 
 *                 was returned by ll_get_objs() or ll_get_data().
 *    IN specification : the time of data requested
 *    OUT resulting_data_type : the data value
 *    RET Success: 0
 *        Failure: -1 : invalid element value
 *                 -2 : invalid specification value 
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <regex.h>

#include <ntbl.h>

#include "common.h"
#include "federation_keys.h"

static slurm_elem_t *_build_step(slurm_elem_t *slurm_job_init_ptr);
static slurm_elem_t *_build_taski(slurm_elem_t *slurm_elem);
static slurm_elem_t *_build_adapter(slurm_elem_t *slurm_elem,
				    int adapter_idx);
static slurm_elem_t *_build_switch(slurm_elem_t *slurm_elem);
static slurm_elem_t *_build_machine(slurm_elem_t *step_elem);
static int	_get_data_adapter(slurm_elem_t *slurm_elem,
				  enum LLAPI_Specification specification,
				  void *resulting_data_type);
static int	_get_data_cluster(slurm_elem_t *slurm_elem,
				  enum LLAPI_Specification specification,
				  void *resulting_data_type);
static int	_get_data_job(slurm_elem_t *slurm_elem, 
			      enum LLAPI_Specification specification,
			      void *resulting_data_type);
static int	_get_data_node(slurm_elem_t *slurm_elem,
			       enum LLAPI_Specification specification,
			       void *resulting_data_type);
static int	_get_data_step(slurm_elem_t *slurm_elem,
			       enum LLAPI_Specification specification,
			       void *resulting_data_type);
static int	_get_data_switch(slurm_elem_t *slurm_elem,
				 enum LLAPI_Specification specification,
				 void *resulting_data_type);
static int	_get_data_task(slurm_elem_t *slurm_elem,
			       enum LLAPI_Specification specification,
			       void *resulting_data_type);
static int	_get_data_task_inst(slurm_elem_t *slurm_elem,
				    enum LLAPI_Specification specification,
				    void *resulting_data_type);
static LL_element *
_get_node_first_task(slurm_elem_t *slurm_elem);
static LL_element *
_get_node_next_task(slurm_elem_t *slurm_elem);
static LL_element *
_get_step_first_node(slurm_elem_t *slurm_elem,
		     char **node_name);
static LL_element *
_get_step_next_node(slurm_elem_t *slurm_step_ptr,
		    char **node_name);
static int	_get_step_node_cnt(slurm_step_elem_t *step_elem_ptr);
static int	_get_task_cnt(slurm_step_elem_t *step_data, int node_inx);
static void	_get_task_ids(slurm_step_elem_t *step_data,
			      slurm_node_elem_t *node_data, int node_inx);
static void _set_network_parameters(slurm_step_elem_t *step_data,
				    slurm_job_init_t *job_data);

extern int ll_get_data(LL_element *element,
		       enum LLAPI_Specification specification, 
		       void *resulting_data_type)
{
	slurm_elem_t *slurm_elem = (slurm_elem_t *) element;
	int rc;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_get_data\n");
	VERBOSE("LLAPI_Specification: %d\n", specification);

	if (element == NULL) {
		ERROR("element is NULL, spec=%u\n", 
		      specification);
		rc = -1;
		goto done;
	}
	if (resulting_data_type == NULL) {
		ERROR("resulting_data_type is NULL, spec=%u\n",
		      specification);
		rc = -1;
		goto done;
	}

	VERBOSE("data type=%s\n", elem_name(slurm_elem->type));
	switch (slurm_elem->type) {
	case ADAPTER_ELEM:
		rc = _get_data_adapter(slurm_elem, specification,
				       resulting_data_type);
		break;
	case CLUSTER_ELEM:
		rc = _get_data_cluster(slurm_elem, specification,
				       resulting_data_type);
		break;
	case JOB_INIT:
		rc = _get_data_job(slurm_elem, specification,
				   resulting_data_type);
		break;
	case NODE_ELEM:
		rc = _get_data_node(slurm_elem, specification,
				    resulting_data_type);
		break;
	case STEP_ELEM:
		rc = _get_data_step(slurm_elem, specification,
				    resulting_data_type);
		break;
	case SWITCH_ELEM:
		rc = _get_data_switch(slurm_elem, specification,
				      resulting_data_type);
		break;
	case TASK_ELEM:
		rc = _get_data_task(slurm_elem, specification,
				    resulting_data_type);
		break;
	case TASK_INST_ELEM:
		rc = _get_data_task_inst(slurm_elem, specification,
					 resulting_data_type);
		break;
	default:
		ERROR("ll_get_data: %s element type "
		      "unsupported\n", 
		      elem_name(slurm_elem->type));
		rc = -1;
		break;
	}

done:
	VERBOSE("--------------------------------------------------\n");
	return rc;
}

static int _get_data_adapter(slurm_elem_t *slurm_elem,
			     enum LLAPI_Specification specification,
			     void *resulting_data_type)
{
	int rc = 0;
	slurm_adapter_elem_t *slurm_adapter_elem_ptr = (slurm_adapter_elem_t *)
		slurm_elem->data;
	char **resulting_char_data = (char **) resulting_data_type;
	int *resulting_int_data    = (int *) resulting_data_type;
		
	switch (specification) {
	case LL_AdapterUsageProtocol:
		/* char * : Protocol used by task	*/
		*resulting_char_data =
			slurm_adapter_elem_ptr->protocol;
		VERBOSE("LL_AdapterUsageProtocol = %s\n",
			*resulting_char_data);
		break;
	case LL_AdapterUsageMode:
		/* char * : Used for css IP or US	*/
		*resulting_char_data =
			slurm_adapter_elem_ptr->mode;
		VERBOSE("LL_AdapterUsageMode = %s\n",
			*resulting_char_data);
		break;
	case LL_AdapterUsageAddress:
		/* char * : IP Address to use adapter	*/
		*resulting_char_data = slurm_adapter_elem_ptr->address;
		/*resulting_char_data = strdup("0.0.0.0");*/
		
		VERBOSE("LL_AdapterUsageAddress = %s\n",
			*resulting_char_data);
		break;
	case LL_AdapterUsageWindow:
		/* int * : window assigned to task	*/
		*resulting_int_data = 
			slurm_adapter_elem_ptr->window;
		VERBOSE("LL_AdapterUsageWindow = %d\n", 
			*resulting_int_data);
		break;
	case LL_AdapterUsageDevice:
		/* char * : adapter device being used	*/
		*resulting_char_data = 
			slurm_adapter_elem_ptr->device;
		VERBOSE("LL_AdapterUsageDevice = %s\n",
			*resulting_char_data);
		break;
	case LL_AdapterUsageInstanceNumber:
		/* int * :Unique ID for multiple instances  */
		*resulting_int_data = slurm_adapter_elem_ptr->unique_id;
		VERBOSE("LL_AdapterUsageInstanceNumber = %d\n",
			*resulting_int_data);
		break;
	case LL_AdapterUsageNetworkId:
		/* int *: Network ID of adapter being used */
		*resulting_int_data = slurm_adapter_elem_ptr->network_id;
		VERBOSE("LL_AdapterUsageNetworkId = %d\n",
			*resulting_int_data);
		break;
  	case LL_AdapterUsageRcxtBlocks: 
  		/* int * : total number of RCXT blocks  
  		   application needs */
		*resulting_int_data =
			slurm_adapter_elem_ptr->job_init->user_rcontext_blocks;

		/*
		 * If bulk_xfer is in use, poe wants rcxt blocks incremented
		 * by one.  However, the the call to ntbl_load_table_rdma()
		 * in the slurm federation driver does the incrementing
		 * internally.
		 */
		if (slurm_adapter_elem_ptr->job_init->bulk_xfer)
			*resulting_int_data += 1;

  		VERBOSE("LL_AdapterUsageRcxtBlocks = %d\n", 
  			*resulting_int_data); 
  		break; 
	default:
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static int _get_data_cluster(slurm_elem_t *slurm_elem,
			     enum LLAPI_Specification specification,
			     void *resulting_data_type)
{
	int rc = 0;
/*	slurm_cluster_elem_t *slurm_cluster_elem_ptr = (slurm_cluster_elem_t *)
	slurm_elem->data;	Unused */
	char **resulting_char_data = (char **) resulting_data_type;

	switch (specification) {
	case LL_ClusterSchedulerType:
		/* char *: scheduler type		*/
		/* Not applicable to SLURM              */
		*resulting_char_data = "slurm";
		VERBOSE("LL_ClusterSchedulerType = %s\n",
			*resulting_char_data);
		break;
	default:
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static int _get_data_job(slurm_elem_t *slurm_elem,
			 enum LLAPI_Specification specification,
			 void *resulting_data_type)
{
	int rc = 0;
	slurm_job_init_t *job_data = (slurm_job_init_t *) slurm_elem->data;
	char **resulting_char_data = (char **) resulting_data_type;
	LL_element **resulting_elem_data = (LL_element **) resulting_data_type;

	switch (specification) {
	case LL_JobGetFirstStep:
		/* LL_element * (Step) : first step	*/
		VERBOSE("LL_JobGetFirstStep = step[0]\n");
		if (job_data == NULL) { 
			ERROR("job_data is NULL\n");
			return -1;
		}
		if (job_data->first_step_elem == NULL) {
			job_data->first_step_elem = 
				_build_step(slurm_elem);
		}
		*resulting_elem_data = job_data->first_step_elem;
		if (job_data->job_state >= JOB_RUNNING) {
			rc = build_step_ctx(slurm_elem, 
					    job_data->first_step_elem);
		}
		break;
	case LL_JobManagementAccountNo:
		/* char * : returns LOADL_ACCOUNT_NO	*/
		/* Not applicable to SLURM		*/
		*resulting_char_data = "NoAcct";
		VERBOSE("LL_JobManagementAccountNo = %s\n",
			*resulting_char_data);
		break;
	case LL_JobManagementInteractiveClass:
		/* char *: LL interactive class		*/
		/* Not applicable to SLURM		*/
		*resulting_char_data = "InteractiveClass";
		VERBOSE(
			"LL_JobManagementInteractiveClass = %s\n",
			*resulting_char_data);
		break;
	default: 
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static slurm_elem_t * _build_step(slurm_elem_t *slurm_job_init_ptr)
{
	slurm_job_init_t *job_data;
	slurm_elem_t *rc = (slurm_elem_t*) NULL;
	slurm_step_elem_t *step_data;

	step_data = calloc(1, sizeof(slurm_step_elem_t));
	if (step_data == NULL) {
		ERROR("calloc failure\n");
		return rc;
	}
	rc = calloc(1, sizeof(slurm_elem_t));
	if (rc == NULL) {
		ERROR("calloc failure\n");
		free(step_data);
		return rc;
	}

	rc->type	= STEP_ELEM;
	rc->data	= step_data;
	step_data->job_init_elem =  slurm_job_init_ptr;

	job_data = (slurm_job_init_t *) slurm_job_init_ptr->data;
	if (job_data->first_step_elem == NULL)
		job_data->first_step_elem = rc;

	return rc;
}

static int _get_data_node(slurm_elem_t *slurm_elem,
			  enum LLAPI_Specification specification,
			  void *resulting_data_type)
{
	int rc = 0;
	slurm_node_elem_t *node_data = (slurm_node_elem_t *)
		slurm_elem->data;
	LL_element **resulting_elem_data = (LL_element **) resulting_data_type;
	int *resulting_int_data = (int *) resulting_data_type;

	switch (specification) {
	case LL_NodeTaskCount:
		/* int * : number of task instances	*/
		*resulting_int_data = node_data->task_cnt;
		VERBOSE("LL_NodeTaskCount = %d\n", 
			*resulting_int_data);
		break;
	case LL_NodeGetFirstTask:
		/* LL_element * (Task) : first task	*/
		*resulting_elem_data = _get_node_first_task(slurm_elem);
		if (*resulting_elem_data)
			VERBOSE("LL_NodeGetFirstTask = task[0]\n");
		else
			ERROR("LL_NodeGetFirstTask = NULL\n");
		break;
	case LL_NodeGetNextTask:
		/* LL_element * (Task) : next task	*/
		*resulting_elem_data = _get_node_next_task(slurm_elem);
		if (*resulting_elem_data)
			VERBOSE("LL_NodeGetNextTask\n");
		else
			ERROR("LL_NodeGetNextTask = NULL\n");
		break;
	default:
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static LL_element *_get_node_first_task(slurm_elem_t *slurm_elem)
{
	slurm_node_elem_t *node_data = (slurm_node_elem_t *)
		slurm_elem->data;

	node_data->next_task_inx = 0;

	return _get_node_next_task(slurm_elem);
}

static LL_element *_get_node_next_task(slurm_elem_t *slurm_elem)
{
	slurm_node_elem_t *node_data = (slurm_node_elem_t *) slurm_elem->data;
	slurm_elem_t *rc = (slurm_elem_t *) NULL;
	slurm_task_elem_t *task_data;

	if ((node_data->next_task_inx < 0) ||
	    (node_data->next_task_inx >= node_data->task_cnt)) {
		ERROR("Invalid task count on node %s\n",
		      node_data->node_name);
		return NULL;
	}

	task_data = calloc(1, sizeof(slurm_task_elem_t));
	if (task_data == NULL) {
		ERROR("calloc failure\n");
		return rc;
	}
	rc = calloc(1, sizeof(slurm_elem_t));
	if (rc == NULL) {
		ERROR("calloc failure\n");
		free(task_data);
		return rc;
	}

	rc->type = TASK_ELEM;
	rc->data = task_data;
	task_data->node_elem = slurm_elem;
	task_data->task_inx  = node_data->next_task_inx++;
	task_data->node_inx  = node_data->node_inx;
	task_data->task_id   = (int) node_data->task_ids[task_data->task_inx];

	return rc;
}

static int _get_data_step(slurm_elem_t *slurm_elem,
			  enum LLAPI_Specification specification,
			  void *resulting_data_type)
{
	int rc = 0;
	slurm_step_elem_t *step_data = (slurm_step_elem_t *) slurm_elem->data;
	slurm_job_init_t *job_data = (slurm_job_init_t *) 
		step_data->job_init_elem->data;
	char **resulting_char_data = (char **) resulting_data_type;
	char *node_name;
	int *resulting_int_data = (int *) resulting_data_type;
	LL_element **resulting_elem_data = (LL_element **) resulting_data_type;

	switch (specification) {
	case LL_StepID:
		/* char * : scheddhostname.jobid.stepid	*/
		if (step_data->step_id == NULL) {
			uint32_t step_id = 0;
			if ((step_data->step_id = malloc(32)) 
			    == NULL) {
				ERROR("malloc failure\n");
				rc = -3;
				break;
			}
			/* Unless the resources were pre-allocated 
			 * (e.g. a batchjob,) we do not have the job 
			 * step context or a real step id, just 
			 * return "0". */
			if (step_data->ctx)
				slurm_step_ctx_get(step_data->ctx,
						   SLURM_STEP_CTX_STEPID, 
						   &step_id);
			snprintf(step_data->step_id, 32, "%u", step_id);
		}
		VERBOSE("LL_StepID = %s\n", 
			step_data->step_id);
		*resulting_char_data = step_data->step_id;
		break;
	case LL_StepCheckpointable:
		/* int * : 0-job not checkpointable	*/
		/* No SLURM checkpoint support now	*/
		*resulting_int_data = 0;
		break;
	case LL_StepState:
		/* int * : current state of the step,
		 * Actually this is a JOB state in the case of SLURM */
		*resulting_int_data = remap_slurm_state(
			job_data->job_state);
		VERBOSE("LL_StepState = %s\n", 
			ll_state_str(*resulting_int_data));
		break;
	case LL_StepLargePage:
		/* char ** : Large Page requirement 
		   (= "M", "Y", or "N") */
		/* FIXME: figure out what this means */
		*resulting_char_data = strdup("M");
		VERBOSE("LL_LargePage = %s\n", 
			*resulting_char_data);
		break;
	case LL_StepBulkXfer:
		/* int * : 0|1 Step Requests Bulk Transfer */
		if (job_data->bulk_xfer) {
			*resulting_int_data = job_data->bulk_xfer;
		} else {
			char *ptr;
			ptr = getenv("SLURM_NETWORK");
			if (strstr(ptr, "bulk_xfer")
			    || strstr(ptr, "BULK_XFER")) {
				job_data->bulk_xfer = 1;
				*resulting_int_data = 1;
			} else {
				*resulting_int_data = 0;
			}
		}

		VERBOSE("LL_StepBulkXfer = %d\n", 
			*resulting_int_data);
		break;
	case LL_StepTotalRcxtBlocks:
		/* OBSOLETE since PE4.2.2 */
		/* int * : total number of RCXT blocks 
		   application needs */
		*resulting_int_data = job_data->bulk_xfer;
		
		VERBOSE("LL_StepTotalRcxtBlocks = %d\n", 
			*resulting_int_data);
		break;
	case LL_StepMessages:
		/* char * : string of messages from LL	*/
		/* Not applicable to SLURM              */
		if (job_data->messages)
			*resulting_char_data = strdup(
				job_data->messages);
		else

			*resulting_char_data = strdup("");
		VERBOSE("LL_StepMessages = %s\n",
			*resulting_char_data);
		break;
	case LL_StepTaskInstanceCount:
		/* int * : total number of task instances	*/
		*resulting_int_data = 
			job_data->slurm_job_desc->num_tasks;
		break;
	case LL_StepJobClass:
		/* char * : defined class for step	*/
		/* Not applicable to SLURM		*/
		*resulting_char_data = strdup("");
		VERBOSE("LL_StepJobClass = %s\n",
			*resulting_char_data);
		break;
	case LL_StepMaxProtocolInstances:
		/* int * : largest number of instances allowed */
		/*         on network stmt */
		*resulting_int_data = 0;
		VERBOSE("LL_StepMaxProtocolInstances = %d\n",
			*resulting_int_data);
		break;
	case LL_StepGetFirstSwitchTable:
		/* LL_element * (SwitchTable):		*/
		*resulting_elem_data = _build_switch(slurm_elem);
		if (*resulting_elem_data)
			VERBOSE("LL_StepGetFirstSwitchTable\n");
		else
			VERBOSE("LL_StepGetFirstSwitchTable = NULL\n");
		break;
	case LL_StepNodeCount:
		/* int * : number of nodes running the step	*/
		*resulting_int_data = _get_step_node_cnt(step_data);
		VERBOSE("LL_StepNodeCount = %d\n", 
			*resulting_int_data);
		break;
	case LL_StepGetFirstNode:
		/* LL_element * (Node) : first node	*/
		*resulting_elem_data = _get_step_first_node(
			slurm_elem, &node_name);
		if (*resulting_elem_data)
			VERBOSE("LL_StepGetFirstNode = %s\n",
				node_name);
		else
			ERROR("LL_StepGetFirstNode = NULL\n");
		break;
	case LL_StepGetNextNode:
		/* LL_element * (Node) : next node	*/
		*resulting_elem_data = _get_step_next_node(
			slurm_elem, &node_name);
		if (*resulting_elem_data)
			VERBOSE("LL_StepGetNextNode = %s\n", node_name);
		else
			ERROR("LL_StepGetNextNode = NULL\n");
		break;
	case LL_StepGetFirstMachine:
		/* LL_element * (Node) : first node	*/
		*resulting_elem_data = _build_machine(slurm_elem);
		if (*resulting_elem_data)
			VERBOSE("LL_StepGetFirstMachine = %s\n",
				node_name);
		else
			ERROR("LL_StepGetFirstMachine = NULL\n");
		break;
	case LL_StepGetNextMachine:
		/* LL_element * (Node) : next node	*/
		*resulting_elem_data = _build_machine(slurm_elem);
		if (*resulting_elem_data)
			VERBOSE("LL_StepGetNextMachine = %s\n", node_name);
		else
			ERROR("LL_StepGetNextMachine = NULL\n");
		break;
	default:
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static slurm_elem_t *_build_switch(slurm_elem_t *step_elem)
{
	slurm_elem_t *rc = (slurm_elem_t *)NULL;
	slurm_step_elem_t *step_data = step_elem->data;
	slurm_switch_elem_t *switch_data;
	job_step_create_response_msg_t *resp_msg;
	switch_jobinfo_t jobinfo;
	int key;
	
	if(slurm_step_ctx_get(step_data->ctx, 
			      SLURM_STEP_CTX_RESP, 
			      &resp_msg) < 0) {
		ERROR("step_ctx_get RESP: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return rc;
	}
	jobinfo = resp_msg->switch_job;
	if(!jobinfo)
		ERROR("Hey this jobinfo isn't set\n");
	switch_data = calloc(1, sizeof(slurm_switch_elem_t));
	if (switch_data == NULL) {
		ERROR("calloc failure\n");
		return rc;
	}
	rc = calloc(1, sizeof(slurm_elem_t));
	if (rc == NULL) {
		ERROR("calloc failure\n");
		free(switch_data);
		return rc;
	}

	rc->type = SWITCH_ELEM;
	rc->data = switch_data;

	if(slurm_jobinfo_ctx_get(
		   jobinfo, FED_JOBINFO_KEY, &key) < 0) {
		ERROR("jobinfo_ctx_get RESP: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return rc;
	}	
	switch_data->job_key = key;
	
	return rc;
}

static slurm_elem_t *_build_machine(slurm_elem_t *step_elem)
{
	slurm_elem_t *rc = (slurm_elem_t *)NULL;
	slurm_step_elem_t *step_data = step_elem->data;
	slurm_machine_elem_t *machine_data;
	job_step_create_response_msg_t *resp_msg;
	switch_jobinfo_t jobinfo;
	int key;
	
	if(slurm_step_ctx_get(step_data->ctx, 
			      SLURM_STEP_CTX_RESP, 
			      &resp_msg) < 0) {
		ERROR("step_ctx_get RESP: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return rc;
	}
	jobinfo = resp_msg->switch_job;
	
	machine_data = calloc(1, sizeof(slurm_machine_elem_t));
	if (machine_data == NULL) {
		ERROR("calloc failure\n");
		return rc;
	}
	rc = calloc(1, sizeof(slurm_elem_t));
	if (rc == NULL) {
		ERROR("calloc failure\n");
		free(machine_data);
		return rc;
	}

	rc->type = SWITCH_ELEM;
	rc->data = machine_data;

	if(slurm_jobinfo_ctx_get(
		   jobinfo, FED_JOBINFO_KEY, &key) < 0) {
		ERROR("machine jobinfo_ctx_get RESP: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return rc;
	}	
	machine_data->job_key = key;
	
	return rc;
}

static LL_element *_get_step_first_node(slurm_elem_t *slurm_elem,
					char **node_name)
{
	slurm_step_elem_t *step_data = (slurm_step_elem_t *) slurm_elem->data;
	slurm_job_init_t *job_data;

	if ((step_data->job_init_elem == NULL) ||
	    ((job_data = (slurm_job_init_t *) step_data->job_init_elem->data) 
	     == NULL)) {
		ERROR("slurm_step_elem lacks job_init_elem\n");
		*node_name = NULL;
		return (LL_element *) NULL;
	}
	if (step_data->host_set == NULL)
		step_data->host_set = hostset_create(
			job_data->job_alloc_resp->node_list);
	if (step_data->host_set == NULL) {
		ERROR("hostset_create failure for %s\n",
		      job_data->job_alloc_resp->node_list);
		*node_name = NULL;
		return (LL_element *) NULL;
	}
	if (step_data->node_cnt == 0) {
		step_data->node_cnt = hostset_count(step_data->host_set);
	}
	
	if (step_data->host_set_copy)
		hostset_destroy(step_data->host_set_copy);
	step_data->host_set_copy = hostset_copy(step_data->host_set);

	_set_network_parameters(step_data, job_data);

	return _get_step_next_node(slurm_elem, node_name) ;
}

/* We probably want to optimize this to use node_index values to map 
 * between job and note table records */
static LL_element *_get_step_next_node(slurm_elem_t *slurm_elem,
				       char **node_name)
{
	slurm_elem_t *rc = (slurm_elem_t *) NULL;
	slurm_step_elem_t *step_data = (slurm_step_elem_t *) slurm_elem->data;
	slurm_node_elem_t * node_data;
	int node_inx;
	struct hostent* pHostInfo;   /* holds info about a host */
	char dotted_quad[INET6_ADDRSTRLEN];

	if (step_data->host_set_copy == NULL) {
		ERROR("called LL_GetNextNode before "
		      "LL_GetFirstNode\n");
		*node_name = NULL;
		return (LL_element *) NULL;
	}

	node_inx = step_data->node_cnt - 
		hostset_count(step_data->host_set_copy);
	*node_name = hostset_shift(step_data->host_set_copy);
	if (*node_name == NULL) {
		VERBOSE("no more hosts in list\n");
		return (LL_element *) NULL;
	}
	
	node_data = calloc(1, sizeof(slurm_node_elem_t));
	if (node_data== NULL) {
		ERROR("calloc failure\n");
		return (LL_element *) NULL;
	}
	rc = calloc(1, sizeof(slurm_elem_t));
	if (rc == NULL) {
		ERROR("calloc failure\n");
		free(node_data);
		return (LL_element *) NULL;
	}

	rc->type = NODE_ELEM;
	rc->data = node_data;
	node_data->node_name = *node_name;
	if((pHostInfo=gethostbyname(node_data->node_name)) == NULL) {
		printf("base::init getHostbyname "
		       "returned NULL for %s\n", node_data->node_name);
		free(node_data);
		return (LL_element *) NULL;
	}
	if (inet_ntop(pHostInfo->h_addrtype, pHostInfo->h_addr,
		      dotted_quad, sizeof(dotted_quad)) <= 0)
		printf("error, with inet_ntop\n");
	node_data->node_addr = strdup(dotted_quad);
	node_data->node_inx  = node_inx;
	node_data->task_cnt  = _get_task_cnt(step_data, node_inx);
	_get_task_ids(step_data, node_data, node_inx);
	node_data->step_elem = slurm_elem;

	return (LL_element *) rc;
}

/* Determine the number of tasks to be initiated on a given node */
static int _get_task_cnt(slurm_step_elem_t *step_data, int node_inx)
{
	if (step_data->tasks_per_node == NULL) {
		if (slurm_step_ctx_get(step_data->ctx, 
				       SLURM_STEP_CTX_TASKS, 
				       &step_data->tasks_per_node) < 0) {
			ERROR("step_ctx_get TASKS: %s\n",
			      slurm_strerror(slurm_get_errno()));
			return 0;
		}
	}

	if (step_data->tasks_per_node)
		return (int) step_data->tasks_per_node[node_inx];
	else {
		ERROR("tasks_per_node is NULL\n");
		return 0;
	}
}

/* Determine the task IDs to be initiated on a given node */
static void _get_task_ids(slurm_step_elem_t *step_data, 
			  slurm_node_elem_t *node_data, int node_inx)
{
	if (slurm_step_ctx_get(step_data->ctx,
			       SLURM_STEP_CTX_TID, node_inx,
			       &node_data->task_ids) < 0) {
		ERROR("step_ctx_get TID: %s\n",
		      slurm_strerror(slurm_get_errno()));
	}
}

/* Determine the node count from the allocated node list expression */
static int _get_step_node_cnt(slurm_step_elem_t *step_data)
{
	slurm_job_init_t *job_data;

	if ((step_data->node_cnt) ||		/* Already have data */
	    (step_data->job_init_elem == NULL))	/* Insufficient data */
		goto done;

	job_data = (slurm_job_init_t *) step_data->job_init_elem->data;
	if ((job_data->job_alloc_resp == NULL) ||
	    (job_data->job_alloc_resp->node_list == NULL) )
		goto done;

	if (step_data->host_set == NULL)
		step_data->host_set = hostset_create(
			job_data->job_alloc_resp->node_list);
	if (step_data->host_set == NULL) {
		ERROR("hostset_create failure for %s\n",
		      job_data->job_alloc_resp->node_list);
		return 0;
	}
	step_data->node_cnt = hostset_count(step_data->host_set);

done:	return step_data->node_cnt;
}

static int _get_data_switch(slurm_elem_t *slurm_elem,
			    enum LLAPI_Specification specification,
			    void *resulting_data_type)
{
	int rc = 0;
	slurm_switch_elem_t *switch_data = (slurm_switch_elem_t *)
		slurm_elem->data;
	int *resulting_int_data = (int *) resulting_data_type;

	switch (specification) {
	case LL_SwitchTableJobKey:
		/* int * : job key			*/
		*resulting_int_data = switch_data->job_key;
		VERBOSE("LL_SwitchTableJobKey = %d\n",
			*resulting_int_data);
		break;
	default:
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static int _get_data_task(slurm_elem_t *slurm_elem,
			  enum LLAPI_Specification specification,
			  void *resulting_data_type)
{
	int rc = 0;
/*	slurm_task_elem_t *task_data = (slurm_task_elem_t *)
	slurm_elem->data; */
	LL_element **resulting_elem_data = (LL_element **) resulting_data_type;
	int *resulting_int_data = (int *) resulting_data_type;

	switch (specification) {
	case LL_TaskTaskInstanceCount:
		/* int * : number of task instances
		 * Always 1 for SLURM			*/
		*resulting_int_data = 1;
		VERBOSE("LL_TaskTaskInstanceCount = %d\n",
			*resulting_int_data);
		break;
	case LL_TaskGetFirstTaskInstance:
		/* LL_element * (TaskInstance)		*/
		*resulting_elem_data = _build_taski(slurm_elem);
		if (*resulting_elem_data)
			VERBOSE("LL_TaskGetFirstTaskInstance\n");
		else
			VERBOSE("LL_TaskGetFirstTaskInstance = NULL\n");
		break;
	case LL_TaskGetNextTaskInstance:
		/* LL_element * (TaskInstance)
		 * Always NULL for SLURM		*/
		*resulting_elem_data = NULL;
		if (*resulting_elem_data)
			VERBOSE("LL_TaskGetNextTaskInstance\n");
		else
			VERBOSE("LL_TaskGetNextTaskInstance = NULL\n");
		break;
	default:
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static slurm_elem_t *_build_taski(slurm_elem_t *slurm_elem)
{
	slurm_task_elem_t *task_data = (slurm_task_elem_t *) slurm_elem->data;
	slurm_elem_t *rc = (slurm_elem_t*) NULL;
	slurm_taski_elem_t *taski_data;

	taski_data = calloc(1, sizeof(slurm_taski_elem_t));
	if (taski_data == NULL) {
		ERROR("calloc failure\n");
		return rc;
	}
	rc = calloc(1, sizeof(slurm_elem_t));
	if (rc == NULL) {
		ERROR("calloc failure\n");
		free(taski_data);
		return rc;
	}

	rc->type	= TASK_INST_ELEM;
	rc->data	= taski_data;
	taski_data->task_elem = slurm_elem;
	taski_data->node_inx  = task_data->node_inx;
	taski_data->task_id   = task_data->task_id;

	if (task_data->taski_elem == NULL)
		task_data->taski_elem = rc;

	return rc;
}


/*
 * We get use the federation driver's fed_jobinfo_t->tables_per_task
 * as the adapter count.
 */
static int _get_adapter_count(slurm_elem_t *taski_elem)
{
	int count = 0;
	slurm_elem_t *tmp;
	slurm_taski_elem_t *taski_data;
	slurm_task_elem_t *task_data;
	slurm_node_elem_t *node_data;
	slurm_step_elem_t *step_data;
	slurm_job_init_t *job_data;
	job_step_create_response_msg_t *resp_msg;
	switch_jobinfo_t jobinfo;
	
	/* Walk through data structures to find switch credential.
	 * This is ugly, but should be pretty fast.
	 */
	taski_data = taski_elem->data;
	tmp = taski_data->task_elem;
	task_data = tmp->data;
	tmp = task_data->node_elem;
	node_data = tmp->data;
	tmp = node_data->step_elem;
	step_data = tmp->data;
	tmp = step_data->job_init_elem;
	job_data = tmp->data;
	
	if(slurm_step_ctx_get(step_data->ctx, 
			      SLURM_STEP_CTX_RESP, 
			      &resp_msg) < 0) {
		ERROR("step_ctx_get RESP: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return 0;
	}
	jobinfo = resp_msg->switch_job;
	
	if(slurm_jobinfo_ctx_get(jobinfo, FED_JOBINFO_TABLESPERTASK,
				 &count) < 0) {
		ERROR("2 jobinfo_ctx_get RESP: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return 0;
	}

	return count;
}


static int _get_data_task_inst(slurm_elem_t *slurm_elem,
			       enum LLAPI_Specification specification,
			       void *resulting_data_type)
{
	int rc = 0;
	slurm_elem_t *tmp_elem_ptr;
	slurm_taski_elem_t *taski_data = 
		(slurm_taski_elem_t *) slurm_elem->data;
	slurm_node_elem_t *node_data;
	slurm_task_elem_t *task_data;
	char **resulting_char_data = (char **) resulting_data_type;
	int *resulting_int_data = (int *) resulting_data_type;
	LL_element **resulting_elem_data = (LL_element **) resulting_data_type;
	static int adapter_idx;

	switch (specification) {
	case LL_TaskInstanceAdapterCount:
		/* int * : number of adapters		*/
		*resulting_int_data = _get_adapter_count(slurm_elem);
		/*
		 * If _get_adapter_count is 0, assume we are in ip mode
		 * FIXME - there is probably a better was to detect ip mode
		 *         than seeing that _get_adapters_count is 0.
		 */
		if (*resulting_int_data == 0)
			*resulting_int_data = 1;
		VERBOSE("LL_TaskInstanceAdapterCount = %d\n",
			*resulting_int_data);
		break;
	case LL_TaskInstanceGetFirstAdapter:
		/* LL_element * (Adapter)		*/
		*resulting_elem_data = NULL;
		VERBOSE("LL_TaskInstanceGetFirstAdapter = NULL\n");
		
		break;
	case LL_TaskInstanceGetNextAdapter:
		/* LL_element * (Adapter)		*/
		*resulting_elem_data = NULL;
		VERBOSE("LL_TaskInstanceGetNextAdapter = NULL\n");
		break;
	case LL_TaskInstanceGetFirstAdapterUsage:
		/* LL_element * (AdapterUsage)		*/
		adapter_idx = 0;
		*resulting_elem_data = _build_adapter(slurm_elem, adapter_idx);
		if (*resulting_elem_data)
			VERBOSE("LL_TaskInstanceGetFirstAdapterUsage 0\n");
		else
			VERBOSE("LL_TaskInstanceGetFirstAdapterUsage "
				"0 = NULL\n");
		break;
	case LL_TaskInstanceGetNextAdapterUsage:
		/* LL_element * (AdapterUsage)		*/
		adapter_idx++;
		*resulting_elem_data = _build_adapter(slurm_elem, adapter_idx);
		if (*resulting_elem_data)
			VERBOSE("LL_TaskInstanceGetNextAdapterUsage %d\n",
				adapter_idx);
		else
			VERBOSE("LL_TaskInstanceGetNextAdapterUsage "
				"%d = NULL\n", adapter_idx);
		break;
	case LL_TaskInstanceMachine:
		//*resulting_elem_data = _get_node_first_task(slurm_elem);
 		*resulting_elem_data = slurm_elem;	
		if (*resulting_elem_data)
			VERBOSE("LL_TaskInstanceMachine set\n");
		else
			ERROR("LL_TaskInstanceMachine = NULL\n");
		break;
	case LL_TaskInstanceMachineAddress:
		/*  char * : machine IP address 	*/

		tmp_elem_ptr = taski_data->task_elem;
		task_data = (slurm_task_elem_t *) tmp_elem_ptr->data;
		tmp_elem_ptr = task_data->node_elem;
		node_data= (slurm_node_elem_t *) tmp_elem_ptr->data;
		*resulting_char_data = node_data->node_addr;
		
		VERBOSE("LL_TaskInstanceMachineAddress = %s\n",
			*resulting_char_data ? *resulting_char_data : "NULL");
		break;
	case LL_TaskInstanceMachineName:
		/*  char * : machine assigned		*/
		tmp_elem_ptr = taski_data->task_elem;
		task_data = (slurm_task_elem_t *) tmp_elem_ptr->data;
		tmp_elem_ptr = task_data->node_elem;
		node_data= (slurm_node_elem_t *) tmp_elem_ptr->data;
		*resulting_char_data = node_data->node_name;
		VERBOSE("LL_TaskInstanceMachineName = %s\n",
			*resulting_char_data);
		break;
	case LL_TaskInstanceTaskID:
		/*  int * : task id			*/
		*resulting_int_data = taski_data->task_id;
		VERBOSE("LL_TaskInstanceTaskID = %d\n",
			*resulting_int_data);
		break;
	default:
		ERROR("ll_get_data: unsupported "
		      "spec=%d\n", specification);
		rc = -2;
	}

	return rc;
}

static char *_adapter_name_check(char *network)
{
	regex_t re;
	char *pattern = "(sni[[:digit:]])";
        size_t nmatch = 5;
        regmatch_t pmatch[5];
        char *name;

	if (regcomp(&re, pattern, REG_EXTENDED) != 0) {
                ERROR("sockname regex compilation failed\n");
                return NULL;
        }
	memset(pmatch, 0, sizeof(regmatch_t)*nmatch);
	if (regexec(&re, network, nmatch, pmatch, 0) == REG_NOMATCH) {
		return NULL;
	}
	name = strndup(network + pmatch[1].rm_so,
		       (size_t)(pmatch[1].rm_eo - pmatch[1].rm_so));
	regfree(&re);
	
	return name;
}

static void _parse_slurm_network_env(char **protocol, char **mode,
				     char **device)
{
	char *network;
	char *tmp;

	if ((network =      getenv("SLURM_NETWORK")) != NULL)
		VERBOSE("SLURM_NETWORK = \"%s\"\n", network);

	/* set defaults */
	*protocol = "MPI";
	*mode = "IP";
	*device = "en0";

	/* SLURM_NETWORK is not set, stick with defaults */
	if (network == NULL)
		return;

	if (strstr(network, "IP") || strstr(network, "ip")) {
		*mode = "IP";
		*device = "en0";
	} else if (strstr(network, "US") || strstr(network, "us")) {
		*mode = "US";
		if (strstr(network, "sn_all")
		    || strstr(network, "SN_ALL")) {
			*device = "sn_all";
		} else if (strstr(network, "sn_single")
			   || strstr(network, "SN_SINGLE")) {
			*device = "sn_single";
		} else if ((tmp = _adapter_name_check(network))) {
			*device = tmp;
		} else {
			*device = "sn_all";
		}
	}

	if (strstr(network, "MPI, LAPI") || strstr(network, "MPI,LAPI"))
		*protocol = "MPI, LAPI";
	else if (strstr(network, "LAPI, MPI") || strstr(network, "LAPI,MPI"))
		*protocol = "LAPI, MPI";
	else if (strstr(network, "LAPI"))
		*protocol = "LAPI";
	else if (strstr(network, "MPI"))
		*protocol = "MPI";
}


/* Parse the network string.  The network string parameter parsed here 
 * is in a slightly different format than from POE.  It looks like:
 *
 * network.protocol,type[,usage[,mode[,comm_level[,instances=<number|max>]]]]
 *  OR as we know them:
 * network.protocol,device,usage,mode
 *
 * Examples:
 *     network.MPI,sn_single,not_shared,US,HIGH
 *     network.MPI,sn_single,,IP
 *     network.MPI,css0,not_shared,US
 */	
static void _parse_network_string(char *network_str, char **protocol,
				  char **mode, char **device)
{
	char *orig, *state, *tok, *tmp;

	/*
	 * poe provided a network string command, so parse it
	 */
	orig = strdup(network_str);
	if (!orig) {
		ERROR("Could not copy network string ");
		goto done;
	}

	/* find protocol */
	tok = strtok_r(orig, ",", &state);
	if (!tok)
		goto done;
	tmp = strstr(tok, "network.");
	if (!tmp)
		goto done;
	*protocol = strdup(tmp + strlen("network."));
	if (!*protocol) {
		ERROR("Failed to copy protocol string ");
		return;
	}

	/* find device */
	tok = strtok_r(NULL, ",", &state);
	if (!tok)
		goto done;
	*device = strdup(tok);
	if (!*device) {
		ERROR("Failed to copy device string ");
	        goto done;
	}

	/* skip usage */
	tok = strtok_r(NULL, ",", &state);
	if (!tok)
		goto done;

	/* find mode */
	tok = strtok_r(NULL, ",", &state);
	if (!tok)
		goto done;
	*mode = strdup(tok);
	if (!*mode) {
		ERROR("Failed to copy mode string ");
		goto done;
	}

done:
	free(orig);

}


/*
 * Set protocol, mode, and device in adapter_data to the correct values
 */
static void _set_network_parameters(slurm_step_elem_t *step_data,
				    slurm_job_init_t *job_data)
{
	if (!strcmp(getenv("LOADLBATCH"), "yes")) {
		/* Batch mode */
		VERBOSE("poe is in batch mode\n");
		_parse_slurm_network_env(&step_data->protocol,
					 &step_data->mode,
					 &step_data->device);
	} else if (job_data->slurm_job_desc->network != NULL) {
		/* Interactive mode */
		VERBOSE("poe is in interactive mode\n");
		_parse_network_string(job_data->slurm_job_desc->network,
				      &step_data->protocol,
				      &step_data->mode,
				      &step_data->device);
	} else {
		/*
		 * Interactive mode, but no network string sent
		 * by poe.  Send the required defaults.
		 */
		VERBOSE("poe is in batch mode, but no network string\n");
		ERROR("Should be handled in ll_parse_string()");
		step_data->protocol = strdup("not specified");
		step_data->mode = strdup("ip");
		step_data->device = strdup("");
	}

	VERBOSE("Using protocol = \"%s\"\n", step_data->protocol);
	VERBOSE("Using mode     = \"%s\"\n", step_data->mode);
	VERBOSE("Using device   = \"%s\"\n", step_data->device);
}


static slurm_elem_t *_build_adapter(slurm_elem_t *taski_elem, int adapter_idx)
{
	slurm_elem_t *rc = (slurm_elem_t *)NULL;
	slurm_elem_t *tmp;
	slurm_adapter_elem_t *adapter_data;
	slurm_taski_elem_t *taski_data;
	slurm_task_elem_t *task_data;
	slurm_node_elem_t *node_data;
	slurm_step_elem_t *step_data;
	slurm_job_init_t *job_data;
	job_step_create_response_msg_t *resp_msg;
	ADAPTER_RESOURCES res;
	switch_jobinfo_t jobinfo;
	char *device = NULL;
	int i;
	int adapters_per_task;
	fed_tableinfo_t *tableinfo;
	NTBL *table;
	
	/* Walk through data structures to find switch credential.
	 * This is ugly, but should be pretty fast.
	 */
	taski_data = taski_elem->data;
	tmp = taski_data->task_elem;
	task_data = tmp->data;
	tmp = task_data->node_elem;
	node_data = tmp->data;
	tmp = node_data->step_elem;
	step_data = tmp->data;
	tmp = step_data->job_init_elem;
	job_data = tmp->data;
	
	if(slurm_step_ctx_get(step_data->ctx, 
			      SLURM_STEP_CTX_RESP, 
			      &resp_msg) < 0) {
		ERROR("step_ctx_get RESP: %s\n",
		      slurm_strerror(slurm_get_errno()));
		return rc;
	}
	jobinfo = resp_msg->switch_job;
	
	adapter_data = calloc(1, sizeof(slurm_adapter_elem_t));
	if (adapter_data == NULL) {
		ERROR("calloc failure\n");
		return rc;
	}
	rc = calloc(1, sizeof(slurm_elem_t));
	if (rc == NULL) {
		ERROR("calloc failure\n");
		free(adapter_data);
		return rc;
	}

	rc->type = ADAPTER_ELEM;
	rc->data = adapter_data;
	
	adapter_data->job_init = job_data;
	adapter_data->protocol = step_data->protocol;
	adapter_data->mode = step_data->mode;
	adapter_data->address = node_data->node_addr;
	adapter_data->unique_id = adapter_idx;

	if(slurm_jobinfo_ctx_get(jobinfo, FED_JOBINFO_TABLESPERTASK,
				 &adapters_per_task) < 0) {
		ERROR("jobinfo_ctx_get TABLESPERTASK: %s\n",
		      slurm_strerror(slurm_get_errno()));
		free(adapter_data);
		free(rc);
		return NULL;
	}

	if(slurm_jobinfo_ctx_get(jobinfo, FED_JOBINFO_TABLEINFO,
				 &tableinfo) < 0) {
		ERROR("jobinfo_ctx_get TABLEINFO: %s\n",
		      slurm_strerror(slurm_get_errno()));
		free(adapter_data);
		free(rc);
		return NULL;
	}

	if(!strcmp(step_data->device, "sn_all")
	   || !strcmp(step_data->device, "sn_single")
	   || !strncmp(step_data->device, "sni", 3)) {
		if(tableinfo) {
			device = tableinfo[adapter_idx].adapter_name;
			VERBOSE("device[%d] = %s\n",
				adapter_idx, device);

			adapter_data->device = strdup(device);
			
			if(adapter_data->device[2] == 'i') {
				ntbl_adapter_resources(NTBL_VERSION,
						       adapter_data->device,
						       &res);
				
				for(i=3;i<=strlen(adapter_data->device);i++)
					adapter_data->device[i-1] =
						adapter_data->device[i];
				adapter_data->device[i] = '\0';
				adapter_data->network_id = res.network_id;
			} else {
				ERROR("don't understand this "
				      "type of adapter %s\n", 
				      adapter_data->device);
				goto fail1;
			}

			table = tableinfo[adapter_idx].
				table[taski_data->task_id];
			
			adapter_data->window = table->window_id;
	
			VERBOSE("table_length = %d, "
				"task_id = %hd, lid = %hd, window_id = %hd, "
				"adapter_name = %s\n",
				tableinfo[adapter_idx].table_length,
				table->task_id, table->lid, table->window_id,
				tableinfo[adapter_idx].adapter_name);
		} else {
			ERROR("no device returned "
			      "from slurm\n");
		}
	} else if (!strncmp(adapter_data->device,"sn",2)) {
		ERROR("don't specify the sn adapter\n");
		goto fail1;
	} else {
		adapter_data->network_id = -3;
		device = step_data->device;
		adapter_data->device = strdup(device);
		VERBOSE("device = %s\n",
			device);

	}

	return rc;

fail1:
	free(adapter_data->device);
	free(adapter_data);
	free(rc);
	rc = NULL;
	return rc;

}
