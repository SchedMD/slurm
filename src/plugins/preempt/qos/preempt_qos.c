/*****************************************************************************\
 *  preempt_qos.c - job preemption plugin that selects preemptable 
 *  jobs based upon their Quality Of Service (QOS).
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <stdio.h>
#include <slurm/slurm_errno.h>

#include "src/common/bitstring.h"
#include "src/common/log.h"
#include "src/common/plugin.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/slurmctld/slurmctld.h"

const char	plugin_name[]	= "Preempt by Quality Of Service (QOS)";
const char	plugin_type[]	= "preempt/qos";
const uint32_t	plugin_version	= 100;

static bool _qos_preemptable(struct job_record *preemptee, 
			     struct job_record *preemptor);
static void _sort_pre_job_list(struct job_record **pre_job_p, 
			       int pre_job_inx);

/**************************************************************************/
/*  TAG(                              init                              ) */
/**************************************************************************/
extern int init( void )
{
	verbose("preempt/qos loaded");
	return SLURM_SUCCESS;
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
extern struct job_record **find_preemptable_jobs(struct job_record *job_ptr)
{
	ListIterator job_iterator;
	struct job_record *job_p, **pre_job_p = NULL;
	int pre_job_inx = 0, pre_job_size = 0;

	/* Validate the preemptor job */
	if (job_ptr == NULL) {
		error("find_preemptable_jobs: job_ptr is NULL");
		return NULL;
	}
	if (!IS_JOB_PENDING(job_ptr)) {
		error("find_preemptable_jobs: job %u not pending", 
		      job_ptr->job_id);
		return NULL;
	}
	if (job_ptr->part_ptr == NULL) {
		error("find_preemptable_jobs: job %u has NULL partition ptr", 
		      job_ptr->job_id);
		return NULL;
	}
	if (job_ptr->part_ptr->node_bitmap == NULL) {
		error("find_preemptable_jobs: partition %s node_bitmap=NULL", 
		      job_ptr->part_ptr->name);
		return NULL;
	}

	/* Build an array of pointers to preemption candidates */
	job_iterator = list_iterator_create(job_list);
	while ((job_p = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_p) && !IS_JOB_SUSPENDED(job_p))
			continue;
		if (!_qos_preemptable(job_p, job_ptr))
			continue;
		if ((job_p->node_bitmap == NULL) ||
		    (bit_overlap(job_p->node_bitmap, 
				 job_ptr->part_ptr->node_bitmap) == 0))
			continue;
		/* This job is a preemption candidate */
		if (pre_job_inx >= pre_job_size) {
			pre_job_size += 100;
			xrealloc(pre_job_p, 
				 (sizeof(struct job_record *) * pre_job_size));
		}
		pre_job_p[pre_job_inx++] = job_p;
	}
	list_iterator_destroy(job_iterator);

	if (pre_job_inx <= 1)
		return pre_job_p;

	_sort_pre_job_list(pre_job_p, pre_job_inx);
	if (pre_job_inx == pre_job_size) {	/* Insure NULL terminated */
		pre_job_size++;
		xrealloc(pre_job_p, 
			 (sizeof(struct job_record * ) * pre_job_size));
	}
	return pre_job_p;
}

static bool _qos_preemptable(struct job_record *preemptee, 
			     struct job_record *preemptor)
{
	acct_qos_rec_t *qos_ee = preemptee->qos_ptr;
	acct_qos_rec_t *qos_or = preemptee->qos_ptr;

	if ((qos_ee == NULL) || (qos_or == NULL) ||
	    (qos_or->preempt_bitstr == NULL)     ||
	    (!bit_test(qos_or->preempt_bitstr, qos_ee->id)))
		return false;
	return true;

}

/* Sort a list of jobs, lowest priority jobs are first */
static void _sort_pre_job_list(struct job_record **pre_job_p, 
			       int pre_job_inx)
{
	int i, j;
	struct job_record *tmp_job_ptr;
	uint32_t tmp_job_prio;
	uint32_t *job_prio = xmalloc(sizeof(uint32_t) * pre_job_inx);

	/* for each job, compute a priority value
	 * (qos_priority << 16) + job_node_count
	 *
	 * alternate algorithms could base job priority upon run time
	 * or other factors */
	for (i=0; i<pre_job_inx; i++) {
		acct_qos_rec_t *qos_ee = pre_job_p[i]->qos_ptr;
		if (qos_ee)
			job_prio[i] = (qos_ee->priority & 0xffff) << 16;
		else
			job_prio[i] = 0;

		if (pre_job_p[i]->node_cnt >= 0xffff)
			job_prio[i] += 0xffff;
		else
			job_prio[i] +=  pre_job_p[i]->node_cnt;
	}

	/* sort the list, lower priority first */
	for (i=0; i<pre_job_inx; i++) {
		for (j=(i+1); j<pre_job_inx; j++) {
			if (job_prio[i] <= job_prio[j])
				continue;
			/* swap the records */
			tmp_job_prio = job_prio[i];
			job_prio[i]  = job_prio[j];
			job_prio[j]  = tmp_job_prio;
			tmp_job_ptr  = pre_job_p[i];
			pre_job_p[i] = pre_job_p[j];
			pre_job_p[j] = tmp_job_ptr;
		}
	}
	xfree(job_prio);
}
