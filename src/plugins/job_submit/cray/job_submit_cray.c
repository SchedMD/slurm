/*****************************************************************************\
 *  job_submit_cray.c - Infrastructure for native Slurm operation on Cray
 *                      computers
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC.
 *  Copyright (C) 2014 Cray Inc. All Rights Reserved.
 *  Written by Morris Jette <jette@schedmd.com>
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

#define _DEBUG 0

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
const char plugin_name[]       	= "Job submit Cray plugin";
const char plugin_type[]       	= "job_submit/cray";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

#define CRAY_GRES "craynetwork"
#define CRAY_GRES_POSTFIX CRAY_GRES":1"

/*
 * Append CRAY_GRES_POSTFIX to the gres provided by the user
 */
static void _append_gres(struct job_descriptor *job_desc)
{
	if (job_desc->tres_per_node == NULL) {
		job_desc->tres_per_node = xstrdup(CRAY_GRES_POSTFIX);
	} else if (strlen(job_desc->tres_per_node) == 0) {
		xstrcat(job_desc->tres_per_node, CRAY_GRES_POSTFIX);
	} else if (strstr(job_desc->tres_per_node, CRAY_GRES) == NULL) {
		// Don't append if they already specified craynetwork
		// Allows the user to ask for more or less than the default
		xstrcat(job_desc->tres_per_node, "," CRAY_GRES_POSTFIX);
	}
}

int init (void)
{
	return SLURM_SUCCESS;
}

int fini (void)
{
	return SLURM_SUCCESS;
}

extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid)
{
	_append_gres(job_desc);
	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
{
	/* Don't call this on modify it shouldn't be needed and will
	 * mess things up if modifying a running job
	 */
	//_append_gres(job_desc);
	return SLURM_SUCCESS;
}
