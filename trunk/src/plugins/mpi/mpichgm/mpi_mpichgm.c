/*****************************************************************************\
 **  mpi_gmpi.c - Library routines for initiating jobs on with gmpi type mpi 
 **  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if     HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#include <slurm/slurm_errno.h>
#include "src/common/slurm_xlator.h"
#include "src/plugins/mpi/mpichgm/mpichgm.h"

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
const char plugin_name[]        = "mpi MPICH-GM plugin";
const char plugin_type[]        = "mpi/mpich-gm";
const uint32_t plugin_version   = 100;

int mpi_p_init(slurmd_job_t *job, int rank)
{
	char addrbuf[1024];
	char *p;
	char *addr = getenvp (job->env, "SLURM_LAUNCH_NODE_IPADDR");
	
	debug("Using mpi/mpich-gm");
	slurm_print_slurm_addr (job->envtp->self, addrbuf, sizeof(addrbuf));
	
	if ((p = strchr (addrbuf, ':')) != NULL)
		*p = '\0';
		
	setenvf (&job->env, "GMPI_MASTER", "%s", addr);
	setenvf (&job->env, "GMPI_SLAVE",  "%s", addrbuf);
	setenvf (&job->env, "GMPI_ID",  "%d", rank);
	debug2("init for mpi rank %d\n", rank);
	
	return SLURM_SUCCESS;
}

int mpi_p_thr_create(srun_job_t *job)
{
	debug("Using mpi/mpich-gm");
	return gmpi_thr_create(job);
}

int mpi_p_single_task()
{
	return false;
}

int mpi_p_exit()
{
	return SLURM_SUCCESS;
}
