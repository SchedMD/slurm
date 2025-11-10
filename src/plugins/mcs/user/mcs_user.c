/*****************************************************************************\
 *  mcs_user.c - Define mcs management functions for users
 *****************************************************************************
 *  Copyright (C) 2015 CEA/DAM/DIF
 *  Written by Aline Roy <aline.roy@cea.fr>
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

#include "slurm/slurm_errno.h"
#include "src/interfaces/mcs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "mcs user plugin";
const char plugin_type[] = "mcs/user";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*********************** local variables *********************/

extern int init(void)
{
	debug("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern void fini(void)
{
	return;
}

/*
 * mcs_p_set_mcs_label() is called to obtain mcs_label.
 */
extern int mcs_p_set_mcs_label(job_record_t *job_ptr, char *label)
{
	char *user = NULL;
	int rc = SLURM_SUCCESS;
	char *mcs_label = NULL;

	user = uid_to_string((uid_t) job_ptr->user_id);

	if (label != NULL) {
		/* test label param */
		if (xstrcmp(label, user) == 0)
			mcs_label = xstrdup(user);
		else
			rc = SLURM_ERROR;
	} else {
		if ((slurm_mcs_get_enforced() == 0) && job_ptr->details &&
		    !(job_ptr->details->whole_node & WHOLE_NODE_MCS))
			;
		else
			mcs_label = xstrdup(user);
	}

	if (!rc) {
		xfree(job_ptr->mcs_label);
		job_ptr->mcs_label = mcs_label;
	}

	xfree(user);
	return rc;
}

/*
 * mcs_p_check_mcs_label() is called to check mcs_label.
 */
extern int mcs_p_check_mcs_label(uint32_t user_id, char *mcs_label,
				 bool assoc_locked)
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
