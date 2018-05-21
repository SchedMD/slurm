/*****************************************************************************\
 *  job_submit_defaults.c - Set defaults in job submit request specifications.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"
#include "src/slurmctld/slurmctld.h"

#define MAX_ACCTG_FREQUENCY 30

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
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job submit defaults plugin";
const char plugin_type[]       	= "job_submit/defaults";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*****************************************************************************\
 * We've provided a simple example of the type of things you can do with this
 * plugin. If you develop another plugin that may be of interest to others
 * please post it to slurm-dev@schedmd.com  Thanks!
\*****************************************************************************/
extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
#if 0
	uint16_t acctg_freq = 0;
	if (job_desc->acctg_freq)
		acctg_freq = atoi(job_desc->acctg_freq);
	/* This example code will prevent users from setting an accounting
	 * frequency of less than 30 seconds in order to ensure more precise
	 *  accounting. Also remove any QOS value set by the user in order
	 * to use the default value from the database. */
	if (acctg_freq < MIN_ACCTG_FREQUENCY) {
		info("Changing accounting frequency of submitted job "
		     "from %u to %u",
		     acctg_freq, MIN_ACCTG_FREQUENCY);
		job_desc->acctg_freq = xstrdup_printf(
			"%d", MIN_ACCTG_FREQUENCY);
		if (err_msg)
			*err_msg = xstrdup("Changed job frequency");
	}

	if (job_desc->qos) {
		info("Clearing QOS (%s) from submitted job", job_desc->qos);
		xfree(job_desc->qos);
	}
#endif
	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
{
#if 0
	uint16_t acctg_freq = 0;
	if (job_desc->acctg_freq)
		acctg_freq = atoi(job_desc->acctg_freq);
	/* This example code will prevent users from setting an accounting
	 * frequency of less than 30 seconds in order to ensure more precise
	 *  accounting. Also remove any QOS value set by the user in order
	 * to use the default value from the database. */
	if (acctg_freq < MIN_ACCTG_FREQUENCY) {
		info("Changing accounting frequency of modify job %u "
		     "from %u to %u", job_ptr->job_id,
		     job_desc->acctg_freq, MIN_ACCTG_FREQUENCY);
		job_desc->acctg_freq = xstrdup_printf(
			"%d", MIN_ACCTG_FREQUENCY);
	}

	if (job_desc->qos) {
		info("Clearing QOS (%s) from modify of job %u",
		     job_desc->qos, job_ptr->job_id);
		xfree(job_desc->qos);
	}
#endif
	return SLURM_SUCCESS;
}
