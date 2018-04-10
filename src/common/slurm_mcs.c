/*****************************************************************************\
 *  slurm_mcs.c - Define mcs plugin functions
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

#include "src/common/slurm_mcs.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/xstring.h"

typedef struct slurm_mcs_ops {
	int (*set)		(struct job_record *job_ptr,char *label);
	int (*check)		(uint32_t user_id, char *mcs_label);
} slurm_mcs_ops_t;

/*
 * Must be synchronized with slurm_mcs_ops_t above.
 */
static const char *syms[] = {
	"mcs_p_set_mcs_label",
	"mcs_p_check_mcs_label"
};

static slurm_mcs_ops_t ops;
static plugin_context_t *g_mcs_context = NULL;
static pthread_mutex_t g_mcs_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
static bool private_data = false;
static bool label_strict_enforced = false;
static uint32_t select_value = MCS_SELECT_ONDEMANDSELECT;
static char *mcs_params = NULL;
static char *mcs_params_common = NULL;
static char *mcs_params_specific = NULL;

static int _slurm_mcs_check_and_load_enforced(char *params);
static int _slurm_mcs_check_and_load_select(char *params);
static int _slurm_mcs_check_and_load_privatedata(char *params);

/*
 * Initialize context for mcs plugin
 */
extern int slurm_mcs_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "mcs";
	char *type = NULL;
	char *sep;

	if (init_run && g_mcs_context)
		return retval;

	slurm_mutex_lock(&g_mcs_context_lock);
	if (g_mcs_context)
		goto done;

	xfree(mcs_params);
	xfree(mcs_params_common);
	xfree(mcs_params_specific);
	type = slurm_get_mcs_plugin();
	mcs_params = slurm_get_mcs_plugin_params();

	if (mcs_params == NULL)
		info("No parameter for mcs plugin, default values set");
	else {
		mcs_params_common = xstrdup(mcs_params);
		sep = xstrchr(mcs_params_common, ':');
		if (sep != NULL) {
			if (sep[1] != '\0')
				mcs_params_specific = xstrdup(sep + 1);
			*sep = '\0';
		}
	}

	_slurm_mcs_check_and_load_privatedata(mcs_params_common);
	_slurm_mcs_check_and_load_enforced(mcs_params_common);
	_slurm_mcs_check_and_load_select(mcs_params_common);

	g_mcs_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_mcs_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}

	init_run = true;
done:
	slurm_mutex_unlock(&g_mcs_context_lock);
	xfree(type);
	return retval;
}

extern int slurm_mcs_fini(void)
{
	int rc = SLURM_SUCCESS;

	if (!g_mcs_context)
		return rc;

	init_run = false;
	rc = plugin_context_destroy(g_mcs_context);
	g_mcs_context = NULL;
	xfree(mcs_params_common);
	xfree(mcs_params_specific);
	xfree(mcs_params);
	return rc;
}

extern int slurm_mcs_reconfig(void)
{
	slurm_mcs_fini();
	return slurm_mcs_init();
}

/* slurm_mcs_get_params_specific
 * RET mcs_params_common_spec, must be xfreed by caller
 */

extern char *slurm_mcs_get_params_specific(void)
{
	char *mcs_params_common_spec = NULL;
	mcs_params_common_spec = xstrdup(mcs_params_specific);
	return mcs_params_common_spec;
}

static int _slurm_mcs_check_and_load_enforced(char *params)
{
	label_strict_enforced = false;

	if ((params != NULL) && xstrcasestr(params, "enforced"))
		label_strict_enforced = true;
	else
		info("mcs: MCSParameters = %s. ondemand set.", params);

	return SLURM_SUCCESS;
}

static int _slurm_mcs_check_and_load_select(char *params)
{
	select_value = MCS_SELECT_ONDEMANDSELECT;

	if (params == NULL) {
		return SLURM_SUCCESS;
	}

	if (xstrcasestr(params, "noselect"))
		select_value = MCS_SELECT_NOSELECT;
	else if (xstrcasestr(params, "ondemandselect"))
		select_value = MCS_SELECT_ONDEMANDSELECT;
	else if (xstrcasestr(params, "select"))
		select_value = MCS_SELECT_SELECT;
	else
		info("mcs: MCSParameters = %s. ondemandselect set.", params);

	return SLURM_SUCCESS;
}

static int _slurm_mcs_check_and_load_privatedata(char *params)
{
	if (params == NULL) {
		private_data = false;
		return SLURM_SUCCESS;
	}

	if (xstrcasestr(params, "privatedata"))
		private_data = true;
	else
		private_data = false;

	return SLURM_SUCCESS;
}

extern int slurm_mcs_reset_params(void)
{
	label_strict_enforced = false;
	select_value = MCS_SELECT_ONDEMANDSELECT;
	private_data = false;

	return SLURM_SUCCESS;
}

extern int slurm_mcs_get_enforced(void)
{
	return label_strict_enforced;
}

extern int slurm_mcs_get_select(struct job_record *job_ptr)
{
	if ((select_value == MCS_SELECT_SELECT) ||
	    ((select_value == MCS_SELECT_ONDEMANDSELECT) &&
	    job_ptr->details &&
	    (job_ptr->details->whole_node == WHOLE_NODE_MCS)))
		return 1;
	else
		return 0;
}

extern int slurm_mcs_get_privatedata(void)
{
	return private_data;
}

extern int mcs_g_set_mcs_label(struct job_record *job_ptr, char *label)
{
	if (slurm_mcs_init() < 0)
		return 0;

	return (int) (*(ops.set))(job_ptr, label);
}

extern int mcs_g_check_mcs_label(uint32_t user_id, char *mcs_label)
{
	if (slurm_mcs_init() < 0)
		return 0;

	return (int)(*(ops.check))(user_id, mcs_label);
}
