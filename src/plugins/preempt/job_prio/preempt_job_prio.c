/*****************************************************************************\
 *  preempt_job_prio.c
 *
 *  DESCRIPTION: This plugin enables the selection of preemptable jobs based
 *  upon their priority, the amount resources used under an account
 *  (optionally), the runtime of the job and its account (i.e. accounts not
 *  finishing with _p can be preempted...)
 *
 *  OPTIONS: The following constants can be set to modify the plugin's behavior:
 *
 *  CHECK_FOR_PREEMPTOR_OVERALLOC: If set to 1, overallocation of the
 *  preemptor's account will prevent preemption for the benefit of that job.
 *  E.g. if running this jobs will create an overallocation of an account, the
 *  preemptees creating this situation will be removed for the preemption
 *  candidates.
 *
 *  CHECK_FOR_ACCOUNT_UNDERALLOC: If set to 1, underallocation of a preemptee's
 *  account will prevents its preemption. E.g. if preempting a job reduces the
 *  usage of its account below its allocated share, it will be removed from the
 *  candidates.
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris jette <jette1@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <math.h>
#include <stdio.h>

#include "slurm/slurm_errno.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/plugin.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_priority.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"


/* If the #define options listed below for CHECK_FOR_PREEMPTOR_OVERALLOC and
 * CHECK_FOR_ACCOUNT_UNDERALLOC are commented out, this plugin will work as a
 * simple job priority based preemption plugin. */
#define CHECK_FOR_PREEMPTOR_OVERALLOC 0
#define CHECK_FOR_ACCOUNT_UNDERALLOC  0

const char  plugin_name[]   = "Preempt by Job Priority and Runtime";
const char  plugin_type[]   = "preempt/job_prio";
const uint32_t  plugin_version  = 100;

static bool	_job_prio_preemptable(struct job_record *preemptor,
				      struct job_record *preemptee);

/*****End of plugin specific declarations**********************************/

/**************************************************************************/
/*  TAG(                              init                              ) */
/**************************************************************************/
extern int init( void )
{
	int rc = SLURM_SUCCESS;
	char *prio_type = slurm_get_priority_type();

	if (strncasecmp(prio_type, "priority/multifactor", 20)) {
		error("The priority plugin (%s) is currently loaded. "
		      "This is NOT compatible with the %s plugin. "
		      "The priority/multifactor plugin must be used",
		      prio_type, plugin_type);
		rc = SLURM_FAILURE;
	}

	xfree(prio_type);
	verbose("%s loaded", plugin_type);
	return rc;
}

/**************************************************************************/
/*  TAG(                              fini                              ) */
/**************************************************************************/
extern void fini( void )
{
	/* Empty. */
}

