/*****************************************************************************\
 *  switch_none.c - Library for managing a switch with no special handling.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include <signal.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/slurmctld/slurmctld.h"
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
 *      <application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "switch" for Slurm switch) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a switch plugin if the plugin_type string has a prefix of "switch/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "switch NONE plugin";
const char plugin_type[]        = "switch/none";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;
const uint32_t plugin_id	= SWITCH_PLUGIN_NONE;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

int fini ( void )
{
	return SLURM_SUCCESS;
}

extern int switch_p_reconfig ( void )
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for global state save/restore
 */
int switch_p_libstate_save ( char * dir_name )
{
	return SLURM_SUCCESS;
}

int switch_p_libstate_restore ( char * dir_name, bool recover )
{
	return SLURM_SUCCESS;
}

int switch_p_libstate_clear ( void )
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for job step specific credential
 */
int switch_p_alloc_jobinfo ( switch_jobinfo_t **switch_job,
			     uint32_t job_id, uint32_t step_id )
{
	return SLURM_SUCCESS;
}

extern int switch_p_build_jobinfo(switch_jobinfo_t *switch_job,
				  slurm_step_layout_t *step_layout,
				  step_record_t *step_ptr)
{
	return SLURM_SUCCESS;
}

int switch_p_duplicate_jobinfo (switch_jobinfo_t *tmp,
			     switch_jobinfo_t **dest)
{
	return SLURM_SUCCESS;
}
void switch_p_free_jobinfo ( switch_jobinfo_t *switch_job )
{
	return;
}

int switch_p_pack_jobinfo(switch_jobinfo_t *switch_job, buf_t *buffer,
			  uint16_t protocol_version)
{
	return 0;
}

int switch_p_unpack_jobinfo(switch_jobinfo_t **switch_job, buf_t *buffer,
			    uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

/*
 * switch functions for job initiation
 */

int switch_p_job_preinit(stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_init (stepd_step_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_suspend_test(switch_jobinfo_t *jobinfo)
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_get(switch_jobinfo_t *jobinfo,
					  void **suspend_info)
{
	return;
}

extern void switch_p_job_suspend_info_pack(void *suspend_info, buf_t *buffer,
					   uint16_t protocol_version)
{
	return;
}

extern int switch_p_job_suspend_info_unpack(void **suspend_info, buf_t *buffer,
					    uint16_t protocol_version)
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_suspend_info_free(void *suspend_info)
{
	return;
}

extern int switch_p_job_suspend(void *suspend_info, int max_wait)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_resume(void *suspend_info, int max_wait)
{
	return SLURM_SUCCESS;
}

int switch_p_job_fini ( switch_jobinfo_t *jobinfo )
{
	return SLURM_SUCCESS;
}

int switch_p_job_postfini (stepd_step_rec_t *job)
{
	uid_t pgid = job->jmgr_pid;
	/*
	 *  Kill all processes in the job's session
	 */
	if (pgid) {
		debug2("Sending SIGKILL to pgid %lu",
			(unsigned long) pgid);
		kill(-pgid, SIGKILL);
	} else
		debug("%ps: Bad pid valud %lu", &job->step_id,
		      (unsigned long) pgid);

	return SLURM_SUCCESS;
}

int switch_p_job_attach ( switch_jobinfo_t *jobinfo, char ***env,
			uint32_t nodeid, uint32_t procid, uint32_t nnodes,
			uint32_t nprocs, uint32_t rank )
{
	return SLURM_SUCCESS;
}

extern int switch_p_get_jobinfo(switch_jobinfo_t *switch_job,
	int key, void *resulting_data)
{
	slurm_seterrno(EINVAL);
	return SLURM_ERROR;
}

extern int switch_p_job_step_complete(switch_jobinfo_t *jobinfo,
	char *nodelist)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_allocated(switch_jobinfo_t *jobinfo,
	char *nodelist)
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_suspend( stepd_step_rec_t *job )
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_suspend( stepd_step_rec_t *job )
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_pre_resume( stepd_step_rec_t *job )
{
	return SLURM_SUCCESS;
}

extern int switch_p_job_step_post_resume( stepd_step_rec_t *job )
{
	return SLURM_SUCCESS;
}

extern void switch_p_job_complete(uint32_t job_id)
{
	return;
}
