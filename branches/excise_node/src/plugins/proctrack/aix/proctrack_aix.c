/*****************************************************************************\
 *  proctrack_aix.c - process tracking via AIX kernel extension.
 *****************************************************************************
 *  Copyright (C) 2005-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include <proctrack.h>
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
 * minimum versions for their plugins as the job completion logging API 
 * matures.
 */
const char plugin_name[]      = "Process tracking via AIX kernel extension plugin";
const char plugin_type[]      = "proctrack/aix";
const uint32_t plugin_version = 90;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	uint32_t required_version = 3;

	if (proctrack_version() < required_version) {
		error("proctrack AIX kernel extension must be >= %u",
		      required_version);
		return SLURM_ERROR;
	}

	if ((pid_t)0 != getuid()) {
		error("proctrack/aix requires the slurmd to run as root.");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

extern int slurm_container_create ( slurmd_job_t *job )
{
	return SLURM_SUCCESS;
}

/*
 * Uses job step process group id as a unique identifier.  Job id
 * and step id are not unique by themselves.
 */
extern int slurm_container_add ( slurmd_job_t *job, pid_t pid )
{
	int pgid = (int) job->pgid;

	xassert(job);
	xassert(pgid > 1);

	if (proctrack_job_reg_pid(&pgid, &pid) != 0) {
		error("proctrack_job_reg_pid(%d, %d): %m", pgid, (int)pid);
		return SLURM_ERROR;
	}

	job->cont_id = (uint32_t)pgid;
	return SLURM_SUCCESS;
}

extern int slurm_container_signal  ( uint32_t id, int signal )
{
	int jobid = (int) id;
	if (!id)	/* no container ID */
		return ESRCH;

	return proctrack_job_kill(&jobid, &signal);
}

extern int slurm_container_destroy ( uint32_t id )
{
	int jobid = (int) id;

	if (!id)	/* no container ID */
		return ESRCH;

	if (proctrack_job_unreg(&jobid) == 0)
		return SLURM_SUCCESS;

	return SLURM_ERROR;
}

extern uint32_t
slurm_container_find(pid_t pid)
{
	int local_pid = (int) pid;
	int cont_id = proctrack_get_job_id(&local_pid);
	if (cont_id == -1)
		return (uint32_t) 0;
	return (uint32_t) cont_id;
}

extern bool
slurm_container_has_pid(uint32_t cont_id, pid_t pid)
{
	int local_pid = (int) pid;
	int found_cont_id = proctrack_get_job_id(&local_pid);

	if (found_cont_id == -1 || (uint32_t)found_cont_id != cont_id)
		return false;

	return true;
}

extern int
slurm_container_get_pids(uint32_t cont_id, pid_t **pids, int *npids)
{
	int32_t *p;
	int np;
	int len = 64;

	p = (int32_t *)xmalloc(len * sizeof(int32_t));
	while((np = proctrack_get_pids(cont_id, len, p)) > len) {
		/* array is too short, double its length */
		len *= 2;
		xrealloc(p, len);
	}

	if (np == -1) {
		error("proctrack_get_pids(AIX) for container %u failed: %m",
		      cont_id);
		xfree(p);
		*pids = NULL;
		*npids = 0;
		return SLURM_ERROR;
	}

	if (sizeof(uint32_t) == sizeof(pid_t)) {
		debug3("slurm_container_get_pids: No need to copy pids array");
		*npids = np;
		*pids = (pid_t *)p;
	} else {
		/* need to cast every individual pid in the array */
		pid_t *p_copy;
		int i;

		debug3("slurm_container_get_pids: Must copy pids array");
		p_copy = (pid_t *)xmalloc(np * sizeof(pid_t));
		for (i = 0; i < np; i++) {
			p_copy[i] = (pid_t)p[i];
		}
		xfree(p);

		*npids = np;
		*pids = p_copy;
	}
	return SLURM_SUCCESS;
}

extern int
slurm_container_wait(uint32_t cont_id)
{
	int jobid = (int) cont_id;
	int delay = 1;

	if (cont_id == 0 || cont_id == 1) {
		errno = EINVAL;
		return SLURM_ERROR;
	}

	/* Spin until the container is successfully destroyed */
	while (proctrack_job_unreg(&jobid) != 0) {
		sleep(delay);
		if (delay < 120) {
			delay *= 2;
		} else {
			int i;
			pid_t *pids = NULL;
			int npids = 0;
			error("Container %u is still not empty", cont_id);

			slurm_container_get_pids(cont_id, &pids, &npids);
			if (npids > 0) {
				for (i = 0; i < npids; i++) {
					verbose("  Container %u has pid %d",
						pids[i]);
				}
				xfree(pids);
			}
		}
	}

	return SLURM_SUCCESS;
}

