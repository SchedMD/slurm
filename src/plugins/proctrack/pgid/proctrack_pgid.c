/*****************************************************************************\
 *  proctrack_pgid.c - process tracking via process group ID plugin.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#ifndef   __USE_XOPEN_EXTENDED
#  define __USE_XOPEN_EXTENDED /* getpgid */
#endif
#include <unistd.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
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
const char plugin_name[]      = "Process tracking via process group ID plugin";
const char plugin_type[]      = "proctrack/pgid";
const uint32_t plugin_version = 91;

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

extern int proctrack_p_create ( stepd_step_rec_t *job )
{
	return SLURM_SUCCESS;
}

/*
 * Uses job step process group id.
 */
extern int proctrack_p_add ( stepd_step_rec_t *job, pid_t pid )
{
	job->cont_id = (uint64_t)job->pgid;
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

	if (cont_id == 0 || cont_id == 1) {
		slurm_seterrno(EINVAL);
		return SLURM_ERROR;
	}

	/* Spin until the process group is gone. */
	while (killpg(pgid, 0) == 0) {
		proctrack_p_signal(cont_id, SIGKILL);
		sleep(delay);
		if (delay < 120) {
			delay *= 2;
		} else {
			error("%s: Unable to destroy container %"PRIu64" "
			      "in pgid plugin, giving up after %d sec",
			      __func__, cont_id, delay);
			break;
		}
	}

	return SLURM_SUCCESS;
}

extern int
proctrack_p_get_pids(uint64_t cont_id, pid_t **pids, int *npids)
{
	DIR *dir;
	struct dirent *de;
	char path[PATH_MAX], *endptr, *num, rbuf[1024];
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
		if (read(fd, rbuf, 1024) <= 0) {
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
	closedir(dir);

fini:	*pids  = pid_array;
	*npids = pid_count;
	return rc;
}
