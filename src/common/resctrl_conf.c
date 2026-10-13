/*****************************************************************************\
 *  resctrl_conf.c - resctrl.yaml configuration
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

#include <stddef.h>

#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/resctrl_conf.h"

extern void resctrl_conf_group_free(resctrl_conf_group_t *group)
{
	if (!group)
		return;

	xfree(group->name);
	xfree(group->schemata);
	xfree(group);
}

extern void resctrl_conf_free(resctrl_conf_t *conf)
{
	if (!conf)
		return;

	FREE_NULL_LIST(conf->groups);
	xfree(conf->mount_options);
	xfree(conf);
}

static const char *required_groups[] = {
	RESCTRL_GROUP_CPU_ONLY,
	RESCTRL_GROUP_GPU_ONE_PER_SOCKET,
	RESCTRL_GROUP_GPU_TWO_PER_SOCKET,
};

/*
 * list_find_first() callback.
 * IN x - resctrl_conf_group_t to compare
 * IN key - group name
 * RET 1 when the names match, else 0
 */
static int _find_group(void *x, void *key)
{
	resctrl_conf_group_t *group = x;

	return !xstrcmp(group->name, key);
}

/*
 * IN name - group name
 * RET true if the name is one of the required groups
 */
static bool _known_group(const char *name)
{
	for (int i = 0; i < ARRAY_SIZE(required_groups); i++) {
		if (!xstrcmp(name, required_groups[i]))
			return true;
	}

	return false;
}

/*
 * list_for_each() callback: validate one group.
 * IN x - resctrl_conf_group_t to validate
 * IN arg - the list it belongs to, to detect duplicates
 * RET 0 to continue, -1 to stop on the first invalid entry
 */
static int _validate_group(void *x, void *arg)
{
	resctrl_conf_group_t *group = x;
	list_t *groups = arg;

	if (!_known_group(group->name)) {
		error("%s: unknown group \"%s\", only %s, %s and %s are accepted",
		      RESCTRL_CONF_FILE, group->name, RESCTRL_GROUP_CPU_ONLY,
		      RESCTRL_GROUP_GPU_ONE_PER_SOCKET,
		      RESCTRL_GROUP_GPU_TWO_PER_SOCKET);
		return -1;
	}

	if (list_find_first(groups, _find_group, group->name) != group) {
		error("%s: duplicate group \"%s\"",
		      RESCTRL_CONF_FILE, group->name);
		return -1;
	}

	if (!group->schemata || !group->schemata[0]) {
		error("%s: group \"%s\" has an empty schemata",
		      RESCTRL_CONF_FILE, group->name);
		return -1;
	}

	return 0;
}

extern int resctrl_conf_load(resctrl_conf_t **conf_out)
{
	int rc = EINVAL;
	resctrl_conf_t *conf = NULL;

	*conf_out = NULL;

	if ((rc = CONF_PARSE(RESCTRL_CONF_PTR, RESCTRL_CONF_FILE, conf))) {
		if (rc != ENOENT)
			error("Failed to load %s: %s",
			      RESCTRL_CONF_FILE, slurm_strerror(rc));
		return rc;
	}

	/*
	 * Every group must be one of the required ones and appear once, so
	 * the right number of valid groups means all of them are defined.
	 */
	if (!conf || !conf->groups ||
	    (list_count(conf->groups) != ARRAY_SIZE(required_groups))) {
		error("%s: the groups %s, %s and %s must all be defined",
		      RESCTRL_CONF_FILE, RESCTRL_GROUP_CPU_ONLY,
		      RESCTRL_GROUP_GPU_ONE_PER_SOCKET,
		      RESCTRL_GROUP_GPU_TWO_PER_SOCKET);
		resctrl_conf_free(conf);
		return SLURM_ERROR;
	}

	if (list_for_each(conf->groups, _validate_group, conf->groups) < 0) {
		resctrl_conf_free(conf);
		return SLURM_ERROR;
	}

	debug("%s: loaded %d resctrl group(s)",
	      RESCTRL_CONF_FILE, list_count(conf->groups));
	*conf_out = conf;

	return SLURM_SUCCESS;
}

extern resctrl_conf_group_t *resctrl_conf_find_group(resctrl_conf_t *conf,
						     const char *name)
{
	return list_find_first(conf->groups, _find_group, (void *) name);
}
