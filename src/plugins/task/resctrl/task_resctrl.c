/*****************************************************************************\
 *  task_resctrl.c - task plugin confining steps to Linux resctrl groups
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "config.h"

#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/resctrl_conf.h"
#include "src/common/run_in_daemon.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/task.h"

#include "src/plugins/task/resctrl/resctrl_fs.h"

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
 * the plugin (e.g., "task" for task control) and <method> is a description
 * of how this plugin satisfies that application.  Slurm will only load
 * a task plugin if the plugin_type string has a prefix of "task/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "task resctrl plugin";
const char plugin_type[] = "task/resctrl";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static resctrl_conf_t *resctrl_conf = NULL; /* slurmd: resctrl.yaml */

/*
 * list_for_each() callback: count the existing groups resctrl.yaml does not
 * define.
 * IN x - name of an existing group
 * IN/OUT arg - uint32_t counter
 * RET 0
 */
static int _count_foreign_groups(void *x, void *arg)
{
	char *name = x;
	uint32_t *count = arg;

	if (!resctrl_conf_find_group(resctrl_conf, name))
		(*count)++;

	return 0;
}

/*
 * Check how many CLOSIDs this hardware supports, how many groups are already
 * created, and fail if we have no room for our configured Slurm groups.
 */
static void _check_max_groups(void)
{
	uint32_t max_groups = resctrl_fs_max_groups();
	uint32_t configured = list_count(resctrl_conf->groups);
	uint32_t foreign = 0;
	list_t *existing = NULL;

	if (!max_groups) {
		debug("cannot determine how many resctrl groups the hardware supports");
		return;
	}

	existing = resctrl_fs_list_groups();
	list_for_each(existing, _count_foreign_groups, &foreign);
	FREE_NULL_LIST(existing);

	if ((configured + foreign + 1) > max_groups)
		fatal("%s configures %u resctrl groups, which with the root group and %u groups not managed by Slurm exceeds the %u groups the hardware supports",
		      RESCTRL_CONF_FILE, configured, foreign, max_groups);
}

/*
 * list_for_each() callback: make one configured group exist with its
 * schemata. Existing directories are reused, whoever created them.
 * IN x - resctrl_conf_group_t to apply
 * IN arg - unused
 * RET 0
 */
static int _apply_group(void *x, void *arg)
{
	resctrl_conf_group_t *group = x;
	char *status = NULL;
	int rc = EINVAL;

	rc = resctrl_fs_group_create(group->name);
	if (!rc) {
		info("created resctrl group %s", group->name);
	} else if (rc == EEXIST) {
		info("using existing resctrl group %s", group->name);
	} else {
		status = resctrl_fs_last_cmd_status();
		fatal("cannot create resctrl group %s: %s (%s)", group->name,
		      slurm_strerror(rc), (status ? status : "no status"));
	}

	if ((rc = resctrl_fs_write_schemata(group->name, group->schemata))) {
		status = resctrl_fs_last_cmd_status();
		fatal("cannot apply the schemata of resctrl group %s: %s (%s)",
		      group->name, slurm_strerror(rc),
		      (status ? status : "no status"));
	}

	return 0;
}

/*
 * Bring the resctrl filesystem in line with resctrl.yaml. Runs on every
 * slurmd start, which includes every reconfigure as slurmd re-execs. On a
 * node without kernel support for resctrl, without resctrl.yaml, or where
 * the filesystem cannot be mounted the plugin does nothing.
 * RET SLURM_SUCCESS
 */
static int _slurmd_init(void)
{
	int rc = EINVAL;

	if (!resctrl_fs_supported()) {
		info("the kernel does not support resctrl, resctrl groups are not managed on this node");
		return SLURM_SUCCESS;
	}

	if ((rc = resctrl_conf_load(&resctrl_conf))) {
		if (rc != ENOENT)
			fatal("cannot load %s", RESCTRL_CONF_FILE);
		info("%s not found, resctrl groups are not managed on this node",
		     RESCTRL_CONF_FILE);
		return SLURM_SUCCESS;
	}

	if (resctrl_fs_mount(resctrl_conf->mount_options)) {
		error("resctrl groups are not managed on this node");
		return SLURM_SUCCESS;
	}

	_check_max_groups();
	list_for_each(resctrl_conf->groups, _apply_group, NULL);

	return SLURM_SUCCESS;
}

extern int init(void)
{
	debug("%s loaded", plugin_name);

	if (running_in_slurmd())
		return _slurmd_init();

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	resctrl_conf_free(resctrl_conf);
	resctrl_conf = NULL;

	debug("%s unloaded", plugin_name);
}

extern int task_p_slurmd_batch_request(batch_job_launch_msg_t *req)
{
	return SLURM_SUCCESS;
}

extern int task_p_slurmd_launch_request(launch_tasks_request_msg_t *req,
					uint32_t node_id, char **err_msg)
{
	return SLURM_SUCCESS;
}

extern int task_p_pre_setuid(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern int task_p_pre_launch_priv(stepd_step_rec_t *step, uint32_t node_tid,
				  uint32_t global_tid)
{
	return SLURM_SUCCESS;
}

extern int task_p_pre_launch(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern int task_p_post_term(stepd_step_rec_t *step,
			    stepd_step_task_info_t *task)
{
	return SLURM_SUCCESS;
}

extern int task_p_post_step(stepd_step_rec_t *step)
{
	return SLURM_SUCCESS;
}

extern int task_p_add_pid(pid_t pid)
{
	return SLURM_SUCCESS;
}

extern int task_p_update_mem_limit(stepd_step_rec_t *step, uint64_t new_job_mem,
				   uint64_t new_step_mem)
{
	return SLURM_SUCCESS;
}
