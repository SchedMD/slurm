/***************************************************************************** \
 *  task_cgroup_cpuset.c - cpuset cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *  Portions copyright (C) 2012,2015 Bull/Atos
 *  Written by Martin Perry <martin.perry@atos.net>
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

#if !(defined(__FreeBSD__) || defined(__NetBSD__))
#include "config.h"

#define _GNU_SOURCE
#include <ctype.h>
#include <limits.h>
#include <sched.h>
#include <sys/types.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/bitstring.h"
#include "src/common/cpu_frequency.h"
#include "src/common/proc_args.h"
#include "src/common/read_config.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/xstring.h"
#include "src/common/cgroup.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/common/task_plugin.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "task_cgroup.h"

extern int task_cgroup_cpuset_init(void)
{
	cgroup_g_initialize(CG_CPUS);
	return SLURM_SUCCESS;
}

extern int task_cgroup_cpuset_fini(void)
{
	return cgroup_g_step_destroy(CG_CPUS);
}

extern int task_cgroup_cpuset_create(stepd_step_rec_t *job)
{
	cgroup_limits_t limits, *slurm_limits = NULL;
	char *job_alloc_cpus = NULL;
	char *step_alloc_cpus = NULL;
	pid_t pid;
	int rc = SLURM_SUCCESS;

	/* First create the cpuset hierarchy for this job */
	if ((rc = cgroup_g_step_create(CG_CPUS, job)) != SLURM_SUCCESS)
		return rc;

	/* Then constrain the user/job/step to the required cores/cpus */

	/* build job and job steps allocated cores lists */
	debug("job abstract cores are '%s'", job->job_alloc_cores);
	debug("step abstract cores are '%s'", job->step_alloc_cores);

	if (xcpuinfo_abs_to_mac(job->job_alloc_cores,
				&job_alloc_cpus) != SLURM_SUCCESS) {
		error("unable to build job physical cores");
		goto endit;
	}
	if (xcpuinfo_abs_to_mac(job->step_alloc_cores,
				&step_alloc_cpus) != SLURM_SUCCESS) {
		error("unable to build step physical cores");
		goto endit;
	}
	debug("job physical CPUs are '%s'", job_alloc_cpus);
	debug("step physical CPUs are '%s'", step_alloc_cpus);

	/*
	 * check that user's cpuset cgroup is consistent and add the job's CPUs
	 */
	slurm_limits = cgroup_g_constrain_get(CG_CPUS, CG_LEVEL_SLURM);

	if (!slurm_limits)
		goto endit;

	cgroup_init_limits(&limits);
	limits.allow_mems = slurm_limits->allow_mems;
	limits.step = job;

	/* User constrain */
	limits.allow_cores = xstrdup_printf(
		"%s,%s", job_alloc_cpus, slurm_limits->allow_cores);
	rc = cgroup_g_constrain_set(CG_CPUS, CG_LEVEL_USER, &limits);
	xfree(limits.allow_cores);
	if (rc != SLURM_SUCCESS)
		goto endit;

	/* Job constrain */
	limits.allow_cores = job_alloc_cpus;
	rc = cgroup_g_constrain_set(CG_CPUS, CG_LEVEL_JOB, &limits);
	if (rc != SLURM_SUCCESS)
		goto endit;

	/* Step constrain */
	limits.allow_cores = step_alloc_cpus;
	rc = cgroup_g_constrain_set(CG_CPUS, CG_LEVEL_STEP, &limits);
	if (rc != SLURM_SUCCESS)
		goto endit;

	/* attach the slurmstepd to the step cpuset cgroup */
	pid = getpid();
	rc = cgroup_g_step_addto(CG_CPUS, &pid, 1);

	/* validate the requested cpu frequency and set it */
	cpu_freq_cgroup_validate(job, step_alloc_cpus);

endit:
	xfree(job_alloc_cpus);
	xfree(step_alloc_cpus);
	cgroup_free_limits(slurm_limits);
	return rc;
}

/*
 * Keep track a of a pid.
 */
extern int task_cgroup_cpuset_add_pid(pid_t pid)
{
	return cgroup_g_step_addto(CG_CPUS, &pid, 1);
}

#endif
