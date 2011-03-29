/*****************************************************************************\
 *  task_cgroup.c - Library for task pre-launch and post_termination functions
 *	            for containment using linux cgroup subsystems
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <signal.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/common/xcgroup_read_config.h"
#include "src/common/xcgroup.h"

#include "task_cgroup_cpuset.h"
#include "task_cgroup_memory.h"
//#include "task_cgroup_devices.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as this API matures.
 */
const char plugin_name[]        = "Tasks containment using linux cgroup";
const char plugin_type[]        = "task/cgroup";
const uint32_t plugin_version   = 100;

static bool use_cpuset  = false;
static bool use_memory  = false;
static bool use_devices = false;

static slurm_cgroup_conf_t slurm_cgroup_conf;

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{

	/* read cgroup configuration */
	if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
		return SLURM_ERROR;

	/* enable subsystems based on conf */
	if (slurm_cgroup_conf.constrain_cores) {
		use_cpuset = true;
		task_cgroup_cpuset_init(&slurm_cgroup_conf);
		debug("%s: now constraining jobs allocated cores",
		      plugin_type);
	}

	if (slurm_cgroup_conf.constrain_ram_space ||
	     slurm_cgroup_conf.constrain_swap_space) {
		use_memory = true;
		task_cgroup_memory_init(&slurm_cgroup_conf);
		debug("%s: now constraining jobs allocated memory",
		      plugin_type);
	}

	if (slurm_cgroup_conf.constrain_devices) {
		use_devices = true;
		/* here we should initialize the devices subsystem */
		debug("%s: now constraining jobs allocated devices",
		      plugin_type);
	}

	verbose("%s: loaded", plugin_type);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini (void)
{

	if (use_cpuset) {
		task_cgroup_cpuset_fini(&slurm_cgroup_conf);
	}
	if (use_memory) {
		task_cgroup_memory_fini(&slurm_cgroup_conf);
	}
	if (use_devices) {
		;
	}

	/* unload configuration */
	free_slurm_cgroup_conf(&slurm_cgroup_conf);

	return SLURM_SUCCESS;
}

/*
 * task_slurmd_batch_request()
 */
extern int task_slurmd_batch_request (uint32_t job_id,
				      batch_job_launch_msg_t *req)
{
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_launch_request()
 */
extern int task_slurmd_launch_request (uint32_t job_id,
				       launch_tasks_request_msg_t *req,
				       uint32_t node_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_reserve_resources()
 */
extern int task_slurmd_reserve_resources (uint32_t job_id,
					  launch_tasks_request_msg_t *req,
					  uint32_t node_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_suspend_job()
 */
extern int task_slurmd_suspend_job (uint32_t job_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_resume_job()
 */
extern int task_slurmd_resume_job (uint32_t job_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_release_resources()
 */
extern int task_slurmd_release_resources (uint32_t job_id)
{
	return SLURM_SUCCESS;
}

/*
 * task_pre_setuid() is called before setting the UID for the
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
extern int task_pre_setuid (slurmd_job_t *job)
{

	if (use_cpuset) {
		/* we create the cpuset container as we are still root */
		task_cgroup_cpuset_create(job);
	}

	if (use_memory) {
		/* we create the memory container as we are still root */
		task_cgroup_memory_create(job);
	}

	if (use_devices) {
		/* here we should create the devices container as we are root */
	}

	return SLURM_SUCCESS;
}

/*
 * task_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
extern int task_pre_launch (slurmd_job_t *job)
{

	if (use_cpuset) {
		/* attach the task ? not necessary but in case of future mods */
		task_cgroup_cpuset_attach_task(job);

		/* set affinity if requested */
		if (slurm_cgroup_conf.task_affinity)
			task_cgroup_cpuset_set_task_affinity(job);
	}

	if (use_memory) {
		/* attach the task ? not necessary but in case of future mods */
		task_cgroup_memory_attach_task(job);
	}

	if (use_devices) {
		;
	}

	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_post_term (slurmd_job_t *job)
{
	return SLURM_SUCCESS;
}

/*
 * task_post_step() is called after termination of the step
 * (all the task)
 */
extern int task_post_step (slurmd_job_t *job)
{
	fini();
	return SLURM_SUCCESS;
}
