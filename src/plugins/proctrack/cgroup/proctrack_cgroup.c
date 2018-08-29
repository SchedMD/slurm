/*****************************************************************************\
 *  proctrack_cgroup.c - process tracking via linux cgroup containers
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xstring.h"
#include "src/slurmd/common/xcpuinfo.h"
#include "src/slurmd/common/xcgroup.h"
#include "src/slurmd/slurmd/slurmd.h"
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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobcomp" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]      = "Process tracking via linux cgroup freezer subsystem";
const char plugin_type[]      = "proctrack/cgroup";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static slurm_cgroup_conf_t slurm_cgroup_conf;

static char user_cgroup_path[PATH_MAX];
static char job_cgroup_path[PATH_MAX];
static char jobstep_cgroup_path[PATH_MAX];

static xcgroup_ns_t freezer_ns;

static bool slurm_freezer_init = false;
static xcgroup_t freezer_cg;
static xcgroup_t slurm_freezer_cg;
static xcgroup_t user_freezer_cg;
static xcgroup_t job_freezer_cg;
static xcgroup_t step_freezer_cg;

int _slurm_cgroup_init(void)
{
	/* initialize user/job/jobstep cgroup relative paths
	 * and release agent path */
	user_cgroup_path[0]='\0';
	job_cgroup_path[0]='\0';
	jobstep_cgroup_path[0]='\0';

	/* initialize freezer cgroup namespace */
	if (xcgroup_ns_create(&slurm_cgroup_conf, &freezer_ns, "", "freezer")
	    != XCGROUP_SUCCESS) {
		error("unable to create freezer cgroup namespace");
		return SLURM_ERROR;
	}

	/* initialize the root freezer cg */
	if (xcgroup_create(&freezer_ns, &freezer_cg, "", 0, 0)
	    != XCGROUP_SUCCESS) {
		error("proctrack/cgroup unable to create root freezer xcgroup");
		xcgroup_ns_destroy(&freezer_ns);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

int _slurm_cgroup_create(stepd_step_rec_t *job, uint64_t id, uid_t uid, gid_t gid)
{
	/*
	 * we do it here as we do not have access to the conf structure
	 * in libslurm (src/common/xcgroup.c)
	 */
	char *pre = (char *)xstrdup(slurm_cgroup_conf.cgroup_prepend);
#ifdef MULTIPLE_SLURMD
	if ( conf->node_name != NULL )
		xstrsubstitute(pre,"%n", conf->node_name);
	else {
		xfree(pre);
		pre = (char*) xstrdup("/slurm");
	}
#endif

	if (xcgroup_create(&freezer_ns, &slurm_freezer_cg, pre,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		xfree(pre);
		return SLURM_ERROR;
	}

	/*
	 * While creating the cgroup hierarchy of the step, lock the root
	 * cgroup directory. The same lock is hold during removal of the
	 * hierarchies of other jobs/steps. This helps to  avoid the race
	 * condition with concurrent creation/removal of the intermediate
	 * shared directories that could result in the failure of the
	 * hierarchy setup
	 */
	if (xcgroup_lock(&freezer_cg) != XCGROUP_SUCCESS) {
		error("%s: xcgroup_lock error", __func__);
		goto bail;
	}

	/* create slurm cgroup in the freezer ns (it could already exist) */
	if (xcgroup_instantiate(&slurm_freezer_cg) != XCGROUP_SUCCESS)
		goto bail;

	/* build user cgroup relative path if not set (should not be) */
	if (*user_cgroup_path == '\0') {
		if (snprintf(user_cgroup_path, PATH_MAX,
			     "%s/uid_%u", pre, uid) >= PATH_MAX) {
			error("unable to build uid %u cgroup relative path : %m",
			      uid);
			goto bail;
		}
	}
	xfree(pre);

	/* build job cgroup relative path if no set (should not be) */
	if (*job_cgroup_path == '\0') {
		if (snprintf(job_cgroup_path, PATH_MAX, "%s/job_%u",
			     user_cgroup_path, job->jobid) >= PATH_MAX) {
			error("unable to build job %u cgroup relative path : %m",
			      job->jobid);
			goto bail;
		}
	}

	/* build job step cgroup relative path (should not be) */
	if (*jobstep_cgroup_path == '\0') {
		int cc;
		if (job->stepid == SLURM_BATCH_SCRIPT) {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				      "%s/step_batch", job_cgroup_path);
		} else if (job->stepid == SLURM_EXTERN_CONT) {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				      "%s/step_extern", job_cgroup_path);
		} else {
			cc = snprintf(jobstep_cgroup_path, PATH_MAX,
				      "%s/step_%u",
				      job_cgroup_path, job->stepid);
		}
		if (cc >= PATH_MAX) {
			error("proctrack/cgroup unable to build job step %u.%u "
			      "freezer cg relative path: %m",
			      job->jobid, job->stepid);
			goto bail;
		}
	}

	/* create user cgroup in the freezer ns (it could already exist) */
	if (xcgroup_create(&freezer_ns, &user_freezer_cg,
			   user_cgroup_path,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&slurm_freezer_cg);
		goto bail;
	}

	/* create job cgroup in the freezer ns (it could already exist) */
	if (xcgroup_create(&freezer_ns, &job_freezer_cg,
			   job_cgroup_path,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&slurm_freezer_cg);
		xcgroup_destroy(&user_freezer_cg);
		goto bail;
	}

	/* create step cgroup in the freezer ns (it should not exists) */
	if (xcgroup_create(&freezer_ns, &step_freezer_cg,
			   jobstep_cgroup_path,
			   getuid(), getgid()) != XCGROUP_SUCCESS) {
		xcgroup_destroy(&slurm_freezer_cg);
		xcgroup_destroy(&user_freezer_cg);
		xcgroup_destroy(&job_freezer_cg);
		goto bail;
	}

	if ((xcgroup_instantiate(&user_freezer_cg) != XCGROUP_SUCCESS) ||
	    (xcgroup_instantiate(&job_freezer_cg)  != XCGROUP_SUCCESS) ||
	    (xcgroup_instantiate(&step_freezer_cg) != XCGROUP_SUCCESS)) {
		xcgroup_destroy(&user_freezer_cg);
		xcgroup_destroy(&job_freezer_cg);
		xcgroup_destroy(&step_freezer_cg);
		goto bail;
	}

	/* inhibit release agent for the step cgroup thus letting
	 * slurmstepd being able to add new pids to the container
	 * when the job ends (TaskEpilog,...) */
	xcgroup_set_param(&step_freezer_cg, "notify_on_release", "0");
	slurm_freezer_init = true;

	xcgroup_unlock(&freezer_cg);
	return SLURM_SUCCESS;

bail:
	xfree(pre);
	xcgroup_destroy(&slurm_freezer_cg);
	xcgroup_unlock(&freezer_cg);
	xcgroup_destroy(&freezer_cg);
	return SLURM_ERROR;
}

