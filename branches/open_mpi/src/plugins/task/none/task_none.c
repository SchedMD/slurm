/*****************************************************************************\
 *  task_none.c - Library for task pre-launch and post_termination functions 
 *	with no actions
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

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
const char plugin_name[]        = "task NONE plugin";
const char plugin_type[]        = "task/none";
const uint32_t plugin_version   = 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated 
 *	storage here.
 */
extern int fini (void)
{
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_batch_request()
 */
extern int task_slurmd_batch_request (uint32_t job_id, 
				      batch_job_launch_msg_t *req)
{
	debug("task_slurmd_batch_request: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_launch_request()
 */
extern int task_slurmd_launch_request (uint32_t job_id, 
				       launch_tasks_request_msg_t *req, 
				       uint32_t node_id)
{
	debug("task_slurmd_launch_request: %u %u", job_id, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_reserve_resources()
 */
extern int task_slurmd_reserve_resources (uint32_t job_id, 
					  launch_tasks_request_msg_t *req,
					  uint32_t node_id)
{
	debug("task_slurmd_reserve_resources: %u %u", job_id, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_suspend_job()
 */
extern int task_slurmd_suspend_job (uint32_t job_id)
{
	debug("task_slurmd_suspend_job: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_resume_job()
 */
extern int task_slurmd_resume_job (uint32_t job_id)
{
	debug("task_slurmd_resume_job: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_release_resources()
 */
extern int task_slurmd_release_resources (uint32_t job_id)
{
	debug("task_slurmd_release_resources: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_pre_setuid() is called before setting the UID for the
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
extern int task_pre_setuid (slurmd_job_t *job)
{
	return SLURM_SUCCESS;
}

/*
 * task_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
extern int task_pre_launch (slurmd_job_t *job)
{
	debug("task_pre_launch: %u.%u, task %d", 
		job->jobid, job->stepid, job->envtp->procid);
	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceeded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_post_term (slurmd_job_t *job)
{
	debug("task_post_term: %u.%u, task %d",
		job->jobid, job->stepid, job->envtp->procid);
	return SLURM_SUCCESS;
}

