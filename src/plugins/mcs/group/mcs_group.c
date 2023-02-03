/*****************************************************************************\
 *  mcs_group.c - Define mcs management functions for groups
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

#include <grp.h>
#include <string.h>

#include "slurm/slurm_errno.h"
#include "src/interfaces/mcs.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#define MAX_GROUPS 128

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
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]	= "mcs group plugin";
const char plugin_type[]	= "mcs/group";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

/*********************** local variables *********************/

static uint32_t *array_mcs_parameter = NULL;
static uint32_t nb_mcs_groups = 0;
static char *mcs_params_specific = NULL;

static int _get_user_groups(uint32_t user_id, uint32_t group_id,
			    gid_t *groups, int max_groups, int *ngroups);
static int _check_and_load_params();
static int _find_mcs_label(gid_t *groups, int ngroups, char **result);
static int _check_mcs_label(job_record_t *job_ptr, char *label);

/*
 * init() is called when the plugin is loaded, before any other functions
 *	are called.  Put global initialization here.
 */
extern int init(void)
{
	debug("%s loaded", plugin_name);
	mcs_params_specific = slurm_mcs_get_params_specific();

	if (_check_and_load_params() != 0) {
		warning("%s: no group in %s", plugin_type, mcs_params_specific);
		xfree(mcs_params_specific);
		/* no need to check others options : default values used */
		return SLURM_SUCCESS;
	}

	xfree(mcs_params_specific);
	return SLURM_SUCCESS;
}

/*
 * fini() is called when the plugin is removed. Clear any allocated
 *	storage here.
 */
extern int fini(void)
{
	xfree(array_mcs_parameter);
	return SLURM_SUCCESS;
}

/*
 * Get a list of groups associated with a specific user_id
 * Return 0 on success, -1 on failure
 */
static int _get_user_groups(uint32_t user_id, uint32_t group_id,
			    gid_t *groups, int max_groups, int *ngroups)
{
	int rc = SLURM_ERROR;
	char *user_name;

	user_name = uid_to_string((uid_t) user_id);
	*ngroups = max_groups;
#if defined(__APPLE__)
	/*
	 * macOS has (int *) for the third argument instead
	 * of (gid_t *) like FreeBSD, NetBSD, and Linux.
	 */
	rc = getgrouplist(user_name, (gid_t) group_id, (int *) groups, ngroups);
#else
	rc = getgrouplist(user_name, (gid_t) group_id, groups, ngroups);
#endif

	if (rc < 0) {
		error("getgrouplist(%s): %m", user_name);
		rc = SLURM_ERROR;
	} else {
		*ngroups = rc;
		rc = SLURM_SUCCESS;
	}

	xfree(user_name);
	return rc;
}

/*
 * Check params format
 */
static int _check_and_load_params(void)
{
	int i, n;
	int nb_valid_group = 0;
	char *tmp_params = NULL, *name_ptr = NULL, *groups_names = NULL;
	char *name_ptr2 = NULL;
	gid_t gid;

	if (mcs_params_specific == NULL) {
		nb_mcs_groups = 0;
		info("mcs: no group");
		array_mcs_parameter = xmalloc(nb_mcs_groups * sizeof(uint32_t));
		slurm_mcs_reset_params();
		return SLURM_ERROR;
	}

	n = strlen(mcs_params_specific);
	for (i = 0 ; i < n ; i++) {
		if (mcs_params_specific[i] == '|')
			nb_mcs_groups = nb_mcs_groups + 1;
	}

	if (nb_mcs_groups == 0) {
		/* no | in param : just one group */
		if (mcs_params_specific != NULL) {
			if (gid_from_string(mcs_params_specific, &gid ) != 0 ) {
				info("mcs: Only one invalid group : %s. ondemand, ondemandselect set",
				     mcs_params_specific);
				nb_mcs_groups = 0;
				array_mcs_parameter = xmalloc(nb_mcs_groups *
							      sizeof(uint32_t));
				slurm_mcs_reset_params();
				return SLURM_ERROR;
			} else {
				nb_mcs_groups = 1;
				array_mcs_parameter = xmalloc(nb_mcs_groups *
							      sizeof(uint32_t));
				array_mcs_parameter[0] = gid;
				return SLURM_SUCCESS;
			}
		} else {
			/* no group */
			info("mcs: no group in MCSParameters. ondemand, ondemandselect set");
			nb_mcs_groups = 0;
			array_mcs_parameter = xmalloc(nb_mcs_groups *
						      sizeof(uint32_t));
			slurm_mcs_reset_params();
			return SLURM_ERROR;
		}
		return SLURM_SUCCESS;
	}

	nb_mcs_groups = nb_mcs_groups + 1;
	array_mcs_parameter = xmalloc(nb_mcs_groups * sizeof(uint32_t));
	tmp_params = xstrdup(mcs_params_specific);
	groups_names = strtok_r(tmp_params, "|", &name_ptr);

	i = 0;
	while (groups_names) {
		if (i == (nb_mcs_groups - 1)) {
			/* last group, test : */
			if (strstr(groups_names, ":")) {
				groups_names = strtok_r(groups_names, ":",
							&name_ptr2);
			}
		}
		if ( gid_from_string( groups_names, &gid ) != 0 ) {
			info("mcs: Invalid group : %s", groups_names);
			array_mcs_parameter[i] = -1;
		} else {
			array_mcs_parameter[i] = gid;
			nb_valid_group = nb_valid_group + 1;
		}
		i = i + 1;
		groups_names = strtok_r(NULL, "|", &name_ptr);
	}

	/* if no valid group : deselect all params */
	if (nb_valid_group == 0) {
		slurm_mcs_reset_params();
		info ("mcs: No valid groups : ondemand, ondemandselect set");
		xfree(tmp_params);
		return SLURM_ERROR;
	}

	xfree(tmp_params);
	return SLURM_SUCCESS;
}

