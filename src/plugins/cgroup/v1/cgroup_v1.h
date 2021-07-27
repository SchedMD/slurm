/*****************************************************************************\
 *  cgroup_v1.h - Cgroup v1 plugin
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

#ifndef _CGROUP_V1_H
#define _CGROUP_V1_H

#define _GNU_SOURCE		/* For POLLRDHUP, O_CLOEXEC on older glibc */
#include <poll.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/bitstring.h"
#include "src/common/cgroup.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/plugins/cgroup/common/cgroup_common.h"

#include "xcgroup.h"

// http://lists.debian.org/debian-boot/2012/04/msg00047.html
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#define	MS_NOSUID MNT_NOSUID
#define	MS_NOEXEC MNT_NOEXEC
#define	MS_NODEV 0
#define	umount(d) unmount(d, 0)
#else
#include <sys/eventfd.h>
#endif

#define MAX_MOVE_WAIT 5000

/* Functions */
extern int init(void);
extern int fini(void);

/*
 * Create the cgroup namespace and the root cgroup objects. This two entities
 * are the basic ones used by any other function and contain information about
 * the cg paths, mount points, name, ownership, and so on. The creation of the
 * root namespace may involve also automounting the cgroup subsystem. Set also
 * any specific required parameter on the root cgroup depending on the
 * controller.
 *
 * This function *does not* involve any mkdir.
 *
 * A subsystem is a synonym for cgroup controller used typically in legacy mode
 * (cgroup v1).
 *
 * IN sub - Controller to initialize.
 * RET SLURM_SUCCESS or error
 */
extern int cgroup_p_initialize(cgroup_ctl_type_t sub);

/*
 * Create the system directories for the specified controller and set any
 * required parameters. These directories are the ones where slurmd and
 * slurmstepd will be put if CoreSpecLimit, MemSpecLimit or CoreSpecCnt are set
 * in slurm.conf. Current supported controllers are only cpuset and memory.
 *
 * IN sub - Controller to initialize.
 * RET SLURM_SUCCESS or error
 */
extern int cgroup_p_system_create(cgroup_ctl_type_t sub);

/*
 * Add pids to the system cgroups. Typically these pids will be slurmstepd pids.
 *
 * IN sub - To which controller will the pids be added.
 * IN pids - Array of pids to add.
 * IN npids - Count of pids in the array.
 * RET SLURM_SUCCESS if pids were correctly added or SLURM_ERROR otherwise.
 */
extern int cgroup_p_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);

/*
 * rmdir the system cgroup controller and destroy the xcgroup global objects.
 * It will move our pid first to the root cgroup, otherwise removal would return
 * EBUSY.
 *
 * IN sub - Which controller will be destroyed.
 * RET SLURM_SUCCESS if destroy was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_p_system_destroy(cgroup_ctl_type_t sub);

/*
 * Create the directories for a job step in the given controller, set also any
 * needed default parameters. Initialize also the step cgroup objects.
 * Every controller may have its own specific settings. This function is called
 * from a slurmstepd only once. Record also that we're using this step object.
 *
 * The directory path will be /<root_cg>/<nodename>/<uid>/<jobid>/<stepid>/
 *
 * IN sub - Under which controller will the directory hierarchy be created.
 * IN job - Step record which is used to create the path in the hierarchy.
 * RET SLURM_SUCCESS if creation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *job);

/*
 * Given a controller, add the specified pids to cgroup.procs of the step. Note
 * that this function will always be called from slurmstepd, which will already
 * have created the step hierarchy and will have the step cgroup objects
 * initialized.
 *
 * IN sub - Under which controller will the directory hierarchy be created.
 * IN pids - Array of pids to add.
 * IN npids - Count of pids in the array.
 * RET SLURM_SUCCESS if addition was possible, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);

/*
 * Get the pids under the freezer controller for this step.
 *
 * OUT pids - Array of pids containing the pids in this step.
 * OUT npids - Count of pids in the array.
 * RET SLURM_SUCCESS if pids were correctly obtained, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_get_pids(pid_t **pids, int *npids);

/*
 * Suspend the step using the freezer controller.
 *
 * RET SLURM_SUCCESS if operation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_suspend();

/*
 * Resume the step using the freezer controller.
 *
 * RET SLURM_SUCCESS if operation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_resume();

/*
 * If the caller (typically from a plugin) is the only one using this step
 * object, rmdir the controller's step directories and destroy the associated
 * cgroup objects. Decrement the step object's active usage count.
 *
 * IN sub - Which controller will be destroyed for this step.
 * RET SLURM_SUCCESS if operation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_destroy(cgroup_ctl_type_t sub);

/*
 * Given a pid, determine if this pid is being tracked by the freezer container.
 *
 * RET true if pid was found, false in any other case.
 */
extern bool cgroup_p_has_pid(pid_t pid);

/*
 * Obtain the constrains set to the root cgroup of the specified controller.
 *
 * IN sub - From which controller we want the limits.
 * RET cgroup_limits_t object if limits could be obtained, NULL otherwise.
 */
