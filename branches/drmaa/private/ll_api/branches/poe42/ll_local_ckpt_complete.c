/*****************************************************************************\
 *  Function:  ll_local_ckpt_complete
 *
 *  Description: This function is called by the PMD process after checkpoint
 *  of its tasks has completed to inform LoadLeveler that a local checkpoint 
 *  has completed. This function sends a transaction to the local Startd
 *  daemon indicating the id of the job step that was checkpointed.
 *
 *  Arguments:
 *    OUT ckpt_rc : return code from the checkpnt command.
 *    OUT ckpt_start_time : Time the checkpoint operation began.
 *    			(0 if unknown)
 *    IN terminate : 0 : to continue after transactton is sent
 *     	             1 : job is to be terminated after checkpoint
 *    RET Success: time_t indicating checkpoint end time.
 *        Failure: NULL
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

extern time_t ll_local_ckpt_complete(int ckpt_rc, time_t ckpt_start_time, 
		int terminate)
{
	VERBOSE(stderr, "ll_local_ckpt_complete\n");
	return (time_t) 0;
}
