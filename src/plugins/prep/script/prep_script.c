/*****************************************************************************\
 *  prep_script.c - PrEp script plugin, handles Prolog / Epilog /
 *		    PrologSlurmctld / EpilogSlurmctld scripts
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
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

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/prep.h"

#include "prep_script.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting t#include <time.h>he type of the plugin or its
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

const char plugin_name[] = "Script PrEp plugin";
const char plugin_type[] = "prep/script";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static bool have_prolog_slurmctld = false;
static bool have_epilog_slurmctld = false;

void (*prolog_slurmctld_callback)(int rc, uint32_t job_id,
				  bool timed_out) = NULL;
void (*epilog_slurmctld_callback)(int rc, uint32_t job_id,
				  bool timed_out) = NULL;

extern int init(void)
{
	if (running_in_slurmctld()) {
		if (slurm_conf.prolog_slurmctld) {
			if (access(slurm_conf.prolog_slurmctld, X_OK) < 0)
				error("Invalid PrologSlurmctld(`%s`): %m",
				      slurm_conf.prolog_slurmctld);
			else
				have_prolog_slurmctld = true;
		}

		if (slurm_conf.epilog_slurmctld) {
			if (access(slurm_conf.epilog_slurmctld, X_OK) < 0)
				error("Invalid EpilogSlurmctld(`%s`): %m",
				      slurm_conf.epilog_slurmctld);
			else
				have_epilog_slurmctld = true;
		}
	}

	return SLURM_SUCCESS;
}

extern void fini(void)
{

}

extern void prep_p_register_callbacks(prep_callbacks_t *callbacks)
{
	/*
	 * Cannot safely run these without a valid callback, so disable
	 * them.
	 */

	if (!(prolog_slurmctld_callback = callbacks->prolog_slurmctld))
		have_prolog_slurmctld = false;
	if (!(epilog_slurmctld_callback = callbacks->epilog_slurmctld))
		have_epilog_slurmctld = false;
}

extern int prep_p_prolog(job_env_t *job_env, slurm_cred_t *cred)
{
	return slurmd_script(job_env, cred, false);
}

extern int prep_p_epilog(job_env_t *job_env, slurm_cred_t *cred)
{
	return slurmd_script(job_env, cred, true);
}

extern int prep_p_prolog_slurmctld(job_record_t *job_ptr, bool *async)
{
	if (!have_prolog_slurmctld) {
		*async = false;
		return SLURM_SUCCESS;
	}

	slurmctld_script(job_ptr, false);

	*async = true;
	return SLURM_SUCCESS;
}

extern int prep_p_epilog_slurmctld(job_record_t *job_ptr, bool *async)
{
	if (!have_epilog_slurmctld) {
		*async = false;
		return SLURM_SUCCESS;
	}

	slurmctld_script(job_ptr, true);

	*async = true;
	return SLURM_SUCCESS;
}

extern void prep_p_required(prep_call_type_t type, bool *required)
{
	*required = false;
	switch (type) {
	case PREP_PROLOG_SLURMCTLD:
		if (running_in_slurmctld() && have_prolog_slurmctld)
			*required = true;
		break;
	case PREP_EPILOG_SLURMCTLD:
		if (running_in_slurmctld() && have_epilog_slurmctld)
			*required = true;
		break;
	case PREP_PROLOG:
	case PREP_EPILOG:
		if (running_in_slurmd())
			*required = true;
		break;
	default:
		return;
	}

	return;
}
