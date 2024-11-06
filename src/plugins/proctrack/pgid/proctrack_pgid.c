/*****************************************************************************\
 *  proctrack_pgid.c - process tracking via process group ID plugin.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#define __USE_XOPEN_EXTENDED	/* getpgid */

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __FreeBSD__
#include <err.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <libprocstat.h> /* must be last */
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
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
const char plugin_name[]      = "Process tracking via process group ID plugin";
const char plugin_type[]      = "proctrack/pgid";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

extern int proctrack_p_create(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

/*
 * Uses job step process group id.
 */
extern int proctrack_p_add(stepd_step_rec_t *step, pid_t pid)
{
	step->cont_id = (uint64_t)step->pgid;
	return SLURM_SUCCESS;
}

extern int proctrack_p_signal  ( uint64_t id, int signal )
{
	pid_t pid = (pid_t) id;

	if (!id) {
		/* no container ID */
	} else if (pid == getpid() || pid == getpgid(0)) {
		error("slurm_signal_container would kill caller!");
	} else {
		return killpg(pid, signal);
	}
	slurm_seterrno(ESRCH);
	return SLURM_ERROR;
}

extern int proctrack_p_destroy ( uint64_t id )
{
	return SLURM_SUCCESS;
}

extern uint64_t proctrack_p_find(pid_t pid)
{
	pid_t rc = getpgid(pid);

	if (rc == -1)
		return (uint64_t) 0;
	else
		return (uint64_t) rc;
}

extern bool proctrack_p_has_pid(uint64_t cont_id, pid_t pid)
{
	pid_t pgid = getpgid(pid);

	if ((pgid == -1) || ((uint64_t)pgid != cont_id))
		return false;

	return true;
}

extern int
proctrack_p_wait(uint64_t cont_id)
{
	pid_t pgid = (pid_t)cont_id;
	int delay = 1;
	time_t start = time(NULL);

	if (cont_id == 0 || cont_id == 1) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	/* Spin until the process group is gone. */
	while (killpg(pgid, 0) == 0) {
		time_t now = time(NULL);

		if (now > (start + slurm_conf.unkillable_timeout)) {
			error("Unable to destroy container %"PRIu64" in pgid plugin, giving up after %lu sec",
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


/*
 * Get list of all PIDs belonging to process group cont_id
 */
#ifdef __FreeBSD__
extern int proctrack_p_get_pids(uint64_t cont_id, pid_t **pids, int *npids)
{
	pid_t *pid_array = NULL;
	struct procstat *proc_info;
	struct kinfo_proc *proc_list;
	FILE *procstat_err;
	unsigned int pid_count = 0;

	/*
	 * procstat_getprocs() prints an innocuous but annoying warning
	 * to stderr by default when no matching processes are found:
	 *
	 * https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=245318
	 *
	 * Remove the redirect to /dev/null if this changes in the future.
	 */
	if ((procstat_err = fopen("/dev/null", "w+")))
		err_set_file(procstat_err);
	proc_info = procstat_open_sysctl();
	proc_list = procstat_getprocs(proc_info, KERN_PROC_PGRP, cont_id,
				      (unsigned int *) &pid_count);
	if (procstat_err)
		fclose(procstat_err);

	if (pid_count > 0) {
		xrecalloc(pid_array, sizeof(pid_t), pid_count);
		// FIXME: Do we need to filter zombies like the Linux code?
		// proc_list[c].ki_paddr->p_state == PRS_ZOMBIE
		for (size_t c = 0; c < pid_count; ++c)
			pid_array[c] = proc_list[c].ki_pid;
	}

	procstat_freeprocs(proc_info, proc_list);
	procstat_close(proc_info);

	*pids  = pid_array;
	*npids = pid_count;

	return SLURM_SUCCESS;
}
#else
extern int
proctrack_p_get_pids(uint64_t cont_id, pid_t **pids, int *npids)
{
	DIR *dir;
	struct dirent *de;
	char path[PATH_MAX], *endptr, *num, *rbuf;
	ssize_t buf_used;
	char cmd[1024];
	char state;
	int fd, rc = SLURM_SUCCESS;
	long pid, ppid, pgid, ret_l;
	pid_t *pid_array = NULL;
	int pid_count = 0;

	if ((dir = opendir("/proc")) == NULL) {
		error("opendir(/proc): %m");
		rc = SLURM_ERROR;
		goto fini;
	}
	rbuf = xmalloc(4096);
	while ((de = readdir(dir)) != NULL) {
		num = de->d_name;
		if ((num[0] < '0') || (num[0] > '9'))
			continue;
		ret_l = strtol(num, &endptr, 10);
		if ((ret_l == LONG_MIN) || (ret_l == LONG_MAX)) {
			error("couldn't do a strtol on str %s(%ld): %m",
			      num, ret_l);
			continue;
		}
		sprintf(path, "/proc/%s/stat", num);
		if ((fd = open(path, O_RDONLY)) < 0) {
			continue;
		}
		buf_used = read(fd, rbuf, 4096);
		if ((buf_used <= 0) || (buf_used >= 4096)) {
			close(fd);
			continue;
		}
		close(fd);
		if (sscanf(rbuf, "%ld %s %c %ld %ld",
			   &pid, cmd, &state, &ppid, &pgid) != 5) {
			continue;
		}
		if (pgid != (long) cont_id)
			continue;
		if (state == 'Z') {
			debug3("Defunct process skipped: command=%s state=%c "
			       "pid=%ld ppid=%ld pgid=%ld",
			       cmd, state, pid, ppid, pgid);
			continue;	/* Defunct, don't try to kill */
		}
		xrealloc(pid_array, sizeof(pid_t) * (pid_count + 1));
		pid_array[pid_count++] = pid;
	}
	xfree(rbuf);
	closedir(dir);

fini:	*pids  = pid_array;
	*npids = pid_count;
	return rc;
}
#endif
