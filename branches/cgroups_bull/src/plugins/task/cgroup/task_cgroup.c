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

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/common/xcgroup_read_config.h"
#include "src/common/xcgroup.h"
#include "src/common/xcpuinfo.h"

#include "affinity.h"
#include "dist_tasks.h"
#include "task_cgroup_cpuset.h"
//#include "task_cgroup_memory.h"

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
const char plugin_name[]        = "task containment using linux cgroup";
const char plugin_type[]        = "task/cgroup";
const uint32_t plugin_version   = 100;

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#define CGROUP_SLURMDIR CGROUP_BASEDIR "/slurm"

static bool use_taskbind = false;

/* cpu bind enforcement, update binding type based upon the
 *	TaskAffinityBindType configuration parameter */
static void _update_bind_type(launch_tasks_request_msg_t *req)
{
	bool set_bind = false;
	if (conf->task_plugin_param & CPU_BIND_NONE) {
		req->cpu_bind_type |= CPU_BIND_NONE;
		req->cpu_bind_type &= (~CPU_BIND_TO_SOCKETS);
		req->cpu_bind_type &= (~CPU_BIND_TO_CORES);
		req->cpu_bind_type &= (~CPU_BIND_TO_THREADS);
		req->cpu_bind_type &= (~CPU_BIND_TO_LDOMS);
		set_bind = true;
	} else if (conf->task_plugin_param & CPU_BIND_TO_SOCKETS) {
		req->cpu_bind_type &= (~CPU_BIND_NONE);
		req->cpu_bind_type |= CPU_BIND_TO_SOCKETS;
		req->cpu_bind_type &= (~CPU_BIND_TO_CORES);
		req->cpu_bind_type &= (~CPU_BIND_TO_THREADS);
		req->cpu_bind_type &= (~CPU_BIND_TO_LDOMS);
		set_bind = true;
	} else if (conf->task_plugin_param & CPU_BIND_TO_CORES) {
		req->cpu_bind_type &= (~CPU_BIND_NONE);
		req->cpu_bind_type &= (~CPU_BIND_TO_SOCKETS);
		req->cpu_bind_type |= CPU_BIND_TO_CORES;
		req->cpu_bind_type &= (~CPU_BIND_TO_THREADS);
		req->cpu_bind_type &= (~CPU_BIND_TO_LDOMS);
		set_bind = true;
	} else if (conf->task_plugin_param & CPU_BIND_TO_THREADS) {
		req->cpu_bind_type &= (~CPU_BIND_NONE);
		req->cpu_bind_type &= (~CPU_BIND_TO_SOCKETS);
		req->cpu_bind_type &= (~CPU_BIND_TO_CORES);
		req->cpu_bind_type |= CPU_BIND_TO_THREADS;
		req->cpu_bind_type &= (~CPU_BIND_TO_LDOMS);
		set_bind = true;
	} else if (conf->task_plugin_param & CPU_BIND_TO_LDOMS) {
		req->cpu_bind_type &= (~CPU_BIND_NONE);
		req->cpu_bind_type &= (~CPU_BIND_TO_SOCKETS);
		req->cpu_bind_type &= (~CPU_BIND_TO_CORES);
		req->cpu_bind_type &= (~CPU_BIND_TO_THREADS);
		req->cpu_bind_type &= CPU_BIND_TO_LDOMS;
		set_bind = true;
	}

	if (conf->task_plugin_param & CPU_BIND_VERBOSE) {
		req->cpu_bind_type |= CPU_BIND_VERBOSE;
		set_bind = true;
	}

	if (set_bind) {
		char bind_str[128];
		slurm_sprint_cpu_bind_type(bind_str, req->cpu_bind_type);
		info("task affinity : enforcing '%s' cpu bind method",
		     bind_str);
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{
	char* pcpuset = NULL;

	/* read cgroup configuration */
	if ( read_slurm_cgroup_conf() )
		return SLURM_ERROR;

	/* initialize cpuinfo internal data */
	if ( xcpuinfo_init() != XCPUINFO_SUCCESS ) {
        free_slurm_cgroup_conf();
		return SLURM_ERROR;
	}

	/* enable subsystems based on cgroup.conf */
	if (slurm_cgroup_conf->cgroup_subsystems) {
	    pcpuset = strstr(slurm_cgroup_conf->cgroup_subsystems, "cpuset");
	}

	/* if CgroupSubsystems list includes cpuset, or task binding is
	 * configured and CgroupAutomount=yes, create cpuset namespace if
	 * not already present
	 */
	if ((pcpuset != NULL) ||
	   (( slurm_cgroup_conf->task_bind_type != CPU_BIND_NONE ) &&
	    ( slurm_cgroup_conf->cgroup_automount ))){
		if ( task_cgroup_cpuset_init() != SLURM_SUCCESS ) {
			xcpuinfo_fini();
			free_slurm_cgroup_conf();
			return SLURM_ERROR;
	   	}
	}
	if ( slurm_cgroup_conf->task_bind_type != CPU_BIND_NONE ) {
		if (task_cpuset_ns_is_available()) {
			use_taskbind = true;
			conf->task_plugin_param = slurm_cgroup_conf->task_bind_type;
		}
	    else {
		   debug("task binding configured without cpuset subsystem");
           return SLURM_ERROR;
	    }
	}

	/* unload configuration, each plugin that uses cgroups will reload it */
	/* sequentially during its own init */
	free_slurm_cgroup_conf();

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini (void)
{
	xcpuinfo_fini();
	free_slurm_cgroup_conf();
	return SLURM_SUCCESS;
}

/*
 * task_slurmd_batch_request()
 */
extern int task_slurmd_batch_request (uint32_t job_id,
				      batch_job_launch_msg_t *req)
{
	debug("task_slurmd_batch_request: %u", job_id);

	if ( use_taskbind ) {
		batch_bind(req);
	}

	return SLURM_SUCCESS;
}

/*
 * task_slurmd_launch_request()
 */
extern int task_slurmd_launch_request (uint32_t job_id,
				       launch_tasks_request_msg_t *req,
				       uint32_t node_id)
{
	char buf_type[100];

	debug("task_slurmd_launch_request: %u %u", job_id, node_id);

	if ( use_taskbind ) {
	    if (((conf->sockets >= 1)
	         && ((conf->cores > 1) || (conf->threads > 1)))
	        || (!(req->cpu_bind_type & CPU_BIND_NONE))) {
		    _update_bind_type(req);

		    slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		    debug("task affinity : before lllp distribution cpu bind "
		         "method is '%s' (%s)", buf_type, req->cpu_bind);

		    lllp_distribution(req, node_id);

		    slurm_sprint_cpu_bind_type(buf_type, req->cpu_bind_type);
		    debug("task affinity : after lllp distribution cpu bind "
		         "method is '%s' (%s)", buf_type, req->cpu_bind);
	    }
	}

	return SLURM_SUCCESS;
}

/*
 * task_pre_setuid() is called before setting the UID for the
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
extern int task_pre_setuid (slurmd_job_t *job)
{
	debug("task_pre_setuid:%u", job->jobid);
	if ( use_taskbind ) {
	    if ( task_build_cgroup_cpuset(job, job->uid, job->gid)
	    		!= SLURM_SUCCESS)
	    	return SLURM_ERROR;
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
	int rc = SLURM_SUCCESS;

	debug("task_pre_launch:%u.%u, task:%u bind:%u",
	      job->jobid, job->stepid, job->envtp->procid,
	      job->cpu_bind_type);

	if ( use_taskbind ){
	/*** CPU binding support ***/
	    if (job->cpu_bind_type) {
	       cpu_set_t new_mask, cur_mask;
	       pid_t mypid  = job->envtp->task_pid;

	       slurm_getaffinity(mypid, sizeof(cur_mask), &cur_mask);

	       if (get_cpuset(&new_mask, job) &&
              (!(job->cpu_bind_type & CPU_BIND_NONE))) {
	          rc = task_set_cgroup_cpuset(job->envtp->localid, mypid,
					                      sizeof(new_mask), &new_mask);
	          task_get_cgroup_cpuset(mypid, sizeof(cur_mask), &cur_mask);
	       }
	       slurm_chkaffinity(rc ? &cur_mask : &new_mask, job, rc);
	    } else
        if (job->mem_bind_type ) {
		   cpu_set_t cur_mask;
		   pid_t mypid  = job->envtp->task_pid;

		   /* Establish cpuset just for the memory binding */
		   slurm_getaffinity(mypid, sizeof(cur_mask), &cur_mask);
		   rc = task_set_cgroup_cpuset(job->envtp->localid,
				                       (pid_t) job->envtp->task_pid,
				                        sizeof(cur_mask), &cur_mask);
	    }
        /* TODO: Add conditional NUMA code, if needed */
    }
    return rc;
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
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_post_term (slurmd_job_t *job)
{
	debug("task_post_term: %u.%u, task %d",
		job->jobid, job->stepid, job->envtp->procid);
	return SLURM_SUCCESS;
}
