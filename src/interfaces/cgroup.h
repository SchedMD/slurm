/*****************************************************************************\
 *  cgroup.h - driver for cgroup plugin
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

#ifndef _COMMON_CGROUP_H_
#define _COMMON_CGROUP_H_

/* Check filesystem type */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
#include <magic.h>
#include <sys/mount.h>
#include <sys/param.h>
#else
#include <linux/magic.h>
#include <sys/vfs.h>
#endif

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>

#include "config.h"

#include "slurm/slurm.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/interfaces/gres.h"
#include "src/common/log.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/plugin.h"
#include "src/common/slurm_opt.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/plugins/cgroup/common/cgroup_common.h"

#ifdef __GNUC__
#define F_TYPE_EQUAL(a, b) (a == (__typeof__(a)) b)
#else
#define F_TYPE_EQUAL(a, b) (a == (__SWORD_TYPE) b)
#endif

/* Not defined in non-supported v2 linux versions -- e.g centos7 */
#ifndef CGROUP2_SUPER_MAGIC
#define CGROUP2_SUPER_MAGIC 0x63677270
#endif

/*  Default lower bound on memory limit in MB. This is required so we
 *   don't immediately kill slurmstepd on mem cgroup creation if
 *   an administrator or user sets and absurdly low mem limit.
 */
#define XCGROUP_DEFAULT_MIN_RAM 30

/* Current supported cgroup controller types */
typedef enum {
	CG_TRACK,
	CG_CPUS,
	CG_MEMORY,
	CG_DEVICES,
	CG_CPUACCT,
	CG_CTL_CNT
} cgroup_ctl_type_t;

/* Current supported cgroup controller features */
typedef enum {
	CG_MEMCG_SWAP
} cgroup_ctl_feature_t;

typedef enum {
	CG_LEVEL_ROOT,
	CG_LEVEL_SLURM,
	CG_LEVEL_USER,
	CG_LEVEL_JOB,
	CG_LEVEL_STEP,
	CG_LEVEL_STEP_SLURM,
	CG_LEVEL_STEP_USER,
	CG_LEVEL_TASK,
	CG_LEVEL_SYSTEM,
	CG_LEVEL_CNT
} cgroup_level_t;

/* This data type is used to get/set various parameters in cgroup hierarchy */
typedef struct {
	/* extra info */
	stepd_step_rec_t *step;
	uint32_t taskid;
	/* task cpuset */
	char *allow_cores;
	char *allow_mems;
	size_t cores_size;
	size_t mems_size;
	/* task devices */
	bool allow_device;
	gres_device_id_t device;
	/* jobacct memory */
	uint64_t limit_in_bytes;
	uint64_t soft_limit_in_bytes;
	uint64_t kmem_limit_in_bytes;
	uint64_t memsw_limit_in_bytes;
	uint64_t swappiness;
} cgroup_limits_t;

typedef struct {
	uint64_t step_mem_failcnt;
	uint64_t step_memsw_failcnt;
	uint64_t job_mem_failcnt;
	uint64_t job_memsw_failcnt;
	uint64_t oom_kill_cnt;
} cgroup_oom_t;

typedef struct {
	uint64_t usec;
	uint64_t ssec;
	uint64_t total_rss;
	uint64_t total_pgmajfault;
	uint64_t total_vmem;
} cgroup_acct_t;

/* Slurm cgroup plugins configuration parameters */
typedef struct {
	bool cgroup_automount;
	char *cgroup_mountpoint;

	char *cgroup_prepend;

	bool constrain_cores;

	bool constrain_ram_space;
	float allowed_ram_space;
	float max_ram_percent;		/* Upper bound on memory as % of RAM */

	uint64_t min_ram_space;		/* Lower bound on memory limit (MB) */

	bool constrain_kmem_space;
	float allowed_kmem_space;
	float max_kmem_percent;
	uint64_t min_kmem_space;

	bool constrain_swap_space;
	float allowed_swap_space;
	float max_swap_percent;		/* Upper bound on swap as % of RAM  */
	uint64_t memory_swappiness;

	bool constrain_devices;
	char *cgroup_plugin;

	bool ignore_systemd;
	bool ignore_systemd_on_failure;

	bool root_owned_cgroups;
	bool enable_controllers;
} cgroup_conf_t;