/*
 * _find_mcs_label() is called to check mcs_label in the parameter's list.
 */
static int _find_mcs_label(gid_t *groups, int ngroups, char **result)
{
	int rc = SLURM_SUCCESS;
	int i = 0;
	int j = 0;
	uint32_t tmp_group ;
	struct group *gr;

	if (ngroups == 0)
		return SLURM_ERROR;

	for (i = 0; i < nb_mcs_groups; i++) {
		for (j = 0; j < ngroups; j++) {
			tmp_group = (uint32_t) groups[j];
			if (array_mcs_parameter[i] == tmp_group) {
				if ((gr = getgrgid(groups[j]))) {
					*result = gr->gr_name;
				} else {
					error("%s: getgrgid(%u): %m",
					      __func__, (uint32_t) groups[j]);
					rc = SLURM_ERROR;
				}
				return rc;
			}
		}
	}
	rc = SLURM_ERROR;

	return rc;
}

/*
 * _check_mcs_label() is called to check a mcs_label of a job
 */
static int _check_mcs_label(job_record_t *job_ptr, char *label)
{
	int rc = SLURM_ERROR;
	int i = 0;
	gid_t gid;
	uint32_t tmp_group ;
	gid_t groups[MAX_GROUPS];
	int ngroups = -1;

	/* test if real unix group */
	if (gid_from_string(label, &gid ) != 0)
		return rc;

	/* test if this group is owned by the user */
	rc = _get_user_groups(job_ptr->user_id, job_ptr->group_id,
			      groups, MAX_GROUPS, &ngroups);
	if (rc)	 /* Failed to get groups */
		return rc;

	rc = SLURM_ERROR;
	for (i = 0; i < ngroups; i++) {
		tmp_group = (uint32_t) groups[i];
		if (gid == tmp_group) {
			rc = SLURM_SUCCESS;
			break;
		}
	}

	if (rc == SLURM_ERROR)
		return rc;

	rc = SLURM_ERROR;
	/* test if mcs_label is in list of possible mcs_label */
	for (i = 0; i < nb_mcs_groups; i++) {
		if (array_mcs_parameter[i] == gid) {
			rc = SLURM_SUCCESS;
			return rc;
		}
	}

	return rc;
}


/*
 * mcs_p_set_mcs_label() is called to obtain/check mcs_label.
 * Return job_ptr->mcs_label value must be xfreed
 */
extern int mcs_p_set_mcs_label(job_record_t *job_ptr, char *label)
{
	char *result = NULL;
	gid_t groups[MAX_GROUPS];
	int ngroups = -1;
	int rc;

	if (label == NULL) {
		if ((slurm_mcs_get_enforced() == 0) && job_ptr->details &&
		    (job_ptr->details->whole_node != WHOLE_NODE_MCS))
			return SLURM_SUCCESS;

		rc = _get_user_groups(job_ptr->user_id,job_ptr->group_id,
			groups, MAX_GROUPS, &ngroups);
		if (rc) {	/* Failed to get groups */
			if (slurm_mcs_get_enforced() == 0)
				return SLURM_SUCCESS;
			else
				return SLURM_ERROR;
		}

		rc = _find_mcs_label(groups, ngroups, &result);
		if (rc) {
			return SLURM_ERROR;
		} else {
			xfree(job_ptr->mcs_label);
			job_ptr->mcs_label = xstrdup(result);
			return SLURM_SUCCESS;
		}
	} else {
		if (_check_mcs_label(job_ptr, label) == 0 )
			return SLURM_SUCCESS;
		else
			return SLURM_ERROR;
	}
}

/*
 * mcs_p_check_mcs_label() is called to check mcs_label.
 */
extern int mcs_p_check_mcs_label(uint32_t user_id, char *mcs_label,
				 bool assoc_locked)
{
	int rc = SLURM_ERROR;
	int i = 0;
	gid_t gid;
	gid_t slurm_user_gid;
	uint32_t tmp_group ;
	gid_t groups[MAX_GROUPS];
	uint32_t group_id;
	int ngroups = -1;

	if (mcs_label != NULL) {
		/* test if real unix group */
		if (gid_from_string(mcs_label, &gid ) != 0)
			return rc;

		/* test if this group is owned by the user */
		slurm_user_gid = gid_from_uid(user_id);
		group_id = (uint32_t) slurm_user_gid;
		rc = _get_user_groups(user_id, group_id, groups, MAX_GROUPS,
				      &ngroups);
		if (rc)	/* Failed to get groups */
			return rc;

		rc = SLURM_ERROR;
		for (i = 0; i < ngroups; i++) {
			tmp_group = (uint32_t) groups[i];
			if (gid == tmp_group) {
				rc = SLURM_SUCCESS;
				break;
			}
		}
	} else
		rc = SLURM_SUCCESS;

	return rc;
}
