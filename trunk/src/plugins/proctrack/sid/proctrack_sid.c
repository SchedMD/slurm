/*****************************************************************************\
 *  proctrack_sid.c - process tracking via session ID plugin.
 *****************************************************************************
 *  Copyright (C) 2005 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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
#include "src/common/log.h"

#ifndef __USE_XOPEN_EXTENDED
extern pid_t setsid(void);	/* missing from <unistd.h> */
#endif

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
const char plugin_name[]      = "Process tracking via process group ID plugin";
const char plugin_type[]      = "proctrack/sid";
const uint32_t plugin_version = 90;

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
 * For this plugin, we ignore the job_id.
 * To generate a unique container ID, we use setsid.
 */
extern uint32_t slurm_create_container ( uint32_t job_id )
{
	pid_t pid = setsid();
	(void) setpgrp();

	if (pid < 0) {
		error("slurm_create_container: setpsid: %m");
		return (uint32_t) 0;
	}
	return (uint32_t) pid;
}

extern int slurm_add_container ( uint32_t id )
{
	return SLURM_SUCCESS;
}

extern int slurm_signal_container  ( uint32_t id, int signal )
{
	pid_t pid = (pid_t) id;

	if (!id)	/* no container ID */
		return ESRCH;

	return killpg(pid, signal);
}

extern int slurm_destroy_container ( uint32_t id )
{
	return SLURM_SUCCESS;
}

extern uint32_t
slurm_find_container(pid_t pid)
{
	return (uint32_t) getsid(pid);
}

