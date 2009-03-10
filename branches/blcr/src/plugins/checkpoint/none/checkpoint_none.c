/*****************************************************************************\
 *  checkpoint_none.c - NO-OP slurm checkpoint plugin.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/slurmctld/slurmctld.h"

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
 * the plugin (e.g., "checkpoint" for SLURM checkpoint) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load checkpoint plugins if the plugin_type string has a 
 * prefix of "checkpoint/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the checkpoint API matures.
 */
const char plugin_name[]       	= "Checkpoint NONE plugin";
const char plugin_type[]       	= "checkpoint/none";
const uint32_t plugin_version	= 100;

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
 * The remainder of this file implements the standard SLURM checkpoint API.
 */

extern int slurm_ckpt_op (uint32_t job_id, uint32_t step_id, uint16_t op, 
		uint16_t data, char *image_dir, time_t * event_time,
		uint32_t *error_code, char **error_msg )
{
	return ESLURM_NOT_SUPPORTED;
}

extern int slurm_ckpt_comp (struct step_record * step_ptr, time_t event_time,
		uint32_t error_code, char *error_msg)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int slurm_ckpt_alloc_job(check_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_free_job(check_jobinfo_t jobinfo)
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_pack_job(check_jobinfo_t jobinfo, Buf buffer)
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_unpack_job(check_jobinfo_t jobinfo, Buf buffer)
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_task_comp (struct step_record * step_ptr, 
				 uint32_t task_id, time_t event_time, 
				 uint32_t error_code, char *error_msg )
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_stepd_prefork(void *slurmd_job)
{
	return SLURM_SUCCESS;
}

extern int slurm_ckpt_signal_tasks(void *slurmd_job)
{
	return ESLURM_NOT_SUPPORTED;
}

extern int slurm_ckpt_restart_task(void *slurmd_job, char *image_dir, int gtid)
{
	return ESLURM_NOT_SUPPORTED;
}
