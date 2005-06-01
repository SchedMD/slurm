
/*****************************************************************************\
 *  jobacct_none.c - NO-OP slurm job completion logging plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>.
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
 *
 *  This file is patterned after jobcomp_none.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <slurm/slurm_errno.h>

#include "src/slurmctld/slurmctld.h"
#include "src/slurmd/slurmd.h"
#include "src/common/slurm_jobacct.h"

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
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] =
    "Job accounting NOT_INVOKED plugin for slurmctld and slurmd";
const char plugin_type[] = "jobacct/none";
const uint32_t plugin_version = 100;


/*
 * The following routines are called by slurmctld
 */

/*
 * slurmctld_jobacct_init() is called when the plugin is loaded by
 * slurmctld, before any other functions are called.  Put global
 * initialization here.
 */

int slurmctld_jobacct_init(char *job_acct_loc, char *job_acct_parameters)
{
	info("jobacct NONE plugin loaded");
	debug3("slurmctld_jobacct_init() called");
	return SLURM_SUCCESS;
}


int slurmctld_jobacct_job_complete(struct job_record *job_ptr)
{
	debug3("slurmctld_jobacct_job_complete() called");
	return SLURM_SUCCESS;
}

int slurmctld_jobacct_job_start(struct job_record *job_ptr)
{
	debug3("slurmctld_jobacct_job_start() called");
	return SLURM_SUCCESS;
}


int slurm_jobacct_process_message(struct slurm_msg *msg)
{
	debug3("slurm_jobacct_process_message() called");
	return SLURM_SUCCESS;
}

/*
 * The following routines are called by slurmd
 */

int slurmd_jobacct_init(char *job_acct_parameters)
{
	info("jobacct NONE plugin loaded");
	debug3("slurmd_jobacct_init() called");
	return SLURM_SUCCESS;
}

int slurmd_jobacct_jobstep_launched(slurmd_job_t *job)
{
	debug3("slurmd_jobacct_jobstep_launched() called");
	return SLURM_SUCCESS;
}

int slurmd_jobacct_jobstep_terminated(slurmd_job_t *job)
{
	debug3("slurmd_jobacct_jobstep_terminated() called");
	return SLURM_SUCCESS;
}

int slurmd_jobacct_smgr(void)
{
	debug3("slurmd_jobacct_smgr() called");
	return SLURM_SUCCESS;
}

int slurmd_jobacct_task_exit(slurmd_job_t *job, pid_t pid, int status, struct rusage *rusage)
{
	debug3("slurmd_jobacct_jobstep_complete() called");
	return SLURM_SUCCESS;
}
