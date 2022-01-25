/***************************************************************************** \
 *  task_cgroup_devices.c - devices cgroup subsystem for task/cgroup
 *****************************************************************************
 *  Copyright (C) 2011 BULL
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.fr>
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

#define _GNU_SOURCE
#include <glob.h>
#include <limits.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/xstring.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/cgroup.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "task_cgroup.h"

typedef struct handle_dev_args {
	cgroup_level_t cgroup_type;
	uint32_t taskid;
	stepd_step_rec_t *job;
} handle_dev_args_t;

static bool is_first_task = true;

static int _handle_device_access(void *x, void *arg)
{
	gres_device_t *gres_device = (gres_device_t *)x;
	handle_dev_args_t *handle_args = (handle_dev_args_t *)arg;
	cgroup_limits_t limits;
	char *dev_id_str;
	int rc = SLURM_SUCCESS;

	dev_id_str = gres_device_id2str(&gres_device->dev_desc);
	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES) {
		char *t_str = NULL;
		switch (handle_args->cgroup_type) {
		case CG_LEVEL_TASK:
			t_str = xstrdup_printf("task_%d", handle_args->taskid);
			break;
		case CG_LEVEL_JOB:
			t_str = xstrdup("job");
			break;
		case CG_LEVEL_STEP:
			t_str = xstrdup("step");
			break;
		default:
			t_str = xstrdup("unknown");
			break;
		}
		log_flag(GRES, "%s %s: adding %s(%s)",
			 t_str,
			 gres_device->alloc ? "devices.allow" : "devices.deny",
			 dev_id_str, gres_device->path);
		xfree(t_str);
	}

	cgroup_init_limits(&limits);
	limits.allow_device = gres_device->alloc;
	limits.device = gres_device->dev_desc;
	limits.taskid = handle_args->taskid;

	if (cgroup_g_constrain_set(CG_DEVICES, handle_args->cgroup_type,
				   &limits) != SLURM_SUCCESS) {
		error("Unable to set access constraint for device %s(%s)",
		      dev_id_str, gres_device->path);
		rc = SLURM_ERROR;
	}

	xfree(dev_id_str);
	return rc;
}

extern int task_cgroup_devices_init(void)
{
	uint16_t cpunum;

	/* initialize cpuinfo internal data */
	if (xcpuinfo_init() != SLURM_SUCCESS)
		return SLURM_ERROR;

	if (get_procs(&cpunum) != 0) {
		error("unable to get a number of CPU");
		goto error;
	}

	if (cgroup_g_initialize(CG_DEVICES) != SLURM_SUCCESS) {
		error("unable to create devices namespace");
		goto error;
	}

	return SLURM_SUCCESS;

error:
	xcpuinfo_fini();
	return SLURM_ERROR;
}

extern int task_cgroup_devices_fini(void)
{
	int rc;

	rc = cgroup_g_step_destroy(CG_DEVICES);
	xcpuinfo_fini();

	return rc;
}

extern int task_cgroup_devices_create(stepd_step_rec_t *job)
{
	int rc = SLURM_SUCCESS;
	pid_t pid;
	List job_gres_list = job->job_gres_list;
	List step_gres_list = job->step_gres_list;
	List device_list = NULL;
	handle_dev_args_t handle_args;

	if (is_first_task) {
		/* Only do once in this plugin. */
		if (cgroup_g_step_create(CG_DEVICES, job) != SLURM_SUCCESS)
			return SLURM_ERROR;
		is_first_task = false;
	}

	/* Allow or deny access to devices according to job GRES permissions. */
	device_list = gres_g_get_devices(job_gres_list, true, 0, NULL, 0, 0);

	if (device_list) {
		int tmp;

		handle_args.cgroup_type = CG_LEVEL_JOB;
		handle_args.job = job;
		tmp = list_for_each(device_list, _handle_device_access,
				    &handle_args);
		FREE_NULL_LIST(device_list);
		if (tmp < 0) {
			rc = SLURM_ERROR;
			goto fini;
		}
		cgroup_g_constrain_apply(CG_DEVICES, CG_LEVEL_JOB, NO_VAL);
	}

	if ((job->step_id.step_id != SLURM_BATCH_SCRIPT) &&
	    (job->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (job->step_id.step_id != SLURM_INTERACTIVE_STEP)) {
		/*
		 * Allow or deny access to devices according to GRES permissions
		 * for the step.
		 */
		device_list = gres_g_get_devices(step_gres_list, false, 0, NULL,
						 0, 0);

		if (device_list) {
			int tmp;

			handle_args.cgroup_type = CG_LEVEL_STEP;
			handle_args.job = job;
			tmp = list_for_each(device_list, _handle_device_access,
					    &handle_args);
			FREE_NULL_LIST(device_list);
			if (tmp < 0) {
				rc = SLURM_ERROR;
				goto fini;
			}
			cgroup_g_constrain_apply(CG_DEVICES, CG_LEVEL_STEP,
						 NO_VAL);
		}
	}

	/* attach the slurmstepd to the step devices cgroup */
	pid = getpid();
	rc = cgroup_g_step_addto(CG_DEVICES, &pid, 1);

fini:
	return rc;
}

extern int task_cgroup_devices_add_pid(stepd_step_rec_t *job, pid_t pid,
				       uint32_t taskid)
{
	/* This plugin constrain devices to task level. */
	return cgroup_g_task_addto(CG_DEVICES, job, pid, taskid);
}

extern int task_cgroup_devices_constrain(stepd_step_rec_t *job, pid_t pid,
					 uint32_t taskid)
{
	List device_list = NULL;
	handle_dev_args_t handle_args;

	/*
	 * We do not explicitly constrain devices on the task level of these
	 * specific steps (they all only have 1 task anyway). e.g. an
	 * salloc --gres=gpu must have access to the allocated GPUs. If we do
	 * add the pid (e.g. bash) we'd get constrained.
	 */
	if ((job->step_id.step_id == SLURM_BATCH_SCRIPT) ||
	    (job->step_id.step_id == SLURM_EXTERN_CONT) ||
	    (job->step_id.step_id == SLURM_INTERACTIVE_STEP))
		return SLURM_SUCCESS;

	/*
	 * Apply gres constrains by getting the allowed devices for this task
	 * from gres plugin.
	 */
	device_list = gres_g_get_devices(job->step_gres_list, false,
					 job->accel_bind_type, job->tres_bind,
					 taskid, pid);
	if (device_list) {
		int tmp;

		handle_args.cgroup_type = CG_LEVEL_TASK;
		handle_args.job = job;
		handle_args.taskid = taskid;
		tmp = list_for_each(device_list, _handle_device_access,
				    &handle_args);
		FREE_NULL_LIST(device_list);
		if (tmp < 0)
			return SLURM_ERROR;

                cgroup_g_constrain_apply(CG_DEVICES, CG_LEVEL_TASK, taskid);
	}

	return SLURM_SUCCESS;
}

extern int task_cgroup_devices_add_extern_pid(pid_t pid)
{
	/* Only in the extern step we will not create specific tasks */
	return cgroup_g_step_addto(CG_DEVICES, &pid, 1);
}
