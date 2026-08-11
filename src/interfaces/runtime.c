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
	int (*run)(slurmd_conf_t *conf, stepd_step_rec_t *step,
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

/*
 * Plugins are loaded into parallel arrays and referenced by index, so that a
 * job can select a different runtime for each heterogeneous component.
 *
 * Index 0 is a permanently empty placeholder, so a zero-initialized index
 * means "no runtime resolved" without any caller having to say so.
 *
 * The single-context caller (slurmstepd) loads exactly one plugin and always
 * reaches it at RUNTIME_IDX_DEFAULT, with plugin_inited saying whether it has
 * been loaded yet.
 */
static opts_t *ops = NULL;
static plugin_context_t **g_context = NULL;
/* Fully qualified type of each loaded plugin, e.g. "runtime/oci". */
static char **loaded_types = NULL;
static int g_context_cnt = 0;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;
static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;

/* IN full_type - fully qualified plugin type. init_lock must be held. */
static int _find_loaded(const char *full_type)
{
	for (int i = 1; i < g_context_cnt; i++)
		if (!xstrcmp(loaded_types[i], full_type))
			return i;

	return RUNTIME_IDX_INVALID;
}

/*
 * Load a runtime plugin, or find one already loaded.
 * IN plugin_name - plugin name, or NULL for DefRuntimePlugin
 * IN context - calling context handed to the plugin
 * OUT idx_ptr - index of the plugin, or RUNTIME_IDX_INVALID on failure
 * RET SLURM_SUCCESS or an error
 */
static int _load_runtime(const char *plugin_name, runtime_context_t context,
			 int *idx_ptr)
{
	const char *plugin_type = PLUGIN_TYPE;
	const char *type = plugin_name;
	char *full_type = NULL;
	int idx = RUNTIME_IDX_INVALID;
	int rc = SLURM_SUCCESS;

	if (!type || !type[0])
		type = slurm_conf.def_runtime_plugin;
	if (!type || !type[0])
		type = DEFAULT_RUNTIME_PLUGIN;

	/* Accept both "oci" and the full "runtime/oci" plugin name. */
	if (!xstrncmp(type, "runtime/", 8))
		type += 8;
	full_type = xstrdup_printf("%s/%s", plugin_type, type);

	slurm_mutex_lock(&init_lock);

	if ((idx = _find_loaded(full_type)) != RUNTIME_IDX_INVALID)
		goto done;

	/*
	 * g_context_cnt is one past the highest loaded index, so it is also
	 * the next free slot. Index 0 stays an empty placeholder.
	 */
	if (g_context_cnt)
		idx = g_context_cnt;
	else
		idx = RUNTIME_IDX_DEFAULT;

	xrecalloc(ops, (idx + 1), sizeof(*ops));
	xrecalloc(g_context, (idx + 1), sizeof(*g_context));
	xrecalloc(loaded_types, (idx + 1), sizeof(*loaded_types));

	if (!(g_context[idx] = plugin_context_create(plugin_type, full_type,
						     (void **) &ops[idx], syms,
						     sizeof(syms)))) {
		error("%s: cannot create %s context for %s",
		      __func__, plugin_type, full_type);
		rc = ESLURM_PLUGIN_INVALID;
		goto done;
	}

	if ((rc = ops[idx].init(context))) {
		plugin_context_destroy(g_context[idx]);
		g_context[idx] = NULL;
		goto done;
	}

	SWAP(loaded_types[idx], full_type);
	g_context_cnt = (idx + 1);

done:
	if (!rc)
		plugin_inited = PLUGIN_INITED;

	slurm_mutex_unlock(&init_lock);
	xfree(full_type);

	if (rc)
		*idx_ptr = RUNTIME_IDX_INVALID;
	else
		*idx_ptr = idx;

	return rc;
}

extern int runtime_g_init(const char *plugin_name, runtime_context_t context)
{
	int rc = EINVAL;
	int idx = RUNTIME_IDX_INVALID;

	if (plugin_inited != PLUGIN_NOT_INITED)
		return SLURM_SUCCESS;

	/*
	 * _load_runtime() sets plugin_inited itself, under init_lock, so a
	 * concurrent reader never observes it flip true before g_context_cnt
	 * and ops[] are fully published.
	 */
	if ((rc = _load_runtime(plugin_name, context, &idx)))
		return rc;

	/* The first plugin loaded always lands on the default index. */
	xassert(idx == RUNTIME_IDX_DEFAULT);

	return SLURM_SUCCESS;
}

extern void runtime_g_fini(void)
{
	slurm_mutex_lock(&init_lock);

	for (int i = 1; i < g_context_cnt; i++) {
		int rc = EINVAL;

		if (!g_context[i])
			continue;

		ops[i].fini();

		if ((rc = plugin_context_destroy(g_context[i])))
			fatal_abort("%s: plugin_context_destroy() failed: %s",
				__func__, slurm_strerror(rc));

		xfree(loaded_types[i]);
	}

	xfree(ops);
	xfree(g_context);
	xfree(loaded_types);
	g_context_cnt = 0;
	plugin_inited = PLUGIN_NOT_INITED;

	slurm_mutex_unlock(&init_lock);
}

extern int runtime_g_setup(slurmd_conf_t *conf, stepd_step_rec_t *step,
			   slurm_addr_t *cli, slurm_msg_t *msg)
{
	xassert(plugin_inited == PLUGIN_INITED);
	return ops[RUNTIME_IDX_DEFAULT].setup(conf, step, cli, msg);
}

extern void runtime_g_cleanup(slurmd_conf_t *conf, stepd_step_rec_t *step)
{
	xassert(plugin_inited == PLUGIN_INITED);
	ops[RUNTIME_IDX_DEFAULT].cleanup(conf, step);
}

extern void runtime_g_task_init(slurmd_conf_t *conf, stepd_step_rec_t *step,
				stepd_step_task_info_t *task)
{
	xassert(plugin_inited == PLUGIN_INITED);
	ops[RUNTIME_IDX_DEFAULT].task_init(conf, step, task);
}

extern int runtime_g_run(slurmd_conf_t *conf, stepd_step_rec_t *step,
			 stepd_step_task_info_t *task)
{
	int rc;

	xassert(plugin_inited == PLUGIN_INITED);

	rc = ops[RUNTIME_IDX_DEFAULT].run(conf, step, task);

	/* The plugin execs the task or returns ESLURM_NOT_SUPPORTED. */
	xassert(rc != SLURM_SUCCESS);

	return rc;
}
