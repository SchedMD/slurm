/*****************************************************************************\
 *  Function:  ll_local_ckpt_start
 *  
 *  Description: This function is called by the PMD process before initiating
 *  a checkpoint of its tasks to inform LoadLeveler that a local checkpoint
 *  is about to start on the node. Call send_local_ckpt to send a transaction
 *  to the Starter to request a checkpoint be allowed to take place.
 *
 *  Arguments:
 *    OUT *ckpt_start_time: time_t structure which will be filled in to
 *  				indicate the start time of the checkpoint.
 *  				This time will be used by both the calling
 *  				application and the starter to identify the
 *  				checkpoint when done.
 *    RET CKPT_YES  - OK to continue with checkpoint
 *        CKPT_NO   - NOT ok to continue with checkpoint
 *        CKPT_FAIL - A problem occurred and the transaction to the Starter 
 *			could not be sent or response received.
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

#include "common.h"
#include "config.h"
#include "llapi.h"

#include <stdlib.h>


extern enum CkptStart ll_local_ckpt_start(time_t *ckpt_start_time)
{
	uint32_t job_id, step_id;
	char *slurm_jobid;
	enum CkptStart rc = CKPT_YES;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_local_ckpt_start\n");

	slurm_jobid = getenv("SLURM_JOBID");
	if (slurm_jobid == NULL) {
		ERROR("SLURM_JOBID is NULL\n");
		rc = CKPT_FAIL;
		goto done;
	}
	job_id = (uint32_t) atol(slurm_jobid);


	slurm_jobid = getenv("SLURM_STEPID");
	if (slurm_jobid == NULL) {
		ERROR("SLURM_STEPID is NULL, using ALL\n");
		step_id = NO_VAL;
	} else
		step_id = (uint32_t) atol(slurm_jobid);

	if (slurm_checkpoint_able(job_id, step_id, ckpt_start_time) 
			!= SLURM_SUCCESS) {
		ERROR("slurm_checkpoint_able error\n");
		rc = CKPT_FAIL;
	}

    done:
	VERBOSE("--------------------------------------------------\n");
	return CKPT_YES;
}
