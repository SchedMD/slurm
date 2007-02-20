/*****************************************************************************\
 *  Function:  ll_request
 *
 *  Description: This function is used to request resources for the execution
 *  of a job.
 *
 *  Arguments:
 *    IN jobmgmtObj: Pointer to the LL_element handle returned by
 *    			 the ll_init_job function.
 *    IN job: Pointer to the LL_element representing the job to submit.
 *    RET Success: 0
 *        Failure: -1: Invalid jobmgmtObj.
 *                 -2: Invalid job object handle.
 *                 -3: Cannot connect to Schedd.
 *                 -4: Cannot issue request as root user.
 *                 -5: System error.
 *                 -7: hostlist expansion error.
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

#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

extern int ll_request(LL_element *jobmgmtObj, LL_element *job)
{
	int rc = 0;
	slurm_elem_t *slurm_elem = (slurm_elem_t *) jobmgmtObj;
	slurm_job_init_t *slurm_job_init_ptr;
	job_desc_msg_t * slurm_job_desc_ptr;
	resource_allocation_response_msg_t * job_alloc_resp;

	VERBOSE(stderr, "++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE(stderr, "ll_request\n");

	if (jobmgmtObj == NULL) {
		ERROR(stderr, "ERROR: jobmgmtObj == NULL\n");
		rc = -1;
		goto done;
	}

	if (job == NULL) {
		ERROR(stderr, "ERROR: job == NULL\n");
		rc = -2;
		goto done;
	}

	if (slurm_elem->type != JOB_INIT) {
		ERROR(stderr, "ERROR: invalid elem type = %s\n",
			elem_name(slurm_elem->type));
		rc = -1;
		goto done;
	}

	slurm_job_init_ptr = (slurm_job_init_t *) slurm_elem->data;
	slurm_job_desc_ptr = slurm_job_init_ptr->slurm_job_desc;
	if (slurm_allocate_resources(slurm_job_desc_ptr, &job_alloc_resp) < 0) {
		if (slurm_job_init_ptr->messages)
			free(slurm_job_init_ptr->messages);
		slurm_job_init_ptr->messages = strdup(slurm_strerror(
					slurm_get_errno()));
		ERROR(stderr, "ERROR: slurm_allocate_resources: %s\n",
			slurm_job_init_ptr->messages);
		slurm_job_init_ptr->job_state = JOB_FAILED;
		rc = -5;
		goto done;
	}
	slurm_job_init_ptr->job_alloc_resp = job_alloc_resp;
	if (job_alloc_resp->node_list && strlen(job_alloc_resp->node_list))
		slurm_job_init_ptr->job_state = JOB_RUNNING;
	else {
		if (slurm_job_init_ptr->messages)
			free(slurm_job_init_ptr->messages);
		slurm_job_init_ptr->messages = strdup("Waiting for resources");
		slurm_job_init_ptr->job_state = JOB_PENDING;
	}
	VERBOSE (stderr, "slurm job %u allocated nodes %s\n",
		job_alloc_resp->job_id, job_alloc_resp->node_list);

done:
	VERBOSE(stderr, "--------------------------------------------------\n");
	return 0;
}
