/*****************************************************************************\
 *  preempt.c - Job preemption plugin function setup.
 *****************************************************************************
 *  Copyright (C) 2009-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <https://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <pthread.h>
#include <signal.h>

#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/job_scheduler.h"

typedef struct slurm_preempt_ops {
	List		(*find_jobs)	      (struct job_record *job_ptr);
	uint16_t	(*job_preempt_mode)   (struct job_record *job_ptr);
	bool		(*preemption_enabled) (void);
	bool		(*job_preempt_check)  (job_queue_rec_t *preemptor,
					       job_queue_rec_t *preemptee);
} slurm_preempt_ops_t;

/*
 * Must be synchronized with slurm_preempt_ops_t above.
 */
static const char *syms[] = {
	"find_preemptable_jobs",
	"job_preempt_mode",
	"preemption_enabled",
	"job_preempt_check",
};

static slurm_preempt_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t	    g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

static void _preempt_signal(struct job_record *job_ptr, uint32_t grace_time)
{
	if (job_ptr->preempt_time)
		return;

	job_ptr->preempt_time = time(NULL);
	job_ptr->end_time = MIN(job_ptr->end_time,
				(job_ptr->preempt_time + (time_t)grace_time));

	/* Signal the job at the beginning of preemption GraceTime */
	job_signal(job_ptr, SIGCONT, 0, 0, 0);
	job_signal(job_ptr, SIGTERM, 0, 0, 0);
}

extern int slurm_job_check_grace(struct job_record *job_ptr,
				 struct job_record *preemptor_ptr)
{
	/* Preempt modes: -1 (unset), 0 (none), 1 (partition), 2 (QOS) */
	static int preempt_mode = 0;
	static time_t last_update_time = (time_t) 0;
	int rc = SLURM_SUCCESS;
	uint32_t grace_time = 0;

	if (job_ptr->preempt_time) {
		if (time(NULL) >= job_ptr->end_time)
			rc = SLURM_ERROR;
		return rc;
	}

	if (last_update_time != slurmctld_conf.last_update) {
		char *preempt_type = slurm_get_preempt_type();
		if (!xstrcmp(preempt_type, "preempt/partition_prio"))
			preempt_mode = 1;
		else if (!xstrcmp(preempt_type, "preempt/qos"))
			preempt_mode = 2;
		else
			preempt_mode = 0;
		xfree(preempt_type);
		last_update_time = slurmctld_conf.last_update;
	}

	if (preempt_mode == 1)
		grace_time = job_ptr->part_ptr->grace_time;
	else if (preempt_mode == 2) {
		if (!job_ptr->qos_ptr)
			error("%s: %pJ has no QOS ptr!  This should never happen",
			      __func__, job_ptr);
		else
			grace_time = job_ptr->qos_ptr->grace_time;
	}

	if (grace_time) {
		debug("setting %u sec preemption grace time for %pJ to reclaim resources for %pJ",
		      grace_time, job_ptr, preemptor_ptr);
		_preempt_signal(job_ptr, grace_time);
	} else
		rc = SLURM_ERROR;

	return rc;
}

extern int slurm_preempt_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "preempt";
	char *type = NULL;

	/* This function is called frequently, so it should be as fast as
	 * possible. The test below will be true almost all of the time and
	 * is as fast as possible. */
	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_preempt_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	xfree(type);
	return retval;
}

extern int slurm_preempt_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

extern List slurm_find_preemptable_jobs(struct job_record *job_ptr)
{
	if (slurm_preempt_init() < 0)
		return NULL;

	return (*(ops.find_jobs))(job_ptr);
}

/*
 * Return the PreemptMode which should apply to stop this job
 */
extern uint16_t slurm_job_preempt_mode(struct job_record *job_ptr)
{
	if (slurm_preempt_init() < 0)
		return (uint16_t) PREEMPT_MODE_OFF;

	return (*(ops.job_preempt_mode))(job_ptr);
}

/*
 * Return true if any jobs can be preempted, otherwise false
 */
extern bool slurm_preemption_enabled(void)
{
	if (slurm_preempt_init() < 0)
		return false;

	return (*(ops.preemption_enabled))();
}

/*
 * Return true if the preemptor can preempt the preemptee, otherwise false
 */
extern bool slurm_job_preempt_check(job_queue_rec_t *preemptor,
				    job_queue_rec_t *preemptee)
{
	if (slurm_preempt_init() < 0)
		return false;

	return (*(ops.job_preempt_check))
		(preemptor, preemptee);
}
