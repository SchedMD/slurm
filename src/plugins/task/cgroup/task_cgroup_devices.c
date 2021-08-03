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
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "task_cgroup.h"

typedef struct {
	stepd_step_rec_t *job;
} task_memory_create_callback_t;

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];
static char cgroup_allowed_devices_file[PATH_MAX];

static xcgroup_ns_t devices_ns;

static xcgroup_t user_devices_cg;
static xcgroup_t job_devices_cg;
static xcgroup_t step_devices_cg;

static void _calc_device_major(char *dev_path[PATH_MAX],
			       char *dev_major[PATH_MAX],
			       int lines);

static int _read_allowed_devices_file(char **allowed_devices);

extern int task_cgroup_devices_init(void)
{
	uint16_t cpunum;
	FILE *file = NULL;
	slurm_cgroup_conf_t *cg_conf;

	/* initialize cpuinfo internal data */
	if (xcpuinfo_init() != XCPUINFO_SUCCESS)
		return SLURM_ERROR;

	/* initialize user/job/jobstep cgroup relative paths */
	user_cgroup_path[0] = '\0';
	job_cgroup_path[0] = '\0';
	jobstep_cgroup_path[0] = '\0';
	/* initialize allowed_devices_filename */
	cgroup_allowed_devices_file[0] = '\0';

	if (get_procs(&cpunum) != 0) {
		error("unable to get a number of CPU");
		goto error;
	}

	/* read cgroup configuration */
	slurm_mutex_lock(&xcgroup_config_read_mutex);
	cg_conf = xcgroup_get_slurm_cgroup_conf();

	if ((strlen(cg_conf->allowed_devices_file) + 1) >= PATH_MAX) {
		error("device file path length exceeds limit: %s",
		      cg_conf->allowed_devices_file);
		slurm_mutex_unlock(&xcgroup_config_read_mutex);
		goto error;
	}
	strcpy(cgroup_allowed_devices_file, cg_conf->allowed_devices_file);
	slurm_mutex_unlock(&xcgroup_config_read_mutex);
	if (xcgroup_ns_create(&devices_ns, "", "devices")
	    != XCGROUP_SUCCESS ) {
		error("unable to create devices namespace");
		goto error;
	}

	file = fopen(cgroup_allowed_devices_file, "r");
	if (!file) {
		debug("unable to open %s: %m",
		      cgroup_allowed_devices_file);
	} else
		fclose(file);

	return SLURM_SUCCESS;

error:
	xcgroup_ns_destroy(&devices_ns);
	xcpuinfo_fini();
	return SLURM_ERROR;
}

extern int task_cgroup_devices_fini(void)
{
	xcgroup_t devices_cg;

	/* Similarly to task_cgroup_{memory,cpuset}_fini(), we must lock the
	 * root cgroup so we don't race with another job step that is
	 * being started.  */
        if (xcgroup_create(&devices_ns, &devices_cg,"",0,0)
	    == XCGROUP_SUCCESS) {
                if (xcgroup_lock(&devices_cg) == XCGROUP_SUCCESS) {
			/* First move slurmstepd to the root devices cg
			 * so we can remove the step/job/user devices
			 * cg's.  */
			xcgroup_move_process(&devices_cg, getpid());

			xcgroup_wait_pid_moved(&step_devices_cg,
					       "devices step");

			if (xcgroup_delete(&step_devices_cg) != SLURM_SUCCESS)
                                debug2("unable to remove step "
                                       "devices : %m");
                        if (xcgroup_delete(&job_devices_cg) != XCGROUP_SUCCESS)
                                debug2("not removing "
                                       "job devices : %m");
                        if (xcgroup_delete(&user_devices_cg)
			    != XCGROUP_SUCCESS)
                                debug2("not removing "
                                       "user devices : %m");
                        xcgroup_unlock(&devices_cg);
                } else
                        error("unable to lock root devices : %m");
                xcgroup_destroy(&devices_cg);
        } else
                error("unable to create root devices : %m");

	if ( user_cgroup_path[0] != '\0' )
		xcgroup_destroy(&user_devices_cg);
	if ( job_cgroup_path[0] != '\0' )
		xcgroup_destroy(&job_devices_cg);
	if ( jobstep_cgroup_path[0] != '\0' )
		xcgroup_destroy(&step_devices_cg);

	user_cgroup_path[0] = '\0';
	job_cgroup_path[0] = '\0';
	jobstep_cgroup_path[0] = '\0';

	cgroup_allowed_devices_file[0] = '\0';

	xcgroup_ns_destroy(&devices_ns);

	xcpuinfo_fini();
	return SLURM_SUCCESS;
}

