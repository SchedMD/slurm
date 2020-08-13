/*****************************************************************************\
 *  site_factor_none.c - NULL site_factor plugin
*****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#define _GNU_SOURCE

#include "src/common/slurm_xlator.h"
#include "src/common/log.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
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
const char	*plugin_name		= "NULL site_factor plugin";
const char	*plugin_type		= "site_factor/none";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern void site_factor_p_reconfig(void)
{
	/*
	 * Handle any reconfiguration, such as potential changes to
	 * PrioritySiteFactorParameters, here.
	 */

	return;
}

extern void site_factor_p_set(job_record_t *job_ptr)
{
	/*
	 * Set initial value for the admin_factor here.
	 *
	 * E.g.:
	 */

	/* job_ptr->site_factor = (lrand48() % range) + NICE_OFFSET; */

	return;
}

#if 0
/*
 * Example function for use with list_for_each()
 */
static int _update(void *x, void *ignored)
{
	job_record_t *job_ptr = (job_record_t *) x;

	/*
	 * You will usually only want to change the priority for
	 * pending jobs, and ignore all other states.
	 */
	if (IS_JOB_PENDING(job_ptr))
		job_ptr->site_factor = (lrand48() % range) + NICE_OFFSET;

	return SLURM_SUCCESS;
}
#endif

extern void site_factor_p_update(void)
{
	/*
	 * For a real plugin, it is expected that you'll run a list_for_each()
	 * against the job_list here, and update the admin_factor values as
	 * desired.
	 *
	 * E.g.:
	 */

	/* list_for_each(job_list, _update, NULL); */

	return;
}
