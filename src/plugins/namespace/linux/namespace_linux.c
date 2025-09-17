/*****************************************************************************\
 *  namespace_linux.c - Define namespace plugin for creating temporary linux
 *  		        namespaces for the job to provde some isolation between
 *  		        jobs on the same node.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
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
#define _GNU_SOURCE
#include <inttypes.h>
#include <stdbool.h>

#include "src/common/slurm_xlator.h"

#include "src/common/run_in_daemon.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

#include "read_nsconf.h"

#if defined(__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

const char plugin_name[] = "namespace linux plugin";
const char plugin_type[] = "namespace/linux";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static slurm_ns_conf_t *ns_conf = NULL;
static bool plugin_disabled = false;

static bool _is_plugin_disabled(char *basepath)
{
	return ((!basepath) || (!xstrncasecmp(basepath, "none", 4)));
}

extern int init(void)
{
	if (running_in_slurmd()) {
		/*
		 * Only init the config here for the slurmd. It will be sent by
		 * the slurmd to the slurmstepd at launch time.
		 */
		if (!(ns_conf = init_slurm_ns_conf())) {
			error("%s: Configuration not read correctly: Does '%s' not exist?",
			      plugin_type, ns_conf_file);
			return SLURM_ERROR;
		}
		plugin_disabled = _is_plugin_disabled(ns_conf->basepath);
		debug("namespace.conf read successfully");
	}

	debug("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

extern void fini(void)
{
#ifdef MEMORY_LEAK_DEBUG
	free_ns_conf();
#endif
	debug("%s unloaded", plugin_name);
}

extern int namespace_p_restore(char *dir_name, bool recover)
{
	if (plugin_disabled)
		return SLURM_SUCCESS;

	return SLURM_SUCCESS;
}

extern int namespace_p_join_external(uint32_t job_id, list_t *ns_map)
{
	if (plugin_disabled)
		return SLURM_SUCCESS;

	return SLURM_SUCCESS;
}

extern int namespace_p_join(slurm_step_id_t *step_id, uid_t uid,
			    bool step_create)
{
	if (plugin_disabled)
		return SLURM_SUCCESS;

	return SLURM_SUCCESS;
}

extern int namespace_p_stepd_create(stepd_step_rec_t *step)
{
	if (plugin_disabled)
		return SLURM_SUCCESS;

	return SLURM_SUCCESS;
}

extern int namespace_p_stepd_delete(slurm_step_id_t *step_id)
{
	if (plugin_disabled)
		return SLURM_SUCCESS;

	return SLURM_SUCCESS;
}

extern int namespace_p_send_stepd(int fd)
{
	return SLURM_SUCCESS;
}

extern int namespace_p_recv_stepd(int fd)
{
	return SLURM_SUCCESS;
}