static int _handle_device_access(void *x, void *arg)
{
	gres_device_t *gres_device = (gres_device_t *)x;
	xcgroup_t *devices_cg = (xcgroup_t *)arg;
	char *cg;

	if (gres_device->alloc)
		cg = "devices.allow";
	else
		cg = "devices.deny";

	log_flag(GRES, "%s %s: adding %s(%s)",
		 (devices_cg == &job_devices_cg) ? "job " : "step",
		 cg, gres_device->major, gres_device->path);
	xcgroup_set_param(devices_cg, cg, gres_device->major);

	return SLURM_SUCCESS;
}

static int _cgroup_create_callback(const char *calling_func,
				   xcgroup_ns_t *ns,
				   void *callback_arg)
{
	task_memory_create_callback_t *cgroup_callback =
		(task_memory_create_callback_t *)callback_arg;
	stepd_step_rec_t *job = cgroup_callback->job;
	pid_t pid;
	List job_gres_list = job->job_gres_list;
	List step_gres_list = job->step_gres_list;
	List device_list = NULL;
	char *allowed_devices[PATH_MAX], *allowed_dev_major[PATH_MAX];
	int k, rc, allow_lines = 0;

	/*
         * create the entry with major minor for the default allowed devices
         * read from the file
         */
	allow_lines = _read_allowed_devices_file(allowed_devices);
	_calc_device_major(allowed_devices, allowed_dev_major, allow_lines);

	/*
	 * with the current cgroup devices subsystem design (whitelist only
	 * supported) we need to allow all different devices that are supposed
	 * to be allowed by* default.
	 */
	for (k = 0; k < allow_lines; k++) {
		debug2("Default access allowed to device %s(%s) for job",
		       allowed_dev_major[k], allowed_devices[k]);
		xcgroup_set_param(&job_devices_cg, "devices.allow",
				  allowed_dev_major[k]);
	}

	/*
         * allow or deny access to devices according to job GRES permissions
         */
	device_list = gres_plugin_get_allocated_devices(job_gres_list, true);

	if (device_list) {
		list_for_each(device_list, _handle_device_access,
			      &job_devices_cg);
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
			xcgroup_set_param(&step_devices_cg, "devices.allow",
					  allowed_dev_major[k]);
		}

		/*
		 * allow or deny access to devices according to GRES permissions
		 * for the step
		 */
		device_list = gres_plugin_get_allocated_devices(
			step_gres_list, false);

		if (device_list) {
			list_for_each(device_list, _handle_device_access,
				      &step_devices_cg);
			FREE_NULL_LIST(device_list);
		}
	}

	for (k = 0; k < allow_lines; k++) {
		xfree(allowed_dev_major[k]);
		xfree(allowed_devices[k]);
	}

	/* attach the slurmstepd to the step devices cgroup */
	pid = getpid();
	rc = xcgroup_add_pids(&step_devices_cg, &pid, 1);
	if (rc != XCGROUP_SUCCESS) {
		error("%s: unable to add slurmstepd to devices cg '%s'",
		      calling_func, step_devices_cg.path);
		rc = SLURM_ERROR;
	} else {
		rc = SLURM_SUCCESS;
	}

	return rc;
}

extern int task_cgroup_devices_create(stepd_step_rec_t *job)
{
	task_memory_create_callback_t cgroup_callback = {
		.job = job,
	};

	return xcgroup_create_hierarchy(__func__,
					job,
					&devices_ns,
					&job_devices_cg,
					&step_devices_cg,
					&user_devices_cg,
					job_cgroup_path,
					jobstep_cgroup_path,
					user_cgroup_path,
					_cgroup_create_callback,
					&cgroup_callback);
}

extern int task_cgroup_devices_attach_task(stepd_step_rec_t *job)
{
	int fstatus = SLURM_ERROR;

	/* tasks are automatically attached as slurmstepd is in the step cg */
	fstatus = SLURM_SUCCESS;

	return fstatus;
}

static void _calc_device_major(char *dev_path[PATH_MAX],
			       char *dev_major[PATH_MAX],
			       int lines)
{

	int k;

	if (lines > PATH_MAX) {
		error("more devices configured than table size "
		      "(%d > %d)", lines, PATH_MAX);
		lines = PATH_MAX;
	}
	for (k = 0; k < lines; k++)
		dev_major[k] = gres_device_major(dev_path[k]);
}


static int _read_allowed_devices_file(char **allowed_devices)
{

	FILE *file = fopen(cgroup_allowed_devices_file, "r");
	int i, l, num_lines = 0;
	char line[256];
	glob_t globbuf;

	for( i=0; i<256; i++ )
		line[i] = '\0';

	if ( file != NULL ){
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
	}

	return num_lines;
}


extern int task_cgroup_devices_add_pid(pid_t pid)
{
	return xcgroup_add_pids(&step_devices_cg, &pid, 1);
}
