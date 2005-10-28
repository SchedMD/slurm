/*****************************************************************************\
 *  Function: ll_spawn
 *
 *  Description: This function is used to start a specific task on a specific
 *  machine. A file descriptor connected to the spawnned task is returned. The
 *  caller is responsible for closing this socket. The caller must also make 
 *  sure that the task object specified has the correct executable name to be
 *  started. The name of the machine assigned to the specified task will be
 *  retrieved from the job object.
 *
 *  Arguments:
 *    IN jobmgmtObj: Pointer to the LL_element handle returned by
 *			the ll_init_job function.
 *    IN job:
 *    IN taskl:
 *    IN executable: pathname of executable to be launched
 *    RET Success: >=0, file descriptor of the socket
 *        Failure: <0,
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

extern int ll_spawn(LL_element *jobmgmtObj, LL_element *job, 
		LL_element *taskI, char *executable)
{
	slurm_elem_t *slurm_elem = (slurm_elem_t *) jobmgmtObj;
	int rc;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_spawn\n");

	if (jobmgmtObj == NULL) {
		ERROR("jobmgmtObj == NULL\n");
		rc = -1;
		goto done;
	}

	if (job == NULL) {
		ERROR("job == NULL\n");
		rc = -2;
		goto done;
	}

	if (taskI == NULL) {
		ERROR("taskI == NULL\n");
		rc = -3;
		goto done;
	}

	if (slurm_elem->type != JOB_INIT) {
		ERROR("invalid elem type = %s\n",
			elem_name(slurm_elem->type));
		rc = -1;
		goto done;
	}

	VERBOSE("executable = %s\n", executable);
	/* FIXME: Need to return file descriptor */
	rc = -1;

done:
	VERBOSE("--------------------------------------------------\n");
	return rc;
}