extern cgroup_conf_t slurm_cgroup_conf;

/* global functions */
extern int cgroup_conf_init(void);
extern void cgroup_conf_destroy(void);
extern void cgroup_conf_reinit(void);
extern void cgroup_free_limits(cgroup_limits_t *limits);
extern void cgroup_init_limits(cgroup_limits_t *limits);
extern List cgroup_get_conf_list(void);
extern int cgroup_write_conf(int fd);
extern int cgroup_read_conf(int fd);
extern bool cgroup_memcg_job_confinement(void);
extern char *autodetect_cgroup_version(void);

/* global plugin functions */
extern int cgroup_g_init(void);
extern int cgroup_g_fini(void);

/*
 * Create the cgroup namespace and the root cgroup objects. This two entities
 * are the basic ones used by any other function and contain information about
 * the cg paths, mount points, name, ownership, and so on. The creation of the
 * root namespace may involve also automounting the cgroup subsystem. Set also
 * any specific required parameter on the root cgroup depending on the
 * controller.
 *
 * In cgroup/v1 a subsystem is a synonym for cgroup controller.
 *
 * IN sub - Controller to initialize.
 * RET SLURM_SUCCESS or error
 */
extern int cgroup_g_initialize(cgroup_ctl_type_t sub);

/*
 * Create the system directories for the specified controller and set any
 * required parameters. These directories are the ones where slurmd will
 * be put if CoreSpecLimit, MemSpecLimit or CoreSpecCnt are set in slurm.conf.
 * Current supported controllers are only cpuset and memory.
 *
 * IN sub - Controller to initialize.
 * RET SLURM_SUCCESS or error
 */
extern int cgroup_g_system_create(cgroup_ctl_type_t sub);

/*
 * Add pids to the system cgroups. Typically these pids will be slurmstepd pids.
 *
 * IN sub - To which controller will the pids be added.
 * IN pids - Array of pids to add.
 * IN npids - Count of pids in the array.
 * RET SLURM_SUCCESS if pids were correctly added or SLURM_ERROR otherwise.
 */
extern int cgroup_g_system_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);

/*
 * rmdir the system cgroup controller and destroy the cgroup global objects.
 * In v1 it will move our pid first to the root cgroup, otherwise removal would
 * return EBUSY.
 *
 * IN sub - Which controller will be destroyed.
 * RET SLURM_SUCCESS if destroy was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_g_system_destroy(cgroup_ctl_type_t sub);

/*
 * Create the directories for a job step in the given controller, set also any
 * needed default parameters. Initialize also the step cgroup objects.
 * Every controller may have its own specific settings. This function is called
 * from a slurmstepd only once. Record also that we're using this step object.
 *
 * IN sub - Under which controller will the directory hierarchy be created.
 * IN job - Step record which is used to create the path in the hierarchy.
 * RET SLURM_SUCCESS if creation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_g_step_create(cgroup_ctl_type_t sub, stepd_step_rec_t *step);

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
extern int cgroup_g_step_addto(cgroup_ctl_type_t sub, pid_t *pids, int npids);

/*
 * Get the pids under the freezer controller for this step.
 *
 * OUT pids - Array of pids containing the pids in this step.
 * OUT npids - Count of pids in the array.
 * RET SLURM_SUCCESS if pids were correctly obtained, SLURM_ERROR otherwise.
 */
extern int cgroup_g_step_get_pids(pid_t **pids, int *npids);

