/*****************************************************************************\
 *  task_affinity.c - Library for task pre-launch and post_termination
 *	functions for task affinity support
 *****************************************************************************
 *  Copyright (C) 2005-2008 Hewlett-Packard Development Company, L.P.
 *  Modified by Hewlett-Packard for task affinity support using task_none.c
 *  Copyright (C) 2005-2007 The Regents of the University of California
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>

#include "affinity.h"
#include "dist_tasks.h"

#include "src/slurmd/common/task_plugin.h"


/* Enable purging of cpuset directories
 * after each task and the step are done.
 */
#define PURGE_CPUSET_DIRS 1

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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "task affinity plugin";
const char plugin_type[]        = "task/affinity";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init (void)
{
	cpu_set_t cur_mask;
	char mstr[1 + CPU_SETSIZE / 4];

	slurm_getaffinity(0, sizeof(cur_mask), &cur_mask);
	task_cpuset_to_str(&cur_mask, mstr);
	verbose("%s loaded with CPU mask %s", plugin_name, mstr);

	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini (void)
{
	debug("%s unloaded", plugin_name);
	return SLURM_SUCCESS;
}

/* cpu bind enforcement, update binding type based upon the
 *	TaskPluginParam configuration parameter */
static void _update_bind_type(launch_tasks_request_msg_t *req)
{
	bool set_bind = false;

	if ((req->cpu_bind_type & (~CPU_BIND_VERBOSE)) == 0) {
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
 * task_p_slurmd_batch_request()
 */
extern int task_p_slurmd_batch_request (batch_job_launch_msg_t *req)
{
	info("task_p_slurmd_batch_request: %u", req->job_id);
	batch_bind(req);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_launch_request()
 */
extern int task_p_slurmd_launch_request (launch_tasks_request_msg_t *req,
					 uint32_t node_id)
{
	char buf_type[100];

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

	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_reserve_resources()
 */
extern int task_p_slurmd_reserve_resources (launch_tasks_request_msg_t *req,
					    uint32_t node_id)
{
	debug("task_p_slurmd_reserve_resources: %u", req->job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_suspend_job()
 */
extern int task_p_slurmd_suspend_job (uint32_t job_id)
{
	debug("task_p_slurmd_suspend_job: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_resume_job()
 */
extern int task_p_slurmd_resume_job (uint32_t job_id)
{
	debug("task_p_slurmd_resume_job: %u", job_id);
	return SLURM_SUCCESS;
}

/*
 * task_p_slurmd_release_resources()
 */
extern int task_p_slurmd_release_resources (uint32_t job_id)
{
	DIR *dirp;
	struct dirent *entryp;
	char base[PATH_MAX];
	char path[PATH_MAX];

	debug("%s: affinity jobid %u", __func__, job_id);

#if PURGE_CPUSET_DIRS
	/* NOTE: The notify_on_release flag set in cpuset.c
	 * should remove the directory, but that is not
	 * happening reliably. */
	if (! (conf->task_plugin_param & CPU_BIND_CPUSETS))
		return SLURM_SUCCESS;


#ifdef MULTIPLE_SLURMD
	if (snprintf(base, PATH_MAX, "%s/slurm_%s_%u",
				 CPUSET_DIR,
				 (conf->node_name != NULL)?conf->node_name:"",
				 job_id) >= PATH_MAX) {
		error("%s: cpuset path too long", __func__);
		return SLURM_ERROR;
	}
#else
	if (snprintf(base, PATH_MAX, "%s/slurm%u",
				 CPUSET_DIR, job_id) >= PATH_MAX) {
		error("%s: cpuset path too long", __func__);
		return SLURM_ERROR;
	}
#endif
	if (rmdir(base) == 0)
		return SLURM_SUCCESS;

	/* EBUSY  Attempted to remove, using rmdir(2),
	 * a cpuset with child cpusets. ENOTEMPTY?
	 */
	if (errno != ENOTEMPTY
		&& errno != EBUSY) {
		error("%s: rmdir(%s) failed %m", __func__, base);
		return SLURM_ERROR;
	}

	/* errno == ENOTEMPTY
	 */
	if ((dirp = opendir(base)) == NULL) {
		error("%s: could not open dir %s: %m", __func__, base);
		return SLURM_ERROR;
	}

	while (1) {
		if (!(entryp = readdir(dirp)))
			break;
		if (xstrncmp(entryp->d_name, "slurm", 5))
			continue;
		if (snprintf(path, PATH_MAX, "%s/%s",
			     base, entryp->d_name) >= PATH_MAX) {
			error("%s: cpuset path too long", __func__);
			break;
		}
		if (rmdir(path) != 0) {
			error("%s: rmdir(%s) failed %m", __func__, base);
			closedir(dirp);
			return SLURM_ERROR;
		}
	}
	closedir(dirp);
	if (rmdir(base) != 0) {
		error("%s: rmdir(%s) failed %m", __func__, base);
		return SLURM_ERROR;
	}
#endif

	return SLURM_SUCCESS;
}

/*
 * task_p_pre_setuid() is called before setting the UID for the
 * user to launch his jobs. Use this to create the CPUSET directory
 * and set the owner appropriately.
 */
extern int task_p_pre_setuid (stepd_step_rec_t *job)
{
	char path[PATH_MAX];
	int rc = SLURM_SUCCESS;

	if (conf->task_plugin_param & CPU_BIND_CPUSETS) {
#ifdef MULTIPLE_SLURMD
		if (snprintf(path, PATH_MAX, "%s/slurm_%s_%u",
			     CPUSET_DIR,
			     (conf->node_name != NULL)?conf->node_name:"",
			     job->jobid) > PATH_MAX) {
			error("%s: cpuset path too long", __func__);
			rc = SLURM_ERROR;
		}
#else
		if (snprintf(path, PATH_MAX, "%s/slurm%u",
			     CPUSET_DIR, job->jobid) > PATH_MAX) {
			error("%s: cpuset path too long", __func__);
			rc = SLURM_ERROR;
		}
#endif
		if (rc == SLURM_SUCCESS) {
			rc = slurm_build_cpuset(CPUSET_DIR, path, job->uid,
						job->gid);
			if (rc != SLURM_SUCCESS) {
				error("%s: slurm_build_cpuset() failed",
				       __func__);
			}
		}
	}

	if (rc == SLURM_SUCCESS)
		cpu_freq_cpuset_validate(job);

	return rc;
}

#ifdef HAVE_NUMA
static void _numa_set_preferred(nodemask_t *new_mask)
{
	int i;

	for (i = 0; i < NUMA_NUM_NODES; i++) {
		if (nodemask_isset(new_mask, i)) {
			numa_set_preferred(i);
			break;
		}
	}
}
#endif

/*
 * task_p_pre_launch() is called prior to exec of application task.
 *	It is followed by TaskProlog program (from slurm.conf) and
 *	--task-prolog (from srun command line).
 */
extern int task_p_pre_launch (stepd_step_rec_t *job)
{
	char base[PATH_MAX], path[PATH_MAX];
	int rc = SLURM_SUCCESS;

	debug("%s: affinity jobid %u.%u, task:%u bind:%u",
		  __func__, job->jobid, job->stepid,
		  job->envtp->procid, job->cpu_bind_type);

	if (conf->task_plugin_param & CPU_BIND_CPUSETS) {
		info("%s: Using cpuset affinity for tasks", __func__);
#ifdef MULTIPLE_SLURMD
		if (snprintf(base, PATH_MAX, "%s/slurm_%s_%u",
					 CPUSET_DIR,
					 (conf->node_name != NULL)?conf->node_name:"",
					 job->jobid) >= PATH_MAX) {
			error("%s: cpuset path too long", __func__);
			return SLURM_ERROR;
		}
#else
		if (snprintf(base, PATH_MAX, "%s/slurm%u",
					 CPUSET_DIR, job->jobid) >= PATH_MAX) {
			error("%s: cpuset path too long", __func__);
			return SLURM_ERROR;
		}
#endif
		if (snprintf(path, PATH_MAX, "%s/slurm%u.%u_%d",
					 base, job->jobid, job->stepid,
					 job->envtp->localid) >= PATH_MAX) {
			error("%s: cpuset path too long", __func__);
			return SLURM_ERROR;
		}
	} else
		info("%s: Using sched_affinity for tasks", __func__);

	/*** CPU binding support ***/
	if (job->cpu_bind_type) {
		cpu_set_t new_mask, cur_mask;
		pid_t mypid  = job->envtp->task_pid;

		slurm_getaffinity(mypid, sizeof(cur_mask), &cur_mask);
		if (get_cpuset(&new_mask, job) &&
		    (!(job->cpu_bind_type & CPU_BIND_NONE))) {
			reset_cpuset(&new_mask, &cur_mask);
			if (conf->task_plugin_param & CPU_BIND_CPUSETS) {
				rc = slurm_set_cpuset(base, path, mypid,
						      sizeof(new_mask),
						      &new_mask);
				slurm_get_cpuset(path, mypid,
						 sizeof(cur_mask),
						 &cur_mask);
			} else {
				rc = slurm_setaffinity(mypid,
						       sizeof(new_mask),
						       &new_mask);
				slurm_getaffinity(mypid,
						  sizeof(cur_mask),
						  &cur_mask);
			}
		}
		task_slurm_chkaffinity(rc ? &cur_mask : &new_mask,
				       job, rc);
	} else if (job->mem_bind_type &&
		   (conf->task_plugin_param & CPU_BIND_CPUSETS)) {
		cpu_set_t cur_mask;
		pid_t mypid  = job->envtp->task_pid;

		/* Establish cpuset just for the memory binding */
		slurm_getaffinity(mypid, sizeof(cur_mask), &cur_mask);
		rc = slurm_set_cpuset(base, path,
				      (pid_t) job->envtp->task_pid,
				      sizeof(cur_mask), &cur_mask);
	}

#ifdef HAVE_NUMA
	if ((conf->task_plugin_param & CPU_BIND_CPUSETS) &&
	    (slurm_memset_available() >= 0)) {
		nodemask_t new_mask, cur_mask;

		cur_mask = numa_get_membind();
		if (get_memset(&new_mask, job) &&
		    (!(job->mem_bind_type & MEM_BIND_NONE))) {
			slurm_set_memset(path, &new_mask);
			if (numa_available() >= 0) {
				if (job->mem_bind_type & MEM_BIND_PREFER)
					_numa_set_preferred(&new_mask);
				else
					numa_set_membind(&new_mask);
			}
			cur_mask = new_mask;
		}
		slurm_chk_memset(&cur_mask, job);
	} else if (job->mem_bind_type && (numa_available() >= 0)) {
		nodemask_t new_mask, cur_mask;

		cur_mask = numa_get_membind();
		if (get_memset(&new_mask, job)
		    &&  (!(job->mem_bind_type & MEM_BIND_NONE))) {
			if (job->mem_bind_type & MEM_BIND_PREFER)
				_numa_set_preferred(&new_mask);
			else
				numa_set_membind(&new_mask);
			cur_mask = new_mask;
		}
		slurm_chk_memset(&cur_mask, job);
	}
#endif
	return rc;
}

/*
 * task_p_pre_launch_priv() is called prior to exec of application task.
 * in privileged mode, just after slurm_spank_task_init_privileged
 */
extern int task_p_pre_launch_priv(stepd_step_rec_t *job, pid_t pid)
{
	return SLURM_SUCCESS;
}

/*
 * task_term() is called after termination of application task.
 *	It is preceded by --task-epilog (from srun command line)
 *	followed by TaskEpilog program (from slurm.conf).
 */
extern int task_p_post_term (stepd_step_rec_t *job, stepd_step_task_info_t *task)
{
		char base[PATH_MAX], path[PATH_MAX];
	debug("%s: affinity %u.%u, task %d",
	      __func__, job->jobid, job->stepid, task->id);

#if PURGE_CPUSET_DIRS
	/* NOTE: The notify_on_release flag set in cpuset.c
	 * should remove the directory, but that is not
	 * happening reliably. */
	if (! (conf->task_plugin_param & CPU_BIND_CPUSETS))
		return SLURM_SUCCESS;

#ifdef MULTIPLE_SLURMD
	if (snprintf(base, PATH_MAX, "%s/slurm_%s_%u",
				 CPUSET_DIR,
				 (conf->node_name != NULL)?conf->node_name:"",
				 job->jobid) >= PATH_MAX) {
		error("%s: cpuset path too long", __func__);
		return SLURM_ERROR;
	}
#else
	if (snprintf(base, PATH_MAX, "%s/slurm%u",
				 CPUSET_DIR, job->jobid) >= PATH_MAX) {
		error("%s: cpuset path too long", __func__);
		return SLURM_ERROR;
	}
#endif
	if (snprintf(path, PATH_MAX, "%s/slurm%u.%u_%d",
				 base, job->jobid, job->stepid,
				 task->id) >= PATH_MAX) {
		error("%s: cpuset path too long", __func__);
		return SLURM_ERROR;
	}
	/* Only error out if it failed to remove the cpuset dir. The cpuset
	 * dir may have already been removed by the release_agent. */
	if (rmdir(path) != 0 && errno != ENOENT) {
		error("%s: rmdir(%s) failed %m", __func__, path);
		return SLURM_ERROR;
	}
#endif

	return SLURM_SUCCESS;
}

/*
 * task_p_post_step() is called after termination of the step
 * (all the task)
 */
extern int task_p_post_step (stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

/*
 * Keep track a of a pid.
 */
extern int task_p_add_pid (pid_t pid)
{
	return SLURM_SUCCESS;
}
