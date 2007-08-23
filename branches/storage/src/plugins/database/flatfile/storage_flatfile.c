/*****************************************************************************\
 *  storage_flatfile.c - Store/Get all information in a flatfile.
 *
 *  $Id: storage_flatfile.c 10893 2007-01-29 21:53:48Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
#include "src/plugins/jobacct/common/jobacct_common.h"

#include "src/slurmctld/slurmctld.h"
#include "flatfile_jobacct.h"
#include "flatfile_jobcomp.h"

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
const char plugin_name[] = "Storage FLATFILE plugin";
const char plugin_type[] = "storage/flatfile";
const uint32_t plugin_version = 100;

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
	if(first) {
		/* since this can be loaded from many different places
		   only tell us once. */
		verbose("%s loaded", plugin_name);
		first = 0;
	} else {
		debug4("%s loaded", plugin_name);
	}
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}
/* 
 * Initialize the storage make sure tables are created and in working
 * order
 */
extern int storage_p_jobacct_init(char *job_acct_log)
{
	return flatfile_jobacct_init(job_acct_log);	
}

/*
 * finish up storage connection
 */
extern int storage_p_jobacct_fini()
{
	return flatfile_jobacct_fini();
}

/* 
 * load into the storage the start of a job
 */
extern int storage_p_jobacct_job_start(struct job_record *job_ptr)
{
	return flatfile_jobacct_job_start(job_ptr);
}

/* 
 * load into the storage the end of a job
 */
extern int storage_p_jobacct_job_complete(struct job_record *job_ptr)
{
	return flatfile_jobacct_job_complete(job_ptr);
}

/* 
 * load into the storage the start of a job step
 */
extern int storage_p_jobacct_step_start(struct step_record *step_ptr)
{
	return flatfile_jobacct_step_start(step_ptr);
}

/* 
 * load into the storage the end of a job step
 */
extern int storage_p_jobacct_step_complete(struct step_record *step_ptr)
{
	return flatfile_jobacct_step_complete(step_ptr);
}

/* 
 * load into the storage a suspention of a job
 */
extern int storage_p_jobacct_suspend(struct job_record *job_ptr)
{
	return flatfile_jobacct_suspend(job_ptr);
}

/* 
 * get info from the storage 
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern void storage_p_jobacct_get_jobs(List job_list, 
					List selected_steps,
					List selected_parts,
					void *params)
{
	flatfile_jobacct_get_jobs(job_list,
				  selected_steps, selected_parts,
				  params);
	return;
}

/* 
 * expire old info from the storage 
 */
extern void storage_p_jobacct_archive(List selected_parts,
				       void *params)
{
	flatfile_jobacct_archive(selected_parts, params);
	return;
}

extern int storage_p_jobcomp_init(char * location)
{
	return flatfile_jobcomp_init(location);
}

extern int storage_p_jobcomp_fini()
{
	return flatfile_jobcomp_fini();
}

extern int storage_p_jobcomp_log_record(struct job_record *job_ptr)
{
	return flatfile_jobcomp_log_record(job_ptr);
}

extern int storage_p_jobcomp_get_errno(void)
{
	return flatfile_jobcomp_get_errno();
}

extern char *storage_p_jobcomp_strerror(int errnum)
{
	return flatfile_jobcomp_strerror(errnum);
}

/* 
 * get info from the storage 
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern void storage_p_jobcomp_get_jobs(List job_list, 
					List selected_steps,
					List selected_parts,
					void *params)
{
	flatfile_jobcomp_get_jobs(job_list, 
				  selected_steps, selected_parts,
				  params);	
	return;
}

/* 
 * expire old info from the storage 
 */
extern void storage_p_jobcomp_archive(List selected_parts, void *params)
{
	flatfile_jobcomp_archive(selected_parts, params);
	return;
}
