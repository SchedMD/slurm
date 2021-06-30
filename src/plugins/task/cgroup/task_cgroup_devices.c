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

#ifdef MAJOR_IN_MKDEV
#  include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#  include <sys/sysmacros.h>
#endif

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
	bool is_step;
	stepd_step_rec_t *job;
} handle_dev_args_t;

static char cgroup_allowed_devices_file[PATH_MAX];

static int _handle_device_access(void *x, void *arg)
{
	gres_device_t *gres_device = (gres_device_t *)x;
	handle_dev_args_t *handle_args = (handle_dev_args_t *)arg;
	cgroup_limits_t limits;

	log_flag(GRES, "%s %s: adding %s(%s)",
		 handle_args->is_step ? "step" : "job",
		 gres_device->alloc ? "devices.allow" : "devices.deny",
		 gres_device->major, gres_device->path);

	memset(&limits, 0, sizeof(limits));
	limits.allow_device = gres_device->alloc;
	limits.device_major = gres_device->major;

	if (!handle_args->is_step)
		cgroup_g_job_constrain_set(CG_DEVICES, handle_args->job,
					   &limits);
	else
		cgroup_g_step_constrain_set(CG_DEVICES, handle_args->job,
					    &limits);


	return SLURM_SUCCESS;
}

static void _calc_device_major(char *dev_path[PATH_MAX],
			       char *dev_major[PATH_MAX], int lines)
{
	int k;

	if (lines > PATH_MAX) {
		error("more devices configured than table size (%d > %d)",
		      lines, PATH_MAX);
		lines = PATH_MAX;
	}

	for (k = 0; k < lines; k++)
		dev_major[k] = gres_device_major(dev_path[k]);
}

static int _read_allowed_devices_file(char **allowed_devices)
{
	FILE *file;
	int i, l, num_lines = 0;
	char line[256];
	glob_t globbuf;

	file = fopen(cgroup_allowed_devices_file, "r");

	if (file == NULL)
		return num_lines;

	for (i = 0; i < 256; i++)
		line[i] = '\0';

	while (fgets(line, sizeof(line), file)) {
		line[strlen(line)-1] = '\0';
		/* global pattern matching and return the list of matches*/
		if (glob(line, GLOB_NOSORT, NULL, &globbuf)) {
			debug3("Device %s does not exist", line);
		} else {
			for (l=0; l < globbuf.gl_pathc; l++) {
				allowed_devices[num_lines] =
					xstrdup(globbuf.gl_pathv[l]);
				num_lines++;
			}
			globfree(&globbuf);
		}
	}
	fclose(file);

	return num_lines;
}

extern int task_cgroup_devices_init(void)
{
	uint16_t cpunum;
	FILE *file = NULL;
	cgroup_conf_t *cg_conf;

	/* initialize cpuinfo internal data */
	if (xcpuinfo_init() != XCPUINFO_SUCCESS)
		return SLURM_ERROR;

	/* initialize allowed_devices_filename */
	cgroup_allowed_devices_file[0] = '\0';

	if (get_procs(&cpunum) != 0) {
		error("unable to get a number of CPU");
		goto error;
	}

	/* read cgroup configuration */
	cg_conf = cgroup_get_conf();

	if (!cg_conf)
		goto error;

	if ((strlen(cg_conf->allowed_devices_file) + 1) >= PATH_MAX) {
		error("device file path length exceeds limit: %s",
		      cg_conf->allowed_devices_file);
		goto error;
	}
	strcpy(cgroup_allowed_devices_file, cg_conf->allowed_devices_file);

	if (cgroup_g_initialize(CG_DEVICES) != SLURM_SUCCESS) {
		error("unable to create devices namespace");
		goto error;
	}

	file = fopen(cgroup_allowed_devices_file, "r");
	if (!file) {
		debug("unable to open %s: %m", cgroup_allowed_devices_file);
	} else
		fclose(file);

	return SLURM_SUCCESS;

error:
	xfree(cg_conf);
	xcpuinfo_fini();
	return SLURM_ERROR;
}

extern int task_cgroup_devices_fini(void)
{
	cgroup_g_step_destroy(CG_DEVICES);
	cgroup_allowed_devices_file[0] = '\0';
	xcpuinfo_fini();
	return SLURM_SUCCESS;
}

extern int task_cgroup_devices_create(stepd_step_rec_t *job)
{
	int k, allow_lines = 0;
	List job_gres_list = job->job_gres_list;
	List step_gres_list = job->step_gres_list;
	List device_list = NULL;
	char *allowed_devices[PATH_MAX], *allowed_dev_major[PATH_MAX];
	cgroup_limits_t limits;
	handle_dev_args_t handle_args;

	if (cgroup_g_step_create(CG_DEVICES, job) != SLURM_SUCCESS)
		return SLURM_ERROR;

	/*
         * create the entry with major minor for the default allowed devices
         * read from the file
         */
	allow_lines = _read_allowed_devices_file(allowed_devices);
	_calc_device_major(allowed_devices, allowed_dev_major, allow_lines);

	/* Prepare limits to constrain devices to job and step */
	memset(&limits, 0, sizeof(limits));
	limits.allow_device = true;

	/*
	 * with the current cgroup devices subsystem design (whitelist only
	 * supported) we need to allow all different devices that are supposed
	 * to be allowed by* default.
	 */
	for (k = 0; k < allow_lines; k++) {
		debug2("Default access allowed to device %s(%s) for job",
		       allowed_dev_major[k], allowed_devices[k]);
		limits.device_major = allowed_dev_major[k];
		cgroup_g_job_constrain_set(CG_DEVICES, job, &limits);
		limits.device_major = NULL;
	}

	/*
         * allow or deny access to devices according to job GRES permissions
         */
	device_list = gres_g_get_devices(job_gres_list, true);

	if (device_list) {
		handle_args.is_step = false;
		handle_args.job = job;
		list_for_each(device_list, _handle_device_access,
			      &handle_args);
		FREE_NULL_LIST(device_list);
	}

	if ((job->step_id.step_id != SLURM_BATCH_SCRIPT) &&
	    (job->step_id.step_id != SLURM_EXTERN_CONT) &&
	    (job->step_id.step_id != SLURM_INTERACTIVE_STEP)) {
		/*
		 * with the current cgroup devices subsystem design (whitelist
		 * only supported) we need to allow all different devices that
		 * are supposed to be allowed by default.
		 */
		for (k = 0; k < allow_lines; k++) {
			debug2("Default access allowed to device %s(%s) for step",
			       allowed_dev_major[k], allowed_devices[k]);
			limits.device_major = allowed_dev_major[k];
			cgroup_g_step_constrain_set(CG_DEVICES, job, &limits);
			limits.device_major = NULL;
		}

		/*
		 * allow or deny access to devices according to GRES permissions
		 * for the step
		 */
		device_list = gres_g_get_devices(step_gres_list, false);

		if (device_list) {
			handle_args.is_step = true;
			handle_args.job = job;
			list_for_each(device_list, _handle_device_access,
				      &handle_args);
			FREE_NULL_LIST(device_list);
		}
	}

	for (k = 0; k < allow_lines; k++) {
		xfree(allowed_dev_major[k]);
		xfree(allowed_devices[k]);
	}

	return SLURM_SUCCESS;
}

extern int task_cgroup_devices_attach_task(stepd_step_rec_t *job)
{
	/* tasks are automatically attached as slurmstepd is in the step cg */
	return SLURM_SUCCESS;
}

extern int task_cgroup_devices_add_pid(pid_t pid)
{
	return cgroup_g_step_addto(CG_DEVICES, &pid, 1);
}
