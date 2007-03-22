/*****************************************************************************\
 *  task_affinity.c - Library for task pre-launch and post_termination
 *	functions for task affinity support
 *****************************************************************************
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Modified by Hewlett-Packard for task affinity support using task_none.c
 *  Copyright (C) 2005 The Regents of the University of California and
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  task_none.c Written by Morris Jette <jette1@llnl.gov>. 
 *  UCRL-CODE-226842.
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

#include "affinity.h"
#include "dist_tasks.h"

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
const char plugin_name[]        = "task affinity plugin";
const char plugin_type[]        = "task/affinity";
const uint32_t plugin_version   = 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
int init ( void )
{
	lllp_ctx_alloc();
	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated 
 *	storage here.
 */
int fini ( void )
{
	lllp_ctx_destroy();
	verbose("%s unloaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_launch_request()
 */
int task_slurmd_launch_request ( uint32_t job_id,
			launch_tasks_request_msg_t *req, uint32_t node_id)
{
	int hw_sockets, hw_cores, hw_threads;

	debug("task_slurmd_launch_request: %u %u", job_id, node_id);
	hw_sockets = conf->sockets;
	hw_cores   = conf->cores;
	hw_threads = conf->threads;

	if (((hw_sockets >= 1) && ((hw_cores > 1) || (hw_threads > 1))) 
	    || (!(req->cpu_bind_type & CPU_BIND_NONE)))	
		lllp_distribution(req, node_id);
	/* Remove the slurm msg timeout needs to be investigated some more */
	/* req->cpu_bind_type = CPU_BIND_NONE; */ 

	return SLURM_SUCCESS;
}

/*
 * task_slurmd_reserve_resources()
 */
int task_slurmd_reserve_resources ( uint32_t job_id,
			launch_tasks_request_msg_t *req, uint32_t node_id)
{
	debug("task_slurmd_reserve_resources: %u",
		job_id);
	cr_reserve_lllp(job_id, req, node_id);
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_release_resources()
 */
int task_slurmd_release_resources ( uint32_t job_id )
{
	debug("task_slurmd_release_resources: %u",
		job_id);
	cr_release_lllp(job_id);
	return SLURM_SUCCESS;
}

/*
 * task_pre_setuid() is called before setting the UID for the 
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
int task_pre_setuid ( slurmd_job_t *job )
{
	char path[PATH_MAX];

	if (!conf->use_cpusets)
		return SLURM_SUCCESS;

	if (snprintf(path, PATH_MAX, "%s/slurm%u",
			CPUSET_DIR, job->jobid) > PATH_MAX) {
		error("cpuset path too long");
		return SLURM_ERROR;
	}
	slurm_build_cpuset(CPUSET_DIR, path, job->uid, job->gid);
	return SLURM_SUCCESS;
}

/*
 * task_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
int task_pre_launch ( slurmd_job_t *job )
{
	char base[PATH_MAX], path[PATH_MAX];

	debug("affinity task_pre_launch: %u.%u, task %d", 
		job->jobid, job->stepid, job->envtp->procid);

	if (conf->use_cpusets) {
		info("Using cpuset affinity for tasks");
		if (snprintf(base, PATH_MAX, "%s/slurm%u",
				CPUSET_DIR, job->jobid) > PATH_MAX) {
			error("cpuset path too long");
			return SLURM_ERROR;
		}
		if (snprintf(path, PATH_MAX, "%s/slurm%u.%u_%d",
				base, job->jobid, job->stepid,
				job->envtp->localid) > PATH_MAX) {
			error("cpuset path too long");
			return SLURM_ERROR;
		}
	} else
		info("Using sched_affinity for tasks");

	/*** CPU binding support ***/
	if (job->cpu_bind_type) {	
		cpu_set_t new_mask, cur_mask;
		pid_t mypid  = job->envtp->task_pid;

		int setval = 0;
		slurm_getaffinity(mypid, sizeof(cur_mask), &cur_mask);

		if (get_cpuset(&new_mask, job)
		&&  (!(job->cpu_bind_type & CPU_BIND_NONE))) {
			if (conf->use_cpusets) {
				setval = slurm_set_cpuset(base, path, mypid,
						sizeof(new_mask), 
						&new_mask);
				slurm_get_cpuset(path, mypid,
						sizeof(cur_mask), 
						&cur_mask);
			} else {
				setval = slurm_setaffinity(mypid,
						sizeof(new_mask), 
						&new_mask);
				slurm_getaffinity(mypid,
						sizeof(cur_mask), 
						&cur_mask);
			}
		}
		slurm_chkaffinity(setval ? &new_mask : &cur_mask, 
					job, setval);
	}

#ifdef HAVE_NUMA
	if (conf->use_cpusets && (slurm_memset_available() >= 0)) {
		nodemask_t new_mask, cur_mask;

		cur_mask = numa_get_membind();
		if (get_memset(&new_mask, job)
		&&  (!(job->mem_bind_type & MEM_BIND_NONE))) {
			slurm_set_memset(path, &new_mask);
			cur_mask = new_mask;
		}
		slurm_chk_memset(&cur_mask, job);
	} else if (job->mem_bind_type && (numa_available() >= 0)) {
		nodemask_t new_mask, cur_mask;

		cur_mask = numa_get_membind();
		if (get_memset(&new_mask, job)
		&&  (!(job->mem_bind_type & MEM_BIND_NONE))) {
			numa_set_membind(&new_mask);
			cur_mask = new_mask;
		}
		slurm_chk_memset(&cur_mask, job);
	}
#endif
	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceeded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
int task_post_term ( slurmd_job_t *job )
{
	debug("affinity task_post_term: %u.%u, task %d",
		job->jobid, job->stepid, job->envtp->procid);

	return SLURM_SUCCESS;
}

