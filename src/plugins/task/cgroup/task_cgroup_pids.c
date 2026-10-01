/*****************************************************************************\
 *  task_cgroup_pids.c - pids cgroup subsystem for task/cgroup
 *****************************************************************************
 *
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "config.h"

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/interfaces/cgroup.h"

#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

extern int task_cgroup_pids_init(void)
{
	if (cgroup_g_initialize(CG_PIDS) != SLURM_SUCCESS)
		return SLURM_ERROR;
	debug("task/cgroup/pids: initialized");

	return SLURM_SUCCESS;
}

extern int task_cgroup_pids_fini(void)
{
	return cgroup_g_step_destroy(CG_PIDS);
}

extern int task_cgroup_pids_create(stepd_step_rec_t *step)
{
	cgroup_limits_t limits;

	if (cgroup_g_step_create(CG_PIDS, step) != SLURM_SUCCESS)
		return SLURM_ERROR;

	/*
	 * Only the step is limited. The job cgroup also holds the slurmstepd
	 * of every step on the node, so it is never constrained.
	 */
	cgroup_init_limits(&limits);
	limits.max_npids = step->max_npids;

	if (limits.max_npids == INFINITE)
		log_flag(CGROUP, "step pids.max=max");
	else if (limits.max_npids != NO_VAL)
		log_flag(CGROUP, "step pids.max=%"PRIu32, limits.max_npids);

	return cgroup_g_constrain_set(CG_PIDS, CG_LEVEL_STEP, &limits);
}

extern int task_cgroup_pids_add_pid(stepd_step_rec_t *step, pid_t pid,
				    uint32_t taskid)
{
	return cgroup_g_task_addto(CG_PIDS, step, pid, taskid);
}

extern int task_cgroup_pids_add_extern_pid(pid_t pid)
{
	/* Only in the extern step we will not create specific tasks */
	return cgroup_g_step_addto(CG_PIDS, &pid, 1);
}
