/*****************************************************************************\
 *  hres.h - Hierarchical resource (HRES) plugin interface.
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

#ifndef _INTERFACES_HRES_H
#define _INTERFACES_HRES_H

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/slurmctld/licenses.h"

/*
 * One context per hierarchical resource that configures a plugin. Multiple
 * contexts may be backed by the same plugin type.
 */
typedef struct {
	list_t *config; /* list of hres_config_t, read-only for plugin */
	uint16_t hres_id; /* HRES this context serves */
	int idx; /* index into ops[]/g_context[] */
	time_t last_maintenance; /* time of last maintenance run, 0 if never */
	uint32_t maintenance_cycle; /* seconds between maintenance runs */
	uint32_t maintenance_timeout; /* max seconds for a maintenance run */
	void *plugin_ctx; /* per-context state, alloc/free'd by the plugin */
} hres_ctx_t;
/*
 * Initialize the HRES plugin interface and load the plugins. May be called
 * again to load plugin types referenced by a new configuration.
 *
 * IN hres_recs - cluster license list (list_t of licenses_t); only the plugin
 *		  types it references are loaded. NULL loads every installed
 *		  HRES plugin.
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_init(list_t *hres_recs);

/*
 * Create one context per hierarchical resource that defines a plugin and call
 * each plugin's create_context callback. A resource without a plugin is
 * ignored. The plugin allocates and owns the context's plugin_ctx.
 *
 * Any existing context is destroyed first, so this may be called again to pick
 * up a new configuration.
 *
 * IN hres_recs - cluster license list (list_t of licenses_t); records whose
 *		  hres_rec.plugin_type is set define an HRES plugin context
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_create_context(list_t *hres_recs);

/*
 * Destroy all contexts by calling each plugin's destroy_context callback. The
 * plugin frees the context's plugin_ctx.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_destroy_context(void);

/*
 * Terminate the HRES plugin interface and unload the plugins.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_fini(void);

/*
 * Start the maintenance thread that periodically invokes each context's
 * plugin maintenance callback. A no-op when no context is configured.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_maintenance_start(void);

/*
 * Stop and join the maintenance thread. Safe to call when not running.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_maintenance_stop(void);

/*
 * Stop the maintenance thread and wait until the background work of every
 * plugin has finished and been applied to slurmctld state. No controller
 * callback may start after this. Must be called without slurmctld locks, the
 * plugins may need them to finish.
 *
 * RET SLURM_SUCCESS or a Slurm errno
 */
extern int hres_g_controller_stop(void);

#endif /* !_INTERFACES_HRES_H */
