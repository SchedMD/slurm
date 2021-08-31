/*****************************************************************************\
 *  cgroup_v2.c - Cgroup v2 plugin
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Felip Moll <felip.moll@schedmd.com>
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

#define _GNU_SOURCE

#include "cgroup_v2.h"

const char plugin_name[] = "Cgroup v2 plugin";
const char plugin_type[] = "cgroup/v2";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*
 * Initialize the cgroup plugin. Slurmd MUST be started by systemd and the
 * option Delegate set to 'Yes' or equal to a string with the desired
 * controllers we want to support in this system. If we are slurmd we're going
 * to create a systemd scope for further slurmstepds. The scope is associated
 * to a cgroup directory, and it will be delegated to us too. We need to
 * separate it from slurmd because if we restart slurmd and there are living
 * steps in the same directory, then slurmd could not be put in a non-leaf
 * cgroup, and systemd will fail (no internal process constraint).
 * Take in mind also we should not do anything upper in the hierarchy because of
 * the single-writer architecture systemd imposes to us. The upper tree is
 * completely under systemd control.
 *
 * We need to play the cgroup v2 game rules:
 *
 * - No Internal Process Constraint
 * - Top-down Constraint
 *
 * And try to be compliant with systemd, or they will complain:
 *
 * - Single writer rule.
 *
 * Read cgroup v2 documentation for more info.
 */
extern int init(void)
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	debug("unloading %s", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * Unlike in Legacy mode (v1) where we needed to create a directory for each
 * controller, in Unified mode this function will be mostly empty because the
 * hierarchy is unified into the same path. The controllers will be enabled
 * when we create the hierarchy. The only controller that may need an init is
 * the 'devices', which in Unified is not a real controller, but instead we
 * need to register an eBPF program.
 */
extern int cgroup_p_initialize(cgroup_ctl_type_t ctl)
{
	return SLURM_SUCCESS;
}

/*
 * As part of the initialization, the slurmd directory is already created, so
 * this function will remain empty.
 */
extern int cgroup_p_system_create(cgroup_ctl_type_t ctl)
{
	return SLURM_SUCCESS;
}

/*
 * Note that as part of the initialization, the slurmd pid is already put
 * inside this cgroup but we still need to implement this for if somebody
 * needs to add a different pid in this cgroup.
 */
extern int cgroup_p_system_addto(cgroup_ctl_type_t ctl, pid_t *pids, int npids)
{
	return SLURM_SUCCESS;
}

/*
 * There's no need to do any cleanup, when systemd terminates the cgroup is
 * automatically removed by systemd.
 */
extern int cgroup_p_system_destroy(cgroup_ctl_type_t ctl)
{
	return SLURM_SUCCESS;
}

/*
 * Create the step hierarchy and move the stepd process into it. Further forked
 * processes will be created in the step directory as child. We need to respect
 * the Top-Down constraint not adding pids to non-leaf cgroups.
 */
extern int cgroup_p_step_create(cgroup_ctl_type_t ctl, stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

/*
 * Move a pid to a specific cgroup. It needs to be a leaf, we cannot move
 * a pid to an intermediate directory in the cgroup hierarchy. Since we always
 * work at task level, we will add this pid to the special task task_4294967293.
 *
 * - Top-down Constraint
 * - No Internal Process Constraint
 *
 * Read cgroup v2 documentation for more info.
 */
extern int cgroup_p_step_addto(cgroup_ctl_type_t ctl, pid_t *pids, int npids)
{
	return SLURM_SUCCESS;
}

/*
 * Read the cgroup.procs of this step.
 */
extern int cgroup_p_step_get_pids(pid_t **pids, int *npids)
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_suspend()
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_resume()
{
	return SLURM_SUCCESS;
}

extern int cgroup_p_step_destroy(cgroup_ctl_type_t ctl)
{
	return SLURM_SUCCESS;
}

/* Return true if the user pid is in this step/task cgroup */
extern bool cgroup_p_has_pid(pid_t pid)
{
	return false;
}

extern int cgroup_p_constrain_set(cgroup_ctl_type_t ctl, cgroup_level_t level,
				  cgroup_limits_t *limits)
{
	return SLURM_SUCCESS;
}

extern cgroup_limits_t *cgroup_p_constrain_get(cgroup_ctl_type_t ctl,
					       cgroup_level_t level)
{
	return NULL;
}

extern int cgroup_p_step_start_oom_mgr()
{
	return SLURM_SUCCESS;
}

extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job)
{
	return NULL;
}

extern int cgroup_p_task_addto(cgroup_ctl_type_t ctl, stepd_step_rec_t *job,
			       pid_t pid, uint32_t task_id)
{
	return SLURM_SUCCESS;
}

extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t task_id)
{
	return NULL;
}
