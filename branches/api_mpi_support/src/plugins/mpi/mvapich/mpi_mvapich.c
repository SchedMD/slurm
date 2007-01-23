/*****************************************************************************\
 **  mpi_mvapich.c - Library routines for initiating jobs on with mvapich
 **  type mpi. 
 **  $Id: mpi_gmpi.c,v 1.7 2005/06/07 18:25:32 morrone Exp $
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-226842.
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

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/plugins/mpi/mvapich/mvapich.h"

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
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for SLURM switch) and <method> is a description 
 * of how this plugin satisfies that application.  SLURM will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as this API matures.
 */
const char plugin_name[]        = "mpi MVAPICH plugin";
const char plugin_type[]        = "mpi/mvapich";
const uint32_t plugin_version   = 100;

int mpi_p_init (slurmd_job_t *job, int rank)
{
	int i;
	char *processes = NULL;
	char *addr = getenvp (job->env, "SLURM_LAUNCH_NODE_IPADDR");

	debug("Using mpi/mvapich");
	setenvf (&job->env, "MPIRUN_HOST", "%s", addr);
	setenvf (&job->env, "MPIRUN_RANK", "%d", rank);
	setenvf (&job->env, "MPIRUN_MPD", "0");

	debug2("init for mpi rank %d\n", rank);
	/*
	 * Fake MPIRUN_PROCESSES env var -- we don't need this for
	 *  SLURM at this time. (what a waste)
	 */
	for (i = 0; i < job->nprocs; i++)
		xstrcat (processes, "x:");
	
	setenvf (&job->env, "MPIRUN_PROCESSES", "%s", processes);

	/* 
	 * Some mvapich versions will ignore MPIRUN_PROCESSES If
	 *  the following env var is set.
	 */
	setenvf (&job->env, "NOT_USE_TOTALVIEW", "1");

	/*
	 * Set VIADEV_ENABLE_AFFINITY=0 so that mvapich doesn't 
	 *  override SLURM's CPU affinity. (Unless this var is
	 *  already set in user env)
	 */
	if (!getenvp (job->env, "VIADEV_ENABLE_AFFINITY"))
		setenvf (&job->env, "VIADEV_ENABLE_AFFINITY", "0");

	return SLURM_SUCCESS;
}

int mpi_p_thr_create(mpi_hook_client_info_t *job, char ***env)
{
	debug("Using mpi/mvapich");
	return mvapich_thr_create(job, env);
}

int mpi_p_single_task()
{
	return false;
}

int mpi_p_exit()
{
	return SLURM_SUCCESS;
}
