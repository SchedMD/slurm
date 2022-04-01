/*****************************************************************************\
 *  task_cgroup.c - Library for task pre-launch and post_termination functions
 *		    for containment using linux cgroup subsystems
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
 *  Modified by Felip Moll <felip.moll@schedmd.com>
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

#include <signal.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/common/xstring.h"
#include "src/common/cgroup.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "task_cgroup.h"
#include "task_cgroup_cpuset.h"
#include "task_cgroup_memory.h"
#include "task_cgroup_devices.h"

const char plugin_name[]        = "Tasks containment cgroup plugin";
const char plugin_type[]        = "task/cgroup";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

static bool use_cpuset  = false;
static bool use_memory  = false;
static bool use_devices = false;

extern int init(void)
{
	int rc = SLURM_SUCCESS;

	if (slurm_cgroup_conf.constrain_swap_space &&
	    !cgroup_g_has_feature(CG_MEMCG_SWAP)) {
		error("ConstrainSwapSpace is enabled but there is no support for swap in the memory cgroup controller.");
		return SLURM_ERROR;
	}

	if (!running_in_slurmstepd())
		goto end;

	if (slurm_cgroup_conf.constrain_cores)
		use_cpuset = true;
	if (slurm_cgroup_conf.constrain_ram_space ||
	    slurm_cgroup_conf.constrain_swap_space)
		use_memory = true;
	if (slurm_cgroup_conf.constrain_devices)
		use_devices = true;

	if (use_cpuset) {
		if ((rc = task_cgroup_cpuset_init())) {
			error("failure enabling core enforcement: %s",
			      slurm_strerror(rc));
			return rc;
		} else
			debug("core enforcement enabled");
	}

	if (use_memory) {
		if ((rc = task_cgroup_memory_init())) {
			error("failure enabling memory enforcement: %s",
			      slurm_strerror(rc));
			return rc;
		} else
			debug("memory enforcement enabled");
	}

	if (use_devices) {
		if ((rc = task_cgroup_devices_init())) {
			error("failure enabling device enforcement: %s",
			      slurm_strerror(rc));
			return rc;
		} else
			debug("device enforcement enabled");
	}
end:
	debug("%s loaded", plugin_name);
	return rc;
}

extern int fini(void)
{
	int rc = SLURM_SUCCESS;

	if (use_cpuset && (task_cgroup_cpuset_fini() != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_memory && (task_cgroup_memory_fini() != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_devices && (task_cgroup_devices_fini() != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	debug("%s unloaded", plugin_name);

	return rc;
}

extern int task_p_slurmd_batch_request(batch_job_launch_msg_t *req)
{
	return SLURM_SUCCESS;
}

extern int task_p_slurmd_launch_request(launch_tasks_request_msg_t *req,
					uint32_t node_id, char **err_msg)
{
	return SLURM_SUCCESS;
}

extern int task_p_slurmd_suspend_job(uint32_t job_id)
{
	return SLURM_SUCCESS;
}

extern int task_p_slurmd_resume_job(uint32_t job_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_setuid() is called as root before setting the UID for the user to
 * launch his jobs. Use this to create the cgroup hierarchy and set the owner
 * appropriately.
 */
extern int task_p_pre_setuid(stepd_step_rec_t *job)
{
	int rc = SLURM_SUCCESS;

	if (use_cpuset && (task_cgroup_cpuset_create(job) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_memory && (task_cgroup_memory_create(job) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_devices && (task_cgroup_devices_create(job) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	return rc;
}

/*
 * task_p_pre_set_affinity() is called prior to exec of application task.
 * Runs in privileged mode.
 */
extern int task_p_pre_set_affinity(stepd_step_rec_t *job, uint32_t node_tid)
{
	int rc = SLURM_SUCCESS;

	if (use_cpuset &&
	    (task_cgroup_cpuset_add_pid(
		    job->task[node_tid]->pid) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_memory &&
	    (task_cgroup_memory_add_pid(job, job->task[node_tid]->pid,
					node_tid) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_devices &&
	    (task_cgroup_devices_add_pid(job, job->task[node_tid]->pid,
					 node_tid) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	return rc;
}

/*
 * task_p_set_affinity() is called prior to exec of application task.
 * Runs in privileged mode.
 */
extern int task_p_set_affinity(stepd_step_rec_t *job, uint32_t node_tid)
{
	return SLURM_SUCCESS;
}

/*
 * task_p_post_set_affinity is called prior to exec of application task.
 * Runs in privileged mode.
 */
extern int task_p_post_set_affinity(stepd_step_rec_t *job, uint32_t node_tid)
{
	if (use_devices)
		return task_cgroup_devices_constrain(job,
						     job->task[node_tid]->pid,
						     node_tid);
	return SLURM_SUCCESS;
}

/*
 * task_p_pre_launch() is called prior to exec of application task.
 * It is followed by TaskProlog program (from slurm.conf) and --task-prolog
 * (from srun command line).
 */
extern int task_p_pre_launch(stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 * It is preceded by --task-epilog (from srun command line) fllowed by
 * TaskEpilog program (from slurm.conf).
 */
extern int task_p_post_term(stepd_step_rec_t *job, stepd_step_task_info_t *task)
{
	static bool ran = false;
	int rc = SLURM_SUCCESS;

	/*
	 * Only run this on the first call since this will run for
	 * every task on the node.
	 */
	if (use_memory && !ran) {
		rc = task_cgroup_memory_check_oom(job);
		ran = true;
	}
	return rc;
}

/* task_p_post_step() is called after termination of the step (all the task). */
extern int task_p_post_step(stepd_step_rec_t *job)
{
	return fini();
}

/* Add pid to specific cgroup. */
extern int task_p_add_pid(pid_t pid)
{
	int rc = SLURM_SUCCESS;

	if (use_cpuset && (task_cgroup_cpuset_add_pid(pid) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_memory && (task_cgroup_memory_add_extern_pid(pid) !=
			   SLURM_SUCCESS))
		rc = SLURM_ERROR;

	if (use_devices &&
	    (task_cgroup_devices_add_extern_pid(pid) != SLURM_SUCCESS))
		rc = SLURM_ERROR;

	return rc;
}
