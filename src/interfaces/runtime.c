/*****************************************************************************\
 *  runtime.c - job runtime plugin interface.
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

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/runtime.h"

#define PLUGIN_TYPE "runtime"

typedef struct {
	int (*init)(runtime_context_t context);
	int (*fini)(void);
	int (*setup)(slurmd_conf_t *conf, stepd_step_rec_t *step,
		     slurm_addr_t *cli, slurm_msg_t *msg);
	void (*cleanup)(slurmd_conf_t *conf, stepd_step_rec_t *step);
	void (*task_init)(slurmd_conf_t *conf, stepd_step_rec_t *step,
			  stepd_step_task_info_t *task);
	void (*run)(slurmd_conf_t *conf, stepd_step_rec_t *step,
		    stepd_step_task_info_t *task);
} opts_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for opts_t.
 */
static const char *syms[] = {
	"runtime_p_init",    "runtime_p_fini",      "runtime_p_setup",
	"runtime_p_cleanup", "runtime_p_task_init", "runtime_p_run",
};

static opts_t ops = { 0 };
static plugin_context_t *g_context = NULL;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;

extern int runtime_g_init(const char *plugin_name, runtime_context_t context)
{
	const char *plugin_type = PLUGIN_TYPE;
	const char *type = plugin_name;
	char *full_type = NULL;
	int rc = SLURM_SUCCESS;

	if (!type || !type[0])
		type = slurm_conf.def_runtime_plugin;
	if (!type || !type[0])
		type = DEFAULT_RUNTIME_PLUGIN;

	slurm_mutex_lock(&init_lock);

	if (plugin_inited != PLUGIN_NOT_INITED) {
		slurm_mutex_unlock(&init_lock);
		return SLURM_SUCCESS;
	}

	/* Accept both "oci" and the full "runtime/oci" plugin name. */
	if (!xstrncmp(type, "runtime/", 8))
		type += 8;
	full_type = xstrdup_printf("%s/%s", plugin_type, type);

	if (!(g_context = plugin_context_create(plugin_type, full_type,
						(void **) &ops, syms,
						sizeof(syms)))) {
		error("%s: cannot create %s context for %s",
		      __func__, plugin_type, full_type);
		rc = ESLURM_PLUGIN_INVALID;
		goto done;
	}

	if ((rc = ops.init(context))) {
		plugin_context_destroy(g_context);
		g_context = NULL;
		goto done;
	}

	plugin_inited = PLUGIN_INITED;
done:
	xfree(full_type);
	slurm_mutex_unlock(&init_lock);
	return rc;
}

extern void runtime_g_fini(void)
{
	int rc = EINVAL;

	slurm_mutex_lock(&init_lock);

	if (g_context) {
		xassert(plugin_inited == PLUGIN_INITED);

		ops.fini();

		if ((rc = plugin_context_destroy(g_context)))
			fatal_abort("%s: plugin_context_destroy() failed: %s",
				__func__, slurm_strerror(rc));

		g_context = NULL;
	}

	plugin_inited = PLUGIN_NOT_INITED;

	slurm_mutex_unlock(&init_lock);
}

extern int runtime_g_setup(slurmd_conf_t *conf, stepd_step_rec_t *step,
			   slurm_addr_t *cli, slurm_msg_t *msg)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return ops.setup(conf, step, cli, msg);
}

extern void runtime_g_cleanup(slurmd_conf_t *conf, stepd_step_rec_t *step)
{
	xassert(plugin_inited == PLUGIN_INITED);
	ops.cleanup(conf, step);
}

extern void runtime_g_task_init(slurmd_conf_t *conf, stepd_step_rec_t *step,
				stepd_step_task_info_t *task)
{
	xassert(plugin_inited == PLUGIN_INITED);
	ops.task_init(conf, step, task);
}

extern void runtime_g_run(slurmd_conf_t *conf, stepd_step_rec_t *step,
			  stepd_step_task_info_t *task)
{
	xassert(plugin_inited == PLUGIN_INITED);
	ops.run(conf, step, task);
}
