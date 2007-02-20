/*****************************************************************************\
 *  Function: ll_set_data
 *
 *  Description: This function will update the dataObject with the specified
 *  data. The field to be updated within the object is referenced by
 *  the dataField.
 *
 *  Arguments:
 *    IN dataObject : Pointer to the object to be updated.
 *    IN dataField : Enum which references the data field within the
 *                   object to be updated.
 *    IN data : Pointer to the data to be stored within the object.
 *    RET Success: 0
 *        Failure: -1 : invalid dataObject or data.
 *        
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

static int _set_job_data(slurm_elem_t * slurm_elem, 
		enum LLAPI_Specification specification, void *data);
static int _set_step_data(slurm_elem_t * slurm_elem,
		enum LLAPI_Specification specification, void *data);

extern int ll_set_data(LL_element *dataObject, 
		enum LLAPI_Specification specification, 
		void *data)
{
	slurm_elem_t * slurm_elem = (LL_element *) dataObject;
	int rc = 0;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_set_data\n");
	VERBOSE("LLAPI_Specification: %d\n", specification);

	if (dataObject == NULL) {
		ERROR("dataObject==NULL, LLAPI_Specification = %d\n",
		      specification);
		rc = -1;
		goto done;
	}

	VERBOSE("data type:%s\n", elem_name(slurm_elem->type));
	switch  (slurm_elem->type) {
		case JOB_INIT:
			rc = _set_job_data(slurm_elem, specification, data);
			break;
		case STEP_ELEM:
			rc = _set_step_data(slurm_elem, specification, data);
			break;
		default:
			ERROR("type invalid\n");
			rc = -1;
	}

done:
	VERBOSE("--------------------------------------------------\n");
	return rc;
}

static int _set_job_data(slurm_elem_t * slurm_elem, 
		enum LLAPI_Specification specification, void *data)
{
	int rc = 0;
	int ll_val_int = (int) data;
	slurm_job_init_t * slurm_job_elem;

	slurm_job_elem = (slurm_job_init_t *) slurm_elem->data;
	switch(specification) {
		case LL_JobManagementPrinterFILE:
			VERBOSE("LL_JobManagementPrinterFILE: NO-OP\n");
			break;
		case LL_JobManagementRestorePrinter:
			VERBOSE("LL_JobManagementRestorePrinter: NO-OP\n");
			break;
		case LL_JobManagementSessionType:
			/* A poerestart command on a checkpointed interactive
			 * job will have SLURM_JOBID set and fail the below
			 * test. Removing the test will result in possible
			 * failure modes for regular poe jobs. Presently, 
			 * poerestart of checkpointed interactive jobs will 
			 * fail. Note that to even reach this point, one 
			 * must set MP_HOSTFILE appropriately. We also need 
			 * to worry about re-creating the SLURM jobid. */
			if (getenv("SLURM_JOBID") != NULL) {
				char *batch_flag = getenv("LOADLBATCH");
				ERROR("Resource allocation "
					"already exists\n");
				if (batch_flag &&
				    (strcmp(batch_flag, "yes") == 0))
					ERROR("POE failed to note "
					      "LOADLBATCH == yes\n");
				else
					ERROR("LOADLBATCH != yes\n");
				rc = -1;
				break;
			}
			if (slurm_job_elem == NULL) {
				ERROR("slurm_job_elem == NULL\n");
				rc = -1;
				break;
			}
			/* Note: Don't de-reference the data value */
			slurm_job_elem->session_type = ll_val_int;
			VERBOSE("LL_JobManagementSessionType = ");
			poe_session = ll_val_int;
			switch (ll_val_int) {
				case BATCH_SESSION:
					VERBOSE("BATCH_SESSION\n");
					rc = 0;
					break;
				case INTERACTIVE_SESSION:
					VERBOSE("INTERACTIVE_SESSION\n");
					rc = 0;
					break;
				case INTERACTIVE_HOSTLIST_SESSION:
					VERBOSE("INTERACTIVE_HOSTLIST_SESSION\n");
					/*  ERROR("SLURM does not currently support" */
/*  					      " poe hostfiles.\n"); */
					rc = 0;
					break;
				default:
					VERBOSE("%d\n", ll_val_int);
					ERROR("Invalid poe session type %d\n",
					      ll_val_int);
					rc = -1;
			}
			break;;
		default:
			ERROR("LLAPI_Specification invalid\n");
			rc = -1;
	}

	return rc;
}

static int _set_step_data(slurm_elem_t * slurm_elem,
		enum LLAPI_Specification specification, void *data)
{
	int rc = 0;
	int ll_val_int = (int) data;
	slurm_step_elem_t * slurm_step_data;
	slurm_job_init_t * slurm_job_init_data;

	slurm_step_data = (slurm_step_elem_t *) slurm_elem->data;
	if (slurm_step_data == NULL) {
		ERROR("slurm_step_data == NULL\n");
		return -1;
	}
	/* NOTE: Value is not de-referenced */
	slurm_job_init_data = (slurm_job_init_t *) 
		slurm_step_data->job_init_elem->data;

	switch(specification) {
		case LL_StepImmediate:
			slurm_job_init_data->slurm_job_desc->immediate = 
				ll_val_int;
			VERBOSE("LL_StepImmediate = %d\n",
				ll_val_int);
			rc = 0;
			break;
		case LL_StepHostName:
			if (hostlist_push_host(slurm_job_init_data->host_list,
				       (char *)data) == 0) {
				ERROR("malloc error adding LL_StepHostName "
					"= %s\n", (char *)data);
				rc = -1;
			} else {
				VERBOSE("LL_StepHostName = %s\n", (char *)data);
				rc = 0;
			}
			break;
		default:
			ERROR("LLAPI_Specification invalid\n");
			rc = -1;
	}
	return rc;
}