/*
 * Suspend the step using the freezer controller.
 *
 * RET SLURM_SUCCESS if operation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_g_step_suspend(void);

/*
 * Resume the step using the freezer controller.
 *
 * RET SLURM_SUCCESS if operation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_g_step_resume(void);

/*
 * If the caller (typically from a plugin) is the only one using this step
 * object, rmdir the controller's step directories and destroy the associated
 * cgroup objects. Decrement the step object's active usage count.
 *
 * IN sub - Which controller will be destroyed for this step.
 * RET SLURM_SUCCESS if operation was successful, SLURM_ERROR otherwise.
 */
extern int cgroup_g_step_destroy(cgroup_ctl_type_t sub);

/*
 * Given a pid, determine if this pid is being tracked by the freezer container.
 *
 * RET true if pid was found, false in any other case.
 */
extern bool cgroup_g_has_pid(pid_t pid);

/*
 * Obtain the constrains set to the cgroup of the specified controller.
 *
 * IN sub - From which controller we want the limits.
 * IN level - Directory level to get the info from.
 * RET cgroup_limits_t object if limits could be obtained, NULL otherwise.
 */
extern cgroup_limits_t *cgroup_g_constrain_get(cgroup_ctl_type_t sub,
					       cgroup_level_t type);

/*
 * Set constrains to the root cgroup of the specified controller.
 *
 * IN sub - To which controller we want the limits be applied to.
 * IN level - Directory level to apply the limits to.
 * IN limits - Struct containing the the limits to be applied.
 * RET SLURM_SUCCESS if limits were applied successfuly, SLURM_ERROR otherwise.
 */
extern int cgroup_g_constrain_set(cgroup_ctl_type_t sub, cgroup_level_t level,
				  cgroup_limits_t *limits);

/*
 * This function is only needed in v2, in v1 will always return SLURM_SUCCESS
 */
extern int cgroup_g_constrain_apply(cgroup_ctl_type_t sub, cgroup_level_t level,
                                    uint32_t task_id);

/*
 * Function to detect OOM conditions.
 *
 * In v2 it will just read memory.oom_control.
 *
 * In v1, use memory.oom_control and cgroup.event_control, see:
 * https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt
 *
 * In v1, Start a monitoring thread which will read the event files with a
 * polling mechanism and wait for a stop signal. When the stop signal is
 * received this thread will communicate the detected OOMs. This is not a 100%
 * reliable method since events can be triggered with more than just OOMs, e.g.
 * rmdirs.
 *
 * RET SLURM_SUCCESS if monitoring thread is started, SLURM_ERROR otherwise.
 */
extern int cgroup_g_step_start_oom_mgr(void);

/*
 * Signal the monitoring thread with a stop message and get the results.
 *
 * IN job - Step record.
 * RET cgroup_oom_t - Struct containing the oom information for this step.
 */
extern cgroup_oom_t *cgroup_g_step_stop_oom_mgr(stepd_step_rec_t *step);

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
extern int cgroup_g_task_addto(cgroup_ctl_type_t sub, stepd_step_rec_t *step,
			       pid_t pid, uint32_t task_id);

/*
 * Given a task id return the accounting data reading the accounting controller
 * files for this step.
 *
 * IN task_id - task number we want the data from, for the current step.
 * RET cgroup_acct_t - struct containing the required data.
 */
extern cgroup_acct_t *cgroup_g_task_get_acct_data(uint32_t taskid);

/*
 * Return conversion units used for stats gathered from cpuacct.
 * Dividing the provided data by this number will give seconds.
 *
 * RET hertz - USER_HZ of the system.
 */
extern long int cgroup_g_get_acct_units();

/*
 * Check if Cgroup has this feature available.
 * Usually this will depend on the kernel config settings or the boot flags,
 * and since checks can be done by slurmd before init, we are checking it
 * directly from the root.
 */
extern bool cgroup_g_has_feature(cgroup_ctl_feature_t f);
#endif
