/*****************************************************************************\
 *  switch_none.c - Library for managing a switch with no
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
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

#include <sys/types.h>

#include <slurm/slurm_errno.h>
#include "src/common/switch.h"

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
const char plugin_name[]        = "switch NONE plugin";
const char plugin_type[]        = "switch/none";
const uint32_t plugin_version   = 90;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	return SLURM_SUCCESS;
}

int fini ( void )
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
int slurm_libstate_save ( char * filename )
{
	return SLURM_SUCCESS;
}

int slurm_libstate_restore ( char * filename )
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
int slurm_alloc_jobinfo ( switch_jobinfo_t *switch_job )
{
	return SLURM_SUCCESS;
}

int slurm_build_jobinfo ( switch_jobinfo_t *switch_job, char *nodelist, 
		int nprocs, int cyclic_alloc)
{
	return SLURM_SUCCESS;
}

switch_jobinfo_t slurm_copy_jobinfo  ( switch_jobinfo_t switch_job )
{
	return NULL;
}

int slurm_free_jobinfo ( switch_jobinfo_t switch_job )
{
	return SLURM_SUCCESS;
}

int slurm_pack_jobinfo ( switch_jobinfo_t switch_job, Buf buffer )
{
	return SLURM_SUCCESS;
}

int slurm_unpack_jobinfo ( switch_jobinfo_t *switch_job, Buf buffer )
{
	return SLURM_SUCCESS;
}

void slurm_print_jobinfo(FILE *fp, struct switch_jobinfo *jobinfo)
{
	return;
}

char *slurm_sprint_jobinfo(switch_jobinfo_t switch_jobinfo, char *buf,
		size_t size)
{
	return NULL;
}

int slurm_prog_init(switch_jobinfo_t switch_jobinfo, uid_t uid)
{
	return SLURM_SUCCESS;
}

int slurm_setcap ( switch_jobinfo_t jobinfo, int procnum )
{
	return SLURM_SUCCESS;
}

void slurm_prog_fini ( switch_jobinfo_t jobinfo )
{
	return;
}

int slurm_prog_signal ( switch_jobinfo_t jobinfo, int signal)
{
	return SLURM_ERROR;	/* not supported */
}

int slurm_prog_destroy ( switch_jobinfo_t jobinfo )
{
	return SLURM_SUCCESS;
}