extern cgroup_limits_t *cgroup_p_root_constrain_get(cgroup_ctl_type_t sub);

/*
 * Set constrains to the root cgroup of the specified controller.
 *
 * IN sub - To which controller we want the limits be applied to.
 * IN limits - Struct containing the the limits to be applied.
 * RET SLURM_SUCCESS if limits were applied successfuly, SLURM_ERROR otherwise.
 */
extern int cgroup_p_root_constrain_set(cgroup_ctl_type_t sub,
				       cgroup_limits_t *limits);

/*
 * Obtain the constrains set to the system cgroup of the specified controller.
 *
 * IN sub - From which controller we want the limits.
 * RET cgroup_limits_t object if limits could be obtained, NULL otherwise.
 */
extern cgroup_limits_t *cgroup_p_system_constrain_get(cgroup_ctl_type_t sub);

/*
 * Set constrains to the system cgroup of the specified controller.
 *
 * IN sub - To which controller we want the limits be applied to.
 * IN limits - Struct containing the the limits to be applied.
 * RET SLURM_SUCCESS if limits were applied successfuly, SLURM_ERROR otherwise.
 */
extern int cgroup_p_system_constrain_set(cgroup_ctl_type_t sub,
					 cgroup_limits_t *limits);

/*
 * Set constrains to the user cgroup of the specified controller, for the
 * specified job.
 *
 * IN sub - To which controller we want the limits be applied to.
 * IN job - Step to which we want the limits be applied to.
 * IN limits - Struct containing the the limits to be applied.
 * RET SLURM_SUCCESS if limits were applied successfuly, SLURM_ERROR otherwise.
 */
extern int cgroup_p_user_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits);

/*
 * Set constrains to the job cgroup of the specified controller, for the
 * specified job.
 *
 * IN sub - To which controller we want the limits be applied to.
 * IN job - Step to which we want the limits be applied to.
 * IN limits - Struct containing the the limits to be applied.
 * RET SLURM_SUCCESS if limits were applied successfuly, SLURM_ERROR otherwise.
 */
extern int cgroup_p_job_constrain_set(cgroup_ctl_type_t sub,
				      stepd_step_rec_t *job,
				      cgroup_limits_t *limits);

/*
 * Set constrains to the step cgroup of the specified controller, for the
 * specified job.
 *
 * IN sub - To which controller we want the limits be applied to.
 * IN job - Step to which we want the limits be applied to.
 * IN limits - Struct containing the the limits to be applied.
 * RET SLURM_SUCCESS if limits were applied successfuly, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_constrain_set(cgroup_ctl_type_t sub,
				       stepd_step_rec_t *job,
				       cgroup_limits_t *limits);

/*
 * Set constrains to the task cgroup of the specified controller, for the
 * specified job.
 *
 * IN sub - To which controller we want the limits be applied to.
 * IN limits - Struct containing the the limits to be applied.
 * IN taskid - task to which we want the limits be applied to.
 * RET SLURM_SUCCESS if limits were applied successfuly, SLURM_ERROR otherwise.
 */
extern int cgroup_p_task_constrain_set(cgroup_ctl_type_t sub,
				       cgroup_limits_t *limits,
				       uint32_t taskid);

/*
 * Cgroup v1 function to detect OOM conditions.
 *
 * Do use memory.oom_control and cgroup.event_control, see:
 * https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt
 *
 * Start a monitoring thread which will read the event files with a polling
 * mechanism and wait for a stop signal. When the stop signal is received this
 * thread will communicate the detected OOMs. This is not a 100% reliable method
 * since events can be triggered with more than just OOMs, e.g. rmdirs.
 *
 * RET SLURM_SUCCESS if monitoring thread is started, SLURM_ERROR otherwise.
 */
extern int cgroup_p_step_start_oom_mgr();

/*
 * Signal the monitoring thread with a stop message and get the results.
 *
 * IN job - Step record.
 * RET cgroup_oom_t - Struct containing the oom information for this step.
 */
extern cgroup_oom_t *cgroup_p_step_stop_oom_mgr(stepd_step_rec_t *job);

/*
 * Add a task_X directories to the specified controllers of this step and
 * record we're tracking this task. Add the task pid to the controller.
 *
 * IN sub - controller we're managing
 * IN job - step record to create the task directories and add the pid to.
 * IN task_id - task number to form the path and create the task_x directory.
 * IN pid - pid to add to. Note, the task_id may not coincide with job->task[i]
 *          so we may not know where the pid is stored in the job struct.
 * RET SLURM_SUCCESS if the task was succesfully created and the pid added to
 *     all accounting controllers.
 */
extern int cgroup_p_task_addto(cgroup_ctl_type_t sub, stepd_step_rec_t *job,
			       pid_t pid, uint32_t task_id);

/*
 * Given a task id return the accounting data reading the accounting controller
 * files for this step.
 *
 * IN task_id - task number we want the data from, for the current step.
 * RET cgroup_acct_t - struct containing the required data.
 */
extern cgroup_acct_t *cgroup_p_task_get_acct_data(uint32_t taskid);

#endif /* !_CGROUP_V1_H */
