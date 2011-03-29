/*****************************************************************************\
 *  proctrack_cgroup.c - process tracking via linux cgroup containers
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

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if HAVE_STDINT_H
#include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/slurmd/slurmd/slurmd.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "src/common/xcgroup_read_config.h"
#include "src/common/xcgroup.h"
#include "src/common/xcpuinfo.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

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
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum version for their plugins as the job completion logging API
 * matures.
 */
const char plugin_name[]      = "Process tracking via linux "
	"cgroup freezer subsystem";
const char plugin_type[]      = "proctrack/cgroup";
const uint32_t plugin_version = 10;

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

static slurm_cgroup_conf_t slurm_cgroup_conf;

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];
static char release_agent_path[PATH_MAX];

static xcgroup_ns_t freezer_ns;

static xcgroup_t user_freezer_cg;
static xcgroup_t job_freezer_cg;
static xcgroup_t step_freezer_cg;

int _slurm_cgroup_init()
{
	/* initialize user/job/jobstep cgroup relative paths
	 * and release agent path */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';
	release_agent_path[0]='\0';

	/* build freezer release agent path */
	if (snprintf(release_agent_path, PATH_MAX, "%s/release_freezer",
		      slurm_cgroup_conf.cgroup_release_agent) >= PATH_MAX) {
		error("unable to build cgroup freezer release agent path");
		return SLURM_ERROR;
	}

	/* initialize freezer cgroup namespace */
	if (xcgroup_ns_create(&freezer_ns, CGROUP_BASEDIR "/freezer", "",
			       "freezer", release_agent_path)
	     != XCGROUP_SUCCESS) {
		error("unable to create freezer cgroup namespace");
		return SLURM_ERROR;
	}

	/* check that freezer cgroup namespace is available */
	if (! xcgroup_ns_is_available(&freezer_ns)) {
		if (slurm_cgroup_conf.cgroup_automount) {
			if (xcgroup_ns_mount(&freezer_ns)) {
				error("unable to mount freezer cgroup"
				      " namespace");
				return SLURM_ERROR;
			}
			info("cgroup namespace '%s' is now mounted", "freezer");
		}
		else {
			error("cgroup namespace '%s' not mounted. aborting",
			      "freezer");
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

int _slurm_cgroup_create(slurmd_job_t *job, uint32_t id, uid_t uid, gid_t gid)
{
	/* build user cgroup relative path if not set (should not be) */
	if (*user_cgroup_path == '\0') {
		if (snprintf(user_cgroup_path, PATH_MAX,
			      "/uid_%u", uid) >= PATH_MAX) {
			error("unable to build uid %u cgroup relative "
			      "path : %m", uid);
			return SLURM_ERROR;
		}
	}

	/* build job cgroup relative path if no set (should not be) */
	if (*job_cgroup_path == '\0') {
		if (snprintf(job_cgroup_path, PATH_MAX, "%s/job_%u",
			      user_cgroup_path, job->jobid) >= PATH_MAX) {
			error("unable to build job %u cgroup relative "
			      "path : %m", job->jobid);
			return SLURM_ERROR;
		}
	}

	/* build job step cgroup relative path (should not be) */
	if (*jobstep_cgroup_path == '\0') {
		if (job->stepid == NO_VAL) {
			if (snprintf(jobstep_cgroup_path, PATH_MAX,
				     "%s/step_batch", job_cgroup_path)
			    >= PATH_MAX) {
				error("proctrack/cgroup unable to build job step"
				      " %u.batch freezer cg relative path: %m",
				      job->jobid);
				return SLURM_ERROR;
			}
		} else {
			if (snprintf(jobstep_cgroup_path, PATH_MAX, "%s/step_%u",
				     job_cgroup_path, job->stepid) >= PATH_MAX) {
				error("proctrack/cgroup unable to build job step"
				      " %u.%u freezer cg relative path: %m",
				      job->jobid, job->stepid);
				return SLURM_ERROR;
			}
		}
	}

	/* create user cgroup in the freezer ns (it could already exist) */
	if (xcgroup_create(&freezer_ns, &user_freezer_cg,
			    user_cgroup_path,
			    getuid(), getgid()) != XCGROUP_SUCCESS) {
		return SLURM_ERROR;
	}
	if (xcgroup_instanciate(&user_freezer_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_freezer_cg);

		return SLURM_ERROR;
	}

	/* create job cgroup in the freezer ns (it could already exist) */
	if (xcgroup_create(&freezer_ns, &job_freezer_cg,
			    job_cgroup_path,
			    getuid(), getgid()) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_freezer_cg);
		return SLURM_ERROR;
	}
	if (xcgroup_instanciate(&job_freezer_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_freezer_cg);
		xcgroup_destroy(&job_freezer_cg);
		return SLURM_ERROR;
	}

	/* create step cgroup in the freezer ns (it should not exists) */
	if (xcgroup_create(&freezer_ns, &step_freezer_cg,
			    jobstep_cgroup_path,
			    getuid(), getgid()) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_freezer_cg);
		xcgroup_destroy(&job_freezer_cg);
		return SLURM_ERROR;
	}
	if (xcgroup_instanciate(&step_freezer_cg) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&user_freezer_cg);
		xcgroup_destroy(&job_freezer_cg);
		xcgroup_destroy(&step_freezer_cg);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

int _slurm_cgroup_destroy(void)
{
	if (jobstep_cgroup_path[0] != '\0') {
		xcgroup_delete(&step_freezer_cg);
		xcgroup_destroy(&step_freezer_cg);
	}

	if (job_cgroup_path[0] != '\0') {
		xcgroup_delete(&job_freezer_cg);
		xcgroup_destroy(&job_freezer_cg);
	}

	if (user_cgroup_path[0] != '\0') {
		xcgroup_delete(&user_freezer_cg);
		xcgroup_destroy(&user_freezer_cg);
	}

	return SLURM_SUCCESS;
}

int _slurm_cgroup_add_pids(uint32_t id, pid_t* pids, int npids)
{
	if (*jobstep_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_add_pids(&step_freezer_cg, pids, npids);
}

int _slurm_cgroup_stick_stepd(uint32_t id, pid_t pid)
{
	if (*job_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_add_pids(&job_freezer_cg, &pid, 1);
}

int
_slurm_cgroup_get_pids(uint32_t id, pid_t **pids, int *npids)
{
	if (*jobstep_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_get_pids(&step_freezer_cg, pids, npids);
}

int _slurm_cgroup_suspend(uint32_t id)
{
	if (*jobstep_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_set_param(&step_freezer_cg,
				 "freezer.state", "FROZEN");
}

int _slurm_cgroup_resume(uint32_t id)
{
	if (*jobstep_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_set_param(&step_freezer_cg,
				 "freezer.state", "THAWED");
}

bool
_slurm_cgroup_has_pid(pid_t pid)
{
	bool fstatus;
	xcgroup_t cg;

	fstatus = xcgroup_ns_find_by_pid(&freezer_ns, &cg, pid);
	if ( fstatus != XCGROUP_SUCCESS)
		return false;

	if (strcmp(cg.path, step_freezer_cg.path)) {
		fstatus = false;
	}
	else {
		fstatus = true;
	}

	xcgroup_destroy(&cg);
	return fstatus;
}

int
_slurm_cgroup_is_pid_a_slurm_task(uint32_t id, pid_t pid)
{
	int fstatus = -1;
	int fd;
	pid_t ppid;
	char file_path[PATH_MAX], buf[2048];

	if (snprintf(file_path, PATH_MAX, "/proc/%ld/stat",
		      (long)pid) >= PATH_MAX) {
		debug2("unable to build pid '%d' stat file: %m ", pid);
		return fstatus;
	}

	if ((fd = open(file_path, O_RDONLY)) < 0) {
		debug2("unable to open '%s' : %m ", file_path);
		return fstatus;
	}
	if (read(fd, buf, 2048) <= 0) {
		debug2("unable to read '%s' : %m ", file_path);
		close(fd);
		return fstatus;
	}
	close(fd);

	if (sscanf(buf, "%*d %*s %*s %d", &ppid) != 1) {
		debug2("unable to get ppid of pid '%d', %m", pid);
		return fstatus;
	}

	/*
	 * assume that any child of slurmstepd is a slurm task
	 * they will get all signals, inherited processes will
	 * only get SIGKILL
	 */
	if (ppid == (long) id)
		fstatus = 1;
	else
		fstatus = 0;

	return fstatus;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init (void)
{
	/* read cgroup configuration */
	if (read_slurm_cgroup_conf(&slurm_cgroup_conf))
		return SLURM_ERROR;

	/* initialize cpuinfo internal data */
	if (xcpuinfo_init() != XCPUINFO_SUCCESS) {
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
		return SLURM_ERROR;
	}

	/* initialize cgroup internal data */
	if (_slurm_cgroup_init() != SLURM_SUCCESS) {
		xcpuinfo_fini();
		free_slurm_cgroup_conf(&slurm_cgroup_conf);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini (void)
{
	_slurm_cgroup_destroy();
	xcpuinfo_fini();
	free_slurm_cgroup_conf(&slurm_cgroup_conf);
	return SLURM_SUCCESS;
}

/*
 * Uses slurmd job-step manager's pid as the unique container id.
 */
extern int slurm_container_plugin_create (slurmd_job_t *job)
{
	int fstatus;

	/* create a new cgroup for that container */
	fstatus = _slurm_cgroup_create(job, (uint32_t)job->jmgr_pid,
				       job->uid, job->gid);
	if (fstatus)
		return SLURM_ERROR;

	/* stick slurmstepd pid to the newly created job container
	 * (Note: we do not put it in the step container because this
	 * container could be used to suspend/resume tasks using freezer
	 * properties so we need to let the slurmstepd outside of
	 * this one)
	 */
	fstatus = _slurm_cgroup_stick_stepd((uint32_t)job->jmgr_pid,
					    job->jmgr_pid);
	if (fstatus) {
		_slurm_cgroup_destroy();
		return SLURM_ERROR;
	}

	/* we use slurmstepd pid as the identifier of the container
	 * the corresponding cgroup could be found using
	 * _slurm_cgroup_find_by_pid */
	job->cont_id = (uint32_t)job->jmgr_pid;

	return SLURM_SUCCESS;
}

extern int slurm_container_plugin_add (slurmd_job_t *job, pid_t pid)
{
	return _slurm_cgroup_add_pids(job->cont_id, &pid, 1);
}

extern int slurm_container_plugin_signal (uint32_t id, int signal)
{
	pid_t* pids = NULL;
	int npids;
	int i;
	int slurm_task;

	/* get all the pids associated with the step */
	if (_slurm_cgroup_get_pids(id, &pids, &npids) !=
	     SLURM_SUCCESS) {
		debug3("unable to get pids list for cont_id=%u", id);
		/* that could mean that all the processes already exit */
		/* the container so return success */
		return SLURM_SUCCESS;
	}

	/* directly manage SIGSTOP using cgroup freezer subsystem */
	if (signal == SIGSTOP) {
		xfree(pids);
		return _slurm_cgroup_suspend(id);
	}

	/* start by resuming in case of SIGKILL */
	if (signal == SIGKILL) {
		_slurm_cgroup_resume(id);
	}

	for (i = 0 ; i<npids ; i++) {
		/* do not kill slurmstepd (it should not be part
		 * of the list, but just to not forget about that ;))
		 */
		if (pids[i] == id)
			continue;

		/* only signal slurm tasks unless signal is SIGKILL */
		slurm_task = _slurm_cgroup_is_pid_a_slurm_task(id, pids[i]);
		if (slurm_task == 1 || signal == SIGKILL) {
			debug2("killing process %d (%s) with signal %d", pids[i],
			       (slurm_task==1)?"slurm_task":"inherited_task",
			       signal);
			kill(pids[i], signal);
		}
	}

	xfree(pids);

	/* resume tasks after signaling slurm tasks with SIGCONT to be sure */
	/* that SIGTSTP received at suspend time is removed */
	if (signal == SIGCONT) {
		return _slurm_cgroup_resume(id);
	}

	return SLURM_SUCCESS;
}

extern int slurm_container_plugin_destroy (uint32_t id)
{
	_slurm_cgroup_destroy();
	return SLURM_SUCCESS;
}

extern uint32_t slurm_container_plugin_find(pid_t pid)
{
	uint32_t cont_id=-1;
	/* not provided for now */
	return cont_id;
}

extern bool slurm_container_plugin_has_pid(uint32_t cont_id, pid_t pid)
{
	return _slurm_cgroup_has_pid(pid);
}

extern int slurm_container_plugin_wait(uint32_t cont_id)
{
	int delay = 1;

	if (cont_id == 0 || cont_id == 1) {
		errno = EINVAL;
		return SLURM_ERROR;
	}

	/* Spin until the container is successfully destroyed */
	while (slurm_container_plugin_destroy(cont_id) != SLURM_SUCCESS) {
		slurm_container_plugin_signal(cont_id, SIGKILL);
		sleep(delay);
		if (delay < 120) {
			delay *= 2;
		} else {
			error("Unable to destroy container %u", cont_id);
		}
	}

	return SLURM_SUCCESS;
}

extern int slurm_container_plugin_get_pids(uint32_t cont_id,
					   pid_t **pids, int *npids)
{
	return _slurm_cgroup_get_pids(cont_id, pids, npids);
}