static int _move_current_to_root_cgroup(xcgroup_ns_t *ns)
{
	xcgroup_t cg;
	int rc;

	if (xcgroup_create(ns, &cg, "", 0, 0) != XCGROUP_SUCCESS)
		return SLURM_ERROR;

	rc = xcgroup_move_process(&cg, getpid());
	xcgroup_destroy(&cg);

	return rc;
}

int _slurm_cgroup_destroy(void)
{
	if (xcgroup_lock(&freezer_cg) != XCGROUP_SUCCESS) {
		error("%s: xcgroup_lock error", __func__);
		return SLURM_ERROR;
	}

	/*
	 *  First move slurmstepd process to the root cgroup, otherwise
	 *   the rmdir(2) triggered by the calls below will always fail,
	 *   because slurmstepd is still in the cgroup!
	 */
	if (_move_current_to_root_cgroup(&freezer_ns) != SLURM_SUCCESS) {
		error("%s: Unable to move pid %d to root cgroup",
		      __func__, getpid());
		xcgroup_unlock(&freezer_cg);
		return SLURM_ERROR;
	}

	xcgroup_wait_pid_moved(&job_freezer_cg, "freezer job");

	if (jobstep_cgroup_path[0] != '\0') {
		if (xcgroup_delete(&step_freezer_cg) != XCGROUP_SUCCESS) {
			debug("_slurm_cgroup_destroy: problem deleting step cgroup path %s: %m",
			      step_freezer_cg.path);
			xcgroup_unlock(&freezer_cg);
			return SLURM_ERROR;
		}
		xcgroup_destroy(&step_freezer_cg);
	}

	if (job_cgroup_path[0] != '\0') {
		(void)xcgroup_delete(&job_freezer_cg);
		xcgroup_destroy(&job_freezer_cg);
	}

	if (user_cgroup_path[0] != '\0') {
		(void)xcgroup_delete(&user_freezer_cg);
		xcgroup_destroy(&user_freezer_cg);
	}

	if (slurm_freezer_init) {
		xcgroup_destroy(&slurm_freezer_cg);
	}

	xcgroup_unlock(&freezer_cg);
	xcgroup_destroy(&freezer_cg);
	xcgroup_ns_destroy(&freezer_ns);

	return SLURM_SUCCESS;
}

