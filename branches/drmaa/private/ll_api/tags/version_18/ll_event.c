/*****************************************************************************\
 *  Function:  ll_event
 *
 *  Description: This function will read and/or select on the listen socket
 *  created by the ll_init_job call. If the listen socket of the jobmgmtObj
 *  is not ready to read, this function will do a select and wait. Any
 *  interactive processes should monitor this socket at all times for event
 *  notification from LoadLeveler. This function returns a pointer to the job
 *  that was updated by the transaction and a list of step within the that job
 *  that had a status change.
 *
 *  Arguments:
 *    IN jobmgmtObj : Pointer to the LL_element handle returned by the
 *    			ll_init_job function.
 *    IN int msecs : Milliseconds to wait for event before timeout.
 *    OUT job : Pointer to the LL_element handle representing the job
 *    			that had an event update.
 *    OUT steplist : Array of stepids representing which steps within
 *    			the job had a status update.
 *    RET STATUS_EVENT : Job was returned with updated status.
 *        TIMER_EVENT :  Timer popped before any event occured.
 *        ERROR_EVENT :  Error occured.
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

#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

#define MAX_DELAY 30

static void _job_poll(slurm_job_init_t *job_data);

extern enum EventType ll_event(LL_element *jobmgmtObj, int msec, 
		LL_element **job, LL_element *steplist)
{
	enum EventType rc = ERROR_EVENT;
	slurm_elem_t *slurm_elem = (slurm_elem_t *) jobmgmtObj;
	slurm_job_init_t *job_data;
	int delay = 1;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_event\n");

	if (jobmgmtObj == NULL) {
		ERROR("jobmgmtObj == NULL\n");
		rc = ERROR_EVENT;
		goto done;
	}
	if (job == NULL) {
		ERROR("job == NULL\n");
		rc = ERROR_EVENT;
		goto done;
	}
	if (steplist == NULL) {
		ERROR("steplist == NULL\n");
		rc = ERROR_EVENT;
		goto done;
	}
	if (slurm_elem->type != JOB_INIT) {
		ERROR("invalid elem type = %s\n",
			elem_name(slurm_elem->type));
		rc = ERROR_EVENT;
		goto done;
	}

	job_data = (slurm_job_init_t *)slurm_elem->data;
	while (job_data->job_state < JOB_RUNNING) {
		sleep(delay);
		_job_poll(job_data);
		delay *= 2;
		if (delay > MAX_DELAY)
			delay = MAX_DELAY;
	}
	*job = jobmgmtObj;
	rc = STATUS_EVENT;
	
done:
	VERBOSE("--------------------------------------------------\n");
	return rc;
}

/* Update state info for selected job */
static void _job_poll(slurm_job_init_t *job_data)
{
	old_job_alloc_msg_t job_desc_msg;
	resource_allocation_response_msg_t *alloc_resp_msg;

	job_desc_msg.job_id = job_data->job_alloc_resp->job_id;

	if (slurm_confirm_allocation(&job_desc_msg, &alloc_resp_msg) < 0) {
		int err = slurm_get_errno();
		ERROR("slurm_confirm_allocation: %s\n", 
			slurm_strerror(err));
		if (err == ESLURM_ALREADY_DONE)
			job_data->job_state = JOB_COMPLETE;
	} else {
		if (alloc_resp_msg->node_list)
			job_data->job_state = JOB_RUNNING;
		slurm_free_resource_allocation_response_msg(
			job_data->job_alloc_resp);
		job_data->job_alloc_resp = alloc_resp_msg;
	}
}
