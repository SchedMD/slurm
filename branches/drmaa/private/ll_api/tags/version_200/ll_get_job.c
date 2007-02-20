/*****************************************************************************\
 *  ll_get_job.c - For a pre-existing resource allocation, return a 
 *  job object.
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STRING_H
#    include <string.h>
#  endif
#  if HAVE_PTHREAD_H
#    include <pthread.h>
#  endif
#else                /* !HAVE_CONFIG_H */
#  include <string.h>
#  include <pthread.h>
#endif                /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h> 
#include <sys/select.h> 
#include <sys/types.h>

#include "common.h"
#include "config.h"
#include "llapi.h"

static int _build_job_obj(slurm_job_init_t *job_data);

extern int ll_get_job(LL_element *mgmt_obj, LL_element **job)
{
	int rc = 0;
	slurm_elem_t *job_elem = (slurm_elem_t *) mgmt_obj;

	VERBOSE("++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	VERBOSE("ll_get_job\n");
	/* validate arguments */
	if (mgmt_obj == NULL) {
		ERROR("mgmt_obj is NULL\n");
		rc = -1;
		goto done;
	}
	if (job_elem->type != JOB_INIT) {
		ERROR("invalid elem type = %s\n",
			elem_name(job_elem->type));
		rc = -1;
		goto done;
	}
	if (job == NULL) {
		ERROR("job is NULL\n");
		rc = -1;
		goto done;
	}

	if ((rc = _build_job_obj((slurm_job_init_t *) job_elem->data)) < 0)
		*job = NULL;
	else
		*job = mgmt_obj;

done:
	VERBOSE("--------------------------------------------------\n");
	return rc;
}


static int _build_job_obj(slurm_job_init_t *job_data)
{
	char *jobid_char = getenv("SLURM_JOBID");
	char *task_dist, *tasks_char;
	long jobid_int, tasks_int;
	resource_allocation_response_msg_t *alloc_resp_msg;

	job_data->user_rcontext_blocks = 0; /* initialize */

	if (jobid_char == NULL) {
		ERROR("SLURM_JOBID environment variable missing\n");
		return -2;
	}
	jobid_int = atol(jobid_char);
	if (jobid_int <= 0) {
		ERROR("Invalid SLURM_JOBID: %s\n", jobid_char);
		return -2;
	}

	/* Get allocation details for this job */
	if (slurm_allocation_lookup_lite((uint32_t)jobid_int,
					 &alloc_resp_msg) < 0) {
		int err = slurm_get_errno();
		ERROR("slurm_confirm_allocation(%ld): %s\n", jobid_int, 
			slurm_strerror(err));
		if (job_data->messages)
			free(job_data->messages);
		if (err == ESLURM_ALREADY_DONE) {
			job_data->job_state = JOB_COMPLETE;
			job_data->messages = strdup("Job already complete");
		} else
			job_data->messages = 
				strdup("Error getting info from SLURM");
		return -2;
	}

	/* Fill in job data structure with available details */
	job_data->session_type = BATCH_SESSION;

	if (alloc_resp_msg->node_list)
		job_data->job_state = JOB_RUNNING;

	slurm_free_resource_allocation_response_msg(
		job_data->job_alloc_resp);
	job_data->job_alloc_resp = alloc_resp_msg;

	task_dist = getenv("SLURM_DISTRIBUTION");
	if (task_dist && (strcmp(task_dist, "cyclic") == 0))
		job_data->task_dist = SLURM_DIST_CYCLIC;
	/* else, leave as default, BLOCK */

	tasks_char = getenv("SLURM_NPROCS");
	if (tasks_char && ((tasks_int = atol(tasks_char)) > 0))
		job_data->task_cnt = (uint32_t) tasks_int;
	else {
		/* one task per node */
		job_data->task_cnt = alloc_resp_msg->node_cnt;
	}
	return 0;
}

/* Message thread stuff */