int _slurm_cgroup_add_pids(uint64_t id, pid_t* pids, int npids)
{
	if (*jobstep_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_add_pids(&step_freezer_cg, pids, npids);
}

int _slurm_cgroup_stick_stepd(uint64_t id, pid_t pid)
{
	if (*job_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_add_pids(&job_freezer_cg, &pid, 1);
}

int
_slurm_cgroup_get_pids(uint64_t id, pid_t **pids, int *npids)
{
	if (*jobstep_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_get_pids(&step_freezer_cg, pids, npids);
}

int _slurm_cgroup_suspend(uint64_t id)
{
	if (*jobstep_cgroup_path == '\0')
		return SLURM_ERROR;

	return xcgroup_set_param(&step_freezer_cg,
				 "freezer.state", "FROZEN");
}

int _slurm_cgroup_resume(uint64_t id)
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

	if (xstrcmp(cg.path, step_freezer_cg.path)) {
		fstatus = false;
	}
	else {
		fstatus = true;
	}

	xcgroup_destroy(&cg);
	return fstatus;
}

int
_slurm_cgroup_is_pid_a_slurm_task(uint64_t id, pid_t pid)
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
	if (ppid == (pid_t) id)
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
extern int proctrack_p_create (stepd_step_rec_t *job)
{
	int fstatus;

	/* create a new cgroup for that container */
	fstatus = _slurm_cgroup_create(job, (uint64_t)job->jmgr_pid,
				       job->uid, job->gid);
	if (fstatus)
		return SLURM_ERROR;

	/* stick slurmstepd pid to the newly created job container
	 * (Note: we do not put it in the step container because this
	 * container could be used to suspend/resume tasks using freezer
	 * properties so we need to let the slurmstepd outside of
	 * this one)
	 */
	fstatus = _slurm_cgroup_stick_stepd((uint64_t)job->jmgr_pid,
					    job->jmgr_pid);
	if (fstatus) {
		_slurm_cgroup_destroy();
		return SLURM_ERROR;
	}

	/* we use slurmstepd pid as the identifier of the container
	 * the corresponding cgroup could be found using
	 * _slurm_cgroup_find_by_pid */
	job->cont_id = (uint64_t)job->jmgr_pid;

	return SLURM_SUCCESS;
}

extern int proctrack_p_add (stepd_step_rec_t *job, pid_t pid)
{
	return _slurm_cgroup_add_pids(job->cont_id, &pid, 1);
}

extern int proctrack_p_signal (uint64_t id, int signal)
{
	pid_t* pids = NULL;
	int npids;
	int i;
	int slurm_task;

	/* get all the pids associated with the step */
	if (_slurm_cgroup_get_pids(id, &pids, &npids) !=
	    SLURM_SUCCESS) {
		debug3("unable to get pids list for cont_id=%"PRIu64"", id);
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
		if (pids[i] == (pid_t)id)
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

extern int proctrack_p_destroy (uint64_t id)
{
	return _slurm_cgroup_destroy();
}

extern uint64_t proctrack_p_find(pid_t pid)
{
	/* not provided for now */
	return 0;
}

extern bool proctrack_p_has_pid(uint64_t cont_id, pid_t pid)
{
	return _slurm_cgroup_has_pid(pid);
}

extern int proctrack_p_wait(uint64_t cont_id)
{
	int delay = 1;

	if (cont_id == 0 || cont_id == 1) {
		errno = EINVAL;
		return SLURM_ERROR;
	}

	/* Spin until the container is successfully destroyed */
	/* This indicates that all tasks have exited the container */
	while (proctrack_p_destroy(cont_id) != SLURM_SUCCESS) {
		proctrack_p_signal(cont_id, SIGKILL);
		sleep(delay);
		if (delay < 120) {
			delay *= 2;
		} else {
			error("%s: Unable to destroy container %"PRIu64" in cgroup plugin, giving up after %d sec",
			      __func__, cont_id, delay);
			break;
		}
	}

	return SLURM_SUCCESS;
}

extern int proctrack_p_get_pids(uint64_t cont_id,
				       pid_t **pids, int *npids)
{
	return _slurm_cgroup_get_pids(cont_id, pids, npids);
}
