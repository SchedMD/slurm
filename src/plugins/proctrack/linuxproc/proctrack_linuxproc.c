/*****************************************************************************\
 *  proctrack_linuxproc.c - process tracking via linux /proc process tree.
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

#include <inttypes.h>
#include <signal.h>	/* SIGKILL */
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "kill_tree.h"

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
const char plugin_name[]      = "Process tracking via linux /proc";
const char plugin_type[]      = "proctrack/linuxproc";
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

/*
 * Uses slurmd job-step manager's pid as the unique container id.
 */
extern int proctrack_p_create(stepd_step_rec_t *step)
{
	step->cont_id = (uint64_t)step->jmgr_pid;
	return SLURM_SUCCESS;
}

extern int proctrack_p_add(stepd_step_rec_t *step, pid_t pid)
{
	return SLURM_SUCCESS;
}

extern int proctrack_p_signal ( uint64_t id, int signal )
{
	return kill_proc_tree((pid_t)id, signal);
}

extern int proctrack_p_destroy ( uint64_t id )
{
	return SLURM_SUCCESS;
}

extern uint64_t proctrack_p_find(pid_t pid)
{
	return (uint64_t) find_ancestor(pid, "slurmstepd");
}

extern bool proctrack_p_has_pid(uint64_t cont_id, pid_t pid)
{
	uint64_t cont;

	cont = (uint64_t) find_ancestor(pid, "slurmstepd");
	if (cont == cont_id)
		return true;

	return false;
}

extern int
proctrack_p_wait(uint64_t cont_id)
{
	if (cont_id == 0 || cont_id == 1) {
		errno = EINVAL;
		return SLURM_ERROR;
	}

	return proctrack_p_destroy(cont_id);
}

extern int
proctrack_p_get_pids(uint64_t cont_id, pid_t **pids, int *npids)
{
	return proctrack_linuxproc_get_pids((pid_t)cont_id, pids, npids);
}
