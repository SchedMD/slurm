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
#include "src/common/xstring.h"
#include "src/common/cgroup.h"
#include "src/common/read_config.h"
#include "src/slurmd/common/xcpuinfo.h"
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
	/* initialize cpuinfo internal data */
	if (xcpuinfo_init() != XCPUINFO_SUCCESS) {
		return SLURM_ERROR;
	}

	/* initialize cgroup internal data */
	if (cgroup_g_initialize(CG_TRACK) != SLURM_SUCCESS) {
		xcpuinfo_fini();
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini (void)
{
	xcpuinfo_fini();
	return SLURM_SUCCESS;
}

/*
 * Uses slurmd job-step manager's pid as the unique container id.
 */
extern int proctrack_p_create (stepd_step_rec_t *job)
{
	return cgroup_g_step_create(CG_TRACK, job);
}

extern int proctrack_p_add (stepd_step_rec_t *job, pid_t pid)
{
	return cgroup_g_step_addto(CG_TRACK, &pid, 1);
}

extern int proctrack_p_signal (uint64_t id, int signal)
{
	pid_t* pids = NULL;
	int npids = 0;
	int i;
	int slurm_task;

	/* get all the pids associated with the step */
	if (cgroup_g_step_get_pids(&pids, &npids) != SLURM_SUCCESS) {
		debug3("unable to get pids list for cont_id=%"PRIu64"", id);
		/* that could mean that all the processes already exit */
		/* the container so return success */
		return SLURM_SUCCESS;
	}

	/* directly manage SIGSTOP using cgroup freezer subsystem */
	if (signal == SIGSTOP) {
		xfree(pids);
		return cgroup_g_step_suspend();
	}

	/* start by resuming in case of SIGKILL */
	if (signal == SIGKILL) {
		cgroup_g_step_resume();
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
		return cgroup_g_step_resume();
	}

	return SLURM_SUCCESS;
}

extern int proctrack_p_destroy (uint64_t id)
{
	return cgroup_g_step_destroy(CG_TRACK);
}

extern uint64_t proctrack_p_find(pid_t pid)
{
	/* not provided for now */
	return 0;
}

extern bool proctrack_p_has_pid(uint64_t cont_id, pid_t pid)
{
	return cgroup_g_has_pid(pid);
}

extern int proctrack_p_wait(uint64_t cont_id)
{
	int delay = 1;
	time_t start = time(NULL);

	if (cont_id == 0 || cont_id == 1) {
		errno = EINVAL;
		return SLURM_ERROR;
	}

	/* Spin until the container is successfully destroyed */
	/* This indicates that all tasks have exited the container */
	while (proctrack_p_destroy(cont_id) != SLURM_SUCCESS) {
		time_t now = time(NULL);

		if (now > (start + slurm_conf.unkillable_timeout)) {
			error("Unable to destroy container %"PRIu64" in cgroup plugin, giving up after %lu sec",
			      cont_id, (now - start));
			break;
		}
		proctrack_p_signal(cont_id, SIGKILL);
		sleep(delay);
		if (delay < 32)
			delay *= 2;
	}

	return SLURM_SUCCESS;
}

extern int proctrack_p_get_pids(uint64_t cont_id, pid_t **pids, int *npids)
{
	return cgroup_g_step_get_pids(pids, npids);
}