/**************************************************************************/
/* TAG(                 find_preemptable_jobs                           ) */
/**************************************************************************/
extern List find_preemptable_jobs(struct job_record *job_ptr)
{
	ListIterator preemptee_candidate_iterator;
	struct job_record *preemptee_job_ptr;
	struct job_record *preemptor_job_ptr = job_ptr;
	List preemptee_job_list = NULL;

	/* Validate the preemptor job */
	if (preemptor_job_ptr == NULL) {
		error("%s: preemptor_job_ptr is NULL", plugin_type);
		return preemptee_job_list;
	}
	if (!IS_JOB_PENDING(preemptor_job_ptr)) {
		error("%s: job %u not pending",
		      plugin_type, preemptor_job_ptr->job_id);
		return preemptee_job_list;
	}
	if (preemptor_job_ptr->part_ptr == NULL) {
		error("%s: job %u has NULL partition ptr",
		      plugin_type, preemptor_job_ptr->job_id);
		return preemptee_job_list;
	}
	if (preemptor_job_ptr->part_ptr->node_bitmap == NULL) {
		error("%s: partition %s node_bitmap==NULL",
		      plugin_type, preemptor_job_ptr->part_ptr->name);
		return preemptee_job_list;
	}

	if (slurm_get_debug_flags() & DEBUG_FLAG_PRIO) {
		info("%s: Looking for jobs to preempt for job %u",
		    plugin_type, preemptor_job_ptr->job_id);
	}

	/* Build an array of pointers to preemption candidates */
	preemptee_candidate_iterator = list_iterator_create(job_list);
	while ((preemptee_job_ptr = (struct job_record *)
				    list_next(preemptee_candidate_iterator))) {
		if (!IS_JOB_RUNNING(preemptee_job_ptr) &&
		    !IS_JOB_SUSPENDED(preemptee_job_ptr))
			continue;
		if (!_job_prio_preemptable(preemptor_job_ptr, preemptee_job_ptr))
			continue;
		if ((preemptee_job_ptr->node_bitmap == NULL) ||
		   (bit_overlap(preemptee_job_ptr->node_bitmap,
				preemptor_job_ptr->part_ptr->node_bitmap) == 0))
			continue;
		if (preemptor_job_ptr->details &&
		    (preemptor_job_ptr->details->expanding_jobid ==
		     preemptee_job_ptr->job_id))
			continue;

		/* This job is a valid preemption candidate and should be added
		 * to the list. Create the list as needed. */
		if (preemptee_job_list == NULL)
			preemptee_job_list = list_create(NULL);
		list_append(preemptee_job_list, preemptee_job_ptr);
	}
	list_iterator_destroy(preemptee_candidate_iterator);

	if ((preemptee_job_list == NULL) &&
	    (slurm_get_debug_flags() & DEBUG_FLAG_PRIO)) {
    		info("NULL preemptee list for job (%u) %s",
		     preemptor_job_ptr->job_id, preemptor_job_ptr->name);
	}

	return preemptee_job_list;
}

/*
 *  Return true if the preemptor can preempt the preemptee, otherwise false
 * */

static bool _job_prio_preemptable(struct job_record *preemptor,
				  struct job_record *preemptee)
{
	uint32_t job_prio1, job_prio2;
	
	job_prio1 = preemptor->priority;
	job_prio2 = preemptee->priority;

	if (job_prio2 >= job_prio1) {
		return false;	/* Preemptor can not preempt */
	} else {
		return true;	/* Preemptor can preempt */
	}
}

/**************************************************************************/
/* TAG(                 job_preempt_mode                                ) */
/**************************************************************************/
extern uint16_t job_preempt_mode(struct job_record *job_ptr)
{
	uint16_t mode;

	if (job_ptr->qos_ptr &&
	   ((slurmdb_qos_rec_t *)job_ptr->qos_ptr)->preempt_mode) {
		mode = ((slurmdb_qos_rec_t *)job_ptr->qos_ptr)->preempt_mode;
		if (slurm_get_debug_flags() & DEBUG_FLAG_PRIO) {
			info("%s: in job_preempt_mode return = %s",
			     plugin_type, preempt_mode_string(mode));
		}
		return mode;
	}

	mode = slurm_get_preempt_mode() & (~PREEMPT_MODE_GANG);
	if (slurm_get_debug_flags() & DEBUG_FLAG_PRIO) {
		info("%s: in job_preempt_mode return = %s",
		     plugin_type, preempt_mode_string(mode));
	}
	return mode;
}

/**************************************************************************/
/* TAG(                 preemption_enabled                              ) */
/**************************************************************************/
extern bool preemption_enabled(void)
{
	return (slurm_get_preempt_mode() != PREEMPT_MODE_OFF);
}

/***************************************************************************/
/* Return true if the preemptor can preempt the preemptee, otherwise false */
/***************************************************************************/
extern bool job_preempt_check(job_queue_rec_t *preemptor,
			      job_queue_rec_t *preemptee)
{
	return _job_prio_preemptable(preemptor->job_ptr, preemptee->job_ptr);
}
