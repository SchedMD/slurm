/*****************************************************************************\
 *  mcs_user.c - Define mcs management functions for users
 *****************************************************************************
 *  Copyright (C) 2015 CEA/DAM/DIF
 *  Written by Aline Roy <aline.roy@cea.fr>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "slurm/slurm_errno.h"
#include "src/common/slurm_mcs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

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
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  SLURM will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]        = "mcs user plugin";
const char plugin_type[]        = "mcs/user";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*********************** local variables *********************/

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	return SLURM_SUCCESS;
}

/*
 * mcs_p_set_mcs_label() is called to obtain mcs_label.
 */
extern int mcs_p_set_mcs_label (struct job_record *job_ptr, char *label)
{
	char *user = NULL;
	int rc = SLURM_SUCCESS;

	user = uid_to_string((uid_t) job_ptr->user_id);
	xfree(job_ptr->mcs_label);

	if (label != NULL) {
		/* test label param */
		if (xstrcmp(label, user) == 0)
			job_ptr->mcs_label = xstrdup(user);
		else
			rc = SLURM_ERROR;
	} else {
		if ((slurm_mcs_get_enforced() == 0) && job_ptr->details &&
		    (job_ptr->details->whole_node != WHOLE_NODE_MCS))
			;
		else
			job_ptr->mcs_label = xstrdup(user);
	}

	xfree(user);
	return rc;
}

/*
 * mcs_p_check_mcs_label() is called to check mcs_label.
 */
extern int mcs_p_check_mcs_label (uint32_t user_id, char *mcs_label)
{
	char *user = NULL;
	int rc = SLURM_SUCCESS;

	user = uid_to_string((uid_t) user_id);
	if (mcs_label != NULL) {
		if (xstrcmp(mcs_label, user) == 0)
			rc = SLURM_SUCCESS;
		else
			rc = SLURM_ERROR;
	} else
		rc = SLURM_SUCCESS;

	xfree(user);
	return rc;
}
