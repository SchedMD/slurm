/*****************************************************************************\
 *  Function:  ll_ckpt_complete
 *
 *  Description: Used to provide LoadLeveler with information about
 *  the success or failure of a checkpoint.
 *
 *  Arguments:
 *    IN jobmgmtObj : pointer to the JobManagement object which was allocated
 *    			in the ll_job_init function.
 *    IN ckpt_retcode : value returned by the checkpnt() system call
 *    OUT cp_error_data : error information structure set by AIX 
 *    			checkpnt() operation. (NULL if successful)
 *    OUT ckpt_start_time : time checkpoint operation began.
 *    			(0 if unknown)
 *    IN stepNumber : put in place for potential future enhancement.
 *    			For now the stepNo is Always 0.
 *    RET Success:  positive value indicating checkpoint end time.
 *        Failure:  NULL
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
#include <time.h>
#include <sys/checkpnt.h>

static inline uint32_t _fetch_jobid(LL_element *jobmgmtObj)
{
	slurm_job_init_t *job_info = (slurm_job_init_t *) jobmgmtObj;
	return job_info->job_alloc_resp->job_id;
}

extern time_t ll_ckpt_complete(LL_element *jobmgmtObj, int ckpt_retcode,
			cr_error_t *cp_error_data, time_t ckpt_start_time, 
			int stepNumber)
{
	int rc;
	uint32_t job_id, step_id, error_code;
	char *error_msg_ptr = NULL, error_msg[256];
	time_t ret_time;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_ckpt_complete\n");

	job_id = _fetch_jobid(jobmgmtObj);
	step_id = NO_VAL;
	if (ckpt_retcode == 0)
		error_code = 0;
	else if (ckpt_retcode < 0)
		error_code = (uint32_t) (-ckpt_retcode);
	else	/* (ckpt_retcode > 0) */
		error_code = (uint32_t) ckpt_retcode;

	if (error_code) {
		/* assemble a useful message */
		snprintf(error_msg, sizeof(error_msg), 
			"Py_error:%d Sy_error:%d Xtnd_error:%d epid:%d data:%s",
			cp_error_data->Py_error, cp_error_data->Sy_error, 
			cp_error_data->Xtnd_error, cp_error_data->epid, 
			cp_error_data->error_data);
		error_msg_ptr = error_msg;
	}

	rc = slurm_checkpoint_complete(job_id, step_id, ckpt_start_time,
		error_code, error_msg);

	if (rc != SLURM_SUCCESS) {
		ERROR("slurm_checkpoint_complete: %s\n",
			slurm_strerror(slurm_get_errno()));
		ret_time = (time_t) NULL;
	} else
		ret_time = time(NULL);
	VERBOSE("--------------------------------------------------\n");
	return ret_time;
}
