/*****************************************************************************\
 * Function:  ll_close
 *
 * Description: This function is used to indicate that the caller is
 * done with the jobs related to the specified LL_element.
 *
 * Arguments:
 *   IN jobmgmtObj: Pointer to the LL_element handle returned by
 * 			the ll_init_job function.
 *   RET Success: 0
 *       Failure: -1 : Invalid jobmgmtObj was specified.
 *                -3: Event popped without a job update.
 *                -5: System Error.
 *                -6: Abnormal termination of job, LoadLevler messages
 *                    available in job steps.
 ******************************************************************************
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

extern int ll_close(LL_element *jobmgmtObj) 
{
	int rc = 0;
	slurm_elem_t *slurm_elem = (slurm_elem_t *) jobmgmtObj;
	slurm_job_init_t *slurm_job_init_ptr;
	resource_allocation_response_msg_t * job_alloc_resp;
	slurm_step_elem_t *slurm_step_elem;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_close\n");

	if (jobmgmtObj == NULL) {
		ERROR("jobmgmtObj == NULL\n");
		rc = -1;
		goto complete;
	}

	if (slurm_elem->type != JOB_INIT) {
		ERROR("invalid elem type = %s\n",
			elem_name(slurm_elem->type));
		rc = -1;
		goto complete;
	}
	slurm_job_init_ptr = (slurm_job_init_t *) slurm_elem->data;

	job_alloc_resp = slurm_job_init_ptr->job_alloc_resp;
	if (job_alloc_resp == NULL) {
		VERBOSE("no resource allocation was made\n");
		goto complete;
	}

	if (!strcmp(getenv("LOADLBATCH"), "yes")) {
		uint32_t step_id;

		slurm_step_elem = (slurm_step_elem_t *)
			slurm_job_init_ptr->first_step_elem->data;
		if (slurm_step_elem == NULL || slurm_step_elem->ctx == NULL) {
			VERBOSE("no step allocation was made\n");
			goto complete;
		}
		slurm_step_ctx_get(slurm_step_elem->ctx, 
				   SLURM_STEP_CTX_STEPID, &step_id);

		VERBOSE("cancelling slurm job step %u.%u\n",
			job_alloc_resp->job_id, step_id);

		if (slurm_terminate_job_step(job_alloc_resp->job_id, step_id)
		    == -1) {
			ERROR("slurm_terminate_job_step: %s\n",
			      slurm_strerror(slurm_get_errno()));
			rc = -5;
			goto complete;
		}
	} else {
		VERBOSE("cancelling slurm job %u\n",
			 job_alloc_resp->job_id);

		if (slurm_complete_job(job_alloc_resp->job_id, 0) < 0) {
			ERROR("slurm_complete_job: %s\n", 
			      slurm_strerror(slurm_get_errno()));
			rc = -5;
			goto complete;
		}
	}
complete:
	VERBOSE("--------------------------------------------------\n");
	return rc;
}
