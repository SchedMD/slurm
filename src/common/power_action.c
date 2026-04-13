/*****************************************************************************\
 *  power_action.c - Configuration and helpers for PowerAction entries used to
 *  run node power up, power down, and reboot scripts on either the slurmctld
 *  or slurmd host.
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

#include <sys/stat.h>

#include "src/common/list.h"
#include "src/common/power_action.h"
#include "src/common/read_config.h"
#include "src/common/run_in_daemon.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static int _match_action_name(void *action_ptr, void *name)
{
	power_action_t *action = (power_action_t *) action_ptr;
	return !xstrcasecmp(action->name, (char *) name);
}

extern power_action_t *power_action_find(list_t *power_action_list, char *name)
{
	if (!power_action_list || !name)
		return NULL;
	return list_find_first(power_action_list, _match_action_name, name);
}

extern const char *power_action_type_name(power_action_type_t type)
{
	switch (type) {
	case POWER_ACTION_RESUME:
		return POWER_ACTION_NAME_RESUME;
	case POWER_ACTION_SUSPEND:
		return POWER_ACTION_NAME_SUSPEND;
	case POWER_ACTION_RESUME_FAIL:
		return POWER_ACTION_NAME_RESUME_FAIL;
	case POWER_ACTION_REBOOT:
		return POWER_ACTION_NAME_REBOOT;
	default:
		error("%s: invalid power action type %d", __func__,
			      type);
		return NULL;
	}
}

extern power_action_t *power_action_find_or_create(list_t *power_action_list,
						   char *field,
						   power_action_type_t type)
{
	power_action_t *action = power_action_find(power_action_list, field);
	bool on_slurmctld = true;
	if (type == POWER_ACTION_REBOOT) {
		/*
		 * Legacy RebootProgram runs on compute nodes unless
		 * reboot_from_controller is set. But when run from the slurmd,
		 * assume it should be run there.
		 */
		if (running_in_slurmctld() &&
		    xstrcasestr(slurm_conf.slurmctld_params,
				"reboot_from_controller")) {
			on_slurmctld = true;
		} else {
			on_slurmctld = false;
		}
	}
	/*
	 * If an action of name $source already exists, then it will be found
	 * first instead of the one created here. Without switching to a hashmap
	 * or adding another lookup here, there's nothing to be done about that.
	 */
	if (!action && field && field[0]) {
		const char *name = power_action_type_name(type);
		if (!name) {
			error("%s: invalid power action type %d", __func__,
			      type);
			return NULL;
		}
		if (power_action_create(name, xstrdup(field), on_slurmctld,
					&action)) {
			error("Failed to create power action %s", name);
			return NULL;
		}
		list_append(power_action_list, action);
	}
	return action;
}

extern bool power_action_valid_prog(power_action_t *action)
{
	char *file_name;
	struct stat buf;

	if (!action || !action->program || !action->program[0]) {
		error("%s: missing program path", __func__);
		return false;
	}

	/*
	 * If the action is not going to be run on the current daemon,
	 * consider it valid here
	 */
	if (action->on_slurmctld && !running_in_slurmctld()) {
		return true;
	} else if (!action->on_slurmctld && !running_in_slurmd()) {
		return true;
	}

	file_name = action->program;
	if (file_name[0] != '/') {
		error("power_save program %s not absolute pathname", file_name);
		return false;
	}

	if (stat(file_name, &buf)) {
		error("power_save program %s not found", file_name);
		return false;
	}

	if (access(file_name, X_OK)) {
		error("power_save program %s is not executable", file_name);
		return false;
	}

	if (buf.st_mode & 022) {
		error("power_save program %s has group or world write permission",
		      file_name);
		return false;
	}

	return true;
}

extern power_action_t *power_action_copy(power_action_t *action)
{
	power_action_t *copy;

	if (!action)
		return NULL;

	copy = xmalloc(sizeof(*copy));
	copy->name = xstrdup(action->name);
	copy->on_slurmctld = action->on_slurmctld;
	copy->program = xstrdup(action->program);
	copy->argc = action->argc;
	copy->argv = xcalloc(copy->argc, sizeof(char *));
	for (int i = 0; i < copy->argc; i++)
		copy->argv[i] = xstrdup(action->argv[i]);
	return copy;
}

extern int power_action_create(const char *name, char *program,
			       bool on_slurmctld, power_action_t **action_ptr)
{
	power_action_t *action = NULL;
	char *save_ptr = NULL;
	char *tok = NULL;
	char *program_copy = NULL;
	size_t size = 0;
	size_t argc = 0;
	char **argv = NULL;

	if ((tok = strtok_r(program, " ", &save_ptr))) {
		program_copy = xstrdup(tok);
	} else {
		error("PowerAction '%s' requires Program= with a non-empty path",
		      name);
		xfree(program);
		return SLURM_ERROR;
	}
	while ((tok = strtok_r(NULL, " ", &save_ptr))) {
		/* stay 1 larger than argc to allow for NULL termination */
		if (size <= argc + 1) {
			size = argc + 4;
			argv = xrealloc(argv, size * sizeof(char *));
		}
		argv[argc++] = xstrdup(tok);
	}
	action = xmalloc(sizeof(*action));
	if (argc > 0) {
		argv[argc] = NULL;
		action->argc = argc;
		action->argv = argv;
	} else {
		action->argv = NULL;
		action->argc = 0;
	}

	action->name = xstrdup(name);
	action->program = program_copy;
	action->on_slurmctld = on_slurmctld;
	xfree(program);
	*action_ptr = action;
	return SLURM_SUCCESS;
}

extern void power_action_destroy(void *ptr)
{
	power_action_t *action = ptr;

	if (!action)
		return;

	xfree(action->program);
	xfree(action->name);
	for (int i = 0; action->argv && i < action->argc; i++)
		xfree(action->argv[i]);
	xfree(action->argv);
	xfree(action);
}
